//===- SubtargetEmitter.cpp - Generate subtarget enumerations -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits subtarget enumerations.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenHwModes.h"
#include "Common/CodeGenSchedule.h"
#include "Common/CodeGenTarget.h"
#include "Common/PredicateExpander.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "subtarget-emitter"

namespace {

/// Sorting predicate to sort record pointers by their
/// FieldName field.
struct LessRecordFieldFieldName {
  bool operator()(const Record *Rec1, const Record *Rec2) const {
    return Rec1->getValueAsString("FieldName") <
           Rec2->getValueAsString("FieldName");
  }
};

class SubtargetEmitter {
  // Each processor has a SchedClassDesc table with an entry for each
  // SchedClass. The SchedClassDesc table indexes into a global write resource
  // table, write latency table, and read advance table.
  struct SchedClassTables {
    std::vector<std::vector<MCSchedClassDesc>> ProcSchedClasses;
    std::vector<MCWriteProcResEntry> WriteProcResources;
    std::vector<MCWriteLatencyEntry> WriteLatencies;
    std::vector<std::string> WriterNames;
    std::vector<MCReadAdvanceEntry> ReadAdvanceEntries;

    // Reserve an invalid entry at index 0
    SchedClassTables() {
      ProcSchedClasses.resize(1);
      WriteProcResources.resize(1);
      WriteLatencies.resize(1);
      WriterNames.push_back("InvalidWrite");
      ReadAdvanceEntries.resize(1);
    }
  };

  struct LessWriteProcResources {
    bool operator()(const MCWriteProcResEntry &LHS,
                    const MCWriteProcResEntry &RHS) {
      return LHS.ProcResourceIdx < RHS.ProcResourceIdx;
    }
  };

  CodeGenTarget TGT;
  RecordKeeper &Records;
  CodeGenSchedModels &SchedModels;
  std::string Target;

  void Enumeration(raw_ostream &OS, DenseMap<Record *, unsigned> &FeatureMap);
  void EmitSubtargetInfoMacroCalls(raw_ostream &OS);
  unsigned FeatureKeyValues(raw_ostream &OS,
                            const DenseMap<Record *, unsigned> &FeatureMap);
  unsigned CPUKeyValues(raw_ostream &OS,
                        const DenseMap<Record *, unsigned> &FeatureMap);
  void FormItineraryStageString(const std::string &Names, Record *ItinData,
                                std::string &ItinString, unsigned &NStages);
  void FormItineraryOperandCycleString(Record *ItinData,
                                       std::string &ItinString,
                                       unsigned &NOperandCycles);
  void FormItineraryBypassString(const std::string &Names, Record *ItinData,
                                 std::string &ItinString,
                                 unsigned NOperandCycles);
  void EmitStageAndOperandCycleData(
      raw_ostream &OS, std::vector<std::vector<InstrItinerary>> &ProcItinLists);
  void EmitItineraries(raw_ostream &OS,
                       std::vector<std::vector<InstrItinerary>> &ProcItinLists);
  unsigned EmitRegisterFileTables(const CodeGenProcModel &ProcModel,
                                  raw_ostream &OS);
  void EmitLoadStoreQueueInfo(const CodeGenProcModel &ProcModel,
                              raw_ostream &OS);
  void EmitExtraProcessorInfo(const CodeGenProcModel &ProcModel,
                              raw_ostream &OS);
  void EmitProcessorProp(raw_ostream &OS, const Record *R, StringRef Name,
                         char Separator);
  void EmitProcessorResourceSubUnits(const CodeGenProcModel &ProcModel,
                                     raw_ostream &OS);
  void EmitProcessorResources(const CodeGenProcModel &ProcModel,
                              raw_ostream &OS);
  Record *FindWriteResources(const CodeGenSchedRW &SchedWrite,
                             const CodeGenProcModel &ProcModel);
  Record *FindReadAdvance(const CodeGenSchedRW &SchedRead,
                          const CodeGenProcModel &ProcModel);
  void ExpandProcResources(RecVec &PRVec, std::vector<int64_t> &ReleaseAtCycles,
                           std::vector<int64_t> &AcquireAtCycles,
                           const CodeGenProcModel &ProcModel);
  void GenSchedClassTables(const CodeGenProcModel &ProcModel,
                           SchedClassTables &SchedTables);
  void EmitSchedClassTables(SchedClassTables &SchedTables, raw_ostream &OS);
  void EmitProcessorModels(raw_ostream &OS);
  void EmitSchedModelHelpers(const std::string &ClassName, raw_ostream &OS);
  void emitSchedModelHelpersImpl(raw_ostream &OS,
                                 bool OnlyExpandMCInstPredicates = false);
  void emitGenMCSubtargetInfo(raw_ostream &OS);
  void EmitMCInstrAnalysisPredicateFunctions(raw_ostream &OS);

  void EmitSchedModel(raw_ostream &OS);
  void emitGetMacroFusions(const std::string &ClassName, raw_ostream &OS);
  void EmitHwModeCheck(const std::string &ClassName, raw_ostream &OS);
  void ParseFeaturesFunction(raw_ostream &OS);

public:
  SubtargetEmitter(RecordKeeper &R)
      : TGT(R), Records(R), SchedModels(TGT.getSchedModels()),
        Target(TGT.getName()) {}

  void run(raw_ostream &o);
};

} // end anonymous namespace

//
// Enumeration - Emit the specified class as an enumeration.
//
void SubtargetEmitter::Enumeration(raw_ostream &OS,
                                   DenseMap<Record *, unsigned> &FeatureMap) {
  // Get all records of class and sort
  std::vector<Record *> DefList =
      Records.getAllDerivedDefinitions("SubtargetFeature");
  llvm::sort(DefList, LessRecord());

  unsigned N = DefList.size();
  if (N == 0)
    return;
  if (N + 1 > MAX_SUBTARGET_FEATURES)
    PrintFatalError(
        "Too many subtarget features! Bump MAX_SUBTARGET_FEATURES.");

  OS << "namespace " << Target << " {\n";

  // Open enumeration.
  OS << "enum {\n";

  // For each record
  for (unsigned i = 0; i < N; ++i) {
    // Next record
    Record *Def = DefList[i];

    // Get and emit name
    OS << "  " << Def->getName() << " = " << i << ",\n";

    // Save the index for this feature.
    FeatureMap[Def] = i;
  }

  OS << "  "
     << "NumSubtargetFeatures = " << N << "\n";

  // Close enumeration and namespace
  OS << "};\n";
  OS << "} // end namespace " << Target << "\n";
}

static void printFeatureMask(raw_ostream &OS, RecVec &FeatureList,
                             const DenseMap<Record *, unsigned> &FeatureMap) {
  std::array<uint64_t, MAX_SUBTARGET_WORDS> Mask = {};
  for (const Record *Feature : FeatureList) {
    unsigned Bit = FeatureMap.lookup(Feature);
    Mask[Bit / 64] |= 1ULL << (Bit % 64);
  }

  OS << "{ { { ";
  for (unsigned i = 0; i != Mask.size(); ++i) {
    OS << "0x";
    OS.write_hex(Mask[i]);
    OS << "ULL, ";
  }
  OS << "} } }";
}

/// Emit some information about the SubtargetFeature as calls to a macro so
/// that they can be used from C++.
void SubtargetEmitter::EmitSubtargetInfoMacroCalls(raw_ostream &OS) {
  OS << "\n#ifdef GET_SUBTARGETINFO_MACRO\n";

  std::vector<Record *> FeatureList =
      Records.getAllDerivedDefinitions("SubtargetFeature");
  llvm::sort(FeatureList, LessRecordFieldFieldName());

  for (const Record *Feature : FeatureList) {
    const StringRef FieldName = Feature->getValueAsString("FieldName");
    const StringRef Value = Feature->getValueAsString("Value");

    // Only handle boolean features for now, excluding BitVectors and enums.
    const bool IsBool = (Value == "false" || Value == "true") &&
                        !StringRef(FieldName).contains('[');
    if (!IsBool)
      continue;

    // Some features default to true, with values set to false if enabled.
    const char *Default = Value == "false" ? "true" : "false";

    // Define the getter with lowercased first char: xxxYyy() { return XxxYyy; }
    const std::string Getter =
        FieldName.substr(0, 1).lower() + FieldName.substr(1).str();

    OS << "GET_SUBTARGETINFO_MACRO(" << FieldName << ", " << Default << ", "
       << Getter << ")\n";
  }
  OS << "#undef GET_SUBTARGETINFO_MACRO\n";
  OS << "#endif // GET_SUBTARGETINFO_MACRO\n\n";

  OS << "\n#ifdef GET_SUBTARGETINFO_MC_DESC\n";
  OS << "#undef GET_SUBTARGETINFO_MC_DESC\n\n";

  if (Target == "AArch64")
    OS << "#include \"llvm/TargetParser/AArch64TargetParser.h\"\n\n";
}

//
// FeatureKeyValues - Emit data of all the subtarget features.  Used by the
// command line.
//
unsigned SubtargetEmitter::FeatureKeyValues(
    raw_ostream &OS, const DenseMap<Record *, unsigned> &FeatureMap) {
  // Gather and sort all the features
  std::vector<Record *> FeatureList =
      Records.getAllDerivedDefinitions("SubtargetFeature");

  if (FeatureList.empty())
    return 0;

  llvm::sort(FeatureList, LessRecordFieldName());

  // Check that there are no duplicate keys
  std::set<StringRef> UniqueKeys;

  // Begin feature table
  OS << "// Sorted (by key) array of values for CPU features.\n"
     << "extern const llvm::SubtargetFeatureKV " << Target
     << "FeatureKV[] = {\n";

  // For each feature
  unsigned NumFeatures = 0;
  for (const Record *Feature : FeatureList) {
    // Next feature
    StringRef Name = Feature->getName();
    StringRef CommandLineName = Feature->getValueAsString("Name");
    StringRef Desc = Feature->getValueAsString("Desc");

    if (CommandLineName.empty())
      continue;

    // Emit as { "feature", "description", { featureEnum }, { i1 , i2 , ... , in
    // } }
    OS << "  { "
       << "\"" << CommandLineName << "\", "
       << "\"" << Desc << "\", " << Target << "::" << Name << ", ";

    RecVec ImpliesList = Feature->getValueAsListOfDefs("Implies");

    printFeatureMask(OS, ImpliesList, FeatureMap);

    OS << " },\n";
    ++NumFeatures;

    if (!UniqueKeys.insert(CommandLineName).second)
      PrintFatalError("Duplicate key in SubtargetFeatureKV: " +
                      CommandLineName);
  }

  // End feature table
  OS << "};\n";

  return NumFeatures;
}

