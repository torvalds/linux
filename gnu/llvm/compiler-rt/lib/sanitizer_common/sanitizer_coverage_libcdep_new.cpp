//===-- sanitizer_coverage_libcdep_new.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Sanitizer Coverage Controller for Trace PC Guard.

#include "sanitizer_platform.h"

#if !SANITIZER_FUCHSIA
#  include "sancov_flags.h"
#  include "sanitizer_allocator_internal.h"
#  include "sanitizer_atomic.h"
#  include "sanitizer_common.h"
#  include "sanitizer_common/sanitizer_stacktrace.h"
#  include "sanitizer_file.h"
#  include "sanitizer_interface_internal.h"

using namespace __sanitizer;

using AddressRange = LoadedModule::AddressRange;

namespace __sancov {
namespace {

static const u64 Magic64 = 0xC0BFFFFFFFFFFF64ULL;
static const u64 Magic32 = 0xC0BFFFFFFFFFFF32ULL;
static const u64 Magic = SANITIZER_WORDSIZE == 64 ? Magic64 : Magic32;

static fd_t OpenFile(const char* path) {
  error_t err;
  fd_t fd = OpenFile(path, WrOnly, &err);
  if (fd == kInvalidFd)
    Report("SanitizerCoverage: failed to open %s for writing (reason: %d)\n",
           path, err);
  return fd;
}

static void GetCoverageFilename(char* path, const char* name,
                                const char* extension) {
  CHECK(name);
  internal_snprintf(path, kMaxPathLength, "%s/%s.%zd.%s",
                    common_flags()->coverage_dir, name, internal_getpid(),
                    extension);
}

static void WriteModuleCoverage(char* file_path, const char* module_name,
                                const uptr* pcs, uptr len) {
  GetCoverageFilename(file_path, StripModuleName(module_name), "sancov");
  fd_t fd = OpenFile(file_path);
  WriteToFile(fd, &Magic, sizeof(Magic));
  WriteToFile(fd, pcs, len * sizeof(*pcs));
  CloseFile(fd);
  Printf("SanitizerCoverage: %s: %zd PCs written\n", file_path, len);
}

static void SanitizerDumpCoverage(const uptr* unsorted_pcs, uptr len) {
  if (!len) return;

  char* file_path = static_cast<char*>(InternalAlloc(kMaxPathLength));
  char* module_name = static_cast<char*>(InternalAlloc(kMaxPathLength));
  uptr* pcs = static_cast<uptr*>(InternalAlloc(len * sizeof(uptr)));

  internal_memcpy(pcs, unsorted_pcs, len * sizeof(uptr));
  Sort(pcs, len);

  bool module_found = false;
  uptr last_base = 0;
  uptr module_start_idx = 0;

  for (uptr i = 0; i < len; ++i) {
    const uptr pc = pcs[i];
    if (!pc) continue;

    if (!GetModuleAndOffsetForPc(pc, nullptr, 0, &pcs[i])) {
      Printf("ERROR: unknown pc %p (may happen if dlclose is used)\n",
             (void*)pc);
      continue;
    }
    uptr module_base = pc - pcs[i];

    if (module_base != last_base || !module_found) {
      if (module_found) {
        WriteModuleCoverage(file_path, module_name, &pcs[module_start_idx],
                            i - module_start_idx);
      }

      last_base = module_base;
      module_start_idx = i;
      module_found = true;
      GetModuleAndOffsetForPc(pc, module_name, kMaxPathLength, &pcs[i]);
    }
  }

  if (module_found) {
    WriteModuleCoverage(file_path, module_name, &pcs[module_start_idx],
                        len - module_start_idx);
  }

  InternalFree(file_path);
  InternalFree(module_name);
  InternalFree(pcs);
}

// Collects trace-pc guard coverage.
// This class relies on zero-initialization.
class TracePcGuardController {
 public:
  void Initialize() {
    CHECK(!initialized);

    initialized = true;
    InitializeSancovFlags();

    pc_vector.Initialize(0);
  }

  void InitTracePcGuard(u32* start, u32* end) {
    if (!initialized) Initialize();
    CHECK(!*start);
    CHECK_NE(start, end);

    u32 i = pc_vector.size();
    for (u32* p = start; p < end; p++) *p = ++i;
    pc_vector.resize(i);
  }

  void TracePcGuard(u32* guard, uptr pc) {
    u32 idx = *guard;
    if (!idx) return;
    // we start indices from 1.
    atomic_uintptr_t* pc_ptr =
        reinterpret_cast<atomic_uintptr_t*>(&pc_vector[idx - 1]);
    if (atomic_load(pc_ptr, memory_order_relaxed) == 0)
      atomic_store(pc_ptr, pc, memory_order_relaxed);
  }

