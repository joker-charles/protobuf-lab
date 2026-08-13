// Demo + self-tests for the reflection-driven protobuf wire codec.

#include "codec.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class Color : std::int32_t
{
  RED = 0,
  GREEN = 1,
  BLUE = 2,
};

struct Address
{
  [[=rpb::field_no<1>{}]] std::string city;
  [[=rpb::field_no<2>{}]] std::int64_t zip;

  bool operator==(Address const &o) const
  {
    return city == o.city && zip == o.zip;
  }
};

struct Person
{
  rpb::UnknownFields unknown;  // any position now (annotation-driven)
  [[=rpb::field_no<1>{}]] std::string name;
  [[=rpb::field_no<2>{}]] std::int32_t id;
  [[=rpb::field_no<3>{}]] std::vector<std::int64_t> lucky;
  [[=rpb::field_no<4>{}]] double score;
  [[=rpb::field_no<5>{}]] Address home;
  [[=rpb::field_no<6>{}]] std::optional<std::string> nick;
  [[=rpb::field_no<7>{}]] Color color;
  [[=rpb::field_no<8>{}]] std::vector<Color> palette;
  [[=rpb::field_no<9>{}]] std::string blob;
  [[=rpb::field_no<10>{}]] rpb::SInt<std::int32_t> delta;
  [[=rpb::field_no<11>{}]] rpb::SInt<std::int64_t> big;
  [[=rpb::field_no<12>{}]] rpb::Fixed32 fx32;
  [[=rpb::field_no<13>{}]] rpb::SFixed64 sfx64;
  [[=rpb::field_no<14>{}]] std::vector<rpb::SInt<std::int64_t>> samples;
  [[=rpb::field_no<15>{}]] std::vector<rpb::Fixed32> hashes;
  [[=rpb::field_no<16>{}]] std::vector<bool> flags;
  [[=rpb::field_no<17>{}]] std::vector<std::string> tags;
  [[=rpb::field_no<18>{}]] std::map<std::string, std::int32_t> scores;
  [[=rpb::field_no<19>{}]]
  std::vector<rpb::Unpacked<std::int32_t>> unpacked_nums;
  [[=rpb::field_no<20>{}, =rpb::field_no<21>{}]]
  rpb::OneOf<std::string, std::int64_t> choice;

  bool operator==(Person const &o) const
  {
    return unknown == o.unknown && name == o.name && id == o.id
           && lucky == o.lucky && score == o.score
           && home == o.home && nick == o.nick && color == o.color
           && palette == o.palette && blob == o.blob && delta == o.delta
           && big == o.big && fx32 == o.fx32 && sfx64 == o.sfx64
           && samples == o.samples && hashes == o.hashes && flags == o.flags
           && tags == o.tags && scores == o.scores
           && unpacked_nums == o.unpacked_nums && choice == o.choice;
  }
};

static int failures = 0;

static void check(bool ok, char const *label)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", label);
  if (!ok)
    ++failures;
}

// Byte-exact comparison: string (char) vs unsigned-char expected bytes.
// Comparing the two value types directly would promote 0xFF to -1 vs 255,
// so both sides are projected to unsigned char first.
template <std::size_t N>
bool bytes_match(std::string const &s, unsigned char const (&expected)[N])
{
  return std::ranges::equal(
      s, expected, std::ranges::equal_to{},
      [](char c) { return static_cast<unsigned char>(c); },
      [](unsigned char c) { return c; });
}

static void test_hand_bytes()
{
  // Expected wire bytes computed by hand (protobuf spec):
  // 1: name "Alice"         0A 05 41 6C 69 63 65
  // 2: id = -1 (int32)      10 FF*9 01
  // 3: lucky packed [1,2]   1A 02 01 02
  // 4: score = 0.0 (default) omitted
  // 5: home {city:"X",zip:0} 2A 03 0A 01 58 (zip=0 default omitted inside)
  // 6-17: all defaults / empty -> omitted (proto3 semantics)
  static unsigned char const expected[] = {
      0x0A, 0x05, 'A', 'l', 'i', 'c', 'e',
      0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
      0x1A, 0x02, 0x01, 0x02,
      0x2A, 0x03, 0x0A, 0x01, 'X'};

  Person p;
  p.name = "Alice";
  p.id = -1;
  p.lucky = {1, 2};
  p.score = 0.0;
  p.home.city = "X";
  p.home.zip = 0;

  std::string bytes;
  rpb::serialize(bytes, p);
  check(bytes_match(bytes, expected),
        "hand-computed wire bytes");

  Person q;
  check(rpb::parse(bytes, q) && q == p, "parse hand bytes");
}

