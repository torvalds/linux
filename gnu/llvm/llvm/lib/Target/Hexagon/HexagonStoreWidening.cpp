//===- HexagonStoreWidening.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Replace sequences of "narrow" stores to adjacent memory locations with
// a fewer "wide" stores that have the same effect.
// For example, replace:
//   S4_storeirb_io  %100, 0, 0   ; store-immediate-byte
//   S4_storeirb_io  %100, 1, 0   ; store-immediate-byte
// with
//   S4_storeirh_io  %100, 0, 0   ; store-immediate-halfword
// The above is the general idea.  The actual cases handled by the code
// may be a bit more complex.
// The purpose of this pass is to reduce the number of outstanding stores,
// or as one could say, "reduce store queue pressure".  Also, wide stores
// mean fewer stores, and since there are only two memory instructions allowed
// per packet, it also means fewer packets, and ultimately fewer cycles.
//===---------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <vector>

#define DEBUG_TYPE "hexagon-widen-stores"

using namespace llvm;

namespace llvm {

FunctionPass *createHexagonStoreWidening();
void initializeHexagonStoreWideningPass(PassRegistry&);

} // end namespace llvm

namespace {

  struct HexagonStoreWidening : public MachineFunctionPass {
    const HexagonInstrInfo      *TII;
    const HexagonRegisterInfo   *TRI;
    const MachineRegisterInfo   *MRI;
    AliasAnalysis               *AA;
    MachineFunction             *MF;

  public:
    static char ID;

    HexagonStoreWidening() : MachineFunctionPass(ID) {
      initializeHexagonStoreWideningPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    StringRef getPassName() const override { return "Hexagon Store Widening"; }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<AAResultsWrapperPass>();
      AU.addPreserved<AAResultsWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    static bool handledStoreType(const MachineInstr *MI);

  private:
    static const int MaxWideSize = 4;

    using InstrGroup = std::vector<MachineInstr *>;
    using InstrGroupList = std::vector<InstrGroup>;

    bool instrAliased(InstrGroup &Stores, const MachineMemOperand &MMO);
    bool instrAliased(InstrGroup &Stores, const MachineInstr *MI);
    void createStoreGroup(MachineInstr *BaseStore, InstrGroup::iterator Begin,
        InstrGroup::iterator End, InstrGroup &Group);
    void createStoreGroups(MachineBasicBlock &MBB,
        InstrGroupList &StoreGroups);
    bool processBasicBlock(MachineBasicBlock &MBB);
    bool processStoreGroup(InstrGroup &Group);
    bool selectStores(InstrGroup::iterator Begin, InstrGroup::iterator End,
        InstrGroup &OG, unsigned &TotalSize, unsigned MaxSize);
    bool createWideStores(InstrGroup &OG, InstrGroup &NG, unsigned TotalSize);
    bool replaceStores(InstrGroup &OG, InstrGroup &NG);
    bool storesAreAdjacent(const MachineInstr *S1, const MachineInstr *S2);
  };

} // end anonymous namespace

char HexagonStoreWidening::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonStoreWidening, "hexagon-widen-stores",
                "Hexason Store Widening", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(HexagonStoreWidening, "hexagon-widen-stores",
                "Hexagon Store Widening", false, false)

// Some local helper functions...
static unsigned getBaseAddressRegister(const MachineInstr *MI) {
  const MachineOperand &MO = MI->getOperand(0);
  assert(MO.isReg() && "Expecting register operand");
  return MO.getReg();
}

static int64_t getStoreOffset(const MachineInstr *MI) {
  unsigned OpC = MI->getOpcode();
  assert(HexagonStoreWidening::handledStoreType(MI) && "Unhandled opcode");

  switch (OpC) {
    case Hexagon::S4_storeirb_io:
    case Hexagon::S4_storeirh_io:
    case Hexagon::S4_storeiri_io: {
      const MachineOperand &MO = MI->getOperand(1);
      assert(MO.isImm() && "Expecting immediate offset");
      return MO.getImm();
    }
  }
  dbgs() << *MI;
  llvm_unreachable("Store offset calculation missing for a handled opcode");
  return 0;
}

static const MachineMemOperand &getStoreTarget(const MachineInstr *MI) {
  assert(!MI->memoperands_empty() && "Expecting memory operands");
  return **MI->memoperands_begin();
}

// Filtering function: any stores whose opcodes are not "approved" of by
// this function will not be subjected to widening.
inline bool HexagonStoreWidening::handledStoreType(const MachineInstr *MI) {
  // For now, only handle stores of immediate values.
  // Also, reject stores to stack slots.
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
    case Hexagon::S4_storeirb_io:
    case Hexagon::S4_storeirh_io:
    case Hexagon::S4_storeiri_io:
      // Base address must be a register. (Implement FI later.)
      return MI->getOperand(0).isReg();
    default:
      return false;
  }
}

// Check if the machine memory operand MMO is aliased with any of the
// stores in the store group Stores.
bool HexagonStoreWidening::instrAliased(InstrGroup &Stores,
      const MachineMemOperand &MMO) {
  if (!MMO.getValue())
    return true;

  MemoryLocation L(MMO.getValue(), MMO.getSize(), MMO.getAAInfo());

  for (auto *SI : Stores) {
    const MachineMemOperand &SMO = getStoreTarget(SI);
    if (!SMO.getValue())
      return true;

    MemoryLocation SL(SMO.getValue(), SMO.getSize(), SMO.getAAInfo());
    if (!AA->isNoAlias(L, SL))
      return true;
  }

  return false;
}

// Check if the machine instruction MI accesses any storage aliased with
// any store in the group Stores.
bool HexagonStoreWidening::instrAliased(InstrGroup &Stores,
      const MachineInstr *MI) {
  for (auto &I : MI->memoperands())
    if (instrAliased(Stores, *I))
      return true;
  return false;
}

// Inspect a machine basic block, and generate store groups out of stores
// encountered in the block.
//
// A store group is a group of stores that use the same base register,
// and which can be reordered within that group without altering the
// semantics of the program.  A single store group could be widened as
// a whole, if there existed a single store instruction with the same
// semantics as the entire group.  In many cases, a single store group
// may need more than one wide store.
void HexagonStoreWidening::createStoreGroups(MachineBasicBlock &MBB,
      InstrGroupList &StoreGroups) {
  InstrGroup AllInsns;

  // Copy all instruction pointers from the basic block to a temporary
  // list.  This will allow operating on the list, and modifying its
  // elements without affecting the basic block.
  for (auto &I : MBB)
    AllInsns.push_back(&I);

  // Traverse all instructions in the AllInsns list, and if we encounter
  // a store, then try to create a store group starting at that instruction
  // i.e. a sequence of independent stores that can be widened.
  for (auto I = AllInsns.begin(), E = AllInsns.end(); I != E; ++I) {
    MachineInstr *MI = *I;
    // Skip null pointers (processed instructions).
    if (!MI || !handledStoreType(MI))
      continue;

    // Found a store.  Try to create a store group.
    InstrGroup G;
    createStoreGroup(MI, I+1, E, G);
    if (G.size() > 1)
      StoreGroups.push_back(G);
  }
}

// Create a single store group.  The stores need to be independent between
// themselves, and also there cannot be other instructions between them
// that could read or modify storage being stored into.
void HexagonStoreWidening::createStoreGroup(MachineInstr *BaseStore,
      InstrGroup::iterator Begin, InstrGroup::iterator End, InstrGroup &Group) {
  assert(handledStoreType(BaseStore) && "Unexpected instruction");
  unsigned BaseReg = getBaseAddressRegister(BaseStore);
  InstrGroup Other;

  Group.push_back(BaseStore);

  for (auto I = Begin; I != End; ++I) {
    MachineInstr *MI = *I;
    if (!MI)
      continue;

    if (handledStoreType(MI)) {
      // If this store instruction is aliased with anything already in the
      // group, terminate the group now.
      if (instrAliased(Group, getStoreTarget(MI)))
        return;
      // If this store is aliased to any of the memory instructions we have
      // seen so far (that are not a part of this group), terminate the group.
      if (instrAliased(Other, getStoreTarget(MI)))
        return;

      unsigned BR = getBaseAddressRegister(MI);
      if (BR == BaseReg) {
        Group.push_back(MI);
        *I = nullptr;
        continue;
      }
    }

    // Assume calls are aliased to everything.
    if (MI->isCall() || MI->hasUnmodeledSideEffects())
      return;

    if (MI->mayLoadOrStore()) {
      if (MI->hasOrderedMemoryRef() || instrAliased(Group, MI))
        return;
      Other.push_back(MI);
    }
  } // for
}

// Check if store instructions S1 and S2 are adjacent.  More precisely,
// S2 has to access memory immediately following that accessed by S1.
bool HexagonStoreWidening::storesAreAdjacent(const MachineInstr *S1,
      const MachineInstr *S2) {
  if (!handledStoreType(S1) || !handledStoreType(S2))
    return false;

  const MachineMemOperand &S1MO = getStoreTarget(S1);

  // Currently only handling immediate stores.
  int Off1 = S1->getOperand(1).getImm();
  int Off2 = S2->getOperand(1).getImm();

  return (Off1 >= 0) ? Off1 + S1MO.getSize().getValue() == unsigned(Off2)
                     : int(Off1 + S1MO.getSize().getValue()) == Off2;
}

/// Given a sequence of adjacent stores, and a maximum size of a single wide
/// store, pick a group of stores that  can be replaced by a single store
/// of size not exceeding MaxSize.  The selected sequence will be recorded
/// in OG ("old group" of instructions).
/// OG should be empty on entry, and should be left empty if the function
/// fails.
bool HexagonStoreWidening::selectStores(InstrGroup::iterator Begin,
      InstrGroup::iterator End, InstrGroup &OG, unsigned &TotalSize,
      unsigned MaxSize) {
  assert(Begin != End && "No instructions to analyze");
  assert(OG.empty() && "Old group not empty on entry");

  if (std::distance(Begin, End) <= 1)
    return false;

  MachineInstr *FirstMI = *Begin;
  assert(!FirstMI->memoperands_empty() && "Expecting some memory operands");
  const MachineMemOperand &FirstMMO = getStoreTarget(FirstMI);
  unsigned Alignment = FirstMMO.getAlign().value();
  unsigned SizeAccum = FirstMMO.getSize().getValue();
  unsigned FirstOffset = getStoreOffset(FirstMI);

  // The initial value of SizeAccum should always be a power of 2.
  assert(isPowerOf2_32(SizeAccum) && "First store size not a power of 2");

  // If the size of the first store equals to or exceeds the limit, do nothing.
  if (SizeAccum >= MaxSize)
    return false;

  // If the size of the first store is greater than or equal to the address
  // stored to, then the store cannot be made any wider.
  if (SizeAccum >= Alignment)
    return false;

  // The offset of a store will put restrictions on how wide the store can be.
  // Offsets in stores of size 2^n bytes need to have the n lowest bits be 0.
  // If the first store already exhausts the offset limits, quit.  Test this
  // by checking if the next wider size would exceed the limit.
  if ((2*SizeAccum-1) & FirstOffset)
    return false;

  OG.push_back(FirstMI);
  MachineInstr *S1 = FirstMI;

  // Pow2Num will be the largest number of elements in OG such that the sum
  // of sizes of stores 0...Pow2Num-1 will be a power of 2.
  unsigned Pow2Num = 1;
  unsigned Pow2Size = SizeAccum;

  // Be greedy: keep accumulating stores as long as they are to adjacent
  // memory locations, and as long as the total number of bytes stored
  // does not exceed the limit (MaxSize).
  // Keep track of when the total size covered is a power of 2, since
  // this is a size a single store can cover.
  for (InstrGroup::iterator I = Begin + 1; I != End; ++I) {
    MachineInstr *S2 = *I;
    // Stores are sorted, so if S1 and S2 are not adjacent, there won't be
    // any other store to fill the "hole".
    if (!storesAreAdjacent(S1, S2))
      break;

    unsigned S2Size = getStoreTarget(S2).getSize().getValue();
    if (SizeAccum + S2Size > std::min(MaxSize, Alignment))
      break;

    OG.push_back(S2);
    SizeAccum += S2Size;
    if (isPowerOf2_32(SizeAccum)) {
      Pow2Num = OG.size();
      Pow2Size = SizeAccum;
    }
    if ((2*Pow2Size-1) & FirstOffset)
      break;

    S1 = S2;
  }

  // The stores don't add up to anything that can be widened.  Clean up.
  if (Pow2Num <= 1) {
    OG.clear();
    return false;
  }

  // Only leave the stored being widened.
  OG.resize(Pow2Num);
  TotalSize = Pow2Size;
  return true;
}

/// Given an "old group" OG of stores, create a "new group" NG of instructions
/// to replace them.  Ideally, NG would only have a single instruction in it,
/// but that may only be possible for store-immediate.
bool HexagonStoreWidening::createWideStores(InstrGroup &OG, InstrGroup &NG,
      unsigned TotalSize) {
  // XXX Current limitations:
  // - only expect stores of immediate values in OG,
  // - only handle a TotalSize of up to 4.

  if (TotalSize > 4)
    return false;

  unsigned Acc = 0;  // Value accumulator.
  unsigned Shift = 0;

  for (MachineInstr *MI : OG) {
    const MachineMemOperand &MMO = getStoreTarget(MI);
    MachineOperand &SO = MI->getOperand(2);  // Source.
    assert(SO.isImm() && "Expecting an immediate operand");

    unsigned NBits = MMO.getSize().getValue() * 8;
    unsigned Mask = (0xFFFFFFFFU >> (32-NBits));
    unsigned Val = (SO.getImm() & Mask) << Shift;
    Acc |= Val;
    Shift += NBits;
  }

  MachineInstr *FirstSt = OG.front();
  DebugLoc DL = OG.back()->getDebugLoc();
  const MachineMemOperand &OldM = getStoreTarget(FirstSt);
  MachineMemOperand *NewM =
      MF->getMachineMemOperand(OldM.getPointerInfo(), OldM.getFlags(),
                               TotalSize, OldM.getAlign(), OldM.getAAInfo());

  if (Acc < 0x10000) {
    // Create mem[hw] = #Acc
    unsigned WOpc = (TotalSize == 2) ? Hexagon::S4_storeirh_io :
                    (TotalSize == 4) ? Hexagon::S4_storeiri_io : 0;
    assert(WOpc && "Unexpected size");

    int Val = (TotalSize == 2) ? int16_t(Acc) : int(Acc);
    const MCInstrDesc &StD = TII->get(WOpc);
    MachineOperand &MR = FirstSt->getOperand(0);
    int64_t Off = FirstSt->getOperand(1).getImm();
    MachineInstr *StI =
        BuildMI(*MF, DL, StD)
            .addReg(MR.getReg(), getKillRegState(MR.isKill()), MR.getSubReg())
            .addImm(Off)
            .addImm(Val);
    StI->addMemOperand(*MF, NewM);
    NG.push_back(StI);
  } else {
    // Create vreg = A2_tfrsi #Acc; mem[hw] = vreg
    const MCInstrDesc &TfrD = TII->get(Hexagon::A2_tfrsi);
    const TargetRegisterClass *RC = TII->getRegClass(TfrD, 0, TRI, *MF);
    Register VReg = MF->getRegInfo().createVirtualRegister(RC);
    MachineInstr *TfrI = BuildMI(*MF, DL, TfrD, VReg)
                           .addImm(int(Acc));
    NG.push_back(TfrI);

    unsigned WOpc = (TotalSize == 2) ? Hexagon::S2_storerh_io :
                    (TotalSize == 4) ? Hexagon::S2_storeri_io : 0;
    assert(WOpc && "Unexpected size");

    const MCInstrDesc &StD = TII->get(WOpc);
    MachineOperand &MR = FirstSt->getOperand(0);
    int64_t Off = FirstSt->getOperand(1).getImm();
    MachineInstr *StI =
        BuildMI(*MF, DL, StD)
            .addReg(MR.getReg(), getKillRegState(MR.isKill()), MR.getSubReg())
            .addImm(Off)
            .addReg(VReg, RegState::Kill);
    StI->addMemOperand(*MF, NewM);
    NG.push_back(StI);
  }

  return true;
}

// Replace instructions from the old group OG with instructions from the
// new group NG.  Conceptually, remove all instructions in OG, and then
// insert all instructions in NG, starting at where the first instruction
// from OG was (in the order in which they appeared in the basic block).
// (The ordering in OG does not have to match the order in the basic block.)
bool HexagonStoreWidening::replaceStores(InstrGroup &OG, InstrGroup &NG) {
  LLVM_DEBUG({
    dbgs() << "Replacing:\n";
    for (auto I : OG)
      dbgs() << "  " << *I;
    dbgs() << "with\n";
    for (auto I : NG)
      dbgs() << "  " << *I;
  });

  MachineBasicBlock *MBB = OG.back()->getParent();
  MachineBasicBlock::iterator InsertAt = MBB->end();

  // Need to establish the insertion point.  The best one is right before
  // the first store in the OG, but in the order in which the stores occur
  // in the program list.  Since the ordering in OG does not correspond
  // to the order in the program list, we need to do some work to find
  // the insertion point.

  // Create a set of all instructions in OG (for quick lookup).
  SmallPtrSet<MachineInstr*, 4> InstrSet;
  for (auto *I : OG)
    InstrSet.insert(I);

  // Traverse the block, until we hit an instruction from OG.
  for (auto &I : *MBB) {
    if (InstrSet.count(&I)) {
      InsertAt = I;
      break;
    }
  }

  assert((InsertAt != MBB->end()) && "Cannot locate any store from the group");

  bool AtBBStart = false;

  // InsertAt points at the first instruction that will be removed.  We need
  // to move it out of the way, so it remains valid after removing all the
  // old stores, and so we are able to recover it back to the proper insertion
  // position.
  if (InsertAt != MBB->begin())
    --InsertAt;
  else
    AtBBStart = true;

  for (auto *I : OG)
    I->eraseFromParent();

  if (!AtBBStart)
    ++InsertAt;
  else
    InsertAt = MBB->begin();

  for (auto *I : NG)
    MBB->insert(InsertAt, I);

  return true;
}

// Break up the group into smaller groups, each of which can be replaced by
// a single wide store.  Widen each such smaller group and replace the old
// instructions with the widened ones.
bool HexagonStoreWidening::processStoreGroup(InstrGroup &Group) {
  bool Changed = false;
  InstrGroup::iterator I = Group.begin(), E = Group.end();
  InstrGroup OG, NG;   // Old and new groups.
  unsigned CollectedSize;

  while (I != E) {
    OG.clear();
    NG.clear();

    bool Succ = selectStores(I++, E, OG, CollectedSize, MaxWideSize) &&
                createWideStores(OG, NG, CollectedSize)              &&
                replaceStores(OG, NG);
    if (!Succ)
      continue;

    assert(OG.size() > 1 && "Created invalid group");
    assert(distance(I, E)+1 >= int(OG.size()) && "Too many elements");
    I += OG.size()-1;

    Changed = true;
  }

  return Changed;
}

// Process a single basic block: create the store groups, and replace them
// with the widened stores, if possible.  Processing of each basic block
// is independent from processing of any other basic block.  This transfor-
// mation could be stopped after having processed any basic block without
// any ill effects (other than not having performed widening in the unpro-
// cessed blocks).  Also, the basic blocks can be processed in any order.
bool HexagonStoreWidening::processBasicBlock(MachineBasicBlock &MBB) {
  InstrGroupList SGs;
  bool Changed = false;

  createStoreGroups(MBB, SGs);

  auto Less = [] (const MachineInstr *A, const MachineInstr *B) -> bool {
    return getStoreOffset(A) < getStoreOffset(B);
  };
  for (auto &G : SGs) {
    assert(G.size() > 1 && "Store group with fewer than 2 elements");
    llvm::sort(G, Less);

    Changed |= processStoreGroup(G);
  }

  return Changed;
}

bool HexagonStoreWidening::runOnMachineFunction(MachineFunction &MFn) {
  if (skipFunction(MFn.getFunction()))
    return false;

  MF = &MFn;
  auto &ST = MFn.getSubtarget<HexagonSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MRI = &MFn.getRegInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  bool Changed = false;

  for (auto &B : MFn)
    Changed |= processBasicBlock(B);

  return Changed;
}

FunctionPass *llvm::createHexagonStoreWidening() {
  return new HexagonStoreWidening();
}
