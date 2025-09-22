//===-- MainLoopPosix.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_MAINLOOPPOSIX_H
#define LLDB_HOST_POSIX_MAINLOOPPOSIX_H

#include "lldb/Host/Config.h"
#include "lldb/Host/MainLoopBase.h"
#include "lldb/Host/Pipe.h"
#include "llvm/ADT/DenseMap.h"
#include <atomic>
#include <csignal>
#include <list>
#include <vector>

namespace lldb_private {

// Implementation of the MainLoopBase class. It can monitor file descriptors for
// readability using ppoll, kqueue, or pselect. In addition to the common base,
// this class provides the ability to invoke a given handler when a signal is
// received.
class MainLoopPosix : public MainLoopBase {
private:
  class SignalHandle;

public:
  typedef std::unique_ptr<SignalHandle> SignalHandleUP;

  MainLoopPosix();
  ~MainLoopPosix() override;

  ReadHandleUP RegisterReadObject(const lldb::IOObjectSP &object_sp,
                                  const Callback &callback,
                                  Status &error) override;

  // Listening for signals from multiple MainLoop instances is perfectly safe
  // as long as they don't try to listen for the same signal. The callback
  // function is invoked when the control returns to the Run() function, not
  // when the hander is executed. This mean that you can treat the callback as
  // a normal function and perform things which would not be safe in a signal
  // handler. However, since the callback is not invoked synchronously, you
  // cannot use this mechanism to handle SIGSEGV and the like.
  SignalHandleUP RegisterSignal(int signo, const Callback &callback,
                                Status &error);

  Status Run() override;

protected:
  void UnregisterReadObject(IOObject::WaitableHandle handle) override;
  void UnregisterSignal(int signo, std::list<Callback>::iterator callback_it);

  void TriggerPendingCallbacks() override;

private:
  void ProcessReadObject(IOObject::WaitableHandle handle);
  void ProcessSignal(int signo);

  class SignalHandle {
  public:
    ~SignalHandle() { m_mainloop.UnregisterSignal(m_signo, m_callback_it); }

  private:
    SignalHandle(MainLoopPosix &mainloop, int signo,
                 std::list<Callback>::iterator callback_it)
        : m_mainloop(mainloop), m_signo(signo), m_callback_it(callback_it) {}

    MainLoopPosix &m_mainloop;
    int m_signo;
    std::list<Callback>::iterator m_callback_it;

    friend class MainLoopPosix;
    SignalHandle(const SignalHandle &) = delete;
    const SignalHandle &operator=(const SignalHandle &) = delete;
  };

  struct SignalInfo {
    std::list<Callback> callbacks;
    struct sigaction old_action;
    bool was_blocked : 1;
  };
  class RunImpl;

  llvm::DenseMap<IOObject::WaitableHandle, Callback> m_read_fds;
  llvm::DenseMap<int, SignalInfo> m_signals;
  Pipe m_trigger_pipe;
  std::atomic<bool> m_triggering;
#if HAVE_SYS_EVENT_H
  int m_kqueue;
#endif
};

} // namespace lldb_private

#endif // LLDB_HOST_POSIX_MAINLOOPPOSIX_H
