# Assignment 2: HTTP, in Binary

This directory contains the BHP/1 protocol specification, a C server and client, tests, and a captured annotated exchange. Requires a POSIX system, a C11 compiler, `make`, and Python 3 for tests. No external libraries are needed.

```sh
cd /home/pranav/computernetwork/assignment2
make
./bserve ./www 9000
```

In another terminal:

```sh
./bcurl -v localhost:9000/index.html
./bcurl localhost:9000/index.html localhost:9000/missing
make test
```

`bcurl` writes only body bytes to stdout. `-v` writes a hexadecimal dump of every transmitted and received frame to stderr. For multiple targets, each must use the same host and port, and all exchanges occur sequentially on one TCP socket. Exit status is 0 when all requests succeed, 1 when any response is 4xx/5xx, or 2 for command, transport, or protocol errors.

The server serves regular files under the supplied root; `/` maps to `index.html`. It refuses symlinks, non-regular files, and `.`/`..` path components. It is deliberately single-threaded for this course exercise: an idle connected client can delay other clients for up to three seconds before its socket times out. The client also times out stalled socket operations and connection attempts. Stop the server with Ctrl+C. See [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md) for the complete wire format and [ANNOTATED_HEXDUMP.md](ANNOTATED_HEXDUMP.md) for a complete byte-level example.

Run `make test` to check binary bodies, multiple requests on one connection, partial reads, malformed and unknown frames, directory traversal, non-regular files, client output, verbose dumps, and the documented hexdump against real execution. `tests/independent_wire_audit.py` builds and parses frames from the written specification without importing the C protocol implementation.
