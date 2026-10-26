// Copyright 2026 Nitrux Latinoamericana S.C.
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

import org.mauikit.controls as Maui

Item
{
    id: control

    property int gridItemSize: Maui.Style.iconSizes.big
    property string serverAddress
    property string serverProtocol
    readonly property int resolvedIconSize: Maui.Style.mapToIconSizes(gridItemSize)

    signal clicked(var mouse)
    signal rightClicked(var mouse)

    Maui.GridBrowserDelegate
    {
        id: delegate

        anchors.fill: parent
        anchors.margins: Maui.Handy.isMobile ? Maui.Style.space.tiny : Maui.Style.space.medium

        template.imageWidth: control.gridItemSize
        template.imageHeight: control.gridItemSize
        template.labelSizeHint: 42

        iconSizeHint: control.resolvedIconSize
        iconSource: "folder-network"
        label1.text: control.serverAddress
        label1.wrapMode: Text.NoWrap
        label2.font.pointSize: Maui.Style.fontSizes.tiny
        label2.text: control.serverProtocol
        tooltipText: control.serverAddress
        template.fillMode: Image.PreserveAspectFit
        template.maskRadius: 0
        isCurrentItem: parent.GridView.isCurrentItem

        onClicked: (mouse) => control.clicked(mouse)
        onRightClicked: (mouse) => control.rightClicked(mouse)
    }
}
