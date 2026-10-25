#!/usr/bin/env python3
"""Read the XIAO SAMD11 UART bridges without flashing or resetting either Nordic."""

import argparse
from contextlib import ExitStack
from dataclasses import dataclass
import math
from pathlib import Path
import selectors
import sys
import time


ROLE_SERIALS = {"tx": "E785BC0F", "rx": "18B7FED4"}
USB_ID = (0x2886, 0x0068)
BY_ID_DIRECTORY = Path("/dev/serial/by-id")
MAX_LINE_BYTES = 4096


@dataclass(frozen=True)
class Endpoint:
    """Bind a firmware role to the exact supported SAMD11 USB serial device."""

    role: str
    serial: str
    device: str


class BufferedLines:
    """Retain partial UART lines independently, with a fixed limit per bridge."""

    def __init__(self, role):
        """Start an empty line accumulator for one selected board."""
        self.role = role
        self.pending = bytearray()

    def emit(self, data, fragment=False):
        """Print firmware labels unchanged and label untagged boot messages."""
        line = data.rstrip(b"\r").decode("utf-8", errors="replace")
        if not line.startswith(("[TX]", "[RX]")):
            line = f"[{self.role.upper()}] {line}"
        if fragment:
            line += " [monitor: partial line]"
        print(line, flush=True)

    def feed(self, data):
        """Emit complete lines while retaining incomplete bytes across reads."""
        self.pending.extend(data)
        while self.pending:
            newline = self.pending.find(b"\n")
            if 0 <= newline < MAX_LINE_BYTES:
                self.emit(bytes(self.pending[:newline]))
                del self.pending[:newline + 1]
            elif len(self.pending) >= MAX_LINE_BYTES:
                # A disconnected wire or unexpected firmware must not allow an
                # unterminated line to consume unbounded host memory. Preserve
                # its bytes as visibly marked fragments instead of dropping it.
                self.emit(bytes(self.pending[:MAX_LINE_BYTES]), fragment=True)
                del self.pending[:MAX_LINE_BYTES]
            else:
                break

    def finish(self):
        """Preserve an unfinished final line when the monitor stops."""
        if self.pending:
            self.emit(bytes(self.pending), fragment=True)
            self.pending.clear()


def positive_seconds(value):
    """Reject zero, negative, infinite, and nonnumeric capture durations."""
    try:
        seconds = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("duration must be a positive number of seconds") from error
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("duration must be a finite, positive number of seconds")
    return seconds


