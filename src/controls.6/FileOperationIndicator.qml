import QtQuick
import QtQuick.Controls

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

/**
 * A persistent headbar entry for the file-operation transfer center.
 */
ToolButton
{
    id: control

    property Popup transferCenter: null
    property bool transferCenterWasOpenOnPress: false

    readonly property bool hasTransfers: FB.FileOperation.operationCount > 0
    readonly property int transferCount: FB.FileOperation.operationCount

    visible: hasTransfers
    enabled: hasTransfers && transferCenter !== null
    display: ToolButton.IconOnly
    icon.name: FB.FileOperation.paused ? "media-playback-start" : "edit-copy"
    text: i18np("%1 file operation", "%1 file operations", transferCount)

    Maui.Controls.badgeText: transferCount > 1 ? String(transferCount) : ""

    onPressed:
    {
        transferCenterWasOpenOnPress = transferCenter ? transferCenter.opened : false
    }

    onClicked:
    {
        const wasOpen = transferCenterWasOpenOnPress
        transferCenterWasOpenOnPress = false

        if (!transferCenter)
            return

        if (wasOpen)
        {
            if (transferCenter.opened)
                transferCenter.close()
            return
        }

        if (!transferCenter.opened)
        {
            transferCenter.open()
            transferCenter.forceActiveFocus()
        }
    }

    ToolTip.visible: hovered
    ToolTip.text: text
}