//
// CPUKeyValues - Emit data of all the subtarget processors.  Used by command
// line.
//
unsigned
SubtargetEmitter::CPUKeyValues(raw_ostream &OS,
                               const DenseMap<Record *, unsigned> &FeatureMap) {
  // Gather and sort processor information
  std::vector<Record *> ProcessorList =
      Records.getAllDerivedDefinitions("Processor");
  llvm::sort(ProcessorList, LessRecordFieldName());

  // Begin processor table
  OS << "// Sorted (by key) array of values for CPU subtype.\n"
     << "extern const llvm::SubtargetSubTypeKV " << Target
     << "SubTypeKV[] = {\n";

  // For each processor
  for (Record *Processor : ProcessorList) {
    StringRef Name = Processor->getValueAsString("Name");
    RecVec FeatureList = Processor->getValueAsListOfDefs("Features");
    RecVec TuneFeatureList = Processor->getValueAsListOfDefs("TuneFeatures");

    // Emit as { "cpu", "description", 0, { f1 , f2 , ... fn } },
    OS << " { "
       << "\"" << Name << "\", ";

    printFeatureMask(OS, FeatureList, FeatureMap);
    OS << ", ";
    printFeatureMask(OS, TuneFeatureList, FeatureMap);

    // Emit the scheduler model pointer.
    const std::string &ProcModelName =
        SchedModels.getModelForProc(Processor).ModelName;
    OS << ", &" << ProcModelName << " },\n";
  }

  // End processor table
  OS << "};\n";

  return ProcessorList.size();
}

//
// FormItineraryStageString - Compose a string containing the stage
// data initialization for the specified itinerary.  N is the number
// of stages.
//
void SubtargetEmitter::FormItineraryStageString(const std::string &Name,
                                                Record *ItinData,
                                                std::string &ItinString,
                                                unsigned &NStages) {
  // Get states list
  RecVec StageList = ItinData->getValueAsListOfDefs("Stages");

  // For each stage
  unsigned N = NStages = StageList.size();
  for (unsigned i = 0; i < N;) {
    // Next stage
    const Record *Stage = StageList[i];

    // Form string as ,{ cycles, u1 | u2 | ... | un, timeinc, kind }
    int Cycles = Stage->getValueAsInt("Cycles");
    ItinString += "  { " + itostr(Cycles) + ", ";

    // Get unit list
    RecVec UnitList = Stage->getValueAsListOfDefs("Units");

    // For each unit
    for (unsigned j = 0, M = UnitList.size(); j < M;) {
      // Add name and bitwise or
      ItinString += Name + "FU::" + UnitList[j]->getName().str();
      if (++j < M)
        ItinString += " | ";
    }

    int TimeInc = Stage->getValueAsInt("TimeInc");
    ItinString += ", " + itostr(TimeInc);

    int Kind = Stage->getValueAsInt("Kind");
    ItinString += ", (llvm::InstrStage::ReservationKinds)" + itostr(Kind);

    // Close off stage
    ItinString += " }";
    if (++i < N)
      ItinString += ", ";
  }
}

//
// FormItineraryOperandCycleString - Compose a string containing the
// operand cycle initialization for the specified itinerary.  N is the
// number of operands that has cycles specified.
//
void SubtargetEmitter::FormItineraryOperandCycleString(
    Record *ItinData, std::string &ItinString, unsigned &NOperandCycles) {
  // Get operand cycle list
  std::vector<int64_t> OperandCycleList =
      ItinData->getValueAsListOfInts("OperandCycles");

  // For each operand cycle
  NOperandCycles = OperandCycleList.size();
  ListSeparator LS;
  for (int OCycle : OperandCycleList) {
    // Next operand cycle
    ItinString += LS;
    ItinString += "  " + itostr(OCycle);
  }
}

void SubtargetEmitter::FormItineraryBypassString(const std::string &Name,
                                                 Record *ItinData,
                                                 std::string &ItinString,
                                                 unsigned NOperandCycles) {
  RecVec BypassList = ItinData->getValueAsListOfDefs("Bypasses");
  unsigned N = BypassList.size();
  unsigned i = 0;
  ListSeparator LS;
  for (; i < N; ++i) {
    ItinString += LS;
    ItinString += Name + "Bypass::" + BypassList[i]->getName().str();
  }
  for (; i < NOperandCycles; ++i) {
    ItinString += LS;
    ItinString += " 0";
  }
}

//
// EmitStageAndOperandCycleData - Generate unique itinerary stages and operand
// cycle tables. Create a list of InstrItinerary objects (ProcItinLists) indexed
// by CodeGenSchedClass::Index.
//
void SubtargetEmitter::EmitStageAndOperandCycleData(
    raw_ostream &OS, std::vector<std::vector<InstrItinerary>> &ProcItinLists) {
  // Multiple processor models may share an itinerary record. Emit it once.
  SmallPtrSet<Record *, 8> ItinsDefSet;

  // Emit functional units for all the itineraries.
  for (const CodeGenProcModel &ProcModel : SchedModels.procModels()) {

    if (!ItinsDefSet.insert(ProcModel.ItinsDef).second)
      continue;

    RecVec FUs = ProcModel.ItinsDef->getValueAsListOfDefs("FU");
    if (FUs.empty())
      continue;

    StringRef Name = ProcModel.ItinsDef->getName();
    OS << "\n// Functional units for \"" << Name << "\"\n"
       << "namespace " << Name << "FU {\n";

    for (unsigned j = 0, FUN = FUs.size(); j < FUN; ++j)
      OS << "  const InstrStage::FuncUnits " << FUs[j]->getName()
         << " = 1ULL << " << j << ";\n";

    OS << "} // end namespace " << Name << "FU\n";

    RecVec BPs = ProcModel.ItinsDef->getValueAsListOfDefs("BP");
    if (!BPs.empty()) {
      OS << "\n// Pipeline forwarding paths for itineraries \"" << Name
         << "\"\n"
         << "namespace " << Name << "Bypass {\n";

      OS << "  const unsigned NoBypass = 0;\n";
      for (unsigned j = 0, BPN = BPs.size(); j < BPN; ++j)
        OS << "  const unsigned " << BPs[j]->getName() << " = 1 << " << j
           << ";\n";

      OS << "} // end namespace " << Name << "Bypass\n";
    }
  }

  // Begin stages table
  std::string StageTable =
      "\nextern const llvm::InstrStage " + Target + "Stages[] = {\n";
  StageTable += "  { 0, 0, 0, llvm::InstrStage::Required }, // No itinerary\n";

  // Begin operand cycle table
  std::string OperandCycleTable =
      "extern const unsigned " + Target + "OperandCycles[] = {\n";
  OperandCycleTable += "  0, // No itinerary\n";

  // Begin pipeline bypass table
  std::string BypassTable =
      "extern const unsigned " + Target + "ForwardingPaths[] = {\n";
  BypassTable += " 0, // No itinerary\n";

  // For each Itinerary across all processors, add a unique entry to the stages,
  // operand cycles, and pipeline bypass tables. Then add the new Itinerary
  // object with computed offsets to the ProcItinLists result.
  unsigned StageCount = 1, OperandCycleCount = 1;
  std::map<std::string, unsigned> ItinStageMap, ItinOperandMap;
  for (const CodeGenProcModel &ProcModel : SchedModels.procModels()) {
    // Add process itinerary to the list.
    std::vector<InstrItinerary> &ItinList = ProcItinLists.emplace_back();

    // If this processor defines no itineraries, then leave the itinerary list
    // empty.
    if (!ProcModel.hasItineraries())
      continue;

    StringRef Name = ProcModel.ItinsDef->getName();

    ItinList.resize(SchedModels.numInstrSchedClasses());
    assert(ProcModel.ItinDefList.size() == ItinList.size() && "bad Itins");

    for (unsigned SchedClassIdx = 0, SchedClassEnd = ItinList.size();
         SchedClassIdx < SchedClassEnd; ++SchedClassIdx) {

      // Next itinerary data
      Record *ItinData = ProcModel.ItinDefList[SchedClassIdx];

      // Get string and stage count
      std::string ItinStageString;
      unsigned NStages = 0;
      if (ItinData)
        FormItineraryStageString(std::string(Name), ItinData, ItinStageString,
                                 NStages);

      // Get string and operand cycle count
      std::string ItinOperandCycleString;
      unsigned NOperandCycles = 0;
      std::string ItinBypassString;
      if (ItinData) {
        FormItineraryOperandCycleString(ItinData, ItinOperandCycleString,
                                        NOperandCycles);

        FormItineraryBypassString(std::string(Name), ItinData, ItinBypassString,
                                  NOperandCycles);
      }

      // Check to see if stage already exists and create if it doesn't
      uint16_t FindStage = 0;
      if (NStages > 0) {
        FindStage = ItinStageMap[ItinStageString];
        if (FindStage == 0) {
          // Emit as { cycles, u1 | u2 | ... | un, timeinc }, // indices
          StageTable += ItinStageString + ", // " + itostr(StageCount);
          if (NStages > 1)
            StageTable += "-" + itostr(StageCount + NStages - 1);
          StageTable += "\n";
          // Record Itin class number.
          ItinStageMap[ItinStageString] = FindStage = StageCount;
          StageCount += NStages;
        }
      }

      // Check to see if operand cycle already exists and create if it doesn't
      uint16_t FindOperandCycle = 0;
      if (NOperandCycles > 0) {
        std::string ItinOperandString =
            ItinOperandCycleString + ItinBypassString;
        FindOperandCycle = ItinOperandMap[ItinOperandString];
        if (FindOperandCycle == 0) {
          // Emit as  cycle, // index
          OperandCycleTable += ItinOperandCycleString + ", // ";
          std::string OperandIdxComment = itostr(OperandCycleCount);
          if (NOperandCycles > 1)
            OperandIdxComment +=
                "-" + itostr(OperandCycleCount + NOperandCycles - 1);
          OperandCycleTable += OperandIdxComment + "\n";
          // Record Itin class number.
          ItinOperandMap[ItinOperandCycleString] = FindOperandCycle =
              OperandCycleCount;
          // Emit as bypass, // index
          BypassTable += ItinBypassString + ", // " + OperandIdxComment + "\n";
          OperandCycleCount += NOperandCycles;
        }
      }

      // Set up itinerary as location and location + stage count
      int16_t NumUOps = ItinData ? ItinData->getValueAsInt("NumMicroOps") : 0;
      InstrItinerary Intinerary = {
          NumUOps,
          FindStage,
          uint16_t(FindStage + NStages),
          FindOperandCycle,
          uint16_t(FindOperandCycle + NOperandCycles),
      };

      // Inject - empty slots will be 0, 0
      ItinList[SchedClassIdx] = Intinerary;
    }
  }

  // Closing stage
  StageTable += "  { 0, 0, 0, llvm::InstrStage::Required } // End stages\n";
  StageTable += "};\n";

  // Closing operand cycles
  OperandCycleTable += "  0 // End operand cycles\n";
  OperandCycleTable += "};\n";

  BypassTable += " 0 // End bypass tables\n";
  BypassTable += "};\n";

  // Emit tables.
  OS << StageTable;
  OS << OperandCycleTable;
  OS << BypassTable;
}

