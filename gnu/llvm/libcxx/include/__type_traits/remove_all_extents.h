//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_REMOVE_ALL_EXTENTS_H
#define _LIBCPP___TYPE_TRAITS_REMOVE_ALL_EXTENTS_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__remove_all_extents)
template <class _Tp>
struct remove_all_extents {
  using type _LIBCPP_NODEBUG = __remove_all_extents(_Tp);
};

template <class _Tp>
using __remove_all_extents_t = __remove_all_extents(_Tp);
#else
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS remove_all_extents {
  typedef _Tp type;
};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS remove_all_extents<_Tp[]> {
  typedef typename remove_all_extents<_Tp>::type type;
};
template <class _Tp, size_t _Np>
struct _LIBCPP_TEMPLATE_VIS remove_all_extents<_Tp[_Np]> {
  typedef typename remove_all_extents<_Tp>::type type;
};

template <class _Tp>
using __remove_all_extents_t = typename remove_all_extents<_Tp>::type;
#endif // __has_builtin(__remove_all_extents)

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using remove_all_extents_t = __remove_all_extents_t<_Tp>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_REMOVE_ALL_EXTENTS_H
