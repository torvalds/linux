//===- HexagonExpandCondsets.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Replace mux instructions with the corresponding legal instructions.
// It is meant to work post-SSA, but still on virtual registers. It was
// originally placed between register coalescing and machine instruction
// scheduler.
// In this place in the optimization sequence, live interval analysis had
// been performed, and the live intervals should be preserved. A large part
// of the code deals with preserving the liveness information.
//
// Liveness tracking aside, the main functionality of this pass is divided
// into two steps. The first step is to replace an instruction
//   %0 = C2_mux %1, %2, %3
// with a pair of conditional transfers
//   %0 = A2_tfrt %1, %2
//   %0 = A2_tfrf %1, %3
// It is the intention that the execution of this pass could be terminated
// after this step, and the code generated would be functionally correct.
//
// If the uses of the source values %1 and %2 are kills, and their
// definitions are predicable, then in the second step, the conditional
// transfers will then be rewritten as predicated instructions. E.g.
//   %0 = A2_or %1, %2
//   %3 = A2_tfrt %99, killed %0
// will be rewritten as
//   %3 = A2_port %99, %1, %2
//
// This replacement has two variants: "up" and "down". Consider this case:
//   %0 = A2_or %1, %2
//   ... [intervening instructions] ...
//   %3 = A2_tfrt %99, killed %0
// variant "up":
//   %3 = A2_port %99, %1, %2
//   ... [intervening instructions, %0->vreg3] ...
//   [deleted]
// variant "down":
//   [deleted]
//   ... [intervening instructions] ...
//   %3 = A2_port %99, %1, %2
//
// Both, one or none of these variants may be valid, and checks are made
// to rule out inapplicable variants.
//
// As an additional optimization, before either of the two steps above is
// executed, the pass attempts to coalesce the target register with one of
// the source registers, e.g. given an instruction
//   %3 = C2_mux %0, %1, %2
// %3 will be coalesced with either %1 or %2. If this succeeds,
// the instruction would then be (for example)
//   %3 = C2_mux %0, %3, %2
// and, under certain circumstances, this could result in only one predicated
// instruction:
//   %3 = A2_tfrf %0, %2
//

// Splitting a definition of a register into two predicated transfers
// creates a complication in liveness tracking. Live interval computation
// will see both instructions as actual definitions, and will mark the
// first one as dead. The definition is not actually dead, and this
// situation will need to be fixed. For example:
//   dead %1 = A2_tfrt ...  ; marked as dead
//   %1 = A2_tfrf ...
//
// Since any of the individual predicated transfers may end up getting
// removed (in case it is an identity copy), some pre-existing def may
// be marked as dead after live interval recomputation:
//   dead %1 = ...          ; marked as dead
//   ...
//   %1 = A2_tfrf ...       ; if A2_tfrt is removed
// This case happens if %1 was used as a source in A2_tfrt, which means
// that is it actually live at the A2_tfrf, and so the now dead definition
// of %1 will need to be updated to non-dead at some point.
//
// This issue could be remedied by adding implicit uses to the predicated
// transfers, but this will create a problem with subsequent predication,
// since the transfers will no longer be possible to reorder. To avoid
// that, the initial splitting will not add any implicit uses. These
// implicit uses will be added later, after predication. The extra price,
// however, is that finding the locations where the implicit uses need
// to be added, and updating the live ranges will be more involved.

#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <map>
#include <set>
#include <utility>

#define DEBUG_TYPE "expand-condsets"

using namespace llvm;

static cl::opt<unsigned> OptTfrLimit("expand-condsets-tfr-limit",
  cl::init(~0U), cl::Hidden, cl::desc("Max number of mux expansions"));
static cl::opt<unsigned> OptCoaLimit("expand-condsets-coa-limit",
  cl::init(~0U), cl::Hidden, cl::desc("Max number of segment coalescings"));

namespace llvm {

  void initializeHexagonExpandCondsetsPass(PassRegistry&);
  FunctionPass *createHexagonExpandCondsets();

} // end namespace llvm

namespace {

  class HexagonExpandCondsets : public MachineFunctionPass {
  public:
    static char ID;

    HexagonExpandCondsets() : MachineFunctionPass(ID) {
      if (OptCoaLimit.getPosition())
        CoaLimitActive = true, CoaLimit = OptCoaLimit;
      if (OptTfrLimit.getPosition())
        TfrLimitActive = true, TfrLimit = OptTfrLimit;
      initializeHexagonExpandCondsetsPass(*PassRegistry::getPassRegistry());
    }

    StringRef getPassName() const override { return "Hexagon Expand Condsets"; }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LiveIntervalsWrapperPass>();
      AU.addPreserved<LiveIntervalsWrapperPass>();
      AU.addPreserved<SlotIndexesWrapperPass>();
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addPreserved<MachineDominatorTreeWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    const HexagonInstrInfo *HII = nullptr;
    const TargetRegisterInfo *TRI = nullptr;
    MachineDominatorTree *MDT;
    MachineRegisterInfo *MRI = nullptr;
    LiveIntervals *LIS = nullptr;
    bool CoaLimitActive = false;
    bool TfrLimitActive = false;
    unsigned CoaLimit;
    unsigned TfrLimit;
    unsigned CoaCounter = 0;
    unsigned TfrCounter = 0;

    // FIXME: Consolidate duplicate definitions of RegisterRef
    struct RegisterRef {
      RegisterRef(const MachineOperand &Op) : Reg(Op.getReg()),
          Sub(Op.getSubReg()) {}
      RegisterRef(unsigned R = 0, unsigned S = 0) : Reg(R), Sub(S) {}

      bool operator== (RegisterRef RR) const {
        return Reg == RR.Reg && Sub == RR.Sub;
      }
      bool operator!= (RegisterRef RR) const { return !operator==(RR); }
      bool operator< (RegisterRef RR) const {
        return Reg < RR.Reg || (Reg == RR.Reg && Sub < RR.Sub);
      }

      Register Reg;
      unsigned Sub;
    };

    using ReferenceMap = DenseMap<unsigned, unsigned>;
    enum { Sub_Low = 0x1, Sub_High = 0x2, Sub_None = (Sub_Low | Sub_High) };
    enum { Exec_Then = 0x10, Exec_Else = 0x20 };

    unsigned getMaskForSub(unsigned Sub);
    bool isCondset(const MachineInstr &MI);
    LaneBitmask getLaneMask(Register Reg, unsigned Sub);

    void addRefToMap(RegisterRef RR, ReferenceMap &Map, unsigned Exec);
    bool isRefInMap(RegisterRef, ReferenceMap &Map, unsigned Exec);

