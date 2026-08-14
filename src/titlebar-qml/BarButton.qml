import QtQuick

/* Breeze-style title bar button: theme icon (real Breeze SVG) with
   hover highlight; the close button turns red with a white glyph. */
Item {
    id: btn
    property string iconName: ""
    property color hoverColor: "#CDD1D4"
    property color iconColor: "#232629"
    signal clicked()

    Rectangle {
        id: hoverRect
        anchors.fill: parent
        color: mouse.containsMouse ? btn.hoverColor : "transparent"
        visible: mouse.containsMouse
    }

    Image {
        anchors.centerIn: parent
        width: 16
        height: 16
        asynchronous: false
        source: btn.iconColor === "white" ? "image://themeicon/" + btn.iconName + "-white"
                                          : "image://themeicon/" + btn.iconName
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: btn.clicked()
    }
}
