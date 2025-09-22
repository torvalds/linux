//===- LoopIdiomRecognize.cpp - Loop idiom recognition --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements an idiom recognizer that transforms simple loops into a
// non-loop form.  In cases that this kicks in, it can be a significant
// performance win.
//
// If compiling for code size we avoid idiom recognition if the resulting
// code could be larger than the code for the original loop. One way this could
// happen is if the loop is not removable after idiom recognition due to the
// presence of non-idiom instructions. The initial implementation of the
// heuristics applies to idioms in multi-block loops.
//
//===----------------------------------------------------------------------===//
//
// TODO List:
//
// Future loop memory idioms to recognize:
//   memcmp, strlen, etc.
//
// This could recognize common matrix multiplies and dot product idioms and
// replace them with calls to BLAS (if linked in??).
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "loop-idiom"

STATISTIC(NumMemSet, "Number of memset's formed from loop stores");
STATISTIC(NumMemCpy, "Number of memcpy's formed from loop load+stores");
STATISTIC(NumMemMove, "Number of memmove's formed from loop load+stores");
STATISTIC(
    NumShiftUntilBitTest,
    "Number of uncountable loops recognized as 'shift until bitttest' idiom");
STATISTIC(NumShiftUntilZero,
          "Number of uncountable loops recognized as 'shift until zero' idiom");

bool DisableLIRP::All;
static cl::opt<bool, true>
    DisableLIRPAll("disable-" DEBUG_TYPE "-all",
                   cl::desc("Options to disable Loop Idiom Recognize Pass."),
                   cl::location(DisableLIRP::All), cl::init(false),
                   cl::ReallyHidden);

bool DisableLIRP::Memset;
static cl::opt<bool, true>
    DisableLIRPMemset("disable-" DEBUG_TYPE "-memset",
                      cl::desc("Proceed with loop idiom recognize pass, but do "
                               "not convert loop(s) to memset."),
                      cl::location(DisableLIRP::Memset), cl::init(false),
                      cl::ReallyHidden);

bool DisableLIRP::Memcpy;
static cl::opt<bool, true>
    DisableLIRPMemcpy("disable-" DEBUG_TYPE "-memcpy",
                      cl::desc("Proceed with loop idiom recognize pass, but do "
                               "not convert loop(s) to memcpy."),
                      cl::location(DisableLIRP::Memcpy), cl::init(false),
                      cl::ReallyHidden);

static cl::opt<bool> UseLIRCodeSizeHeurs(
    "use-lir-code-size-heurs",
    cl::desc("Use loop idiom recognition code size heuristics when compiling"
             "with -Os/-Oz"),
    cl::init(true), cl::Hidden);

namespace {

class LoopIdiomRecognize {
  Loop *CurLoop = nullptr;
  AliasAnalysis *AA;
  DominatorTree *DT;
  LoopInfo *LI;
  ScalarEvolution *SE;
  TargetLibraryInfo *TLI;
  const TargetTransformInfo *TTI;
  const DataLayout *DL;
  OptimizationRemarkEmitter &ORE;
  bool ApplyCodeSizeHeuristics;
  std::unique_ptr<MemorySSAUpdater> MSSAU;

public:
  explicit LoopIdiomRecognize(AliasAnalysis *AA, DominatorTree *DT,
                              LoopInfo *LI, ScalarEvolution *SE,
                              TargetLibraryInfo *TLI,
                              const TargetTransformInfo *TTI, MemorySSA *MSSA,
                              const DataLayout *DL,
                              OptimizationRemarkEmitter &ORE)
      : AA(AA), DT(DT), LI(LI), SE(SE), TLI(TLI), TTI(TTI), DL(DL), ORE(ORE) {
    if (MSSA)
      MSSAU = std::make_unique<MemorySSAUpdater>(MSSA);
  }

  bool runOnLoop(Loop *L);

private:
  using StoreList = SmallVector<StoreInst *, 8>;
  using StoreListMap = MapVector<Value *, StoreList>;

  StoreListMap StoreRefsForMemset;
  StoreListMap StoreRefsForMemsetPattern;
  StoreList StoreRefsForMemcpy;
  bool HasMemset;
  bool HasMemsetPattern;
  bool HasMemcpy;

  /// Return code for isLegalStore()
  enum LegalStoreKind {
    None = 0,
    Memset,
    MemsetPattern,
    Memcpy,
    UnorderedAtomicMemcpy,
    DontUse // Dummy retval never to be used. Allows catching errors in retval
            // handling.
  };

  /// \name Countable Loop Idiom Handling
  /// @{

  bool runOnCountableLoop();
  bool runOnLoopBlock(BasicBlock *BB, const SCEV *BECount,
                      SmallVectorImpl<BasicBlock *> &ExitBlocks);

  void collectStores(BasicBlock *BB);
  LegalStoreKind isLegalStore(StoreInst *SI);
  enum class ForMemset { No, Yes };
  bool processLoopStores(SmallVectorImpl<StoreInst *> &SL, const SCEV *BECount,
                         ForMemset For);

  template <typename MemInst>
  bool processLoopMemIntrinsic(
      BasicBlock *BB,
      bool (LoopIdiomRecognize::*Processor)(MemInst *, const SCEV *),
      const SCEV *BECount);
  bool processLoopMemCpy(MemCpyInst *MCI, const SCEV *BECount);
  bool processLoopMemSet(MemSetInst *MSI, const SCEV *BECount);

  bool processLoopStridedStore(Value *DestPtr, const SCEV *StoreSizeSCEV,
                               MaybeAlign StoreAlignment, Value *StoredVal,
                               Instruction *TheStore,
                               SmallPtrSetImpl<Instruction *> &Stores,
                               const SCEVAddRecExpr *Ev, const SCEV *BECount,
                               bool IsNegStride, bool IsLoopMemset = false);
  bool processLoopStoreOfLoopLoad(StoreInst *SI, const SCEV *BECount);
  bool processLoopStoreOfLoopLoad(Value *DestPtr, Value *SourcePtr,
                                  const SCEV *StoreSize, MaybeAlign StoreAlign,
                                  MaybeAlign LoadAlign, Instruction *TheStore,
                                  Instruction *TheLoad,
                                  const SCEVAddRecExpr *StoreEv,
                                  const SCEVAddRecExpr *LoadEv,
                                  const SCEV *BECount);
  bool avoidLIRForMultiBlockLoop(bool IsMemset = false,
                                 bool IsLoopMemset = false);

  /// @}
  /// \name Noncountable Loop Idiom Handling
  /// @{

  bool runOnNoncountableLoop();

  bool recognizePopcount();
  void transformLoopToPopcount(BasicBlock *PreCondBB, Instruction *CntInst,
                               PHINode *CntPhi, Value *Var);
  bool isProfitableToInsertFFS(Intrinsic::ID IntrinID, Value *InitX,
                               bool ZeroCheck, size_t CanonicalSize);
  bool insertFFSIfProfitable(Intrinsic::ID IntrinID, Value *InitX,
                             Instruction *DefX, PHINode *CntPhi,
                             Instruction *CntInst);
  bool recognizeAndInsertFFS();  /// Find First Set: ctlz or cttz
  bool recognizeShiftUntilLessThan();
  void transformLoopToCountable(Intrinsic::ID IntrinID, BasicBlock *PreCondBB,
                                Instruction *CntInst, PHINode *CntPhi,
                                Value *Var, Instruction *DefX,
                                const DebugLoc &DL, bool ZeroCheck,
                                bool IsCntPhiUsedOutsideLoop,
                                bool InsertSub = false);

  bool recognizeShiftUntilBitTest();
  bool recognizeShiftUntilZero();

  /// @}
};
} // end anonymous namespace

PreservedAnalyses LoopIdiomRecognizePass::run(Loop &L, LoopAnalysisManager &AM,
                                              LoopStandardAnalysisResults &AR,
                                              LPMUpdater &) {
  if (DisableLIRP::All)
    return PreservedAnalyses::all();

  const auto *DL = &L.getHeader()->getDataLayout();

  // For the new PM, we also can't use OptimizationRemarkEmitter as an analysis
  // pass.  Function analyses need to be preserved across loop transformations
  // but ORE cannot be preserved (see comment before the pass definition).
  OptimizationRemarkEmitter ORE(L.getHeader()->getParent());

  LoopIdiomRecognize LIR(&AR.AA, &AR.DT, &AR.LI, &AR.SE, &AR.TLI, &AR.TTI,
                         AR.MSSA, DL, ORE);
  if (!LIR.runOnLoop(&L))
    return PreservedAnalyses::all();

  auto PA = getLoopPassPreservedAnalyses();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}

static void deleteDeadInstruction(Instruction *I) {
  I->replaceAllUsesWith(PoisonValue::get(I->getType()));
  I->eraseFromParent();
}

//===----------------------------------------------------------------------===//
//
//          Implementation of LoopIdiomRecognize
//
//===----------------------------------------------------------------------===//

bool LoopIdiomRecognize::runOnLoop(Loop *L) {
  CurLoop = L;
  // If the loop could not be converted to canonical form, it must have an
  // indirectbr in it, just give up.
  if (!L->getLoopPreheader())
    return false;

  // Disable loop idiom recognition if the function's name is a common idiom.
  StringRef Name = L->getHeader()->getParent()->getName();
  if (Name == "memset" || Name == "memcpy")
    return false;
  if (Name == "_libc_memset" || Name == "_libc_memcpy")
    return false;

  // Determine if code size heuristics need to be applied.
  ApplyCodeSizeHeuristics =
      L->getHeader()->getParent()->hasOptSize() && UseLIRCodeSizeHeurs;

  HasMemset = TLI->has(LibFunc_memset);
  HasMemsetPattern = TLI->has(LibFunc_memset_pattern16);
  HasMemcpy = TLI->has(LibFunc_memcpy);

  if (HasMemset || HasMemsetPattern || HasMemcpy)
    if (SE->hasLoopInvariantBackedgeTakenCount(L))
      return runOnCountableLoop();

  return runOnNoncountableLoop();
}

bool LoopIdiomRecognize::runOnCountableLoop() {
  const SCEV *BECount = SE->getBackedgeTakenCount(CurLoop);
  assert(!isa<SCEVCouldNotCompute>(BECount) &&
         "runOnCountableLoop() called on a loop without a predictable"
         "backedge-taken count");

  // If this loop executes exactly one time, then it should be peeled, not
  // optimized by this pass.
  if (const SCEVConstant *BECst = dyn_cast<SCEVConstant>(BECount))
    if (BECst->getAPInt() == 0)
      return false;

  SmallVector<BasicBlock *, 8> ExitBlocks;
  CurLoop->getUniqueExitBlocks(ExitBlocks);

  LLVM_DEBUG(dbgs() << DEBUG_TYPE " Scanning: F["
                    << CurLoop->getHeader()->getParent()->getName()
                    << "] Countable Loop %" << CurLoop->getHeader()->getName()
                    << "\n");

  // The following transforms hoist stores/memsets into the loop pre-header.
  // Give up if the loop has instructions that may throw.
  SimpleLoopSafetyInfo SafetyInfo;
  SafetyInfo.computeLoopSafetyInfo(CurLoop);
  if (SafetyInfo.anyBlockMayThrow())
    return false;

  bool MadeChange = false;

  // Scan all the blocks in the loop that are not in subloops.
  for (auto *BB : CurLoop->getBlocks()) {
    // Ignore blocks in subloops.
    if (LI->getLoopFor(BB) != CurLoop)
      continue;

    MadeChange |= runOnLoopBlock(BB, BECount, ExitBlocks);
  }
  return MadeChange;
}

static APInt getStoreStride(const SCEVAddRecExpr *StoreEv) {
  const SCEVConstant *ConstStride = cast<SCEVConstant>(StoreEv->getOperand(1));
  return ConstStride->getAPInt();
}

/// getMemSetPatternValue - If a strided store of the specified value is safe to
/// turn into a memset_pattern16, return a ConstantArray of 16 bytes that should
/// be passed in.  Otherwise, return null.
///
/// Note that we don't ever attempt to use memset_pattern8 or 4, because these
/// just replicate their input array and then pass on to memset_pattern16.
static Constant *getMemSetPatternValue(Value *V, const DataLayout *DL) {
  // FIXME: This could check for UndefValue because it can be merged into any
  // other valid pattern.

  // If the value isn't a constant, we can't promote it to being in a constant
  // array.  We could theoretically do a store to an alloca or something, but
  // that doesn't seem worthwhile.
  Constant *C = dyn_cast<Constant>(V);
  if (!C || isa<ConstantExpr>(C))
    return nullptr;

  // Only handle simple values that are a power of two bytes in size.
  uint64_t Size = DL->getTypeSizeInBits(V->getType());
  if (Size == 0 || (Size & 7) || (Size & (Size - 1)))
    return nullptr;

  // Don't care enough about darwin/ppc to implement this.
  if (DL->isBigEndian())
    return nullptr;

  // Convert to size in bytes.
  Size /= 8;

  // TODO: If CI is larger than 16-bytes, we can try slicing it in half to see
  // if the top and bottom are the same (e.g. for vectors and large integers).
  if (Size > 16)
    return nullptr;

  // If the constant is exactly 16 bytes, just use it.
  if (Size == 16)
    return C;

  // Otherwise, we'll use an array of the constants.
  unsigned ArraySize = 16 / Size;
  ArrayType *AT = ArrayType::get(V->getType(), ArraySize);
  return ConstantArray::get(AT, std::vector<Constant *>(ArraySize, C));
}

LoopIdiomRecognize::LegalStoreKind
LoopIdiomRecognize::isLegalStore(StoreInst *SI) {
  // Don't touch volatile stores.
  if (SI->isVolatile())
    return LegalStoreKind::None;
  // We only want simple or unordered-atomic stores.
  if (!SI->isUnordered())
    return LegalStoreKind::None;

  // Avoid merging nontemporal stores.
  if (SI->getMetadata(LLVMContext::MD_nontemporal))
    return LegalStoreKind::None;

  Value *StoredVal = SI->getValueOperand();
  Value *StorePtr = SI->getPointerOperand();

  // Don't convert stores of non-integral pointer types to memsets (which stores
  // integers).
  if (DL->isNonIntegralPointerType(StoredVal->getType()->getScalarType()))
    return LegalStoreKind::None;

  // Reject stores that are so large that they overflow an unsigned.
  // When storing out scalable vectors we bail out for now, since the code
  // below currently only works for constant strides.
  TypeSize SizeInBits = DL->getTypeSizeInBits(StoredVal->getType());
  if (SizeInBits.isScalable() || (SizeInBits.getFixedValue() & 7) ||
      (SizeInBits.getFixedValue() >> 32) != 0)
    return LegalStoreKind::None;

  // See if the pointer expression is an AddRec like {base,+,1} on the current
  // loop, which indicates a strided store.  If we have something else, it's a
  // random store we can't handle.
  const SCEVAddRecExpr *StoreEv =
      dyn_cast<SCEVAddRecExpr>(SE->getSCEV(StorePtr));
  if (!StoreEv || StoreEv->getLoop() != CurLoop || !StoreEv->isAffine())
    return LegalStoreKind::None;

  // Check to see if we have a constant stride.
  if (!isa<SCEVConstant>(StoreEv->getOperand(1)))
    return LegalStoreKind::None;

  // See if the store can be turned into a memset.

  // If the stored value is a byte-wise value (like i32 -1), then it may be
  // turned into a memset of i8 -1, assuming that all the consecutive bytes
  // are stored.  A store of i32 0x01020304 can never be turned into a memset,
  // but it can be turned into memset_pattern if the target supports it.
  Value *SplatValue = isBytewiseValue(StoredVal, *DL);

  // Note: memset and memset_pattern on unordered-atomic is yet not supported
  bool UnorderedAtomic = SI->isUnordered() && !SI->isSimple();

  // If we're allowed to form a memset, and the stored value would be
  // acceptable for memset, use it.
  if (!UnorderedAtomic && HasMemset && SplatValue && !DisableLIRP::Memset &&
      // Verify that the stored value is loop invariant.  If not, we can't
      // promote the memset.
      CurLoop->isLoopInvariant(SplatValue)) {
    // It looks like we can use SplatValue.
    return LegalStoreKind::Memset;
  }
  if (!UnorderedAtomic && HasMemsetPattern && !DisableLIRP::Memset &&
      // Don't create memset_pattern16s with address spaces.
      StorePtr->getType()->getPointerAddressSpace() == 0 &&
      getMemSetPatternValue(StoredVal, DL)) {
    // It looks like we can use PatternValue!
    return LegalStoreKind::MemsetPattern;
  }

  // Otherwise, see if the store can be turned into a memcpy.
  if (HasMemcpy && !DisableLIRP::Memcpy) {
    // Check to see if the stride matches the size of the store.  If so, then we
    // know that every byte is touched in the loop.
    APInt Stride = getStoreStride(StoreEv);
    unsigned StoreSize = DL->getTypeStoreSize(SI->getValueOperand()->getType());
    if (StoreSize != Stride && StoreSize != -Stride)
      return LegalStoreKind::None;

    // The store must be feeding a non-volatile load.
    LoadInst *LI = dyn_cast<LoadInst>(SI->getValueOperand());

    // Only allow non-volatile loads
    if (!LI || LI->isVolatile())
      return LegalStoreKind::None;
    // Only allow simple or unordered-atomic loads
    if (!LI->isUnordered())
      return LegalStoreKind::None;

    // See if the pointer expression is an AddRec like {base,+,1} on the current
    // loop, which indicates a strided load.  If we have something else, it's a
    // random load we can't handle.
    const SCEVAddRecExpr *LoadEv =
        dyn_cast<SCEVAddRecExpr>(SE->getSCEV(LI->getPointerOperand()));
    if (!LoadEv || LoadEv->getLoop() != CurLoop || !LoadEv->isAffine())
      return LegalStoreKind::None;

    // The store and load must share the same stride.
    if (StoreEv->getOperand(1) != LoadEv->getOperand(1))
      return LegalStoreKind::None;

    // Success.  This store can be converted into a memcpy.
    UnorderedAtomic = UnorderedAtomic || LI->isAtomic();
    return UnorderedAtomic ? LegalStoreKind::UnorderedAtomicMemcpy
                           : LegalStoreKind::Memcpy;
  }
  // This store can't be transformed into a memset/memcpy.
  return LegalStoreKind::None;
}

