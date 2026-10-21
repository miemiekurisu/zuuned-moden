/*
 * librarytool — CLI gate for the library core.
 *
 *   librarytool scan <folder> <tmpdb>
 *     Registers <folder> as a music watch folder in <tmpdb>, scans it
 *     (synchronously, on this thread), prints per-chunk progress, final
 *     counts, and 5 sample rows.
 *
 *   librarytool videoscan <folder> <tmpdb> [type]
 *     Phase 6 gate: registers <folder> as a video watch folder
 *     (type: movies|tv|anime, default tv), scans, then runs the TMDB
 *     matcher SYNCHRONOUSLY on every unmatched row and prints what
 *     resolved — series, TMDB titles, poster paths.
 *
 *   librarytool names
 *     VideoNaming self-test: the parsing cases the mac fixed one bug at
 *     a time, frozen as expectations.
 *
 *   librarytool paths
 *     Path/text self-test: what a filename may contain on the host OS, how two
 *     spellings of one file compare, how a playlist line resolves, and how
 *     playlist bytes decode.  Expectations are branched where the right answer
 *     genuinely differs by platform, so one source gates both.
 */
/* Keep first on Windows: the compat shim declares the POSIX surface this
 * file uses (64-bit struct stat, dirent, poll, ...) and the console code page
 * helper below.  Whichever <sys/stat.h> the compiler parses first decides the
 * struct layout, and the Qt headers include one.  See compat/win/posix_shim.h.
 * Other platforms build without it.  */
#if defined(_WIN32)
#include "wincompat.h"
#endif

#include "LibraryDb.h"
#include "LibraryPaths.h"
#include "LibraryScanner.h"
#include "PlaylistText.h"
#include "VideoIdentity.h"
#include "VideoMatcher.h"
#include "VideoNaming.h"

#include <QCoreApplication>
#include <QStringList>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QFile>
#include <sqlite3.h>

#include <cstdio>

