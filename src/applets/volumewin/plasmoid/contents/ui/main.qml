/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.components as PC3
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.volumewin

PlasmoidItem {
    id: root

    compactRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing * 2
        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing * 2

        Kirigami.Icon {
            anchors.centerIn: parent
            source: vc.iconName
            animated: false
            width: Kirigami.Units.iconSizes.smallMedium
            height: Kirigami.Units.iconSizes.smallMedium
        }
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.expanded = !root.expanded
            onWheel: wheel => {
                const step = 0.05;
                vc.volume = vc.volume + (wheel.angleDelta.y > 0 ? step : -step);
            }
        }
    }

    fullRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
        Layout.preferredHeight: Kirigami.Units.gridUnit * 3

        RowLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing

            PC3.ToolButton {
                icon.name: vc.muted ? "audio-volume-muted" : "audio-volume-high"
                onClicked: vc.toggleMuted()
            }
            PC3.Slider {
                id: slider
                Layout.fillWidth: true
                from: 0
                to: 100
                value: vc.volume * 100
                onMoved: vc.volume = value / 100
            }
            PC3.Label {
                text: i18n("%1%", Math.round(slider.value))
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    VolumeController {
        id: vc
    }
}
