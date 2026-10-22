# Official documentation and software index

Collected October 20, 2026. This indexes the public manufacturer material found for the XIAO nRF54LM20A and the Nordic references needed for this project. Online documentation can change; versioned source links and downloaded files provide reproducible evidence.

## Seeed board documentation

The English wiki currently exposes these eleven pages. Many titles say Sense while their instructions also discuss the base board; sensor-specific examples require the corresponding hardware.

| Page | Purpose |
| --- | --- |
| [Getting started](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/) | Board overview, PlatformIO, hardware downloads |
| [nRF Connect SDK](https://wiki.seeedstudio.com/xiao_nrf54lm20a_ncs/) | VS Code, SDK, custom board root, programming |
| [Pin multiplexing](https://wiki.seeedstudio.com/xiao_nrf54lm20a_pin_multiplexing/) | GPIO, ADC, UART, I2C, SPI, PWM |
| [Onboard sensors](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/) | Sense IMU and microphone |
| [Low power](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_low_power/) | Sleep and PMIC examples |
| [Bluetooth LE](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_bluetooth_lowpower/) | Advertising and two-board communication |
| [Matter](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_matter/) | Matter application example |
| [NFC](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_nfc/) | NFC usage |
| [E-paper and microSD](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_epaper_and_sdcard/) | Peripheral examples |
| [MicroPython](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_micropython/) | Python firmware workflow |
| [Arduino](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_arduino/) | Community Arduino core workflow |

## Downloaded hardware material

| Resource | Local copy | Manufacturer download |
| --- | --- | --- |
| V1.0 schematic, seven sheets | [PDF](vendor/seeed/XIAO_nRF54LM20A_Schematic.pdf) | [Seeed PDF](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf) |
| V1.0 KiCad schematic, PCB, project and STEP | [ZIP](vendor/seeed/XIAO_nRF54LM20A_V1.0_SCH_PCB_260508.zip) | [Seeed ZIP](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_V1.0_SCH&PCB_260508.zip) |
| Pin definitions | [XLSX](vendor/seeed/XIAO_nRF54LM20A_Pin_definition.xlsx) | [Seeed spreadsheet](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Pin_definition.xlsx) |
| XIAO footprints | [ZIP](vendor/seeed/New_XIAO_Series_Footprints.zip) | [Seeed ZIP](https://files.seeedstudio.com/wiki/XIAO-KiCad-Library/New_XIAO_Series_Footprints.zip) |
| XIAO schematic symbols | [ZIP](vendor/seeed/XIAO_Series_SCH_Symbols.zip) | [Seeed ZIP](https://files.seeedstudio.com/wiki/XIAO-KiCad-Library/XIAO_Series_SCH_Symbols.zip) |
| nRF54LM20A/B datasheet v1.0 | [PDF](vendor/nordic/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf) | [Seeed-hosted Nordic PDF](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf) |

The wiki's base/Sense download sections currently resolve to the same files. The schematic is labeled V1.0, April 9, 2026; the KiCad archive filename ends in `260508`. Neither establishes V2.0 wiring. Consult the [pinned official V2.0 source](vendor/seeed/platform-seeedboards-a6fd41d/SOURCE.md) for that revision's documented changes, pending its hardware files.

## Nordic silicon and reference hardware

| Reference | Purpose |
| --- | --- |
| [nRF54LM20A product](https://www.nordicsemi.com/Products/nRF54LM20A) | Capabilities and downloads |
| [Documentation hub](https://docs.nordicsemi.com/category/nRF54LM20A-category) | Manufacturer document collection |
| [Online datasheet](https://docs.nordicsemi.com/r/bundle/ps_nrf54lm20a/) | Registers, pin assignments and electrical limits |
| [USBHS chapter](https://docs.nordicsemi.com/r/bundle/ps_nrf54lm20a/page/usbhs.html) | Native USB peripheral |
| [Revision identification](https://docs.nordicsemi.com/r/bundle/comp_matrix_nrf54lm20a/page/comp/nrf54lm20a/nrf54lm20a_ic_revision_overview.html) | Determine silicon revision |
| [Documentation/design compatibility](https://docs.nordicsemi.com/r/bundle/comp_matrix_nrf54lm20a/page/comp/nrf54lm20a/nrf54lm20a_doc_ref_design_files_overview.html) | Match datasheet, errata and reference design |
| [SDK compatibility](https://docs.nordicsemi.com/r/bundle/comp_matrix_nrf54lm20a/page/comp/nrf54lm20a/nrf54lm20a_nrf_connect_sdk.html) | Supported SDK/silicon combinations |
| [Revision 1 errata](https://docs.nordicsemi.com/r/bundle/errata_nrf54lm20a_rev1/page/err/nrf54lm20a/rev1/latest/err_20a.html) | Production silicon anomalies |
| [Engineering B errata](https://docs.nordicsemi.com/r/bundle/errata_nrf54lm20a_engb/page/err/nrf54lm20a/engineeringb/latest/err_20a.html) | Engineering silicon anomalies |
| [Reference circuitry/layout](https://www.nordicsemi.com/Products/nRF54LM20A/Reference-layout) | Custom PCB design |
| [nRF54LM20 DK manual](https://docs.nordicsemi.com/r/bundle/ug_nrf54lm20_dk/) | Native USB reference hardware |
| [DK hardware files](https://www.nordicsemi.com/Products/Development-hardware/nRF54LM20-DK/Hardware-files) | DK schematic and layout |
| [DK revision compatibility](https://docs.nordicsemi.com/r/bundle/comp_matrix_nrf54lm20a/page/comp/nrf54lm20a/nrf54lm20a_ic_rev_comp_with_dev_hw.html) | Some newer DKs use LM20B with LM20A emulation |
| [nPM1300 specification](https://docs.nordicsemi.com/r/bundle/ps_npm1300/page/keyfeatures_html5.html) | PMIC, rails, charging and low power |

## Development software

| Software | Role |
| --- | --- |
| [nRF Connect SDK installation](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/installation/install_ncs.html) | SDK, Zephyr, compiler and matching tools |
| [nRF Connect for VS Code](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-VS-Code) | Build, configuration and debugging interface |
| [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util) | SDK management and device utilities |
| [Seeed board package](https://github.com/Seeed-Studio/platform-seeedboards) | External NCS board definitions and PlatformIO platform |
| [Seeed MicroPython](https://github.com/Seeed-Studio/micropython-seeed-boards) | Alternative for peripheral experiments |
| [Community Arduino core](https://github.com/lolren/nrf54-arduino-core) | Linked by Seeed; not the baseline recommended for native USB-HS work |
| [nRF Connect for Mobile](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) | Scan, connect and inspect BLE services |
| [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-desktop) | Optional testing and measurement applications |
| [Nordic Developer Academy](https://academy.nordicsemi.com/courses/nrf-connect-sdk-fundamentals/) | SDK fundamentals course |

Use the modern VS Code/SDK manager installation instructions. The legacy Desktop Toolchain Manager does not support NCS v3.0.0 onward, according to [Nordic's download page](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-desktop/Download).

## Mouse, transceiver and USB source references

| Starting point | Intended experiment |
| --- | --- |
| [BLE HIDS mouse, NCS v3.4.1](https://github.com/nrfconnect/sdk-nrf/tree/v3.4.1/samples/bluetooth/peripheral_hids_mouse) | Standard Bluetooth mouse |
| [Central HIDS](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/samples/bluetooth/central_hids/README.html) | Receive Bluetooth HID reports |
| [ESB PTX](https://github.com/nrfconnect/sdk-nrf/tree/v3.4.1/samples/esb/esb_ptx) / [PRX](https://github.com/nrfconnect/sdk-nrf/tree/v3.4.1/samples/esb/esb_prx) | Proprietary two-way packet link |
| [LLPM](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/samples/bluetooth/llpm/README.html) | Nordic low-latency BLE extension |
| [nRF Desktop stable board configuration](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/applications/nrf_desktop/board_configuration.rst) | Integrated HID architecture; LM20 DK mouse reference |
| [HID Add-on setup](https://nrfconnectdocs.nordicsemi.com/addons/addon-hid/latest/setup.html) / [board configurations](https://nrfconnectdocs.nordicsemi.com/addons/addon-hid/latest/applications/nrf_desktop/board_configuration.html) | Newer mouse/dongle reference; follow its matching release requirements |
| [USB HID mouse sample, NCS's Zephyr tag](https://github.com/nrfconnect/sdk-zephyr/tree/ncs-v3.4.1/samples/subsys/usb/hid-mouse) | Minimal native USB HID starting point |
| [Zephyr USB stack](https://docs.zephyrproject.org/latest/connectivity/usb/device_next/usb_device.html) | Device stack and custom classes |
| [HID devicetree binding](https://docs.zephyrproject.org/latest/build/dts/api/bindings/usb/zephyr%2Chid-device.html) | Report lengths and polling periods |

The DK sample targets are verified in source; adapting them to the XIAO is project work, not a verified turnkey build. `latest` documentation can describe development code. In particular, stable NCS v3.4.1's LM20 nRF Desktop configuration is a mouse; newer HID Add-on documentation also describes dongles.
