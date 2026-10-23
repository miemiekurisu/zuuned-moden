# Music artwork discovery after adding a folder

October 2 status: the initial multi-folder artwork fixes shipped in 0.1.1. Earlier candidate
notes below are historical. The artist/album follow-up is documented in
[MUSIC_MATCHING_PLAN.md](MUSIC_MATCHING_PLAN.md); its native candidate passes
software gates, with user acceptance and packaging pending.

Automatic album covers come from local folder images and embedded artwork.
Missing album covers fall back to music-store lookup after all known local
sources fail, behind the ``albumCoverLookup`` switch. Customize remains the
explicit match/artwork chooser. Artist portraits use the online
identity/artwork providers; MusicBrainz identity lookup
(``autoMetadataLookup``) ships off, so a portrait is fetched when something
asks for it - see the October 24, 2026 section below for both switches.

## October 24, 2026: the local resolution order, and a store asked at its own pace

Reported on Windows as "the PC albums have no covers".  Album grouping, matching and cache identity
were all correct: the application was losing or overriding a picture that was already there, and
the lookup meant to cover a genuinely artless album then rate-limited itself into silence.  Four
faults, every one of them pinned by a gate now.

**Order.**  Where an album's picture comes from, first match wins:

1. a file named ``cover``, ``folder``, ``artwork``, ``album`` or ``front`` - case-insensitive,
   ``.jpg``, ``.jpeg`` or ``.png`` - in the track's own folder;
2. that same known name one level up, when the track's folder is only a disc: matching
   ``^(?:cd|disc|disk|vinyl|lp|pt|part)[\s._-]*[0-9]+$``, so ``CD1`` and ``Disc 2`` inherit the
   album's picture while an album actually called ``Vol 2`` or ``Album 2024`` does not;
3. the picture embedded in the audio file itself;
4. last, any loadable image in the folder that goes by no recognised name.

Steps 3 and 4 used to be the other way round, so a scanned lyric sheet dropped in beside the
lossless files beat the real cover.  ``tests/device-music-import/album-art-precedence.cpp`` pins the
whole order; ``librarytool cover`` pins the rungs it can reach without a socket.

**Who owns the picture.**  Artist tiles and the player keyed art by the *track* artist, which is the
album owner only until a compilation or a featured artist turns up - and for a local picture that
key is a path an artist row does not have.  ``localArtOwners()`` in ``qml/MusicIdentity.js`` derives
the owner per album, and ``qml/ArtistCard.qml``, ``qml/ArtistDetail.qml``, ``qml/MusicPage.qml`` and
``qml/Main.qml`` ask for the owner's key with the album's own cover as the fallback.

**A cache write that Windows would not perform.**  Publishing staged into a live
``QTemporaryFile`` and then removed and re-copied the destination.  Windows will not delete a name
its own living object still holds, so the verbatim-JPEG path returned false and a folder or
device-sourced cover never reached the cache - on Windows alone, because POSIX unlinks open files.
It streams into the destination now.  See `WINDOWS_COMPAT_AUDIT.md`, twelfth sweep.

**Two switches instead of one.**  ``albumCoverLookup`` is the album-cover lookup and ships **on**;
``autoMetadataLookup`` is the MusicBrainz identity lookup and ships **off**, artist portraits
included.  A file that carries its own art still causes no request of any kind, and right-click
remains how you ask about one album, artist or track.  With the album switch on and every local
source having failed, the store ladder walks ``us, jp, tw, hk, gb, de``, accepts only an exact
normalised artist+album match - earliest release date, then smallest id - and keeps only an
``https`` URL on ``*.mzstatic.com``, widened to ``600x600`` in the URL itself.

**Pacing, because a library is a burst.**  Every artless album used to fire its whole ladder at an
endpoint that had never been throttled, and a catalogue that *errored* was walked through rather
than believed, so a real library drew ``HTTP 429``, spent all six rungs per album, and recorded each
error as an album with no cover.  One store search is in flight process-wide, at least two seconds
apart; a 429 or 503 pushes the next slot out by ``Retry-After`` or by four times the interval; a
wait over thirty seconds is answered locally with a synthesized 503 so a rate limit is never stored
as an absent cover; and the first catalogue that errors ends the ladder, with the album parked for
five minutes.

**Measured.**  On a copy of a real 13-album library, run as its own profile against the built
application: 42 store searches in 87.6 s, minimum gap 1.7 s, no 429 and no 503, every album landing
a terminal verdict, and ``albums=13 artWith=4 artAgree=13 artOwnerWanted=4 artOwnerFound=4``.  Nine
of those thirteen albums have no picture anywhere - they are the fixtures the scan gates generate -
and they display blank, which is the correct answer and the one a "found it!" screenshot cannot
show.  For an album a store does carry, the download is proven end to end: 33,762 bytes published
into an isolated profile's art cache.

## October 1, 2026 correction

Previously, a miss on one representative track suppressed the entire album
for the session. Adding another folder or requesting another track could not
recover its cover. Concurrent requests for another source were also dropped.

LibraryService now indexes all track paths for each existing album cache key.
An on-demand cover request queues these alternatives. AlbumArtService serializes
extraction per album, remembers misses per source path, and stops once a cover
exists. Coordination belongs to the service instance and survives its workers
safely. Existing cache identities and no-overwrite behavior are unchanged.
This does not change album grouping or edition identity. Same-path artwork
changes after a miss still require explicit reset or a fresh application session.

