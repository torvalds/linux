//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ALLOCATOR_DESTRUCTOR_H
#define _LIBCPP___MEMORY_ALLOCATOR_DESTRUCTOR_H

#include <__config>
#include <__memory/allocator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Alloc>
class __allocator_destructor {
  typedef _LIBCPP_NODEBUG allocator_traits<_Alloc> __alloc_traits;

public:
  typedef _LIBCPP_NODEBUG typename __alloc_traits::pointer pointer;
  typedef _LIBCPP_NODEBUG typename __alloc_traits::size_type size_type;

private:
  _Alloc& __alloc_;
  size_type __s_;

public:
  _LIBCPP_HIDE_FROM_ABI __allocator_destructor(_Alloc& __a, size_type __s) _NOEXCEPT : __alloc_(__a), __s_(__s) {}
  _LIBCPP_HIDE_FROM_ABI void operator()(pointer __p) _NOEXCEPT { __alloc_traits::deallocate(__alloc_, __p, __s_); }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_ALLOCATOR_DESTRUCTOR_H
