//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_INVOCABLE_H
#define _LIBCPP___CONCEPTS_INVOCABLE_H

#include <__config>
#include <__functional/invoke.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.invocable]

template <class _Fn, class... _Args>
concept invocable = requires(_Fn&& __fn, _Args&&... __args) {
  std::invoke(std::forward<_Fn>(__fn), std::forward<_Args>(__args)...); // not required to be equality preserving
};

// [concept.regular.invocable]

template <class _Fn, class... _Args>
concept regular_invocable = invocable<_Fn, _Args...>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_INVOCABLE_H
