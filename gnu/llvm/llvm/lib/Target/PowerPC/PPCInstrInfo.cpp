//===-- PPCInstrInfo.cpp - PowerPC Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "PPCInstrInfo.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCHazardRecognizers.h"
#include "PPCInstrBuilder.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-instr-info"

#define GET_INSTRMAP_INFO
#define GET_INSTRINFO_CTOR_DTOR
#include "PPCGenInstrInfo.inc"

STATISTIC(NumStoreSPILLVSRRCAsVec,
          "Number of spillvsrrc spilled to stack as vec");
STATISTIC(NumStoreSPILLVSRRCAsGpr,
          "Number of spillvsrrc spilled to stack as gpr");
STATISTIC(NumGPRtoVSRSpill, "Number of gpr spills to spillvsrrc");
STATISTIC(CmpIselsConverted,
          "Number of ISELs that depend on comparison of constants converted");
STATISTIC(MissedConvertibleImmediateInstrs,
          "Number of compare-immediate instructions fed by constants");
STATISTIC(NumRcRotatesConvertedToRcAnd,
          "Number of record-form rotates converted to record-form andi");

static cl::
opt<bool> DisableCTRLoopAnal("disable-ppc-ctrloop-analysis", cl::Hidden,
            cl::desc("Disable analysis for CTR loops"));

static cl::opt<bool> DisableCmpOpt("disable-ppc-cmp-opt",
cl::desc("Disable compare instruction optimization"), cl::Hidden);

static cl::opt<bool> VSXSelfCopyCrash("crash-on-ppc-vsx-self-copy",
cl::desc("Causes the backend to crash instead of generating a nop VSX copy"),
cl::Hidden);

static cl::opt<bool>
UseOldLatencyCalc("ppc-old-latency-calc", cl::Hidden,
  cl::desc("Use the old (incorrect) instruction latency calculation"));

static cl::opt<float>
    FMARPFactor("ppc-fma-rp-factor", cl::Hidden, cl::init(1.5),
                cl::desc("register pressure factor for the transformations."));

static cl::opt<bool> EnableFMARegPressureReduction(
    "ppc-fma-rp-reduction", cl::Hidden, cl::init(true),
    cl::desc("enable register pressure reduce in machine combiner pass."));

// Pin the vtable to this file.
void PPCInstrInfo::anchor() {}

PPCInstrInfo::PPCInstrInfo(PPCSubtarget &STI)
    : PPCGenInstrInfo(PPC::ADJCALLSTACKDOWN, PPC::ADJCALLSTACKUP,
                      /* CatchRetOpcode */ -1,
                      STI.isPPC64() ? PPC::BLR8 : PPC::BLR),
      Subtarget(STI), RI(STI.getTargetMachine()) {}

/// CreateTargetHazardRecognizer - Return the hazard recognizer to use for
/// this target when scheduling the DAG.
ScheduleHazardRecognizer *
PPCInstrInfo::CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                                           const ScheduleDAG *DAG) const {
  unsigned Directive =
      static_cast<const PPCSubtarget *>(STI)->getCPUDirective();
  if (Directive == PPC::DIR_440 || Directive == PPC::DIR_A2 ||
      Directive == PPC::DIR_E500mc || Directive == PPC::DIR_E5500) {
    const InstrItineraryData *II =
        static_cast<const PPCSubtarget *>(STI)->getInstrItineraryData();
    return new ScoreboardHazardRecognizer(II, DAG);
  }

  return TargetInstrInfo::CreateTargetHazardRecognizer(STI, DAG);
}

/// CreateTargetPostRAHazardRecognizer - Return the postRA hazard recognizer
/// to use for this target when scheduling the DAG.
ScheduleHazardRecognizer *
PPCInstrInfo::CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                                 const ScheduleDAG *DAG) const {
  unsigned Directive =
      DAG->MF.getSubtarget<PPCSubtarget>().getCPUDirective();

  // FIXME: Leaving this as-is until we have POWER9 scheduling info
  if (Directive == PPC::DIR_PWR7 || Directive == PPC::DIR_PWR8)
    return new PPCDispatchGroupSBHazardRecognizer(II, DAG);

  // Most subtargets use a PPC970 recognizer.
  if (Directive != PPC::DIR_440 && Directive != PPC::DIR_A2 &&
      Directive != PPC::DIR_E500mc && Directive != PPC::DIR_E5500) {
    assert(DAG->TII && "No InstrInfo?");

    return new PPCHazardRecognizer970(*DAG);
  }

  return new ScoreboardHazardRecognizer(II, DAG);
}

unsigned PPCInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                       const MachineInstr &MI,
                                       unsigned *PredCost) const {
  if (!ItinData || UseOldLatencyCalc)
    return PPCGenInstrInfo::getInstrLatency(ItinData, MI, PredCost);

  // The default implementation of getInstrLatency calls getStageLatency, but
  // getStageLatency does not do the right thing for us. While we have
  // itinerary, most cores are fully pipelined, and so the itineraries only
  // express the first part of the pipeline, not every stage. Instead, we need
  // to use the listed output operand cycle number (using operand 0 here, which
  // is an output).

  unsigned Latency = 1;
  unsigned DefClass = MI.getDesc().getSchedClass();
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isDef() || MO.isImplicit())
      continue;

    std::optional<unsigned> Cycle = ItinData->getOperandCycle(DefClass, i);
    if (!Cycle)
      continue;

    Latency = std::max(Latency, *Cycle);
  }

  return Latency;
}

std::optional<unsigned> PPCInstrInfo::getOperandLatency(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI,
    unsigned DefIdx, const MachineInstr &UseMI, unsigned UseIdx) const {
  std::optional<unsigned> Latency = PPCGenInstrInfo::getOperandLatency(
      ItinData, DefMI, DefIdx, UseMI, UseIdx);

  if (!DefMI.getParent())
    return Latency;

  const MachineOperand &DefMO = DefMI.getOperand(DefIdx);
  Register Reg = DefMO.getReg();

  bool IsRegCR;
  if (Reg.isVirtual()) {
    const MachineRegisterInfo *MRI =
        &DefMI.getParent()->getParent()->getRegInfo();
    IsRegCR = MRI->getRegClass(Reg)->hasSuperClassEq(&PPC::CRRCRegClass) ||
              MRI->getRegClass(Reg)->hasSuperClassEq(&PPC::CRBITRCRegClass);
  } else {
    IsRegCR = PPC::CRRCRegClass.contains(Reg) ||
              PPC::CRBITRCRegClass.contains(Reg);
  }

  if (UseMI.isBranch() && IsRegCR) {
    if (!Latency)
      Latency = getInstrLatency(ItinData, DefMI);

    // On some cores, there is an additional delay between writing to a condition
    // register, and using it from a branch.
    unsigned Directive = Subtarget.getCPUDirective();
    switch (Directive) {
    default: break;
    case PPC::DIR_7400:
    case PPC::DIR_750:
    case PPC::DIR_970:
    case PPC::DIR_E5500:
    case PPC::DIR_PWR4:
    case PPC::DIR_PWR5:
    case PPC::DIR_PWR5X:
    case PPC::DIR_PWR6:
    case PPC::DIR_PWR6X:
    case PPC::DIR_PWR7:
    case PPC::DIR_PWR8:
    // FIXME: Is this needed for POWER9?
    Latency = *Latency + 2;
    break;
    }
  }

  return Latency;
}

void PPCInstrInfo::setSpecialOperandAttr(MachineInstr &MI,
                                         uint32_t Flags) const {
  MI.setFlags(Flags);
  MI.clearFlag(MachineInstr::MIFlag::NoSWrap);
  MI.clearFlag(MachineInstr::MIFlag::NoUWrap);
  MI.clearFlag(MachineInstr::MIFlag::IsExact);
}

// This function does not list all associative and commutative operations, but
// only those worth feeding through the machine combiner in an attempt to
// reduce the critical path. Mostly, this means floating-point operations,
// because they have high latencies(>=5) (compared to other operations, such as
// and/or, which are also associative and commutative, but have low latencies).
bool PPCInstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst,
                                               bool Invert) const {
  if (Invert)
    return false;
  switch (Inst.getOpcode()) {
  // Floating point:
  // FP Add:
  case PPC::FADD:
  case PPC::FADDS:
  // FP Multiply:
  case PPC::FMUL:
  case PPC::FMULS:
  // Altivec Add:
  case PPC::VADDFP:
  // VSX Add:
  case PPC::XSADDDP:
  case PPC::XVADDDP:
  case PPC::XVADDSP:
  case PPC::XSADDSP:
  // VSX Multiply:
  case PPC::XSMULDP:
  case PPC::XVMULDP:
  case PPC::XVMULSP:
  case PPC::XSMULSP:
    return Inst.getFlag(MachineInstr::MIFlag::FmReassoc) &&
           Inst.getFlag(MachineInstr::MIFlag::FmNsz);
  // Fixed point:
  // Multiply:
  case PPC::MULHD:
  case PPC::MULLD:
  case PPC::MULHW:
  case PPC::MULLW:
    return true;
  default:
    return false;
  }
}

#define InfoArrayIdxFMAInst 0
#define InfoArrayIdxFAddInst 1
#define InfoArrayIdxFMULInst 2
#define InfoArrayIdxAddOpIdx 3
#define InfoArrayIdxMULOpIdx 4
#define InfoArrayIdxFSubInst 5
// Array keeps info for FMA instructions:
// Index 0(InfoArrayIdxFMAInst): FMA instruction;
// Index 1(InfoArrayIdxFAddInst): ADD instruction associated with FMA;
// Index 2(InfoArrayIdxFMULInst): MUL instruction associated with FMA;
// Index 3(InfoArrayIdxAddOpIdx): ADD operand index in FMA operands;
// Index 4(InfoArrayIdxMULOpIdx): first MUL operand index in FMA operands;
//                                second MUL operand index is plus 1;
// Index 5(InfoArrayIdxFSubInst): SUB instruction associated with FMA.
static const uint16_t FMAOpIdxInfo[][6] = {
    // FIXME: Add more FMA instructions like XSNMADDADP and so on.
    {PPC::XSMADDADP, PPC::XSADDDP, PPC::XSMULDP, 1, 2, PPC::XSSUBDP},
    {PPC::XSMADDASP, PPC::XSADDSP, PPC::XSMULSP, 1, 2, PPC::XSSUBSP},
    {PPC::XVMADDADP, PPC::XVADDDP, PPC::XVMULDP, 1, 2, PPC::XVSUBDP},
    {PPC::XVMADDASP, PPC::XVADDSP, PPC::XVMULSP, 1, 2, PPC::XVSUBSP},
    {PPC::FMADD, PPC::FADD, PPC::FMUL, 3, 1, PPC::FSUB},
    {PPC::FMADDS, PPC::FADDS, PPC::FMULS, 3, 1, PPC::FSUBS}};

// Check if an opcode is a FMA instruction. If it is, return the index in array
// FMAOpIdxInfo. Otherwise, return -1.
int16_t PPCInstrInfo::getFMAOpIdxInfo(unsigned Opcode) const {
  for (unsigned I = 0; I < std::size(FMAOpIdxInfo); I++)
    if (FMAOpIdxInfo[I][InfoArrayIdxFMAInst] == Opcode)
      return I;
  return -1;
}

// On PowerPC target, we have two kinds of patterns related to FMA:
// 1: Improve ILP.
// Try to reassociate FMA chains like below:
//
// Pattern 1:
//   A =  FADD X,  Y          (Leaf)
//   B =  FMA  A,  M21,  M22  (Prev)
//   C =  FMA  B,  M31,  M32  (Root)
// -->
//   A =  FMA  X,  M21,  M22
//   B =  FMA  Y,  M31,  M32
//   C =  FADD A,  B
//
// Pattern 2:
//   A =  FMA  X,  M11,  M12  (Leaf)
//   B =  FMA  A,  M21,  M22  (Prev)
//   C =  FMA  B,  M31,  M32  (Root)
// -->
//   A =  FMUL M11,  M12
//   B =  FMA  X,  M21,  M22
//   D =  FMA  A,  M31,  M32
//   C =  FADD B,  D
//
// breaking the dependency between A and B, allowing FMA to be executed in
// parallel (or back-to-back in a pipeline) instead of depending on each other.
//
// 2: Reduce register pressure.
// Try to reassociate FMA with FSUB and a constant like below:
// C is a floating point const.
//
// Pattern 1:
//   A = FSUB  X,  Y      (Leaf)
//   D = FMA   B,  C,  A  (Root)
// -->
//   A = FMA   B,  Y,  -C
//   D = FMA   A,  X,  C
//
// Pattern 2:
//   A = FSUB  X,  Y      (Leaf)
//   D = FMA   B,  A,  C  (Root)
// -->
//   A = FMA   B,  Y,  -C
//   D = FMA   A,  X,  C
//
//  Before the transformation, A must be assigned with different hardware
//  register with D. After the transformation, A and D must be assigned with
//  same hardware register due to TIE attribute of FMA instructions.
//
bool PPCInstrInfo::getFMAPatterns(MachineInstr &Root,
                                  SmallVectorImpl<unsigned> &Patterns,
                                  bool DoRegPressureReduce) const {
  MachineBasicBlock *MBB = Root.getParent();
  const MachineRegisterInfo *MRI = &MBB->getParent()->getRegInfo();
  const TargetRegisterInfo *TRI = &getRegisterInfo();

  auto IsAllOpsVirtualReg = [](const MachineInstr &Instr) {
    for (const auto &MO : Instr.explicit_operands())
      if (!(MO.isReg() && MO.getReg().isVirtual()))
        return false;
    return true;
  };

  auto IsReassociableAddOrSub = [&](const MachineInstr &Instr,
                                    unsigned OpType) {
    if (Instr.getOpcode() !=
        FMAOpIdxInfo[getFMAOpIdxInfo(Root.getOpcode())][OpType])
      return false;

    // Instruction can be reassociated.
    // fast math flags may prohibit reassociation.
    if (!(Instr.getFlag(MachineInstr::MIFlag::FmReassoc) &&
          Instr.getFlag(MachineInstr::MIFlag::FmNsz)))
      return false;

    // Instruction operands are virtual registers for reassociation.
    if (!IsAllOpsVirtualReg(Instr))
      return false;

    // For register pressure reassociation, the FSub must have only one use as
    // we want to delete the sub to save its def.
    if (OpType == InfoArrayIdxFSubInst &&
        !MRI->hasOneNonDBGUse(Instr.getOperand(0).getReg()))
      return false;

    return true;
  };

  auto IsReassociableFMA = [&](const MachineInstr &Instr, int16_t &AddOpIdx,
                               int16_t &MulOpIdx, bool IsLeaf) {
    int16_t Idx = getFMAOpIdxInfo(Instr.getOpcode());
    if (Idx < 0)
      return false;

    // Instruction can be reassociated.
    // fast math flags may prohibit reassociation.
    if (!(Instr.getFlag(MachineInstr::MIFlag::FmReassoc) &&
          Instr.getFlag(MachineInstr::MIFlag::FmNsz)))
      return false;

    // Instruction operands are virtual registers for reassociation.
    if (!IsAllOpsVirtualReg(Instr))
      return false;

    MulOpIdx = FMAOpIdxInfo[Idx][InfoArrayIdxMULOpIdx];
    if (IsLeaf)
      return true;

    AddOpIdx = FMAOpIdxInfo[Idx][InfoArrayIdxAddOpIdx];

    const MachineOperand &OpAdd = Instr.getOperand(AddOpIdx);
    MachineInstr *MIAdd = MRI->getUniqueVRegDef(OpAdd.getReg());
    // If 'add' operand's def is not in current block, don't do ILP related opt.
    if (!MIAdd || MIAdd->getParent() != MBB)
      return false;

    // If this is not Leaf FMA Instr, its 'add' operand should only have one use
    // as this fma will be changed later.
    return IsLeaf ? true : MRI->hasOneNonDBGUse(OpAdd.getReg());
  };

  int16_t AddOpIdx = -1;
  int16_t MulOpIdx = -1;

  bool IsUsedOnceL = false;
  bool IsUsedOnceR = false;
  MachineInstr *MULInstrL = nullptr;
  MachineInstr *MULInstrR = nullptr;

  auto IsRPReductionCandidate = [&]() {
    // Currently, we only support float and double.
    // FIXME: add support for other types.
    unsigned Opcode = Root.getOpcode();
    if (Opcode != PPC::XSMADDASP && Opcode != PPC::XSMADDADP)
      return false;

    // Root must be a valid FMA like instruction.
    // Treat it as leaf as we don't care its add operand.
    if (IsReassociableFMA(Root, AddOpIdx, MulOpIdx, true)) {
      assert((MulOpIdx >= 0) && "mul operand index not right!");
      Register MULRegL = TRI->lookThruSingleUseCopyChain(
          Root.getOperand(MulOpIdx).getReg(), MRI);
      Register MULRegR = TRI->lookThruSingleUseCopyChain(
          Root.getOperand(MulOpIdx + 1).getReg(), MRI);
      if (!MULRegL && !MULRegR)
        return false;

      if (MULRegL && !MULRegR) {
        MULRegR =
            TRI->lookThruCopyLike(Root.getOperand(MulOpIdx + 1).getReg(), MRI);
        IsUsedOnceL = true;
      } else if (!MULRegL && MULRegR) {
        MULRegL =
            TRI->lookThruCopyLike(Root.getOperand(MulOpIdx).getReg(), MRI);
        IsUsedOnceR = true;
      } else {
        IsUsedOnceL = true;
        IsUsedOnceR = true;
      }

      if (!MULRegL.isVirtual() || !MULRegR.isVirtual())
        return false;

      MULInstrL = MRI->getVRegDef(MULRegL);
      MULInstrR = MRI->getVRegDef(MULRegR);
      return true;
    }
    return false;
  };

  // Register pressure fma reassociation patterns.
  if (DoRegPressureReduce && IsRPReductionCandidate()) {
    assert((MULInstrL && MULInstrR) && "wrong register preduction candidate!");
    // Register pressure pattern 1
    if (isLoadFromConstantPool(MULInstrL) && IsUsedOnceR &&
        IsReassociableAddOrSub(*MULInstrR, InfoArrayIdxFSubInst)) {
      LLVM_DEBUG(dbgs() << "add pattern REASSOC_XY_BCA\n");
      Patterns.push_back(PPCMachineCombinerPattern::REASSOC_XY_BCA);
      return true;
    }

    // Register pressure pattern 2
    if ((isLoadFromConstantPool(MULInstrR) && IsUsedOnceL &&
         IsReassociableAddOrSub(*MULInstrL, InfoArrayIdxFSubInst))) {
      LLVM_DEBUG(dbgs() << "add pattern REASSOC_XY_BAC\n");
      Patterns.push_back(PPCMachineCombinerPattern::REASSOC_XY_BAC);
      return true;
    }
  }

  // ILP fma reassociation patterns.
  // Root must be a valid FMA like instruction.
  AddOpIdx = -1;
  if (!IsReassociableFMA(Root, AddOpIdx, MulOpIdx, false))
    return false;

  assert((AddOpIdx >= 0) && "add operand index not right!");

  Register RegB = Root.getOperand(AddOpIdx).getReg();
  MachineInstr *Prev = MRI->getUniqueVRegDef(RegB);

  // Prev must be a valid FMA like instruction.
  AddOpIdx = -1;
  if (!IsReassociableFMA(*Prev, AddOpIdx, MulOpIdx, false))
    return false;

  assert((AddOpIdx >= 0) && "add operand index not right!");

  Register RegA = Prev->getOperand(AddOpIdx).getReg();
  MachineInstr *Leaf = MRI->getUniqueVRegDef(RegA);
  AddOpIdx = -1;
  if (IsReassociableFMA(*Leaf, AddOpIdx, MulOpIdx, true)) {
    Patterns.push_back(PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM);
    LLVM_DEBUG(dbgs() << "add pattern REASSOC_XMM_AMM_BMM\n");
    return true;
  }
  if (IsReassociableAddOrSub(*Leaf, InfoArrayIdxFAddInst)) {
    Patterns.push_back(PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM);
    LLVM_DEBUG(dbgs() << "add pattern REASSOC_XY_AMM_BMM\n");
    return true;
  }
  return false;
}

void PPCInstrInfo::finalizeInsInstrs(
    MachineInstr &Root, unsigned &Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs) const {
  assert(!InsInstrs.empty() && "Instructions set to be inserted is empty!");

  MachineFunction *MF = Root.getMF();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  MachineConstantPool *MCP = MF->getConstantPool();

  int16_t Idx = getFMAOpIdxInfo(Root.getOpcode());
  if (Idx < 0)
    return;

  uint16_t FirstMulOpIdx = FMAOpIdxInfo[Idx][InfoArrayIdxMULOpIdx];

  // For now we only need to fix up placeholder for register pressure reduce
  // patterns.
  Register ConstReg = 0;
  switch (Pattern) {
  case PPCMachineCombinerPattern::REASSOC_XY_BCA:
    ConstReg =
        TRI->lookThruCopyLike(Root.getOperand(FirstMulOpIdx).getReg(), MRI);
    break;
  case PPCMachineCombinerPattern::REASSOC_XY_BAC:
    ConstReg =
        TRI->lookThruCopyLike(Root.getOperand(FirstMulOpIdx + 1).getReg(), MRI);
    break;
  default:
    // Not register pressure reduce patterns.
    return;
  }

  MachineInstr *ConstDefInstr = MRI->getVRegDef(ConstReg);
  // Get const value from const pool.
  const Constant *C = getConstantFromConstantPool(ConstDefInstr);
  assert(isa<llvm::ConstantFP>(C) && "not a valid constant!");

  // Get negative fp const.
  APFloat F1((dyn_cast<ConstantFP>(C))->getValueAPF());
  F1.changeSign();
  Constant *NegC = ConstantFP::get(dyn_cast<ConstantFP>(C)->getContext(), F1);
  Align Alignment = MF->getDataLayout().getPrefTypeAlign(C->getType());

  // Put negative fp const into constant pool.
  unsigned ConstPoolIdx = MCP->getConstantPoolIndex(NegC, Alignment);

  MachineOperand *Placeholder = nullptr;
  // Record the placeholder PPC::ZERO8 we add in reassociateFMA.
  for (auto *Inst : InsInstrs) {
    for (MachineOperand &Operand : Inst->explicit_operands()) {
      assert(Operand.isReg() && "Invalid instruction in InsInstrs!");
      if (Operand.getReg() == PPC::ZERO8) {
        Placeholder = &Operand;
        break;
      }
    }
  }

  assert(Placeholder && "Placeholder does not exist!");

  // Generate instructions to load the const fp from constant pool.
  // We only support PPC64 and medium code model.
  Register LoadNewConst =
      generateLoadForNewConst(ConstPoolIdx, &Root, C->getType(), InsInstrs);

  // Fill the placeholder with the new load from constant pool.
  Placeholder->setReg(LoadNewConst);
}

bool PPCInstrInfo::shouldReduceRegisterPressure(
    const MachineBasicBlock *MBB, const RegisterClassInfo *RegClassInfo) const {

  if (!EnableFMARegPressureReduction)
    return false;

  // Currently, we only enable register pressure reducing in machine combiner
  // for: 1: PPC64; 2: Code Model is Medium; 3: Power9 which also has vector
  // support.
  //
  // So we need following instructions to access a TOC entry:
  //
  // %6:g8rc_and_g8rc_nox0 = ADDIStocHA8 $x2, %const.0
  // %7:vssrc = DFLOADf32 target-flags(ppc-toc-lo) %const.0,
  //   killed %6:g8rc_and_g8rc_nox0, implicit $x2 :: (load 4 from constant-pool)
  //
  // FIXME: add more supported targets, like Small and Large code model, PPC32,
  // AIX.
  if (!(Subtarget.isPPC64() && Subtarget.hasP9Vector() &&
        Subtarget.getTargetMachine().getCodeModel() == CodeModel::Medium))
    return false;

  const TargetRegisterInfo *TRI = &getRegisterInfo();
  const MachineFunction *MF = MBB->getParent();
  const MachineRegisterInfo *MRI = &MF->getRegInfo();

  auto GetMBBPressure =
      [&](const MachineBasicBlock *MBB) -> std::vector<unsigned> {
    RegionPressure Pressure;
    RegPressureTracker RPTracker(Pressure);

    // Initialize the register pressure tracker.
    RPTracker.init(MBB->getParent(), RegClassInfo, nullptr, MBB, MBB->end(),
                   /*TrackLaneMasks*/ false, /*TrackUntiedDefs=*/true);

    for (const auto &MI : reverse(*MBB)) {
      if (MI.isDebugValue() || MI.isDebugLabel())
        continue;
      RegisterOperands RegOpers;
      RegOpers.collect(MI, *TRI, *MRI, false, false);
      RPTracker.recedeSkipDebugValues();
      assert(&*RPTracker.getPos() == &MI && "RPTracker sync error!");
      RPTracker.recede(RegOpers);
    }

    // Close the RPTracker to finalize live ins.
    RPTracker.closeRegion();

    return RPTracker.getPressure().MaxSetPressure;
  };

  // For now we only care about float and double type fma.
  unsigned VSSRCLimit = TRI->getRegPressureSetLimit(
      *MBB->getParent(), PPC::RegisterPressureSets::VSSRC);

  // Only reduce register pressure when pressure is high.
  return GetMBBPressure(MBB)[PPC::RegisterPressureSets::VSSRC] >
         (float)VSSRCLimit * FMARPFactor;
}

bool PPCInstrInfo::isLoadFromConstantPool(MachineInstr *I) const {
  // I has only one memory operand which is load from constant pool.
  if (!I->hasOneMemOperand())
    return false;

  MachineMemOperand *Op = I->memoperands()[0];
  return Op->isLoad() && Op->getPseudoValue() &&
         Op->getPseudoValue()->kind() == PseudoSourceValue::ConstantPool;
}

Register PPCInstrInfo::generateLoadForNewConst(
    unsigned Idx, MachineInstr *MI, Type *Ty,
    SmallVectorImpl<MachineInstr *> &InsInstrs) const {
  // Now we only support PPC64, Medium code model and P9 with vector.
  // We have immutable pattern to access const pool. See function
  // shouldReduceRegisterPressure.
  assert((Subtarget.isPPC64() && Subtarget.hasP9Vector() &&
          Subtarget.getTargetMachine().getCodeModel() == CodeModel::Medium) &&
         "Target not supported!\n");

  MachineFunction *MF = MI->getMF();
  MachineRegisterInfo *MRI = &MF->getRegInfo();

  // Generate ADDIStocHA8
  Register VReg1 = MRI->createVirtualRegister(&PPC::G8RC_and_G8RC_NOX0RegClass);
  MachineInstrBuilder TOCOffset =
      BuildMI(*MF, MI->getDebugLoc(), get(PPC::ADDIStocHA8), VReg1)
          .addReg(PPC::X2)
          .addConstantPoolIndex(Idx);

  assert((Ty->isFloatTy() || Ty->isDoubleTy()) &&
         "Only float and double are supported!");

  unsigned LoadOpcode;
  // Should be float type or double type.
  if (Ty->isFloatTy())
    LoadOpcode = PPC::DFLOADf32;
  else
    LoadOpcode = PPC::DFLOADf64;

  const TargetRegisterClass *RC = MRI->getRegClass(MI->getOperand(0).getReg());
  Register VReg2 = MRI->createVirtualRegister(RC);
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MachinePointerInfo::getConstantPool(*MF), MachineMemOperand::MOLoad,
      Ty->getScalarSizeInBits() / 8, MF->getDataLayout().getPrefTypeAlign(Ty));

  // Generate Load from constant pool.
  MachineInstrBuilder Load =
      BuildMI(*MF, MI->getDebugLoc(), get(LoadOpcode), VReg2)
          .addConstantPoolIndex(Idx)
          .addReg(VReg1, getKillRegState(true))
          .addMemOperand(MMO);

  Load->getOperand(1).setTargetFlags(PPCII::MO_TOC_LO);

  // Insert the toc load instructions into InsInstrs.
  InsInstrs.insert(InsInstrs.begin(), Load);
  InsInstrs.insert(InsInstrs.begin(), TOCOffset);
  return VReg2;
}

// This function returns the const value in constant pool if the \p I is a load
// from constant pool.
const Constant *
PPCInstrInfo::getConstantFromConstantPool(MachineInstr *I) const {
  MachineFunction *MF = I->getMF();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  MachineConstantPool *MCP = MF->getConstantPool();
  assert(I->mayLoad() && "Should be a load instruction.\n");
  for (auto MO : I->uses()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0 || !Reg.isVirtual())
      continue;
    // Find the toc address.
    MachineInstr *DefMI = MRI->getVRegDef(Reg);
    for (auto MO2 : DefMI->uses())
      if (MO2.isCPI())
        return (MCP->getConstants())[MO2.getIndex()].Val.ConstVal;
  }
  return nullptr;
}

