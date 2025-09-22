//===- Bitcode/Writer/BitcodeWriter.cpp - Bitcode Writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bitcode writer implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeWriter.h"
#include "ValueEnumerator.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeCommon.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRangeList.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/UseListOrder.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/IRSymtab.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<unsigned>
    IndexThreshold("bitcode-mdindex-threshold", cl::Hidden, cl::init(25),
                   cl::desc("Number of metadatas above which we emit an index "
                            "to enable lazy-loading"));
static cl::opt<uint32_t> FlushThreshold(
    "bitcode-flush-threshold", cl::Hidden, cl::init(512),
    cl::desc("The threshold (unit M) for flushing LLVM bitcode."));

static cl::opt<bool> WriteRelBFToSummary(
    "write-relbf-to-summary", cl::Hidden, cl::init(false),
    cl::desc("Write relative block frequency to function summary "));

namespace llvm {
extern FunctionSummary::ForceSummaryHotnessType ForceSummaryEdgesCold;
}

extern bool WriteNewDbgInfoFormatToBitcode;
extern llvm::cl::opt<bool> UseNewDbgInfoFormat;

namespace {

/// These are manifest constants used by the bitcode writer. They do not need to
/// be kept in sync with the reader, but need to be consistent within this file.
enum {
  // VALUE_SYMTAB_BLOCK abbrev id's.
  VST_ENTRY_8_ABBREV = bitc::FIRST_APPLICATION_ABBREV,
  VST_ENTRY_7_ABBREV,
  VST_ENTRY_6_ABBREV,
  VST_BBENTRY_6_ABBREV,

  // CONSTANTS_BLOCK abbrev id's.
  CONSTANTS_SETTYPE_ABBREV = bitc::FIRST_APPLICATION_ABBREV,
  CONSTANTS_INTEGER_ABBREV,
  CONSTANTS_CE_CAST_Abbrev,
  CONSTANTS_NULL_Abbrev,

  // FUNCTION_BLOCK abbrev id's.
  FUNCTION_INST_LOAD_ABBREV = bitc::FIRST_APPLICATION_ABBREV,
  FUNCTION_INST_UNOP_ABBREV,
  FUNCTION_INST_UNOP_FLAGS_ABBREV,
  FUNCTION_INST_BINOP_ABBREV,
  FUNCTION_INST_BINOP_FLAGS_ABBREV,
  FUNCTION_INST_CAST_ABBREV,
  FUNCTION_INST_CAST_FLAGS_ABBREV,
  FUNCTION_INST_RET_VOID_ABBREV,
  FUNCTION_INST_RET_VAL_ABBREV,
  FUNCTION_INST_UNREACHABLE_ABBREV,
  FUNCTION_INST_GEP_ABBREV,
  FUNCTION_DEBUG_RECORD_VALUE_ABBREV,
};

/// Abstract class to manage the bitcode writing, subclassed for each bitcode
/// file type.
class BitcodeWriterBase {
protected:
  /// The stream created and owned by the client.
  BitstreamWriter &Stream;

  StringTableBuilder &StrtabBuilder;

public:
  /// Constructs a BitcodeWriterBase object that writes to the provided
  /// \p Stream.
  BitcodeWriterBase(BitstreamWriter &Stream, StringTableBuilder &StrtabBuilder)
      : Stream(Stream), StrtabBuilder(StrtabBuilder) {}

protected:
  void writeModuleVersion();
};

void BitcodeWriterBase::writeModuleVersion() {
  // VERSION: [version#]
  Stream.EmitRecord(bitc::MODULE_CODE_VERSION, ArrayRef<uint64_t>{2});
}

/// Base class to manage the module bitcode writing, currently subclassed for
/// ModuleBitcodeWriter and ThinLinkBitcodeWriter.
class ModuleBitcodeWriterBase : public BitcodeWriterBase {
protected:
  /// The Module to write to bitcode.
  const Module &M;

  /// Enumerates ids for all values in the module.
  ValueEnumerator VE;

  /// Optional per-module index to write for ThinLTO.
  const ModuleSummaryIndex *Index;

  /// Map that holds the correspondence between GUIDs in the summary index,
  /// that came from indirect call profiles, and a value id generated by this
  /// class to use in the VST and summary block records.
  std::map<GlobalValue::GUID, unsigned> GUIDToValueIdMap;

  /// Tracks the last value id recorded in the GUIDToValueMap.
  unsigned GlobalValueId;

  /// Saves the offset of the VSTOffset record that must eventually be
  /// backpatched with the offset of the actual VST.
  uint64_t VSTOffsetPlaceholder = 0;

public:
  /// Constructs a ModuleBitcodeWriterBase object for the given Module,
  /// writing to the provided \p Buffer.
  ModuleBitcodeWriterBase(const Module &M, StringTableBuilder &StrtabBuilder,
                          BitstreamWriter &Stream,
                          bool ShouldPreserveUseListOrder,
                          const ModuleSummaryIndex *Index)
      : BitcodeWriterBase(Stream, StrtabBuilder), M(M),
        VE(M, ShouldPreserveUseListOrder), Index(Index) {
    // Assign ValueIds to any callee values in the index that came from
    // indirect call profiles and were recorded as a GUID not a Value*
    // (which would have been assigned an ID by the ValueEnumerator).
    // The starting ValueId is just after the number of values in the
    // ValueEnumerator, so that they can be emitted in the VST.
    GlobalValueId = VE.getValues().size();
    if (!Index)
      return;
    for (const auto &GUIDSummaryLists : *Index)
      // Examine all summaries for this GUID.
      for (auto &Summary : GUIDSummaryLists.second.SummaryList)
        if (auto FS = dyn_cast<FunctionSummary>(Summary.get())) {
          // For each call in the function summary, see if the call
          // is to a GUID (which means it is for an indirect call,
          // otherwise we would have a Value for it). If so, synthesize
          // a value id.
          for (auto &CallEdge : FS->calls())
            if (!CallEdge.first.haveGVs() || !CallEdge.first.getValue())
              assignValueId(CallEdge.first.getGUID());

          // For each referenced variables in the function summary, see if the
          // variable is represented by a GUID (as opposed to a symbol to
          // declarations or definitions in the module). If so, synthesize a
          // value id.
          for (auto &RefEdge : FS->refs())
            if (!RefEdge.haveGVs() || !RefEdge.getValue())
              assignValueId(RefEdge.getGUID());
        }
  }

protected:
  void writePerModuleGlobalValueSummary();

private:
  void writePerModuleFunctionSummaryRecord(
      SmallVector<uint64_t, 64> &NameVals, GlobalValueSummary *Summary,
      unsigned ValueID, unsigned FSCallsAbbrev, unsigned FSCallsProfileAbbrev,
      unsigned CallsiteAbbrev, unsigned AllocAbbrev, const Function &F);
  void writeModuleLevelReferences(const GlobalVariable &V,
                                  SmallVector<uint64_t, 64> &NameVals,
                                  unsigned FSModRefsAbbrev,
                                  unsigned FSModVTableRefsAbbrev);

  void assignValueId(GlobalValue::GUID ValGUID) {
    GUIDToValueIdMap[ValGUID] = ++GlobalValueId;
  }

  unsigned getValueId(GlobalValue::GUID ValGUID) {
    const auto &VMI = GUIDToValueIdMap.find(ValGUID);
    // Expect that any GUID value had a value Id assigned by an
    // earlier call to assignValueId.
    assert(VMI != GUIDToValueIdMap.end() &&
           "GUID does not have assigned value Id");
    return VMI->second;
  }

  // Helper to get the valueId for the type of value recorded in VI.
  unsigned getValueId(ValueInfo VI) {
    if (!VI.haveGVs() || !VI.getValue())
      return getValueId(VI.getGUID());
    return VE.getValueID(VI.getValue());
  }

  std::map<GlobalValue::GUID, unsigned> &valueIds() { return GUIDToValueIdMap; }
};

/// Class to manage the bitcode writing for a module.
class ModuleBitcodeWriter : public ModuleBitcodeWriterBase {
  /// True if a module hash record should be written.
  bool GenerateHash;

  /// If non-null, when GenerateHash is true, the resulting hash is written
  /// into ModHash.
  ModuleHash *ModHash;

  SHA1 Hasher;

  /// The start bit of the identification block.
  uint64_t BitcodeStartBit;

public:
  /// Constructs a ModuleBitcodeWriter object for the given Module,
  /// writing to the provided \p Buffer.
  ModuleBitcodeWriter(const Module &M, StringTableBuilder &StrtabBuilder,
                      BitstreamWriter &Stream, bool ShouldPreserveUseListOrder,
                      const ModuleSummaryIndex *Index, bool GenerateHash,
                      ModuleHash *ModHash = nullptr)
      : ModuleBitcodeWriterBase(M, StrtabBuilder, Stream,
                                ShouldPreserveUseListOrder, Index),
        GenerateHash(GenerateHash), ModHash(ModHash),
        BitcodeStartBit(Stream.GetCurrentBitNo()) {}

  /// Emit the current module to the bitstream.
  void write();

private:
  uint64_t bitcodeStartBit() { return BitcodeStartBit; }

  size_t addToStrtab(StringRef Str);

