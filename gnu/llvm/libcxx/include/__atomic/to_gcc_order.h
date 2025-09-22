//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_TO_GCC_ORDER_H
#define _LIBCPP___ATOMIC_TO_GCC_ORDER_H

#include <__atomic/memory_order.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if defined(__ATOMIC_RELAXED) && defined(__ATOMIC_CONSUME) && defined(__ATOMIC_ACQUIRE) &&                             \
    defined(__ATOMIC_RELEASE) && defined(__ATOMIC_ACQ_REL) && defined(__ATOMIC_SEQ_CST)

_LIBCPP_HIDE_FROM_ABI inline _LIBCPP_CONSTEXPR int __to_gcc_order(memory_order __order) {
  // Avoid switch statement to make this a constexpr.
  return __order == memory_order_relaxed
           ? __ATOMIC_RELAXED
           : (__order == memory_order_acquire
                  ? __ATOMIC_ACQUIRE
                  : (__order == memory_order_release
                         ? __ATOMIC_RELEASE
                         : (__order == memory_order_seq_cst
                                ? __ATOMIC_SEQ_CST
                                : (__order == memory_order_acq_rel ? __ATOMIC_ACQ_REL : __ATOMIC_CONSUME))));
}

_LIBCPP_HIDE_FROM_ABI inline _LIBCPP_CONSTEXPR int __to_gcc_failure_order(memory_order __order) {
  // Avoid switch statement to make this a constexpr.
  return __order == memory_order_relaxed
           ? __ATOMIC_RELAXED
           : (__order == memory_order_acquire
                  ? __ATOMIC_ACQUIRE
                  : (__order == memory_order_release
                         ? __ATOMIC_RELAXED
                         : (__order == memory_order_seq_cst
                                ? __ATOMIC_SEQ_CST
                                : (__order == memory_order_acq_rel ? __ATOMIC_ACQUIRE : __ATOMIC_CONSUME))));
}

#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ATOMIC_TO_GCC_ORDER_H
