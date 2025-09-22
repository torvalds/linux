//===- llvm/Transforms/Vectorize/LoopVectorizationLegality.h ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file defines the LoopVectorizationLegality class. Original code
/// in Loop Vectorizer has been moved out to its own file for modularity
/// and reusability.
///
/// Currently, it works for innermost loop vectorization. Extending this to
/// outer loop vectorization is a TODO item.
///
/// Also provides:
/// 1) LoopVectorizeHints class which keeps a number of loop annotations
/// locally for easy look up. It has the ability to write them back as
/// loop metadata, upon request.
/// 2) LoopVectorizationRequirements class for lazy bail out for the purpose
/// of reporting useful failure to vectorize message.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZATIONLEGALITY_H
#define LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZATIONLEGALITY_H

#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

namespace llvm {
class AssumptionCache;
class BasicBlock;
class BlockFrequencyInfo;
class DemandedBits;
class DominatorTree;
class Function;
class Loop;
class LoopInfo;
class Metadata;
class OptimizationRemarkEmitter;
class PredicatedScalarEvolution;
class ProfileSummaryInfo;
class TargetLibraryInfo;
class TargetTransformInfo;
class Type;

/// Utility class for getting and setting loop vectorizer hints in the form
/// of loop metadata.
/// This class keeps a number of loop annotations locally (as member variables)
/// and can, upon request, write them back as metadata on the loop. It will
/// initially scan the loop for existing metadata, and will update the local
/// values based on information in the loop.
/// We cannot write all values to metadata, as the mere presence of some info,
/// for example 'force', means a decision has been made. So, we need to be
/// careful NOT to add them if the user hasn't specifically asked so.
class LoopVectorizeHints {
  enum HintKind {
    HK_WIDTH,
    HK_INTERLEAVE,
    HK_FORCE,
    HK_ISVECTORIZED,
    HK_PREDICATE,
    HK_SCALABLE
  };

  /// Hint - associates name and validation with the hint value.
  struct Hint {
    const char *Name;
    unsigned Value; // This may have to change for non-numeric values.
    HintKind Kind;

    Hint(const char *Name, unsigned Value, HintKind Kind)
        : Name(Name), Value(Value), Kind(Kind) {}

    bool validate(unsigned Val);
  };

  /// Vectorization width.
  Hint Width;

  /// Vectorization interleave factor.
  Hint Interleave;

  /// Vectorization forced
  Hint Force;

  /// Already Vectorized
  Hint IsVectorized;

  /// Vector Predicate
  Hint Predicate;

  /// Says whether we should use fixed width or scalable vectorization.
  Hint Scalable;

  /// Return the loop metadata prefix.
  static StringRef Prefix() { return "llvm.loop."; }

  /// True if there is any unsafe math in the loop.
  bool PotentiallyUnsafe = false;

public:
  enum ForceKind {
    FK_Undefined = -1, ///< Not selected.
    FK_Disabled = 0,   ///< Forcing disabled.
    FK_Enabled = 1,    ///< Forcing enabled.
  };

  enum ScalableForceKind {
    /// Not selected.
    SK_Unspecified = -1,
    /// Disables vectorization with scalable vectors.
    SK_FixedWidthOnly = 0,
    /// Vectorize loops using scalable vectors or fixed-width vectors, but favor
    /// scalable vectors when the cost-model is inconclusive. This is the
    /// default when the scalable.enable hint is enabled through a pragma.
    SK_PreferScalable = 1
  };

  LoopVectorizeHints(const Loop *L, bool InterleaveOnlyWhenForced,
                     OptimizationRemarkEmitter &ORE,
                     const TargetTransformInfo *TTI = nullptr);

  /// Mark the loop L as already vectorized by setting the width to 1.
  void setAlreadyVectorized();

  bool allowVectorization(Function *F, Loop *L,
                          bool VectorizeOnlyWhenForced) const;

  /// Dumps all the hint information.
  void emitRemarkWithHints() const;

  ElementCount getWidth() const {
    return ElementCount::get(Width.Value, (ScalableForceKind)Scalable.Value ==
                                              SK_PreferScalable);
  }