void LoopIdiomRecognize::collectStores(BasicBlock *BB) {
  StoreRefsForMemset.clear();
  StoreRefsForMemsetPattern.clear();
  StoreRefsForMemcpy.clear();
  for (Instruction &I : *BB) {
    StoreInst *SI = dyn_cast<StoreInst>(&I);
    if (!SI)
      continue;

    // Make sure this is a strided store with a constant stride.
    switch (isLegalStore(SI)) {
    case LegalStoreKind::None:
      // Nothing to do
      break;
    case LegalStoreKind::Memset: {
      // Find the base pointer.
      Value *Ptr = getUnderlyingObject(SI->getPointerOperand());
      StoreRefsForMemset[Ptr].push_back(SI);
    } break;
    case LegalStoreKind::MemsetPattern: {
      // Find the base pointer.
      Value *Ptr = getUnderlyingObject(SI->getPointerOperand());
      StoreRefsForMemsetPattern[Ptr].push_back(SI);
    } break;
    case LegalStoreKind::Memcpy:
    case LegalStoreKind::UnorderedAtomicMemcpy:
      StoreRefsForMemcpy.push_back(SI);
      break;
    default:
      assert(false && "unhandled return value");
      break;
    }
  }
}

/// runOnLoopBlock - Process the specified block, which lives in a counted loop
/// with the specified backedge count.  This block is known to be in the current
/// loop and not in any subloops.
bool LoopIdiomRecognize::runOnLoopBlock(
    BasicBlock *BB, const SCEV *BECount,
    SmallVectorImpl<BasicBlock *> &ExitBlocks) {
  // We can only promote stores in this block if they are unconditionally
  // executed in the loop.  For a block to be unconditionally executed, it has
  // to dominate all the exit blocks of the loop.  Verify this now.
  for (BasicBlock *ExitBlock : ExitBlocks)
    if (!DT->dominates(BB, ExitBlock))
      return false;

  bool MadeChange = false;
  // Look for store instructions, which may be optimized to memset/memcpy.
  collectStores(BB);

  // Look for a single store or sets of stores with a common base, which can be
  // optimized into a memset (memset_pattern).  The latter most commonly happens
  // with structs and handunrolled loops.
  for (auto &SL : StoreRefsForMemset)
    MadeChange |= processLoopStores(SL.second, BECount, ForMemset::Yes);

  for (auto &SL : StoreRefsForMemsetPattern)
    MadeChange |= processLoopStores(SL.second, BECount, ForMemset::No);

  // Optimize the store into a memcpy, if it feeds an similarly strided load.
  for (auto &SI : StoreRefsForMemcpy)
    MadeChange |= processLoopStoreOfLoopLoad(SI, BECount);

  MadeChange |= processLoopMemIntrinsic<MemCpyInst>(
      BB, &LoopIdiomRecognize::processLoopMemCpy, BECount);
  MadeChange |= processLoopMemIntrinsic<MemSetInst>(
      BB, &LoopIdiomRecognize::processLoopMemSet, BECount);

  return MadeChange;
}

/// See if this store(s) can be promoted to a memset.
bool LoopIdiomRecognize::processLoopStores(SmallVectorImpl<StoreInst *> &SL,
                                           const SCEV *BECount, ForMemset For) {
  // Try to find consecutive stores that can be transformed into memsets.
  SetVector<StoreInst *> Heads, Tails;
  SmallDenseMap<StoreInst *, StoreInst *> ConsecutiveChain;

  // Do a quadratic search on all of the given stores and find
  // all of the pairs of stores that follow each other.
  SmallVector<unsigned, 16> IndexQueue;
  for (unsigned i = 0, e = SL.size(); i < e; ++i) {
    assert(SL[i]->isSimple() && "Expected only non-volatile stores.");

    Value *FirstStoredVal = SL[i]->getValueOperand();
    Value *FirstStorePtr = SL[i]->getPointerOperand();
    const SCEVAddRecExpr *FirstStoreEv =
        cast<SCEVAddRecExpr>(SE->getSCEV(FirstStorePtr));
    APInt FirstStride = getStoreStride(FirstStoreEv);
    unsigned FirstStoreSize = DL->getTypeStoreSize(SL[i]->getValueOperand()->getType());

    // See if we can optimize just this store in isolation.
    if (FirstStride == FirstStoreSize || -FirstStride == FirstStoreSize) {
      Heads.insert(SL[i]);
      continue;
    }

    Value *FirstSplatValue = nullptr;
    Constant *FirstPatternValue = nullptr;

    if (For == ForMemset::Yes)
      FirstSplatValue = isBytewiseValue(FirstStoredVal, *DL);
    else
      FirstPatternValue = getMemSetPatternValue(FirstStoredVal, DL);

    assert((FirstSplatValue || FirstPatternValue) &&
           "Expected either splat value or pattern value.");

    IndexQueue.clear();
    // If a store has multiple consecutive store candidates, search Stores
    // array according to the sequence: from i+1 to e, then from i-1 to 0.
    // This is because usually pairing with immediate succeeding or preceding
    // candidate create the best chance to find memset opportunity.
    unsigned j = 0;
    for (j = i + 1; j < e; ++j)
      IndexQueue.push_back(j);
    for (j = i; j > 0; --j)
      IndexQueue.push_back(j - 1);

    for (auto &k : IndexQueue) {
      assert(SL[k]->isSimple() && "Expected only non-volatile stores.");
      Value *SecondStorePtr = SL[k]->getPointerOperand();
      const SCEVAddRecExpr *SecondStoreEv =
          cast<SCEVAddRecExpr>(SE->getSCEV(SecondStorePtr));
      APInt SecondStride = getStoreStride(SecondStoreEv);

      if (FirstStride != SecondStride)
        continue;

      Value *SecondStoredVal = SL[k]->getValueOperand();
      Value *SecondSplatValue = nullptr;
      Constant *SecondPatternValue = nullptr;

      if (For == ForMemset::Yes)
        SecondSplatValue = isBytewiseValue(SecondStoredVal, *DL);
      else
        SecondPatternValue = getMemSetPatternValue(SecondStoredVal, DL);

      assert((SecondSplatValue || SecondPatternValue) &&
             "Expected either splat value or pattern value.");

      if (isConsecutiveAccess(SL[i], SL[k], *DL, *SE, false)) {
        if (For == ForMemset::Yes) {
          if (isa<UndefValue>(FirstSplatValue))
            FirstSplatValue = SecondSplatValue;
          if (FirstSplatValue != SecondSplatValue)
            continue;
        } else {
          if (isa<UndefValue>(FirstPatternValue))
            FirstPatternValue = SecondPatternValue;
          if (FirstPatternValue != SecondPatternValue)
            continue;
        }
        Tails.insert(SL[k]);
        Heads.insert(SL[i]);
        ConsecutiveChain[SL[i]] = SL[k];
        break;
      }
    }
  }

  // We may run into multiple chains that merge into a single chain. We mark the
  // stores that we transformed so that we don't visit the same store twice.
  SmallPtrSet<Value *, 16> TransformedStores;
  bool Changed = false;

  // For stores that start but don't end a link in the chain:
  for (StoreInst *I : Heads) {
    if (Tails.count(I))
      continue;

    // We found a store instr that starts a chain. Now follow the chain and try
    // to transform it.
    SmallPtrSet<Instruction *, 8> AdjacentStores;
    StoreInst *HeadStore = I;
    unsigned StoreSize = 0;

    // Collect the chain into a list.
    while (Tails.count(I) || Heads.count(I)) {
      if (TransformedStores.count(I))
        break;
      AdjacentStores.insert(I);

      StoreSize += DL->getTypeStoreSize(I->getValueOperand()->getType());
      // Move to the next value in the chain.
      I = ConsecutiveChain[I];
    }

    Value *StoredVal = HeadStore->getValueOperand();
    Value *StorePtr = HeadStore->getPointerOperand();
    const SCEVAddRecExpr *StoreEv = cast<SCEVAddRecExpr>(SE->getSCEV(StorePtr));
    APInt Stride = getStoreStride(StoreEv);

    // Check to see if the stride matches the size of the stores.  If so, then
    // we know that every byte is touched in the loop.
    if (StoreSize != Stride && StoreSize != -Stride)
      continue;

    bool IsNegStride = StoreSize == -Stride;

    Type *IntIdxTy = DL->getIndexType(StorePtr->getType());
    const SCEV *StoreSizeSCEV = SE->getConstant(IntIdxTy, StoreSize);
    if (processLoopStridedStore(StorePtr, StoreSizeSCEV,
                                MaybeAlign(HeadStore->getAlign()), StoredVal,
                                HeadStore, AdjacentStores, StoreEv, BECount,
                                IsNegStride)) {
      TransformedStores.insert(AdjacentStores.begin(), AdjacentStores.end());
      Changed = true;
    }
  }

  return Changed;
}

/// processLoopMemIntrinsic - Template function for calling different processor
/// functions based on mem intrinsic type.
template <typename MemInst>
bool LoopIdiomRecognize::processLoopMemIntrinsic(
    BasicBlock *BB,
    bool (LoopIdiomRecognize::*Processor)(MemInst *, const SCEV *),
    const SCEV *BECount) {
  bool MadeChange = false;
  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E;) {
    Instruction *Inst = &*I++;
    // Look for memory instructions, which may be optimized to a larger one.
    if (MemInst *MI = dyn_cast<MemInst>(Inst)) {
      WeakTrackingVH InstPtr(&*I);
      if (!(this->*Processor)(MI, BECount))
        continue;
      MadeChange = true;

      // If processing the instruction invalidated our iterator, start over from
      // the top of the block.
      if (!InstPtr)
        I = BB->begin();
    }
  }
  return MadeChange;
}

/// processLoopMemCpy - See if this memcpy can be promoted to a large memcpy
bool LoopIdiomRecognize::processLoopMemCpy(MemCpyInst *MCI,
                                           const SCEV *BECount) {
  // We can only handle non-volatile memcpys with a constant size.
  if (MCI->isVolatile() || !isa<ConstantInt>(MCI->getLength()))
    return false;

  // If we're not allowed to hack on memcpy, we fail.
  if ((!HasMemcpy && !isa<MemCpyInlineInst>(MCI)) || DisableLIRP::Memcpy)
    return false;

  Value *Dest = MCI->getDest();
  Value *Source = MCI->getSource();
  if (!Dest || !Source)
    return false;

  // See if the load and store pointer expressions are AddRec like {base,+,1} on
  // the current loop, which indicates a strided load and store.  If we have
  // something else, it's a random load or store we can't handle.
  const SCEVAddRecExpr *StoreEv = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(Dest));
  if (!StoreEv || StoreEv->getLoop() != CurLoop || !StoreEv->isAffine())
    return false;
  const SCEVAddRecExpr *LoadEv = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(Source));
  if (!LoadEv || LoadEv->getLoop() != CurLoop || !LoadEv->isAffine())
    return false;

  // Reject memcpys that are so large that they overflow an unsigned.
  uint64_t SizeInBytes = cast<ConstantInt>(MCI->getLength())->getZExtValue();
  if ((SizeInBytes >> 32) != 0)
    return false;

  // Check if the stride matches the size of the memcpy. If so, then we know
  // that every byte is touched in the loop.
  const SCEVConstant *ConstStoreStride =
      dyn_cast<SCEVConstant>(StoreEv->getOperand(1));
  const SCEVConstant *ConstLoadStride =
      dyn_cast<SCEVConstant>(LoadEv->getOperand(1));
  if (!ConstStoreStride || !ConstLoadStride)
    return false;

  APInt StoreStrideValue = ConstStoreStride->getAPInt();
  APInt LoadStrideValue = ConstLoadStride->getAPInt();
  // Huge stride value - give up
  if (StoreStrideValue.getBitWidth() > 64 || LoadStrideValue.getBitWidth() > 64)
    return false;

  if (SizeInBytes != StoreStrideValue && SizeInBytes != -StoreStrideValue) {
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "SizeStrideUnequal", MCI)
             << ore::NV("Inst", "memcpy") << " in "
             << ore::NV("Function", MCI->getFunction())
             << " function will not be hoisted: "
             << ore::NV("Reason", "memcpy size is not equal to stride");
    });
    return false;
  }

  int64_t StoreStrideInt = StoreStrideValue.getSExtValue();
  int64_t LoadStrideInt = LoadStrideValue.getSExtValue();
  // Check if the load stride matches the store stride.
  if (StoreStrideInt != LoadStrideInt)
    return false;

  return processLoopStoreOfLoopLoad(
      Dest, Source, SE->getConstant(Dest->getType(), SizeInBytes),
      MCI->getDestAlign(), MCI->getSourceAlign(), MCI, MCI, StoreEv, LoadEv,
      BECount);
}

