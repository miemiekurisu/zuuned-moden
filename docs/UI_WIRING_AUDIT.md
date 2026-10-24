# Which feature screens are actually wired

Dated 2026-10-24.  The screens borrowed from the original Zune PC software have grown
for many sessions, and nobody had ever asked the blunt question: of the surfaces that
exist, which ones reach a backend, and which ones are furniture.  This page answers
that per screen, and names every backend entry point that no screen can reach.

It is a wiring audit, not a status page.  It adds no hardware claim and repeats none:
[WINDOWS_PORT.md](WINDOWS_PORT.md) stays the only page that states what has been done
to a player from a Windows machine, and
[ZUNE_CLIENT_FEATURES.md](ZUNE_CLIENT_FEATURES.md) stays the product gap analysis.
What this page adds is the third axis - is there a control at all.

## Method, and why a naive grep gets it wrong in both directions

The nine QML singletons (`rg QML_SINGLETON src` finds exactly these: `DeviceService`,
`LibraryService`, `PlayerService`, `VideoPlayerService`, `SyncEngine`,
`SpokenMediaService`, `AppSettings`, `DiagnosticsService`, `FrameThumbnailService`,
plus the `ZPathText` helper) were parsed for every `Q_PROPERTY`, `Q_INVOKABLE`,
`ZUNE_SETTING` and signal, then matched against all 96 files under `qml/`.  A first
pass reported 59 unreached members and was wrong about twelve of them.  The reasons
matter more than the number, because they cut both ways:

- **A dot may sit on the next line.**  `qml/SettingsPage.qml:1390` reads
  `return LibraryService` / `.pendingPlaylistAddCount(modelData.id)`.  Any pattern
  demanding `LibraryService.` on one line calls that member dead while the Settings
  page calls it on every playlist row.
- **A service arrives under another name.**  `qml/CustomizeSheet.qml:12` is
  `property var service: LibraryService` and the sheet's Apply button calls
  `service.applyCustomization(...)`.  The largest identity-and-artwork editor in the
  app contains no literal `LibraryService.` token at all.
- **A control is deliberately player-agnostic.**  `VinylRecordView`,
  `SpinningDiscView`, `MiniPlayerWindow`, `QueueRibbonBar`, `VolumeSlim` and
  `SpokenPlaybackControls` declare `property var player` and call
  `player.scrubMs(...)`, `player.beginScrub()`, `player.setSleepMinutes(...)`, guarded
  by `!== undefined` where the two players differ.  Eight of those sites bind
  `player: PlayerService` at eleven sites, two bind `player: VideoPlayerService`
  (`qml/VideoPlayerView.qml:80` and `:279`), so one widget paints both transports and
  each player's scrubbing surface is only reachable through a duck-typed property.
  The first pass was also too generous in one place, and only a read of the call site
  catches it: it counted `PlayerService.stop` as reachable, while the two bare
  `stop()` calls in `qml/Main.qml:313` and `:477` sit inside a `Timer.onTriggered` and
  stop the timer.
- **One page answers for two services.**  `qml/MusicPage.qml:22` is
  `property var source: DeviceService`; the local library mounts it as
  `MusicPage { source: LibraryService }` (`qml/LocalLibraryPage.qml:18`).  The artist
  and genre rows read `source.artistsList` / `source.genresList`, so
  `LibraryService.artistsList` and `.genresList` are live and no static reader of
  either service alone would say so.

The same four forms also flatter an audit.  A member referenced only from a component
that is never mounted counts as wired.  That is not hypothetical here:
`LibraryService.tmdbSearch` and `.assignTmdbResult` are called only from
`qml/TmdbLookupSheet.qml`, which nothing instantiates.

Tests do not settle it either.  `tests/actions/**` and eight sibling trees drive real
UI components against **stub** QML services (`tests/actions/photo-stubs/LibraryService.qml`
and friends), which record calls rather than perform them - so those suites prove a
control fires *something*, and prove nothing about the C++ behind it.

## The count

