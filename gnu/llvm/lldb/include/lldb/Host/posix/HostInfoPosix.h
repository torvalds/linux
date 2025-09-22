//===-- HostInfoPosix.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_HOSTINFOPOSIX_H
#define LLDB_HOST_POSIX_HOSTINFOPOSIX_H

#include "lldb/Host/HostInfoBase.h"
#include "lldb/Utility/FileSpec.h"
#include <optional>

namespace lldb_private {

class UserIDResolver;

class HostInfoPosix : public HostInfoBase {
  friend class HostInfoBase;

public:
  static size_t GetPageSize();
  static bool GetHostname(std::string &s);
  static std::optional<std::string> GetOSKernelDescription();

  static uint32_t GetUserID();
  static uint32_t GetGroupID();
  static uint32_t GetEffectiveUserID();
  static uint32_t GetEffectiveGroupID();

  static FileSpec GetDefaultShell();

  static bool GetEnvironmentVar(const std::string &var_name, std::string &var);

  static UserIDResolver &GetUserIDResolver();

protected:
  static bool ComputeSupportExeDirectory(FileSpec &file_spec);
  static bool ComputeHeaderDirectory(FileSpec &file_spec);
};
}

#endif
