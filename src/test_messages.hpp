// Mirror of google::protobuf::test_messages_proto3.TestAllTypesProto3
// (src/google/protobuf/test_messages_proto3.proto in the vendored
// protobuf-3.21.12 tree) as annotation-driven rpb structs.
//
// This is the message protobuf's own conformance/benchmark/fuzz suites use,
// so byte-level interop against it reuses the official test schema instead
// of a hand-rolled one.  Differential fixture lives in main_tt.cpp /
// tests/ref_main_tt.cc (kept in sync).
//
// Deliberately omitted (wire-wise redundant): repeated wrappers (211-219),
// Duration/Timestamp/FieldMask/Any (301-315), fieldname* (401-418).
// All omitted fields stay unset in the shared fixture, so protobuf omits
// them from the wire bytes and they never reach our parser.

#pragma once

#include "codec.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tmm {

struct TestAllTypesProto3;  // for recursive unique_ptr members

enum class ForeignEnum : std::int32_t
{
  FOREIGN_FOO = 0,
  FOREIGN_BAR = 1,
  FOREIGN_BAZ = 2,
};
enum class NestedEnum : std::int32_t
{
  FOO = 0,
  BAR = 1,
  BAZ = 2,
  NEG = -1,  // deliberately negative
};
enum class AliasedEnum : std::int32_t
{
  ALIAS_FOO = 0,
  ALIAS_BAR = 1,
  ALIAS_BAZ = 2,
  MOO = 2,
};
enum class NullValue : std::int32_t
{
  NULL_VALUE = 0,
};

struct ForeignMessage
{
  [[=rpb::field_no<1>{}]] std::int32_t c;
  bool operator==(ForeignMessage const &) const = default;
};

struct NestedMessage
{
  [[=rpb::field_no<1>{}]] std::int32_t a;
  [[=rpb::field_no<2>{}]] std::unique_ptr<TestAllTypesProto3> corecursive;
  rpb::UnknownFields unknown;  // conformance: nested unknowns preserved
  bool operator==(NestedMessage const &o) const
  {
    return rpb::deep_equal(*this, o);
  }
};

// google.protobuf wrapper messages: single `value` field.
struct BoolValue
{
  [[=rpb::field_no<1>{}]] bool value;
  bool operator==(BoolValue const &) const = default;
};
struct Int32Value
{
  [[=rpb::field_no<1>{}]] std::int32_t value;
  bool operator==(Int32Value const &) const = default;
};
struct Int64Value
{
  [[=rpb::field_no<1>{}]] std::int64_t value;
  bool operator==(Int64Value const &) const = default;
};
struct UInt32Value
{
  [[=rpb::field_no<1>{}]] std::uint32_t value;
  bool operator==(UInt32Value const &) const = default;
};
struct UInt64Value
{
  [[=rpb::field_no<1>{}]] std::uint64_t value;
  bool operator==(UInt64Value const &) const = default;
};
struct FloatValue
{
  [[=rpb::field_no<1>{}]] float value;
  bool operator==(FloatValue const &) const = default;
};
struct DoubleValue
{
  [[=rpb::field_no<1>{}]] double value;
  bool operator==(DoubleValue const &) const = default;
};
struct StringValue
{
  [[=rpb::field_no<1>{}]] std::string value;
  bool operator==(StringValue const &) const = default;
};
struct BytesValue
{
  [[=rpb::field_no<1>{}]] std::string value;
  bool operator==(BytesValue const &) const = default;
};

struct StructValue;
struct ListValue;

// google.protobuf.Value: oneof of six kinds.  struct/list alternatives
// break the mutual recursion through unique_ptr.
struct Value
{
  [[=rpb::field_no<1>{}, =rpb::field_no<2>{}, =rpb::field_no<3>{},
    =rpb::field_no<4>{}, =rpb::field_no<5>{}, =rpb::field_no<6>{}]]
  rpb::OneOf<NullValue, double, std::string, bool,
             std::unique_ptr<StructValue>, std::unique_ptr<ListValue>>
      kind;

  bool operator==(Value const &o) const
  {
    return rpb::deep_equal(*this, o);
  }
};

// google.protobuf.Struct: map<string, Value>.
struct StructValue
{
  [[=rpb::field_no<1>{}]] std::map<std::string, Value> fields;
  bool operator==(StructValue const &o) const
  {
    return rpb::deep_equal(*this, o);
  }
};

