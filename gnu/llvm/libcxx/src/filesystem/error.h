//===----------------------------------------------------------------------===////
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===////

#ifndef FILESYSTEM_ERROR_H
#define FILESYSTEM_ERROR_H

#include <__assert>
#include <__config>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility> // __libcpp_unreachable

#include "format_string.h"

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h> // ERROR_* macros
#endif

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace detail {

#if defined(_LIBCPP_WIN32API)

inline errc __win_err_to_errc(int err) {
  constexpr struct {
    DWORD win;
    errc errc;
  } win_error_mapping[] = {
      {ERROR_ACCESS_DENIED, errc::permission_denied},
      {ERROR_ALREADY_EXISTS, errc::file_exists},
      {ERROR_BAD_NETPATH, errc::no_such_file_or_directory},
      {ERROR_BAD_PATHNAME, errc::no_such_file_or_directory},
      {ERROR_BAD_UNIT, errc::no_such_device},
      {ERROR_BROKEN_PIPE, errc::broken_pipe},
      {ERROR_BUFFER_OVERFLOW, errc::filename_too_long},
      {ERROR_BUSY, errc::device_or_resource_busy},
      {ERROR_BUSY_DRIVE, errc::device_or_resource_busy},
      {ERROR_CANNOT_MAKE, errc::permission_denied},
      {ERROR_CANTOPEN, errc::io_error},
      {ERROR_CANTREAD, errc::io_error},
      {ERROR_CANTWRITE, errc::io_error},
      {ERROR_CURRENT_DIRECTORY, errc::permission_denied},
      {ERROR_DEV_NOT_EXIST, errc::no_such_device},
      {ERROR_DEVICE_IN_USE, errc::device_or_resource_busy},
      {ERROR_DIR_NOT_EMPTY, errc::directory_not_empty},
      {ERROR_DIRECTORY, errc::invalid_argument},
      {ERROR_DISK_FULL, errc::no_space_on_device},
      {ERROR_FILE_EXISTS, errc::file_exists},
      {ERROR_FILE_NOT_FOUND, errc::no_such_file_or_directory},
      {ERROR_HANDLE_DISK_FULL, errc::no_space_on_device},
      {ERROR_INVALID_ACCESS, errc::permission_denied},
      {ERROR_INVALID_DRIVE, errc::no_such_device},
      {ERROR_INVALID_FUNCTION, errc::function_not_supported},
      {ERROR_INVALID_HANDLE, errc::invalid_argument},
      {ERROR_INVALID_NAME, errc::no_such_file_or_directory},
      {ERROR_INVALID_PARAMETER, errc::invalid_argument},
      {ERROR_LOCK_VIOLATION, errc::no_lock_available},
      {ERROR_LOCKED, errc::no_lock_available},
      {ERROR_NEGATIVE_SEEK, errc::invalid_argument},
      {ERROR_NOACCESS, errc::permission_denied},
      {ERROR_NOT_ENOUGH_MEMORY, errc::not_enough_memory},
      {ERROR_NOT_READY, errc::resource_unavailable_try_again},
      {ERROR_NOT_SAME_DEVICE, errc::cross_device_link},
      {ERROR_NOT_SUPPORTED, errc::not_supported},
      {ERROR_OPEN_FAILED, errc::io_error},
      {ERROR_OPEN_FILES, errc::device_or_resource_busy},
      {ERROR_OPERATION_ABORTED, errc::operation_canceled},
      {ERROR_OUTOFMEMORY, errc::not_enough_memory},
      {ERROR_PATH_NOT_FOUND, errc::no_such_file_or_directory},
      {ERROR_READ_FAULT, errc::io_error},
      {ERROR_REPARSE_TAG_INVALID, errc::invalid_argument},
      {ERROR_RETRY, errc::resource_unavailable_try_again},
      {ERROR_SEEK, errc::io_error},
      {ERROR_SHARING_VIOLATION, errc::permission_denied},
      {ERROR_TOO_MANY_OPEN_FILES, errc::too_many_files_open},
      {ERROR_WRITE_FAULT, errc::io_error},
      {ERROR_WRITE_PROTECT, errc::permission_denied},
  };

  for (const auto& pair : win_error_mapping)
    if (pair.win == static_cast<DWORD>(err))
      return pair.errc;
  return errc::invalid_argument;
}

#endif // _LIBCPP_WIN32API

inline error_code capture_errno() {
  _LIBCPP_ASSERT_INTERNAL(errno != 0, "Expected errno to be non-zero");
  return error_code(errno, generic_category());
}

#if defined(_LIBCPP_WIN32API)
inline error_code make_windows_error(int err) { return make_error_code(__win_err_to_errc(err)); }
#endif

template <class T>
T error_value();
template <>
inline constexpr void error_value<void>() {}
template <>
inline bool error_value<bool>() {
  return false;
}
#if __SIZEOF_SIZE_T__ != __SIZEOF_LONG_LONG__
template <>
inline size_t error_value<size_t>() {
  return size_t(-1);
}
#endif
template <>
inline uintmax_t error_value<uintmax_t>() {
  return uintmax_t(-1);
}
template <>
inline constexpr file_time_type error_value<file_time_type>() {
  return file_time_type::min();
}
template <>
inline path error_value<path>() {
  return {};
}

template <class T>
struct ErrorHandler {
  const char* func_name_;
  error_code* ec_ = nullptr;
  const path* p1_ = nullptr;
  const path* p2_ = nullptr;

  ErrorHandler(const char* fname, error_code* ec, const path* p1 = nullptr, const path* p2 = nullptr)
      : func_name_(fname), ec_(ec), p1_(p1), p2_(p2) {
    if (ec_)
      ec_->clear();
  }

  T report(const error_code& ec) const {
    if (ec_) {
      *ec_ = ec;
      return error_value<T>();
    }
    string what = string("in ") + func_name_;
    switch (bool(p1_) + bool(p2_)) {
    case 0:
      __throw_filesystem_error(what, ec);
    case 1:
      __throw_filesystem_error(what, *p1_, ec);
    case 2:
      __throw_filesystem_error(what, *p1_, *p2_, ec);
    }
    __libcpp_unreachable();
  }

  _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 3, 0)
  void report_impl(const error_code& ec, const char* msg, va_list ap) const {
    if (ec_) {
      *ec_ = ec;
      return;
    }
    string what = string("in ") + func_name_ + ": " + detail::vformat_string(msg, ap);
    switch (bool(p1_) + bool(p2_)) {
    case 0:
      __throw_filesystem_error(what, ec);
    case 1:
      __throw_filesystem_error(what, *p1_, ec);
    case 2:
      __throw_filesystem_error(what, *p1_, *p2_, ec);
    }
    __libcpp_unreachable();
  }

  _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 3, 4)
  T report(const error_code& ec, const char* msg, ...) const {
    va_list ap;
    va_start(ap, msg);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
      report_impl(ec, msg, ap);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    } catch (...) {
      va_end(ap);
      throw;
    }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    va_end(ap);
    return error_value<T>();
  }

  T report(errc const& err) const { return report(make_error_code(err)); }

  _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 3, 4)
  T report(errc const& err, const char* msg, ...) const {
    va_list ap;
    va_start(ap, msg);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
      report_impl(make_error_code(err), msg, ap);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    } catch (...) {
      va_end(ap);
      throw;
    }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    va_end(ap);
    return error_value<T>();
  }

private:
  ErrorHandler(ErrorHandler const&)            = delete;
  ErrorHandler& operator=(ErrorHandler const&) = delete;
};

} // end namespace detail

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // FILESYSTEM_ERROR_H
