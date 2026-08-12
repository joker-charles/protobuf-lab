// Protobuf wire-format codec driven by C++26 static reflection (P2996).
//
// Wire primitives come from protobuf's own
// google/protobuf/io/coded_stream.{h,cc} (linked from libprotobuf); the
// reflection decides which members are which fields.  Field number =
// member position + 1 (v1 convention).
//
// Type mapping (v1):
//   std::string                      -> wire type 2 (length-delimited)
//   integral / enum                  -> wire type 0 (varint, sign-extended)
//   float / double                   -> wire type 5 / 1
//   std::vector<T> (numeric T)       -> packed, wire type 2
//   std::vector<T> (string/message)  -> repeated length-delimited
//   std::optional<T>                 -> optional field (skipped when empty)
//   nested struct                    -> embedded message, wire type 2

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
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_numeric_v<U>)
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
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_numeric_v<U>)
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

inline bool skip_field(google::protobuf::io::CodedInputStream &cis,
                       std::uint32_t wt)
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
    case 5:
      return cis.Skip(4);
    default:
      return false;  // groups (3/4) are not supported
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
          if (!skip_field(cis, wt))
            return false;
        }
    }
  return true;
}

}  // namespace rpb
