//===-- SIWholeQuadMode.cpp - enter and suspend whole quad mode -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass adds instructions to enable whole quad mode (strict or non-strict)
/// for pixel shaders, and strict whole wavefront mode for all programs.
///
/// The "strict" prefix indicates that inactive lanes do not take part in
/// control flow, specifically an inactive lane enabled by a strict WQM/WWM will
/// always be enabled irrespective of control flow decisions. Conversely in
/// non-strict WQM inactive lanes may control flow decisions.
///
/// Whole quad mode is required for derivative computations, but it interferes
/// with shader side effects (stores and atomics). It ensures that WQM is
/// enabled when necessary, but disabled around stores and atomics.
///
/// When necessary, this pass creates a function prolog
///
///   S_MOV_B64 LiveMask, EXEC
///   S_WQM_B64 EXEC, EXEC
///
/// to enter WQM at the top of the function and surrounds blocks of Exact
/// instructions by
///
///   S_AND_SAVEEXEC_B64 Tmp, LiveMask
///   ...
///   S_MOV_B64 EXEC, Tmp
///
/// We also compute when a sequence of instructions requires strict whole
/// wavefront mode (StrictWWM) and insert instructions to save and restore it:
///
///   S_OR_SAVEEXEC_B64 Tmp, -1
///   ...
///   S_MOV_B64 EXEC, Tmp
///
/// When a sequence of instructions requires strict whole quad mode (StrictWQM)
/// we use a similar save and restore mechanism and force whole quad mode for
/// those instructions:
///
///  S_MOV_B64 Tmp, EXEC
///  S_WQM_B64 EXEC, EXEC
///  ...
///  S_MOV_B64 EXEC, Tmp
///
/// In order to avoid excessive switching during sequences of Exact
/// instructions, the pass first analyzes which instructions must be run in WQM
/// (aka which instructions produce values that lead to derivative
/// computations).
///
/// Basic blocks are always exited in WQM as long as some successor needs WQM.
///
/// There is room for improvement given better control flow analysis:
///
///  (1) at the top level (outside of control flow statements, and as long as
///      kill hasn't been used), one SGPR can be saved by recovering WQM from
///      the LiveMask (this is implemented for the entry block).
///
///  (2) when entire regions (e.g. if-else blocks or entire loops) only
///      consist of exact and don't-care instructions, the switch only has to
///      be done at the entry and exit points rather than potentially in each
///      block of the region.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "si-wqm"

namespace {

enum {
  StateWQM = 0x1,
  StateStrictWWM = 0x2,
  StateStrictWQM = 0x4,
  StateExact = 0x8,
  StateStrict = StateStrictWWM | StateStrictWQM,
};

struct PrintState {
public:
  int State;

  explicit PrintState(int State) : State(State) {}
};

#ifndef NDEBUG
static raw_ostream &operator<<(raw_ostream &OS, const PrintState &PS) {

  static const std::pair<char, const char *> Mapping[] = {
      std::pair(StateWQM, "WQM"), std::pair(StateStrictWWM, "StrictWWM"),
      std::pair(StateStrictWQM, "StrictWQM"), std::pair(StateExact, "Exact")};
  char State = PS.State;
  for (auto M : Mapping) {
    if (State & M.first) {
      OS << M.second;
      State &= ~M.first;

      if (State)
        OS << '|';
    }
  }
  assert(State == 0);
  return OS;
}
#endif

struct InstrInfo {
  char Needs = 0;
  char Disabled = 0;
  char OutNeeds = 0;
};

struct BlockInfo {
  char Needs = 0;
  char InNeeds = 0;
  char OutNeeds = 0;
  char InitialState = 0;
  bool NeedsLowering = false;
};

struct WorkItem {
  MachineBasicBlock *MBB = nullptr;
  MachineInstr *MI = nullptr;

  WorkItem() = default;
  WorkItem(MachineBasicBlock *MBB) : MBB(MBB) {}
  WorkItem(MachineInstr *MI) : MI(MI) {}
};

class SIWholeQuadMode : public MachineFunctionPass {
private:
  const SIInstrInfo *TII;
  const SIRegisterInfo *TRI;
  const GCNSubtarget *ST;
  MachineRegisterInfo *MRI;
  LiveIntervals *LIS;
  MachineDominatorTree *MDT;
  MachinePostDominatorTree *PDT;

  unsigned AndOpc;
  unsigned AndTermOpc;
  unsigned AndN2Opc;
  unsigned XorOpc;
  unsigned AndSaveExecOpc;
  unsigned AndSaveExecTermOpc;
  unsigned WQMOpc;
  Register Exec;
  Register LiveMaskReg;

  DenseMap<const MachineInstr *, InstrInfo> Instructions;
  MapVector<MachineBasicBlock *, BlockInfo> Blocks;

  // Tracks state (WQM/StrictWWM/StrictWQM/Exact) after a given instruction
  DenseMap<const MachineInstr *, char> StateTransition;

  SmallVector<MachineInstr *, 2> LiveMaskQueries;
  SmallVector<MachineInstr *, 4> LowerToMovInstrs;
  SmallVector<MachineInstr *, 4> LowerToCopyInstrs;
  SmallVector<MachineInstr *, 4> KillInstrs;
  SmallVector<MachineInstr *, 4> InitExecInstrs;

  void printInfo();

  void markInstruction(MachineInstr &MI, char Flag,
                       std::vector<WorkItem> &Worklist);
  void markDefs(const MachineInstr &UseMI, LiveRange &LR, Register Reg,
                unsigned SubReg, char Flag, std::vector<WorkItem> &Worklist);
  void markOperand(const MachineInstr &MI, const MachineOperand &Op, char Flag,
                   std::vector<WorkItem> &Worklist);
  void markInstructionUses(const MachineInstr &MI, char Flag,
                           std::vector<WorkItem> &Worklist);
  char scanInstructions(MachineFunction &MF, std::vector<WorkItem> &Worklist);
  void propagateInstruction(MachineInstr &MI, std::vector<WorkItem> &Worklist);
  void propagateBlock(MachineBasicBlock &MBB, std::vector<WorkItem> &Worklist);
  char analyzeFunction(MachineFunction &MF);

  MachineBasicBlock::iterator saveSCC(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator Before);
  MachineBasicBlock::iterator
  prepareInsertion(MachineBasicBlock &MBB, MachineBasicBlock::iterator First,
                   MachineBasicBlock::iterator Last, bool PreferLast,
                   bool SaveSCC);
  void toExact(MachineBasicBlock &MBB, MachineBasicBlock::iterator Before,
               Register SaveWQM);
  void toWQM(MachineBasicBlock &MBB, MachineBasicBlock::iterator Before,
             Register SavedWQM);
  void toStrictMode(MachineBasicBlock &MBB, MachineBasicBlock::iterator Before,
                    Register SaveOrig, char StrictStateNeeded);
  void fromStrictMode(MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator Before, Register SavedOrig,
                      char NonStrictState, char CurrentStrictState);

  MachineBasicBlock *splitBlock(MachineBasicBlock *BB, MachineInstr *TermMI);

  MachineInstr *lowerKillI1(MachineBasicBlock &MBB, MachineInstr &MI,
                            bool IsWQM);
  MachineInstr *lowerKillF32(MachineBasicBlock &MBB, MachineInstr &MI);

  void lowerBlock(MachineBasicBlock &MBB);
  void processBlock(MachineBasicBlock &MBB, bool IsEntry);

  bool lowerLiveMaskQueries();
  bool lowerCopyInstrs();
  bool lowerKillInstrs(bool IsWQM);
  void lowerInitExec(MachineInstr &MI);
  MachineBasicBlock::iterator lowerInitExecInstrs(MachineBasicBlock &Entry,
                                                  bool &Changed);

public:
  static char ID;

  SIWholeQuadMode() :
    MachineFunctionPass(ID) { }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Whole Quad Mode"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }
};

} // end anonymous namespace

char SIWholeQuadMode::ID = 0;