    void updateDeadsInRange(Register Reg, LaneBitmask LM, LiveRange &Range);
    void updateKillFlags(Register Reg);
    void updateDeadFlags(Register Reg);
    void recalculateLiveInterval(Register Reg);
    void removeInstr(MachineInstr &MI);
    void updateLiveness(const std::set<Register> &RegSet, bool Recalc,
                        bool UpdateKills, bool UpdateDeads);
    void distributeLiveIntervals(const std::set<Register> &Regs);

    unsigned getCondTfrOpcode(const MachineOperand &SO, bool Cond);
    MachineInstr *genCondTfrFor(MachineOperand &SrcOp,
        MachineBasicBlock::iterator At, unsigned DstR,
        unsigned DstSR, const MachineOperand &PredOp, bool PredSense,
        bool ReadUndef, bool ImpUse);
    bool split(MachineInstr &MI, std::set<Register> &UpdRegs);

    bool isPredicable(MachineInstr *MI);
    MachineInstr *getReachingDefForPred(RegisterRef RD,
        MachineBasicBlock::iterator UseIt, unsigned PredR, bool Cond);
    bool canMoveOver(MachineInstr &MI, ReferenceMap &Defs, ReferenceMap &Uses);
    bool canMoveMemTo(MachineInstr &MI, MachineInstr &ToI, bool IsDown);
    void predicateAt(const MachineOperand &DefOp, MachineInstr &MI,
                     MachineBasicBlock::iterator Where,
                     const MachineOperand &PredOp, bool Cond,
                     std::set<Register> &UpdRegs);
    void renameInRange(RegisterRef RO, RegisterRef RN, unsigned PredR,
        bool Cond, MachineBasicBlock::iterator First,
        MachineBasicBlock::iterator Last);
    bool predicate(MachineInstr &TfrI, bool Cond, std::set<Register> &UpdRegs);
    bool predicateInBlock(MachineBasicBlock &B, std::set<Register> &UpdRegs);

    bool isIntReg(RegisterRef RR, unsigned &BW);
    bool isIntraBlocks(LiveInterval &LI);
    bool coalesceRegisters(RegisterRef R1, RegisterRef R2);
    bool coalesceSegments(const SmallVectorImpl<MachineInstr *> &Condsets,
                          std::set<Register> &UpdRegs);
  };

} // end anonymous namespace

char HexagonExpandCondsets::ID = 0;

namespace llvm {

  char &HexagonExpandCondsetsID = HexagonExpandCondsets::ID;

} // end namespace llvm

INITIALIZE_PASS_BEGIN(HexagonExpandCondsets, "expand-condsets",
  "Hexagon Expand Condsets", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_END(HexagonExpandCondsets, "expand-condsets",
  "Hexagon Expand Condsets", false, false)

unsigned HexagonExpandCondsets::getMaskForSub(unsigned Sub) {
  switch (Sub) {
    case Hexagon::isub_lo:
    case Hexagon::vsub_lo:
      return Sub_Low;
    case Hexagon::isub_hi:
    case Hexagon::vsub_hi:
      return Sub_High;
    case Hexagon::NoSubRegister:
      return Sub_None;
  }
  llvm_unreachable("Invalid subregister");
}

bool HexagonExpandCondsets::isCondset(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::C2_mux:
    case Hexagon::C2_muxii:
    case Hexagon::C2_muxir:
    case Hexagon::C2_muxri:
    case Hexagon::PS_pselect:
        return true;
      break;
  }
  return false;
}

LaneBitmask HexagonExpandCondsets::getLaneMask(Register Reg, unsigned Sub) {
  assert(Reg.isVirtual());
  return Sub != 0 ? TRI->getSubRegIndexLaneMask(Sub)
                  : MRI->getMaxLaneMaskForVReg(Reg);
}

void HexagonExpandCondsets::addRefToMap(RegisterRef RR, ReferenceMap &Map,
      unsigned Exec) {
  unsigned Mask = getMaskForSub(RR.Sub) | Exec;
  ReferenceMap::iterator F = Map.find(RR.Reg);
  if (F == Map.end())
    Map.insert(std::make_pair(RR.Reg, Mask));
  else
    F->second |= Mask;
}

bool HexagonExpandCondsets::isRefInMap(RegisterRef RR, ReferenceMap &Map,
      unsigned Exec) {
  ReferenceMap::iterator F = Map.find(RR.Reg);
  if (F == Map.end())
    return false;
  unsigned Mask = getMaskForSub(RR.Sub) | Exec;
  if (Mask & F->second)
    return true;
  return false;
}

void HexagonExpandCondsets::updateKillFlags(Register Reg) {
  auto KillAt = [this,Reg] (SlotIndex K, LaneBitmask LM) -> void {
    // Set the <kill> flag on a use of Reg whose lane mask is contained in LM.
    MachineInstr *MI = LIS->getInstructionFromIndex(K);
    for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
      MachineOperand &Op = MI->getOperand(i);
      if (!Op.isReg() || !Op.isUse() || Op.getReg() != Reg ||
          MI->isRegTiedToDefOperand(i))
        continue;
      LaneBitmask SLM = getLaneMask(Reg, Op.getSubReg());
      if ((SLM & LM) == SLM) {
        // Only set the kill flag on the first encountered use of Reg in this
        // instruction.
        Op.setIsKill(true);
        break;
      }
    }
  };

  LiveInterval &LI = LIS->getInterval(Reg);
  for (auto I = LI.begin(), E = LI.end(); I != E; ++I) {
    if (!I->end.isRegister())
      continue;
    // Do not mark the end of the segment as <kill>, if the next segment
    // starts with a predicated instruction.
    auto NextI = std::next(I);
    if (NextI != E && NextI->start.isRegister()) {
      MachineInstr *DefI = LIS->getInstructionFromIndex(NextI->start);
      if (HII->isPredicated(*DefI))
        continue;
    }
    bool WholeReg = true;
    if (LI.hasSubRanges()) {
      auto EndsAtI = [I] (LiveInterval::SubRange &S) -> bool {
        LiveRange::iterator F = S.find(I->end);
        return F != S.end() && I->end == F->end;
      };
      // Check if all subranges end at I->end. If so, make sure to kill
      // the whole register.
      for (LiveInterval::SubRange &S : LI.subranges()) {
        if (EndsAtI(S))
          KillAt(I->end, S.LaneMask);
        else
          WholeReg = false;
      }
    }
    if (WholeReg)
      KillAt(I->end, MRI->getMaxLaneMaskForVReg(Reg));
  }
}

