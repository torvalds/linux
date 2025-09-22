//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_TYPE_IDENTITY_H
#define _LIBCPP___TYPE_TRAITS_TYPE_IDENTITY_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct __type_identity {
  typedef _Tp type;
};

template <class _Tp>
using __type_identity_t _LIBCPP_NODEBUG = typename __type_identity<_Tp>::type;

#if _LIBCPP_STD_VER >= 20
template <class _Tp>
struct type_identity {
  typedef _Tp type;
};
template <class _Tp>
using type_identity_t = typename type_identity<_Tp>::type;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_TYPE_IDENTITY_H
