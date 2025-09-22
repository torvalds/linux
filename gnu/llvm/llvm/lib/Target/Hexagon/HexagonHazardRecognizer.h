//===--- HexagonHazardRecognizer.h - Hexagon Post RA Hazard Recognizer ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines the hazard recognizer for scheduling on Hexagon.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONPROFITRECOGNIZER_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONPROFITRECOGNIZER_H

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"

namespace llvm {

class HexagonHazardRecognizer : public ScheduleHazardRecognizer {
  DFAPacketizer *Resources;
  const HexagonInstrInfo *TII;
  unsigned PacketNum = 0;
  // If the packet contains a potential dot cur instruction. This is
  // used for the scheduling priority function.
  SUnit *UsesDotCur = nullptr;
  // The packet number when a dor cur is emitted. If its use is not generated
  // in the same packet, then try to wait another cycle before emitting.
  int DotCurPNum = -1;
  // Does the packet contain a load. Used to restrict another load, if possible.
  bool UsesLoad = false;
  // Check if we should prefer a vector store that will become a .new version.
  // The .new store uses different resources than a normal store, and the
  // packetizer will not generate the .new if the regular store does not have
  // resources available (even if the .new version does). To help, the schedule
  // attempts to schedule the .new as soon as possible in the packet.
  SUnit *PrefVectorStoreNew = nullptr;
  // The set of registers defined by instructions in the current packet.
  SmallSet<unsigned, 8> RegDefs;

  // Return true if the instruction is a store that is converted to a new value
  // store because its value is defined in the same packet.
  bool isNewStore(MachineInstr &MI);

public:
  HexagonHazardRecognizer(const InstrItineraryData *II,
                          const HexagonInstrInfo *HII,
                          const HexagonSubtarget &ST)
    : Resources(ST.createDFAPacketizer(II)), TII(HII) { }

  ~HexagonHazardRecognizer() override {
    if (Resources)
      delete Resources;
  }

  /// This callback is invoked when a new block of instructions is about to be
  /// scheduled. The hazard state is set to an initialized state.
  void Reset() override;

  /// Return the hazard type of emitting this node.  There are three
  /// possible results.  Either:
  ///  * NoHazard: it is legal to issue this instruction on this cycle.
  ///  * Hazard: issuing this instruction would stall the machine.  If some
  ///     other instruction is available, issue it first.
  HazardType getHazardType(SUnit *SU, int stalls) override;

  /// This callback is invoked when an instruction is emitted to be scheduled,
  /// to advance the hazard state.
  void EmitInstruction(SUnit *) override;

  /// This callback may be invoked if getHazardType returns NoHazard. If, even
  /// though there is no hazard, it would be better to schedule another
  /// available instruction, this callback should return true.
  bool ShouldPreferAnother(SUnit *) override;

  /// This callback is invoked whenever the next top-down instruction to be
  /// scheduled cannot issue in the current cycle, either because of latency
  /// or resource conflicts.  This should increment the internal state of the
  /// hazard recognizer so that previously "Hazard" instructions will now not
  /// be hazards.
  void AdvanceCycle() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONPROFITRECOGNIZER_H
