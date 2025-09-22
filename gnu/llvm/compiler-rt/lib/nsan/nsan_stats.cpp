//===-- nsan_stats.cc -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of NumericalStabilitySanitizer.
//
// NumericalStabilitySanitizer statistics.
//===----------------------------------------------------------------------===//

#include "nsan/nsan_stats.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

#include <assert.h>
#include <stdio.h>

using namespace __sanitizer;
using namespace __nsan;

Stats::Stats() {
  check_and_warnings.Initialize(0);
  TrackedLoads.Initialize(0);
}

Stats::~Stats() { Printf("deleting nsan stats\n"); }

static uptr Key(CheckTypeT CheckType, u32 StackId) {
  return static_cast<uptr>(CheckType) +
         StackId * static_cast<uptr>(CheckTypeT::kMaxCheckType);
}

template <typename MapT, typename VectorT, typename Fn>
static void UpdateEntry(CheckTypeT check_ty, uptr pc, uptr bp, MapT *map,
                        VectorT *vector, Mutex *mutex, Fn F) {
  BufferedStackTrace Stack;
  Stack.Unwind(pc, bp, nullptr, false);
  u32 stack_id = StackDepotPut(Stack);
  typename MapT::Handle Handle(map, Key(check_ty, stack_id));
  Lock L(mutex);
  if (Handle.created()) {
    typename VectorT::value_type entry;
    entry.stack_id = stack_id;
    entry.check_ty = check_ty;
    F(entry);
    vector->push_back(entry);
  } else {
    auto &entry = (*vector)[*Handle];
    F(entry);
  }
}

void Stats::AddCheck(CheckTypeT check_ty, uptr pc, uptr bp, double rel_err) {
  UpdateEntry(check_ty, pc, bp, &CheckAndWarningsMap, &check_and_warnings,
              &check_and_warning_mutex,
              [rel_err](CheckAndWarningsValue &entry) {
                ++entry.num_checks;
                if (rel_err > entry.max_relative_err) {
                  entry.max_relative_err = rel_err;
                }
              });
}

void Stats::AddWarning(CheckTypeT check_ty, uptr pc, uptr bp, double rel_err) {
  UpdateEntry(check_ty, pc, bp, &CheckAndWarningsMap, &check_and_warnings,
              &check_and_warning_mutex,
              [rel_err](CheckAndWarningsValue &entry) {
                ++entry.num_warnings;
                if (rel_err > entry.max_relative_err) {
                  entry.max_relative_err = rel_err;
                }
              });
}

void Stats::AddInvalidLoadTrackingEvent(uptr pc, uptr bp) {
  UpdateEntry(CheckTypeT::kLoad, pc, bp, &LoadTrackingMap, &TrackedLoads,
              &TrackedLoadsMutex,
              [](LoadTrackingValue &entry) { ++entry.num_invalid; });
}

void Stats::AddUnknownLoadTrackingEvent(uptr pc, uptr bp) {
  UpdateEntry(CheckTypeT::kLoad, pc, bp, &LoadTrackingMap, &TrackedLoads,
              &TrackedLoadsMutex,
              [](LoadTrackingValue &entry) { ++entry.num_unknown; });
}

static const char *CheckTypeDisplay(CheckTypeT CheckType) {
  switch (CheckType) {
  case CheckTypeT::kUnknown:
    return "unknown";
  case CheckTypeT::kRet:
    return "return";
  case CheckTypeT::kArg:
    return "argument";
  case CheckTypeT::kLoad:
    return "load";
  case CheckTypeT::kStore:
    return "store";
  case CheckTypeT::kInsert:
    return "vector insert";
  case CheckTypeT::kUser:
    return "user-initiated";
  case CheckTypeT::kFcmp:
    return "fcmp";
  case CheckTypeT::kMaxCheckType:
    return "[max]";
  }
  assert(false && "unknown CheckType case");
  return "";
}

void Stats::Print() const {
  {
    Lock L(&check_and_warning_mutex);
    for (const auto &entry : check_and_warnings) {
      Printf("warned %llu times out of %llu %s checks ", entry.num_warnings,
             entry.num_checks, CheckTypeDisplay(entry.check_ty));
      if (entry.num_warnings > 0) {
        char RelErrBuf[64];
        snprintf(RelErrBuf, sizeof(RelErrBuf) - 1, "%f",
                 entry.max_relative_err * 100.0);
        Printf("(max relative error: %s%%) ", RelErrBuf);
      }
      Printf("at:\n");
      StackDepotGet(entry.stack_id).Print();
    }
  }

  {
    Lock L(&TrackedLoadsMutex);
    u64 TotalInvalidLoadTracking = 0;
    u64 TotalUnknownLoadTracking = 0;
    for (const auto &entry : TrackedLoads) {
      TotalInvalidLoadTracking += entry.num_invalid;
      TotalUnknownLoadTracking += entry.num_unknown;
      Printf("invalid/unknown type for %llu/%llu loads at:\n",
             entry.num_invalid, entry.num_unknown);
      StackDepotGet(entry.stack_id).Print();
    }
    Printf(
        "There were %llu/%llu floating-point loads where the shadow type was "
        "invalid/unknown.\n",
        TotalInvalidLoadTracking, TotalUnknownLoadTracking);
  }
}

alignas(64) static char stats_placeholder[sizeof(Stats)];
Stats *__nsan::nsan_stats = nullptr;

void __nsan::InitializeStats() { nsan_stats = new (stats_placeholder) Stats(); }
