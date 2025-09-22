//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___EXCEPTION_EXCEPTION_H
#define _LIBCPP___EXCEPTION_EXCEPTION_H

#include <__config>

// <vcruntime_exception.h> defines its own std::exception and std::bad_exception types,
// which we use in order to be ABI-compatible with other STLs on Windows.
#if defined(_LIBCPP_ABI_VCRUNTIME)
#  include <vcruntime_exception.h>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

namespace std { // purposefully not using versioning namespace

#if defined(_LIBCPP_ABI_VCRUNTIME) && (!defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0)
// The std::exception class was already included above, but we're explicit about this condition here for clarity.

#elif defined(_LIBCPP_ABI_VCRUNTIME) && _HAS_EXCEPTIONS == 0
// However, <vcruntime_exception.h> does not define std::exception and std::bad_exception
// when _HAS_EXCEPTIONS == 0.
//
// Since libc++ still wants to provide the std::exception hierarchy even when _HAS_EXCEPTIONS == 0
// (after all those are simply types like any other), we define an ABI-compatible version
// of the VCRuntime std::exception and std::bad_exception types in that mode.

struct __std_exception_data {
  char const* _What;
  bool _DoFree;
};

class exception { // base of all library exceptions
public:
  exception() _NOEXCEPT : __data_() {}

  explicit exception(char const* __message) _NOEXCEPT : __data_() {
    __data_._What   = __message;
    __data_._DoFree = true;
  }

  exception(exception const&) _NOEXCEPT {}

  exception& operator=(exception const&) _NOEXCEPT { return *this; }

  virtual ~exception() _NOEXCEPT {}

  virtual char const* what() const _NOEXCEPT { return __data_._What ? __data_._What : "Unknown exception"; }

private:
  __std_exception_data __data_;
};

class bad_exception : public exception {
public:
  bad_exception() _NOEXCEPT : exception("bad exception") {}
};

#else  // !defined(_LIBCPP_ABI_VCRUNTIME)
// On all other platforms, we define our own std::exception and std::bad_exception types
// regardless of whether exceptions are turned on as a language feature.

class _LIBCPP_EXPORTED_FROM_ABI exception {
public:
  _LIBCPP_HIDE_FROM_ABI exception() _NOEXCEPT {}
  _LIBCPP_HIDE_FROM_ABI exception(const exception&) _NOEXCEPT            = default;
  _LIBCPP_HIDE_FROM_ABI exception& operator=(const exception&) _NOEXCEPT = default;

  virtual ~exception() _NOEXCEPT;
  virtual const char* what() const _NOEXCEPT;
};

class _LIBCPP_EXPORTED_FROM_ABI bad_exception : public exception {
public:
  _LIBCPP_HIDE_FROM_ABI bad_exception() _NOEXCEPT {}
  _LIBCPP_HIDE_FROM_ABI bad_exception(const bad_exception&) _NOEXCEPT            = default;
  _LIBCPP_HIDE_FROM_ABI bad_exception& operator=(const bad_exception&) _NOEXCEPT = default;
  ~bad_exception() _NOEXCEPT override;
  const char* what() const _NOEXCEPT override;
};
#endif // !_LIBCPP_ABI_VCRUNTIME

} // namespace std

#endif // _LIBCPP___EXCEPTION_EXCEPTION_H
