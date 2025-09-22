//===---------------------- ExecuteStage.cpp --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the execution stage of an instruction pipeline.
///
/// The ExecuteStage is responsible for managing the hardware scheduler
/// and issuing notifications that an instruction has been executed.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/ExecuteStage.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

HWStallEvent::GenericEventType toHWStallEventType(Scheduler::Status Status) {
  switch (Status) {
  case Scheduler::SC_LOAD_QUEUE_FULL:
    return HWStallEvent::LoadQueueFull;
  case Scheduler::SC_STORE_QUEUE_FULL:
    return HWStallEvent::StoreQueueFull;
  case Scheduler::SC_BUFFERS_FULL:
    return HWStallEvent::SchedulerQueueFull;
  case Scheduler::SC_DISPATCH_GROUP_STALL:
    return HWStallEvent::DispatchGroupStall;
  case Scheduler::SC_AVAILABLE:
    return HWStallEvent::Invalid;
  }

  llvm_unreachable("Don't know how to process this StallKind!");
}

bool ExecuteStage::isAvailable(const InstRef &IR) const {
  if (Scheduler::Status S = HWS.isAvailable(IR)) {
    HWStallEvent::GenericEventType ET = toHWStallEventType(S);
    notifyEvent<HWStallEvent>(HWStallEvent(ET, IR));
    return false;
  }

  return true;
}

Error ExecuteStage::issueInstruction(InstRef &IR) {
  SmallVector<ResourceUse, 4> Used;
  SmallVector<InstRef, 4> Pending;
  SmallVector<InstRef, 4> Ready;

  HWS.issueInstruction(IR, Used, Pending, Ready);
  Instruction &IS = *IR.getInstruction();
  NumIssuedOpcodes += IS.getNumMicroOps();

  notifyReservedOrReleasedBuffers(IR, /* Reserved */ false);

  notifyInstructionIssued(IR, Used);
  if (IS.isExecuted()) {
    notifyInstructionExecuted(IR);
    // FIXME: add a buffer of executed instructions.
    if (Error S = moveToTheNextStage(IR))
      return S;
  }

  for (const InstRef &I : Pending)
    notifyInstructionPending(I);

  for (const InstRef &I : Ready)
    notifyInstructionReady(I);
  return ErrorSuccess();
}

Error ExecuteStage::issueReadyInstructions() {
  InstRef IR = HWS.select();
  while (IR) {
    if (Error Err = issueInstruction(IR))
      return Err;

    // Select the next instruction to issue.
    IR = HWS.select();
  }

  return ErrorSuccess();
}

Error ExecuteStage::cycleStart() {
  SmallVector<ResourceRef, 8> Freed;
  SmallVector<InstRef, 4> Executed;
  SmallVector<InstRef, 4> Pending;
  SmallVector<InstRef, 4> Ready;

  HWS.cycleEvent(Freed, Executed, Pending, Ready);
  NumDispatchedOpcodes = 0;
  NumIssuedOpcodes = 0;

  for (const ResourceRef &RR : Freed)
    notifyResourceAvailable(RR);

  for (InstRef &IR : Executed) {
    notifyInstructionExecuted(IR);
    // FIXME: add a buffer of executed instructions.
    if (Error S = moveToTheNextStage(IR))
      return S;
  }

  for (const InstRef &IR : Pending)
    notifyInstructionPending(IR);

  for (const InstRef &IR : Ready)
    notifyInstructionReady(IR);

  return issueReadyInstructions();
}

Error ExecuteStage::cycleEnd() {
  if (!EnablePressureEvents)
    return ErrorSuccess();

  // Always conservatively report any backpressure events if the dispatch logic
  // was stalled due to unavailable scheduler resources.
  if (!HWS.hadTokenStall() && NumDispatchedOpcodes <= NumIssuedOpcodes)
    return ErrorSuccess();

  SmallVector<InstRef, 8> Insts;
  uint64_t Mask = HWS.analyzeResourcePressure(Insts);
  if (Mask) {
    LLVM_DEBUG(dbgs() << "[E] Backpressure increased because of unavailable "
                         "pipeline resources: "
                      << format_hex(Mask, 16) << '\n');
    HWPressureEvent Ev(HWPressureEvent::RESOURCES, Insts, Mask);
    notifyEvent(Ev);
  }

  SmallVector<InstRef, 8> RegDeps;
  SmallVector<InstRef, 8> MemDeps;
  HWS.analyzeDataDependencies(RegDeps, MemDeps);
  if (RegDeps.size()) {
    LLVM_DEBUG(
        dbgs() << "[E] Backpressure increased by register dependencies\n");
    HWPressureEvent Ev(HWPressureEvent::REGISTER_DEPS, RegDeps);
    notifyEvent(Ev);
  }

  if (MemDeps.size()) {
    LLVM_DEBUG(dbgs() << "[E] Backpressure increased by memory dependencies\n");
    HWPressureEvent Ev(HWPressureEvent::MEMORY_DEPS, MemDeps);
    notifyEvent(Ev);
  }

  return ErrorSuccess();
}

