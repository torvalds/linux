//===-- NativeProcessProtocol.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/common/NativeBreakpointList.h"
#include "lldb/Host/common/NativeRegisterContext.h"
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/Support/Process.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

// NativeProcessProtocol Members

NativeProcessProtocol::NativeProcessProtocol(lldb::pid_t pid, int terminal_fd,
                                             NativeDelegate &delegate)
    : m_pid(pid), m_delegate(delegate), m_terminal_fd(terminal_fd) {
  delegate.InitializeDelegate(this);
}

lldb_private::Status NativeProcessProtocol::Interrupt() {
  Status error;
#if !defined(SIGSTOP)
  error.SetErrorString("local host does not support signaling");
  return error;
#else
  return Signal(SIGSTOP);
#endif
}

Status NativeProcessProtocol::IgnoreSignals(llvm::ArrayRef<int> signals) {
  m_signals_to_ignore.clear();
  m_signals_to_ignore.insert(signals.begin(), signals.end());
  return Status();
}

lldb_private::Status
NativeProcessProtocol::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                           MemoryRegionInfo &range_info) {
  // Default: not implemented.
  return Status("not implemented");
}

lldb_private::Status
NativeProcessProtocol::ReadMemoryTags(int32_t type, lldb::addr_t addr,
                                      size_t len, std::vector<uint8_t> &tags) {
  return Status("not implemented");
}

lldb_private::Status
NativeProcessProtocol::WriteMemoryTags(int32_t type, lldb::addr_t addr,
                                       size_t len,
                                       const std::vector<uint8_t> &tags) {
  return Status("not implemented");
}

std::optional<WaitStatus> NativeProcessProtocol::GetExitStatus() {
  if (m_state == lldb::eStateExited)
    return m_exit_status;

  return std::nullopt;
}

bool NativeProcessProtocol::SetExitStatus(WaitStatus status,
                                          bool bNotifyStateChange) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "status = {0}, notify = {1}", status, bNotifyStateChange);

  // Exit status already set
  if (m_state == lldb::eStateExited) {
    if (m_exit_status)
      LLDB_LOG(log, "exit status already set to {0}", *m_exit_status);
    else
      LLDB_LOG(log, "state is exited, but status not set");
    return false;
  }

  m_state = lldb::eStateExited;
  m_exit_status = status;

  if (bNotifyStateChange)
    SynchronouslyNotifyProcessStateChanged(lldb::eStateExited);

  return true;
}

NativeThreadProtocol *NativeProcessProtocol::GetThreadAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  if (idx < m_threads.size())
    return m_threads[idx].get();
  return nullptr;
}

NativeThreadProtocol *
NativeProcessProtocol::GetThreadByIDUnlocked(lldb::tid_t tid) {
  for (const auto &thread : m_threads) {
    if (thread->GetID() == tid)
      return thread.get();
  }
  return nullptr;
}

NativeThreadProtocol *NativeProcessProtocol::GetThreadByID(lldb::tid_t tid) {
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  return GetThreadByIDUnlocked(tid);
}

bool NativeProcessProtocol::IsAlive() const {
  return m_state != eStateDetached && m_state != eStateExited &&
         m_state != eStateInvalid && m_state != eStateUnloaded;
}

const NativeWatchpointList::WatchpointMap &
NativeProcessProtocol::GetWatchpointMap() const {
  return m_watchpoint_list.GetWatchpointMap();
}

std::optional<std::pair<uint32_t, uint32_t>>
NativeProcessProtocol::GetHardwareDebugSupportInfo() const {
  Log *log = GetLog(LLDBLog::Process);

  // get any thread
  NativeThreadProtocol *thread(
      const_cast<NativeProcessProtocol *>(this)->GetThreadAtIndex(0));
  if (!thread) {
    LLDB_LOG(log, "failed to find a thread to grab a NativeRegisterContext!");
    return std::nullopt;
  }

  NativeRegisterContext &reg_ctx = thread->GetRegisterContext();
  return std::make_pair(reg_ctx.NumSupportedHardwareBreakpoints(),
                        reg_ctx.NumSupportedHardwareWatchpoints());
}

