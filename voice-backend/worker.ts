export interface Env {
  BRICKPHONE_TOKEN: string;
  OPENAI_API_KEY: string;
}

/**
 * Device protocol:
 * - Client sends JSON hello/start/stop/interrupt/ping
 * - Client sends binary frames: 12-byte header + PCM16 mono payload
 * - Server sends JSON ready/state/pong/flow/error (+ optional assistant_text)
 * - Server sends binary frames back: same 12-byte header + PCM16 mono payload
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

const DEVICE_SAMPLE_RATE = 24000;
const FRAME_SAMPLES = 480; // 20ms at 24k

// Pick a model you actually have access to.
// If this is wrong or not enabled for your key, you will see it in logs now.
const OPENAI_MODEL = "gpt-realtime";
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

  let clientLastSeq: number | null = null;
  let serverSeq = 0;
  let sessionStartMs = Date.now();
  let lastActivityMs = Date.now();

  let idleTimer: number | null = null;
  let flowState: "slow" | "resume" = "resume";
  const frameWindow: { t: number; ms: number }[] = [];

  let openaiWs: WebSocket | null = null;
  let openaiReady = false;

  // Output audio: one-frame lookahead so END is never an empty frame
  let outUtteranceActive = false;
  let heldOutFrame: Int16Array | null = null;

  const sendDeviceJson = (msg: ServerMsg) => {
    deviceWs.send(JSON.stringify(msg));
  };

  const setState = (next: State) => {
    state = next;
    sendDeviceJson({ type: "state", value: state });
  };

  const sendErrorAndClose = (code: string, message: string) => {
    console.log("closing:", code, message);
    try {
      sendDeviceJson({ type: "error", code, message });
    } catch {}
    try {
      deviceWs.close(1008, message);
    } catch {}
  };

  const updateFlow = (samples: number) => {
    const now = Date.now();
    const ms = (samples / DEVICE_SAMPLE_RATE) * 1000;
    frameWindow.push({ t: now, ms });
    while (frameWindow.length && now - frameWindow[0].t > 1000) frameWindow.shift();
    const sum = frameWindow.reduce((acc, cur) => acc + cur.ms, 0);

    if (sum > 400 && flowState !== "slow") {
      flowState = "slow";
      sendDeviceJson({ type: "flow", max_buffer_ms: 400, action: "slow" });
    } else if (sum < 200 && flowState !== "resume") {
      flowState = "resume";
      sendDeviceJson({ type: "flow", max_buffer_ms: 400, action: "resume" });
    }
  };

  idleTimer = setInterval(() => {
    if (Date.now() - lastActivityMs > 30000) {
      sendErrorAndClose("TIMEOUT", "idle timeout");
    }
  }, 1000) as unknown as number;

  const buildDevicePcmFrame = (pcm: Int16Array, start: boolean, end: boolean) => {
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

  const flushHeldOutFrame = (end: boolean) => {
    if (!heldOutFrame) return;
    const start = !outUtteranceActive;
    const frame = buildDevicePcmFrame(heldOutFrame, start, end);
    deviceWs.send(frame);

    outUtteranceActive = true;
    heldOutFrame = null;

    if (end) outUtteranceActive = false;
  };

  const openaiSend = (msg: unknown) => {
    if (!openaiWs || openaiWs.readyState !== WebSocket.OPEN) return;
    openaiWs.send(JSON.stringify(msg));
  };

  const connectOpenAI = async () => {
    console.log("OpenAI connect:", OPENAI_URL);

    const res = await fetch(OPENAI_URL, {
      headers: {
        Authorization: `Bearer ${env.OPENAI_API_KEY}`,
        "OpenAI-Beta": "realtime=v1",
      },
    });

    if (!res.webSocket) {
      const body = await res.text().catch(() => "");
      console.log("OpenAI websocket unavailable. status=", res.status, "body=", body);
      sendErrorAndClose("OPENAI_CONNECT", `OpenAI connect failed: ${res.status}`);
      return;
    }

    openaiWs = res.webSocket;
    openaiWs.accept();
    openaiReady = true;

    // Ask for both audio and text so debugging is easier.
    // Turn detection none because you are doing PTT (start/stop).
    openaiSend({
      type: "session.update",
      session: {
        modalities: ["audio", "text"],
        voice: "alloy",
        input_audio_format: "pcm16",
        output_audio_format: "pcm16",
        turn_detection: { type: "none" },
        input_audio_transcription: { model: "gpt-4o-transcribe" },
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

      const t = String(msg.type ?? "");
      if (t) console.log("OpenAI event:", t);

      if (t === "error") {
        const m = msg?.error?.message ?? "openai error";
        sendDeviceJson({ type: "error", code: "OPENAI", message: String(m) });
        return;
      }

      // Audio out (these are the events your earlier code was expecting)
      if (t === "response.audio.delta") {
        const pcmBytes = base64ToBytes(String(msg.delta ?? ""));
        if (pcmBytes.length) {
          const samples = new Int16Array(
            pcmBytes.buffer.slice(pcmBytes.byteOffset, pcmBytes.byteOffset + pcmBytes.byteLength)
          );

          let i = 0;
          while (i < samples.length) {
            const take = Math.min(FRAME_SAMPLES, samples.length - i);
            const chunk = samples.subarray(i, i + take);

            if (heldOutFrame) flushHeldOutFrame(false);
            heldOutFrame = new Int16Array(chunk);

            i += take;
          }

          if (state !== "speaking") setState("speaking");
        }
        return;
      }

      if (t === "response.audio.done") {
        flushHeldOutFrame(true);
        setState("idle");
        return;
      }

      // Text out for debugging
      if (t === "response.text.delta") {
        sendDeviceJson({ type: "assistant_text", text: String(msg.delta ?? ""), final: false });
        return;
      }
      if (t === "response.text.done") {
        sendDeviceJson({ type: "assistant_text", text: String(msg.text ?? ""), final: true });
        return;
      }
    });

    openaiWs.addEventListener("close", () => {
      console.log("OpenAI ws closed");
      openaiReady = false;
      openaiWs = null;
      if (deviceWs.readyState === WebSocket.OPEN) {
        sendErrorAndClose("OPENAI_CLOSED", "OpenAI websocket closed");
      }
    });
  };

  deviceWs.addEventListener("message", (event) => {
    lastActivityMs = Date.now();

    // JSON control
    if (typeof event.data === "string") {
      let msg: any;
      try {
        msg = JSON.parse(event.data);
      } catch {
        sendErrorAndClose("BAD_FORMAT", "invalid json");
        return;
      }

      // First message must be hello
      if (!helloOk) {
        if (msg?.type !== "hello") return sendErrorAndClose("BAD_FORMAT", "expected hello");

        const hello = msg as HelloMsg;
        if (!hello.device_id || !hello.auth || hello.channels !== 1) {
          return sendErrorAndClose("BAD_FORMAT", "invalid hello");
        }
        if (hello.auth !== env.BRICKPHONE_TOKEN) return sendErrorAndClose("AUTH_FAILED", "bad token");
        if (hello.sample_rate !== DEVICE_SAMPLE_RATE) {
          return sendErrorAndClose("UNSUPPORTED_RATE", "sample_rate must be 24000");
        }

        helloOk = true;
        sessionId = crypto.randomUUID();
        sessionStartMs = Date.now();
        sendDeviceJson({ type: "ready", session_id: sessionId, sample_rate: DEVICE_SAMPLE_RATE });

        // Fire and forget but do not hide failures
        connectOpenAI().catch((e) => {
          console.log("connectOpenAI exception:", e);
          sendErrorAndClose("OPENAI_CONNECT", "OpenAI connect exception");
        });
        return;
      }

      const control = msg as ControlMsg;

      if (control.type === "ping") {
        sendDeviceJson({ type: "pong", t: control.t });
        return;
      }

      if (control.type === "start") {
        setState("listening");
        outUtteranceActive = false;
        heldOutFrame = null;
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
        } else {
          sendDeviceJson({ type: "error", code: "OPENAI", message: "OpenAI not ready" });
        }
        return;
      }

      if (control.type === "interrupt") {
        setState("listening");
        sendDeviceJson({ type: "event", value: "barge_in" });
        outUtteranceActive = false;
        heldOutFrame = null;
        if (openaiReady) {
          openaiSend({ type: "response.cancel" });
          openaiSend({ type: "input_audio_buffer.clear" });
        }
        return;
      }

      return;
    }

    // Binary audio frames from device
    if (event.data instanceof ArrayBuffer) {
      if (!helloOk) return sendErrorAndClose("BAD_FORMAT", "binary before hello");

      const buf = event.data;
      if (buf.byteLength < 12) return sendErrorAndClose("BAD_FORMAT", "short frame");

      const view = new DataView(buf);
      const magic = view.getUint16(0, true);
      const version = view.getUint8(2);
      const seq = view.getUint16(4, true);
      const samples = view.getUint16(6, true);

      const expectedLen = 12 + samples * 2;
      if (magic !== MAGIC || version !== VERSION || expectedLen !== buf.byteLength) {
        return sendErrorAndClose("BAD_FORMAT", "invalid frame");
      }

      if (clientLastSeq !== null) {
        const expected = (clientLastSeq + 1) & 0xffff;
        if (seq !== expected) {
          sendDeviceJson({ type: "error", code: "BAD_FORMAT", message: "seq gap" });
        }
      }
      clientLastSeq = seq;

      updateFlow(samples);

      if (openaiReady && samples > 0) {
        const payload = new Uint8Array(buf.slice(12));
        openaiSend({ type: "input_audio_buffer.append", audio: bytesToBase64(payload) });
      }
      return;
    }

    sendErrorAndClose("BAD_FORMAT", "unsupported frame");
  });

  deviceWs.addEventListener("close", () => {
    if (idleTimer !== null) clearInterval(idleTimer);
    if (openaiWs) {
      try {
        openaiWs.close();
      } catch {}
      openaiWs = null;
    }
  });
}

function concatBuffers(a: ArrayBuffer, b: ArrayBuffer) {
  const out = new Uint8Array(a.byteLength + b.byteLength);
  out.set(new Uint8Array(a), 0);
  out.set(new Uint8Array(b), a.byteLength);
  return out.buffer;
}

function bytesToBase64(bytes: Uint8Array) {
  const chunkSize = 0x8000;
  let binary = "";
  for (let i = 0; i < bytes.length; i += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunkSize));
  }
  return btoa(binary);
}

function base64ToBytes(b64: string) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
