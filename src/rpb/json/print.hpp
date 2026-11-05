// Message -> JSON printing: scalar/value/message/row dispatch, string
// escaping and the Any print helpers.
//
// Part of the rpb header-only JSON codec; include "json.hpp" instead of
// this file directly.
#pragma once

#include "rpb/json/dom.hpp"
#include "rpb/json/fwd.hpp"
#include "rpb/json/wkt.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
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

// --- serialization side -------------------------------------------------

inline void json_print_escaped(std::string &out, std::string_view s)
{
  out.push_back('"');
  for (char const ch : s)
    {
      unsigned char c = static_cast<unsigned char>(ch);
      switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20)
            {
              char buf[8];
              std::snprintf(buf, sizeof buf, "\\u%04x", c);
              out += buf;
            }
          else
            out.push_back(static_cast<char>(c));
        }
    }
  out.push_back('"');
}

// enum value -> proto name (empty when the value is unknown).
template <typename E, std::size_t... Is>
std::string_view jenum_name_impl(E value, std::index_sequence<Is...>)
{
  std::string_view name{};
  (([&] {
     if (value == static_cast<E>([: meta::enumerators_of(^^E)[Is] :]))
       name = meta::identifier_of(meta::enumerators_of(^^E)[Is]);
   }()),
   ...);
  return name;
}

template <typename E>
std::string_view jenum_name(E value)
{
  return jenum_name_impl<E>(value,
                            std::make_index_sequence<
                                meta::enumerators_of(^^E).size()>{});
}

// Print a scalar into a JSON value (without the surrounding string quotes
// for int64/uint64/string which are themselves quoted by json_print_scalar
// where required).
template <typename M>
void json_print_scalar(std::string &out, M const &v, bool bytes_ctx = false)
{
  if constexpr (std::is_same_v<M, bool>)
    {
      out += v ? "true" : "false";
    }
  else if constexpr (std::is_same_v<M, std::string>)
    {
      if (bytes_ctx)
        {
          std::string b64;
          base64_encode(v, b64);
          json_print_escaped(out, b64);
        }
      else
        json_print_escaped(out, v);
    }
  else if constexpr (std::is_enum_v<M>)
    {
      // Enums print as their name when known, else as the numeric value.
      // Unquoted numeric (int32 range) so unknown values stay numbers.
      std::string_view n = jenum_name(v);
      if (!n.empty())
        json_print_escaped(out, n);
      else
        json_print_scalar(out, static_cast<std::int32_t>(v), false);
    }
  else if constexpr (std::is_integral_v<M>)
    {
      char buf[32];
      if constexpr (std::is_same_v<M, std::int64_t>
                    || std::is_same_v<M, std::uint64_t>)
        {
          // 64-bit ints print as strings.
          auto res = std::to_chars(buf, buf + sizeof buf, v);
          json_print_escaped(out, std::string_view(buf, res.ptr - buf));
        }
      else
        {
          auto res = std::to_chars(buf, buf + sizeof buf, v);
          out.append(buf, res.ptr);
        }
    }
  else if constexpr (std::is_floating_point_v<M>)
    {
      if (std::isnan(v))
        out += "\"NaN\"";
      else if (std::isinf(v))
        out += v < 0 ? "\"-Infinity\"" : "\"Infinity\"";
      else
        {
          char buf[64];
          auto res = std::to_chars(buf, buf + sizeof buf, v,
                                   std::chars_format::general);
          out.append(buf, res.ptr);
        }
    }
  else if constexpr (is_sint_wrapper_v<M> || is_fixed_wrapper_v<M>
                     || is_unpacked_wrapper_v<M>)
    json_print_scalar(out, v.value, bytes_ctx);
  else
    static_assert(sizeof(M) == 0, "not a JSON-printable scalar");
}

// --- WKT printing -------------------------------------------------------

// A leaf type that renders via json_print_scalar (including wire wrappers).
template <typename M>
inline constexpr bool is_scalarprintable_v =
    std::is_arithmetic_v<M> || std::is_enum_v<M>
    || std::is_same_v<M, std::string> || is_sint_wrapper_v<M>
    || is_fixed_wrapper_v<M> || is_unpacked_wrapper_v<M>;

// Emit "," separators and printable keys for a message object.
inline void json_print_key_into(std::string &out, bool &first,
                                std::string_view name)
{
  if (!first)
    out.push_back(',');
  first = false;
  json_print_escaped(out, to_json_name(name));
  out.push_back(':');
}

