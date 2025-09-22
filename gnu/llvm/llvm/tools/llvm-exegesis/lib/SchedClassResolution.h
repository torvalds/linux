//===-- SchedClassResolution.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Resolution of MCInst sched class into expanded form for further analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_SCHEDCLASSRESOLUTION_H
#define LLVM_TOOLS_LLVM_EXEGESIS_SCHEDCLASSRESOLUTION_H

#include "BenchmarkResult.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace exegesis {

// Computes the idealized ProcRes Unit pressure. This is the expected
// distribution if the CPU scheduler can distribute the load as evenly as
// possible.
std::vector<std::pair<uint16_t, float>>
computeIdealizedProcResPressure(const MCSchedModel &SM,
                                SmallVector<MCWriteProcResEntry, 8> WPRS);

// An MCSchedClassDesc augmented with some additional data.
struct ResolvedSchedClass {
  ResolvedSchedClass(const MCSubtargetInfo &STI, unsigned ResolvedSchedClassId,
                     bool WasVariant);

  static std::pair<unsigned /*SchedClassId*/, bool /*WasVariant*/>
  resolveSchedClassId(const MCSubtargetInfo &SubtargetInfo,
                      const MCInstrInfo &InstrInfo, const MCInst &MCI);

  std::vector<BenchmarkMeasure>
  getAsPoint(Benchmark::ModeE Mode, const MCSubtargetInfo &STI,
             ArrayRef<PerInstructionStats> Representative) const;

  const unsigned SchedClassId;
  const MCSchedClassDesc *const SCDesc;
  const bool WasVariant; // Whether the original class was variant.
  const SmallVector<MCWriteProcResEntry, 8> NonRedundantWriteProcRes;
  const std::vector<std::pair<uint16_t, float>> IdealizedProcResPressure;
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_SCHEDCLASSRESOLUTION_H
