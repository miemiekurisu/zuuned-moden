import QtQuick

// Bottom-border tab bar — port of PivotBar.swift. Text-only tabs, 2px
// pink underline on the active tab, 1px bottom border across.
Item {
    id: root

    property var tabs: []
    property string current: tabs.length > 0 ? tabs[0] : ""
    // Optional display labels, parallel to `tabs` (2026-10-24).  `tabs` stays the
    // identity -- `current`, a host's own comparisons and `--settings-tab Device`
    // all keep answering in English -- so a page can write its tabs in another
    // language without inventing a second vocabulary for the same five buttons.
    property var labels: []

    function labelFor(name) {
        const i = tabs.indexOf(name)
        return i >= 0 && i < labels.length && labels[i].length > 0 ? labels[i] : name
    }

    implicitHeight: row.implicitHeight + Theme.spaceSm
    // The tabs' NATURAL width. The Row draws at its own size and this
    // Item never clips, so a host that reserves less than this only
    // crowds its neighbours — hosts use it as the floor when they
    // compress the reservation on narrow windows.
    implicitWidth: row.implicitWidth

    Row {
        id: row
        spacing: Theme.spaceXl

        Repeater {
            model: root.tabs

            Item {
                id: tab
                required property string modelData
                property bool hovering: false
                readonly property bool active: root.current === modelData

                width: tabLabel.implicitWidth
                height: tabLabel.implicitHeight + Theme.spaceSm

                Text {
                    id: tabLabel
                    text: root.labelFor(tab.modelData).toLowerCase()
                    font.pixelSize: 15
                    font.weight: Font.Light
                    color: tab.active ? Theme.textPrimary
                                      : (tab.hovering ? Theme.textMid : Theme.textSubtle)

                    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: Theme.pink
                    visible: tab.active
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: tab.hovering = true
                    onExited: tab.hovering = false
                    onClicked: root.current = tab.modelData
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }
}
