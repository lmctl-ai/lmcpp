# lmcpp parity boundary (slice 2)

| Area | Implemented | Deferred |
|---|---|---|
| Schema | Deterministic publish; basic published schema validation | Full metadata and warning parity |
| Inputs | Flags, inline/file JSON, direct params or `{op,params}` | GraphQL and remaining argv edge parity |
| Validation | Shared validator for supported scalar, object, array, and JSON params | Full coercion/suggestion parity outside covered vectors |
| Runner | `--schema`, dispatch, JSON envelopes/exits, exceptions; readonly/dryrun/confirm | Help/list and broader server compatibility |
| Results | RAII temp path; JSON/text parsing and 16 MiB cap | CSV/auto formats and CLI bridge integration |
| Client / bridge | Deferred | CLI11 bridge, GraphQL client, encode and interpret APIs |

`TOY_READONLY` and `TOY_DRYRUN` accept `1`, `true`, `yes`, or `on`, case-insensitively.
Dry-run validates before returning a synthetic plan and never invokes the handler. `--confirm`
and `--i-understand` satisfy dangerous-operation confirmation; lmrust has no confirm env floor.
Required parameters publish as `occurs: "1"` and optional parameters as `"?"`; lmapi's
schema validator rejects a separate `required` property.

CTest consumes every validate, argv, and publish vector. It skips only GraphQL validate cases
because this SDK publishes flags and JSON only. Client-only encode and interpret groups are
inapplicable and not run. Number results retain nlohmann's `5.0` formatting; serde_json also
serializes an f64 value of 5.0 as `5.0`.
