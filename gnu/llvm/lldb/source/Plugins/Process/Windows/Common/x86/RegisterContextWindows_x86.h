//===-- RegisterContextWindows_x86.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextWindows_x86_H_
#define liblldb_RegisterContextWindows_x86_H_

#if defined(__i386__) || defined(_M_IX86)

#include "RegisterContextWindows.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {

class Thread;

class RegisterContextWindows_x86 : public RegisterContextWindows {
public:
  // Constructors and Destructors
  RegisterContextWindows_x86(Thread &thread, uint32_t concrete_frame_idx);

  virtual ~RegisterContextWindows_x86();

  // Subclasses must override these functions
  size_t GetRegisterCount() override;

  const RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const RegisterSet *GetRegisterSet(size_t reg_set) override;

  bool ReadRegister(const RegisterInfo *reg_info,
                    RegisterValue &reg_value) override;

  bool WriteRegister(const RegisterInfo *reg_info,
                     const RegisterValue &reg_value) override;

private:
  bool ReadRegisterHelper(DWORD flags_required, const char *reg_name,
                          DWORD value, RegisterValue &reg_value) const;
};
}

#endif // defined(__i386__) || defined(_M_IX86)

#endif // #ifndef liblldb_RegisterContextWindows_x86_H_