Status NativeProcessProtocol::SetWatchpoint(lldb::addr_t addr, size_t size,
                                            uint32_t watch_flags,
                                            bool hardware) {
  // This default implementation assumes setting the watchpoint for the process
  // will require setting the watchpoint for each of the threads.  Furthermore,
  // it will track watchpoints set for the process and will add them to each
  // thread that is attached to via the (FIXME implement) OnThreadAttached ()
  // method.

  Log *log = GetLog(LLDBLog::Process);

  // Update the thread list
  UpdateThreads();

  // Keep track of the threads we successfully set the watchpoint for.  If one
  // of the thread watchpoint setting operations fails, back off and remove the
  // watchpoint for all the threads that were successfully set so we get back
  // to a consistent state.
  std::vector<NativeThreadProtocol *> watchpoint_established_threads;

  // Tell each thread to set a watchpoint.  In the event that hardware
  // watchpoints are requested but the SetWatchpoint fails, try to set a
  // software watchpoint as a fallback.  It's conceivable that if there are
  // more threads than hardware watchpoints available, some of the threads will
  // fail to set hardware watchpoints while software ones may be available.
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");

    Status thread_error =
        thread->SetWatchpoint(addr, size, watch_flags, hardware);
    if (thread_error.Fail() && hardware) {
      // Try software watchpoints since we failed on hardware watchpoint
      // setting and we may have just run out of hardware watchpoints.
      thread_error = thread->SetWatchpoint(addr, size, watch_flags, false);
      if (thread_error.Success())
        LLDB_LOG(log,
                 "hardware watchpoint requested but software watchpoint set");
    }

    if (thread_error.Success()) {
      // Remember that we set this watchpoint successfully in case we need to
      // clear it later.
      watchpoint_established_threads.push_back(thread.get());
    } else {
      // Unset the watchpoint for each thread we successfully set so that we
      // get back to a consistent state of "not set" for the watchpoint.
      for (auto unwatch_thread_sp : watchpoint_established_threads) {
        Status remove_error = unwatch_thread_sp->RemoveWatchpoint(addr);
        if (remove_error.Fail())
          LLDB_LOG(log, "RemoveWatchpoint failed for pid={0}, tid={1}: {2}",
                   GetID(), unwatch_thread_sp->GetID(), remove_error);
      }

      return thread_error;
    }
  }
  return m_watchpoint_list.Add(addr, size, watch_flags, hardware);
}

Status NativeProcessProtocol::RemoveWatchpoint(lldb::addr_t addr) {
  // Update the thread list
  UpdateThreads();

  Status overall_error;

  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");

    const Status thread_error = thread->RemoveWatchpoint(addr);
    if (thread_error.Fail()) {
      // Keep track of the first thread error if any threads fail. We want to
      // try to remove the watchpoint from every thread, though, even if one or
      // more have errors.
      if (!overall_error.Fail())
        overall_error = thread_error;
    }
  }
  const Status error = m_watchpoint_list.Remove(addr);
  return overall_error.Fail() ? overall_error : error;
}

const HardwareBreakpointMap &
NativeProcessProtocol::GetHardwareBreakpointMap() const {
  return m_hw_breakpoints_map;
}

Status NativeProcessProtocol::SetHardwareBreakpoint(lldb::addr_t addr,
                                                    size_t size) {
  // This default implementation assumes setting a hardware breakpoint for this
  // process will require setting same hardware breakpoint for each of its
  // existing threads. New thread will do the same once created.
  Log *log = GetLog(LLDBLog::Process);

  // Update the thread list
  UpdateThreads();

  // Exit here if target does not have required hardware breakpoint capability.
  auto hw_debug_cap = GetHardwareDebugSupportInfo();

  if (hw_debug_cap == std::nullopt || hw_debug_cap->first == 0 ||
      hw_debug_cap->first <= m_hw_breakpoints_map.size())
    return Status("Target does not have required no of hardware breakpoints");

  // Vector below stores all thread pointer for which we have we successfully
  // set this hardware breakpoint. If any of the current process threads fails
  // to set this hardware breakpoint then roll back and remove this breakpoint
  // for all the threads that had already set it successfully.
  std::vector<NativeThreadProtocol *> breakpoint_established_threads;

  // Request to set a hardware breakpoint for each of current process threads.
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");

    Status thread_error = thread->SetHardwareBreakpoint(addr, size);
    if (thread_error.Success()) {
      // Remember that we set this breakpoint successfully in case we need to
      // clear it later.
      breakpoint_established_threads.push_back(thread.get());
    } else {
      // Unset the breakpoint for each thread we successfully set so that we
      // get back to a consistent state of "not set" for this hardware
      // breakpoint.
      for (auto rollback_thread_sp : breakpoint_established_threads) {
        Status remove_error =
            rollback_thread_sp->RemoveHardwareBreakpoint(addr);
        if (remove_error.Fail())
          LLDB_LOG(log,
                   "RemoveHardwareBreakpoint failed for pid={0}, tid={1}: {2}",
                   GetID(), rollback_thread_sp->GetID(), remove_error);
      }

      return thread_error;
    }
  }

  // Register new hardware breakpoint into hardware breakpoints map of current
  // process.
  m_hw_breakpoints_map[addr] = {addr, size};

  return Status();
}

