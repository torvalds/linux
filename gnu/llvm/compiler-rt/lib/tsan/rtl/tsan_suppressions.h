//===-- tsan_suppressions.h -------------------------------------*- C++ -*-===//
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
#ifndef TSAN_SUPPRESSIONS_H
#define TSAN_SUPPRESSIONS_H

#include "sanitizer_common/sanitizer_suppressions.h"
#include "tsan_report.h"

namespace __tsan {

const char kSuppressionNone[] = "none";
const char kSuppressionRace[] = "race";
const char kSuppressionRaceTop[] = "race_top";
const char kSuppressionMutex[] = "mutex";
const char kSuppressionThread[] = "thread";
const char kSuppressionSignal[] = "signal";
const char kSuppressionLib[] = "called_from_lib";
const char kSuppressionDeadlock[] = "deadlock";

void InitializeSuppressions();
SuppressionContext *Suppressions();
void PrintMatchedSuppressions();
uptr IsSuppressed(ReportType typ, const ReportStack *stack, Suppression **sp);
uptr IsSuppressed(ReportType typ, const ReportLocation *loc, Suppression **sp);

}  // namespace __tsan

#endif  // TSAN_SUPPRESSIONS_H
