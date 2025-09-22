//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_ARRAY_H
#define _LIBCPP___FWD_ARRAY_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, size_t _Size>
struct _LIBCPP_TEMPLATE_VIS array;

template <size_t _Ip, class _Tp, size_t _Size>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp& get(array<_Tp, _Size>&) _NOEXCEPT;

template <size_t _Ip, class _Tp, size_t _Size>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 const _Tp& get(const array<_Tp, _Size>&) _NOEXCEPT;

#ifndef _LIBCPP_CXX03_LANG
template <size_t _Ip, class _Tp, size_t _Size>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp&& get(array<_Tp, _Size>&&) _NOEXCEPT;

template <size_t _Ip, class _Tp, size_t _Size>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 const _Tp&& get(const array<_Tp, _Size>&&) _NOEXCEPT;
#endif

template <class>
struct __is_std_array : false_type {};

template <class _Tp, size_t _Size>
struct __is_std_array<array<_Tp, _Size> > : true_type {};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_ARRAY_H
