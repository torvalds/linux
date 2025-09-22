// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/*
    stddef.h synopsis

Macros:

    offsetof(type,member-designator)
    NULL

Types:

    ptrdiff_t
    size_t
    max_align_t // C++11
    nullptr_t

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// Note: This include is outside of header guards because we sometimes get included multiple times
//       with different defines and the underlying <stddef.h> will know how to deal with that.
#include_next <stddef.h>

#ifndef _LIBCPP_STDDEF_H
#  define _LIBCPP_STDDEF_H

#  ifdef __cplusplus
typedef decltype(nullptr) nullptr_t;
#  endif

#endif // _LIBCPP_STDDEF_H
