//===- CodeGenSchedule.h - Scheduling Machine Models ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines structures to encapsulate the machine model as described in
// the target description.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENSCHEDULE_H
#define LLVM_UTILS_TABLEGEN_CODEGENSCHEDULE_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/SetTheory.h"
#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class CodeGenTarget;
class CodeGenSchedModels;
class CodeGenInstruction;

using RecVec = std::vector<Record *>;
using RecIter = std::vector<Record *>::const_iterator;

using IdxVec = std::vector<unsigned>;
using IdxIter = std::vector<unsigned>::const_iterator;

/// We have two kinds of SchedReadWrites. Explicitly defined and inferred
/// sequences.  TheDef is nonnull for explicit SchedWrites, but Sequence may or
/// may not be empty. TheDef is null for inferred sequences, and Sequence must
/// be nonempty.
///
/// IsVariadic controls whether the variants are expanded into multiple operands
/// or a sequence of writes on one operand.
struct CodeGenSchedRW {
  unsigned Index;
  std::string Name;
  Record *TheDef;
  bool IsRead;
  bool IsAlias;
  bool HasVariants;
  bool IsVariadic;
  bool IsSequence;
  IdxVec Sequence;
  RecVec Aliases;

  CodeGenSchedRW()
      : Index(0), TheDef(nullptr), IsRead(false), IsAlias(false),
        HasVariants(false), IsVariadic(false), IsSequence(false) {}
  CodeGenSchedRW(unsigned Idx, Record *Def)
      : Index(Idx), TheDef(Def), IsAlias(false), IsVariadic(false) {
    Name = std::string(Def->getName());
    IsRead = Def->isSubClassOf("SchedRead");
    HasVariants = Def->isSubClassOf("SchedVariant");
    if (HasVariants)
      IsVariadic = Def->getValueAsBit("Variadic");

    // Read records don't currently have sequences, but it can be easily
    // added. Note that implicit Reads (from ReadVariant) may have a Sequence
    // (but no record).
    IsSequence = Def->isSubClassOf("WriteSequence");
  }

  CodeGenSchedRW(unsigned Idx, bool Read, ArrayRef<unsigned> Seq,
                 const std::string &Name)
      : Index(Idx), Name(Name), TheDef(nullptr), IsRead(Read), IsAlias(false),
        HasVariants(false), IsVariadic(false), IsSequence(true), Sequence(Seq) {
    assert(Sequence.size() > 1 && "implied sequence needs >1 RWs");
  }

  bool isValid() const {
    assert((!HasVariants || TheDef) && "Variant write needs record def");
    assert((!IsVariadic || HasVariants) && "Variadic write needs variants");
    assert((!IsSequence || !HasVariants) && "Sequence can't have variant");
    assert((!IsSequence || !Sequence.empty()) && "Sequence should be nonempty");
    assert((!IsAlias || Aliases.empty()) && "Alias cannot have aliases");
    return TheDef || !Sequence.empty();
  }

#ifndef NDEBUG
  void dump() const;
#endif
};

/// Represent a transition between SchedClasses induced by SchedVariant.
struct CodeGenSchedTransition {
  unsigned ToClassIdx;
  unsigned ProcIndex;
  RecVec PredTerm;
};

/// Scheduling class.
///
/// Each instruction description will be mapped to a scheduling class. There are
/// four types of classes:
///
/// 1) An explicitly defined itinerary class with ItinClassDef set.
/// Writes and ReadDefs are empty. ProcIndices contains 0 for any processor.
///
/// 2) An implied class with a list of SchedWrites and SchedReads that are
/// defined in an instruction definition and which are common across all
/// subtargets. ProcIndices contains 0 for any processor.
///
/// 3) An implied class with a list of InstRW records that map instructions to
/// SchedWrites and SchedReads per-processor. InstrClassMap should map the same
/// instructions to this class. ProcIndices contains all the processors that
/// provided InstrRW records for this class. ItinClassDef or Writes/Reads may
/// still be defined for processors with no InstRW entry.
///
/// 4) An inferred class represents a variant of another class that may be
/// resolved at runtime. ProcIndices contains the set of processors that may
/// require the class. ProcIndices are propagated through SchedClasses as
/// variants are expanded. Multiple SchedClasses may be inferred from an
/// itinerary class. Each inherits the processor index from the ItinRW record
/// that mapped the itinerary class to the variant Writes or Reads.
struct CodeGenSchedClass {
  unsigned Index;
  std::string Name;
  Record *ItinClassDef;

