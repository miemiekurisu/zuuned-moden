#pragma once

#include "library/LibraryPaths.h"

#include <QDir>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Two spellings of one path, and the rules for which one goes where.
//
// Qt's forward-slash form is the storage and comparison form on both platforms.
// That is not cosmetic: watch_folders dedupes with ON CONFLICT(path), releasing
// a folder purges its media through a SQL LIKE prefix built from the stored
// path, LibraryScanner matches a track's parents against the root strings, and
// QML row filters compare path strings directly.  Letting a Windows path typed
// as E:\Music in beside one the folder picker stored as E:/Music therefore
// creates two identities for one folder: the duplicate is scanned under
// whichever root string it matched first and the other can never release its
// media again.
//
// QDir::fromNativeSeparators()/toNativeSeparators() are the identity wherever
// the native separator already is '/', so both helpers below leave Linux
// byte-for-byte alone - a backslash is an ordinary filename character there and
// must never be rewritten.
class PathText : public QObject {
    Q_OBJECT
    // QML_NAMED_ELEMENT, not QML_ELEMENT: Qt 6 owns the bare name PathText
    // for its shapes type - Qt6Quick.dll exports PathText and PathSvg - and
    // every QML file here imports QtQuick, so a singleton registered as
    // PathText is Qt's type in any file that does not also import Zuuned.
    // The call then fails per delegate as "Property 'url' of object
    // QtQuick/PathText is not a function".  ZPathText belongs to nobody else,
    // and a scan of every Qt6 DLL says it is the only name of ours that
    // collided at all.
    QML_NAMED_ELEMENT(ZPathText)
    QML_SINGLETON

public:
    using QObject::QObject;

    // Store and compare this.  Decodes a file:// URL the way a picker hands one
    // over - including the drive-letter rule that makes E:/Music out of
    // file:///E:/Music - then settles on a single separator spelling.
    Q_INVOKABLE static QString canonical(const QString &path) {
        return LibraryPaths::canonicalRoot(path);
    }

    // Hand to Image.source and friends.  Never store or compare it: a URL is a
    // display/presentation spelling, and the Windows form needs the third slash
    // that "file://" + path silently omits.
    Q_INVOKABLE static QString url(const QString &path) {
        return LibraryPaths::fileUrl(path);
    }

    // Show this, nothing else.  Never store it, compare it, or hand it to a file
    // API - it exists so a Windows user reads E:\Music and not E:/Music.
    Q_INVOKABLE static QString display(const QString &path) {
        return QDir::toNativeSeparators(path);
    }
};
