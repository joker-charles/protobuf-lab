// Proto text format (parse + print) for the reflection codec.
//
// Implements the proto3 text-format subset exercised by the official
// conformance suite: field names (member identifiers; OneOf alternatives
// via [[=rpb::name<...>]] annotations), nested messages, repeated lists,
// maps (sorted on output, last-wins on duplicate keys), oneofs, Any
// bracket syntax, string/bytes escapes (octal/hex/unicode, UTF-8
// validation for string fields), integer range checks, and float
// nan/inf handling.  Unknown fields are ignored on print and rejected on
// parse (TextFormat's default parser behavior).

#pragma once

#include "codec.hpp"
#include "test_messages.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace rpb {

namespace text_detail {

struct cursor
{
  std::string_view s;
  std::size_t pos = 0;
};

inline char peek(cursor &c)
{
  return c.pos < c.s.size() ? c.s[c.pos] : '\0';
}

inline bool at_end(cursor &c)
{
  return c.pos >= c.s.size();
}

// Skips whitespace, ';' separators, and comments ('#' and '//' to EOL).
inline void skip_ws(cursor &c)
{
  for (;;)
    {
      while (c.pos < c.s.size())
        {
          char ch = c.s[c.pos];
          if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
              || ch == ';')
            ++c.pos;
          else
            break;
        }
      if (c.pos < c.s.size() && c.s[c.pos] == '#')
        {
          while (c.pos < c.s.size() && c.s[c.pos] != '\n')
            ++c.pos;
          continue;
        }
      if (c.pos + 1 < c.s.size() && c.s[c.pos] == '/'
          && c.s[c.pos + 1] == '/')
        {
          c.pos += 2;
          while (c.pos < c.s.size() && c.s[c.pos] != '\n')
            ++c.pos;
          continue;
        }
      break;
    }
}

inline bool parse_ident(cursor &c, std::string_view &id)
{
  if (c.pos >= c.s.size())
    return false;
  char ch = c.s[c.pos];
  if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_'))
    return false;
  std::size_t start = c.pos;
  ++c.pos;
  while (c.pos < c.s.size())
    {
      char d = c.s[c.pos];
      if (std::isalnum(static_cast<unsigned char>(d)) || d == '_')
        ++c.pos;
      else
        break;
    }
  id = c.s.substr(start, c.pos - start);
  return true;
}

inline bool expect_char(cursor &c, char want)
{
  skip_ws(c);
  if (peek(c) != want)
    return false;
  ++c.pos;
  return true;
}

inline bool hex_digit(char ch, int &out)
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

inline void append_utf8(std::string &out, std::uint32_t cp)
{
  if (cp < 0x80)
    {
      out.push_back(static_cast<char>(cp));
    }
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

inline bool valid_utf8(std::string_view s)
{
  std::size_t i = 0;
  while (i < s.size())
    {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c < 0x80)
        {
          ++i;
          continue;
        }
      std::uint32_t cp;
      int extra;
      if ((c & 0xE0) == 0xC0)
        {
          cp = c & 0x1F;
          extra = 1;
        }
      else if ((c & 0xF0) == 0xE0)
        {
          cp = c & 0x0F;
          extra = 2;
        }
      else if ((c & 0xF8) == 0xF0)
        {
          cp = c & 0x07;
          extra = 3;
        }
      else
        return false;
      if (i + static_cast<std::size_t>(extra) >= s.size())
        return false;  // not enough continuation bytes left
      for (int k = 1; k <= extra; ++k)
        {
          unsigned char cc = static_cast<unsigned char>(s[i + k]);
          if ((cc & 0xC0) != 0x80)
            return false;
          cp = (cp << 6) | (cc & 0x3F);
        }
      // Overlong encodings, surrogates, and code points beyond U+10FFFF.
      if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800)
          || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF
          || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;
      i += static_cast<std::size_t>(extra) + 1;
    }
  return true;
}