// google.protobuf.ListValue: repeated Value.
struct ListValue
{
  [[=rpb::field_no<1>{}]] std::vector<Value> values;
  bool operator==(ListValue const &o) const
  {
    return rpb::deep_equal(*this, o);
  }
};

struct TestAllTypesProto3
{
  // Singular scalars (1-15).
  [[=rpb::field_no<1>{}]] std::int32_t optional_int32;
  [[=rpb::field_no<2>{}]] std::int64_t optional_int64;
  [[=rpb::field_no<3>{}]] std::uint32_t optional_uint32;
  [[=rpb::field_no<4>{}]] std::uint64_t optional_uint64;
  [[=rpb::field_no<5>{}]] rpb::SInt<std::int32_t> optional_sint32;
  [[=rpb::field_no<6>{}]] rpb::SInt<std::int64_t> optional_sint64;
  [[=rpb::field_no<7>{}]] rpb::Fixed32 optional_fixed32;
  [[=rpb::field_no<8>{}]] rpb::Fixed64 optional_fixed64;
  [[=rpb::field_no<9>{}]] rpb::SFixed32 optional_sfixed32;
  [[=rpb::field_no<10>{}]] rpb::SFixed64 optional_sfixed64;
  [[=rpb::field_no<11>{}]] float optional_float;
  [[=rpb::field_no<12>{}]] double optional_double;
  [[=rpb::field_no<13>{}]] bool optional_bool;
  [[=rpb::field_no<14>{}]] std::string optional_string;
  [[=rpb::field_no<15>{}]] std::string optional_bytes;

  // Singular message fields use unique_ptr: proto3 message fields have
  // real presence, so "set but empty" (e.g. conformance MESSAGE[0]) must
  // serialize, unlike a value-semantics struct member.
  [[=rpb::field_no<18>{}]] std::unique_ptr<NestedMessage> optional_nested_message;
  [[=rpb::field_no<19>{}]] std::unique_ptr<ForeignMessage> optional_foreign_message;

  [[=rpb::field_no<21>{}]] NestedEnum optional_nested_enum;
  [[=rpb::field_no<22>{}]] ForeignEnum optional_foreign_enum;
  [[=rpb::field_no<23>{}]] AliasedEnum optional_aliased_enum;
  [[=rpb::field_no<24>{}]] std::string optional_string_piece;
  [[=rpb::field_no<25>{}]] std::string optional_cord;
  [[=rpb::field_no<27>{}]] std::unique_ptr<TestAllTypesProto3> recursive_message;

  // Repeated (31-55).
  [[=rpb::field_no<31>{}]] std::vector<std::int32_t> repeated_int32;
  [[=rpb::field_no<32>{}]] std::vector<std::int64_t> repeated_int64;
  [[=rpb::field_no<33>{}]] std::vector<std::uint32_t> repeated_uint32;
  [[=rpb::field_no<34>{}]] std::vector<std::uint64_t> repeated_uint64;
  [[=rpb::field_no<35>{}]] std::vector<rpb::SInt<std::int32_t>> repeated_sint32;
  [[=rpb::field_no<36>{}]] std::vector<rpb::SInt<std::int64_t>> repeated_sint64;
  [[=rpb::field_no<37>{}]] std::vector<rpb::Fixed32> repeated_fixed32;
  [[=rpb::field_no<38>{}]] std::vector<rpb::Fixed64> repeated_fixed64;
  [[=rpb::field_no<39>{}]] std::vector<rpb::SFixed32> repeated_sfixed32;
  [[=rpb::field_no<40>{}]] std::vector<rpb::SFixed64> repeated_sfixed64;
  [[=rpb::field_no<41>{}]] std::vector<float> repeated_float;
  [[=rpb::field_no<42>{}]] std::vector<double> repeated_double;
  [[=rpb::field_no<43>{}]] std::vector<bool> repeated_bool;
  [[=rpb::field_no<44>{}]] std::vector<std::string> repeated_string;
  [[=rpb::field_no<45>{}]] std::vector<std::string> repeated_bytes;

