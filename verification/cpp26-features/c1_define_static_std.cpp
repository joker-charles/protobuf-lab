#include <meta>
#include <print>
enum class E : int { a, b };
struct P { int x; int y; };
int main() {
  constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
  constexpr std::size_t n = es.size();
  constexpr auto s = std::define_static_string("hello");
  constexpr auto o = std::define_static_object(P{1, 2});
  std::println("{} {} {}", n, s, o->y);
}
