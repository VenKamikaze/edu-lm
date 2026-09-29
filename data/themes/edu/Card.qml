import QtQuick

// A white rounded panel with a soft shadow.
Item {
    id: root
    property real radius: 24
    property color color: EduStyle.cardColor
    default property alias content: inner.data

    Repeater {
        model: 4
        Rectangle {
            anchors.fill: parent
            anchors.margins: -(index + 1) * 3
            radius: root.radius + (index + 1) * 3
            color: "#1b3a5a"
            opacity: 0.035
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.color: "#e3e9ee"
        border.width: 1
    }
    Item {
        id: inner
        anchors.fill: parent
    }
}