  void writeAttributeGroupTable();
  void writeAttributeTable();
  void writeTypeTable();
  void writeComdats();
  void writeValueSymbolTableForwardDecl();
  void writeModuleInfo();
  void writeValueAsMetadata(const ValueAsMetadata *MD,
                            SmallVectorImpl<uint64_t> &Record);
  void writeMDTuple(const MDTuple *N, SmallVectorImpl<uint64_t> &Record,
                    unsigned Abbrev);
  unsigned createDILocationAbbrev();
  void writeDILocation(const DILocation *N, SmallVectorImpl<uint64_t> &Record,
                       unsigned &Abbrev);
  unsigned createGenericDINodeAbbrev();
  void writeGenericDINode(const GenericDINode *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned &Abbrev);
  void writeDISubrange(const DISubrange *N, SmallVectorImpl<uint64_t> &Record,
                       unsigned Abbrev);
  void writeDIGenericSubrange(const DIGenericSubrange *N,
                              SmallVectorImpl<uint64_t> &Record,
                              unsigned Abbrev);
  void writeDIEnumerator(const DIEnumerator *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIBasicType(const DIBasicType *N, SmallVectorImpl<uint64_t> &Record,
                        unsigned Abbrev);
  void writeDIStringType(const DIStringType *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIDerivedType(const DIDerivedType *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDICompositeType(const DICompositeType *N,
                            SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDISubroutineType(const DISubroutineType *N,
                             SmallVectorImpl<uint64_t> &Record,
                             unsigned Abbrev);
  void writeDIFile(const DIFile *N, SmallVectorImpl<uint64_t> &Record,
                   unsigned Abbrev);
  void writeDICompileUnit(const DICompileUnit *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDISubprogram(const DISubprogram *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDILexicalBlock(const DILexicalBlock *N,
                           SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDILexicalBlockFile(const DILexicalBlockFile *N,
                               SmallVectorImpl<uint64_t> &Record,
                               unsigned Abbrev);
  void writeDICommonBlock(const DICommonBlock *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDINamespace(const DINamespace *N, SmallVectorImpl<uint64_t> &Record,
                        unsigned Abbrev);
  void writeDIMacro(const DIMacro *N, SmallVectorImpl<uint64_t> &Record,
                    unsigned Abbrev);
  void writeDIMacroFile(const DIMacroFile *N, SmallVectorImpl<uint64_t> &Record,
                        unsigned Abbrev);
  void writeDIArgList(const DIArgList *N, SmallVectorImpl<uint64_t> &Record);
  void writeDIModule(const DIModule *N, SmallVectorImpl<uint64_t> &Record,
                     unsigned Abbrev);
  void writeDIAssignID(const DIAssignID *N, SmallVectorImpl<uint64_t> &Record,
                       unsigned Abbrev);
  void writeDITemplateTypeParameter(const DITemplateTypeParameter *N,
                                    SmallVectorImpl<uint64_t> &Record,
                                    unsigned Abbrev);
  void writeDITemplateValueParameter(const DITemplateValueParameter *N,
                                     SmallVectorImpl<uint64_t> &Record,
                                     unsigned Abbrev);
  void writeDIGlobalVariable(const DIGlobalVariable *N,
                             SmallVectorImpl<uint64_t> &Record,
                             unsigned Abbrev);
  void writeDILocalVariable(const DILocalVariable *N,
                            SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDILabel(const DILabel *N,
                    SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIExpression(const DIExpression *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIGlobalVariableExpression(const DIGlobalVariableExpression *N,
                                       SmallVectorImpl<uint64_t> &Record,
                                       unsigned Abbrev);
  void writeDIObjCProperty(const DIObjCProperty *N,
                           SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIImportedEntity(const DIImportedEntity *N,
                             SmallVectorImpl<uint64_t> &Record,
                             unsigned Abbrev);
  unsigned createNamedMetadataAbbrev();
  void writeNamedMetadata(SmallVectorImpl<uint64_t> &Record);
  unsigned createMetadataStringsAbbrev();
  void writeMetadataStrings(ArrayRef<const Metadata *> Strings,
                            SmallVectorImpl<uint64_t> &Record);
  void writeMetadataRecords(ArrayRef<const Metadata *> MDs,
                            SmallVectorImpl<uint64_t> &Record,
                            std::vector<unsigned> *MDAbbrevs = nullptr,
                            std::vector<uint64_t> *IndexPos = nullptr);
  void writeModuleMetadata();
  void writeFunctionMetadata(const Function &F);
  void writeFunctionMetadataAttachment(const Function &F);
  void pushGlobalMetadataAttachment(SmallVectorImpl<uint64_t> &Record,
                                    const GlobalObject &GO);
  void writeModuleMetadataKinds();
  void writeOperandBundleTags();
  void writeSyncScopeNames();
  void writeConstants(unsigned FirstVal, unsigned LastVal, bool isGlobal);
  void writeModuleConstants();
  bool pushValueAndType(const Value *V, unsigned InstID,
                        SmallVectorImpl<unsigned> &Vals);
  void writeOperandBundles(const CallBase &CB, unsigned InstID);
  void pushValue(const Value *V, unsigned InstID,
                 SmallVectorImpl<unsigned> &Vals);
  void pushValueSigned(const Value *V, unsigned InstID,
                       SmallVectorImpl<uint64_t> &Vals);
  void writeInstruction(const Instruction &I, unsigned InstID,
                        SmallVectorImpl<unsigned> &Vals);
  void writeFunctionLevelValueSymbolTable(const ValueSymbolTable &VST);
  void writeGlobalValueSymbolTable(
      DenseMap<const Function *, uint64_t> &FunctionToBitcodeIndex);
  void writeUseList(UseListOrder &&Order);
  void writeUseListBlock(const Function *F);
  void
  writeFunction(const Function &F,
                DenseMap<const Function *, uint64_t> &FunctionToBitcodeIndex);
  void writeBlockInfo();
  void writeModuleHash(StringRef View);

  unsigned getEncodedSyncScopeID(SyncScope::ID SSID) {
    return unsigned(SSID);
  }

  unsigned getEncodedAlign(MaybeAlign Alignment) { return encode(Alignment); }
};

/// Class to manage the bitcode writing for a combined index.
class IndexBitcodeWriter : public BitcodeWriterBase {
  /// The combined index to write to bitcode.
  const ModuleSummaryIndex &Index;

  /// When writing combined summaries, provides the set of global value
  /// summaries for which the value (function, function alias, etc) should be
  /// imported as a declaration.
  const GVSummaryPtrSet *DecSummaries = nullptr;

  /// When writing a subset of the index for distributed backends, client
  /// provides a map of modules to the corresponding GUIDs/summaries to write.
  const std::map<std::string, GVSummaryMapTy> *ModuleToSummariesForIndex;

  /// Map that holds the correspondence between the GUID used in the combined
  /// index and a value id generated by this class to use in references.
  std::map<GlobalValue::GUID, unsigned> GUIDToValueIdMap;

  // The stack ids used by this index, which will be a subset of those in
  // the full index in the case of distributed indexes.
  std::vector<uint64_t> StackIds;

  // Keep a map of the stack id indices used by records being written for this
  // index to the index of the corresponding stack id in the above StackIds
  // vector. Ensures we write each referenced stack id once.
  DenseMap<unsigned, unsigned> StackIdIndicesToIndex;

  /// Tracks the last value id recorded in the GUIDToValueMap.
  unsigned GlobalValueId = 0;

  /// Tracks the assignment of module paths in the module path string table to
  /// an id assigned for use in summary references to the module path.
  DenseMap<StringRef, uint64_t> ModuleIdMap;

public:
  /// Constructs a IndexBitcodeWriter object for the given combined index,
  /// writing to the provided \p Buffer. When writing a subset of the index
  /// for a distributed backend, provide a \p ModuleToSummariesForIndex map.
  /// If provided, \p DecSummaries specifies the set of summaries for which
  /// the corresponding functions or aliased functions should be imported as a
  /// declaration (but not definition) for each module.
  IndexBitcodeWriter(BitstreamWriter &Stream, StringTableBuilder &StrtabBuilder,
                     const ModuleSummaryIndex &Index,
                     const GVSummaryPtrSet *DecSummaries = nullptr,
                     const std::map<std::string, GVSummaryMapTy>
                         *ModuleToSummariesForIndex = nullptr)
      : BitcodeWriterBase(Stream, StrtabBuilder), Index(Index),
        DecSummaries(DecSummaries),
        ModuleToSummariesForIndex(ModuleToSummariesForIndex) {

    // See if the StackIdIndex was already added to the StackId map and
    // vector. If not, record it.
    auto RecordStackIdReference = [&](unsigned StackIdIndex) {
      // If the StackIdIndex is not yet in the map, the below insert ensures
      // that it will point to the new StackIds vector entry we push to just
      // below.
      auto Inserted =
          StackIdIndicesToIndex.insert({StackIdIndex, StackIds.size()});
      if (Inserted.second)
        StackIds.push_back(Index.getStackIdAtIndex(StackIdIndex));
    };

    // Assign unique value ids to all summaries to be written, for use
    // in writing out the call graph edges. Save the mapping from GUID
    // to the new global value id to use when writing those edges, which
    // are currently saved in the index in terms of GUID.
    forEachSummary([&](GVInfo I, bool IsAliasee) {
      GUIDToValueIdMap[I.first] = ++GlobalValueId;
      if (IsAliasee)
        return;
      auto *FS = dyn_cast<FunctionSummary>(I.second);
      if (!FS)
        return;
      // Record all stack id indices actually used in the summary entries being
      // written, so that we can compact them in the case of distributed ThinLTO
      // indexes.
      for (auto &CI : FS->callsites()) {
        // If the stack id list is empty, this callsite info was synthesized for
        // a missing tail call frame. Ensure that the callee's GUID gets a value
        // id. Normally we only generate these for defined summaries, which in
        // the case of distributed ThinLTO is only the functions already defined
        // in the module or that we want to import. We don't bother to include
        // all the callee symbols as they aren't normally needed in the backend.
        // However, for the synthesized callsite infos we do need the callee
        // GUID in the backend so that we can correlate the identified callee
        // with this callsite info (which for non-tail calls is done by the
        // ordering of the callsite infos and verified via stack ids).
        if (CI.StackIdIndices.empty()) {
          GUIDToValueIdMap[CI.Callee.getGUID()] = ++GlobalValueId;
          continue;
        }
        for (auto Idx : CI.StackIdIndices)
          RecordStackIdReference(Idx);
      }
      for (auto &AI : FS->allocs())
        for (auto &MIB : AI.MIBs)
          for (auto Idx : MIB.StackIdIndices)
            RecordStackIdReference(Idx);
    });
  }

  /// The below iterator returns the GUID and associated summary.
  using GVInfo = std::pair<GlobalValue::GUID, GlobalValueSummary *>;

  /// Calls the callback for each value GUID and summary to be written to
  /// bitcode. This hides the details of whether they are being pulled from the
  /// entire index or just those in a provided ModuleToSummariesForIndex map.
  template<typename Functor>
  void forEachSummary(Functor Callback) {
    if (ModuleToSummariesForIndex) {
      for (auto &M : *ModuleToSummariesForIndex)
        for (auto &Summary : M.second) {
          Callback(Summary, false);
          // Ensure aliasee is handled, e.g. for assigning a valueId,
          // even if we are not importing the aliasee directly (the
          // imported alias will contain a copy of aliasee).
          if (auto *AS = dyn_cast<AliasSummary>(Summary.getSecond()))
            Callback({AS->getAliaseeGUID(), &AS->getAliasee()}, true);
        }
    } else {
      for (auto &Summaries : Index)
        for (auto &Summary : Summaries.second.SummaryList)
          Callback({Summaries.first, Summary.get()}, false);
    }
  }

  /// Calls the callback for each entry in the modulePaths StringMap that
  /// should be written to the module path string table. This hides the details
  /// of whether they are being pulled from the entire index or just those in a
  /// provided ModuleToSummariesForIndex map.
  template <typename Functor> void forEachModule(Functor Callback) {
    if (ModuleToSummariesForIndex) {
      for (const auto &M : *ModuleToSummariesForIndex) {
        const auto &MPI = Index.modulePaths().find(M.first);
        if (MPI == Index.modulePaths().end()) {
          // This should only happen if the bitcode file was empty, in which
          // case we shouldn't be importing (the ModuleToSummariesForIndex
          // would only include the module we are writing and index for).
          assert(ModuleToSummariesForIndex->size() == 1);
          continue;
        }
        Callback(*MPI);
      }
    } else {
      // Since StringMap iteration order isn't guaranteed, order by path string
      // first.
      // FIXME: Make this a vector of StringMapEntry instead to avoid the later
      // map lookup.
      std::vector<StringRef> ModulePaths;
      for (auto &[ModPath, _] : Index.modulePaths())
        ModulePaths.push_back(ModPath);
      llvm::sort(ModulePaths.begin(), ModulePaths.end());
      for (auto &ModPath : ModulePaths)
        Callback(*Index.modulePaths().find(ModPath));
    }
  }

  /// Main entry point for writing a combined index to bitcode.
  void write();

private:
  void writeModStrings();
  void writeCombinedGlobalValueSummary();

  std::optional<unsigned> getValueId(GlobalValue::GUID ValGUID) {
    auto VMI = GUIDToValueIdMap.find(ValGUID);
    if (VMI == GUIDToValueIdMap.end())
      return std::nullopt;
    return VMI->second;
  }

  std::map<GlobalValue::GUID, unsigned> &valueIds() { return GUIDToValueIdMap; }
};

} // end anonymous namespace

static unsigned getEncodedCastOpcode(unsigned Opcode) {
  switch (Opcode) {
  default: llvm_unreachable("Unknown cast instruction!");
  case Instruction::Trunc   : return bitc::CAST_TRUNC;
  case Instruction::ZExt    : return bitc::CAST_ZEXT;
  case Instruction::SExt    : return bitc::CAST_SEXT;
  case Instruction::FPToUI  : return bitc::CAST_FPTOUI;
  case Instruction::FPToSI  : return bitc::CAST_FPTOSI;
  case Instruction::UIToFP  : return bitc::CAST_UITOFP;
  case Instruction::SIToFP  : return bitc::CAST_SITOFP;
  case Instruction::FPTrunc : return bitc::CAST_FPTRUNC;
  case Instruction::FPExt   : return bitc::CAST_FPEXT;
  case Instruction::PtrToInt: return bitc::CAST_PTRTOINT;
  case Instruction::IntToPtr: return bitc::CAST_INTTOPTR;
  case Instruction::BitCast : return bitc::CAST_BITCAST;
  case Instruction::AddrSpaceCast: return bitc::CAST_ADDRSPACECAST;
  }
}

static unsigned getEncodedUnaryOpcode(unsigned Opcode) {
  switch (Opcode) {
  default: llvm_unreachable("Unknown binary instruction!");
  case Instruction::FNeg: return bitc::UNOP_FNEG;
  }
}

static unsigned getEncodedBinaryOpcode(unsigned Opcode) {
  switch (Opcode) {
  default: llvm_unreachable("Unknown binary instruction!");
  case Instruction::Add:
  case Instruction::FAdd: return bitc::BINOP_ADD;
  case Instruction::Sub:
  case Instruction::FSub: return bitc::BINOP_SUB;
  case Instruction::Mul:
  case Instruction::FMul: return bitc::BINOP_MUL;
  case Instruction::UDiv: return bitc::BINOP_UDIV;
  case Instruction::FDiv:
  case Instruction::SDiv: return bitc::BINOP_SDIV;
  case Instruction::URem: return bitc::BINOP_UREM;
  case Instruction::FRem:
  case Instruction::SRem: return bitc::BINOP_SREM;
  case Instruction::Shl:  return bitc::BINOP_SHL;
  case Instruction::LShr: return bitc::BINOP_LSHR;
  case Instruction::AShr: return bitc::BINOP_ASHR;
  case Instruction::And:  return bitc::BINOP_AND;
  case Instruction::Or:   return bitc::BINOP_OR;
  case Instruction::Xor:  return bitc::BINOP_XOR;
  }
}

static unsigned getEncodedRMWOperation(AtomicRMWInst::BinOp Op) {
  switch (Op) {
  default: llvm_unreachable("Unknown RMW operation!");
  case AtomicRMWInst::Xchg: return bitc::RMW_XCHG;
  case AtomicRMWInst::Add: return bitc::RMW_ADD;
  case AtomicRMWInst::Sub: return bitc::RMW_SUB;
  case AtomicRMWInst::And: return bitc::RMW_AND;
  case AtomicRMWInst::Nand: return bitc::RMW_NAND;
  case AtomicRMWInst::Or: return bitc::RMW_OR;
  case AtomicRMWInst::Xor: return bitc::RMW_XOR;
  case AtomicRMWInst::Max: return bitc::RMW_MAX;
  case AtomicRMWInst::Min: return bitc::RMW_MIN;
  case AtomicRMWInst::UMax: return bitc::RMW_UMAX;
  case AtomicRMWInst::UMin: return bitc::RMW_UMIN;
  case AtomicRMWInst::FAdd: return bitc::RMW_FADD;
  case AtomicRMWInst::FSub: return bitc::RMW_FSUB;
  case AtomicRMWInst::FMax: return bitc::RMW_FMAX;
  case AtomicRMWInst::FMin: return bitc::RMW_FMIN;
  case AtomicRMWInst::UIncWrap:
    return bitc::RMW_UINC_WRAP;
  case AtomicRMWInst::UDecWrap:
    return bitc::RMW_UDEC_WRAP;
  }
}

static unsigned getEncodedOrdering(AtomicOrdering Ordering) {
  switch (Ordering) {
  case AtomicOrdering::NotAtomic: return bitc::ORDERING_NOTATOMIC;
  case AtomicOrdering::Unordered: return bitc::ORDERING_UNORDERED;
  case AtomicOrdering::Monotonic: return bitc::ORDERING_MONOTONIC;
  case AtomicOrdering::Acquire: return bitc::ORDERING_ACQUIRE;
  case AtomicOrdering::Release: return bitc::ORDERING_RELEASE;
  case AtomicOrdering::AcquireRelease: return bitc::ORDERING_ACQREL;
  case AtomicOrdering::SequentiallyConsistent: return bitc::ORDERING_SEQCST;
  }
  llvm_unreachable("Invalid ordering");
}

static void writeStringRecord(BitstreamWriter &Stream, unsigned Code,
                              StringRef Str, unsigned AbbrevToUse) {
  SmallVector<unsigned, 64> Vals;

  // Code: [strchar x N]
  for (char C : Str) {
    if (AbbrevToUse && !BitCodeAbbrevOp::isChar6(C))
      AbbrevToUse = 0;
    Vals.push_back(C);
  }

  // Emit the finished record.
  Stream.EmitRecord(Code, Vals, AbbrevToUse);
}

static uint64_t getAttrKindEncoding(Attribute::AttrKind Kind) {
  switch (Kind) {
  case Attribute::Alignment:
    return bitc::ATTR_KIND_ALIGNMENT;
  case Attribute::AllocAlign:
    return bitc::ATTR_KIND_ALLOC_ALIGN;
  case Attribute::AllocSize:
    return bitc::ATTR_KIND_ALLOC_SIZE;
  case Attribute::AlwaysInline:
    return bitc::ATTR_KIND_ALWAYS_INLINE;
  case Attribute::Builtin:
    return bitc::ATTR_KIND_BUILTIN;
  case Attribute::ByVal:
    return bitc::ATTR_KIND_BY_VAL;
  case Attribute::Convergent:
    return bitc::ATTR_KIND_CONVERGENT;
  case Attribute::InAlloca:
    return bitc::ATTR_KIND_IN_ALLOCA;
  case Attribute::Cold:
    return bitc::ATTR_KIND_COLD;
  case Attribute::DisableSanitizerInstrumentation:
    return bitc::ATTR_KIND_DISABLE_SANITIZER_INSTRUMENTATION;
  case Attribute::FnRetThunkExtern:
    return bitc::ATTR_KIND_FNRETTHUNK_EXTERN;
  case Attribute::Hot:
    return bitc::ATTR_KIND_HOT;
  case Attribute::ElementType:
    return bitc::ATTR_KIND_ELEMENTTYPE;
  case Attribute::HybridPatchable:
    return bitc::ATTR_KIND_HYBRID_PATCHABLE;
  case Attribute::InlineHint:
    return bitc::ATTR_KIND_INLINE_HINT;
  case Attribute::InReg:
    return bitc::ATTR_KIND_IN_REG;
  case Attribute::JumpTable:
    return bitc::ATTR_KIND_JUMP_TABLE;
  case Attribute::MinSize:
    return bitc::ATTR_KIND_MIN_SIZE;
  case Attribute::AllocatedPointer:
    return bitc::ATTR_KIND_ALLOCATED_POINTER;
  case Attribute::AllocKind:
    return bitc::ATTR_KIND_ALLOC_KIND;
  case Attribute::Memory:
    return bitc::ATTR_KIND_MEMORY;
  case Attribute::NoFPClass:
    return bitc::ATTR_KIND_NOFPCLASS;
  case Attribute::Naked:
    return bitc::ATTR_KIND_NAKED;
  case Attribute::Nest:
    return bitc::ATTR_KIND_NEST;
  case Attribute::NoAlias:
    return bitc::ATTR_KIND_NO_ALIAS;
  case Attribute::NoBuiltin:
    return bitc::ATTR_KIND_NO_BUILTIN;
  case Attribute::NoCallback:
    return bitc::ATTR_KIND_NO_CALLBACK;
  case Attribute::NoCapture:
    return bitc::ATTR_KIND_NO_CAPTURE;
  case Attribute::NoDuplicate:
    return bitc::ATTR_KIND_NO_DUPLICATE;
  case Attribute::NoFree:
    return bitc::ATTR_KIND_NOFREE;
  case Attribute::NoImplicitFloat:
    return bitc::ATTR_KIND_NO_IMPLICIT_FLOAT;
  case Attribute::NoInline:
    return bitc::ATTR_KIND_NO_INLINE;
  case Attribute::NoRecurse:
    return bitc::ATTR_KIND_NO_RECURSE;
  case Attribute::NoMerge:
    return bitc::ATTR_KIND_NO_MERGE;
  case Attribute::NonLazyBind:
    return bitc::ATTR_KIND_NON_LAZY_BIND;
  case Attribute::NonNull:
    return bitc::ATTR_KIND_NON_NULL;
  case Attribute::Dereferenceable:
    return bitc::ATTR_KIND_DEREFERENCEABLE;
  case Attribute::DereferenceableOrNull:
    return bitc::ATTR_KIND_DEREFERENCEABLE_OR_NULL;
  case Attribute::NoRedZone:
    return bitc::ATTR_KIND_NO_RED_ZONE;
  case Attribute::NoReturn:
    return bitc::ATTR_KIND_NO_RETURN;
  case Attribute::NoSync:
    return bitc::ATTR_KIND_NOSYNC;
  case Attribute::NoCfCheck:
    return bitc::ATTR_KIND_NOCF_CHECK;
  case Attribute::NoProfile:
    return bitc::ATTR_KIND_NO_PROFILE;
  case Attribute::SkipProfile:
    return bitc::ATTR_KIND_SKIP_PROFILE;
  case Attribute::NoUnwind:
    return bitc::ATTR_KIND_NO_UNWIND;
  case Attribute::NoSanitizeBounds:
    return bitc::ATTR_KIND_NO_SANITIZE_BOUNDS;
  case Attribute::NoSanitizeCoverage:
    return bitc::ATTR_KIND_NO_SANITIZE_COVERAGE;
  case Attribute::NullPointerIsValid:
    return bitc::ATTR_KIND_NULL_POINTER_IS_VALID;
  case Attribute::OptimizeForDebugging:
    return bitc::ATTR_KIND_OPTIMIZE_FOR_DEBUGGING;
  case Attribute::OptForFuzzing:
    return bitc::ATTR_KIND_OPT_FOR_FUZZING;
  case Attribute::OptimizeForSize:
    return bitc::ATTR_KIND_OPTIMIZE_FOR_SIZE;
  case Attribute::OptimizeNone:
    return bitc::ATTR_KIND_OPTIMIZE_NONE;
  case Attribute::ReadNone:
    return bitc::ATTR_KIND_READ_NONE;
  case Attribute::ReadOnly:
    return bitc::ATTR_KIND_READ_ONLY;
  case Attribute::Returned:
    return bitc::ATTR_KIND_RETURNED;
  case Attribute::ReturnsTwice:
    return bitc::ATTR_KIND_RETURNS_TWICE;
  case Attribute::SExt:
    return bitc::ATTR_KIND_S_EXT;
  case Attribute::Speculatable:
    return bitc::ATTR_KIND_SPECULATABLE;
  case Attribute::StackAlignment:
    return bitc::ATTR_KIND_STACK_ALIGNMENT;
  case Attribute::StackProtect:
    return bitc::ATTR_KIND_STACK_PROTECT;
  case Attribute::StackProtectReq:
    return bitc::ATTR_KIND_STACK_PROTECT_REQ;
  case Attribute::StackProtectStrong:
    return bitc::ATTR_KIND_STACK_PROTECT_STRONG;
  case Attribute::SafeStack:
    return bitc::ATTR_KIND_SAFESTACK;
  case Attribute::ShadowCallStack:
    return bitc::ATTR_KIND_SHADOWCALLSTACK;
  case Attribute::StrictFP:
    return bitc::ATTR_KIND_STRICT_FP;
  case Attribute::StructRet:
    return bitc::ATTR_KIND_STRUCT_RET;
  case Attribute::SanitizeAddress:
    return bitc::ATTR_KIND_SANITIZE_ADDRESS;
  case Attribute::SanitizeHWAddress:
    return bitc::ATTR_KIND_SANITIZE_HWADDRESS;
  case Attribute::SanitizeThread:
    return bitc::ATTR_KIND_SANITIZE_THREAD;
  case Attribute::SanitizeMemory:
    return bitc::ATTR_KIND_SANITIZE_MEMORY;
  case Attribute::SanitizeNumericalStability:
    return bitc::ATTR_KIND_SANITIZE_NUMERICAL_STABILITY;
  case Attribute::SpeculativeLoadHardening:
    return bitc::ATTR_KIND_SPECULATIVE_LOAD_HARDENING;
  case Attribute::SwiftError:
    return bitc::ATTR_KIND_SWIFT_ERROR;
  case Attribute::SwiftSelf:
    return bitc::ATTR_KIND_SWIFT_SELF;
  case Attribute::SwiftAsync:
    return bitc::ATTR_KIND_SWIFT_ASYNC;
  case Attribute::UWTable:
    return bitc::ATTR_KIND_UW_TABLE;
  case Attribute::VScaleRange:
    return bitc::ATTR_KIND_VSCALE_RANGE;
  case Attribute::WillReturn:
    return bitc::ATTR_KIND_WILLRETURN;
  case Attribute::WriteOnly:
    return bitc::ATTR_KIND_WRITEONLY;
  case Attribute::ZExt:
    return bitc::ATTR_KIND_Z_EXT;
  case Attribute::ImmArg:
    return bitc::ATTR_KIND_IMMARG;
  case Attribute::SanitizeMemTag:
    return bitc::ATTR_KIND_SANITIZE_MEMTAG;
  case Attribute::Preallocated:
    return bitc::ATTR_KIND_PREALLOCATED;
  case Attribute::NoUndef:
    return bitc::ATTR_KIND_NOUNDEF;
  case Attribute::ByRef:
    return bitc::ATTR_KIND_BYREF;
  case Attribute::MustProgress:
    return bitc::ATTR_KIND_MUSTPROGRESS;
  case Attribute::PresplitCoroutine:
    return bitc::ATTR_KIND_PRESPLIT_COROUTINE;
  case Attribute::Writable:
    return bitc::ATTR_KIND_WRITABLE;
  case Attribute::CoroDestroyOnlyWhenComplete:
    return bitc::ATTR_KIND_CORO_ONLY_DESTROY_WHEN_COMPLETE;
  case Attribute::DeadOnUnwind:
    return bitc::ATTR_KIND_DEAD_ON_UNWIND;
  case Attribute::Range:
    return bitc::ATTR_KIND_RANGE;
  case Attribute::Initializes:
    return bitc::ATTR_KIND_INITIALIZES;
  case Attribute::EndAttrKinds:
    llvm_unreachable("Can not encode end-attribute kinds marker.");
  case Attribute::None:
    llvm_unreachable("Can not encode none-attribute.");
  case Attribute::EmptyKey:
  case Attribute::TombstoneKey:
    llvm_unreachable("Trying to encode EmptyKey/TombstoneKey");
  }

  llvm_unreachable("Trying to encode unknown attribute");
}

static void emitSignedInt64(SmallVectorImpl<uint64_t> &Vals, uint64_t V) {
  if ((int64_t)V >= 0)
    Vals.push_back(V << 1);
  else
    Vals.push_back((-V << 1) | 1);
}

static void emitWideAPInt(SmallVectorImpl<uint64_t> &Vals, const APInt &A) {
  // We have an arbitrary precision integer value to write whose
  // bit width is > 64. However, in canonical unsigned integer
  // format it is likely that the high bits are going to be zero.
  // So, we only write the number of active words.
  unsigned NumWords = A.getActiveWords();
  const uint64_t *RawData = A.getRawData();
  for (unsigned i = 0; i < NumWords; i++)
    emitSignedInt64(Vals, RawData[i]);
}

static void emitConstantRange(SmallVectorImpl<uint64_t> &Record,
                              const ConstantRange &CR, bool EmitBitWidth) {
  unsigned BitWidth = CR.getBitWidth();
  if (EmitBitWidth)
    Record.push_back(BitWidth);
  if (BitWidth > 64) {
    Record.push_back(CR.getLower().getActiveWords() |
                     (uint64_t(CR.getUpper().getActiveWords()) << 32));
    emitWideAPInt(Record, CR.getLower());
    emitWideAPInt(Record, CR.getUpper());
  } else {
    emitSignedInt64(Record, CR.getLower().getSExtValue());
    emitSignedInt64(Record, CR.getUpper().getSExtValue());
  }
}

void ModuleBitcodeWriter::writeAttributeGroupTable() {
  const std::vector<ValueEnumerator::IndexAndAttrSet> &AttrGrps =
      VE.getAttributeGroups();
  if (AttrGrps.empty()) return;

  Stream.EnterSubblock(bitc::PARAMATTR_GROUP_BLOCK_ID, 3);

  SmallVector<uint64_t, 64> Record;
  for (ValueEnumerator::IndexAndAttrSet Pair : AttrGrps) {
    unsigned AttrListIndex = Pair.first;
    AttributeSet AS = Pair.second;
    Record.push_back(VE.getAttributeGroupID(Pair));
    Record.push_back(AttrListIndex);

    for (Attribute Attr : AS) {
      if (Attr.isEnumAttribute()) {
        Record.push_back(0);
        Record.push_back(getAttrKindEncoding(Attr.getKindAsEnum()));
      } else if (Attr.isIntAttribute()) {
        Record.push_back(1);
        Record.push_back(getAttrKindEncoding(Attr.getKindAsEnum()));
        Record.push_back(Attr.getValueAsInt());
      } else if (Attr.isStringAttribute()) {
        StringRef Kind = Attr.getKindAsString();
        StringRef Val = Attr.getValueAsString();

        Record.push_back(Val.empty() ? 3 : 4);
        Record.append(Kind.begin(), Kind.end());
        Record.push_back(0);
        if (!Val.empty()) {
          Record.append(Val.begin(), Val.end());
          Record.push_back(0);
        }
      } else if (Attr.isTypeAttribute()) {
        Type *Ty = Attr.getValueAsType();
        Record.push_back(Ty ? 6 : 5);
        Record.push_back(getAttrKindEncoding(Attr.getKindAsEnum()));
        if (Ty)
          Record.push_back(VE.getTypeID(Attr.getValueAsType()));
      } else if (Attr.isConstantRangeAttribute()) {
        Record.push_back(7);
        Record.push_back(getAttrKindEncoding(Attr.getKindAsEnum()));
        emitConstantRange(Record, Attr.getValueAsConstantRange(),
                          /*EmitBitWidth=*/true);
      } else {
        assert(Attr.isConstantRangeListAttribute());
        Record.push_back(8);
        Record.push_back(getAttrKindEncoding(Attr.getKindAsEnum()));
        ArrayRef<ConstantRange> Val = Attr.getValueAsConstantRangeList();
        Record.push_back(Val.size());
        Record.push_back(Val[0].getBitWidth());
        for (auto &CR : Val)
          emitConstantRange(Record, CR, /*EmitBitWidth=*/false);
      }
    }

    Stream.EmitRecord(bitc::PARAMATTR_GRP_CODE_ENTRY, Record);
    Record.clear();
  }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeAttributeTable() {
  const std::vector<AttributeList> &Attrs = VE.getAttributeLists();
  if (Attrs.empty()) return;

  Stream.EnterSubblock(bitc::PARAMATTR_BLOCK_ID, 3);

  SmallVector<uint64_t, 64> Record;
  for (const AttributeList &AL : Attrs) {
    for (unsigned i : AL.indexes()) {
      AttributeSet AS = AL.getAttributes(i);
      if (AS.hasAttributes())
        Record.push_back(VE.getAttributeGroupID({i, AS}));
    }

    Stream.EmitRecord(bitc::PARAMATTR_CODE_ENTRY, Record);
    Record.clear();
  }

  Stream.ExitBlock();
}

/// WriteTypeTable - Write out the type table for a module.
void ModuleBitcodeWriter::writeTypeTable() {
  const ValueEnumerator::TypeList &TypeList = VE.getTypes();

  Stream.EnterSubblock(bitc::TYPE_BLOCK_ID_NEW, 4 /*count from # abbrevs */);
  SmallVector<uint64_t, 64> TypeVals;

  uint64_t NumBits = VE.computeBitsRequiredForTypeIndices();

  // Abbrev for TYPE_CODE_OPAQUE_POINTER.
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_OPAQUE_POINTER));
  Abbv->Add(BitCodeAbbrevOp(0)); // Addrspace = 0
  unsigned OpaquePtrAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_FUNCTION.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_FUNCTION));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));  // isvararg
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned FunctionAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_STRUCT_ANON.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_STRUCT_ANON));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));  // ispacked
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned StructAnonAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_STRUCT_NAME.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_STRUCT_NAME));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
  unsigned StructNameAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_STRUCT_NAMED.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_STRUCT_NAMED));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));  // ispacked
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned StructNamedAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_ARRAY.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_ARRAY));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // size
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned ArrayAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Emit an entry count so the reader can reserve space.
  TypeVals.push_back(TypeList.size());
  Stream.EmitRecord(bitc::TYPE_CODE_NUMENTRY, TypeVals);
  TypeVals.clear();

  // Loop over all of the types, emitting each in turn.
  for (Type *T : TypeList) {
    int AbbrevToUse = 0;
    unsigned Code = 0;

    switch (T->getTypeID()) {
    case Type::VoidTyID:      Code = bitc::TYPE_CODE_VOID;      break;
    case Type::HalfTyID:      Code = bitc::TYPE_CODE_HALF;      break;
    case Type::BFloatTyID:    Code = bitc::TYPE_CODE_BFLOAT;    break;
    case Type::FloatTyID:     Code = bitc::TYPE_CODE_FLOAT;     break;
    case Type::DoubleTyID:    Code = bitc::TYPE_CODE_DOUBLE;    break;
    case Type::X86_FP80TyID:  Code = bitc::TYPE_CODE_X86_FP80;  break;
    case Type::FP128TyID:     Code = bitc::TYPE_CODE_FP128;     break;
    case Type::PPC_FP128TyID: Code = bitc::TYPE_CODE_PPC_FP128; break;
    case Type::LabelTyID:     Code = bitc::TYPE_CODE_LABEL;     break;
    case Type::MetadataTyID:  Code = bitc::TYPE_CODE_METADATA;  break;
    case Type::X86_MMXTyID:   Code = bitc::TYPE_CODE_X86_MMX;   break;
    case Type::X86_AMXTyID:   Code = bitc::TYPE_CODE_X86_AMX;   break;
    case Type::TokenTyID:     Code = bitc::TYPE_CODE_TOKEN;     break;
    case Type::IntegerTyID:
      // INTEGER: [width]
      Code = bitc::TYPE_CODE_INTEGER;
      TypeVals.push_back(cast<IntegerType>(T)->getBitWidth());
      break;
    case Type::PointerTyID: {
      PointerType *PTy = cast<PointerType>(T);
      unsigned AddressSpace = PTy->getAddressSpace();
      // OPAQUE_POINTER: [address space]
      Code = bitc::TYPE_CODE_OPAQUE_POINTER;
      TypeVals.push_back(AddressSpace);
      if (AddressSpace == 0)
        AbbrevToUse = OpaquePtrAbbrev;
      break;
    }
    case Type::FunctionTyID: {
      FunctionType *FT = cast<FunctionType>(T);
      // FUNCTION: [isvararg, retty, paramty x N]
      Code = bitc::TYPE_CODE_FUNCTION;
      TypeVals.push_back(FT->isVarArg());
      TypeVals.push_back(VE.getTypeID(FT->getReturnType()));
      for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i)
        TypeVals.push_back(VE.getTypeID(FT->getParamType(i)));
      AbbrevToUse = FunctionAbbrev;
      break;
    }
    case Type::StructTyID: {
      StructType *ST = cast<StructType>(T);
      // STRUCT: [ispacked, eltty x N]
      TypeVals.push_back(ST->isPacked());
      // Output all of the element types.
      for (Type *ET : ST->elements())
        TypeVals.push_back(VE.getTypeID(ET));

      if (ST->isLiteral()) {
        Code = bitc::TYPE_CODE_STRUCT_ANON;
        AbbrevToUse = StructAnonAbbrev;
      } else {
        if (ST->isOpaque()) {
          Code = bitc::TYPE_CODE_OPAQUE;
        } else {
          Code = bitc::TYPE_CODE_STRUCT_NAMED;
          AbbrevToUse = StructNamedAbbrev;
        }

        // Emit the name if it is present.
        if (!ST->getName().empty())
          writeStringRecord(Stream, bitc::TYPE_CODE_STRUCT_NAME, ST->getName(),
                            StructNameAbbrev);
      }
      break;
    }
    case Type::ArrayTyID: {
      ArrayType *AT = cast<ArrayType>(T);
      // ARRAY: [numelts, eltty]
      Code = bitc::TYPE_CODE_ARRAY;
      TypeVals.push_back(AT->getNumElements());
      TypeVals.push_back(VE.getTypeID(AT->getElementType()));
      AbbrevToUse = ArrayAbbrev;
      break;
    }
    case Type::FixedVectorTyID:
    case Type::ScalableVectorTyID: {
      VectorType *VT = cast<VectorType>(T);
      // VECTOR [numelts, eltty] or
      //        [numelts, eltty, scalable]
      Code = bitc::TYPE_CODE_VECTOR;
      TypeVals.push_back(VT->getElementCount().getKnownMinValue());
      TypeVals.push_back(VE.getTypeID(VT->getElementType()));
      if (isa<ScalableVectorType>(VT))
        TypeVals.push_back(true);
      break;
    }
    case Type::TargetExtTyID: {
      TargetExtType *TET = cast<TargetExtType>(T);
      Code = bitc::TYPE_CODE_TARGET_TYPE;
      writeStringRecord(Stream, bitc::TYPE_CODE_STRUCT_NAME, TET->getName(),
                        StructNameAbbrev);
      TypeVals.push_back(TET->getNumTypeParameters());
      for (Type *InnerTy : TET->type_params())
        TypeVals.push_back(VE.getTypeID(InnerTy));
      for (unsigned IntParam : TET->int_params())
        TypeVals.push_back(IntParam);
      break;
    }
    case Type::TypedPointerTyID:
      llvm_unreachable("Typed pointers cannot be added to IR modules");
    }

    // Emit the finished record.
    Stream.EmitRecord(Code, TypeVals, AbbrevToUse);
    TypeVals.clear();
  }

  Stream.ExitBlock();
}

static unsigned getEncodedLinkage(const GlobalValue::LinkageTypes Linkage) {
  switch (Linkage) {
  case GlobalValue::ExternalLinkage:
    return 0;
  case GlobalValue::WeakAnyLinkage:
    return 16;
  case GlobalValue::AppendingLinkage:
    return 2;
  case GlobalValue::InternalLinkage:
    return 3;
  case GlobalValue::LinkOnceAnyLinkage:
    return 18;
  case GlobalValue::ExternalWeakLinkage:
    return 7;
  case GlobalValue::CommonLinkage:
    return 8;
  case GlobalValue::PrivateLinkage:
    return 9;
  case GlobalValue::WeakODRLinkage:
    return 17;
  case GlobalValue::LinkOnceODRLinkage:
    return 19;
  case GlobalValue::AvailableExternallyLinkage:
    return 12;
  }
  llvm_unreachable("Invalid linkage");
}

static unsigned getEncodedLinkage(const GlobalValue &GV) {
  return getEncodedLinkage(GV.getLinkage());
}

static uint64_t getEncodedFFlags(FunctionSummary::FFlags Flags) {
  uint64_t RawFlags = 0;
  RawFlags |= Flags.ReadNone;
  RawFlags |= (Flags.ReadOnly << 1);
  RawFlags |= (Flags.NoRecurse << 2);
  RawFlags |= (Flags.ReturnDoesNotAlias << 3);
  RawFlags |= (Flags.NoInline << 4);
  RawFlags |= (Flags.AlwaysInline << 5);
  RawFlags |= (Flags.NoUnwind << 6);
  RawFlags |= (Flags.MayThrow << 7);
  RawFlags |= (Flags.HasUnknownCall << 8);
  RawFlags |= (Flags.MustBeUnreachable << 9);
  return RawFlags;
}

// Decode the flags for GlobalValue in the summary. See getDecodedGVSummaryFlags
// in BitcodeReader.cpp.
static uint64_t getEncodedGVSummaryFlags(GlobalValueSummary::GVFlags Flags,
                                         bool ImportAsDecl = false) {
  uint64_t RawFlags = 0;

  RawFlags |= Flags.NotEligibleToImport; // bool
  RawFlags |= (Flags.Live << 1);
  RawFlags |= (Flags.DSOLocal << 2);
  RawFlags |= (Flags.CanAutoHide << 3);

  // Linkage don't need to be remapped at that time for the summary. Any future
  // change to the getEncodedLinkage() function will need to be taken into
  // account here as well.
  RawFlags = (RawFlags << 4) | Flags.Linkage; // 4 bits

  RawFlags |= (Flags.Visibility << 8); // 2 bits

  unsigned ImportType = Flags.ImportType | ImportAsDecl;
  RawFlags |= (ImportType << 10); // 1 bit

  return RawFlags;
}

