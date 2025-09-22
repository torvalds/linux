// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_IDENTITY_H
#define _LIBCPP___FUNCTIONAL_IDENTITY_H

#include <__config>
#include <__fwd/functional.h>
#include <__type_traits/integral_constant.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct __is_identity : false_type {};

struct __identity {
  template <class _Tp>
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Tp&& operator()(_Tp&& __t) const _NOEXCEPT {
    return std::forward<_Tp>(__t);
  }

  using is_transparent = void;
};

template <>
struct __is_identity<__identity> : true_type {};
template <>
struct __is_identity<reference_wrapper<__identity> > : true_type {};
template <>
struct __is_identity<reference_wrapper<const __identity> > : true_type {};

#if _LIBCPP_STD_VER >= 20

struct identity {
  template <class _Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Tp&& operator()(_Tp&& __t) const noexcept {
    return std::forward<_Tp>(__t);
  }

  using is_transparent = void;
};

template <>
struct __is_identity<identity> : true_type {};
template <>
struct __is_identity<reference_wrapper<identity> > : true_type {};
template <>
struct __is_identity<reference_wrapper<const identity> > : true_type {};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_IDENTITY_H
