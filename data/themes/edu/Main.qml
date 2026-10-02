// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EduLM 1.0

// Edu-LM greeter theme: login, quiz, result and cooldown, all through eduQuiz.
Item {
    id: root
    width: 1280
    height: 720
    property real u: EduStyle.unit(width, height)

    readonly property int quizState: eduQuiz.state
    readonly property bool showQuiz: quizState === QuizClient.Question || quizState === QuizClient.Feedback
                                     || quizState === QuizClient.Result || quizState === QuizClient.Cooldown

    // the quiz is opt-in: a stock theme never asks for it
    Component.onCompleted: eduQuiz.enable()

    Background { anchors.fill: parent }

    LoginPage {
        id: login
        anchors.fill: parent
        visible: root.quizState === QuizClient.Idle
    }

    EduQuiz {
        id: quiz
        anchors.fill: parent
        visible: root.showQuiz
    }

    // checking the password, or logged in
    FocusScope {
        anchors.fill: parent
        visible: root.quizState === QuizClient.Checking || root.quizState === QuizClient.LoggedIn
        onVisibleChanged: if (visible) forceActiveFocus()
        Keys.onEscapePressed: eduQuiz.cancelChecking() // only does anything while checking

        Card {
            anchors.centerIn: parent
            width: Math.round(460 * root.u)
            height: Math.round(260 * root.u) + (eduQuiz.slowChecking ? Math.round(80 * root.u) : 0)
            radius: Math.round(28 * root.u)
            Column {
                anchors.centerIn: parent
                spacing: Math.round(22 * root.u)
                Spinner {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: Math.round(72 * root.u)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.quizState === QuizClient.LoggedIn ? qsTr("Welcome! Logging in…") : qsTr("One moment…")
                    color: EduStyle.textColor
                    font.pixelSize: Math.round(30 * root.u * EduStyle.fontScale)
                    font.bold: true
                }
                EduButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: eduQuiz.slowChecking && root.quizState === QuizClient.Checking
                    text: qsTr("Taking too long? Go back")
                    onClicked: eduQuiz.cancelChecking()
                    Component.onCompleted: if (visible) forceActiveFocus()
                    onVisibleChanged: if (visible) forceActiveFocus()
                    Keys.onEscapePressed: clicked()
                }
            }
        }
    }
}
