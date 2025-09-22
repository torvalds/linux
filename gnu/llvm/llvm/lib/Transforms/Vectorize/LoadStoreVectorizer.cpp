//===- LoadStoreVectorizer.cpp - GPU Load & Store Vectorizer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass merges loads/stores to/from sequential memory addresses into vector
// loads/stores.  Although there's nothing GPU-specific in here, this pass is
// motivated by the microarchitectural quirks of nVidia and AMD GPUs.
//
// (For simplicity below we talk about loads only, but everything also applies
// to stores.)
//
// This pass is intended to be run late in the pipeline, after other
// vectorization opportunities have been exploited.  So the assumption here is
// that immediately following our new vector load we'll need to extract out the
// individual elements of the load, so we can operate on them individually.
//
// On CPUs this transformation is usually not beneficial, because extracting the
// elements of a vector register is expensive on most architectures.  It's
// usually better just to load each element individually into its own scalar
// register.
//
// However, nVidia and AMD GPUs don't have proper vector registers.  Instead, a
// "vector load" loads directly into a series of scalar registers.  In effect,
// extracting the elements of the vector is free.  It's therefore always
// beneficial to vectorize a sequence of loads on these architectures.
//
// Vectorizing (perhaps a better name might be "coalescing") loads can have
// large performance impacts on GPU kernels, and opportunities for vectorizing
// are common in GPU code.  This pass tries very hard to find such
// opportunities; its runtime is quadratic in the number of loads in a BB.
//
// Some CPU architectures, such as ARM, have instructions that load into
// multiple scalar registers, similar to a GPU vectorized load.  In theory ARM
// could use this pass (with some modifications), but currently it implements
// its own pass to do something similar to what we do here.
//
// Overview of the algorithm and terminology in this pass:
//
//  - Break up each basic block into pseudo-BBs, composed of instructions which
//    are guaranteed to transfer control to their successors.
//  - Within a single pseudo-BB, find all loads, and group them into
//    "equivalence classes" according to getUnderlyingObject() and loaded
//    element size.  Do the same for stores.
//  - For each equivalence class, greedily build "chains".  Each chain has a
//    leader instruction, and every other member of the chain has a known
//    constant offset from the first instr in the chain.
//  - Break up chains so that they contain only contiguous accesses of legal
//    size with no intervening may-alias instrs.
//  - Convert each chain to vector instructions.
//
// The O(n^2) behavior of this pass comes from initially building the chains.
// In the worst case we have to compare each new instruction to all of those
// that came before. To limit this, we only calculate the offset to the leaders
// of the N most recently-used chains.

#include "llvm/Transforms/Vectorize/LoadStoreVectorizer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <numeric>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "load-store-vectorizer"

STATISTIC(NumVectorInstructions, "Number of vector accesses generated");
STATISTIC(NumScalarsVectorized, "Number of scalar accesses vectorized");

namespace {

// Equivalence class key, the initial tuple by which we group loads/stores.
// Loads/stores with different EqClassKeys are never merged.
//
// (We could in theory remove element-size from the this tuple.  We'd just need
// to fix up the vector packing/unpacking code.)
using EqClassKey =
    std::tuple<const Value * /* result of getUnderlyingObject() */,
               unsigned /* AddrSpace */,
               unsigned /* Load/Store element size bits */,
               char /* IsLoad; char b/c bool can't be a DenseMap key */
               >;
[[maybe_unused]] llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                               const EqClassKey &K) {
  const auto &[UnderlyingObject, AddrSpace, ElementSize, IsLoad] = K;
  return OS << (IsLoad ? "load" : "store") << " of " << *UnderlyingObject
            << " of element size " << ElementSize << " bits in addrspace "
            << AddrSpace;
}

// A Chain is a set of instructions such that:
//  - All instructions have the same equivalence class, so in particular all are
//    loads, or all are stores.
//  - We know the address accessed by the i'th chain elem relative to the
//    chain's leader instruction, which is the first instr of the chain in BB
//    order.
//
// Chains have two canonical orderings:
//  - BB order, sorted by Instr->comesBefore.
//  - Offset order, sorted by OffsetFromLeader.
// This pass switches back and forth between these orders.
struct ChainElem {
  Instruction *Inst;
  APInt OffsetFromLeader;
};
using Chain = SmallVector<ChainElem, 1>;

void sortChainInBBOrder(Chain &C) {
  sort(C, [](auto &A, auto &B) { return A.Inst->comesBefore(B.Inst); });
}

void sortChainInOffsetOrder(Chain &C) {
  sort(C, [](const auto &A, const auto &B) {
    if (A.OffsetFromLeader != B.OffsetFromLeader)
      return A.OffsetFromLeader.slt(B.OffsetFromLeader);
    return A.Inst->comesBefore(B.Inst); // stable tiebreaker
  });
}

[[maybe_unused]] void dumpChain(ArrayRef<ChainElem> C) {
  for (const auto &E : C) {
    dbgs() << "  " << *E.Inst << " (offset " << E.OffsetFromLeader << ")\n";
  }
}

using EquivalenceClassMap =
    MapVector<EqClassKey, SmallVector<Instruction *, 8>>;

// FIXME: Assuming stack alignment of 4 is always good enough
constexpr unsigned StackAdjustedAlignment = 4;

Instruction *propagateMetadata(Instruction *I, const Chain &C) {
  SmallVector<Value *, 8> Values;
  for (const ChainElem &E : C)
    Values.push_back(E.Inst);
  return propagateMetadata(I, Values);
}

bool isInvariantLoad(const Instruction *I) {
  const LoadInst *LI = dyn_cast<LoadInst>(I);
  return LI != nullptr && LI->hasMetadata(LLVMContext::MD_invariant_load);
}

/// Reorders the instructions that I depends on (the instructions defining its
/// operands), to ensure they dominate I.
void reorder(Instruction *I) {
  SmallPtrSet<Instruction *, 16> InstructionsToMove;
  SmallVector<Instruction *, 16> Worklist;

  Worklist.push_back(I);
  while (!Worklist.empty()) {
    Instruction *IW = Worklist.pop_back_val();
    int NumOperands = IW->getNumOperands();
    for (int i = 0; i < NumOperands; i++) {
      Instruction *IM = dyn_cast<Instruction>(IW->getOperand(i));
      if (!IM || IM->getOpcode() == Instruction::PHI)
        continue;

      // If IM is in another BB, no need to move it, because this pass only
      // vectorizes instructions within one BB.
      if (IM->getParent() != I->getParent())
        continue;

      if (!IM->comesBefore(I)) {
        InstructionsToMove.insert(IM);
        Worklist.push_back(IM);
      }
    }
  }

  // All instructions to move should follow I. Start from I, not from begin().
  for (auto BBI = I->getIterator(), E = I->getParent()->end(); BBI != E;) {
    Instruction *IM = &*(BBI++);
    if (!InstructionsToMove.count(IM))
      continue;
    IM->moveBefore(I);
  }
}

class Vectorizer {
  Function &F;
  AliasAnalysis &AA;
  AssumptionCache &AC;
  DominatorTree &DT;
  ScalarEvolution &SE;
  TargetTransformInfo &TTI;
  const DataLayout &DL;
  IRBuilder<> Builder;

  // We could erase instrs right after vectorizing them, but that can mess up
  // our BB iterators, and also can make the equivalence class keys point to
  // freed memory.  This is fixable, but it's simpler just to wait until we're
  // done with the BB and erase all at once.
  SmallVector<Instruction *, 128> ToErase;

public:
  Vectorizer(Function &F, AliasAnalysis &AA, AssumptionCache &AC,
             DominatorTree &DT, ScalarEvolution &SE, TargetTransformInfo &TTI)
      : F(F), AA(AA), AC(AC), DT(DT), SE(SE), TTI(TTI),
        DL(F.getDataLayout()), Builder(SE.getContext()) {}

