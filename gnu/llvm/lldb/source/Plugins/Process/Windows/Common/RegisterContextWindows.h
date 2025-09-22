//===-- RegisterContextWindows.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextWindows_H_
#define liblldb_RegisterContextWindows_H_

#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {

class Thread;

class RegisterContextWindows : public lldb_private::RegisterContext {
public:
  // Constructors and Destructors
  RegisterContextWindows(Thread &thread, uint32_t concrete_frame_idx);

  virtual ~RegisterContextWindows();

  // Subclasses must override these functions
  void InvalidateAllRegisters() override;

  bool ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

  bool HardwareSingleStep(bool enable) override;

  static constexpr uint32_t GetNumHardwareBreakpointSlots() {
    return NUM_HARDWARE_BREAKPOINT_SLOTS;
  }

  bool AddHardwareBreakpoint(uint32_t slot, lldb::addr_t address, uint32_t size,
                             bool read, bool write);
  bool RemoveHardwareBreakpoint(uint32_t slot);

  uint32_t GetTriggeredHardwareBreakpointSlotId();

protected:
  static constexpr unsigned NUM_HARDWARE_BREAKPOINT_SLOTS = 4;

  virtual bool CacheAllRegisterValues();
  virtual bool ApplyAllRegisterValues();

  CONTEXT m_context;
  bool m_context_stale;
};
} // namespace lldb_private

#endif // #ifndef liblldb_RegisterContextWindows_H_
