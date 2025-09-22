//===-- HostInfoAndroid.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/android/HostInfoAndroid.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/linux/HostInfoLinux.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

using namespace lldb_private;
using namespace llvm;

void HostInfoAndroid::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                     ArchSpec &arch_64) {
  HostInfoLinux::ComputeHostArchitectureSupport(arch_32, arch_64);

  if (arch_32.IsValid()) {
    arch_32.GetTriple().setEnvironment(llvm::Triple::Android);
  }
  if (arch_64.IsValid()) {
    arch_64.GetTriple().setEnvironment(llvm::Triple::Android);
  }
}

FileSpec HostInfoAndroid::GetDefaultShell() {
  return FileSpec("/system/bin/sh");
}

FileSpec HostInfoAndroid::ResolveLibraryPath(const std::string &module_path,
                                             const ArchSpec &arch) {
  static const char *const ld_library_path_separator = ":";
  static const char *const default_lib32_path[] = {"/vendor/lib", "/system/lib",
                                                   nullptr};
  static const char *const default_lib64_path[] = {"/vendor/lib64",
                                                   "/system/lib64", nullptr};

  if (module_path.empty() || module_path[0] == '/') {
    FileSpec file_spec(module_path.c_str());
    FileSystem::Instance().Resolve(file_spec);
    return file_spec;
  }

  SmallVector<StringRef, 4> ld_paths;

  if (const char *ld_library_path = ::getenv("LD_LIBRARY_PATH"))
    StringRef(ld_library_path)
        .split(ld_paths, StringRef(ld_library_path_separator), -1, false);

  const char *const *default_lib_path = nullptr;
  switch (arch.GetAddressByteSize()) {
  case 4:
    default_lib_path = default_lib32_path;
    break;
  case 8:
    default_lib_path = default_lib64_path;
    break;
  default:
    assert(false && "Unknown address byte size");
    return FileSpec();
  }

  for (const char *const *it = default_lib_path; *it; ++it)
    ld_paths.push_back(StringRef(*it));

  for (const StringRef &path : ld_paths) {
    FileSpec file_candidate(path.str().c_str());
    FileSystem::Instance().Resolve(file_candidate);
    file_candidate.AppendPathComponent(module_path.c_str());

    if (FileSystem::Instance().Exists(file_candidate))
      return file_candidate;
  }

  return FileSpec();
}

bool HostInfoAndroid::ComputeTempFileBaseDirectory(FileSpec &file_spec) {
  bool success = HostInfoLinux::ComputeTempFileBaseDirectory(file_spec);

  // On Android, there is no path which is guaranteed to be writable. If the
  // user has not provided a path via an environment variable, the generic
  // algorithm will deduce /tmp, which is plain wrong. In that case we have an
  // invalid directory, we substitute the path with /data/local/tmp, which is
  // correct at least in some cases (i.e., when running as shell user).
  if (!success || !FileSystem::Instance().Exists(file_spec))
    file_spec = FileSpec("/data/local/tmp");

  return FileSystem::Instance().Exists(file_spec);
}
