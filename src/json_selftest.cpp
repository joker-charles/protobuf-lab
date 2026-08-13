// JSON codec self-test: parse + serialize + re-parse round trips across the
// proto3 JSON features, plus a few JSON-output exactness checks.
#include "json.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                        \
  do                                                                       \
    {                                                                      \
      if (!(cond))                                                         \
        {                                                                  \
          ++failures;                                                      \
          std::printf("JSON selftest FAIL %s:%d  %s\n", __FILE__, __LINE__, \
                      #cond);                                              \
        }                                                                  \
    }                                                                      \
  while (0)

#define CHECK_JSON(msg, needle)                                            \
  do                                                                       \
    {                                                                      \
      if ((msg).find(needle) == std::string::npos)                         \
        {                                                                  \
          ++failures;                                                      \
          std::printf("JSON selftest FAIL %s:%d missing '%s' in %s\n",     \
                      __FILE__, __LINE__, needle, (msg).c_str());          \
        }                                                                  \
    }                                                                      \
  while (0)

// Parse ``json`` into T, serialize back, then re-parse the JSON output and
// check that the two message instances are deep-equal.
template <typename T>
static void roundtrip(std::string const &json)
{
  T a;
  if (!rpb::json_parse(json, a))
    {
      ++failures;
      std::printf("JSON selftest FAIL parse of %s\n", json.c_str());
      return;
    }
  std::string out;
  if (!rpb::json_serialize(out, a))
    {
      ++failures;
      std::printf("JSON selftest FAIL serialize of %s\n", json.c_str());
      return;
    }
  T b;
  if (!rpb::json_parse(out, b))
    {
      ++failures;
      std::printf("JSON selftest FAIL re-parse of %s\n", out.c_str());
      return;
    }
  if (!rpb::deep_equal(a, b))
    {
      ++failures;
      std::printf("JSON selftest FAIL round-trip mismatch: %s -> %s\n",
                  json.c_str(), out.c_str());
    }
}

int main()
{
  // Scalars, bytes (base64), enums (by name), nested messages.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalInt32":-32,"optionalInt64":"-64","optionalFloat":1.5,
          "optionalString":"hello","optionalBytes":"AQID",
          "optionalNestedEnum":"BAZ","optionalNestedMessage":{"a":123}})");
  // proto + camelCase input names.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optional_int32":7,"optionalString":"x","fieldname1":1})");
  // All-field null acceptance; Value null -> null_value.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalInt32":null,"optionalValue":null,"optionalBool":null})");
  // Repeated, packed-able, string-keyed map, bool-keyed map.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"repeatedInt32":[1,2,3],"mapStringString":{"a":"b","c":"d"},
          "mapBoolBool":{"true":true,"false":false}})");
  // Oneof (present alternative serializes; a second alternative is a dup).
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"oneofUint32":42,"optionalInt32":1})");
  // WKT: Timestamp / Duration / FieldMask.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalTimestamp":"1970-01-01T00:00:00Z",
          "optionalDuration":"1.5s","optionalFieldMask":"foo,barBaz"})");
  // Struct / Value / ListValue native JSON.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalStruct":{"a":1,"b":[1,2,3],"c":null},
          "optionalValue":{"x":[1,2]}})");
  // Any: Duration (WKT short form) and embedded TestAllTypesProto3.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalAny":{"@type":"type.googleapis.com/google.protobuf.Duration",
          "value":"1.5s"}})");
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalAny":{"@type":"type.googleapis.com/protobuf_test_messages.proto3.TestAllTypesProto3",
          "optionalInt32":99}})");
  // Any with nested Any.
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalAny":{"@type":"type.googleapis.com/google.protobuf.Any",
          "value":{"@type":"type.googleapis.com/protobuf_test_messages.proto3.TestAllTypesProto3",
                   "optionalInt32":12345}}})");

  // Output exactness spot-checks.
  tmm::TestAllTypesProto3 p;
  if (rpb::json_parse(
          R"({"optionalInt32":-32,"optionalBytes":"AQID","optionalInt64":"-64",
              "optionalUint32":32,"optionalNestedEnum":"BAZ"})",
          p))
    {
      std::string out;
      CHECK(rpb::json_serialize(out, p));
      CHECK_JSON(out, R"("optionalInt32":-32)");
      CHECK_JSON(out, R"("optionalInt64":"-64")");   // 64-bit as string
      CHECK_JSON(out, R"("optionalUint32":32)");      // 32-bit as number
      CHECK_JSON(out, R"("optionalBytes":"AQID")");    // bytes -> base64
      CHECK_JSON(out, R"("optionalNestedEnum":"BAZ")"); // enum -> name
    }

  // Reject ragged/trailing-comma / out-of-range input.
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(R"({"optionalInt32":1,})", e));
  }
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(R"({"optionalDouble":-1.89769e+308})", e));
  }

  std::printf("json_selftest: %s (%d failures)\n",
              failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
