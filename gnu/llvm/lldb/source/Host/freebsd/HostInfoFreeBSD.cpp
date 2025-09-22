//===-- HostInfoFreeBSD.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/freebsd/HostInfoFreeBSD.h"
#include "llvm/Support/FormatVariadic.h"
#include <cstdio>
#include <cstring>
#include <optional>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

using namespace lldb_private;

llvm::VersionTuple HostInfoFreeBSD::GetOSVersion() {
  struct utsname un;

  ::memset(&un, 0, sizeof(utsname));
  if (uname(&un) < 0)
    return llvm::VersionTuple();

  unsigned major, minor;
  if (2 == sscanf(un.release, "%u.%u", &major, &minor))
    return llvm::VersionTuple(major, minor);
  return llvm::VersionTuple();
}

std::optional<std::string> HostInfoFreeBSD::GetOSBuildString() {
  int mib[2] = {CTL_KERN, KERN_OSREV};
  uint32_t osrev = 0;
  size_t osrev_len = sizeof(osrev);

  if (::sysctl(mib, 2, &osrev, &osrev_len, NULL, 0) == 0)
    return llvm::formatv("{0,8:8}", osrev).str();

  return std::nullopt;
}

FileSpec HostInfoFreeBSD::GetProgramFileSpec() {
  static FileSpec g_program_filespec;
  if (!g_program_filespec) {
    int exe_path_mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, getpid()};
    char exe_path[PATH_MAX];
    size_t exe_path_size = sizeof(exe_path);
    if (sysctl(exe_path_mib, 4, exe_path, &exe_path_size, NULL, 0) == 0)
      g_program_filespec.SetFile(exe_path, FileSpec::Style::native);
  }
  return g_program_filespec;
}
