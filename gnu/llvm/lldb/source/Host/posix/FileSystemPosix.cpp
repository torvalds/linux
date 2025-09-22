//===-- FileSystemPosix.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/FileSystem.h"

// C includes
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__NetBSD__)
#include <sys/statvfs.h>
#endif

// lldb Includes
#include "lldb/Host/Host.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"

using namespace lldb;
using namespace lldb_private;

const char *FileSystem::DEV_NULL = "/dev/null";

Status FileSystem::Symlink(const FileSpec &src, const FileSpec &dst) {
  Status error;
  if (::symlink(dst.GetPath().c_str(), src.GetPath().c_str()) == -1)
    error.SetErrorToErrno();
  return error;
}

Status FileSystem::Readlink(const FileSpec &src, FileSpec &dst) {
  Status error;
  char buf[PATH_MAX];
  ssize_t count = ::readlink(src.GetPath().c_str(), buf, sizeof(buf) - 1);
  if (count < 0)
    error.SetErrorToErrno();
  else {
    buf[count] = '\0'; // Success
    dst.SetFile(buf, FileSpec::Style::native);
  }
  return error;
}

Status FileSystem::ResolveSymbolicLink(const FileSpec &src, FileSpec &dst) {
  char resolved_path[PATH_MAX];
  if (!src.GetPath(resolved_path, sizeof(resolved_path))) {
    return Status("Couldn't get the canonical path for %s",
                  src.GetPath().c_str());
  }

  char real_path[PATH_MAX + 1];
  if (realpath(resolved_path, real_path) == nullptr) {
    Status err;
    err.SetErrorToErrno();
    return err;
  }

  dst = FileSpec(real_path);

  return Status();
}

FILE *FileSystem::Fopen(const char *path, const char *mode) {
  return llvm::sys::RetryAfterSignal(nullptr, ::fopen, path, mode);
}

int FileSystem::Open(const char *path, int flags, int mode) {
  // Call ::open in a lambda to avoid overload resolution in RetryAfterSignal
  // when open is overloaded, such as in Bionic.
  auto lambda = [&]() { return ::open(path, flags, mode); };
  return llvm::sys::RetryAfterSignal(-1, lambda);
}
