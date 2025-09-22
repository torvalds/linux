//===-- RegisterContextPOSIXCore_riscv64.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_RISCV64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_RISCV64_H

#include "Plugins/Process/Utility/RegisterContextPOSIX_riscv64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_riscv64.h"

#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"

#include <memory>

class RegisterContextCorePOSIX_riscv64 : public RegisterContextPOSIX_riscv64 {
public:
  static std::unique_ptr<RegisterContextCorePOSIX_riscv64>
  Create(lldb_private::Thread &thread, const lldb_private::ArchSpec &arch,
         const lldb_private::DataExtractor &gpregset,
         llvm::ArrayRef<lldb_private::CoreNote> notes);

  ~RegisterContextCorePOSIX_riscv64() override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

protected:
  RegisterContextCorePOSIX_riscv64(
      lldb_private::Thread &thread,
      std::unique_ptr<RegisterInfoPOSIX_riscv64> register_info,
      const lldb_private::DataExtractor &gpregset,
      llvm::ArrayRef<lldb_private::CoreNote> notes);

  bool ReadGPR() override;

  bool ReadFPR() override;

  bool WriteGPR() override;

  bool WriteFPR() override;

private:
  lldb::DataBufferSP m_gpr_buffer;
  lldb::DataBufferSP m_fpr_buffer;

  lldb_private::DataExtractor m_gpr;
  lldb_private::DataExtractor m_fpr;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_RISCV64_H
