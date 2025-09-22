//===- LoopVectorizationPlanner.h - Planner for LoopVectorization ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides a LoopVectorizationPlanner class.
/// InnerLoopVectorizer vectorizes loops which contain only one basic
/// LoopVectorizationPlanner - drives the vectorization process after having
/// passed Legality checks.
/// The planner builds and optimizes the Vectorization Plans which record the
/// decisions how to vectorize the given loop. In particular, represent the
/// control-flow of the vectorized version, the replication of instructions that
/// are to be scalarized, and interleave access groups.
///
/// Also provides a VPlan-based builder utility analogous to IRBuilder.
/// It provides an instruction-level API for generating VPInstructions while
/// abstracting away the Recipe manipulation details.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZATIONPLANNER_H
#define LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZATIONPLANNER_H

#include "VPlan.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {

class LoopInfo;
class DominatorTree;
class LoopVectorizationLegality;
class LoopVectorizationCostModel;
class PredicatedScalarEvolution;
class LoopVectorizeHints;
class OptimizationRemarkEmitter;
class TargetTransformInfo;
class TargetLibraryInfo;
class VPRecipeBuilder;

/// VPlan-based builder utility analogous to IRBuilder.
class VPBuilder {
  VPBasicBlock *BB = nullptr;
  VPBasicBlock::iterator InsertPt = VPBasicBlock::iterator();

  /// Insert \p VPI in BB at InsertPt if BB is set.
  VPInstruction *tryInsertInstruction(VPInstruction *VPI) {
    if (BB)
      BB->insert(VPI, InsertPt);
    return VPI;
  }

  VPInstruction *createInstruction(unsigned Opcode,
                                   ArrayRef<VPValue *> Operands, DebugLoc DL,
                                   const Twine &Name = "") {
    return tryInsertInstruction(new VPInstruction(Opcode, Operands, DL, Name));
  }

  VPInstruction *createInstruction(unsigned Opcode,
                                   std::initializer_list<VPValue *> Operands,
                                   DebugLoc DL, const Twine &Name = "") {
    return createInstruction(Opcode, ArrayRef<VPValue *>(Operands), DL, Name);
  }

public:
  VPBuilder() = default;
  VPBuilder(VPBasicBlock *InsertBB) { setInsertPoint(InsertBB); }
  VPBuilder(VPRecipeBase *InsertPt) { setInsertPoint(InsertPt); }

  /// Clear the insertion point: created instructions will not be inserted into
  /// a block.
  void clearInsertionPoint() {
    BB = nullptr;
    InsertPt = VPBasicBlock::iterator();
  }

  VPBasicBlock *getInsertBlock() const { return BB; }
  VPBasicBlock::iterator getInsertPoint() const { return InsertPt; }

  /// Create a VPBuilder to insert after \p R.
  static VPBuilder getToInsertAfter(VPRecipeBase *R) {
    VPBuilder B;
    B.setInsertPoint(R->getParent(), std::next(R->getIterator()));
    return B;
  }

  /// InsertPoint - A saved insertion point.
  class VPInsertPoint {
    VPBasicBlock *Block = nullptr;
    VPBasicBlock::iterator Point;

  public:
    /// Creates a new insertion point which doesn't point to anything.
    VPInsertPoint() = default;

    /// Creates a new insertion point at the given location.
    VPInsertPoint(VPBasicBlock *InsertBlock, VPBasicBlock::iterator InsertPoint)
        : Block(InsertBlock), Point(InsertPoint) {}

    /// Returns true if this insert point is set.
    bool isSet() const { return Block != nullptr; }

    VPBasicBlock *getBlock() const { return Block; }
    VPBasicBlock::iterator getPoint() const { return Point; }
  };

  /// Sets the current insert point to a previously-saved location.
  void restoreIP(VPInsertPoint IP) {
    if (IP.isSet())
      setInsertPoint(IP.getBlock(), IP.getPoint());
    else
      clearInsertionPoint();
  }

  /// This specifies that created VPInstructions should be appended to the end
  /// of the specified block.
  void setInsertPoint(VPBasicBlock *TheBB) {
    assert(TheBB && "Attempting to set a null insert point");
    BB = TheBB;
    InsertPt = BB->end();
  }

  /// This specifies that created instructions should be inserted at the
  /// specified point.
  void setInsertPoint(VPBasicBlock *TheBB, VPBasicBlock::iterator IP) {
    BB = TheBB;
    InsertPt = IP;
  }

  /// This specifies that created instructions should be inserted at the
  /// specified point.
  void setInsertPoint(VPRecipeBase *IP) {
    BB = IP->getParent();
    InsertPt = IP->getIterator();
  }

