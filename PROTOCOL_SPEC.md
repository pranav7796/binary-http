# BHP/1: Binary Hypertext Protocol

BHP/1 is a small HTTP-inspired file-transfer protocol over one TCP connection. All integers are **unsigned, network byte order** (most significant byte first). TCP supplies an ordered byte stream, so implementations must read exact byte counts. A connection carries any number of sequential request/response exchanges; a client sends the next request after the final DATA frame of the prior response. There is no multiplexing, request body, or connection-close frame.

## Fixed frame header (12 bytes)

| Offset | Width | Field | Rule |
| --- | ---: | --- | --- |
| 0 | 16 bits | Magic | ASCII `BH` (`42 48`) |
| 2 | 8 bits | Version | `01` |
| 3 | 8 bits | Type | `01` REQUEST, `02` RESPONSE, `03` DATA; other values are unknown extensions |
| 4 | 8 bits | Flags | Bit 0 (`01`) is END; bits 1–7 must be zero for known types |
| 5 | 8 bits | Reserved | Zero |
| 6 | 32 bits | Payload length | Number of bytes following this header; 0–1,048,576 inclusive |
| 10 | 16 bits | Request ID | Nonzero for valid known frames; response frames echo the request ID. Zero can identify an error when no valid request ID exists. |

The 32-bit length permits a 1 MiB maximum without a 24-bit packing routine. The 16-bit ID makes response matching explicit while keeping the header short. Unlike HTTP/2's 31-bit stream identifier, BHP/1 sends requests sequentially; 65,535 IDs suffice for one connection. The header's magic, version, and reserved byte make framing errors detectable. Normal senders use DATA chunks of at most 65,536 bytes, but receivers accept any DATA payload up to the frame maximum.

**Unknown type rule:** After validating the fixed header and maximum length, a receiver encountering any unknown type **MUST consume and discard exactly its declared payload length** and then read the next 12-byte header. Its flags, ID, and payload have no BHP/1 meaning. This applies anywhere between known frames. An oversized frame is never skipped because its claimed boundary is untrusted.

## Payloads and exchange

`REQUEST` (type 1) has END set and consists of `method:u8`, `path_length:u16`, exactly that many path bytes, and a header block. The only BHP/1 method is `01` GET. The path starts with `/` and is 1–4,096 bytes. `/` selects `index.html` in the document root. Path bytes are printable ASCII (`0x21`–`0x7e`), excluding `\`, `?`, `#`, and `%`; empty components, `.` and `..` components, and a trailing slash (except `/`) are invalid. Paths are literal bytes, with no URL escaping, query, or normalization. The document root is server-local and never appears on the wire. No body follows a REQUEST.

`RESPONSE` (type 2) has flags zero and consists of `status:u16` followed by a header block. Status values are HTTP-style decimal codes encoded as a **binary** 16-bit integer (for example, 200 is `00 c8`). BHP/1 server outcomes are `200` success, `400` invalid request, and `404` missing, inaccessible, non-regular, or symlinked file. Clients accept status 100–599; any 400–599 status makes `bcurl` exit nonzero. A response has exactly one RESPONSE frame followed by one or more DATA frames with the same ID. Each DATA (type 3) has raw body bytes; the last DATA has END set, even when the body is empty. Other DATA frames have flags zero. EOF is not a message delimiter. A `content-length` header, when present, is a decimal ASCII count of all DATA payload bytes and must match the delivered body length. The server sends it on every response.

Only one response is active at a time. Request IDs can be any nonzero 16-bit values; clients should avoid reuse during a connection. The server preserves request order. The server keeps the TCP connection open after 200, 400, or 404, so a later REQUEST can follow. `bcurl` accepts multiple same-host targets in one invocation and uses the same socket for them; it opens one TCP connection at most.

Connection idle timeouts are local implementation policy, not frame delimiters. This `bserve` closes an idle client socket after three seconds; `bcurl` bounds connection attempts and socket reads/writes to three seconds per operation. A peer must never infer a response boundary from an EOF or timeout.

## Header block and name compression

A header block starts with `count:u8` (0–255). Each entry is `name_code:u8`, an optional literal name, `value_length:u16`, and exactly that many value bytes. Codes 1–10 represent these exact lowercase ASCII names; no name bytes follow a coded name:

| Code | Name | Code | Name |
| ---: | --- | ---: | --- |
| 1 | `content-type` | 6 | `server` |
| 2 | `content-length` | 7 | `cache-control` |
| 3 | `host` | 8 | `etag` |
| 4 | `accept` | 9 | `last-modified` |
| 5 | `user-agent` | 10 | `connection` |

Code 0 means a literal name: `name_length:u8` (1–255), then that many name bytes. Literal names contain only lowercase ASCII letters, digits, or `-`. Codes 11–255 are malformed in BHP/1. Values are uninterpreted bytes except NUL, CR, and LF are forbidden; length is 0–65,535. The header block must end exactly at the payload boundary. Receivers validate all entries even if they ignore their meaning. Senders use the numeric codes for the ten listed names; other names use code 0. This is static name compression: no dynamic table or cross-frame state, so either peer can decode a frame independently.

## Errors and compatibility

Known-frame types with invalid flags, ID, field lengths, header encoding, method, or path are malformed. A server that has consumed a complete malformed known frame sends status 400 using its ID, then continues reading at the next frame. An invalid magic/version/reserved byte or length above 1 MiB causes a 400 response with ID zero and connection close: the next reliable boundary cannot be established. A partial header or payload at EOF closes the connection without a response. Unexpected known frame types sent to the server receive 400. Clients reject invalid known response sequences, mismatched IDs, malformed headers, invalid status, truncated responses, and mismatched `content-length`.

Peers may add new frame types in later versions while retaining this header and maximum length; BHP/1 receivers remain synchronized by skipping them. Changing the meaning of known types requires a new version. A receiver rejects an unsupported version rather than guessing at its payload format.
