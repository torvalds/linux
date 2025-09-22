//===- llvm/MC/MCInstrItineraries.h - Scheduling ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the structures used for instruction
// itineraries, stages, and operand reads/writes.  This is used by
// schedulers to determine instruction stages and latencies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRITINERARIES_H
#define LLVM_MC_MCINSTRITINERARIES_H

#include "llvm/MC/MCSchedule.h"
#include <algorithm>
#include <optional>

namespace llvm {

//===----------------------------------------------------------------------===//
/// These values represent a non-pipelined step in
/// the execution of an instruction.  Cycles represents the number of
/// discrete time slots needed to complete the stage.  Units represent
/// the choice of functional units that can be used to complete the
/// stage.  Eg. IntUnit1, IntUnit2. NextCycles indicates how many
/// cycles should elapse from the start of this stage to the start of
/// the next stage in the itinerary. A value of -1 indicates that the
/// next stage should start immediately after the current one.
/// For example:
///
///   { 1, x, -1 }
///      indicates that the stage occupies FU x for 1 cycle and that
///      the next stage starts immediately after this one.
///
///   { 2, x|y, 1 }
///      indicates that the stage occupies either FU x or FU y for 2
///      consecutive cycles and that the next stage starts one cycle
///      after this stage starts. That is, the stage requirements
///      overlap in time.
///
///   { 1, x, 0 }
///      indicates that the stage occupies FU x for 1 cycle and that
///      the next stage starts in this same cycle. This can be used to
///      indicate that the instruction requires multiple stages at the
///      same time.
///
/// FU reservation can be of two different kinds:
///  - FUs which instruction actually requires
///  - FUs which instruction just reserves. Reserved unit is not available for
///    execution of other instruction. However, several instructions can reserve
///    the same unit several times.
/// Such two types of units reservation is used to model instruction domain
/// change stalls, FUs using the same resource (e.g. same register file), etc.

struct InstrStage {
  enum ReservationKinds {
    Required = 0,
    Reserved = 1
  };

  /// Bitmask representing a set of functional units.
  typedef uint64_t FuncUnits;

  unsigned Cycles_;  ///< Length of stage in machine cycles
  FuncUnits Units_;  ///< Choice of functional units
  int NextCycles_;   ///< Number of machine cycles to next stage
  ReservationKinds Kind_; ///< Kind of the FU reservation

  /// Returns the number of cycles the stage is occupied.
  unsigned getCycles() const {
    return Cycles_;
  }

  /// Returns the choice of FUs.
  FuncUnits getUnits() const {
    return Units_;
  }

  ReservationKinds getReservationKind() const {
    return Kind_;
  }

  /// Returns the number of cycles from the start of this stage to the
  /// start of the next stage in the itinerary
  unsigned getNextCycles() const {
    return (NextCycles_ >= 0) ? (unsigned)NextCycles_ : Cycles_;
  }
};

//===----------------------------------------------------------------------===//
/// An itinerary represents the scheduling information for an instruction.
/// This includes a set of stages occupied by the instruction and the pipeline
/// cycle in which operands are read and written.
///
struct InstrItinerary {
  int16_t  NumMicroOps;        ///< # of micro-ops, -1 means it's variable
  uint16_t FirstStage;         ///< Index of first stage in itinerary
  uint16_t LastStage;          ///< Index of last + 1 stage in itinerary
  uint16_t FirstOperandCycle;  ///< Index of first operand rd/wr
  uint16_t LastOperandCycle;   ///< Index of last + 1 operand rd/wr
};

//===----------------------------------------------------------------------===//
/// Itinerary data supplied by a subtarget to be used by a target.
///
class InstrItineraryData {
public:
  MCSchedModel SchedModel =
      MCSchedModel::Default;               ///< Basic machine properties.
  const InstrStage *Stages = nullptr;      ///< Array of stages selected
  const unsigned *OperandCycles = nullptr; ///< Array of operand cycles selected
  const unsigned *Forwardings = nullptr; ///< Array of pipeline forwarding paths
  const InstrItinerary *Itineraries =
      nullptr; ///< Array of itineraries selected

  InstrItineraryData() = default;
  InstrItineraryData(const MCSchedModel &SM, const InstrStage *S,
                     const unsigned *OS, const unsigned *F)
    : SchedModel(SM), Stages(S), OperandCycles(OS), Forwardings(F),
      Itineraries(SchedModel.InstrItineraries) {}

  /// Returns true if there are no itineraries.
  bool isEmpty() const { return Itineraries == nullptr; }

