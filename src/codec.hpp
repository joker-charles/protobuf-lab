// Protobuf wire-format codec driven by C++26 static reflection (P2996 +
// P3394R4 value annotations).
//
// Wire primitives come from protobuf's own
// google/protobuf/io/coded_stream.{h,cc} (linked from libprotobuf); the
// reflection decides which members are which fields.  Field numbers are
// explicit: every member (except UnknownFields) carries one or more
// [[=rpb::field_no<N>{}]] annotations (P3394R4).  Ordinary members carry
// exactly one; OneOf members carry one per alternative, in std::variant
// order.  Known fields serialize in ascending field-number order
// (compile-time sorted); UnknownFields re-emit after all known fields.
//
// Type mapping (v3):
//   std::string                      -> wire type 2 (length-delimited)
//   integral / enum                  -> wire type 0 (varint, sign-extended)
//   SInt<T> (sint32/sint64)          -> wire type 0 (zigzag)
//   Fixed32 / SFixed32               -> wire type 5 (little-endian 4)
//   Fixed64 / SFixed64               -> wire type 1 (little-endian 8)
//   float / double                   -> wire type 5 / 1
//   std::vector<T> (packable T)      -> packed, wire type 2
//   std::vector<T> (string/message)  -> repeated length-delimited
//   std::vector<Unpacked<T>>         -> repeated unpacked (tag per element)
//   std::map<K,V>                    -> repeated map-entry messages (k=1,v=2)
//   std::optional<T>                 -> optional field (skipped when empty)
//   std::unique_ptr<T>               -> singular message (presence; null
//                                        omitted, breaks recursion cycles)
//   OneOf<Ts...>                     -> oneof: one field per alternative
//                                        (presence semantics; default
//                                        values still serialize)
//   nested struct                    -> embedded message, wire type 2
//   UnknownFields (any position)     -> unknown-field preservation
//
// proto3 default omission: scalar / string / bytes / enum members equal to
// their default (and empty packed vectors) are not serialized. Nested
// messages serialize unless all their members are default/absent (value
// semantics cannot distinguish unset from present-but-empty; optional and
// unique_ptr keep real presence). Repeated length-delimited elements are
// never omitted, even when empty. Oneof alternatives serialize whenever
// they are set, even at default values.

#pragma once

#include <meta>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

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

// --- oneof -------------------------------------------------------------
// OneOf<Ts...> = std::variant<std::monostate, Ts...>; monostate means the
// oneof is unset.  Each alternative is a distinct protobuf field with its
// own field_no annotation.

template <typename... Ts>
using OneOf = std::variant<std::monostate, Ts...>;

template <typename T> struct is_one_of : std::false_type {};
template <typename... Ts> struct is_one_of<OneOf<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_one_of_v = is_one_of<T>::value;

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

// --- proto3 default-value omission ---------------------------------------

template <typename M>
inline constexpr bool is_omittable_v =
    std::is_arithmetic_v<M> || std::is_enum_v<M>
    || std::is_same_v<M, std::string> || is_wire_wrapper_v<M>
    || is_bytes_wrapper_v<M> || is_vector_v<M> || is_map_v<M>;

// A by-value member that is neither a scalar/container nor a
// presence-bearing type (optional/unique_ptr/oneof) is a plain nested
// message.  Value semantics cannot distinguish "unset" from "set to an
// empty message", so plain nested messages are omitted when all their
// members are default/absent (matching protobuf for the unset case).
template <typename M>
inline constexpr bool is_plain_message_v =
    !std::is_arithmetic_v<M> && !std::is_enum_v<M>
    && !std::is_same_v<M, std::string> && !is_vector_v<M> && !is_map_v<M>
    && !is_optional_v<M> && !is_unique_ptr_v<M> && !is_one_of_v<M>
    && !is_wire_wrapper_v<M> && !is_unpacked_wrapper_v<M>
    && !is_bytes_wrapper_v<M>
    && !std::is_same_v<M, UnknownField> && !std::is_same_v<M, UnknownFields>;

