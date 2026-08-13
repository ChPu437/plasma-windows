/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.imewin

PlasmoidItem {
    id: root

    compactRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing * 2
        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing * 2

        Text {
            anchors.centerIn: parent
            text: ime.layoutCode
            font.pixelSize: Kirigami.Units.fontMetrics.fontSize
            font.bold: true
            color: Kirigami.Theme.textColor
        }
        MouseArea {
            anchors.fill: parent
            onClicked: ime.toggle()
        }
    }

    ImeController {
        id: ime
    }
}
