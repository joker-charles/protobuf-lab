// Text-format printing: indentation/escaping helpers, scalar and
// named-value printers, per-row/per-message emitters, and the
// text_detail::text_format_print entry point.
//
// Part of the rpb header-only codec's text format; include
// "text_format.hpp" instead of this file directly.

#pragma once

#include "codec.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace rpb {

namespace text_detail {

// --- printing ----------------------------------------------------------

inline void print_indent(std::string &out, int indent)
{
  out.append(static_cast<std::size_t>(indent) * 2, ' ');
}

inline void print_escaped_string(std::string &out, std::string_view s)
{
  out.push_back('"');
  for (unsigned char ch : s)
    {
      switch (ch)
        {
        case '\a': out += "\\a"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\v': out += "\\v"; break;
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\'': out += "\\'"; break;
        default:
          // Escape everything outside printable ASCII: the runner's
          // TextFormat tokenizer rejects raw non-UTF-8 bytes in literals,
          // and octal escapes round-trip for both string and bytes fields.
          if (ch < 0x20 || ch >= 0x7F)
            {
              char buf[8];
              std::snprintf(buf, sizeof buf, "\\%03o", ch);
              out += buf;
            }
          else
            out.push_back(static_cast<char>(ch));
        }
    }
  out.push_back('"');
}

template <typename F>
void print_float_value(std::string &out, F v)
{
  if (std::isnan(v))
    {
      out += "nan";
      return;
    }
  if (std::isinf(v))
    {
      out += std::signbit(v) ? "-inf" : "inf";
      return;
    }
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, v);
  out.append(buf, res.ptr);
}

template <typename M> void text_print_scalar(std::string &out, M const &v);

template <typename M>
void text_print_scalar(std::string &out, M const &v)
{
  if constexpr (std::is_same_v<M, std::string>)
    print_escaped_string(out, v);
  else if constexpr (is_bytes_wrapper_v<M>)
    print_escaped_string(out, v.value);
  else if constexpr (std::is_same_v<M, bool>)
    out += v ? "true" : "false";
  else if constexpr (std::is_enum_v<M>)
    {
      std::string_view n = enum_name(v);
      if (!n.empty())
        out.append(n);
      else
        text_print_scalar(out, static_cast<std::underlying_type_t<M>>(v));
    }
  else if constexpr (std::is_integral_v<M>)
    {
      char buf[32];
      auto res = std::to_chars(buf, buf + sizeof buf, v);
      out.append(buf, res.ptr);
    }
  else if constexpr (std::is_floating_point_v<M>)
    print_float_value(out, v);
  else if constexpr (is_sint_wrapper_v<M> || is_fixed_wrapper_v<M>
                     || is_unpacked_wrapper_v<M>)
    text_print_scalar(out, v.value);
  else
    static_assert(sizeof(M) == 0, "not a printable scalar");
}

template <typename T> void text_print_message(std::string &out, T const &v,
                                              int indent);

template <typename M>
void text_print_named(std::string &out, std::string_view name, M const &val,
                      int indent)
{
  if constexpr (std::is_same_v<M, UnknownFields>)
    return;
  else if constexpr (is_optional_v<M>)
    {
      if (val.has_value())
        text_print_named(out, name, *val, indent);
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      if (!val)
        return;
      print_indent(out, indent);
      out.append(name);
      out += " {\n";
      text_print_message(out, *val, indent + 1);
      print_indent(out, indent);
      out += "}\n";
    }
  else if constexpr (is_vector_v<M>)
    {
      for (auto const &e : val)
        text_print_named(out, name, e, indent);
    }
  else if constexpr (is_map_v<M>)
    {
      for (auto const &kv : val)
        {
          print_indent(out, indent);
          out.append(name);
          out += " {\n";
          print_indent(out, indent + 1);
          out += "key: ";
          text_print_scalar(out, kv.first);
          out.push_back('\n');
          if constexpr (is_plain_message_v<typename M::mapped_type>)
            text_print_named(out, "value", kv.second, indent + 1);
          else
            {
              print_indent(out, indent + 1);
              out += "value: ";
              text_print_scalar(out, kv.second);
              out.push_back('\n');
            }
          print_indent(out, indent);
          out += "}\n";
        }
    }
  else if constexpr (is_one_of_v<M>)
    {
      if (val.index() == 0)
        return;
      std::visit(
          [&](auto const &alt) {
            text_print_named(out, name, alt, indent);
          },
          val);
    }
  else if constexpr (is_plain_message_v<M>)
    {
      print_indent(out, indent);
      out.append(name);
      out += " {\n";
      text_print_message(out, val, indent + 1);
      print_indent(out, indent);
      out += "}\n";
    }
  else
    {
      print_indent(out, indent);
      out.append(name);
      out += ": ";
      text_print_scalar(out, val);
      out.push_back('\n');
    }
}

template <typename T, std::uint32_t FNO, std::size_t MI, std::size_t AI>
void text_print_row(std::string &out, T const &v, int indent)
{
  constexpr meta::info r = member_v<T, MI>;
  using M = typename [: meta::type_of(r) :];
  constexpr std::string_view fname = member_alt_name<r, AI>();
  if constexpr (AI == 0)
    {
      if constexpr (is_omittable_v<M>)
        {
          if (is_default_value(v.[:r:]))
            return;
        }
      else if constexpr (is_plain_message_v<M>)
        {
          if (is_empty_message(v.[:r:]))
            return;
        }
      text_print_named(out, fname, v.[:r:], indent);
    }
  else
    {
      if (v.[:r:].index() != AI)
        return;
      text_print_named(out, fname, std::get<AI>(v.[:r:]), indent);
    }
}

template <typename T>
void text_print_message(std::string &out, T const &v, int indent)
{
  template for (constexpr auto e :
                std::define_static_array(field_table<T>()))
    {
      text_print_row<T, e.fieldno, e.member, e.alt>(out, v, indent);
    }
}

template <typename T>
void text_format_print(std::string &out, T const &v)
{
  text_print_message(out, v, 0);
}

}  // namespace text_detail

}  // namespace rpb
