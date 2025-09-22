//===-- RegisterContextPOSIX_arm64.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_ARM64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_ARM64_H

#include "RegisterInfoInterface.h"
#include "RegisterInfoPOSIX_arm64.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class RegisterContextPOSIX_arm64 : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_arm64(
      lldb_private::Thread &thread,
      std::unique_ptr<RegisterInfoPOSIX_arm64> register_info);

  ~RegisterContextPOSIX_arm64() override;

  void Invalidate();

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  virtual size_t GetGPRSize();

  virtual unsigned GetRegisterSize(unsigned reg);

  virtual unsigned GetRegisterOffset(unsigned reg);

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  const char *GetRegisterName(unsigned reg);

protected:
  std::unique_ptr<RegisterInfoPOSIX_arm64> m_register_info_up;

  virtual const lldb_private::RegisterInfo *GetRegisterInfo();

  bool IsGPR(unsigned reg);

  bool IsFPR(unsigned reg);

  size_t GetFPUSize() { return sizeof(RegisterInfoPOSIX_arm64::FPU); }

  bool IsSVE(unsigned reg) const;
  bool IsPAuth(unsigned reg) const;
  bool IsTLS(unsigned reg) const;
  bool IsSME(unsigned reg) const;
  bool IsMTE(unsigned reg) const;

  bool IsSVEZ(unsigned reg) const { return m_register_info_up->IsSVEZReg(reg); }
  bool IsSVEP(unsigned reg) const { return m_register_info_up->IsSVEPReg(reg); }
  bool IsSVEVG(unsigned reg) const {
    return m_register_info_up->IsSVERegVG(reg);
  }
  bool IsSMEZA(unsigned reg) const {
    return m_register_info_up->IsSMERegZA(reg);
  }

  uint32_t GetRegNumSVEZ0() const {
    return m_register_info_up->GetRegNumSVEZ0();
  }
  uint32_t GetRegNumSVEFFR() const {
    return m_register_info_up->GetRegNumSVEFFR();
  }
  uint32_t GetRegNumFPCR() const { return m_register_info_up->GetRegNumFPCR(); }
  uint32_t GetRegNumFPSR() const { return m_register_info_up->GetRegNumFPSR(); }

  virtual bool ReadGPR() = 0;
  virtual bool ReadFPR() = 0;
  virtual bool WriteGPR() = 0;
  virtual bool WriteFPR() = 0;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_ARM64_H
