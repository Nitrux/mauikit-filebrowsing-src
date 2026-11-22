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
    property var groups: [
        FB.FMList.BOOKMARKS_PATH,
        FB.FMList.REMOTE_PATH,
        FB.FMList.REMOVABLE_PATH,
        FB.FMList.DRIVES_PATH]
    property var quickPlaces: null
    property bool quickPlacesEnabled: true
    property bool showStoragePartitions: true
    property bool mountRemovableOnClick: false
    property string pendingMountUdi
    property string pendingMountFallbackPath
    property int pendingMountModifiers: Qt.NoModifier
    property var deviceReturnPaths: ({})
    property var deviceMountPaths: ({})
    property string currentPath
    property string lastObservedPath
    property string currentQuickPath
    property var hiddenPaths: []

    signal placeClicked(string path, var mouse)
    signal contextMenuRequested(string path, string type, int index)
    signal deviceUnmounting(string label)
    signal devicePathUnavailable(string fallbackPath)
    signal operationError(string message)

    function positionAtBeginning()
    {
        if (item && item.contentItem && item.contentItem.flickable)
            item.contentItem.flickable.positionViewAtBeginning()
    }

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

    function shouldShowPlace(index, type, path)
    {
        if (!isStorageSection(type) || isRootPath(path))
            return true

        return showStoragePartitions && control.list && control.list.isDevice(index)
    }

    function isLocationWithin(path, rootPath)
    {
        let location = String(path || "")
        let root = String(rootPath || "")
        while (location.endsWith("/") && location.length > 8)
            location = location.slice(0, -1)
        while (root.endsWith("/") && root.length > 8)
            root = root.slice(0, -1)
        return location === root || location.startsWith(root + "/")
    }

    function requestDeviceSetup(index, udi, modifiers)
    {
        pendingMountUdi = String(udi)
        pendingMountFallbackPath = currentPath
        pendingMountModifiers = modifiers === undefined ? Qt.NoModifier : modifiers
        list.requestSetup(index)
    }

    function rememberDeviceMountPaths()
    {
        if (!list)
            return

        for (let i = 0; i < list.count; ++i)
        {
            const place = list.get(i)
            if (list.isDevice(i) && isExternalDeviceSection(place.type) && !list.setupNeeded(i))
                deviceMountPaths[String(place.udi)] = String(place.path)
        }
    }

    onCurrentPathChanged:
    {
        const previousPath = String(lastObservedPath || "")
        const nextPath = String(currentPath || "")

        if (previousPath.length > 0 && nextPath.length > 0 && previousPath !== nextPath && list)
        {
            for (let i = 0; i < list.count; ++i)
            {
                const place = list.get(i)
                if (list.isDevice(i) && isExternalDeviceSection(place.type) && !list.setupNeeded(i)
                        && isLocationWithin(nextPath, place.path)
                        && !isLocationWithin(previousPath, place.path))
                {
                    const udi = String(place.udi)
                    deviceReturnPaths[udi] = previousPath
                    deviceMountPaths[udi] = String(place.path)
                }
            }
        }

        lastObservedPath = nextPath
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

            holder.visible: count === 0
            holder.title: i18n("Bookmarks")
            holder.body: i18n("Your bookmarks will be listed here")
            Component.onCompleted:
            {
                flickable.highlightFollowsCurrentItem = false
                control.lastObservedPath = control.currentPath
                control.rememberDeviceMountPaths()
                Qt.callLater(() => flickable.positionViewAtBeginning())
            }

            model: Maui.BaseModel
            {
                list: FB.PlacesList
                {
                    id: placesList
                    groups: control.groups
                }
            }

            Maui.BaseModel
            {
                id: quickPlacesModel
                list: FB.PlacesList
                {
                    groups: [FB.FMList.QUICK_PATH, FB.FMList.PLACES_PATH]
                }
            }

            Connections
            {
                target: placesList
                function onPostListChanged()
                {
                    control.rememberDeviceMountPaths()
                }

                function onOperationError(message)
                {
                    control.pendingMountUdi = ""
                    control.pendingMountFallbackPath = ""
                    control.operationError(message)
                }

                function onDeviceSetupDone(udi, path)
                {
                    if (String(udi) !== control.pendingMountUdi)
                        return

                    if (!control.mountRemovableOnClick)
                    {
                        control.pendingMountUdi = ""
                        control.pendingMountFallbackPath = ""
                        return
                    }

                    const deviceId = control.pendingMountUdi
                    const modifiers = control.pendingMountModifiers
                    control.deviceReturnPaths[deviceId] = control.pendingMountFallbackPath
                    control.deviceMountPaths[deviceId] = String(path)
                    control.pendingMountUdi = ""
                    control.pendingMountFallbackPath = ""
                    control.placeClicked(String(path), {modifiers: modifiers})
                }

                function onDeviceUnavailable(udi, path)
                {
                    const deviceId = String(udi)
                    const mountPath = control.deviceMountPaths[deviceId] || path
                    const fallbackPath = control.deviceReturnPaths[deviceId] || ""
                    delete control.deviceMountPaths[deviceId]
                    delete control.deviceReturnPaths[deviceId]

                    if (!control.isLocationWithin(control.currentPath, mountPath))
                        return

                    control.devicePathUnavailable(control.isLocationWithin(fallbackPath, mountPath) ? "" : fallbackPath)
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
                    active: control.quickPlacesEnabled

                    OpacityAnimator on opacity
                    {
                        from: 0
                        to: 1
                        duration: Maui.Style.units.longDuration
                        running: parent.status === Loader.Ready
                    }

                    sourceComponent: GridLayout
                    {
                        rows: 3
                        columns: 3
                        columnSpacing: Maui.Style.defaultSpacing
                        rowSpacing: Maui.Style.defaultSpacing

                        Repeater
                        {
                            model: control.quickPlaces === null ? quickPlacesModel : control.quickPlaces

                            delegate: Maui.GridBrowserDelegate
                            {
                                readonly property var placeData: modelData === undefined ? model : modelData
                                readonly property string placePath: String(placeData.path)
                                readonly property string placeType: placeData.type === undefined ? "" : String(placeData.type)
                                readonly property string placeLabel: String(placeData.label)
                                readonly property string placeIconName: placeData.icon === undefined ? "" : String(placeData.icon)
                                readonly property bool hiddenPlace: control.isPathHidden(placePath)

                                Maui.Theme.colorSet: Maui.Theme.Button
                                Maui.Theme.inherit: false
                                Layout.preferredHeight: hiddenPlace ? 0 : Math.min(50, width)
                                Layout.preferredWidth: hiddenPlace ? 0 : 50
                                Layout.fillWidth: !hiddenPlace
                                Layout.fillHeight: !hiddenPlace
                                flat: false
                                visible: !hiddenPlace
                                isCurrentItem: control.currentQuickPath.length > 0
                                              ? control.currentQuickPath === placePath
                                              : control.currentPath === placePath
                                iconSource: control.placeIcon(placePath, placeIconName, placeLabel, placeType, false)
                                iconSizeHint: 16
                                label1.text: placeLabel
                                labelsVisible: false
                                tooltipText: placeLabel

                                onClicked: (mouse) =>
                                {
                                    control.pendingMountUdi = ""
                                    control.pendingMountFallbackPath = ""
                                    control.placeClicked(placePath, mouse)
                                }
                                onRightClicked: control.contextMenuRequested(placePath, placeType, -1)
                                onPressAndHold: control.contextMenuRequested(placePath, placeType, -1)
                            }
                        }
                    }
                }
            }

            delegate: Maui.ListDelegate
            {
                contentItem.z: 1
                readonly property bool shownPlace: control.shouldShowPlace(index, model.type, model.path)
                readonly property string displayLabel: control.placeLabel(model.path, model.label, model.type)
                width: listBrowser.availableWidth
                height: shownPlace ? implicitHeight : 0
                visible: shownPlace
                enabled: shownPlace
                iconSize: Maui.Style.iconSize
                labelVisible: true
                iconVisible: true
                isCurrentItem: control.currentPath === model.path
                label: displayLabel
                tooltipText: displayLabel
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
                            control.requestDeviceSetup(index, model.udi, Qt.NoModifier)
                        else
                        {
                            control.deviceMountPaths[String(model.udi)] = String(model.path)
                            unmountDialog.deviceIndex = index
                            unmountDialog.deviceLabel = model.label
                            unmountDialog.open()
                        }
                    }
                }

                onClicked: (mouse) =>
                {
                    if (control.mountRemovableOnClick
                            && placesList.isDevice(index)
                            && control.isExternalDeviceSection(model.type)
                            && placesList.setupNeeded(index))
                    {
                        control.requestDeviceSetup(index, model.udi, mouse.modifiers)
                        return
                    }

                    control.pendingMountUdi = ""
                    control.pendingMountFallbackPath = ""
                    if (control.mountRemovableOnClick && placesList.isDevice(index) && control.isExternalDeviceSection(model.type))
                    {
                        control.deviceMountPaths[String(model.udi)] = String(model.path)
                        if (!control.isLocationWithin(control.currentPath, model.path))
                            control.deviceReturnPaths[String(model.udi)] = control.currentPath
                    }
                    control.placeClicked(model.path, mouse)
                }
                onRightClicked: control.contextMenuRequested(model.path, String(model.type), index)
                onPressAndHold: control.contextMenuRequested(model.path, String(model.type), index)
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
                control.deviceUnmounting(deviceLabel)
                close()
            }
            onRejected: close()
        }
    }
}
