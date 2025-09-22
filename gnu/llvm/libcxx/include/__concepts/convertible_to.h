//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_CONVERTIBLE_TO_H
#define _LIBCPP___CONCEPTS_CONVERTIBLE_TO_H

#include <__config>
#include <__type_traits/is_convertible.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.convertible]

template <class _From, class _To>
concept convertible_to = is_convertible_v<_From, _To> && requires { static_cast<_To>(std::declval<_From>()); };

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_CONVERTIBLE_TO_H