static int runNamesSelfTest() {
    struct Case {
        const char *what;
        QString got;
        QString expect;
    };
    const QVector<Case> cases = {
        {"normalize: dots→spaces",
         VideoNaming::normalizeSeriesName(QStringLiteral("SpongeBob.SquarePants")),
         QStringLiteral("SpongeBob SquarePants")},
        {"normalize: tracker prefix",
         VideoNaming::normalizeSeriesName(QStringLiteral("www.UIndex.org    -    ONE PIECE")),
         QStringLiteral("ONE PIECE")},
        {"normalize: [group] prefix",
         VideoNaming::normalizeSeriesName(QStringLiteral("[SubsPlease] Frieren")),
         QStringLiteral("Frieren")},
        {"folder junk truncation",
         VideoNaming::cleanFolderSeriesName(
             QStringLiteral("Adventure Time S01 1080p HMAX WEBRip DD2.0 x265-Kappa")),
         QStringLiteral("Adventure Time")},
        {"folder junk keeps clean names",
         VideoNaming::cleanFolderSeriesName(QStringLiteral("The Office (US) (2005)")),
         QStringLiteral("The Office (US) (2005)")},
        {"derive: season container",
         VideoNaming::deriveSeriesFromPath(
             QStringLiteral("/tv/Breaking Bad/Season 1/Breaking.Bad.S01E01.mkv")),
         QStringLiteral("Breaking Bad")},
        {"derive: episode-dir layout",
         VideoNaming::deriveSeriesFromPath(
             QStringLiteral("/tv/The Wire/The.Wire.S02E03.720p/The.Wire.S02E03.mkv")),
         QStringLiteral("The Wire")},
        {"derive: specials",
         VideoNaming::deriveSeriesFromPath(
             QStringLiteral("/tv/Doctor Who/Specials/xmas.mkv")),
         QStringLiteral("Doctor Who")},
        {"clean: scene movie name",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Die.Hard.1988.1080p.BluRay.x264-SARTRE.mkv")),
         QStringLiteral("Die Hard 1988")},
        {"clean: keeps real subtitle",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Mission Impossible - Fallout.mkv")),
         QStringLiteral("Mission Impossible Fallout")},
        {"clean: paren year preserved",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Hackers (1995) [1080p].mp4")),
         QStringLiteral("Hackers 1995")},
        {"clean: trailing disc idx after year",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Back To The Future Part III 1990 1.mkv")),
         QStringLiteral("Back To The Future Part III 1990")},
        {"clean: Apollo 13 stays intact",
         VideoNaming::cleanFilenameForSearch(QStringLiteral("Apollo 13.mkv")),
         QStringLiteral("Apollo 13")},
        {"squash equality",
         VideoNaming::squash(QStringLiteral("Bob's Burgers")),
         QStringLiteral("bobsburgers")},
    };

    int failed = 0;
    for (const Case &c : cases) {
        const bool ok = c.got == c.expect;
        if (!ok)
            failed++;
        printf("  %s %s: got '%s'%s\n", ok ? "PASS" : "FAIL", c.what,
               c.got.toUtf8().constData(),
               ok ? "" : (QStringLiteral(" expected '") + c.expect
                          + QLatin1Char('\'')).toUtf8().constData());
    }

    // Scoring sanity: exact squash match wins; junk stays below threshold.
    const double exact = VideoNaming::scoreCandidate(
        QStringLiteral("bobs burgers"), QString(),
        QStringLiteral("Bob's Burgers"), QStringLiteral("2011"), 100);
    const double junk = VideoNaming::scoreCandidate(
        QStringLiteral("some totally different show"), QString(),
        QStringLiteral("Bob's Burgers"), QStringLiteral("2011"), 100);
    printf("  %s score exact-squash (%.2f >= 1.0)\n",
           exact >= 1.0 ? "PASS" : "FAIL", exact);
    printf("  %s score junk rejected (%.2f < %.2f)\n",
           junk < VideoNaming::kAcceptThreshold ? "PASS" : "FAIL", junk,
           VideoNaming::kAcceptThreshold);
    if (exact < 1.0 || junk >= VideoNaming::kAcceptThreshold)
        failed++;

    // Absolute anime mapping: 3 seasons of 10 → ep 25 = S3E5.
    QHash<int, int> counts{{1, 10}, {2, 10}, {3, 10}};
    int s = 0, e = 0;
    const bool mapped = VideoMatcher::mapAbsoluteEpisode(25, counts, &s, &e);
    const bool mapOk = mapped && s == 3 && e == 5;
    printf("  %s absolute episode 25 → S%dE%d\n", mapOk ? "PASS" : "FAIL", s, e);
    if (!mapOk)
        failed++;

    printf("names self-test: %s (%d failures)\n",
           failed == 0 ? "ALL PASS" : "FAILED", failed);
    return failed == 0 ? 0 : 1;
}

// Exercise the actual commit SQL with an old matcher snapshot; no HTTP.
struct VideoMatcherCustomizationTest {
    static void writeStaleResult(VideoMatcher &matcher, const LibVideo &old) {
        VideoMatcher::SeriesResolution meta;
        meta.tmdbId = 99; meta.title = QStringLiteral("Automatic title");
        meta.posterLocalPath = QStringLiteral("/fixture/automatic.jpg");
        matcher.writeMovieRow(old.id, meta);
        matcher.writeTvRow(old, meta, nullptr, {}, -1, -1);
    }
};

