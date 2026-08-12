#include <meta>
#include <print>
struct P { int x; int y; };
constexpr P p{1, 2};
int main() {
  constexpr auto o = std::define_static_object(p);
  std::println("runtime {}", o->y);
}
