//===-- asan_report.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file contains error reporting code.
//===----------------------------------------------------------------------===//

#include "asan_report.h"

#include "asan_descriptions.h"
#include "asan_errors.h"
#include "asan_flags.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_scariness_score.h"
#include "asan_stack.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_interface_internal.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_report_decorator.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

namespace __asan {

// -------------------- User-specified callbacks ----------------- {{{1
static void (*error_report_callback)(const char*);
using ErrorMessageBuffer = InternalMmapVectorNoCtor<char, true>;
alignas(
    alignof(ErrorMessageBuffer)) static char error_message_buffer_placeholder
    [sizeof(ErrorMessageBuffer)];
static ErrorMessageBuffer *error_message_buffer = nullptr;
static Mutex error_message_buf_mutex;
static const unsigned kAsanBuggyPcPoolSize = 25;
static __sanitizer::atomic_uintptr_t AsanBuggyPcPool[kAsanBuggyPcPoolSize];

void AppendToErrorMessageBuffer(const char *buffer) {
  Lock l(&error_message_buf_mutex);
  if (!error_message_buffer) {
    error_message_buffer =
        new (error_message_buffer_placeholder) ErrorMessageBuffer();
    error_message_buffer->Initialize(kErrorMessageBufferSize);
  }
  uptr error_message_buffer_len = error_message_buffer->size();
  uptr buffer_len = internal_strlen(buffer);
  error_message_buffer->resize(error_message_buffer_len + buffer_len);
  internal_memcpy(error_message_buffer->data() + error_message_buffer_len,
                  buffer, buffer_len);
}

// ---------------------- Helper functions ----------------------- {{{1

void PrintMemoryByte(InternalScopedString *str, const char *before, u8 byte,
                     bool in_shadow, const char *after) {
  Decorator d;
  str->AppendF("%s%s%x%x%s%s", before,
               in_shadow ? d.ShadowByte(byte) : d.MemoryByte(), byte >> 4,
               byte & 15, d.Default(), after);
}

static void PrintZoneForPointer(uptr ptr, uptr zone_ptr,
                                const char *zone_name) {
  if (zone_ptr) {
    if (zone_name) {
      Printf("malloc_zone_from_ptr(%p) = %p, which is %s\n", (void *)ptr,
             (void *)zone_ptr, zone_name);
    } else {
      Printf("malloc_zone_from_ptr(%p) = %p, which doesn't have a name\n",
             (void *)ptr, (void *)zone_ptr);
    }
  } else {
    Printf("malloc_zone_from_ptr(%p) = 0\n", (void *)ptr);
  }
}

// ---------------------- Address Descriptions ------------------- {{{1

bool ParseFrameDescription(const char *frame_descr,
                           InternalMmapVector<StackVarDescr> *vars) {
  CHECK(frame_descr);
  const char *p;
  // This string is created by the compiler and has the following form:
  // "n alloc_1 alloc_2 ... alloc_n"
  // where alloc_i looks like "offset size len ObjectName"
  // or                       "offset size len ObjectName:line".
  uptr n_objects = (uptr)internal_simple_strtoll(frame_descr, &p, 10);
  if (n_objects == 0)
    return false;

  for (uptr i = 0; i < n_objects; i++) {
    uptr beg  = (uptr)internal_simple_strtoll(p, &p, 10);
    uptr size = (uptr)internal_simple_strtoll(p, &p, 10);
    uptr len  = (uptr)internal_simple_strtoll(p, &p, 10);
    if (beg == 0 || size == 0 || *p != ' ') {
      return false;
    }
    p++;
    char *colon_pos = internal_strchr(p, ':');
    uptr line = 0;
    uptr name_len = len;
    if (colon_pos != nullptr && colon_pos < p + len) {
      name_len = colon_pos - p;
      line = (uptr)internal_simple_strtoll(colon_pos + 1, nullptr, 10);
    }
    StackVarDescr var = {beg, size, p, name_len, line};
    vars->push_back(var);
    p += len;
  }

  return true;
}

// -------------------- Different kinds of reports ----------------- {{{1

// Use ScopedInErrorReport to run common actions just before and
// immediately after printing error report.
class ScopedInErrorReport {
 public:
  explicit ScopedInErrorReport(bool fatal = false)
      : halt_on_error_(fatal || flags()->halt_on_error) {
    // Make sure the registry and sanitizer report mutexes are locked while
    // we're printing an error report.
    // We can lock them only here to avoid self-deadlock in case of
    // recursive reports.
    asanThreadRegistry().Lock();
    Printf(
        "=================================================================\n");
  }

