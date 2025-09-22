//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ALLOCATE_AT_LEAST_H
#define _LIBCPP___MEMORY_ALLOCATE_AT_LEAST_H

#include <__config>
#include <__memory/allocator_traits.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _Alloc>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto __allocate_at_least(_Alloc& __alloc, size_t __n) {
  return std::allocator_traits<_Alloc>::allocate_at_least(__alloc, __n);
}

#else

template <class _Pointer>
struct __allocation_result {
  _Pointer ptr;
  size_t count;
};

template <class _Alloc>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI
_LIBCPP_CONSTEXPR __allocation_result<typename allocator_traits<_Alloc>::pointer>
__allocate_at_least(_Alloc& __alloc, size_t __n) {
  return {__alloc.allocate(__n), __n};
}

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_ALLOCATE_AT_LEAST_H
