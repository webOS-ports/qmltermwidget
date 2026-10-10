import QtQuick 2.0
import QMLTermWidget 2.0

Item {
    id: scrollbar

    property QMLTermWidget terminal

    property int value: terminal ? terminal.scrollbarCurrentValue : 0
    property int minimum: terminal ? terminal.scrollbarMinimum : 0
    property int maximum: terminal ? terminal.scrollbarMaximum : 0
    property int lines: terminal ? terminal.lines : 0
    property int totalLines: lines + maximum

    // Lines the bar stands for; 0 until there is a terminal with content
    readonly property int _span: totalLines - minimum

    anchors.right: terminal ? terminal.right : undefined

    visible: terminal !== null && _span > 0
    opacity: 0.0

    height: visible ? terminal.height * (lines / _span) : 0
    y: visible ? (terminal.height / _span) * (value - minimum) : 0

    Behavior on opacity {
        NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
    }

    function showScrollbar() {
        opacity = 1.0;
        hideTimer.restart();
    }

    Connections {
        target: scrollbar.terminal
        // Emitted when the view scrolls and when the history grows
        function onScrollbarParamsChanged() { scrollbar.showScrollbar() }
    }

    Timer {
        id: hideTimer
        onTriggered: scrollbar.opacity = 0
    }
}
