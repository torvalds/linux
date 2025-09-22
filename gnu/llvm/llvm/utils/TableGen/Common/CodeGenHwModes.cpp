//===--- CodeGenHwModes.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Classes to parse and store HW mode information for instruction selection
//===----------------------------------------------------------------------===//

#include "CodeGenHwModes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

StringRef CodeGenHwModes::DefaultModeName = "DefaultMode";

HwMode::HwMode(Record *R) {
  Name = R->getName();
  Features = std::string(R->getValueAsString("Features"));

  std::vector<Record *> PredicateRecs = R->getValueAsListOfDefs("Predicates");
  SmallString<128> PredicateCheck;
  raw_svector_ostream OS(PredicateCheck);
  ListSeparator LS(" && ");
  for (Record *Pred : PredicateRecs) {
    StringRef CondString = Pred->getValueAsString("CondString");
    if (CondString.empty())
      continue;
    OS << LS << '(' << CondString << ')';
  }

  Predicates = std::string(PredicateCheck);
}

LLVM_DUMP_METHOD
void HwMode::dump() const { dbgs() << Name << ": " << Features << '\n'; }

HwModeSelect::HwModeSelect(Record *R, CodeGenHwModes &CGH) {
  std::vector<Record *> Modes = R->getValueAsListOfDefs("Modes");
  std::vector<Record *> Objects = R->getValueAsListOfDefs("Objects");
  if (Modes.size() != Objects.size()) {
    PrintError(
        R->getLoc(),
        "in record " + R->getName() +
            " derived from HwModeSelect: the lists Modes and Objects should "
            "have the same size");
    report_fatal_error("error in target description.");
  }
  for (unsigned i = 0, e = Modes.size(); i != e; ++i) {
    unsigned ModeId = CGH.getHwModeId(Modes[i]);
    Items.push_back(std::pair(ModeId, Objects[i]));
  }
}

LLVM_DUMP_METHOD
void HwModeSelect::dump() const {
  dbgs() << '{';
  for (const PairType &P : Items)
    dbgs() << " (" << P.first << ',' << P.second->getName() << ')';
  dbgs() << " }\n";
}

CodeGenHwModes::CodeGenHwModes(RecordKeeper &RK) : Records(RK) {
  for (Record *R : Records.getAllDerivedDefinitions("HwMode")) {
    // The default mode needs a definition in the .td sources for TableGen
    // to accept references to it. We need to ignore the definition here.
    if (R->getName() == DefaultModeName)
      continue;
    Modes.emplace_back(R);
    ModeIds.insert(std::pair(R, Modes.size()));
  }

  assert(Modes.size() <= 32 && "number of HwModes exceeds maximum of 32");

  for (Record *R : Records.getAllDerivedDefinitions("HwModeSelect")) {
    auto P = ModeSelects.emplace(std::pair(R, HwModeSelect(R, *this)));
    assert(P.second);
    (void)P;
  }
}

unsigned CodeGenHwModes::getHwModeId(Record *R) const {
  if (R->getName() == DefaultModeName)
    return DefaultMode;
  auto F = ModeIds.find(R);
  assert(F != ModeIds.end() && "Unknown mode name");
  return F->second;
}

const HwModeSelect &CodeGenHwModes::getHwModeSelect(Record *R) const {
  auto F = ModeSelects.find(R);
  assert(F != ModeSelects.end() && "Record is not a \"mode select\"");
  return F->second;
}

LLVM_DUMP_METHOD
void CodeGenHwModes::dump() const {
  dbgs() << "Modes: {\n";
  for (const HwMode &M : Modes) {
    dbgs() << "  ";
    M.dump();
  }
  dbgs() << "}\n";

  dbgs() << "ModeIds: {\n";
  for (const auto &P : ModeIds)
    dbgs() << "  " << P.first->getName() << " -> " << P.second << '\n';
  dbgs() << "}\n";

  dbgs() << "ModeSelects: {\n";
  for (const auto &P : ModeSelects) {
    dbgs() << "  " << P.first->getName() << " -> ";
    P.second.dump();
  }
  dbgs() << "}\n";
}
