# lmcpp parity boundary

| Area | Implemented and tested | Deferred or limited |
|---|---|---|
| Published schema | Deterministic `cli.schema.v1`; `occurs` drives cardinality; arrays publish `items`; aliases and positional metadata are retained. Integer params publish as protocol type `number`, while the server enforces integer input. | Integer-only constraints cannot be expressed in the current protocol schema. Full schema warning parity. |
| Inputs and validation | One argv parser serves `parse_argv`, `resolve_input`, and `run_cli_io`; flags, kebab names, aliases, boolean negation, positionals, `--`, inline/file JSON and `{op,params}` envelopes. | GraphQL and client-side input encoding. |
| Runner | JSON success/error output, standard exit mapping, exception boundary, `--schema`, minimal `--help`, `--list`, `--explain`, readonly/dryrun/confirm floors. JSON depth is capped at 128; input and result files are bounded to 16 MiB and must be regular files. | Product-version checks and full lmrust help rendering. |
| Result files | RAII temporary result path, JSON/text parsing and `OUTPUT_TOO_LARGE`. | CSV/automatic formats and CLI bridge integration. |
| Client / bridge | Not implemented. | GraphQL client, encode/interpret APIs, CLI11 bridge. |

CTest runs the lmapi validate, argv, and publish vectors. The API validator covers validate **21 passed, 1 GraphQL skipped**; the parser covers argv **14 passed**; publish covers **29 passed**. Runtime tests send validate vectors through `run_cli_io` (**21 passed, 1 GraphQL skipped**) and argv vectors through `run_cli_io` (**14 passed, 0 failed**). Core tests cover CLI flags and JSON dispatch, error/exit envelopes, malformed and deeply nested input, invalid UTF-8 argv/path handling, policy boundaries, FIFO/device rejection, and bounded reads. `ctest -V` shows the vector counts from the fixture runner.

`TOY_READONLY` and `TOY_DRYRUN` accept `1`, `true`, `yes`, and `on`, case-insensitively. The readonly floor blocks writes. Dry-run validates before returning a plan and does not invoke handlers. `--confirm` and `--i-understand` satisfy dangerous-operation confirmation; there is no confirm environment floor.

`Type::Integer` remains an internal validation type, but publishes as `number` because lmts and the protocol do not accept `integer`. As a result, a remote schema consumer cannot see the server's integer-only restriction. Number JSON output preserves `5.0`, matching serde_json for an f64 value of 5.0.
