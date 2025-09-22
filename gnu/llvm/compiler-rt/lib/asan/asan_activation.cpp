//===-- asan_activation.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan activation/deactivation logic.
//===----------------------------------------------------------------------===//

#include "asan_activation.h"
#include "asan_allocator.h"
#include "asan_flags.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"

namespace __asan {

static struct AsanDeactivatedFlags {
  AllocatorOptions allocator_options;
  int malloc_context_size;
  bool poison_heap;
  bool coverage;
  const char *coverage_dir;

  void RegisterActivationFlags(FlagParser *parser, Flags *f, CommonFlags *cf) {
#define ASAN_ACTIVATION_FLAG(Type, Name) \
  RegisterFlag(parser, #Name, "", &f->Name);
#define COMMON_ACTIVATION_FLAG(Type, Name) \
  RegisterFlag(parser, #Name, "", &cf->Name);
#include "asan_activation_flags.inc"
#undef ASAN_ACTIVATION_FLAG
#undef COMMON_ACTIVATION_FLAG

    RegisterIncludeFlags(parser, cf);
  }

  void OverrideFromActivationFlags() {
    Flags f;
    CommonFlags cf;
    FlagParser parser;
    RegisterActivationFlags(&parser, &f, &cf);

    cf.SetDefaults();
    // Copy the current activation flags.
    allocator_options.CopyTo(&f, &cf);
    cf.malloc_context_size = malloc_context_size;
    f.poison_heap = poison_heap;
    cf.coverage = coverage;
    cf.coverage_dir = coverage_dir;
    cf.verbosity = Verbosity();
    cf.help = false; // this is activation-specific help

    // Check if activation flags need to be overriden.
    if (const char *env = GetEnv("ASAN_ACTIVATION_OPTIONS")) {
      parser.ParseString(env);
    }

    InitializeCommonFlags(&cf);

    if (Verbosity()) ReportUnrecognizedFlags();

    if (cf.help) parser.PrintFlagDescriptions();

    allocator_options.SetFrom(&f, &cf);
    malloc_context_size = cf.malloc_context_size;
    poison_heap = f.poison_heap;
    coverage = cf.coverage;
    coverage_dir = cf.coverage_dir;
  }

  void Print() {
    Report(
        "quarantine_size_mb %d, thread_local_quarantine_size_kb %d, "
        "max_redzone %d, poison_heap %d, malloc_context_size %d, "
        "alloc_dealloc_mismatch %d, allocator_may_return_null %d, coverage %d, "
        "coverage_dir %s, allocator_release_to_os_interval_ms %d\n",
        allocator_options.quarantine_size_mb,
        allocator_options.thread_local_quarantine_size_kb,
        allocator_options.max_redzone, poison_heap, malloc_context_size,
        allocator_options.alloc_dealloc_mismatch,
        allocator_options.may_return_null, coverage, coverage_dir,
        allocator_options.release_to_os_interval_ms);
  }
} asan_deactivated_flags;

static bool asan_is_deactivated;

void AsanDeactivate() {
  CHECK(!asan_is_deactivated);
  VReport(1, "Deactivating ASan\n");

  // Stash runtime state.
  GetAllocatorOptions(&asan_deactivated_flags.allocator_options);
  asan_deactivated_flags.malloc_context_size = GetMallocContextSize();
  asan_deactivated_flags.poison_heap = CanPoisonMemory();
  asan_deactivated_flags.coverage = common_flags()->coverage;
  asan_deactivated_flags.coverage_dir = common_flags()->coverage_dir;

  // Deactivate the runtime.
  SetCanPoisonMemory(false);
  SetMallocContextSize(1);

  AllocatorOptions disabled = asan_deactivated_flags.allocator_options;
  disabled.quarantine_size_mb = 0;
  disabled.thread_local_quarantine_size_kb = 0;
  // Redzone must be at least Max(16, granularity) bytes long.
  disabled.min_redzone = Max(16, (int)ASAN_SHADOW_GRANULARITY);
  disabled.max_redzone = disabled.min_redzone;
  disabled.alloc_dealloc_mismatch = false;
  disabled.may_return_null = true;
  ReInitializeAllocator(disabled);

  asan_is_deactivated = true;
}

void AsanActivate() {
  if (!asan_is_deactivated) return;
  VReport(1, "Activating ASan\n");

  UpdateProcessName();

  asan_deactivated_flags.OverrideFromActivationFlags();

  SetCanPoisonMemory(asan_deactivated_flags.poison_heap);
  SetMallocContextSize(asan_deactivated_flags.malloc_context_size);
  ReInitializeAllocator(asan_deactivated_flags.allocator_options);

  asan_is_deactivated = false;
  if (Verbosity()) {
    Report("Activated with flags:\n");
    asan_deactivated_flags.Print();
  }
}

}  // namespace __asan
