// Cross-header forward declarations for the rpb JSON internal headers.
//
// The parse and print dispatchers are mutually recursive (a message
// object is parsed/printed by the value dispatcher, which recurses back
// into the message dispatchers), and the Any registry needs the value
// dispatchers as well; declaring them here breaks the include cycles.
//
// Part of the rpb header-only JSON codec; include "json.hpp" instead of
// this file directly.
#pragma once

#include "rpb/json/dom.hpp"
#include "test_messages.hpp"

#include <string>

namespace rpb {

namespace json_detail {

// --- forward declarations ----------------------------------------------

template <typename T> bool json_parse_value(jvalue const &v, T &out,
                                           bool bytes_ctx = false);
template <typename T> bool json_parse_message(jvalue const &v, T &out);
template <typename T> void json_print_value(std::string &out, T const &v,
                                            bool bytes_ctx = false);
template <typename T> void json_print_message(std::string &out,
                                              T const &v);

// Any registry/parse/print entry points (definitions in any.hpp).
inline bool json_parse_any(jvalue const &v, tmm::Any &out);
inline void json_print_any(std::string &out, tmm::Any const &v);

// --- print forward declarations (defined after json_print_*) -----------
template <typename M> void json_any_print_message(tmm::Any const &,
                                                  std::string &, bool &);
template <typename M> void json_any_print_wkt(tmm::Any const &,
                                              std::string &, bool &);

}  // namespace json_detail

}  // namespace rpb
