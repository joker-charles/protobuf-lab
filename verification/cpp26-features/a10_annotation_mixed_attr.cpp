// Traditional attributes and value annotations cannot share one [[...]]
// list (docs/reflect_error.md §3): "mixing annotations and attributes in
// the same list".  Must FAIL to compile.
#include <cstdint>

namespace rpb {
template <std::uint32_t N>
struct field_no {
  static constexpr std::uint32_t value = N;
};
}

struct S {
  [[nodiscard, =rpb::field_no<7>{}]] int a;  // rejected
};

int main() {}
