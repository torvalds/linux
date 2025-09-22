//===- HexagonVectorLoopCarriedReuse.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass removes the computation of provably redundant expressions that have
// been computed earlier in a previous iteration. It relies on the use of PHIs
// to identify loop carried dependences. This is scalar replacement for vector
// types.
//
//===----------------------------------------------------------------------===//

#include "HexagonVectorLoopCarriedReuse.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <set>

using namespace llvm;

#define DEBUG_TYPE "hexagon-vlcr"

STATISTIC(HexagonNumVectorLoopCarriedReuse,
          "Number of values that were reused from a previous iteration.");

static cl::opt<int> HexagonVLCRIterationLim(
    "hexagon-vlcr-iteration-lim", cl::Hidden,
    cl::desc("Maximum distance of loop carried dependences that are handled"),
    cl::init(2));

namespace llvm {

void initializeHexagonVectorLoopCarriedReuseLegacyPassPass(PassRegistry &);
Pass *createHexagonVectorLoopCarriedReuseLegacyPass();

} // end namespace llvm

namespace {

  // See info about DepChain in the comments at the top of this file.
  using ChainOfDependences = SmallVector<Instruction *, 4>;

  class DepChain {
    ChainOfDependences Chain;

  public:
    bool isIdentical(DepChain &Other) const {
      if (Other.size() != size())
        return false;
      ChainOfDependences &OtherChain = Other.getChain();
      for (int i = 0; i < size(); ++i) {
        if (Chain[i] != OtherChain[i])
          return false;
      }
      return true;
    }

    ChainOfDependences &getChain() {
      return Chain;
    }

    int size() const {
      return Chain.size();
    }

    void clear() {
      Chain.clear();
    }

    void push_back(Instruction *I) {
      Chain.push_back(I);
    }

    int iterations() const {
      return size() - 1;
    }

    Instruction *front() const {
      return Chain.front();
    }

    Instruction *back() const {
      return Chain.back();
    }

    Instruction *&operator[](const int index) {
      return Chain[index];
    }

   friend raw_ostream &operator<< (raw_ostream &OS, const DepChain &D);
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<<(raw_ostream &OS, const DepChain &D) {
    const ChainOfDependences &CD = D.Chain;
    int ChainSize = CD.size();
    OS << "**DepChain Start::**\n";
    for (int i = 0; i < ChainSize -1; ++i) {
      OS << *(CD[i]) << " -->\n";
    }
    OS << *CD[ChainSize-1] << "\n";
    return OS;
  }

  struct ReuseValue {
    Instruction *Inst2Replace = nullptr;

    // In the new PHI node that we'll construct this is the value that'll be
    // used over the backedge. This is the value that gets reused from a
    // previous iteration.
    Instruction *BackedgeInst = nullptr;
    std::map<Instruction *, DepChain *> DepChains;
    int Iterations = -1;

    ReuseValue() = default;

    void reset() {
      Inst2Replace = nullptr;
      BackedgeInst = nullptr;
      DepChains.clear();
      Iterations = -1;
    }
    bool isDefined() { return Inst2Replace != nullptr; }
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<<(raw_ostream &OS, const ReuseValue &RU) {
    OS << "** ReuseValue ***\n";
    OS << "Instruction to Replace: " << *(RU.Inst2Replace) << "\n";
    OS << "Backedge Instruction: " << *(RU.BackedgeInst) << "\n";
    return OS;
  }

  class HexagonVectorLoopCarriedReuseLegacyPass : public LoopPass {
  public:
    static char ID;

    explicit HexagonVectorLoopCarriedReuseLegacyPass() : LoopPass(ID) {
      PassRegistry *PR = PassRegistry::getPassRegistry();
      initializeHexagonVectorLoopCarriedReuseLegacyPassPass(*PR);
    }

