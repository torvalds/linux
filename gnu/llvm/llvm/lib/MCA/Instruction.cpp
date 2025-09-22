//===--------------------- Instruction.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines abstractions used by the Pipeline to model register reads,
// register writes and instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Instruction.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

void WriteState::writeStartEvent(unsigned IID, MCPhysReg RegID,
                                 unsigned Cycles) {
  CRD.IID = IID;
  CRD.RegID = RegID;
  CRD.Cycles = Cycles;
  DependentWriteCyclesLeft = Cycles;
  DependentWrite = nullptr;
}

void ReadState::writeStartEvent(unsigned IID, MCPhysReg RegID,
                                unsigned Cycles) {
  assert(DependentWrites);
  assert(CyclesLeft == UNKNOWN_CYCLES);

  // This read may be dependent on more than one write. This typically occurs
  // when a definition is the result of multiple writes where at least one
  // write does a partial register update.
  // The HW is forced to do some extra bookkeeping to track of all the
  // dependent writes, and implement a merging scheme for the partial writes.
  --DependentWrites;
  if (TotalCycles < Cycles) {
    CRD.IID = IID;
    CRD.RegID = RegID;
    CRD.Cycles = Cycles;
    TotalCycles = Cycles;
  }

  if (!DependentWrites) {
    CyclesLeft = TotalCycles;
    IsReady = !CyclesLeft;
  }
}

void WriteState::onInstructionIssued(unsigned IID) {
  assert(CyclesLeft == UNKNOWN_CYCLES);
  // Update the number of cycles left based on the WriteDescriptor info.
  CyclesLeft = getLatency();

  // Now that the time left before write-back is known, notify
  // all the users.
  for (const std::pair<ReadState *, int> &User : Users) {
    ReadState *RS = User.first;
    unsigned ReadCycles = std::max(0, CyclesLeft - User.second);
    RS->writeStartEvent(IID, RegisterID, ReadCycles);
  }

  // Notify any writes that are in a false dependency with this write.
  if (PartialWrite)
    PartialWrite->writeStartEvent(IID, RegisterID, CyclesLeft);
}

void WriteState::addUser(unsigned IID, ReadState *User, int ReadAdvance) {
  // If CyclesLeft is different than -1, then we don't need to
  // update the list of users. We can just notify the user with
  // the actual number of cycles left (which may be zero).
  if (CyclesLeft != UNKNOWN_CYCLES) {
    unsigned ReadCycles = std::max(0, CyclesLeft - ReadAdvance);
    User->writeStartEvent(IID, RegisterID, ReadCycles);
    return;
  }

  Users.emplace_back(User, ReadAdvance);
}

void WriteState::addUser(unsigned IID, WriteState *User) {
  if (CyclesLeft != UNKNOWN_CYCLES) {
    User->writeStartEvent(IID, RegisterID, std::max(0, CyclesLeft));
    return;
  }

  assert(!PartialWrite && "PartialWrite already set!");
  PartialWrite = User;
  User->setDependentWrite(this);
}

void WriteState::cycleEvent() {
  // Note: CyclesLeft can be a negative number. It is an error to
  // make it an unsigned quantity because users of this write may
  // specify a negative ReadAdvance.
  if (CyclesLeft != UNKNOWN_CYCLES)
    CyclesLeft--;

  if (DependentWriteCyclesLeft)
    DependentWriteCyclesLeft--;
}

void ReadState::cycleEvent() {
  // Update the total number of cycles.
  if (DependentWrites && TotalCycles) {
    --TotalCycles;
    return;
  }

  // Bail out immediately if we don't know how many cycles are left.
  if (CyclesLeft == UNKNOWN_CYCLES)
    return;

  if (CyclesLeft) {
    --CyclesLeft;
    IsReady = !CyclesLeft;
  }
}

#ifndef NDEBUG
void WriteState::dump() const {
  dbgs() << "{ OpIdx=" << WD->OpIndex << ", Lat=" << getLatency() << ", RegID "
         << getRegisterID() << ", Cycles Left=" << getCyclesLeft() << " }";
}
#endif

const CriticalDependency &Instruction::computeCriticalRegDep() {
  if (CriticalRegDep.Cycles)
    return CriticalRegDep;

  unsigned MaxLatency = 0;
  for (const WriteState &WS : getDefs()) {
    const CriticalDependency &WriteCRD = WS.getCriticalRegDep();
    if (WriteCRD.Cycles > MaxLatency)
      CriticalRegDep = WriteCRD;
  }

  for (const ReadState &RS : getUses()) {
    const CriticalDependency &ReadCRD = RS.getCriticalRegDep();
    if (ReadCRD.Cycles > MaxLatency)
      CriticalRegDep = ReadCRD;
  }

  return CriticalRegDep;
}

void Instruction::reset() {
  // Note that this won't clear read/write descriptors
  // or other non-trivial fields
  Stage = IS_INVALID;
  CyclesLeft = UNKNOWN_CYCLES;
  clearOptimizableMove();
  RCUTokenID = 0;
  LSUTokenID = 0;
  CriticalResourceMask = 0;
  IsEliminated = false;
}

void Instruction::dispatch(unsigned RCUToken) {
  assert(Stage == IS_INVALID);
  Stage = IS_DISPATCHED;
  RCUTokenID = RCUToken;

  // Check if input operands are already available.
  if (updateDispatched())
    updatePending();
}

void Instruction::execute(unsigned IID) {
  assert(Stage == IS_READY);
  Stage = IS_EXECUTING;

  // Set the cycles left before the write-back stage.
  CyclesLeft = getLatency();

  for (WriteState &WS : getDefs())
    WS.onInstructionIssued(IID);

  // Transition to the "executed" stage if this is a zero-latency instruction.
  if (!CyclesLeft)
    Stage = IS_EXECUTED;
}

void Instruction::forceExecuted() {
  assert(Stage == IS_READY && "Invalid internal state!");
  CyclesLeft = 0;
  Stage = IS_EXECUTED;
}

bool Instruction::updatePending() {
  assert(isPending() && "Unexpected instruction stage found!");

  if (!all_of(getUses(), [](const ReadState &Use) { return Use.isReady(); }))
    return false;

  // A partial register write cannot complete before a dependent write.
  if (!all_of(getDefs(), [](const WriteState &Def) { return Def.isReady(); }))
    return false;

  Stage = IS_READY;
  return true;
}

bool Instruction::updateDispatched() {
  assert(isDispatched() && "Unexpected instruction stage found!");

  if (!all_of(getUses(), [](const ReadState &Use) {
        return Use.isPending() || Use.isReady();
      }))
    return false;

  // A partial register write cannot complete before a dependent write.
  if (!all_of(getDefs(),
              [](const WriteState &Def) { return !Def.getDependentWrite(); }))
    return false;

  Stage = IS_PENDING;
  return true;
}

void Instruction::update() {
  if (isDispatched())
    updateDispatched();
  if (isPending())
    updatePending();
}

void Instruction::cycleEvent() {
  if (isReady())
    return;

  if (isDispatched() || isPending()) {
    for (ReadState &Use : getUses())
      Use.cycleEvent();

    for (WriteState &Def : getDefs())
      Def.cycleEvent();

    update();
    return;
  }

  assert(isExecuting() && "Instruction not in-flight?");
  assert(CyclesLeft && "Instruction already executed?");
  for (WriteState &Def : getDefs())
    Def.cycleEvent();
  CyclesLeft--;
  if (!CyclesLeft)
    Stage = IS_EXECUTED;
}

} // namespace mca
} // namespace llvm
