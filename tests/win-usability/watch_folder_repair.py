#!/usr/bin/env python3
"""Gate: a mangled watch-folder root repairs itself when the library opens.

Windows folder pickers used to reach the database as "/E:/Music" - a leading
slash in front of a drive letter is not absolute there, so the folder scanned
nothing and stayed "unreachable" forever.  LibraryDb::open() now canonicalises
every stored root, and this gate freezes that behaviour against a real
librarytool run rather than a unit test.

  --seed DB            write the four scarred rows an old build left behind
  --check DB [--allow P]  assert the roots a real librarytool scan leaves behind

The scan needs a database argument, so it also registers its own (empty) root;
pass that folder with --allow so the assertion stays about the four seeded rows.

Seed rows and what they prove:

  /E:/Scar Alone       scar strip + the UNIQUE slot is free -> repaired in place
  /e:/Scar Duplicate   scar strip AND drive-case fold -> lands on row 3, dropped
  E:/Scar Duplicate    the healthy twin that must survive
  E:/Clean Path        already canonical -> byte-for-byte untouched

The case fold matters: without it "/e:/Scar Duplicate" repairs to
"e:/Scar Duplicate", which is a *different* UNIQUE value, so both rows would
survive as two identities for one folder.

On POSIX the same strings are ordinary absolute paths and must NOT be touched,
which is the other half of the assertion.
"""
import argparse
import os
import sqlite3
import sys

TABLE = """
CREATE TABLE IF NOT EXISTS watch_folders (
  id INTEGER PRIMARY KEY,
  path TEXT UNIQUE NOT NULL,
  type TEXT DEFAULT 'all'
);
"""

SEEDED = [
    "/E:/Scar Alone",
    "/e:/Scar Duplicate",
    "E:/Scar Duplicate",
    "E:/Clean Path",
]

WINDOWS_EXPECTED = [
    "E:/Clean Path",
    "E:/Scar Alone",
    "E:/Scar Duplicate",
]

POSIX_EXPECTED = list(sorted(SEEDED))


def fail(msg):
    print("FAIL " + msg)
    return 1


def seed(path):
    if os.path.exists(path):
        os.remove(path)
    con = sqlite3.connect(path)
    try:
        con.executescript(TABLE)
        con.executemany(
            "INSERT INTO watch_folders (path, type) VALUES (?, 'all')",
            [(p,) for p in SEEDED],
        )
        con.commit()
    finally:
        con.close()
    print("seeded=%d db=%s" % (len(SEEDED), path))
    print("OVERALL=ok")
    return 0


def fold(path):
    """Windows-insensitive comparison, used only for --allow matchings."""
    return path.replace("\\", "/").rstrip("/").upper()


def check(path, allowed):
    if not os.path.exists(path):
        return fail("no database at " + path)
    con = sqlite3.connect(path)
    try:
        rows = [r[0] for r in con.execute(
            "SELECT path FROM watch_folders ORDER BY path")]
    finally:
        con.close()
    keep = list(rows)
    for extra in allowed:
        target = fold(extra)
        for row in list(keep):
            if fold(row) == target:
                keep.remove(row)
                break
    expected = WINDOWS_EXPECTED if os.name == "nt" else POSIX_EXPECTED
    print("platform=%s rows=%d allowed=%d" % (os.name, len(rows), len(rows) - len(keep)))
    print("paths=" + " | ".join(keep))
    problems = []
    if keep != list(expected):
        problems.append("expected " + " | ".join(expected))
    for row in rows:
        if os.name == "nt":
            if row.startswith("/"):
                problems.append("root kept its leading slash: " + row)
            if len(row) >= 2 and row[1] == ":" and row[0].islower():
                problems.append("root kept a lowercase drive: " + row)
    if not any("Scar Alone" in r for r in rows):
        problems.append("the scarred root never reached the healthy spelling")
    dups = [r for r in set(rows) if rows.count(r) > 1]
    if dups:
        problems.append("duplicate roots: " + ", ".join(dups))
    if problems:
        for problem in problems:
            print("FAIL " + problem)
        print("OVERALL=fail")
        return 1
    print("OVERALL=ok")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", metavar="DB")
    ap.add_argument("--check", metavar="DB")
    ap.add_argument("--allow", action="append", default=[],
                    help="root added by the scan itself; repeatable")
    args = ap.parse_args()
    if bool(args.seed) == bool(args.check):
        ap.error("exactly one of --seed / --check is required")
    if args.seed:
        return seed(args.seed)
    return check(args.check, args.allow)


if __name__ == "__main__":
    sys.exit(main())
