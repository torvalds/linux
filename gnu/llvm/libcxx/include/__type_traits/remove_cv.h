//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_REMOVE_CV_H
#define _LIBCPP___TYPE_TRAITS_REMOVE_CV_H

#include <__config>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_volatile.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__remove_cv) && !defined(_LIBCPP_COMPILER_GCC)
template <class _Tp>
struct remove_cv {
  using type _LIBCPP_NODEBUG = __remove_cv(_Tp);
};

template <class _Tp>
using __remove_cv_t = __remove_cv(_Tp);
#else
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS remove_cv {
  typedef __remove_volatile_t<__remove_const_t<_Tp> > type;
};

template <class _Tp>
using __remove_cv_t = __remove_volatile_t<__remove_const_t<_Tp> >;
#endif // __has_builtin(__remove_cv)

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using remove_cv_t = __remove_cv_t<_Tp>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_REMOVE_CV_H
