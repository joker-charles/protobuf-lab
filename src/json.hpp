// Proto3 JSON (parse + serialize) for the reflection codec.
//
// Implements the JSON mapping exercised by the official conformance suite:
// lowerCamelCase field names (accepting the original proto names on
// input), int64/uint64 as strings, enums by name (numeric fallback /
// unknown numeric on output), bytes as base64, NaN/Infinity, wrappers as
// bare values, maps as string-keyed objects, oneofs, well-known types
// (Timestamp/Duration/FieldMask/Struct/Value/ListValue/Any), and
// null-accepted-for-any-field semantics.  Unknown fields are ignored on
// parse, matching the conformance runner's expectations.

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

// --- WKT traits --------------------------------------------------------

template <typename T> struct is_duration_type : std::false_type {};
template <> struct is_duration_type<tmm::Duration> : std::true_type {};
template <typename T> inline constexpr bool is_duration_type_v =
    is_duration_type<T>::value;

template <typename T> struct is_timestamp_type : std::false_type {};
template <> struct is_timestamp_type<tmm::Timestamp> : std::true_type {};
template <typename T> inline constexpr bool is_timestamp_type_v =
    is_timestamp_type<T>::value;

template <typename T> struct is_fieldmask_type : std::false_type {};
template <> struct is_fieldmask_type<tmm::FieldMask> : std::true_type {};
template <typename T> inline constexpr bool is_fieldmask_type_v =
    is_fieldmask_type<T>::value;

template <typename T> struct is_struct_type : std::false_type {};
template <> struct is_struct_type<tmm::StructValue> : std::true_type {};
template <typename T> inline constexpr bool is_struct_type_v =
    is_struct_type<T>::value;

template <typename T> struct is_value_type : std::false_type {};
template <> struct is_value_type<tmm::Value> : std::true_type {};
template <typename T> inline constexpr bool is_value_type_v =
    is_value_type<T>::value;

template <typename T> struct is_listvalue_type : std::false_type {};
template <> struct is_listvalue_type<tmm::ListValue> : std::true_type {};
template <typename T> inline constexpr bool is_listvalue_type_v =
    is_listvalue_type<T>::value;

template <typename T> struct is_any_type : std::false_type {};
template <> struct is_any_type<tmm::Any> : std::true_type {};
template <typename T> inline constexpr bool is_any_type_v =
    is_any_type<T>::value;

// Wrapper messages (google.protobuf.*Value) have a single annotated member
// "value" with field number 1; JSON maps them to the bare scalar.  The
// secondary template parameter guards non-class scalars (e.g. the inner
// value of a wrapper) so reflection is never reached for them; the
// member_count_v == 1 check guards opaque classes (std::string reflects to
// zero members) and wire-wrappers (SInt/Fixed, whose lone member is not
// field_annotation-counted).
template <typename T, bool = std::is_class_v<T>>
struct is_wrapper_impl : std::false_type {};
template <typename T>
struct is_wrapper_impl<T, true>
{
  static constexpr bool value = [] {
    if constexpr (member_count_v<T> == 1)
      {
        constexpr meta::info m = member_v<T, 0>;
        if constexpr (annotation_count<m>() == 1)
          return field_number<m, 0>() == 1
                 && meta::identifier_of(m) == "value";
        else
          return false;
      }
    else
      return false;
  }();
};
template <typename T>
inline constexpr bool is_wrapper_type_v = is_wrapper_impl<T>::value;

// A wrapper whose single ``value`` member carries the bytes_type annotation
// (i.e. google.protobuf.BytesValue) parses/times as base64 in JSON.
template <typename T>
inline constexpr bool is_wrapper_inner_bytes_v =
    is_wrapper_type_v<T> && member_is_bytes_ann<member_v<T, 0>>();