//
// EmitProcessorData - Generate data for processor itineraries that were
// computed during EmitStageAndOperandCycleData(). ProcItinLists lists all
// Itineraries for each processor. The Itinerary lists are indexed on
// CodeGenSchedClass::Index.
//
void SubtargetEmitter::EmitItineraries(
    raw_ostream &OS, std::vector<std::vector<InstrItinerary>> &ProcItinLists) {
  // Multiple processor models may share an itinerary record. Emit it once.
  SmallPtrSet<Record *, 8> ItinsDefSet;

  // For each processor's machine model
  std::vector<std::vector<InstrItinerary>>::iterator ProcItinListsIter =
      ProcItinLists.begin();
  for (CodeGenSchedModels::ProcIter PI = SchedModels.procModelBegin(),
                                    PE = SchedModels.procModelEnd();
       PI != PE; ++PI, ++ProcItinListsIter) {

    Record *ItinsDef = PI->ItinsDef;
    if (!ItinsDefSet.insert(ItinsDef).second)
      continue;

    // Get the itinerary list for the processor.
    assert(ProcItinListsIter != ProcItinLists.end() && "bad iterator");
    std::vector<InstrItinerary> &ItinList = *ProcItinListsIter;

    // Empty itineraries aren't referenced anywhere in the tablegen output
    // so don't emit them.
    if (ItinList.empty())
      continue;

    OS << "\n";
    OS << "static const llvm::InstrItinerary ";

    // Begin processor itinerary table
    OS << ItinsDef->getName() << "[] = {\n";

    // For each itinerary class in CodeGenSchedClass::Index order.
    for (unsigned j = 0, M = ItinList.size(); j < M; ++j) {
      InstrItinerary &Intinerary = ItinList[j];

      // Emit Itinerary in the form of
      // { firstStage, lastStage, firstCycle, lastCycle } // index
      OS << "  { " << Intinerary.NumMicroOps << ", " << Intinerary.FirstStage
         << ", " << Intinerary.LastStage << ", " << Intinerary.FirstOperandCycle
         << ", " << Intinerary.LastOperandCycle << " }"
         << ", // " << j << " " << SchedModels.getSchedClass(j).Name << "\n";
    }
    // End processor itinerary table
    OS << "  { 0, uint16_t(~0U), uint16_t(~0U), uint16_t(~0U), uint16_t(~0U) }"
          "// end marker\n";
    OS << "};\n";
  }
}

// Emit either the value defined in the TableGen Record, or the default
// value defined in the C++ header. The Record is null if the processor does not
// define a model.
void SubtargetEmitter::EmitProcessorProp(raw_ostream &OS, const Record *R,
                                         StringRef Name, char Separator) {
  OS << "  ";
  int V = R ? R->getValueAsInt(Name) : -1;
  if (V >= 0)
    OS << V << Separator << " // " << Name;
  else
    OS << "MCSchedModel::Default" << Name << Separator;
  OS << '\n';
}

void SubtargetEmitter::EmitProcessorResourceSubUnits(
    const CodeGenProcModel &ProcModel, raw_ostream &OS) {
  OS << "\nstatic const unsigned " << ProcModel.ModelName
     << "ProcResourceSubUnits[] = {\n"
     << "  0,  // Invalid\n";

  for (unsigned i = 0, e = ProcModel.ProcResourceDefs.size(); i < e; ++i) {
    Record *PRDef = ProcModel.ProcResourceDefs[i];
    if (!PRDef->isSubClassOf("ProcResGroup"))
      continue;
    RecVec ResUnits = PRDef->getValueAsListOfDefs("Resources");
    for (Record *RUDef : ResUnits) {
      Record *const RU =
          SchedModels.findProcResUnits(RUDef, ProcModel, PRDef->getLoc());
      for (unsigned J = 0; J < RU->getValueAsInt("NumUnits"); ++J) {
        OS << "  " << ProcModel.getProcResourceIdx(RU) << ", ";
      }
    }
    OS << "  // " << PRDef->getName() << "\n";
  }
  OS << "};\n";
}

static void EmitRetireControlUnitInfo(const CodeGenProcModel &ProcModel,
                                      raw_ostream &OS) {
  int64_t ReorderBufferSize = 0, MaxRetirePerCycle = 0;
  if (Record *RCU = ProcModel.RetireControlUnit) {
    ReorderBufferSize =
        std::max(ReorderBufferSize, RCU->getValueAsInt("ReorderBufferSize"));
    MaxRetirePerCycle =
        std::max(MaxRetirePerCycle, RCU->getValueAsInt("MaxRetirePerCycle"));
  }

  OS << ReorderBufferSize << ", // ReorderBufferSize\n  ";
  OS << MaxRetirePerCycle << ", // MaxRetirePerCycle\n  ";
}

static void EmitRegisterFileInfo(const CodeGenProcModel &ProcModel,
                                 unsigned NumRegisterFiles,
                                 unsigned NumCostEntries, raw_ostream &OS) {
  if (NumRegisterFiles)
    OS << ProcModel.ModelName << "RegisterFiles,\n  " << (1 + NumRegisterFiles);
  else
    OS << "nullptr,\n  0";

  OS << ", // Number of register files.\n  ";
  if (NumCostEntries)
    OS << ProcModel.ModelName << "RegisterCosts,\n  ";
  else
    OS << "nullptr,\n  ";
  OS << NumCostEntries << ", // Number of register cost entries.\n";
}

unsigned
SubtargetEmitter::EmitRegisterFileTables(const CodeGenProcModel &ProcModel,
                                         raw_ostream &OS) {
  if (llvm::all_of(ProcModel.RegisterFiles, [](const CodeGenRegisterFile &RF) {
        return RF.hasDefaultCosts();
      }))
    return 0;

  // Print the RegisterCost table first.
  OS << "\n// {RegisterClassID, Register Cost, AllowMoveElimination }\n";
  OS << "static const llvm::MCRegisterCostEntry " << ProcModel.ModelName
     << "RegisterCosts"
     << "[] = {\n";

  for (const CodeGenRegisterFile &RF : ProcModel.RegisterFiles) {
    // Skip register files with a default cost table.
    if (RF.hasDefaultCosts())
      continue;
    // Add entries to the cost table.
    for (const CodeGenRegisterCost &RC : RF.Costs) {
      OS << "  { ";
      Record *Rec = RC.RCDef;
      if (Rec->getValue("Namespace"))
        OS << Rec->getValueAsString("Namespace") << "::";
      OS << Rec->getName() << "RegClassID, " << RC.Cost << ", "
         << RC.AllowMoveElimination << "},\n";
    }
  }
  OS << "};\n";

  // Now generate a table with register file info.
  OS << "\n // {Name, #PhysRegs, #CostEntries, IndexToCostTbl, "
     << "MaxMovesEliminatedPerCycle, AllowZeroMoveEliminationOnly }\n";
  OS << "static const llvm::MCRegisterFileDesc " << ProcModel.ModelName
     << "RegisterFiles"
     << "[] = {\n"
     << "  { \"InvalidRegisterFile\", 0, 0, 0, 0, 0 },\n";
  unsigned CostTblIndex = 0;

  for (const CodeGenRegisterFile &RD : ProcModel.RegisterFiles) {
    OS << "  { ";
    OS << '"' << RD.Name << '"' << ", " << RD.NumPhysRegs << ", ";
    unsigned NumCostEntries = RD.Costs.size();
    OS << NumCostEntries << ", " << CostTblIndex << ", "
       << RD.MaxMovesEliminatedPerCycle << ", "
       << RD.AllowZeroMoveEliminationOnly << "},\n";
    CostTblIndex += NumCostEntries;
  }
  OS << "};\n";

  return CostTblIndex;
}

void SubtargetEmitter::EmitLoadStoreQueueInfo(const CodeGenProcModel &ProcModel,
                                              raw_ostream &OS) {
  unsigned QueueID = 0;
  if (ProcModel.LoadQueue) {
    const Record *Queue = ProcModel.LoadQueue->getValueAsDef("QueueDescriptor");
    QueueID = 1 + std::distance(ProcModel.ProcResourceDefs.begin(),
                                find(ProcModel.ProcResourceDefs, Queue));
  }
  OS << "  " << QueueID << ", // Resource Descriptor for the Load Queue\n";

  QueueID = 0;
  if (ProcModel.StoreQueue) {
    const Record *Queue =
        ProcModel.StoreQueue->getValueAsDef("QueueDescriptor");
    QueueID = 1 + std::distance(ProcModel.ProcResourceDefs.begin(),
                                find(ProcModel.ProcResourceDefs, Queue));
  }
  OS << "  " << QueueID << ", // Resource Descriptor for the Store Queue\n";
}

void SubtargetEmitter::EmitExtraProcessorInfo(const CodeGenProcModel &ProcModel,
                                              raw_ostream &OS) {
  // Generate a table of register file descriptors (one entry per each user
  // defined register file), and a table of register costs.
  unsigned NumCostEntries = EmitRegisterFileTables(ProcModel, OS);

  // Now generate a table for the extra processor info.
  OS << "\nstatic const llvm::MCExtraProcessorInfo " << ProcModel.ModelName
     << "ExtraInfo = {\n  ";

  // Add information related to the retire control unit.
  EmitRetireControlUnitInfo(ProcModel, OS);

  // Add information related to the register files (i.e. where to find register
  // file descriptors and register costs).
  EmitRegisterFileInfo(ProcModel, ProcModel.RegisterFiles.size(),
                       NumCostEntries, OS);

  // Add information about load/store queues.
  EmitLoadStoreQueueInfo(ProcModel, OS);

  OS << "};\n";
}

void SubtargetEmitter::EmitProcessorResources(const CodeGenProcModel &ProcModel,
                                              raw_ostream &OS) {
  EmitProcessorResourceSubUnits(ProcModel, OS);

  OS << "\n// {Name, NumUnits, SuperIdx, BufferSize, SubUnitsIdxBegin}\n";
  OS << "static const llvm::MCProcResourceDesc " << ProcModel.ModelName
     << "ProcResources"
     << "[] = {\n"
     << "  {\"InvalidUnit\", 0, 0, 0, 0},\n";

  unsigned SubUnitsOffset = 1;
  for (unsigned i = 0, e = ProcModel.ProcResourceDefs.size(); i < e; ++i) {
    Record *PRDef = ProcModel.ProcResourceDefs[i];

    Record *SuperDef = nullptr;
    unsigned SuperIdx = 0;
    unsigned NumUnits = 0;
    const unsigned SubUnitsBeginOffset = SubUnitsOffset;
    int BufferSize = PRDef->getValueAsInt("BufferSize");
    if (PRDef->isSubClassOf("ProcResGroup")) {
      RecVec ResUnits = PRDef->getValueAsListOfDefs("Resources");
      for (Record *RU : ResUnits) {
        NumUnits += RU->getValueAsInt("NumUnits");
        SubUnitsOffset += RU->getValueAsInt("NumUnits");
      }
    } else {
      // Find the SuperIdx
      if (PRDef->getValueInit("Super")->isComplete()) {
        SuperDef = SchedModels.findProcResUnits(PRDef->getValueAsDef("Super"),
                                                ProcModel, PRDef->getLoc());
        SuperIdx = ProcModel.getProcResourceIdx(SuperDef);
      }
      NumUnits = PRDef->getValueAsInt("NumUnits");
    }
    // Emit the ProcResourceDesc
    OS << "  {\"" << PRDef->getName() << "\", ";
    if (PRDef->getName().size() < 15)
      OS.indent(15 - PRDef->getName().size());
    OS << NumUnits << ", " << SuperIdx << ", " << BufferSize << ", ";
    if (SubUnitsBeginOffset != SubUnitsOffset) {
      OS << ProcModel.ModelName << "ProcResourceSubUnits + "
         << SubUnitsBeginOffset;
    } else {
      OS << "nullptr";
    }
    OS << "}, // #" << i + 1;
    if (SuperDef)
      OS << ", Super=" << SuperDef->getName();
    OS << "\n";
  }
  OS << "};\n";
}

