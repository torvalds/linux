//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_BOOLEAN_TESTABLE_H
#define _LIBCPP___CONCEPTS_BOOLEAN_TESTABLE_H

#include <__concepts/convertible_to.h>
#include <__config>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concepts.booleantestable]

template <class _Tp>
concept __boolean_testable_impl = convertible_to<_Tp, bool>;

template <class _Tp>
concept __boolean_testable = __boolean_testable_impl<_Tp> && requires(_Tp&& __t) {
  { !std::forward<_Tp>(__t) } -> __boolean_testable_impl;
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_BOOLEAN_TESTABLE_H
