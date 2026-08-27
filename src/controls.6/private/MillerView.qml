/*
 *   Copyright 2026 Uri Herrera <uri@nxos.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation, either version 3, or
 *   (at your option) any later version.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

/**
 * @brief A file-aware Miller column browser.
 *
 * The first column contains the current location parent entries, the
 * second column contains the current location, and the third column shows
 * the selected directory entries or a compact file preview.
 */
Item
{
    id: control

    focus: true
    clip: true

    property var currentFMList: null
    property var currentFMModel: null
    property string currentPath: currentFMList ? String(currentFMList.path) : ""

    property bool selectionMode: false
    property var selectionBar: null
    property bool showHiddenFiles: false
    property bool foldersFirst: true
    property int sortBy: FB.FMList.LABEL
    property int listItemSize: Maui.Style.rowHeight
    property string audioFallbackImageSource: ""
    property Component previewComponent: null

    property alias currentIndex: _currentColumn.currentIndex
    readonly property int count: _currentColumn.count
    readonly property alias flickable: _currentColumn.flickable

    readonly property var currentItem: currentFMModel && currentIndex >= 0
                                             ? currentFMModel.get(currentIndex)
                                             : ({})
    readonly property string selectedPath: String(currentItem.path || "")
    readonly property bool selectedIsDirectory: control.isDirectory(currentItem)
    readonly property string parentPath: control.parentPathFor(control.currentPath)
    readonly property string previewPath: selectedIsDirectory
                                            ? selectedPath
                                            : ""

    property bool _navigationTransitionActive: false
    property string _navigationTargetPath: ""

    signal itemClicked(int index)
    signal itemDoubleClicked(int index)
    signal itemRightClicked(int index)
    signal itemToggled(int index, bool state)
    signal itemsSelected(var indexes)
    signal keyPress(var event)
    signal areaClicked(var mouse)
    signal areaRightClicked()
    signal folderRequested(string path)
    signal fileRequested(string path)
    signal contentDropped(var drop, string target)

    function isDirectory(item)
    {
        const value = item && item.isdir
        return value === true || value === "true"
    }

    function parentPathFor(path)
    {
        const value = String(path || "")
        if (value.startsWith("file://"))
            return String(FB.FM.parentDir(value))

        return currentFMList ? String(currentFMList.parentPath) : ""
    }

    function iconFor(item)
    {
        const icon = String(item && item.icon || "")
        if (icon.length > 0)
            return icon

        const mime = String(item && item.mime || "")
        if (mime === "inode/directory")
            return "folder"
        if (mime.startsWith("image/"))
            return "image-x-generic"
        if (mime.startsWith("text/"))
            return "text-x-generic"
        if (mime.startsWith("audio/"))
            return "audio-x-generic"
        if (mime.startsWith("video/"))
            return "video-x-generic"

        return "application-octet-stream"
    }

    function imageFor(item)
    {
        const mime = String(item && item.mime || "")
        const thumbnail = String(item && item.thumbnail || "")
        const size = parseInt(String(item && item.size || "0"), 10)

        if (size === 0)
            return ""

        if (thumbnail.length > 0)
            return thumbnail

        if (mime.startsWith("audio/") && control.audioFallbackImageSource.length > 0)
            return control.audioFallbackImageSource

        return ""
    }

    function label2For(item)
    {
        if (control.isDirectory(item))
        {
            const count = String(item && item.count || "")
            return count.length > 0
                   ? count + i18nd("mauikitfilebrowsing", " items")
                   : ""
        }

        return item && item.size ? Maui.Handy.formatSize(item.size) : ""
    }

    function resetCurrentIndex()
    {
        if (_currentColumn.count > 0)
            _currentColumn.currentIndex = Math.max(0, Math.min(_currentColumn.currentIndex, _currentColumn.count - 1))
        else
            _currentColumn.currentIndex = -1
    }

    function revealParent()
    {
        if (control.currentPath.length === 0)
            return

        for (let i = 0; i < _parentColumn.count; ++i)
        {
            const item = _parentModel.get(i)
            if (String(item.path || "") === control.currentPath)
            {
                _parentColumn.currentIndex = i
                return
            }
        }
    }

    function updatePreview()
    {
        if (_previewLoader.item && typeof _previewLoader.item.setData === "function")
            _previewLoader.item.setData(control.selectedPath)
    }

    function navigationDirectionFor(path)
    {
        if (String(path) === control.parentPath)
            return -1

        if (control.parentPathFor(path) === control.currentPath)
            return 1

        return 0
    }

    function navigateTo(path, direction)
    {
        const targetPath = String(path || "")
        if (targetPath.length === 0 || targetPath === control.currentPath || control._navigationTransitionActive)
            return

        const transitionDirection = direction || control.navigationDirectionFor(targetPath)
        if (transitionDirection === 0 || !control.visible || control.width <= 0 || control.height <= 0)
        {
            control.folderRequested(targetPath)
            return
        }

        control._navigationTransitionActive = true
        control._navigationTargetPath = targetPath
        _navigationAnimation.to = transitionDirection > 0
                ? -(_currentColumn.parent.x - _parentColumn.parent.x)
                : _currentColumn.parent.x - _parentColumn.parent.x
        _navigationAnimation.start()
    }

    function finishNavigationTransition()
    {
        const targetPath = control._navigationTargetPath
        control._navigationTargetPath = ""
        _navigationOffset.x = 0
        control._navigationTransitionActive = false

        if (targetPath.length > 0)
            control.folderRequested(targetPath)
    }

    Component.onCompleted: Qt.callLater(control.revealParent)

    onCurrentPathChanged:
    {
        Qt.callLater(control.revealParent)
        Qt.callLater(control.resetCurrentIndex)
    }

    onSelectedPathChanged: Qt.callLater(control.updatePreview)

    Connections
    {
        target: control.currentFMModel
        ignoreUnknownSignals: true

        function onModelReset()
        {
            Qt.callLater(control.resetCurrentIndex)
        }

        function onRowsInserted()
        {
            Qt.callLater(control.resetCurrentIndex)
        }

        function onRowsRemoved()
        {
            Qt.callLater(control.resetCurrentIndex)
        }
    }

    Connections
    {
        target: _parentColumn

        function onCountChanged()
        {
            Qt.callLater(control.revealParent)
        }
    }

    Connections
    {
        target: _currentColumn

        function onCountChanged()
        {
            Qt.callLater(control.resetCurrentIndex)
        }
    }

    Maui.BaseModel
    {
        id: _parentModel

        list: FB.FMList
        {
            path: control.parentPath
            hidden: control.showHiddenFiles
            foldersFirst: control.foldersFirst
            sortBy: control.sortBy
        }
    }

    Maui.BaseModel
    {
        id: _previewModel

        list: FB.FMList
        {
            path: control.previewPath
            hidden: control.showHiddenFiles
            foldersFirst: control.foldersFirst
            sortBy: control.sortBy
        }
    }

    Maui.SplitView
    {
        id: _splitView
        anchors.fill: parent
        orientation: Qt.Horizontal
        transform: Translate
        {
            id: _navigationOffset
        }
        spacing: Maui.Style.defaultSpacing
        background: null

        Maui.SplitViewItem
        {
            SplitView.preferredWidth: Math.round((SplitView.view.width - SplitView.view.spacing * (SplitView.view.count - 1)) / SplitView.view.count)
            minimumWidth: Maui.Style.units.gridUnit * 8
            autoClose: false
            padding: 0
            background: null

            Maui.ListBrowser
            {
                id: _parentColumn
                anchors.fill: parent
                background: null
                clip: true

                flickable.add: Transition
                {
                    NumberAnimation
                    {
                        property: "x"
                        from: flickable.width
                        to: 0
                        duration: Maui.Style.units.longDuration
                        easing.type: Easing.OutCubic
                    }
                }

                flickable.remove: Transition
                {
                    NumberAnimation
                    {
                        property: "x"
                        to: -flickable.width
                        duration: Maui.Style.units.longDuration
                        easing.type: Easing.InCubic
                    }
                }

                layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
                               && _splitView.currentIndex !== 0
                layer.effect: MultiEffect
                {
                    saturation: -1
                }

                onAreaClicked: (mouse) => control.areaClicked(mouse)
                onAreaRightClicked: control.areaRightClicked()

                model: _parentModel
                currentIndex: -1

                delegate: Maui.ListBrowserDelegate
                {
                    width: ListView.view.width
                    flat: true
                    iconSource: control.iconFor(model)
                    iconSizeHint: Maui.Style.mapToIconSizes(control.listItemSize)
                    label1.text: model.label || ""
                    label2.text: control.label2For(model)
                    tooltipText: model.path
                    isCurrentItem: String(model.path || "") === control.currentPath

                    onClicked:
                    {
                        if (control.isDirectory(model))
                            control.navigateTo(String(model.path))
                        else
                            control.fileRequested(String(model.path))
                    }

                    onDoubleClicked:
                    {
                        if (control.isDirectory(model))
                            control.navigateTo(String(model.path))
                        else
                            control.fileRequested(String(model.path))
                    }
                }
            }
        }

        Maui.SplitViewItem
        {
            SplitView.preferredWidth: Math.round((SplitView.view.width - SplitView.view.spacing * (SplitView.view.count - 1)) / SplitView.view.count)
            minimumWidth: Maui.Style.units.gridUnit * 8
            autoClose: false
            padding: 0
            background: null

            Maui.ListBrowser
            {
                id: _currentColumn
                anchors.fill: parent
                background: null
                clip: true

                flickable.add: Transition
                {
                    NumberAnimation
                    {
                        property: "x"
                        from: flickable.width
                        to: 0
                        duration: Maui.Style.units.longDuration
                        easing.type: Easing.OutCubic
                    }
                }

                flickable.remove: Transition
                {
                    NumberAnimation
                    {
                        property: "x"
                        to: -flickable.width
                        duration: Maui.Style.units.longDuration
                        easing.type: Easing.InCubic
                    }
                }

                layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
                               && _splitView.currentIndex !== 1
                layer.effect: MultiEffect
                {
                    saturation: -1
                }

                onAreaClicked: (mouse) => control.areaClicked(mouse)
                onAreaRightClicked: control.areaRightClicked()

                model: control.currentFMModel
                currentIndex: -1

                delegate: Maui.ListBrowserDelegate
                {
                    width: ListView.view.width
                    flat: true
                    iconSource: control.iconFor(model)
                    imageSource: control.imageFor(model)
                    iconSizeHint: Maui.Style.mapToIconSizes(control.listItemSize)
                    label1.text: model.label || ""
                    label2.text: control.label2For(model)
                    label3.text: model.modified ? Maui.Handy.formatDate(model.modified, "MM/dd/yyyy") : ""
                    tooltipText: model.path
                    checkable: control.selectionMode || checked
                    checked: control.selectionBar ? control.selectionBar.contains(model.path) : false
                    draggable: true

                    template.iconContainer.opacity: model.hidden == "true" ? 0.5 : 1

                    Drag.keys: ["text/uri-list"]
                    Drag.mimeData: {
                        "text/uri-list": model.path
                    }

                    onClicked: (mouse) =>
                    {
                        _currentColumn.currentIndex = index

                        if (mouse.button === Qt.LeftButton && control.selectionMode && !control.isDirectory(model))
                            control.itemsSelected([index])
                    }

                    onDoubleClicked:
                    {
                        _currentColumn.currentIndex = index
                        if (control.isDirectory(model))
                            control.navigateTo(String(model.path))
                        else
                            control.fileRequested(String(model.path))
                    }

                    onRightClicked:
                    {
                        _currentColumn.currentIndex = index
                        control.itemRightClicked(index)
                    }

                    onToggled: (state) => control.itemToggled(index, state)

                    onContentDropped: (drop) => control.contentDropped(drop, String(model.path))
                }
            }
        }

        Maui.SplitViewItem
        {
            SplitView.preferredWidth: Math.round((SplitView.view.width - SplitView.view.spacing * (SplitView.view.count - 1)) / SplitView.view.count)
            minimumWidth: Maui.Style.units.gridUnit * 8
            autoClose: false
            padding: 0
            background: null

            Maui.ListBrowser
            {
                id: _previewColumn
                anchors.fill: parent
                background: null
                clip: true

                flickable.add: Transition
                {
                    NumberAnimation
                    {
                        property: "x"
                        from: flickable.width
                        to: 0
                        duration: Maui.Style.units.longDuration
                        easing.type: Easing.OutCubic
                    }
                }

                flickable.remove: Transition
                {
                    NumberAnimation
                    {
                        property: "x"
                        to: -flickable.width
                        duration: Maui.Style.units.longDuration
                        easing.type: Easing.InCubic
                    }
                }

                layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
                               && _splitView.currentIndex !== 2
                layer.effect: MultiEffect
                {
                    saturation: -1
                }

                onAreaClicked: (mouse) => control.areaClicked(mouse)
                onAreaRightClicked: control.areaRightClicked()

                model: _previewModel
                visible: control.selectedIsDirectory

                delegate: Maui.ListBrowserDelegate
                {
                    width: ListView.view.width
                    flat: true
                    mouseArea.enabled: false
                    iconSource: control.iconFor(model)
                    imageSource: control.imageFor(model)
                    iconSizeHint: Maui.Style.mapToIconSizes(control.listItemSize)
                    label1.text: model.label || ""
                    label2.text: control.label2For(model)
                    label3.text: model.modified ? Maui.Handy.formatDate(model.modified, "MM/dd/yyyy") : ""
                    tooltipText: model.path
                }
            }

            Loader
            {
                id: _previewLoader
                anchors.fill: parent
                active: control.visible
                         && control.enabled
                         && !control.selectedIsDirectory
                         && control.selectedPath.length > 0
                         && control.previewComponent !== null
                visible: active
                sourceComponent: control.previewComponent

                layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
                               && _splitView.currentIndex !== 2
                layer.effect: MultiEffect
                {
                    saturation: -1
                }

                onLoaded: control.updatePreview()
            }
        }
    }

    MouseArea
    {
        anchors.fill: parent
        visible: control._navigationTransitionActive
        z: 2
    }

    NumberAnimation
    {
        id: _navigationAnimation
        target: _navigationOffset
        property: "x"
        from: 0
        duration: Maui.Style.enableEffects ? Maui.Style.units.shortDuration : 0
        easing.type: Easing.OutCubic

        onStopped: control.finishNavigationTransition()
    }

    Keys.enabled: true
    Keys.onPressed: (event) =>
    {
        if (event.key === Qt.Key_Left)
        {
            if (control.parentPath.length > 0 && control.parentPath !== control.currentPath)
                control.navigateTo(control.parentPath, -1)

            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Right)
        {
            if (control.selectedIsDirectory)
                control.navigateTo(control.selectedPath, 1)
            else if (control.selectedPath.length > 0)
                control.fileRequested(control.selectedPath)

            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
        {
            if (control.currentIndex >= 0)
            {
                if (control.selectedIsDirectory)
                    control.navigateTo(control.selectedPath, 1)
                else
                    control.fileRequested(control.selectedPath)
            }

            event.accepted = true
            return
        }

        control.keyPress(event)
        event.accepted = false
    }
}