struct WireBits
{
  [[=rpb::field_no<1>{}]] rpb::SInt<std::int32_t> a;             // 1: sint32
  [[=rpb::field_no<2>{}]] rpb::Fixed32 b;                        // 2: fixed32
  [[=rpb::field_no<3>{}]] rpb::SFixed64 c;                       // 3: sfixed64
  [[=rpb::field_no<4>{}]] std::vector<rpb::SInt<std::int64_t>> d;  // 4: packed
  [[=rpb::field_no<5>{}]] Color e;                               // 5: enum
  [[=rpb::field_no<6>{}]] std::vector<Color> f;                  // 6: packed

  bool operator==(WireBits const &) const = default;
};

static void test_hand_wire_types()
{
  // Expected wire bytes computed by hand (protobuf spec):
  // 1: a=-5  zigzag32 -> 9          08 09
  // 2: b=0xDEADBEEF fixed32 LE      15 EF BE AD DE
  // 3: c=-2  sfixed64 LE            19 FE FF FF FF FF FF FF FF
  // 4: d=[-1,2] zigzag64 -> [1,4]   22 02 01 04
  // 5: e=GREEN(1)                   28 01
  // 6: f=[GREEN,BLUE]=[1,2] packed  32 02 01 02
  static unsigned char const expected[] = {
      0x08, 0x09,
      0x15, 0xEF, 0xBE, 0xAD, 0xDE,
      0x19, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x22, 0x02, 0x01, 0x04,
      0x28, 0x01,
      0x32, 0x02, 0x01, 0x02};

  WireBits w;
  w.a = {-5};
  w.b = {0xDEADBEEFu};
  w.c = {-2};
  w.d = {{-1}, {2}};
  w.e = Color::GREEN;
  w.f = {Color::GREEN, Color::BLUE};

  std::string bytes;
  rpb::serialize(bytes, w);
  check(bytes_match(bytes, expected),
        "hand-computed wire-type bytes");

  WireBits r;
  check(rpb::parse(bytes, r) && r == w, "parse hand wire-type bytes");
}

struct MapBits
{
  [[=rpb::field_no<1>{}]] std::map<std::string, std::int32_t> m;
  [[=rpb::field_no<2>{}]] std::vector<rpb::Unpacked<std::int32_t>> u;

  bool operator==(MapBits const &) const = default;
};

struct OptNested
{
  [[=rpb::field_no<1>{}]] std::optional<Address> addr;  // message
  [[=rpb::field_no<2>{}]] std::optional<std::string> tag;  // scalar

  bool operator==(OptNested const &) const = default;
};

static void test_hand_map_unpacked()
{
  // 1: map entries (std::map serializes in sorted key order: "a", "b")
  //    entry a: key "a" 0A 01 61 ; value 3 10 03   -> 0A 05 0A 01 61 10 03
  //    entry b: key "b" 0A 01 62 ; value 7 10 07   -> 0A 05 0A 01 62 10 07
  // 2: unpacked [5, -1] (one tag per element)
  //    10 05 ; 10 FF FF FF FF FF FF FF FF FF 01
  static unsigned char const expected[] = {
      0x0A, 0x05, 0x0A, 0x01, 'a', 0x10, 0x03,
      0x0A, 0x05, 0x0A, 0x01, 'b', 0x10, 0x07,
      0x10, 0x05,
      0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};

  MapBits w;
  w.m = {{"b", 7}, {"a", 3}};
  w.u = {rpb::Unpacked<std::int32_t>{5}, rpb::Unpacked<std::int32_t>{-1}};

  std::string bytes;
  rpb::serialize(bytes, w);
  check(bytes_match(bytes, expected),
        "hand-computed map/unpacked bytes");

  MapBits r;
  check(rpb::parse(bytes, r) && r == w, "parse hand map/unpacked bytes");
}