CombinerObjective PPCInstrInfo::getCombinerObjective(unsigned Pattern) const {
  switch (Pattern) {
  case PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM:
  case PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM:
    return CombinerObjective::MustReduceDepth;
  case PPCMachineCombinerPattern::REASSOC_XY_BCA:
  case PPCMachineCombinerPattern::REASSOC_XY_BAC:
    return CombinerObjective::MustReduceRegisterPressure;
  default:
    return TargetInstrInfo::getCombinerObjective(Pattern);
  }
}

bool PPCInstrInfo::getMachineCombinerPatterns(
    MachineInstr &Root, SmallVectorImpl<unsigned> &Patterns,
    bool DoRegPressureReduce) const {
  // Using the machine combiner in this way is potentially expensive, so
  // restrict to when aggressive optimizations are desired.
  if (Subtarget.getTargetMachine().getOptLevel() != CodeGenOptLevel::Aggressive)
    return false;

  if (getFMAPatterns(Root, Patterns, DoRegPressureReduce))
    return true;

  return TargetInstrInfo::getMachineCombinerPatterns(Root, Patterns,
                                                     DoRegPressureReduce);
}

void PPCInstrInfo::genAlternativeCodeSequence(
    MachineInstr &Root, unsigned Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const {
  switch (Pattern) {
  case PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM:
  case PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM:
  case PPCMachineCombinerPattern::REASSOC_XY_BCA:
  case PPCMachineCombinerPattern::REASSOC_XY_BAC:
    reassociateFMA(Root, Pattern, InsInstrs, DelInstrs, InstrIdxForVirtReg);
    break;
  default:
    // Reassociate default patterns.
    TargetInstrInfo::genAlternativeCodeSequence(Root, Pattern, InsInstrs,
                                                DelInstrs, InstrIdxForVirtReg);
    break;
  }
}

void PPCInstrInfo::reassociateFMA(
    MachineInstr &Root, unsigned Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const {
  MachineFunction *MF = Root.getMF();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  MachineOperand &OpC = Root.getOperand(0);
  Register RegC = OpC.getReg();
  const TargetRegisterClass *RC = MRI.getRegClass(RegC);
  MRI.constrainRegClass(RegC, RC);

  unsigned FmaOp = Root.getOpcode();
  int16_t Idx = getFMAOpIdxInfo(FmaOp);
  assert(Idx >= 0 && "Root must be a FMA instruction");

  bool IsILPReassociate =
      (Pattern == PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM) ||
      (Pattern == PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM);

  uint16_t AddOpIdx = FMAOpIdxInfo[Idx][InfoArrayIdxAddOpIdx];
  uint16_t FirstMulOpIdx = FMAOpIdxInfo[Idx][InfoArrayIdxMULOpIdx];

  MachineInstr *Prev = nullptr;
  MachineInstr *Leaf = nullptr;
  switch (Pattern) {
  default:
    llvm_unreachable("not recognized pattern!");
  case PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM:
  case PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM:
    Prev = MRI.getUniqueVRegDef(Root.getOperand(AddOpIdx).getReg());
    Leaf = MRI.getUniqueVRegDef(Prev->getOperand(AddOpIdx).getReg());
    break;
  case PPCMachineCombinerPattern::REASSOC_XY_BAC: {
    Register MULReg =
        TRI->lookThruCopyLike(Root.getOperand(FirstMulOpIdx).getReg(), &MRI);
    Leaf = MRI.getVRegDef(MULReg);
    break;
  }
  case PPCMachineCombinerPattern::REASSOC_XY_BCA: {
    Register MULReg = TRI->lookThruCopyLike(
        Root.getOperand(FirstMulOpIdx + 1).getReg(), &MRI);
    Leaf = MRI.getVRegDef(MULReg);
    break;
  }
  }

  uint32_t IntersectedFlags = 0;
  if (IsILPReassociate)
    IntersectedFlags = Root.getFlags() & Prev->getFlags() & Leaf->getFlags();
  else
    IntersectedFlags = Root.getFlags() & Leaf->getFlags();

  auto GetOperandInfo = [&](const MachineOperand &Operand, Register &Reg,
                            bool &KillFlag) {
    Reg = Operand.getReg();
    MRI.constrainRegClass(Reg, RC);
    KillFlag = Operand.isKill();
  };

  auto GetFMAInstrInfo = [&](const MachineInstr &Instr, Register &MulOp1,
                             Register &MulOp2, Register &AddOp,
                             bool &MulOp1KillFlag, bool &MulOp2KillFlag,
                             bool &AddOpKillFlag) {
    GetOperandInfo(Instr.getOperand(FirstMulOpIdx), MulOp1, MulOp1KillFlag);
    GetOperandInfo(Instr.getOperand(FirstMulOpIdx + 1), MulOp2, MulOp2KillFlag);
    GetOperandInfo(Instr.getOperand(AddOpIdx), AddOp, AddOpKillFlag);
  };

  Register RegM11, RegM12, RegX, RegY, RegM21, RegM22, RegM31, RegM32, RegA11,
      RegA21, RegB;
  bool KillX = false, KillY = false, KillM11 = false, KillM12 = false,
       KillM21 = false, KillM22 = false, KillM31 = false, KillM32 = false,
       KillA11 = false, KillA21 = false, KillB = false;

  GetFMAInstrInfo(Root, RegM31, RegM32, RegB, KillM31, KillM32, KillB);

  if (IsILPReassociate)
    GetFMAInstrInfo(*Prev, RegM21, RegM22, RegA21, KillM21, KillM22, KillA21);

  if (Pattern == PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM) {
    GetFMAInstrInfo(*Leaf, RegM11, RegM12, RegA11, KillM11, KillM12, KillA11);
    GetOperandInfo(Leaf->getOperand(AddOpIdx), RegX, KillX);
  } else if (Pattern == PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM) {
    GetOperandInfo(Leaf->getOperand(1), RegX, KillX);
    GetOperandInfo(Leaf->getOperand(2), RegY, KillY);
  } else {
    // Get FSUB instruction info.
    GetOperandInfo(Leaf->getOperand(1), RegX, KillX);
    GetOperandInfo(Leaf->getOperand(2), RegY, KillY);
  }

  // Create new virtual registers for the new results instead of
  // recycling legacy ones because the MachineCombiner's computation of the
  // critical path requires a new register definition rather than an existing
  // one.
  // For register pressure reassociation, we only need create one virtual
  // register for the new fma.
  Register NewVRA = MRI.createVirtualRegister(RC);
  InstrIdxForVirtReg.insert(std::make_pair(NewVRA, 0));

  Register NewVRB = 0;
  if (IsILPReassociate) {
    NewVRB = MRI.createVirtualRegister(RC);
    InstrIdxForVirtReg.insert(std::make_pair(NewVRB, 1));
  }

  Register NewVRD = 0;
  if (Pattern == PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM) {
    NewVRD = MRI.createVirtualRegister(RC);
    InstrIdxForVirtReg.insert(std::make_pair(NewVRD, 2));
  }

  auto AdjustOperandOrder = [&](MachineInstr *MI, Register RegAdd, bool KillAdd,
                                Register RegMul1, bool KillRegMul1,
                                Register RegMul2, bool KillRegMul2) {
    MI->getOperand(AddOpIdx).setReg(RegAdd);
    MI->getOperand(AddOpIdx).setIsKill(KillAdd);
    MI->getOperand(FirstMulOpIdx).setReg(RegMul1);
    MI->getOperand(FirstMulOpIdx).setIsKill(KillRegMul1);
    MI->getOperand(FirstMulOpIdx + 1).setReg(RegMul2);
    MI->getOperand(FirstMulOpIdx + 1).setIsKill(KillRegMul2);
  };

  MachineInstrBuilder NewARegPressure, NewCRegPressure;
  switch (Pattern) {
  default:
    llvm_unreachable("not recognized pattern!");
  case PPCMachineCombinerPattern::REASSOC_XY_AMM_BMM: {
    // Create new instructions for insertion.
    MachineInstrBuilder MINewB =
        BuildMI(*MF, Prev->getDebugLoc(), get(FmaOp), NewVRB)
            .addReg(RegX, getKillRegState(KillX))
            .addReg(RegM21, getKillRegState(KillM21))
            .addReg(RegM22, getKillRegState(KillM22));
    MachineInstrBuilder MINewA =
        BuildMI(*MF, Root.getDebugLoc(), get(FmaOp), NewVRA)
            .addReg(RegY, getKillRegState(KillY))
            .addReg(RegM31, getKillRegState(KillM31))
            .addReg(RegM32, getKillRegState(KillM32));
    // If AddOpIdx is not 1, adjust the order.
    if (AddOpIdx != 1) {
      AdjustOperandOrder(MINewB, RegX, KillX, RegM21, KillM21, RegM22, KillM22);
      AdjustOperandOrder(MINewA, RegY, KillY, RegM31, KillM31, RegM32, KillM32);
    }

    MachineInstrBuilder MINewC =
        BuildMI(*MF, Root.getDebugLoc(),
                get(FMAOpIdxInfo[Idx][InfoArrayIdxFAddInst]), RegC)
            .addReg(NewVRB, getKillRegState(true))
            .addReg(NewVRA, getKillRegState(true));

    // Update flags for newly created instructions.
    setSpecialOperandAttr(*MINewA, IntersectedFlags);
    setSpecialOperandAttr(*MINewB, IntersectedFlags);
    setSpecialOperandAttr(*MINewC, IntersectedFlags);

    // Record new instructions for insertion.
    InsInstrs.push_back(MINewA);
    InsInstrs.push_back(MINewB);
    InsInstrs.push_back(MINewC);
    break;
  }
  case PPCMachineCombinerPattern::REASSOC_XMM_AMM_BMM: {
    assert(NewVRD && "new FMA register not created!");
    // Create new instructions for insertion.
    MachineInstrBuilder MINewA =
        BuildMI(*MF, Leaf->getDebugLoc(),
                get(FMAOpIdxInfo[Idx][InfoArrayIdxFMULInst]), NewVRA)
            .addReg(RegM11, getKillRegState(KillM11))
            .addReg(RegM12, getKillRegState(KillM12));
    MachineInstrBuilder MINewB =
        BuildMI(*MF, Prev->getDebugLoc(), get(FmaOp), NewVRB)
            .addReg(RegX, getKillRegState(KillX))
            .addReg(RegM21, getKillRegState(KillM21))
            .addReg(RegM22, getKillRegState(KillM22));
    MachineInstrBuilder MINewD =
        BuildMI(*MF, Root.getDebugLoc(), get(FmaOp), NewVRD)
            .addReg(NewVRA, getKillRegState(true))
            .addReg(RegM31, getKillRegState(KillM31))
            .addReg(RegM32, getKillRegState(KillM32));
    // If AddOpIdx is not 1, adjust the order.
    if (AddOpIdx != 1) {
      AdjustOperandOrder(MINewB, RegX, KillX, RegM21, KillM21, RegM22, KillM22);
      AdjustOperandOrder(MINewD, NewVRA, true, RegM31, KillM31, RegM32,
                         KillM32);
    }

    MachineInstrBuilder MINewC =
        BuildMI(*MF, Root.getDebugLoc(),
                get(FMAOpIdxInfo[Idx][InfoArrayIdxFAddInst]), RegC)
            .addReg(NewVRB, getKillRegState(true))
            .addReg(NewVRD, getKillRegState(true));

    // Update flags for newly created instructions.
    setSpecialOperandAttr(*MINewA, IntersectedFlags);
    setSpecialOperandAttr(*MINewB, IntersectedFlags);
    setSpecialOperandAttr(*MINewD, IntersectedFlags);
    setSpecialOperandAttr(*MINewC, IntersectedFlags);

    // Record new instructions for insertion.
    InsInstrs.push_back(MINewA);
    InsInstrs.push_back(MINewB);
    InsInstrs.push_back(MINewD);
    InsInstrs.push_back(MINewC);
    break;
  }
  case PPCMachineCombinerPattern::REASSOC_XY_BAC:
  case PPCMachineCombinerPattern::REASSOC_XY_BCA: {
    Register VarReg;
    bool KillVarReg = false;
    if (Pattern == PPCMachineCombinerPattern::REASSOC_XY_BCA) {
      VarReg = RegM31;
      KillVarReg = KillM31;
    } else {
      VarReg = RegM32;
      KillVarReg = KillM32;
    }
    // We don't want to get negative const from memory pool too early, as the
    // created entry will not be deleted even if it has no users. Since all
    // operand of Leaf and Root are virtual register, we use zero register
    // here as a placeholder. When the InsInstrs is selected in
    // MachineCombiner, we call finalizeInsInstrs to replace the zero register
    // with a virtual register which is a load from constant pool.
    NewARegPressure = BuildMI(*MF, Root.getDebugLoc(), get(FmaOp), NewVRA)
                          .addReg(RegB, getKillRegState(RegB))
                          .addReg(RegY, getKillRegState(KillY))
                          .addReg(PPC::ZERO8);
    NewCRegPressure = BuildMI(*MF, Root.getDebugLoc(), get(FmaOp), RegC)
                          .addReg(NewVRA, getKillRegState(true))
                          .addReg(RegX, getKillRegState(KillX))
                          .addReg(VarReg, getKillRegState(KillVarReg));
    // For now, we only support xsmaddadp/xsmaddasp, their add operand are
    // both at index 1, no need to adjust.
    // FIXME: when add more fma instructions support, like fma/fmas, adjust
    // the operand index here.
    break;
  }
  }

  if (!IsILPReassociate) {
    setSpecialOperandAttr(*NewARegPressure, IntersectedFlags);
    setSpecialOperandAttr(*NewCRegPressure, IntersectedFlags);

    InsInstrs.push_back(NewARegPressure);
    InsInstrs.push_back(NewCRegPressure);
  }

  assert(!InsInstrs.empty() &&
         "Insertion instructions set should not be empty!");

  // Record old instructions for deletion.
  DelInstrs.push_back(Leaf);
  if (IsILPReassociate)
    DelInstrs.push_back(Prev);
  DelInstrs.push_back(&Root);
}

// Detect 32 -> 64-bit extensions where we may reuse the low sub-register.
bool PPCInstrInfo::isCoalescableExtInstr(const MachineInstr &MI,
                                         Register &SrcReg, Register &DstReg,
                                         unsigned &SubIdx) const {
  switch (MI.getOpcode()) {
  default: return false;
  case PPC::EXTSW:
  case PPC::EXTSW_32:
  case PPC::EXTSW_32_64:
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    SubIdx = PPC::sub_32;
    return true;
  }
}

Register PPCInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  if (llvm::is_contained(getLoadOpcodesForSpillArray(), MI.getOpcode())) {
    // Check for the operands added by addFrameReference (the immediate is the
    // offset which defaults to 0).
    if (MI.getOperand(1).isImm() && !MI.getOperand(1).getImm() &&
        MI.getOperand(2).isFI()) {
      FrameIndex = MI.getOperand(2).getIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

// For opcodes with the ReMaterializable flag set, this function is called to
// verify the instruction is really rematable.
bool PPCInstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    // Let base implementaion decide.
    break;
  case PPC::LI:
  case PPC::LI8:
  case PPC::PLI:
  case PPC::PLI8:
  case PPC::LIS:
  case PPC::LIS8:
  case PPC::ADDIStocHA:
  case PPC::ADDIStocHA8:
  case PPC::ADDItocL:
  case PPC::ADDItocL8:
  case PPC::LOAD_STACK_GUARD:
  case PPC::PPCLdFixedAddr:
  case PPC::XXLXORz:
  case PPC::XXLXORspz:
  case PPC::XXLXORdpz:
  case PPC::XXLEQVOnes:
  case PPC::XXSPLTI32DX:
  case PPC::XXSPLTIW:
  case PPC::XXSPLTIDP:
  case PPC::V_SET0B:
  case PPC::V_SET0H:
  case PPC::V_SET0:
  case PPC::V_SETALLONESB:
  case PPC::V_SETALLONESH:
  case PPC::V_SETALLONES:
  case PPC::CRSET:
  case PPC::CRUNSET:
  case PPC::XXSETACCZ:
  case PPC::XXSETACCZW:
    return true;
  }
  return TargetInstrInfo::isReallyTriviallyReMaterializable(MI);
}

Register PPCInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  if (llvm::is_contained(getStoreOpcodesForSpillArray(), MI.getOpcode())) {
    if (MI.getOperand(1).isImm() && !MI.getOperand(1).getImm() &&
        MI.getOperand(2).isFI()) {
      FrameIndex = MI.getOperand(2).getIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

MachineInstr *PPCInstrInfo::commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                                   unsigned OpIdx1,
                                                   unsigned OpIdx2) const {
  MachineFunction &MF = *MI.getParent()->getParent();

  // Normal instructions can be commuted the obvious way.
  if (MI.getOpcode() != PPC::RLWIMI && MI.getOpcode() != PPC::RLWIMI_rec)
    return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
  // Note that RLWIMI can be commuted as a 32-bit instruction, but not as a
  // 64-bit instruction (so we don't handle PPC::RLWIMI8 here), because
  // changing the relative order of the mask operands might change what happens
  // to the high-bits of the mask (and, thus, the result).

  // Cannot commute if it has a non-zero rotate count.
  if (MI.getOperand(3).getImm() != 0)
    return nullptr;

  // If we have a zero rotate count, we have:
  //   M = mask(MB,ME)
  //   Op0 = (Op1 & ~M) | (Op2 & M)
  // Change this to:
  //   M = mask((ME+1)&31, (MB-1)&31)
  //   Op0 = (Op2 & ~M) | (Op1 & M)

  // Swap op1/op2
  assert(((OpIdx1 == 1 && OpIdx2 == 2) || (OpIdx1 == 2 && OpIdx2 == 1)) &&
         "Only the operands 1 and 2 can be swapped in RLSIMI/RLWIMI_rec.");
  Register Reg0 = MI.getOperand(0).getReg();
  Register Reg1 = MI.getOperand(1).getReg();
  Register Reg2 = MI.getOperand(2).getReg();
  unsigned SubReg1 = MI.getOperand(1).getSubReg();
  unsigned SubReg2 = MI.getOperand(2).getSubReg();
  bool Reg1IsKill = MI.getOperand(1).isKill();
  bool Reg2IsKill = MI.getOperand(2).isKill();
  bool ChangeReg0 = false;
  // If machine instrs are no longer in two-address forms, update
  // destination register as well.
  if (Reg0 == Reg1) {
    // Must be two address instruction (i.e. op1 is tied to op0).
    assert(MI.getDesc().getOperandConstraint(1, MCOI::TIED_TO) == 0 &&
           "Expecting a two-address instruction!");
    assert(MI.getOperand(0).getSubReg() == SubReg1 && "Tied subreg mismatch");
    Reg2IsKill = false;
    ChangeReg0 = true;
  }

  // Masks.
  unsigned MB = MI.getOperand(4).getImm();
  unsigned ME = MI.getOperand(5).getImm();

  // We can't commute a trivial mask (there is no way to represent an all-zero
  // mask).
  if (MB == 0 && ME == 31)
    return nullptr;

  if (NewMI) {
    // Create a new instruction.
    Register Reg0 = ChangeReg0 ? Reg2 : MI.getOperand(0).getReg();
    bool Reg0IsDead = MI.getOperand(0).isDead();
    return BuildMI(MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(Reg0, RegState::Define | getDeadRegState(Reg0IsDead))
        .addReg(Reg2, getKillRegState(Reg2IsKill))
        .addReg(Reg1, getKillRegState(Reg1IsKill))
        .addImm((ME + 1) & 31)
        .addImm((MB - 1) & 31);
  }

  if (ChangeReg0) {
    MI.getOperand(0).setReg(Reg2);
    MI.getOperand(0).setSubReg(SubReg2);
  }
  MI.getOperand(2).setReg(Reg1);
  MI.getOperand(1).setReg(Reg2);
  MI.getOperand(2).setSubReg(SubReg1);
  MI.getOperand(1).setSubReg(SubReg2);
  MI.getOperand(2).setIsKill(Reg1IsKill);
  MI.getOperand(1).setIsKill(Reg2IsKill);

  // Swap the mask around.
  MI.getOperand(4).setImm((ME + 1) & 31);
  MI.getOperand(5).setImm((MB - 1) & 31);
  return &MI;
}

bool PPCInstrInfo::findCommutedOpIndices(const MachineInstr &MI,
                                         unsigned &SrcOpIdx1,
                                         unsigned &SrcOpIdx2) const {
  // For VSX A-Type FMA instructions, it is the first two operands that can be
  // commuted, however, because the non-encoded tied input operand is listed
  // first, the operands to swap are actually the second and third.

  int AltOpc = PPC::getAltVSXFMAOpcode(MI.getOpcode());
  if (AltOpc == -1)
    return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);

  // The commutable operand indices are 2 and 3. Return them in SrcOpIdx1
  // and SrcOpIdx2.
  return fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, 2, 3);
}

void PPCInstrInfo::insertNoop(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI) const {
  // This function is used for scheduling, and the nop wanted here is the type
  // that terminates dispatch groups on the POWER cores.
  unsigned Directive = Subtarget.getCPUDirective();
  unsigned Opcode;
  switch (Directive) {
  default:            Opcode = PPC::NOP; break;
  case PPC::DIR_PWR6: Opcode = PPC::NOP_GT_PWR6; break;
  case PPC::DIR_PWR7: Opcode = PPC::NOP_GT_PWR7; break;
  case PPC::DIR_PWR8: Opcode = PPC::NOP_GT_PWR7; break; /* FIXME: Update when P8 InstrScheduling model is ready */
  // FIXME: Update when POWER9 scheduling model is ready.
  case PPC::DIR_PWR9: Opcode = PPC::NOP_GT_PWR7; break;
  }

  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(Opcode));
}

/// Return the noop instruction to use for a noop.
MCInst PPCInstrInfo::getNop() const {
  MCInst Nop;
  Nop.setOpcode(PPC::NOP);
  return Nop;
}

// Branch analysis.
// Note: If the condition register is set to CTR or CTR8 then this is a
// BDNZ (imm == 1) or BDZ (imm == 0) branch.
bool PPCInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  bool isPPC64 = Subtarget.isPPC64();

  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;

  if (!isUnpredicatedTerminator(*I))
    return false;

  if (AllowModify) {
    // If the BB ends with an unconditional branch to the fallthrough BB,
    // we eliminate the branch instruction.
    if (I->getOpcode() == PPC::B &&
        MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
      I->eraseFromParent();

      // We update iterator after deleting the last branch.
      I = MBB.getLastNonDebugInstr();
      if (I == MBB.end() || !isUnpredicatedTerminator(*I))
        return false;
    }
  }

  // Get the last instruction in the block.
  MachineInstr &LastInst = *I;

  // If there is only one terminator instruction, process it.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (LastInst.getOpcode() == PPC::B) {
      if (!LastInst.getOperand(0).isMBB())
        return true;
      TBB = LastInst.getOperand(0).getMBB();
      return false;
    } else if (LastInst.getOpcode() == PPC::BCC) {
      if (!LastInst.getOperand(2).isMBB())
        return true;
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(2).getMBB();
      Cond.push_back(LastInst.getOperand(0));
      Cond.push_back(LastInst.getOperand(1));
      return false;
    } else if (LastInst.getOpcode() == PPC::BC) {
      if (!LastInst.getOperand(1).isMBB())
        return true;
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(1).getMBB();
      Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_SET));
      Cond.push_back(LastInst.getOperand(0));
      return false;
    } else if (LastInst.getOpcode() == PPC::BCn) {
      if (!LastInst.getOperand(1).isMBB())
        return true;
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(1).getMBB();
      Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_UNSET));
      Cond.push_back(LastInst.getOperand(0));
      return false;
    } else if (LastInst.getOpcode() == PPC::BDNZ8 ||
               LastInst.getOpcode() == PPC::BDNZ) {
      if (!LastInst.getOperand(0).isMBB())
        return true;
      if (DisableCTRLoopAnal)
        return true;
      TBB = LastInst.getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(1));
      Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                               true));
      return false;
    } else if (LastInst.getOpcode() == PPC::BDZ8 ||
               LastInst.getOpcode() == PPC::BDZ) {
      if (!LastInst.getOperand(0).isMBB())
        return true;
      if (DisableCTRLoopAnal)
        return true;
      TBB = LastInst.getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(0));
      Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                               true));
      return false;
    }

    // Otherwise, don't know what this is.
    return true;
  }

  // Get the instruction before it if it's a terminator.
  MachineInstr &SecondLastInst = *I;

  // If there are three terminators, we don't know what sort of block this is.
  if (I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  // If the block ends with PPC::B and PPC:BCC, handle it.
  if (SecondLastInst.getOpcode() == PPC::BCC &&
      LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(2).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(2).getMBB();
    Cond.push_back(SecondLastInst.getOperand(0));
    Cond.push_back(SecondLastInst.getOperand(1));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if (SecondLastInst.getOpcode() == PPC::BC &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(1).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_SET));
    Cond.push_back(SecondLastInst.getOperand(0));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if (SecondLastInst.getOpcode() == PPC::BCn &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(1).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_UNSET));
    Cond.push_back(SecondLastInst.getOperand(0));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if ((SecondLastInst.getOpcode() == PPC::BDNZ8 ||
              SecondLastInst.getOpcode() == PPC::BDNZ) &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(0).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    if (DisableCTRLoopAnal)
      return true;
    TBB = SecondLastInst.getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(1));
    Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                             true));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if ((SecondLastInst.getOpcode() == PPC::BDZ8 ||
              SecondLastInst.getOpcode() == PPC::BDZ) &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(0).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    if (DisableCTRLoopAnal)
      return true;
    TBB = SecondLastInst.getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(0));
    Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                             true));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  }

  // If the block ends with two PPC:Bs, handle it.  The second one is not
  // executed, so remove it.
  if (SecondLastInst.getOpcode() == PPC::B && LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(0).getMBB();
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // Otherwise, can't handle this.
  return true;
}

unsigned PPCInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (I->getOpcode() != PPC::B && I->getOpcode() != PPC::BCC &&
      I->getOpcode() != PPC::BC && I->getOpcode() != PPC::BCn &&
      I->getOpcode() != PPC::BDNZ8 && I->getOpcode() != PPC::BDNZ &&
      I->getOpcode() != PPC::BDZ8  && I->getOpcode() != PPC::BDZ)
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin()) return 1;
  --I;
  if (I->getOpcode() != PPC::BCC &&
      I->getOpcode() != PPC::BC && I->getOpcode() != PPC::BCn &&
      I->getOpcode() != PPC::BDNZ8 && I->getOpcode() != PPC::BDNZ &&
      I->getOpcode() != PPC::BDZ8  && I->getOpcode() != PPC::BDZ)
    return 1;

  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

unsigned PPCInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) &&
         "PPC branch conditions have two components!");
  assert(!BytesAdded && "code size not handled");

  bool isPPC64 = Subtarget.isPPC64();

  // One-way branch.
  if (!FBB) {
    if (Cond.empty())   // Unconditional branch
      BuildMI(&MBB, DL, get(PPC::B)).addMBB(TBB);
    else if (Cond[1].getReg() == PPC::CTR || Cond[1].getReg() == PPC::CTR8)
      BuildMI(&MBB, DL, get(Cond[0].getImm() ?
                              (isPPC64 ? PPC::BDNZ8 : PPC::BDNZ) :
                              (isPPC64 ? PPC::BDZ8  : PPC::BDZ))).addMBB(TBB);
    else if (Cond[0].getImm() == PPC::PRED_BIT_SET)
      BuildMI(&MBB, DL, get(PPC::BC)).add(Cond[1]).addMBB(TBB);
    else if (Cond[0].getImm() == PPC::PRED_BIT_UNSET)
      BuildMI(&MBB, DL, get(PPC::BCn)).add(Cond[1]).addMBB(TBB);
    else                // Conditional branch
      BuildMI(&MBB, DL, get(PPC::BCC))
          .addImm(Cond[0].getImm())
          .add(Cond[1])
          .addMBB(TBB);
    return 1;
  }

  // Two-way Conditional Branch.
  if (Cond[1].getReg() == PPC::CTR || Cond[1].getReg() == PPC::CTR8)
    BuildMI(&MBB, DL, get(Cond[0].getImm() ?
                            (isPPC64 ? PPC::BDNZ8 : PPC::BDNZ) :
                            (isPPC64 ? PPC::BDZ8  : PPC::BDZ))).addMBB(TBB);
  else if (Cond[0].getImm() == PPC::PRED_BIT_SET)
    BuildMI(&MBB, DL, get(PPC::BC)).add(Cond[1]).addMBB(TBB);
  else if (Cond[0].getImm() == PPC::PRED_BIT_UNSET)
    BuildMI(&MBB, DL, get(PPC::BCn)).add(Cond[1]).addMBB(TBB);
  else
    BuildMI(&MBB, DL, get(PPC::BCC))
        .addImm(Cond[0].getImm())
        .add(Cond[1])
        .addMBB(TBB);
  BuildMI(&MBB, DL, get(PPC::B)).addMBB(FBB);
  return 2;
}