/// processLoopMemSet - See if this memset can be promoted to a large memset.
bool LoopIdiomRecognize::processLoopMemSet(MemSetInst *MSI,
                                           const SCEV *BECount) {
  // We can only handle non-volatile memsets.
  if (MSI->isVolatile())
    return false;

  // If we're not allowed to hack on memset, we fail.
  if (!HasMemset || DisableLIRP::Memset)
    return false;

  Value *Pointer = MSI->getDest();

  // See if the pointer expression is an AddRec like {base,+,1} on the current
  // loop, which indicates a strided store.  If we have something else, it's a
  // random store we can't handle.
  const SCEVAddRecExpr *Ev = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(Pointer));
  if (!Ev || Ev->getLoop() != CurLoop)
    return false;
  if (!Ev->isAffine()) {
    LLVM_DEBUG(dbgs() << "  Pointer is not affine, abort\n");
    return false;
  }

  const SCEV *PointerStrideSCEV = Ev->getOperand(1);
  const SCEV *MemsetSizeSCEV = SE->getSCEV(MSI->getLength());
  if (!PointerStrideSCEV || !MemsetSizeSCEV)
    return false;

  bool IsNegStride = false;
  const bool IsConstantSize = isa<ConstantInt>(MSI->getLength());

  if (IsConstantSize) {
    // Memset size is constant.
    // Check if the pointer stride matches the memset size. If so, then
    // we know that every byte is touched in the loop.
    LLVM_DEBUG(dbgs() << "  memset size is constant\n");
    uint64_t SizeInBytes = cast<ConstantInt>(MSI->getLength())->getZExtValue();
    const SCEVConstant *ConstStride = dyn_cast<SCEVConstant>(Ev->getOperand(1));
    if (!ConstStride)
      return false;

    APInt Stride = ConstStride->getAPInt();
    if (SizeInBytes != Stride && SizeInBytes != -Stride)
      return false;

    IsNegStride = SizeInBytes == -Stride;
  } else {
    // Memset size is non-constant.
    // Check if the pointer stride matches the memset size.
    // To be conservative, the pass would not promote pointers that aren't in
    // address space zero. Also, the pass only handles memset length and stride
    // that are invariant for the top level loop.
    LLVM_DEBUG(dbgs() << "  memset size is non-constant\n");
    if (Pointer->getType()->getPointerAddressSpace() != 0) {
      LLVM_DEBUG(dbgs() << "  pointer is not in address space zero, "
                        << "abort\n");
      return false;
    }
    if (!SE->isLoopInvariant(MemsetSizeSCEV, CurLoop)) {
      LLVM_DEBUG(dbgs() << "  memset size is not a loop-invariant, "
                        << "abort\n");
      return false;
    }

    // Compare positive direction PointerStrideSCEV with MemsetSizeSCEV
    IsNegStride = PointerStrideSCEV->isNonConstantNegative();
    const SCEV *PositiveStrideSCEV =
        IsNegStride ? SE->getNegativeSCEV(PointerStrideSCEV)
                    : PointerStrideSCEV;
    LLVM_DEBUG(dbgs() << "  MemsetSizeSCEV: " << *MemsetSizeSCEV << "\n"
                      << "  PositiveStrideSCEV: " << *PositiveStrideSCEV
                      << "\n");

    if (PositiveStrideSCEV != MemsetSizeSCEV) {
      // If an expression is covered by the loop guard, compare again and
      // proceed with optimization if equal.
      const SCEV *FoldedPositiveStride =
          SE->applyLoopGuards(PositiveStrideSCEV, CurLoop);
      const SCEV *FoldedMemsetSize =
          SE->applyLoopGuards(MemsetSizeSCEV, CurLoop);

      LLVM_DEBUG(dbgs() << "  Try to fold SCEV based on loop guard\n"
                        << "    FoldedMemsetSize: " << *FoldedMemsetSize << "\n"
                        << "    FoldedPositiveStride: " << *FoldedPositiveStride
                        << "\n");

      if (FoldedPositiveStride != FoldedMemsetSize) {
        LLVM_DEBUG(dbgs() << "  SCEV don't match, abort\n");
        return false;
      }
    }
  }

  // Verify that the memset value is loop invariant.  If not, we can't promote
  // the memset.
  Value *SplatValue = MSI->getValue();
  if (!SplatValue || !CurLoop->isLoopInvariant(SplatValue))
    return false;

  SmallPtrSet<Instruction *, 1> MSIs;
  MSIs.insert(MSI);
  return processLoopStridedStore(Pointer, SE->getSCEV(MSI->getLength()),
                                 MSI->getDestAlign(), SplatValue, MSI, MSIs, Ev,
                                 BECount, IsNegStride, /*IsLoopMemset=*/true);
}

/// mayLoopAccessLocation - Return true if the specified loop might access the
/// specified pointer location, which is a loop-strided access.  The 'Access'
/// argument specifies what the verboten forms of access are (read or write).
static bool
mayLoopAccessLocation(Value *Ptr, ModRefInfo Access, Loop *L,
                      const SCEV *BECount, const SCEV *StoreSizeSCEV,
                      AliasAnalysis &AA,
                      SmallPtrSetImpl<Instruction *> &IgnoredInsts) {
  // Get the location that may be stored across the loop.  Since the access is
  // strided positively through memory, we say that the modified location starts
  // at the pointer and has infinite size.
  LocationSize AccessSize = LocationSize::afterPointer();

  // If the loop iterates a fixed number of times, we can refine the access size
  // to be exactly the size of the memset, which is (BECount+1)*StoreSize
  const SCEVConstant *BECst = dyn_cast<SCEVConstant>(BECount);
  const SCEVConstant *ConstSize = dyn_cast<SCEVConstant>(StoreSizeSCEV);
  if (BECst && ConstSize) {
    std::optional<uint64_t> BEInt = BECst->getAPInt().tryZExtValue();
    std::optional<uint64_t> SizeInt = ConstSize->getAPInt().tryZExtValue();
    // FIXME: Should this check for overflow?
    if (BEInt && SizeInt)
      AccessSize = LocationSize::precise((*BEInt + 1) * *SizeInt);
  }

  // TODO: For this to be really effective, we have to dive into the pointer
  // operand in the store.  Store to &A[i] of 100 will always return may alias
  // with store of &A[100], we need to StoreLoc to be "A" with size of 100,
  // which will then no-alias a store to &A[100].
  MemoryLocation StoreLoc(Ptr, AccessSize);

  for (BasicBlock *B : L->blocks())
    for (Instruction &I : *B)
      if (!IgnoredInsts.contains(&I) &&
          isModOrRefSet(AA.getModRefInfo(&I, StoreLoc) & Access))
        return true;
  return false;
}

// If we have a negative stride, Start refers to the end of the memory location
// we're trying to memset.  Therefore, we need to recompute the base pointer,
// which is just Start - BECount*Size.
static const SCEV *getStartForNegStride(const SCEV *Start, const SCEV *BECount,
                                        Type *IntPtr, const SCEV *StoreSizeSCEV,
                                        ScalarEvolution *SE) {
  const SCEV *Index = SE->getTruncateOrZeroExtend(BECount, IntPtr);
  if (!StoreSizeSCEV->isOne()) {
    // index = back edge count * store size
    Index = SE->getMulExpr(Index,
                           SE->getTruncateOrZeroExtend(StoreSizeSCEV, IntPtr),
                           SCEV::FlagNUW);
  }
  // base pointer = start - index * store size
  return SE->getMinusSCEV(Start, Index);
}

/// Compute the number of bytes as a SCEV from the backedge taken count.
///
/// This also maps the SCEV into the provided type and tries to handle the
/// computation in a way that will fold cleanly.
static const SCEV *getNumBytes(const SCEV *BECount, Type *IntPtr,
                               const SCEV *StoreSizeSCEV, Loop *CurLoop,
                               const DataLayout *DL, ScalarEvolution *SE) {
  const SCEV *TripCountSCEV =
      SE->getTripCountFromExitCount(BECount, IntPtr, CurLoop);
  return SE->getMulExpr(TripCountSCEV,
                        SE->getTruncateOrZeroExtend(StoreSizeSCEV, IntPtr),
                        SCEV::FlagNUW);
}

/// processLoopStridedStore - We see a strided store of some value.  If we can
/// transform this into a memset or memset_pattern in the loop preheader, do so.
bool LoopIdiomRecognize::processLoopStridedStore(
    Value *DestPtr, const SCEV *StoreSizeSCEV, MaybeAlign StoreAlignment,
    Value *StoredVal, Instruction *TheStore,
    SmallPtrSetImpl<Instruction *> &Stores, const SCEVAddRecExpr *Ev,
    const SCEV *BECount, bool IsNegStride, bool IsLoopMemset) {
  Module *M = TheStore->getModule();
  Value *SplatValue = isBytewiseValue(StoredVal, *DL);
  Constant *PatternValue = nullptr;

  if (!SplatValue)
    PatternValue = getMemSetPatternValue(StoredVal, DL);

  assert((SplatValue || PatternValue) &&
         "Expected either splat value or pattern value.");

  // The trip count of the loop and the base pointer of the addrec SCEV is
  // guaranteed to be loop invariant, which means that it should dominate the
  // header.  This allows us to insert code for it in the preheader.
  unsigned DestAS = DestPtr->getType()->getPointerAddressSpace();
  BasicBlock *Preheader = CurLoop->getLoopPreheader();
  IRBuilder<> Builder(Preheader->getTerminator());
  SCEVExpander Expander(*SE, *DL, "loop-idiom");
  SCEVExpanderCleaner ExpCleaner(Expander);

  Type *DestInt8PtrTy = Builder.getPtrTy(DestAS);
  Type *IntIdxTy = DL->getIndexType(DestPtr->getType());

  bool Changed = false;
  const SCEV *Start = Ev->getStart();
  // Handle negative strided loops.
  if (IsNegStride)
    Start = getStartForNegStride(Start, BECount, IntIdxTy, StoreSizeSCEV, SE);

  // TODO: ideally we should still be able to generate memset if SCEV expander
  // is taught to generate the dependencies at the latest point.
  if (!Expander.isSafeToExpand(Start))
    return Changed;

  // Okay, we have a strided store "p[i]" of a splattable value.  We can turn
  // this into a memset in the loop preheader now if we want.  However, this
  // would be unsafe to do if there is anything else in the loop that may read
  // or write to the aliased location.  Check for any overlap by generating the
  // base pointer and checking the region.
  Value *BasePtr =
      Expander.expandCodeFor(Start, DestInt8PtrTy, Preheader->getTerminator());

  // From here on out, conservatively report to the pass manager that we've
  // changed the IR, even if we later clean up these added instructions. There
  // may be structural differences e.g. in the order of use lists not accounted
  // for in just a textual dump of the IR. This is written as a variable, even
  // though statically all the places this dominates could be replaced with
  // 'true', with the hope that anyone trying to be clever / "more precise" with
  // the return value will read this comment, and leave them alone.
  Changed = true;

  if (mayLoopAccessLocation(BasePtr, ModRefInfo::ModRef, CurLoop, BECount,
                            StoreSizeSCEV, *AA, Stores))
    return Changed;

  if (avoidLIRForMultiBlockLoop(/*IsMemset=*/true, IsLoopMemset))
    return Changed;

  // Okay, everything looks good, insert the memset.

  const SCEV *NumBytesS =
      getNumBytes(BECount, IntIdxTy, StoreSizeSCEV, CurLoop, DL, SE);

  // TODO: ideally we should still be able to generate memset if SCEV expander
  // is taught to generate the dependencies at the latest point.
  if (!Expander.isSafeToExpand(NumBytesS))
    return Changed;

  Value *NumBytes =
      Expander.expandCodeFor(NumBytesS, IntIdxTy, Preheader->getTerminator());

  if (!SplatValue && !isLibFuncEmittable(M, TLI, LibFunc_memset_pattern16))
    return Changed;

  AAMDNodes AATags = TheStore->getAAMetadata();
  for (Instruction *Store : Stores)
    AATags = AATags.merge(Store->getAAMetadata());
  if (auto CI = dyn_cast<ConstantInt>(NumBytes))
    AATags = AATags.extendTo(CI->getZExtValue());
  else
    AATags = AATags.extendTo(-1);

  CallInst *NewCall;
  if (SplatValue) {
    NewCall = Builder.CreateMemSet(
        BasePtr, SplatValue, NumBytes, MaybeAlign(StoreAlignment),
        /*isVolatile=*/false, AATags.TBAA, AATags.Scope, AATags.NoAlias);
  } else {
    assert (isLibFuncEmittable(M, TLI, LibFunc_memset_pattern16));
    // Everything is emitted in default address space
    Type *Int8PtrTy = DestInt8PtrTy;

    StringRef FuncName = "memset_pattern16";
    FunctionCallee MSP = getOrInsertLibFunc(M, *TLI, LibFunc_memset_pattern16,
                            Builder.getVoidTy(), Int8PtrTy, Int8PtrTy, IntIdxTy);
    inferNonMandatoryLibFuncAttrs(M, FuncName, *TLI);

    // Otherwise we should form a memset_pattern16.  PatternValue is known to be
    // an constant array of 16-bytes.  Plop the value into a mergable global.
    GlobalVariable *GV = new GlobalVariable(*M, PatternValue->getType(), true,
                                            GlobalValue::PrivateLinkage,
                                            PatternValue, ".memset_pattern");
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global); // Ok to merge these.
    GV->setAlignment(Align(16));
    Value *PatternPtr = GV;
    NewCall = Builder.CreateCall(MSP, {BasePtr, PatternPtr, NumBytes});

    // Set the TBAA info if present.
    if (AATags.TBAA)
      NewCall->setMetadata(LLVMContext::MD_tbaa, AATags.TBAA);

    if (AATags.Scope)
      NewCall->setMetadata(LLVMContext::MD_alias_scope, AATags.Scope);

    if (AATags.NoAlias)
      NewCall->setMetadata(LLVMContext::MD_noalias, AATags.NoAlias);
  }

  NewCall->setDebugLoc(TheStore->getDebugLoc());

  if (MSSAU) {
    MemoryAccess *NewMemAcc = MSSAU->createMemoryAccessInBB(
        NewCall, nullptr, NewCall->getParent(), MemorySSA::BeforeTerminator);
    MSSAU->insertDef(cast<MemoryDef>(NewMemAcc), true);
  }

  LLVM_DEBUG(dbgs() << "  Formed memset: " << *NewCall << "\n"
                    << "    from store to: " << *Ev << " at: " << *TheStore
                    << "\n");

  ORE.emit([&]() {
    OptimizationRemark R(DEBUG_TYPE, "ProcessLoopStridedStore",
                         NewCall->getDebugLoc(), Preheader);
    R << "Transformed loop-strided store in "
      << ore::NV("Function", TheStore->getFunction())
      << " function into a call to "
      << ore::NV("NewFunction", NewCall->getCalledFunction())
      << "() intrinsic";
    if (!Stores.empty())
      R << ore::setExtraArgs();
    for (auto *I : Stores) {
      R << ore::NV("FromBlock", I->getParent()->getName())
        << ore::NV("ToBlock", Preheader->getName());
    }
    return R;
  });

  // Okay, the memset has been formed.  Zap the original store and anything that
  // feeds into it.
  for (auto *I : Stores) {
    if (MSSAU)
      MSSAU->removeMemoryAccess(I, true);
    deleteDeadInstruction(I);
  }
  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();
  ++NumMemSet;
  ExpCleaner.markResultUsed();
  return true;
}

/// If the stored value is a strided load in the same loop with the same stride
/// this may be transformable into a memcpy.  This kicks in for stuff like
/// for (i) A[i] = B[i];
bool LoopIdiomRecognize::processLoopStoreOfLoopLoad(StoreInst *SI,
                                                    const SCEV *BECount) {
  assert(SI->isUnordered() && "Expected only non-volatile non-ordered stores.");

  Value *StorePtr = SI->getPointerOperand();
  const SCEVAddRecExpr *StoreEv = cast<SCEVAddRecExpr>(SE->getSCEV(StorePtr));
  unsigned StoreSize = DL->getTypeStoreSize(SI->getValueOperand()->getType());

  // The store must be feeding a non-volatile load.
  LoadInst *LI = cast<LoadInst>(SI->getValueOperand());
  assert(LI->isUnordered() && "Expected only non-volatile non-ordered loads.");

  // See if the pointer expression is an AddRec like {base,+,1} on the current
  // loop, which indicates a strided load.  If we have something else, it's a
  // random load we can't handle.
  Value *LoadPtr = LI->getPointerOperand();
  const SCEVAddRecExpr *LoadEv = cast<SCEVAddRecExpr>(SE->getSCEV(LoadPtr));

  const SCEV *StoreSizeSCEV = SE->getConstant(StorePtr->getType(), StoreSize);
  return processLoopStoreOfLoopLoad(StorePtr, LoadPtr, StoreSizeSCEV,
                                    SI->getAlign(), LI->getAlign(), SI, LI,
                                    StoreEv, LoadEv, BECount);
}

namespace {
class MemmoveVerifier {
public:
  explicit MemmoveVerifier(const Value &LoadBasePtr, const Value &StoreBasePtr,
                           const DataLayout &DL)
      : DL(DL), BP1(llvm::GetPointerBaseWithConstantOffset(
                    LoadBasePtr.stripPointerCasts(), LoadOff, DL)),
        BP2(llvm::GetPointerBaseWithConstantOffset(
            StoreBasePtr.stripPointerCasts(), StoreOff, DL)),
        IsSameObject(BP1 == BP2) {}