static int runCustomizeSelfTest() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir scratch;
    LibraryDb db;
    if (!scratch.isValid() || !db.open(scratch.filePath(QStringLiteral("library.db"))))
        return 1;
    int failed = 0;
    auto check = [&failed](bool ok, const char *label) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failed;
    };
    LibVideo seed;
    seed.filepath = QStringLiteral("/fixture/series/episode.mkv");
    seed.filename = QStringLiteral("episode.mkv");
    seed.category = QStringLiteral("tv"); seed.series = QStringLiteral("Original");
    seed.season = 2; seed.episode = 7; seed.durationMs = 1800000;
    check(db.insertVideo(seed), "video fixture inserted");
    LibVideo video = db.allVideos().first();
    db.updateVideoPosition(video.id, 42000);
    db.setVideosWatched({video.id}, true);
    video.tmdbId = -1; video.tmdbTitle = QStringLiteral("My fan edit");
    video.tmdbPoster = QStringLiteral("/fixture/picked-cover.jpg");
    video.customPoster = true;
    video.tmdbCached = true; video.userEdited = true;
    check(db.begin() && db.updateVideoCustomization(video) && db.commit(),
          "manual identity and chosen art commit together");
    video = db.allVideos().first();
    check(video.tmdbId == -1 && video.userEdited && video.tmdbCached
          && video.tmdbPoster == QStringLiteral("/fixture/picked-cover.jpg"),
          "manual identity and artwork remain pinned");
    check(video.season == 2 && video.episode == 7 && video.watched
          && video.lastPositionMs == 42000,
          "customizing preserves episode numbering and playback state");
    VideoMatcher matcher(scratch.filePath(QStringLiteral("library.db")));
    VideoMatcherCustomizationTest::writeStaleResult(matcher, video);
    check(db.allVideos().first().tmdbTitle == QStringLiteral("My fan edit")
          && db.allVideos().first().tmdbPoster == QStringLiteral("/fixture/picked-cover.jpg"),
          "already-running matcher cannot overwrite a manual identity or poster");

    db.begin();
    video.tmdbTitle = QStringLiteral("Must roll back");
    check(db.updateVideoCustomization(video), "first row staged in transaction");
    LibVideo missing = video; missing.id += 1000;
    check(!db.updateVideoCustomization(missing), "missing target rejects the batch");
    check(db.rollback() && db.allVideos().first().tmdbTitle == QStringLiteral("My fan edit"),
          "failed batch restores earlier row edits");

    video = db.allVideos().first();
    video.tmdbId = 0; video.tmdbCached = false; video.userEdited = false;
    video.tmdbTitle.clear(); video.tmdbPoster.clear();
    video.customPoster = false;
    check(db.begin() && db.updateVideoCustomization(video) && db.commit(),
          "reset commits automatic state");
    video = db.allVideos().first();
    check(video.tmdbId == 0 && !video.tmdbCached && !video.userEdited
          && video.tmdbPoster.isEmpty(), "reset releases the matcher user-edit guard");
    // Reset identity, then choose artwork: identity can match while the
    // separately pinned image survives both movie and TV matcher paths.
    video.tmdbPoster = QStringLiteral("/fixture/independent.jpg"); video.customPoster = true;
    check(db.begin() && db.updateVideoCustomization(video) && db.commit(),
          "reset identity plus chosen art commits independently");
    VideoMatcherCustomizationTest::writeStaleResult(matcher, video);
    video = db.allVideos().first();
    check(video.tmdbId == 99 && video.tmdbTitle == QStringLiteral("Automatic title")
          && video.tmdbPoster == QStringLiteral("/fixture/independent.jpg") && video.customPoster,
          "automatic identity lookup preserves independent custom artwork");
    const auto parsed = VideoNaming::parseIdentity(
        QStringLiteral("/tv/Original Show/Season 2/Original.Show.S02E07.mkv"), QStringLiteral("tv"));
    check(parsed.series == QStringLiteral("Original Show") && parsed.season == 2 && parsed.episode == 7,
          "reset uses scanner folder truth and filename episode numbers");

    LibTrack track;
    track.filepath = QStringLiteral("/fixture/song.flac");
    track.title = QStringLiteral("Song"); track.artist = QStringLiteral("Performer");
    track.albumartist = QStringLiteral("Owner"); track.album = QStringLiteral("Old sleeve");
    check(db.upsertTrack(track), "music fixture inserted");
    LibTrack edited = db.allTracks().first();
    edited.albumartist = QStringLiteral("New owner"); edited.album = QStringLiteral("New sleeve");
    check(db.begin() && db.updateTrackMetadata(edited.id, edited, true) && db.commit(),
          "album identity commits with canonical owner");
    track.mtime = 12345;
    check(db.upsertTrack(track), "scanner revisits the original file metadata");
    edited = db.allTracks().first();
    check(edited.albumartist == QStringLiteral("New owner")
          && edited.album == QStringLiteral("New sleeve") && edited.userEdited
          && edited.mtime == 12345, "rescan refreshes file stats and preserves chosen identity");
    // Reconstruct the actual pre-Customize schema, including pre-slot
    // playlist tables. Merely lowering user_version on a current DB
    // leaves future tables behind and does not represent an old library.
    const QByteArray legacySchema =
        // Rewind every later schema era, not just the Customize ones: a
        // current-schema database still carries the v6 probe columns,
        // the v7 photo-album tables and the v9 fingerprint, which would
        // make the real migration chain fail on columns it never dropped.
        "ALTER TABLE tracks DROP COLUMN discnumber;"
        "ALTER TABLE tracks DROP COLUMN probe_mtime_ns;"
        "ALTER TABLE tracks DROP COLUMN probe_ctime_ns;"
        "ALTER TABLE tracks DROP COLUMN probe_version;"
        "ALTER TABLE tracks DROP COLUMN audio_fingerprint;"
        "DROP TABLE music_probe_failures;"
        "DROP TABLE photo_album_photos; DROP TABLE photo_albums;"
        "ALTER TABLE videos DROP COLUMN custom_poster;"
        "DROP TABLE collection_customizations;"
        "DROP TABLE playlist_tracks; DROP TABLE playlists;"
        "CREATE TABLE playlists(id INTEGER PRIMARY KEY,name TEXT NOT NULL);"
        "CREATE TABLE playlist_tracks(id INTEGER PRIMARY KEY,playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,position INTEGER NOT NULL);"
        "CREATE INDEX idx_plt ON playlist_tracks(playlist_id,position);";
    const QString legacyPath = scratch.filePath(QStringLiteral("legacy.db"));
    LibraryDb legacy;
    char *legacyError = nullptr;
    check(legacy.open(legacyPath) && legacy.insertVideo(seed), "migration fixture created");
    auto legacyVideo = legacy.allVideos().first();
    legacyVideo.tmdbPoster = QStringLiteral("/cache/vidthumbs/custom_1.jpg");
    legacy.updateVideoCustomization(legacyVideo);
    legacy.close();
    sqlite3 *raw = nullptr;
    bool prepared = sqlite3_open(legacyPath.toUtf8().constData(), &raw) == SQLITE_OK;
    if (prepared)
        prepared = sqlite3_exec(raw, (legacySchema + "PRAGMA user_version=2;").constData(),
                                  nullptr, nullptr, &legacyError) == SQLITE_OK;
    if (!prepared)
        printf("      legacy setup failed: %s\n",
               legacyError ? legacyError : (raw ? sqlite3_errmsg(raw) : "fixture open failed"));
    sqlite3_free(legacyError);
    legacyError = nullptr;
    if (raw) sqlite3_close(raw);
    if (prepared && !legacy.open(legacyPath))
        printf("      open failed: %s\n", qUtf8Printable(legacy.lastError()));
    check(prepared && legacy.open(legacyPath), "v2 library upgrades to separate artwork pins");
    check(!legacy.allVideos().isEmpty() && legacy.allVideos().first().customPoster
          && QFile::exists(legacyPath + QStringLiteral(".v2-backup")),
          "migration retains old custom covers and creates the upgrade backup");
    legacy.close();
    raw = nullptr;
    prepared = sqlite3_open(legacyPath.toUtf8().constData(), &raw) == SQLITE_OK;
    if (prepared)
        prepared = sqlite3_exec(raw, (legacySchema + "PRAGMA user_version=0;").constData(),
                                  nullptr, nullptr, &legacyError) == SQLITE_OK;
    if (!prepared)
        printf("      v0 reset failed: %s\n",
               legacyError ? legacyError : (raw ? sqlite3_errmsg(raw) : "fixture open failed"));
    sqlite3_free(legacyError);
    if (raw) sqlite3_close(raw);
    check(prepared && legacy.open(legacyPath) && !legacy.allVideos().isEmpty()
          && legacy.allVideos().first().customPoster,
          "historical unversioned libraries receive the artwork-pin migration too");
    printf("customize self-test: %s (%d failures)\n", failed ? "FAILED" : "ALL PASS", failed);
    return failed ? 1 : 0;
}

