//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_KILL_DEPENDENCY_H
#define _LIBCPP___ATOMIC_KILL_DEPENDENCY_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _Tp kill_dependency(_Tp __y) _NOEXCEPT {
  return __y;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ATOMIC_KILL_DEPENDENCY_H
