//===-- GCNHazardRecognizers.cpp - GCN Hazard Recognizer Impls ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements hazard recognizers for scheduling on GCN processors.
//
//===----------------------------------------------------------------------===//

#include "GCNHazardRecognizer.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/TargetParser/TargetParser.h"

using namespace llvm;

namespace {

struct MFMAPaddingRatioParser : public cl::parser<unsigned> {
  MFMAPaddingRatioParser(cl::Option &O) : cl::parser<unsigned>(O) {}

  bool parse(cl::Option &O, StringRef ArgName, StringRef Arg, unsigned &Value) {
    if (Arg.getAsInteger(0, Value))
      return O.error("'" + Arg + "' value invalid for uint argument!");

    if (Value > 100)
      return O.error("'" + Arg + "' value must be in the range [0, 100]!");

    return false;
  }
};

} // end anonymous namespace

static cl::opt<unsigned, false, MFMAPaddingRatioParser>
    MFMAPaddingRatio("amdgpu-mfma-padding-ratio", cl::init(0), cl::Hidden,
                     cl::desc("Fill a percentage of the latency between "
                              "neighboring MFMA with s_nops."));

//===----------------------------------------------------------------------===//
// Hazard Recognizer Implementation
//===----------------------------------------------------------------------===//

static bool shouldRunLdsBranchVmemWARHazardFixup(const MachineFunction &MF,
                                                 const GCNSubtarget &ST);

GCNHazardRecognizer::GCNHazardRecognizer(const MachineFunction &MF) :
  IsHazardRecognizerMode(false),
  CurrCycleInstr(nullptr),
  MF(MF),
  ST(MF.getSubtarget<GCNSubtarget>()),
  TII(*ST.getInstrInfo()),
  TRI(TII.getRegisterInfo()),
  ClauseUses(TRI.getNumRegUnits()),
  ClauseDefs(TRI.getNumRegUnits()) {
  MaxLookAhead = MF.getRegInfo().isPhysRegUsed(AMDGPU::AGPR0) ? 19 : 5;
  TSchedModel.init(&ST);
  RunLdsBranchVmemWARHazardFixup = shouldRunLdsBranchVmemWARHazardFixup(MF, ST);
}

void GCNHazardRecognizer::Reset() {
  EmittedInstrs.clear();
}

void GCNHazardRecognizer::EmitInstruction(SUnit *SU) {
  EmitInstruction(SU->getInstr());
}

void GCNHazardRecognizer::EmitInstruction(MachineInstr *MI) {
  CurrCycleInstr = MI;
}

static bool isDivFMas(unsigned Opcode) {
  return Opcode == AMDGPU::V_DIV_FMAS_F32_e64 || Opcode == AMDGPU::V_DIV_FMAS_F64_e64;
}

static bool isSGetReg(unsigned Opcode) {
  return Opcode == AMDGPU::S_GETREG_B32;
}

static bool isSSetReg(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::S_SETREG_B32:
  case AMDGPU::S_SETREG_B32_mode:
  case AMDGPU::S_SETREG_IMM32_B32:
  case AMDGPU::S_SETREG_IMM32_B32_mode:
    return true;
  }
  return false;
}

static bool isRWLane(unsigned Opcode) {
  return Opcode == AMDGPU::V_READLANE_B32 || Opcode == AMDGPU::V_WRITELANE_B32;
}

static bool isRFE(unsigned Opcode) {
  return Opcode == AMDGPU::S_RFE_B64;
}

static bool isSMovRel(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::S_MOVRELS_B32:
  case AMDGPU::S_MOVRELS_B64:
  case AMDGPU::S_MOVRELD_B32:
  case AMDGPU::S_MOVRELD_B64:
    return true;
  default:
    return false;
  }
}

static bool isDGEMM(unsigned Opcode) {
  return AMDGPU::getMAIIsDGEMM(Opcode);
}

static bool isXDL(const GCNSubtarget &ST, const MachineInstr &MI) {
  unsigned Opcode = MI.getOpcode();

  if (!SIInstrInfo::isMAI(MI) ||
      isDGEMM(Opcode) ||
      Opcode == AMDGPU::V_ACCVGPR_WRITE_B32_e64 ||
      Opcode == AMDGPU::V_ACCVGPR_READ_B32_e64)
    return false;

  if (!ST.hasGFX940Insts())
    return true;

  return AMDGPU::getMAIIsGFX940XDL(Opcode);
}

static bool isSendMsgTraceDataOrGDS(const SIInstrInfo &TII,
                                    const MachineInstr &MI) {
  if (TII.isAlwaysGDS(MI.getOpcode()))
    return true;

  switch (MI.getOpcode()) {
  case AMDGPU::S_SENDMSG:
  case AMDGPU::S_SENDMSGHALT:
  case AMDGPU::S_TTRACEDATA:
    return true;
  // These DS opcodes don't support GDS.
  case AMDGPU::DS_NOP:
  case AMDGPU::DS_PERMUTE_B32:
  case AMDGPU::DS_BPERMUTE_B32:
    return false;
  default:
    if (TII.isDS(MI.getOpcode())) {
      int GDS = AMDGPU::getNamedOperandIdx(MI.getOpcode(),
                                           AMDGPU::OpName::gds);
      if (MI.getOperand(GDS).getImm())
        return true;
    }
    return false;
  }
}

static bool isPermlane(const MachineInstr &MI) {
  unsigned Opcode = MI.getOpcode();
  return Opcode == AMDGPU::V_PERMLANE16_B32_e64 ||
         Opcode == AMDGPU::V_PERMLANE64_B32 ||
         Opcode == AMDGPU::V_PERMLANEX16_B32_e64 ||
         Opcode == AMDGPU::V_PERMLANE16_VAR_B32_e64 ||
         Opcode == AMDGPU::V_PERMLANEX16_VAR_B32_e64;
}

static bool isLdsDma(const MachineInstr &MI) {
  return SIInstrInfo::isVALU(MI) &&
         (SIInstrInfo::isMUBUF(MI) || SIInstrInfo::isFLAT(MI));
}

static unsigned getHWReg(const SIInstrInfo *TII, const MachineInstr &RegInstr) {
  const MachineOperand *RegOp = TII->getNamedOperand(RegInstr,
                                                     AMDGPU::OpName::simm16);
  return std::get<0>(AMDGPU::Hwreg::HwregEncoding::decode(RegOp->getImm()));
}

ScheduleHazardRecognizer::HazardType
GCNHazardRecognizer::getHazardType(SUnit *SU, int Stalls) {
  MachineInstr *MI = SU->getInstr();
  // If we are not in "HazardRecognizerMode" and therefore not being run from
  // the scheduler, track possible stalls from hazards but don't insert noops.
  auto HazardType = IsHazardRecognizerMode ? NoopHazard : Hazard;

  if (MI->isBundle())
   return NoHazard;

  if (SIInstrInfo::isSMRD(*MI) && checkSMRDHazards(MI) > 0)
    return HazardType;

  if (ST.hasNSAtoVMEMBug() && checkNSAtoVMEMHazard(MI) > 0)
    return HazardType;

  if (checkFPAtomicToDenormModeHazard(MI) > 0)
    return HazardType;

  if (ST.hasNoDataDepHazard())
    return NoHazard;

  // FIXME: Should flat be considered vmem?
  if ((SIInstrInfo::isVMEM(*MI) ||
       SIInstrInfo::isFLAT(*MI))
      && checkVMEMHazards(MI) > 0)
    return HazardType;

  if (SIInstrInfo::isVALU(*MI) && checkVALUHazards(MI) > 0)
    return HazardType;

  if (SIInstrInfo::isDPP(*MI) && checkDPPHazards(MI) > 0)
    return HazardType;

  if (isDivFMas(MI->getOpcode()) && checkDivFMasHazards(MI) > 0)
    return HazardType;

  if (isRWLane(MI->getOpcode()) && checkRWLaneHazards(MI) > 0)
    return HazardType;

  if ((SIInstrInfo::isVALU(*MI) || SIInstrInfo::isVMEM(*MI) ||
       SIInstrInfo::isFLAT(*MI) || SIInstrInfo::isDS(*MI) ||
       SIInstrInfo::isEXP(*MI)) && checkMAIVALUHazards(MI) > 0)
    return HazardType;

  if (isSGetReg(MI->getOpcode()) && checkGetRegHazards(MI) > 0)
    return HazardType;

  if (isSSetReg(MI->getOpcode()) && checkSetRegHazards(MI) > 0)
    return HazardType;

  if (isRFE(MI->getOpcode()) && checkRFEHazards(MI) > 0)
    return HazardType;

  if (((ST.hasReadM0MovRelInterpHazard() &&
        (TII.isVINTRP(*MI) || isSMovRel(MI->getOpcode()) ||
         MI->getOpcode() == AMDGPU::DS_WRITE_ADDTID_B32 ||
         MI->getOpcode() == AMDGPU::DS_READ_ADDTID_B32)) ||
       (ST.hasReadM0SendMsgHazard() && isSendMsgTraceDataOrGDS(TII, *MI)) ||
       (ST.hasReadM0LdsDmaHazard() && isLdsDma(*MI)) ||
       (ST.hasReadM0LdsDirectHazard() &&
        MI->readsRegister(AMDGPU::LDS_DIRECT, /*TRI=*/nullptr))) &&
      checkReadM0Hazards(MI) > 0)
    return HazardType;

  if (SIInstrInfo::isMAI(*MI) && checkMAIHazards(MI) > 0)
    return HazardType;

  if ((SIInstrInfo::isVMEM(*MI) ||
       SIInstrInfo::isFLAT(*MI) ||
       SIInstrInfo::isDS(*MI)) && checkMAILdStHazards(MI) > 0)
    return HazardType;

  if (MI->isInlineAsm() && checkInlineAsmHazards(MI) > 0)
    return HazardType;

  return NoHazard;
}

static void insertNoopsInBundle(MachineInstr *MI, const SIInstrInfo &TII,
                                unsigned Quantity) {
  while (Quantity > 0) {
    unsigned Arg = std::min(Quantity, 8u);
    Quantity -= Arg;
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), TII.get(AMDGPU::S_NOP))
        .addImm(Arg - 1);
  }
}

unsigned
GCNHazardRecognizer::getMFMAPipelineWaitStates(const MachineInstr &MI) const {
  const MCSchedClassDesc *SC = TSchedModel.resolveSchedClass(&MI);
  assert(TSchedModel.getWriteProcResBegin(SC) !=
         TSchedModel.getWriteProcResEnd(SC));
  return TSchedModel.getWriteProcResBegin(SC)->ReleaseAtCycle;
}

void GCNHazardRecognizer::processBundle() {
  MachineBasicBlock::instr_iterator MI = std::next(CurrCycleInstr->getIterator());
  MachineBasicBlock::instr_iterator E = CurrCycleInstr->getParent()->instr_end();
  // Check bundled MachineInstr's for hazards.
  for (; MI != E && MI->isInsideBundle(); ++MI) {
    CurrCycleInstr = &*MI;
    unsigned WaitStates = PreEmitNoopsCommon(CurrCycleInstr);

    if (IsHazardRecognizerMode) {
      fixHazards(CurrCycleInstr);

      insertNoopsInBundle(CurrCycleInstr, TII, WaitStates);
    }

    // It’s unnecessary to track more than MaxLookAhead instructions. Since we
    // include the bundled MI directly after, only add a maximum of
    // (MaxLookAhead - 1) noops to EmittedInstrs.
    for (unsigned i = 0, e = std::min(WaitStates, MaxLookAhead - 1); i < e; ++i)
      EmittedInstrs.push_front(nullptr);

    EmittedInstrs.push_front(CurrCycleInstr);
    EmittedInstrs.resize(MaxLookAhead);
  }
  CurrCycleInstr = nullptr;
}

void GCNHazardRecognizer::runOnInstruction(MachineInstr *MI) {
  assert(IsHazardRecognizerMode);

  unsigned NumPreNoops = PreEmitNoops(MI);
  EmitNoops(NumPreNoops);
  if (MI->isInsideBundle())
    insertNoopsInBundle(MI, TII, NumPreNoops);
  else
    TII.insertNoops(*MI->getParent(), MachineBasicBlock::iterator(MI),
                    NumPreNoops);
  EmitInstruction(MI);
  AdvanceCycle();
}

unsigned GCNHazardRecognizer::PreEmitNoops(MachineInstr *MI) {
  IsHazardRecognizerMode = true;
  CurrCycleInstr = MI;
  unsigned W = PreEmitNoopsCommon(MI);
  fixHazards(MI);
  CurrCycleInstr = nullptr;
  return W;
}

unsigned GCNHazardRecognizer::PreEmitNoopsCommon(MachineInstr *MI) {
  if (MI->isBundle())
    return 0;

  int WaitStates = 0;

  if (SIInstrInfo::isSMRD(*MI))
    return std::max(WaitStates, checkSMRDHazards(MI));

  if (ST.hasNSAtoVMEMBug())
    WaitStates = std::max(WaitStates, checkNSAtoVMEMHazard(MI));

  WaitStates = std::max(WaitStates, checkFPAtomicToDenormModeHazard(MI));

  if (ST.hasNoDataDepHazard())
    return WaitStates;

  if (SIInstrInfo::isVMEM(*MI) || SIInstrInfo::isFLAT(*MI))
    WaitStates = std::max(WaitStates, checkVMEMHazards(MI));

  if (SIInstrInfo::isVALU(*MI))
    WaitStates = std::max(WaitStates, checkVALUHazards(MI));

  if (SIInstrInfo::isDPP(*MI))
    WaitStates = std::max(WaitStates, checkDPPHazards(MI));

  if (isDivFMas(MI->getOpcode()))
    WaitStates = std::max(WaitStates, checkDivFMasHazards(MI));

  if (isRWLane(MI->getOpcode()))
    WaitStates = std::max(WaitStates, checkRWLaneHazards(MI));

  if ((SIInstrInfo::isVALU(*MI) || SIInstrInfo::isVMEM(*MI) ||
       SIInstrInfo::isFLAT(*MI) || SIInstrInfo::isDS(*MI) ||
       SIInstrInfo::isEXP(*MI)) && checkMAIVALUHazards(MI) > 0)
    WaitStates = std::max(WaitStates, checkMAIVALUHazards(MI));

  if (MI->isInlineAsm())
    return std::max(WaitStates, checkInlineAsmHazards(MI));

  if (isSGetReg(MI->getOpcode()))
    return std::max(WaitStates, checkGetRegHazards(MI));

  if (isSSetReg(MI->getOpcode()))
    return std::max(WaitStates, checkSetRegHazards(MI));

  if (isRFE(MI->getOpcode()))
    return std::max(WaitStates, checkRFEHazards(MI));

  if ((ST.hasReadM0MovRelInterpHazard() &&
       (TII.isVINTRP(*MI) || isSMovRel(MI->getOpcode()) ||
        MI->getOpcode() == AMDGPU::DS_WRITE_ADDTID_B32 ||
        MI->getOpcode() == AMDGPU::DS_READ_ADDTID_B32)) ||
      (ST.hasReadM0SendMsgHazard() && isSendMsgTraceDataOrGDS(TII, *MI)) ||
      (ST.hasReadM0LdsDmaHazard() && isLdsDma(*MI)) ||
      (ST.hasReadM0LdsDirectHazard() &&
       MI->readsRegister(AMDGPU::LDS_DIRECT, /*TRI=*/nullptr)))
    return std::max(WaitStates, checkReadM0Hazards(MI));

  if (SIInstrInfo::isMAI(*MI))
    return std::max(WaitStates, checkMAIHazards(MI));

  if (SIInstrInfo::isVMEM(*MI) ||
      SIInstrInfo::isFLAT(*MI) ||
      SIInstrInfo::isDS(*MI))
    return std::max(WaitStates, checkMAILdStHazards(MI));

  return WaitStates;
}

