//===------------------ AMDGPUCustomBehaviour.cpp ---------------*-C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods from the AMDGPUCustomBehaviour class.
///
//===----------------------------------------------------------------------===//

#include "AMDGPUCustomBehaviour.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "TargetInfo/AMDGPUTargetInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/WithColor.h"

namespace llvm::mca {

void AMDGPUInstrPostProcess::postProcessInstruction(
    std::unique_ptr<Instruction> &Inst, const MCInst &MCI) {
  switch (MCI.getOpcode()) {
  case AMDGPU::S_WAITCNT:
  case AMDGPU::S_WAITCNT_soft:
  case AMDGPU::S_WAITCNT_EXPCNT:
  case AMDGPU::S_WAITCNT_LGKMCNT:
  case AMDGPU::S_WAITCNT_VMCNT:
  case AMDGPU::S_WAITCNT_VSCNT:
  case AMDGPU::S_WAITCNT_VSCNT_soft:
  case AMDGPU::S_WAITCNT_EXPCNT_gfx10:
  case AMDGPU::S_WAITCNT_LGKMCNT_gfx10:
  case AMDGPU::S_WAITCNT_VMCNT_gfx10:
  case AMDGPU::S_WAITCNT_VSCNT_gfx10:
  case AMDGPU::S_WAITCNT_gfx10:
  case AMDGPU::S_WAITCNT_gfx6_gfx7:
  case AMDGPU::S_WAITCNT_vi:
    return processWaitCnt(Inst, MCI);
  }
}

// s_waitcnt instructions encode important information as immediate operands
// which are lost during the MCInst -> mca::Instruction lowering.
void AMDGPUInstrPostProcess::processWaitCnt(std::unique_ptr<Instruction> &Inst,
                                            const MCInst &MCI) {
  for (int Idx = 0, N = MCI.size(); Idx < N; Idx++) {
    MCAOperand Op;
    const MCOperand &MCOp = MCI.getOperand(Idx);
    if (MCOp.isReg()) {
      Op = MCAOperand::createReg(MCOp.getReg());
    } else if (MCOp.isImm()) {
      Op = MCAOperand::createImm(MCOp.getImm());
    }
    Op.setIndex(Idx);
    Inst->addOperand(Op);
  }
}

AMDGPUCustomBehaviour::AMDGPUCustomBehaviour(const MCSubtargetInfo &STI,
                                             const mca::SourceMgr &SrcMgr,
                                             const MCInstrInfo &MCII)
    : CustomBehaviour(STI, SrcMgr, MCII) {
  generateWaitCntInfo();
}

unsigned AMDGPUCustomBehaviour::checkCustomHazard(ArrayRef<InstRef> IssuedInst,
                                                  const InstRef &IR) {
  const Instruction &Inst = *IR.getInstruction();
  unsigned Opcode = Inst.getOpcode();

  // llvm-mca is generally run on fully compiled assembly so we wouldn't see any
  // pseudo instructions here. However, there are plans for the future to make
  // it possible to use mca within backend passes. As such, I have left the
  // pseudo version of s_waitcnt within this switch statement.
  switch (Opcode) {
  default:
    return 0;
  case AMDGPU::S_WAITCNT: // This instruction
  case AMDGPU::S_WAITCNT_soft:
  case AMDGPU::S_WAITCNT_EXPCNT:
  case AMDGPU::S_WAITCNT_LGKMCNT:
  case AMDGPU::S_WAITCNT_VMCNT:
  case AMDGPU::S_WAITCNT_VSCNT:
  case AMDGPU::S_WAITCNT_VSCNT_soft: // to this instruction are all pseudo.
  case AMDGPU::S_WAITCNT_EXPCNT_gfx10:
  case AMDGPU::S_WAITCNT_LGKMCNT_gfx10:
  case AMDGPU::S_WAITCNT_VMCNT_gfx10:
  case AMDGPU::S_WAITCNT_VSCNT_gfx10:
  case AMDGPU::S_WAITCNT_gfx10:
  case AMDGPU::S_WAITCNT_gfx6_gfx7:
  case AMDGPU::S_WAITCNT_vi:
    // s_endpgm also behaves as if there is an implicit
    // s_waitcnt 0, but I'm not sure if it would be appropriate
    // to model this in llvm-mca based on how the iterations work
    // while simulating the pipeline over and over.
    return handleWaitCnt(IssuedInst, IR);
  }

  return 0;
}

unsigned AMDGPUCustomBehaviour::handleWaitCnt(ArrayRef<InstRef> IssuedInst,
                                              const InstRef &IR) {
  // Currently, all s_waitcnt instructions are handled except s_waitcnt_depctr.
  // I do not know how that instruction works so I did not attempt to model it.
  // set the max values to begin
  unsigned Vmcnt = 63;
  unsigned Expcnt = 7;
  unsigned Lgkmcnt = 31;
  unsigned Vscnt = 63;
  unsigned CurrVmcnt = 0;
  unsigned CurrExpcnt = 0;
  unsigned CurrLgkmcnt = 0;
  unsigned CurrVscnt = 0;
  unsigned CyclesToWaitVm = ~0U;
  unsigned CyclesToWaitExp = ~0U;
  unsigned CyclesToWaitLgkm = ~0U;
  unsigned CyclesToWaitVs = ~0U;

  computeWaitCnt(IR, Vmcnt, Expcnt, Lgkmcnt, Vscnt);

  // We will now look at each of the currently executing instructions
  // to find out if this wait instruction still needs to wait.
  for (const InstRef &PrevIR : IssuedInst) {
    const Instruction &PrevInst = *PrevIR.getInstruction();
    const unsigned PrevInstIndex = PrevIR.getSourceIndex() % SrcMgr.size();
    const WaitCntInfo &PrevInstWaitInfo = InstrWaitCntInfo[PrevInstIndex];
    const int CyclesLeft = PrevInst.getCyclesLeft();
    assert(CyclesLeft != UNKNOWN_CYCLES &&
           "We should know how many cycles are left for this instruction");
    if (PrevInstWaitInfo.VmCnt) {
      CurrVmcnt++;
      if ((unsigned)CyclesLeft < CyclesToWaitVm)
        CyclesToWaitVm = CyclesLeft;
    }
    if (PrevInstWaitInfo.ExpCnt) {
      CurrExpcnt++;
      if ((unsigned)CyclesLeft < CyclesToWaitExp)
        CyclesToWaitExp = CyclesLeft;
    }
    if (PrevInstWaitInfo.LgkmCnt) {
      CurrLgkmcnt++;
      if ((unsigned)CyclesLeft < CyclesToWaitLgkm)
        CyclesToWaitLgkm = CyclesLeft;
    }
    if (PrevInstWaitInfo.VsCnt) {
      CurrVscnt++;
      if ((unsigned)CyclesLeft < CyclesToWaitVs)
        CyclesToWaitVs = CyclesLeft;
    }
  }

  unsigned CyclesToWait = ~0U;
  if (CurrVmcnt > Vmcnt && CyclesToWaitVm < CyclesToWait)
    CyclesToWait = CyclesToWaitVm;
  if (CurrExpcnt > Expcnt && CyclesToWaitExp < CyclesToWait)
    CyclesToWait = CyclesToWaitExp;
  if (CurrLgkmcnt > Lgkmcnt && CyclesToWaitLgkm < CyclesToWait)
    CyclesToWait = CyclesToWaitLgkm;
  if (CurrVscnt > Vscnt && CyclesToWaitVs < CyclesToWait)
    CyclesToWait = CyclesToWaitVs;

  // We may underestimate how many cycles we need to wait, but this
  // isn't a big deal. Our return value is just how many cycles until
  // this function gets run again. So as long as we don't overestimate
  // the wait time, we'll still end up stalling at this instruction
  // for the correct number of cycles.

  if (CyclesToWait == ~0U)
    return 0;
  return CyclesToWait;
}

void AMDGPUCustomBehaviour::computeWaitCnt(const InstRef &IR, unsigned &Vmcnt,
                                           unsigned &Expcnt, unsigned &Lgkmcnt,
                                           unsigned &Vscnt) {
  AMDGPU::IsaVersion IV = AMDGPU::getIsaVersion(STI.getCPU());
  const Instruction &Inst = *IR.getInstruction();
  unsigned Opcode = Inst.getOpcode();

  switch (Opcode) {
  case AMDGPU::S_WAITCNT_EXPCNT_gfx10:
  case AMDGPU::S_WAITCNT_LGKMCNT_gfx10:
  case AMDGPU::S_WAITCNT_VMCNT_gfx10:
  case AMDGPU::S_WAITCNT_VSCNT_gfx10: {
    // Should probably be checking for nullptr
    // here, but I'm not sure how I should handle the case
    // where we see a nullptr.
    const MCAOperand *OpReg = Inst.getOperand(0);
    const MCAOperand *OpImm = Inst.getOperand(1);
    assert(OpReg && OpReg->isReg() && "First operand should be a register.");
    assert(OpImm && OpImm->isImm() && "Second operand should be an immediate.");
    if (OpReg->getReg() != AMDGPU::SGPR_NULL) {
      // Instruction is using a real register.
      // Since we can't know what value this register will have,
      // we can't compute what the value of this wait should be.
      WithColor::warning() << "The register component of "
                           << MCII.getName(Opcode) << " will be completely "
                           << "ignored. So the wait may not be accurate.\n";
    }
    switch (Opcode) {
    // Redundant switch so I don't have to repeat the code above
    // for each case. There are more clever ways to avoid this
    // extra switch and anyone can feel free to implement one of them.
    case AMDGPU::S_WAITCNT_EXPCNT_gfx10:
      Expcnt = OpImm->getImm();
      break;
    case AMDGPU::S_WAITCNT_LGKMCNT_gfx10:
      Lgkmcnt = OpImm->getImm();
      break;
    case AMDGPU::S_WAITCNT_VMCNT_gfx10:
      Vmcnt = OpImm->getImm();
      break;
    case AMDGPU::S_WAITCNT_VSCNT_gfx10:
      Vscnt = OpImm->getImm();
      break;
    }
    return;
  }
  case AMDGPU::S_WAITCNT_gfx10:
  case AMDGPU::S_WAITCNT_gfx6_gfx7:
  case AMDGPU::S_WAITCNT_vi:
    unsigned WaitCnt = Inst.getOperand(0)->getImm();
    AMDGPU::decodeWaitcnt(IV, WaitCnt, Vmcnt, Expcnt, Lgkmcnt);
    return;
  }
}

void AMDGPUCustomBehaviour::generateWaitCntInfo() {
  // The core logic from this function is taken from
  // SIInsertWaitcnts::updateEventWaitcntAfter() In that pass, the instructions
  // that are being looked at are in the MachineInstr format, whereas we have
  // access to the MCInst format. The side effects of this are that we can't use
  // the mayAccessVMEMThroughFlat(Inst) or mayAccessLDSThroughFlat(Inst)
  // functions. Therefore, we conservatively assume that these functions will
  // return true. This may cause a few instructions to be incorrectly tagged
  // with an extra CNT. However, these are instructions that do interact with at
  // least one CNT so giving them an extra CNT shouldn't cause issues in most
  // scenarios.
  AMDGPU::IsaVersion IV = AMDGPU::getIsaVersion(STI.getCPU());
  InstrWaitCntInfo.resize(SrcMgr.size());

  for (const auto &EN : llvm::enumerate(SrcMgr.getInstructions())) {
    const std::unique_ptr<Instruction> &Inst = EN.value();
    unsigned Index = EN.index();
    unsigned Opcode = Inst->getOpcode();
    const MCInstrDesc &MCID = MCII.get(Opcode);
    if ((MCID.TSFlags & SIInstrFlags::DS) &&
        (MCID.TSFlags & SIInstrFlags::LGKM_CNT)) {
      InstrWaitCntInfo[Index].LgkmCnt = true;
      if (isAlwaysGDS(Opcode) || hasModifiersSet(Inst, AMDGPU::OpName::gds))
        InstrWaitCntInfo[Index].ExpCnt = true;
    } else if (MCID.TSFlags & SIInstrFlags::FLAT) {
      // We conservatively assume that mayAccessVMEMThroughFlat(Inst)
      // and mayAccessLDSThroughFlat(Inst) would both return true for this
      // instruction. We have to do this because those functions use
      // information about the memory operands that we don't have access to.
      InstrWaitCntInfo[Index].LgkmCnt = true;
      if (!STI.hasFeature(AMDGPU::FeatureVscnt))
        InstrWaitCntInfo[Index].VmCnt = true;
      else if (MCID.mayLoad() && !(MCID.TSFlags & SIInstrFlags::IsAtomicNoRet))
        InstrWaitCntInfo[Index].VmCnt = true;
      else
        InstrWaitCntInfo[Index].VsCnt = true;
    } else if (isVMEM(MCID) && !AMDGPU::getMUBUFIsBufferInv(Opcode)) {
      if (!STI.hasFeature(AMDGPU::FeatureVscnt))
        InstrWaitCntInfo[Index].VmCnt = true;
      else if ((MCID.mayLoad() &&
                !(MCID.TSFlags & SIInstrFlags::IsAtomicNoRet)) ||
               ((MCID.TSFlags & SIInstrFlags::MIMG) && !MCID.mayLoad() &&
                !MCID.mayStore()))
        InstrWaitCntInfo[Index].VmCnt = true;
      else if (MCID.mayStore())
        InstrWaitCntInfo[Index].VsCnt = true;

      // (IV.Major < 7) is meant to represent
      // GCNTarget.vmemWriteNeedsExpWaitcnt()
      // which is defined as
      // { return getGeneration() < SEA_ISLANDS; }
      if (IV.Major < 7 &&
          (MCID.mayStore() || (MCID.TSFlags & SIInstrFlags::IsAtomicRet)))
        InstrWaitCntInfo[Index].ExpCnt = true;
    } else if (MCID.TSFlags & SIInstrFlags::SMRD) {
      InstrWaitCntInfo[Index].LgkmCnt = true;
    } else if (MCID.TSFlags & SIInstrFlags::EXP) {
      InstrWaitCntInfo[Index].ExpCnt = true;
    } else {
      switch (Opcode) {
      case AMDGPU::S_SENDMSG:
      case AMDGPU::S_SENDMSGHALT:
      case AMDGPU::S_MEMTIME:
      case AMDGPU::S_MEMREALTIME:
        InstrWaitCntInfo[Index].LgkmCnt = true;
        break;
      }
    }
  }
}

// taken from SIInstrInfo::isVMEM()
bool AMDGPUCustomBehaviour::isVMEM(const MCInstrDesc &MCID) {
  return MCID.TSFlags & SIInstrFlags::MUBUF ||
         MCID.TSFlags & SIInstrFlags::MTBUF ||
         MCID.TSFlags & SIInstrFlags::MIMG;
}

// taken from SIInstrInfo::hasModifiersSet()
bool AMDGPUCustomBehaviour::hasModifiersSet(
    const std::unique_ptr<Instruction> &Inst, unsigned OpName) const {
  int Idx = AMDGPU::getNamedOperandIdx(Inst->getOpcode(), OpName);
  if (Idx == -1)
    return false;

  const MCAOperand *Op = Inst->getOperand(Idx);
  if (Op == nullptr || !Op->isImm() || !Op->getImm())
    return false;

  return true;
}

// taken from SIInstrInfo::isGWS()
bool AMDGPUCustomBehaviour::isGWS(uint16_t Opcode) const {
  const MCInstrDesc &MCID = MCII.get(Opcode);
  return MCID.TSFlags & SIInstrFlags::GWS;
}

// taken from SIInstrInfo::isAlwaysGDS()
bool AMDGPUCustomBehaviour::isAlwaysGDS(uint16_t Opcode) const {
  return Opcode == AMDGPU::DS_ORDERED_COUNT || isGWS(Opcode);
}

} // namespace llvm::mca

using namespace llvm;
using namespace mca;

static CustomBehaviour *
createAMDGPUCustomBehaviour(const MCSubtargetInfo &STI,
                            const mca::SourceMgr &SrcMgr,
                            const MCInstrInfo &MCII) {
  return new AMDGPUCustomBehaviour(STI, SrcMgr, MCII);
}

static InstrPostProcess *
createAMDGPUInstrPostProcess(const MCSubtargetInfo &STI,
                             const MCInstrInfo &MCII) {
  return new AMDGPUInstrPostProcess(STI, MCII);
}

/// Extern function to initialize the targets for the AMDGPU backend

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUTargetMCA() {
  TargetRegistry::RegisterCustomBehaviour(getTheR600Target(),
                                          createAMDGPUCustomBehaviour);
  TargetRegistry::RegisterInstrPostProcess(getTheR600Target(),
                                           createAMDGPUInstrPostProcess);

  TargetRegistry::RegisterCustomBehaviour(getTheGCNTarget(),
                                          createAMDGPUCustomBehaviour);
  TargetRegistry::RegisterInstrPostProcess(getTheGCNTarget(),
                                           createAMDGPUInstrPostProcess);
}
