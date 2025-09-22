//===-- RegisterContextThreadMemory.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTTHREADMEMORY_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTTHREADMEMORY_H

#include <vector>

#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class RegisterContextThreadMemory : public lldb_private::RegisterContext {
public:
  RegisterContextThreadMemory(Thread &thread, lldb::addr_t register_data_addr);

  ~RegisterContextThreadMemory() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const RegisterSet *GetRegisterSet(size_t reg_set) override;

  bool ReadRegister(const RegisterInfo *reg_info,
                    RegisterValue &reg_value) override;

  bool WriteRegister(const RegisterInfo *reg_info,
                     const RegisterValue &reg_value) override;

  // These two functions are used to implement "push" and "pop" of register
  // states.  They are used primarily
  // for expression evaluation, where we need to push a new state (storing the
  // old one in data_sp) and then
  // restoring the original state by passing the data_sp we got from
  // ReadAllRegisters to WriteAllRegisterValues.
  // ReadAllRegisters will do what is necessary to return a coherent set of
  // register values for this thread, which
  // may mean e.g. interrupting a thread that is sitting in a kernel trap.  That
  // is a somewhat disruptive operation,
  // so these API's should only be used when this behavior is needed.

  bool ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  bool CopyFromRegisterContext(lldb::RegisterContextSP context);

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

  uint32_t NumSupportedHardwareBreakpoints() override;

  uint32_t SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;

  bool ClearHardwareBreakpoint(uint32_t hw_idx) override;

  uint32_t NumSupportedHardwareWatchpoints() override;

  uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size, bool read,
                                 bool write) override;

  bool ClearHardwareWatchpoint(uint32_t hw_index) override;

  bool HardwareSingleStep(bool enable) override;

  Status ReadRegisterValueFromMemory(const lldb_private::RegisterInfo *reg_info,
                                     lldb::addr_t src_addr, uint32_t src_len,
                                     RegisterValue &reg_value) override;

  Status WriteRegisterValueToMemory(const lldb_private::RegisterInfo *reg_info,
                                    lldb::addr_t dst_addr, uint32_t dst_len,
                                    const RegisterValue &reg_value) override;

protected:
  void UpdateRegisterContext();

  lldb::ThreadWP m_thread_wp;
  lldb::RegisterContextSP m_reg_ctx_sp;
  lldb::addr_t m_register_data_addr;
  uint32_t m_stop_id;

private:
  RegisterContextThreadMemory(const RegisterContextThreadMemory &) = delete;
  const RegisterContextThreadMemory &
  operator=(const RegisterContextThreadMemory &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTTHREADMEMORY_H
