#include <meta>
struct S { int a; float b; };
consteval int sum() {
  int total = 0;
  std::meta::for_each(std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unprivileged()),
                      [&](std::meta::info) { ++total; });
  return total;
}
static_assert(sum() == 2);
int main() {}
