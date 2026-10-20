#pragma once

#include <QDir>
#include <QString>
#include <QUrl>

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

}  // namespace LibraryPaths