static uint64_t getEncodedGVarFlags(GlobalVarSummary::GVarFlags Flags) {
  uint64_t RawFlags = Flags.MaybeReadOnly | (Flags.MaybeWriteOnly << 1) |
                      (Flags.Constant << 2) | Flags.VCallVisibility << 3;
  return RawFlags;
}

static uint64_t getEncodedHotnessCallEdgeInfo(const CalleeInfo &CI) {
  uint64_t RawFlags = 0;

  RawFlags |= CI.Hotness;            // 3 bits
  RawFlags |= (CI.HasTailCall << 3); // 1 bit

  return RawFlags;
}

static uint64_t getEncodedRelBFCallEdgeInfo(const CalleeInfo &CI) {
  uint64_t RawFlags = 0;

  RawFlags |= CI.RelBlockFreq; // CalleeInfo::RelBlockFreqBits bits
  RawFlags |= (CI.HasTailCall << CalleeInfo::RelBlockFreqBits); // 1 bit

  return RawFlags;
}

static unsigned getEncodedVisibility(const GlobalValue &GV) {
  switch (GV.getVisibility()) {
  case GlobalValue::DefaultVisibility:   return 0;
  case GlobalValue::HiddenVisibility:    return 1;
  case GlobalValue::ProtectedVisibility: return 2;
  }
  llvm_unreachable("Invalid visibility");
}

static unsigned getEncodedDLLStorageClass(const GlobalValue &GV) {
  switch (GV.getDLLStorageClass()) {
  case GlobalValue::DefaultStorageClass:   return 0;
  case GlobalValue::DLLImportStorageClass: return 1;
  case GlobalValue::DLLExportStorageClass: return 2;
  }
  llvm_unreachable("Invalid DLL storage class");
}

static unsigned getEncodedThreadLocalMode(const GlobalValue &GV) {
  switch (GV.getThreadLocalMode()) {
    case GlobalVariable::NotThreadLocal:         return 0;
    case GlobalVariable::GeneralDynamicTLSModel: return 1;
    case GlobalVariable::LocalDynamicTLSModel:   return 2;
    case GlobalVariable::InitialExecTLSModel:    return 3;
    case GlobalVariable::LocalExecTLSModel:      return 4;
  }
  llvm_unreachable("Invalid TLS model");
}

static unsigned getEncodedComdatSelectionKind(const Comdat &C) {
  switch (C.getSelectionKind()) {
  case Comdat::Any:
    return bitc::COMDAT_SELECTION_KIND_ANY;
  case Comdat::ExactMatch:
    return bitc::COMDAT_SELECTION_KIND_EXACT_MATCH;
  case Comdat::Largest:
    return bitc::COMDAT_SELECTION_KIND_LARGEST;
  case Comdat::NoDeduplicate:
    return bitc::COMDAT_SELECTION_KIND_NO_DUPLICATES;
  case Comdat::SameSize:
    return bitc::COMDAT_SELECTION_KIND_SAME_SIZE;
  }
  llvm_unreachable("Invalid selection kind");
}

static unsigned getEncodedUnnamedAddr(const GlobalValue &GV) {
  switch (GV.getUnnamedAddr()) {
  case GlobalValue::UnnamedAddr::None:   return 0;
  case GlobalValue::UnnamedAddr::Local:  return 2;
  case GlobalValue::UnnamedAddr::Global: return 1;
  }
  llvm_unreachable("Invalid unnamed_addr");
}

size_t ModuleBitcodeWriter::addToStrtab(StringRef Str) {
  if (GenerateHash)
    Hasher.update(Str);
  return StrtabBuilder.add(Str);
}

void ModuleBitcodeWriter::writeComdats() {
  SmallVector<unsigned, 64> Vals;
  for (const Comdat *C : VE.getComdats()) {
    // COMDAT: [strtab offset, strtab size, selection_kind]
    Vals.push_back(addToStrtab(C->getName()));
    Vals.push_back(C->getName().size());
    Vals.push_back(getEncodedComdatSelectionKind(*C));
    Stream.EmitRecord(bitc::MODULE_CODE_COMDAT, Vals, /*AbbrevToUse=*/0);
    Vals.clear();
  }
}

/// Write a record that will eventually hold the word offset of the
/// module-level VST. For now the offset is 0, which will be backpatched
/// after the real VST is written. Saves the bit offset to backpatch.
void ModuleBitcodeWriter::writeValueSymbolTableForwardDecl() {
  // Write a placeholder value in for the offset of the real VST,
  // which is written after the function blocks so that it can include
  // the offset of each function. The placeholder offset will be
  // updated when the real VST is written.
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::MODULE_CODE_VSTOFFSET));
  // Blocks are 32-bit aligned, so we can use a 32-bit word offset to
  // hold the real VST offset. Must use fixed instead of VBR as we don't
  // know how many VBR chunks to reserve ahead of time.
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  unsigned VSTOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Emit the placeholder
  uint64_t Vals[] = {bitc::MODULE_CODE_VSTOFFSET, 0};
  Stream.EmitRecordWithAbbrev(VSTOffsetAbbrev, Vals);

  // Compute and save the bit offset to the placeholder, which will be
  // patched when the real VST is written. We can simply subtract the 32-bit
  // fixed size from the current bit number to get the location to backpatch.
  VSTOffsetPlaceholder = Stream.GetCurrentBitNo() - 32;
}

enum StringEncoding { SE_Char6, SE_Fixed7, SE_Fixed8 };

/// Determine the encoding to use for the given string name and length.
static StringEncoding getStringEncoding(StringRef Str) {
  bool isChar6 = true;
  for (char C : Str) {
    if (isChar6)
      isChar6 = BitCodeAbbrevOp::isChar6(C);
    if ((unsigned char)C & 128)
      // don't bother scanning the rest.
      return SE_Fixed8;
  }
  if (isChar6)
    return SE_Char6;
  return SE_Fixed7;
}

static_assert(sizeof(GlobalValue::SanitizerMetadata) <= sizeof(unsigned),
              "Sanitizer Metadata is too large for naive serialization.");
static unsigned
serializeSanitizerMetadata(const GlobalValue::SanitizerMetadata &Meta) {
  return Meta.NoAddress | (Meta.NoHWAddress << 1) |
         (Meta.Memtag << 2) | (Meta.IsDynInit << 3);
}

/// Emit top-level description of module, including target triple, inline asm,
/// descriptors for global variables, and function prototype info.
/// Returns the bit offset to backpatch with the location of the real VST.
void ModuleBitcodeWriter::writeModuleInfo() {
  // Emit various pieces of data attached to a module.
  if (!M.getTargetTriple().empty())
    writeStringRecord(Stream, bitc::MODULE_CODE_TRIPLE, M.getTargetTriple(),
                      0 /*TODO*/);
  const std::string &DL = M.getDataLayoutStr();
  if (!DL.empty())
    writeStringRecord(Stream, bitc::MODULE_CODE_DATALAYOUT, DL, 0 /*TODO*/);
  if (!M.getModuleInlineAsm().empty())
    writeStringRecord(Stream, bitc::MODULE_CODE_ASM, M.getModuleInlineAsm(),
                      0 /*TODO*/);

  // Emit information about sections and GC, computing how many there are. Also
  // compute the maximum alignment value.
  std::map<std::string, unsigned> SectionMap;
  std::map<std::string, unsigned> GCMap;
  MaybeAlign MaxAlignment;
  unsigned MaxGlobalType = 0;
  const auto UpdateMaxAlignment = [&MaxAlignment](const MaybeAlign A) {
    if (A)
      MaxAlignment = !MaxAlignment ? *A : std::max(*MaxAlignment, *A);
  };
  for (const GlobalVariable &GV : M.globals()) {
    UpdateMaxAlignment(GV.getAlign());
    MaxGlobalType = std::max(MaxGlobalType, VE.getTypeID(GV.getValueType()));
    if (GV.hasSection()) {
      // Give section names unique ID's.
      unsigned &Entry = SectionMap[std::string(GV.getSection())];
      if (!Entry) {
        writeStringRecord(Stream, bitc::MODULE_CODE_SECTIONNAME, GV.getSection(),
                          0 /*TODO*/);
        Entry = SectionMap.size();
      }
    }
  }
  for (const Function &F : M) {
    UpdateMaxAlignment(F.getAlign());
    if (F.hasSection()) {
      // Give section names unique ID's.
      unsigned &Entry = SectionMap[std::string(F.getSection())];
      if (!Entry) {
        writeStringRecord(Stream, bitc::MODULE_CODE_SECTIONNAME, F.getSection(),
                          0 /*TODO*/);
        Entry = SectionMap.size();
      }
    }
    if (F.hasGC()) {
      // Same for GC names.
      unsigned &Entry = GCMap[F.getGC()];
      if (!Entry) {
        writeStringRecord(Stream, bitc::MODULE_CODE_GCNAME, F.getGC(),
                          0 /*TODO*/);
        Entry = GCMap.size();
      }
    }
  }

  // Emit abbrev for globals, now that we know # sections and max alignment.
  unsigned SimpleGVarAbbrev = 0;
  if (!M.global_empty()) {
    // Add an abbrev for common globals with no visibility or thread localness.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::MODULE_CODE_GLOBALVAR));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                              Log2_32_Ceil(MaxGlobalType+1)));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // AddrSpace << 2
                                                           //| explicitType << 1
                                                           //| constant
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Initializer.
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 5)); // Linkage.
    if (!MaxAlignment)                                     // Alignment.
      Abbv->Add(BitCodeAbbrevOp(0));
    else {
      unsigned MaxEncAlignment = getEncodedAlign(MaxAlignment);
      Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                               Log2_32_Ceil(MaxEncAlignment+1)));
    }
    if (SectionMap.empty())                                    // Section.
      Abbv->Add(BitCodeAbbrevOp(0));
    else
      Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                               Log2_32_Ceil(SectionMap.size()+1)));
    // Don't bother emitting vis + thread local.
    SimpleGVarAbbrev = Stream.EmitAbbrev(std::move(Abbv));
  }

  SmallVector<unsigned, 64> Vals;
  // Emit the module's source file name.
  {
    StringEncoding Bits = getStringEncoding(M.getSourceFileName());
    BitCodeAbbrevOp AbbrevOpToUse = BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8);
    if (Bits == SE_Char6)
      AbbrevOpToUse = BitCodeAbbrevOp(BitCodeAbbrevOp::Char6);
    else if (Bits == SE_Fixed7)
      AbbrevOpToUse = BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7);

    // MODULE_CODE_SOURCE_FILENAME: [namechar x N]
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::MODULE_CODE_SOURCE_FILENAME));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(AbbrevOpToUse);
    unsigned FilenameAbbrev = Stream.EmitAbbrev(std::move(Abbv));

    for (const auto P : M.getSourceFileName())
      Vals.push_back((unsigned char)P);

    // Emit the finished record.
    Stream.EmitRecord(bitc::MODULE_CODE_SOURCE_FILENAME, Vals, FilenameAbbrev);
    Vals.clear();
  }

  // Emit the global variable information.
  for (const GlobalVariable &GV : M.globals()) {
    unsigned AbbrevToUse = 0;

    // GLOBALVAR: [strtab offset, strtab size, type, isconst, initid,
    //             linkage, alignment, section, visibility, threadlocal,
    //             unnamed_addr, externally_initialized, dllstorageclass,
    //             comdat, attributes, DSO_Local, GlobalSanitizer, code_model]
    Vals.push_back(addToStrtab(GV.getName()));
    Vals.push_back(GV.getName().size());
    Vals.push_back(VE.getTypeID(GV.getValueType()));
    Vals.push_back(GV.getType()->getAddressSpace() << 2 | 2 | GV.isConstant());
    Vals.push_back(GV.isDeclaration() ? 0 :
                   (VE.getValueID(GV.getInitializer()) + 1));
    Vals.push_back(getEncodedLinkage(GV));
    Vals.push_back(getEncodedAlign(GV.getAlign()));
    Vals.push_back(GV.hasSection() ? SectionMap[std::string(GV.getSection())]
                                   : 0);
    if (GV.isThreadLocal() ||
        GV.getVisibility() != GlobalValue::DefaultVisibility ||
        GV.getUnnamedAddr() != GlobalValue::UnnamedAddr::None ||
        GV.isExternallyInitialized() ||
        GV.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
        GV.hasComdat() || GV.hasAttributes() || GV.isDSOLocal() ||
        GV.hasPartition() || GV.hasSanitizerMetadata() || GV.getCodeModel()) {
      Vals.push_back(getEncodedVisibility(GV));
      Vals.push_back(getEncodedThreadLocalMode(GV));
      Vals.push_back(getEncodedUnnamedAddr(GV));
      Vals.push_back(GV.isExternallyInitialized());
      Vals.push_back(getEncodedDLLStorageClass(GV));
      Vals.push_back(GV.hasComdat() ? VE.getComdatID(GV.getComdat()) : 0);

      auto AL = GV.getAttributesAsList(AttributeList::FunctionIndex);
      Vals.push_back(VE.getAttributeListID(AL));

      Vals.push_back(GV.isDSOLocal());
      Vals.push_back(addToStrtab(GV.getPartition()));
      Vals.push_back(GV.getPartition().size());

      Vals.push_back((GV.hasSanitizerMetadata() ? serializeSanitizerMetadata(
                                                      GV.getSanitizerMetadata())
                                                : 0));
      Vals.push_back(GV.getCodeModelRaw());
    } else {
      AbbrevToUse = SimpleGVarAbbrev;
    }

    Stream.EmitRecord(bitc::MODULE_CODE_GLOBALVAR, Vals, AbbrevToUse);
    Vals.clear();
  }

  // Emit the function proto information.
  for (const Function &F : M) {
    // FUNCTION:  [strtab offset, strtab size, type, callingconv, isproto,
    //             linkage, paramattrs, alignment, section, visibility, gc,
    //             unnamed_addr, prologuedata, dllstorageclass, comdat,
    //             prefixdata, personalityfn, DSO_Local, addrspace]
    Vals.push_back(addToStrtab(F.getName()));
    Vals.push_back(F.getName().size());
    Vals.push_back(VE.getTypeID(F.getFunctionType()));
    Vals.push_back(F.getCallingConv());
    Vals.push_back(F.isDeclaration());
    Vals.push_back(getEncodedLinkage(F));
    Vals.push_back(VE.getAttributeListID(F.getAttributes()));
    Vals.push_back(getEncodedAlign(F.getAlign()));
    Vals.push_back(F.hasSection() ? SectionMap[std::string(F.getSection())]
                                  : 0);
    Vals.push_back(getEncodedVisibility(F));
    Vals.push_back(F.hasGC() ? GCMap[F.getGC()] : 0);
    Vals.push_back(getEncodedUnnamedAddr(F));
    Vals.push_back(F.hasPrologueData() ? (VE.getValueID(F.getPrologueData()) + 1)
                                       : 0);
    Vals.push_back(getEncodedDLLStorageClass(F));
    Vals.push_back(F.hasComdat() ? VE.getComdatID(F.getComdat()) : 0);
    Vals.push_back(F.hasPrefixData() ? (VE.getValueID(F.getPrefixData()) + 1)
                                     : 0);
    Vals.push_back(
        F.hasPersonalityFn() ? (VE.getValueID(F.getPersonalityFn()) + 1) : 0);

    Vals.push_back(F.isDSOLocal());
    Vals.push_back(F.getAddressSpace());
    Vals.push_back(addToStrtab(F.getPartition()));
    Vals.push_back(F.getPartition().size());

    unsigned AbbrevToUse = 0;
    Stream.EmitRecord(bitc::MODULE_CODE_FUNCTION, Vals, AbbrevToUse);
    Vals.clear();
  }

  // Emit the alias information.
  for (const GlobalAlias &A : M.aliases()) {
    // ALIAS: [strtab offset, strtab size, alias type, aliasee val#, linkage,
    //         visibility, dllstorageclass, threadlocal, unnamed_addr,
    //         DSO_Local]
    Vals.push_back(addToStrtab(A.getName()));
    Vals.push_back(A.getName().size());
    Vals.push_back(VE.getTypeID(A.getValueType()));
    Vals.push_back(A.getType()->getAddressSpace());
    Vals.push_back(VE.getValueID(A.getAliasee()));
    Vals.push_back(getEncodedLinkage(A));
    Vals.push_back(getEncodedVisibility(A));
    Vals.push_back(getEncodedDLLStorageClass(A));
    Vals.push_back(getEncodedThreadLocalMode(A));
    Vals.push_back(getEncodedUnnamedAddr(A));
    Vals.push_back(A.isDSOLocal());
    Vals.push_back(addToStrtab(A.getPartition()));
    Vals.push_back(A.getPartition().size());

    unsigned AbbrevToUse = 0;
    Stream.EmitRecord(bitc::MODULE_CODE_ALIAS, Vals, AbbrevToUse);
    Vals.clear();
  }

  // Emit the ifunc information.
  for (const GlobalIFunc &I : M.ifuncs()) {
    // IFUNC: [strtab offset, strtab size, ifunc type, address space, resolver
    //         val#, linkage, visibility, DSO_Local]
    Vals.push_back(addToStrtab(I.getName()));
    Vals.push_back(I.getName().size());
    Vals.push_back(VE.getTypeID(I.getValueType()));
    Vals.push_back(I.getType()->getAddressSpace());
    Vals.push_back(VE.getValueID(I.getResolver()));
    Vals.push_back(getEncodedLinkage(I));
    Vals.push_back(getEncodedVisibility(I));
    Vals.push_back(I.isDSOLocal());
    Vals.push_back(addToStrtab(I.getPartition()));
    Vals.push_back(I.getPartition().size());
    Stream.EmitRecord(bitc::MODULE_CODE_IFUNC, Vals);
    Vals.clear();
  }

  writeValueSymbolTableForwardDecl();
}

static uint64_t getOptimizationFlags(const Value *V) {
  uint64_t Flags = 0;

  if (const auto *OBO = dyn_cast<OverflowingBinaryOperator>(V)) {
    if (OBO->hasNoSignedWrap())
      Flags |= 1 << bitc::OBO_NO_SIGNED_WRAP;
    if (OBO->hasNoUnsignedWrap())
      Flags |= 1 << bitc::OBO_NO_UNSIGNED_WRAP;
  } else if (const auto *PEO = dyn_cast<PossiblyExactOperator>(V)) {
    if (PEO->isExact())
      Flags |= 1 << bitc::PEO_EXACT;
  } else if (const auto *PDI = dyn_cast<PossiblyDisjointInst>(V)) {
    if (PDI->isDisjoint())
      Flags |= 1 << bitc::PDI_DISJOINT;
  } else if (const auto *FPMO = dyn_cast<FPMathOperator>(V)) {
    if (FPMO->hasAllowReassoc())
      Flags |= bitc::AllowReassoc;
    if (FPMO->hasNoNaNs())
      Flags |= bitc::NoNaNs;
    if (FPMO->hasNoInfs())
      Flags |= bitc::NoInfs;
    if (FPMO->hasNoSignedZeros())
      Flags |= bitc::NoSignedZeros;
    if (FPMO->hasAllowReciprocal())
      Flags |= bitc::AllowReciprocal;
    if (FPMO->hasAllowContract())
      Flags |= bitc::AllowContract;
    if (FPMO->hasApproxFunc())
      Flags |= bitc::ApproxFunc;
  } else if (const auto *NNI = dyn_cast<PossiblyNonNegInst>(V)) {
    if (NNI->hasNonNeg())
      Flags |= 1 << bitc::PNNI_NON_NEG;
  } else if (const auto *TI = dyn_cast<TruncInst>(V)) {
    if (TI->hasNoSignedWrap())
      Flags |= 1 << bitc::TIO_NO_SIGNED_WRAP;
    if (TI->hasNoUnsignedWrap())
      Flags |= 1 << bitc::TIO_NO_UNSIGNED_WRAP;
  } else if (const auto *GEP = dyn_cast<GEPOperator>(V)) {
    if (GEP->isInBounds())
      Flags |= 1 << bitc::GEP_INBOUNDS;
    if (GEP->hasNoUnsignedSignedWrap())
      Flags |= 1 << bitc::GEP_NUSW;
    if (GEP->hasNoUnsignedWrap())
      Flags |= 1 << bitc::GEP_NUW;
  }

  return Flags;
}

void ModuleBitcodeWriter::writeValueAsMetadata(
    const ValueAsMetadata *MD, SmallVectorImpl<uint64_t> &Record) {
  // Mimic an MDNode with a value as one operand.
  Value *V = MD->getValue();
  Record.push_back(VE.getTypeID(V->getType()));
  Record.push_back(VE.getValueID(V));
  Stream.EmitRecord(bitc::METADATA_VALUE, Record, 0);
  Record.clear();
}

void ModuleBitcodeWriter::writeMDTuple(const MDTuple *N,
                                       SmallVectorImpl<uint64_t> &Record,
                                       unsigned Abbrev) {
  for (const MDOperand &MDO : N->operands()) {
    Metadata *MD = MDO;
    assert(!(MD && isa<LocalAsMetadata>(MD)) &&
           "Unexpected function-local metadata");
    Record.push_back(VE.getMetadataOrNullID(MD));
  }
  Stream.EmitRecord(N->isDistinct() ? bitc::METADATA_DISTINCT_NODE
                                    : bitc::METADATA_NODE,
                    Record, Abbrev);
  Record.clear();
}

unsigned ModuleBitcodeWriter::createDILocationAbbrev() {
  // Assume the column is usually under 128, and always output the inlined-at
  // location (it's never more expensive than building an array size 1).
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_LOCATION));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  return Stream.EmitAbbrev(std::move(Abbv));
}

void ModuleBitcodeWriter::writeDILocation(const DILocation *N,
                                          SmallVectorImpl<uint64_t> &Record,
                                          unsigned &Abbrev) {
  if (!Abbrev)
    Abbrev = createDILocationAbbrev();

  Record.push_back(N->isDistinct());
  Record.push_back(N->getLine());
  Record.push_back(N->getColumn());
  Record.push_back(VE.getMetadataID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getInlinedAt()));
  Record.push_back(N->isImplicitCode());

  Stream.EmitRecord(bitc::METADATA_LOCATION, Record, Abbrev);
  Record.clear();
}

unsigned ModuleBitcodeWriter::createGenericDINodeAbbrev() {
  // Assume the column is usually under 128, and always output the inlined-at
  // location (it's never more expensive than building an array size 1).
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_GENERIC_DEBUG));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  return Stream.EmitAbbrev(std::move(Abbv));
}

