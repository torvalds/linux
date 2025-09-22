//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__verbose_abort>
#include <new>
#include <stdexcept>
#include <string>

#ifdef _LIBCPP_ABI_VCRUNTIME
#  include "support/runtime/stdexcept_vcruntime.ipp"
#else
#  include "support/runtime/stdexcept_default.ipp"
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_NORETURN void __throw_runtime_error(const char* msg) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw runtime_error(msg);
#else
  _LIBCPP_VERBOSE_ABORT("runtime_error was thrown in -fno-exceptions mode with message \"%s\"", msg);
#endif
}

_LIBCPP_END_NAMESPACE_STD