template <typename M>
bool is_default_value(M const &v)
{
  if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    return underlying_value(v) == underlying_value(M{});
  else if constexpr (std::is_floating_point_v<M>)
    return v == 0.0;
  else if constexpr (std::is_same_v<M, std::string>)
    return v.empty();
  else if constexpr (is_sint_wrapper_v<M>)
    return v.value == typename M::value_type{};
  else if constexpr (is_fixed_wrapper_v<M>)
    return v.value == 0;
  else if constexpr (is_bytes_wrapper_v<M>)
    return v.value.empty();
  else
    return v.empty();  // vectors: empty repeated fields are omitted
}

template <typename T> bool is_empty_message(T const &v);

template <typename M>
bool member_is_default_or_absent(M const &v)
{
  if constexpr (is_omittable_v<M>)
    return is_default_value(v);
  else if constexpr (is_optional_v<M>)
    return !v.has_value();
  else if constexpr (is_unique_ptr_v<M>)
    return !v;
  else if constexpr (is_one_of_v<M>)
    return v.index() == 0;  // monostate: unset
  else
    return is_empty_message(v);  // plain nested message
}

template <typename T>
bool is_empty_message(T const &v)
{
  bool empty = true;
  template for (constexpr auto m :
                std::define_static_array(
                    meta::nonstatic_data_members_of(^^T, members_ctx())))
    {
      if (!member_is_default_or_absent(v.[:m:]))
        empty = false;
    }
  return empty;
}

// --- zigzag (sint32/sint64) helpers, matching protobuf's encoding ---
// Kept hand-rolled on purpose: protobuf's WireFormatLite::ZigZagEncode* is
// in the google::protobuf::internal namespace, and we avoid internal::
// dependencies.

constexpr std::uint32_t zigzag32(std::int32_t n)
{
  std::uint32_t u = static_cast<std::uint32_t>(n);
  return (u << 1) ^ (0u - (u >> 31));
}
constexpr std::uint64_t zigzag64(std::int64_t n)
{
  std::uint64_t u = static_cast<std::uint64_t>(n);
  return (u << 1) ^ (0ull - (u >> 63));
}
constexpr std::int32_t unzigzag32(std::uint32_t u)
{
  return static_cast<std::int32_t>((u >> 1) ^ (0u - (u & 1)));
}
constexpr std::int64_t unzigzag64(std::uint64_t u)
{
  return static_cast<std::int64_t>((u >> 1) ^ (0ull - (u & 1)));
}

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
  return (annotation_count<member_v<T, Is>>() + ...);
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
      if (table[i].fieldno == 0)
        return false;
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
                "rpb: field numbers must be >= 1 and unique within a "
                "message");
}

// --- serialization ---

template <typename T> void serialize(std::string &out, T const &v);

template <typename U>
void write_packed_element(google::protobuf::io::CodedOutputStream &cos,
                          U const &e)
{
  if constexpr (std::is_floating_point_v<U>)
    {
      if constexpr (sizeof(U) == 4)
        cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(e));
      else
        cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(e));
    }
  else if constexpr (is_sint_wrapper_v<U>)
    {
      using R = typename U::value_type;
      if constexpr (sizeof(R) == 4)
        cos.WriteVarint64(zigzag32(static_cast<std::int32_t>(e.value)));
      else if constexpr (sizeof(R) == 8)
        cos.WriteVarint64(zigzag64(static_cast<std::int64_t>(e.value)));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
    }
  else if constexpr (std::is_same_v<U, Fixed32>
                     || std::is_same_v<U, SFixed32>)
    cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(e.value));
  else if constexpr (std::is_same_v<U, Fixed64>
                     || std::is_same_v<U, SFixed64>)
    cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(e.value));
  else
    {
      if constexpr (std::is_signed_v<underlying_or_self_t<U>>)
        cos.WriteVarint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(underlying_value(e))));
      else
        cos.WriteVarint64(static_cast<std::uint64_t>(underlying_value(e)));
    }
}