INITIALIZE_PASS_BEGIN(SIWholeQuadMode, DEBUG_TYPE, "SI Whole Quad Mode", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(SIWholeQuadMode, DEBUG_TYPE, "SI Whole Quad Mode", false,
                    false)

char &llvm::SIWholeQuadModeID = SIWholeQuadMode::ID;

FunctionPass *llvm::createSIWholeQuadModePass() {
  return new SIWholeQuadMode;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void SIWholeQuadMode::printInfo() {
  for (const auto &BII : Blocks) {
    dbgs() << "\n"
           << printMBBReference(*BII.first) << ":\n"
           << "  InNeeds = " << PrintState(BII.second.InNeeds)
           << ", Needs = " << PrintState(BII.second.Needs)
           << ", OutNeeds = " << PrintState(BII.second.OutNeeds) << "\n\n";

    for (const MachineInstr &MI : *BII.first) {
      auto III = Instructions.find(&MI);
      if (III != Instructions.end()) {
        dbgs() << "  " << MI << "    Needs = " << PrintState(III->second.Needs)
               << ", OutNeeds = " << PrintState(III->second.OutNeeds) << '\n';
      }
    }
  }
}
#endif

void SIWholeQuadMode::markInstruction(MachineInstr &MI, char Flag,
                                      std::vector<WorkItem> &Worklist) {
  InstrInfo &II = Instructions[&MI];

  assert(!(Flag & StateExact) && Flag != 0);

  // Remove any disabled states from the flag. The user that required it gets
  // an undefined value in the helper lanes. For example, this can happen if
  // the result of an atomic is used by instruction that requires WQM, where
  // ignoring the request for WQM is correct as per the relevant specs.
  Flag &= ~II.Disabled;

  // Ignore if the flag is already encompassed by the existing needs, or we
  // just disabled everything.
  if ((II.Needs & Flag) == Flag)
    return;

  LLVM_DEBUG(dbgs() << "markInstruction " << PrintState(Flag) << ": " << MI);
  II.Needs |= Flag;
  Worklist.emplace_back(&MI);
}

/// Mark all relevant definitions of register \p Reg in usage \p UseMI.
void SIWholeQuadMode::markDefs(const MachineInstr &UseMI, LiveRange &LR,
                               Register Reg, unsigned SubReg, char Flag,
                               std::vector<WorkItem> &Worklist) {
  LLVM_DEBUG(dbgs() << "markDefs " << PrintState(Flag) << ": " << UseMI);

  LiveQueryResult UseLRQ = LR.Query(LIS->getInstructionIndex(UseMI));
  const VNInfo *Value = UseLRQ.valueIn();
  if (!Value)
    return;

  // Note: this code assumes that lane masks on AMDGPU completely
  // cover registers.
  const LaneBitmask UseLanes =
      SubReg ? TRI->getSubRegIndexLaneMask(SubReg)
             : (Reg.isVirtual() ? MRI->getMaxLaneMaskForVReg(Reg)
                                : LaneBitmask::getNone());

  // Perform a depth-first iteration of the LiveRange graph marking defs.
  // Stop processing of a given branch when all use lanes have been defined.
  // The first definition stops processing for a physical register.
  struct PhiEntry {
    const VNInfo *Phi;
    unsigned PredIdx;
    LaneBitmask DefinedLanes;

    PhiEntry(const VNInfo *Phi, unsigned PredIdx, LaneBitmask DefinedLanes)
        : Phi(Phi), PredIdx(PredIdx), DefinedLanes(DefinedLanes) {}
  };
  using VisitKey = std::pair<const VNInfo *, LaneBitmask>;
  SmallVector<PhiEntry, 2> PhiStack;
  SmallSet<VisitKey, 4> Visited;
  LaneBitmask DefinedLanes;
  unsigned NextPredIdx = 0; // Only used for processing phi nodes
  do {
    const VNInfo *NextValue = nullptr;
    const VisitKey Key(Value, DefinedLanes);

    if (Visited.insert(Key).second) {
      // On first visit to a phi then start processing first predecessor
      NextPredIdx = 0;
    }

    if (Value->isPHIDef()) {
      // Each predecessor node in the phi must be processed as a subgraph
      const MachineBasicBlock *MBB = LIS->getMBBFromIndex(Value->def);
      assert(MBB && "Phi-def has no defining MBB");

      // Find next predecessor to process
      unsigned Idx = NextPredIdx;
      auto PI = MBB->pred_begin() + Idx;
      auto PE = MBB->pred_end();
      for (; PI != PE && !NextValue; ++PI, ++Idx) {
        if (const VNInfo *VN = LR.getVNInfoBefore(LIS->getMBBEndIdx(*PI))) {
          if (!Visited.count(VisitKey(VN, DefinedLanes)))
            NextValue = VN;
        }
      }

      // If there are more predecessors to process; add phi to stack
      if (PI != PE)
        PhiStack.emplace_back(Value, Idx, DefinedLanes);
    } else {
      MachineInstr *MI = LIS->getInstructionFromIndex(Value->def);
      assert(MI && "Def has no defining instruction");

      if (Reg.isVirtual()) {
        // Iterate over all operands to find relevant definitions
        bool HasDef = false;
        for (const MachineOperand &Op : MI->all_defs()) {
          if (Op.getReg() != Reg)
            continue;

          // Compute lanes defined and overlap with use
          LaneBitmask OpLanes =
              Op.isUndef() ? LaneBitmask::getAll()
                           : TRI->getSubRegIndexLaneMask(Op.getSubReg());
          LaneBitmask Overlap = (UseLanes & OpLanes);

          // Record if this instruction defined any of use
          HasDef |= Overlap.any();

          // Mark any lanes defined
          DefinedLanes |= OpLanes;
        }

        // Check if all lanes of use have been defined
        if ((DefinedLanes & UseLanes) != UseLanes) {
          // Definition not complete; need to process input value
          LiveQueryResult LRQ = LR.Query(LIS->getInstructionIndex(*MI));
          if (const VNInfo *VN = LRQ.valueIn()) {
            if (!Visited.count(VisitKey(VN, DefinedLanes)))
              NextValue = VN;
          }
        }

        // Only mark the instruction if it defines some part of the use
        if (HasDef)
          markInstruction(*MI, Flag, Worklist);
      } else {
        // For physical registers simply mark the defining instruction
        markInstruction(*MI, Flag, Worklist);
      }
    }

    if (!NextValue && !PhiStack.empty()) {
      // Reach end of chain; revert to processing last phi
      PhiEntry &Entry = PhiStack.back();
      NextValue = Entry.Phi;
      NextPredIdx = Entry.PredIdx;
      DefinedLanes = Entry.DefinedLanes;
      PhiStack.pop_back();
    }

    Value = NextValue;
  } while (Value);
}

void SIWholeQuadMode::markOperand(const MachineInstr &MI,
                                  const MachineOperand &Op, char Flag,
                                  std::vector<WorkItem> &Worklist) {
  assert(Op.isReg());
  Register Reg = Op.getReg();

  // Ignore some hardware registers
  switch (Reg) {
  case AMDGPU::EXEC:
  case AMDGPU::EXEC_LO:
    return;
  default:
    break;
  }

  LLVM_DEBUG(dbgs() << "markOperand " << PrintState(Flag) << ": " << Op
                    << " for " << MI);
  if (Reg.isVirtual()) {
    LiveRange &LR = LIS->getInterval(Reg);
    markDefs(MI, LR, Reg, Op.getSubReg(), Flag, Worklist);
  } else {
    // Handle physical registers that we need to track; this is mostly relevant
    // for VCC, which can appear as the (implicit) input of a uniform branch,
    // e.g. when a loop counter is stored in a VGPR.
    for (MCRegUnit Unit : TRI->regunits(Reg.asMCReg())) {
      LiveRange &LR = LIS->getRegUnit(Unit);
      const VNInfo *Value = LR.Query(LIS->getInstructionIndex(MI)).valueIn();
      if (Value)
        markDefs(MI, LR, Unit, AMDGPU::NoSubRegister, Flag, Worklist);
    }
  }
}

/// Mark all instructions defining the uses in \p MI with \p Flag.
void SIWholeQuadMode::markInstructionUses(const MachineInstr &MI, char Flag,
                                          std::vector<WorkItem> &Worklist) {
  LLVM_DEBUG(dbgs() << "markInstructionUses " << PrintState(Flag) << ": "
                    << MI);

  for (const MachineOperand &Use : MI.all_uses())
    markOperand(MI, Use, Flag, Worklist);
}

// Scan instructions to determine which ones require an Exact execmask and
// which ones seed WQM requirements.
char SIWholeQuadMode::scanInstructions(MachineFunction &MF,
                                       std::vector<WorkItem> &Worklist) {
  char GlobalFlags = 0;
  bool WQMOutputs = MF.getFunction().hasFnAttribute("amdgpu-ps-wqm-outputs");
  SmallVector<MachineInstr *, 4> SetInactiveInstrs;
  SmallVector<MachineInstr *, 4> SoftWQMInstrs;
  bool HasImplicitDerivatives =
      MF.getFunction().getCallingConv() == CallingConv::AMDGPU_PS;

  // We need to visit the basic blocks in reverse post-order so that we visit
  // defs before uses, in particular so that we don't accidentally mark an
  // instruction as needing e.g. WQM before visiting it and realizing it needs
  // WQM disabled.
  ReversePostOrderTraversal<MachineFunction *> RPOT(&MF);
  for (MachineBasicBlock *MBB : RPOT) {
    BlockInfo &BBI = Blocks[MBB];

    for (MachineInstr &MI : *MBB) {
      InstrInfo &III = Instructions[&MI];
      unsigned Opcode = MI.getOpcode();
      char Flags = 0;

      if (TII->isWQM(Opcode)) {
        // If LOD is not supported WQM is not needed.
        // Only generate implicit WQM if implicit derivatives are required.
        // This avoids inserting unintended WQM if a shader type without
        // implicit derivatives uses an image sampling instruction.
        if (ST->hasExtendedImageInsts() && HasImplicitDerivatives) {
          // Sampling instructions don't need to produce results for all pixels
          // in a quad, they just require all inputs of a quad to have been
          // computed for derivatives.
          markInstructionUses(MI, StateWQM, Worklist);
          GlobalFlags |= StateWQM;
        }
      } else if (Opcode == AMDGPU::WQM) {
        // The WQM intrinsic requires its output to have all the helper lanes
        // correct, so we need it to be in WQM.
        Flags = StateWQM;
        LowerToCopyInstrs.push_back(&MI);
      } else if (Opcode == AMDGPU::SOFT_WQM) {
        LowerToCopyInstrs.push_back(&MI);
        SoftWQMInstrs.push_back(&MI);
      } else if (Opcode == AMDGPU::STRICT_WWM) {
        // The STRICT_WWM intrinsic doesn't make the same guarantee, and plus
        // it needs to be executed in WQM or Exact so that its copy doesn't
        // clobber inactive lanes.
        markInstructionUses(MI, StateStrictWWM, Worklist);
        GlobalFlags |= StateStrictWWM;
        LowerToMovInstrs.push_back(&MI);
      } else if (Opcode == AMDGPU::STRICT_WQM ||
                 TII->isDualSourceBlendEXP(MI)) {
        // STRICT_WQM is similar to STRICTWWM, but instead of enabling all
        // threads of the wave like STRICTWWM, STRICT_WQM enables all threads in
        // quads that have at least one active thread.
        markInstructionUses(MI, StateStrictWQM, Worklist);
        GlobalFlags |= StateStrictWQM;

        if (Opcode == AMDGPU::STRICT_WQM) {
          LowerToMovInstrs.push_back(&MI);
        } else {
          // Dual source blend export acts as implicit strict-wqm, its sources
          // need to be shuffled in strict wqm, but the export itself needs to
          // run in exact mode.
          BBI.Needs |= StateExact;
          if (!(BBI.InNeeds & StateExact)) {
            BBI.InNeeds |= StateExact;
            Worklist.emplace_back(MBB);
          }
          GlobalFlags |= StateExact;
          III.Disabled = StateWQM | StateStrict;
        }
      } else if (Opcode == AMDGPU::LDS_PARAM_LOAD ||
                 Opcode == AMDGPU::DS_PARAM_LOAD ||
                 Opcode == AMDGPU::LDS_DIRECT_LOAD ||
                 Opcode == AMDGPU::DS_DIRECT_LOAD) {
        // Mark these STRICTWQM, but only for the instruction, not its operands.
        // This avoid unnecessarily marking M0 as requiring WQM.
        III.Needs |= StateStrictWQM;
        GlobalFlags |= StateStrictWQM;
      } else if (Opcode == AMDGPU::V_SET_INACTIVE_B32 ||
                 Opcode == AMDGPU::V_SET_INACTIVE_B64) {
        III.Disabled = StateStrict;
        MachineOperand &Inactive = MI.getOperand(2);
        if (Inactive.isReg()) {
          if (Inactive.isUndef()) {
            LowerToCopyInstrs.push_back(&MI);
          } else {
            markOperand(MI, Inactive, StateStrictWWM, Worklist);
          }
        }
        SetInactiveInstrs.push_back(&MI);
      } else if (TII->isDisableWQM(MI)) {
        BBI.Needs |= StateExact;
        if (!(BBI.InNeeds & StateExact)) {
          BBI.InNeeds |= StateExact;
          Worklist.emplace_back(MBB);
        }
        GlobalFlags |= StateExact;
        III.Disabled = StateWQM | StateStrict;
      } else if (Opcode == AMDGPU::SI_PS_LIVE ||
                 Opcode == AMDGPU::SI_LIVE_MASK) {
        LiveMaskQueries.push_back(&MI);
      } else if (Opcode == AMDGPU::SI_KILL_I1_TERMINATOR ||
                 Opcode == AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR ||
                 Opcode == AMDGPU::SI_DEMOTE_I1) {
        KillInstrs.push_back(&MI);
        BBI.NeedsLowering = true;
      } else if (Opcode == AMDGPU::SI_INIT_EXEC ||
                 Opcode == AMDGPU::SI_INIT_EXEC_FROM_INPUT) {
        InitExecInstrs.push_back(&MI);
      } else if (WQMOutputs) {
        // The function is in machine SSA form, which means that physical
        // VGPRs correspond to shader inputs and outputs. Inputs are
        // only used, outputs are only defined.
        // FIXME: is this still valid?
        for (const MachineOperand &MO : MI.defs()) {
          Register Reg = MO.getReg();
          if (Reg.isPhysical() &&
              TRI->hasVectorRegisters(TRI->getPhysRegBaseClass(Reg))) {
            Flags = StateWQM;
            break;
          }
        }
      }

      if (Flags) {
        markInstruction(MI, Flags, Worklist);
        GlobalFlags |= Flags;
      }
    }
  }

  // Mark sure that any SET_INACTIVE instructions are computed in WQM if WQM is
  // ever used anywhere in the function. This implements the corresponding
  // semantics of @llvm.amdgcn.set.inactive.
  // Similarly for SOFT_WQM instructions, implementing @llvm.amdgcn.softwqm.
  if (GlobalFlags & StateWQM) {
    for (MachineInstr *MI : SetInactiveInstrs)
      markInstruction(*MI, StateWQM, Worklist);
    for (MachineInstr *MI : SoftWQMInstrs)
      markInstruction(*MI, StateWQM, Worklist);
  }

  return GlobalFlags;
}

void SIWholeQuadMode::propagateInstruction(MachineInstr &MI,
                                           std::vector<WorkItem>& Worklist) {
  MachineBasicBlock *MBB = MI.getParent();
  InstrInfo II = Instructions[&MI]; // take a copy to prevent dangling references
  BlockInfo &BI = Blocks[MBB];

  // Control flow-type instructions and stores to temporary memory that are
  // followed by WQM computations must themselves be in WQM.
  if ((II.OutNeeds & StateWQM) && !(II.Disabled & StateWQM) &&
      (MI.isTerminator() || (TII->usesVM_CNT(MI) && MI.mayStore()))) {
    Instructions[&MI].Needs = StateWQM;
    II.Needs = StateWQM;
  }

  // Propagate to block level
  if (II.Needs & StateWQM) {
    BI.Needs |= StateWQM;
    if (!(BI.InNeeds & StateWQM)) {
      BI.InNeeds |= StateWQM;
      Worklist.emplace_back(MBB);
    }
  }

  // Propagate backwards within block
  if (MachineInstr *PrevMI = MI.getPrevNode()) {
    char InNeeds = (II.Needs & ~StateStrict) | II.OutNeeds;
    if (!PrevMI->isPHI()) {
      InstrInfo &PrevII = Instructions[PrevMI];
      if ((PrevII.OutNeeds | InNeeds) != PrevII.OutNeeds) {
        PrevII.OutNeeds |= InNeeds;
        Worklist.emplace_back(PrevMI);
      }
    }
  }

  // Propagate WQM flag to instruction inputs
  assert(!(II.Needs & StateExact));

  if (II.Needs != 0)
    markInstructionUses(MI, II.Needs, Worklist);

  // Ensure we process a block containing StrictWWM/StrictWQM, even if it does
  // not require any WQM transitions.
  if (II.Needs & StateStrictWWM)
    BI.Needs |= StateStrictWWM;
  if (II.Needs & StateStrictWQM)
    BI.Needs |= StateStrictWQM;
}

void SIWholeQuadMode::propagateBlock(MachineBasicBlock &MBB,
                                     std::vector<WorkItem>& Worklist) {
  BlockInfo BI = Blocks[&MBB]; // Make a copy to prevent dangling references.

  // Propagate through instructions
  if (!MBB.empty()) {
    MachineInstr *LastMI = &*MBB.rbegin();
    InstrInfo &LastII = Instructions[LastMI];
    if ((LastII.OutNeeds | BI.OutNeeds) != LastII.OutNeeds) {
      LastII.OutNeeds |= BI.OutNeeds;
      Worklist.emplace_back(LastMI);
    }
  }

  // Predecessor blocks must provide for our WQM/Exact needs.
  for (MachineBasicBlock *Pred : MBB.predecessors()) {
    BlockInfo &PredBI = Blocks[Pred];
    if ((PredBI.OutNeeds | BI.InNeeds) == PredBI.OutNeeds)
      continue;

    PredBI.OutNeeds |= BI.InNeeds;
    PredBI.InNeeds |= BI.InNeeds;
    Worklist.emplace_back(Pred);
  }

  // All successors must be prepared to accept the same set of WQM/Exact data.
  for (MachineBasicBlock *Succ : MBB.successors()) {
    BlockInfo &SuccBI = Blocks[Succ];
    if ((SuccBI.InNeeds | BI.OutNeeds) == SuccBI.InNeeds)
      continue;

    SuccBI.InNeeds |= BI.OutNeeds;
    Worklist.emplace_back(Succ);
  }
}

char SIWholeQuadMode::analyzeFunction(MachineFunction &MF) {
  std::vector<WorkItem> Worklist;
  char GlobalFlags = scanInstructions(MF, Worklist);

  while (!Worklist.empty()) {
    WorkItem WI = Worklist.back();
    Worklist.pop_back();

    if (WI.MI)
      propagateInstruction(*WI.MI, Worklist);
    else
      propagateBlock(*WI.MBB, Worklist);
  }

  return GlobalFlags;
}

MachineBasicBlock::iterator
SIWholeQuadMode::saveSCC(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator Before) {
  Register SaveReg = MRI->createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  MachineInstr *Save =
      BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::COPY), SaveReg)
          .addReg(AMDGPU::SCC);
  MachineInstr *Restore =
      BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::COPY), AMDGPU::SCC)
          .addReg(SaveReg);

  LIS->InsertMachineInstrInMaps(*Save);
  LIS->InsertMachineInstrInMaps(*Restore);
  LIS->createAndComputeVirtRegInterval(SaveReg);

  return Restore;
}

