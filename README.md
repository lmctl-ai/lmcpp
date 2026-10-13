# lmcpp

`lmcpp` is the C++17 SDK for [lmapi](https://github.com/lmctl-ai/lmapi) native tools. A CLI is the server: `--schema` publishes its `cli.schema.v1` contract, and operation calls are validated before handlers run. The SDK provides schema publishing, one shared argv parser/validator, flags and JSON input, policy floors, result files, and JSON command envelopes.

## Quick start: serve a tool

The repository includes [`examples/toy`](examples/toy), with read operations `add` and `scale` and a write operation `note`.

```sh
cmake -S . -B build -G Ninja
cmake --build build --parallel 2
build/bin/toy --schema
build/bin/toy add --a 2 --b=3
build/bin/toy scale --input-json '{"value":5,"factor":4}'
```

Flags accept `--name value` or `--name=value`; booleans can be bare or negated with `--no-name`. The runner supports aliases, kebab-case names, positionals after `--`, and `--input-file path.json`. JSON can be direct params or an `{ "op": "add", "params": { ... } }` envelope. `--schema` does not initialize operation handlers.

Success prints raw result JSON and exits 0. Errors print `{"success":false,"error":{"code":"...","message":"...","data":{...}}}` to stdout; diagnostics go to stderr. Protocol exit codes include validation 2, parse 3, not found 4, conflict 5, forbidden/readonly 6, timeout 7, unavailable 8, missing confirmation 9, and unsupported product version 10. Internal and command failures exit 1.

Write operations are blocked by `--readonly` or `<TOOL>_READONLY=1` (also `true`, `yes`, or `on`, case-insensitively). `--dryrun` or `<TOOL>_DRYRUN=1` validates input and returns a plan without invoking a handler. Dangerous operations also require `--confirm` or `--i-understand`.

`Context::temp_file_path()` provides a private temporary path removed when its context is destroyed. `read_result_file(path, "json"|"text")` accepts regular files up to 16 MiB; oversized output becomes `COMMAND_FAILED` with `data.tool_code` `OUTPUT_TOO_LARGE`.

## Install and use

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$PWD/stage"
cmake --build build --parallel 2
cmake --install build
```

Consumers use the exported CMake package:

```cmake
find_package(lmcpp CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE lmcpp::core)
```

The package resolves its nlohmann-json dependency. [`tests/consumer`](tests/consumer) is a compiled package smoke test.

## Use with lmscript

[`examples/toy/toy.lms`](examples/toy/toy.lms) imports [`cli.schema.dev.json`](examples/toy/cli.schema.dev.json) and calls `toy`. Put the absolute directory containing the built `toy` binary on `PATH`, then run `lmscript --check examples/toy/toy.lms` and `lmscript examples/toy/toy.lms`.

## Conformance and parity

Against the current lmapi vectors: validate **21 passed, 1 GraphQL skipped**; argv **14 passed**; publish **29 passed**. Through `run_cli_io`, validate vectors also report **21 passed, 1 GraphQL skipped**, and argv vectors **14 passed, 0 failed**. Core CLI tests cover flags/JSON routing and the runtime failure and policy cases listed in [Parity with lmrust](docs/parity.md).

`Type::Integer` enforces integer inputs in the server but publishes as `number`, the protocol type accepted by lmts. Clients therefore cannot infer the integer-only constraint from the schema. GraphQL, client-side encode/interpret, full help rendering, and the CLI11 bridge remain deferred. The private `torchcli` project consumes the installable package; it is maintained separately.

## Build and test

With CMake 3.22+, a C++17 compiler, Ninja, and nlohmann-json:

```sh
cmake -S . -B build -G Ninja -DLMAPI_DIR=../lmapi
cmake --build build --parallel 2
ctest --test-dir build -V --output-on-failure
./scripts/test-consumer.sh build
```

Or run the same tools in the `lmcpp-dev` Docker image:

```sh
./scripts/dev.sh sh -c 'cmake -S . -B build -G Ninja && cmake --build build --parallel 2 && ctest --test-dir build --output-on-failure'
```

Run the AddressSanitizer and UndefinedBehaviorSanitizer suite with `./scripts/test-sanitized.sh`. CI configures `-DLMCPP_REQUIRE_CONFORMANCE=ON`, making absent lmapi vectors fail the conformance test instead of omitting it.