void GCNHazardRecognizer::EmitNoop() {
  EmittedInstrs.push_front(nullptr);
}

void GCNHazardRecognizer::AdvanceCycle() {
  // When the scheduler detects a stall, it will call AdvanceCycle() without
  // emitting any instructions.
  if (!CurrCycleInstr) {
    EmittedInstrs.push_front(nullptr);
    return;
  }

  if (CurrCycleInstr->isBundle()) {
    processBundle();
    return;
  }

  unsigned NumWaitStates = TII.getNumWaitStates(*CurrCycleInstr);
  if (!NumWaitStates) {
    CurrCycleInstr = nullptr;
    return;
  }

  // Keep track of emitted instructions
  EmittedInstrs.push_front(CurrCycleInstr);

  // Add a nullptr for each additional wait state after the first.  Make sure
  // not to add more than getMaxLookAhead() items to the list, since we
  // truncate the list to that size right after this loop.
  for (unsigned i = 1, e = std::min(NumWaitStates, getMaxLookAhead());
       i < e; ++i) {
    EmittedInstrs.push_front(nullptr);
  }

  // getMaxLookahead() is the largest number of wait states we will ever need
  // to insert, so there is no point in keeping track of more than that many
  // wait states.
  EmittedInstrs.resize(getMaxLookAhead());

  CurrCycleInstr = nullptr;
}

void GCNHazardRecognizer::RecedeCycle() {
  llvm_unreachable("hazard recognizer does not support bottom-up scheduling.");
}

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

using HazardFnResult = enum { HazardFound, HazardExpired, NoHazardFound };

using IsExpiredFn = function_ref<bool(const MachineInstr &, int WaitStates)>;
using GetNumWaitStatesFn = function_ref<unsigned int(const MachineInstr &)>;

// Search for a hazard in a block and its predecessors.
template <typename StateT>
static bool
hasHazard(StateT State,
          function_ref<HazardFnResult(StateT &, const MachineInstr &)> IsHazard,
          function_ref<void(StateT &, const MachineInstr &)> UpdateState,
          const MachineBasicBlock *MBB,
          MachineBasicBlock::const_reverse_instr_iterator I,
          DenseSet<const MachineBasicBlock *> &Visited) {
  for (auto E = MBB->instr_rend(); I != E; ++I) {
    // No need to look at parent BUNDLE instructions.
    if (I->isBundle())
      continue;

    switch (IsHazard(State, *I)) {
    case HazardFound:
      return true;
    case HazardExpired:
      return false;
    default:
      // Continue search
      break;
    }

    if (I->isInlineAsm() || I->isMetaInstruction())
      continue;

    UpdateState(State, *I);
  }

  for (MachineBasicBlock *Pred : MBB->predecessors()) {
    if (!Visited.insert(Pred).second)
      continue;

    if (hasHazard(State, IsHazard, UpdateState, Pred, Pred->instr_rbegin(),
                  Visited))
      return true;
  }

  return false;
}

// Returns a minimum wait states since \p I walking all predecessors.
// Only scans until \p IsExpired does not return true.
// Can only be run in a hazard recognizer mode.
static int getWaitStatesSince(
    GCNHazardRecognizer::IsHazardFn IsHazard, const MachineBasicBlock *MBB,
    MachineBasicBlock::const_reverse_instr_iterator I, int WaitStates,
    IsExpiredFn IsExpired, DenseSet<const MachineBasicBlock *> &Visited,
    GetNumWaitStatesFn GetNumWaitStates = SIInstrInfo::getNumWaitStates) {
  for (auto E = MBB->instr_rend(); I != E; ++I) {
    // Don't add WaitStates for parent BUNDLE instructions.
    if (I->isBundle())
      continue;

    if (IsHazard(*I))
      return WaitStates;

    if (I->isInlineAsm())
      continue;

    WaitStates += GetNumWaitStates(*I);

    if (IsExpired(*I, WaitStates))
      return std::numeric_limits<int>::max();
  }

  int MinWaitStates = std::numeric_limits<int>::max();
  for (MachineBasicBlock *Pred : MBB->predecessors()) {
    if (!Visited.insert(Pred).second)
      continue;

    int W = getWaitStatesSince(IsHazard, Pred, Pred->instr_rbegin(), WaitStates,
                               IsExpired, Visited, GetNumWaitStates);

    MinWaitStates = std::min(MinWaitStates, W);
  }

  return MinWaitStates;
}

static int getWaitStatesSince(GCNHazardRecognizer::IsHazardFn IsHazard,
                              const MachineInstr *MI, IsExpiredFn IsExpired) {
  DenseSet<const MachineBasicBlock *> Visited;
  return getWaitStatesSince(IsHazard, MI->getParent(),
                            std::next(MI->getReverseIterator()),
                            0, IsExpired, Visited);
}

int GCNHazardRecognizer::getWaitStatesSince(IsHazardFn IsHazard, int Limit) {
  if (IsHazardRecognizerMode) {
    auto IsExpiredFn = [Limit](const MachineInstr &, int WaitStates) {
      return WaitStates >= Limit;
    };
    return ::getWaitStatesSince(IsHazard, CurrCycleInstr, IsExpiredFn);
  }

  int WaitStates = 0;
  for (MachineInstr *MI : EmittedInstrs) {
    if (MI) {
      if (IsHazard(*MI))
        return WaitStates;

      if (MI->isInlineAsm())
        continue;
    }
    ++WaitStates;

    if (WaitStates >= Limit)
      break;
  }
  return std::numeric_limits<int>::max();
}

int GCNHazardRecognizer::getWaitStatesSinceDef(unsigned Reg,
                                               IsHazardFn IsHazardDef,
                                               int Limit) {
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  auto IsHazardFn = [IsHazardDef, TRI, Reg](const MachineInstr &MI) {
    return IsHazardDef(MI) && MI.modifiesRegister(Reg, TRI);
  };

  return getWaitStatesSince(IsHazardFn, Limit);
}

int GCNHazardRecognizer::getWaitStatesSinceSetReg(IsHazardFn IsHazard,
                                                  int Limit) {
  auto IsHazardFn = [IsHazard](const MachineInstr &MI) {
    return isSSetReg(MI.getOpcode()) && IsHazard(MI);
  };

  return getWaitStatesSince(IsHazardFn, Limit);
}

//===----------------------------------------------------------------------===//
// No-op Hazard Detection
//===----------------------------------------------------------------------===//

static void addRegUnits(const SIRegisterInfo &TRI, BitVector &BV,
                        MCRegister Reg) {
  for (MCRegUnit Unit : TRI.regunits(Reg))
    BV.set(Unit);
}

static void addRegsToSet(const SIRegisterInfo &TRI,
                         iterator_range<MachineInstr::const_mop_iterator> Ops,
                         BitVector &DefSet, BitVector &UseSet) {
  for (const MachineOperand &Op : Ops) {
    if (Op.isReg())
      addRegUnits(TRI, Op.isDef() ? DefSet : UseSet, Op.getReg().asMCReg());
  }
}

void GCNHazardRecognizer::addClauseInst(const MachineInstr &MI) {
  addRegsToSet(TRI, MI.operands(), ClauseDefs, ClauseUses);
}

static bool breaksSMEMSoftClause(MachineInstr *MI) {
  return !SIInstrInfo::isSMRD(*MI);
}

static bool breaksVMEMSoftClause(MachineInstr *MI) {
  return !SIInstrInfo::isVMEM(*MI) && !SIInstrInfo::isFLAT(*MI);
}

int GCNHazardRecognizer::checkSoftClauseHazards(MachineInstr *MEM) {
  // SMEM soft clause are only present on VI+, and only matter if xnack is
  // enabled.
  if (!ST.isXNACKEnabled())
    return 0;

  bool IsSMRD = TII.isSMRD(*MEM);

  resetClause();

  // A soft-clause is any group of consecutive SMEM instructions.  The
  // instructions in this group may return out of order and/or may be
  // replayed (i.e. the same instruction issued more than once).
  //
  // In order to handle these situations correctly we need to make sure that
  // when a clause has more than one instruction, no instruction in the clause
  // writes to a register that is read by another instruction in the clause
  // (including itself). If we encounter this situation, we need to break the
  // clause by inserting a non SMEM instruction.

  for (MachineInstr *MI : EmittedInstrs) {
    // When we hit a non-SMEM instruction then we have passed the start of the
    // clause and we can stop.
    if (!MI)
      break;

    if (IsSMRD ? breaksSMEMSoftClause(MI) : breaksVMEMSoftClause(MI))
      break;

    addClauseInst(*MI);
  }

  if (ClauseDefs.none())
    return 0;

  // We need to make sure not to put loads and stores in the same clause if they
  // use the same address. For now, just start a new clause whenever we see a
  // store.
  if (MEM->mayStore())
    return 1;

  addClauseInst(*MEM);

  // If the set of defs and uses intersect then we cannot add this instruction
  // to the clause, so we have a hazard.
  return ClauseDefs.anyCommon(ClauseUses) ? 1 : 0;
}

