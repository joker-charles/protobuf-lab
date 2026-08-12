// Demo + self-tests for the reflection-driven protobuf wire codec.

#include "codec.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

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

  bool operator==(Person const &o) const
  {
    return name == o.name && id == o.id && lucky == o.lucky && score == o.score
           && home == o.home && nick == o.nick;
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
  // 4: score = 0.0 (double) 21 00*8
  // 5: home {city:"X",zip:0} 2A 05 0A 01 58 10 00
  static unsigned char const expected[] = {
      0x0A, 0x05, 'A', 'l', 'i', 'c', 'e',
      0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
      0x1A, 0x02, 0x01, 0x02,
      0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x2A, 0x05, 0x0A, 0x01, 'X', 0x10, 0x00};

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
  check(rpb::parse(bytes, q) && q == p, "unknown field skipped");
}

static void test_truncated()
{
  Person p = make_fixture();
  std::string bytes;
  rpb::serialize(bytes, p);
  bytes.resize(bytes.size() / 2);
  Person q;
  check(!rpb::parse(bytes, q), "truncated input rejected");
}

static void run_tests()
{
  test_hand_bytes();
  test_roundtrip();
  test_unknown_field();
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