void HexagonExpandCondsets::updateDeadsInRange(Register Reg, LaneBitmask LM,
                                               LiveRange &Range) {
  assert(Reg.isVirtual());
  if (Range.empty())
    return;

  // Return two booleans: { def-modifes-reg, def-covers-reg }.
  auto IsRegDef = [this,Reg,LM] (MachineOperand &Op) -> std::pair<bool,bool> {
    if (!Op.isReg() || !Op.isDef())
      return { false, false };
    Register DR = Op.getReg(), DSR = Op.getSubReg();
    if (!DR.isVirtual() || DR != Reg)
      return { false, false };
    LaneBitmask SLM = getLaneMask(DR, DSR);
    LaneBitmask A = SLM & LM;
    return { A.any(), A == SLM };
  };

  // The splitting step will create pairs of predicated definitions without
  // any implicit uses (since implicit uses would interfere with predication).
  // This can cause the reaching defs to become dead after live range
  // recomputation, even though they are not really dead.
  // We need to identify predicated defs that need implicit uses, and
  // dead defs that are not really dead, and correct both problems.

  auto Dominate = [this] (SetVector<MachineBasicBlock*> &Defs,
                          MachineBasicBlock *Dest) -> bool {
    for (MachineBasicBlock *D : Defs) {
      if (D != Dest && MDT->dominates(D, Dest))
        return true;
    }
    MachineBasicBlock *Entry = &Dest->getParent()->front();
    SetVector<MachineBasicBlock*> Work(Dest->pred_begin(), Dest->pred_end());
    for (unsigned i = 0; i < Work.size(); ++i) {
      MachineBasicBlock *B = Work[i];
      if (Defs.count(B))
        continue;
      if (B == Entry)
        return false;
      for (auto *P : B->predecessors())
        Work.insert(P);
    }
    return true;
  };

  // First, try to extend live range within individual basic blocks. This
  // will leave us only with dead defs that do not reach any predicated
  // defs in the same block.
  SetVector<MachineBasicBlock*> Defs;
  SmallVector<SlotIndex,4> PredDefs;
  for (auto &Seg : Range) {
    if (!Seg.start.isRegister())
      continue;
    MachineInstr *DefI = LIS->getInstructionFromIndex(Seg.start);
    Defs.insert(DefI->getParent());
    if (HII->isPredicated(*DefI))
      PredDefs.push_back(Seg.start);
  }

  SmallVector<SlotIndex,8> Undefs;
  LiveInterval &LI = LIS->getInterval(Reg);
  LI.computeSubRangeUndefs(Undefs, LM, *MRI, *LIS->getSlotIndexes());

  for (auto &SI : PredDefs) {
    MachineBasicBlock *BB = LIS->getMBBFromIndex(SI);
    auto P = Range.extendInBlock(Undefs, LIS->getMBBStartIdx(BB), SI);
    if (P.first != nullptr || P.second)
      SI = SlotIndex();
  }

  // Calculate reachability for those predicated defs that were not handled
  // by the in-block extension.
  SmallVector<SlotIndex,4> ExtTo;
  for (auto &SI : PredDefs) {
    if (!SI.isValid())
      continue;
    MachineBasicBlock *BB = LIS->getMBBFromIndex(SI);
    if (BB->pred_empty())
      continue;
    // If the defs from this range reach SI via all predecessors, it is live.
    // It can happen that SI is reached by the defs through some paths, but
    // not all. In the IR coming into this optimization, SI would not be
    // considered live, since the defs would then not jointly dominate SI.
    // That means that SI is an overwriting def, and no implicit use is
    // needed at this point. Do not add SI to the extension points, since
    // extendToIndices will abort if there is no joint dominance.
    // If the abort was avoided by adding extra undefs added to Undefs,
    // extendToIndices could actually indicate that SI is live, contrary
    // to the original IR.
    if (Dominate(Defs, BB))
      ExtTo.push_back(SI);
  }

  if (!ExtTo.empty())
    LIS->extendToIndices(Range, ExtTo, Undefs);

  // Remove <dead> flags from all defs that are not dead after live range
  // extension, and collect all def operands. They will be used to generate
  // the necessary implicit uses.
  // At the same time, add <dead> flag to all defs that are actually dead.
  // This can happen, for example, when a mux with identical inputs is
  // replaced with a COPY: the use of the predicate register disappears and
  // the dead can become dead.
  std::set<RegisterRef> DefRegs;
  for (auto &Seg : Range) {
    if (!Seg.start.isRegister())
      continue;
    MachineInstr *DefI = LIS->getInstructionFromIndex(Seg.start);
    for (auto &Op : DefI->operands()) {
      auto P = IsRegDef(Op);
      if (P.second && Seg.end.isDead()) {
        Op.setIsDead(true);
      } else if (P.first) {
        DefRegs.insert(Op);
        Op.setIsDead(false);
      }
    }
  }

  // Now, add implicit uses to each predicated def that is reached
  // by other defs.
  for (auto &Seg : Range) {
    if (!Seg.start.isRegister() || !Range.liveAt(Seg.start.getPrevSlot()))
      continue;
    MachineInstr *DefI = LIS->getInstructionFromIndex(Seg.start);
    if (!HII->isPredicated(*DefI))
      continue;
    // Construct the set of all necessary implicit uses, based on the def
    // operands in the instruction. We need to tie the implicit uses to
    // the corresponding defs.
    std::map<RegisterRef,unsigned> ImpUses;
    for (unsigned i = 0, e = DefI->getNumOperands(); i != e; ++i) {
      MachineOperand &Op = DefI->getOperand(i);
      if (!Op.isReg() || !DefRegs.count(Op))
        continue;
      if (Op.isDef()) {
        // Tied defs will always have corresponding uses, so no extra
        // implicit uses are needed.
        if (!Op.isTied())
          ImpUses.insert({Op, i});
      } else {
        // This function can be called for the same register with different
        // lane masks. If the def in this instruction was for the whole
        // register, we can get here more than once. Avoid adding multiple
        // implicit uses (or adding an implicit use when an explicit one is
        // present).
        if (Op.isTied())
          ImpUses.erase(Op);
      }
    }
    if (ImpUses.empty())
      continue;
    MachineFunction &MF = *DefI->getParent()->getParent();
    for (auto [R, DefIdx] : ImpUses) {
      MachineInstrBuilder(MF, DefI).addReg(R.Reg, RegState::Implicit, R.Sub);
      DefI->tieOperands(DefIdx, DefI->getNumOperands()-1);
    }
  }
}

void HexagonExpandCondsets::updateDeadFlags(Register Reg) {
  LiveInterval &LI = LIS->getInterval(Reg);
  if (LI.hasSubRanges()) {
    for (LiveInterval::SubRange &S : LI.subranges()) {
      updateDeadsInRange(Reg, S.LaneMask, S);
      LIS->shrinkToUses(S, Reg);
    }
    LI.clear();
    LIS->constructMainRangeFromSubranges(LI);
  } else {
    updateDeadsInRange(Reg, MRI->getMaxLaneMaskForVReg(Reg), LI);
  }
}

