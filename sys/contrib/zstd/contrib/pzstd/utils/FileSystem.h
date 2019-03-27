/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include "utils/Range.h"

#include <sys/stat.h>
#include <cerrno>
#include <cstdint>
#include <system_error>

// A small subset of `std::filesystem`.
// `std::filesystem` should be a drop in replacement.
// See http://en.cppreference.com/w/cpp/filesystem for documentation.

namespace pzstd {

// using file_status = ... causes gcc to emit a false positive warning
#if defined(_MSC_VER)
typedef struct ::_stat64 file_status;
#else
typedef struct ::stat file_status;
#endif

/// http://en.cppreference.com/w/cpp/filesystem/status
inline file_status status(StringPiece path, std::error_code& ec) noexcept {
  file_status status;
#if defined(_MSC_VER)
  const auto error = ::_stat64(path.data(), &status);
#else
  const auto error = ::stat(path.data(), &status);
#endif
  if (error) {
    ec.assign(errno, std::generic_category());
  } else {
    ec.clear();
  }
  return status;
}

/// http://en.cppreference.com/w/cpp/filesystem/is_regular_file
inline bool is_regular_file(file_status status) noexcept {
#if defined(S_ISREG)
  return S_ISREG(status.st_mode);
#elif !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
  return (status.st_mode & S_IFMT) == S_IFREG;
#else
  static_assert(false, "No POSIX stat() support.");
#endif
}

/// http://en.cppreference.com/w/cpp/filesystem/is_regular_file
inline bool is_regular_file(StringPiece path, std::error_code& ec) noexcept {
  return is_regular_file(status(path, ec));
}

/// http://en.cppreference.com/w/cpp/filesystem/is_directory
inline bool is_directory(file_status status) noexcept {
#if defined(S_ISDIR)
  return S_ISDIR(status.st_mode);
#elif !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
  return (status.st_mode & S_IFMT) == S_IFDIR;
#else
  static_assert(false, "NO POSIX stat() support.");
#endif
}

/// http://en.cppreference.com/w/cpp/filesystem/is_directory
inline bool is_directory(StringPiece path, std::error_code& ec) noexcept {
  return is_directory(status(path, ec));
}

/// http://en.cppreference.com/w/cpp/filesystem/file_size
inline std::uintmax_t file_size(
    StringPiece path,
    std::error_code& ec) noexcept {
  auto stat = status(path, ec);
  if (ec) {
    return -1;
  }
  if (!is_regular_file(stat)) {
    ec.assign(ENOTSUP, std::generic_category());
    return -1;
  }
  ec.clear();
  return stat.st_size;
}
}