template <typename M>
void serialize_value(google::protobuf::io::CodedOutputStream &cos,
                     std::uint32_t fieldno, M const &val)
{
  if constexpr (std::is_same_v<M, std::string>)
    {
      cos.WriteTag((fieldno << 3) | 2);
      // protobuf 3.21's WriteString emits raw bytes WITHOUT the length
      // prefix (only WriteStringWithSizeToArray includes it).
      cos.WriteVarint32(static_cast<std::uint32_t>(val.size()));
      cos.WriteRaw(val.data(), static_cast<int>(val.size()));
    }
  else if constexpr (is_bytes_wrapper_v<M>)
    {
      cos.WriteTag((fieldno << 3) | 2);
      cos.WriteVarint32(static_cast<std::uint32_t>(val.value.size()));
      cos.WriteRaw(val.value.data(), static_cast<int>(val.value.size()));
    }
  else if constexpr (std::is_floating_point_v<M>)
    {
      if constexpr (sizeof(M) == 4)
        {
          cos.WriteTag((fieldno << 3) | 5);
          cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(val));
        }
      else
        {
          cos.WriteTag((fieldno << 3) | 1);
          cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(val));
        }
    }
  else if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    {
      cos.WriteTag((fieldno << 3) | 0);
      using R = underlying_or_self_t<M>;
      if constexpr (std::is_same_v<R, bool>)
        cos.WriteVarint64(val ? 1 : 0);
      else if constexpr (std::is_signed_v<R>)
        cos.WriteVarint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(underlying_value(val))));
      else
        cos.WriteVarint64(static_cast<std::uint64_t>(underlying_value(val)));
    }
  else if constexpr (is_sint_wrapper_v<M>)
    {
      cos.WriteTag((fieldno << 3) | 0);
      using R = typename M::value_type;
      if constexpr (sizeof(R) == 4)
        cos.WriteVarint64(zigzag32(static_cast<std::int32_t>(val.value)));
      else if constexpr (sizeof(R) == 8)
        cos.WriteVarint64(zigzag64(static_cast<std::int64_t>(val.value)));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
    }
  else if constexpr (std::is_same_v<M, Fixed32>
                     || std::is_same_v<M, SFixed32>)
    {
      cos.WriteTag((fieldno << 3) | 5);
      cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(val.value));
    }
  else if constexpr (std::is_same_v<M, Fixed64>
                     || std::is_same_v<M, SFixed64>)
    {
      cos.WriteTag((fieldno << 3) | 1);
      cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(val.value));
    }
  else if constexpr (is_unpacked_wrapper_v<M>)
    serialize_value(cos, fieldno, val.value);
  else if constexpr (std::is_same_v<M, UnknownFields>)
    {
      for (auto const &uf : val)
        {
          cos.WriteTag((uf.fieldno << 3) | uf.wire_type);
          cos.WriteRaw(uf.raw.data(), static_cast<int>(uf.raw.size()));
        }
    }
  else if constexpr (is_map_v<M>)
    {
      // Each map entry is an embedded message with key = field 1 and
      // value = field 2. Entries serialize in std::map (sorted) order;
      // protobuf does not guarantee map order, so byte-level interop only
      // uses single-entry maps.
      for (auto const &kv : val)
        {
          std::string payload;
          {
            google::protobuf::io::StringOutputStream sos(&payload);
            google::protobuf::io::CodedOutputStream pcos(&sos);
            serialize_value(pcos, 1, kv.first);
            serialize_value(pcos, 2, kv.second);
          }
          cos.WriteTag((fieldno << 3) | 2);
          cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
          cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
        }
    }
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_packable_v<U>)
        {
          std::string payload;
          {
            google::protobuf::io::StringOutputStream sos(&payload);
            google::protobuf::io::CodedOutputStream pcos(&sos);
            for (auto const &e : val)
              write_packed_element(pcos, e);
          }
          cos.WriteTag((fieldno << 3) | 2);
          cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
          cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
        }
      else
        {
          for (auto const &e : val)
            serialize_value(cos, fieldno, e);
        }
    }
  else if constexpr (is_optional_v<M>)
    {
      if (val.has_value())
        serialize_value(cos, fieldno, *val);
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      // Singular message behind a pointer: presence semantics, null ->
      // omitted, non-null -> embedded message (even if empty).
      if (!val)
        return;
      std::string payload;
      serialize(payload, *val);
      cos.WriteTag((fieldno << 3) | 2);
      cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
      cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
    }
  else
    {
      if constexpr (std::is_same_v<M, UnknownField>)
        return;  // internal single-entry type is never a wire message
      else
        {
          std::string payload;
          serialize(payload, val);
          cos.WriteTag((fieldno << 3) | 2);
          cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
          cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
        }
    }
}