  /// Create an N-ary operation with \p Opcode, \p Operands and set \p Inst as
  /// its underlying Instruction.
  VPInstruction *createNaryOp(unsigned Opcode, ArrayRef<VPValue *> Operands,
                              Instruction *Inst = nullptr,
                              const Twine &Name = "") {
    DebugLoc DL;
    if (Inst)
      DL = Inst->getDebugLoc();
    VPInstruction *NewVPInst = createInstruction(Opcode, Operands, DL, Name);
    NewVPInst->setUnderlyingValue(Inst);
    return NewVPInst;
  }
  VPInstruction *createNaryOp(unsigned Opcode, ArrayRef<VPValue *> Operands,
                              DebugLoc DL, const Twine &Name = "") {
    return createInstruction(Opcode, Operands, DL, Name);
  }

  VPInstruction *createOverflowingOp(unsigned Opcode,
                                     std::initializer_list<VPValue *> Operands,
                                     VPRecipeWithIRFlags::WrapFlagsTy WrapFlags,
                                     DebugLoc DL = {}, const Twine &Name = "") {
    return tryInsertInstruction(
        new VPInstruction(Opcode, Operands, WrapFlags, DL, Name));
  }
  VPValue *createNot(VPValue *Operand, DebugLoc DL = {},
                     const Twine &Name = "") {
    return createInstruction(VPInstruction::Not, {Operand}, DL, Name);
  }

  VPValue *createAnd(VPValue *LHS, VPValue *RHS, DebugLoc DL = {},
                     const Twine &Name = "") {
    return createInstruction(Instruction::BinaryOps::And, {LHS, RHS}, DL, Name);
  }

  VPValue *createOr(VPValue *LHS, VPValue *RHS, DebugLoc DL = {},
                    const Twine &Name = "") {

    return tryInsertInstruction(new VPInstruction(
        Instruction::BinaryOps::Or, {LHS, RHS},
        VPRecipeWithIRFlags::DisjointFlagsTy(false), DL, Name));
  }

  VPValue *createLogicalAnd(VPValue *LHS, VPValue *RHS, DebugLoc DL = {},
                            const Twine &Name = "") {
    return tryInsertInstruction(
        new VPInstruction(VPInstruction::LogicalAnd, {LHS, RHS}, DL, Name));
  }

  VPValue *createSelect(VPValue *Cond, VPValue *TrueVal, VPValue *FalseVal,
                        DebugLoc DL = {}, const Twine &Name = "",
                        std::optional<FastMathFlags> FMFs = std::nullopt) {
    auto *Select =
        FMFs ? new VPInstruction(Instruction::Select, {Cond, TrueVal, FalseVal},
                                 *FMFs, DL, Name)
             : new VPInstruction(Instruction::Select, {Cond, TrueVal, FalseVal},
                                 DL, Name);
    return tryInsertInstruction(Select);
  }

  /// Create a new ICmp VPInstruction with predicate \p Pred and operands \p A
  /// and \p B.
  /// TODO: add createFCmp when needed.
  VPValue *createICmp(CmpInst::Predicate Pred, VPValue *A, VPValue *B,
                      DebugLoc DL = {}, const Twine &Name = "");

  //===--------------------------------------------------------------------===//
  // RAII helpers.
  //===--------------------------------------------------------------------===//

  /// RAII object that stores the current insertion point and restores it when
  /// the object is destroyed.
  class InsertPointGuard {
    VPBuilder &Builder;
    VPBasicBlock *Block;
    VPBasicBlock::iterator Point;

  public:
    InsertPointGuard(VPBuilder &B)
        : Builder(B), Block(B.getInsertBlock()), Point(B.getInsertPoint()) {}

    InsertPointGuard(const InsertPointGuard &) = delete;
    InsertPointGuard &operator=(const InsertPointGuard &) = delete;

    ~InsertPointGuard() { Builder.restoreIP(VPInsertPoint(Block, Point)); }
  };
};

/// TODO: The following VectorizationFactor was pulled out of
/// LoopVectorizationCostModel class. LV also deals with
/// VectorizerParams::VectorizationFactor.
/// We need to streamline them.

/// Information about vectorization costs.
struct VectorizationFactor {
  /// Vector width with best cost.
  ElementCount Width;

  /// Cost of the loop with that width.
  InstructionCost Cost;

  /// Cost of the scalar loop.
  InstructionCost ScalarCost;

  /// The minimum trip count required to make vectorization profitable, e.g. due
  /// to runtime checks.
  ElementCount MinProfitableTripCount;

