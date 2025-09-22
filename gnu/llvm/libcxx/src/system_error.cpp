//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__assert>
#include <__config>
#include <__verbose_abort>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <string>
#include <system_error>

#include "include/config_elast.h"

#if defined(__ANDROID__)
#  include <android/api-level.h>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace {
#if !defined(_LIBCPP_HAS_NO_THREADS)

//  GLIBC also uses 1024 as the maximum buffer size internally.
constexpr size_t strerror_buff_size = 1024;

string do_strerror_r(int ev);

#  if defined(_LIBCPP_MSVCRT_LIKE)
string do_strerror_r(int ev) {
  char buffer[strerror_buff_size];
  if (::strerror_s(buffer, strerror_buff_size, ev) == 0)
    return string(buffer);
  std::snprintf(buffer, strerror_buff_size, "unknown error %d", ev);
  return string(buffer);
}
#  else

// Only one of the two following functions will be used, depending on
// the return type of strerror_r:

// For the GNU variant, a char* return value:
__attribute__((unused)) const char* handle_strerror_r_return(char* strerror_return, char* buffer) {
  // GNU always returns a string pointer in its return value. The
  // string might point to either the input buffer, or a static
  // buffer, but we don't care which.
  return strerror_return;
}

// For the POSIX variant: an int return value.
__attribute__((unused)) const char* handle_strerror_r_return(int strerror_return, char* buffer) {
  // The POSIX variant either:
  // - fills in the provided buffer and returns 0
  // - returns a positive error value, or
  // - returns -1 and fills in errno with an error value.
  if (strerror_return == 0)
    return buffer;

  // Only handle EINVAL. Other errors abort.
  int new_errno = strerror_return == -1 ? errno : strerror_return;
  if (new_errno == EINVAL)
    return "";

  _LIBCPP_ASSERT_INTERNAL(new_errno == ERANGE, "unexpected error from ::strerror_r");
  // FIXME maybe? 'strerror_buff_size' is likely to exceed the
  // maximum error size so ERANGE shouldn't be returned.
  std::abort();
}

// This function handles both GNU and POSIX variants, dispatching to
// one of the two above functions.
string do_strerror_r(int ev) {
  char buffer[strerror_buff_size];
  // Preserve errno around the call. (The C++ standard requires that
  // system_error functions not modify errno).
  const int old_errno       = errno;
  const char* error_message = handle_strerror_r_return(::strerror_r(ev, buffer, strerror_buff_size), buffer);
  // If we didn't get any message, print one now.
  if (!error_message[0]) {
    std::snprintf(buffer, strerror_buff_size, "Unknown error %d", ev);
    error_message = buffer;
  }
  errno = old_errno;
  return string(error_message);
}
#  endif

#endif // !defined(_LIBCPP_HAS_NO_THREADS)

string make_error_str(const error_code& ec, string what_arg) {
  if (ec) {
    if (!what_arg.empty()) {
      what_arg += ": ";
    }
    what_arg += ec.message();
  }
  return what_arg;
}

string make_error_str(const error_code& ec) {
  if (ec) {
    return ec.message();
  }
  return string();
}
} // end namespace

string __do_message::message(int ev) const {
#if defined(_LIBCPP_HAS_NO_THREADS)
  return string(::strerror(ev));
#else
  return do_strerror_r(ev);
#endif
}

class _LIBCPP_HIDDEN __generic_error_category : public __do_message {
public:
  virtual const char* name() const noexcept;
  virtual string message(int ev) const;
};

const char* __generic_error_category::name() const noexcept { return "generic"; }

string __generic_error_category::message(int ev) const {
#ifdef _LIBCPP_ELAST
  if (ev > _LIBCPP_ELAST)
    return string("unspecified generic_category error");
#endif // _LIBCPP_ELAST
  return __do_message::message(ev);
}

const error_category& generic_category() noexcept {
  union AvoidDestroyingGenericCategory {
    __generic_error_category generic_error_category;
    constexpr explicit AvoidDestroyingGenericCategory() : generic_error_category() {}
    ~AvoidDestroyingGenericCategory() {}
  };
  constinit static AvoidDestroyingGenericCategory helper;
  return helper.generic_error_category;
}

class _LIBCPP_HIDDEN __system_error_category : public __do_message {
public:
  virtual const char* name() const noexcept;
  virtual string message(int ev) const;
  virtual error_condition default_error_condition(int ev) const noexcept;
};

const char* __system_error_category::name() const noexcept { return "system"; }

string __system_error_category::message(int ev) const {
#ifdef _LIBCPP_ELAST
  if (ev > _LIBCPP_ELAST)
    return string("unspecified system_category error");
#endif // _LIBCPP_ELAST
  return __do_message::message(ev);
}

error_condition __system_error_category::default_error_condition(int ev) const noexcept {
#ifdef _LIBCPP_ELAST
  if (ev > _LIBCPP_ELAST)
    return error_condition(ev, system_category());
#endif // _LIBCPP_ELAST
  return error_condition(ev, generic_category());
}

const error_category& system_category() noexcept {
  union AvoidDestroyingSystemCategory {
    __system_error_category system_error_category;
    constexpr explicit AvoidDestroyingSystemCategory() : system_error_category() {}
    ~AvoidDestroyingSystemCategory() {}
  };
  constinit static AvoidDestroyingSystemCategory helper;
  return helper.system_error_category;
}

// error_condition

string error_condition::message() const { return __cat_->message(__val_); }

// error_code

string error_code::message() const { return __cat_->message(__val_); }

// system_error

system_error::system_error(error_code ec, const string& what_arg)
    : runtime_error(make_error_str(ec, what_arg)), __ec_(ec) {}

system_error::system_error(error_code ec, const char* what_arg)
    : runtime_error(make_error_str(ec, what_arg)), __ec_(ec) {}

system_error::system_error(error_code ec) : runtime_error(make_error_str(ec)), __ec_(ec) {}

system_error::system_error(int ev, const error_category& ecat, const string& what_arg)
    : runtime_error(make_error_str(error_code(ev, ecat), what_arg)), __ec_(error_code(ev, ecat)) {}

system_error::system_error(int ev, const error_category& ecat, const char* what_arg)
    : runtime_error(make_error_str(error_code(ev, ecat), what_arg)), __ec_(error_code(ev, ecat)) {}

system_error::system_error(int ev, const error_category& ecat)
    : runtime_error(make_error_str(error_code(ev, ecat))), __ec_(error_code(ev, ecat)) {}

system_error::~system_error() noexcept {}

void __throw_system_error(int ev, const char* what_arg) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  std::__throw_system_error(error_code(ev, system_category()), what_arg);
#else
  // The above could also handle the no-exception case, but for size, avoid referencing system_category() unnecessarily.
  _LIBCPP_VERBOSE_ABORT(
      "system_error was thrown in -fno-exceptions mode with error %i and message \"%s\"", ev, what_arg);
#endif
}

_LIBCPP_END_NAMESPACE_STD
