//===---------------- BPFAdjustOpt.cpp - Adjust Optimization --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Adjust optimization to make the code more kernel verifier friendly.
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFCORE.h"
#include "BPFTargetMachine.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsBPF.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "bpf-adjust-opt"

using namespace llvm;
using namespace llvm::PatternMatch;

static cl::opt<bool>
    DisableBPFserializeICMP("bpf-disable-serialize-icmp", cl::Hidden,
                            cl::desc("BPF: Disable Serializing ICMP insns."),
                            cl::init(false));

static cl::opt<bool> DisableBPFavoidSpeculation(
    "bpf-disable-avoid-speculation", cl::Hidden,
    cl::desc("BPF: Disable Avoiding Speculative Code Motion."),
    cl::init(false));

namespace {
class BPFAdjustOptImpl {
  struct PassThroughInfo {
    Instruction *Input;
    Instruction *UsedInst;
    uint32_t OpIdx;
    PassThroughInfo(Instruction *I, Instruction *U, uint32_t Idx)
        : Input(I), UsedInst(U), OpIdx(Idx) {}
  };

public:
  BPFAdjustOptImpl(Module *M) : M(M) {}

  bool run();

private:
  Module *M;
  SmallVector<PassThroughInfo, 16> PassThroughs;

  bool adjustICmpToBuiltin();
  void adjustBasicBlock(BasicBlock &BB);
  bool serializeICMPCrossBB(BasicBlock &BB);
  void adjustInst(Instruction &I);
  bool serializeICMPInBB(Instruction &I);
  bool avoidSpeculation(Instruction &I);
  bool insertPassThrough();
};

} // End anonymous namespace

bool BPFAdjustOptImpl::run() {
  bool Changed = adjustICmpToBuiltin();

  for (Function &F : *M)
    for (auto &BB : F) {
      adjustBasicBlock(BB);
      for (auto &I : BB)
        adjustInst(I);
    }
  return insertPassThrough() || Changed;
}

// Commit acabad9ff6bf ("[InstCombine] try to canonicalize icmp with
// trunc op into mask and cmp") added a transformation to
// convert "(conv)a < power_2_const" to "a & <const>" in certain
// cases and bpf kernel verifier has to handle the resulted code
// conservatively and this may reject otherwise legitimate program.
// Here, we change related icmp code to a builtin which will
// be restored to original icmp code later to prevent that
// InstCombine transformatin.
bool BPFAdjustOptImpl::adjustICmpToBuiltin() {
  bool Changed = false;
  ICmpInst *ToBeDeleted = nullptr;
  for (Function &F : *M)
    for (auto &BB : F)
      for (auto &I : BB) {
        if (ToBeDeleted) {
          ToBeDeleted->eraseFromParent();
          ToBeDeleted = nullptr;
        }

        auto *Icmp = dyn_cast<ICmpInst>(&I);
        if (!Icmp)
          continue;

        Value *Op0 = Icmp->getOperand(0);
        if (!isa<TruncInst>(Op0))
          continue;

        auto ConstOp1 = dyn_cast<ConstantInt>(Icmp->getOperand(1));
        if (!ConstOp1)
          continue;

        auto ConstOp1Val = ConstOp1->getValue().getZExtValue();
        auto Op = Icmp->getPredicate();
        if (Op == ICmpInst::ICMP_ULT || Op == ICmpInst::ICMP_UGE) {
          if ((ConstOp1Val - 1) & ConstOp1Val)
            continue;
        } else if (Op == ICmpInst::ICMP_ULE || Op == ICmpInst::ICMP_UGT) {
          if (ConstOp1Val & (ConstOp1Val + 1))
            continue;
        } else {
          continue;
        }

        Constant *Opcode =
            ConstantInt::get(Type::getInt32Ty(BB.getContext()), Op);
        Function *Fn = Intrinsic::getDeclaration(
            M, Intrinsic::bpf_compare, {Op0->getType(), ConstOp1->getType()});
        auto *NewInst = CallInst::Create(Fn, {Opcode, Op0, ConstOp1});
        NewInst->insertBefore(&I);
        Icmp->replaceAllUsesWith(NewInst);
        Changed = true;
        ToBeDeleted = Icmp;
      }

  return Changed;
}

bool BPFAdjustOptImpl::insertPassThrough() {
  for (auto &Info : PassThroughs) {
    auto *CI = BPFCoreSharedInfo::insertPassThrough(
        M, Info.UsedInst->getParent(), Info.Input, Info.UsedInst);
    Info.UsedInst->setOperand(Info.OpIdx, CI);
  }

  return !PassThroughs.empty();
}

