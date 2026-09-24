#!/usr/bin/env python3
"""Bridge Windows TCP to the host-networked RBY1 simulator in Docker Desktop."""

from __future__ import annotations

import shutil
import socket
import socketserver
import subprocess
import threading


LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = 55051
DOCKER_CONTAINER = "rby1-ros2"

REMOTE_RELAY = r"""
import socket
import sys
import threading

peer = socket.create_connection(("127.0.0.1", 50051), timeout=5)
peer.settimeout(None)

def upload():
    try:
        while True:
            data = sys.stdin.buffer.read1(65536)
            if not data:
                break
            peer.sendall(data)
    except (BrokenPipeError, ConnectionError, OSError):
        pass
    try:
        peer.shutdown(socket.SHUT_WR)
    except OSError:
        pass

threading.Thread(target=upload, daemon=True).start()
try:
    while True:
        data = peer.recv(65536)
        if not data:
            break
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
except (BrokenPipeError, ConnectionError, OSError):
    pass
finally:
    peer.close()
"""


class ProxyHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        docker = shutil.which("docker")
        if not docker:
            return

        creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        process = subprocess.Popen(
            [docker, "exec", "-i", DOCKER_CONTAINER, "python3", "-u", "-c", REMOTE_RELAY],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            creationflags=creation_flags,
        )

        def upload() -> None:
            assert process.stdin is not None
            try:
                while data := self.request.recv(65536):
                    process.stdin.write(data)
                    process.stdin.flush()
            except (BrokenPipeError, ConnectionError, OSError):
                pass
            finally:
                try:
                    process.stdin.close()
                except OSError:
                    pass

        thread = threading.Thread(target=upload, daemon=True)
        thread.start()
        assert process.stdout is not None
        try:
            while data := process.stdout.read1(65536):
                self.request.sendall(data)
        except (BrokenPipeError, ConnectionError, OSError):
            pass
        finally:
            process.terminate()
            process.wait(timeout=5)


class ProxyServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with ProxyServer((LISTEN_HOST, LISTEN_PORT), ProxyHandler) as server:
        server.serve_forever()
