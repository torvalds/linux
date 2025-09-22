//===------- VectorCombine.cpp - Optimize partial vector operations -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass optimizes scalar/vector interactions using target cost models. The
// transforms implemented here may not fit in traditional loop-based or SLP
// vectorization passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/VectorCombine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <numeric>
#include <queue>

#define DEBUG_TYPE "vector-combine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

using namespace llvm;
using namespace llvm::PatternMatch;

STATISTIC(NumVecLoad, "Number of vector loads formed");
STATISTIC(NumVecCmp, "Number of vector compares formed");
STATISTIC(NumVecBO, "Number of vector binops formed");
STATISTIC(NumVecCmpBO, "Number of vector compare + binop formed");
STATISTIC(NumShufOfBitcast, "Number of shuffles moved after bitcast");
STATISTIC(NumScalarBO, "Number of scalar binops formed");
STATISTIC(NumScalarCmp, "Number of scalar compares formed");

static cl::opt<bool> DisableVectorCombine(
    "disable-vector-combine", cl::init(false), cl::Hidden,
    cl::desc("Disable all vector combine transforms"));

static cl::opt<bool> DisableBinopExtractShuffle(
    "disable-binop-extract-shuffle", cl::init(false), cl::Hidden,
    cl::desc("Disable binop extract to shuffle transforms"));

static cl::opt<unsigned> MaxInstrsToScan(
    "vector-combine-max-scan-instrs", cl::init(30), cl::Hidden,
    cl::desc("Max number of instructions to scan for vector combining."));

static const unsigned InvalidIndex = std::numeric_limits<unsigned>::max();

namespace {
class VectorCombine {
public:
  VectorCombine(Function &F, const TargetTransformInfo &TTI,
                const DominatorTree &DT, AAResults &AA, AssumptionCache &AC,
                const DataLayout *DL, bool TryEarlyFoldsOnly)
      : F(F), Builder(F.getContext()), TTI(TTI), DT(DT), AA(AA), AC(AC), DL(DL),
        TryEarlyFoldsOnly(TryEarlyFoldsOnly) {}

  bool run();

private:
  Function &F;
  IRBuilder<> Builder;
  const TargetTransformInfo &TTI;
  const DominatorTree &DT;
  AAResults &AA;
  AssumptionCache &AC;
  const DataLayout *DL;

  /// If true, only perform beneficial early IR transforms. Do not introduce new
  /// vector operations.
  bool TryEarlyFoldsOnly;

  InstructionWorklist Worklist;

  // TODO: Direct calls from the top-level "run" loop use a plain "Instruction"
  //       parameter. That should be updated to specific sub-classes because the
  //       run loop was changed to dispatch on opcode.
  bool vectorizeLoadInsert(Instruction &I);
  bool widenSubvectorLoad(Instruction &I);
  ExtractElementInst *getShuffleExtract(ExtractElementInst *Ext0,
                                        ExtractElementInst *Ext1,
                                        unsigned PreferredExtractIndex) const;
  bool isExtractExtractCheap(ExtractElementInst *Ext0, ExtractElementInst *Ext1,
                             const Instruction &I,
                             ExtractElementInst *&ConvertToShuffle,
                             unsigned PreferredExtractIndex);
  void foldExtExtCmp(ExtractElementInst *Ext0, ExtractElementInst *Ext1,
                     Instruction &I);
  void foldExtExtBinop(ExtractElementInst *Ext0, ExtractElementInst *Ext1,
                       Instruction &I);
  bool foldExtractExtract(Instruction &I);
  bool foldInsExtFNeg(Instruction &I);
  bool foldBitcastShuffle(Instruction &I);
  bool scalarizeBinopOrCmp(Instruction &I);
  bool scalarizeVPIntrinsic(Instruction &I);
  bool foldExtractedCmps(Instruction &I);
  bool foldSingleElementStore(Instruction &I);
  bool scalarizeLoadExtract(Instruction &I);
  bool foldShuffleOfBinops(Instruction &I);
  bool foldShuffleOfCastops(Instruction &I);
  bool foldShuffleOfShuffles(Instruction &I);
  bool foldShuffleToIdentity(Instruction &I);
  bool foldShuffleFromReductions(Instruction &I);
  bool foldCastFromReductions(Instruction &I);
  bool foldSelectShuffle(Instruction &I, bool FromReduction = false);

  void replaceValue(Value &Old, Value &New) {
    Old.replaceAllUsesWith(&New);
    if (auto *NewI = dyn_cast<Instruction>(&New)) {
      New.takeName(&Old);
      Worklist.pushUsersToWorkList(*NewI);
      Worklist.pushValue(NewI);
    }
    Worklist.pushValue(&Old);
  }

  void eraseInstruction(Instruction &I) {
    for (Value *Op : I.operands())
      Worklist.pushValue(Op);
    Worklist.remove(&I);
    I.eraseFromParent();
  }
};
} // namespace

/// Return the source operand of a potentially bitcasted value. If there is no
/// bitcast, return the input value itself.
static Value *peekThroughBitcasts(Value *V) {
  while (auto *BitCast = dyn_cast<BitCastInst>(V))
    V = BitCast->getOperand(0);
  return V;
}

static bool canWidenLoad(LoadInst *Load, const TargetTransformInfo &TTI) {
  // Do not widen load if atomic/volatile or under asan/hwasan/memtag/tsan.
  // The widened load may load data from dirty regions or create data races
  // non-existent in the source.
  if (!Load || !Load->isSimple() || !Load->hasOneUse() ||
      Load->getFunction()->hasFnAttribute(Attribute::SanitizeMemTag) ||
      mustSuppressSpeculation(*Load))
    return false;

  // We are potentially transforming byte-sized (8-bit) memory accesses, so make
  // sure we have all of our type-based constraints in place for this target.
  Type *ScalarTy = Load->getType()->getScalarType();
  uint64_t ScalarSize = ScalarTy->getPrimitiveSizeInBits();
  unsigned MinVectorSize = TTI.getMinVectorRegisterBitWidth();
  if (!ScalarSize || !MinVectorSize || MinVectorSize % ScalarSize != 0 ||
      ScalarSize % 8 != 0)
    return false;

  return true;
}

bool VectorCombine::vectorizeLoadInsert(Instruction &I) {
  // Match insert into fixed vector of scalar value.
  // TODO: Handle non-zero insert index.
  Value *Scalar;
  if (!match(&I, m_InsertElt(m_Undef(), m_Value(Scalar), m_ZeroInt())) ||
      !Scalar->hasOneUse())
    return false;

  // Optionally match an extract from another vector.
  Value *X;
  bool HasExtract = match(Scalar, m_ExtractElt(m_Value(X), m_ZeroInt()));
  if (!HasExtract)
    X = Scalar;

  auto *Load = dyn_cast<LoadInst>(X);
  if (!canWidenLoad(Load, TTI))
    return false;

  Type *ScalarTy = Scalar->getType();
  uint64_t ScalarSize = ScalarTy->getPrimitiveSizeInBits();
  unsigned MinVectorSize = TTI.getMinVectorRegisterBitWidth();

  // Check safety of replacing the scalar load with a larger vector load.
  // We use minimal alignment (maximum flexibility) because we only care about
  // the dereferenceable region. When calculating cost and creating a new op,
  // we may use a larger value based on alignment attributes.
  Value *SrcPtr = Load->getPointerOperand()->stripPointerCasts();
  assert(isa<PointerType>(SrcPtr->getType()) && "Expected a pointer type");

  unsigned MinVecNumElts = MinVectorSize / ScalarSize;
  auto *MinVecTy = VectorType::get(ScalarTy, MinVecNumElts, false);
  unsigned OffsetEltIndex = 0;
  Align Alignment = Load->getAlign();
  if (!isSafeToLoadUnconditionally(SrcPtr, MinVecTy, Align(1), *DL, Load, &AC,
                                   &DT)) {
    // It is not safe to load directly from the pointer, but we can still peek
    // through gep offsets and check if it safe to load from a base address with
    // updated alignment. If it is, we can shuffle the element(s) into place
    // after loading.
    unsigned OffsetBitWidth = DL->getIndexTypeSizeInBits(SrcPtr->getType());
    APInt Offset(OffsetBitWidth, 0);
    SrcPtr = SrcPtr->stripAndAccumulateInBoundsConstantOffsets(*DL, Offset);

    // We want to shuffle the result down from a high element of a vector, so
    // the offset must be positive.
    if (Offset.isNegative())
      return false;

    // The offset must be a multiple of the scalar element to shuffle cleanly
    // in the element's size.
    uint64_t ScalarSizeInBytes = ScalarSize / 8;
    if (Offset.urem(ScalarSizeInBytes) != 0)
      return false;

    // If we load MinVecNumElts, will our target element still be loaded?
    OffsetEltIndex = Offset.udiv(ScalarSizeInBytes).getZExtValue();
    if (OffsetEltIndex >= MinVecNumElts)
      return false;

    if (!isSafeToLoadUnconditionally(SrcPtr, MinVecTy, Align(1), *DL, Load, &AC,
                                     &DT))
      return false;

    // Update alignment with offset value. Note that the offset could be negated
    // to more accurately represent "(new) SrcPtr - Offset = (old) SrcPtr", but
    // negation does not change the result of the alignment calculation.
    Alignment = commonAlignment(Alignment, Offset.getZExtValue());
  }

  // Original pattern: insertelt undef, load [free casts of] PtrOp, 0
  // Use the greater of the alignment on the load or its source pointer.
  Alignment = std::max(SrcPtr->getPointerAlignment(*DL), Alignment);
  Type *LoadTy = Load->getType();
  unsigned AS = Load->getPointerAddressSpace();
  InstructionCost OldCost =
      TTI.getMemoryOpCost(Instruction::Load, LoadTy, Alignment, AS);
  APInt DemandedElts = APInt::getOneBitSet(MinVecNumElts, 0);
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  OldCost +=
      TTI.getScalarizationOverhead(MinVecTy, DemandedElts,
                                   /* Insert */ true, HasExtract, CostKind);

  // New pattern: load VecPtr
  InstructionCost NewCost =
      TTI.getMemoryOpCost(Instruction::Load, MinVecTy, Alignment, AS);
  // Optionally, we are shuffling the loaded vector element(s) into place.
  // For the mask set everything but element 0 to undef to prevent poison from
  // propagating from the extra loaded memory. This will also optionally
  // shrink/grow the vector from the loaded size to the output size.
  // We assume this operation has no cost in codegen if there was no offset.
  // Note that we could use freeze to avoid poison problems, but then we might
  // still need a shuffle to change the vector size.
  auto *Ty = cast<FixedVectorType>(I.getType());
  unsigned OutputNumElts = Ty->getNumElements();
  SmallVector<int, 16> Mask(OutputNumElts, PoisonMaskElem);
  assert(OffsetEltIndex < MinVecNumElts && "Address offset too big");
  Mask[0] = OffsetEltIndex;
  if (OffsetEltIndex)
    NewCost += TTI.getShuffleCost(TTI::SK_PermuteSingleSrc, MinVecTy, Mask);

  // We can aggressively convert to the vector form because the backend can
  // invert this transform if it does not result in a performance win.
  if (OldCost < NewCost || !NewCost.isValid())
    return false;

  // It is safe and potentially profitable to load a vector directly:
  // inselt undef, load Scalar, 0 --> load VecPtr
  IRBuilder<> Builder(Load);
  Value *CastedPtr =
      Builder.CreatePointerBitCastOrAddrSpaceCast(SrcPtr, Builder.getPtrTy(AS));
  Value *VecLd = Builder.CreateAlignedLoad(MinVecTy, CastedPtr, Alignment);
  VecLd = Builder.CreateShuffleVector(VecLd, Mask);

  replaceValue(I, *VecLd);
  ++NumVecLoad;
  return true;
}

/// If we are loading a vector and then inserting it into a larger vector with
/// undefined elements, try to load the larger vector and eliminate the insert.
/// This removes a shuffle in IR and may allow combining of other loaded values.
bool VectorCombine::widenSubvectorLoad(Instruction &I) {
  // Match subvector insert of fixed vector.
  auto *Shuf = cast<ShuffleVectorInst>(&I);
  if (!Shuf->isIdentityWithPadding())
    return false;

  // Allow a non-canonical shuffle mask that is choosing elements from op1.
  unsigned NumOpElts =
      cast<FixedVectorType>(Shuf->getOperand(0)->getType())->getNumElements();
  unsigned OpIndex = any_of(Shuf->getShuffleMask(), [&NumOpElts](int M) {
    return M >= (int)(NumOpElts);
  });

  auto *Load = dyn_cast<LoadInst>(Shuf->getOperand(OpIndex));
  if (!canWidenLoad(Load, TTI))
    return false;

  // We use minimal alignment (maximum flexibility) because we only care about
  // the dereferenceable region. When calculating cost and creating a new op,
  // we may use a larger value based on alignment attributes.
  auto *Ty = cast<FixedVectorType>(I.getType());
  Value *SrcPtr = Load->getPointerOperand()->stripPointerCasts();
  assert(isa<PointerType>(SrcPtr->getType()) && "Expected a pointer type");
  Align Alignment = Load->getAlign();
  if (!isSafeToLoadUnconditionally(SrcPtr, Ty, Align(1), *DL, Load, &AC, &DT))
    return false;

  Alignment = std::max(SrcPtr->getPointerAlignment(*DL), Alignment);
  Type *LoadTy = Load->getType();
  unsigned AS = Load->getPointerAddressSpace();

  // Original pattern: insert_subvector (load PtrOp)
  // This conservatively assumes that the cost of a subvector insert into an
  // undef value is 0. We could add that cost if the cost model accurately
  // reflects the real cost of that operation.
  InstructionCost OldCost =
      TTI.getMemoryOpCost(Instruction::Load, LoadTy, Alignment, AS);

  // New pattern: load PtrOp
  InstructionCost NewCost =
      TTI.getMemoryOpCost(Instruction::Load, Ty, Alignment, AS);

  // We can aggressively convert to the vector form because the backend can
  // invert this transform if it does not result in a performance win.
  if (OldCost < NewCost || !NewCost.isValid())
    return false;

  IRBuilder<> Builder(Load);
  Value *CastedPtr =
      Builder.CreatePointerBitCastOrAddrSpaceCast(SrcPtr, Builder.getPtrTy(AS));
  Value *VecLd = Builder.CreateAlignedLoad(Ty, CastedPtr, Alignment);
  replaceValue(I, *VecLd);
  ++NumVecLoad;
  return true;
}

