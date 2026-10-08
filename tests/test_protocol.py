import os
import pathlib
import socket
import struct
import subprocess
import tempfile
import time
import unittest

BASE = pathlib.Path(__file__).resolve().parents[1]


def frame(kind, flags, ident, payload=b""):
    return b"BH" + bytes((1, kind, flags, 0)) + struct.pack("!IH", len(payload), ident) + payload


def request(ident, path):
    raw = path.encode("ascii")
    return frame(1, 1, ident, b"\x01" + struct.pack("!H", len(raw)) + raw + b"\x00")


def read_exact(sock, n):
    data = bytearray()
    while len(data) < n:
        part = sock.recv(n - len(data))
        if not part:
            raise EOFError("short frame")
        data.extend(part)
    return bytes(data)


def read_frame(sock):
    head = read_exact(sock, 12)
    assert head[:3] == b"BH\x01"
    length, ident = struct.unpack("!IH", head[6:])
    return head[3], head[4], ident, read_exact(sock, length)


def read_response(sock, ident):
    kind, flags, got, payload = read_frame(sock)
    assert (kind, flags, got) == (2, 0, ident)
    status = struct.unpack("!H", payload[:2])[0]
    body = bytearray()
    while True:
        kind, flags, got, payload = read_frame(sock)
        assert (kind, got) == (3, ident)
        body.extend(payload)
        if flags == 1:
            return status, bytes(body)


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name) / "www"
        cls.root.mkdir()
        (cls.root / "index.html").write_bytes(b"Hi\n")
        (cls.root / "binary.bin").write_bytes(bytes(range(256)) * 600)
        (pathlib.Path(cls.tmp.name) / "secret").write_bytes(b"secret")
        (cls.root / "link").symlink_to(pathlib.Path(cls.tmp.name) / "secret")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            cls.port = probe.getsockname()[1]
        cls.server = subprocess.Popen([str(BASE / "bserve"), str(cls.root), str(cls.port)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.01)
        else:
            raise RuntimeError("server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        cls.server.communicate(timeout=3)
        cls.tmp.cleanup()

    def connect(self):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=3)
        s.settimeout(3)
        return s

    def test_persistent_partial_and_unknown_frame(self):
        with self.connect() as s:
            first = request(7, "/index.html")
            for byte in first:
                s.sendall(bytes((byte,)))
            self.assertEqual(read_response(s, 7), (200, b"Hi\n"))
            s.sendall(frame(99, 0xff, 999, b"ignored\x00bytes") + request(8, "/binary.bin"))
            self.assertEqual(read_response(s, 8), (200, bytes(range(256)) * 600))
            s.sendall(request(9, "/"))
            self.assertEqual(read_response(s, 9), (200, b"Hi\n"))

    def test_errors_continue_and_traversal(self):
        with self.connect() as s:
            s.sendall(request(1, "/missing") + request(2, "/../secret") +
                      request(3, "/link") + frame(1, 1, 4, b"bad") +
                      request(5, "/missing/../secret") + request(6, "/index.html"))
            self.assertEqual(read_response(s, 1)[0], 404)
            self.assertEqual(read_response(s, 2)[0], 400)
            self.assertEqual(read_response(s, 3)[0], 404)
            self.assertEqual(read_response(s, 4)[0], 400)
            self.assertEqual(read_response(s, 5)[0], 400)
            self.assertEqual(read_response(s, 6), (200, b"Hi\n"))

    def test_invalid_fixed_header_closes(self):
        with self.connect() as s:
            s.sendall(b"XX\x01\x01\x01\x00" + struct.pack("!IH", 0, 5))
            self.assertEqual(read_response(s, 0)[0], 400)
            self.assertEqual(s.recv(1), b"")

    def test_client_binary_verbose_and_exit(self):
        target = f"127.0.0.1:{self.port}"
        result = subprocess.run([str(BASE / "bcurl"), "-v", target + "/index.html",
                                 target + "/binary.bin"], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"Hi\n" + bytes(range(256)) * 600)
        self.assertIn(b"send frame type=1", result.stderr)
        self.assertIn(b"recv frame type=3", result.stderr)
        missing = subprocess.run([str(BASE / "bcurl"), target + "/missing"],
                                 capture_output=True, timeout=5)
        self.assertEqual(missing.returncode, 1)
        self.assertEqual(missing.stdout, b"404 Not Found\n")


if __name__ == "__main__":
    unittest.main()
