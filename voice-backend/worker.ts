export interface Env {
  BRICKPHONE_TOKEN: string;
  OPENAI_API_KEY: string;

  // Optional: set in your Worker env vars if you want
  // REALTIME_MODEL?: string;
  // ALWAYS_ON?: string; // "1" to enable always-on server VAD
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
  | { type: "assistant_text"; text: string; final: boolean }
  | { type: "error"; code: string; message: string };

const MAGIC = 0xa0b1;
const VERSION = 1;

const SAMPLE_RATE = 24000;
const FRAME_SAMPLES = 480; // 20 ms at 24 kHz

function getModel(env: Env) {
  // Docs show "gpt-realtime" and pinned variants like "gpt-realtime-2025-08-25". :contentReference[oaicite:2]{index=2}
  // If you tried "gpt-realtime-mini" and it hangs, this is the first thing to change.
  // @ts-expect-error optional env
  return (env.REALTIME_MODEL as string | undefined) || "gpt-realtime";
}

function getAlwaysOn(env: Env) {
  // @ts-expect-error optional env
  return (env.ALWAYS_ON as string | undefined) === "1";
}

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

  const ALWAYS_ON = getAlwaysOn(env);
  const MODEL = getModel(env);
  const OPENAI_URL = `wss://api.openai.com/v1/realtime?model=${encodeURIComponent(MODEL)}`;

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

  const sendDeviceJson = (msg: ServerMsg) => deviceWs.send(JSON.stringify(msg));

  const setState = (next: State) => {
    state = next;
    sendDeviceJson({ type: "state", value: state });
  };

  const sendErrorAndClose = (code: string, message: string) => {
    sendDeviceJson({ type: "error", code, message });
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

    if (sum > 400 && flowState !== "slow") {
      flowState = "slow";
      sendDeviceJson({ type: "flow", max_buffer_ms: 400, action: "slow" });
    } else if (sum < 200 && flowState !== "resume") {
      flowState = "resume";
      sendDeviceJson({ type: "flow", max_buffer_ms: 400, action: "resume" });
    }
  };

  idleTimer = setInterval(() => {
    if (Date.now() - lastActivityMs > 30000) sendErrorAndClose("TIMEOUT", "idle timeout");
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
    // Workers way: fetch() returns { webSocket } when using wss url. :contentReference[oaicite:3]{index=3}
    const res = await fetch(OPENAI_URL, {
      headers: {
        Authorization: `Bearer ${env.OPENAI_API_KEY}`,
        "OpenAI-Beta": "realtime=v1",
      },
    });

    const ws = res.webSocket;
    if (!ws) {
      sendErrorAndClose("INTERNAL", "openai websocket unavailable");
      return;
    }

    openaiWs = ws;
    openaiWs.accept();
    openaiReady = true;

    // Current session schema uses audio.input/output with format.type audio/pcm and rate. :contentReference[oaicite:4]{index=4}
    const turnDetection = ALWAYS_ON
      ? {
          type: "server_vad",
          threshold: 0.5,
          prefix_padding_ms: 300,
          silence_duration_ms: 200,
          create_response: true,
          interrupt_response: true,
        }
      : { type: "none" };

    openaiSend({
      type: "session.update",
      session: {
        type: "realtime",
        model: MODEL,
        instructions: "Respond concisely and clearly.",
        audio: {
          input: {
            format: { type: "audio/pcm", rate: 24000 },
            turn_detection: turnDetection,
          },
          output: {
            format: { type: "audio/pcm", rate: 24000 },
            voice: "alloy",
            speed: 1.0,
          },
        },
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

      // Always forward OpenAI errors so you can see invalid model, auth, etc
      if (msg.type === "error") {
        const m = msg?.error?.message ?? msg?.message ?? "openai error";
        sendDeviceJson({ type: "error", code: "INTERNAL", message: String(m) });
        return;
      }

      // These are the GA event names in the docs. :contentReference[oaicite:5]{index=5}
      if (msg.type === "response.output_audio.delta") {
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

      if (msg.type === "response.output_audio.done") {
        flushHeldOutFrame(true);
        setState("idle");
        return;
      }

      if (msg.type === "response.output_text.delta") {
        sendDeviceJson({ type: "assistant_text", text: String(msg.delta ?? ""), final: false });
        return;
      }
      if (msg.type === "response.output_text.done") {
        sendDeviceJson({ type: "assistant_text", text: String(msg.text ?? ""), final: true });
        return;
      }

      if (msg.type === "input_audio_buffer.speech_started") {
        setState("listening");
        return;
      }
    });

    openaiWs.addEventListener("close", () => {
      openaiReady = false;
      openaiWs = null;
      if (deviceWs.readyState === WebSocket.OPEN) sendErrorAndClose("INTERNAL", "openai ws closed");
    });
  };

  deviceWs.addEventListener("message", (event) => {
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
        if (msg?.type !== "hello") return sendErrorAndClose("BAD_FORMAT", "expected hello");

        const hello = msg as HelloMsg;
        if (!hello.device_id || !hello.auth || hello.channels !== 1) {
          return sendErrorAndClose("BAD_FORMAT", "invalid hello");
        }
        if (hello.auth !== env.BRICKPHONE_TOKEN) return sendErrorAndClose("AUTH_FAILED", "bad token");
        if (hello.sample_rate !== SAMPLE_RATE) {
          return sendErrorAndClose("UNSUPPORTED_RATE", "sample_rate must be 24000");
        }

        helloOk = true;
        sessionId = crypto.randomUUID();
        sessionStartMs = Date.now();
        sendDeviceJson({ type: "ready", session_id: sessionId, sample_rate: SAMPLE_RATE });
        connectOpenAI();
        return;
      }

      const control = msg as ControlMsg;

      if (control.type === "ping") {
        sendDeviceJson({ type: "pong", t: control.t });
        return;
      }

      // In ALWAYS_ON mode these are optional, but keeping them makes PTT work too
      if (control.type === "start") {
        setState("listening");
        outUtteranceActive = false;
        heldOutFrame = null;
        if (openaiReady) openaiSend({ type: "input_audio_buffer.clear" });
        return;
      }

      if (control.type === "stop") {
        setState("thinking");
        if (!ALWAYS_ON && openaiReady) {
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
