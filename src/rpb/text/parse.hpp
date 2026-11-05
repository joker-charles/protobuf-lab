// Text-format parsing: scalar/container/message value parsers, field-row
// dispatch with duplicate-field rejection, message-body parsing, and the
// text_detail::text_format_parse entry point.
//
// Part of the rpb header-only codec's text format; include
// "text_format.hpp" instead of this file directly.

#pragma once

#include "codec.hpp"

#include "rpb/text/any.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace rpb {

namespace text_detail {

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
          skip_ws(c);
          if (peek(c) == ']')
            {
              ++c.pos;  // empty list
              return true;
            }
          for (;;)
            {
              U e{};
              if (!text_parse_value(c, e, bytes_ctx))
                return false;
              val.push_back(std::move(e));
              skip_ws(c);
              if (peek(c) == ']')
                {
                  ++c.pos;
                  break;
                }
              if (peek(c) != ',')
                return false;
              ++c.pos;
              // No "']'" shortcut after the comma: a trailing comma means an
              // element is still required, so "[1,2,]" is rejected.
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
          // The colon is optional before a brace: protobuf (and our own
          // printer) spell a message-valued entry as `value { ... }`.
          skip_ws(c);
          bool colon = false;
          if (peek(c) == ':')
            {
              colon = true;
              ++c.pos;
              skip_ws(c);
            }
          if (!colon && peek(c) != '{' && peek(c) != '[')
            return false;  // scalar values still require ':'
          if (fname == "key")
            {
              if (!text_parse_value(c, k, false))
                return false;
            }
          else if (fname == "value")
            {
              if (!text_parse_value(c, v, bytes_ctx))
                return false;
            }
          else
            return false;
        }
      // A missing key is not an error: the entry's key keeps its default
      // value (TextFormat inserts `std::map{}`-keyed default entries).
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

// A row is repeatable iff its member is a repeated field (vector) or a map:
// TextFormat lets those be spelled any number of times.  Every other field
// (singular scalar/message, optional, oneof alternative) may appear at most
// once per message body.
template <typename T, std::size_t R>
consteval bool row_is_repeatable()
{
  constexpr auto row = field_table<T>()[R];
  constexpr meta::info r = member_v<T, row.member>;
  using M = typename [: meta::type_of(r) :];
  return is_vector_v<M> || is_map_v<M>;
}

// True when any row belonging to `member` was parsed already.  OneOf
// alternatives share one member, so duplicate detection is per member rather
// than per wire field: setting a second alternative is a oneof conflict.
template <typename T, std::size_t... Rs>
bool member_row_seen(std::array<bool, field_table_size_v<T>> const &seen,
                     std::size_t member, std::index_sequence<Rs...>)
{
  bool any = false;
  ((any = (any || (field_table<T>()[Rs].member == member && seen[Rs]))),
   ...);
  return any;
}

// Duplicate-field gate around text_parse_row: reject a second occurrence of
// any non-repeatable field before its value is parsed.
template <typename T, std::size_t R>
bool text_parse_row_checked(cursor &c, T &v,
                            std::array<bool, field_table_size_v<T>> &seen)
{
  constexpr auto row = field_table<T>()[R];
  if constexpr (!row_is_repeatable<T, R>())
    {
      if constexpr (row.alt != 0)
        {
          if (member_row_seen<T>(
                  seen, row.member,
                  std::make_index_sequence<field_table_size_v<T>>{}))
            return false;  // oneof already holds another alternative
        }
      else if (seen[R])
        return false;  // singular field specified multiple times
      seen[R] = true;
    }
  return text_parse_row<T, R>(c, v);
}

template <typename T, std::size_t... Rs>
bool text_parse_row_dispatch(cursor &c, T &v, std::size_t row,
                             std::array<bool, field_table_size_v<T>> &seen,
                             std::index_sequence<Rs...>)
{
  bool ok = false;
  ((row == Rs
    && (ok = text_parse_row_checked<T, Rs>(c, v, seen), true))
   || ...);
  return ok;
}

template <typename T>
bool text_parse_message_body(cursor &c, T &msg, bool top)
{
  // Which non-repeatable fields were already seen in this message body;
  // repeated/map fields stay repeatable and are not tracked here.
  std::array<bool, field_table_size_v<T>> seen{};
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
              c, msg, row, seen,
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

}  // namespace text_detail

}  // namespace rpb
