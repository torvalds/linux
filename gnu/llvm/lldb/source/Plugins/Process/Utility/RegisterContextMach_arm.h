//===-- RegisterContextMach_arm.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMACH_ARM_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMACH_ARM_H

#include "RegisterContextDarwin_arm.h"

class RegisterContextMach_arm : public RegisterContextDarwin_arm {
public:
  RegisterContextMach_arm(lldb_private::Thread &thread,
                          uint32_t concrete_frame_idx);

  ~RegisterContextMach_arm() override;

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override;

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override;

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override;

  int DoReadDBG(lldb::tid_t tid, int flavor, DBG &dbg) override;

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override;

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override;

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override;

  int DoWriteDBG(lldb::tid_t tid, int flavor, const DBG &dbg) override;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTMACH_ARM_H
