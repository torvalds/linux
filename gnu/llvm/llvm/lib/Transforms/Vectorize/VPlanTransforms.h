//===- VPlanTransforms.h - Utility VPlan to VPlan transforms --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility VPlan to VPlan transformations.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLANTRANSFORMS_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLANTRANSFORMS_H

#include "VPlan.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm {

class InductionDescriptor;
class Instruction;
class PHINode;
class ScalarEvolution;
class PredicatedScalarEvolution;
class TargetLibraryInfo;
class VPBuilder;

struct VPlanTransforms {
  /// Replaces the VPInstructions in \p Plan with corresponding
  /// widen recipes.
  static void
  VPInstructionsToVPRecipes(VPlanPtr &Plan,
                            function_ref<const InductionDescriptor *(PHINode *)>
                                GetIntOrFpInductionDescriptor,
                            ScalarEvolution &SE, const TargetLibraryInfo &TLI);

  /// Sink users of fixed-order recurrences after the recipe defining their
  /// previous value. Then introduce FirstOrderRecurrenceSplice VPInstructions
  /// to combine the value from the recurrence phis and previous values. The
  /// current implementation assumes all users can be sunk after the previous
  /// value, which is enforced by earlier legality checks.
  /// \returns true if all users of fixed-order recurrences could be re-arranged
  /// as needed or false if it is not possible. In the latter case, \p Plan is
  /// not valid.
  static bool adjustFixedOrderRecurrences(VPlan &Plan, VPBuilder &Builder);

  /// Clear NSW/NUW flags from reduction instructions if necessary.
  static void clearReductionWrapFlags(VPlan &Plan);

  /// Optimize \p Plan based on \p BestVF and \p BestUF. This may restrict the
  /// resulting plan to \p BestVF and \p BestUF.
  static void optimizeForVFAndUF(VPlan &Plan, ElementCount BestVF,
                                 unsigned BestUF,
                                 PredicatedScalarEvolution &PSE);

  /// Apply VPlan-to-VPlan optimizations to \p Plan, including induction recipe
  /// optimizations, dead recipe removal, replicate region optimizations and
  /// block merging.
  static void optimize(VPlan &Plan, ScalarEvolution &SE);

  /// Wrap predicated VPReplicateRecipes with a mask operand in an if-then
  /// region block and remove the mask operand. Optimize the created regions by
  /// iteratively sinking scalar operands into the region, followed by merging
  /// regions until no improvements are remaining.
  static void createAndOptimizeReplicateRegions(VPlan &Plan);

  /// Replace (ICMP_ULE, wide canonical IV, backedge-taken-count) checks with an
  /// (active-lane-mask recipe, wide canonical IV, trip-count). If \p
  /// UseActiveLaneMaskForControlFlow is true, introduce an
  /// VPActiveLaneMaskPHIRecipe. If \p DataAndControlFlowWithoutRuntimeCheck is
  /// true, no minimum-iteration runtime check will be created (during skeleton
  /// creation) and instead it is handled using active-lane-mask. \p
  /// DataAndControlFlowWithoutRuntimeCheck implies \p
  /// UseActiveLaneMaskForControlFlow.
  static void addActiveLaneMask(VPlan &Plan,
                                bool UseActiveLaneMaskForControlFlow,
                                bool DataAndControlFlowWithoutRuntimeCheck);

  /// Insert truncates and extends for any truncated recipe. Redundant casts
  /// will be folded later.
  static void
  truncateToMinimalBitwidths(VPlan &Plan,
                             const MapVector<Instruction *, uint64_t> &MinBWs,
                             LLVMContext &Ctx);

  /// Drop poison flags from recipes that may generate a poison value that is
  /// used after vectorization, even when their operands are not poison. Those
  /// recipes meet the following conditions:
  ///  * Contribute to the address computation of a recipe generating a widen
  ///    memory load/store (VPWidenMemoryInstructionRecipe or
  ///    VPInterleaveRecipe).
  ///  * Such a widen memory load/store has at least one underlying Instruction
  ///    that is in a basic block that needs predication and after vectorization
  ///    the generated instruction won't be predicated.
  /// Uses \p BlockNeedsPredication to check if a block needs predicating.
  /// TODO: Replace BlockNeedsPredication callback with retrieving info from
  ///       VPlan directly.
  static void dropPoisonGeneratingRecipes(
      VPlan &Plan, function_ref<bool(BasicBlock *)> BlockNeedsPredication);

  /// Add a VPEVLBasedIVPHIRecipe and related recipes to \p Plan and
  /// replaces all uses except the canonical IV increment of
  /// VPCanonicalIVPHIRecipe with a VPEVLBasedIVPHIRecipe.
  /// VPCanonicalIVPHIRecipe is only used to control the loop after
  /// this transformation.
  /// \returns true if the transformation succeeds, or false if it doesn't.
  static bool tryAddExplicitVectorLength(VPlan &Plan);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPLANTRANSFORMS_H
