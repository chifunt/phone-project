export interface Env {
  BRICKPHONE_TOKEN: string;
  OPENAI_API_KEY: string;
}

/**
 * Brickphone Voice Backend
 * - Device <-> Worker: custom WS protocol (JSON control + binary PCM frames)
 * - Worker <-> OpenAI: Realtime WS
 *
 * Audio: PCM16 mono, 24 kHz only end-to-end.
 */

type State = "idle" | "listening" | "thinking" | "speaking";

type HelloMsg = {
  type: "hello";
  device_id: string;
  auth: string;
  sample_rate: number;
  channels: number;
};

type ControlMsg =
  | { type: "start"; mode: "voice" }
  | { type: "stop" }
  | { type: "interrupt" }
  | { type: "ping"; t: number };

type ServerMsg =
  | { type: "ready"; session_id: string; sample_rate: number }
  | { type: "state"; value: State }
  | { type: "pong"; t: number }
  | { type: "event"; value: "barge_in" }
  | { type: "flow"; max_buffer_ms: number; action: "slow" | "resume" }
  | { type: "assistant_text"; text: string; final: boolean }
  | { type: "error"; code: string; message: string };

const MAGIC = 0xa0b1;
const VERSION = 1;

const SAMPLE_RATE = 24000;
const FRAME_SAMPLES = 480; // 20ms @ 24k
const MAX_BUFFER_MS = 400;

const OPENAI_MODEL = "gpt-realtime"; // alias; you can swap to a dated variant if you want
const OPENAI_URL = `wss://api.openai.com/v1/realtime?model=${OPENAI_MODEL}`;

export default {
  async fetch(request: Request, env: Env) {
    const url = new URL(request.url);

    if (url.pathname !== "/voice") return new Response("Not found", { status: 404 });
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected WebSocket", { status: 426 });
    }

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];

    handleSession(server, env);
    return new Response(null, { status: 101, webSocket: client });
  },
};

