// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_FORMAT_H
#define _LIBCPP___FWD_FORMAT_H

#include <__config>
#include <__iterator/concepts.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Context>
class _LIBCPP_TEMPLATE_VIS basic_format_arg;

template <class _OutIt, class _CharT>
  requires output_iterator<_OutIt, const _CharT&>
class _LIBCPP_TEMPLATE_VIS basic_format_context;

template <class _Tp, class _CharT = char>
struct _LIBCPP_TEMPLATE_VIS formatter;

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_FORMAT_H
