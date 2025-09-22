//===- ARMParallelDSP.cpp - Parallel DSP Pass -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Armv6 introduced instructions to perform 32-bit SIMD operations. The
/// purpose of this pass is do some IR pattern matching to create ACLE
/// DSP intrinsics, which map on these 32-bit SIMD operations.
/// This pass runs only when unaligned accesses is supported/enabled.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMSubtarget.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "arm-parallel-dsp"

STATISTIC(NumSMLAD , "Number of smlad instructions generated");

static cl::opt<bool>
DisableParallelDSP("disable-arm-parallel-dsp", cl::Hidden, cl::init(false),
                   cl::desc("Disable the ARM Parallel DSP pass"));

static cl::opt<unsigned>
NumLoadLimit("arm-parallel-dsp-load-limit", cl::Hidden, cl::init(16),
             cl::desc("Limit the number of loads analysed"));

namespace {
  struct MulCandidate;
  class Reduction;

  using MulCandList = SmallVector<std::unique_ptr<MulCandidate>, 8>;
  using MemInstList = SmallVectorImpl<LoadInst*>;
  using MulPairList = SmallVector<std::pair<MulCandidate*, MulCandidate*>, 8>;

  // 'MulCandidate' holds the multiplication instructions that are candidates
  // for parallel execution.
  struct MulCandidate {
    Instruction   *Root;
    Value*        LHS;
    Value*        RHS;
    bool          Exchange = false;
    bool          Paired = false;
    SmallVector<LoadInst*, 2> VecLd;    // Container for loads to widen.

    MulCandidate(Instruction *I, Value *lhs, Value *rhs) :
      Root(I), LHS(lhs), RHS(rhs) { }

    bool HasTwoLoadInputs() const {
      return isa<LoadInst>(LHS) && isa<LoadInst>(RHS);
    }

    LoadInst *getBaseLoad() const {
      return VecLd.front();
    }
  };

  /// Represent a sequence of multiply-accumulate operations with the aim to
  /// perform the multiplications in parallel.
  class Reduction {
    Instruction     *Root = nullptr;
    Value           *Acc = nullptr;
    MulCandList     Muls;
    MulPairList        MulPairs;
    SetVector<Instruction*> Adds;

  public:
    Reduction() = delete;

    Reduction (Instruction *Add) : Root(Add) { }

    /// Record an Add instruction that is a part of the this reduction.
    void InsertAdd(Instruction *I) { Adds.insert(I); }

    /// Create MulCandidates, each rooted at a Mul instruction, that is a part
    /// of this reduction.
    void InsertMuls() {
      auto GetMulOperand = [](Value *V) -> Instruction* {
        if (auto *SExt = dyn_cast<SExtInst>(V)) {
          if (auto *I = dyn_cast<Instruction>(SExt->getOperand(0)))
            if (I->getOpcode() == Instruction::Mul)
              return I;
        } else if (auto *I = dyn_cast<Instruction>(V)) {
          if (I->getOpcode() == Instruction::Mul)
            return I;
        }
        return nullptr;
      };

      auto InsertMul = [this](Instruction *I) {
        Value *LHS = cast<Instruction>(I->getOperand(0))->getOperand(0);
        Value *RHS = cast<Instruction>(I->getOperand(1))->getOperand(0);
        Muls.push_back(std::make_unique<MulCandidate>(I, LHS, RHS));
      };

      for (auto *Add : Adds) {
        if (Add == Acc)
          continue;
        if (auto *Mul = GetMulOperand(Add->getOperand(0)))
          InsertMul(Mul);
        if (auto *Mul = GetMulOperand(Add->getOperand(1)))
          InsertMul(Mul);
      }
    }

    /// Add the incoming accumulator value, returns true if a value had not
    /// already been added. Returning false signals to the user that this
    /// reduction already has a value to initialise the accumulator.
    bool InsertAcc(Value *V) {
      if (Acc)
        return false;
      Acc = V;
      return true;
    }

    /// Set two MulCandidates, rooted at muls, that can be executed as a single
    /// parallel operation.
    void AddMulPair(MulCandidate *Mul0, MulCandidate *Mul1,
                    bool Exchange = false) {
      LLVM_DEBUG(dbgs() << "Pairing:\n"
                 << *Mul0->Root << "\n"
                 << *Mul1->Root << "\n");
      Mul0->Paired = true;
      Mul1->Paired = true;
      if (Exchange)
        Mul1->Exchange = true;
      MulPairs.push_back(std::make_pair(Mul0, Mul1));
    }