void HexagonExpandCondsets::recalculateLiveInterval(Register Reg) {
  LIS->removeInterval(Reg);
  LIS->createAndComputeVirtRegInterval(Reg);
}

void HexagonExpandCondsets::removeInstr(MachineInstr &MI) {
  LIS->RemoveMachineInstrFromMaps(MI);
  MI.eraseFromParent();
}

void HexagonExpandCondsets::updateLiveness(const std::set<Register> &RegSet,
                                           bool Recalc, bool UpdateKills,
                                           bool UpdateDeads) {
  UpdateKills |= UpdateDeads;
  for (Register R : RegSet) {
    if (!R.isVirtual()) {
      assert(R.isPhysical());
      // There shouldn't be any physical registers as operands, except
      // possibly reserved registers.
      assert(MRI->isReserved(R));
      continue;
    }
    if (Recalc)
      recalculateLiveInterval(R);
    if (UpdateKills)
      MRI->clearKillFlags(R);
    if (UpdateDeads)
      updateDeadFlags(R);
    // Fixing <dead> flags may extend live ranges, so reset <kill> flags
    // after that.
    if (UpdateKills)
      updateKillFlags(R);
    LIS->getInterval(R).verify();
  }
}

void HexagonExpandCondsets::distributeLiveIntervals(
    const std::set<Register> &Regs) {
  ConnectedVNInfoEqClasses EQC(*LIS);
  for (Register R : Regs) {
    if (!R.isVirtual())
      continue;
    LiveInterval &LI = LIS->getInterval(R);
    unsigned NumComp = EQC.Classify(LI);
    if (NumComp == 1)
      continue;

    SmallVector<LiveInterval*> NewLIs;
    const TargetRegisterClass *RC = MRI->getRegClass(LI.reg());
    for (unsigned I = 1; I < NumComp; ++I) {
      Register NewR = MRI->createVirtualRegister(RC);
      NewLIs.push_back(&LIS->createEmptyInterval(NewR));
    }
    EQC.Distribute(LI, NewLIs.begin(), *MRI);
  }
}

/// Get the opcode for a conditional transfer of the value in SO (source
/// operand). The condition (true/false) is given in Cond.
unsigned HexagonExpandCondsets::getCondTfrOpcode(const MachineOperand &SO,
      bool IfTrue) {
  if (SO.isReg()) {
    MCRegister PhysR;
    RegisterRef RS = SO;
    if (RS.Reg.isVirtual()) {
      const TargetRegisterClass *VC = MRI->getRegClass(RS.Reg);
      assert(VC->begin() != VC->end() && "Empty register class");
      PhysR = *VC->begin();
    } else {
      PhysR = RS.Reg;
    }
    MCRegister PhysS = (RS.Sub == 0) ? PhysR : TRI->getSubReg(PhysR, RS.Sub);
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(PhysS);
    switch (TRI->getRegSizeInBits(*RC)) {
      case 32:
        return IfTrue ? Hexagon::A2_tfrt : Hexagon::A2_tfrf;
      case 64:
        return IfTrue ? Hexagon::A2_tfrpt : Hexagon::A2_tfrpf;
    }
    llvm_unreachable("Invalid register operand");
  }
  switch (SO.getType()) {
    case MachineOperand::MO_Immediate:
    case MachineOperand::MO_FPImmediate:
    case MachineOperand::MO_ConstantPoolIndex:
    case MachineOperand::MO_TargetIndex:
    case MachineOperand::MO_JumpTableIndex:
    case MachineOperand::MO_ExternalSymbol:
    case MachineOperand::MO_GlobalAddress:
    case MachineOperand::MO_BlockAddress:
      return IfTrue ? Hexagon::C2_cmoveit : Hexagon::C2_cmoveif;
    default:
      break;
  }
  llvm_unreachable("Unexpected source operand");
}

/// Generate a conditional transfer, copying the value SrcOp to the
/// destination register DstR:DstSR, and using the predicate register from
/// PredOp. The Cond argument specifies whether the predicate is to be
/// if(PredOp), or if(!PredOp).
MachineInstr *HexagonExpandCondsets::genCondTfrFor(MachineOperand &SrcOp,
      MachineBasicBlock::iterator At,
      unsigned DstR, unsigned DstSR, const MachineOperand &PredOp,
      bool PredSense, bool ReadUndef, bool ImpUse) {
  MachineInstr *MI = SrcOp.getParent();
  MachineBasicBlock &B = *At->getParent();
  const DebugLoc &DL = MI->getDebugLoc();

  // Don't avoid identity copies here (i.e. if the source and the destination
  // are the same registers). It is actually better to generate them here,
  // since this would cause the copy to potentially be predicated in the next
  // step. The predication will remove such a copy if it is unable to
  /// predicate.

  unsigned Opc = getCondTfrOpcode(SrcOp, PredSense);
  unsigned DstState = RegState::Define | (ReadUndef ? RegState::Undef : 0);
  unsigned PredState = getRegState(PredOp) & ~RegState::Kill;
  MachineInstrBuilder MIB;

  if (SrcOp.isReg()) {
    unsigned SrcState = getRegState(SrcOp);
    if (RegisterRef(SrcOp) == RegisterRef(DstR, DstSR))
      SrcState &= ~RegState::Kill;
    MIB = BuildMI(B, At, DL, HII->get(Opc))
            .addReg(DstR, DstState, DstSR)
            .addReg(PredOp.getReg(), PredState, PredOp.getSubReg())
            .addReg(SrcOp.getReg(), SrcState, SrcOp.getSubReg());
  } else {
    MIB = BuildMI(B, At, DL, HII->get(Opc))
            .addReg(DstR, DstState, DstSR)
            .addReg(PredOp.getReg(), PredState, PredOp.getSubReg())
            .add(SrcOp);
  }

  LLVM_DEBUG(dbgs() << "created an initial copy: " << *MIB);
  return &*MIB;
}

