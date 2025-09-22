//===-- RegisterContextFreeBSD_mips64.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_MIPS64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_MIPS64_H

#include "RegisterInfoInterface.h"

class RegisterContextFreeBSD_mips64
    : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextFreeBSD_mips64(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) const;

  size_t GetRegisterSetCount() const;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

#endif
