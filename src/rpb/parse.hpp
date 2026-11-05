// Wire parsing.
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include "rpb/annotations.hpp"
#include "rpb/traits.hpp"
#include "rpb/layout.hpp"
#include "rpb/wire.hpp"
#include "rpb/group.hpp"
#include "rpb/core.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

namespace rpb {

// --- parsing ---

template <typename T> bool parse(std::string_view data, T &v);

template <typename U>
bool read_packed_element(google::protobuf::io::CodedInputStream &cis, U &out)
{
  if constexpr (std::is_floating_point_v<U>)
    {
      if constexpr (sizeof(U) == 4)
        {
          std::uint32_t raw;
          if (!cis.ReadLittleEndian32(&raw))
            return false;
          out = std::bit_cast<float>(raw);
        }
      else
        {
          std::uint64_t raw;
          if (!cis.ReadLittleEndian64(&raw))
            return false;
          out = std::bit_cast<double>(raw);
        }
      return true;
    }
  else if constexpr (is_sint_wrapper_v<U>)
    {
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = typename U::value_type;
      if constexpr (sizeof(R) == 4)
        out.value =
            static_cast<R>(unzigzag32(static_cast<std::uint32_t>(raw)));
      else if constexpr (sizeof(R) == 8)
        out.value = static_cast<R>(unzigzag64(raw));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
      return true;
    }
  else if constexpr (std::is_same_v<U, Fixed32>
                     || std::is_same_v<U, SFixed32>)
    {
      std::uint32_t raw;
      if (!cis.ReadLittleEndian32(&raw))
        return false;
      out.value = std::bit_cast<decltype(out.value)>(raw);
      return true;
    }
  else if constexpr (std::is_same_v<U, Fixed64>
                     || std::is_same_v<U, SFixed64>)
    {
      std::uint64_t raw;
      if (!cis.ReadLittleEndian64(&raw))
        return false;
      out.value = std::bit_cast<decltype(out.value)>(raw);
      return true;
    }
  else
    {
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = underlying_or_self_t<U>;
      if constexpr (std::is_same_v<R, bool>)
        out = static_cast<U>(raw != 0);
      else if constexpr (std::is_signed_v<R>)
        out = static_cast<U>(static_cast<R>(static_cast<std::int64_t>(raw)));
      else
        out = static_cast<U>(static_cast<R>(raw));
      return true;
    }
}

inline bool skip_field(google::protobuf::io::CodedInputStream &cis,
                       std::uint32_t fieldno, std::uint32_t wt);

// Strict length-prefix reader.  CodedInputStream::ReadVarint32 silently
// truncates varints wider than 32 bits and ReadRaw(int) cannot represent
// sizes above INT_MAX, so read the full 64-bit value first and require it
// to fit both INT_MAX and the bytes remaining in the current limit.  This
// is what prevents a 6-byte input from requesting a 4 GiB allocation.
inline bool read_length(google::protobuf::io::CodedInputStream &cis,
                        std::uint32_t &len)
{
  std::uint64_t raw;
  if (!cis.ReadVarint64(&raw))
    return false;
  constexpr std::uint64_t max_len =
      static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
  if (raw > max_len)
    return false;
  if (cis.BytesUntilLimit() >= 0
      && raw > static_cast<std::uint64_t>(cis.BytesUntilLimit()))
    return false;
  len = static_cast<std::uint32_t>(raw);
  return true;
}

// Read one tag, rejecting field numbers above the 29-bit protobuf limit
// and tag varints wider than 5 bytes.  CodedInputStream::ReadTag uses the
// 32-bit varint reader, which silently truncates over-long encodings, so
// the raw width has to be checked through the current stream position.
inline bool read_tag_checked(google::protobuf::io::CodedInputStream &cis,
                             std::uint32_t &tag)
{
  int before = cis.CurrentPosition();
  tag = cis.ReadTag();
  int consumed = cis.CurrentPosition() - before;
  if (tag == 0)
    return true;  // EOF / invalid; caller distinguishes via position
  if (consumed > 5)
    return false;
  if ((tag >> 3) == 0 || (tag >> 3) > 0x1FFFFFFFu)
    return false;
  return true;
}

// Reads a length-delimited payload (wire type 2) into `payload`.
bool read_message_payload(google::protobuf::io::CodedInputStream &cis,
                          std::uint32_t wt, std::string &payload)
{
  if (wt != 2)
    return false;
  std::uint32_t len;
  if (!read_length(cis, len))
    return false;
  payload.assign(static_cast<std::size_t>(len), '\0');
  return cis.ReadRaw(payload.data(), static_cast<int>(len));
}

template <typename M>
bool parse_value(google::protobuf::io::CodedInputStream &cis, std::uint32_t wt,
                 M &val, std::uint32_t fieldno = 0);

template <typename M>
bool parse_value(google::protobuf::io::CodedInputStream &cis, std::uint32_t wt,
                 M &val, std::uint32_t fieldno)
{
  if constexpr (std::is_same_v<M, std::string>)
    {
      if (wt != 2)
        return false;
      std::uint32_t len;
      if (!read_length(cis, len))
        return false;
      // protobuf 3.21's ReadRaw(void*, int) copies into a caller buffer
      // (newer versions use a const void** out-parameter instead).
      val.resize(static_cast<std::size_t>(len));
      if (!cis.ReadRaw(val.data(), static_cast<int>(len)))
        return false;
      return true;
    }
  else if constexpr (is_bytes_wrapper_v<M>)
    return read_message_payload(cis, wt, val.value);
  else if constexpr (std::is_floating_point_v<M>)
    {
      if constexpr (sizeof(M) == 4)
        {
          if (wt != 5)
            return false;
          std::uint32_t raw;
          if (!cis.ReadLittleEndian32(&raw))
            return false;
          val = std::bit_cast<float>(raw);
          return true;
        }
      else
        {
          if (wt != 1)
            return false;
          std::uint64_t raw;
          if (!cis.ReadLittleEndian64(&raw))
            return false;
          val = std::bit_cast<double>(raw);
          return true;
        }
    }
  else if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    {
      if (wt != 0)
        return false;
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = underlying_or_self_t<M>;
      if constexpr (std::is_same_v<R, bool>)
        val = static_cast<M>(raw != 0);
      else if constexpr (std::is_signed_v<R>)
        val = static_cast<M>(static_cast<R>(static_cast<std::int64_t>(raw)));
      else
        val = static_cast<M>(static_cast<R>(raw));
      return true;
    }
  else if constexpr (is_sint_wrapper_v<M>)
    {
      if (wt != 0)
        return false;
      std::uint64_t raw;
      if (!cis.ReadVarint64(&raw))
        return false;
      using R = typename M::value_type;
      if constexpr (sizeof(R) == 4)
        val.value =
            static_cast<R>(unzigzag32(static_cast<std::uint32_t>(raw)));
      else if constexpr (sizeof(R) == 8)
        val.value = static_cast<R>(unzigzag64(raw));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
      return true;
    }
  else if constexpr (std::is_same_v<M, Fixed32>
                     || std::is_same_v<M, SFixed32>)
    {
      if (wt != 5)
        return false;
      std::uint32_t raw;
      if (!cis.ReadLittleEndian32(&raw))
        return false;
      val.value = std::bit_cast<decltype(val.value)>(raw);
      return true;
    }
  else if constexpr (std::is_same_v<M, Fixed64>
                     || std::is_same_v<M, SFixed64>)
    {
      if (wt != 1)
        return false;
      std::uint64_t raw;
      if (!cis.ReadLittleEndian64(&raw))
        return false;
      val.value = std::bit_cast<decltype(val.value)>(raw);
      return true;
    }
  else if constexpr (is_unpacked_wrapper_v<M>)
    return parse_value(cis, wt, val.value);
  else if constexpr (is_map_v<M>)
    {
      if (wt != 2)
        return false;
      std::uint32_t len;
      if (!read_length(cis, len))
        return false;
      std::string payload(static_cast<std::size_t>(len), '\0');
      if (!cis.ReadRaw(payload.data(), static_cast<int>(len)))
        return false;
      using K = typename M::key_type;
      using V = typename M::mapped_type;
      K k{};
      V v{};
      google::protobuf::io::ArrayInputStream ais(
          payload.data(), static_cast<int>(payload.size()));
      google::protobuf::io::CodedInputStream ecis(&ais);
      ecis.SetRecursionLimit(static_cast<int>(kMaxSerializeDepth));
      ecis.PushLimit(static_cast<int>(payload.size()));
      for (;;)
        {
          int before = ecis.CurrentPosition();
          std::uint32_t tag;
          if (!read_tag_checked(ecis, tag))
            return false;
          if (tag == 0)
            {
              if (before == static_cast<int>(payload.size()))
                break;  // clean end of the map-entry payload
              return false;  // zero/truncated tag inside the entry
            }
          std::uint32_t fn = tag >> 3;
          std::uint32_t w = tag & 7;
          if (fn == 1)
            {
              // protobuf treats a key/value field with the wrong wire type
              // as an unknown field of the map entry: skip it and let the
              // default key/value stand.
              if (w != static_cast<std::uint32_t>(expected_wire_type_v<K>))
                {
                  if (!skip_field(ecis, fn, w))
                    return false;
                }
              else if (!parse_value(ecis, w, k))
                return false;
            }
          else if (fn == 2)
            {
              if (w != static_cast<std::uint32_t>(expected_wire_type_v<V>))
                {
                  if (!skip_field(ecis, fn, w))
                    return false;
                }
              else if (!parse_value(ecis, w, v))
                return false;
            }
          else
            {
              if (!skip_field(ecis, fn, w))
                return false;
            }
        }
      val[std::move(k)] = std::move(v);
      return true;
    }
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_packable_v<U>)
        {
          if (wt != 2)
            {
              // Accept a single unpacked element too; protobuf parsers
              // accept both packed and unpacked forms for repeated numerics,
              // but only with the element's own wire type.
              if (wt != static_cast<std::uint32_t>(expected_wire_type_v<U>))
                return false;
              U e;
              if (!read_packed_element(cis, e))
                return false;
              val.push_back(std::move(e));
              return true;
            }
          std::uint32_t len;
          if (!read_length(cis, len))
            return false;
          auto limit = cis.PushLimit(static_cast<int>(len));
          while (cis.BytesUntilLimit() > 0)
            {
              U e;
              if (!read_packed_element(cis, e))
                {
                  cis.PopLimit(limit);
                  return false;
                }
              val.push_back(e);
            }
          cis.PopLimit(limit);
          return true;
        }
      else
        {
          // repeated length-delimited: one element per tag
          U e;
          if (!parse_value(cis, wt, e))
            return false;
          val.push_back(std::move(e));
          return true;
        }
    }
  else if constexpr (is_optional_v<M>)
    {
      if (val.has_value())
        {
          // Merge into the existing value: scalar/string/wrapper
          // alternatives overwrite (last-wins), but message alternatives
          // accumulate fields, matching the codec's plain-member/oneof
          // merge semantics.
          return parse_value(cis, wt, *val);
        }
      typename M::value_type inner{};
      if (!parse_value(cis, wt, inner))
        return false;
      val = std::move(inner);
      return true;
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      // Singular message behind a pointer: allocate on hit.
      std::string payload;
      if (!read_message_payload(cis, wt, payload))
        return false;
      if (!val)
        val = std::make_unique<typename M::element_type>();
      return parse(payload, *val);
    }
  else
    {
      // embedded message
      if constexpr (std::is_same_v<M, UnknownField>)
        return false;  // internal single-entry type is never a wire message
      else
        {
          std::string payload;
          if (!read_message_payload(cis, wt, payload))
            return false;
          // Merge into the existing value: protobuf merges repeated
          // occurrences of a singular message field instead of replacing.
          return parse(payload, val);
        }
    }
}

