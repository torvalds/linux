// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ITER_MOVE_H
#define _LIBCPP___ITERATOR_ITER_MOVE_H

#include <__concepts/class_or_enum.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_reference.h>
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

// [iterator.cust.move]

namespace ranges {
namespace __iter_move {

void iter_move() = delete;

template <class _Tp>
concept __unqualified_iter_move = __class_or_enum<remove_cvref_t<_Tp>> && requires(_Tp&& __t) {
  // NOLINTNEXTLINE(libcpp-robust-against-adl) iter_swap ADL calls should only be made through ranges::iter_swap
  iter_move(std::forward<_Tp>(__t));
};

template <class _Tp>
concept __move_deref = !__unqualified_iter_move<_Tp> && requires(_Tp&& __t) {
  *__t;
  requires is_lvalue_reference_v<decltype(*__t)>;
};

template <class _Tp>
concept __just_deref = !__unqualified_iter_move<_Tp> && !__move_deref<_Tp> && requires(_Tp&& __t) {
  *__t;
  requires(!is_lvalue_reference_v<decltype(*__t)>);
};

// [iterator.cust.move]

struct __fn {
  // NOLINTBEGIN(libcpp-robust-against-adl) iter_move ADL calls should only be made through ranges::iter_move
  template <class _Ip>
    requires __unqualified_iter_move<_Ip>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator()(_Ip&& __i) const
      noexcept(noexcept(iter_move(std::forward<_Ip>(__i)))) {
    return iter_move(std::forward<_Ip>(__i));
  }
  // NOLINTEND(libcpp-robust-against-adl)

  template <class _Ip>
    requires __move_deref<_Ip>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Ip&& __i) const
      noexcept(noexcept(std::move(*std::forward<_Ip>(__i)))) -> decltype(std::move(*std::forward<_Ip>(__i))) {
    return std::move(*std::forward<_Ip>(__i));
  }

  template <class _Ip>
    requires __just_deref<_Ip>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Ip&& __i) const
      noexcept(noexcept(*std::forward<_Ip>(__i))) -> decltype(*std::forward<_Ip>(__i)) {
    return *std::forward<_Ip>(__i);
  }
};
} // namespace __iter_move

inline namespace __cpo {
inline constexpr auto iter_move = __iter_move::__fn{};
} // namespace __cpo
} // namespace ranges

template <__dereferenceable _Tp>
  requires requires(_Tp& __t) {
    { ranges::iter_move(__t) } -> __can_reference;
  }
using iter_rvalue_reference_t = decltype(ranges::iter_move(std::declval<_Tp&>()));

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_ITER_MOVE_H
