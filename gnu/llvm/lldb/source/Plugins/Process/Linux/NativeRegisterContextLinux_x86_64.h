//===-- NativeRegisterContextLinux_x86_64.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__i386__) || defined(__x86_64__)

#ifndef lldb_NativeRegisterContextLinux_x86_64_h
#define lldb_NativeRegisterContextLinux_x86_64_h

#include "Plugins/Process/Linux/NativeRegisterContextLinux.h"
#include "Plugins/Process/Utility/NativeRegisterContextDBReg_x86.h"
#include "Plugins/Process/Utility/RegisterContextLinux_x86.h"
#include "Plugins/Process/Utility/RegisterContext_x86.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"
#include <optional>
#include <sys/uio.h>

namespace lldb_private {
namespace process_linux {

class NativeProcessLinux;

class NativeRegisterContextLinux_x86_64
    : public NativeRegisterContextLinux,
      public NativeRegisterContextDBReg_x86 {
public:
  NativeRegisterContextLinux_x86_64(const ArchSpec &target_arch,
                                    NativeThreadProtocol &native_thread);

  uint32_t GetRegisterSetCount() const override;

  const RegisterSet *GetRegisterSet(uint32_t set_index) const override;

  uint32_t GetUserRegisterCount() const override;

  Status ReadRegister(const RegisterInfo *reg_info,
                      RegisterValue &reg_value) override;

  Status WriteRegister(const RegisterInfo *reg_info,
                       const RegisterValue &reg_value) override;

  Status ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  Status WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  std::optional<SyscallData> GetSyscallData() override;

  std::optional<MmapData> GetMmapData() override;

  const RegisterInfo *GetDR(int num) const override;

protected:
  void *GetGPRBuffer() override { return &m_gpr_x86_64; }

  void *GetFPRBuffer() override;

  size_t GetFPRSize() override;

  Status ReadFPR() override;

  Status WriteFPR() override;

  uint32_t GetPtraceOffset(uint32_t reg_index) override;

private:
  // Private member types.
  enum class XStateType { Invalid, FXSAVE, XSAVE };
  enum class RegSet { gpr, fpu, avx, mpx };

  // Info about register ranges.
  struct RegInfo {
    uint32_t num_registers;
    uint32_t num_gpr_registers;
    uint32_t num_fpr_registers;
    uint32_t num_avx_registers;
    uint32_t num_mpx_registers;
    uint32_t last_gpr;
    uint32_t first_fpr;
    uint32_t last_fpr;
    uint32_t first_st;
    uint32_t last_st;
    uint32_t first_mm;
    uint32_t last_mm;
    uint32_t first_xmm;
    uint32_t last_xmm;
    uint32_t first_ymm;
    uint32_t last_ymm;
    uint32_t first_mpxr;
    uint32_t last_mpxr;
    uint32_t first_mpxc;
    uint32_t last_mpxc;
    uint32_t first_dr;
    uint32_t last_dr;
    uint32_t gpr_flags;
  };

  // Private member variables.
  mutable XStateType m_xstate_type;
  std::unique_ptr<FPR, llvm::FreeDeleter>
      m_xstate; // Extended States Area, named FPR for historical reasons.
  struct iovec m_iovec;
  YMM m_ymm_set;
  MPX m_mpx_set;
  RegInfo m_reg_info;
  uint64_t m_gpr_x86_64[x86_64_with_base::k_num_gpr_registers];
  uint32_t m_fctrl_offset_in_userarea;

  // Private member methods.
  bool IsCPUFeatureAvailable(RegSet feature_code) const;

  bool IsRegisterSetAvailable(uint32_t set_index) const;

  bool IsGPR(uint32_t reg_index) const;

  bool IsFPR(uint32_t reg_index) const;

  bool IsDR(uint32_t reg_index) const;

  bool CopyXSTATEtoYMM(uint32_t reg_index, lldb::ByteOrder byte_order);

  bool CopyYMMtoXSTATE(uint32_t reg, lldb::ByteOrder byte_order);

  bool IsAVX(uint32_t reg_index) const;

  bool CopyXSTATEtoMPX(uint32_t reg);

  bool CopyMPXtoXSTATE(uint32_t reg);

  bool IsMPX(uint32_t reg_index) const;

  void UpdateXSTATEforWrite(uint32_t reg_index);

  RegisterContextLinux_x86 &GetRegisterInfo() const {
    return static_cast<RegisterContextLinux_x86 &>(
        *m_register_info_interface_up);
  }
};

} // namespace process_linux
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextLinux_x86_64_h

#endif // defined(__i386__) || defined(__x86_64__)
