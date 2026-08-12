#!/bin/sh
# Official protobuf conformance suite against our reflection codec.
# Requires protobuf-3.21.12/build-conf/conformance_test_runner (see
# tests/build_conformance_runner.sh) and the vendored tree's generated
# conformance.pb.cc.
set -eu

cd "$(dirname "$0")"
ROOT=../..
PB_ROOT=$ROOT/protobuf-3.21.12
RUNNER=$PB_ROOT/build-conf/conformance_test_runner
OURS=$ROOT/reflect-proto/build/conformance_ours
FAILURES=$ROOT/reflect-proto/tests/conformance_failures.txt

if [ ! -x "$RUNNER" ]; then
  echo "conformance runner not built; run tests/build_conformance_runner.sh" >&2
  exit 1
fi

"$RUNNER" --failure_list "$FAILURES" "$OURS"