  IdxVec Writes;
  IdxVec Reads;
  // Sorted list of ProcIdx, where ProcIdx==0 implies any processor.
  IdxVec ProcIndices;

  std::vector<CodeGenSchedTransition> Transitions;

  // InstRW records associated with this class. These records may refer to an
  // Instruction no longer mapped to this class by InstrClassMap. These
  // Instructions should be ignored by this class because they have been split
  // off to join another inferred class.
  RecVec InstRWs;
  // InstRWs processor indices. Filled in inferFromInstRWs
  DenseSet<unsigned> InstRWProcIndices;

  CodeGenSchedClass(unsigned Index, std::string Name, Record *ItinClassDef)
      : Index(Index), Name(std::move(Name)), ItinClassDef(ItinClassDef) {}

  bool isKeyEqual(Record *IC, ArrayRef<unsigned> W,
                  ArrayRef<unsigned> R) const {
    return ItinClassDef == IC && ArrayRef(Writes) == W && ArrayRef(Reads) == R;
  }

  // Is this class generated from a variants if existing classes? Instructions
  // are never mapped directly to inferred scheduling classes.
  bool isInferred() const { return !ItinClassDef; }

#ifndef NDEBUG
  void dump(const CodeGenSchedModels *SchedModels) const;
#endif
};

/// Represent the cost of allocating a register of register class RCDef.
///
/// The cost of allocating a register is equivalent to the number of physical
/// registers used by the register renamer. Register costs are defined at
/// register class granularity.
struct CodeGenRegisterCost {
  Record *RCDef;
  unsigned Cost;
  bool AllowMoveElimination;
  CodeGenRegisterCost(Record *RC, unsigned RegisterCost,
                      bool AllowMoveElim = false)
      : RCDef(RC), Cost(RegisterCost), AllowMoveElimination(AllowMoveElim) {}
  CodeGenRegisterCost(const CodeGenRegisterCost &) = default;
  CodeGenRegisterCost &operator=(const CodeGenRegisterCost &) = delete;
};

/// A processor register file.
///
/// This class describes a processor register file. Register file information is
/// currently consumed by external tools like llvm-mca to predict dispatch
/// stalls due to register pressure.
struct CodeGenRegisterFile {
  std::string Name;
  Record *RegisterFileDef;
  unsigned MaxMovesEliminatedPerCycle;
  bool AllowZeroMoveEliminationOnly;

  unsigned NumPhysRegs;
  std::vector<CodeGenRegisterCost> Costs;

  CodeGenRegisterFile(StringRef name, Record *def,
                      unsigned MaxMoveElimPerCy = 0,
                      bool AllowZeroMoveElimOnly = false)
      : Name(name), RegisterFileDef(def),
        MaxMovesEliminatedPerCycle(MaxMoveElimPerCy),
        AllowZeroMoveEliminationOnly(AllowZeroMoveElimOnly), NumPhysRegs(0) {}

  bool hasDefaultCosts() const { return Costs.empty(); }
};

// Processor model.
//
// ModelName is a unique name used to name an instantiation of MCSchedModel.
//
// ModelDef is NULL for inferred Models. This happens when a processor defines
// an itinerary but no machine model. If the processor defines neither a machine
// model nor itinerary, then ModelDef remains pointing to NoModel. NoModel has
// the special "NoModel" field set to true.
//
// ItinsDef always points to a valid record definition, but may point to the
// default NoItineraries. NoItineraries has an empty list of InstrItinData
// records.
//
// ItinDefList orders this processor's InstrItinData records by SchedClass idx.
struct CodeGenProcModel {
  unsigned Index;
  std::string ModelName;
  Record *ModelDef;
  Record *ItinsDef;

  // Derived members...

  // Array of InstrItinData records indexed by a CodeGenSchedClass index.
  // This list is empty if the Processor has no value for Itineraries.
  // Initialized by collectProcItins().
  RecVec ItinDefList;

  // Map itinerary classes to per-operand resources.
  // This list is empty if no ItinRW refers to this Processor.
  RecVec ItinRWDefs;