Status NativeProcessProtocol::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  // Update the thread list
  UpdateThreads();

  Status error;

  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");
    error = thread->RemoveHardwareBreakpoint(addr);
  }

  // Also remove from hardware breakpoint map of current process.
  m_hw_breakpoints_map.erase(addr);

  return error;
}

void NativeProcessProtocol::SynchronouslyNotifyProcessStateChanged(
    lldb::StateType state) {
  Log *log = GetLog(LLDBLog::Process);

  m_delegate.ProcessStateChanged(this, state);

  switch (state) {
  case eStateStopped:
  case eStateExited:
  case eStateCrashed:
    NotifyTracersProcessDidStop();
    break;
  default:
    break;
  }

  LLDB_LOG(log, "sent state notification [{0}] from process {1}", state,
           GetID());
}

void NativeProcessProtocol::NotifyDidExec() {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "process {0} exec()ed", GetID());

  m_software_breakpoints.clear();

  m_delegate.DidExec(this);
}

Status NativeProcessProtocol::SetSoftwareBreakpoint(lldb::addr_t addr,
                                                    uint32_t size_hint) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOG(log, "addr = {0:x}, size_hint = {1}", addr, size_hint);

  auto it = m_software_breakpoints.find(addr);
  if (it != m_software_breakpoints.end()) {
    ++it->second.ref_count;
    return Status();
  }
  auto expected_bkpt = EnableSoftwareBreakpoint(addr, size_hint);
  if (!expected_bkpt)
    return Status(expected_bkpt.takeError());

  m_software_breakpoints.emplace(addr, std::move(*expected_bkpt));
  return Status();
}

Status NativeProcessProtocol::RemoveSoftwareBreakpoint(lldb::addr_t addr) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOG(log, "addr = {0:x}", addr);
  auto it = m_software_breakpoints.find(addr);
  if (it == m_software_breakpoints.end())
    return Status("Breakpoint not found.");
  assert(it->second.ref_count > 0);
  if (--it->second.ref_count > 0)
    return Status();

  // This is the last reference. Let's remove the breakpoint.
  Status error;

  // Clear a software breakpoint instruction
  llvm::SmallVector<uint8_t, 4> curr_break_op(
      it->second.breakpoint_opcodes.size(), 0);

  // Read the breakpoint opcode
  size_t bytes_read = 0;
  error =
      ReadMemory(addr, curr_break_op.data(), curr_break_op.size(), bytes_read);
  if (error.Fail() || bytes_read < curr_break_op.size()) {
    return Status("addr=0x%" PRIx64
                  ": tried to read %zu bytes but only read %zu",
                  addr, curr_break_op.size(), bytes_read);
  }
  const auto &saved = it->second.saved_opcodes;
  // Make sure the breakpoint opcode exists at this address
  if (llvm::ArrayRef(curr_break_op) != it->second.breakpoint_opcodes) {
    if (curr_break_op != it->second.saved_opcodes)
      return Status("Original breakpoint trap is no longer in memory.");
    LLDB_LOG(log,
             "Saved opcodes ({0:@[x]}) have already been restored at {1:x}.",
             llvm::make_range(saved.begin(), saved.end()), addr);
  } else {
    // We found a valid breakpoint opcode at this address, now restore the
    // saved opcode.
    size_t bytes_written = 0;
    error = WriteMemory(addr, saved.data(), saved.size(), bytes_written);
    if (error.Fail() || bytes_written < saved.size()) {
      return Status("addr=0x%" PRIx64
                    ": tried to write %zu bytes but only wrote %zu",
                    addr, saved.size(), bytes_written);
    }

    // Verify that our original opcode made it back to the inferior
    llvm::SmallVector<uint8_t, 4> verify_opcode(saved.size(), 0);
    size_t verify_bytes_read = 0;
    error = ReadMemory(addr, verify_opcode.data(), verify_opcode.size(),
                       verify_bytes_read);
    if (error.Fail() || verify_bytes_read < verify_opcode.size()) {
      return Status("addr=0x%" PRIx64
                    ": tried to read %zu verification bytes but only read %zu",
                    addr, verify_opcode.size(), verify_bytes_read);
    }
    if (verify_opcode != saved)
      LLDB_LOG(log, "Restoring bytes at {0:x}: {1:@[x]}", addr,
               llvm::make_range(saved.begin(), saved.end()));
  }

  m_software_breakpoints.erase(it);
  return Status();
}