/* LibraryPaths and PlaylistText decide what the application is allowed to name a
 * file and what it thinks two files have in common.  Both are header-only, both
 * run at every save, and a wrong answer does not crash anything - it just makes a
 * transfer report "Fail to save", or makes an import report a library full of
 * missing songs.  Those are the failures a hand test on one platform misses, so
 * the cases are frozen here, on both sides of every #ifdef. */
static int runPathsSelfTest() {
    int failed = 0;
    const auto check = [&failed](bool ok, const char *what) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            failed++;
    };
    const auto eq = [&check](const char *what, const QString &got,
                             const QString &expect) {
        const bool ok = got == expect;
        check(ok, what);
        if (!ok)
            printf("      got '%s' expected '%s'\n", got.toUtf8().constData(),
                   expect.toUtf8().constData());
    };

    /* ---- what one filesystem component may be called ---- */
    eq("safeComponent: a colon in an album title is an ordinary character on Linux",
       LibraryPaths::safeComponent(
           QStringLiteral("The Dark Side of the Moon: Immersion"),
           QStringLiteral("fallback")),
       QStringLiteral("The Dark Side of the Moon_ Immersion"));
    eq("safeComponent: a slash is a separator on every platform",
       LibraryPaths::safeComponent(QStringLiteral("Drake/Mixtape"),
                                   QStringLiteral("fallback")),
       QStringLiteral("Drake_Mixtape"));
    eq("safeComponent: empty, . and .. all take the caller's fallback",
       LibraryPaths::safeComponent(QString(), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral("."), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral(".."), QStringLiteral("fallback")),
       QStringLiteral("fallback|fallback|fallback"));
