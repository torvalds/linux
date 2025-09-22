// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMAT_ERROR_H
#define _LIBCPP___FORMAT_FORMAT_ERROR_H

#include <__config>
#include <__verbose_abort>
#include <stdexcept>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

_LIBCPP_DIAGNOSTIC_PUSH
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wweak-vtables")
class _LIBCPP_EXPORTED_FROM_ABI format_error : public runtime_error {
public:
  _LIBCPP_HIDE_FROM_ABI explicit format_error(const string& __s) : runtime_error(__s) {}
  _LIBCPP_HIDE_FROM_ABI explicit format_error(const char* __s) : runtime_error(__s) {}
  _LIBCPP_HIDE_FROM_ABI format_error(const format_error&)            = default;
  _LIBCPP_HIDE_FROM_ABI format_error& operator=(const format_error&) = default;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL
  ~format_error() noexcept override = default;
};
_LIBCPP_DIAGNOSTIC_POP

_LIBCPP_NORETURN inline _LIBCPP_HIDE_FROM_ABI void __throw_format_error(const char* __s) {
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw format_error(__s);
#  else
  _LIBCPP_VERBOSE_ABORT("format_error was thrown in -fno-exceptions mode with message \"%s\"", __s);
#  endif
}

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMAT_ERROR_H