// Find the WriteRes Record that defines processor resources for this
// SchedWrite.
Record *
SubtargetEmitter::FindWriteResources(const CodeGenSchedRW &SchedWrite,
                                     const CodeGenProcModel &ProcModel) {

  // Check if the SchedWrite is already subtarget-specific and directly
  // specifies a set of processor resources.
  if (SchedWrite.TheDef->isSubClassOf("SchedWriteRes"))
    return SchedWrite.TheDef;

  Record *AliasDef = nullptr;
  for (Record *A : SchedWrite.Aliases) {
    const CodeGenSchedRW &AliasRW =
        SchedModels.getSchedRW(A->getValueAsDef("AliasRW"));
    if (AliasRW.TheDef->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = AliasRW.TheDef->getValueAsDef("SchedModel");
      if (&SchedModels.getProcModel(ModelDef) != &ProcModel)
        continue;
    }
    if (AliasDef)
      PrintFatalError(AliasRW.TheDef->getLoc(),
                      "Multiple aliases "
                      "defined for processor " +
                          ProcModel.ModelName +
                          " Ensure only one SchedAlias exists per RW.");
    AliasDef = AliasRW.TheDef;
  }
  if (AliasDef && AliasDef->isSubClassOf("SchedWriteRes"))
    return AliasDef;

  // Check this processor's list of write resources.
  Record *ResDef = nullptr;
  for (Record *WR : ProcModel.WriteResDefs) {
    if (!WR->isSubClassOf("WriteRes"))
      continue;
    Record *WRDef = WR->getValueAsDef("WriteType");
    if (AliasDef == WRDef || SchedWrite.TheDef == WRDef) {
      if (ResDef) {
        PrintFatalError(WR->getLoc(), "Resources are defined for both "
                                      "SchedWrite and its alias on processor " +
                                          ProcModel.ModelName);
      }
      ResDef = WR;
      // If there is no AliasDef and we find a match, we can early exit since
      // there is no need to verify whether there are resources defined for both
      // SchedWrite and its alias.
      if (!AliasDef)
        break;
    }
  }
  // TODO: If ProcModel has a base model (previous generation processor),
  // then call FindWriteResources recursively with that model here.
  if (!ResDef) {
    PrintFatalError(ProcModel.ModelDef->getLoc(),
                    Twine("Processor does not define resources for ") +
                        SchedWrite.TheDef->getName());
  }
  return ResDef;
}

/// Find the ReadAdvance record for the given SchedRead on this processor or
/// return NULL.
Record *SubtargetEmitter::FindReadAdvance(const CodeGenSchedRW &SchedRead,
                                          const CodeGenProcModel &ProcModel) {
  // Check for SchedReads that directly specify a ReadAdvance.
  if (SchedRead.TheDef->isSubClassOf("SchedReadAdvance"))
    return SchedRead.TheDef;

  // Check this processor's list of aliases for SchedRead.
  Record *AliasDef = nullptr;
  for (Record *A : SchedRead.Aliases) {
    const CodeGenSchedRW &AliasRW =
        SchedModels.getSchedRW(A->getValueAsDef("AliasRW"));
    if (AliasRW.TheDef->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = AliasRW.TheDef->getValueAsDef("SchedModel");
      if (&SchedModels.getProcModel(ModelDef) != &ProcModel)
        continue;
    }
    if (AliasDef)
      PrintFatalError(AliasRW.TheDef->getLoc(),
                      "Multiple aliases "
                      "defined for processor " +
                          ProcModel.ModelName +
                          " Ensure only one SchedAlias exists per RW.");
    AliasDef = AliasRW.TheDef;
  }
  if (AliasDef && AliasDef->isSubClassOf("SchedReadAdvance"))
    return AliasDef;

  // Check this processor's ReadAdvanceList.
  Record *ResDef = nullptr;
  for (Record *RA : ProcModel.ReadAdvanceDefs) {
    if (!RA->isSubClassOf("ReadAdvance"))
      continue;
    Record *RADef = RA->getValueAsDef("ReadType");
    if (AliasDef == RADef || SchedRead.TheDef == RADef) {
      if (ResDef) {
        PrintFatalError(RA->getLoc(), "Resources are defined for both "
                                      "SchedRead and its alias on processor " +
                                          ProcModel.ModelName);
      }
      ResDef = RA;
      // If there is no AliasDef and we find a match, we can early exit since
      // there is no need to verify whether there are resources defined for both
      // SchedRead and its alias.
      if (!AliasDef)
        break;
    }
  }
  // TODO: If ProcModel has a base model (previous generation processor),
  // then call FindReadAdvance recursively with that model here.
  if (!ResDef && SchedRead.TheDef->getName() != "ReadDefault") {
    PrintFatalError(ProcModel.ModelDef->getLoc(),
                    Twine("Processor does not define resources for ") +
                        SchedRead.TheDef->getName());
  }
  return ResDef;
}

// Expand an explicit list of processor resources into a full list of implied
// resource groups and super resources that cover them.
void SubtargetEmitter::ExpandProcResources(
    RecVec &PRVec, std::vector<int64_t> &ReleaseAtCycles,
    std::vector<int64_t> &AcquireAtCycles, const CodeGenProcModel &PM) {
  assert(PRVec.size() == ReleaseAtCycles.size() && "failed precondition");
  for (unsigned i = 0, e = PRVec.size(); i != e; ++i) {
    Record *PRDef = PRVec[i];
    RecVec SubResources;
    if (PRDef->isSubClassOf("ProcResGroup"))
      SubResources = PRDef->getValueAsListOfDefs("Resources");
    else {
      SubResources.push_back(PRDef);
      PRDef = SchedModels.findProcResUnits(PRDef, PM, PRDef->getLoc());
      for (Record *SubDef = PRDef;
           SubDef->getValueInit("Super")->isComplete();) {
        if (SubDef->isSubClassOf("ProcResGroup")) {
          // Disallow this for simplicitly.
          PrintFatalError(SubDef->getLoc(), "Processor resource group "
                                            " cannot be a super resources.");
        }
        Record *SuperDef = SchedModels.findProcResUnits(
            SubDef->getValueAsDef("Super"), PM, SubDef->getLoc());
        PRVec.push_back(SuperDef);
        ReleaseAtCycles.push_back(ReleaseAtCycles[i]);
        AcquireAtCycles.push_back(AcquireAtCycles[i]);
        SubDef = SuperDef;
      }
    }
    for (Record *PR : PM.ProcResourceDefs) {
      if (PR == PRDef || !PR->isSubClassOf("ProcResGroup"))
        continue;
      RecVec SuperResources = PR->getValueAsListOfDefs("Resources");
      RecIter SubI = SubResources.begin(), SubE = SubResources.end();
      for (; SubI != SubE; ++SubI) {
        if (!is_contained(SuperResources, *SubI)) {
          break;
        }
      }
      if (SubI == SubE) {
        PRVec.push_back(PR);
        ReleaseAtCycles.push_back(ReleaseAtCycles[i]);
        AcquireAtCycles.push_back(AcquireAtCycles[i]);
      }
    }
  }
}

