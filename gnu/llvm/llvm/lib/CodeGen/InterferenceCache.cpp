//===- InterferenceCache.cpp - Caching per-block interference -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// InterferenceCache remembers per-block interference in LiveIntervalUnions.
//
//===----------------------------------------------------------------------===//

#include "InterferenceCache.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

// Static member used for null interference cursors.
const InterferenceCache::BlockInterference
    InterferenceCache::Cursor::NoInterference;

// Initializes PhysRegEntries (instead of a SmallVector, PhysRegEntries is a
// buffer of size NumPhysRegs to speed up alloc/clear for targets with large
// reg files). Calloced memory is used for good form, and quites tools like
// Valgrind too, but zero initialized memory is not required by the algorithm:
// this is because PhysRegEntries works like a SparseSet and its entries are
// only valid when there is a corresponding CacheEntries assignment. There is
// also support for when pass managers are reused for targets with different
// numbers of PhysRegs: in this case PhysRegEntries is freed and reinitialized.
void InterferenceCache::reinitPhysRegEntries() {
  if (PhysRegEntriesCount == TRI->getNumRegs()) return;
  free(PhysRegEntries);
  PhysRegEntriesCount = TRI->getNumRegs();
  PhysRegEntries = static_cast<unsigned char*>(
      safe_calloc(PhysRegEntriesCount, sizeof(unsigned char)));
}

void InterferenceCache::init(MachineFunction *mf,
                             LiveIntervalUnion *liuarray,
                             SlotIndexes *indexes,
                             LiveIntervals *lis,
                             const TargetRegisterInfo *tri) {
  MF = mf;
  LIUArray = liuarray;
  TRI = tri;
  reinitPhysRegEntries();
  for (Entry &E : Entries)
    E.clear(mf, indexes, lis);
}

InterferenceCache::Entry *InterferenceCache::get(MCRegister PhysReg) {
  unsigned char E = PhysRegEntries[PhysReg.id()];
  if (E < CacheEntries && Entries[E].getPhysReg() == PhysReg) {
    if (!Entries[E].valid(LIUArray, TRI))
      Entries[E].revalidate(LIUArray, TRI);
    return &Entries[E];
  }
  // No valid entry exists, pick the next round-robin entry.
  E = RoundRobin;
  if (++RoundRobin == CacheEntries)
    RoundRobin = 0;
  for (unsigned i = 0; i != CacheEntries; ++i) {
    // Skip entries that are in use.
    if (Entries[E].hasRefs()) {
      if (++E == CacheEntries)
        E = 0;
      continue;
    }
    Entries[E].reset(PhysReg, LIUArray, TRI, MF);
    PhysRegEntries[PhysReg] = E;
    return &Entries[E];
  }
  llvm_unreachable("Ran out of interference cache entries.");
}

/// revalidate - LIU contents have changed, update tags.
void InterferenceCache::Entry::revalidate(LiveIntervalUnion *LIUArray,
                                          const TargetRegisterInfo *TRI) {
  // Invalidate all block entries.
  ++Tag;
  // Invalidate all iterators.
  PrevPos = SlotIndex();
  unsigned i = 0;
  for (MCRegUnit Unit : TRI->regunits(PhysReg))
    RegUnits[i++].VirtTag = LIUArray[Unit].getTag();
}

void InterferenceCache::Entry::reset(MCRegister physReg,
                                     LiveIntervalUnion *LIUArray,
                                     const TargetRegisterInfo *TRI,
                                     const MachineFunction *MF) {
  assert(!hasRefs() && "Cannot reset cache entry with references");
  // LIU's changed, invalidate cache.
  ++Tag;
  PhysReg = physReg;
  Blocks.resize(MF->getNumBlockIDs());

  // Reset iterators.
  PrevPos = SlotIndex();
  RegUnits.clear();
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    RegUnits.push_back(LIUArray[Unit]);
    RegUnits.back().Fixed = &LIS->getRegUnit(Unit);
  }
}

bool InterferenceCache::Entry::valid(LiveIntervalUnion *LIUArray,
                                     const TargetRegisterInfo *TRI) {
  unsigned i = 0, e = RegUnits.size();
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    if (i == e)
      return false;
    if (LIUArray[Unit].changedSince(RegUnits[i].VirtTag))
      return false;
    ++i;
  }
  return i == e;
}

