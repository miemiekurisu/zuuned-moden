/*
 * Playlist file bytes -> text, for a format that never declares its encoding.
 */
#pragma once

#include <QByteArray>
#include <QString>

// m3u has no encoding field and no magic.  This app writes UTF-8 without a BOM,
// so reading its own playlists back with QString::fromUtf8() is enough - but a
// playlist the user points at was usually written by somebody else, and on
// Windows "somebody else" defaults to the ANSI code page: Notepad's "ANSI" save,
// Windows Media Player exports and most shareware converters emit CP1252 in the
// west and CP936 here, where every Chinese artist name is two or three bytes
// that are not valid UTF-8.  Run through fromUtf8() those names arrive as U+FFFD
// runs, no entry matches a library row, and the import answers "0 added, 41
// missing" for a playlist naming forty-one songs the library plainly holds.
//
// A BOM is the other everyday case - Notepad, PowerShell and Visual Studio all
// add one to a UTF-8 file - and it is not a decoding error, it is two characters
// glued onto the front of the first entry.  One wrong entry out of forty reads as
// a bug in the matching code rather than in the decoding, which is why it is
// handled first.
//
// So: honour a BOM, take UTF-8 when the bytes really are UTF-8, and otherwise
// fall back to the locale's own code page, which is exactly what "no BOM and not
// valid UTF-8" means on the machine that wrote the file.  The UTF-8 test is
// QString::fromUtf8() followed by the round trip: Qt replaces an invalid sequence
// with U+FFFD, which re-encodes to the three bytes EF BF BD and therefore never
// reproduces the input, so a mismatch is real evidence rather than a tautology.
//
// What this still does not read is UTF-16 m3u - Notepad's retired "Unicode"
// option, which starts FF FE and delivers every entry with interleaved NULs.
// Recorded as a known gap in docs/WINDOWS_COMPAT_AUDIT.md.
//
// QtCore only, so librarytool can gate it from a command line.
namespace PlaylistText {

inline QString decode(const QByteArray &bytes) {
    if (bytes.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")))
        return QString::fromUtf8(bytes.constData() + 3, bytes.size() - 3);
    const QString utf8 = QString::fromUtf8(bytes);
    if (utf8.toUtf8() == bytes)
        return utf8;
    return QString::fromLocal8Bit(bytes);
}

}  // namespace PlaylistText