/// Replace a MUX instruction MI with a pair A2_tfrt/A2_tfrf. This function
/// performs all necessary changes to complete the replacement.
bool HexagonExpandCondsets::split(MachineInstr &MI,
                                  std::set<Register> &UpdRegs) {
  if (TfrLimitActive) {
    if (TfrCounter >= TfrLimit)
      return false;
    TfrCounter++;
  }
  LLVM_DEBUG(dbgs() << "\nsplitting " << printMBBReference(*MI.getParent())
                    << ": " << MI);
  MachineOperand &MD = MI.getOperand(0);  // Definition
  MachineOperand &MP = MI.getOperand(1);  // Predicate register
  assert(MD.isDef());
  Register DR = MD.getReg(), DSR = MD.getSubReg();
  bool ReadUndef = MD.isUndef();
  MachineBasicBlock::iterator At = MI;

  auto updateRegs = [&UpdRegs] (const MachineInstr &MI) -> void {
    for (auto &Op : MI.operands()) {
      if (Op.isReg())
        UpdRegs.insert(Op.getReg());
    }
  };

  // If this is a mux of the same register, just replace it with COPY.
  // Ideally, this would happen earlier, so that register coalescing would
  // see it.
  MachineOperand &ST = MI.getOperand(2);
  MachineOperand &SF = MI.getOperand(3);
  if (ST.isReg() && SF.isReg()) {
    RegisterRef RT(ST);
    if (RT == RegisterRef(SF)) {
      // Copy regs to update first.
      updateRegs(MI);
      MI.setDesc(HII->get(TargetOpcode::COPY));
      unsigned S = getRegState(ST);
      while (MI.getNumOperands() > 1)
        MI.removeOperand(MI.getNumOperands()-1);
      MachineFunction &MF = *MI.getParent()->getParent();
      MachineInstrBuilder(MF, MI).addReg(RT.Reg, S, RT.Sub);
      return true;
    }
  }

  // First, create the two invididual conditional transfers, and add each
  // of them to the live intervals information. Do that first and then remove
  // the old instruction from live intervals.
  MachineInstr *TfrT =
      genCondTfrFor(ST, At, DR, DSR, MP, true, ReadUndef, false);
  MachineInstr *TfrF =
      genCondTfrFor(SF, At, DR, DSR, MP, false, ReadUndef, true);
  LIS->InsertMachineInstrInMaps(*TfrT);
  LIS->InsertMachineInstrInMaps(*TfrF);

  // Will need to recalculate live intervals for all registers in MI.
  updateRegs(MI);

  removeInstr(MI);
  return true;
}

bool HexagonExpandCondsets::isPredicable(MachineInstr *MI) {
  if (HII->isPredicated(*MI) || !HII->isPredicable(*MI))
    return false;
  if (MI->hasUnmodeledSideEffects() || MI->mayStore())
    return false;
  // Reject instructions with multiple defs (e.g. post-increment loads).
  bool HasDef = false;
  for (auto &Op : MI->operands()) {
    if (!Op.isReg() || !Op.isDef())
      continue;
    if (HasDef)
      return false;
    HasDef = true;
  }
  for (auto &Mo : MI->memoperands()) {
    if (Mo->isVolatile() || Mo->isAtomic())
      return false;
  }
  return true;
}

/// Find the reaching definition for a predicated use of RD. The RD is used
/// under the conditions given by PredR and Cond, and this function will ignore
/// definitions that set RD under the opposite conditions.
MachineInstr *HexagonExpandCondsets::getReachingDefForPred(RegisterRef RD,
      MachineBasicBlock::iterator UseIt, unsigned PredR, bool Cond) {
  MachineBasicBlock &B = *UseIt->getParent();
  MachineBasicBlock::iterator I = UseIt, S = B.begin();
  if (I == S)
    return nullptr;

  bool PredValid = true;
  do {
    --I;
    MachineInstr *MI = &*I;
    // Check if this instruction can be ignored, i.e. if it is predicated
    // on the complementary condition.
    if (PredValid && HII->isPredicated(*MI)) {
      if (MI->readsRegister(PredR, /*TRI=*/nullptr) &&
          (Cond != HII->isPredicatedTrue(*MI)))
        continue;
    }

    // Check the defs. If the PredR is defined, invalidate it. If RD is
    // defined, return the instruction or 0, depending on the circumstances.
    for (auto &Op : MI->operands()) {
      if (!Op.isReg() || !Op.isDef())
        continue;
      RegisterRef RR = Op;
      if (RR.Reg == PredR) {
        PredValid = false;
        continue;
      }
      if (RR.Reg != RD.Reg)
        continue;
      // If the "Reg" part agrees, there is still the subregister to check.
      // If we are looking for %1:loreg, we can skip %1:hireg, but
      // not %1 (w/o subregisters).
      if (RR.Sub == RD.Sub)
        return MI;
      if (RR.Sub == 0 || RD.Sub == 0)
        return nullptr;
      // We have different subregisters, so we can continue looking.
    }
  } while (I != S);

  return nullptr;
}

/// Check if the instruction MI can be safely moved over a set of instructions
/// whose side-effects (in terms of register defs and uses) are expressed in
/// the maps Defs and Uses. These maps reflect the conditional defs and uses
/// that depend on the same predicate register to allow moving instructions
/// over instructions predicated on the opposite condition.
bool HexagonExpandCondsets::canMoveOver(MachineInstr &MI, ReferenceMap &Defs,
                                        ReferenceMap &Uses) {
  // In order to be able to safely move MI over instructions that define
  // "Defs" and use "Uses", no def operand from MI can be defined or used
  // and no use operand can be defined.
  for (auto &Op : MI.operands()) {
    if (!Op.isReg())
      continue;
    RegisterRef RR = Op;
    // For physical register we would need to check register aliases, etc.
    // and we don't want to bother with that. It would be of little value
    // before the actual register rewriting (from virtual to physical).
    if (!RR.Reg.isVirtual())
      return false;
    // No redefs for any operand.
    if (isRefInMap(RR, Defs, Exec_Then))
      return false;
    // For defs, there cannot be uses.
    if (Op.isDef() && isRefInMap(RR, Uses, Exec_Then))
      return false;
  }
  return true;
}

/// Check if the instruction accessing memory (TheI) can be moved to the
/// location ToI.
bool HexagonExpandCondsets::canMoveMemTo(MachineInstr &TheI, MachineInstr &ToI,
                                         bool IsDown) {
  bool IsLoad = TheI.mayLoad(), IsStore = TheI.mayStore();
  if (!IsLoad && !IsStore)
    return true;
  if (HII->areMemAccessesTriviallyDisjoint(TheI, ToI))
    return true;
  if (TheI.hasUnmodeledSideEffects())
    return false;

  MachineBasicBlock::iterator StartI = IsDown ? TheI : ToI;
  MachineBasicBlock::iterator EndI = IsDown ? ToI : TheI;
  bool Ordered = TheI.hasOrderedMemoryRef();

  // Search for aliased memory reference in (StartI, EndI).
  for (MachineInstr &MI : llvm::make_range(std::next(StartI), EndI)) {
    if (MI.hasUnmodeledSideEffects())
      return false;
    bool L = MI.mayLoad(), S = MI.mayStore();
    if (!L && !S)
      continue;
    if (Ordered && MI.hasOrderedMemoryRef())
      return false;

    bool Conflict = (L && IsStore) || S;
    if (Conflict)
      return false;
  }
  return true;
}