def argument_parser():
    """Describe role selection and optional bounded capture without importing pyserial."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--role", choices=("tx", "rx", "both"), default="both",
        help="which known board to monitor (default: both)",
    )
    parser.add_argument(
        "--duration", type=positive_seconds, metavar="SECONDS",
        help="stop automatically after this many seconds; otherwise use Ctrl+C",
    )
    return parser


def load_serial_support():
    """Explain the interpreter-specific dependency if pyserial is unavailable."""
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as error:
        raise RuntimeError(
            "This monitor needs pyserial. The VS Code tasks use /usr/bin/python3; "
            "on this Linux system install the python3-serial package for that interpreter."
        ) from error
    if not hasattr(serial, "Serial"):
        raise RuntimeError(
            "The imported 'serial' package is not pyserial. Install python3-serial "
            "for /usr/bin/python3 and check for a local file named serial.py."
        )
    return serial, list_ports


def discover_endpoints(roles, ports, by_id_directory=BY_ID_DIRECTORY):
    """Require one matching VID, PID, and exact USB serial for each requested role."""
    endpoints = []
    for role in roles:
        expected_serial = ROLE_SERIALS[role]
        matching = [
            port for port in ports
            if (port.vid, port.pid) == USB_ID and port.serial_number == expected_serial
        ]
        if len(matching) != 1:
            raise RuntimeError(
                f"Expected exactly one {role.upper()} UART bridge with USB 2886:0068 "
                f"and serial {expected_serial}; found {len(matching)}. "
                "Connect that XIAO with a data-capable USB cable. "
                "Use --role tx or --role rx to monitor just one connected board."
            )
        device = Path(matching[0].device)
        stable = by_id_directory / (
            "usb-Seeed_Studio_Seeed_Studio_XIAO_nRF54LM20A_CMSIS-DAP_"
            f"{expected_serial}-if02"
        )
        # Prefer the persistent name, but never trust a name whose target does
        # not agree with pyserial's independently enumerated USB identity.
        if stable.exists():
            if stable.resolve() != device.resolve():
                raise RuntimeError(f"UART identity mismatch for {stable}; reconnect the XIAO.")
            device = stable
        endpoints.append(Endpoint(role, expected_serial, str(device)))
    return endpoints


def open_endpoint(endpoint, serial_api):
    """Open 115200 8N1 and explicitly assert DTR to enable the SAMD11 bridge."""
    # Set line-control state before open. DTR is a USB-CDC request to the SAMD11
    # bridge, rather than a Nordic reset command; this firmware needs it asserted
    # before the bridge forwards Nordic UART output to the host.
    port = serial_api.Serial(
        port=None, baudrate=115200, bytesize=serial_api.EIGHTBITS,
        parity=serial_api.PARITY_NONE, stopbits=serial_api.STOPBITS_ONE,
        timeout=0, write_timeout=0, xonxoff=False, rtscts=False,
        dsrdtr=False, exclusive=True,
    )
    port.dtr = True
    port.rts = False
    port.port = endpoint.device
    try:
        port.open()
        port.dtr = True
    except (serial_api.SerialException, OSError) as error:
        port.close()
        raise RuntimeError(
            f"Cannot open {endpoint.role.upper()} UART {endpoint.device}: {error}. "
            "Close any other serial monitor using this board. If access is denied, "
            "check the project's installed XIAO udev rule."
        ) from error
    return port


def monitor(endpoints, serial_api, duration=None):
    """Read both UARTs concurrently without waiting for either one to finish a line."""
    buffers = {endpoint.role: BufferedLines(endpoint.role) for endpoint in endpoints}
    with ExitStack() as stack:
        selector = stack.enter_context(selectors.DefaultSelector())
        for endpoint in endpoints:
            port = stack.enter_context(open_endpoint(endpoint, serial_api))
            selector.register(port, selectors.EVENT_READ, endpoint)
            print(
                f"Monitoring {endpoint.role.upper()} {endpoint.serial} at {endpoint.device} "
                "(115200 8N1, DTR asserted).",
                file=sys.stderr, flush=True,
            )
        print("Press Ctrl+C to stop; neither board will be reset.", file=sys.stderr, flush=True)
        deadline = None if duration is None else time.monotonic() + duration
        try:
            while True:
                remaining = None if deadline is None else deadline - time.monotonic()
                if remaining is not None and remaining <= 0:
                    break
                # A selector watches both nonblocking UART descriptors. A silent
                # RX or a half-written TX line cannot delay output from its peer.
                timeout = 0.25 if remaining is None else min(0.25, remaining)
                for key, _ in selector.select(timeout):
                    endpoint = key.data
                    try:
                        data = key.fileobj.read(4096)
                    except (serial_api.SerialException, OSError) as error:
                        raise RuntimeError(
                            f"Lost {endpoint.role.upper()} UART {endpoint.device}: {error}"
                        ) from error
                    if not data:
                        raise RuntimeError(
                            f"{endpoint.role.upper()} UART {endpoint.device} disconnected."
                        )
                    buffers[endpoint.role].feed(data)
        finally:
            for buffer in buffers.values():
                buffer.finish()


def main(argv=None):
    """Validate identities, monitor selected boards, and close cleanly on Ctrl+C."""
    arguments = argument_parser().parse_args(argv)
    roles = ("tx", "rx") if arguments.role == "both" else (arguments.role,)
    try:
        serial_api, list_ports = load_serial_support()
        endpoints = discover_endpoints(roles, list(list_ports.comports()))
        monitor(endpoints, serial_api, arguments.duration)
    except KeyboardInterrupt:
        print("Serial monitor stopped.", file=sys.stderr, flush=True)
    except RuntimeError as error:
        print(f"Serial monitor: {error}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
