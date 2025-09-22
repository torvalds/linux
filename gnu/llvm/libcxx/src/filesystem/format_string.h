//===----------------------------------------------------------------------===////
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===////

#ifndef FILESYSTEM_FORMAT_STRING_H
#define FILESYSTEM_FORMAT_STRING_H

#include <__assert>
#include <__config>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>

#if defined(_LIBCPP_WIN32API)
#  define PATHSTR(x) (L##x)
#  define PATH_CSTR_FMT "\"%ls\""
#else
#  define PATHSTR(x) (x)
#  define PATH_CSTR_FMT "\"%s\""
#endif

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace detail {

inline _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 1, 0) string vformat_string(const char* msg, va_list ap) {
  array<char, 256> buf;

  va_list apcopy;
  va_copy(apcopy, ap);
  int ret = ::vsnprintf(buf.data(), buf.size(), msg, apcopy);
  va_end(apcopy);

  string result;
  if (static_cast<size_t>(ret) < buf.size()) {
    result.assign(buf.data(), static_cast<size_t>(ret));
  } else {
    // we did not provide a long enough buffer on our first attempt. The
    // return value is the number of bytes (excluding the null byte) that are
    // needed for formatting.
    size_t size_with_null = static_cast<size_t>(ret) + 1;
    result.__resize_default_init(size_with_null - 1);
    ret = ::vsnprintf(&result[0], size_with_null, msg, ap);
    _LIBCPP_ASSERT_INTERNAL(static_cast<size_t>(ret) == (size_with_null - 1), "TODO");
  }
  return result;
}

inline _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 1, 2) string format_string(const char* msg, ...) {
  string ret;
  va_list ap;
  va_start(ap, msg);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    ret = detail::vformat_string(msg, ap);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    va_end(ap);
    throw;
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  va_end(ap);
  return ret;
}

} // end namespace detail

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // FILESYSTEM_FORMAT_STRING_H
