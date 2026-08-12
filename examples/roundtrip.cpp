// Minimal usage of the reflection codec: an annotated struct *is* the
// schema -- no .proto file, no code generation, no runtime descriptor.
//
// Build (within this repo): cmake -S . -B build ... && cmake --build build
// As a consumer: FetchContent this repo and link the `rpb` interface
// target (see README "Usage").
#include "codec.hpp"

#include <cstdio>
#include <string>

struct Greeting
{
  [[=rpb::field_no<1>{}]] std::string name;
  [[=rpb::field_no<2>{}]] std::int32_t times;

  bool operator==(Greeting const &) const = default;
};

int main()
{
  Greeting g;
  g.name = "world";
  g.times = 3;

  std::string bytes;
  rpb::serialize(bytes, g);

  Greeting back;
  if (!rpb::parse(bytes, back) || !(back == g))
    {
      std::fprintf(stderr, "roundtrip failed\n");
      return 1;
    }
  std::printf("roundtrip ok: %zu bytes\n", bytes.size());
  return 0;
}
