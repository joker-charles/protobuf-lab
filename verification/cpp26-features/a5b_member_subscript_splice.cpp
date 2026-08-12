#include <meta>
#include <print>
struct S { int a; float b; double c; };
int main() {
  constexpr auto ms = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unprivileged()));
  constexpr int I = 2;
  S v{};
  v.[: ms[I] :] = 2.5;
  std::println("{}", v.c);
}
