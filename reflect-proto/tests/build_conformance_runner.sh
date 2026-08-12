#!/bin/sh
# Build the official conformance runner from the vendored protobuf 3.21.12
# tree.  The vendored cmake/conformance.cmake omits two source files
# (conformance_test_main.cc, text_format_conformance_suite.cc), without
# which the runner has no main() and fails to link; this script applies
# that fix idempotently, then configures and builds.
set -eu

cd "$(dirname "$0")/../.."
PB_ROOT=protobuf-3.21.12
CMAKE_FILE=$PB_ROOT/cmake/conformance.cmake

if ! grep -q 'conformance_test_main.cc' "$CMAKE_FILE"; then
  sed -i '/conformance\/conformance_test.cc/a \  ${protobuf_SOURCE_DIR}\/conformance\/conformance_test_main.cc' \
      "$CMAKE_FILE"
fi
if ! grep -q 'text_format_conformance_suite.cc' "$CMAKE_FILE"; then
  sed -i '/conformance\/conformance_test_runner.cc/a \  ${protobuf_SOURCE_DIR}\/conformance\/text_format_conformance_suite.cc' \
      "$CMAKE_FILE"
fi

cmake -S "$PB_ROOT" -B "$PB_ROOT/build-conf" -DCMAKE_BUILD_TYPE=Release \
    -Dprotobuf_BUILD_CONFORMANCE=ON -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_EXAMPLES=OFF
cmake --build "$PB_ROOT/build-conf" --target conformance_test_runner \
    -j"${JOBS:-4}"
