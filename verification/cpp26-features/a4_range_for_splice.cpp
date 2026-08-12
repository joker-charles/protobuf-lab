#include <meta>
enum class E : int { a, b, c };
int main() {
  constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
  int sum = 0;
  for (auto r : es) {
    auto e = [: r :];
    sum += static_cast<int>(e);
  }
  (void)sum;
}
