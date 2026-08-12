// P3394R4 annotation values via scope splice (docs/reflect_error.md §3):
// the number rides on the annotation TYPE, read back with
// [: type_of(ann) :]::value -- no std::meta::extract.  type_of yields the
// annotation type with cv/ref qualifiers, so remove_cvref_t is needed for
// type matching.  Multi-annotations come back in declaration order.
#include <meta>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <variant>

namespace rpb {
template <std::uint32_t N>
struct field_no {
  static constexpr std::uint32_t value = N;
};
template <typename... Ts>
using OneOf = std::variant<std::monostate, Ts...>;
}

template <typename T> struct is_field_no : std::false_type {};
template <std::uint32_t N> struct is_field_no<rpb::field_no<N>>
    : std::true_type {};
template <typename T> inline constexpr bool is_field_no_v = is_field_no<T>::value;

struct S {
  [[deprecated]] [[=rpb::field_no<7>{}]] int a;
  [[=rpb::field_no<20>{}, =rpb::field_no<21>{}]]
  rpb::OneOf<std::string, std::int64_t> choice;
};

consteval auto ctx() { return std::meta::access_context::unprivileged(); }

template <std::meta::info M>
consteval std::size_t annotation_count() {
  std::size_t n = 0;
  template for (constexpr auto ann :
                std::define_static_array(std::meta::annotations_of(M))) {
    using A = std::remove_cvref_t<typename [: std::meta::type_of(ann) :]>;
    if (is_field_no_v<A>) ++n;
  }
  return n;
}

template <std::meta::info M, std::size_t K>
consteval std::uint32_t field_number() {
  std::size_t seen = 0;
  template for (constexpr auto ann :
                std::define_static_array(std::meta::annotations_of(M))) {
    using A = std::remove_cvref_t<typename [: std::meta::type_of(ann) :]>;
    if constexpr (is_field_no_v<A>) {
      if (seen == K)
        return [: std::meta::type_of(ann) :]::value;
      ++seen;
    }
  }
  return 0;
}

int main() {
  constexpr std::meta::info m0 =
      std::meta::nonstatic_data_members_of(^^S, ctx())[0];
  constexpr std::meta::info m1 =
      std::meta::nonstatic_data_members_of(^^S, ctx())[1];
  static_assert(annotation_count<m0>() == 1);
  static_assert(annotation_count<m1>() == 2);
  static_assert(field_number<m0, 0>() == 7);
  static_assert(field_number<m1, 0>() == 20);
  static_assert(field_number<m1, 1>() == 21);
  std::printf("annotation splice ok\n");
  return 0;
}
