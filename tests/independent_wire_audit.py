"""Black-box BHP/1 conformance tests; wire encoding comes only from PROTOCOL_SPEC.md."""

import os
import pathlib
import re
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest


BASE = pathlib.Path(__file__).resolve().parents[1]
NAMES = (
    "content-type", "content-length", "host", "accept", "user-agent",
    "server", "cache-control", "etag", "last-modified", "connection",
)


def headers(entries):
    result = bytearray([len(entries)])
    for name, value in entries:
        code = NAMES.index(name) + 1 if name in NAMES else 0
        result.append(code)
        if not code:
            encoded = name.encode("ascii")
            result.extend((len(encoded),))
            result.extend(encoded)
        encoded = value.encode("ascii")
        result.extend(struct.pack("!H", len(encoded)))
        result.extend(encoded)
    return bytes(result)


def frame(kind, flags, ident, payload=b"", length=None):
    if length is None:
        length = len(payload)
    return b"BH" + bytes((1, kind, flags, 0)) + struct.pack("!IH", length, ident) + payload


def request(ident, path, extra=()):
    path = path.encode("ascii")
    return frame(1, 1, ident, b"\x01" + struct.pack("!H", len(path)) + path + headers(extra))


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise EOFError(f"needed {size}, got {len(data)}")
        data.extend(part)
    return bytes(data)


def read_frame(sock):
    head = recv_exact(sock, 12)
    if head[:3] != b"BH\x01" or head[5] != 0:
        raise AssertionError(f"bad fixed header: {head.hex()}")
    size, ident = struct.unpack("!IH", head[6:12])
    if size > 1_048_576:
        raise AssertionError("oversized received frame")
    return head[3], head[4], ident, recv_exact(sock, size)


def decode_headers(data):
    if not data:
        raise AssertionError("missing header count")
    pos = 1
    result = []
    for _ in range(data[0]):
        code = data[pos]
        pos += 1
        if code == 0:
            size = data[pos]
            pos += 1
            name = data[pos:pos + size].decode("ascii")
            pos += size
        else:
            if not 1 <= code <= 10:
                raise AssertionError("invalid name code")
            name = NAMES[code - 1]
        size = struct.unpack("!H", data[pos:pos + 2])[0]
        pos += 2
        value = data[pos:pos + size]
        pos += size
        result.append((name, value))
    if pos != len(data):
        raise AssertionError("header block boundary mismatch")
    return result


def read_response(sock, ident):
    kind, flags, got, payload = read_frame(sock)
    if (kind, flags, got) != (2, 0, ident):
        raise AssertionError("wrong RESPONSE header")
    status = struct.unpack("!H", payload[:2])[0]
    fields = dict(decode_headers(payload[2:]))
    body = bytearray()
    while True:
        kind, flags, got, chunk = read_frame(sock)
        if kind != 3 or got != ident or flags not in (0, 1):
            raise AssertionError("wrong DATA header")
        body.extend(chunk)
        if flags == 1:
            break
    if int(fields["content-length"]) != len(body):
        raise AssertionError("wrong content-length")
    return status, fields, bytes(body)


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def dump_bytes(block):
    result = bytearray()
    for line in block.splitlines():
        match = re.match(r"^\s+([0-9a-f]{4})\s+(.*)$", line)
        if not match:
            continue
        if int(match[1], 16) != len(result):
            raise AssertionError("non-contiguous hexdump offsets")
        left = match[2].split("|")[0]
        result.extend(int(value, 16) for value in re.findall(r"\b[0-9a-f]{2}\b", left))
    return bytes(result)


class ServerWireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temp.name) / "www"
        cls.root.mkdir()
        cls.html = b"<h1>Hello</h1>\n"
        cls.binary = bytes(range(256)) * 271
        cls.large = os.urandom(2_097_241)
        for name, body in (("index.html", cls.html), ("empty", b""),
                           ("binary.bin", cls.binary), ("large.bin", cls.large)):
            (cls.root / name).write_bytes(body)
        (pathlib.Path(cls.temp.name) / "secret").write_bytes(b"secret")
        (cls.root / "link").symlink_to(pathlib.Path(cls.temp.name) / "secret")
        os.mkfifo(cls.root / "pipe")
        cls.port = port()
        cls.server = subprocess.Popen(
            [str(BASE / "bserve"), str(cls.root), str(cls.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", cls.port), 0.1):
                    break
            except OSError:
                time.sleep(0.01)
        else:
            raise RuntimeError("server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        cls.server.communicate(timeout=3)
        cls.temp.cleanup()

    def connect(self):
        sock = socket.create_connection(("127.0.0.1", self.port), 2)
        sock.settimeout(2)
        return sock

    def test_success_binary_large_empty_and_indexed_headers(self):
        with self.connect() as sock:
            for ident, path, expected in (
                (1, "/index.html", self.html), (2, "/empty", b""),
                (3, "/binary.bin", self.binary), (4, "/large.bin", self.large),
            ):
                sock.sendall(request(ident, path, (("host", "localhost"),
                                                   ("x-audit", "yes"))))
                status, fields, body = read_response(sock, ident)
                self.assertEqual(status, 200)
                self.assertEqual(body, expected)
                self.assertEqual(int(fields["content-length"]), len(expected))
                self.assertIn(b"/", fields["content-type"])

    def test_errors_unknown_and_fragmented_request_on_one_connection(self):
        with self.connect() as sock:
            for byte in request(10, "/missing"):
                sock.sendall(bytes([byte]))
            self.assertEqual(read_response(sock, 10)[0], 404)
            sock.sendall(frame(241, 255, 0, b"uninterpreted") +
                         frame(1, 1, 11, b"bad") + request(12, "/index.html"))
            self.assertEqual(read_response(sock, 11)[0], 400)
            self.assertEqual(read_response(sock, 12)[2], self.html)

    def test_traversal_and_symlink(self):
        with self.connect() as sock:
            for ident, path, expected in (
                (20, "/../secret", 400), (21, "/foo/../../secret", 400),
                (22, "/%2e%2e/secret", 400), (23, "/link", 404),
                (24, "/a//b", 400),
            ):
                sock.sendall(request(ident, path))
                self.assertEqual(read_response(sock, ident)[0], expected)

    def test_nonregular_fifo_does_not_block(self):
        with self.connect() as sock:
            sock.sendall(request(25, "/pipe"))
            self.assertEqual(read_response(sock, 25)[0], 404)

    def test_no_file_descriptor_growth_across_repeated_requests(self):
        fd_dir = pathlib.Path(f"/proc/{self.server.pid}/fd")
        if not fd_dir.exists():
            self.skipTest("Linux /proc file descriptors unavailable")
        with self.connect() as sock:
            before = len(list(fd_dir.iterdir()))
            for ident in range(100, 200):
                sock.sendall(request(ident, "/index.html"))
                self.assertEqual(read_response(sock, ident)[0], 200)
            after = len(list(fd_dir.iterdir()))
            self.assertEqual(after, before)

    def test_invalid_length_fixed_header_and_disconnect(self):
        with self.connect() as sock:
            sock.sendall(frame(1, 1, 5, length=1_048_577))
            self.assertEqual(read_response(sock, 0)[0], 400)
            self.assertEqual(sock.recv(1), b"")
        with self.connect() as sock:
            sock.sendall(b"XX\x01\x01\x01\x00" + struct.pack("!IH", 0, 5))
            self.assertEqual(read_response(sock, 0)[0], 400)
            self.assertEqual(sock.recv(1), b"")
        with self.connect() as sock:
            sock.sendall(frame(1, 1, 7, b"\x01", length=6))
            sock.shutdown(socket.SHUT_WR)
            self.assertEqual(sock.recv(1), b"")
        with self.connect() as sock:
            sock.sendall(request(8, "/index.html"))
            self.assertEqual(read_response(sock, 8)[0], 200)

    def test_idle_peer_does_not_block_server_forever(self):
        with self.connect() as idle:
            with self.connect() as active:
                active.settimeout(5)
                active.sendall(request(30, "/index.html"))
                self.assertEqual(read_response(active, 30)[0], 200)


class ClientWireTests(unittest.TestCase):
    def fake_server(self, handler):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(5)
        errors = []

        def run():
            try:
                with listener:
                    with listener.accept()[0] as conn:
                        conn.settimeout(5)
                        handler(conn)
                    listener.settimeout(0.2)
                    try:
                        extra, _ = listener.accept()
                    except socket.timeout:
                        pass
                    else:
                        extra.close()
                        errors.append("client opened a second connection")
            except Exception as exc:
                errors.append(repr(exc))

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        return listener.getsockname()[1], thread, errors

    def run_client(self, port, *paths, verbose=False, timeout=7):
        args = [str(BASE / "bcurl")]
        if verbose:
            args.append("-v")
        args += [f"127.0.0.1:{port}{p}" for p in paths]
        return subprocess.run(args, capture_output=True, timeout=timeout)

    def test_client_one_connection_wire_format_hexdump_and_unknown(self):
        captured = []

        def handler(conn):
            for ident in (1, 2):
                kind, flags, got, payload = read_frame(conn)
                self.assertEqual((kind, flags, got), (1, 1, ident))
                self.assertEqual(payload[0], 1)
                size = struct.unpack("!H", payload[1:3])[0]
                self.assertEqual(payload[3:3 + size], b"/index.html")
                fields = dict(decode_headers(payload[3 + size:]))
                self.assertEqual(fields["host"], b"127.0.0.1")
                self.assertEqual(fields["accept"], b"*/*")
                captured.append(payload)
                body = b"Hi\n"
                response = frame(2, 0, ident, struct.pack("!H", 200) +
                                 headers((("content-length", "3"),)))
                conn.sendall(frame(240, 255, 0, b"skip") + response +
                             frame(3, 1, ident, body))

        port_num, thread, errors = self.fake_server(handler)
        result = self.run_client(port_num, "/index.html", "/index.html", verbose=True)
        thread.join(6)
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"Hi\nHi\n")
        self.assertEqual(len(captured), 2)
        self.assertIn(b"0000  42 48 01 01", result.stderr)
        self.assertIn(b"recv frame type=240", result.stderr)

    def test_verbose_dump_equals_bytes_seen_by_independent_peer(self):
        observed = []

        def handler(conn):
            head = recv_exact(conn, 12)
            size = struct.unpack("!I", head[6:10])[0]
            observed.append(head + recv_exact(conn, size))
            ident = struct.unpack("!H", head[10:12])[0]
            response = frame(2, 0, ident, struct.pack("!H", 200) +
                             headers((("content-length", "3"),)))
            data = frame(3, 1, ident, b"Hi\n")
            observed.extend((response, data))
            conn.sendall(response + data)

        port_num, thread, errors = self.fake_server(handler)
        result = self.run_client(port_num, "/index.html", verbose=True)
        thread.join(6)
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 0, result.stderr)
        blocks = re.split(r"(?=^(?:send|recv) frame type=)", result.stderr.decode(), flags=re.M)
        dumped = [dump_bytes(block) for block in blocks if block.startswith(("send", "recv"))]
        self.assertEqual(dumped, observed)

    def test_client_4xx_and_5xx_exit(self):
        for status in (400, 404, 503):
            def handler(conn):
                kind, flags, ident, _ = read_frame(conn)
                self.assertEqual((kind, flags), (1, 1))
                conn.sendall(frame(2, 0, ident, struct.pack("!H", status) +
                                   headers((("content-length", "0"),))) +
                             frame(3, 1, ident))

            port_num, thread, errors = self.fake_server(handler)
            result = self.run_client(port_num, "/index.html")
            thread.join(6)
            self.assertFalse(errors, errors)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, b"")

    def test_client_rejects_truncated_response(self):
        def handler(conn):
            _, _, ident, _ = read_frame(conn)
            conn.sendall(frame(2, 0, ident, struct.pack("!H", 200) +
                               headers((("content-length", "10"),))) +
                         frame(3, 1, ident, b"short"))

        port_num, thread, errors = self.fake_server(handler)
        result = self.run_client(port_num, "/index.html")
        thread.join(6)
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 2)

    def test_client_fragmented_and_malformed_responses(self):
        def fragmented(conn):
            _, _, ident, _ = read_frame(conn)
            wire = frame(2, 0, ident, struct.pack("!H", 200) +
                         headers((("content-length", "3"),))) + frame(3, 1, ident, b"Hi\n")
            for byte in wire:
                conn.sendall(bytes([byte]))

        port_num, thread, errors = self.fake_server(fragmented)
        result = self.run_client(port_num, "/index.html")
        thread.join(6)
        self.assertFalse(errors, errors)
        self.assertEqual((result.returncode, result.stdout), (0, b"Hi\n"))

        def malformed(conn):
            _, _, ident, _ = read_frame(conn)
            conn.sendall(frame(2, 0, ident, struct.pack("!H", 200) + b"\x01\x0b\x00\x00"))

        port_num, thread, errors = self.fake_server(malformed)
        result = self.run_client(port_num, "/index.html")
        thread.join(6)
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 2)

        def oversized(conn):
            _, _, ident, _ = read_frame(conn)
            conn.sendall(frame(2, 0, ident, length=1_048_577))

        port_num, thread, errors = self.fake_server(oversized)
        result = self.run_client(port_num, "/index.html")
        thread.join(6)
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 2)

    def test_silent_peer_cannot_hang_client_forever(self):
        def handler(conn):
            read_frame(conn)
            time.sleep(5)

        port_num, thread, errors = self.fake_server(handler)
        try:
            result = self.run_client(port_num, "/index.html", timeout=4)
        except subprocess.TimeoutExpired:
            self.fail("bcurl hung waiting for a silent peer")
        self.assertEqual(result.returncode, 2)
        thread.join(6)
        self.assertFalse(errors, errors)