template <typename T, std::size_t I, std::size_t K>
bool parse_oneof_alt(google::protobuf::io::CodedInputStream &cis,
                     std::uint32_t fieldno, std::uint32_t wt, T &v)
{
  constexpr meta::info r = member_v<T, I>;
  using M = typename [: meta::type_of(r) :];
  if (fieldno != field_number<r, K>())
    return false;
  if (v.[:r:].index() == K + 1)
    {
      // Same oneof alternative already active: merge into it (protobuf
      // semantics), so repeated message occurrences accumulate fields.
      return parse_value(cis, wt, std::get<K + 1>(v.[:r:]));
    }
  using Alt = std::variant_alternative_t<K + 1, M>;
  Alt val{};
  if (!parse_value(cis, wt, val))
    return false;
  v.[:r:].template emplace<K + 1>(std::move(val));
  return true;
}

// Dispatch a tag to its field via the compile-time field table.  The
// table is sorted ascending by field number (unique), so a template-
// generated binary decision tree resolves a fieldno in O(log N) constant
// comparisons instead of scanning every member in declaration order
// (protoc-generated parsers use a switch/jump table; this is the
// reflection-codec equivalent).  Each row knows its member index and
// whether it is an ordinary member (alt == 0) or a OneOf alternative
// (alt == variant index 1..N).

