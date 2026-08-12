#include <meta>
#include <print>
enum class E : int { a, b, c };
constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
int main() {
  int sum = 0;
  template for (constexpr auto r : es) {
    auto e = [: r :];
    sum += static_cast<int>(e);
  }
  std::println("sum={}", sum);
}
