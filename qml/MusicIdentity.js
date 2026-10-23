.pragma library

// The library groups albums by album artist + title. Device snapshots
// lack albumartist, so their track artist is the available owner.
//
// A Zune that never learned a name does not leave the field empty: it stores a
// GUID moniker and shows that on its own screen, so the same string arrives here
// through a device row, through an import folder a pull was filed under, and
// through any local file a tagless rip left behind. This file is the last mile -
// MusicPage, ArtistDetail and GenreDetail all group through it for BOTH sources -
// so it repeats the one C++ predicate (MusicIdentity::isGuidMoniker in
// src/library/MusicIdentity.h) rather than trusting its callers, because a group
// that keeps the moniker turns it into a tile name on screen. Braces are optional
// and surrounding blanks are not part of the answer, exactly as there.
const guidMoniker = /^(?:\{)?[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}(?:\})?$/
function isMoniker(text) {
    return guidMoniker.test(String(text === undefined || text === null ? "" : text).trim())
}

// A piece of text as a name, or empty when it is a moniker. Empty is the signal the
// callers below already handle with their own Unknown Artist / Unknown Album.
function known(text) {
    const value = String(text === undefined || text === null ? "" : text).trim()
    return isMoniker(value) ? "" : value
}

// The artist a row belongs to. Album artist wins where the source has one.
function owner(track) {
    return known(track.albumartist) || known(track.artist) || "Unknown Artist"
}

// The Artists pivot groups by the row's own artist, not by an albumartist hint.
function artistName(track) {
    return known(track.artist) || "Unknown Artist"
}

function fromTrack(track) {
    return { name: known(track.album) || "Unknown Album", artist: owner(track) }
}

function fromAlbum(album) {
    return { name: known(album.name) || "Unknown Album",
             artist: known(album.artArtist) || known(album.artist)
                     || known(album.subtitle) || "Unknown Artist" }
}

function key(album) {
    return JSON.stringify([album.artist.toLowerCase(), album.name.toLowerCase()])
}

function matches(track, album) {
    return album && key(fromTrack(track)) === key(album)
}

// Local music keeps disc identity through album drill-downs, playback and sync.
// Unknown disc behaves as disc1; unknown track retains a stable title/path order.
function compareAlbumTracks(a, b) {
    const disc = (a.discNumber > 0 ? a.discNumber : 1) - (b.discNumber > 0 ? b.discNumber : 1)
    if (disc) return disc
    return (a.trackNumber || 0) - (b.trackNumber || 0)
        || (a.title || "").localeCompare(b.title || "", undefined, { sensitivity: "base" })
        || (a.filepath || "").localeCompare(b.filepath || "")
}

function groupAlbums(tracks) {
    const groups = {}
    for (const track of tracks) {
        const album = fromTrack(track)
        const id = key(album)
        if (!groups[id]) {
            groups[id] = { name: album.name, subtitle: album.artist,
                artArtist: album.artist, artAlbum: track.album || "",
                artItemId: track.itemId || 0,
                firstTrackPath: track.filepath || "", count: 0 }
        }
        groups[id].count++
    }
    return Object.values(groups).sort((a, b) =>
        a.name.localeCompare(b.name, undefined, { sensitivity: "base" })
        || a.artArtist.localeCompare(b.artArtist, undefined, { sensitivity: "base" }))
}

// Who owns the only real picture the library already holds for a person: the
// cover of one of their own albums, keyed by lowercased album artist. Both
// album sources share this row shape - LibraryService's albumsList and
// groupAlbums above - so the Artists pivot and an artist page agree, and
// neither one asks a provider for anything.
//
// Which of an artist's albums supplies the picture is the whole question. One
// that actually has cover art wins outright: a listener with a 40-track
// compilation-less discography and art on one EP should not get an empty
// circle because that EP is not their biggest release. Size only breaks the
// tie, so a one-album artist still wears that album's cover. `hasArt`, when the
// caller passes one, answers for an album row - omit it and every album is a
// candidate, which is what a source without a cache to consult wants.
function localArtOwners(albums, hasArt) {
    const owners = {}
    for (const album of albums || []) {
        if (!album || !album.artAlbum) continue
        const owner = String(album.artArtist || "").toLowerCase()
        if (!owner) continue
        const art = hasArt ? (hasArt(album) ? 1 : 0) : 1
        const count = album.count || 0
        const previous = owners[owner]
        if (previous && (previous.art > art
                         || (previous.art === art && previous.count >= count)))
            continue
        owners[owner] = { artist: album.artArtist || "", album: album.artAlbum,
                          filepath: album.firstTrackPath || "",
                          count: count, art: art }
    }
    return owners
}
