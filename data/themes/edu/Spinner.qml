// SPDX-License-Identifier: MIT
import QtQuick

// Eight dots chasing round a circle.
Item {
    id: root
    property real size: 64
    property color color: EduStyle.accent
    width: size
    height: size

    Item {
        anchors.fill: parent
        RotationAnimator on rotation { from: 0; to: 360; duration: 1200; loops: Animation.Infinite; running: root.visible }
        Repeater {
            model: 8
            Rectangle {
                width: root.size * 0.16
                height: width
                radius: width / 2
                color: root.color
                opacity: (index + 1) / 8
                x: root.size / 2 - width / 2 + Math.cos(index * Math.PI / 4) * root.size * 0.38
                y: root.size / 2 - height / 2 + Math.sin(index * Math.PI / 4) * root.size * 0.38
            }
        }
    }
}
