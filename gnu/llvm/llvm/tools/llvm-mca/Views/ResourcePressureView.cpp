//===--------------------- ResourcePressureView.cpp -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods in the ResourcePressureView interface.
///
//===----------------------------------------------------------------------===//

#include "Views/ResourcePressureView.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

ResourcePressureView::ResourcePressureView(const llvm::MCSubtargetInfo &sti,
                                           MCInstPrinter &Printer,
                                           ArrayRef<MCInst> S)
    : InstructionView(sti, Printer, S), LastInstructionIdx(0) {
  // Populate the map of resource descriptors.
  unsigned R2VIndex = 0;
  const MCSchedModel &SM = getSubTargetInfo().getSchedModel();
  for (unsigned I = 0, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    unsigned NumUnits = ProcResource.NumUnits;
    // Skip groups and invalid resources with zero units.
    if (ProcResource.SubUnitsIdxBegin || !NumUnits)
      continue;

    Resource2VecIndex.insert(std::pair<unsigned, unsigned>(I, R2VIndex));
    R2VIndex += ProcResource.NumUnits;
  }

  NumResourceUnits = R2VIndex;
  ResourceUsage.resize(NumResourceUnits * (getSource().size() + 1));
  std::fill(ResourceUsage.begin(), ResourceUsage.end(), 0.0);
}

void ResourcePressureView::onEvent(const HWInstructionEvent &Event) {
  if (Event.Type == HWInstructionEvent::Dispatched) {
    LastInstructionIdx = Event.IR.getSourceIndex();
    return;
  }

  // We're only interested in Issue events.
  if (Event.Type != HWInstructionEvent::Issued)
    return;

  const auto &IssueEvent = static_cast<const HWInstructionIssuedEvent &>(Event);
  ArrayRef<llvm::MCInst> Source = getSource();
  const unsigned SourceIdx = Event.IR.getSourceIndex() % Source.size();
  for (const std::pair<ResourceRef, ReleaseAtCycles> &Use :
       IssueEvent.UsedResources) {
    const ResourceRef &RR = Use.first;
    assert(Resource2VecIndex.contains(RR.first));
    unsigned R2VIndex = Resource2VecIndex[RR.first];
    R2VIndex += llvm::countr_zero(RR.second);
    ResourceUsage[R2VIndex + NumResourceUnits * SourceIdx] += Use.second;
    ResourceUsage[R2VIndex + NumResourceUnits * Source.size()] += Use.second;
  }
}

static void printColumnNames(formatted_raw_ostream &OS,
                             const MCSchedModel &SM) {
  unsigned Column = OS.getColumn();
  for (unsigned I = 1, ResourceIndex = 0, E = SM.getNumProcResourceKinds();
       I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    unsigned NumUnits = ProcResource.NumUnits;
    // Skip groups and invalid resources with zero units.
    if (ProcResource.SubUnitsIdxBegin || !NumUnits)
      continue;

    for (unsigned J = 0; J < NumUnits; ++J) {
      Column += 7;
      OS << "[" << ResourceIndex;
      if (NumUnits > 1)
        OS << '.' << J;
      OS << ']';
      OS.PadToColumn(Column);
    }

    ResourceIndex++;
  }
}

static void printResourcePressure(formatted_raw_ostream &OS, double Pressure,
                                  unsigned Col) {
  if (!Pressure || Pressure < 0.005) {
    OS << " - ";
  } else {
    // Round to the value to the nearest hundredth and then print it.
    OS << format("%.2f", floor((Pressure * 100) + 0.5) / 100);
  }
  OS.PadToColumn(Col);
}

void ResourcePressureView::printResourcePressurePerIter(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  formatted_raw_ostream FOS(TempStream);

  FOS << "\n\nResources:\n";
  const MCSchedModel &SM = getSubTargetInfo().getSchedModel();
  for (unsigned I = 1, ResourceIndex = 0, E = SM.getNumProcResourceKinds();
       I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    unsigned NumUnits = ProcResource.NumUnits;
    // Skip groups and invalid resources with zero units.
    if (ProcResource.SubUnitsIdxBegin || !NumUnits)
      continue;

    for (unsigned J = 0; J < NumUnits; ++J) {
      FOS << '[' << ResourceIndex;
      if (NumUnits > 1)
        FOS << '.' << J;
      FOS << ']';
      FOS.PadToColumn(6);
      FOS << "- " << ProcResource.Name << '\n';
    }

    ResourceIndex++;
  }

  FOS << "\n\nResource pressure per iteration:\n";
  FOS.flush();
  printColumnNames(FOS, SM);
  FOS << '\n';
  FOS.flush();

  ArrayRef<llvm::MCInst> Source = getSource();
  const unsigned Executions = LastInstructionIdx / Source.size() + 1;
  for (unsigned I = 0, E = NumResourceUnits; I < E; ++I) {
    double Usage = ResourceUsage[I + Source.size() * E];
    printResourcePressure(FOS, Usage / Executions, (I + 1) * 7);
  }

  FOS.flush();
  OS << Buffer;
}

void ResourcePressureView::printResourcePressurePerInst(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  formatted_raw_ostream FOS(TempStream);

  FOS << "\n\nResource pressure by instruction:\n";
  printColumnNames(FOS, getSubTargetInfo().getSchedModel());
  FOS << "Instructions:\n";

  unsigned InstrIndex = 0;
  ArrayRef<llvm::MCInst> Source = getSource();
  const unsigned Executions = LastInstructionIdx / Source.size() + 1;
  for (const MCInst &MCI : Source) {
    unsigned BaseEltIdx = InstrIndex * NumResourceUnits;
    for (unsigned J = 0; J < NumResourceUnits; ++J) {
      double Usage = ResourceUsage[J + BaseEltIdx];
      printResourcePressure(FOS, Usage / Executions, (J + 1) * 7);
    }

    FOS << printInstructionString(MCI) << '\n';
    FOS.flush();
    OS << Buffer;
    Buffer = "";

    ++InstrIndex;
  }
}

json::Value ResourcePressureView::toJSON() const {
  // We're dumping the instructions and the ResourceUsage array.
  json::Array ResourcePressureInfo;

  // The ResourceUsage matrix is sparse, so we only consider
  // non-zero values.
  ArrayRef<llvm::MCInst> Source = getSource();
  const unsigned Executions = LastInstructionIdx / Source.size() + 1;
  for (const auto &R : enumerate(ResourceUsage)) {
    const ReleaseAtCycles &RU = R.value();
    if (RU.getNumerator() == 0)
      continue;
    unsigned InstructionIndex = R.index() / NumResourceUnits;
    unsigned ResourceIndex = R.index() % NumResourceUnits;
    double Usage = RU / Executions;
    ResourcePressureInfo.push_back(
        json::Object({{"InstructionIndex", InstructionIndex},
                      {"ResourceIndex", ResourceIndex},
                      {"ResourceUsage", Usage}}));
  }

  json::Object JO({{"ResourcePressureInfo", std::move(ResourcePressureInfo)}});
  return JO;
}
} // namespace mca
} // namespace llvm
