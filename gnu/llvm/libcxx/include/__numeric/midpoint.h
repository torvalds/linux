// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NUMERIC_MIDPOINT_H
#define _LIBCPP___NUMERIC_MIDPOINT_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_floating_point.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_null_pointer.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_void.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/remove_pointer.h>
#include <cstddef>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr enable_if_t<is_integral_v<_Tp> && !is_same_v<bool, _Tp> && !is_null_pointer_v<_Tp>, _Tp>
midpoint(_Tp __a, _Tp __b) noexcept _LIBCPP_DISABLE_UBSAN_UNSIGNED_INTEGER_CHECK {
  using _Up                = make_unsigned_t<_Tp>;
  constexpr _Up __bitshift = numeric_limits<_Up>::digits - 1;

  _Up __diff     = _Up(__b) - _Up(__a);
  _Up __sign_bit = __b < __a;

  _Up __half_diff = (__diff / 2) + (__sign_bit << __bitshift) + (__sign_bit & __diff);

  return __a + __half_diff;
}

template <class _Tp, enable_if_t<is_object_v<_Tp> && !is_void_v<_Tp> && (sizeof(_Tp) > 0), int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp* midpoint(_Tp* __a, _Tp* __b) noexcept {
  return __a + std::midpoint(ptrdiff_t(0), __b - __a);
}

template <typename _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr int __sign(_Tp __val) {
  return (_Tp(0) < __val) - (__val < _Tp(0));
}

template <typename _Fp>
_LIBCPP_HIDE_FROM_ABI constexpr _Fp __fp_abs(_Fp __f) {
  return __f >= 0 ? __f : -__f;
}

template <class _Fp>
_LIBCPP_HIDE_FROM_ABI constexpr enable_if_t<is_floating_point_v<_Fp>, _Fp> midpoint(_Fp __a, _Fp __b) noexcept {
  constexpr _Fp __lo = numeric_limits<_Fp>::min() * 2;
  constexpr _Fp __hi = numeric_limits<_Fp>::max() / 2;

  // typical case: overflow is impossible
  if (std::__fp_abs(__a) <= __hi && std::__fp_abs(__b) <= __hi)
    return (__a + __b) / 2; // always correctly rounded
  if (std::__fp_abs(__a) < __lo)
    return __a + __b / 2; // not safe to halve a
  if (std::__fp_abs(__b) < __lo)
    return __a / 2 + __b; // not safe to halve b

  return __a / 2 + __b / 2; // otherwise correctly rounded
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___NUMERIC_MIDPOINT_H
