#include <meta>
struct S { int a; };
int main() {
  constexpr auto rs = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unprivileged()));
  constexpr auto r = rs[0];
  S v{42};
  int x = (int)v.[:r:];
  (void)x;
}
