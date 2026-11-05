// Field-number and field-name annotations (P3394R4).
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include <meta>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace rpb {

namespace meta = std::meta;

consteval auto members_ctx()
{
  return meta::access_context::unprivileged();
}

template <typename T>
inline constexpr std::size_t member_count_v =
    meta::nonstatic_data_members_of(^^T, members_ctx()).size();

template <typename T, std::size_t I>
inline constexpr meta::info member_v =
    meta::nonstatic_data_members_of(^^T, members_ctx())[I];

// --- field-number annotations (P3394R4) --------------------------------
// [[=rpb::field_no<N>{}]] declares the wire field number of a member.
// The number rides on the annotation TYPE (static constexpr `value`), so
// it is read back with a scope splice ([: type_of(ann) :]::value) -- no
// std::meta::extract involved.  OneOf members carry one annotation per
// alternative, in declaration order.

template <std::uint32_t N>
struct field_no
{
  static constexpr std::uint32_t value = N;
};

template <typename T> struct is_field_no : std::false_type {};
template <std::uint32_t N> struct is_field_no<field_no<N>> : std::true_type {};
template <typename T> inline constexpr bool is_field_no_v =
    is_field_no<T>::value;

// --- field-name annotations (text format / JSON) -----------------------
// The wire codec only needs field numbers, so the C++ member identifier
// doubles as the proto field name for ordinary members.  OneOf members
// collapse several proto fields into one std::variant member, so each
// alternative's proto field name is declared explicitly with
// [[=rpb::name<"...">{}]] (one per alternative, in the same order as the
// field_no annotations).  Also usable on ordinary members whose C++
// identifier differs from the proto field name.

template <std::size_t N>
struct fixed_string
{
  char data[N]{};
  constexpr fixed_string(char const (&s)[N])
  {
    for (std::size_t i = 0; i < N; ++i)
      data[i] = s[i];
  }
  constexpr std::string_view view() const { return {data, N - 1}; }
};
template <std::size_t N>
fixed_string(char const (&)[N]) -> fixed_string<N>;

template <fixed_string S>
struct name
{
  static constexpr std::string_view value = S.view();
};

template <typename T> struct is_name : std::false_type {};
template <fixed_string S> struct is_name<name<S>> : std::true_type {};
template <typename T> inline constexpr bool is_name_v = is_name<T>::value;

// Marker annotation [[=rpb::bytes_type{}]]: a std::string member that is a
// protobuf `bytes` field rather than `string` (the wire codec treats both
// as length-delimited, but text format / JSON need to know: bytes fields
// skip UTF-8 validation and serialize as base64 in JSON).
struct bytes_type {};
template <typename T> struct is_bytes_ann : std::false_type {};
template <> struct is_bytes_ann<bytes_type> : std::true_type {};
template <typename T> inline constexpr bool is_bytes_ann_v =
    is_bytes_ann<T>::value;

template <meta::info M>
consteval bool member_is_bytes_ann()
{
  bool is_bytes = false;
  template for (constexpr auto ann :
                std::define_static_array(meta::annotations_of(M)))
    {
      using A = std::remove_cvref_t<typename [: meta::type_of(ann) :]>;
      if constexpr (is_bytes_ann_v<A>)
        is_bytes = true;
    }
  return is_bytes;
}

// Marker annotation [[=rpb::group_type{}]] on a message-typed member: the
// field is a proto2 group and must be encoded with START_GROUP/END_GROUP
// (wire types 3/4) instead of a length-delimited embedded message.  The
// member's element type is an ordinary annotated message struct.
struct group_type {};
template <typename T> struct is_group_ann : std::false_type {};
template <> struct is_group_ann<group_type> : std::true_type {};
template <typename T> inline constexpr bool is_group_ann_v =
    is_group_ann<T>::value;

template <meta::info M>
consteval bool member_is_group_ann()
{
  bool is_group = false;
  template for (constexpr auto ann :
                std::define_static_array(meta::annotations_of(M)))
    {
      using A = std::remove_cvref_t<typename [: meta::type_of(ann) :]>;
      if constexpr (is_group_ann_v<A>)
        is_group = true;
    }
  return is_group;
}

// Proto field name of member `r` alternative `alt` (alt == 0 for ordinary
// members, else the std::variant index): an explicit `name` annotation if
// present (ordinary members may carry one, e.g. to give a proto2 group its
// type name), else the C++ identifier for ordinary members; the alt-th
// `name` annotation for OneOf alternatives.
template <meta::info M>
consteval std::string_view member_first_name()
{
  std::string_view result{};
  template for (constexpr auto ann :
                std::define_static_array(meta::annotations_of(M)))
    {
      using A = std::remove_cvref_t<typename [: meta::type_of(ann) :]>;
      if constexpr (is_name_v<A>)
        result = A::value;
    }
  return result;
}

template <meta::info M, std::size_t Alt>
consteval std::string_view member_alt_name()
{
  using MT = typename [: meta::type_of(M) :];
  if constexpr (Alt == 0)
    {
      constexpr std::string_view first = member_first_name<M>();
      return first.empty() ? meta::identifier_of(M) : first;
    }
  else
    {
      std::string_view result{};
      std::size_t n = 0;
      template for (constexpr auto ann :
                    std::define_static_array(meta::annotations_of(M)))
        {
          using A = std::remove_cvref_t<typename [: meta::type_of(ann) :]>;
          if constexpr (is_name_v<A>)
            {
              if (n == Alt - 1)
                result = A::value;
              ++n;
            }
        }
      return result;
    }
}

// --- annotation reflection helpers (NTTP only; a function parameter is
// not a constant expression) ---

template <meta::info M>
consteval std::size_t annotation_count()
{
  std::size_t n = 0;
  template for (constexpr auto ann :
                std::define_static_array(meta::annotations_of(M)))
    {
      using A = std::remove_cvref_t<typename [: meta::type_of(ann) :]>;
      if (is_field_no_v<A>)
        ++n;
    }
  return n;
}

template <meta::info M, std::size_t K>
consteval std::uint32_t field_number()
{
  std::size_t seen = 0;
  template for (constexpr auto ann :
                std::define_static_array(meta::annotations_of(M)))
    {
      using A = std::remove_cvref_t<typename [: meta::type_of(ann) :]>;
      if constexpr (is_field_no_v<A>)
        {
          if (seen == K)
            return [: meta::type_of(ann) :]::value;
          ++seen;
        }
    }
  return 0;  // not found (unreachable for valid layouts)
}

}  // namespace rpb
