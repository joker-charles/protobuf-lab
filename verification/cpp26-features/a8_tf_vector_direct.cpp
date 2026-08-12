#include <meta>
enum class E : int { a, b, c };
int main() {
  int sum = 0;
  template for (constexpr auto r : std::meta::enumerators_of(^^E)) {
    auto e = [: r :];
    sum += static_cast<int>(e);
  }
  (void)sum;
}
