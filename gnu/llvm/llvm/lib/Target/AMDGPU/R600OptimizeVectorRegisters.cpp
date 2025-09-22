//===- R600MergeVectorRegisters.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass merges inputs of swizzeable instructions into vector sharing
/// common data and/or have enough undef subreg using swizzle abilities.
///
/// For instance let's consider the following pseudo code :
/// %5 = REG_SEQ %1, sub0, %2, sub1, %3, sub2, undef, sub3
/// ...
/// %7 = REG_SEQ %1, sub0, %3, sub1, undef, sub2, %4, sub3
/// (swizzable Inst) %7, SwizzleMask : sub0, sub1, sub2, sub3
///
/// is turned into :
/// %5 = REG_SEQ %1, sub0, %2, sub1, %3, sub2, undef, sub3
/// ...
/// %7 = INSERT_SUBREG %4, sub3
/// (swizzable Inst) %7, SwizzleMask : sub0, sub2, sub1, sub3
///
/// This allow regalloc to reduce register pressure for vector registers and
/// to reduce MOV count.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600.h"
#include "R600Defines.h"
#include "R600Subtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"

using namespace llvm;

#define DEBUG_TYPE "vec-merger"

static bool isImplicitlyDef(MachineRegisterInfo &MRI, Register Reg) {
  if (Reg.isPhysical())
    return false;
  const MachineInstr *MI = MRI.getUniqueVRegDef(Reg);
  return MI && MI->isImplicitDef();
}

namespace {

class RegSeqInfo {
public:
  MachineInstr *Instr;
  DenseMap<Register, unsigned> RegToChan;
  std::vector<Register> UndefReg;

  RegSeqInfo(MachineRegisterInfo &MRI, MachineInstr *MI) : Instr(MI) {
    assert(MI->getOpcode() == R600::REG_SEQUENCE);
    for (unsigned i = 1, e = Instr->getNumOperands(); i < e; i+=2) {
      MachineOperand &MO = Instr->getOperand(i);
      unsigned Chan = Instr->getOperand(i + 1).getImm();
      if (isImplicitlyDef(MRI, MO.getReg()))
        UndefReg.emplace_back(Chan);
      else
        RegToChan[MO.getReg()] = Chan;
    }
  }

  RegSeqInfo() = default;

  bool operator==(const RegSeqInfo &RSI) const {
    return RSI.Instr == Instr;
  }
};

class R600VectorRegMerger : public MachineFunctionPass {
private:
  using InstructionSetMap = DenseMap<unsigned, std::vector<MachineInstr *>>;

  MachineRegisterInfo *MRI;
  const R600InstrInfo *TII = nullptr;
  DenseMap<MachineInstr *, RegSeqInfo> PreviousRegSeq;
  InstructionSetMap PreviousRegSeqByReg;
  InstructionSetMap PreviousRegSeqByUndefCount;

  bool canSwizzle(const MachineInstr &MI) const;
  bool areAllUsesSwizzeable(Register Reg) const;
  void SwizzleInput(MachineInstr &,
      const std::vector<std::pair<unsigned, unsigned>> &RemapChan) const;
  bool tryMergeVector(const RegSeqInfo *Untouched, RegSeqInfo *ToMerge,
      std::vector<std::pair<unsigned, unsigned>> &Remap) const;
  bool tryMergeUsingCommonSlot(RegSeqInfo &RSI, RegSeqInfo &CompatibleRSI,
      std::vector<std::pair<unsigned, unsigned>> &RemapChan);
  bool tryMergeUsingFreeSlot(RegSeqInfo &RSI, RegSeqInfo &CompatibleRSI,
      std::vector<std::pair<unsigned, unsigned>> &RemapChan);
  MachineInstr *RebuildVector(RegSeqInfo *MI, const RegSeqInfo *BaseVec,
      const std::vector<std::pair<unsigned, unsigned>> &RemapChan) const;
  void RemoveMI(MachineInstr *);
  void trackRSI(const RegSeqInfo &RSI);

public:
  static char ID;

  R600VectorRegMerger() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties()
      .set(MachineFunctionProperties::Property::IsSSA);
  }

