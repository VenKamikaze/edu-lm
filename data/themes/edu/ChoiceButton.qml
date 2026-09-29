import QtQuick
import QtQuick.Controls.Basic

// A big answer button with its number key on the left.
Button {
    id: control
    property int keyNumber: 1
    property real u: EduStyle.unit(Window.width, Window.height)

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Math.round(26 * u * EduStyle.fontScale)
    font.bold: true
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Item {
        Rectangle {
            x: Math.round(16 * control.u)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round(40 * control.u); height: width; radius: width / 2
            color: control.enabled ? EduStyle.accent : "#b6bec6"
            Text {
                anchors.centerIn: parent
                text: control.keyNumber
                color: "#ffffff"
                font.bold: true
                font.pixelSize: Math.round(22 * control.u)
            }
        }
        Text {
            x: Math.round(72 * control.u)
            width: parent.width - Math.round(88 * control.u)
            height: parent.height - Math.round(10 * control.u)
            anchors.verticalCenter: parent.verticalCenter
            verticalAlignment: Text.AlignVCenter
            text: control.text
            font: control.font
            color: control.enabled ? EduStyle.textColor : "#7b848c"
            wrapMode: Text.WordWrap
            fontSizeMode: Text.Fit
            minimumPixelSize: Math.round(13 * control.u)
        }
    }

    background: Rectangle {
        radius: Math.round(20 * control.u)
        color: control.down ? Qt.darker(EduStyle.accentSoft, 1.15) : (control.hovered ? "#e3f1ef" : "#f4f7fa")
        border.width: control.activeFocus ? Math.max(3, Math.round(3 * control.u)) : 2
        border.color: control.activeFocus ? "#3b82f6" : EduStyle.borderColor
    }
}
