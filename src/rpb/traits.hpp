// Type traits and protobuf wire-type wrappers.
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include "rpb/annotations.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rpb {

// --- oneof -------------------------------------------------------------
// OneOf<Ts...> = std::variant<std::monostate, Ts...>; monostate means the
// oneof is unset.  Each alternative is a distinct protobuf field with its
// own field_no annotation.

template <typename... Ts>
using OneOf = std::variant<std::monostate, Ts...>;

template <typename T> struct is_one_of : std::false_type {};
template <typename... Ts> struct is_one_of<OneOf<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_one_of_v = is_one_of<T>::value;

// --- type traits ---

template <typename T> struct is_vector : std::false_type {};
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type {};
template <typename T> inline constexpr bool is_vector_v = is_vector<T>::value;

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type {};
template <typename T> inline constexpr bool is_optional_v =
    is_optional<T>::value;

template <typename T> struct is_unique_ptr : std::false_type {};
template <typename T, typename D>
struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};
template <typename T> inline constexpr bool is_unique_ptr_v =
    is_unique_ptr<T>::value;

template <typename T> struct is_map : std::false_type {};
template <typename K, typename V, typename C, typename A>
struct is_map<std::map<K, V, C, A>> : std::true_type {};
template <typename T> inline constexpr bool is_map_v = is_map<T>::value;

template <typename T>
inline constexpr bool is_numeric_v =
    std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>;

// std::underlying_type_t is ill-formed for non-enums, so resolve it via a
// partial specialization instead of std::conditional_t.
template <typename T, bool = std::is_enum_v<T>> struct underlying_or_self
{
  using type = T;
};
template <typename T> struct underlying_or_self<T, true>
{
  using type = std::underlying_type_t<T>;
};
template <typename T> using underlying_or_self_t =
    typename underlying_or_self<T>::type;

// Value counterpart of underlying_or_self_t: enums -> their underlying
// value (std::to_underlying), everything else unchanged.
template <typename T>
constexpr auto underlying_value(T v)
{
  if constexpr (std::is_enum_v<T>)
    return std::to_underlying(v);
  else
    return v;
}

// --- explicit wire-type wrappers (protobuf wire types not expressible by a
// plain C++ type: sint zigzag and fixed-width integers) ---

template <typename T> struct SInt
{
  using value_type = T;
  T value{};
  bool operator==(SInt const &) const = default;
  auto operator<=>(SInt const &) const = default;  // usable as map key
};
struct Fixed32
{
  std::uint32_t value{};
  bool operator==(Fixed32 const &) const = default;
  auto operator<=>(Fixed32 const &) const = default;
};
struct SFixed32
{
  std::int32_t value{};
  bool operator==(SFixed32 const &) const = default;
  auto operator<=>(SFixed32 const &) const = default;
};
struct Fixed64
{
  std::uint64_t value{};
  bool operator==(Fixed64 const &) const = default;
  auto operator<=>(Fixed64 const &) const = default;
};
struct SFixed64
{
  std::int64_t value{};
  bool operator==(SFixed64 const &) const = default;
  auto operator<=>(SFixed64 const &) const = default;
};
// Forces a packable element type to be encoded unpacked (one tag per
// element), like `[packed=false]` in proto.
template <typename T> struct Unpacked
{
  T value{};
  bool operator==(Unpacked const &) const = default;
};
// Explicit bytes field (wire type 2), a distinct type from std::string so
// a std::variant can hold both string and bytes alternatives (e.g. oneof
// with oneof_string + oneof_bytes).
struct Bytes
{
  std::string value{};
  bool operator==(Bytes const &) const = default;
};

template <typename T> struct is_sint_wrapper : std::false_type {};
template <typename T> struct is_sint_wrapper<SInt<T>> : std::true_type {};
template <typename T> inline constexpr bool is_sint_wrapper_v =
    is_sint_wrapper<T>::value;

template <typename T>
inline constexpr bool is_fixed_wrapper_v =
    std::is_same_v<T, Fixed32> || std::is_same_v<T, SFixed32>
    || std::is_same_v<T, Fixed64> || std::is_same_v<T, SFixed64>;

template <typename T> struct is_unpacked_wrapper : std::false_type {};
template <typename T> struct is_unpacked_wrapper<Unpacked<T>>
    : std::true_type {};
template <typename T> inline constexpr bool is_unpacked_wrapper_v =
    is_unpacked_wrapper<T>::value;

template <typename T> struct is_bytes_wrapper : std::false_type {};
template <> struct is_bytes_wrapper<Bytes> : std::true_type {};
template <typename T> inline constexpr bool is_bytes_wrapper_v =
    is_bytes_wrapper<T>::value;

template <typename T>
inline constexpr bool is_wire_wrapper_v =
    is_sint_wrapper_v<T> || is_fixed_wrapper_v<T>;

// Vectors of anything protobuf can pack: integral (incl. bool), enum,
// floating point, and the wire wrappers above.
template <typename T>
inline constexpr bool is_packable_v = is_numeric_v<T> || is_wire_wrapper_v<T>;

// Wire type a field of C++ type T must use.  Used to reject illegal wire
// types on known fields (protobuf treats a mismatching wire type as an
// unknown field in a map entry, and as a parse error elsewhere).
template <typename T>
consteval int expected_wire_type_of()
{
  using U = std::remove_cvref_t<T>;
  if constexpr (is_unpacked_wrapper_v<U>)
    return expected_wire_type_of<decltype(U::value)>();
  else if constexpr (is_optional_v<U>)
    return expected_wire_type_of<typename U::value_type>();
  else if constexpr (is_unique_ptr_v<U>)
    return 2;  // singular message: length-delimited
  else if constexpr (is_sint_wrapper_v<U> || std::is_integral_v<U>
                     || std::is_enum_v<U>)
    return 0;
  else if constexpr (is_fixed_wrapper_v<U>)
    return sizeof(decltype(U::value)) == 4 ? 5 : 1;
  else if constexpr (std::is_floating_point_v<U>)
    return sizeof(U) == 4 ? 5 : 1;
  else
    return 2;  // string / bytes / message / map entry
}
template <typename T>
inline constexpr int expected_wire_type_v = expected_wire_type_of<T>();

// --- unknown-field preservation -----------------------------------------
// A struct may carry a member of type UnknownFields; fields the codec does
// not recognize are captured there and re-emitted verbatim after the known
// fields on serialization (mirroring protobuf's UnknownFieldSet). Without
// the member, unknown fields are skipped as before. Field numbers are
// annotation-driven, so this member may sit at any position -- it just
// must carry no field_no annotation of its own.

struct UnknownField
{
  std::uint32_t fieldno;
  std::uint32_t wire_type;
  std::string raw;  // payload bytes only (tag is reconstructed)
  bool operator==(UnknownField const &) const = default;
};
using UnknownFields = std::vector<UnknownField>;

template <typename T, std::size_t I>
inline constexpr bool is_unknown_member_v =
    std::is_same_v<typename [: meta::type_of(member_v<T, I>) :], UnknownFields>;

template <typename T, std::size_t... Is>
constexpr std::size_t find_unknown_index(std::index_sequence<Is...>)
{
  std::size_t idx = member_count_v<T>;
  ((idx = is_unknown_member_v<T, Is> ? Is : idx), ...);
  return idx;
}

template <typename T>
inline constexpr std::size_t unknown_member_index_v =
    find_unknown_index<T>(std::make_index_sequence<member_count_v<T>>{});

template <typename T>
inline constexpr bool has_unknown_fields_v =
    unknown_member_index_v<T> != member_count_v<T>;

}  // namespace rpb
