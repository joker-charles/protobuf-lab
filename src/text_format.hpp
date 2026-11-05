// Proto text format (parse + print) for the reflection codec.
//
// Implements the proto3 text-format subset exercised by the official
// conformance suite: field names (member identifiers; OneOf alternatives
// via [[=rpb::name<...>]] annotations), nested messages, repeated lists,
// maps (sorted on output, last-wins on duplicate keys), oneofs, Any
// bracket syntax, string/bytes escapes (octal/hex/unicode, UTF-8
// validation for string fields -- the conformance suite requires it even
// though protobuf's own text parser is lenient; bytes fields are exempt),
// integer literals in decimal/hex (`0x...`) and octal (`010`) spelling
// with range and overflow checks, an optional `f` suffix on floats,
// duplicate non-repeated field rejection, and float nan/inf handling.
// Unknown fields are ignored on print and rejected on parse (TextFormat's
// default parser behavior).

#pragma once

#include "codec.hpp"
#include "test_messages.hpp"

#include "rpb/text/lexer.hpp"
#include "rpb/text/any.hpp"
#include "rpb/text/parse.hpp"
#include "rpb/text/print.hpp"

#include <string>
#include <string_view>

namespace rpb {

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
