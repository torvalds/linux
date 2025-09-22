//===--------------------- RetireControlUnitStatistics.cpp ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the RetireControlUnitStatistics interface.
///
//===----------------------------------------------------------------------===//

#include "Views/RetireControlUnitStatistics.h"
#include "llvm/Support/Format.h"

namespace llvm {
namespace mca {

RetireControlUnitStatistics::RetireControlUnitStatistics(const MCSchedModel &SM)
    : NumRetired(0), NumCycles(0), EntriesInUse(0), MaxUsedEntries(0),
      SumOfUsedEntries(0) {
  TotalROBEntries = SM.MicroOpBufferSize;
  if (SM.hasExtraProcessorInfo()) {
    const MCExtraProcessorInfo &EPI = SM.getExtraProcessorInfo();
    if (EPI.ReorderBufferSize)
      TotalROBEntries = EPI.ReorderBufferSize;
  }
}

void RetireControlUnitStatistics::onEvent(const HWInstructionEvent &Event) {
  if (Event.Type == HWInstructionEvent::Dispatched) {
    unsigned NumEntries =
        static_cast<const HWInstructionDispatchedEvent &>(Event).MicroOpcodes;
    EntriesInUse += NumEntries;
  }

  if (Event.Type == HWInstructionEvent::Retired) {
    unsigned ReleasedEntries = Event.IR.getInstruction()->getDesc().NumMicroOps;
    assert(EntriesInUse >= ReleasedEntries && "Invalid internal state!");
    EntriesInUse -= ReleasedEntries;
    ++NumRetired;
  }
}

void RetireControlUnitStatistics::onCycleEnd() {
  // Update histogram
  RetiredPerCycle[NumRetired]++;
  NumRetired = 0;
  ++NumCycles;
  MaxUsedEntries = std::max(MaxUsedEntries, EntriesInUse);
  SumOfUsedEntries += EntriesInUse;
}

void RetireControlUnitStatistics::printView(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  TempStream << "\n\nRetire Control Unit - "
             << "number of cycles where we saw N instructions retired:\n";
  TempStream << "[# retired], [# cycles]\n";

  for (const std::pair<const unsigned, unsigned> &Entry : RetiredPerCycle) {
    TempStream << " " << Entry.first;
    if (Entry.first < 10)
      TempStream << ",           ";
    else
      TempStream << ",          ";
    TempStream << Entry.second << "  ("
               << format("%.1f", ((double)Entry.second / NumCycles) * 100.0)
               << "%)\n";
  }

  unsigned AvgUsage = (double)SumOfUsedEntries / NumCycles;
  double MaxUsagePercentage =
      ((double)MaxUsedEntries / TotalROBEntries) * 100.0;
  double NormalizedMaxPercentage = floor((MaxUsagePercentage * 10) + 0.5) / 10;
  double AvgUsagePercentage = ((double)AvgUsage / TotalROBEntries) * 100.0;
  double NormalizedAvgPercentage = floor((AvgUsagePercentage * 10) + 0.5) / 10;

  TempStream << "\nTotal ROB Entries:                " << TotalROBEntries
             << "\nMax Used ROB Entries:             " << MaxUsedEntries
             << format("  ( %.1f%% )", NormalizedMaxPercentage)
             << "\nAverage Used ROB Entries per cy:  " << AvgUsage
             << format("  ( %.1f%% )\n", NormalizedAvgPercentage);

  TempStream.flush();
  OS << Buffer;
}

} // namespace mca
} // namespace llvm
