// Shared declarations for the Any bracket syntax.  This header breaks the
// cycle between the type registry (whose parse hooks need
// text_parse_message_body) and the message-body parser (which needs
// text_any_parse): only the parts both sides may see live here.
//
// Part of the rpb header-only codec's text format; include
// "text_format.hpp" instead of this file directly.

#pragma once

#include "rpb/text/lexer.hpp"

#include <string>
#include <string_view>
#include <type_traits>

namespace tmm {

struct Any;  // defined by test_messages.hpp

}  // namespace tmm

namespace rpb {

namespace text_detail {

// --- Any bracket syntax ------------------------------------------------

template <typename T> struct is_any_type : std::false_type {};
template <> struct is_any_type<tmm::Any> : std::true_type {};
template <typename T> inline constexpr bool is_any_type_v =
    is_any_type<T>::value;

template <typename T> bool text_format_parse(std::string_view, T &);
template <typename T> bool text_parse_message_body(cursor &, T &, bool);

struct text_any_entry
{
  std::string_view full_name;
  bool (*parse_body)(cursor &, std::string &);
};

}  // namespace text_detail

}  // namespace rpb
