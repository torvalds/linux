//===-- memprof_rtl.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Main file of the MemProf run-time library.
//===----------------------------------------------------------------------===//

#include "memprof_allocator.h"
#include "memprof_interceptors.h"
#include "memprof_interface_internal.h"
#include "memprof_internal.h"
#include "memprof_mapping.h"
#include "memprof_stack.h"
#include "memprof_stats.h"
#include "memprof_thread.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_interface_internal.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

#include <time.h>

uptr __memprof_shadow_memory_dynamic_address; // Global interface symbol.

// Allow the user to specify a profile output file via the binary.
SANITIZER_WEAK_ATTRIBUTE char __memprof_profile_filename[1];

// Share ClHistogram compiler flag with runtime.
SANITIZER_WEAK_ATTRIBUTE bool __memprof_histogram;

namespace __memprof {

static void MemprofDie() {
  static atomic_uint32_t num_calls;
  if (atomic_fetch_add(&num_calls, 1, memory_order_relaxed) != 0) {
    // Don't die twice - run a busy loop.
    while (1) {
      internal_sched_yield();
    }
  }
  if (common_flags()->print_module_map >= 1)
    DumpProcessMap();
  if (flags()->unmap_shadow_on_exit) {
    if (kHighShadowEnd)
      UnmapOrDie((void *)kLowShadowBeg, kHighShadowEnd - kLowShadowBeg);
  }
}

static void MemprofOnDeadlySignal(int signo, void *siginfo, void *context) {
  // We call StartReportDeadlySignal not HandleDeadlySignal so we get the
  // deadly signal message to stderr but no writing to the profile output file
  StartReportDeadlySignal();
  __memprof_profile_dump();
  Die();
}

static void CheckUnwind() {
  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_check);
  stack.Print();
}

// -------------------------- Globals --------------------- {{{1
int memprof_inited;
bool memprof_init_is_running;
int memprof_timestamp_inited;
long memprof_init_timestamp_s;

uptr kHighMemEnd;

// -------------------------- Run-time entry ------------------- {{{1
// exported functions

#define MEMPROF_MEMORY_ACCESS_CALLBACK_BODY() __memprof::RecordAccess(addr);
#define MEMPROF_MEMORY_ACCESS_CALLBACK_BODY_HIST()                             \
  __memprof::RecordAccessHistogram(addr);

#define MEMPROF_MEMORY_ACCESS_CALLBACK(type)                                   \
  extern "C" NOINLINE INTERFACE_ATTRIBUTE void __memprof_##type(uptr addr) {   \
    MEMPROF_MEMORY_ACCESS_CALLBACK_BODY()                                      \
  }

#define MEMPROF_MEMORY_ACCESS_CALLBACK_HIST(type)                              \
  extern "C" NOINLINE INTERFACE_ATTRIBUTE void __memprof_hist_##type(          \
      uptr addr) {                                                             \
    MEMPROF_MEMORY_ACCESS_CALLBACK_BODY_HIST()                                 \
  }

MEMPROF_MEMORY_ACCESS_CALLBACK_HIST(load)
MEMPROF_MEMORY_ACCESS_CALLBACK_HIST(store)

MEMPROF_MEMORY_ACCESS_CALLBACK(load)
MEMPROF_MEMORY_ACCESS_CALLBACK(store)

// Force the linker to keep the symbols for various MemProf interface
// functions. We want to keep those in the executable in order to let the
// instrumented dynamic libraries access the symbol even if it is not used by
// the executable itself. This should help if the build system is removing dead
// code at link time.
static NOINLINE void force_interface_symbols() {
  volatile int fake_condition = 0; // prevent dead condition elimination.
  // clang-format off
  switch (fake_condition) {
    case 1: __memprof_record_access(nullptr); break;
    case 2: __memprof_record_access_range(nullptr, 0); break;
  }
  // clang-format on
}

static void memprof_atexit() {
  Printf("MemProfiler exit stats:\n");
  __memprof_print_accumulated_stats();
}

static void InitializeHighMemEnd() {
  kHighMemEnd = GetMaxUserVirtualAddress();
  // Increase kHighMemEnd to make sure it's properly
  // aligned together with kHighMemBeg:
  kHighMemEnd |= (GetMmapGranularity() << SHADOW_SCALE) - 1;
}

void PrintAddressSpaceLayout() {
  if (kHighMemBeg) {
    Printf("|| `[%p, %p]` || HighMem    ||\n", (void *)kHighMemBeg,
           (void *)kHighMemEnd);
    Printf("|| `[%p, %p]` || HighShadow ||\n", (void *)kHighShadowBeg,
           (void *)kHighShadowEnd);
  }
  Printf("|| `[%p, %p]` || ShadowGap  ||\n", (void *)kShadowGapBeg,
         (void *)kShadowGapEnd);
  if (kLowShadowBeg) {
    Printf("|| `[%p, %p]` || LowShadow  ||\n", (void *)kLowShadowBeg,
           (void *)kLowShadowEnd);
    Printf("|| `[%p, %p]` || LowMem     ||\n", (void *)kLowMemBeg,
           (void *)kLowMemEnd);
  }
  Printf("MemToShadow(shadow): %p %p", (void *)MEM_TO_SHADOW(kLowShadowBeg),
         (void *)MEM_TO_SHADOW(kLowShadowEnd));
  if (kHighMemBeg) {
    Printf(" %p %p", (void *)MEM_TO_SHADOW(kHighShadowBeg),
           (void *)MEM_TO_SHADOW(kHighShadowEnd));
  }
  Printf("\n");
  Printf("malloc_context_size=%zu\n",
         (uptr)common_flags()->malloc_context_size);

  Printf("SHADOW_SCALE: %d\n", (int)SHADOW_SCALE);
  Printf("SHADOW_GRANULARITY: %d\n", (int)SHADOW_GRANULARITY);
  Printf("SHADOW_OFFSET: %p\n", (void *)SHADOW_OFFSET);
  CHECK(SHADOW_SCALE >= 3 && SHADOW_SCALE <= 7);
}