  bool run();

private:
  static const unsigned MaxDepth = 3;

  /// Runs the vectorizer on a "pseudo basic block", which is a range of
  /// instructions [Begin, End) within one BB all of which have
  /// isGuaranteedToTransferExecutionToSuccessor(I) == true.
  bool runOnPseudoBB(BasicBlock::iterator Begin, BasicBlock::iterator End);

  /// Runs the vectorizer on one equivalence class, i.e. one set of loads/stores
  /// in the same BB with the same value for getUnderlyingObject() etc.
  bool runOnEquivalenceClass(const EqClassKey &EqClassKey,
                             ArrayRef<Instruction *> EqClass);

  /// Runs the vectorizer on one chain, i.e. a subset of an equivalence class
  /// where all instructions access a known, constant offset from the first
  /// instruction.
  bool runOnChain(Chain &C);

  /// Splits the chain into subchains of instructions which read/write a
  /// contiguous block of memory.  Discards any length-1 subchains (because
  /// there's nothing to vectorize in there).
  std::vector<Chain> splitChainByContiguity(Chain &C);

  /// Splits the chain into subchains where it's safe to hoist loads up to the
  /// beginning of the sub-chain and it's safe to sink loads up to the end of
  /// the sub-chain.  Discards any length-1 subchains.
  std::vector<Chain> splitChainByMayAliasInstrs(Chain &C);

  /// Splits the chain into subchains that make legal, aligned accesses.
  /// Discards any length-1 subchains.
  std::vector<Chain> splitChainByAlignment(Chain &C);

  /// Converts the instrs in the chain into a single vectorized load or store.
  /// Adds the old scalar loads/stores to ToErase.
  bool vectorizeChain(Chain &C);

  /// Tries to compute the offset in bytes PtrB - PtrA.
  std::optional<APInt> getConstantOffset(Value *PtrA, Value *PtrB,
                                         Instruction *ContextInst,
                                         unsigned Depth = 0);
  std::optional<APInt> getConstantOffsetComplexAddrs(Value *PtrA, Value *PtrB,
                                                     Instruction *ContextInst,
                                                     unsigned Depth);
  std::optional<APInt> getConstantOffsetSelects(Value *PtrA, Value *PtrB,
                                                Instruction *ContextInst,
                                                unsigned Depth);

  /// Gets the element type of the vector that the chain will load or store.
  /// This is nontrivial because the chain may contain elements of different
  /// types; e.g. it's legal to have a chain that contains both i32 and float.
  Type *getChainElemTy(const Chain &C);

  /// Determines whether ChainElem can be moved up (if IsLoad) or down (if
  /// !IsLoad) to ChainBegin -- i.e. there are no intervening may-alias
  /// instructions.
  ///
  /// The map ChainElemOffsets must contain all of the elements in
  /// [ChainBegin, ChainElem] and their offsets from some arbitrary base
  /// address.  It's ok if it contains additional entries.
  template <bool IsLoadChain>
  bool isSafeToMove(
      Instruction *ChainElem, Instruction *ChainBegin,
      const DenseMap<Instruction *, APInt /*OffsetFromLeader*/> &ChainOffsets);

  /// Collects loads and stores grouped by "equivalence class", where:
  ///   - all elements in an eq class are a load or all are a store,
  ///   - they all load/store the same element size (it's OK to have e.g. i8 and
  ///     <4 x i8> in the same class, but not i32 and <4 x i8>), and
  ///   - they all have the same value for getUnderlyingObject().
  EquivalenceClassMap collectEquivalenceClasses(BasicBlock::iterator Begin,
                                                BasicBlock::iterator End);

  /// Partitions Instrs into "chains" where every instruction has a known
  /// constant offset from the first instr in the chain.
  ///
  /// Postcondition: For all i, ret[i][0].second == 0, because the first instr
  /// in the chain is the leader, and an instr touches distance 0 from itself.
  std::vector<Chain> gatherChains(ArrayRef<Instruction *> Instrs);
};

class LoadStoreVectorizerLegacyPass : public FunctionPass {
public:
  static char ID;

  LoadStoreVectorizerLegacyPass() : FunctionPass(ID) {
    initializeLoadStoreVectorizerLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    return "GPU Load and Store Vectorizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.setPreservesCFG();
  }
};

} // end anonymous namespace

char LoadStoreVectorizerLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(LoadStoreVectorizerLegacyPass, DEBUG_TYPE,
                      "Vectorize load and Store instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(SCEVAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker);
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(LoadStoreVectorizerLegacyPass, DEBUG_TYPE,
                    "Vectorize load and store instructions", false, false)

Pass *llvm::createLoadStoreVectorizerPass() {
  return new LoadStoreVectorizerLegacyPass();
}

bool LoadStoreVectorizerLegacyPass::runOnFunction(Function &F) {
  // Don't vectorize when the attribute NoImplicitFloat is used.
  if (skipFunction(F) || F.hasFnAttribute(Attribute::NoImplicitFloat))
    return false;

  AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  TargetTransformInfo &TTI =
      getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  AssumptionCache &AC =
      getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

  return Vectorizer(F, AA, AC, DT, SE, TTI).run();
}

PreservedAnalyses LoadStoreVectorizerPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  // Don't vectorize when the attribute NoImplicitFloat is used.
  if (F.hasFnAttribute(Attribute::NoImplicitFloat))
    return PreservedAnalyses::all();

  AliasAnalysis &AA = AM.getResult<AAManager>(F);
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  TargetTransformInfo &TTI = AM.getResult<TargetIRAnalysis>(F);
  AssumptionCache &AC = AM.getResult<AssumptionAnalysis>(F);

  bool Changed = Vectorizer(F, AA, AC, DT, SE, TTI).run();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return Changed ? PA : PreservedAnalyses::all();
}

bool Vectorizer::run() {
  bool Changed = false;
  // Break up the BB if there are any instrs which aren't guaranteed to transfer
  // execution to their successor.
  //
  // Consider, for example:
  //
  //   def assert_arr_len(int n) { if (n < 2) exit(); }
  //
  //   load arr[0]
  //   call assert_array_len(arr.length)
  //   load arr[1]
  //
  // Even though assert_arr_len does not read or write any memory, we can't
  // speculate the second load before the call.  More info at
  // https://github.com/llvm/llvm-project/issues/52950.
  for (BasicBlock *BB : post_order(&F)) {
    // BB must at least have a terminator.
    assert(!BB->empty());

    SmallVector<BasicBlock::iterator, 8> Barriers;
    Barriers.push_back(BB->begin());
    for (Instruction &I : *BB)
      if (!isGuaranteedToTransferExecutionToSuccessor(&I))
        Barriers.push_back(I.getIterator());
    Barriers.push_back(BB->end());

    for (auto It = Barriers.begin(), End = std::prev(Barriers.end()); It != End;
         ++It)
      Changed |= runOnPseudoBB(*It, *std::next(It));

    for (Instruction *I : ToErase) {
      auto *PtrOperand = getLoadStorePointerOperand(I);
      if (I->use_empty())
        I->eraseFromParent();
      RecursivelyDeleteTriviallyDeadInstructions(PtrOperand);
    }
    ToErase.clear();
  }

  return Changed;
}

bool Vectorizer::runOnPseudoBB(BasicBlock::iterator Begin,
                               BasicBlock::iterator End) {
  LLVM_DEBUG({
    dbgs() << "LSV: Running on pseudo-BB [" << *Begin << " ... ";
    if (End != Begin->getParent()->end())
      dbgs() << *End;
    else
      dbgs() << "<BB end>";
    dbgs() << ")\n";
  });

  bool Changed = false;
  for (const auto &[EqClassKey, EqClass] :
       collectEquivalenceClasses(Begin, End))
    Changed |= runOnEquivalenceClass(EqClassKey, EqClass);

  return Changed;
}

