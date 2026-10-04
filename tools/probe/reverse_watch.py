#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reverse watchpoint: who last wrote ADDR[:LEN] at or before presented frame F?

Writes a k2_diag write-attribution probe (docs/adr/0006-probe-diagnostics.md), runs the
headless build with it, and answers from the probe log at function granularity. Modelled on
DKC1Recomp tools/reverse_watch.py (elliotttate/DKC1Recomp@3eb9a10c): a bounded, fail-closed range
grammar and "last writer before frame F" over a replayed headless run.

Runs are close to deterministic, not exactly (engine patch 0007 ties EE events to host time);
use --repeat 2 to see whether two runs agree.

Exit codes: 0 answered, 2 bad arguments, 3 run failed or the answer is incomplete.
"""
from __future__ import annotations

import argparse
import dataclasses
import os
import re
import subprocess
import sys
from pathlib import Path

# Mirrors k2::diag::Limits (diag/include/k2/diag/probe_spec.h).
RAM_END = 0x02000000
MAX_ATTRIB_BYTES = 4096
MAX_FRAMES = 5000
DEFAULT_LEAD = 120

REPO = Path(__file__).resolve().parents[2]
DEFAULT_EXE = REPO / "out/build/msvc-x64/app/Release/kessen2.exe"
DEFAULT_ELF = REPO / "work/disc/SLUS-20275/iso/SLUS_202.75"
DEFAULT_WORK = REPO / "out/probe"

_HEX = re.compile(r"(?:0[xX])?([0-9a-fA-F]{1,8})")


class UsageError(ValueError):
    pass


def parse_hex(text: str, what: str) -> int:
    m = _HEX.fullmatch(text)
    if not m:
        raise UsageError(f"{what} must be 1-8 hex digits, got '{text}'")
    return int(m.group(1), 16)


def parse_range(text: str) -> tuple[int, int]:
    """ADDR[:LEN], hex, inside EE RAM, 1..4096 bytes (the C++ attrib limits)."""
    addr_text, sep, len_text = text.partition(":")
    addr = parse_hex(addr_text, "address")
    length = parse_hex(len_text, "length") if sep else 4
    if length == 0 or length > MAX_ATTRIB_BYTES:
        raise UsageError(f"length must be 1..{MAX_ATTRIB_BYTES} bytes, got '{len_text}'")
    if addr + length > RAM_END:
        raise UsageError(f"range '{text}' is outside EE RAM 0x00000000-0x01FFFFFF")
    return addr, length


def parse_frames(before: int, frames: int | None) -> int:
    if before < 1 or before > MAX_FRAMES:
        raise UsageError(f"--before-frame must be 1..{MAX_FRAMES}, got {before}")
    if frames is None:
        frames = min(before + DEFAULT_LEAD, MAX_FRAMES)
    if frames < before or frames > MAX_FRAMES:
        raise UsageError(f"--frames must be {before}..{MAX_FRAMES}, got {frames}")
    return frames


@dataclasses.dataclass(frozen=True)
class Write:
    frame: int
    seq: int
    addr: int
    old: int
    new: int
    writer: int | None
    site: int | None
    edge: str

    def describe(self) -> str:
        who = f"0x{self.writer:08x}" if self.writer is not None else "unattributed"
        site = f" site=0x{self.site:08x}" if self.site is not None else ""
        return (f"frame={self.frame} seq={self.seq} addr=0x{self.addr:08x} "
                f"0x{self.old:02x}->0x{self.new:02x} writer={who}{site} edge={self.edge}")


@dataclasses.dataclass
class Log:
    writes: list[Write]
    truncated_frame: int | None
    ended: bool


def _fields(line: str) -> dict[str, str]:
    out = {}
    for tok in line.split():
        k, sep, v = tok.partition("=")
        if sep:
            out[k] = v
    return out


def parse_log(lines) -> Log:
    writes: list[Write] = []
    truncated = None
    ended = False
    for raw in lines:
        line = raw.strip()
        if line.startswith("[k2probe] "):
            line = line[len("[k2probe] "):]
        f = _fields(line)
        kind = f.get("kind")
        if kind == "write":
            writer = f["writer"]
            writes.append(Write(
                frame=int(f["frame"]), seq=int(f["seq"]), addr=int(f["addr"], 16),
                old=int(f["old"], 16), new=int(f["new"], 16),
                writer=None if writer == "unattributed" else int(writer, 16),
                site=int(f["site"], 16) if "site" in f else None, edge=f.get("edge", "?")))
        elif kind == "attrib-truncated" and truncated is None:
            truncated = int(f["frame"])
        elif kind == "end":
            ended = True
    return Log(writes, truncated, ended)


@dataclasses.dataclass
class Answer:
    last: dict[int, Write | None]  # byte address -> last write at or before F
    attributed: int
    unattributed: int
    incomplete: str | None

    def key(self):
        return {a: (w.writer, w.site, w.new) if w else None for a, w in self.last.items()}

    def writer_key(self):
        return {a: (w.writer, w.site) if w else None for a, w in self.last.items()}


def agreement(answers: list[Answer]) -> str:
    """How far repeated runs agree: fully, on the writer only, or not at all."""
    if all(a.key() == answers[0].key() for a in answers[1:]):
        return "yes (same writer, site and value per byte)"
    if all(a.writer_key() == answers[0].writer_key() for a in answers[1:]):
        return ("writer only (same writer and site per byte, different values: timing moved the "
                "guest, patch 0007)")
    return "NO - the writer depends on host timing (patch 0007); treat the answer as approximate"


def answer(log: Log, addr: int, length: int, before: int) -> Answer:
    last: dict[int, Write | None] = {a: None for a in range(addr, addr + length)}
    attributed = unattributed = 0
    for w in sorted(log.writes, key=lambda w: w.seq):
        if w.frame > before or w.addr not in last:
            continue
        last[w.addr] = w
        if w.writer is None:
            unattributed += 1
        else:
            attributed += 1
    incomplete = None
    if log.truncated_frame is not None and log.truncated_frame <= before:
        incomplete = (f"attribution events were capped at frame {log.truncated_frame}; "
                      "raise attrib-max-events or narrow the range")
    elif not log.ended:
        incomplete = "probe log has no end line (run did not finish cleanly)"
    return Answer(last, attributed, unattributed, incomplete)


def report(ans: Answer, log: Log, before: int, context: int, out=sys.stdout) -> None:
    # Group consecutive bytes with the same last write into one row.
    rows: list[tuple[int, int, Write | None]] = []
    for a in sorted(ans.last):
        w = ans.last[a]
        if rows and rows[-1][2] is not None and w is not None and rows[-1][2].seq == w.seq \
                and rows[-1][1] == a:
            rows[-1] = (rows[-1][0], a + 1, w)
        elif rows and rows[-1][2] is None and w is None and rows[-1][1] == a:
            rows[-1] = (rows[-1][0], a + 1, None)
        else:
            rows.append((a, a + 1, w))
    print(f"last write at or before frame {before}:", file=out)
    for start, end, w in rows:
        span = f"0x{start:08x}" + (f"..0x{end - 1:08x}" if end - start > 1 else "")
        if w is None:
            print(f"  {span}: no write observed (value unchanged since probe install)", file=out)
            continue
        print(f"  {span}: {w.describe()}", file=out)
        if context > 0:
            ordered = sorted(log.writes, key=lambda x: x.seq)
            idx = next(i for i, x in enumerate(ordered) if x.seq == w.seq)
            lo, hi = max(0, idx - context), min(len(ordered), idx + context + 1)
            for x in ordered[lo:hi]:
                mark = ">" if x.seq == w.seq else " "
                print(f"      {mark} {x.describe()}", file=out)
    print(f"writes counted: attributed={ans.attributed} unattributed={ans.unattributed}", file=out)
    if ans.incomplete:
        print(f"INCOMPLETE: {ans.incomplete}", file=out)


def write_probe(path: Path, addr: int, length: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("# Generated by tools/probe/reverse_watch.py\n"
                    "k2probe 1\n"
                    f"attrib 0x{addr:x}:{length:x}\n"
                    "attrib-max-events 1000000\n", encoding="ascii")


def run_once(exe: Path, elf: Path, probe: Path, log_path: Path, frames: int) -> Log:
    env = dict(os.environ)
    env["K2_PROBE"] = str(probe)
    env["K2_PROBE_LOG"] = str(log_path)
    cmd = [str(exe), "--headless", "--frames", str(frames), "--timeout-s", "900", "--elf", str(elf)]
    print(f"running: {' '.join(cmd)}", file=sys.stderr)
    proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          errors="replace", check=False)
    result = [l for l in proc.stderr.splitlines() if "boot: result=" in l]
    print(result[-1] if result else "(no boot result line)", file=sys.stderr)
    if proc.returncode != 0:
        tail = "\n".join(proc.stderr.splitlines()[-15:])
        raise RuntimeError(f"kessen2 exited with {proc.returncode}:\n{tail}")
    with open(log_path, encoding="ascii", errors="replace") as f:
        return parse_log(f)


# ---- self-test (synthetic data only) -------------------------------------------------------

SYNTH_LOG = """\
frame=0 seq=0 kind=probe version=1 funcs=0 watches=0 attrib_ranges=1 attrib_bytes=4 screenshot=0
frame=3 seq=1 kind=write addr=0x00001000 old=0x00 new=0x01 writer=0x00100000 site=0x00100010 edge=enter
frame=5 seq=2 kind=write addr=0x00001001 old=0x00 new=0x02 writer=0x00200000 edge=exit
frame=9 seq=3 kind=write addr=0x00001000 old=0x01 new=0x05 writer=unattributed edge=enter
frame=12 seq=4 kind=write addr=0x00001000 old=0x05 new=0x06 writer=0x00300000 edge=exit
frame=12 seq=5 kind=write addr=0x00002000 old=0x00 new=0x07 writer=0x00300000 edge=exit
frame=13 seq=6 kind=end
"""


def self_test() -> int:
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        if not ok:
            failures += 1
            print(f"FAIL {what}", file=sys.stderr)

    valid = [("1000", (0x1000, 4)), ("0x1595:34", (0x1595, 0x34)), ("1ffffff:1", (0x1ffffff, 1)),
             ("0X10:1000", (0x10, 0x1000))]
    for text, want in valid:
        try:
            check(parse_range(text) == want, f"parse_range({text})")
        except UsageError as e:
            check(False, f"parse_range({text}) raised {e}")
    invalid = ["", "0x", "10g0", "100000000", "1000:0", "1000:1001", "2000000", "1ffffff:2",
               "1000:", "-1", "1000:4:4"]
    for text in invalid:
        try:
            parse_range(text)
            check(False, f"parse_range({text!r}) accepted")
        except UsageError:
            pass
    check(parse_frames(1200, None) == 1320, "default frames F+120")
    check(parse_frames(4990, None) == 5000, "default frames capped at 5000")
    for before, frames in [(0, None), (5001, None), (100, 99), (100, 5001)]:
        try:
            parse_frames(before, frames)
            check(False, f"parse_frames({before}, {frames}) accepted")
        except UsageError:
            pass

    log = parse_log(SYNTH_LOG.splitlines())
    check(len(log.writes) == 5 and log.ended and log.truncated_frame is None, "parse_log")
    a = answer(log, 0x1000, 4, 10)
    check(a.last[0x1000] is not None and a.last[0x1000].seq == 3 and a.last[0x1000].writer is None,
          "byte 0x1000 last write before 10 is unattributed seq 3")
    check(a.last[0x1001] is not None and a.last[0x1001].writer == 0x200000, "byte 0x1001 writer")
    check(a.last[0x1002] is None and a.last[0x1003] is None, "untouched bytes")
    check(a.attributed == 2 and a.unattributed == 1 and a.incomplete is None, "counts before 10")
    b = answer(log, 0x1000, 4, 12)
    b_first = b.last[0x1000]
    check(b_first is not None and b_first.writer == 0x300000, "frame 12 is inclusive")
    check(answer(log, 0x1000, 1, 2).last[0x1000] is None, "nothing before the first write")
    trunc = parse_log((SYNTH_LOG + "frame=4 seq=7 kind=attrib-truncated max_events=1\n").splitlines())
    check(answer(trunc, 0x1000, 4, 10).incomplete is not None, "truncation before F is incomplete")
    check(answer(trunc, 0x1000, 4, 3).incomplete is None, "truncation after F is fine")
    unended = parse_log(SYNTH_LOG.splitlines()[:-1])
    check(answer(unended, 0x1000, 4, 10).incomplete is not None, "missing end line is incomplete")
    check(a.key() != b.key(), "answer keys differ when the writer differs")
    check(agreement([a, a]).startswith("yes"), "identical runs agree")
    check(agreement([a, b]).startswith("NO"), "different writers disagree")
    shifted = parse_log(SYNTH_LOG.replace("new=0x05", "new=0x09").splitlines())
    check(agreement([a, answer(shifted, 0x1000, 4, 10)]).startswith("writer only"),
          "same writer with a different value")
    prefixed = parse_log("[k2probe] " + l for l in SYNTH_LOG.splitlines())
    check(len(prefixed.writes) == 5, "stderr prefix accepted")

    import io
    buf = io.StringIO()
    report(a, log, 10, 1, out=buf)
    text = buf.getvalue()
    check("0x00001002..0x00001003: no write observed" in text, "report groups untouched bytes")
    check("writer=unattributed" in text and "> frame=9" in text, "report shows context")

    print("reverse_watch self-test: " + ("ok" if failures == 0 else f"{failures} failure(s)"),
          file=sys.stderr)
    return 0 if failures == 0 else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").strip().splitlines()[0])
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    ap.add_argument("--address", help="ADDR[:LEN], hex (LEN default 4, max 0x1000)")
    ap.add_argument("--before-frame", type=int, help="answer for writes at or before this frame")
    ap.add_argument("--frames", type=int, help=f"frames to run (default F+{DEFAULT_LEAD}, max {MAX_FRAMES})")
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--context", type=int, default=6, help="surrounding events to show")
    ap.add_argument("--repeat", type=int, default=1, help="runs to compare (1-3)")
    ap.add_argument("--log", type=Path, help="answer from an existing probe log; do not run")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()
    try:
        if args.address is None or args.before_frame is None:
            raise UsageError("--address and --before-frame are required")
        addr, length = parse_range(args.address)
        frames = parse_frames(args.before_frame, args.frames)
        if not 1 <= args.repeat <= 3:
            raise UsageError("--repeat must be 1..3")
        if not 0 <= args.context <= 100:
            raise UsageError("--context must be 0..100")
    except UsageError as e:
        print(f"reverse_watch: {e}", file=sys.stderr)
        return 2

    if args.log is not None:
        with open(args.log, encoding="ascii", errors="replace") as f:
            log = parse_log(f)
        ans = answer(log, addr, length, args.before_frame)
        report(ans, log, args.before_frame, args.context)
        return 3 if ans.incomplete else 0

    for p, what in [(args.exe, "--exe"), (args.elf, "--elf")]:
        if not p.is_file():
            print(f"reverse_watch: {what} not found: {p}", file=sys.stderr)
            return 2
    probe = args.work / "reverse.probe"
    write_probe(probe, addr, length)
    answers = []
    for i in range(args.repeat):
        log_path = args.work / f"reverse-{i + 1}.log"
        try:
            log = run_once(args.exe, args.elf, probe, log_path, frames)
        except (RuntimeError, OSError) as e:
            print(f"reverse_watch: run {i + 1} failed: {e}", file=sys.stderr)
            return 3
        ans = answer(log, addr, length, args.before_frame)
        print(f"== run {i + 1} ({log_path})")
        report(ans, log, args.before_frame, args.context)
        if ans.incomplete:
            return 3
        answers.append(ans)
    if len(answers) > 1:
        print("runs agree: " + agreement(answers))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
