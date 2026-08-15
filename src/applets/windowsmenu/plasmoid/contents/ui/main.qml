/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.kde.plasma.windowsmenu

PlasmoidItem {
    id: root

    // Panel: a start button. Clicking expands the full menu.
    compactRepresentation: MouseArea {
        id: compactArea
        Layout.preferredWidth: Kirigami.Units.iconSizes.medium + Kirigami.Units.smallSpacing * 2
        Layout.preferredHeight: Kirigami.Units.iconSizes.medium + Kirigami.Units.smallSpacing * 2

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "start-here-symbolic"
            implicitWidth: Kirigami.Units.iconSizes.medium
            implicitHeight: Kirigami.Units.iconSizes.medium
        }
        onClicked: root.expanded = !root.expanded
    }

    // The menu itself (shown in the popup when expanded).
    fullRepresentation: Item {
        id: menuRoot
        Layout.preferredWidth: Kirigami.Units.gridUnit * 30
        Layout.preferredHeight: Kirigami.Units.gridUnit * 26

        StartMenuModel {
            id: menu
            directory: "all"
        }

        PowerActions {
            id: power
        }

        // Sidebar: All Apps + user-configured directories.
        property var sidebarDirs: []

        function sidebarModel() {
            const cats = []
            for (let i = 0; i < menu.categories.length; i++) {
                cats.push({ name: menu.categories[i].name, path: "cat:" + menu.categories[i].name, icon: menu.categories[i].icon || "folder-symbolic" })
            }
            return [
                { name: i18n("All Apps"), path: "all", icon: "view-grid-symbolic" },
                ...cats,
                ...sidebarDirs,
            ]
        }

        function currentSidebarIndex() {
            const list = sidebarModel()
            for (let i = 0; i < list.length; i++) {
                if ((list[i].path === "all" && menu.isRoot) || (list[i].path !== "all" && menu.directory === list[i].path)) {
                    return i
                }
            }
            return 0
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            // Left column: sidebar (All Apps + categories) and the
            // power menu at the bottom.
            ColumnLayout {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                // Left: sidebar (All Apps + user dirs)
                ListView {
                    id: sidebarView
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    model: menuRoot.sidebarModel()
                    currentIndex: menuRoot.currentSidebarIndex()
                    clip: true

                    delegate: QQC2.ToolButton {
                        id: sideButton
                        width: sidebarView.width
                        text: modelData.name
                        icon.name: modelData.icon || "folder-symbolic"
                        highlighted: sidebarView.currentIndex === index
                        onClicked: {
                            sidebarView.currentIndex = index
                            menu.directory = modelData.path
                        }
                    }
                }

                // Power menu (shutdown / reboot / suspend / hibernate / lock)
                QQC2.ToolButton {
                    id: powerButton
                    Layout.fillWidth: true
                    text: i18n("Power")
                    icon.name: "system-shutdown-symbolic"
                    onClicked: powerMenu.popup(powerButton, powerButton.width - powerMenu.width, powerButton.height)
                }
            }

            // Right: current view (all apps grid or dir contents)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    visible: !menu.isRoot
                    QQC2.ToolButton {
                        icon.name: "go-previous-symbolic"
                        onClicked: menu.goParent()
                    }
                    Kirigami.Heading {
                        level: 4
                        text: menu.isRoot ? "" : menu.directory.split("/").pop()
                        elide: Text.ElideMiddle
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                GridView {
                    id: grid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: menu
                    clip: true
                    cacheBuffer: Kirigami.Units.gridUnit * 8
                    cellWidth: Kirigami.Units.gridUnit * 6
                    cellHeight: Kirigami.Units.gridUnit * 6

                delegate: Item {
                    id: delegateItem
                    width: grid.cellWidth
                    height: grid.cellHeight

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Kirigami.Units.smallSpacing

                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            width: Kirigami.Units.iconSizes.medium
                            height: Kirigami.Units.iconSizes.medium

                            Kirigami.Icon {
                                anchors.fill: parent
                                source: "folder-symbolic"
                                visible: model.isDir
                            }
                            Image {
                                anchors.fill: parent
                                source: model.isDir ? "" : "image://startmenu/" + encodeURIComponent(model.linkPath)
                                fillMode: Image.PreserveAspectFit
                                visible: !model.isDir
                            }
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: delegateItem.width - Kirigami.Units.smallSpacing * 2
                            text: model.name
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 13
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (model.isDir) {
                                menu.directory = model.path
                            } else {
                                menu.launch(index)
                            }
                        }
                    }
                    QQC2.ToolTip.visible: itemMouse.containsMouse
                    QQC2.ToolTip.text: model.name
                }
                }
            }
        }

        QQC2.Menu {
            id: powerMenu
            title: i18n("Power")

            QQC2.MenuItem {
                text: i18n("Shut Down")
                icon.name: "system-shutdown-symbolic"
                enabled: power.canShutdown
                onClicked: {
                    root.expanded = false
                    power.shutdown()
                }
            }
            QQC2.MenuItem {
                text: i18n("Restart")
                icon.name: "system-reboot-symbolic"
                enabled: power.canReboot
                onClicked: {
                    root.expanded = false
                    power.reboot()
                }
            }
            QQC2.MenuItem {
                text: i18n("Sleep")
                icon.name: "system-suspend-symbolic"
                enabled: power.canSuspend
                onClicked: {
                    root.expanded = false
                    power.suspend()
                }
            }
            QQC2.MenuItem {
                text: i18n("Hibernate")
                icon.name: "system-suspend-hibernate-symbolic"
                enabled: power.canHibernate
                onClicked: {
                    root.expanded = false
                    power.hibernate()
                }
            }
            QQC2.MenuItem {
                text: i18n("Lock Screen")
                icon.name: "system-lock-screen-symbolic"
                enabled: power.canLock
                onClicked: {
                    root.expanded = false
                    power.lock()
                }
            }
        }
    }
}
