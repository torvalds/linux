//===- ScoreboardHazardRecognizer.cpp - Scheduler Support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScoreboardHazardRecognizer class, which
// encapsultes hazard-avoidance heuristics for scheduling, based on the
// scheduling itineraries specified for the target.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE DebugType

ScoreboardHazardRecognizer::ScoreboardHazardRecognizer(
    const InstrItineraryData *II, const ScheduleDAG *SchedDAG,
    const char *ParentDebugType)
    : DebugType(ParentDebugType), ItinData(II), DAG(SchedDAG) {
  (void)DebugType;
  // Determine the maximum depth of any itinerary. This determines the depth of
  // the scoreboard. We always make the scoreboard at least 1 cycle deep to
  // avoid dealing with the boundary condition.
  unsigned ScoreboardDepth = 1;
  if (ItinData && !ItinData->isEmpty()) {
    for (unsigned idx = 0; ; ++idx) {
      if (ItinData->isEndMarker(idx))
        break;

      const InstrStage *IS = ItinData->beginStage(idx);
      const InstrStage *E = ItinData->endStage(idx);
      unsigned CurCycle = 0;
      unsigned ItinDepth = 0;
      for (; IS != E; ++IS) {
        unsigned StageDepth = CurCycle + IS->getCycles();
        if (ItinDepth < StageDepth) ItinDepth = StageDepth;
        CurCycle += IS->getNextCycles();
      }

      // Find the next power-of-2 >= ItinDepth
      while (ItinDepth > ScoreboardDepth) {
        ScoreboardDepth *= 2;
        // Don't set MaxLookAhead until we find at least one nonzero stage.
        // This way, an itinerary with no stages has MaxLookAhead==0, which
        // completely bypasses the scoreboard hazard logic.
        MaxLookAhead = ScoreboardDepth;
      }
    }
  }

  ReservedScoreboard.reset(ScoreboardDepth);
  RequiredScoreboard.reset(ScoreboardDepth);

  // If MaxLookAhead is not set above, then we are not enabled.
  if (!isEnabled())
    LLVM_DEBUG(dbgs() << "Disabled scoreboard hazard recognizer\n");
  else {
    // A nonempty itinerary must have a SchedModel.
    IssueWidth = ItinData->SchedModel.IssueWidth;
    LLVM_DEBUG(dbgs() << "Using scoreboard hazard recognizer: Depth = "
                      << ScoreboardDepth << '\n');
  }
}

