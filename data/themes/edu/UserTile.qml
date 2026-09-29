import QtQuick

// One person on the login screen. Enter or a click picks them.
FocusScope {
    id: root
    property string name: ""
    property string realName: ""
    property string iconSource: ""
    property bool other: false
    property real u: EduStyle.unit(Window.width, Window.height)
    signal picked()

    activeFocusOnTab: true
    width: Math.round(170 * u)
    height: Math.round(214 * u)

    Rectangle {
        anchors.fill: parent
        radius: Math.round(22 * root.u)
        color: mouse.containsMouse ? EduStyle.accentSoft : "transparent"
        border.width: root.activeFocus ? Math.max(3, Math.round(3 * root.u)) : 0
        border.color: "#3b82f6"
    }
    Avatar {
        id: avatar
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(14 * root.u)
        size: Math.round(120 * root.u)
        name: root.other ? "" : root.name
        realName: root.realName
        iconSource: root.iconSource
        visible: !root.other
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(14 * root.u)
        width: Math.round(120 * root.u)
        height: width
        radius: width / 2
        visible: root.other
        color: EduStyle.accentSoft
        border.width: 3
        border.color: EduStyle.accent
        Text {
            anchors.centerIn: parent
            text: "+"
            color: EduStyle.accent
            font.pixelSize: parent.width * 0.6
            font.bold: true
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(146 * root.u)
        width: parent.width - Math.round(12 * root.u)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        elide: Text.ElideRight
        maximumLineCount: 2
        text: root.other ? qsTr("Other\u2026") : (root.realName !== "" ? root.realName : root.name)
        color: EduStyle.textColor
        font.pixelSize: Math.round(21 * root.u * EduStyle.fontScale)
        font.bold: true
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { root.forceActiveFocus(); root.picked() }
    }
    Keys.onReturnPressed: picked()
    Keys.onEnterPressed: picked()
    Keys.onSpacePressed: picked()
}