// Parse a proto2 group body (fields until the matching END_GROUP) into an
// inner message struct; defined after skip_field / capture_unknown_field.
template <typename Inner>
bool parse_group_body(google::protobuf::io::CodedInputStream &cis,
                      std::uint32_t fieldno, Inner &out);

// Result of dispatching a tag through the compile-time field table.  The
// distinction between "no row matches this field number" (unknown field:
// capture/skip) and "row matches but the payload is invalid" (parse error:
// reject, the stream has already been consumed) is required for correct
// failure behaviour; a plain bool conflated the two and let the parser
// resynchronise on garbage.
enum class parse_table_result { not_found, ok, error };

// Does compile-time table row R accept wire type `wt`?  Protobuf treats a
// known field number carrying an unexpected wire type as an *unknown*
// field (skip/capture), not as a parse error; a matching wire type with a
// malformed payload is a hard error.  Packable repeated fields accept the
// packed form (wire type 2) as well as one unpacked element.
template <typename T, std::size_t R>
bool row_accepts_wire_type(std::uint32_t wt)
{
  constexpr auto row = field_table<T>()[R];
  constexpr meta::info r = member_v<T, row.member>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (member_is_group_ann<r>())
    return wt == 3;  // START_GROUP
  else if constexpr (row.alt != 0)
    {
      using Alt = std::variant_alternative_t<row.alt, M>;
      return wt == static_cast<std::uint32_t>(expected_wire_type_v<Alt>);
    }
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_packable_v<U>)
        return wt == 2
               || wt == static_cast<std::uint32_t>(expected_wire_type_v<U>);
      else
        return wt == static_cast<std::uint32_t>(expected_wire_type_v<U>);
    }
  else
    return wt == static_cast<std::uint32_t>(expected_wire_type_v<M>);
}

