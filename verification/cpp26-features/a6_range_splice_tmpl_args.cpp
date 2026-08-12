#include <meta>
#include <vector>
template <typename... Ts> struct Pack {};
consteval auto type_list() {
  return std::define_static_array(std::vector<std::meta::info>{^^int, ^^float, ^^double});
}
using P = Pack<[: type_list() :]>;
static_assert(sizeof(P) > 0);
int main() {}
