#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QString>
#include <QUrl>

// Blocking artwork transport, called only on workers. MusicBrainz calls
// share application-wide pacing and bounded HTTP429/503 retries, and so do
// music store searches: one of each in flight, spaced, with a provider-wide
// cooldown after a rate limit. Artwork downloads and other hosts are not paced.
namespace ArtworkHttp {
struct Response {
    QByteArray body, retryAfter;
    int status = 0;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    bool success() const {
        return networkError == QNetworkReply::NoError && status >= 200 && status < 300;
    }
};
Response get(const QUrl &url, bool musicBrainz = false);
// True for the music store's search endpoint, which get() paces process-wide.
// Artwork downloads and every other host answer false: a cover is one picture,
// and an unpaced lookup for it would delay every album behind it for nothing.
bool isStoreSearch(const QUrl &url);
// Terminal process shutdown: abort active transport and refuse queued requests.
// Call before draining workers, while QCoreApplication is still alive.
void beginShutdown();
QByteArray getBody(const QUrl &url, bool musicBrainz, QString *error = nullptr);
QString userMessage(const Response &response, bool musicBrainz = false);
// Returns -1 for an absent/invalid header. HTTP dates and delay-seconds
// are accepted; very long server delays stop automatic retries.
qint64 retryAfterMs(const QByteArray &header);
}
