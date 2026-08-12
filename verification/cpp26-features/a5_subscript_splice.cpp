#include <meta>
enum class E : int { a, b, c };
int main() {
  constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
  constexpr int I = 1;
  int x = static_cast<int>([: es[I] :]);
  return x == 1 ? 0 : 1;
}
