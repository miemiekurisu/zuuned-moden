#include "ArtworkHttp.h"
#include "NetworkAccess.h"

#include <QDateTime>
#include <QEventLoop>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "../UiText.h"

namespace {
const QByteArray kUA = "Zuuned/1.0 ( https://badgeromen.xyz )";
QMutex musicBrainzMutex;
std::chrono::steady_clock::time_point nextMusicBrainzRequest;
int lastMusicBrainzBusyStatus = 503;
constexpr qint64 kMaximumRetryWaitMs = 8000;
// The music store's search endpoint publishes no quota. What it does publish,
// observed here, is a plain HTTP 429 a couple of dozen searches into a
// library-sized burst - and a refused search reads to the app exactly like an
// album the store does not carry, so a burst is how every cover in a real
// collection ends up blank. One search in flight, spaced like this, is the fix.
QMutex storeSearchMutex;
std::chrono::steady_clock::time_point nextStoreSearch;
constexpr qint64 kStoreSearchIntervalMs = 2000;
constexpr qint64 kMaximumStoreSearchWaitMs = 30000;
std::atomic_bool artworkStopping{false};

ArtworkHttp::Response cancelledArtworkResponse() {
    ArtworkHttp::Response result;
    result.networkError = QNetworkReply::OperationCanceledError;
    return result;
}

ArtworkHttp::Response artworkHttpAttempt(const QUrl &url, bool musicBrainz) {
    if (artworkStopping.load()) return cancelledArtworkResponse();
    QNetworkAccessManager nam;
    nam.setTransferTimeout(musicBrainz ? 10000 : 30000);
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUA);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer deadline;
    QTimer shutdownPoll;
    QObject::connect(&shutdownPoll, &QTimer::timeout, reply, [reply] {
        if (artworkStopping.load()) reply->abort();
    });
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [reply](qint64 received, qint64 total) {
        if (received > 32 * 1024 * 1024 || total > 32 * 1024 * 1024)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(musicBrainz ? 12000 : 45000);
    shutdownPoll.start(25);
    loop.exec();
    ArtworkHttp::Response result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.networkError = reply->error();
    result.retryAfter = reply->rawHeader("Retry-After");
    // Error responses carry useful status/header information even when
    // Qt classifies them as network errors. Never use body text to retry.
    result.body = reply->readAll();
    if (!result.success())
        // The route belongs in the line because a timeout and a refused
        // connection read identically without it: HTTP 0 plus Qt error 4 means
        // only "no answer", and on a machine that reaches its providers through
        // a proxy the reason is usually that this process used none.
        fprintf(stderr, "[customize-http] HTTP %d, Qt error %d via %s (%s)\n",
                result.status, int(result.networkError),
                qUtf8Printable(NetworkAccess::effectiveHttps()),
                qUtf8Printable(url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery)));
    reply->deleteLater();
    return result;
}

ArtworkHttp::Response artworkStoreSearch(const QUrl &url) {
    if (artworkStopping.load()) return cancelledArtworkResponse();
    // One store search in flight and no two starts closer than the interval,
    // across every worker in this process. The lock is held through a retry so
    // a Retry-After earned by one album delays the next album as well.
    while (!storeSearchMutex.tryLock(25))
        if (artworkStopping.load()) return cancelledArtworkResponse();
    const auto unlock = qScopeGuard([] { storeSearchMutex.unlock(); });
    ArtworkHttp::Response result;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (artworkStopping.load()) return cancelledArtworkResponse();
        const auto now = std::chrono::steady_clock::now();
        const qint64 wait = std::chrono::duration_cast<std::chrono::milliseconds>(
            nextStoreSearch - now).count();
        if (wait > kMaximumStoreSearchWaitMs) {
            // Past this point the honest answer is "the store is busy", which
            // parks the album and asks again later. Returning an empty result
            // instead would file a rate limit as "this album has no cover".
            fprintf(stderr, "[album-art] store search waits behind a %lld ms cooldown\n",
                    static_cast<long long>(wait));
            result.status = 503;
            result.networkError = QNetworkReply::ServiceUnavailableError;
            return result;
        }
        for (qint64 remaining = wait; remaining > 0; remaining -= 25) {
            if (artworkStopping.load()) return cancelledArtworkResponse();
            QThread::msleep(static_cast<unsigned long>(qMin(qint64(25), remaining)));
        }
        nextStoreSearch = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(kStoreSearchIntervalMs);
        result = artworkHttpAttempt(url, false);
        if (result.success() || (result.status != 429 && result.status != 503)) break;
        const qint64 requested = ArtworkHttp::retryAfterMs(result.retryAfter);
        const qint64 backoff = requested >= 0
            ? requested : kStoreSearchIntervalMs * 4 * (attempt + 1);
        if (backoff > kMaximumStoreSearchWaitMs) break;
        nextStoreSearch = qMax(nextStoreSearch,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(backoff));
    }
    return result;
}
} // namespace

void ArtworkHttp::beginShutdown() { artworkStopping.store(true); }