  StringRef getPassName() const override {
    return "R600 Vector Registers Merge Pass";
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;
};

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(R600VectorRegMerger, DEBUG_TYPE,
                     "R600 Vector Reg Merger", false, false)
INITIALIZE_PASS_END(R600VectorRegMerger, DEBUG_TYPE,
                    "R600 Vector Reg Merger", false, false)

char R600VectorRegMerger::ID = 0;

char &llvm::R600VectorRegMergerID = R600VectorRegMerger::ID;

bool R600VectorRegMerger::canSwizzle(const MachineInstr &MI)
    const {
  if (TII->get(MI.getOpcode()).TSFlags & R600_InstFlag::TEX_INST)
    return true;
  switch (MI.getOpcode()) {
  case R600::R600_ExportSwz:
  case R600::EG_ExportSwz:
    return true;
  default:
    return false;
  }
}

bool R600VectorRegMerger::tryMergeVector(const RegSeqInfo *Untouched,
    RegSeqInfo *ToMerge, std::vector< std::pair<unsigned, unsigned>> &Remap)
    const {
  unsigned CurrentUndexIdx = 0;
  for (auto &It : ToMerge->RegToChan) {
    DenseMap<Register, unsigned>::const_iterator PosInUntouched =
        Untouched->RegToChan.find(It.first);
    if (PosInUntouched != Untouched->RegToChan.end()) {
      Remap.emplace_back(It.second, (*PosInUntouched).second);
      continue;
    }
    if (CurrentUndexIdx >= Untouched->UndefReg.size())
      return false;
    Remap.emplace_back(It.second, Untouched->UndefReg[CurrentUndexIdx++]);
  }

  return true;
}

static
unsigned getReassignedChan(
    const std::vector<std::pair<unsigned, unsigned>> &RemapChan,
    unsigned Chan) {
  for (const auto &J : RemapChan) {
    if (J.first == Chan)
      return J.second;
  }
  llvm_unreachable("Chan wasn't reassigned");
}

MachineInstr *R600VectorRegMerger::RebuildVector(
    RegSeqInfo *RSI, const RegSeqInfo *BaseRSI,
    const std::vector<std::pair<unsigned, unsigned>> &RemapChan) const {
  Register Reg = RSI->Instr->getOperand(0).getReg();
  MachineBasicBlock::iterator Pos = RSI->Instr;
  MachineBasicBlock &MBB = *Pos->getParent();
  DebugLoc DL = Pos->getDebugLoc();

  Register SrcVec = BaseRSI->Instr->getOperand(0).getReg();
  DenseMap<Register, unsigned> UpdatedRegToChan = BaseRSI->RegToChan;
  std::vector<Register> UpdatedUndef = BaseRSI->UndefReg;
  for (const auto &It : RSI->RegToChan) {
    Register DstReg = MRI->createVirtualRegister(&R600::R600_Reg128RegClass);
    unsigned SubReg = It.first;
    unsigned Swizzle = It.second;
    unsigned Chan = getReassignedChan(RemapChan, Swizzle);

    MachineInstr *Tmp = BuildMI(MBB, Pos, DL, TII->get(R600::INSERT_SUBREG),
        DstReg)
        .addReg(SrcVec)
        .addReg(SubReg)
        .addImm(Chan);
    UpdatedRegToChan[SubReg] = Chan;
    std::vector<Register>::iterator ChanPos = llvm::find(UpdatedUndef, Chan);
    if (ChanPos != UpdatedUndef.end())
      UpdatedUndef.erase(ChanPos);
    assert(!is_contained(UpdatedUndef, Chan) &&
           "UpdatedUndef shouldn't contain Chan more than once!");
    LLVM_DEBUG(dbgs() << "    ->"; Tmp->dump(););
    (void)Tmp;
    SrcVec = DstReg;
  }
  MachineInstr *NewMI =
      BuildMI(MBB, Pos, DL, TII->get(R600::COPY), Reg).addReg(SrcVec);
  LLVM_DEBUG(dbgs() << "    ->"; NewMI->dump(););

  LLVM_DEBUG(dbgs() << "  Updating Swizzle:\n");
  for (MachineRegisterInfo::use_instr_iterator It = MRI->use_instr_begin(Reg),
      E = MRI->use_instr_end(); It != E; ++It) {
    LLVM_DEBUG(dbgs() << "    "; (*It).dump(); dbgs() << "    ->");
    SwizzleInput(*It, RemapChan);
    LLVM_DEBUG((*It).dump());
  }
  RSI->Instr->eraseFromParent();

  // Update RSI
  RSI->Instr = NewMI;
  RSI->RegToChan = UpdatedRegToChan;
  RSI->UndefReg = UpdatedUndef;

  return NewMI;
}

void R600VectorRegMerger::RemoveMI(MachineInstr *MI) {
  for (auto &It : PreviousRegSeqByReg) {
    std::vector<MachineInstr *> &MIs = It.second;
    MIs.erase(llvm::find(MIs, MI), MIs.end());
  }
  for (auto &It : PreviousRegSeqByUndefCount) {
    std::vector<MachineInstr *> &MIs = It.second;
    MIs.erase(llvm::find(MIs, MI), MIs.end());
  }
}

void R600VectorRegMerger::SwizzleInput(MachineInstr &MI,
    const std::vector<std::pair<unsigned, unsigned>> &RemapChan) const {
  unsigned Offset;
  if (TII->get(MI.getOpcode()).TSFlags & R600_InstFlag::TEX_INST)
    Offset = 2;
  else
    Offset = 3;
  for (unsigned i = 0; i < 4; i++) {
    unsigned Swizzle = MI.getOperand(i + Offset).getImm() + 1;
    for (const auto &J : RemapChan) {
      if (J.first == Swizzle) {
        MI.getOperand(i + Offset).setImm(J.second - 1);
        break;
      }
    }
  }
}

bool R600VectorRegMerger::areAllUsesSwizzeable(Register Reg) const {
  return llvm::all_of(MRI->use_instructions(Reg),
                      [&](const MachineInstr &MI) { return canSwizzle(MI); });
}

bool R600VectorRegMerger::tryMergeUsingCommonSlot(RegSeqInfo &RSI,
    RegSeqInfo &CompatibleRSI,
    std::vector<std::pair<unsigned, unsigned>> &RemapChan) {
  for (MachineInstr::mop_iterator MOp = RSI.Instr->operands_begin(),
      MOE = RSI.Instr->operands_end(); MOp != MOE; ++MOp) {
    if (!MOp->isReg())
      continue;
    if (PreviousRegSeqByReg[MOp->getReg()].empty())
      continue;
    for (MachineInstr *MI : PreviousRegSeqByReg[MOp->getReg()]) {
      CompatibleRSI = PreviousRegSeq[MI];
      if (RSI == CompatibleRSI)
        continue;
      if (tryMergeVector(&CompatibleRSI, &RSI, RemapChan))
        return true;
    }
  }
  return false;
}

bool R600VectorRegMerger::tryMergeUsingFreeSlot(RegSeqInfo &RSI,
    RegSeqInfo &CompatibleRSI,
    std::vector<std::pair<unsigned, unsigned>> &RemapChan) {
  unsigned NeededUndefs = 4 - RSI.UndefReg.size();
  if (PreviousRegSeqByUndefCount[NeededUndefs].empty())
    return false;
  std::vector<MachineInstr *> &MIs =
      PreviousRegSeqByUndefCount[NeededUndefs];
  CompatibleRSI = PreviousRegSeq[MIs.back()];
  tryMergeVector(&CompatibleRSI, &RSI, RemapChan);
  return true;
}

void R600VectorRegMerger::trackRSI(const RegSeqInfo &RSI) {
  for (DenseMap<Register, unsigned>::const_iterator
  It = RSI.RegToChan.begin(), E = RSI.RegToChan.end(); It != E; ++It) {
    PreviousRegSeqByReg[(*It).first].push_back(RSI.Instr);
  }
  PreviousRegSeqByUndefCount[RSI.UndefReg.size()].push_back(RSI.Instr);
  PreviousRegSeq[RSI.Instr] = RSI;
}

bool R600VectorRegMerger::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  const R600Subtarget &ST = Fn.getSubtarget<R600Subtarget>();
  TII = ST.getInstrInfo();
  MRI = &Fn.getRegInfo();

