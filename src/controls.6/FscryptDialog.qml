import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB

Maui.InfoDialog
{
    id: control

    property url directory
    property bool createDirectory: false

    Maui.Theme.colorSet: Maui.Theme.View

    readonly property string directoryPath: control.ensureDirectoryUrl(control.directory).toString()
    readonly property string directoryName: _directoryNameField.text.trim()
    readonly property string protectorName: _protectorNameField.text.trim()
    readonly property string passphrase: _passphraseField.text
    readonly property string confirmation: _confirmationField.text
    readonly property string targetDirectoryPath: directoryPath.length > 0 ? directoryPath.replace(/\/$/, "") + "/" + directoryName : ""
    readonly property bool directoryExists: targetDirectoryPath.length > 0 && FB.FM.fileExists(targetDirectoryPath)
    readonly property bool validDirectoryName: directoryName.length > 0
                                                 && directoryName !== "."
                                                 && directoryName !== ".."
                                                 && directoryName.indexOf("/") < 0
                                                 && !directoryExists
    readonly property bool validInput: (!createDirectory || validDirectoryName)
                                       && protectorName.length > 0
                                       && passphrase.length > 0
                                       && passphrase === confirmation

    signal operationCompleted(bool success)

    title: createDirectory ? i18n("Create encrypted directory") : ""
    message: createDirectory
             ? i18n("Create and encrypt an empty directory with fscrypt.")
             : i18n("Encrypt this empty directory with fscrypt.")
    standardButtons: Dialog.Apply | Dialog.Cancel
    template.iconVisible: false

    background: Rectangle
    {
        Maui.Theme.colorSet: Maui.Theme.View
        Maui.Theme.inherit: false
        radius: Maui.Style.radiusV
        color: Maui.Theme.backgroundColor
        border.color: Maui.Theme.alternateBackgroundColor
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect
        {
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowColor: "#80000000"
        }

        Behavior on color
        {
            Maui.ColorTransition{}
        }
    }

    onOpened:
    {
        updateValidation()
        if (createDirectory)
            _directoryNameField.forceActiveFocus()
        else
            _protectorNameField.forceActiveFocus()
    }

    onApplied:
    {
        if (!validate())
            return

        if (createDirectory)
            _fscrypt.createEncryptedDirectory(control.directory, control.directoryName, control.protectorName, control.passphrase)
        else
            _fscrypt.encryptDirectory(control.directory, control.protectorName, control.passphrase)

        updateValidation()
    }

    onRejected:
    {
        if (!_fscrypt.running)
            close()
    }

    Maui.TextField
    {
        id: _directoryNameField
        visible: control.createDirectory
        Layout.fillWidth: true
        Layout.topMargin: Maui.Style.space.medium
        Maui.Controls.title: i18n("Directory name")
        placeholderText: i18n("Encrypted directory")
        onTextChanged: control.updateValidation()
        onAccepted: _protectorNameField.forceActiveFocus()
    }

    Maui.TextField
    {
        id: _directoryPathField
        visible: !control.createDirectory
        Layout.fillWidth: true
        Layout.topMargin: Maui.Style.space.medium
        readOnly: true
        text: control.displayPath(control.directory)
        Maui.Controls.title: i18n("Directory")
    }

    Maui.TextField
    {
        id: _protectorNameField
        Layout.fillWidth: true
        Maui.Controls.title: i18n("Passphrase name")
        placeholderText: i18n("A name for this passphrase")
        onTextChanged: control.updateValidation()
        onAccepted: _passphraseField.forceActiveFocus()
    }

    Maui.PasswordField
    {
        id: _passphraseField
        Layout.fillWidth: true
        echoMode: TextInput.Password
        passwordMaskDelay: 0
        Maui.Controls.title: i18n("Passphrase")
        onTextChanged: control.updateValidation()
        onAccepted: _confirmationField.forceActiveFocus()
    }

    Maui.PasswordField
    {
        id: _confirmationField
        Layout.fillWidth: true
        echoMode: TextInput.Password
        passwordMaskDelay: 0
        Maui.Controls.title: i18n("Confirm passphrase")
        onTextChanged: control.updateValidation()
        onAccepted:
        {
            const applyButton = control.standardButton(Dialog.Apply)
            if (applyButton)
                applyButton.forceActiveFocus()
        }
    }

    FB.Fscrypt
    {
        id: _fscrypt
    }

    Connections
    {
        target: _fscrypt

        function onRunningChanged()
        {
            control.updateValidation()
        }

        function onFinished(success, message)
        {
            if (success)
            {
                control.operationCompleted(true)
                control.close()
                return
            }

            control.alert(message, 2)
            control.updateValidation()
        }
    }

    function displayPath(path)
    {
        const value = path ? path.toString() : ""
        if (value.startsWith("file://"))
            return decodeURIComponent(value.replace(/^file:\/\//, ""))

        return value
    }

    function ensureDirectoryUrl(path)
    {
        const value = path ? path.toString().trim() : ""

        if (value.startsWith("/") && !value.startsWith("//"))
            return "file://" + encodeURI(value)

        return value
    }

    function validate()
    {
        if (createDirectory && !validDirectoryName)
        {
            if (directoryName.length === 0)
                alert(i18n("Directory name can not be empty."), 2)
            else if (directoryExists)
                alert(i18n("A directory with the same name already exists."), 2)
            else
                alert(i18n("Enter a valid directory name."), 2)
            return false
        }

        if (protectorName.length === 0)
        {
            alert(i18n("Passphrase name can not be empty."), 2)
            return false
        }

        if (passphrase.length === 0)
        {
            alert(i18n("Passphrase can not be empty."), 2)
            return false
        }

        if (passphrase !== confirmation)
        {
            alert(i18n("The passphrases do not match."), 2)
            return false
        }

        return true
    }

    function updateValidation()
    {
        Qt.callLater(applyValidation)
    }

    function applyValidation()
    {
        const applyButton = standardButton(Dialog.Apply)
        if (applyButton)
        {
            applyButton.text = createDirectory ? i18n("Create") : i18n("Encrypt")
            applyButton.enabled = validInput && !_fscrypt.running
        }

        const cancelButton = standardButton(Dialog.Cancel)
        if (cancelButton)
            cancelButton.enabled = !_fscrypt.running
    }
}
