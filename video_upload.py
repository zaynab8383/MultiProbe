#!/usr/bin/env python3
"""
MultiProbe - convert a GIF/MP4 and upload it to the board's W25Q64 flash over UART4.

Requirements (PC):
    pip install pyserial
    ffmpeg in PATH (https://ffmpeg.org)

Wiring: USB-serial adapter (3.3 V logic!) -> board "UART" header:
    adapter RX <- PA0 (UART4_TX),  adapter TX -> PA1 (UART4_RX),  GND <-> GND

Usage:
    python video_upload.py cat.mp4 COM5                 # convert + upload (120x120, drawn 2x = fullscreen)
    python video_upload.py cat.mp4 COM5 --size 160      # 160x160, drawn 1x, centered on the screen
    python video_upload.py cat.mp4 --no-upload          # only check that it fits, write video.bin

Then START THE BOARD (or reset it) while the script says "waiting for the board".
The board listens for about 3 seconds after boot (logo on screen).
"""

import argparse
import struct
import subprocess
import sys
import time
import zlib

LCD = 240
BLOCK = 4096
FLASH_CAPACITY = 8 * 1024 * 1024
VIDEO_DATA_ADDR = 0x00200000 + 0x1000
VIDEO_MAX_BYTES = FLASH_CAPACITY - VIDEO_DATA_ADDR       # 0x5FF000

MAGIC_HANDSHAKE = b"MPVUPLD1"
VIDEO_MAGIC = 0x4456504D                                 # "MPVD"


def convert(path, size, fps, max_frames):
    """Return the raw RGB565 big-endian frames (all frames back to back)."""
    vf = (f"fps={fps},"
          f"scale={size}:{size}:force_original_aspect_ratio=increase:flags=lanczos,"
          f"crop={size}:{size}")
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-vf", vf]
    if max_frames:
        cmd += ["-frames:v", str(max_frames)]
    cmd += ["-f", "rawvideo", "-pix_fmt", "rgb565be", "-"]
    try:
        out = subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout
    except FileNotFoundError:
        sys.exit("ffmpeg was not found. Install it and add it to PATH.")
    except subprocess.CalledProcessError:
        sys.exit("ffmpeg failed to read the input file.")
    return out


def build_header(size, scale, fps, frame_count, data):
    frame_bytes = size * size * 2
    head = struct.pack("<IHHHBBHHIIII",
                       VIDEO_MAGIC, 1, size, size, scale, 0,
                       int(round(fps * 10)), 0,
                       frame_count, frame_bytes, len(data),
                       zlib.crc32(data) & 0xFFFFFFFF)
    assert len(head) == 32
    return head + struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF)


def read_until(ser, wanted, timeout):
    """Read bytes until one of `wanted` (bytes) arrives; return it, or None on timeout."""
    end = time.time() + timeout
    while time.time() < end:
        b = ser.read(1)
        if b and b in wanted:
            return b
    return None


def upload(port, baud, header, data, wait_s):
    import serial   # imported here so --no-upload works without pyserial

    ser = serial.Serial(port, baud, timeout=0.05, write_timeout=5)
    print(f"Waiting for the board on {port} ({wait_s:.0f} s) - start / reset the board now ...")

    end = time.time() + wait_s
    ready = False
    while time.time() < end:
        ser.write(MAGIC_HANDSHAKE)
        if read_until(ser, b"R", 0.05) == b"R":
            ready = True
            break
    if not ready:
        sys.exit("The board did not answer. Check wiring (TX/RX crossed?), baud and that it is booting.")

    print("Board is ready, sending header ...")
    time.sleep(0.3)                       # the board fills the LCD blue and drains the line
    ser.reset_input_buffer()
    ser.write(header)
    ser.timeout = 0.5
    answer = read_until(ser, b"KE", 3)
    if answer != b"K":
        sys.exit("The board rejected the header (clip too big or corrupt header).")

    blocks = (len(data) + BLOCK - 1) // BLOCK
    t0 = time.time()
    for i in range(blocks):
        chunk = data[i * BLOCK:(i + 1) * BLOCK]
        chunk = chunk + b"\xff" * (BLOCK - len(chunk))
        packet = chunk + struct.pack("<I", zlib.crc32(chunk) & 0xFFFFFFFF)

        for attempt in range(10):
            ser.write(packet)
            r = read_until(ser, b"KNE", 10)
            if r == b"K":
                break
            if r == b"N":
                time.sleep(0.2)           # board is draining; then resend this block
                ser.reset_input_buffer()
                continue
            sys.exit(f"Upload failed at block {i + 1}/{blocks} (board answered {r!r}).")
        else:
            sys.exit(f"Too many resends at block {i + 1}/{blocks}.")

        if i % 16 == 0 or i == blocks - 1:
            pct = 100.0 * (i + 1) / blocks
            print(f"\r  {i + 1}/{blocks} blocks  {pct:5.1f} %", end="", flush=True)

    print()
    r = read_until(ser, b"DF", 15)        # the board verifies the whole flash CRC now
    ser.close()
    if r == b"D":
        print(f"Done in {time.time() - t0:.0f} s. The board shows green and then plays the clip.")
    else:
        sys.exit("Upload finished but the board's CRC check failed (red screen). Try again.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="GIF / MP4 file")
    ap.add_argument("port", nargs="?", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--size", type=int, default=120, help="stored frame size in pixels (default 120)")
    ap.add_argument("--scale", type=int, default=0, help="enlarge factor on the LCD (default: 240 // size)")
    ap.add_argument("--fps", type=float, default=15.0, help="playback frame rate (default 15)")
    ap.add_argument("--max-frames", type=int, default=0, help="use only the first N frames")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--wait", type=float, default=60.0, help="seconds to wait for the board")
    ap.add_argument("--no-upload", action="store_true", help="only convert + check the size")
    ap.add_argument("--out", default="video.bin", help="with --no-upload: write header+frames here")
    a = ap.parse_args()

    scale = a.scale or max(1, LCD // a.size)
    if a.size * scale > LCD:
        sys.exit(f"size {a.size} x scale {scale} = {a.size * scale} px does not fit the 240 px LCD.")

    data = convert(a.input, a.size, a.fps, a.max_frames)
    frame_bytes = a.size * a.size * 2
    frames = len(data) // frame_bytes
    data = data[:frames * frame_bytes]
    if frames == 0:
        sys.exit("No frames were produced.")

    print(f"{frames} frames of {a.size}x{a.size} (drawn {scale}x -> {a.size * scale} px), "
          f"{a.fps:g} fps, {len(data) / 1e6:.2f} MB, clip length {frames / a.fps:.1f} s")

    if len(data) > VIDEO_MAX_BYTES:
        fit = VIDEO_MAX_BYTES // frame_bytes
        sys.exit(f"Too big for the flash (max {VIDEO_MAX_BYTES / 1e6:.2f} MB = {fit} frames at this size). "
                 f"Use --max-frames {fit}, a smaller --size, or a lower --fps.")

    header = build_header(a.size, scale, a.fps, frames, data)

    if a.no_upload:
        with open(a.out, "wb") as f:
            f.write(header + data)
        print(f"OK, fits. Wrote {a.out}")
        return
    if not a.port:
        sys.exit("Give the serial port (e.g. COM5), or use --no-upload.")

    upload(a.port, a.baud, header, data, a.wait)


if __name__ == "__main__":
    main()