int GCNHazardRecognizer::checkSMRDHazards(MachineInstr *SMRD) {
  int WaitStatesNeeded = 0;

  WaitStatesNeeded = checkSoftClauseHazards(SMRD);

  // This SMRD hazard only affects SI.
  if (!ST.hasSMRDReadVALUDefHazard())
    return WaitStatesNeeded;

  // A read of an SGPR by SMRD instruction requires 4 wait states when the
  // SGPR was written by a VALU instruction.
  int SmrdSgprWaitStates = 4;
  auto IsHazardDefFn = [this](const MachineInstr &MI) {
    return TII.isVALU(MI);
  };
  auto IsBufferHazardDefFn = [this](const MachineInstr &MI) {
    return TII.isSALU(MI);
  };

  bool IsBufferSMRD = TII.isBufferSMRD(*SMRD);

  for (const MachineOperand &Use : SMRD->uses()) {
    if (!Use.isReg())
      continue;
    int WaitStatesNeededForUse =
        SmrdSgprWaitStates - getWaitStatesSinceDef(Use.getReg(), IsHazardDefFn,
                                                   SmrdSgprWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    // This fixes what appears to be undocumented hardware behavior in SI where
    // s_mov writing a descriptor and s_buffer_load_dword reading the descriptor
    // needs some number of nops in between. We don't know how many we need, but
    // let's use 4. This wasn't discovered before probably because the only
    // case when this happens is when we expand a 64-bit pointer into a full
    // descriptor and use s_buffer_load_dword instead of s_load_dword, which was
    // probably never encountered in the closed-source land.
    if (IsBufferSMRD) {
      int WaitStatesNeededForUse =
        SmrdSgprWaitStates - getWaitStatesSinceDef(Use.getReg(),
                                                   IsBufferHazardDefFn,
                                                   SmrdSgprWaitStates);
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
    }
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkVMEMHazards(MachineInstr* VMEM) {
  if (!ST.hasVMEMReadSGPRVALUDefHazard())
    return 0;

  int WaitStatesNeeded = checkSoftClauseHazards(VMEM);

  // A read of an SGPR by a VMEM instruction requires 5 wait states when the
  // SGPR was written by a VALU Instruction.
  const int VmemSgprWaitStates = 5;
  auto IsHazardDefFn = [this](const MachineInstr &MI) {
    return TII.isVALU(MI);
  };
  for (const MachineOperand &Use : VMEM->uses()) {
    if (!Use.isReg() || TRI.isVectorRegister(MF.getRegInfo(), Use.getReg()))
      continue;

    int WaitStatesNeededForUse =
        VmemSgprWaitStates - getWaitStatesSinceDef(Use.getReg(), IsHazardDefFn,
                                                   VmemSgprWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }
  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkDPPHazards(MachineInstr *DPP) {
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();

  // Check for DPP VGPR read after VALU VGPR write and EXEC write.
  int DppVgprWaitStates = 2;
  int DppExecWaitStates = 5;
  int WaitStatesNeeded = 0;
  auto IsHazardDefFn = [TII](const MachineInstr &MI) {
    return TII->isVALU(MI);
  };

  for (const MachineOperand &Use : DPP->uses()) {
    if (!Use.isReg() || !TRI->isVGPR(MF.getRegInfo(), Use.getReg()))
      continue;
    int WaitStatesNeededForUse =
        DppVgprWaitStates - getWaitStatesSinceDef(
                                Use.getReg(),
                                [](const MachineInstr &) { return true; },
                                DppVgprWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  WaitStatesNeeded = std::max(
      WaitStatesNeeded,
      DppExecWaitStates - getWaitStatesSinceDef(AMDGPU::EXEC, IsHazardDefFn,
                                                DppExecWaitStates));

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkDivFMasHazards(MachineInstr *DivFMas) {
  const SIInstrInfo *TII = ST.getInstrInfo();

  // v_div_fmas requires 4 wait states after a write to vcc from a VALU
  // instruction.
  const int DivFMasWaitStates = 4;
  auto IsHazardDefFn = [TII](const MachineInstr &MI) {
    return TII->isVALU(MI);
  };
  int WaitStatesNeeded = getWaitStatesSinceDef(AMDGPU::VCC, IsHazardDefFn,
                                               DivFMasWaitStates);

  return DivFMasWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::checkGetRegHazards(MachineInstr *GetRegInstr) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned GetRegHWReg = getHWReg(TII, *GetRegInstr);

  const int GetRegWaitStates = 2;
  auto IsHazardFn = [TII, GetRegHWReg](const MachineInstr &MI) {
    return GetRegHWReg == getHWReg(TII, MI);
  };
  int WaitStatesNeeded = getWaitStatesSinceSetReg(IsHazardFn, GetRegWaitStates);

  return GetRegWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::checkSetRegHazards(MachineInstr *SetRegInstr) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned HWReg = getHWReg(TII, *SetRegInstr);

  const int SetRegWaitStates = ST.getSetRegWaitStates();
  auto IsHazardFn = [TII, HWReg](const MachineInstr &MI) {
    return HWReg == getHWReg(TII, MI);
  };
  int WaitStatesNeeded = getWaitStatesSinceSetReg(IsHazardFn, SetRegWaitStates);
  return SetRegWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::createsVALUHazard(const MachineInstr &MI) {
  if (!MI.mayStore())
    return -1;

  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MI.getDesc();

  int VDataIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vdata);
  int VDataRCID = -1;
  if (VDataIdx != -1)
    VDataRCID = Desc.operands()[VDataIdx].RegClass;

  if (TII->isMUBUF(MI) || TII->isMTBUF(MI)) {
    // There is no hazard if the instruction does not use vector regs
    // (like wbinvl1)
    if (VDataIdx == -1)
      return -1;
    // For MUBUF/MTBUF instructions this hazard only exists if the
    // instruction is not using a register in the soffset field.
    const MachineOperand *SOffset =
        TII->getNamedOperand(MI, AMDGPU::OpName::soffset);
    // If we have no soffset operand, then assume this field has been
    // hardcoded to zero.
    if (AMDGPU::getRegBitWidth(VDataRCID) > 64 &&
        (!SOffset || !SOffset->isReg()))
      return VDataIdx;
  }

  // MIMG instructions create a hazard if they don't use a 256-bit T# and
  // the store size is greater than 8 bytes and they have more than two bits
  // of their dmask set.
  // All our MIMG definitions use a 256-bit T#, so we can skip checking for them.
  if (TII->isMIMG(MI)) {
    int SRsrcIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::srsrc);
    assert(SRsrcIdx != -1 &&
           AMDGPU::getRegBitWidth(Desc.operands()[SRsrcIdx].RegClass) == 256);
    (void)SRsrcIdx;
  }

  if (TII->isFLAT(MI)) {
    int DataIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vdata);
    if (AMDGPU::getRegBitWidth(Desc.operands()[DataIdx].RegClass) > 64)
      return DataIdx;
  }

  return -1;
}

int
GCNHazardRecognizer::checkVALUHazardsHelper(const MachineOperand &Def,
                                            const MachineRegisterInfo &MRI) {
  // Helper to check for the hazard where VMEM instructions that store more than
  // 8 bytes can have there store data over written by the next instruction.
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  const int VALUWaitStates = ST.hasGFX940Insts() ? 2 : 1;
  int WaitStatesNeeded = 0;

  if (!TRI->isVectorRegister(MRI, Def.getReg()))
    return WaitStatesNeeded;
  Register Reg = Def.getReg();
  auto IsHazardFn = [this, Reg, TRI](const MachineInstr &MI) {
    int DataIdx = createsVALUHazard(MI);
    return DataIdx >= 0 &&
           TRI->regsOverlap(MI.getOperand(DataIdx).getReg(), Reg);
  };
  int WaitStatesNeededForDef =
    VALUWaitStates - getWaitStatesSince(IsHazardFn, VALUWaitStates);
  WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkVALUHazards(MachineInstr *VALU) {
  int WaitStatesNeeded = 0;

  if (ST.hasTransForwardingHazard() && !SIInstrInfo::isTRANS(*VALU)) {
    const int TransDefWaitstates = 1;

    auto IsTransDefFn = [this, VALU](const MachineInstr &MI) {
      if (!SIInstrInfo::isTRANS(MI))
        return false;
      const SIRegisterInfo *TRI = ST.getRegisterInfo();
      const SIInstrInfo *TII = ST.getInstrInfo();
      Register Def = TII->getNamedOperand(MI, AMDGPU::OpName::vdst)->getReg();

      for (const MachineOperand &Use : VALU->explicit_uses()) {
        if (Use.isReg() && TRI->regsOverlap(Def, Use.getReg()))
          return true;
      }

      return false;
    };

    int WaitStatesNeededForDef =
        TransDefWaitstates -
        getWaitStatesSince(IsTransDefFn, TransDefWaitstates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);
  }

  if (ST.hasDstSelForwardingHazard()) {
    const int Shift16DefWaitstates = 1;

    auto IsShift16BitDefFn = [this, VALU](const MachineInstr &MI) {
      if (!SIInstrInfo::isVALU(MI))
        return false;
      const SIInstrInfo *TII = ST.getInstrInfo();
      if (SIInstrInfo::isSDWA(MI)) {
        if (auto *DstSel = TII->getNamedOperand(MI, AMDGPU::OpName::dst_sel))
          if (DstSel->getImm() == AMDGPU::SDWA::DWORD)
            return false;
      } else {
        if (!AMDGPU::hasNamedOperand(MI.getOpcode(), AMDGPU::OpName::op_sel) ||
            !(TII->getNamedOperand(MI, AMDGPU::OpName::src0_modifiers)
                  ->getImm() &
              SISrcMods::DST_OP_SEL))
          return false;
      }
      const SIRegisterInfo *TRI = ST.getRegisterInfo();
      if (auto *Dst = TII->getNamedOperand(MI, AMDGPU::OpName::vdst)) {
        Register Def = Dst->getReg();

        for (const MachineOperand &Use : VALU->explicit_uses()) {
          if (Use.isReg() && TRI->regsOverlap(Def, Use.getReg()))
            return true;
        }
      }

      return false;
    };

    int WaitStatesNeededForDef =
        Shift16DefWaitstates -
        getWaitStatesSince(IsShift16BitDefFn, Shift16DefWaitstates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);
  }

  if (ST.hasVDecCoExecHazard()) {
    const int VALUWriteSGPRVALUReadWaitstates = 2;
    const int VALUWriteEXECRWLane = 4;
    const int VALUWriteVGPRReadlaneRead = 1;

    const SIRegisterInfo *TRI = ST.getRegisterInfo();
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    Register UseReg;
    auto IsVALUDefSGPRFn = [&UseReg, TRI](const MachineInstr &MI) {
      if (!SIInstrInfo::isVALU(MI))
        return false;
      return MI.modifiesRegister(UseReg, TRI);
    };

    for (const MachineOperand &Use : VALU->explicit_uses()) {
      if (!Use.isReg())
        continue;

      UseReg = Use.getReg();
      if (TRI->isSGPRReg(MRI, UseReg)) {
        int WaitStatesNeededForDef =
            VALUWriteSGPRVALUReadWaitstates -
            getWaitStatesSince(IsVALUDefSGPRFn,
                               VALUWriteSGPRVALUReadWaitstates);
        WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);
      }
    }

    if (VALU->readsRegister(AMDGPU::VCC, TRI)) {
      UseReg = AMDGPU::VCC;
      int WaitStatesNeededForDef =
          VALUWriteSGPRVALUReadWaitstates -
          getWaitStatesSince(IsVALUDefSGPRFn, VALUWriteSGPRVALUReadWaitstates);
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);
    }

    switch (VALU->getOpcode()) {
    case AMDGPU::V_READLANE_B32:
    case AMDGPU::V_READFIRSTLANE_B32: {
      MachineOperand *Src = TII.getNamedOperand(*VALU, AMDGPU::OpName::src0);
      UseReg = Src->getReg();
      int WaitStatesNeededForDef =
          VALUWriteVGPRReadlaneRead -
          getWaitStatesSince(IsVALUDefSGPRFn, VALUWriteVGPRReadlaneRead);
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);
    }
      [[fallthrough]];
    case AMDGPU::V_WRITELANE_B32: {
      UseReg = AMDGPU::EXEC;
      int WaitStatesNeededForDef =
          VALUWriteEXECRWLane -
          getWaitStatesSince(IsVALUDefSGPRFn, VALUWriteEXECRWLane);
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);
      break;
    }
    default:
      break;
    }
  }

  // This checks for the hazard where VMEM instructions that store more than
  // 8 bytes can have there store data over written by the next instruction.
  if (!ST.has12DWordStoreHazard())
    return WaitStatesNeeded;

  const MachineRegisterInfo &MRI = MF.getRegInfo();

  for (const MachineOperand &Def : VALU->defs()) {
    WaitStatesNeeded = std::max(WaitStatesNeeded, checkVALUHazardsHelper(Def, MRI));
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkInlineAsmHazards(MachineInstr *IA) {
  // This checks for hazards associated with inline asm statements.
  // Since inline asms can contain just about anything, we use this
  // to call/leverage other check*Hazard routines. Note that
  // this function doesn't attempt to address all possible inline asm
  // hazards (good luck), but is a collection of what has been
  // problematic thus far.

  // see checkVALUHazards()
  if (!ST.has12DWordStoreHazard())
    return 0;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  int WaitStatesNeeded = 0;

  for (const MachineOperand &Op :
       llvm::drop_begin(IA->operands(), InlineAsm::MIOp_FirstOperand)) {
    if (Op.isReg() && Op.isDef()) {
      WaitStatesNeeded =
          std::max(WaitStatesNeeded, checkVALUHazardsHelper(Op, MRI));
    }
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkRWLaneHazards(MachineInstr *RWLane) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  const MachineOperand *LaneSelectOp =
      TII->getNamedOperand(*RWLane, AMDGPU::OpName::src1);

  if (!LaneSelectOp->isReg() || !TRI->isSGPRReg(MRI, LaneSelectOp->getReg()))
    return 0;

  Register LaneSelectReg = LaneSelectOp->getReg();
  auto IsHazardFn = [TII](const MachineInstr &MI) { return TII->isVALU(MI); };

  const int RWLaneWaitStates = 4;
  int WaitStatesSince = getWaitStatesSinceDef(LaneSelectReg, IsHazardFn,
                                              RWLaneWaitStates);
  return RWLaneWaitStates - WaitStatesSince;
}

int GCNHazardRecognizer::checkRFEHazards(MachineInstr *RFE) {
  if (!ST.hasRFEHazards())
    return 0;

  const SIInstrInfo *TII = ST.getInstrInfo();

  const int RFEWaitStates = 1;

  auto IsHazardFn = [TII](const MachineInstr &MI) {
    return getHWReg(TII, MI) == AMDGPU::Hwreg::ID_TRAPSTS;
  };
  int WaitStatesNeeded = getWaitStatesSinceSetReg(IsHazardFn, RFEWaitStates);
  return RFEWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::checkReadM0Hazards(MachineInstr *MI) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const int ReadM0WaitStates = 1;
  auto IsHazardFn = [TII](const MachineInstr &MI) { return TII->isSALU(MI); };
  return ReadM0WaitStates -
         getWaitStatesSinceDef(AMDGPU::M0, IsHazardFn, ReadM0WaitStates);
}

void GCNHazardRecognizer::fixHazards(MachineInstr *MI) {
  fixVMEMtoScalarWriteHazards(MI);
  fixVcmpxPermlaneHazards(MI);
  fixSMEMtoVectorWriteHazards(MI);
  fixVcmpxExecWARHazard(MI);
  fixLdsBranchVmemWARHazard(MI);
  if (ST.hasLdsDirect()) {
    fixLdsDirectVALUHazard(MI);
    fixLdsDirectVMEMHazard(MI);
  }
  fixVALUPartialForwardingHazard(MI);
  fixVALUTransUseHazard(MI);
  fixWMMAHazards(MI);
  fixShift64HighRegBug(MI);
  fixVALUMaskWriteHazard(MI);
  fixRequiredExportPriority(MI);
}

bool GCNHazardRecognizer::fixVcmpxPermlaneHazards(MachineInstr *MI) {
  if (!ST.hasVcmpxPermlaneHazard() || !isPermlane(*MI))
    return false;

  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  auto IsHazardFn = [TII, TRI](const MachineInstr &MI) {
    return (TII->isVOPC(MI) ||
            ((TII->isVOP3(MI) || TII->isSDWA(MI)) && MI.isCompare())) &&
           MI.modifiesRegister(AMDGPU::EXEC, TRI);
  };

  auto IsExpiredFn = [](const MachineInstr &MI, int) {
    unsigned Opc = MI.getOpcode();
    return SIInstrInfo::isVALU(MI) && Opc != AMDGPU::V_NOP_e32 &&
           Opc != AMDGPU::V_NOP_e64 && Opc != AMDGPU::V_NOP_sdwa;
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  // V_NOP will be discarded by SQ.
  // Use V_MOV_B32 v?, v?. Register must be alive so use src0 of V_PERMLANE*
  // which is always a VGPR and available.
  auto *Src0 = TII->getNamedOperand(*MI, AMDGPU::OpName::src0);
  Register Reg = Src0->getReg();
  bool IsUndef = Src0->isUndef();
  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII->get(AMDGPU::V_MOV_B32_e32))
    .addReg(Reg, RegState::Define | (IsUndef ? RegState::Dead : 0))
    .addReg(Reg, IsUndef ? RegState::Undef : RegState::Kill);

  return true;
}

bool GCNHazardRecognizer::fixVMEMtoScalarWriteHazards(MachineInstr *MI) {
  if (!ST.hasVMEMtoScalarWriteHazard())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  if (!SIInstrInfo::isSALU(*MI) && !SIInstrInfo::isSMRD(*MI))
    return false;

  if (MI->getNumDefs() == 0)
    return false;

  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  auto IsHazardFn = [TRI, MI](const MachineInstr &I) {
    if (!SIInstrInfo::isVMEM(I) && !SIInstrInfo::isDS(I) &&
        !SIInstrInfo::isFLAT(I))
      return false;

    for (const MachineOperand &Def : MI->defs()) {
      const MachineOperand *Op =
          I.findRegisterUseOperand(Def.getReg(), TRI, false);
      if (!Op)
        continue;
      return true;
    }
    return false;
  };

  auto IsExpiredFn = [](const MachineInstr &MI, int) {
    return SIInstrInfo::isVALU(MI) ||
           (MI.getOpcode() == AMDGPU::S_WAITCNT &&
            !MI.getOperand(0).getImm()) ||
           (MI.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
            AMDGPU::DepCtr::decodeFieldVmVsrc(MI.getOperand(0).getImm()) == 0);
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  const SIInstrInfo *TII = ST.getInstrInfo();
  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII->get(AMDGPU::S_WAITCNT_DEPCTR))
      .addImm(AMDGPU::DepCtr::encodeFieldVmVsrc(0));
  return true;
}

bool GCNHazardRecognizer::fixSMEMtoVectorWriteHazards(MachineInstr *MI) {
  if (!ST.hasSMEMtoVectorWriteHazard())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  if (!SIInstrInfo::isVALU(*MI))
    return false;

  unsigned SDSTName;
  switch (MI->getOpcode()) {
  case AMDGPU::V_READLANE_B32:
  case AMDGPU::V_READFIRSTLANE_B32:
    SDSTName = AMDGPU::OpName::vdst;
    break;
  default:
    SDSTName = AMDGPU::OpName::sdst;
    break;
  }

  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const AMDGPU::IsaVersion IV = AMDGPU::getIsaVersion(ST.getCPU());
  const MachineOperand *SDST = TII->getNamedOperand(*MI, SDSTName);
  if (!SDST) {
    for (const auto &MO : MI->implicit_operands()) {
      if (MO.isDef() && TRI->isSGPRClass(TRI->getPhysRegBaseClass(MO.getReg()))) {
        SDST = &MO;
        break;
      }
    }
  }

  if (!SDST)
    return false;

  const Register SDSTReg = SDST->getReg();
  auto IsHazardFn = [SDSTReg, TRI](const MachineInstr &I) {
    return SIInstrInfo::isSMRD(I) && I.readsRegister(SDSTReg, TRI);
  };

  auto IsExpiredFn = [TII, IV](const MachineInstr &MI, int) {
    if (TII->isSALU(MI)) {
      switch (MI.getOpcode()) {
      case AMDGPU::S_SETVSKIP:
      case AMDGPU::S_VERSION:
      case AMDGPU::S_WAITCNT_VSCNT:
      case AMDGPU::S_WAITCNT_VMCNT:
      case AMDGPU::S_WAITCNT_EXPCNT:
        // These instructions cannot not mitigate the hazard.
        return false;
      case AMDGPU::S_WAITCNT_LGKMCNT:
        // Reducing lgkmcnt count to 0 always mitigates the hazard.
        return (MI.getOperand(1).getImm() == 0) &&
               (MI.getOperand(0).getReg() == AMDGPU::SGPR_NULL);
      case AMDGPU::S_WAITCNT: {
        const int64_t Imm = MI.getOperand(0).getImm();
        AMDGPU::Waitcnt Decoded = AMDGPU::decodeWaitcnt(IV, Imm);
        // DsCnt corresponds to LGKMCnt here.
        return (Decoded.DsCnt == 0);
      }
      default:
        // SOPP instructions cannot mitigate the hazard.
        if (TII->isSOPP(MI))
          return false;
        // At this point the SALU can be assumed to mitigate the hazard
        // because either:
        // (a) it is independent of the at risk SMEM (breaking chain),
        // or
        // (b) it is dependent on the SMEM, in which case an appropriate
        //     s_waitcnt lgkmcnt _must_ exist between it and the at risk
        //     SMEM instruction.
        return true;
      }
    }
    return false;
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII->get(AMDGPU::S_MOV_B32), AMDGPU::SGPR_NULL)
      .addImm(0);
  return true;
}

bool GCNHazardRecognizer::fixVcmpxExecWARHazard(MachineInstr *MI) {
  if (!ST.hasVcmpxExecWARHazard())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  if (!SIInstrInfo::isVALU(*MI))
    return false;

  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  if (!MI->modifiesRegister(AMDGPU::EXEC, TRI))
    return false;

  auto IsHazardFn = [TRI](const MachineInstr &I) {
    if (SIInstrInfo::isVALU(I))
      return false;
    return I.readsRegister(AMDGPU::EXEC, TRI);
  };

  const SIInstrInfo *TII = ST.getInstrInfo();
  auto IsExpiredFn = [TII, TRI](const MachineInstr &MI, int) {
    if (SIInstrInfo::isVALU(MI)) {
      if (TII->getNamedOperand(MI, AMDGPU::OpName::sdst))
        return true;
      for (auto MO : MI.implicit_operands())
        if (MO.isDef() && TRI->isSGPRClass(TRI->getPhysRegBaseClass(MO.getReg())))
          return true;
    }
    if (MI.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
        AMDGPU::DepCtr::decodeFieldSaSdst(MI.getOperand(0).getImm()) == 0)
      return true;
    return false;
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII->get(AMDGPU::S_WAITCNT_DEPCTR))
      .addImm(AMDGPU::DepCtr::encodeFieldSaSdst(0));
  return true;
}

static bool shouldRunLdsBranchVmemWARHazardFixup(const MachineFunction &MF,
                                                 const GCNSubtarget &ST) {
  if (!ST.hasLdsBranchVmemWARHazard())
    return false;

  // Check if the necessary condition for the hazard is met: both LDS and VMEM
  // instructions need to appear in the same function.
  bool HasLds = false;
  bool HasVmem = false;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      HasLds |= SIInstrInfo::isDS(MI);
      HasVmem |=
          SIInstrInfo::isVMEM(MI) || SIInstrInfo::isSegmentSpecificFLAT(MI);
      if (HasLds && HasVmem)
        return true;
    }
  }
  return false;
}

static bool isStoreCountWaitZero(const MachineInstr &I) {
  return I.getOpcode() == AMDGPU::S_WAITCNT_VSCNT &&
         I.getOperand(0).getReg() == AMDGPU::SGPR_NULL &&
         !I.getOperand(1).getImm();
}

bool GCNHazardRecognizer::fixLdsBranchVmemWARHazard(MachineInstr *MI) {
  if (!RunLdsBranchVmemWARHazardFixup)
    return false;

  assert(ST.hasLdsBranchVmemWARHazard());
  assert(!ST.hasExtendedWaitCounts());

  auto IsHazardInst = [](const MachineInstr &MI) {
    if (SIInstrInfo::isDS(MI))
      return 1;
    if (SIInstrInfo::isVMEM(MI) || SIInstrInfo::isSegmentSpecificFLAT(MI))
      return 2;
    return 0;
  };

  auto InstType = IsHazardInst(*MI);
  if (!InstType)
    return false;

  auto IsExpiredFn = [&IsHazardInst](const MachineInstr &I, int) {
    return IsHazardInst(I) || isStoreCountWaitZero(I);
  };

  auto IsHazardFn = [InstType, &IsHazardInst](const MachineInstr &I) {
    if (!I.isBranch())
      return false;

    auto IsHazardFn = [InstType, IsHazardInst](const MachineInstr &I) {
      auto InstType2 = IsHazardInst(I);
      return InstType2 && InstType != InstType2;
    };

    auto IsExpiredFn = [InstType, &IsHazardInst](const MachineInstr &I, int) {
      auto InstType2 = IsHazardInst(I);
      if (InstType == InstType2)
        return true;

      return isStoreCountWaitZero(I);
    };

    return ::getWaitStatesSince(IsHazardFn, &I, IsExpiredFn) !=
           std::numeric_limits<int>::max();
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  const SIInstrInfo *TII = ST.getInstrInfo();
  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII->get(AMDGPU::S_WAITCNT_VSCNT))
    .addReg(AMDGPU::SGPR_NULL, RegState::Undef)
    .addImm(0);

  return true;
}

bool GCNHazardRecognizer::fixLdsDirectVALUHazard(MachineInstr *MI) {
  if (!SIInstrInfo::isLDSDIR(*MI))
    return false;

  const int NoHazardWaitStates = 15;
  const MachineOperand *VDST = TII.getNamedOperand(*MI, AMDGPU::OpName::vdst);
  const Register VDSTReg = VDST->getReg();

  bool VisitedTrans = false;
  auto IsHazardFn = [this, VDSTReg, &VisitedTrans](const MachineInstr &I) {
    if (!SIInstrInfo::isVALU(I))
      return false;
    VisitedTrans = VisitedTrans || SIInstrInfo::isTRANS(I);
    // Cover both WAR and WAW
    return I.readsRegister(VDSTReg, &TRI) || I.modifiesRegister(VDSTReg, &TRI);
  };
  auto IsExpiredFn = [&](const MachineInstr &I, int WaitStates) {
    if (WaitStates >= NoHazardWaitStates)
      return true;
    // Instructions which cause va_vdst==0 expire hazard
    return SIInstrInfo::isVMEM(I) || SIInstrInfo::isFLAT(I) ||
           SIInstrInfo::isDS(I) || SIInstrInfo::isEXP(I);
  };
  auto GetWaitStatesFn = [](const MachineInstr &MI) {
    return SIInstrInfo::isVALU(MI) ? 1 : 0;
  };

  DenseSet<const MachineBasicBlock *> Visited;
  auto Count = ::getWaitStatesSince(IsHazardFn, MI->getParent(),
                                    std::next(MI->getReverseIterator()), 0,
                                    IsExpiredFn, Visited, GetWaitStatesFn);

  // Transcendentals can execute in parallel to other VALUs.
  // This makes va_vdst count unusable with a mixture of VALU and TRANS.
  if (VisitedTrans)
    Count = 0;

  MachineOperand *WaitVdstOp =
      TII.getNamedOperand(*MI, AMDGPU::OpName::waitvdst);
  WaitVdstOp->setImm(std::min(Count, NoHazardWaitStates));

  return true;
}

bool GCNHazardRecognizer::fixLdsDirectVMEMHazard(MachineInstr *MI) {
  if (!SIInstrInfo::isLDSDIR(*MI))
    return false;

  const MachineOperand *VDST = TII.getNamedOperand(*MI, AMDGPU::OpName::vdst);
  const Register VDSTReg = VDST->getReg();

  auto IsHazardFn = [this, VDSTReg](const MachineInstr &I) {
    if (!SIInstrInfo::isVMEM(I) && !SIInstrInfo::isFLAT(I) &&
        !SIInstrInfo::isDS(I))
      return false;
    return I.readsRegister(VDSTReg, &TRI) || I.modifiesRegister(VDSTReg, &TRI);
  };
  bool LdsdirCanWait = ST.hasLdsWaitVMSRC();
  // TODO: On GFX12 the hazard should expire on S_WAIT_LOADCNT/SAMPLECNT/BVHCNT
  // according to the type of VMEM instruction.
  auto IsExpiredFn = [this, LdsdirCanWait](const MachineInstr &I, int) {
    return SIInstrInfo::isVALU(I) || SIInstrInfo::isEXP(I) ||
           (I.getOpcode() == AMDGPU::S_WAITCNT && !I.getOperand(0).getImm()) ||
           (I.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
            AMDGPU::DepCtr::decodeFieldVmVsrc(I.getOperand(0).getImm()) == 0) ||
           (LdsdirCanWait && SIInstrInfo::isLDSDIR(I) &&
            !TII.getNamedOperand(I, AMDGPU::OpName::waitvsrc)->getImm());
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  if (LdsdirCanWait) {
    TII.getNamedOperand(*MI, AMDGPU::OpName::waitvsrc)->setImm(0);
  } else {
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
            TII.get(AMDGPU::S_WAITCNT_DEPCTR))
        .addImm(AMDGPU::DepCtr::encodeFieldVmVsrc(0));
  }

  return true;
}

bool GCNHazardRecognizer::fixVALUPartialForwardingHazard(MachineInstr *MI) {
  if (!ST.hasVALUPartialForwardingHazard())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  if (!ST.isWave64() || !SIInstrInfo::isVALU(*MI))
    return false;

  SmallSetVector<Register, 4> SrcVGPRs;

  for (const MachineOperand &Use : MI->explicit_uses()) {
    if (Use.isReg() && TRI.isVGPR(MF.getRegInfo(), Use.getReg()))
      SrcVGPRs.insert(Use.getReg());
  }

  // Only applies with >= 2 unique VGPR sources
  if (SrcVGPRs.size() <= 1)
    return false;

  // Look for the following pattern:
  //   Va <- VALU [PreExecPos]
  //   intv1
  //   Exec <- SALU [ExecPos]
  //   intv2
  //   Vb <- VALU [PostExecPos]
  //   intv3
  //   MI Va, Vb (WaitState = 0)
  //
  // Where:
  // intv1 + intv2 <= 2 VALUs
  // intv3 <= 4 VALUs
  //
  // If found, insert an appropriate S_WAITCNT_DEPCTR before MI.

  const int Intv1plus2MaxVALUs = 2;
  const int Intv3MaxVALUs = 4;
  const int IntvMaxVALUs = 6;
  const int NoHazardVALUWaitStates = IntvMaxVALUs + 2;

  struct StateType {
    SmallDenseMap<Register, int, 4> DefPos;
    int ExecPos = std::numeric_limits<int>::max();
    int VALUs = 0;
  };

  StateType State;

  // This overloads expiry testing with all the hazard detection
  auto IsHazardFn = [&, this](StateType &State, const MachineInstr &I) {
    // Too many VALU states have passed
    if (State.VALUs > NoHazardVALUWaitStates)
      return HazardExpired;

    // Instructions which cause va_vdst==0 expire hazard
    if (SIInstrInfo::isVMEM(I) || SIInstrInfo::isFLAT(I) ||
        SIInstrInfo::isDS(I) || SIInstrInfo::isEXP(I) ||
        (I.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
         AMDGPU::DepCtr::decodeFieldVaVdst(I.getOperand(0).getImm()) == 0))
      return HazardExpired;

    // Track registers writes
    bool Changed = false;
    if (SIInstrInfo::isVALU(I)) {
      for (Register Src : SrcVGPRs) {
        if (!State.DefPos.count(Src) && I.modifiesRegister(Src, &TRI)) {
          State.DefPos[Src] = State.VALUs;
          Changed = true;
        }
      }
    } else if (SIInstrInfo::isSALU(I)) {
      if (State.ExecPos == std::numeric_limits<int>::max()) {
        if (!State.DefPos.empty() && I.modifiesRegister(AMDGPU::EXEC, &TRI)) {
          State.ExecPos = State.VALUs;
          Changed = true;
        }
      }
    }

    // Early expiration: too many VALUs in intv3
    if (State.VALUs > Intv3MaxVALUs && State.DefPos.empty())
      return HazardExpired;

    // Only evaluate state if something changed
    if (!Changed)
      return NoHazardFound;

    // Determine positions of VALUs pre/post exec change
    if (State.ExecPos == std::numeric_limits<int>::max())
      return NoHazardFound;

    int PreExecPos = std::numeric_limits<int>::max();
    int PostExecPos = std::numeric_limits<int>::max();

    for (auto Entry : State.DefPos) {
      int DefVALUs = Entry.second;
      if (DefVALUs != std::numeric_limits<int>::max()) {
        if (DefVALUs >= State.ExecPos)
          PreExecPos = std::min(PreExecPos, DefVALUs);
        else
          PostExecPos = std::min(PostExecPos, DefVALUs);
      }
    }

    // Need a VALUs post exec change
    if (PostExecPos == std::numeric_limits<int>::max())
      return NoHazardFound;

    // Too many VALUs in intv3?
    int Intv3VALUs = PostExecPos;
    if (Intv3VALUs > Intv3MaxVALUs)
      return HazardExpired;

    // Too many VALUs in intv2?
    int Intv2VALUs = (State.ExecPos - PostExecPos) - 1;
    if (Intv2VALUs > Intv1plus2MaxVALUs)
      return HazardExpired;

    // Need a VALUs pre exec change
    if (PreExecPos == std::numeric_limits<int>::max())
      return NoHazardFound;

    // Too many VALUs in intv1?
    int Intv1VALUs = PreExecPos - State.ExecPos;
    if (Intv1VALUs > Intv1plus2MaxVALUs)
      return HazardExpired;

    // Too many VALUs in intv1 + intv2
    if (Intv1VALUs + Intv2VALUs > Intv1plus2MaxVALUs)
      return HazardExpired;

    return HazardFound;
  };
  auto UpdateStateFn = [](StateType &State, const MachineInstr &MI) {
    if (SIInstrInfo::isVALU(MI))
      State.VALUs += 1;
  };

  DenseSet<const MachineBasicBlock *> Visited;
  if (!hasHazard<StateType>(State, IsHazardFn, UpdateStateFn, MI->getParent(),
                            std::next(MI->getReverseIterator()), Visited))
    return false;

  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII.get(AMDGPU::S_WAITCNT_DEPCTR))
      .addImm(0x0fff);

  return true;
}

bool GCNHazardRecognizer::fixVALUTransUseHazard(MachineInstr *MI) {
  if (!ST.hasVALUTransUseHazard())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  if (!SIInstrInfo::isVALU(*MI))
    return false;

  SmallSet<Register, 4> SrcVGPRs;

  for (const MachineOperand &Use : MI->explicit_uses()) {
    if (Use.isReg() && TRI.isVGPR(MF.getRegInfo(), Use.getReg()))
      SrcVGPRs.insert(Use.getReg());
  }

  // Look for the following pattern:
  //   Va <- TRANS VALU
  //   intv
  //   MI Va (WaitState = 0)
  //
  // Where:
  // intv <= 5 VALUs / 1 TRANS
  //
  // If found, insert an appropriate S_WAITCNT_DEPCTR before MI.

  const int IntvMaxVALUs = 5;
  const int IntvMaxTRANS = 1;

  struct StateType {
    int VALUs = 0;
    int TRANS = 0;
  };

  StateType State;

  // This overloads expiry testing with all the hazard detection
  auto IsHazardFn = [&, this](StateType &State, const MachineInstr &I) {
    // Too many VALU states have passed
    if (State.VALUs > IntvMaxVALUs || State.TRANS > IntvMaxTRANS)
      return HazardExpired;

    // Instructions which cause va_vdst==0 expire hazard
    if (SIInstrInfo::isVMEM(I) || SIInstrInfo::isFLAT(I) ||
        SIInstrInfo::isDS(I) || SIInstrInfo::isEXP(I) ||
        (I.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
         I.getOperand(0).getImm() == 0x0fff))
      return HazardExpired;

    // Track registers writes
    if (SIInstrInfo::isTRANS(I)) {
      for (Register Src : SrcVGPRs) {
        if (I.modifiesRegister(Src, &TRI)) {
          return HazardFound;
        }
      }
    }

    return NoHazardFound;
  };
  auto UpdateStateFn = [](StateType &State, const MachineInstr &MI) {
    if (SIInstrInfo::isVALU(MI))
      State.VALUs += 1;
    if (SIInstrInfo::isTRANS(MI))
      State.TRANS += 1;
  };

  DenseSet<const MachineBasicBlock *> Visited;
  if (!hasHazard<StateType>(State, IsHazardFn, UpdateStateFn, MI->getParent(),
                            std::next(MI->getReverseIterator()), Visited))
    return false;

  // Hazard is observed - insert a wait on va_dst counter to ensure hazard is
  // avoided.
  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII.get(AMDGPU::S_WAITCNT_DEPCTR))
      .addImm(AMDGPU::DepCtr::encodeFieldVaVdst(0));

  return true;
}

bool GCNHazardRecognizer::fixWMMAHazards(MachineInstr *MI) {
  if (!SIInstrInfo::isWMMA(*MI) && !SIInstrInfo::isSWMMAC(*MI))
    return false;

  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  auto IsHazardFn = [MI, TII, TRI, this](const MachineInstr &I) {
    if (!SIInstrInfo::isWMMA(I) && !SIInstrInfo::isSWMMAC(I))
      return false;

    // Src0(matrix A) or Src1(matrix B) of the current wmma instruction overlaps
    // with the dest(matrix D) of the previous wmma.
    const Register CurSrc0Reg =
        TII->getNamedOperand(*MI, AMDGPU::OpName::src0)->getReg();
    const Register CurSrc1Reg =
        TII->getNamedOperand(*MI, AMDGPU::OpName::src1)->getReg();

    const Register PrevDstReg =
        TII->getNamedOperand(I, AMDGPU::OpName::vdst)->getReg();

    if (TRI->regsOverlap(PrevDstReg, CurSrc0Reg) ||
        TRI->regsOverlap(PrevDstReg, CurSrc1Reg)) {
      return true;
    }

    // GFX12+ allows overlap of matrix C with PrevDstReg (hardware will stall)
    // but Index can't overlap with PrevDstReg.
    if (AMDGPU::isGFX12Plus(ST)) {
      if (SIInstrInfo::isSWMMAC(*MI)) {
        const Register CurIndex =
            TII->getNamedOperand(*MI, AMDGPU::OpName::src2)->getReg();
        if (TRI->regsOverlap(PrevDstReg, CurIndex))
          return true;
      }
      return false;
    }

    return false;
  };

  auto IsExpiredFn = [](const MachineInstr &I, int) {
    return SIInstrInfo::isVALU(I);
  };

  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), TII->get(AMDGPU::V_NOP_e32));

  return true;
}