template <typename T, std::uint32_t FNO, std::size_t MI, std::size_t AI>
void serialize_entry(google::protobuf::io::CodedOutputStream &cos, T const &v)
{
  constexpr meta::info r = member_v<T, MI>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (AI == 0)
    {
      // Ordinary member: proto3 default omission.
      if constexpr (is_omittable_v<M>)
        {
          if (is_default_value(v.[:r:]))
            return;
        }
      else if constexpr (is_plain_message_v<M>)
        {
          // Value semantics: an all-default nested struct behaves like an
          // unset message and is omitted (present-but-empty is not
          // representable).  optional/unique_ptr keep real presence.
          if (is_empty_message(v.[:r:]))
            return;
        }
      serialize_value(cos, FNO, v.[:r:]);
    }
  else
    {
      // OneOf alternative: presence semantics (set -> emit, even defaults).
      if (v.[:r:].index() != AI)
        return;
      serialize_value(cos, FNO, std::get<AI>(v.[:r:]));
    }
}

template <typename T>
void serialize(std::string &out, T const &v)
{
  check_layout<T>();
  google::protobuf::io::StringOutputStream sos(&out);
  google::protobuf::io::CodedOutputStream cos(&sos);
  template for (constexpr auto e : std::define_static_array(field_table<T>()))
    {
      serialize_entry<T, e.fieldno, e.member, e.alt>(cos, v);
    }
  if constexpr (has_unknown_fields_v<T>)
    {
      // Unknown fields re-emit after all known fields (protobuf behavior).
      serialize_value(cos, 0,
                      v.[: member_v<T, unknown_member_index_v<T>> :]);
    }
}

// --- parsing ---

template <typename T> bool parse(std::string_view data, T &v);

template <typename U>
bool read_packed_element(google::protobuf::io::CodedInputStream &cis, U &out)
{
  if constexpr (std::is_floating_point_v<U>)
    {
      if constexpr (sizeof(U) == 4)
        {
          std::uint32_t raw;
          if (!cis.ReadLittleEndian32(&raw))
            return false;
          out = std::bit_cast<float>(raw);
        }
      else
        {
          std::uint64_t raw;
          if (!cis.ReadLittleEndian64(&raw))
            return false;
          out = std::bit_cast<double>(raw);
        }
      return true;
    }
  else if constexpr (is_sint_wrapper_v<U>)
    {
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = typename U::value_type;
      if constexpr (sizeof(R) == 4)
        out.value =
            static_cast<R>(unzigzag32(static_cast<std::uint32_t>(raw)));
      else if constexpr (sizeof(R) == 8)
        out.value = static_cast<R>(unzigzag64(raw));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
      return true;
    }
  else if constexpr (std::is_same_v<U, Fixed32>
                     || std::is_same_v<U, SFixed32>)
    {
      std::uint32_t raw;
      if (!cis.ReadLittleEndian32(&raw))
        return false;
      out.value = std::bit_cast<decltype(out.value)>(raw);
      return true;
    }
  else if constexpr (std::is_same_v<U, Fixed64>
                     || std::is_same_v<U, SFixed64>)
    {
      std::uint64_t raw;
      if (!cis.ReadLittleEndian64(&raw))
        return false;
      out.value = std::bit_cast<decltype(out.value)>(raw);
      return true;
    }
  else
    {
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = underlying_or_self_t<U>;
      if constexpr (std::is_same_v<R, bool>)
        out = static_cast<U>(raw != 0);
      else if constexpr (std::is_signed_v<R>)
        out = static_cast<U>(static_cast<R>(static_cast<std::int64_t>(raw)));
      else
        out = static_cast<U>(static_cast<R>(raw));
      return true;
    }
}

inline bool skip_field(google::protobuf::io::CodedInputStream &cis,
                       std::uint32_t fieldno, std::uint32_t wt);

