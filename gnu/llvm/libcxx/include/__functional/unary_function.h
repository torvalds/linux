//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_UNARY_FUNCTION_H
#define _LIBCPP___FUNCTIONAL_UNARY_FUNCTION_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)

template <class _Arg, class _Result>
struct _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 unary_function {
  typedef _Arg argument_type;
  typedef _Result result_type;
};

#endif // _LIBCPP_STD_VER <= 14

template <class _Arg, class _Result>
struct __unary_function_keep_layout_base {
#if _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_BINDER_TYPEDEFS)
  using argument_type _LIBCPP_DEPRECATED_IN_CXX17 = _Arg;
  using result_type _LIBCPP_DEPRECATED_IN_CXX17   = _Result;
#endif
};

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)
_LIBCPP_DIAGNOSTIC_PUSH
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wdeprecated-declarations")
template <class _Arg, class _Result>
using __unary_function = unary_function<_Arg, _Result>;
_LIBCPP_DIAGNOSTIC_POP
#else
template <class _Arg, class _Result>
using __unary_function = __unary_function_keep_layout_base<_Arg, _Result>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_UNARY_FUNCTION_H