// Parses one quoted string literal (cursor at the opening quote); adjacent
// literals concatenate.  When utf8 is true the final byte sequence must be
// valid UTF-8 (string fields); bytes fields skip that check.
inline bool parse_string_body(cursor &c, std::string &out, bool utf8)
{
  skip_ws(c);
  char quote = peek(c);
  if (quote != '\'' && quote != '"')
    return false;
  ++c.pos;
  for (;;)
    {
      if (at_end(c))
        return false;  // unterminated
      char ch = c.s[c.pos];
      if (ch == quote)
        {
          ++c.pos;
          skip_ws(c);
          char next = peek(c);
          if (next == '\'' || next == '"')
            {
              quote = next;
              ++c.pos;
              continue;
            }
          break;
        }
      if (ch == '\n' || ch == '\r')
        return false;  // raw line feeds are not allowed in literals
      if (ch != '\\')
        {
          out.push_back(ch);
          ++c.pos;
          continue;
        }
      ++c.pos;
      if (at_end(c))
        return false;
      char e = c.s[c.pos];
      switch (e)
        {
        case 'a': out.push_back('\a'); ++c.pos; break;
        case 'b': out.push_back('\b'); ++c.pos; break;
        case 'f': out.push_back('\f'); ++c.pos; break;
        case 'n': out.push_back('\n'); ++c.pos; break;
        case 'r': out.push_back('\r'); ++c.pos; break;
        case 't': out.push_back('\t'); ++c.pos; break;
        case 'v': out.push_back('\v'); ++c.pos; break;
        case '?': out.push_back('?'); ++c.pos; break;
        case '\\': out.push_back('\\'); ++c.pos; break;
        case '\'': out.push_back('\''); ++c.pos; break;
        case '"': out.push_back('"'); ++c.pos; break;
        case 'x':
          {
            ++c.pos;
            unsigned v = 0;
            bool any = false;
            while (c.pos < c.s.size())
              {
                int d;
                if (!hex_digit(c.s[c.pos], d))
                  break;
                any = true;
                v = (v << 4) | static_cast<unsigned>(d);
                ++c.pos;
              }
            if (!any)
              return false;
            out.push_back(static_cast<char>(v & 0xFF));
            break;
          }
        case 'u':
        case 'U':
          {
            int n = e == 'u' ? 4 : 8;
            ++c.pos;
            std::uint32_t cp = 0;
            for (int i = 0; i < n; ++i)
              {
                if (c.pos >= c.s.size())
                  return false;
                int d;
                if (!hex_digit(c.s[c.pos], d))
                  return false;
                cp = (cp << 4) | static_cast<std::uint32_t>(d);
                ++c.pos;
              }
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
              return false;
            append_utf8(out, cp);
            break;
          }
        default:
          if (e >= '0' && e <= '7')
            {
              unsigned v = 0;
              int count = 0;
              while (count < 3 && c.pos < c.s.size()
                     && c.s[c.pos] >= '0' && c.s[c.pos] <= '7')
                {
                  v = (v << 3)
                      | static_cast<unsigned>(c.s[c.pos] - '0');
                  ++c.pos;
                  ++count;
                }
              out.push_back(static_cast<char>(v & 0xFF));
            }
          else
            return false;
        }
    }
  return !utf8 || valid_utf8(out);
}

struct int_token
{
  bool neg = false;
  unsigned __int128 mag = 0;
};

inline bool parse_int_token(cursor &c, int_token &tok)
{
  skip_ws(c);
  bool neg = false;
  if (peek(c) == '-')
    {
      neg = true;
      ++c.pos;
    }
  else if (peek(c) == '+')
    ++c.pos;
  unsigned __int128 v = 0;
  bool any = false;
  while (c.pos < c.s.size())
    {
      char ch = c.s[c.pos];
      if (ch < '0' || ch > '9')
        break;
      any = true;
      v = v * 10 + static_cast<unsigned __int128>(ch - '0');
      ++c.pos;
    }
  if (!any)
    return false;
  tok.neg = neg;
  tok.mag = v;
  return true;
}

template <typename I>
inline bool int_fits(int_token const &t)
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
inline I int_from_token(int_token const &t)
{
  __int128 sv = t.neg ? -static_cast<__int128>(t.mag)
                      : static_cast<__int128>(t.mag);
  return static_cast<I>(sv);
}

inline void read_word(cursor &c, std::string_view &word)
{
  skip_ws(c);
  std::size_t start = c.pos;
  while (c.pos < c.s.size())
    {
      char ch = c.s[c.pos];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
          || ch == '{' || ch == '}' || ch == '[' || ch == ']'
          || ch == ':' || ch == ',' || ch == ';' || ch == '\''
          || ch == '"')
        break;
      ++c.pos;
    }
  word = c.s.substr(start, c.pos - start);
}

