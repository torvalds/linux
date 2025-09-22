//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MINMAX_H
#define _LIBCPP___ALGORITHM_MINMAX_H

#include <__algorithm/comp.h>
#include <__algorithm/minmax_element.h>
#include <__config>
#include <__functional/identity.h>
#include <__type_traits/is_callable.h>
#include <__utility/pair.h>
#include <initializer_list>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<const _Tp&, const _Tp&>
minmax(_LIBCPP_LIFETIMEBOUND const _Tp& __a, _LIBCPP_LIFETIMEBOUND const _Tp& __b, _Compare __comp) {
  return __comp(__b, __a) ? pair<const _Tp&, const _Tp&>(__b, __a) : pair<const _Tp&, const _Tp&>(__a, __b);
}

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<const _Tp&, const _Tp&>
minmax(_LIBCPP_LIFETIMEBOUND const _Tp& __a, _LIBCPP_LIFETIMEBOUND const _Tp& __b) {
  return std::minmax(__a, __b, __less<>());
}

#ifndef _LIBCPP_CXX03_LANG

template <class _Tp, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Tp, _Tp>
minmax(initializer_list<_Tp> __t, _Compare __comp) {
  static_assert(__is_callable<_Compare, _Tp, _Tp>::value, "The comparator has to be callable");
  __identity __proj;
  auto __ret = std::__minmax_element_impl(__t.begin(), __t.end(), __comp, __proj);
  return pair<_Tp, _Tp>(*__ret.first, *__ret.second);
}

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Tp, _Tp>
minmax(initializer_list<_Tp> __t) {
  return std::minmax(__t, __less<>());
}

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_MINMAX_H
