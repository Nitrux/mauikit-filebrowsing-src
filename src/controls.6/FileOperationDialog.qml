import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

/**
 * Displays the active file operation and every queued copy request.
 *
 * The popup is non-modal and can be dismissed while the transfer continues.
 * The FileOperationIndicator provides the persistent way to reopen it.
 */
Maui.PopupPage
{
    id: control

    property int itemCount: 0
    property bool dismissOnCancel: false
    property string operationName: ""
    property string destinationName: ""
    property string errorMessage: ""
    property Item anchorItem: null
    property int anchorMargin: Maui.Style.space.medium

    anchors.centerIn: undefined
    widthHint: 0.9
    maxWidth: Maui.Style.units.gridUnit * 30
    maxHeight: Maui.Style.units.gridUnit * 32
    height: Math.min(implicitHeight, maxHeight)
    modal: false
    persistent: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition
    {
        NumberAnimation
        {
            property: "opacity"
            from: 0
            to: 1
            duration: Maui.Style.units.shortDuration
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition
    {
        NumberAnimation
        {
            property: "opacity"
            from: 1
            to: 0
            duration: Maui.Style.units.shortDuration
            easing.type: Easing.InCubic
        }
    }
    headBar.visible: false
    closeButtonVisible: false
    scrollView.rightPadding: scrollView.leftPadding

    function positionNextToAnchor()
    {
        if (!control.parent)
            return

        const margin = control.anchorMargin
        if (!control.anchorItem)
        {
            control.x = Math.max(margin, (control.parent.width - control.width) / 2)
            control.y = Math.max(margin, (control.parent.height - control.height) / 2)
            return
        }

        const anchorPosition = control.anchorItem.mapToItem(control.parent, 0, 0)
        const centeredX = anchorPosition.x + (control.anchorItem.width - control.width) / 2
        const belowY = anchorPosition.y + control.anchorItem.height + margin
        const aboveY = anchorPosition.y - control.height - margin

        control.x = Math.max(margin, Math.min(control.parent.width - control.width - margin, centeredX))
        control.y = belowY + control.height <= control.parent.height - margin
                     ? belowY
                     : Math.max(margin, aboveY)
    }

    function activeTitle()
    {
        const source = FB.FileOperation.sourceName
        const count = control.itemCount
        const operation = FB.FileOperation.operation

        if (source.length > 0 && count === 1)
        {
            if (operation === "copy")
                return i18nd("mauikitfilebrowsing", "Copying \"%1\"", source)
            if (operation === "paste")
                return i18nd("mauikitfilebrowsing", "Pasting \"%1\"", source)
            if (operation === "move")
                return i18nd("mauikitfilebrowsing", "Moving \"%1\"", source)
            if (operation === "rename")
                return i18nd("mauikitfilebrowsing", "Renaming \"%1\"", source)
            if (operation === "delete")
                return i18nd("mauikitfilebrowsing", "Deleting \"%1\"", source)
        }

        if (operation === "copy")
            return i18np("Copying %1 item", "Copying %1 items", count)
        if (operation === "paste")
            return i18np("Pasting %1 item", "Pasting %1 items", count)
        if (operation === "move")
            return i18np("Moving %1 item", "Moving %1 items", count)
        if (operation === "rename")
            return i18np("Renaming %1 item", "Renaming %1 items", count)
        if (operation === "delete")
            return i18np("Deleting %1 item", "Deleting %1 items", count)
        return i18nd("mauikitfilebrowsing", "Working")
    }

    function activeDetails()
    {
        if (FB.FileOperation.totalBytes <= 0)
            return i18nd("mauikitfilebrowsing", "Preparing transfer…")

        let details = i18nd("mauikitfilebrowsing", "%1 of %2", Maui.Handy.formatSize(FB.FileOperation.processedBytes), Maui.Handy.formatSize(FB.FileOperation.totalBytes))
        if (FB.FileOperation.speed > 0)
        {
            details += i18nd("mauikitfilebrowsing", " · %1/s", Maui.Handy.formatSize(FB.FileOperation.speed))
            if (FB.FileOperation.processedBytes < FB.FileOperation.totalBytes)
            {
                const remaining = Math.ceil((FB.FileOperation.totalBytes - FB.FileOperation.processedBytes) / FB.FileOperation.speed)
                details += i18nd("mauikitfilebrowsing", " · %1 left", Maui.Handy.formatTime(remaining))
            }
        }
        return details
    }

    function queuedTitle(item)
    {
        return item.sourceName && item.sourceName.length > 0
               ? i18nd("mauikitfilebrowsing", "Waiting: \"%1\"", item.sourceName)
               : i18np("Waiting: %1 item", "Waiting: %1 items", item.itemCount)
    }

    onOpened: Qt.callLater(positionNextToAnchor)
    onWidthChanged: if (visible) positionNextToAnchor()
    onHeightChanged: if (visible) positionNextToAnchor()

    Connections
    {
        target: control.anchorItem

        function onXChanged() { if (control.visible) control.positionNextToAnchor() }
        function onYChanged() { if (control.visible) control.positionNextToAnchor() }
        function onWidthChanged() { if (control.visible) control.positionNextToAnchor() }
        function onHeightChanged() { if (control.visible) control.positionNextToAnchor() }
    }

    Connections
    {
        target: control.parent

        function onWidthChanged() { if (control.visible) control.positionNextToAnchor() }
        function onHeightChanged() { if (control.visible) control.positionNextToAnchor() }
    }

    stack: ColumnLayout
    {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: Maui.Style.space.big
        spacing: Maui.Style.space.small

        Rectangle
        {
            id: activeSection
            color: Maui.Theme.backgroundColor
            clip: true
            z: 1
            Layout.fillWidth: true
            Layout.minimumHeight: activeContent.implicitHeight
            Layout.preferredHeight: activeContent.implicitHeight
            implicitHeight: activeContent.implicitHeight

            ColumnLayout
            {
                id: activeContent
                anchors.fill: parent
                spacing: Maui.Style.space.small

            Maui.ListItemTemplate
            {
                visible: FB.FileOperation.running
                Layout.fillWidth: true
                text1: control.activeTitle()
                text2: FB.FileOperation.destination.length > 0
                       ? i18nd("mauikitfilebrowsing", "Destination: %1", FB.FileOperation.destination)
                       : ""
                text3: FB.FileOperation.paused
                       ? i18nd("mauikitfilebrowsing", "Paused")
                       : i18nd("mauikitfilebrowsing", "%1%", FB.FileOperation.progress)
                iconVisible: false
                iconSizeHint: Maui.Style.iconSizes.medium
                label3.font: Maui.Style.defaultFont
                label3.opacity: 1
            }

            RowLayout
            {
                visible: FB.FileOperation.running
                Layout.fillWidth: true

                ProgressBar
                {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: FB.FileOperation.progress
                }

                ToolButton
                {
                    display: ToolButton.IconOnly
                    enabled: FB.FileOperation.running
                    visible: FB.FileOperation.pauseSupported
                    icon.name: FB.FileOperation.paused ? "media-playback-start" : "media-playback-pause"
                    text: FB.FileOperation.paused
                          ? i18nd("mauikitfilebrowsing", "Resume transfer")
                          : i18nd("mauikitfilebrowsing", "Pause transfer")
                    onClicked: FB.FileOperation.paused ? FB.FileOperation.resume() : FB.FileOperation.pause()
                    ToolTip.visible: hovered
                    ToolTip.text: text
                }

                ToolButton
                {
                    display: ToolButton.IconOnly
                    enabled: FB.FileOperation.running
                    icon.name: "dialog-cancel"
                    text: i18nd("mauikitfilebrowsing", "Cancel transfer")
                    onClicked:
                    {
                        control.dismissOnCancel = FB.FileOperation.queuedCount === 0
                        FB.FileOperation.cancel()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: text
                }
            }

            Label
            {
                visible: FB.FileOperation.running
                Layout.fillWidth: true
                text: control.activeDetails()
                elide: Text.ElideRight
            }

            Label
            {
                visible: control.errorMessage.length > 0
                Layout.fillWidth: true
                text: control.errorMessage
                wrapMode: Text.WordWrap
            }

            RowLayout
            {
                visible: FB.FileOperation.queuedCount > 0
                Layout.fillWidth: true

                Label
                {
                    text: i18np("%1 item waiting", "%1 items waiting", FB.FileOperation.queuedCount)
                    Layout.fillWidth: true
                }

                ToolButton
                {
                    display: ToolButton.TextOnly
                    Maui.Theme.textColor: "#cdd6f4"
                    Maui.Theme.highlightedTextColor: "#cdd6f4"
                    text: i18nd("mauikitfilebrowsing", "Cancel All")
                    background: Rectangle
                    {
                        radius: Math.round(Maui.Style.radiusV * 0.6)
                        color: parent.down ? Qt.darker(Maui.Theme.negativeBackgroundColor, 1.12) : Maui.Theme.negativeBackgroundColor
                    }
                    onClicked:
                    {
                        control.dismissOnCancel = true
                        FB.FileOperation.cancelAll()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: text
                }
            }

            Maui.Separator
            {
                visible: FB.FileOperation.queuedCount > 0
                Layout.fillWidth: true
                weight: Maui.Separator.Weight.Light
            }
        }
    }

        Maui.ScrollColumn
        {
            visible: FB.FileOperation.queuedCount > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            padding: 0
            spacing: Maui.Style.space.small
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            flickable.interactive: true

            Repeater
            {
                model: FB.FileOperation.queuedOperations

                delegate: ColumnLayout
                {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Maui.Style.space.tiny

                    Maui.ListItemTemplate
                    {
                        Layout.fillWidth: true
                        text1: control.queuedTitle(modelData)
                        text2: i18nd("mauikitfilebrowsing", "Destination: %1", modelData.destination)
                        text3: i18nd("mauikitfilebrowsing", "Queue position %1", index + 1)
                        iconVisible: false
                        iconSizeHint: Maui.Style.iconSizes.small
                    }
                }
            }
        }


    }

    Connections
    {
        target: FB.FileOperation

        function onStarted(operation, itemCount, destination)
        {
            control.dismissOnCancel = false
            control.operationName = operation
            control.itemCount = itemCount
            control.destinationName = destination
            control.errorMessage = ""
            if (!control.visible)
                control.open()
            else
                control.positionNextToAnchor()
        }

        function onFinished(success, errorMessage)
        {
            if (success)
            {
                if (FB.FileOperation.queuedCount === 0)
                    control.close()
            }
            else if (control.dismissOnCancel && FB.FileOperation.queuedCount === 0)
            {
                control.dismissOnCancel = false
                control.close()
            }
            else
            {
                control.dismissOnCancel = false
                control.errorMessage = errorMessage
                if (!control.visible)
                    control.open()
            }
        }
    }
}