inline bool parse_float_token(cursor &c, double &out)
{
  std::string_view tok;
  read_word(c, tok);
  if (tok.empty())
    return false;
  std::string lower;
  lower.reserve(tok.size());
  for (char ch : tok)
    lower.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  if (lower == "nan")
    {
      out = std::numeric_limits<double>::quiet_NaN();
      return true;
    }
  if (lower == "inf" || lower == "infinity")
    {
      out = std::numeric_limits<double>::infinity();
      return true;
    }
  if (lower == "-inf" || lower == "-infinity")
    {
      out = -std::numeric_limits<double>::infinity();
      return true;
    }
  if (lower == "+inf" || lower == "+infinity")
    {
      out = std::numeric_limits<double>::infinity();
      return true;
    }
  auto res = std::from_chars(tok.data(), tok.data() + tok.size(), out,
                             std::chars_format::general);
  if (res.ec == std::errc{} && res.ptr == tok.data() + tok.size())
    return true;
  // Fallback for spellings from_chars rejects (e.g. ".5").
  char *end = nullptr;
  double d = std::strtod(std::string(tok).c_str(), &end);
  if (end && *end == '\0')
    {
      out = d;
      return true;
    }
  return false;
}

inline bool text_parse_bool(cursor &c, bool &out)
{
  std::string_view tok;
  read_word(c, tok);
  std::string lower;
  for (char ch : tok)
    lower.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  if (lower == "true" || lower == "t" || lower == "1")
    {
      out = true;
      return true;
    }
  if (lower == "false" || lower == "f" || lower == "0")
    {
      out = false;
      return true;
    }
  return false;
}

// --- enum name <-> value (compile-time enumerator tables) --------------

template <typename E>
inline constexpr std::size_t enum_count_v =
    meta::enumerators_of(^^E).size();

template <typename E, std::size_t I>
consteval std::string_view enum_ident_at()
{
  return meta::identifier_of(meta::enumerators_of(^^E)[I]);
}

template <typename E, std::size_t I>
consteval E enum_value_at()
{
  return static_cast<E>([: meta::enumerators_of(^^E)[I] :]);
}

template <typename E, std::size_t... Is>
std::string_view enum_name_impl(E value, std::index_sequence<Is...>)
{
  std::string_view name{};
  ((value == enum_value_at<E, Is>()
    && (name = enum_ident_at<E, Is>(), true)),
   ...);
  return name;
}

template <typename E>
std::string_view enum_name(E value)
{
  return enum_name_impl<E>(
      value, std::make_index_sequence<enum_count_v<E>>{});
}

template <typename E, std::size_t... Is>
bool enum_from_name_impl(std::string_view name, E &out,
                         std::index_sequence<Is...>)
{
  bool ok = false;
  ((name == enum_ident_at<E, Is>()
    && (out = enum_value_at<E, Is>(), ok = true)),
   ...);
  return ok;
}

template <typename E>
bool enum_from_name(std::string_view name, E &out)
{
  return enum_from_name_impl<E>(
      name, out, std::make_index_sequence<enum_count_v<E>>{});
}

// --- Any bracket syntax ------------------------------------------------

template <typename T> struct is_any_type : std::false_type {};
template <> struct is_any_type<tmm::Any> : std::true_type {};
template <typename T> inline constexpr bool is_any_type_v =
    is_any_type<T>::value;

template <typename T> bool text_format_parse(std::string_view, T &);
template <typename T> bool text_parse_message_body(cursor &, T &, bool);

struct text_any_entry
{
  std::string_view full_name;
  bool (*parse_body)(cursor &, std::string &);
};

template <typename T>
bool any_parse_from_cursor(cursor &c, std::string &bytes)
{
  T msg;
  if (!text_parse_message_body(c, msg, false))
    return false;
  rpb::serialize(bytes, msg);
  return true;
}

inline std::vector<text_any_entry> const &text_any_registry()
{
  static std::vector<text_any_entry> const reg = {
      {"protobuf_test_messages.proto3.TestAllTypesProto3",
       &any_parse_from_cursor<tmm::TestAllTypesProto3>},
      {"google.protobuf.Duration", &any_parse_from_cursor<tmm::Duration>},
      {"google.protobuf.Timestamp",
       &any_parse_from_cursor<tmm::Timestamp>},
      {"google.protobuf.FieldMask",
       &any_parse_from_cursor<tmm::FieldMask>},
  };
  return reg;
}

