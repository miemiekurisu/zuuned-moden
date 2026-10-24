#pragma once

#include "Redactor.h"
#include "SupportReport.h"
#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>

class DiagnosticsService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool loggingAvailable READ loggingAvailable NOTIFY statusChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QString logError READ logError NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QUrl lastReportUrl READ lastReportUrl NOTIFY reportChanged)
public:
    explicit DiagnosticsService(QObject *parent = nullptr);
    ~DiagnosticsService() override;
    bool loggingAvailable() const;
    QString logDirectory() const;
    QString logError() const;
    bool busy() const { return m_busy; }
    QUrl lastReportUrl() const { return m_lastReportUrl; }
    static Zuuned::Diagnostics::Redactor::Context redactionContext();
    void setContextProvider(std::function<QJsonObject()> provider) { m_contextProvider = std::move(provider); }
    // What this build can actually convert, answered by asking the libraries
    // rather than by a table that can drift. `bundledLibav` names the libav the
    // app links in-process; `ffmpegPath`/`ffprobePath` are the external commands
    // libzune shells out to for photo thumbnails and a few probes, empty when
    // they are not on PATH. `downloadUrl` is where to get them.
    Q_INVOKABLE QVariantMap conversionTools() const;
    // Which UI language this process is actually painting in, and what
    // "Follow system" means on this machine. Both are decided by the launcher
    // (src/main.cpp), the only place that can know, so Settings can tell a
    // request apart from what is on screen instead of implying that picking a
    // language repaints the page you are standing on.
    Q_INVOKABLE QString uiLanguage() const;
    Q_INVOKABLE QString systemUiLanguage() const;
    static void setUiLanguageApplied(const QString &code);
    Q_INVOKABLE QUrl suggestedReportUrl() const;
    Q_INVOKABLE void exportReport(const QUrl &destination);
    Q_INVOKABLE void openLogFolder() const;
signals:
    void statusChanged();
    void busyChanged();
    void reportChanged();
    void exportFinished(bool success, const QUrl &reportUrl, const QString &error);
private:
    static QString s_uiLanguageApplied;
    bool m_busy = false;
    bool m_lastAvailable = false;
    QString m_lastLogError;
    QUrl m_lastReportUrl;
    std::function<QJsonObject()> m_contextProvider;
    QFuture<Zuuned::Diagnostics::ReportResult> m_export;
};
