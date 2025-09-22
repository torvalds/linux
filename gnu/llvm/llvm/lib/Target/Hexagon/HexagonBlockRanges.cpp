//===- HexagonBlockRanges.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonBlockRanges.h"
#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "hbr"

bool HexagonBlockRanges::IndexRange::overlaps(const IndexRange &A) const {
  // If A contains start(), or "this" contains A.start(), then overlap.
  IndexType S = start(), E = end(), AS = A.start(), AE = A.end();
  if (AS == S)
    return true;
  bool SbAE = (S < AE) || (S == AE && A.TiedEnd);  // S-before-AE.
  bool ASbE = (AS < E) || (AS == E && TiedEnd);    // AS-before-E.
  if ((AS < S && SbAE) || (S < AS && ASbE))
    return true;
  // Otherwise no overlap.
  return false;
}

bool HexagonBlockRanges::IndexRange::contains(const IndexRange &A) const {
  if (start() <= A.start()) {
    // Treat "None" in the range end as equal to the range start.
    IndexType E = (end() != IndexType::None) ? end() : start();
    IndexType AE = (A.end() != IndexType::None) ? A.end() : A.start();
    if (AE <= E)
      return true;
  }
  return false;
}

void HexagonBlockRanges::IndexRange::merge(const IndexRange &A) {
  // Allow merging adjacent ranges.
  assert(end() == A.start() || overlaps(A));
  IndexType AS = A.start(), AE = A.end();
  if (AS < start() || start() == IndexType::None)
    setStart(AS);
  if (end() < AE || end() == IndexType::None) {
    setEnd(AE);
    TiedEnd = A.TiedEnd;
  } else {
    if (end() == AE)
      TiedEnd |= A.TiedEnd;
  }
  if (A.Fixed)
    Fixed = true;
}

void HexagonBlockRanges::RangeList::include(const RangeList &RL) {
  for (const auto &R : RL)
    if (!is_contained(*this, R))
      push_back(R);
}

// Merge all overlapping ranges in the list, so that all that remains
// is a list of disjoint ranges.
void HexagonBlockRanges::RangeList::unionize(bool MergeAdjacent) {
  if (empty())
    return;

  llvm::sort(*this);
  iterator Iter = begin();

  while (Iter != end()-1) {
    iterator Next = std::next(Iter);
    // If MergeAdjacent is true, merge ranges A and B, where A.end == B.start.
    // This allows merging dead ranges, but is not valid for live ranges.
    bool Merge = MergeAdjacent && (Iter->end() == Next->start());
    if (Merge || Iter->overlaps(*Next)) {
      Iter->merge(*Next);
      erase(Next);
      continue;
    }
    ++Iter;
  }
}

// Compute a range A-B and add it to the list.
void HexagonBlockRanges::RangeList::addsub(const IndexRange &A,
      const IndexRange &B) {
  // Exclusion of non-overlapping ranges makes some checks simpler
  // later in this function.
  if (!A.overlaps(B)) {
    // A - B = A.
    add(A);
    return;
  }

  IndexType AS = A.start(), AE = A.end();
  IndexType BS = B.start(), BE = B.end();

  // If AE is None, then A is included in B, since A and B overlap.
  // The result of subtraction if empty, so just return.
  if (AE == IndexType::None)
    return;

  if (AS < BS) {
    // A starts before B.
    // AE cannot be None since A and B overlap.
    assert(AE != IndexType::None);
    // Add the part of A that extends on the "less" side of B.
    add(AS, BS, A.Fixed, false);
  }

  if (BE < AE) {
    // BE cannot be Exit here.
    if (BE == IndexType::None)
      add(BS, AE, A.Fixed, false);
    else
      add(BE, AE, A.Fixed, false);
  }
}

// Subtract a given range from each element in the list.
void HexagonBlockRanges::RangeList::subtract(const IndexRange &Range) {
  // Cannot assume that the list is unionized (i.e. contains only non-
  // overlapping ranges.
  RangeList T;
  for (iterator Next, I = begin(); I != end(); I = Next) {
    IndexRange &Rg = *I;
    if (Rg.overlaps(Range)) {
      T.addsub(Rg, Range);
      Next = this->erase(I);
    } else {
      Next = std::next(I);
    }
  }
  include(T);
}

HexagonBlockRanges::InstrIndexMap::InstrIndexMap(MachineBasicBlock &B)
    : Block(B) {
  IndexType Idx = IndexType::First;
  First = Idx;
  for (auto &In : B) {
    if (In.isDebugInstr())
      continue;
    assert(getIndex(&In) == IndexType::None && "Instruction already in map");
    Map.insert(std::make_pair(Idx, &In));
    ++Idx;
  }
  Last = B.empty() ? IndexType::None : unsigned(Idx)-1;
}

