#!/bin/sh
# Official protobuf conformance suite against our reflection codec.  Both
# binaries are built by CMake (FetchContent provides the runner); ctest
# passes their paths as $1 and $2.
set -eu

cd "$(dirname "$0")"
OURS=$1
RUNNER=$2
FAILURES=conformance_failures.txt

"$RUNNER" --failure_list "$FAILURES" "$OURS"