// Reads a length-delimited payload (wire type 2) into `payload`.
bool read_message_payload(google::protobuf::io::CodedInputStream &cis,
                          std::uint32_t wt, std::string &payload)
{
  if (wt != 2)
    return false;
  std::uint32_t len;
  if (!cis.ReadVarint32(&len))
    return false;
  payload.assign(static_cast<std::size_t>(len), '\0');
  return cis.ReadRaw(payload.data(), static_cast<int>(len));
}

template <typename M>
bool parse_value(google::protobuf::io::CodedInputStream &cis, std::uint32_t wt,
                 M &val)
{
  if constexpr (std::is_same_v<M, std::string>)
    {
      if (wt != 2)
        return false;
      std::uint32_t len;
      if (!cis.ReadVarint32(&len))
        return false;
      // protobuf 3.21's ReadRaw(void*, int) copies into a caller buffer
      // (newer versions use a const void** out-parameter instead).
      val.resize(static_cast<std::size_t>(len));
      if (!cis.ReadRaw(val.data(), static_cast<int>(len)))
        return false;
      return true;
    }
  else if constexpr (is_bytes_wrapper_v<M>)
    return read_message_payload(cis, wt, val.value);
  else if constexpr (std::is_floating_point_v<M>)
    {
      if constexpr (sizeof(M) == 4)
        {
          if (wt != 5)
            return false;
          std::uint32_t raw;
          if (!cis.ReadLittleEndian32(&raw))
            return false;
          val = std::bit_cast<float>(raw);
          return true;
        }
      else
        {
          if (wt != 1)
            return false;
          std::uint64_t raw;
          if (!cis.ReadLittleEndian64(&raw))
            return false;
          val = std::bit_cast<double>(raw);
          return true;
        }
    }
  else if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    {
      if (wt != 0)
        return false;
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = underlying_or_self_t<M>;
      if constexpr (std::is_same_v<R, bool>)
        val = static_cast<M>(raw != 0);
      else if constexpr (std::is_signed_v<R>)
        val = static_cast<M>(static_cast<R>(static_cast<std::int64_t>(raw)));
      else
        val = static_cast<M>(static_cast<R>(raw));
      return true;
    }
  else if constexpr (is_sint_wrapper_v<M>)
    {
      if (wt != 0)
        return false;
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = typename M::value_type;
      if constexpr (sizeof(R) == 4)
        val.value =
            static_cast<R>(unzigzag32(static_cast<std::uint32_t>(raw)));
      else if constexpr (sizeof(R) == 8)
        val.value = static_cast<R>(unzigzag64(raw));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
      return true;
    }
  else if constexpr (std::is_same_v<M, Fixed32>
                     || std::is_same_v<M, SFixed32>)
    {
      if (wt != 5)
        return false;
      std::uint32_t raw;
      if (!cis.ReadLittleEndian32(&raw))
        return false;
      val.value = std::bit_cast<decltype(val.value)>(raw);
      return true;
    }
  else if constexpr (std::is_same_v<M, Fixed64>
                     || std::is_same_v<M, SFixed64>)
    {
      if (wt != 1)
        return false;
      std::uint64_t raw;
      if (!cis.ReadLittleEndian64(&raw))
        return false;
      val.value = std::bit_cast<decltype(val.value)>(raw);
      return true;
    }
  else if constexpr (is_unpacked_wrapper_v<M>)
    return parse_value(cis, wt, val.value);
  else if constexpr (is_map_v<M>)
    {
      if (wt != 2)
        return false;
      std::uint32_t len;
      if (!cis.ReadVarint32(&len))
        return false;
      std::string payload(static_cast<std::size_t>(len), '\0');
      if (!cis.ReadRaw(payload.data(), static_cast<int>(len)))
        return false;
      using K = typename M::key_type;
      using V = typename M::mapped_type;
      K k{};
      V v{};
      google::protobuf::io::ArrayInputStream ais(
          payload.data(), static_cast<int>(payload.size()));
      google::protobuf::io::CodedInputStream ecis(&ais);
      ecis.SetRecursionLimit(64);
      for (;;)
        {
          std::uint32_t tag = ecis.ReadTag();
          if (tag == 0)
            break;
          std::uint32_t fn = tag >> 3;
          std::uint32_t w = tag & 7;
          if (fn == 1)
            {
              if (!parse_value(ecis, w, k))
                return false;
            }
          else if (fn == 2)
            {
              if (!parse_value(ecis, w, v))
                return false;
            }
          else
            {
              if (!skip_field(ecis, fn, w))
                return false;
            }
        }
      val[std::move(k)] = std::move(v);
      return true;
    }
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_packable_v<U>)
        {
          if (wt != 2)
            {
              // Accept a single unpacked element too; protobuf parsers
              // accept both packed and unpacked forms for repeated numerics.
              U e;
              if (!read_packed_element(cis, e))
                return false;
              val.push_back(std::move(e));
              return true;
            }
          std::uint32_t len;
          if (!cis.ReadVarint32(&len))
            return false;
          auto limit = cis.PushLimit(static_cast<int>(len));
          while (cis.BytesUntilLimit() > 0)
            {
              U e;
              if (!read_packed_element(cis, e))
                {
                  cis.PopLimit(limit);
                  return false;
                }
              val.push_back(e);
            }
          cis.PopLimit(limit);
          return true;
        }
      else
        {
          // repeated length-delimited: one element per tag
          U e;
          if (!parse_value(cis, wt, e))
            return false;
          val.push_back(std::move(e));
          return true;
        }
    }
  else if constexpr (is_optional_v<M>)
    {
      if (val.has_value())
        {
          // Merge into the existing value: scalar/string/wrapper
          // alternatives overwrite (last-wins), but message alternatives
          // accumulate fields, matching the codec's plain-member/oneof
          // merge semantics.
          return parse_value(cis, wt, *val);
        }
      typename M::value_type inner{};
      if (!parse_value(cis, wt, inner))
        return false;
      val = std::move(inner);
      return true;
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      // Singular message behind a pointer: allocate on hit.
      std::string payload;
      if (!read_message_payload(cis, wt, payload))
        return false;
      if (!val)
        val = std::make_unique<typename M::element_type>();
      return parse(payload, *val);
    }
  else
    {
      // embedded message
      if constexpr (std::is_same_v<M, UnknownField>)
        return false;  // internal single-entry type is never a wire message
      else
        {
          std::string payload;
          if (!read_message_payload(cis, wt, payload))
            return false;
          // Merge into the existing value: protobuf merges repeated
          // occurrences of a singular message field instead of replacing.
          return parse(payload, val);
        }
    }
}

