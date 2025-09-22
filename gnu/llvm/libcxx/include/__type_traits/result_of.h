//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_RESULT_OF_H
#define _LIBCPP___TYPE_TRAITS_RESULT_OF_H

#include <__config>
#include <__functional/invoke.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// result_of

#if _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS)
template <class _Callable>
class _LIBCPP_DEPRECATED_IN_CXX17 result_of;

template <class _Fp, class... _Args>
class _LIBCPP_TEMPLATE_VIS result_of<_Fp(_Args...)> : public __invoke_of<_Fp, _Args...> {};

#  if _LIBCPP_STD_VER >= 14
template <class _Tp>
using result_of_t _LIBCPP_DEPRECATED_IN_CXX17 = typename result_of<_Tp>::type;
#  endif // _LIBCPP_STD_VER >= 14
#endif   // _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_RESULT_OF_H