// Select analysis.
bool PPCInstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                   ArrayRef<MachineOperand> Cond,
                                   Register DstReg, Register TrueReg,
                                   Register FalseReg, int &CondCycles,
                                   int &TrueCycles, int &FalseCycles) const {
  if (!Subtarget.hasISEL())
    return false;

  if (Cond.size() != 2)
    return false;

  // If this is really a bdnz-like condition, then it cannot be turned into a
  // select.
  if (Cond[1].getReg() == PPC::CTR || Cond[1].getReg() == PPC::CTR8)
    return false;

  // If the conditional branch uses a physical register, then it cannot be
  // turned into a select.
  if (Cond[1].getReg().isPhysical())
    return false;

  // Check register classes.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
    RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC)
    return false;

  // isel is for regular integer GPRs only.
  if (!PPC::GPRCRegClass.hasSubClassEq(RC) &&
      !PPC::GPRC_NOR0RegClass.hasSubClassEq(RC) &&
      !PPC::G8RCRegClass.hasSubClassEq(RC) &&
      !PPC::G8RC_NOX0RegClass.hasSubClassEq(RC))
    return false;

  // FIXME: These numbers are for the A2, how well they work for other cores is
  // an open question. On the A2, the isel instruction has a 2-cycle latency
  // but single-cycle throughput. These numbers are used in combination with
  // the MispredictPenalty setting from the active SchedMachineModel.
  CondCycles = 1;
  TrueCycles = 1;
  FalseCycles = 1;

  return true;
}

void PPCInstrInfo::insertSelect(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &dl, Register DestReg,
                                ArrayRef<MachineOperand> Cond, Register TrueReg,
                                Register FalseReg) const {
  assert(Cond.size() == 2 &&
         "PPC branch conditions have two components!");

  // Get the register classes.
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
    RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  assert(RC && "TrueReg and FalseReg must have overlapping register classes");

  bool Is64Bit = PPC::G8RCRegClass.hasSubClassEq(RC) ||
                 PPC::G8RC_NOX0RegClass.hasSubClassEq(RC);
  assert((Is64Bit ||
          PPC::GPRCRegClass.hasSubClassEq(RC) ||
          PPC::GPRC_NOR0RegClass.hasSubClassEq(RC)) &&
         "isel is for regular integer GPRs only");

  unsigned OpCode = Is64Bit ? PPC::ISEL8 : PPC::ISEL;
  auto SelectPred = static_cast<PPC::Predicate>(Cond[0].getImm());

  unsigned SubIdx = 0;
  bool SwapOps = false;
  switch (SelectPred) {
  case PPC::PRED_EQ:
  case PPC::PRED_EQ_MINUS:
  case PPC::PRED_EQ_PLUS:
      SubIdx = PPC::sub_eq; SwapOps = false; break;
  case PPC::PRED_NE:
  case PPC::PRED_NE_MINUS:
  case PPC::PRED_NE_PLUS:
      SubIdx = PPC::sub_eq; SwapOps = true; break;
  case PPC::PRED_LT:
  case PPC::PRED_LT_MINUS:
  case PPC::PRED_LT_PLUS:
      SubIdx = PPC::sub_lt; SwapOps = false; break;
  case PPC::PRED_GE:
  case PPC::PRED_GE_MINUS:
  case PPC::PRED_GE_PLUS:
      SubIdx = PPC::sub_lt; SwapOps = true; break;
  case PPC::PRED_GT:
  case PPC::PRED_GT_MINUS:
  case PPC::PRED_GT_PLUS:
      SubIdx = PPC::sub_gt; SwapOps = false; break;
  case PPC::PRED_LE:
  case PPC::PRED_LE_MINUS:
  case PPC::PRED_LE_PLUS:
      SubIdx = PPC::sub_gt; SwapOps = true; break;
  case PPC::PRED_UN:
  case PPC::PRED_UN_MINUS:
  case PPC::PRED_UN_PLUS:
      SubIdx = PPC::sub_un; SwapOps = false; break;
  case PPC::PRED_NU:
  case PPC::PRED_NU_MINUS:
  case PPC::PRED_NU_PLUS:
      SubIdx = PPC::sub_un; SwapOps = true; break;
  case PPC::PRED_BIT_SET:   SubIdx = 0; SwapOps = false; break;
  case PPC::PRED_BIT_UNSET: SubIdx = 0; SwapOps = true; break;
  }

  Register FirstReg =  SwapOps ? FalseReg : TrueReg,
           SecondReg = SwapOps ? TrueReg  : FalseReg;

  // The first input register of isel cannot be r0. If it is a member
  // of a register class that can be r0, then copy it first (the
  // register allocator should eliminate the copy).
  if (MRI.getRegClass(FirstReg)->contains(PPC::R0) ||
      MRI.getRegClass(FirstReg)->contains(PPC::X0)) {
    const TargetRegisterClass *FirstRC =
      MRI.getRegClass(FirstReg)->contains(PPC::X0) ?
        &PPC::G8RC_NOX0RegClass : &PPC::GPRC_NOR0RegClass;
    Register OldFirstReg = FirstReg;
    FirstReg = MRI.createVirtualRegister(FirstRC);
    BuildMI(MBB, MI, dl, get(TargetOpcode::COPY), FirstReg)
      .addReg(OldFirstReg);
  }

  BuildMI(MBB, MI, dl, get(OpCode), DestReg)
    .addReg(FirstReg).addReg(SecondReg)
    .addReg(Cond[1].getReg(), 0, SubIdx);
}

static unsigned getCRBitValue(unsigned CRBit) {
  unsigned Ret = 4;
  if (CRBit == PPC::CR0LT || CRBit == PPC::CR1LT ||
      CRBit == PPC::CR2LT || CRBit == PPC::CR3LT ||
      CRBit == PPC::CR4LT || CRBit == PPC::CR5LT ||
      CRBit == PPC::CR6LT || CRBit == PPC::CR7LT)
    Ret = 3;
  if (CRBit == PPC::CR0GT || CRBit == PPC::CR1GT ||
      CRBit == PPC::CR2GT || CRBit == PPC::CR3GT ||
      CRBit == PPC::CR4GT || CRBit == PPC::CR5GT ||
      CRBit == PPC::CR6GT || CRBit == PPC::CR7GT)
    Ret = 2;
  if (CRBit == PPC::CR0EQ || CRBit == PPC::CR1EQ ||
      CRBit == PPC::CR2EQ || CRBit == PPC::CR3EQ ||
      CRBit == PPC::CR4EQ || CRBit == PPC::CR5EQ ||
      CRBit == PPC::CR6EQ || CRBit == PPC::CR7EQ)
    Ret = 1;
  if (CRBit == PPC::CR0UN || CRBit == PPC::CR1UN ||
      CRBit == PPC::CR2UN || CRBit == PPC::CR3UN ||
      CRBit == PPC::CR4UN || CRBit == PPC::CR5UN ||
      CRBit == PPC::CR6UN || CRBit == PPC::CR7UN)
    Ret = 0;

  assert(Ret != 4 && "Invalid CR bit register");
  return Ret;
}

void PPCInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, MCRegister DestReg,
                               MCRegister SrcReg, bool KillSrc) const {
  // We can end up with self copies and similar things as a result of VSX copy
  // legalization. Promote them here.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  if (PPC::F8RCRegClass.contains(DestReg) &&
      PPC::VSRCRegClass.contains(SrcReg)) {
    MCRegister SuperReg =
        TRI->getMatchingSuperReg(DestReg, PPC::sub_64, &PPC::VSRCRegClass);

    if (VSXSelfCopyCrash && SrcReg == SuperReg)
      llvm_unreachable("nop VSX copy");

    DestReg = SuperReg;
  } else if (PPC::F8RCRegClass.contains(SrcReg) &&
             PPC::VSRCRegClass.contains(DestReg)) {
    MCRegister SuperReg =
        TRI->getMatchingSuperReg(SrcReg, PPC::sub_64, &PPC::VSRCRegClass);

    if (VSXSelfCopyCrash && DestReg == SuperReg)
      llvm_unreachable("nop VSX copy");

    SrcReg = SuperReg;
  }

  // Different class register copy
  if (PPC::CRBITRCRegClass.contains(SrcReg) &&
      PPC::GPRCRegClass.contains(DestReg)) {
    MCRegister CRReg = getCRFromCRBit(SrcReg);
    BuildMI(MBB, I, DL, get(PPC::MFOCRF), DestReg).addReg(CRReg);
    getKillRegState(KillSrc);
    // Rotate the CR bit in the CR fields to be the least significant bit and
    // then mask with 0x1 (MB = ME = 31).
    BuildMI(MBB, I, DL, get(PPC::RLWINM), DestReg)
       .addReg(DestReg, RegState::Kill)
       .addImm(TRI->getEncodingValue(CRReg) * 4 + (4 - getCRBitValue(SrcReg)))
       .addImm(31)
       .addImm(31);
    return;
  } else if (PPC::CRRCRegClass.contains(SrcReg) &&
             (PPC::G8RCRegClass.contains(DestReg) ||
              PPC::GPRCRegClass.contains(DestReg))) {
    bool Is64Bit = PPC::G8RCRegClass.contains(DestReg);
    unsigned MvCode = Is64Bit ? PPC::MFOCRF8 : PPC::MFOCRF;
    unsigned ShCode = Is64Bit ? PPC::RLWINM8 : PPC::RLWINM;
    unsigned CRNum = TRI->getEncodingValue(SrcReg);
    BuildMI(MBB, I, DL, get(MvCode), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    if (CRNum == 7)
      return;
    // Shift the CR bits to make the CR field in the lowest 4 bits of GRC.
    BuildMI(MBB, I, DL, get(ShCode), DestReg)
        .addReg(DestReg, RegState::Kill)
        .addImm(CRNum * 4 + 4)
        .addImm(28)
        .addImm(31);
    return;
  } else if (PPC::G8RCRegClass.contains(SrcReg) &&
             PPC::VSFRCRegClass.contains(DestReg)) {
    assert(Subtarget.hasDirectMove() &&
           "Subtarget doesn't support directmove, don't know how to copy.");
    BuildMI(MBB, I, DL, get(PPC::MTVSRD), DestReg).addReg(SrcReg);
    NumGPRtoVSRSpill++;
    getKillRegState(KillSrc);
    return;
  } else if (PPC::VSFRCRegClass.contains(SrcReg) &&
             PPC::G8RCRegClass.contains(DestReg)) {
    assert(Subtarget.hasDirectMove() &&
           "Subtarget doesn't support directmove, don't know how to copy.");
    BuildMI(MBB, I, DL, get(PPC::MFVSRD), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  } else if (PPC::SPERCRegClass.contains(SrcReg) &&
             PPC::GPRCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::EFSCFD), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  } else if (PPC::GPRCRegClass.contains(SrcReg) &&
             PPC::SPERCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::EFDCFS), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  }

  unsigned Opc;
  if (PPC::GPRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::OR;
  else if (PPC::G8RCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::OR8;
  else if (PPC::F4RCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::FMR;
  else if (PPC::CRRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::MCRF;
  else if (PPC::VRRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::VOR;
  else if (PPC::VSRCRegClass.contains(DestReg, SrcReg))
    // There are two different ways this can be done:
    //   1. xxlor : This has lower latency (on the P7), 2 cycles, but can only
    //      issue in VSU pipeline 0.
    //   2. xmovdp/xmovsp: This has higher latency (on the P7), 6 cycles, but
    //      can go to either pipeline.
    // We'll always use xxlor here, because in practically all cases where
    // copies are generated, they are close enough to some use that the
    // lower-latency form is preferable.
    Opc = PPC::XXLOR;
  else if (PPC::VSFRCRegClass.contains(DestReg, SrcReg) ||
           PPC::VSSRCRegClass.contains(DestReg, SrcReg))
    Opc = (Subtarget.hasP9Vector()) ? PPC::XSCPSGNDP : PPC::XXLORf;
  else if (Subtarget.pairedVectorMemops() &&
           PPC::VSRpRCRegClass.contains(DestReg, SrcReg)) {
    if (SrcReg > PPC::VSRp15)
      SrcReg = PPC::V0 + (SrcReg - PPC::VSRp16) * 2;
    else
      SrcReg = PPC::VSL0 + (SrcReg - PPC::VSRp0) * 2;
    if (DestReg > PPC::VSRp15)
      DestReg = PPC::V0 + (DestReg - PPC::VSRp16) * 2;
    else
      DestReg = PPC::VSL0 + (DestReg - PPC::VSRp0) * 2;
    BuildMI(MBB, I, DL, get(PPC::XXLOR), DestReg).
      addReg(SrcReg).addReg(SrcReg, getKillRegState(KillSrc));
    BuildMI(MBB, I, DL, get(PPC::XXLOR), DestReg + 1).
      addReg(SrcReg + 1).addReg(SrcReg + 1, getKillRegState(KillSrc));
    return;
  }
  else if (PPC::CRBITRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::CROR;
  else if (PPC::SPERCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::EVOR;
  else if ((PPC::ACCRCRegClass.contains(DestReg) ||
            PPC::UACCRCRegClass.contains(DestReg)) &&
           (PPC::ACCRCRegClass.contains(SrcReg) ||
            PPC::UACCRCRegClass.contains(SrcReg))) {
    // If primed, de-prime the source register, copy the individual registers
    // and prime the destination if needed. The vector subregisters are
    // vs[(u)acc * 4] - vs[(u)acc * 4 + 3]. If the copy is not a kill and the
    // source is primed, we need to re-prime it after the copy as well.
    PPCRegisterInfo::emitAccCopyInfo(MBB, DestReg, SrcReg);
    bool DestPrimed = PPC::ACCRCRegClass.contains(DestReg);
    bool SrcPrimed = PPC::ACCRCRegClass.contains(SrcReg);
    MCRegister VSLSrcReg =
        PPC::VSL0 + (SrcReg - (SrcPrimed ? PPC::ACC0 : PPC::UACC0)) * 4;
    MCRegister VSLDestReg =
        PPC::VSL0 + (DestReg - (DestPrimed ? PPC::ACC0 : PPC::UACC0)) * 4;
    if (SrcPrimed)
      BuildMI(MBB, I, DL, get(PPC::XXMFACC), SrcReg).addReg(SrcReg);
    for (unsigned Idx = 0; Idx < 4; Idx++)
      BuildMI(MBB, I, DL, get(PPC::XXLOR), VSLDestReg + Idx)
          .addReg(VSLSrcReg + Idx)
          .addReg(VSLSrcReg + Idx, getKillRegState(KillSrc));
    if (DestPrimed)
      BuildMI(MBB, I, DL, get(PPC::XXMTACC), DestReg).addReg(DestReg);
    if (SrcPrimed && !KillSrc)
      BuildMI(MBB, I, DL, get(PPC::XXMTACC), SrcReg).addReg(SrcReg);
    return;
  } else if (PPC::G8pRCRegClass.contains(DestReg) &&
             PPC::G8pRCRegClass.contains(SrcReg)) {
    // TODO: Handle G8RC to G8pRC (and vice versa) copy.
    unsigned DestRegIdx = DestReg - PPC::G8p0;
    MCRegister DestRegSub0 = PPC::X0 + 2 * DestRegIdx;
    MCRegister DestRegSub1 = PPC::X0 + 2 * DestRegIdx + 1;
    unsigned SrcRegIdx = SrcReg - PPC::G8p0;
    MCRegister SrcRegSub0 = PPC::X0 + 2 * SrcRegIdx;
    MCRegister SrcRegSub1 = PPC::X0 + 2 * SrcRegIdx + 1;
    BuildMI(MBB, I, DL, get(PPC::OR8), DestRegSub0)
        .addReg(SrcRegSub0)
        .addReg(SrcRegSub0, getKillRegState(KillSrc));
    BuildMI(MBB, I, DL, get(PPC::OR8), DestRegSub1)
        .addReg(SrcRegSub1)
        .addReg(SrcRegSub1, getKillRegState(KillSrc));
    return;
  } else
    llvm_unreachable("Impossible reg-to-reg copy");

  const MCInstrDesc &MCID = get(Opc);
  if (MCID.getNumOperands() == 3)
    BuildMI(MBB, I, DL, MCID, DestReg)
      .addReg(SrcReg).addReg(SrcReg, getKillRegState(KillSrc));
  else
    BuildMI(MBB, I, DL, MCID, DestReg).addReg(SrcReg, getKillRegState(KillSrc));
}

unsigned PPCInstrInfo::getSpillIndex(const TargetRegisterClass *RC) const {
  int OpcodeIndex = 0;

  if (PPC::GPRCRegClass.hasSubClassEq(RC) ||
      PPC::GPRC_NOR0RegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_Int4Spill;
  } else if (PPC::G8RCRegClass.hasSubClassEq(RC) ||
             PPC::G8RC_NOX0RegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_Int8Spill;
  } else if (PPC::F8RCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_Float8Spill;
  } else if (PPC::F4RCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_Float4Spill;
  } else if (PPC::SPERCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_SPESpill;
  } else if (PPC::CRRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_CRSpill;
  } else if (PPC::CRBITRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_CRBitSpill;
  } else if (PPC::VRRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_VRVectorSpill;
  } else if (PPC::VSRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_VSXVectorSpill;
  } else if (PPC::VSFRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_VectorFloat8Spill;
  } else if (PPC::VSSRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_VectorFloat4Spill;
  } else if (PPC::SPILLTOVSRRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_SpillToVSR;
  } else if (PPC::ACCRCRegClass.hasSubClassEq(RC)) {
    assert(Subtarget.pairedVectorMemops() &&
           "Register unexpected when paired memops are disabled.");
    OpcodeIndex = SOK_AccumulatorSpill;
  } else if (PPC::UACCRCRegClass.hasSubClassEq(RC)) {
    assert(Subtarget.pairedVectorMemops() &&
           "Register unexpected when paired memops are disabled.");
    OpcodeIndex = SOK_UAccumulatorSpill;
  } else if (PPC::WACCRCRegClass.hasSubClassEq(RC)) {
    assert(Subtarget.pairedVectorMemops() &&
           "Register unexpected when paired memops are disabled.");
    OpcodeIndex = SOK_WAccumulatorSpill;
  } else if (PPC::VSRpRCRegClass.hasSubClassEq(RC)) {
    assert(Subtarget.pairedVectorMemops() &&
           "Register unexpected when paired memops are disabled.");
    OpcodeIndex = SOK_PairedVecSpill;
  } else if (PPC::G8pRCRegClass.hasSubClassEq(RC)) {
    OpcodeIndex = SOK_PairedG8Spill;
  } else {
    llvm_unreachable("Unknown regclass!");
  }
  return OpcodeIndex;
}

unsigned
PPCInstrInfo::getStoreOpcodeForSpill(const TargetRegisterClass *RC) const {
  ArrayRef<unsigned> OpcodesForSpill = getStoreOpcodesForSpillArray();
  return OpcodesForSpill[getSpillIndex(RC)];
}

unsigned
PPCInstrInfo::getLoadOpcodeForSpill(const TargetRegisterClass *RC) const {
  ArrayRef<unsigned> OpcodesForSpill = getLoadOpcodesForSpillArray();
  return OpcodesForSpill[getSpillIndex(RC)];
}

void PPCInstrInfo::StoreRegToStackSlot(
    MachineFunction &MF, unsigned SrcReg, bool isKill, int FrameIdx,
    const TargetRegisterClass *RC,
    SmallVectorImpl<MachineInstr *> &NewMIs) const {
  unsigned Opcode = getStoreOpcodeForSpill(RC);
  DebugLoc DL;

  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setHasSpills();

  NewMIs.push_back(addFrameReference(
      BuildMI(MF, DL, get(Opcode)).addReg(SrcReg, getKillRegState(isKill)),
      FrameIdx));

  if (PPC::CRRCRegClass.hasSubClassEq(RC) ||
      PPC::CRBITRCRegClass.hasSubClassEq(RC))
    FuncInfo->setSpillsCR();

  if (isXFormMemOp(Opcode))
    FuncInfo->setHasNonRISpills();
}

void PPCInstrInfo::storeRegToStackSlotNoUpd(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, unsigned SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  SmallVector<MachineInstr *, 4> NewMIs;

  StoreRegToStackSlot(MF, SrcReg, isKill, FrameIdx, RC, NewMIs);

  for (MachineInstr *NewMI : NewMIs)
    MBB.insert(MI, NewMI);

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlign(FrameIdx));
  NewMIs.back()->addMemOperand(MF, MMO);
}

void PPCInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  // We need to avoid a situation in which the value from a VRRC register is
  // spilled using an Altivec instruction and reloaded into a VSRC register
  // using a VSX instruction. The issue with this is that the VSX
  // load/store instructions swap the doublewords in the vector and the Altivec
  // ones don't. The register classes on the spill/reload may be different if
  // the register is defined using an Altivec instruction and is then used by a
  // VSX instruction.
  RC = updatedRC(RC);
  storeRegToStackSlotNoUpd(MBB, MI, SrcReg, isKill, FrameIdx, RC, TRI);
}

void PPCInstrInfo::LoadRegFromStackSlot(MachineFunction &MF, const DebugLoc &DL,
                                        unsigned DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        SmallVectorImpl<MachineInstr *> &NewMIs)
                                        const {
  unsigned Opcode = getLoadOpcodeForSpill(RC);
  NewMIs.push_back(addFrameReference(BuildMI(MF, DL, get(Opcode), DestReg),
                                     FrameIdx));
}

void PPCInstrInfo::loadRegFromStackSlotNoUpd(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, unsigned DestReg,
    int FrameIdx, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  SmallVector<MachineInstr*, 4> NewMIs;
  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  LoadRegFromStackSlot(MF, DL, DestReg, FrameIdx, RC, NewMIs);

  for (MachineInstr *NewMI : NewMIs)
    MBB.insert(MI, NewMI);

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlign(FrameIdx));
  NewMIs.back()->addMemOperand(MF, MMO);
}

void PPCInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI,
                                        Register VReg) const {
  // We need to avoid a situation in which the value from a VRRC register is
  // spilled using an Altivec instruction and reloaded into a VSRC register
  // using a VSX instruction. The issue with this is that the VSX
  // load/store instructions swap the doublewords in the vector and the Altivec
  // ones don't. The register classes on the spill/reload may be different if
  // the register is defined using an Altivec instruction and is then used by a
  // VSX instruction.
  RC = updatedRC(RC);

  loadRegFromStackSlotNoUpd(MBB, MI, DestReg, FrameIdx, RC, TRI);
}

bool PPCInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Invalid PPC branch opcode!");
  if (Cond[1].getReg() == PPC::CTR8 || Cond[1].getReg() == PPC::CTR)
    Cond[0].setImm(Cond[0].getImm() == 0 ? 1 : 0);
  else
    // Leave the CR# the same, but invert the condition.
    Cond[0].setImm(PPC::InvertPredicate((PPC::Predicate)Cond[0].getImm()));
  return false;
}

// For some instructions, it is legal to fold ZERO into the RA register field.
// This function performs that fold by replacing the operand with PPC::ZERO,
// it does not consider whether the load immediate zero is no longer in use.
bool PPCInstrInfo::onlyFoldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                     Register Reg) const {
  // A zero immediate should always be loaded with a single li.
  unsigned DefOpc = DefMI.getOpcode();
  if (DefOpc != PPC::LI && DefOpc != PPC::LI8)
    return false;
  if (!DefMI.getOperand(1).isImm())
    return false;
  if (DefMI.getOperand(1).getImm() != 0)
    return false;

  // Note that we cannot here invert the arguments of an isel in order to fold
  // a ZERO into what is presented as the second argument. All we have here
  // is the condition bit, and that might come from a CR-logical bit operation.

  const MCInstrDesc &UseMCID = UseMI.getDesc();

  // Only fold into real machine instructions.
  if (UseMCID.isPseudo())
    return false;

  // We need to find which of the User's operands is to be folded, that will be
  // the operand that matches the given register ID.
  unsigned UseIdx;
  for (UseIdx = 0; UseIdx < UseMI.getNumOperands(); ++UseIdx)
    if (UseMI.getOperand(UseIdx).isReg() &&
        UseMI.getOperand(UseIdx).getReg() == Reg)
      break;

  assert(UseIdx < UseMI.getNumOperands() && "Cannot find Reg in UseMI");
  assert(UseIdx < UseMCID.getNumOperands() && "No operand description for Reg");

  const MCOperandInfo *UseInfo = &UseMCID.operands()[UseIdx];

  // We can fold the zero if this register requires a GPRC_NOR0/G8RC_NOX0
  // register (which might also be specified as a pointer class kind).
  if (UseInfo->isLookupPtrRegClass()) {
    if (UseInfo->RegClass /* Kind */ != 1)
      return false;
  } else {
    if (UseInfo->RegClass != PPC::GPRC_NOR0RegClassID &&
        UseInfo->RegClass != PPC::G8RC_NOX0RegClassID)
      return false;
  }

  // Make sure this is not tied to an output register (or otherwise
  // constrained). This is true for ST?UX registers, for example, which
  // are tied to their output registers.
  if (UseInfo->Constraints != 0)
    return false;

  MCRegister ZeroReg;
  if (UseInfo->isLookupPtrRegClass()) {
    bool isPPC64 = Subtarget.isPPC64();
    ZeroReg = isPPC64 ? PPC::ZERO8 : PPC::ZERO;
  } else {
    ZeroReg = UseInfo->RegClass == PPC::G8RC_NOX0RegClassID ?
              PPC::ZERO8 : PPC::ZERO;
  }

  LLVM_DEBUG(dbgs() << "Folded immediate zero for: ");
  LLVM_DEBUG(UseMI.dump());
  UseMI.getOperand(UseIdx).setReg(ZeroReg);
  LLVM_DEBUG(dbgs() << "Into: ");
  LLVM_DEBUG(UseMI.dump());
  return true;
}

// Folds zero into instructions which have a load immediate zero as an operand
// but also recognize zero as immediate zero. If the definition of the load
// has no more users it is deleted.
bool PPCInstrInfo::foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                 Register Reg, MachineRegisterInfo *MRI) const {
  bool Changed = onlyFoldImmediate(UseMI, DefMI, Reg);
  if (MRI->use_nodbg_empty(Reg))
    DefMI.eraseFromParent();
  return Changed;
}

static bool MBBDefinesCTR(MachineBasicBlock &MBB) {
  for (MachineInstr &MI : MBB)
    if (MI.definesRegister(PPC::CTR, /*TRI=*/nullptr) ||
        MI.definesRegister(PPC::CTR8, /*TRI=*/nullptr))
      return true;
  return false;
}

// We should make sure that, if we're going to predicate both sides of a
// condition (a diamond), that both sides don't define the counter register. We
// can predicate counter-decrement-based branches, but while that predicates
// the branching, it does not predicate the counter decrement. If we tried to
// merge the triangle into one predicated block, we'd decrement the counter
// twice.
bool PPCInstrInfo::isProfitableToIfCvt(MachineBasicBlock &TMBB,
                     unsigned NumT, unsigned ExtraT,
                     MachineBasicBlock &FMBB,
                     unsigned NumF, unsigned ExtraF,
                     BranchProbability Probability) const {
  return !(MBBDefinesCTR(TMBB) && MBBDefinesCTR(FMBB));
}


bool PPCInstrInfo::isPredicated(const MachineInstr &MI) const {
  // The predicated branches are identified by their type, not really by the
  // explicit presence of a predicate. Furthermore, some of them can be
  // predicated more than once. Because if conversion won't try to predicate
  // any instruction which already claims to be predicated (by returning true
  // here), always return false. In doing so, we let isPredicable() be the
  // final word on whether not the instruction can be (further) predicated.

  return false;
}

