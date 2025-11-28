#!/usr/bin/env python3
import sys, signal, serial, sounddevice as sd

PORT = '/dev/cu.usbmodem101'  # change if needed
BAUD = 115200
RATE = 16000
BLOCK_SAMPLES = 256           # matches N above

def stop(*_):
    try: stream.stop()
    except: pass
    try: ser.close()
    except: pass
    print("\nStopped."); sys.exit(0)

signal.signal(signal.SIGINT, stop)

ser = serial.Serial(PORT, BAUD, timeout=1)
stream = sd.RawOutputStream(samplerate=RATE, channels=1, dtype='int16')
stream.start()
print(f"Playing from {PORT} at {RATE} Hz (Ctrl+C to stop)")

need = 2*BLOCK_SAMPLES
buf = bytearray()
while True:
    chunk = ser.read(need - len(buf))
    if chunk: buf.extend(chunk)
    if len(buf) == need:
        stream.write(bytes(buf))
        buf.clear()
