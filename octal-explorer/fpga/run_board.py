#!/usr/bin/env python3
"""Drive the folded Officers core on the Nexys A7 over its USB-UART, check against octal.

    run_board.py [--port /dev/ttyUSB1] [--n0 50000] [--stream 100000] [--crc 10000000]
    run_board.py --no-check [--total 1e10] [--block 1e9] [--n0 50000] [--fresh] [--state FILE]

1. 'I': read fold, clock, history depth.
2. 'L': preload G(n0 - preload .. n0-1) (computed with octal --rare).
3. 'S': stream --stream values back and compare each with octal.
4. 'C': run --crc further values at full speed, then compare the CRC-32 (zlib, values as
   u16 little-endian), the last value and the block maximum (value and first position)
   with octal; report the measured rate.
5. 'D': dump the history and compare it with octal; run --resume-test values further,
   then reload the dump ('L') and run them again: both CRCs must match octal.
6. Period search ('K', 'H'; boards without it are skipped): the board's window
   fingerprint must equal the one computed here, after the load and after the dump;
   then 'K' at a window W, reload the window one heap earlier and compute one value:
   the board must report a hit with period 1.

--no-check: long runs without octal (only the preload comes from octal).  After the
preload, 'C' commands of --block values each run until --total values are done
(0 = until Ctrl-C), printing n, the CRC, last value and maximum of each block and the rate,
and a line for each new overall maximum with the heap where it first occurs.
The board's state (next n, its history from 'D', the maximum so far and where it
first occurs) is saved to --state (default build/board-state.bin, 41 KB) after
every block and when the run ends.  Ctrl-C stops after the block in flight (at most
one block, 40 s for 1e9); a second Ctrl-C aborts without saving.  The next run
continues from the state file, so a long run can be stopped and restarted any time
and can go on past where octal can produce the preload; --fresh starts again from
--n0 with the octal preload, and --resume FILE starts from another state file.
--checkpoint DIR also keeps a copy per block (DIR/ckpt-<n>.bin).

Period search (--no-check, boards with 'K'/'H'): the board fingerprints its history
window, the last DEPTH values, which determine the whole continuation (window_fp.sv).
At the start of a session the current window becomes the checkpoint n_c ('K'); when
n reaches 2 n_c the checkpoint moves to n (Brent), so any period is found once the
preperiod is below a checkpoint.  After every block the board's fingerprint is
compared with the one computed here from the dump (exit 4 if they differ: a hardware
fault).  On a hit with period p the run dumps the window W(n), runs p more values and
compares W(n + p) with it value by value: equal -> the speculated sequence is periodic
(printed, state saved, exit 5); different -> a fingerprint collision (printed; a new
checkpoint is set).  A period of the *speculated* values is a candidate: it becomes a
theorem only with the values verified (octal --rare) and the periodicity theorem.
The 7-segment display shows the highest nim-value so far.  The values are the
speculation (no new rare values), which Grossman's computation confirms up to 1.4e14.
Protocol: see nexys_a7_top.sv.  Needs read/write access to the port (group dialout).
"""
import argparse
import os
import re
import signal
import struct
import subprocess
import sys
import termios
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


# state / checkpoint file: magic, next heap n, depth, maximum so far and the heap where
# it first occurs, then G(n-depth .. n-1) as u16 LE
CK_MAGIC = b"OFC2"
CK_HEAD = "<4sQIHQ"
STATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "board-state.bin")

# 'C' reply: tag crc cycles last err max at
C_FMT = "<cIQHBHI"
C_LEN = struct.calcsize(C_FMT)

# period search: 'H' reply: tag fp chk since flags hit_at (flags: bit 0 hit, bit 1 checkpoint set)
H_FMT = "<cQQQBQ"
H_LEN = struct.calcsize(H_FMT)
FP_PLOW = 0x1028ecf30eb3f40d                     # P = x^64 + FP_PLOW, as in window_fp.sv
M64 = (1 << 64) - 1