bool Vectorizer::runOnEquivalenceClass(const EqClassKey &EqClassKey,
                                       ArrayRef<Instruction *> EqClass) {
  bool Changed = false;

  LLVM_DEBUG({
    dbgs() << "LSV: Running on equivalence class of size " << EqClass.size()
           << " keyed on " << EqClassKey << ":\n";
    for (Instruction *I : EqClass)
      dbgs() << "  " << *I << "\n";
  });

  std::vector<Chain> Chains = gatherChains(EqClass);
  LLVM_DEBUG(dbgs() << "LSV: Got " << Chains.size()
                    << " nontrivial chains.\n";);
  for (Chain &C : Chains)
    Changed |= runOnChain(C);
  return Changed;
}

bool Vectorizer::runOnChain(Chain &C) {
  LLVM_DEBUG({
    dbgs() << "LSV: Running on chain with " << C.size() << " instructions:\n";
    dumpChain(C);
  });

  // Split up the chain into increasingly smaller chains, until we can finally
  // vectorize the chains.
  //
  // (Don't be scared by the depth of the loop nest here.  These operations are
  // all at worst O(n lg n) in the number of instructions, and splitting chains
  // doesn't change the number of instrs.  So the whole loop nest is O(n lg n).)
  bool Changed = false;
  for (auto &C : splitChainByMayAliasInstrs(C))
    for (auto &C : splitChainByContiguity(C))
      for (auto &C : splitChainByAlignment(C))
        Changed |= vectorizeChain(C);
  return Changed;
}

std::vector<Chain> Vectorizer::splitChainByMayAliasInstrs(Chain &C) {
  if (C.empty())
    return {};

  sortChainInBBOrder(C);

  LLVM_DEBUG({
    dbgs() << "LSV: splitChainByMayAliasInstrs considering chain:\n";
    dumpChain(C);
  });

  // We know that elements in the chain with nonverlapping offsets can't
  // alias, but AA may not be smart enough to figure this out.  Use a
  // hashtable.
  DenseMap<Instruction *, APInt /*OffsetFromLeader*/> ChainOffsets;
  for (const auto &E : C)
    ChainOffsets.insert({&*E.Inst, E.OffsetFromLeader});

  // Loads get hoisted up to the first load in the chain.  Stores get sunk
  // down to the last store in the chain.  Our algorithm for loads is:
  //
  //  - Take the first element of the chain.  This is the start of a new chain.
  //  - Take the next element of `Chain` and check for may-alias instructions
  //    up to the start of NewChain.  If no may-alias instrs, add it to
  //    NewChain.  Otherwise, start a new NewChain.
  //
  // For stores it's the same except in the reverse direction.
  //
  // We expect IsLoad to be an std::bool_constant.
  auto Impl = [&](auto IsLoad) {
    // MSVC is unhappy if IsLoad is a capture, so pass it as an arg.
    auto [ChainBegin, ChainEnd] = [&](auto IsLoad) {
      if constexpr (IsLoad())
        return std::make_pair(C.begin(), C.end());
      else
        return std::make_pair(C.rbegin(), C.rend());
    }(IsLoad);
    assert(ChainBegin != ChainEnd);

    std::vector<Chain> Chains;
    SmallVector<ChainElem, 1> NewChain;
    NewChain.push_back(*ChainBegin);
    for (auto ChainIt = std::next(ChainBegin); ChainIt != ChainEnd; ++ChainIt) {
      if (isSafeToMove<IsLoad>(ChainIt->Inst, NewChain.front().Inst,
                               ChainOffsets)) {
        LLVM_DEBUG(dbgs() << "LSV: No intervening may-alias instrs; can merge "
                          << *ChainIt->Inst << " into " << *ChainBegin->Inst
                          << "\n");
        NewChain.push_back(*ChainIt);
      } else {
        LLVM_DEBUG(
            dbgs() << "LSV: Found intervening may-alias instrs; cannot merge "
                   << *ChainIt->Inst << " into " << *ChainBegin->Inst << "\n");
        if (NewChain.size() > 1) {
          LLVM_DEBUG({
            dbgs() << "LSV: got nontrivial chain without aliasing instrs:\n";
            dumpChain(NewChain);
          });
          Chains.push_back(std::move(NewChain));
        }

        // Start a new chain.
        NewChain = SmallVector<ChainElem, 1>({*ChainIt});
      }
    }
    if (NewChain.size() > 1) {
      LLVM_DEBUG({
        dbgs() << "LSV: got nontrivial chain without aliasing instrs:\n";
        dumpChain(NewChain);
      });
      Chains.push_back(std::move(NewChain));
    }
    return Chains;
  };

  if (isa<LoadInst>(C[0].Inst))
    return Impl(/*IsLoad=*/std::bool_constant<true>());

  assert(isa<StoreInst>(C[0].Inst));
  return Impl(/*IsLoad=*/std::bool_constant<false>());
}

std::vector<Chain> Vectorizer::splitChainByContiguity(Chain &C) {
  if (C.empty())
    return {};

  sortChainInOffsetOrder(C);

  LLVM_DEBUG({
    dbgs() << "LSV: splitChainByContiguity considering chain:\n";
    dumpChain(C);
  });

  std::vector<Chain> Ret;
  Ret.push_back({C.front()});

  for (auto It = std::next(C.begin()), End = C.end(); It != End; ++It) {
    // `prev` accesses offsets [PrevDistFromBase, PrevReadEnd).
    auto &CurChain = Ret.back();
    const ChainElem &Prev = CurChain.back();
    unsigned SzBits = DL.getTypeSizeInBits(getLoadStoreType(&*Prev.Inst));
    assert(SzBits % 8 == 0 && "Non-byte sizes should have been filtered out by "
                              "collectEquivalenceClass");
    APInt PrevReadEnd = Prev.OffsetFromLeader + SzBits / 8;

    // Add this instruction to the end of the current chain, or start a new one.
    bool AreContiguous = It->OffsetFromLeader == PrevReadEnd;
    LLVM_DEBUG(dbgs() << "LSV: Instructions are "
                      << (AreContiguous ? "" : "not ") << "contiguous: "
                      << *Prev.Inst << " (ends at offset " << PrevReadEnd
                      << ") -> " << *It->Inst << " (starts at offset "
                      << It->OffsetFromLeader << ")\n");
    if (AreContiguous)
      CurChain.push_back(*It);
    else
      Ret.push_back({*It});
  }

  // Filter out length-1 chains, these are uninteresting.
  llvm::erase_if(Ret, [](const auto &Chain) { return Chain.size() <= 1; });
  return Ret;
}

Type *Vectorizer::getChainElemTy(const Chain &C) {
  assert(!C.empty());
  // The rules are:
  //  - If there are any pointer types in the chain, use an integer type.
  //  - Prefer an integer type if it appears in the chain.
  //  - Otherwise, use the first type in the chain.
  //
  // The rule about pointer types is a simplification when we merge e.g.  a load
  // of a ptr and a double.  There's no direct conversion from a ptr to a
  // double; it requires a ptrtoint followed by a bitcast.
  //
  // It's unclear to me if the other rules have any practical effect, but we do
  // it to match this pass's previous behavior.
  if (any_of(C, [](const ChainElem &E) {
        return getLoadStoreType(E.Inst)->getScalarType()->isPointerTy();
      })) {
    return Type::getIntNTy(
        F.getContext(),
        DL.getTypeSizeInBits(getLoadStoreType(C[0].Inst)->getScalarType()));
  }

  for (const ChainElem &E : C)
    if (Type *T = getLoadStoreType(E.Inst)->getScalarType(); T->isIntegerTy())
      return T;
  return getLoadStoreType(C[0].Inst)->getScalarType();
}

