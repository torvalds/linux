//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>

#include <cstdlib>
#include <print>

#include <__system_error/system_error.h>

#include "filesystem/error.h"

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <io.h>
#  include <windows.h>
#elif __has_include(<unistd.h>)
#  include <unistd.h>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if defined(_LIBCPP_WIN32API)

_LIBCPP_EXPORTED_FROM_ABI bool __is_windows_terminal(FILE* __stream) {
  // Note the Standard does this in one call, but it's unclear whether
  // an invalid handle is allowed when calling GetConsoleMode.
  //
  // https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/get-osfhandle?view=msvc-170
  // https://learn.microsoft.com/en-us/windows/console/getconsolemode
  intptr_t __handle = _get_osfhandle(fileno(__stream));
  if (__handle == -1)
    return false;

  unsigned long __mode;
  return GetConsoleMode(reinterpret_cast<void*>(__handle), &__mode);
}

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
_LIBCPP_EXPORTED_FROM_ABI void
__write_to_windows_console([[maybe_unused]] FILE* __stream, [[maybe_unused]] wstring_view __view) {
  // https://learn.microsoft.com/en-us/windows/console/writeconsole
  if (WriteConsoleW(reinterpret_cast<void*>(_get_osfhandle(fileno(__stream))), // clang-format aid
                    __view.data(),
                    __view.size(),
                    nullptr,
                    nullptr) == 0) {
    __throw_system_error(filesystem::detail::make_windows_error(GetLastError()), "failed to write formatted output");
  }
}
#  endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#elif __has_include(<unistd.h>) // !_LIBCPP_WIN32API

_LIBCPP_EXPORTED_FROM_ABI bool __is_posix_terminal(FILE* __stream) { return isatty(fileno(__stream)); }
#endif

_LIBCPP_END_NAMESPACE_STD
