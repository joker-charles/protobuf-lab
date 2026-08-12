// Demo + self-tests for the reflection-driven protobuf wire codec.

#include "codec.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
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
  std::string city;
  std::int64_t zip;

  bool operator==(Address const &o) const
  {
    return city == o.city && zip == o.zip;
  }
};

struct Person
{
  std::string name;
  std::int32_t id;
  std::vector<std::int64_t> lucky;
  double score;
  Address home;
  std::optional<std::string> nick;
  Color color;
  std::vector<Color> palette;
  std::string blob;
  rpb::SInt<std::int32_t> delta;
  rpb::SInt<std::int64_t> big;
  rpb::Fixed32 fx32;
  rpb::SFixed64 sfx64;
  std::vector<rpb::SInt<std::int64_t>> samples;
  std::vector<rpb::Fixed32> hashes;
  std::vector<bool> flags;
  std::vector<std::string> tags;
  rpb::UnknownFields unknown;  // must stay last: captures unknown fields

  bool operator==(Person const &o) const
  {
    return name == o.name && id == o.id && lucky == o.lucky && score == o.score
           && home == o.home && nick == o.nick && color == o.color
           && palette == o.palette && blob == o.blob && delta == o.delta
           && big == o.big && fx32 == o.fx32 && sfx64 == o.sfx64
           && samples == o.samples && hashes == o.hashes && flags == o.flags
           && tags == o.tags && unknown == o.unknown;
  }
};

static int failures = 0;

static void check(bool ok, char const *label)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", label);
  if (!ok)
    ++failures;
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
  check(bytes.size() == sizeof expected
            && std::memcmp(bytes.data(), expected, sizeof expected) == 0,
        "hand-computed wire bytes");

  Person q;
  check(rpb::parse(bytes, q) && q == p, "parse hand bytes");
}

struct WireBits
{
  rpb::SInt<std::int32_t> a;                // 1: sint32
  rpb::Fixed32 b;                           // 2: fixed32
  rpb::SFixed64 c;                          // 3: sfixed64
  std::vector<rpb::SInt<std::int64_t>> d;   // 4: packed sint64
  Color e;                                  // 5: enum (varint)
  std::vector<Color> f;                     // 6: packed enum

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
  check(bytes.size() == sizeof expected
            && std::memcmp(bytes.data(), expected, sizeof expected) == 0,
        "hand-computed wire-type bytes");

  WireBits r;
  check(rpb::parse(bytes, r) && r == w, "parse hand wire-type bytes");
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
  return p;
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
  Person q;
  check(rpb::parse(bytes, q), "unknown field parsed");
  check(q.unknown.size() == 1 && q.unknown[0].fieldno == 99
            && q.unknown[0].wire_type == 0
            && q.unknown[0].raw == std::string("\x2A", 1),
        "unknown field captured");
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
  std::FILE *f = std::fopen(path, "rb");
  if (!f)
    {
      std::fprintf(stderr, "cannot open %s\n", path);
      std::exit(2);
    }
  std::string data;
  char buf[4096];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
    data.append(buf, n);
  std::fclose(f);

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