function handleSession(deviceWs: WebSocket, env: Env) {
  deviceWs.accept();

  let state: State = "idle";
  let helloOk = false;
  let sessionId = crypto.randomUUID();

  // Device stream tracking
  let clientLastSeq: number | null = null;
  let serverSeq = 0;
  const sessionStartMs = Date.now();

  // Idle timeout
  let lastActivityMs = Date.now();
  const idleTimer = setInterval(() => {
    if (Date.now() - lastActivityMs > 30000) {
      sendErrorAndClose("TIMEOUT", "idle timeout");
    }
  }, 1000) as unknown as number;

  // Simple flow signal based on last ~1s of received device audio
  let flowState: "slow" | "resume" = "resume";
  const frameWindow: { t: number; ms: number }[] = [];

  // OpenAI realtime ws
  let openaiWs: WebSocket | null = null;
  let openaiReady = false;

  // Outbound (to device) speech framing
  let outSpeechActive = false; // whether we already sent START_OF_UTTERANCE for current assistant speech

  const sendJson = (msg: ServerMsg) => deviceWs.send(JSON.stringify(msg));

  const setState = (next: State) => {
    state = next;
    sendJson({ type: "state", value: state });
  };

  const sendErrorAndClose = (code: string, message: string) => {
    try {
      sendJson({ type: "error", code, message });
    } catch {}
    try {
      deviceWs.close(1008, message);
    } catch {}
  };

  const updateFlow = (samples: number) => {
    const now = Date.now();
    const ms = (samples / SAMPLE_RATE) * 1000;
    frameWindow.push({ t: now, ms });
    while (frameWindow.length && now - frameWindow[0].t > 1000) frameWindow.shift();

    const sum = frameWindow.reduce((acc, cur) => acc + cur.ms, 0);
    if (sum > MAX_BUFFER_MS && flowState !== "slow") {
      flowState = "slow";
      sendJson({ type: "flow", max_buffer_ms: MAX_BUFFER_MS, action: "slow" });
    } else if (sum < 200 && flowState !== "resume") {
      flowState = "resume";
      sendJson({ type: "flow", max_buffer_ms: MAX_BUFFER_MS, action: "resume" });
    }
  };

  const openaiSend = (obj: unknown) => {
    if (!openaiWs || openaiWs.readyState !== WebSocket.OPEN) return;
    openaiWs.send(JSON.stringify(obj));
  };

  const connectOpenAI = async () => {
    const res = await fetch(OPENAI_URL, {
      headers: {
        Authorization: `Bearer ${env.OPENAI_API_KEY}`,
        "OpenAI-Beta": "realtime=v1",
      },
    });

    const ws = res.webSocket;
    if (!ws) {
      sendErrorAndClose("INTERNAL", "openai websocket not available");
      return;
    }

    openaiWs = ws;
    openaiWs.accept();
    openaiReady = true;

    // Configure session: 24k PCM16 in/out, no server VAD (device uses PTT start/stop)
    openaiSend({
      type: "session.update",
      session: {
        input_audio_format: "pcm16",
        output_audio_format: "pcm16",
        turn_detection: { type: "none" },
        voice: "alloy",
      },
    });

    openaiWs.addEventListener("message", (evt) => {
      const text = typeof evt.data === "string" ? evt.data : "";
      if (!text) return;

      let msg: any;
      try {
        msg = JSON.parse(text);
      } catch {
        return;
      }

      // Audio out from model
      if (msg.type === "response.audio.delta" && typeof msg.delta === "string") {
        const pcmBytes = base64ToBytes(msg.delta);
        streamPcmToDevice(pcmBytes);
        if (state !== "speaking") setState("speaking");
        return;
      }

      if (msg.type === "response.audio.done") {
        endAssistantUtterance();
        if (state !== "idle") setState("idle");
        return;
      }

      // Text out from model (optional)
      if (msg.type === "response.text.delta" && typeof msg.delta === "string") {
        sendJson({ type: "assistant_text", text: msg.delta, final: false });
        return;
      }

      if (msg.type === "response.text.done") {
        // Some variants include final text in different fields, so keep it conservative.
        const finalText = typeof msg.text === "string" ? msg.text : "";
        sendJson({ type: "assistant_text", text: finalText, final: true });
        return;
      }
    });

    openaiWs.addEventListener("close", () => {
      openaiReady = false;
      openaiWs = null;
      if (deviceWs.readyState === WebSocket.OPEN) {
        sendErrorAndClose("INTERNAL", "openai ws closed");
      }
    });
  };

  const buildDeviceFrame = (pcm: Int16Array, start: boolean, end: boolean) => {
    const header = new ArrayBuffer(12);
    const view = new DataView(header);

    const flags = (start ? 1 : 0) | (end ? 2 : 0);

    view.setUint16(0, MAGIC, true);
    view.setUint8(2, VERSION);
    view.setUint8(3, flags);
    view.setUint16(4, serverSeq, true);
    view.setUint16(6, pcm.length, true);
    view.setUint32(8, Date.now() - sessionStartMs, true);

    serverSeq = (serverSeq + 1) & 0xffff;

    const payload = pcm.buffer.slice(pcm.byteOffset, pcm.byteOffset + pcm.byteLength);
    return concatBuffers(header, payload);
  };

  const streamPcmToDevice = (pcmBytes: Uint8Array) => {
    // Chunk into 20ms frames for the ESP32
    const totalSamples = Math.floor(pcmBytes.length / 2);
    let off = 0;

    while (off < totalSamples) {
      const n = Math.min(FRAME_SAMPLES, totalSamples - off);
      const start = !outSpeechActive && off === 0;
      const end = false;

      const slice = pcmBytes.buffer.slice(
        pcmBytes.byteOffset + off * 2,
        pcmBytes.byteOffset + (off + n) * 2
      );
      const pcm = new Int16Array(slice);

      const frame = buildDeviceFrame(pcm, start, end);
      deviceWs.send(frame);

      outSpeechActive = true;
      off += n;
    }
  };

  const endAssistantUtterance = () => {
    if (!outSpeechActive) return;

    // Send explicit END frame with 0 samples
    const empty = new Int16Array(0);
    const frame = buildDeviceFrame(empty, false, true);
    deviceWs.send(frame);

    outSpeechActive = false;
  };

  // Device WS handler
  deviceWs.addEventListener("message", (event) => {
    lastActivityMs = Date.now();

    // JSON control messages
    if (typeof event.data === "string") {
      let msg: any;
      try {
        msg = JSON.parse(event.data);
      } catch {
        sendErrorAndClose("BAD_FORMAT", "invalid json");
        return;
      }

      // Handshake
      if (!helloOk) {
        if (msg?.type !== "hello") {
          sendErrorAndClose("BAD_FORMAT", "expected hello");
          return;
        }
        const hello = msg as HelloMsg;

        if (!hello.device_id || !hello.auth || hello.channels !== 1) {
          sendErrorAndClose("BAD_FORMAT", "invalid hello");
          return;
        }
        if (hello.auth !== env.BRICKPHONE_TOKEN) {
          sendErrorAndClose("AUTH_FAILED", "invalid token");
          return;
        }
        if (hello.sample_rate !== SAMPLE_RATE) {
          sendErrorAndClose("UNSUPPORTED_RATE", "sample_rate must be 24000");
          return;
        }

        helloOk = true;
        sessionId = crypto.randomUUID();
        sendJson({ type: "ready", session_id: sessionId, sample_rate: SAMPLE_RATE });

        // Connect upstream only after auth is good
        connectOpenAI();
        return;
      }

      const control = msg as ControlMsg;

      if (control.type === "ping") {
        sendJson({ type: "pong", t: control.t });
        return;
      }

      if (control.type === "start") {
        endAssistantUtterance(); // stop any leftover audio framing
        setState("listening");
        if (openaiReady) openaiSend({ type: "input_audio_buffer.clear" });
        return;
      }

      if (control.type === "stop") {
        setState("thinking");
        if (openaiReady) {
          openaiSend({ type: "input_audio_buffer.commit" });
          openaiSend({
            type: "response.create",
            response: {
              modalities: ["audio", "text"],
              instructions: "Respond concisely and clearly.",
            },
          });
        }
        return;
      }

      if (control.type === "interrupt") {
        // Barge in: stop TTS immediately
        endAssistantUtterance();
        setState("listening");
        sendJson({ type: "event", value: "barge_in" });
        if (openaiReady) {
          openaiSend({ type: "response.cancel" });
          openaiSend({ type: "input_audio_buffer.clear" });
        }
        return;
      }

      // Unknown control
      return;
    }

    // Binary audio frames from device
    if (!(event.data instanceof ArrayBuffer)) {
      sendErrorAndClose("BAD_FORMAT", "unsupported frame");
      return;
    }
    if (!helloOk) {
      sendErrorAndClose("BAD_FORMAT", "binary before hello");
      return;
    }

    const buf = event.data;
    if (buf.byteLength < 12) {
      sendErrorAndClose("BAD_FORMAT", "short frame");
      return;
    }

    const view = new DataView(buf);
    const magic = view.getUint16(0, true);
    const version = view.getUint8(2);
    const seq = view.getUint16(4, true);
    const samples = view.getUint16(6, true);

    const expectedLen = 12 + samples * 2;
    if (magic !== MAGIC || version !== VERSION || expectedLen !== buf.byteLength) {
      sendErrorAndClose("BAD_FORMAT", "invalid frame");
      return;
    }

    if (clientLastSeq !== null) {
      const expected = (clientLastSeq + 1) & 0xffff;
      if (seq !== expected) {
        // warn but continue
        sendJson({ type: "error", code: "BAD_SEQ", message: "seq gap" });
      }
    }
    clientLastSeq = seq;

    updateFlow(samples);

    if (openaiReady) {
      const payload = new Uint8Array(buf.slice(12));
      openaiSend({
        type: "input_audio_buffer.append",
        audio: bytesToBase64(payload),
      });
    }
  });

  deviceWs.addEventListener("close", () => {
    try {
      if (idleTimer !== null) clearInterval(idleTimer);
    } catch {}

    try {
      if (openaiWs) openaiWs.close();
    } catch {}
    openaiWs = null;
    openaiReady = false;
  });
}

function concatBuffers(a: ArrayBuffer, b: ArrayBuffer) {
  const out = new Uint8Array(a.byteLength + b.byteLength);
  out.set(new Uint8Array(a), 0);
  out.set(new Uint8Array(b), a.byteLength);
  return out.buffer;
}

// Safer base64 helpers for Workers (avoid huge spread ops)
function bytesToBase64(bytes: Uint8Array) {
  const chunkSize = 0x8000;
  let binary = "";
  for (let i = 0; i < bytes.length; i += chunkSize) {
    const chunk = bytes.subarray(i, i + chunkSize);
    for (let j = 0; j < chunk.length; j++) binary += String.fromCharCode(chunk[j]);
  }
  return btoa(binary);
}

function base64ToBytes(b64: string) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