llvm::Expected<NativeProcessProtocol::SoftwareBreakpoint>
NativeProcessProtocol::EnableSoftwareBreakpoint(lldb::addr_t addr,
                                                uint32_t size_hint) {
  Log *log = GetLog(LLDBLog::Breakpoints);

  auto expected_trap = GetSoftwareBreakpointTrapOpcode(size_hint);
  if (!expected_trap)
    return expected_trap.takeError();

  llvm::SmallVector<uint8_t, 4> saved_opcode_bytes(expected_trap->size(), 0);
  // Save the original opcodes by reading them so we can restore later.
  size_t bytes_read = 0;
  Status error = ReadMemory(addr, saved_opcode_bytes.data(),
                            saved_opcode_bytes.size(), bytes_read);
  if (error.Fail())
    return error.ToError();

  // Ensure we read as many bytes as we expected.
  if (bytes_read != saved_opcode_bytes.size()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Failed to read memory while attempting to set breakpoint: attempted "
        "to read {0} bytes but only read {1}.",
        saved_opcode_bytes.size(), bytes_read);
  }

  LLDB_LOG(
      log, "Overwriting bytes at {0:x}: {1:@[x]}", addr,
      llvm::make_range(saved_opcode_bytes.begin(), saved_opcode_bytes.end()));

  // Write a software breakpoint in place of the original opcode.
  size_t bytes_written = 0;
  error = WriteMemory(addr, expected_trap->data(), expected_trap->size(),
                      bytes_written);
  if (error.Fail())
    return error.ToError();

  // Ensure we wrote as many bytes as we expected.
  if (bytes_written != expected_trap->size()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Failed write memory while attempting to set "
        "breakpoint: attempted to write {0} bytes but only wrote {1}",
        expected_trap->size(), bytes_written);
  }

  llvm::SmallVector<uint8_t, 4> verify_bp_opcode_bytes(expected_trap->size(),
                                                       0);
  size_t verify_bytes_read = 0;
  error = ReadMemory(addr, verify_bp_opcode_bytes.data(),
                     verify_bp_opcode_bytes.size(), verify_bytes_read);
  if (error.Fail())
    return error.ToError();

  // Ensure we read as many verification bytes as we expected.
  if (verify_bytes_read != verify_bp_opcode_bytes.size()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Failed to read memory while "
        "attempting to verify breakpoint: attempted to read {0} bytes "
        "but only read {1}",
        verify_bp_opcode_bytes.size(), verify_bytes_read);
  }

  if (llvm::ArrayRef(verify_bp_opcode_bytes.data(), verify_bytes_read) !=
      *expected_trap) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Verification of software breakpoint "
        "writing failed - trap opcodes not successfully read back "
        "after writing when setting breakpoint at {0:x}",
        addr);
  }

  LLDB_LOG(log, "addr = {0:x}: SUCCESS", addr);
  return SoftwareBreakpoint{1, saved_opcode_bytes, *expected_trap};
}

