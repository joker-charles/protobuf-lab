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

#include "rpb/json/dom.hpp"
#include "rpb/json/wkt.hpp"
#include "rpb/json/fwd.hpp"
#include "rpb/json/parse.hpp"
#include "rpb/json/print.hpp"
#include "rpb/json/any.hpp"

namespace rpb {

template <typename T>
bool json_parse(std::string_view input, T &out)
{
  if constexpr (json_detail::is_wkt_type_v<T>)
    {
      // A top-level well-known type uses its own JSON form (a bare string for
      // Timestamp/Duration/FieldMask, a scalar for wrappers, native JSON for
      // Struct/Value/ListValue, a "@type" object for Any), so hand the root
      // value to the WKT dispatcher.  A JSON null at the root is still
      // rejected: rpb::json_parse has never accepted a null root (protobuf's
      // own parser rather treats a null root as an empty message; this is a
      // deliberate pre-existing rpb restriction, kept here for consistency).
      json_detail::jvalue v;
      if (!json_detail::json_parse_value(input, v))
        return false;
      if (v.kind == json_detail::jkind::null)
        {
          // google.protobuf.Value has a real JSON null form (null_value);
          // every other well-known type rejects a null root.
          if constexpr (json_detail::is_value_type_v<T>)
            return json_detail::json_parse_value(v, out, false);
          return false;
        }
      return json_detail::json_parse_value(v, out, false);
    }
  else
    return json_detail::json_parse(input, out);
}

template <typename T>
bool json_serialize(std::string &out, T const &v)
{
  json_detail::json_serialize_ok_ = true;
  if constexpr (json_detail::is_wkt_type_v<T>)
    json_detail::json_print_value(out, v, false);
  else
    json_detail::json_print_message(out, v);
  return json_detail::json_serialize_ok_;
}

}  // namespace rpb
