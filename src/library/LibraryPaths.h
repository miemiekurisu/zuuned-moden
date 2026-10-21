#pragma once

#include <QDir>
#include <QString>
#include <QUrl>
#include <QRegularExpression>

// One canonical spelling for every filesystem root the library stores.
//
// Folder pickers hand QML a file:// URL.  Stripping the scheme by hand produced
// "/home/user/Music" on Linux - correct by luck - and "/E:/Music" on Windows,
// where a leading slash in front of a drive letter is NOT absolute: it resolves
// against the current directory of drive E:, so QFileInfo/QDir can never see
// the folder.  A watch folder stored that way scans nothing, reports itself
// unreachable forever, and every save built underneath it fails.
//
// QUrl::toLocalFile() is the decoder that gets both platforms right: it
// percent-decodes and drops the leading slash exactly when a drive letter
// follows it.  Every entry point funnels through it, then settles on Qt's
// forward-slash form, which is what watch_folders dedupes on and what
// LibraryScanner matches a track's parents against.
//
// Both steps are the identity on Linux, so an existing Linux database - and a
// backslash inside a Linux filename, where it is an ordinary character - stays
// byte-for-byte identical.  QtCore only, so LibraryDb and librarytool can use
// this without pulling QtQml into a command-line tool.
namespace LibraryPaths {

// Storage and comparison form: local path, forward slashes, no URL wrapper.
inline QString canonicalRoot(const QString &raw) {
    QString local = raw.trimmed();
    const QUrl parsed(local);
    if (parsed.isLocalFile()) {
        local = parsed.toLocalFile();
    }
    else if (parsed.scheme().compare(QLatin1String("file"), Qt::CaseInsensitive) == 0) {
        // A file:// URL carrying a host only means something on Windows, where
        // it names the server of a UNC share; toLocalFile() refuses those.
        QString share = parsed.path();
        if (share.startsWith(QLatin1Char('/')))
            share = share.mid(1);
#ifdef Q_OS_WIN
        local = QDir::fromNativeSeparators(
            QStringLiteral("\\\\%1\\%2").arg(parsed.host(), share));
#else
        local = share;
#endif
    }
    local = QDir::fromNativeSeparators(local);
#ifdef Q_OS_WIN
    // Repair the "/E:/Music" scar for roots that arrive without a scheme: rows
    // an earlier build already wrote, and settings saved the same way.
    if (local.size() >= 3 && local.at(0) == QLatin1Char('/')
        && local.at(2) == QLatin1Char(':')) {
        const QChar drive = local.at(1);
        if ((drive >= QLatin1Char('A') && drive <= QLatin1Char('Z'))
            || (drive >= QLatin1Char('a') && drive <= QLatin1Char('z')))
            local = local.mid(1);
    }
    // One spelling per drive.  Windows paths fold case but watch_folders
    // dedupes with a byte-UNIQUE(path), so "e:/Music" and "E:/Music" would be
    // stored as two identities: a track scanned under one never matches a file
    // resolved under the other, and each row keeps its own unreachable state.
    // The drive letter is the one component where folding is free - the rest
    // of the path stays verbatim, because on Linux it is not case-insensitive.
    if (local.size() >= 2 && local.at(1) == QLatin1Char(':')
        && (local.size() == 2 || local.at(2) == QLatin1Char('/'))) {
        const QChar drive = local.at(0);
        const QChar upper = drive.toUpper();
        if (drive != upper)
            local.replace(0, 1, upper);
    }
#endif
    return local;
}

// URL form for Image.source and anything else in QML that wants a URL.
// "file://" + path is wrong on Windows: it spells file://E:/Music, which parses
// as host "E:" and loads nothing.  QUrl::fromLocalFile() emits file:///E:/...
// and escapes spaces and non-ASCII for us.  Never store the result.
inline QString fileUrl(const QString &path) {
    const QString local = canonicalRoot(path);
    return local.isEmpty() ? QString() : QUrl::fromLocalFile(local).toString();
}

// Comparison key for two spellings that are meant to name one file.
//
// Windows path comparison ignores case, so  E:\Music\A.mp3 , e:/music/a.mp3 and
// E:/Music/A.MP3 all open the same file — and an m3u written by another program
// spells it however it likes.  Everywhere the app matches paths as bytes, an
// identical file becomes two identities that never recognise each other, which
// is how a playlist import reports every track as missing.  Folding case is
// safe on Windows because two names differing only in case cannot coexist in
// one folder; on Linux case is part of the name, so POSIX keeps the byte
// comparison it always had and every existing key stays byte-for-byte equal.
inline QString caseKey(const QString &path) {
#ifdef Q_OS_WIN
    return path.toLower();
#else
    return path;
#endif
}

// Whether a finished component collides with a DOS device name.
//
// Windows refuses to OPEN a path whose component is  CON ,  PRN ,  AUX ,
//  CLOCK$ ,  NUL ,  COM0-9  or  LPT0-9 , and it matches the stem with the
// extension included, so  NUL.mp3  is a file nobody can reopen by path.
// CreateFile walks the component, hits the device namespace and answers without
// consulting the directory, so the write succeeds and the read that follows it
// fails - the worst kind of failure to diagnose, because the file is sitting
// there in Explorer.  The check is on the last component, case-folded.
// The rule exists nowhere else: on POSIX these are ordinary filenames, so there
// the answer is no and no existing Linux name gains an underscore.
inline bool isReservedWindowsName(const QString &component) {
#ifdef Q_OS_WIN
    static const QRegularExpression reserved(
        QStringLiteral("^(?:CON|PRN|AUX|CLOCK\\$|NUL|COM[0-9]|LPT[0-9])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return reserved.match(component).hasMatch();
#else
    Q_UNUSED(component);
    return false;
#endif
}

// One filesystem component, named by something nobody chose a filename for:
// a device artist, a device album, a device-side folder.
//
// What a component may contain is platform-dependent, and the call sites this
// replaces assumed Linux's answer - "/" is illegal, so "/" was the only
// character replaced.  A Zune album called "The Dark Side of the Moon:
// Immersion" then puts a ":" into a directory name, which Linux accepts and
// Windows refuses: mkpath() fails, the file open fails, and saving that track
// to the library reports a plain failure with nothing wrong on the device.
// Windows also refuses  < > : " / \ | ? *  and the control characters, refuses
// a component ending in a dot or a space (Explorer drops them silently, so two
// different titles collapse onto one file), and reserves CON, PRN, AUX,
// CLOCK$, NUL, COM0-9 and LPT0-9 against the stem - extension included, so
// "nul.mp3" is not a file anybody can open.
//
// POSIX keeps the behaviour it always had, byte for byte: only "/" is touched,
// because a backslash inside a Linux filename is an ordinary character.  The
// "." and ".." refusal is not about the platform: those are device folder
// names that would walk an import out of the folder it was meant to land in,
// on either side.  VideoIdentity::safeFilePart applies the same rule to a
// device video name; this is where it was still hand-rolled.
inline QString safeComponent(const QString &name, const QString &fallback) {
    QString safe = name;
#ifdef Q_OS_WIN
    static const QRegularExpression illegal(
        QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}]"));
    safe.replace(illegal, QStringLiteral("_"));
    safe = safe.trimmed();
    while (!safe.isEmpty()
           && (safe.endsWith(QLatin1Char('.')) || safe.endsWith(QLatin1Char(' '))))
        safe.chop(1);
    if (isReservedWindowsName(safe))
        safe.prepend(QLatin1Char('_'));
#else
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
#endif
    if (safe.isEmpty() || safe == QLatin1String(".") || safe == QLatin1String(".."))
        return fallback;
    return safe;
}

// One line of a playlist, resolved to the spelling the library stores.
//
// A playlist entry is a claim about a file, not a string equal to a database
// filepath.  Windows Media Player writes  E:\Music\A.mp3 , iTunes writes
// file:// URLs, and anything hand-made or exported elsewhere lists its files
// relative to the playlist; compared as text all three are "missing".  Decode a
// URL, take Qt's forward-slash form (a no-op on Linux, where a backslash is an
// ordinary character), resolve a relative entry against  base - the playlist's
// own folder - and clean the result, which is the form LibraryScanner writes
// into filepath.  Relative entries were wrong on Linux too, so this is
// unconditional rather than Windows-branched.
inline QString resolveEntry(const QString &base, const QString &entry) {
    QString local = entry;
    const QUrl asUrl(local);
    if (asUrl.isLocalFile())
        local = asUrl.toLocalFile();
    local = QDir::fromNativeSeparators(local);
    if (!local.isEmpty() && !QDir::isAbsolutePath(local))
        local = base + QLatin1Char('/') + local;
    return QDir::cleanPath(local);
}

}  // namespace LibraryPaths
