//===--------------------- SchedulerStatistics.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines class SchedulerStatistics. Class SchedulerStatistics is a
/// View that listens to instruction issue events in order to print general
/// statistics related to the hardware schedulers.
///
/// Example:
/// ========
///
/// Schedulers - number of cycles where we saw N instructions issued:
/// [# issued], [# cycles]
///  0,          6  (2.9%)
///  1,          106  (50.7%)
///  2,          97  (46.4%)
///
/// Scheduler's queue usage:
/// [1] Resource name.
/// [2] Average number of used buffer entries.
/// [3] Maximum number of used buffer entries.
/// [4] Total number of buffer entries.
///
///  [1]            [2]        [3]        [4]
/// JALU01           0          0          20
/// JFPU01           15         18         18
/// JLSAGU           0          0          12
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_SCHEDULERSTATISTICS_H
#define LLVM_TOOLS_LLVM_MCA_SCHEDULERSTATISTICS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/View.h"
#include <map>

namespace llvm {
namespace mca {

class SchedulerStatistics final : public View {
  const llvm::MCSchedModel &SM;
  unsigned LQResourceID;
  unsigned SQResourceID;

  unsigned NumIssued;
  unsigned NumCycles;

  unsigned MostRecentLoadDispatched;
  unsigned MostRecentStoreDispatched;

  // Tracks the usage of a scheduler's queue.
  struct BufferUsage {
    unsigned SlotsInUse;
    unsigned MaxUsedSlots;
    uint64_t CumulativeNumUsedSlots;
  };

  using Histogram = std::map<unsigned, unsigned>;
  Histogram IssueWidthPerCycle;

  std::vector<BufferUsage> Usage;

  void updateHistograms();
  void printSchedulerStats(llvm::raw_ostream &OS) const;
  void printSchedulerUsage(llvm::raw_ostream &OS) const;

public:
  SchedulerStatistics(const llvm::MCSubtargetInfo &STI);
  void onEvent(const HWInstructionEvent &Event) override;
  void onCycleBegin() override { NumCycles++; }
  void onCycleEnd() override { updateHistograms(); }

  // Increases the number of used scheduler queue slots of every buffered
  // resource in the Buffers set.
  void onReservedBuffers(const InstRef &IR,
                         llvm::ArrayRef<unsigned> Buffers) override;

  // Decreases by one the number of used scheduler queue slots of every
  // buffered resource in the Buffers set.
  void onReleasedBuffers(const InstRef &IR,
                         llvm::ArrayRef<unsigned> Buffers) override;

  void printView(llvm::raw_ostream &OS) const override;
  StringRef getNameAsString() const override { return "SchedulerStatistics"; }
  bool isSerializable() const override { return false; }
};
} // namespace mca
} // namespace llvm

#endif
