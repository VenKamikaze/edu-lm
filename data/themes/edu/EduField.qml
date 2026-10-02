// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic

// A big rounded text field.
TextField {
    id: control
    property real u: EduStyle.unit(Window.width, Window.height)
    property int pixelSize: 22

    implicitHeight: Math.round(60 * u)
    font.pixelSize: Math.round(pixelSize * u * EduStyle.fontScale)
    color: EduStyle.textColor
    placeholderTextColor: "#7b848c"
    selectionColor: EduStyle.accent
    selectedTextColor: EduStyle.accentText
    leftPadding: Math.round(20 * u)
    rightPadding: Math.round(20 * u)
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: control.implicitHeight / 2.6
        color: "#ffffff"
        border.width: control.activeFocus ? Math.max(3, Math.round(3 * control.u)) : 2
        border.color: control.activeFocus ? "#3b82f6" : EduStyle.borderColor
    }
}
