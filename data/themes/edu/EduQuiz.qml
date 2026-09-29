import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EduLM 1.0

// The quiz screens, driven by eduQuiz.state: question, feedback, result and cooldown.
// Keys work wherever the focus is inside: 1-4 answer, S swaps, Escape goes back.
FocusScope {
    id: root
    property real u: EduStyle.unit(width, height)
    property bool confirmingBack: false
    property string shownQuestion: ""

    readonly property int quizState: eduQuiz.state
    readonly property bool onQuestion: quizState === QuizClient.Question
    readonly property bool onFeedback: quizState === QuizClient.Feedback
    readonly property bool onResult: quizState === QuizClient.Result
    readonly property bool onCooldown: quizState === QuizClient.Cooldown
    readonly property bool textAnswer: eduQuiz.questionType === "numeric" || eduQuiz.questionType === "short_text"
    readonly property var options: eduQuiz.questionType === "true_false" ? [qsTr("True"), qsTr("False")] : eduQuiz.choices
    readonly property bool shortChoices: {
        for (var i = 0; i < options.length; ++i)
            if (options[i].length > 26)
                return false
        return true
    }
    // the answer buttons and fields work only when no dialog is up
    readonly property bool interactive: onQuestion && !confirmingBack && !eduQuiz.waiting

    function pxs(n) { return Math.round(n * root.u * EduStyle.fontScale) }

    function choose(i) {
        if (!interactive || i >= options.length)
            return
        // multiple choice answers by position, true/false by word
        eduQuiz.answer(eduQuiz.questionType === "true_false" ? (i === 0 ? "true" : "false") : String(i))
    }

    function submitText() {
        if (!interactive || answerField.text.trim() === "")
            return
        eduQuiz.answer(answerField.text.trim())
        answerField.text = ""
    }

    function swapMessage(reason) {
        if (reason === "already-attempted") return qsTr("You've already tried this one")
        if (reason === "no-more-questions") return qsTr("No more questions to swap in")
        if (reason === "no-swaps-left") return qsTr("No swaps left")
        return ""
    }

    function askToGoBack() {
        // leaving after an answer counts as a try
        if (!onQuestion || confirmingBack)
            return
        if (eduQuiz.answerStarted) {
            confirmingBack = true
            stayButton.forceActiveFocus()
        } else {
            eduQuiz.abandon()
        }
    }

    function scrollPrompt(delta) {
        promptView.contentY = Math.max(0, Math.min(promptView.contentHeight - promptView.height, promptView.contentY + delta))
    }

    function focusQuestion() {
        if (textAnswer)
            answerField.forceActiveFocus()
        else
            focusSink.forceActiveFocus()
    }

    // a new question starts with an empty field; a swap that is refused or a retry keeps the focus where it was
    function questionArrived() {
        if (eduQuiz.questionId !== shownQuestion) {
            shownQuestion = eduQuiz.questionId
            answerField.text = ""
            promptView.contentY = 0
            focusQuestion()
        }
    }

    onOnQuestionChanged: if (onQuestion) { confirmingBack = false; questionArrived(); focusQuestion() }
    Connections {
        target: eduQuiz
        function onQuestionChanged() { if (root.onQuestion) root.questionArrived() }
    }
    onOnResultChanged: if (onResult) resultFocus.forceActiveFocus()
    onOnCooldownChanged: if (onCooldown) cooldownBack.forceActiveFocus()

    Keys.onPressed: (event) => {
        if (!onQuestion && !onFeedback)
            return
        if (event.key === Qt.Key_Escape) {
            root.askToGoBack() // ignored during the feedback screen
            event.accepted = true
        } else if (root.interactive) {
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_4) {
                root.choose(event.key - Qt.Key_1)
                event.accepted = true
            } else if (event.key === Qt.Key_S) {
                eduQuiz.swap()
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                root.scrollPrompt(root.pxs(60)); event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                root.scrollPrompt(-root.pxs(60)); event.accepted = true
            }
        }
    }

    // ---- question ----
    Item {
        id: questionPage
        anchors.fill: parent
        visible: root.onQuestion || root.onFeedback

        // somewhere for the focus to sit when nothing else is wanted; keys go up to root
        Item { id: focusSink; focus: true }

        Text {
            id: progress
            anchors.top: parent.top
            anchors.topMargin: Math.round(28 * root.u)
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Question %1 of %2").arg(eduQuiz.questionNumber).arg(eduQuiz.questionCount)
            color: EduStyle.mutedTextColor
            font.pixelSize: root.pxs(22)
            font.bold: true
        }
        Row {
            id: dots
            anchors.top: progress.bottom
            anchors.topMargin: Math.round(10 * root.u)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Math.round(8 * root.u)
            Repeater {
                model: eduQuiz.questionCount
                Rectangle {
                    width: Math.round(34 * root.u); height: Math.round(8 * root.u); radius: height / 2
                    color: index < eduQuiz.questionNumber - 1 ? EduStyle.accent
                         : index === eduQuiz.questionNumber - 1 ? EduStyle.accentDark : "#c9d3dc"
                }
            }
        }

        // everything a dialog must shut out
        FocusScope {
            id: controls
            anchors.fill: parent
            enabled: !root.confirmingBack && root.onQuestion

            Card {
                id: questionCard
                anchors.top: parent.top
                anchors.topMargin: dots.y + dots.height + Math.round(28 * root.u)
                anchors.bottom: bottomBar.top
                anchors.bottomMargin: Math.round(16 * root.u)
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(root.width - Math.round(60 * root.u), Math.round(1000 * root.u))
                radius: Math.round(28 * root.u)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Math.round(24 * root.u)
                    spacing: Math.round(14 * root.u)

                    // the prompt shrinks to fit, and scrolls (arrow keys, wheel) when even the smallest size does not
                    Flickable {
                        id: promptView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: Math.round(90 * root.u)
                        contentWidth: width
                        contentHeight: Math.max(promptText.paintedHeight, height)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        Text {
                            id: promptText
                            width: promptView.width - Math.round(12 * root.u)
                            height: promptView.height
                            // centred when it fits, from the top when it scrolls
                            transform: Translate { y: Math.max(0, (promptView.height - promptText.paintedHeight) / 2) }
                            text: eduQuiz.prompt
                            wrapMode: Text.WordWrap
                            color: EduStyle.textColor
                            font.pixelSize: root.pxs(32)
                            fontSizeMode: Text.Fit
                            minimumPixelSize: root.pxs(17)
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignTop
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        visible: !root.textAnswer
                        columns: root.shortChoices ? 2 : 1
                        columnSpacing: Math.round(14 * root.u)
                        rowSpacing: Math.round(10 * root.u)
                        Repeater {
                            model: root.options
                            delegate: ChoiceButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.round(root.shortChoices ? 76 * root.u : 68 * root.u)
                                keyNumber: index + 1
                                text: modelData
                                enabled: root.interactive
                                onClicked: root.choose(index)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.textAnswer
                        spacing: Math.round(12 * root.u)
                        EduField {
                            id: answerField
                            Layout.fillWidth: true
                            placeholderText: eduQuiz.questionType === "numeric" ? qsTr("Type your answer (a number)") : qsTr("Type your answer")
                            enabled: root.interactive
                            onAccepted: root.submitText()
                        }
                        EduButton {
                            primary: true
                            text: qsTr("Check")
                            enabled: root.interactive && answerField.text.trim() !== ""
                            onClicked: root.submitText()
                        }
                    }
                }
            }

            RowLayout {
                id: bottomBar
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Math.round(24 * root.u)
                anchors.horizontalCenter: parent.horizontalCenter
                width: questionCard.width
                spacing: Math.round(14 * root.u)
                EduButton {
                    text: qsTr("Back")
                    enabled: root.onQuestion
                    onClicked: root.askToGoBack()
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.swapMessage(eduQuiz.swapRefusedReason)
                    color: EduStyle.wrongColor
                    font.pixelSize: root.pxs(19)
                    font.bold: true
                }
                EduButton {
                    text: eduQuiz.swapsLeft > 0 ? qsTr("Swap question (%1 left)").arg(eduQuiz.swapsLeft) : qsTr("No swaps left")
                    enabled: root.interactive && eduQuiz.swapsLeft > 0
                    onClicked: eduQuiz.swap()
                }
            }
        }

        // "Going back counts as a try": modal, the focus stays inside it
        Rectangle {
            anchors.fill: parent
            visible: root.confirmingBack
            color: "#66203040"
            MouseArea { anchors.fill: parent }
            FocusScope {
                anchors.fill: parent
                Card {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Math.round(60 * root.u), Math.round(560 * root.u))
                    height: Math.round(250 * root.u)
                    radius: Math.round(28 * root.u)
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Math.round(28 * root.u)
                        spacing: Math.round(20 * root.u)
                        Text {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: qsTr("Going back now counts as a try. Go back?")
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: EduStyle.textColor
                            font.pixelSize: root.pxs(26)
                            font.bold: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Math.round(14 * root.u)
                            EduButton {
                                id: stayButton
                                Layout.fillWidth: true
                                primary: true
                                text: qsTr("Stay")
                                KeyNavigation.tab: goBackButton
                                KeyNavigation.backtab: goBackButton
                                onClicked: { root.confirmingBack = false; root.focusQuestion() }
                                Keys.onEscapePressed: clicked()
                            }
                            EduButton {
                                id: goBackButton
                                Layout.fillWidth: true
                                text: qsTr("Go back")
                                KeyNavigation.tab: stayButton
                                KeyNavigation.backtab: stayButton
                                onClicked: { root.confirmingBack = false; eduQuiz.abandon() }
                                Keys.onEscapePressed: stayButton.clicked()
                            }
                        }
                    }
                }
            }
        }

        // "Correct!" / "Not quite"
        Rectangle {
            anchors.fill: parent
            visible: root.onFeedback
            color: "#f2f3f9ff"
            Column {
                anchors.centerIn: parent
                spacing: Math.round(20 * root.u)
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.round(150 * root.u); height: width; radius: width / 2
                    color: eduQuiz.feedbackCorrect ? EduStyle.correctColor : EduStyle.wrongColor
                    Text {
                        anchors.centerIn: parent
                        text: eduQuiz.feedbackCorrect ? "\u2713" : "\u2715"
                        color: "#ffffff"
                        font.pixelSize: parent.width * 0.62
                        font.bold: true
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: eduQuiz.feedbackCorrect ? qsTr("Correct!")
                        : (eduQuiz.feedbackFinished ? qsTr("Not quite") : qsTr("Not quite, try again (%1 left)").arg(eduQuiz.attemptsLeft))
                    color: eduQuiz.feedbackCorrect ? EduStyle.correctColor : EduStyle.wrongColor
                    font.pixelSize: root.pxs(46)
                    font.bold: true
                }
            }
        }
    }

    // ---- result ----
    Item {
        id: resultPage
        anchors.fill: parent
        visible: root.onResult

        Item {
            id: resultFocus
            focus: true
            Keys.onPressed: (event) => {
                if (eduQuiz.resultPassed)
                    return
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    eduQuiz.continueFromResult()
                    event.accepted = true
                } else if (event.key === Qt.Key_Down) {
                    review.contentY = Math.min(review.contentHeight - review.height, review.contentY + root.pxs(90)); event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    review.contentY = Math.max(0, review.contentY - root.pxs(90)); event.accepted = true
                }
            }
        }

        // passed
        Column {
            anchors.centerIn: parent
            spacing: Math.round(18 * root.u)
            visible: eduQuiz.resultPassed
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.round(150 * root.u); height: width; radius: width / 2
                color: EduStyle.correctColor
                Text { anchors.centerIn: parent; text: "✓"; color: "#ffffff"; font.pixelSize: parent.width * 0.62; font.bold: true }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Well done!")
                color: EduStyle.correctColor
                font.pixelSize: root.pxs(52)
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("%1 of %2 correct").arg(eduQuiz.resultCorrect).arg(eduQuiz.resultAsked)
                color: EduStyle.textColor
                font.pixelSize: root.pxs(30)
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Math.round(14 * root.u)
                Spinner { size: Math.round(36 * root.u); anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Logging you in…")
                    color: EduStyle.mutedTextColor
                    font.pixelSize: root.pxs(24)
                }
            }
        }

        // failed
        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: Math.round(28 * root.u)
            anchors.bottomMargin: Math.round(24 * root.u)
            anchors.leftMargin: Math.round(30 * root.u)
            anchors.rightMargin: Math.round(30 * root.u)
            spacing: Math.round(10 * root.u)
            visible: !eduQuiz.resultPassed

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Not this time")
                color: EduStyle.textColor
                font.pixelSize: root.pxs(44)
                font.bold: true
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("%1 of %2 correct, and you needed %3").arg(eduQuiz.resultCorrect).arg(eduQuiz.resultAsked).arg(eduQuiz.resultRequired)
                color: EduStyle.mutedTextColor
                font.pixelSize: root.pxs(24)
            }

            Card {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(root.width - Math.round(60 * root.u), Math.round(1000 * root.u))
                Layout.topMargin: Math.round(8 * root.u)
                radius: Math.round(24 * root.u)

                Text {
                    anchors.centerIn: parent
                    visible: eduQuiz.resultReview.length === 0
                    text: qsTr("Have another go in a little while.")
                    color: EduStyle.mutedTextColor
                    font.pixelSize: root.pxs(24)
                }
                Flickable {
                    id: review
                    anchors.fill: parent
                    anchors.margins: Math.round(12 * root.u)
                    contentWidth: width
                    contentHeight: reviewColumn.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    visible: eduQuiz.resultReview.length > 0
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Column {
                        id: reviewColumn
                        width: review.width - Math.round(16 * root.u)
                        spacing: Math.round(10 * root.u)
                        Repeater {
                            model: eduQuiz.resultReview
                            delegate: Rectangle {
                                width: reviewColumn.width
                                height: itemColumn.height + Math.round(28 * root.u)
                                radius: Math.round(16 * root.u)
                                color: "#f4f7fa"
                                Column {
                                    id: itemColumn
                                    x: Math.round(18 * root.u); y: Math.round(14 * root.u)
                                    width: parent.width - Math.round(36 * root.u)
                                    spacing: Math.round(4 * root.u)
                                    Text {
                                        width: parent.width; wrapMode: Text.WordWrap
                                        text: modelData.prompt
                                        color: EduStyle.textColor; font.bold: true; font.pixelSize: root.pxs(22)
                                    }
                                    Text {
                                        width: parent.width; wrapMode: Text.WordWrap
                                        text: qsTr("Your answer: %1").arg(modelData.givenAnswer !== "" ? modelData.givenAnswer : qsTr("(none)"))
                                        color: EduStyle.wrongColor; font.pixelSize: root.pxs(20)
                                    }
                                    Text {
                                        width: parent.width; wrapMode: Text.WordWrap
                                        text: qsTr("Right answer: %1").arg(modelData.correctAnswer)
                                        color: EduStyle.correctColor; font.bold: true; font.pixelSize: root.pxs(20)
                                    }
                                    Text {
                                        width: parent.width; wrapMode: Text.WordWrap
                                        visible: modelData.explanation !== ""
                                        text: modelData.explanation
                                        color: EduStyle.mutedTextColor; font.pixelSize: root.pxs(18)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            EduButton {
                Layout.alignment: Qt.AlignHCenter
                primary: true
                text: qsTr("Continue")
                onClicked: eduQuiz.continueFromResult()
            }
        }
    }

    // ---- cooldown ----
    Item {
        id: cooldownPage
        anchors.fill: parent
        visible: root.onCooldown

        Card {
            anchors.centerIn: parent
            width: Math.min(root.width - Math.round(60 * root.u), Math.round(620 * root.u))
            height: cooldownColumn.height + Math.round(64 * root.u)
            radius: Math.round(28 * root.u)
            Column {
                id: cooldownColumn
                anchors.centerIn: parent
                width: parent.width - Math.round(64 * root.u)
                spacing: Math.round(14 * root.u)
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Time for a little break")
                    color: EduStyle.textColor
                    font.pixelSize: root.pxs(32)
                    font.bold: true
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Try again in %1").arg(Math.floor(eduQuiz.cooldownSeconds / 60) + ":" + ("0" + (eduQuiz.cooldownSeconds % 60)).slice(-2))
                    color: EduStyle.accentDark
                    font.pixelSize: root.pxs(60)
                    font.bold: true
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("You'll be back at the login screen when the time is up.")
                    color: EduStyle.mutedTextColor
                    font.pixelSize: root.pxs(20)
                }
                EduButton {
                    id: cooldownBack
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Back")
                    onClicked: eduQuiz.dismiss()
                    Keys.onEscapePressed: clicked()
                }
            }
        }
    }
}
