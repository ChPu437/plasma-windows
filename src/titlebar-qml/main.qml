import QtQuick

/* Breeze-style title bar content (hosted in a C++ QQuickWindow -
   the Window wrapper of QtQuick proved unreliable to show here). */
Item {
    id: root
    anchors.fill: parent

    /* bottom hairline */
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: "#D0D1D2"
    }

    /* title, centered (Breeze default) */
    Text {
        id: titleText
        anchors.centerIn: parent
        width: parent.width - 96
        text: bridge.title
        color: "#232629"
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    /* buttons: close | maximize | minimize from the right */
    Row {
        id: btnRow
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height

        BarButton {
            width: 32
            height: parent.height
            iconName: "window-minimize"
            hoverColor: "#CDD1D4"
            onClicked: bridge.minimize()
        }
        BarButton {
            width: 32
            height: parent.height
            iconName: bridge.maximized ? "window-restore" : "window-maximize"
            hoverColor: "#CDD1D4"
            onClicked: bridge.toggleMaximize()
        }
        BarButton {
            width: 32
            height: parent.height
            iconName: "window-close"
            hoverColor: "#E81123"
            iconColor: "white"
            onClicked: bridge.close()
        }
    }

    /* drag + double-click area (everything except the buttons) */
    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: 96
        hoverEnabled: false
        onPressed: bridge.beginDrag()
        onDoubleClicked: bridge.toggleMaximize()
    }
}
