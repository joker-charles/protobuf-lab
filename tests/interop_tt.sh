#!/bin/sh
# Byte-level differential interop on protobuf's official test message
# (test_messages_proto3.proto -> TestAllTypesProto3): protoc-generated
# reference serializer (ref_tt) vs our reflection codec (reflect_proto_tt).
set -eu

cd "$(dirname "$0")"
OURS=$1
REF=$2

"$REF" > ref_tt.bin
"$OURS" --emit > ours_tt.bin

cmp ref_tt.bin ours_tt.bin
"$OURS" --parse-file ref_tt.bin
echo 'interop test_messages_proto3: OK'
