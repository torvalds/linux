//===-- MainLoopBase.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/MainLoopBase.h"

using namespace lldb;
using namespace lldb_private;

void MainLoopBase::AddPendingCallback(const Callback &callback) {
  {
    std::lock_guard<std::mutex> lock{m_callback_mutex};
    m_pending_callbacks.push_back(callback);
  }
  TriggerPendingCallbacks();
}

void MainLoopBase::ProcessPendingCallbacks() {
  // Move the callbacks to a local vector to avoid keeping m_pending_callbacks
  // locked throughout the calls.
  std::vector<Callback> pending_callbacks;
  {
    std::lock_guard<std::mutex> lock{m_callback_mutex};
    pending_callbacks = std::move(m_pending_callbacks);
  }

  for (const Callback &callback : pending_callbacks)
    callback(*this);
}
