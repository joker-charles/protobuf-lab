#include <meta>
enum class E : int { x = 7 };
int main() {
  constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
  constexpr auto r = es[0];
  auto e = [: r :];
  int x = static_cast<int>(e);
  return x == 7 ? 0 : 1;
}
