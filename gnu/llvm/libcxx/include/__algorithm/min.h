//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MIN_H
#define _LIBCPP___ALGORITHM_MIN_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/min_element.h>
#include <__config>
#include <initializer_list>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 const _Tp&
min(_LIBCPP_LIFETIMEBOUND const _Tp& __a, _LIBCPP_LIFETIMEBOUND const _Tp& __b, _Compare __comp) {
  return __comp(__b, __a) ? __b : __a;
}

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 const _Tp&
min(_LIBCPP_LIFETIMEBOUND const _Tp& __a, _LIBCPP_LIFETIMEBOUND const _Tp& __b) {
  return std::min(__a, __b, __less<>());
}

#ifndef _LIBCPP_CXX03_LANG

template <class _Tp, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp
min(initializer_list<_Tp> __t, _Compare __comp) {
  return *std::__min_element<__comp_ref_type<_Compare> >(__t.begin(), __t.end(), __comp);
}

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp min(initializer_list<_Tp> __t) {
  return *std::min_element(__t.begin(), __t.end(), __less<>());
}

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_MIN_H