MachineBasicBlock *SIWholeQuadMode::splitBlock(MachineBasicBlock *BB,
                                               MachineInstr *TermMI) {
  LLVM_DEBUG(dbgs() << "Split block " << printMBBReference(*BB) << " @ "
                    << *TermMI << "\n");

  MachineBasicBlock *SplitBB =
      BB->splitAt(*TermMI, /*UpdateLiveIns*/ true, LIS);

  // Convert last instruction in block to a terminator.
  // Note: this only covers the expected patterns
  unsigned NewOpcode = 0;
  switch (TermMI->getOpcode()) {
  case AMDGPU::S_AND_B32:
    NewOpcode = AMDGPU::S_AND_B32_term;
    break;
  case AMDGPU::S_AND_B64:
    NewOpcode = AMDGPU::S_AND_B64_term;
    break;
  case AMDGPU::S_MOV_B32:
    NewOpcode = AMDGPU::S_MOV_B32_term;
    break;
  case AMDGPU::S_MOV_B64:
    NewOpcode = AMDGPU::S_MOV_B64_term;
    break;
  default:
    break;
  }
  if (NewOpcode)
    TermMI->setDesc(TII->get(NewOpcode));

  if (SplitBB != BB) {
    // Update dominator trees
    using DomTreeT = DomTreeBase<MachineBasicBlock>;
    SmallVector<DomTreeT::UpdateType, 16> DTUpdates;
    for (MachineBasicBlock *Succ : SplitBB->successors()) {
      DTUpdates.push_back({DomTreeT::Insert, SplitBB, Succ});
      DTUpdates.push_back({DomTreeT::Delete, BB, Succ});
    }
    DTUpdates.push_back({DomTreeT::Insert, BB, SplitBB});
    if (MDT)
      MDT->getBase().applyUpdates(DTUpdates);
    if (PDT)
      PDT->applyUpdates(DTUpdates);

    // Link blocks
    MachineInstr *MI =
        BuildMI(*BB, BB->end(), DebugLoc(), TII->get(AMDGPU::S_BRANCH))
            .addMBB(SplitBB);
    LIS->InsertMachineInstrInMaps(*MI);
  }

  return SplitBB;
}

