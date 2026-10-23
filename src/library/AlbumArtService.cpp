#include "AlbumArtService.h"
#include "../ProfilePaths.h"

#include "libav_transcode.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTemporaryFile>

// Port of AlbumArtService.swift's extractArt/artCachePath. The cache
// key is byte-identical to the mac (and the C reference the mac cites):
//   MD5( lowercase("artist\nalbum") ) → <hex>.jpg
// Device art sync reads this same cache — do not change the recipe.

struct AlbumArtService::State {
    QMutex mutex;
    QSet<QString> inFlight;
    QHash<QString, QStringList> queued;
    QHash<QString, QSet<QString>> attempted;
};

namespace {

// The mac substitutes empties before building the cache path
// (extractArt: artist.isEmpty ? "Unknown Artist" : artist).
QString effectiveArtist(const QString &artist) {
    return artist.isEmpty() ? QStringLiteral("Unknown Artist") : artist;
}
QString effectiveAlbum(const QString &album) {
    return album.isEmpty() ? QStringLiteral("Unknown Album") : album;
}

bool isImageSuffix(const QString &suffix) {
    // Method-2 extension set from the Swift (jpg/jpeg/png/bmp/gif/webp)
    static const QSet<QString> kExts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp")
    };
    return kExts.contains(suffix);
}

// Carry the bytes across without ever deleting the destination first. Both
// callers stage into a live QTemporaryFile, and on Windows a name its owning
// object still holds cannot be removed - not even after close() - so
// remove() failed with a sharing violation and copy() then refused the existing
// file. POSIX lets you unlink an open file, so the same lines have always
// worked there: every folder cover and every device-sourced album picture
// vanished on Windows alone. Reopening for writing is allowed, because the
// holder shares reads and writes and withholds only deletion.
bool copyBytes(const QString &src, const QString &dst) {
    QFile input(src);
    if (!input.open(QIODevice::ReadOnly))
        return false;
    QFile output(dst);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 want = input.size();
    qint64 copied = 0;
    while (copied < want) {
        const QByteArray chunk = input.read(64 * 1024);
        if (chunk.isEmpty())
            break;
        if (output.write(chunk) != chunk.size())
            return false;
        copied += chunk.size();
    }
    output.flush();
    output.close();
    return copied == want && !input.error() && !output.error();
}

