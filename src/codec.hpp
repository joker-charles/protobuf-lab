// Protobuf wire-format codec driven by C++26 static reflection (P2996 +
// P3394R4 value annotations).
//
// Wire primitives come from protobuf's own
// google/protobuf/io/coded_stream.{h,cc} (linked from libprotobuf); the
// reflection decides which members are which fields.  Field numbers are
// explicit: every member (except UnknownFields) carries one or more
// [[=rpb::field_no<N>{}]] annotations (P3394R4).  Ordinary members carry
// exactly one; OneOf members carry one per alternative, in std::variant
// order.  Known fields serialize in ascending field-number order
// (compile-time sorted); UnknownFields re-emit after all known fields.
//
// Type mapping (v3):
//   std::string                      -> wire type 2 (length-delimited)
//   integral / enum                  -> wire type 0 (varint, sign-extended)
//   SInt<T> (sint32/sint64)          -> wire type 0 (zigzag)
//   Fixed32 / SFixed32               -> wire type 5 (little-endian 4)
//   Fixed64 / SFixed64               -> wire type 1 (little-endian 8)
//   float / double                   -> wire type 5 / 1
//   std::vector<T> (packable T)      -> packed, wire type 2
//   std::vector<T> (string/message)  -> repeated length-delimited
//   std::vector<Unpacked<T>>         -> repeated unpacked (tag per element)
//   std::map<K,V>                    -> repeated map-entry messages (k=1,v=2)
//   std::optional<T>                 -> optional field (skipped when empty)
//   std::unique_ptr<T>               -> singular message (presence; null
//                                        omitted, breaks recursion cycles)
//   OneOf<Ts...>                     -> oneof: one field per alternative
//                                        (presence semantics; default
//                                        values still serialize)
//   nested struct                    -> embedded message, wire type 2
//   UnknownFields (any position)     -> unknown-field preservation
//
// proto3 default omission: scalar / string / bytes / enum members equal to
// their default (and empty packed vectors) are not serialized. Nested
// messages serialize unless all their members are default/absent (value
// semantics cannot distinguish unset from present-but-empty; optional and
// unique_ptr keep real presence). Repeated length-delimited elements are
// never omitted, even when empty. Oneof alternatives serialize whenever
// they are set, even at default values.

#pragma once

#include "rpb/annotations.hpp"
#include "rpb/traits.hpp"
#include "rpb/layout.hpp"
#include "rpb/wire.hpp"
#include "rpb/group.hpp"
#include "rpb/core.hpp"
#include "rpb/serialize.hpp"
#include "rpb/parse.hpp"
#include "rpb/deep.hpp"