MachineInstr *SIWholeQuadMode::lowerKillF32(MachineBasicBlock &MBB,
                                            MachineInstr &MI) {
  assert(LiveMaskReg.isVirtual());

  const DebugLoc &DL = MI.getDebugLoc();
  unsigned Opcode = 0;

  assert(MI.getOperand(0).isReg());

  // Comparison is for live lanes; however here we compute the inverse
  // (killed lanes).  This is because VCMP will always generate 0 bits
  // for inactive lanes so a mask of live lanes would not be correct
  // inside control flow.
  // Invert the comparison by swapping the operands and adjusting
  // the comparison codes.

  switch (MI.getOperand(2).getImm()) {
  case ISD::SETUEQ:
    Opcode = AMDGPU::V_CMP_LG_F32_e64;
    break;
  case ISD::SETUGT:
    Opcode = AMDGPU::V_CMP_GE_F32_e64;
    break;
  case ISD::SETUGE:
    Opcode = AMDGPU::V_CMP_GT_F32_e64;
    break;
  case ISD::SETULT:
    Opcode = AMDGPU::V_CMP_LE_F32_e64;
    break;
  case ISD::SETULE:
    Opcode = AMDGPU::V_CMP_LT_F32_e64;
    break;
  case ISD::SETUNE:
    Opcode = AMDGPU::V_CMP_EQ_F32_e64;
    break;
  case ISD::SETO:
    Opcode = AMDGPU::V_CMP_O_F32_e64;
    break;
  case ISD::SETUO:
    Opcode = AMDGPU::V_CMP_U_F32_e64;
    break;
  case ISD::SETOEQ:
  case ISD::SETEQ:
    Opcode = AMDGPU::V_CMP_NEQ_F32_e64;
    break;
  case ISD::SETOGT:
  case ISD::SETGT:
    Opcode = AMDGPU::V_CMP_NLT_F32_e64;
    break;
  case ISD::SETOGE:
  case ISD::SETGE:
    Opcode = AMDGPU::V_CMP_NLE_F32_e64;
    break;
  case ISD::SETOLT:
  case ISD::SETLT:
    Opcode = AMDGPU::V_CMP_NGT_F32_e64;
    break;
  case ISD::SETOLE:
  case ISD::SETLE:
    Opcode = AMDGPU::V_CMP_NGE_F32_e64;
    break;
  case ISD::SETONE:
  case ISD::SETNE:
    Opcode = AMDGPU::V_CMP_NLG_F32_e64;
    break;
  default:
    llvm_unreachable("invalid ISD:SET cond code");
  }

  // Pick opcode based on comparison type.
  MachineInstr *VcmpMI;
  const MachineOperand &Op0 = MI.getOperand(0);
  const MachineOperand &Op1 = MI.getOperand(1);

  // VCC represents lanes killed.
  Register VCC = ST->isWave32() ? AMDGPU::VCC_LO : AMDGPU::VCC;

  if (TRI->isVGPR(*MRI, Op0.getReg())) {
    Opcode = AMDGPU::getVOPe32(Opcode);
    VcmpMI = BuildMI(MBB, &MI, DL, TII->get(Opcode)).add(Op1).add(Op0);
  } else {
    VcmpMI = BuildMI(MBB, &MI, DL, TII->get(Opcode))
                 .addReg(VCC, RegState::Define)
                 .addImm(0) // src0 modifiers
                 .add(Op1)
                 .addImm(0) // src1 modifiers
                 .add(Op0)
                 .addImm(0); // omod
  }

  MachineInstr *MaskUpdateMI =
      BuildMI(MBB, MI, DL, TII->get(AndN2Opc), LiveMaskReg)
          .addReg(LiveMaskReg)
          .addReg(VCC);

  // State of SCC represents whether any lanes are live in mask,
  // if SCC is 0 then no lanes will be alive anymore.
  MachineInstr *EarlyTermMI =
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::SI_EARLY_TERMINATE_SCC0));

  MachineInstr *ExecMaskMI =
      BuildMI(MBB, MI, DL, TII->get(AndN2Opc), Exec).addReg(Exec).addReg(VCC);

  assert(MBB.succ_size() == 1);
  MachineInstr *NewTerm = BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_BRANCH))
                              .addMBB(*MBB.succ_begin());

  // Update live intervals
  LIS->ReplaceMachineInstrInMaps(MI, *VcmpMI);
  MBB.remove(&MI);

  LIS->InsertMachineInstrInMaps(*MaskUpdateMI);
  LIS->InsertMachineInstrInMaps(*ExecMaskMI);
  LIS->InsertMachineInstrInMaps(*EarlyTermMI);
  LIS->InsertMachineInstrInMaps(*NewTerm);

  return NewTerm;
}

