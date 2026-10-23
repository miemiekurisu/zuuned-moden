"""Build the scan fixture that the Windows usability gate reads.

Four tracks, one per way a Windows scan has gone wrong here: a plain MP3 so the
pipeline has something to succeed at, a WMA stream inside a file named .mp3
because real libraries contain them and a scan has to sniff magic instead of
trusting the suffix, an album folder named in Chinese characters because the
compat layer decodes narrow paths with the local ANSI code page, and twelve
nested folders so the leaf path goes past 260 characters - the length every
Win32 call that was never converted to the wide, extended-length form quietly
fails at.

The CJK name is written from code points so this file stays ASCII: the whole
point of the fixture is that nothing between the command line and the database
has to survive a code page translation.

``--extra-roots <dir>`` reproduces the same problem in the one place it takes a
different road through the process: argv.  Three one-track trees whose *own root
directory name* is the non-ASCII part - Hebrew, Hangul, and an emoji off the BMP,
because a surrogate pair is precisely what a narrow-path code page conversion
mangles.  A scan root is not a readdir result, and it used to fail on its own:
the scan found nothing and still exited 0.  Three separate roots, scanned one at
a time by the gate, so a root that works cannot mask one that does not.

``--photos <dir>`` puts an album of drawn JPEG frames in the same shape: one folder
whose own name is Chinese.  Photos travel a different table and a different scanner
branch than tracks, and until now nothing on Windows reached either of them from a
gate; it is also the text a Zune photo album is named from, so the bytes that have
to survive a scan here are the bytes a real transfer depends on later.

    python make_scan_fixture.py <music-dir> [--ffmpeg <ffmpeg.exe>] [--force]
                                [--extra-roots <dir>] [--photos <dir>]

Prints ASCII key=value lines.  Exits 0 when all four tracks are on disk, plus the
three extra roots and the two photos when they were asked for.
"""

import argparse
import os
import shutil
import subprocess
import sys

CJK_ALBUM = "\u6f22\u8a5e Album (2024)"
CJK_PHOTO_FOLDER = "\u7167\u7247 Folder"
# Hebrew, Hangul, and an emoji outside the BMP - each spelled from code points.
SCRIPT_ROOTS = [
    "\u05db\u05ea\u05e8\u05d1\u05ea Root",
    "\ud55c\uae00 Root",
    "Emoji \U0001F600 Root",
]
NESTED = ["Nested Volume %02d" % i for i in range(1, 13)]


def run(ffmpeg, args):
    proc = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y"] + args,
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.exit("ffmpeg failed: " + proc.stderr.decode("utf-8", "replace")[-400:])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("music_dir")
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--extra-roots", dest="extra_roots",
                    help="create the three non-ASCII scan roots under this directory")
    ap.add_argument("--photos",
                    help="create a Chinese-named photo album folder under this directory")
    a = ap.parse_args()

    if shutil.which(a.ffmpeg) is None and not os.path.isfile(a.ffmpeg):
        sys.exit("no ffmpeg at " + a.ffmpeg + "; pass --ffmpeg with the path from ZUUNED_WIN_DEPS")

    root = os.path.abspath(a.music_dir)
    if os.path.isdir(root) and os.listdir(root) and not a.force:
        sys.exit(root + " is not empty; pass --force to use it anyway")

    tmp = os.path.join(root, ".tone")
    os.makedirs(tmp, exist_ok=True)
    wav = os.path.join(tmp, "tone.wav")
    mp3 = os.path.join(tmp, "tone.mp3")
    asf = os.path.join(tmp, "sneaky.asf")
    run(a.ffmpeg, ["-f", "lavfi", "-i", "sine=frequency=440:duration=6", wav])
    run(a.ffmpeg, ["-i", wav, "-codec:a", "libmp3lame", "-q:a", "4", mp3])
    run(a.ffmpeg, ["-i", wav, "-codec:a", "wmav2", asf])

    placed = []

    extras = []
    shots = []

    def place(dst, src, into=placed):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        into.append(dst)

    place(os.path.join(root, "Tone A.mp3"), mp3)
    place(os.path.join(root, "Sneaky Wma.mp3"), asf)
    place(os.path.join(root, CJK_ALBUM, "Track One.mp3"), mp3)
    place(os.path.join(root, *NESTED, "Long Path Track.mp3"), mp3)

    # The extra roots are scan roots in their own right, so they must not sit
    # inside the music dir where the main scan would swallow them.
    if a.extra_roots:
        er = os.path.abspath(a.extra_roots)
        if er == root or er.startswith(root + os.sep) or root.startswith(er + os.sep):
            sys.exit("--extra-roots must sit outside the music dir")
        shutil.rmtree(er, ignore_errors=True)
        for name in SCRIPT_ROOTS:
            place(os.path.join(er, name, "Track One.mp3"), mp3, extras)

    # The same rule as the extra roots: a photo album is a scan root of its own,
    # so it must not sit where the music scan would walk into it.  The frames are
    # ffmpeg colour swatches - the scan cares about the name and the bytes, not
    # the picture, and a real photograph would make the fixture depend on luck.
    if a.photos:
        pr = os.path.abspath(a.photos)
        if pr == root or pr.startswith(root + os.sep) or root.startswith(pr + os.sep):
            sys.exit("--photos must sit outside the music dir")
        shutil.rmtree(pr, ignore_errors=True)
        frame = os.path.join(tmp, "frame.jpg")
        run(a.ffmpeg, ["-f", "lavfi", "-i", "color=c=gray:s=64x48", "-frames:v", "1", frame])
        for name in ("Shot One.jpg", "Shot Two.jpg"):
            place(os.path.join(pr, CJK_PHOTO_FOLDER, name), frame, shots)
    shutil.rmtree(tmp, ignore_errors=True)

    longest = max(placed, key=len)
    print("created=%d" % len(placed))
    print("extra_roots=%d" % len(extras))
    print("photos=%d" % len(shots))
    print("longest_path_chars=%d" % len(longest))
    print("fixture=%s" % ascii(root)[1:-1])
    print("OVERALL=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