    /// Return the add instruction which is the root of the reduction.
    Instruction *getRoot() { return Root; }

    bool is64Bit() const { return Root->getType()->isIntegerTy(64); }

    Type *getType() const { return Root->getType(); }

    /// Return the incoming value to be accumulated. This maybe null.
    Value *getAccumulator() { return Acc; }

    /// Return the set of adds that comprise the reduction.
    SetVector<Instruction*> &getAdds() { return Adds; }

    /// Return the MulCandidate, rooted at mul instruction, that comprise the
    /// the reduction.
    MulCandList &getMuls() { return Muls; }

    /// Return the MulCandidate, rooted at mul instructions, that have been
    /// paired for parallel execution.
    MulPairList &getMulPairs() { return MulPairs; }

    /// To finalise, replace the uses of the root with the intrinsic call.
    void UpdateRoot(Instruction *SMLAD) {
      Root->replaceAllUsesWith(SMLAD);
    }

    void dump() {
      LLVM_DEBUG(dbgs() << "Reduction:\n";
        for (auto *Add : Adds)
          LLVM_DEBUG(dbgs() << *Add << "\n");
        for (auto &Mul : Muls)
          LLVM_DEBUG(dbgs() << *Mul->Root << "\n"
                     << "  " << *Mul->LHS << "\n"
                     << "  " << *Mul->RHS << "\n");
        LLVM_DEBUG(if (Acc) dbgs() << "Acc in: " << *Acc << "\n")
      );
    }
  };

  class WidenedLoad {
    LoadInst *NewLd = nullptr;
    SmallVector<LoadInst*, 4> Loads;

  public:
    WidenedLoad(SmallVectorImpl<LoadInst*> &Lds, LoadInst *Wide)
      : NewLd(Wide) {
      append_range(Loads, Lds);
    }
    LoadInst *getLoad() {
      return NewLd;
    }
  };

  class ARMParallelDSP : public FunctionPass {
    ScalarEvolution   *SE;
    AliasAnalysis     *AA;
    TargetLibraryInfo *TLI;
    DominatorTree     *DT;
    const DataLayout  *DL;
    Module            *M;
    std::map<LoadInst*, LoadInst*> LoadPairs;
    SmallPtrSet<LoadInst*, 4> OffsetLoads;
    std::map<LoadInst*, std::unique_ptr<WidenedLoad>> WideLoads;

    template<unsigned>
    bool IsNarrowSequence(Value *V);
    bool Search(Value *V, BasicBlock *BB, Reduction &R);
    bool RecordMemoryOps(BasicBlock *BB);
    void InsertParallelMACs(Reduction &Reduction);
    bool AreSequentialLoads(LoadInst *Ld0, LoadInst *Ld1, MemInstList &VecMem);
    LoadInst* CreateWideLoad(MemInstList &Loads, IntegerType *LoadTy);
    bool CreateParallelPairs(Reduction &R);

    /// Try to match and generate: SMLAD, SMLADX - Signed Multiply Accumulate
    /// Dual performs two signed 16x16-bit multiplications. It adds the
    /// products to a 32-bit accumulate operand. Optionally, the instruction can
    /// exchange the halfwords of the second operand before performing the
    /// arithmetic.
    bool MatchSMLAD(Function &F);

  public:
    static char ID;

    ARMParallelDSP() : FunctionPass(ID) { }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      FunctionPass::getAnalysisUsage(AU);
      AU.addRequired<AssumptionCacheTracker>();
      AU.addRequired<ScalarEvolutionWrapperPass>();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<TargetLibraryInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.addRequired<TargetPassConfig>();
      AU.addPreserved<ScalarEvolutionWrapperPass>();
      AU.addPreserved<GlobalsAAWrapperPass>();
      AU.setPreservesCFG();
    }

