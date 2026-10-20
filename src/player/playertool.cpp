/*
 * playertool — headless CLI gate for the Phase 4 playback core.
 *
 *   playertool play <file> <seconds>   play N seconds, print 4Hz positions
 *   playertool queue <f1> <f2>         verify EOF auto-advance fires once
 *   playertool seek <file> <targetSec> verify absolute seek lands ±0.5s
 *
 *   playertool play <file> <seconds> --audible --mute
 *                                    real audio output, silenced samples
 *
 * Uses ao=null by default (ZUUNED_MPV_AO) so the gate NEVER hijacks the
 * user's audio session; pass --audible as the last arg for real output, and
 * --mute to keep that output silent while mpv still opens the device and
 * drives playback from it.  The play summary prints audio_out=<driver>, the
 * output mpv really bound, which is how a gate tells the platform backend
 * from the null driver instead of trusting an exit code.
 */

#if defined(_WIN32)
/* The console renders the active ANSI code page, so every UTF-8 path and device
 * string printed below would arrive to the operator as '?'.  This tool uses none
 * of the POSIX surface, so only the console helper is declared here rather than
 * forcing compat/win/wincompat.h through the Qt and vendor headers. */
extern "C" void zuuned_win_console_utf8(void);
#endif

#include "PlayerService.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

static QVariantMap item(const QString &path) {
    return {{"filepath", path}, {"title", path}, {"artist", ""},
            {"album", ""}, {"libraryId", -1}};
}

int main(int argc, char *argv[]) {
#if defined(_WIN32)
    zuuned_win_console_utf8();
#endif
    // Default: silent audio output (decodes + advances time, no device)
    bool audible = false;
    bool mute = false;
    for (int i = 1; i < argc; i++) {
        if (QByteArray(argv[i]) == "--audible")
            audible = true;
        else if (QByteArray(argv[i]) == "--mute")
            mute = true;
    }
    if (!audible && qgetenv("ZUUNED_MPV_AO").isEmpty())
        qputenv("ZUUNED_MPV_AO", "null");

    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        fprintf(stderr, "usage: playertool play|queue|seek ...\n");
        return 2;
    }
    const QString cmd = args[1];

    PlayerService player;
    int exitCode = 1;

    // Silence without the null driver: mpv opens the real audio output and
    // still clocks playback from it, so --mute exercises the backend while
    // keeping a gate quiet enough to run on a user's desktop.
    if (mute)
        player.toggleMute();

    if (cmd == QStringLiteral("play")) {
        const QString file = args[2];
        const double seconds = args.size() > 3 ? args[3].toDouble() : 5.0;

        static double lastPos = -1;
        static bool monotonic = true;
        QObject::connect(&player, &PlayerService::positionChanged, [&] {
            const double pos = player.positionMs() / 1000.0;
            printf("pos=%.2f\n", pos);
            fflush(stdout);
            if (pos + 0.05 < lastPos)
                monotonic = false;
            lastPos = pos;
        });

        player.setQueue({item(file)}, 0);

        QTimer::singleShot(int(seconds * 1000), [&] {
            printf("duration=%.2f monotonic=%s final_pos=%.2f audio_out=%s\n",
                   player.durationMs() / 1000.0,
                   monotonic ? "yes" : "NO", lastPos,
                   qUtf8Printable(player.audioDriver()));
            exitCode = (monotonic && lastPos > 0) ? 0 : 1;
            app.quit();
        });
    } else if (cmd == QStringLiteral("queue")) {
        if (args.size() < 4) {
            fprintf(stderr, "queue needs two files\n");
            return 2;
        }
        static int advances = 0;
        static int lastIndex = 0;
        QObject::connect(&player, &PlayerService::queueChanged, [&] {
            if (player.queueIndex() != lastIndex) {
                printf("queueIndex: %d -> %d\n", lastIndex, player.queueIndex());
                fflush(stdout);
                lastIndex = player.queueIndex();
                advances++;
            }
        });
        // End-of-queue is MAC-PARITY: with keep-open=yes mpv fires no
        // END_FILE for the last entry — it pauses at its end. Success =
        // exactly one advance, then paused at the tail of track 2.
        QObject::connect(&player, &PlayerService::stateChanged, [&] {
            if (!player.playing() && !player.stopped()
                && advances == 1 && player.queueIndex() == 1) {
                QTimer::singleShot(400, [&] {
                    const double pos = player.positionMs() / 1000.0;
                    const double dur = player.durationMs() / 1000.0;
                    const bool atEnd = dur > 0 && pos >= dur - 0.5;
                    printf("paused-at-end pos=%.2f dur=%.2f advances=%d %s\n",
                           pos, dur, advances, atEnd ? "PASS" : "FAIL");
                    exitCode = atEnd ? 0 : 1;
                    app.quit();
                });
            }
        });
        player.setQueue({item(args[2]), item(args[3])}, 0);

        QTimer::singleShot(30000, [&] {
            printf("TIMEOUT (advances=%d)\n", advances);
            app.quit();
        });
    } else if (cmd == QStringLiteral("seek")) {
        const QString file = args[2];
        const double target = args.size() > 3 ? args[3].toDouble() : 60.0;

        static bool seeked = false;
        static bool done = false;
        QObject::connect(&player, &PlayerService::positionChanged, [&] {
            const double pos = player.positionMs() / 1000.0;
            if (!seeked && pos > 1.0) {
                seeked = true;
                printf("seeking to %.2f from %.2f\n", target, pos);
                player.seekMs(target * 1000.0);
                return;
            }
            if (seeked && !done && pos > 2.0) {
                done = true;
                const bool pass = qAbs(pos - target) <= 0.5 ||
                                  (pos >= target && pos <= target + 1.0);
                printf("post-seek pos=%.2f target=%.2f %s\n",
                       pos, target, pass ? "PASS" : "FAIL");
                exitCode = pass ? 0 : 1;
                app.quit();
            }
        });
        player.setQueue({item(file)}, 0);

        QTimer::singleShot(20000, [&] { printf("TIMEOUT\n"); app.quit(); });
    } else {
        fprintf(stderr, "unknown command: %s\n", qUtf8Printable(cmd));
        return 2;
    }

    app.exec();
    return exitCode;
}
