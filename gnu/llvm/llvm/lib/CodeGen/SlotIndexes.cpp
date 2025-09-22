//===-- SlotIndexes.cpp - Slot Indexes Pass  ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "slotindexes"

AnalysisKey SlotIndexesAnalysis::Key;

SlotIndexesAnalysis::Result
SlotIndexesAnalysis::run(MachineFunction &MF,
                         MachineFunctionAnalysisManager &) {
  return Result(MF);
}

PreservedAnalyses
SlotIndexesPrinterPass::run(MachineFunction &MF,
                            MachineFunctionAnalysisManager &MFAM) {
  OS << "Slot indexes in machine function: " << MF.getName() << '\n';
  MFAM.getResult<SlotIndexesAnalysis>(MF).print(OS);
  return PreservedAnalyses::all();
}
char SlotIndexesWrapperPass::ID = 0;

SlotIndexesWrapperPass::SlotIndexesWrapperPass() : MachineFunctionPass(ID) {
  initializeSlotIndexesWrapperPassPass(*PassRegistry::getPassRegistry());
}

SlotIndexes::~SlotIndexes() {
  // The indexList's nodes are all allocated in the BumpPtrAllocator.
  indexList.clear();
}

INITIALIZE_PASS(SlotIndexesWrapperPass, DEBUG_TYPE, "Slot index numbering",
                false, false)

STATISTIC(NumLocalRenum,  "Number of local renumberings");

void SlotIndexesWrapperPass::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(au);
}

void SlotIndexes::clear() {
  mi2iMap.clear();
  MBBRanges.clear();
  idx2MBBMap.clear();
  indexList.clear();
  ileAllocator.Reset();
}

void SlotIndexes::analyze(MachineFunction &fn) {

  // Compute numbering as follows:
  // Grab an iterator to the start of the index list.
  // Iterate over all MBBs, and within each MBB all MIs, keeping the MI
  // iterator in lock-step (though skipping it over indexes which have
  // null pointers in the instruction field).
  // At each iteration assert that the instruction pointed to in the index
  // is the same one pointed to by the MI iterator. This

  // FIXME: This can be simplified. The mi2iMap_, Idx2MBBMap, etc. should
  // only need to be set up once after the first numbering is computed.

  mf = &fn;

  // Check that the list contains only the sentinel.
  assert(indexList.empty() && "Index list non-empty at initial numbering?");
  assert(idx2MBBMap.empty() &&
         "Index -> MBB mapping non-empty at initial numbering?");
  assert(MBBRanges.empty() &&
         "MBB -> Index mapping non-empty at initial numbering?");
  assert(mi2iMap.empty() &&
         "MachineInstr -> Index mapping non-empty at initial numbering?");

  unsigned index = 0;
  MBBRanges.resize(mf->getNumBlockIDs());
  idx2MBBMap.reserve(mf->size());

  indexList.push_back(*createEntry(nullptr, index));

  // Iterate over the function.
  for (MachineBasicBlock &MBB : *mf) {
    // Insert an index for the MBB start.
    SlotIndex blockStartIndex(&indexList.back(), SlotIndex::Slot_Block);

    for (MachineInstr &MI : MBB) {
      if (MI.isDebugOrPseudoInstr())
        continue;

      // Insert a store index for the instr.
      indexList.push_back(*createEntry(&MI, index += SlotIndex::InstrDist));

      // Save this base index in the maps.
      mi2iMap.insert(std::make_pair(
          &MI, SlotIndex(&indexList.back(), SlotIndex::Slot_Block)));
    }

    // We insert one blank instructions between basic blocks.
    indexList.push_back(*createEntry(nullptr, index += SlotIndex::InstrDist));

    MBBRanges[MBB.getNumber()].first = blockStartIndex;
    MBBRanges[MBB.getNumber()].second = SlotIndex(&indexList.back(),
                                                   SlotIndex::Slot_Block);
    idx2MBBMap.push_back(IdxMBBPair(blockStartIndex, &MBB));
  }

  // Sort the Idx2MBBMap
  llvm::sort(idx2MBBMap, less_first());

  LLVM_DEBUG(mf->print(dbgs(), this));
}

void SlotIndexes::removeMachineInstrFromMaps(MachineInstr &MI,
                                             bool AllowBundled) {
  assert((AllowBundled || !MI.isBundledWithPred()) &&
         "Use removeSingleMachineInstrFromMaps() instead");
  Mi2IndexMap::iterator mi2iItr = mi2iMap.find(&MI);
  if (mi2iItr == mi2iMap.end())
    return;

  SlotIndex MIIndex = mi2iItr->second;
  IndexListEntry &MIEntry = *MIIndex.listEntry();
  assert(MIEntry.getInstr() == &MI && "Instruction indexes broken.");
  mi2iMap.erase(mi2iItr);
  // FIXME: Eventually we want to actually delete these indexes.
  MIEntry.setInstr(nullptr);
}