static Person make_fixture()
{
  Person p;
  p.name = "Alice";
  p.id = -1;
  p.lucky = {-3, 7};
  p.score = 1.5;
  p.home.city = "X";
  p.home.zip = -2;
  p.nick = "N";
  p.color = Color::GREEN;
  p.palette = {Color::GREEN, Color::BLUE};
  p.blob = std::string("\x01\x02\x03", 3);
  p.delta = rpb::SInt<std::int32_t>{-5};
  p.big = rpb::SInt<std::int64_t>{-1234567890123LL};
  p.fx32 = rpb::Fixed32{0xDEADBEEFu};
  p.sfx64 = rpb::SFixed64{-42};
  p.samples = {rpb::SInt<std::int64_t>{-7}, rpb::SInt<std::int64_t>{1000000}};
  p.hashes = {rpb::Fixed32{0xCAFEBABEu}, rpb::Fixed32{0x12345678u}};
  p.flags = {true, false, true};
  p.tags = {"a", "b"};
  // Single map entry: protobuf map order is unspecified, so multi-entry
  // byte-level interop is not guaranteed (covered by selftest instead).
  p.scores = {{"alice", 3}};
  p.unpacked_nums = {rpb::Unpacked<std::int32_t>{1},
                     rpb::Unpacked<std::int32_t>{2}};
  p.choice = std::int64_t{42};  // oneof: count = 42
  return p;
}

struct OneOfBits
{
  [[=rpb::field_no<20>{}, =rpb::field_no<21>{}]]
  rpb::OneOf<std::string, std::int64_t> choice;

  bool operator==(OneOfBits const &) const = default;
};

static void test_oneof_hand_bytes()
{
  // 20: note (string), 21: count (int64)
  //   note="hi"      A2 01 02 68 69
  //   count=42       A8 01 2A
  //   unset          (nothing emitted)
  //   note=""        A2 01 00  (presence: set, even default, still emits)
  static unsigned char const note_expected[] = {0xA2, 0x01, 0x02, 'h', 'i'};
  static unsigned char const count_expected[] = {0xA8, 0x01, 0x2A};
  static unsigned char const empty_expected[] = {0xA2, 0x01, 0x00};

  OneOfBits w;
  std::string bytes;
  rpb::serialize(bytes, w);
  check(bytes.empty(), "oneof unset emits nothing");

  w.choice = std::string("hi");
  bytes.clear();
  rpb::serialize(bytes, w);
  check(bytes_match(bytes, note_expected),
        "oneof string alternative hand bytes");
  OneOfBits r;
  check(rpb::parse(bytes, r) && r == w, "oneof string alternative parse");

  w.choice = std::int64_t{42};
  bytes.clear();
  rpb::serialize(bytes, w);
  check(bytes_match(bytes, count_expected),
        "oneof int64 alternative hand bytes");
  check(rpb::parse(bytes, r) && r == w, "oneof int64 alternative parse");

  w.choice = std::string("");
  bytes.clear();
  rpb::serialize(bytes, w);
  check(bytes_match(bytes, empty_expected),
        "oneof empty string still emitted");
  check(rpb::parse(bytes, r) && r == w,
        "oneof empty string roundtrip");

  // Last-wins: both alternatives on the wire -> the later one wins.
  static unsigned char const both_expected[] = {
      0xA2, 0x01, 0x02, 'h', 'i', 0xA8, 0x01, 0x2A};
  OneOfBits lw;
  check(rpb::parse(
            std::string(reinterpret_cast<char const *>(both_expected),
                        sizeof both_expected),
            lw)
            && lw.choice.index() == 2 && std::get<2>(lw.choice) == 42,
        "oneof last-wins");
}

