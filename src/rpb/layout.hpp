// Compile-time field table and layout validation.
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include "rpb/annotations.hpp"
#include "rpb/traits.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace rpb {

// --- compile-time field table ------------------------------------------
// One FieldEntry per wire field: ordinary members contribute one entry
// (alt == 0), OneOf members one entry per alternative (alt == the
// std::variant index 1..N).  The table is sorted ascending by field number
// (std::sort, consteval) so serialization always matches protoc's
// field-number order regardless of declaration order.

struct FieldEntry
{
  std::uint32_t fieldno;
  std::size_t member;  // index into nonstatic_data_members_of
  std::size_t alt;     // 0 = ordinary member, else variant index
};

template <typename T, std::size_t... Is>
consteval std::size_t field_table_size_impl(std::index_sequence<Is...>)
{
  // Explicit identity so a zero-member message (legal protobuf) compiles.
  return (std::size_t{0} + ... + annotation_count<member_v<T, Is>>());
}

template <typename T>
inline constexpr std::size_t field_table_size_v =
    field_table_size_impl<T>(std::make_index_sequence<member_count_v<T>>{});

template <typename T, std::size_t I, std::size_t... Ks>
consteval void append_oneof_entries(
    std::array<FieldEntry, field_table_size_v<T>> &table, std::size_t &n,
    std::index_sequence<Ks...>)
{
  constexpr meta::info r = member_v<T, I>;
  ((table[n++] = FieldEntry{field_number<r, Ks>(), I, Ks + 1}), ...);
}

template <typename T, std::size_t I>
consteval void append_member_entry(
    std::array<FieldEntry, field_table_size_v<T>> &table, std::size_t &n)
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (is_one_of_v<M>)
    append_oneof_entries<T, I>(
        table, n, std::make_index_sequence<std::variant_size_v<M> - 1>{});
  else
    table[n++] = FieldEntry{field_number<r, 0>(), I, 0};
}

template <typename T, std::size_t I>
consteval void append_known_member_entry(
    std::array<FieldEntry, field_table_size_v<T>> &table, std::size_t &n)
{
  if constexpr (annotation_count<member_v<T, I>>() != 0)
    append_member_entry<T, I>(table, n);
}

template <typename T, std::size_t... Is>
consteval std::array<FieldEntry, field_table_size_v<T>>
build_field_table(std::index_sequence<Is...>)
{
  std::array<FieldEntry, field_table_size_v<T>> table{};
  std::size_t n = 0;
  (append_known_member_entry<T, Is>(table, n), ...);
  std::sort(table.begin(), table.begin() + static_cast<std::ptrdiff_t>(n),
            [](FieldEntry const &a, FieldEntry const &b) {
              return a.fieldno < b.fieldno;
            });
  return table;
}

template <typename T>
consteval auto field_table()
{
  return build_field_table<T>(
      std::make_index_sequence<member_count_v<T>>{});
}

// --- layout validation -------------------------------------------------
// Compile-time rules: every non-UnknownFields member needs exactly one
// field_no annotation (OneOf: one per alternative); UnknownFields must
// carry none; OneOf alternatives must be single-value types; field
// numbers must be >= 1 and unique within a message.

template <meta::info M>
consteval bool member_annotation_count_ok()
{
  using MT = typename [: meta::type_of(M) :];
  if constexpr (std::is_same_v<MT, UnknownFields>)
    return annotation_count<M>() == 0;
  else if constexpr (is_one_of_v<MT>)
    return annotation_count<M>() == std::variant_size_v<MT> - 1;
  else
    return annotation_count<M>() == 1;
}

template <typename T, std::size_t... Is>
consteval bool layout_annotations_ok_impl(std::index_sequence<Is...>)
{
  return (member_annotation_count_ok<member_v<T, Is>>() && ...);
}

template <typename T>
inline constexpr bool layout_annotations_ok_v =
    layout_annotations_ok_impl<T>(
        std::make_index_sequence<member_count_v<T>>{});

template <typename Alt>
inline constexpr bool bad_oneof_alt_v =
    is_vector_v<Alt> || is_map_v<Alt> || is_optional_v<Alt>
    || is_one_of_v<Alt> || std::is_same_v<Alt, UnknownFields>;

template <typename T, std::size_t I, std::size_t... Ks>
consteval bool oneof_alts_ok_impl(std::index_sequence<Ks...>)
{
  constexpr meta::info r = member_v<T, I>;
  using MT = typename [: meta::type_of(r) :];
  return ((!bad_oneof_alt_v<std::variant_alternative_t<Ks + 1, MT>>) && ...);
}

template <typename T, std::size_t I>
consteval bool member_oneof_alts_ok()
{
  constexpr meta::info r = member_v<T, I>;
  using MT = typename [: meta::type_of(r) :];
  if constexpr (is_one_of_v<MT>)
    return oneof_alts_ok_impl<T, I>(
        std::make_index_sequence<std::variant_size_v<MT> - 1>{});
  else
    return true;
}

template <typename T, std::size_t... Is>
consteval bool layout_alts_ok_impl(std::index_sequence<Is...>)
{
  return (member_oneof_alts_ok<T, Is>() && ...);
}

template <typename T>
inline constexpr bool layout_alts_ok_v =
    layout_alts_ok_impl<T>(std::make_index_sequence<member_count_v<T>>{});

template <typename T>
inline constexpr bool bad_ptr_pointee_v =
    !std::is_class_v<T> || std::is_same_v<T, std::string>
    || is_vector_v<T> || is_map_v<T> || is_optional_v<T> || is_one_of_v<T>
    || is_wire_wrapper_v<T> || is_unpacked_wrapper_v<T>
    || std::is_same_v<T, UnknownField> || std::is_same_v<T, UnknownFields>;

template <typename T, std::size_t I>
consteval bool member_ptr_ok()
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (is_unique_ptr_v<M>)
    return !bad_ptr_pointee_v<typename M::element_type>;
  else
    return true;
}

template <typename T, std::size_t... Is>
consteval bool layout_ptrs_ok_impl(std::index_sequence<Is...>)
{
  return (member_ptr_ok<T, Is>() && ...);
}

template <typename T>
inline constexpr bool layout_ptrs_ok_v =
    layout_ptrs_ok_impl<T>(std::make_index_sequence<member_count_v<T>>{});

template <typename T>
consteval bool layout_numbers_ok()
{
  constexpr auto table = field_table<T>();
  for (std::size_t i = 0; i < table.size(); ++i)
    {
      if (table[i].fieldno == 0 || table[i].fieldno > 0x1FFFFFFFu)
        return false;  // protobuf tag: field numbers are 29-bit
      if (i > 0 && table[i - 1].fieldno == table[i].fieldno)
        return false;
    }
  return true;
}

template <typename T>
consteval void check_layout()
{
  static_assert(layout_annotations_ok_v<T>,
                "rpb: every non-UnknownFields member needs exactly one "
                "[[=rpb::field_no<N>{}]] annotation (OneOf: one per "
                "alternative); UnknownFields must carry none");
  static_assert(layout_alts_ok_v<T>,
                "rpb: OneOf alternatives must be single-value types "
                "(scalar/enum/string/wrapper/nested message), not "
                "vector/map/optional/oneof/UnknownFields");
  static_assert(layout_ptrs_ok_v<T>,
                "rpb: std::unique_ptr members must point to message types "
                "(not string/vector/map/optional/oneof/wrappers/"
                "UnknownFields)");
  static_assert(layout_numbers_ok<T>(),
                "rpb: field numbers must be in [1, 536870911] and unique "
                "within a message");
}

}  // namespace rpb
