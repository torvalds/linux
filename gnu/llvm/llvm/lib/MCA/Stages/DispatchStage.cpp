//===--------------------- DispatchStage.cpp --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file models the dispatch component of an instruction pipeline.
///
/// The DispatchStage is responsible for updating instruction dependencies
/// and communicating to the simulated instruction scheduler that an instruction
/// is ready to be scheduled for execution.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/DispatchStage.h"
#include "llvm/MCA/HWEventListener.h"
#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

DispatchStage::DispatchStage(const MCSubtargetInfo &Subtarget,
                             const MCRegisterInfo &MRI,
                             unsigned MaxDispatchWidth, RetireControlUnit &R,
                             RegisterFile &F)
    : DispatchWidth(MaxDispatchWidth), AvailableEntries(MaxDispatchWidth),
      CarryOver(0U), STI(Subtarget), RCU(R), PRF(F) {
  if (!DispatchWidth)
    DispatchWidth = Subtarget.getSchedModel().IssueWidth;
}

void DispatchStage::notifyInstructionDispatched(const InstRef &IR,
                                                ArrayRef<unsigned> UsedRegs,
                                                unsigned UOps) const {
  LLVM_DEBUG(dbgs() << "[E] Instruction Dispatched: #" << IR << '\n');
  notifyEvent<HWInstructionEvent>(
      HWInstructionDispatchedEvent(IR, UsedRegs, UOps));
}

bool DispatchStage::checkPRF(const InstRef &IR) const {
  SmallVector<MCPhysReg, 4> RegDefs;
  for (const WriteState &RegDef : IR.getInstruction()->getDefs())
    RegDefs.emplace_back(RegDef.getRegisterID());

  const unsigned RegisterMask = PRF.isAvailable(RegDefs);
  // A mask with all zeroes means: register files are available.
  if (RegisterMask) {
    notifyEvent<HWStallEvent>(
        HWStallEvent(HWStallEvent::RegisterFileStall, IR));
    return false;
  }

  return true;
}

bool DispatchStage::checkRCU(const InstRef &IR) const {
  const unsigned NumMicroOps = IR.getInstruction()->getNumMicroOps();
  if (RCU.isAvailable(NumMicroOps))
    return true;
  notifyEvent<HWStallEvent>(
      HWStallEvent(HWStallEvent::RetireControlUnitStall, IR));
  return false;
}

bool DispatchStage::canDispatch(const InstRef &IR) const {
  bool CanDispatch = checkRCU(IR);
  CanDispatch &= checkPRF(IR);
  CanDispatch &= checkNextStage(IR);
  return CanDispatch;
}

Error DispatchStage::dispatch(InstRef IR) {
  assert(!CarryOver && "Cannot dispatch another instruction!");
  Instruction &IS = *IR.getInstruction();
  const unsigned NumMicroOps = IS.getNumMicroOps();
  if (NumMicroOps > DispatchWidth) {
    assert(AvailableEntries == DispatchWidth);
    AvailableEntries = 0;
    CarryOver = NumMicroOps - DispatchWidth;
    CarriedOver = IR;
  } else {
    assert(AvailableEntries >= NumMicroOps);
    AvailableEntries -= NumMicroOps;
  }

  // Check if this instructions ends the dispatch group.
  if (IS.getEndGroup())
    AvailableEntries = 0;

  // Check if this is an optimizable reg-reg move or an XCHG-like instruction.
  if (IS.isOptimizableMove())
    if (PRF.tryEliminateMoveOrSwap(IS.getDefs(), IS.getUses()))
      IS.setEliminated();

  // A dependency-breaking instruction doesn't have to wait on the register
  // input operands, and it is often optimized at register renaming stage.
  // Update RAW dependencies if this instruction is not a dependency-breaking
  // instruction. A dependency-breaking instruction is a zero-latency
  // instruction that doesn't consume hardware resources.
  // An example of dependency-breaking instruction on X86 is a zero-idiom XOR.
  //
  // We also don't update data dependencies for instructions that have been
  // eliminated at register renaming stage.
  if (!IS.isEliminated()) {
    for (ReadState &RS : IS.getUses())
      PRF.addRegisterRead(RS, STI);
  }

  // By default, a dependency-breaking zero-idiom is expected to be optimized
  // at register renaming stage. That means, no physical register is allocated
  // to the instruction.
  SmallVector<unsigned, 4> RegisterFiles(PRF.getNumRegisterFiles());
  for (WriteState &WS : IS.getDefs())
    PRF.addRegisterWrite(WriteRef(IR.getSourceIndex(), &WS), RegisterFiles);

  // Reserve entries in the reorder buffer.
  unsigned RCUTokenID = RCU.dispatch(IR);
  // Notify the instruction that it has been dispatched.
  IS.dispatch(RCUTokenID);

  // Notify listeners of the "instruction dispatched" event,
  // and move IR to the next stage.
  notifyInstructionDispatched(IR, RegisterFiles,
                              std::min(DispatchWidth, NumMicroOps));
  return moveToTheNextStage(IR);
}

Error DispatchStage::cycleStart() {
  // The retire stage is responsible for calling method `cycleStart`
  // on the PRF.
  if (!CarryOver) {
    AvailableEntries = DispatchWidth;
    return ErrorSuccess();
  }

  AvailableEntries = CarryOver >= DispatchWidth ? 0 : DispatchWidth - CarryOver;
  unsigned DispatchedOpcodes = DispatchWidth - AvailableEntries;
  CarryOver -= DispatchedOpcodes;
  assert(CarriedOver && "Invalid dispatched instruction");

  SmallVector<unsigned, 8> RegisterFiles(PRF.getNumRegisterFiles(), 0U);
  notifyInstructionDispatched(CarriedOver, RegisterFiles, DispatchedOpcodes);
  if (!CarryOver)
    CarriedOver = InstRef();
  return ErrorSuccess();
}

bool DispatchStage::isAvailable(const InstRef &IR) const {
  // Conservatively bail out if there are no available dispatch entries.
  if (!AvailableEntries)
    return false;

  const Instruction &Inst = *IR.getInstruction();
  unsigned NumMicroOps = Inst.getNumMicroOps();
  unsigned Required = std::min(NumMicroOps, DispatchWidth);
  if (Required > AvailableEntries)
    return false;

  if (Inst.getBeginGroup() && AvailableEntries != DispatchWidth)
    return false;

  // The dispatch logic doesn't internally buffer instructions.  It only accepts
  // instructions that can be successfully moved to the next stage during this
  // same cycle.
  return canDispatch(IR);
}

Error DispatchStage::execute(InstRef &IR) {
  assert(canDispatch(IR) && "Cannot dispatch another instruction!");
  return dispatch(IR);
}

#ifndef NDEBUG
void DispatchStage::dump() const {
  PRF.dump();
  RCU.dump();
}
#endif
} // namespace mca
} // namespace llvm