// Whether a member type natively represents a JSON null as a real value
// (google.protobuf.Value: null -> null_value) rather than "accepted and
// ignored".  AllFieldAcceptNull sets every other field to null and expects
// the default; a Value field must instead record null_value.  The
// underlying_type-like ::value_type / ::element_type lookups are gated
// behind partial specializations so non-container scalars parse cleanly.
template <typename M, bool = is_optional_v<M>>
struct opt_value_is_value : std::false_type {};
template <typename M>
struct opt_value_is_value<M, true>
{
  static constexpr bool value = is_value_type_v<typename M::value_type>;
};
template <typename M, bool = is_unique_ptr_v<M>>
struct ptr_value_is_value : std::false_type {};
template <typename M>
struct ptr_value_is_value<M, true>
{
  static constexpr bool value = is_value_type_v<typename M::element_type>;
};
template <typename M>
inline constexpr bool null_is_value_v =
    is_value_type_v<M> || opt_value_is_value<M>::value
    || ptr_value_is_value<M>::value;

// --- forward declarations ----------------------------------------------

template <typename T> bool json_parse_value(jvalue const &v, T &out,
                                           bool bytes_ctx = false);
template <typename T> bool json_parse_message(jvalue const &v, T &out);
template <typename T> void json_print_value(std::string &out, T const &v);
template <typename T> void json_print_message(std::string &out, T const &v);

// --- Timestamp / Duration / FieldMask ----------------------------------

// Cleared to true by rpb::json_serialize and set false when a well-known
// type exceeds its serializable range while printing.
inline thread_local bool json_serialize_ok_ = true;

constexpr int days_from_civil(int y, unsigned m, unsigned d)
{
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

constexpr void civil_from_days(int z, int &y, unsigned &m, unsigned &d)
{
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int yy = static_cast<int>(yoe) + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y = yy + (m <= 2);
}

inline bool parse_fraction(std::string_view frac, std::int32_t &nanos)
{
  nanos = 0;
  int count = 0;
  for (char ch : frac)
    {
      if (count >= 9 || ch < '0' || ch > '9')
        return false;
      nanos = nanos * 10 + (ch - '0');
      ++count;
    }
  while (count < 9)
    {
      nanos *= 10;
      ++count;
    }
  return true;
}

inline bool json_parse_timestamp(std::string const &s,
                                 std::int64_t &seconds,
                                 std::int32_t &nanos)
{
  // YYYY-MM-DD'T'HH:MM:SS[.fraction]('Z' | ±HH:MM)
  if (s.size() < 20)
    return false;
  auto dig = [&](std::size_t i, int &v) {
    char ch = s[i];
    if (ch < '0' || ch > '9')
      return false;
    v = ch - '0';
    return true;
  };
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  for (int i = 0; i < 4; ++i)
    {
      int v;
      if (!dig(static_cast<std::size_t>(i), v))
        return false;
      y = y * 10 + v;
    }
  // All remaining date/time fields are two digits: build them by
  // accumulating d1*10 + d2 (dig() only reads a single digit).
  auto read2 = [&](std::size_t a, int &out) {
    int v1 = 0, v2 = 0;
    if (!dig(a, v1) || !dig(a + 1, v2))
      return false;
    out = v1 * 10 + v2;
    return true;
  };
  if (s[4] != '-' || !read2(5, mo) || s[7] != '-' || !read2(8, d)
      || s[10] != 'T' || !read2(11, h) || s[13] != ':' || !read2(14, mi)
      || s[16] != ':' || !read2(17, se))
    return false;
  std::size_t i = 19;
  nanos = 0;
  if (i < s.size() && s[i] == '.')
    {
      ++i;
      std::size_t fstart = i;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        ++i;
      if (i == fstart || i - fstart > 9)
        return false;
      if (!parse_fraction(s.substr(fstart, i - fstart), nanos))
        return false;
    }
  if (i >= s.size())
    return false;
  int offset = 0;
  if (s[i] == 'Z')
    {
      ++i;
    }
  else if (s[i] == '+' || s[i] == '-')
    {
      int sign = s[i] == '-' ? -1 : 1;
      if (i + 6 > s.size() || s[i + 3] != ':')
        return false;
      int oh = 0, om = 0;
      int a = 0, b = 0;
      if (!dig(i + 1, a) || !dig(i + 2, b))
        return false;
      oh = a * 10 + b;
      a = b = 0;
      if (!dig(i + 4, a) || !dig(i + 5, b))
        return false;
      om = a * 10 + b;
      offset = sign * (oh * 60 + om);
      i += 6;
    }
  else
    return false;
  if (i != s.size() || y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1
      || d > 31 || h > 23 || mi > 59 || se > 60)
    return false;
  std::int64_t days = days_from_civil(y, static_cast<unsigned>(mo),
                                      static_cast<unsigned>(d));
  std::int64_t secs = days * 86400 + h * 3600 + mi * 60 + se - offset * 60;
  if (secs < -62135596800LL || secs > 253402300799LL)
    return false;
  seconds = secs;
  return true;
}

inline void json_print_timestamp(std::string &out, std::int64_t seconds,
                                 std::int32_t nanos)
{
  if (seconds < -62135596800LL || seconds > 253402300799LL)
    {
      json_serialize_ok_ = false;
      return;
    }
  int y;
  unsigned mo, d;
  civil_from_days(static_cast<int>(seconds / 86400), y, mo, d);
  std::int64_t rem = seconds % 86400;
  if (rem < 0)
    rem += 86400;
  int h = static_cast<int>(rem / 3600);
  int mi = static_cast<int>((rem % 3600) / 60);
  int se = static_cast<int>(rem % 60);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02d", y, mo, d,
                h, mi, se);
  out += buf;
  if (nanos != 0)
    {
      int digits = 9;
      while (digits > 3 && nanos % 1000 == 0)
        {
          nanos /= 1000;
          digits -= 3;
        }
      std::snprintf(buf, sizeof buf, ".%0*d", digits, nanos);
      out += buf;
    }
  out += 'Z';
}