def window_fp(win):
    """Fingerprint of a window (oldest first), as window_fp.sv computes it:
    sum of G(n-1-j)(x) x^(9j) mod P over GF(2), Horner from the oldest value."""
    h = 0
    for v in win:
        for _ in range(9):
            h = ((h << 1) & M64) ^ (FP_PLOW if h >> 63 else 0)
        h ^= v
    return h


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


def load(fd, vals):
    """'L': vals are G(n-len .. n-1); the board keeps the last depth of them."""
    os.write(fd, b"L" + struct.pack("<I", len(vals)) + struct.pack(f"<{len(vals)}H", *vals))
    assert read_exact(fd, 1, 5) == b"l"


def set_display_max(fd, g):
    """'M': show the run's maximum on the 7-segment display (an 'L' clears it)."""
    os.write(fd, b"M" + struct.pack("<I", g))
    try:
        ok = read_exact(fd, 1, 2) == b"m"
    except TimeoutError:
        ok = False
    if not ok:
        print("note: the board has no 'M' (older bitstream); its display shows the window's maximum")


def run_block(fd, k):
    """'C' k: (crc, cycles, last, err, max, at)."""
    os.write(fd, b"C" + struct.pack("<I", k))
    r = read_exact(fd, C_LEN, 60 + k / 1e6)
    tag, *rest = struct.unpack(C_FMT, r)
    assert tag == b"c", r
    return rest


def period_status(fd, timeout=2):
    """'H': dict fp, chk, since, hit, chk_set, hit_at; None if the board has no 'H'."""
    os.write(fd, b"H")
    try:
        r = read_exact(fd, H_LEN, timeout)
    except TimeoutError:
        return None
    tag, fp, chk, since, flags, hit_at = struct.unpack(H_FMT, r)
    assert tag == b"h", r
    return dict(fp=fp, chk=chk, since=since, hit=bool(flags & 1), chk_set=bool(flags & 2), hit_at=hit_at)


def latch(fd):
    """'K': the current window becomes the checkpoint; returns its fingerprint."""
    os.write(fd, b"K")
    r = read_exact(fd, 9, 2)
    assert r[:1] == b"k", r
    return struct.unpack("<Q", r[1:])[0]


def dump(fd, depth):
    """'D': the history G(n-depth .. n-1), oldest first."""
    os.write(fd, b"D")
    r = read_exact(fd, 2 * depth + 1, 5)
    assert r[-1:] == b"d", r[-8:]
    return list(struct.unpack(f"<{depth}H", r[:-1]))


def save_checkpoint(path, n, hist, gmax=0, gmax_n=0):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(struct.pack(CK_HEAD, CK_MAGIC, n, len(hist), gmax, gmax_n)
                + struct.pack(f"<{len(hist)}H", *hist))
    os.replace(tmp, path)


