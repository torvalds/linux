//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_SYNTH_THREE_WAY_H
#define _LIBCPP___COMPARE_SYNTH_THREE_WAY_H

#include <__compare/ordering.h>
#include <__compare/three_way_comparable.h>
#include <__concepts/boolean_testable.h>
#include <__config>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [expos.only.func]

_LIBCPP_HIDE_FROM_ABI inline constexpr auto __synth_three_way = []<class _Tp, class _Up>(const _Tp& __t, const _Up& __u)
  requires requires {
    { __t < __u } -> __boolean_testable;
    { __u < __t } -> __boolean_testable;
  }
{
  if constexpr (three_way_comparable_with<_Tp, _Up>) {
    return __t <=> __u;
  } else {
    if (__t < __u)
      return weak_ordering::less;
    if (__u < __t)
      return weak_ordering::greater;
    return weak_ordering::equivalent;
  }
};

template <class _Tp, class _Up = _Tp>
using __synth_three_way_result = decltype(std::__synth_three_way(std::declval<_Tp&>(), std::declval<_Up&>()));

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___COMPARE_SYNTH_THREE_WAY_H
