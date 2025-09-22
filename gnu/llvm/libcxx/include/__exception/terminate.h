//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___EXCEPTION_TERMINATE_H
#define _LIBCPP___EXCEPTION_TERMINATE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

namespace std { // purposefully not using versioning namespace
_LIBCPP_NORETURN _LIBCPP_EXPORTED_FROM_ABI void terminate() _NOEXCEPT;
} // namespace std

#endif // _LIBCPP___EXCEPTION_TERMINATE_H