/// Determine which, if any, of the inputs should be replaced by a shuffle
/// followed by extract from a different index.
ExtractElementInst *VectorCombine::getShuffleExtract(
    ExtractElementInst *Ext0, ExtractElementInst *Ext1,
    unsigned PreferredExtractIndex = InvalidIndex) const {
  auto *Index0C = dyn_cast<ConstantInt>(Ext0->getIndexOperand());
  auto *Index1C = dyn_cast<ConstantInt>(Ext1->getIndexOperand());
  assert(Index0C && Index1C && "Expected constant extract indexes");

  unsigned Index0 = Index0C->getZExtValue();
  unsigned Index1 = Index1C->getZExtValue();

  // If the extract indexes are identical, no shuffle is needed.
  if (Index0 == Index1)
    return nullptr;

  Type *VecTy = Ext0->getVectorOperand()->getType();
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  assert(VecTy == Ext1->getVectorOperand()->getType() && "Need matching types");
  InstructionCost Cost0 =
      TTI.getVectorInstrCost(*Ext0, VecTy, CostKind, Index0);
  InstructionCost Cost1 =
      TTI.getVectorInstrCost(*Ext1, VecTy, CostKind, Index1);

  // If both costs are invalid no shuffle is needed
  if (!Cost0.isValid() && !Cost1.isValid())
    return nullptr;

  // We are extracting from 2 different indexes, so one operand must be shuffled
  // before performing a vector operation and/or extract. The more expensive
  // extract will be replaced by a shuffle.
  if (Cost0 > Cost1)
    return Ext0;
  if (Cost1 > Cost0)
    return Ext1;

  // If the costs are equal and there is a preferred extract index, shuffle the
  // opposite operand.
  if (PreferredExtractIndex == Index0)
    return Ext1;
  if (PreferredExtractIndex == Index1)
    return Ext0;

  // Otherwise, replace the extract with the higher index.
  return Index0 > Index1 ? Ext0 : Ext1;
}

/// Compare the relative costs of 2 extracts followed by scalar operation vs.
/// vector operation(s) followed by extract. Return true if the existing
/// instructions are cheaper than a vector alternative. Otherwise, return false
/// and if one of the extracts should be transformed to a shufflevector, set
/// \p ConvertToShuffle to that extract instruction.
bool VectorCombine::isExtractExtractCheap(ExtractElementInst *Ext0,
                                          ExtractElementInst *Ext1,
                                          const Instruction &I,
                                          ExtractElementInst *&ConvertToShuffle,
                                          unsigned PreferredExtractIndex) {
  auto *Ext0IndexC = dyn_cast<ConstantInt>(Ext0->getOperand(1));
  auto *Ext1IndexC = dyn_cast<ConstantInt>(Ext1->getOperand(1));
  assert(Ext0IndexC && Ext1IndexC && "Expected constant extract indexes");

  unsigned Opcode = I.getOpcode();
  Type *ScalarTy = Ext0->getType();
  auto *VecTy = cast<VectorType>(Ext0->getOperand(0)->getType());
  InstructionCost ScalarOpCost, VectorOpCost;

  // Get cost estimates for scalar and vector versions of the operation.
  bool IsBinOp = Instruction::isBinaryOp(Opcode);
  if (IsBinOp) {
    ScalarOpCost = TTI.getArithmeticInstrCost(Opcode, ScalarTy);
    VectorOpCost = TTI.getArithmeticInstrCost(Opcode, VecTy);
  } else {
    assert((Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) &&
           "Expected a compare");
    CmpInst::Predicate Pred = cast<CmpInst>(I).getPredicate();
    ScalarOpCost = TTI.getCmpSelInstrCost(
        Opcode, ScalarTy, CmpInst::makeCmpResultType(ScalarTy), Pred);
    VectorOpCost = TTI.getCmpSelInstrCost(
        Opcode, VecTy, CmpInst::makeCmpResultType(VecTy), Pred);
  }

  // Get cost estimates for the extract elements. These costs will factor into
  // both sequences.
  unsigned Ext0Index = Ext0IndexC->getZExtValue();
  unsigned Ext1Index = Ext1IndexC->getZExtValue();
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  InstructionCost Extract0Cost =
      TTI.getVectorInstrCost(*Ext0, VecTy, CostKind, Ext0Index);
  InstructionCost Extract1Cost =
      TTI.getVectorInstrCost(*Ext1, VecTy, CostKind, Ext1Index);

  // A more expensive extract will always be replaced by a splat shuffle.
  // For example, if Ext0 is more expensive:
  // opcode (extelt V0, Ext0), (ext V1, Ext1) -->
  // extelt (opcode (splat V0, Ext0), V1), Ext1
  // TODO: Evaluate whether that always results in lowest cost. Alternatively,
  //       check the cost of creating a broadcast shuffle and shuffling both
  //       operands to element 0.
  InstructionCost CheapExtractCost = std::min(Extract0Cost, Extract1Cost);

  // Extra uses of the extracts mean that we include those costs in the
  // vector total because those instructions will not be eliminated.
  InstructionCost OldCost, NewCost;
  if (Ext0->getOperand(0) == Ext1->getOperand(0) && Ext0Index == Ext1Index) {
    // Handle a special case. If the 2 extracts are identical, adjust the
    // formulas to account for that. The extra use charge allows for either the
    // CSE'd pattern or an unoptimized form with identical values:
    // opcode (extelt V, C), (extelt V, C) --> extelt (opcode V, V), C
    bool HasUseTax = Ext0 == Ext1 ? !Ext0->hasNUses(2)
                                  : !Ext0->hasOneUse() || !Ext1->hasOneUse();
    OldCost = CheapExtractCost + ScalarOpCost;
    NewCost = VectorOpCost + CheapExtractCost + HasUseTax * CheapExtractCost;
  } else {
    // Handle the general case. Each extract is actually a different value:
    // opcode (extelt V0, C0), (extelt V1, C1) --> extelt (opcode V0, V1), C
    OldCost = Extract0Cost + Extract1Cost + ScalarOpCost;
    NewCost = VectorOpCost + CheapExtractCost +
              !Ext0->hasOneUse() * Extract0Cost +
              !Ext1->hasOneUse() * Extract1Cost;
  }

  ConvertToShuffle = getShuffleExtract(Ext0, Ext1, PreferredExtractIndex);
  if (ConvertToShuffle) {
    if (IsBinOp && DisableBinopExtractShuffle)
      return true;

    // If we are extracting from 2 different indexes, then one operand must be
    // shuffled before performing the vector operation. The shuffle mask is
    // poison except for 1 lane that is being translated to the remaining
    // extraction lane. Therefore, it is a splat shuffle. Ex:
    // ShufMask = { poison, poison, 0, poison }
    // TODO: The cost model has an option for a "broadcast" shuffle
    //       (splat-from-element-0), but no option for a more general splat.
    NewCost +=
        TTI.getShuffleCost(TargetTransformInfo::SK_PermuteSingleSrc, VecTy);
  }

  // Aggressively form a vector op if the cost is equal because the transform
  // may enable further optimization.
  // Codegen can reverse this transform (scalarize) if it was not profitable.
  return OldCost < NewCost;
}

/// Create a shuffle that translates (shifts) 1 element from the input vector
/// to a new element location.
static Value *createShiftShuffle(Value *Vec, unsigned OldIndex,
                                 unsigned NewIndex, IRBuilder<> &Builder) {
  // The shuffle mask is poison except for 1 lane that is being translated
  // to the new element index. Example for OldIndex == 2 and NewIndex == 0:
  // ShufMask = { 2, poison, poison, poison }
  auto *VecTy = cast<FixedVectorType>(Vec->getType());
  SmallVector<int, 32> ShufMask(VecTy->getNumElements(), PoisonMaskElem);
  ShufMask[NewIndex] = OldIndex;
  return Builder.CreateShuffleVector(Vec, ShufMask, "shift");
}

/// Given an extract element instruction with constant index operand, shuffle
/// the source vector (shift the scalar element) to a NewIndex for extraction.
/// Return null if the input can be constant folded, so that we are not creating
/// unnecessary instructions.
static ExtractElementInst *translateExtract(ExtractElementInst *ExtElt,
                                            unsigned NewIndex,
                                            IRBuilder<> &Builder) {
  // Shufflevectors can only be created for fixed-width vectors.
  if (!isa<FixedVectorType>(ExtElt->getOperand(0)->getType()))
    return nullptr;

  // If the extract can be constant-folded, this code is unsimplified. Defer
  // to other passes to handle that.
  Value *X = ExtElt->getVectorOperand();
  Value *C = ExtElt->getIndexOperand();
  assert(isa<ConstantInt>(C) && "Expected a constant index operand");
  if (isa<Constant>(X))
    return nullptr;

  Value *Shuf = createShiftShuffle(X, cast<ConstantInt>(C)->getZExtValue(),
                                   NewIndex, Builder);
  return cast<ExtractElementInst>(Builder.CreateExtractElement(Shuf, NewIndex));
}

/// Try to reduce extract element costs by converting scalar compares to vector
/// compares followed by extract.
/// cmp (ext0 V0, C), (ext1 V1, C)
void VectorCombine::foldExtExtCmp(ExtractElementInst *Ext0,
                                  ExtractElementInst *Ext1, Instruction &I) {
  assert(isa<CmpInst>(&I) && "Expected a compare");
  assert(cast<ConstantInt>(Ext0->getIndexOperand())->getZExtValue() ==
             cast<ConstantInt>(Ext1->getIndexOperand())->getZExtValue() &&
         "Expected matching constant extract indexes");

  // cmp Pred (extelt V0, C), (extelt V1, C) --> extelt (cmp Pred V0, V1), C
  ++NumVecCmp;
  CmpInst::Predicate Pred = cast<CmpInst>(&I)->getPredicate();
  Value *V0 = Ext0->getVectorOperand(), *V1 = Ext1->getVectorOperand();
  Value *VecCmp = Builder.CreateCmp(Pred, V0, V1);
  Value *NewExt = Builder.CreateExtractElement(VecCmp, Ext0->getIndexOperand());
  replaceValue(I, *NewExt);
}

/// Try to reduce extract element costs by converting scalar binops to vector
/// binops followed by extract.
/// bo (ext0 V0, C), (ext1 V1, C)
void VectorCombine::foldExtExtBinop(ExtractElementInst *Ext0,
                                    ExtractElementInst *Ext1, Instruction &I) {
  assert(isa<BinaryOperator>(&I) && "Expected a binary operator");
  assert(cast<ConstantInt>(Ext0->getIndexOperand())->getZExtValue() ==
             cast<ConstantInt>(Ext1->getIndexOperand())->getZExtValue() &&
         "Expected matching constant extract indexes");

  // bo (extelt V0, C), (extelt V1, C) --> extelt (bo V0, V1), C
  ++NumVecBO;
  Value *V0 = Ext0->getVectorOperand(), *V1 = Ext1->getVectorOperand();
  Value *VecBO =
      Builder.CreateBinOp(cast<BinaryOperator>(&I)->getOpcode(), V0, V1);

  // All IR flags are safe to back-propagate because any potential poison
  // created in unused vector elements is discarded by the extract.
  if (auto *VecBOInst = dyn_cast<Instruction>(VecBO))
    VecBOInst->copyIRFlags(&I);

  Value *NewExt = Builder.CreateExtractElement(VecBO, Ext0->getIndexOperand());
  replaceValue(I, *NewExt);
}

/// Match an instruction with extracted vector operands.
bool VectorCombine::foldExtractExtract(Instruction &I) {
  // It is not safe to transform things like div, urem, etc. because we may
  // create undefined behavior when executing those on unknown vector elements.
  if (!isSafeToSpeculativelyExecute(&I))
    return false;

  Instruction *I0, *I1;
  CmpInst::Predicate Pred = CmpInst::BAD_ICMP_PREDICATE;
  if (!match(&I, m_Cmp(Pred, m_Instruction(I0), m_Instruction(I1))) &&
      !match(&I, m_BinOp(m_Instruction(I0), m_Instruction(I1))))
    return false;

  Value *V0, *V1;
  uint64_t C0, C1;
  if (!match(I0, m_ExtractElt(m_Value(V0), m_ConstantInt(C0))) ||
      !match(I1, m_ExtractElt(m_Value(V1), m_ConstantInt(C1))) ||
      V0->getType() != V1->getType())
    return false;

  // If the scalar value 'I' is going to be re-inserted into a vector, then try
  // to create an extract to that same element. The extract/insert can be
  // reduced to a "select shuffle".
  // TODO: If we add a larger pattern match that starts from an insert, this
  //       probably becomes unnecessary.
  auto *Ext0 = cast<ExtractElementInst>(I0);
  auto *Ext1 = cast<ExtractElementInst>(I1);
  uint64_t InsertIndex = InvalidIndex;
  if (I.hasOneUse())
    match(I.user_back(),
          m_InsertElt(m_Value(), m_Value(), m_ConstantInt(InsertIndex)));

  ExtractElementInst *ExtractToChange;
  if (isExtractExtractCheap(Ext0, Ext1, I, ExtractToChange, InsertIndex))
    return false;

  if (ExtractToChange) {
    unsigned CheapExtractIdx = ExtractToChange == Ext0 ? C1 : C0;
    ExtractElementInst *NewExtract =
        translateExtract(ExtractToChange, CheapExtractIdx, Builder);
    if (!NewExtract)
      return false;
    if (ExtractToChange == Ext0)
      Ext0 = NewExtract;
    else
      Ext1 = NewExtract;
  }

  if (Pred != CmpInst::BAD_ICMP_PREDICATE)
    foldExtExtCmp(Ext0, Ext1, I);
  else
    foldExtExtBinop(Ext0, Ext1, I);

  Worklist.push(Ext0);
  Worklist.push(Ext1);
  return true;
}