std::vector<Chain> Vectorizer::splitChainByAlignment(Chain &C) {
  // We use a simple greedy algorithm.
  //  - Given a chain of length N, find all prefixes that
  //    (a) are not longer than the max register length, and
  //    (b) are a power of 2.
  //  - Starting from the longest prefix, try to create a vector of that length.
  //  - If one of them works, great.  Repeat the algorithm on any remaining
  //    elements in the chain.
  //  - If none of them work, discard the first element and repeat on a chain
  //    of length N-1.
  if (C.empty())
    return {};

  sortChainInOffsetOrder(C);

  LLVM_DEBUG({
    dbgs() << "LSV: splitChainByAlignment considering chain:\n";
    dumpChain(C);
  });

  bool IsLoadChain = isa<LoadInst>(C[0].Inst);
  auto getVectorFactor = [&](unsigned VF, unsigned LoadStoreSize,
                             unsigned ChainSizeBytes, VectorType *VecTy) {
    return IsLoadChain ? TTI.getLoadVectorFactor(VF, LoadStoreSize,
                                                 ChainSizeBytes, VecTy)
                       : TTI.getStoreVectorFactor(VF, LoadStoreSize,
                                                  ChainSizeBytes, VecTy);
  };

#ifndef NDEBUG
  for (const auto &E : C) {
    Type *Ty = getLoadStoreType(E.Inst)->getScalarType();
    assert(isPowerOf2_32(DL.getTypeSizeInBits(Ty)) &&
           "Should have filtered out non-power-of-two elements in "
           "collectEquivalenceClasses.");
  }
#endif

  unsigned AS = getLoadStoreAddressSpace(C[0].Inst);
  unsigned VecRegBytes = TTI.getLoadStoreVecRegBitWidth(AS) / 8;

  std::vector<Chain> Ret;
  for (unsigned CBegin = 0; CBegin < C.size(); ++CBegin) {
    // Find candidate chains of size not greater than the largest vector reg.
    // These chains are over the closed interval [CBegin, CEnd].
    SmallVector<std::pair<unsigned /*CEnd*/, unsigned /*SizeBytes*/>, 8>
        CandidateChains;
    for (unsigned CEnd = CBegin + 1, Size = C.size(); CEnd < Size; ++CEnd) {
      APInt Sz = C[CEnd].OffsetFromLeader +
                 DL.getTypeStoreSize(getLoadStoreType(C[CEnd].Inst)) -
                 C[CBegin].OffsetFromLeader;
      if (Sz.sgt(VecRegBytes))
        break;
      CandidateChains.push_back(
          {CEnd, static_cast<unsigned>(Sz.getLimitedValue())});
    }

    // Consider the longest chain first.
    for (auto It = CandidateChains.rbegin(), End = CandidateChains.rend();
         It != End; ++It) {
      auto [CEnd, SizeBytes] = *It;
      LLVM_DEBUG(
          dbgs() << "LSV: splitChainByAlignment considering candidate chain ["
                 << *C[CBegin].Inst << " ... " << *C[CEnd].Inst << "]\n");

      Type *VecElemTy = getChainElemTy(C);
      // Note, VecElemTy is a power of 2, but might be less than one byte.  For
      // example, we can vectorize 2 x <2 x i4> to <4 x i4>, and in this case
      // VecElemTy would be i4.
      unsigned VecElemBits = DL.getTypeSizeInBits(VecElemTy);

      // SizeBytes and VecElemBits are powers of 2, so they divide evenly.
      assert((8 * SizeBytes) % VecElemBits == 0);
      unsigned NumVecElems = 8 * SizeBytes / VecElemBits;
      FixedVectorType *VecTy = FixedVectorType::get(VecElemTy, NumVecElems);
      unsigned VF = 8 * VecRegBytes / VecElemBits;

      // Check that TTI is happy with this vectorization factor.
      unsigned TargetVF = getVectorFactor(VF, VecElemBits,
                                          VecElemBits * NumVecElems / 8, VecTy);
      if (TargetVF != VF && TargetVF < NumVecElems) {
        LLVM_DEBUG(
            dbgs() << "LSV: splitChainByAlignment discarding candidate chain "
                      "because TargetVF="
                   << TargetVF << " != VF=" << VF
                   << " and TargetVF < NumVecElems=" << NumVecElems << "\n");
        continue;
      }

      // Is a load/store with this alignment allowed by TTI and at least as fast
      // as an unvectorized load/store?
      //
      // TTI and F are passed as explicit captures to WAR an MSVC misparse (??).
      auto IsAllowedAndFast = [&, SizeBytes = SizeBytes, &TTI = TTI,
                               &F = F](Align Alignment) {
        if (Alignment.value() % SizeBytes == 0)
          return true;
        unsigned VectorizedSpeed = 0;
        bool AllowsMisaligned = TTI.allowsMisalignedMemoryAccesses(
            F.getContext(), SizeBytes * 8, AS, Alignment, &VectorizedSpeed);
        if (!AllowsMisaligned) {
          LLVM_DEBUG(dbgs()
                     << "LSV: Access of " << SizeBytes << "B in addrspace "
                     << AS << " with alignment " << Alignment.value()
                     << " is misaligned, and therefore can't be vectorized.\n");
          return false;
        }

        unsigned ElementwiseSpeed = 0;
        (TTI).allowsMisalignedMemoryAccesses((F).getContext(), VecElemBits, AS,
                                             Alignment, &ElementwiseSpeed);
        if (VectorizedSpeed < ElementwiseSpeed) {
          LLVM_DEBUG(dbgs()
                     << "LSV: Access of " << SizeBytes << "B in addrspace "
                     << AS << " with alignment " << Alignment.value()
                     << " has relative speed " << VectorizedSpeed
                     << ", which is lower than the elementwise speed of "
                     << ElementwiseSpeed
                     << ".  Therefore this access won't be vectorized.\n");
          return false;
        }
        return true;
      };

      // If we're loading/storing from an alloca, align it if possible.
      //
      // FIXME: We eagerly upgrade the alignment, regardless of whether TTI
      // tells us this is beneficial.  This feels a bit odd, but it matches
      // existing tests.  This isn't *so* bad, because at most we align to 4
      // bytes (current value of StackAdjustedAlignment).
      //
      // FIXME: We will upgrade the alignment of the alloca even if it turns out
      // we can't vectorize for some other reason.
      Value *PtrOperand = getLoadStorePointerOperand(C[CBegin].Inst);
      bool IsAllocaAccess = AS == DL.getAllocaAddrSpace() &&
                            isa<AllocaInst>(PtrOperand->stripPointerCasts());
      Align Alignment = getLoadStoreAlignment(C[CBegin].Inst);
      Align PrefAlign = Align(StackAdjustedAlignment);
      if (IsAllocaAccess && Alignment.value() % SizeBytes != 0 &&
          IsAllowedAndFast(PrefAlign)) {
        Align NewAlign = getOrEnforceKnownAlignment(
            PtrOperand, PrefAlign, DL, C[CBegin].Inst, nullptr, &DT);
        if (NewAlign >= Alignment) {
          LLVM_DEBUG(dbgs()
                     << "LSV: splitByChain upgrading alloca alignment from "
                     << Alignment.value() << " to " << NewAlign.value()
                     << "\n");
          Alignment = NewAlign;
        }
      }

      if (!IsAllowedAndFast(Alignment)) {
        LLVM_DEBUG(
            dbgs() << "LSV: splitChainByAlignment discarding candidate chain "
                      "because its alignment is not AllowedAndFast: "
                   << Alignment.value() << "\n");
        continue;
      }

      if ((IsLoadChain &&
           !TTI.isLegalToVectorizeLoadChain(SizeBytes, Alignment, AS)) ||
          (!IsLoadChain &&
           !TTI.isLegalToVectorizeStoreChain(SizeBytes, Alignment, AS))) {
        LLVM_DEBUG(
            dbgs() << "LSV: splitChainByAlignment discarding candidate chain "
                      "because !isLegalToVectorizeLoad/StoreChain.");
        continue;
      }

      // Hooray, we can vectorize this chain!
      Chain &NewChain = Ret.emplace_back();
      for (unsigned I = CBegin; I <= CEnd; ++I)
        NewChain.push_back(C[I]);
      CBegin = CEnd; // Skip over the instructions we've added to the chain.
      break;
    }
  }
  return Ret;
}

