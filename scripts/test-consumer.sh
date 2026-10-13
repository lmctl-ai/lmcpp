#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=${1:-"$root/build"}
case "$build" in /*) ;; *) build="$root/$build" ;; esac
prefix="$build/consumer/install"
cmake --install "$build" --prefix "$prefix"
cmake -S "$root/tests/consumer" -B "$build/consumer/build" -G Ninja -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$build/consumer/build" --parallel 2
"$build/consumer/build/lmcpp_consumer"
