// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_BINDER1ST_H
#define _LIBCPP___FUNCTIONAL_BINDER1ST_H

#include <__config>
#include <__functional/unary_function.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_BINDERS)

template <class _Operation>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 binder1st
    : public __unary_function<typename _Operation::second_argument_type, typename _Operation::result_type> {
protected:
  _Operation op;
  typename _Operation::first_argument_type value;

public:
  _LIBCPP_HIDE_FROM_ABI binder1st(const _Operation& __x, const typename _Operation::first_argument_type __y)
      : op(__x), value(__y) {}
  _LIBCPP_HIDE_FROM_ABI typename _Operation::result_type
  operator()(typename _Operation::second_argument_type& __x) const {
    return op(value, __x);
  }
  _LIBCPP_HIDE_FROM_ABI typename _Operation::result_type
  operator()(const typename _Operation::second_argument_type& __x) const {
    return op(value, __x);
  }
};

template <class _Operation, class _Tp>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI binder1st<_Operation>
bind1st(const _Operation& __op, const _Tp& __x) {
  return binder1st<_Operation>(__op, __x);
}

#endif // _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_BINDERS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_BINDER1ST_H
