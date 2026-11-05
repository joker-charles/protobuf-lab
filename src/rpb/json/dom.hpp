// Minimal JSON DOM, recursive-descent text parser, base64 and
// field-name helpers used by the rpb JSON codec.
//
// Part of the rpb header-only JSON codec; include "json.hpp" instead of
// this file directly.
#pragma once

#include "codec.hpp"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace rpb {

namespace json_detail {

// --- minimal JSON DOM --------------------------------------------------

struct jvalue;
using jarray = std::vector<jvalue>;
using jmember = std::pair<std::string, jvalue>;
using jobject = std::vector<jmember>;

enum class jkind
{
  null,
  boolean,
  number,
  string,
  array,
  object,
};

struct jvalue
{
  jkind kind = jkind::null;
  bool boolean = false;
  double number = 0;
  std::string num_text;  // raw number text for exact integer parsing
  std::string string;
  jarray array;
  jobject object;
};

struct jcursor
{
  std::string_view s;
  std::size_t pos = 0;
};

inline char jpeek(jcursor &c)
{
  return c.pos < c.s.size() ? c.s[c.pos] : '\0';
}

inline void jskip_ws(jcursor &c)
{
  while (c.pos < c.s.size())
    {
      char ch = c.s[c.pos];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
        ++c.pos;
      else
        break;
    }
}

inline bool jhex(char ch, int &out)
{
  if (ch >= '0' && ch <= '9')
    {
      out = ch - '0';
      return true;
    }
  if (ch >= 'a' && ch <= 'f')
    {
      out = ch - 'a' + 10;
      return true;
    }
  if (ch >= 'A' && ch <= 'F')
    {
      out = ch - 'A' + 10;
      return true;
    }
  return false;
}

inline void jappend_utf8(std::string &out, std::uint32_t cp)
{
  if (cp < 0x80)
    out.push_back(static_cast<char>(cp));
  else if (cp < 0x800)
    {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  else if (cp < 0x10000)
    {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  else
    {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline bool jparse_string(jcursor &c, std::string &out)
{
  if (jpeek(c) != '"')
    return false;
  ++c.pos;
  for (;;)
    {
      if (c.pos >= c.s.size())
        return false;
      unsigned char ch = static_cast<unsigned char>(c.s[c.pos]);
      if (ch == '"')
        {
          ++c.pos;
          return true;
        }
      if (ch < 0x20)
        return false;  // control characters must be escaped
      if (ch != '\\')
        {
          out.push_back(static_cast<char>(ch));
          ++c.pos;
          continue;
        }
      ++c.pos;
      if (c.pos >= c.s.size())
        return false;
      char e = c.s[c.pos];
      switch (e)
        {
        case '"': out.push_back('"'); ++c.pos; break;
        case '\\': out.push_back('\\'); ++c.pos; break;
        case '/': out.push_back('/'); ++c.pos; break;
        case 'b': out.push_back('\b'); ++c.pos; break;
        case 'f': out.push_back('\f'); ++c.pos; break;
        case 'n': out.push_back('\n'); ++c.pos; break;
        case 'r': out.push_back('\r'); ++c.pos; break;
        case 't': out.push_back('\t'); ++c.pos; break;
        case 'u':
          {
            ++c.pos;
            std::uint32_t cp = 0;
            for (int i = 0; i < 4; ++i)
              {
                if (c.pos >= c.s.size())
                  return false;
                int d;
                if (!jhex(c.s[c.pos], d))
                  return false;
                cp = (cp << 4) | static_cast<std::uint32_t>(d);
                ++c.pos;
              }
            if (cp >= 0xD800 && cp <= 0xDBFF)
              {
                // high surrogate: must be followed by \uDC00-\uDFFF
                if (c.pos + 1 >= c.s.size() || c.s[c.pos] != '\\'
                    || c.s[c.pos + 1] != 'u')
                  return false;
                c.pos += 2;
                std::uint32_t lo = 0;
                for (int i = 0; i < 4; ++i)
                  {
                    if (c.pos >= c.s.size())
                      return false;
                    int d;
                    if (!jhex(c.s[c.pos], d))
                      return false;
                    lo = (lo << 4) | static_cast<std::uint32_t>(d);
                    ++c.pos;
                  }
                if (lo < 0xDC00 || lo > 0xDFFF)
                  return false;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
              return false;  // unpaired low surrogate
            jappend_utf8(out, cp);
            break;
          }
        default:
          return false;
        }
    }
}

inline bool jparse_number(jcursor &c, jvalue &out)
{
  std::size_t start = c.pos;
  if (jpeek(c) == '-')
    ++c.pos;
  if (c.pos >= c.s.size() || jpeek(c) < '0' || jpeek(c) > '9')
    return false;
  if (jpeek(c) == '0')
    ++c.pos;
  else
    {
      while (c.pos < c.s.size() && jpeek(c) >= '0' && jpeek(c) <= '9')
        ++c.pos;
    }
  if (c.pos < c.s.size() && jpeek(c) == '.')
    {
      ++c.pos;
      if (c.pos >= c.s.size() || jpeek(c) < '0' || jpeek(c) > '9')
        return false;
      while (c.pos < c.s.size() && jpeek(c) >= '0' && jpeek(c) <= '9')
        ++c.pos;
    }
  if (c.pos < c.s.size() && (jpeek(c) == 'e' || jpeek(c) == 'E'))
    {
      ++c.pos;
      if (c.pos < c.s.size() && (jpeek(c) == '+' || jpeek(c) == '-'))
        ++c.pos;
      if (c.pos >= c.s.size() || jpeek(c) < '0' || jpeek(c) > '9')
        return false;
      while (c.pos < c.s.size() && jpeek(c) >= '0' && jpeek(c) <= '9')
        ++c.pos;
    }
  out.num_text = std::string(c.s.substr(start, c.pos - start));
  auto res = std::from_chars(out.num_text.data(),
                             out.num_text.data() + out.num_text.size(),
                             out.number, std::chars_format::general);
  if (res.ec != std::errc{})
    {
      char *end = nullptr;
      out.number = std::strtod(out.num_text.c_str(), &end);
      if (!end || *end != '\0')
        return false;
    }
  out.kind = jkind::number;
  return true;
}

inline bool jparse_value(jcursor &c, jvalue &out);

inline bool jparse_array(jcursor &c, jvalue &out)
{
  ++c.pos;  // '['
  out.kind = jkind::array;
  bool first = true;
  for (;;)
    {
      jskip_ws(c);
      if (jpeek(c) == ']')
        {
          if (!first)   // trailing comma was seen -> invalid
            return false;
          ++c.pos;
          return true;
        }
      if (!first)
        {
          if (jpeek(c) != ',')
            return false;
          ++c.pos;
          jskip_ws(c);
        }
      if (jpeek(c) == ']')
        return false;  // trailing comma
      jvalue e;
      if (!jparse_value(c, e))
        return false;
      out.array.push_back(std::move(e));
      first = false;
      jskip_ws(c);
      if (jpeek(c) == ',')
        continue;
      if (jpeek(c) == ']')
        {
          ++c.pos;
          return true;
        }
      return false;
    }
}

inline bool jparse_object(jcursor &c, jvalue &out)
{
  ++c.pos;  // '{'
  out.kind = jkind::object;
  bool first = true;
  for (;;)
    {
      jskip_ws(c);
      if (jpeek(c) == '}')
        {
          if (!first)
            return false;  // trailing comma
          ++c.pos;
          return true;
        }
      if (!first)
        {
          if (jpeek(c) != ',')
            return false;
          ++c.pos;
          jskip_ws(c);
          if (jpeek(c) == '}')
            return false;  // trailing comma
        }
      std::string key;
      if (!jparse_string(c, key))
        return false;
      jskip_ws(c);
      if (jpeek(c) != ':')
        return false;
      ++c.pos;
      jvalue v;
      if (!jparse_value(c, v))
        return false;
      out.object.emplace_back(std::move(key), std::move(v));
      first = false;
      jskip_ws(c);
      if (jpeek(c) == ',')
        continue;
      if (jpeek(c) == '}')
        {
          ++c.pos;
          return true;
        }
      return false;
    }
}

inline bool jparse_value(jcursor &c, jvalue &out)
{
  jskip_ws(c);
  char ch = jpeek(c);
  if (ch == 'n')
    {
      if (c.s.substr(c.pos, 4) == "null")
        {
          c.pos += 4;
          out.kind = jkind::null;
          return true;
        }
      return false;
    }
  if (ch == 't')
    {
      if (c.s.substr(c.pos, 4) == "true")
        {
          c.pos += 4;
          out.kind = jkind::boolean;
          out.boolean = true;
          return true;
        }
      return false;
    }
  if (ch == 'f')
    {
      if (c.s.substr(c.pos, 5) == "false")
        {
          c.pos += 5;
          out.kind = jkind::boolean;
          out.boolean = false;
          return true;
        }
      return false;
    }
  if (ch == '"')
    {
      out.kind = jkind::string;
      return jparse_string(c, out.string);
    }
  if (ch == '[')
    return jparse_array(c, out);
  if (ch == '{')
    return jparse_object(c, out);
  if (ch == '-' || (ch >= '0' && ch <= '9'))
    return jparse_number(c, out);
  return false;
}

inline bool json_parse_value(std::string_view input, jvalue &out)
{
  jcursor c{input, 0};
  if (!jparse_value(c, out))
    return false;
  jskip_ws(c);
  return c.pos == c.s.size();
}

// --- base64 ------------------------------------------------------------

inline char b64_char(unsigned v)
{
  static char const table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  return table[v & 63];
}

inline void base64_encode(std::string const &in, std::string &out)
{
  std::size_t i = 0;
  while (i + 2 < in.size())
    {
      unsigned v = (static_cast<unsigned char>(in[i]) << 16)
                   | (static_cast<unsigned char>(in[i + 1]) << 8)
                   | static_cast<unsigned char>(in[i + 2]);
      out.push_back(b64_char(v >> 18));
      out.push_back(b64_char(v >> 12));
      out.push_back(b64_char(v >> 6));
      out.push_back(b64_char(v));
      i += 3;
    }
  if (i + 1 == in.size())
    {
      unsigned v = static_cast<unsigned char>(in[i]) << 16;
      out.push_back(b64_char(v >> 18));
      out.push_back(b64_char(v >> 12));
      out += "==";
    }
  else if (i + 2 == in.size())
    {
      unsigned v = (static_cast<unsigned char>(in[i]) << 16)
                   | (static_cast<unsigned char>(in[i + 1]) << 8);
      out.push_back(b64_char(v >> 18));
      out.push_back(b64_char(v >> 12));
      out.push_back(b64_char(v >> 6));
      out.push_back('=');
    }
}

inline int b64_val(char ch)
{
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+' || ch == '-') return 62;
  if (ch == '/' || ch == '_') return 63;
  return -1;
}

inline bool base64_decode(std::string_view in, std::string &out)
{
  std::size_t i = 0;
  int pad = 0;
  for (;;)
    {
      if (i >= in.size())
        break;
      int a = b64_val(in[i]);
      if (a < 0)
        return false;
      if (i + 1 >= in.size())
        return false;
      int b = b64_val(in[i + 1]);
      if (b < 0)
        return false;
      unsigned v = (static_cast<unsigned>(a) << 18)
                   | (static_cast<unsigned>(b) << 12);
      i += 2;
      if (i < in.size() && in[i] == '=')
        {
          pad = 1;
          ++i;
          if (i < in.size() && in[i] == '=')
            {
              pad = 2;
              ++i;
            }
          if (i != in.size())
            return false;
          out.push_back(static_cast<char>(v >> 16));
          return true;
        }
      if (i >= in.size())
        return false;
      int c = b64_val(in[i]);
      if (c < 0)
        return false;
      v |= static_cast<unsigned>(c) << 6;
      ++i;
      if (i < in.size() && in[i] == '=')
        {
          pad = 1;
          ++i;
          if (i != in.size())
            return false;
          out.push_back(static_cast<char>(v >> 16));
          out.push_back(static_cast<char>((v >> 8) & 0xFF));
          return true;
        }
      if (i >= in.size())
        return false;
      int d = b64_val(in[i]);
      if (d < 0)
        return false;
      v |= static_cast<unsigned>(d);
      ++i;
      out.push_back(static_cast<char>(v >> 16));
      out.push_back(static_cast<char>((v >> 8) & 0xFF));
      out.push_back(static_cast<char>(v & 0xFF));
    }
  (void)pad;
  return true;
}

// --- field name helpers ------------------------------------------------

inline std::string to_json_name(std::string_view name)
{
  std::string out;
  bool cap = false;
  for (char ch : name)
    {
      if (ch == '_')
        {
          cap = true;
          continue;
        }
      if (cap)
        {
          out.push_back(static_cast<char>(
              std::toupper(static_cast<unsigned char>(ch))));
          cap = false;
        }
      else
        out.push_back(ch);
    }
  return out;
}

template <typename T, std::size_t R>
consteval std::string_view row_proto_name()
{
  constexpr auto row = field_table<T>()[R];
  return member_alt_name<member_v<T, row.member>, row.alt>();
}

template <typename T, std::size_t... Rs>
std::size_t find_json_row_impl(std::string_view name,
                               std::index_sequence<Rs...>)
{
  std::size_t found = field_table_size_v<T>;
  ((name == row_proto_name<T, Rs>() && (found = Rs, true)),
   ...);
  return found;
}

template <typename T>
std::size_t find_json_row(std::string_view name)
{
  return find_json_row_impl<T>(
      name, std::make_index_sequence<field_table_size_v<T>>{});
}

template <typename T, std::size_t... Rs>
std::size_t find_json_row_camel_impl(std::string_view name,
                                     std::index_sequence<Rs...>)
{
  std::size_t found = field_table_size_v<T>;
  ((name == to_json_name(row_proto_name<T, Rs>()) && (found = Rs, true)),
   ...);
  return found;
}

template <typename T>
std::size_t find_json_row_camel(std::string_view name)
{
  return find_json_row_camel_impl<T>(
      name, std::make_index_sequence<field_table_size_v<T>>{});
}

}  // namespace json_detail

}  // namespace rpb
