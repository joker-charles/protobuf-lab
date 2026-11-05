// google.protobuf.Any JSON support: the message-type registry plus the
// "@type" object parse/print entry points.
//
// Part of the rpb header-only JSON codec; include "json.hpp" instead of
// this file directly.
#pragma once

#include "rpb/json/dom.hpp"
#include "rpb/json/fwd.hpp"
#include "rpb/json/parse.hpp"
#include "rpb/json/print.hpp"
#include "rpb/json/wkt.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rpb {

namespace json_detail {

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

}  // namespace rpb