bool Vectorizer::vectorizeChain(Chain &C) {
  if (C.size() < 2)
    return false;

  sortChainInOffsetOrder(C);

  LLVM_DEBUG({
    dbgs() << "LSV: Vectorizing chain of " << C.size() << " instructions:\n";
    dumpChain(C);
  });

  Type *VecElemTy = getChainElemTy(C);
  bool IsLoadChain = isa<LoadInst>(C[0].Inst);
  unsigned AS = getLoadStoreAddressSpace(C[0].Inst);
  unsigned ChainBytes = std::accumulate(
      C.begin(), C.end(), 0u, [&](unsigned Bytes, const ChainElem &E) {
        return Bytes + DL.getTypeStoreSize(getLoadStoreType(E.Inst));
      });
  assert(ChainBytes % DL.getTypeStoreSize(VecElemTy) == 0);
  // VecTy is a power of 2 and 1 byte at smallest, but VecElemTy may be smaller
  // than 1 byte (e.g. VecTy == <32 x i1>).
  Type *VecTy = FixedVectorType::get(
      VecElemTy, 8 * ChainBytes / DL.getTypeSizeInBits(VecElemTy));

  Align Alignment = getLoadStoreAlignment(C[0].Inst);
  // If this is a load/store of an alloca, we might have upgraded the alloca's
  // alignment earlier.  Get the new alignment.
  if (AS == DL.getAllocaAddrSpace()) {
    Alignment = std::max(
        Alignment,
        getOrEnforceKnownAlignment(getLoadStorePointerOperand(C[0].Inst),
                                   MaybeAlign(), DL, C[0].Inst, nullptr, &DT));
  }

  // All elements of the chain must have the same scalar-type size.
#ifndef NDEBUG
  for (const ChainElem &E : C)
    assert(DL.getTypeStoreSize(getLoadStoreType(E.Inst)->getScalarType()) ==
           DL.getTypeStoreSize(VecElemTy));
#endif

  Instruction *VecInst;
  if (IsLoadChain) {
    // Loads get hoisted to the location of the first load in the chain.  We may
    // also need to hoist the (transitive) operands of the loads.
    Builder.SetInsertPoint(
        llvm::min_element(C, [](const auto &A, const auto &B) {
          return A.Inst->comesBefore(B.Inst);
        })->Inst);

    // Chain is in offset order, so C[0] is the instr with the lowest offset,
    // i.e. the root of the vector.
    VecInst = Builder.CreateAlignedLoad(VecTy,
                                        getLoadStorePointerOperand(C[0].Inst),
                                        Alignment);

    unsigned VecIdx = 0;
    for (const ChainElem &E : C) {
      Instruction *I = E.Inst;
      Value *V;
      Type *T = getLoadStoreType(I);
      if (auto *VT = dyn_cast<FixedVectorType>(T)) {
        auto Mask = llvm::to_vector<8>(
            llvm::seq<int>(VecIdx, VecIdx + VT->getNumElements()));
        V = Builder.CreateShuffleVector(VecInst, Mask, I->getName());
        VecIdx += VT->getNumElements();
      } else {
        V = Builder.CreateExtractElement(VecInst, Builder.getInt32(VecIdx),
                                         I->getName());
        ++VecIdx;
      }
      if (V->getType() != I->getType())
        V = Builder.CreateBitOrPointerCast(V, I->getType());
      I->replaceAllUsesWith(V);
    }

    // Finally, we need to reorder the instrs in the BB so that the (transitive)
    // operands of VecInst appear before it.  To see why, suppose we have
    // vectorized the following code:
    //
    //   ptr1  = gep a, 1
    //   load1 = load i32 ptr1
    //   ptr0  = gep a, 0
    //   load0 = load i32 ptr0
    //
    // We will put the vectorized load at the location of the earliest load in
    // the BB, i.e. load1.  We get:
    //
    //   ptr1  = gep a, 1
    //   loadv = load <2 x i32> ptr0
    //   load0 = extractelement loadv, 0
    //   load1 = extractelement loadv, 1
    //   ptr0 = gep a, 0
    //
    // Notice that loadv uses ptr0, which is defined *after* it!
    reorder(VecInst);
  } else {
    // Stores get sunk to the location of the last store in the chain.
    Builder.SetInsertPoint(llvm::max_element(C, [](auto &A, auto &B) {
                             return A.Inst->comesBefore(B.Inst);
                           })->Inst);

    // Build the vector to store.
    Value *Vec = PoisonValue::get(VecTy);
    unsigned VecIdx = 0;
    auto InsertElem = [&](Value *V) {
      if (V->getType() != VecElemTy)
        V = Builder.CreateBitOrPointerCast(V, VecElemTy);
      Vec = Builder.CreateInsertElement(Vec, V, Builder.getInt32(VecIdx++));
    };
    for (const ChainElem &E : C) {
      auto I = cast<StoreInst>(E.Inst);
      if (FixedVectorType *VT =
              dyn_cast<FixedVectorType>(getLoadStoreType(I))) {
        for (int J = 0, JE = VT->getNumElements(); J < JE; ++J) {
          InsertElem(Builder.CreateExtractElement(I->getValueOperand(),
                                                  Builder.getInt32(J)));
        }
      } else {
        InsertElem(I->getValueOperand());
      }
    }

    // Chain is in offset order, so C[0] is the instr with the lowest offset,
    // i.e. the root of the vector.
    VecInst = Builder.CreateAlignedStore(
        Vec,
        getLoadStorePointerOperand(C[0].Inst),
        Alignment);
  }

  propagateMetadata(VecInst, C);

  for (const ChainElem &E : C)
    ToErase.push_back(E.Inst);

  ++NumVectorInstructions;
  NumScalarsVectorized += C.size();
  return true;
}