  ~ScopedInErrorReport() {
    if (halt_on_error_ && !__sanitizer_acquire_crash_state()) {
      asanThreadRegistry().Unlock();
      return;
    }
    ASAN_ON_ERROR();
    if (current_error_.IsValid()) current_error_.Print();

    // Make sure the current thread is announced.
    DescribeThread(GetCurrentThread());
    // We may want to grab this lock again when printing stats.
    asanThreadRegistry().Unlock();
    // Print memory stats.
    if (flags()->print_stats)
      __asan_print_accumulated_stats();

    if (common_flags()->print_cmdline)
      PrintCmdline();

    if (common_flags()->print_module_map == 2)
      DumpProcessMap();

    // Copy the message buffer so that we could start logging without holding a
    // lock that gets acquired during printing.
    InternalScopedString buffer_copy;
    {
      Lock l(&error_message_buf_mutex);
      error_message_buffer->push_back('\0');
      buffer_copy.Append(error_message_buffer->data());
      // Clear error_message_buffer so that if we find other errors
      // we don't re-log this error.
      error_message_buffer->clear();
    }

    LogFullErrorReport(buffer_copy.data());

    if (error_report_callback) {
      error_report_callback(buffer_copy.data());
    }

    if (halt_on_error_ && common_flags()->abort_on_error) {
      // On Android the message is truncated to 512 characters.
      // FIXME: implement "compact" error format, possibly without, or with
      // highly compressed stack traces?
      // FIXME: or just use the summary line as abort message?
      SetAbortMessage(buffer_copy.data());
    }

    // In halt_on_error = false mode, reset the current error object (before
    // unlocking).
    if (!halt_on_error_)
      internal_memset(&current_error_, 0, sizeof(current_error_));

    if (halt_on_error_) {
      Report("ABORTING\n");
      Die();
    }
  }

  void ReportError(const ErrorDescription &description) {
    // Can only report one error per ScopedInErrorReport.
    CHECK_EQ(current_error_.kind, kErrorKindInvalid);
    internal_memcpy(&current_error_, &description, sizeof(current_error_));
  }

  static ErrorDescription &CurrentError() {
    return current_error_;
  }

 private:
  ScopedErrorReportLock error_report_lock_;
  // Error currently being reported. This enables the destructor to interact
  // with the debugger and point it to an error description.
  static ErrorDescription current_error_;
  bool halt_on_error_;
};

ErrorDescription ScopedInErrorReport::current_error_(LINKER_INITIALIZED);

void ReportDeadlySignal(const SignalContext &sig) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorDeadlySignal error(GetCurrentTidOrInvalid(), sig);
  in_report.ReportError(error);
}

void ReportDoubleFree(uptr addr, BufferedStackTrace *free_stack) {
  ScopedInErrorReport in_report;
  ErrorDoubleFree error(GetCurrentTidOrInvalid(), free_stack, addr);
  in_report.ReportError(error);
}

void ReportNewDeleteTypeMismatch(uptr addr, uptr delete_size,
                                 uptr delete_alignment,
                                 BufferedStackTrace *free_stack) {
  ScopedInErrorReport in_report;
  ErrorNewDeleteTypeMismatch error(GetCurrentTidOrInvalid(), free_stack, addr,
                                   delete_size, delete_alignment);
  in_report.ReportError(error);
}

void ReportFreeNotMalloced(uptr addr, BufferedStackTrace *free_stack) {
  ScopedInErrorReport in_report;
  ErrorFreeNotMalloced error(GetCurrentTidOrInvalid(), free_stack, addr);
  in_report.ReportError(error);
}

void ReportAllocTypeMismatch(uptr addr, BufferedStackTrace *free_stack,
                             AllocType alloc_type,
                             AllocType dealloc_type) {
  ScopedInErrorReport in_report;
  ErrorAllocTypeMismatch error(GetCurrentTidOrInvalid(), free_stack, addr,
                               alloc_type, dealloc_type);
  in_report.ReportError(error);
}