// Parses "[type_url] { ... }" (cursor at '[') into type_url + packed bytes.
inline bool text_any_parse(cursor &c, std::string &type_url,
                           std::string &bytes)
{
  ++c.pos;  // '['
  std::size_t start = c.pos;
  while (c.pos < c.s.size() && c.s[c.pos] != ']')
    ++c.pos;
  if (c.pos >= c.s.size())
    return false;
  std::string_view url = c.s.substr(start, c.pos - start);
  ++c.pos;  // ']'
  if (!expect_char(c, '{'))
    return false;
  std::size_t slash = url.rfind('/');
  std::string_view full =
      slash == std::string_view::npos ? url : url.substr(slash + 1);
  for (auto const &entry : text_any_registry())
    {
      if (entry.full_name == full)
        {
          if (!entry.parse_body(c, bytes))
            return false;
          type_url = std::string(url);
          return true;
        }
    }
  return false;  // unknown embedded type
}

// --- value parsing ------------------------------------------------------

template <typename M> bool text_parse_value(cursor &c, M &val,
                                            bool bytes_ctx = false);

template <typename M>
bool text_parse_int_or_enum(cursor &c, M &val)
{
  if constexpr (std::is_enum_v<M>)
    {
      std::size_t save = c.pos;
      std::string_view word;
      if (parse_ident(c, word))
        {
          M tmp;
          if (enum_from_name<M>(word, tmp))
            {
              val = tmp;
              return true;
            }
          c.pos = save;  // not a known name; try a number
        }
      int_token tok;
      if (!parse_int_token(c, tok) || !int_fits<M>(tok))
        return false;
      val = static_cast<M>(int_from_token<M>(tok));
      return true;
    }
  else
    {
      int_token tok;
      if (!parse_int_token(c, tok) || !int_fits<M>(tok))
        return false;
      val = int_from_token<M>(tok);
      return true;
    }
}

template <typename M>
bool text_parse_value(cursor &c, M &val, bool bytes_ctx)
{
  if constexpr (std::is_same_v<M, std::string>)
    return parse_string_body(c, val, !bytes_ctx);
  else if constexpr (is_bytes_wrapper_v<M>)
    {
      std::string tmp;
      if (!parse_string_body(c, tmp, false))
        return false;
      val.value = std::move(tmp);
      return true;
    }
  else if constexpr (std::is_floating_point_v<M>)
    {
      double d;
      if (!parse_float_token(c, d))
        return false;
      val = static_cast<M>(d);
      return true;
    }
  else if constexpr (std::is_same_v<M, bool>)
    return text_parse_bool(c, val);
  else if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    return text_parse_int_or_enum(c, val);
  else if constexpr (is_sint_wrapper_v<M> || is_fixed_wrapper_v<M>)
    {
      using R = std::remove_reference_t<decltype(val.value)>;
      R tmp{};
      if (!text_parse_int_or_enum(c, tmp))
        return false;
      val.value = tmp;
      return true;
    }
  else if constexpr (is_unpacked_wrapper_v<M>)
    return text_parse_value(c, val.value, bytes_ctx);
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      skip_ws(c);
      if (peek(c) == '[')
        {
          ++c.pos;
          for (;;)
            {
              skip_ws(c);
              if (peek(c) == ']')
                {
                  ++c.pos;
                  break;
                }
              U e{};
              if (!text_parse_value(c, e, bytes_ctx))
                return false;
              val.push_back(std::move(e));
              skip_ws(c);
              if (peek(c) == ',')
                {
                  ++c.pos;
                  continue;
                }
              if (peek(c) == ']')
                {
                  ++c.pos;
                  break;
                }
              return false;
            }
          return true;
        }
      U e{};
      if (!text_parse_value(c, e, bytes_ctx))
        return false;
      val.push_back(std::move(e));
      return true;
    }
  else if constexpr (is_map_v<M>)
    {
      using K = typename M::key_type;
      using V = typename M::mapped_type;
      if (!expect_char(c, '{'))
        return false;
      K k{};
      V v{};
      bool saw_key = false;
      for (;;)
        {
          skip_ws(c);
          if (peek(c) == '}')
            {
              ++c.pos;
              break;
            }
          std::string_view fname;
          if (!parse_ident(c, fname))
            return false;
          if (!expect_char(c, ':'))
            return false;
          if (fname == "key")
            {
              if (!text_parse_value(c, k, false))
                return false;
              saw_key = true;
            }
          else if (fname == "value")
            {
              if (!text_parse_value(c, v, bytes_ctx))
                return false;
            }
          else
            return false;
        }
      if (!saw_key)
        return false;
      val[std::move(k)] = std::move(v);
      return true;
    }
  else if constexpr (is_optional_v<M>)
    {
      typename M::value_type tmp{};
      if (!text_parse_value(c, tmp, bytes_ctx))
        return false;
      val = std::move(tmp);
      return true;
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      if (!expect_char(c, '{'))
        return false;
      if (!val)
        val = std::make_unique<typename M::element_type>();
      return text_parse_message_body(c, *val, false);
    }
  else
    {
      if constexpr (std::is_same_v<M, UnknownField>
                    || std::is_same_v<M, UnknownFields>)
        return false;
      else
        {
          (void)bytes_ctx;  // nested messages decide per-member
          if (!expect_char(c, '{'))
            return false;
          return text_parse_message_body(c, val, false);
        }
    }
}

