//===-- HostThread.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/HostThread.h"
#include "lldb/Host/HostNativeThread.h"

using namespace lldb;
using namespace lldb_private;

HostThread::HostThread() : m_native_thread(new HostNativeThread) {}

HostThread::HostThread(lldb::thread_t thread)
    : m_native_thread(new HostNativeThread(thread)) {}

Status HostThread::Join(lldb::thread_result_t *result) {
  return m_native_thread->Join(result);
}

Status HostThread::Cancel() { return m_native_thread->Cancel(); }

void HostThread::Reset() { return m_native_thread->Reset(); }

lldb::thread_t HostThread::Release() { return m_native_thread->Release(); }

bool HostThread::IsJoinable() const { return m_native_thread->IsJoinable(); }

HostNativeThread &HostThread::GetNativeThread() {
  return static_cast<HostNativeThread &>(*m_native_thread);
}

const HostNativeThread &HostThread::GetNativeThread() const {
  return static_cast<const HostNativeThread &>(*m_native_thread);
}

lldb::thread_result_t HostThread::GetResult() const {
  return m_native_thread->GetResult();
}

bool HostThread::EqualsThread(lldb::thread_t thread) const {
  return m_native_thread->EqualsThread(thread);
}