// Generate the SchedClass table for this processor and update global
// tables. Must be called for each processor in order.
void SubtargetEmitter::GenSchedClassTables(const CodeGenProcModel &ProcModel,
                                           SchedClassTables &SchedTables) {
  std::vector<MCSchedClassDesc> &SCTab =
      SchedTables.ProcSchedClasses.emplace_back();
  if (!ProcModel.hasInstrSchedModel())
    return;

  LLVM_DEBUG(dbgs() << "\n+++ SCHED CLASSES (GenSchedClassTables) +++\n");
  for (const CodeGenSchedClass &SC : SchedModels.schedClasses()) {
    LLVM_DEBUG(SC.dump(&SchedModels));

    MCSchedClassDesc &SCDesc = SCTab.emplace_back();
    // SCDesc.Name is guarded by NDEBUG
    SCDesc.NumMicroOps = 0;
    SCDesc.BeginGroup = false;
    SCDesc.EndGroup = false;
    SCDesc.RetireOOO = false;
    SCDesc.WriteProcResIdx = 0;
    SCDesc.WriteLatencyIdx = 0;
    SCDesc.ReadAdvanceIdx = 0;

    // A Variant SchedClass has no resources of its own.
    bool HasVariants = false;
    for (const CodeGenSchedTransition &CGT :
         make_range(SC.Transitions.begin(), SC.Transitions.end())) {
      if (CGT.ProcIndex == ProcModel.Index) {
        HasVariants = true;
        break;
      }
    }
    if (HasVariants) {
      SCDesc.NumMicroOps = MCSchedClassDesc::VariantNumMicroOps;
      continue;
    }

    // Determine if the SchedClass is actually reachable on this processor. If
    // not don't try to locate the processor resources, it will fail.
    // If ProcIndices contains 0, this class applies to all processors.
    assert(!SC.ProcIndices.empty() && "expect at least one procidx");
    if (SC.ProcIndices[0] != 0) {
      if (!is_contained(SC.ProcIndices, ProcModel.Index))
        continue;
    }
    IdxVec Writes = SC.Writes;
    IdxVec Reads = SC.Reads;
    if (!SC.InstRWs.empty()) {
      // This class has a default ReadWrite list which can be overridden by
      // InstRW definitions.
      Record *RWDef = nullptr;
      for (Record *RW : SC.InstRWs) {
        Record *RWModelDef = RW->getValueAsDef("SchedModel");
        if (&ProcModel == &SchedModels.getProcModel(RWModelDef)) {
          RWDef = RW;
          break;
        }
      }
      if (RWDef) {
        Writes.clear();
        Reads.clear();
        SchedModels.findRWs(RWDef->getValueAsListOfDefs("OperandReadWrites"),
                            Writes, Reads);
      }
    }
    if (Writes.empty()) {
      // Check this processor's itinerary class resources.
      for (Record *I : ProcModel.ItinRWDefs) {
        RecVec Matched = I->getValueAsListOfDefs("MatchedItinClasses");
        if (is_contained(Matched, SC.ItinClassDef)) {
          SchedModels.findRWs(I->getValueAsListOfDefs("OperandReadWrites"),
                              Writes, Reads);
          break;
        }
      }
      if (Writes.empty()) {
        LLVM_DEBUG(dbgs() << ProcModel.ModelName
                          << " does not have resources for class " << SC.Name
                          << '\n');
        SCDesc.NumMicroOps = MCSchedClassDesc::InvalidNumMicroOps;
      }
    }
    // Sum resources across all operand writes.
    std::vector<MCWriteProcResEntry> WriteProcResources;
    std::vector<MCWriteLatencyEntry> WriteLatencies;
    std::vector<std::string> WriterNames;
    std::vector<MCReadAdvanceEntry> ReadAdvanceEntries;
    for (unsigned W : Writes) {
      IdxVec WriteSeq;
      SchedModels.expandRWSeqForProc(W, WriteSeq, /*IsRead=*/false, ProcModel);

      // For each operand, create a latency entry.
      MCWriteLatencyEntry WLEntry;
      WLEntry.Cycles = 0;
      unsigned WriteID = WriteSeq.back();
      WriterNames.push_back(SchedModels.getSchedWrite(WriteID).Name);
      // If this Write is not referenced by a ReadAdvance, don't distinguish it
      // from other WriteLatency entries.
      if (!ProcModel.hasReadOfWrite(SchedModels.getSchedWrite(WriteID).TheDef))
        WriteID = 0;
      WLEntry.WriteResourceID = WriteID;

      for (unsigned WS : WriteSeq) {

        Record *WriteRes =
            FindWriteResources(SchedModels.getSchedWrite(WS), ProcModel);

        // Mark the parent class as invalid for unsupported write types.
        if (WriteRes->getValueAsBit("Unsupported")) {
          SCDesc.NumMicroOps = MCSchedClassDesc::InvalidNumMicroOps;
          break;
        }
        WLEntry.Cycles += WriteRes->getValueAsInt("Latency");
        SCDesc.NumMicroOps += WriteRes->getValueAsInt("NumMicroOps");
        SCDesc.BeginGroup |= WriteRes->getValueAsBit("BeginGroup");
        SCDesc.EndGroup |= WriteRes->getValueAsBit("EndGroup");
        SCDesc.BeginGroup |= WriteRes->getValueAsBit("SingleIssue");
        SCDesc.EndGroup |= WriteRes->getValueAsBit("SingleIssue");
        SCDesc.RetireOOO |= WriteRes->getValueAsBit("RetireOOO");

        // Create an entry for each ProcResource listed in WriteRes.
        RecVec PRVec = WriteRes->getValueAsListOfDefs("ProcResources");
        std::vector<int64_t> ReleaseAtCycles =
            WriteRes->getValueAsListOfInts("ReleaseAtCycles");

        std::vector<int64_t> AcquireAtCycles =
            WriteRes->getValueAsListOfInts("AcquireAtCycles");

        // Check consistency of the two vectors carrying the start and
        // stop cycles of the resources.
        if (!ReleaseAtCycles.empty() &&
            ReleaseAtCycles.size() != PRVec.size()) {
          // If ReleaseAtCycles is provided, check consistency.
          PrintFatalError(
              WriteRes->getLoc(),
              Twine("Inconsistent release at cycles: size(ReleaseAtCycles) != "
                    "size(ProcResources): ")
                  .concat(Twine(PRVec.size()))
                  .concat(" vs ")
                  .concat(Twine(ReleaseAtCycles.size())));
        }

        if (!AcquireAtCycles.empty() &&
            AcquireAtCycles.size() != PRVec.size()) {
          PrintFatalError(
              WriteRes->getLoc(),
              Twine("Inconsistent resource cycles: size(AcquireAtCycles) != "
                    "size(ProcResources): ")
                  .concat(Twine(AcquireAtCycles.size()))
                  .concat(" vs ")
                  .concat(Twine(PRVec.size())));
        }

        if (ReleaseAtCycles.empty()) {
          // If ReleaseAtCycles is not provided, default to one cycle
          // per resource.
          ReleaseAtCycles.resize(PRVec.size(), 1);
        }

        if (AcquireAtCycles.empty()) {
          // If AcquireAtCycles is not provided, reserve the resource
          // starting from cycle 0.
          AcquireAtCycles.resize(PRVec.size(), 0);
        }

        assert(AcquireAtCycles.size() == ReleaseAtCycles.size());

        ExpandProcResources(PRVec, ReleaseAtCycles, AcquireAtCycles, ProcModel);
        assert(AcquireAtCycles.size() == ReleaseAtCycles.size());

        for (unsigned PRIdx = 0, PREnd = PRVec.size(); PRIdx != PREnd;
             ++PRIdx) {
          MCWriteProcResEntry WPREntry;
          WPREntry.ProcResourceIdx = ProcModel.getProcResourceIdx(PRVec[PRIdx]);
          assert(WPREntry.ProcResourceIdx && "Bad ProcResourceIdx");
          WPREntry.ReleaseAtCycle = ReleaseAtCycles[PRIdx];
          WPREntry.AcquireAtCycle = AcquireAtCycles[PRIdx];
          if (AcquireAtCycles[PRIdx] > ReleaseAtCycles[PRIdx]) {
            PrintFatalError(
                WriteRes->getLoc(),
                Twine("Inconsistent resource cycles: AcquireAtCycles "
                      "< ReleaseAtCycles must hold."));
          }
          if (AcquireAtCycles[PRIdx] < 0) {
            PrintFatalError(WriteRes->getLoc(),
                            Twine("Invalid value: AcquireAtCycle "
                                  "must be a non-negative value."));
          }
          // If this resource is already used in this sequence, add the current
          // entry's cycles so that the same resource appears to be used
          // serially, rather than multiple parallel uses. This is important for
          // in-order machine where the resource consumption is a hazard.
          unsigned WPRIdx = 0, WPREnd = WriteProcResources.size();
          for (; WPRIdx != WPREnd; ++WPRIdx) {
            if (WriteProcResources[WPRIdx].ProcResourceIdx ==
                WPREntry.ProcResourceIdx) {
              // TODO: multiple use of the same resources would
              // require either 1. thinking of how to handle multiple
              // intervals for the same resource in
              // `<Target>WriteProcResTable` (see
              // `SubtargetEmitter::EmitSchedClassTables`), or
              // 2. thinking how to merge multiple intervals into a
              // single interval.
              assert(WPREntry.AcquireAtCycle == 0 &&
                     "multiple use ofthe same resource is not yet handled");
              WriteProcResources[WPRIdx].ReleaseAtCycle +=
                  WPREntry.ReleaseAtCycle;
              break;
            }
          }
          if (WPRIdx == WPREnd)
            WriteProcResources.push_back(WPREntry);
        }
      }
      WriteLatencies.push_back(WLEntry);
    }
    // Create an entry for each operand Read in this SchedClass.
    // Entries must be sorted first by UseIdx then by WriteResourceID.
    for (unsigned UseIdx = 0, EndIdx = Reads.size(); UseIdx != EndIdx;
         ++UseIdx) {
      Record *ReadAdvance =
          FindReadAdvance(SchedModels.getSchedRead(Reads[UseIdx]), ProcModel);
      if (!ReadAdvance)
        continue;

      // Mark the parent class as invalid for unsupported write types.
      if (ReadAdvance->getValueAsBit("Unsupported")) {
        SCDesc.NumMicroOps = MCSchedClassDesc::InvalidNumMicroOps;
        break;
      }
      RecVec ValidWrites = ReadAdvance->getValueAsListOfDefs("ValidWrites");
      IdxVec WriteIDs;
      if (ValidWrites.empty())
        WriteIDs.push_back(0);
      else {
        for (Record *VW : ValidWrites) {
          unsigned WriteID = SchedModels.getSchedRWIdx(VW, /*IsRead=*/false);
          assert(WriteID != 0 &&
                 "Expected a valid SchedRW in the list of ValidWrites");
          WriteIDs.push_back(WriteID);
        }
      }
      llvm::sort(WriteIDs);
      for (unsigned W : WriteIDs) {
        MCReadAdvanceEntry RAEntry;
        RAEntry.UseIdx = UseIdx;
        RAEntry.WriteResourceID = W;
        RAEntry.Cycles = ReadAdvance->getValueAsInt("Cycles");
        ReadAdvanceEntries.push_back(RAEntry);
      }
    }
    if (SCDesc.NumMicroOps == MCSchedClassDesc::InvalidNumMicroOps) {
      WriteProcResources.clear();
      WriteLatencies.clear();
      ReadAdvanceEntries.clear();
    }
    // Add the information for this SchedClass to the global tables using basic
    // compression.
    //
    // WritePrecRes entries are sorted by ProcResIdx.
    llvm::sort(WriteProcResources, LessWriteProcResources());

    SCDesc.NumWriteProcResEntries = WriteProcResources.size();
    std::vector<MCWriteProcResEntry>::iterator WPRPos =
        std::search(SchedTables.WriteProcResources.begin(),
                    SchedTables.WriteProcResources.end(),
                    WriteProcResources.begin(), WriteProcResources.end());
    if (WPRPos != SchedTables.WriteProcResources.end())
      SCDesc.WriteProcResIdx = WPRPos - SchedTables.WriteProcResources.begin();
    else {
      SCDesc.WriteProcResIdx = SchedTables.WriteProcResources.size();
      SchedTables.WriteProcResources.insert(WPRPos, WriteProcResources.begin(),
                                            WriteProcResources.end());
    }
    // Latency entries must remain in operand order.
    SCDesc.NumWriteLatencyEntries = WriteLatencies.size();
    std::vector<MCWriteLatencyEntry>::iterator WLPos = std::search(
        SchedTables.WriteLatencies.begin(), SchedTables.WriteLatencies.end(),
        WriteLatencies.begin(), WriteLatencies.end());
    if (WLPos != SchedTables.WriteLatencies.end()) {
      unsigned idx = WLPos - SchedTables.WriteLatencies.begin();
      SCDesc.WriteLatencyIdx = idx;
      for (unsigned i = 0, e = WriteLatencies.size(); i < e; ++i)
        if (SchedTables.WriterNames[idx + i].find(WriterNames[i]) ==
            std::string::npos) {
          SchedTables.WriterNames[idx + i] += std::string("_") + WriterNames[i];
        }
    } else {
      SCDesc.WriteLatencyIdx = SchedTables.WriteLatencies.size();
      llvm::append_range(SchedTables.WriteLatencies, WriteLatencies);
      llvm::append_range(SchedTables.WriterNames, WriterNames);
    }
    // ReadAdvanceEntries must remain in operand order.
    SCDesc.NumReadAdvanceEntries = ReadAdvanceEntries.size();
    std::vector<MCReadAdvanceEntry>::iterator RAPos =
        std::search(SchedTables.ReadAdvanceEntries.begin(),
                    SchedTables.ReadAdvanceEntries.end(),
                    ReadAdvanceEntries.begin(), ReadAdvanceEntries.end());
    if (RAPos != SchedTables.ReadAdvanceEntries.end())
      SCDesc.ReadAdvanceIdx = RAPos - SchedTables.ReadAdvanceEntries.begin();
    else {
      SCDesc.ReadAdvanceIdx = SchedTables.ReadAdvanceEntries.size();
      llvm::append_range(SchedTables.ReadAdvanceEntries, ReadAdvanceEntries);
    }
  }
}