template <typename T, std::size_t I, std::size_t K>
bool parse_oneof_alt(google::protobuf::io::CodedInputStream &cis,
                     std::uint32_t fieldno, std::uint32_t wt, T &v)
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if (fieldno != field_number<r, K>())
    return false;
  if (v.[:r:].index() == K + 1)
    {
      // Same oneof alternative already active: merge into it (protobuf
      // semantics), so repeated message occurrences accumulate fields.
      return parse_value(cis, wt, std::get<K + 1>(v.[:r:]));
    }
  using Alt = std::variant_alternative_t<K + 1, M>;
  Alt val{};
  if (!parse_value(cis, wt, val))
    return false;
  v.[:r:].template emplace<K + 1>(std::move(val));
  return true;
}

template <typename T, std::size_t I, std::size_t... Ks>
bool parse_oneof_member(google::protobuf::io::CodedInputStream &cis,
                        std::uint32_t fieldno, std::uint32_t wt, T &v,
                        std::index_sequence<Ks...>)
{
  // Repeated occurrences of a oneof: last one wins (emplace overwrites).
  bool handled = false;
  ((handled = handled || parse_oneof_alt<T, I, Ks>(cis, fieldno, wt, v)),
   ...);
  return handled;
}

template <typename T, std::size_t I>
bool parse_member(google::protobuf::io::CodedInputStream &cis,
                  std::uint32_t fieldno, std::uint32_t wt, T &v)
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (is_one_of_v<M>)
    {
      return parse_oneof_member<T, I>(
          cis, fieldno, wt, v,
          std::make_index_sequence<std::variant_size_v<M> - 1>{});
    }
  else
    {
      // UnknownFields members have no annotation -> field_number returns 0,
      // which never matches a real tag, so they fall through to capture.
      if (fieldno != field_number<r, 0>())
        return false;
      return parse_value(cis, wt, v.[:r:]);
    }
}