// To avoid combining conditionals in the same basic block by
// instrcombine optimization.
bool BPFAdjustOptImpl::serializeICMPInBB(Instruction &I) {
  // For:
  //   comp1 = icmp <opcode> ...;
  //   comp2 = icmp <opcode> ...;
  //   ... or comp1 comp2 ...
  // changed to:
  //   comp1 = icmp <opcode> ...;
  //   comp2 = icmp <opcode> ...;
  //   new_comp1 = __builtin_bpf_passthrough(seq_num, comp1)
  //   ... or new_comp1 comp2 ...
  Value *Op0, *Op1;
  // Use LogicalOr (accept `or i1` as well as `select i1 Op0, true, Op1`)
  if (!match(&I, m_LogicalOr(m_Value(Op0), m_Value(Op1))))
    return false;
  auto *Icmp1 = dyn_cast<ICmpInst>(Op0);
  if (!Icmp1)
    return false;
  auto *Icmp2 = dyn_cast<ICmpInst>(Op1);
  if (!Icmp2)
    return false;

  Value *Icmp1Op0 = Icmp1->getOperand(0);
  Value *Icmp2Op0 = Icmp2->getOperand(0);
  if (Icmp1Op0 != Icmp2Op0)
    return false;

  // Now we got two icmp instructions which feed into
  // an "or" instruction.
  PassThroughInfo Info(Icmp1, &I, 0);
  PassThroughs.push_back(Info);
  return true;
}

// To avoid combining conditionals in the same basic block by
// instrcombine optimization.
bool BPFAdjustOptImpl::serializeICMPCrossBB(BasicBlock &BB) {
  // For:
  //   B1:
  //     comp1 = icmp <opcode> ...;
  //     if (comp1) goto B2 else B3;
  //   B2:
  //     comp2 = icmp <opcode> ...;
  //     if (comp2) goto B4 else B5;
  //   B4:
  //     ...
  // changed to:
  //   B1:
  //     comp1 = icmp <opcode> ...;
  //     comp1 = __builtin_bpf_passthrough(seq_num, comp1);
  //     if (comp1) goto B2 else B3;
  //   B2:
  //     comp2 = icmp <opcode> ...;
  //     if (comp2) goto B4 else B5;
  //   B4:
  //     ...

  // Check basic predecessors, if two of them (say B1, B2) are using
  // icmp instructions to generate conditions and one is the predesessor
  // of another (e.g., B1 is the predecessor of B2). Add a passthrough
  // barrier after icmp inst of block B1.
  BasicBlock *B2 = BB.getSinglePredecessor();
  if (!B2)
    return false;

  BasicBlock *B1 = B2->getSinglePredecessor();
  if (!B1)
    return false;

  Instruction *TI = B2->getTerminator();
  auto *BI = dyn_cast<BranchInst>(TI);
  if (!BI || !BI->isConditional())
    return false;
  auto *Cond = dyn_cast<ICmpInst>(BI->getCondition());
  if (!Cond || B2->getFirstNonPHI() != Cond)
    return false;
  Value *B2Op0 = Cond->getOperand(0);
  auto Cond2Op = Cond->getPredicate();

  TI = B1->getTerminator();
  BI = dyn_cast<BranchInst>(TI);
  if (!BI || !BI->isConditional())
    return false;
  Cond = dyn_cast<ICmpInst>(BI->getCondition());
  if (!Cond)
    return false;
  Value *B1Op0 = Cond->getOperand(0);
  auto Cond1Op = Cond->getPredicate();

  if (B1Op0 != B2Op0)
    return false;

  if (Cond1Op == ICmpInst::ICMP_SGT || Cond1Op == ICmpInst::ICMP_SGE) {
    if (Cond2Op != ICmpInst::ICMP_SLT && Cond2Op != ICmpInst::ICMP_SLE)
      return false;
  } else if (Cond1Op == ICmpInst::ICMP_SLT || Cond1Op == ICmpInst::ICMP_SLE) {
    if (Cond2Op != ICmpInst::ICMP_SGT && Cond2Op != ICmpInst::ICMP_SGE)
      return false;
  } else if (Cond1Op == ICmpInst::ICMP_ULT || Cond1Op == ICmpInst::ICMP_ULE) {
    if (Cond2Op != ICmpInst::ICMP_UGT && Cond2Op != ICmpInst::ICMP_UGE)
      return false;
  } else if (Cond1Op == ICmpInst::ICMP_UGT || Cond1Op == ICmpInst::ICMP_UGE) {
    if (Cond2Op != ICmpInst::ICMP_ULT && Cond2Op != ICmpInst::ICMP_ULE)
      return false;
  } else {
    return false;
  }

  PassThroughInfo Info(Cond, BI, 0);
  PassThroughs.push_back(Info);

  return true;
}

