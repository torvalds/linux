//===-- NativeRegisterContextLinux_loongarch64.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__loongarch__) && __loongarch_grlen == 64

#ifndef lldb_NativeRegisterContextLinux_loongarch64_h
#define lldb_NativeRegisterContextLinux_loongarch64_h

#include "Plugins/Process/Linux/NativeRegisterContextLinux.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_loongarch64.h"

#include <asm/ptrace.h>

namespace lldb_private {
namespace process_linux {

class NativeProcessLinux;

class NativeRegisterContextLinux_loongarch64
    : public NativeRegisterContextLinux {
public:
  NativeRegisterContextLinux_loongarch64(
      const ArchSpec &target_arch, NativeThreadProtocol &native_thread,
      std::unique_ptr<RegisterInfoPOSIX_loongarch64> register_info_up);

  uint32_t GetRegisterSetCount() const override;

  uint32_t GetUserRegisterCount() const override;

  const RegisterSet *GetRegisterSet(uint32_t set_index) const override;

  Status ReadRegister(const RegisterInfo *reg_info,
                      RegisterValue &reg_value) override;

  Status WriteRegister(const RegisterInfo *reg_info,
                       const RegisterValue &reg_value) override;

  Status ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  Status WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  void InvalidateAllRegisters() override;

  std::vector<uint32_t>
  GetExpeditedRegisters(ExpeditedRegs expType) const override;

  bool RegisterOffsetIsDynamic() const override { return true; }

protected:
  Status ReadGPR() override;

  Status WriteGPR() override;

  Status ReadFPR() override;

  Status WriteFPR() override;

  void *GetGPRBuffer() override { return &m_gpr; }

  void *GetFPRBuffer() override { return &m_fpr; }

  size_t GetGPRSize() const override { return GetRegisterInfo().GetGPRSize(); }

  size_t GetFPRSize() override { return GetRegisterInfo().GetFPRSize(); }

private:
  bool m_gpr_is_valid;
  bool m_fpu_is_valid;

  RegisterInfoPOSIX_loongarch64::GPR m_gpr;

  RegisterInfoPOSIX_loongarch64::FPR m_fpr;

  bool IsGPR(unsigned reg) const;

  bool IsFPR(unsigned reg) const;

  uint32_t CalculateFprOffset(const RegisterInfo *reg_info) const;

  const RegisterInfoPOSIX_loongarch64 &GetRegisterInfo() const;
};

} // namespace process_linux
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextLinux_loongarch64_h

#endif // defined(__loongarch__) && __loongarch_grlen == 64
