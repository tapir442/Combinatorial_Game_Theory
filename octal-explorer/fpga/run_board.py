#!/usr/bin/env python3
"""Drive the folded Officers core on the Nexys A7 over its USB-UART, check against octal.

    run_board.py [--port /dev/ttyUSB1] [--n0 50000] [--stream 100000] [--crc 10000000]

1. 'I': read fold, clock, history depth.
2. 'L': preload G(n0 - preload .. n0-1) (computed with octal --rare).
3. 'S': stream --stream values back and compare each with octal.
4. 'C': run --crc further values at full speed, then compare the CRC-32 (zlib, values as
   u16 little-endian) and the last value with octal; report the measured rate.
Protocol: see nexys_a7_top.sv.  Needs read/write access to the port (group dialout).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import termios
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def open_port(path, baud=2_000_000):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}")
    a[0] = 0                                    # iflag: raw
    a[1] = 0                                    # oflag
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0                                    # lflag
    a[4] = a[5] = speed
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 1                     # 0.1 s read timeout
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def read_exact(fd, n, timeout):
    buf = bytearray()
    end = time.time() + timeout
    while len(buf) < n:
        chunk = os.read(fd, min(65536, n - len(buf)))
        if chunk:
            buf += chunk
            end = time.time() + timeout         # timeout = silence, not total
        elif time.time() > end:
            raise TimeoutError(f"got {len(buf)} of {n} bytes")
    return bytes(buf)


def octal_values(n):
    out = subprocess.run([os.path.join(HERE, "..", "octal"), ".6", "-n", str(n), "-p", "--rare", "-q"],
                         check=True, capture_output=True, text=True).stdout
    return [int(t) for t in re.findall(r"\d+", out)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyUSB1")
    ap.add_argument("--n0", type=int, default=50000)
    ap.add_argument("--stream", type=int, default=100000)
    ap.add_argument("--crc", type=int, default=10_000_000)
    args = ap.parse_args()

    fd = open_port(args.port)
    os.write(fd, b"I")
    r = read_exact(fd, 10, 2)
    tag, fold, mhz, depth, slots = struct.unpack("<cBHIH", r)
    assert tag == b"I", r
    print(f"board: fold {fold}, {mhz} MHz, history {depth}, {slots} slots "
          f"-> {mhz / fold:.2f} M values/s")
    preload = depth + 2

    total = args.n0 + args.stream + args.crc
    t = time.time()
    G = octal_values(total)
    print(f"octal --rare: {total} values in {time.time() - t:.1f} s")
    if args.n0 < max(preload, 20629):
        sys.exit(f"--n0 must be >= {max(preload, 20629)}")

    # load
    t = time.time()
    pre = G[args.n0 - preload:args.n0]
    os.write(fd, b"L" + struct.pack("<I", len(pre)) + b"".join(struct.pack("<H", v) for v in pre))
    assert read_exact(fd, 1, 5) == b"l"
    print(f"loaded {len(pre)} values in {time.time() - t:.2f} s")

    # stream and compare
    n = args.n0
    if args.stream:
        t = time.time()
        os.write(fd, b"S" + struct.pack("<I", args.stream))
        data = read_exact(fd, 2 * args.stream + 2, 5)
        got = struct.unpack(f"<{args.stream}H", data[:-2])
        assert data[-2:-1] == b"s", data[-2:]
        bad = [i for i, v in enumerate(got) if v != G[n + i]]
        print(f"stream: {args.stream} values from n = {n} in {time.time() - t:.2f} s, "
              f"{len(bad)} mismatches, err = {data[-1]}")
        for i in bad[:10]:
            print(f"  G({n + i}): board {got[i]}, octal {G[n + i]}")
        n += args.stream
        if bad:
            sys.exit(1)

    # full speed with CRC
    if args.crc:
        t = time.time()
        os.write(fd, b"C" + struct.pack("<I", args.crc))
        r = read_exact(fd, 16, 60 + args.crc / 1e6)
        tag, crc, cycles, last, err = struct.unpack("<cIQHB", r)
        assert tag == b"c", r
        ref = zlib.crc32(struct.pack(f"<{args.crc}H", *G[n:n + args.crc]))
        ok = crc == ref and last == G[n + args.crc - 1] and not err
        print(f"crc run: {args.crc} values from n = {n}: crc {crc:08x} (octal {ref:08x}), "
              f"last {last} (octal {G[n + args.crc - 1]}), err = {err} -> {'OK' if ok else 'MISMATCH'}")
        secs = cycles / (mhz * 1e6)
        print(f"  {cycles} cycles = {secs * 1e3:.1f} ms at {mhz} MHz -> "
              f"{args.crc / secs / 1e6:.2f} M values/s (host round trip {time.time() - t:.2f} s)")
        if not ok:
            sys.exit(1)


if __name__ == "__main__":
    main()