template <typename T, std::size_t... Is>
bool parse_fields(google::protobuf::io::CodedInputStream &cis,
                  std::uint32_t fieldno, std::uint32_t wt, T &v,
                  std::index_sequence<Is...>)
{
  bool handled = false;
  ((handled = handled || parse_member<T, Is>(cis, fieldno, wt, v)), ...);
  return handled;
}

inline bool capture_unknown_field(google::protobuf::io::CodedInputStream &cis,
                                  std::uint32_t fieldno, std::uint32_t wt,
                                  UnknownFields &out)
{
  UnknownField uf{fieldno, wt, {}};
  switch (wt)
    {
    case 0:
      {
        std::uint64_t tmp;
        if (!cis.ReadVarint64(&tmp))
          return false;
        // Re-encode canonically; unknown varints are stored by value, like
        // protobuf's UnknownFieldSet.
        google::protobuf::io::StringOutputStream sos(&uf.raw);
        google::protobuf::io::CodedOutputStream cos(&sos);
        cos.WriteVarint64(tmp);
        break;
      }
    case 1:
      uf.raw.resize(8);
      if (!cis.ReadRaw(uf.raw.data(), 8))
        return false;
      break;
    case 2:
      {
        std::uint32_t len;
        if (!cis.ReadVarint32(&len))
          return false;
        // Store the length prefix inside raw so re-emission is just
        // tag + raw for every wire type.
        std::string head;
        {
          google::protobuf::io::StringOutputStream sos(&head);
          google::protobuf::io::CodedOutputStream cos(&sos);
          cos.WriteVarint32(len);
        }
        uf.raw = std::move(head);
        std::size_t base = uf.raw.size();
        uf.raw.resize(base + len);
        if (!cis.ReadRaw(uf.raw.data() + static_cast<int>(base),
                         static_cast<int>(len)))
          return false;
        break;
      }
    case 5:
      uf.raw.resize(4);
      if (!cis.ReadRaw(uf.raw.data(), 4))
        return false;
      break;
    default:
      return false;  // groups (3/4) are skipped, not captured
    }
  out.push_back(std::move(uf));
  return true;
}

inline bool skip_field(google::protobuf::io::CodedInputStream &cis,
                       std::uint32_t fieldno, std::uint32_t wt)
{
  switch (wt)
    {
    case 0:
      {
        std::uint64_t tmp;
        return cis.ReadVarint64(&tmp);
      }
    case 1:
      return cis.Skip(8);
    case 2:
      {
        std::uint32_t len;
        if (!cis.ReadVarint32(&len))
          return false;
        return cis.Skip(static_cast<int>(len));
      }
    case 3:  // start group: skip until the matching end-group tag
      for (;;)
        {
          std::uint32_t tag = cis.ReadTag();
          if (tag == 0)
            return false;  // truncated group
          std::uint32_t fn = tag >> 3;
          std::uint32_t w = tag & 7;
          if (w == 4)
            return fn == fieldno;
          if (!skip_field(cis, fn, w))
            return false;
        }
    case 5:
      return cis.Skip(4);
    default:
      return false;
    }
}

template <typename T>
bool parse(std::string_view data, T &v)
{
  check_layout<T>();
  google::protobuf::io::ArrayInputStream ais(data.data(),
                                             static_cast<int>(data.size()));
  google::protobuf::io::CodedInputStream cis(&ais);
  cis.SetRecursionLimit(64);
  for (;;)
    {
      std::uint32_t tag = cis.ReadTag();
      if (tag == 0)
        break;  // clean EOF
      std::uint32_t fieldno = tag >> 3;
      std::uint32_t wt = tag & 7;
      if (fieldno == 0)
        return false;  // protobuf: field number 0 is an illegal tag
      if (!parse_fields<T>(cis, fieldno, wt, v,
                           std::make_index_sequence<member_count_v<T>>{}))
        {
          if constexpr (has_unknown_fields_v<T>)
            {
              if (capture_unknown_field(
                      cis, fieldno, wt,
                      v.[: member_v<T, unknown_member_index_v<T>> :]))
                continue;
            }
          if (!skip_field(cis, fieldno, wt))
            return false;
        }
    }
  return true;
}

