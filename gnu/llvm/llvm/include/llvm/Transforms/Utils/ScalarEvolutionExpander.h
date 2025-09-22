//===---- llvm/Analysis/ScalarEvolutionExpander.h - SCEV Exprs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes used to generate code from scalar expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SCALAREVOLUTIONEXPANDER_H
#define LLVM_TRANSFORMS_UTILS_SCALAREVOLUTIONEXPANDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {
extern cl::opt<unsigned> SCEVCheapExpansionBudget;

/// struct for holding enough information to help calculate the cost of the
/// given SCEV when expanded into IR.
struct SCEVOperand {
  explicit SCEVOperand(unsigned Opc, int Idx, const SCEV *S) :
    ParentOpcode(Opc), OperandIdx(Idx), S(S) { }
  /// LLVM instruction opcode that uses the operand.
  unsigned ParentOpcode;
  /// The use index of an expanded instruction.
  int OperandIdx;
  /// The SCEV operand to be costed.
  const SCEV* S;
};

struct PoisonFlags {
  unsigned NUW : 1;
  unsigned NSW : 1;
  unsigned Exact : 1;
  unsigned Disjoint : 1;
  unsigned NNeg : 1;

  PoisonFlags(const Instruction *I);
  void apply(Instruction *I);
};

/// This class uses information about analyze scalars to rewrite expressions
/// in canonical form.
///
/// Clients should create an instance of this class when rewriting is needed,
/// and destroy it when finished to allow the release of the associated
/// memory.
class SCEVExpander : public SCEVVisitor<SCEVExpander, Value *> {
  friend class SCEVExpanderCleaner;

  ScalarEvolution &SE;
  const DataLayout &DL;

  // New instructions receive a name to identify them with the current pass.
  const char *IVName;

  /// Indicates whether LCSSA phis should be created for inserted values.
  bool PreserveLCSSA;

  // InsertedExpressions caches Values for reuse, so must track RAUW.
  DenseMap<std::pair<const SCEV *, Instruction *>, TrackingVH<Value>>
      InsertedExpressions;

  // InsertedValues only flags inserted instructions so needs no RAUW.
  DenseSet<AssertingVH<Value>> InsertedValues;
  DenseSet<AssertingVH<Value>> InsertedPostIncValues;

  /// Keep track of the existing IR values re-used during expansion.
  /// FIXME: Ideally re-used instructions would not be added to
  /// InsertedValues/InsertedPostIncValues.
  SmallPtrSet<Value *, 16> ReusedValues;

  /// Original flags of instructions for which they were modified. Used
  /// by SCEVExpanderCleaner to undo changes.
  DenseMap<PoisoningVH<Instruction>, PoisonFlags> OrigFlags;

  // The induction variables generated.
  SmallVector<WeakVH, 2> InsertedIVs;

  /// A memoization of the "relevant" loop for a given SCEV.
  DenseMap<const SCEV *, const Loop *> RelevantLoops;

  /// Addrecs referring to any of the given loops are expanded in post-inc
  /// mode. For example, expanding {1,+,1}<L> in post-inc mode returns the add
  /// instruction that adds one to the phi for {0,+,1}<L>, as opposed to a new
  /// phi starting at 1. This is only supported in non-canonical mode.
  PostIncLoopSet PostIncLoops;

  /// When this is non-null, addrecs expanded in the loop it indicates should
  /// be inserted with increments at IVIncInsertPos.
  const Loop *IVIncInsertLoop;

  /// When expanding addrecs in the IVIncInsertLoop loop, insert the IV
  /// increment at this position.
  Instruction *IVIncInsertPos;

  /// Phis that complete an IV chain. Reuse
  DenseSet<AssertingVH<PHINode>> ChainedPhis;

  /// When true, SCEVExpander tries to expand expressions in "canonical" form.
  /// When false, expressions are expanded in a more literal form.
  ///
  /// In "canonical" form addrecs are expanded as arithmetic based on a
  /// canonical induction variable. Note that CanonicalMode doesn't guarantee
  /// that all expressions are expanded in "canonical" form. For some
  /// expressions literal mode can be preferred.
  bool CanonicalMode;

  /// When invoked from LSR, the expander is in "strength reduction" mode. The
  /// only difference is that phi's are only reused if they are already in
  /// "expanded" form.
  bool LSRMode;

  typedef IRBuilder<InstSimplifyFolder, IRBuilderCallbackInserter> BuilderType;
  BuilderType Builder;

