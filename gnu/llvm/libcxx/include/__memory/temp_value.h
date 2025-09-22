//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_TEMP_VALUE_H
#define _LIBCPP___MEMORY_TEMP_VALUE_H

#include <__config>
#include <__memory/addressof.h>
#include <__memory/allocator_traits.h>
#include <__type_traits/aligned_storage.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Alloc>
struct __temp_value {
  typedef allocator_traits<_Alloc> _Traits;

#ifdef _LIBCPP_CXX03_LANG
  typename aligned_storage<sizeof(_Tp), _LIBCPP_ALIGNOF(_Tp)>::type __v;
#else
  union {
    _Tp __v;
  };
#endif
  _Alloc& __a;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp* __addr() {
#ifdef _LIBCPP_CXX03_LANG
    return reinterpret_cast<_Tp*>(std::addressof(__v));
#else
    return std::addressof(__v);
#endif
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp& get() { return *__addr(); }

  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_CFI _LIBCPP_CONSTEXPR_SINCE_CXX20 __temp_value(_Alloc& __alloc, _Args&&... __args)
      : __a(__alloc) {
    _Traits::construct(__a, __addr(), std::forward<_Args>(__args)...);
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 ~__temp_value() { _Traits::destroy(__a, __addr()); }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_TEMP_VALUE_H