template <bool IsLoadChain>
bool Vectorizer::isSafeToMove(
    Instruction *ChainElem, Instruction *ChainBegin,
    const DenseMap<Instruction *, APInt /*OffsetFromLeader*/> &ChainOffsets) {
  LLVM_DEBUG(dbgs() << "LSV: isSafeToMove(" << *ChainElem << " -> "
                    << *ChainBegin << ")\n");

  assert(isa<LoadInst>(ChainElem) == IsLoadChain);
  if (ChainElem == ChainBegin)
    return true;

  // Invariant loads can always be reordered; by definition they are not
  // clobbered by stores.
  if (isInvariantLoad(ChainElem))
    return true;

  auto BBIt = std::next([&] {
    if constexpr (IsLoadChain)
      return BasicBlock::reverse_iterator(ChainElem);
    else
      return BasicBlock::iterator(ChainElem);
  }());
  auto BBItEnd = std::next([&] {
    if constexpr (IsLoadChain)
      return BasicBlock::reverse_iterator(ChainBegin);
    else
      return BasicBlock::iterator(ChainBegin);
  }());

  const APInt &ChainElemOffset = ChainOffsets.at(ChainElem);
  const unsigned ChainElemSize =
      DL.getTypeStoreSize(getLoadStoreType(ChainElem));

  for (; BBIt != BBItEnd; ++BBIt) {
    Instruction *I = &*BBIt;

    if (!I->mayReadOrWriteMemory())
      continue;

    // Loads can be reordered with other loads.
    if (IsLoadChain && isa<LoadInst>(I))
      continue;

    // Stores can be sunk below invariant loads.
    if (!IsLoadChain && isInvariantLoad(I))
      continue;

    // If I is in the chain, we can tell whether it aliases ChainIt by checking
    // what offset ChainIt accesses.  This may be better than AA is able to do.
    //
    // We should really only have duplicate offsets for stores (the duplicate
    // loads should be CSE'ed), but in case we have a duplicate load, we'll
    // split the chain so we don't have to handle this case specially.
    if (auto OffsetIt = ChainOffsets.find(I); OffsetIt != ChainOffsets.end()) {
      // I and ChainElem overlap if:
      //   - I and ChainElem have the same offset, OR
      //   - I's offset is less than ChainElem's, but I touches past the
      //     beginning of ChainElem, OR
      //   - ChainElem's offset is less than I's, but ChainElem touches past the
      //     beginning of I.
      const APInt &IOffset = OffsetIt->second;
      unsigned IElemSize = DL.getTypeStoreSize(getLoadStoreType(I));
      if (IOffset == ChainElemOffset ||
          (IOffset.sle(ChainElemOffset) &&
           (IOffset + IElemSize).sgt(ChainElemOffset)) ||
          (ChainElemOffset.sle(IOffset) &&
           (ChainElemOffset + ChainElemSize).sgt(OffsetIt->second))) {
        LLVM_DEBUG({
          // Double check that AA also sees this alias.  If not, we probably
          // have a bug.
          ModRefInfo MR = AA.getModRefInfo(I, MemoryLocation::get(ChainElem));
          assert(IsLoadChain ? isModSet(MR) : isModOrRefSet(MR));
          dbgs() << "LSV: Found alias in chain: " << *I << "\n";
        });
        return false; // We found an aliasing instruction; bail.
      }

      continue; // We're confident there's no alias.
    }

    LLVM_DEBUG(dbgs() << "LSV: Querying AA for " << *I << "\n");
    ModRefInfo MR = AA.getModRefInfo(I, MemoryLocation::get(ChainElem));
    if (IsLoadChain ? isModSet(MR) : isModOrRefSet(MR)) {
      LLVM_DEBUG(dbgs() << "LSV: Found alias in chain:\n"
                        << "  Aliasing instruction:\n"
                        << "    " << *I << '\n'
                        << "  Aliased instruction and pointer:\n"
                        << "    " << *ChainElem << '\n'
                        << "    " << *getLoadStorePointerOperand(ChainElem)
                        << '\n');

      return false;
    }
  }
  return true;
}

static bool checkNoWrapFlags(Instruction *I, bool Signed) {
  BinaryOperator *BinOpI = cast<BinaryOperator>(I);
  return (Signed && BinOpI->hasNoSignedWrap()) ||
         (!Signed && BinOpI->hasNoUnsignedWrap());
}

static bool checkIfSafeAddSequence(const APInt &IdxDiff, Instruction *AddOpA,
                                   unsigned MatchingOpIdxA, Instruction *AddOpB,
                                   unsigned MatchingOpIdxB, bool Signed) {
  LLVM_DEBUG(dbgs() << "LSV: checkIfSafeAddSequence IdxDiff=" << IdxDiff
                    << ", AddOpA=" << *AddOpA << ", MatchingOpIdxA="
                    << MatchingOpIdxA << ", AddOpB=" << *AddOpB
                    << ", MatchingOpIdxB=" << MatchingOpIdxB
                    << ", Signed=" << Signed << "\n");
  // If both OpA and OpB are adds with NSW/NUW and with one of the operands
  // being the same, we can guarantee that the transformation is safe if we can
  // prove that OpA won't overflow when Ret added to the other operand of OpA.
  // For example:
  //  %tmp7 = add nsw i32 %tmp2, %v0
  //  %tmp8 = sext i32 %tmp7 to i64
  //  ...
  //  %tmp11 = add nsw i32 %v0, 1
  //  %tmp12 = add nsw i32 %tmp2, %tmp11
  //  %tmp13 = sext i32 %tmp12 to i64
  //
  //  Both %tmp7 and %tmp12 have the nsw flag and the first operand is %tmp2.
  //  It's guaranteed that adding 1 to %tmp7 won't overflow because %tmp11 adds
  //  1 to %v0 and both %tmp11 and %tmp12 have the nsw flag.
  assert(AddOpA->getOpcode() == Instruction::Add &&
         AddOpB->getOpcode() == Instruction::Add &&
         checkNoWrapFlags(AddOpA, Signed) && checkNoWrapFlags(AddOpB, Signed));
  if (AddOpA->getOperand(MatchingOpIdxA) ==
      AddOpB->getOperand(MatchingOpIdxB)) {
    Value *OtherOperandA = AddOpA->getOperand(MatchingOpIdxA == 1 ? 0 : 1);
    Value *OtherOperandB = AddOpB->getOperand(MatchingOpIdxB == 1 ? 0 : 1);
    Instruction *OtherInstrA = dyn_cast<Instruction>(OtherOperandA);
    Instruction *OtherInstrB = dyn_cast<Instruction>(OtherOperandB);
    // Match `x +nsw/nuw y` and `x +nsw/nuw (y +nsw/nuw IdxDiff)`.
    if (OtherInstrB && OtherInstrB->getOpcode() == Instruction::Add &&
        checkNoWrapFlags(OtherInstrB, Signed) &&
        isa<ConstantInt>(OtherInstrB->getOperand(1))) {
      int64_t CstVal =
          cast<ConstantInt>(OtherInstrB->getOperand(1))->getSExtValue();
      if (OtherInstrB->getOperand(0) == OtherOperandA &&
          IdxDiff.getSExtValue() == CstVal)
        return true;
    }
    // Match `x +nsw/nuw (y +nsw/nuw -Idx)` and `x +nsw/nuw (y +nsw/nuw x)`.
    if (OtherInstrA && OtherInstrA->getOpcode() == Instruction::Add &&
        checkNoWrapFlags(OtherInstrA, Signed) &&
        isa<ConstantInt>(OtherInstrA->getOperand(1))) {
      int64_t CstVal =
          cast<ConstantInt>(OtherInstrA->getOperand(1))->getSExtValue();
      if (OtherInstrA->getOperand(0) == OtherOperandB &&
          IdxDiff.getSExtValue() == -CstVal)
        return true;
    }
    // Match `x +nsw/nuw (y +nsw/nuw c)` and
    // `x +nsw/nuw (y +nsw/nuw (c + IdxDiff))`.
    if (OtherInstrA && OtherInstrB &&
        OtherInstrA->getOpcode() == Instruction::Add &&
        OtherInstrB->getOpcode() == Instruction::Add &&
        checkNoWrapFlags(OtherInstrA, Signed) &&
        checkNoWrapFlags(OtherInstrB, Signed) &&
        isa<ConstantInt>(OtherInstrA->getOperand(1)) &&
        isa<ConstantInt>(OtherInstrB->getOperand(1))) {
      int64_t CstValA =
          cast<ConstantInt>(OtherInstrA->getOperand(1))->getSExtValue();
      int64_t CstValB =
          cast<ConstantInt>(OtherInstrB->getOperand(1))->getSExtValue();
      if (OtherInstrA->getOperand(0) == OtherInstrB->getOperand(0) &&
          IdxDiff.getSExtValue() == (CstValB - CstValA))
        return true;
    }
  }
  return false;
}