  bool loadAndStoreMayFormMemmove(unsigned StoreSize, bool IsNegStride,
                                  const Instruction &TheLoad,
                                  bool IsMemCpy) const {
    if (IsMemCpy) {
      // Ensure that LoadBasePtr is after StoreBasePtr or before StoreBasePtr
      // for negative stride.
      if ((!IsNegStride && LoadOff <= StoreOff) ||
          (IsNegStride && LoadOff >= StoreOff))
        return false;
    } else {
      // Ensure that LoadBasePtr is after StoreBasePtr or before StoreBasePtr
      // for negative stride. LoadBasePtr shouldn't overlap with StoreBasePtr.
      int64_t LoadSize =
          DL.getTypeSizeInBits(TheLoad.getType()).getFixedValue() / 8;
      if (BP1 != BP2 || LoadSize != int64_t(StoreSize))
        return false;
      if ((!IsNegStride && LoadOff < StoreOff + int64_t(StoreSize)) ||
          (IsNegStride && LoadOff + LoadSize > StoreOff))
        return false;
    }
    return true;
  }

private:
  const DataLayout &DL;
  int64_t LoadOff = 0;
  int64_t StoreOff = 0;
  const Value *BP1;
  const Value *BP2;

public:
  const bool IsSameObject;
};
} // namespace

bool LoopIdiomRecognize::processLoopStoreOfLoopLoad(
    Value *DestPtr, Value *SourcePtr, const SCEV *StoreSizeSCEV,
    MaybeAlign StoreAlign, MaybeAlign LoadAlign, Instruction *TheStore,
    Instruction *TheLoad, const SCEVAddRecExpr *StoreEv,
    const SCEVAddRecExpr *LoadEv, const SCEV *BECount) {

  // FIXME: until llvm.memcpy.inline supports dynamic sizes, we need to
  // conservatively bail here, since otherwise we may have to transform
  // llvm.memcpy.inline into llvm.memcpy which is illegal.
  if (isa<MemCpyInlineInst>(TheStore))
    return false;

  // The trip count of the loop and the base pointer of the addrec SCEV is
  // guaranteed to be loop invariant, which means that it should dominate the
  // header.  This allows us to insert code for it in the preheader.
  BasicBlock *Preheader = CurLoop->getLoopPreheader();
  IRBuilder<> Builder(Preheader->getTerminator());
  SCEVExpander Expander(*SE, *DL, "loop-idiom");

  SCEVExpanderCleaner ExpCleaner(Expander);

  bool Changed = false;
  const SCEV *StrStart = StoreEv->getStart();
  unsigned StrAS = DestPtr->getType()->getPointerAddressSpace();
  Type *IntIdxTy = Builder.getIntNTy(DL->getIndexSizeInBits(StrAS));

  APInt Stride = getStoreStride(StoreEv);
  const SCEVConstant *ConstStoreSize = dyn_cast<SCEVConstant>(StoreSizeSCEV);

  // TODO: Deal with non-constant size; Currently expect constant store size
  assert(ConstStoreSize && "store size is expected to be a constant");

  int64_t StoreSize = ConstStoreSize->getValue()->getZExtValue();
  bool IsNegStride = StoreSize == -Stride;

  // Handle negative strided loops.
  if (IsNegStride)
    StrStart =
        getStartForNegStride(StrStart, BECount, IntIdxTy, StoreSizeSCEV, SE);

  // Okay, we have a strided store "p[i]" of a loaded value.  We can turn
  // this into a memcpy in the loop preheader now if we want.  However, this
  // would be unsafe to do if there is anything else in the loop that may read
  // or write the memory region we're storing to.  This includes the load that
  // feeds the stores.  Check for an alias by generating the base address and
  // checking everything.
  Value *StoreBasePtr = Expander.expandCodeFor(
      StrStart, Builder.getPtrTy(StrAS), Preheader->getTerminator());

  // From here on out, conservatively report to the pass manager that we've
  // changed the IR, even if we later clean up these added instructions. There
  // may be structural differences e.g. in the order of use lists not accounted
  // for in just a textual dump of the IR. This is written as a variable, even
  // though statically all the places this dominates could be replaced with
  // 'true', with the hope that anyone trying to be clever / "more precise" with
  // the return value will read this comment, and leave them alone.
  Changed = true;

  SmallPtrSet<Instruction *, 2> IgnoredInsts;
  IgnoredInsts.insert(TheStore);

  bool IsMemCpy = isa<MemCpyInst>(TheStore);
  const StringRef InstRemark = IsMemCpy ? "memcpy" : "load and store";

  bool LoopAccessStore =
      mayLoopAccessLocation(StoreBasePtr, ModRefInfo::ModRef, CurLoop, BECount,
                            StoreSizeSCEV, *AA, IgnoredInsts);
  if (LoopAccessStore) {
    // For memmove case it's not enough to guarantee that loop doesn't access
    // TheStore and TheLoad. Additionally we need to make sure that TheStore is
    // the only user of TheLoad.
    if (!TheLoad->hasOneUse())
      return Changed;
    IgnoredInsts.insert(TheLoad);
    if (mayLoopAccessLocation(StoreBasePtr, ModRefInfo::ModRef, CurLoop,
                              BECount, StoreSizeSCEV, *AA, IgnoredInsts)) {
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "LoopMayAccessStore",
                                        TheStore)
               << ore::NV("Inst", InstRemark) << " in "
               << ore::NV("Function", TheStore->getFunction())
               << " function will not be hoisted: "
               << ore::NV("Reason", "The loop may access store location");
      });
      return Changed;
    }
    IgnoredInsts.erase(TheLoad);
  }

  const SCEV *LdStart = LoadEv->getStart();
  unsigned LdAS = SourcePtr->getType()->getPointerAddressSpace();

  // Handle negative strided loops.
  if (IsNegStride)
    LdStart =
        getStartForNegStride(LdStart, BECount, IntIdxTy, StoreSizeSCEV, SE);

  // For a memcpy, we have to make sure that the input array is not being
  // mutated by the loop.
  Value *LoadBasePtr = Expander.expandCodeFor(LdStart, Builder.getPtrTy(LdAS),
                                              Preheader->getTerminator());

  // If the store is a memcpy instruction, we must check if it will write to
  // the load memory locations. So remove it from the ignored stores.
  MemmoveVerifier Verifier(*LoadBasePtr, *StoreBasePtr, *DL);
  if (IsMemCpy && !Verifier.IsSameObject)
    IgnoredInsts.erase(TheStore);
  if (mayLoopAccessLocation(LoadBasePtr, ModRefInfo::Mod, CurLoop, BECount,
                            StoreSizeSCEV, *AA, IgnoredInsts)) {
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "LoopMayAccessLoad", TheLoad)
             << ore::NV("Inst", InstRemark) << " in "
             << ore::NV("Function", TheStore->getFunction())
             << " function will not be hoisted: "
             << ore::NV("Reason", "The loop may access load location");
    });
    return Changed;
  }

  bool UseMemMove = IsMemCpy ? Verifier.IsSameObject : LoopAccessStore;
  if (UseMemMove)
    if (!Verifier.loadAndStoreMayFormMemmove(StoreSize, IsNegStride, *TheLoad,
                                             IsMemCpy))
      return Changed;

  if (avoidLIRForMultiBlockLoop())
    return Changed;

  // Okay, everything is safe, we can transform this!

  const SCEV *NumBytesS =
      getNumBytes(BECount, IntIdxTy, StoreSizeSCEV, CurLoop, DL, SE);

  Value *NumBytes =
      Expander.expandCodeFor(NumBytesS, IntIdxTy, Preheader->getTerminator());

  AAMDNodes AATags = TheLoad->getAAMetadata();
  AAMDNodes StoreAATags = TheStore->getAAMetadata();
  AATags = AATags.merge(StoreAATags);
  if (auto CI = dyn_cast<ConstantInt>(NumBytes))
    AATags = AATags.extendTo(CI->getZExtValue());
  else
    AATags = AATags.extendTo(-1);

  CallInst *NewCall = nullptr;
  // Check whether to generate an unordered atomic memcpy:
  //  If the load or store are atomic, then they must necessarily be unordered
  //  by previous checks.
  if (!TheStore->isAtomic() && !TheLoad->isAtomic()) {
    if (UseMemMove)
      NewCall = Builder.CreateMemMove(
          StoreBasePtr, StoreAlign, LoadBasePtr, LoadAlign, NumBytes,
          /*isVolatile=*/false, AATags.TBAA, AATags.Scope, AATags.NoAlias);
    else
      NewCall =
          Builder.CreateMemCpy(StoreBasePtr, StoreAlign, LoadBasePtr, LoadAlign,
                               NumBytes, /*isVolatile=*/false, AATags.TBAA,
                               AATags.TBAAStruct, AATags.Scope, AATags.NoAlias);
  } else {
    // For now don't support unordered atomic memmove.
    if (UseMemMove)
      return Changed;
    // We cannot allow unaligned ops for unordered load/store, so reject
    // anything where the alignment isn't at least the element size.
    assert((StoreAlign && LoadAlign) &&
           "Expect unordered load/store to have align.");
    if (*StoreAlign < StoreSize || *LoadAlign < StoreSize)
      return Changed;

    // If the element.atomic memcpy is not lowered into explicit
    // loads/stores later, then it will be lowered into an element-size
    // specific lib call. If the lib call doesn't exist for our store size, then
    // we shouldn't generate the memcpy.
    if (StoreSize > TTI->getAtomicMemIntrinsicMaxElementSize())
      return Changed;

    // Create the call.
    // Note that unordered atomic loads/stores are *required* by the spec to
    // have an alignment but non-atomic loads/stores may not.
    NewCall = Builder.CreateElementUnorderedAtomicMemCpy(
        StoreBasePtr, *StoreAlign, LoadBasePtr, *LoadAlign, NumBytes, StoreSize,
        AATags.TBAA, AATags.TBAAStruct, AATags.Scope, AATags.NoAlias);
  }
  NewCall->setDebugLoc(TheStore->getDebugLoc());

  if (MSSAU) {
    MemoryAccess *NewMemAcc = MSSAU->createMemoryAccessInBB(
        NewCall, nullptr, NewCall->getParent(), MemorySSA::BeforeTerminator);
    MSSAU->insertDef(cast<MemoryDef>(NewMemAcc), true);
  }

  LLVM_DEBUG(dbgs() << "  Formed new call: " << *NewCall << "\n"
                    << "    from load ptr=" << *LoadEv << " at: " << *TheLoad
                    << "\n"
                    << "    from store ptr=" << *StoreEv << " at: " << *TheStore
                    << "\n");

  ORE.emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "ProcessLoopStoreOfLoopLoad",
                              NewCall->getDebugLoc(), Preheader)
           << "Formed a call to "
           << ore::NV("NewFunction", NewCall->getCalledFunction())
           << "() intrinsic from " << ore::NV("Inst", InstRemark)
           << " instruction in " << ore::NV("Function", TheStore->getFunction())
           << " function"
           << ore::setExtraArgs()
           << ore::NV("FromBlock", TheStore->getParent()->getName())
           << ore::NV("ToBlock", Preheader->getName());
  });

  // Okay, a new call to memcpy/memmove has been formed.  Zap the original store
  // and anything that feeds into it.
  if (MSSAU)
    MSSAU->removeMemoryAccess(TheStore, true);
  deleteDeadInstruction(TheStore);
  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();
  if (UseMemMove)
    ++NumMemMove;
  else
    ++NumMemCpy;
  ExpCleaner.markResultUsed();
  return true;
}

// When compiling for codesize we avoid idiom recognition for a multi-block loop
// unless it is a loop_memset idiom or a memset/memcpy idiom in a nested loop.
//
bool LoopIdiomRecognize::avoidLIRForMultiBlockLoop(bool IsMemset,
                                                   bool IsLoopMemset) {
  if (ApplyCodeSizeHeuristics && CurLoop->getNumBlocks() > 1) {
    if (CurLoop->isOutermost() && (!IsMemset || !IsLoopMemset)) {
      LLVM_DEBUG(dbgs() << "  " << CurLoop->getHeader()->getParent()->getName()
                        << " : LIR " << (IsMemset ? "Memset" : "Memcpy")
                        << " avoided: multi-block top-level loop\n");
      return true;
    }
  }

  return false;
}

bool LoopIdiomRecognize::runOnNoncountableLoop() {
  LLVM_DEBUG(dbgs() << DEBUG_TYPE " Scanning: F["
                    << CurLoop->getHeader()->getParent()->getName()
                    << "] Noncountable Loop %"
                    << CurLoop->getHeader()->getName() << "\n");

  return recognizePopcount() || recognizeAndInsertFFS() ||
         recognizeShiftUntilBitTest() || recognizeShiftUntilZero() ||
         recognizeShiftUntilLessThan();
}

/// Check if the given conditional branch is based on the comparison between
/// a variable and zero, and if the variable is non-zero or zero (JmpOnZero is
/// true), the control yields to the loop entry. If the branch matches the
/// behavior, the variable involved in the comparison is returned. This function
/// will be called to see if the precondition and postcondition of the loop are
/// in desirable form.
static Value *matchCondition(BranchInst *BI, BasicBlock *LoopEntry,
                             bool JmpOnZero = false) {
  if (!BI || !BI->isConditional())
    return nullptr;

  ICmpInst *Cond = dyn_cast<ICmpInst>(BI->getCondition());
  if (!Cond)
    return nullptr;

  ConstantInt *CmpZero = dyn_cast<ConstantInt>(Cond->getOperand(1));
  if (!CmpZero || !CmpZero->isZero())
    return nullptr;

  BasicBlock *TrueSucc = BI->getSuccessor(0);
  BasicBlock *FalseSucc = BI->getSuccessor(1);
  if (JmpOnZero)
    std::swap(TrueSucc, FalseSucc);

  ICmpInst::Predicate Pred = Cond->getPredicate();
  if ((Pred == ICmpInst::ICMP_NE && TrueSucc == LoopEntry) ||
      (Pred == ICmpInst::ICMP_EQ && FalseSucc == LoopEntry))
    return Cond->getOperand(0);

  return nullptr;
}

/// Check if the given conditional branch is based on an unsigned less-than
/// comparison between a variable and a constant, and if the comparison is false
/// the control yields to the loop entry. If the branch matches the behaviour,
/// the variable involved in the comparison is returned.
static Value *matchShiftULTCondition(BranchInst *BI, BasicBlock *LoopEntry,
                                     APInt &Threshold) {
  if (!BI || !BI->isConditional())
    return nullptr;

  ICmpInst *Cond = dyn_cast<ICmpInst>(BI->getCondition());
  if (!Cond)
    return nullptr;

  ConstantInt *CmpConst = dyn_cast<ConstantInt>(Cond->getOperand(1));
  if (!CmpConst)
    return nullptr;

  BasicBlock *FalseSucc = BI->getSuccessor(1);
  ICmpInst::Predicate Pred = Cond->getPredicate();

  if (Pred == ICmpInst::ICMP_ULT && FalseSucc == LoopEntry) {
    Threshold = CmpConst->getValue();
    return Cond->getOperand(0);
  }

  return nullptr;
}

// Check if the recurrence variable `VarX` is in the right form to create
// the idiom. Returns the value coerced to a PHINode if so.
static PHINode *getRecurrenceVar(Value *VarX, Instruction *DefX,
                                 BasicBlock *LoopEntry) {
  auto *PhiX = dyn_cast<PHINode>(VarX);
  if (PhiX && PhiX->getParent() == LoopEntry &&
      (PhiX->getOperand(0) == DefX || PhiX->getOperand(1) == DefX))
    return PhiX;
  return nullptr;
}