MachineInstr *SIWholeQuadMode::lowerKillI1(MachineBasicBlock &MBB,
                                           MachineInstr &MI, bool IsWQM) {
  assert(LiveMaskReg.isVirtual());

  const DebugLoc &DL = MI.getDebugLoc();
  MachineInstr *MaskUpdateMI = nullptr;

  const bool IsDemote = IsWQM && (MI.getOpcode() == AMDGPU::SI_DEMOTE_I1);
  const MachineOperand &Op = MI.getOperand(0);
  int64_t KillVal = MI.getOperand(1).getImm();
  MachineInstr *ComputeKilledMaskMI = nullptr;
  Register CndReg = !Op.isImm() ? Op.getReg() : Register();
  Register TmpReg;

  // Is this a static or dynamic kill?
  if (Op.isImm()) {
    if (Op.getImm() == KillVal) {
      // Static: all active lanes are killed
      MaskUpdateMI = BuildMI(MBB, MI, DL, TII->get(AndN2Opc), LiveMaskReg)
                         .addReg(LiveMaskReg)
                         .addReg(Exec);
    } else {
      // Static: kill does nothing
      MachineInstr *NewTerm = nullptr;
      if (MI.getOpcode() == AMDGPU::SI_DEMOTE_I1) {
        LIS->RemoveMachineInstrFromMaps(MI);
      } else {
        assert(MBB.succ_size() == 1);
        NewTerm = BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_BRANCH))
                      .addMBB(*MBB.succ_begin());
        LIS->ReplaceMachineInstrInMaps(MI, *NewTerm);
      }
      MBB.remove(&MI);
      return NewTerm;
    }
  } else {
    if (!KillVal) {
      // Op represents live lanes after kill,
      // so exec mask needs to be factored in.
      TmpReg = MRI->createVirtualRegister(TRI->getBoolRC());
      ComputeKilledMaskMI =
          BuildMI(MBB, MI, DL, TII->get(XorOpc), TmpReg).add(Op).addReg(Exec);
      MaskUpdateMI = BuildMI(MBB, MI, DL, TII->get(AndN2Opc), LiveMaskReg)
                         .addReg(LiveMaskReg)
                         .addReg(TmpReg);
    } else {
      // Op represents lanes to kill
      MaskUpdateMI = BuildMI(MBB, MI, DL, TII->get(AndN2Opc), LiveMaskReg)
                         .addReg(LiveMaskReg)
                         .add(Op);
    }
  }

  // State of SCC represents whether any lanes are live in mask,
  // if SCC is 0 then no lanes will be alive anymore.
  MachineInstr *EarlyTermMI =
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::SI_EARLY_TERMINATE_SCC0));

  // In the case we got this far some lanes are still live,
  // update EXEC to deactivate lanes as appropriate.
  MachineInstr *NewTerm;
  MachineInstr *WQMMaskMI = nullptr;
  Register LiveMaskWQM;
  if (IsDemote) {
    // Demote - deactivate quads with only helper lanes
    LiveMaskWQM = MRI->createVirtualRegister(TRI->getBoolRC());
    WQMMaskMI =
        BuildMI(MBB, MI, DL, TII->get(WQMOpc), LiveMaskWQM).addReg(LiveMaskReg);
    NewTerm = BuildMI(MBB, MI, DL, TII->get(AndOpc), Exec)
                  .addReg(Exec)
                  .addReg(LiveMaskWQM);
  } else {
    // Kill - deactivate lanes no longer in live mask
    if (Op.isImm()) {
      unsigned MovOpc = ST->isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
      NewTerm = BuildMI(MBB, &MI, DL, TII->get(MovOpc), Exec).addImm(0);
    } else if (!IsWQM) {
      NewTerm = BuildMI(MBB, &MI, DL, TII->get(AndOpc), Exec)
                    .addReg(Exec)
                    .addReg(LiveMaskReg);
    } else {
      unsigned Opcode = KillVal ? AndN2Opc : AndOpc;
      NewTerm =
          BuildMI(MBB, &MI, DL, TII->get(Opcode), Exec).addReg(Exec).add(Op);
    }
  }

  // Update live intervals
  LIS->RemoveMachineInstrFromMaps(MI);
  MBB.remove(&MI);
  assert(EarlyTermMI);
  assert(MaskUpdateMI);
  assert(NewTerm);
  if (ComputeKilledMaskMI)
    LIS->InsertMachineInstrInMaps(*ComputeKilledMaskMI);
  LIS->InsertMachineInstrInMaps(*MaskUpdateMI);
  LIS->InsertMachineInstrInMaps(*EarlyTermMI);
  if (WQMMaskMI)
    LIS->InsertMachineInstrInMaps(*WQMMaskMI);
  LIS->InsertMachineInstrInMaps(*NewTerm);

  if (CndReg) {
    LIS->removeInterval(CndReg);
    LIS->createAndComputeVirtRegInterval(CndReg);
  }
  if (TmpReg)
    LIS->createAndComputeVirtRegInterval(TmpReg);
  if (LiveMaskWQM)
    LIS->createAndComputeVirtRegInterval(LiveMaskWQM);

  return NewTerm;
}

// Replace (or supplement) instructions accessing live mask.
// This can only happen once all the live mask registers have been created
// and the execute state (WQM/StrictWWM/Exact) of instructions is known.
void SIWholeQuadMode::lowerBlock(MachineBasicBlock &MBB) {
  auto BII = Blocks.find(&MBB);
  if (BII == Blocks.end())
    return;

  const BlockInfo &BI = BII->second;
  if (!BI.NeedsLowering)
    return;

  LLVM_DEBUG(dbgs() << "\nLowering block " << printMBBReference(MBB) << ":\n");

  SmallVector<MachineInstr *, 4> SplitPoints;
  char State = BI.InitialState;

  for (MachineInstr &MI : llvm::make_early_inc_range(
           llvm::make_range(MBB.getFirstNonPHI(), MBB.end()))) {
    if (StateTransition.count(&MI))
      State = StateTransition[&MI];

    MachineInstr *SplitPoint = nullptr;
    switch (MI.getOpcode()) {
    case AMDGPU::SI_DEMOTE_I1:
    case AMDGPU::SI_KILL_I1_TERMINATOR:
      SplitPoint = lowerKillI1(MBB, MI, State == StateWQM);
      break;
    case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
      SplitPoint = lowerKillF32(MBB, MI);
      break;
    default:
      break;
    }
    if (SplitPoint)
      SplitPoints.push_back(SplitPoint);
  }

  // Perform splitting after instruction scan to simplify iteration.
  if (!SplitPoints.empty()) {
    MachineBasicBlock *BB = &MBB;
    for (MachineInstr *MI : SplitPoints) {
      BB = splitBlock(BB, MI);
    }
  }
}