template <typename M>
void json_print_value(std::string &out, M const &v, bool bytes_ctx)
{
  if constexpr (std::is_same_v<M, tmm::Any>)
    {
      json_print_any(out, v);
    }
  else if constexpr (is_wrapper_type_v<M>)
    {
      json_print_value(out, v.value, is_wrapper_inner_bytes_v<M>);
    }
  else if constexpr (is_timestamp_type_v<M>)
    {
      std::string s;
      json_print_timestamp(s, v.seconds, v.nanos);
      json_print_escaped(out, s);
    }
  else if constexpr (is_duration_type_v<M>)
    {
      std::string s;
      json_print_duration(s, v.seconds, v.nanos);
      json_print_escaped(out, s);
    }
  else if constexpr (is_fieldmask_type_v<M>)
    {
      std::string s;
      json_print_fieldmask(s, v.paths);
      json_print_escaped(out, s);
    }
  else if constexpr (is_value_type_v<M>)
    {
      json_print_value(out, v.kind, false);
    }
  else if constexpr (is_struct_type_v<M>)
    {
      // google.protobuf.Struct prints as a native JSON object.
      out.push_back('{');
      bool first = true;
      for (auto const &kv : v.fields)
        {
          if (!first)
            out.push_back(',');
          first = false;
          json_print_escaped(out, kv.first);
          out.push_back(':');
          json_print_value(out, kv.second, false);
        }
      out.push_back('}');
    }
  else if constexpr (is_listvalue_type_v<M>)
    {
      out.push_back('[');
      bool first = true;
      for (auto const &e : v.values)
        {
          if (!first)
            out.push_back(',');
          first = false;
          json_print_value(out, e, false);
        }
      out.push_back(']');
    }
  else if constexpr (is_one_of_v<M>)
    {
      // Value context oneof (e.g. tmm::Value.kind): print the active
      // alternative's own value.  Unset (monostate) prints null.
      if (v.index() == 0)
        out += "null";
      else
        std::visit(
            [&](auto const &alt) {
              if constexpr (std::is_same_v<std::monostate,
                                           std::remove_cvref_t<decltype(alt)>>)
                out += "null";
              else if constexpr (std::is_enum_v<
                                     std::remove_cvref_t<decltype(alt)>>)
                out += "null";  // google.protobuf.Value's null_value
              else
                json_print_value(out, alt, bytes_ctx);
            },
            v);
    }
  else if constexpr (is_bytes_wrapper_v<M>)
    {
      json_print_scalar(out, v.value, true);
    }
  else if constexpr (is_scalarprintable_v<M>)
    {
      json_print_scalar(out, v, bytes_ctx);
    }
  else if constexpr (is_vector_v<M>)
    {
      out.push_back('[');
      bool first = true;
      for (auto const &e : v)
        {
          if (!first)
            out.push_back(',');
          first = false;
          json_print_value(out, e, bytes_ctx);
        }
      out.push_back(']');
    }
  else if constexpr (is_map_v<M>)
    {
      out.push_back('{');
      bool first = true;
      for (auto const &kv : v)
        {
          if (!first)
            out.push_back(',');
          first = false;
          if constexpr (std::is_same_v<typename M::key_type, std::string>)
            json_print_escaped(out, kv.first);
          else if constexpr (std::is_same_v<typename M::key_type, bool>)
            out += kv.first ? "\"true\"" : "\"false\"";
          else
            {
              // Numeric (or SInt/Fixed-wrapped) keys are JSON strings with
              // the bare decimal value inside the quotes.
              auto np = [&](auto const &k) {
                if constexpr (is_sint_wrapper_v<std::remove_cvref_t<decltype(k)>>
                              || is_fixed_wrapper_v<
                                     std::remove_cvref_t<decltype(k)>>)
                  return k.value;
                else
                  return k;
              };
              std::string inner;
              json_print_scalar(inner, np(kv.first), false);
              // json_print_scalar quotes int64/uint64; strip that so the
              // key is a plain string "123".
              out.push_back('"');
              if (inner.size() >= 2 && inner.front() == '"'
                  && inner.back() == '"')
                out.append(inner, 1, inner.size() - 2);
              else
                out += inner;
              out.push_back('"');
            }
          out.push_back(':');
          json_print_value(out, kv.second, bytes_ctx);
        }
      out.push_back('}');
    }
  else if constexpr (is_optional_v<M>)
    {
      if (v.has_value())
        json_print_value(out, *v, bytes_ctx);
      else
        out += "null";
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      if (v)
        json_print_value(out, *v, bytes_ctx);
      else
        out += "null";
    }
  else
    {
      json_print_message(out, v);
    }
}

// --- message printing ---------------------------------------------------

template <typename T, std::size_t R>
void json_print_row(std::string &out, T const &v, bool &first)
{
  constexpr auto row = field_table<T>()[R];
  constexpr meta::info r = member_v<T, row.member>;
  using M = typename [: meta::type_of(r) :];
  constexpr std::string_view fname = member_alt_name<r, row.alt>();
  constexpr bool bytes = member_is_bytes_ann<r>();
  if constexpr (row.alt != 0)
    {
      if (v.[:r:].index() != row.alt)
        return;
      json_print_key_into(out, first, fname);
      json_print_value(out, std::get<row.alt>(v.[:r:]), bytes);
    }
  else
    {
      // proto3 JSON omits unset presence-bearing fields, default-valued
      // scalars, empty containers, and empty plain messages.
      if (member_is_default_or_absent(v.[:r:]))
        return;
      json_print_key_into(out, first, fname);
      json_print_value(out, v.[:r:], bytes);
    }
}

template <typename T, std::size_t... Rs>
void json_print_rows(std::string &out, T const &v, bool &first,
                     std::index_sequence<Rs...>)
{
  ((json_print_row<T, Rs>(out, v, first)), ...);
}

template <typename T>
void json_print_message(std::string &out, T const &v)
{
  out.push_back('{');
  bool first = true;
  json_print_rows(out, v, first,
                  std::make_index_sequence<field_table_size_v<T>>{});
  out.push_back('}');
}

// --- Any printing -------------------------------------------------------

template <typename M>
void json_any_print_message(tmm::Any const &any, std::string &out, bool &first)
{
  M msg;
  if (!parse(any.value, msg)) { out += "null"; return; }
  json_print_rows(out, msg, first,
                  std::make_index_sequence<field_table_size_v<M>>{});
}

template <typename M>
void json_any_print_wkt(tmm::Any const &any, std::string &out, bool &first)
{
  M msg;
  if (!parse(any.value, msg)) { out += "null"; return; }
  json_print_key_into(out, first, "value");
  json_print_value(out, msg, false);
}

}  // namespace json_detail

}  // namespace rpb
