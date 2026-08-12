#!/bin/sh
# Byte-level differential interop on protobuf's official test message
# (test_messages_proto3.proto -> TestAllTypesProto3): protoc-generated
# reference serializer vs our reflection codec on the shared fixture.
set -eu

cd "$(dirname "$0")"
ROOT=../..
PB_ROOT=$ROOT/protobuf-3.21.12
PROTOC=$PB_ROOT/build/protoc
OURS=$ROOT/reflect-proto/build/reflect_proto_tt

GEN=gen_tt
rm -rf "$GEN"
mkdir -p "$GEN"
"$PROTOC" -I "$PB_ROOT/src" --cpp_out="$GEN" \
    "$PB_ROOT/src/google/protobuf/test_messages_proto3.proto"

g++ -std=c++17 -O2 -I "$PB_ROOT/src" -I "$GEN" \
    ref_main_tt.cc "$GEN/google/protobuf/test_messages_proto3.pb.cc" \
    "$PB_ROOT/build/libprotobuf.a" -pthread -lz -o ref_tt

./ref_tt > ref_tt.bin
"$OURS" --emit > ours_tt.bin

cmp ref_tt.bin ours_tt.bin
"$OURS" --parse-file ref_tt.bin
echo 'interop test_messages_proto3: OK'
