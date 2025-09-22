// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_UNARY_NEGATE_H
#define _LIBCPP___FUNCTIONAL_UNARY_NEGATE_H

#include <__config>
#include <__functional/unary_function.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_NEGATORS)

template <class _Predicate>
class _LIBCPP_TEMPLATE_VIS
_LIBCPP_DEPRECATED_IN_CXX17 unary_negate : public __unary_function<typename _Predicate::argument_type, bool> {
  _Predicate __pred_;

public:
  _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI explicit unary_negate(const _Predicate& __pred)
      : __pred_(__pred) {}
  _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI bool
  operator()(const typename _Predicate::argument_type& __x) const {
    return !__pred_(__x);
  }
};

template <class _Predicate>
_LIBCPP_DEPRECATED_IN_CXX17 inline _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI unary_negate<_Predicate>
not1(const _Predicate& __pred) {
  return unary_negate<_Predicate>(__pred);
}

#endif // _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_NEGATORS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_UNARY_NEGATE_H
