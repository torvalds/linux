//===-- RegisterContextPOSIX_riscv64.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_RISCV64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_RISCV64_H

#include "RegisterInfoInterface.h"
#include "RegisterInfoPOSIX_riscv64.h"
#include "lldb-riscv-register-enums.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class RegisterContextPOSIX_riscv64 : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_riscv64(
      lldb_private::Thread &thread,
      std::unique_ptr<RegisterInfoPOSIX_riscv64> register_info);

  ~RegisterContextPOSIX_riscv64() override;

  void invalidate();

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  virtual size_t GetGPRSize();

  virtual unsigned GetRegisterSize(unsigned reg);

  virtual unsigned GetRegisterOffset(unsigned reg);

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

protected:
  std::unique_ptr<RegisterInfoPOSIX_riscv64> m_register_info_up;

  virtual const lldb_private::RegisterInfo *GetRegisterInfo();

  bool IsGPR(unsigned reg);

  bool IsFPR(unsigned reg);

  size_t GetFPRSize() { return sizeof(RegisterInfoPOSIX_riscv64::FPR); }

  uint32_t GetRegNumFCSR() const { return fpr_fcsr_riscv; }

  virtual bool ReadGPR() = 0;
  virtual bool ReadFPR() = 0;
  virtual bool WriteGPR() = 0;
  virtual bool WriteFPR() = 0;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_RISCV64_H