  // List of unsupported feature.
  // This list is empty if the Processor has no UnsupportedFeatures.
  RecVec UnsupportedFeaturesDefs;

  // All read/write resources associated with this processor.
  RecVec WriteResDefs;
  RecVec ReadAdvanceDefs;

  // Per-operand machine model resources associated with this processor.
  RecVec ProcResourceDefs;

  // List of Register Files.
  std::vector<CodeGenRegisterFile> RegisterFiles;

  // Optional Retire Control Unit definition.
  Record *RetireControlUnit;

  // Load/Store queue descriptors.
  Record *LoadQueue;
  Record *StoreQueue;

  CodeGenProcModel(unsigned Idx, std::string Name, Record *MDef, Record *IDef)
      : Index(Idx), ModelName(std::move(Name)), ModelDef(MDef), ItinsDef(IDef),
        RetireControlUnit(nullptr), LoadQueue(nullptr), StoreQueue(nullptr) {}

  bool hasItineraries() const {
    return !ItinsDef->getValueAsListOfDefs("IID").empty();
  }

  bool hasInstrSchedModel() const {
    return !WriteResDefs.empty() || !ItinRWDefs.empty();
  }

  bool hasExtraProcessorInfo() const {
    return RetireControlUnit || LoadQueue || StoreQueue ||
           !RegisterFiles.empty();
  }

  unsigned getProcResourceIdx(Record *PRDef) const;

  bool isUnsupported(const CodeGenInstruction &Inst) const;

  // Return true if the given write record is referenced by a ReadAdvance.
  bool hasReadOfWrite(Record *WriteDef) const;

#ifndef NDEBUG
  void dump() const;
#endif
};

/// Used to correlate instructions to MCInstPredicates specified by
/// InstructionEquivalentClass tablegen definitions.
///
/// Example: a XOR of a register with self, is a known zero-idiom for most
/// X86 processors.
///
/// Each processor can use a (potentially different) InstructionEquivalenceClass
///  definition to classify zero-idioms. That means, XORrr is likely to appear
/// in more than one equivalence class (where each class definition is
/// contributed by a different processor).
///
/// There is no guarantee that the same MCInstPredicate will be used to describe
/// equivalence classes that identify XORrr as a zero-idiom.
///
/// To be more specific, the requirements for being a zero-idiom XORrr may be
/// different for different processors.
///
/// Class PredicateInfo identifies a subset of processors that specify the same
/// requirements (i.e. same MCInstPredicate and OperandMask) for an instruction
/// opcode.
///
/// Back to the example. Field `ProcModelMask` will have one bit set for every
/// processor model that sees XORrr as a zero-idiom, and that specifies the same
/// set of constraints.
///
/// By construction, there can be multiple instances of PredicateInfo associated
/// with a same instruction opcode. For example, different processors may define
/// different constraints on the same opcode.
///
/// Field OperandMask can be used as an extra constraint.
/// It may be used to describe conditions that appy only to a subset of the
/// operands of a machine instruction, and the operands subset may not be the
/// same for all processor models.
struct PredicateInfo {
  llvm::APInt ProcModelMask; // A set of processor model indices.
  llvm::APInt OperandMask;   // An operand mask.
  const Record *Predicate;   // MCInstrPredicate definition.
  PredicateInfo(llvm::APInt CpuMask, llvm::APInt Operands, const Record *Pred)
      : ProcModelMask(CpuMask), OperandMask(Operands), Predicate(Pred) {}

  bool operator==(const PredicateInfo &Other) const {
    return ProcModelMask == Other.ProcModelMask &&
           OperandMask == Other.OperandMask && Predicate == Other.Predicate;
  }
};

/// A collection of PredicateInfo objects.
///
/// There is at least one OpcodeInfo object for every opcode specified by a
/// TIPredicate definition.
class OpcodeInfo {
  std::vector<PredicateInfo> Predicates;

  OpcodeInfo(const OpcodeInfo &Other) = delete;
  OpcodeInfo &operator=(const OpcodeInfo &Other) = delete;

public:
  OpcodeInfo() = default;
  OpcodeInfo &operator=(OpcodeInfo &&Other) = default;
  OpcodeInfo(OpcodeInfo &&Other) = default;

