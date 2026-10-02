// SPDX-License-Identifier: MIT
pragma Singleton
import QtQuick

// Colours and sizes shared by the theme; colours can be overridden in theme.conf.
QtObject {
    function setting(key, fallback) {
        var value = config[key]
        return (value === undefined || value === "") ? fallback : value
    }

    readonly property color accent: setting("accent", "#1a8577")
    readonly property color accentText: setting("accentText", "#ffffff")
    readonly property color accentDark: Qt.darker(accent, 1.25)
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, 0.14)
    readonly property color textColor: setting("textColor", "#232629")
    readonly property color mutedTextColor: setting("mutedTextColor", "#565d64")
    readonly property color cardColor: setting("cardColor", "#ffffff")
    readonly property color backgroundTop: setting("backgroundTop", "#d6ecf8")
    readonly property color backgroundBottom: setting("backgroundBottom", "#f3f9ff")
    readonly property color correctColor: setting("correctColor", "#22803f")
    readonly property color wrongColor: setting("wrongColor", "#c2481f")
    readonly property color borderColor: "#c9d3dc"
    readonly property string background: setting("background", "")
    readonly property real fontScale: parseFloat(setting("fontScale", "1")) || 1

    // Size unit: 1 at 1280x720, never below 0.7 so 1024x600 stays usable
    function unit(width, height) {
        return Math.max(0.7, Math.min(width / 1280, height / 720))
    }

    readonly property var avatarColors: ["#e76f51", "#2a9d8f", "#e9a23b", "#5b8def", "#9b6bd1", "#3fa66b"]

    function avatarColor(name) {
        var hash = 0
        for (var i = 0; i < name.length; ++i)
            hash = (hash * 31 + name.charCodeAt(i)) % 9973
        return avatarColors[hash % avatarColors.length]
    }

    // The user model hands every user without a picture the shared default face
    // (<faces dir>/.face.icon); a personal ~/.face.icon is in the user's home directory
    function ownIcon(icon, homeDir) {
        if (icon === "")
            return ""
        var path = icon.replace(/^file:\/\//, "")
        var slash = path.lastIndexOf("/")
        if (path.slice(slash + 1) !== ".face.icon")
            return icon
        return (homeDir !== "" && path.slice(0, slash) === homeDir) ? icon : ""
    }

    function initials(text) {
        var words = text.trim().split(/\s+/)
        if (words[0] === "")
            return "?"
        var result = words[0].charAt(0)
        if (words.length > 1)
            result += words[words.length - 1].charAt(0)
        return result.toUpperCase()
    }
}