Artist lookup failures previously expired after five minutes but did not
schedule another attempt. A failed request now receives one automatic retry
after five minutes, preserving request generations, customization pauses and
existing portraits. Persistent absence does not cause an endless retry loop.
Artist cards also request images when their model row changes; album art reacts
to artist and local/device source changes.

## Verification

- `bash tests/device-music-import/run.sh`: existing imported-cover protection,
  a miss in the first folder followed by a cover in the second, repeated-miss
  suppression, and a second source arriving while extraction is in flight.
- `bash tests/music-identity/run.sh`: provider/identity and custom-art safeguards,
  automatic recovery without another UI request, bounded retries and cancellation
  after identity invalidation. Provider responses and retry timing are injected;
  these tests do not depend on live services.
- `cmake --build build -j 4`: complete native application and QML compilation.

Live logs also showed a MusicBrainz HTTP 503 and failed music-file probes.
Retrying artwork cannot recover unreadable media or guarantee provider coverage.
The user's second-folder collection still needs an interactive retest. The
published AppImage predates this correction.

## Independent automatic artwork discovery — implemented on feature branch

The fresh onboarding profile `/tmp/zuuned-fresh-setup-3p441zy1` started with
empty data, settings and artwork caches. Its session log confirms that the
populating album covers were extracted from music files or loaded from image
files beside the music on the Kubeplex SMB share. This proves their immediate
source, not which application originally created those sidecar files.

The feature branch populates missing artwork without depending on a Jellyfin server:

1. Preserve explicit custom artwork and existing valid covers. Try recognized
   local cover files and embedded images before any remote album request.
2. Use the existing native MusicBrainz identity client and Cover Art Archive /
   Fanart clients for missing album covers. Saved provider IDs take precedence;
   name-only matches must agree on artist and full album title and reject
   ambiguity. Never strip deluxe, explicit, remaster or edition qualifiers to
   force a match. Release-group identification alone does not prove an edition.
3. Retain the existing MusicBrainz/Fanart artist portrait pipeline and its safe
   identity-linked fallback. Queue missing portraits when library content is
   added, with bounded background work rather than relying solely on visible
   gallery delegates. Both initial setup and later folder additions use this.
4. Downloaded images use Zuuned's existing album cache and unchanged keys.
   Log local extraction and online success/unavailability separately. Settings →
   Library → Music artwork → Find missing artwork rechecks uncached local
   sources and retries remote failures without deleting good images or changing
   source tags/shared-folder files. Full persistent provenance/status UI remains
   future work.
5. Online album requests are serialized. Provider transport retains its global
   pacing, caches and shutdown cancellation. Failed album requests have a
   five-minute cooldown; explicit retry bypasses this queue cooldown, while
   provider-level cooldowns still apply. No automatic album retry loop.
6. Local source changes invalidate pending downloads. Customize pauses online
   album publication and invalidates affected albums on successful saves. Only
   current UI-thread completions publish, atomically and without overwriting
   existing artwork. Album matching changes no metadata, grouping or device
   duplicate policy.

Validation: native build; 72 provider/identity/queue checks; 17 local/device
import checks, including a later embedded cover in an atomic local-source batch.
Live native lookup for Disturbed / The Sickness downloaded a valid 500×500 cover
(54,725 bytes). Fresh setup and adding another folder both schedule the same
background pass; interactive acceptance remains the user's test gate.
The AppImage release is unchanged; this is a native testing build.

Limitations: absent or ambiguous matches stay blank for Customize. Edition
qualifiers are retained, so provider titles lacking those qualifiers may not
match automatically. Existing valid cached artwork is kept, including previously
downloaded covers; later source-file changes do not silently replace it.

Reference behavior: Jellyfin accepts folder and embedded music images
(https://jellyfin.org/docs/general/server/media/music/) and offers external image
providers, including its Cover Art Archive plugin
(https://github.com/jellyfin/jellyfin-plugin-coverartarchive). No Jellyfin API or
cache integration is required for this plan.

Candidate rehearsal: the isolated profile was relaunched with
`build/testing/automatic-music-artwork/zuuned` (PID 516564, mapped on workspace 1).
All 81 pre-existing cached cover files matched their recorded SHA-256 hashes
after startup. Customize backend integration passed; provider shutdown passed
6 checks. This preserves the original regular profile and does not publish
or rebuild the AppImage release.

## Artist ambiguity correction

Live cache inspection showed six exact-name Disturbed artists, four Adele
matches on the first page, and a truncated Korn search (25 of 57 results).
Name-only ambiguity guards therefore left those portraits blank. Artist
requests now include an ephemeral list of the owner's local album titles.
For ambiguous/truncated artist results, up to three full album-title searches
can corroborate one candidate MBID through exact album-artist credits.
Conflicting credits, incomplete album responses and absence of evidence still
require an explicit choice. Saved provider identities remain authoritative;
corroborated MBIDs cannot fall back to an unrelated same-name Deezer artist.
This changes no stored tags, album grouping or user-selected identities.
Provider regression gate: 76 checks passed, including album corroboration,
conflict and incomplete-response rejection. Native application build passed.