  ArrayRef<PredicateInfo> getPredicates() const { return Predicates; }

  void addPredicateForProcModel(const llvm::APInt &CpuMask,
                                const llvm::APInt &OperandMask,
                                const Record *Predicate);
};

/// Used to group together tablegen instruction definitions that are subject
/// to a same set of constraints (identified by an instance of OpcodeInfo).
class OpcodeGroup {
  OpcodeInfo Info;
  std::vector<const Record *> Opcodes;

  OpcodeGroup(const OpcodeGroup &Other) = delete;
  OpcodeGroup &operator=(const OpcodeGroup &Other) = delete;

public:
  OpcodeGroup(OpcodeInfo &&OpInfo) : Info(std::move(OpInfo)) {}
  OpcodeGroup(OpcodeGroup &&Other) = default;

  void addOpcode(const Record *Opcode) {
    assert(!llvm::is_contained(Opcodes, Opcode) && "Opcode already in set!");
    Opcodes.push_back(Opcode);
  }

  ArrayRef<const Record *> getOpcodes() const { return Opcodes; }
  const OpcodeInfo &getOpcodeInfo() const { return Info; }
};

/// An STIPredicateFunction descriptor used by tablegen backends to
/// auto-generate the body of a predicate function as a member of tablegen'd
/// class XXXGenSubtargetInfo.
class STIPredicateFunction {
  const Record *FunctionDeclaration;

  std::vector<const Record *> Definitions;
  std::vector<OpcodeGroup> Groups;

  STIPredicateFunction(const STIPredicateFunction &Other) = delete;
  STIPredicateFunction &operator=(const STIPredicateFunction &Other) = delete;

public:
  STIPredicateFunction(const Record *Rec) : FunctionDeclaration(Rec) {}
  STIPredicateFunction(STIPredicateFunction &&Other) = default;

  bool isCompatibleWith(const STIPredicateFunction &Other) const {
    return FunctionDeclaration == Other.FunctionDeclaration;
  }

  void addDefinition(const Record *Def) { Definitions.push_back(Def); }
  void addOpcode(const Record *OpcodeRec, OpcodeInfo &&Info) {
    if (Groups.empty() ||
        Groups.back().getOpcodeInfo().getPredicates() != Info.getPredicates())
      Groups.emplace_back(std::move(Info));
    Groups.back().addOpcode(OpcodeRec);
  }

  StringRef getName() const {
    return FunctionDeclaration->getValueAsString("Name");
  }
  const Record *getDefaultReturnPredicate() const {
    return FunctionDeclaration->getValueAsDef("DefaultReturnValue");
  }

  const Record *getDeclaration() const { return FunctionDeclaration; }
  ArrayRef<const Record *> getDefinitions() const { return Definitions; }
  ArrayRef<OpcodeGroup> getGroups() const { return Groups; }
};

using ProcModelMapTy = DenseMap<const Record *, unsigned>;

/// Top level container for machine model data.
class CodeGenSchedModels {
  RecordKeeper &Records;
  const CodeGenTarget &Target;

  // Map dag expressions to Instruction lists.
  SetTheory Sets;

  // List of unique processor models.
  std::vector<CodeGenProcModel> ProcModels;

  // Map Processor's MachineModel or ProcItin to a CodeGenProcModel index.
  ProcModelMapTy ProcModelMap;

  // Per-operand SchedReadWrite types.
  std::vector<CodeGenSchedRW> SchedWrites;
  std::vector<CodeGenSchedRW> SchedReads;

  // List of unique SchedClasses.
  std::vector<CodeGenSchedClass> SchedClasses;

  // Any inferred SchedClass has an index greater than NumInstrSchedClassses.
  unsigned NumInstrSchedClasses;

  RecVec ProcResourceDefs;
  RecVec ProcResGroups;

  // Map each instruction to its unique SchedClass index considering the
  // combination of it's itinerary class, SchedRW list, and InstRW records.
  using InstClassMapTy = DenseMap<Record *, unsigned>;
  InstClassMapTy InstrClassMap;

  std::vector<STIPredicateFunction> STIPredicates;
  std::vector<unsigned> getAllProcIndices() const;

public:
  CodeGenSchedModels(RecordKeeper &RK, const CodeGenTarget &TGT);

