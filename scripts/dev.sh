#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
exec docker run --rm -u "$(id -u):$(id -g)" -v "$HOME/repos:/repos" -w /repos/lmctl-ai/lmcpp lmcpp-dev "$@"
