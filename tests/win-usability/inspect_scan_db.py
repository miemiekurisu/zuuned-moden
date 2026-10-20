"""Inspect a librarytool scan database for the Windows usability gate.

Every value goes out as ASCII key=value lines, so the PowerShell gate never has
to carry non-ASCII text through an argument or compare against a mojibake
candidate.  What is expected is passed as code points: the CJK fixture folder is
--expect-codepoints 0x6F22,0x8A5E rather than the literal characters, which is
the only way to express the check without depending on the console codepage of
the caller.

    python inspect_scan_db.py <db> [--expect-tracks N]
           [--expect-codepoints 0xNN,0xNN] [--expect-long-segments N]

Exit code 0 when every requested property holds, 1 otherwise.
"""

import argparse
import sqlite3
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("db")
    ap.add_argument("--expect-tracks", type=int, default=None)
    ap.add_argument("--expect-codepoints", default=None)
    ap.add_argument("--expect-long-segments", type=int, default=None)
    ap.add_argument("--min-longest-path", type=int, default=None)
    args = ap.parse_args()

    con = sqlite3.connect("file:%s?mode=ro" % args.db.replace("\\", "/"), uri=True)
    cur = con.cursor()

    rows = cur.execute(
        "SELECT filepath, title, duration, audio_fingerprint FROM tracks"
        " ORDER BY filepath").fetchall()
    failures = cur.execute("SELECT COUNT(*) FROM music_probe_failures").fetchone()[0]
    folders = cur.execute("SELECT COUNT(*) FROM watch_folders").fetchone()[0]

    durations = [r[2] or 0 for r in rows]
    print("tracks=%d" % len(rows))
    print("probe_failures=%d" % failures)
    print("watch_folders=%d" % folders)
    print("fingerprints=%d" % sum(1 for r in rows if (r[3] or "").strip()))
    print("min_duration=%d" % (min(durations) if durations else -1))
    # Filenames are reported escaped: the gate prints this line verbatim, and a
    # Windows console would otherwise mangle the CJK fixture.
    print("paths=%s" % ascii([r[0] for r in rows]))
    print("titles=%s" % ascii([r[1] for r in rows]))

    ok = True
    if args.expect_tracks is None:
        print("tracks_expected=yes")
    else:
        matched = len(rows) == args.expect_tracks
        print("tracks_expected=%s" % ("yes" if matched else "no"))
        if not matched:
            ok = False

    if failures:
        ok = False
    print("probe_failures_clean=%s" % ("yes" if not failures else "no"))

    if args.expect_codepoints:
        want = "".join(chr(int(t, 16)) for t in args.expect_codepoints.split(","))
        hit = [r[0] for r in rows if want in r[0]]
        print("codepoint_path_rows=%d" % len(hit))
        if not hit:
            ok = False
    if args.min_longest_path:
        # Recorded paths are relative or absolute depending on how the folder was
        # handed to the scan, so the gate asks for a floor, not an exact length.
        longest = max((len(r[0]) for r in rows), default=0)
        enough = longest >= args.min_longest_path
        print("longest_path_chars=%d" % longest)
        print("longest_path_deep_enough=%s" % ("yes" if enough else "no"))
        if not enough:
            ok = False
    if args.expect_long_segments:
        marker = "segment-" * args.expect_long_segments
        hit = [r[0] for r in rows if marker in r[0]]
        longest = max((len(r[0]) for r in rows), default=0)
        print("long_path_rows=%d" % len(hit))
        print("longest_path_chars=%d" % longest)
        if not hit:
            ok = False
    print("nonzero_durations=%s" % ("yes" if durations and min(durations) > 0 else "no"))
    if durations and min(durations) <= 0:
        ok = False
    print("OVERALL=%s" % ("ok" if ok else "fail"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
