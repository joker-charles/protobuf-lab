// Consumer smoke test: the annotated struct is the schema, the `rpb`
// interface target carries include paths, C++26, -freflection and the
// protobuf dependency.
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
      std::fprintf(stderr, "consumer roundtrip failed\n");
      return 1;
    }
  std::printf("consumer roundtrip ok: %zu bytes\n", bytes.size());
  return 0;
}
