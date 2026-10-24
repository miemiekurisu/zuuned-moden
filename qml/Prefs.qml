pragma Singleton
import QtQuick
import QtCore

// W15 "make it yours" preferences — set in first-run onboarding, editable
// in Settings, persisted via QtCore.Settings. The app reads these live
// (e.g. NowPlayingIsland picks Disc vs Vinyl off playerStyle).
QtObject {
    // "disc"  → SpinningDiscView   |  "vinyl" → VinylRecordView
    property alias playerStyle: store.playerStyle
    // "backdrop" and "displayFont" went 2026-10-24. They were aliases of a
    // second copy of two settings that really live in AppSettings
    // (backdropPreset, headerFont), written by nobody and read by nobody, so
    // this store held values that could never be the one the app used. One
    // owner per setting is the rule now.

    // QtObject has no default property, so the persistent store lives in
    // an explicit property rather than as a bare child.
    property Settings store: Settings {
        id: store
        category: "appearance"
        property string playerStyle: "disc"
    }
}
