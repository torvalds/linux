//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_COMPLEX_H
#define _LIBCPP___FWD_COMPLEX_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
class _LIBCPP_TEMPLATE_VIS complex;

#if _LIBCPP_STD_VER >= 26

template <size_t _Ip, class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp& get(complex<_Tp>&) noexcept;

template <size_t _Ip, class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp&& get(complex<_Tp>&&) noexcept;

template <size_t _Ip, class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr const _Tp& get(const complex<_Tp>&) noexcept;

template <size_t _Ip, class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr const _Tp&& get(const complex<_Tp>&&) noexcept;

#endif // _LIBCPP_STD_VER >= 26

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_COMPLEX_H
