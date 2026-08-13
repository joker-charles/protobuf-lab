// Conformance testee: speaks the official protobuf conformance protocol
// over stdin/stdout (4-byte little-endian length-prefixed messages).
// Supports proto3 binary protobuf_test for TestAllTypesProto3 through the
// reflection codec (mirror in test_messages.hpp); everything else (JSON,
// text format, proto2) is skipped.
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

static constexpr char const *kMessageType =
    "protobuf_test_messages.proto3.TestAllTypesProto3";

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
      else if (req.message_type() != kMessageType)
        {
          // proto2 and other message types are not mirrored yet.
          resp.set_skipped("only TestAllTypesProto3 is supported");
        }
      else if (req.test_category() == conformance::BINARY_TEST)
        {
          tmm::TestAllTypesProto3 msg;
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
              else if (req.requested_output_format()
                       == conformance::TEXT_FORMAT)
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
                    resp.set_serialize_error(
                        "well-known type out of range for JSON");
                }
              else
                resp.set_skipped("unsupported output format");
            }
        }
      else if (req.test_category() == conformance::JSON_TEST
               || req.test_category()
                      == conformance::JSON_IGNORE_UNKNOWN_PARSING_TEST)
        {
          tmm::TestAllTypesProto3 msg;
          bool ok;
          if (!req.json_payload().empty())
            ok = rpb::json_parse(req.json_payload(), msg);
          else if (!req.protobuf_payload().empty())
            ok = rpb::parse(req.protobuf_payload(), msg);
          else
            ok = false;
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
                resp.set_serialize_error(
                    "well-known type out of range for JSON");
            }
          else if (req.requested_output_format()
                   == conformance::TEXT_FORMAT)
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
          tmm::TestAllTypesProto3 msg;
          bool ok;
          if (!req.text_payload().empty()
              || req.protobuf_payload().empty())
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
          else if (req.requested_output_format()
                   == conformance::TEXT_FORMAT)
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
                resp.set_serialize_error(
                    "well-known type out of range for JSON");
            }
          else
            resp.set_skipped("unsupported output format");
        }
      else
        {
          resp.set_skipped("other test categories not implemented yet");
        }
      std::string resp_bytes;
      resp.SerializeToString(&resp_bytes);
      write_message(resp_bytes);
    }
}
