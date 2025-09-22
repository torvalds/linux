//===-- R600MachineScheduler.cpp - R600 Scheduler Interface -*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600 Machine Scheduler interface
//
//===----------------------------------------------------------------------===//

#include "R600MachineScheduler.h"
#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600Subtarget.h"

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

void R600SchedStrategy::initialize(ScheduleDAGMI *dag) {
  assert(dag->hasVRegLiveness() && "R600SchedStrategy needs vreg liveness");
  DAG = static_cast<ScheduleDAGMILive*>(dag);
  const R600Subtarget &ST = DAG->MF.getSubtarget<R600Subtarget>();
  TII = static_cast<const R600InstrInfo*>(DAG->TII);
  TRI = static_cast<const R600RegisterInfo*>(DAG->TRI);
  VLIW5 = !ST.hasCaymanISA();
  MRI = &DAG->MRI;
  CurInstKind = IDOther;
  CurEmitted = 0;
  OccupiedSlotsMask = 31;
  InstKindLimit[IDAlu] = TII->getMaxAlusPerClause();
  InstKindLimit[IDOther] = 32;
  InstKindLimit[IDFetch] = ST.getTexVTXClauseSize();
  AluInstCount = 0;
  FetchInstCount = 0;
}

void R600SchedStrategy::MoveUnits(std::vector<SUnit *> &QSrc,
                                  std::vector<SUnit *> &QDst)
{
  llvm::append_range(QDst, QSrc);
  QSrc.clear();
}

static unsigned getWFCountLimitedByGPR(unsigned GPRCount) {
  assert (GPRCount && "GPRCount cannot be 0");
  return 248 / GPRCount;
}

SUnit* R600SchedStrategy::pickNode(bool &IsTopNode) {
  SUnit *SU = nullptr;
  NextInstKind = IDOther;

  IsTopNode = false;

  // check if we might want to switch current clause type
  bool AllowSwitchToAlu = (CurEmitted >= InstKindLimit[CurInstKind]) ||
      (Available[CurInstKind].empty());
  bool AllowSwitchFromAlu = (CurEmitted >= InstKindLimit[CurInstKind]) &&
      (!Available[IDFetch].empty() || !Available[IDOther].empty());

  if (CurInstKind == IDAlu && !Available[IDFetch].empty()) {
    // We use the heuristic provided by AMD Accelerated Parallel Processing
    // OpenCL Programming Guide :
    // The approx. number of WF that allows TEX inst to hide ALU inst is :
    // 500 (cycles for TEX) / (AluFetchRatio * 8 (cycles for ALU))
    float ALUFetchRationEstimate =
        (AluInstCount + AvailablesAluCount() + Pending[IDAlu].size()) /
        (FetchInstCount + Available[IDFetch].size());
    if (ALUFetchRationEstimate == 0) {
      AllowSwitchFromAlu = true;
    } else {
      unsigned NeededWF = 62.5f / ALUFetchRationEstimate;
      LLVM_DEBUG(dbgs() << NeededWF << " approx. Wavefronts Required\n");
      // We assume the local GPR requirements to be "dominated" by the requirement
      // of the TEX clause (which consumes 128 bits regs) ; ALU inst before and
      // after TEX are indeed likely to consume or generate values from/for the
      // TEX clause.
      // Available[IDFetch].size() * 2 : GPRs required in the Fetch clause
      // We assume that fetch instructions are either TnXYZW = TEX TnXYZW (need
      // one GPR) or TmXYZW = TnXYZW (need 2 GPR).
      // (TODO : use RegisterPressure)
      // If we are going too use too many GPR, we flush Fetch instruction to lower
      // register pressure on 128 bits regs.
      unsigned NearRegisterRequirement = 2 * Available[IDFetch].size();
      if (NeededWF > getWFCountLimitedByGPR(NearRegisterRequirement))
        AllowSwitchFromAlu = true;
    }
  }

  if (!SU && ((AllowSwitchToAlu && CurInstKind != IDAlu) ||
      (!AllowSwitchFromAlu && CurInstKind == IDAlu))) {
    // try to pick ALU
    SU = pickAlu();
    if (!SU && !PhysicalRegCopy.empty()) {
      SU = PhysicalRegCopy.front();
      PhysicalRegCopy.erase(PhysicalRegCopy.begin());
    }
    if (SU) {
      if (CurEmitted >= InstKindLimit[IDAlu])
        CurEmitted = 0;
      NextInstKind = IDAlu;
    }
  }

  if (!SU) {
    // try to pick FETCH
    SU = pickOther(IDFetch);
    if (SU)
      NextInstKind = IDFetch;
  }

  // try to pick other
  if (!SU) {
    SU = pickOther(IDOther);
    if (SU)
      NextInstKind = IDOther;
  }

  LLVM_DEBUG(if (SU) {
    dbgs() << " ** Pick node **\n";
    DAG->dumpNode(*SU);
  } else {
    dbgs() << "NO NODE \n";
    for (const SUnit &S : DAG->SUnits)
      if (!S.isScheduled)
        DAG->dumpNode(S);
  });

  return SU;
}

