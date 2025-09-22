// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ITER_SWAP_H
#define _LIBCPP___ITERATOR_ITER_SWAP_H

#include <__concepts/class_or_enum.h>
#include <__concepts/swappable.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iter_move.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/readable_traits.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [iter.cust.swap]

namespace ranges {
namespace __iter_swap {
template <class _I1, class _I2>
void iter_swap(_I1, _I2) = delete;

template <class _T1, class _T2>
concept __unqualified_iter_swap =
    (__class_or_enum<remove_cvref_t<_T1>> || __class_or_enum<remove_cvref_t<_T2>>) && requires(_T1&& __x, _T2&& __y) {
      // NOLINTNEXTLINE(libcpp-robust-against-adl) iter_swap ADL calls should only be made through ranges::iter_swap
      iter_swap(std::forward<_T1>(__x), std::forward<_T2>(__y));
    };

template <class _T1, class _T2>
concept __readable_swappable =
    indirectly_readable<_T1> && indirectly_readable<_T2> &&
    swappable_with<iter_reference_t<_T1>, iter_reference_t<_T2>>;

struct __fn {
  // NOLINTBEGIN(libcpp-robust-against-adl) iter_swap ADL calls should only be made through ranges::iter_swap
  template <class _T1, class _T2>
    requires __unqualified_iter_swap<_T1, _T2>
  _LIBCPP_HIDE_FROM_ABI constexpr void operator()(_T1&& __x, _T2&& __y) const
      noexcept(noexcept(iter_swap(std::forward<_T1>(__x), std::forward<_T2>(__y)))) {
    (void)iter_swap(std::forward<_T1>(__x), std::forward<_T2>(__y));
  }
  // NOLINTEND(libcpp-robust-against-adl)

  template <class _T1, class _T2>
    requires(!__unqualified_iter_swap<_T1, _T2>) && __readable_swappable<_T1, _T2>
  _LIBCPP_HIDE_FROM_ABI constexpr void operator()(_T1&& __x, _T2&& __y) const
      noexcept(noexcept(ranges::swap(*std::forward<_T1>(__x), *std::forward<_T2>(__y)))) {
    ranges::swap(*std::forward<_T1>(__x), *std::forward<_T2>(__y));
  }

  template <class _T1, class _T2>
    requires(!__unqualified_iter_swap<_T1, _T2> &&   //
             !__readable_swappable<_T1, _T2>) &&     //
            indirectly_movable_storable<_T1, _T2> && //
            indirectly_movable_storable<_T2, _T1>
  _LIBCPP_HIDE_FROM_ABI constexpr void operator()(_T1&& __x, _T2&& __y) const
      noexcept(noexcept(iter_value_t<_T2>(ranges::iter_move(__y))) && //
               noexcept(*__y = ranges::iter_move(__x)) &&             //
               noexcept(*std::forward<_T1>(__x) = std::declval<iter_value_t<_T2>>())) {
    iter_value_t<_T2> __old(ranges::iter_move(__y));
    *__y                    = ranges::iter_move(__x);
    *std::forward<_T1>(__x) = std::move(__old);
  }
};
} // namespace __iter_swap

inline namespace __cpo {
inline constexpr auto iter_swap = __iter_swap::__fn{};
} // namespace __cpo
} // namespace ranges

template <class _I1, class _I2 = _I1>
concept indirectly_swappable =
    indirectly_readable<_I1> && indirectly_readable<_I2> && requires(const _I1 __i1, const _I2 __i2) {
      ranges::iter_swap(__i1, __i1);
      ranges::iter_swap(__i2, __i2);
      ranges::iter_swap(__i1, __i2);
      ranges::iter_swap(__i2, __i1);
    };

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_ITER_SWAP_H
