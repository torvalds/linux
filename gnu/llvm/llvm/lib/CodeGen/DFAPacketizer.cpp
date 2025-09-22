//=- llvm/CodeGen/DFAPacketizer.cpp - DFA Packetizer for VLIW -*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This class implements a deterministic finite automaton (DFA) based
// packetizing mechanism for VLIW architectures. It provides APIs to
// determine whether there exists a legal mapping of instructions to
// functional unit assignments in a packet. The DFA is auto-generated from
// the target's Schedule.td file.
//
// A DFA consists of 3 major elements: states, inputs, and transitions. For
// the packetizing mechanism, the input is the set of instruction classes for
// a target. The state models all possible combinations of functional unit
// consumption for a given set of instructions in a packet. A transition
// models the addition of an instruction to a packet. In the DFA constructed
// by this class, if an instruction can be added to a packet, then a valid
// transition exists from the corresponding state. Invalid transitions
// indicate that the instruction cannot be added to the current packet.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "packets"

static cl::opt<unsigned> InstrLimit("dfa-instr-limit", cl::Hidden,
  cl::init(0), cl::desc("If present, stops packetizing after N instructions"));

static unsigned InstrCount = 0;

// Check if the resources occupied by a MCInstrDesc are available in the
// current state.
bool DFAPacketizer::canReserveResources(const MCInstrDesc *MID) {
  unsigned Action = ItinActions[MID->getSchedClass()];
  if (MID->getSchedClass() == 0 || Action == 0)
    return false;
  return A.canAdd(Action);
}

// Reserve the resources occupied by a MCInstrDesc and change the current
// state to reflect that change.
void DFAPacketizer::reserveResources(const MCInstrDesc *MID) {
  unsigned Action = ItinActions[MID->getSchedClass()];
  if (MID->getSchedClass() == 0 || Action == 0)
    return;
  A.add(Action);
}

// Check if the resources occupied by a machine instruction are available
// in the current state.
bool DFAPacketizer::canReserveResources(MachineInstr &MI) {
  const MCInstrDesc &MID = MI.getDesc();
  return canReserveResources(&MID);
}

// Reserve the resources occupied by a machine instruction and change the
// current state to reflect that change.
void DFAPacketizer::reserveResources(MachineInstr &MI) {
  const MCInstrDesc &MID = MI.getDesc();
  reserveResources(&MID);
}

unsigned DFAPacketizer::getUsedResources(unsigned InstIdx) {
  ArrayRef<NfaPath> NfaPaths = A.getNfaPaths();
  assert(!NfaPaths.empty() && "Invalid bundle!");
  const NfaPath &RS = NfaPaths.front();

  // RS stores the cumulative resources used up to and including the I'th
  // instruction. The 0th instruction is the base case.
  if (InstIdx == 0)
    return RS[0];
  // Return the difference between the cumulative resources used by InstIdx and
  // its predecessor.
  return RS[InstIdx] ^ RS[InstIdx - 1];
}

DefaultVLIWScheduler::DefaultVLIWScheduler(MachineFunction &MF,
                                           MachineLoopInfo &MLI,
                                           AAResults *AA)
    : ScheduleDAGInstrs(MF, &MLI), AA(AA) {
  CanHandleTerminators = true;
}

/// Apply each ScheduleDAGMutation step in order.
void DefaultVLIWScheduler::postProcessDAG() {
  for (auto &M : Mutations)
    M->apply(this);
}

void DefaultVLIWScheduler::schedule() {
  // Build the scheduling graph.
  buildSchedGraph(AA);
  postProcessDAG();
}

VLIWPacketizerList::VLIWPacketizerList(MachineFunction &mf,
                                       MachineLoopInfo &mli, AAResults *aa)
    : MF(mf), TII(mf.getSubtarget().getInstrInfo()), AA(aa) {
  ResourceTracker = TII->CreateTargetScheduleState(MF.getSubtarget());
  ResourceTracker->setTrackResources(true);
  VLIWScheduler = new DefaultVLIWScheduler(MF, mli, AA);
}

VLIWPacketizerList::~VLIWPacketizerList() {
  delete VLIWScheduler;
  delete ResourceTracker;
}

// End the current packet, bundle packet instructions and reset DFA state.
void VLIWPacketizerList::endPacket(MachineBasicBlock *MBB,
                                   MachineBasicBlock::iterator MI) {
  LLVM_DEBUG({
    if (!CurrentPacketMIs.empty()) {
      dbgs() << "Finalizing packet:\n";
      unsigned Idx = 0;
      for (MachineInstr *MI : CurrentPacketMIs) {
        unsigned R = ResourceTracker->getUsedResources(Idx++);
        dbgs() << " * [res:0x" << utohexstr(R) << "] " << *MI;
      }
    }
  });
  if (CurrentPacketMIs.size() > 1) {
    MachineInstr &MIFirst = *CurrentPacketMIs.front();
    finalizeBundle(*MBB, MIFirst.getIterator(), MI.getInstrIterator());
  }
  CurrentPacketMIs.clear();
  ResourceTracker->clearResources();
  LLVM_DEBUG(dbgs() << "End packet\n");
}

