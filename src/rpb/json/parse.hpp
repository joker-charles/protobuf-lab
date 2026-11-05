// JSON -> message parsing: scalar/value/message/row dispatch.
//
// Part of the rpb header-only JSON codec; include "json.hpp" instead of
// this file directly.
#pragma once

#include "rpb/json/dom.hpp"
#include "rpb/json/fwd.hpp"
#include "rpb/json/wkt.hpp"

#include <cmath>
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

namespace json_detail {

// --- integer / float / enum parsing helpers -----------------------------

struct jint
{
  bool neg = false;
  unsigned __int128 mag = 0;
};

inline bool jint_from_text(std::string_view s, jint &out)
{
  bool neg = false;
  if (!s.empty() && s[0] == '-')
    {
      neg = true;
      s.remove_prefix(1);
    }
  else if (!s.empty() && s[0] == '+')
    s.remove_prefix(1);
  if (s.empty())
    return false;
  unsigned __int128 v = 0;
  for (char ch : s)
    {
      if (ch < '0' || ch > '9')
        return false;
      v = v * 10 + static_cast<unsigned __int128>(ch - '0');
    }
  out.neg = neg;
  out.mag = v;
  return true;
}

template <typename I>
inline bool jint_fits(jint const &t)
{
  using U = std::make_unsigned_t<I>;
  constexpr unsigned __int128 max_u =
      static_cast<unsigned __int128>(std::numeric_limits<U>::max());
  if constexpr (std::is_signed_v<I>)
    {
      constexpr unsigned __int128 max_s =
          static_cast<unsigned __int128>(std::numeric_limits<I>::max());
      return t.neg ? t.mag <= max_s + 1 : t.mag <= max_s;
    }
  else
    return !t.neg && t.mag <= max_u;
}

template <typename I>
inline I jint_from(jint const &t)
{
  __int128 sv = t.neg ? -static_cast<__int128>(t.mag)
                      : static_cast<__int128>(t.mag);
  return static_cast<I>(sv);
}

// std::underlying_type_t is only well-formed for enums; delegate behind a
// partial-specialization gate so non-enum scalar/bool/string parse cleanly.
template <typename T, bool = std::is_enum_v<T>>
struct enum_underlying { using type = T; };
template <typename T>
struct enum_underlying<T, true> { using type = std::underlying_type_t<T>; };

// --- main value parse dispatch -----------------------------------------

template <typename T>
bool json_parse_scalar(jvalue const &v, T &out, bool bytes_ctx = false)
{
  if constexpr (std::is_same_v<T, bool>)
    {
      if (v.kind == jkind::boolean)
        {
          out = v.boolean;
          return true;
        }
      return false;
    }
  else if constexpr (std::is_floating_point_v<T>)
    {
      double d;
      bool infinite_allowed = false;
      if (v.kind == jkind::number)
        d = v.number;
      else if (v.kind == jkind::string)
        {
          std::string_view s = v.string;
          if (s == "NaN")
            {
              out = std::numeric_limits<T>::quiet_NaN();
              return true;
            }
          if (s == "Infinity")
            {
              out = std::numeric_limits<T>::infinity();
              return true;
            }
          if (s == "-Infinity")
            {
              out = -std::numeric_limits<T>::infinity();
              return true;
            }
          jint t;
          if (!jint_from_text(s, t))
            return false;
          if (!jint_fits<std::int64_t>(t))
            return false;
          d = static_cast<double>(jint_from<std::int64_t>(t));
        }
      else
        return false;
      // A numeric literal that overflows to +-Infinity (rather than the
      // explicit "Infinity"/"-Infinity" strings) is rejected.
      if (!infinite_allowed && !std::isfinite(d))
        return false;
      if constexpr (std::is_same_v<T, float>)
        {
          if (std::isfinite(d)
              && (d > std::numeric_limits<float>::max()
                  || d < -std::numeric_limits<float>::max()))
            return false;
          out = static_cast<float>(d);
        }
      else
        {
          if (std::isfinite(d)
              && (d > std::numeric_limits<double>::max()
                  || d < -std::numeric_limits<double>::max()))
            return false;
          out = d;
        }
      return true;
    }
  else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    {
      using R = typename enum_underlying<T>::type;
      if (v.kind == jkind::string)
        {
          if constexpr (std::is_enum_v<T>)
            {
              // enum name (case-insensitive) first
              std::string lower;
              for (char ch : v.string)
                lower.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch))));
              template for (constexpr auto en :
                            std::define_static_array(
                                meta::enumerators_of(^^T)))
                {
                  if (lower
                      == [&] {
                           std::string s2;
                           for (char ch : meta::identifier_of(en))
                             s2.push_back(static_cast<char>(
                                 std::tolower(
                                     static_cast<unsigned char>(ch))));
                           return s2;
                         }())
                    {
                      out = static_cast<T>(
                          [: en :]);
                      return true;
                    }
                }
              return false;
            }
          jint t;
          if (!jint_from_text(v.string, t) || !jint_fits<R>(t))
            return false;
          out = static_cast<T>(jint_from<R>(t));
          return true;
        }
      if (v.kind == jkind::number)
        {
          jint t;
          if (jint_from_text(v.num_text, t) && jint_fits<R>(t))
            {
              out = static_cast<T>(jint_from<R>(t));
              return true;
            }
          // float-form numbers are accepted when integral
          double d = v.number;
          double trunc = std::trunc(d);
          if (d != trunc)
            return false;
          if constexpr (std::is_same_v<R, std::int32_t>
                        || std::is_same_v<R, std::uint32_t>)
            {
              if (d < static_cast<double>(std::numeric_limits<R>::min())
                  || d > static_cast<double>(std::numeric_limits<R>::max()))
                return false;
            }
          else if constexpr (std::is_same_v<R, std::int64_t>)
            {
              if (d < -9223372036854775808.0 || d >= 9223372036854775808.0)
                return false;
            }
          else if constexpr (std::is_same_v<R, std::uint64_t>)
            {
              if (d < 0 || d >= 18446744073709551616.0)
                return false;
            }
          else
            {
              if (d < static_cast<double>(std::numeric_limits<R>::min())
                  || d > static_cast<double>(std::numeric_limits<R>::max()))
                return false;
            }
          out = static_cast<T>(static_cast<R>(d));
          return true;
        }
      return false;
    }
  else if constexpr (std::is_same_v<T, std::string>)
    {
      // In bytes context the JSON string is base64.  ``BytesValue.value``
      // already carries the bytes_type annotation so this applies to it and
      // to any other bytes-typed std::string member.
      if (v.kind != jkind::string)
        return false;
      if (bytes_ctx)
        return base64_decode(v.string, out);
      out = v.string;
      return true;
    }
  else
    static_assert(sizeof(T) == 0, "not a JSON scalar");
}