    bool runOnFunction(Function &F) override {
      if (DisableParallelDSP)
        return false;
      if (skipFunction(F))
        return false;

      SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
      AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
      TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
      DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
      auto &TPC = getAnalysis<TargetPassConfig>();

      M = F.getParent();
      DL = &M->getDataLayout();

      auto &TM = TPC.getTM<TargetMachine>();
      auto *ST = &TM.getSubtarget<ARMSubtarget>(F);

      if (!ST->allowsUnalignedMem()) {
        LLVM_DEBUG(dbgs() << "Unaligned memory access not supported: not "
                             "running pass ARMParallelDSP\n");
        return false;
      }

      if (!ST->hasDSP()) {
        LLVM_DEBUG(dbgs() << "DSP extension not enabled: not running pass "
                             "ARMParallelDSP\n");
        return false;
      }

      if (!ST->isLittle()) {
        LLVM_DEBUG(dbgs() << "Only supporting little endian: not running pass "
                          << "ARMParallelDSP\n");
        return false;
      }

      LLVM_DEBUG(dbgs() << "\n== Parallel DSP pass ==\n");
      LLVM_DEBUG(dbgs() << " - " << F.getName() << "\n\n");

      bool Changes = MatchSMLAD(F);
      return Changes;
    }
  };
}

bool ARMParallelDSP::AreSequentialLoads(LoadInst *Ld0, LoadInst *Ld1,
                                        MemInstList &VecMem) {
  if (!Ld0 || !Ld1)
    return false;

  if (!LoadPairs.count(Ld0) || LoadPairs[Ld0] != Ld1)
    return false;

  LLVM_DEBUG(dbgs() << "Loads are sequential and valid:\n";
    dbgs() << "Ld0:"; Ld0->dump();
    dbgs() << "Ld1:"; Ld1->dump();
  );

  VecMem.clear();
  VecMem.push_back(Ld0);
  VecMem.push_back(Ld1);
  return true;
}

// MaxBitwidth: the maximum supported bitwidth of the elements in the DSP
// instructions, which is set to 16. So here we should collect all i8 and i16
// narrow operations.
// TODO: we currently only collect i16, and will support i8 later, so that's
// why we check that types are equal to MaxBitWidth, and not <= MaxBitWidth.
template<unsigned MaxBitWidth>
bool ARMParallelDSP::IsNarrowSequence(Value *V) {
  if (auto *SExt = dyn_cast<SExtInst>(V)) {
    if (SExt->getSrcTy()->getIntegerBitWidth() != MaxBitWidth)
      return false;

    if (auto *Ld = dyn_cast<LoadInst>(SExt->getOperand(0))) {
      // Check that this load could be paired.
      return LoadPairs.count(Ld) || OffsetLoads.count(Ld);
    }
  }
  return false;
}

/// Iterate through the block and record base, offset pairs of loads which can
/// be widened into a single load.
bool ARMParallelDSP::RecordMemoryOps(BasicBlock *BB) {
  SmallVector<LoadInst*, 8> Loads;
  SmallVector<Instruction*, 8> Writes;
  LoadPairs.clear();
  WideLoads.clear();

  // Collect loads and instruction that may write to memory. For now we only
  // record loads which are simple, sign-extended and have a single user.
  // TODO: Allow zero-extended loads.
  for (auto &I : *BB) {
    if (I.mayWriteToMemory())
      Writes.push_back(&I);
    auto *Ld = dyn_cast<LoadInst>(&I);
    if (!Ld || !Ld->isSimple() ||
        !Ld->hasOneUse() || !isa<SExtInst>(Ld->user_back()))
      continue;
    Loads.push_back(Ld);
  }

  if (Loads.empty() || Loads.size() > NumLoadLimit)
    return false;

  using InstSet = std::set<Instruction*>;
  using DepMap = std::map<Instruction*, InstSet>;
  DepMap RAWDeps;

  // Record any writes that may alias a load.
  const auto Size = LocationSize::beforeOrAfterPointer();
  for (auto *Write : Writes) {
    for (auto *Read : Loads) {
      MemoryLocation ReadLoc =
        MemoryLocation(Read->getPointerOperand(), Size);

      if (!isModOrRefSet(AA->getModRefInfo(Write, ReadLoc)))
        continue;
      if (Write->comesBefore(Read))
        RAWDeps[Read].insert(Write);
    }
  }

  // Check whether there's not a write between the two loads which would
  // prevent them from being safely merged.
  auto SafeToPair = [&](LoadInst *Base, LoadInst *Offset) {
    bool BaseFirst = Base->comesBefore(Offset);
    LoadInst *Dominator = BaseFirst ? Base : Offset;
    LoadInst *Dominated = BaseFirst ? Offset : Base;

    if (RAWDeps.count(Dominated)) {
      InstSet &WritesBefore = RAWDeps[Dominated];

      for (auto *Before : WritesBefore) {
        // We can't move the second load backward, past a write, to merge
        // with the first load.
        if (Dominator->comesBefore(Before))
          return false;
      }
    }
    return true;
  };

  // Record base, offset load pairs.
  for (auto *Base : Loads) {
    for (auto *Offset : Loads) {
      if (Base == Offset || OffsetLoads.count(Offset))
        continue;

      if (isConsecutiveAccess(Base, Offset, *DL, *SE) &&
          SafeToPair(Base, Offset)) {
        LoadPairs[Base] = Offset;
        OffsetLoads.insert(Offset);
        break;
      }
    }
  }

  LLVM_DEBUG(if (!LoadPairs.empty()) {
               dbgs() << "Consecutive load pairs:\n";
               for (auto &MapIt : LoadPairs) {
                 LLVM_DEBUG(dbgs() << *MapIt.first << ", "
                            << *MapIt.second << "\n");
               }
             });
  return LoadPairs.size() > 1;
}

