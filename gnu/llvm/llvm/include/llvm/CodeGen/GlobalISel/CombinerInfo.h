//===- llvm/CodeGen/GlobalISel/CombinerInfo.h ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Option class for Targets to specify which operations are combined how and
/// when.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_COMBINERINFO_H
#define LLVM_CODEGEN_GLOBALISEL_COMBINERINFO_H

#include <cassert>
namespace llvm {

class LegalizerInfo;

// Contains information relevant to enabling/disabling various combines for a
// pass.
struct CombinerInfo {
  CombinerInfo(bool AllowIllegalOps, bool ShouldLegalizeIllegal,
               const LegalizerInfo *LInfo, bool OptEnabled, bool OptSize,
               bool MinSize)
      : IllegalOpsAllowed(AllowIllegalOps),
        LegalizeIllegalOps(ShouldLegalizeIllegal), LInfo(LInfo),
        EnableOpt(OptEnabled), EnableOptSize(OptSize), EnableMinSize(MinSize) {
    assert(((AllowIllegalOps || !LegalizeIllegalOps) || LInfo) &&
           "Expecting legalizerInfo when illegalops not allowed");
  }
  virtual ~CombinerInfo() = default;
  /// If \p IllegalOpsAllowed is false, the CombinerHelper will make use of
  /// the legalizerInfo to check for legality before each transformation.
  bool IllegalOpsAllowed; // TODO: Make use of this.

  /// If \p LegalizeIllegalOps is true, the Combiner will also legalize the
  /// illegal ops that are created.
  bool LegalizeIllegalOps; // TODO: Make use of this.
  const LegalizerInfo *LInfo;

  /// Whether optimizations should be enabled. This is to distinguish between
  /// uses of the combiner unconditionally and only when optimizations are
  /// specifically enabled/
  bool EnableOpt;
  /// Whether we're optimizing for size.
  bool EnableOptSize;
  /// Whether we're optimizing for minsize (-Oz).
  bool EnableMinSize;

  /// The maximum number of times the Combiner will iterate over the
  /// MachineFunction. Setting this to 0 enables fixed-point iteration.
  unsigned MaxIterations = 0;
};
} // namespace llvm

#endif