void ReportMallocUsableSizeNotOwned(uptr addr, BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  ErrorMallocUsableSizeNotOwned error(GetCurrentTidOrInvalid(), stack, addr);
  in_report.ReportError(error);
}

void ReportSanitizerGetAllocatedSizeNotOwned(uptr addr,
                                             BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  ErrorSanitizerGetAllocatedSizeNotOwned error(GetCurrentTidOrInvalid(), stack,
                                               addr);
  in_report.ReportError(error);
}

void ReportCallocOverflow(uptr count, uptr size, BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorCallocOverflow error(GetCurrentTidOrInvalid(), stack, count, size);
  in_report.ReportError(error);
}

void ReportReallocArrayOverflow(uptr count, uptr size,
                                BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorReallocArrayOverflow error(GetCurrentTidOrInvalid(), stack, count, size);
  in_report.ReportError(error);
}

void ReportPvallocOverflow(uptr size, BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorPvallocOverflow error(GetCurrentTidOrInvalid(), stack, size);
  in_report.ReportError(error);
}

void ReportInvalidAllocationAlignment(uptr alignment,
                                      BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorInvalidAllocationAlignment error(GetCurrentTidOrInvalid(), stack,
                                        alignment);
  in_report.ReportError(error);
}

void ReportInvalidAlignedAllocAlignment(uptr size, uptr alignment,
                                        BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorInvalidAlignedAllocAlignment error(GetCurrentTidOrInvalid(), stack,
                                          size, alignment);
  in_report.ReportError(error);
}

void ReportInvalidPosixMemalignAlignment(uptr alignment,
                                         BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorInvalidPosixMemalignAlignment error(GetCurrentTidOrInvalid(), stack,
                                           alignment);
  in_report.ReportError(error);
}

void ReportAllocationSizeTooBig(uptr user_size, uptr total_size, uptr max_size,
                                BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorAllocationSizeTooBig error(GetCurrentTidOrInvalid(), stack, user_size,
                                  total_size, max_size);
  in_report.ReportError(error);
}

void ReportRssLimitExceeded(BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorRssLimitExceeded error(GetCurrentTidOrInvalid(), stack);
  in_report.ReportError(error);
}

void ReportOutOfMemory(uptr requested_size, BufferedStackTrace *stack) {
  ScopedInErrorReport in_report(/*fatal*/ true);
  ErrorOutOfMemory error(GetCurrentTidOrInvalid(), stack, requested_size);
  in_report.ReportError(error);
}

void ReportStringFunctionMemoryRangesOverlap(const char *function,
                                             const char *offset1, uptr length1,
                                             const char *offset2, uptr length2,
                                             BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  ErrorStringFunctionMemoryRangesOverlap error(
      GetCurrentTidOrInvalid(), stack, (uptr)offset1, length1, (uptr)offset2,
      length2, function);
  in_report.ReportError(error);
}

void ReportStringFunctionSizeOverflow(uptr offset, uptr size,
                                      BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  ErrorStringFunctionSizeOverflow error(GetCurrentTidOrInvalid(), stack, offset,
                                        size);
  in_report.ReportError(error);
}

void ReportBadParamsToAnnotateContiguousContainer(uptr beg, uptr end,
                                                  uptr old_mid, uptr new_mid,
                                                  BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  ErrorBadParamsToAnnotateContiguousContainer error(
      GetCurrentTidOrInvalid(), stack, beg, end, old_mid, new_mid);
  in_report.ReportError(error);
}

void ReportBadParamsToAnnotateDoubleEndedContiguousContainer(
    uptr storage_beg, uptr storage_end, uptr old_container_beg,
    uptr old_container_end, uptr new_container_beg, uptr new_container_end,
    BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  ErrorBadParamsToAnnotateDoubleEndedContiguousContainer error(
      GetCurrentTidOrInvalid(), stack, storage_beg, storage_end,
      old_container_beg, old_container_end, new_container_beg,
      new_container_end);
  in_report.ReportError(error);
}