template <typename T>
bool json_parse_value(jvalue const &v, T &out, bool bytes_ctx)
{
  if constexpr (is_wrapper_type_v<T>)
    {
      // The wrapper's bare value is bytes iff its member is bytes-typed.
      return json_parse_value(v, out.value, is_wrapper_inner_bytes_v<T>);
    }
  else if constexpr (is_timestamp_type_v<T>)
    {
      if (v.kind != jkind::string)
        return false;
      return json_parse_timestamp(v.string, out.seconds, out.nanos);
    }
  else if constexpr (is_duration_type_v<T>)
    {
      if (v.kind != jkind::string)
        return false;
      return json_parse_duration(v.string, out.seconds, out.nanos);
    }
  else if constexpr (is_fieldmask_type_v<T>)
    {
      if (v.kind != jkind::string)
        return false;
      return json_parse_fieldmask(v.string, out.paths);
    }
  else if constexpr (is_value_type_v<T>)
    {
      // google.protobuf.Value: native JSON value.
      switch (v.kind)
        {
        case jkind::null:
          out.kind = tmm::NullValue::NULL_VALUE;
          return true;
        case jkind::boolean:
          out.kind = v.boolean;
          return true;
        case jkind::number:
          out.kind = v.number;
          return true;
        case jkind::string:
          out.kind = v.string;
          return true;
        case jkind::array:
          {
            tmm::ListValue lv;
            for (auto const &e : v.array)
              {
                tmm::Value ev;
                if (!json_parse_value(e, ev, false))
                  return false;
                lv.values.push_back(std::move(ev));
              }
            out.kind = std::make_unique<tmm::ListValue>(std::move(lv));
            return true;
          }
        case jkind::object:
          {
            tmm::StructValue sv;
            for (auto const &kv : v.object)
              {
                tmm::Value fv;
                if (!json_parse_value(kv.second, fv, false))
                  return false;
                sv.fields[kv.first] = std::move(fv);
              }
            out.kind = std::make_unique<tmm::StructValue>(std::move(sv));
            return true;
          }
        }
      return false;
    }
  else if constexpr (is_listvalue_type_v<T>)
    {
      if (v.kind != jkind::array)
        return false;
      out.values.clear();
      for (auto const &e : v.array)
        {
          tmm::Value ev;
          if (!json_parse_value(e, ev, false))
            return false;
          out.values.push_back(std::move(ev));
        }
      return true;
    }
  else if constexpr (is_struct_type_v<T>)
    {
      if (v.kind != jkind::object)
        return false;
      out.fields.clear();
      for (auto const &kv : v.object)
        {
          tmm::Value fv;
          if (!json_parse_value(kv.second, fv, false))
            return false;
          out.fields[kv.first] = std::move(fv);
        }
      return true;
    }
  else if constexpr (is_any_type_v<T>)
    {
      return json_parse_any(v, out);
    }
  else if constexpr (is_bytes_wrapper_v<T>)
    {
      // rpb::Bytes: JSON is a base64 string.
      if (v.kind != jkind::string)
        return false;
      return base64_decode(v.string, out.value);
    }
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>
                     || std::is_same_v<T, std::string>
                     || is_sint_wrapper_v<T> || is_fixed_wrapper_v<T>
                     || is_unpacked_wrapper_v<T>)
    {
      if constexpr (is_sint_wrapper_v<T> || is_fixed_wrapper_v<T>
                    || is_unpacked_wrapper_v<T>)
        return json_parse_value(v, out.value, bytes_ctx);
      else
        return json_parse_scalar(v, out, bytes_ctx);
    }
  else if constexpr (is_vector_v<T>)
    {
      if (v.kind != jkind::array)
        return false;
      using U = typename T::value_type;
      out.clear();
      for (auto const &e : v.array)
        {
          if (e.kind == jkind::null)
            return false;
          U elem{};
          if (!json_parse_value(e, elem, bytes_ctx))
            return false;
          out.push_back(std::move(elem));
        }
      return true;
    }
  else if constexpr (is_map_v<T>)
    {
      if (v.kind != jkind::object)
        return false;
      using K = typename T::key_type;
      using V = typename T::mapped_type;
      out.clear();
      for (auto const &kv : v.object)
        {
          if (kv.second.kind == jkind::null)
            return false;
          K key{};
          if constexpr (std::is_same_v<K, std::string>)
            key = kv.first;
          else if constexpr (std::is_same_v<K, bool>)
            {
              // map<bool,..> keys arrive as the strings "true"/"false".
              if (kv.first == "true")
                key = true;
              else if (kv.first == "false")
                key = false;
              else
                return false;
            }
          else
            {
              // Numeric (or SInt/Fixed-wrapped) map keys arrive as strings.
              jvalue keyv;
              keyv.kind = jkind::string;
              keyv.string = kv.first;
              if (!json_parse_value(keyv, key, false))
                return false;
            }
          V val{};
          if (!json_parse_value(kv.second, val, bytes_ctx))
            return false;
          out[std::move(key)] = std::move(val);
        }
      return true;
    }
  else if constexpr (is_optional_v<T>)
    {
      if (v.kind == jkind::null)
        {
          // Null on an optional wrapping google.protobuf.Value must set
          // null_value, not leave it unset (ValueAcceptNull).
          if constexpr (null_is_value_v<T>)
            {
              out = typename T::value_type{};
              return json_parse_value(v, *out, bytes_ctx);
            }
          return true;  // null leaves it unset
        }
      typename T::value_type inner{};
      if (!json_parse_value(v, inner, bytes_ctx))
        return false;
      out = std::move(inner);
      return true;
    }
  else if constexpr (is_unique_ptr_v<T>)
    {
      if (v.kind == jkind::null)
        {
          // Null on a unique_ptr wrapping google.protobuf.Value must set
          // null_value (ValueAcceptNull).
          if constexpr (null_is_value_v<T>)
            {
              if (!out)
                out = std::make_unique<typename T::element_type>();
              return json_parse_value(v, *out, bytes_ctx);
            }
          return true;  // null leaves it unset
        }
      if (!out)
        out = std::make_unique<typename T::element_type>();
      return json_parse_value(v, *out, bytes_ctx);
    }
  else
    {
      // message object
      if (v.kind == jkind::null)
        return true;
      if (v.kind != jkind::object)
        return false;
      return json_parse_message(v, out);
    }
}

