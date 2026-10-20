#pragma once

#include <QStandardPaths>
#include <QString>

namespace Zuuned {
namespace ProfilePaths {

// A Zuuned profile is one folder: library.db, spoken.db, artwork and thumbnail
// caches, session logs and the single-instance lock.  On Linux $HOME relocates
// the whole thing, which is how every native gate runs the app against a
// throwaway profile.  Windows has no such lever: QStandardPaths asks the shell
// known folders and ignores LOCALAPPDATA/APPDATA, so an instrumented launch
// would read and write the real profile (and fight the running one for the
// diagnostic-capture lock).  ZUUNED_PROFILE_DIR names the folder explicitly,
// which also gives Windows users a portable profile.  Unset, every location is
// exactly what Qt would return, so the Linux line is untouched.
QString root();

// Profile locations resolve under root(); the user's own media folders
// (Music/Movies/Pictures/Documents), the system temp dir and the home folder
// stay wherever the platform puts them.
QString writableLocation(QStandardPaths::StandardLocation type);

// Route default-constructed QSettings into root() as well.  Call before the
// first QSettings object exists.
void applySettingsPath();

}  // namespace ProfilePaths
}  // namespace Zuuned