#ifdef Q_OS_WIN
    eq("safeComponent: the whole shell-illegal set becomes underscore",
       LibraryPaths::safeComponent(QStringLiteral("a<b>c|d?e*f\"g\\h/i:j"),
                                   QStringLiteral("fallback")),
       QStringLiteral("a_b_c_d_e_f_g_h_i_j"));
    eq("safeComponent: a control character cannot survive",
       LibraryPaths::safeComponent(QStringLiteral("bad") + QChar(1)
                                       + QStringLiteral("name"),
                                   QStringLiteral("fallback")),
       QStringLiteral("bad_name"));
    eq("safeComponent: a trailing dot or space goes, Explorer hides them",
       LibraryPaths::safeComponent(QStringLiteral("Trailing.  "),
                                   QStringLiteral("fallback")),
       QStringLiteral("Trailing"));
    eq("safeComponent: NUL and nul.mp3 are both unopenable by path",
       LibraryPaths::safeComponent(QStringLiteral("NUL"), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral("nul.mp3"), QStringLiteral("fallback")),
       QStringLiteral("_NUL|_nul.mp3"));
    eq("safeComponent: CON, aux and COM1.txt take the prefix",
       LibraryPaths::safeComponent(QStringLiteral("CON"), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral("aux"), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral("COM1.txt"), QStringLiteral("fallback")),
       QStringLiteral("_CON|_aux|_COM1.txt"));
    eq("safeComponent: a stem that only starts like a device keeps its name",
       LibraryPaths::safeComponent(QStringLiteral("NULL album"), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral("CONCERT"), QStringLiteral("fallback"))
           + QLatin1Char('|')
           + LibraryPaths::safeComponent(QStringLiteral("CLOCK$"), QStringLiteral("fallback")),
       QStringLiteral("NULL album|CONCERT|_CLOCK$"));
    check(LibraryPaths::isReservedWindowsName(QStringLiteral("LPT9.log"))
              && LibraryPaths::isReservedWindowsName(QStringLiteral("PRN")),
          "isReservedWindowsName: LPTn and PRN are devices");
    check(!LibraryPaths::isReservedWindowsName(QStringLiteral("music"))
              && !LibraryPaths::isReservedWindowsName(QStringLiteral("Noodle")),
          "isReservedWindowsName: ordinary names are not devices");