MachineInstr *HexagonBlockRanges::InstrIndexMap::getInstr(IndexType Idx) const {
  auto F = Map.find(Idx);
  return (F != Map.end()) ? F->second : nullptr;
}

HexagonBlockRanges::IndexType HexagonBlockRanges::InstrIndexMap::getIndex(
      MachineInstr *MI) const {
  for (const auto &I : Map)
    if (I.second == MI)
      return I.first;
  return IndexType::None;
}

HexagonBlockRanges::IndexType HexagonBlockRanges::InstrIndexMap::getPrevIndex(
      IndexType Idx) const {
  assert (Idx != IndexType::None);
  if (Idx == IndexType::Entry)
    return IndexType::None;
  if (Idx == IndexType::Exit)
    return Last;
  if (Idx == First)
    return IndexType::Entry;
  return unsigned(Idx)-1;
}

HexagonBlockRanges::IndexType HexagonBlockRanges::InstrIndexMap::getNextIndex(
      IndexType Idx) const {
  assert (Idx != IndexType::None);
  if (Idx == IndexType::Entry)
    return IndexType::First;
  if (Idx == IndexType::Exit || Idx == Last)
    return IndexType::None;
  return unsigned(Idx)+1;
}

void HexagonBlockRanges::InstrIndexMap::replaceInstr(MachineInstr *OldMI,
      MachineInstr *NewMI) {
  for (auto &I : Map) {
    if (I.second != OldMI)
      continue;
    if (NewMI != nullptr)
      I.second = NewMI;
    else
      Map.erase(I.first);
    break;
  }
}

HexagonBlockRanges::HexagonBlockRanges(MachineFunction &mf)
  : MF(mf), HST(mf.getSubtarget<HexagonSubtarget>()),
    TII(*HST.getInstrInfo()), TRI(*HST.getRegisterInfo()),
    Reserved(TRI.getReservedRegs(mf)) {
  // Consider all non-allocatable registers as reserved.
  for (const TargetRegisterClass *RC : TRI.regclasses()) {
    if (RC->isAllocatable())
      continue;
    for (unsigned R : *RC)
      Reserved[R] = true;
  }
}

HexagonBlockRanges::RegisterSet HexagonBlockRanges::getLiveIns(
      const MachineBasicBlock &B, const MachineRegisterInfo &MRI,
      const TargetRegisterInfo &TRI) {
  RegisterSet LiveIns;
  RegisterSet Tmp;

  for (auto I : B.liveins()) {
    MCSubRegIndexIterator S(I.PhysReg, &TRI);
    if (I.LaneMask.all() || (I.LaneMask.any() && !S.isValid())) {
      Tmp.insert({I.PhysReg, 0});
      continue;
    }
    for (; S.isValid(); ++S) {
      unsigned SI = S.getSubRegIndex();
      if ((I.LaneMask & TRI.getSubRegIndexLaneMask(SI)).any())
        Tmp.insert({S.getSubReg(), 0});
    }
  }

  for (auto R : Tmp) {
    if (!Reserved[R.Reg])
      LiveIns.insert(R);
    for (auto S : expandToSubRegs(R, MRI, TRI))
      if (!Reserved[S.Reg])
        LiveIns.insert(S);
  }
  return LiveIns;
}

HexagonBlockRanges::RegisterSet HexagonBlockRanges::expandToSubRegs(
      RegisterRef R, const MachineRegisterInfo &MRI,
      const TargetRegisterInfo &TRI) {
  RegisterSet SRs;

  if (R.Sub != 0) {
    SRs.insert(R);
    return SRs;
  }

  if (R.Reg.isPhysical()) {
    if (TRI.subregs(R.Reg).empty())
      SRs.insert({R.Reg, 0});
    for (MCPhysReg I : TRI.subregs(R.Reg))
      SRs.insert({I, 0});
  } else {
    assert(R.Reg.isVirtual());
    auto &RC = *MRI.getRegClass(R.Reg);
    unsigned PReg = *RC.begin();
    MCSubRegIndexIterator I(PReg, &TRI);
    if (!I.isValid())
      SRs.insert({R.Reg, 0});
    for (; I.isValid(); ++I)
      SRs.insert({R.Reg, I.getSubRegIndex()});
  }
  return SRs;
}