template <typename T, std::size_t R>
bool json_parse_row(jvalue const &v, T &out)
{
  constexpr auto row = field_table<T>()[R];
  constexpr meta::info r = member_v<T, row.member>;
  using M = typename [: meta::type_of(r) :];
  constexpr bool bytes = member_is_bytes_ann<r>();
  if constexpr (row.alt != 0)
    {
      // Oneof alternatives: null never *sets* a oneof (OneofFieldNullFirst /
      // NullSecond: null is accepted but does not count as present).
      if (v.kind == jkind::null)
        return true;
      using Alt = std::variant_alternative_t<row.alt, M>;
      Alt tmp{};
      if (!json_parse_value(v, tmp, bytes))
        return false;
      out.[:r:].template emplace<row.alt>(std::move(tmp));
      return true;
    }
  else
    {
      // Ordinary members: null is accepted and ignored, except for fields
      // that natively absorb null (google.protobuf.Value -> null_value).
      if (v.kind == jkind::null && !null_is_value_v<M>)
        return true;
      return json_parse_value(v, out.[:r:], bytes);
    }
}

template <typename T, std::size_t... Rs>
bool json_parse_row_dispatch(jvalue const &v, T &out, std::size_t row,
                             std::index_sequence<Rs...>)
{
  bool ok = false;
  ((row == Rs && (ok = json_parse_row<T, Rs>(v, out), true)) || ...);
  return ok;
}