/// Generate a predicated version of MI (where the condition is given via
/// PredR and Cond) at the point indicated by Where.
void HexagonExpandCondsets::predicateAt(const MachineOperand &DefOp,
                                        MachineInstr &MI,
                                        MachineBasicBlock::iterator Where,
                                        const MachineOperand &PredOp, bool Cond,
                                        std::set<Register> &UpdRegs) {
  // The problem with updating live intervals is that we can move one def
  // past another def. In particular, this can happen when moving an A2_tfrt
  // over an A2_tfrf defining the same register. From the point of view of
  // live intervals, these two instructions are two separate definitions,
  // and each one starts another live segment. LiveIntervals's "handleMove"
  // does not allow such moves, so we need to handle it ourselves. To avoid
  // invalidating liveness data while we are using it, the move will be
  // implemented in 4 steps: (1) add a clone of the instruction MI at the
  // target location, (2) update liveness, (3) delete the old instruction,
  // and (4) update liveness again.

  MachineBasicBlock &B = *MI.getParent();
  DebugLoc DL = Where->getDebugLoc();  // "Where" points to an instruction.
  unsigned Opc = MI.getOpcode();
  unsigned PredOpc = HII->getCondOpcode(Opc, !Cond);
  MachineInstrBuilder MB = BuildMI(B, Where, DL, HII->get(PredOpc));
  unsigned Ox = 0, NP = MI.getNumOperands();
  // Skip all defs from MI first.
  while (Ox < NP) {
    MachineOperand &MO = MI.getOperand(Ox);
    if (!MO.isReg() || !MO.isDef())
      break;
    Ox++;
  }
  // Add the new def, then the predicate register, then the rest of the
  // operands.
  MB.addReg(DefOp.getReg(), getRegState(DefOp), DefOp.getSubReg());
  MB.addReg(PredOp.getReg(), PredOp.isUndef() ? RegState::Undef : 0,
            PredOp.getSubReg());
  while (Ox < NP) {
    MachineOperand &MO = MI.getOperand(Ox);
    if (!MO.isReg() || !MO.isImplicit())
      MB.add(MO);
    Ox++;
  }
  MB.cloneMemRefs(MI);

  MachineInstr *NewI = MB;
  NewI->clearKillInfo();
  LIS->InsertMachineInstrInMaps(*NewI);

  for (auto &Op : NewI->operands()) {
    if (Op.isReg())
      UpdRegs.insert(Op.getReg());
  }
}

/// In the range [First, Last], rename all references to the "old" register RO
/// to the "new" register RN, but only in instructions predicated on the given
/// condition.
void HexagonExpandCondsets::renameInRange(RegisterRef RO, RegisterRef RN,
      unsigned PredR, bool Cond, MachineBasicBlock::iterator First,
      MachineBasicBlock::iterator Last) {
  MachineBasicBlock::iterator End = std::next(Last);
  for (MachineInstr &MI : llvm::make_range(First, End)) {
    // Do not touch instructions that are not predicated, or are predicated
    // on the opposite condition.
    if (!HII->isPredicated(MI))
      continue;
    if (!MI.readsRegister(PredR, /*TRI=*/nullptr) ||
        (Cond != HII->isPredicatedTrue(MI)))
      continue;

    for (auto &Op : MI.operands()) {
      if (!Op.isReg() || RO != RegisterRef(Op))
        continue;
      Op.setReg(RN.Reg);
      Op.setSubReg(RN.Sub);
      // In practice, this isn't supposed to see any defs.
      assert(!Op.isDef() && "Not expecting a def");
    }
  }
}

