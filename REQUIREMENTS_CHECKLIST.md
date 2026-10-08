# Assignment 2 Requirement Checklist

Audit basis: `project-requirements.jpeg`, `PROTOCOL_SPEC.md`, source inspection,
and execution of both repository test suites against real TCP sockets.

| Requirement | Status | Evidence |
| --- | --- | --- |
| Binary protocol with a fixed-size frame header | PASS | BHP/1 defines a 12-byte header with fixed offsets and sizes. |
| Explicit byte order and field encoding | PASS | All integer fields are unsigned network byte order; independent tests parse the wire bytes. |
| Server accepts TCP clients and handles requests | PASS | Real `bserve` integration tests receive status/body responses. |
| Server maps paths only under its document root | PASS | `openat` component traversal, `O_NOFOLLOW`, and traversal/symlink tests. |
| Server returns 200 for existing regular files | PASS | HTML, empty, binary, and large-file tests. |
| Server returns 404 for missing/inaccessible/non-regular/symlinked files | PASS | Missing-file, symlink, and FIFO cases were executed; inspection confirms open failures map to 404. A permission-denied file was not separately exercised. |
| Server returns 400 for malformed requests | PASS | Malformed payload, traversal syntax, invalid fixed header, and oversized frame tests. |
| Server keeps a valid TCP connection open for sequential exchanges | PASS | Multiple responses, including after an unknown frame and error response, on one socket. |
| Client constructs a binary request | PASS | Independent fake peer decodes its actual request fields and indexed headers. |
| Client decodes binary status, headers, DATA, and body | PASS | Fake peers and independent server decoder verify actual response handling. |
| Client writes response body to stdout | PASS | Live demo and binary-body integration assertions. |
| Client exits nonzero for 4xx/5xx | PASS | Independent peer tests 400, 404, and 503; client returns 1. |
| Client uses at most one TCP connection for same-host targets | PASS | Fake listener observes both requests over one accepted connection and no second accept. |
| Client handles short/fragmented TCP reads and writes | PASS | Byte-at-a-time request and response integration cases pass. |
| Unknown frame types are skipped by declared length | PASS | Unknown types with arbitrary flags/ID precede valid frames in independent tests. |
| Ten indexed header names and length-prefixed literal names | PASS | Specification enumerates codes 1–10; independent harness encodes and decodes indexed and literal names. |
| Verbose mode dumps request and response frames | PASS | Live `-v` output captured; independent test compares dump bytes to bytes observed on the socket. |
| Malformed or truncated frame handling | PASS | Oversize, bad magic, partial payload, malformed response, and unexpected EOF cases. |
| Deliverable 1: complete protocol specification | PASS | `PROTOCOL_SPEC.md` defines framing, values, path rules, headers, errors, unknown types, and design rationale (1,064 words). |
| Deliverable 2: working server and client | PASS | Clean build and complete suites pass; live client/server exchange succeeds on port 19000. Port 9000 itself was unavailable during this audit. |
| Deliverable 3: annotated complete request/response | PASS | `ANNOTATED_HEXDUMP.md` contains request, response, and final DATA; live-byte comparison passes. |
| Build and test entry points | PASS | `Makefile` provides `all`, `clean`, and `test`; clean normal build and both suites pass. |
| README accurately describes invocation and behavior | PASS | Reviewed commands, CLI, outputs, timeout policy, and test coverage against implementation. |
| Exact mandated live command on port 9000 | NOT VERIFIED | Port 9000 was already bound by PID 1553353 (`./bserve ./www 9000`); it was not stopped or used. Same workflow passed on port 19000. |