// Wire serialization.
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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <contracts>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

namespace rpb {

// --- serialization ---

template <typename T> void serialize(std::string &out, T const &v);
template <typename T>
void serialize_to_cos(google::protobuf::io::CodedOutputStream &cos,
                      T const &v);

template <typename U>
void write_packed_element(google::protobuf::io::CodedOutputStream &cos,
                          U const &e)
{
  if constexpr (std::is_floating_point_v<U>)
    {
      if constexpr (sizeof(U) == 4)
        cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(e));
      else
        cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(e));
    }
  else if constexpr (is_sint_wrapper_v<U>)
    {
      using R = typename U::value_type;
      if constexpr (sizeof(R) == 4)
        cos.WriteVarint64(zigzag32(static_cast<std::int32_t>(e.value)));
      else if constexpr (sizeof(R) == 8)
        cos.WriteVarint64(zigzag64(static_cast<std::int64_t>(e.value)));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
    }
  else if constexpr (std::is_same_v<U, Fixed32>
                     || std::is_same_v<U, SFixed32>)
    cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(e.value));
  else if constexpr (std::is_same_v<U, Fixed64>
                     || std::is_same_v<U, SFixed64>)
    cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(e.value));
  else
    {
      if constexpr (std::is_signed_v<underlying_or_self_t<U>>)
        cos.WriteVarint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(underlying_value(e))));
      else
        cos.WriteVarint64(static_cast<std::uint64_t>(underlying_value(e)));
    }
}

template <typename M>
void serialize_value(google::protobuf::io::CodedOutputStream &cos,
                     std::uint32_t fieldno, M const &val)
{
  if constexpr (std::is_same_v<M, std::string>)
    {
      cos.WriteTag((fieldno << 3) | 2);
      // protobuf 3.21's WriteString emits raw bytes WITHOUT the length
      // prefix (only WriteStringWithSizeToArray includes it).
      cos.WriteVarint32(static_cast<std::uint32_t>(val.size()));
      cos.WriteRaw(val.data(), static_cast<int>(val.size()));
    }
  else if constexpr (is_bytes_wrapper_v<M>)
    {
      cos.WriteTag((fieldno << 3) | 2);
      cos.WriteVarint32(static_cast<std::uint32_t>(val.value.size()));
      cos.WriteRaw(val.value.data(), static_cast<int>(val.value.size()));
    }
  else if constexpr (std::is_floating_point_v<M>)
    {
      if constexpr (sizeof(M) == 4)
        {
          cos.WriteTag((fieldno << 3) | 5);
          cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(val));
        }
      else
        {
          cos.WriteTag((fieldno << 3) | 1);
          cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(val));
        }
    }
  else if constexpr (std::is_integral_v<M> || std::is_enum_v<M>)
    {
      cos.WriteTag((fieldno << 3) | 0);
      using R = underlying_or_self_t<M>;
      if constexpr (std::is_same_v<R, bool>)
        cos.WriteVarint64(val ? 1 : 0);
      else if constexpr (std::is_signed_v<R>)
        cos.WriteVarint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(underlying_value(val))));
      else
        cos.WriteVarint64(static_cast<std::uint64_t>(underlying_value(val)));
    }
  else if constexpr (is_sint_wrapper_v<M>)
    {
      cos.WriteTag((fieldno << 3) | 0);
      using R = typename M::value_type;
      if constexpr (sizeof(R) == 4)
        cos.WriteVarint64(zigzag32(static_cast<std::int32_t>(val.value)));
      else if constexpr (sizeof(R) == 8)
        cos.WriteVarint64(zigzag64(static_cast<std::int64_t>(val.value)));
      else
        static_assert(sizeof(R) == 4 || sizeof(R) == 8,
                      "SInt wrapper must be 4 or 8 bytes");
    }
  else if constexpr (std::is_same_v<M, Fixed32>
                     || std::is_same_v<M, SFixed32>)
    {
      cos.WriteTag((fieldno << 3) | 5);
      cos.WriteLittleEndian32(std::bit_cast<std::uint32_t>(val.value));
    }
  else if constexpr (std::is_same_v<M, Fixed64>
                     || std::is_same_v<M, SFixed64>)
    {
      cos.WriteTag((fieldno << 3) | 1);
      cos.WriteLittleEndian64(std::bit_cast<std::uint64_t>(val.value));
    }
  else if constexpr (is_unpacked_wrapper_v<M>)
    serialize_value(cos, fieldno, val.value);
  else if constexpr (std::is_same_v<M, UnknownFields>)
    {
      for (auto const &uf : val)
        {
          cos.WriteTag((uf.fieldno << 3) | uf.wire_type);
          cos.WriteRaw(uf.raw.data(), static_cast<int>(uf.raw.size()));
        }
    }
  else if constexpr (is_map_v<M>)
    {
      // Each map entry is an embedded message with key = field 1 and
      // value = field 2. Entries serialize in std::map (sorted) order;
      // protobuf does not guarantee map order, so byte-level interop only
      // uses single-entry maps.
      for (auto const &kv : val)
        {
          contract_assert(serialize_scratch_top_v < serialize_scratch_v.size());
          std::string &payload =
              serialize_scratch_v[serialize_scratch_top_v++];
          payload.clear();
          {
            google::protobuf::io::StringOutputStream sos(&payload);
            google::protobuf::io::CodedOutputStream pcos(&sos);
            serialize_value(pcos, 1, kv.first);
            serialize_value(pcos, 2, kv.second);
          }
          cos.WriteTag((fieldno << 3) | 2);
          cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
          cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
          --serialize_scratch_top_v;
        }
    }
  else if constexpr (is_vector_v<M>)
    {
      using U = typename M::value_type;
      if constexpr (is_packable_v<U>)
        {
          contract_assert(serialize_scratch_top_v < serialize_scratch_v.size());
          std::string &payload =
              serialize_scratch_v[serialize_scratch_top_v++];
          payload.clear();
          {
            google::protobuf::io::StringOutputStream sos(&payload);
            google::protobuf::io::CodedOutputStream pcos(&sos);
            for (auto const &e : val)
              write_packed_element(pcos, e);
          }
          cos.WriteTag((fieldno << 3) | 2);
          cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
          cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
          --serialize_scratch_top_v;
        }
      else
        {
          for (auto const &e : val)
            serialize_value(cos, fieldno, e);
        }
    }
  else if constexpr (is_optional_v<M>)
    {
      if (val.has_value())
        serialize_value(cos, fieldno, *val);
    }
  else if constexpr (is_unique_ptr_v<M>)
    {
      // Singular message behind a pointer: presence semantics, null ->
      // omitted, non-null -> embedded message (even if empty).
      if (!val)
        return;
      contract_assert(serialize_scratch_top_v < serialize_scratch_v.size());
      std::string &payload = serialize_scratch_v[serialize_scratch_top_v++];
      payload.clear();
      serialize(payload, *val);
      cos.WriteTag((fieldno << 3) | 2);
      cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
      cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
      --serialize_scratch_top_v;
    }
  else
    {
      if constexpr (std::is_same_v<M, UnknownField>)
        return;  // internal single-entry type is never a wire message
      else
        {
          contract_assert(serialize_scratch_top_v < serialize_scratch_v.size());
          std::string &payload = serialize_scratch_v[serialize_scratch_top_v++];
          payload.clear();
          serialize(payload, val);
          cos.WriteTag((fieldno << 3) | 2);
          cos.WriteVarint32(static_cast<std::uint32_t>(payload.size()));
          cos.WriteRaw(payload.data(), static_cast<int>(payload.size()));
          --serialize_scratch_top_v;
        }
    }
}

