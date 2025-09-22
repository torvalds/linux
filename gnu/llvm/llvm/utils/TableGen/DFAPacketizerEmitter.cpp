//===- DFAPacketizerEmitter.cpp - Packetization DFA for a VLIW machine ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class parses the Schedule.td file and produces an API that can be used
// to reason about whether an instruction can be added to a packet on a VLIW
// architecture. The class internally generates a deterministic finite
// automaton (DFA) that models all possible mappings of machine instructions
// to functional units as instructions are added to a packet.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenSchedule.h"
#include "Common/CodeGenTarget.h"
#include "DFAEmitter.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cassert>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#define DEBUG_TYPE "dfa-emitter"

using namespace llvm;

// We use a uint64_t to represent a resource bitmask.
#define DFA_MAX_RESOURCES 64

namespace {
using ResourceVector = SmallVector<uint64_t, 4>;

struct ScheduleClass {
  /// The parent itinerary index (processor model ID).
  unsigned ItineraryID;

  /// Index within this itinerary of the schedule class.
  unsigned Idx;

  /// The index within the uniqued set of required resources of Resources.
  unsigned ResourcesIdx;

  /// Conjunctive list of resource requirements:
  ///   {a|b, b|c} => (a OR b) AND (b or c).
  /// Resources are unique across all itineraries.
  ResourceVector Resources;
};

// Generates and prints out the DFA for resource tracking.
class DFAPacketizerEmitter {
private:
  std::string TargetName;
  RecordKeeper &Records;

  UniqueVector<ResourceVector> UniqueResources;
  std::vector<ScheduleClass> ScheduleClasses;
  std::map<std::string, uint64_t> FUNameToBitsMap;
  std::map<unsigned, uint64_t> ComboBitToBitsMap;

public:
  DFAPacketizerEmitter(RecordKeeper &R);

  // Construct a map of function unit names to bits.
  int collectAllFuncUnits(ArrayRef<const CodeGenProcModel *> ProcModels);

  // Construct a map from a combo function unit bit to the bits of all included
  // functional units.
  int collectAllComboFuncs(ArrayRef<Record *> ComboFuncList);

  ResourceVector getResourcesForItinerary(Record *Itinerary);
  void createScheduleClasses(unsigned ItineraryIdx, const RecVec &Itineraries);

  // Emit code for a subset of itineraries.
  void emitForItineraries(raw_ostream &OS,
                          std::vector<const CodeGenProcModel *> &ProcItinList,
                          std::string DFAName);

  void run(raw_ostream &OS);
};
} // end anonymous namespace

DFAPacketizerEmitter::DFAPacketizerEmitter(RecordKeeper &R)
    : TargetName(std::string(CodeGenTarget(R).getName())), Records(R) {}

int DFAPacketizerEmitter::collectAllFuncUnits(
    ArrayRef<const CodeGenProcModel *> ProcModels) {
  LLVM_DEBUG(dbgs() << "-------------------------------------------------------"
                       "----------------------\n");
  LLVM_DEBUG(dbgs() << "collectAllFuncUnits");
  LLVM_DEBUG(dbgs() << " (" << ProcModels.size() << " itineraries)\n");

  std::set<Record *> ProcItinList;
  for (const CodeGenProcModel *Model : ProcModels)
    ProcItinList.insert(Model->ItinsDef);

  int totalFUs = 0;
  // Parse functional units for all the itineraries.
  for (Record *Proc : ProcItinList) {
    std::vector<Record *> FUs = Proc->getValueAsListOfDefs("FU");

    LLVM_DEBUG(dbgs() << "    FU:"
                      << " (" << FUs.size() << " FUs) " << Proc->getName());

    // Convert macros to bits for each stage.
    unsigned numFUs = FUs.size();
    for (unsigned j = 0; j < numFUs; ++j) {
      assert((j < DFA_MAX_RESOURCES) &&
             "Exceeded maximum number of representable resources");
      uint64_t FuncResources = 1ULL << j;
      FUNameToBitsMap[std::string(FUs[j]->getName())] = FuncResources;
      LLVM_DEBUG(dbgs() << " " << FUs[j]->getName() << ":0x"
                        << Twine::utohexstr(FuncResources));
    }
    totalFUs += numFUs;
    LLVM_DEBUG(dbgs() << "\n");
  }
  return totalFUs;
}