void R600SchedStrategy::schedNode(SUnit *SU, bool IsTopNode) {
  if (NextInstKind != CurInstKind) {
    LLVM_DEBUG(dbgs() << "Instruction Type Switch\n");
    if (NextInstKind != IDAlu)
      OccupiedSlotsMask |= 31;
    CurEmitted = 0;
    CurInstKind = NextInstKind;
  }

  if (CurInstKind == IDAlu) {
    AluInstCount ++;
    switch (getAluKind(SU)) {
    case AluT_XYZW:
      CurEmitted += 4;
      break;
    case AluDiscarded:
      break;
    default: {
      ++CurEmitted;
      for (MachineInstr::mop_iterator It = SU->getInstr()->operands_begin(),
          E = SU->getInstr()->operands_end(); It != E; ++It) {
        MachineOperand &MO = *It;
        if (MO.isReg() && MO.getReg() == R600::ALU_LITERAL_X)
          ++CurEmitted;
      }
    }
    }
  } else {
    ++CurEmitted;
  }

  LLVM_DEBUG(dbgs() << CurEmitted << " Instructions Emitted in this clause\n");

  if (CurInstKind != IDFetch) {
    MoveUnits(Pending[IDFetch], Available[IDFetch]);
  } else
    FetchInstCount++;
}

static bool
isPhysicalRegCopy(MachineInstr *MI) {
  if (MI->getOpcode() != R600::COPY)
    return false;

  return !MI->getOperand(1).getReg().isVirtual();
}

void R600SchedStrategy::releaseTopNode(SUnit *SU) {
  LLVM_DEBUG(dbgs() << "Top Releasing "; DAG->dumpNode(*SU));
}

void R600SchedStrategy::releaseBottomNode(SUnit *SU) {
  LLVM_DEBUG(dbgs() << "Bottom Releasing "; DAG->dumpNode(*SU));
  if (isPhysicalRegCopy(SU->getInstr())) {
    PhysicalRegCopy.push_back(SU);
    return;
  }

  int IK = getInstKind(SU);

  // There is no export clause, we can schedule one as soon as its ready
  if (IK == IDOther)
    Available[IDOther].push_back(SU);
  else
    Pending[IK].push_back(SU);

}

bool R600SchedStrategy::regBelongsToClass(Register Reg,
                                          const TargetRegisterClass *RC) const {
  if (!Reg.isVirtual())
    return RC->contains(Reg);
  return MRI->getRegClass(Reg) == RC;
}

