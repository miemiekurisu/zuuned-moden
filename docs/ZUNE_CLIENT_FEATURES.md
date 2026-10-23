# The original Zune PC software, and what this app does instead

Purpose of this page: answer "what did the Zune software on Windows do that Zuuned
Modern does not do?" It is a product gap analysis, not a status page. Nothing here
adds or changes a hardware claim; [WINDOWS_PORT.md](WINDOWS_PORT.md) is the only page
that states what has been done to a player from a Windows machine, and
[FEATURE_COMPLETION.md](FEATURE_COMPLETION.md) is the implementation backlog.

Read it in this order: what the original software was, then the capability
comparison, then what cannot come back, then what is still worth doing here.

## What the original software was

The Zune software (also called the Zune client) was Microsoft's desktop companion for
the player line. First release 2006-11-14, final release 4.08.2345 on 2011-08-22.
It ran from Windows XP SP3 up to Windows 10, and it was built on Microsoft Iris (the
Metro-era UI framework), not on a toolkit this tree shares anything with.

It was really five products in one process:

- a desktop media player and local library over files on the PC,
- the sync and management tool for Zune 4/8/16/30, the 80 and 120 GB players, Zune HD,
  and later Windows Phone 7 and Kin devices,
- a media streaming server that shared the library to other PCs and to Xbox 360,
- the storefront client for Zune Marketplace, including the Zune Music Pass subscription,
- the Zune social client, including the files users swapped with each other.

Two of those five are gone in a way no client can repair. Microsoft replaced the
software in October 2012 (Xbox Music, then Movies and TV, plus a separate phone sync
app) and shut the online services down on 2015-11-15. Marketplace, Music Pass, social
presence, swaps, and the metadata and artwork metaservices that the library pulled from
`toc.music.metaservices.microsoft.com` and `images.metaservices.microsoft.com` all went
with them. The tag-enrichment half of what the original client did automatically was a
Microsoft-hosted database, not a property of the protocol, so "the old app filled in
album art by itself and this one does not" is a difference between two providers, not
between two protocol stacks.

Two cells below describe the original client from memory rather than from a source
re-read while writing this page, and say so: they are pointers for someone who wants
to check, not findings.

Media formats the original client handled, as the reference for the transfer table
below: audio MP3, AAC-LC in .mp4/.m4b/.mov, and WMA in its standard, professional and
lossless flavours; video MPEG-4/H.264 in mp4/m4v/mov plus WMV, ASF and AVI; images JPEG.

## Capability comparison

The third column answers one question only: has this line been done on a Windows
machine against a real player? It borrows the vocabulary `WINDOWS_PORT.md` argues in,
and every cell names the narrower thing that was actually measured.

- `read` - bytes came off the player to this machine and were looked at.
- `send` - the player accepted the object and re-listed it; no byte came back.
- `no` - implemented and software-gated, never done from Windows against a player.
- `n/a` - not a device question.
- `yes` - done from Windows against a player, and it is neither a media read nor a
  media write.

### Playing and holding a library

| Capability | Zune software | Zuuned Modern | Windows vs a player |
| --- | --- | --- | --- |
| Desktop player | Yes | Yes - mpv, queue, now-playing island, compact island below the headroom breakpoint, speed, sleep timer, bookmarks | n/a |
| Library over local files | Yes | Yes - watch folders plus scanner, audio-fingerprint duplicate detection, verified local copies | no |
| Album, artist, genre, track views | Yes | Yes - gallery grids, opt-in A-Z breaks, album and artist detail pages | n/a |
| Video library | Yes | Yes - TMDB matched titles, show/episode structure, posters and backdrops | n/a |
| Photo library | Yes, JPEG | Yes - folders plus custom nested albums, ordered membership | no |
| Podcasts | Yes | Yes - RSS preview, subscribe, download, retention and cleanup, opt-in auto-queue on connect, played and played-on-Zune state | no |
| Audiobooks | not verified here | Yes - ordered imports, chapters, custom covers, resume and bookmarks | no |
| Battery and free space on the player | Yes | Yes - battery from MTP property `0x5001` at connect, capacity and free, attributed per category | read |
| CD ripping | Yes | No | n/a |
| Ratings, hearts, play and skip counts | Yes, and they drove auto playlists | No - `LibTrack` in `src/library/LibraryTypes.h` has no rating or play-count field at all, only `lastPositionMs` | n/a |

### Getting media into the library

| Capability | Zune software | Zuuned Modern | Windows vs a player |
| --- | --- | --- | --- |
| From folders on the PC | Yes | Yes, and a folder is the unit - there is no per-file add | no |
| From the player, into the library | Yes | Yes - `Save to Library` for music, photos and video; a pull keeps the device name and judges the payload | read |
| From the store | Yes | No, and it cannot (see below) | n/a |
| From a disc | Yes | No | n/a |
| From another user | Yes, swaps | No, and it cannot (see below) | n/a |
| Playlists as text | Import and export | Import of M3U, including the code-page and BOM rules; export keeps its own naming rule | n/a |

