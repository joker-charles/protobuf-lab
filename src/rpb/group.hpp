// Proto2 group presence helpers (shared by serialize/parse).
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include "rpb/traits.hpp"

#include <memory>
#include <optional>

namespace rpb {

// Helpers to extract the inner message of a group presence-member
// (unique_ptr / optional / bare message) without relying on ``if constexpr``
// discarding (GCC 16 does not discard false branches, see AGENTS.md).
template <typename M> struct GroupInner
{
  using type = M;
};
template <typename T> struct GroupInner<std::unique_ptr<T>>
{
  using type = T;
};
template <typename T> struct GroupInner<std::optional<T>>
{
  using type = T;
};
// Presence holders (unique_ptr / optional) serialize a group whenever they
// are engaged, even if the body is empty.  A bare struct member has value
// semantics, so an all-default body is omitted like any plain nested
// message (otherwise serialize(default) would invent a START/END pair).
template <typename M> struct GroupHasPresence : std::false_type {};
template <typename T>
struct GroupHasPresence<std::unique_ptr<T>> : std::true_type {};
template <typename T>
struct GroupHasPresence<std::optional<T>> : std::true_type {};

// Read a const pointer to the group member's inner message.  Struct partial
// specialization (rather than function overloading) is used because GCC 16
// does not order the unique_ptr/optional function-template overloads above
// the generic ``M const&`` overload reliably.
template <typename M> struct GroupPtr
{
  static M const *get(M const &v)
  {
    return &v;
  }
};
template <typename T> struct GroupPtr<std::unique_ptr<T>>
{
  static T const *get(std::unique_ptr<T> const &p)
  {
    return p.get();
  }
};
template <typename T> struct GroupPtr<std::optional<T>>
{
  static T const *get(std::optional<T> const &o)
  {
    return o ? &*o : nullptr;
  }
};
// Mutable variant (parse): allocates the presence-holding member as needed.
template <typename M> struct GroupPtrMut
{
  static M *get(M &v)
  {
    return &v;
  }
};
template <typename T> struct GroupPtrMut<std::unique_ptr<T>>
{
  static T *get(std::unique_ptr<T> &p)
  {
    if (!p)
      p = std::make_unique<T>();
    return p.get();
  }
};
template <typename T> struct GroupPtrMut<std::optional<T>>
{
  static T *get(std::optional<T> &o)
  {
    if (!o)
      o.emplace();
    return &*o;
  }
};

}  // namespace rpb