void ModuleBitcodeWriter::writeGenericDINode(const GenericDINode *N,
                                             SmallVectorImpl<uint64_t> &Record,
                                             unsigned &Abbrev) {
  if (!Abbrev)
    Abbrev = createGenericDINodeAbbrev();

  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(0); // Per-tag version field; unused for now.

  for (auto &I : N->operands())
    Record.push_back(VE.getMetadataOrNullID(I));

  Stream.EmitRecord(bitc::METADATA_GENERIC_DEBUG, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDISubrange(const DISubrange *N,
                                          SmallVectorImpl<uint64_t> &Record,
                                          unsigned Abbrev) {
  const uint64_t Version = 2 << 1;
  Record.push_back((uint64_t)N->isDistinct() | Version);
  Record.push_back(VE.getMetadataOrNullID(N->getRawCountNode()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawLowerBound()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawUpperBound()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawStride()));

  Stream.EmitRecord(bitc::METADATA_SUBRANGE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIGenericSubrange(
    const DIGenericSubrange *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back((uint64_t)N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getRawCountNode()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawLowerBound()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawUpperBound()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawStride()));

  Stream.EmitRecord(bitc::METADATA_GENERIC_SUBRANGE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIEnumerator(const DIEnumerator *N,
                                            SmallVectorImpl<uint64_t> &Record,
                                            unsigned Abbrev) {
  const uint64_t IsBigInt = 1 << 2;
  Record.push_back(IsBigInt | (N->isUnsigned() << 1) | N->isDistinct());
  Record.push_back(N->getValue().getBitWidth());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  emitWideAPInt(Record, N->getValue());

  Stream.EmitRecord(bitc::METADATA_ENUMERATOR, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIBasicType(const DIBasicType *N,
                                           SmallVectorImpl<uint64_t> &Record,
                                           unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getEncoding());
  Record.push_back(N->getFlags());

  Stream.EmitRecord(bitc::METADATA_BASIC_TYPE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIStringType(const DIStringType *N,
                                            SmallVectorImpl<uint64_t> &Record,
                                            unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getStringLength()));
  Record.push_back(VE.getMetadataOrNullID(N->getStringLengthExp()));
  Record.push_back(VE.getMetadataOrNullID(N->getStringLocationExp()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getEncoding());

  Stream.EmitRecord(bitc::METADATA_STRING_TYPE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIDerivedType(const DIDerivedType *N,
                                             SmallVectorImpl<uint64_t> &Record,
                                             unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getBaseType()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getOffsetInBits());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getExtraData()));

  // DWARF address space is encoded as N->getDWARFAddressSpace() + 1. 0 means
  // that there is no DWARF address space associated with DIDerivedType.
  if (const auto &DWARFAddressSpace = N->getDWARFAddressSpace())
    Record.push_back(*DWARFAddressSpace + 1);
  else
    Record.push_back(0);

  Record.push_back(VE.getMetadataOrNullID(N->getAnnotations().get()));

  if (auto PtrAuthData = N->getPtrAuthData())
    Record.push_back(PtrAuthData->RawData);
  else
    Record.push_back(0);

  Stream.EmitRecord(bitc::METADATA_DERIVED_TYPE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDICompositeType(
    const DICompositeType *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  const unsigned IsNotUsedInOldTypeRef = 0x2;
  Record.push_back(IsNotUsedInOldTypeRef | (unsigned)N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getBaseType()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getOffsetInBits());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getElements().get()));
  Record.push_back(N->getRuntimeLang());
  Record.push_back(VE.getMetadataOrNullID(N->getVTableHolder()));
  Record.push_back(VE.getMetadataOrNullID(N->getTemplateParams().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawIdentifier()));
  Record.push_back(VE.getMetadataOrNullID(N->getDiscriminator()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawDataLocation()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawAssociated()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawAllocated()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawRank()));
  Record.push_back(VE.getMetadataOrNullID(N->getAnnotations().get()));

  Stream.EmitRecord(bitc::METADATA_COMPOSITE_TYPE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDISubroutineType(
    const DISubroutineType *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  const unsigned HasNoOldTypeRefs = 0x2;
  Record.push_back(HasNoOldTypeRefs | (unsigned)N->isDistinct());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getTypeArray().get()));
  Record.push_back(N->getCC());

  Stream.EmitRecord(bitc::METADATA_SUBROUTINE_TYPE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIFile(const DIFile *N,
                                      SmallVectorImpl<uint64_t> &Record,
                                      unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getRawFilename()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawDirectory()));
  if (N->getRawChecksum()) {
    Record.push_back(N->getRawChecksum()->Kind);
    Record.push_back(VE.getMetadataOrNullID(N->getRawChecksum()->Value));
  } else {
    // Maintain backwards compatibility with the old internal representation of
    // CSK_None in ChecksumKind by writing nulls here when Checksum is None.
    Record.push_back(0);
    Record.push_back(VE.getMetadataOrNullID(nullptr));
  }
  auto Source = N->getRawSource();
  if (Source)
    Record.push_back(VE.getMetadataOrNullID(Source));

  Stream.EmitRecord(bitc::METADATA_FILE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDICompileUnit(const DICompileUnit *N,
                                             SmallVectorImpl<uint64_t> &Record,
                                             unsigned Abbrev) {
  assert(N->isDistinct() && "Expected distinct compile units");
  Record.push_back(/* IsDistinct */ true);
  Record.push_back(N->getSourceLanguage());
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawProducer()));
  Record.push_back(N->isOptimized());
  Record.push_back(VE.getMetadataOrNullID(N->getRawFlags()));
  Record.push_back(N->getRuntimeVersion());
  Record.push_back(VE.getMetadataOrNullID(N->getRawSplitDebugFilename()));
  Record.push_back(N->getEmissionKind());
  Record.push_back(VE.getMetadataOrNullID(N->getEnumTypes().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getRetainedTypes().get()));
  Record.push_back(/* subprograms */ 0);
  Record.push_back(VE.getMetadataOrNullID(N->getGlobalVariables().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getImportedEntities().get()));
  Record.push_back(N->getDWOId());
  Record.push_back(VE.getMetadataOrNullID(N->getMacros().get()));
  Record.push_back(N->getSplitDebugInlining());
  Record.push_back(N->getDebugInfoForProfiling());
  Record.push_back((unsigned)N->getNameTableKind());
  Record.push_back(N->getRangesBaseAddress());
  Record.push_back(VE.getMetadataOrNullID(N->getRawSysRoot()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawSDK()));

  Stream.EmitRecord(bitc::METADATA_COMPILE_UNIT, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDISubprogram(const DISubprogram *N,
                                            SmallVectorImpl<uint64_t> &Record,
                                            unsigned Abbrev) {
  const uint64_t HasUnitFlag = 1 << 1;
  const uint64_t HasSPFlagsFlag = 1 << 2;
  Record.push_back(uint64_t(N->isDistinct()) | HasUnitFlag | HasSPFlagsFlag);
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawLinkageName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->getScopeLine());
  Record.push_back(VE.getMetadataOrNullID(N->getContainingType()));
  Record.push_back(N->getSPFlags());
  Record.push_back(N->getVirtualIndex());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getRawUnit()));
  Record.push_back(VE.getMetadataOrNullID(N->getTemplateParams().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getDeclaration()));
  Record.push_back(VE.getMetadataOrNullID(N->getRetainedNodes().get()));
  Record.push_back(N->getThisAdjustment());
  Record.push_back(VE.getMetadataOrNullID(N->getThrownTypes().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getAnnotations().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawTargetFuncName()));

  Stream.EmitRecord(bitc::METADATA_SUBPROGRAM, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDILexicalBlock(const DILexicalBlock *N,
                                              SmallVectorImpl<uint64_t> &Record,
                                              unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(N->getColumn());

  Stream.EmitRecord(bitc::METADATA_LEXICAL_BLOCK, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDILexicalBlockFile(
    const DILexicalBlockFile *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getDiscriminator());

  Stream.EmitRecord(bitc::METADATA_LEXICAL_BLOCK_FILE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDICommonBlock(const DICommonBlock *N,
                                             SmallVectorImpl<uint64_t> &Record,
                                             unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getDecl()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLineNo());

  Stream.EmitRecord(bitc::METADATA_COMMON_BLOCK, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDINamespace(const DINamespace *N,
                                           SmallVectorImpl<uint64_t> &Record,
                                           unsigned Abbrev) {
  Record.push_back(N->isDistinct() | N->getExportSymbols() << 1);
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));

  Stream.EmitRecord(bitc::METADATA_NAMESPACE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIMacro(const DIMacro *N,
                                       SmallVectorImpl<uint64_t> &Record,
                                       unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getMacinfoType());
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawValue()));

  Stream.EmitRecord(bitc::METADATA_MACRO, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIMacroFile(const DIMacroFile *N,
                                           SmallVectorImpl<uint64_t> &Record,
                                           unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getMacinfoType());
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(VE.getMetadataOrNullID(N->getElements().get()));

  Stream.EmitRecord(bitc::METADATA_MACRO_FILE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIArgList(const DIArgList *N,
                                         SmallVectorImpl<uint64_t> &Record) {
  Record.reserve(N->getArgs().size());
  for (ValueAsMetadata *MD : N->getArgs())
    Record.push_back(VE.getMetadataID(MD));

  Stream.EmitRecord(bitc::METADATA_ARG_LIST, Record);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIModule(const DIModule *N,
                                        SmallVectorImpl<uint64_t> &Record,
                                        unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  for (auto &I : N->operands())
    Record.push_back(VE.getMetadataOrNullID(I));
  Record.push_back(N->getLineNo());
  Record.push_back(N->getIsDecl());

  Stream.EmitRecord(bitc::METADATA_MODULE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIAssignID(const DIAssignID *N,
                                          SmallVectorImpl<uint64_t> &Record,
                                          unsigned Abbrev) {
  // There are no arguments for this metadata type.
  Record.push_back(N->isDistinct());
  Stream.EmitRecord(bitc::METADATA_ASSIGN_ID, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDITemplateTypeParameter(
    const DITemplateTypeParameter *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->isDefault());

  Stream.EmitRecord(bitc::METADATA_TEMPLATE_TYPE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDITemplateValueParameter(
    const DITemplateValueParameter *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->isDefault());
  Record.push_back(VE.getMetadataOrNullID(N->getValue()));

  Stream.EmitRecord(bitc::METADATA_TEMPLATE_VALUE, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIGlobalVariable(
    const DIGlobalVariable *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  const uint64_t Version = 2 << 1;
  Record.push_back((uint64_t)N->isDistinct() | Version);
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawLinkageName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->isLocalToUnit());
  Record.push_back(N->isDefinition());
  Record.push_back(VE.getMetadataOrNullID(N->getStaticDataMemberDeclaration()));
  Record.push_back(VE.getMetadataOrNullID(N->getTemplateParams()));
  Record.push_back(N->getAlignInBits());
  Record.push_back(VE.getMetadataOrNullID(N->getAnnotations().get()));

  Stream.EmitRecord(bitc::METADATA_GLOBAL_VAR, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDILocalVariable(
    const DILocalVariable *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  // In order to support all possible bitcode formats in BitcodeReader we need
  // to distinguish the following cases:
  // 1) Record has no artificial tag (Record[1]),
  //   has no obsolete inlinedAt field (Record[9]).
  //   In this case Record size will be 8, HasAlignment flag is false.
  // 2) Record has artificial tag (Record[1]),
  //   has no obsolete inlignedAt field (Record[9]).
  //   In this case Record size will be 9, HasAlignment flag is false.
  // 3) Record has both artificial tag (Record[1]) and
  //   obsolete inlignedAt field (Record[9]).
  //   In this case Record size will be 10, HasAlignment flag is false.
  // 4) Record has neither artificial tag, nor inlignedAt field, but
  //   HasAlignment flag is true and Record[8] contains alignment value.
  const uint64_t HasAlignmentFlag = 1 << 1;
  Record.push_back((uint64_t)N->isDistinct() | HasAlignmentFlag);
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->getArg());
  Record.push_back(N->getFlags());
  Record.push_back(N->getAlignInBits());
  Record.push_back(VE.getMetadataOrNullID(N->getAnnotations().get()));

  Stream.EmitRecord(bitc::METADATA_LOCAL_VAR, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDILabel(
    const DILabel *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back((uint64_t)N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());

  Stream.EmitRecord(bitc::METADATA_LABEL, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIExpression(const DIExpression *N,
                                            SmallVectorImpl<uint64_t> &Record,
                                            unsigned Abbrev) {
  Record.reserve(N->getElements().size() + 1);
  const uint64_t Version = 3 << 1;
  Record.push_back((uint64_t)N->isDistinct() | Version);
  Record.append(N->elements_begin(), N->elements_end());

  Stream.EmitRecord(bitc::METADATA_EXPRESSION, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIGlobalVariableExpression(
    const DIGlobalVariableExpression *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getVariable()));
  Record.push_back(VE.getMetadataOrNullID(N->getExpression()));

  Stream.EmitRecord(bitc::METADATA_GLOBAL_VAR_EXPR, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIObjCProperty(const DIObjCProperty *N,
                                              SmallVectorImpl<uint64_t> &Record,
                                              unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getRawSetterName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawGetterName()));
  Record.push_back(N->getAttributes());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));

  Stream.EmitRecord(bitc::METADATA_OBJC_PROPERTY, Record, Abbrev);
  Record.clear();
}

void ModuleBitcodeWriter::writeDIImportedEntity(
    const DIImportedEntity *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getEntity()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawFile()));
  Record.push_back(VE.getMetadataOrNullID(N->getElements().get()));

  Stream.EmitRecord(bitc::METADATA_IMPORTED_ENTITY, Record, Abbrev);
  Record.clear();
}

unsigned ModuleBitcodeWriter::createNamedMetadataAbbrev() {
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_NAME));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
  return Stream.EmitAbbrev(std::move(Abbv));
}

void ModuleBitcodeWriter::writeNamedMetadata(
    SmallVectorImpl<uint64_t> &Record) {
  if (M.named_metadata_empty())
    return;

  unsigned Abbrev = createNamedMetadataAbbrev();
  for (const NamedMDNode &NMD : M.named_metadata()) {
    // Write name.
    StringRef Str = NMD.getName();
    Record.append(Str.bytes_begin(), Str.bytes_end());
    Stream.EmitRecord(bitc::METADATA_NAME, Record, Abbrev);
    Record.clear();

    // Write named metadata operands.
    for (const MDNode *N : NMD.operands())
      Record.push_back(VE.getMetadataID(N));
    Stream.EmitRecord(bitc::METADATA_NAMED_NODE, Record, 0);
    Record.clear();
  }
}

unsigned ModuleBitcodeWriter::createMetadataStringsAbbrev() {
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_STRINGS));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # of strings
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // offset to chars
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  return Stream.EmitAbbrev(std::move(Abbv));
}

/// Write out a record for MDString.
///
/// All the metadata strings in a metadata block are emitted in a single
/// record.  The sizes and strings themselves are shoved into a blob.
void ModuleBitcodeWriter::writeMetadataStrings(
    ArrayRef<const Metadata *> Strings, SmallVectorImpl<uint64_t> &Record) {
  if (Strings.empty())
    return;

  // Start the record with the number of strings.
  Record.push_back(bitc::METADATA_STRINGS);
  Record.push_back(Strings.size());

  // Emit the sizes of the strings in the blob.
  SmallString<256> Blob;
  {
    BitstreamWriter W(Blob);
    for (const Metadata *MD : Strings)
      W.EmitVBR(cast<MDString>(MD)->getLength(), 6);
    W.FlushToWord();
  }

  // Add the offset to the strings to the record.
  Record.push_back(Blob.size());

  // Add the strings to the blob.
  for (const Metadata *MD : Strings)
    Blob.append(cast<MDString>(MD)->getString());

  // Emit the final record.
  Stream.EmitRecordWithBlob(createMetadataStringsAbbrev(), Record, Blob);
  Record.clear();
}

// Generates an enum to use as an index in the Abbrev array of Metadata record.
enum MetadataAbbrev : unsigned {
#define HANDLE_MDNODE_LEAF(CLASS) CLASS##AbbrevID,
#include "llvm/IR/Metadata.def"
  LastPlusOne
};

void ModuleBitcodeWriter::writeMetadataRecords(
    ArrayRef<const Metadata *> MDs, SmallVectorImpl<uint64_t> &Record,
    std::vector<unsigned> *MDAbbrevs, std::vector<uint64_t> *IndexPos) {
  if (MDs.empty())
    return;

  // Initialize MDNode abbreviations.
#define HANDLE_MDNODE_LEAF(CLASS) unsigned CLASS##Abbrev = 0;
#include "llvm/IR/Metadata.def"

  for (const Metadata *MD : MDs) {
    if (IndexPos)
      IndexPos->push_back(Stream.GetCurrentBitNo());
    if (const MDNode *N = dyn_cast<MDNode>(MD)) {
      assert(N->isResolved() && "Expected forward references to be resolved");

      switch (N->getMetadataID()) {
      default:
        llvm_unreachable("Invalid MDNode subclass");
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case Metadata::CLASS##Kind:                                                  \
    if (MDAbbrevs)                                                             \
      write##CLASS(cast<CLASS>(N), Record,                                     \
                   (*MDAbbrevs)[MetadataAbbrev::CLASS##AbbrevID]);             \
    else                                                                       \
      write##CLASS(cast<CLASS>(N), Record, CLASS##Abbrev);                     \
    continue;
#include "llvm/IR/Metadata.def"
      }
    }
    if (auto *AL = dyn_cast<DIArgList>(MD)) {
      writeDIArgList(AL, Record);
      continue;
    }
    writeValueAsMetadata(cast<ValueAsMetadata>(MD), Record);
  }
}

void ModuleBitcodeWriter::writeModuleMetadata() {
  if (!VE.hasMDs() && M.named_metadata_empty())
    return;

  Stream.EnterSubblock(bitc::METADATA_BLOCK_ID, 4);
  SmallVector<uint64_t, 64> Record;

  // Emit all abbrevs upfront, so that the reader can jump in the middle of the
  // block and load any metadata.
  std::vector<unsigned> MDAbbrevs;

  MDAbbrevs.resize(MetadataAbbrev::LastPlusOne);
  MDAbbrevs[MetadataAbbrev::DILocationAbbrevID] = createDILocationAbbrev();
  MDAbbrevs[MetadataAbbrev::GenericDINodeAbbrevID] =
      createGenericDINodeAbbrev();

  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_INDEX_OFFSET));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  unsigned OffsetAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_INDEX));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  unsigned IndexAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Emit MDStrings together upfront.
  writeMetadataStrings(VE.getMDStrings(), Record);

  // We only emit an index for the metadata record if we have more than a given
  // (naive) threshold of metadatas, otherwise it is not worth it.
  if (VE.getNonMDStrings().size() > IndexThreshold) {
    // Write a placeholder value in for the offset of the metadata index,
    // which is written after the records, so that it can include
    // the offset of each entry. The placeholder offset will be
    // updated after all records are emitted.
    uint64_t Vals[] = {0, 0};
    Stream.EmitRecord(bitc::METADATA_INDEX_OFFSET, Vals, OffsetAbbrev);
  }

  // Compute and save the bit offset to the current position, which will be
  // patched when we emit the index later. We can simply subtract the 64-bit
  // fixed size from the current bit number to get the location to backpatch.
  uint64_t IndexOffsetRecordBitPos = Stream.GetCurrentBitNo();

  // This index will contain the bitpos for each individual record.
  std::vector<uint64_t> IndexPos;
  IndexPos.reserve(VE.getNonMDStrings().size());

  // Write all the records
  writeMetadataRecords(VE.getNonMDStrings(), Record, &MDAbbrevs, &IndexPos);

  if (VE.getNonMDStrings().size() > IndexThreshold) {
    // Now that we have emitted all the records we will emit the index. But
    // first
    // backpatch the forward reference so that the reader can skip the records
    // efficiently.
    Stream.BackpatchWord64(IndexOffsetRecordBitPos - 64,
                           Stream.GetCurrentBitNo() - IndexOffsetRecordBitPos);

    // Delta encode the index.
    uint64_t PreviousValue = IndexOffsetRecordBitPos;
    for (auto &Elt : IndexPos) {
      auto EltDelta = Elt - PreviousValue;
      PreviousValue = Elt;
      Elt = EltDelta;
    }
    // Emit the index record.
    Stream.EmitRecord(bitc::METADATA_INDEX, IndexPos, IndexAbbrev);
    IndexPos.clear();
  }

  // Write the named metadata now.
  writeNamedMetadata(Record);

  auto AddDeclAttachedMetadata = [&](const GlobalObject &GO) {
    SmallVector<uint64_t, 4> Record;
    Record.push_back(VE.getValueID(&GO));
    pushGlobalMetadataAttachment(Record, GO);
    Stream.EmitRecord(bitc::METADATA_GLOBAL_DECL_ATTACHMENT, Record);
  };
  for (const Function &F : M)
    if (F.isDeclaration() && F.hasMetadata())
      AddDeclAttachedMetadata(F);
  // FIXME: Only store metadata for declarations here, and move data for global
  // variable definitions to a separate block (PR28134).
  for (const GlobalVariable &GV : M.globals())
    if (GV.hasMetadata())
      AddDeclAttachedMetadata(GV);

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeFunctionMetadata(const Function &F) {
  if (!VE.hasMDs())
    return;

  Stream.EnterSubblock(bitc::METADATA_BLOCK_ID, 3);
  SmallVector<uint64_t, 64> Record;
  writeMetadataStrings(VE.getMDStrings(), Record);
  writeMetadataRecords(VE.getNonMDStrings(), Record);
  Stream.ExitBlock();
}

void ModuleBitcodeWriter::pushGlobalMetadataAttachment(
    SmallVectorImpl<uint64_t> &Record, const GlobalObject &GO) {
  // [n x [id, mdnode]]
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  GO.getAllMetadata(MDs);
  for (const auto &I : MDs) {
    Record.push_back(I.first);
    Record.push_back(VE.getMetadataID(I.second));
  }
}

void ModuleBitcodeWriter::writeFunctionMetadataAttachment(const Function &F) {
  Stream.EnterSubblock(bitc::METADATA_ATTACHMENT_ID, 3);

  SmallVector<uint64_t, 64> Record;

  if (F.hasMetadata()) {
    pushGlobalMetadataAttachment(Record, F);
    Stream.EmitRecord(bitc::METADATA_ATTACHMENT, Record, 0);
    Record.clear();
  }

  // Write metadata attachments
  // METADATA_ATTACHMENT - [m x [value, [n x [id, mdnode]]]
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB) {
      MDs.clear();
      I.getAllMetadataOtherThanDebugLoc(MDs);

      // If no metadata, ignore instruction.
      if (MDs.empty()) continue;

      Record.push_back(VE.getInstructionID(&I));

      for (unsigned i = 0, e = MDs.size(); i != e; ++i) {
        Record.push_back(MDs[i].first);
        Record.push_back(VE.getMetadataID(MDs[i].second));
      }
      Stream.EmitRecord(bitc::METADATA_ATTACHMENT, Record, 0);
      Record.clear();
    }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeModuleMetadataKinds() {
  SmallVector<uint64_t, 64> Record;

  // Write metadata kinds
  // METADATA_KIND - [n x [id, name]]
  SmallVector<StringRef, 8> Names;
  M.getMDKindNames(Names);

  if (Names.empty()) return;

  Stream.EnterSubblock(bitc::METADATA_KIND_BLOCK_ID, 3);

  for (unsigned MDKindID = 0, e = Names.size(); MDKindID != e; ++MDKindID) {
    Record.push_back(MDKindID);
    StringRef KName = Names[MDKindID];
    Record.append(KName.begin(), KName.end());

    Stream.EmitRecord(bitc::METADATA_KIND, Record, 0);
    Record.clear();
  }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeOperandBundleTags() {
  // Write metadata kinds
  //
  // OPERAND_BUNDLE_TAGS_BLOCK_ID : N x OPERAND_BUNDLE_TAG
  //
  // OPERAND_BUNDLE_TAG - [strchr x N]

  SmallVector<StringRef, 8> Tags;
  M.getOperandBundleTags(Tags);

  if (Tags.empty())
    return;

  Stream.EnterSubblock(bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID, 3);

  SmallVector<uint64_t, 64> Record;

  for (auto Tag : Tags) {
    Record.append(Tag.begin(), Tag.end());

    Stream.EmitRecord(bitc::OPERAND_BUNDLE_TAG, Record, 0);
    Record.clear();
  }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeSyncScopeNames() {
  SmallVector<StringRef, 8> SSNs;
  M.getContext().getSyncScopeNames(SSNs);
  if (SSNs.empty())
    return;

  Stream.EnterSubblock(bitc::SYNC_SCOPE_NAMES_BLOCK_ID, 2);

  SmallVector<uint64_t, 64> Record;
  for (auto SSN : SSNs) {
    Record.append(SSN.begin(), SSN.end());
    Stream.EmitRecord(bitc::SYNC_SCOPE_NAME, Record, 0);
    Record.clear();
  }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeConstants(unsigned FirstVal, unsigned LastVal,
                                         bool isGlobal) {
  if (FirstVal == LastVal) return;

  Stream.EnterSubblock(bitc::CONSTANTS_BLOCK_ID, 4);

  unsigned AggregateAbbrev = 0;
  unsigned String8Abbrev = 0;
  unsigned CString7Abbrev = 0;
  unsigned CString6Abbrev = 0;
  // If this is a constant pool for the module, emit module-specific abbrevs.
  if (isGlobal) {
    // Abbrev for CST_CODE_AGGREGATE.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_AGGREGATE));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, Log2_32_Ceil(LastVal+1)));
    AggregateAbbrev = Stream.EmitAbbrev(std::move(Abbv));

    // Abbrev for CST_CODE_STRING.
    Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_STRING));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
    String8Abbrev = Stream.EmitAbbrev(std::move(Abbv));
    // Abbrev for CST_CODE_CSTRING.
    Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_CSTRING));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7));
    CString7Abbrev = Stream.EmitAbbrev(std::move(Abbv));
    // Abbrev for CST_CODE_CSTRING.
    Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_CSTRING));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
    CString6Abbrev = Stream.EmitAbbrev(std::move(Abbv));
  }

  SmallVector<uint64_t, 64> Record;

  const ValueEnumerator::ValueList &Vals = VE.getValues();
  Type *LastTy = nullptr;
  for (unsigned i = FirstVal; i != LastVal; ++i) {
    const Value *V = Vals[i].first;
    // If we need to switch types, do so now.
    if (V->getType() != LastTy) {
      LastTy = V->getType();
      Record.push_back(VE.getTypeID(LastTy));
      Stream.EmitRecord(bitc::CST_CODE_SETTYPE, Record,
                        CONSTANTS_SETTYPE_ABBREV);
      Record.clear();
    }

    if (const InlineAsm *IA = dyn_cast<InlineAsm>(V)) {
      Record.push_back(VE.getTypeID(IA->getFunctionType()));
      Record.push_back(
          unsigned(IA->hasSideEffects()) | unsigned(IA->isAlignStack()) << 1 |
          unsigned(IA->getDialect() & 1) << 2 | unsigned(IA->canThrow()) << 3);

      // Add the asm string.
      const std::string &AsmStr = IA->getAsmString();
      Record.push_back(AsmStr.size());
      Record.append(AsmStr.begin(), AsmStr.end());

      // Add the constraint string.
      const std::string &ConstraintStr = IA->getConstraintString();
      Record.push_back(ConstraintStr.size());
      Record.append(ConstraintStr.begin(), ConstraintStr.end());
      Stream.EmitRecord(bitc::CST_CODE_INLINEASM, Record);
      Record.clear();
      continue;
    }
    const Constant *C = cast<Constant>(V);
    unsigned Code = -1U;
    unsigned AbbrevToUse = 0;
    if (C->isNullValue()) {
      Code = bitc::CST_CODE_NULL;
    } else if (isa<PoisonValue>(C)) {
      Code = bitc::CST_CODE_POISON;
    } else if (isa<UndefValue>(C)) {
      Code = bitc::CST_CODE_UNDEF;
    } else if (const ConstantInt *IV = dyn_cast<ConstantInt>(C)) {
      if (IV->getBitWidth() <= 64) {
        uint64_t V = IV->getSExtValue();
        emitSignedInt64(Record, V);
        Code = bitc::CST_CODE_INTEGER;
        AbbrevToUse = CONSTANTS_INTEGER_ABBREV;
      } else {                             // Wide integers, > 64 bits in size.
        emitWideAPInt(Record, IV->getValue());
        Code = bitc::CST_CODE_WIDE_INTEGER;
      }
    } else if (const ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
      Code = bitc::CST_CODE_FLOAT;
      Type *Ty = CFP->getType()->getScalarType();
      if (Ty->isHalfTy() || Ty->isBFloatTy() || Ty->isFloatTy() ||
          Ty->isDoubleTy()) {
        Record.push_back(CFP->getValueAPF().bitcastToAPInt().getZExtValue());
      } else if (Ty->isX86_FP80Ty()) {
        // api needed to prevent premature destruction
        // bits are not in the same order as a normal i80 APInt, compensate.
        APInt api = CFP->getValueAPF().bitcastToAPInt();
        const uint64_t *p = api.getRawData();
        Record.push_back((p[1] << 48) | (p[0] >> 16));
        Record.push_back(p[0] & 0xffffLL);
      } else if (Ty->isFP128Ty() || Ty->isPPC_FP128Ty()) {
        APInt api = CFP->getValueAPF().bitcastToAPInt();
        const uint64_t *p = api.getRawData();
        Record.push_back(p[0]);
        Record.push_back(p[1]);
      } else {
        assert(0 && "Unknown FP type!");
      }
    } else if (isa<ConstantDataSequential>(C) &&
               cast<ConstantDataSequential>(C)->isString()) {
      const ConstantDataSequential *Str = cast<ConstantDataSequential>(C);
      // Emit constant strings specially.
      unsigned NumElts = Str->getNumElements();
      // If this is a null-terminated string, use the denser CSTRING encoding.
      if (Str->isCString()) {
        Code = bitc::CST_CODE_CSTRING;
        --NumElts;  // Don't encode the null, which isn't allowed by char6.
      } else {
        Code = bitc::CST_CODE_STRING;
        AbbrevToUse = String8Abbrev;
      }
      bool isCStr7 = Code == bitc::CST_CODE_CSTRING;
      bool isCStrChar6 = Code == bitc::CST_CODE_CSTRING;
      for (unsigned i = 0; i != NumElts; ++i) {
        unsigned char V = Str->getElementAsInteger(i);
        Record.push_back(V);
        isCStr7 &= (V & 128) == 0;
        if (isCStrChar6)
          isCStrChar6 = BitCodeAbbrevOp::isChar6(V);
      }

      if (isCStrChar6)
        AbbrevToUse = CString6Abbrev;
      else if (isCStr7)
        AbbrevToUse = CString7Abbrev;
    } else if (const ConstantDataSequential *CDS =
                  dyn_cast<ConstantDataSequential>(C)) {
      Code = bitc::CST_CODE_DATA;
      Type *EltTy = CDS->getElementType();
      if (isa<IntegerType>(EltTy)) {
        for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i)
          Record.push_back(CDS->getElementAsInteger(i));
      } else {
        for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i)
          Record.push_back(
              CDS->getElementAsAPFloat(i).bitcastToAPInt().getLimitedValue());
      }
    } else if (isa<ConstantAggregate>(C)) {
      Code = bitc::CST_CODE_AGGREGATE;
      for (const Value *Op : C->operands())
        Record.push_back(VE.getValueID(Op));
      AbbrevToUse = AggregateAbbrev;
    } else if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
      switch (CE->getOpcode()) {
      default:
        if (Instruction::isCast(CE->getOpcode())) {
          Code = bitc::CST_CODE_CE_CAST;
          Record.push_back(getEncodedCastOpcode(CE->getOpcode()));
          Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
          Record.push_back(VE.getValueID(C->getOperand(0)));
          AbbrevToUse = CONSTANTS_CE_CAST_Abbrev;
        } else {
          assert(CE->getNumOperands() == 2 && "Unknown constant expr!");
          Code = bitc::CST_CODE_CE_BINOP;
          Record.push_back(getEncodedBinaryOpcode(CE->getOpcode()));
          Record.push_back(VE.getValueID(C->getOperand(0)));
          Record.push_back(VE.getValueID(C->getOperand(1)));
          uint64_t Flags = getOptimizationFlags(CE);
          if (Flags != 0)
            Record.push_back(Flags);
        }
        break;
      case Instruction::FNeg: {
        assert(CE->getNumOperands() == 1 && "Unknown constant expr!");
        Code = bitc::CST_CODE_CE_UNOP;
        Record.push_back(getEncodedUnaryOpcode(CE->getOpcode()));
        Record.push_back(VE.getValueID(C->getOperand(0)));
        uint64_t Flags = getOptimizationFlags(CE);
        if (Flags != 0)
          Record.push_back(Flags);
        break;
      }
      case Instruction::GetElementPtr: {
        Code = bitc::CST_CODE_CE_GEP;
        const auto *GO = cast<GEPOperator>(C);
        Record.push_back(VE.getTypeID(GO->getSourceElementType()));
        Record.push_back(getOptimizationFlags(GO));
        if (std::optional<ConstantRange> Range = GO->getInRange()) {
          Code = bitc::CST_CODE_CE_GEP_WITH_INRANGE;
          emitConstantRange(Record, *Range, /*EmitBitWidth=*/true);
        }
        for (unsigned i = 0, e = CE->getNumOperands(); i != e; ++i) {
          Record.push_back(VE.getTypeID(C->getOperand(i)->getType()));
          Record.push_back(VE.getValueID(C->getOperand(i)));
        }
        break;
      }
      case Instruction::ExtractElement:
        Code = bitc::CST_CODE_CE_EXTRACTELT;
        Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getTypeID(C->getOperand(1)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        break;
      case Instruction::InsertElement:
        Code = bitc::CST_CODE_CE_INSERTELT;
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        Record.push_back(VE.getTypeID(C->getOperand(2)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(2)));
        break;
      case Instruction::ShuffleVector:
        // If the return type and argument types are the same, this is a
        // standard shufflevector instruction.  If the types are different,
        // then the shuffle is widening or truncating the input vectors, and
        // the argument type must also be encoded.
        if (C->getType() == C->getOperand(0)->getType()) {
          Code = bitc::CST_CODE_CE_SHUFFLEVEC;
        } else {
          Code = bitc::CST_CODE_CE_SHUFVEC_EX;
          Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
        }
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        Record.push_back(VE.getValueID(CE->getShuffleMaskForBitcode()));
        break;
      }
    } else if (const BlockAddress *BA = dyn_cast<BlockAddress>(C)) {
      Code = bitc::CST_CODE_BLOCKADDRESS;
      Record.push_back(VE.getTypeID(BA->getFunction()->getType()));
      Record.push_back(VE.getValueID(BA->getFunction()));
      Record.push_back(VE.getGlobalBasicBlockID(BA->getBasicBlock()));
    } else if (const auto *Equiv = dyn_cast<DSOLocalEquivalent>(C)) {
      Code = bitc::CST_CODE_DSO_LOCAL_EQUIVALENT;
      Record.push_back(VE.getTypeID(Equiv->getGlobalValue()->getType()));
      Record.push_back(VE.getValueID(Equiv->getGlobalValue()));
    } else if (const auto *NC = dyn_cast<NoCFIValue>(C)) {
      Code = bitc::CST_CODE_NO_CFI_VALUE;
      Record.push_back(VE.getTypeID(NC->getGlobalValue()->getType()));
      Record.push_back(VE.getValueID(NC->getGlobalValue()));
    } else if (const auto *CPA = dyn_cast<ConstantPtrAuth>(C)) {
      Code = bitc::CST_CODE_PTRAUTH;
      Record.push_back(VE.getValueID(CPA->getPointer()));
      Record.push_back(VE.getValueID(CPA->getKey()));
      Record.push_back(VE.getValueID(CPA->getDiscriminator()));
      Record.push_back(VE.getValueID(CPA->getAddrDiscriminator()));
    } else {
#ifndef NDEBUG
      C->dump();
#endif
      llvm_unreachable("Unknown constant!");
    }
    Stream.EmitRecord(Code, Record, AbbrevToUse);
    Record.clear();
  }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeModuleConstants() {
  const ValueEnumerator::ValueList &Vals = VE.getValues();

  // Find the first constant to emit, which is the first non-globalvalue value.
  // We know globalvalues have been emitted by WriteModuleInfo.
  for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
    if (!isa<GlobalValue>(Vals[i].first)) {
      writeConstants(i, Vals.size(), true);
      return;
    }
  }
}

/// pushValueAndType - The file has to encode both the value and type id for
/// many values, because we need to know what type to create for forward
/// references.  However, most operands are not forward references, so this type
/// field is not needed.
///
/// This function adds V's value ID to Vals.  If the value ID is higher than the
/// instruction ID, then it is a forward reference, and it also includes the
/// type ID.  The value ID that is written is encoded relative to the InstID.
bool ModuleBitcodeWriter::pushValueAndType(const Value *V, unsigned InstID,
                                           SmallVectorImpl<unsigned> &Vals) {
  unsigned ValID = VE.getValueID(V);
  // Make encoding relative to the InstID.
  Vals.push_back(InstID - ValID);
  if (ValID >= InstID) {
    Vals.push_back(VE.getTypeID(V->getType()));
    return true;
  }
  return false;
}

void ModuleBitcodeWriter::writeOperandBundles(const CallBase &CS,
                                              unsigned InstID) {
  SmallVector<unsigned, 64> Record;
  LLVMContext &C = CS.getContext();

  for (unsigned i = 0, e = CS.getNumOperandBundles(); i != e; ++i) {
    const auto &Bundle = CS.getOperandBundleAt(i);
    Record.push_back(C.getOperandBundleTagID(Bundle.getTagName()));

    for (auto &Input : Bundle.Inputs)
      pushValueAndType(Input, InstID, Record);

    Stream.EmitRecord(bitc::FUNC_CODE_OPERAND_BUNDLE, Record);
    Record.clear();
  }
}

/// pushValue - Like pushValueAndType, but where the type of the value is
/// omitted (perhaps it was already encoded in an earlier operand).
void ModuleBitcodeWriter::pushValue(const Value *V, unsigned InstID,
                                    SmallVectorImpl<unsigned> &Vals) {
  unsigned ValID = VE.getValueID(V);
  Vals.push_back(InstID - ValID);
}

void ModuleBitcodeWriter::pushValueSigned(const Value *V, unsigned InstID,
                                          SmallVectorImpl<uint64_t> &Vals) {
  unsigned ValID = VE.getValueID(V);
  int64_t diff = ((int32_t)InstID - (int32_t)ValID);
  emitSignedInt64(Vals, diff);
}

/// WriteInstruction - Emit an instruction to the specified stream.
void ModuleBitcodeWriter::writeInstruction(const Instruction &I,
                                           unsigned InstID,
                                           SmallVectorImpl<unsigned> &Vals) {
  unsigned Code = 0;
  unsigned AbbrevToUse = 0;
  VE.setInstructionID(&I);
  switch (I.getOpcode()) {
  default:
    if (Instruction::isCast(I.getOpcode())) {
      Code = bitc::FUNC_CODE_INST_CAST;
      if (!pushValueAndType(I.getOperand(0), InstID, Vals))
        AbbrevToUse = FUNCTION_INST_CAST_ABBREV;
      Vals.push_back(VE.getTypeID(I.getType()));
      Vals.push_back(getEncodedCastOpcode(I.getOpcode()));
      uint64_t Flags = getOptimizationFlags(&I);
      if (Flags != 0) {
        if (AbbrevToUse == FUNCTION_INST_CAST_ABBREV)
          AbbrevToUse = FUNCTION_INST_CAST_FLAGS_ABBREV;
        Vals.push_back(Flags);
      }
    } else {
      assert(isa<BinaryOperator>(I) && "Unknown instruction!");
      Code = bitc::FUNC_CODE_INST_BINOP;
      if (!pushValueAndType(I.getOperand(0), InstID, Vals))
        AbbrevToUse = FUNCTION_INST_BINOP_ABBREV;
      pushValue(I.getOperand(1), InstID, Vals);
      Vals.push_back(getEncodedBinaryOpcode(I.getOpcode()));
      uint64_t Flags = getOptimizationFlags(&I);
      if (Flags != 0) {
        if (AbbrevToUse == FUNCTION_INST_BINOP_ABBREV)
          AbbrevToUse = FUNCTION_INST_BINOP_FLAGS_ABBREV;
        Vals.push_back(Flags);
      }
    }
    break;
  case Instruction::FNeg: {
    Code = bitc::FUNC_CODE_INST_UNOP;
    if (!pushValueAndType(I.getOperand(0), InstID, Vals))
      AbbrevToUse = FUNCTION_INST_UNOP_ABBREV;
    Vals.push_back(getEncodedUnaryOpcode(I.getOpcode()));
    uint64_t Flags = getOptimizationFlags(&I);
    if (Flags != 0) {
      if (AbbrevToUse == FUNCTION_INST_UNOP_ABBREV)
        AbbrevToUse = FUNCTION_INST_UNOP_FLAGS_ABBREV;
      Vals.push_back(Flags);
    }
    break;
  }
  case Instruction::GetElementPtr: {
    Code = bitc::FUNC_CODE_INST_GEP;
    AbbrevToUse = FUNCTION_INST_GEP_ABBREV;
    auto &GEPInst = cast<GetElementPtrInst>(I);
    Vals.push_back(getOptimizationFlags(&I));
    Vals.push_back(VE.getTypeID(GEPInst.getSourceElementType()));
    for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i)
      pushValueAndType(I.getOperand(i), InstID, Vals);
    break;
  }
  case Instruction::ExtractValue: {
    Code = bitc::FUNC_CODE_INST_EXTRACTVAL;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    const ExtractValueInst *EVI = cast<ExtractValueInst>(&I);
    Vals.append(EVI->idx_begin(), EVI->idx_end());
    break;
  }
  case Instruction::InsertValue: {
    Code = bitc::FUNC_CODE_INST_INSERTVAL;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValueAndType(I.getOperand(1), InstID, Vals);
    const InsertValueInst *IVI = cast<InsertValueInst>(&I);
    Vals.append(IVI->idx_begin(), IVI->idx_end());
    break;
  }
  case Instruction::Select: {
    Code = bitc::FUNC_CODE_INST_VSELECT;
    pushValueAndType(I.getOperand(1), InstID, Vals);
    pushValue(I.getOperand(2), InstID, Vals);
    pushValueAndType(I.getOperand(0), InstID, Vals);
    uint64_t Flags = getOptimizationFlags(&I);
    if (Flags != 0)
      Vals.push_back(Flags);
    break;
  }
  case Instruction::ExtractElement:
    Code = bitc::FUNC_CODE_INST_EXTRACTELT;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValueAndType(I.getOperand(1), InstID, Vals);
    break;
  case Instruction::InsertElement:
    Code = bitc::FUNC_CODE_INST_INSERTELT;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValue(I.getOperand(1), InstID, Vals);
    pushValueAndType(I.getOperand(2), InstID, Vals);
    break;
  case Instruction::ShuffleVector:
    Code = bitc::FUNC_CODE_INST_SHUFFLEVEC;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValue(I.getOperand(1), InstID, Vals);
    pushValue(cast<ShuffleVectorInst>(I).getShuffleMaskForBitcode(), InstID,
              Vals);
    break;
  case Instruction::ICmp:
  case Instruction::FCmp: {
    // compare returning Int1Ty or vector of Int1Ty
    Code = bitc::FUNC_CODE_INST_CMP2;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValue(I.getOperand(1), InstID, Vals);
    Vals.push_back(cast<CmpInst>(I).getPredicate());
    uint64_t Flags = getOptimizationFlags(&I);
    if (Flags != 0)
      Vals.push_back(Flags);
    break;
  }

  case Instruction::Ret:
    {
      Code = bitc::FUNC_CODE_INST_RET;
      unsigned NumOperands = I.getNumOperands();
      if (NumOperands == 0)
        AbbrevToUse = FUNCTION_INST_RET_VOID_ABBREV;
      else if (NumOperands == 1) {
        if (!pushValueAndType(I.getOperand(0), InstID, Vals))
          AbbrevToUse = FUNCTION_INST_RET_VAL_ABBREV;
      } else {
        for (unsigned i = 0, e = NumOperands; i != e; ++i)
          pushValueAndType(I.getOperand(i), InstID, Vals);
      }
    }
    break;
  case Instruction::Br:
    {
      Code = bitc::FUNC_CODE_INST_BR;
      const BranchInst &II = cast<BranchInst>(I);
      Vals.push_back(VE.getValueID(II.getSuccessor(0)));
      if (II.isConditional()) {
        Vals.push_back(VE.getValueID(II.getSuccessor(1)));
        pushValue(II.getCondition(), InstID, Vals);
      }
    }
    break;
  case Instruction::Switch:
    {
      Code = bitc::FUNC_CODE_INST_SWITCH;
      const SwitchInst &SI = cast<SwitchInst>(I);
      Vals.push_back(VE.getTypeID(SI.getCondition()->getType()));
      pushValue(SI.getCondition(), InstID, Vals);
      Vals.push_back(VE.getValueID(SI.getDefaultDest()));
      for (auto Case : SI.cases()) {
        Vals.push_back(VE.getValueID(Case.getCaseValue()));
        Vals.push_back(VE.getValueID(Case.getCaseSuccessor()));
      }
    }
    break;
  case Instruction::IndirectBr:
    Code = bitc::FUNC_CODE_INST_INDIRECTBR;
    Vals.push_back(VE.getTypeID(I.getOperand(0)->getType()));
    // Encode the address operand as relative, but not the basic blocks.
    pushValue(I.getOperand(0), InstID, Vals);
    for (unsigned i = 1, e = I.getNumOperands(); i != e; ++i)
      Vals.push_back(VE.getValueID(I.getOperand(i)));
    break;

  case Instruction::Invoke: {
    const InvokeInst *II = cast<InvokeInst>(&I);
    const Value *Callee = II->getCalledOperand();
    FunctionType *FTy = II->getFunctionType();

    if (II->hasOperandBundles())
      writeOperandBundles(*II, InstID);

    Code = bitc::FUNC_CODE_INST_INVOKE;

    Vals.push_back(VE.getAttributeListID(II->getAttributes()));
    Vals.push_back(II->getCallingConv() | 1 << 13);
    Vals.push_back(VE.getValueID(II->getNormalDest()));
    Vals.push_back(VE.getValueID(II->getUnwindDest()));
    Vals.push_back(VE.getTypeID(FTy));
    pushValueAndType(Callee, InstID, Vals);

    // Emit value #'s for the fixed parameters.
    for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
      pushValue(I.getOperand(i), InstID, Vals); // fixed param.

    // Emit type/value pairs for varargs params.
    if (FTy->isVarArg()) {
      for (unsigned i = FTy->getNumParams(), e = II->arg_size(); i != e; ++i)
        pushValueAndType(I.getOperand(i), InstID, Vals); // vararg
    }
    break;
  }
  case Instruction::Resume:
    Code = bitc::FUNC_CODE_INST_RESUME;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    break;
  case Instruction::CleanupRet: {
    Code = bitc::FUNC_CODE_INST_CLEANUPRET;
    const auto &CRI = cast<CleanupReturnInst>(I);
    pushValue(CRI.getCleanupPad(), InstID, Vals);
    if (CRI.hasUnwindDest())
      Vals.push_back(VE.getValueID(CRI.getUnwindDest()));
    break;
  }
  case Instruction::CatchRet: {
    Code = bitc::FUNC_CODE_INST_CATCHRET;
    const auto &CRI = cast<CatchReturnInst>(I);
    pushValue(CRI.getCatchPad(), InstID, Vals);
    Vals.push_back(VE.getValueID(CRI.getSuccessor()));
    break;
  }
  case Instruction::CleanupPad:
  case Instruction::CatchPad: {
    const auto &FuncletPad = cast<FuncletPadInst>(I);
    Code = isa<CatchPadInst>(FuncletPad) ? bitc::FUNC_CODE_INST_CATCHPAD
                                         : bitc::FUNC_CODE_INST_CLEANUPPAD;
    pushValue(FuncletPad.getParentPad(), InstID, Vals);

    unsigned NumArgOperands = FuncletPad.arg_size();
    Vals.push_back(NumArgOperands);
    for (unsigned Op = 0; Op != NumArgOperands; ++Op)
      pushValueAndType(FuncletPad.getArgOperand(Op), InstID, Vals);
    break;
  }
  case Instruction::CatchSwitch: {
    Code = bitc::FUNC_CODE_INST_CATCHSWITCH;
    const auto &CatchSwitch = cast<CatchSwitchInst>(I);

    pushValue(CatchSwitch.getParentPad(), InstID, Vals);

    unsigned NumHandlers = CatchSwitch.getNumHandlers();
    Vals.push_back(NumHandlers);
    for (const BasicBlock *CatchPadBB : CatchSwitch.handlers())
      Vals.push_back(VE.getValueID(CatchPadBB));

    if (CatchSwitch.hasUnwindDest())
      Vals.push_back(VE.getValueID(CatchSwitch.getUnwindDest()));
    break;
  }
  case Instruction::CallBr: {
    const CallBrInst *CBI = cast<CallBrInst>(&I);
    const Value *Callee = CBI->getCalledOperand();
    FunctionType *FTy = CBI->getFunctionType();

    if (CBI->hasOperandBundles())
      writeOperandBundles(*CBI, InstID);

    Code = bitc::FUNC_CODE_INST_CALLBR;

    Vals.push_back(VE.getAttributeListID(CBI->getAttributes()));

    Vals.push_back(CBI->getCallingConv() << bitc::CALL_CCONV |
                   1 << bitc::CALL_EXPLICIT_TYPE);

    Vals.push_back(VE.getValueID(CBI->getDefaultDest()));
    Vals.push_back(CBI->getNumIndirectDests());
    for (unsigned i = 0, e = CBI->getNumIndirectDests(); i != e; ++i)
      Vals.push_back(VE.getValueID(CBI->getIndirectDest(i)));

    Vals.push_back(VE.getTypeID(FTy));
    pushValueAndType(Callee, InstID, Vals);

    // Emit value #'s for the fixed parameters.
    for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
      pushValue(I.getOperand(i), InstID, Vals); // fixed param.

    // Emit type/value pairs for varargs params.
    if (FTy->isVarArg()) {
      for (unsigned i = FTy->getNumParams(), e = CBI->arg_size(); i != e; ++i)
        pushValueAndType(I.getOperand(i), InstID, Vals); // vararg
    }
    break;
  }
  case Instruction::Unreachable:
    Code = bitc::FUNC_CODE_INST_UNREACHABLE;
    AbbrevToUse = FUNCTION_INST_UNREACHABLE_ABBREV;
    break;

  case Instruction::PHI: {
    const PHINode &PN = cast<PHINode>(I);
    Code = bitc::FUNC_CODE_INST_PHI;
    // With the newer instruction encoding, forward references could give
    // negative valued IDs.  This is most common for PHIs, so we use
    // signed VBRs.
    SmallVector<uint64_t, 128> Vals64;
    Vals64.push_back(VE.getTypeID(PN.getType()));
    for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i) {
      pushValueSigned(PN.getIncomingValue(i), InstID, Vals64);
      Vals64.push_back(VE.getValueID(PN.getIncomingBlock(i)));
    }

    uint64_t Flags = getOptimizationFlags(&I);
    if (Flags != 0)
      Vals64.push_back(Flags);

    // Emit a Vals64 vector and exit.
    Stream.EmitRecord(Code, Vals64, AbbrevToUse);
    Vals64.clear();
    return;
  }

  case Instruction::LandingPad: {
    const LandingPadInst &LP = cast<LandingPadInst>(I);
    Code = bitc::FUNC_CODE_INST_LANDINGPAD;
    Vals.push_back(VE.getTypeID(LP.getType()));
    Vals.push_back(LP.isCleanup());
    Vals.push_back(LP.getNumClauses());
    for (unsigned I = 0, E = LP.getNumClauses(); I != E; ++I) {
      if (LP.isCatch(I))
        Vals.push_back(LandingPadInst::Catch);
      else
        Vals.push_back(LandingPadInst::Filter);
      pushValueAndType(LP.getClause(I), InstID, Vals);
    }
    break;
  }

  case Instruction::Alloca: {
    Code = bitc::FUNC_CODE_INST_ALLOCA;
    const AllocaInst &AI = cast<AllocaInst>(I);
    Vals.push_back(VE.getTypeID(AI.getAllocatedType()));
    Vals.push_back(VE.getTypeID(I.getOperand(0)->getType()));
    Vals.push_back(VE.getValueID(I.getOperand(0))); // size.
    using APV = AllocaPackedValues;
    unsigned Record = 0;
    unsigned EncodedAlign = getEncodedAlign(AI.getAlign());
    Bitfield::set<APV::AlignLower>(
        Record, EncodedAlign & ((1 << APV::AlignLower::Bits) - 1));
    Bitfield::set<APV::AlignUpper>(Record,
                                   EncodedAlign >> APV::AlignLower::Bits);
    Bitfield::set<APV::UsedWithInAlloca>(Record, AI.isUsedWithInAlloca());
    Bitfield::set<APV::ExplicitType>(Record, true);
    Bitfield::set<APV::SwiftError>(Record, AI.isSwiftError());
    Vals.push_back(Record);

    unsigned AS = AI.getAddressSpace();
    if (AS != M.getDataLayout().getAllocaAddrSpace())
      Vals.push_back(AS);
    break;
  }

  case Instruction::Load:
    if (cast<LoadInst>(I).isAtomic()) {
      Code = bitc::FUNC_CODE_INST_LOADATOMIC;
      pushValueAndType(I.getOperand(0), InstID, Vals);
    } else {
      Code = bitc::FUNC_CODE_INST_LOAD;
      if (!pushValueAndType(I.getOperand(0), InstID, Vals)) // ptr
        AbbrevToUse = FUNCTION_INST_LOAD_ABBREV;
    }
    Vals.push_back(VE.getTypeID(I.getType()));
    Vals.push_back(getEncodedAlign(cast<LoadInst>(I).getAlign()));
    Vals.push_back(cast<LoadInst>(I).isVolatile());
    if (cast<LoadInst>(I).isAtomic()) {
      Vals.push_back(getEncodedOrdering(cast<LoadInst>(I).getOrdering()));
      Vals.push_back(getEncodedSyncScopeID(cast<LoadInst>(I).getSyncScopeID()));
    }
    break;
  case Instruction::Store:
    if (cast<StoreInst>(I).isAtomic())
      Code = bitc::FUNC_CODE_INST_STOREATOMIC;
    else
      Code = bitc::FUNC_CODE_INST_STORE;
    pushValueAndType(I.getOperand(1), InstID, Vals); // ptrty + ptr
    pushValueAndType(I.getOperand(0), InstID, Vals); // valty + val
    Vals.push_back(getEncodedAlign(cast<StoreInst>(I).getAlign()));
    Vals.push_back(cast<StoreInst>(I).isVolatile());
    if (cast<StoreInst>(I).isAtomic()) {
      Vals.push_back(getEncodedOrdering(cast<StoreInst>(I).getOrdering()));
      Vals.push_back(
          getEncodedSyncScopeID(cast<StoreInst>(I).getSyncScopeID()));
    }
    break;
  case Instruction::AtomicCmpXchg:
    Code = bitc::FUNC_CODE_INST_CMPXCHG;
    pushValueAndType(I.getOperand(0), InstID, Vals); // ptrty + ptr
    pushValueAndType(I.getOperand(1), InstID, Vals); // cmp.
    pushValue(I.getOperand(2), InstID, Vals);        // newval.
    Vals.push_back(cast<AtomicCmpXchgInst>(I).isVolatile());
    Vals.push_back(
        getEncodedOrdering(cast<AtomicCmpXchgInst>(I).getSuccessOrdering()));
    Vals.push_back(
        getEncodedSyncScopeID(cast<AtomicCmpXchgInst>(I).getSyncScopeID()));
    Vals.push_back(
        getEncodedOrdering(cast<AtomicCmpXchgInst>(I).getFailureOrdering()));
    Vals.push_back(cast<AtomicCmpXchgInst>(I).isWeak());
    Vals.push_back(getEncodedAlign(cast<AtomicCmpXchgInst>(I).getAlign()));
    break;
  case Instruction::AtomicRMW:
    Code = bitc::FUNC_CODE_INST_ATOMICRMW;
    pushValueAndType(I.getOperand(0), InstID, Vals); // ptrty + ptr
    pushValueAndType(I.getOperand(1), InstID, Vals); // valty + val
    Vals.push_back(
        getEncodedRMWOperation(cast<AtomicRMWInst>(I).getOperation()));
    Vals.push_back(cast<AtomicRMWInst>(I).isVolatile());
    Vals.push_back(getEncodedOrdering(cast<AtomicRMWInst>(I).getOrdering()));
    Vals.push_back(
        getEncodedSyncScopeID(cast<AtomicRMWInst>(I).getSyncScopeID()));
    Vals.push_back(getEncodedAlign(cast<AtomicRMWInst>(I).getAlign()));
    break;
  case Instruction::Fence:
    Code = bitc::FUNC_CODE_INST_FENCE;
    Vals.push_back(getEncodedOrdering(cast<FenceInst>(I).getOrdering()));
    Vals.push_back(getEncodedSyncScopeID(cast<FenceInst>(I).getSyncScopeID()));
    break;
  case Instruction::Call: {
    const CallInst &CI = cast<CallInst>(I);
    FunctionType *FTy = CI.getFunctionType();

    if (CI.hasOperandBundles())
      writeOperandBundles(CI, InstID);

    Code = bitc::FUNC_CODE_INST_CALL;

    Vals.push_back(VE.getAttributeListID(CI.getAttributes()));

    unsigned Flags = getOptimizationFlags(&I);
    Vals.push_back(CI.getCallingConv() << bitc::CALL_CCONV |
                   unsigned(CI.isTailCall()) << bitc::CALL_TAIL |
                   unsigned(CI.isMustTailCall()) << bitc::CALL_MUSTTAIL |
                   1 << bitc::CALL_EXPLICIT_TYPE |
                   unsigned(CI.isNoTailCall()) << bitc::CALL_NOTAIL |
                   unsigned(Flags != 0) << bitc::CALL_FMF);
    if (Flags != 0)
      Vals.push_back(Flags);

    Vals.push_back(VE.getTypeID(FTy));
    pushValueAndType(CI.getCalledOperand(), InstID, Vals); // Callee

    // Emit value #'s for the fixed parameters.
    for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i) {
      // Check for labels (can happen with asm labels).
      if (FTy->getParamType(i)->isLabelTy())
        Vals.push_back(VE.getValueID(CI.getArgOperand(i)));
      else
        pushValue(CI.getArgOperand(i), InstID, Vals); // fixed param.
    }

    // Emit type/value pairs for varargs params.
    if (FTy->isVarArg()) {
      for (unsigned i = FTy->getNumParams(), e = CI.arg_size(); i != e; ++i)
        pushValueAndType(CI.getArgOperand(i), InstID, Vals); // varargs
    }
    break;
  }
  case Instruction::VAArg:
    Code = bitc::FUNC_CODE_INST_VAARG;
    Vals.push_back(VE.getTypeID(I.getOperand(0)->getType()));   // valistty
    pushValue(I.getOperand(0), InstID, Vals);                   // valist.
    Vals.push_back(VE.getTypeID(I.getType())); // restype.
    break;
  case Instruction::Freeze:
    Code = bitc::FUNC_CODE_INST_FREEZE;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    break;
  }

  Stream.EmitRecord(Code, Vals, AbbrevToUse);
  Vals.clear();
}

/// Write a GlobalValue VST to the module. The purpose of this data structure is
/// to allow clients to efficiently find the function body.
void ModuleBitcodeWriter::writeGlobalValueSymbolTable(
  DenseMap<const Function *, uint64_t> &FunctionToBitcodeIndex) {
  // Get the offset of the VST we are writing, and backpatch it into
  // the VST forward declaration record.
  uint64_t VSTOffset = Stream.GetCurrentBitNo();
  // The BitcodeStartBit was the stream offset of the identification block.
  VSTOffset -= bitcodeStartBit();
  assert((VSTOffset & 31) == 0 && "VST block not 32-bit aligned");
  // Note that we add 1 here because the offset is relative to one word
  // before the start of the identification block, which was historically
  // always the start of the regular bitcode header.
  Stream.BackpatchWord(VSTOffsetPlaceholder, VSTOffset / 32 + 1);

  Stream.EnterSubblock(bitc::VALUE_SYMTAB_BLOCK_ID, 4);

  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_FNENTRY));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // value id
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // funcoffset
  unsigned FnEntryAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  for (const Function &F : M) {
    uint64_t Record[2];

    if (F.isDeclaration())
      continue;

    Record[0] = VE.getValueID(&F);

    // Save the word offset of the function (from the start of the
    // actual bitcode written to the stream).
    uint64_t BitcodeIndex = FunctionToBitcodeIndex[&F] - bitcodeStartBit();
    assert((BitcodeIndex & 31) == 0 && "function block not 32-bit aligned");
    // Note that we add 1 here because the offset is relative to one word
    // before the start of the identification block, which was historically
    // always the start of the regular bitcode header.
    Record[1] = BitcodeIndex / 32 + 1;

    Stream.EmitRecord(bitc::VST_CODE_FNENTRY, Record, FnEntryAbbrev);
  }

  Stream.ExitBlock();
}