llvm::Expected<llvm::ArrayRef<uint8_t>>
NativeProcessProtocol::GetSoftwareBreakpointTrapOpcode(size_t size_hint) {
  static const uint8_t g_aarch64_opcode[] = {0x00, 0x00, 0x20, 0xd4};
  static const uint8_t g_i386_opcode[] = {0xCC};
  static const uint8_t g_mips64_opcode[] = {0x00, 0x00, 0x00, 0x0d};
  static const uint8_t g_mips64el_opcode[] = {0x0d, 0x00, 0x00, 0x00};
  static const uint8_t g_msp430_opcode[] = {0x43, 0x43};
  static const uint8_t g_s390x_opcode[] = {0x00, 0x01};
  static const uint8_t g_ppc_opcode[] = {0x7f, 0xe0, 0x00, 0x08};   // trap
  static const uint8_t g_ppcle_opcode[] = {0x08, 0x00, 0xe0, 0x7f}; // trap
  static const uint8_t g_riscv_opcode[] = {0x73, 0x00, 0x10, 0x00}; // ebreak
  static const uint8_t g_riscv_opcode_c[] = {0x02, 0x90};           // c.ebreak
  static const uint8_t g_loongarch_opcode[] = {0x05, 0x00, 0x2a,
                                               0x00}; // break 0x5

  switch (GetArchitecture().GetMachine()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
    return llvm::ArrayRef(g_aarch64_opcode);

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return llvm::ArrayRef(g_i386_opcode);

  case llvm::Triple::mips:
  case llvm::Triple::mips64:
    return llvm::ArrayRef(g_mips64_opcode);

  case llvm::Triple::mipsel:
  case llvm::Triple::mips64el:
    return llvm::ArrayRef(g_mips64el_opcode);

  case llvm::Triple::msp430:
    return llvm::ArrayRef(g_msp430_opcode);

  case llvm::Triple::systemz:
    return llvm::ArrayRef(g_s390x_opcode);

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
    return llvm::ArrayRef(g_ppc_opcode);

  case llvm::Triple::ppc64le:
    return llvm::ArrayRef(g_ppcle_opcode);

  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64: {
    return size_hint == 2 ? llvm::ArrayRef(g_riscv_opcode_c)
                          : llvm::ArrayRef(g_riscv_opcode);
  }

  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    return llvm::ArrayRef(g_loongarch_opcode);

  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "CPU type not supported!");
  }
}

size_t NativeProcessProtocol::GetSoftwareBreakpointPCOffset() {
  switch (GetArchitecture().GetMachine()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
  case llvm::Triple::systemz:
    // These architectures report increment the PC after breakpoint is hit.
    return cantFail(GetSoftwareBreakpointTrapOpcode(0)).size();

  case llvm::Triple::arm:
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    // On these architectures the PC doesn't get updated for breakpoint hits.
    return 0;

  default:
    llvm_unreachable("CPU type not supported!");
  }
}

void NativeProcessProtocol::FixupBreakpointPCAsNeeded(
    NativeThreadProtocol &thread) {
  Log *log = GetLog(LLDBLog::Breakpoints);

  Status error;

  // Find out the size of a breakpoint (might depend on where we are in the
  // code).
  NativeRegisterContext &context = thread.GetRegisterContext();

  uint32_t breakpoint_size = GetSoftwareBreakpointPCOffset();
  LLDB_LOG(log, "breakpoint size: {0}", breakpoint_size);
  if (breakpoint_size == 0)
    return;

  // First try probing for a breakpoint at a software breakpoint location: PC -
  // breakpoint size.
  const lldb::addr_t initial_pc_addr = context.GetPCfromBreakpointLocation();
  lldb::addr_t breakpoint_addr = initial_pc_addr;
  // Do not allow breakpoint probe to wrap around.
  if (breakpoint_addr >= breakpoint_size)
    breakpoint_addr -= breakpoint_size;

  if (m_software_breakpoints.count(breakpoint_addr) == 0) {
    // We didn't find one at a software probe location.  Nothing to do.
    LLDB_LOG(log,
             "pid {0} no lldb software breakpoint found at current pc with "
             "adjustment: {1}",
             GetID(), breakpoint_addr);
    return;
  }

  //
  // We have a software breakpoint and need to adjust the PC.
  //

  // Change the program counter.
  LLDB_LOG(log, "pid {0} tid {1}: changing PC from {2:x} to {3:x}", GetID(),
           thread.GetID(), initial_pc_addr, breakpoint_addr);

  error = context.SetPC(breakpoint_addr);
  if (error.Fail()) {
    // This can happen in case the process was killed between the time we read
    // the PC and when we are updating it. There's nothing better to do than to
    // swallow the error.
    LLDB_LOG(log, "pid {0} tid {1}: failed to set PC: {2}", GetID(),
             thread.GetID(), error);
  }
}