#ifndef NDEBUG
static void verifyInstructionEliminated(const InstRef &IR) {
  const Instruction &Inst = *IR.getInstruction();
  assert(Inst.isEliminated() && "Instruction was not eliminated!");
  assert(Inst.isReady() && "Instruction in an inconsistent state!");

  // Ensure that instructions eliminated at register renaming stage are in a
  // consistent state.
  assert(!Inst.getMayLoad() && !Inst.getMayStore() &&
         "Cannot eliminate a memory op!");
}
#endif

Error ExecuteStage::handleInstructionEliminated(InstRef &IR) {
#ifndef NDEBUG
  verifyInstructionEliminated(IR);
#endif
  notifyInstructionPending(IR);
  notifyInstructionReady(IR);
  notifyInstructionIssued(IR, {});
  IR.getInstruction()->forceExecuted();
  notifyInstructionExecuted(IR);
  return moveToTheNextStage(IR);
}

// Schedule the instruction for execution on the hardware.
Error ExecuteStage::execute(InstRef &IR) {
  assert(isAvailable(IR) && "Scheduler is not available!");

#ifndef NDEBUG
  // Ensure that the HWS has not stored this instruction in its queues.
  HWS.instructionCheck(IR);
#endif

  if (IR.getInstruction()->isEliminated())
    return handleInstructionEliminated(IR);

  // Reserve a slot in each buffered resource. Also, mark units with
  // BufferSize=0 as reserved. Resources with a buffer size of zero will only
  // be released after MCIS is issued, and all the ReleaseAtCycles for those
  // units have been consumed.
  bool IsReadyInstruction = HWS.dispatch(IR);
  const Instruction &Inst = *IR.getInstruction();
  unsigned NumMicroOps = Inst.getNumMicroOps();
  NumDispatchedOpcodes += NumMicroOps;
  notifyReservedOrReleasedBuffers(IR, /* Reserved */ true);

  if (!IsReadyInstruction) {
    if (Inst.isPending())
      notifyInstructionPending(IR);
    return ErrorSuccess();
  }

  notifyInstructionPending(IR);

  // If we did not return early, then the scheduler is ready for execution.
  notifyInstructionReady(IR);

  // If we cannot issue immediately, the HWS will add IR to its ready queue for
  // execution later, so we must return early here.
  if (!HWS.mustIssueImmediately(IR))
    return ErrorSuccess();

  // Issue IR to the underlying pipelines.
  return issueInstruction(IR);
}

void ExecuteStage::notifyInstructionExecuted(const InstRef &IR) const {
  LLVM_DEBUG(dbgs() << "[E] Instruction Executed: #" << IR << '\n');
  notifyEvent<HWInstructionEvent>(
      HWInstructionEvent(HWInstructionEvent::Executed, IR));
}

void ExecuteStage::notifyInstructionPending(const InstRef &IR) const {
  LLVM_DEBUG(dbgs() << "[E] Instruction Pending: #" << IR << '\n');
  notifyEvent<HWInstructionEvent>(
      HWInstructionEvent(HWInstructionEvent::Pending, IR));
}

void ExecuteStage::notifyInstructionReady(const InstRef &IR) const {
  LLVM_DEBUG(dbgs() << "[E] Instruction Ready: #" << IR << '\n');
  notifyEvent<HWInstructionEvent>(
      HWInstructionEvent(HWInstructionEvent::Ready, IR));
}

void ExecuteStage::notifyResourceAvailable(const ResourceRef &RR) const {
  LLVM_DEBUG(dbgs() << "[E] Resource Available: [" << RR.first << '.'
                    << RR.second << "]\n");
  for (HWEventListener *Listener : getListeners())
    Listener->onResourceAvailable(RR);
}

void ExecuteStage::notifyInstructionIssued(
    const InstRef &IR, MutableArrayRef<ResourceUse> Used) const {
  LLVM_DEBUG({
    dbgs() << "[E] Instruction Issued: #" << IR << '\n';
    for (const ResourceUse &Use : Used) {
      assert(Use.second.getDenominator() == 1 && "Invalid cycles!");
      dbgs() << "[E] Resource Used: [" << Use.first.first << '.'
             << Use.first.second << "], ";
      dbgs() << "cycles: " << Use.second.getNumerator() << '\n';
    }
  });

  // Replace resource masks with valid resource processor IDs.
  for (ResourceUse &Use : Used)
    Use.first.first = HWS.getResourceID(Use.first.first);

  notifyEvent<HWInstructionEvent>(HWInstructionIssuedEvent(IR, Used));
}

void ExecuteStage::notifyReservedOrReleasedBuffers(const InstRef &IR,
                                                   bool Reserved) const {
  uint64_t UsedBuffers = IR.getInstruction()->getDesc().UsedBuffers;
  if (!UsedBuffers)
    return;

  SmallVector<unsigned, 4> BufferIDs(llvm::popcount(UsedBuffers), 0);
  for (unsigned I = 0, E = BufferIDs.size(); I < E; ++I) {
    uint64_t CurrentBufferMask = UsedBuffers & (-UsedBuffers);
    BufferIDs[I] = HWS.getResourceID(CurrentBufferMask);
    UsedBuffers ^= CurrentBufferMask;
  }

  if (Reserved) {
    for (HWEventListener *Listener : getListeners())
      Listener->onReservedBuffers(IR, BufferIDs);
    return;
  }

  for (HWEventListener *Listener : getListeners())
    Listener->onReleasedBuffers(IR, BufferIDs);
}

} // namespace mca
} // namespace llvm
