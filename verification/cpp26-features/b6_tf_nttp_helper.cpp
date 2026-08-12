#include <meta>
#include <print>
enum class E : int { a, b, c };
template <std::meta::info r> consteval int helper() {
  auto e = [: r :];
  return static_cast<int>(e);
}
int main() {
  int sum = 0;
  template for (constexpr auto r : std::define_static_array(std::meta::enumerators_of(^^E))) {
    sum += helper<r>();
  }
  std::println("sum={}", sum);
}