Status NativeProcessProtocol::RemoveBreakpoint(lldb::addr_t addr,
                                               bool hardware) {
  if (hardware)
    return RemoveHardwareBreakpoint(addr);
  else
    return RemoveSoftwareBreakpoint(addr);
}

Status NativeProcessProtocol::ReadMemoryWithoutTrap(lldb::addr_t addr,
                                                    void *buf, size_t size,
                                                    size_t &bytes_read) {
  Status error = ReadMemory(addr, buf, size, bytes_read);
  if (error.Fail())
    return error;

  llvm::MutableArrayRef data(static_cast<uint8_t *>(buf), bytes_read);
  for (const auto &pair : m_software_breakpoints) {
    lldb::addr_t bp_addr = pair.first;
    auto saved_opcodes = llvm::ArrayRef(pair.second.saved_opcodes);

    if (bp_addr + saved_opcodes.size() < addr || addr + bytes_read <= bp_addr)
      continue; // Breakpoint not in range, ignore

    if (bp_addr < addr) {
      saved_opcodes = saved_opcodes.drop_front(addr - bp_addr);
      bp_addr = addr;
    }
    auto bp_data = data.drop_front(bp_addr - addr);
    std::copy_n(saved_opcodes.begin(),
                std::min(saved_opcodes.size(), bp_data.size()),
                bp_data.begin());
  }
  return Status();
}

llvm::Expected<llvm::StringRef>
NativeProcessProtocol::ReadCStringFromMemory(lldb::addr_t addr, char *buffer,
                                             size_t max_size,
                                             size_t &total_bytes_read) {
  static const size_t cache_line_size =
      llvm::sys::Process::getPageSizeEstimate();
  size_t bytes_read = 0;
  size_t bytes_left = max_size;
  addr_t curr_addr = addr;
  size_t string_size;
  char *curr_buffer = buffer;
  total_bytes_read = 0;
  Status status;

  while (bytes_left > 0 && status.Success()) {
    addr_t cache_line_bytes_left =
        cache_line_size - (curr_addr % cache_line_size);
    addr_t bytes_to_read = std::min<addr_t>(bytes_left, cache_line_bytes_left);
    status = ReadMemory(curr_addr, static_cast<void *>(curr_buffer),
                        bytes_to_read, bytes_read);

    if (bytes_read == 0)
      break;

    void *str_end = std::memchr(curr_buffer, '\0', bytes_read);
    if (str_end != nullptr) {
      total_bytes_read =
          static_cast<size_t>((static_cast<char *>(str_end) - buffer + 1));
      status.Clear();
      break;
    }

    total_bytes_read += bytes_read;
    curr_buffer += bytes_read;
    curr_addr += bytes_read;
    bytes_left -= bytes_read;
  }

  string_size = total_bytes_read - 1;

  // Make sure we return a null terminated string.
  if (bytes_left == 0 && max_size > 0 && buffer[max_size - 1] != '\0') {
    buffer[max_size - 1] = '\0';
    total_bytes_read--;
  }

  if (!status.Success())
    return status.ToError();

  return llvm::StringRef(buffer, string_size);
}

lldb::StateType NativeProcessProtocol::GetState() const {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);
  return m_state;
}

void NativeProcessProtocol::SetState(lldb::StateType state,
                                     bool notify_delegates) {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);

  if (state == m_state)
    return;

  m_state = state;

  if (StateIsStoppedState(state, false)) {
    ++m_stop_id;

    // Give process a chance to do any stop id bump processing, such as
    // clearing cached data that is invalidated each time the process runs.
    // Note if/when we support some threads running, we'll end up needing to
    // manage this per thread and per process.
    DoStopIDBumped(m_stop_id);
  }

  // Optionally notify delegates of the state change.
  if (notify_delegates)
    SynchronouslyNotifyProcessStateChanged(state);
}

uint32_t NativeProcessProtocol::GetStopID() const {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);
  return m_stop_id;
}

void NativeProcessProtocol::DoStopIDBumped(uint32_t /* newBumpId */) {
  // Default implementation does nothing.
}

NativeProcessProtocol::Manager::~Manager() = default;
