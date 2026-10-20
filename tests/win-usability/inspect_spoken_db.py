"""Inspect the spoken-media database a Zuuned launch left behind.

The Windows usability gate launches the application with its profile inside a
Chinese folder because that is what a Chinese Windows user name puts in front of
every path the application builds.  SQLite takes UTF-8 path names on Windows and
was once handed the local ANSI code page there instead, which decodes to a
different file name: the folder appeared and the database did not, and every
podcast and audiobook operation failed at open.

A file listing only proves a file exists.  This proves the file is the store the
application means by reading its own schema version back, so the gate can say the
spoken library works under a non-ASCII path rather than that something was written
there.

Every value goes out as ASCII key=value lines, so the PowerShell gate never has to
carry non-ASCII text through an argument or compare against a mojibake candidate.

    python inspect_spoken_db.py <db>

Exit code 0 when the file is a usable spoken-media schema 1 store, 1 otherwise.
"""

import os
import sqlite3
import sys

WANT_COLUMNS = ["id", "kind", "parent", "identity", "payload"]


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: inspect_spoken_db.py <db>")
        return 2
    path = sys.argv[1]
    print("bytes=%d" % os.path.getsize(path))
    # Read-write on purpose: a launch the gate stopped can leave an un-checkpointed
    # -wal next to the database, and a read-only connection cannot recover it.  The
    # file lives in a throwaway profile this gate owns and deletes.
    try:
        con = sqlite3.connect(path)
        cur = con.cursor()
        version = cur.execute("PRAGMA user_version").fetchone()[0]
        journal = cur.execute("PRAGMA journal_mode").fetchone()[0]
        tables = sorted(r[0] for r in cur.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"))
        indexes = sorted(r[0] for r in cur.execute(
            "SELECT name FROM sqlite_master"
            " WHERE type='index' AND name NOT LIKE 'sqlite_%'"))
        columns = []
        rows = -1
        if "items" in tables:
            columns = [r[1] for r in cur.execute("PRAGMA table_info(items)")]
            rows = cur.execute("SELECT COUNT(*) FROM items").fetchone()[0]
        integrity = cur.execute("PRAGMA integrity_check").fetchone()[0]
        con.close()
    except sqlite3.Error as exc:
        print("error=%s" % type(exc).__name__)
        print("OVERALL=fail")
        return 1

    print("user_version=%d" % version)
    print("journal=%s" % journal)
    print("tables=%s" % ",".join(tables))
    print("indexes=%s" % ",".join(indexes))
    print("columns=%s" % ",".join(columns))
    print("rows=%d" % rows)
    print("integrity=%s" % integrity)

    problems = []
    if version != 1:
        problems.append("user_version=%d" % version)
    if tables != ["items"]:
        problems.append("tables=%s" % ",".join(tables))
    if "spoken_parent" not in indexes:
        problems.append("indexes=%s" % ",".join(indexes))
    if columns != WANT_COLUMNS:
        problems.append("columns=%s" % ",".join(columns))
    if integrity != "ok":
        problems.append("integrity=%s" % integrity)
    if problems:
        for p in problems:
            print("fail=%s" % p)
        print("OVERALL=fail")
        return 1
    print("OVERALL=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
