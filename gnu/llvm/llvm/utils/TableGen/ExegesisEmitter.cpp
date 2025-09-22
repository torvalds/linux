//===- ExegesisEmitter.cpp - Generate exegesis target data ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits llvm-exegesis information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "exegesis-emitter"

namespace {

class ExegesisEmitter {
public:
  ExegesisEmitter(RecordKeeper &RK);

  void run(raw_ostream &OS) const;

private:
  unsigned getPfmCounterId(llvm::StringRef Name) const {
    const auto It = PfmCounterNameTable.find(Name);
    if (It == PfmCounterNameTable.end())
      PrintFatalError("no pfm counter id for " + Name);
    return It->second;
  }

  // Collects all the ProcPfmCounters definitions available in this target.
  void emitPfmCounters(raw_ostream &OS) const;

  void emitPfmCountersInfo(const Record &Def,
                           unsigned &IssueCountersTableOffset,
                           raw_ostream &OS) const;

  void emitPfmCountersLookupTable(raw_ostream &OS) const;

  RecordKeeper &Records;
  std::string Target;

  // Table of counter name -> counter index.
  const std::map<llvm::StringRef, unsigned> PfmCounterNameTable;
};

static std::map<llvm::StringRef, unsigned>
collectPfmCounters(const RecordKeeper &Records) {
  std::map<llvm::StringRef, unsigned> PfmCounterNameTable;
  const auto AddPfmCounterName = [&PfmCounterNameTable](
                                     const Record *PfmCounterDef) {
    const llvm::StringRef Counter = PfmCounterDef->getValueAsString("Counter");
    if (!Counter.empty())
      PfmCounterNameTable.emplace(Counter, 0);
  };
  for (Record *Def : Records.getAllDerivedDefinitions("ProcPfmCounters")) {
    // Check that ResourceNames are unique.
    llvm::SmallSet<llvm::StringRef, 16> Seen;
    for (const Record *IssueCounter :
         Def->getValueAsListOfDefs("IssueCounters")) {
      const llvm::StringRef ResourceName =
          IssueCounter->getValueAsString("ResourceName");
      if (ResourceName.empty())
        PrintFatalError(IssueCounter->getLoc(), "invalid empty ResourceName");
      if (!Seen.insert(ResourceName).second)
        PrintFatalError(IssueCounter->getLoc(),
                        "duplicate ResourceName " + ResourceName);
      AddPfmCounterName(IssueCounter);
    }

    for (const Record *ValidationCounter :
         Def->getValueAsListOfDefs("ValidationCounters"))
      AddPfmCounterName(ValidationCounter);

    AddPfmCounterName(Def->getValueAsDef("CycleCounter"));
    AddPfmCounterName(Def->getValueAsDef("UopsCounter"));
  }
  unsigned Index = 0;
  for (auto &NameAndIndex : PfmCounterNameTable)
    NameAndIndex.second = Index++;
  return PfmCounterNameTable;
}

ExegesisEmitter::ExegesisEmitter(RecordKeeper &RK)
    : Records(RK), PfmCounterNameTable(collectPfmCounters(RK)) {
  std::vector<Record *> Targets = Records.getAllDerivedDefinitions("Target");
  if (Targets.size() == 0)
    PrintFatalError("No 'Target' subclasses defined!");
  if (Targets.size() != 1)
    PrintFatalError("Multiple subclasses of Target defined!");
  Target = std::string(Targets[0]->getName());
}

struct ValidationCounterInfo {
  int64_t EventNumber;
  StringRef EventName;
  unsigned PfmCounterID;
};

bool EventNumberLess(const ValidationCounterInfo &LHS,
                     const ValidationCounterInfo &RHS) {
  return LHS.EventNumber < RHS.EventNumber;
}

void ExegesisEmitter::emitPfmCountersInfo(const Record &Def,
                                          unsigned &IssueCountersTableOffset,
                                          raw_ostream &OS) const {
  const auto CycleCounter =
      Def.getValueAsDef("CycleCounter")->getValueAsString("Counter");
  const auto UopsCounter =
      Def.getValueAsDef("UopsCounter")->getValueAsString("Counter");
  const size_t NumIssueCounters =
      Def.getValueAsListOfDefs("IssueCounters").size();
  const size_t NumValidationCounters =
      Def.getValueAsListOfDefs("ValidationCounters").size();

  // Emit Validation Counters Array
  if (NumValidationCounters != 0) {
    std::vector<ValidationCounterInfo> ValidationCounters;
    ValidationCounters.reserve(NumValidationCounters);
    for (const Record *ValidationCounter :
         Def.getValueAsListOfDefs("ValidationCounters")) {
      ValidationCounters.push_back(
          {ValidationCounter->getValueAsDef("EventType")
               ->getValueAsInt("EventNumber"),
           ValidationCounter->getValueAsDef("EventType")->getName(),
           getPfmCounterId(ValidationCounter->getValueAsString("Counter"))});
    }
    std::sort(ValidationCounters.begin(), ValidationCounters.end(),
              EventNumberLess);
    OS << "\nstatic const std::pair<ValidationEvent, const char*> " << Target
       << Def.getName() << "ValidationCounters[] = {\n";
    for (const ValidationCounterInfo &VCI : ValidationCounters) {
      OS << "  { " << VCI.EventName << ", " << Target << "PfmCounterNames["
         << VCI.PfmCounterID << "]},\n";
    }
    OS << "};\n";
  }

  OS << "\nstatic const PfmCountersInfo " << Target << Def.getName()
     << " = {\n";

  // Cycle Counter.
  if (CycleCounter.empty())
    OS << "  nullptr,  // No cycle counter.\n";
  else
    OS << "  " << Target << "PfmCounterNames[" << getPfmCounterId(CycleCounter)
       << "],  // Cycle counter\n";

  // Uops Counter.
  if (UopsCounter.empty())
    OS << "  nullptr,  // No uops counter.\n";
  else
    OS << "  " << Target << "PfmCounterNames[" << getPfmCounterId(UopsCounter)
       << "],  // Uops counter\n";

  // Issue Counters
  if (NumIssueCounters == 0)
    OS << "  nullptr, 0, // No issue counters\n";
  else
    OS << "  " << Target << "PfmIssueCounters + " << IssueCountersTableOffset
       << ", " << NumIssueCounters << ", // Issue counters.\n";

  // Validation Counters
  if (NumValidationCounters == 0)
    OS << "  nullptr, 0 // No validation counters.\n";
  else
    OS << "  " << Target << Def.getName() << "ValidationCounters, "
       << NumValidationCounters << " // Validation counters.\n";

  OS << "};\n";
  IssueCountersTableOffset += NumIssueCounters;
}

void ExegesisEmitter::emitPfmCounters(raw_ostream &OS) const {
  // Emit the counter name table.
  OS << "\nstatic const char *" << Target << "PfmCounterNames[] = {\n";
  for (const auto &NameAndIndex : PfmCounterNameTable)
    OS << "  \"" << NameAndIndex.first << "\", // " << NameAndIndex.second
       << "\n";
  OS << "};\n\n";

  // Emit the IssueCounters table.
  const auto PfmCounterDefs =
      Records.getAllDerivedDefinitions("ProcPfmCounters");
  // Only emit if non-empty.
  const bool HasAtLeastOnePfmIssueCounter =
      llvm::any_of(PfmCounterDefs, [](const Record *Def) {
        return !Def->getValueAsListOfDefs("IssueCounters").empty();
      });
  if (HasAtLeastOnePfmIssueCounter) {
    OS << "static const PfmCountersInfo::IssueCounter " << Target
       << "PfmIssueCounters[] = {\n";
    for (const Record *Def : PfmCounterDefs) {
      for (const Record *ICDef : Def->getValueAsListOfDefs("IssueCounters"))
        OS << "  { " << Target << "PfmCounterNames["
           << getPfmCounterId(ICDef->getValueAsString("Counter")) << "], \""
           << ICDef->getValueAsString("ResourceName") << "\"},\n";
    }
    OS << "};\n";
  }

  // Now generate the PfmCountersInfo.
  unsigned IssueCountersTableOffset = 0;
  for (const Record *Def : PfmCounterDefs)
    emitPfmCountersInfo(*Def, IssueCountersTableOffset, OS);

  OS << "\n";
} // namespace

void ExegesisEmitter::emitPfmCountersLookupTable(raw_ostream &OS) const {
  std::vector<Record *> Bindings =
      Records.getAllDerivedDefinitions("PfmCountersBinding");
  assert(!Bindings.empty() && "there must be at least one binding");
  llvm::sort(Bindings, [](const Record *L, const Record *R) {
    return L->getValueAsString("CpuName") < R->getValueAsString("CpuName");
  });

  OS << "// Sorted (by CpuName) array of pfm counters.\n"
     << "static const CpuAndPfmCounters " << Target << "CpuPfmCounters[] = {\n";
  for (Record *Binding : Bindings) {
    // Emit as { "cpu", procinit },
    OS << "  { \""                                                        //
       << Binding->getValueAsString("CpuName") << "\","                   //
       << " &" << Target << Binding->getValueAsDef("Counters")->getName() //
       << " },\n";
  }
  OS << "};\n\n";
}

void ExegesisEmitter::run(raw_ostream &OS) const {
  emitSourceFileHeader("Exegesis Tables", OS);
  emitPfmCounters(OS);
  emitPfmCountersLookupTable(OS);
}

} // end anonymous namespace

static TableGen::Emitter::OptClass<ExegesisEmitter>
    X("gen-exegesis", "Generate llvm-exegesis tables");
