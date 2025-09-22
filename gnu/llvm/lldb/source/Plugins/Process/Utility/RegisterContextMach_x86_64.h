//===-- RegisterContextMach_x86_64.h ------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMACH_X86_64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMACH_X86_64_H

#include "RegisterContextDarwin_x86_64.h"

class RegisterContextMach_x86_64 : public RegisterContextDarwin_x86_64 {
public:
  RegisterContextMach_x86_64(lldb_private::Thread &thread,
                             uint32_t concrete_frame_idx);

  ~RegisterContextMach_x86_64() override;

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override;

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override;

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override;

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override;

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override;

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMACH_X86_64_H
