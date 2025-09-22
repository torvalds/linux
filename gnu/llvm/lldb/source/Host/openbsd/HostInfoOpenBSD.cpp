//===-- HostInfoOpenBSD.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/openbsd/HostInfoOpenBSD.h"
#include "lldb/Host/FileSystem.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <optional>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>

using namespace lldb_private;

llvm::VersionTuple HostInfoOpenBSD::GetOSVersion() {
  struct utsname un;

  ::memset(&un, 0, sizeof(un));
  if (::uname(&un) < 0)
    return llvm::VersionTuple();

  uint32_t major, minor;
  int status = ::sscanf(un.release, "%" PRIu32 ".%" PRIu32, &major, &minor);
  switch (status) {
  case 1:
    return llvm::VersionTuple(major);
  case 2:
    return llvm::VersionTuple(major, minor);
  }
  return llvm::VersionTuple();
}

std::optional<std::string> HostInfoOpenBSD::GetOSBuildString() {
  int mib[2] = {CTL_KERN, KERN_OSREV};
  uint32_t osrev = 0;
  size_t osrev_len = sizeof(osrev);

  if (::sysctl(mib, 2, &osrev, &osrev_len, NULL, 0) == 0)
    return llvm::formatv("{0,8:8}", osrev).str();

  return std::nullopt;
}

FileSpec HostInfoOpenBSD::GetProgramFileSpec() {
  static FileSpec g_program_filespec;
  return g_program_filespec;
}

bool HostInfoOpenBSD::ComputeSupportExeDirectory(FileSpec &file_spec) {
  if (HostInfoPosix::ComputeSupportExeDirectory(file_spec) &&
      file_spec.IsAbsolute() && FileSystem::Instance().Exists(file_spec))
    return true;

  file_spec.SetDirectory("/usr/bin");
  return true;
}
