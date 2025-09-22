//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>
#include <__utility/unreachable.h>
#include <filesystem>
#include <system_error>

#include "format_string.h"

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

filesystem_error::~filesystem_error() {}

void filesystem_error::__create_what(int __num_paths) {
  const char* derived_what = system_error::what();
  __storage_->__what_      = [&]() -> string {
    switch (__num_paths) {
    case 0:
      return detail::format_string("filesystem error: %s", derived_what);
    case 1:
      return detail::format_string("filesystem error: %s [" PATH_CSTR_FMT "]", derived_what, path1().c_str());
    case 2:
      return detail::format_string(
          "filesystem error: %s [" PATH_CSTR_FMT "] [" PATH_CSTR_FMT "]",
          derived_what,
          path1().c_str(),
          path2().c_str());
    }
    __libcpp_unreachable();
  }();
}

_LIBCPP_END_NAMESPACE_FILESYSTEM
