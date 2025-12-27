# Brickphone Voice Backend Spec v0.1.1

Cloudflare Workers WebSocket protocol for low-latency PCM16 mono audio streaming between ESP32 device and backend speech pipeline.

## Scope
- Single WebSocket per device session
- PCM16 mono streaming in both directions
- Lightweight auth, flow control, and debuggability
- Suitable for ESP32 and Workers

## Transport
- URL: `wss://<worker-domain>/voice`
- One device per WS
- JSON messages for control/state
- Binary messages for audio frames (with header)

## Audio Format
- PCM16, mono
- Sample rate: 16000 or 24000 Hz (negotiated; server chooses and client must use it for the session)
- Little-endian samples
- Recommended frame duration: 20 ms (e.g., 480 samples @ 24 kHz)

## Authentication
Device sends a lightweight token in `hello`:
- `auth`: pre-shared token per device (short string)
- `device_id`: unique ID
Server rejects if token invalid.

## Binary Audio Frame
Each binary frame includes a small header followed by raw PCM16 samples.

Header (12 bytes, all fields little-endian):
- `u16` magic = 0xA0B1
- `u8`  version = 1
- `u8`  flags (bitfield)
- `u16` seq (incrementing per direction, wraps; gap detection must handle wrap)
- `u16` samples (number of PCM samples in this frame)
- `u32` timestamp_ms (set by sender; monotonic time since session start using sender’s clock)

Flags:
- bit0: START_OF_UTTERANCE (set on first audio frame after JSON `start`)
- bit1: END_OF_UTTERANCE (set on final audio frame before JSON `stop`)
- bit2: DROPPED (set by sender if it dropped frames since last send in this direction)
- bit3: RESERVED

Payload:
- `samples * 2` bytes of PCM16

## JSON Messages
Client -> Server:
- `{"type":"hello","device_id":"<id>","auth":"<token>","sample_rate":24000,"channels":1}`
- `{"type":"start","mode":"voice"}`
- `{"type":"interrupt"}`
- `{"type":"stop"}`
- `{"type":"ping","t":<ms>}`

Server -> Client:
- `{"type":"ready","session_id":"<id>","sample_rate":24000}`
- `{"type":"error","code":"AUTH_FAILED","message":"..."}`
- `{"type":"state","value":"idle|listening|thinking|speaking"}`
- `{"type":"event","value":"barge_in"}`
- `{"type":"transcript","text":"...","final":true}`
- `{"type":"assistant_text","text":"...","final":true}`
- `{"type":"pong","t":<ms>}`
- `{"type":"flow","max_buffer_ms":400,"action":"slow|resume"}`

## Flow Control
- Client should keep outbound buffered audio under 200–400 ms.
- Server may send `flow` to request throttling:
  - `action:"slow"`: pause capture or drop oldest buffered frames to stay under `max_buffer_ms`.
  - `action:"resume"`: return to normal.
- If server buffer exceeds `max_buffer_ms`, it may drop oldest buffered frames and set `DROPPED` in the next outbound frame.

## Barge-In / Interruption
- Client can send `{"type":"interrupt"}` to barge in.
- Server stops TTS playback immediately, flushes queued outbound audio, and transitions to `state: listening`.
- Server should send `{"type":"event","value":"barge_in"}` when interrupt occurs.

## Keepalive
- Client sends `ping` every 10–15 seconds when idle.
- Server responds with `pong` echoing the same `t` for RTT measurement.
- If no activity for 30 seconds, server may close the WS and should send `{"type":"error","code":"TIMEOUT","message":"idle timeout"}` when possible.

## Error Codes
- `AUTH_FAILED` invalid token
- `BAD_FORMAT` invalid JSON or frame header
- `UNSUPPORTED_RATE` sample rate not supported
- `BUFFER_OVERFLOW` server buffer exceeded
- `TIMEOUT` idle timeout
- `INTERNAL` generic error

## Session Behavior
1) Client connects and sends `hello`
2) Server replies `ready` (or `error`)
3) Client sends `start` and begins audio frames
4) Server sends `state` updates and optional transcripts
5) Server may stream PCM response frames (TTS) with header
6) Client sends `stop` to end capture

## Notes
- Use 20 ms frames for a good balance of latency and overhead.
- Device should display `state` transitions on the OLED.
- Server owns all AI logic and API keys; device never receives secrets.

## Worker Config
- Required secrets:
  - `BRICKPHONE_TOKEN` (device auth)
  - `OPENAI_API_KEY`
- Model: `gpt-4o-realtime-preview` over OpenAI Realtime WebSocket
