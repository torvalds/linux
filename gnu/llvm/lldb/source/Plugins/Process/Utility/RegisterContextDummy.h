//===-- RegisterContextDummy.h ----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTDUMMY_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTDUMMY_H

#include <vector>

#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class RegisterContextDummy : public lldb_private::RegisterContext {
public:
  typedef std::shared_ptr<RegisterContextDummy> SharedPtr;

  RegisterContextDummy(Thread &thread, uint32_t concrete_frame_idx,
                       uint32_t address_byte_size);

  ~RegisterContextDummy() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t reg_set) override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

  bool ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

private:
  // For RegisterContextLLDB only

  lldb_private::RegisterSet m_reg_set0; // register set 0 (PC only)
  lldb_private::RegisterInfo m_pc_reg_info;

  RegisterContextDummy(const RegisterContextDummy &) = delete;
  const RegisterContextDummy &operator=(const RegisterContextDummy &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTDUMMY_H
