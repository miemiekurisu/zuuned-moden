#pragma once

#include <QString>
#include <QStringList>

// Pure string helpers for the video library — ports of the mac
// LibraryService's filename/folder parsing statics. All functions are
// thread-safe (compiled regexes are function-local statics; the title
// memo is mutex-guarded).
//
// FOLDER-TRUTH doctrine: the user's directory names ARE the series
// identity. Parsing climbs season/episode container folders to the show
// folder and cleans scene junk from its name; TMDB later decorates but
// never renames.
namespace VideoNaming {

struct ParsedIdentity {
    QString category, series;
    int season = 0, episode = 0;
};
// Shared by scanning and Customize's reset-to-auto action.
ParsedIdentity parseIdentity(const QString &filepath, const QString &folderType);

// Canonical form for grouping + TMDB queries: strip scene/tracker
// prefixes ("www.site.xxx - ", "[Group] ", "(Group) "), dots/underscores
// → spaces, collapse whitespace. Idempotent.
QString normalizeSeriesName(const QString &raw);

// Truncate a SHOW folder's name at the first scene-junk token:
// "Adventure Time S01 1080p HMAX WEBRip …" → "Adventure Time".
QString cleanFolderSeriesName(const QString &raw);

// Climb past season-/episode-container folders (max 2 levels) to the
// show folder, then clean its name. "" when underivable.
QString deriveSeriesFromPath(const QString &filepath);

// Series name (usually folder-derived) → TMDB search query.
QString cleanSeriesNameForSearch(const QString &name);

// Filename → TMDB search query: drop extension, SxxEyy, bracket junk,
// release tags; preserve parenthesized year.
QString cleanFilenameForSearch(const QString &filename);

// Memoized cleanFilenameForSearch — call from per-render paths
// (displayTitle, on-device key sets).
QString cachedCleanTitle(const QString &filename);

// Squashed comparison form: lowercase alphanumerics only.
// "Bob's Burgers" == "bobsburgers".
QString squash(const QString &s);

// Lowercased alphanumeric tokens, "&"→"and", stopwords dropped.
QStringList tokenize(const QString &s);

// Candidate scoring (VideoMatcher.scoreCandidate): token overlap +
// squashed equality/containment rescue + year bonus + capped
// log-popularity tiebreak.
double scoreCandidate(const QString &query, const QString &year,
                      const QString &candidateTitle,
                      const QString &candidateYear, double popularity);

// Minimum acceptable score — below it the row goes to Needs Match.
inline constexpr double kAcceptThreshold = 0.55;

// Movie queries in falling order of trust: folder "Title (Year)",
// cleaned filename, cleaned filename truncated at its first year token.
// Deduped, order-preserving.
QStringList movieQueries(const QString &filepath, const QString &filename);

// True when the directory name is Plex/Jellyfin bonus-content
// convention (extras/featurettes/…).
bool isSkippedDir(const QString &dirName);

// Why a video file sitting in a watch folder is not going to be indexed.
// `bonusName` is the Plex/Jellyfin naming convention (sample.mp4, trailer.mp4,
// and their stems); `sizeFloor` is the per-folder-type minimum size (50MB only
// where the folder declares itself movies, 5MB for every other type including
// "all"; minSizeOverrideMB > 0 wins).  The reason is part of the answer because
// a refusal the scan keeps to itself is indistinguishable from a folder that
// holds nothing, and the two have different fixes.
enum class VideoSkip { none, bonusName, sizeFloor };

VideoSkip videoSkipReason(const QString &filename, qint64 filesize,
                          const QString &folderType, int minSizeOverrideMB,
                          bool appDownload);

// Yes/no form, for callers with no use for the reason.
//
// `appDownload` exempts a file the app itself wrote into the folder that
// receives its downloads.  The floor is a guess that sub-50MB video inside a
// movie collection is trailer debris; a pull the user asked for is not debris,
// and a download that lands on disk and then never appears on any page reads
// exactly like a download that failed.  The name rules are NOT exempt: a file
// called trailer.mp4 is a file called trailer.mp4 whoever copied it in.
bool isSkippedVideoFile(const QString &filename, qint64 filesize,
                        const QString &folderType, int minSizeOverrideMB,
                        bool appDownload);

} // namespace VideoNaming