/// Try to replace an extract + scalar fneg + insert with a vector fneg +
/// shuffle.
bool VectorCombine::foldInsExtFNeg(Instruction &I) {
  // Match an insert (op (extract)) pattern.
  Value *DestVec;
  uint64_t Index;
  Instruction *FNeg;
  if (!match(&I, m_InsertElt(m_Value(DestVec), m_OneUse(m_Instruction(FNeg)),
                             m_ConstantInt(Index))))
    return false;

  // Note: This handles the canonical fneg instruction and "fsub -0.0, X".
  Value *SrcVec;
  Instruction *Extract;
  if (!match(FNeg, m_FNeg(m_CombineAnd(
                       m_Instruction(Extract),
                       m_ExtractElt(m_Value(SrcVec), m_SpecificInt(Index))))))
    return false;

  // TODO: We could handle this with a length-changing shuffle.
  auto *VecTy = cast<FixedVectorType>(I.getType());
  if (SrcVec->getType() != VecTy)
    return false;

  // Ignore bogus insert/extract index.
  unsigned NumElts = VecTy->getNumElements();
  if (Index >= NumElts)
    return false;

  // We are inserting the negated element into the same lane that we extracted
  // from. This is equivalent to a select-shuffle that chooses all but the
  // negated element from the destination vector.
  SmallVector<int> Mask(NumElts);
  std::iota(Mask.begin(), Mask.end(), 0);
  Mask[Index] = Index + NumElts;

  Type *ScalarTy = VecTy->getScalarType();
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  InstructionCost OldCost =
      TTI.getArithmeticInstrCost(Instruction::FNeg, ScalarTy) +
      TTI.getVectorInstrCost(I, VecTy, CostKind, Index);

  // If the extract has one use, it will be eliminated, so count it in the
  // original cost. If it has more than one use, ignore the cost because it will
  // be the same before/after.
  if (Extract->hasOneUse())
    OldCost += TTI.getVectorInstrCost(*Extract, VecTy, CostKind, Index);

  InstructionCost NewCost =
      TTI.getArithmeticInstrCost(Instruction::FNeg, VecTy) +
      TTI.getShuffleCost(TargetTransformInfo::SK_Select, VecTy, Mask);

  if (NewCost > OldCost)
    return false;

  // insertelt DestVec, (fneg (extractelt SrcVec, Index)), Index -->
  // shuffle DestVec, (fneg SrcVec), Mask
  Value *VecFNeg = Builder.CreateFNegFMF(SrcVec, FNeg);
  Value *Shuf = Builder.CreateShuffleVector(DestVec, VecFNeg, Mask);
  replaceValue(I, *Shuf);
  return true;
}

/// If this is a bitcast of a shuffle, try to bitcast the source vector to the
/// destination type followed by shuffle. This can enable further transforms by
/// moving bitcasts or shuffles together.
bool VectorCombine::foldBitcastShuffle(Instruction &I) {
  Value *V0, *V1;
  ArrayRef<int> Mask;
  if (!match(&I, m_BitCast(m_OneUse(
                     m_Shuffle(m_Value(V0), m_Value(V1), m_Mask(Mask))))))
    return false;

  // 1) Do not fold bitcast shuffle for scalable type. First, shuffle cost for
  // scalable type is unknown; Second, we cannot reason if the narrowed shuffle
  // mask for scalable type is a splat or not.
  // 2) Disallow non-vector casts.
  // TODO: We could allow any shuffle.
  auto *DestTy = dyn_cast<FixedVectorType>(I.getType());
  auto *SrcTy = dyn_cast<FixedVectorType>(V0->getType());
  if (!DestTy || !SrcTy)
    return false;

  unsigned DestEltSize = DestTy->getScalarSizeInBits();
  unsigned SrcEltSize = SrcTy->getScalarSizeInBits();
  if (SrcTy->getPrimitiveSizeInBits() % DestEltSize != 0)
    return false;

  bool IsUnary = isa<UndefValue>(V1);

  // For binary shuffles, only fold bitcast(shuffle(X,Y))
  // if it won't increase the number of bitcasts.
  if (!IsUnary) {
    auto *BCTy0 = dyn_cast<FixedVectorType>(peekThroughBitcasts(V0)->getType());
    auto *BCTy1 = dyn_cast<FixedVectorType>(peekThroughBitcasts(V1)->getType());
    if (!(BCTy0 && BCTy0->getElementType() == DestTy->getElementType()) &&
        !(BCTy1 && BCTy1->getElementType() == DestTy->getElementType()))
      return false;
  }

  SmallVector<int, 16> NewMask;
  if (DestEltSize <= SrcEltSize) {
    // The bitcast is from wide to narrow/equal elements. The shuffle mask can
    // always be expanded to the equivalent form choosing narrower elements.
    assert(SrcEltSize % DestEltSize == 0 && "Unexpected shuffle mask");
    unsigned ScaleFactor = SrcEltSize / DestEltSize;
    narrowShuffleMaskElts(ScaleFactor, Mask, NewMask);
  } else {
    // The bitcast is from narrow elements to wide elements. The shuffle mask
    // must choose consecutive elements to allow casting first.
    assert(DestEltSize % SrcEltSize == 0 && "Unexpected shuffle mask");
    unsigned ScaleFactor = DestEltSize / SrcEltSize;
    if (!widenShuffleMaskElts(ScaleFactor, Mask, NewMask))
      return false;
  }

  // Bitcast the shuffle src - keep its original width but using the destination
  // scalar type.
  unsigned NumSrcElts = SrcTy->getPrimitiveSizeInBits() / DestEltSize;
  auto *NewShuffleTy =
      FixedVectorType::get(DestTy->getScalarType(), NumSrcElts);
  auto *OldShuffleTy =
      FixedVectorType::get(SrcTy->getScalarType(), Mask.size());
  unsigned NumOps = IsUnary ? 1 : 2;

  // The new shuffle must not cost more than the old shuffle.
  TargetTransformInfo::TargetCostKind CK =
      TargetTransformInfo::TCK_RecipThroughput;
  TargetTransformInfo::ShuffleKind SK =
      IsUnary ? TargetTransformInfo::SK_PermuteSingleSrc
              : TargetTransformInfo::SK_PermuteTwoSrc;

  InstructionCost DestCost =
      TTI.getShuffleCost(SK, NewShuffleTy, NewMask, CK) +
      (NumOps * TTI.getCastInstrCost(Instruction::BitCast, NewShuffleTy, SrcTy,
                                     TargetTransformInfo::CastContextHint::None,
                                     CK));
  InstructionCost SrcCost =
      TTI.getShuffleCost(SK, SrcTy, Mask, CK) +
      TTI.getCastInstrCost(Instruction::BitCast, DestTy, OldShuffleTy,
                           TargetTransformInfo::CastContextHint::None, CK);
  if (DestCost > SrcCost || !DestCost.isValid())
    return false;

  // bitcast (shuf V0, V1, MaskC) --> shuf (bitcast V0), (bitcast V1), MaskC'
  ++NumShufOfBitcast;
  Value *CastV0 = Builder.CreateBitCast(peekThroughBitcasts(V0), NewShuffleTy);
  Value *CastV1 = Builder.CreateBitCast(peekThroughBitcasts(V1), NewShuffleTy);
  Value *Shuf = Builder.CreateShuffleVector(CastV0, CastV1, NewMask);
  replaceValue(I, *Shuf);
  return true;
}

/// VP Intrinsics whose vector operands are both splat values may be simplified
/// into the scalar version of the operation and the result splatted. This
/// can lead to scalarization down the line.
bool VectorCombine::scalarizeVPIntrinsic(Instruction &I) {
  if (!isa<VPIntrinsic>(I))
    return false;
  VPIntrinsic &VPI = cast<VPIntrinsic>(I);
  Value *Op0 = VPI.getArgOperand(0);
  Value *Op1 = VPI.getArgOperand(1);

  if (!isSplatValue(Op0) || !isSplatValue(Op1))
    return false;

  // Check getSplatValue early in this function, to avoid doing unnecessary
  // work.
  Value *ScalarOp0 = getSplatValue(Op0);
  Value *ScalarOp1 = getSplatValue(Op1);
  if (!ScalarOp0 || !ScalarOp1)
    return false;

  // For the binary VP intrinsics supported here, the result on disabled lanes
  // is a poison value. For now, only do this simplification if all lanes
  // are active.
  // TODO: Relax the condition that all lanes are active by using insertelement
  // on inactive lanes.
  auto IsAllTrueMask = [](Value *MaskVal) {
    if (Value *SplattedVal = getSplatValue(MaskVal))
      if (auto *ConstValue = dyn_cast<Constant>(SplattedVal))
        return ConstValue->isAllOnesValue();
    return false;
  };
  if (!IsAllTrueMask(VPI.getArgOperand(2)))
    return false;

  // Check to make sure we support scalarization of the intrinsic
  Intrinsic::ID IntrID = VPI.getIntrinsicID();
  if (!VPBinOpIntrinsic::isVPBinOp(IntrID))
    return false;

  // Calculate cost of splatting both operands into vectors and the vector
  // intrinsic
  VectorType *VecTy = cast<VectorType>(VPI.getType());
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  SmallVector<int> Mask;
  if (auto *FVTy = dyn_cast<FixedVectorType>(VecTy))
    Mask.resize(FVTy->getNumElements(), 0);
  InstructionCost SplatCost =
      TTI.getVectorInstrCost(Instruction::InsertElement, VecTy, CostKind, 0) +
      TTI.getShuffleCost(TargetTransformInfo::SK_Broadcast, VecTy, Mask);

  // Calculate the cost of the VP Intrinsic
  SmallVector<Type *, 4> Args;
  for (Value *V : VPI.args())
    Args.push_back(V->getType());
  IntrinsicCostAttributes Attrs(IntrID, VecTy, Args);
  InstructionCost VectorOpCost = TTI.getIntrinsicInstrCost(Attrs, CostKind);
  InstructionCost OldCost = 2 * SplatCost + VectorOpCost;

  // Determine scalar opcode
  std::optional<unsigned> FunctionalOpcode =
      VPI.getFunctionalOpcode();
  std::optional<Intrinsic::ID> ScalarIntrID = std::nullopt;
  if (!FunctionalOpcode) {
    ScalarIntrID = VPI.getFunctionalIntrinsicID();
    if (!ScalarIntrID)
      return false;
  }

  // Calculate cost of scalarizing
  InstructionCost ScalarOpCost = 0;
  if (ScalarIntrID) {
    IntrinsicCostAttributes Attrs(*ScalarIntrID, VecTy->getScalarType(), Args);
    ScalarOpCost = TTI.getIntrinsicInstrCost(Attrs, CostKind);
  } else {
    ScalarOpCost =
        TTI.getArithmeticInstrCost(*FunctionalOpcode, VecTy->getScalarType());
  }

  // The existing splats may be kept around if other instructions use them.
  InstructionCost CostToKeepSplats =
      (SplatCost * !Op0->hasOneUse()) + (SplatCost * !Op1->hasOneUse());
  InstructionCost NewCost = ScalarOpCost + SplatCost + CostToKeepSplats;

  LLVM_DEBUG(dbgs() << "Found a VP Intrinsic to scalarize: " << VPI
                    << "\n");
  LLVM_DEBUG(dbgs() << "Cost of Intrinsic: " << OldCost
                    << ", Cost of scalarizing:" << NewCost << "\n");

  // We want to scalarize unless the vector variant actually has lower cost.
  if (OldCost < NewCost || !NewCost.isValid())
    return false;

  // Scalarize the intrinsic
  ElementCount EC = cast<VectorType>(Op0->getType())->getElementCount();
  Value *EVL = VPI.getArgOperand(3);

  // If the VP op might introduce UB or poison, we can scalarize it provided
  // that we know the EVL > 0: If the EVL is zero, then the original VP op
  // becomes a no-op and thus won't be UB, so make sure we don't introduce UB by
  // scalarizing it.
  bool SafeToSpeculate;
  if (ScalarIntrID)
    SafeToSpeculate = Intrinsic::getAttributes(I.getContext(), *ScalarIntrID)
                          .hasFnAttr(Attribute::AttrKind::Speculatable);
  else
    SafeToSpeculate = isSafeToSpeculativelyExecuteWithOpcode(
        *FunctionalOpcode, &VPI, nullptr, &AC, &DT);
  if (!SafeToSpeculate &&
      !isKnownNonZero(EVL, SimplifyQuery(*DL, &DT, &AC, &VPI)))
    return false;

  Value *ScalarVal =
      ScalarIntrID
          ? Builder.CreateIntrinsic(VecTy->getScalarType(), *ScalarIntrID,
                                    {ScalarOp0, ScalarOp1})
          : Builder.CreateBinOp((Instruction::BinaryOps)(*FunctionalOpcode),
                                ScalarOp0, ScalarOp1);

  replaceValue(VPI, *Builder.CreateVectorSplat(EC, ScalarVal));
  return true;
}

/// Match a vector binop or compare instruction with at least one inserted
/// scalar operand and convert to scalar binop/cmp followed by insertelement.
bool VectorCombine::scalarizeBinopOrCmp(Instruction &I) {
  CmpInst::Predicate Pred = CmpInst::BAD_ICMP_PREDICATE;
  Value *Ins0, *Ins1;
  if (!match(&I, m_BinOp(m_Value(Ins0), m_Value(Ins1))) &&
      !match(&I, m_Cmp(Pred, m_Value(Ins0), m_Value(Ins1))))
    return false;

  // Do not convert the vector condition of a vector select into a scalar
  // condition. That may cause problems for codegen because of differences in
  // boolean formats and register-file transfers.
  // TODO: Can we account for that in the cost model?
  bool IsCmp = Pred != CmpInst::Predicate::BAD_ICMP_PREDICATE;
  if (IsCmp)
    for (User *U : I.users())
      if (match(U, m_Select(m_Specific(&I), m_Value(), m_Value())))
        return false;

  // Match against one or both scalar values being inserted into constant
  // vectors:
  // vec_op VecC0, (inselt VecC1, V1, Index)
  // vec_op (inselt VecC0, V0, Index), VecC1
  // vec_op (inselt VecC0, V0, Index), (inselt VecC1, V1, Index)
  // TODO: Deal with mismatched index constants and variable indexes?
  Constant *VecC0 = nullptr, *VecC1 = nullptr;
  Value *V0 = nullptr, *V1 = nullptr;
  uint64_t Index0 = 0, Index1 = 0;
  if (!match(Ins0, m_InsertElt(m_Constant(VecC0), m_Value(V0),
                               m_ConstantInt(Index0))) &&
      !match(Ins0, m_Constant(VecC0)))
    return false;
  if (!match(Ins1, m_InsertElt(m_Constant(VecC1), m_Value(V1),
                               m_ConstantInt(Index1))) &&
      !match(Ins1, m_Constant(VecC1)))
    return false;

  bool IsConst0 = !V0;
  bool IsConst1 = !V1;
  if (IsConst0 && IsConst1)
    return false;
  if (!IsConst0 && !IsConst1 && Index0 != Index1)
    return false;

  // Bail for single insertion if it is a load.
  // TODO: Handle this once getVectorInstrCost can cost for load/stores.
  auto *I0 = dyn_cast_or_null<Instruction>(V0);
  auto *I1 = dyn_cast_or_null<Instruction>(V1);
  if ((IsConst0 && I1 && I1->mayReadFromMemory()) ||
      (IsConst1 && I0 && I0->mayReadFromMemory()))
    return false;

  uint64_t Index = IsConst0 ? Index1 : Index0;
  Type *ScalarTy = IsConst0 ? V1->getType() : V0->getType();
  Type *VecTy = I.getType();
  assert(VecTy->isVectorTy() &&
         (IsConst0 || IsConst1 || V0->getType() == V1->getType()) &&
         (ScalarTy->isIntegerTy() || ScalarTy->isFloatingPointTy() ||
          ScalarTy->isPointerTy()) &&
         "Unexpected types for insert element into binop or cmp");

  unsigned Opcode = I.getOpcode();
  InstructionCost ScalarOpCost, VectorOpCost;
  if (IsCmp) {
    CmpInst::Predicate Pred = cast<CmpInst>(I).getPredicate();
    ScalarOpCost = TTI.getCmpSelInstrCost(
        Opcode, ScalarTy, CmpInst::makeCmpResultType(ScalarTy), Pred);
    VectorOpCost = TTI.getCmpSelInstrCost(
        Opcode, VecTy, CmpInst::makeCmpResultType(VecTy), Pred);
  } else {
    ScalarOpCost = TTI.getArithmeticInstrCost(Opcode, ScalarTy);
    VectorOpCost = TTI.getArithmeticInstrCost(Opcode, VecTy);
  }

  // Get cost estimate for the insert element. This cost will factor into
  // both sequences.
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  InstructionCost InsertCost = TTI.getVectorInstrCost(
      Instruction::InsertElement, VecTy, CostKind, Index);
  InstructionCost OldCost =
      (IsConst0 ? 0 : InsertCost) + (IsConst1 ? 0 : InsertCost) + VectorOpCost;
  InstructionCost NewCost = ScalarOpCost + InsertCost +
                            (IsConst0 ? 0 : !Ins0->hasOneUse() * InsertCost) +
                            (IsConst1 ? 0 : !Ins1->hasOneUse() * InsertCost);

  // We want to scalarize unless the vector variant actually has lower cost.
  if (OldCost < NewCost || !NewCost.isValid())
    return false;

  // vec_op (inselt VecC0, V0, Index), (inselt VecC1, V1, Index) -->
  // inselt NewVecC, (scalar_op V0, V1), Index
  if (IsCmp)
    ++NumScalarCmp;
  else
    ++NumScalarBO;

  // For constant cases, extract the scalar element, this should constant fold.
  if (IsConst0)
    V0 = ConstantExpr::getExtractElement(VecC0, Builder.getInt64(Index));
  if (IsConst1)
    V1 = ConstantExpr::getExtractElement(VecC1, Builder.getInt64(Index));

  Value *Scalar =
      IsCmp ? Builder.CreateCmp(Pred, V0, V1)
            : Builder.CreateBinOp((Instruction::BinaryOps)Opcode, V0, V1);

  Scalar->setName(I.getName() + ".scalar");

  // All IR flags are safe to back-propagate. There is no potential for extra
  // poison to be created by the scalar instruction.
  if (auto *ScalarInst = dyn_cast<Instruction>(Scalar))
    ScalarInst->copyIRFlags(&I);

  // Fold the vector constants in the original vectors into a new base vector.
  Value *NewVecC =
      IsCmp ? Builder.CreateCmp(Pred, VecC0, VecC1)
            : Builder.CreateBinOp((Instruction::BinaryOps)Opcode, VecC0, VecC1);
  Value *Insert = Builder.CreateInsertElement(NewVecC, Scalar, Index);
  replaceValue(I, *Insert);
  return true;
}

