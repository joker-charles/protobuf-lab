#include <concepts>
#include <meta>
struct S { int a; float b; };
int main() {
  constexpr std::meta::info r =
      std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unprivileged())[0];
  using M = typename [: std::meta::type_of(r) :];
  static_assert(std::same_as<M, int>);
}