  // RAII object that stores the current insertion point and restores it when
  // the object is destroyed. This includes the debug location.  Duplicated
  // from InsertPointGuard to add SetInsertPoint() which is used to updated
  // InsertPointGuards stack when insert points are moved during SCEV
  // expansion.
  class SCEVInsertPointGuard {
    IRBuilderBase &Builder;
    AssertingVH<BasicBlock> Block;
    BasicBlock::iterator Point;
    DebugLoc DbgLoc;
    SCEVExpander *SE;

    SCEVInsertPointGuard(const SCEVInsertPointGuard &) = delete;
    SCEVInsertPointGuard &operator=(const SCEVInsertPointGuard &) = delete;

  public:
    SCEVInsertPointGuard(IRBuilderBase &B, SCEVExpander *SE)
        : Builder(B), Block(B.GetInsertBlock()), Point(B.GetInsertPoint()),
          DbgLoc(B.getCurrentDebugLocation()), SE(SE) {
      SE->InsertPointGuards.push_back(this);
    }

    ~SCEVInsertPointGuard() {
      // These guards should always created/destroyed in FIFO order since they
      // are used to guard lexically scoped blocks of code in
      // ScalarEvolutionExpander.
      assert(SE->InsertPointGuards.back() == this);
      SE->InsertPointGuards.pop_back();
      Builder.restoreIP(IRBuilderBase::InsertPoint(Block, Point));
      Builder.SetCurrentDebugLocation(DbgLoc);
    }

    BasicBlock::iterator GetInsertPoint() const { return Point; }
    void SetInsertPoint(BasicBlock::iterator I) { Point = I; }
  };

  /// Stack of pointers to saved insert points, used to keep insert points
  /// consistent when instructions are moved.
  SmallVector<SCEVInsertPointGuard *, 8> InsertPointGuards;

#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
  const char *DebugType;
#endif

  friend struct SCEVVisitor<SCEVExpander, Value *>;

public:
  /// Construct a SCEVExpander in "canonical" mode.
  explicit SCEVExpander(ScalarEvolution &se, const DataLayout &DL,
                        const char *name, bool PreserveLCSSA = true)
      : SE(se), DL(DL), IVName(name), PreserveLCSSA(PreserveLCSSA),
        IVIncInsertLoop(nullptr), IVIncInsertPos(nullptr), CanonicalMode(true),
        LSRMode(false),
        Builder(se.getContext(), InstSimplifyFolder(DL),
                IRBuilderCallbackInserter(
                    [this](Instruction *I) { rememberInstruction(I); })) {
#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
    DebugType = "";
#endif
  }

  ~SCEVExpander() {
    // Make sure the insert point guard stack is consistent.
    assert(InsertPointGuards.empty());
  }

#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
  void setDebugType(const char *s) { DebugType = s; }
#endif

  /// Erase the contents of the InsertedExpressions map so that users trying
  /// to expand the same expression into multiple BasicBlocks or different
  /// places within the same BasicBlock can do so.
  void clear() {
    InsertedExpressions.clear();
    InsertedValues.clear();
    InsertedPostIncValues.clear();
    ReusedValues.clear();
    OrigFlags.clear();
    ChainedPhis.clear();
    InsertedIVs.clear();
  }

  ScalarEvolution *getSE() { return &SE; }
  const SmallVectorImpl<WeakVH> &getInsertedIVs() const { return InsertedIVs; }

  /// Return a vector containing all instructions inserted during expansion.
  SmallVector<Instruction *, 32> getAllInsertedInstructions() const {
    SmallVector<Instruction *, 32> Result;
    for (const auto &VH : InsertedValues) {
      Value *V = VH;
      if (ReusedValues.contains(V))
        continue;
      if (auto *Inst = dyn_cast<Instruction>(V))
        Result.push_back(Inst);
    }
    for (const auto &VH : InsertedPostIncValues) {
      Value *V = VH;
      if (ReusedValues.contains(V))
        continue;
      if (auto *Inst = dyn_cast<Instruction>(V))
        Result.push_back(Inst);
    }

    return Result;
  }

