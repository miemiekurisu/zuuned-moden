/*
 * Playlist file bytes -> text, for a format that never declares its encoding.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

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
// UTF-16 has a BOM too, so it is readable even though the format never declares
// an encoding: Notepad's retired "Unicode" save and PowerShell's -Encoding
// unicode both start FF FE and deliver every entry with interleaved NULs, which
// read as text is a playlist of paths nobody wrote.  Big-endian FF FE's mirror is
// honoured on the same principle.  What is still not read is UTF-16 without a
// BOM - indistinguishable from a byte pair that happens to look like text - and
// UTF-32, which no playlist writer in the wild produces.
//
// The order matters.  A BOM test has to come before the UTF-8 validity test
// because FF FE is not valid UTF-8 but FE FF bytes inside a longer file are
// nobody's idea of a signal, and because fromLocal8Bit() would happily turn both
// BOM bytes into two letters of the active code page and call it a filename.
//
// QtCore only, so librarytool can gate it from a command line.
namespace PlaylistText {

namespace detail {

// UTF-16 whose byte order is already known, with the BOM eaten by the caller.
// QString::fromUtf16() reads char16_t in the order the build happens to use, so a
// big-endian file is swapped here rather than hoped away; spelling it out per
// byte is also what makes the answer the same on either kind of host.
inline QString utf16(const QByteArray &body, bool bigEndian) {
    const qsizetype units = body.size() / 2;   // a stray trailing byte is dropped
    // Parentheses here would be a function declaration, which is what MSVC makes of
    // it; resize() is the spelling that cannot be misread.
    QVector<char16_t> text;
    text.resize(int(units));
    const uchar *in = reinterpret_cast<const uchar *>(body.constData());
    for (qsizetype i = 0; i < units; ++i) {
        const uchar low = in[2 * i];
        const uchar high = in[2 * i + 1];
        text[int(i)] = char16_t(bigEndian ? (uint(low) << 8) | high
                                                  : (uint(high) << 8) | low);
    }
    return QString::fromUtf16(text.constData(), units);
}

}  // namespace detail

inline QString decode(const QByteArray &bytes) {
    if (bytes.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")))
        return QString::fromUtf8(bytes.constData() + 3, bytes.size() - 3);
    if (bytes.startsWith(QByteArrayLiteral("\xFF\xFE")))
        return detail::utf16(bytes.mid(2), false);
    if (bytes.startsWith(QByteArrayLiteral("\xFE\xFF")))
        return detail::utf16(bytes.mid(2), true);
    const QString utf8 = QString::fromUtf8(bytes);
    if (utf8.toUtf8() == bytes)
        return utf8;
    return QString::fromLocal8Bit(bytes);
}

}  // namespace PlaylistText