// --- deep equality (test helper) ---------------------------------------
// unique_ptr members are dereferenced recursively; strings / containers /
// enums use operator==; variants compare by index and then recurse into the
// active alternative (variant::operator== would compare unique_ptr
// alternatives by pointer); anything else (messages, wire wrappers) is
// compared member-wise via reflection.  Lets message structs with
// unique_ptr members keep a one-line operator== without writing out every
// member.

template <typename T> bool deep_equal(T const &a, T const &b);

template <typename T>
bool deep_equal_message(T const &a, T const &b)
{
  bool ok = true;
  template for (constexpr auto m :
                std::define_static_array(
                    meta::nonstatic_data_members_of(^^T, members_ctx())))
    {
      if (!deep_equal(a.[:m:], b.[:m:]))
        ok = false;
    }
  return ok;
}

template <typename T>
bool deep_equal(T const &a, T const &b)
{
  if constexpr (is_unique_ptr_v<T>)
    {
      if (!a && !b)
        return true;
      return a && b && deep_equal(*a, *b);
    }
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>
                     || std::is_same_v<T, std::string> || is_vector_v<T>
                     || is_map_v<T> || is_optional_v<T>
                     || std::is_same_v<T, UnknownFields>)
    return a == b;
  else if constexpr (is_one_of_v<T>)
    {
      if (a.index() != b.index())
        return false;
      if (a.index() == 0)
        return true;  // both monostate (unset)
      return std::visit(
          [&](auto const &x) {
            return deep_equal(
                x, std::get<std::remove_cvref_t<decltype(x)>>(b));
          },
          a);
    }
  else
    return deep_equal_message(a, b);
}

// --- deep copy (test helper) -------------------------------------------
// Value-semantics copy for messages with unique_ptr members (mirror of
// deep_equal): containers/variants/optionals are rebuilt element-wise so
// move-only message members deep-copy; unique_ptr members allocate fresh
// pointees; everything else copies as-is.  Gives structs with unique_ptr
// members protobuf-style deep-copy semantics without hand-written copy
// constructors.

template <typename T> T deep_copy(T const &src);

template <typename T>
T deep_copy_message(T const &src)
{
  T dst;
  template for (constexpr auto m :
                std::define_static_array(
                    meta::nonstatic_data_members_of(^^T, members_ctx())))
    {
      dst.[:m:] = deep_copy(src.[:m:]);
    }
  return dst;
}

template <typename T>
T deep_copy(T const &src)
{
  if constexpr (is_unique_ptr_v<T>)
    {
      if (!src)
        return nullptr;
      using E = typename T::element_type;
      return std::make_unique<E>(deep_copy(*src));
    }
  else if constexpr (std::is_same_v<T, std::vector<bool>>)
    return src;  // bit-proxy elements; plain copy is exact
  else if constexpr (is_vector_v<T>)
    {
      T dst;
      dst.reserve(src.size());
      for (auto const &e : src)
        dst.push_back(deep_copy(e));
      return dst;
    }
  else if constexpr (is_map_v<T>)
    {
      T dst;
      for (auto const &kv : src)
        dst.emplace(kv.first, deep_copy(kv.second));
      return dst;
    }
  else if constexpr (is_optional_v<T>)
    {
      if (!src)
        return std::nullopt;
      return std::optional<typename T::value_type>(deep_copy(*src));
    }
  else if constexpr (is_one_of_v<T>)
    {
      if (src.index() == 0)
        return T{};  // monostate (unset)
      return std::visit(
          [](auto const &x) { return T(deep_copy(x)); }, src);
    }
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>
                     || std::is_same_v<T, std::string>
                     || std::is_same_v<T, UnknownFields>)
    return src;
  else
    return deep_copy_message(src);
}

}  // namespace rpb
