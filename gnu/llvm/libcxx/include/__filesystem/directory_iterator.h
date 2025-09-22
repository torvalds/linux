// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_DIRECTORY_ITERATOR_H
#define _LIBCPP___FILESYSTEM_DIRECTORY_ITERATOR_H

#include <__assert>
#include <__config>
#include <__filesystem/directory_entry.h>
#include <__filesystem/directory_options.h>
#include <__filesystem/path.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/iterator_traits.h>
#include <__memory/shared_ptr.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/enable_view.h>
#include <__system_error/error_code.h>
#include <__utility/move.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_FILESYSTEM)

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_PUSH

class _LIBCPP_HIDDEN __dir_stream;
class directory_iterator {
public:
  typedef directory_entry value_type;
  typedef ptrdiff_t difference_type;
  typedef value_type const* pointer;
  typedef value_type const& reference;
  typedef input_iterator_tag iterator_category;

public:
  // ctor & dtor
  _LIBCPP_HIDE_FROM_ABI directory_iterator() noexcept {}

  _LIBCPP_HIDE_FROM_ABI explicit directory_iterator(const path& __p) : directory_iterator(__p, nullptr) {}

  _LIBCPP_HIDE_FROM_ABI directory_iterator(const path& __p, directory_options __opts)
      : directory_iterator(__p, nullptr, __opts) {}

  _LIBCPP_HIDE_FROM_ABI directory_iterator(const path& __p, error_code& __ec) : directory_iterator(__p, &__ec) {}

  _LIBCPP_HIDE_FROM_ABI directory_iterator(const path& __p, directory_options __opts, error_code& __ec)
      : directory_iterator(__p, &__ec, __opts) {}

  _LIBCPP_HIDE_FROM_ABI directory_iterator(const directory_iterator&)            = default;
  _LIBCPP_HIDE_FROM_ABI directory_iterator(directory_iterator&&)                 = default;
  _LIBCPP_HIDE_FROM_ABI directory_iterator& operator=(const directory_iterator&) = default;

  _LIBCPP_HIDE_FROM_ABI directory_iterator& operator=(directory_iterator&& __o) noexcept {
    // non-default implementation provided to support self-move assign.
    if (this != &__o) {
      __imp_ = std::move(__o.__imp_);
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI ~directory_iterator() = default;

  _LIBCPP_HIDE_FROM_ABI const directory_entry& operator*() const {
    // Note: this check duplicates a check in `__dereference()`.
    _LIBCPP_ASSERT_NON_NULL(__imp_, "The end iterator cannot be dereferenced");
    return __dereference();
  }

  _LIBCPP_HIDE_FROM_ABI const directory_entry* operator->() const { return &**this; }

  _LIBCPP_HIDE_FROM_ABI directory_iterator& operator++() { return __increment(); }

  _LIBCPP_HIDE_FROM_ABI __dir_element_proxy operator++(int) {
    __dir_element_proxy __p(**this);
    __increment();
    return __p;
  }

  _LIBCPP_HIDE_FROM_ABI directory_iterator& increment(error_code& __ec) { return __increment(&__ec); }

#  if _LIBCPP_STD_VER >= 20

  _LIBCPP_HIDE_FROM_ABI bool operator==(default_sentinel_t) const noexcept { return *this == directory_iterator(); }

#  endif

private:
  inline _LIBCPP_HIDE_FROM_ABI friend bool
  operator==(const directory_iterator& __lhs, const directory_iterator& __rhs) noexcept;

  // construct the dir_stream
  _LIBCPP_EXPORTED_FROM_ABI directory_iterator(const path&, error_code*, directory_options = directory_options::none);

  _LIBCPP_EXPORTED_FROM_ABI directory_iterator& __increment(error_code* __ec = nullptr);

  _LIBCPP_EXPORTED_FROM_ABI const directory_entry& __dereference() const;

private:
  shared_ptr<__dir_stream> __imp_;
};

inline _LIBCPP_HIDE_FROM_ABI bool
operator==(const directory_iterator& __lhs, const directory_iterator& __rhs) noexcept {
  return __lhs.__imp_ == __rhs.__imp_;
}

inline _LIBCPP_HIDE_FROM_ABI bool
operator!=(const directory_iterator& __lhs, const directory_iterator& __rhs) noexcept {
  return !(__lhs == __rhs);
}

// enable directory_iterator range-based for statements
inline _LIBCPP_HIDE_FROM_ABI directory_iterator begin(directory_iterator __iter) noexcept { return __iter; }

inline _LIBCPP_HIDE_FROM_ABI directory_iterator end(directory_iterator) noexcept { return directory_iterator(); }

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_POP

_LIBCPP_END_NAMESPACE_FILESYSTEM

#  if _LIBCPP_STD_VER >= 20

template <>
_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY inline constexpr bool
    std::ranges::enable_borrowed_range<std::filesystem::directory_iterator> = true;

template <>
_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY inline constexpr bool
    std::ranges::enable_view<std::filesystem::directory_iterator> = true;

#  endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_FILESYSTEM)

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FILESYSTEM_DIRECTORY_ITERATOR_H