| service | exposed to QML | reachable from a screen | no screen route |
| --- | --- | --- | --- |
| SyncEngine | 24 | 24 | 0 |
| FrameThumbnailService | 2 | 2 | 0 |
| SpokenMediaService | 36 | 35 | 1 |
| DeviceService | 82 | 72 | 10 |
| LibraryService | 104 | 80 | 24 |
| PlayerService | 33 | 29 | 4 |
| VideoPlayerService | 34 | 30 | 4 |
| AppSettings | 31 | 26 | 5 |
| DiagnosticsService | 8 | 7 | 1 |
| **total** | **354** | **305** | **49** |

Of the 49, two (`artistsList`, `genresList`) are false positives for the reason above.
The remaining 47 are all real, and 44 of them are not missing features:

| why there is no route | count | reading |
| --- | --- | --- |
| driven from C++ instead; the QML exposure is a leftover | 11 | harmless, misleading |
| superseded by a newer entry point that the UI does use | 24 | dead code, feature live |
| no affordance exists anywhere | 3 | real gap |
| no caller in QML, C++ or tests at all | 9 | dead code, nothing behind it |

## Screens and what each one actually drives

Routing is flat: `qml/Main.qml:1046-1100` stacks every page as a sibling and switches
them on `window.currentPage`.  A `DevicePanel` browse menu (`qml/DevicePanel.qml:798-803`,
and the compact glyph rail at `:1633`) owns the six `device*` values; the sidebar
(`qml/SidebarView.qml:146-169`) owns the library ones.  `qml/DeviceListPage.qml` is not
part of this: see the dead files below.

| route | component | what it calls |
| --- | --- | --- |
| `music` | `LocalLibraryPage` -> `MusicPage { source: LibraryService }` | LibraryService 19 members, DeviceService 9, PlayerService 4, SyncEngine 2 |
| `videos` | `LibraryVideosView` | LibraryService 5, FrameThumbnailService 2, VideoPlayerService via `setQueue` only, SyncEngine 1 |
| `photos` | `LibraryPhotosView` | LibraryService 8 (album draft API), DeviceService 2, SyncEngine 1 |
| `podcasts` / `audiobooks` | `SpokenMediaPage` (one component, `books:` toggles) | SpokenMediaService 20, DeviceService 2, SyncEngine 1 |
| `settings` | `SettingsPage`, tabs Appearance / Library / Device / Mixtapes / Health | LibraryService 32, AppSettings 17, DeviceService 11, DiagnosticsService 7, SpokenMediaService 1, SyncEngine 1 |
| `deviceMusic` | `MusicPage` (source stays `DeviceService`) | as above, device branch |
| `deviceVideos` | `DeviceVideosPage` | DeviceService 9, SyncEngine 1, `VideoTitleRepair` element |
| `devicePictures` | `DevicePicturesPage` | DeviceService 8 |
| `devicePodcasts` / `deviceAudiobooks` | `DeviceSpokenPage` x2 | DeviceService 1 - inventory, covers, selection and confirmed removal all ride the device service |
| `devicePlaylists` | `PlaylistsPage` | DeviceService 7, LibraryService 1 |
| panel | `DevicePanel` | DeviceService 32, SyncEngine 18 |
| overlay | `NowPlayingIsland`, `QueueRibbonBar`, `MiniPlayerWindow` | the duck-typed player surface |

Every one of those routes is reachable by a click, and none of them is a stub: the
only page in the router that renders a canned "later phase" message is
`LocalLibraryPage`'s non-music branch, which the
router never selects - `videos` and `photos` go to their own views - so even that
empty state is unreachable rather than dishonest (`qml/LocalLibraryPage.qml:31-52`).

The heavier editors are wired through the alias form and were worth confirming
individually, because "no `LibraryService.` token" would otherwise read as unwired:

- `qml/CustomizeSheet.qml` - `customizeContext`, `searchCustomizeIdentity`,
  `searchCustomizeMusicIdentity`, `requestCustomizeArt`, `applyCustomization`,
  `retryArtwork`, `albumArtworkStatus`/`artistArtworkStatus`, `artKey`.
