// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ASSUME_ALIGNED_H
#define _LIBCPP___MEMORY_ASSUME_ALIGNED_H

#include <__assert>
#include <__config>
#include <__type_traits/is_constant_evaluated.h>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <size_t _Np, class _Tp>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp* __assume_aligned(_Tp* __ptr) {
  static_assert(_Np != 0 && (_Np & (_Np - 1)) == 0, "std::assume_aligned<N>(p) requires N to be a power of two");

  if (__libcpp_is_constant_evaluated()) {
    (void)__builtin_assume_aligned(__ptr, _Np);
    return __ptr;
  } else {
    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        reinterpret_cast<uintptr_t>(__ptr) % _Np == 0, "Alignment assumption is violated");
    return static_cast<_Tp*>(__builtin_assume_aligned(__ptr, _Np));
  }
}

#if _LIBCPP_STD_VER >= 20

template <size_t _Np, class _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Tp* assume_aligned(_Tp* __ptr) {
  return std::__assume_aligned<_Np>(__ptr);
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_ASSUME_ALIGNED_H