/// For a given conditional copy, predicate the definition of the source of
/// the copy under the given condition (using the same predicate register as
/// the copy).
bool HexagonExpandCondsets::predicate(MachineInstr &TfrI, bool Cond,
                                      std::set<Register> &UpdRegs) {
  // TfrI - A2_tfr[tf] Instruction (not A2_tfrsi).
  unsigned Opc = TfrI.getOpcode();
  (void)Opc;
  assert(Opc == Hexagon::A2_tfrt || Opc == Hexagon::A2_tfrf);
  LLVM_DEBUG(dbgs() << "\nattempt to predicate if-" << (Cond ? "true" : "false")
                    << ": " << TfrI);

  MachineOperand &MD = TfrI.getOperand(0);
  MachineOperand &MP = TfrI.getOperand(1);
  MachineOperand &MS = TfrI.getOperand(2);
  // The source operand should be a <kill>. This is not strictly necessary,
  // but it makes things a lot simpler. Otherwise, we would need to rename
  // some registers, which would complicate the transformation considerably.
  if (!MS.isKill())
    return false;
  // Avoid predicating instructions that define a subregister if subregister
  // liveness tracking is not enabled.
  if (MD.getSubReg() && !MRI->shouldTrackSubRegLiveness(MD.getReg()))
    return false;

  RegisterRef RT(MS);
  Register PredR = MP.getReg();
  MachineInstr *DefI = getReachingDefForPred(RT, TfrI, PredR, Cond);
  if (!DefI || !isPredicable(DefI))
    return false;

  LLVM_DEBUG(dbgs() << "Source def: " << *DefI);

  // Collect the information about registers defined and used between the
  // DefI and the TfrI.
  // Map: reg -> bitmask of subregs
  ReferenceMap Uses, Defs;
  MachineBasicBlock::iterator DefIt = DefI, TfrIt = TfrI;

  // Check if the predicate register is valid between DefI and TfrI.
  // If it is, we can then ignore instructions predicated on the negated
  // conditions when collecting def and use information.
  bool PredValid = true;
  for (MachineInstr &MI : llvm::make_range(std::next(DefIt), TfrIt)) {
    if (!MI.modifiesRegister(PredR, nullptr))
      continue;
    PredValid = false;
    break;
  }

  for (MachineInstr &MI : llvm::make_range(std::next(DefIt), TfrIt)) {
    // If this instruction is predicated on the same register, it could
    // potentially be ignored.
    // By default assume that the instruction executes on the same condition
    // as TfrI (Exec_Then), and also on the opposite one (Exec_Else).
    unsigned Exec = Exec_Then | Exec_Else;
    if (PredValid && HII->isPredicated(MI) &&
        MI.readsRegister(PredR, /*TRI=*/nullptr))
      Exec = (Cond == HII->isPredicatedTrue(MI)) ? Exec_Then : Exec_Else;

    for (auto &Op : MI.operands()) {
      if (!Op.isReg())
        continue;
      // We don't want to deal with physical registers. The reason is that
      // they can be aliased with other physical registers. Aliased virtual
      // registers must share the same register number, and can only differ
      // in the subregisters, which we are keeping track of. Physical
      // registers ters no longer have subregisters---their super- and
      // subregisters are other physical registers, and we are not checking
      // that.
      RegisterRef RR = Op;
      if (!RR.Reg.isVirtual())
        return false;

      ReferenceMap &Map = Op.isDef() ? Defs : Uses;
      if (Op.isDef() && Op.isUndef()) {
        assert(RR.Sub && "Expecting a subregister on <def,read-undef>");
        // If this is a <def,read-undef>, then it invalidates the non-written
        // part of the register. For the purpose of checking the validity of
        // the move, assume that it modifies the whole register.
        RR.Sub = 0;
      }
      addRefToMap(RR, Map, Exec);
    }
  }

  // The situation:
  //   RT = DefI
  //   ...
  //   RD = TfrI ..., RT

  // If the register-in-the-middle (RT) is used or redefined between
  // DefI and TfrI, we may not be able proceed with this transformation.
  // We can ignore a def that will not execute together with TfrI, and a
  // use that will. If there is such a use (that does execute together with
  // TfrI), we will not be able to move DefI down. If there is a use that
  // executed if TfrI's condition is false, then RT must be available
  // unconditionally (cannot be predicated).
  // Essentially, we need to be able to rename RT to RD in this segment.
  if (isRefInMap(RT, Defs, Exec_Then) || isRefInMap(RT, Uses, Exec_Else))
    return false;
  RegisterRef RD = MD;
  // If the predicate register is defined between DefI and TfrI, the only
  // potential thing to do would be to move the DefI down to TfrI, and then
  // predicate. The reaching def (DefI) must be movable down to the location
  // of the TfrI.
  // If the target register of the TfrI (RD) is not used or defined between
  // DefI and TfrI, consider moving TfrI up to DefI.
  bool CanUp =   canMoveOver(TfrI, Defs, Uses);
  bool CanDown = canMoveOver(*DefI, Defs, Uses);
  // The TfrI does not access memory, but DefI could. Check if it's safe
  // to move DefI down to TfrI.
  if (DefI->mayLoadOrStore()) {
    if (!canMoveMemTo(*DefI, TfrI, true))
      CanDown = false;
  }

  LLVM_DEBUG(dbgs() << "Can move up: " << (CanUp ? "yes" : "no")
                    << ", can move down: " << (CanDown ? "yes\n" : "no\n"));
  MachineBasicBlock::iterator PastDefIt = std::next(DefIt);
  if (CanUp)
    predicateAt(MD, *DefI, PastDefIt, MP, Cond, UpdRegs);
  else if (CanDown)
    predicateAt(MD, *DefI, TfrIt, MP, Cond, UpdRegs);
  else
    return false;

  if (RT != RD) {
    renameInRange(RT, RD, PredR, Cond, PastDefIt, TfrIt);
    UpdRegs.insert(RT.Reg);
  }

  removeInstr(TfrI);
  removeInstr(*DefI);
  return true;
}

/// Predicate all cases of conditional copies in the specified block.
bool HexagonExpandCondsets::predicateInBlock(MachineBasicBlock &B,
                                             std::set<Register> &UpdRegs) {
  bool Changed = false;
  for (MachineInstr &MI : llvm::make_early_inc_range(B)) {
    unsigned Opc = MI.getOpcode();
    if (Opc == Hexagon::A2_tfrt || Opc == Hexagon::A2_tfrf) {
      bool Done = predicate(MI, (Opc == Hexagon::A2_tfrt), UpdRegs);
      if (!Done) {
        // If we didn't predicate I, we may need to remove it in case it is
        // an "identity" copy, e.g.  %1 = A2_tfrt %2, %1.
        if (RegisterRef(MI.getOperand(0)) == RegisterRef(MI.getOperand(2))) {
          for (auto &Op : MI.operands()) {
            if (Op.isReg())
              UpdRegs.insert(Op.getReg());
          }
          removeInstr(MI);
        }
      }
      Changed |= Done;
    }
  }
  return Changed;
}

bool HexagonExpandCondsets::isIntReg(RegisterRef RR, unsigned &BW) {
  if (!RR.Reg.isVirtual())
    return false;
  const TargetRegisterClass *RC = MRI->getRegClass(RR.Reg);
  if (RC == &Hexagon::IntRegsRegClass) {
    BW = 32;
    return true;
  }
  if (RC == &Hexagon::DoubleRegsRegClass) {
    BW = (RR.Sub != 0) ? 32 : 64;
    return true;
  }
  return false;
}

bool HexagonExpandCondsets::isIntraBlocks(LiveInterval &LI) {
  for (LiveRange::Segment &LR : LI) {
    // Range must start at a register...
    if (!LR.start.isRegister())
      return false;
    // ...and end in a register or in a dead slot.
    if (!LR.end.isRegister() && !LR.end.isDead())
      return false;
  }
  return true;
}

bool HexagonExpandCondsets::coalesceRegisters(RegisterRef R1, RegisterRef R2) {
  if (CoaLimitActive) {
    if (CoaCounter >= CoaLimit)
      return false;
    CoaCounter++;
  }
  unsigned BW1, BW2;
  if (!isIntReg(R1, BW1) || !isIntReg(R2, BW2) || BW1 != BW2)
    return false;
  if (MRI->isLiveIn(R1.Reg))
    return false;
  if (MRI->isLiveIn(R2.Reg))
    return false;

  LiveInterval &L1 = LIS->getInterval(R1.Reg);
  LiveInterval &L2 = LIS->getInterval(R2.Reg);
  if (L2.empty())
    return false;
  if (L1.hasSubRanges() || L2.hasSubRanges())
    return false;
  bool Overlap = L1.overlaps(L2);

  LLVM_DEBUG(dbgs() << "compatible registers: ("
                    << (Overlap ? "overlap" : "disjoint") << ")\n  "
                    << printReg(R1.Reg, TRI, R1.Sub) << "  " << L1 << "\n  "
                    << printReg(R2.Reg, TRI, R2.Sub) << "  " << L2 << "\n");
  if (R1.Sub || R2.Sub)
    return false;
  if (Overlap)
    return false;

  // Coalescing could have a negative impact on scheduling, so try to limit
  // to some reasonable extent. Only consider coalescing segments, when one
  // of them does not cross basic block boundaries.
  if (!isIntraBlocks(L1) && !isIntraBlocks(L2))
    return false;

  MRI->replaceRegWith(R2.Reg, R1.Reg);

  // Move all live segments from L2 to L1.
  using ValueInfoMap = DenseMap<VNInfo *, VNInfo *>;
  ValueInfoMap VM;
  for (LiveRange::Segment &I : L2) {
    VNInfo *NewVN, *OldVN = I.valno;
    ValueInfoMap::iterator F = VM.find(OldVN);
    if (F == VM.end()) {
      NewVN = L1.getNextValue(I.valno->def, LIS->getVNInfoAllocator());
      VM.insert(std::make_pair(OldVN, NewVN));
    } else {
      NewVN = F->second;
    }
    L1.addSegment(LiveRange::Segment(I.start, I.end, NewVN));
  }
  while (!L2.empty())
    L2.removeSegment(*L2.begin());
  LIS->removeInterval(R2.Reg);

  updateKillFlags(R1.Reg);
  LLVM_DEBUG(dbgs() << "coalesced: " << L1 << "\n");
  L1.verify();

  return true;
}

