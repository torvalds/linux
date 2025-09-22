//===-------- LLVM-provided High-Level Optimization levels -*- C++ -*------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header enumerates the LLVM-provided high-level optimization levels.
/// Each level has a specific goal and rationale.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_OPTIMIZATIONLEVEL_H
#define LLVM_PASSES_OPTIMIZATIONLEVEL_H

#include <assert.h>

namespace llvm {

class OptimizationLevel final {
  unsigned SpeedLevel = 2;
  unsigned SizeLevel = 0;
  OptimizationLevel(unsigned SpeedLevel, unsigned SizeLevel)
      : SpeedLevel(SpeedLevel), SizeLevel(SizeLevel) {
    // Check that only valid combinations are passed.
    assert(SpeedLevel <= 3 &&
           "Optimization level for speed should be 0, 1, 2, or 3");
    assert(SizeLevel <= 2 &&
           "Optimization level for size should be 0, 1, or 2");
    assert((SizeLevel == 0 || SpeedLevel == 2) &&
           "Optimize for size should be encoded with speedup level == 2");
  }

public:
  OptimizationLevel() = default;
  /// Disable as many optimizations as possible. This doesn't completely
  /// disable the optimizer in all cases, for example always_inline functions
  /// can be required to be inlined for correctness.
  static const OptimizationLevel O0;

  /// Optimize quickly without destroying debuggability.
  ///
  /// This level is tuned to produce a result from the optimizer as quickly
  /// as possible and to avoid destroying debuggability. This tends to result
  /// in a very good development mode where the compiled code will be
  /// immediately executed as part of testing. As a consequence, where
  /// possible, we would like to produce efficient-to-execute code, but not
  /// if it significantly slows down compilation or would prevent even basic
  /// debugging of the resulting binary.
  ///
  /// As an example, complex loop transformations such as versioning,
  /// vectorization, or fusion don't make sense here due to the degree to
  /// which the executed code differs from the source code, and the compile
  /// time cost.
  static const OptimizationLevel O1;
  /// Optimize for fast execution as much as possible without triggering
  /// significant incremental compile time or code size growth.
  ///
  /// The key idea is that optimizations at this level should "pay for
  /// themselves". So if an optimization increases compile time by 5% or
  /// increases code size by 5% for a particular benchmark, that benchmark
  /// should also be one which sees a 5% runtime improvement. If the compile
  /// time or code size penalties happen on average across a diverse range of
  /// LLVM users' benchmarks, then the improvements should as well.
  ///
  /// And no matter what, the compile time needs to not grow superlinearly
  /// with the size of input to LLVM so that users can control the runtime of
  /// the optimizer in this mode.
  ///
  /// This is expected to be a good default optimization level for the vast
  /// majority of users.
  static const OptimizationLevel O2;
  /// Optimize for fast execution as much as possible.
  ///
  /// This mode is significantly more aggressive in trading off compile time
  /// and code size to get execution time improvements. The core idea is that
  /// this mode should include any optimization that helps execution time on
  /// balance across a diverse collection of benchmarks, even if it increases
  /// code size or compile time for some benchmarks without corresponding
  /// improvements to execution time.
  ///
  /// Despite being willing to trade more compile time off to get improved
  /// execution time, this mode still tries to avoid superlinear growth in
  /// order to make even significantly slower compile times at least scale
  /// reasonably. This does not preclude very substantial constant factor
  /// costs though.
  static const OptimizationLevel O3;
  /// Similar to \c O2 but tries to optimize for small code size instead of
  /// fast execution without triggering significant incremental execution
  /// time slowdowns.
  ///
  /// The logic here is exactly the same as \c O2, but with code size and
  /// execution time metrics swapped.
  ///
  /// A consequence of the different core goal is that this should in general
  /// produce substantially smaller executables that still run in
  /// a reasonable amount of time.
  static const OptimizationLevel Os;
  /// A very specialized mode that will optimize for code size at any and all
  /// costs.
  ///
  /// This is useful primarily when there are absolute size limitations and
  /// any effort taken to reduce the size is worth it regardless of the
  /// execution time impact. You should expect this level to produce rather
  /// slow, but very small, code.
  static const OptimizationLevel Oz;

  bool isOptimizingForSpeed() const { return SizeLevel == 0 && SpeedLevel > 0; }

  bool isOptimizingForSize() const { return SizeLevel > 0; }

  bool operator==(const OptimizationLevel &Other) const {
    return SizeLevel == Other.SizeLevel && SpeedLevel == Other.SpeedLevel;
  }
  bool operator!=(const OptimizationLevel &Other) const {
    return SizeLevel != Other.SizeLevel || SpeedLevel != Other.SpeedLevel;
  }

  unsigned getSpeedupLevel() const { return SpeedLevel; }

  unsigned getSizeLevel() const { return SizeLevel; }
};
} // namespace llvm

#endif