// Write a source image into the cache: JPEGs already ≤400px are copied
// verbatim; everything else is decoded, downscaled to ≤400px, and
// encoded as JPEG q90. (The mac copies verbatim and downscales at load
// time; on disk we normalize instead — the KEY contract is unchanged.)
bool storeToCache(const QString &src, const QString &dst) {
    const QString suffix = QFileInfo(src).suffix().toLower();
    const bool jpeg = (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"));

    if (jpeg) {
        QImageReader probe(src);
        const QSize sz = probe.size(); // header read only, no decode
        if (sz.isValid() && sz.width() <= 400 && sz.height() <= 400)
            return copyBytes(src, dst);
    }

    QImage img(src);
    if (img.isNull())
        return false;
    if (img.width() > 400 || img.height() > 400)
        img = img.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img.save(dst, "JPEG", 90);
}

// The names a listener puts on a file on purpose. Order is the mac's.
const char *const kKnownArtNames[] = {"cover", "folder", "artwork", "album",
                                      "front"};

// The extensions method 1 accepts: the mac only trusts a known name when it
// carries a picture extension it can load directly.
bool isArtSuffix(const QString &suffix) {
    return suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("png");
}

// The first file in a folder listing whose basename is one of the known art
// names, matched case-insensitively (which covers the Swift's lowercase and
// Capitalized variants and then some).
QString knownArtIn(const QFileInfoList &entries) {
    for (const char *known : kKnownArtNames) {
        for (const QFileInfo &fi : entries) {
            if (fi.completeBaseName().compare(QLatin1String(known),
                                              Qt::CaseInsensitive) == 0
                && isArtSuffix(fi.suffix().toLower()))
                return fi.absoluteFilePath();
        }
    }
    return QString();
}

// The first loadable picture in a folder listing, whatever it is called.
QString anyImageIn(const QFileInfoList &entries) {
    for (const QFileInfo &fi : entries) {
        if (isImageSuffix(fi.suffix().toLower()))
            return fi.absoluteFilePath();
    }
    return QString();
}

// "CD1", "Disc 2", "disk_1", "LP 3": a disc subfolder of an album, which holds
// no cover of its own because the album's picture sits one level up. The prefix
// is required and the whole name has to be accounted for, so an album that is
// itself named after a year or a volume - "Album 2024", "Vol 2" - is not a disc
// folder and never inherits whatever its own parent happens to contain.
bool isDiscFolder(const QString &name) {
    static const QRegularExpression re(
        QStringLiteral("^(?:cd|disc|disk|vinyl|lp|pt|part)[\\s._-]*[0-9]+$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

// Where an album's picture comes from, in order: a name chosen on purpose in
// the track's own folder, that same name one level up when the folder is only a
// disc, the picture embedded in the file itself, and last of all some image in
// the folder that goes by no recognised name. The last two used to be the other
// way round, which meant a scanned lyric sheet in with the lossless files beat
// the real cover every track already carried. Returns true when cachePath
// exists afterwards.
bool resolveArt(const QString &trackFilepath, const QString &cachePath) {
    const QFileInfo trackInfo(trackFilepath);
    const QDir folder = trackInfo.dir();

    QFileInfoList entries;
    if (folder.exists())
        entries = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);

    const QString own = knownArtIn(entries);
    if (!own.isEmpty() && storeToCache(own, cachePath)) {
        fprintf(stderr, "[art] found folder art: %s\n",
                qUtf8Printable(QFileInfo(own).fileName()));
        return true;
    }

    if (isDiscFolder(folder.dirName())) {
        QDir parent = folder;
        if (parent.cdUp()) {
            const QString above =
                knownArtIn(parent.entryInfoList(QDir::Files | QDir::Readable,
                                                QDir::Name));
            if (!above.isEmpty() && storeToCache(above, cachePath)) {
                fprintf(stderr,
                        "[art] found album cover above disc folder: %s\n",
                        qUtf8Printable(QFileInfo(above).fileName()));
                return true;
            }
        }
    }

    // Embedded art, read by the libav that is already in the process.
    if (trackInfo.exists()
        && zuuned_extract_art(trackFilepath.toUtf8().constData(),
                              cachePath.toUtf8().constData(), 400) == 0
        && QFile::exists(cachePath)) {
        fprintf(stderr, "[art] extracted embedded art from: %s\n",
                qUtf8Printable(trackInfo.fileName()));
        return true;
    }

    const QString stray = anyImageIn(entries);
    if (!stray.isEmpty() && storeToCache(stray, cachePath)) {
        fprintf(stderr, "[art] found image in folder: %s\n",
                qUtf8Printable(QFileInfo(stray).fileName()));
        return true;
    }

    return false;
}

} // namespace

AlbumArtService::AlbumArtService(QObject *parent)
    : QObject(parent), m_state(std::make_shared<State>()) {
    m_cacheDir = Zuuned::ProfilePaths::writableLocation(QStandardPaths::CacheLocation)
                 + QStringLiteral("/art");
    QDir().mkpath(m_cacheDir);
}

QString AlbumArtService::cacheKey(const QString &artist, const QString &album) {
    // Swift: let key = "\(artist)\n\(album)".lowercased(); MD5 of UTF-8.
    const QString key = (artist + QLatin1Char('\n') + album).toLower();
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(digest.toHex());
}

QString AlbumArtService::cachePathFor(const QString &artist, const QString &album) const {
    return m_cacheDir + QLatin1Char('/')
           + cacheKey(effectiveArtist(artist), effectiveAlbum(album))
           + QStringLiteral(".jpg");
}

QString AlbumArtService::cachedArtPath(const QString &artist, const QString &album) const {
    const QString path = cachePathFor(artist, album);
    return QFile::exists(path) ? path : QString();
}

void AlbumArtService::requestArt(const QString &artist, const QString &album,
                                 const QString &trackFilepath) {
    requestSources(artist, album, {trackFilepath});
}

void AlbumArtService::requestSources(const QString &artist, const QString &album,
                                    const QStringList &trackFilepaths) {
    if (artist.isEmpty() && album.isEmpty())
        return;

    const QString path = cachePathFor(artist, album);
    if (QFile::exists(path))
        return; // contract: no-op when cached — caller uses cachedArtPath

    const QString key = cacheKey(effectiveArtist(artist), effectiveAlbum(album));
    const auto state = m_state;
    {
        QMutexLocker lock(&state->mutex);
        for (const QString &source : trackFilepaths) {
            if (!source.isEmpty() && !state->attempted[key].contains(source)
                && !state->queued[key].contains(source))
                state->queued[key].append(source);
        }
        if (state->inFlight.contains(key)) return;
        if (state->queued[key].isEmpty()) {
            // Publish the memoized miss too: the caller may explicitly retry
            // online after an earlier outage without rescanning local bytes.
            QMetaObject::invokeMethod(this, [this, artist, album] {
                emit artReady(artist, album, {}, false);
            }, Qt::QueuedConnection);
            return;
        }
        state->inFlight.insert(key);
    }

    QPointer<AlbumArtService> self(this);
    QThreadPool::globalInstance()->start([self, state, artist, album, path, key] {
        bool ok = false;
        for (;;) {
            QString source;
            {
                QMutexLocker lock(&state->mutex);
                if (ok || state->queued[key].isEmpty()) {
                    state->queued.remove(key);
                    state->inFlight.remove(key);
                    break;
                }
                source = state->queued[key].takeFirst();
                state->attempted[key].insert(source);
            }
            // A miss belongs to this source, not the album. A second folder
            // or another track may supply art that the first file lacked.
            QTemporaryFile staged(QFileInfo(path).dir().filePath(QStringLiteral("auto_XXXXXX.jpg")));
            const bool opened = staged.open();
            staged.close();
            if (!QFile::exists(path) && opened && resolveArt(source, staged.fileName()))
                QFile::copy(staged.fileName(), path); // never replace a custom choice
            ok = QFile::exists(path);
        }
        if (self)
            emit self->artReady(artist, album, ok ? path : QString(), ok);
    });
}

void AlbumArtService::importDeviceArt(const QString &artist,
                                      const QString &album,
                                      const QString &sourcePath) {
    if (album.trimmed().isEmpty() || !QFileInfo::exists(sourcePath))
        return;
    const QString path = cachePathFor(artist, album);
    if (QFile::exists(path)) {
        emit artReady(artist, album, path, true);
        return;
    }

    const QString key = cacheKey(effectiveArtist(artist), effectiveAlbum(album));
    {
        QMutexLocker lock(&m_state->mutex);
        m_state->attempted.remove(key);
    }
    QPointer<AlbumArtService> self(this);
    QThreadPool::globalInstance()->start(
        [self, state = m_state, artist, album, sourcePath, path, key] {
        QTemporaryFile staged(QFileInfo(path).dir().filePath(
            QStringLiteral("device_XXXXXX.jpg")));
        const bool opened = staged.open();
        staged.close();
        const bool prepared = opened
            && storeToCache(sourcePath, staged.fileName());
        if (prepared && !QFile::exists(path))
            QFile::copy(staged.fileName(), path); // never replace custom/local art
        const bool ok = QFile::exists(path);
        {
            QMutexLocker lock(&state->mutex);
            if (ok)
                state->attempted.remove(key);
        }
        if (self)
            emit self->artReady(artist, album, ok ? path : QString(), ok);
    });
}

bool AlbumArtService::isResolving(const QString &artist, const QString &album) const {
    QMutexLocker lock(&m_state->mutex);
    return m_state->inFlight.contains(cacheKey(effectiveArtist(artist), effectiveAlbum(album)));
}

void AlbumArtService::forgetFailure(const QString &artist, const QString &album) {
    QMutexLocker lock(&m_state->mutex);
    m_state->attempted.remove(cacheKey(effectiveArtist(artist), effectiveAlbum(album)));
}