std::optional<APInt> Vectorizer::getConstantOffsetComplexAddrs(
    Value *PtrA, Value *PtrB, Instruction *ContextInst, unsigned Depth) {
  LLVM_DEBUG(dbgs() << "LSV: getConstantOffsetComplexAddrs PtrA=" << *PtrA
                    << " PtrB=" << *PtrB << " ContextInst=" << *ContextInst
                    << " Depth=" << Depth << "\n");
  auto *GEPA = dyn_cast<GetElementPtrInst>(PtrA);
  auto *GEPB = dyn_cast<GetElementPtrInst>(PtrB);
  if (!GEPA || !GEPB)
    return getConstantOffsetSelects(PtrA, PtrB, ContextInst, Depth);

  // Look through GEPs after checking they're the same except for the last
  // index.
  if (GEPA->getNumOperands() != GEPB->getNumOperands() ||
      GEPA->getPointerOperand() != GEPB->getPointerOperand())
    return std::nullopt;
  gep_type_iterator GTIA = gep_type_begin(GEPA);
  gep_type_iterator GTIB = gep_type_begin(GEPB);
  for (unsigned I = 0, E = GEPA->getNumIndices() - 1; I < E; ++I) {
    if (GTIA.getOperand() != GTIB.getOperand())
      return std::nullopt;
    ++GTIA;
    ++GTIB;
  }

  Instruction *OpA = dyn_cast<Instruction>(GTIA.getOperand());
  Instruction *OpB = dyn_cast<Instruction>(GTIB.getOperand());
  if (!OpA || !OpB || OpA->getOpcode() != OpB->getOpcode() ||
      OpA->getType() != OpB->getType())
    return std::nullopt;

  uint64_t Stride = GTIA.getSequentialElementStride(DL);

  // Only look through a ZExt/SExt.
  if (!isa<SExtInst>(OpA) && !isa<ZExtInst>(OpA))
    return std::nullopt;

  bool Signed = isa<SExtInst>(OpA);

  // At this point A could be a function parameter, i.e. not an instruction
  Value *ValA = OpA->getOperand(0);
  OpB = dyn_cast<Instruction>(OpB->getOperand(0));
  if (!OpB || ValA->getType() != OpB->getType())
    return std::nullopt;

  const SCEV *OffsetSCEVA = SE.getSCEV(ValA);
  const SCEV *OffsetSCEVB = SE.getSCEV(OpB);
  const SCEV *IdxDiffSCEV = SE.getMinusSCEV(OffsetSCEVB, OffsetSCEVA);
  if (IdxDiffSCEV == SE.getCouldNotCompute())
    return std::nullopt;

  ConstantRange IdxDiffRange = SE.getSignedRange(IdxDiffSCEV);
  if (!IdxDiffRange.isSingleElement())
    return std::nullopt;
  APInt IdxDiff = *IdxDiffRange.getSingleElement();

  LLVM_DEBUG(dbgs() << "LSV: getConstantOffsetComplexAddrs IdxDiff=" << IdxDiff
                    << "\n");

  // Now we need to prove that adding IdxDiff to ValA won't overflow.
  bool Safe = false;

  // First attempt: if OpB is an add with NSW/NUW, and OpB is IdxDiff added to
  // ValA, we're okay.
  if (OpB->getOpcode() == Instruction::Add &&
      isa<ConstantInt>(OpB->getOperand(1)) &&
      IdxDiff.sle(cast<ConstantInt>(OpB->getOperand(1))->getSExtValue()) &&
      checkNoWrapFlags(OpB, Signed))
    Safe = true;

  // Second attempt: check if we have eligible add NSW/NUW instruction
  // sequences.
  OpA = dyn_cast<Instruction>(ValA);
  if (!Safe && OpA && OpA->getOpcode() == Instruction::Add &&
      OpB->getOpcode() == Instruction::Add && checkNoWrapFlags(OpA, Signed) &&
      checkNoWrapFlags(OpB, Signed)) {
    // In the checks below a matching operand in OpA and OpB is an operand which
    // is the same in those two instructions.  Below we account for possible
    // orders of the operands of these add instructions.
    for (unsigned MatchingOpIdxA : {0, 1})
      for (unsigned MatchingOpIdxB : {0, 1})
        if (!Safe)
          Safe = checkIfSafeAddSequence(IdxDiff, OpA, MatchingOpIdxA, OpB,
                                        MatchingOpIdxB, Signed);
  }

  unsigned BitWidth = ValA->getType()->getScalarSizeInBits();

  // Third attempt:
  //
  // Assuming IdxDiff is positive: If all set bits of IdxDiff or any higher
  // order bit other than the sign bit are known to be zero in ValA, we can add
  // Diff to it while guaranteeing no overflow of any sort.
  //
  // If IdxDiff is negative, do the same, but swap ValA and ValB.
  if (!Safe) {
    // When computing known bits, use the GEPs as context instructions, since
    // they likely are in the same BB as the load/store.
    KnownBits Known(BitWidth);
    computeKnownBits((IdxDiff.sge(0) ? ValA : OpB), Known, DL, 0, &AC,
                     ContextInst, &DT);
    APInt BitsAllowedToBeSet = Known.Zero.zext(IdxDiff.getBitWidth());
    if (Signed)
      BitsAllowedToBeSet.clearBit(BitWidth - 1);
    if (BitsAllowedToBeSet.ult(IdxDiff.abs()))
      return std::nullopt;
    Safe = true;
  }

  if (Safe)
    return IdxDiff * Stride;
  return std::nullopt;
}

std::optional<APInt> Vectorizer::getConstantOffsetSelects(
    Value *PtrA, Value *PtrB, Instruction *ContextInst, unsigned Depth) {
  if (Depth++ == MaxDepth)
    return std::nullopt;

  if (auto *SelectA = dyn_cast<SelectInst>(PtrA)) {
    if (auto *SelectB = dyn_cast<SelectInst>(PtrB)) {
      if (SelectA->getCondition() != SelectB->getCondition())
        return std::nullopt;
      LLVM_DEBUG(dbgs() << "LSV: getConstantOffsetSelects, PtrA=" << *PtrA
                        << ", PtrB=" << *PtrB << ", ContextInst="
                        << *ContextInst << ", Depth=" << Depth << "\n");
      std::optional<APInt> TrueDiff = getConstantOffset(
          SelectA->getTrueValue(), SelectB->getTrueValue(), ContextInst, Depth);
      if (!TrueDiff.has_value())
        return std::nullopt;
      std::optional<APInt> FalseDiff =
          getConstantOffset(SelectA->getFalseValue(), SelectB->getFalseValue(),
                            ContextInst, Depth);
      if (TrueDiff == FalseDiff)
        return TrueDiff;
    }
  }
  return std::nullopt;
}

EquivalenceClassMap
Vectorizer::collectEquivalenceClasses(BasicBlock::iterator Begin,
                                      BasicBlock::iterator End) {
  EquivalenceClassMap Ret;

  auto getUnderlyingObject = [](const Value *Ptr) -> const Value * {
    const Value *ObjPtr = llvm::getUnderlyingObject(Ptr);
    if (const auto *Sel = dyn_cast<SelectInst>(ObjPtr)) {
      // The select's themselves are distinct instructions even if they share
      // the same condition and evaluate to consecutive pointers for true and
      // false values of the condition. Therefore using the select's themselves
      // for grouping instructions would put consecutive accesses into different
      // lists and they won't be even checked for being consecutive, and won't
      // be vectorized.
      return Sel->getCondition();
    }
    return ObjPtr;
  };

  for (Instruction &I : make_range(Begin, End)) {
    auto *LI = dyn_cast<LoadInst>(&I);
    auto *SI = dyn_cast<StoreInst>(&I);
    if (!LI && !SI)
      continue;

    if ((LI && !LI->isSimple()) || (SI && !SI->isSimple()))
      continue;

    if ((LI && !TTI.isLegalToVectorizeLoad(LI)) ||
        (SI && !TTI.isLegalToVectorizeStore(SI)))
      continue;

    Type *Ty = getLoadStoreType(&I);
    if (!VectorType::isValidElementType(Ty->getScalarType()))
      continue;

    // Skip weird non-byte sizes. They probably aren't worth the effort of
    // handling correctly.
    unsigned TySize = DL.getTypeSizeInBits(Ty);
    if ((TySize % 8) != 0)
      continue;

    // Skip vectors of pointers. The vectorizeLoadChain/vectorizeStoreChain
    // functions are currently using an integer type for the vectorized
    // load/store, and does not support casting between the integer type and a
    // vector of pointers (e.g. i64 to <2 x i16*>)
    if (Ty->isVectorTy() && Ty->isPtrOrPtrVectorTy())
      continue;

    Value *Ptr = getLoadStorePointerOperand(&I);
    unsigned AS = Ptr->getType()->getPointerAddressSpace();
    unsigned VecRegSize = TTI.getLoadStoreVecRegBitWidth(AS);

    unsigned VF = VecRegSize / TySize;
    VectorType *VecTy = dyn_cast<VectorType>(Ty);

    // Only handle power-of-two sized elements.
    if ((!VecTy && !isPowerOf2_32(DL.getTypeSizeInBits(Ty))) ||
        (VecTy && !isPowerOf2_32(DL.getTypeSizeInBits(VecTy->getScalarType()))))
      continue;

    // No point in looking at these if they're too big to vectorize.
    if (TySize > VecRegSize / 2 ||
        (VecTy && TTI.getLoadVectorFactor(VF, TySize, TySize / 8, VecTy) == 0))
      continue;

    Ret[{getUnderlyingObject(Ptr), AS,
         DL.getTypeSizeInBits(getLoadStoreType(&I)->getScalarType()),
         /*IsLoad=*/LI != nullptr}]
        .push_back(&I);
  }

  return Ret;
}