template <typename T, std::size_t R>
bool parse_table_row(google::protobuf::io::CodedInputStream &cis,
                     std::uint32_t fieldno, std::uint32_t wt, T &v)
{
  constexpr auto row = field_table<T>()[R];
  constexpr meta::info r = member_v<T, row.member>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (member_is_group_ann<r>())
    {
      // proto2 group: the tag must be a START_GROUP (wire type 3); parse
      // the body into the member's inner message.
      if (wt != 3)
        return false;
      return parse_group_body(cis, fieldno,
                              *GroupPtrMut<M>::get(v.[:r:]));
    }
  else if constexpr (row.alt == 0)
    return parse_value(cis, wt, v.[:r:], fieldno);
  else
    return parse_oneof_alt<T, row.member, row.alt - 1>(cis, fieldno, wt, v);
}

template <typename T, std::size_t Lo, std::size_t Hi>
parse_table_result parse_table_range(google::protobuf::io::CodedInputStream &cis,
                                     std::uint32_t fieldno, std::uint32_t wt,
                                     T &v)
{
  if constexpr (Lo == Hi)
    return parse_table_result::not_found;  // no row carries this field number
  else
    {
      constexpr std::size_t Mid = Lo + (Hi - Lo) / 2;
      constexpr std::uint32_t fn = field_table<T>()[Mid].fieldno;
      if (fieldno < fn)
        return parse_table_range<T, Lo, Mid>(cis, fieldno, wt, v);
      if (fieldno > fn)
        return parse_table_range<T, Mid + 1, Hi>(cis, fieldno, wt, v);
      // Wire-type mismatch on a known field: treat it exactly like an
      // unknown field number (protobuf skips/captures it).
      if (!row_accepts_wire_type<T, Mid>(wt))
        return parse_table_result::not_found;
      return parse_table_row<T, Mid>(cis, fieldno, wt, v)
                 ? parse_table_result::ok
                 : parse_table_result::error;
    }
}