R600SchedStrategy::AluKind R600SchedStrategy::getAluKind(SUnit *SU) const {
  MachineInstr *MI = SU->getInstr();

  if (TII->isTransOnly(*MI))
    return AluTrans;

  switch (MI->getOpcode()) {
  case R600::PRED_X:
    return AluPredX;
  case R600::INTERP_PAIR_XY:
  case R600::INTERP_PAIR_ZW:
  case R600::INTERP_VEC_LOAD:
  case R600::DOT_4:
    return AluT_XYZW;
  case R600::COPY:
    if (MI->getOperand(1).isUndef()) {
      // MI will become a KILL, don't considers it in scheduling
      return AluDiscarded;
    }
    break;
  default:
    break;
  }

  // Does the instruction take a whole IG ?
  // XXX: Is it possible to add a helper function in R600InstrInfo that can
  // be used here and in R600PacketizerList::isSoloInstruction() ?
  if(TII->isVector(*MI) ||
     TII->isCubeOp(MI->getOpcode()) ||
     TII->isReductionOp(MI->getOpcode()) ||
     MI->getOpcode() == R600::GROUP_BARRIER) {
    return AluT_XYZW;
  }

  if (TII->isLDSInstr(MI->getOpcode())) {
    return AluT_X;
  }

  // Is the result already assigned to a channel ?
  unsigned DestSubReg = MI->getOperand(0).getSubReg();
  switch (DestSubReg) {
  case R600::sub0:
    return AluT_X;
  case R600::sub1:
    return AluT_Y;
  case R600::sub2:
    return AluT_Z;
  case R600::sub3:
    return AluT_W;
  default:
    break;
  }

  // Is the result already member of a X/Y/Z/W class ?
  Register DestReg = MI->getOperand(0).getReg();
  if (regBelongsToClass(DestReg, &R600::R600_TReg32_XRegClass) ||
      regBelongsToClass(DestReg, &R600::R600_AddrRegClass))
    return AluT_X;
  if (regBelongsToClass(DestReg, &R600::R600_TReg32_YRegClass))
    return AluT_Y;
  if (regBelongsToClass(DestReg, &R600::R600_TReg32_ZRegClass))
    return AluT_Z;
  if (regBelongsToClass(DestReg, &R600::R600_TReg32_WRegClass))
    return AluT_W;
  if (regBelongsToClass(DestReg, &R600::R600_Reg128RegClass))
    return AluT_XYZW;

  // LDS src registers cannot be used in the Trans slot.
  if (TII->readsLDSSrcReg(*MI))
    return AluT_XYZW;

  return AluAny;
}

int R600SchedStrategy::getInstKind(SUnit* SU) {
  int Opcode = SU->getInstr()->getOpcode();

  if (TII->usesTextureCache(Opcode) || TII->usesVertexCache(Opcode))
    return IDFetch;

  if (TII->isALUInstr(Opcode)) {
    return IDAlu;
  }

  switch (Opcode) {
  case R600::PRED_X:
  case R600::COPY:
  case R600::CONST_COPY:
  case R600::INTERP_PAIR_XY:
  case R600::INTERP_PAIR_ZW:
  case R600::INTERP_VEC_LOAD:
  case R600::DOT_4:
    return IDAlu;
  default:
    return IDOther;
  }
}

SUnit *R600SchedStrategy::PopInst(std::vector<SUnit *> &Q, bool AnyALU) {
  if (Q.empty())
    return nullptr;
  for (std::vector<SUnit *>::reverse_iterator It = Q.rbegin(), E = Q.rend();
      It != E; ++It) {
    SUnit *SU = *It;
    InstructionsGroupCandidate.push_back(SU->getInstr());
    if (TII->fitsConstReadLimitations(InstructionsGroupCandidate) &&
        (!AnyALU || !TII->isVectorOnly(*SU->getInstr()))) {
      InstructionsGroupCandidate.pop_back();
      Q.erase((It + 1).base());
      return SU;
    }
    InstructionsGroupCandidate.pop_back();
  }
  return nullptr;
}

void R600SchedStrategy::LoadAlu() {
  std::vector<SUnit *> &QSrc = Pending[IDAlu];
  for (SUnit *SU : QSrc) {
    AluKind AK = getAluKind(SU);
    AvailableAlus[AK].push_back(SU);
  }
  QSrc.clear();
}

void R600SchedStrategy::PrepareNextSlot() {
  LLVM_DEBUG(dbgs() << "New Slot\n");
  assert(OccupiedSlotsMask && "Slot wasn't filled");
  OccupiedSlotsMask = 0;
  //  if (HwGen == AMDGPUSubtarget::NORTHERN_ISLANDS)
  //    OccupiedSlotsMask |= 16;
  InstructionsGroupCandidate.clear();
  LoadAlu();
}