- `qml/VideoTitleRepairSheet.qml` - a `VideoTitleRepair` element that takes
  `library`/`device`/`sync` and applies through `renameItem` on the worker, not
  through the service invokables of the same name.
- `qml/EditTrackSheet.qml`, `qml/QuickPicker.qml`, `qml/PhotoGalleryView.qml`,
  `qml/OrbitalControls.qml`, `qml/PodcastPreferences.qml` - signal-only by design;
  their parents make the calls.  `OrbitalControls` emits seven intents and
  `qml/VideoPlayerView.qml:330` handles all seven.

## Three real gaps

1. **The music player has no Stop.**  `PlayerService.stop()` is called from C++ in
   many places and from QML in none.  Every other `.stop()` in `qml/` is accounted
   for: eight sit on `Timer`s (`qml/Main.qml:579`, `qml/ToastHost.qml:37`,
   `qml/MiniPlayerWindow.qml:39`) and one is the video player's own stop
   (`qml/VideoPlayerView.qml:74`).  The video overlay has a
   stop glyph (`qml/OrbitalControls.qml:124`), so the asymmetry is a decision someone
   made in the ribbon and never revisited.  Pause-with-seek-to-zero is what a user
   gets instead.
2. **The video overlay cannot move to the previous or next title.**
   `VideoPlayerService.next()` and `.prev()` have no caller in QML.  The overlay has
   chapter skips and a queue with `jumpTo(index)`, so a queue built from a series can
   be stepped through by hand, but there is no transport button for it.
3. **There is no manual TMDB lookup any more, and the backend still thinks there is
   one.**  `LibraryService.tmdbSearch`, `.assignTmdbResult` and the
   `tmdbSearchResults` signal exist only to serve `qml/TmdbLookupSheet.qml`, which is
   never mounted.  Choosing a match by hand still works for videos and music, through
   the Customize sheet's match tab - a different code path - so this is a stale API
   plus a stale screen, not a lost capability.

## Dead files and dead entry points

Two whole files are unreachable.  Both are still listed in `CMakeLists.txt:399` and
`:420`, so they are shipped in the resource bundle and a reader meets them as though
they were live:

- `qml/DeviceListPage.qml` (137 lines) - no reference anywhere in `qml/`, `src/` or the
  gates.  It is the remains of the design that Main.qml:1046-1100 replaced by putting
  the device pages flat in the router.  Checked rather than assumed: across the whole
  tree the only remaining mention is the two `CMakeLists.txt` resource lines above, and
  no document names it as a live screen, so nothing but the build keeps it in evidence.
- `qml/TmdbLookupSheet.qml` (247 lines) - see gap 3.

One inline component is dead for the same reason: `component PlaceholderPage: Item`
(`qml/Main.qml:1532`), whose body renders `label + " - coming soon"`.  Nothing
instantiates it, so the app has never shown a coming-soon panel, which is the honest
outcome and worth keeping in mind before anyone adds one.

Twenty-four invokables are a superseded generation of an API the UI uses under a newer
name.  They are the most expensive kind of dead code because they read as a missing
feature to whoever greps next:

| dead entry point | what the UI calls instead |
| --- | --- |
| `createPhotoAlbum`, `renamePhotoAlbum`, `addPhotosToAlbum`, `removePhotosFromAlbum`, `setPhotoAlbumOrder` | `savePhotoAlbumDraft` with `photoAlbumSnapshot`, `deletePhotoAlbum`, `movePhotoAlbum`, `customAlbumPhotos` |
| `renamePlaylist`, `setPlaylistTracks` | `editPlaylistEntries` |
| `setManualAlbumIdentity`, `setManualArtistIdentity`, `setManualVideoIdentity`, `setAlbumCustomArt`, `setArtistCustomArt`, `setVideoCustomArt` | `applyCustomization` (one draft, all fields) |
| `fetchArtCandidates`, `pickAlbumArtUrl`, `pickArtistArtUrl`, `pickVideoArtUrl` | `requestCustomizeArt` + the `ArtworkRequest` element |
| `fetchFrameGrabs` | `FrameThumbnailService` |
| `updateVideoSeries`, `importPlaylistTrack` | `VideoTitleRepair` element; `importPlaylistM3U` / `importDevicePlaylist` |
| `DeviceService.createPlaylist` | `SyncEngine` forges the device playlist during the sync playlist phase (`src/sync/SyncEngine.cpp:2481`), so playlists do reach the player |
| `PlayerService.clearQueue` | `SyncEngine.clearQueue`, which is what `qml/Main.qml:509` and the eject path call |
| `VideoPlayerService.play`, `.seek` | `setQueue(items, startIndex, ...)`, and `seekMs`/`seekRelative` |