template <typename T>
bool json_parse_message(jvalue const &v, T &out)
{
  static constexpr auto ft = field_table<T>();
  std::vector<std::size_t> set_oneof_members;
  for (auto const &kv : v.object)
    {
      std::size_t row = find_json_row<T>(kv.first);
      if (row == field_table_size_v<T>)
        row = find_json_row_camel<T>(kv.first);
      if (row == field_table_size_v<T>)
        continue;  // unknown fields are ignored
      // Two different alternatives of the same oneof in one object is a
      // duplicate (OneofFieldDuplicate): fail.  A null oneof alternative
      // does not *set* the oneof (OneofFieldNullFirst/Second), so it must
      // not count toward the duplicate check.
      if (ft[row].alt != 0 && kv.second.kind != jkind::null)
        {
          std::size_t m = ft[row].member;
          for (std::size_t x : set_oneof_members)
            if (x == m)
              return false;
          set_oneof_members.push_back(m);
        }
      if (!json_parse_row_dispatch<T>(
              kv.second, out, row,
              std::make_index_sequence<field_table_size_v<T>>{}))
        return false;
    }
  return true;
}

template <typename T>
bool json_parse(std::string_view input, T &out)
{
  jvalue v;
  if (!json_parse_value(input, v))
    return false;
  if (v.kind == jkind::null)
    return false;  // top-level null rejected
  if (v.kind != jkind::object)
    return false;
  return json_parse_message(v, out);
}

}  // namespace json_detail

}  // namespace rpb
