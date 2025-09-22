//===-- RegisterContextMinidump_ARM64.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_REGISTERCONTEXTMINIDUMP_ARM64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_REGISTERCONTEXTMINIDUMP_ARM64_H

#include "MinidumpTypes.h"

#include "Plugins/Process/Utility/RegisterInfoInterface.h"
#include "lldb/Target/RegisterContext.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"

// C includes
// C++ includes

namespace lldb_private {

namespace minidump {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class RegisterContextMinidump_ARM64 : public lldb_private::RegisterContext {
public:
  RegisterContextMinidump_ARM64(lldb_private::Thread &thread,
                                const DataExtractor &data);

  ~RegisterContextMinidump_ARM64() override = default;

  void InvalidateAllRegisters() override {
    // Do nothing... registers are always valid...
  }

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  const char *GetRegisterName(unsigned reg);

  bool ReadRegister(const RegisterInfo *reg_info,
                    RegisterValue &reg_value) override;

  bool WriteRegister(const RegisterInfo *reg_info,
                     const RegisterValue &reg_value) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

  // Reference: see breakpad/crashpad source
  struct Context {
    uint64_t context_flags;
    uint64_t x[32];
    uint64_t pc;
    uint32_t cpsr;
    uint32_t fpsr;
    uint32_t fpcr;
    uint8_t v[32 * 16]; // 32 128-bit floating point registers
  };

  enum class Flags : uint32_t {
    ARM64_Flag = 0x80000000,
    Integer = ARM64_Flag | 0x00000002,
    FloatingPoint = ARM64_Flag | 0x00000004,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ FloatingPoint)
  };

protected:
  Context m_regs;
};

} // end namespace minidump
} // end namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_REGISTERCONTEXTMINIDUMP_ARM64_H