void InterferenceCache::Entry::update(unsigned MBBNum) {
  SlotIndex Start, Stop;
  std::tie(Start, Stop) = Indexes->getMBBRange(MBBNum);

  // Use advanceTo only when possible.
  if (PrevPos != Start) {
    if (!PrevPos.isValid() || Start < PrevPos) {
      for (RegUnitInfo &RUI : RegUnits) {
        RUI.VirtI.find(Start);
        RUI.FixedI = RUI.Fixed->find(Start);
      }
    } else {
      for (RegUnitInfo &RUI : RegUnits) {
        RUI.VirtI.advanceTo(Start);
        if (RUI.FixedI != RUI.Fixed->end())
          RUI.FixedI = RUI.Fixed->advanceTo(RUI.FixedI, Start);
      }
    }
    PrevPos = Start;
  }

  MachineFunction::const_iterator MFI =
      MF->getBlockNumbered(MBBNum)->getIterator();
  BlockInterference *BI = &Blocks[MBBNum];
  ArrayRef<SlotIndex> RegMaskSlots;
  ArrayRef<const uint32_t*> RegMaskBits;
  while (true) {
    BI->Tag = Tag;
    BI->First = BI->Last = SlotIndex();

    // Check for first interference from virtregs.
    for (RegUnitInfo &RUI : RegUnits) {
      LiveIntervalUnion::SegmentIter &I = RUI.VirtI;
      if (!I.valid())
        continue;
      SlotIndex StartI = I.start();
      if (StartI >= Stop)
        continue;
      if (!BI->First.isValid() || StartI < BI->First)
        BI->First = StartI;
    }

    // Same thing for fixed interference.
    for (RegUnitInfo &RUI : RegUnits) {
      LiveInterval::const_iterator I = RUI.FixedI;
      LiveInterval::const_iterator E = RUI.Fixed->end();
      if (I == E)
        continue;
      SlotIndex StartI = I->start;
      if (StartI >= Stop)
        continue;
      if (!BI->First.isValid() || StartI < BI->First)
        BI->First = StartI;
    }

    // Also check for register mask interference.
    RegMaskSlots = LIS->getRegMaskSlotsInBlock(MBBNum);
    RegMaskBits = LIS->getRegMaskBitsInBlock(MBBNum);
    SlotIndex Limit = BI->First.isValid() ? BI->First : Stop;
    for (unsigned i = 0, e = RegMaskSlots.size();
         i != e && RegMaskSlots[i] < Limit; ++i)
      if (MachineOperand::clobbersPhysReg(RegMaskBits[i], PhysReg)) {
        // Register mask i clobbers PhysReg before the LIU interference.
        BI->First = RegMaskSlots[i];
        break;
      }

    PrevPos = Stop;
    if (BI->First.isValid())
      break;

    // No interference in this block? Go ahead and precompute the next block.
    if (++MFI == MF->end())
      return;
    MBBNum = MFI->getNumber();
    BI = &Blocks[MBBNum];
    if (BI->Tag == Tag)
      return;
    std::tie(Start, Stop) = Indexes->getMBBRange(MBBNum);
  }

  // Check for last interference in block.
  for (RegUnitInfo &RUI : RegUnits) {
    LiveIntervalUnion::SegmentIter &I = RUI.VirtI;
    if (!I.valid() || I.start() >= Stop)
      continue;
    I.advanceTo(Stop);
    bool Backup = !I.valid() || I.start() >= Stop;
    if (Backup)
      --I;
    SlotIndex StopI = I.stop();
    if (!BI->Last.isValid() || StopI > BI->Last)
      BI->Last = StopI;
    if (Backup)
      ++I;
  }

  // Fixed interference.
  for (RegUnitInfo &RUI : RegUnits) {
    LiveInterval::iterator &I = RUI.FixedI;
    LiveRange *LR = RUI.Fixed;
    if (I == LR->end() || I->start >= Stop)
      continue;
    I = LR->advanceTo(I, Stop);
    bool Backup = I == LR->end() || I->start >= Stop;
    if (Backup)
      --I;
    SlotIndex StopI = I->end;
    if (!BI->Last.isValid() || StopI > BI->Last)
      BI->Last = StopI;
    if (Backup)
      ++I;
  }

  // Also check for register mask interference.
  SlotIndex Limit = BI->Last.isValid() ? BI->Last : Start;
  for (unsigned i = RegMaskSlots.size();
       i && RegMaskSlots[i-1].getDeadSlot() > Limit; --i)
    if (MachineOperand::clobbersPhysReg(RegMaskBits[i-1], PhysReg)) {
      // Register mask i-1 clobbers PhysReg after the LIU interference.
      // Model the regmask clobber as a dead def.
      BI->Last = RegMaskSlots[i-1].getDeadSlot();
      break;
    }
}