/// Return true if the idiom is detected in the loop.
///
/// Additionally:
/// 1) \p CntInst is set to the instruction Counting Leading Zeros (CTLZ)
///       or nullptr if there is no such.
/// 2) \p CntPhi is set to the corresponding phi node
///       or nullptr if there is no such.
/// 3) \p InitX is set to the value whose CTLZ could be used.
/// 4) \p DefX is set to the instruction calculating Loop exit condition.
/// 5) \p Threshold is set to the constant involved in the unsigned less-than
///       comparison.
///
/// The core idiom we are trying to detect is:
/// \code
///    if (x0 < 2)
///      goto loop-exit // the precondition of the loop
///    cnt0 = init-val
///    do {
///      x = phi (x0, x.next);   //PhiX
///      cnt = phi (cnt0, cnt.next)
///
///      cnt.next = cnt + 1;
///       ...
///      x.next = x >> 1;   // DefX
///    } while (x >= 4)
/// loop-exit:
/// \endcode
static bool detectShiftUntilLessThanIdiom(Loop *CurLoop, const DataLayout &DL,
                                          Intrinsic::ID &IntrinID,
                                          Value *&InitX, Instruction *&CntInst,
                                          PHINode *&CntPhi, Instruction *&DefX,
                                          APInt &Threshold) {
  BasicBlock *LoopEntry;

  DefX = nullptr;
  CntInst = nullptr;
  CntPhi = nullptr;
  LoopEntry = *(CurLoop->block_begin());

  // step 1: Check if the loop-back branch is in desirable form.
  if (Value *T = matchShiftULTCondition(
          dyn_cast<BranchInst>(LoopEntry->getTerminator()), LoopEntry,
          Threshold))
    DefX = dyn_cast<Instruction>(T);
  else
    return false;

  // step 2: Check the recurrence of variable X
  if (!DefX || !isa<PHINode>(DefX))
    return false;

  PHINode *VarPhi = cast<PHINode>(DefX);
  int Idx = VarPhi->getBasicBlockIndex(LoopEntry);
  if (Idx == -1)
    return false;

  DefX = dyn_cast<Instruction>(VarPhi->getIncomingValue(Idx));
  if (!DefX || DefX->getNumOperands() == 0 || DefX->getOperand(0) != VarPhi)
    return false;

  // step 3: detect instructions corresponding to "x.next = x >> 1"
  if (DefX->getOpcode() != Instruction::LShr)
    return false;

  IntrinID = Intrinsic::ctlz;
  ConstantInt *Shft = dyn_cast<ConstantInt>(DefX->getOperand(1));
  if (!Shft || !Shft->isOne())
    return false;

  InitX = VarPhi->getIncomingValueForBlock(CurLoop->getLoopPreheader());

  // step 4: Find the instruction which count the CTLZ: cnt.next = cnt + 1
  //         or cnt.next = cnt + -1.
  // TODO: We can skip the step. If loop trip count is known (CTLZ),
  //       then all uses of "cnt.next" could be optimized to the trip count
  //       plus "cnt0". Currently it is not optimized.
  //       This step could be used to detect POPCNT instruction:
  //       cnt.next = cnt + (x.next & 1)
  for (Instruction &Inst : llvm::make_range(
           LoopEntry->getFirstNonPHI()->getIterator(), LoopEntry->end())) {
    if (Inst.getOpcode() != Instruction::Add)
      continue;

    ConstantInt *Inc = dyn_cast<ConstantInt>(Inst.getOperand(1));
    if (!Inc || (!Inc->isOne() && !Inc->isMinusOne()))
      continue;

    PHINode *Phi = getRecurrenceVar(Inst.getOperand(0), &Inst, LoopEntry);
    if (!Phi)
      continue;

    CntInst = &Inst;
    CntPhi = Phi;
    break;
  }
  if (!CntInst)
    return false;

  return true;
}

/// Return true iff the idiom is detected in the loop.
///
/// Additionally:
/// 1) \p CntInst is set to the instruction counting the population bit.
/// 2) \p CntPhi is set to the corresponding phi node.
/// 3) \p Var is set to the value whose population bits are being counted.
///
/// The core idiom we are trying to detect is:
/// \code
///    if (x0 != 0)
///      goto loop-exit // the precondition of the loop
///    cnt0 = init-val;
///    do {
///       x1 = phi (x0, x2);
///       cnt1 = phi(cnt0, cnt2);
///
///       cnt2 = cnt1 + 1;
///        ...
///       x2 = x1 & (x1 - 1);
///        ...
///    } while(x != 0);
///
/// loop-exit:
/// \endcode
static bool detectPopcountIdiom(Loop *CurLoop, BasicBlock *PreCondBB,
                                Instruction *&CntInst, PHINode *&CntPhi,
                                Value *&Var) {
  // step 1: Check to see if the look-back branch match this pattern:
  //    "if (a!=0) goto loop-entry".
  BasicBlock *LoopEntry;
  Instruction *DefX2, *CountInst;
  Value *VarX1, *VarX0;
  PHINode *PhiX, *CountPhi;

  DefX2 = CountInst = nullptr;
  VarX1 = VarX0 = nullptr;
  PhiX = CountPhi = nullptr;
  LoopEntry = *(CurLoop->block_begin());

  // step 1: Check if the loop-back branch is in desirable form.
  {
    if (Value *T = matchCondition(
            dyn_cast<BranchInst>(LoopEntry->getTerminator()), LoopEntry))
      DefX2 = dyn_cast<Instruction>(T);
    else
      return false;
  }

  // step 2: detect instructions corresponding to "x2 = x1 & (x1 - 1)"
  {
    if (!DefX2 || DefX2->getOpcode() != Instruction::And)
      return false;

    BinaryOperator *SubOneOp;

    if ((SubOneOp = dyn_cast<BinaryOperator>(DefX2->getOperand(0))))
      VarX1 = DefX2->getOperand(1);
    else {
      VarX1 = DefX2->getOperand(0);
      SubOneOp = dyn_cast<BinaryOperator>(DefX2->getOperand(1));
    }
    if (!SubOneOp || SubOneOp->getOperand(0) != VarX1)
      return false;

    ConstantInt *Dec = dyn_cast<ConstantInt>(SubOneOp->getOperand(1));
    if (!Dec ||
        !((SubOneOp->getOpcode() == Instruction::Sub && Dec->isOne()) ||
          (SubOneOp->getOpcode() == Instruction::Add &&
           Dec->isMinusOne()))) {
      return false;
    }
  }

  // step 3: Check the recurrence of variable X
  PhiX = getRecurrenceVar(VarX1, DefX2, LoopEntry);
  if (!PhiX)
    return false;

  // step 4: Find the instruction which count the population: cnt2 = cnt1 + 1
  {
    CountInst = nullptr;
    for (Instruction &Inst : llvm::make_range(
             LoopEntry->getFirstNonPHI()->getIterator(), LoopEntry->end())) {
      if (Inst.getOpcode() != Instruction::Add)
        continue;

      ConstantInt *Inc = dyn_cast<ConstantInt>(Inst.getOperand(1));
      if (!Inc || !Inc->isOne())
        continue;

      PHINode *Phi = getRecurrenceVar(Inst.getOperand(0), &Inst, LoopEntry);
      if (!Phi)
        continue;

      // Check if the result of the instruction is live of the loop.
      bool LiveOutLoop = false;
      for (User *U : Inst.users()) {
        if ((cast<Instruction>(U))->getParent() != LoopEntry) {
          LiveOutLoop = true;
          break;
        }
      }

      if (LiveOutLoop) {
        CountInst = &Inst;
        CountPhi = Phi;
        break;
      }
    }

    if (!CountInst)
      return false;
  }

  // step 5: check if the precondition is in this form:
  //   "if (x != 0) goto loop-head ; else goto somewhere-we-don't-care;"
  {
    auto *PreCondBr = dyn_cast<BranchInst>(PreCondBB->getTerminator());
    Value *T = matchCondition(PreCondBr, CurLoop->getLoopPreheader());
    if (T != PhiX->getOperand(0) && T != PhiX->getOperand(1))
      return false;

    CntInst = CountInst;
    CntPhi = CountPhi;
    Var = T;
  }

  return true;
}

/// Return true if the idiom is detected in the loop.
///
/// Additionally:
/// 1) \p CntInst is set to the instruction Counting Leading Zeros (CTLZ)
///       or nullptr if there is no such.
/// 2) \p CntPhi is set to the corresponding phi node
///       or nullptr if there is no such.
/// 3) \p Var is set to the value whose CTLZ could be used.
/// 4) \p DefX is set to the instruction calculating Loop exit condition.
///
/// The core idiom we are trying to detect is:
/// \code
///    if (x0 == 0)
///      goto loop-exit // the precondition of the loop
///    cnt0 = init-val;
///    do {
///       x = phi (x0, x.next);   //PhiX
///       cnt = phi(cnt0, cnt.next);
///
///       cnt.next = cnt + 1;
///        ...
///       x.next = x >> 1;   // DefX
///        ...
///    } while(x.next != 0);
///
/// loop-exit:
/// \endcode
static bool detectShiftUntilZeroIdiom(Loop *CurLoop, const DataLayout &DL,
                                      Intrinsic::ID &IntrinID, Value *&InitX,
                                      Instruction *&CntInst, PHINode *&CntPhi,
                                      Instruction *&DefX) {
  BasicBlock *LoopEntry;
  Value *VarX = nullptr;

  DefX = nullptr;
  CntInst = nullptr;
  CntPhi = nullptr;
  LoopEntry = *(CurLoop->block_begin());

  // step 1: Check if the loop-back branch is in desirable form.
  if (Value *T = matchCondition(
          dyn_cast<BranchInst>(LoopEntry->getTerminator()), LoopEntry))
    DefX = dyn_cast<Instruction>(T);
  else
    return false;

  // step 2: detect instructions corresponding to "x.next = x >> 1 or x << 1"
  if (!DefX || !DefX->isShift())
    return false;
  IntrinID = DefX->getOpcode() == Instruction::Shl ? Intrinsic::cttz :
                                                     Intrinsic::ctlz;
  ConstantInt *Shft = dyn_cast<ConstantInt>(DefX->getOperand(1));
  if (!Shft || !Shft->isOne())
    return false;
  VarX = DefX->getOperand(0);

  // step 3: Check the recurrence of variable X
  PHINode *PhiX = getRecurrenceVar(VarX, DefX, LoopEntry);
  if (!PhiX)
    return false;

  InitX = PhiX->getIncomingValueForBlock(CurLoop->getLoopPreheader());

  // Make sure the initial value can't be negative otherwise the ashr in the
  // loop might never reach zero which would make the loop infinite.
  if (DefX->getOpcode() == Instruction::AShr && !isKnownNonNegative(InitX, DL))
    return false;

  // step 4: Find the instruction which count the CTLZ: cnt.next = cnt + 1
  //         or cnt.next = cnt + -1.
  // TODO: We can skip the step. If loop trip count is known (CTLZ),
  //       then all uses of "cnt.next" could be optimized to the trip count
  //       plus "cnt0". Currently it is not optimized.
  //       This step could be used to detect POPCNT instruction:
  //       cnt.next = cnt + (x.next & 1)
  for (Instruction &Inst : llvm::make_range(
           LoopEntry->getFirstNonPHI()->getIterator(), LoopEntry->end())) {
    if (Inst.getOpcode() != Instruction::Add)
      continue;

    ConstantInt *Inc = dyn_cast<ConstantInt>(Inst.getOperand(1));
    if (!Inc || (!Inc->isOne() && !Inc->isMinusOne()))
      continue;

    PHINode *Phi = getRecurrenceVar(Inst.getOperand(0), &Inst, LoopEntry);
    if (!Phi)
      continue;

    CntInst = &Inst;
    CntPhi = Phi;
    break;
  }
  if (!CntInst)
    return false;

  return true;
}

// Check if CTLZ / CTTZ intrinsic is profitable. Assume it is always
// profitable if we delete the loop.
bool LoopIdiomRecognize::isProfitableToInsertFFS(Intrinsic::ID IntrinID,
                                                 Value *InitX, bool ZeroCheck,
                                                 size_t CanonicalSize) {
  const Value *Args[] = {InitX,
                         ConstantInt::getBool(InitX->getContext(), ZeroCheck)};

  // @llvm.dbg doesn't count as they have no semantic effect.
  auto InstWithoutDebugIt = CurLoop->getHeader()->instructionsWithoutDebug();
  uint32_t HeaderSize =
      std::distance(InstWithoutDebugIt.begin(), InstWithoutDebugIt.end());

  IntrinsicCostAttributes Attrs(IntrinID, InitX->getType(), Args);
  InstructionCost Cost = TTI->getIntrinsicInstrCost(
      Attrs, TargetTransformInfo::TCK_SizeAndLatency);
  if (HeaderSize != CanonicalSize && Cost > TargetTransformInfo::TCC_Basic)
    return false;

  return true;
}

/// Convert CTLZ / CTTZ idiom loop into countable loop.
/// If CTLZ / CTTZ inserted as a new trip count returns true; otherwise,
/// returns false.
bool LoopIdiomRecognize::insertFFSIfProfitable(Intrinsic::ID IntrinID,
                                               Value *InitX, Instruction *DefX,
                                               PHINode *CntPhi,
                                               Instruction *CntInst) {
  bool IsCntPhiUsedOutsideLoop = false;
  for (User *U : CntPhi->users())
    if (!CurLoop->contains(cast<Instruction>(U))) {
      IsCntPhiUsedOutsideLoop = true;
      break;
    }
  bool IsCntInstUsedOutsideLoop = false;
  for (User *U : CntInst->users())
    if (!CurLoop->contains(cast<Instruction>(U))) {
      IsCntInstUsedOutsideLoop = true;
      break;
    }
  // If both CntInst and CntPhi are used outside the loop the profitability
  // is questionable.
  if (IsCntInstUsedOutsideLoop && IsCntPhiUsedOutsideLoop)
    return false;

  // For some CPUs result of CTLZ(X) intrinsic is undefined
  // when X is 0. If we can not guarantee X != 0, we need to check this
  // when expand.
  bool ZeroCheck = false;
  // It is safe to assume Preheader exist as it was checked in
  // parent function RunOnLoop.
  BasicBlock *PH = CurLoop->getLoopPreheader();

  // If we are using the count instruction outside the loop, make sure we
  // have a zero check as a precondition. Without the check the loop would run
  // one iteration for before any check of the input value. This means 0 and 1
  // would have identical behavior in the original loop and thus
  if (!IsCntPhiUsedOutsideLoop) {
    auto *PreCondBB = PH->getSinglePredecessor();
    if (!PreCondBB)
      return false;
    auto *PreCondBI = dyn_cast<BranchInst>(PreCondBB->getTerminator());
    if (!PreCondBI)
      return false;
    if (matchCondition(PreCondBI, PH) != InitX)
      return false;
    ZeroCheck = true;
  }

  // FFS idiom loop has only 6 instructions:
  //  %n.addr.0 = phi [ %n, %entry ], [ %shr, %while.cond ]
  //  %i.0 = phi [ %i0, %entry ], [ %inc, %while.cond ]
  //  %shr = ashr %n.addr.0, 1
  //  %tobool = icmp eq %shr, 0
  //  %inc = add nsw %i.0, 1
  //  br i1 %tobool
  size_t IdiomCanonicalSize = 6;
  if (!isProfitableToInsertFFS(IntrinID, InitX, ZeroCheck, IdiomCanonicalSize))
    return false;

  transformLoopToCountable(IntrinID, PH, CntInst, CntPhi, InitX, DefX,
                           DefX->getDebugLoc(), ZeroCheck,
                           IsCntPhiUsedOutsideLoop);
  return true;
}

/// Recognize CTLZ or CTTZ idiom in a non-countable loop and convert the loop
/// to countable (with CTLZ / CTTZ trip count). If CTLZ / CTTZ inserted as a new
/// trip count returns true; otherwise, returns false.
bool LoopIdiomRecognize::recognizeAndInsertFFS() {
  // Give up if the loop has multiple blocks or multiple backedges.
  if (CurLoop->getNumBackEdges() != 1 || CurLoop->getNumBlocks() != 1)
    return false;

  Intrinsic::ID IntrinID;
  Value *InitX;
  Instruction *DefX = nullptr;
  PHINode *CntPhi = nullptr;
  Instruction *CntInst = nullptr;

  if (!detectShiftUntilZeroIdiom(CurLoop, *DL, IntrinID, InitX, CntInst, CntPhi,
                                 DefX))
    return false;

  return insertFFSIfProfitable(IntrinID, InitX, DefX, CntPhi, CntInst);
}