bool PPCInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                        const MachineBasicBlock *MBB,
                                        const MachineFunction &MF) const {
  switch (MI.getOpcode()) {
  default:
    break;
  // Set MFFS and MTFSF as scheduling boundary to avoid unexpected code motion
  // across them, since some FP operations may change content of FPSCR.
  // TODO: Model FPSCR in PPC instruction definitions and remove the workaround
  case PPC::MFFS:
  case PPC::MTFSF:
  case PPC::FENCE:
    return true;
  }
  return TargetInstrInfo::isSchedulingBoundary(MI, MBB, MF);
}

bool PPCInstrInfo::PredicateInstruction(MachineInstr &MI,
                                        ArrayRef<MachineOperand> Pred) const {
  unsigned OpC = MI.getOpcode();
  if (OpC == PPC::BLR || OpC == PPC::BLR8) {
    if (Pred[1].getReg() == PPC::CTR8 || Pred[1].getReg() == PPC::CTR) {
      bool isPPC64 = Subtarget.isPPC64();
      MI.setDesc(get(Pred[0].getImm() ? (isPPC64 ? PPC::BDNZLR8 : PPC::BDNZLR)
                                      : (isPPC64 ? PPC::BDZLR8 : PPC::BDZLR)));
      // Need add Def and Use for CTR implicit operand.
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addReg(Pred[1].getReg(), RegState::Implicit)
          .addReg(Pred[1].getReg(), RegState::ImplicitDefine);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_SET) {
      MI.setDesc(get(PPC::BCLR));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_UNSET) {
      MI.setDesc(get(PPC::BCLRn));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
    } else {
      MI.setDesc(get(PPC::BCCLR));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addImm(Pred[0].getImm())
          .add(Pred[1]);
    }

    return true;
  } else if (OpC == PPC::B) {
    if (Pred[1].getReg() == PPC::CTR8 || Pred[1].getReg() == PPC::CTR) {
      bool isPPC64 = Subtarget.isPPC64();
      MI.setDesc(get(Pred[0].getImm() ? (isPPC64 ? PPC::BDNZ8 : PPC::BDNZ)
                                      : (isPPC64 ? PPC::BDZ8 : PPC::BDZ)));
      // Need add Def and Use for CTR implicit operand.
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addReg(Pred[1].getReg(), RegState::Implicit)
          .addReg(Pred[1].getReg(), RegState::ImplicitDefine);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_SET) {
      MachineBasicBlock *MBB = MI.getOperand(0).getMBB();
      MI.removeOperand(0);

      MI.setDesc(get(PPC::BC));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .add(Pred[1])
          .addMBB(MBB);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_UNSET) {
      MachineBasicBlock *MBB = MI.getOperand(0).getMBB();
      MI.removeOperand(0);

      MI.setDesc(get(PPC::BCn));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .add(Pred[1])
          .addMBB(MBB);
    } else {
      MachineBasicBlock *MBB = MI.getOperand(0).getMBB();
      MI.removeOperand(0);

      MI.setDesc(get(PPC::BCC));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addImm(Pred[0].getImm())
          .add(Pred[1])
          .addMBB(MBB);
    }

    return true;
  } else if (OpC == PPC::BCTR || OpC == PPC::BCTR8 || OpC == PPC::BCTRL ||
             OpC == PPC::BCTRL8 || OpC == PPC::BCTRL_RM ||
             OpC == PPC::BCTRL8_RM) {
    if (Pred[1].getReg() == PPC::CTR8 || Pred[1].getReg() == PPC::CTR)
      llvm_unreachable("Cannot predicate bctr[l] on the ctr register");

    bool setLR = OpC == PPC::BCTRL || OpC == PPC::BCTRL8 ||
                 OpC == PPC::BCTRL_RM || OpC == PPC::BCTRL8_RM;
    bool isPPC64 = Subtarget.isPPC64();

    if (Pred[0].getImm() == PPC::PRED_BIT_SET) {
      MI.setDesc(get(isPPC64 ? (setLR ? PPC::BCCTRL8 : PPC::BCCTR8)
                             : (setLR ? PPC::BCCTRL : PPC::BCCTR)));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_UNSET) {
      MI.setDesc(get(isPPC64 ? (setLR ? PPC::BCCTRL8n : PPC::BCCTR8n)
                             : (setLR ? PPC::BCCTRLn : PPC::BCCTRn)));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
    } else {
      MI.setDesc(get(isPPC64 ? (setLR ? PPC::BCCCTRL8 : PPC::BCCCTR8)
                             : (setLR ? PPC::BCCCTRL : PPC::BCCCTR)));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addImm(Pred[0].getImm())
          .add(Pred[1]);
    }

    // Need add Def and Use for LR implicit operand.
    if (setLR)
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addReg(isPPC64 ? PPC::LR8 : PPC::LR, RegState::Implicit)
          .addReg(isPPC64 ? PPC::LR8 : PPC::LR, RegState::ImplicitDefine);
    if (OpC == PPC::BCTRL_RM || OpC == PPC::BCTRL8_RM)
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addReg(PPC::RM, RegState::ImplicitDefine);

    return true;
  }

  return false;
}

bool PPCInstrInfo::SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                                     ArrayRef<MachineOperand> Pred2) const {
  assert(Pred1.size() == 2 && "Invalid PPC first predicate");
  assert(Pred2.size() == 2 && "Invalid PPC second predicate");

  if (Pred1[1].getReg() == PPC::CTR8 || Pred1[1].getReg() == PPC::CTR)
    return false;
  if (Pred2[1].getReg() == PPC::CTR8 || Pred2[1].getReg() == PPC::CTR)
    return false;

  // P1 can only subsume P2 if they test the same condition register.
  if (Pred1[1].getReg() != Pred2[1].getReg())
    return false;

  PPC::Predicate P1 = (PPC::Predicate) Pred1[0].getImm();
  PPC::Predicate P2 = (PPC::Predicate) Pred2[0].getImm();

  if (P1 == P2)
    return true;

  // Does P1 subsume P2, e.g. GE subsumes GT.
  if (P1 == PPC::PRED_LE &&
      (P2 == PPC::PRED_LT || P2 == PPC::PRED_EQ))
    return true;
  if (P1 == PPC::PRED_GE &&
      (P2 == PPC::PRED_GT || P2 == PPC::PRED_EQ))
    return true;

  return false;
}

bool PPCInstrInfo::ClobbersPredicate(MachineInstr &MI,
                                     std::vector<MachineOperand> &Pred,
                                     bool SkipDead) const {
  // Note: At the present time, the contents of Pred from this function is
  // unused by IfConversion. This implementation follows ARM by pushing the
  // CR-defining operand. Because the 'DZ' and 'DNZ' count as types of
  // predicate, instructions defining CTR or CTR8 are also included as
  // predicate-defining instructions.

  const TargetRegisterClass *RCs[] =
    { &PPC::CRRCRegClass, &PPC::CRBITRCRegClass,
      &PPC::CTRRCRegClass, &PPC::CTRRC8RegClass };

  bool Found = false;
  for (const MachineOperand &MO : MI.operands()) {
    for (unsigned c = 0; c < std::size(RCs) && !Found; ++c) {
      const TargetRegisterClass *RC = RCs[c];
      if (MO.isReg()) {
        if (MO.isDef() && RC->contains(MO.getReg())) {
          Pred.push_back(MO);
          Found = true;
        }
      } else if (MO.isRegMask()) {
        for (MCPhysReg R : *RC)
          if (MO.clobbersPhysReg(R)) {
            Pred.push_back(MO);
            Found = true;
          }
      }
    }
  }

  return Found;
}

bool PPCInstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                  Register &SrcReg2, int64_t &Mask,
                                  int64_t &Value) const {
  unsigned Opc = MI.getOpcode();

  switch (Opc) {
  default: return false;
  case PPC::CMPWI:
  case PPC::CMPLWI:
  case PPC::CMPDI:
  case PPC::CMPLDI:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    Value = MI.getOperand(2).getImm();
    Mask = 0xFFFF;
    return true;
  case PPC::CMPW:
  case PPC::CMPLW:
  case PPC::CMPD:
  case PPC::CMPLD:
  case PPC::FCMPUS:
  case PPC::FCMPUD:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = MI.getOperand(2).getReg();
    Value = 0;
    Mask = 0;
    return true;
  }
}

bool PPCInstrInfo::optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                                        Register SrcReg2, int64_t Mask,
                                        int64_t Value,
                                        const MachineRegisterInfo *MRI) const {
  if (DisableCmpOpt)
    return false;

  int OpC = CmpInstr.getOpcode();
  Register CRReg = CmpInstr.getOperand(0).getReg();

  // FP record forms set CR1 based on the exception status bits, not a
  // comparison with zero.
  if (OpC == PPC::FCMPUS || OpC == PPC::FCMPUD)
    return false;

  const TargetRegisterInfo *TRI = &getRegisterInfo();
  // The record forms set the condition register based on a signed comparison
  // with zero (so says the ISA manual). This is not as straightforward as it
  // seems, however, because this is always a 64-bit comparison on PPC64, even
  // for instructions that are 32-bit in nature (like slw for example).
  // So, on PPC32, for unsigned comparisons, we can use the record forms only
  // for equality checks (as those don't depend on the sign). On PPC64,
  // we are restricted to equality for unsigned 64-bit comparisons and for
  // signed 32-bit comparisons the applicability is more restricted.
  bool isPPC64 = Subtarget.isPPC64();
  bool is32BitSignedCompare   = OpC ==  PPC::CMPWI || OpC == PPC::CMPW;
  bool is32BitUnsignedCompare = OpC == PPC::CMPLWI || OpC == PPC::CMPLW;
  bool is64BitUnsignedCompare = OpC == PPC::CMPLDI || OpC == PPC::CMPLD;

  // Look through copies unless that gets us to a physical register.
  Register ActualSrc = TRI->lookThruCopyLike(SrcReg, MRI);
  if (ActualSrc.isVirtual())
    SrcReg = ActualSrc;

  // Get the unique definition of SrcReg.
  MachineInstr *MI = MRI->getUniqueVRegDef(SrcReg);
  if (!MI) return false;

  bool equalityOnly = false;
  bool noSub = false;
  if (isPPC64) {
    if (is32BitSignedCompare) {
      // We can perform this optimization only if SrcReg is sign-extending.
      if (isSignExtended(SrcReg, MRI))
        noSub = true;
      else
        return false;
    } else if (is32BitUnsignedCompare) {
      // We can perform this optimization, equality only, if SrcReg is
      // zero-extending.
      if (isZeroExtended(SrcReg, MRI)) {
        noSub = true;
        equalityOnly = true;
      } else
        return false;
    } else
      equalityOnly = is64BitUnsignedCompare;
  } else
    equalityOnly = is32BitUnsignedCompare;

  if (equalityOnly) {
    // We need to check the uses of the condition register in order to reject
    // non-equality comparisons.
    for (MachineRegisterInfo::use_instr_iterator
         I = MRI->use_instr_begin(CRReg), IE = MRI->use_instr_end();
         I != IE; ++I) {
      MachineInstr *UseMI = &*I;
      if (UseMI->getOpcode() == PPC::BCC) {
        PPC::Predicate Pred = (PPC::Predicate)UseMI->getOperand(0).getImm();
        unsigned PredCond = PPC::getPredicateCondition(Pred);
        // We ignore hint bits when checking for non-equality comparisons.
        if (PredCond != PPC::PRED_EQ && PredCond != PPC::PRED_NE)
          return false;
      } else if (UseMI->getOpcode() == PPC::ISEL ||
                 UseMI->getOpcode() == PPC::ISEL8) {
        unsigned SubIdx = UseMI->getOperand(3).getSubReg();
        if (SubIdx != PPC::sub_eq)
          return false;
      } else
        return false;
    }
  }

  MachineBasicBlock::iterator I = CmpInstr;

  // Scan forward to find the first use of the compare.
  for (MachineBasicBlock::iterator EL = CmpInstr.getParent()->end(); I != EL;
       ++I) {
    bool FoundUse = false;
    for (MachineRegisterInfo::use_instr_iterator
         J = MRI->use_instr_begin(CRReg), JE = MRI->use_instr_end();
         J != JE; ++J)
      if (&*J == &*I) {
        FoundUse = true;
        break;
      }

    if (FoundUse)
      break;
  }

  SmallVector<std::pair<MachineOperand*, PPC::Predicate>, 4> PredsToUpdate;
  SmallVector<std::pair<MachineOperand*, unsigned>, 4> SubRegsToUpdate;

  // There are two possible candidates which can be changed to set CR[01].
  // One is MI, the other is a SUB instruction.
  // For CMPrr(r1,r2), we are looking for SUB(r1,r2) or SUB(r2,r1).
  MachineInstr *Sub = nullptr;
  if (SrcReg2 != 0)
    // MI is not a candidate for CMPrr.
    MI = nullptr;
  // FIXME: Conservatively refuse to convert an instruction which isn't in the
  // same BB as the comparison. This is to allow the check below to avoid calls
  // (and other explicit clobbers); instead we should really check for these
  // more explicitly (in at least a few predecessors).
  else if (MI->getParent() != CmpInstr.getParent())
    return false;
  else if (Value != 0) {
    // The record-form instructions set CR bit based on signed comparison
    // against 0. We try to convert a compare against 1 or -1 into a compare
    // against 0 to exploit record-form instructions. For example, we change
    // the condition "greater than -1" into "greater than or equal to 0"
    // and "less than 1" into "less than or equal to 0".

    // Since we optimize comparison based on a specific branch condition,
    // we don't optimize if condition code is used by more than once.
    if (equalityOnly || !MRI->hasOneUse(CRReg))
      return false;

    MachineInstr *UseMI = &*MRI->use_instr_begin(CRReg);
    if (UseMI->getOpcode() != PPC::BCC)
      return false;

    PPC::Predicate Pred = (PPC::Predicate)UseMI->getOperand(0).getImm();
    unsigned PredCond = PPC::getPredicateCondition(Pred);
    unsigned PredHint = PPC::getPredicateHint(Pred);
    int16_t Immed = (int16_t)Value;

    // When modifying the condition in the predicate, we propagate hint bits
    // from the original predicate to the new one.
    if (Immed == -1 && PredCond == PPC::PRED_GT)
      // We convert "greater than -1" into "greater than or equal to 0",
      // since we are assuming signed comparison by !equalityOnly
      Pred = PPC::getPredicate(PPC::PRED_GE, PredHint);
    else if (Immed == -1 && PredCond == PPC::PRED_LE)
      // We convert "less than or equal to -1" into "less than 0".
      Pred = PPC::getPredicate(PPC::PRED_LT, PredHint);
    else if (Immed == 1 && PredCond == PPC::PRED_LT)
      // We convert "less than 1" into "less than or equal to 0".
      Pred = PPC::getPredicate(PPC::PRED_LE, PredHint);
    else if (Immed == 1 && PredCond == PPC::PRED_GE)
      // We convert "greater than or equal to 1" into "greater than 0".
      Pred = PPC::getPredicate(PPC::PRED_GT, PredHint);
    else
      return false;

    // Convert the comparison and its user to a compare against zero with the
    // appropriate predicate on the branch. Zero comparison might provide
    // optimization opportunities post-RA (see optimization in
    // PPCPreEmitPeephole.cpp).
    UseMI->getOperand(0).setImm(Pred);
    CmpInstr.getOperand(2).setImm(0);
  }

  // Search for Sub.
  --I;

  // Get ready to iterate backward from CmpInstr.
  MachineBasicBlock::iterator E = MI, B = CmpInstr.getParent()->begin();

  for (; I != E && !noSub; --I) {
    const MachineInstr &Instr = *I;
    unsigned IOpC = Instr.getOpcode();

    if (&*I != &CmpInstr && (Instr.modifiesRegister(PPC::CR0, TRI) ||
                             Instr.readsRegister(PPC::CR0, TRI)))
      // This instruction modifies or uses the record condition register after
      // the one we want to change. While we could do this transformation, it
      // would likely not be profitable. This transformation removes one
      // instruction, and so even forcing RA to generate one move probably
      // makes it unprofitable.
      return false;

    // Check whether CmpInstr can be made redundant by the current instruction.
    if ((OpC == PPC::CMPW || OpC == PPC::CMPLW ||
         OpC == PPC::CMPD || OpC == PPC::CMPLD) &&
        (IOpC == PPC::SUBF || IOpC == PPC::SUBF8) &&
        ((Instr.getOperand(1).getReg() == SrcReg &&
          Instr.getOperand(2).getReg() == SrcReg2) ||
        (Instr.getOperand(1).getReg() == SrcReg2 &&
         Instr.getOperand(2).getReg() == SrcReg))) {
      Sub = &*I;
      break;
    }

    if (I == B)
      // The 'and' is below the comparison instruction.
      return false;
  }

  // Return false if no candidates exist.
  if (!MI && !Sub)
    return false;

  // The single candidate is called MI.
  if (!MI) MI = Sub;

  int NewOpC = -1;
  int MIOpC = MI->getOpcode();
  if (MIOpC == PPC::ANDI_rec || MIOpC == PPC::ANDI8_rec ||
      MIOpC == PPC::ANDIS_rec || MIOpC == PPC::ANDIS8_rec)
    NewOpC = MIOpC;
  else {
    NewOpC = PPC::getRecordFormOpcode(MIOpC);
    if (NewOpC == -1 && PPC::getNonRecordFormOpcode(MIOpC) != -1)
      NewOpC = MIOpC;
  }

  // FIXME: On the non-embedded POWER architectures, only some of the record
  // forms are fast, and we should use only the fast ones.

  // The defining instruction has a record form (or is already a record
  // form). It is possible, however, that we'll need to reverse the condition
  // code of the users.
  if (NewOpC == -1)
    return false;

  // This transformation should not be performed if `nsw` is missing and is not
  // `equalityOnly` comparison. Since if there is overflow, sub_lt, sub_gt in
  // CRReg do not reflect correct order. If `equalityOnly` is true, sub_eq in
  // CRReg can reflect if compared values are equal, this optz is still valid.
  if (!equalityOnly && (NewOpC == PPC::SUBF_rec || NewOpC == PPC::SUBF8_rec) &&
      Sub && !Sub->getFlag(MachineInstr::NoSWrap))
    return false;

  // If we have SUB(r1, r2) and CMP(r2, r1), the condition code based on CMP
  // needs to be updated to be based on SUB.  Push the condition code
  // operands to OperandsToUpdate.  If it is safe to remove CmpInstr, the
  // condition code of these operands will be modified.
  // Here, Value == 0 means we haven't converted comparison against 1 or -1 to
  // comparison against 0, which may modify predicate.
  bool ShouldSwap = false;
  if (Sub && Value == 0) {
    ShouldSwap = SrcReg2 != 0 && Sub->getOperand(1).getReg() == SrcReg2 &&
      Sub->getOperand(2).getReg() == SrcReg;

    // The operands to subf are the opposite of sub, so only in the fixed-point
    // case, invert the order.
    ShouldSwap = !ShouldSwap;
  }

  if (ShouldSwap)
    for (MachineRegisterInfo::use_instr_iterator
         I = MRI->use_instr_begin(CRReg), IE = MRI->use_instr_end();
         I != IE; ++I) {
      MachineInstr *UseMI = &*I;
      if (UseMI->getOpcode() == PPC::BCC) {
        PPC::Predicate Pred = (PPC::Predicate) UseMI->getOperand(0).getImm();
        unsigned PredCond = PPC::getPredicateCondition(Pred);
        assert((!equalityOnly ||
                PredCond == PPC::PRED_EQ || PredCond == PPC::PRED_NE) &&
               "Invalid predicate for equality-only optimization");
        (void)PredCond; // To suppress warning in release build.
        PredsToUpdate.push_back(std::make_pair(&(UseMI->getOperand(0)),
                                PPC::getSwappedPredicate(Pred)));
      } else if (UseMI->getOpcode() == PPC::ISEL ||
                 UseMI->getOpcode() == PPC::ISEL8) {
        unsigned NewSubReg = UseMI->getOperand(3).getSubReg();
        assert((!equalityOnly || NewSubReg == PPC::sub_eq) &&
               "Invalid CR bit for equality-only optimization");

        if (NewSubReg == PPC::sub_lt)
          NewSubReg = PPC::sub_gt;
        else if (NewSubReg == PPC::sub_gt)
          NewSubReg = PPC::sub_lt;

        SubRegsToUpdate.push_back(std::make_pair(&(UseMI->getOperand(3)),
                                                 NewSubReg));
      } else // We need to abort on a user we don't understand.
        return false;
    }
  assert(!(Value != 0 && ShouldSwap) &&
         "Non-zero immediate support and ShouldSwap"
         "may conflict in updating predicate");

  // Create a new virtual register to hold the value of the CR set by the
  // record-form instruction. If the instruction was not previously in
  // record form, then set the kill flag on the CR.
  CmpInstr.eraseFromParent();

  MachineBasicBlock::iterator MII = MI;
  BuildMI(*MI->getParent(), std::next(MII), MI->getDebugLoc(),
          get(TargetOpcode::COPY), CRReg)
    .addReg(PPC::CR0, MIOpC != NewOpC ? RegState::Kill : 0);

  // Even if CR0 register were dead before, it is alive now since the
  // instruction we just built uses it.
  MI->clearRegisterDeads(PPC::CR0);

  if (MIOpC != NewOpC) {
    // We need to be careful here: we're replacing one instruction with
    // another, and we need to make sure that we get all of the right
    // implicit uses and defs. On the other hand, the caller may be holding
    // an iterator to this instruction, and so we can't delete it (this is
    // specifically the case if this is the instruction directly after the
    // compare).

    // Rotates are expensive instructions. If we're emitting a record-form
    // rotate that can just be an andi/andis, we should just emit that.
    if (MIOpC == PPC::RLWINM || MIOpC == PPC::RLWINM8) {
      Register GPRRes = MI->getOperand(0).getReg();
      int64_t SH = MI->getOperand(2).getImm();
      int64_t MB = MI->getOperand(3).getImm();
      int64_t ME = MI->getOperand(4).getImm();
      // We can only do this if both the start and end of the mask are in the
      // same halfword.
      bool MBInLoHWord = MB >= 16;
      bool MEInLoHWord = ME >= 16;
      uint64_t Mask = ~0LLU;

      if (MB <= ME && MBInLoHWord == MEInLoHWord && SH == 0) {
        Mask = ((1LLU << (32 - MB)) - 1) & ~((1LLU << (31 - ME)) - 1);
        // The mask value needs to shift right 16 if we're emitting andis.
        Mask >>= MBInLoHWord ? 0 : 16;
        NewOpC = MIOpC == PPC::RLWINM
                     ? (MBInLoHWord ? PPC::ANDI_rec : PPC::ANDIS_rec)
                     : (MBInLoHWord ? PPC::ANDI8_rec : PPC::ANDIS8_rec);
      } else if (MRI->use_empty(GPRRes) && (ME == 31) &&
                 (ME - MB + 1 == SH) && (MB >= 16)) {
        // If we are rotating by the exact number of bits as are in the mask
        // and the mask is in the least significant bits of the register,
        // that's just an andis. (as long as the GPR result has no uses).
        Mask = ((1LLU << 32) - 1) & ~((1LLU << (32 - SH)) - 1);
        Mask >>= 16;
        NewOpC = MIOpC == PPC::RLWINM ? PPC::ANDIS_rec : PPC::ANDIS8_rec;
      }
      // If we've set the mask, we can transform.
      if (Mask != ~0LLU) {
        MI->removeOperand(4);
        MI->removeOperand(3);
        MI->getOperand(2).setImm(Mask);
        NumRcRotatesConvertedToRcAnd++;
      }
    } else if (MIOpC == PPC::RLDICL && MI->getOperand(2).getImm() == 0) {
      int64_t MB = MI->getOperand(3).getImm();
      if (MB >= 48) {
        uint64_t Mask = (1LLU << (63 - MB + 1)) - 1;
        NewOpC = PPC::ANDI8_rec;
        MI->removeOperand(3);
        MI->getOperand(2).setImm(Mask);
        NumRcRotatesConvertedToRcAnd++;
      }
    }

    const MCInstrDesc &NewDesc = get(NewOpC);
    MI->setDesc(NewDesc);

    for (MCPhysReg ImpDef : NewDesc.implicit_defs()) {
      if (!MI->definesRegister(ImpDef, /*TRI=*/nullptr)) {
        MI->addOperand(*MI->getParent()->getParent(),
                       MachineOperand::CreateReg(ImpDef, true, true));
      }
    }
    for (MCPhysReg ImpUse : NewDesc.implicit_uses()) {
      if (!MI->readsRegister(ImpUse, /*TRI=*/nullptr)) {
        MI->addOperand(*MI->getParent()->getParent(),
                       MachineOperand::CreateReg(ImpUse, false, true));
      }
    }
  }
  assert(MI->definesRegister(PPC::CR0, /*TRI=*/nullptr) &&
         "Record-form instruction does not define cr0?");

  // Modify the condition code of operands in OperandsToUpdate.
  // Since we have SUB(r1, r2) and CMP(r2, r1), the condition code needs to
  // be changed from r2 > r1 to r1 < r2, from r2 < r1 to r1 > r2, etc.
  for (unsigned i = 0, e = PredsToUpdate.size(); i < e; i++)
    PredsToUpdate[i].first->setImm(PredsToUpdate[i].second);

  for (unsigned i = 0, e = SubRegsToUpdate.size(); i < e; i++)
    SubRegsToUpdate[i].first->setSubReg(SubRegsToUpdate[i].second);

  return true;
}

bool PPCInstrInfo::optimizeCmpPostRA(MachineInstr &CmpMI) const {
  MachineRegisterInfo *MRI = &CmpMI.getParent()->getParent()->getRegInfo();
  if (MRI->isSSA())
    return false;

  Register SrcReg, SrcReg2;
  int64_t CmpMask, CmpValue;
  if (!analyzeCompare(CmpMI, SrcReg, SrcReg2, CmpMask, CmpValue))
    return false;

  // Try to optimize the comparison against 0.
  if (CmpValue || !CmpMask || SrcReg2)
    return false;

  // The record forms set the condition register based on a signed comparison
  // with zero (see comments in optimizeCompareInstr). Since we can't do the
  // equality checks in post-RA, we are more restricted on a unsigned
  // comparison.
  unsigned Opc = CmpMI.getOpcode();
  if (Opc == PPC::CMPLWI || Opc == PPC::CMPLDI)
    return false;

  // The record forms are always based on a 64-bit comparison on PPC64
  // (similary, a 32-bit comparison on PPC32), while the CMPWI is a 32-bit
  // comparison. Since we can't do the equality checks in post-RA, we bail out
  // the case.
  if (Subtarget.isPPC64() && Opc == PPC::CMPWI)
    return false;

  // CmpMI can't be deleted if it has implicit def.
  if (CmpMI.hasImplicitDef())
    return false;

  bool SrcRegHasOtherUse = false;
  MachineInstr *SrcMI = getDefMIPostRA(SrcReg, CmpMI, SrcRegHasOtherUse);
  if (!SrcMI || !SrcMI->definesRegister(SrcReg, /*TRI=*/nullptr))
    return false;

  MachineOperand RegMO = CmpMI.getOperand(0);
  Register CRReg = RegMO.getReg();
  if (CRReg != PPC::CR0)
    return false;

  // Make sure there is no def/use of CRReg between SrcMI and CmpMI.
  bool SeenUseOfCRReg = false;
  bool IsCRRegKilled = false;
  if (!isRegElgibleForForwarding(RegMO, *SrcMI, CmpMI, false, IsCRRegKilled,
                                 SeenUseOfCRReg) ||
      SrcMI->definesRegister(CRReg, /*TRI=*/nullptr) || SeenUseOfCRReg)
    return false;

  int SrcMIOpc = SrcMI->getOpcode();
  int NewOpC = PPC::getRecordFormOpcode(SrcMIOpc);
  if (NewOpC == -1)
    return false;

  LLVM_DEBUG(dbgs() << "Replace Instr: ");
  LLVM_DEBUG(SrcMI->dump());

  const MCInstrDesc &NewDesc = get(NewOpC);
  SrcMI->setDesc(NewDesc);
  MachineInstrBuilder(*SrcMI->getParent()->getParent(), SrcMI)
      .addReg(CRReg, RegState::ImplicitDefine);
  SrcMI->clearRegisterDeads(CRReg);

  assert(SrcMI->definesRegister(PPC::CR0, /*TRI=*/nullptr) &&
         "Record-form instruction does not define cr0?");

  LLVM_DEBUG(dbgs() << "with: ");
  LLVM_DEBUG(SrcMI->dump());
  LLVM_DEBUG(dbgs() << "Delete dead instruction: ");
  LLVM_DEBUG(CmpMI.dump());
  return true;
}