int DFAPacketizerEmitter::collectAllComboFuncs(
    ArrayRef<Record *> ComboFuncList) {
  LLVM_DEBUG(dbgs() << "-------------------------------------------------------"
                       "----------------------\n");
  LLVM_DEBUG(dbgs() << "collectAllComboFuncs");
  LLVM_DEBUG(dbgs() << " (" << ComboFuncList.size() << " sets)\n");

  int numCombos = 0;
  for (unsigned i = 0, N = ComboFuncList.size(); i < N; ++i) {
    Record *Func = ComboFuncList[i];
    std::vector<Record *> FUs = Func->getValueAsListOfDefs("CFD");

    LLVM_DEBUG(dbgs() << "    CFD:" << i << " (" << FUs.size() << " combo FUs) "
                      << Func->getName() << "\n");

    // Convert macros to bits for each stage.
    for (unsigned j = 0, N = FUs.size(); j < N; ++j) {
      assert((j < DFA_MAX_RESOURCES) &&
             "Exceeded maximum number of DFA resources");
      Record *FuncData = FUs[j];
      Record *ComboFunc = FuncData->getValueAsDef("TheComboFunc");
      const std::vector<Record *> &FuncList =
          FuncData->getValueAsListOfDefs("FuncList");
      const std::string &ComboFuncName = std::string(ComboFunc->getName());
      uint64_t ComboBit = FUNameToBitsMap[ComboFuncName];
      uint64_t ComboResources = ComboBit;
      LLVM_DEBUG(dbgs() << "      combo: " << ComboFuncName << ":0x"
                        << Twine::utohexstr(ComboResources) << "\n");
      for (auto *K : FuncList) {
        std::string FuncName = std::string(K->getName());
        uint64_t FuncResources = FUNameToBitsMap[FuncName];
        LLVM_DEBUG(dbgs() << "        " << FuncName << ":0x"
                          << Twine::utohexstr(FuncResources) << "\n");
        ComboResources |= FuncResources;
      }
      ComboBitToBitsMap[ComboBit] = ComboResources;
      numCombos++;
      LLVM_DEBUG(dbgs() << "          => combo bits: " << ComboFuncName << ":0x"
                        << Twine::utohexstr(ComboBit) << " = 0x"
                        << Twine::utohexstr(ComboResources) << "\n");
    }
  }
  return numCombos;
}

ResourceVector
DFAPacketizerEmitter::getResourcesForItinerary(Record *Itinerary) {
  ResourceVector Resources;
  assert(Itinerary);
  for (Record *StageDef : Itinerary->getValueAsListOfDefs("Stages")) {
    uint64_t StageResources = 0;
    for (Record *Unit : StageDef->getValueAsListOfDefs("Units")) {
      StageResources |= FUNameToBitsMap[std::string(Unit->getName())];
    }
    if (StageResources != 0)
      Resources.push_back(StageResources);
  }
  return Resources;
}

void DFAPacketizerEmitter::createScheduleClasses(unsigned ItineraryIdx,
                                                 const RecVec &Itineraries) {
  unsigned Idx = 0;
  for (Record *Itinerary : Itineraries) {
    if (!Itinerary) {
      ScheduleClasses.push_back({ItineraryIdx, Idx++, 0, ResourceVector{}});
      continue;
    }
    ResourceVector Resources = getResourcesForItinerary(Itinerary);
    ScheduleClasses.push_back(
        {ItineraryIdx, Idx++, UniqueResources.insert(Resources), Resources});
  }
}

//
// Run the worklist algorithm to generate the DFA.
//
void DFAPacketizerEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("Target DFA Packetizer Tables", OS);
  OS << "\n"
     << "#include \"llvm/CodeGen/DFAPacketizer.h\"\n";
  OS << "namespace llvm {\n";

  CodeGenTarget CGT(Records);
  CodeGenSchedModels CGS(Records, CGT);

  std::unordered_map<std::string, std::vector<const CodeGenProcModel *>>
      ItinsByNamespace;
  for (const CodeGenProcModel &ProcModel : CGS.procModels()) {
    if (ProcModel.hasItineraries()) {
      auto NS = ProcModel.ItinsDef->getValueAsString("PacketizerNamespace");
      ItinsByNamespace[std::string(NS)].push_back(&ProcModel);
    }
  }

  for (auto &KV : ItinsByNamespace)
    emitForItineraries(OS, KV.second, KV.first);
  OS << "} // end namespace llvm\n";
}