bool LoopIdiomRecognize::recognizeShiftUntilLessThan() {
  // Give up if the loop has multiple blocks or multiple backedges.
  if (CurLoop->getNumBackEdges() != 1 || CurLoop->getNumBlocks() != 1)
    return false;

  Intrinsic::ID IntrinID;
  Value *InitX;
  Instruction *DefX = nullptr;
  PHINode *CntPhi = nullptr;
  Instruction *CntInst = nullptr;

  APInt LoopThreshold;
  if (!detectShiftUntilLessThanIdiom(CurLoop, *DL, IntrinID, InitX, CntInst,
                                     CntPhi, DefX, LoopThreshold))
    return false;

  if (LoopThreshold == 2) {
    // Treat as regular FFS.
    return insertFFSIfProfitable(IntrinID, InitX, DefX, CntPhi, CntInst);
  }

  // Look for Floor Log2 Idiom.
  if (LoopThreshold != 4)
    return false;

  // Abort if CntPhi is used outside of the loop.
  for (User *U : CntPhi->users())
    if (!CurLoop->contains(cast<Instruction>(U)))
      return false;

  // It is safe to assume Preheader exist as it was checked in
  // parent function RunOnLoop.
  BasicBlock *PH = CurLoop->getLoopPreheader();
  auto *PreCondBB = PH->getSinglePredecessor();
  if (!PreCondBB)
    return false;
  auto *PreCondBI = dyn_cast<BranchInst>(PreCondBB->getTerminator());
  if (!PreCondBI)
    return false;

  APInt PreLoopThreshold;
  if (matchShiftULTCondition(PreCondBI, PH, PreLoopThreshold) != InitX ||
      PreLoopThreshold != 2)
    return false;

  bool ZeroCheck = true;

  // the loop has only 6 instructions:
  //  %n.addr.0 = phi [ %n, %entry ], [ %shr, %while.cond ]
  //  %i.0 = phi [ %i0, %entry ], [ %inc, %while.cond ]
  //  %shr = ashr %n.addr.0, 1
  //  %tobool = icmp ult %n.addr.0, C
  //  %inc = add nsw %i.0, 1
  //  br i1 %tobool
  size_t IdiomCanonicalSize = 6;
  if (!isProfitableToInsertFFS(IntrinID, InitX, ZeroCheck, IdiomCanonicalSize))
    return false;

  // log2(x) = w − 1 − clz(x)
  transformLoopToCountable(IntrinID, PH, CntInst, CntPhi, InitX, DefX,
                           DefX->getDebugLoc(), ZeroCheck,
                           /*IsCntPhiUsedOutsideLoop=*/false,
                           /*InsertSub=*/true);
  return true;
}

/// Recognizes a population count idiom in a non-countable loop.
///
/// If detected, transforms the relevant code to issue the popcount intrinsic
/// function call, and returns true; otherwise, returns false.
bool LoopIdiomRecognize::recognizePopcount() {
  if (TTI->getPopcntSupport(32) != TargetTransformInfo::PSK_FastHardware)
    return false;

  // Counting population are usually conducted by few arithmetic instructions.
  // Such instructions can be easily "absorbed" by vacant slots in a
  // non-compact loop. Therefore, recognizing popcount idiom only makes sense
  // in a compact loop.

  // Give up if the loop has multiple blocks or multiple backedges.
  if (CurLoop->getNumBackEdges() != 1 || CurLoop->getNumBlocks() != 1)
    return false;

  BasicBlock *LoopBody = *(CurLoop->block_begin());
  if (LoopBody->size() >= 20) {
    // The loop is too big, bail out.
    return false;
  }

  // It should have a preheader containing nothing but an unconditional branch.
  BasicBlock *PH = CurLoop->getLoopPreheader();
  if (!PH || &PH->front() != PH->getTerminator())
    return false;
  auto *EntryBI = dyn_cast<BranchInst>(PH->getTerminator());
  if (!EntryBI || EntryBI->isConditional())
    return false;

  // It should have a precondition block where the generated popcount intrinsic
  // function can be inserted.
  auto *PreCondBB = PH->getSinglePredecessor();
  if (!PreCondBB)
    return false;
  auto *PreCondBI = dyn_cast<BranchInst>(PreCondBB->getTerminator());
  if (!PreCondBI || PreCondBI->isUnconditional())
    return false;

  Instruction *CntInst;
  PHINode *CntPhi;
  Value *Val;
  if (!detectPopcountIdiom(CurLoop, PreCondBB, CntInst, CntPhi, Val))
    return false;

  transformLoopToPopcount(PreCondBB, CntInst, CntPhi, Val);
  return true;
}

static CallInst *createPopcntIntrinsic(IRBuilder<> &IRBuilder, Value *Val,
                                       const DebugLoc &DL) {
  Value *Ops[] = {Val};
  Type *Tys[] = {Val->getType()};

  Module *M = IRBuilder.GetInsertBlock()->getParent()->getParent();
  Function *Func = Intrinsic::getDeclaration(M, Intrinsic::ctpop, Tys);
  CallInst *CI = IRBuilder.CreateCall(Func, Ops);
  CI->setDebugLoc(DL);

  return CI;
}

static CallInst *createFFSIntrinsic(IRBuilder<> &IRBuilder, Value *Val,
                                    const DebugLoc &DL, bool ZeroCheck,
                                    Intrinsic::ID IID) {
  Value *Ops[] = {Val, IRBuilder.getInt1(ZeroCheck)};
  Type *Tys[] = {Val->getType()};

  Module *M = IRBuilder.GetInsertBlock()->getParent()->getParent();
  Function *Func = Intrinsic::getDeclaration(M, IID, Tys);
  CallInst *CI = IRBuilder.CreateCall(Func, Ops);
  CI->setDebugLoc(DL);

  return CI;
}

/// Transform the following loop (Using CTLZ, CTTZ is similar):
/// loop:
///   CntPhi = PHI [Cnt0, CntInst]
///   PhiX = PHI [InitX, DefX]
///   CntInst = CntPhi + 1
///   DefX = PhiX >> 1
///   LOOP_BODY
///   Br: loop if (DefX != 0)
/// Use(CntPhi) or Use(CntInst)
///
/// Into:
/// If CntPhi used outside the loop:
///   CountPrev = BitWidth(InitX) - CTLZ(InitX >> 1)
///   Count = CountPrev + 1
/// else
///   Count = BitWidth(InitX) - CTLZ(InitX)
/// loop:
///   CntPhi = PHI [Cnt0, CntInst]
///   PhiX = PHI [InitX, DefX]
///   PhiCount = PHI [Count, Dec]
///   CntInst = CntPhi + 1
///   DefX = PhiX >> 1
///   Dec = PhiCount - 1
///   LOOP_BODY
///   Br: loop if (Dec != 0)
/// Use(CountPrev + Cnt0) // Use(CntPhi)
/// or
/// Use(Count + Cnt0) // Use(CntInst)
///
/// If LOOP_BODY is empty the loop will be deleted.
/// If CntInst and DefX are not used in LOOP_BODY they will be removed.
void LoopIdiomRecognize::transformLoopToCountable(
    Intrinsic::ID IntrinID, BasicBlock *Preheader, Instruction *CntInst,
    PHINode *CntPhi, Value *InitX, Instruction *DefX, const DebugLoc &DL,
    bool ZeroCheck, bool IsCntPhiUsedOutsideLoop, bool InsertSub) {
  BranchInst *PreheaderBr = cast<BranchInst>(Preheader->getTerminator());

  // Step 1: Insert the CTLZ/CTTZ instruction at the end of the preheader block
  IRBuilder<> Builder(PreheaderBr);
  Builder.SetCurrentDebugLocation(DL);

  // If there are no uses of CntPhi crate:
  //   Count = BitWidth - CTLZ(InitX);
  //   NewCount = Count;
  // If there are uses of CntPhi create:
  //   NewCount = BitWidth - CTLZ(InitX >> 1);
  //   Count = NewCount + 1;
  Value *InitXNext;
  if (IsCntPhiUsedOutsideLoop) {
    if (DefX->getOpcode() == Instruction::AShr)
      InitXNext = Builder.CreateAShr(InitX, 1);
    else if (DefX->getOpcode() == Instruction::LShr)
      InitXNext = Builder.CreateLShr(InitX, 1);
    else if (DefX->getOpcode() == Instruction::Shl) // cttz
      InitXNext = Builder.CreateShl(InitX, 1);
    else
      llvm_unreachable("Unexpected opcode!");
  } else
    InitXNext = InitX;
  Value *Count =
      createFFSIntrinsic(Builder, InitXNext, DL, ZeroCheck, IntrinID);
  Type *CountTy = Count->getType();
  Count = Builder.CreateSub(
      ConstantInt::get(CountTy, CountTy->getIntegerBitWidth()), Count);
  if (InsertSub)
    Count = Builder.CreateSub(Count, ConstantInt::get(CountTy, 1));
  Value *NewCount = Count;
  if (IsCntPhiUsedOutsideLoop)
    Count = Builder.CreateAdd(Count, ConstantInt::get(CountTy, 1));

  NewCount = Builder.CreateZExtOrTrunc(NewCount, CntInst->getType());

  Value *CntInitVal = CntPhi->getIncomingValueForBlock(Preheader);
  if (cast<ConstantInt>(CntInst->getOperand(1))->isOne()) {
    // If the counter was being incremented in the loop, add NewCount to the
    // counter's initial value, but only if the initial value is not zero.
    ConstantInt *InitConst = dyn_cast<ConstantInt>(CntInitVal);
    if (!InitConst || !InitConst->isZero())
      NewCount = Builder.CreateAdd(NewCount, CntInitVal);
  } else {
    // If the count was being decremented in the loop, subtract NewCount from
    // the counter's initial value.
    NewCount = Builder.CreateSub(CntInitVal, NewCount);
  }

  // Step 2: Insert new IV and loop condition:
  // loop:
  //   ...
  //   PhiCount = PHI [Count, Dec]
  //   ...
  //   Dec = PhiCount - 1
  //   ...
  //   Br: loop if (Dec != 0)
  BasicBlock *Body = *(CurLoop->block_begin());
  auto *LbBr = cast<BranchInst>(Body->getTerminator());
  ICmpInst *LbCond = cast<ICmpInst>(LbBr->getCondition());

  PHINode *TcPhi = PHINode::Create(CountTy, 2, "tcphi");
  TcPhi->insertBefore(Body->begin());

  Builder.SetInsertPoint(LbCond);
  Instruction *TcDec = cast<Instruction>(Builder.CreateSub(
      TcPhi, ConstantInt::get(CountTy, 1), "tcdec", false, true));

  TcPhi->addIncoming(Count, Preheader);
  TcPhi->addIncoming(TcDec, Body);

  CmpInst::Predicate Pred =
      (LbBr->getSuccessor(0) == Body) ? CmpInst::ICMP_NE : CmpInst::ICMP_EQ;
  LbCond->setPredicate(Pred);
  LbCond->setOperand(0, TcDec);
  LbCond->setOperand(1, ConstantInt::get(CountTy, 0));

  // Step 3: All the references to the original counter outside
  //  the loop are replaced with the NewCount
  if (IsCntPhiUsedOutsideLoop)
    CntPhi->replaceUsesOutsideBlock(NewCount, Body);
  else
    CntInst->replaceUsesOutsideBlock(NewCount, Body);

  // step 4: Forget the "non-computable" trip-count SCEV associated with the
  //   loop. The loop would otherwise not be deleted even if it becomes empty.
  SE->forgetLoop(CurLoop);
}

void LoopIdiomRecognize::transformLoopToPopcount(BasicBlock *PreCondBB,
                                                 Instruction *CntInst,
                                                 PHINode *CntPhi, Value *Var) {
  BasicBlock *PreHead = CurLoop->getLoopPreheader();
  auto *PreCondBr = cast<BranchInst>(PreCondBB->getTerminator());
  const DebugLoc &DL = CntInst->getDebugLoc();

  // Assuming before transformation, the loop is following:
  //  if (x) // the precondition
  //     do { cnt++; x &= x - 1; } while(x);

  // Step 1: Insert the ctpop instruction at the end of the precondition block
  IRBuilder<> Builder(PreCondBr);
  Value *PopCnt, *PopCntZext, *NewCount, *TripCnt;
  {
    PopCnt = createPopcntIntrinsic(Builder, Var, DL);
    NewCount = PopCntZext =
        Builder.CreateZExtOrTrunc(PopCnt, cast<IntegerType>(CntPhi->getType()));

    if (NewCount != PopCnt)
      (cast<Instruction>(NewCount))->setDebugLoc(DL);

    // TripCnt is exactly the number of iterations the loop has
    TripCnt = NewCount;

    // If the population counter's initial value is not zero, insert Add Inst.
    Value *CntInitVal = CntPhi->getIncomingValueForBlock(PreHead);
    ConstantInt *InitConst = dyn_cast<ConstantInt>(CntInitVal);
    if (!InitConst || !InitConst->isZero()) {
      NewCount = Builder.CreateAdd(NewCount, CntInitVal);
      (cast<Instruction>(NewCount))->setDebugLoc(DL);
    }
  }

  // Step 2: Replace the precondition from "if (x == 0) goto loop-exit" to
  //   "if (NewCount == 0) loop-exit". Without this change, the intrinsic
  //   function would be partial dead code, and downstream passes will drag
  //   it back from the precondition block to the preheader.
  {
    ICmpInst *PreCond = cast<ICmpInst>(PreCondBr->getCondition());

    Value *Opnd0 = PopCntZext;
    Value *Opnd1 = ConstantInt::get(PopCntZext->getType(), 0);
    if (PreCond->getOperand(0) != Var)
      std::swap(Opnd0, Opnd1);

    ICmpInst *NewPreCond = cast<ICmpInst>(
        Builder.CreateICmp(PreCond->getPredicate(), Opnd0, Opnd1));
    PreCondBr->setCondition(NewPreCond);

    RecursivelyDeleteTriviallyDeadInstructions(PreCond, TLI);
  }

  // Step 3: Note that the population count is exactly the trip count of the
  // loop in question, which enable us to convert the loop from noncountable
  // loop into a countable one. The benefit is twofold:
  //
  //  - If the loop only counts population, the entire loop becomes dead after
  //    the transformation. It is a lot easier to prove a countable loop dead
  //    than to prove a noncountable one. (In some C dialects, an infinite loop
  //    isn't dead even if it computes nothing useful. In general, DCE needs
  //    to prove a noncountable loop finite before safely delete it.)
  //
  //  - If the loop also performs something else, it remains alive.
  //    Since it is transformed to countable form, it can be aggressively
  //    optimized by some optimizations which are in general not applicable
  //    to a noncountable loop.
  //
  // After this step, this loop (conceptually) would look like following:
  //   newcnt = __builtin_ctpop(x);
  //   t = newcnt;
  //   if (x)
  //     do { cnt++; x &= x-1; t--) } while (t > 0);
  BasicBlock *Body = *(CurLoop->block_begin());
  {
    auto *LbBr = cast<BranchInst>(Body->getTerminator());
    ICmpInst *LbCond = cast<ICmpInst>(LbBr->getCondition());
    Type *Ty = TripCnt->getType();

    PHINode *TcPhi = PHINode::Create(Ty, 2, "tcphi");
    TcPhi->insertBefore(Body->begin());

    Builder.SetInsertPoint(LbCond);
    Instruction *TcDec = cast<Instruction>(
        Builder.CreateSub(TcPhi, ConstantInt::get(Ty, 1),
                          "tcdec", false, true));

    TcPhi->addIncoming(TripCnt, PreHead);
    TcPhi->addIncoming(TcDec, Body);

    CmpInst::Predicate Pred =
        (LbBr->getSuccessor(0) == Body) ? CmpInst::ICMP_UGT : CmpInst::ICMP_SLE;
    LbCond->setPredicate(Pred);
    LbCond->setOperand(0, TcDec);
    LbCond->setOperand(1, ConstantInt::get(Ty, 0));
  }

  // Step 4: All the references to the original population counter outside
  //  the loop are replaced with the NewCount -- the value returned from
  //  __builtin_ctpop().
  CntInst->replaceUsesOutsideBlock(NewCount, Body);

  // step 5: Forget the "non-computable" trip-count SCEV associated with the
  //   loop. The loop would otherwise not be deleted even if it becomes empty.
  SE->forgetLoop(CurLoop);
}

