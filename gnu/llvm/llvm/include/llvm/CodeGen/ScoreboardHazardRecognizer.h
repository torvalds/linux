//=- llvm/CodeGen/ScoreboardHazardRecognizer.h - Schedule Support -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ScoreboardHazardRecognizer class, which
// encapsulates hazard-avoidance heuristics for scheduling, based on the
// scheduling itineraries specified for the target.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCOREBOARDHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_SCOREBOARDHAZARDRECOGNIZER_H

#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/MC/MCInstrItineraries.h"
#include <cassert>
#include <cstddef>
#include <cstring>

namespace llvm {

class ScheduleDAG;
class SUnit;

class ScoreboardHazardRecognizer : public ScheduleHazardRecognizer {
  // Scoreboard to track function unit usage. Scoreboard[0] is a
  // mask of the FUs in use in the cycle currently being
  // schedule. Scoreboard[1] is a mask for the next cycle. The
  // Scoreboard is used as a circular buffer with the current cycle
  // indicated by Head.
  //
  // Scoreboard always counts cycles in forward execution order. If used by a
  // bottom-up scheduler, then the scoreboard cycles are the inverse of the
  // scheduler's cycles.
  class Scoreboard {
    InstrStage::FuncUnits *Data = nullptr;

    // The maximum number of cycles monitored by the Scoreboard. This
    // value is determined based on the target itineraries to ensure
    // that all hazards can be tracked.
    size_t Depth = 0;

    // Indices into the Scoreboard that represent the current cycle.
    size_t Head = 0;

  public:
    Scoreboard() = default;
    Scoreboard &operator=(const Scoreboard &other) = delete;
    Scoreboard(const Scoreboard &other) = delete;
    ~Scoreboard() {
      delete[] Data;
    }

    size_t getDepth() const { return Depth; }

    InstrStage::FuncUnits& operator[](size_t idx) const {
      // Depth is expected to be a power-of-2.
      assert(Depth && !(Depth & (Depth - 1)) &&
             "Scoreboard was not initialized properly!");

      return Data[(Head + idx) & (Depth-1)];
    }

    void reset(size_t d = 1) {
      if (!Data) {
        Depth = d;
        Data = new InstrStage::FuncUnits[Depth];
      }

      memset(Data, 0, Depth * sizeof(Data[0]));
      Head = 0;
    }

    void advance() {
      Head = (Head + 1) & (Depth-1);
    }

    void recede() {
      Head = (Head - 1) & (Depth-1);
    }

    // Print the scoreboard.
    void dump() const;
  };

  // Support for tracing ScoreboardHazardRecognizer as a component within
  // another module.
  const char *DebugType;

  // Itinerary data for the target.
  const InstrItineraryData *ItinData;

  const ScheduleDAG *DAG;

  /// IssueWidth - Max issue per cycle. 0=Unknown.
  unsigned IssueWidth = 0;

  /// IssueCount - Count instructions issued in this cycle.
  unsigned IssueCount = 0;

  Scoreboard ReservedScoreboard;
  Scoreboard RequiredScoreboard;

public:
  ScoreboardHazardRecognizer(const InstrItineraryData *II,
                             const ScheduleDAG *DAG,
                             const char *ParentDebugType = "");

  /// atIssueLimit - Return true if no more instructions may be issued in this
  /// cycle.
  bool atIssueLimit() const override;

  // Stalls provides an cycle offset at which SU will be scheduled. It will be
  // negative for bottom-up scheduling.
  HazardType getHazardType(SUnit *SU, int Stalls) override;
  void Reset() override;
  void EmitInstruction(SUnit *SU) override;
  void AdvanceCycle() override;
  void RecedeCycle() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SCOREBOARDHAZARDRECOGNIZER_H