  unsigned getInterleave() const {
    if (Interleave.Value)
      return Interleave.Value;
    // If interleaving is not explicitly set, assume that if we do not want
    // unrolling, we also don't want any interleaving.
    if (llvm::hasUnrollTransformation(TheLoop) & TM_Disable)
      return 1;
    return 0;
  }
  unsigned getIsVectorized() const { return IsVectorized.Value; }
  unsigned getPredicate() const { return Predicate.Value; }
  enum ForceKind getForce() const {
    if ((ForceKind)Force.Value == FK_Undefined &&
        hasDisableAllTransformsHint(TheLoop))
      return FK_Disabled;
    return (ForceKind)Force.Value;
  }

  /// \return true if scalable vectorization has been explicitly disabled.
  bool isScalableVectorizationDisabled() const {
    return (ScalableForceKind)Scalable.Value == SK_FixedWidthOnly;
  }

  /// If hints are provided that force vectorization, use the AlwaysPrint
  /// pass name to force the frontend to print the diagnostic.
  const char *vectorizeAnalysisPassName() const;

  /// When enabling loop hints are provided we allow the vectorizer to change
  /// the order of operations that is given by the scalar loop. This is not
  /// enabled by default because can be unsafe or inefficient. For example,
  /// reordering floating-point operations will change the way round-off
  /// error accumulates in the loop.
  bool allowReordering() const;

  bool isPotentiallyUnsafe() const {
    // Avoid FP vectorization if the target is unsure about proper support.
    // This may be related to the SIMD unit in the target not handling
    // IEEE 754 FP ops properly, or bad single-to-double promotions.
    // Otherwise, a sequence of vectorized loops, even without reduction,
    // could lead to different end results on the destination vectors.
    return getForce() != LoopVectorizeHints::FK_Enabled && PotentiallyUnsafe;
  }

  void setPotentiallyUnsafe() { PotentiallyUnsafe = true; }

private:
  /// Find hints specified in the loop metadata and update local values.
  void getHintsFromMetadata();

  /// Checks string hint with one operand and set value if valid.
  void setHint(StringRef Name, Metadata *Arg);

  /// The loop these hints belong to.
  const Loop *TheLoop;

  /// Interface to emit optimization remarks.
  OptimizationRemarkEmitter &ORE;
};

/// This holds vectorization requirements that must be verified late in
/// the process. The requirements are set by legalize and costmodel. Once
/// vectorization has been determined to be possible and profitable the
/// requirements can be verified by looking for metadata or compiler options.
/// For example, some loops require FP commutativity which is only allowed if
/// vectorization is explicitly specified or if the fast-math compiler option
/// has been provided.
/// Late evaluation of these requirements allows helpful diagnostics to be
/// composed that tells the user what need to be done to vectorize the loop. For
/// example, by specifying #pragma clang loop vectorize or -ffast-math. Late
/// evaluation should be used only when diagnostics can generated that can be
/// followed by a non-expert user.
class LoopVectorizationRequirements {
public:
  /// Track the 1st floating-point instruction that can not be reassociated.
  void addExactFPMathInst(Instruction *I) {
    if (I && !ExactFPMathInst)
      ExactFPMathInst = I;
  }

  Instruction *getExactFPInst() { return ExactFPMathInst; }

private:
  Instruction *ExactFPMathInst = nullptr;
};

/// LoopVectorizationLegality checks if it is legal to vectorize a loop, and
/// to what vectorization factor.
/// This class does not look at the profitability of vectorization, only the
/// legality. This class has two main kinds of checks:
/// * Memory checks - The code in canVectorizeMemory checks if vectorization
///   will change the order of memory accesses in a way that will change the
///   correctness of the program.
/// * Scalars checks - The code in canVectorizeInstrs and canVectorizeMemory
/// checks for a number of different conditions, such as the availability of a
/// single induction variable, that all types are supported and vectorize-able,
/// etc. This code reflects the capabilities of InnerLoopVectorizer.
/// This class is also used by InnerLoopVectorizer for identifying
/// induction variable and the different reduction variables.
class LoopVectorizationLegality {
public:
  LoopVectorizationLegality(
      Loop *L, PredicatedScalarEvolution &PSE, DominatorTree *DT,
      TargetTransformInfo *TTI, TargetLibraryInfo *TLI, Function *F,
      LoopAccessInfoManager &LAIs, LoopInfo *LI, OptimizationRemarkEmitter *ORE,
      LoopVectorizationRequirements *R, LoopVectorizeHints *H, DemandedBits *DB,
      AssumptionCache *AC, BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI)
      : TheLoop(L), LI(LI), PSE(PSE), TTI(TTI), TLI(TLI), DT(DT), LAIs(LAIs),
        ORE(ORE), Requirements(R), Hints(H), DB(DB), AC(AC), BFI(BFI),
        PSI(PSI) {}

