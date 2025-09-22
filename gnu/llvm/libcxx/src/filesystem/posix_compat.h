//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// POSIX-like portability helper functions.
//
// These generally behave like the proper posix functions, with these
// exceptions:
// On Windows, they take paths in wchar_t* form, instead of char* form.
// The symlink() function is split into two frontends, symlink_file()
// and symlink_dir().
//
// These are provided within an anonymous namespace within the detail
// namespace - callers need to include this header and call them as
// detail::function(), regardless of platform.
//

#ifndef POSIX_COMPAT_H
#define POSIX_COMPAT_H

#include <__assert>
#include <__config>
#include <filesystem>

#include "error.h"
#include "time_utils.h"

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <io.h>
#  include <windows.h>
#  include <winioctl.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/statvfs.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif
#include <stdlib.h>
#include <time.h>

#if defined(_LIBCPP_WIN32API)
// This struct isn't defined in the normal Windows SDK, but only in the
// Windows Driver Kit.
struct LIBCPP_REPARSE_DATA_BUFFER {
  unsigned long ReparseTag;
  unsigned short ReparseDataLength;
  unsigned short Reserved;
  union {
    struct {
      unsigned short SubstituteNameOffset;
      unsigned short SubstituteNameLength;
      unsigned short PrintNameOffset;
      unsigned short PrintNameLength;
      unsigned long Flags;
      wchar_t PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      unsigned short SubstituteNameOffset;
      unsigned short SubstituteNameLength;
      unsigned short PrintNameOffset;
      unsigned short PrintNameLength;
      wchar_t PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      unsigned char DataBuffer[1];
    } GenericReparseBuffer;
  };
};
#endif

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace detail {

#if defined(_LIBCPP_WIN32API)

// Various C runtime header sets provide more or less of these. As we
// provide our own implementation, undef all potential defines from the
// C runtime headers and provide a complete set of macros of our own.

#  undef _S_IFMT
#  undef _S_IFDIR
#  undef _S_IFCHR
#  undef _S_IFIFO
#  undef _S_IFREG
#  undef _S_IFBLK
#  undef _S_IFLNK
#  undef _S_IFSOCK

#  define _S_IFMT 0xF000
#  define _S_IFDIR 0x4000
#  define _S_IFCHR 0x2000
#  define _S_IFIFO 0x1000
#  define _S_IFREG 0x8000
#  define _S_IFBLK 0x6000
#  define _S_IFLNK 0xA000
#  define _S_IFSOCK 0xC000

#  undef S_ISDIR
#  undef S_ISFIFO
#  undef S_ISCHR
#  undef S_ISREG
#  undef S_ISLNK
#  undef S_ISBLK
#  undef S_ISSOCK

#  define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#  define S_ISCHR(m) (((m) & _S_IFMT) == _S_IFCHR)
#  define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#  define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#  define S_ISBLK(m) (((m) & _S_IFMT) == _S_IFBLK)
#  define S_ISLNK(m) (((m) & _S_IFMT) == _S_IFLNK)
#  define S_ISSOCK(m) (((m) & _S_IFMT) == _S_IFSOCK)

#  define O_NONBLOCK 0

inline int set_errno(int e = GetLastError()) {
  errno = static_cast<int>(__win_err_to_errc(e));
  return -1;
}

class WinHandle {
public:
  WinHandle(const wchar_t* p, DWORD access, DWORD flags) {
    h = CreateFileW(
        p,
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | flags,
        nullptr);
  }
  ~WinHandle() {
    if (h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
  operator HANDLE() const { return h; }
  operator bool() const { return h != INVALID_HANDLE_VALUE; }

private:
  HANDLE h;
};

inline int stat_handle(HANDLE h, StatT* buf) {
  FILE_BASIC_INFO basic;
  if (!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)))
    return set_errno();
  memset(buf, 0, sizeof(*buf));
  buf->st_mtim = filetime_to_timespec(basic.LastWriteTime);
  buf->st_atim = filetime_to_timespec(basic.LastAccessTime);
  buf->st_mode = 0555; // Read-only
  if (!(basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
    buf->st_mode |= 0222; // Write
  if (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    buf->st_mode |= _S_IFDIR;
  } else {
    buf->st_mode |= _S_IFREG;
  }
  if (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    FILE_ATTRIBUTE_TAG_INFO tag;
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)))
      return set_errno();
    if (tag.ReparseTag == IO_REPARSE_TAG_SYMLINK)
      buf->st_mode = (buf->st_mode & ~_S_IFMT) | _S_IFLNK;
  }
  FILE_STANDARD_INFO standard;
  if (!GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof(standard)))
    return set_errno();
  buf->st_nlink = standard.NumberOfLinks;
  buf->st_size  = standard.EndOfFile.QuadPart;
  BY_HANDLE_FILE_INFORMATION info;
  if (!GetFileInformationByHandle(h, &info))
    return set_errno();
  buf->st_dev = info.dwVolumeSerialNumber;
  memcpy(&buf->st_ino.id[0], &info.nFileIndexHigh, 4);
  memcpy(&buf->st_ino.id[4], &info.nFileIndexLow, 4);
  return 0;
}

