//===----------------------------------------------------------------------===////
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===////

#ifndef FILESYSTEM_FILE_DESCRIPTOR_H
#define FILESYSTEM_FILE_DESCRIPTOR_H

#include <__config>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

#include "error.h"
#include "posix_compat.h"
#include "time_utils.h"

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dirent.h> // for DIR & friends
#  include <fcntl.h>  // values for fchmodat
#  include <sys/stat.h>
#  include <sys/statvfs.h>
#  include <unistd.h>
#endif // defined(_LIBCPP_WIN32API)

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace detail {

#if !defined(_LIBCPP_WIN32API)

#  if defined(DT_BLK)
template <class DirEntT, class = decltype(DirEntT::d_type)>
file_type get_file_type(DirEntT* ent, int) {
  switch (ent->d_type) {
  case DT_BLK:
    return file_type::block;
  case DT_CHR:
    return file_type::character;
  case DT_DIR:
    return file_type::directory;
  case DT_FIFO:
    return file_type::fifo;
  case DT_LNK:
    return file_type::symlink;
  case DT_REG:
    return file_type::regular;
  case DT_SOCK:
    return file_type::socket;
  // Unlike in lstat, hitting "unknown" here simply means that the underlying
  // filesystem doesn't support d_type. Report is as 'none' so we correctly
  // set the cache to empty.
  case DT_UNKNOWN:
    break;
  }
  return file_type::none;
}
#  endif // defined(DT_BLK)

template <class DirEntT>
file_type get_file_type(DirEntT*, long) {
  return file_type::none;
}

inline pair<string_view, file_type> posix_readdir(DIR* dir_stream, error_code& ec) {
  struct dirent* dir_entry_ptr = nullptr;
  errno                        = 0; // zero errno in order to detect errors
  ec.clear();
  if ((dir_entry_ptr = ::readdir(dir_stream)) == nullptr) {
    if (errno)
      ec = capture_errno();
    return {};
  } else {
    return {dir_entry_ptr->d_name, get_file_type(dir_entry_ptr, 0)};
  }
}

#else // _LIBCPP_WIN32API

inline file_type get_file_type(const WIN32_FIND_DATAW& data) {
  if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && data.dwReserved0 == IO_REPARSE_TAG_SYMLINK)
    return file_type::symlink;
  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    return file_type::directory;
  return file_type::regular;
}
inline uintmax_t get_file_size(const WIN32_FIND_DATAW& data) {
  return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) + data.nFileSizeLow;
}
inline file_time_type get_write_time(const WIN32_FIND_DATAW& data) {
  ULARGE_INTEGER tmp;
  const FILETIME& time = data.ftLastWriteTime;
  tmp.u.LowPart        = time.dwLowDateTime;
  tmp.u.HighPart       = time.dwHighDateTime;
  return file_time_type(file_time_type::duration(tmp.QuadPart));
}

#endif // !_LIBCPP_WIN32API

//                       POSIX HELPERS

using value_type  = path::value_type;
using string_type = path::string_type;

struct FileDescriptor {
  const path& name;
  int fd = -1;
  StatT m_stat;
  file_status m_status;

  template <class... Args>
  static FileDescriptor create(const path* p, error_code& ec, Args... args) {
    ec.clear();
    int fd;
#ifdef _LIBCPP_WIN32API
    // TODO: most of the filesystem implementation uses native Win32 calls
    // (mostly via posix_compat.h). However, here we use the C-runtime APIs to
    // open a file, because we subsequently pass the C-runtime fd to
    // `std::[io]fstream::__open(int fd)` in order to implement copy_file.
    //
    // Because we're calling the windows C-runtime, win32 error codes are
    // translated into C error numbers by the C runtime, and returned in errno,
    // rather than being accessible directly via GetLastError.
    //
    // Ideally copy_file should be calling the Win32 CopyFile2 function, which
    // works on paths, not open files -- at which point this FileDescriptor type
    // will no longer be needed on windows at all.
    fd = ::_wopen(p->c_str(), args...);
#else
    fd = open(p->c_str(), args...);
#endif

    if (fd == -1) {
      ec = capture_errno();
      return FileDescriptor{p};
    }
    return FileDescriptor(p, fd);
  }

  template <class... Args>
  static FileDescriptor create_with_status(const path* p, error_code& ec, Args... args) {
    FileDescriptor fd = create(p, ec, args...);
    if (!ec)
      fd.refresh_status(ec);

    return fd;
  }