// Search recursively back through the operands to find a tree of values that
// form a multiply-accumulate chain. The search records the Add and Mul
// instructions that form the reduction and allows us to find a single value
// to be used as the initial input to the accumlator.
bool ARMParallelDSP::Search(Value *V, BasicBlock *BB, Reduction &R) {
  // If we find a non-instruction, try to use it as the initial accumulator
  // value. This may have already been found during the search in which case
  // this function will return false, signaling a search fail.
  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return R.InsertAcc(V);

  if (I->getParent() != BB)
    return false;

  switch (I->getOpcode()) {
  default:
    break;
  case Instruction::PHI:
    // Could be the accumulator value.
    return R.InsertAcc(V);
  case Instruction::Add: {
    // Adds should be adding together two muls, or another add and a mul to
    // be within the mac chain. One of the operands may also be the
    // accumulator value at which point we should stop searching.
    R.InsertAdd(I);
    Value *LHS = I->getOperand(0);
    Value *RHS = I->getOperand(1);
    bool ValidLHS = Search(LHS, BB, R);
    bool ValidRHS = Search(RHS, BB, R);

    if (ValidLHS && ValidRHS)
      return true;

    // Ensure we don't add the root as the incoming accumulator.
    if (R.getRoot() == I)
      return false;

    return R.InsertAcc(I);
  }
  case Instruction::Mul: {
    Value *MulOp0 = I->getOperand(0);
    Value *MulOp1 = I->getOperand(1);
    return IsNarrowSequence<16>(MulOp0) && IsNarrowSequence<16>(MulOp1);
  }
  case Instruction::SExt:
    return Search(I->getOperand(0), BB, R);
  }
  return false;
}

// The pass needs to identify integer add/sub reductions of 16-bit vector
// multiplications.
// To use SMLAD:
// 1) we first need to find integer add then look for this pattern:
//
// acc0 = ...
// ld0 = load i16
// sext0 = sext i16 %ld0 to i32
// ld1 = load i16
// sext1 = sext i16 %ld1 to i32
// mul0 = mul %sext0, %sext1
// ld2 = load i16
// sext2 = sext i16 %ld2 to i32
// ld3 = load i16
// sext3 = sext i16 %ld3 to i32
// mul1 = mul i32 %sext2, %sext3
// add0 = add i32 %mul0, %acc0
// acc1 = add i32 %add0, %mul1
//
// Which can be selected to:
//
// ldr r0
// ldr r1
// smlad r2, r0, r1, r2
//
// If constants are used instead of loads, these will need to be hoisted
// out and into a register.
//
// If loop invariants are used instead of loads, these need to be packed
// before the loop begins.
//
bool ARMParallelDSP::MatchSMLAD(Function &F) {
  bool Changed = false;

  for (auto &BB : F) {
    SmallPtrSet<Instruction*, 4> AllAdds;
    if (!RecordMemoryOps(&BB))
      continue;

    for (Instruction &I : reverse(BB)) {
      if (I.getOpcode() != Instruction::Add)
        continue;

      if (AllAdds.count(&I))
        continue;

      const auto *Ty = I.getType();
      if (!Ty->isIntegerTy(32) && !Ty->isIntegerTy(64))
        continue;

      Reduction R(&I);
      if (!Search(&I, &BB, R))
        continue;

      R.InsertMuls();
      LLVM_DEBUG(dbgs() << "After search, Reduction:\n"; R.dump());

      if (!CreateParallelPairs(R))
        continue;

      InsertParallelMACs(R);
      Changed = true;
      AllAdds.insert(R.getAdds().begin(), R.getAdds().end());
      LLVM_DEBUG(dbgs() << "BB after inserting parallel MACs:\n" << BB);
    }
  }

  return Changed;
}