// Return an iterator in the (inclusive) range [First, Last] at which
// instructions can be safely inserted, keeping in mind that some of the
// instructions we want to add necessarily clobber SCC.
MachineBasicBlock::iterator SIWholeQuadMode::prepareInsertion(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator First,
    MachineBasicBlock::iterator Last, bool PreferLast, bool SaveSCC) {
  if (!SaveSCC)
    return PreferLast ? Last : First;

  LiveRange &LR =
      LIS->getRegUnit(*TRI->regunits(MCRegister::from(AMDGPU::SCC)).begin());
  auto MBBE = MBB.end();
  SlotIndex FirstIdx = First != MBBE ? LIS->getInstructionIndex(*First)
                                     : LIS->getMBBEndIdx(&MBB);
  SlotIndex LastIdx =
      Last != MBBE ? LIS->getInstructionIndex(*Last) : LIS->getMBBEndIdx(&MBB);
  SlotIndex Idx = PreferLast ? LastIdx : FirstIdx;
  const LiveRange::Segment *S;

  for (;;) {
    S = LR.getSegmentContaining(Idx);
    if (!S)
      break;

    if (PreferLast) {
      SlotIndex Next = S->start.getBaseIndex();
      if (Next < FirstIdx)
        break;
      Idx = Next;
    } else {
      MachineInstr *EndMI = LIS->getInstructionFromIndex(S->end.getBaseIndex());
      assert(EndMI && "Segment does not end on valid instruction");
      auto NextI = std::next(EndMI->getIterator());
      if (NextI == MBB.end())
        break;
      SlotIndex Next = LIS->getInstructionIndex(*NextI);
      if (Next > LastIdx)
        break;
      Idx = Next;
    }
  }

  MachineBasicBlock::iterator MBBI;

  if (MachineInstr *MI = LIS->getInstructionFromIndex(Idx))
    MBBI = MI;
  else {
    assert(Idx == LIS->getMBBEndIdx(&MBB));
    MBBI = MBB.end();
  }

  // Move insertion point past any operations modifying EXEC.
  // This assumes that the value of SCC defined by any of these operations
  // does not need to be preserved.
  while (MBBI != Last) {
    bool IsExecDef = false;
    for (const MachineOperand &MO : MBBI->all_defs()) {
      IsExecDef |=
          MO.getReg() == AMDGPU::EXEC_LO || MO.getReg() == AMDGPU::EXEC;
    }
    if (!IsExecDef)
      break;
    MBBI++;
    S = nullptr;
  }

  if (S)
    MBBI = saveSCC(MBB, MBBI);

  return MBBI;
}

void SIWholeQuadMode::toExact(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator Before,
                              Register SaveWQM) {
  assert(LiveMaskReg.isVirtual());

  bool IsTerminator = Before == MBB.end();
  if (!IsTerminator) {
    auto FirstTerm = MBB.getFirstTerminator();
    if (FirstTerm != MBB.end()) {
      SlotIndex FirstTermIdx = LIS->getInstructionIndex(*FirstTerm);
      SlotIndex BeforeIdx = LIS->getInstructionIndex(*Before);
      IsTerminator = BeforeIdx > FirstTermIdx;
    }
  }

  MachineInstr *MI;

  if (SaveWQM) {
    unsigned Opcode = IsTerminator ? AndSaveExecTermOpc : AndSaveExecOpc;
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(Opcode), SaveWQM)
             .addReg(LiveMaskReg);
  } else {
    unsigned Opcode = IsTerminator ? AndTermOpc : AndOpc;
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(Opcode), Exec)
             .addReg(Exec)
             .addReg(LiveMaskReg);
  }

  LIS->InsertMachineInstrInMaps(*MI);
  StateTransition[MI] = StateExact;
}

void SIWholeQuadMode::toWQM(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator Before,
                            Register SavedWQM) {
  MachineInstr *MI;

  if (SavedWQM) {
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::COPY), Exec)
             .addReg(SavedWQM);
  } else {
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(WQMOpc), Exec).addReg(Exec);
  }

  LIS->InsertMachineInstrInMaps(*MI);
  StateTransition[MI] = StateWQM;
}

void SIWholeQuadMode::toStrictMode(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator Before,
                                   Register SaveOrig, char StrictStateNeeded) {
  MachineInstr *MI;
  assert(SaveOrig);
  assert(StrictStateNeeded == StateStrictWWM ||
         StrictStateNeeded == StateStrictWQM);

  if (StrictStateNeeded == StateStrictWWM) {
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::ENTER_STRICT_WWM),
                 SaveOrig)
             .addImm(-1);
  } else {
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::ENTER_STRICT_WQM),
                 SaveOrig)
             .addImm(-1);
  }
  LIS->InsertMachineInstrInMaps(*MI);
  StateTransition[MI] = StrictStateNeeded;
}

void SIWholeQuadMode::fromStrictMode(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator Before,
                                     Register SavedOrig, char NonStrictState,
                                     char CurrentStrictState) {
  MachineInstr *MI;

  assert(SavedOrig);
  assert(CurrentStrictState == StateStrictWWM ||
         CurrentStrictState == StateStrictWQM);

  if (CurrentStrictState == StateStrictWWM) {
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::EXIT_STRICT_WWM),
                 Exec)
             .addReg(SavedOrig);
  } else {
    MI = BuildMI(MBB, Before, DebugLoc(), TII->get(AMDGPU::EXIT_STRICT_WQM),
                 Exec)
             .addReg(SavedOrig);
  }
  LIS->InsertMachineInstrInMaps(*MI);
  StateTransition[MI] = NonStrictState;
}