  file_status get_status() const { return m_status; }
  StatT const& get_stat() const { return m_stat; }

  bool status_known() const { return filesystem::status_known(m_status); }

  file_status refresh_status(error_code& ec);

  void close() noexcept {
    if (fd != -1) {
#ifdef _LIBCPP_WIN32API
      ::_close(fd);
#else
      ::close(fd);
#endif
      // FIXME: shouldn't this return an error_code?
    }
    fd = -1;
  }

  FileDescriptor(FileDescriptor&& other)
      : name(other.name), fd(other.fd), m_stat(other.m_stat), m_status(other.m_status) {
    other.fd       = -1;
    other.m_status = file_status{};
  }

  ~FileDescriptor() { close(); }

  FileDescriptor(FileDescriptor const&)            = delete;
  FileDescriptor& operator=(FileDescriptor const&) = delete;

private:
  explicit FileDescriptor(const path* p, int descriptor = -1) : name(*p), fd(descriptor) {}
};

inline perms posix_get_perms(const StatT& st) noexcept { return static_cast<perms>(st.st_mode) & perms::mask; }

inline file_status create_file_status(error_code& m_ec, path const& p, const StatT& path_stat, error_code* ec) {
  if (ec)
    *ec = m_ec;
  if (m_ec && (m_ec.value() == ENOENT || m_ec.value() == ENOTDIR)) {
    return file_status(file_type::not_found);
  } else if (m_ec) {
    ErrorHandler<void> err("posix_stat", ec, &p);
    err.report(m_ec, "failed to determine attributes for the specified path");
    return file_status(file_type::none);
  }
  // else

  file_status fs_tmp;
  auto const mode = path_stat.st_mode;
  if (S_ISLNK(mode))
    fs_tmp.type(file_type::symlink);
  else if (S_ISREG(mode))
    fs_tmp.type(file_type::regular);
  else if (S_ISDIR(mode))
    fs_tmp.type(file_type::directory);
  else if (S_ISBLK(mode))
    fs_tmp.type(file_type::block);
  else if (S_ISCHR(mode))
    fs_tmp.type(file_type::character);
  else if (S_ISFIFO(mode))
    fs_tmp.type(file_type::fifo);
  else if (S_ISSOCK(mode))
    fs_tmp.type(file_type::socket);
  else
    fs_tmp.type(file_type::unknown);

  fs_tmp.permissions(detail::posix_get_perms(path_stat));
  return fs_tmp;
}

inline file_status posix_stat(path const& p, StatT& path_stat, error_code* ec) {
  error_code m_ec;
  if (detail::stat(p.c_str(), &path_stat) == -1)
    m_ec = detail::capture_errno();
  return create_file_status(m_ec, p, path_stat, ec);
}

inline file_status posix_stat(path const& p, error_code* ec) {
  StatT path_stat;
  return posix_stat(p, path_stat, ec);
}

inline file_status posix_lstat(path const& p, StatT& path_stat, error_code* ec) {
  error_code m_ec;
  if (detail::lstat(p.c_str(), &path_stat) == -1)
    m_ec = detail::capture_errno();
  return create_file_status(m_ec, p, path_stat, ec);
}

inline file_status posix_lstat(path const& p, error_code* ec) {
  StatT path_stat;
  return posix_lstat(p, path_stat, ec);
}

// http://pubs.opengroup.org/onlinepubs/9699919799/functions/ftruncate.html
inline bool posix_ftruncate(const FileDescriptor& fd, off_t to_size, error_code& ec) {
  if (detail::ftruncate(fd.fd, to_size) == -1) {
    ec = capture_errno();
    return true;
  }
  ec.clear();
  return false;
}

inline bool posix_fchmod(const FileDescriptor& fd, const StatT& st, error_code& ec) {
  if (detail::fchmod(fd.fd, st.st_mode) == -1) {
    ec = capture_errno();
    return true;
  }
  ec.clear();
  return false;
}

inline bool stat_equivalent(const StatT& st1, const StatT& st2) {
  return (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
}

inline file_status FileDescriptor::refresh_status(error_code& ec) {
  // FD must be open and good.
  m_status = file_status{};
  m_stat   = {};
  error_code m_ec;
  if (detail::fstat(fd, &m_stat) == -1)
    m_ec = capture_errno();
  m_status = create_file_status(m_ec, name, m_stat, &ec);
  return m_status;
}

} // end namespace detail

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // FILESYSTEM_FILE_DESCRIPTOR_H
