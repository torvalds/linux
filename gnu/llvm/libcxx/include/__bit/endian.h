//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___BIT_ENDIAN_H
#define _LIBCPP___BIT_ENDIAN_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

enum class endian {
  little = 0xDEAD,
  big    = 0xFACE,
#  if defined(_LIBCPP_LITTLE_ENDIAN)
  native = little
#  elif defined(_LIBCPP_BIG_ENDIAN)
  native = big
#  else
  native = 0xCAFE
#  endif
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___BIT_ENDIAN_H
