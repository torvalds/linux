//===-- SBProcessInfo.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBPROCESSINFO_H
#define LLDB_API_SBPROCESSINFO_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBProcessInfo {
public:
  SBProcessInfo();
  SBProcessInfo(const SBProcessInfo &rhs);

  ~SBProcessInfo();

  SBProcessInfo &operator=(const SBProcessInfo &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetName();

  SBFileSpec GetExecutableFile();

  lldb::pid_t GetProcessID();

  uint32_t GetUserID();

  uint32_t GetGroupID();

  bool UserIDIsValid();

  bool GroupIDIsValid();

  uint32_t GetEffectiveUserID();

  uint32_t GetEffectiveGroupID();

  bool EffectiveUserIDIsValid();

  bool EffectiveGroupIDIsValid();

  lldb::pid_t GetParentProcessID();

  /// Return the target triple (arch-vendor-os) for the described process.
  const char *GetTriple();

private:
  friend class SBProcess;
  friend class SBProcessInfoList;

  lldb_private::ProcessInstanceInfo &ref();

  void SetProcessInfo(const lldb_private::ProcessInstanceInfo &proc_info_ref);

  std::unique_ptr<lldb_private::ProcessInstanceInfo> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBPROCESSINFO_H
