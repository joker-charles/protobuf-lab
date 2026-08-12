// Differential interop binary for the official TestAllTypesProto3 mirror
// (see test_messages.hpp).  The fixture values here must stay in sync with
// tests/ref_main_tt.cc (protobuf side).

#include "test_messages.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static tmm::TestAllTypesProto3 make_fixture()
{
  tmm::TestAllTypesProto3 p;

  // Singular scalars.
  p.optional_int32 = -32;
  p.optional_int64 = -64;
  p.optional_uint32 = 32;
  p.optional_uint64 = 64;
  p.optional_sint32 = rpb::SInt<std::int32_t>{-5};
  p.optional_sint64 = rpb::SInt<std::int64_t>{-1234567890123LL};
  p.optional_fixed32 = rpb::Fixed32{0xCAFEBABEu};
  p.optional_fixed64 = rpb::Fixed64{0x1122334455667788ull};
  p.optional_sfixed32 = rpb::SFixed32{-7};
  p.optional_sfixed64 = rpb::SFixed64{-8};
  p.optional_float = 1.5f;
  p.optional_double = -2.25;
  p.optional_bool = true;
  p.optional_string = "hello";
  p.optional_bytes = std::string("\x01\x02\x03", 3);

  // Nested messages and enums.
  p.optional_nested_message = std::make_unique<tmm::NestedMessage>();
  p.optional_nested_message->a = 123;
  p.optional_foreign_message = std::make_unique<tmm::ForeignMessage>();
  p.optional_foreign_message->c = 456;
  p.optional_nested_enum = tmm::NestedEnum::BAZ;
  p.optional_foreign_enum = tmm::ForeignEnum::FOREIGN_BAR;
  p.optional_aliased_enum = tmm::AliasedEnum::ALIAS_BAZ;
  p.optional_string_piece = "piece";
  p.optional_cord = "cord";
  p.recursive_message = std::make_unique<tmm::TestAllTypesProto3>();
  p.recursive_message->optional_int32 = 77;
  p.recursive_message->optional_string = "deep";
  p.optional_nested_message->corecursive =
      std::make_unique<tmm::TestAllTypesProto3>();
  p.optional_nested_message->corecursive->optional_uint64 = 88;

  // Repeated.
  p.repeated_int32 = {1, -2};
  p.repeated_int64 = {3, -4};
  p.repeated_uint32 = {5, 6};
  p.repeated_uint64 = {7, 8};
  p.repeated_sint32 = {rpb::SInt<std::int32_t>{-1}, rpb::SInt<std::int32_t>{2}};
  p.repeated_sint64 = {rpb::SInt<std::int64_t>{-3}, rpb::SInt<std::int64_t>{4}};
  p.repeated_fixed32 = {rpb::Fixed32{1}, rpb::Fixed32{2}};
  p.repeated_fixed64 = {rpb::Fixed64{3}, rpb::Fixed64{4}};
  p.repeated_sfixed32 = {rpb::SFixed32{-1}, rpb::SFixed32{-2}};
  p.repeated_sfixed64 = {rpb::SFixed64{-3}, rpb::SFixed64{-4}};
  p.repeated_float = {1.5f, -2.5f};
  p.repeated_double = {3.5, -4.5};
  p.repeated_bool = {true, false, true};
  p.repeated_string = {"a", "bb"};
  p.repeated_bytes = {std::string("\x01", 1), std::string("\x02\x03", 2)};
  p.repeated_nested_message.push_back(tmm::NestedMessage{1, nullptr});
  p.repeated_nested_message.push_back(tmm::NestedMessage{2, nullptr});
  p.repeated_foreign_message = {{3}, {4}};
  p.repeated_nested_enum = {tmm::NestedEnum::FOO, tmm::NestedEnum::NEG};
  p.repeated_foreign_enum = {tmm::ForeignEnum::FOREIGN_BAZ,
                             tmm::ForeignEnum::FOREIGN_FOO};
  p.repeated_string_piece = {"p1", "p2"};
  p.repeated_cord = {"c1", "c2"};

  // Maps (single entry each; protobuf map order is unspecified).
  p.map_int32_int32 = {{1, 2}};
  p.map_int64_int64 = {{3, 4}};
  p.map_uint32_uint32 = {{5, 6}};
  p.map_uint64_uint64 = {{7, 8}};
  p.map_sint32_sint32 = {{rpb::SInt<std::int32_t>{-1},
                          rpb::SInt<std::int32_t>{-2}}};
  p.map_sint64_sint64 = {{rpb::SInt<std::int64_t>{-3},
                          rpb::SInt<std::int64_t>{-4}}};
  p.map_fixed32_fixed32 = {{rpb::Fixed32{0x11111111u},
                            rpb::Fixed32{0x22222222u}}};
  p.map_fixed64_fixed64 = {{rpb::Fixed64{0x3333333333333333ull},
                            rpb::Fixed64{0x4444444444444444ull}}};
  p.map_sfixed32_sfixed32 = {{rpb::SFixed32{-5}, rpb::SFixed32{-6}}};
  p.map_sfixed64_sfixed64 = {{rpb::SFixed64{-7}, rpb::SFixed64{-8}}};
  p.map_int32_float = {{9, 1.5f}};
  p.map_int32_double = {{10, -2.5}};
  p.map_bool_bool = {{true, false}};
  p.map_string_string = {{"k", "v"}};
  p.map_string_bytes = {{"k", std::string("\x01\x02", 2)}};
  p.map_string_nested_message["k"] = tmm::NestedMessage{7, nullptr};
  p.map_string_foreign_message = {{"k", tmm::ForeignMessage{8}}};
  p.map_string_nested_enum = {{"k", tmm::NestedEnum::BAR}};
  p.map_string_foreign_enum = {{"k", tmm::ForeignEnum::FOREIGN_BAZ}};

  // Packed.
  p.packed_int32 = {3, -4};
  p.packed_int64 = {5, -6};
  p.packed_uint32 = {7, 8};
  p.packed_uint64 = {9, 10};
  p.packed_sint32 = {rpb::SInt<std::int32_t>{-5}, rpb::SInt<std::int32_t>{6}};
  p.packed_sint64 = {rpb::SInt<std::int64_t>{-7}, rpb::SInt<std::int64_t>{8}};
  p.packed_fixed32 = {rpb::Fixed32{9}, rpb::Fixed32{10}};
  p.packed_fixed64 = {rpb::Fixed64{11}, rpb::Fixed64{12}};
  p.packed_sfixed32 = {rpb::SFixed32{-13}, rpb::SFixed32{-14}};
  p.packed_sfixed64 = {rpb::SFixed64{-15}, rpb::SFixed64{-16}};
  p.packed_float = {1.25f, -2.5f};
  p.packed_double = {3.5, -4.5};
  p.packed_bool = {true, false, true};
  p.packed_nested_enum = {tmm::NestedEnum::FOO, tmm::NestedEnum::NEG};

  // Unpacked ([packed=false]).
  p.unpacked_int32 = {rpb::Unpacked<std::int32_t>{1},
                      rpb::Unpacked<std::int32_t>{-2}};
  p.unpacked_int64 = {rpb::Unpacked<std::int64_t>{3},
                      rpb::Unpacked<std::int64_t>{-4}};
  p.unpacked_uint32 = {rpb::Unpacked<std::uint32_t>{5},
                       rpb::Unpacked<std::uint32_t>{6}};
  p.unpacked_uint64 = {rpb::Unpacked<std::uint64_t>{7},
                       rpb::Unpacked<std::uint64_t>{8}};
  p.unpacked_sint32 = {rpb::Unpacked<rpb::SInt<std::int32_t>>{{-1}},
                       rpb::Unpacked<rpb::SInt<std::int32_t>>{{2}}};
  p.unpacked_sint64 = {rpb::Unpacked<rpb::SInt<std::int64_t>>{{-3}},
                       rpb::Unpacked<rpb::SInt<std::int64_t>>{{4}}};
  p.unpacked_fixed32 = {rpb::Unpacked<rpb::Fixed32>{{1}},
                        rpb::Unpacked<rpb::Fixed32>{{2}}};
  p.unpacked_fixed64 = {rpb::Unpacked<rpb::Fixed64>{{3}},
                        rpb::Unpacked<rpb::Fixed64>{{4}}};
  p.unpacked_sfixed32 = {rpb::Unpacked<rpb::SFixed32>{{-5}},
                         rpb::Unpacked<rpb::SFixed32>{{-6}}};
  p.unpacked_sfixed64 = {rpb::Unpacked<rpb::SFixed64>{{-7}},
                         rpb::Unpacked<rpb::SFixed64>{{-8}}};
  p.unpacked_float = {rpb::Unpacked<float>{1.5f}, rpb::Unpacked<float>{-2.5f}};
  p.unpacked_double = {rpb::Unpacked<double>{3.5}, rpb::Unpacked<double>{-4.5}};
  p.unpacked_bool = {rpb::Unpacked<bool>{true}, rpb::Unpacked<bool>{false}};
  p.unpacked_nested_enum = {rpb::Unpacked<tmm::NestedEnum>{tmm::NestedEnum::BAR},
                            rpb::Unpacked<tmm::NestedEnum>{tmm::NestedEnum::NEG}};

  // Oneof: alternative B (oneof_nested_message).
  p.oneof_field = tmm::NestedMessage{99, nullptr};

  // Optional wrappers.
  p.optional_bool_wrapper = std::make_unique<tmm::BoolValue>(true);
  p.optional_int32_wrapper = std::make_unique<tmm::Int32Value>(-32);
  p.optional_int64_wrapper = std::make_unique<tmm::Int64Value>(-64);
  p.optional_uint32_wrapper = std::make_unique<tmm::UInt32Value>(32);
  p.optional_uint64_wrapper = std::make_unique<tmm::UInt64Value>(64);
  p.optional_float_wrapper = std::make_unique<tmm::FloatValue>(1.5f);
  p.optional_double_wrapper = std::make_unique<tmm::DoubleValue>(-2.5);
  p.optional_string_wrapper = std::make_unique<tmm::StringValue>("w");
  p.optional_bytes_wrapper =
      std::make_unique<tmm::BytesValue>(std::string("\x09", 1));

  // Well-known types: Struct / Value / ListValue (mutual recursion broken
  // by unique_ptr oneof alternatives).  Maps stay single-entry: protobuf
  // Map serialization order is unspecified (hash order), ours is sorted.
  p.optional_struct = std::make_unique<tmm::StructValue>();
  p.optional_struct->fields["k"].kind = 3.5;
  p.optional_value = std::make_unique<tmm::Value>();
  p.optional_value->kind = std::string("str");
  p.repeated_value.push_back(tmm::Value{});
  p.repeated_value.back().kind = true;
  p.repeated_value.push_back(tmm::Value{});
  p.repeated_value.back().kind = 1.5;
  tmm::ListValue lv;
  lv.values.push_back(tmm::Value{});
  lv.values.back().kind = std::string("x");
  p.repeated_list_value.push_back(std::move(lv));
  tmm::StructValue sv;
  sv.fields["a"].kind = std::make_unique<tmm::StructValue>();
  std::get<5>(sv.fields["a"].kind)->fields["x"].kind = true;
  p.repeated_struct.push_back(std::move(sv));

  return p;
}

