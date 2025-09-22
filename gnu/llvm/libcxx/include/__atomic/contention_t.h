//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_CONTENTION_T_H
#define _LIBCPP___ATOMIC_CONTENTION_T_H

#include <__atomic/cxx_atomic_impl.h>
#include <__config>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if defined(__linux__) || (defined(_AIX) && !defined(__64BIT__))
using __cxx_contention_t = int32_t;
#else
using __cxx_contention_t = int64_t;
#endif // __linux__ || (_AIX && !__64BIT__)

using __cxx_atomic_contention_t = __cxx_atomic_impl<__cxx_contention_t>;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ATOMIC_CONTENTION_T_H
