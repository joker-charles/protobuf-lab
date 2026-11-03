// Text-format codec self-test: parse + print round trips and rejection
// cases for the TextFormat semantics fixed in the text-format workstream
// (google.protobuf.Value oneof names, message/enum-valued map entries,
// integer bases and overflow, \x escapes, UTF-8 relaxation, duplicate
// fields, float suffix, list trailing comma, default map keys).
#include "text_format.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                        \
  do                                                                       \
    {                                                                      \
      if (!(cond))                                                         \
        {                                                                  \
          ++failures;                                                      \
          std::printf("text selftest FAIL %s:%d  %s\n", __FILE__, __LINE__, \
                      #cond);                                              \
        }                                                                  \
    }                                                                      \
  while (0)

#define CHECK_HAS(haystack, needle)                                        \
  do                                                                       \
    {                                                                      \
      if ((haystack).find(needle) == std::string::npos)                    \
        {                                                                  \
          ++failures;                                                      \
          std::printf("text selftest FAIL %s:%d missing '%s' in:\n%s\n",   \
                      __FILE__, __LINE__, needle, (haystack).c_str());     \
        }                                                                  \
    }                                                                      \
  while (0)

template <typename M>
static std::string printed(M const &m)
{
  std::string out;
  rpb::text_format_print(out, m);
  return out;
}

// Full text round trip: parse `text`, print the value, re-parse the print
// and require the two messages to be deep-equal (print -> parse must be
// lossless).  `ok` reports both parse directions and the equality check;
// the printed text is returned for exactness spot-checks.
template <typename M>
static std::string roundtrip(std::string_view text, bool &ok)
{
  M a;
  ok = rpb::text_format_parse(text, a);
  if (!ok)
    return {};
  std::string out = printed(a);
  M b;
  ok = rpb::text_format_parse(out, b) && rpb::deep_equal(a, b);
  return out;
}

template <typename M>
static bool roundtrip(std::string_view text)
{
  bool ok = false;
  (void)roundtrip<M>(text, ok);
  return ok;
}

int main()
{
  using TT3 = tmm::TestAllTypesProto3;

  // --- google.protobuf.Value: all six oneof alternatives round-trip under
  // their proto field names (JSON is unaffected; it handles Value natively).
  {
    struct case_t
    {
      char const *text;
      char const *name;
    };
    case_t const cases[] = {
        {"optional_value { null_value: NULL_VALUE }", "null_value"},
        {"optional_value { number_value: 1.5 }", "number_value"},
        {"optional_value { string_value: \"s\" }", "string_value"},
        {"optional_value { bool_value: true }", "bool_value"},
        {"optional_value { struct_value { fields { key: \"k\" "
         "value { number_value: 2 } } } }",
         "struct_value"},
        {"optional_value { list_value { values { number_value: 3 } } }",
         "list_value"},
    };
    for (auto const &c : cases)
      {
        bool ok = false;
        std::string out = roundtrip<TT3>(c.text, ok);
        CHECK(ok);
        CHECK_HAS(out, c.name);
        // A missing rpb::name annotation used to print an empty field name
        // ("  : 1.5" / "  {"), which can never be re-parsed.
        CHECK(out.find("  :") == std::string::npos);
      }
  }

  // --- message-valued map entries: canonical `value { ... }` (no colon,
  // matching our printer and protobuf's) parses, and the colon spelling too.
  {
    TT3 m;
    CHECK(rpb::text_format_parse(
        "map_string_nested_message { key: \"k\" value { a: 3 } }", m));
    CHECK(m.map_string_nested_message.size() == 1);
    CHECK(m.map_string_nested_message["k"].a == 3);
    CHECK_HAS(printed(m), "value {");
    CHECK(roundtrip<TT3>(
        "map_string_nested_message { key: \"k\" value { a: 3 } }"));

    TT3 colon;
    CHECK(rpb::text_format_parse(
        "map_string_nested_message { key: \"k\" value: { a: 3 } }", colon));
    CHECK(rpb::deep_equal(m, colon));
  }

  // --- enum-valued map entries: the colon is followed by whitespace, so
  // the enum-name parser must skip it before reading the identifier.
  {
    TT3 m;
    CHECK(rpb::text_format_parse(
        "map_string_nested_enum { key: \"e\" value: BAZ }", m));
    CHECK(m.map_string_nested_enum.size() == 1);
    CHECK(m.map_string_nested_enum["e"] == tmm::NestedEnum::BAZ);
    CHECK_HAS(printed(m), "value: BAZ");
    CHECK(roundtrip<TT3>(
        "map_string_nested_enum { key: \"e\" value: BAZ }"));
  }

  // --- unknown negative enum numbers: the printer emits the underlying
  // number when no enumerator matches, so the parser must range-check
  // against the enum's signed underlying type and accept it back.
  {
    TT3 m;
    CHECK(rpb::text_format_parse("optional_foreign_enum: -1", m));
    CHECK(m.optional_foreign_enum == static_cast<tmm::ForeignEnum>(-1));
    CHECK_HAS(printed(m), "optional_foreign_enum: -1");
    CHECK(roundtrip<TT3>("optional_foreign_enum: -1"));
    // Signed int32 underlying range is enforced (no wrap to -1 / INT_MIN).
    CHECK(!rpb::text_format_parse("optional_foreign_enum: 2147483648", m));
    CHECK(!rpb::text_format_parse("optional_foreign_enum: 4294967295", m));
    CHECK(rpb::text_format_parse("optional_foreign_enum: -2147483648", m));
    // Known negative enumerators still print/parse by name.
    CHECK(rpb::text_format_parse("optional_nested_enum: NEG", m));
    CHECK(m.optional_nested_enum == tmm::NestedEnum::NEG);
    CHECK_HAS(printed(m), "optional_nested_enum: NEG");
  }

  // --- integer literals: decimal, 0x/0X hex, leading-0 octal, overflow.
  {
    TT3 m;
    CHECK(rpb::text_format_parse("optional_int32: 010", m));
    CHECK(m.optional_int32 == 8);
    CHECK(rpb::text_format_parse("optional_int32: 0x10", m));
    CHECK(m.optional_int32 == 16);
    CHECK(rpb::text_format_parse("optional_int32: 0X1F", m));
    CHECK(m.optional_int32 == 31);
    CHECK(rpb::text_format_parse("optional_int32: -0x10", m));
    CHECK(m.optional_int32 == -16);
    CHECK(rpb::text_format_parse("optional_int32: -010", m));
    CHECK(m.optional_int32 == -8);
    // 8/9 are not octal digits; "0x" needs at least one hex digit.
    CHECK(!rpb::text_format_parse("optional_int32: 08", m));
    CHECK(!rpb::text_format_parse("optional_int32: 09", m));
    CHECK(!rpb::text_format_parse("optional_int32: 0x", m));
    // 2^128, 2^128+1 and 2^64 are rejected instead of wrapping.
    CHECK(!rpb::text_format_parse(
        "optional_uint64: 340282366920938463463374607431768211456", m));
    CHECK(!rpb::text_format_parse(
        "optional_uint64: 340282366920938463463374607431768211457", m));
    CHECK(!rpb::text_format_parse("optional_uint64: 18446744073709551616", m));
    CHECK(rpb::text_format_parse("optional_uint64: 18446744073709551615", m));
    CHECK(m.optional_uint64 == 18446744073709551615ull);
    CHECK(rpb::text_format_parse("optional_uint64: 0xFFFFFFFFFFFFFFFF", m));
    CHECK(m.optional_uint64 == 18446744073709551615ull);
    CHECK(rpb::text_format_parse("optional_int64: -9223372036854775808", m));
    CHECK(m.optional_int64 == std::numeric_limits<std::int64_t>::min());
    CHECK(!rpb::text_format_parse("optional_int64: 9223372036854775808", m));
    CHECK(!rpb::text_format_parse("optional_int32: +1", m));
  }

  // --- \x takes at most two hex digits ("\x4142" is 'A' + "42").
  {
    TT3 m;
    CHECK(rpb::text_format_parse("optional_string: \"\\x4142\"", m));
    CHECK(m.optional_string == "A42");
    CHECK(rpb::text_format_parse("optional_bytes: \"\\x4142\"", m));
    CHECK(m.optional_bytes == "A42");
  }

  // --- UTF-8 handling: the text-format conformance suite requires string
  // fields to reject invalid UTF-8 byte sequences (Required
  // StringFieldBadUTF8Octal/Hex; protobuf's own text parser is lenient, but
  // the spec tests are authoritative).  bytes fields are exempt and accept
  // arbitrary byte escapes.  Consequence: a string member holding invalid
  // UTF-8 prints as octal escapes but does not re-parse - the same
  // print/parse asymmetry protobuf has.
  {
    TT3 m;
    CHECK(!rpb::text_format_parse("optional_string: '\\300'", m));
    CHECK(!rpb::text_format_parse("optional_string: '\\xc0'", m));

    TT3 b;
    CHECK(rpb::text_format_parse("optional_bytes: '\\300'", b));
    CHECK(b.optional_bytes == std::string("\xc0", 1));
    TT3 b2;
    CHECK(rpb::text_format_parse("optional_bytes: \"\\377\"", b2));
    CHECK(b2.optional_bytes == std::string("\xff", 1));
    // The printer still emits octal escapes for bytes, and those round-trip.
    TT3 br;
    CHECK(rpb::text_format_parse("optional_bytes: \"\\377\\376\"", br));
    CHECK(roundtrip<TT3>(printed(br)));
  }

  // --- duplicates: non-repeated fields are one-shot (parity with
  // TextFormat's "specified multiple times" / oneof conflict errors);
  // repeated and map fields stay repeatable.
  {
    TT3 m;
    CHECK(!rpb::text_format_parse("optional_int32: 1 optional_int32: 2", m));
    CHECK(!rpb::text_format_parse("optional_double: 1 optional_double: 2", m));
    CHECK(!rpb::text_format_parse("oneof_uint32: 1 oneof_uint32: 2", m));
    CHECK(!rpb::text_format_parse("oneof_uint32: 1 oneof_string: \"x\"", m));
    CHECK(!rpb::text_format_parse(
        "optional_nested_message { a: 1 } optional_nested_message { a: 2 }",
        m));

    CHECK(rpb::text_format_parse(
        "repeated_int32: 1 repeated_int32: 2 repeated_int32: [3, 4]", m));
    CHECK(m.repeated_int32.size() == 4);
    CHECK(rpb::text_format_parse(
        "map_string_string { key: \"a\" value: \"b\" }"
        " map_string_string { key: \"c\" value: \"d\" }",
        m));
    CHECK(m.map_string_string.size() == 2);
  }

  // --- float spellings: optional f/F suffix, leading-'.' fallback, and
  // rejection of a leading '+' / hex floats / "+inf".
  {
    TT3 m;
    CHECK(rpb::text_format_parse("optional_double: 1.5f", m));
    CHECK(m.optional_double == 1.5);
    CHECK(rpb::text_format_parse("optional_double: 2F", m));
    CHECK(m.optional_double == 2.0);
    CHECK(rpb::text_format_parse("optional_float: 1f", m));
    CHECK(m.optional_float == 1.0f);
    // strtod fallback path (from_chars rejects ".5"); must not use a
    // dangling pointer into a destroyed temporary std::string.
    CHECK(rpb::text_format_parse("optional_double: .5", m));
    CHECK(m.optional_double == 0.5);
    CHECK(rpb::text_format_parse("optional_double: 5.", m));
    CHECK(m.optional_double == 5.0);
    CHECK(!rpb::text_format_parse("optional_double: +1.5", m));
    CHECK(!rpb::text_format_parse("optional_double: 0x10", m));
    CHECK(!rpb::text_format_parse("optional_double: +inf", m));
    // The 'f' suffix is only a decimal-literal suffix ("nanf"/"inff" are
    // rejected by protobuf's tokenizer).
    CHECK(!rpb::text_format_parse("optional_double: nanf", m));
    CHECK(!rpb::text_format_parse("optional_double: inff", m));
    CHECK(rpb::text_format_parse("optional_double: 1e5f", m));
    CHECK(m.optional_double == 1e5);
    CHECK(rpb::text_format_parse("optional_double: inf", m));
    CHECK(std::isinf(m.optional_double) && m.optional_double > 0);
    CHECK(rpb::text_format_parse("optional_double: -inf", m));
    CHECK(std::isinf(m.optional_double) && m.optional_double < 0);
    CHECK(rpb::text_format_parse("optional_double: nan", m));
    CHECK(std::isnan(m.optional_double));
    CHECK(roundtrip<TT3>("optional_double: 1.5f"));
  }

  // --- list syntax: trailing commas are rejected (empty/single lists too).
  {
    TT3 m;
    CHECK(!rpb::text_format_parse("repeated_int32: [1, 2,]", m));
    CHECK(!rpb::text_format_parse("repeated_string: [\"a\",]", m));
    CHECK(!rpb::text_format_parse("repeated_int32: [,]", m));
    CHECK(rpb::text_format_parse("repeated_int32: [1, 2]", m));
    CHECK(rpb::text_format_parse("repeated_int32: []", m));
    CHECK(rpb::text_format_parse("repeated_int32: 7", m));
  }

  // --- map entries may omit `key`: the entry uses the default key (and an
  // empty entry inserts the all-default key/value pair), like TextFormat.
  {
    TT3 m;
    CHECK(rpb::text_format_parse("map_string_string { value: \"v\" }", m));
    CHECK(m.map_string_string.size() == 1);
    CHECK(m.map_string_string[""] == "v");

    TT3 e;
    CHECK(rpb::text_format_parse("map_int32_int32 { }", e));
    CHECK(e.map_int32_int32.size() == 1);
    CHECK(e.map_int32_int32[0] == 0);
  }

  std::printf("text_selftest: %s (%d failures)\n",
              failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