  for (MachineBasicBlock &MB : Fn) {
    PreviousRegSeq.clear();
    PreviousRegSeqByReg.clear();
    PreviousRegSeqByUndefCount.clear();

    for (MachineBasicBlock::iterator MII = MB.begin(), MIIE = MB.end();
         MII != MIIE; ++MII) {
      MachineInstr &MI = *MII;
      if (MI.getOpcode() != R600::REG_SEQUENCE) {
        if (TII->get(MI.getOpcode()).TSFlags & R600_InstFlag::TEX_INST) {
          Register Reg = MI.getOperand(1).getReg();
          for (MachineRegisterInfo::def_instr_iterator
               It = MRI->def_instr_begin(Reg), E = MRI->def_instr_end();
               It != E; ++It) {
            RemoveMI(&(*It));
          }
        }
        continue;
      }

      RegSeqInfo RSI(*MRI, &MI);

      // All uses of MI are swizzeable ?
      Register Reg = MI.getOperand(0).getReg();
      if (!areAllUsesSwizzeable(Reg))
        continue;

      LLVM_DEBUG({
        dbgs() << "Trying to optimize ";
        MI.dump();
      });

      RegSeqInfo CandidateRSI;
      std::vector<std::pair<unsigned, unsigned>> RemapChan;
      LLVM_DEBUG(dbgs() << "Using common slots...\n";);
      if (tryMergeUsingCommonSlot(RSI, CandidateRSI, RemapChan)) {
        // Remove CandidateRSI mapping
        RemoveMI(CandidateRSI.Instr);
        MII = RebuildVector(&RSI, &CandidateRSI, RemapChan);
        trackRSI(RSI);
        continue;
      }
      LLVM_DEBUG(dbgs() << "Using free slots...\n";);
      RemapChan.clear();
      if (tryMergeUsingFreeSlot(RSI, CandidateRSI, RemapChan)) {
        RemoveMI(CandidateRSI.Instr);
        MII = RebuildVector(&RSI, &CandidateRSI, RemapChan);
        trackRSI(RSI);
        continue;
      }
      //Failed to merge
      trackRSI(RSI);
    }
  }
  return false;
}

llvm::FunctionPass *llvm::createR600VectorRegMerger() {
  return new R600VectorRegMerger();
}
