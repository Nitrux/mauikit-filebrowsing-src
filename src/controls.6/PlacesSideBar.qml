import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

Loader
{
    id: control

    asynchronous: true
    active: (enabled && visible) || item
    Keys.enabled: false
    focus: false

    readonly property var list: item && item.list ? item.list : null
    property string currentPath
    property var hiddenPaths: []

    signal placeClicked(string path)

    function isRootPath(path)
    {
        const value = String(path)
        return value === "/" || value === "file:///" || value === "file:/"
    }

    function isStorageSection(type)
    {
        const value = String(type)
        if (value === i18nd("mauikitfilebrowsing", "Drives") || value === i18n("Storage"))
            return true

        const placesList = control.list
        if (!placesList)
            return false

        for (let i = 0; i < placesList.count; ++i)
        {
            const place = placesList.get(i)
            if (String(place.type) === value && isRootPath(place.path))
                return true
        }

        return false
    }

    function placeLabel(path, label, type)
    {
        return isStorageSection(type) && isRootPath(path) ? i18n("Root") : String(label)
    }

    function placeIcon(path, iconName, label, type, isDeviceEntry)
    {
        const value = String(path)
        const text = String(label)
        const sectionType = String(type)

        if (isDeviceEntry && (isStorageSection(sectionType) || sectionType === i18nd("mauikitfilebrowsing", "Removable")))
            return sectionType === i18nd("mauikitfilebrowsing", "Removable") ? "drive-removable-media" : "drive-harddisk"
        if (isRootPath(value))
            return "folder-red"
        if ((value.startsWith("/") || value.startsWith("file:///")) && text.startsWith("/"))
            return "folder"
        return iconName === undefined || iconName === null ? "" : String(iconName)
    }

    function isExternalDeviceSection(type)
    {
        return String(type) === i18nd("mauikitfilebrowsing", "Removable")
    }

    function isPathHidden(path)
    {
        const candidate = path ? path.toString() : ""
        for (let i = 0; i < hiddenPaths.length; ++i)
        {
            const hiddenPath = hiddenPaths[i]
            if ((hiddenPath ? hiddenPath.toString() : "") === candidate)
                return true
        }
        return false
    }

    sourceComponent: Pane
    {
        id: sideBarPane
        readonly property alias list: placesList

        padding: 0
        focus: false
        clip: true
        Maui.Theme.colorSet: Maui.Theme.Window

        background: Rectangle
        {
            color: Maui.Theme.alternateBackgroundColor
            radius: Maui.Style.radiusV
            border.color: Maui.Theme.backgroundColor
        }

        contentItem: Maui.ListBrowser
        {
            id: listBrowser
            verticalScrollBarPolicy: ScrollBar.AlwaysOff
            focus: false
            focusPolicy: Qt.NoFocus
            Keys.enabled: false

            readonly property alias list: placesList

            model: Maui.BaseModel
            {
                list: FB.PlacesList
                {
                    id: placesList
                    groups: [
                        FB.FMList.BOOKMARKS_PATH,
                        FB.FMList.REMOTE_PATH,
                        FB.FMList.REMOVABLE_PATH,
                        FB.FMList.DRIVES_PATH]
                }
            }

            flickable.header: ColumnLayout
            {
                width: parent.width
                spacing: Maui.Style.space.small

                Loader
                {
                    asynchronous: true
                    Layout.fillWidth: true
                    Layout.bottomMargin: Maui.Style.space.medium
                    height: implicitHeight + listBrowser.spacing * 2

                    sourceComponent: GridLayout
                    {
                        rows: 3
                        columns: 3
                        columnSpacing: Maui.Style.defaultSpacing
                        rowSpacing: Maui.Style.defaultSpacing

                        Repeater
                        {
                            model: Maui.BaseModel
                            {
                                list: FB.PlacesList
                                {
                                    groups: [FB.FMList.QUICK_PATH, FB.FMList.PLACES_PATH]
                                }
                            }

                            delegate: Maui.GridBrowserDelegate
                            {
                                Maui.Theme.colorSet: Maui.Theme.Button
                                Maui.Theme.inherit: false

                                readonly property bool hiddenPlace: control.isPathHidden(model.path)
                                Layout.preferredHeight: hiddenPlace ? 0 : Math.min(50, width)
                                Layout.preferredWidth: hiddenPlace ? 0 : 50
                                Layout.fillWidth: !hiddenPlace
                                Layout.fillHeight: !hiddenPlace
                                flat: false
                                visible: !hiddenPlace
                                isCurrentItem: control.currentPath === model.path
                                iconSource: control.placeIcon(model.path, model.icon, model.label, model.type, false)
                                iconSizeHint: Maui.Style.iconSize
                                label1.text: model.label
                                labelsVisible: false
                                tooltipText: model.label
                                onClicked: control.placeClicked(model.path)
                            }
                        }
                    }
                }
            }

            delegate: Maui.ListDelegate
            {
                contentItem.z: 1
                width: listBrowser.availableWidth
                iconSize: Maui.Style.iconSize
                labelVisible: true
                iconVisible: true
                isCurrentItem: control.currentPath === model.path
                label: control.placeLabel(model.path, model.label, model.type)
                tooltipText: label
                iconName: control.placeIcon(model.path, model.icon, model.label, model.type, placesList.isDevice(index))

                template.content: ToolButton
                {
                    visible: placesList.isDevice(index) && control.isExternalDeviceSection(model.type)
                    icon.name: placesList.setupNeeded(index) ? "media-mount" : "media-eject"
                    display: AbstractButton.IconOnly
                    flat: true
                    icon.height: Maui.Style.iconSizes.small
                    icon.width: Maui.Style.iconSizes.small

                    onClicked:
                    {
                        if (placesList.setupNeeded(index))
                            placesList.requestSetup(index)
                        else
                        {
                            unmountDialog.deviceIndex = index
                            unmountDialog.deviceLabel = model.label
                            unmountDialog.open()
                        }
                    }
                }

                onClicked: control.placeClicked(model.path)
            }

            section.property: "type"
            section.criteria: ViewSection.FullString
            section.delegate: Maui.SectionHeader
            {
                width: listBrowser.availableWidth
                text1: control.isStorageSection(section) ? i18n("Storage") : String(section)
                label1.font.weight: Font.Bold
                label1.font.pixelSize: 14
                label1.opacity: 1.0
                label2.visible: false
            }
        }

        Maui.InfoDialog
        {
            id: unmountDialog
            property int deviceIndex: -1
            property string deviceLabel: ""

            title: i18n("Unmount")
            message: i18n("Are you sure you want to unmount %1?", deviceLabel)
            standardButtons: Dialog.Discard | Dialog.Cancel

            Component.onCompleted: standardButton(Dialog.Discard).text = i18n("Unmount")
            onDiscarded:
            {
                sideBarPane.list.requestTeardown(deviceIndex)
                close()
            }
            onRejected: close()
        }
    }
}