  /// ReductionList contains the reduction descriptors for all
  /// of the reductions that were found in the loop.
  using ReductionList = MapVector<PHINode *, RecurrenceDescriptor>;

  /// InductionList saves induction variables and maps them to the
  /// induction descriptor.
  using InductionList = MapVector<PHINode *, InductionDescriptor>;

  /// RecurrenceSet contains the phi nodes that are recurrences other than
  /// inductions and reductions.
  using RecurrenceSet = SmallPtrSet<const PHINode *, 8>;

  /// Returns true if it is legal to vectorize this loop.
  /// This does not mean that it is profitable to vectorize this
  /// loop, only that it is legal to do so.
  /// Temporarily taking UseVPlanNativePath parameter. If true, take
  /// the new code path being implemented for outer loop vectorization
  /// (should be functional for inner loop vectorization) based on VPlan.
  /// If false, good old LV code.
  bool canVectorize(bool UseVPlanNativePath);

  /// Returns true if it is legal to vectorize the FP math operations in this
  /// loop. Vectorizing is legal if we allow reordering of FP operations, or if
  /// we can use in-order reductions.
  bool canVectorizeFPMath(bool EnableStrictReductions);

  /// Return true if we can vectorize this loop while folding its tail by
  /// masking.
  bool canFoldTailByMasking() const;

  /// Mark all respective loads/stores for masking. Must only be called when
  /// tail-folding is possible.
  void prepareToFoldTailByMasking();

  /// Returns the primary induction variable.
  PHINode *getPrimaryInduction() { return PrimaryInduction; }

  /// Returns the reduction variables found in the loop.
  const ReductionList &getReductionVars() const { return Reductions; }

  /// Returns the induction variables found in the loop.
  const InductionList &getInductionVars() const { return Inductions; }

  /// Return the fixed-order recurrences found in the loop.
  RecurrenceSet &getFixedOrderRecurrences() { return FixedOrderRecurrences; }

  /// Returns the widest induction type.
  Type *getWidestInductionType() { return WidestIndTy; }

  /// Returns True if given store is a final invariant store of one of the
  /// reductions found in the loop.
  bool isInvariantStoreOfReduction(StoreInst *SI);

  /// Returns True if given address is invariant and is used to store recurrent
  /// expression
  bool isInvariantAddressOfReduction(Value *V);

  /// Returns True if V is a Phi node of an induction variable in this loop.
  bool isInductionPhi(const Value *V) const;

  /// Returns a pointer to the induction descriptor, if \p Phi is an integer or
  /// floating point induction.
  const InductionDescriptor *getIntOrFpInductionDescriptor(PHINode *Phi) const;

  /// Returns a pointer to the induction descriptor, if \p Phi is pointer
  /// induction.
  const InductionDescriptor *getPointerInductionDescriptor(PHINode *Phi) const;

  /// Returns True if V is a cast that is part of an induction def-use chain,
  /// and had been proven to be redundant under a runtime guard (in other
  /// words, the cast has the same SCEV expression as the induction phi).
  bool isCastedInductionVariable(const Value *V) const;

  /// Returns True if V can be considered as an induction variable in this
  /// loop. V can be the induction phi, or some redundant cast in the def-use
  /// chain of the inducion phi.
  bool isInductionVariable(const Value *V) const;

  /// Returns True if PN is a reduction variable in this loop.
  bool isReductionVariable(PHINode *PN) const { return Reductions.count(PN); }

  /// Returns True if Phi is a fixed-order recurrence in this loop.
  bool isFixedOrderRecurrence(const PHINode *Phi) const;

  /// Return true if the block BB needs to be predicated in order for the loop
  /// to be vectorized.
  bool blockNeedsPredication(BasicBlock *BB) const;

