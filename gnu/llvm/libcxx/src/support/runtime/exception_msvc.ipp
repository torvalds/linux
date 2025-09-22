// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_ABI_MICROSOFT
#  error this header can only be used when targeting the MSVC ABI
#endif

#include <stdio.h>
#include <stdlib.h>

extern "C" {
typedef void(__cdecl* terminate_handler)();
_LIBCPP_CRT_FUNC terminate_handler __cdecl set_terminate(terminate_handler _NewTerminateHandler) throw();
_LIBCPP_CRT_FUNC terminate_handler __cdecl _get_terminate();

typedef void(__cdecl* unexpected_handler)();
unexpected_handler __cdecl set_unexpected(unexpected_handler _NewUnexpectedHandler) throw();
unexpected_handler __cdecl _get_unexpected();

int __cdecl __uncaught_exceptions();
}

namespace std {

unexpected_handler set_unexpected(unexpected_handler func) noexcept { return ::set_unexpected(func); }

unexpected_handler get_unexpected() noexcept { return ::_get_unexpected(); }

_LIBCPP_NORETURN void unexpected() {
  (*get_unexpected())();
  // unexpected handler should not return
  terminate();
}

terminate_handler set_terminate(terminate_handler func) noexcept { return ::set_terminate(func); }

terminate_handler get_terminate() noexcept { return ::_get_terminate(); }

_LIBCPP_NORETURN void terminate() noexcept {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    (*get_terminate())();
    // handler should not return
    fprintf(stderr, "terminate_handler unexpectedly returned\n");
    ::abort();
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    // handler should not throw exception
    fprintf(stderr, "terminate_handler unexpectedly threw an exception\n");
    ::abort();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
}

bool uncaught_exception() noexcept { return uncaught_exceptions() > 0; }

int uncaught_exceptions() noexcept { return __uncaught_exceptions(); }

#if !defined(_LIBCPP_ABI_VCRUNTIME)
bad_cast::bad_cast() noexcept {}

bad_cast::~bad_cast() noexcept {}

const char* bad_cast::what() const noexcept { return "std::bad_cast"; }

bad_typeid::bad_typeid() noexcept {}

bad_typeid::~bad_typeid() noexcept {}

const char* bad_typeid::what() const noexcept { return "std::bad_typeid"; }

exception::~exception() noexcept {}

const char* exception::what() const noexcept { return "std::exception"; }

bad_exception::~bad_exception() noexcept {}

const char* bad_exception::what() const noexcept { return "std::bad_exception"; }

bad_alloc::bad_alloc() noexcept {}

bad_alloc::~bad_alloc() noexcept {}

const char* bad_alloc::what() const noexcept { return "std::bad_alloc"; }

bad_array_new_length::bad_array_new_length() noexcept {}

bad_array_new_length::~bad_array_new_length() noexcept {}

const char* bad_array_new_length::what() const noexcept { return "bad_array_new_length"; }
#endif // !_LIBCPP_ABI_VCRUNTIME

} // namespace std