// --- field dispatch -----------------------------------------------------

template <typename T, std::size_t... Rs>
std::size_t find_field_row_impl(std::string_view name,
                                std::index_sequence<Rs...>)
{
  std::size_t found = field_table_size_v<T>;
  ((name
        == member_alt_name<member_v<T, field_table<T>()[Rs].member>,
                           field_table<T>()[Rs].alt>()
    && (found = Rs, true)),
   ...);
  return found;
}

template <typename T>
std::size_t find_field_row(std::string_view name)
{
  return find_field_row_impl<T>(
      name, std::make_index_sequence<field_table_size_v<T>>{});
}

template <typename T, std::size_t R>
bool text_parse_row(cursor &c, T &v)
{
  constexpr auto row = field_table<T>()[R];
  constexpr meta::info r = member_v<T, row.member>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (row.alt != 0)
    {
      using Alt = std::variant_alternative_t<row.alt, M>;
      Alt tmp{};
      if (!text_parse_value(c, tmp, member_is_bytes_ann<r>()))
        return false;
      v.[:r:].template emplace<row.alt>(std::move(tmp));
      return true;
    }
  else
    return text_parse_value(c, v.[:r:], member_is_bytes_ann<r>());
}

template <typename T, std::size_t... Rs>
bool text_parse_row_dispatch(cursor &c, T &v, std::size_t row,
                             std::index_sequence<Rs...>)
{
  bool ok = false;
  ((row == Rs && (ok = text_parse_row<T, Rs>(c, v), true)) || ...);
  return ok;
}

template <typename T>
bool text_parse_message_body(cursor &c, T &msg, bool top)
{
  for (;;)
    {
      skip_ws(c);
      if (at_end(c))
        return top;
      char ch = peek(c);
      if (ch == '}')
        {
          if (top)
            return false;
          ++c.pos;
          return true;
        }
      if (ch == ',')
        {
          ++c.pos;
          continue;
        }
      if constexpr (is_any_type_v<T>)
        {
          if (ch == '[')
            {
              std::string url;
              std::string bytes;
              if (!text_any_parse(c, url, bytes))
                return false;
              msg.type_url = std::move(url);
              msg.value = std::move(bytes);
              continue;
            }
        }
      std::string_view name;
      if (!parse_ident(c, name))
        return false;
      std::size_t row = find_field_row<T>(name);
      if (row == field_table_size_v<T>)
        return false;  // unknown field
      skip_ws(c);
      bool colon = false;
      if (peek(c) == ':')
        {
          colon = true;
          ++c.pos;
          skip_ws(c);
        }
      if (!colon && peek(c) != '{')
        return false;  // scalar fields require ':'
      if (!text_parse_row_dispatch<T>(
              c, msg, row,
              std::make_index_sequence<field_table_size_v<T>>{}))
        return false;
    }
}

template <typename T>
bool text_format_parse(std::string_view input, T &msg)
{
  cursor c{input, 0};
  if (!text_parse_message_body(c, msg, true))
    return false;
  skip_ws(c);
  return at_end(c);
}

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

template <typename T>
bool text_format_parse(std::string_view input, T &msg)
{
  return text_detail::text_format_parse(input, msg);
}

template <typename T>
void text_format_print(std::string &out, T const &v)
{
  text_detail::text_format_print(out, v);
}

}  // namespace rpb