void HexagonBlockRanges::computeInitialLiveRanges(InstrIndexMap &IndexMap,
      RegToRangeMap &LiveMap) {
  std::map<RegisterRef,IndexType> LastDef, LastUse;
  RegisterSet LiveOnEntry;
  MachineBasicBlock &B = IndexMap.getBlock();
  MachineRegisterInfo &MRI = B.getParent()->getRegInfo();

  for (auto R : getLiveIns(B, MRI, TRI))
    LiveOnEntry.insert(R);

  for (auto R : LiveOnEntry)
    LastDef[R] = IndexType::Entry;

  auto closeRange = [&LastUse,&LastDef,&LiveMap] (RegisterRef R) -> void {
    auto LD = LastDef[R], LU = LastUse[R];
    if (LD == IndexType::None)
      LD = IndexType::Entry;
    if (LU == IndexType::None)
      LU = IndexType::Exit;
    LiveMap[R].add(LD, LU, false, false);
    LastUse[R] = LastDef[R] = IndexType::None;
  };

  RegisterSet Defs, Clobbers;

  for (auto &In : B) {
    if (In.isDebugInstr())
      continue;
    IndexType Index = IndexMap.getIndex(&In);
    // Process uses first.
    for (auto &Op : In.operands()) {
      if (!Op.isReg() || !Op.isUse() || Op.isUndef())
        continue;
      RegisterRef R = { Op.getReg(), Op.getSubReg() };
      if (R.Reg.isPhysical() && Reserved[R.Reg])
        continue;
      bool IsKill = Op.isKill();
      for (auto S : expandToSubRegs(R, MRI, TRI)) {
        LastUse[S] = Index;
        if (IsKill)
          closeRange(S);
      }
    }
    // Process defs and clobbers.
    Defs.clear();
    Clobbers.clear();
    for (auto &Op : In.operands()) {
      if (!Op.isReg() || !Op.isDef() || Op.isUndef())
        continue;
      RegisterRef R = { Op.getReg(), Op.getSubReg() };
      for (auto S : expandToSubRegs(R, MRI, TRI)) {
        if (S.Reg.isPhysical() && Reserved[S.Reg])
          continue;
        if (Op.isDead())
          Clobbers.insert(S);
        else
          Defs.insert(S);
      }
    }

    for (auto &Op : In.operands()) {
      if (!Op.isRegMask())
        continue;
      const uint32_t *BM = Op.getRegMask();
      for (unsigned PR = 1, N = TRI.getNumRegs(); PR != N; ++PR) {
        // Skip registers that have subregisters. A register is preserved
        // iff its bit is set in the regmask, so if R1:0 was preserved, both
        // R1 and R0 would also be present.
        if (!TRI.subregs(PR).empty())
          continue;
        if (Reserved[PR])
          continue;
        if (BM[PR/32] & (1u << (PR%32)))
          continue;
        RegisterRef R = { PR, 0 };
        if (!Defs.count(R))
          Clobbers.insert(R);
      }
    }
    // Defs and clobbers can overlap, e.g.
    // dead %d0 = COPY %5, implicit-def %r0, implicit-def %r1
    for (RegisterRef R : Defs)
      Clobbers.erase(R);

    // Update maps for defs.
    for (RegisterRef S : Defs) {
      // Defs should already be expanded into subregs.
      assert(!S.Reg.isPhysical() || TRI.subregs(S.Reg).empty());
      if (LastDef[S] != IndexType::None || LastUse[S] != IndexType::None)
        closeRange(S);
      LastDef[S] = Index;
    }
    // Update maps for clobbers.
    for (RegisterRef S : Clobbers) {
      // Clobbers should already be expanded into subregs.
      assert(!S.Reg.isPhysical() || TRI.subregs(S.Reg).empty());
      if (LastDef[S] != IndexType::None || LastUse[S] != IndexType::None)
        closeRange(S);
      // Create a single-instruction range.
      LastDef[S] = LastUse[S] = Index;
      closeRange(S);
    }
  }

  // Collect live-on-exit.
  RegisterSet LiveOnExit;
  for (auto *SB : B.successors())
    for (auto R : getLiveIns(*SB, MRI, TRI))
      LiveOnExit.insert(R);

  for (auto R : LiveOnExit)
    LastUse[R] = IndexType::Exit;

  // Process remaining registers.
  RegisterSet Left;
  for (auto &I : LastUse)
    if (I.second != IndexType::None)
      Left.insert(I.first);
  for (auto &I : LastDef)
    if (I.second != IndexType::None)
      Left.insert(I.first);
  for (auto R : Left)
    closeRange(R);

  // Finalize the live ranges.
  for (auto &P : LiveMap)
    P.second.unionize();
}

