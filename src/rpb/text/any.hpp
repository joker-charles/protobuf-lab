// Any bracket syntax: parsing "[type_url] { ... }" plus the registry that
// maps embedded message type URLs to their packed-body parsers.
//
// Part of the rpb header-only codec's text format; include
// "text_format.hpp" instead of this file directly.

#pragma once

#include "codec.hpp"
#include "test_messages.hpp"

#include "rpb/text/any_fwd.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rpb {

namespace text_detail {

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

}  // namespace text_detail

}  // namespace rpb
