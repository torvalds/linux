//===-- MainLoopBase.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_MAINLOOPBASE_H
#define LLDB_HOST_MAINLOOPBASE_H

#include "lldb/Utility/IOObject.h"
#include "lldb/Utility/Status.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/ErrorHandling.h"
#include <functional>
#include <mutex>

namespace lldb_private {

// The purpose of this class is to enable multiplexed processing of data from
// different sources without resorting to multi-threading. Clients can register
// IOObjects, which will be monitored for readability, and when they become
// ready, the specified callback will be invoked. Monitoring for writability is
// not supported, but can be easily added if needed.
//
// The RegisterReadObject function return a handle, which controls the duration
// of the monitoring. When this handle is destroyed, the callback is
// deregistered.
//
// Since this class is primarily intended to be used for single-threaded
// processing, it does not attempt to perform any internal synchronisation and
// any concurrent accesses must be protected  externally. However, it is
// perfectly legitimate to have more than one instance of this class running on
// separate threads, or even a single thread.
class MainLoopBase {
private:
  class ReadHandle;

public:
  MainLoopBase() : m_terminate_request(false) {}
  virtual ~MainLoopBase() = default;

  typedef std::unique_ptr<ReadHandle> ReadHandleUP;

  typedef std::function<void(MainLoopBase &)> Callback;

  virtual ReadHandleUP RegisterReadObject(const lldb::IOObjectSP &object_sp,
                                          const Callback &callback,
                                          Status &error) = 0;

  // Add a pending callback that will be executed once after all the pending
  // events are processed. The callback will be executed even if termination
  // was requested.
  void AddPendingCallback(const Callback &callback);

  // Waits for registered events and invoke the proper callbacks. Returns when
  // all callbacks deregister themselves or when someone requests termination.
  virtual Status Run() { llvm_unreachable("Not implemented"); }

  // This should only be performed from a callback. Do not attempt to terminate
  // the processing from another thread.
  virtual void RequestTermination() { m_terminate_request = true; }

protected:
  ReadHandleUP CreateReadHandle(const lldb::IOObjectSP &object_sp) {
    return ReadHandleUP(new ReadHandle(*this, object_sp->GetWaitableHandle()));
  }

  virtual void UnregisterReadObject(IOObject::WaitableHandle handle) = 0;

  // Interrupt the loop that is currently waiting for events and execute
  // the current pending callbacks immediately.
  virtual void TriggerPendingCallbacks() = 0;

  void ProcessPendingCallbacks();

  std::mutex m_callback_mutex;
  std::vector<Callback> m_pending_callbacks;
  bool m_terminate_request : 1;

private:
  class ReadHandle {
  public:
    ~ReadHandle() { m_mainloop.UnregisterReadObject(m_handle); }

  private:
    ReadHandle(MainLoopBase &mainloop, IOObject::WaitableHandle handle)
        : m_mainloop(mainloop), m_handle(handle) {}

    MainLoopBase &m_mainloop;
    IOObject::WaitableHandle m_handle;

    friend class MainLoopBase;
    ReadHandle(const ReadHandle &) = delete;
    const ReadHandle &operator=(const ReadHandle &) = delete;
  };

  MainLoopBase(const MainLoopBase &) = delete;
  const MainLoopBase &operator=(const MainLoopBase &) = delete;
};

} // namespace lldb_private

#endif // LLDB_HOST_MAINLOOPBASE_H
