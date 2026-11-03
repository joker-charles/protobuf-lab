// JSON + text-format interop for the well-known types, compared against
// protoc-generated code from protobuf's own test_messages_proto3.proto.
//
// The reflection fixtures and the generated reference message are filled
// with the same values; each side's JSON/text output is then parsed by the
// OTHER implementation and the resulting wire bytes are compared.  This
// catches mapping bugs that conformance's fixed fixtures do not cover
// (pre-epoch Timestamps, negative-nanos Durations, dotted FieldMasks,
// message/enum-valued maps, google.protobuf.Value field names).
#include "json.hpp"
#include "test_messages.hpp"
#include "text_format.hpp"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/test_messages_proto3.pb.h>

#include <cstdio>
#include <string>

namespace pb = protobuf_test_messages::proto3;

static int failures = 0;

#define CHECK(cond)                                                      \
  do                                                                     \
    {                                                                    \
      if (!(cond))                                                       \
        {                                                                \
          ++failures;                                                    \
          std::printf("interop_wkt FAIL %s:%d  %s\n", __FILE__, __LINE__, \
                      #cond);                                            \
        }                                                                \
    }                                                                    \
  while (0)

static void build(tmm::TestAllTypesProto3 &m)
{
  m.optional_timestamp = std::make_unique<tmm::Timestamp>();
  m.optional_timestamp->seconds = -1;  // 1969-12-31T23:59:59Z
  m.optional_duration = std::make_unique<tmm::Duration>();
  m.optional_duration->nanos = -500000000;  // -0.5s
  m.optional_field_mask = std::make_unique<tmm::FieldMask>();
  m.optional_field_mask->paths = {"foo_bar.baz_qux"};
  m.optional_value = std::make_unique<tmm::Value>();
  m.optional_value->kind = 1.5;
  m.optional_struct = std::make_unique<tmm::StructValue>();
  m.optional_struct->fields["a"].kind = 1.5;
  m.repeated_value.emplace_back();
  m.repeated_value.back().kind = 2.0;
  m.repeated_list_value.emplace_back();
  m.repeated_list_value.back().values.emplace_back();
  m.repeated_list_value.back().values.back().kind = true;
  m.repeated_struct.emplace_back();
  m.repeated_struct.back().fields["n"].kind = 7.0;
  m.map_string_nested_message["k"].a = 3;
  m.map_string_nested_enum["k"] = tmm::NestedEnum::BAZ;
  m.optional_nested_enum = static_cast<tmm::NestedEnum>(-5);  // unknown
}

static void build(pb::TestAllTypesProto3 &m)
{
  m.mutable_optional_timestamp()->set_seconds(-1);
  m.mutable_optional_duration()->set_nanos(-500000000);
  m.mutable_optional_field_mask()->add_paths("foo_bar.baz_qux");
  m.mutable_optional_value()->set_number_value(1.5);
  (*m.mutable_optional_struct()->mutable_fields())["a"].set_number_value(1.5);
  m.add_repeated_value()->set_number_value(2.0);
  m.add_repeated_list_value()->add_values()->set_bool_value(true);
  (*m.add_repeated_struct()->mutable_fields())["n"].set_number_value(7);
  (*m.mutable_map_string_nested_message())["k"].set_a(3);
  (*m.mutable_map_string_nested_enum())["k"] = pb::TestAllTypesProto3::BAZ;
  m.set_optional_nested_enum(
      static_cast<pb::TestAllTypesProto3::NestedEnum>(-5));
}

static std::string json_of(pb::TestAllTypesProto3 const &m)
{
  std::string out;
  google::protobuf::util::JsonPrintOptions opt;
  google::protobuf::util::MessageToJsonString(m, &out, opt);
  return out;
}

int main()
{
  tmm::TestAllTypesProto3 ours;
  pb::TestAllTypesProto3 ref;
  build(ours);
  build(ref);

  std::string ours_json;
  CHECK(rpb::json_serialize(ours_json, ours));
  CHECK(rpb::json_parse(json_of(ref), ours));  // ref JSON -> our parser

  // Exact substrings for the value mappings this test exists for.
  CHECK(ours_json.find("\"optionalTimestamp\":\"1969-12-31T23:59:59Z\"")
        != std::string::npos);
  CHECK(ours_json.find("\"optionalDuration\":\"-0.500s\"")
        != std::string::npos);
  CHECK(ours_json.find("\"optionalFieldMask\":\"fooBar.bazQux\"")
        != std::string::npos);

  // Our JSON must be understood by protobuf and round-trip to the same
  // message (map order / <>& escaping differences are semantically equal).
  pb::TestAllTypesProto3 parsed;
  google::protobuf::util::JsonParseOptions jp;
  CHECK(google::protobuf::util::JsonStringToMessage(ours_json, &parsed, jp)
            .ok());
  CHECK(parsed.SerializeAsString() == ref.SerializeAsString());

  // Same in the other direction: re-emit protobuf's JSON and compare text
  // output semantics through protobuf's own parser.
  tmm::TestAllTypesProto3 ours2;
  CHECK(rpb::json_parse(json_of(ref), ours2));
  CHECK(rpb::deep_equal(ours, ours2));

  // Text format: our printer must feed protobuf's parser and vice versa.
  std::string ours_text;
  rpb::text_format_print(ours_text, ours);
  CHECK(ours_text.find("number_value:") != std::string::npos);
  CHECK(ours_text.find("value {") != std::string::npos);
  pb::TestAllTypesProto3 text_parsed;
  CHECK(google::protobuf::TextFormat::ParseFromString(ours_text,
                                                      &text_parsed));
  CHECK(text_parsed.SerializeAsString() == ref.SerializeAsString());

  std::string ref_text;
  CHECK(google::protobuf::TextFormat::PrintToString(ref, &ref_text));
  tmm::TestAllTypesProto3 text_ours;
  CHECK(rpb::text_format_parse(ref_text, text_ours));
  CHECK(rpb::deep_equal(ours, text_ours));

  // google.protobuf.Value's top-level JSON null must round-trip: print
  // "null", parse "null" back to null_value (other WKTs reject null roots).
  {
    tmm::Value val;
    val.kind = tmm::NullValue::NULL_VALUE;
    std::string js;
    CHECK(rpb::json_serialize(js, val) && js == "null");
    tmm::Value back;
    CHECK(rpb::json_parse("null", back));
    CHECK(rpb::deep_equal(val, back));
    tmm::Timestamp not_null;
    CHECK(!rpb::json_parse("null", not_null));
  }

  std::printf("interop_wkt: %s (%d failures)\n",
              failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