/// Emit names for arguments, instructions and basic blocks in a function.
void ModuleBitcodeWriter::writeFunctionLevelValueSymbolTable(
    const ValueSymbolTable &VST) {
  if (VST.empty())
    return;

  Stream.EnterSubblock(bitc::VALUE_SYMTAB_BLOCK_ID, 4);

  // FIXME: Set up the abbrev, we know how many values there are!
  // FIXME: We know if the type names can use 7-bit ascii.
  SmallVector<uint64_t, 64> NameVals;

  for (const ValueName &Name : VST) {
    // Figure out the encoding to use for the name.
    StringEncoding Bits = getStringEncoding(Name.getKey());

    unsigned AbbrevToUse = VST_ENTRY_8_ABBREV;
    NameVals.push_back(VE.getValueID(Name.getValue()));

    // VST_CODE_ENTRY:   [valueid, namechar x N]
    // VST_CODE_BBENTRY: [bbid, namechar x N]
    unsigned Code;
    if (isa<BasicBlock>(Name.getValue())) {
      Code = bitc::VST_CODE_BBENTRY;
      if (Bits == SE_Char6)
        AbbrevToUse = VST_BBENTRY_6_ABBREV;
    } else {
      Code = bitc::VST_CODE_ENTRY;
      if (Bits == SE_Char6)
        AbbrevToUse = VST_ENTRY_6_ABBREV;
      else if (Bits == SE_Fixed7)
        AbbrevToUse = VST_ENTRY_7_ABBREV;
    }

    for (const auto P : Name.getKey())
      NameVals.push_back((unsigned char)P);

    // Emit the finished record.
    Stream.EmitRecord(Code, NameVals, AbbrevToUse);
    NameVals.clear();
  }

  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeUseList(UseListOrder &&Order) {
  assert(Order.Shuffle.size() >= 2 && "Shuffle too small");
  unsigned Code;
  if (isa<BasicBlock>(Order.V))
    Code = bitc::USELIST_CODE_BB;
  else
    Code = bitc::USELIST_CODE_DEFAULT;

  SmallVector<uint64_t, 64> Record(Order.Shuffle.begin(), Order.Shuffle.end());
  Record.push_back(VE.getValueID(Order.V));
  Stream.EmitRecord(Code, Record);
}

void ModuleBitcodeWriter::writeUseListBlock(const Function *F) {
  assert(VE.shouldPreserveUseListOrder() &&
         "Expected to be preserving use-list order");

  auto hasMore = [&]() {
    return !VE.UseListOrders.empty() && VE.UseListOrders.back().F == F;
  };
  if (!hasMore())
    // Nothing to do.
    return;

  Stream.EnterSubblock(bitc::USELIST_BLOCK_ID, 3);
  while (hasMore()) {
    writeUseList(std::move(VE.UseListOrders.back()));
    VE.UseListOrders.pop_back();
  }
  Stream.ExitBlock();
}

/// Emit a function body to the module stream.
void ModuleBitcodeWriter::writeFunction(
    const Function &F,
    DenseMap<const Function *, uint64_t> &FunctionToBitcodeIndex) {
  // Save the bitcode index of the start of this function block for recording
  // in the VST.
  FunctionToBitcodeIndex[&F] = Stream.GetCurrentBitNo();

  Stream.EnterSubblock(bitc::FUNCTION_BLOCK_ID, 4);
  VE.incorporateFunction(F);

  SmallVector<unsigned, 64> Vals;

  // Emit the number of basic blocks, so the reader can create them ahead of
  // time.
  Vals.push_back(VE.getBasicBlocks().size());
  Stream.EmitRecord(bitc::FUNC_CODE_DECLAREBLOCKS, Vals);
  Vals.clear();

  // If there are function-local constants, emit them now.
  unsigned CstStart, CstEnd;
  VE.getFunctionConstantRange(CstStart, CstEnd);
  writeConstants(CstStart, CstEnd, false);

  // If there is function-local metadata, emit it now.
  writeFunctionMetadata(F);

  // Keep a running idea of what the instruction ID is.
  unsigned InstID = CstEnd;

  bool NeedsMetadataAttachment = F.hasMetadata();

  DILocation *LastDL = nullptr;
  SmallSetVector<Function *, 4> BlockAddressUsers;

  // Finally, emit all the instructions, in order.
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      writeInstruction(I, InstID, Vals);

      if (!I.getType()->isVoidTy())
        ++InstID;

      // If the instruction has metadata, write a metadata attachment later.
      NeedsMetadataAttachment |= I.hasMetadataOtherThanDebugLoc();

      // If the instruction has a debug location, emit it.
      if (DILocation *DL = I.getDebugLoc()) {
        if (DL == LastDL) {
          // Just repeat the same debug loc as last time.
          Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_LOC_AGAIN, Vals);
        } else {
          Vals.push_back(DL->getLine());
          Vals.push_back(DL->getColumn());
          Vals.push_back(VE.getMetadataOrNullID(DL->getScope()));
          Vals.push_back(VE.getMetadataOrNullID(DL->getInlinedAt()));
          Vals.push_back(DL->isImplicitCode());
          Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_LOC, Vals);
          Vals.clear();
          LastDL = DL;
        }
      }

      // If the instruction has DbgRecords attached to it, emit them. Note that
      // they come after the instruction so that it's easy to attach them again
      // when reading the bitcode, even though conceptually the debug locations
      // start "before" the instruction.
      if (I.hasDbgRecords() && WriteNewDbgInfoFormatToBitcode) {
        /// Try to push the value only (unwrapped), otherwise push the
        /// metadata wrapped value. Returns true if the value was pushed
        /// without the ValueAsMetadata wrapper.
        auto PushValueOrMetadata = [&Vals, InstID,
                                    this](Metadata *RawLocation) {
          assert(RawLocation &&
                 "RawLocation unexpectedly null in DbgVariableRecord");
          if (ValueAsMetadata *VAM = dyn_cast<ValueAsMetadata>(RawLocation)) {
            SmallVector<unsigned, 2> ValAndType;
            // If the value is a fwd-ref the type is also pushed. We don't
            // want the type, so fwd-refs are kept wrapped (pushValueAndType
            // returns false if the value is pushed without type).
            if (!pushValueAndType(VAM->getValue(), InstID, ValAndType)) {
              Vals.push_back(ValAndType[0]);
              return true;
            }
          }
          // The metadata is a DIArgList, or ValueAsMetadata wrapping a
          // fwd-ref. Push the metadata ID.
          Vals.push_back(VE.getMetadataID(RawLocation));
          return false;
        };

        // Write out non-instruction debug information attached to this
        // instruction. Write it after the instruction so that it's easy to
        // re-attach to the instruction reading the records in.
        for (DbgRecord &DR : I.DebugMarker->getDbgRecordRange()) {
          if (DbgLabelRecord *DLR = dyn_cast<DbgLabelRecord>(&DR)) {
            Vals.push_back(VE.getMetadataID(&*DLR->getDebugLoc()));
            Vals.push_back(VE.getMetadataID(DLR->getLabel()));
            Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_RECORD_LABEL, Vals);
            Vals.clear();
            continue;
          }

          // First 3 fields are common to all kinds:
          //   DILocation, DILocalVariable, DIExpression
          // dbg_value (FUNC_CODE_DEBUG_RECORD_VALUE)
          //   ..., LocationMetadata
          // dbg_value (FUNC_CODE_DEBUG_RECORD_VALUE_SIMPLE - abbrev'd)
          //   ..., Value
          // dbg_declare (FUNC_CODE_DEBUG_RECORD_DECLARE)
          //   ..., LocationMetadata
          // dbg_assign (FUNC_CODE_DEBUG_RECORD_ASSIGN)
          //   ..., LocationMetadata, DIAssignID, DIExpression, LocationMetadata
          DbgVariableRecord &DVR = cast<DbgVariableRecord>(DR);
          Vals.push_back(VE.getMetadataID(&*DVR.getDebugLoc()));
          Vals.push_back(VE.getMetadataID(DVR.getVariable()));
          Vals.push_back(VE.getMetadataID(DVR.getExpression()));
          if (DVR.isDbgValue()) {
            if (PushValueOrMetadata(DVR.getRawLocation()))
              Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_RECORD_VALUE_SIMPLE, Vals,
                                FUNCTION_DEBUG_RECORD_VALUE_ABBREV);
            else
              Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_RECORD_VALUE, Vals);
          } else if (DVR.isDbgDeclare()) {
            Vals.push_back(VE.getMetadataID(DVR.getRawLocation()));
            Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_RECORD_DECLARE, Vals);
          } else {
            assert(DVR.isDbgAssign() && "Unexpected DbgRecord kind");
            Vals.push_back(VE.getMetadataID(DVR.getRawLocation()));
            Vals.push_back(VE.getMetadataID(DVR.getAssignID()));
            Vals.push_back(VE.getMetadataID(DVR.getAddressExpression()));
            Vals.push_back(VE.getMetadataID(DVR.getRawAddress()));
            Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_RECORD_ASSIGN, Vals);
          }
          Vals.clear();
        }
      }
    }

    if (BlockAddress *BA = BlockAddress::lookup(&BB)) {
      SmallVector<Value *> Worklist{BA};
      SmallPtrSet<Value *, 8> Visited{BA};
      while (!Worklist.empty()) {
        Value *V = Worklist.pop_back_val();
        for (User *U : V->users()) {
          if (auto *I = dyn_cast<Instruction>(U)) {
            Function *P = I->getFunction();
            if (P != &F)
              BlockAddressUsers.insert(P);
          } else if (isa<Constant>(U) && !isa<GlobalValue>(U) &&
                     Visited.insert(U).second)
            Worklist.push_back(U);
        }
      }
    }
  }

  if (!BlockAddressUsers.empty()) {
    Vals.resize(BlockAddressUsers.size());
    for (auto I : llvm::enumerate(BlockAddressUsers))
      Vals[I.index()] = VE.getValueID(I.value());
    Stream.EmitRecord(bitc::FUNC_CODE_BLOCKADDR_USERS, Vals);
    Vals.clear();
  }

  // Emit names for all the instructions etc.
  if (auto *Symtab = F.getValueSymbolTable())
    writeFunctionLevelValueSymbolTable(*Symtab);

  if (NeedsMetadataAttachment)
    writeFunctionMetadataAttachment(F);
  if (VE.shouldPreserveUseListOrder())
    writeUseListBlock(&F);
  VE.purgeFunction();
  Stream.ExitBlock();
}