void SlotIndexes::removeSingleMachineInstrFromMaps(MachineInstr &MI) {
  Mi2IndexMap::iterator mi2iItr = mi2iMap.find(&MI);
  if (mi2iItr == mi2iMap.end())
    return;

  SlotIndex MIIndex = mi2iItr->second;
  IndexListEntry &MIEntry = *MIIndex.listEntry();
  assert(MIEntry.getInstr() == &MI && "Instruction indexes broken.");
  mi2iMap.erase(mi2iItr);

  // When removing the first instruction of a bundle update mapping to next
  // instruction.
  if (MI.isBundledWithSucc()) {
    // Only the first instruction of a bundle should have an index assigned.
    assert(!MI.isBundledWithPred() && "Should be first bundle instruction");

    MachineBasicBlock::instr_iterator Next = std::next(MI.getIterator());
    MachineInstr &NextMI = *Next;
    MIEntry.setInstr(&NextMI);
    mi2iMap.insert(std::make_pair(&NextMI, MIIndex));
    return;
  } else {
    // FIXME: Eventually we want to actually delete these indexes.
    MIEntry.setInstr(nullptr);
  }
}

// Renumber indexes locally after curItr was inserted, but failed to get a new
// index.
void SlotIndexes::renumberIndexes(IndexList::iterator curItr) {
  // Number indexes with half the default spacing so we can catch up quickly.
  const unsigned Space = SlotIndex::InstrDist/2;
  static_assert((Space & 3) == 0, "InstrDist must be a multiple of 2*NUM");

  IndexList::iterator startItr = std::prev(curItr);
  unsigned index = startItr->getIndex();
  do {
    curItr->setIndex(index += Space);
    ++curItr;
    // If the next index is bigger, we have caught up.
  } while (curItr != indexList.end() && curItr->getIndex() <= index);

  LLVM_DEBUG(dbgs() << "\n*** Renumbered SlotIndexes " << startItr->getIndex()
                    << '-' << index << " ***\n");
  ++NumLocalRenum;
}

// Repair indexes after adding and removing instructions.
void SlotIndexes::repairIndexesInRange(MachineBasicBlock *MBB,
                                       MachineBasicBlock::iterator Begin,
                                       MachineBasicBlock::iterator End) {
  bool includeStart = (Begin == MBB->begin());
  SlotIndex startIdx;
  if (includeStart)
    startIdx = getMBBStartIdx(MBB);
  else
    startIdx = getInstructionIndex(*--Begin);

  SlotIndex endIdx;
  if (End == MBB->end())
    endIdx = getMBBEndIdx(MBB);
  else
    endIdx = getInstructionIndex(*End);

  // FIXME: Conceptually, this code is implementing an iterator on MBB that
  // optionally includes an additional position prior to MBB->begin(), indicated
  // by the includeStart flag. This is done so that we can iterate MIs in a MBB
  // in parallel with SlotIndexes, but there should be a better way to do this.
  IndexList::iterator ListB = startIdx.listEntry()->getIterator();
  IndexList::iterator ListI = endIdx.listEntry()->getIterator();
  MachineBasicBlock::iterator MBBI = End;
  bool pastStart = false;
  while (ListI != ListB || MBBI != Begin || (includeStart && !pastStart)) {
    assert(ListI->getIndex() >= startIdx.getIndex() &&
           (includeStart || !pastStart) &&
           "Decremented past the beginning of region to repair.");

    MachineInstr *SlotMI = ListI->getInstr();
    MachineInstr *MI = (MBBI != MBB->end() && !pastStart) ? &*MBBI : nullptr;
    bool MBBIAtBegin = MBBI == Begin && (!includeStart || pastStart);

    if (SlotMI == MI && !MBBIAtBegin) {
      --ListI;
      if (MBBI != Begin)
        --MBBI;
      else
        pastStart = true;
    } else if (MI && !mi2iMap.contains(MI)) {
      if (MBBI != Begin)
        --MBBI;
      else
        pastStart = true;
    } else {
      --ListI;
      if (SlotMI)
        removeMachineInstrFromMaps(*SlotMI);
    }
  }

  // In theory this could be combined with the previous loop, but it is tricky
  // to update the IndexList while we are iterating it.
  for (MachineBasicBlock::iterator I = End; I != Begin;) {
    --I;
    MachineInstr &MI = *I;
    if (!MI.isDebugOrPseudoInstr() && !mi2iMap.contains(&MI))
      insertMachineInstrInMaps(MI);
  }
}

void SlotIndexes::packIndexes() {
  for (auto [Index, Entry] : enumerate(indexList))
    Entry.setIndex(Index * SlotIndex::InstrDist);
}

void SlotIndexes::print(raw_ostream &OS) const {
  for (const IndexListEntry &ILE : indexList) {
    OS << ILE.getIndex() << ' ';

    if (ILE.getInstr())
      OS << *ILE.getInstr();
    else
      OS << '\n';
  }

  for (unsigned i = 0, e = MBBRanges.size(); i != e; ++i)
    OS << "%bb." << i << "\t[" << MBBRanges[i].first << ';'
       << MBBRanges[i].second << ")\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SlotIndexes::dump() const { print(dbgs()); }
#endif

// Print a SlotIndex to a raw_ostream.
void SlotIndex::print(raw_ostream &os) const {
  if (isValid())
    os << listEntry()->getIndex() << "Berd"[getSlot()];
  else
    os << "invalid";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// Dump a SlotIndex to stderr.
LLVM_DUMP_METHOD void SlotIndex::dump() const {
  print(dbgs());
  dbgs() << "\n";
}
#endif
