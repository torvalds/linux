// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdio>

namespace std {

static constinit std::terminate_handler __terminate_handler   = nullptr;
static constinit std::unexpected_handler __unexpected_handler = nullptr;

// libcxxrt provides implementations of these functions itself.
unexpected_handler set_unexpected(unexpected_handler func) noexcept {
  return __libcpp_atomic_exchange(&__unexpected_handler, func);
}

unexpected_handler get_unexpected() noexcept { return __libcpp_atomic_load(&__unexpected_handler); }

_LIBCPP_NORETURN void unexpected() {
  (*get_unexpected())();
  // unexpected handler should not return
  terminate();
}

terminate_handler set_terminate(terminate_handler func) noexcept {
  return __libcpp_atomic_exchange(&__terminate_handler, func);
}

terminate_handler get_terminate() noexcept { return __libcpp_atomic_load(&__terminate_handler); }

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

int uncaught_exceptions() noexcept {
#warning uncaught_exception not yet implemented
  fprintf(stderr, "uncaught_exceptions not yet implemented\n");
  ::abort();
}

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

bad_cast::bad_cast() noexcept {}

bad_typeid::bad_typeid() noexcept {}

bad_cast::~bad_cast() noexcept {}

const char* bad_cast::what() const noexcept { return "std::bad_cast"; }

bad_typeid::~bad_typeid() noexcept {}

const char* bad_typeid::what() const noexcept { return "std::bad_typeid"; }

} // namespace std