  VectorizationFactor(ElementCount Width, InstructionCost Cost,
                      InstructionCost ScalarCost)
      : Width(Width), Cost(Cost), ScalarCost(ScalarCost) {}

  /// Width 1 means no vectorization, cost 0 means uncomputed cost.
  static VectorizationFactor Disabled() {
    return {ElementCount::getFixed(1), 0, 0};
  }

  bool operator==(const VectorizationFactor &rhs) const {
    return Width == rhs.Width && Cost == rhs.Cost;
  }

  bool operator!=(const VectorizationFactor &rhs) const {
    return !(*this == rhs);
  }
};

/// A class that represents two vectorization factors (initialized with 0 by
/// default). One for fixed-width vectorization and one for scalable
/// vectorization. This can be used by the vectorizer to choose from a range of
/// fixed and/or scalable VFs in order to find the most cost-effective VF to
/// vectorize with.
struct FixedScalableVFPair {
  ElementCount FixedVF;
  ElementCount ScalableVF;

  FixedScalableVFPair()
      : FixedVF(ElementCount::getFixed(0)),
        ScalableVF(ElementCount::getScalable(0)) {}
  FixedScalableVFPair(const ElementCount &Max) : FixedScalableVFPair() {
    *(Max.isScalable() ? &ScalableVF : &FixedVF) = Max;
  }
  FixedScalableVFPair(const ElementCount &FixedVF,
                      const ElementCount &ScalableVF)
      : FixedVF(FixedVF), ScalableVF(ScalableVF) {
    assert(!FixedVF.isScalable() && ScalableVF.isScalable() &&
           "Invalid scalable properties");
  }

  static FixedScalableVFPair getNone() { return FixedScalableVFPair(); }

  /// \return true if either fixed- or scalable VF is non-zero.
  explicit operator bool() const { return FixedVF || ScalableVF; }

  /// \return true if either fixed- or scalable VF is a valid vector VF.
  bool hasVector() const { return FixedVF.isVector() || ScalableVF.isVector(); }
};

/// Planner drives the vectorization process after having passed
/// Legality checks.
class LoopVectorizationPlanner {
  /// The loop that we evaluate.
  Loop *OrigLoop;

  /// Loop Info analysis.
  LoopInfo *LI;

  /// The dominator tree.
  DominatorTree *DT;

  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// Target Transform Info.
  const TargetTransformInfo &TTI;

  /// The legality analysis.
  LoopVectorizationLegality *Legal;

  /// The profitability analysis.
  LoopVectorizationCostModel &CM;

  /// The interleaved access analysis.
  InterleavedAccessInfo &IAI;

  PredicatedScalarEvolution &PSE;

  const LoopVectorizeHints &Hints;

  OptimizationRemarkEmitter *ORE;

  SmallVector<VPlanPtr, 4> VPlans;

  /// Profitable vector factors.
  SmallVector<VectorizationFactor, 8> ProfitableVFs;

  /// A builder used to construct the current plan.
  VPBuilder Builder;

  /// Computes the cost of \p Plan for vectorization factor \p VF.
  ///
  /// The current implementation requires access to the
  /// LoopVectorizationLegality to handle inductions and reductions, which is
  /// why it is kept separate from the VPlan-only cost infrastructure.
  ///
  /// TODO: Move to VPlan::cost once the use of LoopVectorizationLegality has
  /// been retired.
  InstructionCost cost(VPlan &Plan, ElementCount VF) const;

public:
  LoopVectorizationPlanner(
      Loop *L, LoopInfo *LI, DominatorTree *DT, const TargetLibraryInfo *TLI,
      const TargetTransformInfo &TTI, LoopVectorizationLegality *Legal,
      LoopVectorizationCostModel &CM, InterleavedAccessInfo &IAI,
      PredicatedScalarEvolution &PSE, const LoopVectorizeHints &Hints,
      OptimizationRemarkEmitter *ORE)
      : OrigLoop(L), LI(LI), DT(DT), TLI(TLI), TTI(TTI), Legal(Legal), CM(CM),
        IAI(IAI), PSE(PSE), Hints(Hints), ORE(ORE) {}

  /// Plan how to best vectorize, return the best VF and its cost, or
  /// std::nullopt if vectorization and interleaving should be avoided up front.
  std::optional<VectorizationFactor> plan(ElementCount UserVF, unsigned UserIC);

  /// Use the VPlan-native path to plan how to best vectorize, return the best
  /// VF and its cost.
  VectorizationFactor planInVPlanNativePath(ElementCount UserVF);

  /// Return the best VPlan for \p VF.
  VPlan &getBestPlanFor(ElementCount VF) const;

  /// Return the most profitable plan and fix its VF to the most profitable one.
  VPlan &getBestPlan() const;