inline bool json_parse_duration(std::string const &s,
                                std::int64_t &seconds,
                                std::int32_t &nanos)
{
  if (s.empty() || s.back() != 's')
    return false;
  std::string_view body(s.data(), s.size() - 1);
  bool neg = false;
  if (!body.empty() && body[0] == '-')
    {
      neg = true;
      body.remove_prefix(1);
    }
  std::int64_t sec = 0;
  std::size_t i = 0;
  if (i >= body.size() || body[i] < '0' || body[i] > '9')
    return false;
  while (i < body.size() && body[i] >= '0' && body[i] <= '9')
    {
      if (sec > 315576000000LL)
        return false;
      sec = sec * 10 + (body[i] - '0');
      ++i;
    }
  std::int32_t n = 0;
  if (i < body.size())
    {
      if (body[i] != '.')
        return false;
      ++i;
      std::size_t fstart = i;
      while (i < body.size() && body[i] >= '0' && body[i] <= '9')
        ++i;
      if (i == fstart || i - fstart > 9 || !parse_fraction(
                                               body.substr(fstart, i - fstart),
                                               n))
        return false;
    }
  if (i != body.size())
    return false;
  if (neg)
    {
      sec = -sec;
      n = -n;
    }
  if (sec < -315576000000LL || sec > 315576000000LL)
    return false;
  seconds = sec;
  nanos = n;
  return true;
}

inline void json_print_duration(std::string &out, std::int64_t seconds,
                                std::int32_t nanos)
{
  if (seconds < -315576000000LL || seconds > 315576000000LL
      || nanos <= -1000000000 || nanos >= 1000000000)
    {
      json_serialize_ok_ = false;
      return;
    }
  char buf[40];
  if (nanos < 0)
    {
      std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(seconds));
      out += buf;
      int n = -nanos;
      int digits = 9;
      while (digits > 3 && n % 1000 == 0)
        {
          n /= 1000;
          digits -= 3;
        }
      std::snprintf(buf, sizeof buf, ".%0*d", digits, n);
      out += buf;
      out += 's';
      return;
    }
  std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(seconds));
  out += buf;
  if (nanos != 0)
    {
      int n = nanos;
      int digits = 9;
      while (digits > 3 && n % 1000 == 0)
        {
          n /= 1000;
          digits -= 3;
        }
      std::snprintf(buf, sizeof buf, ".%0*d", digits, n);
      out += buf;
    }
  out += 's';
}