bool PPCInstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &LdSt, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo *TRI) const {
  const MachineOperand *BaseOp;
  OffsetIsScalable = false;
  if (!getMemOperandWithOffsetWidth(LdSt, BaseOp, Offset, Width, TRI))
    return false;
  BaseOps.push_back(BaseOp);
  return true;
}

static bool isLdStSafeToCluster(const MachineInstr &LdSt,
                                const TargetRegisterInfo *TRI) {
  // If this is a volatile load/store, don't mess with it.
  if (LdSt.hasOrderedMemoryRef() || LdSt.getNumExplicitOperands() != 3)
    return false;

  if (LdSt.getOperand(2).isFI())
    return true;

  assert(LdSt.getOperand(2).isReg() && "Expected a reg operand.");
  // Can't cluster if the instruction modifies the base register
  // or it is update form. e.g. ld r2,3(r2)
  if (LdSt.modifiesRegister(LdSt.getOperand(2).getReg(), TRI))
    return false;

  return true;
}

// Only cluster instruction pair that have the same opcode, and they are
// clusterable according to PowerPC specification.
static bool isClusterableLdStOpcPair(unsigned FirstOpc, unsigned SecondOpc,
                                     const PPCSubtarget &Subtarget) {
  switch (FirstOpc) {
  default:
    return false;
  case PPC::STD:
  case PPC::STFD:
  case PPC::STXSD:
  case PPC::DFSTOREf64:
    return FirstOpc == SecondOpc;
  // PowerPC backend has opcode STW/STW8 for instruction "stw" to deal with
  // 32bit and 64bit instruction selection. They are clusterable pair though
  // they are different opcode.
  case PPC::STW:
  case PPC::STW8:
    return SecondOpc == PPC::STW || SecondOpc == PPC::STW8;
  }
}

bool PPCInstrInfo::shouldClusterMemOps(
    ArrayRef<const MachineOperand *> BaseOps1, int64_t OpOffset1,
    bool OffsetIsScalable1, ArrayRef<const MachineOperand *> BaseOps2,
    int64_t OpOffset2, bool OffsetIsScalable2, unsigned ClusterSize,
    unsigned NumBytes) const {

  assert(BaseOps1.size() == 1 && BaseOps2.size() == 1);
  const MachineOperand &BaseOp1 = *BaseOps1.front();
  const MachineOperand &BaseOp2 = *BaseOps2.front();
  assert((BaseOp1.isReg() || BaseOp1.isFI()) &&
         "Only base registers and frame indices are supported.");

  // ClusterSize means the number of memory operations that will have been
  // clustered if this hook returns true.
  // Don't cluster memory op if there are already two ops clustered at least.
  if (ClusterSize > 2)
    return false;

  // Cluster the load/store only when they have the same base
  // register or FI.
  if ((BaseOp1.isReg() != BaseOp2.isReg()) ||
      (BaseOp1.isReg() && BaseOp1.getReg() != BaseOp2.getReg()) ||
      (BaseOp1.isFI() && BaseOp1.getIndex() != BaseOp2.getIndex()))
    return false;

  // Check if the load/store are clusterable according to the PowerPC
  // specification.
  const MachineInstr &FirstLdSt = *BaseOp1.getParent();
  const MachineInstr &SecondLdSt = *BaseOp2.getParent();
  unsigned FirstOpc = FirstLdSt.getOpcode();
  unsigned SecondOpc = SecondLdSt.getOpcode();
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  // Cluster the load/store only when they have the same opcode, and they are
  // clusterable opcode according to PowerPC specification.
  if (!isClusterableLdStOpcPair(FirstOpc, SecondOpc, Subtarget))
    return false;

  // Can't cluster load/store that have ordered or volatile memory reference.
  if (!isLdStSafeToCluster(FirstLdSt, TRI) ||
      !isLdStSafeToCluster(SecondLdSt, TRI))
    return false;

  int64_t Offset1 = 0, Offset2 = 0;
  LocationSize Width1 = 0, Width2 = 0;
  const MachineOperand *Base1 = nullptr, *Base2 = nullptr;
  if (!getMemOperandWithOffsetWidth(FirstLdSt, Base1, Offset1, Width1, TRI) ||
      !getMemOperandWithOffsetWidth(SecondLdSt, Base2, Offset2, Width2, TRI) ||
      Width1 != Width2)
    return false;

  assert(Base1 == &BaseOp1 && Base2 == &BaseOp2 &&
         "getMemOperandWithOffsetWidth return incorrect base op");
  // The caller should already have ordered FirstMemOp/SecondMemOp by offset.
  assert(Offset1 <= Offset2 && "Caller should have ordered offsets.");
  return Offset1 + (int64_t)Width1.getValue() == Offset2;
}

/// GetInstSize - Return the number of bytes of code the specified
/// instruction may be.  This returns the maximum number of bytes.
///
unsigned PPCInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();

  if (Opcode == PPC::INLINEASM || Opcode == PPC::INLINEASM_BR) {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  } else if (Opcode == TargetOpcode::STACKMAP) {
    StackMapOpers Opers(&MI);
    return Opers.getNumPatchBytes();
  } else if (Opcode == TargetOpcode::PATCHPOINT) {
    PatchPointOpers Opers(&MI);
    return Opers.getNumPatchBytes();
  } else {
    return get(Opcode).getSize();
  }
}

std::pair<unsigned, unsigned>
PPCInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  // PPC always uses a direct mask.
  return std::make_pair(TF, 0u);
}

ArrayRef<std::pair<unsigned, const char *>>
PPCInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace PPCII;
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_PLT, "ppc-plt"},
      {MO_PIC_FLAG, "ppc-pic"},
      {MO_PCREL_FLAG, "ppc-pcrel"},
      {MO_GOT_FLAG, "ppc-got"},
      {MO_PCREL_OPT_FLAG, "ppc-opt-pcrel"},
      {MO_TLSGD_FLAG, "ppc-tlsgd"},
      {MO_TPREL_FLAG, "ppc-tprel"},
      {MO_TLSLDM_FLAG, "ppc-tlsldm"},
      {MO_TLSLD_FLAG, "ppc-tlsld"},
      {MO_TLSGDM_FLAG, "ppc-tlsgdm"},
      {MO_GOT_TLSGD_PCREL_FLAG, "ppc-got-tlsgd-pcrel"},
      {MO_GOT_TLSLD_PCREL_FLAG, "ppc-got-tlsld-pcrel"},
      {MO_GOT_TPREL_PCREL_FLAG, "ppc-got-tprel-pcrel"},
      {MO_LO, "ppc-lo"},
      {MO_HA, "ppc-ha"},
      {MO_TPREL_LO, "ppc-tprel-lo"},
      {MO_TPREL_HA, "ppc-tprel-ha"},
      {MO_DTPREL_LO, "ppc-dtprel-lo"},
      {MO_TLSLD_LO, "ppc-tlsld-lo"},
      {MO_TOC_LO, "ppc-toc-lo"},
      {MO_TLS, "ppc-tls"},
      {MO_PIC_HA_FLAG, "ppc-ha-pic"},
      {MO_PIC_LO_FLAG, "ppc-lo-pic"},
      {MO_TPREL_PCREL_FLAG, "ppc-tprel-pcrel"},
      {MO_TLS_PCREL_FLAG, "ppc-tls-pcrel"},
      {MO_GOT_PCREL_FLAG, "ppc-got-pcrel"},
  };
  return ArrayRef(TargetFlags);
}

// Expand VSX Memory Pseudo instruction to either a VSX or a FP instruction.
// The VSX versions have the advantage of a full 64-register target whereas
// the FP ones have the advantage of lower latency and higher throughput. So
// what we are after is using the faster instructions in low register pressure
// situations and using the larger register file in high register pressure
// situations.
bool PPCInstrInfo::expandVSXMemPseudo(MachineInstr &MI) const {
    unsigned UpperOpcode, LowerOpcode;
    switch (MI.getOpcode()) {
    case PPC::DFLOADf32:
      UpperOpcode = PPC::LXSSP;
      LowerOpcode = PPC::LFS;
      break;
    case PPC::DFLOADf64:
      UpperOpcode = PPC::LXSD;
      LowerOpcode = PPC::LFD;
      break;
    case PPC::DFSTOREf32:
      UpperOpcode = PPC::STXSSP;
      LowerOpcode = PPC::STFS;
      break;
    case PPC::DFSTOREf64:
      UpperOpcode = PPC::STXSD;
      LowerOpcode = PPC::STFD;
      break;
    case PPC::XFLOADf32:
      UpperOpcode = PPC::LXSSPX;
      LowerOpcode = PPC::LFSX;
      break;
    case PPC::XFLOADf64:
      UpperOpcode = PPC::LXSDX;
      LowerOpcode = PPC::LFDX;
      break;
    case PPC::XFSTOREf32:
      UpperOpcode = PPC::STXSSPX;
      LowerOpcode = PPC::STFSX;
      break;
    case PPC::XFSTOREf64:
      UpperOpcode = PPC::STXSDX;
      LowerOpcode = PPC::STFDX;
      break;
    case PPC::LIWAX:
      UpperOpcode = PPC::LXSIWAX;
      LowerOpcode = PPC::LFIWAX;
      break;
    case PPC::LIWZX:
      UpperOpcode = PPC::LXSIWZX;
      LowerOpcode = PPC::LFIWZX;
      break;
    case PPC::STIWX:
      UpperOpcode = PPC::STXSIWX;
      LowerOpcode = PPC::STFIWX;
      break;
    default:
      llvm_unreachable("Unknown Operation!");
    }

    Register TargetReg = MI.getOperand(0).getReg();
    unsigned Opcode;
    if ((TargetReg >= PPC::F0 && TargetReg <= PPC::F31) ||
        (TargetReg >= PPC::VSL0 && TargetReg <= PPC::VSL31))
      Opcode = LowerOpcode;
    else
      Opcode = UpperOpcode;
    MI.setDesc(get(Opcode));
    return true;
}

static bool isAnImmediateOperand(const MachineOperand &MO) {
  return MO.isCPI() || MO.isGlobal() || MO.isImm();
}

bool PPCInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  auto &MBB = *MI.getParent();
  auto DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  case PPC::BUILD_UACC: {
    MCRegister ACC = MI.getOperand(0).getReg();
    MCRegister UACC = MI.getOperand(1).getReg();
    if (ACC - PPC::ACC0 != UACC - PPC::UACC0) {
      MCRegister SrcVSR = PPC::VSL0 + (UACC - PPC::UACC0) * 4;
      MCRegister DstVSR = PPC::VSL0 + (ACC - PPC::ACC0) * 4;
      // FIXME: This can easily be improved to look up to the top of the MBB
      // to see if the inputs are XXLOR's. If they are and SrcReg is killed,
      // we can just re-target any such XXLOR's to DstVSR + offset.
      for (int VecNo = 0; VecNo < 4; VecNo++)
        BuildMI(MBB, MI, DL, get(PPC::XXLOR), DstVSR + VecNo)
            .addReg(SrcVSR + VecNo)
            .addReg(SrcVSR + VecNo);
    }
    // BUILD_UACC is expanded to 4 copies of the underlying vsx registers.
    // So after building the 4 copies, we can replace the BUILD_UACC instruction
    // with a NOP.
    [[fallthrough]];
  }
  case PPC::KILL_PAIR: {
    MI.setDesc(get(PPC::UNENCODED_NOP));
    MI.removeOperand(1);
    MI.removeOperand(0);
    return true;
  }
  case TargetOpcode::LOAD_STACK_GUARD: {
    assert(Subtarget.isTargetLinux() &&
           "Only Linux target is expected to contain LOAD_STACK_GUARD");
    const int64_t Offset = Subtarget.isPPC64() ? -0x7010 : -0x7008;
    const unsigned Reg = Subtarget.isPPC64() ? PPC::X13 : PPC::R2;
    MI.setDesc(get(Subtarget.isPPC64() ? PPC::LD : PPC::LWZ));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(Offset)
        .addReg(Reg);
    return true;
  }
  case PPC::PPCLdFixedAddr: {
    assert(Subtarget.getTargetTriple().isOSGlibc() &&
           "Only targets with Glibc expected to contain PPCLdFixedAddr");
    int64_t Offset = 0;
    const unsigned Reg = Subtarget.isPPC64() ? PPC::X13 : PPC::R2;
    MI.setDesc(get(PPC::LWZ));
    uint64_t FAType = MI.getOperand(1).getImm();
#undef PPC_LNX_FEATURE
#undef PPC_CPU
#define PPC_LNX_DEFINE_OFFSETS
#include "llvm/TargetParser/PPCTargetParser.def"
    bool IsLE = Subtarget.isLittleEndian();
    bool Is64 = Subtarget.isPPC64();
    if (FAType == PPC_FAWORD_HWCAP) {
      if (IsLE)
        Offset = Is64 ? PPC_HWCAP_OFFSET_LE64 : PPC_HWCAP_OFFSET_LE32;
      else
        Offset = Is64 ? PPC_HWCAP_OFFSET_BE64 : PPC_HWCAP_OFFSET_BE32;
    } else if (FAType == PPC_FAWORD_HWCAP2) {
      if (IsLE)
        Offset = Is64 ? PPC_HWCAP2_OFFSET_LE64 : PPC_HWCAP2_OFFSET_LE32;
      else
        Offset = Is64 ? PPC_HWCAP2_OFFSET_BE64 : PPC_HWCAP2_OFFSET_BE32;
    } else if (FAType == PPC_FAWORD_CPUID) {
      if (IsLE)
        Offset = Is64 ? PPC_CPUID_OFFSET_LE64 : PPC_CPUID_OFFSET_LE32;
      else
        Offset = Is64 ? PPC_CPUID_OFFSET_BE64 : PPC_CPUID_OFFSET_BE32;
    }
    assert(Offset && "Do not know the offset for this fixed addr load");
    MI.removeOperand(1);
    Subtarget.getTargetMachine().setGlibcHWCAPAccess();
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(Offset)
        .addReg(Reg);
    return true;
#define PPC_TGT_PARSER_UNDEF_MACROS
#include "llvm/TargetParser/PPCTargetParser.def"
#undef PPC_TGT_PARSER_UNDEF_MACROS
  }
  case PPC::DFLOADf32:
  case PPC::DFLOADf64:
  case PPC::DFSTOREf32:
  case PPC::DFSTOREf64: {
    assert(Subtarget.hasP9Vector() &&
           "Invalid D-Form Pseudo-ops on Pre-P9 target.");
    assert(MI.getOperand(2).isReg() &&
           isAnImmediateOperand(MI.getOperand(1)) &&
           "D-form op must have register and immediate operands");
    return expandVSXMemPseudo(MI);
  }
  case PPC::XFLOADf32:
  case PPC::XFSTOREf32:
  case PPC::LIWAX:
  case PPC::LIWZX:
  case PPC::STIWX: {
    assert(Subtarget.hasP8Vector() &&
           "Invalid X-Form Pseudo-ops on Pre-P8 target.");
    assert(MI.getOperand(2).isReg() && MI.getOperand(1).isReg() &&
           "X-form op must have register and register operands");
    return expandVSXMemPseudo(MI);
  }
  case PPC::XFLOADf64:
  case PPC::XFSTOREf64: {
    assert(Subtarget.hasVSX() &&
           "Invalid X-Form Pseudo-ops on target that has no VSX.");
    assert(MI.getOperand(2).isReg() && MI.getOperand(1).isReg() &&
           "X-form op must have register and register operands");
    return expandVSXMemPseudo(MI);
  }
  case PPC::SPILLTOVSR_LD: {
    Register TargetReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(TargetReg)) {
      MI.setDesc(get(PPC::DFLOADf64));
      return expandPostRAPseudo(MI);
    }
    else
      MI.setDesc(get(PPC::LD));
    return true;
  }
  case PPC::SPILLTOVSR_ST: {
    Register SrcReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(SrcReg)) {
      NumStoreSPILLVSRRCAsVec++;
      MI.setDesc(get(PPC::DFSTOREf64));
      return expandPostRAPseudo(MI);
    } else {
      NumStoreSPILLVSRRCAsGpr++;
      MI.setDesc(get(PPC::STD));
    }
    return true;
  }
  case PPC::SPILLTOVSR_LDX: {
    Register TargetReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(TargetReg))
      MI.setDesc(get(PPC::LXSDX));
    else
      MI.setDesc(get(PPC::LDX));
    return true;
  }
  case PPC::SPILLTOVSR_STX: {
    Register SrcReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(SrcReg)) {
      NumStoreSPILLVSRRCAsVec++;
      MI.setDesc(get(PPC::STXSDX));
    } else {
      NumStoreSPILLVSRRCAsGpr++;
      MI.setDesc(get(PPC::STDX));
    }
    return true;
  }

    // FIXME: Maybe we can expand it in 'PowerPC Expand Atomic' pass.
  case PPC::CFENCE:
  case PPC::CFENCE8: {
    auto Val = MI.getOperand(0).getReg();
    unsigned CmpOp = Subtarget.isPPC64() ? PPC::CMPD : PPC::CMPW;
    BuildMI(MBB, MI, DL, get(CmpOp), PPC::CR7).addReg(Val).addReg(Val);
    BuildMI(MBB, MI, DL, get(PPC::CTRL_DEP))
        .addImm(PPC::PRED_NE_MINUS)
        .addReg(PPC::CR7)
        .addImm(1);
    MI.setDesc(get(PPC::ISYNC));
    MI.removeOperand(0);
    return true;
  }
  }
  return false;
}

// Essentially a compile-time implementation of a compare->isel sequence.
// It takes two constants to compare, along with the true/false registers
// and the comparison type (as a subreg to a CR field) and returns one
// of the true/false registers, depending on the comparison results.
static unsigned selectReg(int64_t Imm1, int64_t Imm2, unsigned CompareOpc,
                          unsigned TrueReg, unsigned FalseReg,
                          unsigned CRSubReg) {
  // Signed comparisons. The immediates are assumed to be sign-extended.
  if (CompareOpc == PPC::CMPWI || CompareOpc == PPC::CMPDI) {
    switch (CRSubReg) {
    default: llvm_unreachable("Unknown integer comparison type.");
    case PPC::sub_lt:
      return Imm1 < Imm2 ? TrueReg : FalseReg;
    case PPC::sub_gt:
      return Imm1 > Imm2 ? TrueReg : FalseReg;
    case PPC::sub_eq:
      return Imm1 == Imm2 ? TrueReg : FalseReg;
    }
  }
  // Unsigned comparisons.
  else if (CompareOpc == PPC::CMPLWI || CompareOpc == PPC::CMPLDI) {
    switch (CRSubReg) {
    default: llvm_unreachable("Unknown integer comparison type.");
    case PPC::sub_lt:
      return (uint64_t)Imm1 < (uint64_t)Imm2 ? TrueReg : FalseReg;
    case PPC::sub_gt:
      return (uint64_t)Imm1 > (uint64_t)Imm2 ? TrueReg : FalseReg;
    case PPC::sub_eq:
      return Imm1 == Imm2 ? TrueReg : FalseReg;
    }
  }
  return PPC::NoRegister;
}

void PPCInstrInfo::replaceInstrOperandWithImm(MachineInstr &MI,
                                              unsigned OpNo,
                                              int64_t Imm) const {
  assert(MI.getOperand(OpNo).isReg() && "Operand must be a REG");
  // Replace the REG with the Immediate.
  Register InUseReg = MI.getOperand(OpNo).getReg();
  MI.getOperand(OpNo).ChangeToImmediate(Imm);

  // We need to make sure that the MI didn't have any implicit use
  // of this REG any more. We don't call MI.implicit_operands().empty() to
  // return early, since MI's MCID might be changed in calling context, as a
  // result its number of explicit operands may be changed, thus the begin of
  // implicit operand is changed.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  int UseOpIdx = MI.findRegisterUseOperandIdx(InUseReg, TRI, false);
  if (UseOpIdx >= 0) {
    MachineOperand &MO = MI.getOperand(UseOpIdx);
    if (MO.isImplicit())
      // The operands must always be in the following order:
      // - explicit reg defs,
      // - other explicit operands (reg uses, immediates, etc.),
      // - implicit reg defs
      // - implicit reg uses
      // Therefore, removing the implicit operand won't change the explicit
      // operands layout.
      MI.removeOperand(UseOpIdx);
  }
}

// Replace an instruction with one that materializes a constant (and sets
// CR0 if the original instruction was a record-form instruction).
void PPCInstrInfo::replaceInstrWithLI(MachineInstr &MI,
                                      const LoadImmediateInfo &LII) const {
  // Remove existing operands.
  int OperandToKeep = LII.SetCR ? 1 : 0;
  for (int i = MI.getNumOperands() - 1; i > OperandToKeep; i--)
    MI.removeOperand(i);

  // Replace the instruction.
  if (LII.SetCR) {
    MI.setDesc(get(LII.Is64Bit ? PPC::ANDI8_rec : PPC::ANDI_rec));
    // Set the immediate.
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(LII.Imm).addReg(PPC::CR0, RegState::ImplicitDefine);
    return;
  }
  else
    MI.setDesc(get(LII.Is64Bit ? PPC::LI8 : PPC::LI));

  // Set the immediate.
  MachineInstrBuilder(*MI.getParent()->getParent(), MI)
      .addImm(LII.Imm);
}

MachineInstr *PPCInstrInfo::getDefMIPostRA(unsigned Reg, MachineInstr &MI,
                                           bool &SeenIntermediateUse) const {
  assert(!MI.getParent()->getParent()->getRegInfo().isSSA() &&
         "Should be called after register allocation.");
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  MachineBasicBlock::reverse_iterator E = MI.getParent()->rend(), It = MI;
  It++;
  SeenIntermediateUse = false;
  for (; It != E; ++It) {
    if (It->modifiesRegister(Reg, TRI))
      return &*It;
    if (It->readsRegister(Reg, TRI))
      SeenIntermediateUse = true;
  }
  return nullptr;
}