// Emit SchedClass tables for all processors and associated global tables.
void SubtargetEmitter::EmitSchedClassTables(SchedClassTables &SchedTables,
                                            raw_ostream &OS) {
  // Emit global WriteProcResTable.
  OS << "\n// {ProcResourceIdx, ReleaseAtCycle, AcquireAtCycle}\n"
     << "extern const llvm::MCWriteProcResEntry " << Target
     << "WriteProcResTable[] = {\n"
     << "  { 0,  0,  0 }, // Invalid\n";
  for (unsigned WPRIdx = 1, WPREnd = SchedTables.WriteProcResources.size();
       WPRIdx != WPREnd; ++WPRIdx) {
    MCWriteProcResEntry &WPREntry = SchedTables.WriteProcResources[WPRIdx];
    OS << "  {" << format("%2d", WPREntry.ProcResourceIdx) << ", "
       << format("%2d", WPREntry.ReleaseAtCycle) << ",  "
       << format("%2d", WPREntry.AcquireAtCycle) << "}";
    if (WPRIdx + 1 < WPREnd)
      OS << ',';
    OS << " // #" << WPRIdx << '\n';
  }
  OS << "}; // " << Target << "WriteProcResTable\n";

  // Emit global WriteLatencyTable.
  OS << "\n// {Cycles, WriteResourceID}\n"
     << "extern const llvm::MCWriteLatencyEntry " << Target
     << "WriteLatencyTable[] = {\n"
     << "  { 0,  0}, // Invalid\n";
  for (unsigned WLIdx = 1, WLEnd = SchedTables.WriteLatencies.size();
       WLIdx != WLEnd; ++WLIdx) {
    MCWriteLatencyEntry &WLEntry = SchedTables.WriteLatencies[WLIdx];
    OS << "  {" << format("%2d", WLEntry.Cycles) << ", "
       << format("%2d", WLEntry.WriteResourceID) << "}";
    if (WLIdx + 1 < WLEnd)
      OS << ',';
    OS << " // #" << WLIdx << " " << SchedTables.WriterNames[WLIdx] << '\n';
  }
  OS << "}; // " << Target << "WriteLatencyTable\n";

  // Emit global ReadAdvanceTable.
  OS << "\n// {UseIdx, WriteResourceID, Cycles}\n"
     << "extern const llvm::MCReadAdvanceEntry " << Target
     << "ReadAdvanceTable[] = {\n"
     << "  {0,  0,  0}, // Invalid\n";
  for (unsigned RAIdx = 1, RAEnd = SchedTables.ReadAdvanceEntries.size();
       RAIdx != RAEnd; ++RAIdx) {
    MCReadAdvanceEntry &RAEntry = SchedTables.ReadAdvanceEntries[RAIdx];
    OS << "  {" << RAEntry.UseIdx << ", "
       << format("%2d", RAEntry.WriteResourceID) << ", "
       << format("%2d", RAEntry.Cycles) << "}";
    if (RAIdx + 1 < RAEnd)
      OS << ',';
    OS << " // #" << RAIdx << '\n';
  }
  OS << "}; // " << Target << "ReadAdvanceTable\n";

  // Emit a SchedClass table for each processor.
  for (CodeGenSchedModels::ProcIter PI = SchedModels.procModelBegin(),
                                    PE = SchedModels.procModelEnd();
       PI != PE; ++PI) {
    if (!PI->hasInstrSchedModel())
      continue;

    std::vector<MCSchedClassDesc> &SCTab =
        SchedTables.ProcSchedClasses[1 + (PI - SchedModels.procModelBegin())];

    OS << "\n// {Name, NumMicroOps, BeginGroup, EndGroup, RetireOOO,"
       << " WriteProcResIdx,#, WriteLatencyIdx,#, ReadAdvanceIdx,#}\n";
    OS << "static const llvm::MCSchedClassDesc " << PI->ModelName
       << "SchedClasses[] = {\n";

    // The first class is always invalid. We no way to distinguish it except by
    // name and position.
    assert(SchedModels.getSchedClass(0).Name == "NoInstrModel" &&
           "invalid class not first");
    OS << "  {DBGFIELD(\"InvalidSchedClass\")  "
       << MCSchedClassDesc::InvalidNumMicroOps
       << ", false, false, false, 0, 0,  0, 0,  0, 0},\n";

    for (unsigned SCIdx = 1, SCEnd = SCTab.size(); SCIdx != SCEnd; ++SCIdx) {
      MCSchedClassDesc &MCDesc = SCTab[SCIdx];
      const CodeGenSchedClass &SchedClass = SchedModels.getSchedClass(SCIdx);
      OS << "  {DBGFIELD(\"" << SchedClass.Name << "\") ";
      if (SchedClass.Name.size() < 18)
        OS.indent(18 - SchedClass.Name.size());
      OS << MCDesc.NumMicroOps << ", " << (MCDesc.BeginGroup ? "true" : "false")
         << ", " << (MCDesc.EndGroup ? "true" : "false") << ", "
         << (MCDesc.RetireOOO ? "true" : "false") << ", "
         << format("%2d", MCDesc.WriteProcResIdx) << ", "
         << MCDesc.NumWriteProcResEntries << ", "
         << format("%2d", MCDesc.WriteLatencyIdx) << ", "
         << MCDesc.NumWriteLatencyEntries << ", "
         << format("%2d", MCDesc.ReadAdvanceIdx) << ", "
         << MCDesc.NumReadAdvanceEntries << "}, // #" << SCIdx << '\n';
    }
    OS << "}; // " << PI->ModelName << "SchedClasses\n";
  }
}

void SubtargetEmitter::EmitProcessorModels(raw_ostream &OS) {
  // For each processor model.
  for (const CodeGenProcModel &PM : SchedModels.procModels()) {
    // Emit extra processor info if available.
    if (PM.hasExtraProcessorInfo())
      EmitExtraProcessorInfo(PM, OS);
    // Emit processor resource table.
    if (PM.hasInstrSchedModel())
      EmitProcessorResources(PM, OS);
    else if (!PM.ProcResourceDefs.empty())
      PrintFatalError(PM.ModelDef->getLoc(),
                      "SchedMachineModel defines "
                      "ProcResources without defining WriteRes SchedWriteRes");

    // Begin processor itinerary properties
    OS << "\n";
    OS << "static const llvm::MCSchedModel " << PM.ModelName << " = {\n";
    EmitProcessorProp(OS, PM.ModelDef, "IssueWidth", ',');
    EmitProcessorProp(OS, PM.ModelDef, "MicroOpBufferSize", ',');
    EmitProcessorProp(OS, PM.ModelDef, "LoopMicroOpBufferSize", ',');
    EmitProcessorProp(OS, PM.ModelDef, "LoadLatency", ',');
    EmitProcessorProp(OS, PM.ModelDef, "HighLatency", ',');
    EmitProcessorProp(OS, PM.ModelDef, "MispredictPenalty", ',');

    bool PostRAScheduler =
        (PM.ModelDef ? PM.ModelDef->getValueAsBit("PostRAScheduler") : false);

    OS << "  " << (PostRAScheduler ? "true" : "false") << ", // "
       << "PostRAScheduler\n";

    bool CompleteModel =
        (PM.ModelDef ? PM.ModelDef->getValueAsBit("CompleteModel") : false);

    OS << "  " << (CompleteModel ? "true" : "false") << ", // "
       << "CompleteModel\n";

    bool EnableIntervals =
        (PM.ModelDef ? PM.ModelDef->getValueAsBit("EnableIntervals") : false);

    OS << "  " << (EnableIntervals ? "true" : "false") << ", // "
       << "EnableIntervals\n";

    OS << "  " << PM.Index << ", // Processor ID\n";
    if (PM.hasInstrSchedModel())
      OS << "  " << PM.ModelName << "ProcResources"
         << ",\n"
         << "  " << PM.ModelName << "SchedClasses"
         << ",\n"
         << "  " << PM.ProcResourceDefs.size() + 1 << ",\n"
         << "  "
         << (SchedModels.schedClassEnd() - SchedModels.schedClassBegin())
         << ",\n";
    else
      OS << "  nullptr, nullptr, 0, 0,"
         << " // No instruction-level machine model.\n";
    if (PM.hasItineraries())
      OS << "  " << PM.ItinsDef->getName() << ",\n";
    else
      OS << "  nullptr, // No Itinerary\n";
    if (PM.hasExtraProcessorInfo())
      OS << "  &" << PM.ModelName << "ExtraInfo,\n";
    else
      OS << "  nullptr // No extra processor descriptor\n";
    OS << "};\n";
  }
}

//
// EmitSchedModel - Emits all scheduling model tables, folding common patterns.
//
void SubtargetEmitter::EmitSchedModel(raw_ostream &OS) {
  OS << "#ifdef DBGFIELD\n"
     << "#error \"<target>GenSubtargetInfo.inc requires a DBGFIELD macro\"\n"
     << "#endif\n"
     << "#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)\n"
     << "#define DBGFIELD(x) x,\n"
     << "#else\n"
     << "#define DBGFIELD(x)\n"
     << "#endif\n";

  if (SchedModels.hasItineraries()) {
    std::vector<std::vector<InstrItinerary>> ProcItinLists;
    // Emit the stage data
    EmitStageAndOperandCycleData(OS, ProcItinLists);
    EmitItineraries(OS, ProcItinLists);
  }
  OS << "\n// ===============================================================\n"
     << "// Data tables for the new per-operand machine model.\n";

  SchedClassTables SchedTables;
  for (const CodeGenProcModel &ProcModel : SchedModels.procModels()) {
    GenSchedClassTables(ProcModel, SchedTables);
  }
  EmitSchedClassTables(SchedTables, OS);

  OS << "\n#undef DBGFIELD\n";

  // Emit the processor machine model
  EmitProcessorModels(OS);
}

static void emitPredicateProlog(const RecordKeeper &Records, raw_ostream &OS) {
  std::string Buffer;
  raw_string_ostream Stream(Buffer);

  // Collect all the PredicateProlog records and print them to the output
  // stream.
  std::vector<Record *> Prologs =
      Records.getAllDerivedDefinitions("PredicateProlog");
  llvm::sort(Prologs, LessRecord());
  for (Record *P : Prologs)
    Stream << P->getValueAsString("Code") << '\n';

  OS << Buffer;
}

static bool isTruePredicate(const Record *Rec) {
  return Rec->isSubClassOf("MCSchedPredicate") &&
         Rec->getValueAsDef("Pred")->isSubClassOf("MCTrue");
}

static void emitPredicates(const CodeGenSchedTransition &T,
                           const CodeGenSchedClass &SC, PredicateExpander &PE,
                           raw_ostream &OS) {
  std::string Buffer;
  raw_string_ostream SS(Buffer);

  // If not all predicates are MCTrue, then we need an if-stmt.
  unsigned NumNonTruePreds =
      T.PredTerm.size() - count_if(T.PredTerm, isTruePredicate);

  SS.indent(PE.getIndentLevel() * 2);

  if (NumNonTruePreds) {
    bool FirstNonTruePredicate = true;
    SS << "if (";

    PE.setIndentLevel(PE.getIndentLevel() + 2);

    for (const Record *Rec : T.PredTerm) {
      // Skip predicates that evaluate to "true".
      if (isTruePredicate(Rec))
        continue;

      if (FirstNonTruePredicate) {
        FirstNonTruePredicate = false;
      } else {
        SS << "\n";
        SS.indent(PE.getIndentLevel() * 2);
        SS << "&& ";
      }

      if (Rec->isSubClassOf("MCSchedPredicate")) {
        PE.expandPredicate(SS, Rec->getValueAsDef("Pred"));
        continue;
      }

      // Expand this legacy predicate and wrap it around braces if there is more
      // than one predicate to expand.
      SS << ((NumNonTruePreds > 1) ? "(" : "")
         << Rec->getValueAsString("Predicate")
         << ((NumNonTruePreds > 1) ? ")" : "");
    }

    SS << ")\n"; // end of if-stmt
    PE.decreaseIndentLevel();
    SS.indent(PE.getIndentLevel() * 2);
    PE.decreaseIndentLevel();
  }

  SS << "return " << T.ToClassIdx << "; // " << SC.Name << '\n';
  OS << Buffer;
}

// Used by method `SubtargetEmitter::emitSchedModelHelpersImpl()` to generate
// epilogue code for the auto-generated helper.
static void emitSchedModelHelperEpilogue(raw_ostream &OS,
                                         bool ShouldReturnZero) {
  if (ShouldReturnZero) {
    OS << "  // Don't know how to resolve this scheduling class.\n"
       << "  return 0;\n";
    return;
  }

  OS << "  report_fatal_error(\"Expected a variant SchedClass\");\n";
}

static bool hasMCSchedPredicates(const CodeGenSchedTransition &T) {
  return all_of(T.PredTerm, [](const Record *Rec) {
    return Rec->isSubClassOf("MCSchedPredicate");
  });
}

