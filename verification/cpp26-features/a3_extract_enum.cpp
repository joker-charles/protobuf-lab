#include <meta>
enum class E : int { x = 42 };
int main() {
  constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
  constexpr auto e = es[0];
  int v = std::meta::extract<int>(e);
  (void)v;
}
