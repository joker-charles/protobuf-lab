#include <meta>
#include <print>
enum class E : int { a = 1 };
struct S { int x; };
constexpr int f(int a, float b) { return a; }
int main() {
  constexpr auto ctx = std::meta::access_context::unprivileged();
  constexpr auto es = std::define_static_array(std::meta::enumerators_of(^^E));
  constexpr auto ms = std::define_static_array(std::meta::nonstatic_data_members_of(^^S, ctx));
  constexpr auto mems = std::define_static_array(std::meta::members_of(^^S));
  constexpr auto ps = std::define_static_array(std::meta::parameters_of(^^f));
  constexpr auto nm = std::meta::identifier_of(^^E);
  constexpr auto unm = std::meta::u8identifier_of(^^E);
  constexpr auto dn = std::meta::display_string_of(^^E);
  static_assert(std::meta::is_enum(^^E));
  static_assert(std::meta::is_class(^^S));
  static_assert(es.size() == 1 && ms.size() == 1 && ps.size() == 2 && mems.size() >= 1);
  std::println("{} {} {} {}", nm, (int)unm.size(), dn, (int)es.size());
}