void ReportODRViolation(const __asan_global *g1, u32 stack_id1,
                        const __asan_global *g2, u32 stack_id2) {
  ScopedInErrorReport in_report;
  ErrorODRViolation error(GetCurrentTidOrInvalid(), g1, stack_id1, g2,
                          stack_id2);
  in_report.ReportError(error);
}

// ----------------------- CheckForInvalidPointerPair ----------- {{{1
static NOINLINE void ReportInvalidPointerPair(uptr pc, uptr bp, uptr sp,
                                              uptr a1, uptr a2) {
  ScopedInErrorReport in_report;
  ErrorInvalidPointerPair error(GetCurrentTidOrInvalid(), pc, bp, sp, a1, a2);
  in_report.ReportError(error);
}

static bool IsInvalidPointerPair(uptr a1, uptr a2) {
  if (a1 == a2)
    return false;

  // 256B in shadow memory can be iterated quite fast
  static const uptr kMaxOffset = 2048;

  uptr left = a1 < a2 ? a1 : a2;
  uptr right = a1 < a2 ? a2 : a1;
  uptr offset = right - left;
  if (offset <= kMaxOffset)
    return __asan_region_is_poisoned(left, offset);

  AsanThread *t = GetCurrentThread();

  // check whether left is a stack memory pointer
  if (uptr shadow_offset1 = t->GetStackVariableShadowStart(left)) {
    uptr shadow_offset2 = t->GetStackVariableShadowStart(right);
    return shadow_offset2 == 0 || shadow_offset1 != shadow_offset2;
  }

  // check whether left is a heap memory address
  HeapAddressDescription hdesc1, hdesc2;
  if (GetHeapAddressInformation(left, 0, &hdesc1) &&
      hdesc1.chunk_access.access_type == kAccessTypeInside)
    return !GetHeapAddressInformation(right, 0, &hdesc2) ||
        hdesc2.chunk_access.access_type != kAccessTypeInside ||
        hdesc1.chunk_access.chunk_begin != hdesc2.chunk_access.chunk_begin;

  // check whether left is an address of a global variable
  GlobalAddressDescription gdesc1, gdesc2;
  if (GetGlobalAddressInformation(left, 0, &gdesc1))
    return !GetGlobalAddressInformation(right - 1, 0, &gdesc2) ||
        !gdesc1.PointsInsideTheSameVariable(gdesc2);

  if (t->GetStackVariableShadowStart(right) ||
      GetHeapAddressInformation(right, 0, &hdesc2) ||
      GetGlobalAddressInformation(right - 1, 0, &gdesc2))
    return true;

  // At this point we know nothing about both a1 and a2 addresses.
  return false;
}

static inline void CheckForInvalidPointerPair(void *p1, void *p2) {
  switch (flags()->detect_invalid_pointer_pairs) {
    case 0:
      return;
    case 1:
      if (p1 == nullptr || p2 == nullptr)
        return;
      break;
  }

  uptr a1 = reinterpret_cast<uptr>(p1);
  uptr a2 = reinterpret_cast<uptr>(p2);

  if (IsInvalidPointerPair(a1, a2)) {
    GET_CALLER_PC_BP_SP;
    ReportInvalidPointerPair(pc, bp, sp, a1, a2);
  }
}
// ----------------------- Mac-specific reports ----------------- {{{1

void ReportMacMzReallocUnknown(uptr addr, uptr zone_ptr, const char *zone_name,
                               BufferedStackTrace *stack) {
  ScopedInErrorReport in_report;
  Printf(
      "mz_realloc(%p) -- attempting to realloc unallocated memory.\n"
      "This is an unrecoverable problem, exiting now.\n",
      (void *)addr);
  PrintZoneForPointer(addr, zone_ptr, zone_name);
  stack->Print();
  DescribeAddressIfHeap(addr);
}

// -------------- SuppressErrorReport -------------- {{{1
// Avoid error reports duplicating for ASan recover mode.
static bool SuppressErrorReport(uptr pc) {
  if (!common_flags()->suppress_equal_pcs) return false;
  for (unsigned i = 0; i < kAsanBuggyPcPoolSize; i++) {
    uptr cmp = atomic_load_relaxed(&AsanBuggyPcPool[i]);
    if (cmp == 0 && atomic_compare_exchange_strong(&AsanBuggyPcPool[i], &cmp,
                                                   pc, memory_order_relaxed))
      return false;
    if (cmp == pc) return true;
  }
  Die();
}

