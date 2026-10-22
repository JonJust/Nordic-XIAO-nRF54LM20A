# NCS v3.4.1 registration repair

Verified October 20, 2026, after the VS Code installation reported `west zephyr-export` exiting with code `-2`.

## Result

The SDK and toolchain downloads were intact. Registration was rerun successfully with the installed SDK manager and an explicit toolchain. Both CMake package entries point to the installed SDK. A freestanding copy of the official Hello World sample configured and compiled successfully for `nrf54lm20dk/nrf54lm20a/cpuapp`.

This validates SDK discovery and compilation. It does not validate the XIAO board integration, debugger, flashing, USB, or radio. No hardware was programmed.

## Installed locations

- SDK: `/home/skipper/ncs/v3.4.1`
- Toolchain: `/home/skipper/ncs/toolchains/8285d8ad56`
- SDK manager used by VS Code: `/home/skipper/.vscode/extensions/nordic-semiconductor.nrf-connect-2026.9.2823-linux-x64/platform/nrfutil/bin/nrfutil-sdk-manager`

The SDK manager's repair invocation was:

```bash
/home/skipper/.vscode/extensions/nordic-semiconductor.nrf-connect-2026.9.2823-linux-x64/platform/nrfutil/bin/nrfutil-sdk-manager \
  sdk register v3.4.1 \
  --sdk-path /home/skipper/ncs/v3.4.1 \
  --toolchain-path /home/skipper/ncs/toolchains/8285d8ad56 \
  --log-output stdout --log-level info
```

This command completed with exit code 0. It updates CMake registration without downloading or reinstalling the SDK. [Nordic's registration documentation](https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-sdk-manager/guides/sdk_manager_sdk.html/registering-an-sdk-directly)

## Likely original cause

The installed Nordic extension makes a separate `west zephyr-export` call after SDK Manager installation. That call can use the previously selected toolchain rather than explicitly selecting the new one. The extension's stored state showed an SDK selection but no toolchain selection, and ordinary shell PATH did not contain `west`.

The extension suppresses the process-spawn error and prints its numeric close code. In VS Code's Node runtime, a missing executable reproduces `ENOENT -2` followed by close code `-2`. This makes a first-install toolchain selection/refresh problem the likely cause; the exact original spawn error was not retained, so it is not proven. Existing CMake entries were already valid before the repair.

## Refresh VS Code

Open the Command Palette with Ctrl+Shift+P and run:

1. **nRF Connect: Refresh Toolchains**
2. **nRF Connect: Refresh SDKs**
3. **nRF Connect: Pick Toolchain and SDK for nRF Connect Terminal**; choose v3.4.1 for both.

If the views remain stale, use **Developer: Reload Window**. These command names were verified in the installed Nordic extension. There is no need to add the bundled tools permanently to the system PATH.

## Verification performed

- `west --version`: 1.5.0
- Bundled CMake: 4.2.1
- Bundled Python: 3.12.4
- ARM compiler: GCC 14.3.0, Zephyr SDK 1.0.1
- CMake found NCS's Zephyr 4.4.2 through registration from a freestanding application.
- All 264 build steps completed, generating `zephyr.elf`, `.hex`, and `.bin` in `/tmp/nrf54lm20a-ncs341-smoke-20261020/build/zephyr`.

The build used a copy of the installed SDK's unmodified `zephyr/samples/hello_world` in a temporary directory. It is a DK reference image, not XIAO firmware. One vendor configuration deprecation warning (`NRF_PLATFORM_LUMOS`) did not prevent the build.
