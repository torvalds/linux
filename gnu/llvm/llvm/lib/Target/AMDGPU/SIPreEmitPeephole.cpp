//===-- SIPreEmitPeephole.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass performs the peephole optimizations before code emission.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "si-pre-emit-peephole"

static unsigned SkipThreshold;

static cl::opt<unsigned, true> SkipThresholdFlag(
    "amdgpu-skip-threshold", cl::Hidden,
    cl::desc(
        "Number of instructions before jumping over divergent control flow"),
    cl::location(SkipThreshold), cl::init(12));

namespace {

class SIPreEmitPeephole : public MachineFunctionPass {
private:
  const SIInstrInfo *TII = nullptr;
  const SIRegisterInfo *TRI = nullptr;

  bool optimizeVccBranch(MachineInstr &MI) const;
  bool optimizeSetGPR(MachineInstr &First, MachineInstr &MI) const;
  bool getBlockDestinations(MachineBasicBlock &SrcMBB,
                            MachineBasicBlock *&TrueMBB,
                            MachineBasicBlock *&FalseMBB,
                            SmallVectorImpl<MachineOperand> &Cond);
  bool mustRetainExeczBranch(const MachineBasicBlock &From,
                             const MachineBasicBlock &To) const;
  bool removeExeczBranch(MachineInstr &MI, MachineBasicBlock &SrcMBB);

public:
  static char ID;

