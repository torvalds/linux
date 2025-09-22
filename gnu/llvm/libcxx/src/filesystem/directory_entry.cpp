//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>
#include <cstdint>
#include <filesystem>
#include <system_error>

#include "file_descriptor.h"
#include "posix_compat.h"
#include "time_utils.h"

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

error_code directory_entry::__do_refresh() noexcept {
  __data_.__reset();
  error_code failure_ec;

  detail::StatT full_st;
  file_status st = detail::posix_lstat(__p_, full_st, &failure_ec);
  if (!status_known(st)) {
    __data_.__reset();
    return failure_ec;
  }

  if (!filesystem::exists(st) || !filesystem::is_symlink(st)) {
    __data_.__cache_type_    = directory_entry::_RefreshNonSymlink;
    __data_.__type_          = st.type();
    __data_.__non_sym_perms_ = st.permissions();
  } else { // we have a symlink
    __data_.__sym_perms_ = st.permissions();
    // Get the information about the linked entity.
    // Ignore errors from stat, since we don't want errors regarding symlink
    // resolution to be reported to the user.
    error_code ignored_ec;
    st = detail::posix_stat(__p_, full_st, &ignored_ec);

    __data_.__type_          = st.type();
    __data_.__non_sym_perms_ = st.permissions();

    // If we failed to resolve the link, then only partially populate the
    // cache.
    if (!status_known(st)) {
      __data_.__cache_type_ = directory_entry::_RefreshSymlinkUnresolved;
      return error_code{};
    }
    // Otherwise, we resolved the link, potentially as not existing.
    // That's OK.
    __data_.__cache_type_ = directory_entry::_RefreshSymlink;
  }

  if (filesystem::is_regular_file(st))
    __data_.__size_ = static_cast<uintmax_t>(full_st.st_size);

  if (filesystem::exists(st)) {
    __data_.__nlink_ = static_cast<uintmax_t>(full_st.st_nlink);

    // Attempt to extract the mtime, and fail if it's not representable using
    // file_time_type. For now we ignore the error, as we'll report it when
    // the value is actually used.
    error_code ignored_ec;
    __data_.__write_time_ = detail::__extract_last_write_time(__p_, full_st, &ignored_ec);
  }

  return failure_ec;
}

_LIBCPP_END_NAMESPACE_FILESYSTEM