// Ordinary (non-group) member serialize.
template <typename T, std::uint32_t FNO, std::size_t MI, std::size_t AI>
void serialize_entry_group(std::false_type,
                           google::protobuf::io::CodedOutputStream &cos,
                           T const &v)
{
  constexpr meta::info r = member_v<T, MI>;
  using M = typename [: meta::type_of(r) :];
  if constexpr (AI == 0)
    {
      // Ordinary member: proto3 default omission.
      if constexpr (is_omittable_v<M>)
        {
          if (is_default_value(v.[:r:]))
            return;
        }
      else if constexpr (is_plain_message_v<M>)
        {
          // Value semantics: an all-default nested struct behaves like an
          // unset message and is omitted (present-but-empty is not
          // representable).  optional/unique_ptr keep real presence.
          if (is_empty_message(v.[:r:]))
            return;
        }
      serialize_value(cos, FNO, v.[:r:]);
    }
  else
    {
      // OneOf alternative: presence semantics (set -> emit, even defaults).
      if (v.[:r:].index() != AI)
        return;
      serialize_value(cos, FNO, std::get<AI>(v.[:r:]));
    }
}

// proto2 group member (wire types 3/4).
template <typename T, std::uint32_t FNO, std::size_t MI, std::size_t AI>
void serialize_entry_group(std::true_type,
                           google::protobuf::io::CodedOutputStream &cos,
                           T const &v)
{
  constexpr meta::info r = member_v<T, MI>;
  using M = typename [: meta::type_of(r) :];
  // Emit START_GROUP only when the presence-holding member is set.  The
  // group body is the member's inner message struct, serialized directly
  // (its own members carry their field numbers).
  auto const *inner = GroupPtr<M>::get(v.[:r:]);
  if (!inner)
    return;
  if constexpr (!GroupHasPresence<M>::value)
    {
      // Bare struct group: no presence bit exists, so omit the group when
      // its body is all-default (matches plain nested-message semantics).
      if (is_empty_message(*inner))
        return;
    }
  cos.WriteTag((FNO << 3) | 3);
  serialize_to_cos(cos, *inner);
  cos.WriteTag((FNO << 3) | 4);
}

template <typename T, std::uint32_t FNO, std::size_t MI, std::size_t AI>
void serialize_entry(google::protobuf::io::CodedOutputStream &cos, T const &v)
{
  serialize_entry_group<T, FNO, MI, AI>(
      std::bool_constant<member_is_group_ann<member_v<T, MI>>()>{}, cos, v);
}

template <typename T>
void serialize_to_cos(google::protobuf::io::CodedOutputStream &cos, T const &v)
{
  // Group bodies are serialized directly through this function (bypassing
  // serialize()), so both the layout validation and the cumulative depth
  // guard must live here to cover every nested message path.
  check_layout<T>();
  counter_scope depth(serialize_depth_v);
  contract_assert(serialize_depth_v <= kMaxSerializeDepth);
  template for (constexpr auto e : std::define_static_array(field_table<T>()))
    {
      serialize_entry<T, e.fieldno, e.member, e.alt>(cos, v);
    }
  if constexpr (has_unknown_fields_v<T>)
    {
      // Unknown fields re-emit after all known fields (protobuf behavior).
      serialize_value(cos, 0,
                      v.[: member_v<T, unknown_member_index_v<T>> :]);
    }
}

template <typename T>
void serialize(std::string &out, T const &v)
{
  google::protobuf::io::StringOutputStream sos(&out);
  google::protobuf::io::CodedOutputStream cos(&sos);
  serialize_to_cos(cos, v);
}

}  // namespace rpb
