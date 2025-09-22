//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_REL_OPS_H
#define _LIBCPP___UTILITY_REL_OPS_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace rel_ops {

template <class _Tp>
inline _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_HIDE_FROM_ABI bool operator!=(const _Tp& __x, const _Tp& __y) {
  return !(__x == __y);
}

template <class _Tp>
inline _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_HIDE_FROM_ABI bool operator>(const _Tp& __x, const _Tp& __y) {
  return __y < __x;
}

template <class _Tp>
inline _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_HIDE_FROM_ABI bool operator<=(const _Tp& __x, const _Tp& __y) {
  return !(__y < __x);
}

template <class _Tp>
inline _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_HIDE_FROM_ABI bool operator>=(const _Tp& __x, const _Tp& __y) {
  return !(__x < __y);
}

} // namespace rel_ops

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_REL_OPS_H
