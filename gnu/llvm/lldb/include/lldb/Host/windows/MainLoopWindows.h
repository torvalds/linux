//===-- MainLoopWindows.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_WINDOWS_MAINLOOPWINDOWS_H
#define LLDB_HOST_WINDOWS_MAINLOOPWINDOWS_H

#include "lldb/Host/Config.h"
#include "lldb/Host/MainLoopBase.h"
#include <csignal>
#include <list>
#include <vector>

namespace lldb_private {

// Windows-specific implementation of the MainLoopBase class. It can monitor
// socket descriptors for readability using WSAEventSelect. Non-socket file
// descriptors are not supported.
class MainLoopWindows : public MainLoopBase {
public:
  MainLoopWindows();
  ~MainLoopWindows() override;

  ReadHandleUP RegisterReadObject(const lldb::IOObjectSP &object_sp,
                                  const Callback &callback,
                                  Status &error) override;

  Status Run() override;

protected:
  void UnregisterReadObject(IOObject::WaitableHandle handle) override;

  void TriggerPendingCallbacks() override;

private:
  void ProcessReadObject(IOObject::WaitableHandle handle);
  llvm::Expected<size_t> Poll();

  struct FdInfo {
    void *event;
    Callback callback;
  };
  llvm::DenseMap<IOObject::WaitableHandle, FdInfo> m_read_fds;
  void *m_trigger_event;
};

} // namespace lldb_private

#endif // LLDB_HOST_WINDOWS_MAINLOOPWINDOWS_H
