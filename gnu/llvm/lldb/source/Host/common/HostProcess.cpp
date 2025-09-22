//===-- HostProcess.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/HostProcess.h"
#include "lldb/Host/HostNativeProcess.h"
#include "lldb/Host/HostThread.h"

using namespace lldb;
using namespace lldb_private;

HostProcess::HostProcess() : m_native_process(new HostNativeProcess) {}

HostProcess::HostProcess(lldb::process_t process)
    : m_native_process(new HostNativeProcess(process)) {}

HostProcess::~HostProcess() = default;

Status HostProcess::Terminate() { return m_native_process->Terminate(); }

lldb::pid_t HostProcess::GetProcessId() const {
  return m_native_process->GetProcessId();
}

bool HostProcess::IsRunning() const { return m_native_process->IsRunning(); }

llvm::Expected<HostThread> HostProcess::StartMonitoring(
    const Host::MonitorChildProcessCallback &callback) {
  return m_native_process->StartMonitoring(callback);
}

HostNativeProcessBase &HostProcess::GetNativeProcess() {
  return *m_native_process;
}

const HostNativeProcessBase &HostProcess::GetNativeProcess() const {
  return *m_native_process;
}
