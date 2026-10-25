#!/usr/bin/env python3
"""Build and program the two-role XIAO ESB benchmark without guessing a probe."""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass


REPOSITORY = Path(__file__).resolve().parent.parent
APPLICATION = REPOSITORY / "apps/wireless_benchmark"
SDK = Path("/home/skipper/ncs/v3.4.1")
TOOLCHAIN = Path("/home/skipper/ncs/toolchains/8285d8ad56")
SDK_MANAGER = Path(
    "/home/skipper/.vscode/extensions/"
    "nordic-semiconductor.nrf-connect-2026.9.2823-linux-x64/"
    "platform/nrfutil/bin/nrfutil-sdk-manager"
)
OPENOCD = REPOSITORY / ".tools/openocd/bin/openocd"
OPENOCD_SCRIPTS = REPOSITORY / ".tools/openocd/share/openocd/scripts"
BOARD_CONFIGURATION = REPOSITORY / (
    "third_party/platform-seeedboards/zephyr/nrf/boards/seeed/"
    "xiao_nrf54lm20a/support/openocd.cfg"
)
BOARD_TARGET = "xiao_nrf54lm20a@1.0.0/nrf54lm20a/cpuapp"


@dataclass(frozen=True)
class Probe:
    """Identify the SAMD11 CMSIS-DAP probe on one supported XIAO board."""

    serial: str
    usb_path: str


def connected_probes(usb_root=Path("/sys/bus/usb/devices")):
    """Read USB identities; unrelated J-Link, ST-Link, and other probes are ignored."""
    probes = []
    for device in sorted(usb_root.iterdir()):
        try:
            vendor = (device / "idVendor").read_text().strip().lower()
            product = (device / "idProduct").read_text().strip().lower()
            if (vendor, product) != ("2886", "0068"):
                continue
            serial = (device / "serial").read_text().strip()
        except (FileNotFoundError, PermissionError, NotADirectoryError):
            # USB interfaces lack device attributes, and devices may disappear
            # while this read-only enumeration is in progress.
            continue
        probes.append(Probe(serial=serial, usb_path=device.name))
    return probes


def select_probe(serial):
    """Require an exact connected serial whenever selecting a board is ambiguous."""
    probes = connected_probes()
    if not probes:
        raise RuntimeError("No XIAO CMSIS-DAP probe (USB 2886:0068) is connected.")
    if serial:
        matching = [probe for probe in probes if probe.serial == serial]
        if len(matching) != 1:
            raise RuntimeError(
                f"Serial {serial!r} does not identify exactly one connected XIAO. "
                "Run this helper's probes command and copy its serial exactly."
            )
        selected = matching[0]
    elif len(probes) == 1:
        selected = probes[0]
    else:
        raise RuntimeError(
            "Multiple XIAO boards are connected. Supply --serial with the intended "
            "board's exact serial; run this helper's probes command to list them."
        )
    # OpenOCD -c arguments are Tcl programs even though subprocess avoids a shell.
    # Refuse unusual serial characters rather than interpolate untrusted Tcl.
    if not re.fullmatch(r"[A-Za-z0-9_.:-]+", selected.serial):
        raise RuntimeError("The selected probe has an empty or unsupported USB serial.")
    print(
        f"Selected XIAO serial {selected.serial} at USB {selected.usb_path}.",
        flush=True,
    )
    return selected


def require_paths(*paths):
    """Fail early with a useful path when an installed dependency is missing."""
    for path in paths:
        if not path.exists():
            raise RuntimeError(f"Required path is missing: {path}")


def build_directory(role):
    """Keep transmitter and receiver output separate so their images cannot collide."""
    return APPLICATION / f"build-{role}"


def tool_environment():
    """Pin the vendor board script to CMSIS-DAP even if a shell selected J-Link."""
    environment = os.environ.copy()
    environment["OPENOCD_INTERFACE"] = "cmsis-dap"
    return environment


def run_west(arguments):
    """Run west with the same Nordic toolchain used by the verified blinky tasks."""
    require_paths(SDK_MANAGER, SDK, TOOLCHAIN)
    command = [
        str(SDK_MANAGER), "toolchain", "launch", "--toolchain-path", str(TOOLCHAIN),
        "--log-output", "stdout", "--log-level", "off", "--", "west", *arguments,
    ]
    # shlex.join is for readable output only; execution always uses an argv list.
    print(shlex.join(command), flush=True)
    return subprocess.call(command, cwd=SDK, env=tool_environment())


