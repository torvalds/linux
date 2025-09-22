//===--------------------- DispatchStatistics.cpp ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the DispatchStatistics interface.
///
//===----------------------------------------------------------------------===//

#include "Views/DispatchStatistics.h"
#include "llvm/Support/Format.h"

namespace llvm {
namespace mca {

void DispatchStatistics::onEvent(const HWStallEvent &Event) {
  if (Event.Type < HWStallEvent::LastGenericEvent)
    HWStalls[Event.Type]++;
}

void DispatchStatistics::onEvent(const HWInstructionEvent &Event) {
  if (Event.Type != HWInstructionEvent::Dispatched)
    return;

  const auto &DE = static_cast<const HWInstructionDispatchedEvent &>(Event);
  NumDispatched += DE.MicroOpcodes;
}

void DispatchStatistics::printDispatchHistogram(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  TempStream << "\n\nDispatch Logic - "
             << "number of cycles where we saw N micro opcodes dispatched:\n";
  TempStream << "[# dispatched], [# cycles]\n";
  for (const std::pair<const unsigned, unsigned> &Entry :
       DispatchGroupSizePerCycle) {
    double Percentage = ((double)Entry.second / NumCycles) * 100.0;
    TempStream << " " << Entry.first << ",              " << Entry.second
               << "  (" << format("%.1f", floor((Percentage * 10) + 0.5) / 10)
               << "%)\n";
  }

  TempStream.flush();
  OS << Buffer;
}

static void printStalls(raw_ostream &OS, unsigned NumStalls,
                        unsigned NumCycles) {
  if (!NumStalls) {
    OS << NumStalls;
    return;
  }

  double Percentage = ((double)NumStalls / NumCycles) * 100.0;
  OS << NumStalls << "  ("
     << format("%.1f", floor((Percentage * 10) + 0.5) / 10) << "%)";
}

void DispatchStatistics::printDispatchStalls(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream SS(Buffer);
  SS << "\n\nDynamic Dispatch Stall Cycles:\n";
  SS << "RAT     - Register unavailable:                      ";
  printStalls(SS, HWStalls[HWStallEvent::RegisterFileStall], NumCycles);
  SS << "\nRCU     - Retire tokens unavailable:                 ";
  printStalls(SS, HWStalls[HWStallEvent::RetireControlUnitStall], NumCycles);
  SS << "\nSCHEDQ  - Scheduler full:                            ";
  printStalls(SS, HWStalls[HWStallEvent::SchedulerQueueFull], NumCycles);
  SS << "\nLQ      - Load queue full:                           ";
  printStalls(SS, HWStalls[HWStallEvent::LoadQueueFull], NumCycles);
  SS << "\nSQ      - Store queue full:                          ";
  printStalls(SS, HWStalls[HWStallEvent::StoreQueueFull], NumCycles);
  SS << "\nGROUP   - Static restrictions on the dispatch group: ";
  printStalls(SS, HWStalls[HWStallEvent::DispatchGroupStall], NumCycles);
  SS << "\nUSH     - Uncategorised Structural Hazard:           ";
  printStalls(SS, HWStalls[HWStallEvent::CustomBehaviourStall], NumCycles);
  SS << '\n';
  SS.flush();
  OS << Buffer;
}

json::Value DispatchStatistics::toJSON() const {
  json::Object JO({{"RAT", HWStalls[HWStallEvent::RegisterFileStall]},
                   {"RCU", HWStalls[HWStallEvent::RetireControlUnitStall]},
                   {"SCHEDQ", HWStalls[HWStallEvent::SchedulerQueueFull]},
                   {"LQ", HWStalls[HWStallEvent::LoadQueueFull]},
                   {"SQ", HWStalls[HWStallEvent::StoreQueueFull]},
                   {"GROUP", HWStalls[HWStallEvent::DispatchGroupStall]},
                   {"USH", HWStalls[HWStallEvent::CustomBehaviourStall]}});
  return JO;
}

} // namespace mca
} // namespace llvm
