//===-- tsan_suppressions.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

#include "tsan_suppressions.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_suppressions.h"
#include "tsan_flags.h"
#include "tsan_mman.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"

#if !SANITIZER_GO
// Suppressions for true/false positives in standard libraries.
static const char *const std_suppressions =
// Libstdc++ 4.4 has data races in std::string.
// See http://crbug.com/181502 for an example.
"race:^_M_rep$\n"
"race:^_M_is_leaked$\n"
// False positive when using std <thread>.
// Happens because we miss atomic synchronization in libstdc++.
// See http://llvm.org/bugs/show_bug.cgi?id=17066 for details.
"race:std::_Sp_counted_ptr_inplace<std::thread::_Impl\n";

// Can be overriden in frontend.
SANITIZER_WEAK_DEFAULT_IMPL
const char *__tsan_default_suppressions() {
  return 0;
}
#endif

namespace __tsan {

alignas(64) static char suppression_placeholder[sizeof(SuppressionContext)];
static SuppressionContext *suppression_ctx = nullptr;
static const char *kSuppressionTypes[] = {
    kSuppressionRace,   kSuppressionRaceTop, kSuppressionMutex,
    kSuppressionThread, kSuppressionSignal, kSuppressionLib,
    kSuppressionDeadlock};

void InitializeSuppressions() {
  CHECK_EQ(nullptr, suppression_ctx);
  suppression_ctx = new (suppression_placeholder)
      SuppressionContext(kSuppressionTypes, ARRAY_SIZE(kSuppressionTypes));
  suppression_ctx->ParseFromFile(flags()->suppressions);
#if !SANITIZER_GO
  suppression_ctx->Parse(__tsan_default_suppressions());
  suppression_ctx->Parse(std_suppressions);
#endif
}

SuppressionContext *Suppressions() {
  CHECK(suppression_ctx);
  return suppression_ctx;
}

static const char *conv(ReportType typ) {
  switch (typ) {
    case ReportTypeRace:
    case ReportTypeVptrRace:
    case ReportTypeUseAfterFree:
    case ReportTypeVptrUseAfterFree:
    case ReportTypeExternalRace:
      return kSuppressionRace;
    case ReportTypeThreadLeak:
      return kSuppressionThread;
    case ReportTypeMutexDestroyLocked:
    case ReportTypeMutexDoubleLock:
    case ReportTypeMutexInvalidAccess:
    case ReportTypeMutexBadUnlock:
    case ReportTypeMutexBadReadLock:
    case ReportTypeMutexBadReadUnlock:
    case ReportTypeMutexHeldWrongContext:
      return kSuppressionMutex;
    case ReportTypeSignalUnsafe:
    case ReportTypeErrnoInSignal:
      return kSuppressionSignal;
    case ReportTypeDeadlock:
      return kSuppressionDeadlock;
    // No default case so compiler warns us if we miss one
  }
  UNREACHABLE("missing case");
}

static uptr IsSuppressed(const char *stype, const AddressInfo &info,
    Suppression **sp) {
  if (suppression_ctx->Match(info.function, stype, sp) ||
      suppression_ctx->Match(info.file, stype, sp) ||
      suppression_ctx->Match(info.module, stype, sp)) {
    VPrintf(2, "ThreadSanitizer: matched suppression '%s'\n", (*sp)->templ);
    atomic_fetch_add(&(*sp)->hit_count, 1, memory_order_relaxed);
    return info.address;
  }
  return 0;
}

uptr IsSuppressed(ReportType typ, const ReportStack *stack, Suppression **sp) {
  CHECK(suppression_ctx);
  if (!suppression_ctx->SuppressionCount() || stack == 0 ||
      !stack->suppressable)
    return 0;
  const char *stype = conv(typ);
  if (0 == internal_strcmp(stype, kSuppressionNone))
    return 0;
  for (const SymbolizedStack *frame = stack->frames; frame;
      frame = frame->next) {
    uptr pc = IsSuppressed(stype, frame->info, sp);
    if (pc != 0)
      return pc;
  }
  if (0 == internal_strcmp(stype, kSuppressionRace) && stack->frames != nullptr)
    return IsSuppressed(kSuppressionRaceTop, stack->frames->info, sp);
  return 0;
}

uptr IsSuppressed(ReportType typ, const ReportLocation *loc, Suppression **sp) {
  CHECK(suppression_ctx);
  if (!suppression_ctx->SuppressionCount() || loc == 0 ||
      loc->type != ReportLocationGlobal || !loc->suppressable)
    return 0;
  const char *stype = conv(typ);
  if (0 == internal_strcmp(stype, kSuppressionNone))
    return 0;
  Suppression *s;
  const DataInfo &global = loc->global;
  if (suppression_ctx->Match(global.name, stype, &s) ||
      suppression_ctx->Match(global.module, stype, &s)) {
      VPrintf(2, "ThreadSanitizer: matched suppression '%s'\n", s->templ);
      atomic_fetch_add(&s->hit_count, 1, memory_order_relaxed);
      *sp = s;
      return global.start;
  }
  return 0;
}

void PrintMatchedSuppressions() {
  InternalMmapVector<Suppression *> matched;
  CHECK(suppression_ctx);
  suppression_ctx->GetMatched(&matched);
  if (!matched.size())
    return;
  int hit_count = 0;
  for (uptr i = 0; i < matched.size(); i++)
    hit_count += atomic_load_relaxed(&matched[i]->hit_count);
  Printf("ThreadSanitizer: Matched %d suppressions (pid=%d):\n", hit_count,
         (int)internal_getpid());
  for (uptr i = 0; i < matched.size(); i++) {
    Printf("%d %s:%s\n", atomic_load_relaxed(&matched[i]->hit_count),
           matched[i]->type, matched[i]->templ);
  }
}
}  // namespace __tsan