// Only the search endpoint is paced. Its answers cost the store nothing but
// query parsing, which is precisely why it is the one that pushes back; the
// CDN that serves the pictures it names never sees more than one request per
// cover. A host that merely contains the name is not the store.
bool ArtworkHttp::isStoreSearch(const QUrl &url) {
    return url.host().compare(QLatin1String("itunes.apple.com"), Qt::CaseInsensitive) == 0
        && url.path().compare(QLatin1String("/search"), Qt::CaseInsensitive) == 0;
}

qint64 ArtworkHttp::retryAfterMs(const QByteArray &header) {
    const QByteArray value = header.trimmed();
    if (value.isEmpty()) return -1;
    bool ok = false;
    const qint64 seconds = value.toLongLong(&ok);
    // Bound arithmetic while retaining a long server cooldown. Such a
    // response returns immediately rather than sleeping on a worker.
    if (ok && seconds >= 0) return qMin(seconds, qint64(86400)) * 1000;
    // Qt's RFC2822 parser expects a numeric zone; HTTP dates use GMT.
    QString dateText = QString::fromLatin1(value);
    if (dateText.endsWith(QLatin1String(" GMT"), Qt::CaseInsensitive)) {
        dateText.chop(4);
        dateText += QStringLiteral(" +0000");
    }
    const QDateTime date = QDateTime::fromString(dateText, Qt::RFC2822Date);
    return date.isValid() ? qBound(qint64(0), QDateTime::currentDateTimeUtc().msecsTo(date),
                                  qint64(86400000)) : -1;
}

ArtworkHttp::Response ArtworkHttp::get(const QUrl &url, bool musicBrainz) {
    if (artworkStopping.load()) return cancelledArtworkResponse();
    if (!musicBrainz)
        return isStoreSearch(url) ? artworkStoreSearch(url) : artworkHttpAttempt(url, false);
    // Only one MusicBrainz request can start per second across every
    // sheet/worker in this process. Hold the lock through retries so a
    // second lookup also observes a provider-wide Retry-After cooldown.
    while (!musicBrainzMutex.tryLock(25))
        if (artworkStopping.load()) return cancelledArtworkResponse();
    const auto unlock = qScopeGuard([] { musicBrainzMutex.unlock(); });
    Response result;
    result.status = lastMusicBrainzBusyStatus;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (artworkStopping.load()) return cancelledArtworkResponse();
        const auto now = std::chrono::steady_clock::now();
        const qint64 wait = std::chrono::duration_cast<std::chrono::milliseconds>(
            nextMusicBrainzRequest - now).count();
        if (wait > kMaximumRetryWaitMs) return result;
        for (qint64 remaining = wait; remaining > 0; remaining -= 25) {
            if (artworkStopping.load()) return cancelledArtworkResponse();
            QThread::msleep(static_cast<unsigned long>(qMin(qint64(25), remaining)));
        }
        nextMusicBrainzRequest = std::chrono::steady_clock::now() + std::chrono::milliseconds(1100);
        result = artworkHttpAttempt(url, true);
        if (result.success() || (result.status != 429 && result.status != 503)) break;
        lastMusicBrainzBusyStatus = result.status;
        const qint64 requested = retryAfterMs(result.retryAfter);
        const qint64 backoff = requested >= 0 ? requested : 1200 * (attempt + 1);
        nextMusicBrainzRequest = qMax(nextMusicBrainzRequest,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(backoff));
    }
    return result;
}

QString ArtworkHttp::userMessage(const Response &response, bool musicBrainz) {
    if (response.success()) return {};
    if (response.status == 429 || response.status == 503)
        return musicBrainz
            ? UiText::tr("MusicBrainz is busy. Try again shortly, or choose a cover from your computer.")
            : UiText::tr("The artwork provider is busy. Try again shortly.");
    if (response.status == 404 || response.status == 410)
        return UiText::tr("That artwork is no longer available. Choose another cover.");
    if (response.status == 401 || response.status == 403)
        return musicBrainz ? UiText::tr("MusicBrainz refused the lookup. Try again later, or choose a local cover.")
                          : UiText::tr("The artwork provider refused access to that image. Choose another cover.");
    if (response.status >= 500)
        return musicBrainz ? UiText::tr("MusicBrainz is unavailable. Try again later.")
                          : UiText::tr("The artwork provider is unavailable. Try again later.");
    if (response.status >= 400)
        return musicBrainz ? UiText::tr("MusicBrainz could not use that search. Check the album and artist names.")
                          : UiText::tr("The artwork request was rejected. Choose another cover.");
    return musicBrainz ? UiText::tr("Could not reach MusicBrainz. Check your connection and try again.")
                      : UiText::tr("Could not reach the artwork provider. Check your connection and try again.");
}

QByteArray ArtworkHttp::getBody(const QUrl &url, bool musicBrainz, QString *error) {
    const auto response = get(url, musicBrainz);
    if (error) *error = userMessage(response, musicBrainz);
    return response.success() ? response.body : QByteArray();
}
