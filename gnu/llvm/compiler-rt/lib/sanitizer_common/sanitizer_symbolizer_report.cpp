//===-- sanitizer_symbolizer_report.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file is shared between AddressSanitizer and other sanitizer run-time
/// libraries and implements symbolized reports related functions.
///
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_file.h"
#include "sanitizer_flags.h"
#include "sanitizer_procmaps.h"
#include "sanitizer_report_decorator.h"
#include "sanitizer_stacktrace.h"
#include "sanitizer_stacktrace_printer.h"
#include "sanitizer_symbolizer.h"

#if SANITIZER_POSIX
# include "sanitizer_posix.h"
# include <sys/mman.h>
#endif

namespace __sanitizer {

#if !SANITIZER_GO

static bool FrameIsInternal(const SymbolizedStack *frame) {
  if (!frame)
    return true;
  const char *file = frame->info.file;
  const char *module = frame->info.module;
  // On Gentoo, the path is g++-*, so there's *not* a missing /.
  if (file && (internal_strstr(file, "/compiler-rt/lib/") ||
               internal_strstr(file, "/include/c++/") ||
               internal_strstr(file, "/include/g++")))
    return true;
  if (file && internal_strstr(file, "\\compiler-rt\\lib\\"))
    return true;
  if (module && (internal_strstr(module, "libclang_rt.")))
    return true;
  if (module && (internal_strstr(module, "clang_rt.")))
    return true;
  return false;
}

const SymbolizedStack *SkipInternalFrames(const SymbolizedStack *frames) {
  for (const SymbolizedStack *f = frames; f; f = f->next)
    if (!FrameIsInternal(f))
      return f;
  return nullptr;
}

void ReportErrorSummary(const char *error_type, const AddressInfo &info,
                        const char *alt_tool_name) {
  if (!common_flags()->print_summary) return;
  InternalScopedString buff;
  buff.AppendF("%s ", error_type);
  StackTracePrinter::GetOrInit()->RenderFrame(
      &buff, "%L %F", 0, info.address, &info,
      common_flags()->symbolize_vs_style, common_flags()->strip_path_prefix);
  ReportErrorSummary(buff.data(), alt_tool_name);
}
#endif

#if !SANITIZER_FUCHSIA

bool ReportFile::SupportsColors() {
  SpinMutexLock l(mu);
  ReopenIfNecessary();
  return SupportsColoredOutput(fd);
}

static inline bool ReportSupportsColors() {
  return report_file.SupportsColors();
}

#else  // SANITIZER_FUCHSIA

// Fuchsia's logs always go through post-processing that handles colorization.
static inline bool ReportSupportsColors() { return true; }

#endif  // !SANITIZER_FUCHSIA

bool ColorizeReports() {
  // FIXME: Add proper Windows support to AnsiColorDecorator and re-enable color
  // printing on Windows.
  if (SANITIZER_WINDOWS)
    return false;

  const char *flag = common_flags()->color;
  return internal_strcmp(flag, "always") == 0 ||
         (internal_strcmp(flag, "auto") == 0 && ReportSupportsColors());
}

void ReportErrorSummary(const char *error_type, const StackTrace *stack,
                        const char *alt_tool_name) {
#if !SANITIZER_GO
  if (!common_flags()->print_summary)
    return;

  // Find first non-internal stack frame.
  for (uptr i = 0; i < stack->size; ++i) {
    uptr pc = StackTrace::GetPreviousInstructionPc(stack->trace[i]);
    SymbolizedStackHolder symbolized_stack(
        Symbolizer::GetOrInit()->SymbolizePC(pc));
    if (const SymbolizedStack *frame = symbolized_stack.get()) {
      if (const SymbolizedStack *summary_frame = SkipInternalFrames(frame)) {
        ReportErrorSummary(error_type, summary_frame->info, alt_tool_name);
        return;
      }
    }
  }

  // Fallback to the top one.
  if (stack->size) {
    uptr pc = StackTrace::GetPreviousInstructionPc(stack->trace[0]);
    SymbolizedStackHolder symbolized_stack(
        Symbolizer::GetOrInit()->SymbolizePC(pc));
    if (const SymbolizedStack *frame = symbolized_stack.get()) {
      ReportErrorSummary(error_type, frame->info, alt_tool_name);
      return;
    }
  }

  // Fallback to a summary without location.
  ReportErrorSummary(error_type);
#endif
}

void ReportMmapWriteExec(int prot, int flags) {
#if SANITIZER_POSIX && (!SANITIZER_GO && !SANITIZER_ANDROID)
  int pflags = (PROT_WRITE | PROT_EXEC);
  if ((prot & pflags) != pflags)
    return;

#  if SANITIZER_APPLE && defined(MAP_JIT)
  if ((flags & MAP_JIT) == MAP_JIT)
    return;
#  endif

  ScopedErrorReportLock l;
  SanitizerCommonDecorator d;

  InternalMmapVector<BufferedStackTrace> stack_buffer(1);
  BufferedStackTrace *stack = stack_buffer.data();
  stack->Reset();
  uptr top = 0;
  uptr bottom = 0;
  GET_CALLER_PC_BP;
  bool fast = common_flags()->fast_unwind_on_fatal;
  if (StackTrace::WillUseFastUnwind(fast)) {
    GetThreadStackTopAndBottom(false, &top, &bottom);
    stack->Unwind(kStackTraceMax, pc, bp, nullptr, top, bottom, true);
  } else {
    stack->Unwind(kStackTraceMax, pc, 0, nullptr, 0, 0, false);
  }

  Printf("%s", d.Warning());
  Report("WARNING: %s: writable-executable page usage\n", SanitizerToolName);
  Printf("%s", d.Default());

  stack->Print();
  ReportErrorSummary("w-and-x-usage", stack);
#endif
}

#if !SANITIZER_FUCHSIA && !SANITIZER_GO
void StartReportDeadlySignal() {
  // Write the first message using fd=2, just in case.
  // It may actually fail to write in case stderr is closed.
  CatastrophicErrorWrite(SanitizerToolName, internal_strlen(SanitizerToolName));
  static const char kDeadlySignal[] = ":DEADLYSIGNAL\n";
  CatastrophicErrorWrite(kDeadlySignal, sizeof(kDeadlySignal) - 1);
}

static void MaybeReportNonExecRegion(uptr pc) {
#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD
  MemoryMappingLayout proc_maps(/*cache_enabled*/ true);
  MemoryMappedSegment segment;
  while (proc_maps.Next(&segment)) {
    if (pc >= segment.start && pc < segment.end && !segment.IsExecutable())
      Report("Hint: PC is at a non-executable region. Maybe a wild jump?\n");
  }
#endif
}

static void PrintMemoryByte(InternalScopedString *str, const char *before,
                            u8 byte) {
  SanitizerCommonDecorator d;
  str->AppendF("%s%s%x%x%s ", before, d.MemoryByte(), byte >> 4, byte & 15,
               d.Default());
}

static void MaybeDumpInstructionBytes(uptr pc) {
  if (!common_flags()->dump_instruction_bytes || (pc < GetPageSizeCached()))
    return;
  InternalScopedString str;
  str.AppendF("First 16 instruction bytes at pc: ");
  if (IsAccessibleMemoryRange(pc, 16)) {
    for (int i = 0; i < 16; ++i) {
      PrintMemoryByte(&str, "", ((u8 *)pc)[i]);
    }
    str.AppendF("\n");
  } else {
    str.AppendF("unaccessible\n");
  }
  Report("%s", str.data());
}

static void MaybeDumpRegisters(void *context) {
  if (!common_flags()->dump_registers) return;
  SignalContext::DumpAllRegisters(context);
}

static void ReportStackOverflowImpl(const SignalContext &sig, u32 tid,
                                    UnwindSignalStackCallbackType unwind,
                                    const void *unwind_context) {
  SanitizerCommonDecorator d;
  Printf("%s", d.Warning());
  static const char kDescription[] = "stack-overflow";
  Report("ERROR: %s: %s on address %p (pc %p bp %p sp %p T%d)\n",
         SanitizerToolName, kDescription, (void *)sig.addr, (void *)sig.pc,
         (void *)sig.bp, (void *)sig.sp, tid);
  Printf("%s", d.Default());
  InternalMmapVector<BufferedStackTrace> stack_buffer(1);
  BufferedStackTrace *stack = stack_buffer.data();
  stack->Reset();
  unwind(sig, unwind_context, stack);
  stack->Print();
  ReportErrorSummary(kDescription, stack);
}

static void ReportDeadlySignalImpl(const SignalContext &sig, u32 tid,
                                   UnwindSignalStackCallbackType unwind,
                                   const void *unwind_context) {
  SanitizerCommonDecorator d;
  Printf("%s", d.Warning());
  const char *description = sig.Describe();
  if (sig.is_memory_access && !sig.is_true_faulting_addr)
    Report("ERROR: %s: %s on unknown address (pc %p bp %p sp %p T%d)\n",
           SanitizerToolName, description, (void *)sig.pc, (void *)sig.bp,
           (void *)sig.sp, tid);
  else
    Report("ERROR: %s: %s on unknown address %p (pc %p bp %p sp %p T%d)\n",
           SanitizerToolName, description, (void *)sig.addr, (void *)sig.pc,
           (void *)sig.bp, (void *)sig.sp, tid);
  Printf("%s", d.Default());
  if (sig.pc < GetPageSizeCached())
    Report("Hint: pc points to the zero page.\n");
  if (sig.is_memory_access) {
    const char *access_type =
        sig.write_flag == SignalContext::Write
            ? "WRITE"
            : (sig.write_flag == SignalContext::Read ? "READ" : "UNKNOWN");
    Report("The signal is caused by a %s memory access.\n", access_type);
    if (!sig.is_true_faulting_addr)
      Report("Hint: this fault was caused by a dereference of a high value "
             "address (see register values below).  Disassemble the provided "
             "pc to learn which register was used.\n");
    else if (sig.addr < GetPageSizeCached())
      Report("Hint: address points to the zero page.\n");
  }
  MaybeReportNonExecRegion(sig.pc);
  InternalMmapVector<BufferedStackTrace> stack_buffer(1);
  BufferedStackTrace *stack = stack_buffer.data();
  stack->Reset();
  unwind(sig, unwind_context, stack);
  stack->Print();
  MaybeDumpInstructionBytes(sig.pc);
  MaybeDumpRegisters(sig.context);
  Printf("%s can not provide additional info.\n", SanitizerToolName);
  ReportErrorSummary(description, stack);
}

void ReportDeadlySignal(const SignalContext &sig, u32 tid,
                        UnwindSignalStackCallbackType unwind,
                        const void *unwind_context) {
  if (sig.IsStackOverflow())
    ReportStackOverflowImpl(sig, tid, unwind, unwind_context);
  else
    ReportDeadlySignalImpl(sig, tid, unwind, unwind_context);
}

void HandleDeadlySignal(void *siginfo, void *context, u32 tid,
                        UnwindSignalStackCallbackType unwind,
                        const void *unwind_context) {
  StartReportDeadlySignal();
  ScopedErrorReportLock rl;
  SignalContext sig(siginfo, context);
  ReportDeadlySignal(sig, tid, unwind, unwind_context);
  Report("ABORTING\n");
  Die();
}

#endif  // !SANITIZER_FUCHSIA && !SANITIZER_GO

atomic_uintptr_t ScopedErrorReportLock::reporting_thread_ = {0};
StaticSpinMutex ScopedErrorReportLock::mutex_;

void ScopedErrorReportLock::Lock() {
  uptr current = GetThreadSelf();
  for (;;) {
    uptr expected = 0;
    if (atomic_compare_exchange_strong(&reporting_thread_, &expected, current,
                                       memory_order_relaxed)) {
      // We've claimed reporting_thread so proceed.
      mutex_.Lock();
      return;
    }

    if (expected == current) {
      // This is either asynch signal or nested error during error reporting.
      // Fail simple to avoid deadlocks in Report().

      // Can't use Report() here because of potential deadlocks in nested
      // signal handlers.
      CatastrophicErrorWrite(SanitizerToolName,
                             internal_strlen(SanitizerToolName));
      static const char msg[] = ": nested bug in the same thread, aborting.\n";
      CatastrophicErrorWrite(msg, sizeof(msg) - 1);

      internal__exit(common_flags()->exitcode);
    }

    internal_sched_yield();
  }
}

void ScopedErrorReportLock::Unlock() {
  mutex_.Unlock();
  atomic_store_relaxed(&reporting_thread_, 0);
}

void ScopedErrorReportLock::CheckLocked() { mutex_.CheckLocked(); }

}  // namespace __sanitizer