bool ARMParallelDSP::CreateParallelPairs(Reduction &R) {

  // Not enough mul operations to make a pair.
  if (R.getMuls().size() < 2)
    return false;

  // Check that the muls operate directly upon sign extended loads.
  for (auto &MulCand : R.getMuls()) {
    if (!MulCand->HasTwoLoadInputs())
      return false;
  }

  auto CanPair = [&](Reduction &R, MulCandidate *PMul0, MulCandidate *PMul1) {
    // The first elements of each vector should be loads with sexts. If we
    // find that its two pairs of consecutive loads, then these can be
    // transformed into two wider loads and the users can be replaced with
    // DSP intrinsics.
    auto Ld0 = static_cast<LoadInst*>(PMul0->LHS);
    auto Ld1 = static_cast<LoadInst*>(PMul1->LHS);
    auto Ld2 = static_cast<LoadInst*>(PMul0->RHS);
    auto Ld3 = static_cast<LoadInst*>(PMul1->RHS);

    // Check that each mul is operating on two different loads.
    if (Ld0 == Ld2 || Ld1 == Ld3)
      return false;

    if (AreSequentialLoads(Ld0, Ld1, PMul0->VecLd)) {
      if (AreSequentialLoads(Ld2, Ld3, PMul1->VecLd)) {
        LLVM_DEBUG(dbgs() << "OK: found two pairs of parallel loads!\n");
        R.AddMulPair(PMul0, PMul1);
        return true;
      } else if (AreSequentialLoads(Ld3, Ld2, PMul1->VecLd)) {
        LLVM_DEBUG(dbgs() << "OK: found two pairs of parallel loads!\n");
        LLVM_DEBUG(dbgs() << "    exchanging Ld2 and Ld3\n");
        R.AddMulPair(PMul0, PMul1, true);
        return true;
      }
    } else if (AreSequentialLoads(Ld1, Ld0, PMul0->VecLd) &&
               AreSequentialLoads(Ld2, Ld3, PMul1->VecLd)) {
      LLVM_DEBUG(dbgs() << "OK: found two pairs of parallel loads!\n");
      LLVM_DEBUG(dbgs() << "    exchanging Ld0 and Ld1\n");
      LLVM_DEBUG(dbgs() << "    and swapping muls\n");
      // Only the second operand can be exchanged, so swap the muls.
      R.AddMulPair(PMul1, PMul0, true);
      return true;
    }
    return false;
  };

  MulCandList &Muls = R.getMuls();
  const unsigned Elems = Muls.size();
  for (unsigned i = 0; i < Elems; ++i) {
    MulCandidate *PMul0 = static_cast<MulCandidate*>(Muls[i].get());
    if (PMul0->Paired)
      continue;

    for (unsigned j = 0; j < Elems; ++j) {
      if (i == j)
        continue;

      MulCandidate *PMul1 = static_cast<MulCandidate*>(Muls[j].get());
      if (PMul1->Paired)
        continue;

      const Instruction *Mul0 = PMul0->Root;
      const Instruction *Mul1 = PMul1->Root;
      if (Mul0 == Mul1)
        continue;

      assert(PMul0 != PMul1 && "expected different chains");

      if (CanPair(R, PMul0, PMul1))
        break;
    }
  }
  return !R.getMulPairs().empty();
}

