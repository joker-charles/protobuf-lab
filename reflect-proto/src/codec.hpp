// Protobuf wire-format codec driven by C++26 static reflection (P2996).
//
// Wire primitives come from protobuf's own
// google/protobuf/io/coded_stream.{h,cc} (linked from libprotobuf); the
// reflection decides which members are which fields.  Field number =
// member position + 1 (v1 convention).
//
// Type mapping (v2):
//   std::string                      -> wire type 2 (length-delimited)
//   integral / enum                  -> wire type 0 (varint, sign-extended)
//   SInt<T> (sint32/sint64)          -> wire type 0 (zigzag)
//   Fixed32 / SFixed32               -> wire type 5 (little-endian 4)
//   Fixed64 / SFixed64               -> wire type 1 (little-endian 8)
//   float / double                   -> wire type 5 / 1
//   std::vector<T> (packable T)      -> packed, wire type 2
//   std::vector<T> (string/message)  -> repeated length-delimited
//   std::optional<T>                 -> optional field (skipped when empty)
//   nested struct                    -> embedded message, wire type 2
//   UnknownFields (trailing member)  -> unknown-field preservation
//
// proto3 default omission: scalar / string / bytes / enum members equal to
// their default (and empty packed vectors) are not serialized. Nested
// messages and optionals-with-value are always serialized. Repeated
// length-delimited elements are never omitted, even when empty.

#pragma once

#include <meta>

#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
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

// --- type traits ---

template <typename T> struct is_vector : std::false_type {};
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type {};
template <typename T> inline constexpr bool is_vector_v = is_vector<T>::value;

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type {};
template <typename T> inline constexpr bool is_optional_v =
    is_optional<T>::value;

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

// --- explicit wire-type wrappers (protobuf wire types not expressible by a
// plain C++ type: sint zigzag and fixed-width integers) ---

template <typename T> struct SInt
{
  using value_type = T;
  T value{};
  bool operator==(SInt const &) const = default;
};
struct Fixed32
{
  std::uint32_t value{};
  bool operator==(Fixed32 const &) const = default;
};
struct SFixed32
{
  std::int32_t value{};
  bool operator==(SFixed32 const &) const = default;
};
struct Fixed64
{
  std::uint64_t value{};
  bool operator==(Fixed64 const &) const = default;
};
struct SFixed64
{
  std::int64_t value{};
  bool operator==(SFixed64 const &) const = default;
};

template <typename T> struct is_sint_wrapper : std::false_type {};
template <typename T> struct is_sint_wrapper<SInt<T>> : std::true_type {};
template <typename T> inline constexpr bool is_sint_wrapper_v =
    is_sint_wrapper<T>::value;

template <typename T>
inline constexpr bool is_fixed_wrapper_v =
    std::is_same_v<T, Fixed32> || std::is_same_v<T, SFixed32>
    || std::is_same_v<T, Fixed64> || std::is_same_v<T, SFixed64>;

template <typename T>
inline constexpr bool is_wire_wrapper_v =
    is_sint_wrapper_v<T> || is_fixed_wrapper_v<T>;

// Vectors of anything protobuf can pack: integral (incl. bool), enum,
// floating point, and the wire wrappers above.
template <typename T>
inline constexpr bool is_packable_v = is_numeric_v<T> || is_wire_wrapper_v<T>;

// --- unknown-field preservation -----------------------------------------
// A struct may carry a trailing member of type UnknownFields; fields the
// codec does not recognize are captured there and re-emitted verbatim on
// serialization (mirroring protobuf's UnknownFieldSet). Without the member,
// unknown fields are skipped as before. Put the member last so it does not
// shift the field numbers of the real fields.

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
    || is_vector_v<M>;

template <typename M>
bool is_default_value(M const &v)
{
  if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    {
      using R = underlying_or_self_t<M>;
      return static_cast<R>(v) == R{};
    }
  else if constexpr (std::is_floating_point_v<M>)
    return v == 0.0;
  else if constexpr (std::is_same_v<M, std::string>)
    return v.empty();
  else if constexpr (is_sint_wrapper_v<M>)
    return v.value == typename M::value_type{};
  else if constexpr (is_fixed_wrapper_v<M>)
    return v.value == 0;
  else
    return v.empty();  // vectors: empty repeated fields are omitted
}

// --- zigzag (sint32/sint64) helpers, matching protobuf's encoding ---

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
      using R = underlying_or_self_t<U>;
      if constexpr (std::is_signed_v<R>)
        cos.WriteVarint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<R>(e))));
      else
        cos.WriteVarint64(static_cast<std::uint64_t>(static_cast<R>(e)));
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
            static_cast<std::int64_t>(static_cast<R>(val))));
      else
        cos.WriteVarint64(static_cast<std::uint64_t>(static_cast<R>(val)));
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
  else if constexpr (std::is_same_v<M, UnknownFields>)
    {
      for (auto const &uf : val)
        {
          cos.WriteTag((uf.fieldno << 3) | uf.wire_type);
          cos.WriteRaw(uf.raw.data(), static_cast<int>(uf.raw.size()));
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
  else
    {
      std::string payload;
      serialize(payload, val);
      cos.WriteTag((fieldno << 3) | 2);
      cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
      cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
    }
}

template <typename T, std::size_t I>
void serialize_member(google::protobuf::io::CodedOutputStream &cos, T const &v)
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (is_omittable_v<M>)
    {
      if (is_default_value(v.[:r:]))
        return;
    }
  serialize_value(cos, static_cast<std::uint32_t>(I + 1), v.[:r:]);
}

template <typename T, std::size_t... Is>
void serialize_members(google::protobuf::io::CodedOutputStream &cos, T const &v,
                       std::index_sequence<Is...>)
{
  (serialize_member<T, Is>(cos, v), ...);
}

template <typename T>
void serialize(std::string &out, T const &v)
{
  google::protobuf::io::StringOutputStream sos(&out);
  google::protobuf::io::CodedOutputStream cos(&sos);
  serialize_members<T>(cos, v, std::make_index_sequence<member_count_v<T>>{});
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
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_packable_v<U>)
        {
          // packed
          if (wt != 2)
            return false;
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
          if (wt != 2)
            return false;
          U e;
          if (!parse_value(cis, wt, e))
            return false;
          val.push_back(std::move(e));
          return true;
        }
    }
  else if constexpr (is_optional_v<M>)
    {
      typename M::value_type inner{};
      if (!parse_value(cis, wt, inner))
        return false;
      val = std::move(inner);
      return true;
    }
  else
    {
      // embedded message
      if (wt != 2)
        return false;
      std::uint32_t len;
      if (!cis.ReadVarint32(&len))
        return false;
      std::string payload(static_cast<std::size_t>(len), '\0');
      if (!cis.ReadRaw(payload.data(), static_cast<int>(len)))
        return false;
      val = M{};
      return parse(payload, val);
    }
}

template <typename T, std::size_t I>
bool parse_member(google::protobuf::io::CodedInputStream &cis,
                  std::uint32_t fieldno, std::uint32_t wt, T &v)
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if (fieldno != I + 1)
    return false;
  return parse_value(cis, wt, v.[:r:]);
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
        uf.raw.resize(len);
        if (!cis.ReadRaw(uf.raw.data(), static_cast<int>(len)))
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

}  // namespace rpb