// Emit blockinfo, which defines the standard abbreviations etc.
void ModuleBitcodeWriter::writeBlockInfo() {
  // We only want to emit block info records for blocks that have multiple
  // instances: CONSTANTS_BLOCK, FUNCTION_BLOCK and VALUE_SYMTAB_BLOCK.
  // Other blocks can define their abbrevs inline.
  Stream.EnterBlockInfoBlock();

  { // 8-bit fixed-width VST_CODE_ENTRY/VST_CODE_BBENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID, Abbv) !=
        VST_ENTRY_8_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // 7-bit fixed width VST_CODE_ENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_ENTRY));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID, Abbv) !=
        VST_ENTRY_7_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // 6-bit char6 VST_CODE_ENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_ENTRY));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID, Abbv) !=
        VST_ENTRY_6_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // 6-bit char6 VST_CODE_BBENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_BBENTRY));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID, Abbv) !=
        VST_BBENTRY_6_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // SETTYPE abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_SETTYPE));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                              VE.computeBitsRequiredForTypeIndices()));
    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, Abbv) !=
        CONSTANTS_SETTYPE_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // INTEGER abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_INTEGER));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, Abbv) !=
        CONSTANTS_INTEGER_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // CE_CAST abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_CE_CAST));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4));  // cast opc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,       // typeid
                              VE.computeBitsRequiredForTypeIndices()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));    // value id

    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, Abbv) !=
        CONSTANTS_CE_CAST_Abbrev)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // NULL abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_NULL));
    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, Abbv) !=
        CONSTANTS_NULL_Abbrev)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  // FIXME: This should only use space for first class types!

  { // INST_LOAD abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_LOAD));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Ptr
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,    // dest ty
                              VE.computeBitsRequiredForTypeIndices()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // Align
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // volatile
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_LOAD_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_UNOP abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_UNOP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // LHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_UNOP_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_UNOP_FLAGS abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_UNOP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // LHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8)); // flags
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_UNOP_FLAGS_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_BINOP abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_BINOP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // LHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // RHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_BINOP_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_BINOP_FLAGS abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_BINOP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // LHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // RHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8)); // flags
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_BINOP_FLAGS_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_CAST abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_CAST));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));    // OpVal
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,       // dest ty
                              VE.computeBitsRequiredForTypeIndices()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4));  // opc
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_CAST_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_CAST_FLAGS abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_CAST));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // OpVal
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,    // dest ty
                              VE.computeBitsRequiredForTypeIndices()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8)); // flags
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_CAST_FLAGS_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // INST_RET abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_RET));
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_RET_VOID_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_RET abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_RET));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ValID
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_RET_VAL_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_UNREACHABLE abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_UNREACHABLE));
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_UNREACHABLE_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  {
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_GEP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, // dest ty
                              Log2_32_Ceil(VE.getTypes().size() + 1)));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_INST_GEP_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  {
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_DEBUG_RECORD_VALUE_SIMPLE));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7)); // dbgloc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7)); // var
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7)); // expr
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // val
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, Abbv) !=
        FUNCTION_DEBUG_RECORD_VALUE_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering! 1");
  }
  Stream.ExitBlock();
}

/// Write the module path strings, currently only used when generating
/// a combined index file.
void IndexBitcodeWriter::writeModStrings() {
  Stream.EnterSubblock(bitc::MODULE_STRTAB_BLOCK_ID, 3);

  // TODO: See which abbrev sizes we actually need to emit

  // 8-bit fixed-width MST_ENTRY strings.
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::MST_CODE_ENTRY));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
  unsigned Abbrev8Bit = Stream.EmitAbbrev(std::move(Abbv));

  // 7-bit fixed width MST_ENTRY strings.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::MST_CODE_ENTRY));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7));
  unsigned Abbrev7Bit = Stream.EmitAbbrev(std::move(Abbv));

  // 6-bit char6 MST_ENTRY strings.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::MST_CODE_ENTRY));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
  unsigned Abbrev6Bit = Stream.EmitAbbrev(std::move(Abbv));

  // Module Hash, 160 bits SHA1. Optionally, emitted after each MST_CODE_ENTRY.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::MST_CODE_HASH));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  unsigned AbbrevHash = Stream.EmitAbbrev(std::move(Abbv));

  SmallVector<unsigned, 64> Vals;
  forEachModule([&](const StringMapEntry<ModuleHash> &MPSE) {
    StringRef Key = MPSE.getKey();
    const auto &Hash = MPSE.getValue();
    StringEncoding Bits = getStringEncoding(Key);
    unsigned AbbrevToUse = Abbrev8Bit;
    if (Bits == SE_Char6)
      AbbrevToUse = Abbrev6Bit;
    else if (Bits == SE_Fixed7)
      AbbrevToUse = Abbrev7Bit;

    auto ModuleId = ModuleIdMap.size();
    ModuleIdMap[Key] = ModuleId;
    Vals.push_back(ModuleId);
    Vals.append(Key.begin(), Key.end());

    // Emit the finished record.
    Stream.EmitRecord(bitc::MST_CODE_ENTRY, Vals, AbbrevToUse);

    // Emit an optional hash for the module now
    if (llvm::any_of(Hash, [](uint32_t H) { return H; })) {
      Vals.assign(Hash.begin(), Hash.end());
      // Emit the hash record.
      Stream.EmitRecord(bitc::MST_CODE_HASH, Vals, AbbrevHash);
    }

    Vals.clear();
  });
  Stream.ExitBlock();
}

/// Write the function type metadata related records that need to appear before
/// a function summary entry (whether per-module or combined).
template <typename Fn>
static void writeFunctionTypeMetadataRecords(BitstreamWriter &Stream,
                                             FunctionSummary *FS,
                                             Fn GetValueID) {
  if (!FS->type_tests().empty())
    Stream.EmitRecord(bitc::FS_TYPE_TESTS, FS->type_tests());

  SmallVector<uint64_t, 64> Record;

  auto WriteVFuncIdVec = [&](uint64_t Ty,
                             ArrayRef<FunctionSummary::VFuncId> VFs) {
    if (VFs.empty())
      return;
    Record.clear();
    for (auto &VF : VFs) {
      Record.push_back(VF.GUID);
      Record.push_back(VF.Offset);
    }
    Stream.EmitRecord(Ty, Record);
  };

  WriteVFuncIdVec(bitc::FS_TYPE_TEST_ASSUME_VCALLS,
                  FS->type_test_assume_vcalls());
  WriteVFuncIdVec(bitc::FS_TYPE_CHECKED_LOAD_VCALLS,
                  FS->type_checked_load_vcalls());

  auto WriteConstVCallVec = [&](uint64_t Ty,
                                ArrayRef<FunctionSummary::ConstVCall> VCs) {
    for (auto &VC : VCs) {
      Record.clear();
      Record.push_back(VC.VFunc.GUID);
      Record.push_back(VC.VFunc.Offset);
      llvm::append_range(Record, VC.Args);
      Stream.EmitRecord(Ty, Record);
    }
  };

  WriteConstVCallVec(bitc::FS_TYPE_TEST_ASSUME_CONST_VCALL,
                     FS->type_test_assume_const_vcalls());
  WriteConstVCallVec(bitc::FS_TYPE_CHECKED_LOAD_CONST_VCALL,
                     FS->type_checked_load_const_vcalls());

  auto WriteRange = [&](ConstantRange Range) {
    Range = Range.sextOrTrunc(FunctionSummary::ParamAccess::RangeWidth);
    assert(Range.getLower().getNumWords() == 1);
    assert(Range.getUpper().getNumWords() == 1);
    emitSignedInt64(Record, *Range.getLower().getRawData());
    emitSignedInt64(Record, *Range.getUpper().getRawData());
  };

  if (!FS->paramAccesses().empty()) {
    Record.clear();
    for (auto &Arg : FS->paramAccesses()) {
      size_t UndoSize = Record.size();
      Record.push_back(Arg.ParamNo);
      WriteRange(Arg.Use);
      Record.push_back(Arg.Calls.size());
      for (auto &Call : Arg.Calls) {
        Record.push_back(Call.ParamNo);
        std::optional<unsigned> ValueID = GetValueID(Call.Callee);
        if (!ValueID) {
          // If ValueID is unknown we can't drop just this call, we must drop
          // entire parameter.
          Record.resize(UndoSize);
          break;
        }
        Record.push_back(*ValueID);
        WriteRange(Call.Offsets);
      }
    }
    if (!Record.empty())
      Stream.EmitRecord(bitc::FS_PARAM_ACCESS, Record);
  }
}

/// Collect type IDs from type tests used by function.
static void
getReferencedTypeIds(FunctionSummary *FS,
                     std::set<GlobalValue::GUID> &ReferencedTypeIds) {
  if (!FS->type_tests().empty())
    for (auto &TT : FS->type_tests())
      ReferencedTypeIds.insert(TT);

  auto GetReferencedTypesFromVFuncIdVec =
      [&](ArrayRef<FunctionSummary::VFuncId> VFs) {
        for (auto &VF : VFs)
          ReferencedTypeIds.insert(VF.GUID);
      };

  GetReferencedTypesFromVFuncIdVec(FS->type_test_assume_vcalls());
  GetReferencedTypesFromVFuncIdVec(FS->type_checked_load_vcalls());

  auto GetReferencedTypesFromConstVCallVec =
      [&](ArrayRef<FunctionSummary::ConstVCall> VCs) {
        for (auto &VC : VCs)
          ReferencedTypeIds.insert(VC.VFunc.GUID);
      };

  GetReferencedTypesFromConstVCallVec(FS->type_test_assume_const_vcalls());
  GetReferencedTypesFromConstVCallVec(FS->type_checked_load_const_vcalls());
}