void ARMParallelDSP::InsertParallelMACs(Reduction &R) {

  auto CreateSMLAD = [&](LoadInst* WideLd0, LoadInst *WideLd1,
                         Value *Acc, bool Exchange,
                         Instruction *InsertAfter) {
    // Replace the reduction chain with an intrinsic call

    Value* Args[] = { WideLd0, WideLd1, Acc };
    Function *SMLAD = nullptr;
    if (Exchange)
      SMLAD = Acc->getType()->isIntegerTy(32) ?
        Intrinsic::getDeclaration(M, Intrinsic::arm_smladx) :
        Intrinsic::getDeclaration(M, Intrinsic::arm_smlaldx);
    else
      SMLAD = Acc->getType()->isIntegerTy(32) ?
        Intrinsic::getDeclaration(M, Intrinsic::arm_smlad) :
        Intrinsic::getDeclaration(M, Intrinsic::arm_smlald);

    IRBuilder<NoFolder> Builder(InsertAfter->getParent(),
                                BasicBlock::iterator(InsertAfter));
    Instruction *Call = Builder.CreateCall(SMLAD, Args);
    NumSMLAD++;
    return Call;
  };

  // Return the instruction after the dominated instruction.
  auto GetInsertPoint = [this](Value *A, Value *B) {
    assert((isa<Instruction>(A) || isa<Instruction>(B)) &&
           "expected at least one instruction");

    Value *V = nullptr;
    if (!isa<Instruction>(A))
      V = B;
    else if (!isa<Instruction>(B))
      V = A;
    else
      V = DT->dominates(cast<Instruction>(A), cast<Instruction>(B)) ? B : A;

    return &*++BasicBlock::iterator(cast<Instruction>(V));
  };

  Value *Acc = R.getAccumulator();

  // For any muls that were discovered but not paired, accumulate their values
  // as before.
  IRBuilder<NoFolder> Builder(R.getRoot()->getParent());
  MulCandList &MulCands = R.getMuls();
  for (auto &MulCand : MulCands) {
    if (MulCand->Paired)
      continue;

    Instruction *Mul = cast<Instruction>(MulCand->Root);
    LLVM_DEBUG(dbgs() << "Accumulating unpaired mul: " << *Mul << "\n");

    if (R.getType() != Mul->getType()) {
      assert(R.is64Bit() && "expected 64-bit result");
      Builder.SetInsertPoint(&*++BasicBlock::iterator(Mul));
      Mul = cast<Instruction>(Builder.CreateSExt(Mul, R.getRoot()->getType()));
    }

    if (!Acc) {
      Acc = Mul;
      continue;
    }

    // If Acc is the original incoming value to the reduction, it could be a
    // phi. But the phi will dominate Mul, meaning that Mul will be the
    // insertion point.
    Builder.SetInsertPoint(GetInsertPoint(Mul, Acc));
    Acc = Builder.CreateAdd(Mul, Acc);
  }

  if (!Acc) {
    Acc = R.is64Bit() ?
      ConstantInt::get(IntegerType::get(M->getContext(), 64), 0) :
      ConstantInt::get(IntegerType::get(M->getContext(), 32), 0);
  } else if (Acc->getType() != R.getType()) {
    Builder.SetInsertPoint(R.getRoot());
    Acc = Builder.CreateSExt(Acc, R.getType());
  }

  // Roughly sort the mul pairs in their program order.
  llvm::sort(R.getMulPairs(), [](auto &PairA, auto &PairB) {
    const Instruction *A = PairA.first->Root;
    const Instruction *B = PairB.first->Root;
    return A->comesBefore(B);
  });

  IntegerType *Ty = IntegerType::get(M->getContext(), 32);
  for (auto &Pair : R.getMulPairs()) {
    MulCandidate *LHSMul = Pair.first;
    MulCandidate *RHSMul = Pair.second;
    LoadInst *BaseLHS = LHSMul->getBaseLoad();
    LoadInst *BaseRHS = RHSMul->getBaseLoad();
    LoadInst *WideLHS = WideLoads.count(BaseLHS) ?
      WideLoads[BaseLHS]->getLoad() : CreateWideLoad(LHSMul->VecLd, Ty);
    LoadInst *WideRHS = WideLoads.count(BaseRHS) ?
      WideLoads[BaseRHS]->getLoad() : CreateWideLoad(RHSMul->VecLd, Ty);

    Instruction *InsertAfter = GetInsertPoint(WideLHS, WideRHS);
    InsertAfter = GetInsertPoint(InsertAfter, Acc);
    Acc = CreateSMLAD(WideLHS, WideRHS, Acc, RHSMul->Exchange, InsertAfter);
  }
  R.UpdateRoot(cast<Instruction>(Acc));
}