class DeliverableTests(unittest.TestCase):
    def test_annotated_hexdump_matches_real_program_bytes(self):
        doc = (BASE / "ANNOTATED_HEXDUMP.md").read_text()

        expected = []
        for section in ("## Client → server: REQUEST", "## Server → client: RESPONSE",
                        "## Server → client: final DATA"):
            block = doc.split(section, 1)[1].split("```text\n", 1)[1].split("\n```", 1)[0]
            expected.append(dump_bytes(block))

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / "index.html").write_bytes(b"Hi\n")
            port_num = port()
            server = subprocess.Popen([str(BASE / "bserve"), str(root), str(port_num)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(("127.0.0.1", port_num), 0.1):
                            break
                    except OSError:
                        time.sleep(0.01)
                result = subprocess.run(
                    [str(BASE / "bcurl"), "-v", f"127.0.0.1:{port_num}/index.html"],
                    capture_output=True, timeout=6,
                )
            finally:
                server.terminate()
                server.communicate(timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"Hi\n")
        blocks = re.split(r"(?=^(?:send|recv) frame type=)", result.stderr.decode(), flags=re.M)
        actual = [dump_bytes(block) for block in blocks if block.startswith(("send", "recv"))]
        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 3)
        self.assertEqual(actual[0][6:10], struct.pack("!I", 43))
        self.assertEqual(actual[1][12:14], struct.pack("!H", 200))
        self.assertEqual(actual[2][12:], b"Hi\n")


if __name__ == "__main__":
    unittest.main()