  [[=rpb::field_no<48>{}]] std::vector<NestedMessage> repeated_nested_message;
  [[=rpb::field_no<49>{}]] std::vector<ForeignMessage> repeated_foreign_message;
  [[=rpb::field_no<51>{}]] std::vector<NestedEnum> repeated_nested_enum;
  [[=rpb::field_no<52>{}]] std::vector<ForeignEnum> repeated_foreign_enum;
  [[=rpb::field_no<54>{}]] std::vector<std::string> repeated_string_piece;
  [[=rpb::field_no<55>{}]] std::vector<std::string> repeated_cord;

  // Maps (56-74), single entry in the shared fixture (byte order stable).
  // sint/fixed keys (60-65) use the rpb wire wrappers as std::map keys.
  [[=rpb::field_no<56>{}]] std::map<std::int32_t, std::int32_t> map_int32_int32;
  [[=rpb::field_no<57>{}]] std::map<std::int64_t, std::int64_t> map_int64_int64;
  [[=rpb::field_no<58>{}]] std::map<std::uint32_t, std::uint32_t> map_uint32_uint32;
  [[=rpb::field_no<59>{}]] std::map<std::uint64_t, std::uint64_t> map_uint64_uint64;
  [[=rpb::field_no<60>{}]]
  std::map<rpb::SInt<std::int32_t>, rpb::SInt<std::int32_t>> map_sint32_sint32;
  [[=rpb::field_no<61>{}]]
  std::map<rpb::SInt<std::int64_t>, rpb::SInt<std::int64_t>> map_sint64_sint64;
  [[=rpb::field_no<62>{}]] std::map<rpb::Fixed32, rpb::Fixed32> map_fixed32_fixed32;
  [[=rpb::field_no<63>{}]] std::map<rpb::Fixed64, rpb::Fixed64> map_fixed64_fixed64;
  [[=rpb::field_no<64>{}]] std::map<rpb::SFixed32, rpb::SFixed32> map_sfixed32_sfixed32;
  [[=rpb::field_no<65>{}]] std::map<rpb::SFixed64, rpb::SFixed64> map_sfixed64_sfixed64;
  [[=rpb::field_no<66>{}]] std::map<std::int32_t, float> map_int32_float;
  [[=rpb::field_no<67>{}]] std::map<std::int32_t, double> map_int32_double;
  [[=rpb::field_no<68>{}]] std::map<bool, bool> map_bool_bool;
  [[=rpb::field_no<69>{}]] std::map<std::string, std::string> map_string_string;
  [[=rpb::field_no<70>{}]] std::map<std::string, std::string> map_string_bytes;
  [[=rpb::field_no<71>{}]] std::map<std::string, NestedMessage> map_string_nested_message;
  [[=rpb::field_no<72>{}]] std::map<std::string, ForeignMessage> map_string_foreign_message;
  [[=rpb::field_no<73>{}]] std::map<std::string, NestedEnum> map_string_nested_enum;
  [[=rpb::field_no<74>{}]] std::map<std::string, ForeignEnum> map_string_foreign_enum;

  // Packed repeated (75-88).
  [[=rpb::field_no<75>{}]] std::vector<std::int32_t> packed_int32;
  [[=rpb::field_no<76>{}]] std::vector<std::int64_t> packed_int64;
  [[=rpb::field_no<77>{}]] std::vector<std::uint32_t> packed_uint32;
  [[=rpb::field_no<78>{}]] std::vector<std::uint64_t> packed_uint64;
  [[=rpb::field_no<79>{}]] std::vector<rpb::SInt<std::int32_t>> packed_sint32;
  [[=rpb::field_no<80>{}]] std::vector<rpb::SInt<std::int64_t>> packed_sint64;
  [[=rpb::field_no<81>{}]] std::vector<rpb::Fixed32> packed_fixed32;
  [[=rpb::field_no<82>{}]] std::vector<rpb::Fixed64> packed_fixed64;
  [[=rpb::field_no<83>{}]] std::vector<rpb::SFixed32> packed_sfixed32;
  [[=rpb::field_no<84>{}]] std::vector<rpb::SFixed64> packed_sfixed64;
  [[=rpb::field_no<85>{}]] std::vector<float> packed_float;
  [[=rpb::field_no<86>{}]] std::vector<double> packed_double;
  [[=rpb::field_no<87>{}]] std::vector<bool> packed_bool;
  [[=rpb::field_no<88>{}]] std::vector<NestedEnum> packed_nested_enum;