LoadInst* ARMParallelDSP::CreateWideLoad(MemInstList &Loads,
                                         IntegerType *LoadTy) {
  assert(Loads.size() == 2 && "currently only support widening two loads");

  LoadInst *Base = Loads[0];
  LoadInst *Offset = Loads[1];

  Instruction *BaseSExt = dyn_cast<SExtInst>(Base->user_back());
  Instruction *OffsetSExt = dyn_cast<SExtInst>(Offset->user_back());

  assert((BaseSExt && OffsetSExt)
         && "Loads should have a single, extending, user");

  std::function<void(Value*, Value*)> MoveBefore =
    [&](Value *A, Value *B) -> void {
      if (!isa<Instruction>(A) || !isa<Instruction>(B))
        return;

      auto *Source = cast<Instruction>(A);
      auto *Sink = cast<Instruction>(B);

      if (DT->dominates(Source, Sink) ||
          Source->getParent() != Sink->getParent() ||
          isa<PHINode>(Source) || isa<PHINode>(Sink))
        return;

      Source->moveBefore(Sink);
      for (auto &Op : Source->operands())
        MoveBefore(Op, Source);
    };

  // Insert the load at the point of the original dominating load.
  LoadInst *DomLoad = DT->dominates(Base, Offset) ? Base : Offset;
  IRBuilder<NoFolder> IRB(DomLoad->getParent(),
                          ++BasicBlock::iterator(DomLoad));

  // Create the wide load, while making sure to maintain the original alignment
  // as this prevents ldrd from being generated when it could be illegal due to
  // memory alignment.
  Value *VecPtr = Base->getPointerOperand();
  LoadInst *WideLoad = IRB.CreateAlignedLoad(LoadTy, VecPtr, Base->getAlign());

  // Make sure everything is in the correct order in the basic block.
  MoveBefore(Base->getPointerOperand(), VecPtr);
  MoveBefore(VecPtr, WideLoad);

  // From the wide load, create two values that equal the original two loads.
  // Loads[0] needs trunc while Loads[1] needs a lshr and trunc.
  // TODO: Support big-endian as well.
  Value *Bottom = IRB.CreateTrunc(WideLoad, Base->getType());
  Value *NewBaseSExt = IRB.CreateSExt(Bottom, BaseSExt->getType());
  BaseSExt->replaceAllUsesWith(NewBaseSExt);

  IntegerType *OffsetTy = cast<IntegerType>(Offset->getType());
  Value *ShiftVal = ConstantInt::get(LoadTy, OffsetTy->getBitWidth());
  Value *Top = IRB.CreateLShr(WideLoad, ShiftVal);
  Value *Trunc = IRB.CreateTrunc(Top, OffsetTy);
  Value *NewOffsetSExt = IRB.CreateSExt(Trunc, OffsetSExt->getType());
  OffsetSExt->replaceAllUsesWith(NewOffsetSExt);

  LLVM_DEBUG(dbgs() << "From Base and Offset:\n"
             << *Base << "\n" << *Offset << "\n"
             << "Created Wide Load:\n"
             << *WideLoad << "\n"
             << *Bottom << "\n"
             << *NewBaseSExt << "\n"
             << *Top << "\n"
             << *Trunc << "\n"
             << *NewOffsetSExt << "\n");
  WideLoads.emplace(std::make_pair(Base,
                                   std::make_unique<WidenedLoad>(Loads, WideLoad)));
  return WideLoad;
}

Pass *llvm::createARMParallelDSPPass() {
  return new ARMParallelDSP();
}

char ARMParallelDSP::ID = 0;

INITIALIZE_PASS_BEGIN(ARMParallelDSP, "arm-parallel-dsp",
                "Transform functions to use DSP intrinsics", false, false)
INITIALIZE_PASS_END(ARMParallelDSP, "arm-parallel-dsp",
                "Transform functions to use DSP intrinsics", false, false)
