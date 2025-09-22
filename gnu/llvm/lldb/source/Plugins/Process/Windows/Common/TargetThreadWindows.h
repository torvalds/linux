//===-- TargetThreadWindows.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Windows_TargetThreadWindows_H_
#define liblldb_Plugins_Process_Windows_TargetThreadWindows_H_

//#include "ForwardDecl.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Target/Thread.h"
#include "lldb/lldb-forward.h"

#include "RegisterContextWindows.h"

namespace lldb_private {
class ProcessWindows;
class HostThread;
class StackFrame;

class TargetThreadWindows : public lldb_private::Thread {
public:
  TargetThreadWindows(ProcessWindows &process, const HostThread &thread);
  virtual ~TargetThreadWindows();

  // lldb_private::Thread overrides
  void RefreshStateAfterStop() override;
  void WillResume(lldb::StateType resume_state) override;
  void DidStop() override;
  lldb::RegisterContextSP GetRegisterContext() override;
  lldb::RegisterContextSP
  CreateRegisterContextForFrame(StackFrame *frame) override;
  bool CalculateStopInfo() override;
  const char *GetName() override;

  Status DoResume();

  HostThread GetHostThread() const { return m_host_thread; }

private:
  lldb::RegisterContextSP m_thread_reg_ctx_sp;
  HostThread m_host_thread;
  std::string m_name;
};
} // namespace lldb_private

#endif
