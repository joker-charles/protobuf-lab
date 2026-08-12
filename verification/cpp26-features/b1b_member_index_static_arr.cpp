#include <meta>
#include <print>
struct S { int a; float b; };
int main() {
  constexpr auto ms = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unprivileged()));
  constexpr std::meta::info r = ms[0];
  S v{};
  v.[:r:] = 7;
  std::println("{}", v.a);
}
