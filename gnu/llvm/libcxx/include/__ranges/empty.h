// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_EMPTY_H
#define _LIBCPP___RANGES_EMPTY_H

#include <__concepts/class_or_enum.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__ranges/access.h>
#include <__ranges/size.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [range.prim.empty]

namespace ranges {
namespace __empty {
template <class _Tp>
concept __member_empty = requires(_Tp&& __t) { bool(__t.empty()); };

template <class _Tp>
concept __can_invoke_size = !__member_empty<_Tp> && requires(_Tp&& __t) { ranges::size(__t); };

template <class _Tp>
concept __can_compare_begin_end = !__member_empty<_Tp> && !__can_invoke_size<_Tp> && requires(_Tp&& __t) {
  bool(ranges::begin(__t) == ranges::end(__t));
  { ranges::begin(__t) } -> forward_iterator;
};

struct __fn {
  template <__member_empty _Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(_Tp&& __t) const noexcept(noexcept(bool(__t.empty()))) {
    return bool(__t.empty());
  }

  template <__can_invoke_size _Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(_Tp&& __t) const noexcept(noexcept(ranges::size(__t))) {
    return ranges::size(__t) == 0;
  }

  template <__can_compare_begin_end _Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(_Tp&& __t) const
      noexcept(noexcept(bool(ranges::begin(__t) == ranges::end(__t)))) {
    return ranges::begin(__t) == ranges::end(__t);
  }
};
} // namespace __empty

inline namespace __cpo {
inline constexpr auto empty = __empty::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_EMPTY_H
