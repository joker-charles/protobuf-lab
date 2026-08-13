// Conformance testee: speaks the official protobuf conformance protocol
// over stdin/stdout (4-byte little-endian length-prefixed messages).
// Supports proto3 and proto2 TestAllTypes through the reflection codec
// (mirrors in test_messages.hpp): binary wire, JSON, and text format.
#include "test_messages.hpp"
#include "text_format.hpp"
#include "json.hpp"

#include "conformance.pb.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using conformance::ConformanceRequest;
using conformance::ConformanceResponse;
using conformance::FailureSet;

static constexpr char const *kType3 =
    "protobuf_test_messages.proto3.TestAllTypesProto3";
static constexpr char const *kType2 =
    "protobuf_test_messages.proto2.TestAllTypesProto2";

static bool read_message(std::string &out)
{
  unsigned char lenbuf[4];
  std::size_t got = std::fread(lenbuf, 1, 4, stdin);
  if (got != 4)
    return false;  // EOF or broken pipe
  std::uint32_t len = static_cast<std::uint32_t>(lenbuf[0])
                      | (static_cast<std::uint32_t>(lenbuf[1]) << 8)
                      | (static_cast<std::uint32_t>(lenbuf[2]) << 16)
                      | (static_cast<std::uint32_t>(lenbuf[3]) << 24);
  out.assign(len, '\0');
  return std::fread(out.data(), 1, len, stdin) == len;
}

static void write_message(std::string const &msg)
{
  std::uint32_t len = static_cast<std::uint32_t>(msg.size());
  unsigned char lenbuf[4] = {
      static_cast<unsigned char>(len & 0xFF),
      static_cast<unsigned char>((len >> 8) & 0xFF),
      static_cast<unsigned char>((len >> 16) & 0xFF),
      static_cast<unsigned char>((len >> 24) & 0xFF)};
  std::fwrite(lenbuf, 1, 4, stdout);
  std::fwrite(msg.data(), 1, msg.size(), stdout);
  std::fflush(stdout);
}

// Handle one ConformanceRequest for a concrete message type M.
template <typename M>
static void handle(ConformanceRequest const &req, ConformanceResponse &resp)
{
  // JSON_TEST / JSON_IGNORE_UNKNOWN: the input payload is json_payload, or
  // protobuf_payload for PROTOBUF-input JSON-output tests.
  auto parse_json_or_binary = [&](M &msg) {
    if (!req.json_payload().empty())
      return rpb::json_parse(req.json_payload(), msg);
    if (!req.protobuf_payload().empty())
      return rpb::parse(req.protobuf_payload(), msg);
    return false;
  };

  if (req.test_category() == conformance::BINARY_TEST)
    {
      M msg;
      if (!rpb::parse(req.protobuf_payload(), msg))
        {
          resp.set_parse_error("invalid protobuf input");
        }
      else
        {
          if (req.requested_output_format() == conformance::PROTOBUF)
            {
              std::string out;
              rpb::serialize(out, msg);
              resp.set_protobuf_payload(out);
            }
          else if (req.requested_output_format() == conformance::TEXT_FORMAT)
            {
              std::string out;
              rpb::text_format_print(out, msg);
              resp.set_text_payload(out);
            }
          else if (req.requested_output_format() == conformance::JSON)
            {
              std::string out;
              if (rpb::json_serialize(out, msg))
                resp.set_json_payload(out);
              else
                resp.set_serialize_error("well-known type out of range");
            }
          else
            resp.set_skipped("unsupported output format");
        }
    }
  else if (req.test_category() == conformance::JSON_TEST
           || req.test_category()
                  == conformance::JSON_IGNORE_UNKNOWN_PARSING_TEST)
    {
      M msg;
      bool ok = parse_json_or_binary(msg);
      if (!ok)
        {
          resp.set_parse_error("invalid JSON/protobuf input");
        }
      else if (req.requested_output_format() == conformance::PROTOBUF)
        {
          std::string out;
          rpb::serialize(out, msg);
          resp.set_protobuf_payload(out);
        }
      else if (req.requested_output_format() == conformance::JSON)
        {
          std::string out;
          if (rpb::json_serialize(out, msg))
            resp.set_json_payload(out);
          else
            resp.set_serialize_error("well-known type out of range");
        }
      else if (req.requested_output_format() == conformance::TEXT_FORMAT)
        {
          std::string out;
          rpb::text_format_print(out, msg);
          resp.set_text_payload(out);
        }
      else
        resp.set_skipped("unsupported output format");
    }
  else if (req.test_category() == conformance::TEXT_FORMAT_TEST)
    {
      M msg;
      bool ok;
      if (!req.text_payload().empty() || req.protobuf_payload().empty())
        ok = rpb::text_format_parse(req.text_payload(), msg);
      else
        ok = rpb::parse(req.protobuf_payload(), msg);
      if (!ok)
        {
          resp.set_parse_error("invalid text format input");
        }
      else if (req.requested_output_format() == conformance::PROTOBUF)
        {
          std::string out;
          rpb::serialize(out, msg);
          resp.set_protobuf_payload(out);
        }
      else if (req.requested_output_format() == conformance::TEXT_FORMAT)
        {
          std::string out;
          rpb::text_format_print(out, msg);
          resp.set_text_payload(out);
        }
      else if (req.requested_output_format() == conformance::JSON)
        {
          std::string out;
          if (rpb::json_serialize(out, msg))
            resp.set_json_payload(out);
          else
            resp.set_serialize_error("well-known type out of range");
        }
      else
        resp.set_skipped("unsupported output format");
    }
  else
    {
      resp.set_skipped("test category not implemented");
    }
}

int main()
{
  for (;;)
    {
      std::string req_bytes;
      if (!read_message(req_bytes))
        return 0;
      ConformanceRequest req;
      ConformanceResponse resp;
      if (!req.ParseFromString(req_bytes))
        {
          resp.set_runtime_error("cannot parse ConformanceRequest");
        }
      else if (req.message_type() == FailureSet::descriptor()->full_name())
        {
          // Runner asks for our declared failures; we keep them in the
          // --failure_list file instead, so reply with an empty set.
          resp.set_protobuf_payload(FailureSet().SerializeAsString());
        }
      else if (req.message_type() == kType3)
        {
          handle<tmm::TestAllTypesProto3>(req, resp);
        }
      else if (req.message_type() == kType2)
        {
          handle<tmm::TestAllTypesProto2>(req, resp);
        }
      else
        {
          resp.set_skipped("unknown message type");
        }
      std::string resp_bytes;
      resp.SerializeToString(&resp_bytes);
      write_message(resp_bytes);
    }
}
