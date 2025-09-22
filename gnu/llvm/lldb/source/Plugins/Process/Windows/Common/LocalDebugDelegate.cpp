//===-- LocalDebugDelegate.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LocalDebugDelegate.h"
#include "ProcessWindows.h"

using namespace lldb;
using namespace lldb_private;

LocalDebugDelegate::LocalDebugDelegate(ProcessWP process)
    : m_process(process) {}

void LocalDebugDelegate::OnExitProcess(uint32_t exit_code) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnExitProcess(exit_code);
}

void LocalDebugDelegate::OnDebuggerConnected(lldb::addr_t image_base) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnDebuggerConnected(image_base);
}

ExceptionResult
LocalDebugDelegate::OnDebugException(bool first_chance,
                                     const ExceptionRecord &record) {
  if (ProcessWindowsSP process = GetProcessPointer())
    return process->OnDebugException(first_chance, record);
  else
    return ExceptionResult::MaskException;
}

void LocalDebugDelegate::OnCreateThread(const HostThread &thread) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnCreateThread(thread);
}

void LocalDebugDelegate::OnExitThread(lldb::tid_t thread_id,
                                      uint32_t exit_code) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnExitThread(thread_id, exit_code);
}

void LocalDebugDelegate::OnLoadDll(const lldb_private::ModuleSpec &module_spec,
                                   lldb::addr_t module_addr) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnLoadDll(module_spec, module_addr);
}

void LocalDebugDelegate::OnUnloadDll(lldb::addr_t module_addr) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnUnloadDll(module_addr);
}

void LocalDebugDelegate::OnDebugString(const std::string &string) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnDebugString(string);
}

void LocalDebugDelegate::OnDebuggerError(const Status &error, uint32_t type) {
  if (ProcessWindowsSP process = GetProcessPointer())
    process->OnDebuggerError(error, type);
}

ProcessWindowsSP LocalDebugDelegate::GetProcessPointer() {
  ProcessSP process = m_process.lock();
  return std::static_pointer_cast<ProcessWindows>(process);
}
