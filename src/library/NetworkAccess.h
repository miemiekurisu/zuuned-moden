#pragma once

#include <QByteArray>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QString>
#include <QUrl>

// One place decides whether this process reaches an artwork, identity or feed
// provider through a route the operator named, and says out loud which route it
// ended up on.
//
// Recorded 2026-10-22 on the Windows development machine, by the log line a
// failed customise request leaves behind:
//
//     [customize-http] HTTP 0, Qt error 4 (https://api.deezer.com/artist/325381)
//
// Qt error 4 is QNetworkReply::TimeoutError (QtNetwork/qnetworkreply.h), so a
// request started and never got an answer, and four timed curls on the same
// machine say why:
//
//     curl https://api.deezer.com/artist/1006             connect fails, 21.07 s
//     curl https://www.google.com/generate_204            connect fails, 21.06 s
//     curl https://api.github.com                         200, 0.39 s
//     curl -x http://192.168.2.100:8117 the two failures  200 and 204, under 1 s
//
// Those numbers also killed the explanation this file was written to implement.
// The obvious reading was that a Windows application had never been told about
// the one proxy this network has, so a fresh Qt 6.11 process was asked what it
// would do before it had done anything at all:
//
//     env http_proxy set: 0
//     before QCoreApplication: usesSystemConfiguration=1
//     factory answer: NoProxy ; system answer: NoProxy
//     with http_proxy set after init: factory answer: NoProxy
//
// Qt already consults the Windows shell, so installing a system-proxy factory by
// hand is a no-op justified by a comment nobody measured, and it is not here.
// The same probe is the reason an exported http_proxy changes nothing on this
// platform: once the shell owns the answer, the environment does not get a vote.
//
// What was genuinely missing is the route in the log. "HTTP 0, Qt error 4" reads
// identically whether the request went direct into a network that drops it or
// through a proxy that refused it, and this shell currently answers NoProxy -
// ProxyEnable is 0 while ProxyServer is still set to 192.168.2.100:8117 - so the
// truthful app behaviour is to go direct, which reaches GitHub and times out on
// Deezer. That is a machine with its proxy switched off, not a broken client, and
// no status code of 0 tells anybody so.
//
// ZUUNED_PROXY is the one thing this adds: a route the shell does not name, for a
// tester who wants artwork without switching the whole desktop through a proxy,
// and for a gate that has to prove a factory is consulted without depending on
// what the operator's network currently is. Unset - the normal state - nothing is
// installed and Qt's own answer stands on both platforms.
//
// Header-only inline like LibraryPaths, because src/library/*.cpp is also
// compiled stand-alone by gate scripts.
namespace NetworkAccess {

enum class Policy {
    Unchanged,    // nothing configured: whatever Qt does by default, untouched
    Environment,  // ZUUNED_PROXY named a route and a factory now returns it
    Rejected      // ZUUNED_PROXY was set to something unusable
};

struct Decision {
    Policy policy = Policy::Unchanged;
    QString note;  // for the log; never carries a credential
};

// "http host:port" / "socks5 host:port" / "direct".  A QNetworkProxy can carry a
// username and password; this never renders them, because a proxy credential is
// exactly what diagnostics/Redactor.cpp exists to fold.
inline QString describe(const QNetworkProxy &proxy) {
    const QString endpoint = proxy.hostName() + QLatin1Char(':') + QString::number(proxy.port());
    switch (proxy.type()) {
    case QNetworkProxy::HttpProxy:
        if (!proxy.hostName().isEmpty()) return QStringLiteral("http ") + endpoint;
        break;
    case QNetworkProxy::HttpCachingProxy:
        if (!proxy.hostName().isEmpty()) return QStringLiteral("http-caching ") + endpoint;
        break;
    case QNetworkProxy::Socks5Proxy:
        if (!proxy.hostName().isEmpty()) return QStringLiteral("socks5 ") + endpoint;
        break;
    case QNetworkProxy::DefaultProxy:
    case QNetworkProxy::NoProxy:
        break;
    }
    return QStringLiteral("direct");
}

// The proxy this process would use for one HTTPS request right now.  Asked of the
// factory rather than remembered at startup, so a proxy turned on or off
// mid-session shows up in the next log line instead of being contradicted by a
// stale one.
inline QString effectiveHttps() {
    const QNetworkProxyQuery query(QUrl(QStringLiteral("https://example.invalid/")),
                                   QNetworkProxyQuery::UrlRequest);
    const QList<QNetworkProxy> proxies = QNetworkProxyFactory::proxyForQuery(query);
    return proxies.isEmpty() ? QStringLiteral("direct") : describe(proxies.first());
}

// Accepts [scheme://][user[:password]@]host:port, the shape every proxy tool on
// both platforms uses.  A port is required: guessing 80 or 1080 would quietly
// send library traffic somewhere the operator did not ask for, whereas refusing
// says so.  An unbracketed host with two colons is ambiguous rather than IPv6, so
// it is refused for the same reason.  Never logs; the caller prints the reason.
inline QNetworkProxy parse(const QString &spec, bool *ok = nullptr) {
    if (ok) *ok = false;
    QString rest = spec.trimmed();
    if (rest.isEmpty()) return {};

    QNetworkProxy::ProxyType type = QNetworkProxy::HttpProxy;
    const int schemeEnd = rest.indexOf(QLatin1String("://"));
    if (schemeEnd > 0) {
        const QString scheme = rest.left(schemeEnd).trimmed().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
            || scheme == QLatin1String("socks5h") || scheme == QLatin1String("socks5")
            || scheme == QLatin1String("socks"))
            type = scheme.startsWith(QLatin1String("socks")) ? QNetworkProxy::Socks5Proxy
                                                             : QNetworkProxy::HttpProxy;
        else
            return {};  // an unknown dialect is not a proxy
        rest = rest.mid(schemeEnd + 3);
    }

