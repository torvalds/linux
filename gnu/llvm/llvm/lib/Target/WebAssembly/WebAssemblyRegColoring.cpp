//===-- WebAssemblyRegColoring.cpp - Register coloring --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a virtual register coloring pass.
///
/// WebAssembly doesn't have a fixed number of registers, but it is still
/// desirable to minimize the total number of registers used in each function.
///
/// This code is modeled after lib/CodeGen/StackSlotColoring.cpp.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-reg-coloring"

namespace {
class WebAssemblyRegColoring final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyRegColoring() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Register Coloring";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
};
} // end anonymous namespace

char WebAssemblyRegColoring::ID = 0;
INITIALIZE_PASS(WebAssemblyRegColoring, DEBUG_TYPE,
                "Minimize number of registers used", false, false)

FunctionPass *llvm::createWebAssemblyRegColoring() {
  return new WebAssemblyRegColoring();
}

// Compute the total spill weight for VReg.
static float computeWeight(const MachineRegisterInfo *MRI,
                           const MachineBlockFrequencyInfo *MBFI,
                           unsigned VReg) {
  float Weight = 0.0f;
  for (MachineOperand &MO : MRI->reg_nodbg_operands(VReg))
    Weight += LiveIntervals::getSpillWeight(MO.isDef(), MO.isUse(), MBFI,
                                            *MO.getParent());
  return Weight;
}

// Create a map of "Register -> vector of <SlotIndex, DBG_VALUE>".
// The SlotIndex is the slot index of the next non-debug instruction or the end
// of a BB, because DBG_VALUE's don't have slot index themselves.
// Adapted from RegisterCoalescer::buildVRegToDbgValueMap.
static DenseMap<Register, std::vector<std::pair<SlotIndex, MachineInstr *>>>
buildVRegToDbgValueMap(MachineFunction &MF, const LiveIntervals *Liveness) {
  DenseMap<Register, std::vector<std::pair<SlotIndex, MachineInstr *>>>
      DbgVRegToValues;
  const SlotIndexes *Slots = Liveness->getSlotIndexes();
  SmallVector<MachineInstr *, 8> ToInsert;

  // After collecting a block of DBG_VALUEs into ToInsert, enter them into the
  // map.
  auto CloseNewDVRange = [&DbgVRegToValues, &ToInsert](SlotIndex Slot) {
    for (auto *X : ToInsert) {
      for (const auto &Op : X->debug_operands()) {
        if (Op.isReg() && Op.getReg().isVirtual())
          DbgVRegToValues[Op.getReg()].push_back({Slot, X});
      }
    }

    ToInsert.clear();
  };

  // Iterate over all instructions, collecting them into the ToInsert vector.
  // Once a non-debug instruction is found, record the slot index of the
  // collected DBG_VALUEs.
  for (auto &MBB : MF) {
    SlotIndex CurrentSlot = Slots->getMBBStartIdx(&MBB);

    for (auto &MI : MBB) {
      if (MI.isDebugValue()) {
        if (any_of(MI.debug_operands(), [](const MachineOperand &MO) {
              return MO.isReg() && MO.getReg().isVirtual();
            }))
          ToInsert.push_back(&MI);
      } else if (!MI.isDebugOrPseudoInstr()) {
        CurrentSlot = Slots->getInstructionIndex(MI);
        CloseNewDVRange(CurrentSlot);
      }
    }

    // Close range of DBG_VALUEs at the end of blocks.
    CloseNewDVRange(Slots->getMBBEndIdx(&MBB));
  }

  // Sort all DBG_VALUEs we've seen by slot number.
  for (auto &Pair : DbgVRegToValues)
    llvm::sort(Pair.second);
  return DbgVRegToValues;
}

// After register coalescing, some DBG_VALUEs will be invalid. Set them undef.
// This function has to run before the actual coalescing, i.e., the register
// changes.
static void undefInvalidDbgValues(
    const LiveIntervals *Liveness,
    ArrayRef<SmallVector<LiveInterval *, 4>> Assignments,
    DenseMap<Register, std::vector<std::pair<SlotIndex, MachineInstr *>>>
        &DbgVRegToValues) {
#ifndef NDEBUG
  DenseSet<Register> SeenRegs;
#endif
  for (const auto &CoalescedIntervals : Assignments) {
    if (CoalescedIntervals.empty())
      continue;
    for (LiveInterval *LI : CoalescedIntervals) {
      Register Reg = LI->reg();
#ifndef NDEBUG
      // Ensure we don't process the same register twice
      assert(SeenRegs.insert(Reg).second);
#endif
      auto RegMapIt = DbgVRegToValues.find(Reg);
      if (RegMapIt == DbgVRegToValues.end())
        continue;
      SlotIndex LastSlot;
      bool LastUndefResult = false;
      for (auto [Slot, DbgValue] : RegMapIt->second) {
        // All consecutive DBG_VALUEs have the same slot because the slot
        // indices they have is the one for the first non-debug instruction
        // after it, because DBG_VALUEs don't have slot index themselves. Before
        // doing live range queries, quickly check if the current DBG_VALUE has
        // the same slot index as the previous one, in which case we should do
        // the same. Note that RegMapIt->second, the vector of {SlotIndex,
        // DBG_VALUE}, is sorted by SlotIndex, which is necessary for this
        // check.
        if (Slot == LastSlot) {
          if (LastUndefResult) {
            LLVM_DEBUG(dbgs() << "Undefed: " << *DbgValue << "\n");
            DbgValue->setDebugValueUndef();
          }
          continue;
        }
        LastSlot = Slot;
        LastUndefResult = false;
        for (LiveInterval *OtherLI : CoalescedIntervals) {
          if (LI == OtherLI)
            continue;

          // This DBG_VALUE has 'Reg' (the current LiveInterval's register) as
          // its operand. If this DBG_VALUE's slot index is within other
          // registers' live ranges, this DBG_VALUE should be undefed. For
          // example, suppose %0 and %1 are to be coalesced into %0.
          //   ; %0's live range starts
          //   %0 = value_0
          //   DBG_VALUE %0, !"a", ...      (a)
          //   DBG_VALUE %1, !"b", ...      (b)
          //   use %0
          //   ; %0's live range ends
          //   ...
          //   ; %1's live range starts
          //   %1 = value_1
          //   DBG_VALUE %0, !"c", ...      (c)
          //   DBG_VALUE %1, !"d", ...      (d)
          //   use %1
          //   ; %1's live range ends
          //
          // In this code, (b) and (c) should be set to undef. After the two
          // registers are coalesced, (b) will incorrectly say the variable
          // "b"'s value is 'value_0', and (c) will also incorrectly say the
          // variable "c"'s value is value_1. Note it doesn't actually matter
          // which register they are coalesced into (%0 or %1); (b) and (c)
          // should be set to undef as well if they are coalesced into %1.
          //
          // This happens DBG_VALUEs are not included when computing live
          // ranges.
          //
          // Note that it is not possible for this DBG_VALUE to be
          // simultaneously within 'Reg''s live range and one of other coalesced
          // registers' live ranges because if their live ranges overlapped they
          // would have not been selected as a coalescing candidate in the first
          // place.
          auto *SegmentIt = OtherLI->find(Slot);
          if (SegmentIt != OtherLI->end() && SegmentIt->contains(Slot)) {
            LLVM_DEBUG(dbgs() << "Undefed: " << *DbgValue << "\n");
            DbgValue->setDebugValueUndef();
            LastUndefResult = true;
            break;
          }
        }
      }
    }
  }
}

