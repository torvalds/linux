//===- LoopVectorize.cpp - A Loop Vectorizer ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM loop vectorizer. This pass modifies 'vectorizable' loops
// and generates target-independent LLVM-IR.
// The vectorizer uses the TargetTransformInfo analysis to estimate the costs
// of instructions in order to estimate the profitability of vectorization.
//
// The loop vectorizer combines consecutive loop iterations into a single
// 'wide' iteration. After this transformation the index is incremented
// by the SIMD vector width, and not by one.
//
// This pass has three parts:
// 1. The main loop pass that drives the different parts.
// 2. LoopVectorizationLegality - A unit that checks for the legality
//    of the vectorization.
// 3. InnerLoopVectorizer - A unit that performs the actual
//    widening of instructions.
// 4. LoopVectorizationCostModel - A unit that checks for the profitability
//    of vectorization. It decides on the optimal vector width, which
//    can be one, if vectorization is not profitable.
//
// There is a development effort going on to migrate loop vectorizer to the
// VPlan infrastructure and to introduce outer loop vectorization support (see
// docs/VectorizationPlan.rst and
// http://lists.llvm.org/pipermail/llvm-dev/2017-December/119523.html). For this
// purpose, we temporarily introduced the VPlan-native vectorization path: an
// alternative vectorization path that is natively implemented on top of the
// VPlan infrastructure. See EnableVPlanNativePath for enabling.
//
//===----------------------------------------------------------------------===//
//
// The reduction-variable vectorization is based on the paper:
//  D. Nuzman and R. Henderson. Multi-platform Auto-vectorization.
//
// Variable uniformity checks are inspired by:
//  Karrenberg, R. and Hack, S. Whole Function Vectorization.
//
// The interleaved access vectorization is based on the paper:
//  Dorit Nuzman, Ira Rosen and Ayal Zaks.  Auto-Vectorization of Interleaved
//  Data for SIMD
//
// Other ideas/concepts are from:
//  A. Zaks and D. Nuzman. Autovectorization in GCC-two years later.
//
//  S. Maleki, Y. Gao, M. Garzaran, T. Wong and D. Padua.  An Evaluation of
//  Vectorizing Compilers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "LoopVectorizationPlanner.h"
#include "VPRecipeBuilder.h"
#include "VPlan.h"
#include "VPlanAnalysis.h"
#include "VPlanHCFGBuilder.h"
#include "VPlanPatternMatch.h"
#include "VPlanTransforms.h"
#include "VPlanVerifier.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/VectorBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/InjectTLIMappings.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include "llvm/Transforms/Vectorize/LoopVectorizationLegality.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define LV_NAME "loop-vectorize"
#define DEBUG_TYPE LV_NAME

#ifndef NDEBUG
const char VerboseDebug[] = DEBUG_TYPE "-verbose";
#endif

/// @{
/// Metadata attribute names
const char LLVMLoopVectorizeFollowupAll[] = "llvm.loop.vectorize.followup_all";
const char LLVMLoopVectorizeFollowupVectorized[] =
    "llvm.loop.vectorize.followup_vectorized";
const char LLVMLoopVectorizeFollowupEpilogue[] =
    "llvm.loop.vectorize.followup_epilogue";
/// @}

STATISTIC(LoopsVectorized, "Number of loops vectorized");
STATISTIC(LoopsAnalyzed, "Number of loops analyzed for vectorization");
STATISTIC(LoopsEpilogueVectorized, "Number of epilogues vectorized");

static cl::opt<bool> EnableEpilogueVectorization(
    "enable-epilogue-vectorization", cl::init(true), cl::Hidden,
    cl::desc("Enable vectorization of epilogue loops."));

static cl::opt<unsigned> EpilogueVectorizationForceVF(
    "epilogue-vectorization-force-VF", cl::init(1), cl::Hidden,
    cl::desc("When epilogue vectorization is enabled, and a value greater than "
             "1 is specified, forces the given VF for all applicable epilogue "
             "loops."));

static cl::opt<unsigned> EpilogueVectorizationMinVF(
    "epilogue-vectorization-minimum-VF", cl::init(16), cl::Hidden,
    cl::desc("Only loops with vectorization factor equal to or larger than "
             "the specified value are considered for epilogue vectorization."));

/// Loops with a known constant trip count below this number are vectorized only
/// if no scalar iteration overheads are incurred.
static cl::opt<unsigned> TinyTripCountVectorThreshold(
    "vectorizer-min-trip-count", cl::init(16), cl::Hidden,
    cl::desc("Loops with a constant trip count that is smaller than this "
             "value are vectorized only if no scalar iteration overheads "
             "are incurred."));

static cl::opt<unsigned> VectorizeMemoryCheckThreshold(
    "vectorize-memory-check-threshold", cl::init(128), cl::Hidden,
    cl::desc("The maximum allowed number of runtime memory checks"));

static cl::opt<bool> UseLegacyCostModel(
    "vectorize-use-legacy-cost-model", cl::init(true), cl::Hidden,
    cl::desc("Use the legacy cost model instead of the VPlan-based cost model. "
             "This option will be removed in the future."));

// Option prefer-predicate-over-epilogue indicates that an epilogue is undesired,
// that predication is preferred, and this lists all options. I.e., the
// vectorizer will try to fold the tail-loop (epilogue) into the vector body
// and predicate the instructions accordingly. If tail-folding fails, there are
// different fallback strategies depending on these values:
namespace PreferPredicateTy {
  enum Option {
    ScalarEpilogue = 0,
    PredicateElseScalarEpilogue,
    PredicateOrDontVectorize
  };
} // namespace PreferPredicateTy

static cl::opt<PreferPredicateTy::Option> PreferPredicateOverEpilogue(
    "prefer-predicate-over-epilogue",
    cl::init(PreferPredicateTy::ScalarEpilogue),
    cl::Hidden,
    cl::desc("Tail-folding and predication preferences over creating a scalar "
             "epilogue loop."),
    cl::values(clEnumValN(PreferPredicateTy::ScalarEpilogue,
                         "scalar-epilogue",
                         "Don't tail-predicate loops, create scalar epilogue"),
              clEnumValN(PreferPredicateTy::PredicateElseScalarEpilogue,
                         "predicate-else-scalar-epilogue",
                         "prefer tail-folding, create scalar epilogue if tail "
                         "folding fails."),
              clEnumValN(PreferPredicateTy::PredicateOrDontVectorize,
                         "predicate-dont-vectorize",
                         "prefers tail-folding, don't attempt vectorization if "
                         "tail-folding fails.")));

static cl::opt<TailFoldingStyle> ForceTailFoldingStyle(
    "force-tail-folding-style", cl::desc("Force the tail folding style"),
    cl::init(TailFoldingStyle::None),
    cl::values(
        clEnumValN(TailFoldingStyle::None, "none", "Disable tail folding"),
        clEnumValN(
            TailFoldingStyle::Data, "data",
            "Create lane mask for data only, using active.lane.mask intrinsic"),
        clEnumValN(TailFoldingStyle::DataWithoutLaneMask,
                   "data-without-lane-mask",
                   "Create lane mask with compare/stepvector"),
        clEnumValN(TailFoldingStyle::DataAndControlFlow, "data-and-control",
                   "Create lane mask using active.lane.mask intrinsic, and use "
                   "it for both data and control flow"),
        clEnumValN(TailFoldingStyle::DataAndControlFlowWithoutRuntimeCheck,
                   "data-and-control-without-rt-check",
                   "Similar to data-and-control, but remove the runtime check"),
        clEnumValN(TailFoldingStyle::DataWithEVL, "data-with-evl",
                   "Use predicated EVL instructions for tail folding. If EVL "
                   "is unsupported, fallback to data-without-lane-mask.")));

static cl::opt<bool> MaximizeBandwidth(
    "vectorizer-maximize-bandwidth", cl::init(false), cl::Hidden,
    cl::desc("Maximize bandwidth when selecting vectorization factor which "
             "will be determined by the smallest type in loop."));

static cl::opt<bool> EnableInterleavedMemAccesses(
    "enable-interleaved-mem-accesses", cl::init(false), cl::Hidden,
    cl::desc("Enable vectorization on interleaved memory accesses in a loop"));

/// An interleave-group may need masking if it resides in a block that needs
/// predication, or in order to mask away gaps.
static cl::opt<bool> EnableMaskedInterleavedMemAccesses(
    "enable-masked-interleaved-mem-accesses", cl::init(false), cl::Hidden,
    cl::desc("Enable vectorization on masked interleaved memory accesses in a loop"));

static cl::opt<unsigned> ForceTargetNumScalarRegs(
    "force-target-num-scalar-regs", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's number of scalar registers."));

static cl::opt<unsigned> ForceTargetNumVectorRegs(
    "force-target-num-vector-regs", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's number of vector registers."));

static cl::opt<unsigned> ForceTargetMaxScalarInterleaveFactor(
    "force-target-max-scalar-interleave", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's max interleave factor for "
             "scalar loops."));

static cl::opt<unsigned> ForceTargetMaxVectorInterleaveFactor(
    "force-target-max-vector-interleave", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's max interleave factor for "
             "vectorized loops."));

cl::opt<unsigned> ForceTargetInstructionCost(
    "force-target-instruction-cost", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's expected cost for "
             "an instruction to a single constant value. Mostly "
             "useful for getting consistent testing."));

static cl::opt<bool> ForceTargetSupportsScalableVectors(
    "force-target-supports-scalable-vectors", cl::init(false), cl::Hidden,
    cl::desc(
        "Pretend that scalable vectors are supported, even if the target does "
        "not support them. This flag should only be used for testing."));

static cl::opt<unsigned> SmallLoopCost(
    "small-loop-cost", cl::init(20), cl::Hidden,
    cl::desc(
        "The cost of a loop that is considered 'small' by the interleaver."));

static cl::opt<bool> LoopVectorizeWithBlockFrequency(
    "loop-vectorize-with-block-frequency", cl::init(true), cl::Hidden,
    cl::desc("Enable the use of the block frequency analysis to access PGO "
             "heuristics minimizing code growth in cold regions and being more "
             "aggressive in hot regions."));

// Runtime interleave loops for load/store throughput.
static cl::opt<bool> EnableLoadStoreRuntimeInterleave(
    "enable-loadstore-runtime-interleave", cl::init(true), cl::Hidden,
    cl::desc(
        "Enable runtime interleaving until load/store ports are saturated"));

/// The number of stores in a loop that are allowed to need predication.
static cl::opt<unsigned> NumberOfStoresToPredicate(
    "vectorize-num-stores-pred", cl::init(1), cl::Hidden,
    cl::desc("Max number of stores to be predicated behind an if."));

static cl::opt<bool> EnableIndVarRegisterHeur(
    "enable-ind-var-reg-heur", cl::init(true), cl::Hidden,
    cl::desc("Count the induction variable only once when interleaving"));

static cl::opt<bool> EnableCondStoresVectorization(
    "enable-cond-stores-vec", cl::init(true), cl::Hidden,
    cl::desc("Enable if predication of stores during vectorization."));

static cl::opt<unsigned> MaxNestedScalarReductionIC(
    "max-nested-scalar-reduction-interleave", cl::init(2), cl::Hidden,
    cl::desc("The maximum interleave count to use when interleaving a scalar "
             "reduction in a nested loop."));

static cl::opt<bool>
    PreferInLoopReductions("prefer-inloop-reductions", cl::init(false),
                           cl::Hidden,
                           cl::desc("Prefer in-loop vector reductions, "
                                    "overriding the targets preference."));

static cl::opt<bool> ForceOrderedReductions(
    "force-ordered-reductions", cl::init(false), cl::Hidden,
    cl::desc("Enable the vectorisation of loops with in-order (strict) "
             "FP reductions"));

static cl::opt<bool> PreferPredicatedReductionSelect(
    "prefer-predicated-reduction-select", cl::init(false), cl::Hidden,
    cl::desc(
        "Prefer predicating a reduction operation over an after loop select."));

namespace llvm {
cl::opt<bool> EnableVPlanNativePath(
    "enable-vplan-native-path", cl::Hidden,
    cl::desc("Enable VPlan-native vectorization path with "
             "support for outer loop vectorization."));
}

// This flag enables the stress testing of the VPlan H-CFG construction in the
// VPlan-native vectorization path. It must be used in conjuction with
// -enable-vplan-native-path. -vplan-verify-hcfg can also be used to enable the
// verification of the H-CFGs built.
static cl::opt<bool> VPlanBuildStressTest(
    "vplan-build-stress-test", cl::init(false), cl::Hidden,
    cl::desc(
        "Build VPlan for every supported loop nest in the function and bail "
        "out right after the build (stress test the VPlan H-CFG construction "
        "in the VPlan-native vectorization path)."));

cl::opt<bool> llvm::EnableLoopInterleaving(
    "interleave-loops", cl::init(true), cl::Hidden,
    cl::desc("Enable loop interleaving in Loop vectorization passes"));
cl::opt<bool> llvm::EnableLoopVectorization(
    "vectorize-loops", cl::init(true), cl::Hidden,
    cl::desc("Run the Loop vectorization passes"));

static cl::opt<bool> PrintVPlansInDotFormat(
    "vplan-print-in-dot-format", cl::Hidden,
    cl::desc("Use dot format instead of plain text when dumping VPlans"));

static cl::opt<cl::boolOrDefault> ForceSafeDivisor(
    "force-widen-divrem-via-safe-divisor", cl::Hidden,
    cl::desc(
        "Override cost based safe divisor widening for div/rem instructions"));

static cl::opt<bool> UseWiderVFIfCallVariantsPresent(
    "vectorizer-maximize-bandwidth-for-vector-calls", cl::init(true),
    cl::Hidden,
    cl::desc("Try wider VFs if they enable the use of vector variants"));

// Likelyhood of bypassing the vectorized loop because assumptions about SCEV
// variables not overflowing do not hold. See `emitSCEVChecks`.
static constexpr uint32_t SCEVCheckBypassWeights[] = {1, 127};
// Likelyhood of bypassing the vectorized loop because pointers overlap. See
// `emitMemRuntimeChecks`.
static constexpr uint32_t MemCheckBypassWeights[] = {1, 127};
// Likelyhood of bypassing the vectorized loop because there are zero trips left
// after prolog. See `emitIterationCountCheck`.
static constexpr uint32_t MinItersBypassWeights[] = {1, 127};

/// A helper function that returns true if the given type is irregular. The
/// type is irregular if its allocated size doesn't equal the store size of an
/// element of the corresponding vector type.
static bool hasIrregularType(Type *Ty, const DataLayout &DL) {
  // Determine if an array of N elements of type Ty is "bitcast compatible"
  // with a <N x Ty> vector.
  // This is only true if there is no padding between the array elements.
  return DL.getTypeAllocSizeInBits(Ty) != DL.getTypeSizeInBits(Ty);
}

/// Returns "best known" trip count for the specified loop \p L as defined by
/// the following procedure:
///   1) Returns exact trip count if it is known.
///   2) Returns expected trip count according to profile data if any.
///   3) Returns upper bound estimate if it is known.
///   4) Returns std::nullopt if all of the above failed.
static std::optional<unsigned> getSmallBestKnownTC(ScalarEvolution &SE,
                                                   Loop *L) {
  // Check if exact trip count is known.
  if (unsigned ExpectedTC = SE.getSmallConstantTripCount(L))
    return ExpectedTC;

  // Check if there is an expected trip count available from profile data.
  if (LoopVectorizeWithBlockFrequency)
    if (auto EstimatedTC = getLoopEstimatedTripCount(L))
      return *EstimatedTC;

  // Check if upper bound estimate is known.
  if (unsigned ExpectedTC = SE.getSmallConstantMaxTripCount(L))
    return ExpectedTC;

  return std::nullopt;
}

namespace {
// Forward declare GeneratedRTChecks.
class GeneratedRTChecks;

using SCEV2ValueTy = DenseMap<const SCEV *, Value *>;
} // namespace

namespace llvm {

AnalysisKey ShouldRunExtraVectorPasses::Key;

/// InnerLoopVectorizer vectorizes loops which contain only one basic
/// block to a specified vectorization factor (VF).
/// This class performs the widening of scalars into vectors, or multiple
/// scalars. This class also implements the following features:
/// * It inserts an epilogue loop for handling loops that don't have iteration
///   counts that are known to be a multiple of the vectorization factor.
/// * It handles the code generation for reduction variables.
/// * Scalarization (implementation using scalars) of un-vectorizable
///   instructions.
/// InnerLoopVectorizer does not perform any vectorization-legality
/// checks, and relies on the caller to check for the different legality
/// aspects. The InnerLoopVectorizer relies on the
/// LoopVectorizationLegality class to provide information about the induction
/// and reduction variables that were found to a given vectorization factor.
class InnerLoopVectorizer {
public:
  InnerLoopVectorizer(Loop *OrigLoop, PredicatedScalarEvolution &PSE,
                      LoopInfo *LI, DominatorTree *DT,
                      const TargetLibraryInfo *TLI,
                      const TargetTransformInfo *TTI, AssumptionCache *AC,
                      OptimizationRemarkEmitter *ORE, ElementCount VecWidth,
                      ElementCount MinProfitableTripCount,
                      unsigned UnrollFactor, LoopVectorizationLegality *LVL,
                      LoopVectorizationCostModel *CM, BlockFrequencyInfo *BFI,
                      ProfileSummaryInfo *PSI, GeneratedRTChecks &RTChecks)
      : OrigLoop(OrigLoop), PSE(PSE), LI(LI), DT(DT), TLI(TLI), TTI(TTI),
        AC(AC), ORE(ORE), VF(VecWidth), UF(UnrollFactor),
        Builder(PSE.getSE()->getContext()), Legal(LVL), Cost(CM), BFI(BFI),
        PSI(PSI), RTChecks(RTChecks) {
    // Query this against the original loop and save it here because the profile
    // of the original loop header may change as the transformation happens.
    OptForSizeBasedOnProfile = llvm::shouldOptimizeForSize(
        OrigLoop->getHeader(), PSI, BFI, PGSOQueryType::IRPass);

    if (MinProfitableTripCount.isZero())
      this->MinProfitableTripCount = VecWidth;
    else
      this->MinProfitableTripCount = MinProfitableTripCount;
  }

  virtual ~InnerLoopVectorizer() = default;

  /// Create a new empty loop that will contain vectorized instructions later
  /// on, while the old loop will be used as the scalar remainder. Control flow
  /// is generated around the vectorized (and scalar epilogue) loops consisting
  /// of various checks and bypasses. Return the pre-header block of the new
  /// loop and the start value for the canonical induction, if it is != 0. The
  /// latter is the case when vectorizing the epilogue loop. In the case of
  /// epilogue vectorization, this function is overriden to handle the more
  /// complex control flow around the loops.  \p ExpandedSCEVs is used to
  /// look up SCEV expansions for expressions needed during skeleton creation.
  virtual std::pair<BasicBlock *, Value *>
  createVectorizedLoopSkeleton(const SCEV2ValueTy &ExpandedSCEVs);

  /// Fix the vectorized code, taking care of header phi's, live-outs, and more.
  void fixVectorizedLoop(VPTransformState &State, VPlan &Plan);

  // Return true if any runtime check is added.
  bool areSafetyChecksAdded() { return AddedSafetyChecks; }

  /// A helper function to scalarize a single Instruction in the innermost loop.
  /// Generates a sequence of scalar instances for each lane between \p MinLane
  /// and \p MaxLane, times each part between \p MinPart and \p MaxPart,
  /// inclusive. Uses the VPValue operands from \p RepRecipe instead of \p
  /// Instr's operands.
  void scalarizeInstruction(const Instruction *Instr,
                            VPReplicateRecipe *RepRecipe,
                            const VPIteration &Instance,
                            VPTransformState &State);

  /// Fix the non-induction PHIs in \p Plan.
  void fixNonInductionPHIs(VPlan &Plan, VPTransformState &State);

  /// Create a new phi node for the induction variable \p OrigPhi to resume
  /// iteration count in the scalar epilogue, from where the vectorized loop
  /// left off. \p Step is the SCEV-expanded induction step to use. In cases
  /// where the loop skeleton is more complicated (i.e., epilogue vectorization)
  /// and the resume values can come from an additional bypass block, the \p
  /// AdditionalBypass pair provides information about the bypass block and the
  /// end value on the edge from bypass to this loop.
  PHINode *createInductionResumeValue(
      PHINode *OrigPhi, const InductionDescriptor &ID, Value *Step,
      ArrayRef<BasicBlock *> BypassBlocks,
      std::pair<BasicBlock *, Value *> AdditionalBypass = {nullptr, nullptr});

  /// Returns the original loop trip count.
  Value *getTripCount() const { return TripCount; }

  /// Used to set the trip count after ILV's construction and after the
  /// preheader block has been executed. Note that this always holds the trip
  /// count of the original loop for both main loop and epilogue vectorization.
  void setTripCount(Value *TC) { TripCount = TC; }

protected:
  friend class LoopVectorizationPlanner;

  /// A small list of PHINodes.
  using PhiVector = SmallVector<PHINode *, 4>;

  /// A type for scalarized values in the new loop. Each value from the
  /// original loop, when scalarized, is represented by UF x VF scalar values
  /// in the new unrolled loop, where UF is the unroll factor and VF is the
  /// vectorization factor.
  using ScalarParts = SmallVector<SmallVector<Value *, 4>, 2>;

  /// Set up the values of the IVs correctly when exiting the vector loop.
  void fixupIVUsers(PHINode *OrigPhi, const InductionDescriptor &II,
                    Value *VectorTripCount, Value *EndValue,
                    BasicBlock *MiddleBlock, BasicBlock *VectorHeader,
                    VPlan &Plan, VPTransformState &State);

  /// Iteratively sink the scalarized operands of a predicated instruction into
  /// the block that was created for it.
  void sinkScalarOperands(Instruction *PredInst);

  /// Returns (and creates if needed) the trip count of the widened loop.
  Value *getOrCreateVectorTripCount(BasicBlock *InsertBlock);

  /// Emit a bypass check to see if the vector trip count is zero, including if
  /// it overflows.
  void emitIterationCountCheck(BasicBlock *Bypass);

  /// Emit a bypass check to see if all of the SCEV assumptions we've
  /// had to make are correct. Returns the block containing the checks or
  /// nullptr if no checks have been added.
  BasicBlock *emitSCEVChecks(BasicBlock *Bypass);

  /// Emit bypass checks to check any memory assumptions we may have made.
  /// Returns the block containing the checks or nullptr if no checks have been
  /// added.
  BasicBlock *emitMemRuntimeChecks(BasicBlock *Bypass);

  /// Emit basic blocks (prefixed with \p Prefix) for the iteration check,
  /// vector loop preheader, middle block and scalar preheader.
  void createVectorLoopSkeleton(StringRef Prefix);

  /// Create new phi nodes for the induction variables to resume iteration count
  /// in the scalar epilogue, from where the vectorized loop left off.
  /// In cases where the loop skeleton is more complicated (eg. epilogue
  /// vectorization) and the resume values can come from an additional bypass
  /// block, the \p AdditionalBypass pair provides information about the bypass
  /// block and the end value on the edge from bypass to this loop.
  void createInductionResumeValues(
      const SCEV2ValueTy &ExpandedSCEVs,
      std::pair<BasicBlock *, Value *> AdditionalBypass = {nullptr, nullptr});

  /// Complete the loop skeleton by adding debug MDs, creating appropriate
  /// conditional branches in the middle block, preparing the builder and
  /// running the verifier. Return the preheader of the completed vector loop.
  BasicBlock *completeLoopSkeleton();

  /// Allow subclasses to override and print debug traces before/after vplan
  /// execution, when trace information is requested.
  virtual void printDebugTracesAtStart(){};
  virtual void printDebugTracesAtEnd(){};

  /// The original loop.
  Loop *OrigLoop;

  /// A wrapper around ScalarEvolution used to add runtime SCEV checks. Applies
  /// dynamic knowledge to simplify SCEV expressions and converts them to a
  /// more usable form.
  PredicatedScalarEvolution &PSE;

  /// Loop Info.
  LoopInfo *LI;

  /// Dominator Tree.
  DominatorTree *DT;

  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// Target Transform Info.
  const TargetTransformInfo *TTI;

  /// Assumption Cache.
  AssumptionCache *AC;

  /// Interface to emit optimization remarks.
  OptimizationRemarkEmitter *ORE;

  /// The vectorization SIMD factor to use. Each vector will have this many
  /// vector elements.
  ElementCount VF;

  ElementCount MinProfitableTripCount;

  /// The vectorization unroll factor to use. Each scalar is vectorized to this
  /// many different vector instructions.
  unsigned UF;

  /// The builder that we use
  IRBuilder<> Builder;

  // --- Vectorization state ---

  /// The vector-loop preheader.
  BasicBlock *LoopVectorPreHeader;

  /// The scalar-loop preheader.
  BasicBlock *LoopScalarPreHeader;

  /// Middle Block between the vector and the scalar.
  BasicBlock *LoopMiddleBlock;

  /// The unique ExitBlock of the scalar loop if one exists.  Note that
  /// there can be multiple exiting edges reaching this block.
  BasicBlock *LoopExitBlock;

  /// The scalar loop body.
  BasicBlock *LoopScalarBody;

  /// A list of all bypass blocks. The first block is the entry of the loop.
  SmallVector<BasicBlock *, 4> LoopBypassBlocks;

  /// Store instructions that were predicated.
  SmallVector<Instruction *, 4> PredicatedInstructions;

  /// Trip count of the original loop.
  Value *TripCount = nullptr;

  /// Trip count of the widened loop (TripCount - TripCount % (VF*UF))
  Value *VectorTripCount = nullptr;

  /// The legality analysis.
  LoopVectorizationLegality *Legal;

  /// The profitablity analysis.
  LoopVectorizationCostModel *Cost;

  // Record whether runtime checks are added.
  bool AddedSafetyChecks = false;

  // Holds the end values for each induction variable. We save the end values
  // so we can later fix-up the external users of the induction variables.
  DenseMap<PHINode *, Value *> IVEndValues;

  /// BFI and PSI are used to check for profile guided size optimizations.
  BlockFrequencyInfo *BFI;
  ProfileSummaryInfo *PSI;

  // Whether this loop should be optimized for size based on profile guided size
  // optimizatios.
  bool OptForSizeBasedOnProfile;

  /// Structure to hold information about generated runtime checks, responsible
  /// for cleaning the checks, if vectorization turns out unprofitable.
  GeneratedRTChecks &RTChecks;

  // Holds the resume values for reductions in the loops, used to set the
  // correct start value of reduction PHIs when vectorizing the epilogue.
  SmallMapVector<const RecurrenceDescriptor *, PHINode *, 4>
      ReductionResumeValues;
};

class InnerLoopUnroller : public InnerLoopVectorizer {
public:
  InnerLoopUnroller(Loop *OrigLoop, PredicatedScalarEvolution &PSE,
                    LoopInfo *LI, DominatorTree *DT,
                    const TargetLibraryInfo *TLI,
                    const TargetTransformInfo *TTI, AssumptionCache *AC,
                    OptimizationRemarkEmitter *ORE, unsigned UnrollFactor,
                    LoopVectorizationLegality *LVL,
                    LoopVectorizationCostModel *CM, BlockFrequencyInfo *BFI,
                    ProfileSummaryInfo *PSI, GeneratedRTChecks &Check)
      : InnerLoopVectorizer(OrigLoop, PSE, LI, DT, TLI, TTI, AC, ORE,
                            ElementCount::getFixed(1),
                            ElementCount::getFixed(1), UnrollFactor, LVL, CM,
                            BFI, PSI, Check) {}
};

/// Encapsulate information regarding vectorization of a loop and its epilogue.
/// This information is meant to be updated and used across two stages of
/// epilogue vectorization.
struct EpilogueLoopVectorizationInfo {
  ElementCount MainLoopVF = ElementCount::getFixed(0);
  unsigned MainLoopUF = 0;
  ElementCount EpilogueVF = ElementCount::getFixed(0);
  unsigned EpilogueUF = 0;
  BasicBlock *MainLoopIterationCountCheck = nullptr;
  BasicBlock *EpilogueIterationCountCheck = nullptr;
  BasicBlock *SCEVSafetyCheck = nullptr;
  BasicBlock *MemSafetyCheck = nullptr;
  Value *TripCount = nullptr;
  Value *VectorTripCount = nullptr;

  EpilogueLoopVectorizationInfo(ElementCount MVF, unsigned MUF,
                                ElementCount EVF, unsigned EUF)
      : MainLoopVF(MVF), MainLoopUF(MUF), EpilogueVF(EVF), EpilogueUF(EUF) {
    assert(EUF == 1 &&
           "A high UF for the epilogue loop is likely not beneficial.");
  }
};

/// An extension of the inner loop vectorizer that creates a skeleton for a
/// vectorized loop that has its epilogue (residual) also vectorized.
/// The idea is to run the vplan on a given loop twice, firstly to setup the
/// skeleton and vectorize the main loop, and secondly to complete the skeleton
/// from the first step and vectorize the epilogue.  This is achieved by
/// deriving two concrete strategy classes from this base class and invoking
/// them in succession from the loop vectorizer planner.
class InnerLoopAndEpilogueVectorizer : public InnerLoopVectorizer {
public:
  InnerLoopAndEpilogueVectorizer(
      Loop *OrigLoop, PredicatedScalarEvolution &PSE, LoopInfo *LI,
      DominatorTree *DT, const TargetLibraryInfo *TLI,
      const TargetTransformInfo *TTI, AssumptionCache *AC,
      OptimizationRemarkEmitter *ORE, EpilogueLoopVectorizationInfo &EPI,
      LoopVectorizationLegality *LVL, llvm::LoopVectorizationCostModel *CM,
      BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI,
      GeneratedRTChecks &Checks)
      : InnerLoopVectorizer(OrigLoop, PSE, LI, DT, TLI, TTI, AC, ORE,
                            EPI.MainLoopVF, EPI.MainLoopVF, EPI.MainLoopUF, LVL,
                            CM, BFI, PSI, Checks),
        EPI(EPI) {}

  // Override this function to handle the more complex control flow around the
  // three loops.
  std::pair<BasicBlock *, Value *> createVectorizedLoopSkeleton(
      const SCEV2ValueTy &ExpandedSCEVs) final {
    return createEpilogueVectorizedLoopSkeleton(ExpandedSCEVs);
  }

  /// The interface for creating a vectorized skeleton using one of two
  /// different strategies, each corresponding to one execution of the vplan
  /// as described above.
  virtual std::pair<BasicBlock *, Value *>
  createEpilogueVectorizedLoopSkeleton(const SCEV2ValueTy &ExpandedSCEVs) = 0;

  /// Holds and updates state information required to vectorize the main loop
  /// and its epilogue in two separate passes. This setup helps us avoid
  /// regenerating and recomputing runtime safety checks. It also helps us to
  /// shorten the iteration-count-check path length for the cases where the
  /// iteration count of the loop is so small that the main vector loop is
  /// completely skipped.
  EpilogueLoopVectorizationInfo &EPI;
};

/// A specialized derived class of inner loop vectorizer that performs
/// vectorization of *main* loops in the process of vectorizing loops and their
/// epilogues.
class EpilogueVectorizerMainLoop : public InnerLoopAndEpilogueVectorizer {
public:
  EpilogueVectorizerMainLoop(
      Loop *OrigLoop, PredicatedScalarEvolution &PSE, LoopInfo *LI,
      DominatorTree *DT, const TargetLibraryInfo *TLI,
      const TargetTransformInfo *TTI, AssumptionCache *AC,
      OptimizationRemarkEmitter *ORE, EpilogueLoopVectorizationInfo &EPI,
      LoopVectorizationLegality *LVL, llvm::LoopVectorizationCostModel *CM,
      BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI,
      GeneratedRTChecks &Check)
      : InnerLoopAndEpilogueVectorizer(OrigLoop, PSE, LI, DT, TLI, TTI, AC, ORE,
                                       EPI, LVL, CM, BFI, PSI, Check) {}
  /// Implements the interface for creating a vectorized skeleton using the
  /// *main loop* strategy (ie the first pass of vplan execution).
  std::pair<BasicBlock *, Value *>
  createEpilogueVectorizedLoopSkeleton(const SCEV2ValueTy &ExpandedSCEVs) final;

protected:
  /// Emits an iteration count bypass check once for the main loop (when \p
  /// ForEpilogue is false) and once for the epilogue loop (when \p
  /// ForEpilogue is true).
  BasicBlock *emitIterationCountCheck(BasicBlock *Bypass, bool ForEpilogue);
  void printDebugTracesAtStart() override;
  void printDebugTracesAtEnd() override;
};

// A specialized derived class of inner loop vectorizer that performs
// vectorization of *epilogue* loops in the process of vectorizing loops and
// their epilogues.
class EpilogueVectorizerEpilogueLoop : public InnerLoopAndEpilogueVectorizer {
public:
  EpilogueVectorizerEpilogueLoop(
      Loop *OrigLoop, PredicatedScalarEvolution &PSE, LoopInfo *LI,
      DominatorTree *DT, const TargetLibraryInfo *TLI,
      const TargetTransformInfo *TTI, AssumptionCache *AC,
      OptimizationRemarkEmitter *ORE, EpilogueLoopVectorizationInfo &EPI,
      LoopVectorizationLegality *LVL, llvm::LoopVectorizationCostModel *CM,
      BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI,
      GeneratedRTChecks &Checks)
      : InnerLoopAndEpilogueVectorizer(OrigLoop, PSE, LI, DT, TLI, TTI, AC, ORE,
                                       EPI, LVL, CM, BFI, PSI, Checks) {
    TripCount = EPI.TripCount;
  }
  /// Implements the interface for creating a vectorized skeleton using the
  /// *epilogue loop* strategy (ie the second pass of vplan execution).
  std::pair<BasicBlock *, Value *>
  createEpilogueVectorizedLoopSkeleton(const SCEV2ValueTy &ExpandedSCEVs) final;

protected:
  /// Emits an iteration count bypass check after the main vector loop has
  /// finished to see if there are any iterations left to execute by either
  /// the vector epilogue or the scalar epilogue.
  BasicBlock *emitMinimumVectorEpilogueIterCountCheck(
                                                      BasicBlock *Bypass,
                                                      BasicBlock *Insert);
  void printDebugTracesAtStart() override;
  void printDebugTracesAtEnd() override;
};
} // end namespace llvm

/// Look for a meaningful debug location on the instruction or it's
/// operands.
static DebugLoc getDebugLocFromInstOrOperands(Instruction *I) {
  if (!I)
    return DebugLoc();

  DebugLoc Empty;
  if (I->getDebugLoc() != Empty)
    return I->getDebugLoc();

  for (Use &Op : I->operands()) {
    if (Instruction *OpInst = dyn_cast<Instruction>(Op))
      if (OpInst->getDebugLoc() != Empty)
        return OpInst->getDebugLoc();
  }

  return I->getDebugLoc();
}

/// Write a \p DebugMsg about vectorization to the debug output stream. If \p I
/// is passed, the message relates to that particular instruction.
#ifndef NDEBUG
static void debugVectorizationMessage(const StringRef Prefix,
                                      const StringRef DebugMsg,
                                      Instruction *I) {
  dbgs() << "LV: " << Prefix << DebugMsg;
  if (I != nullptr)
    dbgs() << " " << *I;
  else
    dbgs() << '.';
  dbgs() << '\n';
}
#endif

/// Create an analysis remark that explains why vectorization failed
///
/// \p PassName is the name of the pass (e.g. can be AlwaysPrint).  \p
/// RemarkName is the identifier for the remark.  If \p I is passed it is an
/// instruction that prevents vectorization.  Otherwise \p TheLoop is used for
/// the location of the remark.  \return the remark object that can be
/// streamed to.
static OptimizationRemarkAnalysis createLVAnalysis(const char *PassName,
    StringRef RemarkName, Loop *TheLoop, Instruction *I) {
  Value *CodeRegion = TheLoop->getHeader();
  DebugLoc DL = TheLoop->getStartLoc();

  if (I) {
    CodeRegion = I->getParent();
    // If there is no debug location attached to the instruction, revert back to
    // using the loop's.
    if (I->getDebugLoc())
      DL = I->getDebugLoc();
  }

  return OptimizationRemarkAnalysis(PassName, RemarkName, DL, CodeRegion);
}

namespace llvm {

/// Return a value for Step multiplied by VF.
Value *createStepForVF(IRBuilderBase &B, Type *Ty, ElementCount VF,
                       int64_t Step) {
  assert(Ty->isIntegerTy() && "Expected an integer step");
  return B.CreateElementCount(Ty, VF.multiplyCoefficientBy(Step));
}

/// Return the runtime value for VF.
Value *getRuntimeVF(IRBuilderBase &B, Type *Ty, ElementCount VF) {
  return B.CreateElementCount(Ty, VF);
}

const SCEV *createTripCountSCEV(Type *IdxTy, PredicatedScalarEvolution &PSE,
                                Loop *OrigLoop) {
  const SCEV *BackedgeTakenCount = PSE.getBackedgeTakenCount();
  assert(!isa<SCEVCouldNotCompute>(BackedgeTakenCount) && "Invalid loop count");

  ScalarEvolution &SE = *PSE.getSE();
  return SE.getTripCountFromExitCount(BackedgeTakenCount, IdxTy, OrigLoop);
}

void reportVectorizationFailure(const StringRef DebugMsg,
                                const StringRef OREMsg, const StringRef ORETag,
                                OptimizationRemarkEmitter *ORE, Loop *TheLoop,
                                Instruction *I) {
  LLVM_DEBUG(debugVectorizationMessage("Not vectorizing: ", DebugMsg, I));
  LoopVectorizeHints Hints(TheLoop, true /* doesn't matter */, *ORE);
  ORE->emit(
      createLVAnalysis(Hints.vectorizeAnalysisPassName(), ORETag, TheLoop, I)
      << "loop not vectorized: " << OREMsg);
}

/// Reports an informative message: print \p Msg for debugging purposes as well
/// as an optimization remark. Uses either \p I as location of the remark, or
/// otherwise \p TheLoop.
static void reportVectorizationInfo(const StringRef Msg, const StringRef ORETag,
                             OptimizationRemarkEmitter *ORE, Loop *TheLoop,
                             Instruction *I = nullptr) {
  LLVM_DEBUG(debugVectorizationMessage("", Msg, I));
  LoopVectorizeHints Hints(TheLoop, true /* doesn't matter */, *ORE);
  ORE->emit(
      createLVAnalysis(Hints.vectorizeAnalysisPassName(), ORETag, TheLoop, I)
      << Msg);
}

/// Report successful vectorization of the loop. In case an outer loop is
/// vectorized, prepend "outer" to the vectorization remark.
static void reportVectorization(OptimizationRemarkEmitter *ORE, Loop *TheLoop,
                                VectorizationFactor VF, unsigned IC) {
  LLVM_DEBUG(debugVectorizationMessage(
      "Vectorizing: ", TheLoop->isInnermost() ? "innermost loop" : "outer loop",
      nullptr));
  StringRef LoopType = TheLoop->isInnermost() ? "" : "outer ";
  ORE->emit([&]() {
    return OptimizationRemark(LV_NAME, "Vectorized", TheLoop->getStartLoc(),
                              TheLoop->getHeader())
           << "vectorized " << LoopType << "loop (vectorization width: "
           << ore::NV("VectorizationFactor", VF.Width)
           << ", interleaved count: " << ore::NV("InterleaveCount", IC) << ")";
  });
}

} // end namespace llvm

namespace llvm {

// Loop vectorization cost-model hints how the scalar epilogue loop should be
// lowered.
enum ScalarEpilogueLowering {

  // The default: allowing scalar epilogues.
  CM_ScalarEpilogueAllowed,

  // Vectorization with OptForSize: don't allow epilogues.
  CM_ScalarEpilogueNotAllowedOptSize,

  // A special case of vectorisation with OptForSize: loops with a very small
  // trip count are considered for vectorization under OptForSize, thereby
  // making sure the cost of their loop body is dominant, free of runtime
  // guards and scalar iteration overheads.
  CM_ScalarEpilogueNotAllowedLowTripLoop,

  // Loop hint predicate indicating an epilogue is undesired.
  CM_ScalarEpilogueNotNeededUsePredicate,

  // Directive indicating we must either tail fold or not vectorize
  CM_ScalarEpilogueNotAllowedUsePredicate
};

using InstructionVFPair = std::pair<Instruction *, ElementCount>;

/// LoopVectorizationCostModel - estimates the expected speedups due to
/// vectorization.
/// In many cases vectorization is not profitable. This can happen because of
/// a number of reasons. In this class we mainly attempt to predict the
/// expected speedup/slowdowns due to the supported instruction set. We use the
/// TargetTransformInfo to query the different backends for the cost of
/// different operations.
class LoopVectorizationCostModel {
public:
  LoopVectorizationCostModel(ScalarEpilogueLowering SEL, Loop *L,
                             PredicatedScalarEvolution &PSE, LoopInfo *LI,
                             LoopVectorizationLegality *Legal,
                             const TargetTransformInfo &TTI,
                             const TargetLibraryInfo *TLI, DemandedBits *DB,
                             AssumptionCache *AC,
                             OptimizationRemarkEmitter *ORE, const Function *F,
                             const LoopVectorizeHints *Hints,
                             InterleavedAccessInfo &IAI)
      : ScalarEpilogueStatus(SEL), TheLoop(L), PSE(PSE), LI(LI), Legal(Legal),
        TTI(TTI), TLI(TLI), DB(DB), AC(AC), ORE(ORE), TheFunction(F),
        Hints(Hints), InterleaveInfo(IAI) {}

  /// \return An upper bound for the vectorization factors (both fixed and
  /// scalable). If the factors are 0, vectorization and interleaving should be
  /// avoided up front.
  FixedScalableVFPair computeMaxVF(ElementCount UserVF, unsigned UserIC);

  /// \return True if runtime checks are required for vectorization, and false
  /// otherwise.
  bool runtimeChecksRequired();

  /// Setup cost-based decisions for user vectorization factor.
  /// \return true if the UserVF is a feasible VF to be chosen.
  bool selectUserVectorizationFactor(ElementCount UserVF) {
    collectUniformsAndScalars(UserVF);
    collectInstsToScalarize(UserVF);
    return expectedCost(UserVF).isValid();
  }

  /// \return The size (in bits) of the smallest and widest types in the code
  /// that needs to be vectorized. We ignore values that remain scalar such as
  /// 64 bit loop indices.
  std::pair<unsigned, unsigned> getSmallestAndWidestTypes();

  /// \return The desired interleave count.
  /// If interleave count has been specified by metadata it will be returned.
  /// Otherwise, the interleave count is computed and returned. VF and LoopCost
  /// are the selected vectorization factor and the cost of the selected VF.
  unsigned selectInterleaveCount(ElementCount VF, InstructionCost LoopCost);

  /// Memory access instruction may be vectorized in more than one way.
  /// Form of instruction after vectorization depends on cost.
  /// This function takes cost-based decisions for Load/Store instructions
  /// and collects them in a map. This decisions map is used for building
  /// the lists of loop-uniform and loop-scalar instructions.
  /// The calculated cost is saved with widening decision in order to
  /// avoid redundant calculations.
  void setCostBasedWideningDecision(ElementCount VF);

  /// A call may be vectorized in different ways depending on whether we have
  /// vectorized variants available and whether the target supports masking.
  /// This function analyzes all calls in the function at the supplied VF,
  /// makes a decision based on the costs of available options, and stores that
  /// decision in a map for use in planning and plan execution.
  void setVectorizedCallDecision(ElementCount VF);

  /// A struct that represents some properties of the register usage
  /// of a loop.
  struct RegisterUsage {
    /// Holds the number of loop invariant values that are used in the loop.
    /// The key is ClassID of target-provided register class.
    SmallMapVector<unsigned, unsigned, 4> LoopInvariantRegs;
    /// Holds the maximum number of concurrent live intervals in the loop.
    /// The key is ClassID of target-provided register class.
    SmallMapVector<unsigned, unsigned, 4> MaxLocalUsers;
  };

  /// \return Returns information about the register usages of the loop for the
  /// given vectorization factors.
  SmallVector<RegisterUsage, 8>
  calculateRegisterUsage(ArrayRef<ElementCount> VFs);

  /// Collect values we want to ignore in the cost model.
  void collectValuesToIgnore();

  /// Collect all element types in the loop for which widening is needed.
  void collectElementTypesForWidening();

  /// Split reductions into those that happen in the loop, and those that happen
  /// outside. In loop reductions are collected into InLoopReductions.
  void collectInLoopReductions();

  /// Returns true if we should use strict in-order reductions for the given
  /// RdxDesc. This is true if the -enable-strict-reductions flag is passed,
  /// the IsOrdered flag of RdxDesc is set and we do not allow reordering
  /// of FP operations.
  bool useOrderedReductions(const RecurrenceDescriptor &RdxDesc) const {
    return !Hints->allowReordering() && RdxDesc.isOrdered();
  }

  /// \returns The smallest bitwidth each instruction can be represented with.
  /// The vector equivalents of these instructions should be truncated to this
  /// type.
  const MapVector<Instruction *, uint64_t> &getMinimalBitwidths() const {
    return MinBWs;
  }

  /// \returns True if it is more profitable to scalarize instruction \p I for
  /// vectorization factor \p VF.
  bool isProfitableToScalarize(Instruction *I, ElementCount VF) const {
    assert(VF.isVector() &&
           "Profitable to scalarize relevant only for VF > 1.");
    assert(
        TheLoop->isInnermost() &&
        "cost-model should not be used for outer loops (in VPlan-native path)");

    auto Scalars = InstsToScalarize.find(VF);
    assert(Scalars != InstsToScalarize.end() &&
           "VF not yet analyzed for scalarization profitability");
    return Scalars->second.contains(I);
  }

  /// Returns true if \p I is known to be uniform after vectorization.
  bool isUniformAfterVectorization(Instruction *I, ElementCount VF) const {
    assert(
        TheLoop->isInnermost() &&
        "cost-model should not be used for outer loops (in VPlan-native path)");
    // Pseudo probe needs to be duplicated for each unrolled iteration and
    // vector lane so that profiled loop trip count can be accurately
    // accumulated instead of being under counted.
    if (isa<PseudoProbeInst>(I))
      return false;

    if (VF.isScalar())
      return true;

    auto UniformsPerVF = Uniforms.find(VF);
    assert(UniformsPerVF != Uniforms.end() &&
           "VF not yet analyzed for uniformity");
    return UniformsPerVF->second.count(I);
  }

  /// Returns true if \p I is known to be scalar after vectorization.
  bool isScalarAfterVectorization(Instruction *I, ElementCount VF) const {
    assert(
        TheLoop->isInnermost() &&
        "cost-model should not be used for outer loops (in VPlan-native path)");
    if (VF.isScalar())
      return true;

    auto ScalarsPerVF = Scalars.find(VF);
    assert(ScalarsPerVF != Scalars.end() &&
           "Scalar values are not calculated for VF");
    return ScalarsPerVF->second.count(I);
  }

  /// \returns True if instruction \p I can be truncated to a smaller bitwidth
  /// for vectorization factor \p VF.
  bool canTruncateToMinimalBitwidth(Instruction *I, ElementCount VF) const {
    return VF.isVector() && MinBWs.contains(I) &&
           !isProfitableToScalarize(I, VF) &&
           !isScalarAfterVectorization(I, VF);
  }

  /// Decision that was taken during cost calculation for memory instruction.
  enum InstWidening {
    CM_Unknown,
    CM_Widen,         // For consecutive accesses with stride +1.
    CM_Widen_Reverse, // For consecutive accesses with stride -1.
    CM_Interleave,
    CM_GatherScatter,
    CM_Scalarize,
    CM_VectorCall,
    CM_IntrinsicCall
  };

  /// Save vectorization decision \p W and \p Cost taken by the cost model for
  /// instruction \p I and vector width \p VF.
  void setWideningDecision(Instruction *I, ElementCount VF, InstWidening W,
                           InstructionCost Cost) {
    assert(VF.isVector() && "Expected VF >=2");
    WideningDecisions[std::make_pair(I, VF)] = std::make_pair(W, Cost);
  }

  /// Save vectorization decision \p W and \p Cost taken by the cost model for
  /// interleaving group \p Grp and vector width \p VF.
  void setWideningDecision(const InterleaveGroup<Instruction> *Grp,
                           ElementCount VF, InstWidening W,
                           InstructionCost Cost) {
    assert(VF.isVector() && "Expected VF >=2");
    /// Broadcast this decicion to all instructions inside the group.
    /// But the cost will be assigned to one instruction only.
    for (unsigned i = 0; i < Grp->getFactor(); ++i) {
      if (auto *I = Grp->getMember(i)) {
        if (Grp->getInsertPos() == I)
          WideningDecisions[std::make_pair(I, VF)] = std::make_pair(W, Cost);
        else
          WideningDecisions[std::make_pair(I, VF)] = std::make_pair(W, 0);
      }
    }
  }

  /// Return the cost model decision for the given instruction \p I and vector
  /// width \p VF. Return CM_Unknown if this instruction did not pass
  /// through the cost modeling.
  InstWidening getWideningDecision(Instruction *I, ElementCount VF) const {
    assert(VF.isVector() && "Expected VF to be a vector VF");
    assert(
        TheLoop->isInnermost() &&
        "cost-model should not be used for outer loops (in VPlan-native path)");

    std::pair<Instruction *, ElementCount> InstOnVF = std::make_pair(I, VF);
    auto Itr = WideningDecisions.find(InstOnVF);
    if (Itr == WideningDecisions.end())
      return CM_Unknown;
    return Itr->second.first;
  }

  /// Return the vectorization cost for the given instruction \p I and vector
  /// width \p VF.
  InstructionCost getWideningCost(Instruction *I, ElementCount VF) {
    assert(VF.isVector() && "Expected VF >=2");
    std::pair<Instruction *, ElementCount> InstOnVF = std::make_pair(I, VF);
    assert(WideningDecisions.contains(InstOnVF) &&
           "The cost is not calculated");
    return WideningDecisions[InstOnVF].second;
  }

  struct CallWideningDecision {
    InstWidening Kind;
    Function *Variant;
    Intrinsic::ID IID;
    std::optional<unsigned> MaskPos;
    InstructionCost Cost;
  };

  void setCallWideningDecision(CallInst *CI, ElementCount VF, InstWidening Kind,
                               Function *Variant, Intrinsic::ID IID,
                               std::optional<unsigned> MaskPos,
                               InstructionCost Cost) {
    assert(!VF.isScalar() && "Expected vector VF");
    CallWideningDecisions[std::make_pair(CI, VF)] = {Kind, Variant, IID,
                                                     MaskPos, Cost};
  }

  CallWideningDecision getCallWideningDecision(CallInst *CI,
                                               ElementCount VF) const {
    assert(!VF.isScalar() && "Expected vector VF");
    return CallWideningDecisions.at(std::make_pair(CI, VF));
  }

  /// Return True if instruction \p I is an optimizable truncate whose operand
  /// is an induction variable. Such a truncate will be removed by adding a new
  /// induction variable with the destination type.
  bool isOptimizableIVTruncate(Instruction *I, ElementCount VF) {
    // If the instruction is not a truncate, return false.
    auto *Trunc = dyn_cast<TruncInst>(I);
    if (!Trunc)
      return false;

    // Get the source and destination types of the truncate.
    Type *SrcTy = ToVectorTy(cast<CastInst>(I)->getSrcTy(), VF);
    Type *DestTy = ToVectorTy(cast<CastInst>(I)->getDestTy(), VF);

    // If the truncate is free for the given types, return false. Replacing a
    // free truncate with an induction variable would add an induction variable
    // update instruction to each iteration of the loop. We exclude from this
    // check the primary induction variable since it will need an update
    // instruction regardless.
    Value *Op = Trunc->getOperand(0);
    if (Op != Legal->getPrimaryInduction() && TTI.isTruncateFree(SrcTy, DestTy))
      return false;

    // If the truncated value is not an induction variable, return false.
    return Legal->isInductionPhi(Op);
  }

  /// Collects the instructions to scalarize for each predicated instruction in
  /// the loop.
  void collectInstsToScalarize(ElementCount VF);

  /// Collect Uniform and Scalar values for the given \p VF.
  /// The sets depend on CM decision for Load/Store instructions
  /// that may be vectorized as interleave, gather-scatter or scalarized.
  /// Also make a decision on what to do about call instructions in the loop
  /// at that VF -- scalarize, call a known vector routine, or call a
  /// vector intrinsic.
  void collectUniformsAndScalars(ElementCount VF) {
    // Do the analysis once.
    if (VF.isScalar() || Uniforms.contains(VF))
      return;
    setCostBasedWideningDecision(VF);
    setVectorizedCallDecision(VF);
    collectLoopUniforms(VF);
    collectLoopScalars(VF);
  }

  /// Returns true if the target machine supports masked store operation
  /// for the given \p DataType and kind of access to \p Ptr.
  bool isLegalMaskedStore(Type *DataType, Value *Ptr, Align Alignment) const {
    return Legal->isConsecutivePtr(DataType, Ptr) &&
           TTI.isLegalMaskedStore(DataType, Alignment);
  }

  /// Returns true if the target machine supports masked load operation
  /// for the given \p DataType and kind of access to \p Ptr.
  bool isLegalMaskedLoad(Type *DataType, Value *Ptr, Align Alignment) const {
    return Legal->isConsecutivePtr(DataType, Ptr) &&
           TTI.isLegalMaskedLoad(DataType, Alignment);
  }

  /// Returns true if the target machine can represent \p V as a masked gather
  /// or scatter operation.
  bool isLegalGatherOrScatter(Value *V, ElementCount VF) {
    bool LI = isa<LoadInst>(V);
    bool SI = isa<StoreInst>(V);
    if (!LI && !SI)
      return false;
    auto *Ty = getLoadStoreType(V);
    Align Align = getLoadStoreAlignment(V);
    if (VF.isVector())
      Ty = VectorType::get(Ty, VF);
    return (LI && TTI.isLegalMaskedGather(Ty, Align)) ||
           (SI && TTI.isLegalMaskedScatter(Ty, Align));
  }

  /// Returns true if the target machine supports all of the reduction
  /// variables found for the given VF.
  bool canVectorizeReductions(ElementCount VF) const {
    return (all_of(Legal->getReductionVars(), [&](auto &Reduction) -> bool {
      const RecurrenceDescriptor &RdxDesc = Reduction.second;
      return TTI.isLegalToVectorizeReduction(RdxDesc, VF);
    }));
  }

  /// Given costs for both strategies, return true if the scalar predication
  /// lowering should be used for div/rem.  This incorporates an override
  /// option so it is not simply a cost comparison.
  bool isDivRemScalarWithPredication(InstructionCost ScalarCost,
                                     InstructionCost SafeDivisorCost) const {
    switch (ForceSafeDivisor) {
    case cl::BOU_UNSET:
      return ScalarCost < SafeDivisorCost;
    case cl::BOU_TRUE:
      return false;
    case cl::BOU_FALSE:
      return true;
    };
    llvm_unreachable("impossible case value");
  }

  /// Returns true if \p I is an instruction which requires predication and
  /// for which our chosen predication strategy is scalarization (i.e. we
  /// don't have an alternate strategy such as masking available).
  /// \p VF is the vectorization factor that will be used to vectorize \p I.
  bool isScalarWithPredication(Instruction *I, ElementCount VF) const;

  /// Returns true if \p I is an instruction that needs to be predicated
  /// at runtime.  The result is independent of the predication mechanism.
  /// Superset of instructions that return true for isScalarWithPredication.
  bool isPredicatedInst(Instruction *I) const;

  /// Return the costs for our two available strategies for lowering a
  /// div/rem operation which requires speculating at least one lane.
  /// First result is for scalarization (will be invalid for scalable
  /// vectors); second is for the safe-divisor strategy.
  std::pair<InstructionCost, InstructionCost>
  getDivRemSpeculationCost(Instruction *I,
                           ElementCount VF) const;

  /// Returns true if \p I is a memory instruction with consecutive memory
  /// access that can be widened.
  bool memoryInstructionCanBeWidened(Instruction *I, ElementCount VF);

  /// Returns true if \p I is a memory instruction in an interleaved-group
  /// of memory accesses that can be vectorized with wide vector loads/stores
  /// and shuffles.
  bool interleavedAccessCanBeWidened(Instruction *I, ElementCount VF) const;

  /// Check if \p Instr belongs to any interleaved access group.
  bool isAccessInterleaved(Instruction *Instr) const {
    return InterleaveInfo.isInterleaved(Instr);
  }

  /// Get the interleaved access group that \p Instr belongs to.
  const InterleaveGroup<Instruction> *
  getInterleavedAccessGroup(Instruction *Instr) const {
    return InterleaveInfo.getInterleaveGroup(Instr);
  }

  /// Returns true if we're required to use a scalar epilogue for at least
  /// the final iteration of the original loop.
  bool requiresScalarEpilogue(bool IsVectorizing) const {
    if (!isScalarEpilogueAllowed()) {
      LLVM_DEBUG(dbgs() << "LV: Loop does not require scalar epilogue\n");
      return false;
    }
    // If we might exit from anywhere but the latch, must run the exiting
    // iteration in scalar form.
    if (TheLoop->getExitingBlock() != TheLoop->getLoopLatch()) {
      LLVM_DEBUG(
          dbgs() << "LV: Loop requires scalar epilogue: multiple exits\n");
      return true;
    }
    if (IsVectorizing && InterleaveInfo.requiresScalarEpilogue()) {
      LLVM_DEBUG(dbgs() << "LV: Loop requires scalar epilogue: "
                           "interleaved group requires scalar epilogue\n");
      return true;
    }
    LLVM_DEBUG(dbgs() << "LV: Loop does not require scalar epilogue\n");
    return false;
  }

  /// Returns true if we're required to use a scalar epilogue for at least
  /// the final iteration of the original loop for all VFs in \p Range.
  /// A scalar epilogue must either be required for all VFs in \p Range or for
  /// none.
  bool requiresScalarEpilogue(VFRange Range) const {
    auto RequiresScalarEpilogue = [this](ElementCount VF) {
      return requiresScalarEpilogue(VF.isVector());
    };
    bool IsRequired = all_of(Range, RequiresScalarEpilogue);
    assert(
        (IsRequired || none_of(Range, RequiresScalarEpilogue)) &&
        "all VFs in range must agree on whether a scalar epilogue is required");
    return IsRequired;
  }

  /// Returns true if a scalar epilogue is not allowed due to optsize or a
  /// loop hint annotation.
  bool isScalarEpilogueAllowed() const {
    return ScalarEpilogueStatus == CM_ScalarEpilogueAllowed;
  }

  /// Returns the TailFoldingStyle that is best for the current loop.
  TailFoldingStyle getTailFoldingStyle(bool IVUpdateMayOverflow = true) const {
    if (!ChosenTailFoldingStyle)
      return TailFoldingStyle::None;
    return IVUpdateMayOverflow ? ChosenTailFoldingStyle->first
                               : ChosenTailFoldingStyle->second;
  }

  /// Selects and saves TailFoldingStyle for 2 options - if IV update may
  /// overflow or not.
  /// \param IsScalableVF true if scalable vector factors enabled.
  /// \param UserIC User specific interleave count.
  void setTailFoldingStyles(bool IsScalableVF, unsigned UserIC) {
    assert(!ChosenTailFoldingStyle && "Tail folding must not be selected yet.");
    if (!Legal->canFoldTailByMasking()) {
      ChosenTailFoldingStyle =
          std::make_pair(TailFoldingStyle::None, TailFoldingStyle::None);
      return;
    }

    if (!ForceTailFoldingStyle.getNumOccurrences()) {
      ChosenTailFoldingStyle = std::make_pair(
          TTI.getPreferredTailFoldingStyle(/*IVUpdateMayOverflow=*/true),
          TTI.getPreferredTailFoldingStyle(/*IVUpdateMayOverflow=*/false));
      return;
    }

    // Set styles when forced.
    ChosenTailFoldingStyle = std::make_pair(ForceTailFoldingStyle.getValue(),
                                            ForceTailFoldingStyle.getValue());
    if (ForceTailFoldingStyle != TailFoldingStyle::DataWithEVL)
      return;
    // Override forced styles if needed.
    // FIXME: use actual opcode/data type for analysis here.
    // FIXME: Investigate opportunity for fixed vector factor.
    bool EVLIsLegal =
        IsScalableVF && UserIC <= 1 &&
        TTI.hasActiveVectorLength(0, nullptr, Align()) &&
        !EnableVPlanNativePath &&
        // FIXME: implement support for max safe dependency distance.
        Legal->isSafeForAnyVectorWidth();
    if (!EVLIsLegal) {
      // If for some reason EVL mode is unsupported, fallback to
      // DataWithoutLaneMask to try to vectorize the loop with folded tail
      // in a generic way.
      ChosenTailFoldingStyle =
          std::make_pair(TailFoldingStyle::DataWithoutLaneMask,
                         TailFoldingStyle::DataWithoutLaneMask);
      LLVM_DEBUG(
          dbgs()
          << "LV: Preference for VP intrinsics indicated. Will "
             "not try to generate VP Intrinsics "
          << (UserIC > 1
                  ? "since interleave count specified is greater than 1.\n"
                  : "due to non-interleaving reasons.\n"));
    }
  }

  /// Returns true if all loop blocks should be masked to fold tail loop.
  bool foldTailByMasking() const {
    // TODO: check if it is possible to check for None style independent of
    // IVUpdateMayOverflow flag in getTailFoldingStyle.
    return getTailFoldingStyle() != TailFoldingStyle::None;
  }

  /// Returns true if the instructions in this block requires predication
  /// for any reason, e.g. because tail folding now requires a predicate
  /// or because the block in the original loop was predicated.
  bool blockNeedsPredicationForAnyReason(BasicBlock *BB) const {
    return foldTailByMasking() || Legal->blockNeedsPredication(BB);
  }

  /// Returns true if VP intrinsics with explicit vector length support should
  /// be generated in the tail folded loop.
  bool foldTailWithEVL() const {
    return getTailFoldingStyle() == TailFoldingStyle::DataWithEVL;
  }

  /// Returns true if the Phi is part of an inloop reduction.
  bool isInLoopReduction(PHINode *Phi) const {
    return InLoopReductions.contains(Phi);
  }

  /// Estimate cost of an intrinsic call instruction CI if it were vectorized
  /// with factor VF.  Return the cost of the instruction, including
  /// scalarization overhead if it's needed.
  InstructionCost getVectorIntrinsicCost(CallInst *CI, ElementCount VF) const;

  /// Estimate cost of a call instruction CI if it were vectorized with factor
  /// VF. Return the cost of the instruction, including scalarization overhead
  /// if it's needed.
  InstructionCost getVectorCallCost(CallInst *CI, ElementCount VF) const;

  /// Invalidates decisions already taken by the cost model.
  void invalidateCostModelingDecisions() {
    WideningDecisions.clear();
    CallWideningDecisions.clear();
    Uniforms.clear();
    Scalars.clear();
  }

  /// Returns the expected execution cost. The unit of the cost does
  /// not matter because we use the 'cost' units to compare different
  /// vector widths. The cost that is returned is *not* normalized by
  /// the factor width. If \p Invalid is not nullptr, this function
  /// will add a pair(Instruction*, ElementCount) to \p Invalid for
  /// each instruction that has an Invalid cost for the given VF.
  InstructionCost
  expectedCost(ElementCount VF,
               SmallVectorImpl<InstructionVFPair> *Invalid = nullptr);

  bool hasPredStores() const { return NumPredStores > 0; }

  /// Returns true if epilogue vectorization is considered profitable, and
  /// false otherwise.
  /// \p VF is the vectorization factor chosen for the original loop.
  bool isEpilogueVectorizationProfitable(const ElementCount VF) const;

  /// Returns the execution time cost of an instruction for a given vector
  /// width. Vector width of one means scalar.
  InstructionCost getInstructionCost(Instruction *I, ElementCount VF);

  /// Return the cost of instructions in an inloop reduction pattern, if I is
  /// part of that pattern.
  std::optional<InstructionCost>
  getReductionPatternCost(Instruction *I, ElementCount VF, Type *VectorTy,
                          TTI::TargetCostKind CostKind) const;

private:
  unsigned NumPredStores = 0;

  /// \return An upper bound for the vectorization factors for both
  /// fixed and scalable vectorization, where the minimum-known number of
  /// elements is a power-of-2 larger than zero. If scalable vectorization is
  /// disabled or unsupported, then the scalable part will be equal to
  /// ElementCount::getScalable(0).
  FixedScalableVFPair computeFeasibleMaxVF(unsigned MaxTripCount,
                                           ElementCount UserVF,
                                           bool FoldTailByMasking);

  /// \return the maximized element count based on the targets vector
  /// registers and the loop trip-count, but limited to a maximum safe VF.
  /// This is a helper function of computeFeasibleMaxVF.
  ElementCount getMaximizedVFForTarget(unsigned MaxTripCount,
                                       unsigned SmallestType,
                                       unsigned WidestType,
                                       ElementCount MaxSafeVF,
                                       bool FoldTailByMasking);

  /// Checks if scalable vectorization is supported and enabled. Caches the
  /// result to avoid repeated debug dumps for repeated queries.
  bool isScalableVectorizationAllowed();

  /// \return the maximum legal scalable VF, based on the safe max number
  /// of elements.
  ElementCount getMaxLegalScalableVF(unsigned MaxSafeElements);

  /// Calculate vectorization cost of memory instruction \p I.
  InstructionCost getMemoryInstructionCost(Instruction *I, ElementCount VF);

  /// The cost computation for scalarized memory instruction.
  InstructionCost getMemInstScalarizationCost(Instruction *I, ElementCount VF);

  /// The cost computation for interleaving group of memory instructions.
  InstructionCost getInterleaveGroupCost(Instruction *I, ElementCount VF);

  /// The cost computation for Gather/Scatter instruction.
  InstructionCost getGatherScatterCost(Instruction *I, ElementCount VF);

  /// The cost computation for widening instruction \p I with consecutive
  /// memory access.
  InstructionCost getConsecutiveMemOpCost(Instruction *I, ElementCount VF);

  /// The cost calculation for Load/Store instruction \p I with uniform pointer -
  /// Load: scalar load + broadcast.
  /// Store: scalar store + (loop invariant value stored? 0 : extract of last
  /// element)
  InstructionCost getUniformMemOpCost(Instruction *I, ElementCount VF);

  /// Estimate the overhead of scalarizing an instruction. This is a
  /// convenience wrapper for the type-based getScalarizationOverhead API.
  InstructionCost getScalarizationOverhead(Instruction *I, ElementCount VF,
                                           TTI::TargetCostKind CostKind) const;

  /// Returns true if an artificially high cost for emulated masked memrefs
  /// should be used.
  bool useEmulatedMaskMemRefHack(Instruction *I, ElementCount VF);

  /// Map of scalar integer values to the smallest bitwidth they can be legally
  /// represented as. The vector equivalents of these values should be truncated
  /// to this type.
  MapVector<Instruction *, uint64_t> MinBWs;

  /// A type representing the costs for instructions if they were to be
  /// scalarized rather than vectorized. The entries are Instruction-Cost
  /// pairs.
  using ScalarCostsTy = DenseMap<Instruction *, InstructionCost>;

  /// A set containing all BasicBlocks that are known to present after
  /// vectorization as a predicated block.
  DenseMap<ElementCount, SmallPtrSet<BasicBlock *, 4>>
      PredicatedBBsAfterVectorization;

  /// Records whether it is allowed to have the original scalar loop execute at
  /// least once. This may be needed as a fallback loop in case runtime
  /// aliasing/dependence checks fail, or to handle the tail/remainder
  /// iterations when the trip count is unknown or doesn't divide by the VF,
  /// or as a peel-loop to handle gaps in interleave-groups.
  /// Under optsize and when the trip count is very small we don't allow any
  /// iterations to execute in the scalar loop.
  ScalarEpilogueLowering ScalarEpilogueStatus = CM_ScalarEpilogueAllowed;

  /// Control finally chosen tail folding style. The first element is used if
  /// the IV update may overflow, the second element - if it does not.
  std::optional<std::pair<TailFoldingStyle, TailFoldingStyle>>
      ChosenTailFoldingStyle;

  /// true if scalable vectorization is supported and enabled.
  std::optional<bool> IsScalableVectorizationAllowed;

  /// A map holding scalar costs for different vectorization factors. The
  /// presence of a cost for an instruction in the mapping indicates that the
  /// instruction will be scalarized when vectorizing with the associated
  /// vectorization factor. The entries are VF-ScalarCostTy pairs.
  DenseMap<ElementCount, ScalarCostsTy> InstsToScalarize;

  /// Holds the instructions known to be uniform after vectorization.
  /// The data is collected per VF.
  DenseMap<ElementCount, SmallPtrSet<Instruction *, 4>> Uniforms;

  /// Holds the instructions known to be scalar after vectorization.
  /// The data is collected per VF.
  DenseMap<ElementCount, SmallPtrSet<Instruction *, 4>> Scalars;

  /// Holds the instructions (address computations) that are forced to be
  /// scalarized.
  DenseMap<ElementCount, SmallPtrSet<Instruction *, 4>> ForcedScalars;

  /// PHINodes of the reductions that should be expanded in-loop.
  SmallPtrSet<PHINode *, 4> InLoopReductions;

  /// A Map of inloop reduction operations and their immediate chain operand.
  /// FIXME: This can be removed once reductions can be costed correctly in
  /// VPlan. This was added to allow quick lookup of the inloop operations.
  DenseMap<Instruction *, Instruction *> InLoopReductionImmediateChains;

  /// Returns the expected difference in cost from scalarizing the expression
  /// feeding a predicated instruction \p PredInst. The instructions to
  /// scalarize and their scalar costs are collected in \p ScalarCosts. A
  /// non-negative return value implies the expression will be scalarized.
  /// Currently, only single-use chains are considered for scalarization.
  InstructionCost computePredInstDiscount(Instruction *PredInst,
                                          ScalarCostsTy &ScalarCosts,
                                          ElementCount VF);

  /// Collect the instructions that are uniform after vectorization. An
  /// instruction is uniform if we represent it with a single scalar value in
  /// the vectorized loop corresponding to each vector iteration. Examples of
  /// uniform instructions include pointer operands of consecutive or
  /// interleaved memory accesses. Note that although uniformity implies an
  /// instruction will be scalar, the reverse is not true. In general, a
  /// scalarized instruction will be represented by VF scalar values in the
  /// vectorized loop, each corresponding to an iteration of the original
  /// scalar loop.
  void collectLoopUniforms(ElementCount VF);

  /// Collect the instructions that are scalar after vectorization. An
  /// instruction is scalar if it is known to be uniform or will be scalarized
  /// during vectorization. collectLoopScalars should only add non-uniform nodes
  /// to the list if they are used by a load/store instruction that is marked as
  /// CM_Scalarize. Non-uniform scalarized instructions will be represented by
  /// VF values in the vectorized loop, each corresponding to an iteration of
  /// the original scalar loop.
  void collectLoopScalars(ElementCount VF);

  /// Keeps cost model vectorization decision and cost for instructions.
  /// Right now it is used for memory instructions only.
  using DecisionList = DenseMap<std::pair<Instruction *, ElementCount>,
                                std::pair<InstWidening, InstructionCost>>;

  DecisionList WideningDecisions;

  using CallDecisionList =
      DenseMap<std::pair<CallInst *, ElementCount>, CallWideningDecision>;

  CallDecisionList CallWideningDecisions;

  /// Returns true if \p V is expected to be vectorized and it needs to be
  /// extracted.
  bool needsExtract(Value *V, ElementCount VF) const {
    Instruction *I = dyn_cast<Instruction>(V);
    if (VF.isScalar() || !I || !TheLoop->contains(I) ||
        TheLoop->isLoopInvariant(I))
      return false;

    // Assume we can vectorize V (and hence we need extraction) if the
    // scalars are not computed yet. This can happen, because it is called
    // via getScalarizationOverhead from setCostBasedWideningDecision, before
    // the scalars are collected. That should be a safe assumption in most
    // cases, because we check if the operands have vectorizable types
    // beforehand in LoopVectorizationLegality.
    return !Scalars.contains(VF) || !isScalarAfterVectorization(I, VF);
  };

  /// Returns a range containing only operands needing to be extracted.
  SmallVector<Value *, 4> filterExtractingOperands(Instruction::op_range Ops,
                                                   ElementCount VF) const {
    return SmallVector<Value *, 4>(make_filter_range(
        Ops, [this, VF](Value *V) { return this->needsExtract(V, VF); }));
  }

public:
  /// The loop that we evaluate.
  Loop *TheLoop;

  /// Predicated scalar evolution analysis.
  PredicatedScalarEvolution &PSE;

  /// Loop Info analysis.
  LoopInfo *LI;

  /// Vectorization legality.
  LoopVectorizationLegality *Legal;

  /// Vector target information.
  const TargetTransformInfo &TTI;

  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// Demanded bits analysis.
  DemandedBits *DB;

  /// Assumption cache.
  AssumptionCache *AC;

  /// Interface to emit optimization remarks.
  OptimizationRemarkEmitter *ORE;

  const Function *TheFunction;

  /// Loop Vectorize Hint.
  const LoopVectorizeHints *Hints;

  /// The interleave access information contains groups of interleaved accesses
  /// with the same stride and close to each other.
  InterleavedAccessInfo &InterleaveInfo;

  /// Values to ignore in the cost model.
  SmallPtrSet<const Value *, 16> ValuesToIgnore;

  /// Values to ignore in the cost model when VF > 1.
  SmallPtrSet<const Value *, 16> VecValuesToIgnore;

  /// All element types found in the loop.
  SmallPtrSet<Type *, 16> ElementTypesInLoop;
};
} // end namespace llvm

namespace {
/// Helper struct to manage generating runtime checks for vectorization.
///
/// The runtime checks are created up-front in temporary blocks to allow better
/// estimating the cost and un-linked from the existing IR. After deciding to
/// vectorize, the checks are moved back. If deciding not to vectorize, the
/// temporary blocks are completely removed.
class GeneratedRTChecks {
  /// Basic block which contains the generated SCEV checks, if any.
  BasicBlock *SCEVCheckBlock = nullptr;

  /// The value representing the result of the generated SCEV checks. If it is
  /// nullptr, either no SCEV checks have been generated or they have been used.
  Value *SCEVCheckCond = nullptr;

  /// Basic block which contains the generated memory runtime checks, if any.
  BasicBlock *MemCheckBlock = nullptr;

  /// The value representing the result of the generated memory runtime checks.
  /// If it is nullptr, either no memory runtime checks have been generated or
  /// they have been used.
  Value *MemRuntimeCheckCond = nullptr;

  DominatorTree *DT;
  LoopInfo *LI;
  TargetTransformInfo *TTI;

  SCEVExpander SCEVExp;
  SCEVExpander MemCheckExp;

  bool CostTooHigh = false;
  const bool AddBranchWeights;

  Loop *OuterLoop = nullptr;

public:
  GeneratedRTChecks(ScalarEvolution &SE, DominatorTree *DT, LoopInfo *LI,
                    TargetTransformInfo *TTI, const DataLayout &DL,
                    bool AddBranchWeights)
      : DT(DT), LI(LI), TTI(TTI), SCEVExp(SE, DL, "scev.check"),
        MemCheckExp(SE, DL, "scev.check"), AddBranchWeights(AddBranchWeights) {}

  /// Generate runtime checks in SCEVCheckBlock and MemCheckBlock, so we can
  /// accurately estimate the cost of the runtime checks. The blocks are
  /// un-linked from the IR and is added back during vector code generation. If
  /// there is no vector code generation, the check blocks are removed
  /// completely.
  void Create(Loop *L, const LoopAccessInfo &LAI,
              const SCEVPredicate &UnionPred, ElementCount VF, unsigned IC) {

    // Hard cutoff to limit compile-time increase in case a very large number of
    // runtime checks needs to be generated.
    // TODO: Skip cutoff if the loop is guaranteed to execute, e.g. due to
    // profile info.
    CostTooHigh =
        LAI.getNumRuntimePointerChecks() > VectorizeMemoryCheckThreshold;
    if (CostTooHigh)
      return;

    BasicBlock *LoopHeader = L->getHeader();
    BasicBlock *Preheader = L->getLoopPreheader();

    // Use SplitBlock to create blocks for SCEV & memory runtime checks to
    // ensure the blocks are properly added to LoopInfo & DominatorTree. Those
    // may be used by SCEVExpander. The blocks will be un-linked from their
    // predecessors and removed from LI & DT at the end of the function.
    if (!UnionPred.isAlwaysTrue()) {
      SCEVCheckBlock = SplitBlock(Preheader, Preheader->getTerminator(), DT, LI,
                                  nullptr, "vector.scevcheck");

      SCEVCheckCond = SCEVExp.expandCodeForPredicate(
          &UnionPred, SCEVCheckBlock->getTerminator());
    }

    const auto &RtPtrChecking = *LAI.getRuntimePointerChecking();
    if (RtPtrChecking.Need) {
      auto *Pred = SCEVCheckBlock ? SCEVCheckBlock : Preheader;
      MemCheckBlock = SplitBlock(Pred, Pred->getTerminator(), DT, LI, nullptr,
                                 "vector.memcheck");

      auto DiffChecks = RtPtrChecking.getDiffChecks();
      if (DiffChecks) {
        Value *RuntimeVF = nullptr;
        MemRuntimeCheckCond = addDiffRuntimeChecks(
            MemCheckBlock->getTerminator(), *DiffChecks, MemCheckExp,
            [VF, &RuntimeVF](IRBuilderBase &B, unsigned Bits) {
              if (!RuntimeVF)
                RuntimeVF = getRuntimeVF(B, B.getIntNTy(Bits), VF);
              return RuntimeVF;
            },
            IC);
      } else {
        MemRuntimeCheckCond = addRuntimeChecks(
            MemCheckBlock->getTerminator(), L, RtPtrChecking.getChecks(),
            MemCheckExp, VectorizerParams::HoistRuntimeChecks);
      }
      assert(MemRuntimeCheckCond &&
             "no RT checks generated although RtPtrChecking "
             "claimed checks are required");
    }

    if (!MemCheckBlock && !SCEVCheckBlock)
      return;

    // Unhook the temporary block with the checks, update various places
    // accordingly.
    if (SCEVCheckBlock)
      SCEVCheckBlock->replaceAllUsesWith(Preheader);
    if (MemCheckBlock)
      MemCheckBlock->replaceAllUsesWith(Preheader);

    if (SCEVCheckBlock) {
      SCEVCheckBlock->getTerminator()->moveBefore(Preheader->getTerminator());
      new UnreachableInst(Preheader->getContext(), SCEVCheckBlock);
      Preheader->getTerminator()->eraseFromParent();
    }
    if (MemCheckBlock) {
      MemCheckBlock->getTerminator()->moveBefore(Preheader->getTerminator());
      new UnreachableInst(Preheader->getContext(), MemCheckBlock);
      Preheader->getTerminator()->eraseFromParent();
    }

    DT->changeImmediateDominator(LoopHeader, Preheader);
    if (MemCheckBlock) {
      DT->eraseNode(MemCheckBlock);
      LI->removeBlock(MemCheckBlock);
    }
    if (SCEVCheckBlock) {
      DT->eraseNode(SCEVCheckBlock);
      LI->removeBlock(SCEVCheckBlock);
    }

    // Outer loop is used as part of the later cost calculations.
    OuterLoop = L->getParentLoop();
  }

  InstructionCost getCost() {
    if (SCEVCheckBlock || MemCheckBlock)
      LLVM_DEBUG(dbgs() << "Calculating cost of runtime checks:\n");

    if (CostTooHigh) {
      InstructionCost Cost;
      Cost.setInvalid();
      LLVM_DEBUG(dbgs() << "  number of checks exceeded threshold\n");
      return Cost;
    }

    InstructionCost RTCheckCost = 0;
    if (SCEVCheckBlock)
      for (Instruction &I : *SCEVCheckBlock) {
        if (SCEVCheckBlock->getTerminator() == &I)
          continue;
        InstructionCost C =
            TTI->getInstructionCost(&I, TTI::TCK_RecipThroughput);
        LLVM_DEBUG(dbgs() << "  " << C << "  for " << I << "\n");
        RTCheckCost += C;
      }
    if (MemCheckBlock) {
      InstructionCost MemCheckCost = 0;
      for (Instruction &I : *MemCheckBlock) {
        if (MemCheckBlock->getTerminator() == &I)
          continue;
        InstructionCost C =
            TTI->getInstructionCost(&I, TTI::TCK_RecipThroughput);
        LLVM_DEBUG(dbgs() << "  " << C << "  for " << I << "\n");
        MemCheckCost += C;
      }

      // If the runtime memory checks are being created inside an outer loop
      // we should find out if these checks are outer loop invariant. If so,
      // the checks will likely be hoisted out and so the effective cost will
      // reduce according to the outer loop trip count.
      if (OuterLoop) {
        ScalarEvolution *SE = MemCheckExp.getSE();
        // TODO: If profitable, we could refine this further by analysing every
        // individual memory check, since there could be a mixture of loop
        // variant and invariant checks that mean the final condition is
        // variant.
        const SCEV *Cond = SE->getSCEV(MemRuntimeCheckCond);
        if (SE->isLoopInvariant(Cond, OuterLoop)) {
          // It seems reasonable to assume that we can reduce the effective
          // cost of the checks even when we know nothing about the trip
          // count. Assume that the outer loop executes at least twice.
          unsigned BestTripCount = 2;

          // If exact trip count is known use that.
          if (unsigned SmallTC = SE->getSmallConstantTripCount(OuterLoop))
            BestTripCount = SmallTC;
          else if (LoopVectorizeWithBlockFrequency) {
            // Else use profile data if available.
            if (auto EstimatedTC = getLoopEstimatedTripCount(OuterLoop))
              BestTripCount = *EstimatedTC;
          }

          BestTripCount = std::max(BestTripCount, 1U);
          InstructionCost NewMemCheckCost = MemCheckCost / BestTripCount;

          // Let's ensure the cost is always at least 1.
          NewMemCheckCost = std::max(*NewMemCheckCost.getValue(),
                                     (InstructionCost::CostType)1);

          if (BestTripCount > 1)
            LLVM_DEBUG(dbgs()
                       << "We expect runtime memory checks to be hoisted "
                       << "out of the outer loop. Cost reduced from "
                       << MemCheckCost << " to " << NewMemCheckCost << '\n');

          MemCheckCost = NewMemCheckCost;
        }
      }

      RTCheckCost += MemCheckCost;
    }

    if (SCEVCheckBlock || MemCheckBlock)
      LLVM_DEBUG(dbgs() << "Total cost of runtime checks: " << RTCheckCost
                        << "\n");

    return RTCheckCost;
  }

  /// Remove the created SCEV & memory runtime check blocks & instructions, if
  /// unused.
  ~GeneratedRTChecks() {
    SCEVExpanderCleaner SCEVCleaner(SCEVExp);
    SCEVExpanderCleaner MemCheckCleaner(MemCheckExp);
    if (!SCEVCheckCond)
      SCEVCleaner.markResultUsed();

    if (!MemRuntimeCheckCond)
      MemCheckCleaner.markResultUsed();

    if (MemRuntimeCheckCond) {
      auto &SE = *MemCheckExp.getSE();
      // Memory runtime check generation creates compares that use expanded
      // values. Remove them before running the SCEVExpanderCleaners.
      for (auto &I : make_early_inc_range(reverse(*MemCheckBlock))) {
        if (MemCheckExp.isInsertedInstruction(&I))
          continue;
        SE.forgetValue(&I);
        I.eraseFromParent();
      }
    }
    MemCheckCleaner.cleanup();
    SCEVCleaner.cleanup();

    if (SCEVCheckCond)
      SCEVCheckBlock->eraseFromParent();
    if (MemRuntimeCheckCond)
      MemCheckBlock->eraseFromParent();
  }

  /// Adds the generated SCEVCheckBlock before \p LoopVectorPreHeader and
  /// adjusts the branches to branch to the vector preheader or \p Bypass,
  /// depending on the generated condition.
  BasicBlock *emitSCEVChecks(BasicBlock *Bypass,
                             BasicBlock *LoopVectorPreHeader,
                             BasicBlock *LoopExitBlock) {
    if (!SCEVCheckCond)
      return nullptr;

    Value *Cond = SCEVCheckCond;
    // Mark the check as used, to prevent it from being removed during cleanup.
    SCEVCheckCond = nullptr;
    if (auto *C = dyn_cast<ConstantInt>(Cond))
      if (C->isZero())
        return nullptr;

    auto *Pred = LoopVectorPreHeader->getSinglePredecessor();

    BranchInst::Create(LoopVectorPreHeader, SCEVCheckBlock);
    // Create new preheader for vector loop.
    if (OuterLoop)
      OuterLoop->addBasicBlockToLoop(SCEVCheckBlock, *LI);

    SCEVCheckBlock->getTerminator()->eraseFromParent();
    SCEVCheckBlock->moveBefore(LoopVectorPreHeader);
    Pred->getTerminator()->replaceSuccessorWith(LoopVectorPreHeader,
                                                SCEVCheckBlock);

    DT->addNewBlock(SCEVCheckBlock, Pred);
    DT->changeImmediateDominator(LoopVectorPreHeader, SCEVCheckBlock);

    BranchInst &BI = *BranchInst::Create(Bypass, LoopVectorPreHeader, Cond);
    if (AddBranchWeights)
      setBranchWeights(BI, SCEVCheckBypassWeights, /*IsExpected=*/false);
    ReplaceInstWithInst(SCEVCheckBlock->getTerminator(), &BI);
    return SCEVCheckBlock;
  }

  /// Adds the generated MemCheckBlock before \p LoopVectorPreHeader and adjusts
  /// the branches to branch to the vector preheader or \p Bypass, depending on
  /// the generated condition.
  BasicBlock *emitMemRuntimeChecks(BasicBlock *Bypass,
                                   BasicBlock *LoopVectorPreHeader) {
    // Check if we generated code that checks in runtime if arrays overlap.
    if (!MemRuntimeCheckCond)
      return nullptr;

    auto *Pred = LoopVectorPreHeader->getSinglePredecessor();
    Pred->getTerminator()->replaceSuccessorWith(LoopVectorPreHeader,
                                                MemCheckBlock);

    DT->addNewBlock(MemCheckBlock, Pred);
    DT->changeImmediateDominator(LoopVectorPreHeader, MemCheckBlock);
    MemCheckBlock->moveBefore(LoopVectorPreHeader);

    if (OuterLoop)
      OuterLoop->addBasicBlockToLoop(MemCheckBlock, *LI);

    BranchInst &BI =
        *BranchInst::Create(Bypass, LoopVectorPreHeader, MemRuntimeCheckCond);
    if (AddBranchWeights) {
      setBranchWeights(BI, MemCheckBypassWeights, /*IsExpected=*/false);
    }
    ReplaceInstWithInst(MemCheckBlock->getTerminator(), &BI);
    MemCheckBlock->getTerminator()->setDebugLoc(
        Pred->getTerminator()->getDebugLoc());

    // Mark the check as used, to prevent it from being removed during cleanup.
    MemRuntimeCheckCond = nullptr;
    return MemCheckBlock;
  }
};
} // namespace

static bool useActiveLaneMask(TailFoldingStyle Style) {
  return Style == TailFoldingStyle::Data ||
         Style == TailFoldingStyle::DataAndControlFlow ||
         Style == TailFoldingStyle::DataAndControlFlowWithoutRuntimeCheck;
}

static bool useActiveLaneMaskForControlFlow(TailFoldingStyle Style) {
  return Style == TailFoldingStyle::DataAndControlFlow ||
         Style == TailFoldingStyle::DataAndControlFlowWithoutRuntimeCheck;
}

// Return true if \p OuterLp is an outer loop annotated with hints for explicit
// vectorization. The loop needs to be annotated with #pragma omp simd
// simdlen(#) or #pragma clang vectorize(enable) vectorize_width(#). If the
// vector length information is not provided, vectorization is not considered
// explicit. Interleave hints are not allowed either. These limitations will be
// relaxed in the future.
// Please, note that we are currently forced to abuse the pragma 'clang
// vectorize' semantics. This pragma provides *auto-vectorization hints*
// (i.e., LV must check that vectorization is legal) whereas pragma 'omp simd'
// provides *explicit vectorization hints* (LV can bypass legal checks and
// assume that vectorization is legal). However, both hints are implemented
// using the same metadata (llvm.loop.vectorize, processed by
// LoopVectorizeHints). This will be fixed in the future when the native IR
// representation for pragma 'omp simd' is introduced.
static bool isExplicitVecOuterLoop(Loop *OuterLp,
                                   OptimizationRemarkEmitter *ORE) {
  assert(!OuterLp->isInnermost() && "This is not an outer loop");
  LoopVectorizeHints Hints(OuterLp, true /*DisableInterleaving*/, *ORE);

  // Only outer loops with an explicit vectorization hint are supported.
  // Unannotated outer loops are ignored.
  if (Hints.getForce() == LoopVectorizeHints::FK_Undefined)
    return false;

  Function *Fn = OuterLp->getHeader()->getParent();
  if (!Hints.allowVectorization(Fn, OuterLp,
                                true /*VectorizeOnlyWhenForced*/)) {
    LLVM_DEBUG(dbgs() << "LV: Loop hints prevent outer loop vectorization.\n");
    return false;
  }

  if (Hints.getInterleave() > 1) {
    // TODO: Interleave support is future work.
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: Interleave is not supported for "
                         "outer loops.\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  return true;
}

static void collectSupportedLoops(Loop &L, LoopInfo *LI,
                                  OptimizationRemarkEmitter *ORE,
                                  SmallVectorImpl<Loop *> &V) {
  // Collect inner loops and outer loops without irreducible control flow. For
  // now, only collect outer loops that have explicit vectorization hints. If we
  // are stress testing the VPlan H-CFG construction, we collect the outermost
  // loop of every loop nest.
  if (L.isInnermost() || VPlanBuildStressTest ||
      (EnableVPlanNativePath && isExplicitVecOuterLoop(&L, ORE))) {
    LoopBlocksRPO RPOT(&L);
    RPOT.perform(LI);
    if (!containsIrreducibleCFG<const BasicBlock *>(RPOT, *LI)) {
      V.push_back(&L);
      // TODO: Collect inner loops inside marked outer loops in case
      // vectorization fails for the outer loop. Do not invoke
      // 'containsIrreducibleCFG' again for inner loops when the outer loop is
      // already known to be reducible. We can use an inherited attribute for
      // that.
      return;
    }
  }
  for (Loop *InnerL : L)
    collectSupportedLoops(*InnerL, LI, ORE, V);
}

//===----------------------------------------------------------------------===//
// Implementation of LoopVectorizationLegality, InnerLoopVectorizer and
// LoopVectorizationCostModel and LoopVectorizationPlanner.
//===----------------------------------------------------------------------===//

/// Compute the transformed value of Index at offset StartValue using step
/// StepValue.
/// For integer induction, returns StartValue + Index * StepValue.
/// For pointer induction, returns StartValue[Index * StepValue].
/// FIXME: The newly created binary instructions should contain nsw/nuw
/// flags, which can be found from the original scalar operations.
static Value *
emitTransformedIndex(IRBuilderBase &B, Value *Index, Value *StartValue,
                     Value *Step,
                     InductionDescriptor::InductionKind InductionKind,
                     const BinaryOperator *InductionBinOp) {
  Type *StepTy = Step->getType();
  Value *CastedIndex = StepTy->isIntegerTy()
                           ? B.CreateSExtOrTrunc(Index, StepTy)
                           : B.CreateCast(Instruction::SIToFP, Index, StepTy);
  if (CastedIndex != Index) {
    CastedIndex->setName(CastedIndex->getName() + ".cast");
    Index = CastedIndex;
  }

  // Note: the IR at this point is broken. We cannot use SE to create any new
  // SCEV and then expand it, hoping that SCEV's simplification will give us
  // a more optimal code. Unfortunately, attempt of doing so on invalid IR may
  // lead to various SCEV crashes. So all we can do is to use builder and rely
  // on InstCombine for future simplifications. Here we handle some trivial
  // cases only.
  auto CreateAdd = [&B](Value *X, Value *Y) {
    assert(X->getType() == Y->getType() && "Types don't match!");
    if (auto *CX = dyn_cast<ConstantInt>(X))
      if (CX->isZero())
        return Y;
    if (auto *CY = dyn_cast<ConstantInt>(Y))
      if (CY->isZero())
        return X;
    return B.CreateAdd(X, Y);
  };

  // We allow X to be a vector type, in which case Y will potentially be
  // splatted into a vector with the same element count.
  auto CreateMul = [&B](Value *X, Value *Y) {
    assert(X->getType()->getScalarType() == Y->getType() &&
           "Types don't match!");
    if (auto *CX = dyn_cast<ConstantInt>(X))
      if (CX->isOne())
        return Y;
    if (auto *CY = dyn_cast<ConstantInt>(Y))
      if (CY->isOne())
        return X;
    VectorType *XVTy = dyn_cast<VectorType>(X->getType());
    if (XVTy && !isa<VectorType>(Y->getType()))
      Y = B.CreateVectorSplat(XVTy->getElementCount(), Y);
    return B.CreateMul(X, Y);
  };

  switch (InductionKind) {
  case InductionDescriptor::IK_IntInduction: {
    assert(!isa<VectorType>(Index->getType()) &&
           "Vector indices not supported for integer inductions yet");
    assert(Index->getType() == StartValue->getType() &&
           "Index type does not match StartValue type");
    if (isa<ConstantInt>(Step) && cast<ConstantInt>(Step)->isMinusOne())
      return B.CreateSub(StartValue, Index);
    auto *Offset = CreateMul(Index, Step);
    return CreateAdd(StartValue, Offset);
  }
  case InductionDescriptor::IK_PtrInduction:
    return B.CreatePtrAdd(StartValue, CreateMul(Index, Step));
  case InductionDescriptor::IK_FpInduction: {
    assert(!isa<VectorType>(Index->getType()) &&
           "Vector indices not supported for FP inductions yet");
    assert(Step->getType()->isFloatingPointTy() && "Expected FP Step value");
    assert(InductionBinOp &&
           (InductionBinOp->getOpcode() == Instruction::FAdd ||
            InductionBinOp->getOpcode() == Instruction::FSub) &&
           "Original bin op should be defined for FP induction");

    Value *MulExp = B.CreateFMul(Step, Index);
    return B.CreateBinOp(InductionBinOp->getOpcode(), StartValue, MulExp,
                         "induction");
  }
  case InductionDescriptor::IK_NoInduction:
    return nullptr;
  }
  llvm_unreachable("invalid enum");
}

std::optional<unsigned> getMaxVScale(const Function &F,
                                     const TargetTransformInfo &TTI) {
  if (std::optional<unsigned> MaxVScale = TTI.getMaxVScale())
    return MaxVScale;

  if (F.hasFnAttribute(Attribute::VScaleRange))
    return F.getFnAttribute(Attribute::VScaleRange).getVScaleRangeMax();

  return std::nullopt;
}

/// For the given VF and UF and maximum trip count computed for the loop, return
/// whether the induction variable might overflow in the vectorized loop. If not,
/// then we know a runtime overflow check always evaluates to false and can be
/// removed.
static bool isIndvarOverflowCheckKnownFalse(
    const LoopVectorizationCostModel *Cost,
    ElementCount VF, std::optional<unsigned> UF = std::nullopt) {
  // Always be conservative if we don't know the exact unroll factor.
  unsigned MaxUF = UF ? *UF : Cost->TTI.getMaxInterleaveFactor(VF);

  Type *IdxTy = Cost->Legal->getWidestInductionType();
  APInt MaxUIntTripCount = cast<IntegerType>(IdxTy)->getMask();

  // We know the runtime overflow check is known false iff the (max) trip-count
  // is known and (max) trip-count + (VF * UF) does not overflow in the type of
  // the vector loop induction variable.
  if (unsigned TC =
          Cost->PSE.getSE()->getSmallConstantMaxTripCount(Cost->TheLoop)) {
    uint64_t MaxVF = VF.getKnownMinValue();
    if (VF.isScalable()) {
      std::optional<unsigned> MaxVScale =
          getMaxVScale(*Cost->TheFunction, Cost->TTI);
      if (!MaxVScale)
        return false;
      MaxVF *= *MaxVScale;
    }

    return (MaxUIntTripCount - TC).ugt(MaxVF * MaxUF);
  }

  return false;
}

// Return whether we allow using masked interleave-groups (for dealing with
// strided loads/stores that reside in predicated blocks, or for dealing
// with gaps).
static bool useMaskedInterleavedAccesses(const TargetTransformInfo &TTI) {
  // If an override option has been passed in for interleaved accesses, use it.
  if (EnableMaskedInterleavedMemAccesses.getNumOccurrences() > 0)
    return EnableMaskedInterleavedMemAccesses;

  return TTI.enableMaskedInterleavedAccessVectorization();
}

void InnerLoopVectorizer::scalarizeInstruction(const Instruction *Instr,
                                               VPReplicateRecipe *RepRecipe,
                                               const VPIteration &Instance,
                                               VPTransformState &State) {
  assert(!Instr->getType()->isAggregateType() && "Can't handle vectors");

  // llvm.experimental.noalias.scope.decl intrinsics must only be duplicated for
  // the first lane and part.
  if (isa<NoAliasScopeDeclInst>(Instr))
    if (!Instance.isFirstIteration())
      return;

  // Does this instruction return a value ?
  bool IsVoidRetTy = Instr->getType()->isVoidTy();

  Instruction *Cloned = Instr->clone();
  if (!IsVoidRetTy) {
    Cloned->setName(Instr->getName() + ".cloned");
#if !defined(NDEBUG)
    // Verify that VPlan type inference results agree with the type of the
    // generated values.
    assert(State.TypeAnalysis.inferScalarType(RepRecipe) == Cloned->getType() &&
           "inferred type and type from generated instructions do not match");
#endif
  }

  RepRecipe->setFlags(Cloned);

  if (auto DL = Instr->getDebugLoc())
    State.setDebugLocFrom(DL);

  // Replace the operands of the cloned instructions with their scalar
  // equivalents in the new loop.
  for (const auto &I : enumerate(RepRecipe->operands())) {
    auto InputInstance = Instance;
    VPValue *Operand = I.value();
    if (vputils::isUniformAfterVectorization(Operand))
      InputInstance.Lane = VPLane::getFirstLane();
    Cloned->setOperand(I.index(), State.get(Operand, InputInstance));
  }
  State.addNewMetadata(Cloned, Instr);

  // Place the cloned scalar in the new loop.
  State.Builder.Insert(Cloned);

  State.set(RepRecipe, Cloned, Instance);

  // If we just cloned a new assumption, add it the assumption cache.
  if (auto *II = dyn_cast<AssumeInst>(Cloned))
    AC->registerAssumption(II);

  // End if-block.
  bool IfPredicateInstr = RepRecipe->getParent()->getParent()->isReplicator();
  if (IfPredicateInstr)
    PredicatedInstructions.push_back(Cloned);
}

Value *
InnerLoopVectorizer::getOrCreateVectorTripCount(BasicBlock *InsertBlock) {
  if (VectorTripCount)
    return VectorTripCount;

  Value *TC = getTripCount();
  IRBuilder<> Builder(InsertBlock->getTerminator());

  Type *Ty = TC->getType();
  // This is where we can make the step a runtime constant.
  Value *Step = createStepForVF(Builder, Ty, VF, UF);

  // If the tail is to be folded by masking, round the number of iterations N
  // up to a multiple of Step instead of rounding down. This is done by first
  // adding Step-1 and then rounding down. Note that it's ok if this addition
  // overflows: the vector induction variable will eventually wrap to zero given
  // that it starts at zero and its Step is a power of two; the loop will then
  // exit, with the last early-exit vector comparison also producing all-true.
  // For scalable vectors the VF is not guaranteed to be a power of 2, but this
  // is accounted for in emitIterationCountCheck that adds an overflow check.
  if (Cost->foldTailByMasking()) {
    assert(isPowerOf2_32(VF.getKnownMinValue() * UF) &&
           "VF*UF must be a power of 2 when folding tail by masking");
    TC = Builder.CreateAdd(TC, Builder.CreateSub(Step, ConstantInt::get(Ty, 1)),
                           "n.rnd.up");
  }

  // Now we need to generate the expression for the part of the loop that the
  // vectorized body will execute. This is equal to N - (N % Step) if scalar
  // iterations are not required for correctness, or N - Step, otherwise. Step
  // is equal to the vectorization factor (number of SIMD elements) times the
  // unroll factor (number of SIMD instructions).
  Value *R = Builder.CreateURem(TC, Step, "n.mod.vf");

  // There are cases where we *must* run at least one iteration in the remainder
  // loop.  See the cost model for when this can happen.  If the step evenly
  // divides the trip count, we set the remainder to be equal to the step. If
  // the step does not evenly divide the trip count, no adjustment is necessary
  // since there will already be scalar iterations. Note that the minimum
  // iterations check ensures that N >= Step.
  if (Cost->requiresScalarEpilogue(VF.isVector())) {
    auto *IsZero = Builder.CreateICmpEQ(R, ConstantInt::get(R->getType(), 0));
    R = Builder.CreateSelect(IsZero, Step, R);
  }

  VectorTripCount = Builder.CreateSub(TC, R, "n.vec");

  return VectorTripCount;
}

void InnerLoopVectorizer::emitIterationCountCheck(BasicBlock *Bypass) {
  Value *Count = getTripCount();
  // Reuse existing vector loop preheader for TC checks.
  // Note that new preheader block is generated for vector loop.
  BasicBlock *const TCCheckBlock = LoopVectorPreHeader;
  IRBuilder<> Builder(TCCheckBlock->getTerminator());

  // Generate code to check if the loop's trip count is less than VF * UF, or
  // equal to it in case a scalar epilogue is required; this implies that the
  // vector trip count is zero. This check also covers the case where adding one
  // to the backedge-taken count overflowed leading to an incorrect trip count
  // of zero. In this case we will also jump to the scalar loop.
  auto P = Cost->requiresScalarEpilogue(VF.isVector()) ? ICmpInst::ICMP_ULE
                                                       : ICmpInst::ICMP_ULT;

  // If tail is to be folded, vector loop takes care of all iterations.
  Type *CountTy = Count->getType();
  Value *CheckMinIters = Builder.getFalse();
  auto CreateStep = [&]() -> Value * {
    // Create step with max(MinProTripCount, UF * VF).
    if (UF * VF.getKnownMinValue() >= MinProfitableTripCount.getKnownMinValue())
      return createStepForVF(Builder, CountTy, VF, UF);

    Value *MinProfTC =
        createStepForVF(Builder, CountTy, MinProfitableTripCount, 1);
    if (!VF.isScalable())
      return MinProfTC;
    return Builder.CreateBinaryIntrinsic(
        Intrinsic::umax, MinProfTC, createStepForVF(Builder, CountTy, VF, UF));
  };

  TailFoldingStyle Style = Cost->getTailFoldingStyle();
  if (Style == TailFoldingStyle::None)
    CheckMinIters =
        Builder.CreateICmp(P, Count, CreateStep(), "min.iters.check");
  else if (VF.isScalable() &&
           !isIndvarOverflowCheckKnownFalse(Cost, VF, UF) &&
           Style != TailFoldingStyle::DataAndControlFlowWithoutRuntimeCheck) {
    // vscale is not necessarily a power-of-2, which means we cannot guarantee
    // an overflow to zero when updating induction variables and so an
    // additional overflow check is required before entering the vector loop.

    // Get the maximum unsigned value for the type.
    Value *MaxUIntTripCount =
        ConstantInt::get(CountTy, cast<IntegerType>(CountTy)->getMask());
    Value *LHS = Builder.CreateSub(MaxUIntTripCount, Count);

    // Don't execute the vector loop if (UMax - n) < (VF * UF).
    CheckMinIters = Builder.CreateICmp(ICmpInst::ICMP_ULT, LHS, CreateStep());
  }

  // Create new preheader for vector loop.
  LoopVectorPreHeader =
      SplitBlock(TCCheckBlock, TCCheckBlock->getTerminator(), DT, LI, nullptr,
                 "vector.ph");

  assert(DT->properlyDominates(DT->getNode(TCCheckBlock),
                               DT->getNode(Bypass)->getIDom()) &&
         "TC check is expected to dominate Bypass");

  // Update dominator for Bypass & LoopExit (if needed).
  DT->changeImmediateDominator(Bypass, TCCheckBlock);
  BranchInst &BI =
      *BranchInst::Create(Bypass, LoopVectorPreHeader, CheckMinIters);
  if (hasBranchWeightMD(*OrigLoop->getLoopLatch()->getTerminator()))
    setBranchWeights(BI, MinItersBypassWeights, /*IsExpected=*/false);
  ReplaceInstWithInst(TCCheckBlock->getTerminator(), &BI);
  LoopBypassBlocks.push_back(TCCheckBlock);
}

BasicBlock *InnerLoopVectorizer::emitSCEVChecks(BasicBlock *Bypass) {
  BasicBlock *const SCEVCheckBlock =
      RTChecks.emitSCEVChecks(Bypass, LoopVectorPreHeader, LoopExitBlock);
  if (!SCEVCheckBlock)
    return nullptr;

  assert(!(SCEVCheckBlock->getParent()->hasOptSize() ||
           (OptForSizeBasedOnProfile &&
            Cost->Hints->getForce() != LoopVectorizeHints::FK_Enabled)) &&
         "Cannot SCEV check stride or overflow when optimizing for size");


  // Update dominator only if this is first RT check.
  if (LoopBypassBlocks.empty()) {
    DT->changeImmediateDominator(Bypass, SCEVCheckBlock);
    if (!Cost->requiresScalarEpilogue(VF.isVector()))
      // If there is an epilogue which must run, there's no edge from the
      // middle block to exit blocks  and thus no need to update the immediate
      // dominator of the exit blocks.
      DT->changeImmediateDominator(LoopExitBlock, SCEVCheckBlock);
  }

  LoopBypassBlocks.push_back(SCEVCheckBlock);
  AddedSafetyChecks = true;
  return SCEVCheckBlock;
}

BasicBlock *InnerLoopVectorizer::emitMemRuntimeChecks(BasicBlock *Bypass) {
  // VPlan-native path does not do any analysis for runtime checks currently.
  if (EnableVPlanNativePath)
    return nullptr;

  BasicBlock *const MemCheckBlock =
      RTChecks.emitMemRuntimeChecks(Bypass, LoopVectorPreHeader);

  // Check if we generated code that checks in runtime if arrays overlap. We put
  // the checks into a separate block to make the more common case of few
  // elements faster.
  if (!MemCheckBlock)
    return nullptr;

  if (MemCheckBlock->getParent()->hasOptSize() || OptForSizeBasedOnProfile) {
    assert(Cost->Hints->getForce() == LoopVectorizeHints::FK_Enabled &&
           "Cannot emit memory checks when optimizing for size, unless forced "
           "to vectorize.");
    ORE->emit([&]() {
      return OptimizationRemarkAnalysis(DEBUG_TYPE, "VectorizationCodeSize",
                                        OrigLoop->getStartLoc(),
                                        OrigLoop->getHeader())
             << "Code-size may be reduced by not forcing "
                "vectorization, or by source-code modifications "
                "eliminating the need for runtime checks "
                "(e.g., adding 'restrict').";
    });
  }

  LoopBypassBlocks.push_back(MemCheckBlock);

  AddedSafetyChecks = true;

  return MemCheckBlock;
}

void InnerLoopVectorizer::createVectorLoopSkeleton(StringRef Prefix) {
  LoopScalarBody = OrigLoop->getHeader();
  LoopVectorPreHeader = OrigLoop->getLoopPreheader();
  assert(LoopVectorPreHeader && "Invalid loop structure");
  LoopExitBlock = OrigLoop->getUniqueExitBlock(); // may be nullptr
  assert((LoopExitBlock || Cost->requiresScalarEpilogue(VF.isVector())) &&
         "multiple exit loop without required epilogue?");

  LoopMiddleBlock =
      SplitBlock(LoopVectorPreHeader, LoopVectorPreHeader->getTerminator(), DT,
                 LI, nullptr, Twine(Prefix) + "middle.block");
  LoopScalarPreHeader =
      SplitBlock(LoopMiddleBlock, LoopMiddleBlock->getTerminator(), DT, LI,
                 nullptr, Twine(Prefix) + "scalar.ph");
}

PHINode *InnerLoopVectorizer::createInductionResumeValue(
    PHINode *OrigPhi, const InductionDescriptor &II, Value *Step,
    ArrayRef<BasicBlock *> BypassBlocks,
    std::pair<BasicBlock *, Value *> AdditionalBypass) {
  Value *VectorTripCount = getOrCreateVectorTripCount(LoopVectorPreHeader);
  assert(VectorTripCount && "Expected valid arguments");

  Instruction *OldInduction = Legal->getPrimaryInduction();
  Value *&EndValue = IVEndValues[OrigPhi];
  Value *EndValueFromAdditionalBypass = AdditionalBypass.second;
  if (OrigPhi == OldInduction) {
    // We know what the end value is.
    EndValue = VectorTripCount;
  } else {
    IRBuilder<> B(LoopVectorPreHeader->getTerminator());

    // Fast-math-flags propagate from the original induction instruction.
    if (II.getInductionBinOp() && isa<FPMathOperator>(II.getInductionBinOp()))
      B.setFastMathFlags(II.getInductionBinOp()->getFastMathFlags());

    EndValue = emitTransformedIndex(B, VectorTripCount, II.getStartValue(),
                                    Step, II.getKind(), II.getInductionBinOp());
    EndValue->setName("ind.end");

    // Compute the end value for the additional bypass (if applicable).
    if (AdditionalBypass.first) {
      B.SetInsertPoint(AdditionalBypass.first,
                       AdditionalBypass.first->getFirstInsertionPt());
      EndValueFromAdditionalBypass =
          emitTransformedIndex(B, AdditionalBypass.second, II.getStartValue(),
                               Step, II.getKind(), II.getInductionBinOp());
      EndValueFromAdditionalBypass->setName("ind.end");
    }
  }

  // Create phi nodes to merge from the  backedge-taken check block.
  PHINode *BCResumeVal = PHINode::Create(OrigPhi->getType(), 3, "bc.resume.val",
                                         LoopScalarPreHeader->getFirstNonPHI());
  // Copy original phi DL over to the new one.
  BCResumeVal->setDebugLoc(OrigPhi->getDebugLoc());

  // The new PHI merges the original incoming value, in case of a bypass,
  // or the value at the end of the vectorized loop.
  BCResumeVal->addIncoming(EndValue, LoopMiddleBlock);

  // Fix the scalar body counter (PHI node).
  // The old induction's phi node in the scalar body needs the truncated
  // value.
  for (BasicBlock *BB : BypassBlocks)
    BCResumeVal->addIncoming(II.getStartValue(), BB);

  if (AdditionalBypass.first)
    BCResumeVal->setIncomingValueForBlock(AdditionalBypass.first,
                                          EndValueFromAdditionalBypass);
  return BCResumeVal;
}

/// Return the expanded step for \p ID using \p ExpandedSCEVs to look up SCEV
/// expansion results.
static Value *getExpandedStep(const InductionDescriptor &ID,
                              const SCEV2ValueTy &ExpandedSCEVs) {
  const SCEV *Step = ID.getStep();
  if (auto *C = dyn_cast<SCEVConstant>(Step))
    return C->getValue();
  if (auto *U = dyn_cast<SCEVUnknown>(Step))
    return U->getValue();
  auto I = ExpandedSCEVs.find(Step);
  assert(I != ExpandedSCEVs.end() && "SCEV must be expanded at this point");
  return I->second;
}

void InnerLoopVectorizer::createInductionResumeValues(
    const SCEV2ValueTy &ExpandedSCEVs,
    std::pair<BasicBlock *, Value *> AdditionalBypass) {
  assert(((AdditionalBypass.first && AdditionalBypass.second) ||
          (!AdditionalBypass.first && !AdditionalBypass.second)) &&
         "Inconsistent information about additional bypass.");
  // We are going to resume the execution of the scalar loop.
  // Go over all of the induction variables that we found and fix the
  // PHIs that are left in the scalar version of the loop.
  // The starting values of PHI nodes depend on the counter of the last
  // iteration in the vectorized loop.
  // If we come from a bypass edge then we need to start from the original
  // start value.
  for (const auto &InductionEntry : Legal->getInductionVars()) {
    PHINode *OrigPhi = InductionEntry.first;
    const InductionDescriptor &II = InductionEntry.second;
    PHINode *BCResumeVal = createInductionResumeValue(
        OrigPhi, II, getExpandedStep(II, ExpandedSCEVs), LoopBypassBlocks,
        AdditionalBypass);
    OrigPhi->setIncomingValueForBlock(LoopScalarPreHeader, BCResumeVal);
  }
}

std::pair<BasicBlock *, Value *>
InnerLoopVectorizer::createVectorizedLoopSkeleton(
    const SCEV2ValueTy &ExpandedSCEVs) {
  /*
   In this function we generate a new loop. The new loop will contain
   the vectorized instructions while the old loop will continue to run the
   scalar remainder.

       [ ] <-- old preheader - loop iteration number check and SCEVs in Plan's
     /  |      preheader are expanded here. Eventually all required SCEV
    /   |      expansion should happen here.
   /    v
  |    [ ] <-- vector loop bypass (may consist of multiple blocks).
  |  /  |
  | /   v
  ||   [ ]     <-- vector pre header.
  |/    |
  |     v
  |    [  ] \
  |    [  ]_|   <-- vector loop (created during VPlan execution).
  |     |
  |     v
  \   -[ ]   <--- middle-block (wrapped in VPIRBasicBlock with the branch to
   |    |                       successors created during VPlan execution)
   \/   |
   /\   v
   | ->[ ]     <--- new preheader (wrapped in VPIRBasicBlock).
   |    |
 (opt)  v      <-- edge from middle to exit iff epilogue is not required.
   |   [ ] \
   |   [ ]_|   <-- old scalar loop to handle remainder (scalar epilogue).
    \   |
     \  v
      >[ ]     <-- exit block(s). (wrapped in VPIRBasicBlock)
   ...
   */

  // Create an empty vector loop, and prepare basic blocks for the runtime
  // checks.
  createVectorLoopSkeleton("");

  // Now, compare the new count to zero. If it is zero skip the vector loop and
  // jump to the scalar loop. This check also covers the case where the
  // backedge-taken count is uint##_max: adding one to it will overflow leading
  // to an incorrect trip count of zero. In this (rare) case we will also jump
  // to the scalar loop.
  emitIterationCountCheck(LoopScalarPreHeader);

  // Generate the code to check any assumptions that we've made for SCEV
  // expressions.
  emitSCEVChecks(LoopScalarPreHeader);

  // Generate the code that checks in runtime if arrays overlap. We put the
  // checks into a separate block to make the more common case of few elements
  // faster.
  emitMemRuntimeChecks(LoopScalarPreHeader);

  // Emit phis for the new starting index of the scalar loop.
  createInductionResumeValues(ExpandedSCEVs);

  return {LoopVectorPreHeader, nullptr};
}

// Fix up external users of the induction variable. At this point, we are
// in LCSSA form, with all external PHIs that use the IV having one input value,
// coming from the remainder loop. We need those PHIs to also have a correct
// value for the IV when arriving directly from the middle block.
void InnerLoopVectorizer::fixupIVUsers(PHINode *OrigPhi,
                                       const InductionDescriptor &II,
                                       Value *VectorTripCount, Value *EndValue,
                                       BasicBlock *MiddleBlock,
                                       BasicBlock *VectorHeader, VPlan &Plan,
                                       VPTransformState &State) {
  // There are two kinds of external IV usages - those that use the value
  // computed in the last iteration (the PHI) and those that use the penultimate
  // value (the value that feeds into the phi from the loop latch).
  // We allow both, but they, obviously, have different values.

  assert(OrigLoop->getUniqueExitBlock() && "Expected a single exit block");

  DenseMap<Value *, Value *> MissingVals;

  // An external user of the last iteration's value should see the value that
  // the remainder loop uses to initialize its own IV.
  Value *PostInc = OrigPhi->getIncomingValueForBlock(OrigLoop->getLoopLatch());
  for (User *U : PostInc->users()) {
    Instruction *UI = cast<Instruction>(U);
    if (!OrigLoop->contains(UI)) {
      assert(isa<PHINode>(UI) && "Expected LCSSA form");
      MissingVals[UI] = EndValue;
    }
  }

  // An external user of the penultimate value need to see EndValue - Step.
  // The simplest way to get this is to recompute it from the constituent SCEVs,
  // that is Start + (Step * (CRD - 1)).
  for (User *U : OrigPhi->users()) {
    auto *UI = cast<Instruction>(U);
    if (!OrigLoop->contains(UI)) {
      assert(isa<PHINode>(UI) && "Expected LCSSA form");
      IRBuilder<> B(MiddleBlock->getTerminator());

      // Fast-math-flags propagate from the original induction instruction.
      if (II.getInductionBinOp() && isa<FPMathOperator>(II.getInductionBinOp()))
        B.setFastMathFlags(II.getInductionBinOp()->getFastMathFlags());

      Value *CountMinusOne = B.CreateSub(
          VectorTripCount, ConstantInt::get(VectorTripCount->getType(), 1));
      CountMinusOne->setName("cmo");

      VPValue *StepVPV = Plan.getSCEVExpansion(II.getStep());
      assert(StepVPV && "step must have been expanded during VPlan execution");
      Value *Step = StepVPV->isLiveIn() ? StepVPV->getLiveInIRValue()
                                        : State.get(StepVPV, {0, 0});
      Value *Escape =
          emitTransformedIndex(B, CountMinusOne, II.getStartValue(), Step,
                               II.getKind(), II.getInductionBinOp());
      Escape->setName("ind.escape");
      MissingVals[UI] = Escape;
    }
  }

  for (auto &I : MissingVals) {
    PHINode *PHI = cast<PHINode>(I.first);
    // One corner case we have to handle is two IVs "chasing" each-other,
    // that is %IV2 = phi [...], [ %IV1, %latch ]
    // In this case, if IV1 has an external use, we need to avoid adding both
    // "last value of IV1" and "penultimate value of IV2". So, verify that we
    // don't already have an incoming value for the middle block.
    if (PHI->getBasicBlockIndex(MiddleBlock) == -1) {
      PHI->addIncoming(I.second, MiddleBlock);
      Plan.removeLiveOut(PHI);
    }
  }
}

namespace {

struct CSEDenseMapInfo {
  static bool canHandle(const Instruction *I) {
    return isa<InsertElementInst>(I) || isa<ExtractElementInst>(I) ||
           isa<ShuffleVectorInst>(I) || isa<GetElementPtrInst>(I);
  }

  static inline Instruction *getEmptyKey() {
    return DenseMapInfo<Instruction *>::getEmptyKey();
  }

  static inline Instruction *getTombstoneKey() {
    return DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Instruction *I) {
    assert(canHandle(I) && "Unknown instruction!");
    return hash_combine(I->getOpcode(), hash_combine_range(I->value_op_begin(),
                                                           I->value_op_end()));
  }

  static bool isEqual(const Instruction *LHS, const Instruction *RHS) {
    if (LHS == getEmptyKey() || RHS == getEmptyKey() ||
        LHS == getTombstoneKey() || RHS == getTombstoneKey())
      return LHS == RHS;
    return LHS->isIdenticalTo(RHS);
  }
};

} // end anonymous namespace

///Perform cse of induction variable instructions.
static void cse(BasicBlock *BB) {
  // Perform simple cse.
  SmallDenseMap<Instruction *, Instruction *, 4, CSEDenseMapInfo> CSEMap;
  for (Instruction &In : llvm::make_early_inc_range(*BB)) {
    if (!CSEDenseMapInfo::canHandle(&In))
      continue;

    // Check if we can replace this instruction with any of the
    // visited instructions.
    if (Instruction *V = CSEMap.lookup(&In)) {
      In.replaceAllUsesWith(V);
      In.eraseFromParent();
      continue;
    }

    CSEMap[&In] = &In;
  }
}

InstructionCost
LoopVectorizationCostModel::getVectorCallCost(CallInst *CI,
                                              ElementCount VF) const {
  // We only need to calculate a cost if the VF is scalar; for actual vectors
  // we should already have a pre-calculated cost at each VF.
  if (!VF.isScalar())
    return CallWideningDecisions.at(std::make_pair(CI, VF)).Cost;

  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  Type *RetTy = CI->getType();
  if (RecurrenceDescriptor::isFMulAddIntrinsic(CI))
    if (auto RedCost = getReductionPatternCost(CI, VF, RetTy, CostKind))
      return *RedCost;

  SmallVector<Type *, 4> Tys;
  for (auto &ArgOp : CI->args())
    Tys.push_back(ArgOp->getType());

  InstructionCost ScalarCallCost =
      TTI.getCallInstrCost(CI->getCalledFunction(), RetTy, Tys, CostKind);

  // If this is an intrinsic we may have a lower cost for it.
  if (getVectorIntrinsicIDForCall(CI, TLI)) {
    InstructionCost IntrinsicCost = getVectorIntrinsicCost(CI, VF);
    return std::min(ScalarCallCost, IntrinsicCost);
  }
  return ScalarCallCost;
}

static Type *MaybeVectorizeType(Type *Elt, ElementCount VF) {
  if (VF.isScalar() || (!Elt->isIntOrPtrTy() && !Elt->isFloatingPointTy()))
    return Elt;
  return VectorType::get(Elt, VF);
}

InstructionCost
LoopVectorizationCostModel::getVectorIntrinsicCost(CallInst *CI,
                                                   ElementCount VF) const {
  Intrinsic::ID ID = getVectorIntrinsicIDForCall(CI, TLI);
  assert(ID && "Expected intrinsic call!");
  Type *RetTy = MaybeVectorizeType(CI->getType(), VF);
  FastMathFlags FMF;
  if (auto *FPMO = dyn_cast<FPMathOperator>(CI))
    FMF = FPMO->getFastMathFlags();

  SmallVector<const Value *> Arguments(CI->args());
  FunctionType *FTy = CI->getCalledFunction()->getFunctionType();
  SmallVector<Type *> ParamTys;
  std::transform(FTy->param_begin(), FTy->param_end(),
                 std::back_inserter(ParamTys),
                 [&](Type *Ty) { return MaybeVectorizeType(Ty, VF); });

  IntrinsicCostAttributes CostAttrs(ID, RetTy, Arguments, ParamTys, FMF,
                                    dyn_cast<IntrinsicInst>(CI));
  return TTI.getIntrinsicInstrCost(CostAttrs,
                                   TargetTransformInfo::TCK_RecipThroughput);
}

void InnerLoopVectorizer::fixVectorizedLoop(VPTransformState &State,
                                            VPlan &Plan) {
  // Fix widened non-induction PHIs by setting up the PHI operands.
  if (EnableVPlanNativePath)
    fixNonInductionPHIs(Plan, State);

  // Forget the original basic block.
  PSE.getSE()->forgetLoop(OrigLoop);
  PSE.getSE()->forgetBlockAndLoopDispositions();

  // After vectorization, the exit blocks of the original loop will have
  // additional predecessors. Invalidate SCEVs for the exit phis in case SE
  // looked through single-entry phis.
  SmallVector<BasicBlock *> ExitBlocks;
  OrigLoop->getExitBlocks(ExitBlocks);
  for (BasicBlock *Exit : ExitBlocks)
    for (PHINode &PN : Exit->phis())
      PSE.getSE()->forgetLcssaPhiWithNewPredecessor(OrigLoop, &PN);

  VPRegionBlock *VectorRegion = State.Plan->getVectorLoopRegion();
  VPBasicBlock *LatchVPBB = VectorRegion->getExitingBasicBlock();
  Loop *VectorLoop = LI->getLoopFor(State.CFG.VPBB2IRBB[LatchVPBB]);
  if (Cost->requiresScalarEpilogue(VF.isVector())) {
    // No edge from the middle block to the unique exit block has been inserted
    // and there is nothing to fix from vector loop; phis should have incoming
    // from scalar loop only.
  } else {
    // TODO: Check VPLiveOuts to see if IV users need fixing instead of checking
    // the cost model.

    // If we inserted an edge from the middle block to the unique exit block,
    // update uses outside the loop (phis) to account for the newly inserted
    // edge.

    // Fix-up external users of the induction variables.
    for (const auto &Entry : Legal->getInductionVars())
      fixupIVUsers(Entry.first, Entry.second,
                   getOrCreateVectorTripCount(VectorLoop->getLoopPreheader()),
                   IVEndValues[Entry.first], LoopMiddleBlock,
                   VectorLoop->getHeader(), Plan, State);
  }

  // Fix live-out phis not already fixed earlier.
  for (const auto &KV : Plan.getLiveOuts())
    KV.second->fixPhi(Plan, State);

  for (Instruction *PI : PredicatedInstructions)
    sinkScalarOperands(&*PI);

  // Remove redundant induction instructions.
  cse(VectorLoop->getHeader());

  // Set/update profile weights for the vector and remainder loops as original
  // loop iterations are now distributed among them. Note that original loop
  // represented by LoopScalarBody becomes remainder loop after vectorization.
  //
  // For cases like foldTailByMasking() and requiresScalarEpiloque() we may
  // end up getting slightly roughened result but that should be OK since
  // profile is not inherently precise anyway. Note also possible bypass of
  // vector code caused by legality checks is ignored, assigning all the weight
  // to the vector loop, optimistically.
  //
  // For scalable vectorization we can't know at compile time how many iterations
  // of the loop are handled in one vector iteration, so instead assume a pessimistic
  // vscale of '1'.
  setProfileInfoAfterUnrolling(LI->getLoopFor(LoopScalarBody), VectorLoop,
                               LI->getLoopFor(LoopScalarBody),
                               VF.getKnownMinValue() * UF);
}

void InnerLoopVectorizer::sinkScalarOperands(Instruction *PredInst) {
  // The basic block and loop containing the predicated instruction.
  auto *PredBB = PredInst->getParent();
  auto *VectorLoop = LI->getLoopFor(PredBB);

  // Initialize a worklist with the operands of the predicated instruction.
  SetVector<Value *> Worklist(PredInst->op_begin(), PredInst->op_end());

  // Holds instructions that we need to analyze again. An instruction may be
  // reanalyzed if we don't yet know if we can sink it or not.
  SmallVector<Instruction *, 8> InstsToReanalyze;

  // Returns true if a given use occurs in the predicated block. Phi nodes use
  // their operands in their corresponding predecessor blocks.
  auto isBlockOfUsePredicated = [&](Use &U) -> bool {
    auto *I = cast<Instruction>(U.getUser());
    BasicBlock *BB = I->getParent();
    if (auto *Phi = dyn_cast<PHINode>(I))
      BB = Phi->getIncomingBlock(
          PHINode::getIncomingValueNumForOperand(U.getOperandNo()));
    return BB == PredBB;
  };

  // Iteratively sink the scalarized operands of the predicated instruction
  // into the block we created for it. When an instruction is sunk, it's
  // operands are then added to the worklist. The algorithm ends after one pass
  // through the worklist doesn't sink a single instruction.
  bool Changed;
  do {
    // Add the instructions that need to be reanalyzed to the worklist, and
    // reset the changed indicator.
    Worklist.insert(InstsToReanalyze.begin(), InstsToReanalyze.end());
    InstsToReanalyze.clear();
    Changed = false;

    while (!Worklist.empty()) {
      auto *I = dyn_cast<Instruction>(Worklist.pop_back_val());

      // We can't sink an instruction if it is a phi node, is not in the loop,
      // may have side effects or may read from memory.
      // TODO Could dor more granular checking to allow sinking a load past non-store instructions.
      if (!I || isa<PHINode>(I) || !VectorLoop->contains(I) ||
          I->mayHaveSideEffects() || I->mayReadFromMemory())
          continue;

      // If the instruction is already in PredBB, check if we can sink its
      // operands. In that case, VPlan's sinkScalarOperands() succeeded in
      // sinking the scalar instruction I, hence it appears in PredBB; but it
      // may have failed to sink I's operands (recursively), which we try
      // (again) here.
      if (I->getParent() == PredBB) {
        Worklist.insert(I->op_begin(), I->op_end());
        continue;
      }

      // It's legal to sink the instruction if all its uses occur in the
      // predicated block. Otherwise, there's nothing to do yet, and we may
      // need to reanalyze the instruction.
      if (!llvm::all_of(I->uses(), isBlockOfUsePredicated)) {
        InstsToReanalyze.push_back(I);
        continue;
      }

      // Move the instruction to the beginning of the predicated block, and add
      // it's operands to the worklist.
      I->moveBefore(&*PredBB->getFirstInsertionPt());
      Worklist.insert(I->op_begin(), I->op_end());

      // The sinking may have enabled other instructions to be sunk, so we will
      // need to iterate.
      Changed = true;
    }
  } while (Changed);
}

void InnerLoopVectorizer::fixNonInductionPHIs(VPlan &Plan,
                                              VPTransformState &State) {
  auto Iter = vp_depth_first_deep(Plan.getEntry());
  for (VPBasicBlock *VPBB : VPBlockUtils::blocksOnly<VPBasicBlock>(Iter)) {
    for (VPRecipeBase &P : VPBB->phis()) {
      VPWidenPHIRecipe *VPPhi = dyn_cast<VPWidenPHIRecipe>(&P);
      if (!VPPhi)
        continue;
      PHINode *NewPhi = cast<PHINode>(State.get(VPPhi, 0));
      // Make sure the builder has a valid insert point.
      Builder.SetInsertPoint(NewPhi);
      for (unsigned i = 0; i < VPPhi->getNumOperands(); ++i) {
        VPValue *Inc = VPPhi->getIncomingValue(i);
        VPBasicBlock *VPBB = VPPhi->getIncomingBlock(i);
        NewPhi->addIncoming(State.get(Inc, 0), State.CFG.VPBB2IRBB[VPBB]);
      }
    }
  }
}

void LoopVectorizationCostModel::collectLoopScalars(ElementCount VF) {
  // We should not collect Scalars more than once per VF. Right now, this
  // function is called from collectUniformsAndScalars(), which already does
  // this check. Collecting Scalars for VF=1 does not make any sense.
  assert(VF.isVector() && !Scalars.contains(VF) &&
         "This function should not be visited twice for the same VF");

  // This avoids any chances of creating a REPLICATE recipe during planning
  // since that would result in generation of scalarized code during execution,
  // which is not supported for scalable vectors.
  if (VF.isScalable()) {
    Scalars[VF].insert(Uniforms[VF].begin(), Uniforms[VF].end());
    return;
  }

  SmallSetVector<Instruction *, 8> Worklist;

  // These sets are used to seed the analysis with pointers used by memory
  // accesses that will remain scalar.
  SmallSetVector<Instruction *, 8> ScalarPtrs;
  SmallPtrSet<Instruction *, 8> PossibleNonScalarPtrs;
  auto *Latch = TheLoop->getLoopLatch();

  // A helper that returns true if the use of Ptr by MemAccess will be scalar.
  // The pointer operands of loads and stores will be scalar as long as the
  // memory access is not a gather or scatter operation. The value operand of a
  // store will remain scalar if the store is scalarized.
  auto isScalarUse = [&](Instruction *MemAccess, Value *Ptr) {
    InstWidening WideningDecision = getWideningDecision(MemAccess, VF);
    assert(WideningDecision != CM_Unknown &&
           "Widening decision should be ready at this moment");
    if (auto *Store = dyn_cast<StoreInst>(MemAccess))
      if (Ptr == Store->getValueOperand())
        return WideningDecision == CM_Scalarize;
    assert(Ptr == getLoadStorePointerOperand(MemAccess) &&
           "Ptr is neither a value or pointer operand");
    return WideningDecision != CM_GatherScatter;
  };

  // A helper that returns true if the given value is a bitcast or
  // getelementptr instruction contained in the loop.
  auto isLoopVaryingBitCastOrGEP = [&](Value *V) {
    return ((isa<BitCastInst>(V) && V->getType()->isPointerTy()) ||
            isa<GetElementPtrInst>(V)) &&
           !TheLoop->isLoopInvariant(V);
  };

  // A helper that evaluates a memory access's use of a pointer. If the use will
  // be a scalar use and the pointer is only used by memory accesses, we place
  // the pointer in ScalarPtrs. Otherwise, the pointer is placed in
  // PossibleNonScalarPtrs.
  auto evaluatePtrUse = [&](Instruction *MemAccess, Value *Ptr) {
    // We only care about bitcast and getelementptr instructions contained in
    // the loop.
    if (!isLoopVaryingBitCastOrGEP(Ptr))
      return;

    // If the pointer has already been identified as scalar (e.g., if it was
    // also identified as uniform), there's nothing to do.
    auto *I = cast<Instruction>(Ptr);
    if (Worklist.count(I))
      return;

    // If the use of the pointer will be a scalar use, and all users of the
    // pointer are memory accesses, place the pointer in ScalarPtrs. Otherwise,
    // place the pointer in PossibleNonScalarPtrs.
    if (isScalarUse(MemAccess, Ptr) && llvm::all_of(I->users(), [&](User *U) {
          return isa<LoadInst>(U) || isa<StoreInst>(U);
        }))
      ScalarPtrs.insert(I);
    else
      PossibleNonScalarPtrs.insert(I);
  };

  // We seed the scalars analysis with three classes of instructions: (1)
  // instructions marked uniform-after-vectorization and (2) bitcast,
  // getelementptr and (pointer) phi instructions used by memory accesses
  // requiring a scalar use.
  //
  // (1) Add to the worklist all instructions that have been identified as
  // uniform-after-vectorization.
  Worklist.insert(Uniforms[VF].begin(), Uniforms[VF].end());

  // (2) Add to the worklist all bitcast and getelementptr instructions used by
  // memory accesses requiring a scalar use. The pointer operands of loads and
  // stores will be scalar as long as the memory accesses is not a gather or
  // scatter operation. The value operand of a store will remain scalar if the
  // store is scalarized.
  for (auto *BB : TheLoop->blocks())
    for (auto &I : *BB) {
      if (auto *Load = dyn_cast<LoadInst>(&I)) {
        evaluatePtrUse(Load, Load->getPointerOperand());
      } else if (auto *Store = dyn_cast<StoreInst>(&I)) {
        evaluatePtrUse(Store, Store->getPointerOperand());
        evaluatePtrUse(Store, Store->getValueOperand());
      }
    }
  for (auto *I : ScalarPtrs)
    if (!PossibleNonScalarPtrs.count(I)) {
      LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *I << "\n");
      Worklist.insert(I);
    }

  // Insert the forced scalars.
  // FIXME: Currently VPWidenPHIRecipe() often creates a dead vector
  // induction variable when the PHI user is scalarized.
  auto ForcedScalar = ForcedScalars.find(VF);
  if (ForcedScalar != ForcedScalars.end())
    for (auto *I : ForcedScalar->second) {
      LLVM_DEBUG(dbgs() << "LV: Found (forced) scalar instruction: " << *I << "\n");
      Worklist.insert(I);
    }

  // Expand the worklist by looking through any bitcasts and getelementptr
  // instructions we've already identified as scalar. This is similar to the
  // expansion step in collectLoopUniforms(); however, here we're only
  // expanding to include additional bitcasts and getelementptr instructions.
  unsigned Idx = 0;
  while (Idx != Worklist.size()) {
    Instruction *Dst = Worklist[Idx++];
    if (!isLoopVaryingBitCastOrGEP(Dst->getOperand(0)))
      continue;
    auto *Src = cast<Instruction>(Dst->getOperand(0));
    if (llvm::all_of(Src->users(), [&](User *U) -> bool {
          auto *J = cast<Instruction>(U);
          return !TheLoop->contains(J) || Worklist.count(J) ||
                 ((isa<LoadInst>(J) || isa<StoreInst>(J)) &&
                  isScalarUse(J, Src));
        })) {
      Worklist.insert(Src);
      LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *Src << "\n");
    }
  }

  // An induction variable will remain scalar if all users of the induction
  // variable and induction variable update remain scalar.
  for (const auto &Induction : Legal->getInductionVars()) {
    auto *Ind = Induction.first;
    auto *IndUpdate = cast<Instruction>(Ind->getIncomingValueForBlock(Latch));

    // If tail-folding is applied, the primary induction variable will be used
    // to feed a vector compare.
    if (Ind == Legal->getPrimaryInduction() && foldTailByMasking())
      continue;

    // Returns true if \p Indvar is a pointer induction that is used directly by
    // load/store instruction \p I.
    auto IsDirectLoadStoreFromPtrIndvar = [&](Instruction *Indvar,
                                              Instruction *I) {
      return Induction.second.getKind() ==
                 InductionDescriptor::IK_PtrInduction &&
             (isa<LoadInst>(I) || isa<StoreInst>(I)) &&
             Indvar == getLoadStorePointerOperand(I) && isScalarUse(I, Indvar);
    };

    // Determine if all users of the induction variable are scalar after
    // vectorization.
    auto ScalarInd = llvm::all_of(Ind->users(), [&](User *U) -> bool {
      auto *I = cast<Instruction>(U);
      return I == IndUpdate || !TheLoop->contains(I) || Worklist.count(I) ||
             IsDirectLoadStoreFromPtrIndvar(Ind, I);
    });
    if (!ScalarInd)
      continue;

    // If the induction variable update is a fixed-order recurrence, neither the
    // induction variable or its update should be marked scalar after
    // vectorization.
    auto *IndUpdatePhi = dyn_cast<PHINode>(IndUpdate);
    if (IndUpdatePhi && Legal->isFixedOrderRecurrence(IndUpdatePhi))
      continue;

    // Determine if all users of the induction variable update instruction are
    // scalar after vectorization.
    auto ScalarIndUpdate =
        llvm::all_of(IndUpdate->users(), [&](User *U) -> bool {
          auto *I = cast<Instruction>(U);
          return I == Ind || !TheLoop->contains(I) || Worklist.count(I) ||
                 IsDirectLoadStoreFromPtrIndvar(IndUpdate, I);
        });
    if (!ScalarIndUpdate)
      continue;

    // The induction variable and its update instruction will remain scalar.
    Worklist.insert(Ind);
    Worklist.insert(IndUpdate);
    LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *Ind << "\n");
    LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *IndUpdate
                      << "\n");
  }

  Scalars[VF].insert(Worklist.begin(), Worklist.end());
}

bool LoopVectorizationCostModel::isScalarWithPredication(
    Instruction *I, ElementCount VF) const {
  if (!isPredicatedInst(I))
    return false;

  // Do we have a non-scalar lowering for this predicated
  // instruction? No - it is scalar with predication.
  switch(I->getOpcode()) {
  default:
    return true;
  case Instruction::Call:
    if (VF.isScalar())
      return true;
    return CallWideningDecisions.at(std::make_pair(cast<CallInst>(I), VF))
               .Kind == CM_Scalarize;
  case Instruction::Load:
  case Instruction::Store: {
    auto *Ptr = getLoadStorePointerOperand(I);
    auto *Ty = getLoadStoreType(I);
    Type *VTy = Ty;
    if (VF.isVector())
      VTy = VectorType::get(Ty, VF);
    const Align Alignment = getLoadStoreAlignment(I);
    return isa<LoadInst>(I) ? !(isLegalMaskedLoad(Ty, Ptr, Alignment) ||
                                TTI.isLegalMaskedGather(VTy, Alignment))
                            : !(isLegalMaskedStore(Ty, Ptr, Alignment) ||
                                TTI.isLegalMaskedScatter(VTy, Alignment));
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::SRem:
  case Instruction::URem: {
    // We have the option to use the safe-divisor idiom to avoid predication.
    // The cost based decision here will always select safe-divisor for
    // scalable vectors as scalarization isn't legal.
    const auto [ScalarCost, SafeDivisorCost] = getDivRemSpeculationCost(I, VF);
    return isDivRemScalarWithPredication(ScalarCost, SafeDivisorCost);
  }
  }
}

bool LoopVectorizationCostModel::isPredicatedInst(Instruction *I) const {
  if (!blockNeedsPredicationForAnyReason(I->getParent()))
    return false;

  // Can we prove this instruction is safe to unconditionally execute?
  // If not, we must use some form of predication.
  switch(I->getOpcode()) {
  default:
    return false;
  case Instruction::Load:
  case Instruction::Store: {
    if (!Legal->isMaskRequired(I))
      return false;
    // When we know the load's address is loop invariant and the instruction
    // in the original scalar loop was unconditionally executed then we
    // don't need to mark it as a predicated instruction. Tail folding may
    // introduce additional predication, but we're guaranteed to always have
    // at least one active lane.  We call Legal->blockNeedsPredication here
    // because it doesn't query tail-folding.  For stores, we need to prove
    // both speculation safety (which follows from the same argument as loads),
    // but also must prove the value being stored is correct.  The easiest
    // form of the later is to require that all values stored are the same.
    if (Legal->isInvariant(getLoadStorePointerOperand(I)) &&
        (isa<LoadInst>(I) ||
         (isa<StoreInst>(I) &&
          TheLoop->isLoopInvariant(cast<StoreInst>(I)->getValueOperand()))) &&
        !Legal->blockNeedsPredication(I->getParent()))
      return false;
    return true;
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::SRem:
  case Instruction::URem:
    // TODO: We can use the loop-preheader as context point here and get
    // context sensitive reasoning
    return !isSafeToSpeculativelyExecute(I);
  case Instruction::Call:
    return Legal->isMaskRequired(I);
  }
}

std::pair<InstructionCost, InstructionCost>
LoopVectorizationCostModel::getDivRemSpeculationCost(Instruction *I,
                                                    ElementCount VF) const {
  assert(I->getOpcode() == Instruction::UDiv ||
         I->getOpcode() == Instruction::SDiv ||
         I->getOpcode() == Instruction::SRem ||
         I->getOpcode() == Instruction::URem);
  assert(!isSafeToSpeculativelyExecute(I));

  const TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  // Scalarization isn't legal for scalable vector types
  InstructionCost ScalarizationCost = InstructionCost::getInvalid();
  if (!VF.isScalable()) {
    // Get the scalarization cost and scale this amount by the probability of
    // executing the predicated block. If the instruction is not predicated,
    // we fall through to the next case.
    ScalarizationCost = 0;

    // These instructions have a non-void type, so account for the phi nodes
    // that we will create. This cost is likely to be zero. The phi node
    // cost, if any, should be scaled by the block probability because it
    // models a copy at the end of each predicated block.
    ScalarizationCost += VF.getKnownMinValue() *
      TTI.getCFInstrCost(Instruction::PHI, CostKind);

    // The cost of the non-predicated instruction.
    ScalarizationCost += VF.getKnownMinValue() *
      TTI.getArithmeticInstrCost(I->getOpcode(), I->getType(), CostKind);

    // The cost of insertelement and extractelement instructions needed for
    // scalarization.
    ScalarizationCost += getScalarizationOverhead(I, VF, CostKind);

    // Scale the cost by the probability of executing the predicated blocks.
    // This assumes the predicated block for each vector lane is equally
    // likely.
    ScalarizationCost = ScalarizationCost / getReciprocalPredBlockProb();
  }
  InstructionCost SafeDivisorCost = 0;

  auto *VecTy = ToVectorTy(I->getType(), VF);

  // The cost of the select guard to ensure all lanes are well defined
  // after we speculate above any internal control flow.
  SafeDivisorCost += TTI.getCmpSelInstrCost(
    Instruction::Select, VecTy,
    ToVectorTy(Type::getInt1Ty(I->getContext()), VF),
    CmpInst::BAD_ICMP_PREDICATE, CostKind);

  // Certain instructions can be cheaper to vectorize if they have a constant
  // second vector operand. One example of this are shifts on x86.
  Value *Op2 = I->getOperand(1);
  auto Op2Info = TTI.getOperandInfo(Op2);
  if (Op2Info.Kind == TargetTransformInfo::OK_AnyValue &&
      Legal->isInvariant(Op2))
    Op2Info.Kind = TargetTransformInfo::OK_UniformValue;

  SmallVector<const Value *, 4> Operands(I->operand_values());
  SafeDivisorCost += TTI.getArithmeticInstrCost(
    I->getOpcode(), VecTy, CostKind,
    {TargetTransformInfo::OK_AnyValue, TargetTransformInfo::OP_None},
    Op2Info, Operands, I);
  return {ScalarizationCost, SafeDivisorCost};
}

bool LoopVectorizationCostModel::interleavedAccessCanBeWidened(
    Instruction *I, ElementCount VF) const {
  assert(isAccessInterleaved(I) && "Expecting interleaved access.");
  assert(getWideningDecision(I, VF) == CM_Unknown &&
         "Decision should not be set yet.");
  auto *Group = getInterleavedAccessGroup(I);
  assert(Group && "Must have a group.");

  // If the instruction's allocated size doesn't equal it's type size, it
  // requires padding and will be scalarized.
  auto &DL = I->getDataLayout();
  auto *ScalarTy = getLoadStoreType(I);
  if (hasIrregularType(ScalarTy, DL))
    return false;

  // If the group involves a non-integral pointer, we may not be able to
  // losslessly cast all values to a common type.
  unsigned InterleaveFactor = Group->getFactor();
  bool ScalarNI = DL.isNonIntegralPointerType(ScalarTy);
  for (unsigned i = 0; i < InterleaveFactor; i++) {
    Instruction *Member = Group->getMember(i);
    if (!Member)
      continue;
    auto *MemberTy = getLoadStoreType(Member);
    bool MemberNI = DL.isNonIntegralPointerType(MemberTy);
    // Don't coerce non-integral pointers to integers or vice versa.
    if (MemberNI != ScalarNI) {
      // TODO: Consider adding special nullptr value case here
      return false;
    } else if (MemberNI && ScalarNI &&
               ScalarTy->getPointerAddressSpace() !=
               MemberTy->getPointerAddressSpace()) {
      return false;
    }
  }

  // Check if masking is required.
  // A Group may need masking for one of two reasons: it resides in a block that
  // needs predication, or it was decided to use masking to deal with gaps
  // (either a gap at the end of a load-access that may result in a speculative
  // load, or any gaps in a store-access).
  bool PredicatedAccessRequiresMasking =
      blockNeedsPredicationForAnyReason(I->getParent()) &&
      Legal->isMaskRequired(I);
  bool LoadAccessWithGapsRequiresEpilogMasking =
      isa<LoadInst>(I) && Group->requiresScalarEpilogue() &&
      !isScalarEpilogueAllowed();
  bool StoreAccessWithGapsRequiresMasking =
      isa<StoreInst>(I) && (Group->getNumMembers() < Group->getFactor());
  if (!PredicatedAccessRequiresMasking &&
      !LoadAccessWithGapsRequiresEpilogMasking &&
      !StoreAccessWithGapsRequiresMasking)
    return true;

  // If masked interleaving is required, we expect that the user/target had
  // enabled it, because otherwise it either wouldn't have been created or
  // it should have been invalidated by the CostModel.
  assert(useMaskedInterleavedAccesses(TTI) &&
         "Masked interleave-groups for predicated accesses are not enabled.");

  if (Group->isReverse())
    return false;

  auto *Ty = getLoadStoreType(I);
  const Align Alignment = getLoadStoreAlignment(I);
  return isa<LoadInst>(I) ? TTI.isLegalMaskedLoad(Ty, Alignment)
                          : TTI.isLegalMaskedStore(Ty, Alignment);
}

bool LoopVectorizationCostModel::memoryInstructionCanBeWidened(
    Instruction *I, ElementCount VF) {
  // Get and ensure we have a valid memory instruction.
  assert((isa<LoadInst, StoreInst>(I)) && "Invalid memory instruction");

  auto *Ptr = getLoadStorePointerOperand(I);
  auto *ScalarTy = getLoadStoreType(I);

  // In order to be widened, the pointer should be consecutive, first of all.
  if (!Legal->isConsecutivePtr(ScalarTy, Ptr))
    return false;

  // If the instruction is a store located in a predicated block, it will be
  // scalarized.
  if (isScalarWithPredication(I, VF))
    return false;

  // If the instruction's allocated size doesn't equal it's type size, it
  // requires padding and will be scalarized.
  auto &DL = I->getDataLayout();
  if (hasIrregularType(ScalarTy, DL))
    return false;

  return true;
}

void LoopVectorizationCostModel::collectLoopUniforms(ElementCount VF) {
  // We should not collect Uniforms more than once per VF. Right now,
  // this function is called from collectUniformsAndScalars(), which
  // already does this check. Collecting Uniforms for VF=1 does not make any
  // sense.

  assert(VF.isVector() && !Uniforms.contains(VF) &&
         "This function should not be visited twice for the same VF");

  // Visit the list of Uniforms. If we'll not find any uniform value, we'll
  // not analyze again.  Uniforms.count(VF) will return 1.
  Uniforms[VF].clear();

  // We now know that the loop is vectorizable!
  // Collect instructions inside the loop that will remain uniform after
  // vectorization.

  // Global values, params and instructions outside of current loop are out of
  // scope.
  auto isOutOfScope = [&](Value *V) -> bool {
    Instruction *I = dyn_cast<Instruction>(V);
    return (!I || !TheLoop->contains(I));
  };

  // Worklist containing uniform instructions demanding lane 0.
  SetVector<Instruction *> Worklist;

  // Add uniform instructions demanding lane 0 to the worklist. Instructions
  // that require predication must not be considered uniform after
  // vectorization, because that would create an erroneous replicating region
  // where only a single instance out of VF should be formed.
  auto addToWorklistIfAllowed = [&](Instruction *I) -> void {
    if (isOutOfScope(I)) {
      LLVM_DEBUG(dbgs() << "LV: Found not uniform due to scope: "
                        << *I << "\n");
      return;
    }
    if (isPredicatedInst(I)) {
      LLVM_DEBUG(
          dbgs() << "LV: Found not uniform due to requiring predication: " << *I
                 << "\n");
      return;
    }
    LLVM_DEBUG(dbgs() << "LV: Found uniform instruction: " << *I << "\n");
    Worklist.insert(I);
  };

  // Start with the conditional branches exiting the loop. If the branch
  // condition is an instruction contained in the loop that is only used by the
  // branch, it is uniform.
  SmallVector<BasicBlock *> Exiting;
  TheLoop->getExitingBlocks(Exiting);
  for (BasicBlock *E : Exiting) {
    auto *Cmp = dyn_cast<Instruction>(E->getTerminator()->getOperand(0));
    if (Cmp && TheLoop->contains(Cmp) && Cmp->hasOneUse())
      addToWorklistIfAllowed(Cmp);
  }

  auto PrevVF = VF.divideCoefficientBy(2);
  // Return true if all lanes perform the same memory operation, and we can
  // thus chose to execute only one.
  auto isUniformMemOpUse = [&](Instruction *I) {
    // If the value was already known to not be uniform for the previous
    // (smaller VF), it cannot be uniform for the larger VF.
    if (PrevVF.isVector()) {
      auto Iter = Uniforms.find(PrevVF);
      if (Iter != Uniforms.end() && !Iter->second.contains(I))
        return false;
    }
    if (!Legal->isUniformMemOp(*I, VF))
      return false;
    if (isa<LoadInst>(I))
      // Loading the same address always produces the same result - at least
      // assuming aliasing and ordering which have already been checked.
      return true;
    // Storing the same value on every iteration.
    return TheLoop->isLoopInvariant(cast<StoreInst>(I)->getValueOperand());
  };

  auto isUniformDecision = [&](Instruction *I, ElementCount VF) {
    InstWidening WideningDecision = getWideningDecision(I, VF);
    assert(WideningDecision != CM_Unknown &&
           "Widening decision should be ready at this moment");

    if (isUniformMemOpUse(I))
      return true;

    return (WideningDecision == CM_Widen ||
            WideningDecision == CM_Widen_Reverse ||
            WideningDecision == CM_Interleave);
  };

  // Returns true if Ptr is the pointer operand of a memory access instruction
  // I, I is known to not require scalarization, and the pointer is not also
  // stored.
  auto isVectorizedMemAccessUse = [&](Instruction *I, Value *Ptr) -> bool {
    if (isa<StoreInst>(I) && I->getOperand(0) == Ptr)
      return false;
    return getLoadStorePointerOperand(I) == Ptr &&
           (isUniformDecision(I, VF) || Legal->isInvariant(Ptr));
  };

  // Holds a list of values which are known to have at least one uniform use.
  // Note that there may be other uses which aren't uniform.  A "uniform use"
  // here is something which only demands lane 0 of the unrolled iterations;
  // it does not imply that all lanes produce the same value (e.g. this is not
  // the usual meaning of uniform)
  SetVector<Value *> HasUniformUse;

  // Scan the loop for instructions which are either a) known to have only
  // lane 0 demanded or b) are uses which demand only lane 0 of their operand.
  for (auto *BB : TheLoop->blocks())
    for (auto &I : *BB) {
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I)) {
        switch (II->getIntrinsicID()) {
        case Intrinsic::sideeffect:
        case Intrinsic::experimental_noalias_scope_decl:
        case Intrinsic::assume:
        case Intrinsic::lifetime_start:
        case Intrinsic::lifetime_end:
          if (TheLoop->hasLoopInvariantOperands(&I))
            addToWorklistIfAllowed(&I);
          break;
        default:
          break;
        }
      }

      // ExtractValue instructions must be uniform, because the operands are
      // known to be loop-invariant.
      if (auto *EVI = dyn_cast<ExtractValueInst>(&I)) {
        assert(isOutOfScope(EVI->getAggregateOperand()) &&
               "Expected aggregate value to be loop invariant");
        addToWorklistIfAllowed(EVI);
        continue;
      }

      // If there's no pointer operand, there's nothing to do.
      auto *Ptr = getLoadStorePointerOperand(&I);
      if (!Ptr)
        continue;

      if (isUniformMemOpUse(&I))
        addToWorklistIfAllowed(&I);

      if (isVectorizedMemAccessUse(&I, Ptr))
        HasUniformUse.insert(Ptr);
    }

  // Add to the worklist any operands which have *only* uniform (e.g. lane 0
  // demanding) users.  Since loops are assumed to be in LCSSA form, this
  // disallows uses outside the loop as well.
  for (auto *V : HasUniformUse) {
    if (isOutOfScope(V))
      continue;
    auto *I = cast<Instruction>(V);
    auto UsersAreMemAccesses =
      llvm::all_of(I->users(), [&](User *U) -> bool {
        return isVectorizedMemAccessUse(cast<Instruction>(U), V);
      });
    if (UsersAreMemAccesses)
      addToWorklistIfAllowed(I);
  }

  // Expand Worklist in topological order: whenever a new instruction
  // is added , its users should be already inside Worklist.  It ensures
  // a uniform instruction will only be used by uniform instructions.
  unsigned idx = 0;
  while (idx != Worklist.size()) {
    Instruction *I = Worklist[idx++];

    for (auto *OV : I->operand_values()) {
      // isOutOfScope operands cannot be uniform instructions.
      if (isOutOfScope(OV))
        continue;
      // First order recurrence Phi's should typically be considered
      // non-uniform.
      auto *OP = dyn_cast<PHINode>(OV);
      if (OP && Legal->isFixedOrderRecurrence(OP))
        continue;
      // If all the users of the operand are uniform, then add the
      // operand into the uniform worklist.
      auto *OI = cast<Instruction>(OV);
      if (llvm::all_of(OI->users(), [&](User *U) -> bool {
            auto *J = cast<Instruction>(U);
            return Worklist.count(J) || isVectorizedMemAccessUse(J, OI);
          }))
        addToWorklistIfAllowed(OI);
    }
  }

  // For an instruction to be added into Worklist above, all its users inside
  // the loop should also be in Worklist. However, this condition cannot be
  // true for phi nodes that form a cyclic dependence. We must process phi
  // nodes separately. An induction variable will remain uniform if all users
  // of the induction variable and induction variable update remain uniform.
  // The code below handles both pointer and non-pointer induction variables.
  BasicBlock *Latch = TheLoop->getLoopLatch();
  for (const auto &Induction : Legal->getInductionVars()) {
    auto *Ind = Induction.first;
    auto *IndUpdate = cast<Instruction>(Ind->getIncomingValueForBlock(Latch));

    // Determine if all users of the induction variable are uniform after
    // vectorization.
    auto UniformInd = llvm::all_of(Ind->users(), [&](User *U) -> bool {
      auto *I = cast<Instruction>(U);
      return I == IndUpdate || !TheLoop->contains(I) || Worklist.count(I) ||
             isVectorizedMemAccessUse(I, Ind);
    });
    if (!UniformInd)
      continue;

    // Determine if all users of the induction variable update instruction are
    // uniform after vectorization.
    auto UniformIndUpdate =
        llvm::all_of(IndUpdate->users(), [&](User *U) -> bool {
          auto *I = cast<Instruction>(U);
          return I == Ind || !TheLoop->contains(I) || Worklist.count(I) ||
                 isVectorizedMemAccessUse(I, IndUpdate);
        });
    if (!UniformIndUpdate)
      continue;

    // The induction variable and its update instruction will remain uniform.
    addToWorklistIfAllowed(Ind);
    addToWorklistIfAllowed(IndUpdate);
  }

  Uniforms[VF].insert(Worklist.begin(), Worklist.end());
}

bool LoopVectorizationCostModel::runtimeChecksRequired() {
  LLVM_DEBUG(dbgs() << "LV: Performing code size checks.\n");

  if (Legal->getRuntimePointerChecking()->Need) {
    reportVectorizationFailure("Runtime ptr check is required with -Os/-Oz",
        "runtime pointer checks needed. Enable vectorization of this "
        "loop with '#pragma clang loop vectorize(enable)' when "
        "compiling with -Os/-Oz",
        "CantVersionLoopWithOptForSize", ORE, TheLoop);
    return true;
  }

  if (!PSE.getPredicate().isAlwaysTrue()) {
    reportVectorizationFailure("Runtime SCEV check is required with -Os/-Oz",
        "runtime SCEV checks needed. Enable vectorization of this "
        "loop with '#pragma clang loop vectorize(enable)' when "
        "compiling with -Os/-Oz",
        "CantVersionLoopWithOptForSize", ORE, TheLoop);
    return true;
  }

  // FIXME: Avoid specializing for stride==1 instead of bailing out.
  if (!Legal->getLAI()->getSymbolicStrides().empty()) {
    reportVectorizationFailure("Runtime stride check for small trip count",
        "runtime stride == 1 checks needed. Enable vectorization of "
        "this loop without such check by compiling with -Os/-Oz",
        "CantVersionLoopWithOptForSize", ORE, TheLoop);
    return true;
  }

  return false;
}

bool LoopVectorizationCostModel::isScalableVectorizationAllowed() {
  if (IsScalableVectorizationAllowed)
    return *IsScalableVectorizationAllowed;

  IsScalableVectorizationAllowed = false;
  if (!TTI.supportsScalableVectors() && !ForceTargetSupportsScalableVectors)
    return false;

  if (Hints->isScalableVectorizationDisabled()) {
    reportVectorizationInfo("Scalable vectorization is explicitly disabled",
                            "ScalableVectorizationDisabled", ORE, TheLoop);
    return false;
  }

  LLVM_DEBUG(dbgs() << "LV: Scalable vectorization is available\n");

  auto MaxScalableVF = ElementCount::getScalable(
      std::numeric_limits<ElementCount::ScalarTy>::max());

  // Test that the loop-vectorizer can legalize all operations for this MaxVF.
  // FIXME: While for scalable vectors this is currently sufficient, this should
  // be replaced by a more detailed mechanism that filters out specific VFs,
  // instead of invalidating vectorization for a whole set of VFs based on the
  // MaxVF.

  // Disable scalable vectorization if the loop contains unsupported reductions.
  if (!canVectorizeReductions(MaxScalableVF)) {
    reportVectorizationInfo(
        "Scalable vectorization not supported for the reduction "
        "operations found in this loop.",
        "ScalableVFUnfeasible", ORE, TheLoop);
    return false;
  }

  // Disable scalable vectorization if the loop contains any instructions
  // with element types not supported for scalable vectors.
  if (any_of(ElementTypesInLoop, [&](Type *Ty) {
        return !Ty->isVoidTy() &&
               !this->TTI.isElementTypeLegalForScalableVector(Ty);
      })) {
    reportVectorizationInfo("Scalable vectorization is not supported "
                            "for all element types found in this loop.",
                            "ScalableVFUnfeasible", ORE, TheLoop);
    return false;
  }

  if (!Legal->isSafeForAnyVectorWidth() && !getMaxVScale(*TheFunction, TTI)) {
    reportVectorizationInfo("The target does not provide maximum vscale value "
                            "for safe distance analysis.",
                            "ScalableVFUnfeasible", ORE, TheLoop);
    return false;
  }

  IsScalableVectorizationAllowed = true;
  return true;
}

ElementCount
LoopVectorizationCostModel::getMaxLegalScalableVF(unsigned MaxSafeElements) {
  if (!isScalableVectorizationAllowed())
    return ElementCount::getScalable(0);

  auto MaxScalableVF = ElementCount::getScalable(
      std::numeric_limits<ElementCount::ScalarTy>::max());
  if (Legal->isSafeForAnyVectorWidth())
    return MaxScalableVF;

  std::optional<unsigned> MaxVScale = getMaxVScale(*TheFunction, TTI);
  // Limit MaxScalableVF by the maximum safe dependence distance.
  MaxScalableVF = ElementCount::getScalable(MaxSafeElements / *MaxVScale);

  if (!MaxScalableVF)
    reportVectorizationInfo(
        "Max legal vector width too small, scalable vectorization "
        "unfeasible.",
        "ScalableVFUnfeasible", ORE, TheLoop);

  return MaxScalableVF;
}

FixedScalableVFPair LoopVectorizationCostModel::computeFeasibleMaxVF(
    unsigned MaxTripCount, ElementCount UserVF, bool FoldTailByMasking) {
  MinBWs = computeMinimumValueSizes(TheLoop->getBlocks(), *DB, &TTI);
  unsigned SmallestType, WidestType;
  std::tie(SmallestType, WidestType) = getSmallestAndWidestTypes();

  // Get the maximum safe dependence distance in bits computed by LAA.
  // It is computed by MaxVF * sizeOf(type) * 8, where type is taken from
  // the memory accesses that is most restrictive (involved in the smallest
  // dependence distance).
  unsigned MaxSafeElements =
      llvm::bit_floor(Legal->getMaxSafeVectorWidthInBits() / WidestType);

  auto MaxSafeFixedVF = ElementCount::getFixed(MaxSafeElements);
  auto MaxSafeScalableVF = getMaxLegalScalableVF(MaxSafeElements);

  LLVM_DEBUG(dbgs() << "LV: The max safe fixed VF is: " << MaxSafeFixedVF
                    << ".\n");
  LLVM_DEBUG(dbgs() << "LV: The max safe scalable VF is: " << MaxSafeScalableVF
                    << ".\n");

  // First analyze the UserVF, fall back if the UserVF should be ignored.
  if (UserVF) {
    auto MaxSafeUserVF =
        UserVF.isScalable() ? MaxSafeScalableVF : MaxSafeFixedVF;

    if (ElementCount::isKnownLE(UserVF, MaxSafeUserVF)) {
      // If `VF=vscale x N` is safe, then so is `VF=N`
      if (UserVF.isScalable())
        return FixedScalableVFPair(
            ElementCount::getFixed(UserVF.getKnownMinValue()), UserVF);
      else
        return UserVF;
    }

    assert(ElementCount::isKnownGT(UserVF, MaxSafeUserVF));

    // Only clamp if the UserVF is not scalable. If the UserVF is scalable, it
    // is better to ignore the hint and let the compiler choose a suitable VF.
    if (!UserVF.isScalable()) {
      LLVM_DEBUG(dbgs() << "LV: User VF=" << UserVF
                        << " is unsafe, clamping to max safe VF="
                        << MaxSafeFixedVF << ".\n");
      ORE->emit([&]() {
        return OptimizationRemarkAnalysis(DEBUG_TYPE, "VectorizationFactor",
                                          TheLoop->getStartLoc(),
                                          TheLoop->getHeader())
               << "User-specified vectorization factor "
               << ore::NV("UserVectorizationFactor", UserVF)
               << " is unsafe, clamping to maximum safe vectorization factor "
               << ore::NV("VectorizationFactor", MaxSafeFixedVF);
      });
      return MaxSafeFixedVF;
    }

    if (!TTI.supportsScalableVectors() && !ForceTargetSupportsScalableVectors) {
      LLVM_DEBUG(dbgs() << "LV: User VF=" << UserVF
                        << " is ignored because scalable vectors are not "
                           "available.\n");
      ORE->emit([&]() {
        return OptimizationRemarkAnalysis(DEBUG_TYPE, "VectorizationFactor",
                                          TheLoop->getStartLoc(),
                                          TheLoop->getHeader())
               << "User-specified vectorization factor "
               << ore::NV("UserVectorizationFactor", UserVF)
               << " is ignored because the target does not support scalable "
                  "vectors. The compiler will pick a more suitable value.";
      });
    } else {
      LLVM_DEBUG(dbgs() << "LV: User VF=" << UserVF
                        << " is unsafe. Ignoring scalable UserVF.\n");
      ORE->emit([&]() {
        return OptimizationRemarkAnalysis(DEBUG_TYPE, "VectorizationFactor",
                                          TheLoop->getStartLoc(),
                                          TheLoop->getHeader())
               << "User-specified vectorization factor "
               << ore::NV("UserVectorizationFactor", UserVF)
               << " is unsafe. Ignoring the hint to let the compiler pick a "
                  "more suitable value.";
      });
    }
  }

  LLVM_DEBUG(dbgs() << "LV: The Smallest and Widest types: " << SmallestType
                    << " / " << WidestType << " bits.\n");

  FixedScalableVFPair Result(ElementCount::getFixed(1),
                             ElementCount::getScalable(0));
  if (auto MaxVF =
          getMaximizedVFForTarget(MaxTripCount, SmallestType, WidestType,
                                  MaxSafeFixedVF, FoldTailByMasking))
    Result.FixedVF = MaxVF;

  if (auto MaxVF =
          getMaximizedVFForTarget(MaxTripCount, SmallestType, WidestType,
                                  MaxSafeScalableVF, FoldTailByMasking))
    if (MaxVF.isScalable()) {
      Result.ScalableVF = MaxVF;
      LLVM_DEBUG(dbgs() << "LV: Found feasible scalable VF = " << MaxVF
                        << "\n");
    }

  return Result;
}

FixedScalableVFPair
LoopVectorizationCostModel::computeMaxVF(ElementCount UserVF, unsigned UserIC) {
  if (Legal->getRuntimePointerChecking()->Need && TTI.hasBranchDivergence()) {
    // TODO: It may by useful to do since it's still likely to be dynamically
    // uniform if the target can skip.
    reportVectorizationFailure(
        "Not inserting runtime ptr check for divergent target",
        "runtime pointer checks needed. Not enabled for divergent target",
        "CantVersionLoopWithDivergentTarget", ORE, TheLoop);
    return FixedScalableVFPair::getNone();
  }

  unsigned TC = PSE.getSE()->getSmallConstantTripCount(TheLoop);
  unsigned MaxTC = PSE.getSE()->getSmallConstantMaxTripCount(TheLoop);
  LLVM_DEBUG(dbgs() << "LV: Found trip count: " << TC << '\n');
  if (TC == 1) {
    reportVectorizationFailure("Single iteration (non) loop",
        "loop trip count is one, irrelevant for vectorization",
        "SingleIterationLoop", ORE, TheLoop);
    return FixedScalableVFPair::getNone();
  }

  switch (ScalarEpilogueStatus) {
  case CM_ScalarEpilogueAllowed:
    return computeFeasibleMaxVF(MaxTC, UserVF, false);
  case CM_ScalarEpilogueNotAllowedUsePredicate:
    [[fallthrough]];
  case CM_ScalarEpilogueNotNeededUsePredicate:
    LLVM_DEBUG(
        dbgs() << "LV: vector predicate hint/switch found.\n"
               << "LV: Not allowing scalar epilogue, creating predicated "
               << "vector loop.\n");
    break;
  case CM_ScalarEpilogueNotAllowedLowTripLoop:
    // fallthrough as a special case of OptForSize
  case CM_ScalarEpilogueNotAllowedOptSize:
    if (ScalarEpilogueStatus == CM_ScalarEpilogueNotAllowedOptSize)
      LLVM_DEBUG(
          dbgs() << "LV: Not allowing scalar epilogue due to -Os/-Oz.\n");
    else
      LLVM_DEBUG(dbgs() << "LV: Not allowing scalar epilogue due to low trip "
                        << "count.\n");

    // Bail if runtime checks are required, which are not good when optimising
    // for size.
    if (runtimeChecksRequired())
      return FixedScalableVFPair::getNone();

    break;
  }

  // The only loops we can vectorize without a scalar epilogue, are loops with
  // a bottom-test and a single exiting block. We'd have to handle the fact
  // that not every instruction executes on the last iteration.  This will
  // require a lane mask which varies through the vector loop body.  (TODO)
  if (TheLoop->getExitingBlock() != TheLoop->getLoopLatch()) {
    // If there was a tail-folding hint/switch, but we can't fold the tail by
    // masking, fallback to a vectorization with a scalar epilogue.
    if (ScalarEpilogueStatus == CM_ScalarEpilogueNotNeededUsePredicate) {
      LLVM_DEBUG(dbgs() << "LV: Cannot fold tail by masking: vectorize with a "
                           "scalar epilogue instead.\n");
      ScalarEpilogueStatus = CM_ScalarEpilogueAllowed;
      return computeFeasibleMaxVF(MaxTC, UserVF, false);
    }
    return FixedScalableVFPair::getNone();
  }

  // Now try the tail folding

  // Invalidate interleave groups that require an epilogue if we can't mask
  // the interleave-group.
  if (!useMaskedInterleavedAccesses(TTI)) {
    assert(WideningDecisions.empty() && Uniforms.empty() && Scalars.empty() &&
           "No decisions should have been taken at this point");
    // Note: There is no need to invalidate any cost modeling decisions here, as
    // non where taken so far.
    InterleaveInfo.invalidateGroupsRequiringScalarEpilogue();
  }

  FixedScalableVFPair MaxFactors = computeFeasibleMaxVF(MaxTC, UserVF, true);

  // Avoid tail folding if the trip count is known to be a multiple of any VF
  // we choose.
  std::optional<unsigned> MaxPowerOf2RuntimeVF =
      MaxFactors.FixedVF.getFixedValue();
  if (MaxFactors.ScalableVF) {
    std::optional<unsigned> MaxVScale = getMaxVScale(*TheFunction, TTI);
    if (MaxVScale && TTI.isVScaleKnownToBeAPowerOfTwo()) {
      MaxPowerOf2RuntimeVF = std::max<unsigned>(
          *MaxPowerOf2RuntimeVF,
          *MaxVScale * MaxFactors.ScalableVF.getKnownMinValue());
    } else
      MaxPowerOf2RuntimeVF = std::nullopt; // Stick with tail-folding for now.
  }

  if (MaxPowerOf2RuntimeVF && *MaxPowerOf2RuntimeVF > 0) {
    assert((UserVF.isNonZero() || isPowerOf2_32(*MaxPowerOf2RuntimeVF)) &&
           "MaxFixedVF must be a power of 2");
    unsigned MaxVFtimesIC =
        UserIC ? *MaxPowerOf2RuntimeVF * UserIC : *MaxPowerOf2RuntimeVF;
    ScalarEvolution *SE = PSE.getSE();
    const SCEV *BackedgeTakenCount = PSE.getBackedgeTakenCount();
    const SCEV *ExitCount = SE->getAddExpr(
        BackedgeTakenCount, SE->getOne(BackedgeTakenCount->getType()));
    const SCEV *Rem = SE->getURemExpr(
        SE->applyLoopGuards(ExitCount, TheLoop),
        SE->getConstant(BackedgeTakenCount->getType(), MaxVFtimesIC));
    if (Rem->isZero()) {
      // Accept MaxFixedVF if we do not have a tail.
      LLVM_DEBUG(dbgs() << "LV: No tail will remain for any chosen VF.\n");
      return MaxFactors;
    }
  }

  // If we don't know the precise trip count, or if the trip count that we
  // found modulo the vectorization factor is not zero, try to fold the tail
  // by masking.
  // FIXME: look for a smaller MaxVF that does divide TC rather than masking.
  setTailFoldingStyles(MaxFactors.ScalableVF.isScalable(), UserIC);
  if (foldTailByMasking()) {
    if (getTailFoldingStyle() == TailFoldingStyle::DataWithEVL) {
      LLVM_DEBUG(
          dbgs()
          << "LV: tail is folded with EVL, forcing unroll factor to be 1. Will "
             "try to generate VP Intrinsics with scalable vector "
             "factors only.\n");
      // Tail folded loop using VP intrinsics restricts the VF to be scalable
      // for now.
      // TODO: extend it for fixed vectors, if required.
      assert(MaxFactors.ScalableVF.isScalable() &&
             "Expected scalable vector factor.");

      MaxFactors.FixedVF = ElementCount::getFixed(1);
    }
    return MaxFactors;
  }

  // If there was a tail-folding hint/switch, but we can't fold the tail by
  // masking, fallback to a vectorization with a scalar epilogue.
  if (ScalarEpilogueStatus == CM_ScalarEpilogueNotNeededUsePredicate) {
    LLVM_DEBUG(dbgs() << "LV: Cannot fold tail by masking: vectorize with a "
                         "scalar epilogue instead.\n");
    ScalarEpilogueStatus = CM_ScalarEpilogueAllowed;
    return MaxFactors;
  }

  if (ScalarEpilogueStatus == CM_ScalarEpilogueNotAllowedUsePredicate) {
    LLVM_DEBUG(dbgs() << "LV: Can't fold tail by masking: don't vectorize\n");
    return FixedScalableVFPair::getNone();
  }

  if (TC == 0) {
    reportVectorizationFailure(
        "Unable to calculate the loop count due to complex control flow",
        "unable to calculate the loop count due to complex control flow",
        "UnknownLoopCountComplexCFG", ORE, TheLoop);
    return FixedScalableVFPair::getNone();
  }

  reportVectorizationFailure(
      "Cannot optimize for size and vectorize at the same time.",
      "cannot optimize for size and vectorize at the same time. "
      "Enable vectorization of this loop with '#pragma clang loop "
      "vectorize(enable)' when compiling with -Os/-Oz",
      "NoTailLoopWithOptForSize", ORE, TheLoop);
  return FixedScalableVFPair::getNone();
}

ElementCount LoopVectorizationCostModel::getMaximizedVFForTarget(
    unsigned MaxTripCount, unsigned SmallestType, unsigned WidestType,
    ElementCount MaxSafeVF, bool FoldTailByMasking) {
  bool ComputeScalableMaxVF = MaxSafeVF.isScalable();
  const TypeSize WidestRegister = TTI.getRegisterBitWidth(
      ComputeScalableMaxVF ? TargetTransformInfo::RGK_ScalableVector
                           : TargetTransformInfo::RGK_FixedWidthVector);

  // Convenience function to return the minimum of two ElementCounts.
  auto MinVF = [](const ElementCount &LHS, const ElementCount &RHS) {
    assert((LHS.isScalable() == RHS.isScalable()) &&
           "Scalable flags must match");
    return ElementCount::isKnownLT(LHS, RHS) ? LHS : RHS;
  };

  // Ensure MaxVF is a power of 2; the dependence distance bound may not be.
  // Note that both WidestRegister and WidestType may not be a powers of 2.
  auto MaxVectorElementCount = ElementCount::get(
      llvm::bit_floor(WidestRegister.getKnownMinValue() / WidestType),
      ComputeScalableMaxVF);
  MaxVectorElementCount = MinVF(MaxVectorElementCount, MaxSafeVF);
  LLVM_DEBUG(dbgs() << "LV: The Widest register safe to use is: "
                    << (MaxVectorElementCount * WidestType) << " bits.\n");

  if (!MaxVectorElementCount) {
    LLVM_DEBUG(dbgs() << "LV: The target has no "
                      << (ComputeScalableMaxVF ? "scalable" : "fixed")
                      << " vector registers.\n");
    return ElementCount::getFixed(1);
  }

  unsigned WidestRegisterMinEC = MaxVectorElementCount.getKnownMinValue();
  if (MaxVectorElementCount.isScalable() &&
      TheFunction->hasFnAttribute(Attribute::VScaleRange)) {
    auto Attr = TheFunction->getFnAttribute(Attribute::VScaleRange);
    auto Min = Attr.getVScaleRangeMin();
    WidestRegisterMinEC *= Min;
  }

  // When a scalar epilogue is required, at least one iteration of the scalar
  // loop has to execute. Adjust MaxTripCount accordingly to avoid picking a
  // max VF that results in a dead vector loop.
  if (MaxTripCount > 0 && requiresScalarEpilogue(true))
    MaxTripCount -= 1;

  if (MaxTripCount && MaxTripCount <= WidestRegisterMinEC &&
      (!FoldTailByMasking || isPowerOf2_32(MaxTripCount))) {
    // If upper bound loop trip count (TC) is known at compile time there is no
    // point in choosing VF greater than TC (as done in the loop below). Select
    // maximum power of two which doesn't exceed TC. If MaxVectorElementCount is
    // scalable, we only fall back on a fixed VF when the TC is less than or
    // equal to the known number of lanes.
    auto ClampedUpperTripCount = llvm::bit_floor(MaxTripCount);
    LLVM_DEBUG(dbgs() << "LV: Clamping the MaxVF to maximum power of two not "
                         "exceeding the constant trip count: "
                      << ClampedUpperTripCount << "\n");
    return ElementCount::get(
        ClampedUpperTripCount,
        FoldTailByMasking ? MaxVectorElementCount.isScalable() : false);
  }

  TargetTransformInfo::RegisterKind RegKind =
      ComputeScalableMaxVF ? TargetTransformInfo::RGK_ScalableVector
                           : TargetTransformInfo::RGK_FixedWidthVector;
  ElementCount MaxVF = MaxVectorElementCount;
  if (MaximizeBandwidth ||
      (MaximizeBandwidth.getNumOccurrences() == 0 &&
       (TTI.shouldMaximizeVectorBandwidth(RegKind) ||
        (UseWiderVFIfCallVariantsPresent && Legal->hasVectorCallVariants())))) {
    auto MaxVectorElementCountMaxBW = ElementCount::get(
        llvm::bit_floor(WidestRegister.getKnownMinValue() / SmallestType),
        ComputeScalableMaxVF);
    MaxVectorElementCountMaxBW = MinVF(MaxVectorElementCountMaxBW, MaxSafeVF);

    // Collect all viable vectorization factors larger than the default MaxVF
    // (i.e. MaxVectorElementCount).
    SmallVector<ElementCount, 8> VFs;
    for (ElementCount VS = MaxVectorElementCount * 2;
         ElementCount::isKnownLE(VS, MaxVectorElementCountMaxBW); VS *= 2)
      VFs.push_back(VS);

    // For each VF calculate its register usage.
    auto RUs = calculateRegisterUsage(VFs);

    // Select the largest VF which doesn't require more registers than existing
    // ones.
    for (int I = RUs.size() - 1; I >= 0; --I) {
      const auto &MLU = RUs[I].MaxLocalUsers;
      if (all_of(MLU, [&](decltype(MLU.front()) &LU) {
            return LU.second <= TTI.getNumberOfRegisters(LU.first);
          })) {
        MaxVF = VFs[I];
        break;
      }
    }
    if (ElementCount MinVF =
            TTI.getMinimumVF(SmallestType, ComputeScalableMaxVF)) {
      if (ElementCount::isKnownLT(MaxVF, MinVF)) {
        LLVM_DEBUG(dbgs() << "LV: Overriding calculated MaxVF(" << MaxVF
                          << ") with target's minimum: " << MinVF << '\n');
        MaxVF = MinVF;
      }
    }

    // Invalidate any widening decisions we might have made, in case the loop
    // requires prediction (decided later), but we have already made some
    // load/store widening decisions.
    invalidateCostModelingDecisions();
  }
  return MaxVF;
}

/// Convenience function that returns the value of vscale_range iff
/// vscale_range.min == vscale_range.max or otherwise returns the value
/// returned by the corresponding TTI method.
static std::optional<unsigned>
getVScaleForTuning(const Loop *L, const TargetTransformInfo &TTI) {
  const Function *Fn = L->getHeader()->getParent();
  if (Fn->hasFnAttribute(Attribute::VScaleRange)) {
    auto Attr = Fn->getFnAttribute(Attribute::VScaleRange);
    auto Min = Attr.getVScaleRangeMin();
    auto Max = Attr.getVScaleRangeMax();
    if (Max && Min == Max)
      return Max;
  }

  return TTI.getVScaleForTuning();
}

bool LoopVectorizationPlanner::isMoreProfitable(
    const VectorizationFactor &A, const VectorizationFactor &B) const {
  InstructionCost CostA = A.Cost;
  InstructionCost CostB = B.Cost;

  unsigned MaxTripCount = PSE.getSE()->getSmallConstantMaxTripCount(OrigLoop);

  // Improve estimate for the vector width if it is scalable.
  unsigned EstimatedWidthA = A.Width.getKnownMinValue();
  unsigned EstimatedWidthB = B.Width.getKnownMinValue();
  if (std::optional<unsigned> VScale = getVScaleForTuning(OrigLoop, TTI)) {
    if (A.Width.isScalable())
      EstimatedWidthA *= *VScale;
    if (B.Width.isScalable())
      EstimatedWidthB *= *VScale;
  }

  // Assume vscale may be larger than 1 (or the value being tuned for),
  // so that scalable vectorization is slightly favorable over fixed-width
  // vectorization.
  bool PreferScalable = !TTI.preferFixedOverScalableIfEqualCost() &&
                        A.Width.isScalable() && !B.Width.isScalable();

  auto CmpFn = [PreferScalable](const InstructionCost &LHS,
                                const InstructionCost &RHS) {
    return PreferScalable ? LHS <= RHS : LHS < RHS;
  };

  // To avoid the need for FP division:
  //      (CostA / EstimatedWidthA) < (CostB / EstimatedWidthB)
  // <=>  (CostA * EstimatedWidthB) < (CostB * EstimatedWidthA)
  if (!MaxTripCount)
    return CmpFn(CostA * EstimatedWidthB, CostB * EstimatedWidthA);

  auto GetCostForTC = [MaxTripCount, this](unsigned VF,
                                           InstructionCost VectorCost,
                                           InstructionCost ScalarCost) {
    // If the trip count is a known (possibly small) constant, the trip count
    // will be rounded up to an integer number of iterations under
    // FoldTailByMasking. The total cost in that case will be
    // VecCost*ceil(TripCount/VF). When not folding the tail, the total
    // cost will be VecCost*floor(TC/VF) + ScalarCost*(TC%VF). There will be
    // some extra overheads, but for the purpose of comparing the costs of
    // different VFs we can use this to compare the total loop-body cost
    // expected after vectorization.
    if (CM.foldTailByMasking())
      return VectorCost * divideCeil(MaxTripCount, VF);
    return VectorCost * (MaxTripCount / VF) + ScalarCost * (MaxTripCount % VF);
  };

  auto RTCostA = GetCostForTC(EstimatedWidthA, CostA, A.ScalarCost);
  auto RTCostB = GetCostForTC(EstimatedWidthB, CostB, B.ScalarCost);
  return CmpFn(RTCostA, RTCostB);
}

static void emitInvalidCostRemarks(SmallVector<InstructionVFPair> InvalidCosts,
                                   OptimizationRemarkEmitter *ORE,
                                   Loop *TheLoop) {
  if (InvalidCosts.empty())
    return;

  // Emit a report of VFs with invalid costs in the loop.

  // Group the remarks per instruction, keeping the instruction order from
  // InvalidCosts.
  std::map<Instruction *, unsigned> Numbering;
  unsigned I = 0;
  for (auto &Pair : InvalidCosts)
    if (!Numbering.count(Pair.first))
      Numbering[Pair.first] = I++;

  // Sort the list, first on instruction(number) then on VF.
  sort(InvalidCosts, [&Numbering](InstructionVFPair &A, InstructionVFPair &B) {
    if (Numbering[A.first] != Numbering[B.first])
      return Numbering[A.first] < Numbering[B.first];
    const auto &LHS = A.second;
    const auto &RHS = B.second;
    return std::make_tuple(LHS.isScalable(), LHS.getKnownMinValue()) <
           std::make_tuple(RHS.isScalable(), RHS.getKnownMinValue());
  });

  // For a list of ordered instruction-vf pairs:
  //   [(load, vf1), (load, vf2), (store, vf1)]
  // Group the instructions together to emit separate remarks for:
  //   load  (vf1, vf2)
  //   store (vf1)
  auto Tail = ArrayRef<InstructionVFPair>(InvalidCosts);
  auto Subset = ArrayRef<InstructionVFPair>();
  do {
    if (Subset.empty())
      Subset = Tail.take_front(1);

    Instruction *I = Subset.front().first;

    // If the next instruction is different, or if there are no other pairs,
    // emit a remark for the collated subset. e.g.
    //   [(load, vf1), (load, vf2))]
    // to emit:
    //  remark: invalid costs for 'load' at VF=(vf, vf2)
    if (Subset == Tail || Tail[Subset.size()].first != I) {
      std::string OutString;
      raw_string_ostream OS(OutString);
      assert(!Subset.empty() && "Unexpected empty range");
      OS << "Instruction with invalid costs prevented vectorization at VF=(";
      for (const auto &Pair : Subset)
        OS << (Pair.second == Subset.front().second ? "" : ", ") << Pair.second;
      OS << "):";
      if (auto *CI = dyn_cast<CallInst>(I))
        OS << " call to " << CI->getCalledFunction()->getName();
      else
        OS << " " << I->getOpcodeName();
      OS.flush();
      reportVectorizationInfo(OutString, "InvalidCost", ORE, TheLoop, I);
      Tail = Tail.drop_front(Subset.size());
      Subset = {};
    } else
      // Grow the subset by one element
      Subset = Tail.take_front(Subset.size() + 1);
  } while (!Tail.empty());
}

/// Check if any recipe of \p Plan will generate a vector value, which will be
/// assigned a vector register.
static bool willGenerateVectors(VPlan &Plan, ElementCount VF,
                                const TargetTransformInfo &TTI) {
  assert(VF.isVector() && "Checking a scalar VF?");
  VPTypeAnalysis TypeInfo(Plan.getCanonicalIV()->getScalarType(),
                          Plan.getCanonicalIV()->getScalarType()->getContext());
  DenseSet<VPRecipeBase *> EphemeralRecipes;
  collectEphemeralRecipesForVPlan(Plan, EphemeralRecipes);
  // Set of already visited types.
  DenseSet<Type *> Visited;
  for (VPBasicBlock *VPBB : VPBlockUtils::blocksOnly<VPBasicBlock>(
           vp_depth_first_shallow(Plan.getVectorLoopRegion()->getEntry()))) {
    for (VPRecipeBase &R : *VPBB) {
      if (EphemeralRecipes.contains(&R))
        continue;
      // Continue early if the recipe is considered to not produce a vector
      //  result. Note that this includes VPInstruction where some opcodes may
      // produce a vector, to preserve existing behavior as VPInstructions model
      // aspects not directly mapped to existing IR instructions.
      switch (R.getVPDefID()) {
      case VPDef::VPDerivedIVSC:
      case VPDef::VPScalarIVStepsSC:
      case VPDef::VPScalarCastSC:
      case VPDef::VPReplicateSC:
      case VPDef::VPInstructionSC:
      case VPDef::VPCanonicalIVPHISC:
      case VPDef::VPVectorPointerSC:
      case VPDef::VPExpandSCEVSC:
      case VPDef::VPEVLBasedIVPHISC:
      case VPDef::VPPredInstPHISC:
      case VPDef::VPBranchOnMaskSC:
        continue;
      case VPDef::VPReductionSC:
      case VPDef::VPActiveLaneMaskPHISC:
      case VPDef::VPWidenCallSC:
      case VPDef::VPWidenCanonicalIVSC:
      case VPDef::VPWidenCastSC:
      case VPDef::VPWidenGEPSC:
      case VPDef::VPWidenSC:
      case VPDef::VPWidenSelectSC:
      case VPDef::VPBlendSC:
      case VPDef::VPFirstOrderRecurrencePHISC:
      case VPDef::VPWidenPHISC:
      case VPDef::VPWidenIntOrFpInductionSC:
      case VPDef::VPWidenPointerInductionSC:
      case VPDef::VPReductionPHISC:
      case VPDef::VPInterleaveSC:
      case VPDef::VPWidenLoadEVLSC:
      case VPDef::VPWidenLoadSC:
      case VPDef::VPWidenStoreEVLSC:
      case VPDef::VPWidenStoreSC:
        break;
      default:
        llvm_unreachable("unhandled recipe");
      }

      auto WillWiden = [&TTI, VF](Type *ScalarTy) {
        Type *VectorTy = ToVectorTy(ScalarTy, VF);
        unsigned NumLegalParts = TTI.getNumberOfParts(VectorTy);
        if (!NumLegalParts)
          return false;
        if (VF.isScalable()) {
          // <vscale x 1 x iN> is assumed to be profitable over iN because
          // scalable registers are a distinct register class from scalar
          // ones. If we ever find a target which wants to lower scalable
          // vectors back to scalars, we'll need to update this code to
          // explicitly ask TTI about the register class uses for each part.
          return NumLegalParts <= VF.getKnownMinValue();
        }
        // Two or more parts that share a register - are vectorized.
        return NumLegalParts < VF.getKnownMinValue();
      };

      // If no def nor is a store, e.g., branches, continue - no value to check.
      if (R.getNumDefinedValues() == 0 &&
          !isa<VPWidenStoreRecipe, VPWidenStoreEVLRecipe, VPInterleaveRecipe>(
              &R))
        continue;
      // For multi-def recipes, currently only interleaved loads, suffice to
      // check first def only.
      // For stores check their stored value; for interleaved stores suffice
      // the check first stored value only. In all cases this is the second
      // operand.
      VPValue *ToCheck =
          R.getNumDefinedValues() >= 1 ? R.getVPValue(0) : R.getOperand(1);
      Type *ScalarTy = TypeInfo.inferScalarType(ToCheck);
      if (!Visited.insert({ScalarTy}).second)
        continue;
      if (WillWiden(ScalarTy))
        return true;
    }
  }

  return false;
}

VectorizationFactor LoopVectorizationPlanner::selectVectorizationFactor() {
  InstructionCost ExpectedCost = CM.expectedCost(ElementCount::getFixed(1));
  LLVM_DEBUG(dbgs() << "LV: Scalar loop costs: " << ExpectedCost << ".\n");
  assert(ExpectedCost.isValid() && "Unexpected invalid cost for scalar loop");
  assert(any_of(VPlans,
                [](std::unique_ptr<VPlan> &P) {
                  return P->hasVF(ElementCount::getFixed(1));
                }) &&
         "Expected Scalar VF to be a candidate");

  const VectorizationFactor ScalarCost(ElementCount::getFixed(1), ExpectedCost,
                                       ExpectedCost);
  VectorizationFactor ChosenFactor = ScalarCost;

  bool ForceVectorization = Hints.getForce() == LoopVectorizeHints::FK_Enabled;
  if (ForceVectorization &&
      (VPlans.size() > 1 || !VPlans[0]->hasScalarVFOnly())) {
    // Ignore scalar width, because the user explicitly wants vectorization.
    // Initialize cost to max so that VF = 2 is, at least, chosen during cost
    // evaluation.
    ChosenFactor.Cost = InstructionCost::getMax();
  }

  SmallVector<InstructionVFPair> InvalidCosts;
  for (auto &P : VPlans) {
    for (ElementCount VF : P->vectorFactors()) {
      // The cost for scalar VF=1 is already calculated, so ignore it.
      if (VF.isScalar())
        continue;

      InstructionCost C = CM.expectedCost(VF, &InvalidCosts);
      VectorizationFactor Candidate(VF, C, ScalarCost.ScalarCost);

#ifndef NDEBUG
      unsigned AssumedMinimumVscale =
          getVScaleForTuning(OrigLoop, TTI).value_or(1);
      unsigned Width =
          Candidate.Width.isScalable()
              ? Candidate.Width.getKnownMinValue() * AssumedMinimumVscale
              : Candidate.Width.getFixedValue();
      LLVM_DEBUG(dbgs() << "LV: Vector loop of width " << VF
                        << " costs: " << (Candidate.Cost / Width));
      if (VF.isScalable())
        LLVM_DEBUG(dbgs() << " (assuming a minimum vscale of "
                          << AssumedMinimumVscale << ")");
      LLVM_DEBUG(dbgs() << ".\n");
#endif

      if (!ForceVectorization && !willGenerateVectors(*P, VF, TTI)) {
        LLVM_DEBUG(
            dbgs()
            << "LV: Not considering vector loop of width " << VF
            << " because it will not generate any vector instructions.\n");
        continue;
      }

      // If profitable add it to ProfitableVF list.
      if (isMoreProfitable(Candidate, ScalarCost))
        ProfitableVFs.push_back(Candidate);

      if (isMoreProfitable(Candidate, ChosenFactor))
        ChosenFactor = Candidate;
    }
  }

  emitInvalidCostRemarks(InvalidCosts, ORE, OrigLoop);

  if (!EnableCondStoresVectorization && CM.hasPredStores()) {
    reportVectorizationFailure(
        "There are conditional stores.",
        "store that is conditionally executed prevents vectorization",
        "ConditionalStore", ORE, OrigLoop);
    ChosenFactor = ScalarCost;
  }

  LLVM_DEBUG(if (ForceVectorization && !ChosenFactor.Width.isScalar() &&
                 !isMoreProfitable(ChosenFactor, ScalarCost)) dbgs()
             << "LV: Vectorization seems to be not beneficial, "
             << "but was forced by a user.\n");
  LLVM_DEBUG(dbgs() << "LV: Selecting VF: " << ChosenFactor.Width << ".\n");
  return ChosenFactor;
}

bool LoopVectorizationPlanner::isCandidateForEpilogueVectorization(
    ElementCount VF) const {
  // Cross iteration phis such as reductions need special handling and are
  // currently unsupported.
  if (any_of(OrigLoop->getHeader()->phis(),
             [&](PHINode &Phi) { return Legal->isFixedOrderRecurrence(&Phi); }))
    return false;

  // Phis with uses outside of the loop require special handling and are
  // currently unsupported.
  for (const auto &Entry : Legal->getInductionVars()) {
    // Look for uses of the value of the induction at the last iteration.
    Value *PostInc =
        Entry.first->getIncomingValueForBlock(OrigLoop->getLoopLatch());
    for (User *U : PostInc->users())
      if (!OrigLoop->contains(cast<Instruction>(U)))
        return false;
    // Look for uses of penultimate value of the induction.
    for (User *U : Entry.first->users())
      if (!OrigLoop->contains(cast<Instruction>(U)))
        return false;
  }

  // Epilogue vectorization code has not been auditted to ensure it handles
  // non-latch exits properly.  It may be fine, but it needs auditted and
  // tested.
  if (OrigLoop->getExitingBlock() != OrigLoop->getLoopLatch())
    return false;

  return true;
}

bool LoopVectorizationCostModel::isEpilogueVectorizationProfitable(
    const ElementCount VF) const {
  // FIXME: We need a much better cost-model to take different parameters such
  // as register pressure, code size increase and cost of extra branches into
  // account. For now we apply a very crude heuristic and only consider loops
  // with vectorization factors larger than a certain value.

  // Allow the target to opt out entirely.
  if (!TTI.preferEpilogueVectorization())
    return false;

  // We also consider epilogue vectorization unprofitable for targets that don't
  // consider interleaving beneficial (eg. MVE).
  if (TTI.getMaxInterleaveFactor(VF) <= 1)
    return false;

  unsigned Multiplier = 1;
  if (VF.isScalable())
    Multiplier = getVScaleForTuning(TheLoop, TTI).value_or(1);
  if ((Multiplier * VF.getKnownMinValue()) >= EpilogueVectorizationMinVF)
    return true;
  return false;
}

VectorizationFactor LoopVectorizationPlanner::selectEpilogueVectorizationFactor(
    const ElementCount MainLoopVF, unsigned IC) {
  VectorizationFactor Result = VectorizationFactor::Disabled();
  if (!EnableEpilogueVectorization) {
    LLVM_DEBUG(dbgs() << "LEV: Epilogue vectorization is disabled.\n");
    return Result;
  }

  if (!CM.isScalarEpilogueAllowed()) {
    LLVM_DEBUG(dbgs() << "LEV: Unable to vectorize epilogue because no "
                         "epilogue is allowed.\n");
    return Result;
  }

  // Not really a cost consideration, but check for unsupported cases here to
  // simplify the logic.
  if (!isCandidateForEpilogueVectorization(MainLoopVF)) {
    LLVM_DEBUG(dbgs() << "LEV: Unable to vectorize epilogue because the loop "
                         "is not a supported candidate.\n");
    return Result;
  }

  if (EpilogueVectorizationForceVF > 1) {
    LLVM_DEBUG(dbgs() << "LEV: Epilogue vectorization factor is forced.\n");
    ElementCount ForcedEC = ElementCount::getFixed(EpilogueVectorizationForceVF);
    if (hasPlanWithVF(ForcedEC))
      return {ForcedEC, 0, 0};
    else {
      LLVM_DEBUG(dbgs() << "LEV: Epilogue vectorization forced factor is not "
                           "viable.\n");
      return Result;
    }
  }

  if (OrigLoop->getHeader()->getParent()->hasOptSize() ||
      OrigLoop->getHeader()->getParent()->hasMinSize()) {
    LLVM_DEBUG(
        dbgs() << "LEV: Epilogue vectorization skipped due to opt for size.\n");
    return Result;
  }

  if (!CM.isEpilogueVectorizationProfitable(MainLoopVF)) {
    LLVM_DEBUG(dbgs() << "LEV: Epilogue vectorization is not profitable for "
                         "this loop\n");
    return Result;
  }

  // If MainLoopVF = vscale x 2, and vscale is expected to be 4, then we know
  // the main loop handles 8 lanes per iteration. We could still benefit from
  // vectorizing the epilogue loop with VF=4.
  ElementCount EstimatedRuntimeVF = MainLoopVF;
  if (MainLoopVF.isScalable()) {
    EstimatedRuntimeVF = ElementCount::getFixed(MainLoopVF.getKnownMinValue());
    if (std::optional<unsigned> VScale = getVScaleForTuning(OrigLoop, TTI))
      EstimatedRuntimeVF *= *VScale;
  }

  ScalarEvolution &SE = *PSE.getSE();
  Type *TCType = Legal->getWidestInductionType();
  const SCEV *RemainingIterations = nullptr;
  for (auto &NextVF : ProfitableVFs) {
    // Skip candidate VFs without a corresponding VPlan.
    if (!hasPlanWithVF(NextVF.Width))
      continue;

    // Skip candidate VFs with widths >= the estimate runtime VF (scalable
    // vectors) or the VF of the main loop (fixed vectors).
    if ((!NextVF.Width.isScalable() && MainLoopVF.isScalable() &&
         ElementCount::isKnownGE(NextVF.Width, EstimatedRuntimeVF)) ||
        ElementCount::isKnownGE(NextVF.Width, MainLoopVF))
      continue;

    // If NextVF is greater than the number of remaining iterations, the
    // epilogue loop would be dead. Skip such factors.
    if (!MainLoopVF.isScalable() && !NextVF.Width.isScalable()) {
      // TODO: extend to support scalable VFs.
      if (!RemainingIterations) {
        const SCEV *TC = createTripCountSCEV(TCType, PSE, OrigLoop);
        RemainingIterations = SE.getURemExpr(
            TC, SE.getConstant(TCType, MainLoopVF.getKnownMinValue() * IC));
      }
      if (SE.isKnownPredicate(
              CmpInst::ICMP_UGT,
              SE.getConstant(TCType, NextVF.Width.getKnownMinValue()),
              RemainingIterations))
        continue;
    }

    if (Result.Width.isScalar() || isMoreProfitable(NextVF, Result))
      Result = NextVF;
  }

  if (Result != VectorizationFactor::Disabled())
    LLVM_DEBUG(dbgs() << "LEV: Vectorizing epilogue loop with VF = "
                      << Result.Width << "\n");
  return Result;
}

std::pair<unsigned, unsigned>
LoopVectorizationCostModel::getSmallestAndWidestTypes() {
  unsigned MinWidth = -1U;
  unsigned MaxWidth = 8;
  const DataLayout &DL = TheFunction->getDataLayout();
  // For in-loop reductions, no element types are added to ElementTypesInLoop
  // if there are no loads/stores in the loop. In this case, check through the
  // reduction variables to determine the maximum width.
  if (ElementTypesInLoop.empty() && !Legal->getReductionVars().empty()) {
    // Reset MaxWidth so that we can find the smallest type used by recurrences
    // in the loop.
    MaxWidth = -1U;
    for (const auto &PhiDescriptorPair : Legal->getReductionVars()) {
      const RecurrenceDescriptor &RdxDesc = PhiDescriptorPair.second;
      // When finding the min width used by the recurrence we need to account
      // for casts on the input operands of the recurrence.
      MaxWidth = std::min<unsigned>(
          MaxWidth, std::min<unsigned>(
                        RdxDesc.getMinWidthCastToRecurrenceTypeInBits(),
                        RdxDesc.getRecurrenceType()->getScalarSizeInBits()));
    }
  } else {
    for (Type *T : ElementTypesInLoop) {
      MinWidth = std::min<unsigned>(
          MinWidth, DL.getTypeSizeInBits(T->getScalarType()).getFixedValue());
      MaxWidth = std::max<unsigned>(
          MaxWidth, DL.getTypeSizeInBits(T->getScalarType()).getFixedValue());
    }
  }
  return {MinWidth, MaxWidth};
}

void LoopVectorizationCostModel::collectElementTypesForWidening() {
  ElementTypesInLoop.clear();
  // For each block.
  for (BasicBlock *BB : TheLoop->blocks()) {
    // For each instruction in the loop.
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      Type *T = I.getType();

      // Skip ignored values.
      if (ValuesToIgnore.count(&I))
        continue;

      // Only examine Loads, Stores and PHINodes.
      if (!isa<LoadInst>(I) && !isa<StoreInst>(I) && !isa<PHINode>(I))
        continue;

      // Examine PHI nodes that are reduction variables. Update the type to
      // account for the recurrence type.
      if (auto *PN = dyn_cast<PHINode>(&I)) {
        if (!Legal->isReductionVariable(PN))
          continue;
        const RecurrenceDescriptor &RdxDesc =
            Legal->getReductionVars().find(PN)->second;
        if (PreferInLoopReductions || useOrderedReductions(RdxDesc) ||
            TTI.preferInLoopReduction(RdxDesc.getOpcode(),
                                      RdxDesc.getRecurrenceType(),
                                      TargetTransformInfo::ReductionFlags()))
          continue;
        T = RdxDesc.getRecurrenceType();
      }

      // Examine the stored values.
      if (auto *ST = dyn_cast<StoreInst>(&I))
        T = ST->getValueOperand()->getType();

      assert(T->isSized() &&
             "Expected the load/store/recurrence type to be sized");

      ElementTypesInLoop.insert(T);
    }
  }
}

unsigned
LoopVectorizationCostModel::selectInterleaveCount(ElementCount VF,
                                                  InstructionCost LoopCost) {
  // -- The interleave heuristics --
  // We interleave the loop in order to expose ILP and reduce the loop overhead.
  // There are many micro-architectural considerations that we can't predict
  // at this level. For example, frontend pressure (on decode or fetch) due to
  // code size, or the number and capabilities of the execution ports.
  //
  // We use the following heuristics to select the interleave count:
  // 1. If the code has reductions, then we interleave to break the cross
  // iteration dependency.
  // 2. If the loop is really small, then we interleave to reduce the loop
  // overhead.
  // 3. We don't interleave if we think that we will spill registers to memory
  // due to the increased register pressure.

  if (!isScalarEpilogueAllowed())
    return 1;

  // Do not interleave if EVL is preferred and no User IC is specified.
  if (foldTailWithEVL()) {
    LLVM_DEBUG(dbgs() << "LV: Preference for VP intrinsics indicated. "
                         "Unroll factor forced to be 1.\n");
    return 1;
  }

  // We used the distance for the interleave count.
  if (!Legal->isSafeForAnyVectorWidth())
    return 1;

  auto BestKnownTC = getSmallBestKnownTC(*PSE.getSE(), TheLoop);
  const bool HasReductions = !Legal->getReductionVars().empty();

  // If we did not calculate the cost for VF (because the user selected the VF)
  // then we calculate the cost of VF here.
  if (LoopCost == 0) {
    LoopCost = expectedCost(VF);
    assert(LoopCost.isValid() && "Expected to have chosen a VF with valid cost");

    // Loop body is free and there is no need for interleaving.
    if (LoopCost == 0)
      return 1;
  }

  RegisterUsage R = calculateRegisterUsage({VF})[0];
  // We divide by these constants so assume that we have at least one
  // instruction that uses at least one register.
  for (auto& pair : R.MaxLocalUsers) {
    pair.second = std::max(pair.second, 1U);
  }

  // We calculate the interleave count using the following formula.
  // Subtract the number of loop invariants from the number of available
  // registers. These registers are used by all of the interleaved instances.
  // Next, divide the remaining registers by the number of registers that is
  // required by the loop, in order to estimate how many parallel instances
  // fit without causing spills. All of this is rounded down if necessary to be
  // a power of two. We want power of two interleave count to simplify any
  // addressing operations or alignment considerations.
  // We also want power of two interleave counts to ensure that the induction
  // variable of the vector loop wraps to zero, when tail is folded by masking;
  // this currently happens when OptForSize, in which case IC is set to 1 above.
  unsigned IC = UINT_MAX;

  for (auto& pair : R.MaxLocalUsers) {
    unsigned TargetNumRegisters = TTI.getNumberOfRegisters(pair.first);
    LLVM_DEBUG(dbgs() << "LV: The target has " << TargetNumRegisters
                      << " registers of "
                      << TTI.getRegisterClassName(pair.first) << " register class\n");
    if (VF.isScalar()) {
      if (ForceTargetNumScalarRegs.getNumOccurrences() > 0)
        TargetNumRegisters = ForceTargetNumScalarRegs;
    } else {
      if (ForceTargetNumVectorRegs.getNumOccurrences() > 0)
        TargetNumRegisters = ForceTargetNumVectorRegs;
    }
    unsigned MaxLocalUsers = pair.second;
    unsigned LoopInvariantRegs = 0;
    if (R.LoopInvariantRegs.find(pair.first) != R.LoopInvariantRegs.end())
      LoopInvariantRegs = R.LoopInvariantRegs[pair.first];

    unsigned TmpIC = llvm::bit_floor((TargetNumRegisters - LoopInvariantRegs) /
                                     MaxLocalUsers);
    // Don't count the induction variable as interleaved.
    if (EnableIndVarRegisterHeur) {
      TmpIC = llvm::bit_floor((TargetNumRegisters - LoopInvariantRegs - 1) /
                              std::max(1U, (MaxLocalUsers - 1)));
    }

    IC = std::min(IC, TmpIC);
  }

  // Clamp the interleave ranges to reasonable counts.
  unsigned MaxInterleaveCount = TTI.getMaxInterleaveFactor(VF);

  // Check if the user has overridden the max.
  if (VF.isScalar()) {
    if (ForceTargetMaxScalarInterleaveFactor.getNumOccurrences() > 0)
      MaxInterleaveCount = ForceTargetMaxScalarInterleaveFactor;
  } else {
    if (ForceTargetMaxVectorInterleaveFactor.getNumOccurrences() > 0)
      MaxInterleaveCount = ForceTargetMaxVectorInterleaveFactor;
  }

  unsigned EstimatedVF = VF.getKnownMinValue();
  if (VF.isScalable()) {
    if (std::optional<unsigned> VScale = getVScaleForTuning(TheLoop, TTI))
      EstimatedVF *= *VScale;
  }
  assert(EstimatedVF >= 1 && "Estimated VF shouldn't be less than 1");

  unsigned KnownTC = PSE.getSE()->getSmallConstantTripCount(TheLoop);
  if (KnownTC > 0) {
    // At least one iteration must be scalar when this constraint holds. So the
    // maximum available iterations for interleaving is one less.
    unsigned AvailableTC =
        requiresScalarEpilogue(VF.isVector()) ? KnownTC - 1 : KnownTC;

    // If trip count is known we select between two prospective ICs, where
    // 1) the aggressive IC is capped by the trip count divided by VF
    // 2) the conservative IC is capped by the trip count divided by (VF * 2)
    // The final IC is selected in a way that the epilogue loop trip count is
    // minimized while maximizing the IC itself, so that we either run the
    // vector loop at least once if it generates a small epilogue loop, or else
    // we run the vector loop at least twice.

    unsigned InterleaveCountUB = bit_floor(
        std::max(1u, std::min(AvailableTC / EstimatedVF, MaxInterleaveCount)));
    unsigned InterleaveCountLB = bit_floor(std::max(
        1u, std::min(AvailableTC / (EstimatedVF * 2), MaxInterleaveCount)));
    MaxInterleaveCount = InterleaveCountLB;

    if (InterleaveCountUB != InterleaveCountLB) {
      unsigned TailTripCountUB =
          (AvailableTC % (EstimatedVF * InterleaveCountUB));
      unsigned TailTripCountLB =
          (AvailableTC % (EstimatedVF * InterleaveCountLB));
      // If both produce same scalar tail, maximize the IC to do the same work
      // in fewer vector loop iterations
      if (TailTripCountUB == TailTripCountLB)
        MaxInterleaveCount = InterleaveCountUB;
    }
  } else if (BestKnownTC && *BestKnownTC > 0) {
    // At least one iteration must be scalar when this constraint holds. So the
    // maximum available iterations for interleaving is one less.
    unsigned AvailableTC = requiresScalarEpilogue(VF.isVector())
                               ? (*BestKnownTC) - 1
                               : *BestKnownTC;

    // If trip count is an estimated compile time constant, limit the
    // IC to be capped by the trip count divided by VF * 2, such that the vector
    // loop runs at least twice to make interleaving seem profitable when there
    // is an epilogue loop present. Since exact Trip count is not known we
    // choose to be conservative in our IC estimate.
    MaxInterleaveCount = bit_floor(std::max(
        1u, std::min(AvailableTC / (EstimatedVF * 2), MaxInterleaveCount)));
  }

  assert(MaxInterleaveCount > 0 &&
         "Maximum interleave count must be greater than 0");

  // Clamp the calculated IC to be between the 1 and the max interleave count
  // that the target and trip count allows.
  if (IC > MaxInterleaveCount)
    IC = MaxInterleaveCount;
  else
    // Make sure IC is greater than 0.
    IC = std::max(1u, IC);

  assert(IC > 0 && "Interleave count must be greater than 0.");

  // Interleave if we vectorized this loop and there is a reduction that could
  // benefit from interleaving.
  if (VF.isVector() && HasReductions) {
    LLVM_DEBUG(dbgs() << "LV: Interleaving because of reductions.\n");
    return IC;
  }

  // For any scalar loop that either requires runtime checks or predication we
  // are better off leaving this to the unroller. Note that if we've already
  // vectorized the loop we will have done the runtime check and so interleaving
  // won't require further checks.
  bool ScalarInterleavingRequiresPredication =
      (VF.isScalar() && any_of(TheLoop->blocks(), [this](BasicBlock *BB) {
         return Legal->blockNeedsPredication(BB);
       }));
  bool ScalarInterleavingRequiresRuntimePointerCheck =
      (VF.isScalar() && Legal->getRuntimePointerChecking()->Need);

  // We want to interleave small loops in order to reduce the loop overhead and
  // potentially expose ILP opportunities.
  LLVM_DEBUG(dbgs() << "LV: Loop cost is " << LoopCost << '\n'
                    << "LV: IC is " << IC << '\n'
                    << "LV: VF is " << VF << '\n');
  const bool AggressivelyInterleaveReductions =
      TTI.enableAggressiveInterleaving(HasReductions);
  if (!ScalarInterleavingRequiresRuntimePointerCheck &&
      !ScalarInterleavingRequiresPredication && LoopCost < SmallLoopCost) {
    // We assume that the cost overhead is 1 and we use the cost model
    // to estimate the cost of the loop and interleave until the cost of the
    // loop overhead is about 5% of the cost of the loop.
    unsigned SmallIC = std::min(IC, (unsigned)llvm::bit_floor<uint64_t>(
                                        SmallLoopCost / *LoopCost.getValue()));

    // Interleave until store/load ports (estimated by max interleave count) are
    // saturated.
    unsigned NumStores = Legal->getNumStores();
    unsigned NumLoads = Legal->getNumLoads();
    unsigned StoresIC = IC / (NumStores ? NumStores : 1);
    unsigned LoadsIC = IC / (NumLoads ? NumLoads : 1);

    // There is little point in interleaving for reductions containing selects
    // and compares when VF=1 since it may just create more overhead than it's
    // worth for loops with small trip counts. This is because we still have to
    // do the final reduction after the loop.
    bool HasSelectCmpReductions =
        HasReductions &&
        any_of(Legal->getReductionVars(), [&](auto &Reduction) -> bool {
          const RecurrenceDescriptor &RdxDesc = Reduction.second;
          return RecurrenceDescriptor::isAnyOfRecurrenceKind(
              RdxDesc.getRecurrenceKind());
        });
    if (HasSelectCmpReductions) {
      LLVM_DEBUG(dbgs() << "LV: Not interleaving select-cmp reductions.\n");
      return 1;
    }

    // If we have a scalar reduction (vector reductions are already dealt with
    // by this point), we can increase the critical path length if the loop
    // we're interleaving is inside another loop. For tree-wise reductions
    // set the limit to 2, and for ordered reductions it's best to disable
    // interleaving entirely.
    if (HasReductions && TheLoop->getLoopDepth() > 1) {
      bool HasOrderedReductions =
          any_of(Legal->getReductionVars(), [&](auto &Reduction) -> bool {
            const RecurrenceDescriptor &RdxDesc = Reduction.second;
            return RdxDesc.isOrdered();
          });
      if (HasOrderedReductions) {
        LLVM_DEBUG(
            dbgs() << "LV: Not interleaving scalar ordered reductions.\n");
        return 1;
      }

      unsigned F = static_cast<unsigned>(MaxNestedScalarReductionIC);
      SmallIC = std::min(SmallIC, F);
      StoresIC = std::min(StoresIC, F);
      LoadsIC = std::min(LoadsIC, F);
    }

    if (EnableLoadStoreRuntimeInterleave &&
        std::max(StoresIC, LoadsIC) > SmallIC) {
      LLVM_DEBUG(
          dbgs() << "LV: Interleaving to saturate store or load ports.\n");
      return std::max(StoresIC, LoadsIC);
    }

    // If there are scalar reductions and TTI has enabled aggressive
    // interleaving for reductions, we will interleave to expose ILP.
    if (VF.isScalar() && AggressivelyInterleaveReductions) {
      LLVM_DEBUG(dbgs() << "LV: Interleaving to expose ILP.\n");
      // Interleave no less than SmallIC but not as aggressive as the normal IC
      // to satisfy the rare situation when resources are too limited.
      return std::max(IC / 2, SmallIC);
    } else {
      LLVM_DEBUG(dbgs() << "LV: Interleaving to reduce branch cost.\n");
      return SmallIC;
    }
  }

  // Interleave if this is a large loop (small loops are already dealt with by
  // this point) that could benefit from interleaving.
  if (AggressivelyInterleaveReductions) {
    LLVM_DEBUG(dbgs() << "LV: Interleaving to expose ILP.\n");
    return IC;
  }

  LLVM_DEBUG(dbgs() << "LV: Not Interleaving.\n");
  return 1;
}

SmallVector<LoopVectorizationCostModel::RegisterUsage, 8>
LoopVectorizationCostModel::calculateRegisterUsage(ArrayRef<ElementCount> VFs) {
  // This function calculates the register usage by measuring the highest number
  // of values that are alive at a single location. Obviously, this is a very
  // rough estimation. We scan the loop in a topological order in order and
  // assign a number to each instruction. We use RPO to ensure that defs are
  // met before their users. We assume that each instruction that has in-loop
  // users starts an interval. We record every time that an in-loop value is
  // used, so we have a list of the first and last occurrences of each
  // instruction. Next, we transpose this data structure into a multi map that
  // holds the list of intervals that *end* at a specific location. This multi
  // map allows us to perform a linear search. We scan the instructions linearly
  // and record each time that a new interval starts, by placing it in a set.
  // If we find this value in the multi-map then we remove it from the set.
  // The max register usage is the maximum size of the set.
  // We also search for instructions that are defined outside the loop, but are
  // used inside the loop. We need this number separately from the max-interval
  // usage number because when we unroll, loop-invariant values do not take
  // more register.
  LoopBlocksDFS DFS(TheLoop);
  DFS.perform(LI);

  RegisterUsage RU;

  // Each 'key' in the map opens a new interval. The values
  // of the map are the index of the 'last seen' usage of the
  // instruction that is the key.
  using IntervalMap = DenseMap<Instruction *, unsigned>;

  // Maps instruction to its index.
  SmallVector<Instruction *, 64> IdxToInstr;
  // Marks the end of each interval.
  IntervalMap EndPoint;
  // Saves the list of instruction indices that are used in the loop.
  SmallPtrSet<Instruction *, 8> Ends;
  // Saves the list of values that are used in the loop but are defined outside
  // the loop (not including non-instruction values such as arguments and
  // constants).
  SmallSetVector<Instruction *, 8> LoopInvariants;

  for (BasicBlock *BB : make_range(DFS.beginRPO(), DFS.endRPO())) {
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      IdxToInstr.push_back(&I);

      // Save the end location of each USE.
      for (Value *U : I.operands()) {
        auto *Instr = dyn_cast<Instruction>(U);

        // Ignore non-instruction values such as arguments, constants, etc.
        // FIXME: Might need some motivation why these values are ignored. If
        // for example an argument is used inside the loop it will increase the
        // register pressure (so shouldn't we add it to LoopInvariants).
        if (!Instr)
          continue;

        // If this instruction is outside the loop then record it and continue.
        if (!TheLoop->contains(Instr)) {
          LoopInvariants.insert(Instr);
          continue;
        }

        // Overwrite previous end points.
        EndPoint[Instr] = IdxToInstr.size();
        Ends.insert(Instr);
      }
    }
  }

  // Saves the list of intervals that end with the index in 'key'.
  using InstrList = SmallVector<Instruction *, 2>;
  DenseMap<unsigned, InstrList> TransposeEnds;

  // Transpose the EndPoints to a list of values that end at each index.
  for (auto &Interval : EndPoint)
    TransposeEnds[Interval.second].push_back(Interval.first);

  SmallPtrSet<Instruction *, 8> OpenIntervals;
  SmallVector<RegisterUsage, 8> RUs(VFs.size());
  SmallVector<SmallMapVector<unsigned, unsigned, 4>, 8> MaxUsages(VFs.size());

  LLVM_DEBUG(dbgs() << "LV(REG): Calculating max register usage:\n");

  const auto &TTICapture = TTI;
  auto GetRegUsage = [&TTICapture](Type *Ty, ElementCount VF) -> unsigned {
    if (Ty->isTokenTy() || !VectorType::isValidElementType(Ty))
      return 0;
    return TTICapture.getRegUsageForType(VectorType::get(Ty, VF));
  };

  for (unsigned int i = 0, s = IdxToInstr.size(); i < s; ++i) {
    Instruction *I = IdxToInstr[i];

    // Remove all of the instructions that end at this location.
    InstrList &List = TransposeEnds[i];
    for (Instruction *ToRemove : List)
      OpenIntervals.erase(ToRemove);

    // Ignore instructions that are never used within the loop.
    if (!Ends.count(I))
      continue;

    // Skip ignored values.
    if (ValuesToIgnore.count(I))
      continue;

    collectInLoopReductions();

    // For each VF find the maximum usage of registers.
    for (unsigned j = 0, e = VFs.size(); j < e; ++j) {
      // Count the number of registers used, per register class, given all open
      // intervals.
      // Note that elements in this SmallMapVector will be default constructed
      // as 0. So we can use "RegUsage[ClassID] += n" in the code below even if
      // there is no previous entry for ClassID.
      SmallMapVector<unsigned, unsigned, 4> RegUsage;

      if (VFs[j].isScalar()) {
        for (auto *Inst : OpenIntervals) {
          unsigned ClassID =
              TTI.getRegisterClassForType(false, Inst->getType());
          // FIXME: The target might use more than one register for the type
          // even in the scalar case.
          RegUsage[ClassID] += 1;
        }
      } else {
        collectUniformsAndScalars(VFs[j]);
        for (auto *Inst : OpenIntervals) {
          // Skip ignored values for VF > 1.
          if (VecValuesToIgnore.count(Inst))
            continue;
          if (isScalarAfterVectorization(Inst, VFs[j])) {
            unsigned ClassID =
                TTI.getRegisterClassForType(false, Inst->getType());
            // FIXME: The target might use more than one register for the type
            // even in the scalar case.
            RegUsage[ClassID] += 1;
          } else {
            unsigned ClassID =
                TTI.getRegisterClassForType(true, Inst->getType());
            RegUsage[ClassID] += GetRegUsage(Inst->getType(), VFs[j]);
          }
        }
      }

      for (auto& pair : RegUsage) {
        auto &Entry = MaxUsages[j][pair.first];
        Entry = std::max(Entry, pair.second);
      }
    }

    LLVM_DEBUG(dbgs() << "LV(REG): At #" << i << " Interval # "
                      << OpenIntervals.size() << '\n');

    // Add the current instruction to the list of open intervals.
    OpenIntervals.insert(I);
  }

  for (unsigned i = 0, e = VFs.size(); i < e; ++i) {
    // Note that elements in this SmallMapVector will be default constructed
    // as 0. So we can use "Invariant[ClassID] += n" in the code below even if
    // there is no previous entry for ClassID.
    SmallMapVector<unsigned, unsigned, 4> Invariant;

    for (auto *Inst : LoopInvariants) {
      // FIXME: The target might use more than one register for the type
      // even in the scalar case.
      bool IsScalar = all_of(Inst->users(), [&](User *U) {
        auto *I = cast<Instruction>(U);
        return TheLoop != LI->getLoopFor(I->getParent()) ||
               isScalarAfterVectorization(I, VFs[i]);
      });

      ElementCount VF = IsScalar ? ElementCount::getFixed(1) : VFs[i];
      unsigned ClassID =
          TTI.getRegisterClassForType(VF.isVector(), Inst->getType());
      Invariant[ClassID] += GetRegUsage(Inst->getType(), VF);
    }

    LLVM_DEBUG({
      dbgs() << "LV(REG): VF = " << VFs[i] << '\n';
      dbgs() << "LV(REG): Found max usage: " << MaxUsages[i].size()
             << " item\n";
      for (const auto &pair : MaxUsages[i]) {
        dbgs() << "LV(REG): RegisterClass: "
               << TTI.getRegisterClassName(pair.first) << ", " << pair.second
               << " registers\n";
      }
      dbgs() << "LV(REG): Found invariant usage: " << Invariant.size()
             << " item\n";
      for (const auto &pair : Invariant) {
        dbgs() << "LV(REG): RegisterClass: "
               << TTI.getRegisterClassName(pair.first) << ", " << pair.second
               << " registers\n";
      }
    });

    RU.LoopInvariantRegs = Invariant;
    RU.MaxLocalUsers = MaxUsages[i];
    RUs[i] = RU;
  }

  return RUs;
}

bool LoopVectorizationCostModel::useEmulatedMaskMemRefHack(Instruction *I,
                                                           ElementCount VF) {
  // TODO: Cost model for emulated masked load/store is completely
  // broken. This hack guides the cost model to use an artificially
  // high enough value to practically disable vectorization with such
  // operations, except where previously deployed legality hack allowed
  // using very low cost values. This is to avoid regressions coming simply
  // from moving "masked load/store" check from legality to cost model.
  // Masked Load/Gather emulation was previously never allowed.
  // Limited number of Masked Store/Scatter emulation was allowed.
  assert((isPredicatedInst(I)) &&
         "Expecting a scalar emulated instruction");
  return isa<LoadInst>(I) ||
         (isa<StoreInst>(I) &&
          NumPredStores > NumberOfStoresToPredicate);
}

void LoopVectorizationCostModel::collectInstsToScalarize(ElementCount VF) {
  // If we aren't vectorizing the loop, or if we've already collected the
  // instructions to scalarize, there's nothing to do. Collection may already
  // have occurred if we have a user-selected VF and are now computing the
  // expected cost for interleaving.
  if (VF.isScalar() || VF.isZero() || InstsToScalarize.contains(VF))
    return;

  // Initialize a mapping for VF in InstsToScalalarize. If we find that it's
  // not profitable to scalarize any instructions, the presence of VF in the
  // map will indicate that we've analyzed it already.
  ScalarCostsTy &ScalarCostsVF = InstsToScalarize[VF];

  PredicatedBBsAfterVectorization[VF].clear();

  // Find all the instructions that are scalar with predication in the loop and
  // determine if it would be better to not if-convert the blocks they are in.
  // If so, we also record the instructions to scalarize.
  for (BasicBlock *BB : TheLoop->blocks()) {
    if (!blockNeedsPredicationForAnyReason(BB))
      continue;
    for (Instruction &I : *BB)
      if (isScalarWithPredication(&I, VF)) {
        ScalarCostsTy ScalarCosts;
        // Do not apply discount logic for:
        // 1. Scalars after vectorization, as there will only be a single copy
        // of the instruction.
        // 2. Scalable VF, as that would lead to invalid scalarization costs.
        // 3. Emulated masked memrefs, if a hacked cost is needed.
        if (!isScalarAfterVectorization(&I, VF) && !VF.isScalable() &&
            !useEmulatedMaskMemRefHack(&I, VF) &&
            computePredInstDiscount(&I, ScalarCosts, VF) >= 0)
          ScalarCostsVF.insert(ScalarCosts.begin(), ScalarCosts.end());
        // Remember that BB will remain after vectorization.
        PredicatedBBsAfterVectorization[VF].insert(BB);
        for (auto *Pred : predecessors(BB)) {
          if (Pred->getSingleSuccessor() == BB)
            PredicatedBBsAfterVectorization[VF].insert(Pred);
        }
      }
  }
}

InstructionCost LoopVectorizationCostModel::computePredInstDiscount(
    Instruction *PredInst, ScalarCostsTy &ScalarCosts, ElementCount VF) {
  assert(!isUniformAfterVectorization(PredInst, VF) &&
         "Instruction marked uniform-after-vectorization will be predicated");

  // Initialize the discount to zero, meaning that the scalar version and the
  // vector version cost the same.
  InstructionCost Discount = 0;

  // Holds instructions to analyze. The instructions we visit are mapped in
  // ScalarCosts. Those instructions are the ones that would be scalarized if
  // we find that the scalar version costs less.
  SmallVector<Instruction *, 8> Worklist;

  // Returns true if the given instruction can be scalarized.
  auto canBeScalarized = [&](Instruction *I) -> bool {
    // We only attempt to scalarize instructions forming a single-use chain
    // from the original predicated block that would otherwise be vectorized.
    // Although not strictly necessary, we give up on instructions we know will
    // already be scalar to avoid traversing chains that are unlikely to be
    // beneficial.
    if (!I->hasOneUse() || PredInst->getParent() != I->getParent() ||
        isScalarAfterVectorization(I, VF))
      return false;

    // If the instruction is scalar with predication, it will be analyzed
    // separately. We ignore it within the context of PredInst.
    if (isScalarWithPredication(I, VF))
      return false;

    // If any of the instruction's operands are uniform after vectorization,
    // the instruction cannot be scalarized. This prevents, for example, a
    // masked load from being scalarized.
    //
    // We assume we will only emit a value for lane zero of an instruction
    // marked uniform after vectorization, rather than VF identical values.
    // Thus, if we scalarize an instruction that uses a uniform, we would
    // create uses of values corresponding to the lanes we aren't emitting code
    // for. This behavior can be changed by allowing getScalarValue to clone
    // the lane zero values for uniforms rather than asserting.
    for (Use &U : I->operands())
      if (auto *J = dyn_cast<Instruction>(U.get()))
        if (isUniformAfterVectorization(J, VF))
          return false;

    // Otherwise, we can scalarize the instruction.
    return true;
  };

  // Compute the expected cost discount from scalarizing the entire expression
  // feeding the predicated instruction. We currently only consider expressions
  // that are single-use instruction chains.
  Worklist.push_back(PredInst);
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();

    // If we've already analyzed the instruction, there's nothing to do.
    if (ScalarCosts.contains(I))
      continue;

    // Compute the cost of the vector instruction. Note that this cost already
    // includes the scalarization overhead of the predicated instruction.
    InstructionCost VectorCost = getInstructionCost(I, VF);

    // Compute the cost of the scalarized instruction. This cost is the cost of
    // the instruction as if it wasn't if-converted and instead remained in the
    // predicated block. We will scale this cost by block probability after
    // computing the scalarization overhead.
    InstructionCost ScalarCost =
        VF.getFixedValue() * getInstructionCost(I, ElementCount::getFixed(1));

    // Compute the scalarization overhead of needed insertelement instructions
    // and phi nodes.
    TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
    if (isScalarWithPredication(I, VF) && !I->getType()->isVoidTy()) {
      ScalarCost += TTI.getScalarizationOverhead(
          cast<VectorType>(ToVectorTy(I->getType(), VF)),
          APInt::getAllOnes(VF.getFixedValue()), /*Insert*/ true,
          /*Extract*/ false, CostKind);
      ScalarCost +=
          VF.getFixedValue() * TTI.getCFInstrCost(Instruction::PHI, CostKind);
    }

    // Compute the scalarization overhead of needed extractelement
    // instructions. For each of the instruction's operands, if the operand can
    // be scalarized, add it to the worklist; otherwise, account for the
    // overhead.
    for (Use &U : I->operands())
      if (auto *J = dyn_cast<Instruction>(U.get())) {
        assert(VectorType::isValidElementType(J->getType()) &&
               "Instruction has non-scalar type");
        if (canBeScalarized(J))
          Worklist.push_back(J);
        else if (needsExtract(J, VF)) {
          ScalarCost += TTI.getScalarizationOverhead(
              cast<VectorType>(ToVectorTy(J->getType(), VF)),
              APInt::getAllOnes(VF.getFixedValue()), /*Insert*/ false,
              /*Extract*/ true, CostKind);
        }
      }

    // Scale the total scalar cost by block probability.
    ScalarCost /= getReciprocalPredBlockProb();

    // Compute the discount. A non-negative discount means the vector version
    // of the instruction costs more, and scalarizing would be beneficial.
    Discount += VectorCost - ScalarCost;
    ScalarCosts[I] = ScalarCost;
  }

  return Discount;
}

InstructionCost LoopVectorizationCostModel::expectedCost(
    ElementCount VF, SmallVectorImpl<InstructionVFPair> *Invalid) {
  InstructionCost Cost;

  // For each block.
  for (BasicBlock *BB : TheLoop->blocks()) {
    InstructionCost BlockCost;

    // For each instruction in the old loop.
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      // Skip ignored values.
      if (ValuesToIgnore.count(&I) ||
          (VF.isVector() && VecValuesToIgnore.count(&I)))
        continue;

      InstructionCost C = getInstructionCost(&I, VF);

      // Check if we should override the cost.
      if (C.isValid() && ForceTargetInstructionCost.getNumOccurrences() > 0)
        C = InstructionCost(ForceTargetInstructionCost);

      // Keep a list of instructions with invalid costs.
      if (Invalid && !C.isValid())
        Invalid->emplace_back(&I, VF);

      BlockCost += C;
      LLVM_DEBUG(dbgs() << "LV: Found an estimated cost of " << C << " for VF "
                        << VF << " For instruction: " << I << '\n');
    }

    // If we are vectorizing a predicated block, it will have been
    // if-converted. This means that the block's instructions (aside from
    // stores and instructions that may divide by zero) will now be
    // unconditionally executed. For the scalar case, we may not always execute
    // the predicated block, if it is an if-else block. Thus, scale the block's
    // cost by the probability of executing it. blockNeedsPredication from
    // Legal is used so as to not include all blocks in tail folded loops.
    if (VF.isScalar() && Legal->blockNeedsPredication(BB))
      BlockCost /= getReciprocalPredBlockProb();

    Cost += BlockCost;
  }

  return Cost;
}

/// Gets Address Access SCEV after verifying that the access pattern
/// is loop invariant except the induction variable dependence.
///
/// This SCEV can be sent to the Target in order to estimate the address
/// calculation cost.
static const SCEV *getAddressAccessSCEV(
              Value *Ptr,
              LoopVectorizationLegality *Legal,
              PredicatedScalarEvolution &PSE,
              const Loop *TheLoop) {

  auto *Gep = dyn_cast<GetElementPtrInst>(Ptr);
  if (!Gep)
    return nullptr;

  // We are looking for a gep with all loop invariant indices except for one
  // which should be an induction variable.
  auto SE = PSE.getSE();
  unsigned NumOperands = Gep->getNumOperands();
  for (unsigned i = 1; i < NumOperands; ++i) {
    Value *Opd = Gep->getOperand(i);
    if (!SE->isLoopInvariant(SE->getSCEV(Opd), TheLoop) &&
        !Legal->isInductionVariable(Opd))
      return nullptr;
  }

  // Now we know we have a GEP ptr, %inv, %ind, %inv. return the Ptr SCEV.
  return PSE.getSCEV(Ptr);
}

InstructionCost
LoopVectorizationCostModel::getMemInstScalarizationCost(Instruction *I,
                                                        ElementCount VF) {
  assert(VF.isVector() &&
         "Scalarization cost of instruction implies vectorization.");
  if (VF.isScalable())
    return InstructionCost::getInvalid();

  Type *ValTy = getLoadStoreType(I);
  auto SE = PSE.getSE();

  unsigned AS = getLoadStoreAddressSpace(I);
  Value *Ptr = getLoadStorePointerOperand(I);
  Type *PtrTy = ToVectorTy(Ptr->getType(), VF);
  // NOTE: PtrTy is a vector to signal `TTI::getAddressComputationCost`
  //       that it is being called from this specific place.

  // Figure out whether the access is strided and get the stride value
  // if it's known in compile time
  const SCEV *PtrSCEV = getAddressAccessSCEV(Ptr, Legal, PSE, TheLoop);

  // Get the cost of the scalar memory instruction and address computation.
  InstructionCost Cost =
      VF.getKnownMinValue() * TTI.getAddressComputationCost(PtrTy, SE, PtrSCEV);

  // Don't pass *I here, since it is scalar but will actually be part of a
  // vectorized loop where the user of it is a vectorized instruction.
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  const Align Alignment = getLoadStoreAlignment(I);
  Cost += VF.getKnownMinValue() * TTI.getMemoryOpCost(I->getOpcode(),
                                                      ValTy->getScalarType(),
                                                      Alignment, AS, CostKind);

  // Get the overhead of the extractelement and insertelement instructions
  // we might create due to scalarization.
  Cost += getScalarizationOverhead(I, VF, CostKind);

  // If we have a predicated load/store, it will need extra i1 extracts and
  // conditional branches, but may not be executed for each vector lane. Scale
  // the cost by the probability of executing the predicated block.
  if (isPredicatedInst(I)) {
    Cost /= getReciprocalPredBlockProb();

    // Add the cost of an i1 extract and a branch
    auto *Vec_i1Ty =
        VectorType::get(IntegerType::getInt1Ty(ValTy->getContext()), VF);
    Cost += TTI.getScalarizationOverhead(
        Vec_i1Ty, APInt::getAllOnes(VF.getKnownMinValue()),
        /*Insert=*/false, /*Extract=*/true, CostKind);
    Cost += TTI.getCFInstrCost(Instruction::Br, CostKind);

    if (useEmulatedMaskMemRefHack(I, VF))
      // Artificially setting to a high enough value to practically disable
      // vectorization with such operations.
      Cost = 3000000;
  }

  return Cost;
}

InstructionCost
LoopVectorizationCostModel::getConsecutiveMemOpCost(Instruction *I,
                                                    ElementCount VF) {
  Type *ValTy = getLoadStoreType(I);
  auto *VectorTy = cast<VectorType>(ToVectorTy(ValTy, VF));
  Value *Ptr = getLoadStorePointerOperand(I);
  unsigned AS = getLoadStoreAddressSpace(I);
  int ConsecutiveStride = Legal->isConsecutivePtr(ValTy, Ptr);
  enum TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  assert((ConsecutiveStride == 1 || ConsecutiveStride == -1) &&
         "Stride should be 1 or -1 for consecutive memory access");
  const Align Alignment = getLoadStoreAlignment(I);
  InstructionCost Cost = 0;
  if (Legal->isMaskRequired(I)) {
    Cost += TTI.getMaskedMemoryOpCost(I->getOpcode(), VectorTy, Alignment, AS,
                                      CostKind);
  } else {
    TTI::OperandValueInfo OpInfo = TTI::getOperandInfo(I->getOperand(0));
    Cost += TTI.getMemoryOpCost(I->getOpcode(), VectorTy, Alignment, AS,
                                CostKind, OpInfo, I);
  }

  bool Reverse = ConsecutiveStride < 0;
  if (Reverse)
    Cost += TTI.getShuffleCost(TargetTransformInfo::SK_Reverse, VectorTy,
                               std::nullopt, CostKind, 0);
  return Cost;
}

InstructionCost
LoopVectorizationCostModel::getUniformMemOpCost(Instruction *I,
                                                ElementCount VF) {
  assert(Legal->isUniformMemOp(*I, VF));

  Type *ValTy = getLoadStoreType(I);
  auto *VectorTy = cast<VectorType>(ToVectorTy(ValTy, VF));
  const Align Alignment = getLoadStoreAlignment(I);
  unsigned AS = getLoadStoreAddressSpace(I);
  enum TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  if (isa<LoadInst>(I)) {
    return TTI.getAddressComputationCost(ValTy) +
           TTI.getMemoryOpCost(Instruction::Load, ValTy, Alignment, AS,
                               CostKind) +
           TTI.getShuffleCost(TargetTransformInfo::SK_Broadcast, VectorTy);
  }
  StoreInst *SI = cast<StoreInst>(I);

  bool isLoopInvariantStoreValue = Legal->isInvariant(SI->getValueOperand());
  return TTI.getAddressComputationCost(ValTy) +
         TTI.getMemoryOpCost(Instruction::Store, ValTy, Alignment, AS,
                             CostKind) +
         (isLoopInvariantStoreValue
              ? 0
              : TTI.getVectorInstrCost(Instruction::ExtractElement, VectorTy,
                                       CostKind, VF.getKnownMinValue() - 1));
}

InstructionCost
LoopVectorizationCostModel::getGatherScatterCost(Instruction *I,
                                                 ElementCount VF) {
  Type *ValTy = getLoadStoreType(I);
  auto *VectorTy = cast<VectorType>(ToVectorTy(ValTy, VF));
  const Align Alignment = getLoadStoreAlignment(I);
  const Value *Ptr = getLoadStorePointerOperand(I);

  return TTI.getAddressComputationCost(VectorTy) +
         TTI.getGatherScatterOpCost(
             I->getOpcode(), VectorTy, Ptr, Legal->isMaskRequired(I), Alignment,
             TargetTransformInfo::TCK_RecipThroughput, I);
}

InstructionCost
LoopVectorizationCostModel::getInterleaveGroupCost(Instruction *I,
                                                   ElementCount VF) {
  Type *ValTy = getLoadStoreType(I);
  auto *VectorTy = cast<VectorType>(ToVectorTy(ValTy, VF));
  unsigned AS = getLoadStoreAddressSpace(I);
  enum TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  auto Group = getInterleavedAccessGroup(I);
  assert(Group && "Fail to get an interleaved access group.");

  unsigned InterleaveFactor = Group->getFactor();
  auto *WideVecTy = VectorType::get(ValTy, VF * InterleaveFactor);

  // Holds the indices of existing members in the interleaved group.
  SmallVector<unsigned, 4> Indices;
  for (unsigned IF = 0; IF < InterleaveFactor; IF++)
    if (Group->getMember(IF))
      Indices.push_back(IF);

  // Calculate the cost of the whole interleaved group.
  bool UseMaskForGaps =
      (Group->requiresScalarEpilogue() && !isScalarEpilogueAllowed()) ||
      (isa<StoreInst>(I) && (Group->getNumMembers() < Group->getFactor()));
  InstructionCost Cost = TTI.getInterleavedMemoryOpCost(
      I->getOpcode(), WideVecTy, Group->getFactor(), Indices, Group->getAlign(),
      AS, CostKind, Legal->isMaskRequired(I), UseMaskForGaps);

  if (Group->isReverse()) {
    // TODO: Add support for reversed masked interleaved access.
    assert(!Legal->isMaskRequired(I) &&
           "Reverse masked interleaved access not supported.");
    Cost += Group->getNumMembers() *
            TTI.getShuffleCost(TargetTransformInfo::SK_Reverse, VectorTy,
                               std::nullopt, CostKind, 0);
  }
  return Cost;
}

std::optional<InstructionCost>
LoopVectorizationCostModel::getReductionPatternCost(
    Instruction *I, ElementCount VF, Type *Ty,
    TTI::TargetCostKind CostKind) const {
  using namespace llvm::PatternMatch;
  // Early exit for no inloop reductions
  if (InLoopReductions.empty() || VF.isScalar() || !isa<VectorType>(Ty))
    return std::nullopt;
  auto *VectorTy = cast<VectorType>(Ty);

  // We are looking for a pattern of, and finding the minimal acceptable cost:
  //  reduce(mul(ext(A), ext(B))) or
  //  reduce(mul(A, B)) or
  //  reduce(ext(A)) or
  //  reduce(A).
  // The basic idea is that we walk down the tree to do that, finding the root
  // reduction instruction in InLoopReductionImmediateChains. From there we find
  // the pattern of mul/ext and test the cost of the entire pattern vs the cost
  // of the components. If the reduction cost is lower then we return it for the
  // reduction instruction and 0 for the other instructions in the pattern. If
  // it is not we return an invalid cost specifying the orignal cost method
  // should be used.
  Instruction *RetI = I;
  if (match(RetI, m_ZExtOrSExt(m_Value()))) {
    if (!RetI->hasOneUser())
      return std::nullopt;
    RetI = RetI->user_back();
  }

  if (match(RetI, m_OneUse(m_Mul(m_Value(), m_Value()))) &&
      RetI->user_back()->getOpcode() == Instruction::Add) {
    RetI = RetI->user_back();
  }

  // Test if the found instruction is a reduction, and if not return an invalid
  // cost specifying the parent to use the original cost modelling.
  if (!InLoopReductionImmediateChains.count(RetI))
    return std::nullopt;

  // Find the reduction this chain is a part of and calculate the basic cost of
  // the reduction on its own.
  Instruction *LastChain = InLoopReductionImmediateChains.at(RetI);
  Instruction *ReductionPhi = LastChain;
  while (!isa<PHINode>(ReductionPhi))
    ReductionPhi = InLoopReductionImmediateChains.at(ReductionPhi);

  const RecurrenceDescriptor &RdxDesc =
      Legal->getReductionVars().find(cast<PHINode>(ReductionPhi))->second;

  InstructionCost BaseCost;
  RecurKind RK = RdxDesc.getRecurrenceKind();
  if (RecurrenceDescriptor::isMinMaxRecurrenceKind(RK)) {
    Intrinsic::ID MinMaxID = getMinMaxReductionIntrinsicOp(RK);
    BaseCost = TTI.getMinMaxReductionCost(MinMaxID, VectorTy,
                                          RdxDesc.getFastMathFlags(), CostKind);
  } else {
    BaseCost = TTI.getArithmeticReductionCost(
        RdxDesc.getOpcode(), VectorTy, RdxDesc.getFastMathFlags(), CostKind);
  }

  // For a call to the llvm.fmuladd intrinsic we need to add the cost of a
  // normal fmul instruction to the cost of the fadd reduction.
  if (RK == RecurKind::FMulAdd)
    BaseCost +=
        TTI.getArithmeticInstrCost(Instruction::FMul, VectorTy, CostKind);

  // If we're using ordered reductions then we can just return the base cost
  // here, since getArithmeticReductionCost calculates the full ordered
  // reduction cost when FP reassociation is not allowed.
  if (useOrderedReductions(RdxDesc))
    return BaseCost;

  // Get the operand that was not the reduction chain and match it to one of the
  // patterns, returning the better cost if it is found.
  Instruction *RedOp = RetI->getOperand(1) == LastChain
                           ? dyn_cast<Instruction>(RetI->getOperand(0))
                           : dyn_cast<Instruction>(RetI->getOperand(1));

  VectorTy = VectorType::get(I->getOperand(0)->getType(), VectorTy);

  Instruction *Op0, *Op1;
  if (RedOp && RdxDesc.getOpcode() == Instruction::Add &&
      match(RedOp,
            m_ZExtOrSExt(m_Mul(m_Instruction(Op0), m_Instruction(Op1)))) &&
      match(Op0, m_ZExtOrSExt(m_Value())) &&
      Op0->getOpcode() == Op1->getOpcode() &&
      Op0->getOperand(0)->getType() == Op1->getOperand(0)->getType() &&
      !TheLoop->isLoopInvariant(Op0) && !TheLoop->isLoopInvariant(Op1) &&
      (Op0->getOpcode() == RedOp->getOpcode() || Op0 == Op1)) {

    // Matched reduce.add(ext(mul(ext(A), ext(B)))
    // Note that the extend opcodes need to all match, or if A==B they will have
    // been converted to zext(mul(sext(A), sext(A))) as it is known positive,
    // which is equally fine.
    bool IsUnsigned = isa<ZExtInst>(Op0);
    auto *ExtType = VectorType::get(Op0->getOperand(0)->getType(), VectorTy);
    auto *MulType = VectorType::get(Op0->getType(), VectorTy);

    InstructionCost ExtCost =
        TTI.getCastInstrCost(Op0->getOpcode(), MulType, ExtType,
                             TTI::CastContextHint::None, CostKind, Op0);
    InstructionCost MulCost =
        TTI.getArithmeticInstrCost(Instruction::Mul, MulType, CostKind);
    InstructionCost Ext2Cost =
        TTI.getCastInstrCost(RedOp->getOpcode(), VectorTy, MulType,
                             TTI::CastContextHint::None, CostKind, RedOp);

    InstructionCost RedCost = TTI.getMulAccReductionCost(
        IsUnsigned, RdxDesc.getRecurrenceType(), ExtType, CostKind);

    if (RedCost.isValid() &&
        RedCost < ExtCost * 2 + MulCost + Ext2Cost + BaseCost)
      return I == RetI ? RedCost : 0;
  } else if (RedOp && match(RedOp, m_ZExtOrSExt(m_Value())) &&
             !TheLoop->isLoopInvariant(RedOp)) {
    // Matched reduce(ext(A))
    bool IsUnsigned = isa<ZExtInst>(RedOp);
    auto *ExtType = VectorType::get(RedOp->getOperand(0)->getType(), VectorTy);
    InstructionCost RedCost = TTI.getExtendedReductionCost(
        RdxDesc.getOpcode(), IsUnsigned, RdxDesc.getRecurrenceType(), ExtType,
        RdxDesc.getFastMathFlags(), CostKind);

    InstructionCost ExtCost =
        TTI.getCastInstrCost(RedOp->getOpcode(), VectorTy, ExtType,
                             TTI::CastContextHint::None, CostKind, RedOp);
    if (RedCost.isValid() && RedCost < BaseCost + ExtCost)
      return I == RetI ? RedCost : 0;
  } else if (RedOp && RdxDesc.getOpcode() == Instruction::Add &&
             match(RedOp, m_Mul(m_Instruction(Op0), m_Instruction(Op1)))) {
    if (match(Op0, m_ZExtOrSExt(m_Value())) &&
        Op0->getOpcode() == Op1->getOpcode() &&
        !TheLoop->isLoopInvariant(Op0) && !TheLoop->isLoopInvariant(Op1)) {
      bool IsUnsigned = isa<ZExtInst>(Op0);
      Type *Op0Ty = Op0->getOperand(0)->getType();
      Type *Op1Ty = Op1->getOperand(0)->getType();
      Type *LargestOpTy =
          Op0Ty->getIntegerBitWidth() < Op1Ty->getIntegerBitWidth() ? Op1Ty
                                                                    : Op0Ty;
      auto *ExtType = VectorType::get(LargestOpTy, VectorTy);

      // Matched reduce.add(mul(ext(A), ext(B))), where the two ext may be of
      // different sizes. We take the largest type as the ext to reduce, and add
      // the remaining cost as, for example reduce(mul(ext(ext(A)), ext(B))).
      InstructionCost ExtCost0 = TTI.getCastInstrCost(
          Op0->getOpcode(), VectorTy, VectorType::get(Op0Ty, VectorTy),
          TTI::CastContextHint::None, CostKind, Op0);
      InstructionCost ExtCost1 = TTI.getCastInstrCost(
          Op1->getOpcode(), VectorTy, VectorType::get(Op1Ty, VectorTy),
          TTI::CastContextHint::None, CostKind, Op1);
      InstructionCost MulCost =
          TTI.getArithmeticInstrCost(Instruction::Mul, VectorTy, CostKind);

      InstructionCost RedCost = TTI.getMulAccReductionCost(
          IsUnsigned, RdxDesc.getRecurrenceType(), ExtType, CostKind);
      InstructionCost ExtraExtCost = 0;
      if (Op0Ty != LargestOpTy || Op1Ty != LargestOpTy) {
        Instruction *ExtraExtOp = (Op0Ty != LargestOpTy) ? Op0 : Op1;
        ExtraExtCost = TTI.getCastInstrCost(
            ExtraExtOp->getOpcode(), ExtType,
            VectorType::get(ExtraExtOp->getOperand(0)->getType(), VectorTy),
            TTI::CastContextHint::None, CostKind, ExtraExtOp);
      }

      if (RedCost.isValid() &&
          (RedCost + ExtraExtCost) < (ExtCost0 + ExtCost1 + MulCost + BaseCost))
        return I == RetI ? RedCost : 0;
    } else if (!match(I, m_ZExtOrSExt(m_Value()))) {
      // Matched reduce.add(mul())
      InstructionCost MulCost =
          TTI.getArithmeticInstrCost(Instruction::Mul, VectorTy, CostKind);

      InstructionCost RedCost = TTI.getMulAccReductionCost(
          true, RdxDesc.getRecurrenceType(), VectorTy, CostKind);

      if (RedCost.isValid() && RedCost < MulCost + BaseCost)
        return I == RetI ? RedCost : 0;
    }
  }

  return I == RetI ? std::optional<InstructionCost>(BaseCost) : std::nullopt;
}

InstructionCost
LoopVectorizationCostModel::getMemoryInstructionCost(Instruction *I,
                                                     ElementCount VF) {
  // Calculate scalar cost only. Vectorization cost should be ready at this
  // moment.
  if (VF.isScalar()) {
    Type *ValTy = getLoadStoreType(I);
    const Align Alignment = getLoadStoreAlignment(I);
    unsigned AS = getLoadStoreAddressSpace(I);

    TTI::OperandValueInfo OpInfo = TTI::getOperandInfo(I->getOperand(0));
    return TTI.getAddressComputationCost(ValTy) +
           TTI.getMemoryOpCost(I->getOpcode(), ValTy, Alignment, AS,
                               TTI::TCK_RecipThroughput, OpInfo, I);
  }
  return getWideningCost(I, VF);
}

InstructionCost LoopVectorizationCostModel::getScalarizationOverhead(
    Instruction *I, ElementCount VF, TTI::TargetCostKind CostKind) const {

  // There is no mechanism yet to create a scalable scalarization loop,
  // so this is currently Invalid.
  if (VF.isScalable())
    return InstructionCost::getInvalid();

  if (VF.isScalar())
    return 0;

  InstructionCost Cost = 0;
  Type *RetTy = ToVectorTy(I->getType(), VF);
  if (!RetTy->isVoidTy() &&
      (!isa<LoadInst>(I) || !TTI.supportsEfficientVectorElementLoadStore()))
    Cost += TTI.getScalarizationOverhead(
        cast<VectorType>(RetTy), APInt::getAllOnes(VF.getKnownMinValue()),
        /*Insert*/ true,
        /*Extract*/ false, CostKind);

  // Some targets keep addresses scalar.
  if (isa<LoadInst>(I) && !TTI.prefersVectorizedAddressing())
    return Cost;

  // Some targets support efficient element stores.
  if (isa<StoreInst>(I) && TTI.supportsEfficientVectorElementLoadStore())
    return Cost;

  // Collect operands to consider.
  CallInst *CI = dyn_cast<CallInst>(I);
  Instruction::op_range Ops = CI ? CI->args() : I->operands();

  // Skip operands that do not require extraction/scalarization and do not incur
  // any overhead.
  SmallVector<Type *> Tys;
  for (auto *V : filterExtractingOperands(Ops, VF))
    Tys.push_back(MaybeVectorizeType(V->getType(), VF));
  return Cost + TTI.getOperandsScalarizationOverhead(
                    filterExtractingOperands(Ops, VF), Tys, CostKind);
}

void LoopVectorizationCostModel::setCostBasedWideningDecision(ElementCount VF) {
  if (VF.isScalar())
    return;
  NumPredStores = 0;
  for (BasicBlock *BB : TheLoop->blocks()) {
    // For each instruction in the old loop.
    for (Instruction &I : *BB) {
      Value *Ptr =  getLoadStorePointerOperand(&I);
      if (!Ptr)
        continue;

      // TODO: We should generate better code and update the cost model for
      // predicated uniform stores. Today they are treated as any other
      // predicated store (see added test cases in
      // invariant-store-vectorization.ll).
      if (isa<StoreInst>(&I) && isScalarWithPredication(&I, VF))
        NumPredStores++;

      if (Legal->isUniformMemOp(I, VF)) {
        auto isLegalToScalarize = [&]() {
          if (!VF.isScalable())
            // Scalarization of fixed length vectors "just works".
            return true;

          // We have dedicated lowering for unpredicated uniform loads and
          // stores.  Note that even with tail folding we know that at least
          // one lane is active (i.e. generalized predication is not possible
          // here), and the logic below depends on this fact.
          if (!foldTailByMasking())
            return true;

          // For scalable vectors, a uniform memop load is always
          // uniform-by-parts  and we know how to scalarize that.
          if (isa<LoadInst>(I))
            return true;

          // A uniform store isn't neccessarily uniform-by-part
          // and we can't assume scalarization.
          auto &SI = cast<StoreInst>(I);
          return TheLoop->isLoopInvariant(SI.getValueOperand());
        };

        const InstructionCost GatherScatterCost =
          isLegalGatherOrScatter(&I, VF) ?
          getGatherScatterCost(&I, VF) : InstructionCost::getInvalid();

        // Load: Scalar load + broadcast
        // Store: Scalar store + isLoopInvariantStoreValue ? 0 : extract
        // FIXME: This cost is a significant under-estimate for tail folded
        // memory ops.
        const InstructionCost ScalarizationCost = isLegalToScalarize() ?
          getUniformMemOpCost(&I, VF) : InstructionCost::getInvalid();

        // Choose better solution for the current VF,  Note that Invalid
        // costs compare as maximumal large.  If both are invalid, we get
        // scalable invalid which signals a failure and a vectorization abort.
        if (GatherScatterCost < ScalarizationCost)
          setWideningDecision(&I, VF, CM_GatherScatter, GatherScatterCost);
        else
          setWideningDecision(&I, VF, CM_Scalarize, ScalarizationCost);
        continue;
      }

      // We assume that widening is the best solution when possible.
      if (memoryInstructionCanBeWidened(&I, VF)) {
        InstructionCost Cost = getConsecutiveMemOpCost(&I, VF);
        int ConsecutiveStride = Legal->isConsecutivePtr(
            getLoadStoreType(&I), getLoadStorePointerOperand(&I));
        assert((ConsecutiveStride == 1 || ConsecutiveStride == -1) &&
               "Expected consecutive stride.");
        InstWidening Decision =
            ConsecutiveStride == 1 ? CM_Widen : CM_Widen_Reverse;
        setWideningDecision(&I, VF, Decision, Cost);
        continue;
      }

      // Choose between Interleaving, Gather/Scatter or Scalarization.
      InstructionCost InterleaveCost = InstructionCost::getInvalid();
      unsigned NumAccesses = 1;
      if (isAccessInterleaved(&I)) {
        auto Group = getInterleavedAccessGroup(&I);
        assert(Group && "Fail to get an interleaved access group.");

        // Make one decision for the whole group.
        if (getWideningDecision(&I, VF) != CM_Unknown)
          continue;

        NumAccesses = Group->getNumMembers();
        if (interleavedAccessCanBeWidened(&I, VF))
          InterleaveCost = getInterleaveGroupCost(&I, VF);
      }

      InstructionCost GatherScatterCost =
          isLegalGatherOrScatter(&I, VF)
              ? getGatherScatterCost(&I, VF) * NumAccesses
              : InstructionCost::getInvalid();

      InstructionCost ScalarizationCost =
          getMemInstScalarizationCost(&I, VF) * NumAccesses;

      // Choose better solution for the current VF,
      // write down this decision and use it during vectorization.
      InstructionCost Cost;
      InstWidening Decision;
      if (InterleaveCost <= GatherScatterCost &&
          InterleaveCost < ScalarizationCost) {
        Decision = CM_Interleave;
        Cost = InterleaveCost;
      } else if (GatherScatterCost < ScalarizationCost) {
        Decision = CM_GatherScatter;
        Cost = GatherScatterCost;
      } else {
        Decision = CM_Scalarize;
        Cost = ScalarizationCost;
      }
      // If the instructions belongs to an interleave group, the whole group
      // receives the same decision. The whole group receives the cost, but
      // the cost will actually be assigned to one instruction.
      if (auto Group = getInterleavedAccessGroup(&I))
        setWideningDecision(Group, VF, Decision, Cost);
      else
        setWideningDecision(&I, VF, Decision, Cost);
    }
  }

  // Make sure that any load of address and any other address computation
  // remains scalar unless there is gather/scatter support. This avoids
  // inevitable extracts into address registers, and also has the benefit of
  // activating LSR more, since that pass can't optimize vectorized
  // addresses.
  if (TTI.prefersVectorizedAddressing())
    return;

  // Start with all scalar pointer uses.
  SmallPtrSet<Instruction *, 8> AddrDefs;
  for (BasicBlock *BB : TheLoop->blocks())
    for (Instruction &I : *BB) {
      Instruction *PtrDef =
        dyn_cast_or_null<Instruction>(getLoadStorePointerOperand(&I));
      if (PtrDef && TheLoop->contains(PtrDef) &&
          getWideningDecision(&I, VF) != CM_GatherScatter)
        AddrDefs.insert(PtrDef);
    }

  // Add all instructions used to generate the addresses.
  SmallVector<Instruction *, 4> Worklist;
  append_range(Worklist, AddrDefs);
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();
    for (auto &Op : I->operands())
      if (auto *InstOp = dyn_cast<Instruction>(Op))
        if ((InstOp->getParent() == I->getParent()) && !isa<PHINode>(InstOp) &&
            AddrDefs.insert(InstOp).second)
          Worklist.push_back(InstOp);
  }

  for (auto *I : AddrDefs) {
    if (isa<LoadInst>(I)) {
      // Setting the desired widening decision should ideally be handled in
      // by cost functions, but since this involves the task of finding out
      // if the loaded register is involved in an address computation, it is
      // instead changed here when we know this is the case.
      InstWidening Decision = getWideningDecision(I, VF);
      if (Decision == CM_Widen || Decision == CM_Widen_Reverse)
        // Scalarize a widened load of address.
        setWideningDecision(
            I, VF, CM_Scalarize,
            (VF.getKnownMinValue() *
             getMemoryInstructionCost(I, ElementCount::getFixed(1))));
      else if (auto Group = getInterleavedAccessGroup(I)) {
        // Scalarize an interleave group of address loads.
        for (unsigned I = 0; I < Group->getFactor(); ++I) {
          if (Instruction *Member = Group->getMember(I))
            setWideningDecision(
                Member, VF, CM_Scalarize,
                (VF.getKnownMinValue() *
                 getMemoryInstructionCost(Member, ElementCount::getFixed(1))));
        }
      }
    } else
      // Make sure I gets scalarized and a cost estimate without
      // scalarization overhead.
      ForcedScalars[VF].insert(I);
  }
}

void LoopVectorizationCostModel::setVectorizedCallDecision(ElementCount VF) {
  assert(!VF.isScalar() &&
         "Trying to set a vectorization decision for a scalar VF");

  for (BasicBlock *BB : TheLoop->blocks()) {
    // For each instruction in the old loop.
    for (Instruction &I : *BB) {
      CallInst *CI = dyn_cast<CallInst>(&I);

      if (!CI)
        continue;

      InstructionCost ScalarCost = InstructionCost::getInvalid();
      InstructionCost VectorCost = InstructionCost::getInvalid();
      InstructionCost IntrinsicCost = InstructionCost::getInvalid();
      TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

      Function *ScalarFunc = CI->getCalledFunction();
      Type *ScalarRetTy = CI->getType();
      SmallVector<Type *, 4> Tys, ScalarTys;
      bool MaskRequired = Legal->isMaskRequired(CI);
      for (auto &ArgOp : CI->args())
        ScalarTys.push_back(ArgOp->getType());

      // Compute corresponding vector type for return value and arguments.
      Type *RetTy = ToVectorTy(ScalarRetTy, VF);
      for (Type *ScalarTy : ScalarTys)
        Tys.push_back(ToVectorTy(ScalarTy, VF));

      // An in-loop reduction using an fmuladd intrinsic is a special case;
      // we don't want the normal cost for that intrinsic.
      if (RecurrenceDescriptor::isFMulAddIntrinsic(CI))
        if (auto RedCost = getReductionPatternCost(CI, VF, RetTy, CostKind)) {
          setCallWideningDecision(CI, VF, CM_IntrinsicCall, nullptr,
                                  getVectorIntrinsicIDForCall(CI, TLI),
                                  std::nullopt, *RedCost);
          continue;
        }

      // Estimate cost of scalarized vector call. The source operands are
      // assumed to be vectors, so we need to extract individual elements from
      // there, execute VF scalar calls, and then gather the result into the
      // vector return value.
      InstructionCost ScalarCallCost =
          TTI.getCallInstrCost(ScalarFunc, ScalarRetTy, ScalarTys, CostKind);

      // Compute costs of unpacking argument values for the scalar calls and
      // packing the return values to a vector.
      InstructionCost ScalarizationCost =
          getScalarizationOverhead(CI, VF, CostKind);

      ScalarCost = ScalarCallCost * VF.getKnownMinValue() + ScalarizationCost;

      // Find the cost of vectorizing the call, if we can find a suitable
      // vector variant of the function.
      bool UsesMask = false;
      VFInfo FuncInfo;
      Function *VecFunc = nullptr;
      // Search through any available variants for one we can use at this VF.
      for (VFInfo &Info : VFDatabase::getMappings(*CI)) {
        // Must match requested VF.
        if (Info.Shape.VF != VF)
          continue;

        // Must take a mask argument if one is required
        if (MaskRequired && !Info.isMasked())
          continue;

        // Check that all parameter kinds are supported
        bool ParamsOk = true;
        for (VFParameter Param : Info.Shape.Parameters) {
          switch (Param.ParamKind) {
          case VFParamKind::Vector:
            break;
          case VFParamKind::OMP_Uniform: {
            Value *ScalarParam = CI->getArgOperand(Param.ParamPos);
            // Make sure the scalar parameter in the loop is invariant.
            if (!PSE.getSE()->isLoopInvariant(PSE.getSCEV(ScalarParam),
                                              TheLoop))
              ParamsOk = false;
            break;
          }
          case VFParamKind::OMP_Linear: {
            Value *ScalarParam = CI->getArgOperand(Param.ParamPos);
            // Find the stride for the scalar parameter in this loop and see if
            // it matches the stride for the variant.
            // TODO: do we need to figure out the cost of an extract to get the
            // first lane? Or do we hope that it will be folded away?
            ScalarEvolution *SE = PSE.getSE();
            const auto *SAR =
                dyn_cast<SCEVAddRecExpr>(SE->getSCEV(ScalarParam));

            if (!SAR || SAR->getLoop() != TheLoop) {
              ParamsOk = false;
              break;
            }

            const SCEVConstant *Step =
                dyn_cast<SCEVConstant>(SAR->getStepRecurrence(*SE));

            if (!Step ||
                Step->getAPInt().getSExtValue() != Param.LinearStepOrPos)
              ParamsOk = false;

            break;
          }
          case VFParamKind::GlobalPredicate:
            UsesMask = true;
            break;
          default:
            ParamsOk = false;
            break;
          }
        }

        if (!ParamsOk)
          continue;

        // Found a suitable candidate, stop here.
        VecFunc = CI->getModule()->getFunction(Info.VectorName);
        FuncInfo = Info;
        break;
      }

      // Add in the cost of synthesizing a mask if one wasn't required.
      InstructionCost MaskCost = 0;
      if (VecFunc && UsesMask && !MaskRequired)
        MaskCost = TTI.getShuffleCost(
            TargetTransformInfo::SK_Broadcast,
            VectorType::get(IntegerType::getInt1Ty(
                                VecFunc->getFunctionType()->getContext()),
                            VF));

      if (TLI && VecFunc && !CI->isNoBuiltin())
        VectorCost =
            TTI.getCallInstrCost(nullptr, RetTy, Tys, CostKind) + MaskCost;

      // Find the cost of an intrinsic; some targets may have instructions that
      // perform the operation without needing an actual call.
      Intrinsic::ID IID = getVectorIntrinsicIDForCall(CI, TLI);
      if (IID != Intrinsic::not_intrinsic)
        IntrinsicCost = getVectorIntrinsicCost(CI, VF);

      InstructionCost Cost = ScalarCost;
      InstWidening Decision = CM_Scalarize;

      if (VectorCost <= Cost) {
        Cost = VectorCost;
        Decision = CM_VectorCall;
      }

      if (IntrinsicCost <= Cost) {
        Cost = IntrinsicCost;
        Decision = CM_IntrinsicCall;
      }

      setCallWideningDecision(CI, VF, Decision, VecFunc, IID,
                              FuncInfo.getParamIndexForOptionalMask(), Cost);
    }
  }
}

InstructionCost
LoopVectorizationCostModel::getInstructionCost(Instruction *I,
                                               ElementCount VF) {
  // If we know that this instruction will remain uniform, check the cost of
  // the scalar version.
  if (isUniformAfterVectorization(I, VF))
    VF = ElementCount::getFixed(1);

  if (VF.isVector() && isProfitableToScalarize(I, VF))
    return InstsToScalarize[VF][I];

  // Forced scalars do not have any scalarization overhead.
  auto ForcedScalar = ForcedScalars.find(VF);
  if (VF.isVector() && ForcedScalar != ForcedScalars.end()) {
    auto InstSet = ForcedScalar->second;
    if (InstSet.count(I))
      return getInstructionCost(I, ElementCount::getFixed(1)) *
             VF.getKnownMinValue();
  }

  Type *RetTy = I->getType();
  if (canTruncateToMinimalBitwidth(I, VF))
    RetTy = IntegerType::get(RetTy->getContext(), MinBWs[I]);
  auto SE = PSE.getSE();
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  auto hasSingleCopyAfterVectorization = [this](Instruction *I,
                                                ElementCount VF) -> bool {
    if (VF.isScalar())
      return true;

    auto Scalarized = InstsToScalarize.find(VF);
    assert(Scalarized != InstsToScalarize.end() &&
           "VF not yet analyzed for scalarization profitability");
    return !Scalarized->second.count(I) &&
           llvm::all_of(I->users(), [&](User *U) {
             auto *UI = cast<Instruction>(U);
             return !Scalarized->second.count(UI);
           });
  };
  (void) hasSingleCopyAfterVectorization;

  Type *VectorTy;
  if (isScalarAfterVectorization(I, VF)) {
    // With the exception of GEPs and PHIs, after scalarization there should
    // only be one copy of the instruction generated in the loop. This is
    // because the VF is either 1, or any instructions that need scalarizing
    // have already been dealt with by the time we get here. As a result,
    // it means we don't have to multiply the instruction cost by VF.
    assert(I->getOpcode() == Instruction::GetElementPtr ||
           I->getOpcode() == Instruction::PHI ||
           (I->getOpcode() == Instruction::BitCast &&
            I->getType()->isPointerTy()) ||
           hasSingleCopyAfterVectorization(I, VF));
    VectorTy = RetTy;
  } else
    VectorTy = ToVectorTy(RetTy, VF);

  if (VF.isVector() && VectorTy->isVectorTy() &&
      !TTI.getNumberOfParts(VectorTy))
    return InstructionCost::getInvalid();

  // TODO: We need to estimate the cost of intrinsic calls.
  switch (I->getOpcode()) {
  case Instruction::GetElementPtr:
    // We mark this instruction as zero-cost because the cost of GEPs in
    // vectorized code depends on whether the corresponding memory instruction
    // is scalarized or not. Therefore, we handle GEPs with the memory
    // instruction cost.
    return 0;
  case Instruction::Br: {
    // In cases of scalarized and predicated instructions, there will be VF
    // predicated blocks in the vectorized loop. Each branch around these
    // blocks requires also an extract of its vector compare i1 element.
    // Note that the conditional branch from the loop latch will be replaced by
    // a single branch controlling the loop, so there is no extra overhead from
    // scalarization.
    bool ScalarPredicatedBB = false;
    BranchInst *BI = cast<BranchInst>(I);
    if (VF.isVector() && BI->isConditional() &&
        (PredicatedBBsAfterVectorization[VF].count(BI->getSuccessor(0)) ||
         PredicatedBBsAfterVectorization[VF].count(BI->getSuccessor(1))) &&
        BI->getParent() != TheLoop->getLoopLatch())
      ScalarPredicatedBB = true;

    if (ScalarPredicatedBB) {
      // Not possible to scalarize scalable vector with predicated instructions.
      if (VF.isScalable())
        return InstructionCost::getInvalid();
      // Return cost for branches around scalarized and predicated blocks.
      auto *Vec_i1Ty =
          VectorType::get(IntegerType::getInt1Ty(RetTy->getContext()), VF);
      return (
          TTI.getScalarizationOverhead(
              Vec_i1Ty, APInt::getAllOnes(VF.getFixedValue()),
              /*Insert*/ false, /*Extract*/ true, CostKind) +
          (TTI.getCFInstrCost(Instruction::Br, CostKind) * VF.getFixedValue()));
    } else if (I->getParent() == TheLoop->getLoopLatch() || VF.isScalar())
      // The back-edge branch will remain, as will all scalar branches.
      return TTI.getCFInstrCost(Instruction::Br, CostKind);
    else
      // This branch will be eliminated by if-conversion.
      return 0;
    // Note: We currently assume zero cost for an unconditional branch inside
    // a predicated block since it will become a fall-through, although we
    // may decide in the future to call TTI for all branches.
  }
  case Instruction::PHI: {
    auto *Phi = cast<PHINode>(I);

    // First-order recurrences are replaced by vector shuffles inside the loop.
    if (VF.isVector() && Legal->isFixedOrderRecurrence(Phi)) {
      // For <vscale x 1 x i64>, if vscale = 1 we are unable to extract the
      // penultimate value of the recurrence.
      // TODO: Consider vscale_range info.
      if (VF.isScalable() && VF.getKnownMinValue() == 1)
        return InstructionCost::getInvalid();
      SmallVector<int> Mask(VF.getKnownMinValue());
      std::iota(Mask.begin(), Mask.end(), VF.getKnownMinValue() - 1);
      return TTI.getShuffleCost(TargetTransformInfo::SK_Splice,
                                cast<VectorType>(VectorTy), Mask, CostKind,
                                VF.getKnownMinValue() - 1);
    }

    // Phi nodes in non-header blocks (not inductions, reductions, etc.) are
    // converted into select instructions. We require N - 1 selects per phi
    // node, where N is the number of incoming values.
    if (VF.isVector() && Phi->getParent() != TheLoop->getHeader())
      return (Phi->getNumIncomingValues() - 1) *
             TTI.getCmpSelInstrCost(
                 Instruction::Select, ToVectorTy(Phi->getType(), VF),
                 ToVectorTy(Type::getInt1Ty(Phi->getContext()), VF),
                 CmpInst::BAD_ICMP_PREDICATE, CostKind);

    return TTI.getCFInstrCost(Instruction::PHI, CostKind);
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    if (VF.isVector() && isPredicatedInst(I)) {
      const auto [ScalarCost, SafeDivisorCost] = getDivRemSpeculationCost(I, VF);
      return isDivRemScalarWithPredication(ScalarCost, SafeDivisorCost) ?
        ScalarCost : SafeDivisorCost;
    }
    // We've proven all lanes safe to speculate, fall through.
    [[fallthrough]];
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    // If we're speculating on the stride being 1, the multiplication may
    // fold away.  We can generalize this for all operations using the notion
    // of neutral elements.  (TODO)
    if (I->getOpcode() == Instruction::Mul &&
        (PSE.getSCEV(I->getOperand(0))->isOne() ||
         PSE.getSCEV(I->getOperand(1))->isOne()))
      return 0;

    // Detect reduction patterns
    if (auto RedCost = getReductionPatternCost(I, VF, VectorTy, CostKind))
      return *RedCost;

    // Certain instructions can be cheaper to vectorize if they have a constant
    // second vector operand. One example of this are shifts on x86.
    Value *Op2 = I->getOperand(1);
    auto Op2Info = TTI.getOperandInfo(Op2);
    if (Op2Info.Kind == TargetTransformInfo::OK_AnyValue &&
        Legal->isInvariant(Op2))
      Op2Info.Kind = TargetTransformInfo::OK_UniformValue;

    SmallVector<const Value *, 4> Operands(I->operand_values());
    return TTI.getArithmeticInstrCost(
        I->getOpcode(), VectorTy, CostKind,
        {TargetTransformInfo::OK_AnyValue, TargetTransformInfo::OP_None},
        Op2Info, Operands, I, TLI);
  }
  case Instruction::FNeg: {
    return TTI.getArithmeticInstrCost(
        I->getOpcode(), VectorTy, CostKind,
        {TargetTransformInfo::OK_AnyValue, TargetTransformInfo::OP_None},
        {TargetTransformInfo::OK_AnyValue, TargetTransformInfo::OP_None},
        I->getOperand(0), I);
  }
  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(I);
    const SCEV *CondSCEV = SE->getSCEV(SI->getCondition());
    bool ScalarCond = (SE->isLoopInvariant(CondSCEV, TheLoop));

    const Value *Op0, *Op1;
    using namespace llvm::PatternMatch;
    if (!ScalarCond && (match(I, m_LogicalAnd(m_Value(Op0), m_Value(Op1))) ||
                        match(I, m_LogicalOr(m_Value(Op0), m_Value(Op1))))) {
      // select x, y, false --> x & y
      // select x, true, y --> x | y
      const auto [Op1VK, Op1VP] = TTI::getOperandInfo(Op0);
      const auto [Op2VK, Op2VP] = TTI::getOperandInfo(Op1);
      assert(Op0->getType()->getScalarSizeInBits() == 1 &&
              Op1->getType()->getScalarSizeInBits() == 1);

      SmallVector<const Value *, 2> Operands{Op0, Op1};
      return TTI.getArithmeticInstrCost(
          match(I, m_LogicalOr()) ? Instruction::Or : Instruction::And, VectorTy,
          CostKind, {Op1VK, Op1VP}, {Op2VK, Op2VP}, Operands, I);
    }

    Type *CondTy = SI->getCondition()->getType();
    if (!ScalarCond)
      CondTy = VectorType::get(CondTy, VF);

    CmpInst::Predicate Pred = CmpInst::BAD_ICMP_PREDICATE;
    if (auto *Cmp = dyn_cast<CmpInst>(SI->getCondition()))
      Pred = Cmp->getPredicate();
    return TTI.getCmpSelInstrCost(I->getOpcode(), VectorTy, CondTy, Pred,
                                  CostKind, I);
  }
  case Instruction::ICmp:
  case Instruction::FCmp: {
    Type *ValTy = I->getOperand(0)->getType();
    Instruction *Op0AsInstruction = dyn_cast<Instruction>(I->getOperand(0));
    if (canTruncateToMinimalBitwidth(Op0AsInstruction, VF))
      ValTy = IntegerType::get(ValTy->getContext(), MinBWs[Op0AsInstruction]);
    VectorTy = ToVectorTy(ValTy, VF);
    return TTI.getCmpSelInstrCost(I->getOpcode(), VectorTy, nullptr,
                                  cast<CmpInst>(I)->getPredicate(), CostKind,
                                  I);
  }
  case Instruction::Store:
  case Instruction::Load: {
    ElementCount Width = VF;
    if (Width.isVector()) {
      InstWidening Decision = getWideningDecision(I, Width);
      assert(Decision != CM_Unknown &&
             "CM decision should be taken at this point");
      if (getWideningCost(I, VF) == InstructionCost::getInvalid())
        return InstructionCost::getInvalid();
      if (Decision == CM_Scalarize)
        Width = ElementCount::getFixed(1);
    }
    VectorTy = ToVectorTy(getLoadStoreType(I), Width);
    return getMemoryInstructionCost(I, VF);
  }
  case Instruction::BitCast:
    if (I->getType()->isPointerTy())
      return 0;
    [[fallthrough]];
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc: {
    // Computes the CastContextHint from a Load/Store instruction.
    auto ComputeCCH = [&](Instruction *I) -> TTI::CastContextHint {
      assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
             "Expected a load or a store!");

      if (VF.isScalar() || !TheLoop->contains(I))
        return TTI::CastContextHint::Normal;

      switch (getWideningDecision(I, VF)) {
      case LoopVectorizationCostModel::CM_GatherScatter:
        return TTI::CastContextHint::GatherScatter;
      case LoopVectorizationCostModel::CM_Interleave:
        return TTI::CastContextHint::Interleave;
      case LoopVectorizationCostModel::CM_Scalarize:
      case LoopVectorizationCostModel::CM_Widen:
        return Legal->isMaskRequired(I) ? TTI::CastContextHint::Masked
                                        : TTI::CastContextHint::Normal;
      case LoopVectorizationCostModel::CM_Widen_Reverse:
        return TTI::CastContextHint::Reversed;
      case LoopVectorizationCostModel::CM_Unknown:
        llvm_unreachable("Instr did not go through cost modelling?");
      case LoopVectorizationCostModel::CM_VectorCall:
      case LoopVectorizationCostModel::CM_IntrinsicCall:
        llvm_unreachable_internal("Instr has invalid widening decision");
      }

      llvm_unreachable("Unhandled case!");
    };

    unsigned Opcode = I->getOpcode();
    TTI::CastContextHint CCH = TTI::CastContextHint::None;
    // For Trunc, the context is the only user, which must be a StoreInst.
    if (Opcode == Instruction::Trunc || Opcode == Instruction::FPTrunc) {
      if (I->hasOneUse())
        if (StoreInst *Store = dyn_cast<StoreInst>(*I->user_begin()))
          CCH = ComputeCCH(Store);
    }
    // For Z/Sext, the context is the operand, which must be a LoadInst.
    else if (Opcode == Instruction::ZExt || Opcode == Instruction::SExt ||
             Opcode == Instruction::FPExt) {
      if (LoadInst *Load = dyn_cast<LoadInst>(I->getOperand(0)))
        CCH = ComputeCCH(Load);
    }

    // We optimize the truncation of induction variables having constant
    // integer steps. The cost of these truncations is the same as the scalar
    // operation.
    if (isOptimizableIVTruncate(I, VF)) {
      auto *Trunc = cast<TruncInst>(I);
      return TTI.getCastInstrCost(Instruction::Trunc, Trunc->getDestTy(),
                                  Trunc->getSrcTy(), CCH, CostKind, Trunc);
    }

    // Detect reduction patterns
    if (auto RedCost = getReductionPatternCost(I, VF, VectorTy, CostKind))
      return *RedCost;

    Type *SrcScalarTy = I->getOperand(0)->getType();
    Instruction *Op0AsInstruction = dyn_cast<Instruction>(I->getOperand(0));
    if (canTruncateToMinimalBitwidth(Op0AsInstruction, VF))
      SrcScalarTy =
          IntegerType::get(SrcScalarTy->getContext(), MinBWs[Op0AsInstruction]);
    Type *SrcVecTy =
        VectorTy->isVectorTy() ? ToVectorTy(SrcScalarTy, VF) : SrcScalarTy;

    if (canTruncateToMinimalBitwidth(I, VF)) {
      // If the result type is <= the source type, there will be no extend
      // after truncating the users to the minimal required bitwidth.
      if (VectorTy->getScalarSizeInBits() <= SrcVecTy->getScalarSizeInBits() &&
          (I->getOpcode() == Instruction::ZExt ||
           I->getOpcode() == Instruction::SExt))
        return 0;
    }

    return TTI.getCastInstrCost(Opcode, VectorTy, SrcVecTy, CCH, CostKind, I);
  }
  case Instruction::Call:
    return getVectorCallCost(cast<CallInst>(I), VF);
  case Instruction::ExtractValue:
    return TTI.getInstructionCost(I, TTI::TCK_RecipThroughput);
  case Instruction::Alloca:
    // We cannot easily widen alloca to a scalable alloca, as
    // the result would need to be a vector of pointers.
    if (VF.isScalable())
      return InstructionCost::getInvalid();
    [[fallthrough]];
  default:
    // This opcode is unknown. Assume that it is the same as 'mul'.
    return TTI.getArithmeticInstrCost(Instruction::Mul, VectorTy, CostKind);
  } // end of switch.
}

void LoopVectorizationCostModel::collectValuesToIgnore() {
  // Ignore ephemeral values.
  CodeMetrics::collectEphemeralValues(TheLoop, AC, ValuesToIgnore);

  SmallVector<Value *, 4> DeadInterleavePointerOps;
  for (BasicBlock *BB : TheLoop->blocks())
    for (Instruction &I : *BB) {
      // Find all stores to invariant variables. Since they are going to sink
      // outside the loop we do not need calculate cost for them.
      StoreInst *SI;
      if ((SI = dyn_cast<StoreInst>(&I)) &&
          Legal->isInvariantAddressOfReduction(SI->getPointerOperand()))
        ValuesToIgnore.insert(&I);

      // For interleave groups, we only create a pointer for the start of the
      // interleave group. Queue up addresses of group members except the insert
      // position for further processing.
      if (isAccessInterleaved(&I)) {
        auto *Group = getInterleavedAccessGroup(&I);
        if (Group->getInsertPos() == &I)
          continue;
        Value *PointerOp = getLoadStorePointerOperand(&I);
        DeadInterleavePointerOps.push_back(PointerOp);
      }
    }

  // Mark ops feeding interleave group members as free, if they are only used
  // by other dead computations.
  for (unsigned I = 0; I != DeadInterleavePointerOps.size(); ++I) {
    auto *Op = dyn_cast<Instruction>(DeadInterleavePointerOps[I]);
    if (!Op || !TheLoop->contains(Op) || any_of(Op->users(), [this](User *U) {
          Instruction *UI = cast<Instruction>(U);
          return !VecValuesToIgnore.contains(U) &&
                 (!isAccessInterleaved(UI) ||
                  getInterleavedAccessGroup(UI)->getInsertPos() == UI);
        }))
      continue;
    VecValuesToIgnore.insert(Op);
    DeadInterleavePointerOps.append(Op->op_begin(), Op->op_end());
  }

  // Ignore type-promoting instructions we identified during reduction
  // detection.
  for (const auto &Reduction : Legal->getReductionVars()) {
    const RecurrenceDescriptor &RedDes = Reduction.second;
    const SmallPtrSetImpl<Instruction *> &Casts = RedDes.getCastInsts();
    VecValuesToIgnore.insert(Casts.begin(), Casts.end());
  }
  // Ignore type-casting instructions we identified during induction
  // detection.
  for (const auto &Induction : Legal->getInductionVars()) {
    const InductionDescriptor &IndDes = Induction.second;
    const SmallVectorImpl<Instruction *> &Casts = IndDes.getCastInsts();
    VecValuesToIgnore.insert(Casts.begin(), Casts.end());
  }
}

void LoopVectorizationCostModel::collectInLoopReductions() {
  for (const auto &Reduction : Legal->getReductionVars()) {
    PHINode *Phi = Reduction.first;
    const RecurrenceDescriptor &RdxDesc = Reduction.second;

    // We don't collect reductions that are type promoted (yet).
    if (RdxDesc.getRecurrenceType() != Phi->getType())
      continue;

    // If the target would prefer this reduction to happen "in-loop", then we
    // want to record it as such.
    unsigned Opcode = RdxDesc.getOpcode();
    if (!PreferInLoopReductions && !useOrderedReductions(RdxDesc) &&
        !TTI.preferInLoopReduction(Opcode, Phi->getType(),
                                   TargetTransformInfo::ReductionFlags()))
      continue;

    // Check that we can correctly put the reductions into the loop, by
    // finding the chain of operations that leads from the phi to the loop
    // exit value.
    SmallVector<Instruction *, 4> ReductionOperations =
        RdxDesc.getReductionOpChain(Phi, TheLoop);
    bool InLoop = !ReductionOperations.empty();

    if (InLoop) {
      InLoopReductions.insert(Phi);
      // Add the elements to InLoopReductionImmediateChains for cost modelling.
      Instruction *LastChain = Phi;
      for (auto *I : ReductionOperations) {
        InLoopReductionImmediateChains[I] = LastChain;
        LastChain = I;
      }
    }
    LLVM_DEBUG(dbgs() << "LV: Using " << (InLoop ? "inloop" : "out of loop")
                      << " reduction for phi: " << *Phi << "\n");
  }
}

VPValue *VPBuilder::createICmp(CmpInst::Predicate Pred, VPValue *A, VPValue *B,
                               DebugLoc DL, const Twine &Name) {
  assert(Pred >= CmpInst::FIRST_ICMP_PREDICATE &&
         Pred <= CmpInst::LAST_ICMP_PREDICATE && "invalid predicate");
  return tryInsertInstruction(
      new VPInstruction(Instruction::ICmp, Pred, A, B, DL, Name));
}

// This function will select a scalable VF if the target supports scalable
// vectors and a fixed one otherwise.
// TODO: we could return a pair of values that specify the max VF and
// min VF, to be used in `buildVPlans(MinVF, MaxVF)` instead of
// `buildVPlans(VF, VF)`. We cannot do it because VPLAN at the moment
// doesn't have a cost model that can choose which plan to execute if
// more than one is generated.
static ElementCount determineVPlanVF(const TargetTransformInfo &TTI,
                                     LoopVectorizationCostModel &CM) {
  unsigned WidestType;
  std::tie(std::ignore, WidestType) = CM.getSmallestAndWidestTypes();

  TargetTransformInfo::RegisterKind RegKind =
      TTI.enableScalableVectorization()
          ? TargetTransformInfo::RGK_ScalableVector
          : TargetTransformInfo::RGK_FixedWidthVector;

  TypeSize RegSize = TTI.getRegisterBitWidth(RegKind);
  unsigned N = RegSize.getKnownMinValue() / WidestType;
  return ElementCount::get(N, RegSize.isScalable());
}

VectorizationFactor
LoopVectorizationPlanner::planInVPlanNativePath(ElementCount UserVF) {
  ElementCount VF = UserVF;
  // Outer loop handling: They may require CFG and instruction level
  // transformations before even evaluating whether vectorization is profitable.
  // Since we cannot modify the incoming IR, we need to build VPlan upfront in
  // the vectorization pipeline.
  if (!OrigLoop->isInnermost()) {
    // If the user doesn't provide a vectorization factor, determine a
    // reasonable one.
    if (UserVF.isZero()) {
      VF = determineVPlanVF(TTI, CM);
      LLVM_DEBUG(dbgs() << "LV: VPlan computed VF " << VF << ".\n");

      // Make sure we have a VF > 1 for stress testing.
      if (VPlanBuildStressTest && (VF.isScalar() || VF.isZero())) {
        LLVM_DEBUG(dbgs() << "LV: VPlan stress testing: "
                          << "overriding computed VF.\n");
        VF = ElementCount::getFixed(4);
      }
    } else if (UserVF.isScalable() && !TTI.supportsScalableVectors() &&
               !ForceTargetSupportsScalableVectors) {
      LLVM_DEBUG(dbgs() << "LV: Not vectorizing. Scalable VF requested, but "
                        << "not supported by the target.\n");
      reportVectorizationFailure(
          "Scalable vectorization requested but not supported by the target",
          "the scalable user-specified vectorization width for outer-loop "
          "vectorization cannot be used because the target does not support "
          "scalable vectors.",
          "ScalableVFUnfeasible", ORE, OrigLoop);
      return VectorizationFactor::Disabled();
    }
    assert(EnableVPlanNativePath && "VPlan-native path is not enabled.");
    assert(isPowerOf2_32(VF.getKnownMinValue()) &&
           "VF needs to be a power of two");
    LLVM_DEBUG(dbgs() << "LV: Using " << (!UserVF.isZero() ? "user " : "")
                      << "VF " << VF << " to build VPlans.\n");
    buildVPlans(VF, VF);

    // For VPlan build stress testing, we bail out after VPlan construction.
    if (VPlanBuildStressTest)
      return VectorizationFactor::Disabled();

    return {VF, 0 /*Cost*/, 0 /* ScalarCost */};
  }

  LLVM_DEBUG(
      dbgs() << "LV: Not vectorizing. Inner loops aren't supported in the "
                "VPlan-native path.\n");
  return VectorizationFactor::Disabled();
}

std::optional<VectorizationFactor>
LoopVectorizationPlanner::plan(ElementCount UserVF, unsigned UserIC) {
  assert(OrigLoop->isInnermost() && "Inner loop expected.");
  CM.collectValuesToIgnore();
  CM.collectElementTypesForWidening();

  FixedScalableVFPair MaxFactors = CM.computeMaxVF(UserVF, UserIC);
  if (!MaxFactors) // Cases that should not to be vectorized nor interleaved.
    return std::nullopt;

  // Invalidate interleave groups if all blocks of loop will be predicated.
  if (CM.blockNeedsPredicationForAnyReason(OrigLoop->getHeader()) &&
      !useMaskedInterleavedAccesses(TTI)) {
    LLVM_DEBUG(
        dbgs()
        << "LV: Invalidate all interleaved groups due to fold-tail by masking "
           "which requires masked-interleaved support.\n");
    if (CM.InterleaveInfo.invalidateGroups())
      // Invalidating interleave groups also requires invalidating all decisions
      // based on them, which includes widening decisions and uniform and scalar
      // values.
      CM.invalidateCostModelingDecisions();
  }

  if (CM.foldTailByMasking())
    Legal->prepareToFoldTailByMasking();

  ElementCount MaxUserVF =
      UserVF.isScalable() ? MaxFactors.ScalableVF : MaxFactors.FixedVF;
  bool UserVFIsLegal = ElementCount::isKnownLE(UserVF, MaxUserVF);
  if (!UserVF.isZero() && UserVFIsLegal) {
    assert(isPowerOf2_32(UserVF.getKnownMinValue()) &&
           "VF needs to be a power of two");
    // Collect the instructions (and their associated costs) that will be more
    // profitable to scalarize.
    CM.collectInLoopReductions();
    if (CM.selectUserVectorizationFactor(UserVF)) {
      LLVM_DEBUG(dbgs() << "LV: Using user VF " << UserVF << ".\n");
      buildVPlansWithVPRecipes(UserVF, UserVF);
      if (!hasPlanWithVF(UserVF)) {
        LLVM_DEBUG(dbgs() << "LV: No VPlan could be built for " << UserVF
                          << ".\n");
        return std::nullopt;
      }

      LLVM_DEBUG(printPlans(dbgs()));
      return {{UserVF, 0, 0}};
    } else
      reportVectorizationInfo("UserVF ignored because of invalid costs.",
                              "InvalidCost", ORE, OrigLoop);
  }

  // Collect the Vectorization Factor Candidates.
  SmallVector<ElementCount> VFCandidates;
  for (auto VF = ElementCount::getFixed(1);
       ElementCount::isKnownLE(VF, MaxFactors.FixedVF); VF *= 2)
    VFCandidates.push_back(VF);
  for (auto VF = ElementCount::getScalable(1);
       ElementCount::isKnownLE(VF, MaxFactors.ScalableVF); VF *= 2)
    VFCandidates.push_back(VF);

  CM.collectInLoopReductions();
  for (const auto &VF : VFCandidates) {
    // Collect Uniform and Scalar instructions after vectorization with VF.
    CM.collectUniformsAndScalars(VF);

    // Collect the instructions (and their associated costs) that will be more
    // profitable to scalarize.
    if (VF.isVector())
      CM.collectInstsToScalarize(VF);
  }

  buildVPlansWithVPRecipes(ElementCount::getFixed(1), MaxFactors.FixedVF);
  buildVPlansWithVPRecipes(ElementCount::getScalable(1), MaxFactors.ScalableVF);

  LLVM_DEBUG(printPlans(dbgs()));
  if (VPlans.empty())
    return std::nullopt;
  if (all_of(VPlans,
             [](std::unique_ptr<VPlan> &P) { return P->hasScalarVFOnly(); }))
    return VectorizationFactor::Disabled();

  // Select the optimal vectorization factor according to the legacy cost-model.
  // This is now only used to verify the decisions by the new VPlan-based
  // cost-model and will be retired once the VPlan-based cost-model is
  // stabilized.
  VectorizationFactor VF = selectVectorizationFactor();
  assert((VF.Width.isScalar() || VF.ScalarCost > 0) && "when vectorizing, the scalar cost must be non-zero.");
  if (!hasPlanWithVF(VF.Width)) {
    LLVM_DEBUG(dbgs() << "LV: No VPlan could be built for " << VF.Width
                      << ".\n");
    return std::nullopt;
  }
  return VF;
}

InstructionCost VPCostContext::getLegacyCost(Instruction *UI,
                                             ElementCount VF) const {
  return CM.getInstructionCost(UI, VF);
}

bool VPCostContext::skipCostComputation(Instruction *UI, bool IsVector) const {
  return CM.ValuesToIgnore.contains(UI) ||
         (IsVector && CM.VecValuesToIgnore.contains(UI)) ||
         SkipCostComputation.contains(UI);
}

InstructionCost LoopVectorizationPlanner::cost(VPlan &Plan,
                                               ElementCount VF) const {
  InstructionCost Cost = 0;
  LLVMContext &LLVMCtx = OrigLoop->getHeader()->getContext();
  VPCostContext CostCtx(CM.TTI, Legal->getWidestInductionType(), LLVMCtx, CM);

  // Cost modeling for inductions is inaccurate in the legacy cost model
  // compared to the recipes that are generated. To match here initially during
  // VPlan cost model bring up directly use the induction costs from the legacy
  // cost model. Note that we do this as pre-processing; the VPlan may not have
  // any recipes associated with the original induction increment instruction
  // and may replace truncates with VPWidenIntOrFpInductionRecipe. We precompute
  // the cost of induction phis and increments (both that are represented by
  // recipes and those that are not), to avoid distinguishing between them here,
  // and skip all recipes that represent induction phis and increments (the
  // former case) later on, if they exist, to avoid counting them twice.
  // Similarly we pre-compute the cost of any optimized truncates.
  // TODO: Switch to more accurate costing based on VPlan.
  for (const auto &[IV, IndDesc] : Legal->getInductionVars()) {
    Instruction *IVInc = cast<Instruction>(
        IV->getIncomingValueForBlock(OrigLoop->getLoopLatch()));
    SmallVector<Instruction *> IVInsts = {IV, IVInc};
    for (User *U : IV->users()) {
      auto *CI = cast<Instruction>(U);
      if (!CostCtx.CM.isOptimizableIVTruncate(CI, VF))
        continue;
      IVInsts.push_back(CI);
    }
    for (Instruction *IVInst : IVInsts) {
      if (!CostCtx.SkipCostComputation.insert(IVInst).second)
        continue;
      InstructionCost InductionCost = CostCtx.getLegacyCost(IVInst, VF);
      LLVM_DEBUG({
        dbgs() << "Cost of " << InductionCost << " for VF " << VF
               << ": induction instruction " << *IVInst << "\n";
      });
      Cost += InductionCost;
    }
  }

  /// Compute the cost of all exiting conditions of the loop using the legacy
  /// cost model. This is to match the legacy behavior, which adds the cost of
  /// all exit conditions. Note that this over-estimates the cost, as there will
  /// be a single condition to control the vector loop.
  SmallVector<BasicBlock *> Exiting;
  CM.TheLoop->getExitingBlocks(Exiting);
  SetVector<Instruction *> ExitInstrs;
  // Collect all exit conditions.
  for (BasicBlock *EB : Exiting) {
    auto *Term = dyn_cast<BranchInst>(EB->getTerminator());
    if (!Term)
      continue;
    if (auto *CondI = dyn_cast<Instruction>(Term->getOperand(0))) {
      ExitInstrs.insert(CondI);
    }
  }
  // Compute the cost of all instructions only feeding the exit conditions.
  for (unsigned I = 0; I != ExitInstrs.size(); ++I) {
    Instruction *CondI = ExitInstrs[I];
    if (!OrigLoop->contains(CondI) ||
        !CostCtx.SkipCostComputation.insert(CondI).second)
      continue;
    Cost += CostCtx.getLegacyCost(CondI, VF);
    for (Value *Op : CondI->operands()) {
      auto *OpI = dyn_cast<Instruction>(Op);
      if (!OpI || any_of(OpI->users(), [&ExitInstrs, this](User *U) {
            return OrigLoop->contains(cast<Instruction>(U)->getParent()) &&
                   !ExitInstrs.contains(cast<Instruction>(U));
          }))
        continue;
      ExitInstrs.insert(OpI);
    }
  }

  // The legacy cost model has special logic to compute the cost of in-loop
  // reductions, which may be smaller than the sum of all instructions involved
  // in the reduction. For AnyOf reductions, VPlan codegen may remove the select
  // which the legacy cost model uses to assign cost. Pre-compute their costs
  // for now.
  // TODO: Switch to costing based on VPlan once the logic has been ported.
  for (const auto &[RedPhi, RdxDesc] : Legal->getReductionVars()) {
    if (!CM.isInLoopReduction(RedPhi) &&
        !RecurrenceDescriptor::isAnyOfRecurrenceKind(
            RdxDesc.getRecurrenceKind()))
      continue;

    // AnyOf reduction codegen may remove the select. To match the legacy cost
    // model, pre-compute the cost for AnyOf reductions here.
    if (RecurrenceDescriptor::isAnyOfRecurrenceKind(
            RdxDesc.getRecurrenceKind())) {
      auto *Select = cast<SelectInst>(*find_if(
          RedPhi->users(), [](User *U) { return isa<SelectInst>(U); }));
      assert(!CostCtx.SkipCostComputation.contains(Select) &&
             "reduction op visited multiple times");
      CostCtx.SkipCostComputation.insert(Select);
      auto ReductionCost = CostCtx.getLegacyCost(Select, VF);
      LLVM_DEBUG(dbgs() << "Cost of " << ReductionCost << " for VF " << VF
                        << ":\n any-of reduction " << *Select << "\n");
      Cost += ReductionCost;
      continue;
    }

    const auto &ChainOps = RdxDesc.getReductionOpChain(RedPhi, OrigLoop);
    SetVector<Instruction *> ChainOpsAndOperands(ChainOps.begin(),
                                                 ChainOps.end());
    // Also include the operands of instructions in the chain, as the cost-model
    // may mark extends as free.
    for (auto *ChainOp : ChainOps) {
      for (Value *Op : ChainOp->operands()) {
        if (auto *I = dyn_cast<Instruction>(Op))
          ChainOpsAndOperands.insert(I);
      }
    }

    // Pre-compute the cost for I, if it has a reduction pattern cost.
    for (Instruction *I : ChainOpsAndOperands) {
      auto ReductionCost = CM.getReductionPatternCost(
          I, VF, ToVectorTy(I->getType(), VF), TTI::TCK_RecipThroughput);
      if (!ReductionCost)
        continue;

      assert(!CostCtx.SkipCostComputation.contains(I) &&
             "reduction op visited multiple times");
      CostCtx.SkipCostComputation.insert(I);
      LLVM_DEBUG(dbgs() << "Cost of " << ReductionCost << " for VF " << VF
                        << ":\n in-loop reduction " << *I << "\n");
      Cost += *ReductionCost;
    }
  }

  // Pre-compute the costs for branches except for the backedge, as the number
  // of replicate regions in a VPlan may not directly match the number of
  // branches, which would lead to different decisions.
  // TODO: Compute cost of branches for each replicate region in the VPlan,
  // which is more accurate than the legacy cost model.
  for (BasicBlock *BB : OrigLoop->blocks()) {
    if (BB == OrigLoop->getLoopLatch())
      continue;
    CostCtx.SkipCostComputation.insert(BB->getTerminator());
    auto BranchCost = CostCtx.getLegacyCost(BB->getTerminator(), VF);
    Cost += BranchCost;
  }
  // Now compute and add the VPlan-based cost.
  Cost += Plan.cost(VF, CostCtx);
  LLVM_DEBUG(dbgs() << "Cost for VF " << VF << ": " << Cost << "\n");
  return Cost;
}

VPlan &LoopVectorizationPlanner::getBestPlan() const {
  // If there is a single VPlan with a single VF, return it directly.
  VPlan &FirstPlan = *VPlans[0];
  if (VPlans.size() == 1 && size(FirstPlan.vectorFactors()) == 1)
    return FirstPlan;

  VPlan *BestPlan = &FirstPlan;
  ElementCount ScalarVF = ElementCount::getFixed(1);
  assert(hasPlanWithVF(ScalarVF) &&
         "More than a single plan/VF w/o any plan having scalar VF");

  // TODO: Compute scalar cost using VPlan-based cost model.
  InstructionCost ScalarCost = CM.expectedCost(ScalarVF);
  VectorizationFactor BestFactor(ScalarVF, ScalarCost, ScalarCost);

  bool ForceVectorization = Hints.getForce() == LoopVectorizeHints::FK_Enabled;
  if (ForceVectorization) {
    // Ignore scalar width, because the user explicitly wants vectorization.
    // Initialize cost to max so that VF = 2 is, at least, chosen during cost
    // evaluation.
    BestFactor.Cost = InstructionCost::getMax();
  }

  for (auto &P : VPlans) {
    for (ElementCount VF : P->vectorFactors()) {
      if (VF.isScalar())
        continue;
      if (!ForceVectorization && !willGenerateVectors(*P, VF, TTI)) {
        LLVM_DEBUG(
            dbgs()
            << "LV: Not considering vector loop of width " << VF
            << " because it will not generate any vector instructions.\n");
        continue;
      }

      InstructionCost Cost = cost(*P, VF);
      VectorizationFactor CurrentFactor(VF, Cost, ScalarCost);
      if (isMoreProfitable(CurrentFactor, BestFactor)) {
        BestFactor = CurrentFactor;
        BestPlan = &*P;
      }
    }
  }
  BestPlan->setVF(BestFactor.Width);
  return *BestPlan;
}

VPlan &LoopVectorizationPlanner::getBestPlanFor(ElementCount VF) const {
  assert(count_if(VPlans,
                  [VF](const VPlanPtr &Plan) { return Plan->hasVF(VF); }) ==
             1 &&
         "Best VF has not a single VPlan.");

  for (const VPlanPtr &Plan : VPlans) {
    if (Plan->hasVF(VF))
      return *Plan.get();
  }
  llvm_unreachable("No plan found!");
}

static void AddRuntimeUnrollDisableMetaData(Loop *L) {
  SmallVector<Metadata *, 4> MDs;
  // Reserve first location for self reference to the LoopID metadata node.
  MDs.push_back(nullptr);
  bool IsUnrollMetadata = false;
  MDNode *LoopID = L->getLoopID();
  if (LoopID) {
    // First find existing loop unrolling disable metadata.
    for (unsigned i = 1, ie = LoopID->getNumOperands(); i < ie; ++i) {
      auto *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
      if (MD) {
        const auto *S = dyn_cast<MDString>(MD->getOperand(0));
        IsUnrollMetadata =
            S && S->getString().starts_with("llvm.loop.unroll.disable");
      }
      MDs.push_back(LoopID->getOperand(i));
    }
  }

  if (!IsUnrollMetadata) {
    // Add runtime unroll disable metadata.
    LLVMContext &Context = L->getHeader()->getContext();
    SmallVector<Metadata *, 1> DisableOperands;
    DisableOperands.push_back(
        MDString::get(Context, "llvm.loop.unroll.runtime.disable"));
    MDNode *DisableNode = MDNode::get(Context, DisableOperands);
    MDs.push_back(DisableNode);
    MDNode *NewLoopID = MDNode::get(Context, MDs);
    // Set operand 0 to refer to the loop id itself.
    NewLoopID->replaceOperandWith(0, NewLoopID);
    L->setLoopID(NewLoopID);
  }
}

// Check if \p RedResult is a ComputeReductionResult instruction, and if it is
// create a merge phi node for it and add it to \p ReductionResumeValues.
static void createAndCollectMergePhiForReduction(
    VPInstruction *RedResult,
    DenseMap<const RecurrenceDescriptor *, Value *> &ReductionResumeValues,
    VPTransformState &State, Loop *OrigLoop, BasicBlock *LoopMiddleBlock,
    bool VectorizingEpilogue) {
  if (!RedResult ||
      RedResult->getOpcode() != VPInstruction::ComputeReductionResult)
    return;

  auto *PhiR = cast<VPReductionPHIRecipe>(RedResult->getOperand(0));
  const RecurrenceDescriptor &RdxDesc = PhiR->getRecurrenceDescriptor();

  Value *FinalValue =
      State.get(RedResult, VPIteration(State.UF - 1, VPLane::getFirstLane()));
  auto *ResumePhi =
      dyn_cast<PHINode>(PhiR->getStartValue()->getUnderlyingValue());
  if (VectorizingEpilogue && RecurrenceDescriptor::isAnyOfRecurrenceKind(
                                 RdxDesc.getRecurrenceKind())) {
    auto *Cmp = cast<ICmpInst>(PhiR->getStartValue()->getUnderlyingValue());
    assert(Cmp->getPredicate() == CmpInst::ICMP_NE);
    assert(Cmp->getOperand(1) == RdxDesc.getRecurrenceStartValue());
    ResumePhi = cast<PHINode>(Cmp->getOperand(0));
  }
  assert((!VectorizingEpilogue || ResumePhi) &&
         "when vectorizing the epilogue loop, we need a resume phi from main "
         "vector loop");

  // TODO: bc.merge.rdx should not be created here, instead it should be
  // modeled in VPlan.
  BasicBlock *LoopScalarPreHeader = OrigLoop->getLoopPreheader();
  // Create a phi node that merges control-flow from the backedge-taken check
  // block and the middle block.
  auto *BCBlockPhi =
      PHINode::Create(FinalValue->getType(), 2, "bc.merge.rdx",
                      LoopScalarPreHeader->getTerminator()->getIterator());

  // If we are fixing reductions in the epilogue loop then we should already
  // have created a bc.merge.rdx Phi after the main vector body. Ensure that
  // we carry over the incoming values correctly.
  for (auto *Incoming : predecessors(LoopScalarPreHeader)) {
    if (Incoming == LoopMiddleBlock)
      BCBlockPhi->addIncoming(FinalValue, Incoming);
    else if (ResumePhi && is_contained(ResumePhi->blocks(), Incoming))
      BCBlockPhi->addIncoming(ResumePhi->getIncomingValueForBlock(Incoming),
                              Incoming);
    else
      BCBlockPhi->addIncoming(RdxDesc.getRecurrenceStartValue(), Incoming);
  }

  auto *OrigPhi = cast<PHINode>(PhiR->getUnderlyingValue());
  // TODO: This fixup should instead be modeled in VPlan.
  // Fix the scalar loop reduction variable with the incoming reduction sum
  // from the vector body and from the backedge value.
  int IncomingEdgeBlockIdx =
      OrigPhi->getBasicBlockIndex(OrigLoop->getLoopLatch());
  assert(IncomingEdgeBlockIdx >= 0 && "Invalid block index");
  // Pick the other block.
  int SelfEdgeBlockIdx = (IncomingEdgeBlockIdx ? 0 : 1);
  OrigPhi->setIncomingValue(SelfEdgeBlockIdx, BCBlockPhi);
  Instruction *LoopExitInst = RdxDesc.getLoopExitInstr();
  OrigPhi->setIncomingValue(IncomingEdgeBlockIdx, LoopExitInst);

  ReductionResumeValues[&RdxDesc] = BCBlockPhi;
}

std::pair<DenseMap<const SCEV *, Value *>,
          DenseMap<const RecurrenceDescriptor *, Value *>>
LoopVectorizationPlanner::executePlan(
    ElementCount BestVF, unsigned BestUF, VPlan &BestVPlan,
    InnerLoopVectorizer &ILV, DominatorTree *DT, bool IsEpilogueVectorization,
    const DenseMap<const SCEV *, Value *> *ExpandedSCEVs) {
  assert(BestVPlan.hasVF(BestVF) &&
         "Trying to execute plan with unsupported VF");
  assert(BestVPlan.hasUF(BestUF) &&
         "Trying to execute plan with unsupported UF");
  assert(
      (IsEpilogueVectorization || !ExpandedSCEVs) &&
      "expanded SCEVs to reuse can only be used during epilogue vectorization");
  (void)IsEpilogueVectorization;

  VPlanTransforms::optimizeForVFAndUF(BestVPlan, BestVF, BestUF, PSE);

  LLVM_DEBUG(dbgs() << "Executing best plan with VF=" << BestVF
                    << ", UF=" << BestUF << '\n');
  BestVPlan.setName("Final VPlan");
  LLVM_DEBUG(BestVPlan.dump());

  // Perform the actual loop transformation.
  VPTransformState State(BestVF, BestUF, LI, DT, ILV.Builder, &ILV, &BestVPlan,
                         OrigLoop->getHeader()->getContext());

  // 0. Generate SCEV-dependent code into the preheader, including TripCount,
  // before making any changes to the CFG.
  if (!BestVPlan.getPreheader()->empty()) {
    State.CFG.PrevBB = OrigLoop->getLoopPreheader();
    State.Builder.SetInsertPoint(OrigLoop->getLoopPreheader()->getTerminator());
    BestVPlan.getPreheader()->execute(&State);
  }
  if (!ILV.getTripCount())
    ILV.setTripCount(State.get(BestVPlan.getTripCount(), {0, 0}));
  else
    assert(IsEpilogueVectorization && "should only re-use the existing trip "
                                      "count during epilogue vectorization");

  // 1. Set up the skeleton for vectorization, including vector pre-header and
  // middle block. The vector loop is created during VPlan execution.
  Value *CanonicalIVStartValue;
  std::tie(State.CFG.PrevBB, CanonicalIVStartValue) =
      ILV.createVectorizedLoopSkeleton(ExpandedSCEVs ? *ExpandedSCEVs
                                                     : State.ExpandedSCEVs);
#ifdef EXPENSIVE_CHECKS
  assert(DT->verify(DominatorTree::VerificationLevel::Fast));
#endif

  // Only use noalias metadata when using memory checks guaranteeing no overlap
  // across all iterations.
  const LoopAccessInfo *LAI = ILV.Legal->getLAI();
  std::unique_ptr<LoopVersioning> LVer = nullptr;
  if (LAI && !LAI->getRuntimePointerChecking()->getChecks().empty() &&
      !LAI->getRuntimePointerChecking()->getDiffChecks()) {

    //  We currently don't use LoopVersioning for the actual loop cloning but we
    //  still use it to add the noalias metadata.
    //  TODO: Find a better way to re-use LoopVersioning functionality to add
    //        metadata.
    LVer = std::make_unique<LoopVersioning>(
        *LAI, LAI->getRuntimePointerChecking()->getChecks(), OrigLoop, LI, DT,
        PSE.getSE());
    State.LVer = &*LVer;
    State.LVer->prepareNoAliasMetadata();
  }

  ILV.printDebugTracesAtStart();

  //===------------------------------------------------===//
  //
  // Notice: any optimization or new instruction that go
  // into the code below should also be implemented in
  // the cost-model.
  //
  //===------------------------------------------------===//

  // 2. Copy and widen instructions from the old loop into the new loop.
  BestVPlan.prepareToExecute(ILV.getTripCount(),
                             ILV.getOrCreateVectorTripCount(nullptr),
                             CanonicalIVStartValue, State);

  BestVPlan.execute(&State);

  // 2.5 Collect reduction resume values.
  DenseMap<const RecurrenceDescriptor *, Value *> ReductionResumeValues;
  auto *ExitVPBB =
      cast<VPBasicBlock>(BestVPlan.getVectorLoopRegion()->getSingleSuccessor());
  for (VPRecipeBase &R : *ExitVPBB) {
    createAndCollectMergePhiForReduction(
        dyn_cast<VPInstruction>(&R), ReductionResumeValues, State, OrigLoop,
        State.CFG.VPBB2IRBB[ExitVPBB], ExpandedSCEVs);
  }

  // 2.6. Maintain Loop Hints
  // Keep all loop hints from the original loop on the vector loop (we'll
  // replace the vectorizer-specific hints below).
  MDNode *OrigLoopID = OrigLoop->getLoopID();

  std::optional<MDNode *> VectorizedLoopID =
      makeFollowupLoopID(OrigLoopID, {LLVMLoopVectorizeFollowupAll,
                                      LLVMLoopVectorizeFollowupVectorized});

  VPBasicBlock *HeaderVPBB =
      BestVPlan.getVectorLoopRegion()->getEntryBasicBlock();
  Loop *L = LI->getLoopFor(State.CFG.VPBB2IRBB[HeaderVPBB]);
  if (VectorizedLoopID)
    L->setLoopID(*VectorizedLoopID);
  else {
    // Keep all loop hints from the original loop on the vector loop (we'll
    // replace the vectorizer-specific hints below).
    if (MDNode *LID = OrigLoop->getLoopID())
      L->setLoopID(LID);

    LoopVectorizeHints Hints(L, true, *ORE);
    Hints.setAlreadyVectorized();
  }
  TargetTransformInfo::UnrollingPreferences UP;
  TTI.getUnrollingPreferences(L, *PSE.getSE(), UP, ORE);
  if (!UP.UnrollVectorizedLoop || CanonicalIVStartValue)
    AddRuntimeUnrollDisableMetaData(L);

  // 3. Fix the vectorized code: take care of header phi's, live-outs,
  //    predication, updating analyses.
  ILV.fixVectorizedLoop(State, BestVPlan);

  ILV.printDebugTracesAtEnd();

  // 4. Adjust branch weight of the branch in the middle block.
  auto *MiddleTerm =
      cast<BranchInst>(State.CFG.VPBB2IRBB[ExitVPBB]->getTerminator());
  if (MiddleTerm->isConditional() &&
      hasBranchWeightMD(*OrigLoop->getLoopLatch()->getTerminator())) {
    // Assume that `Count % VectorTripCount` is equally distributed.
    unsigned TripCount = State.UF * State.VF.getKnownMinValue();
    assert(TripCount > 0 && "trip count should not be zero");
    const uint32_t Weights[] = {1, TripCount - 1};
    setBranchWeights(*MiddleTerm, Weights, /*IsExpected=*/false);
  }

  return {State.ExpandedSCEVs, ReductionResumeValues};
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void LoopVectorizationPlanner::printPlans(raw_ostream &O) {
  for (const auto &Plan : VPlans)
    if (PrintVPlansInDotFormat)
      Plan->printDOT(O);
    else
      Plan->print(O);
}
#endif

//===--------------------------------------------------------------------===//
// EpilogueVectorizerMainLoop
//===--------------------------------------------------------------------===//

/// This function is partially responsible for generating the control flow
/// depicted in https://llvm.org/docs/Vectorizers.html#epilogue-vectorization.
std::pair<BasicBlock *, Value *>
EpilogueVectorizerMainLoop::createEpilogueVectorizedLoopSkeleton(
    const SCEV2ValueTy &ExpandedSCEVs) {
  createVectorLoopSkeleton("");

  // Generate the code to check the minimum iteration count of the vector
  // epilogue (see below).
  EPI.EpilogueIterationCountCheck =
      emitIterationCountCheck(LoopScalarPreHeader, true);
  EPI.EpilogueIterationCountCheck->setName("iter.check");

  // Generate the code to check any assumptions that we've made for SCEV
  // expressions.
  EPI.SCEVSafetyCheck = emitSCEVChecks(LoopScalarPreHeader);

  // Generate the code that checks at runtime if arrays overlap. We put the
  // checks into a separate block to make the more common case of few elements
  // faster.
  EPI.MemSafetyCheck = emitMemRuntimeChecks(LoopScalarPreHeader);

  // Generate the iteration count check for the main loop, *after* the check
  // for the epilogue loop, so that the path-length is shorter for the case
  // that goes directly through the vector epilogue. The longer-path length for
  // the main loop is compensated for, by the gain from vectorizing the larger
  // trip count. Note: the branch will get updated later on when we vectorize
  // the epilogue.
  EPI.MainLoopIterationCountCheck =
      emitIterationCountCheck(LoopScalarPreHeader, false);

  // Generate the induction variable.
  EPI.VectorTripCount = getOrCreateVectorTripCount(LoopVectorPreHeader);

  // Skip induction resume value creation here because they will be created in
  // the second pass for the scalar loop. The induction resume values for the
  // inductions in the epilogue loop are created before executing the plan for
  // the epilogue loop.

  return {LoopVectorPreHeader, nullptr};
}

void EpilogueVectorizerMainLoop::printDebugTracesAtStart() {
  LLVM_DEBUG({
    dbgs() << "Create Skeleton for epilogue vectorized loop (first pass)\n"
           << "Main Loop VF:" << EPI.MainLoopVF
           << ", Main Loop UF:" << EPI.MainLoopUF
           << ", Epilogue Loop VF:" << EPI.EpilogueVF
           << ", Epilogue Loop UF:" << EPI.EpilogueUF << "\n";
  });
}

void EpilogueVectorizerMainLoop::printDebugTracesAtEnd() {
  DEBUG_WITH_TYPE(VerboseDebug, {
    dbgs() << "intermediate fn:\n"
           << *OrigLoop->getHeader()->getParent() << "\n";
  });
}

BasicBlock *
EpilogueVectorizerMainLoop::emitIterationCountCheck(BasicBlock *Bypass,
                                                    bool ForEpilogue) {
  assert(Bypass && "Expected valid bypass basic block.");
  ElementCount VFactor = ForEpilogue ? EPI.EpilogueVF : VF;
  unsigned UFactor = ForEpilogue ? EPI.EpilogueUF : UF;
  Value *Count = getTripCount();
  // Reuse existing vector loop preheader for TC checks.
  // Note that new preheader block is generated for vector loop.
  BasicBlock *const TCCheckBlock = LoopVectorPreHeader;
  IRBuilder<> Builder(TCCheckBlock->getTerminator());

  // Generate code to check if the loop's trip count is less than VF * UF of the
  // main vector loop.
  auto P = Cost->requiresScalarEpilogue(ForEpilogue ? EPI.EpilogueVF.isVector()
                                                    : VF.isVector())
               ? ICmpInst::ICMP_ULE
               : ICmpInst::ICMP_ULT;

  Value *CheckMinIters = Builder.CreateICmp(
      P, Count, createStepForVF(Builder, Count->getType(), VFactor, UFactor),
      "min.iters.check");

  if (!ForEpilogue)
    TCCheckBlock->setName("vector.main.loop.iter.check");

  // Create new preheader for vector loop.
  LoopVectorPreHeader = SplitBlock(TCCheckBlock, TCCheckBlock->getTerminator(),
                                   DT, LI, nullptr, "vector.ph");

  if (ForEpilogue) {
    assert(DT->properlyDominates(DT->getNode(TCCheckBlock),
                                 DT->getNode(Bypass)->getIDom()) &&
           "TC check is expected to dominate Bypass");

    // Update dominator for Bypass.
    DT->changeImmediateDominator(Bypass, TCCheckBlock);
    LoopBypassBlocks.push_back(TCCheckBlock);

    // Save the trip count so we don't have to regenerate it in the
    // vec.epilog.iter.check. This is safe to do because the trip count
    // generated here dominates the vector epilog iter check.
    EPI.TripCount = Count;
  }

  BranchInst &BI =
      *BranchInst::Create(Bypass, LoopVectorPreHeader, CheckMinIters);
  if (hasBranchWeightMD(*OrigLoop->getLoopLatch()->getTerminator()))
    setBranchWeights(BI, MinItersBypassWeights, /*IsExpected=*/false);
  ReplaceInstWithInst(TCCheckBlock->getTerminator(), &BI);

  return TCCheckBlock;
}

//===--------------------------------------------------------------------===//
// EpilogueVectorizerEpilogueLoop
//===--------------------------------------------------------------------===//

/// This function is partially responsible for generating the control flow
/// depicted in https://llvm.org/docs/Vectorizers.html#epilogue-vectorization.
std::pair<BasicBlock *, Value *>
EpilogueVectorizerEpilogueLoop::createEpilogueVectorizedLoopSkeleton(
    const SCEV2ValueTy &ExpandedSCEVs) {
  createVectorLoopSkeleton("vec.epilog.");

  // Now, compare the remaining count and if there aren't enough iterations to
  // execute the vectorized epilogue skip to the scalar part.
  LoopVectorPreHeader->setName("vec.epilog.ph");
  BasicBlock *VecEpilogueIterationCountCheck =
      SplitBlock(LoopVectorPreHeader, LoopVectorPreHeader->begin(), DT, LI,
                 nullptr, "vec.epilog.iter.check", true);
  emitMinimumVectorEpilogueIterCountCheck(LoopScalarPreHeader,
                                          VecEpilogueIterationCountCheck);

  // Adjust the control flow taking the state info from the main loop
  // vectorization into account.
  assert(EPI.MainLoopIterationCountCheck && EPI.EpilogueIterationCountCheck &&
         "expected this to be saved from the previous pass.");
  EPI.MainLoopIterationCountCheck->getTerminator()->replaceUsesOfWith(
      VecEpilogueIterationCountCheck, LoopVectorPreHeader);

  DT->changeImmediateDominator(LoopVectorPreHeader,
                               EPI.MainLoopIterationCountCheck);

  EPI.EpilogueIterationCountCheck->getTerminator()->replaceUsesOfWith(
      VecEpilogueIterationCountCheck, LoopScalarPreHeader);

  if (EPI.SCEVSafetyCheck)
    EPI.SCEVSafetyCheck->getTerminator()->replaceUsesOfWith(
        VecEpilogueIterationCountCheck, LoopScalarPreHeader);
  if (EPI.MemSafetyCheck)
    EPI.MemSafetyCheck->getTerminator()->replaceUsesOfWith(
        VecEpilogueIterationCountCheck, LoopScalarPreHeader);

  DT->changeImmediateDominator(
      VecEpilogueIterationCountCheck,
      VecEpilogueIterationCountCheck->getSinglePredecessor());

  DT->changeImmediateDominator(LoopScalarPreHeader,
                               EPI.EpilogueIterationCountCheck);
  if (!Cost->requiresScalarEpilogue(EPI.EpilogueVF.isVector()))
    // If there is an epilogue which must run, there's no edge from the
    // middle block to exit blocks  and thus no need to update the immediate
    // dominator of the exit blocks.
    DT->changeImmediateDominator(LoopExitBlock,
                                 EPI.EpilogueIterationCountCheck);

  // Keep track of bypass blocks, as they feed start values to the induction and
  // reduction phis in the scalar loop preheader.
  if (EPI.SCEVSafetyCheck)
    LoopBypassBlocks.push_back(EPI.SCEVSafetyCheck);
  if (EPI.MemSafetyCheck)
    LoopBypassBlocks.push_back(EPI.MemSafetyCheck);
  LoopBypassBlocks.push_back(EPI.EpilogueIterationCountCheck);

  // The vec.epilog.iter.check block may contain Phi nodes from inductions or
  // reductions which merge control-flow from the latch block and the middle
  // block. Update the incoming values here and move the Phi into the preheader.
  SmallVector<PHINode *, 4> PhisInBlock;
  for (PHINode &Phi : VecEpilogueIterationCountCheck->phis())
    PhisInBlock.push_back(&Phi);

  for (PHINode *Phi : PhisInBlock) {
    Phi->moveBefore(LoopVectorPreHeader->getFirstNonPHI());
    Phi->replaceIncomingBlockWith(
        VecEpilogueIterationCountCheck->getSinglePredecessor(),
        VecEpilogueIterationCountCheck);

    // If the phi doesn't have an incoming value from the
    // EpilogueIterationCountCheck, we are done. Otherwise remove the incoming
    // value and also those from other check blocks. This is needed for
    // reduction phis only.
    if (none_of(Phi->blocks(), [&](BasicBlock *IncB) {
          return EPI.EpilogueIterationCountCheck == IncB;
        }))
      continue;
    Phi->removeIncomingValue(EPI.EpilogueIterationCountCheck);
    if (EPI.SCEVSafetyCheck)
      Phi->removeIncomingValue(EPI.SCEVSafetyCheck);
    if (EPI.MemSafetyCheck)
      Phi->removeIncomingValue(EPI.MemSafetyCheck);
  }

  // Generate a resume induction for the vector epilogue and put it in the
  // vector epilogue preheader
  Type *IdxTy = Legal->getWidestInductionType();
  PHINode *EPResumeVal = PHINode::Create(IdxTy, 2, "vec.epilog.resume.val");
  EPResumeVal->insertBefore(LoopVectorPreHeader->getFirstNonPHIIt());
  EPResumeVal->addIncoming(EPI.VectorTripCount, VecEpilogueIterationCountCheck);
  EPResumeVal->addIncoming(ConstantInt::get(IdxTy, 0),
                           EPI.MainLoopIterationCountCheck);

  // Generate induction resume values. These variables save the new starting
  // indexes for the scalar loop. They are used to test if there are any tail
  // iterations left once the vector loop has completed.
  // Note that when the vectorized epilogue is skipped due to iteration count
  // check, then the resume value for the induction variable comes from
  // the trip count of the main vector loop, hence passing the AdditionalBypass
  // argument.
  createInductionResumeValues(ExpandedSCEVs,
                              {VecEpilogueIterationCountCheck,
                               EPI.VectorTripCount} /* AdditionalBypass */);

  return {LoopVectorPreHeader, EPResumeVal};
}

BasicBlock *
EpilogueVectorizerEpilogueLoop::emitMinimumVectorEpilogueIterCountCheck(
    BasicBlock *Bypass, BasicBlock *Insert) {

  assert(EPI.TripCount &&
         "Expected trip count to have been safed in the first pass.");
  assert(
      (!isa<Instruction>(EPI.TripCount) ||
       DT->dominates(cast<Instruction>(EPI.TripCount)->getParent(), Insert)) &&
      "saved trip count does not dominate insertion point.");
  Value *TC = EPI.TripCount;
  IRBuilder<> Builder(Insert->getTerminator());
  Value *Count = Builder.CreateSub(TC, EPI.VectorTripCount, "n.vec.remaining");

  // Generate code to check if the loop's trip count is less than VF * UF of the
  // vector epilogue loop.
  auto P = Cost->requiresScalarEpilogue(EPI.EpilogueVF.isVector())
               ? ICmpInst::ICMP_ULE
               : ICmpInst::ICMP_ULT;

  Value *CheckMinIters =
      Builder.CreateICmp(P, Count,
                         createStepForVF(Builder, Count->getType(),
                                         EPI.EpilogueVF, EPI.EpilogueUF),
                         "min.epilog.iters.check");

  BranchInst &BI =
      *BranchInst::Create(Bypass, LoopVectorPreHeader, CheckMinIters);
  if (hasBranchWeightMD(*OrigLoop->getLoopLatch()->getTerminator())) {
    unsigned MainLoopStep = UF * VF.getKnownMinValue();
    unsigned EpilogueLoopStep =
        EPI.EpilogueUF * EPI.EpilogueVF.getKnownMinValue();
    // We assume the remaining `Count` is equally distributed in
    // [0, MainLoopStep)
    // So the probability for `Count < EpilogueLoopStep` should be
    // min(MainLoopStep, EpilogueLoopStep) / MainLoopStep
    unsigned EstimatedSkipCount = std::min(MainLoopStep, EpilogueLoopStep);
    const uint32_t Weights[] = {EstimatedSkipCount,
                                MainLoopStep - EstimatedSkipCount};
    setBranchWeights(BI, Weights, /*IsExpected=*/false);
  }
  ReplaceInstWithInst(Insert->getTerminator(), &BI);
  LoopBypassBlocks.push_back(Insert);
  return Insert;
}

void EpilogueVectorizerEpilogueLoop::printDebugTracesAtStart() {
  LLVM_DEBUG({
    dbgs() << "Create Skeleton for epilogue vectorized loop (second pass)\n"
           << "Epilogue Loop VF:" << EPI.EpilogueVF
           << ", Epilogue Loop UF:" << EPI.EpilogueUF << "\n";
  });
}

void EpilogueVectorizerEpilogueLoop::printDebugTracesAtEnd() {
  DEBUG_WITH_TYPE(VerboseDebug, {
    dbgs() << "final fn:\n" << *OrigLoop->getHeader()->getParent() << "\n";
  });
}

bool LoopVectorizationPlanner::getDecisionAndClampRange(
    const std::function<bool(ElementCount)> &Predicate, VFRange &Range) {
  assert(!Range.isEmpty() && "Trying to test an empty VF range.");
  bool PredicateAtRangeStart = Predicate(Range.Start);

  for (ElementCount TmpVF : VFRange(Range.Start * 2, Range.End))
    if (Predicate(TmpVF) != PredicateAtRangeStart) {
      Range.End = TmpVF;
      break;
    }

  return PredicateAtRangeStart;
}

/// Build VPlans for the full range of feasible VF's = {\p MinVF, 2 * \p MinVF,
/// 4 * \p MinVF, ..., \p MaxVF} by repeatedly building a VPlan for a sub-range
/// of VF's starting at a given VF and extending it as much as possible. Each
/// vectorization decision can potentially shorten this sub-range during
/// buildVPlan().
void LoopVectorizationPlanner::buildVPlans(ElementCount MinVF,
                                           ElementCount MaxVF) {
  auto MaxVFTimes2 = MaxVF * 2;
  for (ElementCount VF = MinVF; ElementCount::isKnownLT(VF, MaxVFTimes2);) {
    VFRange SubRange = {VF, MaxVFTimes2};
    VPlans.push_back(buildVPlan(SubRange));
    VF = SubRange.End;
  }
}

iterator_range<mapped_iterator<Use *, std::function<VPValue *(Value *)>>>
VPRecipeBuilder::mapToVPValues(User::op_range Operands) {
  std::function<VPValue *(Value *)> Fn = [this](Value *Op) {
    if (auto *I = dyn_cast<Instruction>(Op)) {
      if (auto *R = Ingredient2Recipe.lookup(I))
        return R->getVPSingleValue();
    }
    return Plan.getOrAddLiveIn(Op);
  };
  return map_range(Operands, Fn);
}

VPValue *VPRecipeBuilder::createEdgeMask(BasicBlock *Src, BasicBlock *Dst) {
  assert(is_contained(predecessors(Dst), Src) && "Invalid edge");

  // Look for cached value.
  std::pair<BasicBlock *, BasicBlock *> Edge(Src, Dst);
  EdgeMaskCacheTy::iterator ECEntryIt = EdgeMaskCache.find(Edge);
  if (ECEntryIt != EdgeMaskCache.end())
    return ECEntryIt->second;

  VPValue *SrcMask = getBlockInMask(Src);

  // The terminator has to be a branch inst!
  BranchInst *BI = dyn_cast<BranchInst>(Src->getTerminator());
  assert(BI && "Unexpected terminator found");

  if (!BI->isConditional() || BI->getSuccessor(0) == BI->getSuccessor(1))
    return EdgeMaskCache[Edge] = SrcMask;

  // If source is an exiting block, we know the exit edge is dynamically dead
  // in the vector loop, and thus we don't need to restrict the mask.  Avoid
  // adding uses of an otherwise potentially dead instruction.
  if (OrigLoop->isLoopExiting(Src))
    return EdgeMaskCache[Edge] = SrcMask;

  VPValue *EdgeMask = getVPValueOrAddLiveIn(BI->getCondition(), Plan);
  assert(EdgeMask && "No Edge Mask found for condition");

  if (BI->getSuccessor(0) != Dst)
    EdgeMask = Builder.createNot(EdgeMask, BI->getDebugLoc());

  if (SrcMask) { // Otherwise block in-mask is all-one, no need to AND.
    // The bitwise 'And' of SrcMask and EdgeMask introduces new UB if SrcMask
    // is false and EdgeMask is poison. Avoid that by using 'LogicalAnd'
    // instead which generates 'select i1 SrcMask, i1 EdgeMask, i1 false'.
    EdgeMask = Builder.createLogicalAnd(SrcMask, EdgeMask, BI->getDebugLoc());
  }

  return EdgeMaskCache[Edge] = EdgeMask;
}

VPValue *VPRecipeBuilder::getEdgeMask(BasicBlock *Src, BasicBlock *Dst) const {
  assert(is_contained(predecessors(Dst), Src) && "Invalid edge");

  // Look for cached value.
  std::pair<BasicBlock *, BasicBlock *> Edge(Src, Dst);
  EdgeMaskCacheTy::const_iterator ECEntryIt = EdgeMaskCache.find(Edge);
  assert(ECEntryIt != EdgeMaskCache.end() &&
         "looking up mask for edge which has not been created");
  return ECEntryIt->second;
}

void VPRecipeBuilder::createHeaderMask() {
  BasicBlock *Header = OrigLoop->getHeader();

  // When not folding the tail, use nullptr to model all-true mask.
  if (!CM.foldTailByMasking()) {
    BlockMaskCache[Header] = nullptr;
    return;
  }

  // Introduce the early-exit compare IV <= BTC to form header block mask.
  // This is used instead of IV < TC because TC may wrap, unlike BTC. Start by
  // constructing the desired canonical IV in the header block as its first
  // non-phi instructions.

  VPBasicBlock *HeaderVPBB = Plan.getVectorLoopRegion()->getEntryBasicBlock();
  auto NewInsertionPoint = HeaderVPBB->getFirstNonPhi();
  auto *IV = new VPWidenCanonicalIVRecipe(Plan.getCanonicalIV());
  HeaderVPBB->insert(IV, NewInsertionPoint);

  VPBuilder::InsertPointGuard Guard(Builder);
  Builder.setInsertPoint(HeaderVPBB, NewInsertionPoint);
  VPValue *BlockMask = nullptr;
  VPValue *BTC = Plan.getOrCreateBackedgeTakenCount();
  BlockMask = Builder.createICmp(CmpInst::ICMP_ULE, IV, BTC);
  BlockMaskCache[Header] = BlockMask;
}

VPValue *VPRecipeBuilder::getBlockInMask(BasicBlock *BB) const {
  // Return the cached value.
  BlockMaskCacheTy::const_iterator BCEntryIt = BlockMaskCache.find(BB);
  assert(BCEntryIt != BlockMaskCache.end() &&
         "Trying to access mask for block without one.");
  return BCEntryIt->second;
}

void VPRecipeBuilder::createBlockInMask(BasicBlock *BB) {
  assert(OrigLoop->contains(BB) && "Block is not a part of a loop");
  assert(BlockMaskCache.count(BB) == 0 && "Mask for block already computed");
  assert(OrigLoop->getHeader() != BB &&
         "Loop header must have cached block mask");

  // All-one mask is modelled as no-mask following the convention for masked
  // load/store/gather/scatter. Initialize BlockMask to no-mask.
  VPValue *BlockMask = nullptr;
  // This is the block mask. We OR all incoming edges.
  for (auto *Predecessor : predecessors(BB)) {
    VPValue *EdgeMask = createEdgeMask(Predecessor, BB);
    if (!EdgeMask) { // Mask of predecessor is all-one so mask of block is too.
      BlockMaskCache[BB] = EdgeMask;
      return;
    }

    if (!BlockMask) { // BlockMask has its initialized nullptr value.
      BlockMask = EdgeMask;
      continue;
    }

    BlockMask = Builder.createOr(BlockMask, EdgeMask, {});
  }

  BlockMaskCache[BB] = BlockMask;
}

VPWidenMemoryRecipe *
VPRecipeBuilder::tryToWidenMemory(Instruction *I, ArrayRef<VPValue *> Operands,
                                  VFRange &Range) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Must be called with either a load or store");

  auto willWiden = [&](ElementCount VF) -> bool {
    LoopVectorizationCostModel::InstWidening Decision =
        CM.getWideningDecision(I, VF);
    assert(Decision != LoopVectorizationCostModel::CM_Unknown &&
           "CM decision should be taken at this point.");
    if (Decision == LoopVectorizationCostModel::CM_Interleave)
      return true;
    if (CM.isScalarAfterVectorization(I, VF) ||
        CM.isProfitableToScalarize(I, VF))
      return false;
    return Decision != LoopVectorizationCostModel::CM_Scalarize;
  };

  if (!LoopVectorizationPlanner::getDecisionAndClampRange(willWiden, Range))
    return nullptr;

  VPValue *Mask = nullptr;
  if (Legal->isMaskRequired(I))
    Mask = getBlockInMask(I->getParent());

  // Determine if the pointer operand of the access is either consecutive or
  // reverse consecutive.
  LoopVectorizationCostModel::InstWidening Decision =
      CM.getWideningDecision(I, Range.Start);
  bool Reverse = Decision == LoopVectorizationCostModel::CM_Widen_Reverse;
  bool Consecutive =
      Reverse || Decision == LoopVectorizationCostModel::CM_Widen;

  VPValue *Ptr = isa<LoadInst>(I) ? Operands[0] : Operands[1];
  if (Consecutive) {
    auto *GEP = dyn_cast<GetElementPtrInst>(
        Ptr->getUnderlyingValue()->stripPointerCasts());
    auto *VectorPtr = new VPVectorPointerRecipe(
        Ptr, getLoadStoreType(I), Reverse, GEP ? GEP->isInBounds() : false,
        I->getDebugLoc());
    Builder.getInsertBlock()->appendRecipe(VectorPtr);
    Ptr = VectorPtr;
  }
  if (LoadInst *Load = dyn_cast<LoadInst>(I))
    return new VPWidenLoadRecipe(*Load, Ptr, Mask, Consecutive, Reverse,
                                 I->getDebugLoc());

  StoreInst *Store = cast<StoreInst>(I);
  return new VPWidenStoreRecipe(*Store, Ptr, Operands[0], Mask, Consecutive,
                                Reverse, I->getDebugLoc());
}

/// Creates a VPWidenIntOrFpInductionRecpipe for \p Phi. If needed, it will also
/// insert a recipe to expand the step for the induction recipe.
static VPWidenIntOrFpInductionRecipe *
createWidenInductionRecipes(PHINode *Phi, Instruction *PhiOrTrunc,
                            VPValue *Start, const InductionDescriptor &IndDesc,
                            VPlan &Plan, ScalarEvolution &SE, Loop &OrigLoop) {
  assert(IndDesc.getStartValue() ==
         Phi->getIncomingValueForBlock(OrigLoop.getLoopPreheader()));
  assert(SE.isLoopInvariant(IndDesc.getStep(), &OrigLoop) &&
         "step must be loop invariant");

  VPValue *Step =
      vputils::getOrCreateVPValueForSCEVExpr(Plan, IndDesc.getStep(), SE);
  if (auto *TruncI = dyn_cast<TruncInst>(PhiOrTrunc)) {
    return new VPWidenIntOrFpInductionRecipe(Phi, Start, Step, IndDesc, TruncI);
  }
  assert(isa<PHINode>(PhiOrTrunc) && "must be a phi node here");
  return new VPWidenIntOrFpInductionRecipe(Phi, Start, Step, IndDesc);
}

VPHeaderPHIRecipe *VPRecipeBuilder::tryToOptimizeInductionPHI(
    PHINode *Phi, ArrayRef<VPValue *> Operands, VFRange &Range) {

  // Check if this is an integer or fp induction. If so, build the recipe that
  // produces its scalar and vector values.
  if (auto *II = Legal->getIntOrFpInductionDescriptor(Phi))
    return createWidenInductionRecipes(Phi, Phi, Operands[0], *II, Plan,
                                       *PSE.getSE(), *OrigLoop);

  // Check if this is pointer induction. If so, build the recipe for it.
  if (auto *II = Legal->getPointerInductionDescriptor(Phi)) {
    VPValue *Step = vputils::getOrCreateVPValueForSCEVExpr(Plan, II->getStep(),
                                                           *PSE.getSE());
    return new VPWidenPointerInductionRecipe(
        Phi, Operands[0], Step, *II,
        LoopVectorizationPlanner::getDecisionAndClampRange(
            [&](ElementCount VF) {
              return CM.isScalarAfterVectorization(Phi, VF);
            },
            Range));
  }
  return nullptr;
}

VPWidenIntOrFpInductionRecipe *VPRecipeBuilder::tryToOptimizeInductionTruncate(
    TruncInst *I, ArrayRef<VPValue *> Operands, VFRange &Range) {
  // Optimize the special case where the source is a constant integer
  // induction variable. Notice that we can only optimize the 'trunc' case
  // because (a) FP conversions lose precision, (b) sext/zext may wrap, and
  // (c) other casts depend on pointer size.

  // Determine whether \p K is a truncation based on an induction variable that
  // can be optimized.
  auto isOptimizableIVTruncate =
      [&](Instruction *K) -> std::function<bool(ElementCount)> {
    return [=](ElementCount VF) -> bool {
      return CM.isOptimizableIVTruncate(K, VF);
    };
  };

  if (LoopVectorizationPlanner::getDecisionAndClampRange(
          isOptimizableIVTruncate(I), Range)) {

    auto *Phi = cast<PHINode>(I->getOperand(0));
    const InductionDescriptor &II = *Legal->getIntOrFpInductionDescriptor(Phi);
    VPValue *Start = Plan.getOrAddLiveIn(II.getStartValue());
    return createWidenInductionRecipes(Phi, I, Start, II, Plan, *PSE.getSE(),
                                       *OrigLoop);
  }
  return nullptr;
}

VPBlendRecipe *VPRecipeBuilder::tryToBlend(PHINode *Phi,
                                           ArrayRef<VPValue *> Operands) {
  unsigned NumIncoming = Phi->getNumIncomingValues();

  // We know that all PHIs in non-header blocks are converted into selects, so
  // we don't have to worry about the insertion order and we can just use the
  // builder. At this point we generate the predication tree. There may be
  // duplications since this is a simple recursive scan, but future
  // optimizations will clean it up.
  // TODO: At the moment the first mask is always skipped, but it would be
  // better to skip the most expensive mask.
  SmallVector<VPValue *, 2> OperandsWithMask;

  for (unsigned In = 0; In < NumIncoming; In++) {
    OperandsWithMask.push_back(Operands[In]);
    VPValue *EdgeMask =
        getEdgeMask(Phi->getIncomingBlock(In), Phi->getParent());
    if (!EdgeMask) {
      assert(In == 0 && "Both null and non-null edge masks found");
      assert(all_equal(Operands) &&
             "Distinct incoming values with one having a full mask");
      break;
    }
    if (In == 0)
      continue;
    OperandsWithMask.push_back(EdgeMask);
  }
  return new VPBlendRecipe(Phi, OperandsWithMask);
}

VPWidenCallRecipe *VPRecipeBuilder::tryToWidenCall(CallInst *CI,
                                                   ArrayRef<VPValue *> Operands,
                                                   VFRange &Range) {
  bool IsPredicated = LoopVectorizationPlanner::getDecisionAndClampRange(
      [this, CI](ElementCount VF) {
        return CM.isScalarWithPredication(CI, VF);
      },
      Range);

  if (IsPredicated)
    return nullptr;

  Intrinsic::ID ID = getVectorIntrinsicIDForCall(CI, TLI);
  if (ID && (ID == Intrinsic::assume || ID == Intrinsic::lifetime_end ||
             ID == Intrinsic::lifetime_start || ID == Intrinsic::sideeffect ||
             ID == Intrinsic::pseudoprobe ||
             ID == Intrinsic::experimental_noalias_scope_decl))
    return nullptr;

  SmallVector<VPValue *, 4> Ops(Operands.take_front(CI->arg_size()));
  Ops.push_back(Operands.back());

  // Is it beneficial to perform intrinsic call compared to lib call?
  bool ShouldUseVectorIntrinsic =
      ID && LoopVectorizationPlanner::getDecisionAndClampRange(
                [&](ElementCount VF) -> bool {
                  return CM.getCallWideningDecision(CI, VF).Kind ==
                         LoopVectorizationCostModel::CM_IntrinsicCall;
                },
                Range);
  if (ShouldUseVectorIntrinsic)
    return new VPWidenCallRecipe(CI, make_range(Ops.begin(), Ops.end()), ID,
                                 CI->getDebugLoc());

  Function *Variant = nullptr;
  std::optional<unsigned> MaskPos;
  // Is better to call a vectorized version of the function than to to scalarize
  // the call?
  auto ShouldUseVectorCall = LoopVectorizationPlanner::getDecisionAndClampRange(
      [&](ElementCount VF) -> bool {
        // The following case may be scalarized depending on the VF.
        // The flag shows whether we can use a usual Call for vectorized
        // version of the instruction.

        // If we've found a variant at a previous VF, then stop looking. A
        // vectorized variant of a function expects input in a certain shape
        // -- basically the number of input registers, the number of lanes
        // per register, and whether there's a mask required.
        // We store a pointer to the variant in the VPWidenCallRecipe, so
        // once we have an appropriate variant it's only valid for that VF.
        // This will force a different vplan to be generated for each VF that
        // finds a valid variant.
        if (Variant)
          return false;
        LoopVectorizationCostModel::CallWideningDecision Decision =
            CM.getCallWideningDecision(CI, VF);
        if (Decision.Kind == LoopVectorizationCostModel::CM_VectorCall) {
          Variant = Decision.Variant;
          MaskPos = Decision.MaskPos;
          return true;
        }

        return false;
      },
      Range);
  if (ShouldUseVectorCall) {
    if (MaskPos.has_value()) {
      // We have 2 cases that would require a mask:
      //   1) The block needs to be predicated, either due to a conditional
      //      in the scalar loop or use of an active lane mask with
      //      tail-folding, and we use the appropriate mask for the block.
      //   2) No mask is required for the block, but the only available
      //      vector variant at this VF requires a mask, so we synthesize an
      //      all-true mask.
      VPValue *Mask = nullptr;
      if (Legal->isMaskRequired(CI))
        Mask = getBlockInMask(CI->getParent());
      else
        Mask = Plan.getOrAddLiveIn(ConstantInt::getTrue(
            IntegerType::getInt1Ty(Variant->getFunctionType()->getContext())));

      Ops.insert(Ops.begin() + *MaskPos, Mask);
    }

    return new VPWidenCallRecipe(CI, make_range(Ops.begin(), Ops.end()),
                                 Intrinsic::not_intrinsic, CI->getDebugLoc(),
                                 Variant);
  }

  return nullptr;
}

bool VPRecipeBuilder::shouldWiden(Instruction *I, VFRange &Range) const {
  assert(!isa<BranchInst>(I) && !isa<PHINode>(I) && !isa<LoadInst>(I) &&
         !isa<StoreInst>(I) && "Instruction should have been handled earlier");
  // Instruction should be widened, unless it is scalar after vectorization,
  // scalarization is profitable or it is predicated.
  auto WillScalarize = [this, I](ElementCount VF) -> bool {
    return CM.isScalarAfterVectorization(I, VF) ||
           CM.isProfitableToScalarize(I, VF) ||
           CM.isScalarWithPredication(I, VF);
  };
  return !LoopVectorizationPlanner::getDecisionAndClampRange(WillScalarize,
                                                             Range);
}

VPWidenRecipe *VPRecipeBuilder::tryToWiden(Instruction *I,
                                           ArrayRef<VPValue *> Operands,
                                           VPBasicBlock *VPBB) {
  switch (I->getOpcode()) {
  default:
    return nullptr;
  case Instruction::SDiv:
  case Instruction::UDiv:
  case Instruction::SRem:
  case Instruction::URem: {
    // If not provably safe, use a select to form a safe divisor before widening the
    // div/rem operation itself.  Otherwise fall through to general handling below.
    if (CM.isPredicatedInst(I)) {
      SmallVector<VPValue *> Ops(Operands.begin(), Operands.end());
      VPValue *Mask = getBlockInMask(I->getParent());
      VPValue *One =
          Plan.getOrAddLiveIn(ConstantInt::get(I->getType(), 1u, false));
      auto *SafeRHS = Builder.createSelect(Mask, Ops[1], One, I->getDebugLoc());
      Ops[1] = SafeRHS;
      return new VPWidenRecipe(*I, make_range(Ops.begin(), Ops.end()));
    }
    [[fallthrough]];
  }
  case Instruction::Add:
  case Instruction::And:
  case Instruction::AShr:
  case Instruction::FAdd:
  case Instruction::FCmp:
  case Instruction::FDiv:
  case Instruction::FMul:
  case Instruction::FNeg:
  case Instruction::FRem:
  case Instruction::FSub:
  case Instruction::ICmp:
  case Instruction::LShr:
  case Instruction::Mul:
  case Instruction::Or:
  case Instruction::Select:
  case Instruction::Shl:
  case Instruction::Sub:
  case Instruction::Xor:
  case Instruction::Freeze:
    return new VPWidenRecipe(*I, make_range(Operands.begin(), Operands.end()));
  };
}

void VPRecipeBuilder::fixHeaderPhis() {
  BasicBlock *OrigLatch = OrigLoop->getLoopLatch();
  for (VPHeaderPHIRecipe *R : PhisToFix) {
    auto *PN = cast<PHINode>(R->getUnderlyingValue());
    VPRecipeBase *IncR =
        getRecipe(cast<Instruction>(PN->getIncomingValueForBlock(OrigLatch)));
    R->addOperand(IncR->getVPSingleValue());
  }
}

VPReplicateRecipe *VPRecipeBuilder::handleReplication(Instruction *I,
                                                      VFRange &Range) {
  bool IsUniform = LoopVectorizationPlanner::getDecisionAndClampRange(
      [&](ElementCount VF) { return CM.isUniformAfterVectorization(I, VF); },
      Range);

  bool IsPredicated = CM.isPredicatedInst(I);

  // Even if the instruction is not marked as uniform, there are certain
  // intrinsic calls that can be effectively treated as such, so we check for
  // them here. Conservatively, we only do this for scalable vectors, since
  // for fixed-width VFs we can always fall back on full scalarization.
  if (!IsUniform && Range.Start.isScalable() && isa<IntrinsicInst>(I)) {
    switch (cast<IntrinsicInst>(I)->getIntrinsicID()) {
    case Intrinsic::assume:
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
      // For scalable vectors if one of the operands is variant then we still
      // want to mark as uniform, which will generate one instruction for just
      // the first lane of the vector. We can't scalarize the call in the same
      // way as for fixed-width vectors because we don't know how many lanes
      // there are.
      //
      // The reasons for doing it this way for scalable vectors are:
      //   1. For the assume intrinsic generating the instruction for the first
      //      lane is still be better than not generating any at all. For
      //      example, the input may be a splat across all lanes.
      //   2. For the lifetime start/end intrinsics the pointer operand only
      //      does anything useful when the input comes from a stack object,
      //      which suggests it should always be uniform. For non-stack objects
      //      the effect is to poison the object, which still allows us to
      //      remove the call.
      IsUniform = true;
      break;
    default:
      break;
    }
  }
  VPValue *BlockInMask = nullptr;
  if (!IsPredicated) {
    // Finalize the recipe for Instr, first if it is not predicated.
    LLVM_DEBUG(dbgs() << "LV: Scalarizing:" << *I << "\n");
  } else {
    LLVM_DEBUG(dbgs() << "LV: Scalarizing and predicating:" << *I << "\n");
    // Instructions marked for predication are replicated and a mask operand is
    // added initially. Masked replicate recipes will later be placed under an
    // if-then construct to prevent side-effects. Generate recipes to compute
    // the block mask for this region.
    BlockInMask = getBlockInMask(I->getParent());
  }

  // Note that there is some custom logic to mark some intrinsics as uniform
  // manually above for scalable vectors, which this assert needs to account for
  // as well.
  assert((Range.Start.isScalar() || !IsUniform || !IsPredicated ||
          (Range.Start.isScalable() && isa<IntrinsicInst>(I))) &&
         "Should not predicate a uniform recipe");
  auto *Recipe = new VPReplicateRecipe(I, mapToVPValues(I->operands()),
                                       IsUniform, BlockInMask);
  return Recipe;
}

VPRecipeBase *
VPRecipeBuilder::tryToCreateWidenRecipe(Instruction *Instr,
                                        ArrayRef<VPValue *> Operands,
                                        VFRange &Range, VPBasicBlock *VPBB) {
  // First, check for specific widening recipes that deal with inductions, Phi
  // nodes, calls and memory operations.
  VPRecipeBase *Recipe;
  if (auto Phi = dyn_cast<PHINode>(Instr)) {
    if (Phi->getParent() != OrigLoop->getHeader())
      return tryToBlend(Phi, Operands);

    if ((Recipe = tryToOptimizeInductionPHI(Phi, Operands, Range)))
      return Recipe;

    VPHeaderPHIRecipe *PhiRecipe = nullptr;
    assert((Legal->isReductionVariable(Phi) ||
            Legal->isFixedOrderRecurrence(Phi)) &&
           "can only widen reductions and fixed-order recurrences here");
    VPValue *StartV = Operands[0];
    if (Legal->isReductionVariable(Phi)) {
      const RecurrenceDescriptor &RdxDesc =
          Legal->getReductionVars().find(Phi)->second;
      assert(RdxDesc.getRecurrenceStartValue() ==
             Phi->getIncomingValueForBlock(OrigLoop->getLoopPreheader()));
      PhiRecipe = new VPReductionPHIRecipe(Phi, RdxDesc, *StartV,
                                           CM.isInLoopReduction(Phi),
                                           CM.useOrderedReductions(RdxDesc));
    } else {
      // TODO: Currently fixed-order recurrences are modeled as chains of
      // first-order recurrences. If there are no users of the intermediate
      // recurrences in the chain, the fixed order recurrence should be modeled
      // directly, enabling more efficient codegen.
      PhiRecipe = new VPFirstOrderRecurrencePHIRecipe(Phi, *StartV);
    }

    PhisToFix.push_back(PhiRecipe);
    return PhiRecipe;
  }

  if (isa<TruncInst>(Instr) && (Recipe = tryToOptimizeInductionTruncate(
                                    cast<TruncInst>(Instr), Operands, Range)))
    return Recipe;

  // All widen recipes below deal only with VF > 1.
  if (LoopVectorizationPlanner::getDecisionAndClampRange(
          [&](ElementCount VF) { return VF.isScalar(); }, Range))
    return nullptr;

  if (auto *CI = dyn_cast<CallInst>(Instr))
    return tryToWidenCall(CI, Operands, Range);

  if (isa<LoadInst>(Instr) || isa<StoreInst>(Instr))
    return tryToWidenMemory(Instr, Operands, Range);

  if (!shouldWiden(Instr, Range))
    return nullptr;

  if (auto GEP = dyn_cast<GetElementPtrInst>(Instr))
    return new VPWidenGEPRecipe(GEP,
                                make_range(Operands.begin(), Operands.end()));

  if (auto *SI = dyn_cast<SelectInst>(Instr)) {
    return new VPWidenSelectRecipe(
        *SI, make_range(Operands.begin(), Operands.end()));
  }

  if (auto *CI = dyn_cast<CastInst>(Instr)) {
    return new VPWidenCastRecipe(CI->getOpcode(), Operands[0], CI->getType(),
                                 *CI);
  }

  return tryToWiden(Instr, Operands, VPBB);
}

void LoopVectorizationPlanner::buildVPlansWithVPRecipes(ElementCount MinVF,
                                                        ElementCount MaxVF) {
  assert(OrigLoop->isInnermost() && "Inner loop expected.");

  auto MaxVFTimes2 = MaxVF * 2;
  for (ElementCount VF = MinVF; ElementCount::isKnownLT(VF, MaxVFTimes2);) {
    VFRange SubRange = {VF, MaxVFTimes2};
    if (auto Plan = tryToBuildVPlanWithVPRecipes(SubRange)) {
      // Now optimize the initial VPlan.
      if (!Plan->hasVF(ElementCount::getFixed(1)))
        VPlanTransforms::truncateToMinimalBitwidths(
            *Plan, CM.getMinimalBitwidths(), PSE.getSE()->getContext());
      VPlanTransforms::optimize(*Plan, *PSE.getSE());
      // TODO: try to put it close to addActiveLaneMask().
      // Discard the plan if it is not EVL-compatible
      if (CM.foldTailWithEVL() &&
          !VPlanTransforms::tryAddExplicitVectorLength(*Plan))
        break;
      assert(verifyVPlanIsValid(*Plan) && "VPlan is invalid");
      VPlans.push_back(std::move(Plan));
    }
    VF = SubRange.End;
  }
}

// Add the necessary canonical IV and branch recipes required to control the
// loop.
static void addCanonicalIVRecipes(VPlan &Plan, Type *IdxTy, bool HasNUW,
                                  DebugLoc DL) {
  Value *StartIdx = ConstantInt::get(IdxTy, 0);
  auto *StartV = Plan.getOrAddLiveIn(StartIdx);

  // Add a VPCanonicalIVPHIRecipe starting at 0 to the header.
  auto *CanonicalIVPHI = new VPCanonicalIVPHIRecipe(StartV, DL);
  VPRegionBlock *TopRegion = Plan.getVectorLoopRegion();
  VPBasicBlock *Header = TopRegion->getEntryBasicBlock();
  Header->insert(CanonicalIVPHI, Header->begin());

  VPBuilder Builder(TopRegion->getExitingBasicBlock());
  // Add a VPInstruction to increment the scalar canonical IV by VF * UF.
  auto *CanonicalIVIncrement = Builder.createOverflowingOp(
      Instruction::Add, {CanonicalIVPHI, &Plan.getVFxUF()}, {HasNUW, false}, DL,
      "index.next");
  CanonicalIVPHI->addOperand(CanonicalIVIncrement);

  // Add the BranchOnCount VPInstruction to the latch.
  Builder.createNaryOp(VPInstruction::BranchOnCount,
                       {CanonicalIVIncrement, &Plan.getVectorTripCount()}, DL);
}

// Add exit values to \p Plan. VPLiveOuts are added for each LCSSA phi in the
// original exit block.
static void addUsersInExitBlock(VPBasicBlock *HeaderVPBB, Loop *OrigLoop,
                                VPRecipeBuilder &Builder, VPlan &Plan) {
  BasicBlock *ExitBB = OrigLoop->getUniqueExitBlock();
  BasicBlock *ExitingBB = OrigLoop->getExitingBlock();
  // Only handle single-exit loops with unique exit blocks for now.
  if (!ExitBB || !ExitBB->getSinglePredecessor() || !ExitingBB)
    return;

  // Introduce VPUsers modeling the exit values.
  for (PHINode &ExitPhi : ExitBB->phis()) {
    Value *IncomingValue =
        ExitPhi.getIncomingValueForBlock(ExitingBB);
    VPValue *V = Builder.getVPValueOrAddLiveIn(IncomingValue, Plan);
    // Exit values for inductions are computed and updated outside of VPlan and
    // independent of induction recipes.
    // TODO: Compute induction exit values in VPlan, use VPLiveOuts to update
    // live-outs.
    if ((isa<VPWidenIntOrFpInductionRecipe>(V) &&
         !cast<VPWidenIntOrFpInductionRecipe>(V)->getTruncInst()) ||
        isa<VPWidenPointerInductionRecipe>(V))
      continue;
    Plan.addLiveOut(&ExitPhi, V);
  }
}

/// Feed a resume value for every FOR from the vector loop to the scalar loop,
/// if middle block branches to scalar preheader, by introducing ExtractFromEnd
/// and ResumePhi recipes in each, respectively, and a VPLiveOut which uses the
/// latter and corresponds to the scalar header.
static void addLiveOutsForFirstOrderRecurrences(VPlan &Plan) {
  VPRegionBlock *VectorRegion = Plan.getVectorLoopRegion();

  // Start by finding out if middle block branches to scalar preheader, which is
  // not a VPIRBasicBlock, unlike Exit block - the other possible successor of
  // middle block.
  // TODO: Should be replaced by
  // Plan->getScalarLoopRegion()->getSinglePredecessor() in the future once the
  // scalar region is modeled as well.
  VPBasicBlock *ScalarPHVPBB = nullptr;
  auto *MiddleVPBB = cast<VPBasicBlock>(VectorRegion->getSingleSuccessor());
  for (VPBlockBase *Succ : MiddleVPBB->getSuccessors()) {
    if (isa<VPIRBasicBlock>(Succ))
      continue;
    assert(!ScalarPHVPBB && "Two candidates for ScalarPHVPBB?");
    ScalarPHVPBB = cast<VPBasicBlock>(Succ);
  }
  if (!ScalarPHVPBB)
    return;

  VPBuilder ScalarPHBuilder(ScalarPHVPBB);
  VPBuilder MiddleBuilder(MiddleVPBB);
  // Reset insert point so new recipes are inserted before terminator and
  // condition, if there is either the former or both.
  if (auto *Terminator = MiddleVPBB->getTerminator()) {
    auto *Condition = dyn_cast<VPInstruction>(Terminator->getOperand(0));
    assert((!Condition || Condition->getParent() == MiddleVPBB) &&
           "Condition expected in MiddleVPBB");
    MiddleBuilder.setInsertPoint(Condition ? Condition : Terminator);
  }
  VPValue *OneVPV = Plan.getOrAddLiveIn(
      ConstantInt::get(Plan.getCanonicalIV()->getScalarType(), 1));

  for (auto &HeaderPhi : VectorRegion->getEntryBasicBlock()->phis()) {
    auto *FOR = dyn_cast<VPFirstOrderRecurrencePHIRecipe>(&HeaderPhi);
    if (!FOR)
      continue;

    // Extract the resume value and create a new VPLiveOut for it.
    auto *Resume = MiddleBuilder.createNaryOp(VPInstruction::ExtractFromEnd,
                                              {FOR->getBackedgeValue(), OneVPV},
                                              {}, "vector.recur.extract");
    auto *ResumePhiRecipe = ScalarPHBuilder.createNaryOp(
        VPInstruction::ResumePhi, {Resume, FOR->getStartValue()}, {},
        "scalar.recur.init");
    Plan.addLiveOut(cast<PHINode>(FOR->getUnderlyingInstr()), ResumePhiRecipe);
  }
}

VPlanPtr
LoopVectorizationPlanner::tryToBuildVPlanWithVPRecipes(VFRange &Range) {

  SmallPtrSet<const InterleaveGroup<Instruction> *, 1> InterleaveGroups;

  // ---------------------------------------------------------------------------
  // Build initial VPlan: Scan the body of the loop in a topological order to
  // visit each basic block after having visited its predecessor basic blocks.
  // ---------------------------------------------------------------------------

  // Create initial VPlan skeleton, having a basic block for the pre-header
  // which contains SCEV expansions that need to happen before the CFG is
  // modified; a basic block for the vector pre-header, followed by a region for
  // the vector loop, followed by the middle basic block. The skeleton vector
  // loop region contains a header and latch basic blocks.

  bool RequiresScalarEpilogueCheck =
      LoopVectorizationPlanner::getDecisionAndClampRange(
          [this](ElementCount VF) {
            return !CM.requiresScalarEpilogue(VF.isVector());
          },
          Range);
  VPlanPtr Plan = VPlan::createInitialVPlan(
      createTripCountSCEV(Legal->getWidestInductionType(), PSE, OrigLoop),
      *PSE.getSE(), RequiresScalarEpilogueCheck, CM.foldTailByMasking(),
      OrigLoop);

  // Don't use getDecisionAndClampRange here, because we don't know the UF
  // so this function is better to be conservative, rather than to split
  // it up into different VPlans.
  // TODO: Consider using getDecisionAndClampRange here to split up VPlans.
  bool IVUpdateMayOverflow = false;
  for (ElementCount VF : Range)
    IVUpdateMayOverflow |= !isIndvarOverflowCheckKnownFalse(&CM, VF);

  DebugLoc DL = getDebugLocFromInstOrOperands(Legal->getPrimaryInduction());
  TailFoldingStyle Style = CM.getTailFoldingStyle(IVUpdateMayOverflow);
  // When not folding the tail, we know that the induction increment will not
  // overflow.
  bool HasNUW = Style == TailFoldingStyle::None;
  addCanonicalIVRecipes(*Plan, Legal->getWidestInductionType(), HasNUW, DL);

  VPRecipeBuilder RecipeBuilder(*Plan, OrigLoop, TLI, Legal, CM, PSE, Builder);

  // ---------------------------------------------------------------------------
  // Pre-construction: record ingredients whose recipes we'll need to further
  // process after constructing the initial VPlan.
  // ---------------------------------------------------------------------------

  // For each interleave group which is relevant for this (possibly trimmed)
  // Range, add it to the set of groups to be later applied to the VPlan and add
  // placeholders for its members' Recipes which we'll be replacing with a
  // single VPInterleaveRecipe.
  for (InterleaveGroup<Instruction> *IG : IAI.getInterleaveGroups()) {
    auto applyIG = [IG, this](ElementCount VF) -> bool {
      bool Result = (VF.isVector() && // Query is illegal for VF == 1
                     CM.getWideningDecision(IG->getInsertPos(), VF) ==
                         LoopVectorizationCostModel::CM_Interleave);
      // For scalable vectors, the only interleave factor currently supported
      // is 2 since we require the (de)interleave2 intrinsics instead of
      // shufflevectors.
      assert((!Result || !VF.isScalable() || IG->getFactor() == 2) &&
             "Unsupported interleave factor for scalable vectors");
      return Result;
    };
    if (!getDecisionAndClampRange(applyIG, Range))
      continue;
    InterleaveGroups.insert(IG);
  };

  // ---------------------------------------------------------------------------
  // Construct recipes for the instructions in the loop
  // ---------------------------------------------------------------------------

  // Scan the body of the loop in a topological order to visit each basic block
  // after having visited its predecessor basic blocks.
  LoopBlocksDFS DFS(OrigLoop);
  DFS.perform(LI);

  VPBasicBlock *HeaderVPBB = Plan->getVectorLoopRegion()->getEntryBasicBlock();
  VPBasicBlock *VPBB = HeaderVPBB;
  BasicBlock *HeaderBB = OrigLoop->getHeader();
  bool NeedsMasks =
      CM.foldTailByMasking() ||
      any_of(OrigLoop->blocks(), [this, HeaderBB](BasicBlock *BB) {
        bool NeedsBlends = BB != HeaderBB && !BB->phis().empty();
        return Legal->blockNeedsPredication(BB) || NeedsBlends;
      });
  for (BasicBlock *BB : make_range(DFS.beginRPO(), DFS.endRPO())) {
    // Relevant instructions from basic block BB will be grouped into VPRecipe
    // ingredients and fill a new VPBasicBlock.
    if (VPBB != HeaderVPBB)
      VPBB->setName(BB->getName());
    Builder.setInsertPoint(VPBB);

    if (VPBB == HeaderVPBB)
      RecipeBuilder.createHeaderMask();
    else if (NeedsMasks)
      RecipeBuilder.createBlockInMask(BB);

    // Introduce each ingredient into VPlan.
    // TODO: Model and preserve debug intrinsics in VPlan.
    for (Instruction &I : drop_end(BB->instructionsWithoutDebug(false))) {
      Instruction *Instr = &I;
      SmallVector<VPValue *, 4> Operands;
      auto *Phi = dyn_cast<PHINode>(Instr);
      if (Phi && Phi->getParent() == HeaderBB) {
        Operands.push_back(Plan->getOrAddLiveIn(
            Phi->getIncomingValueForBlock(OrigLoop->getLoopPreheader())));
      } else {
        auto OpRange = RecipeBuilder.mapToVPValues(Instr->operands());
        Operands = {OpRange.begin(), OpRange.end()};
      }

      // Invariant stores inside loop will be deleted and a single store
      // with the final reduction value will be added to the exit block
      StoreInst *SI;
      if ((SI = dyn_cast<StoreInst>(&I)) &&
          Legal->isInvariantAddressOfReduction(SI->getPointerOperand()))
        continue;

      VPRecipeBase *Recipe =
          RecipeBuilder.tryToCreateWidenRecipe(Instr, Operands, Range, VPBB);
      if (!Recipe)
        Recipe = RecipeBuilder.handleReplication(Instr, Range);

      RecipeBuilder.setRecipe(Instr, Recipe);
      if (isa<VPHeaderPHIRecipe>(Recipe)) {
        // VPHeaderPHIRecipes must be kept in the phi section of HeaderVPBB. In
        // the following cases, VPHeaderPHIRecipes may be created after non-phi
        // recipes and need to be moved to the phi section of HeaderVPBB:
        // * tail-folding (non-phi recipes computing the header mask are
        // introduced earlier than regular header phi recipes, and should appear
        // after them)
        // * Optimizing truncates to VPWidenIntOrFpInductionRecipe.

        assert((HeaderVPBB->getFirstNonPhi() == VPBB->end() ||
                CM.foldTailByMasking() || isa<TruncInst>(Instr)) &&
               "unexpected recipe needs moving");
        Recipe->insertBefore(*HeaderVPBB, HeaderVPBB->getFirstNonPhi());
      } else
        VPBB->appendRecipe(Recipe);
    }

    VPBlockUtils::insertBlockAfter(new VPBasicBlock(), VPBB);
    VPBB = cast<VPBasicBlock>(VPBB->getSingleSuccessor());
  }

  // After here, VPBB should not be used.
  VPBB = nullptr;

  if (CM.requiresScalarEpilogue(Range)) {
    // No edge from the middle block to the unique exit block has been inserted
    // and there is nothing to fix from vector loop; phis should have incoming
    // from scalar loop only.
  } else
    addUsersInExitBlock(HeaderVPBB, OrigLoop, RecipeBuilder, *Plan);

  assert(isa<VPRegionBlock>(Plan->getVectorLoopRegion()) &&
         !Plan->getVectorLoopRegion()->getEntryBasicBlock()->empty() &&
         "entry block must be set to a VPRegionBlock having a non-empty entry "
         "VPBasicBlock");
  RecipeBuilder.fixHeaderPhis();

  addLiveOutsForFirstOrderRecurrences(*Plan);

  // ---------------------------------------------------------------------------
  // Transform initial VPlan: Apply previously taken decisions, in order, to
  // bring the VPlan to its final state.
  // ---------------------------------------------------------------------------

  // Adjust the recipes for any inloop reductions.
  adjustRecipesForReductions(Plan, RecipeBuilder, Range.Start);

  // Interleave memory: for each Interleave Group we marked earlier as relevant
  // for this VPlan, replace the Recipes widening its memory instructions with a
  // single VPInterleaveRecipe at its insertion point.
  for (const auto *IG : InterleaveGroups) {
    auto *Recipe =
        cast<VPWidenMemoryRecipe>(RecipeBuilder.getRecipe(IG->getInsertPos()));
    SmallVector<VPValue *, 4> StoredValues;
    for (unsigned i = 0; i < IG->getFactor(); ++i)
      if (auto *SI = dyn_cast_or_null<StoreInst>(IG->getMember(i))) {
        auto *StoreR = cast<VPWidenStoreRecipe>(RecipeBuilder.getRecipe(SI));
        StoredValues.push_back(StoreR->getStoredValue());
      }

    bool NeedsMaskForGaps =
        IG->requiresScalarEpilogue() && !CM.isScalarEpilogueAllowed();
    assert((!NeedsMaskForGaps || useMaskedInterleavedAccesses(CM.TTI)) &&
           "masked interleaved groups are not allowed.");
    auto *VPIG = new VPInterleaveRecipe(IG, Recipe->getAddr(), StoredValues,
                                        Recipe->getMask(), NeedsMaskForGaps);
    VPIG->insertBefore(Recipe);
    unsigned J = 0;
    for (unsigned i = 0; i < IG->getFactor(); ++i)
      if (Instruction *Member = IG->getMember(i)) {
        VPRecipeBase *MemberR = RecipeBuilder.getRecipe(Member);
        if (!Member->getType()->isVoidTy()) {
          VPValue *OriginalV = MemberR->getVPSingleValue();
          OriginalV->replaceAllUsesWith(VPIG->getVPValue(J));
          J++;
        }
        MemberR->eraseFromParent();
      }
  }

  for (ElementCount VF : Range)
    Plan->addVF(VF);
  Plan->setName("Initial VPlan");

  // Replace VPValues for known constant strides guaranteed by predicate scalar
  // evolution.
  for (auto [_, Stride] : Legal->getLAI()->getSymbolicStrides()) {
    auto *StrideV = cast<SCEVUnknown>(Stride)->getValue();
    auto *ScevStride = dyn_cast<SCEVConstant>(PSE.getSCEV(StrideV));
    // Only handle constant strides for now.
    if (!ScevStride)
      continue;

    auto *CI = Plan->getOrAddLiveIn(
        ConstantInt::get(Stride->getType(), ScevStride->getAPInt()));
    if (VPValue *StrideVPV = Plan->getLiveIn(StrideV))
      StrideVPV->replaceAllUsesWith(CI);

    // The versioned value may not be used in the loop directly but through a
    // sext/zext. Add new live-ins in those cases.
    for (Value *U : StrideV->users()) {
      if (!isa<SExtInst, ZExtInst>(U))
        continue;
      VPValue *StrideVPV = Plan->getLiveIn(U);
      if (!StrideVPV)
        continue;
      unsigned BW = U->getType()->getScalarSizeInBits();
      APInt C = isa<SExtInst>(U) ? ScevStride->getAPInt().sext(BW)
                                 : ScevStride->getAPInt().zext(BW);
      VPValue *CI = Plan->getOrAddLiveIn(ConstantInt::get(U->getType(), C));
      StrideVPV->replaceAllUsesWith(CI);
    }
  }

  VPlanTransforms::dropPoisonGeneratingRecipes(*Plan, [this](BasicBlock *BB) {
    return Legal->blockNeedsPredication(BB);
  });

  // Sink users of fixed-order recurrence past the recipe defining the previous
  // value and introduce FirstOrderRecurrenceSplice VPInstructions.
  if (!VPlanTransforms::adjustFixedOrderRecurrences(*Plan, Builder))
    return nullptr;

  if (useActiveLaneMask(Style)) {
    // TODO: Move checks to VPlanTransforms::addActiveLaneMask once
    // TailFoldingStyle is visible there.
    bool ForControlFlow = useActiveLaneMaskForControlFlow(Style);
    bool WithoutRuntimeCheck =
        Style == TailFoldingStyle::DataAndControlFlowWithoutRuntimeCheck;
    VPlanTransforms::addActiveLaneMask(*Plan, ForControlFlow,
                                       WithoutRuntimeCheck);
  }
  return Plan;
}

VPlanPtr LoopVectorizationPlanner::buildVPlan(VFRange &Range) {
  // Outer loop handling: They may require CFG and instruction level
  // transformations before even evaluating whether vectorization is profitable.
  // Since we cannot modify the incoming IR, we need to build VPlan upfront in
  // the vectorization pipeline.
  assert(!OrigLoop->isInnermost());
  assert(EnableVPlanNativePath && "VPlan-native path is not enabled.");

  // Create new empty VPlan
  auto Plan = VPlan::createInitialVPlan(
      createTripCountSCEV(Legal->getWidestInductionType(), PSE, OrigLoop),
      *PSE.getSE(), true, false, OrigLoop);

  // Build hierarchical CFG
  VPlanHCFGBuilder HCFGBuilder(OrigLoop, LI, *Plan);
  HCFGBuilder.buildHierarchicalCFG();

  for (ElementCount VF : Range)
    Plan->addVF(VF);

  VPlanTransforms::VPInstructionsToVPRecipes(
      Plan,
      [this](PHINode *P) { return Legal->getIntOrFpInductionDescriptor(P); },
      *PSE.getSE(), *TLI);

  // Remove the existing terminator of the exiting block of the top-most region.
  // A BranchOnCount will be added instead when adding the canonical IV recipes.
  auto *Term =
      Plan->getVectorLoopRegion()->getExitingBasicBlock()->getTerminator();
  Term->eraseFromParent();

  // Tail folding is not supported for outer loops, so the induction increment
  // is guaranteed to not wrap.
  bool HasNUW = true;
  addCanonicalIVRecipes(*Plan, Legal->getWidestInductionType(), HasNUW,
                        DebugLoc());
  assert(verifyVPlanIsValid(*Plan) && "VPlan is invalid");
  return Plan;
}

// Adjust the recipes for reductions. For in-loop reductions the chain of
// instructions leading from the loop exit instr to the phi need to be converted
// to reductions, with one operand being vector and the other being the scalar
// reduction chain. For other reductions, a select is introduced between the phi
// and live-out recipes when folding the tail.
//
// A ComputeReductionResult recipe is added to the middle block, also for
// in-loop reductions which compute their result in-loop, because generating
// the subsequent bc.merge.rdx phi is driven by ComputeReductionResult recipes.
//
// Adjust AnyOf reductions; replace the reduction phi for the selected value
// with a boolean reduction phi node to check if the condition is true in any
// iteration. The final value is selected by the final ComputeReductionResult.
void LoopVectorizationPlanner::adjustRecipesForReductions(
    VPlanPtr &Plan, VPRecipeBuilder &RecipeBuilder, ElementCount MinVF) {
  VPRegionBlock *VectorLoopRegion = Plan->getVectorLoopRegion();
  VPBasicBlock *Header = VectorLoopRegion->getEntryBasicBlock();
  // Gather all VPReductionPHIRecipe and sort them so that Intermediate stores
  // sank outside of the loop would keep the same order as they had in the
  // original loop.
  SmallVector<VPReductionPHIRecipe *> ReductionPHIList;
  for (VPRecipeBase &R : Header->phis()) {
    if (auto *ReductionPhi = dyn_cast<VPReductionPHIRecipe>(&R))
      ReductionPHIList.emplace_back(ReductionPhi);
  }
  bool HasIntermediateStore = false;
  stable_sort(ReductionPHIList,
              [this, &HasIntermediateStore](const VPReductionPHIRecipe *R1,
                                            const VPReductionPHIRecipe *R2) {
                auto *IS1 = R1->getRecurrenceDescriptor().IntermediateStore;
                auto *IS2 = R2->getRecurrenceDescriptor().IntermediateStore;
                HasIntermediateStore |= IS1 || IS2;

                // If neither of the recipes has an intermediate store, keep the
                // order the same.
                if (!IS1 && !IS2)
                  return false;

                // If only one of the recipes has an intermediate store, then
                // move it towards the beginning of the list.
                if (IS1 && !IS2)
                  return true;

                if (!IS1 && IS2)
                  return false;

                // If both recipes have an intermediate store, then the recipe
                // with the later store should be processed earlier. So it
                // should go to the beginning of the list.
                return DT->dominates(IS2, IS1);
              });

  if (HasIntermediateStore && ReductionPHIList.size() > 1)
    for (VPRecipeBase *R : ReductionPHIList)
      R->moveBefore(*Header, Header->getFirstNonPhi());

  for (VPRecipeBase &R : Header->phis()) {
    auto *PhiR = dyn_cast<VPReductionPHIRecipe>(&R);
    if (!PhiR || !PhiR->isInLoop() || (MinVF.isScalar() && !PhiR->isOrdered()))
      continue;

    const RecurrenceDescriptor &RdxDesc = PhiR->getRecurrenceDescriptor();
    RecurKind Kind = RdxDesc.getRecurrenceKind();
    assert(!RecurrenceDescriptor::isAnyOfRecurrenceKind(Kind) &&
           "AnyOf reductions are not allowed for in-loop reductions");

    // Collect the chain of "link" recipes for the reduction starting at PhiR.
    SetVector<VPSingleDefRecipe *> Worklist;
    Worklist.insert(PhiR);
    for (unsigned I = 0; I != Worklist.size(); ++I) {
      VPSingleDefRecipe *Cur = Worklist[I];
      for (VPUser *U : Cur->users()) {
        auto *UserRecipe = dyn_cast<VPSingleDefRecipe>(U);
        if (!UserRecipe) {
          assert(isa<VPLiveOut>(U) &&
                 "U must either be a VPSingleDef or VPLiveOut");
          continue;
        }
        Worklist.insert(UserRecipe);
      }
    }

    // Visit operation "Links" along the reduction chain top-down starting from
    // the phi until LoopExitValue. We keep track of the previous item
    // (PreviousLink) to tell which of the two operands of a Link will remain
    // scalar and which will be reduced. For minmax by select(cmp), Link will be
    // the select instructions. Blend recipes of in-loop reduction phi's  will
    // get folded to their non-phi operand, as the reduction recipe handles the
    // condition directly.
    VPSingleDefRecipe *PreviousLink = PhiR; // Aka Worklist[0].
    for (VPSingleDefRecipe *CurrentLink : Worklist.getArrayRef().drop_front()) {
      Instruction *CurrentLinkI = CurrentLink->getUnderlyingInstr();

      // Index of the first operand which holds a non-mask vector operand.
      unsigned IndexOfFirstOperand;
      // Recognize a call to the llvm.fmuladd intrinsic.
      bool IsFMulAdd = (Kind == RecurKind::FMulAdd);
      VPValue *VecOp;
      VPBasicBlock *LinkVPBB = CurrentLink->getParent();
      if (IsFMulAdd) {
        assert(
            RecurrenceDescriptor::isFMulAddIntrinsic(CurrentLinkI) &&
            "Expected instruction to be a call to the llvm.fmuladd intrinsic");
        assert(((MinVF.isScalar() && isa<VPReplicateRecipe>(CurrentLink)) ||
                isa<VPWidenCallRecipe>(CurrentLink)) &&
               CurrentLink->getOperand(2) == PreviousLink &&
               "expected a call where the previous link is the added operand");

        // If the instruction is a call to the llvm.fmuladd intrinsic then we
        // need to create an fmul recipe (multiplying the first two operands of
        // the fmuladd together) to use as the vector operand for the fadd
        // reduction.
        VPInstruction *FMulRecipe = new VPInstruction(
            Instruction::FMul,
            {CurrentLink->getOperand(0), CurrentLink->getOperand(1)},
            CurrentLinkI->getFastMathFlags());
        LinkVPBB->insert(FMulRecipe, CurrentLink->getIterator());
        VecOp = FMulRecipe;
      } else {
        auto *Blend = dyn_cast<VPBlendRecipe>(CurrentLink);
        if (PhiR->isInLoop() && Blend) {
          assert(Blend->getNumIncomingValues() == 2 &&
                 "Blend must have 2 incoming values");
          if (Blend->getIncomingValue(0) == PhiR)
            Blend->replaceAllUsesWith(Blend->getIncomingValue(1));
          else {
            assert(Blend->getIncomingValue(1) == PhiR &&
                   "PhiR must be an operand of the blend");
            Blend->replaceAllUsesWith(Blend->getIncomingValue(0));
          }
          continue;
        }

        if (RecurrenceDescriptor::isMinMaxRecurrenceKind(Kind)) {
          if (isa<VPWidenRecipe>(CurrentLink)) {
            assert(isa<CmpInst>(CurrentLinkI) &&
                   "need to have the compare of the select");
            continue;
          }
          assert(isa<VPWidenSelectRecipe>(CurrentLink) &&
                 "must be a select recipe");
          IndexOfFirstOperand = 1;
        } else {
          assert((MinVF.isScalar() || isa<VPWidenRecipe>(CurrentLink)) &&
                 "Expected to replace a VPWidenSC");
          IndexOfFirstOperand = 0;
        }
        // Note that for non-commutable operands (cmp-selects), the semantics of
        // the cmp-select are captured in the recurrence kind.
        unsigned VecOpId =
            CurrentLink->getOperand(IndexOfFirstOperand) == PreviousLink
                ? IndexOfFirstOperand + 1
                : IndexOfFirstOperand;
        VecOp = CurrentLink->getOperand(VecOpId);
        assert(VecOp != PreviousLink &&
               CurrentLink->getOperand(CurrentLink->getNumOperands() - 1 -
                                       (VecOpId - IndexOfFirstOperand)) ==
                   PreviousLink &&
               "PreviousLink must be the operand other than VecOp");
      }

      BasicBlock *BB = CurrentLinkI->getParent();
      VPValue *CondOp = nullptr;
      if (CM.blockNeedsPredicationForAnyReason(BB))
        CondOp = RecipeBuilder.getBlockInMask(BB);

      VPReductionRecipe *RedRecipe =
          new VPReductionRecipe(RdxDesc, CurrentLinkI, PreviousLink, VecOp,
                                CondOp, CM.useOrderedReductions(RdxDesc));
      // Append the recipe to the end of the VPBasicBlock because we need to
      // ensure that it comes after all of it's inputs, including CondOp.
      // Note that this transformation may leave over dead recipes (including
      // CurrentLink), which will be cleaned by a later VPlan transform.
      LinkVPBB->appendRecipe(RedRecipe);
      CurrentLink->replaceAllUsesWith(RedRecipe);
      PreviousLink = RedRecipe;
    }
  }
  VPBasicBlock *LatchVPBB = VectorLoopRegion->getExitingBasicBlock();
  Builder.setInsertPoint(&*LatchVPBB->begin());
  VPBasicBlock *MiddleVPBB =
      cast<VPBasicBlock>(VectorLoopRegion->getSingleSuccessor());
  VPBasicBlock::iterator IP = MiddleVPBB->getFirstNonPhi();
  for (VPRecipeBase &R :
       Plan->getVectorLoopRegion()->getEntryBasicBlock()->phis()) {
    VPReductionPHIRecipe *PhiR = dyn_cast<VPReductionPHIRecipe>(&R);
    if (!PhiR)
      continue;

    const RecurrenceDescriptor &RdxDesc = PhiR->getRecurrenceDescriptor();
    // Adjust AnyOf reductions; replace the reduction phi for the selected value
    // with a boolean reduction phi node to check if the condition is true in
    // any iteration. The final value is selected by the final
    // ComputeReductionResult.
    if (RecurrenceDescriptor::isAnyOfRecurrenceKind(
            RdxDesc.getRecurrenceKind())) {
      auto *Select = cast<VPRecipeBase>(*find_if(PhiR->users(), [](VPUser *U) {
        return isa<VPWidenSelectRecipe>(U) ||
               (isa<VPReplicateRecipe>(U) &&
                cast<VPReplicateRecipe>(U)->getUnderlyingInstr()->getOpcode() ==
                    Instruction::Select);
      }));
      VPValue *Cmp = Select->getOperand(0);
      // If the compare is checking the reduction PHI node, adjust it to check
      // the start value.
      if (VPRecipeBase *CmpR = Cmp->getDefiningRecipe()) {
        for (unsigned I = 0; I != CmpR->getNumOperands(); ++I)
          if (CmpR->getOperand(I) == PhiR)
            CmpR->setOperand(I, PhiR->getStartValue());
      }
      VPBuilder::InsertPointGuard Guard(Builder);
      Builder.setInsertPoint(Select);

      // If the true value of the select is the reduction phi, the new value is
      // selected if the negated condition is true in any iteration.
      if (Select->getOperand(1) == PhiR)
        Cmp = Builder.createNot(Cmp);
      VPValue *Or = Builder.createOr(PhiR, Cmp);
      Select->getVPSingleValue()->replaceAllUsesWith(Or);

      // Convert the reduction phi to operate on bools.
      PhiR->setOperand(0, Plan->getOrAddLiveIn(ConstantInt::getFalse(
                              OrigLoop->getHeader()->getContext())));
    }

    // If tail is folded by masking, introduce selects between the phi
    // and the live-out instruction of each reduction, at the beginning of the
    // dedicated latch block.
    auto *OrigExitingVPV = PhiR->getBackedgeValue();
    auto *NewExitingVPV = PhiR->getBackedgeValue();
    if (!PhiR->isInLoop() && CM.foldTailByMasking()) {
      VPValue *Cond = RecipeBuilder.getBlockInMask(OrigLoop->getHeader());
      assert(OrigExitingVPV->getDefiningRecipe()->getParent() != LatchVPBB &&
             "reduction recipe must be defined before latch");
      Type *PhiTy = PhiR->getOperand(0)->getLiveInIRValue()->getType();
      std::optional<FastMathFlags> FMFs =
          PhiTy->isFloatingPointTy()
              ? std::make_optional(RdxDesc.getFastMathFlags())
              : std::nullopt;
      NewExitingVPV =
          Builder.createSelect(Cond, OrigExitingVPV, PhiR, {}, "", FMFs);
      OrigExitingVPV->replaceUsesWithIf(NewExitingVPV, [](VPUser &U, unsigned) {
        return isa<VPInstruction>(&U) &&
               cast<VPInstruction>(&U)->getOpcode() ==
                   VPInstruction::ComputeReductionResult;
      });
      if (PreferPredicatedReductionSelect ||
          TTI.preferPredicatedReductionSelect(
              PhiR->getRecurrenceDescriptor().getOpcode(), PhiTy,
              TargetTransformInfo::ReductionFlags()))
        PhiR->setOperand(1, NewExitingVPV);
    }

    // If the vector reduction can be performed in a smaller type, we truncate
    // then extend the loop exit value to enable InstCombine to evaluate the
    // entire expression in the smaller type.
    Type *PhiTy = PhiR->getStartValue()->getLiveInIRValue()->getType();
    if (MinVF.isVector() && PhiTy != RdxDesc.getRecurrenceType() &&
        !RecurrenceDescriptor::isAnyOfRecurrenceKind(
            RdxDesc.getRecurrenceKind())) {
      assert(!PhiR->isInLoop() && "Unexpected truncated inloop reduction!");
      Type *RdxTy = RdxDesc.getRecurrenceType();
      auto *Trunc =
          new VPWidenCastRecipe(Instruction::Trunc, NewExitingVPV, RdxTy);
      auto *Extnd =
          RdxDesc.isSigned()
              ? new VPWidenCastRecipe(Instruction::SExt, Trunc, PhiTy)
              : new VPWidenCastRecipe(Instruction::ZExt, Trunc, PhiTy);

      Trunc->insertAfter(NewExitingVPV->getDefiningRecipe());
      Extnd->insertAfter(Trunc);
      if (PhiR->getOperand(1) == NewExitingVPV)
        PhiR->setOperand(1, Extnd->getVPSingleValue());
      NewExitingVPV = Extnd;
    }

    // We want code in the middle block to appear to execute on the location of
    // the scalar loop's latch terminator because: (a) it is all compiler
    // generated, (b) these instructions are always executed after evaluating
    // the latch conditional branch, and (c) other passes may add new
    // predecessors which terminate on this line. This is the easiest way to
    // ensure we don't accidentally cause an extra step back into the loop while
    // debugging.
    DebugLoc ExitDL = OrigLoop->getLoopLatch()->getTerminator()->getDebugLoc();

    // TODO: At the moment ComputeReductionResult also drives creation of the
    // bc.merge.rdx phi nodes, hence it needs to be created unconditionally here
    // even for in-loop reductions, until the reduction resume value handling is
    // also modeled in VPlan.
    auto *FinalReductionResult = new VPInstruction(
        VPInstruction::ComputeReductionResult, {PhiR, NewExitingVPV}, ExitDL);
    FinalReductionResult->insertBefore(*MiddleVPBB, IP);
    OrigExitingVPV->replaceUsesWithIf(
        FinalReductionResult,
        [](VPUser &User, unsigned) { return isa<VPLiveOut>(&User); });
  }

  VPlanTransforms::clearReductionWrapFlags(*Plan);
}

void VPWidenPointerInductionRecipe::execute(VPTransformState &State) {
  assert(IndDesc.getKind() == InductionDescriptor::IK_PtrInduction &&
         "Not a pointer induction according to InductionDescriptor!");
  assert(cast<PHINode>(getUnderlyingInstr())->getType()->isPointerTy() &&
         "Unexpected type.");
  assert(!onlyScalarsGenerated(State.VF.isScalable()) &&
         "Recipe should have been replaced");

  auto *IVR = getParent()->getPlan()->getCanonicalIV();
  PHINode *CanonicalIV = cast<PHINode>(State.get(IVR, 0, /*IsScalar*/ true));
  Type *PhiType = IndDesc.getStep()->getType();

  // Build a pointer phi
  Value *ScalarStartValue = getStartValue()->getLiveInIRValue();
  Type *ScStValueType = ScalarStartValue->getType();
  PHINode *NewPointerPhi = PHINode::Create(ScStValueType, 2, "pointer.phi",
                                           CanonicalIV->getIterator());

  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);
  NewPointerPhi->addIncoming(ScalarStartValue, VectorPH);

  // A pointer induction, performed by using a gep
  BasicBlock::iterator InductionLoc = State.Builder.GetInsertPoint();

  Value *ScalarStepValue = State.get(getOperand(1), VPIteration(0, 0));
  Value *RuntimeVF = getRuntimeVF(State.Builder, PhiType, State.VF);
  Value *NumUnrolledElems =
      State.Builder.CreateMul(RuntimeVF, ConstantInt::get(PhiType, State.UF));
  Value *InductionGEP = GetElementPtrInst::Create(
      State.Builder.getInt8Ty(), NewPointerPhi,
      State.Builder.CreateMul(ScalarStepValue, NumUnrolledElems), "ptr.ind",
      InductionLoc);
  // Add induction update using an incorrect block temporarily. The phi node
  // will be fixed after VPlan execution. Note that at this point the latch
  // block cannot be used, as it does not exist yet.
  // TODO: Model increment value in VPlan, by turning the recipe into a
  // multi-def and a subclass of VPHeaderPHIRecipe.
  NewPointerPhi->addIncoming(InductionGEP, VectorPH);

  // Create UF many actual address geps that use the pointer
  // phi as base and a vectorized version of the step value
  // (<step*0, ..., step*N>) as offset.
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    Type *VecPhiType = VectorType::get(PhiType, State.VF);
    Value *StartOffsetScalar =
        State.Builder.CreateMul(RuntimeVF, ConstantInt::get(PhiType, Part));
    Value *StartOffset =
        State.Builder.CreateVectorSplat(State.VF, StartOffsetScalar);
    // Create a vector of consecutive numbers from zero to VF.
    StartOffset = State.Builder.CreateAdd(
        StartOffset, State.Builder.CreateStepVector(VecPhiType));

    assert(ScalarStepValue == State.get(getOperand(1), VPIteration(Part, 0)) &&
           "scalar step must be the same across all parts");
    Value *GEP = State.Builder.CreateGEP(
        State.Builder.getInt8Ty(), NewPointerPhi,
        State.Builder.CreateMul(
            StartOffset,
            State.Builder.CreateVectorSplat(State.VF, ScalarStepValue),
            "vector.gep"));
    State.set(this, GEP, Part);
  }
}

void VPDerivedIVRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "VPDerivedIVRecipe being replicated.");

  // Fast-math-flags propagate from the original induction instruction.
  IRBuilder<>::FastMathFlagGuard FMFG(State.Builder);
  if (FPBinOp)
    State.Builder.setFastMathFlags(FPBinOp->getFastMathFlags());

  Value *Step = State.get(getStepValue(), VPIteration(0, 0));
  Value *CanonicalIV = State.get(getOperand(1), VPIteration(0, 0));
  Value *DerivedIV = emitTransformedIndex(
      State.Builder, CanonicalIV, getStartValue()->getLiveInIRValue(), Step,
      Kind, cast_if_present<BinaryOperator>(FPBinOp));
  DerivedIV->setName("offset.idx");
  assert(DerivedIV != CanonicalIV && "IV didn't need transforming?");

  State.set(this, DerivedIV, VPIteration(0, 0));
}

void VPReplicateRecipe::execute(VPTransformState &State) {
  Instruction *UI = getUnderlyingInstr();
  if (State.Instance) { // Generate a single instance.
    assert((State.VF.isScalar() || !isUniform()) &&
           "uniform recipe shouldn't be predicated");
    assert(!State.VF.isScalable() && "Can't scalarize a scalable vector");
    State.ILV->scalarizeInstruction(UI, this, *State.Instance, State);
    // Insert scalar instance packing it into a vector.
    if (State.VF.isVector() && shouldPack()) {
      // If we're constructing lane 0, initialize to start from poison.
      if (State.Instance->Lane.isFirstLane()) {
        assert(!State.VF.isScalable() && "VF is assumed to be non scalable.");
        Value *Poison = PoisonValue::get(
            VectorType::get(UI->getType(), State.VF));
        State.set(this, Poison, State.Instance->Part);
      }
      State.packScalarIntoVectorValue(this, *State.Instance);
    }
    return;
  }

  if (IsUniform) {
    // If the recipe is uniform across all parts (instead of just per VF), only
    // generate a single instance.
    if ((isa<LoadInst>(UI) || isa<StoreInst>(UI)) &&
        all_of(operands(), [](VPValue *Op) {
          return Op->isDefinedOutsideVectorRegions();
        })) {
      State.ILV->scalarizeInstruction(UI, this, VPIteration(0, 0), State);
      if (user_begin() != user_end()) {
        for (unsigned Part = 1; Part < State.UF; ++Part)
          State.set(this, State.get(this, VPIteration(0, 0)),
                    VPIteration(Part, 0));
      }
      return;
    }

    // Uniform within VL means we need to generate lane 0 only for each
    // unrolled copy.
    for (unsigned Part = 0; Part < State.UF; ++Part)
      State.ILV->scalarizeInstruction(UI, this, VPIteration(Part, 0), State);
    return;
  }

  // A store of a loop varying value to a uniform address only needs the last
  // copy of the store.
  if (isa<StoreInst>(UI) &&
      vputils::isUniformAfterVectorization(getOperand(1))) {
    auto Lane = VPLane::getLastLaneForVF(State.VF);
    State.ILV->scalarizeInstruction(UI, this, VPIteration(State.UF - 1, Lane),
                                    State);
    return;
  }

  // Generate scalar instances for all VF lanes of all UF parts.
  assert(!State.VF.isScalable() && "Can't scalarize a scalable vector");
  const unsigned EndLane = State.VF.getKnownMinValue();
  for (unsigned Part = 0; Part < State.UF; ++Part)
    for (unsigned Lane = 0; Lane < EndLane; ++Lane)
      State.ILV->scalarizeInstruction(UI, this, VPIteration(Part, Lane), State);
}

void VPWidenLoadRecipe::execute(VPTransformState &State) {
  auto *LI = cast<LoadInst>(&Ingredient);

  Type *ScalarDataTy = getLoadStoreType(&Ingredient);
  auto *DataTy = VectorType::get(ScalarDataTy, State.VF);
  const Align Alignment = getLoadStoreAlignment(&Ingredient);
  bool CreateGather = !isConsecutive();

  auto &Builder = State.Builder;
  State.setDebugLocFrom(getDebugLoc());
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    Value *NewLI;
    Value *Mask = nullptr;
    if (auto *VPMask = getMask()) {
      // Mask reversal is only needed for non-all-one (null) masks, as reverse
      // of a null all-one mask is a null mask.
      Mask = State.get(VPMask, Part);
      if (isReverse())
        Mask = Builder.CreateVectorReverse(Mask, "reverse");
    }

    Value *Addr = State.get(getAddr(), Part, /*IsScalar*/ !CreateGather);
    if (CreateGather) {
      NewLI = Builder.CreateMaskedGather(DataTy, Addr, Alignment, Mask, nullptr,
                                         "wide.masked.gather");
    } else if (Mask) {
      NewLI = Builder.CreateMaskedLoad(DataTy, Addr, Alignment, Mask,
                                       PoisonValue::get(DataTy),
                                       "wide.masked.load");
    } else {
      NewLI = Builder.CreateAlignedLoad(DataTy, Addr, Alignment, "wide.load");
    }
    // Add metadata to the load, but setVectorValue to the reverse shuffle.
    State.addMetadata(NewLI, LI);
    if (Reverse)
      NewLI = Builder.CreateVectorReverse(NewLI, "reverse");
    State.set(this, NewLI, Part);
  }
}

/// Use all-true mask for reverse rather than actual mask, as it avoids a
/// dependence w/o affecting the result.
static Instruction *createReverseEVL(IRBuilderBase &Builder, Value *Operand,
                                     Value *EVL, const Twine &Name) {
  VectorType *ValTy = cast<VectorType>(Operand->getType());
  Value *AllTrueMask =
      Builder.CreateVectorSplat(ValTy->getElementCount(), Builder.getTrue());
  return Builder.CreateIntrinsic(ValTy, Intrinsic::experimental_vp_reverse,
                                 {Operand, AllTrueMask, EVL}, nullptr, Name);
}

void VPWidenLoadEVLRecipe::execute(VPTransformState &State) {
  assert(State.UF == 1 && "Expected only UF == 1 when vectorizing with "
                          "explicit vector length.");
  auto *LI = cast<LoadInst>(&Ingredient);

  Type *ScalarDataTy = getLoadStoreType(&Ingredient);
  auto *DataTy = VectorType::get(ScalarDataTy, State.VF);
  const Align Alignment = getLoadStoreAlignment(&Ingredient);
  bool CreateGather = !isConsecutive();

  auto &Builder = State.Builder;
  State.setDebugLocFrom(getDebugLoc());
  CallInst *NewLI;
  Value *EVL = State.get(getEVL(), VPIteration(0, 0));
  Value *Addr = State.get(getAddr(), 0, !CreateGather);
  Value *Mask = nullptr;
  if (VPValue *VPMask = getMask()) {
    Mask = State.get(VPMask, 0);
    if (isReverse())
      Mask = createReverseEVL(Builder, Mask, EVL, "vp.reverse.mask");
  } else {
    Mask = Builder.CreateVectorSplat(State.VF, Builder.getTrue());
  }

  if (CreateGather) {
    NewLI =
        Builder.CreateIntrinsic(DataTy, Intrinsic::vp_gather, {Addr, Mask, EVL},
                                nullptr, "wide.masked.gather");
  } else {
    VectorBuilder VBuilder(Builder);
    VBuilder.setEVL(EVL).setMask(Mask);
    NewLI = cast<CallInst>(VBuilder.createVectorInstruction(
        Instruction::Load, DataTy, Addr, "vp.op.load"));
  }
  NewLI->addParamAttr(
      0, Attribute::getWithAlignment(NewLI->getContext(), Alignment));
  State.addMetadata(NewLI, LI);
  Instruction *Res = NewLI;
  if (isReverse())
    Res = createReverseEVL(Builder, Res, EVL, "vp.reverse");
  State.set(this, Res, 0);
}

void VPWidenStoreRecipe::execute(VPTransformState &State) {
  auto *SI = cast<StoreInst>(&Ingredient);

  VPValue *StoredVPValue = getStoredValue();
  bool CreateScatter = !isConsecutive();
  const Align Alignment = getLoadStoreAlignment(&Ingredient);

  auto &Builder = State.Builder;
  State.setDebugLocFrom(getDebugLoc());

  for (unsigned Part = 0; Part < State.UF; ++Part) {
    Instruction *NewSI = nullptr;
    Value *Mask = nullptr;
    if (auto *VPMask = getMask()) {
      // Mask reversal is only needed for non-all-one (null) masks, as reverse
      // of a null all-one mask is a null mask.
      Mask = State.get(VPMask, Part);
      if (isReverse())
        Mask = Builder.CreateVectorReverse(Mask, "reverse");
    }

    Value *StoredVal = State.get(StoredVPValue, Part);
    if (isReverse()) {
      // If we store to reverse consecutive memory locations, then we need
      // to reverse the order of elements in the stored value.
      StoredVal = Builder.CreateVectorReverse(StoredVal, "reverse");
      // We don't want to update the value in the map as it might be used in
      // another expression. So don't call resetVectorValue(StoredVal).
    }
    Value *Addr = State.get(getAddr(), Part, /*IsScalar*/ !CreateScatter);
    if (CreateScatter)
      NewSI = Builder.CreateMaskedScatter(StoredVal, Addr, Alignment, Mask);
    else if (Mask)
      NewSI = Builder.CreateMaskedStore(StoredVal, Addr, Alignment, Mask);
    else
      NewSI = Builder.CreateAlignedStore(StoredVal, Addr, Alignment);
    State.addMetadata(NewSI, SI);
  }
}

void VPWidenStoreEVLRecipe::execute(VPTransformState &State) {
  assert(State.UF == 1 && "Expected only UF == 1 when vectorizing with "
                          "explicit vector length.");
  auto *SI = cast<StoreInst>(&Ingredient);

  VPValue *StoredValue = getStoredValue();
  bool CreateScatter = !isConsecutive();
  const Align Alignment = getLoadStoreAlignment(&Ingredient);

  auto &Builder = State.Builder;
  State.setDebugLocFrom(getDebugLoc());

  CallInst *NewSI = nullptr;
  Value *StoredVal = State.get(StoredValue, 0);
  Value *EVL = State.get(getEVL(), VPIteration(0, 0));
  if (isReverse())
    StoredVal = createReverseEVL(Builder, StoredVal, EVL, "vp.reverse");
  Value *Mask = nullptr;
  if (VPValue *VPMask = getMask()) {
    Mask = State.get(VPMask, 0);
    if (isReverse())
      Mask = createReverseEVL(Builder, Mask, EVL, "vp.reverse.mask");
  } else {
    Mask = Builder.CreateVectorSplat(State.VF, Builder.getTrue());
  }
  Value *Addr = State.get(getAddr(), 0, !CreateScatter);
  if (CreateScatter) {
    NewSI = Builder.CreateIntrinsic(Type::getVoidTy(EVL->getContext()),
                                    Intrinsic::vp_scatter,
                                    {StoredVal, Addr, Mask, EVL});
  } else {
    VectorBuilder VBuilder(Builder);
    VBuilder.setEVL(EVL).setMask(Mask);
    NewSI = cast<CallInst>(VBuilder.createVectorInstruction(
        Instruction::Store, Type::getVoidTy(EVL->getContext()),
        {StoredVal, Addr}));
  }
  NewSI->addParamAttr(
      1, Attribute::getWithAlignment(NewSI->getContext(), Alignment));
  State.addMetadata(NewSI, SI);
}

// Determine how to lower the scalar epilogue, which depends on 1) optimising
// for minimum code-size, 2) predicate compiler options, 3) loop hints forcing
// predication, and 4) a TTI hook that analyses whether the loop is suitable
// for predication.
static ScalarEpilogueLowering getScalarEpilogueLowering(
    Function *F, Loop *L, LoopVectorizeHints &Hints, ProfileSummaryInfo *PSI,
    BlockFrequencyInfo *BFI, TargetTransformInfo *TTI, TargetLibraryInfo *TLI,
    LoopVectorizationLegality &LVL, InterleavedAccessInfo *IAI) {
  // 1) OptSize takes precedence over all other options, i.e. if this is set,
  // don't look at hints or options, and don't request a scalar epilogue.
  // (For PGSO, as shouldOptimizeForSize isn't currently accessible from
  // LoopAccessInfo (due to code dependency and not being able to reliably get
  // PSI/BFI from a loop analysis under NPM), we cannot suppress the collection
  // of strides in LoopAccessInfo::analyzeLoop() and vectorize without
  // versioning when the vectorization is forced, unlike hasOptSize. So revert
  // back to the old way and vectorize with versioning when forced. See D81345.)
  if (F->hasOptSize() || (llvm::shouldOptimizeForSize(L->getHeader(), PSI, BFI,
                                                      PGSOQueryType::IRPass) &&
                          Hints.getForce() != LoopVectorizeHints::FK_Enabled))
    return CM_ScalarEpilogueNotAllowedOptSize;

  // 2) If set, obey the directives
  if (PreferPredicateOverEpilogue.getNumOccurrences()) {
    switch (PreferPredicateOverEpilogue) {
    case PreferPredicateTy::ScalarEpilogue:
      return CM_ScalarEpilogueAllowed;
    case PreferPredicateTy::PredicateElseScalarEpilogue:
      return CM_ScalarEpilogueNotNeededUsePredicate;
    case PreferPredicateTy::PredicateOrDontVectorize:
      return CM_ScalarEpilogueNotAllowedUsePredicate;
    };
  }

  // 3) If set, obey the hints
  switch (Hints.getPredicate()) {
  case LoopVectorizeHints::FK_Enabled:
    return CM_ScalarEpilogueNotNeededUsePredicate;
  case LoopVectorizeHints::FK_Disabled:
    return CM_ScalarEpilogueAllowed;
  };

  // 4) if the TTI hook indicates this is profitable, request predication.
  TailFoldingInfo TFI(TLI, &LVL, IAI);
  if (TTI->preferPredicateOverEpilogue(&TFI))
    return CM_ScalarEpilogueNotNeededUsePredicate;

  return CM_ScalarEpilogueAllowed;
}

// Process the loop in the VPlan-native vectorization path. This path builds
// VPlan upfront in the vectorization pipeline, which allows to apply
// VPlan-to-VPlan transformations from the very beginning without modifying the
// input LLVM IR.
static bool processLoopInVPlanNativePath(
    Loop *L, PredicatedScalarEvolution &PSE, LoopInfo *LI, DominatorTree *DT,
    LoopVectorizationLegality *LVL, TargetTransformInfo *TTI,
    TargetLibraryInfo *TLI, DemandedBits *DB, AssumptionCache *AC,
    OptimizationRemarkEmitter *ORE, BlockFrequencyInfo *BFI,
    ProfileSummaryInfo *PSI, LoopVectorizeHints &Hints,
    LoopVectorizationRequirements &Requirements) {

  if (isa<SCEVCouldNotCompute>(PSE.getBackedgeTakenCount())) {
    LLVM_DEBUG(dbgs() << "LV: cannot compute the outer-loop trip count\n");
    return false;
  }
  assert(EnableVPlanNativePath && "VPlan-native path is disabled.");
  Function *F = L->getHeader()->getParent();
  InterleavedAccessInfo IAI(PSE, L, DT, LI, LVL->getLAI());

  ScalarEpilogueLowering SEL =
      getScalarEpilogueLowering(F, L, Hints, PSI, BFI, TTI, TLI, *LVL, &IAI);

  LoopVectorizationCostModel CM(SEL, L, PSE, LI, LVL, *TTI, TLI, DB, AC, ORE, F,
                                &Hints, IAI);
  // Use the planner for outer loop vectorization.
  // TODO: CM is not used at this point inside the planner. Turn CM into an
  // optional argument if we don't need it in the future.
  LoopVectorizationPlanner LVP(L, LI, DT, TLI, *TTI, LVL, CM, IAI, PSE, Hints,
                               ORE);

  // Get user vectorization factor.
  ElementCount UserVF = Hints.getWidth();

  CM.collectElementTypesForWidening();

  // Plan how to best vectorize, return the best VF and its cost.
  const VectorizationFactor VF = LVP.planInVPlanNativePath(UserVF);

  // If we are stress testing VPlan builds, do not attempt to generate vector
  // code. Masked vector code generation support will follow soon.
  // Also, do not attempt to vectorize if no vector code will be produced.
  if (VPlanBuildStressTest || VectorizationFactor::Disabled() == VF)
    return false;

  VPlan &BestPlan = LVP.getBestPlanFor(VF.Width);

  {
    bool AddBranchWeights =
        hasBranchWeightMD(*L->getLoopLatch()->getTerminator());
    GeneratedRTChecks Checks(*PSE.getSE(), DT, LI, TTI,
                             F->getDataLayout(), AddBranchWeights);
    InnerLoopVectorizer LB(L, PSE, LI, DT, TLI, TTI, AC, ORE, VF.Width,
                           VF.Width, 1, LVL, &CM, BFI, PSI, Checks);
    LLVM_DEBUG(dbgs() << "Vectorizing outer loop in \""
                      << L->getHeader()->getParent()->getName() << "\"\n");
    LVP.executePlan(VF.Width, 1, BestPlan, LB, DT, false);
  }

  reportVectorization(ORE, L, VF, 1);

  // Mark the loop as already vectorized to avoid vectorizing again.
  Hints.setAlreadyVectorized();
  assert(!verifyFunction(*L->getHeader()->getParent(), &dbgs()));
  return true;
}

// Emit a remark if there are stores to floats that required a floating point
// extension. If the vectorized loop was generated with floating point there
// will be a performance penalty from the conversion overhead and the change in
// the vector width.
static void checkMixedPrecision(Loop *L, OptimizationRemarkEmitter *ORE) {
  SmallVector<Instruction *, 4> Worklist;
  for (BasicBlock *BB : L->getBlocks()) {
    for (Instruction &Inst : *BB) {
      if (auto *S = dyn_cast<StoreInst>(&Inst)) {
        if (S->getValueOperand()->getType()->isFloatTy())
          Worklist.push_back(S);
      }
    }
  }

  // Traverse the floating point stores upwards searching, for floating point
  // conversions.
  SmallPtrSet<const Instruction *, 4> Visited;
  SmallPtrSet<const Instruction *, 4> EmittedRemark;
  while (!Worklist.empty()) {
    auto *I = Worklist.pop_back_val();
    if (!L->contains(I))
      continue;
    if (!Visited.insert(I).second)
      continue;

    // Emit a remark if the floating point store required a floating
    // point conversion.
    // TODO: More work could be done to identify the root cause such as a
    // constant or a function return type and point the user to it.
    if (isa<FPExtInst>(I) && EmittedRemark.insert(I).second)
      ORE->emit([&]() {
        return OptimizationRemarkAnalysis(LV_NAME, "VectorMixedPrecision",
                                          I->getDebugLoc(), L->getHeader())
               << "floating point conversion changes vector width. "
               << "Mixed floating point precision requires an up/down "
               << "cast that will negatively impact performance.";
      });

    for (Use &Op : I->operands())
      if (auto *OpI = dyn_cast<Instruction>(Op))
        Worklist.push_back(OpI);
  }
}

static bool areRuntimeChecksProfitable(GeneratedRTChecks &Checks,
                                       VectorizationFactor &VF,
                                       std::optional<unsigned> VScale, Loop *L,
                                       ScalarEvolution &SE,
                                       ScalarEpilogueLowering SEL) {
  InstructionCost CheckCost = Checks.getCost();
  if (!CheckCost.isValid())
    return false;

  // When interleaving only scalar and vector cost will be equal, which in turn
  // would lead to a divide by 0. Fall back to hard threshold.
  if (VF.Width.isScalar()) {
    if (CheckCost > VectorizeMemoryCheckThreshold) {
      LLVM_DEBUG(
          dbgs()
          << "LV: Interleaving only is not profitable due to runtime checks\n");
      return false;
    }
    return true;
  }

  // The scalar cost should only be 0 when vectorizing with a user specified VF/IC. In those cases, runtime checks should always be generated.
  uint64_t ScalarC = *VF.ScalarCost.getValue();
  if (ScalarC == 0)
    return true;

  // First, compute the minimum iteration count required so that the vector
  // loop outperforms the scalar loop.
  //  The total cost of the scalar loop is
  //   ScalarC * TC
  //  where
  //  * TC is the actual trip count of the loop.
  //  * ScalarC is the cost of a single scalar iteration.
  //
  //  The total cost of the vector loop is
  //    RtC + VecC * (TC / VF) + EpiC
  //  where
  //  * RtC is the cost of the generated runtime checks
  //  * VecC is the cost of a single vector iteration.
  //  * TC is the actual trip count of the loop
  //  * VF is the vectorization factor
  //  * EpiCost is the cost of the generated epilogue, including the cost
  //    of the remaining scalar operations.
  //
  // Vectorization is profitable once the total vector cost is less than the
  // total scalar cost:
  //   RtC + VecC * (TC / VF) + EpiC <  ScalarC * TC
  //
  // Now we can compute the minimum required trip count TC as
  //   VF * (RtC + EpiC) / (ScalarC * VF - VecC) < TC
  //
  // For now we assume the epilogue cost EpiC = 0 for simplicity. Note that
  // the computations are performed on doubles, not integers and the result
  // is rounded up, hence we get an upper estimate of the TC.
  unsigned IntVF = VF.Width.getKnownMinValue();
  if (VF.Width.isScalable()) {
    unsigned AssumedMinimumVscale = 1;
    if (VScale)
      AssumedMinimumVscale = *VScale;
    IntVF *= AssumedMinimumVscale;
  }
  uint64_t RtC = *CheckCost.getValue();
  uint64_t Div = ScalarC * IntVF - *VF.Cost.getValue();
  uint64_t MinTC1 = Div == 0 ? 0 : divideCeil(RtC * IntVF, Div);

  // Second, compute a minimum iteration count so that the cost of the
  // runtime checks is only a fraction of the total scalar loop cost. This
  // adds a loop-dependent bound on the overhead incurred if the runtime
  // checks fail. In case the runtime checks fail, the cost is RtC + ScalarC
  // * TC. To bound the runtime check to be a fraction 1/X of the scalar
  // cost, compute
  //   RtC < ScalarC * TC * (1 / X)  ==>  RtC * X / ScalarC < TC
  uint64_t MinTC2 = divideCeil(RtC * 10, ScalarC);

  // Now pick the larger minimum. If it is not a multiple of VF and a scalar
  // epilogue is allowed, choose the next closest multiple of VF. This should
  // partly compensate for ignoring the epilogue cost.
  uint64_t MinTC = std::max(MinTC1, MinTC2);
  if (SEL == CM_ScalarEpilogueAllowed)
    MinTC = alignTo(MinTC, IntVF);
  VF.MinProfitableTripCount = ElementCount::getFixed(MinTC);

  LLVM_DEBUG(
      dbgs() << "LV: Minimum required TC for runtime checks to be profitable:"
             << VF.MinProfitableTripCount << "\n");

  // Skip vectorization if the expected trip count is less than the minimum
  // required trip count.
  if (auto ExpectedTC = getSmallBestKnownTC(SE, L)) {
    if (ElementCount::isKnownLT(ElementCount::getFixed(*ExpectedTC),
                                VF.MinProfitableTripCount)) {
      LLVM_DEBUG(dbgs() << "LV: Vectorization is not beneficial: expected "
                           "trip count < minimum profitable VF ("
                        << *ExpectedTC << " < " << VF.MinProfitableTripCount
                        << ")\n");

      return false;
    }
  }
  return true;
}

LoopVectorizePass::LoopVectorizePass(LoopVectorizeOptions Opts)
    : InterleaveOnlyWhenForced(Opts.InterleaveOnlyWhenForced ||
                               !EnableLoopInterleaving),
      VectorizeOnlyWhenForced(Opts.VectorizeOnlyWhenForced ||
                              !EnableLoopVectorization) {}

bool LoopVectorizePass::processLoop(Loop *L) {
  assert((EnableVPlanNativePath || L->isInnermost()) &&
         "VPlan-native path is not enabled. Only process inner loops.");

  LLVM_DEBUG(dbgs() << "\nLV: Checking a loop in '"
                    << L->getHeader()->getParent()->getName() << "' from "
                    << L->getLocStr() << "\n");

  LoopVectorizeHints Hints(L, InterleaveOnlyWhenForced, *ORE, TTI);

  LLVM_DEBUG(
      dbgs() << "LV: Loop hints:"
             << " force="
             << (Hints.getForce() == LoopVectorizeHints::FK_Disabled
                     ? "disabled"
                     : (Hints.getForce() == LoopVectorizeHints::FK_Enabled
                            ? "enabled"
                            : "?"))
             << " width=" << Hints.getWidth()
             << " interleave=" << Hints.getInterleave() << "\n");

  // Function containing loop
  Function *F = L->getHeader()->getParent();

  // Looking at the diagnostic output is the only way to determine if a loop
  // was vectorized (other than looking at the IR or machine code), so it
  // is important to generate an optimization remark for each loop. Most of
  // these messages are generated as OptimizationRemarkAnalysis. Remarks
  // generated as OptimizationRemark and OptimizationRemarkMissed are
  // less verbose reporting vectorized loops and unvectorized loops that may
  // benefit from vectorization, respectively.

  if (!Hints.allowVectorization(F, L, VectorizeOnlyWhenForced)) {
    LLVM_DEBUG(dbgs() << "LV: Loop hints prevent vectorization.\n");
    return false;
  }

  PredicatedScalarEvolution PSE(*SE, *L);

  // Check if it is legal to vectorize the loop.
  LoopVectorizationRequirements Requirements;
  LoopVectorizationLegality LVL(L, PSE, DT, TTI, TLI, F, *LAIs, LI, ORE,
                                &Requirements, &Hints, DB, AC, BFI, PSI);
  if (!LVL.canVectorize(EnableVPlanNativePath)) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: Cannot prove legality.\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  // Entrance to the VPlan-native vectorization path. Outer loops are processed
  // here. They may require CFG and instruction level transformations before
  // even evaluating whether vectorization is profitable. Since we cannot modify
  // the incoming IR, we need to build VPlan upfront in the vectorization
  // pipeline.
  if (!L->isInnermost())
    return processLoopInVPlanNativePath(L, PSE, LI, DT, &LVL, TTI, TLI, DB, AC,
                                        ORE, BFI, PSI, Hints, Requirements);

  assert(L->isInnermost() && "Inner loop expected.");

  InterleavedAccessInfo IAI(PSE, L, DT, LI, LVL.getLAI());
  bool UseInterleaved = TTI->enableInterleavedAccessVectorization();

  // If an override option has been passed in for interleaved accesses, use it.
  if (EnableInterleavedMemAccesses.getNumOccurrences() > 0)
    UseInterleaved = EnableInterleavedMemAccesses;

  // Analyze interleaved memory accesses.
  if (UseInterleaved)
    IAI.analyzeInterleaving(useMaskedInterleavedAccesses(*TTI));

  // Check the function attributes and profiles to find out if this function
  // should be optimized for size.
  ScalarEpilogueLowering SEL =
      getScalarEpilogueLowering(F, L, Hints, PSI, BFI, TTI, TLI, LVL, &IAI);

  // Check the loop for a trip count threshold: vectorize loops with a tiny trip
  // count by optimizing for size, to minimize overheads.
  auto ExpectedTC = getSmallBestKnownTC(*SE, L);
  if (ExpectedTC && *ExpectedTC < TinyTripCountVectorThreshold) {
    LLVM_DEBUG(dbgs() << "LV: Found a loop with a very small trip count. "
                      << "This loop is worth vectorizing only if no scalar "
                      << "iteration overheads are incurred.");
    if (Hints.getForce() == LoopVectorizeHints::FK_Enabled)
      LLVM_DEBUG(dbgs() << " But vectorizing was explicitly forced.\n");
    else {
      if (*ExpectedTC > TTI->getMinTripCountTailFoldingThreshold()) {
        LLVM_DEBUG(dbgs() << "\n");
        // Predicate tail-folded loops are efficient even when the loop
        // iteration count is low. However, setting the epilogue policy to
        // `CM_ScalarEpilogueNotAllowedLowTripLoop` prevents vectorizing loops
        // with runtime checks. It's more effective to let
        // `areRuntimeChecksProfitable` determine if vectorization is beneficial
        // for the loop.
        if (SEL != CM_ScalarEpilogueNotNeededUsePredicate)
          SEL = CM_ScalarEpilogueNotAllowedLowTripLoop;
      } else {
        LLVM_DEBUG(dbgs() << " But the target considers the trip count too "
                             "small to consider vectorizing.\n");
        reportVectorizationFailure(
            "The trip count is below the minial threshold value.",
            "loop trip count is too low, avoiding vectorization",
            "LowTripCount", ORE, L);
        Hints.emitRemarkWithHints();
        return false;
      }
    }
  }

  // Check the function attributes to see if implicit floats or vectors are
  // allowed.
  if (F->hasFnAttribute(Attribute::NoImplicitFloat)) {
    reportVectorizationFailure(
        "Can't vectorize when the NoImplicitFloat attribute is used",
        "loop not vectorized due to NoImplicitFloat attribute",
        "NoImplicitFloat", ORE, L);
    Hints.emitRemarkWithHints();
    return false;
  }

  // Check if the target supports potentially unsafe FP vectorization.
  // FIXME: Add a check for the type of safety issue (denormal, signaling)
  // for the target we're vectorizing for, to make sure none of the
  // additional fp-math flags can help.
  if (Hints.isPotentiallyUnsafe() &&
      TTI->isFPVectorizationPotentiallyUnsafe()) {
    reportVectorizationFailure(
        "Potentially unsafe FP op prevents vectorization",
        "loop not vectorized due to unsafe FP support.",
        "UnsafeFP", ORE, L);
    Hints.emitRemarkWithHints();
    return false;
  }

  bool AllowOrderedReductions;
  // If the flag is set, use that instead and override the TTI behaviour.
  if (ForceOrderedReductions.getNumOccurrences() > 0)
    AllowOrderedReductions = ForceOrderedReductions;
  else
    AllowOrderedReductions = TTI->enableOrderedReductions();
  if (!LVL.canVectorizeFPMath(AllowOrderedReductions)) {
    ORE->emit([&]() {
      auto *ExactFPMathInst = Requirements.getExactFPInst();
      return OptimizationRemarkAnalysisFPCommute(DEBUG_TYPE, "CantReorderFPOps",
                                                 ExactFPMathInst->getDebugLoc(),
                                                 ExactFPMathInst->getParent())
             << "loop not vectorized: cannot prove it is safe to reorder "
                "floating-point operations";
    });
    LLVM_DEBUG(dbgs() << "LV: loop not vectorized: cannot prove it is safe to "
                         "reorder floating-point operations\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  // Use the cost model.
  LoopVectorizationCostModel CM(SEL, L, PSE, LI, &LVL, *TTI, TLI, DB, AC, ORE,
                                F, &Hints, IAI);
  // Use the planner for vectorization.
  LoopVectorizationPlanner LVP(L, LI, DT, TLI, *TTI, &LVL, CM, IAI, PSE, Hints,
                               ORE);

  // Get user vectorization factor and interleave count.
  ElementCount UserVF = Hints.getWidth();
  unsigned UserIC = Hints.getInterleave();

  // Plan how to best vectorize, return the best VF and its cost.
  std::optional<VectorizationFactor> MaybeVF = LVP.plan(UserVF, UserIC);

  VectorizationFactor VF = VectorizationFactor::Disabled();
  unsigned IC = 1;

  bool AddBranchWeights =
      hasBranchWeightMD(*L->getLoopLatch()->getTerminator());
  GeneratedRTChecks Checks(*PSE.getSE(), DT, LI, TTI,
                           F->getDataLayout(), AddBranchWeights);
  if (MaybeVF) {
    VF = *MaybeVF;
    // Select the interleave count.
    IC = CM.selectInterleaveCount(VF.Width, VF.Cost);

    unsigned SelectedIC = std::max(IC, UserIC);
    //  Optimistically generate runtime checks if they are needed. Drop them if
    //  they turn out to not be profitable.
    if (VF.Width.isVector() || SelectedIC > 1)
      Checks.Create(L, *LVL.getLAI(), PSE.getPredicate(), VF.Width, SelectedIC);

    // Check if it is profitable to vectorize with runtime checks.
    bool ForceVectorization =
        Hints.getForce() == LoopVectorizeHints::FK_Enabled;
    if (!ForceVectorization &&
        !areRuntimeChecksProfitable(Checks, VF, getVScaleForTuning(L, *TTI), L,
                                    *PSE.getSE(), SEL)) {
      ORE->emit([&]() {
        return OptimizationRemarkAnalysisAliasing(
                   DEBUG_TYPE, "CantReorderMemOps", L->getStartLoc(),
                   L->getHeader())
               << "loop not vectorized: cannot prove it is safe to reorder "
                  "memory operations";
      });
      LLVM_DEBUG(dbgs() << "LV: Too many memory checks needed.\n");
      Hints.emitRemarkWithHints();
      return false;
    }
  }

  // Identify the diagnostic messages that should be produced.
  std::pair<StringRef, std::string> VecDiagMsg, IntDiagMsg;
  bool VectorizeLoop = true, InterleaveLoop = true;
  if (VF.Width.isScalar()) {
    LLVM_DEBUG(dbgs() << "LV: Vectorization is possible but not beneficial.\n");
    VecDiagMsg = std::make_pair(
        "VectorizationNotBeneficial",
        "the cost-model indicates that vectorization is not beneficial");
    VectorizeLoop = false;
  }

  if (!MaybeVF && UserIC > 1) {
    // Tell the user interleaving was avoided up-front, despite being explicitly
    // requested.
    LLVM_DEBUG(dbgs() << "LV: Ignoring UserIC, because vectorization and "
                         "interleaving should be avoided up front\n");
    IntDiagMsg = std::make_pair(
        "InterleavingAvoided",
        "Ignoring UserIC, because interleaving was avoided up front");
    InterleaveLoop = false;
  } else if (IC == 1 && UserIC <= 1) {
    // Tell the user interleaving is not beneficial.
    LLVM_DEBUG(dbgs() << "LV: Interleaving is not beneficial.\n");
    IntDiagMsg = std::make_pair(
        "InterleavingNotBeneficial",
        "the cost-model indicates that interleaving is not beneficial");
    InterleaveLoop = false;
    if (UserIC == 1) {
      IntDiagMsg.first = "InterleavingNotBeneficialAndDisabled";
      IntDiagMsg.second +=
          " and is explicitly disabled or interleave count is set to 1";
    }
  } else if (IC > 1 && UserIC == 1) {
    // Tell the user interleaving is beneficial, but it explicitly disabled.
    LLVM_DEBUG(
        dbgs() << "LV: Interleaving is beneficial but is explicitly disabled.");
    IntDiagMsg = std::make_pair(
        "InterleavingBeneficialButDisabled",
        "the cost-model indicates that interleaving is beneficial "
        "but is explicitly disabled or interleave count is set to 1");
    InterleaveLoop = false;
  }

  // Override IC if user provided an interleave count.
  IC = UserIC > 0 ? UserIC : IC;

  // Emit diagnostic messages, if any.
  const char *VAPassName = Hints.vectorizeAnalysisPassName();
  if (!VectorizeLoop && !InterleaveLoop) {
    // Do not vectorize or interleaving the loop.
    ORE->emit([&]() {
      return OptimizationRemarkMissed(VAPassName, VecDiagMsg.first,
                                      L->getStartLoc(), L->getHeader())
             << VecDiagMsg.second;
    });
    ORE->emit([&]() {
      return OptimizationRemarkMissed(LV_NAME, IntDiagMsg.first,
                                      L->getStartLoc(), L->getHeader())
             << IntDiagMsg.second;
    });
    return false;
  } else if (!VectorizeLoop && InterleaveLoop) {
    LLVM_DEBUG(dbgs() << "LV: Interleave Count is " << IC << '\n');
    ORE->emit([&]() {
      return OptimizationRemarkAnalysis(VAPassName, VecDiagMsg.first,
                                        L->getStartLoc(), L->getHeader())
             << VecDiagMsg.second;
    });
  } else if (VectorizeLoop && !InterleaveLoop) {
    LLVM_DEBUG(dbgs() << "LV: Found a vectorizable loop (" << VF.Width
                      << ") in " << L->getLocStr() << '\n');
    ORE->emit([&]() {
      return OptimizationRemarkAnalysis(LV_NAME, IntDiagMsg.first,
                                        L->getStartLoc(), L->getHeader())
             << IntDiagMsg.second;
    });
  } else if (VectorizeLoop && InterleaveLoop) {
    LLVM_DEBUG(dbgs() << "LV: Found a vectorizable loop (" << VF.Width
                      << ") in " << L->getLocStr() << '\n');
    LLVM_DEBUG(dbgs() << "LV: Interleave Count is " << IC << '\n');
  }

  bool DisableRuntimeUnroll = false;
  MDNode *OrigLoopID = L->getLoopID();
  {
    using namespace ore;
    if (!VectorizeLoop) {
      assert(IC > 1 && "interleave count should not be 1 or 0");
      // If we decided that it is not legal to vectorize the loop, then
      // interleave it.
      InnerLoopUnroller Unroller(L, PSE, LI, DT, TLI, TTI, AC, ORE, IC, &LVL,
                                 &CM, BFI, PSI, Checks);

      VPlan &BestPlan =
          UseLegacyCostModel ? LVP.getBestPlanFor(VF.Width) : LVP.getBestPlan();
      assert((UseLegacyCostModel || BestPlan.hasScalarVFOnly()) &&
             "VPlan cost model and legacy cost model disagreed");
      LVP.executePlan(VF.Width, IC, BestPlan, Unroller, DT, false);

      ORE->emit([&]() {
        return OptimizationRemark(LV_NAME, "Interleaved", L->getStartLoc(),
                                  L->getHeader())
               << "interleaved loop (interleaved count: "
               << NV("InterleaveCount", IC) << ")";
      });
    } else {
      // If we decided that it is *legal* to vectorize the loop, then do it.

      // Consider vectorizing the epilogue too if it's profitable.
      VectorizationFactor EpilogueVF =
          LVP.selectEpilogueVectorizationFactor(VF.Width, IC);
      if (EpilogueVF.Width.isVector()) {

        // The first pass vectorizes the main loop and creates a scalar epilogue
        // to be vectorized by executing the plan (potentially with a different
        // factor) again shortly afterwards.
        EpilogueLoopVectorizationInfo EPI(VF.Width, IC, EpilogueVF.Width, 1);
        EpilogueVectorizerMainLoop MainILV(L, PSE, LI, DT, TLI, TTI, AC, ORE,
                                           EPI, &LVL, &CM, BFI, PSI, Checks);

        std::unique_ptr<VPlan> BestMainPlan(
            LVP.getBestPlanFor(EPI.MainLoopVF).duplicate());
        const auto &[ExpandedSCEVs, ReductionResumeValues] = LVP.executePlan(
            EPI.MainLoopVF, EPI.MainLoopUF, *BestMainPlan, MainILV, DT, true);
        ++LoopsVectorized;

        // Second pass vectorizes the epilogue and adjusts the control flow
        // edges from the first pass.
        EPI.MainLoopVF = EPI.EpilogueVF;
        EPI.MainLoopUF = EPI.EpilogueUF;
        EpilogueVectorizerEpilogueLoop EpilogILV(L, PSE, LI, DT, TLI, TTI, AC,
                                                 ORE, EPI, &LVL, &CM, BFI, PSI,
                                                 Checks);

        VPlan &BestEpiPlan = LVP.getBestPlanFor(EPI.EpilogueVF);
        VPRegionBlock *VectorLoop = BestEpiPlan.getVectorLoopRegion();
        VPBasicBlock *Header = VectorLoop->getEntryBasicBlock();
        Header->setName("vec.epilog.vector.body");

        // Re-use the trip count and steps expanded for the main loop, as
        // skeleton creation needs it as a value that dominates both the scalar
        // and vector epilogue loops
        // TODO: This is a workaround needed for epilogue vectorization and it
        // should be removed once induction resume value creation is done
        // directly in VPlan.
        EpilogILV.setTripCount(MainILV.getTripCount());
        for (auto &R : make_early_inc_range(*BestEpiPlan.getPreheader())) {
          auto *ExpandR = cast<VPExpandSCEVRecipe>(&R);
          auto *ExpandedVal = BestEpiPlan.getOrAddLiveIn(
              ExpandedSCEVs.find(ExpandR->getSCEV())->second);
          ExpandR->replaceAllUsesWith(ExpandedVal);
          if (BestEpiPlan.getTripCount() == ExpandR)
            BestEpiPlan.resetTripCount(ExpandedVal);
          ExpandR->eraseFromParent();
        }

        // Ensure that the start values for any VPWidenIntOrFpInductionRecipe,
        // VPWidenPointerInductionRecipe and VPReductionPHIRecipes are updated
        // before vectorizing the epilogue loop.
        for (VPRecipeBase &R : Header->phis()) {
          if (isa<VPCanonicalIVPHIRecipe>(&R))
            continue;

          Value *ResumeV = nullptr;
          // TODO: Move setting of resume values to prepareToExecute.
          if (auto *ReductionPhi = dyn_cast<VPReductionPHIRecipe>(&R)) {
            const RecurrenceDescriptor &RdxDesc =
                ReductionPhi->getRecurrenceDescriptor();
            RecurKind RK = RdxDesc.getRecurrenceKind();
            ResumeV = ReductionResumeValues.find(&RdxDesc)->second;
            if (RecurrenceDescriptor::isAnyOfRecurrenceKind(RK)) {
              // VPReductionPHIRecipes for AnyOf reductions expect a boolean as
              // start value; compare the final value from the main vector loop
              // to the start value.
              IRBuilder<> Builder(
                  cast<Instruction>(ResumeV)->getParent()->getFirstNonPHI());
              ResumeV = Builder.CreateICmpNE(ResumeV,
                                             RdxDesc.getRecurrenceStartValue());
            }
          } else {
            // Create induction resume values for both widened pointer and
            // integer/fp inductions and update the start value of the induction
            // recipes to use the resume value.
            PHINode *IndPhi = nullptr;
            const InductionDescriptor *ID;
            if (auto *Ind = dyn_cast<VPWidenPointerInductionRecipe>(&R)) {
              IndPhi = cast<PHINode>(Ind->getUnderlyingValue());
              ID = &Ind->getInductionDescriptor();
            } else {
              auto *WidenInd = cast<VPWidenIntOrFpInductionRecipe>(&R);
              IndPhi = WidenInd->getPHINode();
              ID = &WidenInd->getInductionDescriptor();
            }

            ResumeV = MainILV.createInductionResumeValue(
                IndPhi, *ID, getExpandedStep(*ID, ExpandedSCEVs),
                {EPI.MainLoopIterationCountCheck});
          }
          assert(ResumeV && "Must have a resume value");
          VPValue *StartVal = BestEpiPlan.getOrAddLiveIn(ResumeV);
          cast<VPHeaderPHIRecipe>(&R)->setStartValue(StartVal);
        }

        assert(DT->verify(DominatorTree::VerificationLevel::Fast) &&
               "DT not preserved correctly");
        LVP.executePlan(EPI.EpilogueVF, EPI.EpilogueUF, BestEpiPlan, EpilogILV,
                        DT, true, &ExpandedSCEVs);
        ++LoopsEpilogueVectorized;

        if (!MainILV.areSafetyChecksAdded())
          DisableRuntimeUnroll = true;
      } else {
        ElementCount Width = VF.Width;
        VPlan &BestPlan =
            UseLegacyCostModel ? LVP.getBestPlanFor(Width) : LVP.getBestPlan();
        if (!UseLegacyCostModel) {
          assert(size(BestPlan.vectorFactors()) == 1 &&
                 "Plan should have a single VF");
          Width = *BestPlan.vectorFactors().begin();
          LLVM_DEBUG(dbgs()
                     << "VF picked by VPlan cost model: " << Width << "\n");
          assert(VF.Width == Width &&
                 "VPlan cost model and legacy cost model disagreed");
        }
        InnerLoopVectorizer LB(L, PSE, LI, DT, TLI, TTI, AC, ORE, Width,
                               VF.MinProfitableTripCount, IC, &LVL, &CM, BFI,
                               PSI, Checks);
        LVP.executePlan(Width, IC, BestPlan, LB, DT, false);
        ++LoopsVectorized;

        // Add metadata to disable runtime unrolling a scalar loop when there
        // are no runtime checks about strides and memory. A scalar loop that is
        // rarely used is not worth unrolling.
        if (!LB.areSafetyChecksAdded())
          DisableRuntimeUnroll = true;
      }
      // Report the vectorization decision.
      reportVectorization(ORE, L, VF, IC);
    }

    if (ORE->allowExtraAnalysis(LV_NAME))
      checkMixedPrecision(L, ORE);
  }

  std::optional<MDNode *> RemainderLoopID =
      makeFollowupLoopID(OrigLoopID, {LLVMLoopVectorizeFollowupAll,
                                      LLVMLoopVectorizeFollowupEpilogue});
  if (RemainderLoopID) {
    L->setLoopID(*RemainderLoopID);
  } else {
    if (DisableRuntimeUnroll)
      AddRuntimeUnrollDisableMetaData(L);

    // Mark the loop as already vectorized to avoid vectorizing again.
    Hints.setAlreadyVectorized();
  }

  assert(!verifyFunction(*L->getHeader()->getParent(), &dbgs()));
  return true;
}

LoopVectorizeResult LoopVectorizePass::runImpl(
    Function &F, ScalarEvolution &SE_, LoopInfo &LI_, TargetTransformInfo &TTI_,
    DominatorTree &DT_, BlockFrequencyInfo *BFI_, TargetLibraryInfo *TLI_,
    DemandedBits &DB_, AssumptionCache &AC_, LoopAccessInfoManager &LAIs_,
    OptimizationRemarkEmitter &ORE_, ProfileSummaryInfo *PSI_) {
  SE = &SE_;
  LI = &LI_;
  TTI = &TTI_;
  DT = &DT_;
  BFI = BFI_;
  TLI = TLI_;
  AC = &AC_;
  LAIs = &LAIs_;
  DB = &DB_;
  ORE = &ORE_;
  PSI = PSI_;

  // Don't attempt if
  // 1. the target claims to have no vector registers, and
  // 2. interleaving won't help ILP.
  //
  // The second condition is necessary because, even if the target has no
  // vector registers, loop vectorization may still enable scalar
  // interleaving.
  if (!TTI->getNumberOfRegisters(TTI->getRegisterClassForType(true)) &&
      TTI->getMaxInterleaveFactor(ElementCount::getFixed(1)) < 2)
    return LoopVectorizeResult(false, false);

  bool Changed = false, CFGChanged = false;

  // The vectorizer requires loops to be in simplified form.
  // Since simplification may add new inner loops, it has to run before the
  // legality and profitability checks. This means running the loop vectorizer
  // will simplify all loops, regardless of whether anything end up being
  // vectorized.
  for (const auto &L : *LI)
    Changed |= CFGChanged |=
        simplifyLoop(L, DT, LI, SE, AC, nullptr, false /* PreserveLCSSA */);

  // Build up a worklist of inner-loops to vectorize. This is necessary as
  // the act of vectorizing or partially unrolling a loop creates new loops
  // and can invalidate iterators across the loops.
  SmallVector<Loop *, 8> Worklist;

  for (Loop *L : *LI)
    collectSupportedLoops(*L, LI, ORE, Worklist);

  LoopsAnalyzed += Worklist.size();

  // Now walk the identified inner loops.
  while (!Worklist.empty()) {
    Loop *L = Worklist.pop_back_val();

    // For the inner loops we actually process, form LCSSA to simplify the
    // transform.
    Changed |= formLCSSARecursively(*L, *DT, LI, SE);

    Changed |= CFGChanged |= processLoop(L);

    if (Changed) {
      LAIs->clear();

#ifndef NDEBUG
      if (VerifySCEV)
        SE->verify();
#endif
    }
  }

  // Process each loop nest in the function.
  return LoopVectorizeResult(Changed, CFGChanged);
}

PreservedAnalyses LoopVectorizePass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
    auto &LI = AM.getResult<LoopAnalysis>(F);
    // There are no loops in the function. Return before computing other expensive
    // analyses.
    if (LI.empty())
      return PreservedAnalyses::all();
    auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    auto &TTI = AM.getResult<TargetIRAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
    auto &AC = AM.getResult<AssumptionAnalysis>(F);
    auto &DB = AM.getResult<DemandedBitsAnalysis>(F);
    auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    LoopAccessInfoManager &LAIs = AM.getResult<LoopAccessAnalysis>(F);
    auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
    ProfileSummaryInfo *PSI =
        MAMProxy.getCachedResult<ProfileSummaryAnalysis>(*F.getParent());
    BlockFrequencyInfo *BFI = nullptr;
    if (PSI && PSI->hasProfileSummary())
      BFI = &AM.getResult<BlockFrequencyAnalysis>(F);
    LoopVectorizeResult Result =
        runImpl(F, SE, LI, TTI, DT, BFI, &TLI, DB, AC, LAIs, ORE, PSI);
    if (!Result.MadeAnyChange)
      return PreservedAnalyses::all();
    PreservedAnalyses PA;

    if (isAssignmentTrackingEnabled(*F.getParent())) {
      for (auto &BB : F)
        RemoveRedundantDbgInstrs(&BB);
    }

    PA.preserve<LoopAnalysis>();
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<LoopAccessAnalysis>();

    if (Result.MadeCFGChange) {
      // Making CFG changes likely means a loop got vectorized. Indicate that
      // extra simplification passes should be run.
      // TODO: MadeCFGChanges is not a prefect proxy. Extra passes should only
      // be run if runtime checks have been added.
      AM.getResult<ShouldRunExtraVectorPasses>(F);
      PA.preserve<ShouldRunExtraVectorPasses>();
    } else {
      PA.preserveSet<CFGAnalyses>();
    }
    return PA;
}

void LoopVectorizePass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<LoopVectorizePass> *>(this)->printPipeline(
      OS, MapClassName2PassName);

  OS << '<';
  OS << (InterleaveOnlyWhenForced ? "" : "no-") << "interleave-forced-only;";
  OS << (VectorizeOnlyWhenForced ? "" : "no-") << "vectorize-forced-only;";
  OS << '>';
}
