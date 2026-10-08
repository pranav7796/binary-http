# Final Audit Report

## Summary

The implementation passed the clean build, both existing suites, the independent
wire audit, sanitizer execution, static analysis, and a live end-to-end exchange.
No implementation defect was reproduced, so no protocol or source-code change was
made. This audit added `REQUIREMENTS_CHECKLIST.md` and this report.

One environment limitation remains: port 9000 was already owned by PID 1553353,
running `./bserve ./www 9000`. That process was not stopped or modified. The required
workflow was run against this build on port 19000 instead. The complete requirement
matrix is in `REQUIREMENTS_CHECKLIST.md`.

## Requirements And Deliverables

| Area | Status | Audit result |
| --- | --- | --- |
| Fixed-size binary protocol header | PASS | Header is 12 bytes; fields, offsets, and network byte order match BHP/1. |
| Request construction and response decoding | PASS | Actual client frames were decoded by the independent harness; status, indexed headers, DATA, and body were checked. |
| Status codes 200, 400, 404 | PASS | Success, malformed request, and missing-file cases returned the required binary status values. |
| Persistent TCP server connection | PASS | Sequential success/error requests completed on one socket. |
| Client single-connection behavior | PASS | Independent peer saw multiple sequential requests on one accepted connection and no second connection. |
| Unknown frame handling | PASS | Server and client consumed unknown frame payloads and continued to valid frames. |
| Indexed and literal header encoding | PASS | Codes 1–10 and literal-name encoding are documented; independent harness exercises both. |
| Verbose hexdump | PASS | Actual `-v` output was captured and its bytes compared with the wire. |
| Malformed frame behavior | PASS | Bad fixed header and oversized header produced ID-zero 400 then close; complete malformed known request received 400 and the next request worked. |
| Deliverable 1, specification | PASS | `PROTOCOL_SPEC.md` is 1,064 words and specifies field sizes, values, byte order, frame sequences, path/header validation, errors, and design rationale. Approximate page count is content-based; Markdown pagination was not separately rendered. |
| Deliverable 2, programs | PASS | Clean normal build succeeded. Real client/server exchange succeeded on port 19000. Exact port-9000 invocation could not be exercised due to the pre-existing listener. |
| Deliverable 3, annotated hexdump | PASS | Complete request, RESPONSE, and final DATA bytes are annotated. Automated live comparison passes byte-for-byte. |
| README and Makefile | PASS | Commands and described behavior were checked against implementation; clean `make` and `make test` succeeded. |
| REQUIREMENTS_CHECKLIST.md | PASS | Added with per-requirement status and evidence. |

## Tests Executed

All statuses below are observed from actual command execution. Both Python suites
use real TCP sockets; the independent suite constructs/parses frames from the
written specification and does not import the C protocol implementation.

