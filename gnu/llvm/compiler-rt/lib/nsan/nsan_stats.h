//===-- nsan_stats.h --------------------------------------------*- C++- *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of NumericalStabilitySanitizer.
//
// NSan statistics. This class counts the number of checks per code location,
// and is used to output statistics (typically when using
// `disable_warnings=1,enable_check_stats=1,enable_warning_stats=1`).
//===----------------------------------------------------------------------===//

#ifndef NSAN_STATS_H
#define NSAN_STATS_H

#include "sanitizer_common/sanitizer_addrhashmap.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_mutex.h"

namespace __nsan {

enum class CheckTypeT {
  kUnknown = 0,
  kRet,
  kArg,
  kLoad,
  kStore,
  kInsert,
  kUser, // User initiated.
  kFcmp,
  kMaxCheckType,
};

class Stats {
public:
  Stats();
  ~Stats();

  // Signal that we checked the instruction at the given address.
  void AddCheck(CheckTypeT check_ty, __sanitizer::uptr pc, __sanitizer::uptr bp,
                double rel_err);
  // Signal that we warned for the instruction at the given address.
  void AddWarning(CheckTypeT check_ty, __sanitizer::uptr pc,
                  __sanitizer::uptr bp, double rel_err);

  // Signal that we detected a floating-point load where the shadow type was
  // invalid.
  void AddInvalidLoadTrackingEvent(__sanitizer::uptr pc, __sanitizer::uptr bp);
  // Signal that we detected a floating-point load where the shadow type was
  // unknown but the value was nonzero.
  void AddUnknownLoadTrackingEvent(__sanitizer::uptr pc, __sanitizer::uptr bp);

  void Print() const;

private:
  using IndexMap = __sanitizer::AddrHashMap<__sanitizer::uptr, 11>;

  struct CheckAndWarningsValue {
    CheckTypeT check_ty;
    __sanitizer::u32 stack_id = 0;
    __sanitizer::u64 num_checks = 0;
    __sanitizer::u64 num_warnings = 0;
    // This is a bitcasted double. Doubles have the nice idea to be ordered as
    // ints.
    double max_relative_err = 0;
  };
  // Map Key(check_ty, StackId) to indices in CheckAndWarnings.
  IndexMap CheckAndWarningsMap;
  __sanitizer::InternalMmapVectorNoCtor<CheckAndWarningsValue>
      check_and_warnings;
  mutable __sanitizer::Mutex check_and_warning_mutex;

  struct LoadTrackingValue {
    CheckTypeT check_ty;
    __sanitizer::u32 stack_id = 0;
    __sanitizer::u64 num_invalid = 0;
    __sanitizer::u64 num_unknown = 0;
  };
  // Map Key(CheckTypeT::kLoad, StackId) to indices in TrackedLoads.
  IndexMap LoadTrackingMap;
  __sanitizer::InternalMmapVectorNoCtor<LoadTrackingValue> TrackedLoads;
  mutable __sanitizer::Mutex TrackedLoadsMutex;
};

extern Stats *nsan_stats;
void InitializeStats();

} // namespace __nsan

#endif // NSAN_STATS_H