Eleven more are reached from C++ only, where the `Q_INVOKABLE`/`Q_PROPERTY` is a
vestige of an earlier UI: `DeviceService.refreshStorage` and `.sever` (the finalize
chain, `src/sync/SyncEngine.cpp:2606`), `.podcastStatusWarning` (`src/main.cpp:324`),
`LibraryService.importRoot`/`.ensureImportRoot` (`src/main.cpp:348`),
`SpokenMediaService.podcastCleanupCandidates` (`src/main.cpp:300` - the cleanup plan is
`SyncEngine`-owned, not button-owned), `DiagnosticsService.logDirectory` (consumed by
its own `openLogs`), and `AppSettings.tmdbApiKey`, `.fanarttvApiKey`,
`.videoMinSizeMB` - the last three are ini-only by design and already recorded as
having no control.  `PlayerService.setVolume` belongs here with a caveat worth stating
plainly: its only caller is `src/player/MprisService.cpp:49`, which is the Linux
D-Bus path, so on Windows the volume surface is the `volume` write property and
nothing on this platform ever calls the invokable.

Nine have no caller at all, in QML, C++ or the gates: `AppSettings.miniPlayerFrame`,
`AppSettings.videoControlsStyle`, `DeviceService.albumCount`, `.artistCount`,
`.importsDir`, `.photoImportsDir`, `.onDeviceKeys`, `.pullNoun`, and
`PlayerService.playFile`.  `importsDir`/`photoImportsDir` are the sharpest of these:
both are named in comments as the destination of a device pull
(`qml/DeviceVideosPage.qml:120`, `qml/DevicePicturesPage.qml:101`) while the code
resolves the destination elsewhere, so a reader will look for a caller that does not
exist.  `PlayerService.playFile` is the single-file open entry point; nothing opens a
file that way, music always goes through the queue.

## What this audit does not establish

Nothing here was clicked.  A member being reachable proves a handler exists, not that
the handler does the right thing to a player - the evidence for behaviour stays where
it was, on the gates and on the one hand-tested Pavo Zune HD in
[WINDOWS_PORT.md](WINDOWS_PORT.md).  Conversely, an unreachable member says nothing
about the feature: 44 of 47 have a live sibling.  And the audit is a snapshot of
static references - it cannot see a signal connected from C++ to QML by string, of
which there is one precedent in the tree (`src/VideoTitleRepair.cpp:347` calls
`renameItem` by name), so anything claimed unreachable here was confirmed by reading
the call site, not by the count.

## Labels (2026-10-25)

Every label reachable from the navigation rail, the context menus and the five
settings tabs goes through the catalogue, so the Chinese build is Chinese from
edge to edge rather than at the headings.  The 29 labels that needed a
context-qualified tsv line are all pivot bars - `Albums` is 专辑 on
`qml/MusicPage.qml` and 相册 on `qml/LibraryPhotosView.qml` and
`qml/DevicePicturesPage.qml` - and the `tabs:` token lists themselves stay English
identities, translated only where they are painted, so `pivot === "Albums"`,
`picker.mode = current.toLowerCase()` and `page.filter === "all"` keep comparing
what they always compared.  Rules and the lupdate lookup are in
[I18N.md](I18N.md).
