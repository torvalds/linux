//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_REMOVE_REFERENCE_H
#define _LIBCPP___TYPE_TRAITS_REMOVE_REFERENCE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__remove_reference_t)
template <class _Tp>
struct remove_reference {
  using type _LIBCPP_NODEBUG = __remove_reference_t(_Tp);
};

template <class _Tp>
using __libcpp_remove_reference_t = __remove_reference_t(_Tp);
#elif __has_builtin(__remove_reference)
template <class _Tp>
struct remove_reference {
  using type _LIBCPP_NODEBUG = __remove_reference(_Tp);
};

template <class _Tp>
using __libcpp_remove_reference_t = typename remove_reference<_Tp>::type;
#else
#  error "remove_reference not implemented!"
#endif // __has_builtin(__remove_reference_t)

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using remove_reference_t = __libcpp_remove_reference_t<_Tp>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_REMOVE_REFERENCE_H