// Bundle machine instructions into packets.
void VLIWPacketizerList::PacketizeMIs(MachineBasicBlock *MBB,
                                      MachineBasicBlock::iterator BeginItr,
                                      MachineBasicBlock::iterator EndItr) {
  assert(VLIWScheduler && "VLIW Scheduler is not initialized!");
  VLIWScheduler->startBlock(MBB);
  VLIWScheduler->enterRegion(MBB, BeginItr, EndItr,
                             std::distance(BeginItr, EndItr));
  VLIWScheduler->schedule();

  LLVM_DEBUG({
    dbgs() << "Scheduling DAG of the packetize region\n";
    VLIWScheduler->dump();
  });

  // Generate MI -> SU map.
  MIToSUnit.clear();
  for (SUnit &SU : VLIWScheduler->SUnits)
    MIToSUnit[SU.getInstr()] = &SU;

  bool LimitPresent = InstrLimit.getPosition();

  // The main packetizer loop.
  for (; BeginItr != EndItr; ++BeginItr) {
    if (LimitPresent) {
      if (InstrCount >= InstrLimit) {
        EndItr = BeginItr;
        break;
      }
      InstrCount++;
    }
    MachineInstr &MI = *BeginItr;
    initPacketizerState();

    // End the current packet if needed.
    if (isSoloInstruction(MI)) {
      endPacket(MBB, MI);
      continue;
    }

    // Ignore pseudo instructions.
    if (ignorePseudoInstruction(MI, MBB))
      continue;

    SUnit *SUI = MIToSUnit[&MI];
    assert(SUI && "Missing SUnit Info!");

    // Ask DFA if machine resource is available for MI.
    LLVM_DEBUG(dbgs() << "Checking resources for adding MI to packet " << MI);

    bool ResourceAvail = ResourceTracker->canReserveResources(MI);
    LLVM_DEBUG({
      if (ResourceAvail)
        dbgs() << "  Resources are available for adding MI to packet\n";
      else
        dbgs() << "  Resources NOT available\n";
    });
    if (ResourceAvail && shouldAddToPacket(MI)) {
      // Dependency check for MI with instructions in CurrentPacketMIs.
      for (auto *MJ : CurrentPacketMIs) {
        SUnit *SUJ = MIToSUnit[MJ];
        assert(SUJ && "Missing SUnit Info!");

        LLVM_DEBUG(dbgs() << "  Checking against MJ " << *MJ);
        // Is it legal to packetize SUI and SUJ together.
        if (!isLegalToPacketizeTogether(SUI, SUJ)) {
          LLVM_DEBUG(dbgs() << "  Not legal to add MI, try to prune\n");
          // Allow packetization if dependency can be pruned.
          if (!isLegalToPruneDependencies(SUI, SUJ)) {
            // End the packet if dependency cannot be pruned.
            LLVM_DEBUG(dbgs()
                       << "  Could not prune dependencies for adding MI\n");
            endPacket(MBB, MI);
            break;
          }
          LLVM_DEBUG(dbgs() << "  Pruned dependence for adding MI\n");
        }
      }
    } else {
      LLVM_DEBUG(if (ResourceAvail) dbgs()
                 << "Resources are available, but instruction should not be "
                    "added to packet\n  "
                 << MI);
      // End the packet if resource is not available, or if the instruction
      // should not be added to the current packet.
      endPacket(MBB, MI);
    }

    // Add MI to the current packet.
    LLVM_DEBUG(dbgs() << "* Adding MI to packet " << MI << '\n');
    BeginItr = addToPacket(MI);
  } // For all instructions in the packetization range.

  // End any packet left behind.
  endPacket(MBB, EndItr);
  VLIWScheduler->exitRegion();
  VLIWScheduler->finishBlock();
}

bool VLIWPacketizerList::alias(const MachineMemOperand &Op1,
                               const MachineMemOperand &Op2,
                               bool UseTBAA) const {
  if (!Op1.getValue() || !Op2.getValue() || !Op1.getSize().hasValue() ||
      !Op2.getSize().hasValue())
    return true;

  int64_t MinOffset = std::min(Op1.getOffset(), Op2.getOffset());
  int64_t Overlapa = Op1.getSize().getValue() + Op1.getOffset() - MinOffset;
  int64_t Overlapb = Op2.getSize().getValue() + Op2.getOffset() - MinOffset;

  AliasResult AAResult =
      AA->alias(MemoryLocation(Op1.getValue(), Overlapa,
                               UseTBAA ? Op1.getAAInfo() : AAMDNodes()),
                MemoryLocation(Op2.getValue(), Overlapb,
                               UseTBAA ? Op2.getAAInfo() : AAMDNodes()));

  return AAResult != AliasResult::NoAlias;
}

bool VLIWPacketizerList::alias(const MachineInstr &MI1,
                               const MachineInstr &MI2,
                               bool UseTBAA) const {
  if (MI1.memoperands_empty() || MI2.memoperands_empty())
    return true;

  for (const MachineMemOperand *Op1 : MI1.memoperands())
    for (const MachineMemOperand *Op2 : MI2.memoperands())
      if (alias(*Op1, *Op2, UseTBAA))
        return true;
  return false;
}

// Add a DAG mutation object to the ordered list.
void VLIWPacketizerList::addMutation(
      std::unique_ptr<ScheduleDAGMutation> Mutation) {
  VLIWScheduler->addMutation(std::move(Mutation));
}
