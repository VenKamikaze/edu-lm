// SPDX-License-Identifier: MIT
import QtQuick

// A picture from theme.conf when set, otherwise a soft gradient with a few gentle shapes.
Item {
    id: root
    readonly property bool hasPicture: EduStyle.background !== ""

    Rectangle {
        anchors.fill: parent
        visible: !root.hasPicture
        gradient: Gradient {
            GradientStop { position: 0.0; color: EduStyle.backgroundTop }
            GradientStop { position: 1.0; color: EduStyle.backgroundBottom }
        }
    }
    Item {
        anchors.fill: parent
        visible: !root.hasPicture
        clip: true
        Rectangle {
            width: root.height * 0.9; height: width; radius: width / 2
            x: -width * 0.25; y: root.height - height * 0.7
            color: EduStyle.accent; opacity: 0.10
        }
        Rectangle {
            width: root.height * 0.6; height: width; radius: width / 2
            x: root.width - width * 0.7; y: -height * 0.3
            color: "#e9a23b"; opacity: 0.16
        }
        Rectangle {
            width: root.height * 0.35; height: width; radius: width / 2
            x: root.width * 0.78; y: root.height * 0.68
            color: "#5b8def"; opacity: 0.10
        }
        Rectangle {
            width: root.height * 0.22; height: width; radius: width / 2
            x: root.width * 0.08; y: root.height * 0.12
            color: "#e76f51"; opacity: 0.10
        }
    }
    Image {
        anchors.fill: parent
        visible: root.hasPicture
        source: root.hasPicture ? Qt.resolvedUrl(EduStyle.background) : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
}
