#pragma once

#include "LibraryTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

#include <QDir>
#include <QFileInfo>

#include <functional>

// Folder scanner — port of the mac's performScan: per media type,
// 3 phases — (1) serial prep: enumerate + stat + exclusion check +
// mtime-unchanged fast-path + user_edited protection; (2) parallel
// metadata probe on QThreadPool (audio chunks of 64, photos 128) via
// zune_probe() (in-process libavformat); (3) serial chunked
// transactional upsert, emitting a refresh after each chunk so the UI
// streams in.
//
// Metadata fallback chain (mac parity): file tags → folder structure
// (WatchFolder/Artist/Album/file) → "Artist - Title" filename parse.
//
// Lives on its own QThread; owns its own LibraryDb connection (opened
// on first scan). All signals are emitted from the worker thread —
// connect queued.
class LibraryScanner : public QObject {
    Q_OBJECT

public:
    explicit LibraryScanner(QObject *parent = nullptr);
    // Explicit DB path for tests; default = the app DB.
    void setDbPath(const QString &path) { m_dbPath = path; }

public slots:
    // Full scan of every watch folder in the DB. Safe to call again
    // after completion; ignores calls while a scan runs.
    void scan();
    void cancel();

signals:
    // stage: "music" | "videos" | "photos"; current/total are per-stage
    // file counts
    void progress(const QString &stage, int current, int total,
                  const QString &currentFile);
    // Emitted after each committed chunk — the UI facade reloads.
    void chunkCommitted();
    void finished(int tracksAdded, int tracksUpdated, int photosAdded);
    void failed(const QString &error);
    // Quiet diagnostics for Settings; failed reads never become fake tracks.
    void musicProbeFailuresChanged();

private:
    QString m_dbPath;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
};

// One watch folder, walked once.  Every media scan here used to be a recursive
// QDirIterator, which cannot answer the one question a watch folder has to
// ask: have I already been in this folder?
namespace LibraryScan {

struct WalkOutcome {
    bool complete = false;    // every directory under the root could be read
    bool cancelled = false;   // the caller's flag stopped the walk
    int directories = 0;      // folders actually listed
    int files = 0;            // entries handed to the visitor
    int duplicates = 0;       // folders skipped as already walked
};

// Hands every entry under `root` that matches `filters` to `visitFile`, depth
// first, and each one exactly once.  A folder that resolves to one this call
// has already listed is skipped, which is what catches a Windows junction -
// QFileInfo::isSymLink() is false for a junction, so the filesystem reports it
// as an ordinary directory and the only thing that distinguishes it from one is
// that its identity has been seen before.  Symbolic links keep the treatment
// QDirIterator gave them without FollowSymlinks: listed, never descended.
// `complete` stays false if any folder could not be read or the walk was
// cancelled, so a partial walk can never certify a watch folder.
WalkOutcome walkFiles(const QString &root, QDir::Filters filters,
                      const std::function<void(const QFileInfo &)> &visitFile,
                      const std::atomic<bool> *cancel = nullptr);
}