### Metadata and artwork

| Capability | Zune software | Zuuned Modern | Windows vs a player |
| --- | --- | --- | --- |
| Album art | Fetched automatically from Microsoft metaservices | Local-first: embedded picture, then folder image. A provider is reached only when the user asks | read |
| Artist imagery | Same hosted service | Deezer and Fanart.tv, per item, on request | no |
| Metadata and tag enrichment | Automatic | `AppSettings.autoMetadataLookup`, an ini key, defaults to false. When it is off a library read logs how much it resolved locally and that nothing was queried | n/a |
| Asking for one item | Automatic, no per-item control | Right-click: one album, one artist, one track row. Settings to Library carries a whole-library button for the times you want that | n/a |
| Tag editing | Yes | Yes - Customize matches a track against a release identity and writes tags back | n/a |
| Artwork rendering style | Fixed | Artwork style plus clean, halftone and worn parameters | n/a |

The order matters and was a deliberate decision: on a library whose files carry no
embedded picture and no folder image, tiles stay empty and the artist grid says
`Needs a match` until someone asks. The network is something a user does to one item,
not something that happens to a library. The measured reason is the eighteenth pass of
`WINDOWS_PORT.md`, where an artist portrait with no local source at all turned fourteen
songs into six provider requests and a `HTTP 503`.

### Organizing

| Capability | Zune software | Zuuned Modern | Windows vs a player |
| --- | --- | --- | --- |
| Manual playlists | Yes | Yes - create, rename, delete, reorder, Add to Playlist with dedup, library-first so a playlist survives having no player attached | n/a |
| Device playlists | Yes | Yes - forge a playlist on the player, and import one off it | read |
| Automatic playlists from rules | Yes - top rated, most played and similar built themselves | No - there is no rule engine, and the ratings and play counts it would rank on do not exist in the schema yet | n/a |
| Favourites and starred items | Loved flag | Podcast and audiobook favourites, independent per show, episode and book | n/a |
| Virtual grouping | Genre and mood views | Podcast series, audiobook Libraries and starred collections, many-to-many and never folders | n/a |
| Mood and genre artwork | not verified here | Genre and artist identity moments with the artwork style pipeline | n/a |

### Managing the player

| Capability | Zune software | Zuuned Modern | Windows vs a player |
| --- | --- | --- | --- |
| Detect it plugged in | Yes | Yes - libusb, and on Windows `cfgmgr32` as well, because a driver-less Zune is at PnP problem 28 and libusb never sees it | yes |
| Tell the user how to fix the driver | n/a on its own platform | Yes - the fix-it card and Settings to Device name WinUSB and print the exact device id to pick in Zadig | read |
| Authenticate | Yes | Yes - MTPZ, with credentials supplied at build time | yes |
| Browse what is on it | Yes | Yes - music, video, photos, playlists, podcasts and audiobooks, each lazily and serialized | read |
| Re-index after a write | Yes | Yes - finalize plus a bounded reconnect ladder, because WinUSB keeps the interface and Windows sends no hotplug arrival | read |
| Rename the player | Yes | Yes - `renameDevice` through MTP property `0xD402` | no |
| Rename an object | Yes | Yes - `renameItem` | no |
| Create a playlist on it | Yes | Yes - `createPlaylist` | no |
| Delete from it | Yes | Yes - `purgeItems` and `purgeSpokenItems`, user-confirmed, live selection revalidated first | no |
| Eject safely | Yes | Yes - `eject` | no |
| Duplicate detection before sending | Yes | Yes - shared identity with disc/track distinction and an explicit ambiguous answer | no |
| Storage accounting per category | Yes | Yes, with honest counts where per-media bytes are unknown on classic players | read |
| Sync on connect, driven by settings | Yes | No equivalent setting on the music side; the queue is explicit and only the reconnect ladder is automatic | no |
| Device clock write | n/a | No - the only device-property write in this tree is the `0xD402` name | n/a |
| Battery charge state shown while connected | Yes | Yes, as a connect-time snapshot | read |

### Moving media

