//===-- RegisterContextMinidump_ARM.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_REGISTERCONTEXTMINIDUMP_ARM_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_REGISTERCONTEXTMINIDUMP_ARM_H

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

class RegisterContextMinidump_ARM : public lldb_private::RegisterContext {
public:
  RegisterContextMinidump_ARM(lldb_private::Thread &thread,
                              const DataExtractor &data, bool apple);

  ~RegisterContextMinidump_ARM() override = default;

  void InvalidateAllRegisters() override {
    // Do nothing... registers are always valid...
  }

  // Used for unit testing.
  static size_t GetRegisterCountStatic();
  // Used for unit testing.
  static const lldb_private::RegisterInfo *
  GetRegisterInfoAtIndexStatic(size_t reg, bool apple);

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
  struct QRegValue {
    uint64_t lo;
    uint64_t hi;
  };

  struct Context {
    uint32_t context_flags;
    uint32_t r[16];
    uint32_t cpsr;
    uint64_t fpscr;
    union {
      uint64_t d[32];
      uint32_t s[32];
      QRegValue q[16];
    };
    uint32_t extra[8];
  };

protected:
  enum class Flags : uint32_t {
    ARM_Flag = 0x40000000,
    Integer = ARM_Flag | 0x00000002,
    FloatingPoint = ARM_Flag | 0x00000004,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ FloatingPoint)
  };
  Context m_regs;
  const bool m_apple; // True if this is an Apple ARM where FP is R7
};

} // end namespace minidump
} // end namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_REGISTERCONTEXTMINIDUMP_ARM_H
