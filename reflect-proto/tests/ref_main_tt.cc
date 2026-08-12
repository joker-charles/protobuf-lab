// Protobuf-side fixture for the TestAllTypesProto3 differential interop.
// Values must stay in sync with src/main_tt.cpp (our side).  Omitted fields
// in the mirror (oneof_string, Struct/Value/ListValue, etc.) stay unset
// here too, so protobuf never emits them.
#include <cstdio>
#include <cstdlib>
#include <string>

#include <google/protobuf/text_format.h>
#include "google/protobuf/test_messages_proto3.pb.h"

namespace m = protobuf_test_messages::proto3;

static void make_fixture(m::TestAllTypesProto3 &p)
{
  p.set_optional_int32(-32);
  p.set_optional_int64(-64);
  p.set_optional_uint32(32);
  p.set_optional_uint64(64);
  p.set_optional_sint32(-5);
  p.set_optional_sint64(-1234567890123LL);
  p.set_optional_fixed32(0xCAFEBABEu);
  p.set_optional_fixed64(0x1122334455667788ull);
  p.set_optional_sfixed32(-7);
  p.set_optional_sfixed64(-8);
  p.set_optional_float(1.5f);
  p.set_optional_double(-2.25);
  p.set_optional_bool(true);
  p.set_optional_string("hello");
  p.set_optional_bytes("\x01\x02\x03", 3);

  p.mutable_optional_nested_message()->set_a(123);
  p.mutable_optional_foreign_message()->set_c(456);
  p.set_optional_nested_enum(m::TestAllTypesProto3::BAZ);
  p.set_optional_foreign_enum(m::FOREIGN_BAR);
  p.set_optional_aliased_enum(m::TestAllTypesProto3::ALIAS_BAZ);
  // ctype=STRING_PIECE/CORD fields have private accessors in generated 3.21
  // code; set them through TextFormat merge.
  std::string ctype_fields =
      "optional_string_piece: \"piece\" optional_cord: \"cord\" "
      "repeated_string_piece: \"p1\" repeated_string_piece: \"p2\" "
      "repeated_cord: \"c1\" repeated_cord: \"c2\"";
  if (!google::protobuf::TextFormat::MergeFromString(ctype_fields, &p))
    std::abort();
  p.mutable_recursive_message()->set_optional_int32(77);
  p.mutable_recursive_message()->set_optional_string("deep");
  p.mutable_optional_nested_message()->mutable_corecursive()
      ->set_optional_uint64(88);

  p.add_repeated_int32(1);
  p.add_repeated_int32(-2);
  p.add_repeated_int64(3);
  p.add_repeated_int64(-4);
  p.add_repeated_uint32(5);
  p.add_repeated_uint32(6);
  p.add_repeated_uint64(7);
  p.add_repeated_uint64(8);
  p.add_repeated_sint32(-1);
  p.add_repeated_sint32(2);
  p.add_repeated_sint64(-3);
  p.add_repeated_sint64(4);
  p.add_repeated_fixed32(1);
  p.add_repeated_fixed32(2);
  p.add_repeated_fixed64(3);
  p.add_repeated_fixed64(4);
  p.add_repeated_sfixed32(-1);
  p.add_repeated_sfixed32(-2);
  p.add_repeated_sfixed64(-3);
  p.add_repeated_sfixed64(-4);
  p.add_repeated_float(1.5f);
  p.add_repeated_float(-2.5f);
  p.add_repeated_double(3.5);
  p.add_repeated_double(-4.5);
  p.add_repeated_bool(true);
  p.add_repeated_bool(false);
  p.add_repeated_bool(true);
  p.add_repeated_string("a");
  p.add_repeated_string("bb");
  p.add_repeated_bytes("\x01", 1);
  p.add_repeated_bytes("\x02\x03", 2);
  p.add_repeated_nested_message()->set_a(1);
  p.add_repeated_nested_message()->set_a(2);
  p.add_repeated_foreign_message()->set_c(3);
  p.add_repeated_foreign_message()->set_c(4);
  p.add_repeated_nested_enum(m::TestAllTypesProto3::FOO);
  p.add_repeated_nested_enum(m::TestAllTypesProto3::NEG);
  p.add_repeated_foreign_enum(m::FOREIGN_BAZ);
  p.add_repeated_foreign_enum(m::FOREIGN_FOO);

  (*p.mutable_map_int32_int32())[1] = 2;
  (*p.mutable_map_int64_int64())[3] = 4;
  (*p.mutable_map_uint32_uint32())[5] = 6;
  (*p.mutable_map_uint64_uint64())[7] = 8;
  (*p.mutable_map_sint32_sint32())[-1] = -2;
  (*p.mutable_map_sint64_sint64())[-3] = -4;
  (*p.mutable_map_fixed32_fixed32())[0x11111111u] = 0x22222222u;
  (*p.mutable_map_fixed64_fixed64())[0x3333333333333333ull] =
      0x4444444444444444ull;
  (*p.mutable_map_sfixed32_sfixed32())[-5] = -6;
  (*p.mutable_map_sfixed64_sfixed64())[-7] = -8;
  (*p.mutable_map_int32_float())[9] = 1.5f;
  (*p.mutable_map_int32_double())[10] = -2.5;
  (*p.mutable_map_bool_bool())[true] = false;
  (*p.mutable_map_string_string())["k"] = "v";
  (*p.mutable_map_string_bytes())["k"] = "\x01\x02";
  (*p.mutable_map_string_nested_message())["k"].set_a(7);
  (*p.mutable_map_string_foreign_message())["k"].set_c(8);
  (*p.mutable_map_string_nested_enum())["k"] = m::TestAllTypesProto3::BAR;
  (*p.mutable_map_string_foreign_enum())["k"] = m::FOREIGN_BAZ;

  p.add_packed_int32(3);
  p.add_packed_int32(-4);
  p.add_packed_int64(5);
  p.add_packed_int64(-6);
  p.add_packed_uint32(7);
  p.add_packed_uint32(8);
  p.add_packed_uint64(9);
  p.add_packed_uint64(10);
  p.add_packed_sint32(-5);
  p.add_packed_sint32(6);
  p.add_packed_sint64(-7);
  p.add_packed_sint64(8);
  p.add_packed_fixed32(9);
  p.add_packed_fixed32(10);
  p.add_packed_fixed64(11);
  p.add_packed_fixed64(12);
  p.add_packed_sfixed32(-13);
  p.add_packed_sfixed32(-14);
  p.add_packed_sfixed64(-15);
  p.add_packed_sfixed64(-16);
  p.add_packed_float(1.25f);
  p.add_packed_float(-2.5f);
  p.add_packed_double(3.5);
  p.add_packed_double(-4.5);
  p.add_packed_bool(true);
  p.add_packed_bool(false);
  p.add_packed_bool(true);
  p.add_packed_nested_enum(m::TestAllTypesProto3::FOO);
  p.add_packed_nested_enum(m::TestAllTypesProto3::NEG);

  p.add_unpacked_int32(1);
  p.add_unpacked_int32(-2);
  p.add_unpacked_int64(3);
  p.add_unpacked_int64(-4);
  p.add_unpacked_uint32(5);
  p.add_unpacked_uint32(6);
  p.add_unpacked_uint64(7);
  p.add_unpacked_uint64(8);
  p.add_unpacked_sint32(-1);
  p.add_unpacked_sint32(2);
  p.add_unpacked_sint64(-3);
  p.add_unpacked_sint64(4);
  p.add_unpacked_fixed32(1);
  p.add_unpacked_fixed32(2);
  p.add_unpacked_fixed64(3);
  p.add_unpacked_fixed64(4);
  p.add_unpacked_sfixed32(-5);
  p.add_unpacked_sfixed32(-6);
  p.add_unpacked_sfixed64(-7);
  p.add_unpacked_sfixed64(-8);
  p.add_unpacked_float(1.5f);
  p.add_unpacked_float(-2.5f);
  p.add_unpacked_double(3.5);
  p.add_unpacked_double(-4.5);
  p.add_unpacked_bool(true);
  p.add_unpacked_bool(false);
  p.add_unpacked_nested_enum(m::TestAllTypesProto3::BAR);
  p.add_unpacked_nested_enum(m::TestAllTypesProto3::NEG);

  p.mutable_oneof_nested_message()->set_a(99);

  p.mutable_optional_bool_wrapper()->set_value(true);
  p.mutable_optional_int32_wrapper()->set_value(-32);
  p.mutable_optional_int64_wrapper()->set_value(-64);
  p.mutable_optional_uint32_wrapper()->set_value(32);
  p.mutable_optional_uint64_wrapper()->set_value(64);
  p.mutable_optional_float_wrapper()->set_value(1.5f);
  p.mutable_optional_double_wrapper()->set_value(-2.5);
  p.mutable_optional_string_wrapper()->set_value("w");
  p.mutable_optional_bytes_wrapper()->set_value("\x09", 1);

  auto &os = *p.mutable_optional_struct();
  (*os.mutable_fields())["k"].set_number_value(3.5);
  p.mutable_optional_value()->set_string_value("str");
  p.add_repeated_value()->set_bool_value(true);
  p.add_repeated_value()->set_number_value(1.5);
  p.add_repeated_list_value()->add_values()->set_string_value("x");
  (*(*p.add_repeated_struct()->mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["x"].set_bool_value(true);
}

int main()
{
  m::TestAllTypesProto3 p;
  make_fixture(p);
  std::string s;
  if (!p.SerializeToString(&s))
    return 1;
  std::fwrite(s.data(), 1, s.size(), stdout);
  return 0;
}