#else
    eq("safeComponent: POSIX touches nothing but the slash",
       LibraryPaths::safeComponent(QStringLiteral("a<b>c|d?e*f\"g\\h/i:j"),
                                   QStringLiteral("fallback")),
       QStringLiteral("a<b>c|d?e*f\"g_h:i:j"));
    eq("safeComponent: POSIX keeps a trailing dot and space - they are legal",
       LibraryPaths::safeComponent(QStringLiteral("Trailing.  "),
                                   QStringLiteral("fallback")),
       QStringLiteral("Trailing.  "));
    eq("safeComponent: POSIX has no reserved stems, so NUL stays NUL",
       LibraryPaths::safeComponent(QStringLiteral("nul.mp3"), QStringLiteral("fallback")),
       QStringLiteral("nul.mp3"));
    check(!LibraryPaths::isReservedWindowsName(QStringLiteral("NUL")),
          "isReservedWindowsName: no device namespace on POSIX, so no name gains an underscore");
#endif

    /* ---- two spellings of one file ---- */
#ifdef Q_OS_WIN
    check(LibraryPaths::caseKey(QStringLiteral("E:/Music/A.MP3"))
              == LibraryPaths::caseKey(QStringLiteral("e:/music/a.mp3")),
          "caseKey: a Windows folder cannot hold both spellings, so they are one key");
    check(LibraryPaths::caseKey(QStringLiteral("E:/Music/A.MP3"))
              != LibraryPaths::caseKey(QStringLiteral("E:/Music/B.MP3")),
          "caseKey: folding still separates different files");
#else
    eq("caseKey: POSIX folds nothing, so every stored key stays byte-identical",
       LibraryPaths::caseKey(QStringLiteral("/Music/A.MP3")),
       QStringLiteral("/Music/A.MP3"));
    check(LibraryPaths::caseKey(QStringLiteral("/Music/A.MP3"))
              != LibraryPaths::caseKey(QStringLiteral("/Music/a.mp3")),
          "caseKey: on POSIX case is part of the filename");
#endif

    /* ---- one playlist line, resolved ---- */
    const QString base = QStringLiteral("/music/playlists");
    eq("resolveEntry: an absolute entry is left alone",
       LibraryPaths::resolveEntry(base, QStringLiteral("/music/A.mp3")),
       QStringLiteral("/music/A.mp3"));
    eq("resolveEntry: a bare name belongs to the playlist's own folder",
       LibraryPaths::resolveEntry(base, QStringLiteral("A.mp3")),
       QStringLiteral("/music/playlists/A.mp3"));
    eq("resolveEntry: a relative entry is cleaned, not left with a traversal in it",
       LibraryPaths::resolveEntry(base, QStringLiteral("sub/../A.flac")),
       QStringLiteral("/music/playlists/A.flac"));
    eq("resolveEntry: a parent escape resolves and stays resolved",
       LibraryPaths::resolveEntry(base, QStringLiteral("../A.mp3")),
       QStringLiteral("/music/A.mp3"));
#ifdef Q_OS_WIN
    eq("resolveEntry: a Windows playlist spells it with backslashes and a drive",
       LibraryPaths::resolveEntry(base, QStringLiteral("E:\\Music\\A.mp3")),
       QStringLiteral("E:/Music/A.mp3"));
    eq("resolveEntry: an iTunes file:// URL with a drive decodes",
       LibraryPaths::resolveEntry(base, QStringLiteral("file:///E:/Music/A.mp3")),
       QStringLiteral("E:/Music/A.mp3"));
#else
    eq("resolveEntry: a backslash on POSIX is an ordinary filename character",
       LibraryPaths::resolveEntry(base, QStringLiteral("E:\\Music\\A.mp3")),
       QStringLiteral("/music/playlists/E:\\Music\\A.mp3"));