void SIWholeQuadMode::processBlock(MachineBasicBlock &MBB, bool IsEntry) {
  auto BII = Blocks.find(&MBB);
  if (BII == Blocks.end())
    return;

  BlockInfo &BI = BII->second;

  // This is a non-entry block that is WQM throughout, so no need to do
  // anything.
  if (!IsEntry && BI.Needs == StateWQM && BI.OutNeeds != StateExact) {
    BI.InitialState = StateWQM;
    return;
  }

  LLVM_DEBUG(dbgs() << "\nProcessing block " << printMBBReference(MBB)
                    << ":\n");

  Register SavedWQMReg;
  Register SavedNonStrictReg;
  bool WQMFromExec = IsEntry;
  char State = (IsEntry || !(BI.InNeeds & StateWQM)) ? StateExact : StateWQM;
  char NonStrictState = 0;
  const TargetRegisterClass *BoolRC = TRI->getBoolRC();

  auto II = MBB.getFirstNonPHI(), IE = MBB.end();
  if (IsEntry) {
    // Skip the instruction that saves LiveMask
    if (II != IE && II->getOpcode() == AMDGPU::COPY &&
        II->getOperand(1).getReg() == TRI->getExec())
      ++II;
  }

  // This stores the first instruction where it's safe to switch from WQM to
  // Exact or vice versa.
  MachineBasicBlock::iterator FirstWQM = IE;

  // This stores the first instruction where it's safe to switch from Strict
  // mode to Exact/WQM or to switch to Strict mode. It must always be the same
  // as, or after, FirstWQM since if it's safe to switch to/from Strict, it must
  // be safe to switch to/from WQM as well.
  MachineBasicBlock::iterator FirstStrict = IE;

  // Record initial state is block information.
  BI.InitialState = State;

  for (;;) {
    MachineBasicBlock::iterator Next = II;
    char Needs = StateExact | StateWQM; // Strict mode is disabled by default.
    char OutNeeds = 0;

    if (FirstWQM == IE)
      FirstWQM = II;

    if (FirstStrict == IE)
      FirstStrict = II;

    // First, figure out the allowed states (Needs) based on the propagated
    // flags.
    if (II != IE) {
      MachineInstr &MI = *II;

      if (MI.isTerminator() || TII->mayReadEXEC(*MRI, MI)) {
        auto III = Instructions.find(&MI);
        if (III != Instructions.end()) {
          if (III->second.Needs & StateStrictWWM)
            Needs = StateStrictWWM;
          else if (III->second.Needs & StateStrictWQM)
            Needs = StateStrictWQM;
          else if (III->second.Needs & StateWQM)
            Needs = StateWQM;
          else
            Needs &= ~III->second.Disabled;
          OutNeeds = III->second.OutNeeds;
        }
      } else {
        // If the instruction doesn't actually need a correct EXEC, then we can
        // safely leave Strict mode enabled.
        Needs = StateExact | StateWQM | StateStrict;
      }

      // Exact mode exit can occur in terminators, but must be before branches.
      if (MI.isBranch() && OutNeeds == StateExact)
        Needs = StateExact;

      ++Next;
    } else {
      // End of basic block
      if (BI.OutNeeds & StateWQM)
        Needs = StateWQM;
      else if (BI.OutNeeds == StateExact)
        Needs = StateExact;
      else
        Needs = StateWQM | StateExact;
    }

    // Now, transition if necessary.
    if (!(Needs & State)) {
      MachineBasicBlock::iterator First;
      if (State == StateStrictWWM || Needs == StateStrictWWM ||
          State == StateStrictWQM || Needs == StateStrictWQM) {
        // We must switch to or from Strict mode.
        First = FirstStrict;
      } else {
        // We only need to switch to/from WQM, so we can use FirstWQM.
        First = FirstWQM;
      }

      // Whether we need to save SCC depends on start and end states.
      bool SaveSCC = false;
      switch (State) {
      case StateExact:
      case StateStrictWWM:
      case StateStrictWQM:
        // Exact/Strict -> Strict: save SCC
        // Exact/Strict -> WQM: save SCC if WQM mask is generated from exec
        // Exact/Strict -> Exact: no save
        SaveSCC = (Needs & StateStrict) || ((Needs & StateWQM) && WQMFromExec);
        break;
      case StateWQM:
        // WQM -> Exact/Strict: save SCC
        SaveSCC = !(Needs & StateWQM);
        break;
      default:
        llvm_unreachable("Unknown state");
        break;
      }
      MachineBasicBlock::iterator Before =
          prepareInsertion(MBB, First, II, Needs == StateWQM, SaveSCC);

      if (State & StateStrict) {
        assert(State == StateStrictWWM || State == StateStrictWQM);
        assert(SavedNonStrictReg);
        fromStrictMode(MBB, Before, SavedNonStrictReg, NonStrictState, State);

        LIS->createAndComputeVirtRegInterval(SavedNonStrictReg);
        SavedNonStrictReg = 0;
        State = NonStrictState;
      }

      if (Needs & StateStrict) {
        NonStrictState = State;
        assert(Needs == StateStrictWWM || Needs == StateStrictWQM);
        assert(!SavedNonStrictReg);
        SavedNonStrictReg = MRI->createVirtualRegister(BoolRC);

        toStrictMode(MBB, Before, SavedNonStrictReg, Needs);
        State = Needs;

      } else {
        if (State == StateWQM && (Needs & StateExact) && !(Needs & StateWQM)) {
          if (!WQMFromExec && (OutNeeds & StateWQM)) {
            assert(!SavedWQMReg);
            SavedWQMReg = MRI->createVirtualRegister(BoolRC);
          }

          toExact(MBB, Before, SavedWQMReg);
          State = StateExact;
        } else if (State == StateExact && (Needs & StateWQM) &&
                   !(Needs & StateExact)) {
          assert(WQMFromExec == (SavedWQMReg == 0));

          toWQM(MBB, Before, SavedWQMReg);

          if (SavedWQMReg) {
            LIS->createAndComputeVirtRegInterval(SavedWQMReg);
            SavedWQMReg = 0;
          }
          State = StateWQM;
        } else {
          // We can get here if we transitioned from StrictWWM to a
          // non-StrictWWM state that already matches our needs, but we
          // shouldn't need to do anything.
          assert(Needs & State);
        }
      }
    }

    if (Needs != (StateExact | StateWQM | StateStrict)) {
      if (Needs != (StateExact | StateWQM))
        FirstWQM = IE;
      FirstStrict = IE;
    }

    if (II == IE)
      break;

    II = Next;
  }
  assert(!SavedWQMReg);
  assert(!SavedNonStrictReg);
}

bool SIWholeQuadMode::lowerLiveMaskQueries() {
  for (MachineInstr *MI : LiveMaskQueries) {
    const DebugLoc &DL = MI->getDebugLoc();
    Register Dest = MI->getOperand(0).getReg();

    MachineInstr *Copy =
        BuildMI(*MI->getParent(), MI, DL, TII->get(AMDGPU::COPY), Dest)
            .addReg(LiveMaskReg);

    LIS->ReplaceMachineInstrInMaps(*MI, *Copy);
    MI->eraseFromParent();
  }
  return !LiveMaskQueries.empty();
}

bool SIWholeQuadMode::lowerCopyInstrs() {
  for (MachineInstr *MI : LowerToMovInstrs) {
    assert(MI->getNumExplicitOperands() == 2);

    const Register Reg = MI->getOperand(0).getReg();

    const TargetRegisterClass *regClass =
        TRI->getRegClassForOperandReg(*MRI, MI->getOperand(0));
    if (TRI->isVGPRClass(regClass)) {
      const unsigned MovOp = TII->getMovOpcode(regClass);
      MI->setDesc(TII->get(MovOp));

      // Check that it already implicitly depends on exec (like all VALU movs
      // should do).
      assert(any_of(MI->implicit_operands(), [](const MachineOperand &MO) {
        return MO.isUse() && MO.getReg() == AMDGPU::EXEC;
      }));
    } else {
      // Remove early-clobber and exec dependency from simple SGPR copies.
      // This allows some to be eliminated during/post RA.
      LLVM_DEBUG(dbgs() << "simplify SGPR copy: " << *MI);
      if (MI->getOperand(0).isEarlyClobber()) {
        LIS->removeInterval(Reg);
        MI->getOperand(0).setIsEarlyClobber(false);
        LIS->createAndComputeVirtRegInterval(Reg);
      }
      int Index = MI->findRegisterUseOperandIdx(AMDGPU::EXEC, /*TRI=*/nullptr);
      while (Index >= 0) {
        MI->removeOperand(Index);
        Index = MI->findRegisterUseOperandIdx(AMDGPU::EXEC, /*TRI=*/nullptr);
      }
      MI->setDesc(TII->get(AMDGPU::COPY));
      LLVM_DEBUG(dbgs() << "  -> " << *MI);
    }
  }
  for (MachineInstr *MI : LowerToCopyInstrs) {
    if (MI->getOpcode() == AMDGPU::V_SET_INACTIVE_B32 ||
        MI->getOpcode() == AMDGPU::V_SET_INACTIVE_B64) {
      assert(MI->getNumExplicitOperands() == 3);
      // the only reason we should be here is V_SET_INACTIVE has
      // an undef input so it is being replaced by a simple copy.
      // There should be a second undef source that we should remove.
      assert(MI->getOperand(2).isUndef());
      MI->removeOperand(2);
      MI->untieRegOperand(1);
    } else {
      assert(MI->getNumExplicitOperands() == 2);
    }

    unsigned CopyOp = MI->getOperand(1).isReg()
                          ? (unsigned)AMDGPU::COPY
                          : TII->getMovOpcode(TRI->getRegClassForOperandReg(
                                *MRI, MI->getOperand(0)));
    MI->setDesc(TII->get(CopyOp));
  }
  return !LowerToCopyInstrs.empty() || !LowerToMovInstrs.empty();
}

bool SIWholeQuadMode::lowerKillInstrs(bool IsWQM) {
  for (MachineInstr *MI : KillInstrs) {
    MachineBasicBlock *MBB = MI->getParent();
    MachineInstr *SplitPoint = nullptr;
    switch (MI->getOpcode()) {
    case AMDGPU::SI_DEMOTE_I1:
    case AMDGPU::SI_KILL_I1_TERMINATOR:
      SplitPoint = lowerKillI1(*MBB, *MI, IsWQM);
      break;
    case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
      SplitPoint = lowerKillF32(*MBB, *MI);
      break;
    }
    if (SplitPoint)
      splitBlock(MBB, SplitPoint);
  }
  return !KillInstrs.empty();
}

