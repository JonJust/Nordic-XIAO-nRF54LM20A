# Repeated VS Code notifications

Diagnosed October 21, 2026. The J-Link installation prompt and extension conflict
warning have separate causes.

## J-Link 9.82 installation prompt

VS Code's logs record failed installations on October 20 and October 21:

```text
trying to overwrite '/etc/udev/rules.d/99-jlink.rules',
which is also in package segger-jlink-udev-rules (8.80)
```

The full `jlink` package is not installed; the older rules-only package is
installed. Therefore the installer notification correctly continues to report
missing J-Link software. Nordic's extension recommends J-Link 9.82.

The downloaded installer remains at `/tmp/JLink_Linux_V982_x86_64.deb`.
The following transaction was simulated successfully:

```bash
sudo apt-get install /tmp/JLink_Linux_V982_x86_64.deb segger-jlink-udev-rules-
```

The trailing `-` requests removal of the rules-only package. The transaction
removes only `segger-jlink-udev-rules` 8.80 and installs `jlink` 9.82.0; no installed
packages depend on the old rules package. Its single rules file is byte-identical
to the replacement supplied by the new package.

**Pending user authentication:** an attempted noninteractive installation stopped
with `sudo: interactive authentication is required`. No package changes were
made. Run the command in your own terminal to authenticate, then reload VS Code.

You can verify package completion with:

```bash
dpkg-query -W -f='${Status} ${Version}\n' jlink
```

The expected result is `install ok installed 9.82.0`.

For this board's SAMD11/CMSIS-DAP and OpenOCD workflow, J-Link is not the flasher.
The notification's **Skip** button remembers this recommended version; closing
the notification does not. Skip dismisses the recommendation without repairing
the package installation.

## Conflicting extensions warning

The installed Nordic extension's conflict detector identifies **CMake Tools**
(`ms-vscode.cmake-tools`); the latest logs confirm that it is active. No legacy
Devicetree or Kconfig extension from Nordic's conflict list is installed.

In VS Code, open Extensions with Ctrl+Shift+X, search for CMake Tools, then use
its gear menu and select **Disable (Workspace)**. Reload if prompted. This keeps
CMake Tools available in other workspaces.

The project's existing `cmake.configureOnOpen=false` setting prevents automatic
configuration, but does not disable the extension or suppress Nordic's conflict
detector. The supported persistent workspace-disable action requires the VS Code
UI; `code --disable-extension` is a temporary new-window option.

Continue building with **nRF: Build blinky (NCS 3.4.1)** or the nRF Connect sidebar.

References: [Nordic prerequisites](https://docs.nordicsemi.com/r/bundle/nrf-connect-vscode/page/get_started/install.html/extension-prerequisites),
[VS Code extension management](https://code.visualstudio.com/docs/configure/extensions/extension-marketplace).