static void collectVariantClasses(const CodeGenSchedModels &SchedModels,
                                  IdxVec &VariantClasses,
                                  bool OnlyExpandMCInstPredicates) {
  for (const CodeGenSchedClass &SC : SchedModels.schedClasses()) {
    // Ignore non-variant scheduling classes.
    if (SC.Transitions.empty())
      continue;

    if (OnlyExpandMCInstPredicates) {
      // Ignore this variant scheduling class no transitions use any meaningful
      // MCSchedPredicate definitions.
      if (llvm::none_of(SC.Transitions, hasMCSchedPredicates))
        continue;
    }

    VariantClasses.push_back(SC.Index);
  }
}

static void collectProcessorIndices(const CodeGenSchedClass &SC,
                                    IdxVec &ProcIndices) {
  // A variant scheduling class may define transitions for multiple
  // processors.  This function identifies wich processors are associated with
  // transition rules specified by variant class `SC`.
  for (const CodeGenSchedTransition &T : SC.Transitions) {
    IdxVec PI;
    std::set_union(&T.ProcIndex, &T.ProcIndex + 1, ProcIndices.begin(),
                   ProcIndices.end(), std::back_inserter(PI));
    ProcIndices = std::move(PI);
  }
}

static bool isAlwaysTrue(const CodeGenSchedTransition &T) {
  return llvm::all_of(T.PredTerm, isTruePredicate);
}

void SubtargetEmitter::emitSchedModelHelpersImpl(
    raw_ostream &OS, bool OnlyExpandMCInstPredicates) {
  IdxVec VariantClasses;
  collectVariantClasses(SchedModels, VariantClasses,
                        OnlyExpandMCInstPredicates);

  if (VariantClasses.empty()) {
    emitSchedModelHelperEpilogue(OS, OnlyExpandMCInstPredicates);
    return;
  }

  // Construct a switch statement where the condition is a check on the
  // scheduling class identifier. There is a `case` for every variant class
  // defined by the processor models of this target.
  // Each `case` implements a number of rules to resolve (i.e. to transition
  // from) a variant scheduling class to another scheduling class.  Rules are
  // described by instances of CodeGenSchedTransition. Note that transitions may
  // not be valid for all processors.
  OS << "  switch (SchedClass) {\n";
  for (unsigned VC : VariantClasses) {
    IdxVec ProcIndices;
    const CodeGenSchedClass &SC = SchedModels.getSchedClass(VC);
    collectProcessorIndices(SC, ProcIndices);

    OS << "  case " << VC << ": // " << SC.Name << '\n';

    PredicateExpander PE(Target);
    PE.setByRef(false);
    PE.setExpandForMC(OnlyExpandMCInstPredicates);
    for (unsigned PI : ProcIndices) {
      OS << "    ";

      // Emit a guard on the processor ID.
      if (PI != 0) {
        OS << (OnlyExpandMCInstPredicates
                   ? "if (CPUID == "
                   : "if (SchedModel->getProcessorID() == ");
        OS << PI << ") ";
        OS << "{ // " << (SchedModels.procModelBegin() + PI)->ModelName << '\n';
      }

      // Now emit transitions associated with processor PI.
      const CodeGenSchedTransition *FinalT = nullptr;
      for (const CodeGenSchedTransition &T : SC.Transitions) {
        if (PI != 0 && T.ProcIndex != PI)
          continue;

        // Emit only transitions based on MCSchedPredicate, if it's the case.
        // At least the transition specified by NoSchedPred is emitted,
        // which becomes the default transition for those variants otherwise
        // not based on MCSchedPredicate.
        // FIXME: preferably, llvm-mca should instead assume a reasonable
        // default when a variant transition is not based on MCSchedPredicate
        // for a given processor.
        if (OnlyExpandMCInstPredicates && !hasMCSchedPredicates(T))
          continue;

        // If transition is folded to 'return X' it should be the last one.
        if (isAlwaysTrue(T)) {
          FinalT = &T;
          continue;
        }
        PE.setIndentLevel(3);
        emitPredicates(T, SchedModels.getSchedClass(T.ToClassIdx), PE, OS);
      }
      if (FinalT)
        emitPredicates(*FinalT, SchedModels.getSchedClass(FinalT->ToClassIdx),
                       PE, OS);

      OS << "    }\n";

      if (PI == 0)
        break;
    }

    if (SC.isInferred())
      OS << "    return " << SC.Index << ";\n";
    OS << "    break;\n";
  }

  OS << "  };\n";

  emitSchedModelHelperEpilogue(OS, OnlyExpandMCInstPredicates);
}

void SubtargetEmitter::EmitSchedModelHelpers(const std::string &ClassName,
                                             raw_ostream &OS) {
  OS << "unsigned " << ClassName
     << "\n::resolveSchedClass(unsigned SchedClass, const MachineInstr *MI,"
     << " const TargetSchedModel *SchedModel) const {\n";

  // Emit the predicate prolog code.
  emitPredicateProlog(Records, OS);

  // Emit target predicates.
  emitSchedModelHelpersImpl(OS);

  OS << "} // " << ClassName << "::resolveSchedClass\n\n";

  OS << "unsigned " << ClassName
     << "\n::resolveVariantSchedClass(unsigned SchedClass, const MCInst *MI,"
     << " const MCInstrInfo *MCII, unsigned CPUID) const {\n"
     << "  return " << Target << "_MC"
     << "::resolveVariantSchedClassImpl(SchedClass, MI, MCII, CPUID);\n"
     << "} // " << ClassName << "::resolveVariantSchedClass\n\n";

  STIPredicateExpander PE(Target);
  PE.setClassPrefix(ClassName);
  PE.setExpandDefinition(true);
  PE.setByRef(false);
  PE.setIndentLevel(0);

  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);
}

void SubtargetEmitter::EmitHwModeCheck(const std::string &ClassName,
                                       raw_ostream &OS) {
  const CodeGenHwModes &CGH = TGT.getHwModes();
  assert(CGH.getNumModeIds() > 0);
  if (CGH.getNumModeIds() == 1)
    return;

  // Collect all HwModes and related features defined in the TD files,
  // and store them as a bit set.
  unsigned ValueTypeModes = 0;
  unsigned RegInfoModes = 0;
  unsigned EncodingInfoModes = 0;
  for (const auto &MS : CGH.getHwModeSelects()) {
    for (const HwModeSelect::PairType &P : MS.second.Items) {
      if (P.first == DefaultMode)
        continue;
      if (P.second->isSubClassOf("ValueType")) {
        ValueTypeModes |= (1 << (P.first - 1));
      } else if (P.second->isSubClassOf("RegInfo") ||
                 P.second->isSubClassOf("SubRegRange")) {
        RegInfoModes |= (1 << (P.first - 1));
      } else if (P.second->isSubClassOf("InstructionEncoding")) {
        EncodingInfoModes |= (1 << (P.first - 1));
      }
    }
  }

  // Start emitting for getHwModeSet().
  OS << "unsigned " << ClassName << "::getHwModeSet() const {\n";
  OS << "  // Collect HwModes and store them as a bit set.\n";
  OS << "  unsigned Modes = 0;\n";
  for (unsigned M = 1, NumModes = CGH.getNumModeIds(); M != NumModes; ++M) {
    const HwMode &HM = CGH.getMode(M);
    OS << "  if (checkFeatures(\"" << HM.Features << "\")) Modes |= (1 << "
       << (M - 1) << ");\n";
  }
  OS << "  return Modes;\n}\n";
  // End emitting for getHwModeSet().

  auto handlePerMode = [&](std::string ModeType, unsigned ModeInBitSet) {
    OS << "  case HwMode_" << ModeType << ":\n"
       << "    Modes &= " << ModeInBitSet << ";\n"
       << "    if (!Modes)\n      return Modes;\n"
       << "    if (!llvm::has_single_bit<unsigned>(Modes))\n"
       << "      llvm_unreachable(\"Two or more HwModes for " << ModeType
       << " were found!\");\n"
       << "    return llvm::countr_zero(Modes) + 1;\n";
  };

  // Start emitting for getHwMode().
  OS << "unsigned " << ClassName
     << "::getHwMode(enum HwModeType type) const {\n";
  OS << "  unsigned Modes = getHwModeSet();\n\n";
  OS << "  if (!Modes)\n    return Modes;\n\n";
  OS << "  switch (type) {\n";
  OS << "  case HwMode_Default:\n    return llvm::countr_zero(Modes) + 1;\n";
  handlePerMode("ValueType", ValueTypeModes);
  handlePerMode("RegInfo", RegInfoModes);
  handlePerMode("EncodingInfo", EncodingInfoModes);
  OS << "  }\n";
  OS << "  llvm_unreachable(\"unexpected HwModeType\");\n"
     << "  return 0; // should not get here\n}\n";
  // End emitting for getHwMode().
}

void SubtargetEmitter::emitGetMacroFusions(const std::string &ClassName,
                                           raw_ostream &OS) {
  if (!TGT.hasMacroFusion())
    return;

  OS << "std::vector<MacroFusionPredTy> " << ClassName
     << "::getMacroFusions() const {\n";
  OS.indent(2) << "std::vector<MacroFusionPredTy> Fusions;\n";
  for (auto *Fusion : TGT.getMacroFusions()) {
    std::string Name = Fusion->getNameInitAsString();
    OS.indent(2) << "if (hasFeature(" << Target << "::" << Name
                 << ")) Fusions.push_back(llvm::is" << Name << ");\n";
  }

  OS.indent(2) << "return Fusions;\n";
  OS << "}\n";
}

// Produces a subtarget specific function for parsing
// the subtarget features string.
void SubtargetEmitter::ParseFeaturesFunction(raw_ostream &OS) {
  std::vector<Record *> Features =
      Records.getAllDerivedDefinitions("SubtargetFeature");
  llvm::sort(Features, LessRecord());

  OS << "// ParseSubtargetFeatures - Parses features string setting specified\n"
     << "// subtarget options.\n"
     << "void llvm::";
  OS << Target;
  OS << "Subtarget::ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, "
     << "StringRef FS) {\n"
     << "  LLVM_DEBUG(dbgs() << \"\\nFeatures:\" << FS);\n"
     << "  LLVM_DEBUG(dbgs() << \"\\nCPU:\" << CPU);\n"
     << "  LLVM_DEBUG(dbgs() << \"\\nTuneCPU:\" << TuneCPU << \"\\n\\n\");\n";

  if (Features.empty()) {
    OS << "}\n";
    return;
  }

  if (Target == "AArch64")
    OS << "  CPU = AArch64::resolveCPUAlias(CPU);\n"
       << "  TuneCPU = AArch64::resolveCPUAlias(TuneCPU);\n";

  OS << "  InitMCProcessorInfo(CPU, TuneCPU, FS);\n"
     << "  const FeatureBitset &Bits = getFeatureBits();\n";

  for (Record *R : Features) {
    // Next record
    StringRef Instance = R->getName();
    StringRef Value = R->getValueAsString("Value");
    StringRef FieldName = R->getValueAsString("FieldName");

    if (Value == "true" || Value == "false")
      OS << "  if (Bits[" << Target << "::" << Instance << "]) " << FieldName
         << " = " << Value << ";\n";
    else
      OS << "  if (Bits[" << Target << "::" << Instance << "] && " << FieldName
         << " < " << Value << ") " << FieldName << " = " << Value << ";\n";
  }

  OS << "}\n";
}

