import QtQuick

// A user's picture, or their initials on a coloured circle.
Item {
    id: root
    property string name: ""
    property string realName: ""
    property string iconSource: ""
    property real size: 120
    property color maskColor: EduStyle.cardColor
    width: size
    height: size

    readonly property string label: realName !== "" ? realName : name

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: EduStyle.avatarColor(root.name)
        visible: !picture.visible

        Text {
            anchors.centerIn: parent
            text: EduStyle.initials(root.label)
            color: "#ffffff"
            font.pixelSize: root.size * 0.4
            font.bold: true
        }
    }
    Image {
        id: picture
        anchors.fill: parent
        source: root.iconSource
        visible: status === Image.Ready
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        asynchronous: true
        layer.enabled: false
    }
    // rounds the picture's corners without a shader
    Canvas {
        anchors.fill: parent
        visible: picture.visible
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = root.maskColor
            ctx.beginPath()
            ctx.rect(0, 0, width, height)
            ctx.arc(width / 2, height / 2, width / 2, 0, Math.PI * 2, true)
            ctx.fill()
        }
    }
}
