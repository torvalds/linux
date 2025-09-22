//===-- nsan_suppressions.cc ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "nsan_suppressions.h"
#include "nsan_flags.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

using namespace __sanitizer;
using namespace __nsan;

SANITIZER_INTERFACE_WEAK_DEF(const char *, __nsan_default_suppressions, void) {
  return 0;
}

const char kSuppressionFcmp[] = "fcmp";
const char kSuppressionConsistency[] = "consistency";

alignas(64) static char suppression_placeholder[sizeof(SuppressionContext)];
static SuppressionContext *suppression_ctx;

// The order should match the enum CheckKind.
static const char *kSuppressionTypes[] = {kSuppressionFcmp,
                                          kSuppressionConsistency};

void __nsan::InitializeSuppressions() {
  CHECK_EQ(nullptr, suppression_ctx);
  suppression_ctx = new (suppression_placeholder)
      SuppressionContext(kSuppressionTypes, ARRAY_SIZE(kSuppressionTypes));
  suppression_ctx->ParseFromFile(flags().suppressions);
  suppression_ctx->Parse(__nsan_default_suppressions());
}

static Suppression *GetSuppressionForAddr(uptr addr, const char *suppr_type) {
  Suppression *s = nullptr;

  // Suppress by module name.
  SuppressionContext *suppressions = suppression_ctx;
  if (const char *moduleName =
          Symbolizer::GetOrInit()->GetModuleNameForPc(addr)) {
    if (suppressions->Match(moduleName, suppr_type, &s))
      return s;
  }

  // Suppress by file or function name.
  SymbolizedStack *frames = Symbolizer::GetOrInit()->SymbolizePC(addr);
  for (SymbolizedStack *cur = frames; cur; cur = cur->next) {
    if (suppressions->Match(cur->info.function, suppr_type, &s) ||
        suppressions->Match(cur->info.file, suppr_type, &s)) {
      break;
    }
  }
  frames->ClearAll();
  return s;
}

Suppression *__nsan::GetSuppressionForStack(const StackTrace *stack,
                                            CheckKind k) {
  for (uptr i = 0, e = stack->size; i < e; i++) {
    Suppression *s = GetSuppressionForAddr(
        StackTrace::GetPreviousInstructionPc(stack->trace[i]),
        kSuppressionTypes[static_cast<int>(k)]);
    if (s)
      return s;
  }
  return nullptr;
}