def read_checkpoint(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, n, depth, gmax, gmax_n = struct.unpack_from(CK_HEAD, data)
    if magic != CK_MAGIC:
        sys.exit(f"{path}: not a state file")
    return n, list(struct.unpack_from(f"<{depth}H", data, struct.calcsize(CK_HEAD))), gmax, gmax_n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyUSB1")
    ap.add_argument("--n0", type=int, default=50000)
    ap.add_argument("--stream", type=int, default=100000)
    ap.add_argument("--crc", type=int, default=10_000_000)
    ap.add_argument("--info", action="store_true",
                    help="only ask the board for its parameters; exit 2 if it does not answer")
    ap.add_argument("--resume-test", type=int, default=1_000_000, help="values run after the dump, twice")
    ap.add_argument("--no-check", action="store_true", help="long run without comparing with octal")
    ap.add_argument("--total", type=float, default=0, help="--no-check: values to compute (0 = forever)")
    ap.add_argument("--block", type=float, default=1e9, help="--no-check: values per 'C' command")
    ap.add_argument("--checkpoint", metavar="DIR", help="--no-check: save the history after each block")
    ap.add_argument("--state", default=STATE, help="--no-check: state file, saved after each block and "
                    "at the end, and continued from at the start (default %(default)s)")
    ap.add_argument("--fresh", action="store_true", help="--no-check: ignore the state file, start at --n0")
    ap.add_argument("--resume", metavar="FILE", help="--no-check: start from this state file")
    args = ap.parse_args()
    if args.info:
        fd = open_port(args.port)
        try:
            fold, mhz, depth, slots = board_info(fd)
        except (TimeoutError, AssertionError, struct.error):
            print(f"{args.port}: no answer from the board (not programmed, or busy)")
            sys.exit(2)
        print(f"{args.port}: fold {fold}, {mhz} MHz, history {depth}, {slots} slots")
        return
    if args.no_check:
        return long_run(args)

    fd = open_port(args.port)
    os.write(fd, b"I")
    r = read_exact(fd, 10, 2)
    tag, fold, mhz, depth, slots = struct.unpack("<cBHIH", r)
    assert tag == b"I", r
    print(f"board: fold {fold}, {mhz} MHz, history {depth}, {slots} slots "
          f"-> {mhz / fold:.2f} M values/s")
    preload = depth + 2

    total = args.n0 + args.stream + args.crc + args.resume_test
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
    ps = period_status(fd)
    if ps is None:
        print("period search: the board has no 'H' (older bitstream); skipped")
    else:
        ref = window_fp(pre[-depth:])
        print(f"period search: window fingerprint after the load {ps['fp']:016x} (here {ref:016x}) "
              f"-> {'OK' if ps['fp'] == ref else 'MISMATCH'}")
        if ps["fp"] != ref:
            sys.exit(1)

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
        r = read_exact(fd, C_LEN, 60 + args.crc / 1e6)
        tag, crc, cycles, last, err, bmax, at = struct.unpack(C_FMT, r)
        assert tag == b"c", r
        blk = G[n:n + args.crc]
        ref = zlib.crc32(struct.pack(f"<{args.crc}H", *blk))
        rmax = max(blk)
        ok = (crc == ref and last == blk[-1] and not err and bmax == rmax
              and at == blk.index(rmax))
        print(f"crc run: {args.crc} values from n = {n}: crc {crc:08x} (octal {ref:08x}), "
              f"last {last} (octal {blk[-1]}), max G({n + at}) = {bmax} "
              f"(octal G({n + blk.index(rmax)}) = {rmax}), err = {err} -> {'OK' if ok else 'MISMATCH'}")
        secs = cycles / (mhz * 1e6)
        print(f"  {cycles} cycles = {secs * 1e3:.1f} ms at {mhz} MHz -> "
              f"{args.crc / secs / 1e6:.2f} M values/s (host round trip {time.time() - t:.2f} s)")
        if not ok:
            sys.exit(1)

    # dump, continue, restore
    if args.resume_test:
        n1 = args.n0 + args.stream + args.crc
        k = args.resume_test
        t = time.time()
        hist = dump(fd, depth)
        ok = hist == G[n1 - depth:n1]
        print(f"dump: G({n1 - depth} .. {n1 - 1}) in {time.time() - t:.2f} s: "
              f"{'OK' if ok else 'MISMATCH'}")
        ref = zlib.crc32(struct.pack(f"<{k}H", *G[n1:n1 + k]))
        crc_a = run_block(fd, k)[0]
        load(fd, hist[:2] + hist)
        crc_b = run_block(fd, k)[0]
        ok2 = crc_a == ref and crc_b == ref
        print(f"resume: {k} values from n = {n1}: after the dump {crc_a:08x}, after reloading it "
              f"{crc_b:08x} (octal {ref:08x}) -> {'OK' if ok2 else 'MISMATCH'}")
        if not (ok and ok2):
            sys.exit(1)

        # period search: fingerprint after a dump (the history rotated once), and a hit
        n2 = n1 + k
        if period_status(fd) is not None:
            ok = True
            fp_dump = window_fp(dump(fd, depth))
            ps = period_status(fd)
            ok &= ps["fp"] == fp_dump == window_fp(G[n2 - depth:n2])
            print(f"period search: fingerprint after a dump {ps['fp']:016x} (here {fp_dump:016x}) "
                  f"-> {'OK' if ok else 'MISMATCH'}")
            chk = latch(fd)                              # checkpoint: W(n2)
            load(fd, G[n2 - 1 - preload:n2 - 1])         # back to W(n2 - 1) (loads are not compared)
            ps0 = period_status(fd)
            last = run_block(fd, 1)[2]                   # computes G(n2 - 1): the window is W(n2) again
            ps1 = period_status(fd)
            ok2 = (chk == ps["fp"] and not ps0["hit"] and ps0["since"] == 0 and last == G[n2 - 1]
                   and ps1["hit"] and ps1["hit_at"] == 1 and ps1["since"] == 1 and ps1["fp"] == chk)
            print(f"period search: checkpoint at W({n2}), reload W({n2 - 1}), one value -> hit "
                  f"{ps1['hit']}, period {ps1['hit_at']}, since {ps1['since']} "
                  f"-> {'OK' if ok2 else 'MISMATCH'}")
            if not (ok and ok2):
                sys.exit(1)


def board_info(fd):
    os.write(fd, b"I")
    tag, fold, mhz, depth, slots = struct.unpack("<cBHIH", read_exact(fd, 10, 2))
    assert tag == b"I"
    return fold, mhz, depth, slots


def period_step(fd, n, hist, n_c, confirm):
    """After a block (n = next heap, hist = W(n) from the dump): check the board's
    fingerprint, act on a hit, move the checkpoint (Brent).  Returns (n_c, confirm)."""
    ps = period_status(fd)
    ref = window_fp(hist)
    if ps is None or ps["fp"] != ref:
        got = "no answer to 'H'" if ps is None else f"board fingerprint {ps['fp']:016x}"
        print(f"period search: {got} at n = {n:,}, expected {ref:016x}: hardware fault", flush=True)
        sys.exit(4)
    if confirm and n == confirm[0]:
        p = n - confirm[1][0]
        if hist == confirm[1][1]:
            print(f"PERIOD CONFIRMED: W({n:,}) = W({n - p:,}) value by value: the speculated sequence "
                  f"is periodic with period {p:,}, preperiod at most {n_c:,}", flush=True)
            sys.exit(5)
        print(f"period search: W({n:,}) != W({n - p:,}): the hit was a fingerprint collision; "
              f"new checkpoint at n = {n:,}", flush=True)
        latch(fd)
        return n, None
    if ps["hit"] and not confirm:
        p = ps["hit_at"]
        print(f"PERIOD CANDIDATE: fingerprint of W({n_c + p:,}) = checkpoint W({n_c:,}), period {p:,}; "
              f"confirming: run {p:,} values and compare W({n + p:,}) with W({n:,})", flush=True)
        return n_c, (n + p, (n, hist))
    if not confirm and n >= 2 * n_c:
        chk = latch(fd)
        print(f"period search: no period from n = {n_c:,} up to {n:,}; checkpoint moved to n = {n:,} "
              f"(fingerprint {chk:016x})", flush=True)
        return n, None
    return n_c, confirm


def long_run(args):
    block = int(args.block)
    total = int(args.total)
    if not 0 < block < 2**32:
        sys.exit("--block must be between 1 and 4294967295")
    fd = open_port(args.port)
    fold, mhz, depth, slots = board_info(fd)
    preload = depth + 2
    print(f"board: fold {fold}, {mhz} MHz -> {mhz / fold:.2f} M values/s; no check against octal")
    start = args.resume or (None if args.fresh or not os.path.exists(args.state) else args.state)
    if start:
        n0, hist, gmax, gmax_n = read_checkpoint(start)
        if len(hist) != depth:
            sys.exit(f"{start}: history {len(hist)}, board {depth}")
        pre = hist[:2] + hist                    # the first two only pass through
        print(f"continuing from {start}: n = {n0:,}, maximum so far G({gmax_n:,}) = {gmax}")
    else:
        n0 = args.n0
        if n0 < preload:
            sys.exit(f"--n0 must be >= {preload}")
        G = octal_values(n0)
        pre = G[n0 - preload:]
        gmax = max(G)
        gmax_n = G.index(gmax)
        print(f"starting at n = {n0:,} with octal's values; maximum so far G({gmax_n:,}) = {gmax}")
    load(fd, pre)
    set_display_max(fd, gmax)
    # period search: check the board's fingerprint of the loaded window, set the checkpoint
    ps = period_status(fd)
    n_c = confirm = None                         # checkpoint heap; (n + p, W(n)) while confirming a hit
    if ps is None:
        print("period search: the board has no 'H' (older bitstream); off")
    else:
        ref = window_fp(pre[-depth:])
        if ps["fp"] != ref:
            print(f"period search: board fingerprint {ps['fp']:016x} after the load, expected {ref:016x}")
            sys.exit(4)
        latch(fd)
        n_c = n0
        print(f"period search: checkpoint at n = {n_c:,} (fingerprint {ref:016x})", flush=True)
    os.makedirs(os.path.dirname(os.path.abspath(args.state)), exist_ok=True)
    if args.checkpoint:
        os.makedirs(args.checkpoint, exist_ok=True)

    # first Ctrl-C: stop after the block in flight and save; second: abort
    stop = []

    def on_int(sig, frame):
        if stop:
            raise KeyboardInterrupt
        stop.append(1)
        print(f"\nstopping after this block (at most {block / (mhz / fold * 1e6):.0f} s), then saving "
              f"the state; Ctrl-C again aborts without saving", flush=True)

    signal.signal(signal.SIGINT, on_int)
    n, t0 = n0, time.time()
    try:
        while not stop and (total == 0 or n < n0 + total):
            k = block if total == 0 else min(block, n0 + total - n)
            if confirm:
                k = min(k, confirm[0] - n)               # end a block exactly at n + p
            crc, cycles, last, err, bmax, at = run_block(fd, k)
            if bmax > gmax:
                gmax, gmax_n = bmax, n + at
                print(f"new maximum: G({gmax_n:,}) = {bmax}", flush=True)
            n += k
            done, el = n - n0, time.time() - t0
            eta = f", {(total - done) * el / done / 60:.1f} min left" if total else ""
            # whole run (from --n0, across restarts) first, then this session
            print(f"n = {n:,}: block crc {crc:08x}, G({n - 1}) = {last}, max {bmax} (run {gmax}), "
                  f"err = {err}, {k / (cycles / (mhz * 1e6)) / 1e6:.2f} M values/s, "
                  f"{n - args.n0:,} values; this session {done:,} in {el:.0f} s{eta}",
                  flush=True)
            if err:
                print("err: all 256 common values marked in this block -- the speculation failed here "
                         f"(state file not updated: it still holds the start of this block)", flush=True)
                sys.exit(3)
            hist = dump(fd, depth)
            save_checkpoint(args.state, n, hist, gmax, gmax_n)
            if n_c is not None:
                n_c, confirm = period_step(fd, n, hist, n_c, confirm)
            if args.checkpoint:
                save_checkpoint(os.path.join(args.checkpoint, f"ckpt-{n}.bin"), n, hist, gmax, gmax_n)
    except KeyboardInterrupt:
        sys.exit(f"\naborted; the state file holds n = {n:,}.  A 'C' command in flight still finishes on "
                 f"the board (LED 0 goes off, at most {block / (mhz / fold * 1e6):.0f} s).")
    print(f"stopped at n = {n:,}; state saved to {args.state}; maximum so far G({gmax_n:,}) = {gmax}")


if __name__ == "__main__":
    main()
