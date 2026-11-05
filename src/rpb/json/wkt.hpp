// Well-known-type traits plus Timestamp / Duration / FieldMask JSON
// parse and print helpers.
//
// Part of the rpb header-only JSON codec; include "json.hpp" instead of
// this file directly.
#pragma once

#include "codec.hpp"
#include "test_messages.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rpb {

namespace json_detail {

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

// A top-level well-known type has its own JSON form rather than a field-keyed
// object: wrappers are a bare scalar, Timestamp/Duration/FieldMask a string,
// Struct/Value/ListValue native JSON, and Any a "@type" object.  rpb::json_parse
// / rpb::json_serialize dispatch to json_parse_value / json_print_value for
// these instead of requiring an object root.
template <typename T>
inline constexpr bool is_wkt_type_v =
    is_wrapper_type_v<T> || is_timestamp_type_v<T> || is_duration_type_v<T>
    || is_fieldmask_type_v<T> || is_struct_type_v<T> || is_value_type_v<T>
    || is_listvalue_type_v<T> || is_any_type_v<T>;

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

inline bool valid_civil_date(int y, int mo, int d)
{
  if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1)
    return false;
  constexpr int days_in_month[13] = {0, 31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
  int max_day = days_in_month[mo];
  // Gregorian leap year: divisible by 4, except centuries not divisible by
  // 400 (2000 is a leap year, 1900 is not).
  if (mo == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
    ++max_day;
  return d <= max_day;
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
      if (i == fstart)
        return false;
      // protobuf accepts any number of fractional digits and keeps only the
      // first nine (nanosecond resolution), silently truncating the rest.
      std::size_t keep = i - fstart;
      if (keep > 9)
        keep = 9;
      if (!parse_fraction(s.substr(fstart, keep), nanos))
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
      // protobuf accepts only a real UTC offset: "+23:59" is the maximum
      // magnitude, "+24:00"/"+99:99" are rejected.
      if (oh > 23 || om > 59)
        return false;
      offset = sign * (oh * 60 + om);
      i += 6;
    }
  else
    return false;
  // Reject impossible calendar dates (2021-02-31, 2019-02-29) and field
  // ranges the earlier parsing did not constrain (second 60, ...); protobuf
  // validates the full civil date before converting to epoch seconds.
  if (i != s.size() || !valid_civil_date(y, mo, d) || h > 23 || mi > 59
      || se > 59)
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
  // Floor division by the seconds-per-day.  ``seconds / 86400`` alone
  // truncates toward zero, so any pre-epoch time with a non-zero time of day
  // (e.g. 1969-12-31T23:59:59Z == -1) would land on the following day; the
  // truncated quotient must borrow a day whenever the remainder is negative.
  std::int64_t days = seconds / 86400;
  std::int64_t rem = seconds % 86400;
  if (rem < 0)
    {
      rem += 86400;
      --days;
    }
  civil_from_days(static_cast<int>(days), y, mo, d);
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
  // protobuf prints a Duration as "<sign><abs seconds>[.<abs fraction>]s":
  // the sign is carried by the whole value, so seconds == 0 with negative
  // nanos (e.g. -0.5s) must still emit '-'.  Formatting sign and absolute
  // value separately avoids losing it through "%lld" of the zero seconds.
  bool neg = seconds < 0 || nanos < 0;
  if (neg)
    out.push_back('-');
  std::uint64_t sec =
      static_cast<std::uint64_t>(neg ? -seconds : seconds);
  int n = neg ? -nanos : nanos;
  char buf[40];
  std::snprintf(buf, sizeof buf, "%llu",
                static_cast<unsigned long long>(sec));
  out += buf;
  if (n != 0)
    {
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

// FieldMask: the JSON form is a comma-separated list of lowerCamelCase paths,
// while the proto form stores snake_case paths.  The conversion follows
// protobuf (FieldMaskUtil and the protobuf C++ JSON parser, which is what the
// conformance suite's own C++ implementation does):
//   * ',' separates paths; empty segments are dropped;
//   * '.' and '_' and digits pass through unchanged (no '_' is invented
//     before a digit, so "foo_3_bar" stays "foo_3_bar");
//   * an uppercase letter becomes '_' + its lowercase form, unless it starts
//     a path or a '.'-separated segment, where it is merely lowercased
//     ("foo.Bar" -> "foo.bar").
inline bool json_parse_fieldmask(std::string const &s,
                                 std::vector<std::string> &paths)
{
  paths.clear();
  if (s.empty())
    return true;  // empty fieldmask -> no paths
  std::string cur;
  bool seg_start = true;  // at start of a path or of a "." segment
  for (char ch : s)
    {
      unsigned char uc = static_cast<unsigned char>(ch);
      if (ch == ',')
        {
          if (!cur.empty())
            paths.push_back(std::move(cur));
          cur.clear();
          seg_start = true;
          continue;
        }
      if (!std::isalnum(uc) && ch != '.' && ch != '_')
        return false;
      if (std::isupper(uc))
        {
          if (!seg_start)
            cur.push_back('_');
          cur.push_back(static_cast<char>(std::tolower(uc)));
          seg_start = false;
        }
      else
        {
          cur.push_back(ch);
          // '_' starts the next word and '.' the next segment; either way an
          // immediately following uppercase letter must not gain a '_'.
          seg_start = (ch == '.' || ch == '_');
        }
    }
  if (!cur.empty())
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
      bool cap_next = false;
      bool seg_start = true;  // first char of a path or of a "." segment
      for (char ch : p)
        {
          if (ch == '_')
            {
              cap_next = true;
              continue;
            }
          if (ch == '.')
            {
              out.push_back('.');
              cap_next = false;
              seg_start = true;
              continue;
            }
          if (cap_next)
            {
              out.push_back(static_cast<char>(
                  std::toupper(static_cast<unsigned char>(ch))));
              cap_next = false;
            }
          else if (seg_start && std::isupper(static_cast<unsigned char>(ch)))
            {
              // protobuf lowercases the first letter of every path segment.
              out.push_back(static_cast<char>(
                  std::tolower(static_cast<unsigned char>(ch))));
            }
          else
            out.push_back(ch);
          seg_start = false;
        }
    }
}

}  // namespace json_detail

}  // namespace rpb
