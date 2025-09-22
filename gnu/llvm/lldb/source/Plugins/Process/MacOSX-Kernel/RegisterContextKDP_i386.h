//===-- RegisterContextKDP_i386.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_REGISTERCONTEXTKDP_I386_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_REGISTERCONTEXTKDP_I386_H

#include "Plugins/Process/Utility/RegisterContextDarwin_i386.h"

class ThreadKDP;

class RegisterContextKDP_i386 : public RegisterContextDarwin_i386 {
public:
  RegisterContextKDP_i386(ThreadKDP &thread, uint32_t concrete_frame_idx);

  ~RegisterContextKDP_i386() override;

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override;

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override;

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override;

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override;

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override;

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override;

  ThreadKDP &m_kdp_thread;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_REGISTERCONTEXTKDP_I386_H
