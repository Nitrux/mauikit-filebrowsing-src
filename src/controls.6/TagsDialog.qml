import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

/**
 * @inherit org::mauikit::controls::PopupPage
 * @brief A popup dialog for selecting between all the available tags to associate to a given set of file URLs.
 *
 * This popup page also allows to create new tags, edit existing ones and removing.
 *
 * To associate the set of file URLs, use the exposed property `composerList.urls`, which is an alias to TagsList::urls.
 * @see composerList
 * @see TagsList::urls
 * The `composerList` property exposes most of the available properties for tweaking the behaviour, and also contains the methods to perform any modifications to the tags.
 *
 * @image html tagsdialog.png "Example using the TagsDialog control"
 *
 * @code
 * Maui.Page
 * {
 *    Maui.Controls.showCSD: true
 *    anchors.fill: parent
 *
 *    title: "Add tags to a file"
 *
 *    FB.FileBrowser
 *    {
 *        id: _fileBrowser
 *        anchors.fill: parent
 *        currentPath: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
 *        settings.viewType: FB.FMList.LIST_VIEW
 *        onItemClicked: (index) =>
 *                        {
 *                            _tagsDialog.composerList.urls = [_fileBrowser.currentFMModel.get(index).url]
 *                            _tagsDialog.open()
 *                        }
 *    }
 *
 *    FB.TagsDialog
 *    {
 *        id: _tagsDialog
 *        composerList.strict: false //Show all the associated tags to the file
 *        onTagsReady: (tags) => console.log(tags)
 *    }
 * }
 * @endcode
 */