  /// Check if this pointer is consecutive when vectorizing. This happens
  /// when the last index of the GEP is the induction variable, or that the
  /// pointer itself is an induction variable.
  /// This check allows us to vectorize A[idx] into a wide load/store.
  /// Returns:
  /// 0 - Stride is unknown or non-consecutive.
  /// 1 - Address is consecutive.
  /// -1 - Address is consecutive, and decreasing.
  /// NOTE: This method must only be used before modifying the original scalar
  /// loop. Do not use after invoking 'createVectorizedLoopSkeleton' (PR34965).
  int isConsecutivePtr(Type *AccessTy, Value *Ptr) const;

  /// Returns true if value V is uniform across \p VF lanes, when \p VF is
  /// provided, and otherwise if \p V is invariant across all loop iterations.
  bool isInvariant(Value *V) const;

  /// Returns true if value V is uniform across \p VF lanes, when \p VF is
  /// provided, and otherwise if \p V is invariant across all loop iterations.
  bool isUniform(Value *V, ElementCount VF) const;

  /// A uniform memory op is a load or store which accesses the same memory
  /// location on all \p VF lanes, if \p VF is provided and otherwise if the
  /// memory location is invariant.
  bool isUniformMemOp(Instruction &I, ElementCount VF) const;

  /// Returns the information that we collected about runtime memory check.
  const RuntimePointerChecking *getRuntimePointerChecking() const {
    return LAI->getRuntimePointerChecking();
  }

  const LoopAccessInfo *getLAI() const { return LAI; }

  bool isSafeForAnyVectorWidth() const {
    return LAI->getDepChecker().isSafeForAnyVectorWidth();
  }

  uint64_t getMaxSafeVectorWidthInBits() const {
    return LAI->getDepChecker().getMaxSafeVectorWidthInBits();
  }

  /// Returns true if vector representation of the instruction \p I
  /// requires mask.
  bool isMaskRequired(const Instruction *I) const {
    return MaskedOp.contains(I);
  }

  /// Returns true if there is at least one function call in the loop which
  /// has a vectorized variant available.
  bool hasVectorCallVariants() const { return VecCallVariantsFound; }

  unsigned getNumStores() const { return LAI->getNumStores(); }
  unsigned getNumLoads() const { return LAI->getNumLoads(); }

  PredicatedScalarEvolution *getPredicatedScalarEvolution() const {
    return &PSE;
  }

  Loop *getLoop() const { return TheLoop; }

  LoopInfo *getLoopInfo() const { return LI; }

  AssumptionCache *getAssumptionCache() const { return AC; }

  ScalarEvolution *getScalarEvolution() const { return PSE.getSE(); }

  DominatorTree *getDominatorTree() const { return DT; }

private:
  /// Return true if the pre-header, exiting and latch blocks of \p Lp and all
  /// its nested loops are considered legal for vectorization. These legal
  /// checks are common for inner and outer loop vectorization.
  /// Temporarily taking UseVPlanNativePath parameter. If true, take
  /// the new code path being implemented for outer loop vectorization
  /// (should be functional for inner loop vectorization) based on VPlan.
  /// If false, good old LV code.
  bool canVectorizeLoopNestCFG(Loop *Lp, bool UseVPlanNativePath);

  /// Set up outer loop inductions by checking Phis in outer loop header for
  /// supported inductions (int inductions). Return false if any of these Phis
  /// is not a supported induction or if we fail to find an induction.
  bool setupOuterLoopInductions();

  /// Return true if the pre-header, exiting and latch blocks of \p Lp
  /// (non-recursive) are considered legal for vectorization.
  /// Temporarily taking UseVPlanNativePath parameter. If true, take
  /// the new code path being implemented for outer loop vectorization
  /// (should be functional for inner loop vectorization) based on VPlan.
  /// If false, good old LV code.
  bool canVectorizeLoopCFG(Loop *Lp, bool UseVPlanNativePath);

  /// Check if a single basic block loop is vectorizable.
  /// At this point we know that this is a loop with a constant trip count
  /// and we only need to check individual instructions.
  bool canVectorizeInstrs();

  /// When we vectorize loops we may change the order in which
  /// we read and write from memory. This method checks if it is
  /// legal to vectorize the code, considering only memory constrains.
  /// Returns true if the loop is vectorizable
  bool canVectorizeMemory();