void SubtargetEmitter::emitGenMCSubtargetInfo(raw_ostream &OS) {
  OS << "namespace " << Target << "_MC {\n"
     << "unsigned resolveVariantSchedClassImpl(unsigned SchedClass,\n"
     << "    const MCInst *MI, const MCInstrInfo *MCII, unsigned CPUID) {\n";
  emitSchedModelHelpersImpl(OS, /* OnlyExpandMCPredicates */ true);
  OS << "}\n";
  OS << "} // end namespace " << Target << "_MC\n\n";

  OS << "struct " << Target
     << "GenMCSubtargetInfo : public MCSubtargetInfo {\n";
  OS << "  " << Target << "GenMCSubtargetInfo(const Triple &TT,\n"
     << "    StringRef CPU, StringRef TuneCPU, StringRef FS,\n"
     << "    ArrayRef<SubtargetFeatureKV> PF,\n"
     << "    ArrayRef<SubtargetSubTypeKV> PD,\n"
     << "    const MCWriteProcResEntry *WPR,\n"
     << "    const MCWriteLatencyEntry *WL,\n"
     << "    const MCReadAdvanceEntry *RA, const InstrStage *IS,\n"
     << "    const unsigned *OC, const unsigned *FP) :\n"
     << "      MCSubtargetInfo(TT, CPU, TuneCPU, FS, PF, PD,\n"
     << "                      WPR, WL, RA, IS, OC, FP) { }\n\n"
     << "  unsigned resolveVariantSchedClass(unsigned SchedClass,\n"
     << "      const MCInst *MI, const MCInstrInfo *MCII,\n"
     << "      unsigned CPUID) const override {\n"
     << "    return " << Target << "_MC"
     << "::resolveVariantSchedClassImpl(SchedClass, MI, MCII, CPUID);\n";
  OS << "  }\n";
  if (TGT.getHwModes().getNumModeIds() > 1) {
    OS << "  unsigned getHwModeSet() const override;\n";
    OS << "  unsigned getHwMode(enum HwModeType type = HwMode_Default) const "
          "override;\n";
  }
  if (Target == "AArch64")
    OS << "  bool isCPUStringValid(StringRef CPU) const override {\n"
       << "    CPU = AArch64::resolveCPUAlias(CPU);\n"
       << "    return MCSubtargetInfo::isCPUStringValid(CPU);\n"
       << "  }\n";
  OS << "};\n";
  EmitHwModeCheck(Target + "GenMCSubtargetInfo", OS);
}

void SubtargetEmitter::EmitMCInstrAnalysisPredicateFunctions(raw_ostream &OS) {
  OS << "\n#ifdef GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS\n";
  OS << "#undef GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS\n\n";

  STIPredicateExpander PE(Target);
  PE.setExpandForMC(true);
  PE.setByRef(true);
  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);

  OS << "#endif // GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS\n\n";

  OS << "\n#ifdef GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS\n";
  OS << "#undef GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS\n\n";

  std::string ClassPrefix = Target + "MCInstrAnalysis";
  PE.setExpandDefinition(true);
  PE.setClassPrefix(ClassPrefix);
  PE.setIndentLevel(0);
  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);

  OS << "#endif // GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS\n\n";
}

//
// SubtargetEmitter::run - Main subtarget enumeration emitter.
//
void SubtargetEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("Subtarget Enumeration Source Fragment", OS);

  OS << "\n#ifdef GET_SUBTARGETINFO_ENUM\n";
  OS << "#undef GET_SUBTARGETINFO_ENUM\n\n";

  DenseMap<Record *, unsigned> FeatureMap;

  OS << "namespace llvm {\n";
  Enumeration(OS, FeatureMap);
  OS << "} // end namespace llvm\n\n";
  OS << "#endif // GET_SUBTARGETINFO_ENUM\n\n";

  EmitSubtargetInfoMacroCalls(OS);

  OS << "namespace llvm {\n";
#if 0
  OS << "namespace {\n";
#endif
  unsigned NumFeatures = FeatureKeyValues(OS, FeatureMap);
  OS << "\n";
  EmitSchedModel(OS);
  OS << "\n";
  unsigned NumProcs = CPUKeyValues(OS, FeatureMap);
  OS << "\n";
#if 0
  OS << "} // end anonymous namespace\n\n";
#endif

  // MCInstrInfo initialization routine.
  emitGenMCSubtargetInfo(OS);

  OS << "\nstatic inline MCSubtargetInfo *create" << Target
     << "MCSubtargetInfoImpl("
     << "const Triple &TT, StringRef CPU, StringRef TuneCPU, StringRef FS) {\n";
  if (Target == "AArch64")
    OS << "  CPU = AArch64::resolveCPUAlias(CPU);\n"
       << "  TuneCPU = AArch64::resolveCPUAlias(TuneCPU);\n";
  OS << "  return new " << Target
     << "GenMCSubtargetInfo(TT, CPU, TuneCPU, FS, ";
  if (NumFeatures)
    OS << Target << "FeatureKV, ";
  else
    OS << "std::nullopt, ";
  if (NumProcs)
    OS << Target << "SubTypeKV, ";
  else
    OS << "std::nullopt, ";
  OS << '\n';
  OS.indent(22);
  OS << Target << "WriteProcResTable, " << Target << "WriteLatencyTable, "
     << Target << "ReadAdvanceTable, ";
  OS << '\n';
  OS.indent(22);
  if (SchedModels.hasItineraries()) {
    OS << Target << "Stages, " << Target << "OperandCycles, " << Target
       << "ForwardingPaths";
  } else
    OS << "nullptr, nullptr, nullptr";
  OS << ");\n}\n\n";

  OS << "} // end namespace llvm\n\n";

  OS << "#endif // GET_SUBTARGETINFO_MC_DESC\n\n";

  OS << "\n#ifdef GET_SUBTARGETINFO_TARGET_DESC\n";
  OS << "#undef GET_SUBTARGETINFO_TARGET_DESC\n\n";

  OS << "#include \"llvm/Support/Debug.h\"\n";
  OS << "#include \"llvm/Support/raw_ostream.h\"\n\n";
  if (Target == "AArch64")
    OS << "#include \"llvm/TargetParser/AArch64TargetParser.h\"\n\n";
  ParseFeaturesFunction(OS);

  OS << "#endif // GET_SUBTARGETINFO_TARGET_DESC\n\n";

  // Create a TargetSubtargetInfo subclass to hide the MC layer initialization.
  OS << "\n#ifdef GET_SUBTARGETINFO_HEADER\n";
  OS << "#undef GET_SUBTARGETINFO_HEADER\n\n";

  std::string ClassName = Target + "GenSubtargetInfo";
  OS << "namespace llvm {\n";
  OS << "class DFAPacketizer;\n";
  OS << "namespace " << Target << "_MC {\n"
     << "unsigned resolveVariantSchedClassImpl(unsigned SchedClass,"
     << " const MCInst *MI, const MCInstrInfo *MCII, unsigned CPUID);\n"
     << "} // end namespace " << Target << "_MC\n\n";
  OS << "struct " << ClassName << " : public TargetSubtargetInfo {\n"
     << "  explicit " << ClassName << "(const Triple &TT, StringRef CPU, "
     << "StringRef TuneCPU, StringRef FS);\n"
     << "public:\n"
     << "  unsigned resolveSchedClass(unsigned SchedClass, "
     << " const MachineInstr *DefMI,"
     << " const TargetSchedModel *SchedModel) const override;\n"
     << "  unsigned resolveVariantSchedClass(unsigned SchedClass,"
     << " const MCInst *MI, const MCInstrInfo *MCII,"
     << " unsigned CPUID) const override;\n"
     << "  DFAPacketizer *createDFAPacketizer(const InstrItineraryData *IID)"
     << " const;\n";
  if (TGT.getHwModes().getNumModeIds() > 1) {
    OS << "  unsigned getHwModeSet() const override;\n";
    OS << "  unsigned getHwMode(enum HwModeType type = HwMode_Default) const "
          "override;\n";
  }
  if (TGT.hasMacroFusion())
    OS << "  std::vector<MacroFusionPredTy> getMacroFusions() const "
          "override;\n";

  STIPredicateExpander PE(Target);
  PE.setByRef(false);
  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);

  OS << "};\n"
     << "} // end namespace llvm\n\n";

  OS << "#endif // GET_SUBTARGETINFO_HEADER\n\n";

  OS << "\n#ifdef GET_SUBTARGETINFO_CTOR\n";
  OS << "#undef GET_SUBTARGETINFO_CTOR\n\n";

  OS << "#include \"llvm/CodeGen/TargetSchedule.h\"\n\n";
  OS << "namespace llvm {\n";
  OS << "extern const llvm::SubtargetFeatureKV " << Target << "FeatureKV[];\n";
  OS << "extern const llvm::SubtargetSubTypeKV " << Target << "SubTypeKV[];\n";
  OS << "extern const llvm::MCWriteProcResEntry " << Target
     << "WriteProcResTable[];\n";
  OS << "extern const llvm::MCWriteLatencyEntry " << Target
     << "WriteLatencyTable[];\n";
  OS << "extern const llvm::MCReadAdvanceEntry " << Target
     << "ReadAdvanceTable[];\n";

  if (SchedModels.hasItineraries()) {
    OS << "extern const llvm::InstrStage " << Target << "Stages[];\n";
    OS << "extern const unsigned " << Target << "OperandCycles[];\n";
    OS << "extern const unsigned " << Target << "ForwardingPaths[];\n";
  }

  OS << ClassName << "::" << ClassName << "(const Triple &TT, StringRef CPU, "
     << "StringRef TuneCPU, StringRef FS)\n";

  if (Target == "AArch64")
    OS << "  : TargetSubtargetInfo(TT, AArch64::resolveCPUAlias(CPU),\n"
       << "                        AArch64::resolveCPUAlias(TuneCPU), FS, ";
  else
    OS << "  : TargetSubtargetInfo(TT, CPU, TuneCPU, FS, ";
  if (NumFeatures)
    OS << "ArrayRef(" << Target << "FeatureKV, " << NumFeatures << "), ";
  else
    OS << "std::nullopt, ";
  if (NumProcs)
    OS << "ArrayRef(" << Target << "SubTypeKV, " << NumProcs << "), ";
  else
    OS << "std::nullopt, ";
  OS << '\n';
  OS.indent(24);
  OS << Target << "WriteProcResTable, " << Target << "WriteLatencyTable, "
     << Target << "ReadAdvanceTable, ";
  OS << '\n';
  OS.indent(24);
  if (SchedModels.hasItineraries()) {
    OS << Target << "Stages, " << Target << "OperandCycles, " << Target
       << "ForwardingPaths";
  } else
    OS << "nullptr, nullptr, nullptr";
  OS << ") {}\n\n";

  EmitSchedModelHelpers(ClassName, OS);
  EmitHwModeCheck(ClassName, OS);
  emitGetMacroFusions(ClassName, OS);

  OS << "} // end namespace llvm\n\n";

  OS << "#endif // GET_SUBTARGETINFO_CTOR\n\n";

  EmitMCInstrAnalysisPredicateFunctions(OS);
}

static TableGen::Emitter::OptClass<SubtargetEmitter>
    X("gen-subtarget", "Generate subtarget enumerations");
