# lmcpp

`lmcpp` is the C++17 SDK for [lmapi](https://github.com/lmctl-ai/lmapi) native tools. A CLI is the server: `--schema` publishes the `cli.schema.v1` contract, and each published operation accepts validated flags or JSON input. The SDK provides schema publishing, shared validation, argv/JSON resolution, policy floors, result files, and the command runner.

## Quick start: serve a tool

The repository includes a complete small server in [`examples/toy`](examples/toy): read operations `add` and `scale`, plus write operation `note`.

```sh
cmake -S . -B build -G Ninja
cmake --build build --parallel 2
build/bin/toy --schema
build/bin/toy add --a 2 --b=3
build/bin/toy scale --input-json '{"value":5,"factor":4}'
```

The same operation can receive JSON from a file with `--input-file path.json`. JSON may contain just the params or an `{ "op": "add", "params": { ... } }` envelope. Flags use `--name value` or `--name=value`; boolean flags can be bare. `--schema` works without initializing or invoking operation handlers.

Success prints the raw result JSON and exits 0. Errors print `{"success":false,"error":{"code":"...","message":"...","data":{...}}}` to stdout; diagnostics go to stderr. Protocol exit codes include validation 2, parse 3, not found 4, conflict 5, readonly forbidden 6, timeout 7, unavailable 8, missing confirmation 9, and unsupported product version 10. Other command failures use 1.

Write operations are blocked by `--readonly` or `<TOOL>_READONLY=1` (case-insensitive true values are also accepted). `--dryrun` or `<TOOL>_DRYRUN=1` validates input and returns a plan without running the handler. Dangerous operations additionally require `--confirm` or `--i-understand`.

`Context::temp_file_path()` provides a private temporary path which is removed when the context is destroyed. `read_result_file(path, "json"|"text")` reads a handler result with a 16 MiB limit; oversized output is a `COMMAND_FAILED` error with `data.tool_code` set to `OUTPUT_TOO_LARGE`.

## Install and use

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$PWD/stage"
cmake --build build --parallel 2
cmake --install build
```

A consumer can then use the exported package:

```cmake
find_package(lmcpp CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE lmcpp::core)
```

The package exports its nlohmann-json dependency. See [`tests/consumer`](tests/consumer) for a compiled smoke test.

## Use with lmscript

[`examples/toy/toy.lms`](examples/toy/toy.lms) imports the checked-in [`cli.schema.dev.json`](examples/toy/cli.schema.dev.json) and invokes the tool. Put the absolute directory containing `toy` on `PATH`, then run `lmscript --check examples/toy/toy.lms` and `lmscript examples/toy/toy.lms`.

## Conformance and parity

At the current lmapi fixture revision, CTest covers **validate: 21 passed + 1 skipped GraphQL case; argv: 14 passed; publish: 29 passed**. GraphQL is outside this server's published flags/JSON inputs. See [Parity with lmrust](docs/parity.md) for implemented behavior and deferred work.

The private `torchcli` project is a downstream consumer of the installable package; it is not part of this repository. Deferred areas include GraphQL, client-side encode/interpret APIs, and the CLI11 bridge.

## Build and test

With CMake 3.22+, a C++17 compiler, Ninja, and nlohmann-json installed:

```sh
cmake -S . -B build -G Ninja -DLMAPI_DIR=../lmapi
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Or use the repository's Docker route, which runs the same build tools in `lmcpp-dev`:

```sh
./scripts/dev.sh sh -c 'cmake -S . -B build -G Ninja && cmake --build build --parallel 2 && ctest --test-dir build --output-on-failure'
```

Set `-DLMCPP_REQUIRE_CONFORMANCE=ON` in CI to make missing lmapi vectors fail the conformance test instead of omitting it.