static int failures = 0;

static void check(bool ok, char const *label)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", label);
  if (!ok)
    ++failures;
}

static void self_test()
{
  tmm::TestAllTypesProto3 p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  tmm::TestAllTypesProto3 q;
  check(rpb::parse(bytes, q) && q == p, "roundtrip");

  // The 10-alternative oneof with both string and Bytes alternatives.
  p.oneof_field = std::string("note");
  bytes.clear();
  rpb::serialize(bytes, p);
  tmm::TestAllTypesProto3 q2;
  check(rpb::parse(bytes, q2) && q2 == p, "oneof string roundtrip");
  p.oneof_field = rpb::Bytes{std::string("\x01\x02", 2)};
  bytes.clear();
  rpb::serialize(bytes, p);
  tmm::TestAllTypesProto3 q3;
  check(rpb::parse(bytes, q3) && q3 == p, "oneof bytes roundtrip");
}

static void emit_fixture()
{
  tmm::TestAllTypesProto3 p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  std::fwrite(bytes.data(), 1, bytes.size(), stdout);
}

static void parse_file(char const *path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f)
    {
      std::fprintf(stderr, "cannot open %s\n", path);
      std::exit(2);
    }
  std::string data((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());

  tmm::TestAllTypesProto3 p;
  if (!rpb::parse(data, p) || !(p == make_fixture()))
    {
      std::fprintf(stderr, "parse-file: mismatch\n");
      std::exit(1);
    }
  std::printf("parse-file: ok\n");
}

int main(int argc, char **argv)
{
  if (argc > 1 && std::strcmp(argv[1], "--test") == 0)
    {
      self_test();
      std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
      return failures == 0 ? 0 : 1;
    }
  if (argc > 1 && std::strcmp(argv[1], "--emit") == 0)
    {
      emit_fixture();
      return 0;
    }
  if (argc > 1 && std::strcmp(argv[1], "--parse-file") == 0)
    {
      parse_file(argv[2]);
      return 0;
    }

  tmm::TestAllTypesProto3 p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  std::printf("serialized %zu bytes\n", bytes.size());
  return 0;
}