  // iterator access to the scheduling classes.
  using class_iterator = std::vector<CodeGenSchedClass>::iterator;
  using const_class_iterator = std::vector<CodeGenSchedClass>::const_iterator;
  class_iterator classes_begin() { return SchedClasses.begin(); }
  const_class_iterator classes_begin() const { return SchedClasses.begin(); }
  class_iterator classes_end() { return SchedClasses.end(); }
  const_class_iterator classes_end() const { return SchedClasses.end(); }
  iterator_range<class_iterator> classes() {
    return make_range(classes_begin(), classes_end());
  }
  iterator_range<const_class_iterator> classes() const {
    return make_range(classes_begin(), classes_end());
  }
  iterator_range<class_iterator> explicit_classes() {
    return make_range(classes_begin(), classes_begin() + NumInstrSchedClasses);
  }
  iterator_range<const_class_iterator> explicit_classes() const {
    return make_range(classes_begin(), classes_begin() + NumInstrSchedClasses);
  }

  Record *getModelOrItinDef(Record *ProcDef) const {
    Record *ModelDef = ProcDef->getValueAsDef("SchedModel");
    Record *ItinsDef = ProcDef->getValueAsDef("ProcItin");
    if (!ItinsDef->getValueAsListOfDefs("IID").empty()) {
      assert(ModelDef->getValueAsBit("NoModel") &&
             "Itineraries must be defined within SchedMachineModel");
      return ItinsDef;
    }
    return ModelDef;
  }

  const CodeGenProcModel &getModelForProc(Record *ProcDef) const {
    Record *ModelDef = getModelOrItinDef(ProcDef);
    ProcModelMapTy::const_iterator I = ProcModelMap.find(ModelDef);
    assert(I != ProcModelMap.end() && "missing machine model");
    return ProcModels[I->second];
  }

  CodeGenProcModel &getProcModel(Record *ModelDef) {
    ProcModelMapTy::const_iterator I = ProcModelMap.find(ModelDef);
    assert(I != ProcModelMap.end() && "missing machine model");
    return ProcModels[I->second];
  }
  const CodeGenProcModel &getProcModel(Record *ModelDef) const {
    return const_cast<CodeGenSchedModels *>(this)->getProcModel(ModelDef);
  }

  // Iterate over the unique processor models.
  using ProcIter = std::vector<CodeGenProcModel>::const_iterator;
  ProcIter procModelBegin() const { return ProcModels.begin(); }
  ProcIter procModelEnd() const { return ProcModels.end(); }
  ArrayRef<CodeGenProcModel> procModels() const { return ProcModels; }

  // Return true if any processors have itineraries.
  bool hasItineraries() const;

  // Get a SchedWrite from its index.
  const CodeGenSchedRW &getSchedWrite(unsigned Idx) const {
    assert(Idx < SchedWrites.size() && "bad SchedWrite index");
    assert(SchedWrites[Idx].isValid() && "invalid SchedWrite");
    return SchedWrites[Idx];
  }
  // Get a SchedWrite from its index.
  const CodeGenSchedRW &getSchedRead(unsigned Idx) const {
    assert(Idx < SchedReads.size() && "bad SchedRead index");
    assert(SchedReads[Idx].isValid() && "invalid SchedRead");
    return SchedReads[Idx];
  }

  const CodeGenSchedRW &getSchedRW(unsigned Idx, bool IsRead) const {
    return IsRead ? getSchedRead(Idx) : getSchedWrite(Idx);
  }
  CodeGenSchedRW &getSchedRW(Record *Def) {
    bool IsRead = Def->isSubClassOf("SchedRead");
    unsigned Idx = getSchedRWIdx(Def, IsRead);
    return const_cast<CodeGenSchedRW &>(IsRead ? getSchedRead(Idx)
                                               : getSchedWrite(Idx));
  }
  const CodeGenSchedRW &getSchedRW(Record *Def) const {
    return const_cast<CodeGenSchedModels &>(*this).getSchedRW(Def);
  }

  unsigned getSchedRWIdx(const Record *Def, bool IsRead) const;

