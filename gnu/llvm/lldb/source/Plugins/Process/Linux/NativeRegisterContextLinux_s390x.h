//===-- NativeRegisterContextLinux_s390x.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__s390x__) && defined(__linux__)

#ifndef lldb_NativeRegisterContextLinux_s390x_h
#define lldb_NativeRegisterContextLinux_s390x_h

#include "Plugins/Process/Linux/NativeRegisterContextLinux.h"
#include "Plugins/Process/Utility/RegisterContext_s390x.h"
#include "Plugins/Process/Utility/lldb-s390x-register-enums.h"
#include <asm/ptrace.h>

namespace lldb_private {
namespace process_linux {

class NativeProcessLinux;

class NativeRegisterContextLinux_s390x : public NativeRegisterContextLinux {
public:
  NativeRegisterContextLinux_s390x(const ArchSpec &target_arch,
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

  Status IsWatchpointHit(uint32_t wp_index, bool &is_hit) override;

  Status GetWatchpointHitIndex(uint32_t &wp_index,
                               lldb::addr_t trap_addr) override;

  Status IsWatchpointVacant(uint32_t wp_index, bool &is_vacant) override;

  bool ClearHardwareWatchpoint(uint32_t wp_index) override;

  Status ClearAllHardwareWatchpoints() override;

  uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                 uint32_t watch_flags) override;

  lldb::addr_t GetWatchpointAddress(uint32_t wp_index) override;

  uint32_t NumSupportedHardwareWatchpoints() override;

protected:
  Status DoReadRegisterValue(uint32_t offset, const char *reg_name,
                             uint32_t size, RegisterValue &value) override;

  Status DoWriteRegisterValue(uint32_t offset, const char *reg_name,
                              const RegisterValue &value) override;

  Status ReadGPR() override;

  Status WriteGPR() override;

  Status ReadFPR() override;

  Status WriteFPR() override;

  void *GetGPRBuffer() override { return &m_regs; }
  size_t GetGPRSize() const override { return sizeof(m_regs); }
  void *GetFPRBuffer() override { return &m_fp_regs; }
  size_t GetFPRSize() override { return sizeof(m_fp_regs); }

private:
  // Info about register ranges.
  struct RegInfo {
    uint32_t num_registers;
    uint32_t num_gpr_registers;
    uint32_t num_fpr_registers;

    uint32_t last_gpr;
    uint32_t first_fpr;
    uint32_t last_fpr;
  };

  // Private member variables.
  RegInfo m_reg_info;
  lldb::addr_t m_watchpoint_addr;

  s390_regs m_regs;
  s390_fp_regs m_fp_regs;

  // Private member methods.
  bool IsRegisterSetAvailable(uint32_t set_index) const;

  bool IsGPR(uint32_t reg_index) const;

  bool IsFPR(uint32_t reg_index) const;

  Status PeekUserArea(uint32_t offset, void *buf, size_t buf_size);

  Status PokeUserArea(uint32_t offset, const void *buf, size_t buf_size);

  Status DoReadRegisterSet(uint32_t regset, void *buf, size_t buf_size);

  Status DoWriteRegisterSet(uint32_t regset, const void *buf, size_t buf_size);
};

} // namespace process_linux
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextLinux_s390x_h

#endif // defined(__s390x__) && defined(__linux__)
