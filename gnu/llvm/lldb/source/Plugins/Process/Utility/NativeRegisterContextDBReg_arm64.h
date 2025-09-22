//===-- NativeRegisterContextDBReg_arm64.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextDBReg_arm64_h
#define lldb_NativeRegisterContextDBReg_arm64_h

#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

#include <array>

namespace lldb_private {

class NativeRegisterContextDBReg_arm64
    : public virtual NativeRegisterContextRegisterInfo {
public:
  uint32_t NumSupportedHardwareBreakpoints() override;

  uint32_t SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;

  bool ClearHardwareBreakpoint(uint32_t hw_idx) override;

  Status ClearAllHardwareBreakpoints() override;

  Status GetHardwareBreakHitIndex(uint32_t &bp_index,
                                  lldb::addr_t trap_addr) override;

  bool BreakpointIsEnabled(uint32_t bp_index);

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
  /// Debug register info for hardware breakpoints and watchpoints management.
  /// Watchpoints: For a user requested size 4 at addr 0x1004, where BAS
  /// watchpoints are at doubleword (8-byte) alignment.
  ///   \a real_addr is 0x1004
  ///   \a address is 0x1000
  ///   size is 8
  ///   If a one-byte write to 0x1006 is the most recent watchpoint trap,
  ///   \a hit_addr is 0x1006
  struct DREG {
    lldb::addr_t address;  // Breakpoint/watchpoint address value.
    lldb::addr_t hit_addr; // Address at which last watchpoint trigger exception
                           // occurred.
    lldb::addr_t real_addr; // Address value that should cause target to stop.
    uint32_t control;       // Breakpoint/watchpoint control value.
  };

  std::array<struct DREG, 16> m_hbp_regs; // hardware breakpoints
  std::array<struct DREG, 16> m_hwp_regs; // hardware watchpoints

  uint32_t m_max_hbp_supported;
  uint32_t m_max_hwp_supported;

  virtual llvm::Error ReadHardwareDebugInfo() = 0;
  virtual llvm::Error WriteHardwareDebugRegs(DREGType hwbType) = 0;
  virtual lldb::addr_t FixWatchpointHitAddress(lldb::addr_t hit_addr) {
    return hit_addr;
  }
};

} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextDBReg_arm64_h