  /// Return true if we can vectorize this loop using the IF-conversion
  /// transformation.
  bool canVectorizeWithIfConvert();

  /// Return true if we can vectorize this outer loop. The method performs
  /// specific checks for outer loop vectorization.
  bool canVectorizeOuterLoop();

  /// Return true if all of the instructions in the block can be speculatively
  /// executed, and record the loads/stores that require masking.
  /// \p SafePtrs is a list of addresses that are known to be legal and we know
  /// that we can read from them without segfault.
  /// \p MaskedOp is a list of instructions that have to be transformed into
  /// calls to the appropriate masked intrinsic when the loop is vectorized
  /// or dropped if the instruction is a conditional assume intrinsic.
  bool
  blockCanBePredicated(BasicBlock *BB, SmallPtrSetImpl<Value *> &SafePtrs,
                       SmallPtrSetImpl<const Instruction *> &MaskedOp) const;

  /// Updates the vectorization state by adding \p Phi to the inductions list.
  /// This can set \p Phi as the main induction of the loop if \p Phi is a
  /// better choice for the main induction than the existing one.
  void addInductionPhi(PHINode *Phi, const InductionDescriptor &ID,
                       SmallPtrSetImpl<Value *> &AllowedExit);

  /// The loop that we evaluate.
  Loop *TheLoop;

  /// Loop Info analysis.
  LoopInfo *LI;

  /// A wrapper around ScalarEvolution used to add runtime SCEV checks.
  /// Applies dynamic knowledge to simplify SCEV expressions in the context
  /// of existing SCEV assumptions. The analysis will also add a minimal set
  /// of new predicates if this is required to enable vectorization and
  /// unrolling.
  PredicatedScalarEvolution &PSE;

  /// Target Transform Info.
  TargetTransformInfo *TTI;

  /// Target Library Info.
  TargetLibraryInfo *TLI;

  /// Dominator Tree.
  DominatorTree *DT;

  // LoopAccess analysis.
  LoopAccessInfoManager &LAIs;

  const LoopAccessInfo *LAI = nullptr;

  /// Interface to emit optimization remarks.
  OptimizationRemarkEmitter *ORE;

  //  ---  vectorization state --- //

  /// Holds the primary induction variable. This is the counter of the
  /// loop.
  PHINode *PrimaryInduction = nullptr;

  /// Holds the reduction variables.
  ReductionList Reductions;

  /// Holds all of the induction variables that we found in the loop.
  /// Notice that inductions don't need to start at zero and that induction
  /// variables can be pointers.
  InductionList Inductions;

  /// Holds all the casts that participate in the update chain of the induction
  /// variables, and that have been proven to be redundant (possibly under a
  /// runtime guard). These casts can be ignored when creating the vectorized
  /// loop body.
  SmallPtrSet<Instruction *, 4> InductionCastsToIgnore;

  /// Holds the phi nodes that are fixed-order recurrences.
  RecurrenceSet FixedOrderRecurrences;

  /// Holds the widest induction type encountered.
  Type *WidestIndTy = nullptr;

  /// Allowed outside users. This holds the variables that can be accessed from
  /// outside the loop.
  SmallPtrSet<Value *, 4> AllowedExit;

  /// Vectorization requirements that will go through late-evaluation.
  LoopVectorizationRequirements *Requirements;

  /// Used to emit an analysis of any legality issues.
  LoopVectorizeHints *Hints;

  /// The demanded bits analysis is used to compute the minimum type size in
  /// which a reduction can be computed.
  DemandedBits *DB;

  /// The assumption cache analysis is used to compute the minimum type size in
  /// which a reduction can be computed.
  AssumptionCache *AC;

  /// While vectorizing these instructions we have to generate a
  /// call to the appropriate masked intrinsic or drop them in case of
  /// conditional assumes.
  SmallPtrSet<const Instruction *, 8> MaskedOp;

  /// BFI and PSI are used to check for profile guided size optimizations.
  BlockFrequencyInfo *BFI;
  ProfileSummaryInfo *PSI;

  /// If we discover function calls within the loop which have a valid
  /// vectorized variant, record that fact so that LoopVectorize can
  /// (potentially) make a better decision on the maximum VF and enable
  /// the use of those function variants.
  bool VecCallVariantsFound = false;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZATIONLEGALITY_H