#endif

    /* ---- playlist bytes -> text ---- */
    eq("decode: plain ASCII is the text it already was",
       PlaylistText::decode(QByteArrayLiteral("01 - Track.mp3\n")),
       QStringLiteral("01 - Track.mp3\n"));
    check(PlaylistText::decode(QByteArray()).isEmpty(),
          "decode: an empty file is empty text, not a missing entry");
    /* No non-ASCII literal here: MSVC reads a source file in the active code page
     * unless /utf-8 is passed, so the bytes are spelled out instead.  E5 91 A8
     * E6 B8 B8 is two CJK characters, ten bytes of valid UTF-8. */
    const QByteArray utf8Name("\xE5\x91\xA8\xE6\xB8\xB8.mp3");
    eq("decode: valid UTF-8 multibyte decodes as UTF-8",
       PlaylistText::decode(utf8Name), QString::fromUtf8(utf8Name));
    const QString afterBom
        = PlaylistText::decode(QByteArray("\xEF\xBB\xBF") + utf8Name);
    check(!afterBom.startsWith(QChar(0xFEFF)) && afterBom == QString::fromUtf8(utf8Name),
          "decode: a UTF-8 BOM is stripped instead of corrupting the first entry");
    /* C4 E3 C3 C7 is two GBK characters and is not valid UTF-8 - 0xC4 demands a
     * continuation byte and gets 0xE3.  Notepad's "ANSI" save, Windows Media
     * Player and most converters emit exactly this shape. */
    const QByteArray ansiName("\xC4\xE3\xC3\xC7.mp3");
    eq("decode: a file that is not UTF-8 falls back to the locale code page",
       PlaylistText::decode(ansiName), QString::fromLocal8Bit(ansiName));
#ifdef Q_OS_WIN
    check(!PlaylistText::decode(ansiName).contains(QChar(0xFFFD)),
          "decode: an ANSI playlist does not arrive as replacement characters");
#endif
    check(PlaylistText::decode(QByteArrayLiteral("A.mp3\r\nB.mp3\r\n"))
                  .split(QLatin1Char('\n'))
                  .at(0)
                  .trimmed()
              == QLatin1String("A.mp3"),
          "decode: a CRLF playlist still yields the path and nothing else");

    /* ---- the video import name shares the same device-name rule ---- */
    eq("safeFilePart: an ordinary device object name is untouched",
       VideoIdentity::safeFilePart(QStringLiteral("Show A - S01E01 - Pilot.wmv")),
       QStringLiteral("Show A - S01E01 - Pilot.wmv"));
#ifdef Q_OS_WIN
    eq("safeFilePart: a device object called NUL.wmv imports to a file that opens",
       VideoIdentity::safeFilePart(QStringLiteral("NUL.wmv")),
       QStringLiteral("_NUL.wmv"));
#else
    eq("safeFilePart: POSIX imports NUL.wmv under that exact name",
       VideoIdentity::safeFilePart(QStringLiteral("NUL.wmv")),
       QStringLiteral("NUL.wmv"));
#endif

    printf("paths self-test: %s (%d failures)\n",
           failed == 0 ? "ALL PASS" : "FAILED", failed);
    return failed == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#if defined(_WIN32)
    /* The console renders the active ANSI code page, so every UTF-8 path this
     * gate prints - the ones with Chinese, Hebrew or emoji in them - came out
     * as '?'.  The arguments were already fixed by reading them back through
     * Qt; this fixes what the operator can actually read. */
    zuuned_win_console_utf8();
