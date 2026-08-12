#!/bin/sh
# Byte-level interop: protoc-generated reference serializer (ref) vs our
# reflection codec (reflect_proto) on the same fixture.  Both binaries are
# built by CMake; ctest passes their paths as $1 and $2.
set -eu

cd "$(dirname "$0")"
OURS=$1
REF=$2

"$REF" > ref.bin
"$OURS" --emit > ours.bin

cmp ref.bin ours.bin
"$OURS" --parse-file ref.bin
echo 'interop: OK'