// FieldMask: parse camelCase paths into snake_case, print the reverse.
inline bool json_parse_fieldmask(std::string const &s,
                                 std::vector<std::string> &paths)
{
  if (s.empty())
    return true;  // empty fieldmask -> no paths
  std::string cur;
  for (std::size_t i = 0; i < s.size(); ++i)
    {
      char ch = s[i];
      if (ch == ',')
        {
          paths.push_back(std::move(cur));
          cur.clear();
          continue;
        }
      if (ch == '_' || !(std::isalnum(static_cast<unsigned char>(ch))))
        return false;
      if (std::isupper(static_cast<unsigned char>(ch)))
        {
          if (!cur.empty())
            cur.push_back('_');
          cur.push_back(static_cast<char>(
              std::tolower(static_cast<unsigned char>(ch))));
        }
      else if (std::isdigit(static_cast<unsigned char>(ch)) && !cur.empty()
               && !std::isdigit(static_cast<unsigned char>(cur.back())))
        {
          cur.push_back('_');
          cur.push_back(ch);
        }
      else
        cur.push_back(ch);
    }
  paths.push_back(std::move(cur));
  return true;
}

inline void json_print_fieldmask(std::string &out,
                                 std::vector<std::string> const &paths)
{
  bool first = true;
  for (auto const &p : paths)
    {
      if (!first)
        out.push_back(',');
      first = false;
      bool cap = false;
      for (char ch : p)
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
    }
}

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

// --- Any (@type + embedded message) ------------------------------------
//
// An Any's JSON form is an object carrying "@type" plus the embedded
// message's own JSON representation:
//   {"@type": "type.googleapis.com/<full.message.Name>", ...fields...}
// For well-known types (wrappers, Timestamp, Duration, FieldMask, Struct,
// Value, ListValue, Any) the embedded representation's special JSON form is
// carried under a "value" member whose value follows that type's special
// JSON mapping (e.g. Duration -> "1.5s", Int32Value -> 12345, Value ->
// native JSON).  Every well-known form is exactly what json_parse_value /
// json_print_value already produce for the bare type, so the registry hands
// the "value" member (or the whole object, for ordinary messages) straight
// to those dispatchers and serializes the result to raw bytes.

struct json_any_entry
{
  std::string_view full_name;
  bool well_known;                     // embedded form lives under "value"
  bool (*to_bytes)(jvalue const &, std::string &);
  void (*print_any)(tmm::Any const &, std::string &, bool &first);
};

// Return the "value" member of the embedded WKT object, or nullptr.
inline jvalue const *json_any_take_value(jobject const &o)
{
  for (auto const &kv : o)
    if (kv.first == "value")
      return &kv.second;
  return nullptr;
}

// Ordinary message: the whole object (with "@type" an ignored unknown key)
// is the embedded message.
template <typename M>
bool json_any_parse_message(jvalue const &v, std::string &bytes)
{
  M msg;
  if (!json_parse_value(v, msg, false))
    return false;
  rpb::serialize(bytes, msg);
  return true;
}

// --- print forward declarations (defined after json_print_*) -----------
template <typename M> void json_any_print_message(tmm::Any const &,
                                                  std::string &, bool &);
template <typename M> void json_any_print_wkt(tmm::Any const &,
                                              std::string &, bool &);

// Well-known type: parse the special JSON form under the "value" member.
template <typename M>
bool json_any_parse_wkt(jvalue const &v, std::string &bytes)
{
  if (v.kind != jkind::object)
    return false;
  jvalue const *field = json_any_take_value(v.object);
  if (!field)
    return false;          // missing "value"
  M msg;
  if (!json_parse_value(*field, msg, false))
    return false;
  rpb::serialize(bytes, msg);
  return true;
}