/// Try to combine a scalar binop + 2 scalar compares of extracted elements of
/// a vector into vector operations followed by extract. Note: The SLP pass
/// may miss this pattern because of implementation problems.
bool VectorCombine::foldExtractedCmps(Instruction &I) {
  // We are looking for a scalar binop of booleans.
  // binop i1 (cmp Pred I0, C0), (cmp Pred I1, C1)
  if (!I.isBinaryOp() || !I.getType()->isIntegerTy(1))
    return false;

  // The compare predicates should match, and each compare should have a
  // constant operand.
  // TODO: Relax the one-use constraints.
  Value *B0 = I.getOperand(0), *B1 = I.getOperand(1);
  Instruction *I0, *I1;
  Constant *C0, *C1;
  CmpInst::Predicate P0, P1;
  if (!match(B0, m_OneUse(m_Cmp(P0, m_Instruction(I0), m_Constant(C0)))) ||
      !match(B1, m_OneUse(m_Cmp(P1, m_Instruction(I1), m_Constant(C1)))) ||
      P0 != P1)
    return false;

  // The compare operands must be extracts of the same vector with constant
  // extract indexes.
  // TODO: Relax the one-use constraints.
  Value *X;
  uint64_t Index0, Index1;
  if (!match(I0, m_OneUse(m_ExtractElt(m_Value(X), m_ConstantInt(Index0)))) ||
      !match(I1, m_OneUse(m_ExtractElt(m_Specific(X), m_ConstantInt(Index1)))))
    return false;

  auto *Ext0 = cast<ExtractElementInst>(I0);
  auto *Ext1 = cast<ExtractElementInst>(I1);
  ExtractElementInst *ConvertToShuf = getShuffleExtract(Ext0, Ext1);
  if (!ConvertToShuf)
    return false;

  // The original scalar pattern is:
  // binop i1 (cmp Pred (ext X, Index0), C0), (cmp Pred (ext X, Index1), C1)
  CmpInst::Predicate Pred = P0;
  unsigned CmpOpcode = CmpInst::isFPPredicate(Pred) ? Instruction::FCmp
                                                    : Instruction::ICmp;
  auto *VecTy = dyn_cast<FixedVectorType>(X->getType());
  if (!VecTy)
    return false;

  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  InstructionCost OldCost =
      TTI.getVectorInstrCost(*Ext0, VecTy, CostKind, Index0);
  OldCost += TTI.getVectorInstrCost(*Ext1, VecTy, CostKind, Index1);
  OldCost +=
      TTI.getCmpSelInstrCost(CmpOpcode, I0->getType(),
                             CmpInst::makeCmpResultType(I0->getType()), Pred) *
      2;
  OldCost += TTI.getArithmeticInstrCost(I.getOpcode(), I.getType());

  // The proposed vector pattern is:
  // vcmp = cmp Pred X, VecC
  // ext (binop vNi1 vcmp, (shuffle vcmp, Index1)), Index0
  int CheapIndex = ConvertToShuf == Ext0 ? Index1 : Index0;
  int ExpensiveIndex = ConvertToShuf == Ext0 ? Index0 : Index1;
  auto *CmpTy = cast<FixedVectorType>(CmpInst::makeCmpResultType(X->getType()));
  InstructionCost NewCost = TTI.getCmpSelInstrCost(
      CmpOpcode, X->getType(), CmpInst::makeCmpResultType(X->getType()), Pred);
  SmallVector<int, 32> ShufMask(VecTy->getNumElements(), PoisonMaskElem);
  ShufMask[CheapIndex] = ExpensiveIndex;
  NewCost += TTI.getShuffleCost(TargetTransformInfo::SK_PermuteSingleSrc, CmpTy,
                                ShufMask);
  NewCost += TTI.getArithmeticInstrCost(I.getOpcode(), CmpTy);
  NewCost += TTI.getVectorInstrCost(*Ext0, CmpTy, CostKind, CheapIndex);

  // Aggressively form vector ops if the cost is equal because the transform
  // may enable further optimization.
  // Codegen can reverse this transform (scalarize) if it was not profitable.
  if (OldCost < NewCost || !NewCost.isValid())
    return false;

  // Create a vector constant from the 2 scalar constants.
  SmallVector<Constant *, 32> CmpC(VecTy->getNumElements(),
                                   PoisonValue::get(VecTy->getElementType()));
  CmpC[Index0] = C0;
  CmpC[Index1] = C1;
  Value *VCmp = Builder.CreateCmp(Pred, X, ConstantVector::get(CmpC));

  Value *Shuf = createShiftShuffle(VCmp, ExpensiveIndex, CheapIndex, Builder);
  Value *VecLogic = Builder.CreateBinOp(cast<BinaryOperator>(I).getOpcode(),
                                        VCmp, Shuf);
  Value *NewExt = Builder.CreateExtractElement(VecLogic, CheapIndex);
  replaceValue(I, *NewExt);
  ++NumVecCmpBO;
  return true;
}

// Check if memory loc modified between two instrs in the same BB
static bool isMemModifiedBetween(BasicBlock::iterator Begin,
                                 BasicBlock::iterator End,
                                 const MemoryLocation &Loc, AAResults &AA) {
  unsigned NumScanned = 0;
  return std::any_of(Begin, End, [&](const Instruction &Instr) {
    return isModSet(AA.getModRefInfo(&Instr, Loc)) ||
           ++NumScanned > MaxInstrsToScan;
  });
}

namespace {
/// Helper class to indicate whether a vector index can be safely scalarized and
/// if a freeze needs to be inserted.
class ScalarizationResult {
  enum class StatusTy { Unsafe, Safe, SafeWithFreeze };

  StatusTy Status;
  Value *ToFreeze;

  ScalarizationResult(StatusTy Status, Value *ToFreeze = nullptr)
      : Status(Status), ToFreeze(ToFreeze) {}

public:
  ScalarizationResult(const ScalarizationResult &Other) = default;
  ~ScalarizationResult() {
    assert(!ToFreeze && "freeze() not called with ToFreeze being set");
  }

  static ScalarizationResult unsafe() { return {StatusTy::Unsafe}; }
  static ScalarizationResult safe() { return {StatusTy::Safe}; }
  static ScalarizationResult safeWithFreeze(Value *ToFreeze) {
    return {StatusTy::SafeWithFreeze, ToFreeze};
  }

  /// Returns true if the index can be scalarize without requiring a freeze.
  bool isSafe() const { return Status == StatusTy::Safe; }
  /// Returns true if the index cannot be scalarized.
  bool isUnsafe() const { return Status == StatusTy::Unsafe; }
  /// Returns true if the index can be scalarize, but requires inserting a
  /// freeze.
  bool isSafeWithFreeze() const { return Status == StatusTy::SafeWithFreeze; }

  /// Reset the state of Unsafe and clear ToFreze if set.
  void discard() {
    ToFreeze = nullptr;
    Status = StatusTy::Unsafe;
  }

  /// Freeze the ToFreeze and update the use in \p User to use it.
  void freeze(IRBuilder<> &Builder, Instruction &UserI) {
    assert(isSafeWithFreeze() &&
           "should only be used when freezing is required");
    assert(is_contained(ToFreeze->users(), &UserI) &&
           "UserI must be a user of ToFreeze");
    IRBuilder<>::InsertPointGuard Guard(Builder);
    Builder.SetInsertPoint(cast<Instruction>(&UserI));
    Value *Frozen =
        Builder.CreateFreeze(ToFreeze, ToFreeze->getName() + ".frozen");
    for (Use &U : make_early_inc_range((UserI.operands())))
      if (U.get() == ToFreeze)
        U.set(Frozen);

    ToFreeze = nullptr;
  }
};
} // namespace

/// Check if it is legal to scalarize a memory access to \p VecTy at index \p
/// Idx. \p Idx must access a valid vector element.
static ScalarizationResult canScalarizeAccess(VectorType *VecTy, Value *Idx,
                                              Instruction *CtxI,
                                              AssumptionCache &AC,
                                              const DominatorTree &DT) {
  // We do checks for both fixed vector types and scalable vector types.
  // This is the number of elements of fixed vector types,
  // or the minimum number of elements of scalable vector types.
  uint64_t NumElements = VecTy->getElementCount().getKnownMinValue();

  if (auto *C = dyn_cast<ConstantInt>(Idx)) {
    if (C->getValue().ult(NumElements))
      return ScalarizationResult::safe();
    return ScalarizationResult::unsafe();
  }

  unsigned IntWidth = Idx->getType()->getScalarSizeInBits();
  APInt Zero(IntWidth, 0);
  APInt MaxElts(IntWidth, NumElements);
  ConstantRange ValidIndices(Zero, MaxElts);
  ConstantRange IdxRange(IntWidth, true);

  if (isGuaranteedNotToBePoison(Idx, &AC)) {
    if (ValidIndices.contains(computeConstantRange(Idx, /* ForSigned */ false,
                                                   true, &AC, CtxI, &DT)))
      return ScalarizationResult::safe();
    return ScalarizationResult::unsafe();
  }

  // If the index may be poison, check if we can insert a freeze before the
  // range of the index is restricted.
  Value *IdxBase;
  ConstantInt *CI;
  if (match(Idx, m_And(m_Value(IdxBase), m_ConstantInt(CI)))) {
    IdxRange = IdxRange.binaryAnd(CI->getValue());
  } else if (match(Idx, m_URem(m_Value(IdxBase), m_ConstantInt(CI)))) {
    IdxRange = IdxRange.urem(CI->getValue());
  }

  if (ValidIndices.contains(IdxRange))
    return ScalarizationResult::safeWithFreeze(IdxBase);
  return ScalarizationResult::unsafe();
}

/// The memory operation on a vector of \p ScalarType had alignment of
/// \p VectorAlignment. Compute the maximal, but conservatively correct,
/// alignment that will be valid for the memory operation on a single scalar
/// element of the same type with index \p Idx.
static Align computeAlignmentAfterScalarization(Align VectorAlignment,
                                                Type *ScalarType, Value *Idx,
                                                const DataLayout &DL) {
  if (auto *C = dyn_cast<ConstantInt>(Idx))
    return commonAlignment(VectorAlignment,
                           C->getZExtValue() * DL.getTypeStoreSize(ScalarType));
  return commonAlignment(VectorAlignment, DL.getTypeStoreSize(ScalarType));
}

// Combine patterns like:
//   %0 = load <4 x i32>, <4 x i32>* %a
//   %1 = insertelement <4 x i32> %0, i32 %b, i32 1
//   store <4 x i32> %1, <4 x i32>* %a
// to:
//   %0 = bitcast <4 x i32>* %a to i32*
//   %1 = getelementptr inbounds i32, i32* %0, i64 0, i64 1
//   store i32 %b, i32* %1
bool VectorCombine::foldSingleElementStore(Instruction &I) {
  auto *SI = cast<StoreInst>(&I);
  if (!SI->isSimple() || !isa<VectorType>(SI->getValueOperand()->getType()))
    return false;

  // TODO: Combine more complicated patterns (multiple insert) by referencing
  // TargetTransformInfo.
  Instruction *Source;
  Value *NewElement;
  Value *Idx;
  if (!match(SI->getValueOperand(),
             m_InsertElt(m_Instruction(Source), m_Value(NewElement),
                         m_Value(Idx))))
    return false;

  if (auto *Load = dyn_cast<LoadInst>(Source)) {
    auto VecTy = cast<VectorType>(SI->getValueOperand()->getType());
    Value *SrcAddr = Load->getPointerOperand()->stripPointerCasts();
    // Don't optimize for atomic/volatile load or store. Ensure memory is not
    // modified between, vector type matches store size, and index is inbounds.
    if (!Load->isSimple() || Load->getParent() != SI->getParent() ||
        !DL->typeSizeEqualsStoreSize(Load->getType()->getScalarType()) ||
        SrcAddr != SI->getPointerOperand()->stripPointerCasts())
      return false;

    auto ScalarizableIdx = canScalarizeAccess(VecTy, Idx, Load, AC, DT);
    if (ScalarizableIdx.isUnsafe() ||
        isMemModifiedBetween(Load->getIterator(), SI->getIterator(),
                             MemoryLocation::get(SI), AA))
      return false;

    if (ScalarizableIdx.isSafeWithFreeze())
      ScalarizableIdx.freeze(Builder, *cast<Instruction>(Idx));
    Value *GEP = Builder.CreateInBoundsGEP(
        SI->getValueOperand()->getType(), SI->getPointerOperand(),
        {ConstantInt::get(Idx->getType(), 0), Idx});
    StoreInst *NSI = Builder.CreateStore(NewElement, GEP);
    NSI->copyMetadata(*SI);
    Align ScalarOpAlignment = computeAlignmentAfterScalarization(
        std::max(SI->getAlign(), Load->getAlign()), NewElement->getType(), Idx,
        *DL);
    NSI->setAlignment(ScalarOpAlignment);
    replaceValue(I, *NSI);
    eraseInstruction(I);
    return true;
  }

  return false;
}

