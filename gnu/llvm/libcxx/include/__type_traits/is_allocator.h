//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_IS_ALLOCATOR_H
#define _LIBCPP___TYPE_IS_ALLOCATOR_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <typename _Alloc, typename = void, typename = void>
struct __is_allocator : false_type {};

template <typename _Alloc>
struct __is_allocator<_Alloc,
                      __void_t<typename _Alloc::value_type>,
                      __void_t<decltype(std::declval<_Alloc&>().allocate(size_t(0)))> > : true_type {};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_IS_ALLOCATOR_H
