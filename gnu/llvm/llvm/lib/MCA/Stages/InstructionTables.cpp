//===--------------------- InstructionTables.cpp ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the method InstructionTables::execute().
/// Method execute() prints a theoretical resource pressure distribution based
/// on the information available in the scheduling model, and without running
/// the pipeline.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/InstructionTables.h"

namespace llvm {
namespace mca {

Error InstructionTables::execute(InstRef &IR) {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  UsedResources.clear();

  // Identify the resources consumed by this instruction.
  for (const std::pair<uint64_t, ResourceUsage> &Resource :
       Desc.Resources) {
    // Skip zero-cycle resources (i.e., unused resources).
    if (!Resource.second.size())
      continue;
    unsigned Cycles = Resource.second.size();
    unsigned Index = std::distance(Masks.begin(), find(Masks, Resource.first));
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(Index);
    unsigned NumUnits = ProcResource.NumUnits;
    if (!ProcResource.SubUnitsIdxBegin) {
      // The number of cycles consumed by each unit.
      for (unsigned I = 0, E = NumUnits; I < E; ++I) {
        ResourceRef ResourceUnit = std::make_pair(Index, 1U << I);
        UsedResources.emplace_back(
            std::make_pair(ResourceUnit, ReleaseAtCycles(Cycles, NumUnits)));
      }
      continue;
    }

    // This is a group. Obtain the set of resources contained in this
    // group. Some of these resources may implement multiple units.
    // Uniformly distribute Cycles across all of the units.
    for (unsigned I1 = 0; I1 < NumUnits; ++I1) {
      unsigned SubUnitIdx = ProcResource.SubUnitsIdxBegin[I1];
      const MCProcResourceDesc &SubUnit = *SM.getProcResource(SubUnitIdx);
      // Compute the number of cycles consumed by each resource unit.
      for (unsigned I2 = 0, E2 = SubUnit.NumUnits; I2 < E2; ++I2) {
        ResourceRef ResourceUnit = std::make_pair(SubUnitIdx, 1U << I2);
        UsedResources.emplace_back(std::make_pair(
            ResourceUnit,
            ReleaseAtCycles(Cycles, NumUnits * SubUnit.NumUnits)));
      }
    }
  }

  // Send a fake instruction issued event to all the views.
  HWInstructionIssuedEvent Event(IR, UsedResources);
  notifyEvent<HWInstructionIssuedEvent>(Event);
  return ErrorSuccess();
}

} // namespace mca
} // namespace llvm
