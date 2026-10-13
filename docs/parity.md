# lmcpp parity boundary (slice 1)

| Area | Slice 1 | lmrust / later |
|---|---|---|
| Schema publish | Deterministic cli.schema.v1 for scalar params, explicit read/write | Parse/validate published schemas and optional metadata later |
| Inputs | Flags and inline JSON, direct params or `{op,params}` | input-file, GraphQL, aliases, booleans false/repeat/positionals later |
| Validation | Unknown, duplicate, missing, scalar type, enum issues | Full lmapi coercion and conformance vectors later |
| Runner | `--schema`, dispatch, stdout JSON, protocol envelope/exits, exception boundary | Help/list, readonly/dryrun/confirm and environment policy floors later |
| Result files | Deferred | lmrust adapter deferred by requested scope |
| CLI bridge / client | Deferred | CLI11 bridge, GraphQL, client APIs out of scope |

Policy enforcement (readonly/dryrun) is intentionally a TODO for slice 2. The write toy.note
operation is included as a fixture; this slice does not claim policy-floor conformance.
Required parameters publish as `occurs: "1"` and optional parameters as `"?"`; lmapi's
schema validator rejects a separate `required` property.

The lmapi JSON vector groups are deferred to slice 2: this first runner does not expose the
lower-level published-schema parser/validator interfaces those vectors exercise, and adapting
only selected cases into a second test harness would not be a meaningful conformance run.
