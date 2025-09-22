//===-- RegisterContextKDP_arm64.h --------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_REGISTERCONTEXTKDP_ARM64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_REGISTERCONTEXTKDP_ARM64_H

#include "Plugins/Process/Utility/RegisterContextDarwin_arm64.h"

class ThreadKDP;

class RegisterContextKDP_arm64 : public RegisterContextDarwin_arm64 {
public:
  RegisterContextKDP_arm64(ThreadKDP &thread, uint32_t concrete_frame_idx);

  ~RegisterContextKDP_arm64() override;

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override;

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override;

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override;

  int DoReadDBG(lldb::tid_t tid, int flavor, DBG &dbg) override;

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override;

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override;

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override;

  int DoWriteDBG(lldb::tid_t tid, int flavor, const DBG &dbg) override;

  ThreadKDP &m_kdp_thread;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_REGISTERCONTEXTKDP_ARM64_H