  /// Return true for expressions that can't be evaluated at runtime
  /// within given \b Budget.
  ///
  /// \p At is a parameter which specifies point in code where user is going to
  /// expand these expressions. Sometimes this knowledge can lead to
  /// a less pessimistic cost estimation.
  bool isHighCostExpansion(ArrayRef<const SCEV *> Exprs, Loop *L,
                           unsigned Budget, const TargetTransformInfo *TTI,
                           const Instruction *At) {
    assert(TTI && "This function requires TTI to be provided.");
    assert(At && "This function requires At instruction to be provided.");
    if (!TTI)      // In assert-less builds, avoid crashing
      return true; // by always claiming to be high-cost.
    SmallVector<SCEVOperand, 8> Worklist;
    SmallPtrSet<const SCEV *, 8> Processed;
    InstructionCost Cost = 0;
    unsigned ScaledBudget = Budget * TargetTransformInfo::TCC_Basic;
    for (auto *Expr : Exprs)
      Worklist.emplace_back(-1, -1, Expr);
    while (!Worklist.empty()) {
      const SCEVOperand WorkItem = Worklist.pop_back_val();
      if (isHighCostExpansionHelper(WorkItem, L, *At, Cost, ScaledBudget, *TTI,
                                    Processed, Worklist))
        return true;
    }
    assert(Cost <= ScaledBudget && "Should have returned from inner loop.");
    return false;
  }

  /// Return the induction variable increment's IV operand.
  Instruction *getIVIncOperand(Instruction *IncV, Instruction *InsertPos,
                               bool allowScale);

  /// Utility for hoisting \p IncV (with all subexpressions requried for its
  /// computation) before \p InsertPos. If \p RecomputePoisonFlags is set, drops
  /// all poison-generating flags from instructions being hoisted and tries to
  /// re-infer them in the new location. It should be used when we are going to
  /// introduce a new use in the new position that didn't exist before, and may
  /// trigger new UB in case of poison.
  bool hoistIVInc(Instruction *IncV, Instruction *InsertPos,
                  bool RecomputePoisonFlags = false);

  /// Return true if both increments directly increment the corresponding IV PHI
  /// nodes and have the same opcode. It is not safe to re-use the flags from
  /// the original increment, if it is more complex and SCEV expansion may have
  /// yielded a more simplified wider increment.
  static bool canReuseFlagsFromOriginalIVInc(PHINode *OrigPhi, PHINode *WidePhi,
                                             Instruction *OrigInc,
                                             Instruction *WideInc);

  /// replace congruent phis with their most canonical representative. Return
  /// the number of phis eliminated.
  unsigned replaceCongruentIVs(Loop *L, const DominatorTree *DT,
                               SmallVectorImpl<WeakTrackingVH> &DeadInsts,
                               const TargetTransformInfo *TTI = nullptr);

  /// Return true if the given expression is safe to expand in the sense that
  /// all materialized values are safe to speculate anywhere their operands are
  /// defined, and the expander is capable of expanding the expression.
  bool isSafeToExpand(const SCEV *S) const;

  /// Return true if the given expression is safe to expand in the sense that
  /// all materialized values are defined and safe to speculate at the specified
  /// location and their operands are defined at this location.
  bool isSafeToExpandAt(const SCEV *S, const Instruction *InsertionPoint) const;

  /// Insert code to directly compute the specified SCEV expression into the
  /// program.  The code is inserted into the specified block.
  Value *expandCodeFor(const SCEV *SH, Type *Ty, BasicBlock::iterator I);
  Value *expandCodeFor(const SCEV *SH, Type *Ty, Instruction *I) {
    return expandCodeFor(SH, Ty, I->getIterator());
  }

  /// Insert code to directly compute the specified SCEV expression into the
  /// program.  The code is inserted into the SCEVExpander's current
  /// insertion point. If a type is specified, the result will be expanded to
  /// have that type, with a cast if necessary.
  Value *expandCodeFor(const SCEV *SH, Type *Ty = nullptr);

  /// Generates a code sequence that evaluates this predicate.  The inserted
  /// instructions will be at position \p Loc.  The result will be of type i1
  /// and will have a value of 0 when the predicate is false and 1 otherwise.
  Value *expandCodeForPredicate(const SCEVPredicate *Pred, Instruction *Loc);

  /// A specialized variant of expandCodeForPredicate, handling the case when
  /// we are expanding code for a SCEVComparePredicate.
  Value *expandComparePredicate(const SCEVComparePredicate *Pred,
                                Instruction *Loc);

  /// Generates code that evaluates if the \p AR expression will overflow.
  Value *generateOverflowCheck(const SCEVAddRecExpr *AR, Instruction *Loc,
                               bool Signed);

  /// A specialized variant of expandCodeForPredicate, handling the case when
  /// we are expanding code for a SCEVWrapPredicate.
  Value *expandWrapPredicate(const SCEVWrapPredicate *P, Instruction *Loc);

  /// A specialized variant of expandCodeForPredicate, handling the case when
  /// we are expanding code for a SCEVUnionPredicate.
  Value *expandUnionPredicate(const SCEVUnionPredicate *Pred, Instruction *Loc);

