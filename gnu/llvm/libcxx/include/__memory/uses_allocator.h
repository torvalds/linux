// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_USES_ALLOCATOR_H
#define _LIBCPP___MEMORY_USES_ALLOCATOR_H

#include <__config>
#include <__type_traits/is_convertible.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct __has_allocator_type {
private:
  template <class _Up>
  static false_type __test(...);
  template <class _Up>
  static true_type __test(typename _Up::allocator_type* = 0);

public:
  static const bool value = decltype(__test<_Tp>(0))::value;
};

template <class _Tp, class _Alloc, bool = __has_allocator_type<_Tp>::value>
struct __uses_allocator : public integral_constant<bool, is_convertible<_Alloc, typename _Tp::allocator_type>::value> {
};

template <class _Tp, class _Alloc>
struct __uses_allocator<_Tp, _Alloc, false> : public false_type {};

template <class _Tp, class _Alloc>
struct _LIBCPP_TEMPLATE_VIS uses_allocator : public __uses_allocator<_Tp, _Alloc> {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp, class _Alloc>
inline constexpr bool uses_allocator_v = uses_allocator<_Tp, _Alloc>::value;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_USES_ALLOCATOR_H