  void Reset() {
    internal_memset(&pc_vector[0], 0, sizeof(pc_vector[0]) * pc_vector.size());
  }

  void Dump() {
    if (!initialized || !common_flags()->coverage) return;
    __sanitizer_dump_coverage(pc_vector.data(), pc_vector.size());
  }

 private:
  bool initialized;
  InternalMmapVectorNoCtor<uptr> pc_vector;
};

static TracePcGuardController pc_guard_controller;

// A basic default implementation of callbacks for
// -fsanitize-coverage=inline-8bit-counters,pc-table.
// Use TOOL_OPTIONS (UBSAN_OPTIONS, etc) to dump the coverage data:
// * cov_8bit_counters_out=PATH to dump the 8bit counters.
// * cov_pcs_out=PATH to dump the pc table.
//
// Most users will still need to define their own callbacks for greater
// flexibility.
namespace SingletonCounterCoverage {

static char *counters_beg, *counters_end;
static const uptr *pcs_beg, *pcs_end;

static void DumpCoverage() {
  const char* file_path = common_flags()->cov_8bit_counters_out;
  if (file_path && internal_strlen(file_path)) {
    fd_t fd = OpenFile(file_path);
    FileCloser file_closer(fd);
    uptr size = counters_end - counters_beg;
    WriteToFile(fd, counters_beg, size);
    if (common_flags()->verbosity)
      __sanitizer::Printf("cov_8bit_counters_out: written %zd bytes to %s\n",
                          size, file_path);
  }
  file_path = common_flags()->cov_pcs_out;
  if (file_path && internal_strlen(file_path)) {
    fd_t fd = OpenFile(file_path);
    FileCloser file_closer(fd);
    uptr size = (pcs_end - pcs_beg) * sizeof(uptr);
    WriteToFile(fd, pcs_beg, size);
    if (common_flags()->verbosity)
      __sanitizer::Printf("cov_pcs_out: written %zd bytes to %s\n", size,
                          file_path);
  }
}

static void Cov8bitCountersInit(char* beg, char* end) {
  counters_beg = beg;
  counters_end = end;
  Atexit(DumpCoverage);
}

static void CovPcsInit(const uptr* beg, const uptr* end) {
  pcs_beg = beg;
  pcs_end = end;
}

}  // namespace SingletonCounterCoverage

}  // namespace
}  // namespace __sancov

namespace __sanitizer {
void InitializeCoverage(bool enabled, const char *dir) {
  static bool coverage_enabled = false;
  if (coverage_enabled)
    return;  // May happen if two sanitizer enable coverage in the same process.
  coverage_enabled = enabled;
  Atexit(__sanitizer_cov_dump);
  AddDieCallback(__sanitizer_cov_dump);
}
} // namespace __sanitizer

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_dump_coverage(const uptr* pcs,
                                                             uptr len) {
  return __sancov::SanitizerDumpCoverage(pcs, len);
}

SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_guard, u32* guard) {
  if (!*guard) return;
  __sancov::pc_guard_controller.TracePcGuard(
      guard, StackTrace::GetPreviousInstructionPc(GET_CALLER_PC()));
}

SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_guard_init,
                             u32* start, u32* end) {
  if (start == end || *start) return;
  __sancov::pc_guard_controller.InitTracePcGuard(start, end);
}

SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_dump_trace_pc_guard_coverage() {
  __sancov::pc_guard_controller.Dump();
}
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_cov_dump() {
  __sanitizer_dump_trace_pc_guard_coverage();
}
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_cov_reset() {
  __sancov::pc_guard_controller.Reset();
}
// Default implementations (weak).
// Either empty or very simple.
// Most users should redefine them.
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp1, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp2, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp1, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp2, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_switch, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_div4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_div8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_gep, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_indir, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_load1, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_load2, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_load4, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_load8, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_load16, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_store1, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_store2, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_store4, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_store8, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_store16, void){}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_8bit_counters_init,
                             char* start, char* end) {
  __sancov::SingletonCounterCoverage::Cov8bitCountersInit(start, end);
}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_bool_flag_init, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_pcs_init, const uptr* beg,
                             const uptr* end) {
  __sancov::SingletonCounterCoverage::CovPcsInit(beg, end);
}
}  // extern "C"
// Weak definition for code instrumented with -fsanitize-coverage=stack-depth
// and later linked with code containing a strong definition.
// E.g., -fsanitize=fuzzer-no-link
// FIXME: Update Apple deployment target so that thread_local is always
// supported, and remove the #if.
// FIXME: Figure out how this should work on Windows, exported thread_local
// symbols are not supported:
// "data with thread storage duration may not have dll interface"
#if !SANITIZER_APPLE && !SANITIZER_WINDOWS
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
thread_local uptr __sancov_lowest_stack;
#endif

#endif  // !SANITIZER_FUCHSIA