  /// Set the current IV increment loop and position.
  void setIVIncInsertPos(const Loop *L, Instruction *Pos) {
    assert(!CanonicalMode &&
           "IV increment positions are not supported in CanonicalMode");
    IVIncInsertLoop = L;
    IVIncInsertPos = Pos;
  }

  /// Enable post-inc expansion for addrecs referring to the given
  /// loops. Post-inc expansion is only supported in non-canonical mode.
  void setPostInc(const PostIncLoopSet &L) {
    assert(!CanonicalMode &&
           "Post-inc expansion is not supported in CanonicalMode");
    PostIncLoops = L;
  }

  /// Disable all post-inc expansion.
  void clearPostInc() {
    PostIncLoops.clear();

    // When we change the post-inc loop set, cached expansions may no
    // longer be valid.
    InsertedPostIncValues.clear();
  }

  /// Disable the behavior of expanding expressions in canonical form rather
  /// than in a more literal form. Non-canonical mode is useful for late
  /// optimization passes.
  void disableCanonicalMode() { CanonicalMode = false; }

  void enableLSRMode() { LSRMode = true; }

  /// Set the current insertion point. This is useful if multiple calls to
  /// expandCodeFor() are going to be made with the same insert point and the
  /// insert point may be moved during one of the expansions (e.g. if the
  /// insert point is not a block terminator).
  void setInsertPoint(Instruction *IP) {
    assert(IP);
    Builder.SetInsertPoint(IP);
  }

  void setInsertPoint(BasicBlock::iterator IP) {
    Builder.SetInsertPoint(IP->getParent(), IP);
  }

  /// Clear the current insertion point. This is useful if the instruction
  /// that had been serving as the insertion point may have been deleted.
  void clearInsertPoint() { Builder.ClearInsertionPoint(); }

  /// Set location information used by debugging information.
  void SetCurrentDebugLocation(DebugLoc L) {
    Builder.SetCurrentDebugLocation(std::move(L));
  }

  /// Get location information used by debugging information.
  DebugLoc getCurrentDebugLocation() const {
    return Builder.getCurrentDebugLocation();
  }

  /// Return true if the specified instruction was inserted by the code
  /// rewriter.  If so, the client should not modify the instruction. Note that
  /// this also includes instructions re-used during expansion.
  bool isInsertedInstruction(Instruction *I) const {
    return InsertedValues.count(I) || InsertedPostIncValues.count(I);
  }

  void setChainedPhi(PHINode *PN) { ChainedPhis.insert(PN); }

  /// Determine whether there is an existing expansion of S that can be reused.
  /// This is used to check whether S can be expanded cheaply.
  ///
  /// L is a hint which tells in which loop to look for the suitable value.
  ///
  /// Note that this function does not perform an exhaustive search. I.e if it
  /// didn't find any value it does not mean that there is no such value.
  bool hasRelatedExistingExpansion(const SCEV *S, const Instruction *At,
                                   Loop *L);

  /// Returns a suitable insert point after \p I, that dominates \p
  /// MustDominate. Skips instructions inserted by the expander.
  BasicBlock::iterator findInsertPointAfter(Instruction *I,
                                            Instruction *MustDominate) const;

private:
  LLVMContext &getContext() const { return SE.getContext(); }

  /// Recursive helper function for isHighCostExpansion.
  bool isHighCostExpansionHelper(const SCEVOperand &WorkItem, Loop *L,
                                 const Instruction &At, InstructionCost &Cost,
                                 unsigned Budget,
                                 const TargetTransformInfo &TTI,
                                 SmallPtrSetImpl<const SCEV *> &Processed,
                                 SmallVectorImpl<SCEVOperand> &Worklist);

  /// Insert the specified binary operator, doing a small amount of work to
  /// avoid inserting an obviously redundant operation, and hoisting to an
  /// outer loop when the opportunity is there and it is safe.
  Value *InsertBinop(Instruction::BinaryOps Opcode, Value *LHS, Value *RHS,
                     SCEV::NoWrapFlags Flags, bool IsSafeToHoist);

  /// We want to cast \p V. What would be the best place for such a cast?
  BasicBlock::iterator GetOptimalInsertionPointForCastOf(Value *V) const;

  /// Arrange for there to be a cast of V to Ty at IP, reusing an existing
  /// cast if a suitable one exists, moving an existing cast if a suitable one
  /// exists but isn't in the right place, or creating a new one.
  Value *ReuseOrCreateCast(Value *V, Type *Ty, Instruction::CastOps Op,
                           BasicBlock::iterator IP);