// Result of trying to capture an unknown field.  Groups (wire types 3/4)
// are not captured but may still be skipped; a malformed payload is a hard
// error (the stream has already consumed bytes and must not be retried from
// a different position).
enum class capture_result { captured, unsupported, error };

inline capture_result capture_unknown_field(
    google::protobuf::io::CodedInputStream &cis, std::uint32_t fieldno,
    std::uint32_t wt, UnknownFields &out)
{
  UnknownField uf{fieldno, wt, {}};
  switch (wt)
    {
    case 0:
      {
        std::uint64_t tmp;
        if (!cis.ReadVarint64(&tmp))
          return capture_result::error;
        // Re-encode canonically; unknown varints are stored by value, like
        // protobuf's UnknownFieldSet.
        google::protobuf::io::StringOutputStream sos(&uf.raw);
        google::protobuf::io::CodedOutputStream cos(&sos);
        cos.WriteVarint64(tmp);
        break;
      }
    case 1:
      uf.raw.resize(8);
      if (!cis.ReadRaw(uf.raw.data(), 8))
        return capture_result::error;
      break;
    case 2:
      {
        std::uint32_t len;
        if (!read_length(cis, len))
          return capture_result::error;
        // Store the length prefix inside raw so re-emission is just
        // tag + raw for every wire type.
        std::string head;
        {
          google::protobuf::io::StringOutputStream sos(&head);
          google::protobuf::io::CodedOutputStream cos(&sos);
          cos.WriteVarint32(len);
        }
        uf.raw = std::move(head);
        std::size_t base = uf.raw.size();
        uf.raw.resize(base + len);
        if (!cis.ReadRaw(uf.raw.data() + static_cast<int>(base),
                         static_cast<int>(len)))
          return capture_result::error;
        break;
      }
    case 5:
      uf.raw.resize(4);
      if (!cis.ReadRaw(uf.raw.data(), 4))
        return capture_result::error;
      break;
    default:
      return capture_result::unsupported;  // groups (3/4): skip instead
    }
  out.push_back(std::move(uf));
  return capture_result::captured;
}

inline bool skip_field(google::protobuf::io::CodedInputStream &cis,
                       std::uint32_t fieldno, std::uint32_t wt)
{
  switch (wt)
    {
    case 0:
      {
        std::uint64_t tmp;
        return cis.ReadVarint64(&tmp);
      }
    case 1:
      return cis.Skip(8);
    case 2:
      {
        std::uint32_t len;
        if (!read_length(cis, len))
          return false;
        return cis.Skip(static_cast<int>(len));
      }
    case 3:  // start group: skip until the matching end-group tag
      {
        // Bound the recursion: skipping nested unknown groups otherwise
        // follows untrusted input all the way down the stack.
        if (skip_depth_v >= kMaxSerializeDepth)
          return false;
        counter_scope depth(skip_depth_v);
        for (;;)
          {
            std::uint32_t tag;
            if (!read_tag_checked(cis, tag))
              return false;
            if (tag == 0)
              return false;  // truncated group
            std::uint32_t fn = tag >> 3;
            std::uint32_t w = tag & 7;
            if (w == 4)
              return fn == fieldno;
            if (!skip_field(cis, fn, w))
              return false;
          }
      }
    case 5:
      return cis.Skip(4);
    default:
      return false;
    }
}