void PPCInstrInfo::materializeImmPostRA(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const DebugLoc &DL, Register Reg,
                                        int64_t Imm) const {
  assert(!MBB.getParent()->getRegInfo().isSSA() &&
         "Register should be in non-SSA form after RA");
  bool isPPC64 = Subtarget.isPPC64();
  // FIXME: Materialization here is not optimal.
  // For some special bit patterns we can use less instructions.
  // See `selectI64ImmDirect` in PPCISelDAGToDAG.cpp.
  if (isInt<16>(Imm)) {
    BuildMI(MBB, MBBI, DL, get(isPPC64 ? PPC::LI8 : PPC::LI), Reg).addImm(Imm);
  } else if (isInt<32>(Imm)) {
    BuildMI(MBB, MBBI, DL, get(isPPC64 ? PPC::LIS8 : PPC::LIS), Reg)
        .addImm(Imm >> 16);
    if (Imm & 0xFFFF)
      BuildMI(MBB, MBBI, DL, get(isPPC64 ? PPC::ORI8 : PPC::ORI), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(Imm & 0xFFFF);
  } else {
    assert(isPPC64 && "Materializing 64-bit immediate to single register is "
                      "only supported in PPC64");
    BuildMI(MBB, MBBI, DL, get(PPC::LIS8), Reg).addImm(Imm >> 48);
    if ((Imm >> 32) & 0xFFFF)
      BuildMI(MBB, MBBI, DL, get(PPC::ORI8), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm((Imm >> 32) & 0xFFFF);
    BuildMI(MBB, MBBI, DL, get(PPC::RLDICR), Reg)
        .addReg(Reg, RegState::Kill)
        .addImm(32)
        .addImm(31);
    BuildMI(MBB, MBBI, DL, get(PPC::ORIS8), Reg)
        .addReg(Reg, RegState::Kill)
        .addImm((Imm >> 16) & 0xFFFF);
    if (Imm & 0xFFFF)
      BuildMI(MBB, MBBI, DL, get(PPC::ORI8), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(Imm & 0xFFFF);
  }
}

MachineInstr *PPCInstrInfo::getForwardingDefMI(
  MachineInstr &MI,
  unsigned &OpNoForForwarding,
  bool &SeenIntermediateUse) const {
  OpNoForForwarding = ~0U;
  MachineInstr *DefMI = nullptr;
  MachineRegisterInfo *MRI = &MI.getParent()->getParent()->getRegInfo();
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  // If we're in SSA, get the defs through the MRI. Otherwise, only look
  // within the basic block to see if the register is defined using an
  // LI/LI8/ADDI/ADDI8.
  if (MRI->isSSA()) {
    for (int i = 1, e = MI.getNumOperands(); i < e; i++) {
      if (!MI.getOperand(i).isReg())
        continue;
      Register Reg = MI.getOperand(i).getReg();
      if (!Reg.isVirtual())
        continue;
      Register TrueReg = TRI->lookThruCopyLike(Reg, MRI);
      if (TrueReg.isVirtual()) {
        MachineInstr *DefMIForTrueReg = MRI->getVRegDef(TrueReg);
        if (DefMIForTrueReg->getOpcode() == PPC::LI ||
            DefMIForTrueReg->getOpcode() == PPC::LI8 ||
            DefMIForTrueReg->getOpcode() == PPC::ADDI ||
            DefMIForTrueReg->getOpcode() == PPC::ADDI8) {
          OpNoForForwarding = i;
          DefMI = DefMIForTrueReg;
          // The ADDI and LI operand maybe exist in one instruction at same
          // time. we prefer to fold LI operand as LI only has one Imm operand
          // and is more possible to be converted. So if current DefMI is
          // ADDI/ADDI8, we continue to find possible LI/LI8.
          if (DefMI->getOpcode() == PPC::LI || DefMI->getOpcode() == PPC::LI8)
            break;
        }
      }
    }
  } else {
    // Looking back through the definition for each operand could be expensive,
    // so exit early if this isn't an instruction that either has an immediate
    // form or is already an immediate form that we can handle.
    ImmInstrInfo III;
    unsigned Opc = MI.getOpcode();
    bool ConvertibleImmForm =
        Opc == PPC::CMPWI || Opc == PPC::CMPLWI || Opc == PPC::CMPDI ||
        Opc == PPC::CMPLDI || Opc == PPC::ADDI || Opc == PPC::ADDI8 ||
        Opc == PPC::ORI || Opc == PPC::ORI8 || Opc == PPC::XORI ||
        Opc == PPC::XORI8 || Opc == PPC::RLDICL || Opc == PPC::RLDICL_rec ||
        Opc == PPC::RLDICL_32 || Opc == PPC::RLDICL_32_64 ||
        Opc == PPC::RLWINM || Opc == PPC::RLWINM_rec || Opc == PPC::RLWINM8 ||
        Opc == PPC::RLWINM8_rec;
    bool IsVFReg = (MI.getNumOperands() && MI.getOperand(0).isReg())
                       ? PPC::isVFRegister(MI.getOperand(0).getReg())
                       : false;
    if (!ConvertibleImmForm && !instrHasImmForm(Opc, IsVFReg, III, true))
      return nullptr;

    // Don't convert or %X, %Y, %Y since that's just a register move.
    if ((Opc == PPC::OR || Opc == PPC::OR8) &&
        MI.getOperand(1).getReg() == MI.getOperand(2).getReg())
      return nullptr;
    for (int i = 1, e = MI.getNumOperands(); i < e; i++) {
      MachineOperand &MO = MI.getOperand(i);
      SeenIntermediateUse = false;
      if (MO.isReg() && MO.isUse() && !MO.isImplicit()) {
        Register Reg = MI.getOperand(i).getReg();
        // If we see another use of this reg between the def and the MI,
        // we want to flag it so the def isn't deleted.
        MachineInstr *DefMI = getDefMIPostRA(Reg, MI, SeenIntermediateUse);
        if (DefMI) {
          // Is this register defined by some form of add-immediate (including
          // load-immediate) within this basic block?
          switch (DefMI->getOpcode()) {
          default:
            break;
          case PPC::LI:
          case PPC::LI8:
          case PPC::ADDItocL8:
          case PPC::ADDI:
          case PPC::ADDI8:
            OpNoForForwarding = i;
            return DefMI;
          }
        }
      }
    }
  }
  return OpNoForForwarding == ~0U ? nullptr : DefMI;
}

unsigned PPCInstrInfo::getSpillTarget() const {
  // With P10, we may need to spill paired vector registers or accumulator
  // registers. MMA implies paired vectors, so we can just check that.
  bool IsP10Variant = Subtarget.isISA3_1() || Subtarget.pairedVectorMemops();
  // P11 uses the P10 target.
  return Subtarget.isISAFuture() ? 3 : IsP10Variant ?
                                   2 : Subtarget.hasP9Vector() ?
                                   1 : 0;
}

ArrayRef<unsigned> PPCInstrInfo::getStoreOpcodesForSpillArray() const {
  return {StoreSpillOpcodesArray[getSpillTarget()], SOK_LastOpcodeSpill};
}

ArrayRef<unsigned> PPCInstrInfo::getLoadOpcodesForSpillArray() const {
  return {LoadSpillOpcodesArray[getSpillTarget()], SOK_LastOpcodeSpill};
}

// This opt tries to convert the following imm form to an index form to save an
// add for stack variables.
// Return false if no such pattern found.
//
// ADDI instr: ToBeChangedReg = ADDI FrameBaseReg, OffsetAddi
// ADD instr:  ToBeDeletedReg = ADD ToBeChangedReg(killed), ScaleReg
// Imm instr:  Reg            = op OffsetImm, ToBeDeletedReg(killed)
//
// can be converted to:
//
// new ADDI instr: ToBeChangedReg = ADDI FrameBaseReg, (OffsetAddi + OffsetImm)
// Index instr:    Reg            = opx ScaleReg, ToBeChangedReg(killed)
//
// In order to eliminate ADD instr, make sure that:
// 1: (OffsetAddi + OffsetImm) must be int16 since this offset will be used in
//    new ADDI instr and ADDI can only take int16 Imm.
// 2: ToBeChangedReg must be killed in ADD instr and there is no other use
//    between ADDI and ADD instr since its original def in ADDI will be changed
//    in new ADDI instr. And also there should be no new def for it between
//    ADD and Imm instr as ToBeChangedReg will be used in Index instr.
// 3: ToBeDeletedReg must be killed in Imm instr and there is no other use
//    between ADD and Imm instr since ADD instr will be eliminated.
// 4: ScaleReg must not be redefined between ADD and Imm instr since it will be
//    moved to Index instr.
bool PPCInstrInfo::foldFrameOffset(MachineInstr &MI) const {
  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  bool PostRA = !MRI->isSSA();
  // Do this opt after PEI which is after RA. The reason is stack slot expansion
  // in PEI may expose such opportunities since in PEI, stack slot offsets to
  // frame base(OffsetAddi) are determined.
  if (!PostRA)
    return false;
  unsigned ToBeDeletedReg = 0;
  int64_t OffsetImm = 0;
  unsigned XFormOpcode = 0;
  ImmInstrInfo III;

  // Check if Imm instr meets requirement.
  if (!isImmInstrEligibleForFolding(MI, ToBeDeletedReg, XFormOpcode, OffsetImm,
                                    III))
    return false;

  bool OtherIntermediateUse = false;
  MachineInstr *ADDMI = getDefMIPostRA(ToBeDeletedReg, MI, OtherIntermediateUse);

  // Exit if there is other use between ADD and Imm instr or no def found.
  if (OtherIntermediateUse || !ADDMI)
    return false;

  // Check if ADD instr meets requirement.
  if (!isADDInstrEligibleForFolding(*ADDMI))
    return false;

  unsigned ScaleRegIdx = 0;
  int64_t OffsetAddi = 0;
  MachineInstr *ADDIMI = nullptr;

  // Check if there is a valid ToBeChangedReg in ADDMI.
  // 1: It must be killed.
  // 2: Its definition must be a valid ADDIMI.
  // 3: It must satify int16 offset requirement.
  if (isValidToBeChangedReg(ADDMI, 1, ADDIMI, OffsetAddi, OffsetImm))
    ScaleRegIdx = 2;
  else if (isValidToBeChangedReg(ADDMI, 2, ADDIMI, OffsetAddi, OffsetImm))
    ScaleRegIdx = 1;
  else
    return false;

  assert(ADDIMI && "There should be ADDIMI for valid ToBeChangedReg.");
  Register ToBeChangedReg = ADDIMI->getOperand(0).getReg();
  Register ScaleReg = ADDMI->getOperand(ScaleRegIdx).getReg();
  auto NewDefFor = [&](unsigned Reg, MachineBasicBlock::iterator Start,
                       MachineBasicBlock::iterator End) {
    for (auto It = ++Start; It != End; It++)
      if (It->modifiesRegister(Reg, &getRegisterInfo()))
        return true;
    return false;
  };

  // We are trying to replace the ImmOpNo with ScaleReg. Give up if it is
  // treated as special zero when ScaleReg is R0/X0 register.
  if (III.ZeroIsSpecialOrig == III.ImmOpNo &&
      (ScaleReg == PPC::R0 || ScaleReg == PPC::X0))
    return false;

  // Make sure no other def for ToBeChangedReg and ScaleReg between ADD Instr
  // and Imm Instr.
  if (NewDefFor(ToBeChangedReg, *ADDMI, MI) || NewDefFor(ScaleReg, *ADDMI, MI))
    return false;

  // Now start to do the transformation.
  LLVM_DEBUG(dbgs() << "Replace instruction: "
                    << "\n");
  LLVM_DEBUG(ADDIMI->dump());
  LLVM_DEBUG(ADDMI->dump());
  LLVM_DEBUG(MI.dump());
  LLVM_DEBUG(dbgs() << "with: "
                    << "\n");

  // Update ADDI instr.
  ADDIMI->getOperand(2).setImm(OffsetAddi + OffsetImm);

  // Update Imm instr.
  MI.setDesc(get(XFormOpcode));
  MI.getOperand(III.ImmOpNo)
      .ChangeToRegister(ScaleReg, false, false,
                        ADDMI->getOperand(ScaleRegIdx).isKill());

  MI.getOperand(III.OpNoForForwarding)
      .ChangeToRegister(ToBeChangedReg, false, false, true);

  // Eliminate ADD instr.
  ADDMI->eraseFromParent();

  LLVM_DEBUG(ADDIMI->dump());
  LLVM_DEBUG(MI.dump());

  return true;
}

bool PPCInstrInfo::isADDIInstrEligibleForFolding(MachineInstr &ADDIMI,
                                                 int64_t &Imm) const {
  unsigned Opc = ADDIMI.getOpcode();

  // Exit if the instruction is not ADDI.
  if (Opc != PPC::ADDI && Opc != PPC::ADDI8)
    return false;

  // The operand may not necessarily be an immediate - it could be a relocation.
  if (!ADDIMI.getOperand(2).isImm())
    return false;

  Imm = ADDIMI.getOperand(2).getImm();

  return true;
}

bool PPCInstrInfo::isADDInstrEligibleForFolding(MachineInstr &ADDMI) const {
  unsigned Opc = ADDMI.getOpcode();

  // Exit if the instruction is not ADD.
  return Opc == PPC::ADD4 || Opc == PPC::ADD8;
}

bool PPCInstrInfo::isImmInstrEligibleForFolding(MachineInstr &MI,
                                                unsigned &ToBeDeletedReg,
                                                unsigned &XFormOpcode,
                                                int64_t &OffsetImm,
                                                ImmInstrInfo &III) const {
  // Only handle load/store.
  if (!MI.mayLoadOrStore())
    return false;

  unsigned Opc = MI.getOpcode();

  XFormOpcode = RI.getMappedIdxOpcForImmOpc(Opc);

  // Exit if instruction has no index form.
  if (XFormOpcode == PPC::INSTRUCTION_LIST_END)
    return false;

  // TODO: sync the logic between instrHasImmForm() and ImmToIdxMap.
  if (!instrHasImmForm(XFormOpcode,
                       PPC::isVFRegister(MI.getOperand(0).getReg()), III, true))
    return false;

  if (!III.IsSummingOperands)
    return false;

  MachineOperand ImmOperand = MI.getOperand(III.ImmOpNo);
  MachineOperand RegOperand = MI.getOperand(III.OpNoForForwarding);
  // Only support imm operands, not relocation slots or others.
  if (!ImmOperand.isImm())
    return false;

  assert(RegOperand.isReg() && "Instruction format is not right");

  // There are other use for ToBeDeletedReg after Imm instr, can not delete it.
  if (!RegOperand.isKill())
    return false;

  ToBeDeletedReg = RegOperand.getReg();
  OffsetImm = ImmOperand.getImm();

  return true;
}

bool PPCInstrInfo::isValidToBeChangedReg(MachineInstr *ADDMI, unsigned Index,
                                         MachineInstr *&ADDIMI,
                                         int64_t &OffsetAddi,
                                         int64_t OffsetImm) const {
  assert((Index == 1 || Index == 2) && "Invalid operand index for add.");
  MachineOperand &MO = ADDMI->getOperand(Index);

  if (!MO.isKill())
    return false;

  bool OtherIntermediateUse = false;

  ADDIMI = getDefMIPostRA(MO.getReg(), *ADDMI, OtherIntermediateUse);
  // Currently handle only one "add + Imminstr" pair case, exit if other
  // intermediate use for ToBeChangedReg found.
  // TODO: handle the cases where there are other "add + Imminstr" pairs
  // with same offset in Imminstr which is like:
  //
  // ADDI instr: ToBeChangedReg  = ADDI FrameBaseReg, OffsetAddi
  // ADD instr1: ToBeDeletedReg1 = ADD ToBeChangedReg, ScaleReg1
  // Imm instr1: Reg1            = op1 OffsetImm, ToBeDeletedReg1(killed)
  // ADD instr2: ToBeDeletedReg2 = ADD ToBeChangedReg(killed), ScaleReg2
  // Imm instr2: Reg2            = op2 OffsetImm, ToBeDeletedReg2(killed)
  //
  // can be converted to:
  //
  // new ADDI instr: ToBeChangedReg = ADDI FrameBaseReg,
  //                                       (OffsetAddi + OffsetImm)
  // Index instr1:   Reg1           = opx1 ScaleReg1, ToBeChangedReg
  // Index instr2:   Reg2           = opx2 ScaleReg2, ToBeChangedReg(killed)

  if (OtherIntermediateUse || !ADDIMI)
    return false;
  // Check if ADDI instr meets requirement.
  if (!isADDIInstrEligibleForFolding(*ADDIMI, OffsetAddi))
    return false;

  if (isInt<16>(OffsetAddi + OffsetImm))
    return true;
  return false;
}

// If this instruction has an immediate form and one of its operands is a
// result of a load-immediate or an add-immediate, convert it to
// the immediate form if the constant is in range.
bool PPCInstrInfo::convertToImmediateForm(MachineInstr &MI,
                                          SmallSet<Register, 4> &RegsToUpdate,
                                          MachineInstr **KilledDef) const {
  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  bool PostRA = !MRI->isSSA();
  bool SeenIntermediateUse = true;
  unsigned ForwardingOperand = ~0U;
  MachineInstr *DefMI = getForwardingDefMI(MI, ForwardingOperand,
                                           SeenIntermediateUse);
  if (!DefMI)
    return false;
  assert(ForwardingOperand < MI.getNumOperands() &&
         "The forwarding operand needs to be valid at this point");
  bool IsForwardingOperandKilled = MI.getOperand(ForwardingOperand).isKill();
  bool KillFwdDefMI = !SeenIntermediateUse && IsForwardingOperandKilled;
  if (KilledDef && KillFwdDefMI)
    *KilledDef = DefMI;

  // Conservatively add defs from DefMI and defs/uses from MI to the set of
  // registers that need their kill flags updated.
  for (const MachineOperand &MO : DefMI->operands())
    if (MO.isReg() && MO.isDef())
      RegsToUpdate.insert(MO.getReg());
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg())
      RegsToUpdate.insert(MO.getReg());

  // If this is a imm instruction and its register operands is produced by ADDI,
  // put the imm into imm inst directly.
  if (RI.getMappedIdxOpcForImmOpc(MI.getOpcode()) !=
          PPC::INSTRUCTION_LIST_END &&
      transformToNewImmFormFedByAdd(MI, *DefMI, ForwardingOperand))
    return true;

  ImmInstrInfo III;
  bool IsVFReg = MI.getOperand(0).isReg()
                     ? PPC::isVFRegister(MI.getOperand(0).getReg())
                     : false;
  bool HasImmForm = instrHasImmForm(MI.getOpcode(), IsVFReg, III, PostRA);
  // If this is a reg+reg instruction that has a reg+imm form,
  // and one of the operands is produced by an add-immediate,
  // try to convert it.
  if (HasImmForm &&
      transformToImmFormFedByAdd(MI, III, ForwardingOperand, *DefMI,
                                 KillFwdDefMI))
    return true;

  // If this is a reg+reg instruction that has a reg+imm form,
  // and one of the operands is produced by LI, convert it now.
  if (HasImmForm &&
      transformToImmFormFedByLI(MI, III, ForwardingOperand, *DefMI))
    return true;

  // If this is not a reg+reg, but the DefMI is LI/LI8, check if its user MI
  // can be simpified to LI.
  if (!HasImmForm && simplifyToLI(MI, *DefMI, ForwardingOperand, KilledDef))
    return true;

  return false;
}

bool PPCInstrInfo::combineRLWINM(MachineInstr &MI,
                                 MachineInstr **ToErase) const {
  MachineRegisterInfo *MRI = &MI.getParent()->getParent()->getRegInfo();
  Register FoldingReg = MI.getOperand(1).getReg();
  if (!FoldingReg.isVirtual())
    return false;
  MachineInstr *SrcMI = MRI->getVRegDef(FoldingReg);
  if (SrcMI->getOpcode() != PPC::RLWINM &&
      SrcMI->getOpcode() != PPC::RLWINM_rec &&
      SrcMI->getOpcode() != PPC::RLWINM8 &&
      SrcMI->getOpcode() != PPC::RLWINM8_rec)
    return false;
  assert((MI.getOperand(2).isImm() && MI.getOperand(3).isImm() &&
          MI.getOperand(4).isImm() && SrcMI->getOperand(2).isImm() &&
          SrcMI->getOperand(3).isImm() && SrcMI->getOperand(4).isImm()) &&
         "Invalid PPC::RLWINM Instruction!");
  uint64_t SHSrc = SrcMI->getOperand(2).getImm();
  uint64_t SHMI = MI.getOperand(2).getImm();
  uint64_t MBSrc = SrcMI->getOperand(3).getImm();
  uint64_t MBMI = MI.getOperand(3).getImm();
  uint64_t MESrc = SrcMI->getOperand(4).getImm();
  uint64_t MEMI = MI.getOperand(4).getImm();

  assert((MEMI < 32 && MESrc < 32 && MBMI < 32 && MBSrc < 32) &&
         "Invalid PPC::RLWINM Instruction!");
  // If MBMI is bigger than MEMI, we always can not get run of ones.
  // RotatedSrcMask non-wrap:
  //                 0........31|32........63
  // RotatedSrcMask:   B---E        B---E
  // MaskMI:         -----------|--E  B------
  // Result:           -----          ---      (Bad candidate)
  //
  // RotatedSrcMask wrap:
  //                 0........31|32........63
  // RotatedSrcMask: --E   B----|--E    B----
  // MaskMI:         -----------|--E  B------
  // Result:         ---   -----|---    -----  (Bad candidate)
  //
  // One special case is RotatedSrcMask is a full set mask.
  // RotatedSrcMask full:
  //                 0........31|32........63
  // RotatedSrcMask: ------EB---|-------EB---
  // MaskMI:         -----------|--E  B------
  // Result:         -----------|---  -------  (Good candidate)

  // Mark special case.
  bool SrcMaskFull = (MBSrc - MESrc == 1) || (MBSrc == 0 && MESrc == 31);

  // For other MBMI > MEMI cases, just return.
  if ((MBMI > MEMI) && !SrcMaskFull)
    return false;

  // Handle MBMI <= MEMI cases.
  APInt MaskMI = APInt::getBitsSetWithWrap(32, 32 - MEMI - 1, 32 - MBMI);
  // In MI, we only need low 32 bits of SrcMI, just consider about low 32
  // bit of SrcMI mask. Note that in APInt, lowerest bit is at index 0,
  // while in PowerPC ISA, lowerest bit is at index 63.
  APInt MaskSrc = APInt::getBitsSetWithWrap(32, 32 - MESrc - 1, 32 - MBSrc);

  APInt RotatedSrcMask = MaskSrc.rotl(SHMI);
  APInt FinalMask = RotatedSrcMask & MaskMI;
  uint32_t NewMB, NewME;
  bool Simplified = false;

  // If final mask is 0, MI result should be 0 too.
  if (FinalMask.isZero()) {
    bool Is64Bit =
        (MI.getOpcode() == PPC::RLWINM8 || MI.getOpcode() == PPC::RLWINM8_rec);
    Simplified = true;
    LLVM_DEBUG(dbgs() << "Replace Instr: ");
    LLVM_DEBUG(MI.dump());

    if (MI.getOpcode() == PPC::RLWINM || MI.getOpcode() == PPC::RLWINM8) {
      // Replace MI with "LI 0"
      MI.removeOperand(4);
      MI.removeOperand(3);
      MI.removeOperand(2);
      MI.getOperand(1).ChangeToImmediate(0);
      MI.setDesc(get(Is64Bit ? PPC::LI8 : PPC::LI));
    } else {
      // Replace MI with "ANDI_rec reg, 0"
      MI.removeOperand(4);
      MI.removeOperand(3);
      MI.getOperand(2).setImm(0);
      MI.setDesc(get(Is64Bit ? PPC::ANDI8_rec : PPC::ANDI_rec));
      MI.getOperand(1).setReg(SrcMI->getOperand(1).getReg());
      if (SrcMI->getOperand(1).isKill()) {
        MI.getOperand(1).setIsKill(true);
        SrcMI->getOperand(1).setIsKill(false);
      } else
        // About to replace MI.getOperand(1), clear its kill flag.
        MI.getOperand(1).setIsKill(false);
    }

    LLVM_DEBUG(dbgs() << "With: ");
    LLVM_DEBUG(MI.dump());

  } else if ((isRunOfOnes((unsigned)(FinalMask.getZExtValue()), NewMB, NewME) &&
              NewMB <= NewME) ||
             SrcMaskFull) {
    // Here we only handle MBMI <= MEMI case, so NewMB must be no bigger
    // than NewME. Otherwise we get a 64 bit value after folding, but MI
    // return a 32 bit value.
    Simplified = true;
    LLVM_DEBUG(dbgs() << "Converting Instr: ");
    LLVM_DEBUG(MI.dump());

    uint16_t NewSH = (SHSrc + SHMI) % 32;
    MI.getOperand(2).setImm(NewSH);
    // If SrcMI mask is full, no need to update MBMI and MEMI.
    if (!SrcMaskFull) {
      MI.getOperand(3).setImm(NewMB);
      MI.getOperand(4).setImm(NewME);
    }
    MI.getOperand(1).setReg(SrcMI->getOperand(1).getReg());
    if (SrcMI->getOperand(1).isKill()) {
      MI.getOperand(1).setIsKill(true);
      SrcMI->getOperand(1).setIsKill(false);
    } else
      // About to replace MI.getOperand(1), clear its kill flag.
      MI.getOperand(1).setIsKill(false);

    LLVM_DEBUG(dbgs() << "To: ");
    LLVM_DEBUG(MI.dump());
  }
  if (Simplified & MRI->use_nodbg_empty(FoldingReg) &&
      !SrcMI->hasImplicitDef()) {
    // If FoldingReg has no non-debug use and it has no implicit def (it
    // is not RLWINMO or RLWINM8o), it's safe to delete its def SrcMI.
    // Otherwise keep it.
    *ToErase = SrcMI;
    LLVM_DEBUG(dbgs() << "Delete dead instruction: ");
    LLVM_DEBUG(SrcMI->dump());
  }
  return Simplified;
}

bool PPCInstrInfo::instrHasImmForm(unsigned Opc, bool IsVFReg,
                                   ImmInstrInfo &III, bool PostRA) const {
  // The vast majority of the instructions would need their operand 2 replaced
  // with an immediate when switching to the reg+imm form. A marked exception
  // are the update form loads/stores for which a constant operand 2 would need
  // to turn into a displacement and move operand 1 to the operand 2 position.
  III.ImmOpNo = 2;
  III.OpNoForForwarding = 2;
  III.ImmWidth = 16;
  III.ImmMustBeMultipleOf = 1;
  III.TruncateImmTo = 0;
  III.IsSummingOperands = false;
  switch (Opc) {
  default: return false;
  case PPC::ADD4:
  case PPC::ADD8:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 1;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpcode = Opc == PPC::ADD4 ? PPC::ADDI : PPC::ADDI8;
    break;
  case PPC::ADDC:
  case PPC::ADDC8:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpcode = Opc == PPC::ADDC ? PPC::ADDIC : PPC::ADDIC8;
    break;
  case PPC::ADDC_rec:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpcode = PPC::ADDIC_rec;
    break;
  case PPC::SUBFC:
  case PPC::SUBFC8:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    III.ImmOpcode = Opc == PPC::SUBFC ? PPC::SUBFIC : PPC::SUBFIC8;
    break;
  case PPC::CMPW:
  case PPC::CMPD:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    III.ImmOpcode = Opc == PPC::CMPW ? PPC::CMPWI : PPC::CMPDI;
    break;
  case PPC::CMPLW:
  case PPC::CMPLD:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    III.ImmOpcode = Opc == PPC::CMPLW ? PPC::CMPLWI : PPC::CMPLDI;
    break;
  case PPC::AND_rec:
  case PPC::AND8_rec:
  case PPC::OR:
  case PPC::OR8:
  case PPC::XOR:
  case PPC::XOR8:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = true;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::AND_rec:
      III.ImmOpcode = PPC::ANDI_rec;
      break;
    case PPC::AND8_rec:
      III.ImmOpcode = PPC::ANDI8_rec;
      break;
    case PPC::OR: III.ImmOpcode = PPC::ORI; break;
    case PPC::OR8: III.ImmOpcode = PPC::ORI8; break;
    case PPC::XOR: III.ImmOpcode = PPC::XORI; break;
    case PPC::XOR8: III.ImmOpcode = PPC::XORI8; break;
    }
    break;
  case PPC::RLWNM:
  case PPC::RLWNM8:
  case PPC::RLWNM_rec:
  case PPC::RLWNM8_rec:
  case PPC::SLW:
  case PPC::SLW8:
  case PPC::SLW_rec:
  case PPC::SLW8_rec:
  case PPC::SRW:
  case PPC::SRW8:
  case PPC::SRW_rec:
  case PPC::SRW8_rec:
  case PPC::SRAW:
  case PPC::SRAW_rec:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    // This isn't actually true, but the instructions ignore any of the
    // upper bits, so any immediate loaded with an LI is acceptable.
    // This does not apply to shift right algebraic because a value
    // out of range will produce a -1/0.
    III.ImmWidth = 16;
    if (Opc == PPC::RLWNM || Opc == PPC::RLWNM8 || Opc == PPC::RLWNM_rec ||
        Opc == PPC::RLWNM8_rec)
      III.TruncateImmTo = 5;
    else
      III.TruncateImmTo = 6;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::RLWNM: III.ImmOpcode = PPC::RLWINM; break;
    case PPC::RLWNM8: III.ImmOpcode = PPC::RLWINM8; break;
    case PPC::RLWNM_rec:
      III.ImmOpcode = PPC::RLWINM_rec;
      break;
    case PPC::RLWNM8_rec:
      III.ImmOpcode = PPC::RLWINM8_rec;
      break;
    case PPC::SLW: III.ImmOpcode = PPC::RLWINM; break;
    case PPC::SLW8: III.ImmOpcode = PPC::RLWINM8; break;
    case PPC::SLW_rec:
      III.ImmOpcode = PPC::RLWINM_rec;
      break;
    case PPC::SLW8_rec:
      III.ImmOpcode = PPC::RLWINM8_rec;
      break;
    case PPC::SRW: III.ImmOpcode = PPC::RLWINM; break;
    case PPC::SRW8: III.ImmOpcode = PPC::RLWINM8; break;
    case PPC::SRW_rec:
      III.ImmOpcode = PPC::RLWINM_rec;
      break;
    case PPC::SRW8_rec:
      III.ImmOpcode = PPC::RLWINM8_rec;
      break;
    case PPC::SRAW:
      III.ImmWidth = 5;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRAWI;
      break;
    case PPC::SRAW_rec:
      III.ImmWidth = 5;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRAWI_rec;
      break;
    }
    break;
  case PPC::RLDCL:
  case PPC::RLDCL_rec:
  case PPC::RLDCR:
  case PPC::RLDCR_rec:
  case PPC::SLD:
  case PPC::SLD_rec:
  case PPC::SRD:
  case PPC::SRD_rec:
  case PPC::SRAD:
  case PPC::SRAD_rec:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    // This isn't actually true, but the instructions ignore any of the
    // upper bits, so any immediate loaded with an LI is acceptable.
    // This does not apply to shift right algebraic because a value
    // out of range will produce a -1/0.
    III.ImmWidth = 16;
    if (Opc == PPC::RLDCL || Opc == PPC::RLDCL_rec || Opc == PPC::RLDCR ||
        Opc == PPC::RLDCR_rec)
      III.TruncateImmTo = 6;
    else
      III.TruncateImmTo = 7;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::RLDCL: III.ImmOpcode = PPC::RLDICL; break;
    case PPC::RLDCL_rec:
      III.ImmOpcode = PPC::RLDICL_rec;
      break;
    case PPC::RLDCR: III.ImmOpcode = PPC::RLDICR; break;
    case PPC::RLDCR_rec:
      III.ImmOpcode = PPC::RLDICR_rec;
      break;
    case PPC::SLD: III.ImmOpcode = PPC::RLDICR; break;
    case PPC::SLD_rec:
      III.ImmOpcode = PPC::RLDICR_rec;
      break;
    case PPC::SRD: III.ImmOpcode = PPC::RLDICL; break;
    case PPC::SRD_rec:
      III.ImmOpcode = PPC::RLDICL_rec;
      break;
    case PPC::SRAD:
      III.ImmWidth = 6;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRADI;
       break;
    case PPC::SRAD_rec:
      III.ImmWidth = 6;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRADI_rec;
      break;
    }
    break;
  // Loads and stores:
  case PPC::LBZX:
  case PPC::LBZX8:
  case PPC::LHZX:
  case PPC::LHZX8:
  case PPC::LHAX:
  case PPC::LHAX8:
  case PPC::LWZX:
  case PPC::LWZX8:
  case PPC::LWAX:
  case PPC::LDX:
  case PPC::LFSX:
  case PPC::LFDX:
  case PPC::STBX:
  case PPC::STBX8:
  case PPC::STHX:
  case PPC::STHX8:
  case PPC::STWX:
  case PPC::STWX8:
  case PPC::STDX:
  case PPC::STFSX:
  case PPC::STFDX:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 1;
    III.ZeroIsSpecialNew = 2;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpNo = 1;
    III.OpNoForForwarding = 2;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::LBZX: III.ImmOpcode = PPC::LBZ; break;
    case PPC::LBZX8: III.ImmOpcode = PPC::LBZ8; break;
    case PPC::LHZX: III.ImmOpcode = PPC::LHZ; break;
    case PPC::LHZX8: III.ImmOpcode = PPC::LHZ8; break;
    case PPC::LHAX: III.ImmOpcode = PPC::LHA; break;
    case PPC::LHAX8: III.ImmOpcode = PPC::LHA8; break;
    case PPC::LWZX: III.ImmOpcode = PPC::LWZ; break;
    case PPC::LWZX8: III.ImmOpcode = PPC::LWZ8; break;
    case PPC::LWAX:
      III.ImmOpcode = PPC::LWA;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::LDX: III.ImmOpcode = PPC::LD; III.ImmMustBeMultipleOf = 4; break;
    case PPC::LFSX: III.ImmOpcode = PPC::LFS; break;
    case PPC::LFDX: III.ImmOpcode = PPC::LFD; break;
    case PPC::STBX: III.ImmOpcode = PPC::STB; break;
    case PPC::STBX8: III.ImmOpcode = PPC::STB8; break;
    case PPC::STHX: III.ImmOpcode = PPC::STH; break;
    case PPC::STHX8: III.ImmOpcode = PPC::STH8; break;
    case PPC::STWX: III.ImmOpcode = PPC::STW; break;
    case PPC::STWX8: III.ImmOpcode = PPC::STW8; break;
    case PPC::STDX:
      III.ImmOpcode = PPC::STD;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::STFSX: III.ImmOpcode = PPC::STFS; break;
    case PPC::STFDX: III.ImmOpcode = PPC::STFD; break;
    }
    break;
  case PPC::LBZUX:
  case PPC::LBZUX8:
  case PPC::LHZUX:
  case PPC::LHZUX8:
  case PPC::LHAUX:
  case PPC::LHAUX8:
  case PPC::LWZUX:
  case PPC::LWZUX8:
  case PPC::LDUX:
  case PPC::LFSUX:
  case PPC::LFDUX:
  case PPC::STBUX:
  case PPC::STBUX8:
  case PPC::STHUX:
  case PPC::STHUX8:
  case PPC::STWUX:
  case PPC::STWUX8:
  case PPC::STDUX:
  case PPC::STFSUX:
  case PPC::STFDUX:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 2;
    III.ZeroIsSpecialNew = 3;
    III.IsCommutative = false;
    III.IsSummingOperands = true;
    III.ImmOpNo = 2;
    III.OpNoForForwarding = 3;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::LBZUX: III.ImmOpcode = PPC::LBZU; break;
    case PPC::LBZUX8: III.ImmOpcode = PPC::LBZU8; break;
    case PPC::LHZUX: III.ImmOpcode = PPC::LHZU; break;
    case PPC::LHZUX8: III.ImmOpcode = PPC::LHZU8; break;
    case PPC::LHAUX: III.ImmOpcode = PPC::LHAU; break;
    case PPC::LHAUX8: III.ImmOpcode = PPC::LHAU8; break;
    case PPC::LWZUX: III.ImmOpcode = PPC::LWZU; break;
    case PPC::LWZUX8: III.ImmOpcode = PPC::LWZU8; break;
    case PPC::LDUX:
      III.ImmOpcode = PPC::LDU;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::LFSUX: III.ImmOpcode = PPC::LFSU; break;
    case PPC::LFDUX: III.ImmOpcode = PPC::LFDU; break;
    case PPC::STBUX: III.ImmOpcode = PPC::STBU; break;
    case PPC::STBUX8: III.ImmOpcode = PPC::STBU8; break;
    case PPC::STHUX: III.ImmOpcode = PPC::STHU; break;
    case PPC::STHUX8: III.ImmOpcode = PPC::STHU8; break;
    case PPC::STWUX: III.ImmOpcode = PPC::STWU; break;
    case PPC::STWUX8: III.ImmOpcode = PPC::STWU8; break;
    case PPC::STDUX:
      III.ImmOpcode = PPC::STDU;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::STFSUX: III.ImmOpcode = PPC::STFSU; break;
    case PPC::STFDUX: III.ImmOpcode = PPC::STFDU; break;
    }
    break;
  // Power9 and up only. For some of these, the X-Form version has access to all
  // 64 VSR's whereas the D-Form only has access to the VR's. We replace those
  // with pseudo-ops pre-ra and for post-ra, we check that the register loaded
  // into or stored from is one of the VR registers.
  case PPC::LXVX:
  case PPC::LXSSPX:
  case PPC::LXSDX:
  case PPC::STXVX:
  case PPC::STXSSPX:
  case PPC::STXSDX:
  case PPC::XFLOADf32:
  case PPC::XFLOADf64:
  case PPC::XFSTOREf32:
  case PPC::XFSTOREf64:
    if (!Subtarget.hasP9Vector())
      return false;
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 1;
    III.ZeroIsSpecialNew = 2;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpNo = 1;
    III.OpNoForForwarding = 2;
    III.ImmMustBeMultipleOf = 4;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::LXVX:
      III.ImmOpcode = PPC::LXV;
      III.ImmMustBeMultipleOf = 16;
      break;
    case PPC::LXSSPX:
      if (PostRA) {
        if (IsVFReg)
          III.ImmOpcode = PPC::LXSSP;
        else {
          III.ImmOpcode = PPC::LFS;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      [[fallthrough]];
    case PPC::XFLOADf32:
      III.ImmOpcode = PPC::DFLOADf32;
      break;
    case PPC::LXSDX:
      if (PostRA) {
        if (IsVFReg)
          III.ImmOpcode = PPC::LXSD;
        else {
          III.ImmOpcode = PPC::LFD;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      [[fallthrough]];
    case PPC::XFLOADf64:
      III.ImmOpcode = PPC::DFLOADf64;
      break;
    case PPC::STXVX:
      III.ImmOpcode = PPC::STXV;
      III.ImmMustBeMultipleOf = 16;
      break;
    case PPC::STXSSPX:
      if (PostRA) {
        if (IsVFReg)
          III.ImmOpcode = PPC::STXSSP;
        else {
          III.ImmOpcode = PPC::STFS;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      [[fallthrough]];
    case PPC::XFSTOREf32:
      III.ImmOpcode = PPC::DFSTOREf32;
      break;
    case PPC::STXSDX:
      if (PostRA) {
        if (IsVFReg)
          III.ImmOpcode = PPC::STXSD;
        else {
          III.ImmOpcode = PPC::STFD;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      [[fallthrough]];
    case PPC::XFSTOREf64:
      III.ImmOpcode = PPC::DFSTOREf64;
      break;
    }
    break;
  }
  return true;
}

// Utility function for swaping two arbitrary operands of an instruction.
static void swapMIOperands(MachineInstr &MI, unsigned Op1, unsigned Op2) {
  assert(Op1 != Op2 && "Cannot swap operand with itself.");

  unsigned MaxOp = std::max(Op1, Op2);
  unsigned MinOp = std::min(Op1, Op2);
  MachineOperand MOp1 = MI.getOperand(MinOp);
  MachineOperand MOp2 = MI.getOperand(MaxOp);
  MI.removeOperand(std::max(Op1, Op2));
  MI.removeOperand(std::min(Op1, Op2));

  // If the operands we are swapping are the two at the end (the common case)
  // we can just remove both and add them in the opposite order.
  if (MaxOp - MinOp == 1 && MI.getNumOperands() == MinOp) {
    MI.addOperand(MOp2);
    MI.addOperand(MOp1);
  } else {
    // Store all operands in a temporary vector, remove them and re-add in the
    // right order.
    SmallVector<MachineOperand, 2> MOps;
    unsigned TotalOps = MI.getNumOperands() + 2; // We've already removed 2 ops.
    for (unsigned i = MI.getNumOperands() - 1; i >= MinOp; i--) {
      MOps.push_back(MI.getOperand(i));
      MI.removeOperand(i);
    }
    // MOp2 needs to be added next.
    MI.addOperand(MOp2);
    // Now add the rest.
    for (unsigned i = MI.getNumOperands(); i < TotalOps; i++) {
      if (i == MaxOp)
        MI.addOperand(MOp1);
      else {
        MI.addOperand(MOps.back());
        MOps.pop_back();
      }
    }
  }
}

// Check if the 'MI' that has the index OpNoForForwarding
// meets the requirement described in the ImmInstrInfo.
bool PPCInstrInfo::isUseMIElgibleForForwarding(MachineInstr &MI,
                                               const ImmInstrInfo &III,
                                               unsigned OpNoForForwarding
                                               ) const {
  // As the algorithm of checking for PPC::ZERO/PPC::ZERO8
  // would not work pre-RA, we can only do the check post RA.
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  if (MRI.isSSA())
    return false;

  // Cannot do the transform if MI isn't summing the operands.
  if (!III.IsSummingOperands)
    return false;

  // The instruction we are trying to replace must have the ZeroIsSpecialOrig set.
  if (!III.ZeroIsSpecialOrig)
    return false;

  // We cannot do the transform if the operand we are trying to replace
  // isn't the same as the operand the instruction allows.
  if (OpNoForForwarding != III.OpNoForForwarding)
    return false;

  // Check if the instruction we are trying to transform really has
  // the special zero register as its operand.
  if (MI.getOperand(III.ZeroIsSpecialOrig).getReg() != PPC::ZERO &&
      MI.getOperand(III.ZeroIsSpecialOrig).getReg() != PPC::ZERO8)
    return false;

  // This machine instruction is convertible if it is,
  // 1. summing the operands.
  // 2. one of the operands is special zero register.
  // 3. the operand we are trying to replace is allowed by the MI.
  return true;
}

// Check if the DefMI is the add inst and set the ImmMO and RegMO
// accordingly.
bool PPCInstrInfo::isDefMIElgibleForForwarding(MachineInstr &DefMI,
                                               const ImmInstrInfo &III,
                                               MachineOperand *&ImmMO,
                                               MachineOperand *&RegMO) const {
  unsigned Opc = DefMI.getOpcode();
  if (Opc != PPC::ADDItocL8 && Opc != PPC::ADDI && Opc != PPC::ADDI8)
    return false;

  // Skip the optimization of transformTo[NewImm|Imm]FormFedByAdd for ADDItocL8
  // on AIX which is used for toc-data access. TODO: Follow up to see if it can
  // apply for AIX toc-data as well.
  if (Opc == PPC::ADDItocL8 && Subtarget.isAIX())
    return false;

  assert(DefMI.getNumOperands() >= 3 &&
         "Add inst must have at least three operands");
  RegMO = &DefMI.getOperand(1);
  ImmMO = &DefMI.getOperand(2);

  // Before RA, ADDI first operand could be a frame index.
  if (!RegMO->isReg())
    return false;

  // This DefMI is elgible for forwarding if it is:
  // 1. add inst
  // 2. one of the operands is Imm/CPI/Global.
  return isAnImmediateOperand(*ImmMO);
}

bool PPCInstrInfo::isRegElgibleForForwarding(
    const MachineOperand &RegMO, const MachineInstr &DefMI,
    const MachineInstr &MI, bool KillDefMI,
    bool &IsFwdFeederRegKilled, bool &SeenIntermediateUse) const {
  // x = addi y, imm
  // ...
  // z = lfdx 0, x   -> z = lfd imm(y)
  // The Reg "y" can be forwarded to the MI(z) only when there is no DEF
  // of "y" between the DEF of "x" and "z".
  // The query is only valid post RA.
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  if (MRI.isSSA())
    return false;

  Register Reg = RegMO.getReg();

  // Walking the inst in reverse(MI-->DefMI) to get the last DEF of the Reg.
  MachineBasicBlock::const_reverse_iterator It = MI;
  MachineBasicBlock::const_reverse_iterator E = MI.getParent()->rend();
  It++;
  for (; It != E; ++It) {
    if (It->modifiesRegister(Reg, &getRegisterInfo()) && (&*It) != &DefMI)
      return false;
    else if (It->killsRegister(Reg, &getRegisterInfo()) && (&*It) != &DefMI)
      IsFwdFeederRegKilled = true;
    if (It->readsRegister(Reg, &getRegisterInfo()) && (&*It) != &DefMI)
      SeenIntermediateUse = true;
    // Made it to DefMI without encountering a clobber.
    if ((&*It) == &DefMI)
      break;
  }
  assert((&*It) == &DefMI && "DefMI is missing");

  // If DefMI also defines the register to be forwarded, we can only forward it
  // if DefMI is being erased.
  if (DefMI.modifiesRegister(Reg, &getRegisterInfo()))
    return KillDefMI;

  return true;
}

bool PPCInstrInfo::isImmElgibleForForwarding(const MachineOperand &ImmMO,
                                             const MachineInstr &DefMI,
                                             const ImmInstrInfo &III,
                                             int64_t &Imm,
                                             int64_t BaseImm) const {
  assert(isAnImmediateOperand(ImmMO) && "ImmMO is NOT an immediate");
  if (DefMI.getOpcode() == PPC::ADDItocL8) {
    // The operand for ADDItocL8 is CPI, which isn't imm at compiling time,
    // However, we know that, it is 16-bit width, and has the alignment of 4.
    // Check if the instruction met the requirement.
    if (III.ImmMustBeMultipleOf > 4 ||
       III.TruncateImmTo || III.ImmWidth != 16)
      return false;

    // Going from XForm to DForm loads means that the displacement needs to be
    // not just an immediate but also a multiple of 4, or 16 depending on the
    // load. A DForm load cannot be represented if it is a multiple of say 2.
    // XForm loads do not have this restriction.
    if (ImmMO.isGlobal()) {
      const DataLayout &DL = ImmMO.getGlobal()->getDataLayout();
      if (ImmMO.getGlobal()->getPointerAlignment(DL) < III.ImmMustBeMultipleOf)
        return false;
    }

    return true;
  }

  if (ImmMO.isImm()) {
    // It is Imm, we need to check if the Imm fit the range.
    // Sign-extend to 64-bits.
    // DefMI may be folded with another imm form instruction, the result Imm is
    // the sum of Imm of DefMI and BaseImm which is from imm form instruction.
    APInt ActualValue(64, ImmMO.getImm() + BaseImm, true);
    if (III.SignedImm && !ActualValue.isSignedIntN(III.ImmWidth))
      return false;
    if (!III.SignedImm && !ActualValue.isIntN(III.ImmWidth))
      return false;
    Imm = SignExtend64<16>(ImmMO.getImm() + BaseImm);

    if (Imm % III.ImmMustBeMultipleOf)
      return false;
    if (III.TruncateImmTo)
      Imm &= ((1 << III.TruncateImmTo) - 1);
  }
  else
    return false;

  // This ImmMO is forwarded if it meets the requriement describle
  // in ImmInstrInfo
  return true;
}

bool PPCInstrInfo::simplifyToLI(MachineInstr &MI, MachineInstr &DefMI,
                                unsigned OpNoForForwarding,
                                MachineInstr **KilledDef) const {
  if ((DefMI.getOpcode() != PPC::LI && DefMI.getOpcode() != PPC::LI8) ||
      !DefMI.getOperand(1).isImm())
    return false;

  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  bool PostRA = !MRI->isSSA();

  int64_t Immediate = DefMI.getOperand(1).getImm();
  // Sign-extend to 64-bits.
  int64_t SExtImm = SignExtend64<16>(Immediate);

  bool ReplaceWithLI = false;
  bool Is64BitLI = false;
  int64_t NewImm = 0;
  bool SetCR = false;
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    return false;

  // FIXME: Any branches conditional on such a comparison can be made
  // unconditional. At this time, this happens too infrequently to be worth
  // the implementation effort, but if that ever changes, we could convert
  // such a pattern here.
  case PPC::CMPWI:
  case PPC::CMPLWI:
  case PPC::CMPDI:
  case PPC::CMPLDI: {
    // Doing this post-RA would require dataflow analysis to reliably find uses
    // of the CR register set by the compare.
    // No need to fixup killed/dead flag since this transformation is only valid
    // before RA.
    if (PostRA)
      return false;
    // If a compare-immediate is fed by an immediate and is itself an input of
    // an ISEL (the most common case) into a COPY of the correct register.
    bool Changed = false;
    Register DefReg = MI.getOperand(0).getReg();
    int64_t Comparand = MI.getOperand(2).getImm();
    int64_t SExtComparand = ((uint64_t)Comparand & ~0x7FFFuLL) != 0
                                ? (Comparand | 0xFFFFFFFFFFFF0000)
                                : Comparand;

    for (auto &CompareUseMI : MRI->use_instructions(DefReg)) {
      unsigned UseOpc = CompareUseMI.getOpcode();
      if (UseOpc != PPC::ISEL && UseOpc != PPC::ISEL8)
        continue;
      unsigned CRSubReg = CompareUseMI.getOperand(3).getSubReg();
      Register TrueReg = CompareUseMI.getOperand(1).getReg();
      Register FalseReg = CompareUseMI.getOperand(2).getReg();
      unsigned RegToCopy =
          selectReg(SExtImm, SExtComparand, Opc, TrueReg, FalseReg, CRSubReg);
      if (RegToCopy == PPC::NoRegister)
        continue;
      // Can't use PPC::COPY to copy PPC::ZERO[8]. Convert it to LI[8] 0.
      if (RegToCopy == PPC::ZERO || RegToCopy == PPC::ZERO8) {
        CompareUseMI.setDesc(get(UseOpc == PPC::ISEL8 ? PPC::LI8 : PPC::LI));
        replaceInstrOperandWithImm(CompareUseMI, 1, 0);
        CompareUseMI.removeOperand(3);
        CompareUseMI.removeOperand(2);
        continue;
      }
      LLVM_DEBUG(
          dbgs() << "Found LI -> CMPI -> ISEL, replacing with a copy.\n");
      LLVM_DEBUG(DefMI.dump(); MI.dump(); CompareUseMI.dump());
      LLVM_DEBUG(dbgs() << "Is converted to:\n");
      // Convert to copy and remove unneeded operands.
      CompareUseMI.setDesc(get(PPC::COPY));
      CompareUseMI.removeOperand(3);
      CompareUseMI.removeOperand(RegToCopy == TrueReg ? 2 : 1);
      CmpIselsConverted++;
      Changed = true;
      LLVM_DEBUG(CompareUseMI.dump());
    }
    if (Changed)
      return true;
    // This may end up incremented multiple times since this function is called
    // during a fixed-point transformation, but it is only meant to indicate the
    // presence of this opportunity.
    MissedConvertibleImmediateInstrs++;
    return false;
  }

  // Immediate forms - may simply be convertable to an LI.
  case PPC::ADDI:
  case PPC::ADDI8: {
    // Does the sum fit in a 16-bit signed field?
    int64_t Addend = MI.getOperand(2).getImm();
    if (isInt<16>(Addend + SExtImm)) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::ADDI8;
      NewImm = Addend + SExtImm;
      break;
    }
    return false;
  }
  case PPC::SUBFIC:
  case PPC::SUBFIC8: {
    // Only transform this if the CARRY implicit operand is dead.
    if (MI.getNumOperands() > 3 && !MI.getOperand(3).isDead())
      return false;
    int64_t Minuend = MI.getOperand(2).getImm();
    if (isInt<16>(Minuend - SExtImm)) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::SUBFIC8;
      NewImm = Minuend - SExtImm;
      break;
    }
    return false;
  }
  case PPC::RLDICL:
  case PPC::RLDICL_rec:
  case PPC::RLDICL_32:
  case PPC::RLDICL_32_64: {
    // Use APInt's rotate function.
    int64_t SH = MI.getOperand(2).getImm();
    int64_t MB = MI.getOperand(3).getImm();
    APInt InVal((Opc == PPC::RLDICL || Opc == PPC::RLDICL_rec) ? 64 : 32,
                SExtImm, true);
    InVal = InVal.rotl(SH);
    uint64_t Mask = MB == 0 ? -1LLU : (1LLU << (63 - MB + 1)) - 1;
    InVal &= Mask;
    // Can't replace negative values with an LI as that will sign-extend
    // and not clear the left bits. If we're setting the CR bit, we will use
    // ANDI_rec which won't sign extend, so that's safe.
    if (isUInt<15>(InVal.getSExtValue()) ||
        (Opc == PPC::RLDICL_rec && isUInt<16>(InVal.getSExtValue()))) {
      ReplaceWithLI = true;
      Is64BitLI = Opc != PPC::RLDICL_32;
      NewImm = InVal.getSExtValue();
      SetCR = Opc == PPC::RLDICL_rec;
      break;
    }
    return false;
  }
  case PPC::RLWINM:
  case PPC::RLWINM8:
  case PPC::RLWINM_rec:
  case PPC::RLWINM8_rec: {
    int64_t SH = MI.getOperand(2).getImm();
    int64_t MB = MI.getOperand(3).getImm();
    int64_t ME = MI.getOperand(4).getImm();
    APInt InVal(32, SExtImm, true);
    InVal = InVal.rotl(SH);
    APInt Mask = APInt::getBitsSetWithWrap(32, 32 - ME - 1, 32 - MB);
    InVal &= Mask;
    // Can't replace negative values with an LI as that will sign-extend
    // and not clear the left bits. If we're setting the CR bit, we will use
    // ANDI_rec which won't sign extend, so that's safe.
    bool ValueFits = isUInt<15>(InVal.getSExtValue());
    ValueFits |= ((Opc == PPC::RLWINM_rec || Opc == PPC::RLWINM8_rec) &&
                  isUInt<16>(InVal.getSExtValue()));
    if (ValueFits) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::RLWINM8 || Opc == PPC::RLWINM8_rec;
      NewImm = InVal.getSExtValue();
      SetCR = Opc == PPC::RLWINM_rec || Opc == PPC::RLWINM8_rec;
      break;
    }
    return false;
  }
  case PPC::ORI:
  case PPC::ORI8:
  case PPC::XORI:
  case PPC::XORI8: {
    int64_t LogicalImm = MI.getOperand(2).getImm();
    int64_t Result = 0;
    if (Opc == PPC::ORI || Opc == PPC::ORI8)
      Result = LogicalImm | SExtImm;
    else
      Result = LogicalImm ^ SExtImm;
    if (isInt<16>(Result)) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::ORI8 || Opc == PPC::XORI8;
      NewImm = Result;
      break;
    }
    return false;
  }
  }

  if (ReplaceWithLI) {
    // We need to be careful with CR-setting instructions we're replacing.
    if (SetCR) {
      // We don't know anything about uses when we're out of SSA, so only
      // replace if the new immediate will be reproduced.
      bool ImmChanged = (SExtImm & NewImm) != NewImm;
      if (PostRA && ImmChanged)
        return false;

      if (!PostRA) {
        // If the defining load-immediate has no other uses, we can just replace
        // the immediate with the new immediate.
        if (MRI->hasOneUse(DefMI.getOperand(0).getReg()))
          DefMI.getOperand(1).setImm(NewImm);

        // If we're not using the GPR result of the CR-setting instruction, we
        // just need to and with zero/non-zero depending on the new immediate.
        else if (MRI->use_empty(MI.getOperand(0).getReg())) {
          if (NewImm) {
            assert(Immediate && "Transformation converted zero to non-zero?");
            NewImm = Immediate;
          }
        } else if (ImmChanged)
          return false;
      }
    }

    LLVM_DEBUG(dbgs() << "Replacing constant instruction:\n");
    LLVM_DEBUG(MI.dump());
    LLVM_DEBUG(dbgs() << "Fed by:\n");
    LLVM_DEBUG(DefMI.dump());
    LoadImmediateInfo LII;
    LII.Imm = NewImm;
    LII.Is64Bit = Is64BitLI;
    LII.SetCR = SetCR;
    // If we're setting the CR, the original load-immediate must be kept (as an
    // operand to ANDI_rec/ANDI8_rec).
    if (KilledDef && SetCR)
      *KilledDef = nullptr;
    replaceInstrWithLI(MI, LII);

    if (PostRA)
      recomputeLivenessFlags(*MI.getParent());

    LLVM_DEBUG(dbgs() << "With:\n");
    LLVM_DEBUG(MI.dump());
    return true;
  }
  return false;
}

