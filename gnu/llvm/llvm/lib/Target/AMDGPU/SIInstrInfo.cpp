//===- SIInstrInfo.cpp - SI Instruction Information  ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// SI Implementation of TargetInstrInfo.
//
//===----------------------------------------------------------------------===//

#include "SIInstrInfo.h"
#include "AMDGPU.h"
#include "AMDGPUInstrInfo.h"
#include "GCNHazardRecognizer.h"
#include "GCNSubtarget.h"
#include "SIMachineFunctionInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "si-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "AMDGPUGenInstrInfo.inc"

namespace llvm::AMDGPU {
#define GET_D16ImageDimIntrinsics_IMPL
#define GET_ImageDimIntrinsicTable_IMPL
#define GET_RsrcIntrinsics_IMPL
#include "AMDGPUGenSearchableTables.inc"
} // namespace llvm::AMDGPU

// Must be at least 4 to be able to branch over minimum unconditional branch
// code. This is only for making it possible to write reasonably small tests for
// long branches.
static cl::opt<unsigned>
BranchOffsetBits("amdgpu-s-branch-bits", cl::ReallyHidden, cl::init(16),
                 cl::desc("Restrict range of branch instructions (DEBUG)"));

static cl::opt<bool> Fix16BitCopies(
  "amdgpu-fix-16-bit-physreg-copies",
  cl::desc("Fix copies between 32 and 16 bit registers by extending to 32 bit"),
  cl::init(true),
  cl::ReallyHidden);

SIInstrInfo::SIInstrInfo(const GCNSubtarget &ST)
  : AMDGPUGenInstrInfo(AMDGPU::ADJCALLSTACKUP, AMDGPU::ADJCALLSTACKDOWN),
    RI(ST), ST(ST) {
  SchedModel.init(&ST);
}

//===----------------------------------------------------------------------===//
// TargetInstrInfo callbacks
//===----------------------------------------------------------------------===//

static unsigned getNumOperandsNoGlue(SDNode *Node) {
  unsigned N = Node->getNumOperands();
  while (N && Node->getOperand(N - 1).getValueType() == MVT::Glue)
    --N;
  return N;
}

/// Returns true if both nodes have the same value for the given
///        operand \p Op, or if both nodes do not have this operand.
static bool nodesHaveSameOperandValue(SDNode *N0, SDNode* N1, unsigned OpName) {
  unsigned Opc0 = N0->getMachineOpcode();
  unsigned Opc1 = N1->getMachineOpcode();

  int Op0Idx = AMDGPU::getNamedOperandIdx(Opc0, OpName);
  int Op1Idx = AMDGPU::getNamedOperandIdx(Opc1, OpName);

  if (Op0Idx == -1 && Op1Idx == -1)
    return true;


  if ((Op0Idx == -1 && Op1Idx != -1) ||
      (Op1Idx == -1 && Op0Idx != -1))
    return false;

  // getNamedOperandIdx returns the index for the MachineInstr's operands,
  // which includes the result as the first operand. We are indexing into the
  // MachineSDNode's operands, so we need to skip the result operand to get
  // the real index.
  --Op0Idx;
  --Op1Idx;

  return N0->getOperand(Op0Idx) == N1->getOperand(Op1Idx);
}

static bool canRemat(const MachineInstr &MI) {

  if (SIInstrInfo::isVOP1(MI) || SIInstrInfo::isVOP2(MI) ||
      SIInstrInfo::isVOP3(MI) || SIInstrInfo::isSDWA(MI) ||
      SIInstrInfo::isSALU(MI))
    return true;

  if (SIInstrInfo::isSMRD(MI)) {
    return !MI.memoperands_empty() &&
           llvm::all_of(MI.memoperands(), [](const MachineMemOperand *MMO) {
             return MMO->isLoad() && MMO->isInvariant();
           });
  }

  return false;
}

bool SIInstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {

  if (canRemat(MI)) {
    // Normally VALU use of exec would block the rematerialization, but that
    // is OK in this case to have an implicit exec read as all VALU do.
    // We really want all of the generic logic for this except for this.

    // Another potential implicit use is mode register. The core logic of
    // the RA will not attempt rematerialization if mode is set anywhere
    // in the function, otherwise it is safe since mode is not changed.

    // There is difference to generic method which does not allow
    // rematerialization if there are virtual register uses. We allow this,
    // therefore this method includes SOP instructions as well.
    if (!MI.hasImplicitDef() &&
        MI.getNumImplicitOperands() == MI.getDesc().implicit_uses().size() &&
        !MI.mayRaiseFPException())
      return true;
  }

  return TargetInstrInfo::isReallyTriviallyReMaterializable(MI);
}

// Returns true if the scalar result of a VALU instruction depends on exec.
static bool resultDependsOnExec(const MachineInstr &MI) {
  // Ignore comparisons which are only used masked with exec.
  // This allows some hoisting/sinking of VALU comparisons.
  if (MI.isCompare()) {
    const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
    Register DstReg = MI.getOperand(0).getReg();
    if (!DstReg.isVirtual())
      return true;
    for (MachineInstr &Use : MRI.use_nodbg_instructions(DstReg)) {
      switch (Use.getOpcode()) {
      case AMDGPU::S_AND_SAVEEXEC_B32:
      case AMDGPU::S_AND_SAVEEXEC_B64:
        break;
      case AMDGPU::S_AND_B32:
      case AMDGPU::S_AND_B64:
        if (!Use.readsRegister(AMDGPU::EXEC, /*TRI=*/nullptr))
          return true;
        break;
      default:
        return true;
      }
    }
    return false;
  }

  switch (MI.getOpcode()) {
  default:
    break;
  case AMDGPU::V_READFIRSTLANE_B32:
    return true;
  }

  return false;
}

bool SIInstrInfo::isIgnorableUse(const MachineOperand &MO) const {
  // Any implicit use of exec by VALU is not a real register read.
  return MO.getReg() == AMDGPU::EXEC && MO.isImplicit() &&
         isVALU(*MO.getParent()) && !resultDependsOnExec(*MO.getParent());
}

bool SIInstrInfo::isSafeToSink(MachineInstr &MI,
                               MachineBasicBlock *SuccToSinkTo,
                               MachineCycleInfo *CI) const {
  // Allow sinking if MI edits lane mask (divergent i1 in sgpr).
  if (MI.getOpcode() == AMDGPU::SI_IF_BREAK)
    return true;

  MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  // Check if sinking of MI would create temporal divergent use.
  for (auto Op : MI.uses()) {
    if (Op.isReg() && Op.getReg().isVirtual() &&
        RI.isSGPRClass(MRI.getRegClass(Op.getReg()))) {
      MachineInstr *SgprDef = MRI.getVRegDef(Op.getReg());

      // SgprDef defined inside cycle
      MachineCycle *FromCycle = CI->getCycle(SgprDef->getParent());
      if (FromCycle == nullptr)
        continue;

      MachineCycle *ToCycle = CI->getCycle(SuccToSinkTo);
      // Check if there is a FromCycle that contains SgprDef's basic block but
      // does not contain SuccToSinkTo and also has divergent exit condition.
      while (FromCycle && !FromCycle->contains(ToCycle)) {
        SmallVector<MachineBasicBlock *, 1> ExitingBlocks;
        FromCycle->getExitingBlocks(ExitingBlocks);

        // FromCycle has divergent exit condition.
        for (MachineBasicBlock *ExitingBlock : ExitingBlocks) {
          if (hasDivergentBranch(ExitingBlock))
            return false;
        }

        FromCycle = FromCycle->getParentCycle();
      }
    }
  }

  return true;
}

bool SIInstrInfo::areLoadsFromSameBasePtr(SDNode *Load0, SDNode *Load1,
                                          int64_t &Offset0,
                                          int64_t &Offset1) const {
  if (!Load0->isMachineOpcode() || !Load1->isMachineOpcode())
    return false;

  unsigned Opc0 = Load0->getMachineOpcode();
  unsigned Opc1 = Load1->getMachineOpcode();

  // Make sure both are actually loads.
  if (!get(Opc0).mayLoad() || !get(Opc1).mayLoad())
    return false;

  // A mayLoad instruction without a def is not a load. Likely a prefetch.
  if (!get(Opc0).getNumDefs() || !get(Opc1).getNumDefs())
    return false;

  if (isDS(Opc0) && isDS(Opc1)) {

    // FIXME: Handle this case:
    if (getNumOperandsNoGlue(Load0) != getNumOperandsNoGlue(Load1))
      return false;

    // Check base reg.
    if (Load0->getOperand(0) != Load1->getOperand(0))
      return false;

    // Skip read2 / write2 variants for simplicity.
    // TODO: We should report true if the used offsets are adjacent (excluded
    // st64 versions).
    int Offset0Idx = AMDGPU::getNamedOperandIdx(Opc0, AMDGPU::OpName::offset);
    int Offset1Idx = AMDGPU::getNamedOperandIdx(Opc1, AMDGPU::OpName::offset);
    if (Offset0Idx == -1 || Offset1Idx == -1)
      return false;

    // XXX - be careful of dataless loads
    // getNamedOperandIdx returns the index for MachineInstrs.  Since they
    // include the output in the operand list, but SDNodes don't, we need to
    // subtract the index by one.
    Offset0Idx -= get(Opc0).NumDefs;
    Offset1Idx -= get(Opc1).NumDefs;
    Offset0 = Load0->getConstantOperandVal(Offset0Idx);
    Offset1 = Load1->getConstantOperandVal(Offset1Idx);
    return true;
  }

  if (isSMRD(Opc0) && isSMRD(Opc1)) {
    // Skip time and cache invalidation instructions.
    if (!AMDGPU::hasNamedOperand(Opc0, AMDGPU::OpName::sbase) ||
        !AMDGPU::hasNamedOperand(Opc1, AMDGPU::OpName::sbase))
      return false;

    unsigned NumOps = getNumOperandsNoGlue(Load0);
    if (NumOps != getNumOperandsNoGlue(Load1))
      return false;

    // Check base reg.
    if (Load0->getOperand(0) != Load1->getOperand(0))
      return false;

    // Match register offsets, if both register and immediate offsets present.
    assert(NumOps == 4 || NumOps == 5);
    if (NumOps == 5 && Load0->getOperand(1) != Load1->getOperand(1))
      return false;

    const ConstantSDNode *Load0Offset =
        dyn_cast<ConstantSDNode>(Load0->getOperand(NumOps - 3));
    const ConstantSDNode *Load1Offset =
        dyn_cast<ConstantSDNode>(Load1->getOperand(NumOps - 3));

    if (!Load0Offset || !Load1Offset)
      return false;

    Offset0 = Load0Offset->getZExtValue();
    Offset1 = Load1Offset->getZExtValue();
    return true;
  }

  // MUBUF and MTBUF can access the same addresses.
  if ((isMUBUF(Opc0) || isMTBUF(Opc0)) && (isMUBUF(Opc1) || isMTBUF(Opc1))) {

    // MUBUF and MTBUF have vaddr at different indices.
    if (!nodesHaveSameOperandValue(Load0, Load1, AMDGPU::OpName::soffset) ||
        !nodesHaveSameOperandValue(Load0, Load1, AMDGPU::OpName::vaddr) ||
        !nodesHaveSameOperandValue(Load0, Load1, AMDGPU::OpName::srsrc))
      return false;

    int OffIdx0 = AMDGPU::getNamedOperandIdx(Opc0, AMDGPU::OpName::offset);
    int OffIdx1 = AMDGPU::getNamedOperandIdx(Opc1, AMDGPU::OpName::offset);

    if (OffIdx0 == -1 || OffIdx1 == -1)
      return false;

    // getNamedOperandIdx returns the index for MachineInstrs.  Since they
    // include the output in the operand list, but SDNodes don't, we need to
    // subtract the index by one.
    OffIdx0 -= get(Opc0).NumDefs;
    OffIdx1 -= get(Opc1).NumDefs;

    SDValue Off0 = Load0->getOperand(OffIdx0);
    SDValue Off1 = Load1->getOperand(OffIdx1);

    // The offset might be a FrameIndexSDNode.
    if (!isa<ConstantSDNode>(Off0) || !isa<ConstantSDNode>(Off1))
      return false;

    Offset0 = Off0->getAsZExtVal();
    Offset1 = Off1->getAsZExtVal();
    return true;
  }

  return false;
}

static bool isStride64(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::DS_READ2ST64_B32:
  case AMDGPU::DS_READ2ST64_B64:
  case AMDGPU::DS_WRITE2ST64_B32:
  case AMDGPU::DS_WRITE2ST64_B64:
    return true;
  default:
    return false;
  }
}

bool SIInstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &LdSt, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo *TRI) const {
  if (!LdSt.mayLoadOrStore())
    return false;

  unsigned Opc = LdSt.getOpcode();
  OffsetIsScalable = false;
  const MachineOperand *BaseOp, *OffsetOp;
  int DataOpIdx;

  if (isDS(LdSt)) {
    BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::addr);
    OffsetOp = getNamedOperand(LdSt, AMDGPU::OpName::offset);
    if (OffsetOp) {
      // Normal, single offset LDS instruction.
      if (!BaseOp) {
        // DS_CONSUME/DS_APPEND use M0 for the base address.
        // TODO: find the implicit use operand for M0 and use that as BaseOp?
        return false;
      }
      BaseOps.push_back(BaseOp);
      Offset = OffsetOp->getImm();
      // Get appropriate operand, and compute width accordingly.
      DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst);
      if (DataOpIdx == -1)
        DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::data0);
      Width = getOpSize(LdSt, DataOpIdx);
    } else {
      // The 2 offset instructions use offset0 and offset1 instead. We can treat
      // these as a load with a single offset if the 2 offsets are consecutive.
      // We will use this for some partially aligned loads.
      const MachineOperand *Offset0Op =
          getNamedOperand(LdSt, AMDGPU::OpName::offset0);
      const MachineOperand *Offset1Op =
          getNamedOperand(LdSt, AMDGPU::OpName::offset1);

      unsigned Offset0 = Offset0Op->getImm() & 0xff;
      unsigned Offset1 = Offset1Op->getImm() & 0xff;
      if (Offset0 + 1 != Offset1)
        return false;

      // Each of these offsets is in element sized units, so we need to convert
      // to bytes of the individual reads.

      unsigned EltSize;
      if (LdSt.mayLoad())
        EltSize = TRI->getRegSizeInBits(*getOpRegClass(LdSt, 0)) / 16;
      else {
        assert(LdSt.mayStore());
        int Data0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::data0);
        EltSize = TRI->getRegSizeInBits(*getOpRegClass(LdSt, Data0Idx)) / 8;
      }

      if (isStride64(Opc))
        EltSize *= 64;

      BaseOps.push_back(BaseOp);
      Offset = EltSize * Offset0;
      // Get appropriate operand(s), and compute width accordingly.
      DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst);
      if (DataOpIdx == -1) {
        DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::data0);
        Width = getOpSize(LdSt, DataOpIdx);
        DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::data1);
        Width = Width.getValue() + getOpSize(LdSt, DataOpIdx);
      } else {
        Width = getOpSize(LdSt, DataOpIdx);
      }
    }
    return true;
  }

  if (isMUBUF(LdSt) || isMTBUF(LdSt)) {
    const MachineOperand *RSrc = getNamedOperand(LdSt, AMDGPU::OpName::srsrc);
    if (!RSrc) // e.g. BUFFER_WBINVL1_VOL
      return false;
    BaseOps.push_back(RSrc);
    BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::vaddr);
    if (BaseOp && !BaseOp->isFI())
      BaseOps.push_back(BaseOp);
    const MachineOperand *OffsetImm =
        getNamedOperand(LdSt, AMDGPU::OpName::offset);
    Offset = OffsetImm->getImm();
    const MachineOperand *SOffset =
        getNamedOperand(LdSt, AMDGPU::OpName::soffset);
    if (SOffset) {
      if (SOffset->isReg())
        BaseOps.push_back(SOffset);
      else
        Offset += SOffset->getImm();
    }
    // Get appropriate operand, and compute width accordingly.
    DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst);
    if (DataOpIdx == -1)
      DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdata);
    if (DataOpIdx == -1) // LDS DMA
      return false;
    Width = getOpSize(LdSt, DataOpIdx);
    return true;
  }

  if (isImage(LdSt)) {
    auto RsrcOpName =
        isMIMG(LdSt) ? AMDGPU::OpName::srsrc : AMDGPU::OpName::rsrc;
    int SRsrcIdx = AMDGPU::getNamedOperandIdx(Opc, RsrcOpName);
    BaseOps.push_back(&LdSt.getOperand(SRsrcIdx));
    int VAddr0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vaddr0);
    if (VAddr0Idx >= 0) {
      // GFX10 possible NSA encoding.
      for (int I = VAddr0Idx; I < SRsrcIdx; ++I)
        BaseOps.push_back(&LdSt.getOperand(I));
    } else {
      BaseOps.push_back(getNamedOperand(LdSt, AMDGPU::OpName::vaddr));
    }
    Offset = 0;
    // Get appropriate operand, and compute width accordingly.
    DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdata);
    if (DataOpIdx == -1)
      return false; // no return sampler
    Width = getOpSize(LdSt, DataOpIdx);
    return true;
  }

  if (isSMRD(LdSt)) {
    BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::sbase);
    if (!BaseOp) // e.g. S_MEMTIME
      return false;
    BaseOps.push_back(BaseOp);
    OffsetOp = getNamedOperand(LdSt, AMDGPU::OpName::offset);
    Offset = OffsetOp ? OffsetOp->getImm() : 0;
    // Get appropriate operand, and compute width accordingly.
    DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::sdst);
    if (DataOpIdx == -1)
      return false;
    Width = getOpSize(LdSt, DataOpIdx);
    return true;
  }

  if (isFLAT(LdSt)) {
    // Instructions have either vaddr or saddr or both or none.
    BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::vaddr);
    if (BaseOp)
      BaseOps.push_back(BaseOp);
    BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::saddr);
    if (BaseOp)
      BaseOps.push_back(BaseOp);
    Offset = getNamedOperand(LdSt, AMDGPU::OpName::offset)->getImm();
    // Get appropriate operand, and compute width accordingly.
    DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst);
    if (DataOpIdx == -1)
      DataOpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdata);
    if (DataOpIdx == -1) // LDS DMA
      return false;
    Width = getOpSize(LdSt, DataOpIdx);
    return true;
  }

  return false;
}

static bool memOpsHaveSameBasePtr(const MachineInstr &MI1,
                                  ArrayRef<const MachineOperand *> BaseOps1,
                                  const MachineInstr &MI2,
                                  ArrayRef<const MachineOperand *> BaseOps2) {
  // Only examine the first "base" operand of each instruction, on the
  // assumption that it represents the real base address of the memory access.
  // Other operands are typically offsets or indices from this base address.
  if (BaseOps1.front()->isIdenticalTo(*BaseOps2.front()))
    return true;

  if (!MI1.hasOneMemOperand() || !MI2.hasOneMemOperand())
    return false;

  auto MO1 = *MI1.memoperands_begin();
  auto MO2 = *MI2.memoperands_begin();
  if (MO1->getAddrSpace() != MO2->getAddrSpace())
    return false;

  auto Base1 = MO1->getValue();
  auto Base2 = MO2->getValue();
  if (!Base1 || !Base2)
    return false;
  Base1 = getUnderlyingObject(Base1);
  Base2 = getUnderlyingObject(Base2);

  if (isa<UndefValue>(Base1) || isa<UndefValue>(Base2))
    return false;

  return Base1 == Base2;
}

bool SIInstrInfo::shouldClusterMemOps(ArrayRef<const MachineOperand *> BaseOps1,
                                      int64_t Offset1, bool OffsetIsScalable1,
                                      ArrayRef<const MachineOperand *> BaseOps2,
                                      int64_t Offset2, bool OffsetIsScalable2,
                                      unsigned ClusterSize,
                                      unsigned NumBytes) const {
  // If the mem ops (to be clustered) do not have the same base ptr, then they
  // should not be clustered
  if (!BaseOps1.empty() && !BaseOps2.empty()) {
    const MachineInstr &FirstLdSt = *BaseOps1.front()->getParent();
    const MachineInstr &SecondLdSt = *BaseOps2.front()->getParent();
    if (!memOpsHaveSameBasePtr(FirstLdSt, BaseOps1, SecondLdSt, BaseOps2))
      return false;
  } else if (!BaseOps1.empty() || !BaseOps2.empty()) {
    // If only one base op is empty, they do not have the same base ptr
    return false;
  }

  // In order to avoid register pressure, on an average, the number of DWORDS
  // loaded together by all clustered mem ops should not exceed 8. This is an
  // empirical value based on certain observations and performance related
  // experiments.
  // The good thing about this heuristic is - it avoids clustering of too many
  // sub-word loads, and also avoids clustering of wide loads. Below is the
  // brief summary of how the heuristic behaves for various `LoadSize`.
  // (1) 1 <= LoadSize <= 4: cluster at max 8 mem ops
  // (2) 5 <= LoadSize <= 8: cluster at max 4 mem ops
  // (3) 9 <= LoadSize <= 12: cluster at max 2 mem ops
  // (4) 13 <= LoadSize <= 16: cluster at max 2 mem ops
  // (5) LoadSize >= 17: do not cluster
  const unsigned LoadSize = NumBytes / ClusterSize;
  const unsigned NumDWORDs = ((LoadSize + 3) / 4) * ClusterSize;
  return NumDWORDs <= 8;
}

// FIXME: This behaves strangely. If, for example, you have 32 load + stores,
// the first 16 loads will be interleaved with the stores, and the next 16 will
// be clustered as expected. It should really split into 2 16 store batches.
//
// Loads are clustered until this returns false, rather than trying to schedule
// groups of stores. This also means we have to deal with saying different
// address space loads should be clustered, and ones which might cause bank
// conflicts.
//
// This might be deprecated so it might not be worth that much effort to fix.
bool SIInstrInfo::shouldScheduleLoadsNear(SDNode *Load0, SDNode *Load1,
                                          int64_t Offset0, int64_t Offset1,
                                          unsigned NumLoads) const {
  assert(Offset1 > Offset0 &&
         "Second offset should be larger than first offset!");
  // If we have less than 16 loads in a row, and the offsets are within 64
  // bytes, then schedule together.

  // A cacheline is 64 bytes (for global memory).
  return (NumLoads <= 16 && (Offset1 - Offset0) < 64);
}

static void reportIllegalCopy(const SIInstrInfo *TII, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              const DebugLoc &DL, MCRegister DestReg,
                              MCRegister SrcReg, bool KillSrc,
                              const char *Msg = "illegal VGPR to SGPR copy") {
  MachineFunction *MF = MBB.getParent();
  DiagnosticInfoUnsupported IllegalCopy(MF->getFunction(), Msg, DL, DS_Error);
  LLVMContext &C = MF->getFunction().getContext();
  C.diagnose(IllegalCopy);

  BuildMI(MBB, MI, DL, TII->get(AMDGPU::SI_ILLEGAL_COPY), DestReg)
    .addReg(SrcReg, getKillRegState(KillSrc));
}

/// Handle copying from SGPR to AGPR, or from AGPR to AGPR on GFX908. It is not
/// possible to have a direct copy in these cases on GFX908, so an intermediate
/// VGPR copy is required.
static void indirectCopyToAGPR(const SIInstrInfo &TII,
                               MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, MCRegister DestReg,
                               MCRegister SrcReg, bool KillSrc,
                               RegScavenger &RS, bool RegsOverlap,
                               Register ImpDefSuperReg = Register(),
                               Register ImpUseSuperReg = Register()) {
  assert((TII.getSubtarget().hasMAIInsts() &&
          !TII.getSubtarget().hasGFX90AInsts()) &&
         "Expected GFX908 subtarget.");

  assert((AMDGPU::SReg_32RegClass.contains(SrcReg) ||
          AMDGPU::AGPR_32RegClass.contains(SrcReg)) &&
         "Source register of the copy should be either an SGPR or an AGPR.");

  assert(AMDGPU::AGPR_32RegClass.contains(DestReg) &&
         "Destination register of the copy should be an AGPR.");

  const SIRegisterInfo &RI = TII.getRegisterInfo();

  // First try to find defining accvgpr_write to avoid temporary registers.
  // In the case of copies of overlapping AGPRs, we conservatively do not
  // reuse previous accvgpr_writes. Otherwise, we may incorrectly pick up
  // an accvgpr_write used for this same copy due to implicit-defs
  if (!RegsOverlap) {
    for (auto Def = MI, E = MBB.begin(); Def != E; ) {
      --Def;

      if (!Def->modifiesRegister(SrcReg, &RI))
        continue;

      if (Def->getOpcode() != AMDGPU::V_ACCVGPR_WRITE_B32_e64 ||
          Def->getOperand(0).getReg() != SrcReg)
        break;

      MachineOperand &DefOp = Def->getOperand(1);
      assert(DefOp.isReg() || DefOp.isImm());

      if (DefOp.isReg()) {
        bool SafeToPropagate = true;
        // Check that register source operand is not clobbered before MI.
        // Immediate operands are always safe to propagate.
        for (auto I = Def; I != MI && SafeToPropagate; ++I)
          if (I->modifiesRegister(DefOp.getReg(), &RI))
            SafeToPropagate = false;

        if (!SafeToPropagate)
          break;

        DefOp.setIsKill(false);
      }

      MachineInstrBuilder Builder =
        BuildMI(MBB, MI, DL, TII.get(AMDGPU::V_ACCVGPR_WRITE_B32_e64), DestReg)
        .add(DefOp);
      if (ImpDefSuperReg)
        Builder.addReg(ImpDefSuperReg, RegState::Define | RegState::Implicit);

      if (ImpUseSuperReg) {
        Builder.addReg(ImpUseSuperReg,
                      getKillRegState(KillSrc) | RegState::Implicit);
      }

      return;
    }
  }

  RS.enterBasicBlockEnd(MBB);
  RS.backward(std::next(MI));

  // Ideally we want to have three registers for a long reg_sequence copy
  // to hide 2 waitstates between v_mov_b32 and accvgpr_write.
  unsigned MaxVGPRs = RI.getRegPressureLimit(&AMDGPU::VGPR_32RegClass,
                                             *MBB.getParent());

  // Registers in the sequence are allocated contiguously so we can just
  // use register number to pick one of three round-robin temps.
  unsigned RegNo = (DestReg - AMDGPU::AGPR0) % 3;
  Register Tmp =
      MBB.getParent()->getInfo<SIMachineFunctionInfo>()->getVGPRForAGPRCopy();
  assert(MBB.getParent()->getRegInfo().isReserved(Tmp) &&
         "VGPR used for an intermediate copy should have been reserved.");

  // Only loop through if there are any free registers left. We don't want to
  // spill.
  while (RegNo--) {
    Register Tmp2 = RS.scavengeRegisterBackwards(AMDGPU::VGPR_32RegClass, MI,
                                                 /* RestoreAfter */ false, 0,
                                                 /* AllowSpill */ false);
    if (!Tmp2 || RI.getHWRegIndex(Tmp2) >= MaxVGPRs)
      break;
    Tmp = Tmp2;
    RS.setRegUsed(Tmp);
  }

  // Insert copy to temporary VGPR.
  unsigned TmpCopyOp = AMDGPU::V_MOV_B32_e32;
  if (AMDGPU::AGPR_32RegClass.contains(SrcReg)) {
    TmpCopyOp = AMDGPU::V_ACCVGPR_READ_B32_e64;
  } else {
    assert(AMDGPU::SReg_32RegClass.contains(SrcReg));
  }

  MachineInstrBuilder UseBuilder = BuildMI(MBB, MI, DL, TII.get(TmpCopyOp), Tmp)
    .addReg(SrcReg, getKillRegState(KillSrc));
  if (ImpUseSuperReg) {
    UseBuilder.addReg(ImpUseSuperReg,
                      getKillRegState(KillSrc) | RegState::Implicit);
  }

  MachineInstrBuilder DefBuilder
    = BuildMI(MBB, MI, DL, TII.get(AMDGPU::V_ACCVGPR_WRITE_B32_e64), DestReg)
    .addReg(Tmp, RegState::Kill);

  if (ImpDefSuperReg)
    DefBuilder.addReg(ImpDefSuperReg, RegState::Define | RegState::Implicit);
}

static void expandSGPRCopy(const SIInstrInfo &TII, MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, const DebugLoc &DL,
                           MCRegister DestReg, MCRegister SrcReg, bool KillSrc,
                           const TargetRegisterClass *RC, bool Forward) {
  const SIRegisterInfo &RI = TII.getRegisterInfo();
  ArrayRef<int16_t> BaseIndices = RI.getRegSplitParts(RC, 4);
  MachineBasicBlock::iterator I = MI;
  MachineInstr *FirstMI = nullptr, *LastMI = nullptr;

  for (unsigned Idx = 0; Idx < BaseIndices.size(); ++Idx) {
    int16_t SubIdx = BaseIndices[Idx];
    Register DestSubReg = RI.getSubReg(DestReg, SubIdx);
    Register SrcSubReg = RI.getSubReg(SrcReg, SubIdx);
    assert(DestSubReg && SrcSubReg && "Failed to find subregs!");
    unsigned Opcode = AMDGPU::S_MOV_B32;

    // Is SGPR aligned? If so try to combine with next.
    bool AlignedDest = ((DestSubReg - AMDGPU::SGPR0) % 2) == 0;
    bool AlignedSrc = ((SrcSubReg - AMDGPU::SGPR0) % 2) == 0;
    if (AlignedDest && AlignedSrc && (Idx + 1 < BaseIndices.size())) {
      // Can use SGPR64 copy
      unsigned Channel = RI.getChannelFromSubReg(SubIdx);
      SubIdx = RI.getSubRegFromChannel(Channel, 2);
      DestSubReg = RI.getSubReg(DestReg, SubIdx);
      SrcSubReg = RI.getSubReg(SrcReg, SubIdx);
      assert(DestSubReg && SrcSubReg && "Failed to find subregs!");
      Opcode = AMDGPU::S_MOV_B64;
      Idx++;
    }

    LastMI = BuildMI(MBB, I, DL, TII.get(Opcode), DestSubReg)
                 .addReg(SrcSubReg)
                 .addReg(SrcReg, RegState::Implicit);

    if (!FirstMI)
      FirstMI = LastMI;

    if (!Forward)
      I--;
  }

  assert(FirstMI && LastMI);
  if (!Forward)
    std::swap(FirstMI, LastMI);

  FirstMI->addOperand(
      MachineOperand::CreateReg(DestReg, true /*IsDef*/, true /*IsImp*/));

  if (KillSrc)
    LastMI->addRegisterKilled(SrcReg, &RI);
}

void SIInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              const DebugLoc &DL, MCRegister DestReg,
                              MCRegister SrcReg, bool KillSrc) const {
  const TargetRegisterClass *RC = RI.getPhysRegBaseClass(DestReg);
  unsigned Size = RI.getRegSizeInBits(*RC);
  const TargetRegisterClass *SrcRC = RI.getPhysRegBaseClass(SrcReg);
  unsigned SrcSize = RI.getRegSizeInBits(*SrcRC);

  // The rest of copyPhysReg assumes Src and Dst size are the same size.
  // TODO-GFX11_16BIT If all true 16 bit instruction patterns are completed can
  // we remove Fix16BitCopies and this code block?
  if (Fix16BitCopies) {
    if (((Size == 16) != (SrcSize == 16))) {
      // Non-VGPR Src and Dst will later be expanded back to 32 bits.
      assert(ST.hasTrue16BitInsts());
      MCRegister &RegToFix = (Size == 32) ? DestReg : SrcReg;
      MCRegister SubReg = RI.getSubReg(RegToFix, AMDGPU::lo16);
      RegToFix = SubReg;

      if (DestReg == SrcReg) {
        // Identity copy. Insert empty bundle since ExpandPostRA expects an
        // instruction here.
        BuildMI(MBB, MI, DL, get(AMDGPU::BUNDLE));
        return;
      }
      RC = RI.getPhysRegBaseClass(DestReg);
      Size = RI.getRegSizeInBits(*RC);
      SrcRC = RI.getPhysRegBaseClass(SrcReg);
      SrcSize = RI.getRegSizeInBits(*SrcRC);
    }
  }

  if (RC == &AMDGPU::VGPR_32RegClass) {
    assert(AMDGPU::VGPR_32RegClass.contains(SrcReg) ||
           AMDGPU::SReg_32RegClass.contains(SrcReg) ||
           AMDGPU::AGPR_32RegClass.contains(SrcReg));
    unsigned Opc = AMDGPU::AGPR_32RegClass.contains(SrcReg) ?
                     AMDGPU::V_ACCVGPR_READ_B32_e64 : AMDGPU::V_MOV_B32_e32;
    BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (RC == &AMDGPU::SReg_32_XM0RegClass ||
      RC == &AMDGPU::SReg_32RegClass) {
    if (SrcReg == AMDGPU::SCC) {
      BuildMI(MBB, MI, DL, get(AMDGPU::S_CSELECT_B32), DestReg)
          .addImm(1)
          .addImm(0);
      return;
    }

    if (DestReg == AMDGPU::VCC_LO) {
      if (AMDGPU::SReg_32RegClass.contains(SrcReg)) {
        BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), AMDGPU::VCC_LO)
          .addReg(SrcReg, getKillRegState(KillSrc));
      } else {
        // FIXME: Hack until VReg_1 removed.
        assert(AMDGPU::VGPR_32RegClass.contains(SrcReg));
        BuildMI(MBB, MI, DL, get(AMDGPU::V_CMP_NE_U32_e32))
          .addImm(0)
          .addReg(SrcReg, getKillRegState(KillSrc));
      }

      return;
    }

    if (!AMDGPU::SReg_32RegClass.contains(SrcReg)) {
      reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
      return;
    }

    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), DestReg)
            .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (RC == &AMDGPU::SReg_64RegClass) {
    if (SrcReg == AMDGPU::SCC) {
      BuildMI(MBB, MI, DL, get(AMDGPU::S_CSELECT_B64), DestReg)
          .addImm(1)
          .addImm(0);
      return;
    }

    if (DestReg == AMDGPU::VCC) {
      if (AMDGPU::SReg_64RegClass.contains(SrcReg)) {
        BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B64), AMDGPU::VCC)
          .addReg(SrcReg, getKillRegState(KillSrc));
      } else {
        // FIXME: Hack until VReg_1 removed.
        assert(AMDGPU::VGPR_32RegClass.contains(SrcReg));
        BuildMI(MBB, MI, DL, get(AMDGPU::V_CMP_NE_U32_e32))
          .addImm(0)
          .addReg(SrcReg, getKillRegState(KillSrc));
      }

      return;
    }

    if (!AMDGPU::SReg_64RegClass.contains(SrcReg)) {
      reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
      return;
    }

    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B64), DestReg)
            .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (DestReg == AMDGPU::SCC) {
    // Copying 64-bit or 32-bit sources to SCC barely makes sense,
    // but SelectionDAG emits such copies for i1 sources.
    if (AMDGPU::SReg_64RegClass.contains(SrcReg)) {
      // This copy can only be produced by patterns
      // with explicit SCC, which are known to be enabled
      // only for subtargets with S_CMP_LG_U64 present.
      assert(ST.hasScalarCompareEq64());
      BuildMI(MBB, MI, DL, get(AMDGPU::S_CMP_LG_U64))
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addImm(0);
    } else {
      assert(AMDGPU::SReg_32RegClass.contains(SrcReg));
      BuildMI(MBB, MI, DL, get(AMDGPU::S_CMP_LG_U32))
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addImm(0);
    }

    return;
  }

  if (RC == &AMDGPU::AGPR_32RegClass) {
    if (AMDGPU::VGPR_32RegClass.contains(SrcReg) ||
        (ST.hasGFX90AInsts() && AMDGPU::SReg_32RegClass.contains(SrcReg))) {
      BuildMI(MBB, MI, DL, get(AMDGPU::V_ACCVGPR_WRITE_B32_e64), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
      return;
    }

    if (AMDGPU::AGPR_32RegClass.contains(SrcReg) && ST.hasGFX90AInsts()) {
      BuildMI(MBB, MI, DL, get(AMDGPU::V_ACCVGPR_MOV_B32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
      return;
    }

    // FIXME: Pass should maintain scavenger to avoid scan through the block on
    // every AGPR spill.
    RegScavenger RS;
    const bool Overlap = RI.regsOverlap(SrcReg, DestReg);
    indirectCopyToAGPR(*this, MBB, MI, DL, DestReg, SrcReg, KillSrc, RS, Overlap);
    return;
  }

  if (Size == 16) {
    assert(AMDGPU::VGPR_16RegClass.contains(SrcReg) ||
           AMDGPU::SReg_LO16RegClass.contains(SrcReg) ||
           AMDGPU::AGPR_LO16RegClass.contains(SrcReg));

    bool IsSGPRDst = AMDGPU::SReg_LO16RegClass.contains(DestReg);
    bool IsSGPRSrc = AMDGPU::SReg_LO16RegClass.contains(SrcReg);
    bool IsAGPRDst = AMDGPU::AGPR_LO16RegClass.contains(DestReg);
    bool IsAGPRSrc = AMDGPU::AGPR_LO16RegClass.contains(SrcReg);
    bool DstLow = !AMDGPU::isHi(DestReg, RI);
    bool SrcLow = !AMDGPU::isHi(SrcReg, RI);
    MCRegister NewDestReg = RI.get32BitRegister(DestReg);
    MCRegister NewSrcReg = RI.get32BitRegister(SrcReg);

    if (IsSGPRDst) {
      if (!IsSGPRSrc) {
        reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
        return;
      }

      BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), NewDestReg)
        .addReg(NewSrcReg, getKillRegState(KillSrc));
      return;
    }

    if (IsAGPRDst || IsAGPRSrc) {
      if (!DstLow || !SrcLow) {
        reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc,
                          "Cannot use hi16 subreg with an AGPR!");
      }

      copyPhysReg(MBB, MI, DL, NewDestReg, NewSrcReg, KillSrc);
      return;
    }

    if (ST.hasTrue16BitInsts()) {
      if (IsSGPRSrc) {
        assert(SrcLow);
        SrcReg = NewSrcReg;
      }
      // Use the smaller instruction encoding if possible.
      if (AMDGPU::VGPR_16_Lo128RegClass.contains(DestReg) &&
          (IsSGPRSrc || AMDGPU::VGPR_16_Lo128RegClass.contains(SrcReg))) {
        BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B16_t16_e32), DestReg)
            .addReg(SrcReg);
      } else {
        BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B16_t16_e64), DestReg)
            .addImm(0) // src0_modifiers
            .addReg(SrcReg)
            .addImm(0); // op_sel
      }
      return;
    }

    if (IsSGPRSrc && !ST.hasSDWAScalar()) {
      if (!DstLow || !SrcLow) {
        reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc,
                          "Cannot use hi16 subreg on VI!");
      }

      BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), NewDestReg)
        .addReg(NewSrcReg, getKillRegState(KillSrc));
      return;
    }

    auto MIB = BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_sdwa), NewDestReg)
      .addImm(0) // src0_modifiers
      .addReg(NewSrcReg)
      .addImm(0) // clamp
      .addImm(DstLow ? AMDGPU::SDWA::SdwaSel::WORD_0
                     : AMDGPU::SDWA::SdwaSel::WORD_1)
      .addImm(AMDGPU::SDWA::DstUnused::UNUSED_PRESERVE)
      .addImm(SrcLow ? AMDGPU::SDWA::SdwaSel::WORD_0
                     : AMDGPU::SDWA::SdwaSel::WORD_1)
      .addReg(NewDestReg, RegState::Implicit | RegState::Undef);
    // First implicit operand is $exec.
    MIB->tieOperands(0, MIB->getNumOperands() - 1);
    return;
  }

  if (RC == RI.getVGPR64Class() && (SrcRC == RC || RI.isSGPRClass(SrcRC))) {
    if (ST.hasMovB64()) {
      BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B64_e32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
      return;
    }
    if (ST.hasPkMovB32()) {
      BuildMI(MBB, MI, DL, get(AMDGPU::V_PK_MOV_B32), DestReg)
        .addImm(SISrcMods::OP_SEL_1)
        .addReg(SrcReg)
        .addImm(SISrcMods::OP_SEL_0 | SISrcMods::OP_SEL_1)
        .addReg(SrcReg)
        .addImm(0) // op_sel_lo
        .addImm(0) // op_sel_hi
        .addImm(0) // neg_lo
        .addImm(0) // neg_hi
        .addImm(0) // clamp
        .addReg(SrcReg, getKillRegState(KillSrc) | RegState::Implicit);
      return;
    }
  }

  const bool Forward = RI.getHWRegIndex(DestReg) <= RI.getHWRegIndex(SrcReg);
  if (RI.isSGPRClass(RC)) {
    if (!RI.isSGPRClass(SrcRC)) {
      reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
      return;
    }
    const bool CanKillSuperReg = KillSrc && !RI.regsOverlap(SrcReg, DestReg);
    expandSGPRCopy(*this, MBB, MI, DL, DestReg, SrcReg, CanKillSuperReg, RC,
                   Forward);
    return;
  }

  unsigned EltSize = 4;
  unsigned Opcode = AMDGPU::V_MOV_B32_e32;
  if (RI.isAGPRClass(RC)) {
    if (ST.hasGFX90AInsts() && RI.isAGPRClass(SrcRC))
      Opcode = AMDGPU::V_ACCVGPR_MOV_B32;
    else if (RI.hasVGPRs(SrcRC) ||
             (ST.hasGFX90AInsts() && RI.isSGPRClass(SrcRC)))
      Opcode = AMDGPU::V_ACCVGPR_WRITE_B32_e64;
    else
      Opcode = AMDGPU::INSTRUCTION_LIST_END;
  } else if (RI.hasVGPRs(RC) && RI.isAGPRClass(SrcRC)) {
    Opcode = AMDGPU::V_ACCVGPR_READ_B32_e64;
  } else if ((Size % 64 == 0) && RI.hasVGPRs(RC) &&
             (RI.isProperlyAlignedRC(*RC) &&
              (SrcRC == RC || RI.isSGPRClass(SrcRC)))) {
    // TODO: In 96-bit case, could do a 64-bit mov and then a 32-bit mov.
    if (ST.hasMovB64()) {
      Opcode = AMDGPU::V_MOV_B64_e32;
      EltSize = 8;
    } else if (ST.hasPkMovB32()) {
      Opcode = AMDGPU::V_PK_MOV_B32;
      EltSize = 8;
    }
  }

  // For the cases where we need an intermediate instruction/temporary register
  // (destination is an AGPR), we need a scavenger.
  //
  // FIXME: The pass should maintain this for us so we don't have to re-scan the
  // whole block for every handled copy.
  std::unique_ptr<RegScavenger> RS;
  if (Opcode == AMDGPU::INSTRUCTION_LIST_END)
    RS = std::make_unique<RegScavenger>();

  ArrayRef<int16_t> SubIndices = RI.getRegSplitParts(RC, EltSize);

  // If there is an overlap, we can't kill the super-register on the last
  // instruction, since it will also kill the components made live by this def.
  const bool Overlap = RI.regsOverlap(SrcReg, DestReg);
  const bool CanKillSuperReg = KillSrc && !Overlap;

  for (unsigned Idx = 0; Idx < SubIndices.size(); ++Idx) {
    unsigned SubIdx;
    if (Forward)
      SubIdx = SubIndices[Idx];
    else
      SubIdx = SubIndices[SubIndices.size() - Idx - 1];
    Register DestSubReg = RI.getSubReg(DestReg, SubIdx);
    Register SrcSubReg = RI.getSubReg(SrcReg, SubIdx);
    assert(DestSubReg && SrcSubReg && "Failed to find subregs!");

    bool IsFirstSubreg = Idx == 0;
    bool UseKill = CanKillSuperReg && Idx == SubIndices.size() - 1;

    if (Opcode == AMDGPU::INSTRUCTION_LIST_END) {
      Register ImpDefSuper = IsFirstSubreg ? Register(DestReg) : Register();
      Register ImpUseSuper = SrcReg;
      indirectCopyToAGPR(*this, MBB, MI, DL, DestSubReg, SrcSubReg, UseKill,
                         *RS, Overlap, ImpDefSuper, ImpUseSuper);
    } else if (Opcode == AMDGPU::V_PK_MOV_B32) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, MI, DL, get(AMDGPU::V_PK_MOV_B32), DestSubReg)
              .addImm(SISrcMods::OP_SEL_1)
              .addReg(SrcSubReg)
              .addImm(SISrcMods::OP_SEL_0 | SISrcMods::OP_SEL_1)
              .addReg(SrcSubReg)
              .addImm(0) // op_sel_lo
              .addImm(0) // op_sel_hi
              .addImm(0) // neg_lo
              .addImm(0) // neg_hi
              .addImm(0) // clamp
              .addReg(SrcReg, getKillRegState(UseKill) | RegState::Implicit);
      if (IsFirstSubreg)
        MIB.addReg(DestReg, RegState::Define | RegState::Implicit);
    } else {
      MachineInstrBuilder Builder =
          BuildMI(MBB, MI, DL, get(Opcode), DestSubReg).addReg(SrcSubReg);
      if (IsFirstSubreg)
        Builder.addReg(DestReg, RegState::Define | RegState::Implicit);

      Builder.addReg(SrcReg, getKillRegState(UseKill) | RegState::Implicit);
    }
  }
}

int SIInstrInfo::commuteOpcode(unsigned Opcode) const {
  int NewOpc;

  // Try to map original to commuted opcode
  NewOpc = AMDGPU::getCommuteRev(Opcode);
  if (NewOpc != -1)
    // Check if the commuted (REV) opcode exists on the target.
    return pseudoToMCOpcode(NewOpc) != -1 ? NewOpc : -1;

  // Try to map commuted to original opcode
  NewOpc = AMDGPU::getCommuteOrig(Opcode);
  if (NewOpc != -1)
    // Check if the original (non-REV) opcode exists on the target.
    return pseudoToMCOpcode(NewOpc) != -1 ? NewOpc : -1;

  return Opcode;
}

void SIInstrInfo::materializeImmediate(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       const DebugLoc &DL, Register DestReg,
                                       int64_t Value) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RegClass = MRI.getRegClass(DestReg);
  if (RegClass == &AMDGPU::SReg_32RegClass ||
      RegClass == &AMDGPU::SGPR_32RegClass ||
      RegClass == &AMDGPU::SReg_32_XM0RegClass ||
      RegClass == &AMDGPU::SReg_32_XM0_XEXECRegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), DestReg)
      .addImm(Value);
    return;
  }

  if (RegClass == &AMDGPU::SReg_64RegClass ||
      RegClass == &AMDGPU::SGPR_64RegClass ||
      RegClass == &AMDGPU::SReg_64_XEXECRegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B64), DestReg)
      .addImm(Value);
    return;
  }

  if (RegClass == &AMDGPU::VGPR_32RegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DestReg)
      .addImm(Value);
    return;
  }
  if (RegClass->hasSuperClassEq(&AMDGPU::VReg_64RegClass)) {
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B64_PSEUDO), DestReg)
      .addImm(Value);
    return;
  }

  unsigned EltSize = 4;
  unsigned Opcode = AMDGPU::V_MOV_B32_e32;
  if (RI.isSGPRClass(RegClass)) {
    if (RI.getRegSizeInBits(*RegClass) > 32) {
      Opcode =  AMDGPU::S_MOV_B64;
      EltSize = 8;
    } else {
      Opcode = AMDGPU::S_MOV_B32;
      EltSize = 4;
    }
  }

  ArrayRef<int16_t> SubIndices = RI.getRegSplitParts(RegClass, EltSize);
  for (unsigned Idx = 0; Idx < SubIndices.size(); ++Idx) {
    int64_t IdxValue = Idx == 0 ? Value : 0;

    MachineInstrBuilder Builder = BuildMI(MBB, MI, DL,
      get(Opcode), RI.getSubReg(DestReg, SubIndices[Idx]));
    Builder.addImm(IdxValue);
  }
}

const TargetRegisterClass *
SIInstrInfo::getPreferredSelectRegClass(unsigned Size) const {
  return &AMDGPU::VGPR_32RegClass;
}

void SIInstrInfo::insertVectorSelect(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     const DebugLoc &DL, Register DstReg,
                                     ArrayRef<MachineOperand> Cond,
                                     Register TrueReg,
                                     Register FalseReg) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *BoolXExecRC =
    RI.getRegClass(AMDGPU::SReg_1_XEXECRegClassID);
  assert(MRI.getRegClass(DstReg) == &AMDGPU::VGPR_32RegClass &&
         "Not a VGPR32 reg");

  if (Cond.size() == 1) {
    Register SReg = MRI.createVirtualRegister(BoolXExecRC);
    BuildMI(MBB, I, DL, get(AMDGPU::COPY), SReg)
      .add(Cond[0]);
    BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
      .addImm(0)
      .addReg(FalseReg)
      .addImm(0)
      .addReg(TrueReg)
      .addReg(SReg);
  } else if (Cond.size() == 2) {
    assert(Cond[0].isImm() && "Cond[0] is not an immediate");
    switch (Cond[0].getImm()) {
    case SIInstrInfo::SCC_TRUE: {
      Register SReg = MRI.createVirtualRegister(BoolXExecRC);
      BuildMI(MBB, I, DL, get(ST.isWave32() ? AMDGPU::S_CSELECT_B32
                                            : AMDGPU::S_CSELECT_B64), SReg)
        .addImm(1)
        .addImm(0);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addImm(0)
        .addReg(FalseReg)
        .addImm(0)
        .addReg(TrueReg)
        .addReg(SReg);
      break;
    }
    case SIInstrInfo::SCC_FALSE: {
      Register SReg = MRI.createVirtualRegister(BoolXExecRC);
      BuildMI(MBB, I, DL, get(ST.isWave32() ? AMDGPU::S_CSELECT_B32
                                            : AMDGPU::S_CSELECT_B64), SReg)
        .addImm(0)
        .addImm(1);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addImm(0)
        .addReg(FalseReg)
        .addImm(0)
        .addReg(TrueReg)
        .addReg(SReg);
      break;
    }
    case SIInstrInfo::VCCNZ: {
      MachineOperand RegOp = Cond[1];
      RegOp.setImplicit(false);
      Register SReg = MRI.createVirtualRegister(BoolXExecRC);
      BuildMI(MBB, I, DL, get(AMDGPU::COPY), SReg)
        .add(RegOp);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
          .addImm(0)
          .addReg(FalseReg)
          .addImm(0)
          .addReg(TrueReg)
          .addReg(SReg);
      break;
    }
    case SIInstrInfo::VCCZ: {
      MachineOperand RegOp = Cond[1];
      RegOp.setImplicit(false);
      Register SReg = MRI.createVirtualRegister(BoolXExecRC);
      BuildMI(MBB, I, DL, get(AMDGPU::COPY), SReg)
        .add(RegOp);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
          .addImm(0)
          .addReg(TrueReg)
          .addImm(0)
          .addReg(FalseReg)
          .addReg(SReg);
      break;
    }
    case SIInstrInfo::EXECNZ: {
      Register SReg = MRI.createVirtualRegister(BoolXExecRC);
      Register SReg2 = MRI.createVirtualRegister(RI.getBoolRC());
      BuildMI(MBB, I, DL, get(ST.isWave32() ? AMDGPU::S_OR_SAVEEXEC_B32
                                            : AMDGPU::S_OR_SAVEEXEC_B64), SReg2)
        .addImm(0);
      BuildMI(MBB, I, DL, get(ST.isWave32() ? AMDGPU::S_CSELECT_B32
                                            : AMDGPU::S_CSELECT_B64), SReg)
        .addImm(1)
        .addImm(0);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addImm(0)
        .addReg(FalseReg)
        .addImm(0)
        .addReg(TrueReg)
        .addReg(SReg);
      break;
    }
    case SIInstrInfo::EXECZ: {
      Register SReg = MRI.createVirtualRegister(BoolXExecRC);
      Register SReg2 = MRI.createVirtualRegister(RI.getBoolRC());
      BuildMI(MBB, I, DL, get(ST.isWave32() ? AMDGPU::S_OR_SAVEEXEC_B32
                                            : AMDGPU::S_OR_SAVEEXEC_B64), SReg2)
        .addImm(0);
      BuildMI(MBB, I, DL, get(ST.isWave32() ? AMDGPU::S_CSELECT_B32
                                            : AMDGPU::S_CSELECT_B64), SReg)
        .addImm(0)
        .addImm(1);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addImm(0)
        .addReg(FalseReg)
        .addImm(0)
        .addReg(TrueReg)
        .addReg(SReg);
      llvm_unreachable("Unhandled branch predicate EXECZ");
      break;
    }
    default:
      llvm_unreachable("invalid branch predicate");
    }
  } else {
    llvm_unreachable("Can only handle Cond size 1 or 2");
  }
}

Register SIInstrInfo::insertEQ(MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL,
                               Register SrcReg, int Value) const {
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  Register Reg = MRI.createVirtualRegister(RI.getBoolRC());
  BuildMI(*MBB, I, DL, get(AMDGPU::V_CMP_EQ_I32_e64), Reg)
    .addImm(Value)
    .addReg(SrcReg);

  return Reg;
}

Register SIInstrInfo::insertNE(MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL,
                               Register SrcReg, int Value) const {
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  Register Reg = MRI.createVirtualRegister(RI.getBoolRC());
  BuildMI(*MBB, I, DL, get(AMDGPU::V_CMP_NE_I32_e64), Reg)
    .addImm(Value)
    .addReg(SrcReg);

  return Reg;
}

unsigned SIInstrInfo::getMovOpcode(const TargetRegisterClass *DstRC) const {

  if (RI.isAGPRClass(DstRC))
    return AMDGPU::COPY;
  if (RI.getRegSizeInBits(*DstRC) == 16) {
    // Assume hi bits are unneeded. Only _e64 true16 instructions are legal
    // before RA.
    return RI.isSGPRClass(DstRC) ? AMDGPU::COPY : AMDGPU::V_MOV_B16_t16_e64;
  }
  if (RI.getRegSizeInBits(*DstRC) == 32)
    return RI.isSGPRClass(DstRC) ? AMDGPU::S_MOV_B32 : AMDGPU::V_MOV_B32_e32;
  if (RI.getRegSizeInBits(*DstRC) == 64 && RI.isSGPRClass(DstRC))
    return AMDGPU::S_MOV_B64;
  if (RI.getRegSizeInBits(*DstRC) == 64 && !RI.isSGPRClass(DstRC))
    return AMDGPU::V_MOV_B64_PSEUDO;
  return AMDGPU::COPY;
}

const MCInstrDesc &
SIInstrInfo::getIndirectGPRIDXPseudo(unsigned VecSize,
                                     bool IsIndirectSrc) const {
  if (IsIndirectSrc) {
    if (VecSize <= 32) // 4 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V1);
    if (VecSize <= 64) // 8 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V2);
    if (VecSize <= 96) // 12 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V3);
    if (VecSize <= 128) // 16 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V4);
    if (VecSize <= 160) // 20 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V5);
    if (VecSize <= 256) // 32 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V8);
    if (VecSize <= 288) // 36 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V9);
    if (VecSize <= 320) // 40 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V10);
    if (VecSize <= 352) // 44 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V11);
    if (VecSize <= 384) // 48 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V12);
    if (VecSize <= 512) // 64 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V16);
    if (VecSize <= 1024) // 128 bytes
      return get(AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V32);

    llvm_unreachable("unsupported size for IndirectRegReadGPRIDX pseudos");
  }

  if (VecSize <= 32) // 4 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V1);
  if (VecSize <= 64) // 8 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V2);
  if (VecSize <= 96) // 12 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V3);
  if (VecSize <= 128) // 16 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V4);
  if (VecSize <= 160) // 20 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V5);
  if (VecSize <= 256) // 32 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V8);
  if (VecSize <= 288) // 36 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V9);
  if (VecSize <= 320) // 40 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V10);
  if (VecSize <= 352) // 44 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V11);
  if (VecSize <= 384) // 48 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V12);
  if (VecSize <= 512) // 64 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V16);
  if (VecSize <= 1024) // 128 bytes
    return get(AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V32);

  llvm_unreachable("unsupported size for IndirectRegWriteGPRIDX pseudos");
}

static unsigned getIndirectVGPRWriteMovRelPseudoOpc(unsigned VecSize) {
  if (VecSize <= 32) // 4 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V1;
  if (VecSize <= 64) // 8 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V2;
  if (VecSize <= 96) // 12 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V3;
  if (VecSize <= 128) // 16 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V4;
  if (VecSize <= 160) // 20 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V5;
  if (VecSize <= 256) // 32 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V8;
  if (VecSize <= 288) // 36 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V9;
  if (VecSize <= 320) // 40 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V10;
  if (VecSize <= 352) // 44 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V11;
  if (VecSize <= 384) // 48 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V12;
  if (VecSize <= 512) // 64 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V16;
  if (VecSize <= 1024) // 128 bytes
    return AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V32;

  llvm_unreachable("unsupported size for IndirectRegWrite pseudos");
}

static unsigned getIndirectSGPRWriteMovRelPseudo32(unsigned VecSize) {
  if (VecSize <= 32) // 4 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V1;
  if (VecSize <= 64) // 8 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V2;
  if (VecSize <= 96) // 12 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V3;
  if (VecSize <= 128) // 16 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V4;
  if (VecSize <= 160) // 20 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V5;
  if (VecSize <= 256) // 32 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V8;
  if (VecSize <= 288) // 36 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V9;
  if (VecSize <= 320) // 40 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V10;
  if (VecSize <= 352) // 44 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V11;
  if (VecSize <= 384) // 48 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V12;
  if (VecSize <= 512) // 64 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V16;
  if (VecSize <= 1024) // 128 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V32;

  llvm_unreachable("unsupported size for IndirectRegWrite pseudos");
}

static unsigned getIndirectSGPRWriteMovRelPseudo64(unsigned VecSize) {
  if (VecSize <= 64) // 8 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V1;
  if (VecSize <= 128) // 16 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V2;
  if (VecSize <= 256) // 32 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V4;
  if (VecSize <= 512) // 64 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V8;
  if (VecSize <= 1024) // 128 bytes
    return AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V16;

  llvm_unreachable("unsupported size for IndirectRegWrite pseudos");
}

const MCInstrDesc &
SIInstrInfo::getIndirectRegWriteMovRelPseudo(unsigned VecSize, unsigned EltSize,
                                             bool IsSGPR) const {
  if (IsSGPR) {
    switch (EltSize) {
    case 32:
      return get(getIndirectSGPRWriteMovRelPseudo32(VecSize));
    case 64:
      return get(getIndirectSGPRWriteMovRelPseudo64(VecSize));
    default:
      llvm_unreachable("invalid reg indexing elt size");
    }
  }

  assert(EltSize == 32 && "invalid reg indexing elt size");
  return get(getIndirectVGPRWriteMovRelPseudoOpc(VecSize));
}

static unsigned getSGPRSpillSaveOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_S32_SAVE;
  case 8:
    return AMDGPU::SI_SPILL_S64_SAVE;
  case 12:
    return AMDGPU::SI_SPILL_S96_SAVE;
  case 16:
    return AMDGPU::SI_SPILL_S128_SAVE;
  case 20:
    return AMDGPU::SI_SPILL_S160_SAVE;
  case 24:
    return AMDGPU::SI_SPILL_S192_SAVE;
  case 28:
    return AMDGPU::SI_SPILL_S224_SAVE;
  case 32:
    return AMDGPU::SI_SPILL_S256_SAVE;
  case 36:
    return AMDGPU::SI_SPILL_S288_SAVE;
  case 40:
    return AMDGPU::SI_SPILL_S320_SAVE;
  case 44:
    return AMDGPU::SI_SPILL_S352_SAVE;
  case 48:
    return AMDGPU::SI_SPILL_S384_SAVE;
  case 64:
    return AMDGPU::SI_SPILL_S512_SAVE;
  case 128:
    return AMDGPU::SI_SPILL_S1024_SAVE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getVGPRSpillSaveOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_V32_SAVE;
  case 8:
    return AMDGPU::SI_SPILL_V64_SAVE;
  case 12:
    return AMDGPU::SI_SPILL_V96_SAVE;
  case 16:
    return AMDGPU::SI_SPILL_V128_SAVE;
  case 20:
    return AMDGPU::SI_SPILL_V160_SAVE;
  case 24:
    return AMDGPU::SI_SPILL_V192_SAVE;
  case 28:
    return AMDGPU::SI_SPILL_V224_SAVE;
  case 32:
    return AMDGPU::SI_SPILL_V256_SAVE;
  case 36:
    return AMDGPU::SI_SPILL_V288_SAVE;
  case 40:
    return AMDGPU::SI_SPILL_V320_SAVE;
  case 44:
    return AMDGPU::SI_SPILL_V352_SAVE;
  case 48:
    return AMDGPU::SI_SPILL_V384_SAVE;
  case 64:
    return AMDGPU::SI_SPILL_V512_SAVE;
  case 128:
    return AMDGPU::SI_SPILL_V1024_SAVE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getAGPRSpillSaveOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_A32_SAVE;
  case 8:
    return AMDGPU::SI_SPILL_A64_SAVE;
  case 12:
    return AMDGPU::SI_SPILL_A96_SAVE;
  case 16:
    return AMDGPU::SI_SPILL_A128_SAVE;
  case 20:
    return AMDGPU::SI_SPILL_A160_SAVE;
  case 24:
    return AMDGPU::SI_SPILL_A192_SAVE;
  case 28:
    return AMDGPU::SI_SPILL_A224_SAVE;
  case 32:
    return AMDGPU::SI_SPILL_A256_SAVE;
  case 36:
    return AMDGPU::SI_SPILL_A288_SAVE;
  case 40:
    return AMDGPU::SI_SPILL_A320_SAVE;
  case 44:
    return AMDGPU::SI_SPILL_A352_SAVE;
  case 48:
    return AMDGPU::SI_SPILL_A384_SAVE;
  case 64:
    return AMDGPU::SI_SPILL_A512_SAVE;
  case 128:
    return AMDGPU::SI_SPILL_A1024_SAVE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getAVSpillSaveOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_AV32_SAVE;
  case 8:
    return AMDGPU::SI_SPILL_AV64_SAVE;
  case 12:
    return AMDGPU::SI_SPILL_AV96_SAVE;
  case 16:
    return AMDGPU::SI_SPILL_AV128_SAVE;
  case 20:
    return AMDGPU::SI_SPILL_AV160_SAVE;
  case 24:
    return AMDGPU::SI_SPILL_AV192_SAVE;
  case 28:
    return AMDGPU::SI_SPILL_AV224_SAVE;
  case 32:
    return AMDGPU::SI_SPILL_AV256_SAVE;
  case 36:
    return AMDGPU::SI_SPILL_AV288_SAVE;
  case 40:
    return AMDGPU::SI_SPILL_AV320_SAVE;
  case 44:
    return AMDGPU::SI_SPILL_AV352_SAVE;
  case 48:
    return AMDGPU::SI_SPILL_AV384_SAVE;
  case 64:
    return AMDGPU::SI_SPILL_AV512_SAVE;
  case 128:
    return AMDGPU::SI_SPILL_AV1024_SAVE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getWWMRegSpillSaveOpcode(unsigned Size,
                                         bool IsVectorSuperClass) {
  // Currently, there is only 32-bit WWM register spills needed.
  if (Size != 4)
    llvm_unreachable("unknown wwm register spill size");

  if (IsVectorSuperClass)
    return AMDGPU::SI_SPILL_WWM_AV32_SAVE;

  return AMDGPU::SI_SPILL_WWM_V32_SAVE;
}

static unsigned getVectorRegSpillSaveOpcode(Register Reg,
                                            const TargetRegisterClass *RC,
                                            unsigned Size,
                                            const SIRegisterInfo &TRI,
                                            const SIMachineFunctionInfo &MFI) {
  bool IsVectorSuperClass = TRI.isVectorSuperClass(RC);

  // Choose the right opcode if spilling a WWM register.
  if (MFI.checkFlag(Reg, AMDGPU::VirtRegFlag::WWM_REG))
    return getWWMRegSpillSaveOpcode(Size, IsVectorSuperClass);

  if (IsVectorSuperClass)
    return getAVSpillSaveOpcode(Size);

  return TRI.isAGPRClass(RC) ? getAGPRSpillSaveOpcode(Size)
                             : getVGPRSpillSaveOpcode(Size);
}

void SIInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIndex, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  MachineFunction *MF = MBB.getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const DebugLoc &DL = MBB.findDebugLoc(MI);

  MachinePointerInfo PtrInfo
    = MachinePointerInfo::getFixedStack(*MF, FrameIndex);
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      PtrInfo, MachineMemOperand::MOStore, FrameInfo.getObjectSize(FrameIndex),
      FrameInfo.getObjectAlign(FrameIndex));
  unsigned SpillSize = TRI->getSpillSize(*RC);

  MachineRegisterInfo &MRI = MF->getRegInfo();
  if (RI.isSGPRClass(RC)) {
    MFI->setHasSpilledSGPRs();
    assert(SrcReg != AMDGPU::M0 && "m0 should not be spilled");
    assert(SrcReg != AMDGPU::EXEC_LO && SrcReg != AMDGPU::EXEC_HI &&
           SrcReg != AMDGPU::EXEC && "exec should not be spilled");

    // We are only allowed to create one new instruction when spilling
    // registers, so we need to use pseudo instruction for spilling SGPRs.
    const MCInstrDesc &OpDesc = get(getSGPRSpillSaveOpcode(SpillSize));

    // The SGPR spill/restore instructions only work on number sgprs, so we need
    // to make sure we are using the correct register class.
    if (SrcReg.isVirtual() && SpillSize == 4) {
      MRI.constrainRegClass(SrcReg, &AMDGPU::SReg_32_XM0_XEXECRegClass);
    }

    BuildMI(MBB, MI, DL, OpDesc)
      .addReg(SrcReg, getKillRegState(isKill)) // data
      .addFrameIndex(FrameIndex)               // addr
      .addMemOperand(MMO)
      .addReg(MFI->getStackPtrOffsetReg(), RegState::Implicit);

    if (RI.spillSGPRToVGPR())
      FrameInfo.setStackID(FrameIndex, TargetStackID::SGPRSpill);
    return;
  }

  unsigned Opcode = getVectorRegSpillSaveOpcode(VReg ? VReg : SrcReg, RC,
                                                SpillSize, RI, *MFI);
  MFI->setHasSpilledVGPRs();

  BuildMI(MBB, MI, DL, get(Opcode))
    .addReg(SrcReg, getKillRegState(isKill)) // data
    .addFrameIndex(FrameIndex)               // addr
    .addReg(MFI->getStackPtrOffsetReg())     // scratch_offset
    .addImm(0)                               // offset
    .addMemOperand(MMO);
}

static unsigned getSGPRSpillRestoreOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_S32_RESTORE;
  case 8:
    return AMDGPU::SI_SPILL_S64_RESTORE;
  case 12:
    return AMDGPU::SI_SPILL_S96_RESTORE;
  case 16:
    return AMDGPU::SI_SPILL_S128_RESTORE;
  case 20:
    return AMDGPU::SI_SPILL_S160_RESTORE;
  case 24:
    return AMDGPU::SI_SPILL_S192_RESTORE;
  case 28:
    return AMDGPU::SI_SPILL_S224_RESTORE;
  case 32:
    return AMDGPU::SI_SPILL_S256_RESTORE;
  case 36:
    return AMDGPU::SI_SPILL_S288_RESTORE;
  case 40:
    return AMDGPU::SI_SPILL_S320_RESTORE;
  case 44:
    return AMDGPU::SI_SPILL_S352_RESTORE;
  case 48:
    return AMDGPU::SI_SPILL_S384_RESTORE;
  case 64:
    return AMDGPU::SI_SPILL_S512_RESTORE;
  case 128:
    return AMDGPU::SI_SPILL_S1024_RESTORE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getVGPRSpillRestoreOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_V32_RESTORE;
  case 8:
    return AMDGPU::SI_SPILL_V64_RESTORE;
  case 12:
    return AMDGPU::SI_SPILL_V96_RESTORE;
  case 16:
    return AMDGPU::SI_SPILL_V128_RESTORE;
  case 20:
    return AMDGPU::SI_SPILL_V160_RESTORE;
  case 24:
    return AMDGPU::SI_SPILL_V192_RESTORE;
  case 28:
    return AMDGPU::SI_SPILL_V224_RESTORE;
  case 32:
    return AMDGPU::SI_SPILL_V256_RESTORE;
  case 36:
    return AMDGPU::SI_SPILL_V288_RESTORE;
  case 40:
    return AMDGPU::SI_SPILL_V320_RESTORE;
  case 44:
    return AMDGPU::SI_SPILL_V352_RESTORE;
  case 48:
    return AMDGPU::SI_SPILL_V384_RESTORE;
  case 64:
    return AMDGPU::SI_SPILL_V512_RESTORE;
  case 128:
    return AMDGPU::SI_SPILL_V1024_RESTORE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getAGPRSpillRestoreOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_A32_RESTORE;
  case 8:
    return AMDGPU::SI_SPILL_A64_RESTORE;
  case 12:
    return AMDGPU::SI_SPILL_A96_RESTORE;
  case 16:
    return AMDGPU::SI_SPILL_A128_RESTORE;
  case 20:
    return AMDGPU::SI_SPILL_A160_RESTORE;
  case 24:
    return AMDGPU::SI_SPILL_A192_RESTORE;
  case 28:
    return AMDGPU::SI_SPILL_A224_RESTORE;
  case 32:
    return AMDGPU::SI_SPILL_A256_RESTORE;
  case 36:
    return AMDGPU::SI_SPILL_A288_RESTORE;
  case 40:
    return AMDGPU::SI_SPILL_A320_RESTORE;
  case 44:
    return AMDGPU::SI_SPILL_A352_RESTORE;
  case 48:
    return AMDGPU::SI_SPILL_A384_RESTORE;
  case 64:
    return AMDGPU::SI_SPILL_A512_RESTORE;
  case 128:
    return AMDGPU::SI_SPILL_A1024_RESTORE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getAVSpillRestoreOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_AV32_RESTORE;
  case 8:
    return AMDGPU::SI_SPILL_AV64_RESTORE;
  case 12:
    return AMDGPU::SI_SPILL_AV96_RESTORE;
  case 16:
    return AMDGPU::SI_SPILL_AV128_RESTORE;
  case 20:
    return AMDGPU::SI_SPILL_AV160_RESTORE;
  case 24:
    return AMDGPU::SI_SPILL_AV192_RESTORE;
  case 28:
    return AMDGPU::SI_SPILL_AV224_RESTORE;
  case 32:
    return AMDGPU::SI_SPILL_AV256_RESTORE;
  case 36:
    return AMDGPU::SI_SPILL_AV288_RESTORE;
  case 40:
    return AMDGPU::SI_SPILL_AV320_RESTORE;
  case 44:
    return AMDGPU::SI_SPILL_AV352_RESTORE;
  case 48:
    return AMDGPU::SI_SPILL_AV384_RESTORE;
  case 64:
    return AMDGPU::SI_SPILL_AV512_RESTORE;
  case 128:
    return AMDGPU::SI_SPILL_AV1024_RESTORE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getWWMRegSpillRestoreOpcode(unsigned Size,
                                            bool IsVectorSuperClass) {
  // Currently, there is only 32-bit WWM register spills needed.
  if (Size != 4)
    llvm_unreachable("unknown wwm register spill size");

  if (IsVectorSuperClass)
    return AMDGPU::SI_SPILL_WWM_AV32_RESTORE;

  return AMDGPU::SI_SPILL_WWM_V32_RESTORE;
}

static unsigned
getVectorRegSpillRestoreOpcode(Register Reg, const TargetRegisterClass *RC,
                               unsigned Size, const SIRegisterInfo &TRI,
                               const SIMachineFunctionInfo &MFI) {
  bool IsVectorSuperClass = TRI.isVectorSuperClass(RC);

  // Choose the right opcode if restoring a WWM register.
  if (MFI.checkFlag(Reg, AMDGPU::VirtRegFlag::WWM_REG))
    return getWWMRegSpillRestoreOpcode(Size, IsVectorSuperClass);

  if (IsVectorSuperClass)
    return getAVSpillRestoreOpcode(Size);

  return TRI.isAGPRClass(RC) ? getAGPRSpillRestoreOpcode(Size)
                             : getVGPRSpillRestoreOpcode(Size);
}

void SIInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register DestReg, int FrameIndex,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI,
                                       Register VReg) const {
  MachineFunction *MF = MBB.getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const DebugLoc &DL = MBB.findDebugLoc(MI);
  unsigned SpillSize = TRI->getSpillSize(*RC);

  MachinePointerInfo PtrInfo
    = MachinePointerInfo::getFixedStack(*MF, FrameIndex);

  MachineMemOperand *MMO = MF->getMachineMemOperand(
      PtrInfo, MachineMemOperand::MOLoad, FrameInfo.getObjectSize(FrameIndex),
      FrameInfo.getObjectAlign(FrameIndex));

  if (RI.isSGPRClass(RC)) {
    MFI->setHasSpilledSGPRs();
    assert(DestReg != AMDGPU::M0 && "m0 should not be reloaded into");
    assert(DestReg != AMDGPU::EXEC_LO && DestReg != AMDGPU::EXEC_HI &&
           DestReg != AMDGPU::EXEC && "exec should not be spilled");

    // FIXME: Maybe this should not include a memoperand because it will be
    // lowered to non-memory instructions.
    const MCInstrDesc &OpDesc = get(getSGPRSpillRestoreOpcode(SpillSize));
    if (DestReg.isVirtual() && SpillSize == 4) {
      MachineRegisterInfo &MRI = MF->getRegInfo();
      MRI.constrainRegClass(DestReg, &AMDGPU::SReg_32_XM0_XEXECRegClass);
    }

    if (RI.spillSGPRToVGPR())
      FrameInfo.setStackID(FrameIndex, TargetStackID::SGPRSpill);
    BuildMI(MBB, MI, DL, OpDesc, DestReg)
      .addFrameIndex(FrameIndex) // addr
      .addMemOperand(MMO)
      .addReg(MFI->getStackPtrOffsetReg(), RegState::Implicit);

    return;
  }

  unsigned Opcode = getVectorRegSpillRestoreOpcode(VReg ? VReg : DestReg, RC,
                                                   SpillSize, RI, *MFI);
  BuildMI(MBB, MI, DL, get(Opcode), DestReg)
      .addFrameIndex(FrameIndex)           // vaddr
      .addReg(MFI->getStackPtrOffsetReg()) // scratch_offset
      .addImm(0)                           // offset
      .addMemOperand(MMO);
}

void SIInstrInfo::insertNoop(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MI) const {
  insertNoops(MBB, MI, 1);
}

void SIInstrInfo::insertNoops(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              unsigned Quantity) const {
  DebugLoc DL = MBB.findDebugLoc(MI);
  while (Quantity > 0) {
    unsigned Arg = std::min(Quantity, 8u);
    Quantity -= Arg;
    BuildMI(MBB, MI, DL, get(AMDGPU::S_NOP)).addImm(Arg - 1);
  }
}

void SIInstrInfo::insertReturn(MachineBasicBlock &MBB) const {
  auto MF = MBB.getParent();
  SIMachineFunctionInfo *Info = MF->getInfo<SIMachineFunctionInfo>();

  assert(Info->isEntryFunction());

  if (MBB.succ_empty()) {
    bool HasNoTerminator = MBB.getFirstTerminator() == MBB.end();
    if (HasNoTerminator) {
      if (Info->returnsVoid()) {
        BuildMI(MBB, MBB.end(), DebugLoc(), get(AMDGPU::S_ENDPGM)).addImm(0);
      } else {
        BuildMI(MBB, MBB.end(), DebugLoc(), get(AMDGPU::SI_RETURN_TO_EPILOG));
      }
    }
  }
}

MachineBasicBlock *SIInstrInfo::insertSimulatedTrap(MachineRegisterInfo &MRI,
                                                    MachineBasicBlock &MBB,
                                                    MachineInstr &MI,
                                                    const DebugLoc &DL) const {
  MachineFunction *MF = MBB.getParent();
  constexpr unsigned DoorbellIDMask = 0x3ff;
  constexpr unsigned ECQueueWaveAbort = 0x400;

  MachineBasicBlock *TrapBB = &MBB;
  MachineBasicBlock *ContBB = &MBB;
  MachineBasicBlock *HaltLoopBB = MF->CreateMachineBasicBlock();

  if (!MBB.succ_empty() || std::next(MI.getIterator()) != MBB.end()) {
    ContBB = MBB.splitAt(MI, /*UpdateLiveIns=*/false);
    TrapBB = MF->CreateMachineBasicBlock();
    BuildMI(MBB, MI, DL, get(AMDGPU::S_CBRANCH_EXECNZ)).addMBB(TrapBB);
    MF->push_back(TrapBB);
    MBB.addSuccessor(TrapBB);
  }

  // Start with a `s_trap 2`, if we're in PRIV=1 and we need the workaround this
  // will be a nop.
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_TRAP))
      .addImm(static_cast<unsigned>(GCNSubtarget::TrapID::LLVMAMDHSATrap));
  Register DoorbellReg = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_SENDMSG_RTN_B32),
          DoorbellReg)
      .addImm(AMDGPU::SendMsg::ID_RTN_GET_DOORBELL);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_MOV_B32), AMDGPU::TTMP2)
      .addUse(AMDGPU::M0);
  Register DoorbellRegMasked =
      MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_AND_B32), DoorbellRegMasked)
      .addUse(DoorbellReg)
      .addImm(DoorbellIDMask);
  Register SetWaveAbortBit =
      MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_OR_B32), SetWaveAbortBit)
      .addUse(DoorbellRegMasked)
      .addImm(ECQueueWaveAbort);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_MOV_B32), AMDGPU::M0)
      .addUse(SetWaveAbortBit);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_SENDMSG))
      .addImm(AMDGPU::SendMsg::ID_INTERRUPT);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_MOV_B32), AMDGPU::M0)
      .addUse(AMDGPU::TTMP2);
  BuildMI(*TrapBB, TrapBB->end(), DL, get(AMDGPU::S_BRANCH)).addMBB(HaltLoopBB);
  TrapBB->addSuccessor(HaltLoopBB);

  BuildMI(*HaltLoopBB, HaltLoopBB->end(), DL, get(AMDGPU::S_SETHALT)).addImm(5);
  BuildMI(*HaltLoopBB, HaltLoopBB->end(), DL, get(AMDGPU::S_BRANCH))
      .addMBB(HaltLoopBB);
  MF->push_back(HaltLoopBB);
  HaltLoopBB->addSuccessor(HaltLoopBB);

  return ContBB;
}

unsigned SIInstrInfo::getNumWaitStates(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    if (MI.isMetaInstruction())
      return 0;
    return 1; // FIXME: Do wait states equal cycles?

  case AMDGPU::S_NOP:
    return MI.getOperand(0).getImm() + 1;
  // SI_RETURN_TO_EPILOG is a fallthrough to code outside of the function. The
  // hazard, even if one exist, won't really be visible. Should we handle it?
  }
}

bool SIInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MBB.findDebugLoc(MI);
  switch (MI.getOpcode()) {
  default: return TargetInstrInfo::expandPostRAPseudo(MI);
  case AMDGPU::S_MOV_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_MOV_B64));
    break;

  case AMDGPU::S_MOV_B32_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_MOV_B32));
    break;

  case AMDGPU::S_XOR_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_XOR_B64));
    break;

  case AMDGPU::S_XOR_B32_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_XOR_B32));
    break;
  case AMDGPU::S_OR_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_OR_B64));
    break;
  case AMDGPU::S_OR_B32_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_OR_B32));
    break;

  case AMDGPU::S_ANDN2_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_ANDN2_B64));
    break;

  case AMDGPU::S_ANDN2_B32_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_ANDN2_B32));
    break;

  case AMDGPU::S_AND_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_AND_B64));
    break;

  case AMDGPU::S_AND_B32_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_AND_B32));
    break;

  case AMDGPU::S_AND_SAVEEXEC_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_AND_SAVEEXEC_B64));
    break;

  case AMDGPU::S_AND_SAVEEXEC_B32_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_AND_SAVEEXEC_B32));
    break;

  case AMDGPU::SI_SPILL_S32_TO_VGPR:
    MI.setDesc(get(AMDGPU::V_WRITELANE_B32));
    break;

  case AMDGPU::SI_RESTORE_S32_FROM_VGPR:
    MI.setDesc(get(AMDGPU::V_READLANE_B32));
    break;

  case AMDGPU::V_MOV_B64_PSEUDO: {
    Register Dst = MI.getOperand(0).getReg();
    Register DstLo = RI.getSubReg(Dst, AMDGPU::sub0);
    Register DstHi = RI.getSubReg(Dst, AMDGPU::sub1);

    const MachineOperand &SrcOp = MI.getOperand(1);
    // FIXME: Will this work for 64-bit floating point immediates?
    assert(!SrcOp.isFPImm());
    if (ST.hasMovB64()) {
      MI.setDesc(get(AMDGPU::V_MOV_B64_e32));
      if (SrcOp.isReg() || isInlineConstant(MI, 1) ||
          isUInt<32>(SrcOp.getImm()))
        break;
    }
    if (SrcOp.isImm()) {
      APInt Imm(64, SrcOp.getImm());
      APInt Lo(32, Imm.getLoBits(32).getZExtValue());
      APInt Hi(32, Imm.getHiBits(32).getZExtValue());
      if (ST.hasPkMovB32() && Lo == Hi && isInlineConstant(Lo)) {
        BuildMI(MBB, MI, DL, get(AMDGPU::V_PK_MOV_B32), Dst)
          .addImm(SISrcMods::OP_SEL_1)
          .addImm(Lo.getSExtValue())
          .addImm(SISrcMods::OP_SEL_1)
          .addImm(Lo.getSExtValue())
          .addImm(0)  // op_sel_lo
          .addImm(0)  // op_sel_hi
          .addImm(0)  // neg_lo
          .addImm(0)  // neg_hi
          .addImm(0); // clamp
      } else {
        BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstLo)
          .addImm(Lo.getSExtValue())
          .addReg(Dst, RegState::Implicit | RegState::Define);
        BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstHi)
          .addImm(Hi.getSExtValue())
          .addReg(Dst, RegState::Implicit | RegState::Define);
      }
    } else {
      assert(SrcOp.isReg());
      if (ST.hasPkMovB32() &&
          !RI.isAGPR(MBB.getParent()->getRegInfo(), SrcOp.getReg())) {
        BuildMI(MBB, MI, DL, get(AMDGPU::V_PK_MOV_B32), Dst)
          .addImm(SISrcMods::OP_SEL_1) // src0_mod
          .addReg(SrcOp.getReg())
          .addImm(SISrcMods::OP_SEL_0 | SISrcMods::OP_SEL_1) // src1_mod
          .addReg(SrcOp.getReg())
          .addImm(0)  // op_sel_lo
          .addImm(0)  // op_sel_hi
          .addImm(0)  // neg_lo
          .addImm(0)  // neg_hi
          .addImm(0); // clamp
      } else {
        BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstLo)
          .addReg(RI.getSubReg(SrcOp.getReg(), AMDGPU::sub0))
          .addReg(Dst, RegState::Implicit | RegState::Define);
        BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstHi)
          .addReg(RI.getSubReg(SrcOp.getReg(), AMDGPU::sub1))
          .addReg(Dst, RegState::Implicit | RegState::Define);
      }
    }
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_MOV_B64_DPP_PSEUDO: {
    expandMovDPP64(MI);
    break;
  }
  case AMDGPU::S_MOV_B64_IMM_PSEUDO: {
    const MachineOperand &SrcOp = MI.getOperand(1);
    assert(!SrcOp.isFPImm());
    APInt Imm(64, SrcOp.getImm());
    if (Imm.isIntN(32) || isInlineConstant(Imm)) {
      MI.setDesc(get(AMDGPU::S_MOV_B64));
      break;
    }

    Register Dst = MI.getOperand(0).getReg();
    Register DstLo = RI.getSubReg(Dst, AMDGPU::sub0);
    Register DstHi = RI.getSubReg(Dst, AMDGPU::sub1);

    APInt Lo(32, Imm.getLoBits(32).getZExtValue());
    APInt Hi(32, Imm.getHiBits(32).getZExtValue());
    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), DstLo)
      .addImm(Lo.getSExtValue())
      .addReg(Dst, RegState::Implicit | RegState::Define);
    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), DstHi)
      .addImm(Hi.getSExtValue())
      .addReg(Dst, RegState::Implicit | RegState::Define);
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_SET_INACTIVE_B32: {
    unsigned NotOpc = ST.isWave32() ? AMDGPU::S_NOT_B32 : AMDGPU::S_NOT_B64;
    unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
    // FIXME: We may possibly optimize the COPY once we find ways to make LLVM
    // optimizations (mainly Register Coalescer) aware of WWM register liveness.
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), MI.getOperand(0).getReg())
        .add(MI.getOperand(1));
    auto FirstNot = BuildMI(MBB, MI, DL, get(NotOpc), Exec).addReg(Exec);
    FirstNot->addRegisterDead(AMDGPU::SCC, TRI); // SCC is overwritten
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), MI.getOperand(0).getReg())
      .add(MI.getOperand(2));
    BuildMI(MBB, MI, DL, get(NotOpc), Exec)
      .addReg(Exec);
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_SET_INACTIVE_B64: {
    unsigned NotOpc = ST.isWave32() ? AMDGPU::S_NOT_B32 : AMDGPU::S_NOT_B64;
    unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
    MachineInstr *Copy = BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B64_PSEUDO),
                                 MI.getOperand(0).getReg())
                             .add(MI.getOperand(1));
    expandPostRAPseudo(*Copy);
    auto FirstNot = BuildMI(MBB, MI, DL, get(NotOpc), Exec).addReg(Exec);
    FirstNot->addRegisterDead(AMDGPU::SCC, TRI); // SCC is overwritten
    Copy = BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B64_PSEUDO),
                   MI.getOperand(0).getReg())
               .add(MI.getOperand(2));
    expandPostRAPseudo(*Copy);
    BuildMI(MBB, MI, DL, get(NotOpc), Exec)
      .addReg(Exec);
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V1:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V2:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V3:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V4:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V5:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V8:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V9:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V10:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V11:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V12:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V16:
  case AMDGPU::V_INDIRECT_REG_WRITE_MOVREL_B32_V32:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V1:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V2:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V3:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V4:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V5:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V8:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V9:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V10:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V11:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V12:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V16:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B32_V32:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V1:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V2:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V4:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V8:
  case AMDGPU::S_INDIRECT_REG_WRITE_MOVREL_B64_V16: {
    const TargetRegisterClass *EltRC = getOpRegClass(MI, 2);

    unsigned Opc;
    if (RI.hasVGPRs(EltRC)) {
      Opc = AMDGPU::V_MOVRELD_B32_e32;
    } else {
      Opc = RI.getRegSizeInBits(*EltRC) == 64 ? AMDGPU::S_MOVRELD_B64
                                              : AMDGPU::S_MOVRELD_B32;
    }

    const MCInstrDesc &OpDesc = get(Opc);
    Register VecReg = MI.getOperand(0).getReg();
    bool IsUndef = MI.getOperand(1).isUndef();
    unsigned SubReg = MI.getOperand(3).getImm();
    assert(VecReg == MI.getOperand(1).getReg());

    MachineInstrBuilder MIB =
      BuildMI(MBB, MI, DL, OpDesc)
        .addReg(RI.getSubReg(VecReg, SubReg), RegState::Undef)
        .add(MI.getOperand(2))
        .addReg(VecReg, RegState::ImplicitDefine)
        .addReg(VecReg, RegState::Implicit | (IsUndef ? RegState::Undef : 0));

    const int ImpDefIdx =
        OpDesc.getNumOperands() + OpDesc.implicit_uses().size();
    const int ImpUseIdx = ImpDefIdx + 1;
    MIB->tieOperands(ImpDefIdx, ImpUseIdx);
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V1:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V2:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V3:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V4:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V5:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V8:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V9:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V10:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V11:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V12:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V16:
  case AMDGPU::V_INDIRECT_REG_WRITE_GPR_IDX_B32_V32: {
    assert(ST.useVGPRIndexMode());
    Register VecReg = MI.getOperand(0).getReg();
    bool IsUndef = MI.getOperand(1).isUndef();
    Register Idx = MI.getOperand(3).getReg();
    Register SubReg = MI.getOperand(4).getImm();

    MachineInstr *SetOn = BuildMI(MBB, MI, DL, get(AMDGPU::S_SET_GPR_IDX_ON))
                              .addReg(Idx)
                              .addImm(AMDGPU::VGPRIndexMode::DST_ENABLE);
    SetOn->getOperand(3).setIsUndef();

    const MCInstrDesc &OpDesc = get(AMDGPU::V_MOV_B32_indirect_write);
    MachineInstrBuilder MIB =
        BuildMI(MBB, MI, DL, OpDesc)
            .addReg(RI.getSubReg(VecReg, SubReg), RegState::Undef)
            .add(MI.getOperand(2))
            .addReg(VecReg, RegState::ImplicitDefine)
            .addReg(VecReg,
                    RegState::Implicit | (IsUndef ? RegState::Undef : 0));

    const int ImpDefIdx =
        OpDesc.getNumOperands() + OpDesc.implicit_uses().size();
    const int ImpUseIdx = ImpDefIdx + 1;
    MIB->tieOperands(ImpDefIdx, ImpUseIdx);

    MachineInstr *SetOff = BuildMI(MBB, MI, DL, get(AMDGPU::S_SET_GPR_IDX_OFF));

    finalizeBundle(MBB, SetOn->getIterator(), std::next(SetOff->getIterator()));

    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V1:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V2:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V3:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V4:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V5:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V8:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V9:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V10:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V11:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V12:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V16:
  case AMDGPU::V_INDIRECT_REG_READ_GPR_IDX_B32_V32: {
    assert(ST.useVGPRIndexMode());
    Register Dst = MI.getOperand(0).getReg();
    Register VecReg = MI.getOperand(1).getReg();
    bool IsUndef = MI.getOperand(1).isUndef();
    Register Idx = MI.getOperand(2).getReg();
    Register SubReg = MI.getOperand(3).getImm();

    MachineInstr *SetOn = BuildMI(MBB, MI, DL, get(AMDGPU::S_SET_GPR_IDX_ON))
                              .addReg(Idx)
                              .addImm(AMDGPU::VGPRIndexMode::SRC0_ENABLE);
    SetOn->getOperand(3).setIsUndef();

    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_indirect_read))
        .addDef(Dst)
        .addReg(RI.getSubReg(VecReg, SubReg), RegState::Undef)
        .addReg(VecReg, RegState::Implicit | (IsUndef ? RegState::Undef : 0));

    MachineInstr *SetOff = BuildMI(MBB, MI, DL, get(AMDGPU::S_SET_GPR_IDX_OFF));

    finalizeBundle(MBB, SetOn->getIterator(), std::next(SetOff->getIterator()));

    MI.eraseFromParent();
    break;
  }
  case AMDGPU::SI_PC_ADD_REL_OFFSET: {
    MachineFunction &MF = *MBB.getParent();
    Register Reg = MI.getOperand(0).getReg();
    Register RegLo = RI.getSubReg(Reg, AMDGPU::sub0);
    Register RegHi = RI.getSubReg(Reg, AMDGPU::sub1);
    MachineOperand OpLo = MI.getOperand(1);
    MachineOperand OpHi = MI.getOperand(2);

    // Create a bundle so these instructions won't be re-ordered by the
    // post-RA scheduler.
    MIBundleBuilder Bundler(MBB, MI);
    Bundler.append(BuildMI(MF, DL, get(AMDGPU::S_GETPC_B64), Reg));

    // What we want here is an offset from the value returned by s_getpc (which
    // is the address of the s_add_u32 instruction) to the global variable, but
    // since the encoding of $symbol starts 4 bytes after the start of the
    // s_add_u32 instruction, we end up with an offset that is 4 bytes too
    // small. This requires us to add 4 to the global variable offset in order
    // to compute the correct address. Similarly for the s_addc_u32 instruction,
    // the encoding of $symbol starts 12 bytes after the start of the s_add_u32
    // instruction.

    int64_t Adjust = 0;
    if (ST.hasGetPCZeroExtension()) {
      // Fix up hardware that does not sign-extend the 48-bit PC value by
      // inserting: s_sext_i32_i16 reghi, reghi
      Bundler.append(
          BuildMI(MF, DL, get(AMDGPU::S_SEXT_I32_I16), RegHi).addReg(RegHi));
      Adjust += 4;
    }

    if (OpLo.isGlobal())
      OpLo.setOffset(OpLo.getOffset() + Adjust + 4);
    Bundler.append(
        BuildMI(MF, DL, get(AMDGPU::S_ADD_U32), RegLo).addReg(RegLo).add(OpLo));

    if (OpHi.isGlobal())
      OpHi.setOffset(OpHi.getOffset() + Adjust + 12);
    Bundler.append(BuildMI(MF, DL, get(AMDGPU::S_ADDC_U32), RegHi)
                       .addReg(RegHi)
                       .add(OpHi));

    finalizeBundle(MBB, Bundler.begin());

    MI.eraseFromParent();
    break;
  }
  case AMDGPU::ENTER_STRICT_WWM: {
    // This only gets its own opcode so that SIPreAllocateWWMRegs can tell when
    // Whole Wave Mode is entered.
    MI.setDesc(get(ST.isWave32() ? AMDGPU::S_OR_SAVEEXEC_B32
                                 : AMDGPU::S_OR_SAVEEXEC_B64));
    break;
  }
  case AMDGPU::ENTER_STRICT_WQM: {
    // This only gets its own opcode so that SIPreAllocateWWMRegs can tell when
    // STRICT_WQM is entered.
    const unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
    const unsigned WQMOp = ST.isWave32() ? AMDGPU::S_WQM_B32 : AMDGPU::S_WQM_B64;
    const unsigned MovOp = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
    BuildMI(MBB, MI, DL, get(MovOp), MI.getOperand(0).getReg()).addReg(Exec);
    BuildMI(MBB, MI, DL, get(WQMOp), Exec).addReg(Exec);

    MI.eraseFromParent();
    break;
  }
  case AMDGPU::EXIT_STRICT_WWM:
  case AMDGPU::EXIT_STRICT_WQM: {
    // This only gets its own opcode so that SIPreAllocateWWMRegs can tell when
    // WWM/STICT_WQM is exited.
    MI.setDesc(get(ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64));
    break;
  }
  case AMDGPU::SI_RETURN: {
    const MachineFunction *MF = MBB.getParent();
    const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
    const SIRegisterInfo *TRI = ST.getRegisterInfo();
    // Hiding the return address use with SI_RETURN may lead to extra kills in
    // the function and missing live-ins. We are fine in practice because callee
    // saved register handling ensures the register value is restored before
    // RET, but we need the undef flag here to appease the MachineVerifier
    // liveness checks.
    MachineInstrBuilder MIB =
        BuildMI(MBB, MI, DL, get(AMDGPU::S_SETPC_B64_return))
            .addReg(TRI->getReturnAddressReg(*MF), RegState::Undef);

    MIB.copyImplicitOps(MI);
    MI.eraseFromParent();
    break;
  }

  case AMDGPU::S_MUL_U64_U32_PSEUDO:
  case AMDGPU::S_MUL_I64_I32_PSEUDO:
    MI.setDesc(get(AMDGPU::S_MUL_U64));
    break;

  case AMDGPU::S_GETPC_B64_pseudo:
    MI.setDesc(get(AMDGPU::S_GETPC_B64));
    if (ST.hasGetPCZeroExtension()) {
      Register Dst = MI.getOperand(0).getReg();
      Register DstHi = RI.getSubReg(Dst, AMDGPU::sub1);
      // Fix up hardware that does not sign-extend the 48-bit PC value by
      // inserting: s_sext_i32_i16 dsthi, dsthi
      BuildMI(MBB, std::next(MI.getIterator()), DL, get(AMDGPU::S_SEXT_I32_I16),
              DstHi)
          .addReg(DstHi);
    }
    break;
  }
  return true;
}

void SIInstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I, Register DestReg,
                                unsigned SubIdx, const MachineInstr &Orig,
                                const TargetRegisterInfo &RI) const {

  // Try shrinking the instruction to remat only the part needed for current
  // context.
  // TODO: Handle more cases.
  unsigned Opcode = Orig.getOpcode();
  switch (Opcode) {
  case AMDGPU::S_LOAD_DWORDX16_IMM:
  case AMDGPU::S_LOAD_DWORDX8_IMM: {
    if (SubIdx != 0)
      break;

    if (I == MBB.end())
      break;

    if (I->isBundled())
      break;

    // Look for a single use of the register that is also a subreg.
    Register RegToFind = Orig.getOperand(0).getReg();
    MachineOperand *UseMO = nullptr;
    for (auto &CandMO : I->operands()) {
      if (!CandMO.isReg() || CandMO.getReg() != RegToFind || CandMO.isDef())
        continue;
      if (UseMO) {
        UseMO = nullptr;
        break;
      }
      UseMO = &CandMO;
    }
    if (!UseMO || UseMO->getSubReg() == AMDGPU::NoSubRegister)
      break;

    unsigned Offset = RI.getSubRegIdxOffset(UseMO->getSubReg());
    unsigned SubregSize = RI.getSubRegIdxSize(UseMO->getSubReg());

    MachineFunction *MF = MBB.getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    assert(MRI.use_nodbg_empty(DestReg) && "DestReg should have no users yet.");

    unsigned NewOpcode = -1;
    if (SubregSize == 256)
      NewOpcode = AMDGPU::S_LOAD_DWORDX8_IMM;
    else if (SubregSize == 128)
      NewOpcode = AMDGPU::S_LOAD_DWORDX4_IMM;
    else
      break;

    const MCInstrDesc &TID = get(NewOpcode);
    const TargetRegisterClass *NewRC =
        RI.getAllocatableClass(getRegClass(TID, 0, &RI, *MF));
    MRI.setRegClass(DestReg, NewRC);

    UseMO->setReg(DestReg);
    UseMO->setSubReg(AMDGPU::NoSubRegister);

    // Use a smaller load with the desired size, possibly with updated offset.
    MachineInstr *MI = MF->CloneMachineInstr(&Orig);
    MI->setDesc(TID);
    MI->getOperand(0).setReg(DestReg);
    MI->getOperand(0).setSubReg(AMDGPU::NoSubRegister);
    if (Offset) {
      MachineOperand *OffsetMO = getNamedOperand(*MI, AMDGPU::OpName::offset);
      int64_t FinalOffset = OffsetMO->getImm() + Offset / 8;
      OffsetMO->setImm(FinalOffset);
    }
    SmallVector<MachineMemOperand *> NewMMOs;
    for (const MachineMemOperand *MemOp : Orig.memoperands())
      NewMMOs.push_back(MF->getMachineMemOperand(MemOp, MemOp->getPointerInfo(),
                                                 SubregSize / 8));
    MI->setMemRefs(*MF, NewMMOs);

    MBB.insert(I, MI);
    return;
  }

  default:
    break;
  }

  TargetInstrInfo::reMaterialize(MBB, I, DestReg, SubIdx, Orig, RI);
}

std::pair<MachineInstr*, MachineInstr*>
SIInstrInfo::expandMovDPP64(MachineInstr &MI) const {
  assert (MI.getOpcode() == AMDGPU::V_MOV_B64_DPP_PSEUDO);

  if (ST.hasMovB64() &&
      AMDGPU::isLegalDPALU_DPPControl(
        getNamedOperand(MI, AMDGPU::OpName::dpp_ctrl)->getImm())) {
    MI.setDesc(get(AMDGPU::V_MOV_B64_dpp));
    return std::pair(&MI, nullptr);
  }

  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MBB.findDebugLoc(MI);
  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  Register Dst = MI.getOperand(0).getReg();
  unsigned Part = 0;
  MachineInstr *Split[2];

  for (auto Sub : { AMDGPU::sub0, AMDGPU::sub1 }) {
    auto MovDPP = BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_dpp));
    if (Dst.isPhysical()) {
      MovDPP.addDef(RI.getSubReg(Dst, Sub));
    } else {
      assert(MRI.isSSA());
      auto Tmp = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      MovDPP.addDef(Tmp);
    }

    for (unsigned I = 1; I <= 2; ++I) { // old and src operands.
      const MachineOperand &SrcOp = MI.getOperand(I);
      assert(!SrcOp.isFPImm());
      if (SrcOp.isImm()) {
        APInt Imm(64, SrcOp.getImm());
        Imm.ashrInPlace(Part * 32);
        MovDPP.addImm(Imm.getLoBits(32).getZExtValue());
      } else {
        assert(SrcOp.isReg());
        Register Src = SrcOp.getReg();
        if (Src.isPhysical())
          MovDPP.addReg(RI.getSubReg(Src, Sub));
        else
          MovDPP.addReg(Src, SrcOp.isUndef() ? RegState::Undef : 0, Sub);
      }
    }

    for (const MachineOperand &MO : llvm::drop_begin(MI.explicit_operands(), 3))
      MovDPP.addImm(MO.getImm());

    Split[Part] = MovDPP;
    ++Part;
  }

  if (Dst.isVirtual())
    BuildMI(MBB, MI, DL, get(AMDGPU::REG_SEQUENCE), Dst)
      .addReg(Split[0]->getOperand(0).getReg())
      .addImm(AMDGPU::sub0)
      .addReg(Split[1]->getOperand(0).getReg())
      .addImm(AMDGPU::sub1);

  MI.eraseFromParent();
  return std::pair(Split[0], Split[1]);
}

std::optional<DestSourcePair>
SIInstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  if (MI.getOpcode() == AMDGPU::WWM_COPY)
    return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};

  return std::nullopt;
}

bool SIInstrInfo::swapSourceModifiers(MachineInstr &MI,
                                      MachineOperand &Src0,
                                      unsigned Src0OpName,
                                      MachineOperand &Src1,
                                      unsigned Src1OpName) const {
  MachineOperand *Src0Mods = getNamedOperand(MI, Src0OpName);
  if (!Src0Mods)
    return false;

  MachineOperand *Src1Mods = getNamedOperand(MI, Src1OpName);
  assert(Src1Mods &&
         "All commutable instructions have both src0 and src1 modifiers");

  int Src0ModsVal = Src0Mods->getImm();
  int Src1ModsVal = Src1Mods->getImm();

  Src1Mods->setImm(Src0ModsVal);
  Src0Mods->setImm(Src1ModsVal);
  return true;
}

static MachineInstr *swapRegAndNonRegOperand(MachineInstr &MI,
                                             MachineOperand &RegOp,
                                             MachineOperand &NonRegOp) {
  Register Reg = RegOp.getReg();
  unsigned SubReg = RegOp.getSubReg();
  bool IsKill = RegOp.isKill();
  bool IsDead = RegOp.isDead();
  bool IsUndef = RegOp.isUndef();
  bool IsDebug = RegOp.isDebug();

  if (NonRegOp.isImm())
    RegOp.ChangeToImmediate(NonRegOp.getImm());
  else if (NonRegOp.isFI())
    RegOp.ChangeToFrameIndex(NonRegOp.getIndex());
  else if (NonRegOp.isGlobal()) {
    RegOp.ChangeToGA(NonRegOp.getGlobal(), NonRegOp.getOffset(),
                     NonRegOp.getTargetFlags());
  } else
    return nullptr;

  // Make sure we don't reinterpret a subreg index in the target flags.
  RegOp.setTargetFlags(NonRegOp.getTargetFlags());

  NonRegOp.ChangeToRegister(Reg, false, false, IsKill, IsDead, IsUndef, IsDebug);
  NonRegOp.setSubReg(SubReg);

  return &MI;
}

MachineInstr *SIInstrInfo::commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                                  unsigned Src0Idx,
                                                  unsigned Src1Idx) const {
  assert(!NewMI && "this should never be used");

  unsigned Opc = MI.getOpcode();
  int CommutedOpcode = commuteOpcode(Opc);
  if (CommutedOpcode == -1)
    return nullptr;

  if (Src0Idx > Src1Idx)
    std::swap(Src0Idx, Src1Idx);

  assert(AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0) ==
           static_cast<int>(Src0Idx) &&
         AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1) ==
           static_cast<int>(Src1Idx) &&
         "inconsistency with findCommutedOpIndices");

  MachineOperand &Src0 = MI.getOperand(Src0Idx);
  MachineOperand &Src1 = MI.getOperand(Src1Idx);

  MachineInstr *CommutedMI = nullptr;
  if (Src0.isReg() && Src1.isReg()) {
    if (isOperandLegal(MI, Src1Idx, &Src0)) {
      // Be sure to copy the source modifiers to the right place.
      CommutedMI
        = TargetInstrInfo::commuteInstructionImpl(MI, NewMI, Src0Idx, Src1Idx);
    }

  } else if (Src0.isReg() && !Src1.isReg()) {
    // src0 should always be able to support any operand type, so no need to
    // check operand legality.
    CommutedMI = swapRegAndNonRegOperand(MI, Src0, Src1);
  } else if (!Src0.isReg() && Src1.isReg()) {
    if (isOperandLegal(MI, Src1Idx, &Src0))
      CommutedMI = swapRegAndNonRegOperand(MI, Src1, Src0);
  } else {
    // FIXME: Found two non registers to commute. This does happen.
    return nullptr;
  }

  if (CommutedMI) {
    swapSourceModifiers(MI, Src0, AMDGPU::OpName::src0_modifiers,
                        Src1, AMDGPU::OpName::src1_modifiers);

    CommutedMI->setDesc(get(CommutedOpcode));
  }

  return CommutedMI;
}

// This needs to be implemented because the source modifiers may be inserted
// between the true commutable operands, and the base
// TargetInstrInfo::commuteInstruction uses it.
bool SIInstrInfo::findCommutedOpIndices(const MachineInstr &MI,
                                        unsigned &SrcOpIdx0,
                                        unsigned &SrcOpIdx1) const {
  return findCommutedOpIndices(MI.getDesc(), SrcOpIdx0, SrcOpIdx1);
}

bool SIInstrInfo::findCommutedOpIndices(const MCInstrDesc &Desc,
                                        unsigned &SrcOpIdx0,
                                        unsigned &SrcOpIdx1) const {
  if (!Desc.isCommutable())
    return false;

  unsigned Opc = Desc.getOpcode();
  int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
  if (Src0Idx == -1)
    return false;

  int Src1Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1);
  if (Src1Idx == -1)
    return false;

  return fixCommutedOpIndices(SrcOpIdx0, SrcOpIdx1, Src0Idx, Src1Idx);
}

bool SIInstrInfo::isBranchOffsetInRange(unsigned BranchOp,
                                        int64_t BrOffset) const {
  // BranchRelaxation should never have to check s_setpc_b64 because its dest
  // block is unanalyzable.
  assert(BranchOp != AMDGPU::S_SETPC_B64);

  // Convert to dwords.
  BrOffset /= 4;

  // The branch instructions do PC += signext(SIMM16 * 4) + 4, so the offset is
  // from the next instruction.
  BrOffset -= 1;

  return isIntN(BranchOffsetBits, BrOffset);
}

MachineBasicBlock *
SIInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  return MI.getOperand(0).getMBB();
}

bool SIInstrInfo::hasDivergentBranch(const MachineBasicBlock *MBB) const {
  for (const MachineInstr &MI : MBB->terminators()) {
    if (MI.getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO ||
        MI.getOpcode() == AMDGPU::SI_IF || MI.getOpcode() == AMDGPU::SI_ELSE ||
        MI.getOpcode() == AMDGPU::SI_LOOP)
      return true;
  }
  return false;
}

void SIInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                       MachineBasicBlock &DestBB,
                                       MachineBasicBlock &RestoreBB,
                                       const DebugLoc &DL, int64_t BrOffset,
                                       RegScavenger *RS) const {
  assert(RS && "RegScavenger required for long branching");
  assert(MBB.empty() &&
         "new block should be inserted for expanding unconditional branch");
  assert(MBB.pred_size() == 1);
  assert(RestoreBB.empty() &&
         "restore block should be inserted for restoring clobbered registers");

  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();

  // FIXME: Virtual register workaround for RegScavenger not working with empty
  // blocks.
  Register PCReg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);

  auto I = MBB.end();

  // We need to compute the offset relative to the instruction immediately after
  // s_getpc_b64. Insert pc arithmetic code before last terminator.
  MachineInstr *GetPC = BuildMI(MBB, I, DL, get(AMDGPU::S_GETPC_B64), PCReg);

  auto &MCCtx = MF->getContext();
  MCSymbol *PostGetPCLabel =
      MCCtx.createTempSymbol("post_getpc", /*AlwaysAddSuffix=*/true);
  GetPC->setPostInstrSymbol(*MF, PostGetPCLabel);

  MCSymbol *OffsetLo =
      MCCtx.createTempSymbol("offset_lo", /*AlwaysAddSuffix=*/true);
  MCSymbol *OffsetHi =
      MCCtx.createTempSymbol("offset_hi", /*AlwaysAddSuffix=*/true);
  BuildMI(MBB, I, DL, get(AMDGPU::S_ADD_U32))
      .addReg(PCReg, RegState::Define, AMDGPU::sub0)
      .addReg(PCReg, 0, AMDGPU::sub0)
      .addSym(OffsetLo, MO_FAR_BRANCH_OFFSET);
  BuildMI(MBB, I, DL, get(AMDGPU::S_ADDC_U32))
      .addReg(PCReg, RegState::Define, AMDGPU::sub1)
      .addReg(PCReg, 0, AMDGPU::sub1)
      .addSym(OffsetHi, MO_FAR_BRANCH_OFFSET);

  // Insert the indirect branch after the other terminator.
  BuildMI(&MBB, DL, get(AMDGPU::S_SETPC_B64))
    .addReg(PCReg);

  // If a spill is needed for the pc register pair, we need to insert a spill
  // restore block right before the destination block, and insert a short branch
  // into the old destination block's fallthrough predecessor.
  // e.g.:
  //
  // s_cbranch_scc0 skip_long_branch:
  //
  // long_branch_bb:
  //   spill s[8:9]
  //   s_getpc_b64 s[8:9]
  //   s_add_u32 s8, s8, restore_bb
  //   s_addc_u32 s9, s9, 0
  //   s_setpc_b64 s[8:9]
  //
  // skip_long_branch:
  //   foo;
  //
  // .....
  //
  // dest_bb_fallthrough_predecessor:
  // bar;
  // s_branch dest_bb
  //
  // restore_bb:
  //  restore s[8:9]
  //  fallthrough dest_bb
  ///
  // dest_bb:
  //   buzz;

  Register LongBranchReservedReg = MFI->getLongBranchReservedReg();
  Register Scav;

  // If we've previously reserved a register for long branches
  // avoid running the scavenger and just use those registers
  if (LongBranchReservedReg) {
    RS->enterBasicBlock(MBB);
    Scav = LongBranchReservedReg;
  } else {
    RS->enterBasicBlockEnd(MBB);
    Scav = RS->scavengeRegisterBackwards(
        AMDGPU::SReg_64RegClass, MachineBasicBlock::iterator(GetPC),
        /* RestoreAfter */ false, 0, /* AllowSpill */ false);
  }
  if (Scav) {
    RS->setRegUsed(Scav);
    MRI.replaceRegWith(PCReg, Scav);
    MRI.clearVirtRegs();
  } else {
    // As SGPR needs VGPR to be spilled, we reuse the slot of temporary VGPR for
    // SGPR spill.
    const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
    const SIRegisterInfo *TRI = ST.getRegisterInfo();
    TRI->spillEmergencySGPR(GetPC, RestoreBB, AMDGPU::SGPR0_SGPR1, RS);
    MRI.replaceRegWith(PCReg, AMDGPU::SGPR0_SGPR1);
    MRI.clearVirtRegs();
  }

  MCSymbol *DestLabel = Scav ? DestBB.getSymbol() : RestoreBB.getSymbol();
  // Now, the distance could be defined.
  auto *Offset = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(DestLabel, MCCtx),
      MCSymbolRefExpr::create(PostGetPCLabel, MCCtx), MCCtx);
  // Add offset assignments.
  auto *Mask = MCConstantExpr::create(0xFFFFFFFFULL, MCCtx);
  OffsetLo->setVariableValue(MCBinaryExpr::createAnd(Offset, Mask, MCCtx));
  auto *ShAmt = MCConstantExpr::create(32, MCCtx);
  OffsetHi->setVariableValue(MCBinaryExpr::createAShr(Offset, ShAmt, MCCtx));
}

unsigned SIInstrInfo::getBranchOpcode(SIInstrInfo::BranchPredicate Cond) {
  switch (Cond) {
  case SIInstrInfo::SCC_TRUE:
    return AMDGPU::S_CBRANCH_SCC1;
  case SIInstrInfo::SCC_FALSE:
    return AMDGPU::S_CBRANCH_SCC0;
  case SIInstrInfo::VCCNZ:
    return AMDGPU::S_CBRANCH_VCCNZ;
  case SIInstrInfo::VCCZ:
    return AMDGPU::S_CBRANCH_VCCZ;
  case SIInstrInfo::EXECNZ:
    return AMDGPU::S_CBRANCH_EXECNZ;
  case SIInstrInfo::EXECZ:
    return AMDGPU::S_CBRANCH_EXECZ;
  default:
    llvm_unreachable("invalid branch predicate");
  }
}

SIInstrInfo::BranchPredicate SIInstrInfo::getBranchPredicate(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::S_CBRANCH_SCC0:
    return SCC_FALSE;
  case AMDGPU::S_CBRANCH_SCC1:
    return SCC_TRUE;
  case AMDGPU::S_CBRANCH_VCCNZ:
    return VCCNZ;
  case AMDGPU::S_CBRANCH_VCCZ:
    return VCCZ;
  case AMDGPU::S_CBRANCH_EXECNZ:
    return EXECNZ;
  case AMDGPU::S_CBRANCH_EXECZ:
    return EXECZ;
  default:
    return INVALID_BR;
  }
}

bool SIInstrInfo::analyzeBranchImpl(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    MachineBasicBlock *&TBB,
                                    MachineBasicBlock *&FBB,
                                    SmallVectorImpl<MachineOperand> &Cond,
                                    bool AllowModify) const {
  if (I->getOpcode() == AMDGPU::S_BRANCH) {
    // Unconditional Branch
    TBB = I->getOperand(0).getMBB();
    return false;
  }

  MachineBasicBlock *CondBB = nullptr;

  if (I->getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO) {
    CondBB = I->getOperand(1).getMBB();
    Cond.push_back(I->getOperand(0));
  } else {
    BranchPredicate Pred = getBranchPredicate(I->getOpcode());
    if (Pred == INVALID_BR)
      return true;

    CondBB = I->getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(Pred));
    Cond.push_back(I->getOperand(1)); // Save the branch register.
  }
  ++I;

  if (I == MBB.end()) {
    // Conditional branch followed by fall-through.
    TBB = CondBB;
    return false;
  }

  if (I->getOpcode() == AMDGPU::S_BRANCH) {
    TBB = CondBB;
    FBB = I->getOperand(0).getMBB();
    return false;
  }

  return true;
}

bool SIInstrInfo::analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                                MachineBasicBlock *&FBB,
                                SmallVectorImpl<MachineOperand> &Cond,
                                bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.getFirstTerminator();
  auto E = MBB.end();
  if (I == E)
    return false;

  // Skip over the instructions that are artificially terminators for special
  // exec management.
  while (I != E && !I->isBranch() && !I->isReturn()) {
    switch (I->getOpcode()) {
    case AMDGPU::S_MOV_B64_term:
    case AMDGPU::S_XOR_B64_term:
    case AMDGPU::S_OR_B64_term:
    case AMDGPU::S_ANDN2_B64_term:
    case AMDGPU::S_AND_B64_term:
    case AMDGPU::S_AND_SAVEEXEC_B64_term:
    case AMDGPU::S_MOV_B32_term:
    case AMDGPU::S_XOR_B32_term:
    case AMDGPU::S_OR_B32_term:
    case AMDGPU::S_ANDN2_B32_term:
    case AMDGPU::S_AND_B32_term:
    case AMDGPU::S_AND_SAVEEXEC_B32_term:
      break;
    case AMDGPU::SI_IF:
    case AMDGPU::SI_ELSE:
    case AMDGPU::SI_KILL_I1_TERMINATOR:
    case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
      // FIXME: It's messy that these need to be considered here at all.
      return true;
    default:
      llvm_unreachable("unexpected non-branch terminator inst");
    }

    ++I;
  }

  if (I == E)
    return false;

  return analyzeBranchImpl(MBB, I, TBB, FBB, Cond, AllowModify);
}

unsigned SIInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                   int *BytesRemoved) const {
  unsigned Count = 0;
  unsigned RemovedSize = 0;
  for (MachineInstr &MI : llvm::make_early_inc_range(MBB.terminators())) {
    // Skip over artificial terminators when removing instructions.
    if (MI.isBranch() || MI.isReturn()) {
      RemovedSize += getInstSizeInBytes(MI);
      MI.eraseFromParent();
      ++Count;
    }
  }

  if (BytesRemoved)
    *BytesRemoved = RemovedSize;

  return Count;
}

// Copy the flags onto the implicit condition register operand.
static void preserveCondRegFlags(MachineOperand &CondReg,
                                 const MachineOperand &OrigCond) {
  CondReg.setIsUndef(OrigCond.isUndef());
  CondReg.setIsKill(OrigCond.isKill());
}

unsigned SIInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *TBB,
                                   MachineBasicBlock *FBB,
                                   ArrayRef<MachineOperand> Cond,
                                   const DebugLoc &DL,
                                   int *BytesAdded) const {
  if (!FBB && Cond.empty()) {
    BuildMI(&MBB, DL, get(AMDGPU::S_BRANCH))
      .addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = ST.hasOffset3fBug() ? 8 : 4;
    return 1;
  }

  if(Cond.size() == 1 && Cond[0].isReg()) {
     BuildMI(&MBB, DL, get(AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO))
       .add(Cond[0])
       .addMBB(TBB);
     return 1;
  }

  assert(TBB && Cond[0].isImm());

  unsigned Opcode
    = getBranchOpcode(static_cast<BranchPredicate>(Cond[0].getImm()));

  if (!FBB) {
    MachineInstr *CondBr =
      BuildMI(&MBB, DL, get(Opcode))
      .addMBB(TBB);

    // Copy the flags onto the implicit condition register operand.
    preserveCondRegFlags(CondBr->getOperand(1), Cond[1]);
    fixImplicitOperands(*CondBr);

    if (BytesAdded)
      *BytesAdded = ST.hasOffset3fBug() ? 8 : 4;
    return 1;
  }

  assert(TBB && FBB);

  MachineInstr *CondBr =
    BuildMI(&MBB, DL, get(Opcode))
    .addMBB(TBB);
  fixImplicitOperands(*CondBr);
  BuildMI(&MBB, DL, get(AMDGPU::S_BRANCH))
    .addMBB(FBB);

  MachineOperand &CondReg = CondBr->getOperand(1);
  CondReg.setIsUndef(Cond[1].isUndef());
  CondReg.setIsKill(Cond[1].isKill());

  if (BytesAdded)
    *BytesAdded = ST.hasOffset3fBug() ? 16 : 8;

  return 2;
}

bool SIInstrInfo::reverseBranchCondition(
  SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() != 2) {
    return true;
  }

  if (Cond[0].isImm()) {
    Cond[0].setImm(-Cond[0].getImm());
    return false;
  }

  return true;
}

bool SIInstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                  ArrayRef<MachineOperand> Cond,
                                  Register DstReg, Register TrueReg,
                                  Register FalseReg, int &CondCycles,
                                  int &TrueCycles, int &FalseCycles) const {
  switch (Cond[0].getImm()) {
  case VCCNZ:
  case VCCZ: {
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(TrueReg);
    if (MRI.getRegClass(FalseReg) != RC)
      return false;

    int NumInsts = AMDGPU::getRegBitWidth(*RC) / 32;
    CondCycles = TrueCycles = FalseCycles = NumInsts; // ???

    // Limit to equal cost for branch vs. N v_cndmask_b32s.
    return RI.hasVGPRs(RC) && NumInsts <= 6;
  }
  case SCC_TRUE:
  case SCC_FALSE: {
    // FIXME: We could insert for VGPRs if we could replace the original compare
    // with a vector one.
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(TrueReg);
    if (MRI.getRegClass(FalseReg) != RC)
      return false;

    int NumInsts = AMDGPU::getRegBitWidth(*RC) / 32;

    // Multiples of 8 can do s_cselect_b64
    if (NumInsts % 2 == 0)
      NumInsts /= 2;

    CondCycles = TrueCycles = FalseCycles = NumInsts; // ???
    return RI.isSGPRClass(RC);
  }
  default:
    return false;
  }
}

void SIInstrInfo::insertSelect(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I, const DebugLoc &DL,
                               Register DstReg, ArrayRef<MachineOperand> Cond,
                               Register TrueReg, Register FalseReg) const {
  BranchPredicate Pred = static_cast<BranchPredicate>(Cond[0].getImm());
  if (Pred == VCCZ || Pred == SCC_FALSE) {
    Pred = static_cast<BranchPredicate>(-Pred);
    std::swap(TrueReg, FalseReg);
  }

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *DstRC = MRI.getRegClass(DstReg);
  unsigned DstSize = RI.getRegSizeInBits(*DstRC);

  if (DstSize == 32) {
    MachineInstr *Select;
    if (Pred == SCC_TRUE) {
      Select = BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B32), DstReg)
        .addReg(TrueReg)
        .addReg(FalseReg);
    } else {
      // Instruction's operands are backwards from what is expected.
      Select = BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e32), DstReg)
        .addReg(FalseReg)
        .addReg(TrueReg);
    }

    preserveCondRegFlags(Select->getOperand(3), Cond[1]);
    return;
  }

  if (DstSize == 64 && Pred == SCC_TRUE) {
    MachineInstr *Select =
      BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B64), DstReg)
      .addReg(TrueReg)
      .addReg(FalseReg);

    preserveCondRegFlags(Select->getOperand(3), Cond[1]);
    return;
  }

  static const int16_t Sub0_15[] = {
    AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2, AMDGPU::sub3,
    AMDGPU::sub4, AMDGPU::sub5, AMDGPU::sub6, AMDGPU::sub7,
    AMDGPU::sub8, AMDGPU::sub9, AMDGPU::sub10, AMDGPU::sub11,
    AMDGPU::sub12, AMDGPU::sub13, AMDGPU::sub14, AMDGPU::sub15,
  };

  static const int16_t Sub0_15_64[] = {
    AMDGPU::sub0_sub1, AMDGPU::sub2_sub3,
    AMDGPU::sub4_sub5, AMDGPU::sub6_sub7,
    AMDGPU::sub8_sub9, AMDGPU::sub10_sub11,
    AMDGPU::sub12_sub13, AMDGPU::sub14_sub15,
  };

  unsigned SelOp = AMDGPU::V_CNDMASK_B32_e32;
  const TargetRegisterClass *EltRC = &AMDGPU::VGPR_32RegClass;
  const int16_t *SubIndices = Sub0_15;
  int NElts = DstSize / 32;

  // 64-bit select is only available for SALU.
  // TODO: Split 96-bit into 64-bit and 32-bit, not 3x 32-bit.
  if (Pred == SCC_TRUE) {
    if (NElts % 2) {
      SelOp = AMDGPU::S_CSELECT_B32;
      EltRC = &AMDGPU::SGPR_32RegClass;
    } else {
      SelOp = AMDGPU::S_CSELECT_B64;
      EltRC = &AMDGPU::SGPR_64RegClass;
      SubIndices = Sub0_15_64;
      NElts /= 2;
    }
  }

  MachineInstrBuilder MIB = BuildMI(
    MBB, I, DL, get(AMDGPU::REG_SEQUENCE), DstReg);

  I = MIB->getIterator();

  SmallVector<Register, 8> Regs;
  for (int Idx = 0; Idx != NElts; ++Idx) {
    Register DstElt = MRI.createVirtualRegister(EltRC);
    Regs.push_back(DstElt);

    unsigned SubIdx = SubIndices[Idx];

    MachineInstr *Select;
    if (SelOp == AMDGPU::V_CNDMASK_B32_e32) {
      Select =
        BuildMI(MBB, I, DL, get(SelOp), DstElt)
        .addReg(FalseReg, 0, SubIdx)
        .addReg(TrueReg, 0, SubIdx);
    } else {
      Select =
        BuildMI(MBB, I, DL, get(SelOp), DstElt)
        .addReg(TrueReg, 0, SubIdx)
        .addReg(FalseReg, 0, SubIdx);
    }

    preserveCondRegFlags(Select->getOperand(3), Cond[1]);
    fixImplicitOperands(*Select);

    MIB.addReg(DstElt)
       .addImm(SubIdx);
  }
}

bool SIInstrInfo::isFoldableCopy(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::V_MOV_B32_e32:
  case AMDGPU::V_MOV_B32_e64:
  case AMDGPU::V_MOV_B64_PSEUDO:
  case AMDGPU::V_MOV_B64_e32:
  case AMDGPU::V_MOV_B64_e64:
  case AMDGPU::S_MOV_B32:
  case AMDGPU::S_MOV_B64:
  case AMDGPU::S_MOV_B64_IMM_PSEUDO:
  case AMDGPU::COPY:
  case AMDGPU::WWM_COPY:
  case AMDGPU::V_ACCVGPR_WRITE_B32_e64:
  case AMDGPU::V_ACCVGPR_READ_B32_e64:
  case AMDGPU::V_ACCVGPR_MOV_B32:
    return true;
  default:
    return false;
  }
}

static constexpr unsigned ModifierOpNames[] = {
    AMDGPU::OpName::src0_modifiers, AMDGPU::OpName::src1_modifiers,
    AMDGPU::OpName::src2_modifiers, AMDGPU::OpName::clamp,
    AMDGPU::OpName::omod,           AMDGPU::OpName::op_sel};

void SIInstrInfo::removeModOperands(MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  for (unsigned Name : reverse(ModifierOpNames)) {
    int Idx = AMDGPU::getNamedOperandIdx(Opc, Name);
    if (Idx >= 0)
      MI.removeOperand(Idx);
  }
}

bool SIInstrInfo::foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                Register Reg, MachineRegisterInfo *MRI) const {
  if (!MRI->hasOneNonDBGUse(Reg))
    return false;

  switch (DefMI.getOpcode()) {
  default:
    return false;
  case AMDGPU::V_MOV_B64_e32:
  case AMDGPU::S_MOV_B64:
  case AMDGPU::V_MOV_B64_PSEUDO:
  case AMDGPU::S_MOV_B64_IMM_PSEUDO:
  case AMDGPU::V_MOV_B32_e32:
  case AMDGPU::S_MOV_B32:
  case AMDGPU::V_ACCVGPR_WRITE_B32_e64:
    break;
  }

  const MachineOperand *ImmOp = getNamedOperand(DefMI, AMDGPU::OpName::src0);
  assert(ImmOp);
  // FIXME: We could handle FrameIndex values here.
  if (!ImmOp->isImm())
    return false;

  auto getImmFor = [ImmOp](const MachineOperand &UseOp) -> int64_t {
    int64_t Imm = ImmOp->getImm();
    switch (UseOp.getSubReg()) {
    default:
      return Imm;
    case AMDGPU::sub0:
      return Lo_32(Imm);
    case AMDGPU::sub1:
      return Hi_32(Imm);
    case AMDGPU::lo16:
      return APInt(16, Imm).getSExtValue();
    case AMDGPU::hi16:
      return APInt(32, Imm).ashr(16).getSExtValue();
    case AMDGPU::sub1_lo16:
      return APInt(16, Hi_32(Imm)).getSExtValue();
    case AMDGPU::sub1_hi16:
      return APInt(32, Hi_32(Imm)).ashr(16).getSExtValue();
    }
  };

  assert(!DefMI.getOperand(0).getSubReg() && "Expected SSA form");

  unsigned Opc = UseMI.getOpcode();
  if (Opc == AMDGPU::COPY) {
    assert(!UseMI.getOperand(0).getSubReg() && "Expected SSA form");

    Register DstReg = UseMI.getOperand(0).getReg();
    unsigned OpSize = getOpSize(UseMI, 0);
    bool Is16Bit = OpSize == 2;
    bool Is64Bit = OpSize == 8;
    bool isVGPRCopy = RI.isVGPR(*MRI, DstReg);
    unsigned NewOpc = isVGPRCopy ? Is64Bit ? AMDGPU::V_MOV_B64_PSEUDO
                                           : AMDGPU::V_MOV_B32_e32
                                 : Is64Bit ? AMDGPU::S_MOV_B64_IMM_PSEUDO
                                           : AMDGPU::S_MOV_B32;
    APInt Imm(Is64Bit ? 64 : 32, getImmFor(UseMI.getOperand(1)));

    if (RI.isAGPR(*MRI, DstReg)) {
      if (Is64Bit || !isInlineConstant(Imm))
        return false;
      NewOpc = AMDGPU::V_ACCVGPR_WRITE_B32_e64;
    }

    if (Is16Bit) {
      if (isVGPRCopy)
        return false; // Do not clobber vgpr_hi16

      if (DstReg.isVirtual() && UseMI.getOperand(0).getSubReg() != AMDGPU::lo16)
        return false;

      UseMI.getOperand(0).setSubReg(0);
      if (DstReg.isPhysical()) {
        DstReg = RI.get32BitRegister(DstReg);
        UseMI.getOperand(0).setReg(DstReg);
      }
      assert(UseMI.getOperand(1).getReg().isVirtual());
    }

    const MCInstrDesc &NewMCID = get(NewOpc);
    if (DstReg.isPhysical() &&
        !RI.getRegClass(NewMCID.operands()[0].RegClass)->contains(DstReg))
      return false;

    UseMI.setDesc(NewMCID);
    UseMI.getOperand(1).ChangeToImmediate(Imm.getSExtValue());
    UseMI.addImplicitDefUseOperands(*UseMI.getParent()->getParent());
    return true;
  }

  if (Opc == AMDGPU::V_MAD_F32_e64 || Opc == AMDGPU::V_MAC_F32_e64 ||
      Opc == AMDGPU::V_MAD_F16_e64 || Opc == AMDGPU::V_MAC_F16_e64 ||
      Opc == AMDGPU::V_FMA_F32_e64 || Opc == AMDGPU::V_FMAC_F32_e64 ||
      Opc == AMDGPU::V_FMA_F16_e64 || Opc == AMDGPU::V_FMAC_F16_e64 ||
      Opc == AMDGPU::V_FMAC_F16_t16_e64) {
    // Don't fold if we are using source or output modifiers. The new VOP2
    // instructions don't have them.
    if (hasAnyModifiersSet(UseMI))
      return false;

    // If this is a free constant, there's no reason to do this.
    // TODO: We could fold this here instead of letting SIFoldOperands do it
    // later.
    MachineOperand *Src0 = getNamedOperand(UseMI, AMDGPU::OpName::src0);

    // Any src operand can be used for the legality check.
    if (isInlineConstant(UseMI, *Src0, *ImmOp))
      return false;

    bool IsF32 = Opc == AMDGPU::V_MAD_F32_e64 || Opc == AMDGPU::V_MAC_F32_e64 ||
                 Opc == AMDGPU::V_FMA_F32_e64 || Opc == AMDGPU::V_FMAC_F32_e64;
    bool IsFMA =
        Opc == AMDGPU::V_FMA_F32_e64 || Opc == AMDGPU::V_FMAC_F32_e64 ||
        Opc == AMDGPU::V_FMA_F16_e64 || Opc == AMDGPU::V_FMAC_F16_e64 ||
        Opc == AMDGPU::V_FMAC_F16_t16_e64;
    MachineOperand *Src1 = getNamedOperand(UseMI, AMDGPU::OpName::src1);
    MachineOperand *Src2 = getNamedOperand(UseMI, AMDGPU::OpName::src2);

    // Multiplied part is the constant: Use v_madmk_{f16, f32}.
    if ((Src0->isReg() && Src0->getReg() == Reg) ||
        (Src1->isReg() && Src1->getReg() == Reg)) {
      MachineOperand *RegSrc =
          Src1->isReg() && Src1->getReg() == Reg ? Src0 : Src1;
      if (!RegSrc->isReg())
        return false;
      if (RI.isSGPRClass(MRI->getRegClass(RegSrc->getReg())) &&
          ST.getConstantBusLimit(Opc) < 2)
        return false;

      if (!Src2->isReg() || RI.isSGPRClass(MRI->getRegClass(Src2->getReg())))
        return false;

      // If src2 is also a literal constant then we have to choose which one to
      // fold. In general it is better to choose madak so that the other literal
      // can be materialized in an sgpr instead of a vgpr:
      //   s_mov_b32 s0, literal
      //   v_madak_f32 v0, s0, v0, literal
      // Instead of:
      //   v_mov_b32 v1, literal
      //   v_madmk_f32 v0, v0, literal, v1
      MachineInstr *Def = MRI->getUniqueVRegDef(Src2->getReg());
      if (Def && Def->isMoveImmediate() &&
          !isInlineConstant(Def->getOperand(1)))
        return false;

      unsigned NewOpc =
          IsFMA ? (IsF32                    ? AMDGPU::V_FMAMK_F32
                   : ST.hasTrue16BitInsts() ? AMDGPU::V_FMAMK_F16_t16
                                            : AMDGPU::V_FMAMK_F16)
                : (IsF32 ? AMDGPU::V_MADMK_F32 : AMDGPU::V_MADMK_F16);
      if (pseudoToMCOpcode(NewOpc) == -1)
        return false;

      // V_FMAMK_F16_t16 takes VGPR_32_Lo128 operands, so the rewrite
      // would also require restricting their register classes. For now
      // just bail out.
      if (NewOpc == AMDGPU::V_FMAMK_F16_t16)
        return false;

      const int64_t Imm = getImmFor(RegSrc == Src1 ? *Src0 : *Src1);

      // FIXME: This would be a lot easier if we could return a new instruction
      // instead of having to modify in place.

      Register SrcReg = RegSrc->getReg();
      unsigned SrcSubReg = RegSrc->getSubReg();
      Src0->setReg(SrcReg);
      Src0->setSubReg(SrcSubReg);
      Src0->setIsKill(RegSrc->isKill());

      if (Opc == AMDGPU::V_MAC_F32_e64 || Opc == AMDGPU::V_MAC_F16_e64 ||
          Opc == AMDGPU::V_FMAC_F32_e64 || Opc == AMDGPU::V_FMAC_F16_t16_e64 ||
          Opc == AMDGPU::V_FMAC_F16_e64)
        UseMI.untieRegOperand(
            AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2));

      Src1->ChangeToImmediate(Imm);

      removeModOperands(UseMI);
      UseMI.setDesc(get(NewOpc));

      bool DeleteDef = MRI->use_nodbg_empty(Reg);
      if (DeleteDef)
        DefMI.eraseFromParent();

      return true;
    }

    // Added part is the constant: Use v_madak_{f16, f32}.
    if (Src2->isReg() && Src2->getReg() == Reg) {
      if (ST.getConstantBusLimit(Opc) < 2) {
        // Not allowed to use constant bus for another operand.
        // We can however allow an inline immediate as src0.
        bool Src0Inlined = false;
        if (Src0->isReg()) {
          // Try to inline constant if possible.
          // If the Def moves immediate and the use is single
          // We are saving VGPR here.
          MachineInstr *Def = MRI->getUniqueVRegDef(Src0->getReg());
          if (Def && Def->isMoveImmediate() &&
              isInlineConstant(Def->getOperand(1)) &&
              MRI->hasOneUse(Src0->getReg())) {
            Src0->ChangeToImmediate(Def->getOperand(1).getImm());
            Src0Inlined = true;
          } else if (ST.getConstantBusLimit(Opc) <= 1 &&
                     RI.isSGPRReg(*MRI, Src0->getReg())) {
            return false;
          }
          // VGPR is okay as Src0 - fallthrough
        }

        if (Src1->isReg() && !Src0Inlined) {
          // We have one slot for inlinable constant so far - try to fill it
          MachineInstr *Def = MRI->getUniqueVRegDef(Src1->getReg());
          if (Def && Def->isMoveImmediate() &&
              isInlineConstant(Def->getOperand(1)) &&
              MRI->hasOneUse(Src1->getReg()) && commuteInstruction(UseMI))
            Src0->ChangeToImmediate(Def->getOperand(1).getImm());
          else if (RI.isSGPRReg(*MRI, Src1->getReg()))
            return false;
          // VGPR is okay as Src1 - fallthrough
        }
      }

      unsigned NewOpc =
          IsFMA ? (IsF32                    ? AMDGPU::V_FMAAK_F32
                   : ST.hasTrue16BitInsts() ? AMDGPU::V_FMAAK_F16_t16
                                            : AMDGPU::V_FMAAK_F16)
                : (IsF32 ? AMDGPU::V_MADAK_F32 : AMDGPU::V_MADAK_F16);
      if (pseudoToMCOpcode(NewOpc) == -1)
        return false;

      // V_FMAAK_F16_t16 takes VGPR_32_Lo128 operands, so the rewrite
      // would also require restricting their register classes. For now
      // just bail out.
      if (NewOpc == AMDGPU::V_FMAAK_F16_t16)
        return false;

      // FIXME: This would be a lot easier if we could return a new instruction
      // instead of having to modify in place.

      if (Opc == AMDGPU::V_MAC_F32_e64 || Opc == AMDGPU::V_MAC_F16_e64 ||
          Opc == AMDGPU::V_FMAC_F32_e64 || Opc == AMDGPU::V_FMAC_F16_t16_e64 ||
          Opc == AMDGPU::V_FMAC_F16_e64)
        UseMI.untieRegOperand(
            AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2));

      // ChangingToImmediate adds Src2 back to the instruction.
      Src2->ChangeToImmediate(getImmFor(*Src2));

      // These come before src2.
      removeModOperands(UseMI);
      UseMI.setDesc(get(NewOpc));
      // It might happen that UseMI was commuted
      // and we now have SGPR as SRC1. If so 2 inlined
      // constant and SGPR are illegal.
      legalizeOperands(UseMI);

      bool DeleteDef = MRI->use_nodbg_empty(Reg);
      if (DeleteDef)
        DefMI.eraseFromParent();

      return true;
    }
  }

  return false;
}

static bool
memOpsHaveSameBaseOperands(ArrayRef<const MachineOperand *> BaseOps1,
                           ArrayRef<const MachineOperand *> BaseOps2) {
  if (BaseOps1.size() != BaseOps2.size())
    return false;
  for (size_t I = 0, E = BaseOps1.size(); I < E; ++I) {
    if (!BaseOps1[I]->isIdenticalTo(*BaseOps2[I]))
      return false;
  }
  return true;
}

static bool offsetsDoNotOverlap(LocationSize WidthA, int OffsetA,
                                LocationSize WidthB, int OffsetB) {
  int LowOffset = OffsetA < OffsetB ? OffsetA : OffsetB;
  int HighOffset = OffsetA < OffsetB ? OffsetB : OffsetA;
  LocationSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
  return LowWidth.hasValue() &&
         LowOffset + (int)LowWidth.getValue() <= HighOffset;
}

bool SIInstrInfo::checkInstOffsetsDoNotOverlap(const MachineInstr &MIa,
                                               const MachineInstr &MIb) const {
  SmallVector<const MachineOperand *, 4> BaseOps0, BaseOps1;
  int64_t Offset0, Offset1;
  LocationSize Dummy0 = 0, Dummy1 = 0;
  bool Offset0IsScalable, Offset1IsScalable;
  if (!getMemOperandsWithOffsetWidth(MIa, BaseOps0, Offset0, Offset0IsScalable,
                                     Dummy0, &RI) ||
      !getMemOperandsWithOffsetWidth(MIb, BaseOps1, Offset1, Offset1IsScalable,
                                     Dummy1, &RI))
    return false;

  if (!memOpsHaveSameBaseOperands(BaseOps0, BaseOps1))
    return false;

  if (!MIa.hasOneMemOperand() || !MIb.hasOneMemOperand()) {
    // FIXME: Handle ds_read2 / ds_write2.
    return false;
  }
  LocationSize Width0 = MIa.memoperands().front()->getSize();
  LocationSize Width1 = MIb.memoperands().front()->getSize();
  return offsetsDoNotOverlap(Width0, Offset0, Width1, Offset1);
}

bool SIInstrInfo::areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                                  const MachineInstr &MIb) const {
  assert(MIa.mayLoadOrStore() &&
         "MIa must load from or modify a memory location");
  assert(MIb.mayLoadOrStore() &&
         "MIb must load from or modify a memory location");

  if (MIa.hasUnmodeledSideEffects() || MIb.hasUnmodeledSideEffects())
    return false;

  // XXX - Can we relax this between address spaces?
  if (MIa.hasOrderedMemoryRef() || MIb.hasOrderedMemoryRef())
    return false;

  if (isLDSDMA(MIa) || isLDSDMA(MIb))
    return false;

  // TODO: Should we check the address space from the MachineMemOperand? That
  // would allow us to distinguish objects we know don't alias based on the
  // underlying address space, even if it was lowered to a different one,
  // e.g. private accesses lowered to use MUBUF instructions on a scratch
  // buffer.
  if (isDS(MIa)) {
    if (isDS(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    return !isFLAT(MIb) || isSegmentSpecificFLAT(MIb);
  }

  if (isMUBUF(MIa) || isMTBUF(MIa)) {
    if (isMUBUF(MIb) || isMTBUF(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    if (isFLAT(MIb))
      return isFLATScratch(MIb);

    return !isSMRD(MIb);
  }

  if (isSMRD(MIa)) {
    if (isSMRD(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    if (isFLAT(MIb))
      return isFLATScratch(MIb);

    return !isMUBUF(MIb) && !isMTBUF(MIb);
  }

  if (isFLAT(MIa)) {
    if (isFLAT(MIb)) {
      if ((isFLATScratch(MIa) && isFLATGlobal(MIb)) ||
          (isFLATGlobal(MIa) && isFLATScratch(MIb)))
        return true;

      return checkInstOffsetsDoNotOverlap(MIa, MIb);
    }

    return false;
  }

  return false;
}

static bool getFoldableImm(Register Reg, const MachineRegisterInfo &MRI,
                           int64_t &Imm, MachineInstr **DefMI = nullptr) {
  if (Reg.isPhysical())
    return false;
  auto *Def = MRI.getUniqueVRegDef(Reg);
  if (Def && SIInstrInfo::isFoldableCopy(*Def) && Def->getOperand(1).isImm()) {
    Imm = Def->getOperand(1).getImm();
    if (DefMI)
      *DefMI = Def;
    return true;
  }
  return false;
}

static bool getFoldableImm(const MachineOperand *MO, int64_t &Imm,
                           MachineInstr **DefMI = nullptr) {
  if (!MO->isReg())
    return false;
  const MachineFunction *MF = MO->getParent()->getParent()->getParent();
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  return getFoldableImm(MO->getReg(), MRI, Imm, DefMI);
}

static void updateLiveVariables(LiveVariables *LV, MachineInstr &MI,
                                MachineInstr &NewMI) {
  if (LV) {
    unsigned NumOps = MI.getNumOperands();
    for (unsigned I = 1; I < NumOps; ++I) {
      MachineOperand &Op = MI.getOperand(I);
      if (Op.isReg() && Op.isKill())
        LV->replaceKillInstruction(Op.getReg(), MI, NewMI);
    }
  }
}

MachineInstr *SIInstrInfo::convertToThreeAddress(MachineInstr &MI,
                                                 LiveVariables *LV,
                                                 LiveIntervals *LIS) const {
  MachineBasicBlock &MBB = *MI.getParent();
  unsigned Opc = MI.getOpcode();

  // Handle MFMA.
  int NewMFMAOpc = AMDGPU::getMFMAEarlyClobberOp(Opc);
  if (NewMFMAOpc != -1) {
    MachineInstrBuilder MIB =
        BuildMI(MBB, MI, MI.getDebugLoc(), get(NewMFMAOpc));
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I)
      MIB.add(MI.getOperand(I));
    updateLiveVariables(LV, MI, *MIB);
    if (LIS) {
      LIS->ReplaceMachineInstrInMaps(MI, *MIB);
      // SlotIndex of defs needs to be updated when converting to early-clobber
      MachineOperand &Def = MIB->getOperand(0);
      if (Def.isEarlyClobber() && Def.isReg() &&
          LIS->hasInterval(Def.getReg())) {
        SlotIndex OldIndex = LIS->getInstructionIndex(*MIB).getRegSlot(false);
        SlotIndex NewIndex = LIS->getInstructionIndex(*MIB).getRegSlot(true);
        auto &LI = LIS->getInterval(Def.getReg());
        auto UpdateDefIndex = [&](LiveRange &LR) {
          auto S = LR.find(OldIndex);
          if (S != LR.end() && S->start == OldIndex) {
            assert(S->valno && S->valno->def == OldIndex);
            S->start = NewIndex;
            S->valno->def = NewIndex;
          }
        };
        UpdateDefIndex(LI);
        for (auto &SR : LI.subranges())
          UpdateDefIndex(SR);
      }
    }
    return MIB;
  }

  if (SIInstrInfo::isWMMA(MI)) {
    unsigned NewOpc = AMDGPU::mapWMMA2AddrTo3AddrOpcode(MI.getOpcode());
    MachineInstrBuilder MIB = BuildMI(MBB, MI, MI.getDebugLoc(), get(NewOpc))
                                  .setMIFlags(MI.getFlags());
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I)
      MIB->addOperand(MI.getOperand(I));

    updateLiveVariables(LV, MI, *MIB);
    if (LIS)
      LIS->ReplaceMachineInstrInMaps(MI, *MIB);

    return MIB;
  }

  assert(Opc != AMDGPU::V_FMAC_F16_t16_e32 &&
         "V_FMAC_F16_t16_e32 is not supported and not expected to be present "
         "pre-RA");

  // Handle MAC/FMAC.
  bool IsF16 = Opc == AMDGPU::V_MAC_F16_e32 || Opc == AMDGPU::V_MAC_F16_e64 ||
               Opc == AMDGPU::V_FMAC_F16_e32 || Opc == AMDGPU::V_FMAC_F16_e64 ||
               Opc == AMDGPU::V_FMAC_F16_t16_e64;
  bool IsFMA = Opc == AMDGPU::V_FMAC_F32_e32 || Opc == AMDGPU::V_FMAC_F32_e64 ||
               Opc == AMDGPU::V_FMAC_LEGACY_F32_e32 ||
               Opc == AMDGPU::V_FMAC_LEGACY_F32_e64 ||
               Opc == AMDGPU::V_FMAC_F16_e32 || Opc == AMDGPU::V_FMAC_F16_e64 ||
               Opc == AMDGPU::V_FMAC_F16_t16_e64 ||
               Opc == AMDGPU::V_FMAC_F64_e32 || Opc == AMDGPU::V_FMAC_F64_e64;
  bool IsF64 = Opc == AMDGPU::V_FMAC_F64_e32 || Opc == AMDGPU::V_FMAC_F64_e64;
  bool IsLegacy = Opc == AMDGPU::V_MAC_LEGACY_F32_e32 ||
                  Opc == AMDGPU::V_MAC_LEGACY_F32_e64 ||
                  Opc == AMDGPU::V_FMAC_LEGACY_F32_e32 ||
                  Opc == AMDGPU::V_FMAC_LEGACY_F32_e64;
  bool Src0Literal = false;

  switch (Opc) {
  default:
    return nullptr;
  case AMDGPU::V_MAC_F16_e64:
  case AMDGPU::V_FMAC_F16_e64:
  case AMDGPU::V_FMAC_F16_t16_e64:
  case AMDGPU::V_MAC_F32_e64:
  case AMDGPU::V_MAC_LEGACY_F32_e64:
  case AMDGPU::V_FMAC_F32_e64:
  case AMDGPU::V_FMAC_LEGACY_F32_e64:
  case AMDGPU::V_FMAC_F64_e64:
    break;
  case AMDGPU::V_MAC_F16_e32:
  case AMDGPU::V_FMAC_F16_e32:
  case AMDGPU::V_MAC_F32_e32:
  case AMDGPU::V_MAC_LEGACY_F32_e32:
  case AMDGPU::V_FMAC_F32_e32:
  case AMDGPU::V_FMAC_LEGACY_F32_e32:
  case AMDGPU::V_FMAC_F64_e32: {
    int Src0Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(),
                                             AMDGPU::OpName::src0);
    const MachineOperand *Src0 = &MI.getOperand(Src0Idx);
    if (!Src0->isReg() && !Src0->isImm())
      return nullptr;

    if (Src0->isImm() && !isInlineConstant(MI, Src0Idx, *Src0))
      Src0Literal = true;

    break;
  }
  }

  MachineInstrBuilder MIB;
  const MachineOperand *Dst = getNamedOperand(MI, AMDGPU::OpName::vdst);
  const MachineOperand *Src0 = getNamedOperand(MI, AMDGPU::OpName::src0);
  const MachineOperand *Src0Mods =
    getNamedOperand(MI, AMDGPU::OpName::src0_modifiers);
  const MachineOperand *Src1 = getNamedOperand(MI, AMDGPU::OpName::src1);
  const MachineOperand *Src1Mods =
    getNamedOperand(MI, AMDGPU::OpName::src1_modifiers);
  const MachineOperand *Src2 = getNamedOperand(MI, AMDGPU::OpName::src2);
  const MachineOperand *Src2Mods =
      getNamedOperand(MI, AMDGPU::OpName::src2_modifiers);
  const MachineOperand *Clamp = getNamedOperand(MI, AMDGPU::OpName::clamp);
  const MachineOperand *Omod = getNamedOperand(MI, AMDGPU::OpName::omod);
  const MachineOperand *OpSel = getNamedOperand(MI, AMDGPU::OpName::op_sel);

  if (!Src0Mods && !Src1Mods && !Src2Mods && !Clamp && !Omod && !IsF64 &&
      !IsLegacy &&
      // If we have an SGPR input, we will violate the constant bus restriction.
      (ST.getConstantBusLimit(Opc) > 1 || !Src0->isReg() ||
       !RI.isSGPRReg(MBB.getParent()->getRegInfo(), Src0->getReg()))) {
    MachineInstr *DefMI;
    const auto killDef = [&]() -> void {
      const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
      // The only user is the instruction which will be killed.
      Register DefReg = DefMI->getOperand(0).getReg();
      if (!MRI.hasOneNonDBGUse(DefReg))
        return;
      // We cannot just remove the DefMI here, calling pass will crash.
      DefMI->setDesc(get(AMDGPU::IMPLICIT_DEF));
      for (unsigned I = DefMI->getNumOperands() - 1; I != 0; --I)
        DefMI->removeOperand(I);
      if (LV)
        LV->getVarInfo(DefReg).AliveBlocks.clear();
    };

    int64_t Imm;
    if (!Src0Literal && getFoldableImm(Src2, Imm, &DefMI)) {
      unsigned NewOpc =
          IsFMA ? (IsF16 ? (ST.hasTrue16BitInsts() ? AMDGPU::V_FMAAK_F16_t16
                                                   : AMDGPU::V_FMAAK_F16)
                         : AMDGPU::V_FMAAK_F32)
                : (IsF16 ? AMDGPU::V_MADAK_F16 : AMDGPU::V_MADAK_F32);
      if (pseudoToMCOpcode(NewOpc) != -1) {
        MIB = BuildMI(MBB, MI, MI.getDebugLoc(), get(NewOpc))
                  .add(*Dst)
                  .add(*Src0)
                  .add(*Src1)
                  .addImm(Imm)
                  .setMIFlags(MI.getFlags());
        updateLiveVariables(LV, MI, *MIB);
        if (LIS)
          LIS->ReplaceMachineInstrInMaps(MI, *MIB);
        killDef();
        return MIB;
      }
    }
    unsigned NewOpc =
        IsFMA ? (IsF16 ? (ST.hasTrue16BitInsts() ? AMDGPU::V_FMAMK_F16_t16
                                                 : AMDGPU::V_FMAMK_F16)
                       : AMDGPU::V_FMAMK_F32)
              : (IsF16 ? AMDGPU::V_MADMK_F16 : AMDGPU::V_MADMK_F32);
    if (!Src0Literal && getFoldableImm(Src1, Imm, &DefMI)) {
      if (pseudoToMCOpcode(NewOpc) != -1) {
        MIB = BuildMI(MBB, MI, MI.getDebugLoc(), get(NewOpc))
                  .add(*Dst)
                  .add(*Src0)
                  .addImm(Imm)
                  .add(*Src2)
                  .setMIFlags(MI.getFlags());
        updateLiveVariables(LV, MI, *MIB);
        if (LIS)
          LIS->ReplaceMachineInstrInMaps(MI, *MIB);
        killDef();
        return MIB;
      }
    }
    if (Src0Literal || getFoldableImm(Src0, Imm, &DefMI)) {
      if (Src0Literal) {
        Imm = Src0->getImm();
        DefMI = nullptr;
      }
      if (pseudoToMCOpcode(NewOpc) != -1 &&
          isOperandLegal(
              MI, AMDGPU::getNamedOperandIdx(NewOpc, AMDGPU::OpName::src0),
              Src1)) {
        MIB = BuildMI(MBB, MI, MI.getDebugLoc(), get(NewOpc))
                  .add(*Dst)
                  .add(*Src1)
                  .addImm(Imm)
                  .add(*Src2)
                  .setMIFlags(MI.getFlags());
        updateLiveVariables(LV, MI, *MIB);
        if (LIS)
          LIS->ReplaceMachineInstrInMaps(MI, *MIB);
        if (DefMI)
          killDef();
        return MIB;
      }
    }
  }

  // VOP2 mac/fmac with a literal operand cannot be converted to VOP3 mad/fma
  // if VOP3 does not allow a literal operand.
  if (Src0Literal && !ST.hasVOP3Literal())
    return nullptr;

  unsigned NewOpc = IsFMA ? IsF16 ? AMDGPU::V_FMA_F16_gfx9_e64
                                  : IsF64 ? AMDGPU::V_FMA_F64_e64
                                          : IsLegacy
                                                ? AMDGPU::V_FMA_LEGACY_F32_e64
                                                : AMDGPU::V_FMA_F32_e64
                          : IsF16 ? AMDGPU::V_MAD_F16_e64
                                  : IsLegacy ? AMDGPU::V_MAD_LEGACY_F32_e64
                                             : AMDGPU::V_MAD_F32_e64;
  if (pseudoToMCOpcode(NewOpc) == -1)
    return nullptr;

  MIB = BuildMI(MBB, MI, MI.getDebugLoc(), get(NewOpc))
            .add(*Dst)
            .addImm(Src0Mods ? Src0Mods->getImm() : 0)
            .add(*Src0)
            .addImm(Src1Mods ? Src1Mods->getImm() : 0)
            .add(*Src1)
            .addImm(Src2Mods ? Src2Mods->getImm() : 0)
            .add(*Src2)
            .addImm(Clamp ? Clamp->getImm() : 0)
            .addImm(Omod ? Omod->getImm() : 0)
            .setMIFlags(MI.getFlags());
  if (AMDGPU::hasNamedOperand(NewOpc, AMDGPU::OpName::op_sel))
    MIB.addImm(OpSel ? OpSel->getImm() : 0);
  updateLiveVariables(LV, MI, *MIB);
  if (LIS)
    LIS->ReplaceMachineInstrInMaps(MI, *MIB);
  return MIB;
}

// It's not generally safe to move VALU instructions across these since it will
// start using the register as a base index rather than directly.
// XXX - Why isn't hasSideEffects sufficient for these?
static bool changesVGPRIndexingMode(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::S_SET_GPR_IDX_ON:
  case AMDGPU::S_SET_GPR_IDX_MODE:
  case AMDGPU::S_SET_GPR_IDX_OFF:
    return true;
  default:
    return false;
  }
}

bool SIInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                       const MachineBasicBlock *MBB,
                                       const MachineFunction &MF) const {
  // Skipping the check for SP writes in the base implementation. The reason it
  // was added was apparently due to compile time concerns.
  //
  // TODO: Do we really want this barrier? It triggers unnecessary hazard nops
  // but is probably avoidable.

  // Copied from base implementation.
  // Terminators and labels can't be scheduled around.
  if (MI.isTerminator() || MI.isPosition())
    return true;

  // INLINEASM_BR can jump to another block
  if (MI.getOpcode() == TargetOpcode::INLINEASM_BR)
    return true;

  if (MI.getOpcode() == AMDGPU::SCHED_BARRIER && MI.getOperand(0).getImm() == 0)
    return true;

  // Target-independent instructions do not have an implicit-use of EXEC, even
  // when they operate on VGPRs. Treating EXEC modifications as scheduling
  // boundaries prevents incorrect movements of such instructions.
  return MI.modifiesRegister(AMDGPU::EXEC, &RI) ||
         MI.getOpcode() == AMDGPU::S_SETREG_IMM32_B32 ||
         MI.getOpcode() == AMDGPU::S_SETREG_B32 ||
         MI.getOpcode() == AMDGPU::S_SETPRIO ||
         changesVGPRIndexingMode(MI);
}

bool SIInstrInfo::isAlwaysGDS(uint16_t Opcode) const {
  return Opcode == AMDGPU::DS_ORDERED_COUNT || isGWS(Opcode);
}

bool SIInstrInfo::modifiesModeRegister(const MachineInstr &MI) {
  // Skip the full operand and register alias search modifiesRegister
  // does. There's only a handful of instructions that touch this, it's only an
  // implicit def, and doesn't alias any other registers.
  return is_contained(MI.getDesc().implicit_defs(), AMDGPU::MODE);
}

bool SIInstrInfo::hasUnwantedEffectsWhenEXECEmpty(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();

  if (MI.mayStore() && isSMRD(MI))
    return true; // scalar store or atomic

  // This will terminate the function when other lanes may need to continue.
  if (MI.isReturn())
    return true;

  // These instructions cause shader I/O that may cause hardware lockups
  // when executed with an empty EXEC mask.
  //
  // Note: exp with VM = DONE = 0 is automatically skipped by hardware when
  //       EXEC = 0, but checking for that case here seems not worth it
  //       given the typical code patterns.
  if (Opcode == AMDGPU::S_SENDMSG || Opcode == AMDGPU::S_SENDMSGHALT ||
      isEXP(Opcode) || Opcode == AMDGPU::DS_ORDERED_COUNT ||
      Opcode == AMDGPU::S_TRAP || Opcode == AMDGPU::S_WAIT_EVENT)
    return true;

  if (MI.isCall() || MI.isInlineAsm())
    return true; // conservative assumption

  // Assume that barrier interactions are only intended with active lanes.
  if (isBarrier(Opcode))
    return true;

  // A mode change is a scalar operation that influences vector instructions.
  if (modifiesModeRegister(MI))
    return true;

  // These are like SALU instructions in terms of effects, so it's questionable
  // whether we should return true for those.
  //
  // However, executing them with EXEC = 0 causes them to operate on undefined
  // data, which we avoid by returning true here.
  if (Opcode == AMDGPU::V_READFIRSTLANE_B32 ||
      Opcode == AMDGPU::V_READLANE_B32 || Opcode == AMDGPU::V_WRITELANE_B32 ||
      Opcode == AMDGPU::SI_RESTORE_S32_FROM_VGPR ||
      Opcode == AMDGPU::SI_SPILL_S32_TO_VGPR)
    return true;

  return false;
}

bool SIInstrInfo::mayReadEXEC(const MachineRegisterInfo &MRI,
                              const MachineInstr &MI) const {
  if (MI.isMetaInstruction())
    return false;

  // This won't read exec if this is an SGPR->SGPR copy.
  if (MI.isCopyLike()) {
    if (!RI.isSGPRReg(MRI, MI.getOperand(0).getReg()))
      return true;

    // Make sure this isn't copying exec as a normal operand
    return MI.readsRegister(AMDGPU::EXEC, &RI);
  }

  // Make a conservative assumption about the callee.
  if (MI.isCall())
    return true;

  // Be conservative with any unhandled generic opcodes.
  if (!isTargetSpecificOpcode(MI.getOpcode()))
    return true;

  return !isSALU(MI) || MI.readsRegister(AMDGPU::EXEC, &RI);
}

bool SIInstrInfo::isInlineConstant(const APInt &Imm) const {
  switch (Imm.getBitWidth()) {
  case 1: // This likely will be a condition code mask.
    return true;

  case 32:
    return AMDGPU::isInlinableLiteral32(Imm.getSExtValue(),
                                        ST.hasInv2PiInlineImm());
  case 64:
    return AMDGPU::isInlinableLiteral64(Imm.getSExtValue(),
                                        ST.hasInv2PiInlineImm());
  case 16:
    return ST.has16BitInsts() &&
           AMDGPU::isInlinableLiteralI16(Imm.getSExtValue(),
                                         ST.hasInv2PiInlineImm());
  default:
    llvm_unreachable("invalid bitwidth");
  }
}

bool SIInstrInfo::isInlineConstant(const APFloat &Imm) const {
  APInt IntImm = Imm.bitcastToAPInt();
  int64_t IntImmVal = IntImm.getSExtValue();
  bool HasInv2Pi = ST.hasInv2PiInlineImm();
  switch (APFloat::SemanticsToEnum(Imm.getSemantics())) {
  default:
    llvm_unreachable("invalid fltSemantics");
  case APFloatBase::S_IEEEsingle:
  case APFloatBase::S_IEEEdouble:
    return isInlineConstant(IntImm);
  case APFloatBase::S_BFloat:
    return ST.has16BitInsts() &&
           AMDGPU::isInlinableLiteralBF16(IntImmVal, HasInv2Pi);
  case APFloatBase::S_IEEEhalf:
    return ST.has16BitInsts() &&
           AMDGPU::isInlinableLiteralFP16(IntImmVal, HasInv2Pi);
  }
}

bool SIInstrInfo::isInlineConstant(const MachineOperand &MO,
                                   uint8_t OperandType) const {
  assert(!MO.isReg() && "isInlineConstant called on register operand!");
  if (!MO.isImm())
    return false;

  // MachineOperand provides no way to tell the true operand size, since it only
  // records a 64-bit value. We need to know the size to determine if a 32-bit
  // floating point immediate bit pattern is legal for an integer immediate. It
  // would be for any 32-bit integer operand, but would not be for a 64-bit one.

  int64_t Imm = MO.getImm();
  switch (OperandType) {
  case AMDGPU::OPERAND_REG_IMM_INT32:
  case AMDGPU::OPERAND_REG_IMM_FP32:
  case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_FP32:
  case AMDGPU::OPERAND_REG_IMM_V2FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP32:
  case AMDGPU::OPERAND_REG_IMM_V2INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT32:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT32:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
  case AMDGPU::OPERAND_INLINE_SPLIT_BARRIER_INT32: {
    int32_t Trunc = static_cast<int32_t>(Imm);
    return AMDGPU::isInlinableLiteral32(Trunc, ST.hasInv2PiInlineImm());
  }
  case AMDGPU::OPERAND_REG_IMM_INT64:
  case AMDGPU::OPERAND_REG_IMM_FP64:
  case AMDGPU::OPERAND_REG_INLINE_C_INT64:
  case AMDGPU::OPERAND_REG_INLINE_C_FP64:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP64:
    return AMDGPU::isInlinableLiteral64(MO.getImm(),
                                        ST.hasInv2PiInlineImm());
  case AMDGPU::OPERAND_REG_IMM_INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT16:
    // We would expect inline immediates to not be concerned with an integer/fp
    // distinction. However, in the case of 16-bit integer operations, the
    // "floating point" values appear to not work. It seems read the low 16-bits
    // of 32-bit immediates, which happens to always work for the integer
    // values.
    //
    // See llvm bugzilla 46302.
    //
    // TODO: Theoretically we could use op-sel to use the high bits of the
    // 32-bit FP values.
    return AMDGPU::isInlinableIntLiteral(Imm);
  case AMDGPU::OPERAND_REG_IMM_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2INT16:
    return AMDGPU::isInlinableLiteralV2I16(Imm);
  case AMDGPU::OPERAND_REG_IMM_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16:
    return AMDGPU::isInlinableLiteralV2F16(Imm);
  case AMDGPU::OPERAND_REG_IMM_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2BF16:
    return AMDGPU::isInlinableLiteralV2BF16(Imm);
  case AMDGPU::OPERAND_REG_IMM_FP16:
  case AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP16: {
    if (isInt<16>(Imm) || isUInt<16>(Imm)) {
      // A few special case instructions have 16-bit operands on subtargets
      // where 16-bit instructions are not legal.
      // TODO: Do the 32-bit immediates work? We shouldn't really need to handle
      // constants in these cases
      int16_t Trunc = static_cast<int16_t>(Imm);
      return ST.has16BitInsts() &&
             AMDGPU::isInlinableLiteralFP16(Trunc, ST.hasInv2PiInlineImm());
    }

    return false;
  }
  case AMDGPU::OPERAND_REG_IMM_BF16:
  case AMDGPU::OPERAND_REG_IMM_BF16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_BF16: {
    if (isInt<16>(Imm) || isUInt<16>(Imm)) {
      int16_t Trunc = static_cast<int16_t>(Imm);
      return ST.has16BitInsts() &&
             AMDGPU::isInlinableLiteralBF16(Trunc, ST.hasInv2PiInlineImm());
    }
    return false;
  }
  case AMDGPU::OPERAND_KIMM32:
  case AMDGPU::OPERAND_KIMM16:
    return false;
  case AMDGPU::OPERAND_INPUT_MODS:
  case MCOI::OPERAND_IMMEDIATE:
    // Always embedded in the instruction for free.
    return true;
  case MCOI::OPERAND_UNKNOWN:
  case MCOI::OPERAND_REGISTER:
  case MCOI::OPERAND_PCREL:
  case MCOI::OPERAND_GENERIC_0:
  case MCOI::OPERAND_GENERIC_1:
  case MCOI::OPERAND_GENERIC_2:
  case MCOI::OPERAND_GENERIC_3:
  case MCOI::OPERAND_GENERIC_4:
  case MCOI::OPERAND_GENERIC_5:
    // Just ignore anything else.
    return true;
  default:
    llvm_unreachable("invalid operand type");
  }
}

static bool compareMachineOp(const MachineOperand &Op0,
                             const MachineOperand &Op1) {
  if (Op0.getType() != Op1.getType())
    return false;

  switch (Op0.getType()) {
  case MachineOperand::MO_Register:
    return Op0.getReg() == Op1.getReg();
  case MachineOperand::MO_Immediate:
    return Op0.getImm() == Op1.getImm();
  default:
    llvm_unreachable("Didn't expect to be comparing these operand types");
  }
}

bool SIInstrInfo::isImmOperandLegal(const MachineInstr &MI, unsigned OpNo,
                                    const MachineOperand &MO) const {
  const MCInstrDesc &InstDesc = MI.getDesc();
  const MCOperandInfo &OpInfo = InstDesc.operands()[OpNo];

  assert(MO.isImm() || MO.isTargetIndex() || MO.isFI() || MO.isGlobal());

  if (OpInfo.OperandType == MCOI::OPERAND_IMMEDIATE)
    return true;

  if (OpInfo.RegClass < 0)
    return false;

  if (MO.isImm() && isInlineConstant(MO, OpInfo)) {
    if (isMAI(MI) && ST.hasMFMAInlineLiteralBug() &&
        OpNo ==(unsigned)AMDGPU::getNamedOperandIdx(MI.getOpcode(),
                                                    AMDGPU::OpName::src2))
      return false;
    return RI.opCanUseInlineConstant(OpInfo.OperandType);
  }

  if (!RI.opCanUseLiteralConstant(OpInfo.OperandType))
    return false;

  if (!isVOP3(MI) || !AMDGPU::isSISrcOperand(InstDesc, OpNo))
    return true;

  return ST.hasVOP3Literal();
}

bool SIInstrInfo::hasVALU32BitEncoding(unsigned Opcode) const {
  // GFX90A does not have V_MUL_LEGACY_F32_e32.
  if (Opcode == AMDGPU::V_MUL_LEGACY_F32_e64 && ST.hasGFX90AInsts())
    return false;

  int Op32 = AMDGPU::getVOPe32(Opcode);
  if (Op32 == -1)
    return false;

  return pseudoToMCOpcode(Op32) != -1;
}

bool SIInstrInfo::hasModifiers(unsigned Opcode) const {
  // The src0_modifier operand is present on all instructions
  // that have modifiers.

  return AMDGPU::hasNamedOperand(Opcode, AMDGPU::OpName::src0_modifiers);
}

bool SIInstrInfo::hasModifiersSet(const MachineInstr &MI,
                                  unsigned OpName) const {
  const MachineOperand *Mods = getNamedOperand(MI, OpName);
  return Mods && Mods->getImm();
}

bool SIInstrInfo::hasAnyModifiersSet(const MachineInstr &MI) const {
  return any_of(ModifierOpNames,
                [&](unsigned Name) { return hasModifiersSet(MI, Name); });
}

bool SIInstrInfo::canShrink(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI) const {
  const MachineOperand *Src2 = getNamedOperand(MI, AMDGPU::OpName::src2);
  // Can't shrink instruction with three operands.
  if (Src2) {
    switch (MI.getOpcode()) {
      default: return false;

      case AMDGPU::V_ADDC_U32_e64:
      case AMDGPU::V_SUBB_U32_e64:
      case AMDGPU::V_SUBBREV_U32_e64: {
        const MachineOperand *Src1
          = getNamedOperand(MI, AMDGPU::OpName::src1);
        if (!Src1->isReg() || !RI.isVGPR(MRI, Src1->getReg()))
          return false;
        // Additional verification is needed for sdst/src2.
        return true;
      }
      case AMDGPU::V_MAC_F16_e64:
      case AMDGPU::V_MAC_F32_e64:
      case AMDGPU::V_MAC_LEGACY_F32_e64:
      case AMDGPU::V_FMAC_F16_e64:
      case AMDGPU::V_FMAC_F16_t16_e64:
      case AMDGPU::V_FMAC_F32_e64:
      case AMDGPU::V_FMAC_F64_e64:
      case AMDGPU::V_FMAC_LEGACY_F32_e64:
        if (!Src2->isReg() || !RI.isVGPR(MRI, Src2->getReg()) ||
            hasModifiersSet(MI, AMDGPU::OpName::src2_modifiers))
          return false;
        break;

      case AMDGPU::V_CNDMASK_B32_e64:
        break;
    }
  }

  const MachineOperand *Src1 = getNamedOperand(MI, AMDGPU::OpName::src1);
  if (Src1 && (!Src1->isReg() || !RI.isVGPR(MRI, Src1->getReg()) ||
               hasModifiersSet(MI, AMDGPU::OpName::src1_modifiers)))
    return false;

  // We don't need to check src0, all input types are legal, so just make sure
  // src0 isn't using any modifiers.
  if (hasModifiersSet(MI, AMDGPU::OpName::src0_modifiers))
    return false;

  // Can it be shrunk to a valid 32 bit opcode?
  if (!hasVALU32BitEncoding(MI.getOpcode()))
    return false;

  // Check output modifiers
  return !hasModifiersSet(MI, AMDGPU::OpName::omod) &&
         !hasModifiersSet(MI, AMDGPU::OpName::clamp) &&
         !hasModifiersSet(MI, AMDGPU::OpName::byte_sel);
}

// Set VCC operand with all flags from \p Orig, except for setting it as
// implicit.
static void copyFlagsToImplicitVCC(MachineInstr &MI,
                                   const MachineOperand &Orig) {

  for (MachineOperand &Use : MI.implicit_operands()) {
    if (Use.isUse() &&
        (Use.getReg() == AMDGPU::VCC || Use.getReg() == AMDGPU::VCC_LO)) {
      Use.setIsUndef(Orig.isUndef());
      Use.setIsKill(Orig.isKill());
      return;
    }
  }
}

MachineInstr *SIInstrInfo::buildShrunkInst(MachineInstr &MI,
                                           unsigned Op32) const {
  MachineBasicBlock *MBB = MI.getParent();

  const MCInstrDesc &Op32Desc = get(Op32);
  MachineInstrBuilder Inst32 =
    BuildMI(*MBB, MI, MI.getDebugLoc(), Op32Desc)
    .setMIFlags(MI.getFlags());

  // Add the dst operand if the 32-bit encoding also has an explicit $vdst.
  // For VOPC instructions, this is replaced by an implicit def of vcc.

  // We assume the defs of the shrunk opcode are in the same order, and the
  // shrunk opcode loses the last def (SGPR def, in the VOP3->VOPC case).
  for (int I = 0, E = Op32Desc.getNumDefs(); I != E; ++I)
    Inst32.add(MI.getOperand(I));

  const MachineOperand *Src2 = getNamedOperand(MI, AMDGPU::OpName::src2);

  int Idx = MI.getNumExplicitDefs();
  for (const MachineOperand &Use : MI.explicit_uses()) {
    int OpTy = MI.getDesc().operands()[Idx++].OperandType;
    if (OpTy == AMDGPU::OPERAND_INPUT_MODS || OpTy == MCOI::OPERAND_IMMEDIATE)
      continue;

    if (&Use == Src2) {
      if (AMDGPU::getNamedOperandIdx(Op32, AMDGPU::OpName::src2) == -1) {
        // In the case of V_CNDMASK_B32_e32, the explicit operand src2 is
        // replaced with an implicit read of vcc or vcc_lo. The implicit read
        // of vcc was already added during the initial BuildMI, but we
        // 1) may need to change vcc to vcc_lo to preserve the original register
        // 2) have to preserve the original flags.
        fixImplicitOperands(*Inst32);
        copyFlagsToImplicitVCC(*Inst32, *Src2);
        continue;
      }
    }

    Inst32.add(Use);
  }

  // FIXME: Losing implicit operands

  return Inst32;
}

bool SIInstrInfo::usesConstantBus(const MachineRegisterInfo &MRI,
                                  const MachineOperand &MO,
                                  const MCOperandInfo &OpInfo) const {
  // Literal constants use the constant bus.
  if (!MO.isReg())
    return !isInlineConstant(MO, OpInfo);

  if (!MO.isUse())
    return false;

  if (MO.getReg().isVirtual())
    return RI.isSGPRClass(MRI.getRegClass(MO.getReg()));

  // Null is free
  if (MO.getReg() == AMDGPU::SGPR_NULL || MO.getReg() == AMDGPU::SGPR_NULL64)
    return false;

  // SGPRs use the constant bus
  if (MO.isImplicit()) {
    return MO.getReg() == AMDGPU::M0 || MO.getReg() == AMDGPU::VCC ||
           MO.getReg() == AMDGPU::VCC_LO;
  }
  return AMDGPU::SReg_32RegClass.contains(MO.getReg()) ||
         AMDGPU::SReg_64RegClass.contains(MO.getReg());
}

static Register findImplicitSGPRRead(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.implicit_operands()) {
    // We only care about reads.
    if (MO.isDef())
      continue;

    switch (MO.getReg()) {
    case AMDGPU::VCC:
    case AMDGPU::VCC_LO:
    case AMDGPU::VCC_HI:
    case AMDGPU::M0:
    case AMDGPU::FLAT_SCR:
      return MO.getReg();

    default:
      break;
    }
  }

  return Register();
}

static bool shouldReadExec(const MachineInstr &MI) {
  if (SIInstrInfo::isVALU(MI)) {
    switch (MI.getOpcode()) {
    case AMDGPU::V_READLANE_B32:
    case AMDGPU::SI_RESTORE_S32_FROM_VGPR:
    case AMDGPU::V_WRITELANE_B32:
    case AMDGPU::SI_SPILL_S32_TO_VGPR:
      return false;
    }

    return true;
  }

  if (MI.isPreISelOpcode() ||
      SIInstrInfo::isGenericOpcode(MI.getOpcode()) ||
      SIInstrInfo::isSALU(MI) ||
      SIInstrInfo::isSMRD(MI))
    return false;

  return true;
}

static bool isSubRegOf(const SIRegisterInfo &TRI,
                       const MachineOperand &SuperVec,
                       const MachineOperand &SubReg) {
  if (SubReg.getReg().isPhysical())
    return TRI.isSubRegister(SuperVec.getReg(), SubReg.getReg());

  return SubReg.getSubReg() != AMDGPU::NoSubRegister &&
         SubReg.getReg() == SuperVec.getReg();
}

bool SIInstrInfo::verifyInstruction(const MachineInstr &MI,
                                    StringRef &ErrInfo) const {
  uint16_t Opcode = MI.getOpcode();
  if (SIInstrInfo::isGenericOpcode(MI.getOpcode()))
    return true;

  const MachineFunction *MF = MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF->getRegInfo();

  int Src0Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src0);
  int Src1Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src1);
  int Src2Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src2);
  int Src3Idx = -1;
  if (Src0Idx == -1) {
    // VOPD V_DUAL_* instructions use different operand names.
    Src0Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src0X);
    Src1Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vsrc1X);
    Src2Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src0Y);
    Src3Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vsrc1Y);
  }

  // Make sure the number of operands is correct.
  const MCInstrDesc &Desc = get(Opcode);
  if (!Desc.isVariadic() &&
      Desc.getNumOperands() != MI.getNumExplicitOperands()) {
    ErrInfo = "Instruction has wrong number of operands.";
    return false;
  }

  if (MI.isInlineAsm()) {
    // Verify register classes for inlineasm constraints.
    for (unsigned I = InlineAsm::MIOp_FirstOperand, E = MI.getNumOperands();
         I != E; ++I) {
      const TargetRegisterClass *RC = MI.getRegClassConstraint(I, this, &RI);
      if (!RC)
        continue;

      const MachineOperand &Op = MI.getOperand(I);
      if (!Op.isReg())
        continue;

      Register Reg = Op.getReg();
      if (!Reg.isVirtual() && !RC->contains(Reg)) {
        ErrInfo = "inlineasm operand has incorrect register class.";
        return false;
      }
    }

    return true;
  }

  if (isImage(MI) && MI.memoperands_empty() && MI.mayLoadOrStore()) {
    ErrInfo = "missing memory operand from image instruction.";
    return false;
  }

  // Make sure the register classes are correct.
  for (int i = 0, e = Desc.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isFPImm()) {
      ErrInfo = "FPImm Machine Operands are not supported. ISel should bitcast "
                "all fp values to integers.";
      return false;
    }

    int RegClass = Desc.operands()[i].RegClass;

    switch (Desc.operands()[i].OperandType) {
    case MCOI::OPERAND_REGISTER:
      if (MI.getOperand(i).isImm() || MI.getOperand(i).isGlobal()) {
        ErrInfo = "Illegal immediate value for operand.";
        return false;
      }
      break;
    case AMDGPU::OPERAND_REG_IMM_INT32:
    case AMDGPU::OPERAND_REG_IMM_FP32:
    case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
    case AMDGPU::OPERAND_REG_IMM_V2FP32:
      break;
    case AMDGPU::OPERAND_REG_INLINE_C_INT32:
    case AMDGPU::OPERAND_REG_INLINE_C_FP32:
    case AMDGPU::OPERAND_REG_INLINE_C_INT64:
    case AMDGPU::OPERAND_REG_INLINE_C_FP64:
    case AMDGPU::OPERAND_REG_INLINE_C_INT16:
    case AMDGPU::OPERAND_REG_INLINE_C_FP16:
    case AMDGPU::OPERAND_REG_INLINE_AC_INT32:
    case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
    case AMDGPU::OPERAND_REG_INLINE_AC_INT16:
    case AMDGPU::OPERAND_REG_INLINE_AC_FP16:
    case AMDGPU::OPERAND_REG_INLINE_AC_FP64: {
      if (!MO.isReg() && (!MO.isImm() || !isInlineConstant(MI, i))) {
        ErrInfo = "Illegal immediate value for operand.";
        return false;
      }
      break;
    }
    case AMDGPU::OPERAND_INLINE_SPLIT_BARRIER_INT32:
      if (!MI.getOperand(i).isImm() || !isInlineConstant(MI, i)) {
        ErrInfo = "Expected inline constant for operand.";
        return false;
      }
      break;
    case MCOI::OPERAND_IMMEDIATE:
    case AMDGPU::OPERAND_KIMM32:
      // Check if this operand is an immediate.
      // FrameIndex operands will be replaced by immediates, so they are
      // allowed.
      if (!MI.getOperand(i).isImm() && !MI.getOperand(i).isFI()) {
        ErrInfo = "Expected immediate, but got non-immediate";
        return false;
      }
      [[fallthrough]];
    default:
      continue;
    }

    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;

    // FIXME: Ideally we would have separate instruction definitions with the
    // aligned register constraint.
    // FIXME: We do not verify inline asm operands, but custom inline asm
    // verification is broken anyway
    if (ST.needsAlignedVGPRs()) {
      const TargetRegisterClass *RC = RI.getRegClassForReg(MRI, Reg);
      if (RI.hasVectorRegisters(RC) && MO.getSubReg()) {
        const TargetRegisterClass *SubRC =
            RI.getSubRegisterClass(RC, MO.getSubReg());
        RC = RI.getCompatibleSubRegClass(RC, SubRC, MO.getSubReg());
        if (RC)
          RC = SubRC;
      }

      // Check that this is the aligned version of the class.
      if (!RC || !RI.isProperlyAlignedRC(*RC)) {
        ErrInfo = "Subtarget requires even aligned vector registers";
        return false;
      }
    }

    if (RegClass != -1) {
      if (Reg.isVirtual())
        continue;

      const TargetRegisterClass *RC = RI.getRegClass(RegClass);
      if (!RC->contains(Reg)) {
        ErrInfo = "Operand has incorrect register class.";
        return false;
      }
    }
  }

  // Verify SDWA
  if (isSDWA(MI)) {
    if (!ST.hasSDWA()) {
      ErrInfo = "SDWA is not supported on this target";
      return false;
    }

    int DstIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vdst);

    for (int OpIdx : {DstIdx, Src0Idx, Src1Idx, Src2Idx}) {
      if (OpIdx == -1)
        continue;
      const MachineOperand &MO = MI.getOperand(OpIdx);

      if (!ST.hasSDWAScalar()) {
        // Only VGPRS on VI
        if (!MO.isReg() || !RI.hasVGPRs(RI.getRegClassForReg(MRI, MO.getReg()))) {
          ErrInfo = "Only VGPRs allowed as operands in SDWA instructions on VI";
          return false;
        }
      } else {
        // No immediates on GFX9
        if (!MO.isReg()) {
          ErrInfo =
            "Only reg allowed as operands in SDWA instructions on GFX9+";
          return false;
        }
      }
    }

    if (!ST.hasSDWAOmod()) {
      // No omod allowed on VI
      const MachineOperand *OMod = getNamedOperand(MI, AMDGPU::OpName::omod);
      if (OMod != nullptr &&
        (!OMod->isImm() || OMod->getImm() != 0)) {
        ErrInfo = "OMod not allowed in SDWA instructions on VI";
        return false;
      }
    }

    if (Opcode == AMDGPU::V_CVT_F32_FP8_sdwa ||
        Opcode == AMDGPU::V_CVT_F32_BF8_sdwa ||
        Opcode == AMDGPU::V_CVT_PK_F32_FP8_sdwa ||
        Opcode == AMDGPU::V_CVT_PK_F32_BF8_sdwa) {
      const MachineOperand *Src0ModsMO =
          getNamedOperand(MI, AMDGPU::OpName::src0_modifiers);
      unsigned Mods = Src0ModsMO->getImm();
      if (Mods & SISrcMods::ABS || Mods & SISrcMods::NEG ||
          Mods & SISrcMods::SEXT) {
        ErrInfo = "sext, abs and neg are not allowed on this instruction";
        return false;
      }
    }

    uint16_t BasicOpcode = AMDGPU::getBasicFromSDWAOp(Opcode);
    if (isVOPC(BasicOpcode)) {
      if (!ST.hasSDWASdst() && DstIdx != -1) {
        // Only vcc allowed as dst on VI for VOPC
        const MachineOperand &Dst = MI.getOperand(DstIdx);
        if (!Dst.isReg() || Dst.getReg() != AMDGPU::VCC) {
          ErrInfo = "Only VCC allowed as dst in SDWA instructions on VI";
          return false;
        }
      } else if (!ST.hasSDWAOutModsVOPC()) {
        // No clamp allowed on GFX9 for VOPC
        const MachineOperand *Clamp = getNamedOperand(MI, AMDGPU::OpName::clamp);
        if (Clamp && (!Clamp->isImm() || Clamp->getImm() != 0)) {
          ErrInfo = "Clamp not allowed in VOPC SDWA instructions on VI";
          return false;
        }

        // No omod allowed on GFX9 for VOPC
        const MachineOperand *OMod = getNamedOperand(MI, AMDGPU::OpName::omod);
        if (OMod && (!OMod->isImm() || OMod->getImm() != 0)) {
          ErrInfo = "OMod not allowed in VOPC SDWA instructions on VI";
          return false;
        }
      }
    }

    const MachineOperand *DstUnused = getNamedOperand(MI, AMDGPU::OpName::dst_unused);
    if (DstUnused && DstUnused->isImm() &&
        DstUnused->getImm() == AMDGPU::SDWA::UNUSED_PRESERVE) {
      const MachineOperand &Dst = MI.getOperand(DstIdx);
      if (!Dst.isReg() || !Dst.isTied()) {
        ErrInfo = "Dst register should have tied register";
        return false;
      }

      const MachineOperand &TiedMO =
          MI.getOperand(MI.findTiedOperandIdx(DstIdx));
      if (!TiedMO.isReg() || !TiedMO.isImplicit() || !TiedMO.isUse()) {
        ErrInfo =
            "Dst register should be tied to implicit use of preserved register";
        return false;
      }
      if (TiedMO.getReg().isPhysical() && Dst.getReg() != TiedMO.getReg()) {
        ErrInfo = "Dst register should use same physical register as preserved";
        return false;
      }
    }
  }

  // Verify MIMG / VIMAGE / VSAMPLE
  if (isImage(MI.getOpcode()) && !MI.mayStore()) {
    // Ensure that the return type used is large enough for all the options
    // being used TFE/LWE require an extra result register.
    const MachineOperand *DMask = getNamedOperand(MI, AMDGPU::OpName::dmask);
    if (DMask) {
      uint64_t DMaskImm = DMask->getImm();
      uint32_t RegCount =
          isGather4(MI.getOpcode()) ? 4 : llvm::popcount(DMaskImm);
      const MachineOperand *TFE = getNamedOperand(MI, AMDGPU::OpName::tfe);
      const MachineOperand *LWE = getNamedOperand(MI, AMDGPU::OpName::lwe);
      const MachineOperand *D16 = getNamedOperand(MI, AMDGPU::OpName::d16);

      // Adjust for packed 16 bit values
      if (D16 && D16->getImm() && !ST.hasUnpackedD16VMem())
        RegCount = divideCeil(RegCount, 2);

      // Adjust if using LWE or TFE
      if ((LWE && LWE->getImm()) || (TFE && TFE->getImm()))
        RegCount += 1;

      const uint32_t DstIdx =
          AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::vdata);
      const MachineOperand &Dst = MI.getOperand(DstIdx);
      if (Dst.isReg()) {
        const TargetRegisterClass *DstRC = getOpRegClass(MI, DstIdx);
        uint32_t DstSize = RI.getRegSizeInBits(*DstRC) / 32;
        if (RegCount > DstSize) {
          ErrInfo = "Image instruction returns too many registers for dst "
                    "register class";
          return false;
        }
      }
    }
  }

  // Verify VOP*. Ignore multiple sgpr operands on writelane.
  if (isVALU(MI) && Desc.getOpcode() != AMDGPU::V_WRITELANE_B32) {
    unsigned ConstantBusCount = 0;
    bool UsesLiteral = false;
    const MachineOperand *LiteralVal = nullptr;

    int ImmIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::imm);
    if (ImmIdx != -1) {
      ++ConstantBusCount;
      UsesLiteral = true;
      LiteralVal = &MI.getOperand(ImmIdx);
    }

    SmallVector<Register, 2> SGPRsUsed;
    Register SGPRUsed;

    // Only look at the true operands. Only a real operand can use the constant
    // bus, and we don't want to check pseudo-operands like the source modifier
    // flags.
    for (int OpIdx : {Src0Idx, Src1Idx, Src2Idx, Src3Idx}) {
      if (OpIdx == -1)
        continue;
      const MachineOperand &MO = MI.getOperand(OpIdx);
      if (usesConstantBus(MRI, MO, MI.getDesc().operands()[OpIdx])) {
        if (MO.isReg()) {
          SGPRUsed = MO.getReg();
          if (!llvm::is_contained(SGPRsUsed, SGPRUsed)) {
            ++ConstantBusCount;
            SGPRsUsed.push_back(SGPRUsed);
          }
        } else {
          if (!UsesLiteral) {
            ++ConstantBusCount;
            UsesLiteral = true;
            LiteralVal = &MO;
          } else if (!MO.isIdenticalTo(*LiteralVal)) {
            assert(isVOP2(MI) || isVOP3(MI));
            ErrInfo = "VOP2/VOP3 instruction uses more than one literal";
            return false;
          }
        }
      }
    }

    SGPRUsed = findImplicitSGPRRead(MI);
    if (SGPRUsed) {
      // Implicit uses may safely overlap true operands
      if (llvm::all_of(SGPRsUsed, [this, SGPRUsed](unsigned SGPR) {
            return !RI.regsOverlap(SGPRUsed, SGPR);
          })) {
        ++ConstantBusCount;
        SGPRsUsed.push_back(SGPRUsed);
      }
    }

    // v_writelane_b32 is an exception from constant bus restriction:
    // vsrc0 can be sgpr, const or m0 and lane select sgpr, m0 or inline-const
    if (ConstantBusCount > ST.getConstantBusLimit(Opcode) &&
        Opcode != AMDGPU::V_WRITELANE_B32) {
      ErrInfo = "VOP* instruction violates constant bus restriction";
      return false;
    }

    if (isVOP3(MI) && UsesLiteral && !ST.hasVOP3Literal()) {
      ErrInfo = "VOP3 instruction uses literal";
      return false;
    }
  }

  // Special case for writelane - this can break the multiple constant bus rule,
  // but still can't use more than one SGPR register
  if (Desc.getOpcode() == AMDGPU::V_WRITELANE_B32) {
    unsigned SGPRCount = 0;
    Register SGPRUsed;

    for (int OpIdx : {Src0Idx, Src1Idx}) {
      if (OpIdx == -1)
        break;

      const MachineOperand &MO = MI.getOperand(OpIdx);

      if (usesConstantBus(MRI, MO, MI.getDesc().operands()[OpIdx])) {
        if (MO.isReg() && MO.getReg() != AMDGPU::M0) {
          if (MO.getReg() != SGPRUsed)
            ++SGPRCount;
          SGPRUsed = MO.getReg();
        }
      }
      if (SGPRCount > ST.getConstantBusLimit(Opcode)) {
        ErrInfo = "WRITELANE instruction violates constant bus restriction";
        return false;
      }
    }
  }

  // Verify misc. restrictions on specific instructions.
  if (Desc.getOpcode() == AMDGPU::V_DIV_SCALE_F32_e64 ||
      Desc.getOpcode() == AMDGPU::V_DIV_SCALE_F64_e64) {
    const MachineOperand &Src0 = MI.getOperand(Src0Idx);
    const MachineOperand &Src1 = MI.getOperand(Src1Idx);
    const MachineOperand &Src2 = MI.getOperand(Src2Idx);
    if (Src0.isReg() && Src1.isReg() && Src2.isReg()) {
      if (!compareMachineOp(Src0, Src1) &&
          !compareMachineOp(Src0, Src2)) {
        ErrInfo = "v_div_scale_{f32|f64} require src0 = src1 or src2";
        return false;
      }
    }
    if ((getNamedOperand(MI, AMDGPU::OpName::src0_modifiers)->getImm() &
         SISrcMods::ABS) ||
        (getNamedOperand(MI, AMDGPU::OpName::src1_modifiers)->getImm() &
         SISrcMods::ABS) ||
        (getNamedOperand(MI, AMDGPU::OpName::src2_modifiers)->getImm() &
         SISrcMods::ABS)) {
      ErrInfo = "ABS not allowed in VOP3B instructions";
      return false;
    }
  }

  if (isSOP2(MI) || isSOPC(MI)) {
    const MachineOperand &Src0 = MI.getOperand(Src0Idx);
    const MachineOperand &Src1 = MI.getOperand(Src1Idx);

    if (!Src0.isReg() && !Src1.isReg() &&
        !isInlineConstant(Src0, Desc.operands()[Src0Idx]) &&
        !isInlineConstant(Src1, Desc.operands()[Src1Idx]) &&
        !Src0.isIdenticalTo(Src1)) {
      ErrInfo = "SOP2/SOPC instruction requires too many immediate constants";
      return false;
    }
  }

  if (isSOPK(MI)) {
    auto Op = getNamedOperand(MI, AMDGPU::OpName::simm16);
    if (Desc.isBranch()) {
      if (!Op->isMBB()) {
        ErrInfo = "invalid branch target for SOPK instruction";
        return false;
      }
    } else {
      uint64_t Imm = Op->getImm();
      if (sopkIsZext(Opcode)) {
        if (!isUInt<16>(Imm)) {
          ErrInfo = "invalid immediate for SOPK instruction";
          return false;
        }
      } else {
        if (!isInt<16>(Imm)) {
          ErrInfo = "invalid immediate for SOPK instruction";
          return false;
        }
      }
    }
  }

  if (Desc.getOpcode() == AMDGPU::V_MOVRELS_B32_e32 ||
      Desc.getOpcode() == AMDGPU::V_MOVRELS_B32_e64 ||
      Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e32 ||
      Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e64) {
    const bool IsDst = Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e32 ||
                       Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e64;

    const unsigned StaticNumOps =
        Desc.getNumOperands() + Desc.implicit_uses().size();
    const unsigned NumImplicitOps = IsDst ? 2 : 1;

    // Allow additional implicit operands. This allows a fixup done by the post
    // RA scheduler where the main implicit operand is killed and implicit-defs
    // are added for sub-registers that remain live after this instruction.
    if (MI.getNumOperands() < StaticNumOps + NumImplicitOps) {
      ErrInfo = "missing implicit register operands";
      return false;
    }

    const MachineOperand *Dst = getNamedOperand(MI, AMDGPU::OpName::vdst);
    if (IsDst) {
      if (!Dst->isUse()) {
        ErrInfo = "v_movreld_b32 vdst should be a use operand";
        return false;
      }

      unsigned UseOpIdx;
      if (!MI.isRegTiedToUseOperand(StaticNumOps, &UseOpIdx) ||
          UseOpIdx != StaticNumOps + 1) {
        ErrInfo = "movrel implicit operands should be tied";
        return false;
      }
    }

    const MachineOperand &Src0 = MI.getOperand(Src0Idx);
    const MachineOperand &ImpUse
      = MI.getOperand(StaticNumOps + NumImplicitOps - 1);
    if (!ImpUse.isReg() || !ImpUse.isUse() ||
        !isSubRegOf(RI, ImpUse, IsDst ? *Dst : Src0)) {
      ErrInfo = "src0 should be subreg of implicit vector use";
      return false;
    }
  }

  // Make sure we aren't losing exec uses in the td files. This mostly requires
  // being careful when using let Uses to try to add other use registers.
  if (shouldReadExec(MI)) {
    if (!MI.hasRegisterImplicitUseOperand(AMDGPU::EXEC)) {
      ErrInfo = "VALU instruction does not implicitly read exec mask";
      return false;
    }
  }

  if (isSMRD(MI)) {
    if (MI.mayStore() &&
        ST.getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS) {
      // The register offset form of scalar stores may only use m0 as the
      // soffset register.
      const MachineOperand *Soff = getNamedOperand(MI, AMDGPU::OpName::soffset);
      if (Soff && Soff->getReg() != AMDGPU::M0) {
        ErrInfo = "scalar stores must use m0 as offset register";
        return false;
      }
    }
  }

  if (isFLAT(MI) && !ST.hasFlatInstOffsets()) {
    const MachineOperand *Offset = getNamedOperand(MI, AMDGPU::OpName::offset);
    if (Offset->getImm() != 0) {
      ErrInfo = "subtarget does not support offsets in flat instructions";
      return false;
    }
  }

  if (isDS(MI) && !ST.hasGDS()) {
    const MachineOperand *GDSOp = getNamedOperand(MI, AMDGPU::OpName::gds);
    if (GDSOp && GDSOp->getImm() != 0) {
      ErrInfo = "GDS is not supported on this subtarget";
      return false;
    }
  }

  if (isImage(MI)) {
    const MachineOperand *DimOp = getNamedOperand(MI, AMDGPU::OpName::dim);
    if (DimOp) {
      int VAddr0Idx = AMDGPU::getNamedOperandIdx(Opcode,
                                                 AMDGPU::OpName::vaddr0);
      int RSrcOpName =
          isMIMG(MI) ? AMDGPU::OpName::srsrc : AMDGPU::OpName::rsrc;
      int RsrcIdx = AMDGPU::getNamedOperandIdx(Opcode, RSrcOpName);
      const AMDGPU::MIMGInfo *Info = AMDGPU::getMIMGInfo(Opcode);
      const AMDGPU::MIMGBaseOpcodeInfo *BaseOpcode =
          AMDGPU::getMIMGBaseOpcodeInfo(Info->BaseOpcode);
      const AMDGPU::MIMGDimInfo *Dim =
          AMDGPU::getMIMGDimInfoByEncoding(DimOp->getImm());

      if (!Dim) {
        ErrInfo = "dim is out of range";
        return false;
      }

      bool IsA16 = false;
      if (ST.hasR128A16()) {
        const MachineOperand *R128A16 = getNamedOperand(MI, AMDGPU::OpName::r128);
        IsA16 = R128A16->getImm() != 0;
      } else if (ST.hasA16()) {
        const MachineOperand *A16 = getNamedOperand(MI, AMDGPU::OpName::a16);
        IsA16 = A16->getImm() != 0;
      }

      bool IsNSA = RsrcIdx - VAddr0Idx > 1;

      unsigned AddrWords =
          AMDGPU::getAddrSizeMIMGOp(BaseOpcode, Dim, IsA16, ST.hasG16());

      unsigned VAddrWords;
      if (IsNSA) {
        VAddrWords = RsrcIdx - VAddr0Idx;
        if (ST.hasPartialNSAEncoding() &&
            AddrWords > ST.getNSAMaxSize(isVSAMPLE(MI))) {
          unsigned LastVAddrIdx = RsrcIdx - 1;
          VAddrWords += getOpSize(MI, LastVAddrIdx) / 4 - 1;
        }
      } else {
        VAddrWords = getOpSize(MI, VAddr0Idx) / 4;
        if (AddrWords > 12)
          AddrWords = 16;
      }

      if (VAddrWords != AddrWords) {
        LLVM_DEBUG(dbgs() << "bad vaddr size, expected " << AddrWords
                          << " but got " << VAddrWords << "\n");
        ErrInfo = "bad vaddr size";
        return false;
      }
    }
  }

  const MachineOperand *DppCt = getNamedOperand(MI, AMDGPU::OpName::dpp_ctrl);
  if (DppCt) {
    using namespace AMDGPU::DPP;

    unsigned DC = DppCt->getImm();
    if (DC == DppCtrl::DPP_UNUSED1 || DC == DppCtrl::DPP_UNUSED2 ||
        DC == DppCtrl::DPP_UNUSED3 || DC > DppCtrl::DPP_LAST ||
        (DC >= DppCtrl::DPP_UNUSED4_FIRST && DC <= DppCtrl::DPP_UNUSED4_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED5_FIRST && DC <= DppCtrl::DPP_UNUSED5_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED6_FIRST && DC <= DppCtrl::DPP_UNUSED6_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED7_FIRST && DC <= DppCtrl::DPP_UNUSED7_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED8_FIRST && DC <= DppCtrl::DPP_UNUSED8_LAST)) {
      ErrInfo = "Invalid dpp_ctrl value";
      return false;
    }
    if (DC >= DppCtrl::WAVE_SHL1 && DC <= DppCtrl::WAVE_ROR1 &&
        ST.getGeneration() >= AMDGPUSubtarget::GFX10) {
      ErrInfo = "Invalid dpp_ctrl value: "
                "wavefront shifts are not supported on GFX10+";
      return false;
    }
    if (DC >= DppCtrl::BCAST15 && DC <= DppCtrl::BCAST31 &&
        ST.getGeneration() >= AMDGPUSubtarget::GFX10) {
      ErrInfo = "Invalid dpp_ctrl value: "
                "broadcasts are not supported on GFX10+";
      return false;
    }
    if (DC >= DppCtrl::ROW_SHARE_FIRST && DC <= DppCtrl::ROW_XMASK_LAST &&
        ST.getGeneration() < AMDGPUSubtarget::GFX10) {
      if (DC >= DppCtrl::ROW_NEWBCAST_FIRST &&
          DC <= DppCtrl::ROW_NEWBCAST_LAST &&
          !ST.hasGFX90AInsts()) {
        ErrInfo = "Invalid dpp_ctrl value: "
                  "row_newbroadcast/row_share is not supported before "
                  "GFX90A/GFX10";
        return false;
      }
      if (DC > DppCtrl::ROW_NEWBCAST_LAST || !ST.hasGFX90AInsts()) {
        ErrInfo = "Invalid dpp_ctrl value: "
                  "row_share and row_xmask are not supported before GFX10";
        return false;
      }
    }

    if (Opcode != AMDGPU::V_MOV_B64_DPP_PSEUDO &&
        !AMDGPU::isLegalDPALU_DPPControl(DC) && AMDGPU::isDPALU_DPP(Desc)) {
      ErrInfo = "Invalid dpp_ctrl value: "
                "DP ALU dpp only support row_newbcast";
      return false;
    }
  }

  if ((MI.mayStore() || MI.mayLoad()) && !isVGPRSpill(MI)) {
    const MachineOperand *Dst = getNamedOperand(MI, AMDGPU::OpName::vdst);
    uint16_t DataNameIdx = isDS(Opcode) ? AMDGPU::OpName::data0
                                        : AMDGPU::OpName::vdata;
    const MachineOperand *Data = getNamedOperand(MI, DataNameIdx);
    const MachineOperand *Data2 = getNamedOperand(MI, AMDGPU::OpName::data1);
    if (Data && !Data->isReg())
      Data = nullptr;

    if (ST.hasGFX90AInsts()) {
      if (Dst && Data &&
          (RI.isAGPR(MRI, Dst->getReg()) != RI.isAGPR(MRI, Data->getReg()))) {
        ErrInfo = "Invalid register class: "
                  "vdata and vdst should be both VGPR or AGPR";
        return false;
      }
      if (Data && Data2 &&
          (RI.isAGPR(MRI, Data->getReg()) != RI.isAGPR(MRI, Data2->getReg()))) {
        ErrInfo = "Invalid register class: "
                  "both data operands should be VGPR or AGPR";
        return false;
      }
    } else {
      if ((Dst && RI.isAGPR(MRI, Dst->getReg())) ||
          (Data && RI.isAGPR(MRI, Data->getReg())) ||
          (Data2 && RI.isAGPR(MRI, Data2->getReg()))) {
        ErrInfo = "Invalid register class: "
                  "agpr loads and stores not supported on this GPU";
        return false;
      }
    }
  }

  if (ST.needsAlignedVGPRs()) {
    const auto isAlignedReg = [&MI, &MRI, this](unsigned OpName) -> bool {
      const MachineOperand *Op = getNamedOperand(MI, OpName);
      if (!Op)
        return true;
      Register Reg = Op->getReg();
      if (Reg.isPhysical())
        return !(RI.getHWRegIndex(Reg) & 1);
      const TargetRegisterClass &RC = *MRI.getRegClass(Reg);
      return RI.getRegSizeInBits(RC) > 32 && RI.isProperlyAlignedRC(RC) &&
             !(RI.getChannelFromSubReg(Op->getSubReg()) & 1);
    };

    if (MI.getOpcode() == AMDGPU::DS_GWS_INIT ||
        MI.getOpcode() == AMDGPU::DS_GWS_SEMA_BR ||
        MI.getOpcode() == AMDGPU::DS_GWS_BARRIER) {

      if (!isAlignedReg(AMDGPU::OpName::data0)) {
        ErrInfo = "Subtarget requires even aligned vector registers "
                  "for DS_GWS instructions";
        return false;
      }
    }

    if (isMIMG(MI)) {
      if (!isAlignedReg(AMDGPU::OpName::vaddr)) {
        ErrInfo = "Subtarget requires even aligned vector registers "
                  "for vaddr operand of image instructions";
        return false;
      }
    }
  }

  if (MI.getOpcode() == AMDGPU::V_ACCVGPR_WRITE_B32_e64 &&
      !ST.hasGFX90AInsts()) {
    const MachineOperand *Src = getNamedOperand(MI, AMDGPU::OpName::src0);
    if (Src->isReg() && RI.isSGPRReg(MRI, Src->getReg())) {
      ErrInfo = "Invalid register class: "
                "v_accvgpr_write with an SGPR is not supported on this GPU";
      return false;
    }
  }

  if (Desc.getOpcode() == AMDGPU::G_AMDGPU_WAVE_ADDRESS) {
    const MachineOperand &SrcOp = MI.getOperand(1);
    if (!SrcOp.isReg() || SrcOp.getReg().isVirtual()) {
      ErrInfo = "pseudo expects only physical SGPRs";
      return false;
    }
  }

  return true;
}

// It is more readable to list mapped opcodes on the same line.
// clang-format off

unsigned SIInstrInfo::getVALUOp(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default: return AMDGPU::INSTRUCTION_LIST_END;
  case AMDGPU::REG_SEQUENCE: return AMDGPU::REG_SEQUENCE;
  case AMDGPU::COPY: return AMDGPU::COPY;
  case AMDGPU::PHI: return AMDGPU::PHI;
  case AMDGPU::INSERT_SUBREG: return AMDGPU::INSERT_SUBREG;
  case AMDGPU::WQM: return AMDGPU::WQM;
  case AMDGPU::SOFT_WQM: return AMDGPU::SOFT_WQM;
  case AMDGPU::STRICT_WWM: return AMDGPU::STRICT_WWM;
  case AMDGPU::STRICT_WQM: return AMDGPU::STRICT_WQM;
  case AMDGPU::S_MOV_B32: {
    const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
    return MI.getOperand(1).isReg() ||
           RI.isAGPR(MRI, MI.getOperand(0).getReg()) ?
           AMDGPU::COPY : AMDGPU::V_MOV_B32_e32;
  }
  case AMDGPU::S_ADD_I32:
    return ST.hasAddNoCarry() ? AMDGPU::V_ADD_U32_e64 : AMDGPU::V_ADD_CO_U32_e32;
  case AMDGPU::S_ADDC_U32:
    return AMDGPU::V_ADDC_U32_e32;
  case AMDGPU::S_SUB_I32:
    return ST.hasAddNoCarry() ? AMDGPU::V_SUB_U32_e64 : AMDGPU::V_SUB_CO_U32_e32;
    // FIXME: These are not consistently handled, and selected when the carry is
    // used.
  case AMDGPU::S_ADD_U32:
    return AMDGPU::V_ADD_CO_U32_e32;
  case AMDGPU::S_SUB_U32:
    return AMDGPU::V_SUB_CO_U32_e32;
  case AMDGPU::S_SUBB_U32: return AMDGPU::V_SUBB_U32_e32;
  case AMDGPU::S_MUL_I32: return AMDGPU::V_MUL_LO_U32_e64;
  case AMDGPU::S_MUL_HI_U32: return AMDGPU::V_MUL_HI_U32_e64;
  case AMDGPU::S_MUL_HI_I32: return AMDGPU::V_MUL_HI_I32_e64;
  case AMDGPU::S_AND_B32: return AMDGPU::V_AND_B32_e64;
  case AMDGPU::S_OR_B32: return AMDGPU::V_OR_B32_e64;
  case AMDGPU::S_XOR_B32: return AMDGPU::V_XOR_B32_e64;
  case AMDGPU::S_XNOR_B32:
    return ST.hasDLInsts() ? AMDGPU::V_XNOR_B32_e64 : AMDGPU::INSTRUCTION_LIST_END;
  case AMDGPU::S_MIN_I32: return AMDGPU::V_MIN_I32_e64;
  case AMDGPU::S_MIN_U32: return AMDGPU::V_MIN_U32_e64;
  case AMDGPU::S_MAX_I32: return AMDGPU::V_MAX_I32_e64;
  case AMDGPU::S_MAX_U32: return AMDGPU::V_MAX_U32_e64;
  case AMDGPU::S_ASHR_I32: return AMDGPU::V_ASHR_I32_e32;
  case AMDGPU::S_ASHR_I64: return AMDGPU::V_ASHR_I64_e64;
  case AMDGPU::S_LSHL_B32: return AMDGPU::V_LSHL_B32_e32;
  case AMDGPU::S_LSHL_B64: return AMDGPU::V_LSHL_B64_e64;
  case AMDGPU::S_LSHR_B32: return AMDGPU::V_LSHR_B32_e32;
  case AMDGPU::S_LSHR_B64: return AMDGPU::V_LSHR_B64_e64;
  case AMDGPU::S_SEXT_I32_I8: return AMDGPU::V_BFE_I32_e64;
  case AMDGPU::S_SEXT_I32_I16: return AMDGPU::V_BFE_I32_e64;
  case AMDGPU::S_BFE_U32: return AMDGPU::V_BFE_U32_e64;
  case AMDGPU::S_BFE_I32: return AMDGPU::V_BFE_I32_e64;
  case AMDGPU::S_BFM_B32: return AMDGPU::V_BFM_B32_e64;
  case AMDGPU::S_BREV_B32: return AMDGPU::V_BFREV_B32_e32;
  case AMDGPU::S_NOT_B32: return AMDGPU::V_NOT_B32_e32;
  case AMDGPU::S_NOT_B64: return AMDGPU::V_NOT_B32_e32;
  case AMDGPU::S_CMP_EQ_I32: return AMDGPU::V_CMP_EQ_I32_e64;
  case AMDGPU::S_CMP_LG_I32: return AMDGPU::V_CMP_NE_I32_e64;
  case AMDGPU::S_CMP_GT_I32: return AMDGPU::V_CMP_GT_I32_e64;
  case AMDGPU::S_CMP_GE_I32: return AMDGPU::V_CMP_GE_I32_e64;
  case AMDGPU::S_CMP_LT_I32: return AMDGPU::V_CMP_LT_I32_e64;
  case AMDGPU::S_CMP_LE_I32: return AMDGPU::V_CMP_LE_I32_e64;
  case AMDGPU::S_CMP_EQ_U32: return AMDGPU::V_CMP_EQ_U32_e64;
  case AMDGPU::S_CMP_LG_U32: return AMDGPU::V_CMP_NE_U32_e64;
  case AMDGPU::S_CMP_GT_U32: return AMDGPU::V_CMP_GT_U32_e64;
  case AMDGPU::S_CMP_GE_U32: return AMDGPU::V_CMP_GE_U32_e64;
  case AMDGPU::S_CMP_LT_U32: return AMDGPU::V_CMP_LT_U32_e64;
  case AMDGPU::S_CMP_LE_U32: return AMDGPU::V_CMP_LE_U32_e64;
  case AMDGPU::S_CMP_EQ_U64: return AMDGPU::V_CMP_EQ_U64_e64;
  case AMDGPU::S_CMP_LG_U64: return AMDGPU::V_CMP_NE_U64_e64;
  case AMDGPU::S_BCNT1_I32_B32: return AMDGPU::V_BCNT_U32_B32_e64;
  case AMDGPU::S_FF1_I32_B32: return AMDGPU::V_FFBL_B32_e32;
  case AMDGPU::S_FLBIT_I32_B32: return AMDGPU::V_FFBH_U32_e32;
  case AMDGPU::S_FLBIT_I32: return AMDGPU::V_FFBH_I32_e64;
  case AMDGPU::S_CBRANCH_SCC0: return AMDGPU::S_CBRANCH_VCCZ;
  case AMDGPU::S_CBRANCH_SCC1: return AMDGPU::S_CBRANCH_VCCNZ;
  case AMDGPU::S_CVT_F32_I32: return AMDGPU::V_CVT_F32_I32_e64;
  case AMDGPU::S_CVT_F32_U32: return AMDGPU::V_CVT_F32_U32_e64;
  case AMDGPU::S_CVT_I32_F32: return AMDGPU::V_CVT_I32_F32_e64;
  case AMDGPU::S_CVT_U32_F32: return AMDGPU::V_CVT_U32_F32_e64;
  case AMDGPU::S_CVT_F32_F16: return AMDGPU::V_CVT_F32_F16_t16_e64;
  case AMDGPU::S_CVT_HI_F32_F16: return AMDGPU::V_CVT_F32_F16_t16_e64;
  case AMDGPU::S_CVT_F16_F32: return AMDGPU::V_CVT_F16_F32_t16_e64;
  case AMDGPU::S_CEIL_F32: return AMDGPU::V_CEIL_F32_e64;
  case AMDGPU::S_FLOOR_F32: return AMDGPU::V_FLOOR_F32_e64;
  case AMDGPU::S_TRUNC_F32: return AMDGPU::V_TRUNC_F32_e64;
  case AMDGPU::S_RNDNE_F32: return AMDGPU::V_RNDNE_F32_e64;
  case AMDGPU::S_CEIL_F16:
    return ST.useRealTrue16Insts() ? AMDGPU::V_CEIL_F16_t16_e64
                                   : AMDGPU::V_CEIL_F16_fake16_e64;
  case AMDGPU::S_FLOOR_F16:
    return ST.useRealTrue16Insts() ? AMDGPU::V_FLOOR_F16_t16_e64
                                   : AMDGPU::V_FLOOR_F16_fake16_e64;
  case AMDGPU::S_TRUNC_F16:
    return AMDGPU::V_TRUNC_F16_fake16_e64;
  case AMDGPU::S_RNDNE_F16:
    return AMDGPU::V_RNDNE_F16_fake16_e64;
  case AMDGPU::S_ADD_F32: return AMDGPU::V_ADD_F32_e64;
  case AMDGPU::S_SUB_F32: return AMDGPU::V_SUB_F32_e64;
  case AMDGPU::S_MIN_F32: return AMDGPU::V_MIN_F32_e64;
  case AMDGPU::S_MAX_F32: return AMDGPU::V_MAX_F32_e64;
  case AMDGPU::S_MINIMUM_F32: return AMDGPU::V_MINIMUM_F32_e64;
  case AMDGPU::S_MAXIMUM_F32: return AMDGPU::V_MAXIMUM_F32_e64;
  case AMDGPU::S_MUL_F32: return AMDGPU::V_MUL_F32_e64;
  case AMDGPU::S_ADD_F16: return AMDGPU::V_ADD_F16_fake16_e64;
  case AMDGPU::S_SUB_F16: return AMDGPU::V_SUB_F16_fake16_e64;
  case AMDGPU::S_MIN_F16: return AMDGPU::V_MIN_F16_fake16_e64;
  case AMDGPU::S_MAX_F16: return AMDGPU::V_MAX_F16_fake16_e64;
  case AMDGPU::S_MINIMUM_F16: return AMDGPU::V_MINIMUM_F16_e64;
  case AMDGPU::S_MAXIMUM_F16: return AMDGPU::V_MAXIMUM_F16_e64;
  case AMDGPU::S_MUL_F16: return AMDGPU::V_MUL_F16_fake16_e64;
  case AMDGPU::S_CVT_PK_RTZ_F16_F32: return AMDGPU::V_CVT_PKRTZ_F16_F32_e64;
  case AMDGPU::S_FMAC_F32: return AMDGPU::V_FMAC_F32_e64;
  case AMDGPU::S_FMAC_F16: return AMDGPU::V_FMAC_F16_t16_e64;
  case AMDGPU::S_FMAMK_F32: return AMDGPU::V_FMAMK_F32;
  case AMDGPU::S_FMAAK_F32: return AMDGPU::V_FMAAK_F32;
  case AMDGPU::S_CMP_LT_F32: return AMDGPU::V_CMP_LT_F32_e64;
  case AMDGPU::S_CMP_EQ_F32: return AMDGPU::V_CMP_EQ_F32_e64;
  case AMDGPU::S_CMP_LE_F32: return AMDGPU::V_CMP_LE_F32_e64;
  case AMDGPU::S_CMP_GT_F32: return AMDGPU::V_CMP_GT_F32_e64;
  case AMDGPU::S_CMP_LG_F32: return AMDGPU::V_CMP_LG_F32_e64;
  case AMDGPU::S_CMP_GE_F32: return AMDGPU::V_CMP_GE_F32_e64;
  case AMDGPU::S_CMP_O_F32: return AMDGPU::V_CMP_O_F32_e64;
  case AMDGPU::S_CMP_U_F32: return AMDGPU::V_CMP_U_F32_e64;
  case AMDGPU::S_CMP_NGE_F32: return AMDGPU::V_CMP_NGE_F32_e64;
  case AMDGPU::S_CMP_NLG_F32: return AMDGPU::V_CMP_NLG_F32_e64;
  case AMDGPU::S_CMP_NGT_F32: return AMDGPU::V_CMP_NGT_F32_e64;
  case AMDGPU::S_CMP_NLE_F32: return AMDGPU::V_CMP_NLE_F32_e64;
  case AMDGPU::S_CMP_NEQ_F32: return AMDGPU::V_CMP_NEQ_F32_e64;
  case AMDGPU::S_CMP_NLT_F32: return AMDGPU::V_CMP_NLT_F32_e64;
  case AMDGPU::S_CMP_LT_F16: return AMDGPU::V_CMP_LT_F16_t16_e64;
  case AMDGPU::S_CMP_EQ_F16: return AMDGPU::V_CMP_EQ_F16_t16_e64;
  case AMDGPU::S_CMP_LE_F16: return AMDGPU::V_CMP_LE_F16_t16_e64;
  case AMDGPU::S_CMP_GT_F16: return AMDGPU::V_CMP_GT_F16_t16_e64;
  case AMDGPU::S_CMP_LG_F16: return AMDGPU::V_CMP_LG_F16_t16_e64;
  case AMDGPU::S_CMP_GE_F16: return AMDGPU::V_CMP_GE_F16_t16_e64;
  case AMDGPU::S_CMP_O_F16: return AMDGPU::V_CMP_O_F16_t16_e64;
  case AMDGPU::S_CMP_U_F16: return AMDGPU::V_CMP_U_F16_t16_e64;
  case AMDGPU::S_CMP_NGE_F16: return AMDGPU::V_CMP_NGE_F16_t16_e64;
  case AMDGPU::S_CMP_NLG_F16: return AMDGPU::V_CMP_NLG_F16_t16_e64;
  case AMDGPU::S_CMP_NGT_F16: return AMDGPU::V_CMP_NGT_F16_t16_e64;
  case AMDGPU::S_CMP_NLE_F16: return AMDGPU::V_CMP_NLE_F16_t16_e64;
  case AMDGPU::S_CMP_NEQ_F16: return AMDGPU::V_CMP_NEQ_F16_t16_e64;
  case AMDGPU::S_CMP_NLT_F16: return AMDGPU::V_CMP_NLT_F16_t16_e64;
  case AMDGPU::V_S_EXP_F32_e64: return AMDGPU::V_EXP_F32_e64;
  case AMDGPU::V_S_EXP_F16_e64: return AMDGPU::V_EXP_F16_fake16_e64;
  case AMDGPU::V_S_LOG_F32_e64: return AMDGPU::V_LOG_F32_e64;
  case AMDGPU::V_S_LOG_F16_e64: return AMDGPU::V_LOG_F16_fake16_e64;
  case AMDGPU::V_S_RCP_F32_e64: return AMDGPU::V_RCP_F32_e64;
  case AMDGPU::V_S_RCP_F16_e64: return AMDGPU::V_RCP_F16_fake16_e64;
  case AMDGPU::V_S_RSQ_F32_e64: return AMDGPU::V_RSQ_F32_e64;
  case AMDGPU::V_S_RSQ_F16_e64: return AMDGPU::V_RSQ_F16_fake16_e64;
  case AMDGPU::V_S_SQRT_F32_e64: return AMDGPU::V_SQRT_F32_e64;
  case AMDGPU::V_S_SQRT_F16_e64: return AMDGPU::V_SQRT_F16_fake16_e64;
  }
  llvm_unreachable(
      "Unexpected scalar opcode without corresponding vector one!");
}

// clang-format on

void SIInstrInfo::insertScratchExecCopy(MachineFunction &MF,
                                        MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const DebugLoc &DL, Register Reg,
                                        bool IsSCCLive,
                                        SlotIndexes *Indexes) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  bool IsWave32 = ST.isWave32();
  if (IsSCCLive) {
    // Insert two move instructions, one to save the original value of EXEC and
    // the other to turn on all bits in EXEC. This is required as we can't use
    // the single instruction S_OR_SAVEEXEC that clobbers SCC.
    unsigned MovOpc = IsWave32 ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
    MCRegister Exec = IsWave32 ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
    auto StoreExecMI = BuildMI(MBB, MBBI, DL, TII->get(MovOpc), Reg)
                           .addReg(Exec, RegState::Kill);
    auto FlipExecMI = BuildMI(MBB, MBBI, DL, TII->get(MovOpc), Exec).addImm(-1);
    if (Indexes) {
      Indexes->insertMachineInstrInMaps(*StoreExecMI);
      Indexes->insertMachineInstrInMaps(*FlipExecMI);
    }
  } else {
    const unsigned OrSaveExec =
        IsWave32 ? AMDGPU::S_OR_SAVEEXEC_B32 : AMDGPU::S_OR_SAVEEXEC_B64;
    auto SaveExec =
        BuildMI(MBB, MBBI, DL, TII->get(OrSaveExec), Reg).addImm(-1);
    SaveExec->getOperand(3).setIsDead(); // Mark SCC as dead.
    if (Indexes)
      Indexes->insertMachineInstrInMaps(*SaveExec);
  }
}

void SIInstrInfo::restoreExec(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              const DebugLoc &DL, Register Reg,
                              SlotIndexes *Indexes) const {
  unsigned ExecMov = isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
  MCRegister Exec = isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
  auto ExecRestoreMI =
      BuildMI(MBB, MBBI, DL, get(ExecMov), Exec).addReg(Reg, RegState::Kill);
  if (Indexes)
    Indexes->insertMachineInstrInMaps(*ExecRestoreMI);
}

static const TargetRegisterClass *
adjustAllocatableRegClass(const GCNSubtarget &ST, const SIRegisterInfo &RI,
                          const MachineRegisterInfo &MRI,
                          const MCInstrDesc &TID, unsigned RCID,
                          bool IsAllocatable) {
  if ((IsAllocatable || !ST.hasGFX90AInsts() || !MRI.reservedRegsFrozen()) &&
      (((TID.mayLoad() || TID.mayStore()) &&
        !(TID.TSFlags & SIInstrFlags::Spill)) ||
       (TID.TSFlags & (SIInstrFlags::DS | SIInstrFlags::MIMG)))) {
    switch (RCID) {
    case AMDGPU::AV_32RegClassID:
      RCID = AMDGPU::VGPR_32RegClassID;
      break;
    case AMDGPU::AV_64RegClassID:
      RCID = AMDGPU::VReg_64RegClassID;
      break;
    case AMDGPU::AV_96RegClassID:
      RCID = AMDGPU::VReg_96RegClassID;
      break;
    case AMDGPU::AV_128RegClassID:
      RCID = AMDGPU::VReg_128RegClassID;
      break;
    case AMDGPU::AV_160RegClassID:
      RCID = AMDGPU::VReg_160RegClassID;
      break;
    case AMDGPU::AV_512RegClassID:
      RCID = AMDGPU::VReg_512RegClassID;
      break;
    default:
      break;
    }
  }

  return RI.getProperlyAlignedRC(RI.getRegClass(RCID));
}

const TargetRegisterClass *SIInstrInfo::getRegClass(const MCInstrDesc &TID,
    unsigned OpNum, const TargetRegisterInfo *TRI,
    const MachineFunction &MF)
  const {
  if (OpNum >= TID.getNumOperands())
    return nullptr;
  auto RegClass = TID.operands()[OpNum].RegClass;
  bool IsAllocatable = false;
  if (TID.TSFlags & (SIInstrFlags::DS | SIInstrFlags::FLAT)) {
    // vdst and vdata should be both VGPR or AGPR, same for the DS instructions
    // with two data operands. Request register class constrained to VGPR only
    // of both operands present as Machine Copy Propagation can not check this
    // constraint and possibly other passes too.
    //
    // The check is limited to FLAT and DS because atomics in non-flat encoding
    // have their vdst and vdata tied to be the same register.
    const int VDstIdx = AMDGPU::getNamedOperandIdx(TID.Opcode,
                                                   AMDGPU::OpName::vdst);
    const int DataIdx = AMDGPU::getNamedOperandIdx(TID.Opcode,
        (TID.TSFlags & SIInstrFlags::DS) ? AMDGPU::OpName::data0
                                         : AMDGPU::OpName::vdata);
    if (DataIdx != -1) {
      IsAllocatable = VDstIdx != -1 || AMDGPU::hasNamedOperand(
                                           TID.Opcode, AMDGPU::OpName::data1);
    }
  }
  return adjustAllocatableRegClass(ST, RI, MF.getRegInfo(), TID, RegClass,
                                   IsAllocatable);
}

const TargetRegisterClass *SIInstrInfo::getOpRegClass(const MachineInstr &MI,
                                                      unsigned OpNo) const {
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  const MCInstrDesc &Desc = get(MI.getOpcode());
  if (MI.isVariadic() || OpNo >= Desc.getNumOperands() ||
      Desc.operands()[OpNo].RegClass == -1) {
    Register Reg = MI.getOperand(OpNo).getReg();

    if (Reg.isVirtual())
      return MRI.getRegClass(Reg);
    return RI.getPhysRegBaseClass(Reg);
  }

  unsigned RCID = Desc.operands()[OpNo].RegClass;
  return adjustAllocatableRegClass(ST, RI, MRI, Desc, RCID, true);
}

void SIInstrInfo::legalizeOpWithMove(MachineInstr &MI, unsigned OpIdx) const {
  MachineBasicBlock::iterator I = MI;
  MachineBasicBlock *MBB = MI.getParent();
  MachineOperand &MO = MI.getOperand(OpIdx);
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  unsigned RCID = get(MI.getOpcode()).operands()[OpIdx].RegClass;
  const TargetRegisterClass *RC = RI.getRegClass(RCID);
  unsigned Size = RI.getRegSizeInBits(*RC);
  unsigned Opcode = (Size == 64) ? AMDGPU::V_MOV_B64_PSEUDO : AMDGPU::V_MOV_B32_e32;
  if (MO.isReg())
    Opcode = AMDGPU::COPY;
  else if (RI.isSGPRClass(RC))
    Opcode = (Size == 64) ? AMDGPU::S_MOV_B64 : AMDGPU::S_MOV_B32;

  const TargetRegisterClass *VRC = RI.getEquivalentVGPRClass(RC);
  Register Reg = MRI.createVirtualRegister(VRC);
  DebugLoc DL = MBB->findDebugLoc(I);
  BuildMI(*MI.getParent(), I, DL, get(Opcode), Reg).add(MO);
  MO.ChangeToRegister(Reg, false);
}

unsigned SIInstrInfo::buildExtractSubReg(
    MachineBasicBlock::iterator MI, MachineRegisterInfo &MRI,
    const MachineOperand &SuperReg, const TargetRegisterClass *SuperRC,
    unsigned SubIdx, const TargetRegisterClass *SubRC) const {
  MachineBasicBlock *MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  Register SubReg = MRI.createVirtualRegister(SubRC);

  unsigned NewSubIdx = RI.composeSubRegIndices(SuperReg.getSubReg(), SubIdx);
  BuildMI(*MBB, MI, DL, get(TargetOpcode::COPY), SubReg)
      .addReg(SuperReg.getReg(), 0, NewSubIdx);
  return SubReg;
}

MachineOperand SIInstrInfo::buildExtractSubRegOrImm(
    MachineBasicBlock::iterator MII, MachineRegisterInfo &MRI,
    const MachineOperand &Op, const TargetRegisterClass *SuperRC,
    unsigned SubIdx, const TargetRegisterClass *SubRC) const {
  if (Op.isImm()) {
    if (SubIdx == AMDGPU::sub0)
      return MachineOperand::CreateImm(static_cast<int32_t>(Op.getImm()));
    if (SubIdx == AMDGPU::sub1)
      return MachineOperand::CreateImm(static_cast<int32_t>(Op.getImm() >> 32));

    llvm_unreachable("Unhandled register index for immediate");
  }

  unsigned SubReg = buildExtractSubReg(MII, MRI, Op, SuperRC,
                                       SubIdx, SubRC);
  return MachineOperand::CreateReg(SubReg, false);
}

// Change the order of operands from (0, 1, 2) to (0, 2, 1)
void SIInstrInfo::swapOperands(MachineInstr &Inst) const {
  assert(Inst.getNumExplicitOperands() == 3);
  MachineOperand Op1 = Inst.getOperand(1);
  Inst.removeOperand(1);
  Inst.addOperand(Op1);
}

bool SIInstrInfo::isLegalRegOperand(const MachineRegisterInfo &MRI,
                                    const MCOperandInfo &OpInfo,
                                    const MachineOperand &MO) const {
  if (!MO.isReg())
    return false;

  Register Reg = MO.getReg();

  const TargetRegisterClass *DRC = RI.getRegClass(OpInfo.RegClass);
  if (Reg.isPhysical())
    return DRC->contains(Reg);

  const TargetRegisterClass *RC = MRI.getRegClass(Reg);

  if (MO.getSubReg()) {
    const MachineFunction *MF = MO.getParent()->getParent()->getParent();
    const TargetRegisterClass *SuperRC = RI.getLargestLegalSuperClass(RC, *MF);
    if (!SuperRC)
      return false;

    DRC = RI.getMatchingSuperRegClass(SuperRC, DRC, MO.getSubReg());
    if (!DRC)
      return false;
  }
  return RC->hasSuperClassEq(DRC);
}

bool SIInstrInfo::isLegalVSrcOperand(const MachineRegisterInfo &MRI,
                                     const MCOperandInfo &OpInfo,
                                     const MachineOperand &MO) const {
  if (MO.isReg())
    return isLegalRegOperand(MRI, OpInfo, MO);

  // Handle non-register types that are treated like immediates.
  assert(MO.isImm() || MO.isTargetIndex() || MO.isFI() || MO.isGlobal());
  return true;
}

bool SIInstrInfo::isOperandLegal(const MachineInstr &MI, unsigned OpIdx,
                                 const MachineOperand *MO) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const MCInstrDesc &InstDesc = MI.getDesc();
  const MCOperandInfo &OpInfo = InstDesc.operands()[OpIdx];
  const TargetRegisterClass *DefinedRC =
      OpInfo.RegClass != -1 ? RI.getRegClass(OpInfo.RegClass) : nullptr;
  if (!MO)
    MO = &MI.getOperand(OpIdx);

  int ConstantBusLimit = ST.getConstantBusLimit(MI.getOpcode());
  int LiteralLimit = !isVOP3(MI) || ST.hasVOP3Literal() ? 1 : 0;
  if (isVALU(MI) && usesConstantBus(MRI, *MO, OpInfo)) {
    if (!MO->isReg() && !isInlineConstant(*MO, OpInfo) && !LiteralLimit--)
      return false;

    SmallDenseSet<RegSubRegPair> SGPRsUsed;
    if (MO->isReg())
      SGPRsUsed.insert(RegSubRegPair(MO->getReg(), MO->getSubReg()));

    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      if (i == OpIdx)
        continue;
      const MachineOperand &Op = MI.getOperand(i);
      if (Op.isReg()) {
        RegSubRegPair SGPR(Op.getReg(), Op.getSubReg());
        if (!SGPRsUsed.count(SGPR) &&
            // FIXME: This can access off the end of the operands() array.
            usesConstantBus(MRI, Op, InstDesc.operands().begin()[i])) {
          if (--ConstantBusLimit <= 0)
            return false;
          SGPRsUsed.insert(SGPR);
        }
      } else if (AMDGPU::isSISrcOperand(InstDesc, i) &&
                 !isInlineConstant(Op, InstDesc.operands()[i])) {
        if (!LiteralLimit--)
          return false;
        if (--ConstantBusLimit <= 0)
          return false;
      }
    }
  } else if (ST.hasNoF16PseudoScalarTransInlineConstants() && !MO->isReg() &&
             isF16PseudoScalarTrans(MI.getOpcode()) &&
             isInlineConstant(*MO, OpInfo)) {
    return false;
  }

  if (MO->isReg()) {
    if (!DefinedRC)
      return OpInfo.OperandType == MCOI::OPERAND_UNKNOWN;
    if (!isLegalRegOperand(MRI, OpInfo, *MO))
      return false;
    bool IsAGPR = RI.isAGPR(MRI, MO->getReg());
    if (IsAGPR && !ST.hasMAIInsts())
      return false;
    unsigned Opc = MI.getOpcode();
    if (IsAGPR &&
        (!ST.hasGFX90AInsts() || !MRI.reservedRegsFrozen()) &&
        (MI.mayLoad() || MI.mayStore() || isDS(Opc) || isMIMG(Opc)))
      return false;
    // Atomics should have both vdst and vdata either vgpr or agpr.
    const int VDstIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst);
    const int DataIdx = AMDGPU::getNamedOperandIdx(Opc,
        isDS(Opc) ? AMDGPU::OpName::data0 : AMDGPU::OpName::vdata);
    if ((int)OpIdx == VDstIdx && DataIdx != -1 &&
        MI.getOperand(DataIdx).isReg() &&
        RI.isAGPR(MRI, MI.getOperand(DataIdx).getReg()) != IsAGPR)
      return false;
    if ((int)OpIdx == DataIdx) {
      if (VDstIdx != -1 &&
          RI.isAGPR(MRI, MI.getOperand(VDstIdx).getReg()) != IsAGPR)
        return false;
      // DS instructions with 2 src operands also must have tied RC.
      const int Data1Idx = AMDGPU::getNamedOperandIdx(Opc,
                                                      AMDGPU::OpName::data1);
      if (Data1Idx != -1 && MI.getOperand(Data1Idx).isReg() &&
          RI.isAGPR(MRI, MI.getOperand(Data1Idx).getReg()) != IsAGPR)
        return false;
    }
    if (Opc == AMDGPU::V_ACCVGPR_WRITE_B32_e64 && !ST.hasGFX90AInsts() &&
        (int)OpIdx == AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0) &&
        RI.isSGPRReg(MRI, MO->getReg()))
      return false;
    return true;
  }

  if (MO->isImm()) {
    uint64_t Imm = MO->getImm();
    bool Is64BitFPOp = OpInfo.OperandType == AMDGPU::OPERAND_REG_IMM_FP64;
    bool Is64BitOp = Is64BitFPOp ||
                     OpInfo.OperandType == AMDGPU::OPERAND_REG_IMM_INT64 ||
                     OpInfo.OperandType == AMDGPU::OPERAND_REG_IMM_V2INT32 ||
                     OpInfo.OperandType == AMDGPU::OPERAND_REG_IMM_V2FP32;
    if (Is64BitOp &&
        !AMDGPU::isInlinableLiteral64(Imm, ST.hasInv2PiInlineImm())) {
      if (!AMDGPU::isValid32BitLiteral(Imm, Is64BitFPOp))
        return false;

      // FIXME: We can use sign extended 64-bit literals, but only for signed
      //        operands. At the moment we do not know if an operand is signed.
      //        Such operand will be encoded as its low 32 bits and then either
      //        correctly sign extended or incorrectly zero extended by HW.
      if (!Is64BitFPOp && (int32_t)Imm < 0)
        return false;
    }
  }

  // Handle non-register types that are treated like immediates.
  assert(MO->isImm() || MO->isTargetIndex() || MO->isFI() || MO->isGlobal());

  if (!DefinedRC) {
    // This operand expects an immediate.
    return true;
  }

  return isImmOperandLegal(MI, OpIdx, *MO);
}

void SIInstrInfo::legalizeOperandsVOP2(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  const MCInstrDesc &InstrDesc = get(Opc);

  int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
  MachineOperand &Src0 = MI.getOperand(Src0Idx);

  int Src1Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1);
  MachineOperand &Src1 = MI.getOperand(Src1Idx);

  // If there is an implicit SGPR use such as VCC use for v_addc_u32/v_subb_u32
  // we need to only have one constant bus use before GFX10.
  bool HasImplicitSGPR = findImplicitSGPRRead(MI);
  if (HasImplicitSGPR && ST.getConstantBusLimit(Opc) <= 1 && Src0.isReg() &&
      RI.isSGPRReg(MRI, Src0.getReg()))
    legalizeOpWithMove(MI, Src0Idx);

  // Special case: V_WRITELANE_B32 accepts only immediate or SGPR operands for
  // both the value to write (src0) and lane select (src1).  Fix up non-SGPR
  // src0/src1 with V_READFIRSTLANE.
  if (Opc == AMDGPU::V_WRITELANE_B32) {
    const DebugLoc &DL = MI.getDebugLoc();
    if (Src0.isReg() && RI.isVGPR(MRI, Src0.getReg())) {
      Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
          .add(Src0);
      Src0.ChangeToRegister(Reg, false);
    }
    if (Src1.isReg() && RI.isVGPR(MRI, Src1.getReg())) {
      Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      const DebugLoc &DL = MI.getDebugLoc();
      BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
          .add(Src1);
      Src1.ChangeToRegister(Reg, false);
    }
    return;
  }

  // No VOP2 instructions support AGPRs.
  if (Src0.isReg() && RI.isAGPR(MRI, Src0.getReg()))
    legalizeOpWithMove(MI, Src0Idx);

  if (Src1.isReg() && RI.isAGPR(MRI, Src1.getReg()))
    legalizeOpWithMove(MI, Src1Idx);

  // Special case: V_FMAC_F32 and V_FMAC_F16 have src2.
  if (Opc == AMDGPU::V_FMAC_F32_e32 || Opc == AMDGPU::V_FMAC_F16_e32) {
    int Src2Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2);
    if (!RI.isVGPR(MRI, MI.getOperand(Src2Idx).getReg()))
      legalizeOpWithMove(MI, Src2Idx);
  }

  // VOP2 src0 instructions support all operand types, so we don't need to check
  // their legality. If src1 is already legal, we don't need to do anything.
  if (isLegalRegOperand(MRI, InstrDesc.operands()[Src1Idx], Src1))
    return;

  // Special case: V_READLANE_B32 accepts only immediate or SGPR operands for
  // lane select. Fix up using V_READFIRSTLANE, since we assume that the lane
  // select is uniform.
  if (Opc == AMDGPU::V_READLANE_B32 && Src1.isReg() &&
      RI.isVGPR(MRI, Src1.getReg())) {
    Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
    const DebugLoc &DL = MI.getDebugLoc();
    BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
        .add(Src1);
    Src1.ChangeToRegister(Reg, false);
    return;
  }

  // We do not use commuteInstruction here because it is too aggressive and will
  // commute if it is possible. We only want to commute here if it improves
  // legality. This can be called a fairly large number of times so don't waste
  // compile time pointlessly swapping and checking legality again.
  if (HasImplicitSGPR || !MI.isCommutable()) {
    legalizeOpWithMove(MI, Src1Idx);
    return;
  }

  // If src0 can be used as src1, commuting will make the operands legal.
  // Otherwise we have to give up and insert a move.
  //
  // TODO: Other immediate-like operand kinds could be commuted if there was a
  // MachineOperand::ChangeTo* for them.
  if ((!Src1.isImm() && !Src1.isReg()) ||
      !isLegalRegOperand(MRI, InstrDesc.operands()[Src1Idx], Src0)) {
    legalizeOpWithMove(MI, Src1Idx);
    return;
  }

  int CommutedOpc = commuteOpcode(MI);
  if (CommutedOpc == -1) {
    legalizeOpWithMove(MI, Src1Idx);
    return;
  }

  MI.setDesc(get(CommutedOpc));

  Register Src0Reg = Src0.getReg();
  unsigned Src0SubReg = Src0.getSubReg();
  bool Src0Kill = Src0.isKill();

  if (Src1.isImm())
    Src0.ChangeToImmediate(Src1.getImm());
  else if (Src1.isReg()) {
    Src0.ChangeToRegister(Src1.getReg(), false, false, Src1.isKill());
    Src0.setSubReg(Src1.getSubReg());
  } else
    llvm_unreachable("Should only have register or immediate operands");

  Src1.ChangeToRegister(Src0Reg, false, false, Src0Kill);
  Src1.setSubReg(Src0SubReg);
  fixImplicitOperands(MI);
}

// Legalize VOP3 operands. All operand types are supported for any operand
// but only one literal constant and only starting from GFX10.
void SIInstrInfo::legalizeOperandsVOP3(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  int VOP3Idx[3] = {
    AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0),
    AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1),
    AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2)
  };

  if (Opc == AMDGPU::V_PERMLANE16_B32_e64 ||
      Opc == AMDGPU::V_PERMLANEX16_B32_e64) {
    // src1 and src2 must be scalar
    MachineOperand &Src1 = MI.getOperand(VOP3Idx[1]);
    MachineOperand &Src2 = MI.getOperand(VOP3Idx[2]);
    const DebugLoc &DL = MI.getDebugLoc();
    if (Src1.isReg() && !RI.isSGPRClass(MRI.getRegClass(Src1.getReg()))) {
      Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
        .add(Src1);
      Src1.ChangeToRegister(Reg, false);
    }
    if (Src2.isReg() && !RI.isSGPRClass(MRI.getRegClass(Src2.getReg()))) {
      Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
        .add(Src2);
      Src2.ChangeToRegister(Reg, false);
    }
  }

  // Find the one SGPR operand we are allowed to use.
  int ConstantBusLimit = ST.getConstantBusLimit(Opc);
  int LiteralLimit = ST.hasVOP3Literal() ? 1 : 0;
  SmallDenseSet<unsigned> SGPRsUsed;
  Register SGPRReg = findUsedSGPR(MI, VOP3Idx);
  if (SGPRReg) {
    SGPRsUsed.insert(SGPRReg);
    --ConstantBusLimit;
  }

  for (int Idx : VOP3Idx) {
    if (Idx == -1)
      break;
    MachineOperand &MO = MI.getOperand(Idx);

    if (!MO.isReg()) {
      if (isInlineConstant(MO, get(Opc).operands()[Idx]))
        continue;

      if (LiteralLimit > 0 && ConstantBusLimit > 0) {
        --LiteralLimit;
        --ConstantBusLimit;
        continue;
      }

      --LiteralLimit;
      --ConstantBusLimit;
      legalizeOpWithMove(MI, Idx);
      continue;
    }

    if (RI.hasAGPRs(RI.getRegClassForReg(MRI, MO.getReg())) &&
        !isOperandLegal(MI, Idx, &MO)) {
      legalizeOpWithMove(MI, Idx);
      continue;
    }

    if (!RI.isSGPRClass(RI.getRegClassForReg(MRI, MO.getReg())))
      continue; // VGPRs are legal

    // We can use one SGPR in each VOP3 instruction prior to GFX10
    // and two starting from GFX10.
    if (SGPRsUsed.count(MO.getReg()))
      continue;
    if (ConstantBusLimit > 0) {
      SGPRsUsed.insert(MO.getReg());
      --ConstantBusLimit;
      continue;
    }

    // If we make it this far, then the operand is not legal and we must
    // legalize it.
    legalizeOpWithMove(MI, Idx);
  }

  // Special case: V_FMAC_F32 and V_FMAC_F16 have src2 tied to vdst.
  if ((Opc == AMDGPU::V_FMAC_F32_e64 || Opc == AMDGPU::V_FMAC_F16_e64) &&
      !RI.isVGPR(MRI, MI.getOperand(VOP3Idx[2]).getReg()))
    legalizeOpWithMove(MI, VOP3Idx[2]);
}

Register SIInstrInfo::readlaneVGPRToSGPR(Register SrcReg, MachineInstr &UseMI,
                                         MachineRegisterInfo &MRI) const {
  const TargetRegisterClass *VRC = MRI.getRegClass(SrcReg);
  const TargetRegisterClass *SRC = RI.getEquivalentSGPRClass(VRC);
  Register DstReg = MRI.createVirtualRegister(SRC);
  unsigned SubRegs = RI.getRegSizeInBits(*VRC) / 32;

  if (RI.hasAGPRs(VRC)) {
    VRC = RI.getEquivalentVGPRClass(VRC);
    Register NewSrcReg = MRI.createVirtualRegister(VRC);
    BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
            get(TargetOpcode::COPY), NewSrcReg)
        .addReg(SrcReg);
    SrcReg = NewSrcReg;
  }

  if (SubRegs == 1) {
    BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
            get(AMDGPU::V_READFIRSTLANE_B32), DstReg)
        .addReg(SrcReg);
    return DstReg;
  }

  SmallVector<Register, 8> SRegs;
  for (unsigned i = 0; i < SubRegs; ++i) {
    Register SGPR = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
    BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
            get(AMDGPU::V_READFIRSTLANE_B32), SGPR)
        .addReg(SrcReg, 0, RI.getSubRegFromChannel(i));
    SRegs.push_back(SGPR);
  }

  MachineInstrBuilder MIB =
      BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
              get(AMDGPU::REG_SEQUENCE), DstReg);
  for (unsigned i = 0; i < SubRegs; ++i) {
    MIB.addReg(SRegs[i]);
    MIB.addImm(RI.getSubRegFromChannel(i));
  }
  return DstReg;
}

void SIInstrInfo::legalizeOperandsSMRD(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {

  // If the pointer is store in VGPRs, then we need to move them to
  // SGPRs using v_readfirstlane.  This is safe because we only select
  // loads with uniform pointers to SMRD instruction so we know the
  // pointer value is uniform.
  MachineOperand *SBase = getNamedOperand(MI, AMDGPU::OpName::sbase);
  if (SBase && !RI.isSGPRClass(MRI.getRegClass(SBase->getReg()))) {
    Register SGPR = readlaneVGPRToSGPR(SBase->getReg(), MI, MRI);
    SBase->setReg(SGPR);
  }
  MachineOperand *SOff = getNamedOperand(MI, AMDGPU::OpName::soffset);
  if (SOff && !RI.isSGPRClass(MRI.getRegClass(SOff->getReg()))) {
    Register SGPR = readlaneVGPRToSGPR(SOff->getReg(), MI, MRI);
    SOff->setReg(SGPR);
  }
}

bool SIInstrInfo::moveFlatAddrToVGPR(MachineInstr &Inst) const {
  unsigned Opc = Inst.getOpcode();
  int OldSAddrIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::saddr);
  if (OldSAddrIdx < 0)
    return false;

  assert(isSegmentSpecificFLAT(Inst));

  int NewOpc = AMDGPU::getGlobalVaddrOp(Opc);
  if (NewOpc < 0)
    NewOpc = AMDGPU::getFlatScratchInstSVfromSS(Opc);
  if (NewOpc < 0)
    return false;

  MachineRegisterInfo &MRI = Inst.getMF()->getRegInfo();
  MachineOperand &SAddr = Inst.getOperand(OldSAddrIdx);
  if (RI.isSGPRReg(MRI, SAddr.getReg()))
    return false;

  int NewVAddrIdx = AMDGPU::getNamedOperandIdx(NewOpc, AMDGPU::OpName::vaddr);
  if (NewVAddrIdx < 0)
    return false;

  int OldVAddrIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vaddr);

  // Check vaddr, it shall be zero or absent.
  MachineInstr *VAddrDef = nullptr;
  if (OldVAddrIdx >= 0) {
    MachineOperand &VAddr = Inst.getOperand(OldVAddrIdx);
    VAddrDef = MRI.getUniqueVRegDef(VAddr.getReg());
    if (!VAddrDef || VAddrDef->getOpcode() != AMDGPU::V_MOV_B32_e32 ||
        !VAddrDef->getOperand(1).isImm() ||
        VAddrDef->getOperand(1).getImm() != 0)
      return false;
  }

  const MCInstrDesc &NewDesc = get(NewOpc);
  Inst.setDesc(NewDesc);

  // Callers expect iterator to be valid after this call, so modify the
  // instruction in place.
  if (OldVAddrIdx == NewVAddrIdx) {
    MachineOperand &NewVAddr = Inst.getOperand(NewVAddrIdx);
    // Clear use list from the old vaddr holding a zero register.
    MRI.removeRegOperandFromUseList(&NewVAddr);
    MRI.moveOperands(&NewVAddr, &SAddr, 1);
    Inst.removeOperand(OldSAddrIdx);
    // Update the use list with the pointer we have just moved from vaddr to
    // saddr position. Otherwise new vaddr will be missing from the use list.
    MRI.removeRegOperandFromUseList(&NewVAddr);
    MRI.addRegOperandToUseList(&NewVAddr);
  } else {
    assert(OldSAddrIdx == NewVAddrIdx);

    if (OldVAddrIdx >= 0) {
      int NewVDstIn = AMDGPU::getNamedOperandIdx(NewOpc,
                                                 AMDGPU::OpName::vdst_in);

      // removeOperand doesn't try to fixup tied operand indexes at it goes, so
      // it asserts. Untie the operands for now and retie them afterwards.
      if (NewVDstIn != -1) {
        int OldVDstIn = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst_in);
        Inst.untieRegOperand(OldVDstIn);
      }

      Inst.removeOperand(OldVAddrIdx);

      if (NewVDstIn != -1) {
        int NewVDst = AMDGPU::getNamedOperandIdx(NewOpc, AMDGPU::OpName::vdst);
        Inst.tieOperands(NewVDst, NewVDstIn);
      }
    }
  }

  if (VAddrDef && MRI.use_nodbg_empty(VAddrDef->getOperand(0).getReg()))
    VAddrDef->eraseFromParent();

  return true;
}

// FIXME: Remove this when SelectionDAG is obsoleted.
void SIInstrInfo::legalizeOperandsFLAT(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {
  if (!isSegmentSpecificFLAT(MI))
    return;

  // Fixup SGPR operands in VGPRs. We only select these when the DAG divergence
  // thinks they are uniform, so a readfirstlane should be valid.
  MachineOperand *SAddr = getNamedOperand(MI, AMDGPU::OpName::saddr);
  if (!SAddr || RI.isSGPRClass(MRI.getRegClass(SAddr->getReg())))
    return;

  if (moveFlatAddrToVGPR(MI))
    return;

  Register ToSGPR = readlaneVGPRToSGPR(SAddr->getReg(), MI, MRI);
  SAddr->setReg(ToSGPR);
}

void SIInstrInfo::legalizeGenericOperand(MachineBasicBlock &InsertMBB,
                                         MachineBasicBlock::iterator I,
                                         const TargetRegisterClass *DstRC,
                                         MachineOperand &Op,
                                         MachineRegisterInfo &MRI,
                                         const DebugLoc &DL) const {
  Register OpReg = Op.getReg();
  unsigned OpSubReg = Op.getSubReg();

  const TargetRegisterClass *OpRC = RI.getSubClassWithSubReg(
      RI.getRegClassForReg(MRI, OpReg), OpSubReg);

  // Check if operand is already the correct register class.
  if (DstRC == OpRC)
    return;

  Register DstReg = MRI.createVirtualRegister(DstRC);
  auto Copy = BuildMI(InsertMBB, I, DL, get(AMDGPU::COPY), DstReg).add(Op);

  Op.setReg(DstReg);
  Op.setSubReg(0);

  MachineInstr *Def = MRI.getVRegDef(OpReg);
  if (!Def)
    return;

  // Try to eliminate the copy if it is copying an immediate value.
  if (Def->isMoveImmediate() && DstRC != &AMDGPU::VReg_1RegClass)
    foldImmediate(*Copy, *Def, OpReg, &MRI);

  bool ImpDef = Def->isImplicitDef();
  while (!ImpDef && Def && Def->isCopy()) {
    if (Def->getOperand(1).getReg().isPhysical())
      break;
    Def = MRI.getUniqueVRegDef(Def->getOperand(1).getReg());
    ImpDef = Def && Def->isImplicitDef();
  }
  if (!RI.isSGPRClass(DstRC) && !Copy->readsRegister(AMDGPU::EXEC, &RI) &&
      !ImpDef)
    Copy.addReg(AMDGPU::EXEC, RegState::Implicit);
}

// Emit the actual waterfall loop, executing the wrapped instruction for each
// unique value of \p ScalarOps across all lanes. In the best case we execute 1
// iteration, in the worst case we execute 64 (once per lane).
static void emitLoadScalarOpsFromVGPRLoop(
    const SIInstrInfo &TII, MachineRegisterInfo &MRI, MachineBasicBlock &OrigBB,
    MachineBasicBlock &LoopBB, MachineBasicBlock &BodyBB, const DebugLoc &DL,
    ArrayRef<MachineOperand *> ScalarOps) {
  MachineFunction &MF = *OrigBB.getParent();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
  unsigned SaveExecOpc =
      ST.isWave32() ? AMDGPU::S_AND_SAVEEXEC_B32 : AMDGPU::S_AND_SAVEEXEC_B64;
  unsigned XorTermOpc =
      ST.isWave32() ? AMDGPU::S_XOR_B32_term : AMDGPU::S_XOR_B64_term;
  unsigned AndOpc =
      ST.isWave32() ? AMDGPU::S_AND_B32 : AMDGPU::S_AND_B64;
  const auto *BoolXExecRC = TRI->getRegClass(AMDGPU::SReg_1_XEXECRegClassID);

  MachineBasicBlock::iterator I = LoopBB.begin();

  SmallVector<Register, 8> ReadlanePieces;
  Register CondReg;

  for (MachineOperand *ScalarOp : ScalarOps) {
    unsigned RegSize = TRI->getRegSizeInBits(ScalarOp->getReg(), MRI);
    unsigned NumSubRegs = RegSize / 32;
    Register VScalarOp = ScalarOp->getReg();

    if (NumSubRegs == 1) {
      Register CurReg = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);

      BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), CurReg)
          .addReg(VScalarOp);

      Register NewCondReg = MRI.createVirtualRegister(BoolXExecRC);

      BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_CMP_EQ_U32_e64), NewCondReg)
          .addReg(CurReg)
          .addReg(VScalarOp);

      // Combine the comparison results with AND.
      if (!CondReg) // First.
        CondReg = NewCondReg;
      else { // If not the first, we create an AND.
        Register AndReg = MRI.createVirtualRegister(BoolXExecRC);
        BuildMI(LoopBB, I, DL, TII.get(AndOpc), AndReg)
            .addReg(CondReg)
            .addReg(NewCondReg);
        CondReg = AndReg;
      }

      // Update ScalarOp operand to use the SGPR ScalarOp.
      ScalarOp->setReg(CurReg);
      ScalarOp->setIsKill();
    } else {
      unsigned VScalarOpUndef = getUndefRegState(ScalarOp->isUndef());
      assert(NumSubRegs % 2 == 0 && NumSubRegs <= 32 &&
             "Unhandled register size");

      for (unsigned Idx = 0; Idx < NumSubRegs; Idx += 2) {
        Register CurRegLo = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
        Register CurRegHi = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);

        // Read the next variant <- also loop target.
        BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), CurRegLo)
            .addReg(VScalarOp, VScalarOpUndef, TRI->getSubRegFromChannel(Idx));

        // Read the next variant <- also loop target.
        BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), CurRegHi)
            .addReg(VScalarOp, VScalarOpUndef,
                    TRI->getSubRegFromChannel(Idx + 1));

        ReadlanePieces.push_back(CurRegLo);
        ReadlanePieces.push_back(CurRegHi);

        // Comparison is to be done as 64-bit.
        Register CurReg = MRI.createVirtualRegister(&AMDGPU::SGPR_64RegClass);
        BuildMI(LoopBB, I, DL, TII.get(AMDGPU::REG_SEQUENCE), CurReg)
            .addReg(CurRegLo)
            .addImm(AMDGPU::sub0)
            .addReg(CurRegHi)
            .addImm(AMDGPU::sub1);

        Register NewCondReg = MRI.createVirtualRegister(BoolXExecRC);
        auto Cmp = BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_CMP_EQ_U64_e64),
                           NewCondReg)
                       .addReg(CurReg);
        if (NumSubRegs <= 2)
          Cmp.addReg(VScalarOp);
        else
          Cmp.addReg(VScalarOp, VScalarOpUndef,
                     TRI->getSubRegFromChannel(Idx, 2));

        // Combine the comparison results with AND.
        if (!CondReg) // First.
          CondReg = NewCondReg;
        else { // If not the first, we create an AND.
          Register AndReg = MRI.createVirtualRegister(BoolXExecRC);
          BuildMI(LoopBB, I, DL, TII.get(AndOpc), AndReg)
              .addReg(CondReg)
              .addReg(NewCondReg);
          CondReg = AndReg;
        }
      } // End for loop.

      auto SScalarOpRC =
          TRI->getEquivalentSGPRClass(MRI.getRegClass(VScalarOp));
      Register SScalarOp = MRI.createVirtualRegister(SScalarOpRC);

      // Build scalar ScalarOp.
      auto Merge =
          BuildMI(LoopBB, I, DL, TII.get(AMDGPU::REG_SEQUENCE), SScalarOp);
      unsigned Channel = 0;
      for (Register Piece : ReadlanePieces) {
        Merge.addReg(Piece).addImm(TRI->getSubRegFromChannel(Channel++));
      }

      // Update ScalarOp operand to use the SGPR ScalarOp.
      ScalarOp->setReg(SScalarOp);
      ScalarOp->setIsKill();
    }
  }

  Register SaveExec = MRI.createVirtualRegister(BoolXExecRC);
  MRI.setSimpleHint(SaveExec, CondReg);

  // Update EXEC to matching lanes, saving original to SaveExec.
  BuildMI(LoopBB, I, DL, TII.get(SaveExecOpc), SaveExec)
      .addReg(CondReg, RegState::Kill);

  // The original instruction is here; we insert the terminators after it.
  I = BodyBB.end();

  // Update EXEC, switch all done bits to 0 and all todo bits to 1.
  BuildMI(BodyBB, I, DL, TII.get(XorTermOpc), Exec)
      .addReg(Exec)
      .addReg(SaveExec);

  BuildMI(BodyBB, I, DL, TII.get(AMDGPU::SI_WATERFALL_LOOP)).addMBB(&LoopBB);
}

// Build a waterfall loop around \p MI, replacing the VGPR \p ScalarOp register
// with SGPRs by iterating over all unique values across all lanes.
// Returns the loop basic block that now contains \p MI.
static MachineBasicBlock *
loadMBUFScalarOperandsFromVGPR(const SIInstrInfo &TII, MachineInstr &MI,
                               ArrayRef<MachineOperand *> ScalarOps,
                               MachineDominatorTree *MDT,
                               MachineBasicBlock::iterator Begin = nullptr,
                               MachineBasicBlock::iterator End = nullptr) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  if (!Begin.isValid())
    Begin = &MI;
  if (!End.isValid()) {
    End = &MI;
    ++End;
  }
  const DebugLoc &DL = MI.getDebugLoc();
  unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
  unsigned MovExecOpc = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
  const auto *BoolXExecRC = TRI->getRegClass(AMDGPU::SReg_1_XEXECRegClassID);

  // Save SCC. Waterfall Loop may overwrite SCC.
  Register SaveSCCReg;

  // FIXME: We should maintain SCC liveness while doing the FixSGPRCopies walk
  // rather than unlimited scan everywhere
  bool SCCNotDead =
      MBB.computeRegisterLiveness(TRI, AMDGPU::SCC, MI,
                                  std::numeric_limits<unsigned>::max()) !=
      MachineBasicBlock::LQR_Dead;
  if (SCCNotDead) {
    SaveSCCReg = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    BuildMI(MBB, Begin, DL, TII.get(AMDGPU::S_CSELECT_B32), SaveSCCReg)
        .addImm(1)
        .addImm(0);
  }

  Register SaveExec = MRI.createVirtualRegister(BoolXExecRC);

  // Save the EXEC mask
  BuildMI(MBB, Begin, DL, TII.get(MovExecOpc), SaveExec).addReg(Exec);

  // Killed uses in the instruction we are waterfalling around will be
  // incorrect due to the added control-flow.
  MachineBasicBlock::iterator AfterMI = MI;
  ++AfterMI;
  for (auto I = Begin; I != AfterMI; I++) {
    for (auto &MO : I->all_uses())
      MRI.clearKillFlags(MO.getReg());
  }

  // To insert the loop we need to split the block. Move everything after this
  // point to a new block, and insert a new empty block between the two.
  MachineBasicBlock *LoopBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *BodyBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *RemainderBB = MF.CreateMachineBasicBlock();
  MachineFunction::iterator MBBI(MBB);
  ++MBBI;

  MF.insert(MBBI, LoopBB);
  MF.insert(MBBI, BodyBB);
  MF.insert(MBBI, RemainderBB);

  LoopBB->addSuccessor(BodyBB);
  BodyBB->addSuccessor(LoopBB);
  BodyBB->addSuccessor(RemainderBB);

  // Move Begin to MI to the BodyBB, and the remainder of the block to
  // RemainderBB.
  RemainderBB->transferSuccessorsAndUpdatePHIs(&MBB);
  RemainderBB->splice(RemainderBB->begin(), &MBB, End, MBB.end());
  BodyBB->splice(BodyBB->begin(), &MBB, Begin, MBB.end());

  MBB.addSuccessor(LoopBB);

  // Update dominators. We know that MBB immediately dominates LoopBB, that
  // LoopBB immediately dominates BodyBB, and BodyBB immediately dominates
  // RemainderBB. RemainderBB immediately dominates all of the successors
  // transferred to it from MBB that MBB used to properly dominate.
  if (MDT) {
    MDT->addNewBlock(LoopBB, &MBB);
    MDT->addNewBlock(BodyBB, LoopBB);
    MDT->addNewBlock(RemainderBB, BodyBB);
    for (auto &Succ : RemainderBB->successors()) {
      if (MDT->properlyDominates(&MBB, Succ)) {
        MDT->changeImmediateDominator(Succ, RemainderBB);
      }
    }
  }

  emitLoadScalarOpsFromVGPRLoop(TII, MRI, MBB, *LoopBB, *BodyBB, DL, ScalarOps);

  MachineBasicBlock::iterator First = RemainderBB->begin();
  // Restore SCC
  if (SCCNotDead) {
    BuildMI(*RemainderBB, First, DL, TII.get(AMDGPU::S_CMP_LG_U32))
        .addReg(SaveSCCReg, RegState::Kill)
        .addImm(0);
  }

  // Restore the EXEC mask
  BuildMI(*RemainderBB, First, DL, TII.get(MovExecOpc), Exec).addReg(SaveExec);
  return BodyBB;
}

// Extract pointer from Rsrc and return a zero-value Rsrc replacement.
static std::tuple<unsigned, unsigned>
extractRsrcPtr(const SIInstrInfo &TII, MachineInstr &MI, MachineOperand &Rsrc) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Extract the ptr from the resource descriptor.
  unsigned RsrcPtr =
      TII.buildExtractSubReg(MI, MRI, Rsrc, &AMDGPU::VReg_128RegClass,
                             AMDGPU::sub0_sub1, &AMDGPU::VReg_64RegClass);

  // Create an empty resource descriptor
  Register Zero64 = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  Register SRsrcFormatLo = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  Register SRsrcFormatHi = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  Register NewSRsrc = MRI.createVirtualRegister(&AMDGPU::SGPR_128RegClass);
  uint64_t RsrcDataFormat = TII.getDefaultRsrcDataFormat();

  // Zero64 = 0
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::S_MOV_B64), Zero64)
      .addImm(0);

  // SRsrcFormatLo = RSRC_DATA_FORMAT{31-0}
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::S_MOV_B32), SRsrcFormatLo)
      .addImm(RsrcDataFormat & 0xFFFFFFFF);

  // SRsrcFormatHi = RSRC_DATA_FORMAT{63-32}
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::S_MOV_B32), SRsrcFormatHi)
      .addImm(RsrcDataFormat >> 32);

  // NewSRsrc = {Zero64, SRsrcFormat}
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::REG_SEQUENCE), NewSRsrc)
      .addReg(Zero64)
      .addImm(AMDGPU::sub0_sub1)
      .addReg(SRsrcFormatLo)
      .addImm(AMDGPU::sub2)
      .addReg(SRsrcFormatHi)
      .addImm(AMDGPU::sub3);

  return std::tuple(RsrcPtr, NewSRsrc);
}

MachineBasicBlock *
SIInstrInfo::legalizeOperands(MachineInstr &MI,
                              MachineDominatorTree *MDT) const {
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineBasicBlock *CreatedBB = nullptr;

  // Legalize VOP2
  if (isVOP2(MI) || isVOPC(MI)) {
    legalizeOperandsVOP2(MRI, MI);
    return CreatedBB;
  }

  // Legalize VOP3
  if (isVOP3(MI)) {
    legalizeOperandsVOP3(MRI, MI);
    return CreatedBB;
  }

  // Legalize SMRD
  if (isSMRD(MI)) {
    legalizeOperandsSMRD(MRI, MI);
    return CreatedBB;
  }

  // Legalize FLAT
  if (isFLAT(MI)) {
    legalizeOperandsFLAT(MRI, MI);
    return CreatedBB;
  }

  // Legalize REG_SEQUENCE and PHI
  // The register class of the operands much be the same type as the register
  // class of the output.
  if (MI.getOpcode() == AMDGPU::PHI) {
    const TargetRegisterClass *RC = nullptr, *SRC = nullptr, *VRC = nullptr;
    for (unsigned i = 1, e = MI.getNumOperands(); i != e; i += 2) {
      if (!MI.getOperand(i).isReg() || !MI.getOperand(i).getReg().isVirtual())
        continue;
      const TargetRegisterClass *OpRC =
          MRI.getRegClass(MI.getOperand(i).getReg());
      if (RI.hasVectorRegisters(OpRC)) {
        VRC = OpRC;
      } else {
        SRC = OpRC;
      }
    }

    // If any of the operands are VGPR registers, then they all most be
    // otherwise we will create illegal VGPR->SGPR copies when legalizing
    // them.
    if (VRC || !RI.isSGPRClass(getOpRegClass(MI, 0))) {
      if (!VRC) {
        assert(SRC);
        if (getOpRegClass(MI, 0) == &AMDGPU::VReg_1RegClass) {
          VRC = &AMDGPU::VReg_1RegClass;
        } else
          VRC = RI.isAGPRClass(getOpRegClass(MI, 0))
                    ? RI.getEquivalentAGPRClass(SRC)
                    : RI.getEquivalentVGPRClass(SRC);
      } else {
        VRC = RI.isAGPRClass(getOpRegClass(MI, 0))
                  ? RI.getEquivalentAGPRClass(VRC)
                  : RI.getEquivalentVGPRClass(VRC);
      }
      RC = VRC;
    } else {
      RC = SRC;
    }

    // Update all the operands so they have the same type.
    for (unsigned I = 1, E = MI.getNumOperands(); I != E; I += 2) {
      MachineOperand &Op = MI.getOperand(I);
      if (!Op.isReg() || !Op.getReg().isVirtual())
        continue;

      // MI is a PHI instruction.
      MachineBasicBlock *InsertBB = MI.getOperand(I + 1).getMBB();
      MachineBasicBlock::iterator Insert = InsertBB->getFirstTerminator();

      // Avoid creating no-op copies with the same src and dst reg class.  These
      // confuse some of the machine passes.
      legalizeGenericOperand(*InsertBB, Insert, RC, Op, MRI, MI.getDebugLoc());
    }
  }

  // REG_SEQUENCE doesn't really require operand legalization, but if one has a
  // VGPR dest type and SGPR sources, insert copies so all operands are
  // VGPRs. This seems to help operand folding / the register coalescer.
  if (MI.getOpcode() == AMDGPU::REG_SEQUENCE) {
    MachineBasicBlock *MBB = MI.getParent();
    const TargetRegisterClass *DstRC = getOpRegClass(MI, 0);
    if (RI.hasVGPRs(DstRC)) {
      // Update all the operands so they are VGPR register classes. These may
      // not be the same register class because REG_SEQUENCE supports mixing
      // subregister index types e.g. sub0_sub1 + sub2 + sub3
      for (unsigned I = 1, E = MI.getNumOperands(); I != E; I += 2) {
        MachineOperand &Op = MI.getOperand(I);
        if (!Op.isReg() || !Op.getReg().isVirtual())
          continue;

        const TargetRegisterClass *OpRC = MRI.getRegClass(Op.getReg());
        const TargetRegisterClass *VRC = RI.getEquivalentVGPRClass(OpRC);
        if (VRC == OpRC)
          continue;

        legalizeGenericOperand(*MBB, MI, VRC, Op, MRI, MI.getDebugLoc());
        Op.setIsKill();
      }
    }

    return CreatedBB;
  }

  // Legalize INSERT_SUBREG
  // src0 must have the same register class as dst
  if (MI.getOpcode() == AMDGPU::INSERT_SUBREG) {
    Register Dst = MI.getOperand(0).getReg();
    Register Src0 = MI.getOperand(1).getReg();
    const TargetRegisterClass *DstRC = MRI.getRegClass(Dst);
    const TargetRegisterClass *Src0RC = MRI.getRegClass(Src0);
    if (DstRC != Src0RC) {
      MachineBasicBlock *MBB = MI.getParent();
      MachineOperand &Op = MI.getOperand(1);
      legalizeGenericOperand(*MBB, MI, DstRC, Op, MRI, MI.getDebugLoc());
    }
    return CreatedBB;
  }

  // Legalize SI_INIT_M0
  if (MI.getOpcode() == AMDGPU::SI_INIT_M0) {
    MachineOperand &Src = MI.getOperand(0);
    if (Src.isReg() && RI.hasVectorRegisters(MRI.getRegClass(Src.getReg())))
      Src.setReg(readlaneVGPRToSGPR(Src.getReg(), MI, MRI));
    return CreatedBB;
  }

  // Legalize S_BITREPLICATE, S_QUADMASK and S_WQM
  if (MI.getOpcode() == AMDGPU::S_BITREPLICATE_B64_B32 ||
      MI.getOpcode() == AMDGPU::S_QUADMASK_B32 ||
      MI.getOpcode() == AMDGPU::S_QUADMASK_B64 ||
      MI.getOpcode() == AMDGPU::S_WQM_B32 ||
      MI.getOpcode() == AMDGPU::S_WQM_B64 ||
      MI.getOpcode() == AMDGPU::S_INVERSE_BALLOT_U32 ||
      MI.getOpcode() == AMDGPU::S_INVERSE_BALLOT_U64) {
    MachineOperand &Src = MI.getOperand(1);
    if (Src.isReg() && RI.hasVectorRegisters(MRI.getRegClass(Src.getReg())))
      Src.setReg(readlaneVGPRToSGPR(Src.getReg(), MI, MRI));
    return CreatedBB;
  }

  // Legalize MIMG/VIMAGE/VSAMPLE and MUBUF/MTBUF for shaders.
  //
  // Shaders only generate MUBUF/MTBUF instructions via intrinsics or via
  // scratch memory access. In both cases, the legalization never involves
  // conversion to the addr64 form.
  if (isImage(MI) || (AMDGPU::isGraphics(MF.getFunction().getCallingConv()) &&
                      (isMUBUF(MI) || isMTBUF(MI)))) {
    int RSrcOpName = (isVIMAGE(MI) || isVSAMPLE(MI)) ? AMDGPU::OpName::rsrc
                                                     : AMDGPU::OpName::srsrc;
    MachineOperand *SRsrc = getNamedOperand(MI, RSrcOpName);
    if (SRsrc && !RI.isSGPRClass(MRI.getRegClass(SRsrc->getReg())))
      CreatedBB = loadMBUFScalarOperandsFromVGPR(*this, MI, {SRsrc}, MDT);

    int SampOpName = isMIMG(MI) ? AMDGPU::OpName::ssamp : AMDGPU::OpName::samp;
    MachineOperand *SSamp = getNamedOperand(MI, SampOpName);
    if (SSamp && !RI.isSGPRClass(MRI.getRegClass(SSamp->getReg())))
      CreatedBB = loadMBUFScalarOperandsFromVGPR(*this, MI, {SSamp}, MDT);

    return CreatedBB;
  }

  // Legalize SI_CALL
  if (MI.getOpcode() == AMDGPU::SI_CALL_ISEL) {
    MachineOperand *Dest = &MI.getOperand(0);
    if (!RI.isSGPRClass(MRI.getRegClass(Dest->getReg()))) {
      // Move everything between ADJCALLSTACKUP and ADJCALLSTACKDOWN and
      // following copies, we also need to move copies from and to physical
      // registers into the loop block.
      unsigned FrameSetupOpcode = getCallFrameSetupOpcode();
      unsigned FrameDestroyOpcode = getCallFrameDestroyOpcode();

      // Also move the copies to physical registers into the loop block
      MachineBasicBlock &MBB = *MI.getParent();
      MachineBasicBlock::iterator Start(&MI);
      while (Start->getOpcode() != FrameSetupOpcode)
        --Start;
      MachineBasicBlock::iterator End(&MI);
      while (End->getOpcode() != FrameDestroyOpcode)
        ++End;
      // Also include following copies of the return value
      ++End;
      while (End != MBB.end() && End->isCopy() && End->getOperand(1).isReg() &&
             MI.definesRegister(End->getOperand(1).getReg(), /*TRI=*/nullptr))
        ++End;
      CreatedBB =
          loadMBUFScalarOperandsFromVGPR(*this, MI, {Dest}, MDT, Start, End);
    }
  }

  // Legalize s_sleep_var.
  if (MI.getOpcode() == AMDGPU::S_SLEEP_VAR) {
    const DebugLoc &DL = MI.getDebugLoc();
    Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
    int Src0Idx =
        AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::src0);
    MachineOperand &Src0 = MI.getOperand(Src0Idx);
    BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
        .add(Src0);
    Src0.ChangeToRegister(Reg, false);
    return nullptr;
  }

  // Legalize MUBUF instructions.
  bool isSoffsetLegal = true;
  int SoffsetIdx =
      AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::soffset);
  if (SoffsetIdx != -1) {
    MachineOperand *Soffset = &MI.getOperand(SoffsetIdx);
    if (Soffset->isReg() && Soffset->getReg().isVirtual() &&
        !RI.isSGPRClass(MRI.getRegClass(Soffset->getReg()))) {
      isSoffsetLegal = false;
    }
  }

  bool isRsrcLegal = true;
  int RsrcIdx =
      AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::srsrc);
  if (RsrcIdx != -1) {
    MachineOperand *Rsrc = &MI.getOperand(RsrcIdx);
    if (Rsrc->isReg() && !RI.isSGPRClass(MRI.getRegClass(Rsrc->getReg()))) {
      isRsrcLegal = false;
    }
  }

  // The operands are legal.
  if (isRsrcLegal && isSoffsetLegal)
    return CreatedBB;

  if (!isRsrcLegal) {
    // Legalize a VGPR Rsrc
    //
    // If the instruction is _ADDR64, we can avoid a waterfall by extracting
    // the base pointer from the VGPR Rsrc, adding it to the VAddr, then using
    // a zero-value SRsrc.
    //
    // If the instruction is _OFFSET (both idxen and offen disabled), and we
    // support ADDR64 instructions, we can convert to ADDR64 and do the same as
    // above.
    //
    // Otherwise we are on non-ADDR64 hardware, and/or we have
    // idxen/offen/bothen and we fall back to a waterfall loop.

    MachineOperand *Rsrc = &MI.getOperand(RsrcIdx);
    MachineBasicBlock &MBB = *MI.getParent();

    MachineOperand *VAddr = getNamedOperand(MI, AMDGPU::OpName::vaddr);
    if (VAddr && AMDGPU::getIfAddr64Inst(MI.getOpcode()) != -1) {
      // This is already an ADDR64 instruction so we need to add the pointer
      // extracted from the resource descriptor to the current value of VAddr.
      Register NewVAddrLo = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      Register NewVAddrHi = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      Register NewVAddr = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);

      const auto *BoolXExecRC = RI.getRegClass(AMDGPU::SReg_1_XEXECRegClassID);
      Register CondReg0 = MRI.createVirtualRegister(BoolXExecRC);
      Register CondReg1 = MRI.createVirtualRegister(BoolXExecRC);

      unsigned RsrcPtr, NewSRsrc;
      std::tie(RsrcPtr, NewSRsrc) = extractRsrcPtr(*this, MI, *Rsrc);

      // NewVaddrLo = RsrcPtr:sub0 + VAddr:sub0
      const DebugLoc &DL = MI.getDebugLoc();
      BuildMI(MBB, MI, DL, get(AMDGPU::V_ADD_CO_U32_e64), NewVAddrLo)
        .addDef(CondReg0)
        .addReg(RsrcPtr, 0, AMDGPU::sub0)
        .addReg(VAddr->getReg(), 0, AMDGPU::sub0)
        .addImm(0);

      // NewVaddrHi = RsrcPtr:sub1 + VAddr:sub1
      BuildMI(MBB, MI, DL, get(AMDGPU::V_ADDC_U32_e64), NewVAddrHi)
        .addDef(CondReg1, RegState::Dead)
        .addReg(RsrcPtr, 0, AMDGPU::sub1)
        .addReg(VAddr->getReg(), 0, AMDGPU::sub1)
        .addReg(CondReg0, RegState::Kill)
        .addImm(0);

      // NewVaddr = {NewVaddrHi, NewVaddrLo}
      BuildMI(MBB, MI, MI.getDebugLoc(), get(AMDGPU::REG_SEQUENCE), NewVAddr)
          .addReg(NewVAddrLo)
          .addImm(AMDGPU::sub0)
          .addReg(NewVAddrHi)
          .addImm(AMDGPU::sub1);

      VAddr->setReg(NewVAddr);
      Rsrc->setReg(NewSRsrc);
    } else if (!VAddr && ST.hasAddr64()) {
      // This instructions is the _OFFSET variant, so we need to convert it to
      // ADDR64.
      assert(ST.getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS &&
             "FIXME: Need to emit flat atomics here");

      unsigned RsrcPtr, NewSRsrc;
      std::tie(RsrcPtr, NewSRsrc) = extractRsrcPtr(*this, MI, *Rsrc);

      Register NewVAddr = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);
      MachineOperand *VData = getNamedOperand(MI, AMDGPU::OpName::vdata);
      MachineOperand *Offset = getNamedOperand(MI, AMDGPU::OpName::offset);
      MachineOperand *SOffset = getNamedOperand(MI, AMDGPU::OpName::soffset);
      unsigned Addr64Opcode = AMDGPU::getAddr64Inst(MI.getOpcode());

      // Atomics with return have an additional tied operand and are
      // missing some of the special bits.
      MachineOperand *VDataIn = getNamedOperand(MI, AMDGPU::OpName::vdata_in);
      MachineInstr *Addr64;

      if (!VDataIn) {
        // Regular buffer load / store.
        MachineInstrBuilder MIB =
            BuildMI(MBB, MI, MI.getDebugLoc(), get(Addr64Opcode))
                .add(*VData)
                .addReg(NewVAddr)
                .addReg(NewSRsrc)
                .add(*SOffset)
                .add(*Offset);

        if (const MachineOperand *CPol =
                getNamedOperand(MI, AMDGPU::OpName::cpol)) {
          MIB.addImm(CPol->getImm());
        }

        if (const MachineOperand *TFE =
                getNamedOperand(MI, AMDGPU::OpName::tfe)) {
          MIB.addImm(TFE->getImm());
        }

        MIB.addImm(getNamedImmOperand(MI, AMDGPU::OpName::swz));

        MIB.cloneMemRefs(MI);
        Addr64 = MIB;
      } else {
        // Atomics with return.
        Addr64 = BuildMI(MBB, MI, MI.getDebugLoc(), get(Addr64Opcode))
                     .add(*VData)
                     .add(*VDataIn)
                     .addReg(NewVAddr)
                     .addReg(NewSRsrc)
                     .add(*SOffset)
                     .add(*Offset)
                     .addImm(getNamedImmOperand(MI, AMDGPU::OpName::cpol))
                     .cloneMemRefs(MI);
      }

      MI.removeFromParent();

      // NewVaddr = {NewVaddrHi, NewVaddrLo}
      BuildMI(MBB, Addr64, Addr64->getDebugLoc(), get(AMDGPU::REG_SEQUENCE),
              NewVAddr)
          .addReg(RsrcPtr, 0, AMDGPU::sub0)
          .addImm(AMDGPU::sub0)
          .addReg(RsrcPtr, 0, AMDGPU::sub1)
          .addImm(AMDGPU::sub1);
    } else {
      // Legalize a VGPR Rsrc and soffset together.
      if (!isSoffsetLegal) {
        MachineOperand *Soffset = getNamedOperand(MI, AMDGPU::OpName::soffset);
        CreatedBB =
            loadMBUFScalarOperandsFromVGPR(*this, MI, {Rsrc, Soffset}, MDT);
        return CreatedBB;
      }
      CreatedBB = loadMBUFScalarOperandsFromVGPR(*this, MI, {Rsrc}, MDT);
      return CreatedBB;
    }
  }

  // Legalize a VGPR soffset.
  if (!isSoffsetLegal) {
    MachineOperand *Soffset = getNamedOperand(MI, AMDGPU::OpName::soffset);
    CreatedBB = loadMBUFScalarOperandsFromVGPR(*this, MI, {Soffset}, MDT);
    return CreatedBB;
  }
  return CreatedBB;
}

void SIInstrWorklist::insert(MachineInstr *MI) {
  InstrList.insert(MI);
  // Add MBUF instructiosn to deferred list.
  int RsrcIdx =
      AMDGPU::getNamedOperandIdx(MI->getOpcode(), AMDGPU::OpName::srsrc);
  if (RsrcIdx != -1) {
    DeferredList.insert(MI);
  }
}

bool SIInstrWorklist::isDeferred(MachineInstr *MI) {
  return DeferredList.contains(MI);
}

void SIInstrInfo::moveToVALU(SIInstrWorklist &Worklist,
                             MachineDominatorTree *MDT) const {

  while (!Worklist.empty()) {
    MachineInstr &Inst = *Worklist.top();
    Worklist.erase_top();
    // Skip MachineInstr in the deferred list.
    if (Worklist.isDeferred(&Inst))
      continue;
    moveToVALUImpl(Worklist, MDT, Inst);
  }

  // Deferred list of instructions will be processed once
  // all the MachineInstr in the worklist are done.
  for (MachineInstr *Inst : Worklist.getDeferredList()) {
    moveToVALUImpl(Worklist, MDT, *Inst);
    assert(Worklist.empty() &&
           "Deferred MachineInstr are not supposed to re-populate worklist");
  }
}

void SIInstrInfo::moveToVALUImpl(SIInstrWorklist &Worklist,
                                 MachineDominatorTree *MDT,
                                 MachineInstr &Inst) const {

  MachineBasicBlock *MBB = Inst.getParent();
  if (!MBB)
    return;
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  unsigned Opcode = Inst.getOpcode();
  unsigned NewOpcode = getVALUOp(Inst);
  // Handle some special cases
  switch (Opcode) {
  default:
    break;
  case AMDGPU::S_ADD_U64_PSEUDO:
    NewOpcode = AMDGPU::V_ADD_U64_PSEUDO;
    break;
  case AMDGPU::S_SUB_U64_PSEUDO:
    NewOpcode = AMDGPU::V_SUB_U64_PSEUDO;
    break;
  case AMDGPU::S_ADD_I32:
  case AMDGPU::S_SUB_I32: {
    // FIXME: The u32 versions currently selected use the carry.
    bool Changed;
    MachineBasicBlock *CreatedBBTmp = nullptr;
    std::tie(Changed, CreatedBBTmp) = moveScalarAddSub(Worklist, Inst, MDT);
    if (Changed)
      return;

    // Default handling
    break;
  }

  case AMDGPU::S_MUL_U64:
    // Split s_mul_u64 in 32-bit vector multiplications.
    splitScalarSMulU64(Worklist, Inst, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_MUL_U64_U32_PSEUDO:
  case AMDGPU::S_MUL_I64_I32_PSEUDO:
    // This is a special case of s_mul_u64 where all the operands are either
    // zero extended or sign extended.
    splitScalarSMulPseudo(Worklist, Inst, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_AND_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_AND_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_OR_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_OR_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_XOR_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_XOR_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_NAND_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_NAND_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_NOR_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_NOR_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_XNOR_B64:
    if (ST.hasDLInsts())
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_XNOR_B32, MDT);
    else
      splitScalar64BitXnor(Worklist, Inst, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_ANDN2_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_ANDN2_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_ORN2_B64:
    splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_ORN2_B32, MDT);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_BREV_B64:
    splitScalar64BitUnaryOp(Worklist, Inst, AMDGPU::S_BREV_B32, true);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_NOT_B64:
    splitScalar64BitUnaryOp(Worklist, Inst, AMDGPU::S_NOT_B32);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_BCNT1_I32_B64:
    splitScalar64BitBCNT(Worklist, Inst);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_BFE_I64:
    splitScalar64BitBFE(Worklist, Inst);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_FLBIT_I32_B64:
    splitScalar64BitCountOp(Worklist, Inst, AMDGPU::V_FFBH_U32_e32);
    Inst.eraseFromParent();
    return;
  case AMDGPU::S_FF1_I32_B64:
    splitScalar64BitCountOp(Worklist, Inst, AMDGPU::V_FFBL_B32_e32);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_LSHL_B32:
    if (ST.hasOnlyRevVALUShifts()) {
      NewOpcode = AMDGPU::V_LSHLREV_B32_e64;
      swapOperands(Inst);
    }
    break;
  case AMDGPU::S_ASHR_I32:
    if (ST.hasOnlyRevVALUShifts()) {
      NewOpcode = AMDGPU::V_ASHRREV_I32_e64;
      swapOperands(Inst);
    }
    break;
  case AMDGPU::S_LSHR_B32:
    if (ST.hasOnlyRevVALUShifts()) {
      NewOpcode = AMDGPU::V_LSHRREV_B32_e64;
      swapOperands(Inst);
    }
    break;
  case AMDGPU::S_LSHL_B64:
    if (ST.hasOnlyRevVALUShifts()) {
      NewOpcode = ST.getGeneration() >= AMDGPUSubtarget::GFX12
                      ? AMDGPU::V_LSHLREV_B64_pseudo_e64
                      : AMDGPU::V_LSHLREV_B64_e64;
      swapOperands(Inst);
    }
    break;
  case AMDGPU::S_ASHR_I64:
    if (ST.hasOnlyRevVALUShifts()) {
      NewOpcode = AMDGPU::V_ASHRREV_I64_e64;
      swapOperands(Inst);
    }
    break;
  case AMDGPU::S_LSHR_B64:
    if (ST.hasOnlyRevVALUShifts()) {
      NewOpcode = AMDGPU::V_LSHRREV_B64_e64;
      swapOperands(Inst);
    }
    break;

  case AMDGPU::S_ABS_I32:
    lowerScalarAbs(Worklist, Inst);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_CBRANCH_SCC0:
  case AMDGPU::S_CBRANCH_SCC1: {
    // Clear unused bits of vcc
    Register CondReg = Inst.getOperand(1).getReg();
    bool IsSCC = CondReg == AMDGPU::SCC;
    Register VCC = RI.getVCC();
    Register EXEC = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
    unsigned Opc = ST.isWave32() ? AMDGPU::S_AND_B32 : AMDGPU::S_AND_B64;
    BuildMI(*MBB, Inst, Inst.getDebugLoc(), get(Opc), VCC)
        .addReg(EXEC)
        .addReg(IsSCC ? VCC : CondReg);
    Inst.removeOperand(1);
  } break;

  case AMDGPU::S_BFE_U64:
  case AMDGPU::S_BFM_B64:
    llvm_unreachable("Moving this op to VALU not implemented");

  case AMDGPU::S_PACK_LL_B32_B16:
  case AMDGPU::S_PACK_LH_B32_B16:
  case AMDGPU::S_PACK_HL_B32_B16:
  case AMDGPU::S_PACK_HH_B32_B16:
    movePackToVALU(Worklist, MRI, Inst);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_XNOR_B32:
    lowerScalarXnor(Worklist, Inst);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_NAND_B32:
    splitScalarNotBinop(Worklist, Inst, AMDGPU::S_AND_B32);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_NOR_B32:
    splitScalarNotBinop(Worklist, Inst, AMDGPU::S_OR_B32);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_ANDN2_B32:
    splitScalarBinOpN2(Worklist, Inst, AMDGPU::S_AND_B32);
    Inst.eraseFromParent();
    return;

  case AMDGPU::S_ORN2_B32:
    splitScalarBinOpN2(Worklist, Inst, AMDGPU::S_OR_B32);
    Inst.eraseFromParent();
    return;

  // TODO: remove as soon as everything is ready
  // to replace VGPR to SGPR copy with V_READFIRSTLANEs.
  // S_ADD/SUB_CO_PSEUDO as well as S_UADDO/USUBO_PSEUDO
  // can only be selected from the uniform SDNode.
  case AMDGPU::S_ADD_CO_PSEUDO:
  case AMDGPU::S_SUB_CO_PSEUDO: {
    unsigned Opc = (Inst.getOpcode() == AMDGPU::S_ADD_CO_PSEUDO)
                       ? AMDGPU::V_ADDC_U32_e64
                       : AMDGPU::V_SUBB_U32_e64;
    const auto *CarryRC = RI.getRegClass(AMDGPU::SReg_1_XEXECRegClassID);

    Register CarryInReg = Inst.getOperand(4).getReg();
    if (!MRI.constrainRegClass(CarryInReg, CarryRC)) {
      Register NewCarryReg = MRI.createVirtualRegister(CarryRC);
      BuildMI(*MBB, Inst, Inst.getDebugLoc(), get(AMDGPU::COPY), NewCarryReg)
          .addReg(CarryInReg);
    }

    Register CarryOutReg = Inst.getOperand(1).getReg();

    Register DestReg = MRI.createVirtualRegister(RI.getEquivalentVGPRClass(
        MRI.getRegClass(Inst.getOperand(0).getReg())));
    MachineInstr *CarryOp =
        BuildMI(*MBB, &Inst, Inst.getDebugLoc(), get(Opc), DestReg)
            .addReg(CarryOutReg, RegState::Define)
            .add(Inst.getOperand(2))
            .add(Inst.getOperand(3))
            .addReg(CarryInReg)
            .addImm(0);
    legalizeOperands(*CarryOp);
    MRI.replaceRegWith(Inst.getOperand(0).getReg(), DestReg);
    addUsersToMoveToVALUWorklist(DestReg, MRI, Worklist);
    Inst.eraseFromParent();
  }
    return;
  case AMDGPU::S_UADDO_PSEUDO:
  case AMDGPU::S_USUBO_PSEUDO: {
    const DebugLoc &DL = Inst.getDebugLoc();
    MachineOperand &Dest0 = Inst.getOperand(0);
    MachineOperand &Dest1 = Inst.getOperand(1);
    MachineOperand &Src0 = Inst.getOperand(2);
    MachineOperand &Src1 = Inst.getOperand(3);

    unsigned Opc = (Inst.getOpcode() == AMDGPU::S_UADDO_PSEUDO)
                       ? AMDGPU::V_ADD_CO_U32_e64
                       : AMDGPU::V_SUB_CO_U32_e64;
    const TargetRegisterClass *NewRC =
        RI.getEquivalentVGPRClass(MRI.getRegClass(Dest0.getReg()));
    Register DestReg = MRI.createVirtualRegister(NewRC);
    MachineInstr *NewInstr = BuildMI(*MBB, &Inst, DL, get(Opc), DestReg)
                                 .addReg(Dest1.getReg(), RegState::Define)
                                 .add(Src0)
                                 .add(Src1)
                                 .addImm(0); // clamp bit

    legalizeOperands(*NewInstr, MDT);
    MRI.replaceRegWith(Dest0.getReg(), DestReg);
    addUsersToMoveToVALUWorklist(NewInstr->getOperand(0).getReg(), MRI,
                                 Worklist);
    Inst.eraseFromParent();
  }
    return;

  case AMDGPU::S_CSELECT_B32:
  case AMDGPU::S_CSELECT_B64:
    lowerSelect(Worklist, Inst, MDT);
    Inst.eraseFromParent();
    return;
  case AMDGPU::S_CMP_EQ_I32:
  case AMDGPU::S_CMP_LG_I32:
  case AMDGPU::S_CMP_GT_I32:
  case AMDGPU::S_CMP_GE_I32:
  case AMDGPU::S_CMP_LT_I32:
  case AMDGPU::S_CMP_LE_I32:
  case AMDGPU::S_CMP_EQ_U32:
  case AMDGPU::S_CMP_LG_U32:
  case AMDGPU::S_CMP_GT_U32:
  case AMDGPU::S_CMP_GE_U32:
  case AMDGPU::S_CMP_LT_U32:
  case AMDGPU::S_CMP_LE_U32:
  case AMDGPU::S_CMP_EQ_U64:
  case AMDGPU::S_CMP_LG_U64:
  case AMDGPU::S_CMP_LT_F32:
  case AMDGPU::S_CMP_EQ_F32:
  case AMDGPU::S_CMP_LE_F32:
  case AMDGPU::S_CMP_GT_F32:
  case AMDGPU::S_CMP_LG_F32:
  case AMDGPU::S_CMP_GE_F32:
  case AMDGPU::S_CMP_O_F32:
  case AMDGPU::S_CMP_U_F32:
  case AMDGPU::S_CMP_NGE_F32:
  case AMDGPU::S_CMP_NLG_F32:
  case AMDGPU::S_CMP_NGT_F32:
  case AMDGPU::S_CMP_NLE_F32:
  case AMDGPU::S_CMP_NEQ_F32:
  case AMDGPU::S_CMP_NLT_F32:
  case AMDGPU::S_CMP_LT_F16:
  case AMDGPU::S_CMP_EQ_F16:
  case AMDGPU::S_CMP_LE_F16:
  case AMDGPU::S_CMP_GT_F16:
  case AMDGPU::S_CMP_LG_F16:
  case AMDGPU::S_CMP_GE_F16:
  case AMDGPU::S_CMP_O_F16:
  case AMDGPU::S_CMP_U_F16:
  case AMDGPU::S_CMP_NGE_F16:
  case AMDGPU::S_CMP_NLG_F16:
  case AMDGPU::S_CMP_NGT_F16:
  case AMDGPU::S_CMP_NLE_F16:
  case AMDGPU::S_CMP_NEQ_F16:
  case AMDGPU::S_CMP_NLT_F16: {
    Register CondReg = MRI.createVirtualRegister(RI.getWaveMaskRegClass());
    auto NewInstr =
        BuildMI(*MBB, Inst, Inst.getDebugLoc(), get(NewOpcode), CondReg)
        .setMIFlags(Inst.getFlags());
    if (AMDGPU::getNamedOperandIdx(NewOpcode,
                                   AMDGPU::OpName::src0_modifiers) >= 0) {
      NewInstr
          .addImm(0)               // src0_modifiers
          .add(Inst.getOperand(0)) // src0
          .addImm(0)               // src1_modifiers
          .add(Inst.getOperand(1)) // src1
          .addImm(0);              // clamp
    } else {
      NewInstr
          .add(Inst.getOperand(0))
          .add(Inst.getOperand(1));
    }
    legalizeOperands(*NewInstr, MDT);
    int SCCIdx = Inst.findRegisterDefOperandIdx(AMDGPU::SCC, /*TRI=*/nullptr);
    MachineOperand SCCOp = Inst.getOperand(SCCIdx);
    addSCCDefUsersToVALUWorklist(SCCOp, Inst, Worklist, CondReg);
    Inst.eraseFromParent();
    return;
  }
  case AMDGPU::S_CVT_HI_F32_F16: {
    const DebugLoc &DL = Inst.getDebugLoc();
    Register TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register NewDst = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHRREV_B32_e64), TmpReg)
        .addImm(16)
        .add(Inst.getOperand(1));
    BuildMI(*MBB, Inst, DL, get(NewOpcode), NewDst)
        .addImm(0) // src0_modifiers
        .addReg(TmpReg)
        .addImm(0)  // clamp
        .addImm(0); // omod

    MRI.replaceRegWith(Inst.getOperand(0).getReg(), NewDst);
    addUsersToMoveToVALUWorklist(NewDst, MRI, Worklist);
    Inst.eraseFromParent();
    return;
  }
  case AMDGPU::S_MINIMUM_F32:
  case AMDGPU::S_MAXIMUM_F32:
  case AMDGPU::S_MINIMUM_F16:
  case AMDGPU::S_MAXIMUM_F16: {
    const DebugLoc &DL = Inst.getDebugLoc();
    Register NewDst = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    MachineInstr *NewInstr = BuildMI(*MBB, Inst, DL, get(NewOpcode), NewDst)
                                 .addImm(0) // src0_modifiers
                                 .add(Inst.getOperand(1))
                                 .addImm(0) // src1_modifiers
                                 .add(Inst.getOperand(2))
                                 .addImm(0)  // clamp
                                 .addImm(0); // omod
    MRI.replaceRegWith(Inst.getOperand(0).getReg(), NewDst);

    legalizeOperands(*NewInstr, MDT);
    addUsersToMoveToVALUWorklist(NewDst, MRI, Worklist);
    Inst.eraseFromParent();
    return;
  }
  }

  if (NewOpcode == AMDGPU::INSTRUCTION_LIST_END) {
    // We cannot move this instruction to the VALU, so we should try to
    // legalize its operands instead.
    legalizeOperands(Inst, MDT);
    return;
  }
  // Handle converting generic instructions like COPY-to-SGPR into
  // COPY-to-VGPR.
  if (NewOpcode == Opcode) {
    Register DstReg = Inst.getOperand(0).getReg();
    const TargetRegisterClass *NewDstRC = getDestEquivalentVGPRClass(Inst);

    // If it's a copy of a VGPR to a physical SGPR, insert a V_READFIRSTLANE and
    // hope for the best.
    if (Inst.isCopy() && DstReg.isPhysical() &&
        RI.isVGPR(MRI, Inst.getOperand(1).getReg())) {
      // TODO: Only works for 32 bit registers.
      BuildMI(*Inst.getParent(), &Inst, Inst.getDebugLoc(),
              get(AMDGPU::V_READFIRSTLANE_B32), Inst.getOperand(0).getReg())
          .add(Inst.getOperand(1));
      Inst.eraseFromParent();
      return;
    }

    if (Inst.isCopy() && Inst.getOperand(1).getReg().isVirtual() &&
        NewDstRC == RI.getRegClassForReg(MRI, Inst.getOperand(1).getReg())) {
      // Instead of creating a copy where src and dst are the same register
      // class, we just replace all uses of dst with src.  These kinds of
      // copies interfere with the heuristics MachineSink uses to decide
      // whether or not to split a critical edge.  Since the pass assumes
      // that copies will end up as machine instructions and not be
      // eliminated.
      addUsersToMoveToVALUWorklist(DstReg, MRI, Worklist);
      MRI.replaceRegWith(DstReg, Inst.getOperand(1).getReg());
      MRI.clearKillFlags(Inst.getOperand(1).getReg());
      Inst.getOperand(0).setReg(DstReg);
      // Make sure we don't leave around a dead VGPR->SGPR copy. Normally
      // these are deleted later, but at -O0 it would leave a suspicious
      // looking illegal copy of an undef register.
      for (unsigned I = Inst.getNumOperands() - 1; I != 0; --I)
        Inst.removeOperand(I);
      Inst.setDesc(get(AMDGPU::IMPLICIT_DEF));
      return;
    }
    Register NewDstReg = MRI.createVirtualRegister(NewDstRC);
    MRI.replaceRegWith(DstReg, NewDstReg);
    legalizeOperands(Inst, MDT);
    addUsersToMoveToVALUWorklist(NewDstReg, MRI, Worklist);
    return;
  }

  // Use the new VALU Opcode.
  auto NewInstr = BuildMI(*MBB, Inst, Inst.getDebugLoc(), get(NewOpcode))
                      .setMIFlags(Inst.getFlags());
  if (isVOP3(NewOpcode) && !isVOP3(Opcode)) {
    // Intersperse VOP3 modifiers among the SALU operands.
    NewInstr->addOperand(Inst.getOperand(0));
    if (AMDGPU::getNamedOperandIdx(NewOpcode,
                                   AMDGPU::OpName::src0_modifiers) >= 0)
      NewInstr.addImm(0);
    if (AMDGPU::hasNamedOperand(NewOpcode, AMDGPU::OpName::src0)) {
      MachineOperand Src = Inst.getOperand(1);
      if (AMDGPU::isTrue16Inst(NewOpcode) && ST.useRealTrue16Insts() &&
          Src.isReg() && RI.isVGPR(MRI, Src.getReg()))
        NewInstr.addReg(Src.getReg(), 0, AMDGPU::lo16);
      else
        NewInstr->addOperand(Src);
    }

    if (Opcode == AMDGPU::S_SEXT_I32_I8 || Opcode == AMDGPU::S_SEXT_I32_I16) {
      // We are converting these to a BFE, so we need to add the missing
      // operands for the size and offset.
      unsigned Size = (Opcode == AMDGPU::S_SEXT_I32_I8) ? 8 : 16;
      NewInstr.addImm(0);
      NewInstr.addImm(Size);
    } else if (Opcode == AMDGPU::S_BCNT1_I32_B32) {
      // The VALU version adds the second operand to the result, so insert an
      // extra 0 operand.
      NewInstr.addImm(0);
    } else if (Opcode == AMDGPU::S_BFE_I32 || Opcode == AMDGPU::S_BFE_U32) {
      const MachineOperand &OffsetWidthOp = Inst.getOperand(2);
      // If we need to move this to VGPRs, we need to unpack the second
      // operand back into the 2 separate ones for bit offset and width.
      assert(OffsetWidthOp.isImm() &&
             "Scalar BFE is only implemented for constant width and offset");
      uint32_t Imm = OffsetWidthOp.getImm();

      uint32_t Offset = Imm & 0x3f;               // Extract bits [5:0].
      uint32_t BitWidth = (Imm & 0x7f0000) >> 16; // Extract bits [22:16].
      NewInstr.addImm(Offset);
      NewInstr.addImm(BitWidth);
    } else {
      if (AMDGPU::getNamedOperandIdx(NewOpcode,
                                     AMDGPU::OpName::src1_modifiers) >= 0)
        NewInstr.addImm(0);
      if (AMDGPU::getNamedOperandIdx(NewOpcode, AMDGPU::OpName::src1) >= 0)
        NewInstr->addOperand(Inst.getOperand(2));
      if (AMDGPU::getNamedOperandIdx(NewOpcode,
                                     AMDGPU::OpName::src2_modifiers) >= 0)
        NewInstr.addImm(0);
      if (AMDGPU::getNamedOperandIdx(NewOpcode, AMDGPU::OpName::src2) >= 0)
        NewInstr->addOperand(Inst.getOperand(3));
      if (AMDGPU::getNamedOperandIdx(NewOpcode, AMDGPU::OpName::clamp) >= 0)
        NewInstr.addImm(0);
      if (AMDGPU::getNamedOperandIdx(NewOpcode, AMDGPU::OpName::omod) >= 0)
        NewInstr.addImm(0);
      if (AMDGPU::getNamedOperandIdx(NewOpcode, AMDGPU::OpName::op_sel) >= 0)
        NewInstr.addImm(0);
    }
  } else {
    // Just copy the SALU operands.
    for (const MachineOperand &Op : Inst.explicit_operands())
      NewInstr->addOperand(Op);
  }

  // Remove any references to SCC. Vector instructions can't read from it, and
  // We're just about to add the implicit use / defs of VCC, and we don't want
  // both.
  for (MachineOperand &Op : Inst.implicit_operands()) {
    if (Op.getReg() == AMDGPU::SCC) {
      // Only propagate through live-def of SCC.
      if (Op.isDef() && !Op.isDead())
        addSCCDefUsersToVALUWorklist(Op, Inst, Worklist);
      if (Op.isUse())
        addSCCDefsToVALUWorklist(NewInstr, Worklist);
    }
  }
  Inst.eraseFromParent();
  Register NewDstReg;
  if (NewInstr->getOperand(0).isReg() && NewInstr->getOperand(0).isDef()) {
    Register DstReg = NewInstr->getOperand(0).getReg();
    assert(DstReg.isVirtual());
    // Update the destination register class.
    const TargetRegisterClass *NewDstRC = getDestEquivalentVGPRClass(*NewInstr);
    assert(NewDstRC);
    NewDstReg = MRI.createVirtualRegister(NewDstRC);
    MRI.replaceRegWith(DstReg, NewDstReg);
  }
  fixImplicitOperands(*NewInstr);
  // Legalize the operands
  legalizeOperands(*NewInstr, MDT);
  if (NewDstReg)
    addUsersToMoveToVALUWorklist(NewDstReg, MRI, Worklist);
}

// Add/sub require special handling to deal with carry outs.
std::pair<bool, MachineBasicBlock *>
SIInstrInfo::moveScalarAddSub(SIInstrWorklist &Worklist, MachineInstr &Inst,
                              MachineDominatorTree *MDT) const {
  if (ST.hasAddNoCarry()) {
    // Assume there is no user of scc since we don't select this in that case.
    // Since scc isn't used, it doesn't really matter if the i32 or u32 variant
    // is used.

    MachineBasicBlock &MBB = *Inst.getParent();
    MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

    Register OldDstReg = Inst.getOperand(0).getReg();
    Register ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

    unsigned Opc = Inst.getOpcode();
    assert(Opc == AMDGPU::S_ADD_I32 || Opc == AMDGPU::S_SUB_I32);

    unsigned NewOpc = Opc == AMDGPU::S_ADD_I32 ?
      AMDGPU::V_ADD_U32_e64 : AMDGPU::V_SUB_U32_e64;

    assert(Inst.getOperand(3).getReg() == AMDGPU::SCC);
    Inst.removeOperand(3);

    Inst.setDesc(get(NewOpc));
    Inst.addOperand(MachineOperand::CreateImm(0)); // clamp bit
    Inst.addImplicitDefUseOperands(*MBB.getParent());
    MRI.replaceRegWith(OldDstReg, ResultReg);
    MachineBasicBlock *NewBB = legalizeOperands(Inst, MDT);

    addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
    return std::pair(true, NewBB);
  }

  return std::pair(false, nullptr);
}

void SIInstrInfo::lowerSelect(SIInstrWorklist &Worklist, MachineInstr &Inst,
                              MachineDominatorTree *MDT) const {

  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  DebugLoc DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  MachineOperand &Cond = Inst.getOperand(3);

  Register CondReg = Cond.getReg();
  bool IsSCC = (CondReg == AMDGPU::SCC);

  // If this is a trivial select where the condition is effectively not SCC
  // (CondReg is a source of copy to SCC), then the select is semantically
  // equivalent to copying CondReg. Hence, there is no need to create
  // V_CNDMASK, we can just use that and bail out.
  if (!IsSCC && Src0.isImm() && (Src0.getImm() == -1) && Src1.isImm() &&
      (Src1.getImm() == 0)) {
    MRI.replaceRegWith(Dest.getReg(), CondReg);
    return;
  }

  Register NewCondReg = CondReg;
  if (IsSCC) {
    const TargetRegisterClass *TC =
        RI.getRegClass(AMDGPU::SReg_1_XEXECRegClassID);
    NewCondReg = MRI.createVirtualRegister(TC);

    // Now look for the closest SCC def if it is a copy
    // replacing the CondReg with the COPY source register
    bool CopyFound = false;
    for (MachineInstr &CandI :
         make_range(std::next(MachineBasicBlock::reverse_iterator(Inst)),
                    Inst.getParent()->rend())) {
      if (CandI.findRegisterDefOperandIdx(AMDGPU::SCC, &RI, false, false) !=
          -1) {
        if (CandI.isCopy() && CandI.getOperand(0).getReg() == AMDGPU::SCC) {
          BuildMI(MBB, MII, DL, get(AMDGPU::COPY), NewCondReg)
              .addReg(CandI.getOperand(1).getReg());
          CopyFound = true;
        }
        break;
      }
    }
    if (!CopyFound) {
      // SCC def is not a copy
      // Insert a trivial select instead of creating a copy, because a copy from
      // SCC would semantically mean just copying a single bit, but we may need
      // the result to be a vector condition mask that needs preserving.
      unsigned Opcode = (ST.getWavefrontSize() == 64) ? AMDGPU::S_CSELECT_B64
                                                      : AMDGPU::S_CSELECT_B32;
      auto NewSelect =
          BuildMI(MBB, MII, DL, get(Opcode), NewCondReg).addImm(-1).addImm(0);
      NewSelect->getOperand(3).setIsUndef(Cond.isUndef());
    }
  }

  Register NewDestReg = MRI.createVirtualRegister(
      RI.getEquivalentVGPRClass(MRI.getRegClass(Dest.getReg())));
  MachineInstr *NewInst;
  if (Inst.getOpcode() == AMDGPU::S_CSELECT_B32) {
    NewInst = BuildMI(MBB, MII, DL, get(AMDGPU::V_CNDMASK_B32_e64), NewDestReg)
                  .addImm(0)
                  .add(Src1) // False
                  .addImm(0)
                  .add(Src0) // True
                  .addReg(NewCondReg);
  } else {
    NewInst =
        BuildMI(MBB, MII, DL, get(AMDGPU::V_CNDMASK_B64_PSEUDO), NewDestReg)
            .add(Src1) // False
            .add(Src0) // True
            .addReg(NewCondReg);
  }
  MRI.replaceRegWith(Dest.getReg(), NewDestReg);
  legalizeOperands(*NewInst, MDT);
  addUsersToMoveToVALUWorklist(NewDestReg, MRI, Worklist);
}

void SIInstrInfo::lowerScalarAbs(SIInstrWorklist &Worklist,
                                 MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  DebugLoc DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src = Inst.getOperand(1);
  Register TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  unsigned SubOp = ST.hasAddNoCarry() ?
    AMDGPU::V_SUB_U32_e32 : AMDGPU::V_SUB_CO_U32_e32;

  BuildMI(MBB, MII, DL, get(SubOp), TmpReg)
    .addImm(0)
    .addReg(Src.getReg());

  BuildMI(MBB, MII, DL, get(AMDGPU::V_MAX_I32_e64), ResultReg)
    .addReg(Src.getReg())
    .addReg(TmpReg);

  MRI.replaceRegWith(Dest.getReg(), ResultReg);
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::lowerScalarXnor(SIInstrWorklist &Worklist,
                                  MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);

  if (ST.hasDLInsts()) {
    Register NewDest = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    legalizeGenericOperand(MBB, MII, &AMDGPU::VGPR_32RegClass, Src0, MRI, DL);
    legalizeGenericOperand(MBB, MII, &AMDGPU::VGPR_32RegClass, Src1, MRI, DL);

    BuildMI(MBB, MII, DL, get(AMDGPU::V_XNOR_B32_e64), NewDest)
      .add(Src0)
      .add(Src1);

    MRI.replaceRegWith(Dest.getReg(), NewDest);
    addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
  } else {
    // Using the identity !(x ^ y) == (!x ^ y) == (x ^ !y), we can
    // invert either source and then perform the XOR. If either source is a
    // scalar register, then we can leave the inversion on the scalar unit to
    // achieve a better distribution of scalar and vector instructions.
    bool Src0IsSGPR = Src0.isReg() &&
                      RI.isSGPRClass(MRI.getRegClass(Src0.getReg()));
    bool Src1IsSGPR = Src1.isReg() &&
                      RI.isSGPRClass(MRI.getRegClass(Src1.getReg()));
    MachineInstr *Xor;
    Register Temp = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    Register NewDest = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);

    // Build a pair of scalar instructions and add them to the work list.
    // The next iteration over the work list will lower these to the vector
    // unit as necessary.
    if (Src0IsSGPR) {
      BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), Temp).add(Src0);
      Xor = BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B32), NewDest)
      .addReg(Temp)
      .add(Src1);
    } else if (Src1IsSGPR) {
      BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), Temp).add(Src1);
      Xor = BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B32), NewDest)
      .add(Src0)
      .addReg(Temp);
    } else {
      Xor = BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B32), Temp)
        .add(Src0)
        .add(Src1);
      MachineInstr *Not =
          BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), NewDest).addReg(Temp);
      Worklist.insert(Not);
    }

    MRI.replaceRegWith(Dest.getReg(), NewDest);

    Worklist.insert(Xor);

    addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
  }
}

void SIInstrInfo::splitScalarNotBinop(SIInstrWorklist &Worklist,
                                      MachineInstr &Inst,
                                      unsigned Opcode) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);

  Register NewDest = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
  Register Interm = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);

  MachineInstr &Op = *BuildMI(MBB, MII, DL, get(Opcode), Interm)
    .add(Src0)
    .add(Src1);

  MachineInstr &Not = *BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), NewDest)
    .addReg(Interm);

  Worklist.insert(&Op);
  Worklist.insert(&Not);

  MRI.replaceRegWith(Dest.getReg(), NewDest);
  addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
}

void SIInstrInfo::splitScalarBinOpN2(SIInstrWorklist &Worklist,
                                     MachineInstr &Inst,
                                     unsigned Opcode) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);

  Register NewDest = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
  Register Interm = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  MachineInstr &Not = *BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), Interm)
    .add(Src1);

  MachineInstr &Op = *BuildMI(MBB, MII, DL, get(Opcode), NewDest)
    .add(Src0)
    .addReg(Interm);

  Worklist.insert(&Not);
  Worklist.insert(&Op);

  MRI.replaceRegWith(Dest.getReg(), NewDest);
  addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitUnaryOp(SIInstrWorklist &Worklist,
                                          MachineInstr &Inst, unsigned Opcode,
                                          bool Swap) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  DebugLoc DL = Inst.getDebugLoc();

  MachineBasicBlock::iterator MII = Inst;

  const MCInstrDesc &InstDesc = get(Opcode);
  const TargetRegisterClass *Src0RC = Src0.isReg() ?
    MRI.getRegClass(Src0.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  const TargetRegisterClass *Src0SubRC =
      RI.getSubRegisterClass(Src0RC, AMDGPU::sub0);

  MachineOperand SrcReg0Sub0 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub0, Src0SubRC);

  const TargetRegisterClass *DestRC = MRI.getRegClass(Dest.getReg());
  const TargetRegisterClass *NewDestRC = RI.getEquivalentVGPRClass(DestRC);
  const TargetRegisterClass *NewDestSubRC =
      RI.getSubRegisterClass(NewDestRC, AMDGPU::sub0);

  Register DestSub0 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &LoHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub0).add(SrcReg0Sub0);

  MachineOperand SrcReg0Sub1 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub1, Src0SubRC);

  Register DestSub1 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &HiHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub1).add(SrcReg0Sub1);

  if (Swap)
    std::swap(DestSub0, DestSub1);

  Register FullDestReg = MRI.createVirtualRegister(NewDestRC);
  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
    .addReg(DestSub0)
    .addImm(AMDGPU::sub0)
    .addReg(DestSub1)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  Worklist.insert(&LoHalf);
  Worklist.insert(&HiHalf);

  // We don't need to legalizeOperands here because for a single operand, src0
  // will support any kind of input.

  // Move all users of this moved value.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

// There is not a vector equivalent of s_mul_u64. For this reason, we need to
// split the s_mul_u64 in 32-bit vector multiplications.
void SIInstrInfo::splitScalarSMulU64(SIInstrWorklist &Worklist,
                                     MachineInstr &Inst,
                                     MachineDominatorTree *MDT) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  Register FullDestReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);
  Register DestSub0 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register DestSub1 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();
  MachineBasicBlock::iterator MII = Inst;

  const TargetRegisterClass *Src0RC = MRI.getRegClass(Src0.getReg());
  const TargetRegisterClass *Src1RC = MRI.getRegClass(Src1.getReg());
  const TargetRegisterClass *Src0SubRC =
      RI.getSubRegisterClass(Src0RC, AMDGPU::sub0);
  if (RI.isSGPRClass(Src0SubRC))
    Src0SubRC = RI.getEquivalentVGPRClass(Src0SubRC);
  const TargetRegisterClass *Src1SubRC =
      RI.getSubRegisterClass(Src1RC, AMDGPU::sub0);
  if (RI.isSGPRClass(Src1SubRC))
    Src1SubRC = RI.getEquivalentVGPRClass(Src1SubRC);

  // First, we extract the low 32-bit and high 32-bit values from each of the
  // operands.
  MachineOperand Op0L =
      buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC, AMDGPU::sub0, Src0SubRC);
  MachineOperand Op1L =
      buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC, AMDGPU::sub0, Src1SubRC);
  MachineOperand Op0H =
      buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC, AMDGPU::sub1, Src0SubRC);
  MachineOperand Op1H =
      buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC, AMDGPU::sub1, Src1SubRC);

  // The multilication is done as follows:
  //
  //                            Op1H  Op1L
  //                          * Op0H  Op0L
  //                       --------------------
  //                       Op1H*Op0L  Op1L*Op0L
  //          + Op1H*Op0H  Op1L*Op0H
  // -----------------------------------------
  // (Op1H*Op0L + Op1L*Op0H + carry)  Op1L*Op0L
  //
  //  We drop Op1H*Op0H because the result of the multiplication is a 64-bit
  //  value and that would overflow.
  //  The low 32-bit value is Op1L*Op0L.
  //  The high 32-bit value is Op1H*Op0L + Op1L*Op0H + carry (from Op1L*Op0L).

  Register Op1L_Op0H_Reg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineInstr *Op1L_Op0H =
      BuildMI(MBB, MII, DL, get(AMDGPU::V_MUL_LO_U32_e64), Op1L_Op0H_Reg)
          .add(Op1L)
          .add(Op0H);

  Register Op1H_Op0L_Reg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineInstr *Op1H_Op0L =
      BuildMI(MBB, MII, DL, get(AMDGPU::V_MUL_LO_U32_e64), Op1H_Op0L_Reg)
          .add(Op1H)
          .add(Op0L);

  Register CarryReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineInstr *Carry =
      BuildMI(MBB, MII, DL, get(AMDGPU::V_MUL_HI_U32_e64), CarryReg)
          .add(Op1L)
          .add(Op0L);

  MachineInstr *LoHalf =
      BuildMI(MBB, MII, DL, get(AMDGPU::V_MUL_LO_U32_e64), DestSub0)
          .add(Op1L)
          .add(Op0L);

  Register AddReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineInstr *Add = BuildMI(MBB, MII, DL, get(AMDGPU::V_ADD_U32_e32), AddReg)
                          .addReg(Op1L_Op0H_Reg)
                          .addReg(Op1H_Op0L_Reg);

  MachineInstr *HiHalf =
      BuildMI(MBB, MII, DL, get(AMDGPU::V_ADD_U32_e32), DestSub1)
          .addReg(AddReg)
          .addReg(CarryReg);

  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
      .addReg(DestSub0)
      .addImm(AMDGPU::sub0)
      .addReg(DestSub1)
      .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  // Try to legalize the operands in case we need to swap the order to keep it
  // valid.
  legalizeOperands(*Op1L_Op0H, MDT);
  legalizeOperands(*Op1H_Op0L, MDT);
  legalizeOperands(*Carry, MDT);
  legalizeOperands(*LoHalf, MDT);
  legalizeOperands(*Add, MDT);
  legalizeOperands(*HiHalf, MDT);

  // Move all users of this moved value.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

// Lower S_MUL_U64_U32_PSEUDO/S_MUL_I64_I32_PSEUDO in two 32-bit vector
// multiplications.
void SIInstrInfo::splitScalarSMulPseudo(SIInstrWorklist &Worklist,
                                        MachineInstr &Inst,
                                        MachineDominatorTree *MDT) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  Register FullDestReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);
  Register DestSub0 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register DestSub1 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();
  MachineBasicBlock::iterator MII = Inst;

  const TargetRegisterClass *Src0RC = MRI.getRegClass(Src0.getReg());
  const TargetRegisterClass *Src1RC = MRI.getRegClass(Src1.getReg());
  const TargetRegisterClass *Src0SubRC =
      RI.getSubRegisterClass(Src0RC, AMDGPU::sub0);
  if (RI.isSGPRClass(Src0SubRC))
    Src0SubRC = RI.getEquivalentVGPRClass(Src0SubRC);
  const TargetRegisterClass *Src1SubRC =
      RI.getSubRegisterClass(Src1RC, AMDGPU::sub0);
  if (RI.isSGPRClass(Src1SubRC))
    Src1SubRC = RI.getEquivalentVGPRClass(Src1SubRC);

  // First, we extract the low 32-bit and high 32-bit values from each of the
  // operands.
  MachineOperand Op0L =
      buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC, AMDGPU::sub0, Src0SubRC);
  MachineOperand Op1L =
      buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC, AMDGPU::sub0, Src1SubRC);

  unsigned Opc = Inst.getOpcode();
  unsigned NewOpc = Opc == AMDGPU::S_MUL_U64_U32_PSEUDO
                        ? AMDGPU::V_MUL_HI_U32_e64
                        : AMDGPU::V_MUL_HI_I32_e64;
  MachineInstr *HiHalf =
      BuildMI(MBB, MII, DL, get(NewOpc), DestSub1).add(Op1L).add(Op0L);

  MachineInstr *LoHalf =
      BuildMI(MBB, MII, DL, get(AMDGPU::V_MUL_LO_U32_e64), DestSub0)
          .add(Op1L)
          .add(Op0L);

  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
      .addReg(DestSub0)
      .addImm(AMDGPU::sub0)
      .addReg(DestSub1)
      .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  // Try to legalize the operands in case we need to swap the order to keep it
  // valid.
  legalizeOperands(*HiHalf, MDT);
  legalizeOperands(*LoHalf, MDT);

  // Move all users of this moved value.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitBinaryOp(SIInstrWorklist &Worklist,
                                           MachineInstr &Inst, unsigned Opcode,
                                           MachineDominatorTree *MDT) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  DebugLoc DL = Inst.getDebugLoc();

  MachineBasicBlock::iterator MII = Inst;

  const MCInstrDesc &InstDesc = get(Opcode);
  const TargetRegisterClass *Src0RC = Src0.isReg() ?
    MRI.getRegClass(Src0.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  const TargetRegisterClass *Src0SubRC =
      RI.getSubRegisterClass(Src0RC, AMDGPU::sub0);
  const TargetRegisterClass *Src1RC = Src1.isReg() ?
    MRI.getRegClass(Src1.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  const TargetRegisterClass *Src1SubRC =
      RI.getSubRegisterClass(Src1RC, AMDGPU::sub0);

  MachineOperand SrcReg0Sub0 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub0, Src0SubRC);
  MachineOperand SrcReg1Sub0 = buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC,
                                                       AMDGPU::sub0, Src1SubRC);
  MachineOperand SrcReg0Sub1 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub1, Src0SubRC);
  MachineOperand SrcReg1Sub1 = buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC,
                                                       AMDGPU::sub1, Src1SubRC);

  const TargetRegisterClass *DestRC = MRI.getRegClass(Dest.getReg());
  const TargetRegisterClass *NewDestRC = RI.getEquivalentVGPRClass(DestRC);
  const TargetRegisterClass *NewDestSubRC =
      RI.getSubRegisterClass(NewDestRC, AMDGPU::sub0);

  Register DestSub0 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &LoHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub0)
                              .add(SrcReg0Sub0)
                              .add(SrcReg1Sub0);

  Register DestSub1 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &HiHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub1)
                              .add(SrcReg0Sub1)
                              .add(SrcReg1Sub1);

  Register FullDestReg = MRI.createVirtualRegister(NewDestRC);
  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
    .addReg(DestSub0)
    .addImm(AMDGPU::sub0)
    .addReg(DestSub1)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  Worklist.insert(&LoHalf);
  Worklist.insert(&HiHalf);

  // Move all users of this moved value.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitXnor(SIInstrWorklist &Worklist,
                                       MachineInstr &Inst,
                                       MachineDominatorTree *MDT) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineBasicBlock::iterator MII = Inst;

  const TargetRegisterClass *DestRC = MRI.getRegClass(Dest.getReg());

  Register Interm = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);

  MachineOperand* Op0;
  MachineOperand* Op1;

  if (Src0.isReg() && RI.isSGPRReg(MRI, Src0.getReg())) {
    Op0 = &Src0;
    Op1 = &Src1;
  } else {
    Op0 = &Src1;
    Op1 = &Src0;
  }

  BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B64), Interm)
    .add(*Op0);

  Register NewDest = MRI.createVirtualRegister(DestRC);

  MachineInstr &Xor = *BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B64), NewDest)
    .addReg(Interm)
    .add(*Op1);

  MRI.replaceRegWith(Dest.getReg(), NewDest);

  Worklist.insert(&Xor);
}

void SIInstrInfo::splitScalar64BitBCNT(SIInstrWorklist &Worklist,
                                       MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src = Inst.getOperand(1);

  const MCInstrDesc &InstDesc = get(AMDGPU::V_BCNT_U32_B32_e64);
  const TargetRegisterClass *SrcRC = Src.isReg() ?
    MRI.getRegClass(Src.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  Register MidReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  const TargetRegisterClass *SrcSubRC =
      RI.getSubRegisterClass(SrcRC, AMDGPU::sub0);

  MachineOperand SrcRegSub0 = buildExtractSubRegOrImm(MII, MRI, Src, SrcRC,
                                                      AMDGPU::sub0, SrcSubRC);
  MachineOperand SrcRegSub1 = buildExtractSubRegOrImm(MII, MRI, Src, SrcRC,
                                                      AMDGPU::sub1, SrcSubRC);

  BuildMI(MBB, MII, DL, InstDesc, MidReg).add(SrcRegSub0).addImm(0);

  BuildMI(MBB, MII, DL, InstDesc, ResultReg).add(SrcRegSub1).addReg(MidReg);

  MRI.replaceRegWith(Dest.getReg(), ResultReg);

  // We don't need to legalize operands here. src0 for either instruction can be
  // an SGPR, and the second input is unused or determined here.
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitBFE(SIInstrWorklist &Worklist,
                                      MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  uint32_t Imm = Inst.getOperand(2).getImm();
  uint32_t Offset = Imm & 0x3f; // Extract bits [5:0].
  uint32_t BitWidth = (Imm & 0x7f0000) >> 16; // Extract bits [22:16].

  (void) Offset;

  // Only sext_inreg cases handled.
  assert(Inst.getOpcode() == AMDGPU::S_BFE_I64 && BitWidth <= 32 &&
         Offset == 0 && "Not implemented");

  if (BitWidth < 32) {
    Register MidRegLo = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register MidRegHi = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register ResultReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);

    BuildMI(MBB, MII, DL, get(AMDGPU::V_BFE_I32_e64), MidRegLo)
        .addReg(Inst.getOperand(1).getReg(), 0, AMDGPU::sub0)
        .addImm(0)
        .addImm(BitWidth);

    BuildMI(MBB, MII, DL, get(AMDGPU::V_ASHRREV_I32_e32), MidRegHi)
      .addImm(31)
      .addReg(MidRegLo);

    BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), ResultReg)
      .addReg(MidRegLo)
      .addImm(AMDGPU::sub0)
      .addReg(MidRegHi)
      .addImm(AMDGPU::sub1);

    MRI.replaceRegWith(Dest.getReg(), ResultReg);
    addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
    return;
  }

  MachineOperand &Src = Inst.getOperand(1);
  Register TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register ResultReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);

  BuildMI(MBB, MII, DL, get(AMDGPU::V_ASHRREV_I32_e64), TmpReg)
    .addImm(31)
    .addReg(Src.getReg(), 0, AMDGPU::sub0);

  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), ResultReg)
    .addReg(Src.getReg(), 0, AMDGPU::sub0)
    .addImm(AMDGPU::sub0)
    .addReg(TmpReg)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), ResultReg);
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitCountOp(SIInstrWorklist &Worklist,
                                          MachineInstr &Inst, unsigned Opcode,
                                          MachineDominatorTree *MDT) const {
  //  (S_FLBIT_I32_B64 hi:lo) ->
  // -> (umin (V_FFBH_U32_e32 hi), (uaddsat (V_FFBH_U32_e32 lo), 32))
  //  (S_FF1_I32_B64 hi:lo) ->
  // ->(umin (uaddsat (V_FFBL_B32_e32 hi), 32) (V_FFBL_B32_e32 lo))

  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src = Inst.getOperand(1);

  const MCInstrDesc &InstDesc = get(Opcode);

  bool IsCtlz = Opcode == AMDGPU::V_FFBH_U32_e32;
  unsigned OpcodeAdd =
      ST.hasAddNoCarry() ? AMDGPU::V_ADD_U32_e64 : AMDGPU::V_ADD_CO_U32_e32;

  const TargetRegisterClass *SrcRC =
      Src.isReg() ? MRI.getRegClass(Src.getReg()) : &AMDGPU::SGPR_32RegClass;
  const TargetRegisterClass *SrcSubRC =
      RI.getSubRegisterClass(SrcRC, AMDGPU::sub0);

  MachineOperand SrcRegSub0 =
      buildExtractSubRegOrImm(MII, MRI, Src, SrcRC, AMDGPU::sub0, SrcSubRC);
  MachineOperand SrcRegSub1 =
      buildExtractSubRegOrImm(MII, MRI, Src, SrcRC, AMDGPU::sub1, SrcSubRC);

  Register MidReg1 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register MidReg2 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register MidReg3 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register MidReg4 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  BuildMI(MBB, MII, DL, InstDesc, MidReg1).add(SrcRegSub0);

  BuildMI(MBB, MII, DL, InstDesc, MidReg2).add(SrcRegSub1);

  BuildMI(MBB, MII, DL, get(OpcodeAdd), MidReg3)
      .addReg(IsCtlz ? MidReg1 : MidReg2)
      .addImm(32)
      .addImm(1); // enable clamp

  BuildMI(MBB, MII, DL, get(AMDGPU::V_MIN_U32_e64), MidReg4)
      .addReg(MidReg3)
      .addReg(IsCtlz ? MidReg2 : MidReg1);

  MRI.replaceRegWith(Dest.getReg(), MidReg4);

  addUsersToMoveToVALUWorklist(MidReg4, MRI, Worklist);
}

void SIInstrInfo::addUsersToMoveToVALUWorklist(
    Register DstReg, MachineRegisterInfo &MRI,
    SIInstrWorklist &Worklist) const {
  for (MachineRegisterInfo::use_iterator I = MRI.use_begin(DstReg),
         E = MRI.use_end(); I != E;) {
    MachineInstr &UseMI = *I->getParent();

    unsigned OpNo = 0;

    switch (UseMI.getOpcode()) {
    case AMDGPU::COPY:
    case AMDGPU::WQM:
    case AMDGPU::SOFT_WQM:
    case AMDGPU::STRICT_WWM:
    case AMDGPU::STRICT_WQM:
    case AMDGPU::REG_SEQUENCE:
    case AMDGPU::PHI:
    case AMDGPU::INSERT_SUBREG:
      break;
    default:
      OpNo = I.getOperandNo();
      break;
    }

    if (!RI.hasVectorRegisters(getOpRegClass(UseMI, OpNo))) {
      Worklist.insert(&UseMI);

      do {
        ++I;
      } while (I != E && I->getParent() == &UseMI);
    } else {
      ++I;
    }
  }
}

void SIInstrInfo::movePackToVALU(SIInstrWorklist &Worklist,
                                 MachineRegisterInfo &MRI,
                                 MachineInstr &Inst) const {
  Register ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineBasicBlock *MBB = Inst.getParent();
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();

  switch (Inst.getOpcode()) {
  case AMDGPU::S_PACK_LL_B32_B16: {
    Register ImmReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

    // FIXME: Can do a lot better if we know the high bits of src0 or src1 are
    // 0.
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_MOV_B32_e32), ImmReg)
      .addImm(0xffff);

    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_AND_B32_e64), TmpReg)
      .addReg(ImmReg, RegState::Kill)
      .add(Src0);

    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHL_OR_B32_e64), ResultReg)
      .add(Src1)
      .addImm(16)
      .addReg(TmpReg, RegState::Kill);
    break;
  }
  case AMDGPU::S_PACK_LH_B32_B16: {
    Register ImmReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_MOV_B32_e32), ImmReg)
      .addImm(0xffff);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_BFI_B32_e64), ResultReg)
      .addReg(ImmReg, RegState::Kill)
      .add(Src0)
      .add(Src1);
    break;
  }
  case AMDGPU::S_PACK_HL_B32_B16: {
    Register TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHRREV_B32_e64), TmpReg)
        .addImm(16)
        .add(Src0);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHL_OR_B32_e64), ResultReg)
        .add(Src1)
        .addImm(16)
        .addReg(TmpReg, RegState::Kill);
    break;
  }
  case AMDGPU::S_PACK_HH_B32_B16: {
    Register ImmReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHRREV_B32_e64), TmpReg)
      .addImm(16)
      .add(Src0);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_MOV_B32_e32), ImmReg)
      .addImm(0xffff0000);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_AND_OR_B32_e64), ResultReg)
      .add(Src1)
      .addReg(ImmReg, RegState::Kill)
      .addReg(TmpReg, RegState::Kill);
    break;
  }
  default:
    llvm_unreachable("unhandled s_pack_* instruction");
  }

  MachineOperand &Dest = Inst.getOperand(0);
  MRI.replaceRegWith(Dest.getReg(), ResultReg);
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::addSCCDefUsersToVALUWorklist(MachineOperand &Op,
                                               MachineInstr &SCCDefInst,
                                               SIInstrWorklist &Worklist,
                                               Register NewCond) const {

  // Ensure that def inst defines SCC, which is still live.
  assert(Op.isReg() && Op.getReg() == AMDGPU::SCC && Op.isDef() &&
         !Op.isDead() && Op.getParent() == &SCCDefInst);
  SmallVector<MachineInstr *, 4> CopyToDelete;
  // This assumes that all the users of SCC are in the same block
  // as the SCC def.
  for (MachineInstr &MI : // Skip the def inst itself.
       make_range(std::next(MachineBasicBlock::iterator(SCCDefInst)),
                  SCCDefInst.getParent()->end())) {
    // Check if SCC is used first.
    int SCCIdx = MI.findRegisterUseOperandIdx(AMDGPU::SCC, &RI, false);
    if (SCCIdx != -1) {
      if (MI.isCopy()) {
        MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
        Register DestReg = MI.getOperand(0).getReg();

        MRI.replaceRegWith(DestReg, NewCond);
        CopyToDelete.push_back(&MI);
      } else {

        if (NewCond.isValid())
          MI.getOperand(SCCIdx).setReg(NewCond);

        Worklist.insert(&MI);
      }
    }
    // Exit if we find another SCC def.
    if (MI.findRegisterDefOperandIdx(AMDGPU::SCC, &RI, false, false) != -1)
      break;
  }
  for (auto &Copy : CopyToDelete)
    Copy->eraseFromParent();
}

// Instructions that use SCC may be converted to VALU instructions. When that
// happens, the SCC register is changed to VCC_LO. The instruction that defines
// SCC must be changed to an instruction that defines VCC. This function makes
// sure that the instruction that defines SCC is added to the moveToVALU
// worklist.
void SIInstrInfo::addSCCDefsToVALUWorklist(MachineInstr *SCCUseInst,
                                           SIInstrWorklist &Worklist) const {
  // Look for a preceding instruction that either defines VCC or SCC. If VCC
  // then there is nothing to do because the defining instruction has been
  // converted to a VALU already. If SCC then that instruction needs to be
  // converted to a VALU.
  for (MachineInstr &MI :
       make_range(std::next(MachineBasicBlock::reverse_iterator(SCCUseInst)),
                  SCCUseInst->getParent()->rend())) {
    if (MI.modifiesRegister(AMDGPU::VCC, &RI))
      break;
    if (MI.definesRegister(AMDGPU::SCC, &RI)) {
      Worklist.insert(&MI);
      break;
    }
  }
}

const TargetRegisterClass *SIInstrInfo::getDestEquivalentVGPRClass(
  const MachineInstr &Inst) const {
  const TargetRegisterClass *NewDstRC = getOpRegClass(Inst, 0);

  switch (Inst.getOpcode()) {
  // For target instructions, getOpRegClass just returns the virtual register
  // class associated with the operand, so we need to find an equivalent VGPR
  // register class in order to move the instruction to the VALU.
  case AMDGPU::COPY:
  case AMDGPU::PHI:
  case AMDGPU::REG_SEQUENCE:
  case AMDGPU::INSERT_SUBREG:
  case AMDGPU::WQM:
  case AMDGPU::SOFT_WQM:
  case AMDGPU::STRICT_WWM:
  case AMDGPU::STRICT_WQM: {
    const TargetRegisterClass *SrcRC = getOpRegClass(Inst, 1);
    if (RI.isAGPRClass(SrcRC)) {
      if (RI.isAGPRClass(NewDstRC))
        return nullptr;

      switch (Inst.getOpcode()) {
      case AMDGPU::PHI:
      case AMDGPU::REG_SEQUENCE:
      case AMDGPU::INSERT_SUBREG:
        NewDstRC = RI.getEquivalentAGPRClass(NewDstRC);
        break;
      default:
        NewDstRC = RI.getEquivalentVGPRClass(NewDstRC);
      }

      if (!NewDstRC)
        return nullptr;
    } else {
      if (RI.isVGPRClass(NewDstRC) || NewDstRC == &AMDGPU::VReg_1RegClass)
        return nullptr;

      NewDstRC = RI.getEquivalentVGPRClass(NewDstRC);
      if (!NewDstRC)
        return nullptr;
    }

    return NewDstRC;
  }
  default:
    return NewDstRC;
  }
}

// Find the one SGPR operand we are allowed to use.
Register SIInstrInfo::findUsedSGPR(const MachineInstr &MI,
                                   int OpIndices[3]) const {
  const MCInstrDesc &Desc = MI.getDesc();

  // Find the one SGPR operand we are allowed to use.
  //
  // First we need to consider the instruction's operand requirements before
  // legalizing. Some operands are required to be SGPRs, such as implicit uses
  // of VCC, but we are still bound by the constant bus requirement to only use
  // one.
  //
  // If the operand's class is an SGPR, we can never move it.

  Register SGPRReg = findImplicitSGPRRead(MI);
  if (SGPRReg)
    return SGPRReg;

  Register UsedSGPRs[3] = {Register()};
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();

  for (unsigned i = 0; i < 3; ++i) {
    int Idx = OpIndices[i];
    if (Idx == -1)
      break;

    const MachineOperand &MO = MI.getOperand(Idx);
    if (!MO.isReg())
      continue;

    // Is this operand statically required to be an SGPR based on the operand
    // constraints?
    const TargetRegisterClass *OpRC =
        RI.getRegClass(Desc.operands()[Idx].RegClass);
    bool IsRequiredSGPR = RI.isSGPRClass(OpRC);
    if (IsRequiredSGPR)
      return MO.getReg();

    // If this could be a VGPR or an SGPR, Check the dynamic register class.
    Register Reg = MO.getReg();
    const TargetRegisterClass *RegRC = MRI.getRegClass(Reg);
    if (RI.isSGPRClass(RegRC))
      UsedSGPRs[i] = Reg;
  }

  // We don't have a required SGPR operand, so we have a bit more freedom in
  // selecting operands to move.

  // Try to select the most used SGPR. If an SGPR is equal to one of the
  // others, we choose that.
  //
  // e.g.
  // V_FMA_F32 v0, s0, s0, s0 -> No moves
  // V_FMA_F32 v0, s0, s1, s0 -> Move s1

  // TODO: If some of the operands are 64-bit SGPRs and some 32, we should
  // prefer those.

  if (UsedSGPRs[0]) {
    if (UsedSGPRs[0] == UsedSGPRs[1] || UsedSGPRs[0] == UsedSGPRs[2])
      SGPRReg = UsedSGPRs[0];
  }

  if (!SGPRReg && UsedSGPRs[1]) {
    if (UsedSGPRs[1] == UsedSGPRs[2])
      SGPRReg = UsedSGPRs[1];
  }

  return SGPRReg;
}

MachineOperand *SIInstrInfo::getNamedOperand(MachineInstr &MI,
                                             unsigned OperandName) const {
  int Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), OperandName);
  if (Idx == -1)
    return nullptr;

  return &MI.getOperand(Idx);
}

uint64_t SIInstrInfo::getDefaultRsrcDataFormat() const {
  if (ST.getGeneration() >= AMDGPUSubtarget::GFX10) {
    int64_t Format = ST.getGeneration() >= AMDGPUSubtarget::GFX11
                         ? (int64_t)AMDGPU::UfmtGFX11::UFMT_32_FLOAT
                         : (int64_t)AMDGPU::UfmtGFX10::UFMT_32_FLOAT;
    return (Format << 44) |
           (1ULL << 56) | // RESOURCE_LEVEL = 1
           (3ULL << 60); // OOB_SELECT = 3
  }

  uint64_t RsrcDataFormat = AMDGPU::RSRC_DATA_FORMAT;
  if (ST.isAmdHsaOS()) {
    // Set ATC = 1. GFX9 doesn't have this bit.
    if (ST.getGeneration() <= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      RsrcDataFormat |= (1ULL << 56);

    // Set MTYPE = 2 (MTYPE_UC = uncached). GFX9 doesn't have this.
    // BTW, it disables TC L2 and therefore decreases performance.
    if (ST.getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS)
      RsrcDataFormat |= (2ULL << 59);
  }

  return RsrcDataFormat;
}

uint64_t SIInstrInfo::getScratchRsrcWords23() const {
  uint64_t Rsrc23 = getDefaultRsrcDataFormat() |
                    AMDGPU::RSRC_TID_ENABLE |
                    0xffffffff; // Size;

  // GFX9 doesn't have ELEMENT_SIZE.
  if (ST.getGeneration() <= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
    uint64_t EltSizeValue = Log2_32(ST.getMaxPrivateElementSize(true)) - 1;
    Rsrc23 |= EltSizeValue << AMDGPU::RSRC_ELEMENT_SIZE_SHIFT;
  }

  // IndexStride = 64 / 32.
  uint64_t IndexStride = ST.getWavefrontSize() == 64 ? 3 : 2;
  Rsrc23 |= IndexStride << AMDGPU::RSRC_INDEX_STRIDE_SHIFT;

  // If TID_ENABLE is set, DATA_FORMAT specifies stride bits [14:17].
  // Clear them unless we want a huge stride.
  if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS &&
      ST.getGeneration() <= AMDGPUSubtarget::GFX9)
    Rsrc23 &= ~AMDGPU::RSRC_DATA_FORMAT;

  return Rsrc23;
}

bool SIInstrInfo::isLowLatencyInstruction(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  return isSMRD(Opc);
}

bool SIInstrInfo::isHighLatencyDef(int Opc) const {
  return get(Opc).mayLoad() &&
         (isMUBUF(Opc) || isMTBUF(Opc) || isMIMG(Opc) || isFLAT(Opc));
}

unsigned SIInstrInfo::isStackAccess(const MachineInstr &MI,
                                    int &FrameIndex) const {
  const MachineOperand *Addr = getNamedOperand(MI, AMDGPU::OpName::vaddr);
  if (!Addr || !Addr->isFI())
    return Register();

  assert(!MI.memoperands_empty() &&
         (*MI.memoperands_begin())->getAddrSpace() == AMDGPUAS::PRIVATE_ADDRESS);

  FrameIndex = Addr->getIndex();
  return getNamedOperand(MI, AMDGPU::OpName::vdata)->getReg();
}

unsigned SIInstrInfo::isSGPRStackAccess(const MachineInstr &MI,
                                        int &FrameIndex) const {
  const MachineOperand *Addr = getNamedOperand(MI, AMDGPU::OpName::addr);
  assert(Addr && Addr->isFI());
  FrameIndex = Addr->getIndex();
  return getNamedOperand(MI, AMDGPU::OpName::data)->getReg();
}

Register SIInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  if (!MI.mayLoad())
    return Register();

  if (isMUBUF(MI) || isVGPRSpill(MI))
    return isStackAccess(MI, FrameIndex);

  if (isSGPRSpill(MI))
    return isSGPRStackAccess(MI, FrameIndex);

  return Register();
}

Register SIInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                         int &FrameIndex) const {
  if (!MI.mayStore())
    return Register();

  if (isMUBUF(MI) || isVGPRSpill(MI))
    return isStackAccess(MI, FrameIndex);

  if (isSGPRSpill(MI))
    return isSGPRStackAccess(MI, FrameIndex);

  return Register();
}

unsigned SIInstrInfo::getInstBundleSize(const MachineInstr &MI) const {
  unsigned Size = 0;
  MachineBasicBlock::const_instr_iterator I = MI.getIterator();
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
  while (++I != E && I->isInsideBundle()) {
    assert(!I->isBundle() && "No nested bundle!");
    Size += getInstSizeInBytes(*I);
  }

  return Size;
}

unsigned SIInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  const MCInstrDesc &Desc = getMCOpcodeFromPseudo(Opc);
  unsigned DescSize = Desc.getSize();

  // If we have a definitive size, we can use it. Otherwise we need to inspect
  // the operands to know the size.
  if (isFixedSize(MI)) {
    unsigned Size = DescSize;

    // If we hit the buggy offset, an extra nop will be inserted in MC so
    // estimate the worst case.
    if (MI.isBranch() && ST.hasOffset3fBug())
      Size += 4;

    return Size;
  }

  // Instructions may have a 32-bit literal encoded after them. Check
  // operands that could ever be literals.
  if (isVALU(MI) || isSALU(MI)) {
    if (isDPP(MI))
      return DescSize;
    bool HasLiteral = false;
    for (int I = 0, E = MI.getNumExplicitOperands(); I != E; ++I) {
      const MachineOperand &Op = MI.getOperand(I);
      const MCOperandInfo &OpInfo = Desc.operands()[I];
      if (!Op.isReg() && !isInlineConstant(Op, OpInfo)) {
        HasLiteral = true;
        break;
      }
    }
    return HasLiteral ? DescSize + 4 : DescSize;
  }

  // Check whether we have extra NSA words.
  if (isMIMG(MI)) {
    int VAddr0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vaddr0);
    if (VAddr0Idx < 0)
      return 8;

    int RSrcIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::srsrc);
    return 8 + 4 * ((RSrcIdx - VAddr0Idx + 2) / 4);
  }

  switch (Opc) {
  case TargetOpcode::BUNDLE:
    return getInstBundleSize(MI);
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR: {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo(), &ST);
  }
  default:
    if (MI.isMetaInstruction())
      return 0;
    return DescSize;
  }
}

bool SIInstrInfo::mayAccessFlatAddressSpace(const MachineInstr &MI) const {
  if (!isFLAT(MI))
    return false;

  if (MI.memoperands_empty())
    return true;

  for (const MachineMemOperand *MMO : MI.memoperands()) {
    if (MMO->getAddrSpace() == AMDGPUAS::FLAT_ADDRESS)
      return true;
  }
  return false;
}

bool SIInstrInfo::isNonUniformBranchInstr(MachineInstr &Branch) const {
  return Branch.getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO;
}

void SIInstrInfo::convertNonUniformIfRegion(MachineBasicBlock *IfEntry,
                                            MachineBasicBlock *IfEnd) const {
  MachineBasicBlock::iterator TI = IfEntry->getFirstTerminator();
  assert(TI != IfEntry->end());

  MachineInstr *Branch = &(*TI);
  MachineFunction *MF = IfEntry->getParent();
  MachineRegisterInfo &MRI = IfEntry->getParent()->getRegInfo();

  if (Branch->getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO) {
    Register DstReg = MRI.createVirtualRegister(RI.getBoolRC());
    MachineInstr *SIIF =
        BuildMI(*MF, Branch->getDebugLoc(), get(AMDGPU::SI_IF), DstReg)
            .add(Branch->getOperand(0))
            .add(Branch->getOperand(1));
    MachineInstr *SIEND =
        BuildMI(*MF, Branch->getDebugLoc(), get(AMDGPU::SI_END_CF))
            .addReg(DstReg);

    IfEntry->erase(TI);
    IfEntry->insert(IfEntry->end(), SIIF);
    IfEnd->insert(IfEnd->getFirstNonPHI(), SIEND);
  }
}

void SIInstrInfo::convertNonUniformLoopRegion(
    MachineBasicBlock *LoopEntry, MachineBasicBlock *LoopEnd) const {
  MachineBasicBlock::iterator TI = LoopEnd->getFirstTerminator();
  // We expect 2 terminators, one conditional and one unconditional.
  assert(TI != LoopEnd->end());

  MachineInstr *Branch = &(*TI);
  MachineFunction *MF = LoopEnd->getParent();
  MachineRegisterInfo &MRI = LoopEnd->getParent()->getRegInfo();

  if (Branch->getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO) {

    Register DstReg = MRI.createVirtualRegister(RI.getBoolRC());
    Register BackEdgeReg = MRI.createVirtualRegister(RI.getBoolRC());
    MachineInstrBuilder HeaderPHIBuilder =
        BuildMI(*(MF), Branch->getDebugLoc(), get(TargetOpcode::PHI), DstReg);
    for (MachineBasicBlock *PMBB : LoopEntry->predecessors()) {
      if (PMBB == LoopEnd) {
        HeaderPHIBuilder.addReg(BackEdgeReg);
      } else {
        Register ZeroReg = MRI.createVirtualRegister(RI.getBoolRC());
        materializeImmediate(*PMBB, PMBB->getFirstTerminator(), DebugLoc(),
                             ZeroReg, 0);
        HeaderPHIBuilder.addReg(ZeroReg);
      }
      HeaderPHIBuilder.addMBB(PMBB);
    }
    MachineInstr *HeaderPhi = HeaderPHIBuilder;
    MachineInstr *SIIFBREAK = BuildMI(*(MF), Branch->getDebugLoc(),
                                      get(AMDGPU::SI_IF_BREAK), BackEdgeReg)
                                  .addReg(DstReg)
                                  .add(Branch->getOperand(0));
    MachineInstr *SILOOP =
        BuildMI(*(MF), Branch->getDebugLoc(), get(AMDGPU::SI_LOOP))
            .addReg(BackEdgeReg)
            .addMBB(LoopEntry);

    LoopEntry->insert(LoopEntry->begin(), HeaderPhi);
    LoopEnd->erase(TI);
    LoopEnd->insert(LoopEnd->end(), SIIFBREAK);
    LoopEnd->insert(LoopEnd->end(), SILOOP);
  }
}

ArrayRef<std::pair<int, const char *>>
SIInstrInfo::getSerializableTargetIndices() const {
  static const std::pair<int, const char *> TargetIndices[] = {
      {AMDGPU::TI_CONSTDATA_START, "amdgpu-constdata-start"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD0, "amdgpu-scratch-rsrc-dword0"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD1, "amdgpu-scratch-rsrc-dword1"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD2, "amdgpu-scratch-rsrc-dword2"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD3, "amdgpu-scratch-rsrc-dword3"}};
  return ArrayRef(TargetIndices);
}

/// This is used by the post-RA scheduler (SchedulePostRAList.cpp).  The
/// post-RA version of misched uses CreateTargetMIHazardRecognizer.
ScheduleHazardRecognizer *
SIInstrInfo::CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                            const ScheduleDAG *DAG) const {
  return new GCNHazardRecognizer(DAG->MF);
}

/// This is the hazard recognizer used at -O0 by the PostRAHazardRecognizer
/// pass.
ScheduleHazardRecognizer *
SIInstrInfo::CreateTargetPostRAHazardRecognizer(const MachineFunction &MF) const {
  return new GCNHazardRecognizer(MF);
}

// Called during:
// - pre-RA scheduling and post-RA scheduling
ScheduleHazardRecognizer *
SIInstrInfo::CreateTargetMIHazardRecognizer(const InstrItineraryData *II,
                                            const ScheduleDAGMI *DAG) const {
  // Borrowed from Arm Target
  // We would like to restrict this hazard recognizer to only
  // post-RA scheduling; we can tell that we're post-RA because we don't
  // track VRegLiveness.
  if (!DAG->hasVRegLiveness())
    return new GCNHazardRecognizer(DAG->MF);
  return TargetInstrInfo::CreateTargetMIHazardRecognizer(II, DAG);
}

std::pair<unsigned, unsigned>
SIInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  return std::pair(TF & MO_MASK, TF & ~MO_MASK);
}

ArrayRef<std::pair<unsigned, const char *>>
SIInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  static const std::pair<unsigned, const char *> TargetFlags[] = {
    { MO_GOTPCREL, "amdgpu-gotprel" },
    { MO_GOTPCREL32_LO, "amdgpu-gotprel32-lo" },
    { MO_GOTPCREL32_HI, "amdgpu-gotprel32-hi" },
    { MO_REL32_LO, "amdgpu-rel32-lo" },
    { MO_REL32_HI, "amdgpu-rel32-hi" },
    { MO_ABS32_LO, "amdgpu-abs32-lo" },
    { MO_ABS32_HI, "amdgpu-abs32-hi" },
  };

  return ArrayRef(TargetFlags);
}

ArrayRef<std::pair<MachineMemOperand::Flags, const char *>>
SIInstrInfo::getSerializableMachineMemOperandTargetFlags() const {
  static const std::pair<MachineMemOperand::Flags, const char *> TargetFlags[] =
      {
          {MONoClobber, "amdgpu-noclobber"},
          {MOLastUse, "amdgpu-last-use"},
      };

  return ArrayRef(TargetFlags);
}

unsigned SIInstrInfo::getLiveRangeSplitOpcode(Register SrcReg,
                                              const MachineFunction &MF) const {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  assert(SrcReg.isVirtual());
  if (MFI->checkFlag(SrcReg, AMDGPU::VirtRegFlag::WWM_REG))
    return AMDGPU::WWM_COPY;

  return AMDGPU::COPY;
}

bool SIInstrInfo::isBasicBlockPrologue(const MachineInstr &MI,
                                       Register Reg) const {
  // We need to handle instructions which may be inserted during register
  // allocation to handle the prolog. The initial prolog instruction may have
  // been separated from the start of the block by spills and copies inserted
  // needed by the prolog. However, the insertions for scalar registers can
  // always be placed at the BB top as they are independent of the exec mask
  // value.
  bool IsNullOrVectorRegister = true;
  if (Reg) {
    const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
    IsNullOrVectorRegister = !RI.isSGPRClass(RI.getRegClassForReg(MRI, Reg));
  }

  uint16_t Opcode = MI.getOpcode();
  // FIXME: Copies inserted in the block prolog for live-range split should also
  // be included.
  return IsNullOrVectorRegister &&
         (isSpill(Opcode) || (!MI.isTerminator() && Opcode != AMDGPU::COPY &&
                              MI.modifiesRegister(AMDGPU::EXEC, &RI)));
}

MachineInstrBuilder
SIInstrInfo::getAddNoCarry(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I,
                           const DebugLoc &DL,
                           Register DestReg) const {
  if (ST.hasAddNoCarry())
    return BuildMI(MBB, I, DL, get(AMDGPU::V_ADD_U32_e64), DestReg);

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  Register UnusedCarry = MRI.createVirtualRegister(RI.getBoolRC());
  MRI.setRegAllocationHint(UnusedCarry, 0, RI.getVCC());

  return BuildMI(MBB, I, DL, get(AMDGPU::V_ADD_CO_U32_e64), DestReg)
           .addReg(UnusedCarry, RegState::Define | RegState::Dead);
}

MachineInstrBuilder SIInstrInfo::getAddNoCarry(MachineBasicBlock &MBB,
                                               MachineBasicBlock::iterator I,
                                               const DebugLoc &DL,
                                               Register DestReg,
                                               RegScavenger &RS) const {
  if (ST.hasAddNoCarry())
    return BuildMI(MBB, I, DL, get(AMDGPU::V_ADD_U32_e32), DestReg);

  // If available, prefer to use vcc.
  Register UnusedCarry = !RS.isRegUsed(AMDGPU::VCC)
                             ? Register(RI.getVCC())
                             : RS.scavengeRegisterBackwards(
                                   *RI.getBoolRC(), I, /* RestoreAfter */ false,
                                   0, /* AllowSpill */ false);

  // TODO: Users need to deal with this.
  if (!UnusedCarry.isValid())
    return MachineInstrBuilder();

  return BuildMI(MBB, I, DL, get(AMDGPU::V_ADD_CO_U32_e64), DestReg)
           .addReg(UnusedCarry, RegState::Define | RegState::Dead);
}

bool SIInstrInfo::isKillTerminator(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
  case AMDGPU::SI_KILL_I1_TERMINATOR:
    return true;
  default:
    return false;
  }
}

const MCInstrDesc &SIInstrInfo::getKillTerminatorFromPseudo(unsigned Opcode) const {
  switch (Opcode) {
  case AMDGPU::SI_KILL_F32_COND_IMM_PSEUDO:
    return get(AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR);
  case AMDGPU::SI_KILL_I1_PSEUDO:
    return get(AMDGPU::SI_KILL_I1_TERMINATOR);
  default:
    llvm_unreachable("invalid opcode, expected SI_KILL_*_PSEUDO");
  }
}

bool SIInstrInfo::isLegalMUBUFImmOffset(unsigned Imm) const {
  return Imm <= getMaxMUBUFImmOffset(ST);
}

unsigned SIInstrInfo::getMaxMUBUFImmOffset(const GCNSubtarget &ST) {
  // GFX12 field is non-negative 24-bit signed byte offset.
  const unsigned OffsetBits =
      ST.getGeneration() >= AMDGPUSubtarget::GFX12 ? 23 : 12;
  return (1 << OffsetBits) - 1;
}

void SIInstrInfo::fixImplicitOperands(MachineInstr &MI) const {
  if (!ST.isWave32())
    return;

  if (MI.isInlineAsm())
    return;

  for (auto &Op : MI.implicit_operands()) {
    if (Op.isReg() && Op.getReg() == AMDGPU::VCC)
      Op.setReg(AMDGPU::VCC_LO);
  }
}

bool SIInstrInfo::isBufferSMRD(const MachineInstr &MI) const {
  if (!isSMRD(MI))
    return false;

  // Check that it is using a buffer resource.
  int Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::sbase);
  if (Idx == -1) // e.g. s_memtime
    return false;

  const auto RCID = MI.getDesc().operands()[Idx].RegClass;
  return RI.getRegClass(RCID)->hasSubClassEq(&AMDGPU::SGPR_128RegClass);
}

// Given Imm, split it into the values to put into the SOffset and ImmOffset
// fields in an MUBUF instruction. Return false if it is not possible (due to a
// hardware bug needing a workaround).
//
// The required alignment ensures that individual address components remain
// aligned if they are aligned to begin with. It also ensures that additional
// offsets within the given alignment can be added to the resulting ImmOffset.
bool SIInstrInfo::splitMUBUFOffset(uint32_t Imm, uint32_t &SOffset,
                                   uint32_t &ImmOffset, Align Alignment) const {
  const uint32_t MaxOffset = SIInstrInfo::getMaxMUBUFImmOffset(ST);
  const uint32_t MaxImm = alignDown(MaxOffset, Alignment.value());
  uint32_t Overflow = 0;

  if (Imm > MaxImm) {
    if (Imm <= MaxImm + 64) {
      // Use an SOffset inline constant for 4..64
      Overflow = Imm - MaxImm;
      Imm = MaxImm;
    } else {
      // Try to keep the same value in SOffset for adjacent loads, so that
      // the corresponding register contents can be re-used.
      //
      // Load values with all low-bits (except for alignment bits) set into
      // SOffset, so that a larger range of values can be covered using
      // s_movk_i32.
      //
      // Atomic operations fail to work correctly when individual address
      // components are unaligned, even if their sum is aligned.
      uint32_t High = (Imm + Alignment.value()) & ~MaxOffset;
      uint32_t Low = (Imm + Alignment.value()) & MaxOffset;
      Imm = Low;
      Overflow = High - Alignment.value();
    }
  }

  if (Overflow > 0) {
    // There is a hardware bug in SI and CI which prevents address clamping in
    // MUBUF instructions from working correctly with SOffsets. The immediate
    // offset is unaffected.
    if (ST.getGeneration() <= AMDGPUSubtarget::SEA_ISLANDS)
      return false;

    // It is not possible to set immediate in SOffset field on some targets.
    if (ST.hasRestrictedSOffset())
      return false;
  }

  ImmOffset = Imm;
  SOffset = Overflow;
  return true;
}

// Depending on the used address space and instructions, some immediate offsets
// are allowed and some are not.
// Pre-GFX12, flat instruction offsets can only be non-negative, global and
// scratch instruction offsets can also be negative. On GFX12, offsets can be
// negative for all variants.
//
// There are several bugs related to these offsets:
// On gfx10.1, flat instructions that go into the global address space cannot
// use an offset.
//
// For scratch instructions, the address can be either an SGPR or a VGPR.
// The following offsets can be used, depending on the architecture (x means
// cannot be used):
// +----------------------------+------+------+
// | Address-Mode               | SGPR | VGPR |
// +----------------------------+------+------+
// | gfx9                       |      |      |
// | negative, 4-aligned offset | x    | ok   |
// | negative, unaligned offset | x    | ok   |
// +----------------------------+------+------+
// | gfx10                      |      |      |
// | negative, 4-aligned offset | ok   | ok   |
// | negative, unaligned offset | ok   | x    |
// +----------------------------+------+------+
// | gfx10.3                    |      |      |
// | negative, 4-aligned offset | ok   | ok   |
// | negative, unaligned offset | ok   | ok   |
// +----------------------------+------+------+
//
// This function ignores the addressing mode, so if an offset cannot be used in
// one addressing mode, it is considered illegal.
bool SIInstrInfo::isLegalFLATOffset(int64_t Offset, unsigned AddrSpace,
                                    uint64_t FlatVariant) const {
  // TODO: Should 0 be special cased?
  if (!ST.hasFlatInstOffsets())
    return false;

  if (ST.hasFlatSegmentOffsetBug() && FlatVariant == SIInstrFlags::FLAT &&
      (AddrSpace == AMDGPUAS::FLAT_ADDRESS ||
       AddrSpace == AMDGPUAS::GLOBAL_ADDRESS))
    return false;

  if (ST.hasNegativeUnalignedScratchOffsetBug() &&
      FlatVariant == SIInstrFlags::FlatScratch && Offset < 0 &&
      (Offset % 4) != 0) {
    return false;
  }

  bool AllowNegative = allowNegativeFlatOffset(FlatVariant);
  unsigned N = AMDGPU::getNumFlatOffsetBits(ST);
  return isIntN(N, Offset) && (AllowNegative || Offset >= 0);
}

// See comment on SIInstrInfo::isLegalFLATOffset for what is legal and what not.
std::pair<int64_t, int64_t>
SIInstrInfo::splitFlatOffset(int64_t COffsetVal, unsigned AddrSpace,
                             uint64_t FlatVariant) const {
  int64_t RemainderOffset = COffsetVal;
  int64_t ImmField = 0;

  bool AllowNegative = allowNegativeFlatOffset(FlatVariant);
  const unsigned NumBits = AMDGPU::getNumFlatOffsetBits(ST) - 1;

  if (AllowNegative) {
    // Use signed division by a power of two to truncate towards 0.
    int64_t D = 1LL << NumBits;
    RemainderOffset = (COffsetVal / D) * D;
    ImmField = COffsetVal - RemainderOffset;

    if (ST.hasNegativeUnalignedScratchOffsetBug() &&
        FlatVariant == SIInstrFlags::FlatScratch && ImmField < 0 &&
        (ImmField % 4) != 0) {
      // Make ImmField a multiple of 4
      RemainderOffset += ImmField % 4;
      ImmField -= ImmField % 4;
    }
  } else if (COffsetVal >= 0) {
    ImmField = COffsetVal & maskTrailingOnes<uint64_t>(NumBits);
    RemainderOffset = COffsetVal - ImmField;
  }

  assert(isLegalFLATOffset(ImmField, AddrSpace, FlatVariant));
  assert(RemainderOffset + ImmField == COffsetVal);
  return {ImmField, RemainderOffset};
}

bool SIInstrInfo::allowNegativeFlatOffset(uint64_t FlatVariant) const {
  if (ST.hasNegativeScratchOffsetBug() &&
      FlatVariant == SIInstrFlags::FlatScratch)
    return false;

  return FlatVariant != SIInstrFlags::FLAT || AMDGPU::isGFX12Plus(ST);
}

static unsigned subtargetEncodingFamily(const GCNSubtarget &ST) {
  switch (ST.getGeneration()) {
  default:
    break;
  case AMDGPUSubtarget::SOUTHERN_ISLANDS:
  case AMDGPUSubtarget::SEA_ISLANDS:
    return SIEncodingFamily::SI;
  case AMDGPUSubtarget::VOLCANIC_ISLANDS:
  case AMDGPUSubtarget::GFX9:
    return SIEncodingFamily::VI;
  case AMDGPUSubtarget::GFX10:
    return SIEncodingFamily::GFX10;
  case AMDGPUSubtarget::GFX11:
    return SIEncodingFamily::GFX11;
  case AMDGPUSubtarget::GFX12:
    return SIEncodingFamily::GFX12;
  }
  llvm_unreachable("Unknown subtarget generation!");
}

bool SIInstrInfo::isAsmOnlyOpcode(int MCOp) const {
  switch(MCOp) {
  // These opcodes use indirect register addressing so
  // they need special handling by codegen (currently missing).
  // Therefore it is too risky to allow these opcodes
  // to be selected by dpp combiner or sdwa peepholer.
  case AMDGPU::V_MOVRELS_B32_dpp_gfx10:
  case AMDGPU::V_MOVRELS_B32_sdwa_gfx10:
  case AMDGPU::V_MOVRELD_B32_dpp_gfx10:
  case AMDGPU::V_MOVRELD_B32_sdwa_gfx10:
  case AMDGPU::V_MOVRELSD_B32_dpp_gfx10:
  case AMDGPU::V_MOVRELSD_B32_sdwa_gfx10:
  case AMDGPU::V_MOVRELSD_2_B32_dpp_gfx10:
  case AMDGPU::V_MOVRELSD_2_B32_sdwa_gfx10:
    return true;
  default:
    return false;
  }
}

int SIInstrInfo::pseudoToMCOpcode(int Opcode) const {
  Opcode = SIInstrInfo::getNonSoftWaitcntOpcode(Opcode);

  unsigned Gen = subtargetEncodingFamily(ST);

  if ((get(Opcode).TSFlags & SIInstrFlags::renamedInGFX9) != 0 &&
    ST.getGeneration() == AMDGPUSubtarget::GFX9)
    Gen = SIEncodingFamily::GFX9;

  // Adjust the encoding family to GFX80 for D16 buffer instructions when the
  // subtarget has UnpackedD16VMem feature.
  // TODO: remove this when we discard GFX80 encoding.
  if (ST.hasUnpackedD16VMem() && (get(Opcode).TSFlags & SIInstrFlags::D16Buf))
    Gen = SIEncodingFamily::GFX80;

  if (get(Opcode).TSFlags & SIInstrFlags::SDWA) {
    switch (ST.getGeneration()) {
    default:
      Gen = SIEncodingFamily::SDWA;
      break;
    case AMDGPUSubtarget::GFX9:
      Gen = SIEncodingFamily::SDWA9;
      break;
    case AMDGPUSubtarget::GFX10:
      Gen = SIEncodingFamily::SDWA10;
      break;
    }
  }

  if (isMAI(Opcode)) {
    int MFMAOp = AMDGPU::getMFMAEarlyClobberOp(Opcode);
    if (MFMAOp != -1)
      Opcode = MFMAOp;
  }

  int MCOp = AMDGPU::getMCOpcode(Opcode, Gen);

  // -1 means that Opcode is already a native instruction.
  if (MCOp == -1)
    return Opcode;

  if (ST.hasGFX90AInsts()) {
    uint16_t NMCOp = (uint16_t)-1;
    if (ST.hasGFX940Insts())
      NMCOp = AMDGPU::getMCOpcode(Opcode, SIEncodingFamily::GFX940);
    if (NMCOp == (uint16_t)-1)
      NMCOp = AMDGPU::getMCOpcode(Opcode, SIEncodingFamily::GFX90A);
    if (NMCOp == (uint16_t)-1)
      NMCOp = AMDGPU::getMCOpcode(Opcode, SIEncodingFamily::GFX9);
    if (NMCOp != (uint16_t)-1)
      MCOp = NMCOp;
  }

  // (uint16_t)-1 means that Opcode is a pseudo instruction that has
  // no encoding in the given subtarget generation.
  if (MCOp == (uint16_t)-1)
    return -1;

  if (isAsmOnlyOpcode(MCOp))
    return -1;

  return MCOp;
}

static
TargetInstrInfo::RegSubRegPair getRegOrUndef(const MachineOperand &RegOpnd) {
  assert(RegOpnd.isReg());
  return RegOpnd.isUndef() ? TargetInstrInfo::RegSubRegPair() :
                             getRegSubRegPair(RegOpnd);
}

TargetInstrInfo::RegSubRegPair
llvm::getRegSequenceSubReg(MachineInstr &MI, unsigned SubReg) {
  assert(MI.isRegSequence());
  for (unsigned I = 0, E = (MI.getNumOperands() - 1)/ 2; I < E; ++I)
    if (MI.getOperand(1 + 2 * I + 1).getImm() == SubReg) {
      auto &RegOp = MI.getOperand(1 + 2 * I);
      return getRegOrUndef(RegOp);
    }
  return TargetInstrInfo::RegSubRegPair();
}

// Try to find the definition of reg:subreg in subreg-manipulation pseudos
// Following a subreg of reg:subreg isn't supported
static bool followSubRegDef(MachineInstr &MI,
                            TargetInstrInfo::RegSubRegPair &RSR) {
  if (!RSR.SubReg)
    return false;
  switch (MI.getOpcode()) {
  default: break;
  case AMDGPU::REG_SEQUENCE:
    RSR = getRegSequenceSubReg(MI, RSR.SubReg);
    return true;
  // EXTRACT_SUBREG ins't supported as this would follow a subreg of subreg
  case AMDGPU::INSERT_SUBREG:
    if (RSR.SubReg == (unsigned)MI.getOperand(3).getImm())
      // inserted the subreg we're looking for
      RSR = getRegOrUndef(MI.getOperand(2));
    else { // the subreg in the rest of the reg
      auto R1 = getRegOrUndef(MI.getOperand(1));
      if (R1.SubReg) // subreg of subreg isn't supported
        return false;
      RSR.Reg = R1.Reg;
    }
    return true;
  }
  return false;
}

MachineInstr *llvm::getVRegSubRegDef(const TargetInstrInfo::RegSubRegPair &P,
                                     MachineRegisterInfo &MRI) {
  assert(MRI.isSSA());
  if (!P.Reg.isVirtual())
    return nullptr;

  auto RSR = P;
  auto *DefInst = MRI.getVRegDef(RSR.Reg);
  while (auto *MI = DefInst) {
    DefInst = nullptr;
    switch (MI->getOpcode()) {
    case AMDGPU::COPY:
    case AMDGPU::V_MOV_B32_e32: {
      auto &Op1 = MI->getOperand(1);
      if (Op1.isReg() && Op1.getReg().isVirtual()) {
        if (Op1.isUndef())
          return nullptr;
        RSR = getRegSubRegPair(Op1);
        DefInst = MRI.getVRegDef(RSR.Reg);
      }
      break;
    }
    default:
      if (followSubRegDef(*MI, RSR)) {
        if (!RSR.Reg)
          return nullptr;
        DefInst = MRI.getVRegDef(RSR.Reg);
      }
    }
    if (!DefInst)
      return MI;
  }
  return nullptr;
}

bool llvm::execMayBeModifiedBeforeUse(const MachineRegisterInfo &MRI,
                                      Register VReg,
                                      const MachineInstr &DefMI,
                                      const MachineInstr &UseMI) {
  assert(MRI.isSSA() && "Must be run on SSA");

  auto *TRI = MRI.getTargetRegisterInfo();
  auto *DefBB = DefMI.getParent();

  // Don't bother searching between blocks, although it is possible this block
  // doesn't modify exec.
  if (UseMI.getParent() != DefBB)
    return true;

  const int MaxInstScan = 20;
  int NumInst = 0;

  // Stop scan at the use.
  auto E = UseMI.getIterator();
  for (auto I = std::next(DefMI.getIterator()); I != E; ++I) {
    if (I->isDebugInstr())
      continue;

    if (++NumInst > MaxInstScan)
      return true;

    if (I->modifiesRegister(AMDGPU::EXEC, TRI))
      return true;
  }

  return false;
}

bool llvm::execMayBeModifiedBeforeAnyUse(const MachineRegisterInfo &MRI,
                                         Register VReg,
                                         const MachineInstr &DefMI) {
  assert(MRI.isSSA() && "Must be run on SSA");

  auto *TRI = MRI.getTargetRegisterInfo();
  auto *DefBB = DefMI.getParent();

  const int MaxUseScan = 10;
  int NumUse = 0;

  for (auto &Use : MRI.use_nodbg_operands(VReg)) {
    auto &UseInst = *Use.getParent();
    // Don't bother searching between blocks, although it is possible this block
    // doesn't modify exec.
    if (UseInst.getParent() != DefBB || UseInst.isPHI())
      return true;

    if (++NumUse > MaxUseScan)
      return true;
  }

  if (NumUse == 0)
    return false;

  const int MaxInstScan = 20;
  int NumInst = 0;

  // Stop scan when we have seen all the uses.
  for (auto I = std::next(DefMI.getIterator()); ; ++I) {
    assert(I != DefBB->end());

    if (I->isDebugInstr())
      continue;

    if (++NumInst > MaxInstScan)
      return true;

    for (const MachineOperand &Op : I->operands()) {
      // We don't check reg masks here as they're used only on calls:
      // 1. EXEC is only considered const within one BB
      // 2. Call should be a terminator instruction if present in a BB

      if (!Op.isReg())
        continue;

      Register Reg = Op.getReg();
      if (Op.isUse()) {
        if (Reg == VReg && --NumUse == 0)
          return false;
      } else if (TRI->regsOverlap(Reg, AMDGPU::EXEC))
        return true;
    }
  }
}

MachineInstr *SIInstrInfo::createPHIDestinationCopy(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator LastPHIIt,
    const DebugLoc &DL, Register Src, Register Dst) const {
  auto Cur = MBB.begin();
  if (Cur != MBB.end())
    do {
      if (!Cur->isPHI() && Cur->readsRegister(Dst, /*TRI=*/nullptr))
        return BuildMI(MBB, Cur, DL, get(TargetOpcode::COPY), Dst).addReg(Src);
      ++Cur;
    } while (Cur != MBB.end() && Cur != LastPHIIt);

  return TargetInstrInfo::createPHIDestinationCopy(MBB, LastPHIIt, DL, Src,
                                                   Dst);
}

MachineInstr *SIInstrInfo::createPHISourceCopy(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsPt,
    const DebugLoc &DL, Register Src, unsigned SrcSubReg, Register Dst) const {
  if (InsPt != MBB.end() &&
      (InsPt->getOpcode() == AMDGPU::SI_IF ||
       InsPt->getOpcode() == AMDGPU::SI_ELSE ||
       InsPt->getOpcode() == AMDGPU::SI_IF_BREAK) &&
      InsPt->definesRegister(Src, /*TRI=*/nullptr)) {
    InsPt++;
    return BuildMI(MBB, InsPt, DL,
                   get(ST.isWave32() ? AMDGPU::S_MOV_B32_term
                                     : AMDGPU::S_MOV_B64_term),
                   Dst)
        .addReg(Src, 0, SrcSubReg)
        .addReg(AMDGPU::EXEC, RegState::Implicit);
  }
  return TargetInstrInfo::createPHISourceCopy(MBB, InsPt, DL, Src, SrcSubReg,
                                              Dst);
}

bool llvm::SIInstrInfo::isWave32() const { return ST.isWave32(); }

MachineInstr *SIInstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, int FrameIndex, LiveIntervals *LIS,
    VirtRegMap *VRM) const {
  // This is a bit of a hack (copied from AArch64). Consider this instruction:
  //
  //   %0:sreg_32 = COPY $m0
  //
  // We explicitly chose SReg_32 for the virtual register so such a copy might
  // be eliminated by RegisterCoalescer. However, that may not be possible, and
  // %0 may even spill. We can't spill $m0 normally (it would require copying to
  // a numbered SGPR anyway), and since it is in the SReg_32 register class,
  // TargetInstrInfo::foldMemoryOperand() is going to try.
  // A similar issue also exists with spilling and reloading $exec registers.
  //
  // To prevent that, constrain the %0 register class here.
  if (isFullCopyInstr(MI)) {
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    if ((DstReg.isVirtual() || SrcReg.isVirtual()) &&
        (DstReg.isVirtual() != SrcReg.isVirtual())) {
      MachineRegisterInfo &MRI = MF.getRegInfo();
      Register VirtReg = DstReg.isVirtual() ? DstReg : SrcReg;
      const TargetRegisterClass *RC = MRI.getRegClass(VirtReg);
      if (RC->hasSuperClassEq(&AMDGPU::SReg_32RegClass)) {
        MRI.constrainRegClass(VirtReg, &AMDGPU::SReg_32_XM0_XEXECRegClass);
        return nullptr;
      }
      if (RC->hasSuperClassEq(&AMDGPU::SReg_64RegClass)) {
        MRI.constrainRegClass(VirtReg, &AMDGPU::SReg_64_XEXECRegClass);
        return nullptr;
      }
    }
  }

  return nullptr;
}

unsigned SIInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                      const MachineInstr &MI,
                                      unsigned *PredCost) const {
  if (MI.isBundle()) {
    MachineBasicBlock::const_instr_iterator I(MI.getIterator());
    MachineBasicBlock::const_instr_iterator E(MI.getParent()->instr_end());
    unsigned Lat = 0, Count = 0;
    for (++I; I != E && I->isBundledWithPred(); ++I) {
      ++Count;
      Lat = std::max(Lat, SchedModel.computeInstrLatency(&*I));
    }
    return Lat + Count - 1;
  }

  return SchedModel.computeInstrLatency(&MI);
}

InstructionUniformity
SIInstrInfo::getGenericInstructionUniformity(const MachineInstr &MI) const {
  unsigned opcode = MI.getOpcode();
  if (auto *GI = dyn_cast<GIntrinsic>(&MI)) {
    auto IID = GI->getIntrinsicID();
    if (AMDGPU::isIntrinsicSourceOfDivergence(IID))
      return InstructionUniformity::NeverUniform;
    if (AMDGPU::isIntrinsicAlwaysUniform(IID))
      return InstructionUniformity::AlwaysUniform;

    switch (IID) {
    case Intrinsic::amdgcn_if:
    case Intrinsic::amdgcn_else:
      // FIXME: Uniform if second result
      break;
    }

    return InstructionUniformity::Default;
  }

  // Loads from the private and flat address spaces are divergent, because
  // threads can execute the load instruction with the same inputs and get
  // different results.
  //
  // All other loads are not divergent, because if threads issue loads with the
  // same arguments, they will always get the same result.
  if (opcode == AMDGPU::G_LOAD) {
    if (MI.memoperands_empty())
      return InstructionUniformity::NeverUniform; // conservative assumption

    if (llvm::any_of(MI.memoperands(), [](const MachineMemOperand *mmo) {
          return mmo->getAddrSpace() == AMDGPUAS::PRIVATE_ADDRESS ||
                 mmo->getAddrSpace() == AMDGPUAS::FLAT_ADDRESS;
        })) {
      // At least one MMO in a non-global address space.
      return InstructionUniformity::NeverUniform;
    }
    return InstructionUniformity::Default;
  }

  if (SIInstrInfo::isGenericAtomicRMWOpcode(opcode) ||
      opcode == AMDGPU::G_ATOMIC_CMPXCHG ||
      opcode == AMDGPU::G_ATOMIC_CMPXCHG_WITH_SUCCESS ||
      AMDGPU::isGenericAtomic(opcode)) {
    return InstructionUniformity::NeverUniform;
  }
  return InstructionUniformity::Default;
}

InstructionUniformity
SIInstrInfo::getInstructionUniformity(const MachineInstr &MI) const {

  if (isNeverUniform(MI))
    return InstructionUniformity::NeverUniform;

  unsigned opcode = MI.getOpcode();
  if (opcode == AMDGPU::V_READLANE_B32 ||
      opcode == AMDGPU::V_READFIRSTLANE_B32 ||
      opcode == AMDGPU::SI_RESTORE_S32_FROM_VGPR)
    return InstructionUniformity::AlwaysUniform;

  if (isCopyInstr(MI)) {
    const MachineOperand &srcOp = MI.getOperand(1);
    if (srcOp.isReg() && srcOp.getReg().isPhysical()) {
      const TargetRegisterClass *regClass =
          RI.getPhysRegBaseClass(srcOp.getReg());
      return RI.isSGPRClass(regClass) ? InstructionUniformity::AlwaysUniform
                                      : InstructionUniformity::NeverUniform;
    }
    return InstructionUniformity::Default;
  }

  // GMIR handling
  if (MI.isPreISelOpcode())
    return SIInstrInfo::getGenericInstructionUniformity(MI);

  // Atomics are divergent because they are executed sequentially: when an
  // atomic operation refers to the same address in each thread, then each
  // thread after the first sees the value written by the previous thread as
  // original value.

  if (isAtomic(MI))
    return InstructionUniformity::NeverUniform;

  // Loads from the private and flat address spaces are divergent, because
  // threads can execute the load instruction with the same inputs and get
  // different results.
  if (isFLAT(MI) && MI.mayLoad()) {
    if (MI.memoperands_empty())
      return InstructionUniformity::NeverUniform; // conservative assumption

    if (llvm::any_of(MI.memoperands(), [](const MachineMemOperand *mmo) {
          return mmo->getAddrSpace() == AMDGPUAS::PRIVATE_ADDRESS ||
                 mmo->getAddrSpace() == AMDGPUAS::FLAT_ADDRESS;
        })) {
      // At least one MMO in a non-global address space.
      return InstructionUniformity::NeverUniform;
    }

    return InstructionUniformity::Default;
  }

  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  const AMDGPURegisterBankInfo *RBI = ST.getRegBankInfo();

  // FIXME: It's conceptually broken to report this for an instruction, and not
  // a specific def operand. For inline asm in particular, there could be mixed
  // uniform and divergent results.
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MachineOperand &SrcOp = MI.getOperand(I);
    if (!SrcOp.isReg())
      continue;

    Register Reg = SrcOp.getReg();
    if (!Reg || !SrcOp.readsReg())
      continue;

    // If RegBank is null, this is unassigned or an unallocatable special
    // register, which are all scalars.
    const RegisterBank *RegBank = RBI->getRegBank(Reg, MRI, RI);
    if (RegBank && RegBank->getID() != AMDGPU::SGPRRegBankID)
      return InstructionUniformity::NeverUniform;
  }

  // TODO: Uniformity check condtions above can be rearranged for more
  // redability

  // TODO: amdgcn.{ballot, [if]cmp} should be AlwaysUniform, but they are
  //       currently turned into no-op COPYs by SelectionDAG ISel and are
  //       therefore no longer recognizable.

  return InstructionUniformity::Default;
}

unsigned SIInstrInfo::getDSShaderTypeValue(const MachineFunction &MF) {
  switch (MF.getFunction().getCallingConv()) {
  case CallingConv::AMDGPU_PS:
    return 1;
  case CallingConv::AMDGPU_VS:
    return 2;
  case CallingConv::AMDGPU_GS:
    return 3;
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_LS:
  case CallingConv::AMDGPU_ES:
    report_fatal_error("ds_ordered_count unsupported for this calling conv");
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::C:
  case CallingConv::Fast:
  default:
    // Assume other calling conventions are various compute callable functions
    return 0;
  }
}

bool SIInstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                 Register &SrcReg2, int64_t &CmpMask,
                                 int64_t &CmpValue) const {
  if (!MI.getOperand(0).isReg() || MI.getOperand(0).getSubReg())
    return false;

  switch (MI.getOpcode()) {
  default:
    break;
  case AMDGPU::S_CMP_EQ_U32:
  case AMDGPU::S_CMP_EQ_I32:
  case AMDGPU::S_CMP_LG_U32:
  case AMDGPU::S_CMP_LG_I32:
  case AMDGPU::S_CMP_LT_U32:
  case AMDGPU::S_CMP_LT_I32:
  case AMDGPU::S_CMP_GT_U32:
  case AMDGPU::S_CMP_GT_I32:
  case AMDGPU::S_CMP_LE_U32:
  case AMDGPU::S_CMP_LE_I32:
  case AMDGPU::S_CMP_GE_U32:
  case AMDGPU::S_CMP_GE_I32:
  case AMDGPU::S_CMP_EQ_U64:
  case AMDGPU::S_CMP_LG_U64:
    SrcReg = MI.getOperand(0).getReg();
    if (MI.getOperand(1).isReg()) {
      if (MI.getOperand(1).getSubReg())
        return false;
      SrcReg2 = MI.getOperand(1).getReg();
      CmpValue = 0;
    } else if (MI.getOperand(1).isImm()) {
      SrcReg2 = Register();
      CmpValue = MI.getOperand(1).getImm();
    } else {
      return false;
    }
    CmpMask = ~0;
    return true;
  case AMDGPU::S_CMPK_EQ_U32:
  case AMDGPU::S_CMPK_EQ_I32:
  case AMDGPU::S_CMPK_LG_U32:
  case AMDGPU::S_CMPK_LG_I32:
  case AMDGPU::S_CMPK_LT_U32:
  case AMDGPU::S_CMPK_LT_I32:
  case AMDGPU::S_CMPK_GT_U32:
  case AMDGPU::S_CMPK_GT_I32:
  case AMDGPU::S_CMPK_LE_U32:
  case AMDGPU::S_CMPK_LE_I32:
  case AMDGPU::S_CMPK_GE_U32:
  case AMDGPU::S_CMPK_GE_I32:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = Register();
    CmpValue = MI.getOperand(1).getImm();
    CmpMask = ~0;
    return true;
  }

  return false;
}

bool SIInstrInfo::optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                                       Register SrcReg2, int64_t CmpMask,
                                       int64_t CmpValue,
                                       const MachineRegisterInfo *MRI) const {
  if (!SrcReg || SrcReg.isPhysical())
    return false;

  if (SrcReg2 && !getFoldableImm(SrcReg2, *MRI, CmpValue))
    return false;

  const auto optimizeCmpAnd = [&CmpInstr, SrcReg, CmpValue, MRI,
                               this](int64_t ExpectedValue, unsigned SrcSize,
                                     bool IsReversible, bool IsSigned) -> bool {
    // s_cmp_eq_u32 (s_and_b32 $src, 1 << n), 1 << n => s_and_b32 $src, 1 << n
    // s_cmp_eq_i32 (s_and_b32 $src, 1 << n), 1 << n => s_and_b32 $src, 1 << n
    // s_cmp_ge_u32 (s_and_b32 $src, 1 << n), 1 << n => s_and_b32 $src, 1 << n
    // s_cmp_ge_i32 (s_and_b32 $src, 1 << n), 1 << n => s_and_b32 $src, 1 << n
    // s_cmp_eq_u64 (s_and_b64 $src, 1 << n), 1 << n => s_and_b64 $src, 1 << n
    // s_cmp_lg_u32 (s_and_b32 $src, 1 << n), 0 => s_and_b32 $src, 1 << n
    // s_cmp_lg_i32 (s_and_b32 $src, 1 << n), 0 => s_and_b32 $src, 1 << n
    // s_cmp_gt_u32 (s_and_b32 $src, 1 << n), 0 => s_and_b32 $src, 1 << n
    // s_cmp_gt_i32 (s_and_b32 $src, 1 << n), 0 => s_and_b32 $src, 1 << n
    // s_cmp_lg_u64 (s_and_b64 $src, 1 << n), 0 => s_and_b64 $src, 1 << n
    //
    // Signed ge/gt are not used for the sign bit.
    //
    // If result of the AND is unused except in the compare:
    // s_and_b(32|64) $src, 1 << n => s_bitcmp1_b(32|64) $src, n
    //
    // s_cmp_eq_u32 (s_and_b32 $src, 1 << n), 0 => s_bitcmp0_b32 $src, n
    // s_cmp_eq_i32 (s_and_b32 $src, 1 << n), 0 => s_bitcmp0_b32 $src, n
    // s_cmp_eq_u64 (s_and_b64 $src, 1 << n), 0 => s_bitcmp0_b64 $src, n
    // s_cmp_lg_u32 (s_and_b32 $src, 1 << n), 1 << n => s_bitcmp0_b32 $src, n
    // s_cmp_lg_i32 (s_and_b32 $src, 1 << n), 1 << n => s_bitcmp0_b32 $src, n
    // s_cmp_lg_u64 (s_and_b64 $src, 1 << n), 1 << n => s_bitcmp0_b64 $src, n

    MachineInstr *Def = MRI->getUniqueVRegDef(SrcReg);
    if (!Def || Def->getParent() != CmpInstr.getParent())
      return false;

    if (Def->getOpcode() != AMDGPU::S_AND_B32 &&
        Def->getOpcode() != AMDGPU::S_AND_B64)
      return false;

    int64_t Mask;
    const auto isMask = [&Mask, SrcSize](const MachineOperand *MO) -> bool {
      if (MO->isImm())
        Mask = MO->getImm();
      else if (!getFoldableImm(MO, Mask))
        return false;
      Mask &= maxUIntN(SrcSize);
      return isPowerOf2_64(Mask);
    };

    MachineOperand *SrcOp = &Def->getOperand(1);
    if (isMask(SrcOp))
      SrcOp = &Def->getOperand(2);
    else if (isMask(&Def->getOperand(2)))
      SrcOp = &Def->getOperand(1);
    else
      return false;

    unsigned BitNo = llvm::countr_zero((uint64_t)Mask);
    if (IsSigned && BitNo == SrcSize - 1)
      return false;

    ExpectedValue <<= BitNo;

    bool IsReversedCC = false;
    if (CmpValue != ExpectedValue) {
      if (!IsReversible)
        return false;
      IsReversedCC = CmpValue == (ExpectedValue ^ Mask);
      if (!IsReversedCC)
        return false;
    }

    Register DefReg = Def->getOperand(0).getReg();
    if (IsReversedCC && !MRI->hasOneNonDBGUse(DefReg))
      return false;

    for (auto I = std::next(Def->getIterator()), E = CmpInstr.getIterator();
         I != E; ++I) {
      if (I->modifiesRegister(AMDGPU::SCC, &RI) ||
          I->killsRegister(AMDGPU::SCC, &RI))
        return false;
    }

    MachineOperand *SccDef =
        Def->findRegisterDefOperand(AMDGPU::SCC, /*TRI=*/nullptr);
    SccDef->setIsDead(false);
    CmpInstr.eraseFromParent();

    if (!MRI->use_nodbg_empty(DefReg)) {
      assert(!IsReversedCC);
      return true;
    }

    // Replace AND with unused result with a S_BITCMP.
    MachineBasicBlock *MBB = Def->getParent();

    unsigned NewOpc = (SrcSize == 32) ? IsReversedCC ? AMDGPU::S_BITCMP0_B32
                                                     : AMDGPU::S_BITCMP1_B32
                                      : IsReversedCC ? AMDGPU::S_BITCMP0_B64
                                                     : AMDGPU::S_BITCMP1_B64;

    BuildMI(*MBB, Def, Def->getDebugLoc(), get(NewOpc))
      .add(*SrcOp)
      .addImm(BitNo);
    Def->eraseFromParent();

    return true;
  };

  switch (CmpInstr.getOpcode()) {
  default:
    break;
  case AMDGPU::S_CMP_EQ_U32:
  case AMDGPU::S_CMP_EQ_I32:
  case AMDGPU::S_CMPK_EQ_U32:
  case AMDGPU::S_CMPK_EQ_I32:
    return optimizeCmpAnd(1, 32, true, false);
  case AMDGPU::S_CMP_GE_U32:
  case AMDGPU::S_CMPK_GE_U32:
    return optimizeCmpAnd(1, 32, false, false);
  case AMDGPU::S_CMP_GE_I32:
  case AMDGPU::S_CMPK_GE_I32:
    return optimizeCmpAnd(1, 32, false, true);
  case AMDGPU::S_CMP_EQ_U64:
    return optimizeCmpAnd(1, 64, true, false);
  case AMDGPU::S_CMP_LG_U32:
  case AMDGPU::S_CMP_LG_I32:
  case AMDGPU::S_CMPK_LG_U32:
  case AMDGPU::S_CMPK_LG_I32:
    return optimizeCmpAnd(0, 32, true, false);
  case AMDGPU::S_CMP_GT_U32:
  case AMDGPU::S_CMPK_GT_U32:
    return optimizeCmpAnd(0, 32, false, false);
  case AMDGPU::S_CMP_GT_I32:
  case AMDGPU::S_CMPK_GT_I32:
    return optimizeCmpAnd(0, 32, false, true);
  case AMDGPU::S_CMP_LG_U64:
    return optimizeCmpAnd(0, 64, true, false);
  }

  return false;
}

void SIInstrInfo::enforceOperandRCAlignment(MachineInstr &MI,
                                            unsigned OpName) const {
  if (!ST.needsAlignedVGPRs())
    return;

  int OpNo = AMDGPU::getNamedOperandIdx(MI.getOpcode(), OpName);
  if (OpNo < 0)
    return;
  MachineOperand &Op = MI.getOperand(OpNo);
  if (getOpSize(MI, OpNo) > 4)
    return;

  // Add implicit aligned super-reg to force alignment on the data operand.
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock *BB = MI.getParent();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  Register DataReg = Op.getReg();
  bool IsAGPR = RI.isAGPR(MRI, DataReg);
  Register Undef = MRI.createVirtualRegister(
      IsAGPR ? &AMDGPU::AGPR_32RegClass : &AMDGPU::VGPR_32RegClass);
  BuildMI(*BB, MI, DL, get(AMDGPU::IMPLICIT_DEF), Undef);
  Register NewVR =
      MRI.createVirtualRegister(IsAGPR ? &AMDGPU::AReg_64_Align2RegClass
                                       : &AMDGPU::VReg_64_Align2RegClass);
  BuildMI(*BB, MI, DL, get(AMDGPU::REG_SEQUENCE), NewVR)
      .addReg(DataReg, 0, Op.getSubReg())
      .addImm(AMDGPU::sub0)
      .addReg(Undef)
      .addImm(AMDGPU::sub1);
  Op.setReg(NewVR);
  Op.setSubReg(AMDGPU::sub0);
  MI.addOperand(MachineOperand::CreateReg(NewVR, false, true));
}
