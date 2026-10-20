#pragma once

// A Zuuned profile is one folder: library.db, spoken.db, artwork and thumbnail
// caches, session logs, the single-instance lock and the settings file.
//
// On Linux $HOME relocates the whole thing, and that is how every native gate
// runs the application against a throwaway profile.  Windows has no such lever:
// QStandardPaths asks the shell known folders and ignores LOCALAPPDATA/APPDATA
// overrides, so an instrumented launch reads and writes the real profile and
// loses the diagnostic-capture lock to the instance that is already running.
// ZUUNED_PROFILE_DIR names the folder explicitly, which equally gives a Windows
// user a portable profile.
//
// Unset, every call here is exactly what QStandardPaths::writableLocation
// returns, so the Linux line and every existing gate keep their behaviour.  The
// implementation lives in the header on purpose: src/library/LibraryDb.cpp is
// compiled both into the application and by stand-alone gate scripts that link a
// single translation unit, and a separate .cpp would break those compile lines
// for no benefit.
//
// Only folders a Zuuned profile owns are redirected.  The user's own media
// folders, the system temp dir and the home folder stay wherever the platform
// puts them, so a test profile can still be pointed at real media on purpose.

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

#include <cstdio>

namespace Zuuned {
namespace ProfilePaths {

// The profile folder, or empty when ZUUNED_PROFILE_DIR is unset or unusable.
// A relative value is refused rather than resolved against whatever the current
// directory happens to be, because silently writing next to the build tree is
// worse than a test that fails with a visible reason.
inline QString root() {
    static const QString cached = [] {
        const QString declared = qEnvironmentVariable("ZUUNED_PROFILE_DIR");
        if (declared.isEmpty())
            return QString();
        const QString path = QDir::cleanPath(declared);
        if (!QDir::isAbsolutePath(path)) {
            std::fprintf(stderr, "[profile] ignoring relative ZUUNED_PROFILE_DIR \"%s\"\n",
                         qPrintable(declared));
            return QString();
        }
        if (!QDir().mkpath(path))
            std::fprintf(stderr,
                         "[profile] could not create \"%s\"; profile writes stay there "
                         "and will fail rather than touch the real profile\n",
                         qPrintable(path));
        else
            std::fprintf(stderr, "[profile] every profile file goes to \"%s\"\n", qPrintable(path));
        return path;
    }();
    return cached;
}

// Drop-in for QStandardPaths::writableLocation for the locations a profile owns.
// Like Qt, the returned directory is created when possible.
inline QString writableLocation(QStandardPaths::StandardLocation type) {
    const QString base = root();
    if (base.isEmpty())
        return QStandardPaths::writableLocation(type);
    QString path;
    switch (type) {
    case QStandardPaths::AppDataLocation:
    case QStandardPaths::GenericDataLocation:
    case QStandardPaths::AppConfigLocation:
    case QStandardPaths::GenericConfigLocation:
        path = base;
        break;
    case QStandardPaths::CacheLocation:
    case QStandardPaths::GenericCacheLocation:
        path = base + QStringLiteral("/cache");
        break;
    case QStandardPaths::StateLocation:
    case QStandardPaths::GenericStateLocation:
        path = base + QStringLiteral("/State");
        break;
    case QStandardPaths::RuntimeLocation:
        path = base + QStringLiteral("/run");
        break;
    default:
        return QStandardPaths::writableLocation(type);
    }
    QDir().mkpath(path);
    return path;
}

// Route default-constructed QSettings into root() as well, so the settings file
// is part of the profile folder instead of %APPDATA%.  Qt appends the
// organisation and application names, which puts the file at
// <root>/Zuuned/Zuuned.ini.  Call before the first QSettings object exists.
inline void applySettingsPath() {
    const QString base = root();
    if (base.isEmpty())
        return;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, base);
    const QString file = QSettings().fileName();
    std::fprintf(stderr, "[profile] settings live at \"%s\"\n", qPrintable(file));
    if (!file.startsWith(base))
        std::fprintf(stderr,
                     "[profile] ZUUNED_PROFILE_DIR did not capture the settings file; "
                     "profile isolation is incomplete\n");
}

}  // namespace ProfilePaths
}  // namespace Zuuned
