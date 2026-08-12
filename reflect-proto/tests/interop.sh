#!/bin/sh
# Byte-level interop: official protoc-generated serializer vs our
# reflection-driven codec on the same fixture.
set -eu

cd "$(dirname "$0")"
ROOT=../..
PB_ROOT=$ROOT/protobuf-3.21.12
PROTOC=$PB_ROOT/build/protoc
OURS=$ROOT/reflect-proto/build/reflect_proto

GEN=gen
rm -rf "$GEN"
mkdir -p "$GEN"
"$PROTOC" --cpp_out="$GEN" test.proto

g++ -std=c++17 -O2 -I "$PB_ROOT/src" -I "$GEN" \
    ref_main.cc "$GEN/test.pb.cc" "$PB_ROOT/build/libprotobuf.a" \
    -pthread -lz -o ref

./ref > ref.bin
"$OURS" --emit > ours.bin

cmp ref.bin ours.bin
"$OURS" --parse-file ref.bin
echo 'interop: OK'
