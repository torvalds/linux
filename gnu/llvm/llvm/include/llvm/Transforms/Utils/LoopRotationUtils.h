//===- LoopRotationUtils.h - Utilities to perform loop rotation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utilities to convert a loop into a loop with bottom test.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOPROTATIONUTILS_H
#define LLVM_TRANSFORMS_UTILS_LOOPROTATIONUTILS_H

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Loop;
class LoopInfo;
class MemorySSAUpdater;
class ScalarEvolution;
struct SimplifyQuery;
class TargetTransformInfo;

/// Convert a loop into a loop with bottom test. It may
/// perform loop latch simplication as well if the flag RotationOnly
/// is false. The flag Threshold represents the size threshold of the loop
/// header. If the loop header's size exceeds the threshold, the loop rotation
/// will give up. The flag IsUtilMode controls the heuristic used in the
/// LoopRotation. If it is true, the profitability heuristic will be ignored.
bool LoopRotation(Loop *L, LoopInfo *LI, const TargetTransformInfo *TTI,
                  AssumptionCache *AC, DominatorTree *DT, ScalarEvolution *SE,
                  MemorySSAUpdater *MSSAU, const SimplifyQuery &SQ,
                  bool RotationOnly, unsigned Threshold, bool IsUtilMode,
                  bool PrepareForLTO = false);

} // namespace llvm

#endif