static void test_optional_message_merge()
{
  // Repeated occurrences of an optional MESSAGE field merge (protobuf
  // semantics), while optional scalar/string fields stay last-wins.
  // Each side sets a different Address member so only merging keeps both.
  OptNested first;
  first.addr = Address{"X", 0};
  first.tag = "first";
  OptNested second;
  second.addr = Address{"", 42};
  second.tag = "second";

  std::string bytes;
  rpb::serialize(bytes, first);
  rpb::serialize(bytes, second);  // append: field 1 appears twice

  OptNested q;
  check(rpb::parse(bytes, q), "optional message merge parse");
  check(q.addr.has_value() && q.addr->city == "X" && q.addr->zip == 42,
        "optional message members merged across occurrences");
  check(q.tag.has_value() && *q.tag == "second",
        "optional scalar/string stays last-wins");

  // Roundtrip of the merged value stays stable.
  std::string re;
  rpb::serialize(re, q);
  OptNested q2;
  check(rpb::parse(re, q2) && q2 == q, "merged optional roundtrip");
}

struct OutOfOrder
{
  [[=rpb::field_no<2>{}]] std::int32_t b;  // declared second, field 2
  [[=rpb::field_no<1>{}]] std::int32_t a;  // declared first, field 1

  bool operator==(OutOfOrder const &) const = default;
};

static void test_out_of_order()
{
  // Members declared 2 then 1; the compile-time table must sort them to
  // 1 then 2 on the wire (protoc always emits ascending field numbers).
  OutOfOrder w;
  w.a = 42;
  w.b = 7;
  std::string bytes;
  rpb::serialize(bytes, w);
  static unsigned char const expected[] = {0x08, 0x2A, 0x10, 0x07};
  check(bytes_match(bytes, expected),
        "out-of-order declaration serialized ascending");
  OutOfOrder r;
  check(rpb::parse(bytes, r) && r == w, "out-of-order roundtrip");
}

struct RecursiveChoice
{
  [[=rpb::field_no<1>{}, =rpb::field_no<2>{}]]
  rpb::OneOf<std::int32_t, std::unique_ptr<RecursiveChoice>> next;

  bool operator==(RecursiveChoice const &o) const
  {
    return rpb::deep_equal(*this, o);
  }
};

static void test_recursive_oneof_deep_equal()
{
  // Regression: deep_equal must compare oneof alternatives by value, not
  // via variant::operator== (which would compare unique_ptr by pointer).
  RecursiveChoice a;
  a.next = std::make_unique<RecursiveChoice>();
  std::get<2>(a.next)->next = std::int32_t{42};
  RecursiveChoice b;  // same structure, different pointer identity
  b.next = std::make_unique<RecursiveChoice>();
  std::get<2>(b.next)->next = std::int32_t{42};
  check(a == b && rpb::deep_equal(a, b),
        "oneof unique_ptr alternative deep equality");

  RecursiveChoice c;
  c.next = std::make_unique<RecursiveChoice>();
  std::get<2>(c.next)->next = std::int32_t{43};
  check(!(a == c), "oneof unique_ptr alternative inequality");

  std::string bytes;
  rpb::serialize(bytes, a);
  RecursiveChoice q;
  check(rpb::parse(bytes, q) && q == a, "recursive oneof roundtrip");
}

static void test_depth_limit_roundtrip()
{
  // Root + 63 nested unique_ptr levels = 64 serialize()/parse() frames,
  // exactly at kMaxSerializeDepth (parser SetRecursionLimit(64)); proves
  // the depth guard does not misfire on legal nesting.
  RecursiveChoice deep;
  auto *cur = &deep;
  for (int i = 0; i < 63; ++i)
    {
      cur->next = std::make_unique<RecursiveChoice>();
      cur = std::get<2>(cur->next).get();
    }
  cur->next = std::int32_t{42};

  std::string bytes;
  rpb::serialize(bytes, deep);
  RecursiveChoice q;
  check(rpb::parse(bytes, q) && q == deep,
        "depth-64 nested message roundtrip");

  // 65 nested messages on the wire (built by hand; our own serializer
  // would refuse to produce them) must be rejected gracefully.
  std::string deep65;
  deep65.push_back(static_cast<char>(0x08));  // innermost: field 1 varint
  deep65.push_back(static_cast<char>(0x2A));  // value 42
  for (int i = 1; i < 65; ++i)
    {
      std::string wrapped;
      wrapped.push_back(static_cast<char>(0x12));  // field 2, length-delimited
      std::size_t len = deep65.size();
      do
        {
          unsigned char b = static_cast<unsigned char>(len & 0x7F);
          len >>= 7;
          if (len)
            b |= 0x80;
          wrapped.push_back(static_cast<char>(b));
        }
      while (len);
      wrapped += deep65;
      deep65 = std::move(wrapped);
    }
  RecursiveChoice r;
  check(!rpb::parse(deep65, r), "depth-65 nested message rejected");
}

