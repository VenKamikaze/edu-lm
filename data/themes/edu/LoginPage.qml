// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EduLM 1.0

// Who is logging in, and their password. Everything goes through eduQuiz.login().
FocusScope {
    id: root
    property real u: EduStyle.unit(width, height)
    property alias sessionIndex: sessions.currentIndex

    property string mode: "users" // users | password | other
    property string userName: ""
    property string userRealName: ""
    property string userIcon: ""

    readonly property bool needsPassword: !(eduQuiz.statusKnown && eduQuiz.quizRequired && !eduQuiz.studentPasswords)
    readonly property bool checking: eduQuiz.state === QuizClient.Checking
    // Log in stays enabled when there is no quiz connection at all, so parents can still log in
    readonly property bool canSubmit: !checking && !eduQuiz.connecting

    function pick(name, realName, icon) {
        userName = name
        userRealName = realName
        userIcon = icon
        eduQuiz.query(name)
        password.text = ""
        mode = "password"
        focusMode()
    }

    function focusMode() {
        if (mode === "users")
            (firstTile.visible ? firstTile : otherTile).forceActiveFocus()
        else if (mode === "other")
            otherName.forceActiveFocus()
        else if (needsPassword)
            password.forceActiveFocus()
        else
            startButton.forceActiveFocus()
    }

    function back() {
        password.text = ""
        eduQuiz.dismiss()
        mode = "users"
        focusMode()
    }

    function submit() {
        if (eduQuiz.state !== QuizClient.Idle || eduQuiz.connecting)
            return
        eduQuiz.login(userName, password.text, sessions.currentIndex)
        password.text = ""
    }

    onVisibleChanged: if (visible) focusMode()
    Component.onCompleted: focusMode()
    onNeedsPasswordChanged: if (mode === "password") focusMode()

    Keys.onEscapePressed: if (mode !== "users") back()

    // a wrong password puts the child back in the field
    Connections {
        target: eduQuiz
        function onStateChanged() {
            if (eduQuiz.state === QuizClient.Idle && root.visible && root.mode === "password")
                root.focusMode()
        }
    }

    Column {
        id: clock
        visible: !banner.visible // the banner takes the clock's place
        anchors.top: parent.top
        anchors.topMargin: Math.round(36 * root.u)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 2
        Text {
            id: timeText
            anchors.horizontalCenter: parent.horizontalCenter
            color: EduStyle.textColor
            font.pixelSize: Math.round(64 * root.u * EduStyle.fontScale)
            font.weight: Font.Light
        }
        Text {
            id: dateText
            anchors.horizontalCenter: parent.horizontalCenter
            color: EduStyle.mutedTextColor
            font.pixelSize: Math.round(22 * root.u * EduStyle.fontScale)
        }
        Timer {
            interval: 1000; running: root.visible; repeat: true; triggeredOnStart: true
            onTriggered: {
                var now = new Date()
                timeText.text = Qt.formatTime(now, Qt.locale().timeFormat(Locale.ShortFormat))
                dateText.text = Qt.formatDate(now, Qt.locale().dateFormat(Locale.LongFormat))
            }
        }
    }

    readonly property real tileWidth: Math.round(180 * u)
    readonly property int tileCount: usersRepeater.count + 1

    Card {
        id: card
        anchors.horizontalCenter: parent.horizontalCenter
        y: banner.visible ? banner.y + banner.height + Math.round(16 * root.u) : clock.y + clock.height + Math.round(28 * root.u)
        width: root.mode === "users"
               ? Math.min(root.width - Math.round(80 * root.u), Math.max(Math.round(520 * root.u), root.tileCount * root.tileWidth + Math.round(64 * root.u)))
               : Math.min(root.width - Math.round(80 * root.u), Math.round(620 * root.u))
        height: Math.min(root.height - y - Math.round(100 * root.u),
                         root.mode === "users" ? usersColumn.height + Math.round(56 * root.u) : content.implicitHeight + Math.round(64 * root.u))
        radius: Math.round(28 * root.u)

        // pick someone
        Flickable {
            id: usersView
            anchors.fill: parent
            anchors.margins: Math.round(28 * root.u)
            visible: root.mode === "users"
            contentWidth: width
            contentHeight: usersColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: usersColumn
                width: usersView.width
                spacing: Math.round(10 * root.u)
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Who's logging in?")
                    color: EduStyle.textColor
                    font.pixelSize: Math.round(28 * root.u * EduStyle.fontScale)
                    font.bold: true
                }
                Flow {
                    id: tiles
                    width: parent.width
                    spacing: 0
                    leftPadding: Math.max(0, (width - Math.min(root.tileCount, Math.max(1, Math.floor(width / root.tileWidth))) * root.tileWidth) / 2)
                    Repeater {
                        id: usersRepeater
                        model: userModel
                        delegate: UserTile {
                            id: tile
                            width: root.tileWidth
                            name: model.name
                            realName: model.realName
                            iconSource: EduStyle.ownIcon(model.icon, model.homeDir)
                            onPicked: root.pick(model.name, model.realName, tile.iconSource)
                            Component.onCompleted: if (index === 0) root.firstTile = tile
                        }
                    }
                    UserTile {
                        id: otherTile
                        width: root.tileWidth
                        other: true
                        onPicked: { root.mode = "other"; otherName.text = ""; root.focusMode() }
                    }
                }
            }
        }

        // scrolls if a small screen leaves too little room
        Flickable {
            id: contentView
            anchors.fill: parent
            visible: root.mode !== "users"
            contentWidth: width
            contentHeight: content.implicitHeight + Math.round(64 * root.u)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            x: Math.round(32 * root.u)
            y: Math.round(32 * root.u)
            width: contentView.width - Math.round(64 * root.u)
            spacing: Math.round(14 * root.u)

            Avatar {
                Layout.alignment: Qt.AlignHCenter
                size: Math.round((banner.visible ? 72 : 110) * root.u)
                name: root.mode === "other" ? "" : root.userName
                realName: root.userRealName
                iconSource: root.userIcon
                visible: root.mode === "password"
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.mode === "other" ? qsTr("Who is logging in?")
                    : (root.userRealName !== "" ? root.userRealName : root.userName)
                color: EduStyle.textColor
                font.pixelSize: Math.round(30 * root.u * EduStyle.fontScale)
                font.bold: true
            }

            // shown to kids who will get a quiz
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: root.mode === "password" && eduQuiz.statusKnown && eduQuiz.quizRequired
                implicitHeight: hint.implicitHeight + Math.round(20 * root.u)
                radius: height / 2.4
                color: EduStyle.accentSoft
                Text {
                    id: hint
                    anchors.centerIn: parent
                    width: parent.width - Math.round(32 * root.u)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: eduQuiz.questionCount > 0
                        ? qsTr("You'll answer %1 questions before logging in").arg(eduQuiz.questionCount)
                        : qsTr("You'll answer some questions before logging in")
                    color: EduStyle.accentDark
                    font.pixelSize: Math.round(19 * root.u * EduStyle.fontScale)
                    font.bold: true
                }
            }

            EduField {
                id: otherName
                Layout.fillWidth: true
                visible: root.mode === "other"
                placeholderText: qsTr("Username")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: nextButton.clicked()
            }
            EduField {
                id: password
                Layout.fillWidth: true
                visible: root.mode === "password" && root.needsPassword
                enabled: !root.checking
                echoMode: TextInput.Password
                passwordCharacter: "●"
                placeholderText: qsTr("Password")
                onAccepted: loginButton.clicked()
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text !== ""
                color: EduStyle.wrongColor
                font.pixelSize: Math.round(19 * root.u * EduStyle.fontScale)
                font.bold: true
                text: eduQuiz.notice === QuizClient.WrongPassword ? qsTr("That password didn't work. Please try again.")
                    : eduQuiz.notice === QuizClient.LoginFailed ? qsTr("Couldn't log in. Please try again, and ask a parent if it keeps happening.")
                    : ""
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text !== "" && root.mode === "password" && eduQuiz.errorMessage === ""
                color: EduStyle.mutedTextColor
                font.pixelSize: Math.round(16 * root.u * EduStyle.fontScale)
                text: eduQuiz.infoMessage
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Math.round(12 * root.u)
                EduButton {
                    text: qsTr("Back")
                    onClicked: root.back()
                }
                Item { Layout.fillWidth: true }
                EduButton {
                    id: nextButton
                    visible: root.mode === "other"
                    primary: true
                    enabled: otherName.text.trim() !== ""
                    text: qsTr("Next")
                    onClicked: if (enabled) root.pick(otherName.text.trim(), "", "")
                }
                EduButton {
                    id: loginButton
                    visible: root.mode === "password" && root.needsPassword
                    primary: true
                    enabled: root.canSubmit
                    text: qsTr("Log in")
                    onClicked: root.submit()
                }
                EduButton {
                    id: startButton
                    visible: root.mode === "password" && !root.needsPassword
                    primary: true
                    enabled: root.canSubmit
                    text: qsTr("Start")
                    onClicked: root.submit()
                }
            }
        }
        }
    }

    property var firstTile: otherTile

    // a problem with the quiz: shown here, not instead of the login screen
    Rectangle {
        id: banner
        visible: eduQuiz.errorMessage !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(20 * root.u)
        width: Math.min(root.width - Math.round(60 * root.u), Math.round(760 * root.u))
        height: bannerRow.implicitHeight + Math.round(24 * root.u)
        radius: Math.round(20 * root.u)
        color: "#fff4e5"
        border.color: "#e9a23b"
        border.width: 2
        RowLayout {
            id: bannerRow
            anchors.fill: parent
            anchors.margins: Math.round(12 * root.u)
            spacing: Math.round(12 * root.u)
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Something isn't working. Please ask a parent for help.") + "\n" + eduQuiz.errorMessage
                color: EduStyle.textColor
                font.pixelSize: Math.round(16 * root.u * EduStyle.fontScale)
            }
            EduButton {
                text: qsTr("OK")
                pixelSize: 16
                implicitHeight: Math.round(44 * root.u)
                visible: !eduQuiz.versionMismatch // the mismatch stays until the greeter restarts
                onClicked: { eduQuiz.dismiss(); root.focusMode() }
            }
        }
    }

    // session, host, power
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Math.round(24 * root.u)
        spacing: Math.round(12 * root.u)

        ComboBox {
            id: sessions
            Layout.preferredWidth: Math.round(260 * root.u)
            Layout.preferredHeight: Math.round(44 * root.u)
            model: sessionModel
            textRole: "name"
            currentIndex: sessionModel.lastIndex
            font.pixelSize: Math.round(16 * root.u * EduStyle.fontScale)
            visible: count > 1
            focusPolicy: Qt.StrongFocus
        }
        Text {
            text: sddm.hostName
            color: EduStyle.mutedTextColor
            font.pixelSize: Math.round(16 * root.u * EduStyle.fontScale)
            visible: text !== ""
        }
        Item { Layout.fillWidth: true }
        EduButton {
            text: qsTr("Restart")
            pixelSize: 16
            implicitHeight: Math.round(44 * root.u)
            visible: sddm.canReboot
            onClicked: sddm.reboot()
        }
        EduButton {
            text: qsTr("Shut down")
            pixelSize: 16
            implicitHeight: Math.round(44 * root.u)
            visible: sddm.canPowerOff
            onClicked: sddm.powerOff()
        }
    }
}