inline int stat_file(const wchar_t* path, StatT* buf, DWORD flags) {
  WinHandle h(path, FILE_READ_ATTRIBUTES, flags);
  if (!h)
    return set_errno();
  int ret = stat_handle(h, buf);
  return ret;
}

inline int stat(const wchar_t* path, StatT* buf) { return stat_file(path, buf, 0); }

inline int lstat(const wchar_t* path, StatT* buf) { return stat_file(path, buf, FILE_FLAG_OPEN_REPARSE_POINT); }

inline int fstat(int fd, StatT* buf) {
  HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  return stat_handle(h, buf);
}

inline int mkdir(const wchar_t* path, int permissions) {
  (void)permissions;
  if (!CreateDirectoryW(path, nullptr))
    return set_errno();
  return 0;
}

inline int symlink_file_dir(const wchar_t* oldname, const wchar_t* newname, bool is_dir) {
  path dest(oldname);
  dest.make_preferred();
  oldname     = dest.c_str();
  DWORD flags = is_dir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  if (CreateSymbolicLinkW(newname, oldname, flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    return 0;
  int e = GetLastError();
  if (e != ERROR_INVALID_PARAMETER)
    return set_errno(e);
  if (CreateSymbolicLinkW(newname, oldname, flags))
    return 0;
  return set_errno();
}

inline int symlink_file(const wchar_t* oldname, const wchar_t* newname) {
  return symlink_file_dir(oldname, newname, false);
}

inline int symlink_dir(const wchar_t* oldname, const wchar_t* newname) {
  return symlink_file_dir(oldname, newname, true);
}

inline int link(const wchar_t* oldname, const wchar_t* newname) {
  if (CreateHardLinkW(newname, oldname, nullptr))
    return 0;
  return set_errno();
}

inline int remove(const wchar_t* path) {
  detail::WinHandle h(path, DELETE, FILE_FLAG_OPEN_REPARSE_POINT);
  if (!h)
    return set_errno();
  FILE_DISPOSITION_INFO info;
  info.DeleteFile = TRUE;
  if (!SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info)))
    return set_errno();
  return 0;
}

inline int truncate_handle(HANDLE h, off_t length) {
  LARGE_INTEGER size_param;
  size_param.QuadPart = length;
  if (!SetFilePointerEx(h, size_param, 0, FILE_BEGIN))
    return set_errno();
  if (!SetEndOfFile(h))
    return set_errno();
  return 0;
}

inline int ftruncate(int fd, off_t length) {
  HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  return truncate_handle(h, length);
}

inline int truncate(const wchar_t* path, off_t length) {
  detail::WinHandle h(path, GENERIC_WRITE, 0);
  if (!h)
    return set_errno();
  return truncate_handle(h, length);
}

inline int rename(const wchar_t* from, const wchar_t* to) {
  if (!(MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)))
    return set_errno();
  return 0;
}

inline int chdir(const wchar_t* path) {
  if (!SetCurrentDirectoryW(path))
    return set_errno();
  return 0;
}

struct StatVFS {
  uint64_t f_frsize;
  uint64_t f_blocks;
  uint64_t f_bfree;
  uint64_t f_bavail;
};