// To avoid speculative hoisting certain computations out of
// a basic block.
bool BPFAdjustOptImpl::avoidSpeculation(Instruction &I) {
  if (auto *LdInst = dyn_cast<LoadInst>(&I)) {
    if (auto *GV = dyn_cast<GlobalVariable>(LdInst->getOperand(0))) {
      if (GV->hasAttribute(BPFCoreSharedInfo::AmaAttr) ||
          GV->hasAttribute(BPFCoreSharedInfo::TypeIdAttr))
        return false;
    }
  }

  if (!isa<LoadInst>(&I) && !isa<CallInst>(&I))
    return false;

  // For:
  //   B1:
  //     var = ...
  //     ...
  //     /* icmp may not be in the same block as var = ... */
  //     comp1 = icmp <opcode> var, <const>;
  //     if (comp1) goto B2 else B3;
  //   B2:
  //     ... var ...
  // change to:
  //   B1:
  //     var = ...
  //     ...
  //     /* icmp may not be in the same block as var = ... */
  //     comp1 = icmp <opcode> var, <const>;
  //     if (comp1) goto B2 else B3;
  //   B2:
  //     var = __builtin_bpf_passthrough(seq_num, var);
  //     ... var ...
  bool isCandidate = false;
  SmallVector<PassThroughInfo, 4> Candidates;
  for (User *U : I.users()) {
    Instruction *Inst = dyn_cast<Instruction>(U);
    if (!Inst)
      continue;

    // May cover a little bit more than the
    // above pattern.
    if (auto *Icmp1 = dyn_cast<ICmpInst>(Inst)) {
      Value *Icmp1Op1 = Icmp1->getOperand(1);
      if (!isa<Constant>(Icmp1Op1))
        return false;
      isCandidate = true;
      continue;
    }

    // Ignore the use in the same basic block as the definition.
    if (Inst->getParent() == I.getParent())
      continue;

    // use in a different basic block, If there is a call or
    // load/store insn before this instruction in this basic
    // block. Most likely it cannot be hoisted out. Skip it.
    for (auto &I2 : *Inst->getParent()) {
      if (isa<CallInst>(&I2))
        return false;
      if (isa<LoadInst>(&I2) || isa<StoreInst>(&I2))
        return false;
      if (&I2 == Inst)
        break;
    }

    // It should be used in a GEP or a simple arithmetic like
    // ZEXT/SEXT which is used for GEP.
    if (Inst->getOpcode() == Instruction::ZExt ||
        Inst->getOpcode() == Instruction::SExt) {
      PassThroughInfo Info(&I, Inst, 0);
      Candidates.push_back(Info);
    } else if (auto *GI = dyn_cast<GetElementPtrInst>(Inst)) {
      // traverse GEP inst to find Use operand index
      unsigned i, e;
      for (i = 1, e = GI->getNumOperands(); i != e; ++i) {
        Value *V = GI->getOperand(i);
        if (V == &I)
          break;
      }
      if (i == e)
        continue;

      PassThroughInfo Info(&I, GI, i);
      Candidates.push_back(Info);
    }
  }

  if (!isCandidate || Candidates.empty())
    return false;

  llvm::append_range(PassThroughs, Candidates);
  return true;
}

void BPFAdjustOptImpl::adjustBasicBlock(BasicBlock &BB) {
  if (!DisableBPFserializeICMP && serializeICMPCrossBB(BB))
    return;
}

void BPFAdjustOptImpl::adjustInst(Instruction &I) {
  if (!DisableBPFserializeICMP && serializeICMPInBB(I))
    return;
  if (!DisableBPFavoidSpeculation && avoidSpeculation(I))
    return;
}

PreservedAnalyses BPFAdjustOptPass::run(Module &M, ModuleAnalysisManager &AM) {
  return BPFAdjustOptImpl(&M).run() ? PreservedAnalyses::none()
                                    : PreservedAnalyses::all();
}