/// Attempt to coalesce one of the source registers to a MUX instruction with
/// the destination register. This could lead to having only one predicated
/// instruction in the end instead of two.
bool HexagonExpandCondsets::coalesceSegments(
    const SmallVectorImpl<MachineInstr *> &Condsets,
    std::set<Register> &UpdRegs) {
  SmallVector<MachineInstr*,16> TwoRegs;
  for (MachineInstr *MI : Condsets) {
    MachineOperand &S1 = MI->getOperand(2), &S2 = MI->getOperand(3);
    if (!S1.isReg() && !S2.isReg())
      continue;
    TwoRegs.push_back(MI);
  }

  bool Changed = false;
  for (MachineInstr *CI : TwoRegs) {
    RegisterRef RD = CI->getOperand(0);
    RegisterRef RP = CI->getOperand(1);
    MachineOperand &S1 = CI->getOperand(2), &S2 = CI->getOperand(3);
    bool Done = false;
    // Consider this case:
    //   %1 = instr1 ...
    //   %2 = instr2 ...
    //   %0 = C2_mux ..., %1, %2
    // If %0 was coalesced with %1, we could end up with the following
    // code:
    //   %0 = instr1 ...
    //   %2 = instr2 ...
    //   %0 = A2_tfrf ..., %2
    // which will later become:
    //   %0 = instr1 ...
    //   %0 = instr2_cNotPt ...
    // i.e. there will be an unconditional definition (instr1) of %0
    // followed by a conditional one. The output dependency was there before
    // and it unavoidable, but if instr1 is predicable, we will no longer be
    // able to predicate it here.
    // To avoid this scenario, don't coalesce the destination register with
    // a source register that is defined by a predicable instruction.
    if (S1.isReg()) {
      RegisterRef RS = S1;
      MachineInstr *RDef = getReachingDefForPred(RS, CI, RP.Reg, true);
      if (!RDef || !HII->isPredicable(*RDef)) {
        Done = coalesceRegisters(RD, RegisterRef(S1));
        if (Done) {
          UpdRegs.insert(RD.Reg);
          UpdRegs.insert(S1.getReg());
        }
      }
    }
    if (!Done && S2.isReg()) {
      RegisterRef RS = S2;
      MachineInstr *RDef = getReachingDefForPred(RS, CI, RP.Reg, false);
      if (!RDef || !HII->isPredicable(*RDef)) {
        Done = coalesceRegisters(RD, RegisterRef(S2));
        if (Done) {
          UpdRegs.insert(RD.Reg);
          UpdRegs.insert(S2.getReg());
        }
      }
    }
    Changed |= Done;
  }
  return Changed;
}

bool HexagonExpandCondsets::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  HII = static_cast<const HexagonInstrInfo*>(MF.getSubtarget().getInstrInfo());
  TRI = MF.getSubtarget().getRegisterInfo();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  MRI = &MF.getRegInfo();

  LLVM_DEBUG(LIS->print(dbgs() << "Before expand-condsets\n"));

  bool Changed = false;
  std::set<Register> CoalUpd, PredUpd;

  SmallVector<MachineInstr*,16> Condsets;
  for (auto &B : MF) {
    for (auto &I : B) {
      if (isCondset(I))
        Condsets.push_back(&I);
    }
  }

  // Try to coalesce the target of a mux with one of its sources.
  // This could eliminate a register copy in some circumstances.
  Changed |= coalesceSegments(Condsets, CoalUpd);

  // Update kill flags on all source operands. This is done here because
  // at this moment (when expand-condsets runs), there are no kill flags
  // in the IR (they have been removed by live range analysis).
  // Updating them right before we split is the easiest, because splitting
  // adds definitions which would interfere with updating kills afterwards.
  std::set<Register> KillUpd;
  for (MachineInstr *MI : Condsets) {
    for (MachineOperand &Op : MI->operands()) {
      if (Op.isReg() && Op.isUse()) {
        if (!CoalUpd.count(Op.getReg()))
          KillUpd.insert(Op.getReg());
      }
    }
  }
  updateLiveness(KillUpd, false, true, false);
  LLVM_DEBUG(LIS->print(dbgs() << "After coalescing\n"));

  // First, simply split all muxes into a pair of conditional transfers
  // and update the live intervals to reflect the new arrangement. The
  // goal is to update the kill flags, since predication will rely on
  // them.
  for (MachineInstr *MI : Condsets)
    Changed |= split(*MI, PredUpd);
  Condsets.clear(); // The contents of Condsets are invalid here anyway.

  // Do not update live ranges after splitting. Recalculation of live
  // intervals removes kill flags, which were preserved by splitting on
  // the source operands of condsets. These kill flags are needed by
  // predication, and after splitting they are difficult to recalculate
  // (because of predicated defs), so make sure they are left untouched.
  // Predication does not use live intervals.
  LLVM_DEBUG(LIS->print(dbgs() << "After splitting\n"));

  // Traverse all blocks and collapse predicable instructions feeding
  // conditional transfers into predicated instructions.
  // Walk over all the instructions again, so we may catch pre-existing
  // cases that were not created in the previous step.
  for (auto &B : MF)
    Changed |= predicateInBlock(B, PredUpd);
  LLVM_DEBUG(LIS->print(dbgs() << "After predicating\n"));

  PredUpd.insert(CoalUpd.begin(), CoalUpd.end());
  updateLiveness(PredUpd, true, true, true);

  if (Changed)
    distributeLiveIntervals(PredUpd);

  LLVM_DEBUG({
    if (Changed)
      LIS->print(dbgs() << "After expand-condsets\n");
  });

  return Changed;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//
FunctionPass *llvm::createHexagonExpandCondsets() {
  return new HexagonExpandCondsets();
}