    QString user, password;
    const int at = rest.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {
        const QString credentials = rest.left(at);
        rest = rest.mid(at + 1);
        const int split = credentials.indexOf(QLatin1Char(':'));
        user = split >= 0 ? credentials.left(split) : credentials;
        password = split >= 0 ? credentials.mid(split + 1) : QString();
        if (user.isEmpty() || user.contains(QLatin1Char('@'))) return {};
    }

    if (rest.startsWith(QLatin1Char('['))) {
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close < 0 || !rest.mid(close + 1).startsWith(QLatin1Char(':'))) return {};
        const QString host = rest.left(close + 1);
        bool portOk = false;
        const int port = rest.mid(close + 2).toInt(&portOk);
        if (!portOk || port < 1 || port > 65535) return {};
        if (ok) *ok = true;
        return QNetworkProxy(type, host, quint16(port), user, password);
    }
    if (rest.count(QLatin1Char(':')) != 1) return {};
    const int colon = rest.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == rest.length() - 1) return {};
    const QString host = rest.left(colon);
    bool portOk = false;
    const int port = rest.mid(colon + 1).toInt(&portOk);
    if (!portOk || port < 1 || port > 65535 || host.contains(QLatin1Char('/'))
        || host.contains(QLatin1Char('@')))
        return {};
    if (ok) *ok = true;
    return QNetworkProxy(type, host, quint16(port), user, password);
}

// Installed only for a usable ZUUNED_PROXY, the one case where the answer is
// known once and cannot change underneath us.  Setting a factory outranks the
// platform default, which is what makes the override mean something on a platform
// whose default answer is the shell's.
class FixedProxyFactory final : public QNetworkProxyFactory {
public:
    explicit FixedProxyFactory(const QNetworkProxy &proxy) : m_proxy(proxy) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery & = QNetworkProxyQuery()) override {
        return {m_proxy};
    }

private:
    QNetworkProxy m_proxy;
};

// A refused value is quoted back so the operator can see what is wrong with it.
// Anything before the last '@' goes first, because a proxy credential is exactly
// what diagnostics/Redactor.cpp exists to fold and the Redactor only ever sees a
// whole line after this note has been written into it: "ZUUNED_PROXY is not a
// proxy" is worth printing, "ZUUNED_PROXY=http://name:password@host" is not.
inline QString redacted(const QString &value) {
    const int at = value.lastIndexOf(QLatin1Char('@'));
    return at < 0 ? value : QStringLiteral("[credential]@") + value.mid(at + 1);
}

// Installs the override if there is one and returns what happened, for one
// startup log line.  With ZUUNED_PROXY unset this changes nothing at all, on
// either platform.
inline Decision apply() {
    Decision decision;
    const QByteArray configured = qgetenv("ZUUNED_PROXY").trimmed();
    if (configured.isEmpty()) return decision;
    bool ok = false;
    const QNetworkProxy proxy = parse(QString::fromUtf8(configured), &ok);
    if (ok) {
        QNetworkProxyFactory::setApplicationProxyFactory(new FixedProxyFactory(proxy));
        decision.policy = Policy::Environment;
    } else {
        // Refusing loudly beats reaching the library through a route the operator
        // did not ask for, so the unusable value is quoted back - folded.
        decision.policy = Policy::Rejected;
        decision.note = QStringLiteral("ZUUNED_PROXY=%1 is not "
                                       "[scheme://][user[:pass]@]host:port")
                            .arg(redacted(QString::fromUtf8(configured)));
    }
    return decision;
}

// One line for the session log: where the route came from, what it answers for
// HTTPS, and the offending value when one was refused.
inline QString summary(const Decision &decision) {
    QString origin;
#ifdef Q_OS_WIN
    origin = QStringLiteral("Windows proxy settings");
#else
    origin = QStringLiteral("Qt default route");
#endif
    switch (decision.policy) {
    case Policy::Unchanged:
        break;
    case Policy::Environment:
        origin = QStringLiteral("ZUUNED_PROXY");
        break;
    case Policy::Rejected:
        origin = QStringLiteral("ZUUNED_PROXY refused, falling back to ") + origin;
        break;
    }
    QString text = QStringLiteral("%1: HTTPS goes %2").arg(origin, effectiveHttps());
    if (!decision.note.isEmpty()) text += QStringLiteral(" (%1)").arg(decision.note);
    return text;
}

}  // namespace NetworkAccess
