//===-- ubsan_init.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Initialization of UBSan runtime.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if CAN_SANITIZE_UB
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_interface_internal.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "ubsan_diag.h"
#include "ubsan_flags.h"
#include "ubsan_init.h"

using namespace __ubsan;

const char *__ubsan::GetSanititizerToolName() {
  return "UndefinedBehaviorSanitizer";
}

static bool ubsan_initialized;
static StaticSpinMutex ubsan_init_mu;

static void CommonInit() {
  InitializeSuppressions();
}

static void UbsanDie() {
  if (common_flags()->print_module_map >= 1)
    DumpProcessMap();
}

static void CommonStandaloneInit() {
  SanitizerToolName = GetSanititizerToolName();
  CacheBinaryName();
  InitializeFlags();
  __sanitizer::InitializePlatformEarly();
  __sanitizer_set_report_path(common_flags()->log_path);
  AndroidLogInit();
  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);
  CommonInit();

  // Only add die callback when running in standalone mode to avoid printing
  // the same information from multiple sanitizers' output
  AddDieCallback(UbsanDie);
  Symbolizer::LateInitialize();
}

void __ubsan::InitAsStandalone() {
  SpinMutexLock l(&ubsan_init_mu);
  if (!ubsan_initialized) {
    CommonStandaloneInit();
    ubsan_initialized = true;
  }
}

void __ubsan::InitAsStandaloneIfNecessary() { return InitAsStandalone(); }

void __ubsan::InitAsPlugin() {
  SpinMutexLock l(&ubsan_init_mu);
  if (!ubsan_initialized) {
    CommonInit();
    ubsan_initialized = true;
  }
}

#endif  // CAN_SANITIZE_UB
