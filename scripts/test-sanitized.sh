#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
exec ./scripts/dev.sh sh -c 'cmake -S . -B build-sanitize -G Ninja -DLMCPP_ENABLE_SANITIZERS=ON -DLMCPP_STATIC_STDLIB=OFF -DLMAPI_DIR=../lmapi && cmake --build build-sanitize --parallel 2 && ctest --test-dir build-sanitize -V --output-on-failure'