static void writeWholeProgramDevirtResolutionByArg(
    SmallVector<uint64_t, 64> &NameVals, const std::vector<uint64_t> &args,
    const WholeProgramDevirtResolution::ByArg &ByArg) {
  NameVals.push_back(args.size());
  llvm::append_range(NameVals, args);

  NameVals.push_back(ByArg.TheKind);
  NameVals.push_back(ByArg.Info);
  NameVals.push_back(ByArg.Byte);
  NameVals.push_back(ByArg.Bit);
}

static void writeWholeProgramDevirtResolution(
    SmallVector<uint64_t, 64> &NameVals, StringTableBuilder &StrtabBuilder,
    uint64_t Id, const WholeProgramDevirtResolution &Wpd) {
  NameVals.push_back(Id);

  NameVals.push_back(Wpd.TheKind);
  NameVals.push_back(StrtabBuilder.add(Wpd.SingleImplName));
  NameVals.push_back(Wpd.SingleImplName.size());

  NameVals.push_back(Wpd.ResByArg.size());
  for (auto &A : Wpd.ResByArg)
    writeWholeProgramDevirtResolutionByArg(NameVals, A.first, A.second);
}

static void writeTypeIdSummaryRecord(SmallVector<uint64_t, 64> &NameVals,
                                     StringTableBuilder &StrtabBuilder,
                                     const std::string &Id,
                                     const TypeIdSummary &Summary) {
  NameVals.push_back(StrtabBuilder.add(Id));
  NameVals.push_back(Id.size());

  NameVals.push_back(Summary.TTRes.TheKind);
  NameVals.push_back(Summary.TTRes.SizeM1BitWidth);
  NameVals.push_back(Summary.TTRes.AlignLog2);
  NameVals.push_back(Summary.TTRes.SizeM1);
  NameVals.push_back(Summary.TTRes.BitMask);
  NameVals.push_back(Summary.TTRes.InlineBits);

  for (auto &W : Summary.WPDRes)
    writeWholeProgramDevirtResolution(NameVals, StrtabBuilder, W.first,
                                      W.second);
}

static void writeTypeIdCompatibleVtableSummaryRecord(
    SmallVector<uint64_t, 64> &NameVals, StringTableBuilder &StrtabBuilder,
    const std::string &Id, const TypeIdCompatibleVtableInfo &Summary,
    ValueEnumerator &VE) {
  NameVals.push_back(StrtabBuilder.add(Id));
  NameVals.push_back(Id.size());

  for (auto &P : Summary) {
    NameVals.push_back(P.AddressPointOffset);
    NameVals.push_back(VE.getValueID(P.VTableVI.getValue()));
  }
}

static void writeFunctionHeapProfileRecords(
    BitstreamWriter &Stream, FunctionSummary *FS, unsigned CallsiteAbbrev,
    unsigned AllocAbbrev, bool PerModule,
    std::function<unsigned(const ValueInfo &VI)> GetValueID,
    std::function<unsigned(unsigned)> GetStackIndex) {
  SmallVector<uint64_t> Record;

  for (auto &CI : FS->callsites()) {
    Record.clear();
    // Per module callsite clones should always have a single entry of
    // value 0.
    assert(!PerModule || (CI.Clones.size() == 1 && CI.Clones[0] == 0));
    Record.push_back(GetValueID(CI.Callee));
    if (!PerModule) {
      Record.push_back(CI.StackIdIndices.size());
      Record.push_back(CI.Clones.size());
    }
    for (auto Id : CI.StackIdIndices)
      Record.push_back(GetStackIndex(Id));
    if (!PerModule) {
      for (auto V : CI.Clones)
        Record.push_back(V);
    }
    Stream.EmitRecord(PerModule ? bitc::FS_PERMODULE_CALLSITE_INFO
                                : bitc::FS_COMBINED_CALLSITE_INFO,
                      Record, CallsiteAbbrev);
  }

  for (auto &AI : FS->allocs()) {
    Record.clear();
    // Per module alloc versions should always have a single entry of
    // value 0.
    assert(!PerModule || (AI.Versions.size() == 1 && AI.Versions[0] == 0));
    Record.push_back(AI.MIBs.size());
    if (!PerModule)
      Record.push_back(AI.Versions.size());
    for (auto &MIB : AI.MIBs) {
      Record.push_back((uint8_t)MIB.AllocType);
      Record.push_back(MIB.StackIdIndices.size());
      for (auto Id : MIB.StackIdIndices)
        Record.push_back(GetStackIndex(Id));
    }
    if (!PerModule) {
      for (auto V : AI.Versions)
        Record.push_back(V);
    }
    assert(AI.TotalSizes.empty() || AI.TotalSizes.size() == AI.MIBs.size());
    if (!AI.TotalSizes.empty()) {
      for (auto Size : AI.TotalSizes)
        Record.push_back(Size);
    }
    Stream.EmitRecord(PerModule ? bitc::FS_PERMODULE_ALLOC_INFO
                                : bitc::FS_COMBINED_ALLOC_INFO,
                      Record, AllocAbbrev);
  }
}

// Helper to emit a single function summary record.
void ModuleBitcodeWriterBase::writePerModuleFunctionSummaryRecord(
    SmallVector<uint64_t, 64> &NameVals, GlobalValueSummary *Summary,
    unsigned ValueID, unsigned FSCallsRelBFAbbrev,
    unsigned FSCallsProfileAbbrev, unsigned CallsiteAbbrev,
    unsigned AllocAbbrev, const Function &F) {
  NameVals.push_back(ValueID);

  FunctionSummary *FS = cast<FunctionSummary>(Summary);

  writeFunctionTypeMetadataRecords(
      Stream, FS, [&](const ValueInfo &VI) -> std::optional<unsigned> {
        return {VE.getValueID(VI.getValue())};
      });

  writeFunctionHeapProfileRecords(
      Stream, FS, CallsiteAbbrev, AllocAbbrev,
      /*PerModule*/ true,
      /*GetValueId*/ [&](const ValueInfo &VI) { return getValueId(VI); },
      /*GetStackIndex*/ [&](unsigned I) { return I; });

  auto SpecialRefCnts = FS->specialRefCounts();
  NameVals.push_back(getEncodedGVSummaryFlags(FS->flags()));
  NameVals.push_back(FS->instCount());
  NameVals.push_back(getEncodedFFlags(FS->fflags()));
  NameVals.push_back(FS->refs().size());
  NameVals.push_back(SpecialRefCnts.first);  // rorefcnt
  NameVals.push_back(SpecialRefCnts.second); // worefcnt

  for (auto &RI : FS->refs())
    NameVals.push_back(getValueId(RI));

  const bool UseRelBFRecord =
      WriteRelBFToSummary && !F.hasProfileData() &&
      ForceSummaryEdgesCold == FunctionSummary::FSHT_None;
  for (auto &ECI : FS->calls()) {
    NameVals.push_back(getValueId(ECI.first));
    if (UseRelBFRecord)
      NameVals.push_back(getEncodedRelBFCallEdgeInfo(ECI.second));
    else
      NameVals.push_back(getEncodedHotnessCallEdgeInfo(ECI.second));
  }

  unsigned FSAbbrev =
      (UseRelBFRecord ? FSCallsRelBFAbbrev : FSCallsProfileAbbrev);
  unsigned Code =
      (UseRelBFRecord ? bitc::FS_PERMODULE_RELBF : bitc::FS_PERMODULE_PROFILE);

  // Emit the finished record.
  Stream.EmitRecord(Code, NameVals, FSAbbrev);
  NameVals.clear();
}

// Collect the global value references in the given variable's initializer,
// and emit them in a summary record.
void ModuleBitcodeWriterBase::writeModuleLevelReferences(
    const GlobalVariable &V, SmallVector<uint64_t, 64> &NameVals,
    unsigned FSModRefsAbbrev, unsigned FSModVTableRefsAbbrev) {
  auto VI = Index->getValueInfo(V.getGUID());
  if (!VI || VI.getSummaryList().empty()) {
    // Only declarations should not have a summary (a declaration might however
    // have a summary if the def was in module level asm).
    assert(V.isDeclaration());
    return;
  }
  auto *Summary = VI.getSummaryList()[0].get();
  NameVals.push_back(VE.getValueID(&V));
  GlobalVarSummary *VS = cast<GlobalVarSummary>(Summary);
  NameVals.push_back(getEncodedGVSummaryFlags(VS->flags()));
  NameVals.push_back(getEncodedGVarFlags(VS->varflags()));

  auto VTableFuncs = VS->vTableFuncs();
  if (!VTableFuncs.empty())
    NameVals.push_back(VS->refs().size());

  unsigned SizeBeforeRefs = NameVals.size();
  for (auto &RI : VS->refs())
    NameVals.push_back(VE.getValueID(RI.getValue()));
  // Sort the refs for determinism output, the vector returned by FS->refs() has
  // been initialized from a DenseSet.
  llvm::sort(drop_begin(NameVals, SizeBeforeRefs));

  if (VTableFuncs.empty())
    Stream.EmitRecord(bitc::FS_PERMODULE_GLOBALVAR_INIT_REFS, NameVals,
                      FSModRefsAbbrev);
  else {
    // VTableFuncs pairs should already be sorted by offset.
    for (auto &P : VTableFuncs) {
      NameVals.push_back(VE.getValueID(P.FuncVI.getValue()));
      NameVals.push_back(P.VTableOffset);
    }

    Stream.EmitRecord(bitc::FS_PERMODULE_VTABLE_GLOBALVAR_INIT_REFS, NameVals,
                      FSModVTableRefsAbbrev);
  }
  NameVals.clear();
}

/// Emit the per-module summary section alongside the rest of
/// the module's bitcode.
void ModuleBitcodeWriterBase::writePerModuleGlobalValueSummary() {
  // By default we compile with ThinLTO if the module has a summary, but the
  // client can request full LTO with a module flag.
  bool IsThinLTO = true;
  if (auto *MD =
          mdconst::extract_or_null<ConstantInt>(M.getModuleFlag("ThinLTO")))
    IsThinLTO = MD->getZExtValue();
  Stream.EnterSubblock(IsThinLTO ? bitc::GLOBALVAL_SUMMARY_BLOCK_ID
                                 : bitc::FULL_LTO_GLOBALVAL_SUMMARY_BLOCK_ID,
                       4);

  Stream.EmitRecord(
      bitc::FS_VERSION,
      ArrayRef<uint64_t>{ModuleSummaryIndex::BitcodeSummaryVersion});

  // Write the index flags.
  uint64_t Flags = 0;
  // Bits 1-3 are set only in the combined index, skip them.
  if (Index->enableSplitLTOUnit())
    Flags |= 0x8;
  if (Index->hasUnifiedLTO())
    Flags |= 0x200;

  Stream.EmitRecord(bitc::FS_FLAGS, ArrayRef<uint64_t>{Flags});

  if (Index->begin() == Index->end()) {
    Stream.ExitBlock();
    return;
  }

  for (const auto &GVI : valueIds()) {
    Stream.EmitRecord(bitc::FS_VALUE_GUID,
                      ArrayRef<uint64_t>{GVI.second, GVI.first});
  }

  if (!Index->stackIds().empty()) {
    auto StackIdAbbv = std::make_shared<BitCodeAbbrev>();
    StackIdAbbv->Add(BitCodeAbbrevOp(bitc::FS_STACK_IDS));
    // numids x stackid
    StackIdAbbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    StackIdAbbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    unsigned StackIdAbbvId = Stream.EmitAbbrev(std::move(StackIdAbbv));
    Stream.EmitRecord(bitc::FS_STACK_IDS, Index->stackIds(), StackIdAbbvId);
  }

  // Abbrev for FS_PERMODULE_PROFILE.
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_PERMODULE_PROFILE));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // instcount
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // fflags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // numrefs
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // rorefcnt
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // worefcnt
  // numrefs x valueid, n x (valueid, hotness+tailcall flags)
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned FSCallsProfileAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_PERMODULE_RELBF.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_PERMODULE_RELBF));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // instcount
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // fflags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // numrefs
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // rorefcnt
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // worefcnt
  // numrefs x valueid, n x (valueid, rel_block_freq+tailcall])
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned FSCallsRelBFAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_PERMODULE_GLOBALVAR_INIT_REFS.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_PERMODULE_GLOBALVAR_INIT_REFS));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));  // valueids
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned FSModRefsAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_PERMODULE_VTABLE_GLOBALVAR_INIT_REFS.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_PERMODULE_VTABLE_GLOBALVAR_INIT_REFS));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // numrefs
  // numrefs x valueid, n x (valueid , offset)
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned FSModVTableRefsAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_ALIAS.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_ALIAS));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  unsigned FSAliasAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_TYPE_ID_METADATA
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_TYPE_ID_METADATA));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // typeid strtab index
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // typeid length
  // n x (valueid , offset)
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned TypeIdCompatibleVtableAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_PERMODULE_CALLSITE_INFO));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // valueid
  // n x stackidindex
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned CallsiteAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_PERMODULE_ALLOC_INFO));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // nummib
  // n x (alloc type, numstackids, numstackids x stackidindex)
  // optional: nummib x total size
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned AllocAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  SmallVector<uint64_t, 64> NameVals;
  // Iterate over the list of functions instead of the Index to
  // ensure the ordering is stable.
  for (const Function &F : M) {
    // Summary emission does not support anonymous functions, they have to
    // renamed using the anonymous function renaming pass.
    if (!F.hasName())
      report_fatal_error("Unexpected anonymous function when writing summary");

    ValueInfo VI = Index->getValueInfo(F.getGUID());
    if (!VI || VI.getSummaryList().empty()) {
      // Only declarations should not have a summary (a declaration might
      // however have a summary if the def was in module level asm).
      assert(F.isDeclaration());
      continue;
    }
    auto *Summary = VI.getSummaryList()[0].get();
    writePerModuleFunctionSummaryRecord(
        NameVals, Summary, VE.getValueID(&F), FSCallsRelBFAbbrev,
        FSCallsProfileAbbrev, CallsiteAbbrev, AllocAbbrev, F);
  }

  // Capture references from GlobalVariable initializers, which are outside
  // of a function scope.
  for (const GlobalVariable &G : M.globals())
    writeModuleLevelReferences(G, NameVals, FSModRefsAbbrev,
                               FSModVTableRefsAbbrev);

  for (const GlobalAlias &A : M.aliases()) {
    auto *Aliasee = A.getAliaseeObject();
    // Skip ifunc and nameless functions which don't have an entry in the
    // summary.
    if (!Aliasee->hasName() || isa<GlobalIFunc>(Aliasee))
      continue;
    auto AliasId = VE.getValueID(&A);
    auto AliaseeId = VE.getValueID(Aliasee);
    NameVals.push_back(AliasId);
    auto *Summary = Index->getGlobalValueSummary(A);
    AliasSummary *AS = cast<AliasSummary>(Summary);
    NameVals.push_back(getEncodedGVSummaryFlags(AS->flags()));
    NameVals.push_back(AliaseeId);
    Stream.EmitRecord(bitc::FS_ALIAS, NameVals, FSAliasAbbrev);
    NameVals.clear();
  }

  for (auto &S : Index->typeIdCompatibleVtableMap()) {
    writeTypeIdCompatibleVtableSummaryRecord(NameVals, StrtabBuilder, S.first,
                                             S.second, VE);
    Stream.EmitRecord(bitc::FS_TYPE_ID_METADATA, NameVals,
                      TypeIdCompatibleVtableAbbrev);
    NameVals.clear();
  }

  if (Index->getBlockCount())
    Stream.EmitRecord(bitc::FS_BLOCK_COUNT,
                      ArrayRef<uint64_t>{Index->getBlockCount()});

  Stream.ExitBlock();
}

/// Emit the combined summary section into the combined index file.
void IndexBitcodeWriter::writeCombinedGlobalValueSummary() {
  Stream.EnterSubblock(bitc::GLOBALVAL_SUMMARY_BLOCK_ID, 4);
  Stream.EmitRecord(
      bitc::FS_VERSION,
      ArrayRef<uint64_t>{ModuleSummaryIndex::BitcodeSummaryVersion});

  // Write the index flags.
  Stream.EmitRecord(bitc::FS_FLAGS, ArrayRef<uint64_t>{Index.getFlags()});

  for (const auto &GVI : valueIds()) {
    Stream.EmitRecord(bitc::FS_VALUE_GUID,
                      ArrayRef<uint64_t>{GVI.second, GVI.first});
  }

  // Write the stack ids used by this index, which will be a subset of those in
  // the full index in the case of distributed indexes.
  if (!StackIds.empty()) {
    auto StackIdAbbv = std::make_shared<BitCodeAbbrev>();
    StackIdAbbv->Add(BitCodeAbbrevOp(bitc::FS_STACK_IDS));
    // numids x stackid
    StackIdAbbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    StackIdAbbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    unsigned StackIdAbbvId = Stream.EmitAbbrev(std::move(StackIdAbbv));
    Stream.EmitRecord(bitc::FS_STACK_IDS, StackIds, StackIdAbbvId);
  }

  // Abbrev for FS_COMBINED_PROFILE.
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_COMBINED_PROFILE));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // modid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // instcount
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // fflags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // entrycount
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // numrefs
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // rorefcnt
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // worefcnt
  // numrefs x valueid, n x (valueid, hotness+tailcall flags)
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned FSCallsProfileAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_COMBINED_GLOBALVAR_INIT_REFS.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_COMBINED_GLOBALVAR_INIT_REFS));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // modid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));    // valueids
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned FSModRefsAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for FS_COMBINED_ALIAS.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_COMBINED_ALIAS));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // modid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // flags
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // valueid
  unsigned FSAliasAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_COMBINED_CALLSITE_INFO));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // valueid
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // numstackindices
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // numver
  // numstackindices x stackidindex, numver x version
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned CallsiteAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::FS_COMBINED_ALLOC_INFO));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // nummib
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4)); // numver
  // nummib x (alloc type, numstackids, numstackids x stackidindex),
  // numver x version
  // optional: nummib x total size
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  unsigned AllocAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  auto shouldImportValueAsDecl = [&](GlobalValueSummary *GVS) -> bool {
    if (DecSummaries == nullptr)
      return false;
    return DecSummaries->count(GVS);
  };

  // The aliases are emitted as a post-pass, and will point to the value
  // id of the aliasee. Save them in a vector for post-processing.
  SmallVector<AliasSummary *, 64> Aliases;

  // Save the value id for each summary for alias emission.
  DenseMap<const GlobalValueSummary *, unsigned> SummaryToValueIdMap;

  SmallVector<uint64_t, 64> NameVals;

  // Set that will be populated during call to writeFunctionTypeMetadataRecords
  // with the type ids referenced by this index file.
  std::set<GlobalValue::GUID> ReferencedTypeIds;

  // For local linkage, we also emit the original name separately
  // immediately after the record.
  auto MaybeEmitOriginalName = [&](GlobalValueSummary &S) {
    // We don't need to emit the original name if we are writing the index for
    // distributed backends (in which case ModuleToSummariesForIndex is
    // non-null). The original name is only needed during the thin link, since
    // for SamplePGO the indirect call targets for local functions have
    // have the original name annotated in profile.
    // Continue to emit it when writing out the entire combined index, which is
    // used in testing the thin link via llvm-lto.
    if (ModuleToSummariesForIndex || !GlobalValue::isLocalLinkage(S.linkage()))
      return;
    NameVals.push_back(S.getOriginalName());
    Stream.EmitRecord(bitc::FS_COMBINED_ORIGINAL_NAME, NameVals);
    NameVals.clear();
  };

  std::set<GlobalValue::GUID> DefOrUseGUIDs;
  forEachSummary([&](GVInfo I, bool IsAliasee) {
    GlobalValueSummary *S = I.second;
    assert(S);
    DefOrUseGUIDs.insert(I.first);
    for (const ValueInfo &VI : S->refs())
      DefOrUseGUIDs.insert(VI.getGUID());

    auto ValueId = getValueId(I.first);
    assert(ValueId);
    SummaryToValueIdMap[S] = *ValueId;

    // If this is invoked for an aliasee, we want to record the above
    // mapping, but then not emit a summary entry (if the aliasee is
    // to be imported, we will invoke this separately with IsAliasee=false).
    if (IsAliasee)
      return;

    if (auto *AS = dyn_cast<AliasSummary>(S)) {
      // Will process aliases as a post-pass because the reader wants all
      // global to be loaded first.
      Aliases.push_back(AS);
      return;
    }

    if (auto *VS = dyn_cast<GlobalVarSummary>(S)) {
      NameVals.push_back(*ValueId);
      assert(ModuleIdMap.count(VS->modulePath()));
      NameVals.push_back(ModuleIdMap[VS->modulePath()]);
      NameVals.push_back(getEncodedGVSummaryFlags(VS->flags()));
      NameVals.push_back(getEncodedGVarFlags(VS->varflags()));
      for (auto &RI : VS->refs()) {
        auto RefValueId = getValueId(RI.getGUID());
        if (!RefValueId)
          continue;
        NameVals.push_back(*RefValueId);
      }

      // Emit the finished record.
      Stream.EmitRecord(bitc::FS_COMBINED_GLOBALVAR_INIT_REFS, NameVals,
                        FSModRefsAbbrev);
      NameVals.clear();
      MaybeEmitOriginalName(*S);
      return;
    }

    auto GetValueId = [&](const ValueInfo &VI) -> std::optional<unsigned> {
      if (!VI)
        return std::nullopt;
      return getValueId(VI.getGUID());
    };

    auto *FS = cast<FunctionSummary>(S);
    writeFunctionTypeMetadataRecords(Stream, FS, GetValueId);
    getReferencedTypeIds(FS, ReferencedTypeIds);

    writeFunctionHeapProfileRecords(
        Stream, FS, CallsiteAbbrev, AllocAbbrev,
        /*PerModule*/ false,
        /*GetValueId*/
        [&](const ValueInfo &VI) -> unsigned {
          std::optional<unsigned> ValueID = GetValueId(VI);
          // This can happen in shared index files for distributed ThinLTO if
          // the callee function summary is not included. Record 0 which we
          // will have to deal with conservatively when doing any kind of
          // validation in the ThinLTO backends.
          if (!ValueID)
            return 0;
          return *ValueID;
        },
        /*GetStackIndex*/
        [&](unsigned I) {
          // Get the corresponding index into the list of StackIds actually
          // being written for this combined index (which may be a subset in
          // the case of distributed indexes).
          assert(StackIdIndicesToIndex.contains(I));
          return StackIdIndicesToIndex[I];
        });

    NameVals.push_back(*ValueId);
    assert(ModuleIdMap.count(FS->modulePath()));
    NameVals.push_back(ModuleIdMap[FS->modulePath()]);
    NameVals.push_back(
        getEncodedGVSummaryFlags(FS->flags(), shouldImportValueAsDecl(FS)));
    NameVals.push_back(FS->instCount());
    NameVals.push_back(getEncodedFFlags(FS->fflags()));
    NameVals.push_back(FS->entryCount());

    // Fill in below
    NameVals.push_back(0); // numrefs
    NameVals.push_back(0); // rorefcnt
    NameVals.push_back(0); // worefcnt

    unsigned Count = 0, RORefCnt = 0, WORefCnt = 0;
    for (auto &RI : FS->refs()) {
      auto RefValueId = getValueId(RI.getGUID());
      if (!RefValueId)
        continue;
      NameVals.push_back(*RefValueId);
      if (RI.isReadOnly())
        RORefCnt++;
      else if (RI.isWriteOnly())
        WORefCnt++;
      Count++;
    }
    NameVals[6] = Count;
    NameVals[7] = RORefCnt;
    NameVals[8] = WORefCnt;

    for (auto &EI : FS->calls()) {
      // If this GUID doesn't have a value id, it doesn't have a function
      // summary and we don't need to record any calls to it.
      std::optional<unsigned> CallValueId = GetValueId(EI.first);
      if (!CallValueId)
        continue;
      NameVals.push_back(*CallValueId);
      NameVals.push_back(getEncodedHotnessCallEdgeInfo(EI.second));
    }

    // Emit the finished record.
    Stream.EmitRecord(bitc::FS_COMBINED_PROFILE, NameVals,
                      FSCallsProfileAbbrev);
    NameVals.clear();
    MaybeEmitOriginalName(*S);
  });

  for (auto *AS : Aliases) {
    auto AliasValueId = SummaryToValueIdMap[AS];
    assert(AliasValueId);
    NameVals.push_back(AliasValueId);
    assert(ModuleIdMap.count(AS->modulePath()));
    NameVals.push_back(ModuleIdMap[AS->modulePath()]);
    NameVals.push_back(
        getEncodedGVSummaryFlags(AS->flags(), shouldImportValueAsDecl(AS)));
    auto AliaseeValueId = SummaryToValueIdMap[&AS->getAliasee()];
    assert(AliaseeValueId);
    NameVals.push_back(AliaseeValueId);

    // Emit the finished record.
    Stream.EmitRecord(bitc::FS_COMBINED_ALIAS, NameVals, FSAliasAbbrev);
    NameVals.clear();
    MaybeEmitOriginalName(*AS);

    if (auto *FS = dyn_cast<FunctionSummary>(&AS->getAliasee()))
      getReferencedTypeIds(FS, ReferencedTypeIds);
  }

  if (!Index.cfiFunctionDefs().empty()) {
    for (auto &S : Index.cfiFunctionDefs()) {
      if (DefOrUseGUIDs.count(
              GlobalValue::getGUID(GlobalValue::dropLLVMManglingEscape(S)))) {
        NameVals.push_back(StrtabBuilder.add(S));
        NameVals.push_back(S.size());
      }
    }
    if (!NameVals.empty()) {
      Stream.EmitRecord(bitc::FS_CFI_FUNCTION_DEFS, NameVals);
      NameVals.clear();
    }
  }

  if (!Index.cfiFunctionDecls().empty()) {
    for (auto &S : Index.cfiFunctionDecls()) {
      if (DefOrUseGUIDs.count(
              GlobalValue::getGUID(GlobalValue::dropLLVMManglingEscape(S)))) {
        NameVals.push_back(StrtabBuilder.add(S));
        NameVals.push_back(S.size());
      }
    }
    if (!NameVals.empty()) {
      Stream.EmitRecord(bitc::FS_CFI_FUNCTION_DECLS, NameVals);
      NameVals.clear();
    }
  }

  // Walk the GUIDs that were referenced, and write the
  // corresponding type id records.
  for (auto &T : ReferencedTypeIds) {
    auto TidIter = Index.typeIds().equal_range(T);
    for (auto It = TidIter.first; It != TidIter.second; ++It) {
      writeTypeIdSummaryRecord(NameVals, StrtabBuilder, It->second.first,
                               It->second.second);
      Stream.EmitRecord(bitc::FS_TYPE_ID, NameVals);
      NameVals.clear();
    }
  }

  if (Index.getBlockCount())
    Stream.EmitRecord(bitc::FS_BLOCK_COUNT,
                      ArrayRef<uint64_t>{Index.getBlockCount()});

  Stream.ExitBlock();
}

