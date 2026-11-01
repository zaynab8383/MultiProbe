#!/usr/bin/env python3
"""
MultiProbe - upload a GIF/MP4 to the board's W25Q64 flash through the ST-LINK (SWD).
No UART adapter needed: only the ST-LINK you already use for programming.

How it works: the PC writes chunks of the clip into a RAM buffer of the running board with
STM32_Programmer_CLI (hot-plug: the CPU is not stopped), the firmware (video_player.c)
stores each chunk in the external flash and acknowledges it through a small RAM mailbox.

Requirements (PC):
    - STM32CubeProgrammer (STM32_Programmer_CLI) - it is also bundled with CubeIDE
    - ffmpeg in PATH
    - python 3 (no extra packages)
    - the board must run the firmware that contains the SWD mailbox (flash it from CubeIDE first)
    - close the CubeIDE debug session / CubeProgrammer GUI (only ONE program may use the ST-LINK)

Usage:
    python swd_upload.py cat.mp4                      # 120x120, drawn 2x = full screen, 15 fps
    python swd_upload.py cat.mp4 --max-frames 10      # quick test (about 30 s)
    python swd_upload.py cat.mp4 --cli "C:\\path\\to\\STM32_Programmer_CLI.exe"
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from video_upload import convert, build_header, VIDEO_MAX_BYTES, LCD   # same folder

# ---- RAM layout (must match video_player.c) ---------------------------------------------
MB        = 0x24080000          # mailbox
BUF       = 0x24081000          # data buffer
BUF_SIZE  = 0x40000             # 256 KB
BLOCK     = 4096
FW_MAGIC  = 0x3158424D          # "MBX1"

O_MAGIC, O_CMD, O_OFFSET, O_LEN, O_CRC, O_SEQ, O_ACK, O_STATUS, O_STATE, O_HDR = \
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x40

CMD_BEGIN, CMD_CHUNK, CMD_FINISH = 1, 2, 3
ST_CRC = 4
STATUS_TEXT = {
    0: "ok",
    1: "the board rejected the clip header (too big / corrupt)",
    2: "flash (IC1) erase/program error",
    3: "bad chunk arguments",
    4: "chunk CRC mismatch (data damaged in RAM)",
    5: "final CRC check of the flash failed",
    6: "chunk/finish without begin",
    7: "unknown command",
}


class CliError(Exception):
    pass


def find_cli(explicit):
    if explicit:
        return explicit
    for name in ("STM32_Programmer_CLI", "STM32_Programmer_CLI.exe"):
        p = shutil.which(name)
        if p:
            return p
    patterns = [
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin\STM32_Programmer_CLI.exe",
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeIDE_*\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin\STM32_Programmer_CLI.exe",
        os.path.expanduser("~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"),
        "/opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_*/tools/bin/STM32_Programmer_CLI",
    ]
    for pat in patterns:
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]                      # newest version
    sys.exit("STM32_Programmer_CLI was not found. Install STM32CubeProgrammer, or pass --cli <full path>.\n"
             "(CubeIDE ships it in: <CubeIDE>\\plugins\\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*\\tools\\bin)")


WORD_LINE = re.compile(r"^\s*0x[0-9A-Fa-f]{8}\s*:\s*((?:[0-9A-Fa-f]{8}\s*)+)$", re.M)


class Board:
    def __init__(self, cli, mode):
        self.cli = cli
        self.mode = mode
        self.seq = 0
        self.combined = True          # try "-w32 addr w1 w2 w3..." and "-w + -w32" in one call first

    # -- low level -----------------------------------------------------------------------
    def run(self, args, timeout=180):
        cmd = [self.cli, "-c", "port=SWD", "mode=" + self.mode] + args
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               universal_newlines=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise CliError("STM32_Programmer_CLI timed out")
        out = p.stdout or ""
        if p.returncode != 0 or re.search(r"^\s*error\b", out, re.I | re.M):
            raise CliError(out.strip()[-800:] or ("exit code %d" % p.returncode))
        return out

    def read_words(self, addr, nwords):
        out = self.run(["-r32", hex(addr), str(nwords * 4)])
        words = []
        for m in WORD_LINE.finditer(out):
            words += [int(w, 16) for w in m.group(1).split()]
        if len(words) < nwords:
            raise CliError("could not parse the memory dump:\n" + out[-400:])
        return words[:nwords]

    def write(self, files, addr, words):
        """Write files (list of (path, address)) and then the 32-bit words at addr, in this order."""
        if self.combined:
            args = []
            for path, a in files:
                args += ["-w", path, hex(a)]
            args += ["-w32", hex(addr)] + [hex(w) for w in words]
            try:
                self.run(args)
                return
            except CliError as e:
                # maybe only this CLI version dislikes the combined form: fall back to single calls
                self.combined = False
                self.last_combined_error = str(e)
        for path, a in files:
            self.run(["-w", path, hex(a)])
        for i, w in enumerate(words):                 # ascending order, seq (last word) goes last
            self.run(["-w32", hex(addr + 4 * i), hex(w)])

    # -- protocol ------------------------------------------------------------------------
    def probe(self):
        try:
            w = self.read_words(MB, 9)
        except CliError as e:
            sys.exit("Cannot talk to the board through the ST-LINK:\n%s\n\n"
                     "Check: ST-LINK connected (SWDIO/SWCLK/GND), board powered, and CubeIDE's debug session /\n"
                     "CubeProgrammer GUI closed (only one program can use the ST-LINK)." % e)
        if w[O_MAGIC // 4] != FW_MAGIC:
            sys.exit("The board answered, but the SWD mailbox is not active (magic 0x%08X).\n"
                     "Flash the newest firmware (video_player.c with the SWD part) from CubeIDE and let it\n"
                     "start (the logo must be on the screen), then run this again." % w[0])
        seq, ack = w[O_SEQ // 4], w[O_ACK // 4]
        if seq != ack:                                 # an old command is still being handled
            for _ in range(60):
                time.sleep(0.5)
                w = self.read_words(MB, 9)
                if w[O_SEQ // 4] == w[O_ACK // 4]:
                    break
            else:
                sys.exit("The board is busy with an old command. Reset the board and try again.")
        self.seq = w[O_ACK // 4]

    def command(self, cmd, offset=0, length=0, crc=0, files=(), timeout=120):
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        self.write(list(files), MB + O_CMD, [cmd, offset, length, crc, self.seq])   # seq is the LAST word

        t0 = time.time()
        while True:
            ack, status, _state = self.read_words(MB + O_ACK, 3)
            if ack == self.seq:
                return status
            if time.time() - t0 > timeout:
                sys.exit("The board did not answer (timeout). Is the firmware running?")
            time.sleep(0.15)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="GIF / MP4 file")
    ap.add_argument("--size", type=int, default=120, help="stored frame size in pixels (default 120)")
    ap.add_argument("--scale", type=int, default=0, help="enlarge factor on the LCD (default: 240 // size)")
    ap.add_argument("--fps", type=float, default=15.0, help="playback frame rate (default 15)")
    ap.add_argument("--max-frames", type=int, default=0, help="use only the first N frames")
    ap.add_argument("--cli", help="full path of STM32_Programmer_CLI(.exe)")
    ap.add_argument("--mode", default="HOTPLUG", help="connect mode (default HOTPLUG = do not stop the CPU)")
    ap.add_argument("--chunk-kb", type=int, default=256, help="chunk size, 4..256 KB (default 256)")
    a = ap.parse_args()

    scale = a.scale or max(1, LCD // a.size)
    if a.size * scale > LCD:
        sys.exit("size %d x scale %d = %d px does not fit the 240 px LCD." % (a.size, scale, a.size * scale))
    chunk = max(BLOCK, min(a.chunk_kb * 1024, BUF_SIZE) // BLOCK * BLOCK)

    data = convert(a.input, a.size, a.fps, a.max_frames)
    frame_bytes = a.size * a.size * 2
    frames = len(data) // frame_bytes
    data = data[:frames * frame_bytes]
    if frames == 0:
        sys.exit("No frames were produced.")
    print("%d frames of %dx%d (drawn %dx -> %d px), %g fps, %.2f MB, clip length %.1f s" %
          (frames, a.size, a.size, scale, a.size * scale, a.fps, len(data) / 1e6, frames / a.fps))
    if len(data) > VIDEO_MAX_BYTES:
        fit = VIDEO_MAX_BYTES // frame_bytes
        sys.exit("Too big for the flash (max %.2f MB = %d frames at this size). Use --max-frames %d, "
                 "a smaller --size, or a lower --fps." % (VIDEO_MAX_BYTES / 1e6, fit, fit))

    header = build_header(a.size, scale, a.fps, frames, data)            # 36 bytes
    padded = data + b"\xff" * (-len(data) % BLOCK)

    board = Board(find_cli(a.cli), a.mode)
    print("Using", board.cli)
    board.probe()
    print("Board found (SWD mailbox active).")

    with tempfile.TemporaryDirectory() as tmp:
        hdr_file = os.path.join(tmp, "hdr.bin")
        with open(hdr_file, "wb") as f:
            f.write(header + b"\x00\x00\x00\x00")                         # 40 bytes = multiple of 4

        st = board.command(CMD_BEGIN, files=[(hdr_file, MB + O_HDR)])
        if st != 0:
            sys.exit("BEGIN failed: " + STATUS_TEXT.get(st, str(st)))
        print("Board is receiving (screen is blue). Sending %d chunks ..." % ((len(padded) + chunk - 1) // chunk))

        t0 = time.time()
        total_chunks = (len(padded) + chunk - 1) // chunk
        chunk_file = os.path.join(tmp, "chunk.bin")
        for n, off in enumerate(range(0, len(padded), chunk), 1):
            part = padded[off:off + chunk]
            with open(chunk_file, "wb") as f:
                f.write(part)
            crc = zlib.crc32(part) & 0xFFFFFFFF
            for attempt in range(4):
                st = board.command(CMD_CHUNK, off, len(part), crc, files=[(chunk_file, BUF)], timeout=180)
                if st == 0:
                    break
                if st != ST_CRC:
                    sys.exit("Chunk %d failed: %s" % (n, STATUS_TEXT.get(st, str(st))))
                print("  chunk %d: CRC mismatch, resending" % n)
            else:
                sys.exit("Chunk %d: too many CRC errors." % n)
            el = time.time() - t0
            print("  chunk %d/%d done  (%.0f %%, %.0f s elapsed)" % (n, total_chunks, 100.0 * n / total_chunks, el))

        print("Verifying the flash and finishing ...")
        st = board.command(CMD_FINISH, timeout=180)
        if st != 0:
            sys.exit("FINISH failed: " + STATUS_TEXT.get(st, str(st)) + " (screen is red). Try again.")

    print("Done in %.0f s. The board shows green and then plays the clip." % (time.time() - t0))


if __name__ == "__main__":
    main()
