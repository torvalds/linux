//===- llvm/CodeGen/DFAPacketizer.h - DFA Packetizer for VLIW ---*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_DFAPACKETIZER_H
#define LLVM_CODEGEN_DFAPACKETIZER_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/Support/Automaton.h"
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class ScheduleDAGMutation;
class InstrItineraryData;
class MachineFunction;
class MachineInstr;
class MachineLoopInfo;
class MCInstrDesc;
class SUnit;
class TargetInstrInfo;

// This class extends ScheduleDAGInstrs and overrides the schedule method
// to build the dependence graph.
class DefaultVLIWScheduler : public ScheduleDAGInstrs {
private:
  AAResults *AA;
  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

public:
  DefaultVLIWScheduler(MachineFunction &MF, MachineLoopInfo &MLI,
                       AAResults *AA);

  // Actual scheduling work.
  void schedule() override;

  /// DefaultVLIWScheduler takes ownership of the Mutation object.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    Mutations.push_back(std::move(Mutation));
  }

protected:
  void postProcessDAG();
};

class DFAPacketizer {
private:
  const InstrItineraryData *InstrItins;
  Automaton<uint64_t> A;
  /// For every itinerary, an "action" to apply to the automaton. This removes
  /// the redundancy in actions between itinerary classes.
  ArrayRef<unsigned> ItinActions;

public:
  DFAPacketizer(const InstrItineraryData *InstrItins, Automaton<uint64_t> a,
                ArrayRef<unsigned> ItinActions)
      : InstrItins(InstrItins), A(std::move(a)), ItinActions(ItinActions) {
    // Start off with resource tracking disabled.
    A.enableTranscription(false);
  }

  // Reset the current state to make all resources available.
  void clearResources() {
    A.reset();
  }

  // Set whether this packetizer should track not just whether instructions
  // can be packetized, but also which functional units each instruction ends up
  // using after packetization.
  void setTrackResources(bool Track) {
    A.enableTranscription(Track);
  }

  // Check if the resources occupied by a MCInstrDesc are available in
  // the current state.
  bool canReserveResources(const MCInstrDesc *MID);

  // Reserve the resources occupied by a MCInstrDesc and change the current
  // state to reflect that change.
  void reserveResources(const MCInstrDesc *MID);

  // Check if the resources occupied by a machine instruction are available
  // in the current state.
  bool canReserveResources(MachineInstr &MI);

  // Reserve the resources occupied by a machine instruction and change the
  // current state to reflect that change.
  void reserveResources(MachineInstr &MI);

  // Return the resources used by the InstIdx'th instruction added to this
  // packet. The resources are returned as a bitvector of functional units.
  //
  // Note that a bundle may be packed in multiple valid ways. This function
  // returns one arbitary valid packing.
  //
  // Requires setTrackResources(true) to have been called.
  unsigned getUsedResources(unsigned InstIdx);

  const InstrItineraryData *getInstrItins() const { return InstrItins; }
};

// VLIWPacketizerList implements a simple VLIW packetizer using DFA. The
// packetizer works on machine basic blocks. For each instruction I in BB,
// the packetizer consults the DFA to see if machine resources are available
// to execute I. If so, the packetizer checks if I depends on any instruction
// in the current packet. If no dependency is found, I is added to current
// packet and the machine resource is marked as taken. If any dependency is
// found, a target API call is made to prune the dependence.
class VLIWPacketizerList {
protected:
  MachineFunction &MF;
  const TargetInstrInfo *TII;
  AAResults *AA;

  // The VLIW Scheduler.
  DefaultVLIWScheduler *VLIWScheduler;
  // Vector of instructions assigned to the current packet.
  std::vector<MachineInstr*> CurrentPacketMIs;
  // DFA resource tracker.
  DFAPacketizer *ResourceTracker;
  // Map: MI -> SU.
  std::map<MachineInstr*, SUnit*> MIToSUnit;

public:
  // The AAResults parameter can be nullptr.
  VLIWPacketizerList(MachineFunction &MF, MachineLoopInfo &MLI,
                     AAResults *AA);
  VLIWPacketizerList &operator=(const VLIWPacketizerList &other) = delete;
  VLIWPacketizerList(const VLIWPacketizerList &other) = delete;
  virtual ~VLIWPacketizerList();

  // Implement this API in the backend to bundle instructions.
  void PacketizeMIs(MachineBasicBlock *MBB,
                    MachineBasicBlock::iterator BeginItr,
                    MachineBasicBlock::iterator EndItr);

  // Return the ResourceTracker.
  DFAPacketizer *getResourceTracker() {return ResourceTracker;}

  // addToPacket - Add MI to the current packet.
  virtual MachineBasicBlock::iterator addToPacket(MachineInstr &MI) {
    CurrentPacketMIs.push_back(&MI);
    ResourceTracker->reserveResources(MI);
    return MI;
  }

  // End the current packet and reset the state of the packetizer.
  // Overriding this function allows the target-specific packetizer
  // to perform custom finalization.
  virtual void endPacket(MachineBasicBlock *MBB,
                         MachineBasicBlock::iterator MI);

  // Perform initialization before packetizing an instruction. This
  // function is supposed to be overrided by the target dependent packetizer.
  virtual void initPacketizerState() {}

  // Check if the given instruction I should be ignored by the packetizer.
  virtual bool ignorePseudoInstruction(const MachineInstr &I,
                                       const MachineBasicBlock *MBB) {
    return false;
  }

  // Return true if instruction MI can not be packetized with any other
  // instruction, which means that MI itself is a packet.
  virtual bool isSoloInstruction(const MachineInstr &MI) { return true; }

  // Check if the packetizer should try to add the given instruction to
  // the current packet. One reasons for which it may not be desirable
  // to include an instruction in the current packet could be that it
  // would cause a stall.
  // If this function returns "false", the current packet will be ended,
  // and the instruction will be added to the next packet.
  virtual bool shouldAddToPacket(const MachineInstr &MI) { return true; }

  // Check if it is legal to packetize SUI and SUJ together.
  virtual bool isLegalToPacketizeTogether(SUnit *SUI, SUnit *SUJ) {
    return false;
  }

  // Check if it is legal to prune dependece between SUI and SUJ.
  virtual bool isLegalToPruneDependencies(SUnit *SUI, SUnit *SUJ) {
    return false;
  }

  // Add a DAG mutation to be done before the packetization begins.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation);

  bool alias(const MachineInstr &MI1, const MachineInstr &MI2,
             bool UseTBAA = true) const;

private:
  bool alias(const MachineMemOperand &Op1, const MachineMemOperand &Op2,
             bool UseTBAA = true) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_DFAPACKETIZER_H