| Command / test | Result |
| --- | --- |
| `make clean && make && make test` | Exit 0. 4 tests in `tests/test_protocol.py` and 14 tests in `tests/independent_wire_audit.py` passed. |
| `tests.test_protocol.ProtocolTests.test_persistent_partial_and_unknown_frame` | PASS: byte-fragmented request, unknown frame, binary response, later request. |
| `tests.test_protocol.ProtocolTests.test_errors_continue_and_traversal` | PASS: missing, traversal, symlink, malformed request, and subsequent valid request. |
| `tests.test_protocol.ProtocolTests.test_invalid_fixed_header_closes` | PASS: ID-zero 400 followed by connection close. |
| `tests.test_protocol.ProtocolTests.test_client_binary_verbose_and_exit` | PASS: binary stdout, verbose output, two targets, missing-file exit 1. |
| `ServerWireTests.test_success_binary_large_empty_and_indexed_headers` | PASS: HTML, zero-length, binary, >2 MiB body, indexed and literal headers. |
| `ServerWireTests.test_errors_unknown_and_fragmented_request_on_one_connection` | PASS: fragmented request, missing-file response, unknown type, malformed request, valid continuation. |
| `ServerWireTests.test_traversal_and_symlink` | PASS: traversal, encoded traversal, repeated path separators, symlink rejection. |
| `ServerWireTests.test_nonregular_fifo_does_not_block` | PASS: FIFO rejected with 404 without blocking. |
| `ServerWireTests.test_no_file_descriptor_growth_across_repeated_requests` | PASS: descriptor count stable over 100 requests. |
| `ServerWireTests.test_invalid_length_fixed_header_and_disconnect` | PASS: oversize, invalid magic, truncated payload EOF, subsequent connection. |
| `ServerWireTests.test_idle_peer_does_not_block_server_forever` | PASS: later request completed within test timeout despite idle peer. |
| `ClientWireTests.test_client_one_connection_wire_format_hexdump_and_unknown` | PASS: client request wire fields, multiple targets, one connection, unknown response frame. |
| `ClientWireTests.test_verbose_dump_equals_bytes_seen_by_independent_peer` | PASS: every dumped frame equals observed network bytes. |
| `ClientWireTests.test_client_4xx_and_5xx_exit` | PASS: statuses 400, 404, 503 returned client exit 1. |
| `ClientWireTests.test_client_rejects_truncated_response` | PASS: mismatched declared body length rejected with exit 2. |
| `ClientWireTests.test_client_fragmented_and_malformed_responses` | PASS: bytewise response, invalid header name code, oversized response length. |
| `ClientWireTests.test_silent_peer_cannot_hang_client_forever` | PASS: silent peer bounded by socket timeout; client returned exit 2. |
| `DeliverableTests.test_annotated_hexdump_matches_real_program_bytes` | PASS: all documented frame bytes match a real `bcurl -v` exchange. |
| ASan/UBSan clean build and `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 make test` | Exit 0. Same 18 tests passed; no sanitizer or leak report. |
| GCC `-fanalyzer` clean build | Exit 0. All C translation units compiled with no analyzer diagnostics or compiler warnings. |
| `timeout 1 ./bserve ./www 0` | Server rejected invalid port; printed `invalid port`. |
| `./bserve ./www 9000` | Server exit 2; printed `listen: Address already in use`. Existing listener PID 1553353 was left untouched. |
| `./bserve ./www 19000` + `./bcurl -v localhost:19000/index.html` | Server stayed active for the test; client exit 0. See live evidence below. |

## Live Demonstration Evidence

Command used after finding port 9000 occupied:

```sh
./bserve ./www 19000
./bcurl -v localhost:19000/index.html
```

Observed `bcurl` result: exit code `0`; stdout was the actual `www/index.html`
content (103 bytes). Its stderr reported:

```text
send frame type=1 flags=0x01 id=1 length=43
recv frame type=2 flags=0x00 id=1 length=47
recv frame type=3 flags=0x01 id=1 length=103
```

The request contained GET, `/index.html`, and indexed `host`, `accept`, and
`user-agent` headers. The response encoded status 200 as `00 c8`, content length
as ASCII `103`, and the final DATA frame carried exactly 103 bytes. The independent
hexdump test additionally established equality between emitted dump bytes and
captured network bytes. The audit-owned server on port 19000 was stopped afterward;
the pre-existing port-9000 process was not touched.

## Defects And Changes

No implementation bug was reproduced. The code correctly handles partial I/O,
frame length limits, unknown frame skipping, file descriptor cleanup, path
component validation, symlink refusal, and client/server timeouts in the tested
scenarios. No implementation source files were changed. Added files:

- `REQUIREMENTS_CHECKLIST.md`
- `FINAL_AUDIT_REPORT.md`

## Remaining Limitations

- The exact live command on port 9000 remains unverified because another process
  already owned that port. Equivalent live behavior was verified on port 19000.
- No external developer implementation was available for a reciprocal
  interoperability test; compatibility was tested using an independent Python
  harness based solely on the published specification.
- Markdown page count depends on renderer and print settings; semantic completeness
  and approximate document length were checked, but rendered pagination was not.

## Reproduction

From the project directory, with port 9000 free:

```sh
make clean
make
make test
./bserve ./www 9000
```

In another terminal:

```sh
./bcurl -v localhost:9000/index.html
```

To reproduce the live demonstration in this audit when port 9000 is occupied,
substitute `19000` in both commands. The server must be stopped after the client
demonstration; do not terminate an unrelated process already bound to the port.