inline int statvfs(const wchar_t* p, StatVFS* buf) {
  path dir = p;
  while (true) {
    error_code local_ec;
    const file_status st = status(dir, local_ec);
    if (!exists(st) || is_directory(st))
      break;
    path parent = dir.parent_path();
    if (parent == dir) {
      errno = ENOENT;
      return -1;
    }
    dir = parent;
  }
  ULARGE_INTEGER free_bytes_available_to_caller, total_number_of_bytes, total_number_of_free_bytes;
  if (!GetDiskFreeSpaceExW(
          dir.c_str(), &free_bytes_available_to_caller, &total_number_of_bytes, &total_number_of_free_bytes))
    return set_errno();
  buf->f_frsize = 1;
  buf->f_blocks = total_number_of_bytes.QuadPart;
  buf->f_bfree  = total_number_of_free_bytes.QuadPart;
  buf->f_bavail = free_bytes_available_to_caller.QuadPart;
  return 0;
}

inline wchar_t* getcwd([[maybe_unused]] wchar_t* in_buf, [[maybe_unused]] size_t in_size) {
  // Only expected to be used with us allocating the buffer.
  _LIBCPP_ASSERT_INTERNAL(in_buf == nullptr, "Windows getcwd() assumes in_buf==nullptr");
  _LIBCPP_ASSERT_INTERNAL(in_size == 0, "Windows getcwd() assumes in_size==0");

  size_t buff_size = MAX_PATH + 10;
  std::unique_ptr<wchar_t, decltype(&::free)> buff(static_cast<wchar_t*>(malloc(buff_size * sizeof(wchar_t))), &::free);
  DWORD retval = GetCurrentDirectoryW(buff_size, buff.get());
  if (retval > buff_size) {
    buff_size = retval;
    buff.reset(static_cast<wchar_t*>(malloc(buff_size * sizeof(wchar_t))));
    retval = GetCurrentDirectoryW(buff_size, buff.get());
  }
  if (!retval) {
    set_errno();
    return nullptr;
  }
  return buff.release();
}

inline wchar_t* realpath(const wchar_t* path, [[maybe_unused]] wchar_t* resolved_name) {
  // Only expected to be used with us allocating the buffer.
  _LIBCPP_ASSERT_INTERNAL(resolved_name == nullptr, "Windows realpath() assumes a null resolved_name");

  WinHandle h(path, FILE_READ_ATTRIBUTES, 0);
  if (!h) {
    set_errno();
    return nullptr;
  }
  size_t buff_size = MAX_PATH + 10;
  std::unique_ptr<wchar_t, decltype(&::free)> buff(static_cast<wchar_t*>(malloc(buff_size * sizeof(wchar_t))), &::free);
  DWORD retval = GetFinalPathNameByHandleW(h, buff.get(), buff_size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (retval > buff_size) {
    buff_size = retval;
    buff.reset(static_cast<wchar_t*>(malloc(buff_size * sizeof(wchar_t))));
    retval = GetFinalPathNameByHandleW(h, buff.get(), buff_size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  }
  if (!retval) {
    set_errno();
    return nullptr;
  }
  wchar_t* ptr = buff.get();
  if (!wcsncmp(ptr, L"\\\\?\\", 4)) {
    if (ptr[5] == ':') { // \\?\X: -> X:
      memmove(&ptr[0], &ptr[4], (wcslen(&ptr[4]) + 1) * sizeof(wchar_t));
    } else if (!wcsncmp(&ptr[4], L"UNC\\", 4)) { // \\?\UNC\server -> \\server
      wcscpy(&ptr[0], L"\\\\");
      memmove(&ptr[2], &ptr[8], (wcslen(&ptr[8]) + 1) * sizeof(wchar_t));
    }
  }
  return buff.release();
}

#  define AT_FDCWD -1
#  define AT_SYMLINK_NOFOLLOW 1
using ModeT = int;

inline int fchmod_handle(HANDLE h, int perms) {
  FILE_BASIC_INFO basic;
  if (!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)))
    return set_errno();
  DWORD orig_attributes = basic.FileAttributes;
  basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
  if ((perms & 0222) == 0)
    basic.FileAttributes |= FILE_ATTRIBUTE_READONLY;
  if (basic.FileAttributes != orig_attributes && !SetFileInformationByHandle(h, FileBasicInfo, &basic, sizeof(basic)))
    return set_errno();
  return 0;
}

