// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_SPECIALIZATION
#define _LIBCPP___TYPE_TRAITS_IS_SPECIALIZATION

// This contains parts of P2098R1 but is based on MSVC STL's implementation.
//
// The paper has been rejected
//   We will not pursue P2098R0 (std::is_specialization_of) at this time; we'd
//   like to see a solution to this problem, but it requires language evolution
//   too.
//
// Since it is expected a real solution will be provided in the future only the
// minimal part is implemented.
//
// Note a cvref qualified _Tp is never considered a specialization.

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _Tp, template <class...> class _Template>
inline constexpr bool __is_specialization_v = false; // true if and only if _Tp is a specialization of _Template

template <template <class...> class _Template, class... _Args>
inline constexpr bool __is_specialization_v<_Template<_Args...>, _Template> = true;

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_SPECIALIZATION
