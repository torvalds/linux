//===-- RegisterContextLinuxCore_x86_64.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTLINUXCORE_X86_64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTLINUXCORE_X86_64_H

#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "RegisterContextPOSIXCore_x86_64.h"

class RegisterContextLinuxCore_x86_64 : public RegisterContextCorePOSIX_x86_64 {
public:
  RegisterContextLinuxCore_x86_64(
      lldb_private::Thread &thread,
      lldb_private::RegisterInfoInterface *register_info,
      const lldb_private::DataExtractor &gpregset,
      llvm::ArrayRef<lldb_private::CoreNote> notes);

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  lldb_private::RegInfo &GetRegInfo() override;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTLINUXCORE_X86_64_H
