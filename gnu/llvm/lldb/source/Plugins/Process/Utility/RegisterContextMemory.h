//===-- RegisterContextMemory.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMEMORY_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMEMORY_H

#include <vector>

#include "lldb/Target/DynamicRegisterInfo.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/lldb-private.h"

class RegisterContextMemory : public lldb_private::RegisterContext {
public:
  RegisterContextMemory(lldb_private::Thread &thread,
                        uint32_t concrete_frame_idx,
                        lldb_private::DynamicRegisterInfo &reg_info,
                        lldb::addr_t reg_data_addr);

  ~RegisterContextMemory() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t reg_set) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

  // If all of the thread register are in a contiguous buffer in
  // memory, then the default ReadRegister/WriteRegister and
  // ReadAllRegisterValues/WriteAllRegisterValues will work. If thread
  // registers are not contiguous, clients will want to subclass this
  // class and modify the read/write functions as needed.

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &reg_value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &reg_value) override;

  bool ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  void SetAllRegisterData(const lldb::DataBufferSP &data_sp);

protected:
  void SetAllRegisterValid(bool b);

  lldb_private::DynamicRegisterInfo &m_reg_infos;
  std::vector<bool> m_reg_valid;
  lldb::WritableDataBufferSP m_data;
  lldb_private::DataExtractor m_reg_data;
  lldb::addr_t m_reg_data_addr; // If this is valid, then we have a register
                                // context that is stored in memmory

private:
  RegisterContextMemory(const RegisterContextMemory &) = delete;
  const RegisterContextMemory &
  operator=(const RegisterContextMemory &) = delete;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMEMORY_H