HexagonBlockRanges::RegToRangeMap HexagonBlockRanges::computeLiveMap(
      InstrIndexMap &IndexMap) {
  RegToRangeMap LiveMap;
  LLVM_DEBUG(dbgs() << __func__ << ": index map\n" << IndexMap << '\n');
  computeInitialLiveRanges(IndexMap, LiveMap);
  LLVM_DEBUG(dbgs() << __func__ << ": live map\n"
                    << PrintRangeMap(LiveMap, TRI) << '\n');
  return LiveMap;
}

HexagonBlockRanges::RegToRangeMap HexagonBlockRanges::computeDeadMap(
      InstrIndexMap &IndexMap, RegToRangeMap &LiveMap) {
  RegToRangeMap DeadMap;

  auto addDeadRanges = [&IndexMap,&LiveMap,&DeadMap] (RegisterRef R) -> void {
    auto F = LiveMap.find(R);
    if (F == LiveMap.end() || F->second.empty()) {
      DeadMap[R].add(IndexType::Entry, IndexType::Exit, false, false);
      return;
    }

    RangeList &RL = F->second;
    RangeList::iterator A = RL.begin(), Z = RL.end()-1;

    // Try to create the initial range.
    if (A->start() != IndexType::Entry) {
      IndexType DE = IndexMap.getPrevIndex(A->start());
      if (DE != IndexType::Entry)
        DeadMap[R].add(IndexType::Entry, DE, false, false);
    }

    while (A != Z) {
      // Creating a dead range that follows A.  Pay attention to empty
      // ranges (i.e. those ending with "None").
      IndexType AE = (A->end() == IndexType::None) ? A->start() : A->end();
      IndexType DS = IndexMap.getNextIndex(AE);
      ++A;
      IndexType DE = IndexMap.getPrevIndex(A->start());
      if (DS < DE)
        DeadMap[R].add(DS, DE, false, false);
    }

    // Try to create the final range.
    if (Z->end() != IndexType::Exit) {
      IndexType ZE = (Z->end() == IndexType::None) ? Z->start() : Z->end();
      IndexType DS = IndexMap.getNextIndex(ZE);
      if (DS < IndexType::Exit)
        DeadMap[R].add(DS, IndexType::Exit, false, false);
    }
  };

  MachineFunction &MF = *IndexMap.getBlock().getParent();
  auto &MRI = MF.getRegInfo();
  unsigned NumRegs = TRI.getNumRegs();
  BitVector Visited(NumRegs);
  for (unsigned R = 1; R < NumRegs; ++R) {
    for (auto S : expandToSubRegs({R,0}, MRI, TRI)) {
      if (Reserved[S.Reg] || Visited[S.Reg])
        continue;
      addDeadRanges(S);
      Visited[S.Reg] = true;
    }
  }
  for (auto &P : LiveMap)
    if (P.first.Reg.isVirtual())
      addDeadRanges(P.first);

  LLVM_DEBUG(dbgs() << __func__ << ": dead map\n"
                    << PrintRangeMap(DeadMap, TRI) << '\n');
  return DeadMap;
}

raw_ostream &llvm::operator<<(raw_ostream &OS,
                              HexagonBlockRanges::IndexType Idx) {
  if (Idx == HexagonBlockRanges::IndexType::None)
    return OS << '-';
  if (Idx == HexagonBlockRanges::IndexType::Entry)
    return OS << 'n';
  if (Idx == HexagonBlockRanges::IndexType::Exit)
    return OS << 'x';
  return OS << unsigned(Idx)-HexagonBlockRanges::IndexType::First+1;
}

// A mapping to translate between instructions and their indices.
raw_ostream &llvm::operator<<(raw_ostream &OS,
                              const HexagonBlockRanges::IndexRange &IR) {
  OS << '[' << IR.start() << ':' << IR.end() << (IR.TiedEnd ? '}' : ']');
  if (IR.Fixed)
    OS << '!';
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS,
                              const HexagonBlockRanges::RangeList &RL) {
  for (const auto &R : RL)
    OS << R << " ";
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS,
                              const HexagonBlockRanges::InstrIndexMap &M) {
  for (auto &In : M.Block) {
    HexagonBlockRanges::IndexType Idx = M.getIndex(&In);
    OS << Idx << (Idx == M.Last ? ". " : "  ") << In;
  }
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS,
                              const HexagonBlockRanges::PrintRangeMap &P) {
  for (const auto &I : P.Map) {
    const HexagonBlockRanges::RangeList &RL = I.second;
    OS << printReg(I.first.Reg, &P.TRI, I.first.Sub) << " -> " << RL << "\n";
  }
  return OS;
}