  // Unpacked repeated (89-102), [packed=false] -> Unpacked wrapper.
  [[=rpb::field_no<89>{}]] std::vector<rpb::Unpacked<std::int32_t>> unpacked_int32;
  [[=rpb::field_no<90>{}]] std::vector<rpb::Unpacked<std::int64_t>> unpacked_int64;
  [[=rpb::field_no<91>{}]] std::vector<rpb::Unpacked<std::uint32_t>> unpacked_uint32;
  [[=rpb::field_no<92>{}]] std::vector<rpb::Unpacked<std::uint64_t>> unpacked_uint64;
  [[=rpb::field_no<93>{}]] std::vector<rpb::Unpacked<rpb::SInt<std::int32_t>>> unpacked_sint32;
  [[=rpb::field_no<94>{}]] std::vector<rpb::Unpacked<rpb::SInt<std::int64_t>>> unpacked_sint64;
  [[=rpb::field_no<95>{}]] std::vector<rpb::Unpacked<rpb::Fixed32>> unpacked_fixed32;
  [[=rpb::field_no<96>{}]] std::vector<rpb::Unpacked<rpb::Fixed64>> unpacked_fixed64;
  [[=rpb::field_no<97>{}]] std::vector<rpb::Unpacked<rpb::SFixed32>> unpacked_sfixed32;
  [[=rpb::field_no<98>{}]] std::vector<rpb::Unpacked<rpb::SFixed64>> unpacked_sfixed64;
  [[=rpb::field_no<99>{}]] std::vector<rpb::Unpacked<float>> unpacked_float;
  [[=rpb::field_no<100>{}]] std::vector<rpb::Unpacked<double>> unpacked_double;
  [[=rpb::field_no<101>{}]] std::vector<rpb::Unpacked<bool>> unpacked_bool;
  [[=rpb::field_no<102>{}]] std::vector<rpb::Unpacked<NestedEnum>> unpacked_nested_enum;

  // Oneof (111-120).  oneof_bytes (114) is rpb::Bytes so the variant can
  // hold both string and bytes alternatives.
  [[=rpb::field_no<111>{}, =rpb::field_no<112>{}, =rpb::field_no<114>{},
    =rpb::field_no<113>{}, =rpb::field_no<115>{}, =rpb::field_no<116>{},
    =rpb::field_no<117>{}, =rpb::field_no<118>{}, =rpb::field_no<119>{},
    =rpb::field_no<120>{}]]
  rpb::OneOf<std::uint32_t, NestedMessage, rpb::Bytes, std::string, bool,
             std::uint64_t, float, double, NestedEnum, NullValue>
      oneof_field;

  // Optional wrappers (201-209).
  [[=rpb::field_no<201>{}]] std::unique_ptr<BoolValue> optional_bool_wrapper;
  [[=rpb::field_no<202>{}]] std::unique_ptr<Int32Value> optional_int32_wrapper;
  [[=rpb::field_no<203>{}]] std::unique_ptr<Int64Value> optional_int64_wrapper;
  [[=rpb::field_no<204>{}]] std::unique_ptr<UInt32Value> optional_uint32_wrapper;
  [[=rpb::field_no<205>{}]] std::unique_ptr<UInt64Value> optional_uint64_wrapper;
  [[=rpb::field_no<206>{}]] std::unique_ptr<FloatValue> optional_float_wrapper;
  [[=rpb::field_no<207>{}]] std::unique_ptr<DoubleValue> optional_double_wrapper;
  [[=rpb::field_no<208>{}]] std::unique_ptr<StringValue> optional_string_wrapper;
  [[=rpb::field_no<209>{}]] std::unique_ptr<BytesValue> optional_bytes_wrapper;

  // Well-known types with mutual recursion (Struct/Value/ListValue).
  [[=rpb::field_no<304>{}]] std::unique_ptr<StructValue> optional_struct;
  [[=rpb::field_no<306>{}]] std::unique_ptr<Value> optional_value;
  [[=rpb::field_no<307>{}]] NullValue optional_null_value;
  [[=rpb::field_no<316>{}]] std::vector<Value> repeated_value;
  [[=rpb::field_no<317>{}]] std::vector<ListValue> repeated_list_value;
  [[=rpb::field_no<324>{}]] std::vector<StructValue> repeated_struct;

  rpb::UnknownFields unknown;  // conformance: unknown fields preserved

  bool operator==(TestAllTypesProto3 const &o) const
  {
    return rpb::deep_equal(*this, o);
  }
};

}  // namespace tmm
