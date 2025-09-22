//===-- NativeRegisterContextLinux_arm.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)

#ifndef lldb_NativeRegisterContextLinux_arm_h
#define lldb_NativeRegisterContextLinux_arm_h

#include "Plugins/Process/Linux/NativeRegisterContextLinux.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm.h"
#include "Plugins/Process/Utility/lldb-arm-register-enums.h"

namespace lldb_private {
namespace process_linux {

class NativeProcessLinux;

class NativeRegisterContextLinux_arm : public NativeRegisterContextLinux {
public:
  NativeRegisterContextLinux_arm(const ArchSpec &target_arch,
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

  // Hardware breakpoints/watchpoint management functions

  uint32_t NumSupportedHardwareBreakpoints() override;

  uint32_t SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;

  bool ClearHardwareBreakpoint(uint32_t hw_idx) override;

  Status ClearAllHardwareBreakpoints() override;

  Status GetHardwareBreakHitIndex(uint32_t &bp_index,
                                  lldb::addr_t trap_addr) override;

  uint32_t NumSupportedHardwareWatchpoints() override;

  uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                 uint32_t watch_flags) override;

  bool ClearHardwareWatchpoint(uint32_t hw_index) override;

  Status ClearAllHardwareWatchpoints() override;

  Status GetWatchpointHitIndex(uint32_t &wp_index,
                               lldb::addr_t trap_addr) override;

  lldb::addr_t GetWatchpointHitAddress(uint32_t wp_index) override;

  lldb::addr_t GetWatchpointAddress(uint32_t wp_index) override;

  uint32_t GetWatchpointSize(uint32_t wp_index);

  bool WatchpointIsEnabled(uint32_t wp_index);

  // Debug register type select
  enum DREGType { eDREGTypeWATCH = 0, eDREGTypeBREAK };

protected:
  Status DoReadRegisterValue(uint32_t offset, const char *reg_name,
                             uint32_t size, RegisterValue &value) override;

  Status DoWriteRegisterValue(uint32_t offset, const char *reg_name,
                              const RegisterValue &value) override;

  Status ReadGPR() override;

  Status WriteGPR() override;

  Status ReadFPR() override;

  Status WriteFPR() override;

  void *GetGPRBuffer() override { return &m_gpr_arm; }

  void *GetFPRBuffer() override { return &m_fpr; }

  size_t GetFPRSize() override { return sizeof(m_fpr); }

private:
  uint32_t m_gpr_arm[k_num_gpr_registers_arm];
  RegisterInfoPOSIX_arm::FPU m_fpr;

  // Debug register info for hardware breakpoints and watchpoints management.
  struct DREG {
    lldb::addr_t address;  // Breakpoint/watchpoint address value.
    lldb::addr_t hit_addr; // Address at which last watchpoint trigger exception
                           // occurred.
    lldb::addr_t real_addr; // Address value that should cause target to stop.
    uint32_t control;       // Breakpoint/watchpoint control value.
    uint32_t refcount;      // Serves as enable/disable and reference counter.
  };

  struct DREG m_hbr_regs[16]; // Arm native linux hardware breakpoints
  struct DREG m_hwp_regs[16]; // Arm native linux hardware watchpoints

  uint32_t m_max_hwp_supported;
  uint32_t m_max_hbp_supported;
  bool m_refresh_hwdebug_info;

  bool IsGPR(unsigned reg) const;

  bool IsFPR(unsigned reg) const;

  Status ReadHardwareDebugInfo();

  Status WriteHardwareDebugRegs(int hwbType, int hwb_index);

  uint32_t CalculateFprOffset(const RegisterInfo *reg_info) const;

  RegisterInfoPOSIX_arm &GetRegisterInfo() const;
};

} // namespace process_linux
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextLinux_arm_h

#endif // defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