bool PPCInstrInfo::transformToNewImmFormFedByAdd(
    MachineInstr &MI, MachineInstr &DefMI, unsigned OpNoForForwarding) const {
  MachineRegisterInfo *MRI = &MI.getParent()->getParent()->getRegInfo();
  bool PostRA = !MRI->isSSA();
  // FIXME: extend this to post-ra. Need to do some change in getForwardingDefMI
  // for post-ra.
  if (PostRA)
    return false;

  // Only handle load/store.
  if (!MI.mayLoadOrStore())
    return false;

  unsigned XFormOpcode = RI.getMappedIdxOpcForImmOpc(MI.getOpcode());

  assert((XFormOpcode != PPC::INSTRUCTION_LIST_END) &&
         "MI must have x-form opcode");

  // get Imm Form info.
  ImmInstrInfo III;
  bool IsVFReg = MI.getOperand(0).isReg()
                     ? PPC::isVFRegister(MI.getOperand(0).getReg())
                     : false;

  if (!instrHasImmForm(XFormOpcode, IsVFReg, III, PostRA))
    return false;

  if (!III.IsSummingOperands)
    return false;

  if (OpNoForForwarding != III.OpNoForForwarding)
    return false;

  MachineOperand ImmOperandMI = MI.getOperand(III.ImmOpNo);
  if (!ImmOperandMI.isImm())
    return false;

  // Check DefMI.
  MachineOperand *ImmMO = nullptr;
  MachineOperand *RegMO = nullptr;
  if (!isDefMIElgibleForForwarding(DefMI, III, ImmMO, RegMO))
    return false;
  assert(ImmMO && RegMO && "Imm and Reg operand must have been set");

  // Check Imm.
  // Set ImmBase from imm instruction as base and get new Imm inside
  // isImmElgibleForForwarding.
  int64_t ImmBase = ImmOperandMI.getImm();
  int64_t Imm = 0;
  if (!isImmElgibleForForwarding(*ImmMO, DefMI, III, Imm, ImmBase))
    return false;

  // Do the transform
  LLVM_DEBUG(dbgs() << "Replacing existing reg+imm instruction:\n");
  LLVM_DEBUG(MI.dump());
  LLVM_DEBUG(dbgs() << "Fed by:\n");
  LLVM_DEBUG(DefMI.dump());

  MI.getOperand(III.OpNoForForwarding).setReg(RegMO->getReg());
  MI.getOperand(III.ImmOpNo).setImm(Imm);

  LLVM_DEBUG(dbgs() << "With:\n");
  LLVM_DEBUG(MI.dump());
  return true;
}

// If an X-Form instruction is fed by an add-immediate and one of its operands
// is the literal zero, attempt to forward the source of the add-immediate to
// the corresponding D-Form instruction with the displacement coming from
// the immediate being added.
bool PPCInstrInfo::transformToImmFormFedByAdd(
    MachineInstr &MI, const ImmInstrInfo &III, unsigned OpNoForForwarding,
    MachineInstr &DefMI, bool KillDefMI) const {
  //         RegMO ImmMO
  //           |    |
  // x = addi reg, imm  <----- DefMI
  // y = op    0 ,  x   <----- MI
  //                |
  //         OpNoForForwarding
  // Check if the MI meet the requirement described in the III.
  if (!isUseMIElgibleForForwarding(MI, III, OpNoForForwarding))
    return false;

  // Check if the DefMI meet the requirement
  // described in the III. If yes, set the ImmMO and RegMO accordingly.
  MachineOperand *ImmMO = nullptr;
  MachineOperand *RegMO = nullptr;
  if (!isDefMIElgibleForForwarding(DefMI, III, ImmMO, RegMO))
    return false;
  assert(ImmMO && RegMO && "Imm and Reg operand must have been set");

  // As we get the Imm operand now, we need to check if the ImmMO meet
  // the requirement described in the III. If yes set the Imm.
  int64_t Imm = 0;
  if (!isImmElgibleForForwarding(*ImmMO, DefMI, III, Imm))
    return false;

  bool IsFwdFeederRegKilled = false;
  bool SeenIntermediateUse = false;
  // Check if the RegMO can be forwarded to MI.
  if (!isRegElgibleForForwarding(*RegMO, DefMI, MI, KillDefMI,
                                 IsFwdFeederRegKilled, SeenIntermediateUse))
    return false;

  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  bool PostRA = !MRI.isSSA();

  // We know that, the MI and DefMI both meet the pattern, and
  // the Imm also meet the requirement with the new Imm-form.
  // It is safe to do the transformation now.
  LLVM_DEBUG(dbgs() << "Replacing indexed instruction:\n");
  LLVM_DEBUG(MI.dump());
  LLVM_DEBUG(dbgs() << "Fed by:\n");
  LLVM_DEBUG(DefMI.dump());

  // Update the base reg first.
  MI.getOperand(III.OpNoForForwarding).ChangeToRegister(RegMO->getReg(),
                                                        false, false,
                                                        RegMO->isKill());

  // Then, update the imm.
  if (ImmMO->isImm()) {
    // If the ImmMO is Imm, change the operand that has ZERO to that Imm
    // directly.
    replaceInstrOperandWithImm(MI, III.ZeroIsSpecialOrig, Imm);
  }
  else {
    // Otherwise, it is Constant Pool Index(CPI) or Global,
    // which is relocation in fact. We need to replace the special zero
    // register with ImmMO.
    // Before that, we need to fixup the target flags for imm.
    // For some reason, we miss to set the flag for the ImmMO if it is CPI.
    if (DefMI.getOpcode() == PPC::ADDItocL8)
      ImmMO->setTargetFlags(PPCII::MO_TOC_LO);

    // MI didn't have the interface such as MI.setOperand(i) though
    // it has MI.getOperand(i). To repalce the ZERO MachineOperand with
    // ImmMO, we need to remove ZERO operand and all the operands behind it,
    // and, add the ImmMO, then, move back all the operands behind ZERO.
    SmallVector<MachineOperand, 2> MOps;
    for (unsigned i = MI.getNumOperands() - 1; i >= III.ZeroIsSpecialOrig; i--) {
      MOps.push_back(MI.getOperand(i));
      MI.removeOperand(i);
    }

    // Remove the last MO in the list, which is ZERO operand in fact.
    MOps.pop_back();
    // Add the imm operand.
    MI.addOperand(*ImmMO);
    // Now add the rest back.
    for (auto &MO : MOps)
      MI.addOperand(MO);
  }

  // Update the opcode.
  MI.setDesc(get(III.ImmOpcode));

  if (PostRA)
    recomputeLivenessFlags(*MI.getParent());
  LLVM_DEBUG(dbgs() << "With:\n");
  LLVM_DEBUG(MI.dump());

  return true;
}

