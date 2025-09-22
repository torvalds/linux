//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_IS_ALWAYS_LOCK_FREE_H
#define _LIBCPP___ATOMIC_IS_ALWAYS_LOCK_FREE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct __libcpp_is_always_lock_free {
  // __atomic_always_lock_free is available in all Standard modes
  static const bool __value = __atomic_always_lock_free(sizeof(_Tp), nullptr);
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ATOMIC_IS_ALWAYS_LOCK_FREE_H
