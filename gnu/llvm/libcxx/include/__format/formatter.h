// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_H
#define _LIBCPP___FORMAT_FORMATTER_H

#include <__config>
#include <__fwd/format.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

/// The default formatter template.
///
/// [format.formatter.spec]/5
/// If F is a disabled specialization of formatter, these values are false:
/// - is_default_constructible_v<F>,
/// - is_copy_constructible_v<F>,
/// - is_move_constructible_v<F>,
/// - is_copy_assignable<F>, and
/// - is_move_assignable<F>.
template <class _Tp, class _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter {
  formatter()                            = delete;
  formatter(const formatter&)            = delete;
  formatter& operator=(const formatter&) = delete;
};

#  if _LIBCPP_STD_VER >= 23

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr void __set_debug_format(_Tp& __formatter) {
  if constexpr (requires { __formatter.set_debug_format(); })
    __formatter.set_debug_format();
}

#  endif // _LIBCPP_STD_VER >= 23
#endif   // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMATTER_H
