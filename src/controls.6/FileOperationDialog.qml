import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

/**
 * Displays all active file operations and the operations waiting behind them.
 *
 * The active section remains fixed while the waiting and failed operations are
 * independently scrollable. The popup is non-modal and can be dismissed while
 * operations continue in the background.
 */
Maui.PopupPage
{
    id: control

    property Item anchorItem: null
    property int anchorMargin: Maui.Style.space.medium
    property bool positionUpdatePending: false
    property int dialogItemThreshold: 2
    property int dialogSizeThreshold: 1024 * 1024
    property int dialogOpenDelay: Maui.Style.units.longDuration * 2
    property string lastSummaryText: ""
    property bool replaceConflicts: false

    Component.onCompleted: synchronizeOperationModels()

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

    function synchronizeOperationModel(model, active)
    {
        const next = []
        const operations = FB.FileOperation.operations
        for (let i = 0; i < operations.length; ++i)
        {
            if (Boolean(operations[i].active) === active)
                next.push(operations[i])
        }

        for (let i = 0; i < next.length; ++i)
        {
            const item = next[i]
            if (i < model.count && model.get(i).operationId === item.id)
            {
                model.setProperty(i, "operationData", item)
                continue
            }

            let existingIndex = -1
            for (let j = i + 1; j < model.count; ++j)
            {
                if (model.get(j).operationId === item.id)
                {
                    existingIndex = j
                    break
                }
            }

            if (existingIndex >= 0)
                model.move(existingIndex, i, 1)
            else
                model.insert(i, { operationId: item.id, operationData: item })

            model.setProperty(i, "operationData", item)
        }

        while (model.count > next.length)
            model.remove(model.count - 1)
    }

    function synchronizeOperationModels()
    {
        synchronizeOperationModel(activeModel, true)
        synchronizeOperationModel(waitingModel, false)
    }

    function conflictItemName(path)
    {
        const original = String(path || "")
        const value = original.endsWith("/") ? original.slice(0, -1) : original
        const separator = value.lastIndexOf("/")
        return separator >= 0 ? value.slice(separator + 1) : value
    }

    function synchronizeConflictDialog()
    {
        const operations = FB.FileOperation.operations
        let conflict = null
        for (let i = 0; i < operations.length; ++i)
        {
            if (operations[i].conflictPending)
            {
                conflict = operations[i]
                break
            }
        }

        if (!conflict)
        {
            if (conflictDialog.visible)
            {
                conflictDialog.close()
            }
            return
        }

        const canRename = (conflict.kind === "copy" || conflict.kind === "move") && conflict.itemCount === 1
        conflictDialog.operationId = conflict.id
        conflictDialog.conflictDestination = conflict.conflictDestination
        conflictDialog.conflictKind = conflict.operation === "rename" ? "move" : conflict.kind
        conflictDialog.canRename = canRename
        if (!control.visible)
            control.open()
        if (!conflictDialog.visible)
            conflictDialog.open()
    }

    function schedulePosition()
    {
        if (!control.visible || control.positionUpdatePending || FB.FileOperation.operationCount === 0)
            return

        control.positionUpdatePending = true
        Qt.callLater(function()
        {
            control.positionUpdatePending = false
            if (control.visible && FB.FileOperation.operationCount > 0)
                control.positionNextToAnchor()
        })
    }

    function positionNextToAnchor()
    {
        if (!control.parent)
            return

        const margin = control.anchorMargin
        if (!control.anchorItem || !control.anchorItem.visible
                || control.anchorItem.width <= 0 || control.anchorItem.height <= 0)
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

    function dialogShouldOpenImmediately()
    {
        const operations = FB.FileOperation.operations
        for (let i = 0; i < operations.length; ++i)
        {
            const operation = operations[i]
            if (operation.status === "failed"
                    || operation.itemCount > control.dialogItemThreshold
                    || operation.totalBytes > control.dialogSizeThreshold)
                return true
        }
        return false
    }

    function scheduleDialogOpen()
    {
        if (control.visible || FB.FileOperation.operationCount === 0)
            return

        if (control.dialogShouldOpenImmediately())
        {
            dialogOpenTimer.stop()
            control.open()
            return
        }

        if (!dialogOpenTimer.running)
            dialogOpenTimer.start()
    }

    function operationTitle(item)
    {
        const source = item.sourceName || ""
        const count = item.itemCount
        const operation = item.operation

        if (item.status === "failed")
            return source.length > 0 && count === 1
                    ? i18nd("mauikitfilebrowsing", "Failed: %1", source)
                    : i18np("Failed: %1 item", "Failed: %1 items", count)

        if (item.status === "queued")
            return source.length > 0 && count === 1
                    ? i18nd("mauikitfilebrowsing", "Waiting: “%1”", source)
                    : i18np("Waiting: %1 item", "Waiting: %1 items", count)

        if (source.length > 0 && count === 1)
        {
            if (operation === "copy")
                return i18nd("mauikitfilebrowsing", "Copying “%1”", source)
            if (operation === "paste")
                return i18nd("mauikitfilebrowsing", "Pasting: “%1”", source)
            if (operation === "move")
                return i18nd("mauikitfilebrowsing", "Moving “%1”", source)
            if (operation === "rename")
                return i18nd("mauikitfilebrowsing", "Renaming “%1”", source)
            if (operation === "delete")
                return i18nd("mauikitfilebrowsing", "Deleting “%1”", source)
            if (operation === "trash")
                return i18nd("mauikitfilebrowsing", "Moving %1 to Trash", source)
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
        if (operation === "trash")
            return i18np("Moving %1 item to Trash", "Moving %1 items to Trash", count)
        return i18nd("mauikitfilebrowsing", "Working")
    }

    function operationStatus(item)
    {
        if (item.status === "paused")
            return i18nd("mauikitfilebrowsing", "Paused")
        if (item.status === "running" || item.status === "queued")
            return ""
        if (item.status === "failed")
            return i18nd("mauikitfilebrowsing", "Failed")
        return ""
    }

    function operationDetails(item)
    {
        if (item.status === "queued")
            return i18nd("mauikitfilebrowsing", "Waiting")

        if (item.status === "failed")
            return item.error || i18nd("mauikitfilebrowsing", "The operation failed")

        if (item.totalBytes <= 0)
            return item.status === "paused"
                    ? ""
                    : i18nd("mauikitfilebrowsing", "Preparing operation…")

        let details = i18nd("mauikitfilebrowsing", "%1 of %2", Maui.Handy.formatSize(item.processedBytes), Maui.Handy.formatSize(item.totalBytes))
        if (item.speed > 0)
        {
            details += i18nd("mauikitfilebrowsing", " · %1/s", Maui.Handy.formatSize(item.speed))
            if (item.processedBytes < item.totalBytes)
            {
                const left = Math.ceil((item.totalBytes - item.processedBytes) / item.speed)
                details += i18nd("mauikitfilebrowsing", " · %1 left", Maui.Handy.formatTime(left))
            }
        }
        return details
    }

    function operationDestination(item)
    {
        return item.destination && item.destination.length > 0
                ? i18nd("mauikitfilebrowsing", "Destination: %1", item.destination)
                : ""
    }

    function summaryText()
    {
        if (FB.FileOperation.activeCount > 0 && FB.FileOperation.queuedCount > 0)
            return i18nd("mauikitfilebrowsing", "%1 active · %2 waiting", FB.FileOperation.activeCount, FB.FileOperation.queuedCount)
        if (FB.FileOperation.activeCount > 0)
            return i18np("%1 active", "%1 active", FB.FileOperation.activeCount)
        if (FB.FileOperation.queuedCount > 0)
            return i18np("%1 waiting", "%1 waiting", FB.FileOperation.queuedCount)
        return ""
    }

    Timer
    {
        id: dialogOpenTimer
        interval: control.dialogOpenDelay
        repeat: false

        onTriggered:
        {
            if (!control.visible && FB.FileOperation.operationCount > 0)
                control.open()
        }
    }

    onAboutToShow: positionNextToAnchor()
    onOpened: schedulePosition()
    onClosed:
    {
        control.synchronizeOperationModels()
        control.lastSummaryText = ""
    }
    onWidthChanged: schedulePosition()
    onHeightChanged: schedulePosition()

    Connections
    {
        target: control.anchorItem

        function onXChanged() { control.schedulePosition() }
        function onYChanged() { control.schedulePosition() }
        function onVisibleChanged() { control.schedulePosition() }
        function onWidthChanged() { control.schedulePosition() }
        function onHeightChanged() { control.schedulePosition() }
    }

    Connections
    {
        target: control.parent

        function onWidthChanged() { control.schedulePosition() }
        function onHeightChanged() { control.schedulePosition() }
    }

    stack: ColumnLayout
    {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: Maui.Style.space.big
        Layout.topMargin: Maui.Style.space.medium
        spacing: Maui.Style.space.small

        ListModel { id: activeModel }
        ListModel { id: waitingModel }

        Maui.ScrollColumn
        {
            id: activeOperationsScroll
            visible: activeModel.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: activeSection.implicitHeight
            padding: 0
            topPadding: Maui.Style.space.small
            bottomPadding: Maui.Style.space.small
            spacing: Maui.Style.space.small
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            flickable.interactive: true

            Rectangle
            {
                id: activeSection
                visible: activeModel.count > 0
                color: Maui.Theme.backgroundColor
                clip: true
                z: 1
                Layout.fillWidth: true
                Layout.preferredHeight: activeContent.implicitHeight + Maui.Style.space.small * 2
                implicitHeight: activeContent.implicitHeight + Maui.Style.space.small * 2

                ColumnLayout
                {
                    id: activeContent
                    anchors.fill: parent
                    anchors.margins: Maui.Style.space.small
                    spacing: Maui.Style.space.small

                    Repeater
                    {
                        model: activeModel

                        delegate: ColumnLayout
                        {
                            required property int index
                            required property var operationData
                            Layout.fillWidth: true
                            Layout.topMargin: Maui.Style.space.small
                            Layout.bottomMargin: Maui.Style.space.small
                            spacing: Maui.Style.space.small

                            Maui.ListItemTemplate
                            {
                                Layout.fillWidth: true
                                text1: control.operationTitle(operationData)
                                text2: control.operationDestination(operationData)
                                text3: control.operationStatus(operationData)
                                iconVisible: false
                                iconSizeHint: Maui.Style.iconSizes.medium
                                label3.font: Maui.Style.defaultFont
                                label3.opacity: 1
                            }

                            RowLayout
                            {
                                Layout.fillWidth: true

                                ProgressBar
                                {
                                    visible: operationData.totalBytes > 0
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    value: operationData.progress
                                }

                                ToolButton
                                {
                                    visible: operationData.pauseSupported
                                    enabled: operationData.active
                                    display: ToolButton.IconOnly
                                    icon.name: operationData.paused ? "media-playback-start" : "media-playback-pause"
                                    text: operationData.paused
                                          ? i18nd("mauikitfilebrowsing", "Resume transfer")
                                          : i18nd("mauikitfilebrowsing", "Pause transfer")
                                    onClicked: operationData.paused
                                               ? FB.FileOperation.resumeOperation(operationData.id)
                                               : FB.FileOperation.pauseOperation(operationData.id)
                                    ToolTip.visible: hovered
                                    ToolTip.text: text
                                }

                                ToolButton
                                {
                                    visible: operationData.canCancel
                                    enabled: operationData.canCancel
                                    display: ToolButton.IconOnly
                                    icon.name: operationData.active ? "dialog-cancel" : "edit-clear"
                                    text: operationData.active
                                          ? i18nd("mauikitfilebrowsing", "Cancel transfer")
                                          : i18nd("mauikitfilebrowsing", "Cancel queued transfer")
                                    onClicked: FB.FileOperation.cancelOperation(operationData.id)
                                    ToolTip.visible: hovered
                                    ToolTip.text: text
                                }
                            }

                            Label
                            {
                                Layout.fillWidth: true
                                text: control.operationDetails(operationData)
                                elide: Text.ElideRight
                            }

                            Maui.Separator
                            {
                                visible: index < activeModel.count - 1
                                Layout.fillWidth: true
                                weight: Maui.Separator.Weight.Light
                            }
                        }
                    }
                }
            }
        }

        RowLayout
        {
            visible: FB.FileOperation.activeCount > 0 || FB.FileOperation.queuedCount > 0 || control.lastSummaryText.length > 0
            Layout.fillWidth: true

            Label
            {
                Layout.fillWidth: true
                text: FB.FileOperation.operationCount > 0 ? control.summaryText() : control.lastSummaryText
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
                onClicked: FB.FileOperation.cancelAll()
                ToolTip.visible: hovered
                ToolTip.text: text
            }
        }

        Maui.ScrollColumn
        {
            visible: waitingModel.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: implicitHeight
            padding: 0
            topPadding: Maui.Style.space.small
            bottomPadding: Maui.Style.space.small
            spacing: Maui.Style.space.small
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            flickable.interactive: true

            Repeater
            {
                model: waitingModel

                delegate: Item
                {
                    required property int index
                    required property var operationData
                    Layout.fillWidth: true
                    Layout.topMargin: Maui.Style.space.small
                    Layout.bottomMargin: Maui.Style.space.small
                    implicitHeight: operationContent.implicitHeight

                    ColumnLayout
                    {
                        id: operationContent
                        width: parent.width
                        spacing: Maui.Style.space.small

                        Maui.ListItemTemplate
                        {
                            Layout.fillWidth: true
                            text1: control.operationTitle(operationData)
                            text2: control.operationDestination(operationData)
                            text3: control.operationStatus(operationData)
                            iconVisible: false
                            iconSizeHint: Maui.Style.iconSizes.small
                            label3.font: Maui.Style.defaultFont
                            label3.opacity: 1
                        }

                    RowLayout
                    {
                        visible: operationData.status === "failed"
                        Layout.fillWidth: true

                        Label
                        {
                            Layout.fillWidth: true
                            text: control.operationDetails(operationData)
                            color: Maui.Theme.negativeTextColor
                            wrapMode: Text.WordWrap
                        }

                        ToolButton
                        {
                            enabled: operationData.canClear
                            display: ToolButton.IconOnly
                            icon.name: "edit-clear"
                            text: i18nd("mauikitfilebrowsing", "Clear operation")
                            onClicked: FB.FileOperation.clearOperation(operationData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: text
                        }
                    }

                    Maui.Separator
                    {
                        visible: index < waitingModel.count - 1
                        Layout.fillWidth: true
                        weight: Maui.Separator.Weight.Light
                    }
                    }
                }
            }
        }
    }

    Maui.InfoDialog
    {
        id: conflictDialog
        property int operationId: -1
        property string conflictDestination: ""
        property string conflictKind: ""
        property bool canRename: false
        title: i18nd("mauikitfilebrowsing", "File already exists")
        message: i18nd("mauikitfilebrowsing", "A file named “%1” already exists here.", control.conflictItemName(conflictDestination))
        template.iconSource: "dialog-warning"
        standardButtons: Dialog.NoButton

        onOpened:
        {
            conflictActionButton.forceActiveFocus()
        }

        onAccepted:
        {
            const id = operationId
            close()
            FB.FileOperation.resolveConflict(id, true)
        }

        onRejected:
        {
            const id = operationId
            close()
            FB.FileOperation.resolveConflict(id, false)
        }

        RowLayout
        {
            Layout.fillWidth: true
            Layout.topMargin: Maui.Style.space.medium
            spacing: Maui.Style.space.small

            Button
            {
                id: conflictActionButton
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                focus: true
                text: replaceConflicts || !conflictDialog.canRename ? i18nd("mauikitfilebrowsing", "Replace") : i18nd("mauikitfilebrowsing", "Rename")
                Maui.Controls.status: replaceConflicts || !conflictDialog.canRename ? Maui.Controls.Negative : Maui.Controls.Positive
                highlighted: hovered || activeFocus
                onClicked:
                {
                    const id = conflictDialog.operationId
                    if (replaceConflicts || !conflictDialog.canRename)
                    {
                        conflictDialog.accept()
                        return
                    }

                    const name = control.conflictItemName(conflictDialog.conflictDestination)
                    const suggestedName = FB.FileOperation.conflictRenameSuggestion(id)
                    close()
                    const dialog = renameConflictDialogComponent.createObject(control, {
                        operationId: id,
                        initialName: suggestedName || name
                    })
                    dialog.open()
                }
            }

            Button
            {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: i18nd("mauikitfilebrowsing", "Cancel")
                onClicked: conflictDialog.reject()
            }
        }
    }

    Component
    {
        id: renameConflictDialogComponent

        Maui.InputDialog
        {
            property int operationId: -1
            property string initialName: ""

            title: i18nd("mauikitfilebrowsing", "Rename")
            message: i18nd("mauikitfilebrowsing", "Enter a new name for the file.")
            template.iconSource: "edit-rename"
            textEntry.text: initialName

            onOpened: {}
            onFinished: (name) =>
            {
                Qt.callLater(() => FB.FileOperation.renameConflict(operationId, name))
            }
            onRejected: { conflictDialog.open() }
        }
    }

    Connections
    {
        target: FB.FileOperation

        function onOperationAdded(id)
        {
            Qt.callLater(control.scheduleDialogOpen)
        }

        function onOperationsChanged()
        {
            if (FB.FileOperation.operationCount === 0)
            {
                dialogOpenTimer.stop()
                control.synchronizeConflictDialog()
                if (control.visible)
                {
                    control.close()
                    return
                }

                control.synchronizeOperationModels()
                return
            }

            control.synchronizeOperationModels()
            control.synchronizeConflictDialog()
            control.lastSummaryText = control.summaryText()

            if (!control.visible && dialogOpenTimer.running && control.dialogShouldOpenImmediately())
            {
                dialogOpenTimer.stop()
                control.open()
                return
            }

            if (control.visible)
                control.schedulePosition()
        }

        function onFinished(success)
        {
            if (FB.FileOperation.operationCount === 0)
            {
                dialogOpenTimer.stop()
                control.close()
            }
            else if (!control.visible && (!success || control.dialogShouldOpenImmediately()))
            {
                dialogOpenTimer.stop()
                control.open()
            }
        }
    }
}