    StringRef getPassName() const override {
      return "Hexagon-specific loop carried reuse for HVX vectors";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequiredID(LoopSimplifyID);
      AU.addRequiredID(LCSSAID);
      AU.addPreservedID(LCSSAID);
      AU.setPreservesCFG();
    }

    bool runOnLoop(Loop *L, LPPassManager &LPM) override;
  };

  class HexagonVectorLoopCarriedReuse {
  public:
    HexagonVectorLoopCarriedReuse(Loop *L) : CurLoop(L){};

    bool run();

  private:
    SetVector<DepChain *> Dependences;
    std::set<Instruction *> ReplacedInsts;
    Loop *CurLoop;
    ReuseValue ReuseCandidate;

    bool doVLCR();
    void findLoopCarriedDeps();
    void findValueToReuse();
    void findDepChainFromPHI(Instruction *I, DepChain &D);
    void reuseValue();
    Value *findValueInBlock(Value *Op, BasicBlock *BB);
    DepChain *getDepChainBtwn(Instruction *I1, Instruction *I2, int Iters);
    bool isEquivalentOperation(Instruction *I1, Instruction *I2);
    bool canReplace(Instruction *I);
    bool isCallInstCommutative(CallInst *C);
  };

} // end anonymous namespace

char HexagonVectorLoopCarriedReuseLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonVectorLoopCarriedReuseLegacyPass, "hexagon-vlcr",
                      "Hexagon-specific predictive commoning for HVX vectors",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(LCSSAWrapperPass)
INITIALIZE_PASS_END(HexagonVectorLoopCarriedReuseLegacyPass, "hexagon-vlcr",
                    "Hexagon-specific predictive commoning for HVX vectors",
                    false, false)

PreservedAnalyses
HexagonVectorLoopCarriedReusePass::run(Loop &L, LoopAnalysisManager &LAM,
                                       LoopStandardAnalysisResults &AR,
                                       LPMUpdater &U) {
  HexagonVectorLoopCarriedReuse Vlcr(&L);
  if (!Vlcr.run())
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

bool HexagonVectorLoopCarriedReuseLegacyPass::runOnLoop(Loop *L,
                                                        LPPassManager &LPM) {
  if (skipLoop(L))
    return false;
  HexagonVectorLoopCarriedReuse Vlcr(L);
  return Vlcr.run();
}

bool HexagonVectorLoopCarriedReuse::run() {
  if (!CurLoop->getLoopPreheader())
    return false;

  // Work only on innermost loops.
  if (!CurLoop->getSubLoops().empty())
    return false;

  // Work only on single basic blocks loops.
  if (CurLoop->getNumBlocks() != 1)
    return false;

  return doVLCR();
}

bool HexagonVectorLoopCarriedReuse::isCallInstCommutative(CallInst *C) {
  switch (C->getCalledFunction()->getIntrinsicID()) {
    case Intrinsic::hexagon_V6_vaddb:
    case Intrinsic::hexagon_V6_vaddb_128B:
    case Intrinsic::hexagon_V6_vaddh:
    case Intrinsic::hexagon_V6_vaddh_128B:
    case Intrinsic::hexagon_V6_vaddw:
    case Intrinsic::hexagon_V6_vaddw_128B:
    case Intrinsic::hexagon_V6_vaddubh:
    case Intrinsic::hexagon_V6_vaddubh_128B:
    case Intrinsic::hexagon_V6_vadduhw:
    case Intrinsic::hexagon_V6_vadduhw_128B:
    case Intrinsic::hexagon_V6_vaddhw:
    case Intrinsic::hexagon_V6_vaddhw_128B:
    case Intrinsic::hexagon_V6_vmaxb:
    case Intrinsic::hexagon_V6_vmaxb_128B:
    case Intrinsic::hexagon_V6_vmaxh:
    case Intrinsic::hexagon_V6_vmaxh_128B:
    case Intrinsic::hexagon_V6_vmaxw:
    case Intrinsic::hexagon_V6_vmaxw_128B:
    case Intrinsic::hexagon_V6_vmaxub:
    case Intrinsic::hexagon_V6_vmaxub_128B:
    case Intrinsic::hexagon_V6_vmaxuh:
    case Intrinsic::hexagon_V6_vmaxuh_128B:
    case Intrinsic::hexagon_V6_vminub:
    case Intrinsic::hexagon_V6_vminub_128B:
    case Intrinsic::hexagon_V6_vminuh:
    case Intrinsic::hexagon_V6_vminuh_128B:
    case Intrinsic::hexagon_V6_vminb:
    case Intrinsic::hexagon_V6_vminb_128B:
    case Intrinsic::hexagon_V6_vminh:
    case Intrinsic::hexagon_V6_vminh_128B:
    case Intrinsic::hexagon_V6_vminw:
    case Intrinsic::hexagon_V6_vminw_128B:
    case Intrinsic::hexagon_V6_vmpyub:
    case Intrinsic::hexagon_V6_vmpyub_128B:
    case Intrinsic::hexagon_V6_vmpyuh:
    case Intrinsic::hexagon_V6_vmpyuh_128B:
    case Intrinsic::hexagon_V6_vavgub:
    case Intrinsic::hexagon_V6_vavgub_128B:
    case Intrinsic::hexagon_V6_vavgh:
    case Intrinsic::hexagon_V6_vavgh_128B:
    case Intrinsic::hexagon_V6_vavguh:
    case Intrinsic::hexagon_V6_vavguh_128B:
    case Intrinsic::hexagon_V6_vavgw:
    case Intrinsic::hexagon_V6_vavgw_128B:
    case Intrinsic::hexagon_V6_vavgb:
    case Intrinsic::hexagon_V6_vavgb_128B:
    case Intrinsic::hexagon_V6_vavguw:
    case Intrinsic::hexagon_V6_vavguw_128B:
    case Intrinsic::hexagon_V6_vabsdiffh:
    case Intrinsic::hexagon_V6_vabsdiffh_128B:
    case Intrinsic::hexagon_V6_vabsdiffub:
    case Intrinsic::hexagon_V6_vabsdiffub_128B:
    case Intrinsic::hexagon_V6_vabsdiffuh:
    case Intrinsic::hexagon_V6_vabsdiffuh_128B:
    case Intrinsic::hexagon_V6_vabsdiffw:
    case Intrinsic::hexagon_V6_vabsdiffw_128B:
      return true;
    default:
      return false;
  }
}

bool HexagonVectorLoopCarriedReuse::isEquivalentOperation(Instruction *I1,
                                                          Instruction *I2) {
  if (!I1->isSameOperationAs(I2))
    return false;
  // This check is in place specifically for intrinsics. isSameOperationAs will
  // return two for any two hexagon intrinsics because they are essentially the
  // same instruciton (CallInst). We need to scratch the surface to see if they
  // are calls to the same function.
  if (CallInst *C1 = dyn_cast<CallInst>(I1)) {
    if (CallInst *C2 = dyn_cast<CallInst>(I2)) {
      if (C1->getCalledFunction() != C2->getCalledFunction())
        return false;
    }
  }

  // If both the Instructions are of Vector Type and any of the element
  // is integer constant, check their values too for equivalence.
  if (I1->getType()->isVectorTy() && I2->getType()->isVectorTy()) {
    unsigned NumOperands = I1->getNumOperands();
    for (unsigned i = 0; i < NumOperands; ++i) {
      ConstantInt *C1 = dyn_cast<ConstantInt>(I1->getOperand(i));
      ConstantInt *C2 = dyn_cast<ConstantInt>(I2->getOperand(i));
      if(!C1) continue;
      assert(C2);
      if (C1->getSExtValue() != C2->getSExtValue())
        return false;
    }
  }

  return true;
}

bool HexagonVectorLoopCarriedReuse::canReplace(Instruction *I) {
  const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I);
  if (!II)
    return true;

  switch (II->getIntrinsicID()) {
  case Intrinsic::hexagon_V6_hi:
  case Intrinsic::hexagon_V6_lo:
  case Intrinsic::hexagon_V6_hi_128B:
  case Intrinsic::hexagon_V6_lo_128B:
    LLVM_DEBUG(dbgs() << "Not considering for reuse: " << *II << "\n");
    return false;
  default:
    return true;
  }
}
void HexagonVectorLoopCarriedReuse::findValueToReuse() {
  for (auto *D : Dependences) {
    LLVM_DEBUG(dbgs() << "Processing dependence " << *(D->front()) << "\n");
    if (D->iterations() > HexagonVLCRIterationLim) {
      LLVM_DEBUG(
          dbgs()
          << ".. Skipping because number of iterations > than the limit\n");
      continue;
    }

    PHINode *PN = cast<PHINode>(D->front());
    Instruction *BEInst = D->back();
    int Iters = D->iterations();
    BasicBlock *BB = PN->getParent();
    LLVM_DEBUG(dbgs() << "Checking if any uses of " << *PN
                      << " can be reused\n");

    SmallVector<Instruction *, 4> PNUsers;
    for (Use &U : PN->uses()) {
      Instruction *User = cast<Instruction>(U.getUser());

      if (User->getParent() != BB)
        continue;
      if (ReplacedInsts.count(User)) {
        LLVM_DEBUG(dbgs() << *User
                          << " has already been replaced. Skipping...\n");
        continue;
      }
      if (isa<PHINode>(User))
        continue;
      if (User->mayHaveSideEffects())
        continue;
      if (!canReplace(User))
        continue;

      PNUsers.push_back(User);
    }
    LLVM_DEBUG(dbgs() << PNUsers.size() << " use(s) of the PHI in the block\n");

    // For each interesting use I of PN, find an Instruction BEUser that
    // performs the same operation as I on BEInst and whose other operands,
    // if any, can also be rematerialized in OtherBB. We stop when we find the
    // first such Instruction BEUser. This is because once BEUser is
    // rematerialized in OtherBB, we may find more such "fixup" opportunities
    // in this block. So, we'll start over again.
    for (Instruction *I : PNUsers) {
      for (Use &U : BEInst->uses()) {
        Instruction *BEUser = cast<Instruction>(U.getUser());

        if (BEUser->getParent() != BB)
          continue;
        if (!isEquivalentOperation(I, BEUser))
          continue;

        int NumOperands = I->getNumOperands();

        // Take operands of each PNUser one by one and try to find DepChain
        // with every operand of the BEUser. If any of the operands of BEUser
        // has DepChain with current operand of the PNUser, break the matcher
        // loop. Keep doing this for Every PNUser operand. If PNUser operand
        // does not have DepChain with any of the BEUser operand, break the
        // outer matcher loop, mark the BEUser as null and reset the ReuseCandidate.
        // This ensures that DepChain exist for all the PNUser operand with
        // BEUser operand. This also ensures that DepChains are independent of
        // the positions in PNUser and BEUser.
        std::map<Instruction *, DepChain *> DepChains;
        CallInst *C1 = dyn_cast<CallInst>(I);
        if ((I && I->isCommutative()) || (C1 && isCallInstCommutative(C1))) {
          bool Found = false;
          for (int OpNo = 0; OpNo < NumOperands; ++OpNo) {
            Value *Op = I->getOperand(OpNo);
            Instruction *OpInst = dyn_cast<Instruction>(Op);
            Found = false;
            for (int T = 0; T < NumOperands; ++T) {
              Value *BEOp = BEUser->getOperand(T);
              Instruction *BEOpInst = dyn_cast<Instruction>(BEOp);
              if (!OpInst && !BEOpInst) {
                if (Op == BEOp) {
                  Found = true;
                  break;
                }
              }

              if ((OpInst && !BEOpInst) || (!OpInst && BEOpInst))
                continue;

              DepChain *D = getDepChainBtwn(OpInst, BEOpInst, Iters);

              if (D) {
                Found = true;
                DepChains[OpInst] = D;
                break;
              }
            }
            if (!Found) {
              BEUser = nullptr;
              break;
            }
          }
        } else {

          for (int OpNo = 0; OpNo < NumOperands; ++OpNo) {
            Value *Op = I->getOperand(OpNo);
            Value *BEOp = BEUser->getOperand(OpNo);

            Instruction *OpInst = dyn_cast<Instruction>(Op);
            if (!OpInst) {
              if (Op == BEOp)
                continue;
              // Do not allow reuse to occur when the operands may be different
              // values.
              BEUser = nullptr;
              break;
            }

            Instruction *BEOpInst = dyn_cast<Instruction>(BEOp);
            DepChain *D = getDepChainBtwn(OpInst, BEOpInst, Iters);

            if (D) {
              DepChains[OpInst] = D;
            } else {
              BEUser = nullptr;
              break;
            }
          }
        }
        if (BEUser) {
          LLVM_DEBUG(dbgs() << "Found Value for reuse.\n");
          ReuseCandidate.Inst2Replace = I;
          ReuseCandidate.BackedgeInst = BEUser;
          ReuseCandidate.DepChains = DepChains;
          ReuseCandidate.Iterations = Iters;
          return;
        }
        ReuseCandidate.reset();
      }
    }
  }
  ReuseCandidate.reset();
}

Value *HexagonVectorLoopCarriedReuse::findValueInBlock(Value *Op,
                                                       BasicBlock *BB) {
  PHINode *PN = dyn_cast<PHINode>(Op);
  assert(PN);
  Value *ValueInBlock = PN->getIncomingValueForBlock(BB);
  return ValueInBlock;
}

void HexagonVectorLoopCarriedReuse::reuseValue() {
  LLVM_DEBUG(dbgs() << ReuseCandidate);
  Instruction *Inst2Replace = ReuseCandidate.Inst2Replace;
  Instruction *BEInst = ReuseCandidate.BackedgeInst;
  int NumOperands = Inst2Replace->getNumOperands();
  std::map<Instruction *, DepChain *> &DepChains = ReuseCandidate.DepChains;
  int Iterations = ReuseCandidate.Iterations;
  BasicBlock *LoopPH = CurLoop->getLoopPreheader();
  assert(!DepChains.empty() && "No DepChains");
  LLVM_DEBUG(dbgs() << "reuseValue is making the following changes\n");

  SmallVector<Instruction *, 4> InstsInPreheader;
  for (int i = 0; i < Iterations; ++i) {
    Instruction *InstInPreheader = Inst2Replace->clone();
    SmallVector<Value *, 4> Ops;
    for (int j = 0; j < NumOperands; ++j) {
      Instruction *I = dyn_cast<Instruction>(Inst2Replace->getOperand(j));
      if (!I)
        continue;
      // Get the DepChain corresponding to this operand.
      DepChain &D = *DepChains[I];
      // Get the PHI for the iteration number and find
      // the incoming value from the Loop Preheader for
      // that PHI.
      Value *ValInPreheader = findValueInBlock(D[i], LoopPH);
      InstInPreheader->setOperand(j, ValInPreheader);
    }
    InstsInPreheader.push_back(InstInPreheader);
    InstInPreheader->setName(Inst2Replace->getName() + ".hexagon.vlcr");
    InstInPreheader->insertBefore(LoopPH->getTerminator());
    LLVM_DEBUG(dbgs() << "Added " << *InstInPreheader << " to "
                      << LoopPH->getName() << "\n");
  }
  BasicBlock *BB = BEInst->getParent();
  IRBuilder<> IRB(BB);
  IRB.SetInsertPoint(BB, BB->getFirstNonPHIIt());
  Value *BEVal = BEInst;
  PHINode *NewPhi;
  for (int i = Iterations-1; i >=0 ; --i) {
    Instruction *InstInPreheader = InstsInPreheader[i];
    NewPhi = IRB.CreatePHI(InstInPreheader->getType(), 2);
    NewPhi->addIncoming(InstInPreheader, LoopPH);
    NewPhi->addIncoming(BEVal, BB);
    LLVM_DEBUG(dbgs() << "Adding " << *NewPhi << " to " << BB->getName()
                      << "\n");
    BEVal = NewPhi;
  }
  // We are in LCSSA form. So, a value defined inside the Loop is used only
  // inside the loop. So, the following is safe.
  Inst2Replace->replaceAllUsesWith(NewPhi);
  ReplacedInsts.insert(Inst2Replace);
  ++HexagonNumVectorLoopCarriedReuse;
}

bool HexagonVectorLoopCarriedReuse::doVLCR() {
  assert(CurLoop->getSubLoops().empty() &&
         "Can do VLCR on the innermost loop only");
  assert((CurLoop->getNumBlocks() == 1) &&
         "Can do VLCR only on single block loops");

  bool Changed = false;
  bool Continue;

  LLVM_DEBUG(dbgs() << "Working on Loop: " << *CurLoop->getHeader() << "\n");
  do {
    // Reset datastructures.
    Dependences.clear();
    Continue = false;

    findLoopCarriedDeps();
    findValueToReuse();
    if (ReuseCandidate.isDefined()) {
      reuseValue();
      Changed = true;
      Continue = true;
    }
    llvm::for_each(Dependences, std::default_delete<DepChain>());
  } while (Continue);
  return Changed;
}

void HexagonVectorLoopCarriedReuse::findDepChainFromPHI(Instruction *I,
                                                        DepChain &D) {
  PHINode *PN = dyn_cast<PHINode>(I);
  if (!PN) {
    D.push_back(I);
    return;
  } else {
    auto NumIncomingValues = PN->getNumIncomingValues();
    if (NumIncomingValues != 2) {
      D.clear();
      return;
    }

    BasicBlock *BB = PN->getParent();
    if (BB != CurLoop->getHeader()) {
      D.clear();
      return;
    }

    Value *BEVal = PN->getIncomingValueForBlock(BB);
    Instruction *BEInst = dyn_cast<Instruction>(BEVal);
    // This is a single block loop with a preheader, so at least
    // one value should come over the backedge.
    assert(BEInst && "There should be a value over the backedge");

    Value *PreHdrVal =
      PN->getIncomingValueForBlock(CurLoop->getLoopPreheader());
    if(!PreHdrVal || !isa<Instruction>(PreHdrVal)) {
      D.clear();
      return;
    }
    D.push_back(PN);
    findDepChainFromPHI(BEInst, D);
  }
}

DepChain *HexagonVectorLoopCarriedReuse::getDepChainBtwn(Instruction *I1,
                                                         Instruction *I2,
                                                         int Iters) {
  for (auto *D : Dependences) {
    if (D->front() == I1 && D->back() == I2 && D->iterations() == Iters)
      return D;
  }
  return nullptr;
}

void HexagonVectorLoopCarriedReuse::findLoopCarriedDeps() {
  BasicBlock *BB = CurLoop->getHeader();
  for (auto I = BB->begin(), E = BB->end(); I != E && isa<PHINode>(I); ++I) {
    auto *PN = cast<PHINode>(I);
    if (!isa<VectorType>(PN->getType()))
      continue;

    DepChain *D = new DepChain();
    findDepChainFromPHI(PN, *D);
    if (D->size() != 0)
      Dependences.insert(D);
    else
      delete D;
  }
  LLVM_DEBUG(dbgs() << "Found " << Dependences.size() << " dependences\n");
  LLVM_DEBUG(for (const DepChain *D : Dependences) dbgs() << *D << "\n";);
}

Pass *llvm::createHexagonVectorLoopCarriedReuseLegacyPass() {
  return new HexagonVectorLoopCarriedReuseLegacyPass();
}
