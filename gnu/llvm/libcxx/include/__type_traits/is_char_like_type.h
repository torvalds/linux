//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_CHAR_LIKE_TYPE_H
#define _LIBCPP___TYPE_TRAITS_IS_CHAR_LIKE_TYPE_H

#include <__config>
#include <__type_traits/conjunction.h>
#include <__type_traits/is_standard_layout.h>
#include <__type_traits/is_trivial.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _CharT>
using _IsCharLikeType = _And<is_standard_layout<_CharT>, is_trivial<_CharT> >;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_CHAR_LIKE_TYPE_H