/// Try to scalarize vector loads feeding extractelement instructions.
bool VectorCombine::scalarizeLoadExtract(Instruction &I) {
  Value *Ptr;
  if (!match(&I, m_Load(m_Value(Ptr))))
    return false;

  auto *VecTy = cast<VectorType>(I.getType());
  auto *LI = cast<LoadInst>(&I);
  if (LI->isVolatile() || !DL->typeSizeEqualsStoreSize(VecTy->getScalarType()))
    return false;

  InstructionCost OriginalCost =
      TTI.getMemoryOpCost(Instruction::Load, VecTy, LI->getAlign(),
                          LI->getPointerAddressSpace());
  InstructionCost ScalarizedCost = 0;

  Instruction *LastCheckedInst = LI;
  unsigned NumInstChecked = 0;
  DenseMap<ExtractElementInst *, ScalarizationResult> NeedFreeze;
  auto FailureGuard = make_scope_exit([&]() {
    // If the transform is aborted, discard the ScalarizationResults.
    for (auto &Pair : NeedFreeze)
      Pair.second.discard();
  });

  // Check if all users of the load are extracts with no memory modifications
  // between the load and the extract. Compute the cost of both the original
  // code and the scalarized version.
  for (User *U : LI->users()) {
    auto *UI = dyn_cast<ExtractElementInst>(U);
    if (!UI || UI->getParent() != LI->getParent())
      return false;

    // Check if any instruction between the load and the extract may modify
    // memory.
    if (LastCheckedInst->comesBefore(UI)) {
      for (Instruction &I :
           make_range(std::next(LI->getIterator()), UI->getIterator())) {
        // Bail out if we reached the check limit or the instruction may write
        // to memory.
        if (NumInstChecked == MaxInstrsToScan || I.mayWriteToMemory())
          return false;
        NumInstChecked++;
      }
      LastCheckedInst = UI;
    }

    auto ScalarIdx = canScalarizeAccess(VecTy, UI->getOperand(1), &I, AC, DT);
    if (ScalarIdx.isUnsafe())
      return false;
    if (ScalarIdx.isSafeWithFreeze()) {
      NeedFreeze.try_emplace(UI, ScalarIdx);
      ScalarIdx.discard();
    }

    auto *Index = dyn_cast<ConstantInt>(UI->getOperand(1));
    TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
    OriginalCost +=
        TTI.getVectorInstrCost(Instruction::ExtractElement, VecTy, CostKind,
                               Index ? Index->getZExtValue() : -1);
    ScalarizedCost +=
        TTI.getMemoryOpCost(Instruction::Load, VecTy->getElementType(),
                            Align(1), LI->getPointerAddressSpace());
    ScalarizedCost += TTI.getAddressComputationCost(VecTy->getElementType());
  }

  if (ScalarizedCost >= OriginalCost)
    return false;

  // Replace extracts with narrow scalar loads.
  for (User *U : LI->users()) {
    auto *EI = cast<ExtractElementInst>(U);
    Value *Idx = EI->getOperand(1);

    // Insert 'freeze' for poison indexes.
    auto It = NeedFreeze.find(EI);
    if (It != NeedFreeze.end())
      It->second.freeze(Builder, *cast<Instruction>(Idx));

    Builder.SetInsertPoint(EI);
    Value *GEP =
        Builder.CreateInBoundsGEP(VecTy, Ptr, {Builder.getInt32(0), Idx});
    auto *NewLoad = cast<LoadInst>(Builder.CreateLoad(
        VecTy->getElementType(), GEP, EI->getName() + ".scalar"));

    Align ScalarOpAlignment = computeAlignmentAfterScalarization(
        LI->getAlign(), VecTy->getElementType(), Idx, *DL);
    NewLoad->setAlignment(ScalarOpAlignment);

    replaceValue(*EI, *NewLoad);
  }

  FailureGuard.release();
  return true;
}

/// Try to convert "shuffle (binop), (binop)" into "binop (shuffle), (shuffle)".
bool VectorCombine::foldShuffleOfBinops(Instruction &I) {
  BinaryOperator *B0, *B1;
  ArrayRef<int> OldMask;
  if (!match(&I, m_Shuffle(m_OneUse(m_BinOp(B0)), m_OneUse(m_BinOp(B1)),
                           m_Mask(OldMask))))
    return false;

  // Don't introduce poison into div/rem.
  if (any_of(OldMask, [](int M) { return M == PoisonMaskElem; }) &&
      B0->isIntDivRem())
    return false;

  // TODO: Add support for addlike etc.
  Instruction::BinaryOps Opcode = B0->getOpcode();
  if (Opcode != B1->getOpcode())
    return false;

  auto *ShuffleDstTy = dyn_cast<FixedVectorType>(I.getType());
  auto *BinOpTy = dyn_cast<FixedVectorType>(B0->getType());
  if (!ShuffleDstTy || !BinOpTy)
    return false;

  unsigned NumSrcElts = BinOpTy->getNumElements();

  // If we have something like "add X, Y" and "add Z, X", swap ops to match.
  Value *X = B0->getOperand(0), *Y = B0->getOperand(1);
  Value *Z = B1->getOperand(0), *W = B1->getOperand(1);
  if (BinaryOperator::isCommutative(Opcode) && X != Z && Y != W &&
      (X == W || Y == Z))
    std::swap(X, Y);

  auto ConvertToUnary = [NumSrcElts](int &M) {
    if (M >= (int)NumSrcElts)
      M -= NumSrcElts;
  };

  SmallVector<int> NewMask0(OldMask.begin(), OldMask.end());
  TargetTransformInfo::ShuffleKind SK0 = TargetTransformInfo::SK_PermuteTwoSrc;
  if (X == Z) {
    llvm::for_each(NewMask0, ConvertToUnary);
    SK0 = TargetTransformInfo::SK_PermuteSingleSrc;
    Z = PoisonValue::get(BinOpTy);
  }

  SmallVector<int> NewMask1(OldMask.begin(), OldMask.end());
  TargetTransformInfo::ShuffleKind SK1 = TargetTransformInfo::SK_PermuteTwoSrc;
  if (Y == W) {
    llvm::for_each(NewMask1, ConvertToUnary);
    SK1 = TargetTransformInfo::SK_PermuteSingleSrc;
    W = PoisonValue::get(BinOpTy);
  }

  // Try to replace a binop with a shuffle if the shuffle is not costly.
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  InstructionCost OldCost =
      TTI.getArithmeticInstrCost(B0->getOpcode(), BinOpTy, CostKind) +
      TTI.getArithmeticInstrCost(B1->getOpcode(), BinOpTy, CostKind) +
      TTI.getShuffleCost(TargetTransformInfo::SK_PermuteTwoSrc, BinOpTy,
                         OldMask, CostKind, 0, nullptr, {B0, B1}, &I);

  InstructionCost NewCost =
      TTI.getShuffleCost(SK0, BinOpTy, NewMask0, CostKind, 0, nullptr, {X, Z}) +
      TTI.getShuffleCost(SK1, BinOpTy, NewMask1, CostKind, 0, nullptr, {Y, W}) +
      TTI.getArithmeticInstrCost(Opcode, ShuffleDstTy, CostKind);

  LLVM_DEBUG(dbgs() << "Found a shuffle feeding two binops: " << I
                    << "\n  OldCost: " << OldCost << " vs NewCost: " << NewCost
                    << "\n");
  if (NewCost >= OldCost)
    return false;

  Value *Shuf0 = Builder.CreateShuffleVector(X, Z, NewMask0);
  Value *Shuf1 = Builder.CreateShuffleVector(Y, W, NewMask1);
  Value *NewBO = Builder.CreateBinOp(Opcode, Shuf0, Shuf1);

  // Intersect flags from the old binops.
  if (auto *NewInst = dyn_cast<Instruction>(NewBO)) {
    NewInst->copyIRFlags(B0);
    NewInst->andIRFlags(B1);
  }

  Worklist.pushValue(Shuf0);
  Worklist.pushValue(Shuf1);
  replaceValue(I, *NewBO);
  return true;
}

/// Try to convert "shuffle (castop), (castop)" with a shared castop operand
/// into "castop (shuffle)".
bool VectorCombine::foldShuffleOfCastops(Instruction &I) {
  Value *V0, *V1;
  ArrayRef<int> OldMask;
  if (!match(&I, m_Shuffle(m_Value(V0), m_Value(V1), m_Mask(OldMask))))
    return false;

  auto *C0 = dyn_cast<CastInst>(V0);
  auto *C1 = dyn_cast<CastInst>(V1);
  if (!C0 || !C1)
    return false;

  Instruction::CastOps Opcode = C0->getOpcode();
  if (C0->getSrcTy() != C1->getSrcTy())
    return false;

  // Handle shuffle(zext_nneg(x), sext(y)) -> sext(shuffle(x,y)) folds.
  if (Opcode != C1->getOpcode()) {
    if (match(C0, m_SExtLike(m_Value())) && match(C1, m_SExtLike(m_Value())))
      Opcode = Instruction::SExt;
    else
      return false;
  }

  auto *ShuffleDstTy = dyn_cast<FixedVectorType>(I.getType());
  auto *CastDstTy = dyn_cast<FixedVectorType>(C0->getDestTy());
  auto *CastSrcTy = dyn_cast<FixedVectorType>(C0->getSrcTy());
  if (!ShuffleDstTy || !CastDstTy || !CastSrcTy)
    return false;

  unsigned NumSrcElts = CastSrcTy->getNumElements();
  unsigned NumDstElts = CastDstTy->getNumElements();
  assert((NumDstElts == NumSrcElts || Opcode == Instruction::BitCast) &&
         "Only bitcasts expected to alter src/dst element counts");

  // Check for bitcasting of unscalable vector types.
  // e.g. <32 x i40> -> <40 x i32>
  if (NumDstElts != NumSrcElts && (NumSrcElts % NumDstElts) != 0 &&
      (NumDstElts % NumSrcElts) != 0)
    return false;

  SmallVector<int, 16> NewMask;
  if (NumSrcElts >= NumDstElts) {
    // The bitcast is from wide to narrow/equal elements. The shuffle mask can
    // always be expanded to the equivalent form choosing narrower elements.
    assert(NumSrcElts % NumDstElts == 0 && "Unexpected shuffle mask");
    unsigned ScaleFactor = NumSrcElts / NumDstElts;
    narrowShuffleMaskElts(ScaleFactor, OldMask, NewMask);
  } else {
    // The bitcast is from narrow elements to wide elements. The shuffle mask
    // must choose consecutive elements to allow casting first.
    assert(NumDstElts % NumSrcElts == 0 && "Unexpected shuffle mask");
    unsigned ScaleFactor = NumDstElts / NumSrcElts;
    if (!widenShuffleMaskElts(ScaleFactor, OldMask, NewMask))
      return false;
  }

  auto *NewShuffleDstTy =
      FixedVectorType::get(CastSrcTy->getScalarType(), NewMask.size());

  // Try to replace a castop with a shuffle if the shuffle is not costly.
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  InstructionCost CostC0 =
      TTI.getCastInstrCost(C0->getOpcode(), CastDstTy, CastSrcTy,
                           TTI::CastContextHint::None, CostKind);
  InstructionCost CostC1 =
      TTI.getCastInstrCost(C1->getOpcode(), CastDstTy, CastSrcTy,
                           TTI::CastContextHint::None, CostKind);
  InstructionCost OldCost = CostC0 + CostC1;
  OldCost +=
      TTI.getShuffleCost(TargetTransformInfo::SK_PermuteTwoSrc, CastDstTy,
                         OldMask, CostKind, 0, nullptr, std::nullopt, &I);

  InstructionCost NewCost = TTI.getShuffleCost(
      TargetTransformInfo::SK_PermuteTwoSrc, CastSrcTy, NewMask, CostKind);
  NewCost += TTI.getCastInstrCost(Opcode, ShuffleDstTy, NewShuffleDstTy,
                                  TTI::CastContextHint::None, CostKind);
  if (!C0->hasOneUse())
    NewCost += CostC0;
  if (!C1->hasOneUse())
    NewCost += CostC1;

  LLVM_DEBUG(dbgs() << "Found a shuffle feeding two casts: " << I
                    << "\n  OldCost: " << OldCost << " vs NewCost: " << NewCost
                    << "\n");
  if (NewCost > OldCost)
    return false;

  Value *Shuf = Builder.CreateShuffleVector(C0->getOperand(0),
                                            C1->getOperand(0), NewMask);
  Value *Cast = Builder.CreateCast(Opcode, Shuf, ShuffleDstTy);

  // Intersect flags from the old casts.
  if (auto *NewInst = dyn_cast<Instruction>(Cast)) {
    NewInst->copyIRFlags(C0);
    NewInst->andIRFlags(C1);
  }

  Worklist.pushValue(Shuf);
  replaceValue(I, *Cast);
  return true;
}

