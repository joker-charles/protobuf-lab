// Depth guards, scratch buffers and proto3 default omission.
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include "rpb/annotations.hpp"
#include "rpb/traits.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <type_traits>

#include <contracts>

__attribute__((weak)) void handle_contract_violation(
    std::contracts::contract_violation const &)
{
  std::abort();
}

namespace rpb {

// Serialization recursion guard.  Every nested message (unique_ptr /
// optional / map value / vector element / plain struct member) recurses
// through serialize(), so one counter covers all paths.  Mirrors the
// parser's recursion limit; over-deep input aborts via the contract
// handler (parse, by contrast, returns false) - documented asymmetry.
inline constexpr std::size_t kMaxSerializeDepth = 64;
inline thread_local std::size_t serialize_depth_v = 0;
inline thread_local std::size_t parse_depth_v = 0;
// Unknown-group skipping recurses too; bound it independently so a stream
// full of nested START_GROUP tags cannot smash the stack.
inline thread_local std::size_t skip_depth_v = 0;

// RAII increment/decrement for the cumulative nesting counters.
template <typename Counter> struct counter_scope
{
  Counter &counter;
  explicit counter_scope(Counter &c) : counter(c) { ++counter; }
  ~counter_scope() { --counter; }
};

// Reusable per-thread scratch buffers for embedded-message / map-entry /
// packed payloads.  Chunk building is strictly LIFO (nested messages
// recurse through serialize(), which may itself open further chunks), so
// slots are handed out from a stack and popped when the chunk is written.
// Strings keep their capacity across calls, eliminating the per-message
// heap allocation of the old local-payload approach.  256 slots bounds
// the worst case (~2 chunks per serialize level, 64 levels).
inline thread_local std::array<std::string, 256> serialize_scratch_v;
inline thread_local std::size_t serialize_scratch_top_v = 0;

// --- proto3 default-value omission ---------------------------------------

template <typename M>
inline constexpr bool is_omittable_v =
    std::is_arithmetic_v<M> || std::is_enum_v<M>
    || std::is_same_v<M, std::string> || is_wire_wrapper_v<M>
    || is_bytes_wrapper_v<M> || is_vector_v<M> || is_map_v<M>;

// A by-value member that is neither a scalar/container nor a
// presence-bearing type (optional/unique_ptr/oneof) is a plain nested
// message.  Value semantics cannot distinguish "unset" from "set to an
// empty message", so plain nested messages are omitted when all their
// members are default/absent (matching protobuf for the unset case).
template <typename M>
inline constexpr bool is_plain_message_v =
    !std::is_arithmetic_v<M> && !std::is_enum_v<M>
    && !std::is_same_v<M, std::string> && !is_vector_v<M> && !is_map_v<M>
    && !is_optional_v<M> && !is_unique_ptr_v<M> && !is_one_of_v<M>
    && !is_wire_wrapper_v<M> && !is_unpacked_wrapper_v<M>
    && !is_bytes_wrapper_v<M>
    && !std::is_same_v<M, UnknownField> && !std::is_same_v<M, UnknownFields>;

template <typename M>
bool is_default_value(M const &v)
{
  if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    return underlying_value(v) == underlying_value(M{});
  else if constexpr (std::is_floating_point_v<M>)
    // protobuf compares the raw bits: -0.0 is *not* the default encoding
    // (it serializes as a present 0x80000000 / 0x8000... field).
    return v == 0.0 && !std::signbit(v);
  else if constexpr (std::is_same_v<M, std::string>)
    return v.empty();
  else if constexpr (is_sint_wrapper_v<M>)
    return v.value == typename M::value_type{};
  else if constexpr (is_fixed_wrapper_v<M>)
    return v.value == 0;
  else if constexpr (is_bytes_wrapper_v<M>)
    return v.value.empty();
  else
    return v.empty();  // vectors: empty repeated fields are omitted
}

template <typename T> bool is_empty_message(T const &v);

template <typename M>
bool member_is_default_or_absent(M const &v)
{
  if constexpr (is_omittable_v<M>)
    return is_default_value(v);
  else if constexpr (is_optional_v<M>)
    return !v.has_value();
  else if constexpr (is_unique_ptr_v<M>)
    return !v;
  else if constexpr (is_one_of_v<M>)
    return v.index() == 0;  // monostate: unset
  else
    return is_empty_message(v);  // plain nested message
}

template <typename T>
bool is_empty_message(T const &v)
{
  bool empty = true;
  template for (constexpr auto m :
                std::define_static_array(
                    meta::nonstatic_data_members_of(^^T, members_ctx())))
    {
      if (!member_is_default_or_absent(v.[:m:]))
        empty = false;
    }
  return empty;
}

}  // namespace rpb