void R600SchedStrategy::AssignSlot(MachineInstr* MI, unsigned Slot) {
  int DstIndex = TII->getOperandIdx(MI->getOpcode(), R600::OpName::dst);
  if (DstIndex == -1) {
    return;
  }
  Register DestReg = MI->getOperand(DstIndex).getReg();
  // PressureRegister crashes if an operand is def and used in the same inst
  // and we try to constraint its regclass
  for (MachineInstr::mop_iterator It = MI->operands_begin(),
      E = MI->operands_end(); It != E; ++It) {
    MachineOperand &MO = *It;
    if (MO.isReg() && !MO.isDef() &&
        MO.getReg() == DestReg)
      return;
  }
  // Constrains the regclass of DestReg to assign it to Slot
  switch (Slot) {
  case 0:
    MRI->constrainRegClass(DestReg, &R600::R600_TReg32_XRegClass);
    break;
  case 1:
    MRI->constrainRegClass(DestReg, &R600::R600_TReg32_YRegClass);
    break;
  case 2:
    MRI->constrainRegClass(DestReg, &R600::R600_TReg32_ZRegClass);
    break;
  case 3:
    MRI->constrainRegClass(DestReg, &R600::R600_TReg32_WRegClass);
    break;
  }
}

SUnit *R600SchedStrategy::AttemptFillSlot(unsigned Slot, bool AnyAlu) {
  static const AluKind IndexToID[] = {AluT_X, AluT_Y, AluT_Z, AluT_W};
  SUnit *SlotedSU = PopInst(AvailableAlus[IndexToID[Slot]], AnyAlu);
  if (SlotedSU)
    return SlotedSU;
  SUnit *UnslotedSU = PopInst(AvailableAlus[AluAny], AnyAlu);
  if (UnslotedSU)
    AssignSlot(UnslotedSU->getInstr(), Slot);
  return UnslotedSU;
}

unsigned R600SchedStrategy::AvailablesAluCount() const {
  return AvailableAlus[AluAny].size() + AvailableAlus[AluT_XYZW].size() +
      AvailableAlus[AluT_X].size() + AvailableAlus[AluT_Y].size() +
      AvailableAlus[AluT_Z].size() + AvailableAlus[AluT_W].size() +
      AvailableAlus[AluTrans].size() + AvailableAlus[AluDiscarded].size() +
      AvailableAlus[AluPredX].size();
}

SUnit* R600SchedStrategy::pickAlu() {
  while (AvailablesAluCount() || !Pending[IDAlu].empty()) {
    if (!OccupiedSlotsMask) {
      // Bottom up scheduling : predX must comes first
      if (!AvailableAlus[AluPredX].empty()) {
        OccupiedSlotsMask |= 31;
        return PopInst(AvailableAlus[AluPredX], false);
      }
      // Flush physical reg copies (RA will discard them)
      if (!AvailableAlus[AluDiscarded].empty()) {
        OccupiedSlotsMask |= 31;
        return PopInst(AvailableAlus[AluDiscarded], false);
      }
      // If there is a T_XYZW alu available, use it
      if (!AvailableAlus[AluT_XYZW].empty()) {
        OccupiedSlotsMask |= 15;
        return PopInst(AvailableAlus[AluT_XYZW], false);
      }
    }
    bool TransSlotOccupied = OccupiedSlotsMask & 16;
    if (!TransSlotOccupied && VLIW5) {
      if (!AvailableAlus[AluTrans].empty()) {
        OccupiedSlotsMask |= 16;
        return PopInst(AvailableAlus[AluTrans], false);
      }
      SUnit *SU = AttemptFillSlot(3, true);
      if (SU) {
        OccupiedSlotsMask |= 16;
        return SU;
      }
    }
    for (int Chan = 3; Chan > -1; --Chan) {
      bool isOccupied = OccupiedSlotsMask & (1 << Chan);
      if (!isOccupied) {
        SUnit *SU = AttemptFillSlot(Chan, false);
        if (SU) {
          OccupiedSlotsMask |= (1 << Chan);
          InstructionsGroupCandidate.push_back(SU->getInstr());
          return SU;
        }
      }
    }
    PrepareNextSlot();
  }
  return nullptr;
}

SUnit* R600SchedStrategy::pickOther(int QID) {
  SUnit *SU = nullptr;
  std::vector<SUnit *> &AQ = Available[QID];

  if (AQ.empty()) {
    MoveUnits(Pending[QID], AQ);
  }
  if (!AQ.empty()) {
    SU = AQ.back();
    AQ.pop_back();
  }
  return SU;
}
