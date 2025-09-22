//===-------------------------- CodeRegion.cpp -----------------*- C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods from the CodeRegions interface.
///
//===----------------------------------------------------------------------===//

#include "CodeRegion.h"

namespace llvm {
namespace mca {

bool CodeRegion::isLocInRange(SMLoc Loc) const {
  if (RangeEnd.isValid() && Loc.getPointer() > RangeEnd.getPointer())
    return false;
  if (RangeStart.isValid() && Loc.getPointer() < RangeStart.getPointer())
    return false;
  return true;
}

void CodeRegions::addInstruction(const MCInst &Instruction) {
  SMLoc Loc = Instruction.getLoc();
  for (UniqueCodeRegion &Region : Regions)
    if (Region->isLocInRange(Loc))
      Region->addInstruction(Instruction);
}

AnalysisRegions::AnalysisRegions(llvm::SourceMgr &S) : CodeRegions(S) {
  // Create a default region for the input code sequence.
  Regions.emplace_back(std::make_unique<CodeRegion>("", SMLoc()));
}

void AnalysisRegions::beginRegion(StringRef Description, SMLoc Loc) {
  if (ActiveRegions.empty()) {
    // Remove the default region if there is at least one user defined region.
    // By construction, only the default region has an invalid start location.
    if (Regions.size() == 1 && !Regions[0]->startLoc().isValid() &&
        !Regions[0]->endLoc().isValid()) {
      ActiveRegions[Description] = 0;
      Regions[0] = std::make_unique<CodeRegion>(Description, Loc);
      return;
    }
  } else {
    auto It = ActiveRegions.find(Description);
    if (It != ActiveRegions.end()) {
      const CodeRegion &R = *Regions[It->second];
      if (Description.empty()) {
        SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                        "found multiple overlapping anonymous regions");
        SM.PrintMessage(R.startLoc(), llvm::SourceMgr::DK_Note,
                        "Previous anonymous region was defined here");
        FoundErrors = true;
        return;
      }

      SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                      "overlapping regions cannot have the same name");
      SM.PrintMessage(R.startLoc(), llvm::SourceMgr::DK_Note,
                      "region " + Description + " was previously defined here");
      FoundErrors = true;
      return;
    }
  }

  ActiveRegions[Description] = Regions.size();
  Regions.emplace_back(std::make_unique<CodeRegion>(Description, Loc));
}

void AnalysisRegions::endRegion(StringRef Description, SMLoc Loc) {
  if (Description.empty()) {
    // Special case where there is only one user defined region,
    // and this LLVM-MCA-END directive doesn't provide a region name.
    // In this case, we assume that the user simply wanted to just terminate
    // the only active region.
    if (ActiveRegions.size() == 1) {
      auto It = ActiveRegions.begin();
      Regions[It->second]->setEndLocation(Loc);
      ActiveRegions.erase(It);
      return;
    }

    // Special case where the region end marker applies to the default region.
    if (ActiveRegions.empty() && Regions.size() == 1 &&
        !Regions[0]->startLoc().isValid() && !Regions[0]->endLoc().isValid()) {
      Regions[0]->setEndLocation(Loc);
      return;
    }
  }

  auto It = ActiveRegions.find(Description);
  if (It != ActiveRegions.end()) {
    Regions[It->second]->setEndLocation(Loc);
    ActiveRegions.erase(It);
    return;
  }

  FoundErrors = true;
  SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                  "found an invalid region end directive");
  if (!Description.empty()) {
    SM.PrintMessage(Loc, llvm::SourceMgr::DK_Note,
                    "unable to find an active region named " + Description);
  } else {
    SM.PrintMessage(Loc, llvm::SourceMgr::DK_Note,
                    "unable to find an active anonymous region");
  }
}

InstrumentRegions::InstrumentRegions(llvm::SourceMgr &S) : CodeRegions(S) {}

void InstrumentRegions::beginRegion(StringRef Description, SMLoc Loc,
                                    UniqueInstrument I) {
  if (Description.empty()) {
    SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                    "anonymous instrumentation regions are not permitted");
    FoundErrors = true;
    return;
  }

  auto It = ActiveRegions.find(Description);
  if (It != ActiveRegions.end()) {
    const CodeRegion &R = *Regions[It->second];
    SM.PrintMessage(
        Loc, llvm::SourceMgr::DK_Error,
        "overlapping instrumentation regions cannot be of the same kind");
    SM.PrintMessage(R.startLoc(), llvm::SourceMgr::DK_Note,
                    "instrumentation region " + Description +
                        " was previously defined here");
    FoundErrors = true;
    return;
  }

  ActiveRegions[Description] = Regions.size();
  Regions.emplace_back(
      std::make_unique<InstrumentRegion>(Description, Loc, std::move(I)));
}

void InstrumentRegions::endRegion(StringRef Description, SMLoc Loc) {
  auto It = ActiveRegions.find(Description);
  if (It != ActiveRegions.end()) {
    Regions[It->second]->setEndLocation(Loc);
    ActiveRegions.erase(It);
    return;
  }

  FoundErrors = true;
  SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                  "found an invalid instrumentation region end directive");
  if (!Description.empty()) {
    SM.PrintMessage(Loc, llvm::SourceMgr::DK_Note,
                    "unable to find an active instrumentation region named " +
                        Description);
  }
}

const SmallVector<Instrument *>
InstrumentRegions::getActiveInstruments(SMLoc Loc) const {
  SmallVector<Instrument *> AI;
  for (auto &R : Regions) {
    if (R->isLocInRange(Loc)) {
      InstrumentRegion *IR = static_cast<InstrumentRegion *>(R.get());
      AI.push_back(IR->getInstrument());
    }
  }
  return AI;
}

} // namespace mca
} // namespace llvm
