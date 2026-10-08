# One complete BHP/1 exchange

Captured from `bcurl -v 127.0.0.1:<port>/index.html` against `bserve` with an `index.html` containing the three bytes `Hi\n`. The chosen TCP port is not present in the BHP/1 payload; the client sends `127.0.0.1` as its `host` header. This exchange has one REQUEST, one RESPONSE, and one final DATA frame. Hex offsets restart at zero for each frame.

## Client → server: REQUEST

```text
send frame type=1 flags=0x01 id=1 length=43
  0000  42 48 01 01 01 00 00 00 00 2b 00 01 01 00 0b 2f  |BH.......+...../|
  0010  69 6e 64 65 78 2e 68 74 6d 6c 03 03 00 09 31 32  |index.html....12|
  0020  37 2e 30 2e 30 2e 31 04 00 03 2a 2f 2a 05 00 07  |7.0.0.1...*/*...|
  0030  62 63 75 72 6c 2f 31                             |bcurl/1|
```

| Byte range | Bytes | Meaning |
| --- | --- | --- |
| 0–1 | `42 48` | Magic `BH` |
| 2–5 | `01 01 01 00` | Version 1, REQUEST, END flag, reserved zero |
| 6–11 | `00 00 00 2b 00 01` | 43-byte payload, request ID 1 |
| 12–14 | `01 00 0b` | GET, 11-byte path |
| 15–25 | `2f 69 6e 64 65 78 2e 68 74 6d 6c` | `/index.html` |
| 26 | `03` | Three headers |
| 27–38 | `03 00 09 31 32 37 2e 30 2e 30 2e 31` | Name code 3 (`host`), nine-byte value `127.0.0.1` |
| 39–44 | `04 00 03 2a 2f 2a` | Name code 4 (`accept`), value `*/*` |
| 45–54 | `05 00 07 62 63 75 72 6c 2f 31` | Name code 5 (`user-agent`), value `bcurl/1` |

## Server → client: RESPONSE

```text
recv frame type=2 flags=0x00 id=1 length=45
  0000  42 48 01 02 00 00 00 00 00 2d 00 01 00 c8 03 01  |BH.......-......|
  0010  00 18 74 65 78 74 2f 68 74 6d 6c 3b 20 63 68 61  |..text/html; cha|
  0020  72 73 65 74 3d 75 74 66 2d 38 02 00 01 33 06 00  |rset=utf-8...3..|
  0030  08 62 73 65 72 76 65 2f 31                       |.bserve/1|
```

| Byte range | Bytes | Meaning |
| --- | --- | --- |
| 0–5 | `42 48 01 02 00 00` | Magic, version 1, RESPONSE, no flags, reserved zero |
| 6–11 | `00 00 00 2d 00 01` | 45-byte payload, matching ID 1 |
| 12–14 | `00 c8 03` | Binary status 200, three headers |
| 15–41 | `01 00 18` + 24 ASCII bytes | Code 1 (`content-type`), `text/html; charset=utf-8` |
| 42–45 | `02 00 01 33` | Code 2 (`content-length`), value `3` |
| 46–56 | `06 00 08` + `bserve/1` | Code 6 (`server`), eight-byte value |

## Server → client: final DATA

```text
recv frame type=3 flags=0x01 id=1 length=3
  0000  42 48 01 03 01 00 00 00 00 03 00 01 48 69 0a     |BH..........Hi.|
```

Bytes 0–11 are the fixed header: type 3 DATA, END set, length 3, ID 1. Bytes 12–14 are `48 69 0a`, or `Hi\n`. The END bit completes the response while the TCP connection remains open for another request. `bcurl` writes exactly those three bytes to stdout.