bool GCNHazardRecognizer::fixShift64HighRegBug(MachineInstr *MI) {
  if (!ST.hasShift64HighRegBug())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  switch (MI->getOpcode()) {
  default:
    return false;
  case AMDGPU::V_LSHLREV_B64_e64:
  case AMDGPU::V_LSHRREV_B64_e64:
  case AMDGPU::V_ASHRREV_I64_e64:
    break;
  }

  MachineOperand *Amt = TII.getNamedOperand(*MI, AMDGPU::OpName::src0);
  if (!Amt->isReg())
    return false;

  Register AmtReg = Amt->getReg();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  // Check if this is a last VGPR in the allocation block.
  if (!TRI.isVGPR(MRI, AmtReg) || ((AmtReg - AMDGPU::VGPR0) & 7) != 7)
    return false;

  if (AmtReg != AMDGPU::VGPR255 && MRI.isPhysRegUsed(AmtReg + 1))
    return false;

  MachineOperand *Src1 = TII.getNamedOperand(*MI, AMDGPU::OpName::src1);
  bool OverlappedSrc = Src1->isReg() && TRI.regsOverlap(Src1->getReg(), AmtReg);
  bool OverlappedDst = MI->modifiesRegister(AmtReg, &TRI);
  bool Overlapped = OverlappedSrc || OverlappedDst;

  assert(!OverlappedDst || !OverlappedSrc ||
         Src1->getReg() == MI->getOperand(0).getReg());
  assert(ST.needsAlignedVGPRs());
  static_assert(AMDGPU::VGPR0 + 1 == AMDGPU::VGPR1);

  Register NewReg;
  for (MCRegister Reg : Overlapped ? AMDGPU::VReg_64_Align2RegClass
                                   : AMDGPU::VGPR_32RegClass) {
    if (!MI->modifiesRegister(Reg, &TRI) && !MI->readsRegister(Reg, &TRI)) {
      NewReg = Reg;
      break;
    }
  }

  Register NewAmt = Overlapped ? (Register)TRI.getSubReg(NewReg, AMDGPU::sub1)
                               : NewReg;
  Register NewAmtLo;

  if (Overlapped)
    NewAmtLo = TRI.getSubReg(NewReg, AMDGPU::sub0);

  DebugLoc DL = MI->getDebugLoc();
  MachineBasicBlock *MBB = MI->getParent();
  // Insert a full wait count because found register might be pending a wait.
  BuildMI(*MBB, MI, DL, TII.get(AMDGPU::S_WAITCNT))
      .addImm(0);

  // Insert V_SWAP_B32 instruction(s) and run hazard recognizer on them.
  if (Overlapped)
    runOnInstruction(
        BuildMI(*MBB, MI, DL, TII.get(AMDGPU::V_SWAP_B32), NewAmtLo)
            .addDef(AmtReg - 1)
            .addReg(AmtReg - 1, RegState::Undef)
            .addReg(NewAmtLo, RegState::Undef));
  runOnInstruction(BuildMI(*MBB, MI, DL, TII.get(AMDGPU::V_SWAP_B32), NewAmt)
                       .addDef(AmtReg)
                       .addReg(AmtReg, RegState::Undef)
                       .addReg(NewAmt, RegState::Undef));

  // Instructions emitted after the current instruction will be processed by the
  // parent loop of the hazard recognizer in a natural way.
  BuildMI(*MBB, std::next(MI->getIterator()), DL, TII.get(AMDGPU::V_SWAP_B32),
          AmtReg)
      .addDef(NewAmt)
      .addReg(NewAmt)
      .addReg(AmtReg);
  if (Overlapped)
    BuildMI(*MBB, std::next(MI->getIterator()), DL, TII.get(AMDGPU::V_SWAP_B32),
            AmtReg - 1)
        .addDef(NewAmtLo)
        .addReg(NewAmtLo)
        .addReg(AmtReg - 1);

  // Re-running hazard recognizer on the modified instruction is not necessary,
  // inserted V_SWAP_B32 has already both read and write new registers so
  // hazards related to these register has already been handled.
  Amt->setReg(NewAmt);
  Amt->setIsKill(false);
  // We do not update liveness, so verifier may see it as undef.
  Amt->setIsUndef();
  if (OverlappedDst)
    MI->getOperand(0).setReg(NewReg);
  if (OverlappedSrc) {
    Src1->setReg(NewReg);
    Src1->setIsKill(false);
    Src1->setIsUndef();
  }

  return true;
}

