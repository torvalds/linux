//===-- RegisterInfoPOSIX_loongarch64.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_LOONGARCH64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_LOONGARCH64_H

#include "RegisterInfoAndSetInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"
#include <map>

class RegisterInfoPOSIX_loongarch64
    : public lldb_private::RegisterInfoAndSetInterface {
public:
  static const lldb_private::RegisterInfo *
  GetRegisterInfoPtr(const lldb_private::ArchSpec &target_arch);
  static uint32_t
  GetRegisterInfoCount(const lldb_private::ArchSpec &target_arch);

public:
  enum RegSetKind {
    GPRegSet,
    FPRegSet,
  };

  struct GPR {
    uint64_t gpr[32];

    uint64_t orig_a0;
    uint64_t csr_era;
    uint64_t csr_badv;
    uint64_t reserved[10];
  };

  struct FPR {
    uint64_t fpr[32];
    uint64_t fcc;
    uint32_t fcsr;
  };

  RegisterInfoPOSIX_loongarch64(const lldb_private::ArchSpec &target_arch,
                                lldb_private::Flags flags);

  size_t GetGPRSize() const override;

  size_t GetFPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  const lldb_private::RegisterSet *
  GetRegisterSet(size_t reg_set) const override;

  size_t GetRegisterSetCount() const override;

  size_t GetRegisterSetFromRegisterIndex(uint32_t reg_index) const override;

private:
  const lldb_private::RegisterInfo *m_register_info_p;
  uint32_t m_register_info_count;
};

#endif
