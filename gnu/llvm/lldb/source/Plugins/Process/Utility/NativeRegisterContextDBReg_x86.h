//===-- NativeRegisterContextDBReg_x86.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextDBReg_x86_h
#define lldb_NativeRegisterContextDBReg_x86_h

#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

namespace lldb_private {

class NativeRegisterContextDBReg_x86
    : public virtual NativeRegisterContextRegisterInfo {
public:
  // NB: This constructor is here only because gcc<=6.5 requires a virtual base
  // class initializer on abstract class (even though it is never used). It can
  // be deleted once we move to gcc>=7.0.
  NativeRegisterContextDBReg_x86(NativeThreadProtocol &thread)
      : NativeRegisterContextRegisterInfo(thread, nullptr) {}

  Status IsWatchpointHit(uint32_t wp_index, bool &is_hit) override;

  Status GetWatchpointHitIndex(uint32_t &wp_index,
                               lldb::addr_t trap_addr) override;

  Status IsWatchpointVacant(uint32_t wp_index, bool &is_vacant) override;

  bool ClearHardwareWatchpoint(uint32_t wp_index) override;

  Status ClearWatchpointHit(uint32_t wp_index) override;

  Status ClearAllHardwareWatchpoints() override;

  Status SetHardwareWatchpointWithIndex(lldb::addr_t addr, size_t size,
                                        uint32_t watch_flags,
                                        uint32_t wp_index);

  uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                 uint32_t watch_flags) override;

  lldb::addr_t GetWatchpointAddress(uint32_t wp_index) override;

  uint32_t NumSupportedHardwareWatchpoints() override;

  virtual const RegisterInfo *GetDR(int num) const;
};

} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextDBReg_x86_h
