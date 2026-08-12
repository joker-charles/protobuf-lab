#include <meta>
#include <print>
struct S { int a; float b; };
int main() {
  constexpr std::meta::info r =
      std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unprivileged())[1];
  S v{};
  v.[:r:] = 1.5f;
  std::println("{}", v.b);
}
