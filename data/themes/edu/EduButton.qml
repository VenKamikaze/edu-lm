import QtQuick
import QtQuick.Controls.Basic

// A big rounded button that also works from the keyboard (Tab, Enter, Space).
Button {
    id: control
    property bool primary: false
    property real u: EduStyle.unit(Window.width, Window.height)
    property int pixelSize: 20

    implicitHeight: Math.round(56 * u)
    implicitWidth: Math.max(implicitHeight * 2, contentItem.implicitWidth + Math.round(48 * u))
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    font.pixelSize: Math.round(pixelSize * u * EduStyle.fontScale)
    font.bold: true

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? "#8b939b" : (control.primary ? EduStyle.accentText : EduStyle.accentDark)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: control.implicitHeight / 2.6
        color: !control.enabled ? "#e4e8ec"
             : control.primary ? (control.down ? EduStyle.accentDark : EduStyle.accent)
             : (control.down ? Qt.darker(EduStyle.accentSoft, 1.1) : (control.hovered ? Qt.rgba(EduStyle.accent.r, EduStyle.accent.g, EduStyle.accent.b, 0.22) : EduStyle.accentSoft))
        border.width: control.activeFocus ? Math.max(3, Math.round(3 * control.u)) : 0
        border.color: "#3b82f6"
    }
}