void ScoreboardHazardRecognizer::Reset() {
  IssueCount = 0;
  RequiredScoreboard.reset();
  ReservedScoreboard.reset();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ScoreboardHazardRecognizer::Scoreboard::dump() const {
  dbgs() << "Scoreboard:\n";

  unsigned last = Depth - 1;
  while ((last > 0) && ((*this)[last] == 0))
    last--;

  for (unsigned i = 0; i <= last; i++) {
    InstrStage::FuncUnits FUs = (*this)[i];
    dbgs() << "\t";
    for (int j = std::numeric_limits<InstrStage::FuncUnits>::digits - 1;
         j >= 0; j--)
      dbgs() << ((FUs & (1ULL << j)) ? '1' : '0');
    dbgs() << '\n';
  }
}
#endif

bool ScoreboardHazardRecognizer::atIssueLimit() const {
  if (IssueWidth == 0)
    return false;

  return IssueCount == IssueWidth;
}

ScheduleHazardRecognizer::HazardType
ScoreboardHazardRecognizer::getHazardType(SUnit *SU, int Stalls) {
  if (!ItinData || ItinData->isEmpty())
    return NoHazard;

  // Note that stalls will be negative for bottom-up scheduling.
  int cycle = Stalls;

  // Use the itinerary for the underlying instruction to check for
  // free FU's in the scoreboard at the appropriate future cycles.

  const MCInstrDesc *MCID = DAG->getInstrDesc(SU);
  if (!MCID) {
    // Don't check hazards for non-machineinstr Nodes.
    return NoHazard;
  }
  unsigned idx = MCID->getSchedClass();
  for (const InstrStage *IS = ItinData->beginStage(idx),
         *E = ItinData->endStage(idx); IS != E; ++IS) {
    // We must find one of the stage's units free for every cycle the
    // stage is occupied. FIXME it would be more accurate to find the
    // same unit free in all the cycles.
    for (unsigned int i = 0; i < IS->getCycles(); ++i) {
      int StageCycle = cycle + (int)i;
      if (StageCycle < 0)
        continue;

      if (StageCycle >= (int)RequiredScoreboard.getDepth()) {
        assert((StageCycle - Stalls) < (int)RequiredScoreboard.getDepth() &&
               "Scoreboard depth exceeded!");
        // This stage was stalled beyond pipeline depth, so cannot conflict.
        break;
      }

      InstrStage::FuncUnits freeUnits = IS->getUnits();
      switch (IS->getReservationKind()) {
      case InstrStage::Required:
        // Required FUs conflict with both reserved and required ones
        freeUnits &= ~ReservedScoreboard[StageCycle];
        [[fallthrough]];
      case InstrStage::Reserved:
        // Reserved FUs can conflict only with required ones.
        freeUnits &= ~RequiredScoreboard[StageCycle];
        break;
      }

      if (!freeUnits) {
        LLVM_DEBUG(dbgs() << "*** Hazard in cycle +" << StageCycle << ", ");
        LLVM_DEBUG(DAG->dumpNode(*SU));
        return Hazard;
      }
    }

    // Advance the cycle to the next stage.
    cycle += IS->getNextCycles();
  }

  return NoHazard;
}

void ScoreboardHazardRecognizer::EmitInstruction(SUnit *SU) {
  if (!ItinData || ItinData->isEmpty())
    return;

  // Use the itinerary for the underlying instruction to reserve FU's
  // in the scoreboard at the appropriate future cycles.
  const MCInstrDesc *MCID = DAG->getInstrDesc(SU);
  assert(MCID && "The scheduler must filter non-machineinstrs");
  if (DAG->TII->isZeroCost(MCID->Opcode))
    return;

  ++IssueCount;

  unsigned cycle = 0;

  unsigned idx = MCID->getSchedClass();
  for (const InstrStage *IS = ItinData->beginStage(idx),
         *E = ItinData->endStage(idx); IS != E; ++IS) {
    // We must reserve one of the stage's units for every cycle the
    // stage is occupied. FIXME it would be more accurate to reserve
    // the same unit free in all the cycles.
    for (unsigned int i = 0; i < IS->getCycles(); ++i) {
      assert(((cycle + i) < RequiredScoreboard.getDepth()) &&
             "Scoreboard depth exceeded!");

      InstrStage::FuncUnits freeUnits = IS->getUnits();
      switch (IS->getReservationKind()) {
      case InstrStage::Required:
        // Required FUs conflict with both reserved and required ones
        freeUnits &= ~ReservedScoreboard[cycle + i];
        [[fallthrough]];
      case InstrStage::Reserved:
        // Reserved FUs can conflict only with required ones.
        freeUnits &= ~RequiredScoreboard[cycle + i];
        break;
      }

      // reduce to a single unit
      InstrStage::FuncUnits freeUnit = 0;
      do {
        freeUnit = freeUnits;
        freeUnits = freeUnit & (freeUnit - 1);
      } while (freeUnits);

      if (IS->getReservationKind() == InstrStage::Required)
        RequiredScoreboard[cycle + i] |= freeUnit;
      else
        ReservedScoreboard[cycle + i] |= freeUnit;
    }

    // Advance the cycle to the next stage.
    cycle += IS->getNextCycles();
  }

  LLVM_DEBUG(ReservedScoreboard.dump());
  LLVM_DEBUG(RequiredScoreboard.dump());
}

void ScoreboardHazardRecognizer::AdvanceCycle() {
  IssueCount = 0;
  ReservedScoreboard[0] = 0; ReservedScoreboard.advance();
  RequiredScoreboard[0] = 0; RequiredScoreboard.advance();
}

void ScoreboardHazardRecognizer::RecedeCycle() {
  IssueCount = 0;
  ReservedScoreboard[ReservedScoreboard.getDepth()-1] = 0;
  ReservedScoreboard.recede();
  RequiredScoreboard[RequiredScoreboard.getDepth()-1] = 0;
  RequiredScoreboard.recede();
}