  /// Insert a cast of V to the specified type, which must be possible with a
  /// noop cast, doing what we can to share the casts.
  Value *InsertNoopCastOfTo(Value *V, Type *Ty);

  /// Expand a SCEVAddExpr with a pointer type into a GEP instead of using
  /// ptrtoint+arithmetic+inttoptr.
  Value *expandAddToGEP(const SCEV *Op, Value *V);

  /// Find a previous Value in ExprValueMap for expand.
  /// DropPoisonGeneratingInsts is populated with instructions for which
  /// poison-generating flags must be dropped if the value is reused.
  Value *FindValueInExprValueMap(
      const SCEV *S, const Instruction *InsertPt,
      SmallVectorImpl<Instruction *> &DropPoisonGeneratingInsts);

  Value *expand(const SCEV *S);
  Value *expand(const SCEV *S, BasicBlock::iterator I) {
    setInsertPoint(I);
    return expand(S);
  }
  Value *expand(const SCEV *S, Instruction *I) {
    setInsertPoint(I);
    return expand(S);
  }

  /// Determine the most "relevant" loop for the given SCEV.
  const Loop *getRelevantLoop(const SCEV *);

  Value *expandMinMaxExpr(const SCEVNAryExpr *S, Intrinsic::ID IntrinID,
                          Twine Name, bool IsSequential = false);

  Value *visitConstant(const SCEVConstant *S) { return S->getValue(); }

  Value *visitVScale(const SCEVVScale *S);

  Value *visitPtrToIntExpr(const SCEVPtrToIntExpr *S);

  Value *visitTruncateExpr(const SCEVTruncateExpr *S);

  Value *visitZeroExtendExpr(const SCEVZeroExtendExpr *S);

  Value *visitSignExtendExpr(const SCEVSignExtendExpr *S);

  Value *visitAddExpr(const SCEVAddExpr *S);

  Value *visitMulExpr(const SCEVMulExpr *S);

  Value *visitUDivExpr(const SCEVUDivExpr *S);

  Value *visitAddRecExpr(const SCEVAddRecExpr *S);

  Value *visitSMaxExpr(const SCEVSMaxExpr *S);

  Value *visitUMaxExpr(const SCEVUMaxExpr *S);

  Value *visitSMinExpr(const SCEVSMinExpr *S);

  Value *visitUMinExpr(const SCEVUMinExpr *S);

  Value *visitSequentialUMinExpr(const SCEVSequentialUMinExpr *S);

  Value *visitUnknown(const SCEVUnknown *S) { return S->getValue(); }

  void rememberInstruction(Value *I);

  void rememberFlags(Instruction *I);

  bool isNormalAddRecExprPHI(PHINode *PN, Instruction *IncV, const Loop *L);

  bool isExpandedAddRecExprPHI(PHINode *PN, Instruction *IncV, const Loop *L);

  Value *expandAddRecExprLiterally(const SCEVAddRecExpr *);
  PHINode *getAddRecExprPHILiterally(const SCEVAddRecExpr *Normalized,
                                     const Loop *L, Type *&TruncTy,
                                     bool &InvertStep);
  Value *expandIVInc(PHINode *PN, Value *StepV, const Loop *L,
                     bool useSubtract);

  void fixupInsertPoints(Instruction *I);

  /// Create LCSSA PHIs for \p V, if it is required for uses at the Builder's
  /// current insertion point.
  Value *fixupLCSSAFormFor(Value *V);

  /// Replace congruent phi increments with their most canonical representative.
  /// May swap \p Phi and \p OrigPhi, if \p Phi is more canonical, due to its
  /// increment.
  void replaceCongruentIVInc(PHINode *&Phi, PHINode *&OrigPhi, Loop *L,
                             const DominatorTree *DT,
                             SmallVectorImpl<WeakTrackingVH> &DeadInsts);
};

/// Helper to remove instructions inserted during SCEV expansion, unless they
/// are marked as used.
class SCEVExpanderCleaner {
  SCEVExpander &Expander;

  /// Indicates whether the result of the expansion is used. If false, the
  /// instructions added during expansion are removed.
  bool ResultUsed;

public:
  SCEVExpanderCleaner(SCEVExpander &Expander)
      : Expander(Expander), ResultUsed(false) {}

  ~SCEVExpanderCleaner() { cleanup(); }

  /// Indicate that the result of the expansion is used.
  void markResultUsed() { ResultUsed = true; }

  void cleanup();
};
} // namespace llvm

#endif
