#include <meta>
enum class E : int { a, b, c };
consteval int helper(std::meta::info r) {
  auto e = [: r :];
  return static_cast<int>(e);
}
int main() {
  int sum = 0;
  template for (constexpr auto r : std::define_static_array(std::meta::enumerators_of(^^E))) {
    sum += helper(r);
  }
  (void)sum;
}
