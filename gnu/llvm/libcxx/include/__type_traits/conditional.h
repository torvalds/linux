//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_CONDITIONAL_H
#define _LIBCPP___TYPE_TRAITS_CONDITIONAL_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <bool>
struct _IfImpl;

template <>
struct _IfImpl<true> {
  template <class _IfRes, class _ElseRes>
  using _Select _LIBCPP_NODEBUG = _IfRes;
};

template <>
struct _IfImpl<false> {
  template <class _IfRes, class _ElseRes>
  using _Select _LIBCPP_NODEBUG = _ElseRes;
};

template <bool _Cond, class _IfRes, class _ElseRes>
using _If _LIBCPP_NODEBUG = typename _IfImpl<_Cond>::template _Select<_IfRes, _ElseRes>;

template <bool _Bp, class _If, class _Then>
struct _LIBCPP_TEMPLATE_VIS conditional {
  using type _LIBCPP_NODEBUG = _If;
};
template <class _If, class _Then>
struct _LIBCPP_TEMPLATE_VIS conditional<false, _If, _Then> {
  using type _LIBCPP_NODEBUG = _Then;
};

#if _LIBCPP_STD_VER >= 14
template <bool _Bp, class _IfRes, class _ElseRes>
using conditional_t _LIBCPP_NODEBUG = typename conditional<_Bp, _IfRes, _ElseRes>::type;
#endif

// Helper so we can use "conditional_t" in all language versions.
template <bool _Bp, class _If, class _Then>
using __conditional_t _LIBCPP_NODEBUG = typename conditional<_Bp, _If, _Then>::type;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_CONDITIONAL_H
