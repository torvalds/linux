//===- llvm/CodeGen/TargetSchedule.h - Sched Machine Model ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a wrapper around MCSchedModel that allows the interface to
// benefit from information currently only available in TargetInstrInfo.
// Ideally, the scheduling interface would be fully defined in the MC layer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETSCHEDULE_H
#define LLVM_CODEGEN_TARGETSCHEDULE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"

namespace llvm {

class MachineInstr;
class TargetInstrInfo;

/// Provide an instruction scheduling machine model to CodeGen passes.
class TargetSchedModel {
  // For efficiency, hold a copy of the statically defined MCSchedModel for this
  // processor.
  MCSchedModel SchedModel;
  InstrItineraryData InstrItins;
  const TargetSubtargetInfo *STI = nullptr;
  const TargetInstrInfo *TII = nullptr;

  SmallVector<unsigned, 16> ResourceFactors;

  // Multiply to normalize microops to resource units.
  unsigned MicroOpFactor = 0;

  // Resource units per cycle. Latency normalization factor.
  unsigned ResourceLCM = 0;

  unsigned computeInstrLatency(const MCSchedClassDesc &SCDesc) const;

public:
  TargetSchedModel() : SchedModel(MCSchedModel::Default) {}

  /// Initialize the machine model for instruction scheduling.
  ///
  /// The machine model API keeps a copy of the top-level MCSchedModel table
  /// indices and may query TargetSubtargetInfo and TargetInstrInfo to resolve
  /// dynamic properties.
  void init(const TargetSubtargetInfo *TSInfo);

  /// Return the MCSchedClassDesc for this instruction.
  const MCSchedClassDesc *resolveSchedClass(const MachineInstr *MI) const;

  /// TargetSubtargetInfo getter.
  const TargetSubtargetInfo *getSubtargetInfo() const { return STI; }

  /// TargetInstrInfo getter.
  const TargetInstrInfo *getInstrInfo() const { return TII; }

  /// Return true if this machine model includes an instruction-level
  /// scheduling model.
  ///
  /// This is more detailed than the course grain IssueWidth and default
  /// latency properties, but separate from the per-cycle itinerary data.
  bool hasInstrSchedModel() const;

  const MCSchedModel *getMCSchedModel() const { return &SchedModel; }

  /// Return true if this machine model includes cycle-to-cycle itinerary
  /// data.
  ///
  /// This models scheduling at each stage in the processor pipeline.
  bool hasInstrItineraries() const;

  const InstrItineraryData *getInstrItineraries() const {
    if (hasInstrItineraries())
      return &InstrItins;
    return nullptr;
  }

  /// Return true if this machine model includes an instruction-level
  /// scheduling model or cycle-to-cycle itinerary data.
  bool hasInstrSchedModelOrItineraries() const {
    return hasInstrSchedModel() || hasInstrItineraries();
  }
  bool enableIntervals() const;
  /// Identify the processor corresponding to the current subtarget.
  unsigned getProcessorID() const { return SchedModel.getProcessorID(); }

  /// Maximum number of micro-ops that may be scheduled per cycle.
  unsigned getIssueWidth() const { return SchedModel.IssueWidth; }

  /// Return true if new group must begin.
  bool mustBeginGroup(const MachineInstr *MI,
                          const MCSchedClassDesc *SC = nullptr) const;
  /// Return true if current group must end.
  bool mustEndGroup(const MachineInstr *MI,
                          const MCSchedClassDesc *SC = nullptr) const;

  /// Return the number of issue slots required for this MI.
  unsigned getNumMicroOps(const MachineInstr *MI,
                          const MCSchedClassDesc *SC = nullptr) const;

  /// Get the number of kinds of resources for this target.
  unsigned getNumProcResourceKinds() const {
    return SchedModel.getNumProcResourceKinds();
  }

  /// Get a processor resource by ID for convenience.
  const MCProcResourceDesc *getProcResource(unsigned PIdx) const {
    return SchedModel.getProcResource(PIdx);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  const char *getResourceName(unsigned PIdx) const {
    if (!PIdx)
      return "MOps";
    return SchedModel.getProcResource(PIdx)->Name;
  }
#endif

  using ProcResIter = const MCWriteProcResEntry *;

  // Get an iterator into the processor resources consumed by this
  // scheduling class.
  ProcResIter getWriteProcResBegin(const MCSchedClassDesc *SC) const {
    // The subtarget holds a single resource table for all processors.
    return STI->getWriteProcResBegin(SC);
  }
  ProcResIter getWriteProcResEnd(const MCSchedClassDesc *SC) const {
    return STI->getWriteProcResEnd(SC);
  }

  /// Multiply the number of units consumed for a resource by this factor
  /// to normalize it relative to other resources.
  unsigned getResourceFactor(unsigned ResIdx) const {
    return ResourceFactors[ResIdx];
  }

  /// Multiply number of micro-ops by this factor to normalize it
  /// relative to other resources.
  unsigned getMicroOpFactor() const {
    return MicroOpFactor;
  }

  /// Multiply cycle count by this factor to normalize it relative to
  /// other resources. This is the number of resource units per cycle.
  unsigned getLatencyFactor() const {
    return ResourceLCM;
  }

  /// Number of micro-ops that may be buffered for OOO execution.
  unsigned getMicroOpBufferSize() const { return SchedModel.MicroOpBufferSize; }

  /// Number of resource units that may be buffered for OOO execution.
  /// \return The buffer size in resource units or -1 for unlimited.
  int getResourceBufferSize(unsigned PIdx) const {
    return SchedModel.getProcResource(PIdx)->BufferSize;
  }

  /// Compute operand latency based on the available machine model.
  ///
  /// Compute and return the latency of the given data dependent def and use
  /// when the operand indices are already known. UseMI may be NULL for an
  /// unknown user.
  unsigned computeOperandLatency(const MachineInstr *DefMI, unsigned DefOperIdx,
                                 const MachineInstr *UseMI, unsigned UseOperIdx)
    const;

  /// Compute the instruction latency based on the available machine
  /// model.
  ///
  /// Compute and return the expected latency of this instruction independent of
  /// a particular use. computeOperandLatency is the preferred API, but this is
  /// occasionally useful to help estimate instruction cost.
  ///
  /// If UseDefaultDefLatency is false and no new machine sched model is
  /// present this method falls back to TII->getInstrLatency with an empty
  /// instruction itinerary (this is so we preserve the previous behavior of the
  /// if converter after moving it to TargetSchedModel).
  unsigned computeInstrLatency(const MachineInstr *MI,
                               bool UseDefaultDefLatency = true) const;
  unsigned computeInstrLatency(const MCInst &Inst) const;
  unsigned computeInstrLatency(unsigned Opcode) const;


  /// Output dependency latency of a pair of defs of the same register.
  ///
  /// This is typically one cycle.
  unsigned computeOutputLatency(const MachineInstr *DefMI, unsigned DefOperIdx,
                                const MachineInstr *DepMI) const;

  /// Compute the reciprocal throughput of the given instruction.
  double computeReciprocalThroughput(const MachineInstr *MI) const;
  double computeReciprocalThroughput(const MCInst &MI) const;
  double computeReciprocalThroughput(unsigned Opcode) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETSCHEDULE_H