bool WebAssemblyRegColoring::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Register Coloring **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  // If there are calls to setjmp or sigsetjmp, don't perform coloring. Virtual
  // registers could be modified before the longjmp is executed, resulting in
  // the wrong value being used afterwards.
  // TODO: Does WebAssembly need to care about setjmp for register coloring?
  if (MF.exposesReturnsTwice())
    return false;

  MachineRegisterInfo *MRI = &MF.getRegInfo();
  LiveIntervals *Liveness = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  const MachineBlockFrequencyInfo *MBFI =
      &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();

  // We don't preserve SSA form.
  MRI->leaveSSA();

  // Gather all register intervals into a list and sort them.
  unsigned NumVRegs = MRI->getNumVirtRegs();
  SmallVector<LiveInterval *, 0> SortedIntervals;
  SortedIntervals.reserve(NumVRegs);

  // Record DBG_VALUEs and their SlotIndexes.
  auto DbgVRegToValues = buildVRegToDbgValueMap(MF, Liveness);

  LLVM_DEBUG(dbgs() << "Interesting register intervals:\n");
  for (unsigned I = 0; I < NumVRegs; ++I) {
    Register VReg = Register::index2VirtReg(I);
    if (MFI.isVRegStackified(VReg))
      continue;
    // Skip unused registers, which can use $drop.
    if (MRI->use_empty(VReg))
      continue;

    LiveInterval *LI = &Liveness->getInterval(VReg);
    assert(LI->weight() == 0.0f);
    LI->setWeight(computeWeight(MRI, MBFI, VReg));
    LLVM_DEBUG(LI->dump());
    SortedIntervals.push_back(LI);
  }
  LLVM_DEBUG(dbgs() << '\n');

  // Sort them to put arguments first (since we don't want to rename live-in
  // registers), by weight next, and then by position.
  // TODO: Investigate more intelligent sorting heuristics. For starters, we
  // should try to coalesce adjacent live intervals before non-adjacent ones.
  llvm::sort(SortedIntervals, [MRI](LiveInterval *LHS, LiveInterval *RHS) {
    if (MRI->isLiveIn(LHS->reg()) != MRI->isLiveIn(RHS->reg()))
      return MRI->isLiveIn(LHS->reg());
    if (LHS->weight() != RHS->weight())
      return LHS->weight() > RHS->weight();
    if (LHS->empty() || RHS->empty())
      return !LHS->empty() && RHS->empty();
    return *LHS < *RHS;
  });

  LLVM_DEBUG(dbgs() << "Coloring register intervals:\n");
  SmallVector<unsigned, 16> SlotMapping(SortedIntervals.size(), -1u);
  SmallVector<SmallVector<LiveInterval *, 4>, 16> Assignments(
      SortedIntervals.size());
  BitVector UsedColors(SortedIntervals.size());
  bool Changed = false;
  for (size_t I = 0, E = SortedIntervals.size(); I < E; ++I) {
    LiveInterval *LI = SortedIntervals[I];
    Register Old = LI->reg();
    size_t Color = I;
    const TargetRegisterClass *RC = MRI->getRegClass(Old);

    // Check if it's possible to reuse any of the used colors.
    if (!MRI->isLiveIn(Old))
      for (unsigned C : UsedColors.set_bits()) {
        if (MRI->getRegClass(SortedIntervals[C]->reg()) != RC)
          continue;
        for (LiveInterval *OtherLI : Assignments[C])
          if (!OtherLI->empty() && OtherLI->overlaps(*LI))
            goto continue_outer;
        Color = C;
        break;
      continue_outer:;
      }

    Register New = SortedIntervals[Color]->reg();
    SlotMapping[I] = New;
    Changed |= Old != New;
    UsedColors.set(Color);
    Assignments[Color].push_back(LI);
    // If we reassigned the stack pointer, update the debug frame base info.
    if (Old != New && MFI.isFrameBaseVirtual() && MFI.getFrameBaseVreg() == Old)
      MFI.setFrameBaseVreg(New);
    LLVM_DEBUG(dbgs() << "Assigning vreg" << Register::virtReg2Index(LI->reg())
                      << " to vreg" << Register::virtReg2Index(New) << "\n");
  }
  if (!Changed)
    return false;

  // Set DBG_VALUEs that will be invalid after coalescing to undef.
  undefInvalidDbgValues(Liveness, Assignments, DbgVRegToValues);

  // Rewrite register operands.
  for (size_t I = 0, E = SortedIntervals.size(); I < E; ++I) {
    Register Old = SortedIntervals[I]->reg();
    unsigned New = SlotMapping[I];
    if (Old != New)
      MRI->replaceRegWith(Old, New);
  }
  return true;
}