int GCNHazardRecognizer::checkNSAtoVMEMHazard(MachineInstr *MI) {
  int NSAtoVMEMWaitStates = 1;

  if (!ST.hasNSAtoVMEMBug())
    return 0;

  if (!SIInstrInfo::isMUBUF(*MI) && !SIInstrInfo::isMTBUF(*MI))
    return 0;

  const SIInstrInfo *TII = ST.getInstrInfo();
  const auto *Offset = TII->getNamedOperand(*MI, AMDGPU::OpName::offset);
  if (!Offset || (Offset->getImm() & 6) == 0)
    return 0;

  auto IsHazardFn = [TII](const MachineInstr &I) {
    if (!SIInstrInfo::isMIMG(I))
      return false;
    const AMDGPU::MIMGInfo *Info = AMDGPU::getMIMGInfo(I.getOpcode());
    return Info->MIMGEncoding == AMDGPU::MIMGEncGfx10NSA &&
           TII->getInstSizeInBytes(I) >= 16;
  };

  return NSAtoVMEMWaitStates - getWaitStatesSince(IsHazardFn, 1);
}

int GCNHazardRecognizer::checkFPAtomicToDenormModeHazard(MachineInstr *MI) {
  int FPAtomicToDenormModeWaitStates = 3;

  if (!ST.hasFPAtomicToDenormModeHazard())
    return 0;
  assert(!ST.hasExtendedWaitCounts());

  if (MI->getOpcode() != AMDGPU::S_DENORM_MODE)
    return 0;

  auto IsHazardFn = [](const MachineInstr &I) {
    if (!SIInstrInfo::isVMEM(I) && !SIInstrInfo::isFLAT(I))
      return false;
    return SIInstrInfo::isFPAtomic(I);
  };

  auto IsExpiredFn = [](const MachineInstr &MI, int WaitStates) {
    if (WaitStates >= 3 || SIInstrInfo::isVALU(MI))
      return true;

    switch (MI.getOpcode()) {
    case AMDGPU::S_WAITCNT:
    case AMDGPU::S_WAITCNT_VSCNT:
    case AMDGPU::S_WAITCNT_VMCNT:
    case AMDGPU::S_WAITCNT_EXPCNT:
    case AMDGPU::S_WAITCNT_LGKMCNT:
    case AMDGPU::S_WAIT_IDLE:
      return true;
    default:
      break;
    }

    return false;
  };

  return FPAtomicToDenormModeWaitStates -
         ::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn);
}

int GCNHazardRecognizer::checkMAIHazards(MachineInstr *MI) {
  assert(SIInstrInfo::isMAI(*MI));

  return ST.hasGFX90AInsts() ? checkMAIHazards90A(MI) : checkMAIHazards908(MI);
}

int GCNHazardRecognizer::checkMFMAPadding(MachineInstr *MI) {
  // Early exit if no padding is requested.
  if (MFMAPaddingRatio == 0)
    return 0;

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  if (!SIInstrInfo::isMFMA(*MI) || MFI->getOccupancy() < 2)
    return 0;

  int NeighborMFMALatency = 0;
  auto IsNeighboringMFMA = [&NeighborMFMALatency,
                            this](const MachineInstr &MI) {
    if (!SIInstrInfo::isMFMA(MI))
      return false;

    NeighborMFMALatency = this->getMFMAPipelineWaitStates(MI);
    return true;
  };

  const int MaxMFMAPipelineWaitStates = 16;
  int WaitStatesSinceNeighborMFMA =
      getWaitStatesSince(IsNeighboringMFMA, MaxMFMAPipelineWaitStates);

  int NeighborMFMAPaddingNeeded =
      (NeighborMFMALatency * MFMAPaddingRatio / 100) -
      WaitStatesSinceNeighborMFMA;

  return std::max(0, NeighborMFMAPaddingNeeded);
}

