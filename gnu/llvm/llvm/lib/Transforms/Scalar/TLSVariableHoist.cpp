//===- TLSVariableHoist.cpp -------- Remove Redundant TLS Loads ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass identifies/eliminate Redundant TLS Loads if related option is set.
// The example: Please refer to the comment at the head of TLSVariableHoist.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/TLSVariableHoist.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;
using namespace tlshoist;

#define DEBUG_TYPE "tlshoist"

static cl::opt<bool> TLSLoadHoist(
    "tls-load-hoist", cl::init(false), cl::Hidden,
    cl::desc("hoist the TLS loads in PIC model to eliminate redundant "
             "TLS address calculation."));

namespace {

/// The TLS Variable hoist pass.
class TLSVariableHoistLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  TLSVariableHoistLegacyPass() : FunctionPass(ID) {
    initializeTLSVariableHoistLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &Fn) override;

  StringRef getPassName() const override { return "TLS Variable Hoist"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }

private:
  TLSVariableHoistPass Impl;
};

} // end anonymous namespace

char TLSVariableHoistLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(TLSVariableHoistLegacyPass, "tlshoist",
                      "TLS Variable Hoist", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(TLSVariableHoistLegacyPass, "tlshoist",
                    "TLS Variable Hoist", false, false)

FunctionPass *llvm::createTLSVariableHoistPass() {
  return new TLSVariableHoistLegacyPass();
}

/// Perform the TLS Variable Hoist optimization for the given function.
bool TLSVariableHoistLegacyPass::runOnFunction(Function &Fn) {
  if (skipFunction(Fn))
    return false;

  LLVM_DEBUG(dbgs() << "********** Begin TLS Variable Hoist **********\n");
  LLVM_DEBUG(dbgs() << "********** Function: " << Fn.getName() << '\n');

  bool MadeChange =
      Impl.runImpl(Fn, getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
                   getAnalysis<LoopInfoWrapperPass>().getLoopInfo());

  if (MadeChange) {
    LLVM_DEBUG(dbgs() << "********** Function after TLS Variable Hoist: "
                      << Fn.getName() << '\n');
    LLVM_DEBUG(dbgs() << Fn);
  }
  LLVM_DEBUG(dbgs() << "********** End TLS Variable Hoist **********\n");

  return MadeChange;
}

void TLSVariableHoistPass::collectTLSCandidate(Instruction *Inst) {
  // Skip all cast instructions. They are visited indirectly later on.
  if (Inst->isCast())
    return;

  // Scan all operands.
  for (unsigned Idx = 0, E = Inst->getNumOperands(); Idx != E; ++Idx) {
    auto *GV = dyn_cast<GlobalVariable>(Inst->getOperand(Idx));
    if (!GV || !GV->isThreadLocal())
      continue;

    // Add Candidate to TLSCandMap (GV --> Candidate).
    TLSCandMap[GV].addUser(Inst, Idx);
  }
}

void TLSVariableHoistPass::collectTLSCandidates(Function &Fn) {
  // First, quickly check if there is TLS Variable.
  Module *M = Fn.getParent();

  bool HasTLS = llvm::any_of(
      M->globals(), [](GlobalVariable &GV) { return GV.isThreadLocal(); });

  // If non, directly return.
  if (!HasTLS)
    return;

  TLSCandMap.clear();

  // Then, collect TLS Variable info.
  for (BasicBlock &BB : Fn) {
    // Ignore unreachable basic blocks.
    if (!DT->isReachableFromEntry(&BB))
      continue;

    for (Instruction &Inst : BB)
      collectTLSCandidate(&Inst);
  }
}

static bool oneUseOutsideLoop(tlshoist::TLSCandidate &Cand, LoopInfo *LI) {
  if (Cand.Users.size() != 1)
    return false;

  BasicBlock *BB = Cand.Users[0].Inst->getParent();
  if (LI->getLoopFor(BB))
    return false;

  return true;
}

Instruction *TLSVariableHoistPass::getNearestLoopDomInst(BasicBlock *BB,
                                                         Loop *L) {
  assert(L && "Unexcepted Loop status!");

  // Get the outermost loop.
  while (Loop *Parent = L->getParentLoop())
    L = Parent;

  BasicBlock *PreHeader = L->getLoopPreheader();

  // There is unique predecessor outside the loop.
  if (PreHeader)
    return PreHeader->getTerminator();

  BasicBlock *Header = L->getHeader();
  BasicBlock *Dom = Header;
  for (BasicBlock *PredBB : predecessors(Header))
    Dom = DT->findNearestCommonDominator(Dom, PredBB);

  assert(Dom && "Not find dominator BB!");
  Instruction *Term = Dom->getTerminator();

  return Term;
}

Instruction *TLSVariableHoistPass::getDomInst(Instruction *I1,
                                              Instruction *I2) {
  if (!I1)
    return I2;
  return DT->findNearestCommonDominator(I1, I2);
}

BasicBlock::iterator TLSVariableHoistPass::findInsertPos(Function &Fn,
                                                         GlobalVariable *GV,
                                                         BasicBlock *&PosBB) {
  tlshoist::TLSCandidate &Cand = TLSCandMap[GV];

  // We should hoist the TLS use out of loop, so choose its nearest instruction
  // which dominate the loop and the outside loops (if exist).
  Instruction *LastPos = nullptr;
  for (auto &User : Cand.Users) {
    BasicBlock *BB = User.Inst->getParent();
    Instruction *Pos = User.Inst;
    if (Loop *L = LI->getLoopFor(BB)) {
      Pos = getNearestLoopDomInst(BB, L);
      assert(Pos && "Not find insert position out of loop!");
    }
    Pos = getDomInst(LastPos, Pos);
    LastPos = Pos;
  }

  assert(LastPos && "Unexpected insert position!");
  BasicBlock *Parent = LastPos->getParent();
  PosBB = Parent;
  return LastPos->getIterator();
}

// Generate a bitcast (no type change) to replace the uses of TLS Candidate.
Instruction *TLSVariableHoistPass::genBitCastInst(Function &Fn,
                                                  GlobalVariable *GV) {
  BasicBlock *PosBB = &Fn.getEntryBlock();
  BasicBlock::iterator Iter = findInsertPos(Fn, GV, PosBB);
  Type *Ty = GV->getType();
  auto *CastInst = new BitCastInst(GV, Ty, "tls_bitcast");
  CastInst->insertInto(PosBB, Iter);
  return CastInst;
}

bool TLSVariableHoistPass::tryReplaceTLSCandidate(Function &Fn,
                                                  GlobalVariable *GV) {

  tlshoist::TLSCandidate &Cand = TLSCandMap[GV];

  // If only used 1 time and not in loops, we no need to replace it.
  if (oneUseOutsideLoop(Cand, LI))
    return false;

  // Generate a bitcast (no type change)
  auto *CastInst = genBitCastInst(Fn, GV);

  // to replace the uses of TLS Candidate
  for (auto &User : Cand.Users)
    User.Inst->setOperand(User.OpndIdx, CastInst);

  return true;
}

bool TLSVariableHoistPass::tryReplaceTLSCandidates(Function &Fn) {
  if (TLSCandMap.empty())
    return false;

  bool Replaced = false;
  for (auto &GV2Cand : TLSCandMap) {
    GlobalVariable *GV = GV2Cand.first;
    Replaced |= tryReplaceTLSCandidate(Fn, GV);
  }

  return Replaced;
}

/// Optimize expensive TLS variables in the given function.
bool TLSVariableHoistPass::runImpl(Function &Fn, DominatorTree &DT,
                                   LoopInfo &LI) {
  if (Fn.hasOptNone())
    return false;

  if (!TLSLoadHoist && !Fn.getAttributes().hasFnAttr("tls-load-hoist"))
    return false;

  this->LI = &LI;
  this->DT = &DT;
  assert(this->LI && this->DT && "Unexcepted requirement!");

  // Collect all TLS variable candidates.
  collectTLSCandidates(Fn);

  bool MadeChange = tryReplaceTLSCandidates(Fn);

  return MadeChange;
}

PreservedAnalyses TLSVariableHoistPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {

  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  if (!runImpl(F, DT, LI))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
