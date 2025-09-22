//===-- HostInfoAndroid.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_android_HostInfoAndroid_h_
#define lldb_Host_android_HostInfoAndroid_h_

#include "lldb/Host/linux/HostInfoLinux.h"

namespace lldb_private {

class HostInfoAndroid : public HostInfoLinux {
  friend class HostInfoBase;

public:
  static FileSpec GetDefaultShell();
  static FileSpec ResolveLibraryPath(const std::string &path,
                                     const ArchSpec &arch);

protected:
  static void ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                             ArchSpec &arch_64);
  static bool ComputeTempFileBaseDirectory(FileSpec &file_spec);
};

} // end of namespace lldb_private

#endif // #ifndef lldb_Host_android_HostInfoAndroid_h_
