// Zigzag (sint32/sint64) encoding helpers.
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include <cstdint>

namespace rpb {

// --- zigzag (sint32/sint64) helpers, matching protobuf's encoding ---
// Kept hand-rolled on purpose: protobuf's WireFormatLite::ZigZagEncode* is
// in the google::protobuf::internal namespace, and we avoid internal::
// dependencies.

constexpr std::uint32_t zigzag32(std::int32_t n)
{
  std::uint32_t u = static_cast<std::uint32_t>(n);
  return (u << 1) ^ (0u - (u >> 31));
}
constexpr std::uint64_t zigzag64(std::int64_t n)
{
  std::uint64_t u = static_cast<std::uint64_t>(n);
  return (u << 1) ^ (0ull - (u >> 63));
}
constexpr std::int32_t unzigzag32(std::uint32_t u)
{
  return static_cast<std::int32_t>((u >> 1) ^ (0u - (u & 1)));
}
constexpr std::int64_t unzigzag64(std::uint64_t u)
{
  return static_cast<std::int64_t>((u >> 1) ^ (0ull - (u & 1)));
}

}  // namespace rpb
