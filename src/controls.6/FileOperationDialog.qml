import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

Maui.PopupPage
{
    id: control

    property int itemCount: 0
    property string operationName: ""
    property string destinationName: ""
    property string errorMessage: ""

    widthHint: 0.9
    maxWidth: 400
    persistent: true
    headBar.visible: false
    title: i18nd("mauikitfilebrowsing", "File operation")

    actions: [
        Action
        {
            text: i18nd("mauikitfilebrowsing", "Cancel")
            onTriggered:
            {
                FB.FileOperation.cancel()
                control.close()
            }
        }
    ]

    Maui.ListItemTemplate
    {
        Layout.fillWidth: true
        text1: {
            if (control.operationName === "copy")
                return i18np("Copying %1 item", "Copying %1 items", control.itemCount)
            if (control.operationName === "paste")
                return i18np("Pasting %1 item", "Pasting %1 items", control.itemCount)
            if (control.operationName === "rename")
                return i18np("Renaming %1 item", "Renaming %1 items", control.itemCount)
            if (control.operationName === "delete")
                return i18np("Deleting %1 item", "Deleting %1 items", control.itemCount)
            if (control.operationName === "move")
                return i18np("Moving %1 item", "Moving %1 items", control.itemCount)
            return i18nd("mauikitfilebrowsing", "Working")
        }
        text2: control.operationName !== "delete" && control.destinationName.length > 0
               ? i18nd("mauikitfilebrowsing", "Destination: %1", control.destinationName)
               : ""
        iconSizeHint: Maui.Style.iconSizes.medium
    }

    ProgressBar
    {
        Layout.fillWidth: true
        from: 0
        to: 100
        indeterminate: false
        value: FB.FileOperation.progress
    }

    Label
    {
        Layout.fillWidth: true
        text: FB.FileOperation.totalBytes > 0
              ? i18nd("mauikitfilebrowsing", "%1 of %2", Maui.Handy.formatSize(FB.FileOperation.processedBytes), Maui.Handy.formatSize(FB.FileOperation.totalBytes))
              : i18nd("mauikitfilebrowsing", "Preparing transfer…")
        elide: Text.ElideRight
    }

    Label
    {
        Layout.fillWidth: true
        text: FB.FileOperation.speed > 0
              ? i18nd("mauikitfilebrowsing", "%1/s", Maui.Handy.formatSize(FB.FileOperation.speed))
              : i18nd("mauikitfilebrowsing", "Calculating transfer speed…")
        elide: Text.ElideRight
    }

    Label
    {
        Layout.fillWidth: true
        visible: control.errorMessage.length > 0
        text: control.errorMessage
        wrapMode: Text.WordWrap
    }

    Connections
    {
        target: FB.FileOperation

        function onStarted(operation, itemCount, destination)
        {
            control.operationName = operation
            control.itemCount = itemCount
            control.destinationName = destination
            control.errorMessage = ""
            control.open()
        }

        function onFinished(success, errorMessage)
        {
            if (success) {
                control.close()
            } else if (control.visible) {
                control.errorMessage = errorMessage
            }
        }
    }
}