static void test_deep_copy()
{
  RecursiveChoice a;
  a.next = std::make_unique<RecursiveChoice>();
  std::get<2>(a.next)->next = std::int32_t{42};

  RecursiveChoice b = rpb::deep_copy(a);
  check(b == a, "deep_copy equal");
  check(std::get<2>(b.next).get() != std::get<2>(a.next).get(),
        "deep_copy allocates independent storage");
  std::get<2>(b.next)->next = std::int32_t{43};
  check(!(b == a), "deep_copy does not share state");
}

static void test_roundtrip()
{
  Person p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  Person q;
  check(rpb::parse(bytes, q) && q == p, "roundtrip");
}

static void test_unknown_field()
{
  Person p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  // Append an unknown field: field 99, varint, value 42.
  // tag = (99 << 3) | 0 = 792 -> varint 98 06 ; value 42 -> 2A
  bytes.push_back(static_cast<char>(0x98));
  bytes.push_back(static_cast<char>(0x06));
  bytes.push_back(static_cast<char>(0x2A));
  // Append an unknown length-delimited field: field 98, len 3, "abc".
  // tag = (98 << 3) | 2 = 786 -> varint 92 06 ; len 03 ; "abc".
  bytes.push_back(static_cast<char>(0x92));
  bytes.push_back(static_cast<char>(0x06));
  bytes.push_back(static_cast<char>(0x03));
  bytes.push_back('a');
  bytes.push_back('b');
  bytes.push_back('c');
  Person q;
  check(rpb::parse(bytes, q), "unknown field parsed");
  check(q.unknown.size() == 2 && q.unknown[0].fieldno == 99
            && q.unknown[0].wire_type == 0
            && q.unknown[0].raw == std::string("\x2A", 1)
            && q.unknown[1].fieldno == 98 && q.unknown[1].wire_type == 2
            && q.unknown[1].raw == std::string("\x03" "abc", 4),
        "unknown fields captured (varint + length-delimited)");
  std::string re;
  rpb::serialize(re, q);
  check(re == bytes, "unknown field re-serialized");
}

static void test_group_skip()
{
  Person p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  // Append an unknown group: field 20 start (tag A3 01), inner field 1
  // varint 7 (08 07), end group (tag A4 01).
  bytes.push_back(static_cast<char>(0xA3));
  bytes.push_back(static_cast<char>(0x01));
  bytes.push_back(static_cast<char>(0x08));
  bytes.push_back(static_cast<char>(0x07));
  bytes.push_back(static_cast<char>(0xA4));
  bytes.push_back(static_cast<char>(0x01));
  Person q;
  check(rpb::parse(bytes, q), "group field skipped");
  check(q == p, "group skip preserves fields");
}

static void test_truncated()
{
  Person p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  // Cut the final byte of the last field; a half-length cut can land exactly
  // on a field boundary and parse cleanly (clean EOF), which would make this
  // test pass vacuously.
  bytes.resize(bytes.size() - 1);
  Person q;
  check(!rpb::parse(bytes, q), "truncated input rejected");
}

static void run_tests()
{
  test_hand_bytes();
  test_hand_wire_types();
  test_hand_map_unpacked();
  test_oneof_hand_bytes();
  test_optional_message_merge();
  test_out_of_order();
  test_recursive_oneof_deep_equal();
  test_depth_limit_roundtrip();
  test_deep_copy();
  test_roundtrip();
  test_unknown_field();
  test_group_skip();
  test_truncated();
}

static void emit_fixture()
{
  Person p = make_fixture();
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

  Person p;
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
      run_tests();
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

  Person p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  std::printf("serialized %zu bytes\n", bytes.size());
  for (unsigned char c : bytes)
    std::printf("%02X ", c);
  std::printf("\n");
  return 0;
}