  // Get a SchedClass from its index.
  CodeGenSchedClass &getSchedClass(unsigned Idx) {
    assert(Idx < SchedClasses.size() && "bad SchedClass index");
    return SchedClasses[Idx];
  }
  const CodeGenSchedClass &getSchedClass(unsigned Idx) const {
    assert(Idx < SchedClasses.size() && "bad SchedClass index");
    return SchedClasses[Idx];
  }

  // Get the SchedClass index for an instruction. Instructions with no
  // itinerary, no SchedReadWrites, and no InstrReadWrites references return 0
  // for NoItinerary.
  unsigned getSchedClassIdx(const CodeGenInstruction &Inst) const;

  using SchedClassIter = std::vector<CodeGenSchedClass>::const_iterator;
  SchedClassIter schedClassBegin() const { return SchedClasses.begin(); }
  SchedClassIter schedClassEnd() const { return SchedClasses.end(); }
  ArrayRef<CodeGenSchedClass> schedClasses() const { return SchedClasses; }

  unsigned numInstrSchedClasses() const { return NumInstrSchedClasses; }

  void findRWs(const RecVec &RWDefs, IdxVec &Writes, IdxVec &Reads) const;
  void findRWs(const RecVec &RWDefs, IdxVec &RWs, bool IsRead) const;
  void expandRWSequence(unsigned RWIdx, IdxVec &RWSeq, bool IsRead) const;
  void expandRWSeqForProc(unsigned RWIdx, IdxVec &RWSeq, bool IsRead,
                          const CodeGenProcModel &ProcModel) const;

  unsigned addSchedClass(Record *ItinDef, ArrayRef<unsigned> OperWrites,
                         ArrayRef<unsigned> OperReads,
                         ArrayRef<unsigned> ProcIndices);

  unsigned findOrInsertRW(ArrayRef<unsigned> Seq, bool IsRead);

  Record *findProcResUnits(Record *ProcResKind, const CodeGenProcModel &PM,
                           ArrayRef<SMLoc> Loc) const;

  ArrayRef<STIPredicateFunction> getSTIPredicates() const {
    return STIPredicates;
  }

private:
  void collectProcModels();

  // Initialize a new processor model if it is unique.
  void addProcModel(Record *ProcDef);

  void collectSchedRW();

  std::string genRWName(ArrayRef<unsigned> Seq, bool IsRead);
  unsigned findRWForSequence(ArrayRef<unsigned> Seq, bool IsRead);

  void collectSchedClasses();

  void collectRetireControlUnits();

  void collectRegisterFiles();

  void collectOptionalProcessorInfo();

  std::string createSchedClassName(Record *ItinClassDef,
                                   ArrayRef<unsigned> OperWrites,
                                   ArrayRef<unsigned> OperReads);
  std::string createSchedClassName(const RecVec &InstDefs);
  void createInstRWClass(Record *InstRWDef);

  void collectProcItins();

  void collectProcItinRW();

  void collectProcUnsupportedFeatures();

  void inferSchedClasses();

  void checkMCInstPredicates() const;

  void checkSTIPredicates() const;

  void collectSTIPredicates();

  void collectLoadStoreQueueInfo();

  void checkCompleteness();

  void inferFromRW(ArrayRef<unsigned> OperWrites, ArrayRef<unsigned> OperReads,
                   unsigned FromClassIdx, ArrayRef<unsigned> ProcIndices);
  void inferFromItinClass(Record *ItinClassDef, unsigned FromClassIdx);
  void inferFromInstRWs(unsigned SCIdx);

  bool hasSuperGroup(RecVec &SubUnits, CodeGenProcModel &PM);
  void verifyProcResourceGroups(CodeGenProcModel &PM);

  void collectProcResources();

  void collectItinProcResources(Record *ItinClassDef);

  void collectRWResources(unsigned RWIdx, bool IsRead,
                          ArrayRef<unsigned> ProcIndices);

  void collectRWResources(ArrayRef<unsigned> Writes, ArrayRef<unsigned> Reads,
                          ArrayRef<unsigned> ProcIndices);

  void addProcResource(Record *ProcResourceKind, CodeGenProcModel &PM,
                       ArrayRef<SMLoc> Loc);

  void addWriteRes(Record *ProcWriteResDef, unsigned PIdx);

  void addReadAdvance(Record *ProcReadAdvanceDef, unsigned PIdx);
};

} // namespace llvm

#endif