std::vector<Chain> Vectorizer::gatherChains(ArrayRef<Instruction *> Instrs) {
  if (Instrs.empty())
    return {};

  unsigned AS = getLoadStoreAddressSpace(Instrs[0]);
  unsigned ASPtrBits = DL.getIndexSizeInBits(AS);

#ifndef NDEBUG
  // Check that Instrs is in BB order and all have the same addr space.
  for (size_t I = 1; I < Instrs.size(); ++I) {
    assert(Instrs[I - 1]->comesBefore(Instrs[I]));
    assert(getLoadStoreAddressSpace(Instrs[I]) == AS);
  }
#endif

  // Machinery to build an MRU-hashtable of Chains.
  //
  // (Ideally this could be done with MapVector, but as currently implemented,
  // moving an element to the front of a MapVector is O(n).)
  struct InstrListElem : ilist_node<InstrListElem>,
                         std::pair<Instruction *, Chain> {
    explicit InstrListElem(Instruction *I)
        : std::pair<Instruction *, Chain>(I, {}) {}
  };
  struct InstrListElemDenseMapInfo {
    using PtrInfo = DenseMapInfo<InstrListElem *>;
    using IInfo = DenseMapInfo<Instruction *>;
    static InstrListElem *getEmptyKey() { return PtrInfo::getEmptyKey(); }
    static InstrListElem *getTombstoneKey() {
      return PtrInfo::getTombstoneKey();
    }
    static unsigned getHashValue(const InstrListElem *E) {
      return IInfo::getHashValue(E->first);
    }
    static bool isEqual(const InstrListElem *A, const InstrListElem *B) {
      if (A == getEmptyKey() || B == getEmptyKey())
        return A == getEmptyKey() && B == getEmptyKey();
      if (A == getTombstoneKey() || B == getTombstoneKey())
        return A == getTombstoneKey() && B == getTombstoneKey();
      return IInfo::isEqual(A->first, B->first);
    }
  };
  SpecificBumpPtrAllocator<InstrListElem> Allocator;
  simple_ilist<InstrListElem> MRU;
  DenseSet<InstrListElem *, InstrListElemDenseMapInfo> Chains;

  // Compare each instruction in `instrs` to leader of the N most recently-used
  // chains.  This limits the O(n^2) behavior of this pass while also allowing
  // us to build arbitrarily long chains.
  for (Instruction *I : Instrs) {
    constexpr int MaxChainsToTry = 64;

    bool MatchFound = false;
    auto ChainIter = MRU.begin();
    for (size_t J = 0; J < MaxChainsToTry && ChainIter != MRU.end();
         ++J, ++ChainIter) {
      std::optional<APInt> Offset = getConstantOffset(
          getLoadStorePointerOperand(ChainIter->first),
          getLoadStorePointerOperand(I),
          /*ContextInst=*/
          (ChainIter->first->comesBefore(I) ? I : ChainIter->first));
      if (Offset.has_value()) {
        // `Offset` might not have the expected number of bits, if e.g. AS has a
        // different number of bits than opaque pointers.
        ChainIter->second.push_back(ChainElem{I, Offset.value()});
        // Move ChainIter to the front of the MRU list.
        MRU.remove(*ChainIter);
        MRU.push_front(*ChainIter);
        MatchFound = true;
        break;
      }
    }

    if (!MatchFound) {
      APInt ZeroOffset(ASPtrBits, 0);
      InstrListElem *E = new (Allocator.Allocate()) InstrListElem(I);
      E->second.push_back(ChainElem{I, ZeroOffset});
      MRU.push_front(*E);
      Chains.insert(E);
    }
  }

  std::vector<Chain> Ret;
  Ret.reserve(Chains.size());
  // Iterate over MRU rather than Chains so the order is deterministic.
  for (auto &E : MRU)
    if (E.second.size() > 1)
      Ret.push_back(std::move(E.second));
  return Ret;
}

std::optional<APInt> Vectorizer::getConstantOffset(Value *PtrA, Value *PtrB,
                                                   Instruction *ContextInst,
                                                   unsigned Depth) {
  LLVM_DEBUG(dbgs() << "LSV: getConstantOffset, PtrA=" << *PtrA
                    << ", PtrB=" << *PtrB << ", ContextInst= " << *ContextInst
                    << ", Depth=" << Depth << "\n");
  // We'll ultimately return a value of this bit width, even if computations
  // happen in a different width.
  unsigned OrigBitWidth = DL.getIndexTypeSizeInBits(PtrA->getType());
  APInt OffsetA(OrigBitWidth, 0);
  APInt OffsetB(OrigBitWidth, 0);
  PtrA = PtrA->stripAndAccumulateInBoundsConstantOffsets(DL, OffsetA);
  PtrB = PtrB->stripAndAccumulateInBoundsConstantOffsets(DL, OffsetB);
  unsigned NewPtrBitWidth = DL.getTypeStoreSizeInBits(PtrA->getType());
  if (NewPtrBitWidth != DL.getTypeStoreSizeInBits(PtrB->getType()))
    return std::nullopt;

  // If we have to shrink the pointer, stripAndAccumulateInBoundsConstantOffsets
  // should properly handle a possible overflow and the value should fit into
  // the smallest data type used in the cast/gep chain.
  assert(OffsetA.getSignificantBits() <= NewPtrBitWidth &&
         OffsetB.getSignificantBits() <= NewPtrBitWidth);

  OffsetA = OffsetA.sextOrTrunc(NewPtrBitWidth);
  OffsetB = OffsetB.sextOrTrunc(NewPtrBitWidth);
  if (PtrA == PtrB)
    return (OffsetB - OffsetA).sextOrTrunc(OrigBitWidth);

  // Try to compute B - A.
  const SCEV *DistScev = SE.getMinusSCEV(SE.getSCEV(PtrB), SE.getSCEV(PtrA));
  if (DistScev != SE.getCouldNotCompute()) {
    LLVM_DEBUG(dbgs() << "LSV: SCEV PtrB - PtrA =" << *DistScev << "\n");
    ConstantRange DistRange = SE.getSignedRange(DistScev);
    if (DistRange.isSingleElement()) {
      // Handle index width (the width of Dist) != pointer width (the width of
      // the Offset*s at this point).
      APInt Dist = DistRange.getSingleElement()->sextOrTrunc(NewPtrBitWidth);
      return (OffsetB - OffsetA + Dist).sextOrTrunc(OrigBitWidth);
    }
  }
  std::optional<APInt> Diff =
      getConstantOffsetComplexAddrs(PtrA, PtrB, ContextInst, Depth);
  if (Diff.has_value())
    return (OffsetB - OffsetA + Diff->sext(OffsetB.getBitWidth()))
        .sextOrTrunc(OrigBitWidth);
  return std::nullopt;
}
