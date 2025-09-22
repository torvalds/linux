//===-- RegisterContextLinux_x86_64.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_X86_64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_X86_64_H

#include "Plugins/Process/Utility/RegisterContextLinux_x86.h"

class RegisterContextLinux_x86_64
    : public lldb_private::RegisterContextLinux_x86 {
public:
  RegisterContextLinux_x86_64(const lldb_private::ArchSpec &target_arch);

  static size_t GetGPRSizeStatic();
  size_t GetGPRSize() const override { return GetGPRSizeStatic(); }

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  uint32_t GetUserRegisterCount() const override;

private:
  const lldb_private::RegisterInfo *m_register_info_p;
  uint32_t m_register_info_count;
  uint32_t m_user_register_count;
};

#endif
