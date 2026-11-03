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

  // --- WKT regression coverage -----------------------------------------

  // J1: Timestamp printing used truncating seconds/86400 division, so every
  // pre-epoch instant with a non-zero time of day came out one day late.
  // Cover both a one-second-before-epoch instant and the exact minimum, plus
  // a pre-epoch midnight (remainder 0) to show the borrow is conditional.
  {
    tmm::TestAllTypesProto3 p;
    CHECK(rpb::json_parse(
        R"({"optionalTimestamp":"1969-12-31T23:59:59Z"})", p));
    std::string out;
    CHECK(rpb::json_serialize(out, p));
    CHECK_JSON(out, R"("optionalTimestamp":"1969-12-31T23:59:59Z")");
    roundtrip<tmm::TestAllTypesProto3>(
        R"({"optionalTimestamp":"1969-12-31T23:59:59Z"})");
  }
  {
    tmm::TestAllTypesProto3 p;
    CHECK(rpb::json_parse(
        R"({"optionalTimestamp":"0001-01-01T00:00:00Z"})", p));
    std::string out;
    CHECK(rpb::json_serialize(out, p));
    CHECK_JSON(out, R"("optionalTimestamp":"0001-01-01T00:00:00Z")");
    roundtrip<tmm::TestAllTypesProto3>(
        R"({"optionalTimestamp":"0001-01-01T00:00:00Z"})");
  }
  {
    tmm::TestAllTypesProto3 p;
    CHECK(rpb::json_parse(
        R"({"optionalTimestamp":"1969-12-31T00:00:00Z"})", p));
    std::string out;
    CHECK(rpb::json_serialize(out, p));
    CHECK_JSON(out, R"("optionalTimestamp":"1969-12-31T00:00:00Z")");
    roundtrip<tmm::TestAllTypesProto3>(
        R"({"optionalTimestamp":"1969-12-31T00:00:00Z"})");
  }

  // J2: a Duration whose seconds field is zero but whose nanos are negative
  // lost the '-' ("-0.5s" printed as "0.5s"): the sign belongs to the whole
  // value and must be formatted separately from the absolute magnitude.
  {
    tmm::TestAllTypesProto3 p;
    CHECK(rpb::json_parse(R"({"optionalDuration":"-0.5s"})", p));
    CHECK(p.optional_duration != nullptr);
    if (p.optional_duration)
      {
        CHECK(p.optional_duration->seconds == 0);
        CHECK(p.optional_duration->nanos == -500000000);
      }
    std::string out;
    CHECK(rpb::json_serialize(out, p));
    CHECK_JSON(out, R"("optionalDuration":"-0.500s")");
    roundtrip<tmm::TestAllTypesProto3>(R"({"optionalDuration":"-0.5s"})");
  }
  // Negative seconds and nanos together still keep one shared sign.
  roundtrip<tmm::TestAllTypesProto3>(R"({"optionalDuration":"-1.5s"})");

  // J3: FieldMask JSON is lowerCamelCase; parsing "fooBar.bazQux" must yield
  // the snake_case proto path foo_bar.baz_qux and printing must reverse it.
  {
    tmm::TestAllTypesProto3 p;
    CHECK(rpb::json_parse(
        R"({"optionalFieldMask":"fooBar.bazQux"})", p));
    CHECK(p.optional_field_mask != nullptr);
    if (p.optional_field_mask)
      {
        CHECK(p.optional_field_mask->paths.size() == 1);
        CHECK(p.optional_field_mask->paths[0] == "foo_bar.baz_qux");
      }
    std::string out;
    CHECK(rpb::json_serialize(out, p));
    CHECK_JSON(out, R"("optionalFieldMask":"fooBar.bazQux")");
    roundtrip<tmm::TestAllTypesProto3>(
        R"({"optionalFieldMask":"fooBar.bazQux"})");
  }
  // '.'/'_'/digits are accepted verbatim (protobuf's C++ JSON parser does the
  // same; the conformance Recommended.FieldMaskInvalidCharacter case is a
  // known C++ non-conformance and is not enforced), and no '_' is invented
  // before a digit.
  {
    tmm::FieldMask fm;
    CHECK(rpb::json_parse(R"("foo,bar_bar")", fm));
    CHECK(fm.paths.size() == 2);
    if (fm.paths.size() == 2)
      CHECK(fm.paths[1] == "bar_bar");
    CHECK(rpb::json_parse(R"("foo3Bar")", fm));
    CHECK(fm.paths.size() == 1);
    if (fm.paths.size() == 1)
      CHECK(fm.paths[0] == "foo3_bar");
  }

  // J4: calendar / clock / offset validation.  protobuf rejects impossible
  // dates and out-of-range fields; leap days are real dates.
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(
        R"({"optionalTimestamp":"2021-02-31T00:00:00Z"})", e));
  }
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(
        R"({"optionalTimestamp":"2019-02-29T00:00:00Z"})", e));
  }
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(
        R"({"optionalTimestamp":"0001-01-01T00:00:60Z"})", e));
  }
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(
        R"({"optionalTimestamp":"2020-01-01T00:00:00+24:00"})", e));
  }
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(
        R"({"optionalTimestamp":"2020-01-01T00:00:00+99:99"})", e));
  }
  roundtrip<tmm::TestAllTypesProto3>(
      R"({"optionalTimestamp":"2020-02-29T00:00:00Z"})");
  // More than nine fractional digits are truncated to nanosecond precision,
  // matching protobuf, but at least one digit is still required.
  {
    tmm::Timestamp ts;
    CHECK(rpb::json_parse(R"("1970-01-01T00:00:00.123456789123Z")", ts));
    CHECK(ts.seconds == 0);
    CHECK(ts.nanos == 123456789);
    CHECK(!rpb::json_parse(R"("1970-01-01T00:00:00.Z")", ts));
  }

  // J5: at top level a well-known type uses its own JSON form instead of a
  // field-keyed object; ordinary messages still require an object root.
  {
    tmm::Timestamp ts;
    CHECK(rpb::json_parse(R"("1969-12-31T23:59:59Z")", ts));
    CHECK(ts.seconds == -1);
    CHECK(ts.nanos == 0);
    std::string out;
    CHECK(rpb::json_serialize(out, ts));
    CHECK(out == R"("1969-12-31T23:59:59Z")");
  }
  {
    tmm::Duration d;
    CHECK(rpb::json_parse(R"("-0.5s")", d));
    CHECK(d.seconds == 0);
    CHECK(d.nanos == -500000000);
    std::string out;
    CHECK(rpb::json_serialize(out, d));
    CHECK(out == R"("-0.500s")");
  }
  {
    tmm::FieldMask fm;
    CHECK(rpb::json_parse(R"("fooBar.bazQux")", fm));
    CHECK(fm.paths.size() == 1);
    if (fm.paths.size() == 1)
      CHECK(fm.paths[0] == "foo_bar.baz_qux");
    std::string out;
    CHECK(rpb::json_serialize(out, fm));
    CHECK(out == R"("fooBar.bazQux")");
  }
  {
    tmm::TestAllTypesProto3 e;
    CHECK(!rpb::json_parse(R"("not an object")", e));
    CHECK(!rpb::json_parse("null", e));
  }
  {
    tmm::Timestamp e;
    CHECK(!rpb::json_parse("null", e));
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