Maui.PopupPage
{
    id: control

    title: i18nd("mauikitfilebrowsing", "Tags")

    header: Maui.DialogHeader
    {
        width: parent ? parent.width : implicitWidth
        title: control.title
        onCloseRequested: control.close()
    }

    /**
     * @brief An alias to the TagsList list/model controlling and listing all of the available tags.
     * @see TagsListmodel
     * @property TagsList TagsDialog::tagList
     */
    readonly property alias taglist : _tagsList

    /**
     * @brief An alias to the Mauikit ListBrowser element listing the tag elements.
     * @property MauiKit::ListBrowser TagsDialog::listView
     */
    readonly property alias listView: _listView

    /**
     * @brief An alias to the TagsList controller and model.
     * This property is exposed to set the file URLs to which perform any new assignment or removal of tags.
     * Refer to its documentation for more details on the available actions.
     * @property TagsList TagsDialog::composerList
     */
    readonly property alias composerList: tagListComposer.list

    /**
     * @brief Emitted once the current set of tags has been saved to the target file URLs.
     * This can include removals or additions depending on how the composer list was edited.
     * @param tags the list of tag names that was saved
     */
    signal tagsReady(var tags)

    property string thumbnailSource

    hint: 1

    maxHeight: 500
    maxWidth: 350

    actions: [

        Action
        {
            Maui.Controls.status: Maui.Controls.Positive
            text: i18nd("mauikitfilebrowsing", "Save")
            onTriggered: control.setTags()
        },

        Action
        {
            text: i18nd("mauikitfilebrowsing", "Cancel")
            onTriggered: control.close()
        }
    ]
    headBar.visible: false
    Maui.InfoDialog
    {
        id: _deleteDialog

        property string tag
        title: i18nd("mauikitfilebrowsing", "Delete %1", tag)
        message: i18nd("mauikitfilebrowsing", "Are you sure you want to delete this tag? This action can not be undone.")
        template.iconSource: "tag"
        onAccepted:
        {
            FB.Tagging.removeTag(tag)
            _deleteDialog.close()
        }

        onRejected: _deleteDialog.close()
    }

    Maui.ContextualMenu
    {
        id: _menu

        MenuItem
        {
            text: i18nd("mauikitfilebrowsing", "Edit")
            icon.name: "document-edit"
        }

        MenuItem
        {
            text: i18nd("mauikitfilebrowsing", "Delete")
            icon.name: "delete"
            onTriggered:
            {
                _deleteDialog.tag = _tagsModel.get(_listView.currentIndex).tag
                _deleteDialog.open()
            }
        }
    }

    stack: [

        ColumnLayout
        {
            Layout.fillWidth: true
            spacing: Maui.Style.space.small

            Maui.SectionHeader
            {
                readonly property var firstItemInfo: composerList.urls.length > 0
                                                      ? FB.FM.getFileInfo(composerList.urls[0])
                                                      : ({})
                Layout.fillWidth: true
                label1.text: firstItemInfo.label || i18nd("mauikitfilebrowsing", "Selected files")
                label2.text: i18np("1 file", "%1 files", composerList.urls.length)
            }

            Maui.TextField
            {
                id: tagText
                Layout.fillWidth: true
                Layout.leftMargin: Maui.Style.space.small
                Layout.rightMargin: Maui.Style.space.small
                placeholderText: i18nd("mauikitfilebrowsing", "Filter or add a new tag")
                icon.source: "tag"
                onAccepted: control.commitPendingTags()
                onTextChanged: _tagsModel.filter = text
            }

            FB.TagList
            {
                id: tagListComposer
                Layout.fillWidth: true
                visible: count > 0
                onTagRemoved: (index) => list.remove(index)
                placeholderText: i18nd("mauikitfilebrowsing", "No tags yet.")
            }
        },

        Maui.ListBrowser
        {
            id: _listView

            Layout.fillHeight: true
            Layout.fillWidth: true
            clip: true
            currentIndex: -1

            holder.emoji: "tag"
            holder.visible: _listView.count === 0
            holder.title : i18nd("mauikitfilebrowsing", "No Tags!")
            holder.body: i18nd("mauikitfilebrowsing", "Create new tags to organize your files.")

            model: Maui.BaseModel
            {
                id: _tagsModel
                sort: "tag"
                sortOrder: Qt.AscendingOrder
                recursiveFilteringEnabled: true
                sortCaseSensitivity: Qt.CaseInsensitive
                filterCaseSensitivity: Qt.CaseInsensitive
                list: FB.TagsListModel
                {
                    id: _tagsList
                    strict: false
                }
            }

            delegate: Maui.ListBrowserDelegate
            {
                width: ListView.view.width
                label1.text: model.tag
                iconSource: model.icon
                iconSizeHint: Maui.Style.iconSizes.small

                template.content: Rectangle
                {
                    color: model.color ? model.color : "transparent"
                    height: Maui.Style.iconSizes.small
                    radius: height/2
                    width: height
                }

                onClicked:
                {
                    _listView.currentIndex = index
                    control.appendSelectedTag()
                }

                onDoubleClicked:
                {
                    _listView.currentIndex = index
                    control.appendSelectedTag()
                }

                onPressAndHold:
                {
                    _listView.currentIndex = index
                    _menu.open()
                }

                onRightClicked:
                {
                    _listView.currentIndex = index
                    _menu.open()
                }
            }
        }
    ]

    onClosed:
    {
        composerList.urls = []
        tagText.clear()
        _tagsModel.filter = ""
    }

    onOpened: tagText.forceActiveFocus()

    /**
     * @brief Saves the currently composed set of tags to the target file URLs,
     * emits the `tagsReady` signal and then closes the dialog.
     */
    function setTags()
    {
        commitPendingTags()
        const tags = tagListComposer.list.tags
        composerList.updateToUrls(tags)
        control.tagsReady(tags)
        close()
    }

    function appendSelectedTag()
    {
        const tagItem = _tagsModel.get(_listView.currentIndex)

        if (tagItem)
            tagListComposer.list.appendItem(tagItem)
    }

    function commitPendingTags()
    {
        const pendingText = tagText.text.trim()

        if (pendingText.length === 0)
            return

        const tags = pendingText.split(",")
        for (var i in tags)
        {
            const myTag = tags[i].trim()

            if (myTag.length === 0)
                continue

            _tagsList.append(myTag)
            tagListComposer.list.append(myTag)
        }

        tagText.clear()
    }
}
