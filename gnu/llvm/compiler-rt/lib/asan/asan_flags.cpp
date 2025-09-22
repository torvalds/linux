//===-- asan_flags.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan flag parsing logic.
//===----------------------------------------------------------------------===//

#include "asan_activation.h"
#include "asan_flags.h"
#include "asan_interface_internal.h"
#include "asan_stack.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "ubsan/ubsan_flags.h"
#include "ubsan/ubsan_platform.h"

namespace __asan {

Flags asan_flags_dont_use_directly;  // use via flags().

static const char *MaybeUseAsanDefaultOptionsCompileDefinition() {
#ifdef ASAN_DEFAULT_OPTIONS
  return SANITIZER_STRINGIFY(ASAN_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

void Flags::SetDefaults() {
#define ASAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "asan_flags.inc"
#undef ASAN_FLAG
}

static void RegisterAsanFlags(FlagParser *parser, Flags *f) {
#define ASAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "asan_flags.inc"
#undef ASAN_FLAG
}

void InitializeFlags() {
  // Set the default values and prepare for parsing ASan and common flags.
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.detect_leaks = cf.detect_leaks && CAN_SANITIZE_LEAKS;
    cf.external_symbolizer_path = GetEnv("ASAN_SYMBOLIZER_PATH");
    cf.malloc_context_size = kDefaultMallocContextSize;
    cf.intercept_tls_get_addr = true;
    cf.exitcode = 1;
    OverrideCommonFlags(cf);
  }
  Flags *f = flags();
  f->SetDefaults();

  FlagParser asan_parser;
  RegisterAsanFlags(&asan_parser, f);
  RegisterCommonFlags(&asan_parser);

  // Set the default values and prepare for parsing LSan and UBSan flags
  // (which can also overwrite common flags).
#if CAN_SANITIZE_LEAKS
  __lsan::Flags *lf = __lsan::flags();
  lf->SetDefaults();

  FlagParser lsan_parser;
  __lsan::RegisterLsanFlags(&lsan_parser, lf);
  RegisterCommonFlags(&lsan_parser);
#endif

#if CAN_SANITIZE_UB
  __ubsan::Flags *uf = __ubsan::flags();
  uf->SetDefaults();

  FlagParser ubsan_parser;
  __ubsan::RegisterUbsanFlags(&ubsan_parser, uf);
  RegisterCommonFlags(&ubsan_parser);
#endif

  if (SANITIZER_APPLE) {
    // Support macOS MallocScribble and MallocPreScribble:
    // <https://developer.apple.com/library/content/documentation/Performance/
    // Conceptual/ManagingMemory/Articles/MallocDebug.html>
    if (GetEnv("MallocScribble")) {
      f->max_free_fill_size = 0x1000;
    }
    if (GetEnv("MallocPreScribble")) {
      f->malloc_fill_byte = 0xaa;
    }
  }

  // Override from ASan compile definition.
  const char *asan_compile_def = MaybeUseAsanDefaultOptionsCompileDefinition();
  asan_parser.ParseString(asan_compile_def);

  // Override from user-specified string.
  const char *asan_default_options = __asan_default_options();
  asan_parser.ParseString(asan_default_options);
#if CAN_SANITIZE_UB
  const char *ubsan_default_options = __ubsan_default_options();
  ubsan_parser.ParseString(ubsan_default_options);
#endif
#if CAN_SANITIZE_LEAKS
  const char *lsan_default_options = __lsan_default_options();
  lsan_parser.ParseString(lsan_default_options);
#endif

  // Override from command line.
  asan_parser.ParseStringFromEnv("ASAN_OPTIONS");
#if CAN_SANITIZE_LEAKS
  lsan_parser.ParseStringFromEnv("LSAN_OPTIONS");
#endif
#if CAN_SANITIZE_UB
  ubsan_parser.ParseStringFromEnv("UBSAN_OPTIONS");
#endif

  InitializeCommonFlags();

  // TODO(eugenis): dump all flags at verbosity>=2?
  if (Verbosity()) ReportUnrecognizedFlags();

  if (common_flags()->help) {
    // TODO(samsonov): print all of the flags (ASan, LSan, common).
    asan_parser.PrintFlagDescriptions();
  }

  // Flag validation:
  if (!CAN_SANITIZE_LEAKS && common_flags()->detect_leaks) {
    Report("%s: detect_leaks is not supported on this platform.\n",
           SanitizerToolName);
    Die();
  }
  // Ensure that redzone is at least ASAN_SHADOW_GRANULARITY.
  if (f->redzone < (int)ASAN_SHADOW_GRANULARITY)
    f->redzone = ASAN_SHADOW_GRANULARITY;
  // Make "strict_init_order" imply "check_initialization_order".
  // TODO(samsonov): Use a single runtime flag for an init-order checker.
  if (f->strict_init_order) {
    f->check_initialization_order = true;
  }
  CHECK_LE((uptr)common_flags()->malloc_context_size, kStackTraceMax);
  CHECK_LE(f->min_uar_stack_size_log, f->max_uar_stack_size_log);
  CHECK_GE(f->redzone, 16);
  CHECK_GE(f->max_redzone, f->redzone);
  CHECK_LE(f->max_redzone, 2048);
  CHECK(IsPowerOfTwo(f->redzone));
  CHECK(IsPowerOfTwo(f->max_redzone));

  // quarantine_size is deprecated but we still honor it.
  // quarantine_size can not be used together with quarantine_size_mb.
  if (f->quarantine_size >= 0 && f->quarantine_size_mb >= 0) {
    Report("%s: please use either 'quarantine_size' (deprecated) or "
           "quarantine_size_mb, but not both\n", SanitizerToolName);
    Die();
  }
  if (f->quarantine_size >= 0)
    f->quarantine_size_mb = f->quarantine_size >> 20;
  if (f->quarantine_size_mb < 0) {
    const int kDefaultQuarantineSizeMb =
        (ASAN_LOW_MEMORY) ? 1UL << 4 : 1UL << 8;
    f->quarantine_size_mb = kDefaultQuarantineSizeMb;
  }
  if (f->thread_local_quarantine_size_kb < 0) {
    const u32 kDefaultThreadLocalQuarantineSizeKb =
        // It is not advised to go lower than 64Kb, otherwise quarantine batches
        // pushed from thread local quarantine to global one will create too
        // much overhead. One quarantine batch size is 8Kb and it  holds up to
        // 1021 chunk, which amounts to 1/8 memory overhead per batch when
        // thread local quarantine is set to 64Kb.
        (ASAN_LOW_MEMORY) ? 1 << 6 : FIRST_32_SECOND_64(1 << 8, 1 << 10);
    f->thread_local_quarantine_size_kb = kDefaultThreadLocalQuarantineSizeKb;
  }
  if (f->thread_local_quarantine_size_kb == 0 && f->quarantine_size_mb > 0) {
    Report("%s: thread_local_quarantine_size_kb can be set to 0 only when "
           "quarantine_size_mb is set to 0\n", SanitizerToolName);
    Die();
  }
  if (!f->replace_str && common_flags()->intercept_strlen) {
    Report("WARNING: strlen interceptor is enabled even though replace_str=0. "
           "Use intercept_strlen=0 to disable it.");
  }
  if (!f->replace_str && common_flags()->intercept_strchr) {
    Report("WARNING: strchr* interceptors are enabled even though "
           "replace_str=0. Use intercept_strchr=0 to disable them.");
  }
  if (!f->replace_str && common_flags()->intercept_strndup) {
    Report("WARNING: strndup* interceptors are enabled even though "
           "replace_str=0. Use intercept_strndup=0 to disable them.");
  }
}

}  // namespace __asan

SANITIZER_INTERFACE_WEAK_DEF(const char*, __asan_default_options, void) {
  return "";
}