/// Create the "IDENTIFICATION_BLOCK_ID" containing a single string with the
/// current llvm version, and a record for the epoch number.
static void writeIdentificationBlock(BitstreamWriter &Stream) {
  Stream.EnterSubblock(bitc::IDENTIFICATION_BLOCK_ID, 5);

  // Write the "user readable" string identifying the bitcode producer
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::IDENTIFICATION_CODE_STRING));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
  auto StringAbbrev = Stream.EmitAbbrev(std::move(Abbv));
  writeStringRecord(Stream, bitc::IDENTIFICATION_CODE_STRING,
                    "LLVM" LLVM_VERSION_STRING, StringAbbrev);

  // Write the epoch version
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::IDENTIFICATION_CODE_EPOCH));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  auto EpochAbbrev = Stream.EmitAbbrev(std::move(Abbv));
  constexpr std::array<unsigned, 1> Vals = {{bitc::BITCODE_CURRENT_EPOCH}};
  Stream.EmitRecord(bitc::IDENTIFICATION_CODE_EPOCH, Vals, EpochAbbrev);
  Stream.ExitBlock();
}

void ModuleBitcodeWriter::writeModuleHash(StringRef View) {
  // Emit the module's hash.
  // MODULE_CODE_HASH: [5*i32]
  if (GenerateHash) {
    uint32_t Vals[5];
    Hasher.update(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(View.data()), View.size()));
    std::array<uint8_t, 20> Hash = Hasher.result();
    for (int Pos = 0; Pos < 20; Pos += 4) {
      Vals[Pos / 4] = support::endian::read32be(Hash.data() + Pos);
    }

    // Emit the finished record.
    Stream.EmitRecord(bitc::MODULE_CODE_HASH, Vals);

    if (ModHash)
      // Save the written hash value.
      llvm::copy(Vals, std::begin(*ModHash));
  }
}

void ModuleBitcodeWriter::write() {
  writeIdentificationBlock(Stream);

  Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);
  // We will want to write the module hash at this point. Block any flushing so
  // we can have access to the whole underlying data later.
  Stream.markAndBlockFlushing();

  writeModuleVersion();

  // Emit blockinfo, which defines the standard abbreviations etc.
  writeBlockInfo();

  // Emit information describing all of the types in the module.
  writeTypeTable();

  // Emit information about attribute groups.
  writeAttributeGroupTable();

  // Emit information about parameter attributes.
  writeAttributeTable();

  writeComdats();

  // Emit top-level description of module, including target triple, inline asm,
  // descriptors for global variables, and function prototype info.
  writeModuleInfo();

  // Emit constants.
  writeModuleConstants();

  // Emit metadata kind names.
  writeModuleMetadataKinds();

  // Emit metadata.
  writeModuleMetadata();

  // Emit module-level use-lists.
  if (VE.shouldPreserveUseListOrder())
    writeUseListBlock(nullptr);

  writeOperandBundleTags();
  writeSyncScopeNames();

  // Emit function bodies.
  DenseMap<const Function *, uint64_t> FunctionToBitcodeIndex;
  for (const Function &F : M)
    if (!F.isDeclaration())
      writeFunction(F, FunctionToBitcodeIndex);

  // Need to write after the above call to WriteFunction which populates
  // the summary information in the index.
  if (Index)
    writePerModuleGlobalValueSummary();

  writeGlobalValueSymbolTable(FunctionToBitcodeIndex);

  writeModuleHash(Stream.getMarkedBufferAndResumeFlushing());

  Stream.ExitBlock();
}

static void writeInt32ToBuffer(uint32_t Value, SmallVectorImpl<char> &Buffer,
                               uint32_t &Position) {
  support::endian::write32le(&Buffer[Position], Value);
  Position += 4;
}

/// If generating a bc file on darwin, we have to emit a
/// header and trailer to make it compatible with the system archiver.  To do
/// this we emit the following header, and then emit a trailer that pads the
/// file out to be a multiple of 16 bytes.
///
/// struct bc_header {
///   uint32_t Magic;         // 0x0B17C0DE
///   uint32_t Version;       // Version, currently always 0.
///   uint32_t BitcodeOffset; // Offset to traditional bitcode file.
///   uint32_t BitcodeSize;   // Size of traditional bitcode file.
///   uint32_t CPUType;       // CPU specifier.
///   ... potentially more later ...
/// };
static void emitDarwinBCHeaderAndTrailer(SmallVectorImpl<char> &Buffer,
                                         const Triple &TT) {
  unsigned CPUType = ~0U;

  // Match x86_64-*, i[3-9]86-*, powerpc-*, powerpc64-*, arm-*, thumb-*,
  // armv[0-9]-*, thumbv[0-9]-*, armv5te-*, or armv6t2-*. The CPUType is a magic
  // number from /usr/include/mach/machine.h.  It is ok to reproduce the
  // specific constants here because they are implicitly part of the Darwin ABI.
  enum {
    DARWIN_CPU_ARCH_ABI64      = 0x01000000,
    DARWIN_CPU_TYPE_X86        = 7,
    DARWIN_CPU_TYPE_ARM        = 12,
    DARWIN_CPU_TYPE_POWERPC    = 18
  };

  Triple::ArchType Arch = TT.getArch();
  if (Arch == Triple::x86_64)
    CPUType = DARWIN_CPU_TYPE_X86 | DARWIN_CPU_ARCH_ABI64;
  else if (Arch == Triple::x86)
    CPUType = DARWIN_CPU_TYPE_X86;
  else if (Arch == Triple::ppc)
    CPUType = DARWIN_CPU_TYPE_POWERPC;
  else if (Arch == Triple::ppc64)
    CPUType = DARWIN_CPU_TYPE_POWERPC | DARWIN_CPU_ARCH_ABI64;
  else if (Arch == Triple::arm || Arch == Triple::thumb)
    CPUType = DARWIN_CPU_TYPE_ARM;

  // Traditional Bitcode starts after header.
  assert(Buffer.size() >= BWH_HeaderSize &&
         "Expected header size to be reserved");
  unsigned BCOffset = BWH_HeaderSize;
  unsigned BCSize = Buffer.size() - BWH_HeaderSize;

  // Write the magic and version.
  unsigned Position = 0;
  writeInt32ToBuffer(0x0B17C0DE, Buffer, Position);
  writeInt32ToBuffer(0, Buffer, Position); // Version.
  writeInt32ToBuffer(BCOffset, Buffer, Position);
  writeInt32ToBuffer(BCSize, Buffer, Position);
  writeInt32ToBuffer(CPUType, Buffer, Position);

  // If the file is not a multiple of 16 bytes, insert dummy padding.
  while (Buffer.size() & 15)
    Buffer.push_back(0);
}

/// Helper to write the header common to all bitcode files.
static void writeBitcodeHeader(BitstreamWriter &Stream) {
  // Emit the file header.
  Stream.Emit((unsigned)'B', 8);
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit(0x0, 4);
  Stream.Emit(0xC, 4);
  Stream.Emit(0xE, 4);
  Stream.Emit(0xD, 4);
}

BitcodeWriter::BitcodeWriter(SmallVectorImpl<char> &Buffer)
    : Stream(new BitstreamWriter(Buffer)) {
  writeBitcodeHeader(*Stream);
}

BitcodeWriter::BitcodeWriter(raw_ostream &FS)
    : Stream(new BitstreamWriter(FS, FlushThreshold)) {
  writeBitcodeHeader(*Stream);
}

BitcodeWriter::~BitcodeWriter() { assert(WroteStrtab); }

void BitcodeWriter::writeBlob(unsigned Block, unsigned Record, StringRef Blob) {
  Stream->EnterSubblock(Block, 3);

  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(Record));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  auto AbbrevNo = Stream->EmitAbbrev(std::move(Abbv));

  Stream->EmitRecordWithBlob(AbbrevNo, ArrayRef<uint64_t>{Record}, Blob);

  Stream->ExitBlock();
}

void BitcodeWriter::writeSymtab() {
  assert(!WroteStrtab && !WroteSymtab);

  // If any module has module-level inline asm, we will require a registered asm
  // parser for the target so that we can create an accurate symbol table for
  // the module.
  for (Module *M : Mods) {
    if (M->getModuleInlineAsm().empty())
      continue;

    std::string Err;
    const Triple TT(M->getTargetTriple());
    const Target *T = TargetRegistry::lookupTarget(TT.str(), Err);
    if (!T || !T->hasMCAsmParser())
      return;
  }

  WroteSymtab = true;
  SmallVector<char, 0> Symtab;
  // The irsymtab::build function may be unable to create a symbol table if the
  // module is malformed (e.g. it contains an invalid alias). Writing a symbol
  // table is not required for correctness, but we still want to be able to
  // write malformed modules to bitcode files, so swallow the error.
  if (Error E = irsymtab::build(Mods, Symtab, StrtabBuilder, Alloc)) {
    consumeError(std::move(E));
    return;
  }

  writeBlob(bitc::SYMTAB_BLOCK_ID, bitc::SYMTAB_BLOB,
            {Symtab.data(), Symtab.size()});
}

void BitcodeWriter::writeStrtab() {
  assert(!WroteStrtab);

  std::vector<char> Strtab;
  StrtabBuilder.finalizeInOrder();
  Strtab.resize(StrtabBuilder.getSize());
  StrtabBuilder.write((uint8_t *)Strtab.data());

  writeBlob(bitc::STRTAB_BLOCK_ID, bitc::STRTAB_BLOB,
            {Strtab.data(), Strtab.size()});

  WroteStrtab = true;
}

void BitcodeWriter::copyStrtab(StringRef Strtab) {
  writeBlob(bitc::STRTAB_BLOCK_ID, bitc::STRTAB_BLOB, Strtab);
  WroteStrtab = true;
}

void BitcodeWriter::writeModule(const Module &M,
                                bool ShouldPreserveUseListOrder,
                                const ModuleSummaryIndex *Index,
                                bool GenerateHash, ModuleHash *ModHash) {
  assert(!WroteStrtab);

  // The Mods vector is used by irsymtab::build, which requires non-const
  // Modules in case it needs to materialize metadata. But the bitcode writer
  // requires that the module is materialized, so we can cast to non-const here,
  // after checking that it is in fact materialized.
  assert(M.isMaterialized());
  Mods.push_back(const_cast<Module *>(&M));

  ModuleBitcodeWriter ModuleWriter(M, StrtabBuilder, *Stream,
                                   ShouldPreserveUseListOrder, Index,
                                   GenerateHash, ModHash);
  ModuleWriter.write();
}

void BitcodeWriter::writeIndex(
    const ModuleSummaryIndex *Index,
    const std::map<std::string, GVSummaryMapTy> *ModuleToSummariesForIndex,
    const GVSummaryPtrSet *DecSummaries) {
  IndexBitcodeWriter IndexWriter(*Stream, StrtabBuilder, *Index, DecSummaries,
                                 ModuleToSummariesForIndex);
  IndexWriter.write();
}

/// Write the specified module to the specified output stream.
void llvm::WriteBitcodeToFile(const Module &M, raw_ostream &Out,
                              bool ShouldPreserveUseListOrder,
                              const ModuleSummaryIndex *Index,
                              bool GenerateHash, ModuleHash *ModHash) {
  auto Write = [&](BitcodeWriter &Writer) {
    Writer.writeModule(M, ShouldPreserveUseListOrder, Index, GenerateHash,
                       ModHash);
    Writer.writeSymtab();
    Writer.writeStrtab();
  };
  Triple TT(M.getTargetTriple());
  if (TT.isOSDarwin() || TT.isOSBinFormatMachO()) {
    // If this is darwin or another generic macho target, reserve space for the
    // header. Note that the header is computed *after* the output is known, so
    // we currently explicitly use a buffer, write to it, and then subsequently
    // flush to Out.
    SmallVector<char, 0> Buffer;
    Buffer.reserve(256 * 1024);
    Buffer.insert(Buffer.begin(), BWH_HeaderSize, 0);
    BitcodeWriter Writer(Buffer);
    Write(Writer);
    emitDarwinBCHeaderAndTrailer(Buffer, TT);
    Out.write(Buffer.data(), Buffer.size());
  } else {
    BitcodeWriter Writer(Out);
    Write(Writer);
  }
}

void IndexBitcodeWriter::write() {
  Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);

  writeModuleVersion();

  // Write the module paths in the combined index.
  writeModStrings();

  // Write the summary combined index records.
  writeCombinedGlobalValueSummary();

  Stream.ExitBlock();
}

// Write the specified module summary index to the given raw output stream,
// where it will be written in a new bitcode block. This is used when
// writing the combined index file for ThinLTO. When writing a subset of the
// index for a distributed backend, provide a \p ModuleToSummariesForIndex map.
void llvm::writeIndexToFile(
    const ModuleSummaryIndex &Index, raw_ostream &Out,
    const std::map<std::string, GVSummaryMapTy> *ModuleToSummariesForIndex,
    const GVSummaryPtrSet *DecSummaries) {
  SmallVector<char, 0> Buffer;
  Buffer.reserve(256 * 1024);

  BitcodeWriter Writer(Buffer);
  Writer.writeIndex(&Index, ModuleToSummariesForIndex, DecSummaries);
  Writer.writeStrtab();

  Out.write((char *)&Buffer.front(), Buffer.size());
}

namespace {

/// Class to manage the bitcode writing for a thin link bitcode file.
class ThinLinkBitcodeWriter : public ModuleBitcodeWriterBase {
  /// ModHash is for use in ThinLTO incremental build, generated while writing
  /// the module bitcode file.
  const ModuleHash *ModHash;

public:
  ThinLinkBitcodeWriter(const Module &M, StringTableBuilder &StrtabBuilder,
                        BitstreamWriter &Stream,
                        const ModuleSummaryIndex &Index,
                        const ModuleHash &ModHash)
      : ModuleBitcodeWriterBase(M, StrtabBuilder, Stream,
                                /*ShouldPreserveUseListOrder=*/false, &Index),
        ModHash(&ModHash) {}

  void write();

private:
  void writeSimplifiedModuleInfo();
};

} // end anonymous namespace

// This function writes a simpilified module info for thin link bitcode file.
// It only contains the source file name along with the name(the offset and
// size in strtab) and linkage for global values. For the global value info
// entry, in order to keep linkage at offset 5, there are three zeros used
// as padding.
void ThinLinkBitcodeWriter::writeSimplifiedModuleInfo() {
  SmallVector<unsigned, 64> Vals;
  // Emit the module's source file name.
  {
    StringEncoding Bits = getStringEncoding(M.getSourceFileName());
    BitCodeAbbrevOp AbbrevOpToUse = BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8);
    if (Bits == SE_Char6)
      AbbrevOpToUse = BitCodeAbbrevOp(BitCodeAbbrevOp::Char6);
    else if (Bits == SE_Fixed7)
      AbbrevOpToUse = BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7);

    // MODULE_CODE_SOURCE_FILENAME: [namechar x N]
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::MODULE_CODE_SOURCE_FILENAME));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(AbbrevOpToUse);
    unsigned FilenameAbbrev = Stream.EmitAbbrev(std::move(Abbv));

    for (const auto P : M.getSourceFileName())
      Vals.push_back((unsigned char)P);

    Stream.EmitRecord(bitc::MODULE_CODE_SOURCE_FILENAME, Vals, FilenameAbbrev);
    Vals.clear();
  }

  // Emit the global variable information.
  for (const GlobalVariable &GV : M.globals()) {
    // GLOBALVAR: [strtab offset, strtab size, 0, 0, 0, linkage]
    Vals.push_back(StrtabBuilder.add(GV.getName()));
    Vals.push_back(GV.getName().size());
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(getEncodedLinkage(GV));

    Stream.EmitRecord(bitc::MODULE_CODE_GLOBALVAR, Vals);
    Vals.clear();
  }

  // Emit the function proto information.
  for (const Function &F : M) {
    // FUNCTION:  [strtab offset, strtab size, 0, 0, 0, linkage]
    Vals.push_back(StrtabBuilder.add(F.getName()));
    Vals.push_back(F.getName().size());
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(getEncodedLinkage(F));

    Stream.EmitRecord(bitc::MODULE_CODE_FUNCTION, Vals);
    Vals.clear();
  }

  // Emit the alias information.
  for (const GlobalAlias &A : M.aliases()) {
    // ALIAS: [strtab offset, strtab size, 0, 0, 0, linkage]
    Vals.push_back(StrtabBuilder.add(A.getName()));
    Vals.push_back(A.getName().size());
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(getEncodedLinkage(A));

    Stream.EmitRecord(bitc::MODULE_CODE_ALIAS, Vals);
    Vals.clear();
  }

  // Emit the ifunc information.
  for (const GlobalIFunc &I : M.ifuncs()) {
    // IFUNC: [strtab offset, strtab size, 0, 0, 0, linkage]
    Vals.push_back(StrtabBuilder.add(I.getName()));
    Vals.push_back(I.getName().size());
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(0);
    Vals.push_back(getEncodedLinkage(I));

    Stream.EmitRecord(bitc::MODULE_CODE_IFUNC, Vals);
    Vals.clear();
  }
}

void ThinLinkBitcodeWriter::write() {
  Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);

  writeModuleVersion();

  writeSimplifiedModuleInfo();

  writePerModuleGlobalValueSummary();

  // Write module hash.
  Stream.EmitRecord(bitc::MODULE_CODE_HASH, ArrayRef<uint32_t>(*ModHash));

  Stream.ExitBlock();
}

void BitcodeWriter::writeThinLinkBitcode(const Module &M,
                                         const ModuleSummaryIndex &Index,
                                         const ModuleHash &ModHash) {
  assert(!WroteStrtab);

  // The Mods vector is used by irsymtab::build, which requires non-const
  // Modules in case it needs to materialize metadata. But the bitcode writer
  // requires that the module is materialized, so we can cast to non-const here,
  // after checking that it is in fact materialized.
  assert(M.isMaterialized());
  Mods.push_back(const_cast<Module *>(&M));

  ThinLinkBitcodeWriter ThinLinkWriter(M, StrtabBuilder, *Stream, Index,
                                       ModHash);
  ThinLinkWriter.write();
}

// Write the specified thin link bitcode file to the given raw output stream,
// where it will be written in a new bitcode block. This is used when
// writing the per-module index file for ThinLTO.
void llvm::writeThinLinkBitcodeToFile(const Module &M, raw_ostream &Out,
                                      const ModuleSummaryIndex &Index,
                                      const ModuleHash &ModHash) {
  SmallVector<char, 0> Buffer;
  Buffer.reserve(256 * 1024);

  BitcodeWriter Writer(Buffer);
  Writer.writeThinLinkBitcode(M, Index, ModHash);
  Writer.writeSymtab();
  Writer.writeStrtab();

  Out.write((char *)&Buffer.front(), Buffer.size());
}

static const char *getSectionNameForBitcode(const Triple &T) {
  switch (T.getObjectFormat()) {
  case Triple::MachO:
    return "__LLVM,__bitcode";
  case Triple::COFF:
  case Triple::ELF:
  case Triple::Wasm:
  case Triple::UnknownObjectFormat:
    return ".llvmbc";
  case Triple::GOFF:
    llvm_unreachable("GOFF is not yet implemented");
    break;
  case Triple::SPIRV:
    if (T.getVendor() == Triple::AMD)
      return ".llvmbc";
    llvm_unreachable("SPIRV is not yet implemented");
    break;
  case Triple::XCOFF:
    llvm_unreachable("XCOFF is not yet implemented");
    break;
  case Triple::DXContainer:
    llvm_unreachable("DXContainer is not yet implemented");
    break;
  }
  llvm_unreachable("Unimplemented ObjectFormatType");
}

static const char *getSectionNameForCommandline(const Triple &T) {
  switch (T.getObjectFormat()) {
  case Triple::MachO:
    return "__LLVM,__cmdline";
  case Triple::COFF:
  case Triple::ELF:
  case Triple::Wasm:
  case Triple::UnknownObjectFormat:
    return ".llvmcmd";
  case Triple::GOFF:
    llvm_unreachable("GOFF is not yet implemented");
    break;
  case Triple::SPIRV:
    if (T.getVendor() == Triple::AMD)
      return ".llvmcmd";
    llvm_unreachable("SPIRV is not yet implemented");
    break;
  case Triple::XCOFF:
    llvm_unreachable("XCOFF is not yet implemented");
    break;
  case Triple::DXContainer:
    llvm_unreachable("DXC is not yet implemented");
    break;
  }
  llvm_unreachable("Unimplemented ObjectFormatType");
}

void llvm::embedBitcodeInModule(llvm::Module &M, llvm::MemoryBufferRef Buf,
                                bool EmbedBitcode, bool EmbedCmdline,
                                const std::vector<uint8_t> &CmdArgs) {
  // Save llvm.compiler.used and remove it.
  SmallVector<Constant *, 2> UsedArray;
  SmallVector<GlobalValue *, 4> UsedGlobals;
  Type *UsedElementType = PointerType::getUnqual(M.getContext());
  GlobalVariable *Used = collectUsedGlobalVariables(M, UsedGlobals, true);
  for (auto *GV : UsedGlobals) {
    if (GV->getName() != "llvm.embedded.module" &&
        GV->getName() != "llvm.cmdline")
      UsedArray.push_back(
          ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, UsedElementType));
  }
  if (Used)
    Used->eraseFromParent();

  // Embed the bitcode for the llvm module.
  std::string Data;
  ArrayRef<uint8_t> ModuleData;
  Triple T(M.getTargetTriple());

  if (EmbedBitcode) {
    if (Buf.getBufferSize() == 0 ||
        !isBitcode((const unsigned char *)Buf.getBufferStart(),
                   (const unsigned char *)Buf.getBufferEnd())) {
      // If the input is LLVM Assembly, bitcode is produced by serializing
      // the module. Use-lists order need to be preserved in this case.
      llvm::raw_string_ostream OS(Data);
      llvm::WriteBitcodeToFile(M, OS, /* ShouldPreserveUseListOrder */ true);
      ModuleData =
          ArrayRef<uint8_t>((const uint8_t *)OS.str().data(), OS.str().size());
    } else
      // If the input is LLVM bitcode, write the input byte stream directly.
      ModuleData = ArrayRef<uint8_t>((const uint8_t *)Buf.getBufferStart(),
                                     Buf.getBufferSize());
  }
  llvm::Constant *ModuleConstant =
      llvm::ConstantDataArray::get(M.getContext(), ModuleData);
  llvm::GlobalVariable *GV = new llvm::GlobalVariable(
      M, ModuleConstant->getType(), true, llvm::GlobalValue::PrivateLinkage,
      ModuleConstant);
  GV->setSection(getSectionNameForBitcode(T));
  // Set alignment to 1 to prevent padding between two contributions from input
  // sections after linking.
  GV->setAlignment(Align(1));
  UsedArray.push_back(
      ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, UsedElementType));
  if (llvm::GlobalVariable *Old =
          M.getGlobalVariable("llvm.embedded.module", true)) {
    assert(Old->hasZeroLiveUses() &&
           "llvm.embedded.module can only be used once in llvm.compiler.used");
    GV->takeName(Old);
    Old->eraseFromParent();
  } else {
    GV->setName("llvm.embedded.module");
  }

  // Skip if only bitcode needs to be embedded.
  if (EmbedCmdline) {
    // Embed command-line options.
    ArrayRef<uint8_t> CmdData(const_cast<uint8_t *>(CmdArgs.data()),
                              CmdArgs.size());
    llvm::Constant *CmdConstant =
        llvm::ConstantDataArray::get(M.getContext(), CmdData);
    GV = new llvm::GlobalVariable(M, CmdConstant->getType(), true,
                                  llvm::GlobalValue::PrivateLinkage,
                                  CmdConstant);
    GV->setSection(getSectionNameForCommandline(T));
    GV->setAlignment(Align(1));
    UsedArray.push_back(
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, UsedElementType));
    if (llvm::GlobalVariable *Old = M.getGlobalVariable("llvm.cmdline", true)) {
      assert(Old->hasZeroLiveUses() &&
             "llvm.cmdline can only be used once in llvm.compiler.used");
      GV->takeName(Old);
      Old->eraseFromParent();
    } else {
      GV->setName("llvm.cmdline");
    }
  }

  if (UsedArray.empty())
    return;

  // Recreate llvm.compiler.used.
  ArrayType *ATy = ArrayType::get(UsedElementType, UsedArray.size());
  auto *NewUsed = new GlobalVariable(
      M, ATy, false, llvm::GlobalValue::AppendingLinkage,
      llvm::ConstantArray::get(ATy, UsedArray), "llvm.compiler.used");
  NewUsed->setSection("llvm.metadata");
}
