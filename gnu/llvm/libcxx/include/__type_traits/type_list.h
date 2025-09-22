//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_TYPE_LIST_H
#define _LIBCPP___TYPE_TRAITS_TYPE_LIST_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Hp, class _Tp>
struct __type_list {
  typedef _Hp _Head;
  typedef _Tp _Tail;
};

template <class _TypeList, size_t _Size, bool = _Size <= sizeof(typename _TypeList::_Head)>
struct __find_first;

template <class _Hp, class _Tp, size_t _Size>
struct __find_first<__type_list<_Hp, _Tp>, _Size, true> {
  typedef _LIBCPP_NODEBUG _Hp type;
};

template <class _Hp, class _Tp, size_t _Size>
struct __find_first<__type_list<_Hp, _Tp>, _Size, false> {
  typedef _LIBCPP_NODEBUG typename __find_first<_Tp, _Size>::type type;
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_TYPE_LIST_H
