//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_DESTRUCT_N_H
#define _LIBCPP___MEMORY_DESTRUCT_N_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_trivially_destructible.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

struct __destruct_n {
private:
  size_t __size_;

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void __process(_Tp* __p, false_type) _NOEXCEPT {
    for (size_t __i = 0; __i < __size_; ++__i, ++__p)
      __p->~_Tp();
  }

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void __process(_Tp*, true_type) _NOEXCEPT {}

  _LIBCPP_HIDE_FROM_ABI void __incr(false_type) _NOEXCEPT { ++__size_; }
  _LIBCPP_HIDE_FROM_ABI void __incr(true_type) _NOEXCEPT {}

  _LIBCPP_HIDE_FROM_ABI void __set(size_t __s, false_type) _NOEXCEPT { __size_ = __s; }
  _LIBCPP_HIDE_FROM_ABI void __set(size_t, true_type) _NOEXCEPT {}

public:
  _LIBCPP_HIDE_FROM_ABI explicit __destruct_n(size_t __s) _NOEXCEPT : __size_(__s) {}

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void __incr() _NOEXCEPT {
    __incr(integral_constant<bool, is_trivially_destructible<_Tp>::value>());
  }

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void __set(size_t __s, _Tp*) _NOEXCEPT {
    __set(__s, integral_constant<bool, is_trivially_destructible<_Tp>::value>());
  }

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void operator()(_Tp* __p) _NOEXCEPT {
    __process(__p, integral_constant<bool, is_trivially_destructible<_Tp>::value>());
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_DESTRUCT_N_H
