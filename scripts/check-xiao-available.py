#!/usr/bin/env python3
"""Catch an active debug server before another operation tries to use the probe."""

import errno
import socket
import sys

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
    probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        probe.bind(("127.0.0.1", 3333))
    except OSError as error:
        if error.errno != errno.EADDRINUSE:
            raise
        print(
            "The XIAO debug port (localhost:3333) is already in use.\n"
            "Stop the current debug session with Shift+F5, then start Run or Debug again.\n"
            "To continue the current paused session instead, press F5.",
            file=sys.stderr,
        )
        sys.exit(1)
