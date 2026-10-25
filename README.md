# XIAO nRF54LM20A development research

Initial research: October 20, 2026. Working setup verified October 21, 2026. Board hardware revision: **V1.0, confirmed by the user**. SDK registration is repaired; the XIAO LED project builds, flashes, runs, and supports source-level debugging from VS Code. On October 22, the two-board 4 Mbit/s ESB benchmark automatically negotiated, transmitted one million packets at nominal 8 kHz, received 999,827, exchanged the RX summary, and started its next run. Native Nordic USB remains unverified.

The nRF54LM20A is suitable for investigating a wireless mouse link and supports native USB High Speed. Whether this particular XIAO can connect that USB peripheral to a PC depends on its hardware revision.

| Hardware | USB connection | Consequence for this project |
| --- | --- | --- |
| XIAO V1.0 | USB-C data goes to an onboard SAMD11 debugger; Nordic native USB data pins are unconnected | Use it for radio development. A separate USB-capable controller or a different receiver board is needed for native USB HID. |
| XIAO V2.0 | Seeed's current board source describes native Nordic USB and removes SAMD11 | Native HID is a plausible software path; preserve the factory bootloader layout and verify actual 480 Mb/s enumeration. |

The user's board is V1.0; its USB-C connector is for the SAMD11 debug/UART bridge. The V2.0 comparison is retained as a reference. V1.0 wiring is confirmed by the published schematic and PCB. V2.0 is supported by official revision-specific source added October 20, 2026; a corresponding V2.0 schematic and physical USB test have not been verified.

- [Documentation and download index](docs/RESOURCES.md): all eleven Seeed English wiki pages found, hardware files, Nordic specifications, errata, and software references.
- [Toolchain, wireless, and USB-HS investigation](docs/DEVELOPMENT.md): version choices, revision identification, proposed bring-up commands, and mouse test sequence.
- [Hardware download manifest](docs/vendor/manifest.json): original URLs, byte counts, and SHA-256 checksums.
- [Pinned Seeed board-source provenance](docs/vendor/seeed/platform-seeedboards-a6fd41d/SOURCE.md): the source that distinguishes V1.0 and V2.0.
- [SDK registration repair](docs/SDK_REGISTRATION.md): diagnosis, repair, build verification, and VS Code refresh steps.
- [Repeated VS Code notifications](docs/VSCODE_NOTIFICATIONS.md): the J-Link package collision and CMake Tools conflict warning.
- [First project walkthrough](apps/blinky/README.md): the build-verified V1.0 application, each file's purpose, and VS Code/terminal setup.
- [Toolchain and VS Code explained](docs/TOOLCHAIN.md): how the SDK, compiler, build tools, debugger, JSON files, and Run/Debug buttons fit together.
- [8 kHz wireless mouse evaluation plan](docs/WIRELESS_EVALUATION.md): STM32H562-to-Nordic SPI architecture, 4 Mbit/s ESB timing, mouse packets, pairing, and staged validation with two XIAOs.
- [4 Mbit/s ESB benchmark application](apps/wireless_benchmark/README.md): separate TX/RX images, a 125 µs data schedule, three encapsulated modules, and 25 RAM summaries plus an all-history running average for debugging.
- [On-board debugging walkthrough](apps/blinky/DEBUGGING.md): program the debug image, start GDB/OpenOCD, set breakpoints, and inspect variables.

The downloaded hardware pack contains the seven-page Seeed schematic, editable KiCad schematic/PCB and STEP archive, pin-definition spreadsheet, KiCad libraries, and Nordic's 1,302-page datasheet. Seeed currently links the same hardware resources for base and Sense variants. Those files retain their vendor licensing and remain unmodified.

Development direction: C/C++ with **nRF Connect SDK, Zephyr, and nRF Connect for VS Code**. Board programming, debugging, and the two-board ESB benchmark work. Next characterize wireless loss, interference, and timing, then the STM32 SPI interface and a USB-HS-capable receiver. This project uses NCS v3.4.1; Seeed's walkthrough uses v3.3.0. Their board integration and version differences are documented in the development guide.

NCS v3.4.1 is installed at `/home/skipper/ncs/v3.4.1` with toolchain bundle `8285d8ad56`. Registration succeeded using that toolchain. The tools work through the Nordic toolchain environment even though `west` is absent from the ordinary shell PATH. `apps/blinky` and its separate debug configuration compile for `xiao_nrf54lm20a@1.0.0/nrf54lm20a/cpuapp` using the pinned Seeed board source. Seeed's OpenOCD is installed locally under `.tools/openocd/`. Programming, verification, breakpoints, source stepping, Run/Debug task integration, and UART output have passed on the connected board.

Primary evidence: [Seeed getting started](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/), [published schematic](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf), [pinned V2.0 overlay](https://github.com/Seeed-Studio/platform-seeedboards/blob/a6fd41debbbcda13b5340fff50c9092ed3289194/zephyr/nrf/boards/seeed/xiao_nrf54lm20a/xiao_nrf54lm20a_nrf54lm20a_cpuapp_2_0_0.overlay), [Nordic installation guide](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/installation/install_ncs.html).
