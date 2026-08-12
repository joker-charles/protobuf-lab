#include <meta>
enum class E : int { a, b };
constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
static_assert(es.size() == 2);
int main() {
  return 0;
}
