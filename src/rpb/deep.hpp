// Reflection-driven deep equality / deep copy (test helpers).
//
// Part of the rpb header-only codec; include "codec.hpp" instead of
// this file directly.
#pragma once

#include "rpb/annotations.hpp"
#include "rpb/traits.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rpb {

// --- deep equality (test helper) ---------------------------------------
// unique_ptr members are dereferenced recursively; strings / containers /
// enums use operator==; variants compare by index and then recurse into the
// active alternative (variant::operator== would compare unique_ptr
// alternatives by pointer); anything else (messages, wire wrappers) is
// compared member-wise via reflection.  Lets message structs with
// unique_ptr members keep a one-line operator== without writing out every
// member.

template <typename T> bool deep_equal(T const &a, T const &b);

template <typename T>
bool deep_equal_message(T const &a, T const &b)
{
  bool ok = true;
  template for (constexpr auto m :
                std::define_static_array(
                    meta::nonstatic_data_members_of(^^T, members_ctx())))
    {
      if (!deep_equal(a.[:m:], b.[:m:]))
        ok = false;
    }
  return ok;
}

template <typename T>
bool deep_equal(T const &a, T const &b)
{
  if constexpr (is_unique_ptr_v<T>)
    {
      if (!a && !b)
        return true;
      return a && b && deep_equal(*a, *b);
    }
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>
                     || std::is_same_v<T, std::string>
                     || std::is_same_v<T, UnknownFields>)
    return a == b;
  else if constexpr (std::is_same_v<T, std::vector<bool>>)
    return a == b;  // bit proxies have no reference type to recurse into
  else if constexpr (is_vector_v<T>)
    {
      if (a.size() != b.size())
        return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!deep_equal(a[i], b[i]))
          return false;
      return true;
    }
  else if constexpr (is_map_v<T>)
    {
      if (a.size() != b.size())
        return false;
      auto ia = a.begin();
      auto ib = b.begin();
      for (; ia != a.end(); ++ia, ++ib)
        if (!(ia->first == ib->first) || !deep_equal(ia->second, ib->second))
          return false;
      return true;
    }
  else if constexpr (is_optional_v<T>)
    {
      if (a.has_value() != b.has_value())
        return false;
      return !a.has_value() || deep_equal(*a, *b);
    }
  else if constexpr (is_one_of_v<T>)
    {
      if (a.index() != b.index())
        return false;
      if (a.index() == 0)
        return true;  // both monostate (unset)
      return std::visit(
          [&](auto const &x) {
            return deep_equal(
                x, std::get<std::remove_cvref_t<decltype(x)>>(b));
          },
          a);
    }
  else
    return deep_equal_message(a, b);
}

// --- deep copy (test helper) -------------------------------------------
// Value-semantics copy for messages with unique_ptr members (mirror of
// deep_equal): containers/variants/optionals are rebuilt element-wise so
// move-only message members deep-copy; unique_ptr members allocate fresh
// pointees; everything else copies as-is.  Gives structs with unique_ptr
// members protobuf-style deep-copy semantics without hand-written copy
// constructors.

template <typename T> T deep_copy(T const &src);

template <typename T>
T deep_copy_message(T const &src)
{
  T dst;
  template for (constexpr auto m :
                std::define_static_array(
                    meta::nonstatic_data_members_of(^^T, members_ctx())))
    {
      dst.[:m:] = deep_copy(src.[:m:]);
    }
  return dst;
}

template <typename T>
T deep_copy(T const &src)
{
  if constexpr (is_unique_ptr_v<T>)
    {
      if (!src)
        return nullptr;
      using E = typename T::element_type;
      return std::make_unique<E>(deep_copy(*src));
    }
  else if constexpr (std::is_same_v<T, std::vector<bool>>)
    return src;  // bit-proxy elements; plain copy is exact
  else if constexpr (is_vector_v<T>)
    {
      T dst;
      dst.reserve(src.size());
      for (auto const &e : src)
        dst.push_back(deep_copy(e));
      return dst;
    }
  else if constexpr (is_map_v<T>)
    {
      T dst;
      for (auto const &kv : src)
        dst.emplace(kv.first, deep_copy(kv.second));
      return dst;
    }
  else if constexpr (is_optional_v<T>)
    {
      if (!src)
        return std::nullopt;
      return std::optional<typename T::value_type>(deep_copy(*src));
    }
  else if constexpr (is_one_of_v<T>)
    {
      if (src.index() == 0)
        return T{};  // monostate (unset)
      return std::visit(
          [](auto const &x) { return T(deep_copy(x)); }, src);
    }
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>
                     || std::is_same_v<T, std::string>
                     || std::is_same_v<T, UnknownFields>)
    return src;
  else
    return deep_copy_message(src);
}

}  // namespace rpb