/// Try to convert "shuffle (shuffle x, undef), (shuffle y, undef)"
/// into "shuffle x, y".
bool VectorCombine::foldShuffleOfShuffles(Instruction &I) {
  Value *V0, *V1;
  UndefValue *U0, *U1;
  ArrayRef<int> OuterMask, InnerMask0, InnerMask1;
  if (!match(&I, m_Shuffle(m_OneUse(m_Shuffle(m_Value(V0), m_UndefValue(U0),
                                              m_Mask(InnerMask0))),
                           m_OneUse(m_Shuffle(m_Value(V1), m_UndefValue(U1),
                                              m_Mask(InnerMask1))),
                           m_Mask(OuterMask))))
    return false;

  auto *ShufI0 = dyn_cast<Instruction>(I.getOperand(0));
  auto *ShufI1 = dyn_cast<Instruction>(I.getOperand(1));
  auto *ShuffleDstTy = dyn_cast<FixedVectorType>(I.getType());
  auto *ShuffleSrcTy = dyn_cast<FixedVectorType>(V0->getType());
  auto *ShuffleImmTy = dyn_cast<FixedVectorType>(I.getOperand(0)->getType());
  if (!ShuffleDstTy || !ShuffleSrcTy || !ShuffleImmTy ||
      V0->getType() != V1->getType())
    return false;

  unsigned NumSrcElts = ShuffleSrcTy->getNumElements();
  unsigned NumImmElts = ShuffleImmTy->getNumElements();

  // Bail if either inner masks reference a RHS undef arg.
  if ((!isa<PoisonValue>(U0) &&
       any_of(InnerMask0, [&](int M) { return M >= (int)NumSrcElts; })) ||
      (!isa<PoisonValue>(U1) &&
       any_of(InnerMask1, [&](int M) { return M >= (int)NumSrcElts; })))
    return false;

  // Merge shuffles - replace index to the RHS poison arg with PoisonMaskElem,
  SmallVector<int, 16> NewMask(OuterMask.begin(), OuterMask.end());
  for (int &M : NewMask) {
    if (0 <= M && M < (int)NumImmElts) {
      M = (InnerMask0[M] >= (int)NumSrcElts) ? PoisonMaskElem : InnerMask0[M];
    } else if (M >= (int)NumImmElts) {
      if (InnerMask1[M - NumImmElts] >= (int)NumSrcElts)
        M = PoisonMaskElem;
      else
        M = InnerMask1[M - NumImmElts] + (V0 == V1 ? 0 : NumSrcElts);
    }
  }

  // Have we folded to an Identity shuffle?
  if (ShuffleVectorInst::isIdentityMask(NewMask, NumSrcElts)) {
    replaceValue(I, *V0);
    return true;
  }

  // Try to merge the shuffles if the new shuffle is not costly.
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;

  InstructionCost OldCost =
      TTI.getShuffleCost(TargetTransformInfo::SK_PermuteSingleSrc, ShuffleSrcTy,
                         InnerMask0, CostKind, 0, nullptr, {V0, U0}, ShufI0) +
      TTI.getShuffleCost(TargetTransformInfo::SK_PermuteSingleSrc, ShuffleSrcTy,
                         InnerMask1, CostKind, 0, nullptr, {V1, U1}, ShufI1) +
      TTI.getShuffleCost(TargetTransformInfo::SK_PermuteTwoSrc, ShuffleImmTy,
                         OuterMask, CostKind, 0, nullptr, {ShufI0, ShufI1}, &I);

  InstructionCost NewCost =
      TTI.getShuffleCost(TargetTransformInfo::SK_PermuteTwoSrc, ShuffleSrcTy,
                         NewMask, CostKind, 0, nullptr, {V0, V1});

  LLVM_DEBUG(dbgs() << "Found a shuffle feeding two shuffles: " << I
                    << "\n  OldCost: " << OldCost << " vs NewCost: " << NewCost
                    << "\n");
  if (NewCost > OldCost)
    return false;

  // Clear unused sources to poison.
  if (none_of(NewMask, [&](int M) { return 0 <= M && M < (int)NumSrcElts; }))
    V0 = PoisonValue::get(ShuffleSrcTy);
  if (none_of(NewMask, [&](int M) { return (int)NumSrcElts <= M; }))
    V1 = PoisonValue::get(ShuffleSrcTy);

  Value *Shuf = Builder.CreateShuffleVector(V0, V1, NewMask);
  replaceValue(I, *Shuf);
  return true;
}

using InstLane = std::pair<Use *, int>;

static InstLane lookThroughShuffles(Use *U, int Lane) {
  while (auto *SV = dyn_cast<ShuffleVectorInst>(U->get())) {
    unsigned NumElts =
        cast<FixedVectorType>(SV->getOperand(0)->getType())->getNumElements();
    int M = SV->getMaskValue(Lane);
    if (M < 0)
      return {nullptr, PoisonMaskElem};
    if (static_cast<unsigned>(M) < NumElts) {
      U = &SV->getOperandUse(0);
      Lane = M;
    } else {
      U = &SV->getOperandUse(1);
      Lane = M - NumElts;
    }
  }
  return InstLane{U, Lane};
}

static SmallVector<InstLane>
generateInstLaneVectorFromOperand(ArrayRef<InstLane> Item, int Op) {
  SmallVector<InstLane> NItem;
  for (InstLane IL : Item) {
    auto [U, Lane] = IL;
    InstLane OpLane =
        U ? lookThroughShuffles(&cast<Instruction>(U->get())->getOperandUse(Op),
                                Lane)
          : InstLane{nullptr, PoisonMaskElem};
    NItem.emplace_back(OpLane);
  }
  return NItem;
}

/// Detect concat of multiple values into a vector
static bool isFreeConcat(ArrayRef<InstLane> Item,
                         const TargetTransformInfo &TTI) {
  auto *Ty = cast<FixedVectorType>(Item.front().first->get()->getType());
  unsigned NumElts = Ty->getNumElements();
  if (Item.size() == NumElts || NumElts == 1 || Item.size() % NumElts != 0)
    return false;

  // Check that the concat is free, usually meaning that the type will be split
  // during legalization.
  SmallVector<int, 16> ConcatMask(NumElts * 2);
  std::iota(ConcatMask.begin(), ConcatMask.end(), 0);
  if (TTI.getShuffleCost(TTI::SK_PermuteTwoSrc, Ty, ConcatMask,
                         TTI::TCK_RecipThroughput) != 0)
    return false;

  unsigned NumSlices = Item.size() / NumElts;
  // Currently we generate a tree of shuffles for the concats, which limits us
  // to a power2.
  if (!isPowerOf2_32(NumSlices))
    return false;
  for (unsigned Slice = 0; Slice < NumSlices; ++Slice) {
    Use *SliceV = Item[Slice * NumElts].first;
    if (!SliceV || SliceV->get()->getType() != Ty)
      return false;
    for (unsigned Elt = 0; Elt < NumElts; ++Elt) {
      auto [V, Lane] = Item[Slice * NumElts + Elt];
      if (Lane != static_cast<int>(Elt) || SliceV->get() != V->get())
        return false;
    }
  }
  return true;
}

static Value *generateNewInstTree(ArrayRef<InstLane> Item, FixedVectorType *Ty,
                                  const SmallPtrSet<Use *, 4> &IdentityLeafs,
                                  const SmallPtrSet<Use *, 4> &SplatLeafs,
                                  const SmallPtrSet<Use *, 4> &ConcatLeafs,
                                  IRBuilder<> &Builder) {
  auto [FrontU, FrontLane] = Item.front();

  if (IdentityLeafs.contains(FrontU)) {
    return FrontU->get();
  }
  if (SplatLeafs.contains(FrontU)) {
    SmallVector<int, 16> Mask(Ty->getNumElements(), FrontLane);
    return Builder.CreateShuffleVector(FrontU->get(), Mask);
  }
  if (ConcatLeafs.contains(FrontU)) {
    unsigned NumElts =
        cast<FixedVectorType>(FrontU->get()->getType())->getNumElements();
    SmallVector<Value *> Values(Item.size() / NumElts, nullptr);
    for (unsigned S = 0; S < Values.size(); ++S)
      Values[S] = Item[S * NumElts].first->get();

    while (Values.size() > 1) {
      NumElts *= 2;
      SmallVector<int, 16> Mask(NumElts, 0);
      std::iota(Mask.begin(), Mask.end(), 0);
      SmallVector<Value *> NewValues(Values.size() / 2, nullptr);
      for (unsigned S = 0; S < NewValues.size(); ++S)
        NewValues[S] =
            Builder.CreateShuffleVector(Values[S * 2], Values[S * 2 + 1], Mask);
      Values = NewValues;
    }
    return Values[0];
  }

  auto *I = cast<Instruction>(FrontU->get());
  auto *II = dyn_cast<IntrinsicInst>(I);
  unsigned NumOps = I->getNumOperands() - (II ? 1 : 0);
  SmallVector<Value *> Ops(NumOps);
  for (unsigned Idx = 0; Idx < NumOps; Idx++) {
    if (II && isVectorIntrinsicWithScalarOpAtArg(II->getIntrinsicID(), Idx)) {
      Ops[Idx] = II->getOperand(Idx);
      continue;
    }
    Ops[Idx] =
        generateNewInstTree(generateInstLaneVectorFromOperand(Item, Idx), Ty,
                            IdentityLeafs, SplatLeafs, ConcatLeafs, Builder);
  }

  SmallVector<Value *, 8> ValueList;
  for (const auto &Lane : Item)
    if (Lane.first)
      ValueList.push_back(Lane.first->get());

  Type *DstTy =
      FixedVectorType::get(I->getType()->getScalarType(), Ty->getNumElements());
  if (auto *BI = dyn_cast<BinaryOperator>(I)) {
    auto *Value = Builder.CreateBinOp((Instruction::BinaryOps)BI->getOpcode(),
                                      Ops[0], Ops[1]);
    propagateIRFlags(Value, ValueList);
    return Value;
  }
  if (auto *CI = dyn_cast<CmpInst>(I)) {
    auto *Value = Builder.CreateCmp(CI->getPredicate(), Ops[0], Ops[1]);
    propagateIRFlags(Value, ValueList);
    return Value;
  }
  if (auto *SI = dyn_cast<SelectInst>(I)) {
    auto *Value = Builder.CreateSelect(Ops[0], Ops[1], Ops[2], "", SI);
    propagateIRFlags(Value, ValueList);
    return Value;
  }
  if (auto *CI = dyn_cast<CastInst>(I)) {
    auto *Value = Builder.CreateCast((Instruction::CastOps)CI->getOpcode(),
                                     Ops[0], DstTy);
    propagateIRFlags(Value, ValueList);
    return Value;
  }
  if (II) {
    auto *Value = Builder.CreateIntrinsic(DstTy, II->getIntrinsicID(), Ops);
    propagateIRFlags(Value, ValueList);
    return Value;
  }
  assert(isa<UnaryInstruction>(I) && "Unexpected instruction type in Generate");
  auto *Value =
      Builder.CreateUnOp((Instruction::UnaryOps)I->getOpcode(), Ops[0]);
  propagateIRFlags(Value, ValueList);
  return Value;
}

// Starting from a shuffle, look up through operands tracking the shuffled index
// of each lane. If we can simplify away the shuffles to identities then
// do so.
bool VectorCombine::foldShuffleToIdentity(Instruction &I) {
  auto *Ty = dyn_cast<FixedVectorType>(I.getType());
  if (!Ty || I.use_empty())
    return false;

  SmallVector<InstLane> Start(Ty->getNumElements());
  for (unsigned M = 0, E = Ty->getNumElements(); M < E; ++M)
    Start[M] = lookThroughShuffles(&*I.use_begin(), M);

  SmallVector<SmallVector<InstLane>> Worklist;
  Worklist.push_back(Start);
  SmallPtrSet<Use *, 4> IdentityLeafs, SplatLeafs, ConcatLeafs;
  unsigned NumVisited = 0;

  while (!Worklist.empty()) {
    if (++NumVisited > MaxInstrsToScan)
      return false;

    SmallVector<InstLane> Item = Worklist.pop_back_val();
    auto [FrontU, FrontLane] = Item.front();

    // If we found an undef first lane then bail out to keep things simple.
    if (!FrontU)
      return false;

    // Helper to peek through bitcasts to the same value.
    auto IsEquiv = [&](Value *X, Value *Y) {
      return X->getType() == Y->getType() &&
             peekThroughBitcasts(X) == peekThroughBitcasts(Y);
    };

    // Look for an identity value.
    if (FrontLane == 0 &&
        cast<FixedVectorType>(FrontU->get()->getType())->getNumElements() ==
            Ty->getNumElements() &&
        all_of(drop_begin(enumerate(Item)), [IsEquiv, Item](const auto &E) {
          Value *FrontV = Item.front().first->get();
          return !E.value().first || (IsEquiv(E.value().first->get(), FrontV) &&
                                      E.value().second == (int)E.index());
        })) {
      IdentityLeafs.insert(FrontU);
      continue;
    }
    // Look for constants, for the moment only supporting constant splats.
    if (auto *C = dyn_cast<Constant>(FrontU);
        C && C->getSplatValue() &&
        all_of(drop_begin(Item), [Item](InstLane &IL) {
          Value *FrontV = Item.front().first->get();
          Use *U = IL.first;
          return !U || U->get() == FrontV;
        })) {
      SplatLeafs.insert(FrontU);
      continue;
    }
    // Look for a splat value.
    if (all_of(drop_begin(Item), [Item](InstLane &IL) {
          auto [FrontU, FrontLane] = Item.front();
          auto [U, Lane] = IL;
          return !U || (U->get() == FrontU->get() && Lane == FrontLane);
        })) {
      SplatLeafs.insert(FrontU);
      continue;
    }

    // We need each element to be the same type of value, and check that each
    // element has a single use.
    auto CheckLaneIsEquivalentToFirst = [Item](InstLane IL) {
      Value *FrontV = Item.front().first->get();
      if (!IL.first)
        return true;
      Value *V = IL.first->get();
      if (auto *I = dyn_cast<Instruction>(V); I && !I->hasOneUse())
        return false;
      if (V->getValueID() != FrontV->getValueID())
        return false;
      if (auto *CI = dyn_cast<CmpInst>(V))
        if (CI->getPredicate() != cast<CmpInst>(FrontV)->getPredicate())
          return false;
      if (auto *CI = dyn_cast<CastInst>(V))
        if (CI->getSrcTy() != cast<CastInst>(FrontV)->getSrcTy())
          return false;
      if (auto *SI = dyn_cast<SelectInst>(V))
        if (!isa<VectorType>(SI->getOperand(0)->getType()) ||
            SI->getOperand(0)->getType() !=
                cast<SelectInst>(FrontV)->getOperand(0)->getType())
          return false;
      if (isa<CallInst>(V) && !isa<IntrinsicInst>(V))
        return false;
      auto *II = dyn_cast<IntrinsicInst>(V);
      return !II || (isa<IntrinsicInst>(FrontV) &&
                     II->getIntrinsicID() ==
                         cast<IntrinsicInst>(FrontV)->getIntrinsicID() &&
                     !II->hasOperandBundles());
    };
    if (all_of(drop_begin(Item), CheckLaneIsEquivalentToFirst)) {
      // Check the operator is one that we support.
      if (isa<BinaryOperator, CmpInst>(FrontU)) {
        //  We exclude div/rem in case they hit UB from poison lanes.
        if (auto *BO = dyn_cast<BinaryOperator>(FrontU);
            BO && BO->isIntDivRem())
          return false;
        Worklist.push_back(generateInstLaneVectorFromOperand(Item, 0));
        Worklist.push_back(generateInstLaneVectorFromOperand(Item, 1));
        continue;
      } else if (isa<UnaryOperator, TruncInst, ZExtInst, SExtInst>(FrontU)) {
        Worklist.push_back(generateInstLaneVectorFromOperand(Item, 0));
        continue;
      } else if (auto *BitCast = dyn_cast<BitCastInst>(FrontU)) {
        // TODO: Handle vector widening/narrowing bitcasts.
        auto *DstTy = dyn_cast<FixedVectorType>(BitCast->getDestTy());
        auto *SrcTy = dyn_cast<FixedVectorType>(BitCast->getSrcTy());
        if (DstTy && SrcTy &&
            SrcTy->getNumElements() == DstTy->getNumElements()) {
          Worklist.push_back(generateInstLaneVectorFromOperand(Item, 0));
          continue;
        }
      } else if (isa<SelectInst>(FrontU)) {
        Worklist.push_back(generateInstLaneVectorFromOperand(Item, 0));
        Worklist.push_back(generateInstLaneVectorFromOperand(Item, 1));
        Worklist.push_back(generateInstLaneVectorFromOperand(Item, 2));
        continue;
      } else if (auto *II = dyn_cast<IntrinsicInst>(FrontU);
                 II && isTriviallyVectorizable(II->getIntrinsicID()) &&
                 !II->hasOperandBundles()) {
        for (unsigned Op = 0, E = II->getNumOperands() - 1; Op < E; Op++) {
          if (isVectorIntrinsicWithScalarOpAtArg(II->getIntrinsicID(), Op)) {
            if (!all_of(drop_begin(Item), [Item, Op](InstLane &IL) {
                  Value *FrontV = Item.front().first->get();
                  Use *U = IL.first;
                  return !U || (cast<Instruction>(U->get())->getOperand(Op) ==
                                cast<Instruction>(FrontV)->getOperand(Op));
                }))
              return false;
            continue;
          }
          Worklist.push_back(generateInstLaneVectorFromOperand(Item, Op));
        }
        continue;
      }
    }

    if (isFreeConcat(Item, TTI)) {
      ConcatLeafs.insert(FrontU);
      continue;
    }

    return false;
  }

  if (NumVisited <= 1)
    return false;

  // If we got this far, we know the shuffles are superfluous and can be
  // removed. Scan through again and generate the new tree of instructions.
  Builder.SetInsertPoint(&I);
  Value *V = generateNewInstTree(Start, Ty, IdentityLeafs, SplatLeafs,
                                 ConcatLeafs, Builder);
  replaceValue(I, *V);
  return true;
}