bool PPCInstrInfo::transformToImmFormFedByLI(MachineInstr &MI,
                                             const ImmInstrInfo &III,
                                             unsigned ConstantOpNo,
                                             MachineInstr &DefMI) const {
  // DefMI must be LI or LI8.
  if ((DefMI.getOpcode() != PPC::LI && DefMI.getOpcode() != PPC::LI8) ||
      !DefMI.getOperand(1).isImm())
    return false;

  // Get Imm operand and Sign-extend to 64-bits.
  int64_t Imm = SignExtend64<16>(DefMI.getOperand(1).getImm());

  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  bool PostRA = !MRI.isSSA();
  // Exit early if we can't convert this.
  if ((ConstantOpNo != III.OpNoForForwarding) && !III.IsCommutative)
    return false;
  if (Imm % III.ImmMustBeMultipleOf)
    return false;
  if (III.TruncateImmTo)
    Imm &= ((1 << III.TruncateImmTo) - 1);
  if (III.SignedImm) {
    APInt ActualValue(64, Imm, true);
    if (!ActualValue.isSignedIntN(III.ImmWidth))
      return false;
  } else {
    uint64_t UnsignedMax = (1 << III.ImmWidth) - 1;
    if ((uint64_t)Imm > UnsignedMax)
      return false;
  }

  // If we're post-RA, the instructions don't agree on whether register zero is
  // special, we can transform this as long as the register operand that will
  // end up in the location where zero is special isn't R0.
  if (PostRA && III.ZeroIsSpecialOrig != III.ZeroIsSpecialNew) {
    unsigned PosForOrigZero = III.ZeroIsSpecialOrig ? III.ZeroIsSpecialOrig :
      III.ZeroIsSpecialNew + 1;
    Register OrigZeroReg = MI.getOperand(PosForOrigZero).getReg();
    Register NewZeroReg = MI.getOperand(III.ZeroIsSpecialNew).getReg();
    // If R0 is in the operand where zero is special for the new instruction,
    // it is unsafe to transform if the constant operand isn't that operand.
    if ((NewZeroReg == PPC::R0 || NewZeroReg == PPC::X0) &&
        ConstantOpNo != III.ZeroIsSpecialNew)
      return false;
    if ((OrigZeroReg == PPC::R0 || OrigZeroReg == PPC::X0) &&
        ConstantOpNo != PosForOrigZero)
      return false;
  }

  unsigned Opc = MI.getOpcode();
  bool SpecialShift32 = Opc == PPC::SLW || Opc == PPC::SLW_rec ||
                        Opc == PPC::SRW || Opc == PPC::SRW_rec ||
                        Opc == PPC::SLW8 || Opc == PPC::SLW8_rec ||
                        Opc == PPC::SRW8 || Opc == PPC::SRW8_rec;
  bool SpecialShift64 = Opc == PPC::SLD || Opc == PPC::SLD_rec ||
                        Opc == PPC::SRD || Opc == PPC::SRD_rec;
  bool SetCR = Opc == PPC::SLW_rec || Opc == PPC::SRW_rec ||
               Opc == PPC::SLD_rec || Opc == PPC::SRD_rec;
  bool RightShift = Opc == PPC::SRW || Opc == PPC::SRW_rec || Opc == PPC::SRD ||
                    Opc == PPC::SRD_rec;

  LLVM_DEBUG(dbgs() << "Replacing reg+reg instruction: ");
  LLVM_DEBUG(MI.dump());
  LLVM_DEBUG(dbgs() << "Fed by load-immediate: ");
  LLVM_DEBUG(DefMI.dump());
  MI.setDesc(get(III.ImmOpcode));
  if (ConstantOpNo == III.OpNoForForwarding) {
    // Converting shifts to immediate form is a bit tricky since they may do
    // one of three things:
    // 1. If the shift amount is between OpSize and 2*OpSize, the result is zero
    // 2. If the shift amount is zero, the result is unchanged (save for maybe
    //    setting CR0)
    // 3. If the shift amount is in [1, OpSize), it's just a shift
    if (SpecialShift32 || SpecialShift64) {
      LoadImmediateInfo LII;
      LII.Imm = 0;
      LII.SetCR = SetCR;
      LII.Is64Bit = SpecialShift64;
      uint64_t ShAmt = Imm & (SpecialShift32 ? 0x1F : 0x3F);
      if (Imm & (SpecialShift32 ? 0x20 : 0x40))
        replaceInstrWithLI(MI, LII);
      // Shifts by zero don't change the value. If we don't need to set CR0,
      // just convert this to a COPY. Can't do this post-RA since we've already
      // cleaned up the copies.
      else if (!SetCR && ShAmt == 0 && !PostRA) {
        MI.removeOperand(2);
        MI.setDesc(get(PPC::COPY));
      } else {
        // The 32 bit and 64 bit instructions are quite different.
        if (SpecialShift32) {
          // Left shifts use (N, 0, 31-N).
          // Right shifts use (32-N, N, 31) if 0 < N < 32.
          //              use (0, 0, 31)    if N == 0.
          uint64_t SH = ShAmt == 0 ? 0 : RightShift ? 32 - ShAmt : ShAmt;
          uint64_t MB = RightShift ? ShAmt : 0;
          uint64_t ME = RightShift ? 31 : 31 - ShAmt;
          replaceInstrOperandWithImm(MI, III.OpNoForForwarding, SH);
          MachineInstrBuilder(*MI.getParent()->getParent(), MI).addImm(MB)
            .addImm(ME);
        } else {
          // Left shifts use (N, 63-N).
          // Right shifts use (64-N, N) if 0 < N < 64.
          //              use (0, 0)    if N == 0.
          uint64_t SH = ShAmt == 0 ? 0 : RightShift ? 64 - ShAmt : ShAmt;
          uint64_t ME = RightShift ? ShAmt : 63 - ShAmt;
          replaceInstrOperandWithImm(MI, III.OpNoForForwarding, SH);
          MachineInstrBuilder(*MI.getParent()->getParent(), MI).addImm(ME);
        }
      }
    } else
      replaceInstrOperandWithImm(MI, ConstantOpNo, Imm);
  }
  // Convert commutative instructions (switch the operands and convert the
  // desired one to an immediate.
  else if (III.IsCommutative) {
    replaceInstrOperandWithImm(MI, ConstantOpNo, Imm);
    swapMIOperands(MI, ConstantOpNo, III.OpNoForForwarding);
  } else
    llvm_unreachable("Should have exited early!");

  // For instructions for which the constant register replaces a different
  // operand than where the immediate goes, we need to swap them.
  if (III.OpNoForForwarding != III.ImmOpNo)
    swapMIOperands(MI, III.OpNoForForwarding, III.ImmOpNo);

  // If the special R0/X0 register index are different for original instruction
  // and new instruction, we need to fix up the register class in new
  // instruction.
  if (!PostRA && III.ZeroIsSpecialOrig != III.ZeroIsSpecialNew) {
    if (III.ZeroIsSpecialNew) {
      // If operand at III.ZeroIsSpecialNew is physical reg(eg: ZERO/ZERO8), no
      // need to fix up register class.
      Register RegToModify = MI.getOperand(III.ZeroIsSpecialNew).getReg();
      if (RegToModify.isVirtual()) {
        const TargetRegisterClass *NewRC =
          MRI.getRegClass(RegToModify)->hasSuperClassEq(&PPC::GPRCRegClass) ?
          &PPC::GPRC_and_GPRC_NOR0RegClass : &PPC::G8RC_and_G8RC_NOX0RegClass;
        MRI.setRegClass(RegToModify, NewRC);
      }
    }
  }

  if (PostRA)
    recomputeLivenessFlags(*MI.getParent());

  LLVM_DEBUG(dbgs() << "With: ");
  LLVM_DEBUG(MI.dump());
  LLVM_DEBUG(dbgs() << "\n");
  return true;
}

const TargetRegisterClass *
PPCInstrInfo::updatedRC(const TargetRegisterClass *RC) const {
  if (Subtarget.hasVSX() && RC == &PPC::VRRCRegClass)
    return &PPC::VSRCRegClass;
  return RC;
}

int PPCInstrInfo::getRecordFormOpcode(unsigned Opcode) {
  return PPC::getRecordFormOpcode(Opcode);
}

static bool isOpZeroOfSubwordPreincLoad(int Opcode) {
  return (Opcode == PPC::LBZU || Opcode == PPC::LBZUX || Opcode == PPC::LBZU8 ||
          Opcode == PPC::LBZUX8 || Opcode == PPC::LHZU ||
          Opcode == PPC::LHZUX || Opcode == PPC::LHZU8 ||
          Opcode == PPC::LHZUX8);
}

// This function checks for sign extension from 32 bits to 64 bits.
static bool definedBySignExtendingOp(const unsigned Reg,
                                     const MachineRegisterInfo *MRI) {
  if (!Register::isVirtualRegister(Reg))
    return false;

  MachineInstr *MI = MRI->getVRegDef(Reg);
  if (!MI)
    return false;

  int Opcode = MI->getOpcode();
  const PPCInstrInfo *TII =
      MI->getMF()->getSubtarget<PPCSubtarget>().getInstrInfo();
  if (TII->isSExt32To64(Opcode))
    return true;

  // The first def of LBZU/LHZU is sign extended.
  if (isOpZeroOfSubwordPreincLoad(Opcode) && MI->getOperand(0).getReg() == Reg)
    return true;

  // RLDICL generates sign-extended output if it clears at least
  // 33 bits from the left (MSB).
  if (Opcode == PPC::RLDICL && MI->getOperand(3).getImm() >= 33)
    return true;

  // If at least one bit from left in a lower word is masked out,
  // all of 0 to 32-th bits of the output are cleared.
  // Hence the output is already sign extended.
  if ((Opcode == PPC::RLWINM || Opcode == PPC::RLWINM_rec ||
       Opcode == PPC::RLWNM || Opcode == PPC::RLWNM_rec) &&
      MI->getOperand(3).getImm() > 0 &&
      MI->getOperand(3).getImm() <= MI->getOperand(4).getImm())
    return true;

  // If the most significant bit of immediate in ANDIS is zero,
  // all of 0 to 32-th bits are cleared.
  if (Opcode == PPC::ANDIS_rec || Opcode == PPC::ANDIS8_rec) {
    uint16_t Imm = MI->getOperand(2).getImm();
    if ((Imm & 0x8000) == 0)
      return true;
  }

  return false;
}

// This function checks the machine instruction that defines the input register
// Reg. If that machine instruction always outputs a value that has only zeros
// in the higher 32 bits then this function will return true.
static bool definedByZeroExtendingOp(const unsigned Reg,
                                     const MachineRegisterInfo *MRI) {
  if (!Register::isVirtualRegister(Reg))
    return false;

  MachineInstr *MI = MRI->getVRegDef(Reg);
  if (!MI)
    return false;

  int Opcode = MI->getOpcode();
  const PPCInstrInfo *TII =
      MI->getMF()->getSubtarget<PPCSubtarget>().getInstrInfo();
  if (TII->isZExt32To64(Opcode))
    return true;

  // The first def of LBZU/LHZU/LWZU are zero extended.
  if ((isOpZeroOfSubwordPreincLoad(Opcode) || Opcode == PPC::LWZU ||
       Opcode == PPC::LWZUX || Opcode == PPC::LWZU8 || Opcode == PPC::LWZUX8) &&
      MI->getOperand(0).getReg() == Reg)
    return true;

  // The 16-bit immediate is sign-extended in li/lis.
  // If the most significant bit is zero, all higher bits are zero.
  if (Opcode == PPC::LI  || Opcode == PPC::LI8 ||
      Opcode == PPC::LIS || Opcode == PPC::LIS8) {
    int64_t Imm = MI->getOperand(1).getImm();
    if (((uint64_t)Imm & ~0x7FFFuLL) == 0)
      return true;
  }

  // We have some variations of rotate-and-mask instructions
  // that clear higher 32-bits.
  if ((Opcode == PPC::RLDICL || Opcode == PPC::RLDICL_rec ||
       Opcode == PPC::RLDCL || Opcode == PPC::RLDCL_rec ||
       Opcode == PPC::RLDICL_32_64) &&
      MI->getOperand(3).getImm() >= 32)
    return true;

  if ((Opcode == PPC::RLDIC || Opcode == PPC::RLDIC_rec) &&
      MI->getOperand(3).getImm() >= 32 &&
      MI->getOperand(3).getImm() <= 63 - MI->getOperand(2).getImm())
    return true;

  if ((Opcode == PPC::RLWINM || Opcode == PPC::RLWINM_rec ||
       Opcode == PPC::RLWNM || Opcode == PPC::RLWNM_rec ||
       Opcode == PPC::RLWINM8 || Opcode == PPC::RLWNM8) &&
      MI->getOperand(3).getImm() <= MI->getOperand(4).getImm())
    return true;

  return false;
}

// This function returns true if the input MachineInstr is a TOC save
// instruction.
bool PPCInstrInfo::isTOCSaveMI(const MachineInstr &MI) const {
  if (!MI.getOperand(1).isImm() || !MI.getOperand(2).isReg())
    return false;
  unsigned TOCSaveOffset = Subtarget.getFrameLowering()->getTOCSaveOffset();
  unsigned StackOffset = MI.getOperand(1).getImm();
  Register StackReg = MI.getOperand(2).getReg();
  Register SPReg = Subtarget.isPPC64() ? PPC::X1 : PPC::R1;
  if (StackReg == SPReg && StackOffset == TOCSaveOffset)
    return true;

  return false;
}

// We limit the max depth to track incoming values of PHIs or binary ops
// (e.g. AND) to avoid excessive cost.
const unsigned MAX_BINOP_DEPTH = 1;
// The isSignOrZeroExtended function is recursive. The parameter BinOpDepth
// does not count all of the recursions. The parameter BinOpDepth is incremented
// only when isSignOrZeroExtended calls itself more than once. This is done to
// prevent expontential recursion. There is no parameter to track linear
// recursion.
std::pair<bool, bool>
PPCInstrInfo::isSignOrZeroExtended(const unsigned Reg,
                                   const unsigned BinOpDepth,
                                   const MachineRegisterInfo *MRI) const {
  if (!Register::isVirtualRegister(Reg))
    return std::pair<bool, bool>(false, false);

  MachineInstr *MI = MRI->getVRegDef(Reg);
  if (!MI)
    return std::pair<bool, bool>(false, false);

  bool IsSExt = definedBySignExtendingOp(Reg, MRI);
  bool IsZExt = definedByZeroExtendingOp(Reg, MRI);

  // If we know the instruction always returns sign- and zero-extended result,
  // return here.
  if (IsSExt && IsZExt)
    return std::pair<bool, bool>(IsSExt, IsZExt);

  switch (MI->getOpcode()) {
  case PPC::COPY: {
    Register SrcReg = MI->getOperand(1).getReg();

    // In both ELFv1 and v2 ABI, method parameters and the return value
    // are sign- or zero-extended.
    const MachineFunction *MF = MI->getMF();

    if (!MF->getSubtarget<PPCSubtarget>().isSVR4ABI()) {
      // If this is a copy from another register, we recursively check source.
      auto SrcExt = isSignOrZeroExtended(SrcReg, BinOpDepth, MRI);
      return std::pair<bool, bool>(SrcExt.first || IsSExt,
                                   SrcExt.second || IsZExt);
    }

    // From here on everything is SVR4ABI
    const PPCFunctionInfo *FuncInfo = MF->getInfo<PPCFunctionInfo>();
    // We check the ZExt/SExt flags for a method parameter.
    if (MI->getParent()->getBasicBlock() ==
        &MF->getFunction().getEntryBlock()) {
      Register VReg = MI->getOperand(0).getReg();
      if (MF->getRegInfo().isLiveIn(VReg)) {
        IsSExt |= FuncInfo->isLiveInSExt(VReg);
        IsZExt |= FuncInfo->isLiveInZExt(VReg);
        return std::pair<bool, bool>(IsSExt, IsZExt);
      }
    }

    if (SrcReg != PPC::X3) {
      // If this is a copy from another register, we recursively check source.
      auto SrcExt = isSignOrZeroExtended(SrcReg, BinOpDepth, MRI);
      return std::pair<bool, bool>(SrcExt.first || IsSExt,
                                   SrcExt.second || IsZExt);
    }

    // For a method return value, we check the ZExt/SExt flags in attribute.
    // We assume the following code sequence for method call.
    //   ADJCALLSTACKDOWN 32, implicit dead %r1, implicit %r1
    //   BL8_NOP @func,...
    //   ADJCALLSTACKUP 32, 0, implicit dead %r1, implicit %r1
    //   %5 = COPY %x3; G8RC:%5
    const MachineBasicBlock *MBB = MI->getParent();
    std::pair<bool, bool> IsExtendPair = std::pair<bool, bool>(IsSExt, IsZExt);
    MachineBasicBlock::const_instr_iterator II =
        MachineBasicBlock::const_instr_iterator(MI);
    if (II == MBB->instr_begin() || (--II)->getOpcode() != PPC::ADJCALLSTACKUP)
      return IsExtendPair;

    const MachineInstr &CallMI = *(--II);
    if (!CallMI.isCall() || !CallMI.getOperand(0).isGlobal())
      return IsExtendPair;

    const Function *CalleeFn =
        dyn_cast_if_present<Function>(CallMI.getOperand(0).getGlobal());
    if (!CalleeFn)
      return IsExtendPair;
    const IntegerType *IntTy = dyn_cast<IntegerType>(CalleeFn->getReturnType());
    if (IntTy && IntTy->getBitWidth() <= 32) {
      const AttributeSet &Attrs = CalleeFn->getAttributes().getRetAttrs();
      IsSExt |= Attrs.hasAttribute(Attribute::SExt);
      IsZExt |= Attrs.hasAttribute(Attribute::ZExt);
      return std::pair<bool, bool>(IsSExt, IsZExt);
    }

    return IsExtendPair;
  }

  // OR, XOR with 16-bit immediate does not change the upper 48 bits.
  // So, we track the operand register as we do for register copy.
  case PPC::ORI:
  case PPC::XORI:
  case PPC::ORI8:
  case PPC::XORI8: {
    Register SrcReg = MI->getOperand(1).getReg();
    auto SrcExt = isSignOrZeroExtended(SrcReg, BinOpDepth, MRI);
    return std::pair<bool, bool>(SrcExt.first || IsSExt,
                                 SrcExt.second || IsZExt);
  }

  // OR, XOR with shifted 16-bit immediate does not change the upper
  // 32 bits. So, we track the operand register for zero extension.
  // For sign extension when the MSB of the immediate is zero, we also
  // track the operand register since the upper 33 bits are unchanged.
  case PPC::ORIS:
  case PPC::XORIS:
  case PPC::ORIS8:
  case PPC::XORIS8: {
    Register SrcReg = MI->getOperand(1).getReg();
    auto SrcExt = isSignOrZeroExtended(SrcReg, BinOpDepth, MRI);
    uint16_t Imm = MI->getOperand(2).getImm();
    if (Imm & 0x8000)
      return std::pair<bool, bool>(false, SrcExt.second || IsZExt);
    else
      return std::pair<bool, bool>(SrcExt.first || IsSExt,
                                   SrcExt.second || IsZExt);
  }

  // If all incoming values are sign-/zero-extended,
  // the output of OR, ISEL or PHI is also sign-/zero-extended.
  case PPC::OR:
  case PPC::OR8:
  case PPC::ISEL:
  case PPC::PHI: {
    if (BinOpDepth >= MAX_BINOP_DEPTH)
      return std::pair<bool, bool>(false, false);

    // The input registers for PHI are operand 1, 3, ...
    // The input registers for others are operand 1 and 2.
    unsigned OperandEnd = 3, OperandStride = 1;
    if (MI->getOpcode() == PPC::PHI) {
      OperandEnd = MI->getNumOperands();
      OperandStride = 2;
    }

    IsSExt = true;
    IsZExt = true;
    for (unsigned I = 1; I != OperandEnd; I += OperandStride) {
      if (!MI->getOperand(I).isReg())
        return std::pair<bool, bool>(false, false);

      Register SrcReg = MI->getOperand(I).getReg();
      auto SrcExt = isSignOrZeroExtended(SrcReg, BinOpDepth + 1, MRI);
      IsSExt &= SrcExt.first;
      IsZExt &= SrcExt.second;
    }
    return std::pair<bool, bool>(IsSExt, IsZExt);
  }

  // If at least one of the incoming values of an AND is zero extended
  // then the output is also zero-extended. If both of the incoming values
  // are sign-extended then the output is also sign extended.
  case PPC::AND:
  case PPC::AND8: {
    if (BinOpDepth >= MAX_BINOP_DEPTH)
      return std::pair<bool, bool>(false, false);

    Register SrcReg1 = MI->getOperand(1).getReg();
    Register SrcReg2 = MI->getOperand(2).getReg();
    auto Src1Ext = isSignOrZeroExtended(SrcReg1, BinOpDepth + 1, MRI);
    auto Src2Ext = isSignOrZeroExtended(SrcReg2, BinOpDepth + 1, MRI);
    return std::pair<bool, bool>(Src1Ext.first && Src2Ext.first,
                                 Src1Ext.second || Src2Ext.second);
  }

  default:
    break;
  }
  return std::pair<bool, bool>(IsSExt, IsZExt);
}

bool PPCInstrInfo::isBDNZ(unsigned Opcode) const {
  return (Opcode == (Subtarget.isPPC64() ? PPC::BDNZ8 : PPC::BDNZ));
}

namespace {
class PPCPipelinerLoopInfo : public TargetInstrInfo::PipelinerLoopInfo {
  MachineInstr *Loop, *EndLoop, *LoopCount;
  MachineFunction *MF;
  const TargetInstrInfo *TII;
  int64_t TripCount;

public:
  PPCPipelinerLoopInfo(MachineInstr *Loop, MachineInstr *EndLoop,
                       MachineInstr *LoopCount)
      : Loop(Loop), EndLoop(EndLoop), LoopCount(LoopCount),
        MF(Loop->getParent()->getParent()),
        TII(MF->getSubtarget().getInstrInfo()) {
    // Inspect the Loop instruction up-front, as it may be deleted when we call
    // createTripCountGreaterCondition.
    if (LoopCount->getOpcode() == PPC::LI8 || LoopCount->getOpcode() == PPC::LI)
      TripCount = LoopCount->getOperand(1).getImm();
    else
      TripCount = -1;
  }

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override {
    // Only ignore the terminator.
    return MI == EndLoop;
  }

  std::optional<bool> createTripCountGreaterCondition(
      int TC, MachineBasicBlock &MBB,
      SmallVectorImpl<MachineOperand> &Cond) override {
    if (TripCount == -1) {
      // Since BDZ/BDZ8 that we will insert will also decrease the ctr by 1,
      // so we don't need to generate any thing here.
      Cond.push_back(MachineOperand::CreateImm(0));
      Cond.push_back(MachineOperand::CreateReg(
          MF->getSubtarget<PPCSubtarget>().isPPC64() ? PPC::CTR8 : PPC::CTR,
          true));
      return {};
    }

    return TripCount > TC;
  }

  void setPreheader(MachineBasicBlock *NewPreheader) override {
    // Do nothing. We want the LOOP setup instruction to stay in the *old*
    // preheader, so we can use BDZ in the prologs to adapt the loop trip count.
  }

  void adjustTripCount(int TripCountAdjust) override {
    // If the loop trip count is a compile-time value, then just change the
    // value.
    if (LoopCount->getOpcode() == PPC::LI8 ||
        LoopCount->getOpcode() == PPC::LI) {
      int64_t TripCount = LoopCount->getOperand(1).getImm() + TripCountAdjust;
      LoopCount->getOperand(1).setImm(TripCount);
      return;
    }

    // Since BDZ/BDZ8 that we will insert will also decrease the ctr by 1,
    // so we don't need to generate any thing here.
  }

  void disposed() override {
    Loop->eraseFromParent();
    // Ensure the loop setup instruction is deleted too.
    LoopCount->eraseFromParent();
  }
};
} // namespace

std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
PPCInstrInfo::analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
  // We really "analyze" only hardware loops right now.
  MachineBasicBlock::iterator I = LoopBB->getFirstTerminator();
  MachineBasicBlock *Preheader = *LoopBB->pred_begin();
  if (Preheader == LoopBB)
    Preheader = *std::next(LoopBB->pred_begin());
  MachineFunction *MF = Preheader->getParent();

  if (I != LoopBB->end() && isBDNZ(I->getOpcode())) {
    SmallPtrSet<MachineBasicBlock *, 8> Visited;
    if (MachineInstr *LoopInst = findLoopInstr(*Preheader, Visited)) {
      Register LoopCountReg = LoopInst->getOperand(0).getReg();
      MachineRegisterInfo &MRI = MF->getRegInfo();
      MachineInstr *LoopCount = MRI.getUniqueVRegDef(LoopCountReg);
      return std::make_unique<PPCPipelinerLoopInfo>(LoopInst, &*I, LoopCount);
    }
  }
  return nullptr;
}

MachineInstr *PPCInstrInfo::findLoopInstr(
    MachineBasicBlock &PreHeader,
    SmallPtrSet<MachineBasicBlock *, 8> &Visited) const {

  unsigned LOOPi = (Subtarget.isPPC64() ? PPC::MTCTR8loop : PPC::MTCTRloop);

  // The loop set-up instruction should be in preheader
  for (auto &I : PreHeader.instrs())
    if (I.getOpcode() == LOOPi)
      return &I;
  return nullptr;
}

// Return true if get the base operand, byte offset of an instruction and the
// memory width. Width is the size of memory that is being loaded/stored.
bool PPCInstrInfo::getMemOperandWithOffsetWidth(
    const MachineInstr &LdSt, const MachineOperand *&BaseReg, int64_t &Offset,
    LocationSize &Width, const TargetRegisterInfo *TRI) const {
  if (!LdSt.mayLoadOrStore() || LdSt.getNumExplicitOperands() != 3)
    return false;

  // Handle only loads/stores with base register followed by immediate offset.
  if (!LdSt.getOperand(1).isImm() ||
      (!LdSt.getOperand(2).isReg() && !LdSt.getOperand(2).isFI()))
    return false;
  if (!LdSt.getOperand(1).isImm() ||
      (!LdSt.getOperand(2).isReg() && !LdSt.getOperand(2).isFI()))
    return false;

  if (!LdSt.hasOneMemOperand())
    return false;

  Width = (*LdSt.memoperands_begin())->getSize();
  Offset = LdSt.getOperand(1).getImm();
  BaseReg = &LdSt.getOperand(2);
  return true;
}

bool PPCInstrInfo::areMemAccessesTriviallyDisjoint(
    const MachineInstr &MIa, const MachineInstr &MIb) const {
  assert(MIa.mayLoadOrStore() && "MIa must be a load or store.");
  assert(MIb.mayLoadOrStore() && "MIb must be a load or store.");

  if (MIa.hasUnmodeledSideEffects() || MIb.hasUnmodeledSideEffects() ||
      MIa.hasOrderedMemoryRef() || MIb.hasOrderedMemoryRef())
    return false;

  // Retrieve the base register, offset from the base register and width. Width
  // is the size of memory that is being loaded/stored (e.g. 1, 2, 4).  If
  // base registers are identical, and the offset of a lower memory access +
  // the width doesn't overlap the offset of a higher memory access,
  // then the memory accesses are different.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  const MachineOperand *BaseOpA = nullptr, *BaseOpB = nullptr;
  int64_t OffsetA = 0, OffsetB = 0;
  LocationSize WidthA = 0, WidthB = 0;
  if (getMemOperandWithOffsetWidth(MIa, BaseOpA, OffsetA, WidthA, TRI) &&
      getMemOperandWithOffsetWidth(MIb, BaseOpB, OffsetB, WidthB, TRI)) {
    if (BaseOpA->isIdenticalTo(*BaseOpB)) {
      int LowOffset = std::min(OffsetA, OffsetB);
      int HighOffset = std::max(OffsetA, OffsetB);
      LocationSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
      if (LowWidth.hasValue() &&
          LowOffset + (int)LowWidth.getValue() <= HighOffset)
        return true;
    }
  }
  return false;
}