#endif

    /* Read the arguments back through Qt rather than argv.  On Windows the CRT
     * builds narrow argv from the wide command line with the ANSI code page, so
     * QString::fromUtf8(argv[n]) decoded every non-ASCII library folder into
     * '?'-filled garbage; the scan then truthfully reported zero tracks and
     * exited 0, which reads as an empty library rather than a broken argument.
     * QCoreApplication re-reads GetCommandLineW() and survives the name. */
    const QStringList arguments = app.arguments();

    const QString mode = arguments.size() >= 2 ? arguments.at(1) : QString();

    if (mode == QLatin1String("names"))
        return runNamesSelfTest();
    if (mode == QLatin1String("customize"))
        return runCustomizeSelfTest();
    if (mode == QLatin1String("paths"))
        return runPathsSelfTest();

    const bool videoMode = mode == QLatin1String("videoscan");
    if (arguments.size() < 4 || (mode != QLatin1String("scan") && !videoMode)) {
        fprintf(stderr,
                "librarytool — library core gate\n"
                "  librarytool scan <folder> <tmpdb>\n"
                "  librarytool videoscan <folder> <tmpdb> [movies|tv|anime]\n"
                "  librarytool names\n"
                "  librarytool customize\n"
                "  librarytool paths\n");
        return 2;
    }
    const QString folder = arguments.at(2);
    const QString dbPath = arguments.at(3);
    const QString folderType = videoMode
        ? (arguments.size() >= 5 ? arguments.at(4) : QStringLiteral("tv"))
        : QStringLiteral("music");

    {
        LibraryDb db;
        if (!db.open(dbPath)) {
            fprintf(stderr, "FAIL: db open: %s\n",
                    db.lastError().toUtf8().constData());
            return 1;
        }
        db.addWatchFolder(folder, folderType);
    }

    LibraryScanner scanner;
    scanner.setDbPath(dbPath);

    QObject::connect(&scanner, &LibraryScanner::progress,
        [](const QString &stage, int cur, int total, const QString &file) {
            fprintf(stderr, "[scan] %s %d/%d  %s\n",
                    stage.toUtf8().constData(), cur, total,
                    file.toUtf8().constData());
        });
    int added = -1, updated = -1, photos = -1;
    QObject::connect(&scanner, &LibraryScanner::finished,
        [&](int a, int u, int p) { added = a; updated = u; photos = p; });
    QObject::connect(&scanner, &LibraryScanner::failed,
        [](const QString &err) {
            fprintf(stderr, "FAIL: %s\n", err.toUtf8().constData());
        });

    scanner.scan(); // synchronous on this thread

    if (added < 0)
        return 1;

    printf("scan done: %d tracks added, %d updated, %d photos\n",
           added, updated, photos);

    LibraryDb db;
    if (!db.open(dbPath))
        return 1;

    if (!videoMode) {
        const auto tracks = db.allTracks();
        printf("total tracks in db: %d\n", int(tracks.size()));
        const int n = qMin(5, int(tracks.size()));
        for (int i = 0; i < n; i++) {
            const LibTrack &t = tracks[i];
            printf("  [%lld] '%s' | artist='%s' | albumartist='%s' | album='%s'"
                   " | genre='%s' | track=%d | dur=%dms\n",
                   (long long)t.id, t.title.toUtf8().constData(),
                   t.artist.toUtf8().constData(),
                   t.albumartist.toUtf8().constData(),
                   t.album.toUtf8().constData(), t.genre.toUtf8().constData(),
                   t.trackNumber, t.durationMs);
        }
        return 0;
    }

    // ── Phase 6 gate: run the matcher synchronously on the scan output ──
    auto videos = db.allVideos();
    printf("total videos in db: %d\n", int(videos.size()));
    for (const LibVideo &v : videos)
        printf("  [%lld] %s | series='%s' S%02dE%02d cat=%s\n",
               (long long)v.id, v.filename.toUtf8().constData(),
               v.series.toUtf8().constData(), v.season, v.episode,
               v.category.toUtf8().constData());

    QVector<LibVideo> uncached;
    for (const LibVideo &v : videos)
        if (!v.tmdbCached)
            uncached.append(v);

    if (!uncached.isEmpty()) {
        printf("matching %d videos against TMDB...\n", int(uncached.size()));
        VideoMatcher matcher(dbPath);
        QObject::connect(&matcher, &VideoMatcher::updatesCommitted,
            [](int n) { fprintf(stderr, "[match] %d rows committed\n", n); });
        matcher.matchBatch(uncached, QStringLiteral("tmdb")); // synchronous here
    }

    videos = db.allVideos();
    int matched = 0, needsMatch = 0, posters = 0;
    for (const LibVideo &v : videos) {
        if (v.tmdbId > 0) matched++;
        else if (v.tmdbCached) needsMatch++;
        if (!v.tmdbPoster.isEmpty()) posters++;
        printf("  [%lld] %s\n        → tmdb='%s' (%s) S%02dE%02d ep='%s'"
               " poster=%s still=%s\n",
               (long long)v.id, v.filename.toUtf8().constData(),
               v.tmdbTitle.toUtf8().constData(), v.tmdbYear.toUtf8().constData(),
               v.season, v.episode, v.episodeTitle.toUtf8().constData(),
               v.tmdbPoster.isEmpty() ? "NO" : "yes",
               v.episodeStill.isEmpty() ? "no" : "yes");
    }
    printf("videoscan done: %d matched, %d needs-match, %d with posters\n",
           matched, needsMatch, posters);
    return 0;
}