void ReportGenericError(uptr pc, uptr bp, uptr sp, uptr addr, bool is_write,
                        uptr access_size, u32 exp, bool fatal) {
  if (__asan_test_only_reported_buggy_pointer) {
    *__asan_test_only_reported_buggy_pointer = addr;
    return;
  }
  if (!fatal && SuppressErrorReport(pc)) return;
  ENABLE_FRAME_POINTER;

  // Optimization experiments.
  // The experiments can be used to evaluate potential optimizations that remove
  // instrumentation (assess false negatives). Instead of completely removing
  // some instrumentation, compiler can emit special calls into runtime
  // (e.g. __asan_report_exp_load1 instead of __asan_report_load1) and pass
  // mask of experiments (exp).
  // The reaction to a non-zero value of exp is to be defined.
  (void)exp;

  ScopedInErrorReport in_report(fatal);
  ErrorGeneric error(GetCurrentTidOrInvalid(), pc, bp, sp, addr, is_write,
                     access_size);
  in_report.ReportError(error);
}

}  // namespace __asan

// --------------------------- Interface --------------------- {{{1
using namespace __asan;

void __asan_report_error(uptr pc, uptr bp, uptr sp, uptr addr, int is_write,
                         uptr access_size, u32 exp) {
  ENABLE_FRAME_POINTER;
  bool fatal = flags()->halt_on_error;
  ReportGenericError(pc, bp, sp, addr, is_write, access_size, exp, fatal);
}

void NOINLINE __asan_set_error_report_callback(void (*callback)(const char*)) {
  Lock l(&error_message_buf_mutex);
  error_report_callback = callback;
}

void __asan_describe_address(uptr addr) {
  // Thread registry must be locked while we're describing an address.
  asanThreadRegistry().Lock();
  PrintAddressDescription(addr, 1, "");
  asanThreadRegistry().Unlock();
}

int __asan_report_present() {
  return ScopedInErrorReport::CurrentError().kind != kErrorKindInvalid;
}

uptr __asan_get_report_pc() {
  if (ScopedInErrorReport::CurrentError().kind == kErrorKindGeneric)
    return ScopedInErrorReport::CurrentError().Generic.pc;
  return 0;
}

uptr __asan_get_report_bp() {
  if (ScopedInErrorReport::CurrentError().kind == kErrorKindGeneric)
    return ScopedInErrorReport::CurrentError().Generic.bp;
  return 0;
}

uptr __asan_get_report_sp() {
  if (ScopedInErrorReport::CurrentError().kind == kErrorKindGeneric)
    return ScopedInErrorReport::CurrentError().Generic.sp;
  return 0;
}

uptr __asan_get_report_address() {
  ErrorDescription &err = ScopedInErrorReport::CurrentError();
  if (err.kind == kErrorKindGeneric)
    return err.Generic.addr_description.Address();
  else if (err.kind == kErrorKindDoubleFree)
    return err.DoubleFree.addr_description.addr;
  return 0;
}

int __asan_get_report_access_type() {
  if (ScopedInErrorReport::CurrentError().kind == kErrorKindGeneric)
    return ScopedInErrorReport::CurrentError().Generic.is_write;
  return 0;
}

uptr __asan_get_report_access_size() {
  if (ScopedInErrorReport::CurrentError().kind == kErrorKindGeneric)
    return ScopedInErrorReport::CurrentError().Generic.access_size;
  return 0;
}

const char *__asan_get_report_description() {
  if (ScopedInErrorReport::CurrentError().kind == kErrorKindGeneric)
    return ScopedInErrorReport::CurrentError().Generic.bug_descr;
  return ScopedInErrorReport::CurrentError().Base.scariness.GetDescription();
}

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_ptr_sub(void *a, void *b) {
  CheckForInvalidPointerPair(a, b);
}
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_ptr_cmp(void *a, void *b) {
  CheckForInvalidPointerPair(a, b);
}
} // extern "C"

// Provide default implementation of __asan_on_error that does nothing
// and may be overriden by user.
SANITIZER_INTERFACE_WEAK_DEF(void, __asan_on_error, void) {}