  SIPreEmitPeephole() : MachineFunctionPass(ID) {
    initializeSIPreEmitPeepholePass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // End anonymous namespace.

INITIALIZE_PASS(SIPreEmitPeephole, DEBUG_TYPE,
                "SI peephole optimizations", false, false)

char SIPreEmitPeephole::ID = 0;

char &llvm::SIPreEmitPeepholeID = SIPreEmitPeephole::ID;

bool SIPreEmitPeephole::optimizeVccBranch(MachineInstr &MI) const {
  // Match:
  // sreg = -1 or 0
  // vcc = S_AND_B64 exec, sreg or S_ANDN2_B64 exec, sreg
  // S_CBRANCH_VCC[N]Z
  // =>
  // S_CBRANCH_EXEC[N]Z
  // We end up with this pattern sometimes after basic block placement.
  // It happens while combining a block which assigns -1 or 0 to a saved mask
  // and another block which consumes that saved mask and then a branch.
  //
  // While searching this also performs the following substitution:
  // vcc = V_CMP
  // vcc = S_AND exec, vcc
  // S_CBRANCH_VCC[N]Z
  // =>
  // vcc = V_CMP
  // S_CBRANCH_VCC[N]Z

  bool Changed = false;
  MachineBasicBlock &MBB = *MI.getParent();
  const GCNSubtarget &ST = MBB.getParent()->getSubtarget<GCNSubtarget>();
  const bool IsWave32 = ST.isWave32();
  const unsigned CondReg = TRI->getVCC();
  const unsigned ExecReg = IsWave32 ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
  const unsigned And = IsWave32 ? AMDGPU::S_AND_B32 : AMDGPU::S_AND_B64;
  const unsigned AndN2 = IsWave32 ? AMDGPU::S_ANDN2_B32 : AMDGPU::S_ANDN2_B64;
  const unsigned Mov = IsWave32 ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;

  MachineBasicBlock::reverse_iterator A = MI.getReverseIterator(),
                                      E = MBB.rend();
  bool ReadsCond = false;
  unsigned Threshold = 5;
  for (++A; A != E; ++A) {
    if (!--Threshold)
      return false;
    if (A->modifiesRegister(ExecReg, TRI))
      return false;
    if (A->modifiesRegister(CondReg, TRI)) {
      if (!A->definesRegister(CondReg, TRI) ||
          (A->getOpcode() != And && A->getOpcode() != AndN2))
        return false;
      break;
    }
    ReadsCond |= A->readsRegister(CondReg, TRI);
  }
  if (A == E)
    return false;

  MachineOperand &Op1 = A->getOperand(1);
  MachineOperand &Op2 = A->getOperand(2);
  if (Op1.getReg() != ExecReg && Op2.isReg() && Op2.getReg() == ExecReg) {
    TII->commuteInstruction(*A);
    Changed = true;
  }
  if (Op1.getReg() != ExecReg)
    return Changed;
  if (Op2.isImm() && !(Op2.getImm() == -1 || Op2.getImm() == 0))
    return Changed;

  int64_t MaskValue = 0;
  Register SReg;
  if (Op2.isReg()) {
    SReg = Op2.getReg();
    auto M = std::next(A);
    bool ReadsSreg = false;
    bool ModifiesExec = false;
    for (; M != E; ++M) {
      if (M->definesRegister(SReg, TRI))
        break;
      if (M->modifiesRegister(SReg, TRI))
        return Changed;
      ReadsSreg |= M->readsRegister(SReg, TRI);
      ModifiesExec |= M->modifiesRegister(ExecReg, TRI);
    }
    if (M == E)
      return Changed;
    // If SReg is VCC and SReg definition is a VALU comparison.
    // This means S_AND with EXEC is not required.
    // Erase the S_AND and return.
    // Note: isVOPC is used instead of isCompare to catch V_CMP_CLASS
    if (A->getOpcode() == And && SReg == CondReg && !ModifiesExec &&
        TII->isVOPC(*M)) {
      A->eraseFromParent();
      return true;
    }
    if (!M->isMoveImmediate() || !M->getOperand(1).isImm() ||
        (M->getOperand(1).getImm() != -1 && M->getOperand(1).getImm() != 0))
      return Changed;
    MaskValue = M->getOperand(1).getImm();
    // First if sreg is only used in the AND instruction fold the immediate
    // into the AND.
    if (!ReadsSreg && Op2.isKill()) {
      A->getOperand(2).ChangeToImmediate(MaskValue);
      M->eraseFromParent();
    }
  } else if (Op2.isImm()) {
    MaskValue = Op2.getImm();
  } else {
    llvm_unreachable("Op2 must be register or immediate");
  }

  // Invert mask for s_andn2
  assert(MaskValue == 0 || MaskValue == -1);
  if (A->getOpcode() == AndN2)
    MaskValue = ~MaskValue;

  if (!ReadsCond && A->registerDefIsDead(AMDGPU::SCC, /*TRI=*/nullptr)) {
    if (!MI.killsRegister(CondReg, TRI)) {
      // Replace AND with MOV
      if (MaskValue == 0) {
        BuildMI(*A->getParent(), *A, A->getDebugLoc(), TII->get(Mov), CondReg)
            .addImm(0);
      } else {
        BuildMI(*A->getParent(), *A, A->getDebugLoc(), TII->get(Mov), CondReg)
            .addReg(ExecReg);
      }
    }
    // Remove AND instruction
    A->eraseFromParent();
  }

  bool IsVCCZ = MI.getOpcode() == AMDGPU::S_CBRANCH_VCCZ;
  if (SReg == ExecReg) {
    // EXEC is updated directly
    if (IsVCCZ) {
      MI.eraseFromParent();
      return true;
    }
    MI.setDesc(TII->get(AMDGPU::S_BRANCH));
  } else if (IsVCCZ && MaskValue == 0) {
    // Will always branch
    // Remove all successors shadowed by new unconditional branch
    MachineBasicBlock *Parent = MI.getParent();
    SmallVector<MachineInstr *, 4> ToRemove;
    bool Found = false;
    for (MachineInstr &Term : Parent->terminators()) {
      if (Found) {
        if (Term.isBranch())
          ToRemove.push_back(&Term);
      } else {
        Found = Term.isIdenticalTo(MI);
      }
    }
    assert(Found && "conditional branch is not terminator");
    for (auto *BranchMI : ToRemove) {
      MachineOperand &Dst = BranchMI->getOperand(0);
      assert(Dst.isMBB() && "destination is not basic block");
      Parent->removeSuccessor(Dst.getMBB());
      BranchMI->eraseFromParent();
    }

    if (MachineBasicBlock *Succ = Parent->getFallThrough()) {
      Parent->removeSuccessor(Succ);
    }

    // Rewrite to unconditional branch
    MI.setDesc(TII->get(AMDGPU::S_BRANCH));
  } else if (!IsVCCZ && MaskValue == 0) {
    // Will never branch
    MachineOperand &Dst = MI.getOperand(0);
    assert(Dst.isMBB() && "destination is not basic block");
    MI.getParent()->removeSuccessor(Dst.getMBB());
    MI.eraseFromParent();
    return true;
  } else if (MaskValue == -1) {
    // Depends only on EXEC
    MI.setDesc(
        TII->get(IsVCCZ ? AMDGPU::S_CBRANCH_EXECZ : AMDGPU::S_CBRANCH_EXECNZ));
  }

  MI.removeOperand(MI.findRegisterUseOperandIdx(CondReg, TRI, false /*Kill*/));
  MI.addImplicitDefUseOperands(*MBB.getParent());

  return true;
}

bool SIPreEmitPeephole::optimizeSetGPR(MachineInstr &First,
                                       MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineOperand *Idx = TII->getNamedOperand(MI, AMDGPU::OpName::src0);
  Register IdxReg = Idx->isReg() ? Idx->getReg() : Register();
  SmallVector<MachineInstr *, 4> ToRemove;
  bool IdxOn = true;

  if (!MI.isIdenticalTo(First))
    return false;

  // Scan back to find an identical S_SET_GPR_IDX_ON
  for (MachineBasicBlock::instr_iterator I = std::next(First.getIterator()),
                                         E = MI.getIterator();
       I != E; ++I) {
    if (I->isBundle())
      continue;
    switch (I->getOpcode()) {
    case AMDGPU::S_SET_GPR_IDX_MODE:
      return false;
    case AMDGPU::S_SET_GPR_IDX_OFF:
      IdxOn = false;
      ToRemove.push_back(&*I);
      break;
    default:
      if (I->modifiesRegister(AMDGPU::M0, TRI))
        return false;
      if (IdxReg && I->modifiesRegister(IdxReg, TRI))
        return false;
      if (llvm::any_of(I->operands(),
                       [&MRI, this](const MachineOperand &MO) {
                         return MO.isReg() &&
                                TRI->isVectorRegister(MRI, MO.getReg());
                       })) {
        // The only exception allowed here is another indirect vector move
        // with the same mode.
        if (!IdxOn || !(I->getOpcode() == AMDGPU::V_MOV_B32_indirect_write ||
                        I->getOpcode() == AMDGPU::V_MOV_B32_indirect_read))
          return false;
      }
    }
  }

  MI.eraseFromBundle();
  for (MachineInstr *RI : ToRemove)
    RI->eraseFromBundle();
  return true;
}

bool SIPreEmitPeephole::getBlockDestinations(
    MachineBasicBlock &SrcMBB, MachineBasicBlock *&TrueMBB,
    MachineBasicBlock *&FalseMBB, SmallVectorImpl<MachineOperand> &Cond) {
  if (TII->analyzeBranch(SrcMBB, TrueMBB, FalseMBB, Cond))
    return false;

  if (!FalseMBB)
    FalseMBB = SrcMBB.getNextNode();

  return true;
}

bool SIPreEmitPeephole::mustRetainExeczBranch(
    const MachineBasicBlock &From, const MachineBasicBlock &To) const {
  unsigned NumInstr = 0;
  const MachineFunction *MF = From.getParent();

  for (MachineFunction::const_iterator MBBI(&From), ToI(&To), End = MF->end();
       MBBI != End && MBBI != ToI; ++MBBI) {
    const MachineBasicBlock &MBB = *MBBI;

    for (const MachineInstr &MI : MBB) {
      // When a uniform loop is inside non-uniform control flow, the branch
      // leaving the loop might never be taken when EXEC = 0.
      // Hence we should retain cbranch out of the loop lest it become infinite.
      if (MI.isConditionalBranch())
        return true;

      if (MI.isMetaInstruction())
        continue;

      if (TII->hasUnwantedEffectsWhenEXECEmpty(MI))
        return true;

      // These instructions are potentially expensive even if EXEC = 0.
      if (TII->isSMRD(MI) || TII->isVMEM(MI) || TII->isFLAT(MI) ||
          TII->isDS(MI) || TII->isWaitcnt(MI.getOpcode()))
        return true;

      ++NumInstr;
      if (NumInstr >= SkipThreshold)
        return true;
    }
  }

  return false;
}

// Returns true if the skip branch instruction is removed.
bool SIPreEmitPeephole::removeExeczBranch(MachineInstr &MI,
                                          MachineBasicBlock &SrcMBB) {
  MachineBasicBlock *TrueMBB = nullptr;
  MachineBasicBlock *FalseMBB = nullptr;
  SmallVector<MachineOperand, 1> Cond;

  if (!getBlockDestinations(SrcMBB, TrueMBB, FalseMBB, Cond))
    return false;

  // Consider only the forward branches.
  if ((SrcMBB.getNumber() >= TrueMBB->getNumber()) ||
      mustRetainExeczBranch(*FalseMBB, *TrueMBB))
    return false;

  LLVM_DEBUG(dbgs() << "Removing the execz branch: " << MI);
  MI.eraseFromParent();
  SrcMBB.removeSuccessor(TrueMBB);

  return true;
}

bool SIPreEmitPeephole::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();
  TRI = &TII->getRegisterInfo();
  bool Changed = false;

  MF.RenumberBlocks();

  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator TermI = MBB.getFirstTerminator();
    // Check first terminator for branches to optimize
    if (TermI != MBB.end()) {
      MachineInstr &MI = *TermI;
      switch (MI.getOpcode()) {
      case AMDGPU::S_CBRANCH_VCCZ:
      case AMDGPU::S_CBRANCH_VCCNZ:
        Changed |= optimizeVccBranch(MI);
        break;
      case AMDGPU::S_CBRANCH_EXECZ:
        Changed |= removeExeczBranch(MI, MBB);
        break;
      }
    }

    if (!ST.hasVGPRIndexMode())
      continue;

    MachineInstr *SetGPRMI = nullptr;
    const unsigned Threshold = 20;
    unsigned Count = 0;
    // Scan the block for two S_SET_GPR_IDX_ON instructions to see if a
    // second is not needed. Do expensive checks in the optimizeSetGPR()
    // and limit the distance to 20 instructions for compile time purposes.
    // Note: this needs to work on bundles as S_SET_GPR_IDX* instructions
    // may be bundled with the instructions they modify.
    for (auto &MI : make_early_inc_range(MBB.instrs())) {
      if (Count == Threshold)
        SetGPRMI = nullptr;
      else
        ++Count;

      if (MI.getOpcode() != AMDGPU::S_SET_GPR_IDX_ON)
        continue;

      Count = 0;
      if (!SetGPRMI) {
        SetGPRMI = &MI;
        continue;
      }

      if (optimizeSetGPR(*SetGPRMI, MI))
        Changed = true;
      else
        SetGPRMI = &MI;
    }
  }

  return Changed;
}