static void MemprofInitInternal() {
  if (LIKELY(memprof_inited))
    return;
  SanitizerToolName = "MemProfiler";
  CHECK(!memprof_init_is_running && "MemProf init calls itself!");
  memprof_init_is_running = true;

  CacheBinaryName();

  // Initialize flags. This must be done early, because most of the
  // initialization steps look at flags().
  InitializeFlags();

  AvoidCVE_2016_2143();

  SetMallocContextSize(common_flags()->malloc_context_size);

  InitializeHighMemEnd();

  // Make sure we are not statically linked.
  __interception::DoesNotSupportStaticLinking();

  // Install tool-specific callbacks in sanitizer_common.
  AddDieCallback(MemprofDie);
  SetCheckUnwindCallback(CheckUnwind);

  // Use profile name specified via the binary itself if it exists, and hasn't
  // been overrriden by a flag at runtime.
  if (__memprof_profile_filename[0] != 0 && !common_flags()->log_path)
    __sanitizer_set_report_path(__memprof_profile_filename);
  else
    __sanitizer_set_report_path(common_flags()->log_path);

  __sanitizer::InitializePlatformEarly();

  // Setup internal allocator callback.
  SetLowLevelAllocateMinAlignment(SHADOW_GRANULARITY);

  InitializeMemprofInterceptors();
  CheckASLR();

  ReplaceSystemMalloc();

  DisableCoreDumperIfNecessary();

  InitializeShadowMemory();

  TSDInit(PlatformTSDDtor);
  InstallDeadlySignalHandlers(MemprofOnDeadlySignal);

  InitializeAllocator();

  if (flags()->atexit)
    Atexit(memprof_atexit);

  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);

  // interceptors
  InitTlsSize();

  // Create main thread.
  MemprofThread *main_thread = CreateMainThread();
  CHECK_EQ(0, main_thread->tid());
  force_interface_symbols(); // no-op.
  SanitizerInitializeUnwinder();

  Symbolizer::LateInitialize();

  VReport(1, "MemProfiler Init done\n");

  memprof_init_is_running = false;
  memprof_inited = 1;
}

void MemprofInitTime() {
  if (LIKELY(memprof_timestamp_inited))
    return;
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  memprof_init_timestamp_s = ts.tv_sec;
  memprof_timestamp_inited = 1;
}

// Initialize as requested from some part of MemProf runtime library
// (interceptors, allocator, etc).
void MemprofInitFromRtl() { MemprofInitInternal(); }

#if MEMPROF_DYNAMIC
// Initialize runtime in case it's LD_PRELOAD-ed into uninstrumented executable
// (and thus normal initializers from .preinit_array or modules haven't run).

class MemprofInitializer {
public:
  MemprofInitializer() { MemprofInitFromRtl(); }
};

static MemprofInitializer memprof_initializer;
#endif // MEMPROF_DYNAMIC

} // namespace __memprof

// ---------------------- Interface ---------------- {{{1
using namespace __memprof;

// Initialize as requested from instrumented application code.
void __memprof_init() {
  MemprofInitTime();
  MemprofInitInternal();
}

void __memprof_preinit() { MemprofInitInternal(); }

void __memprof_version_mismatch_check_v1() {}

void __memprof_record_access(void const volatile *addr) {
  __memprof::RecordAccess((uptr)addr);
}

void __memprof_record_access_hist(void const volatile *addr) {
  __memprof::RecordAccessHistogram((uptr)addr);
}

void __memprof_record_access_range(void const volatile *addr, uptr size) {
  for (uptr a = (uptr)addr; a < (uptr)addr + size; a += kWordSize)
    __memprof::RecordAccess(a);
}

void __memprof_record_access_range_hist(void const volatile *addr, uptr size) {
  for (uptr a = (uptr)addr; a < (uptr)addr + size; a += kWordSize)
    __memprof::RecordAccessHistogram(a);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE u16
__sanitizer_unaligned_load16(const uu16 *p) {
  __memprof_record_access(p);
  return *p;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE u32
__sanitizer_unaligned_load32(const uu32 *p) {
  __memprof_record_access(p);
  return *p;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE u64
__sanitizer_unaligned_load64(const uu64 *p) {
  __memprof_record_access(p);
  return *p;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__sanitizer_unaligned_store16(uu16 *p, u16 x) {
  __memprof_record_access(p);
  *p = x;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__sanitizer_unaligned_store32(uu32 *p, u32 x) {
  __memprof_record_access(p);
  *p = x;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__sanitizer_unaligned_store64(uu64 *p, u64 x) {
  __memprof_record_access(p);
  *p = x;
}
