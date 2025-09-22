//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_REMOVE_POINTER_H
#define _LIBCPP___TYPE_TRAITS_REMOVE_POINTER_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if !defined(_LIBCPP_WORKAROUND_OBJCXX_COMPILER_INTRINSICS) && __has_builtin(__remove_pointer)
template <class _Tp>
struct remove_pointer {
  using type _LIBCPP_NODEBUG = __remove_pointer(_Tp);
};

#  ifdef _LIBCPP_COMPILER_GCC
template <class _Tp>
using __remove_pointer_t = typename remove_pointer<_Tp>::type;
#  else
template <class _Tp>
using __remove_pointer_t = __remove_pointer(_Tp);
#  endif
#else
// clang-format off
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS remove_pointer                      {typedef _LIBCPP_NODEBUG _Tp type;};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS remove_pointer<_Tp*>                {typedef _LIBCPP_NODEBUG _Tp type;};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS remove_pointer<_Tp* const>          {typedef _LIBCPP_NODEBUG _Tp type;};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS remove_pointer<_Tp* volatile>       {typedef _LIBCPP_NODEBUG _Tp type;};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS remove_pointer<_Tp* const volatile> {typedef _LIBCPP_NODEBUG _Tp type;};
// clang-format on

template <class _Tp>
using __remove_pointer_t = typename remove_pointer<_Tp>::type;
#endif // !defined(_LIBCPP_WORKAROUND_OBJCXX_COMPILER_INTRINSICS) && __has_builtin(__remove_pointer)

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using remove_pointer_t = __remove_pointer_t<_Tp>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_REMOVE_POINTER_H