void DFAPacketizerEmitter::emitForItineraries(
    raw_ostream &OS, std::vector<const CodeGenProcModel *> &ProcModels,
    std::string DFAName) {
  OS << "} // end namespace llvm\n\n";
  OS << "namespace {\n";
  collectAllFuncUnits(ProcModels);
  collectAllComboFuncs(Records.getAllDerivedDefinitions("ComboFuncUnits"));

  // Collect the itineraries.
  DenseMap<const CodeGenProcModel *, unsigned> ProcModelStartIdx;
  for (const CodeGenProcModel *Model : ProcModels) {
    assert(Model->hasItineraries());
    ProcModelStartIdx[Model] = ScheduleClasses.size();
    createScheduleClasses(Model->Index, Model->ItinDefList);
  }

  // Output the mapping from ScheduleClass to ResourcesIdx.
  unsigned Idx = 0;
  OS << "constexpr unsigned " << TargetName << DFAName
     << "ResourceIndices[] = {";
  for (const ScheduleClass &SC : ScheduleClasses) {
    if (Idx++ % 32 == 0)
      OS << "\n  ";
    OS << SC.ResourcesIdx << ", ";
  }
  OS << "\n};\n\n";

  // And the mapping from Itinerary index into the previous table.
  OS << "constexpr unsigned " << TargetName << DFAName
     << "ProcResourceIndexStart[] = {\n";
  OS << "  0, // NoSchedModel\n";
  for (const CodeGenProcModel *Model : ProcModels) {
    OS << "  " << ProcModelStartIdx[Model] << ", // " << Model->ModelName
       << "\n";
  }
  OS << "  " << ScheduleClasses.size() << "\n};\n\n";

  // The type of a state in the nondeterministic automaton we're defining.
  using NfaStateTy = uint64_t;

  // Given a resource state, return all resource states by applying
  // InsnClass.
  auto applyInsnClass = [&](const ResourceVector &InsnClass,
                            NfaStateTy State) -> std::deque<NfaStateTy> {
    std::deque<NfaStateTy> V(1, State);
    // Apply every stage in the class individually.
    for (NfaStateTy Stage : InsnClass) {
      // Apply this stage to every existing member of V in turn.
      size_t Sz = V.size();
      for (unsigned I = 0; I < Sz; ++I) {
        NfaStateTy S = V.front();
        V.pop_front();

        // For this stage, state combination, try all possible resources.
        for (unsigned J = 0; J < DFA_MAX_RESOURCES; ++J) {
          NfaStateTy ResourceMask = 1ULL << J;
          if ((ResourceMask & Stage) == 0)
            // This resource isn't required by this stage.
            continue;
          NfaStateTy Combo = ComboBitToBitsMap[ResourceMask];
          if (Combo && ((~S & Combo) != Combo))
            // This combo units bits are not available.
            continue;
          NfaStateTy ResultingResourceState = S | ResourceMask | Combo;
          if (ResultingResourceState == S)
            continue;
          V.push_back(ResultingResourceState);
        }
      }
    }
    return V;
  };

  // Given a resource state, return a quick (conservative) guess as to whether
  // InsnClass can be applied. This is a filter for the more heavyweight
  // applyInsnClass.
  auto canApplyInsnClass = [](const ResourceVector &InsnClass,
                              NfaStateTy State) -> bool {
    for (NfaStateTy Resources : InsnClass) {
      if ((State | Resources) == State)
        return false;
    }
    return true;
  };

  DfaEmitter Emitter;
  std::deque<NfaStateTy> Worklist(1, 0);
  std::set<NfaStateTy> SeenStates;
  SeenStates.insert(Worklist.front());
  while (!Worklist.empty()) {
    NfaStateTy State = Worklist.front();
    Worklist.pop_front();
    for (const ResourceVector &Resources : UniqueResources) {
      if (!canApplyInsnClass(Resources, State))
        continue;
      unsigned ResourcesID = UniqueResources.idFor(Resources);
      for (uint64_t NewState : applyInsnClass(Resources, State)) {
        if (SeenStates.emplace(NewState).second)
          Worklist.emplace_back(NewState);
        Emitter.addTransition(State, NewState, ResourcesID);
      }
    }
  }

  std::string TargetAndDFAName = TargetName + DFAName;
  Emitter.emit(TargetAndDFAName, OS);
  OS << "} // end anonymous namespace\n\n";

  std::string SubTargetClassName = TargetName + "GenSubtargetInfo";
  OS << "namespace llvm {\n";
  OS << "DFAPacketizer *" << SubTargetClassName << "::"
     << "create" << DFAName
     << "DFAPacketizer(const InstrItineraryData *IID) const {\n"
     << "  static Automaton<uint64_t> A(ArrayRef<" << TargetAndDFAName
     << "Transition>(" << TargetAndDFAName << "Transitions), "
     << TargetAndDFAName << "TransitionInfo);\n"
     << "  unsigned ProcResIdxStart = " << TargetAndDFAName
     << "ProcResourceIndexStart[IID->SchedModel.ProcID];\n"
     << "  unsigned ProcResIdxNum = " << TargetAndDFAName
     << "ProcResourceIndexStart[IID->SchedModel.ProcID + 1] - "
        "ProcResIdxStart;\n"
     << "  return new DFAPacketizer(IID, A, {&" << TargetAndDFAName
     << "ResourceIndices[ProcResIdxStart], ProcResIdxNum});\n"
     << "\n}\n\n";
}

static TableGen::Emitter::OptClass<DFAPacketizerEmitter>
    X("gen-dfa-packetizer", "Generate DFA Packetizer for VLIW targets");
