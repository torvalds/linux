//===-- LiveStacks.cpp - Live Stack Slot Analysis -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the live stack slot analysis pass. It is analogous to
// live interval analysis except it's analyzing liveness of stack slots rather
// than registers.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
using namespace llvm;

#define DEBUG_TYPE "livestacks"

char LiveStacks::ID = 0;
INITIALIZE_PASS_BEGIN(LiveStacks, DEBUG_TYPE,
                "Live Stack Slot Analysis", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_END(LiveStacks, DEBUG_TYPE,
                "Live Stack Slot Analysis", false, false)

char &llvm::LiveStacksID = LiveStacks::ID;

void LiveStacks::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addPreserved<SlotIndexesWrapperPass>();
  AU.addRequiredTransitive<SlotIndexesWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void LiveStacks::releaseMemory() {
  // Release VNInfo memory regions, VNInfo objects don't need to be dtor'd.
  VNInfoAllocator.Reset();
  S2IMap.clear();
  S2RCMap.clear();
}

bool LiveStacks::runOnMachineFunction(MachineFunction &MF) {
  TRI = MF.getSubtarget().getRegisterInfo();
  // FIXME: No analysis is being done right now. We are relying on the
  // register allocators to provide the information.
  return false;
}

LiveInterval &
LiveStacks::getOrCreateInterval(int Slot, const TargetRegisterClass *RC) {
  assert(Slot >= 0 && "Spill slot indice must be >= 0");
  SS2IntervalMap::iterator I = S2IMap.find(Slot);
  if (I == S2IMap.end()) {
    I = S2IMap
            .emplace(
                std::piecewise_construct, std::forward_as_tuple(Slot),
                std::forward_as_tuple(Register::index2StackSlot(Slot), 0.0F))
            .first;
    S2RCMap.insert(std::make_pair(Slot, RC));
  } else {
    // Use the largest common subclass register class.
    const TargetRegisterClass *OldRC = S2RCMap[Slot];
    S2RCMap[Slot] = TRI->getCommonSubClass(OldRC, RC);
  }
  return I->second;
}

/// print - Implement the dump method.
void LiveStacks::print(raw_ostream &OS, const Module*) const {

  OS << "********** INTERVALS **********\n";
  for (const_iterator I = begin(), E = end(); I != E; ++I) {
    I->second.print(OS);
    int Slot = I->first;
    const TargetRegisterClass *RC = getIntervalRegClass(Slot);
    if (RC)
      OS << " [" << TRI->getRegClassName(RC) << "]\n";
    else
      OS << " [Unknown]\n";
  }
}