/// Match loop-invariant value.
template <typename SubPattern_t> struct match_LoopInvariant {
  SubPattern_t SubPattern;
  const Loop *L;

  match_LoopInvariant(const SubPattern_t &SP, const Loop *L)
      : SubPattern(SP), L(L) {}

  template <typename ITy> bool match(ITy *V) {
    return L->isLoopInvariant(V) && SubPattern.match(V);
  }
};

/// Matches if the value is loop-invariant.
template <typename Ty>
inline match_LoopInvariant<Ty> m_LoopInvariant(const Ty &M, const Loop *L) {
  return match_LoopInvariant<Ty>(M, L);
}

/// Return true if the idiom is detected in the loop.
///
/// The core idiom we are trying to detect is:
/// \code
///   entry:
///     <...>
///     %bitmask = shl i32 1, %bitpos
///     br label %loop
///
///   loop:
///     %x.curr = phi i32 [ %x, %entry ], [ %x.next, %loop ]
///     %x.curr.bitmasked = and i32 %x.curr, %bitmask
///     %x.curr.isbitunset = icmp eq i32 %x.curr.bitmasked, 0
///     %x.next = shl i32 %x.curr, 1
///     <...>
///     br i1 %x.curr.isbitunset, label %loop, label %end
///
///   end:
///     %x.curr.res = phi i32 [ %x.curr, %loop ] <...>
///     %x.next.res = phi i32 [ %x.next, %loop ] <...>
///     <...>
/// \endcode
static bool detectShiftUntilBitTestIdiom(Loop *CurLoop, Value *&BaseX,
                                         Value *&BitMask, Value *&BitPos,
                                         Value *&CurrX, Instruction *&NextX) {
  LLVM_DEBUG(dbgs() << DEBUG_TYPE
             " Performing shift-until-bittest idiom detection.\n");

  // Give up if the loop has multiple blocks or multiple backedges.
  if (CurLoop->getNumBlocks() != 1 || CurLoop->getNumBackEdges() != 1) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad block/backedge count.\n");
    return false;
  }

  BasicBlock *LoopHeaderBB = CurLoop->getHeader();
  BasicBlock *LoopPreheaderBB = CurLoop->getLoopPreheader();
  assert(LoopPreheaderBB && "There is always a loop preheader.");

  using namespace PatternMatch;

  // Step 1: Check if the loop backedge is in desirable form.

  ICmpInst::Predicate Pred;
  Value *CmpLHS, *CmpRHS;
  BasicBlock *TrueBB, *FalseBB;
  if (!match(LoopHeaderBB->getTerminator(),
             m_Br(m_ICmp(Pred, m_Value(CmpLHS), m_Value(CmpRHS)),
                  m_BasicBlock(TrueBB), m_BasicBlock(FalseBB)))) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad backedge structure.\n");
    return false;
  }

  // Step 2: Check if the backedge's condition is in desirable form.

  auto MatchVariableBitMask = [&]() {
    return ICmpInst::isEquality(Pred) && match(CmpRHS, m_Zero()) &&
           match(CmpLHS,
                 m_c_And(m_Value(CurrX),
                         m_CombineAnd(
                             m_Value(BitMask),
                             m_LoopInvariant(m_Shl(m_One(), m_Value(BitPos)),
                                             CurLoop))));
  };
  auto MatchConstantBitMask = [&]() {
    return ICmpInst::isEquality(Pred) && match(CmpRHS, m_Zero()) &&
           match(CmpLHS, m_And(m_Value(CurrX),
                               m_CombineAnd(m_Value(BitMask), m_Power2()))) &&
           (BitPos = ConstantExpr::getExactLogBase2(cast<Constant>(BitMask)));
  };
  auto MatchDecomposableConstantBitMask = [&]() {
    APInt Mask;
    return llvm::decomposeBitTestICmp(CmpLHS, CmpRHS, Pred, CurrX, Mask) &&
           ICmpInst::isEquality(Pred) && Mask.isPowerOf2() &&
           (BitMask = ConstantInt::get(CurrX->getType(), Mask)) &&
           (BitPos = ConstantInt::get(CurrX->getType(), Mask.logBase2()));
  };

  if (!MatchVariableBitMask() && !MatchConstantBitMask() &&
      !MatchDecomposableConstantBitMask()) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad backedge comparison.\n");
    return false;
  }

  // Step 3: Check if the recurrence is in desirable form.
  auto *CurrXPN = dyn_cast<PHINode>(CurrX);
  if (!CurrXPN || CurrXPN->getParent() != LoopHeaderBB) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Not an expected PHI node.\n");
    return false;
  }

  BaseX = CurrXPN->getIncomingValueForBlock(LoopPreheaderBB);
  NextX =
      dyn_cast<Instruction>(CurrXPN->getIncomingValueForBlock(LoopHeaderBB));

  assert(CurLoop->isLoopInvariant(BaseX) &&
         "Expected BaseX to be avaliable in the preheader!");

  if (!NextX || !match(NextX, m_Shl(m_Specific(CurrX), m_One()))) {
    // FIXME: support right-shift?
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad recurrence.\n");
    return false;
  }

  // Step 4: Check if the backedge's destinations are in desirable form.

  assert(ICmpInst::isEquality(Pred) &&
         "Should only get equality predicates here.");

  // cmp-br is commutative, so canonicalize to a single variant.
  if (Pred != ICmpInst::Predicate::ICMP_EQ) {
    Pred = ICmpInst::getInversePredicate(Pred);
    std::swap(TrueBB, FalseBB);
  }

  // We expect to exit loop when comparison yields false,
  // so when it yields true we should branch back to loop header.
  if (TrueBB != LoopHeaderBB) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad backedge flow.\n");
    return false;
  }

  // Okay, idiom checks out.
  return true;
}

/// Look for the following loop:
/// \code
///   entry:
///     <...>
///     %bitmask = shl i32 1, %bitpos
///     br label %loop
///
///   loop:
///     %x.curr = phi i32 [ %x, %entry ], [ %x.next, %loop ]
///     %x.curr.bitmasked = and i32 %x.curr, %bitmask
///     %x.curr.isbitunset = icmp eq i32 %x.curr.bitmasked, 0
///     %x.next = shl i32 %x.curr, 1
///     <...>
///     br i1 %x.curr.isbitunset, label %loop, label %end
///
///   end:
///     %x.curr.res = phi i32 [ %x.curr, %loop ] <...>
///     %x.next.res = phi i32 [ %x.next, %loop ] <...>
///     <...>
/// \endcode
///
/// And transform it into:
/// \code
///   entry:
///     %bitmask = shl i32 1, %bitpos
///     %lowbitmask = add i32 %bitmask, -1
///     %mask = or i32 %lowbitmask, %bitmask
///     %x.masked = and i32 %x, %mask
///     %x.masked.numleadingzeros = call i32 @llvm.ctlz.i32(i32 %x.masked,
///                                                         i1 true)
///     %x.masked.numactivebits = sub i32 32, %x.masked.numleadingzeros
///     %x.masked.leadingonepos = add i32 %x.masked.numactivebits, -1
///     %backedgetakencount = sub i32 %bitpos, %x.masked.leadingonepos
///     %tripcount = add i32 %backedgetakencount, 1
///     %x.curr = shl i32 %x, %backedgetakencount
///     %x.next = shl i32 %x, %tripcount
///     br label %loop
///
///   loop:
///     %loop.iv = phi i32 [ 0, %entry ], [ %loop.iv.next, %loop ]
///     %loop.iv.next = add nuw i32 %loop.iv, 1
///     %loop.ivcheck = icmp eq i32 %loop.iv.next, %tripcount
///     <...>
///     br i1 %loop.ivcheck, label %end, label %loop
///
///   end:
///     %x.curr.res = phi i32 [ %x.curr, %loop ] <...>
///     %x.next.res = phi i32 [ %x.next, %loop ] <...>
///     <...>
/// \endcode
bool LoopIdiomRecognize::recognizeShiftUntilBitTest() {
  bool MadeChange = false;

  Value *X, *BitMask, *BitPos, *XCurr;
  Instruction *XNext;
  if (!detectShiftUntilBitTestIdiom(CurLoop, X, BitMask, BitPos, XCurr,
                                    XNext)) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE
               " shift-until-bittest idiom detection failed.\n");
    return MadeChange;
  }
  LLVM_DEBUG(dbgs() << DEBUG_TYPE " shift-until-bittest idiom detected!\n");

  // Ok, it is the idiom we were looking for, we *could* transform this loop,
  // but is it profitable to transform?

  BasicBlock *LoopHeaderBB = CurLoop->getHeader();
  BasicBlock *LoopPreheaderBB = CurLoop->getLoopPreheader();
  assert(LoopPreheaderBB && "There is always a loop preheader.");

  BasicBlock *SuccessorBB = CurLoop->getExitBlock();
  assert(SuccessorBB && "There is only a single successor.");

  IRBuilder<> Builder(LoopPreheaderBB->getTerminator());
  Builder.SetCurrentDebugLocation(cast<Instruction>(XCurr)->getDebugLoc());

  Intrinsic::ID IntrID = Intrinsic::ctlz;
  Type *Ty = X->getType();
  unsigned Bitwidth = Ty->getScalarSizeInBits();

  TargetTransformInfo::TargetCostKind CostKind =
      TargetTransformInfo::TCK_SizeAndLatency;

  // The rewrite is considered to be unprofitable iff and only iff the
  // intrinsic/shift we'll use are not cheap. Note that we are okay with *just*
  // making the loop countable, even if nothing else changes.
  IntrinsicCostAttributes Attrs(
      IntrID, Ty, {PoisonValue::get(Ty), /*is_zero_poison=*/Builder.getTrue()});
  InstructionCost Cost = TTI->getIntrinsicInstrCost(Attrs, CostKind);
  if (Cost > TargetTransformInfo::TCC_Basic) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE
               " Intrinsic is too costly, not beneficial\n");
    return MadeChange;
  }
  if (TTI->getArithmeticInstrCost(Instruction::Shl, Ty, CostKind) >
      TargetTransformInfo::TCC_Basic) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Shift is too costly, not beneficial\n");
    return MadeChange;
  }

  // Ok, transform appears worthwhile.
  MadeChange = true;

  if (!isGuaranteedNotToBeUndefOrPoison(BitPos)) {
    // BitMask may be computed from BitPos, Freeze BitPos so we can increase
    // it's use count.
    std::optional<BasicBlock::iterator> InsertPt = std::nullopt;
    if (auto *BitPosI = dyn_cast<Instruction>(BitPos))
      InsertPt = BitPosI->getInsertionPointAfterDef();
    else
      InsertPt = DT->getRoot()->getFirstNonPHIOrDbgOrAlloca();
    if (!InsertPt)
      return false;
    FreezeInst *BitPosFrozen =
        new FreezeInst(BitPos, BitPos->getName() + ".fr", *InsertPt);
    BitPos->replaceUsesWithIf(BitPosFrozen, [BitPosFrozen](Use &U) {
      return U.getUser() != BitPosFrozen;
    });
    BitPos = BitPosFrozen;
  }

  // Step 1: Compute the loop trip count.

  Value *LowBitMask = Builder.CreateAdd(BitMask, Constant::getAllOnesValue(Ty),
                                        BitPos->getName() + ".lowbitmask");
  Value *Mask =
      Builder.CreateOr(LowBitMask, BitMask, BitPos->getName() + ".mask");
  Value *XMasked = Builder.CreateAnd(X, Mask, X->getName() + ".masked");
  CallInst *XMaskedNumLeadingZeros = Builder.CreateIntrinsic(
      IntrID, Ty, {XMasked, /*is_zero_poison=*/Builder.getTrue()},
      /*FMFSource=*/nullptr, XMasked->getName() + ".numleadingzeros");
  Value *XMaskedNumActiveBits = Builder.CreateSub(
      ConstantInt::get(Ty, Ty->getScalarSizeInBits()), XMaskedNumLeadingZeros,
      XMasked->getName() + ".numactivebits", /*HasNUW=*/true,
      /*HasNSW=*/Bitwidth != 2);
  Value *XMaskedLeadingOnePos =
      Builder.CreateAdd(XMaskedNumActiveBits, Constant::getAllOnesValue(Ty),
                        XMasked->getName() + ".leadingonepos", /*HasNUW=*/false,
                        /*HasNSW=*/Bitwidth > 2);

  Value *LoopBackedgeTakenCount = Builder.CreateSub(
      BitPos, XMaskedLeadingOnePos, CurLoop->getName() + ".backedgetakencount",
      /*HasNUW=*/true, /*HasNSW=*/true);
  // We know loop's backedge-taken count, but what's loop's trip count?
  // Note that while NUW is always safe, while NSW is only for bitwidths != 2.
  Value *LoopTripCount =
      Builder.CreateAdd(LoopBackedgeTakenCount, ConstantInt::get(Ty, 1),
                        CurLoop->getName() + ".tripcount", /*HasNUW=*/true,
                        /*HasNSW=*/Bitwidth != 2);

  // Step 2: Compute the recurrence's final value without a loop.

  // NewX is always safe to compute, because `LoopBackedgeTakenCount`
  // will always be smaller than `bitwidth(X)`, i.e. we never get poison.
  Value *NewX = Builder.CreateShl(X, LoopBackedgeTakenCount);
  NewX->takeName(XCurr);
  if (auto *I = dyn_cast<Instruction>(NewX))
    I->copyIRFlags(XNext, /*IncludeWrapFlags=*/true);

  Value *NewXNext;
  // Rewriting XNext is more complicated, however, because `X << LoopTripCount`
  // will be poison iff `LoopTripCount == bitwidth(X)` (which will happen
  // iff `BitPos` is `bitwidth(x) - 1` and `X` is `1`). So unless we know
  // that isn't the case, we'll need to emit an alternative, safe IR.
  if (XNext->hasNoSignedWrap() || XNext->hasNoUnsignedWrap() ||
      PatternMatch::match(
          BitPos, PatternMatch::m_SpecificInt_ICMP(
                      ICmpInst::ICMP_NE, APInt(Ty->getScalarSizeInBits(),
                                               Ty->getScalarSizeInBits() - 1))))
    NewXNext = Builder.CreateShl(X, LoopTripCount);
  else {
    // Otherwise, just additionally shift by one. It's the smallest solution,
    // alternatively, we could check that NewX is INT_MIN (or BitPos is )
    // and select 0 instead.
    NewXNext = Builder.CreateShl(NewX, ConstantInt::get(Ty, 1));
  }

  NewXNext->takeName(XNext);
  if (auto *I = dyn_cast<Instruction>(NewXNext))
    I->copyIRFlags(XNext, /*IncludeWrapFlags=*/true);

  // Step 3: Adjust the successor basic block to recieve the computed
  //         recurrence's final value instead of the recurrence itself.

  XCurr->replaceUsesOutsideBlock(NewX, LoopHeaderBB);
  XNext->replaceUsesOutsideBlock(NewXNext, LoopHeaderBB);

  // Step 4: Rewrite the loop into a countable form, with canonical IV.

  // The new canonical induction variable.
  Builder.SetInsertPoint(LoopHeaderBB, LoopHeaderBB->begin());
  auto *IV = Builder.CreatePHI(Ty, 2, CurLoop->getName() + ".iv");

  // The induction itself.
  // Note that while NUW is always safe, while NSW is only for bitwidths != 2.
  Builder.SetInsertPoint(LoopHeaderBB->getTerminator());
  auto *IVNext =
      Builder.CreateAdd(IV, ConstantInt::get(Ty, 1), IV->getName() + ".next",
                        /*HasNUW=*/true, /*HasNSW=*/Bitwidth != 2);

  // The loop trip count check.
  auto *IVCheck = Builder.CreateICmpEQ(IVNext, LoopTripCount,
                                       CurLoop->getName() + ".ivcheck");
  Builder.CreateCondBr(IVCheck, SuccessorBB, LoopHeaderBB);
  LoopHeaderBB->getTerminator()->eraseFromParent();

  // Populate the IV PHI.
  IV->addIncoming(ConstantInt::get(Ty, 0), LoopPreheaderBB);
  IV->addIncoming(IVNext, LoopHeaderBB);

  // Step 5: Forget the "non-computable" trip-count SCEV associated with the
  //   loop. The loop would otherwise not be deleted even if it becomes empty.

  SE->forgetLoop(CurLoop);

  // Other passes will take care of actually deleting the loop if possible.

  LLVM_DEBUG(dbgs() << DEBUG_TYPE " shift-until-bittest idiom optimized!\n");

  ++NumShiftUntilBitTest;
  return MadeChange;
}

