//===-- RegisterContextFreeBSD_x86_64.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_X86_64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_X86_64_H

#include "RegisterInfoInterface.h"

class RegisterContextFreeBSD_x86_64
    : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextFreeBSD_x86_64(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

private:
  const lldb_private::RegisterInfo *m_register_info_p;
  const uint32_t m_register_count;
};

#endif