void SIWholeQuadMode::lowerInitExec(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  bool IsWave32 = ST->isWave32();

  if (MI.getOpcode() == AMDGPU::SI_INIT_EXEC) {
    // This should be before all vector instructions.
    MachineInstr *InitMI =
        BuildMI(*MBB, MBB->begin(), MI.getDebugLoc(),
                TII->get(IsWave32 ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64),
                Exec)
            .addImm(MI.getOperand(0).getImm());
    if (LIS) {
      LIS->RemoveMachineInstrFromMaps(MI);
      LIS->InsertMachineInstrInMaps(*InitMI);
    }
    MI.eraseFromParent();
    return;
  }

  // Extract the thread count from an SGPR input and set EXEC accordingly.
  // Since BFM can't shift by 64, handle that case with CMP + CMOV.
  //
  // S_BFE_U32 count, input, {shift, 7}
  // S_BFM_B64 exec, count, 0
  // S_CMP_EQ_U32 count, 64
  // S_CMOV_B64 exec, -1
  Register InputReg = MI.getOperand(0).getReg();
  MachineInstr *FirstMI = &*MBB->begin();
  if (InputReg.isVirtual()) {
    MachineInstr *DefInstr = MRI->getVRegDef(InputReg);
    assert(DefInstr && DefInstr->isCopy());
    if (DefInstr->getParent() == MBB) {
      if (DefInstr != FirstMI) {
        // If the `InputReg` is defined in current block, we also need to
        // move that instruction to the beginning of the block.
        DefInstr->removeFromParent();
        MBB->insert(FirstMI, DefInstr);
        if (LIS)
          LIS->handleMove(*DefInstr);
      } else {
        // If first instruction is definition then move pointer after it.
        FirstMI = &*std::next(FirstMI->getIterator());
      }
    }
  }

  // Insert instruction sequence at block beginning (before vector operations).
  const DebugLoc DL = MI.getDebugLoc();
  const unsigned WavefrontSize = ST->getWavefrontSize();
  const unsigned Mask = (WavefrontSize << 1) - 1;
  Register CountReg = MRI->createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  auto BfeMI = BuildMI(*MBB, FirstMI, DL, TII->get(AMDGPU::S_BFE_U32), CountReg)
                   .addReg(InputReg)
                   .addImm((MI.getOperand(1).getImm() & Mask) | 0x70000);
  auto BfmMI =
      BuildMI(*MBB, FirstMI, DL,
              TII->get(IsWave32 ? AMDGPU::S_BFM_B32 : AMDGPU::S_BFM_B64), Exec)
          .addReg(CountReg)
          .addImm(0);
  auto CmpMI = BuildMI(*MBB, FirstMI, DL, TII->get(AMDGPU::S_CMP_EQ_U32))
                   .addReg(CountReg, RegState::Kill)
                   .addImm(WavefrontSize);
  auto CmovMI =
      BuildMI(*MBB, FirstMI, DL,
              TII->get(IsWave32 ? AMDGPU::S_CMOV_B32 : AMDGPU::S_CMOV_B64),
              Exec)
          .addImm(-1);

  if (!LIS) {
    MI.eraseFromParent();
    return;
  }

  LIS->RemoveMachineInstrFromMaps(MI);
  MI.eraseFromParent();

  LIS->InsertMachineInstrInMaps(*BfeMI);
  LIS->InsertMachineInstrInMaps(*BfmMI);
  LIS->InsertMachineInstrInMaps(*CmpMI);
  LIS->InsertMachineInstrInMaps(*CmovMI);

  LIS->removeInterval(InputReg);
  LIS->createAndComputeVirtRegInterval(InputReg);
  LIS->createAndComputeVirtRegInterval(CountReg);
}

/// Lower INIT_EXEC instructions. Return a suitable insert point in \p Entry
/// for instructions that depend on EXEC.
MachineBasicBlock::iterator
SIWholeQuadMode::lowerInitExecInstrs(MachineBasicBlock &Entry, bool &Changed) {
  MachineBasicBlock::iterator InsertPt = Entry.getFirstNonPHI();

  for (MachineInstr *MI : InitExecInstrs) {
    // Try to handle undefined cases gracefully:
    // - multiple INIT_EXEC instructions
    // - INIT_EXEC instructions not in the entry block
    if (MI->getParent() == &Entry)
      InsertPt = std::next(MI->getIterator());

    lowerInitExec(*MI);
    Changed = true;
  }

  return InsertPt;
}

bool SIWholeQuadMode::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "SI Whole Quad Mode on " << MF.getName()
                    << " ------------- \n");
  LLVM_DEBUG(MF.dump(););

  Instructions.clear();
  Blocks.clear();
  LiveMaskQueries.clear();
  LowerToCopyInstrs.clear();
  LowerToMovInstrs.clear();
  KillInstrs.clear();
  InitExecInstrs.clear();
  StateTransition.clear();

  ST = &MF.getSubtarget<GCNSubtarget>();

  TII = ST->getInstrInfo();
  TRI = &TII->getRegisterInfo();
  MRI = &MF.getRegInfo();
  LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  auto *MDTWrapper = getAnalysisIfAvailable<MachineDominatorTreeWrapperPass>();
  MDT = MDTWrapper ? &MDTWrapper->getDomTree() : nullptr;
  auto *PDTWrapper =
      getAnalysisIfAvailable<MachinePostDominatorTreeWrapperPass>();
  PDT = PDTWrapper ? &PDTWrapper->getPostDomTree() : nullptr;

  if (ST->isWave32()) {
    AndOpc = AMDGPU::S_AND_B32;
    AndTermOpc = AMDGPU::S_AND_B32_term;
    AndN2Opc = AMDGPU::S_ANDN2_B32;
    XorOpc = AMDGPU::S_XOR_B32;
    AndSaveExecOpc = AMDGPU::S_AND_SAVEEXEC_B32;
    AndSaveExecTermOpc = AMDGPU::S_AND_SAVEEXEC_B32_term;
    WQMOpc = AMDGPU::S_WQM_B32;
    Exec = AMDGPU::EXEC_LO;
  } else {
    AndOpc = AMDGPU::S_AND_B64;
    AndTermOpc = AMDGPU::S_AND_B64_term;
    AndN2Opc = AMDGPU::S_ANDN2_B64;
    XorOpc = AMDGPU::S_XOR_B64;
    AndSaveExecOpc = AMDGPU::S_AND_SAVEEXEC_B64;
    AndSaveExecTermOpc = AMDGPU::S_AND_SAVEEXEC_B64_term;
    WQMOpc = AMDGPU::S_WQM_B64;
    Exec = AMDGPU::EXEC;
  }

  const char GlobalFlags = analyzeFunction(MF);
  bool Changed = false;

  LiveMaskReg = Exec;

  MachineBasicBlock &Entry = MF.front();
  MachineBasicBlock::iterator EntryMI = lowerInitExecInstrs(Entry, Changed);

  // Store a copy of the original live mask when required
  const bool HasLiveMaskQueries = !LiveMaskQueries.empty();
  const bool HasWaveModes = GlobalFlags & ~StateExact;
  const bool HasKills = !KillInstrs.empty();
  const bool UsesWQM = GlobalFlags & StateWQM;
  if (HasKills || UsesWQM || (HasWaveModes && HasLiveMaskQueries)) {
    LiveMaskReg = MRI->createVirtualRegister(TRI->getBoolRC());
    MachineInstr *MI =
        BuildMI(Entry, EntryMI, DebugLoc(), TII->get(AMDGPU::COPY), LiveMaskReg)
            .addReg(Exec);
    LIS->InsertMachineInstrInMaps(*MI);
    Changed = true;
  }

  LLVM_DEBUG(printInfo());

  Changed |= lowerLiveMaskQueries();
  Changed |= lowerCopyInstrs();

  if (!HasWaveModes) {
    // No wave mode execution
    Changed |= lowerKillInstrs(false);
  } else if (GlobalFlags == StateWQM) {
    // Shader only needs WQM
    auto MI = BuildMI(Entry, EntryMI, DebugLoc(), TII->get(WQMOpc), Exec)
                  .addReg(Exec);
    LIS->InsertMachineInstrInMaps(*MI);
    lowerKillInstrs(true);
    Changed = true;
  } else {
    // Wave mode switching requires full lowering pass.
    for (auto BII : Blocks)
      processBlock(*BII.first, BII.first == &Entry);
    // Lowering blocks causes block splitting so perform as a second pass.
    for (auto BII : Blocks)
      lowerBlock(*BII.first);
    Changed = true;
  }

  // Compute live range for live mask
  if (LiveMaskReg != Exec)
    LIS->createAndComputeVirtRegInterval(LiveMaskReg);

  // Physical registers like SCC aren't tracked by default anyway, so just
  // removing the ranges we computed is the simplest option for maintaining
  // the analysis results.
  LIS->removeAllRegUnitsForPhysReg(AMDGPU::SCC);

  // If we performed any kills then recompute EXEC
  if (!KillInstrs.empty() || !InitExecInstrs.empty())
    LIS->removeAllRegUnitsForPhysReg(AMDGPU::EXEC);

  return Changed;
}