| Capability | Zune software | Zuuned Modern | Windows vs a player |
| --- | --- | --- | --- |
| Music to the player | Yes | Yes | read |
| Music off the player | Yes | Yes, into `Music\Zuuned Imports`, indexed and with its embedded art read out | read |
| Convert for the target model | Yes, quality presets | Yes - video profile auto from the connected model, Zune 30 WMV and 480x272 profiles, mp3 through LAME, `transcodeAccel` auto with NVDEC on this machine | send |
| Photos to the player | Yes | Yes | send |
| Photos off the player | Yes | Implemented; the thumbnail fallback path has never returned a picture from a player | no |
| Video to the player | Yes | Yes, through the real transcoder | send |
| Video off the player | Yes | Yes - `saveVideoToLibrary` | no |
| Podcasts and audiobooks to the player | Yes | Yes in source, with firmware listening state and typed admission | no |
| Size and space guard before a batch | Yes | Yes - capacity check plus typed admission with recovery | no |
| Client-side content protection | Yes, WMA playsync | No, and it cannot (see below) | n/a |

## What cannot come back

These are not gaps in this port. They depended on a Microsoft service that has been
shut down, or on material Microsoft never published, and no client can restore them.

- Zune Marketplace: the storefront, the 14 million track catalogue, TV and movie
  purchase and rental, and the Zune HD apps and games catalogue.
- Zune Music Pass and every WMDRM-licensed file. A `.wmz`/`.mswmp` purchase from 2009
  has no license server to answer for it, which is also why there is no
  DRM-conversion path here.
- Zune social: presence, what-a-friend-is-listening, and the swapped files, which were
  themselves DRM-limited exchanges between two clients.
- Microsoft's metadata and artwork metaservices. Replaced here by open providers
  (MusicBrainz and Cover Art Archive for releases, Deezer and Fanart.tv for artist
  images, TMDB for video) reached per item and only on request.
- Firmware updates, Windows Phone 7 and Kin management, and deploying apps or games to
  a Zune HD.
- Sharing the library to other PCs and to Xbox 360 through Windows Media Player
  Sharing - a different dead ecosystem, and out of scope for a Zune manager.

One line in that list is a real judgement call rather than a dead service: CD ripping.
It was never a Zune protocol feature, so it is missing here because nobody has built it,
not because anything is missing. It sits in the open list below for that reason.

## Pointers to the surviving community work

Found while writing this page. None of it is a dependency of this repository, none of it
is wired in, and it is named as a pointer rather than as something reviewed line by
line. Two of them are useful for exactly the question this page is about.

- `ZuneDev/ZuneNetApi` and `JarHead4/PyZuneMetadataServer` - recreations of the original
  Zune web and metadata APIs. Between them they are the best surviving record of which
  fields the original client asked a server for, which is what a metadata feature has to
  be compared against.
- `ZuneDev/ZuneShell.dll`, `ZuneDev/MicrosoftIris`, `ZuneDev/Xune` - decompiled ZuneShell
  and UIX material and the Iris UI framework the original client was built on, plus a
  cross-platform Iris port. Reference for behaviour and layout, not for this UI.
- `kbhomes/libmtp-zune`, `nicebeard/zune-explorer` - other independent clients, useful as
  a second opinion on protocol behaviour alongside libzune.
- `project-lyra-zune/project-lyra`, `simulacra10/zunefirmware-upgrade`,
  `ZuneDev/ZuneModdingHelper` - the jailbreak, firmware and modding side. This is where
  anything about firmware or on-device app deployment would have to start from, and this
  repository does not go there.

## Still worth doing here, in the order worth doing it

Item 1 is not a feature decision, it is the precondition for the rest: the Windows
hardware gates in `WINDOWS_PORT.md` are open, and every added capability would be
measured against a player that this port has never deleted from.

1. Close the open Windows device gates: one deletion, audible playback, the photo and
   video read halves, the podcast and audiobook categories with firmware resume, then a
   Classic, a Draco and an 80/120.
2. Packaging and an install path, so a tester is not choosing a driver in Zadig by hand.
3. Ratings or hearts, plus play and skip counts, in the music schema. Everything
   rule-driven in the original client ranked on these, and the schema has no field for
   them, so this is the one gap that blocks three others. The video side already carries
   TMDB and episode ratings, which is the precedent for the migration shape.
4. Automatic playlists from rules, once there is something to rank on.
5. A per-device "sync this on connect" preference, which is what made the original
   client feel automatic. The reconnect ladder today is not that and should not be
   described as it.
6. A device clock write, if a Zune property for it exists. Nothing in this tree writes
   time, and no claim is made here about what the original client did.
7. CD ripping as an add-media path, for people whose source is discs rather than folders.
8. Per-media exact sizes on classic players, and resume as a protocol question, both
   already deferred in `FEATURE_COMPLETION.md`.

## What this page is not

It is not evidence. Every `read` and `send` cell above is a summary of a capture that
lives in [WINDOWS_PORT.md](WINDOWS_PORT.md), and the dated checkpoints there are the
record; if this page and that one ever disagree, that one is right. Software gates say a
rule is in the running binary, nothing more, and no Linux hardware result carried into
any cell here.