  /// Returns true if the index is for the end marker itinerary.
  bool isEndMarker(unsigned ItinClassIndx) const {
    return ((Itineraries[ItinClassIndx].FirstStage == UINT16_MAX) &&
            (Itineraries[ItinClassIndx].LastStage == UINT16_MAX));
  }

  /// Return the first stage of the itinerary.
  const InstrStage *beginStage(unsigned ItinClassIndx) const {
    unsigned StageIdx = Itineraries[ItinClassIndx].FirstStage;
    return Stages + StageIdx;
  }

  /// Return the last+1 stage of the itinerary.
  const InstrStage *endStage(unsigned ItinClassIndx) const {
    unsigned StageIdx = Itineraries[ItinClassIndx].LastStage;
    return Stages + StageIdx;
  }

  /// Return the total stage latency of the given class.  The latency is
  /// the maximum completion time for any stage in the itinerary.  If no stages
  /// exist, it defaults to one cycle.
  unsigned getStageLatency(unsigned ItinClassIndx) const {
    // If the target doesn't provide itinerary information, use a simple
    // non-zero default value for all instructions.
    if (isEmpty())
      return 1;

    // Calculate the maximum completion time for any stage.
    unsigned Latency = 0, StartCycle = 0;
    for (const InstrStage *IS = beginStage(ItinClassIndx),
           *E = endStage(ItinClassIndx); IS != E; ++IS) {
      Latency = std::max(Latency, StartCycle + IS->getCycles());
      StartCycle += IS->getNextCycles();
    }
    return Latency;
  }

  /// Return the cycle for the given class and operand. Return std::nullopt if
  /// the information is not available for the operand.
  std::optional<unsigned> getOperandCycle(unsigned ItinClassIndx,
                                          unsigned OperandIdx) const {
    if (isEmpty())
      return std::nullopt;

    unsigned FirstIdx = Itineraries[ItinClassIndx].FirstOperandCycle;
    unsigned LastIdx = Itineraries[ItinClassIndx].LastOperandCycle;
    if ((FirstIdx + OperandIdx) >= LastIdx)
      return std::nullopt;

    return OperandCycles[FirstIdx + OperandIdx];
  }

  /// Return true if there is a pipeline forwarding between instructions
  /// of itinerary classes DefClass and UseClasses so that value produced by an
  /// instruction of itinerary class DefClass, operand index DefIdx can be
  /// bypassed when it's read by an instruction of itinerary class UseClass,
  /// operand index UseIdx.
  bool hasPipelineForwarding(unsigned DefClass, unsigned DefIdx,
                             unsigned UseClass, unsigned UseIdx) const {
    unsigned FirstDefIdx = Itineraries[DefClass].FirstOperandCycle;
    unsigned LastDefIdx = Itineraries[DefClass].LastOperandCycle;
    if ((FirstDefIdx + DefIdx) >= LastDefIdx)
      return false;
    if (Forwardings[FirstDefIdx + DefIdx] == 0)
      return false;

    unsigned FirstUseIdx = Itineraries[UseClass].FirstOperandCycle;
    unsigned LastUseIdx = Itineraries[UseClass].LastOperandCycle;
    if ((FirstUseIdx + UseIdx) >= LastUseIdx)
      return false;

    return Forwardings[FirstDefIdx + DefIdx] ==
      Forwardings[FirstUseIdx + UseIdx];
  }

  /// Compute and return the use operand latency of a given itinerary
  /// class and operand index if the value is produced by an instruction of the
  /// specified itinerary class and def operand index. Return std::nullopt if
  /// the information is not available for the operand.
  std::optional<unsigned> getOperandLatency(unsigned DefClass, unsigned DefIdx,
                                            unsigned UseClass,
                                            unsigned UseIdx) const {
    if (isEmpty())
      return std::nullopt;

    std::optional<unsigned> DefCycle = getOperandCycle(DefClass, DefIdx);
    std::optional<unsigned> UseCycle = getOperandCycle(UseClass, UseIdx);
    if (!DefCycle || !UseCycle)
      return std::nullopt;

    if (UseCycle > *DefCycle + 1)
      return std::nullopt;

    UseCycle = *DefCycle - *UseCycle + 1;
    if (UseCycle > 0u &&
        hasPipelineForwarding(DefClass, DefIdx, UseClass, UseIdx))
      // FIXME: This assumes one cycle benefit for every pipeline forwarding.
      UseCycle = *UseCycle - 1;
    return UseCycle;
  }

  /// Return the number of micro-ops that the given class decodes to.
  /// Return -1 for classes that require dynamic lookup via TargetInstrInfo.
  int getNumMicroOps(unsigned ItinClassIndx) const {
    if (isEmpty())
      return 1;
    return Itineraries[ItinClassIndx].NumMicroOps;
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCINSTRITINERARIES_H