/// Given a commutative reduction, the order of the input lanes does not alter
/// the results. We can use this to remove certain shuffles feeding the
/// reduction, removing the need to shuffle at all.
bool VectorCombine::foldShuffleFromReductions(Instruction &I) {
  auto *II = dyn_cast<IntrinsicInst>(&I);
  if (!II)
    return false;
  switch (II->getIntrinsicID()) {
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_smax:
  case Intrinsic::vector_reduce_umin:
  case Intrinsic::vector_reduce_umax:
    break;
  default:
    return false;
  }

  // Find all the inputs when looking through operations that do not alter the
  // lane order (binops, for example). Currently we look for a single shuffle,
  // and can ignore splat values.
  std::queue<Value *> Worklist;
  SmallPtrSet<Value *, 4> Visited;
  ShuffleVectorInst *Shuffle = nullptr;
  if (auto *Op = dyn_cast<Instruction>(I.getOperand(0)))
    Worklist.push(Op);

  while (!Worklist.empty()) {
    Value *CV = Worklist.front();
    Worklist.pop();
    if (Visited.contains(CV))
      continue;

    // Splats don't change the order, so can be safely ignored.
    if (isSplatValue(CV))
      continue;

    Visited.insert(CV);

    if (auto *CI = dyn_cast<Instruction>(CV)) {
      if (CI->isBinaryOp()) {
        for (auto *Op : CI->operand_values())
          Worklist.push(Op);
        continue;
      } else if (auto *SV = dyn_cast<ShuffleVectorInst>(CI)) {
        if (Shuffle && Shuffle != SV)
          return false;
        Shuffle = SV;
        continue;
      }
    }

    // Anything else is currently an unknown node.
    return false;
  }

  if (!Shuffle)
    return false;

  // Check all uses of the binary ops and shuffles are also included in the
  // lane-invariant operations (Visited should be the list of lanewise
  // instructions, including the shuffle that we found).
  for (auto *V : Visited)
    for (auto *U : V->users())
      if (!Visited.contains(U) && U != &I)
        return false;

  FixedVectorType *VecType =
      dyn_cast<FixedVectorType>(II->getOperand(0)->getType());
  if (!VecType)
    return false;
  FixedVectorType *ShuffleInputType =
      dyn_cast<FixedVectorType>(Shuffle->getOperand(0)->getType());
  if (!ShuffleInputType)
    return false;
  unsigned NumInputElts = ShuffleInputType->getNumElements();

  // Find the mask from sorting the lanes into order. This is most likely to
  // become a identity or concat mask. Undef elements are pushed to the end.
  SmallVector<int> ConcatMask;
  Shuffle->getShuffleMask(ConcatMask);
  sort(ConcatMask, [](int X, int Y) { return (unsigned)X < (unsigned)Y; });
  // In the case of a truncating shuffle it's possible for the mask
  // to have an index greater than the size of the resulting vector.
  // This requires special handling.
  bool IsTruncatingShuffle = VecType->getNumElements() < NumInputElts;
  bool UsesSecondVec =
      any_of(ConcatMask, [&](int M) { return M >= (int)NumInputElts; });

  FixedVectorType *VecTyForCost =
      (UsesSecondVec && !IsTruncatingShuffle) ? VecType : ShuffleInputType;
  InstructionCost OldCost = TTI.getShuffleCost(
      UsesSecondVec ? TTI::SK_PermuteTwoSrc : TTI::SK_PermuteSingleSrc,
      VecTyForCost, Shuffle->getShuffleMask());
  InstructionCost NewCost = TTI.getShuffleCost(
      UsesSecondVec ? TTI::SK_PermuteTwoSrc : TTI::SK_PermuteSingleSrc,
      VecTyForCost, ConcatMask);

  LLVM_DEBUG(dbgs() << "Found a reduction feeding from a shuffle: " << *Shuffle
                    << "\n");
  LLVM_DEBUG(dbgs() << "  OldCost: " << OldCost << " vs NewCost: " << NewCost
                    << "\n");
  if (NewCost < OldCost) {
    Builder.SetInsertPoint(Shuffle);
    Value *NewShuffle = Builder.CreateShuffleVector(
        Shuffle->getOperand(0), Shuffle->getOperand(1), ConcatMask);
    LLVM_DEBUG(dbgs() << "Created new shuffle: " << *NewShuffle << "\n");
    replaceValue(*Shuffle, *NewShuffle);
  }

  // See if we can re-use foldSelectShuffle, getting it to reduce the size of
  // the shuffle into a nicer order, as it can ignore the order of the shuffles.
  return foldSelectShuffle(*Shuffle, true);
}

/// Determine if its more efficient to fold:
///   reduce(trunc(x)) -> trunc(reduce(x)).
///   reduce(sext(x))  -> sext(reduce(x)).
///   reduce(zext(x))  -> zext(reduce(x)).
bool VectorCombine::foldCastFromReductions(Instruction &I) {
  auto *II = dyn_cast<IntrinsicInst>(&I);
  if (!II)
    return false;

  bool TruncOnly = false;
  Intrinsic::ID IID = II->getIntrinsicID();
  switch (IID) {
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
    TruncOnly = true;
    break;
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
    break;
  default:
    return false;
  }

  unsigned ReductionOpc = getArithmeticReductionInstruction(IID);
  Value *ReductionSrc = I.getOperand(0);

  Value *Src;
  if (!match(ReductionSrc, m_OneUse(m_Trunc(m_Value(Src)))) &&
      (TruncOnly || !match(ReductionSrc, m_OneUse(m_ZExtOrSExt(m_Value(Src))))))
    return false;

  auto CastOpc =
      (Instruction::CastOps)cast<Instruction>(ReductionSrc)->getOpcode();

  auto *SrcTy = cast<VectorType>(Src->getType());
  auto *ReductionSrcTy = cast<VectorType>(ReductionSrc->getType());
  Type *ResultTy = I.getType();

  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  InstructionCost OldCost = TTI.getArithmeticReductionCost(
      ReductionOpc, ReductionSrcTy, std::nullopt, CostKind);
  OldCost += TTI.getCastInstrCost(CastOpc, ReductionSrcTy, SrcTy,
                                  TTI::CastContextHint::None, CostKind,
                                  cast<CastInst>(ReductionSrc));
  InstructionCost NewCost =
      TTI.getArithmeticReductionCost(ReductionOpc, SrcTy, std::nullopt,
                                     CostKind) +
      TTI.getCastInstrCost(CastOpc, ResultTy, ReductionSrcTy->getScalarType(),
                           TTI::CastContextHint::None, CostKind);

  if (OldCost <= NewCost || !NewCost.isValid())
    return false;

  Value *NewReduction = Builder.CreateIntrinsic(SrcTy->getScalarType(),
                                                II->getIntrinsicID(), {Src});
  Value *NewCast = Builder.CreateCast(CastOpc, NewReduction, ResultTy);
  replaceValue(I, *NewCast);
  return true;
}

/// This method looks for groups of shuffles acting on binops, of the form:
///  %x = shuffle ...
///  %y = shuffle ...
///  %a = binop %x, %y
///  %b = binop %x, %y
///  shuffle %a, %b, selectmask
/// We may, especially if the shuffle is wider than legal, be able to convert
/// the shuffle to a form where only parts of a and b need to be computed. On
/// architectures with no obvious "select" shuffle, this can reduce the total
/// number of operations if the target reports them as cheaper.
bool VectorCombine::foldSelectShuffle(Instruction &I, bool FromReduction) {
  auto *SVI = cast<ShuffleVectorInst>(&I);
  auto *VT = cast<FixedVectorType>(I.getType());
  auto *Op0 = dyn_cast<Instruction>(SVI->getOperand(0));
  auto *Op1 = dyn_cast<Instruction>(SVI->getOperand(1));
  if (!Op0 || !Op1 || Op0 == Op1 || !Op0->isBinaryOp() || !Op1->isBinaryOp() ||
      VT != Op0->getType())
    return false;

  auto *SVI0A = dyn_cast<Instruction>(Op0->getOperand(0));
  auto *SVI0B = dyn_cast<Instruction>(Op0->getOperand(1));
  auto *SVI1A = dyn_cast<Instruction>(Op1->getOperand(0));
  auto *SVI1B = dyn_cast<Instruction>(Op1->getOperand(1));
  SmallPtrSet<Instruction *, 4> InputShuffles({SVI0A, SVI0B, SVI1A, SVI1B});
  auto checkSVNonOpUses = [&](Instruction *I) {
    if (!I || I->getOperand(0)->getType() != VT)
      return true;
    return any_of(I->users(), [&](User *U) {
      return U != Op0 && U != Op1 &&
             !(isa<ShuffleVectorInst>(U) &&
               (InputShuffles.contains(cast<Instruction>(U)) ||
                isInstructionTriviallyDead(cast<Instruction>(U))));
    });
  };
  if (checkSVNonOpUses(SVI0A) || checkSVNonOpUses(SVI0B) ||
      checkSVNonOpUses(SVI1A) || checkSVNonOpUses(SVI1B))
    return false;

  // Collect all the uses that are shuffles that we can transform together. We
  // may not have a single shuffle, but a group that can all be transformed
  // together profitably.
  SmallVector<ShuffleVectorInst *> Shuffles;
  auto collectShuffles = [&](Instruction *I) {
    for (auto *U : I->users()) {
      auto *SV = dyn_cast<ShuffleVectorInst>(U);
      if (!SV || SV->getType() != VT)
        return false;
      if ((SV->getOperand(0) != Op0 && SV->getOperand(0) != Op1) ||
          (SV->getOperand(1) != Op0 && SV->getOperand(1) != Op1))
        return false;
      if (!llvm::is_contained(Shuffles, SV))
        Shuffles.push_back(SV);
    }
    return true;
  };
  if (!collectShuffles(Op0) || !collectShuffles(Op1))
    return false;
  // From a reduction, we need to be processing a single shuffle, otherwise the
  // other uses will not be lane-invariant.
  if (FromReduction && Shuffles.size() > 1)
    return false;

  // Add any shuffle uses for the shuffles we have found, to include them in our
  // cost calculations.
  if (!FromReduction) {
    for (ShuffleVectorInst *SV : Shuffles) {
      for (auto *U : SV->users()) {
        ShuffleVectorInst *SSV = dyn_cast<ShuffleVectorInst>(U);
        if (SSV && isa<UndefValue>(SSV->getOperand(1)) && SSV->getType() == VT)
          Shuffles.push_back(SSV);
      }
    }
  }

  // For each of the output shuffles, we try to sort all the first vector
  // elements to the beginning, followed by the second array elements at the
  // end. If the binops are legalized to smaller vectors, this may reduce total
  // number of binops. We compute the ReconstructMask mask needed to convert
  // back to the original lane order.
  SmallVector<std::pair<int, int>> V1, V2;
  SmallVector<SmallVector<int>> OrigReconstructMasks;
  int MaxV1Elt = 0, MaxV2Elt = 0;
  unsigned NumElts = VT->getNumElements();
  for (ShuffleVectorInst *SVN : Shuffles) {
    SmallVector<int> Mask;
    SVN->getShuffleMask(Mask);

    // Check the operands are the same as the original, or reversed (in which
    // case we need to commute the mask).
    Value *SVOp0 = SVN->getOperand(0);
    Value *SVOp1 = SVN->getOperand(1);
    if (isa<UndefValue>(SVOp1)) {
      auto *SSV = cast<ShuffleVectorInst>(SVOp0);
      SVOp0 = SSV->getOperand(0);
      SVOp1 = SSV->getOperand(1);
      for (unsigned I = 0, E = Mask.size(); I != E; I++) {
        if (Mask[I] >= static_cast<int>(SSV->getShuffleMask().size()))
          return false;
        Mask[I] = Mask[I] < 0 ? Mask[I] : SSV->getMaskValue(Mask[I]);
      }
    }
    if (SVOp0 == Op1 && SVOp1 == Op0) {
      std::swap(SVOp0, SVOp1);
      ShuffleVectorInst::commuteShuffleMask(Mask, NumElts);
    }
    if (SVOp0 != Op0 || SVOp1 != Op1)
      return false;

    // Calculate the reconstruction mask for this shuffle, as the mask needed to
    // take the packed values from Op0/Op1 and reconstructing to the original
    // order.
    SmallVector<int> ReconstructMask;
    for (unsigned I = 0; I < Mask.size(); I++) {
      if (Mask[I] < 0) {
        ReconstructMask.push_back(-1);
      } else if (Mask[I] < static_cast<int>(NumElts)) {
        MaxV1Elt = std::max(MaxV1Elt, Mask[I]);
        auto It = find_if(V1, [&](const std::pair<int, int> &A) {
          return Mask[I] == A.first;
        });
        if (It != V1.end())
          ReconstructMask.push_back(It - V1.begin());
        else {
          ReconstructMask.push_back(V1.size());
          V1.emplace_back(Mask[I], V1.size());
        }
      } else {
        MaxV2Elt = std::max<int>(MaxV2Elt, Mask[I] - NumElts);
        auto It = find_if(V2, [&](const std::pair<int, int> &A) {
          return Mask[I] - static_cast<int>(NumElts) == A.first;
        });
        if (It != V2.end())
          ReconstructMask.push_back(NumElts + It - V2.begin());
        else {
          ReconstructMask.push_back(NumElts + V2.size());
          V2.emplace_back(Mask[I] - NumElts, NumElts + V2.size());
        }
      }
    }

    // For reductions, we know that the lane ordering out doesn't alter the
    // result. In-order can help simplify the shuffle away.
    if (FromReduction)
      sort(ReconstructMask);
    OrigReconstructMasks.push_back(std::move(ReconstructMask));
  }

  // If the Maximum element used from V1 and V2 are not larger than the new
  // vectors, the vectors are already packes and performing the optimization
  // again will likely not help any further. This also prevents us from getting
  // stuck in a cycle in case the costs do not also rule it out.
  if (V1.empty() || V2.empty() ||
      (MaxV1Elt == static_cast<int>(V1.size()) - 1 &&
       MaxV2Elt == static_cast<int>(V2.size()) - 1))
    return false;

  // GetBaseMaskValue takes one of the inputs, which may either be a shuffle, a
  // shuffle of another shuffle, or not a shuffle (that is treated like a
  // identity shuffle).
  auto GetBaseMaskValue = [&](Instruction *I, int M) {
    auto *SV = dyn_cast<ShuffleVectorInst>(I);
    if (!SV)
      return M;
    if (isa<UndefValue>(SV->getOperand(1)))
      if (auto *SSV = dyn_cast<ShuffleVectorInst>(SV->getOperand(0)))
        if (InputShuffles.contains(SSV))
          return SSV->getMaskValue(SV->getMaskValue(M));
    return SV->getMaskValue(M);
  };

  // Attempt to sort the inputs my ascending mask values to make simpler input
  // shuffles and push complex shuffles down to the uses. We sort on the first
  // of the two input shuffle orders, to try and get at least one input into a
  // nice order.
  auto SortBase = [&](Instruction *A, std::pair<int, int> X,
                      std::pair<int, int> Y) {
    int MXA = GetBaseMaskValue(A, X.first);
    int MYA = GetBaseMaskValue(A, Y.first);
    return MXA < MYA;
  };
  stable_sort(V1, [&](std::pair<int, int> A, std::pair<int, int> B) {
    return SortBase(SVI0A, A, B);
  });
  stable_sort(V2, [&](std::pair<int, int> A, std::pair<int, int> B) {
    return SortBase(SVI1A, A, B);
  });
  // Calculate our ReconstructMasks from the OrigReconstructMasks and the
  // modified order of the input shuffles.
  SmallVector<SmallVector<int>> ReconstructMasks;
  for (const auto &Mask : OrigReconstructMasks) {
    SmallVector<int> ReconstructMask;
    for (int M : Mask) {
      auto FindIndex = [](const SmallVector<std::pair<int, int>> &V, int M) {
        auto It = find_if(V, [M](auto A) { return A.second == M; });
        assert(It != V.end() && "Expected all entries in Mask");
        return std::distance(V.begin(), It);
      };
      if (M < 0)
        ReconstructMask.push_back(-1);
      else if (M < static_cast<int>(NumElts)) {
        ReconstructMask.push_back(FindIndex(V1, M));
      } else {
        ReconstructMask.push_back(NumElts + FindIndex(V2, M));
      }
    }
    ReconstructMasks.push_back(std::move(ReconstructMask));
  }

  // Calculate the masks needed for the new input shuffles, which get padded
  // with undef
  SmallVector<int> V1A, V1B, V2A, V2B;
  for (unsigned I = 0; I < V1.size(); I++) {
    V1A.push_back(GetBaseMaskValue(SVI0A, V1[I].first));
    V1B.push_back(GetBaseMaskValue(SVI0B, V1[I].first));
  }
  for (unsigned I = 0; I < V2.size(); I++) {
    V2A.push_back(GetBaseMaskValue(SVI1A, V2[I].first));
    V2B.push_back(GetBaseMaskValue(SVI1B, V2[I].first));
  }
  while (V1A.size() < NumElts) {
    V1A.push_back(PoisonMaskElem);
    V1B.push_back(PoisonMaskElem);
  }
  while (V2A.size() < NumElts) {
    V2A.push_back(PoisonMaskElem);
    V2B.push_back(PoisonMaskElem);
  }

  auto AddShuffleCost = [&](InstructionCost C, Instruction *I) {
    auto *SV = dyn_cast<ShuffleVectorInst>(I);
    if (!SV)
      return C;
    return C + TTI.getShuffleCost(isa<UndefValue>(SV->getOperand(1))
                                      ? TTI::SK_PermuteSingleSrc
                                      : TTI::SK_PermuteTwoSrc,
                                  VT, SV->getShuffleMask());
  };
  auto AddShuffleMaskCost = [&](InstructionCost C, ArrayRef<int> Mask) {
    return C + TTI.getShuffleCost(TTI::SK_PermuteTwoSrc, VT, Mask);
  };

  // Get the costs of the shuffles + binops before and after with the new
  // shuffle masks.
  InstructionCost CostBefore =
      TTI.getArithmeticInstrCost(Op0->getOpcode(), VT) +
      TTI.getArithmeticInstrCost(Op1->getOpcode(), VT);
  CostBefore += std::accumulate(Shuffles.begin(), Shuffles.end(),
                                InstructionCost(0), AddShuffleCost);
  CostBefore += std::accumulate(InputShuffles.begin(), InputShuffles.end(),
                                InstructionCost(0), AddShuffleCost);

  // The new binops will be unused for lanes past the used shuffle lengths.
  // These types attempt to get the correct cost for that from the target.
  FixedVectorType *Op0SmallVT =
      FixedVectorType::get(VT->getScalarType(), V1.size());
  FixedVectorType *Op1SmallVT =
      FixedVectorType::get(VT->getScalarType(), V2.size());
  InstructionCost CostAfter =
      TTI.getArithmeticInstrCost(Op0->getOpcode(), Op0SmallVT) +
      TTI.getArithmeticInstrCost(Op1->getOpcode(), Op1SmallVT);
  CostAfter += std::accumulate(ReconstructMasks.begin(), ReconstructMasks.end(),
                               InstructionCost(0), AddShuffleMaskCost);
  std::set<SmallVector<int>> OutputShuffleMasks({V1A, V1B, V2A, V2B});
  CostAfter +=
      std::accumulate(OutputShuffleMasks.begin(), OutputShuffleMasks.end(),
                      InstructionCost(0), AddShuffleMaskCost);

  LLVM_DEBUG(dbgs() << "Found a binop select shuffle pattern: " << I << "\n");
  LLVM_DEBUG(dbgs() << "  CostBefore: " << CostBefore
                    << " vs CostAfter: " << CostAfter << "\n");
  if (CostBefore <= CostAfter)
    return false;

  // The cost model has passed, create the new instructions.
  auto GetShuffleOperand = [&](Instruction *I, unsigned Op) -> Value * {
    auto *SV = dyn_cast<ShuffleVectorInst>(I);
    if (!SV)
      return I;
    if (isa<UndefValue>(SV->getOperand(1)))
      if (auto *SSV = dyn_cast<ShuffleVectorInst>(SV->getOperand(0)))
        if (InputShuffles.contains(SSV))
          return SSV->getOperand(Op);
    return SV->getOperand(Op);
  };
  Builder.SetInsertPoint(*SVI0A->getInsertionPointAfterDef());
  Value *NSV0A = Builder.CreateShuffleVector(GetShuffleOperand(SVI0A, 0),
                                             GetShuffleOperand(SVI0A, 1), V1A);
  Builder.SetInsertPoint(*SVI0B->getInsertionPointAfterDef());
  Value *NSV0B = Builder.CreateShuffleVector(GetShuffleOperand(SVI0B, 0),
                                             GetShuffleOperand(SVI0B, 1), V1B);
  Builder.SetInsertPoint(*SVI1A->getInsertionPointAfterDef());
  Value *NSV1A = Builder.CreateShuffleVector(GetShuffleOperand(SVI1A, 0),
                                             GetShuffleOperand(SVI1A, 1), V2A);
  Builder.SetInsertPoint(*SVI1B->getInsertionPointAfterDef());
  Value *NSV1B = Builder.CreateShuffleVector(GetShuffleOperand(SVI1B, 0),
                                             GetShuffleOperand(SVI1B, 1), V2B);
  Builder.SetInsertPoint(Op0);
  Value *NOp0 = Builder.CreateBinOp((Instruction::BinaryOps)Op0->getOpcode(),
                                    NSV0A, NSV0B);
  if (auto *I = dyn_cast<Instruction>(NOp0))
    I->copyIRFlags(Op0, true);
  Builder.SetInsertPoint(Op1);
  Value *NOp1 = Builder.CreateBinOp((Instruction::BinaryOps)Op1->getOpcode(),
                                    NSV1A, NSV1B);
  if (auto *I = dyn_cast<Instruction>(NOp1))
    I->copyIRFlags(Op1, true);

  for (int S = 0, E = ReconstructMasks.size(); S != E; S++) {
    Builder.SetInsertPoint(Shuffles[S]);
    Value *NSV = Builder.CreateShuffleVector(NOp0, NOp1, ReconstructMasks[S]);
    replaceValue(*Shuffles[S], *NSV);
  }

  Worklist.pushValue(NSV0A);
  Worklist.pushValue(NSV0B);
  Worklist.pushValue(NSV1A);
  Worklist.pushValue(NSV1B);
  for (auto *S : Shuffles)
    Worklist.add(S);
  return true;
}

/// This is the entry point for all transforms. Pass manager differences are
/// handled in the callers of this function.
bool VectorCombine::run() {
  if (DisableVectorCombine)
    return false;

  // Don't attempt vectorization if the target does not support vectors.
  if (!TTI.getNumberOfRegisters(TTI.getRegisterClassForType(/*Vector*/ true)))
    return false;

  bool MadeChange = false;
  auto FoldInst = [this, &MadeChange](Instruction &I) {
    Builder.SetInsertPoint(&I);
    bool IsFixedVectorType = isa<FixedVectorType>(I.getType());
    auto Opcode = I.getOpcode();

    // These folds should be beneficial regardless of when this pass is run
    // in the optimization pipeline.
    // The type checking is for run-time efficiency. We can avoid wasting time
    // dispatching to folding functions if there's no chance of matching.
    if (IsFixedVectorType) {
      switch (Opcode) {
      case Instruction::InsertElement:
        MadeChange |= vectorizeLoadInsert(I);
        break;
      case Instruction::ShuffleVector:
        MadeChange |= widenSubvectorLoad(I);
        break;
      default:
        break;
      }
    }

    // This transform works with scalable and fixed vectors
    // TODO: Identify and allow other scalable transforms
    if (isa<VectorType>(I.getType())) {
      MadeChange |= scalarizeBinopOrCmp(I);
      MadeChange |= scalarizeLoadExtract(I);
      MadeChange |= scalarizeVPIntrinsic(I);
    }

    if (Opcode == Instruction::Store)
      MadeChange |= foldSingleElementStore(I);

    // If this is an early pipeline invocation of this pass, we are done.
    if (TryEarlyFoldsOnly)
      return;

    // Otherwise, try folds that improve codegen but may interfere with
    // early IR canonicalizations.
    // The type checking is for run-time efficiency. We can avoid wasting time
    // dispatching to folding functions if there's no chance of matching.
    if (IsFixedVectorType) {
      switch (Opcode) {
      case Instruction::InsertElement:
        MadeChange |= foldInsExtFNeg(I);
        break;
      case Instruction::ShuffleVector:
        MadeChange |= foldShuffleOfBinops(I);
        MadeChange |= foldShuffleOfCastops(I);
        MadeChange |= foldShuffleOfShuffles(I);
        MadeChange |= foldSelectShuffle(I);
        MadeChange |= foldShuffleToIdentity(I);
        break;
      case Instruction::BitCast:
        MadeChange |= foldBitcastShuffle(I);
        break;
      }
    } else {
      switch (Opcode) {
      case Instruction::Call:
        MadeChange |= foldShuffleFromReductions(I);
        MadeChange |= foldCastFromReductions(I);
        break;
      case Instruction::ICmp:
      case Instruction::FCmp:
        MadeChange |= foldExtractExtract(I);
        break;
      default:
        if (Instruction::isBinaryOp(Opcode)) {
          MadeChange |= foldExtractExtract(I);
          MadeChange |= foldExtractedCmps(I);
        }
        break;
      }
    }
  };

  for (BasicBlock &BB : F) {
    // Ignore unreachable basic blocks.
    if (!DT.isReachableFromEntry(&BB))
      continue;
    // Use early increment range so that we can erase instructions in loop.
    for (Instruction &I : make_early_inc_range(BB)) {
      if (I.isDebugOrPseudoInst())
        continue;
      FoldInst(I);
    }
  }

  while (!Worklist.isEmpty()) {
    Instruction *I = Worklist.removeOne();
    if (!I)
      continue;

    if (isInstructionTriviallyDead(I)) {
      eraseInstruction(*I);
      continue;
    }

    FoldInst(*I);
  }

  return MadeChange;
}

PreservedAnalyses VectorCombinePass::run(Function &F,
                                         FunctionAnalysisManager &FAM) {
  auto &AC = FAM.getResult<AssumptionAnalysis>(F);
  TargetTransformInfo &TTI = FAM.getResult<TargetIRAnalysis>(F);
  DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  AAResults &AA = FAM.getResult<AAManager>(F);
  const DataLayout *DL = &F.getDataLayout();
  VectorCombine Combiner(F, TTI, DT, AA, AC, DL, TryEarlyFoldsOnly);
  if (!Combiner.run())
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
