// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_TEMPORARY_BUFFER_H
#define _LIBCPP___MEMORY_TEMPORARY_BUFFER_H

#include <__config>
#include <__utility/pair.h>
#include <cstddef>
#include <new>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_CFI _LIBCPP_DEPRECATED_IN_CXX17 pair<_Tp*, ptrdiff_t>
get_temporary_buffer(ptrdiff_t __n) _NOEXCEPT {
  pair<_Tp*, ptrdiff_t> __r(0, 0);
  const ptrdiff_t __m =
      (~ptrdiff_t(0) ^ ptrdiff_t(ptrdiff_t(1) << (sizeof(ptrdiff_t) * __CHAR_BIT__ - 1))) / sizeof(_Tp);
  if (__n > __m)
    __n = __m;
  while (__n > 0) {
#if !defined(_LIBCPP_HAS_NO_ALIGNED_ALLOCATION)
    if (__is_overaligned_for_new(_LIBCPP_ALIGNOF(_Tp))) {
      align_val_t __al = align_val_t(_LIBCPP_ALIGNOF(_Tp));
      __r.first        = static_cast<_Tp*>(::operator new(__n * sizeof(_Tp), __al, nothrow));
    } else {
      __r.first = static_cast<_Tp*>(::operator new(__n * sizeof(_Tp), nothrow));
    }
#else
    if (__is_overaligned_for_new(_LIBCPP_ALIGNOF(_Tp))) {
      // Since aligned operator new is unavailable, return an empty
      // buffer rather than one with invalid alignment.
      return __r;
    }

    __r.first = static_cast<_Tp*>(::operator new(__n * sizeof(_Tp), nothrow));
#endif

    if (__r.first) {
      __r.second = __n;
      break;
    }
    __n /= 2;
  }
  return __r;
}

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_DEPRECATED_IN_CXX17 void return_temporary_buffer(_Tp* __p) _NOEXCEPT {
  std::__libcpp_deallocate_unsized((void*)__p, _LIBCPP_ALIGNOF(_Tp));
}

struct __return_temporary_buffer {
  _LIBCPP_SUPPRESS_DEPRECATED_PUSH
  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void operator()(_Tp* __p) const {
    std::return_temporary_buffer(__p);
  }
  _LIBCPP_SUPPRESS_DEPRECATED_POP
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_TEMPORARY_BUFFER_H