inline std::vector<json_any_entry> const &json_any_registry()
{
  // All well-known types needed by the conformance Any suite; the bare
  // json_parse_value/json_print_value dispatch already understands each
  // special form.
  static std::vector<json_any_entry> const reg = {
      {"protobuf_test_messages.proto3.TestAllTypesProto3", false,
       &json_any_parse_message<tmm::TestAllTypesProto3>,
       &json_any_print_message<tmm::TestAllTypesProto3>},
      {"google.protobuf.Duration", true, &json_any_parse_wkt<tmm::Duration>,
       &json_any_print_wkt<tmm::Duration>},
      {"google.protobuf.Timestamp", true, &json_any_parse_wkt<tmm::Timestamp>,
       &json_any_print_wkt<tmm::Timestamp>},
      {"google.protobuf.FieldMask", true, &json_any_parse_wkt<tmm::FieldMask>,
       &json_any_print_wkt<tmm::FieldMask>},
      {"google.protobuf.Struct", true, &json_any_parse_wkt<tmm::StructValue>,
       &json_any_print_wkt<tmm::StructValue>},
      {"google.protobuf.Value", true, &json_any_parse_wkt<tmm::Value>,
       &json_any_print_wkt<tmm::Value>},
      {"google.protobuf.ListValue", true, &json_any_parse_wkt<tmm::ListValue>,
       &json_any_print_wkt<tmm::ListValue>},
      {"google.protobuf.Any", true, &json_any_parse_wkt<tmm::Any>,
       &json_any_print_wkt<tmm::Any>},
      {"google.protobuf.BoolValue", true, &json_any_parse_wkt<tmm::BoolValue>,
       &json_any_print_wkt<tmm::BoolValue>},
      {"google.protobuf.Int32Value", true, &json_any_parse_wkt<tmm::Int32Value>,
       &json_any_print_wkt<tmm::Int32Value>},
      {"google.protobuf.Int64Value", true, &json_any_parse_wkt<tmm::Int64Value>,
       &json_any_print_wkt<tmm::Int64Value>},
      {"google.protobuf.UInt32Value", true,
       &json_any_parse_wkt<tmm::UInt32Value>,
       &json_any_print_wkt<tmm::UInt32Value>},
      {"google.protobuf.UInt64Value", true,
       &json_any_parse_wkt<tmm::UInt64Value>,
       &json_any_print_wkt<tmm::UInt64Value>},
      {"google.protobuf.FloatValue", true, &json_any_parse_wkt<tmm::FloatValue>,
       &json_any_print_wkt<tmm::FloatValue>},
      {"google.protobuf.DoubleValue", true,
       &json_any_parse_wkt<tmm::DoubleValue>,
       &json_any_print_wkt<tmm::DoubleValue>},
      {"google.protobuf.StringValue", true,
       &json_any_parse_wkt<tmm::StringValue>,
       &json_any_print_wkt<tmm::StringValue>},
      {"google.protobuf.BytesValue", true, &json_any_parse_wkt<tmm::BytesValue>,
       &json_any_print_wkt<tmm::BytesValue>},
  };
  return reg;
}

inline bool json_parse_any(jvalue const &v, tmm::Any &out)
{
  if (v.kind != jkind::object)
    return false;
  std::string type_url;
  bool have_type = false;
  for (auto const &kv : v.object)
    {
      if (kv.first == "@type")
        {
          if (kv.second.kind != jkind::string)
            return false;
          type_url = kv.second.string;
          have_type = true;
        }
    }
  if (!have_type)
    return false;  // Missing @type
  std::size_t slash = type_url.rfind('/');
  std::string_view full =
      slash == std::string_view::npos
          ? std::string_view(type_url)
          : std::string_view(type_url).substr(slash + 1);
  for (auto const &entry : json_any_registry())
    {
      if (entry.full_name == full)
        {
          if (!entry.to_bytes(v, out.value))
            return false;
          out.type_url = type_url;
          return true;
        }
    }
  return false;  // unknown @type
}

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

template <typename T>
void json_print_value(std::string &out, T const &v, bool bytes_ctx = false);
inline void json_print_any(std::string &out, tmm::Any const &v);

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

template <typename T>
void json_print_message(std::string &out, T const &v);

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

inline void json_print_any(std::string &out, tmm::Any const &v)
{
  std::size_t slash = v.type_url.rfind('/');
  std::string_view full =
      slash == std::string_view::npos
          ? std::string_view(v.type_url)
          : std::string_view(v.type_url).substr(slash + 1);
  out.push_back('{');
  bool first = true;
  json_print_key_into(out, first, "@type");
  json_print_escaped(out, v.type_url);
  for (auto const &entry : json_any_registry())
    {
      if (entry.full_name == full)
        {
          entry.print_any(v, out, first);
          break;
        }
    }
  out.push_back('}');
}

}  // namespace json_detail

template <typename T>
bool json_parse(std::string_view input, T &out)
{
  return json_detail::json_parse(input, out);
}

template <typename T>
bool json_serialize(std::string &out, T const &v)
{
  json_detail::json_serialize_ok_ = true;
  json_detail::json_print_message(out, v);
  return json_detail::json_serialize_ok_;
}

}  // namespace rpb