inline int fchmodat(int /*fd*/, const wchar_t* path, int perms, int flag) {
  DWORD attributes = GetFileAttributesW(path);
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return set_errno();
  if (attributes & FILE_ATTRIBUTE_REPARSE_POINT && !(flag & AT_SYMLINK_NOFOLLOW)) {
    // If the file is a symlink, and we are supposed to operate on the target
    // of the symlink, we need to open a handle to it, without the
    // FILE_FLAG_OPEN_REPARSE_POINT flag, to open the destination of the
    // symlink, and operate on it via the handle.
    detail::WinHandle h(path, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, 0);
    if (!h)
      return set_errno();
    return fchmod_handle(h, perms);
  } else {
    // For a non-symlink, or if operating on the symlink itself instead of
    // its target, we can use SetFileAttributesW, saving a few calls.
    DWORD orig_attributes = attributes;
    attributes &= ~FILE_ATTRIBUTE_READONLY;
    if ((perms & 0222) == 0)
      attributes |= FILE_ATTRIBUTE_READONLY;
    if (attributes != orig_attributes && !SetFileAttributesW(path, attributes))
      return set_errno();
  }
  return 0;
}

inline int fchmod(int fd, int perms) {
  HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  return fchmod_handle(h, perms);
}

#  define MAX_SYMLINK_SIZE MAXIMUM_REPARSE_DATA_BUFFER_SIZE
using SSizeT = ::int64_t;

inline SSizeT readlink(const wchar_t* path, wchar_t* ret_buf, size_t bufsize) {
  uint8_t buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
  detail::WinHandle h(path, FILE_READ_ATTRIBUTES, FILE_FLAG_OPEN_REPARSE_POINT);
  if (!h)
    return set_errno();
  DWORD out;
  if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buf, sizeof(buf), &out, 0))
    return set_errno();
  const auto* reparse    = reinterpret_cast<LIBCPP_REPARSE_DATA_BUFFER*>(buf);
  size_t path_buf_offset = offsetof(LIBCPP_REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer[0]);
  if (out < path_buf_offset) {
    errno = EINVAL;
    return -1;
  }
  if (reparse->ReparseTag != IO_REPARSE_TAG_SYMLINK) {
    errno = EINVAL;
    return -1;
  }
  const auto& symlink = reparse->SymbolicLinkReparseBuffer;
  unsigned short name_offset, name_length;
  if (symlink.PrintNameLength == 0) {
    name_offset = symlink.SubstituteNameOffset;
    name_length = symlink.SubstituteNameLength;
  } else {
    name_offset = symlink.PrintNameOffset;
    name_length = symlink.PrintNameLength;
  }
  // name_offset/length are expressed in bytes, not in wchar_t
  if (path_buf_offset + name_offset + name_length > out) {
    errno = EINVAL;
    return -1;
  }
  if (name_length / sizeof(wchar_t) > bufsize) {
    errno = ENOMEM;
    return -1;
  }
  memcpy(ret_buf, &symlink.PathBuffer[name_offset / sizeof(wchar_t)], name_length);
  return name_length / sizeof(wchar_t);
}

#else
inline int symlink_file(const char* oldname, const char* newname) { return ::symlink(oldname, newname); }
inline int symlink_dir(const char* oldname, const char* newname) { return ::symlink(oldname, newname); }
using ::chdir;
using ::fchmod;
#  if defined(AT_SYMLINK_NOFOLLOW) && defined(AT_FDCWD)
using ::fchmodat;
#  endif
using ::fstat;
using ::ftruncate;
using ::getcwd;
using ::link;
using ::lstat;
using ::mkdir;
using ::readlink;
using ::realpath;
using ::remove;
using ::rename;
using ::stat;
using ::statvfs;
using ::truncate;

#  define O_BINARY 0

using StatVFS = struct statvfs;
using ModeT   = ::mode_t;
using SSizeT  = ::ssize_t;

#endif

} // end namespace detail

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // POSIX_COMPAT_H
