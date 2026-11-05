// Text-format lexical layer: cursor, whitespace/comment skipping,
// identifiers, quoted string literals (octal/hex/unicode escapes, adjacent
// literal concatenation, UTF-8 validation), integer literals
// (decimal/hex/octal, range checked), float and bool tokens, and the
// compile-time enum name <-> value tables.
//
// Part of the rpb header-only codec's text format; include
// "text_format.hpp" instead of this file directly.

#pragma once

#include "rpb/annotations.hpp"

#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

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
  // Leading whitespace is skipped here so callers may invoke this with a
  // pending separator (map entries: `value: BAZ`).
  skip_ws(c);
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
// valid UTF-8 (string fields); bytes fields skip that check.  The official
// text-format conformance suite requires string fields to reject invalid
// UTF-8 (StringFieldBadUTF8Octal/Hex, REQUIRED), even though protobuf's own
// TextFormat parser is lenient there; keeping the check means a string
// member holding invalid UTF-8 does not round-trip through print -> parse,
// which matches protobuf's printer/parser pair.
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
            // TextFormat's \x escape consumes at most two hex digits (so
            // "\x4142" is 'A' followed by a literal "42", not 0x42).
            int count = 0;
            while (count < 2 && c.pos < c.s.size())
              {
                int d;
                if (!hex_digit(c.s[c.pos], d))
                  break;
                any = true;
                v = (v << 4) | static_cast<unsigned>(d);
                ++c.pos;
                ++count;
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

// Parses an integer literal.  TextFormat follows C literal syntax for the
// base: "0x"/"0X" prefix selects hexadecimal, a bare leading '0' selects
// octal (so "010" is 8 and "08"/"09" are invalid), everything else is
// decimal; "0" itself is the valid octal spelling of zero.  A leading '+'
// is rejected (protobuf's parser does not accept it).  The magnitude is
// accumulated in unsigned __int128 with an explicit overflow check, so an
// over-long literal is rejected instead of wrapping modulo 2^128.
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
    return false;  // TextFormat has no leading-plus integer spelling
  int base = 10;
  bool any = false;
  if (peek(c) == '0')
    {
      if (c.pos + 1 < c.s.size()
          && (c.s[c.pos + 1] == 'x' || c.s[c.pos + 1] == 'X'))
        {
          base = 16;
          c.pos += 2;
        }
      else
        {
          base = 8;
          ++c.pos;      // consume the leading '0' (counts as a digit)
          any = true;
        }
    }
  constexpr unsigned __int128 max_mag = ~static_cast<unsigned __int128>(0);
  unsigned __int128 v = 0;
  while (c.pos < c.s.size())
    {
      char ch = c.s[c.pos];
      int d;
      if (base == 16)
        {
          if (!hex_digit(ch, d))
            break;
        }
      else if (ch >= '0' && ch <= '9')
        d = ch - '0';
      else
        break;
      if (d >= base)
        return false;  // e.g. "08": 8 is not an octal digit
      // Reject before multiplying so the value can never wrap: the largest
      // admissible value is (max_mag - d) / base.
      if (v > (max_mag - static_cast<unsigned __int128>(d))
                  / static_cast<unsigned __int128>(base))
        return false;  // integer literal out of range
      v = v * static_cast<unsigned>(base) + static_cast<unsigned>(d);
      any = true;
      ++c.pos;
    }
  if (!any)
    return false;  // "0x" with no digits
  tok.neg = neg;
  tok.mag = v;
  return true;
}

// Integer type an enum range-checks against: an enum's own type is never
// `is_signed`, so it must be mapped to its fixed underlying type first.
// (A tag-dispatch trait instead of std::conditional_t: both arguments of
// conditional_t are instantiated, and underlying_type_t<int> is ill-formed.)
template <typename I, bool = std::is_enum_v<I>> struct int_underlying
{
  using type = I;
};
template <typename I> struct int_underlying<I, true>
{
  using type = std::underlying_type_t<I>;
};
template <typename I> using int_underlying_t = typename int_underlying<I>::type;

template <typename I>
inline bool int_fits(int_token const &t)
{
  // Enums range-check against their underlying integer type: a scoped enum
  // is never `is_signed` in the type system, so using I directly would take
  // the unsigned branch and reject every negative enumerator/numeric value.
  using UI = int_underlying_t<I>;
  using U = std::make_unsigned_t<UI>;
  constexpr unsigned __int128 max_u =
      static_cast<unsigned __int128>(std::numeric_limits<U>::max());
  if constexpr (std::is_signed_v<UI>)
    {
      constexpr unsigned __int128 max_s =
          static_cast<unsigned __int128>(std::numeric_limits<UI>::max());
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
  // Range-checked by int_fits() first; enums convert through their fixed
  // underlying type so the value is always representable.
  if constexpr (std::is_enum_v<I>)
    return static_cast<I>(static_cast<int_underlying_t<I>>(sv));
  else
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
  // TextFormat allows an optional 'f'/'F' suffix on float literals
  // ("1.5f", "1f"); strip it before numeric parsing.  "inf"/"-inf" were
  // handled above, so a trailing 'f' here is always the suffix.
  std::string_view num = tok;
  bool had_suffix = false;
  if (num.size() > 1 && (num.back() == 'f' || num.back() == 'F'))
    {
      num.remove_suffix(1);
      had_suffix = true;
    }
  if (had_suffix)
    {
      // The suffix is only valid on a decimal literal: "nanf"/"inff" are
      // rejected by protobuf's tokenizer ("e"/"E" is the only other letter
      // a float token may contain, as the exponent marker).
      for (char ch : num)
        if (std::isalpha(static_cast<unsigned char>(ch)) && ch != 'e'
            && ch != 'E')
          return false;
    }
  // No leading '+' (rejected by protobuf's parser) and no hex floats:
  // strtod's fallback below would otherwise accept C99 spellings such as
  // "0x1p3", which TextFormat does not have.
  if (num.empty() || num.front() == '+')
    return false;
  if (num.find_first_of("xX") != std::string_view::npos)
    return false;
  auto res = std::from_chars(num.data(), num.data() + num.size(), out,
                             std::chars_format::general);
  if (res.ec == std::errc{} && res.ptr == num.data() + num.size())
    return true;
  // Fallback for spellings from_chars rejects (e.g. ".5").  Keep the token
  // in a named local: strtod points `end` into its buffer, and dereferencing
  // that pointer after a temporary dies is stack-use-after-scope (ASan).
  std::string tmp(num);
  char *end = nullptr;
  double d = std::strtod(tmp.c_str(), &end);
  if (end == tmp.c_str() + tmp.size())
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

}  // namespace text_detail

}  // namespace rpb
