#include <meta>
#include <print>
struct P { int x; int y; };
int main() {
  constexpr auto o = std::define_static_object(P{1, 2});
  static_assert(o->y == 2);
  std::println("runtime {}", o->y);
}
