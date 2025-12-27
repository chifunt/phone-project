export interface Env {
  BRICKPHONE_TOKEN: string;
  OPENAI_API_KEY: string;
}

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
  | { type: "error"; code: string; message: string };

const MAGIC = 0xa0b1;
const VERSION = 1;
const OPENAI_MODEL = "gpt-4o-realtime-preview";
const OPENAI_URL = `wss://api.openai.com/v1/realtime?model=${OPENAI_MODEL}`;

export default {
  async fetch(request: Request, env: Env) {
    const url = new URL(request.url);
    if (url.pathname !== "/voice") {
      return new Response("Not found", { status: 404 });
    }
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

function handleSession(ws: WebSocket, env: Env) {
  ws.accept();

  let state: State = "idle";
  let sessionId = "";
  let sampleRate = 24000;
  let helloOk = false;
  let clientLastSeq: number | null = null;
  let serverSeq = 0;
  let sessionStartMs = Date.now();
  let lastActivityMs = Date.now();
  let idleTimer: number | null = null;
  let ttsTimer: number | null = null;
  let flowState: "slow" | "resume" = "resume";
  const frameWindow: { t: number; ms: number }[] = [];
  let openaiWs: WebSocket | null = null;
  let openaiReady = false;
  let speaking = false;

  const sendJson = (msg: ServerMsg) => {
    ws.send(JSON.stringify(msg));
  };

  const setState = (next: State) => {
    state = next;
    sendJson({ type: "state", value: state });
  };

  const sendErrorAndClose = (code: string, message: string) => {
    sendJson({ type: "error", code, message });
    ws.close(1008, message);
  };

  const stopTts = () => {
    if (ttsTimer !== null) {
      clearTimeout(ttsTimer);
      ttsTimer = null;
    }
  };

  const buildPcmFrame = (pcm: Int16Array, start: boolean, end: boolean) => {
    const header = new ArrayBuffer(12);
    const view = new DataView(header);
    const flags = (start ? 1 : 0) | (end ? 2 : 0);
    const samples = pcm.length;
    view.setUint16(0, MAGIC, true);
    view.setUint8(2, VERSION);
    view.setUint8(3, flags);
    view.setUint16(4, serverSeq, true);
    view.setUint16(6, samples, true);
    view.setUint32(8, Date.now() - sessionStartMs, true);
    serverSeq = (serverSeq + 1) & 0xffff;

    const payload = pcm.buffer.slice(pcm.byteOffset, pcm.byteOffset + pcm.byteLength);
    return concatBuffers(header, payload);
  };

  const updateFlow = (samples: number) => {
    const now = Date.now();
    const ms = (samples / sampleRate) * 1000;
    frameWindow.push({ t: now, ms });
    while (frameWindow.length && now - frameWindow[0].t > 1000) {
      frameWindow.shift();
    }
    const sum = frameWindow.reduce((acc, cur) => acc + cur.ms, 0);
    if (sum > 400 && flowState !== "slow") {
      flowState = "slow";
      sendJson({ type: "flow", max_buffer_ms: 400, action: "slow" });
    } else if (sum < 200 && flowState !== "resume") {
      flowState = "resume";
      sendJson({ type: "flow", max_buffer_ms: 400, action: "resume" });
    }
  };

  idleTimer = setInterval(() => {
    if (Date.now() - lastActivityMs > 30000) {
      sendErrorAndClose("TIMEOUT", "idle timeout");
    }
  }, 1000) as unknown as number;

  const openaiSend = (msg: unknown) => {
    if (!openaiWs || openaiWs.readyState !== WebSocket.OPEN) return;
    openaiWs.send(JSON.stringify(msg));
  };

  const connectOpenAI = async () => {
    const res = await fetch(OPENAI_URL, {
      headers: {
        Authorization: `Bearer ${env.OPENAI_API_KEY}`,
        "OpenAI-Beta": "realtime=v1",
      },
    });
    const wsOpenAI = res.webSocket;
    if (!wsOpenAI) {
      sendErrorAndClose("INTERNAL", "openai ws failed");
      return;
    }
    openaiWs = wsOpenAI;
    openaiWs.accept();
    openaiReady = true;

    openaiSend({
      type: "session.update",
      session: {
        input_audio_format: "pcm16",
        output_audio_format: "pcm16",
        input_audio_transcription: { model: "gpt-4o-transcribe" },
        turn_detection: { type: "none" },
        voice: "alloy",
      },
    });

    openaiWs.addEventListener("message", (evt) => {
      const data = typeof evt.data === "string" ? evt.data : "";
      if (!data) return;
      let msg: any;
      try {
        msg = JSON.parse(data);
      } catch {
        return;
      }

      if (msg.type === "response.audio.delta") {
        const pcmBytes = base64ToBytes(msg.delta);
        streamPcmToClient(pcmBytes);
        if (!speaking) {
          speaking = true;
          setState("speaking");
        }
      } else if (msg.type === "response.audio.done") {
        speaking = false;
        setState("idle");
      } else if (msg.type === "response.text.delta") {
        sendJson({ type: "assistant_text", text: msg.delta, final: false });
      } else if (msg.type === "response.text.done") {
        sendJson({ type: "assistant_text", text: msg.text ?? "", final: true });
      } else if (msg.type === "input_audio_buffer.speech_started") {
        setState("listening");
      }
    });

    openaiWs.addEventListener("close", () => {
      openaiReady = false;
      openaiWs = null;
      if (ws.readyState === WebSocket.OPEN) {
        sendErrorAndClose("INTERNAL", "openai ws closed");
      }
    });
  };

  const streamPcmToClient = (pcmBytes: Uint8Array) => {
    const totalSamples = Math.floor(pcmBytes.length / 2);
    let sampleOffset = 0;
    while (sampleOffset < totalSamples) {
      const frameSamples = Math.min(480, totalSamples - sampleOffset);
      const start = sampleOffset === 0;
      const end = sampleOffset + frameSamples >= totalSamples;
      const pcm = new Int16Array(pcmBytes.buffer.slice(
        pcmBytes.byteOffset + sampleOffset * 2,
        pcmBytes.byteOffset + (sampleOffset + frameSamples) * 2
      ));
      const frame = buildPcmFrame(pcm, start, end);
      ws.send(frame);
      sampleOffset += frameSamples;
    }
  };

  ws.addEventListener("message", (event) => {
    lastActivityMs = Date.now();
    if (typeof event.data === "string") {
      let msg: any;
      try {
        msg = JSON.parse(event.data);
      } catch {
        sendErrorAndClose("BAD_FORMAT", "invalid json");
        return;
      }

      if (!helloOk) {
        if (msg?.type !== "hello") {
          sendErrorAndClose("BAD_FORMAT", "expected hello");
          return;
        }
        const hello = msg as HelloMsg;
        if (!hello.device_id || !hello.auth || !hello.sample_rate || hello.channels !== 1) {
          sendErrorAndClose("BAD_FORMAT", "invalid hello");
          return;
        }
        if (hello.auth !== env.BRICKPHONE_TOKEN) {
          sendErrorAndClose("AUTH_FAILED", "invalid token");
          return;
        }
        if (hello.sample_rate !== 24000) {
          sendErrorAndClose("UNSUPPORTED_RATE", "unsupported sample rate");
          return;
        }
        helloOk = true;
        sampleRate = 24000;
        sessionId = crypto.randomUUID();
        sessionStartMs = Date.now();
        sendJson({ type: "ready", session_id: sessionId, sample_rate: 24000 });
        connectOpenAI();
        return;
      }

      const control = msg as ControlMsg;
      if (control.type === "ping") {
        sendJson({ type: "pong", t: control.t });
        return;
      }
      if (control.type === "start") {
        stopTts();
        setState("listening");
        if (openaiReady) {
          openaiSend({ type: "input_audio_buffer.clear" });
        }
        return;
      }
      if (control.type === "stop") {
        setState("thinking");
        stopTts();
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
        stopTts();
        setState("listening");
        sendJson({ type: "event", value: "barge_in" });
        if (openaiReady) {
          openaiSend({ type: "response.cancel" });
          openaiSend({ type: "input_audio_buffer.clear" });
        }
        return;
      }
    } else if (event.data instanceof ArrayBuffer) {
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
          sendJson({ type: "error", code: "BAD_FORMAT", message: "seq gap" });
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
    } else {
      sendErrorAndClose("BAD_FORMAT", "unsupported frame");
    }
  });

  ws.addEventListener("close", () => {
    if (idleTimer !== null) clearInterval(idleTimer);
    stopTts();
    if (openaiWs) {
      openaiWs.close();
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
  let binary = "";
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return btoa(binary);
}

function base64ToBytes(b64: string) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) {
    out[i] = bin.charCodeAt(i);
  }
  return out;
}