/// Return true if the idiom is detected in the loop.
///
/// The core idiom we are trying to detect is:
/// \code
///   entry:
///     <...>
///     %start = <...>
///     %extraoffset = <...>
///     <...>
///     br label %for.cond
///
///   loop:
///     %iv = phi i8 [ %start, %entry ], [ %iv.next, %for.cond ]
///     %nbits = add nsw i8 %iv, %extraoffset
///     %val.shifted = {{l,a}shr,shl} i8 %val, %nbits
///     %val.shifted.iszero = icmp eq i8 %val.shifted, 0
///     %iv.next = add i8 %iv, 1
///     <...>
///     br i1 %val.shifted.iszero, label %end, label %loop
///
///   end:
///     %iv.res = phi i8 [ %iv, %loop ] <...>
///     %nbits.res = phi i8 [ %nbits, %loop ] <...>
///     %val.shifted.res = phi i8 [ %val.shifted, %loop ] <...>
///     %val.shifted.iszero.res = phi i1 [ %val.shifted.iszero, %loop ] <...>
///     %iv.next.res = phi i8 [ %iv.next, %loop ] <...>
///     <...>
/// \endcode
static bool detectShiftUntilZeroIdiom(Loop *CurLoop, ScalarEvolution *SE,
                                      Instruction *&ValShiftedIsZero,
                                      Intrinsic::ID &IntrinID, Instruction *&IV,
                                      Value *&Start, Value *&Val,
                                      const SCEV *&ExtraOffsetExpr,
                                      bool &InvertedCond) {
  LLVM_DEBUG(dbgs() << DEBUG_TYPE
             " Performing shift-until-zero idiom detection.\n");

  // Give up if the loop has multiple blocks or multiple backedges.
  if (CurLoop->getNumBlocks() != 1 || CurLoop->getNumBackEdges() != 1) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad block/backedge count.\n");
    return false;
  }

  Instruction *ValShifted, *NBits, *IVNext;
  Value *ExtraOffset;

  BasicBlock *LoopHeaderBB = CurLoop->getHeader();
  BasicBlock *LoopPreheaderBB = CurLoop->getLoopPreheader();
  assert(LoopPreheaderBB && "There is always a loop preheader.");

  using namespace PatternMatch;

  // Step 1: Check if the loop backedge, condition is in desirable form.

  ICmpInst::Predicate Pred;
  BasicBlock *TrueBB, *FalseBB;
  if (!match(LoopHeaderBB->getTerminator(),
             m_Br(m_Instruction(ValShiftedIsZero), m_BasicBlock(TrueBB),
                  m_BasicBlock(FalseBB))) ||
      !match(ValShiftedIsZero,
             m_ICmp(Pred, m_Instruction(ValShifted), m_Zero())) ||
      !ICmpInst::isEquality(Pred)) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad backedge structure.\n");
    return false;
  }

  // Step 2: Check if the comparison's operand is in desirable form.
  // FIXME: Val could be a one-input PHI node, which we should look past.
  if (!match(ValShifted, m_Shift(m_LoopInvariant(m_Value(Val), CurLoop),
                                 m_Instruction(NBits)))) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad comparisons value computation.\n");
    return false;
  }
  IntrinID = ValShifted->getOpcode() == Instruction::Shl ? Intrinsic::cttz
                                                         : Intrinsic::ctlz;

  // Step 3: Check if the shift amount is in desirable form.

  if (match(NBits, m_c_Add(m_Instruction(IV),
                           m_LoopInvariant(m_Value(ExtraOffset), CurLoop))) &&
      (NBits->hasNoSignedWrap() || NBits->hasNoUnsignedWrap()))
    ExtraOffsetExpr = SE->getNegativeSCEV(SE->getSCEV(ExtraOffset));
  else if (match(NBits,
                 m_Sub(m_Instruction(IV),
                       m_LoopInvariant(m_Value(ExtraOffset), CurLoop))) &&
           NBits->hasNoSignedWrap())
    ExtraOffsetExpr = SE->getSCEV(ExtraOffset);
  else {
    IV = NBits;
    ExtraOffsetExpr = SE->getZero(NBits->getType());
  }

  // Step 4: Check if the recurrence is in desirable form.
  auto *IVPN = dyn_cast<PHINode>(IV);
  if (!IVPN || IVPN->getParent() != LoopHeaderBB) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Not an expected PHI node.\n");
    return false;
  }

  Start = IVPN->getIncomingValueForBlock(LoopPreheaderBB);
  IVNext = dyn_cast<Instruction>(IVPN->getIncomingValueForBlock(LoopHeaderBB));

  if (!IVNext || !match(IVNext, m_Add(m_Specific(IVPN), m_One()))) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad recurrence.\n");
    return false;
  }

  // Step 4: Check if the backedge's destinations are in desirable form.

  assert(ICmpInst::isEquality(Pred) &&
         "Should only get equality predicates here.");

  // cmp-br is commutative, so canonicalize to a single variant.
  InvertedCond = Pred != ICmpInst::Predicate::ICMP_EQ;
  if (InvertedCond) {
    Pred = ICmpInst::getInversePredicate(Pred);
    std::swap(TrueBB, FalseBB);
  }

  // We expect to exit loop when comparison yields true,
  // so when it yields false we should branch back to loop header.
  if (FalseBB != LoopHeaderBB) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Bad backedge flow.\n");
    return false;
  }

  // The new, countable, loop will certainly only run a known number of
  // iterations, It won't be infinite. But the old loop might be infinite
  // under certain conditions. For logical shifts, the value will become zero
  // after at most bitwidth(%Val) loop iterations. However, for arithmetic
  // right-shift, iff the sign bit was set, the value will never become zero,
  // and the loop may never finish.
  if (ValShifted->getOpcode() == Instruction::AShr &&
      !isMustProgress(CurLoop) && !SE->isKnownNonNegative(SE->getSCEV(Val))) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE " Can not prove the loop is finite.\n");
    return false;
  }

  // Okay, idiom checks out.
  return true;
}

/// Look for the following loop:
/// \code
///   entry:
///     <...>
///     %start = <...>
///     %extraoffset = <...>
///     <...>
///     br label %for.cond
///
///   loop:
///     %iv = phi i8 [ %start, %entry ], [ %iv.next, %for.cond ]
///     %nbits = add nsw i8 %iv, %extraoffset
///     %val.shifted = {{l,a}shr,shl} i8 %val, %nbits
///     %val.shifted.iszero = icmp eq i8 %val.shifted, 0
///     %iv.next = add i8 %iv, 1
///     <...>
///     br i1 %val.shifted.iszero, label %end, label %loop
///
///   end:
///     %iv.res = phi i8 [ %iv, %loop ] <...>
///     %nbits.res = phi i8 [ %nbits, %loop ] <...>
///     %val.shifted.res = phi i8 [ %val.shifted, %loop ] <...>
///     %val.shifted.iszero.res = phi i1 [ %val.shifted.iszero, %loop ] <...>
///     %iv.next.res = phi i8 [ %iv.next, %loop ] <...>
///     <...>
/// \endcode
///
/// And transform it into:
/// \code
///   entry:
///     <...>
///     %start = <...>
///     %extraoffset = <...>
///     <...>
///     %val.numleadingzeros = call i8 @llvm.ct{l,t}z.i8(i8 %val, i1 0)
///     %val.numactivebits = sub i8 8, %val.numleadingzeros
///     %extraoffset.neg = sub i8 0, %extraoffset
///     %tmp = add i8 %val.numactivebits, %extraoffset.neg
///     %iv.final = call i8 @llvm.smax.i8(i8 %tmp, i8 %start)
///     %loop.tripcount = sub i8 %iv.final, %start
///     br label %loop
///
///   loop:
///     %loop.iv = phi i8 [ 0, %entry ], [ %loop.iv.next, %loop ]
///     %loop.iv.next = add i8 %loop.iv, 1
///     %loop.ivcheck = icmp eq i8 %loop.iv.next, %loop.tripcount
///     %iv = add i8 %loop.iv, %start
///     <...>
///     br i1 %loop.ivcheck, label %end, label %loop
///
///   end:
///     %iv.res = phi i8 [ %iv.final, %loop ] <...>
///     <...>
/// \endcode
bool LoopIdiomRecognize::recognizeShiftUntilZero() {
  bool MadeChange = false;

  Instruction *ValShiftedIsZero;
  Intrinsic::ID IntrID;
  Instruction *IV;
  Value *Start, *Val;
  const SCEV *ExtraOffsetExpr;
  bool InvertedCond;
  if (!detectShiftUntilZeroIdiom(CurLoop, SE, ValShiftedIsZero, IntrID, IV,
                                 Start, Val, ExtraOffsetExpr, InvertedCond)) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE
               " shift-until-zero idiom detection failed.\n");
    return MadeChange;
  }
  LLVM_DEBUG(dbgs() << DEBUG_TYPE " shift-until-zero idiom detected!\n");

  // Ok, it is the idiom we were looking for, we *could* transform this loop,
  // but is it profitable to transform?

  BasicBlock *LoopHeaderBB = CurLoop->getHeader();
  BasicBlock *LoopPreheaderBB = CurLoop->getLoopPreheader();
  assert(LoopPreheaderBB && "There is always a loop preheader.");

  BasicBlock *SuccessorBB = CurLoop->getExitBlock();
  assert(SuccessorBB && "There is only a single successor.");

  IRBuilder<> Builder(LoopPreheaderBB->getTerminator());
  Builder.SetCurrentDebugLocation(IV->getDebugLoc());

  Type *Ty = Val->getType();
  unsigned Bitwidth = Ty->getScalarSizeInBits();

  TargetTransformInfo::TargetCostKind CostKind =
      TargetTransformInfo::TCK_SizeAndLatency;

  // The rewrite is considered to be unprofitable iff and only iff the
  // intrinsic we'll use are not cheap. Note that we are okay with *just*
  // making the loop countable, even if nothing else changes.
  IntrinsicCostAttributes Attrs(
      IntrID, Ty, {PoisonValue::get(Ty), /*is_zero_poison=*/Builder.getFalse()});
  InstructionCost Cost = TTI->getIntrinsicInstrCost(Attrs, CostKind);
  if (Cost > TargetTransformInfo::TCC_Basic) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE
               " Intrinsic is too costly, not beneficial\n");
    return MadeChange;
  }

  // Ok, transform appears worthwhile.
  MadeChange = true;

  bool OffsetIsZero = false;
  if (auto *ExtraOffsetExprC = dyn_cast<SCEVConstant>(ExtraOffsetExpr))
    OffsetIsZero = ExtraOffsetExprC->isZero();

  // Step 1: Compute the loop's final IV value / trip count.

  CallInst *ValNumLeadingZeros = Builder.CreateIntrinsic(
      IntrID, Ty, {Val, /*is_zero_poison=*/Builder.getFalse()},
      /*FMFSource=*/nullptr, Val->getName() + ".numleadingzeros");
  Value *ValNumActiveBits = Builder.CreateSub(
      ConstantInt::get(Ty, Ty->getScalarSizeInBits()), ValNumLeadingZeros,
      Val->getName() + ".numactivebits", /*HasNUW=*/true,
      /*HasNSW=*/Bitwidth != 2);

  SCEVExpander Expander(*SE, *DL, "loop-idiom");
  Expander.setInsertPoint(&*Builder.GetInsertPoint());
  Value *ExtraOffset = Expander.expandCodeFor(ExtraOffsetExpr);

  Value *ValNumActiveBitsOffset = Builder.CreateAdd(
      ValNumActiveBits, ExtraOffset, ValNumActiveBits->getName() + ".offset",
      /*HasNUW=*/OffsetIsZero, /*HasNSW=*/true);
  Value *IVFinal = Builder.CreateIntrinsic(Intrinsic::smax, {Ty},
                                           {ValNumActiveBitsOffset, Start},
                                           /*FMFSource=*/nullptr, "iv.final");

  auto *LoopBackedgeTakenCount = cast<Instruction>(Builder.CreateSub(
      IVFinal, Start, CurLoop->getName() + ".backedgetakencount",
      /*HasNUW=*/OffsetIsZero, /*HasNSW=*/true));
  // FIXME: or when the offset was `add nuw`

  // We know loop's backedge-taken count, but what's loop's trip count?
  Value *LoopTripCount =
      Builder.CreateAdd(LoopBackedgeTakenCount, ConstantInt::get(Ty, 1),
                        CurLoop->getName() + ".tripcount", /*HasNUW=*/true,
                        /*HasNSW=*/Bitwidth != 2);

  // Step 2: Adjust the successor basic block to recieve the original
  //         induction variable's final value instead of the orig. IV itself.

  IV->replaceUsesOutsideBlock(IVFinal, LoopHeaderBB);

  // Step 3: Rewrite the loop into a countable form, with canonical IV.

  // The new canonical induction variable.
  Builder.SetInsertPoint(LoopHeaderBB, LoopHeaderBB->begin());
  auto *CIV = Builder.CreatePHI(Ty, 2, CurLoop->getName() + ".iv");

  // The induction itself.
  Builder.SetInsertPoint(LoopHeaderBB, LoopHeaderBB->getFirstNonPHIIt());
  auto *CIVNext =
      Builder.CreateAdd(CIV, ConstantInt::get(Ty, 1), CIV->getName() + ".next",
                        /*HasNUW=*/true, /*HasNSW=*/Bitwidth != 2);

  // The loop trip count check.
  auto *CIVCheck = Builder.CreateICmpEQ(CIVNext, LoopTripCount,
                                        CurLoop->getName() + ".ivcheck");
  auto *NewIVCheck = CIVCheck;
  if (InvertedCond) {
    NewIVCheck = Builder.CreateNot(CIVCheck);
    NewIVCheck->takeName(ValShiftedIsZero);
  }

  // The original IV, but rebased to be an offset to the CIV.
  auto *IVDePHId = Builder.CreateAdd(CIV, Start, "", /*HasNUW=*/false,
                                     /*HasNSW=*/true); // FIXME: what about NUW?
  IVDePHId->takeName(IV);

  // The loop terminator.
  Builder.SetInsertPoint(LoopHeaderBB->getTerminator());
  Builder.CreateCondBr(CIVCheck, SuccessorBB, LoopHeaderBB);
  LoopHeaderBB->getTerminator()->eraseFromParent();

  // Populate the IV PHI.
  CIV->addIncoming(ConstantInt::get(Ty, 0), LoopPreheaderBB);
  CIV->addIncoming(CIVNext, LoopHeaderBB);

  // Step 4: Forget the "non-computable" trip-count SCEV associated with the
  //   loop. The loop would otherwise not be deleted even if it becomes empty.

  SE->forgetLoop(CurLoop);

  // Step 5: Try to cleanup the loop's body somewhat.
  IV->replaceAllUsesWith(IVDePHId);
  IV->eraseFromParent();

  ValShiftedIsZero->replaceAllUsesWith(NewIVCheck);
  ValShiftedIsZero->eraseFromParent();

  // Other passes will take care of actually deleting the loop if possible.

  LLVM_DEBUG(dbgs() << DEBUG_TYPE " shift-until-zero idiom optimized!\n");

  ++NumShiftUntilZero;
  return MadeChange;
}