template <typename Inner>
bool parse_group_body(google::protobuf::io::CodedInputStream &cis,
                      std::uint32_t fieldno, Inner &out)
{
  // Group bodies are parsed here rather than through parse(), so the
  // layout check and the cumulative nesting guard must be applied here.
  check_layout<Inner>();
  if (parse_depth_v >= kMaxSerializeDepth)
    return false;
  counter_scope depth(parse_depth_v);
  for (;;)
    {
      std::uint32_t tag;
      if (!read_tag_checked(cis, tag))
        return false;
      if (tag == 0)
        return false;  // truncated group
      std::uint32_t fn = tag >> 3;
      std::uint32_t w = tag & 7;
      if (w == 4)
        return fn == fieldno;  // end group must match the start group tag
      parse_table_result r
          = parse_table_range<Inner, 0, field_table_size_v<Inner>>(cis, fn, w,
                                                                   out);
      if (r == parse_table_result::ok)
        continue;
      if (r == parse_table_result::error)
        return false;  // known field consumed, payload invalid: reject
      if constexpr (has_unknown_fields_v<Inner>)
        {
          capture_result cr = capture_unknown_field(
              cis, fn, w,
              out.[: member_v<Inner, unknown_member_index_v<Inner>> :]);
          if (cr == capture_result::captured)
            continue;
          if (cr == capture_result::error)
            return false;  // malformed payload: do not resynchronise
          // unsupported (group): fall through to skip_field below
        }
      if (!skip_field(cis, fn, w))
        return false;
    }
}

template <typename T>
bool parse(std::string_view data, T &v)
{
  check_layout<T>();
  if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return false;
  // Cumulative nesting guard.  Nested messages recurse through parse()
  // and each level builds a fresh CodedInputStream, so a per-stream
  // SetRecursionLimit would reset per level; the thread_local counter
  // bounds the real recursion depth, mirroring serialize()'s guard.
  // Unlike serialize(), over-depth input is rejected gracefully (false) -
  // documented asymmetry.
  if (parse_depth_v >= kMaxSerializeDepth)
    return false;
  counter_scope depth(parse_depth_v);
  google::protobuf::io::ArrayInputStream ais(data.data(),
                                             static_cast<int>(data.size()));
  google::protobuf::io::CodedInputStream cis(&ais);
  cis.SetRecursionLimit(static_cast<int>(kMaxSerializeDepth));
  // Give the stream an explicit limit so read_length() can bound length
  // prefixes by the bytes actually remaining (top-level and every nested
  // parse() call gets its own exact-sized stream).
  cis.PushLimit(static_cast<int>(data.size()));
  for (;;)
    {
      int before = cis.CurrentPosition();
      std::uint32_t tag;
      if (!read_tag_checked(cis, tag))
        return false;
      if (tag == 0)
        {
          // CodedInputStream returns 0 for real EOF *and* for a literal
          // 0x00 tag or a truncated tag varint.  Only accept the former:
          // no byte may have been consumed by the failed read.
          if (before == static_cast<int>(data.size()))
            break;  // clean EOF
          return false;
        }
      std::uint32_t fieldno = tag >> 3;
      std::uint32_t wt = tag & 7;
      parse_table_result r
          = parse_table_range<T, 0, field_table_size_v<T>>(cis, fieldno, wt,
                                                           v);
      if (r == parse_table_result::ok)
        continue;
      if (r == parse_table_result::error)
        return false;  // known field consumed, payload invalid: reject
      if constexpr (has_unknown_fields_v<T>)
        {
          capture_result cr = capture_unknown_field(
              cis, fieldno, wt,
              v.[: member_v<T, unknown_member_index_v<T>> :]);
          if (cr == capture_result::captured)
            continue;
          if (cr == capture_result::error)
            return false;  // malformed payload: do not resynchronise
          // unsupported (group): fall through to skip_field below
        }
      if (!skip_field(cis, fieldno, wt))
        return false;
    }
  return true;
}

}  // namespace rpb