int GCNHazardRecognizer::checkMAIHazards908(MachineInstr *MI) {
  int WaitStatesNeeded = 0;
  unsigned Opc = MI->getOpcode();

  auto IsVALUFn = [](const MachineInstr &MI) {
    return SIInstrInfo::isVALU(MI) || MI.isInlineAsm();
  };

  if (Opc != AMDGPU::V_ACCVGPR_READ_B32_e64) { // MFMA or v_accvgpr_write
    const int LegacyVALUWritesVGPRWaitStates = 2;
    const int VALUWritesExecWaitStates = 4;
    const int MaxWaitStates = 4;

    int WaitStatesNeededForUse = VALUWritesExecWaitStates -
      getWaitStatesSinceDef(AMDGPU::EXEC, IsVALUFn, MaxWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    if (WaitStatesNeeded < MaxWaitStates) {
      for (const MachineOperand &Use : MI->explicit_uses()) {
        const int MaxWaitStates = 2;

        if (!Use.isReg() || !TRI.isVGPR(MF.getRegInfo(), Use.getReg()))
          continue;

        int WaitStatesNeededForUse = LegacyVALUWritesVGPRWaitStates -
          getWaitStatesSinceDef(Use.getReg(), IsVALUFn, MaxWaitStates);
        WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

        if (WaitStatesNeeded == MaxWaitStates)
          break;
      }
    }
  }

  for (const MachineOperand &Op : MI->explicit_operands()) {
    if (!Op.isReg() || !TRI.isAGPR(MF.getRegInfo(), Op.getReg()))
      continue;

    if (Op.isDef() && Opc != AMDGPU::V_ACCVGPR_WRITE_B32_e64)
      continue;

    const int MFMAWritesAGPROverlappedSrcABWaitStates = 4;
    const int MFMAWritesAGPROverlappedSrcCWaitStates = 2;
    const int MFMA4x4WritesAGPRAccVgprReadWaitStates = 4;
    const int MFMA16x16WritesAGPRAccVgprReadWaitStates = 10;
    const int MFMA32x32WritesAGPRAccVgprReadWaitStates = 18;
    const int MFMA4x4WritesAGPRAccVgprWriteWaitStates = 1;
    const int MFMA16x16WritesAGPRAccVgprWriteWaitStates = 7;
    const int MFMA32x32WritesAGPRAccVgprWriteWaitStates = 15;
    const int MaxWaitStates = 18;
    Register Reg = Op.getReg();
    unsigned HazardDefLatency = 0;

    auto IsOverlappedMFMAFn = [Reg, &HazardDefLatency,
                               this](const MachineInstr &MI) {
      if (!SIInstrInfo::isMFMA(MI))
        return false;
      Register DstReg = MI.getOperand(0).getReg();
      if (DstReg == Reg)
        return false;
      HazardDefLatency =
          std::max(HazardDefLatency, TSchedModel.computeInstrLatency(&MI));
      return TRI.regsOverlap(DstReg, Reg);
    };

    int WaitStatesSinceDef = getWaitStatesSinceDef(Reg, IsOverlappedMFMAFn,
                                                   MaxWaitStates);
    int NeedWaitStates = MFMAWritesAGPROverlappedSrcABWaitStates;
    int SrcCIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2);
    int OpNo = Op.getOperandNo();
    if (OpNo == SrcCIdx) {
      NeedWaitStates = MFMAWritesAGPROverlappedSrcCWaitStates;
    } else if (Opc == AMDGPU::V_ACCVGPR_READ_B32_e64) {
      switch (HazardDefLatency) {
      case 2:  NeedWaitStates = MFMA4x4WritesAGPRAccVgprReadWaitStates;
               break;
      case 8:  NeedWaitStates = MFMA16x16WritesAGPRAccVgprReadWaitStates;
               break;
      case 16: [[fallthrough]];
      default: NeedWaitStates = MFMA32x32WritesAGPRAccVgprReadWaitStates;
               break;
      }
    } else if (Opc == AMDGPU::V_ACCVGPR_WRITE_B32_e64) {
      switch (HazardDefLatency) {
      case 2:  NeedWaitStates = MFMA4x4WritesAGPRAccVgprWriteWaitStates;
               break;
      case 8:  NeedWaitStates = MFMA16x16WritesAGPRAccVgprWriteWaitStates;
               break;
      case 16: [[fallthrough]];
      default: NeedWaitStates = MFMA32x32WritesAGPRAccVgprWriteWaitStates;
               break;
      }
    }

    int WaitStatesNeededForUse = NeedWaitStates - WaitStatesSinceDef;
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    if (WaitStatesNeeded == MaxWaitStates)
      return WaitStatesNeeded; // Early exit.

    auto IsAccVgprWriteFn = [Reg, this](const MachineInstr &MI) {
      if (MI.getOpcode() != AMDGPU::V_ACCVGPR_WRITE_B32_e64)
        return false;
      Register DstReg = MI.getOperand(0).getReg();
      return TRI.regsOverlap(Reg, DstReg);
    };

    const int AccVGPRWriteMFMAReadSrcCWaitStates = 1;
    const int AccVGPRWriteMFMAReadSrcABWaitStates = 3;
    const int AccVGPRWriteAccVgprReadWaitStates = 3;
    NeedWaitStates = AccVGPRWriteMFMAReadSrcABWaitStates;
    if (OpNo == SrcCIdx)
      NeedWaitStates = AccVGPRWriteMFMAReadSrcCWaitStates;
    else if (Opc == AMDGPU::V_ACCVGPR_READ_B32_e64)
      NeedWaitStates = AccVGPRWriteAccVgprReadWaitStates;

    WaitStatesNeededForUse = NeedWaitStates -
      getWaitStatesSinceDef(Reg, IsAccVgprWriteFn, MaxWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    if (WaitStatesNeeded == MaxWaitStates)
      return WaitStatesNeeded; // Early exit.
  }

  if (Opc == AMDGPU::V_ACCVGPR_WRITE_B32_e64) {
    const int MFMA4x4ReadSrcCAccVgprWriteWaitStates = 0;
    const int MFMA16x16ReadSrcCAccVgprWriteWaitStates = 5;
    const int MFMA32x32ReadSrcCAccVgprWriteWaitStates = 13;
    const int MaxWaitStates = 13;
    Register DstReg = MI->getOperand(0).getReg();
    unsigned HazardDefLatency = 0;

    auto IsSrcCMFMAFn = [DstReg, &HazardDefLatency,
                         this](const MachineInstr &MI) {
      if (!SIInstrInfo::isMFMA(MI))
        return false;
      Register Reg = TII.getNamedOperand(MI, AMDGPU::OpName::src2)->getReg();
      HazardDefLatency =
          std::max(HazardDefLatency, TSchedModel.computeInstrLatency(&MI));
      return TRI.regsOverlap(Reg, DstReg);
    };

    int WaitStatesSince = getWaitStatesSince(IsSrcCMFMAFn, MaxWaitStates);
    int NeedWaitStates;
    switch (HazardDefLatency) {
    case 2:  NeedWaitStates = MFMA4x4ReadSrcCAccVgprWriteWaitStates;
             break;
    case 8:  NeedWaitStates = MFMA16x16ReadSrcCAccVgprWriteWaitStates;
             break;
    case 16: [[fallthrough]];
    default: NeedWaitStates = MFMA32x32ReadSrcCAccVgprWriteWaitStates;
             break;
    }

    int WaitStatesNeededForUse = NeedWaitStates - WaitStatesSince;
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  // Pad neighboring MFMA with noops for better inter-wave performance.
  WaitStatesNeeded = std::max(WaitStatesNeeded, checkMFMAPadding(MI));

  return WaitStatesNeeded;
}

static int
GFX940_XDL_N_PassWritesVGPROverlappedSMFMASrcCWaitStates(int NumPasses) {
  // 2 pass -> 3
  // 4 pass -> 5
  // 8 pass -> 9
  // 16 pass -> 17
  return NumPasses + 1;
}

static int
GFX940_SMFMA_N_PassWritesVGPROverlappedSMFMASrcCWaitStates(int NumPasses) {
  // 2 pass -> 2
  // 4 pass -> 4
  // 8 pass -> 8
  // 16 pass -> 16
  return NumPasses;
}

static int
GFX940_SMFMA_N_PassWritesVGPROverlappedSrcABWaitStates(int NumPasses) {
  // 2 pass -> 4
  // 4 pass -> 6
  // 8 pass -> 10
  // 16 pass -> 18
  return NumPasses + 2;
}

static int GFX940_XDL_N_PassWritesVGPROverlappedSrcABWaitStates(int NumPasses) {
  // 2 pass -> 5
  // 4 pass -> 7
  // 8 pass -> 11
  // 16 pass -> 19
  return NumPasses + 3;
}

int GCNHazardRecognizer::checkMAIHazards90A(MachineInstr *MI) {
  int WaitStatesNeeded = 0;
  unsigned Opc = MI->getOpcode();

  auto IsLegacyVALUFn = [](const MachineInstr &MI) {
    return SIInstrInfo::isVALU(MI) && !SIInstrInfo::isMFMA(MI);
  };

  auto IsLegacyVALUNotDotFn = [](const MachineInstr &MI) {
    return SIInstrInfo::isVALU(MI) && !SIInstrInfo::isMFMA(MI) &&
           !SIInstrInfo::isDOT(MI);
  };

  if (!SIInstrInfo::isMFMA(*MI))
    return WaitStatesNeeded;

  const int VALUWritesExecWaitStates = 4;
  int WaitStatesNeededForUse = VALUWritesExecWaitStates -
    getWaitStatesSinceDef(AMDGPU::EXEC, IsLegacyVALUFn,
                          VALUWritesExecWaitStates);
  WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

  int SrcCIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2);

  // Loop for both DGEMM and S/HGEMM 2nd instruction.
  for (const MachineOperand &Use : MI->explicit_uses()) {
    const int LegacyVALUNotDotWritesVGPRWaitStates = 2;
    const int SMFMA4x4WritesVGPROverlappedSMFMASrcCWaitStates = 2;
    const int SMFMA16x16WritesVGPROverlappedSMFMASrcCWaitStates = 8;
    const int SMFMA32x32WritesVGPROverlappedSMFMASrcCWaitStates = 16;
    const int SMFMA4x4WritesVGPROverlappedDMFMASrcCWaitStates = 3;
    const int SMFMA16x16WritesVGPROverlappedDMFMASrcCWaitStates = 9;
    const int SMFMA32x32WritesVGPROverlappedDMFMASrcCWaitStates = 17;
    const int DMFMA16x16WritesVGPROverlappedSrcCWaitStates = 9;
    const int DMFMA4x4WritesVGPROverlappedSrcCWaitStates = 4;
    const int SMFMA4x4WritesVGPROverlappedSrcABWaitStates = 5;
    const int SMFMA16x16WritesVGPROverlappedSrcABWaitStates = 11;
    const int SMFMA32x32WritesVGPROverlappedSrcABWaitStates = 19;
    const int DMFMA4x4WritesVGPROverlappedMFMASrcABWaitStates = 6;
    const int DMFMA16x16WritesVGPROverlappedMFMASrcABWaitStates = 11;
    const int DMFMA4x4WritesVGPRFullSrcCWaitStates = 4;
    const int GFX940_SMFMA4x4WritesVGPRFullSrcCWaitStates = 2;
    const int MaxWaitStates = 19;

    if (!Use.isReg())
      continue;
    Register Reg = Use.getReg();
    bool FullReg;
    const MachineInstr *MI1;

    auto IsOverlappedMFMAFn = [Reg, &FullReg, &MI1,
                               this](const MachineInstr &MI) {
      if (!SIInstrInfo::isMFMA(MI))
        return false;
      Register DstReg = MI.getOperand(0).getReg();
      FullReg = (DstReg == Reg);
      MI1 = &MI;
      return TRI.regsOverlap(DstReg, Reg);
    };

    WaitStatesNeededForUse = LegacyVALUNotDotWritesVGPRWaitStates -
      getWaitStatesSinceDef(Reg, IsLegacyVALUNotDotFn, MaxWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    int NumWaitStates =
        getWaitStatesSinceDef(Reg, IsOverlappedMFMAFn, MaxWaitStates);
    if (NumWaitStates == std::numeric_limits<int>::max())
      continue;

    int OpNo = Use.getOperandNo();
    unsigned Opc1 = MI1->getOpcode();
    int NeedWaitStates = 0;
    if (OpNo == SrcCIdx) {
      if (!isDGEMM(Opc) && (!ST.hasGFX940Insts() && isDGEMM(Opc1))) {
        NeedWaitStates = 0;
      } else if (FullReg) {
        if ((Opc == AMDGPU::V_MFMA_F64_4X4X4F64_e64 ||
             Opc == AMDGPU::V_MFMA_F64_4X4X4F64_vgprcd_e64) &&
            (Opc1 == AMDGPU::V_MFMA_F64_4X4X4F64_e64 ||
             Opc1 == AMDGPU::V_MFMA_F64_4X4X4F64_vgprcd_e64))
          NeedWaitStates = DMFMA4x4WritesVGPRFullSrcCWaitStates;
        else if (ST.hasGFX940Insts() &&
                 TSchedModel.computeInstrLatency(MI1) == 2)
          NeedWaitStates = GFX940_SMFMA4x4WritesVGPRFullSrcCWaitStates;
      } else {
        switch (Opc1) {
        case AMDGPU::V_MFMA_F64_16X16X4F64_e64:
        case AMDGPU::V_MFMA_F64_16X16X4F64_vgprcd_e64:
        case AMDGPU::V_MFMA_F64_16X16X4F64_mac_e64:
        case AMDGPU::V_MFMA_F64_16X16X4F64_mac_vgprcd_e64:
          if (!isXDL(ST, *MI))
            NeedWaitStates = DMFMA16x16WritesVGPROverlappedSrcCWaitStates;
          break;
        case AMDGPU::V_MFMA_F64_4X4X4F64_e64:
        case AMDGPU::V_MFMA_F64_4X4X4F64_vgprcd_e64:
          if (!isXDL(ST, *MI))
            NeedWaitStates = DMFMA4x4WritesVGPROverlappedSrcCWaitStates;
          break;
        default:
          int NumPasses = TSchedModel.computeInstrLatency(MI1);
          if (ST.hasGFX940Insts()) {
            if (isXDL(ST, *MI) && !isXDL(ST, *MI1))
              break;

            NeedWaitStates =
                isXDL(ST, *MI1)
                    ? GFX940_XDL_N_PassWritesVGPROverlappedSMFMASrcCWaitStates(
                          NumPasses)
                    : GFX940_SMFMA_N_PassWritesVGPROverlappedSMFMASrcCWaitStates(
                          NumPasses);
            break;
          }

          switch (NumPasses) {
          case 2:
            NeedWaitStates =
                isDGEMM(Opc) ? SMFMA4x4WritesVGPROverlappedDMFMASrcCWaitStates
                             : SMFMA4x4WritesVGPROverlappedSMFMASrcCWaitStates;
            break;
          case 8:
            NeedWaitStates =
                isDGEMM(Opc)
                    ? SMFMA16x16WritesVGPROverlappedDMFMASrcCWaitStates
                    : SMFMA16x16WritesVGPROverlappedSMFMASrcCWaitStates;
            break;
          case 16:
            NeedWaitStates =
                isDGEMM(Opc)
                    ? SMFMA32x32WritesVGPROverlappedDMFMASrcCWaitStates
                    : SMFMA32x32WritesVGPROverlappedSMFMASrcCWaitStates;
            break;
          default:
            llvm_unreachable("unexpected number of passes");
          }
        }
      }
    } else {
      switch (Opc1) {
      case AMDGPU::V_MFMA_F64_16X16X4F64_e64:
      case AMDGPU::V_MFMA_F64_16X16X4F64_vgprcd_e64:
      case AMDGPU::V_MFMA_F64_16X16X4F64_mac_e64:
      case AMDGPU::V_MFMA_F64_16X16X4F64_mac_vgprcd_e64:
        NeedWaitStates = DMFMA16x16WritesVGPROverlappedMFMASrcABWaitStates;
        break;
      case AMDGPU::V_MFMA_F64_4X4X4F64_e64:
      case AMDGPU::V_MFMA_F64_4X4X4F64_vgprcd_e64:
        NeedWaitStates = DMFMA4x4WritesVGPROverlappedMFMASrcABWaitStates;
        break;
      default:
        int NumPasses = TSchedModel.computeInstrLatency(MI1);

        if (ST.hasGFX940Insts()) {
          NeedWaitStates =
              isXDL(ST, *MI1)
                  ? GFX940_XDL_N_PassWritesVGPROverlappedSrcABWaitStates(
                        NumPasses)
                  : GFX940_SMFMA_N_PassWritesVGPROverlappedSrcABWaitStates(
                        NumPasses);
          break;
        }

        switch (NumPasses) {
        case 2:
          NeedWaitStates = SMFMA4x4WritesVGPROverlappedSrcABWaitStates;
          break;
        case 4:
          llvm_unreachable("unexpected number of passes for mfma");
        case 8:
          NeedWaitStates = SMFMA16x16WritesVGPROverlappedSrcABWaitStates;
          break;
        case 16:
        default:
          NeedWaitStates = SMFMA32x32WritesVGPROverlappedSrcABWaitStates;
        }
      }
    }
    if (WaitStatesNeeded >= NeedWaitStates)
      continue;

    WaitStatesNeededForUse = NeedWaitStates - NumWaitStates;
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    if (WaitStatesNeeded == MaxWaitStates)
      break;
  }

  // Pad neighboring MFMA with noops for better inter-wave performance.
  WaitStatesNeeded = std::max(WaitStatesNeeded, checkMFMAPadding(MI));

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkMAILdStHazards(MachineInstr *MI) {
  // On gfx90a+ relevant hazards are checked in checkMAIVALUHazards()
  if (!ST.hasMAIInsts() || ST.hasGFX90AInsts())
    return 0;

  int WaitStatesNeeded = 0;

  auto IsAccVgprReadFn = [](const MachineInstr &MI) {
    return MI.getOpcode() == AMDGPU::V_ACCVGPR_READ_B32_e64;
  };

  for (const MachineOperand &Op : MI->explicit_uses()) {
    if (!Op.isReg() || !TRI.isVGPR(MF.getRegInfo(), Op.getReg()))
      continue;

    Register Reg = Op.getReg();

    const int AccVgprReadLdStWaitStates = 2;
    const int VALUWriteAccVgprRdWrLdStDepVALUWaitStates = 1;
    const int MaxWaitStates = 2;

    int WaitStatesNeededForUse = AccVgprReadLdStWaitStates -
      getWaitStatesSinceDef(Reg, IsAccVgprReadFn, MaxWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    if (WaitStatesNeeded == MaxWaitStates)
      return WaitStatesNeeded; // Early exit.

    auto IsVALUAccVgprRdWrCheckFn = [Reg, this](const MachineInstr &MI) {
      if (MI.getOpcode() != AMDGPU::V_ACCVGPR_READ_B32_e64 &&
          MI.getOpcode() != AMDGPU::V_ACCVGPR_WRITE_B32_e64)
        return false;
      auto IsVALUFn = [](const MachineInstr &MI) {
        return SIInstrInfo::isVALU(MI) && !SIInstrInfo::isMAI(MI);
      };
      return getWaitStatesSinceDef(Reg, IsVALUFn, 2 /*MaxWaitStates*/) <
             std::numeric_limits<int>::max();
    };

    WaitStatesNeededForUse = VALUWriteAccVgprRdWrLdStDepVALUWaitStates -
      getWaitStatesSince(IsVALUAccVgprRdWrCheckFn, MaxWaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  return WaitStatesNeeded;
}

static int GFX940_SMFMA_N_PassWriteVgprVALUWawWaitStates(int NumPasses) {
  // 2 pass -> 4
  // 4 pass -> 6
  // 8 pass -> 10
  // 16 pass -> 18
  return NumPasses + 2;
}

static int GFX940_XDL_N_PassWriteVgprVALUWawWaitStates(int NumPasses) {
  // 2 pass -> 5
  // 4 pass -> 7
  // 8 pass -> 11
  // 16 pass -> 19
  return NumPasses + 3;
}

static int GFX940_XDL_N_PassWriteVgprVALUMemExpReadWaitStates(int NumPasses) {
  // 2 pass -> 5
  // 4 pass -> 7
  // 8 pass -> 11
  // 16 pass -> 19
  return NumPasses + 3;
}

static int GFX940_SMFMA_N_PassWriteVgprVALUMemExpReadWaitStates(int NumPasses) {
  // 2 pass -> 4
  // 4 pass -> 6
  // 8 pass -> 10
  // 16 pass -> 18
  return NumPasses + 2;
}

int GCNHazardRecognizer::checkMAIVALUHazards(MachineInstr *MI) {
  if (!ST.hasGFX90AInsts())
    return 0;

  auto IsDGEMMFn = [](const MachineInstr &MI) -> bool {
    return isDGEMM(MI.getOpcode());
  };

  // This is checked in checkMAIHazards90A()
  if (SIInstrInfo::isMFMA(*MI))
    return 0;

  const MachineRegisterInfo &MRI = MF.getRegInfo();

  int WaitStatesNeeded = 0;

  bool IsMem = SIInstrInfo::isVMEM(*MI) ||
               SIInstrInfo::isFLAT(*MI) ||
               SIInstrInfo::isDS(*MI);
  bool IsMemOrExport = IsMem || SIInstrInfo::isEXP(*MI);
  bool IsVALU = SIInstrInfo::isVALU(*MI);

  const MachineInstr *MFMA = nullptr;
  unsigned Reg;
  auto IsMFMAWriteFn = [&Reg, &MFMA, this](const MachineInstr &MI) {
    if (!SIInstrInfo::isMFMA(MI) ||
        !TRI.regsOverlap(MI.getOperand(0).getReg(), Reg))
      return false;
    MFMA = &MI;
    return true;
  };

  const MachineInstr *DOT = nullptr;
  auto IsDotWriteFn = [&Reg, &DOT, this](const MachineInstr &MI) {
    if (!SIInstrInfo::isDOT(MI) ||
        !TRI.regsOverlap(MI.getOperand(0).getReg(), Reg))
      return false;
    DOT = &MI;
    return true;
  };

  bool DGEMMAfterVALUWrite = false;
  auto IsDGEMMHazard = [&DGEMMAfterVALUWrite, this](const MachineInstr &MI) {
    // Found DGEMM on reverse traversal to def.
    if (isDGEMM(MI.getOpcode()))
      DGEMMAfterVALUWrite = true;

    // Only hazard if register is defined by a VALU and a DGEMM is found after
    // after the def.
    if (!TII.isVALU(MI) || !DGEMMAfterVALUWrite)
      return false;

    return true;
  };

  int SrcCIdx = AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                           AMDGPU::OpName::src2);

  if (IsMemOrExport || IsVALU) {
    const int SMFMA4x4WriteVgprVALUMemExpReadWaitStates = 5;
    const int SMFMA16x16WriteVgprVALUMemExpReadWaitStates = 11;
    const int SMFMA32x32WriteVgprVALUMemExpReadWaitStates = 19;
    const int DMFMA4x4WriteVgprMemExpReadWaitStates = 9;
    const int DMFMA16x16WriteVgprMemExpReadWaitStates = 18;
    const int DMFMA4x4WriteVgprVALUReadWaitStates = 6;
    const int DMFMA16x16WriteVgprVALUReadWaitStates = 11;
    const int DotWriteSameDotReadSrcAB = 3;
    const int DotWriteDifferentVALURead = 3;
    const int DMFMABetweenVALUWriteVMEMRead = 2;
    const int MaxWaitStates = 19;

    for (const MachineOperand &Use : MI->explicit_uses()) {
      if (!Use.isReg())
        continue;
      Reg = Use.getReg();

      DOT = nullptr;
      int WaitStatesSinceDef = getWaitStatesSinceDef(Reg, IsDotWriteFn,
                                                     MaxWaitStates);
      if (DOT) {
        int NeedWaitStates = 0;
        if (DOT->getOpcode() == MI->getOpcode()) {
          if (&Use - &MI->getOperand(0) != SrcCIdx)
            NeedWaitStates = DotWriteSameDotReadSrcAB;
        } else {
          NeedWaitStates = DotWriteDifferentVALURead;
        }

        int WaitStatesNeededForUse = NeedWaitStates - WaitStatesSinceDef;
        WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
      }

      // Workaround for HW data hazard bug observed only in GFX90A. When there
      // is a DGEMM instruction in-between a VALU and a VMEM instruction it
      // causes the SQ to incorrectly not insert two wait states between the two
      // instructions needed to avoid data hazard.
      if (IsMem && ST.hasGFX90AInsts() && !ST.hasGFX940Insts()) {
        DGEMMAfterVALUWrite = false;
        if (TRI.isVectorRegister(MRI, Reg)) {
          int WaitStatesNeededForUse =
                DMFMABetweenVALUWriteVMEMRead -
                getWaitStatesSinceDef(Reg, IsDGEMMHazard,
                                      DMFMABetweenVALUWriteVMEMRead);

          WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
        }
      }

      MFMA = nullptr;
      WaitStatesSinceDef =
          getWaitStatesSinceDef(Reg, IsMFMAWriteFn, MaxWaitStates);
      if (!MFMA)
        continue;

      unsigned HazardDefLatency = TSchedModel.computeInstrLatency(MFMA);
      int NumPasses = HazardDefLatency;
      int NeedWaitStates = MaxWaitStates;

      if (isDGEMM(MFMA->getOpcode())) {
        switch (HazardDefLatency) {
        case 4:
          NeedWaitStates = IsMemOrExport ? DMFMA4x4WriteVgprMemExpReadWaitStates
                                         : DMFMA4x4WriteVgprVALUReadWaitStates;
          break;
        case 8:
        case 16:
          NeedWaitStates = IsMemOrExport
                               ? DMFMA16x16WriteVgprMemExpReadWaitStates
                               : DMFMA16x16WriteVgprVALUReadWaitStates;
          break;
        default:
          llvm_unreachable("unexpected dgemm");
        }
      } else if (ST.hasGFX940Insts()) {
        NeedWaitStates =
            isXDL(ST, *MFMA)
                ? GFX940_XDL_N_PassWriteVgprVALUMemExpReadWaitStates(NumPasses)
                : GFX940_SMFMA_N_PassWriteVgprVALUMemExpReadWaitStates(
                      NumPasses);
      } else {
        switch (HazardDefLatency) {
        case 2:
          NeedWaitStates = SMFMA4x4WriteVgprVALUMemExpReadWaitStates;
          break;
        case 8:
          NeedWaitStates = SMFMA16x16WriteVgprVALUMemExpReadWaitStates;
          break;
        case 16:
          NeedWaitStates = SMFMA32x32WriteVgprVALUMemExpReadWaitStates;
          break;
        default:
          llvm_unreachable("unexpected number of passes for mfma");
        }
      }

      int WaitStatesNeededForUse = NeedWaitStates - WaitStatesSinceDef;
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

      if (WaitStatesNeeded == MaxWaitStates)
        break;
    }
  }

  unsigned Opc = MI->getOpcode();
  const int DMFMAToFMA64WaitStates = 2;
  if ((Opc == AMDGPU::V_FMA_F64_e64 ||
       Opc == AMDGPU::V_FMAC_F64_e32 || Opc == AMDGPU::V_FMAC_F64_e64 ||
       Opc == AMDGPU::V_FMAC_F64_dpp) &&
      WaitStatesNeeded < DMFMAToFMA64WaitStates) {
    int WaitStatesNeededForUse = DMFMAToFMA64WaitStates -
      getWaitStatesSince(IsDGEMMFn, DMFMAToFMA64WaitStates);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  if (!IsVALU && !IsMemOrExport)
    return WaitStatesNeeded;

  for (const MachineOperand &Def : MI->defs()) {
    const int SMFMA4x4WriteVgprVALUWawWaitStates = 5;
    const int SMFMA16x16WriteVgprVALUWawWaitStates = 11;
    const int SMFMA32x32WriteVgprVALUWawWaitStates = 19;
    const int SMFMA4x4ReadVgprVALUWarWaitStates = 1;
    const int GFX940_XDL4PassReadVgprVALUWarWaitStates = 3;
    const int SMFMA16x16ReadVgprVALUWarWaitStates = 7;
    const int SMFMA32x32ReadVgprVALUWarWaitStates = 15;
    const int DMFMA4x4WriteVgprVALUWriteWaitStates = 6;
    const int DMFMA16x16WriteVgprVALUWriteWaitStates = 11;
    const int DotWriteDifferentVALUWrite = 3;
    const int MaxWaitStates = 19;
    const int MaxWarWaitStates = 15;

    Reg = Def.getReg();

    DOT = nullptr;
    int WaitStatesSinceDef = getWaitStatesSinceDef(Reg, IsDotWriteFn,
                                                   MaxWaitStates);
    if (DOT && DOT->getOpcode() != MI->getOpcode())
      WaitStatesNeeded = std::max(WaitStatesNeeded, DotWriteDifferentVALUWrite -
                                                    WaitStatesSinceDef);

    MFMA = nullptr;
    WaitStatesSinceDef =
        getWaitStatesSinceDef(Reg, IsMFMAWriteFn, MaxWaitStates);
    if (MFMA) {
      int NeedWaitStates = MaxWaitStates;
      int NumPasses = TSchedModel.computeInstrLatency(MFMA);

      if (isDGEMM(MFMA->getOpcode())) {
        switch (NumPasses) {
        case 4:
          NeedWaitStates = DMFMA4x4WriteVgprVALUWriteWaitStates;
          break;
        case 8:
        case 16:
          NeedWaitStates = DMFMA16x16WriteVgprVALUWriteWaitStates;
          break;
        default:
          llvm_unreachable("unexpected number of cycles for dgemm");
        }
      } else if (ST.hasGFX940Insts()) {
        NeedWaitStates =
            isXDL(ST, *MFMA)
                ? GFX940_XDL_N_PassWriteVgprVALUWawWaitStates(NumPasses)
                : GFX940_SMFMA_N_PassWriteVgprVALUWawWaitStates(NumPasses);
      } else {
        switch (NumPasses) {
        case 2:
          NeedWaitStates = SMFMA4x4WriteVgprVALUWawWaitStates;
          break;
        case 8:
          NeedWaitStates = SMFMA16x16WriteVgprVALUWawWaitStates;
          break;
        case 16:
          NeedWaitStates = SMFMA32x32WriteVgprVALUWawWaitStates;
          break;
        default:
          llvm_unreachable("Unexpected number of passes for mfma");
        }
      }

      int WaitStatesNeededForUse = NeedWaitStates - WaitStatesSinceDef;
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

      if (WaitStatesNeeded == MaxWaitStates)
        break;
    }

    auto IsSMFMAReadAsCFn = [&Reg, &MFMA, this](const MachineInstr &MI) {
      if (!SIInstrInfo::isMFMA(MI) || isDGEMM(MI.getOpcode()) ||
          !MI.readsRegister(Reg, &TRI))
        return false;

      if (ST.hasGFX940Insts() && !isXDL(ST, MI))
        return false;

      const MachineOperand *SrcC =
          TII.getNamedOperand(MI, AMDGPU::OpName::src2);
      assert(SrcC);
      if (!SrcC->isReg() || !TRI.regsOverlap(SrcC->getReg(), Reg))
        return false;

      MFMA = &MI;
      return true;
    };

    MFMA = nullptr;
    int WaitStatesSinceUse = getWaitStatesSince(IsSMFMAReadAsCFn,
                                                MaxWarWaitStates);
    if (!MFMA)
      continue;

    unsigned HazardDefLatency = TSchedModel.computeInstrLatency(MFMA);
    int NeedWaitStates = MaxWaitStates;
    switch (HazardDefLatency) {
    case 2:  NeedWaitStates = SMFMA4x4ReadVgprVALUWarWaitStates;
             break;
    case 4:  assert(ST.hasGFX940Insts());
             NeedWaitStates = GFX940_XDL4PassReadVgprVALUWarWaitStates;
             break;
    case 8:  NeedWaitStates = SMFMA16x16ReadVgprVALUWarWaitStates;
             break;
    case 16: [[fallthrough]];
    default: NeedWaitStates = SMFMA32x32ReadVgprVALUWarWaitStates;
             break;
    }

    int WaitStatesNeededForUse = NeedWaitStates - WaitStatesSinceUse;
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  return WaitStatesNeeded;
}

bool GCNHazardRecognizer::ShouldPreferAnother(SUnit *SU) {
  if (!SU->isInstr())
    return false;

  const MachineInstr *MAI = nullptr;

  auto IsMFMAFn = [&MAI](const MachineInstr &MI) {
    MAI = nullptr;
    if (SIInstrInfo::isMFMA(MI))
      MAI = &MI;
    return MAI != nullptr;
  };

  MachineInstr *MI = SU->getInstr();
  if (IsMFMAFn(*MI)) {
    int W = getWaitStatesSince(IsMFMAFn, 16);
    if (MAI)
      return W < (int)TSchedModel.computeInstrLatency(MAI);
  }

  return false;
}

bool GCNHazardRecognizer::fixVALUMaskWriteHazard(MachineInstr *MI) {
  if (!ST.hasVALUMaskWriteHazard())
    return false;
  assert(!ST.hasExtendedWaitCounts());

  if (!ST.isWave64() || !SIInstrInfo::isSALU(*MI))
    return false;

  // The hazard sequence is three instructions:
  //   1. VALU reads SGPR as mask
  //   2. SALU writes SGPR
  //   3. SALU reads SGPR
  // The hazard can expire if the distance between 2 and 3 is sufficient.
  // In practice this happens <10% of the time, hence this always assumes
  // the hazard exists if 1 and 2 are present to avoid searching.

  const MachineOperand *SDSTOp = TII.getNamedOperand(*MI, AMDGPU::OpName::sdst);
  if (!SDSTOp || !SDSTOp->isReg())
    return false;

  const Register HazardReg = SDSTOp->getReg();
  if (HazardReg == AMDGPU::EXEC ||
      HazardReg == AMDGPU::EXEC_LO ||
      HazardReg == AMDGPU::EXEC_HI ||
      HazardReg == AMDGPU::M0)
    return false;

  auto IsHazardFn = [HazardReg, this](const MachineInstr &I) {
    switch (I.getOpcode()) {
    case AMDGPU::V_ADDC_U32_e32:
    case AMDGPU::V_ADDC_U32_dpp:
    case AMDGPU::V_CNDMASK_B16_e32:
    case AMDGPU::V_CNDMASK_B16_dpp:
    case AMDGPU::V_CNDMASK_B32_e32:
    case AMDGPU::V_CNDMASK_B32_dpp:
    case AMDGPU::V_DIV_FMAS_F32_e64:
    case AMDGPU::V_DIV_FMAS_F64_e64:
    case AMDGPU::V_SUBB_U32_e32:
    case AMDGPU::V_SUBB_U32_dpp:
    case AMDGPU::V_SUBBREV_U32_e32:
    case AMDGPU::V_SUBBREV_U32_dpp:
      // These implicitly read VCC as mask source.
      return HazardReg == AMDGPU::VCC ||
             HazardReg == AMDGPU::VCC_LO ||
             HazardReg == AMDGPU::VCC_HI;
    case AMDGPU::V_ADDC_U32_e64:
    case AMDGPU::V_ADDC_U32_e64_dpp:
    case AMDGPU::V_CNDMASK_B16_e64:
    case AMDGPU::V_CNDMASK_B16_e64_dpp:
    case AMDGPU::V_CNDMASK_B32_e64:
    case AMDGPU::V_CNDMASK_B32_e64_dpp:
    case AMDGPU::V_SUBB_U32_e64:
    case AMDGPU::V_SUBB_U32_e64_dpp:
    case AMDGPU::V_SUBBREV_U32_e64:
    case AMDGPU::V_SUBBREV_U32_e64_dpp: {
      // Only check mask register overlaps.
      const MachineOperand *SSRCOp = TII.getNamedOperand(I, AMDGPU::OpName::src2);
      assert(SSRCOp);
      return TRI.regsOverlap(SSRCOp->getReg(), HazardReg);
    }
    default:
      return false;
    }
  };

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  auto IsExpiredFn = [&MRI, this](const MachineInstr &I, int) {
    // s_waitcnt_depctr sa_sdst(0) mitigates hazard.
    if (I.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
        AMDGPU::DepCtr::decodeFieldSaSdst(I.getOperand(0).getImm()) == 0)
      return true;

    // VALU access to any SGPR or literal constant other than HazardReg
    // mitigates hazard. No need to check HazardReg here as this will
    // only be called when !IsHazardFn.
    if (!SIInstrInfo::isVALU(I))
      return false;
    for (int OpNo = 0, End = I.getNumOperands(); OpNo < End; ++OpNo) {
      const MachineOperand &Op = I.getOperand(OpNo);
      if (Op.isReg()) {
        Register OpReg = Op.getReg();
        // Only consider uses
        if (!Op.isUse())
          continue;
        // Ignore EXEC
        if (OpReg == AMDGPU::EXEC ||
            OpReg == AMDGPU::EXEC_LO ||
            OpReg == AMDGPU::EXEC_HI)
          continue;
        // Ignore all implicit uses except VCC
        if (Op.isImplicit()) {
          if (OpReg == AMDGPU::VCC ||
              OpReg == AMDGPU::VCC_LO ||
              OpReg == AMDGPU::VCC_HI)
            return true;
          continue;
        }
        if (TRI.isSGPRReg(MRI, OpReg))
          return true;
      } else {
        const MCInstrDesc &InstDesc = I.getDesc();
        const MCOperandInfo &OpInfo = InstDesc.operands()[OpNo];
        if (!TII.isInlineConstant(Op, OpInfo))
          return true;
      }
    }
    return false;
  };

  // Check for hazard
  if (::getWaitStatesSince(IsHazardFn, MI, IsExpiredFn) ==
      std::numeric_limits<int>::max())
    return false;

  auto NextMI = std::next(MI->getIterator());

  // Add s_waitcnt_depctr sa_sdst(0) after SALU write.
  BuildMI(*MI->getParent(), NextMI, MI->getDebugLoc(),
          TII.get(AMDGPU::S_WAITCNT_DEPCTR))
      .addImm(AMDGPU::DepCtr::encodeFieldSaSdst(0));

  // SALU write may be s_getpc in a bundle.
  if (MI->getOpcode() == AMDGPU::S_GETPC_B64) {
    // Update offsets of any references in the bundle.
    while (NextMI != MI->getParent()->end() &&
           NextMI->isBundledWithPred()) {
      for (auto &Operand : NextMI->operands()) {
        if (Operand.isGlobal())
          Operand.setOffset(Operand.getOffset() + 4);
      }
      NextMI++;
    }
  }

  return true;
}

static bool ensureEntrySetPrio(MachineFunction *MF, int Priority,
                               const SIInstrInfo &TII) {
  MachineBasicBlock &EntryMBB = MF->front();
  if (EntryMBB.begin() != EntryMBB.end()) {
    auto &EntryMI = *EntryMBB.begin();
    if (EntryMI.getOpcode() == AMDGPU::S_SETPRIO &&
        EntryMI.getOperand(0).getImm() >= Priority)
      return false;
  }

  BuildMI(EntryMBB, EntryMBB.begin(), DebugLoc(), TII.get(AMDGPU::S_SETPRIO))
      .addImm(Priority);
  return true;
}

bool GCNHazardRecognizer::fixRequiredExportPriority(MachineInstr *MI) {
  if (!ST.hasRequiredExportPriority())
    return false;

  // Assume the following shader types will never have exports,
  // and avoid adding or adjusting S_SETPRIO.
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction *MF = MBB->getParent();
  auto CC = MF->getFunction().getCallingConv();
  switch (CC) {
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
  case CallingConv::AMDGPU_KERNEL:
    return false;
  default:
    break;
  }

  const int MaxPriority = 3;
  const int NormalPriority = 2;
  const int PostExportPriority = 0;

  auto It = MI->getIterator();
  switch (MI->getOpcode()) {
  case AMDGPU::S_ENDPGM:
  case AMDGPU::S_ENDPGM_SAVED:
  case AMDGPU::S_ENDPGM_ORDERED_PS_DONE:
  case AMDGPU::SI_RETURN_TO_EPILOG:
    // Ensure shader with calls raises priority at entry.
    // This ensures correct priority if exports exist in callee.
    if (MF->getFrameInfo().hasCalls())
      return ensureEntrySetPrio(MF, NormalPriority, TII);
    return false;
  case AMDGPU::S_SETPRIO: {
    // Raise minimum priority unless in workaround.
    auto &PrioOp = MI->getOperand(0);
    int Prio = PrioOp.getImm();
    bool InWA = (Prio == PostExportPriority) &&
                (It != MBB->begin() && TII.isEXP(*std::prev(It)));
    if (InWA || Prio >= NormalPriority)
      return false;
    PrioOp.setImm(std::min(Prio + NormalPriority, MaxPriority));
    return true;
  }
  default:
    if (!TII.isEXP(*MI))
      return false;
    break;
  }

  // Check entry priority at each export (as there will only be a few).
  // Note: amdgpu_gfx can only be a callee, so defer to caller setprio.
  bool Changed = false;
  if (CC != CallingConv::AMDGPU_Gfx)
    Changed = ensureEntrySetPrio(MF, NormalPriority, TII);

  auto NextMI = std::next(It);
  bool EndOfShader = false;
  if (NextMI != MBB->end()) {
    // Only need WA at end of sequence of exports.
    if (TII.isEXP(*NextMI))
      return Changed;
    // Assume appropriate S_SETPRIO after export means WA already applied.
    if (NextMI->getOpcode() == AMDGPU::S_SETPRIO &&
        NextMI->getOperand(0).getImm() == PostExportPriority)
      return Changed;
    EndOfShader = NextMI->getOpcode() == AMDGPU::S_ENDPGM;
  }

  const DebugLoc &DL = MI->getDebugLoc();

  // Lower priority.
  BuildMI(*MBB, NextMI, DL, TII.get(AMDGPU::S_SETPRIO))
      .addImm(PostExportPriority);

  if (!EndOfShader) {
    // Wait for exports to complete.
    BuildMI(*MBB, NextMI, DL, TII.get(AMDGPU::S_WAITCNT_EXPCNT))
        .addReg(AMDGPU::SGPR_NULL)
        .addImm(0);
  }

  BuildMI(*MBB, NextMI, DL, TII.get(AMDGPU::S_NOP)).addImm(0);
  BuildMI(*MBB, NextMI, DL, TII.get(AMDGPU::S_NOP)).addImm(0);

  if (!EndOfShader) {
    // Return to normal (higher) priority.
    BuildMI(*MBB, NextMI, DL, TII.get(AMDGPU::S_SETPRIO))
        .addImm(NormalPriority);
  }

  return true;
}
