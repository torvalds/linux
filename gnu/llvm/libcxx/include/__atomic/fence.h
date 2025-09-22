//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_FENCE_H
#define _LIBCPP___ATOMIC_FENCE_H

#include <__atomic/cxx_atomic_impl.h>
#include <__atomic/memory_order.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

inline _LIBCPP_HIDE_FROM_ABI void atomic_thread_fence(memory_order __m) _NOEXCEPT { __cxx_atomic_thread_fence(__m); }

inline _LIBCPP_HIDE_FROM_ABI void atomic_signal_fence(memory_order __m) _NOEXCEPT { __cxx_atomic_signal_fence(__m); }

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ATOMIC_FENCE_H
