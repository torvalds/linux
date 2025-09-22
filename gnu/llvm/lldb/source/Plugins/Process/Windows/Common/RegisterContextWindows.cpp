//===-- RegisterContextWindows.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-types.h"

#include "ProcessWindowsLog.h"
#include "RegisterContextWindows.h"
#include "TargetThreadWindows.h"

#include "llvm/ADT/STLExtras.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

const DWORD kWinContextFlags = CONTEXT_ALL;

// Constructors and Destructors
RegisterContextWindows::RegisterContextWindows(Thread &thread,
                                               uint32_t concrete_frame_idx)
    : RegisterContext(thread, concrete_frame_idx), m_context(),
      m_context_stale(true) {}

RegisterContextWindows::~RegisterContextWindows() {}

void RegisterContextWindows::InvalidateAllRegisters() {
  m_context_stale = true;
}

bool RegisterContextWindows::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {

  if (!CacheAllRegisterValues())
    return false;

  data_sp.reset(new DataBufferHeap(sizeof(CONTEXT), 0));
  memcpy(data_sp->GetBytes(), &m_context, sizeof(m_context));

  return true;
}

bool RegisterContextWindows::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  assert(data_sp->GetByteSize() >= sizeof(m_context));
  memcpy(&m_context, data_sp->GetBytes(), sizeof(m_context));

  return ApplyAllRegisterValues();
}

uint32_t RegisterContextWindows::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  const uint32_t num_regs = GetRegisterCount();

  assert(kind < kNumRegisterKinds);
  for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg_idx);

    if (reg_info->kinds[kind] == num)
      return reg_idx;
  }

  return LLDB_INVALID_REGNUM;
}

bool RegisterContextWindows::HardwareSingleStep(bool enable) { return false; }

bool RegisterContextWindows::AddHardwareBreakpoint(uint32_t slot,
                                                   lldb::addr_t address,
                                                   uint32_t size, bool read,
                                                   bool write) {
  if (slot >= NUM_HARDWARE_BREAKPOINT_SLOTS)
    return false;

  switch (size) {
  case 1:
  case 2:
  case 4:
#if defined(_WIN64)
  case 8:
#endif
    break;
  default:
    return false;
  }

  if (!CacheAllRegisterValues())
    return false;

#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_AMD64)
  unsigned shift = 2 * slot;
  m_context.Dr7 |= 1ULL << shift;

  (&m_context.Dr0)[slot] = address;

  shift = 18 + 4 * slot;
  m_context.Dr7 &= ~(3ULL << shift);
  m_context.Dr7 |= (size == 8 ? 2ULL : size - 1) << shift;

  shift = 16 + 4 * slot;
  m_context.Dr7 &= ~(3ULL << shift);
  m_context.Dr7 |= (read ? 3ULL : (write ? 1ULL : 0)) << shift;

  return ApplyAllRegisterValues();

#else
  Log *log = GetLog(WindowsLog::Registers);
  LLDB_LOG(log, "hardware breakpoints not currently supported on this arch");
  return false;
#endif
}

bool RegisterContextWindows::RemoveHardwareBreakpoint(uint32_t slot) {
  if (slot >= NUM_HARDWARE_BREAKPOINT_SLOTS)
    return false;

  if (!CacheAllRegisterValues())
    return false;

#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_AMD64)
  unsigned shift = 2 * slot;
  m_context.Dr7 &= ~(1ULL << shift);

  return ApplyAllRegisterValues();
#else
  return false;
#endif
}

uint32_t RegisterContextWindows::GetTriggeredHardwareBreakpointSlotId() {
  if (!CacheAllRegisterValues())
    return LLDB_INVALID_INDEX32;

#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_AMD64)
  for (unsigned i = 0UL; i < NUM_HARDWARE_BREAKPOINT_SLOTS; i++)
    if (m_context.Dr6 & (1ULL << i))
      return i;
#endif

  return LLDB_INVALID_INDEX32;
}

bool RegisterContextWindows::CacheAllRegisterValues() {
  Log *log = GetLog(WindowsLog::Registers);
  if (!m_context_stale)
    return true;

  TargetThreadWindows &wthread = static_cast<TargetThreadWindows &>(m_thread);
  memset(&m_context, 0, sizeof(m_context));
  m_context.ContextFlags = kWinContextFlags;
  if (::SuspendThread(
          wthread.GetHostThread().GetNativeThread().GetSystemHandle()) ==
      (DWORD)-1) {
    return false;
  }
  if (!::GetThreadContext(
          wthread.GetHostThread().GetNativeThread().GetSystemHandle(),
          &m_context)) {
    LLDB_LOG(
        log,
        "GetThreadContext failed with error {0} while caching register values.",
        ::GetLastError());
    return false;
  }
  if (::ResumeThread(
          wthread.GetHostThread().GetNativeThread().GetSystemHandle()) ==
      (DWORD)-1) {
    return false;
  }
  LLDB_LOG(log, "successfully updated the register values.");
  m_context_stale = false;
  return true;
}

bool RegisterContextWindows::ApplyAllRegisterValues() {
  TargetThreadWindows &wthread = static_cast<TargetThreadWindows &>(m_thread);
  return ::SetThreadContext(
      wthread.GetHostThread().GetNativeThread().GetSystemHandle(), &m_context);
}