  /// Generate the IR code for the vectorized loop captured in VPlan \p BestPlan
  /// according to the best selected \p VF and  \p UF.
  ///
  /// TODO: \p IsEpilogueVectorization is needed to avoid issues due to epilogue
  /// vectorization re-using plans for both the main and epilogue vector loops.
  /// It should be removed once the re-use issue has been fixed.
  /// \p ExpandedSCEVs is passed during execution of the plan for epilogue loop
  /// to re-use expansion results generated during main plan execution.
  ///
  /// Returns a mapping of SCEVs to their expanded IR values and a mapping for
  /// the reduction resume values. Note that this is a temporary workaround
  /// needed due to the current epilogue handling.
  std::pair<DenseMap<const SCEV *, Value *>,
            DenseMap<const RecurrenceDescriptor *, Value *>>
  executePlan(ElementCount VF, unsigned UF, VPlan &BestPlan,
              InnerLoopVectorizer &LB, DominatorTree *DT,
              bool IsEpilogueVectorization,
              const DenseMap<const SCEV *, Value *> *ExpandedSCEVs = nullptr);

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void printPlans(raw_ostream &O);
#endif

  /// Look through the existing plans and return true if we have one with
  /// vectorization factor \p VF.
  bool hasPlanWithVF(ElementCount VF) const {
    return any_of(VPlans,
                  [&](const VPlanPtr &Plan) { return Plan->hasVF(VF); });
  }

  /// Test a \p Predicate on a \p Range of VF's. Return the value of applying
  /// \p Predicate on Range.Start, possibly decreasing Range.End such that the
  /// returned value holds for the entire \p Range.
  static bool
  getDecisionAndClampRange(const std::function<bool(ElementCount)> &Predicate,
                           VFRange &Range);

  /// \return The most profitable vectorization factor and the cost of that VF
  /// for vectorizing the epilogue. Returns VectorizationFactor::Disabled if
  /// epilogue vectorization is not supported for the loop.
  VectorizationFactor
  selectEpilogueVectorizationFactor(const ElementCount MaxVF, unsigned IC);

protected:
  /// Build VPlans for power-of-2 VF's between \p MinVF and \p MaxVF inclusive,
  /// according to the information gathered by Legal when it checked if it is
  /// legal to vectorize the loop.
  void buildVPlans(ElementCount MinVF, ElementCount MaxVF);

private:
  /// Build a VPlan according to the information gathered by Legal. \return a
  /// VPlan for vectorization factors \p Range.Start and up to \p Range.End
  /// exclusive, possibly decreasing \p Range.End.
  VPlanPtr buildVPlan(VFRange &Range);

  /// Build a VPlan using VPRecipes according to the information gather by
  /// Legal. This method is only used for the legacy inner loop vectorizer.
  /// \p Range's largest included VF is restricted to the maximum VF the
  /// returned VPlan is valid for. If no VPlan can be built for the input range,
  /// set the largest included VF to the maximum VF for which no plan could be
  /// built.
  VPlanPtr tryToBuildVPlanWithVPRecipes(VFRange &Range);

  /// Build VPlans for power-of-2 VF's between \p MinVF and \p MaxVF inclusive,
  /// according to the information gathered by Legal when it checked if it is
  /// legal to vectorize the loop. This method creates VPlans using VPRecipes.
  void buildVPlansWithVPRecipes(ElementCount MinVF, ElementCount MaxVF);

  // Adjust the recipes for reductions. For in-loop reductions the chain of
  // instructions leading from the loop exit instr to the phi need to be
  // converted to reductions, with one operand being vector and the other being
  // the scalar reduction chain. For other reductions, a select is introduced
  // between the phi and live-out recipes when folding the tail.
  void adjustRecipesForReductions(VPlanPtr &Plan,
                                  VPRecipeBuilder &RecipeBuilder,
                                  ElementCount MinVF);

  /// \return The most profitable vectorization factor for the available VPlans
  /// and the cost of that VF.
  /// This is now only used to verify the decisions by the new VPlan-based
  /// cost-model and will be retired once the VPlan-based cost-model is
  /// stabilized.
  VectorizationFactor selectVectorizationFactor();

  /// Returns true if the per-lane cost of VectorizationFactor A is lower than
  /// that of B.
  bool isMoreProfitable(const VectorizationFactor &A,
                        const VectorizationFactor &B) const;

  /// Determines if we have the infrastructure to vectorize the loop and its
  /// epilogue, assuming the main loop is vectorized by \p VF.
  bool isCandidateForEpilogueVectorization(const ElementCount VF) const;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZATIONPLANNER_H