def build(role):
    """Build one fixed radio role with debug symbols and the V1.0 board definition."""
    require_paths(APPLICATION / f"{role}.conf", OPENOCD, BOARD_CONFIGURATION)
    return run_west([
        "build", "--no-sysbuild", "-p", "auto", "-b", BOARD_TARGET,
        "-s", str(APPLICATION), "-d", str(build_directory(role)), "--",
        f"-DEXTRA_CONF_FILE={role}.conf", f"-DOPENOCD={OPENOCD}",
    ])


def clean(role):
    """Remove compiled outputs for one role while retaining its CMake configuration."""
    directory = build_directory(role)
    if not (directory / "CMakeCache.txt").is_file():
        print(f"No configured {role.upper()} build to clean: {directory}")
        return 0
    return run_west(["build", "--no-sysbuild", "-d", str(directory), "-t", "clean"])


def flash(role, serial):
    """Verify and run an existing role image on the explicitly selected XIAO."""
    probe = select_probe(serial)
    directory = build_directory(role)
    require_paths(directory / "zephyr/zephyr.hex", OPENOCD, OPENOCD_SCRIPTS)
    # Keep the vendor HEX loader: it commits the last Nordic RRAM write buffer.
    # The board file's automatic erase/unlock hook is explicitly disabled.
    return run_west([
        "flash", "--no-rebuild", "-d", str(directory), "-r", "openocd",
        "--openocd", str(OPENOCD), "--openocd-search", str(OPENOCD_SCRIPTS),
        "--cmd-pre-init", f"adapter serial {probe.serial}",
        "--cmd-pre-init", "nrf54lm20a.cpu configure -event examine-fail {}",
        "--verify",
    ])


def debug_server(role, serial, attach=False):
    """Start a local-only OpenOCD server, optionally preserving the running image."""
    probe = select_probe(serial)
    require_paths(
        build_directory(role) / "zephyr/zephyr.elf", OPENOCD,
        OPENOCD_SCRIPTS, BOARD_CONFIGURATION,
    )
    command = [
        str(OPENOCD), "-s", str(OPENOCD_SCRIPTS), "-f", str(BOARD_CONFIGURATION),
    ]
    for setting in [
        f"adapter serial {probe.serial}",
        "nrf54lm20a.cpu configure -event examine-fail {}",
        "bindto 127.0.0.1", "gdb_port 3333", "tcl_port disabled",
        "telnet_port disabled", "gdb_memory_map disable", "gdb_flash_program disable",
        "nrf54lm20a.cpu configure -event gdb-detach {resume}", "init",
    ]:
        command.extend(["-c", setting])
    # Attaching must leave completed RAM results intact. A GDB connection may
    # briefly halt execution, but it must never restart startup code or main().
    if not attach:
        command.extend(["-c", "reset halt"])
    print(f"Starting {role.upper()} debug server: {shlex.join(command)}", flush=True)
    # exec preserves the server PID. When VS Code ends debugging, it therefore
    # terminates OpenOCD itself instead of leaving an orphan holding the probe.
    os.execve(str(OPENOCD), command, tool_environment())


def parse_arguments():
    """Expose small, explicit commands rather than accepting arbitrary west options."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("probes", help="list supported connected XIAO probe serials")
    for name in ("build", "clean", "flash", "debug-server"):
        command = commands.add_parser(name)
        command.add_argument("--role", choices=("tx", "rx"), required=True)
        if name in ("flash", "debug-server"):
            command.add_argument(
                "--serial", default="",
                help="exact XIAO USB serial; may be empty only with one XIAO connected",
            )
        if name == "debug-server":
            command.add_argument(
                "--attach", action="store_true",
                help="preserve running firmware and RAM; do not reset or halt at startup",
            )
    return parser.parse_args()


def main():
    """Dispatch the requested operation and report expected setup errors concisely."""
    arguments = parse_arguments()
    try:
        if arguments.command == "probes":
            probes = connected_probes()
            if not probes:
                print("No XIAO CMSIS-DAP probes (USB 2886:0068) are connected.")
            for probe in probes:
                print(f"{probe.serial or '<missing serial>'}\tUSB {probe.usb_path}")
            return 0
        if arguments.command == "build":
            return build(arguments.role)
        if arguments.command == "clean":
            return clean(arguments.role)
        if arguments.command == "flash":
            return flash(arguments.role, arguments.serial)
        debug_server(arguments.role, arguments.serial, attach=arguments.attach)
        return 0
    except (OSError, RuntimeError) as error:
        print(f"Wireless benchmark: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
