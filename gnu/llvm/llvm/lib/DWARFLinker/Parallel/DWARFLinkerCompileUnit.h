//===- DWARFLinkerCompileUnit.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_DWARFLINKERCOMPILEUNIT_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_DWARFLINKERCOMPILEUNIT_H

#include "DWARFLinkerUnit.h"
#include "llvm/DWARFLinker/DWARFFile.h"
#include <optional>

namespace llvm {
namespace dwarf_linker {
namespace parallel {

using OffsetToUnitTy = function_ref<CompileUnit *(uint64_t Offset)>;

struct AttributesInfo;
class SyntheticTypeNameBuilder;
class DIEGenerator;
class TypeUnit;
class DependencyTracker;

class CompileUnit;

/// This is a helper structure which keeps a debug info entry
/// with it's containing compilation unit.
struct UnitEntryPairTy {
  UnitEntryPairTy() = default;
  UnitEntryPairTy(CompileUnit *CU, const DWARFDebugInfoEntry *DieEntry)
      : CU(CU), DieEntry(DieEntry) {}

  CompileUnit *CU = nullptr;
  const DWARFDebugInfoEntry *DieEntry = nullptr;

  UnitEntryPairTy getNamespaceOrigin();
  std::optional<UnitEntryPairTy> getParent();
};

enum ResolveInterCUReferencesMode : bool {
  Resolve = true,
  AvoidResolving = false,
};

/// Stores all information related to a compile unit, be it in its original
/// instance of the object file or its brand new cloned and generated DIE tree.
/// NOTE: we need alignment of at least 8 bytes as we use
///       PointerIntPair<CompileUnit *, 3> in the DependencyTracker.h
class alignas(8) CompileUnit : public DwarfUnit {
public:
  /// The stages of new compile unit processing.
  enum class Stage : uint8_t {
    /// Created, linked with input DWARF file.
    CreatedNotLoaded = 0,

    /// Input DWARF is loaded.
    Loaded,

    /// Input DWARF is analysed(DIEs pointing to the real code section are
    /// discovered, type names are assigned if ODR is requested).
    LivenessAnalysisDone,

    /// Check if dependencies have incompatible placement.
    /// If that is the case modify placement to be compatible.
    UpdateDependenciesCompleteness,

    /// Type names assigned to DIEs.
    TypeNamesAssigned,

    /// Output DWARF is generated.
    Cloned,

    /// Offsets inside patch records are updated.
    PatchesUpdated,

    /// Resources(Input DWARF, Output DWARF tree) are released.
    Cleaned,

    /// Compile Unit should be skipped
    Skipped
  };

  CompileUnit(LinkingGlobalData &GlobalData, unsigned ID,
              StringRef ClangModuleName, DWARFFile &File,
              OffsetToUnitTy UnitFromOffset, dwarf::FormParams Format,
              llvm::endianness Endianess);

  CompileUnit(LinkingGlobalData &GlobalData, DWARFUnit &OrigUnit, unsigned ID,
              StringRef ClangModuleName, DWARFFile &File,
              OffsetToUnitTy UnitFromOffset, dwarf::FormParams Format,
              llvm::endianness Endianess);

  /// Returns stage of overall processing.
  Stage getStage() const { return Stage; }

  /// Set stage of overall processing.
  void setStage(Stage Stage) { this->Stage = Stage; }

  /// Loads unit line table.
  void loadLineTable();

  /// Returns name of the file for the \p FileIdx
  /// from the unit`s line table.
  StringEntry *getFileName(unsigned FileIdx, StringPool &GlobalStrings);

  /// Returns DWARFFile containing this compile unit.
  const DWARFFile &getContaingFile() const { return File; }

  /// Load DIEs of input compilation unit. \returns true if input DIEs
  /// successfully loaded.
  bool loadInputDIEs();

  /// Reset compile units data(results of liveness analysis, clonning)
  /// if current stage greater than Stage::Loaded. We need to reset data
  /// as we are going to repeat stages.
  void maybeResetToLoadedStage();

  /// Collect references to parseable Swift interfaces in imported
  /// DW_TAG_module blocks.
  void analyzeImportedModule(const DWARFDebugInfoEntry *DieEntry);

  /// Navigate DWARF tree and set die properties.
  void analyzeDWARFStructure() {
    analyzeDWARFStructureRec(getUnitDIE().getDebugInfoEntry(), false);
  }

  /// Cleanup unneeded resources after compile unit is cloned.
  void cleanupDataAfterClonning();

  /// After cloning stage the output DIEs offsets are deallocated.
  /// This method copies output offsets for referenced DIEs into DIEs patches.
  void updateDieRefPatchesWithClonedOffsets();

  /// Search for subprograms and variables referencing live code and discover
  /// dependend DIEs. Mark live DIEs, set placement for DIEs.
  bool resolveDependenciesAndMarkLiveness(
      bool InterCUProcessingStarted,
      std::atomic<bool> &HasNewInterconnectedCUs);

  /// Check dependend DIEs for incompatible placement.
  /// Make placement to be consistent.
  bool updateDependenciesCompleteness();

  /// Check DIEs to have a consistent marking(keep marking, placement marking).
  void verifyDependencies();

  /// Search for type entries and assign names.
  Error assignTypeNames(TypePool &TypePoolRef);

  /// Kinds of placement for the output die.
  enum DieOutputPlacement : uint8_t {
    NotSet = 0,

    /// Corresponding DIE goes to the type table only.
    TypeTable = 1,

    /// Corresponding DIE goes to the plain dwarf only.
    PlainDwarf = 2,

    /// Corresponding DIE goes to type table and to plain dwarf.
    Both = 3,
  };

  /// Information gathered about source DIEs.
  struct DIEInfo {
    DIEInfo() = default;
    DIEInfo(const DIEInfo &Other) { Flags = Other.Flags.load(); }
    DIEInfo &operator=(const DIEInfo &Other) {
      Flags = Other.Flags.load();
      return *this;
    }

    /// Data member keeping various flags.
    std::atomic<uint16_t> Flags = {0};

    /// \returns Placement kind for the corresponding die.
    DieOutputPlacement getPlacement() const {
      return DieOutputPlacement(Flags & 0x7);
    }

    /// Sets Placement kind for the corresponding die.
    void setPlacement(DieOutputPlacement Placement) {
      auto InputData = Flags.load();
      while (!Flags.compare_exchange_weak(InputData,
                                          ((InputData & ~0x7) | Placement))) {
      }
    }

    /// Unsets Placement kind for the corresponding die.
    void unsetPlacement() {
      auto InputData = Flags.load();
      while (!Flags.compare_exchange_weak(InputData, (InputData & ~0x7))) {
      }
    }

    /// Sets Placement kind for the corresponding die.
    bool setPlacementIfUnset(DieOutputPlacement Placement) {
      auto InputData = Flags.load();
      if ((InputData & 0x7) == NotSet)
        if (Flags.compare_exchange_weak(InputData, (InputData | Placement)))
          return true;

      return false;
    }

#define SINGLE_FLAG_METHODS_SET(Name, Value)                                   \
  bool get##Name() const { return Flags & Value; }                             \
  void set##Name() {                                                           \
    auto InputData = Flags.load();                                             \
    while (!Flags.compare_exchange_weak(InputData, InputData | Value)) {       \
    }                                                                          \
  }                                                                            \
  void unset##Name() {                                                         \
    auto InputData = Flags.load();                                             \
    while (!Flags.compare_exchange_weak(InputData, InputData & ~Value)) {      \
    }                                                                          \
  }

    /// DIE is a part of the linked output.
    SINGLE_FLAG_METHODS_SET(Keep, 0x08)

    /// DIE has children which are part of the linked output.
    SINGLE_FLAG_METHODS_SET(KeepPlainChildren, 0x10)

    /// DIE has children which are part of the type table.
    SINGLE_FLAG_METHODS_SET(KeepTypeChildren, 0x20)

    /// DIE is in module scope.
    SINGLE_FLAG_METHODS_SET(IsInMouduleScope, 0x40)

    /// DIE is in function scope.
    SINGLE_FLAG_METHODS_SET(IsInFunctionScope, 0x80)

    /// DIE is in anonymous namespace scope.
    SINGLE_FLAG_METHODS_SET(IsInAnonNamespaceScope, 0x100)

    /// DIE is available for ODR type deduplication.
    SINGLE_FLAG_METHODS_SET(ODRAvailable, 0x200)

    /// Track liveness for the DIE.
    SINGLE_FLAG_METHODS_SET(TrackLiveness, 0x400)

    /// Track liveness for the DIE.
    SINGLE_FLAG_METHODS_SET(HasAnAddress, 0x800)

    void unsetFlagsWhichSetDuringLiveAnalysis() {
      auto InputData = Flags.load();
      while (!Flags.compare_exchange_weak(
          InputData, InputData & ~(0x7 | 0x8 | 0x10 | 0x20))) {
      }
    }

    /// Erase all flags.
    void eraseData() { Flags = 0; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    LLVM_DUMP_METHOD void dump();
#endif

    bool needToPlaceInTypeTable() const {
      return (getKeep() && (getPlacement() == CompileUnit::TypeTable ||
                            getPlacement() == CompileUnit::Both)) ||
             getKeepTypeChildren();
    }

    bool needToKeepInPlainDwarf() const {
      return (getKeep() && (getPlacement() == CompileUnit::PlainDwarf ||
                            getPlacement() == CompileUnit::Both)) ||
             getKeepPlainChildren();
    }
  };

  /// \defgroup Group of functions returning DIE info.
  ///
  /// @{

  /// \p Idx index of the DIE.
  /// \returns DieInfo descriptor.
  DIEInfo &getDIEInfo(unsigned Idx) { return DieInfoArray[Idx]; }

  /// \p Idx index of the DIE.
  /// \returns DieInfo descriptor.
  const DIEInfo &getDIEInfo(unsigned Idx) const { return DieInfoArray[Idx]; }

  /// \p Idx index of the DIE.
  /// \returns DieInfo descriptor.
  DIEInfo &getDIEInfo(const DWARFDebugInfoEntry *Entry) {
    return DieInfoArray[getOrigUnit().getDIEIndex(Entry)];
  }

  /// \p Idx index of the DIE.
  /// \returns DieInfo descriptor.
  const DIEInfo &getDIEInfo(const DWARFDebugInfoEntry *Entry) const {
    return DieInfoArray[getOrigUnit().getDIEIndex(Entry)];
  }

  /// \p Die
  /// \returns PlainDieInfo descriptor.
  DIEInfo &getDIEInfo(const DWARFDie &Die) {
    return DieInfoArray[getOrigUnit().getDIEIndex(Die)];
  }

  /// \p Die
  /// \returns PlainDieInfo descriptor.
  const DIEInfo &getDIEInfo(const DWARFDie &Die) const {
    return DieInfoArray[getOrigUnit().getDIEIndex(Die)];
  }

  /// \p Idx index of the DIE.
  /// \returns DieInfo descriptor.
  uint64_t getDieOutOffset(uint32_t Idx) {
    return reinterpret_cast<std::atomic<uint64_t> *>(&OutDieOffsetArray[Idx])
        ->load();
  }

  /// \p Idx index of the DIE.
  /// \returns type entry.
  TypeEntry *getDieTypeEntry(uint32_t Idx) {
    return reinterpret_cast<std::atomic<TypeEntry *> *>(&TypeEntries[Idx])
        ->load();
  }

  /// \p InputDieEntry debug info entry.
  /// \returns DieInfo descriptor.
  uint64_t getDieOutOffset(const DWARFDebugInfoEntry *InputDieEntry) {
    return reinterpret_cast<std::atomic<uint64_t> *>(
               &OutDieOffsetArray[getOrigUnit().getDIEIndex(InputDieEntry)])
        ->load();
  }

  /// \p InputDieEntry debug info entry.
  /// \returns type entry.
  TypeEntry *getDieTypeEntry(const DWARFDebugInfoEntry *InputDieEntry) {
    return reinterpret_cast<std::atomic<TypeEntry *> *>(
               &TypeEntries[getOrigUnit().getDIEIndex(InputDieEntry)])
        ->load();
  }

  /// \p Idx index of the DIE.
  /// \returns DieInfo descriptor.
  void rememberDieOutOffset(uint32_t Idx, uint64_t Offset) {
    reinterpret_cast<std::atomic<uint64_t> *>(&OutDieOffsetArray[Idx])
        ->store(Offset);
  }

  /// \p Idx index of the DIE.
  /// \p Type entry.
  void setDieTypeEntry(uint32_t Idx, TypeEntry *Entry) {
    reinterpret_cast<std::atomic<TypeEntry *> *>(&TypeEntries[Idx])
        ->store(Entry);
  }

  /// \p InputDieEntry debug info entry.
  /// \p Type entry.
  void setDieTypeEntry(const DWARFDebugInfoEntry *InputDieEntry,
                       TypeEntry *Entry) {
    reinterpret_cast<std::atomic<TypeEntry *> *>(
        &TypeEntries[getOrigUnit().getDIEIndex(InputDieEntry)])
        ->store(Entry);
  }

  /// @}

  /// Returns value of DW_AT_low_pc attribute.
  std::optional<uint64_t> getLowPc() const { return LowPc; }

  /// Returns value of DW_AT_high_pc attribute.
  uint64_t getHighPc() const { return HighPc; }

  /// Returns true if there is a label corresponding to the specified \p Addr.
  bool hasLabelAt(uint64_t Addr) const { return Labels.count(Addr); }

  /// Add the low_pc of a label that is relocated by applying
  /// offset \p PCOffset.
  void addLabelLowPc(uint64_t LabelLowPc, int64_t PcOffset);

  /// Resolve the DIE attribute reference that has been extracted in \p
  /// RefValue. The resulting DIE might be in another CompileUnit.
  /// \returns referenced die and corresponding compilation unit.
  ///          compilation unit is null if reference could not be resolved.
  std::optional<UnitEntryPairTy>
  resolveDIEReference(const DWARFFormValue &RefValue,
                      ResolveInterCUReferencesMode CanResolveInterCUReferences);

  std::optional<UnitEntryPairTy>
  resolveDIEReference(const DWARFDebugInfoEntry *DieEntry,
                      dwarf::Attribute Attr,
                      ResolveInterCUReferencesMode CanResolveInterCUReferences);

  /// @}

  /// Add a function range [\p LowPC, \p HighPC) that is relocated by applying
  /// offset \p PCOffset.
  void addFunctionRange(uint64_t LowPC, uint64_t HighPC, int64_t PCOffset);

  /// Returns function ranges of this unit.
  const RangesTy &getFunctionRanges() const { return Ranges; }

  /// Clone and emit this compilation unit.
  Error
  cloneAndEmit(std::optional<std::reference_wrapper<const Triple>> TargetTriple,
               TypeUnit *ArtificialTypeUnit);

  /// Clone and emit debug locations(.debug_loc/.debug_loclists).
  Error cloneAndEmitDebugLocations();

  /// Clone and emit ranges.
  Error cloneAndEmitRanges();

  /// Clone and emit debug macros(.debug_macinfo/.debug_macro).
  Error cloneAndEmitDebugMacro();

  // Clone input DIE entry.
  std::pair<DIE *, TypeEntry *>
  cloneDIE(const DWARFDebugInfoEntry *InputDieEntry,
           TypeEntry *ClonedParentTypeDIE, uint64_t OutOffset,
           std::optional<int64_t> FuncAddressAdjustment,
           std::optional<int64_t> VarAddressAdjustment,
           BumpPtrAllocator &Allocator, TypeUnit *ArtificialTypeUnit);

  // Clone and emit line table.
  Error cloneAndEmitLineTable(const Triple &TargetTriple);

  /// Clone attribute location axpression.
  void cloneDieAttrExpression(const DWARFExpression &InputExpression,
                              SmallVectorImpl<uint8_t> &OutputExpression,
                              SectionDescriptor &Section,
                              std::optional<int64_t> VarAddressAdjustment,
                              OffsetsPtrVector &PatchesOffsets);

  /// Returns index(inside .debug_addr) of an address.
  uint64_t getDebugAddrIndex(uint64_t Addr) {
    return DebugAddrIndexMap.getValueIndex(Addr);
  }

  /// Returns directory and file from the line table by index.
  std::optional<std::pair<StringRef, StringRef>>
  getDirAndFilenameFromLineTable(const DWARFFormValue &FileIdxValue);

  /// Returns directory and file from the line table by index.
  std::optional<std::pair<StringRef, StringRef>>
  getDirAndFilenameFromLineTable(uint64_t FileIdx);

  /// \defgroup Helper methods to access OrigUnit.
  ///
  /// @{

  /// Returns paired compile unit from input DWARF.
  DWARFUnit &getOrigUnit() const {
    assert(OrigUnit != nullptr);
    return *OrigUnit;
  }

  const DWARFDebugInfoEntry *
  getFirstChildEntry(const DWARFDebugInfoEntry *Die) const {
    assert(OrigUnit != nullptr);
    return OrigUnit->getFirstChildEntry(Die);
  }

  const DWARFDebugInfoEntry *
  getSiblingEntry(const DWARFDebugInfoEntry *Die) const {
    assert(OrigUnit != nullptr);
    return OrigUnit->getSiblingEntry(Die);
  }

  DWARFDie getParent(const DWARFDebugInfoEntry *Die) {
    assert(OrigUnit != nullptr);
    return OrigUnit->getParent(Die);
  }

  DWARFDie getDIEAtIndex(unsigned Index) {
    assert(OrigUnit != nullptr);
    return OrigUnit->getDIEAtIndex(Index);
  }

  const DWARFDebugInfoEntry *getDebugInfoEntry(unsigned Index) const {
    assert(OrigUnit != nullptr);
    return OrigUnit->getDebugInfoEntry(Index);
  }

  DWARFDie getUnitDIE(bool ExtractUnitDIEOnly = true) {
    assert(OrigUnit != nullptr);
    return OrigUnit->getUnitDIE(ExtractUnitDIEOnly);
  }

  DWARFDie getDIE(const DWARFDebugInfoEntry *Die) {
    assert(OrigUnit != nullptr);
    return DWARFDie(OrigUnit, Die);
  }

  uint32_t getDIEIndex(const DWARFDebugInfoEntry *Die) const {
    assert(OrigUnit != nullptr);
    return OrigUnit->getDIEIndex(Die);
  }

  uint32_t getDIEIndex(const DWARFDie &Die) const {
    assert(OrigUnit != nullptr);
    return OrigUnit->getDIEIndex(Die);
  }

  std::optional<DWARFFormValue> find(uint32_t DieIdx,
                                     ArrayRef<dwarf::Attribute> Attrs) const {
    assert(OrigUnit != nullptr);
    return find(OrigUnit->getDebugInfoEntry(DieIdx), Attrs);
  }

  std::optional<DWARFFormValue> find(const DWARFDebugInfoEntry *Die,
                                     ArrayRef<dwarf::Attribute> Attrs) const {
    if (!Die)
      return std::nullopt;
    auto AbbrevDecl = Die->getAbbreviationDeclarationPtr();
    if (AbbrevDecl) {
      for (auto Attr : Attrs) {
        if (auto Value = AbbrevDecl->getAttributeValue(Die->getOffset(), Attr,
                                                       *OrigUnit))
          return Value;
      }
    }
    return std::nullopt;
  }

  std::optional<uint32_t> getDIEIndexForOffset(uint64_t Offset) {
    return OrigUnit->getDIEIndexForOffset(Offset);
  }

  /// @}

  /// \defgroup Methods used for reporting warnings and errors:
  ///
  /// @{

  void warn(const Twine &Warning, const DWARFDie *DIE = nullptr) {
    GlobalData.warn(Warning, getUnitName(), DIE);
  }

  void warn(Error Warning, const DWARFDie *DIE = nullptr) {
    handleAllErrors(std::move(Warning), [&](ErrorInfoBase &Info) {
      GlobalData.warn(Info.message(), getUnitName(), DIE);
    });
  }

  void warn(const Twine &Warning, const DWARFDebugInfoEntry *DieEntry) {
    if (DieEntry != nullptr) {
      DWARFDie DIE(&getOrigUnit(), DieEntry);
      GlobalData.warn(Warning, getUnitName(), &DIE);
      return;
    }

    GlobalData.warn(Warning, getUnitName());
  }

  void error(const Twine &Err, const DWARFDie *DIE = nullptr) {
    GlobalData.warn(Err, getUnitName(), DIE);
  }

  void error(Error Err, const DWARFDie *DIE = nullptr) {
    handleAllErrors(std::move(Err), [&](ErrorInfoBase &Info) {
      GlobalData.error(Info.message(), getUnitName(), DIE);
    });
  }

  /// @}

  /// Save specified accelerator info \p Info.
  void saveAcceleratorInfo(const DwarfUnit::AccelInfo &Info) {
    AcceleratorRecords.add(Info);
  }

  /// Enumerates all units accelerator records.
  void
  forEachAcceleratorRecord(function_ref<void(AccelInfo &)> Handler) override {
    AcceleratorRecords.forEach(Handler);
  }

  /// Output unit selector.
  class OutputUnitVariantPtr {
  public:
    OutputUnitVariantPtr(CompileUnit *U);
    OutputUnitVariantPtr(TypeUnit *U);

    /// Accessor for common functionality.
    DwarfUnit *operator->();

    bool isCompileUnit();

    bool isTypeUnit();

    /// Returns CompileUnit if applicable.
    CompileUnit *getAsCompileUnit();

    /// Returns TypeUnit if applicable.
    TypeUnit *getAsTypeUnit();

  protected:
    PointerUnion<CompileUnit *, TypeUnit *> Ptr;
  };

private:
  /// Navigate DWARF tree recursively and set die properties.
  void analyzeDWARFStructureRec(const DWARFDebugInfoEntry *DieEntry,
                                bool IsODRUnavailableFunctionScope);

  struct LinkedLocationExpressionsWithOffsetPatches {
    DWARFLocationExpression Expression;
    OffsetsPtrVector Patches;
  };
  using LinkedLocationExpressionsVector =
      SmallVector<LinkedLocationExpressionsWithOffsetPatches>;

  /// Emit debug locations.
  void emitLocations(DebugSectionKind LocationSectionKind);

  /// Emit location list header.
  uint64_t emitLocListHeader(SectionDescriptor &OutLocationSection);

  /// Emit location list fragment.
  uint64_t emitLocListFragment(
      const LinkedLocationExpressionsVector &LinkedLocationExpression,
      SectionDescriptor &OutLocationSection);

  /// Emit the .debug_addr section fragment for current unit.
  Error emitDebugAddrSection();

  /// Emit .debug_aranges.
  void emitAranges(AddressRanges &LinkedFunctionRanges);

  /// Clone and emit .debug_ranges/.debug_rnglists.
  void cloneAndEmitRangeList(DebugSectionKind RngSectionKind,
                             AddressRanges &LinkedFunctionRanges);

  /// Emit range list header.
  uint64_t emitRangeListHeader(SectionDescriptor &OutRangeSection);

  /// Emit range list fragment.
  void emitRangeListFragment(const AddressRanges &LinkedRanges,
                             SectionDescriptor &OutRangeSection);

  /// Insert the new line info sequence \p Seq into the current
  /// set of already linked line info \p Rows.
  void insertLineSequence(std::vector<DWARFDebugLine::Row> &Seq,
                          std::vector<DWARFDebugLine::Row> &Rows);

  /// Emits body for both macro sections.
  void emitMacroTableImpl(const DWARFDebugMacro *MacroTable,
                          uint64_t OffsetToMacroTable, bool hasDWARFv5Header);

  /// Creates DIE which would be placed into the "Plain" compile unit.
  DIE *createPlainDIEandCloneAttributes(
      const DWARFDebugInfoEntry *InputDieEntry, DIEGenerator &PlainDIEGenerator,
      uint64_t &OutOffset, std::optional<int64_t> &FuncAddressAdjustment,
      std::optional<int64_t> &VarAddressAdjustment);

  /// Creates DIE which would be placed into the "Type" compile unit.
  TypeEntry *createTypeDIEandCloneAttributes(
      const DWARFDebugInfoEntry *InputDieEntry, DIEGenerator &TypeDIEGenerator,
      TypeEntry *ClonedParentTypeDIE, TypeUnit *ArtificialTypeUnit);

  /// Create output DIE inside specified \p TypeDescriptor.
  DIE *allocateTypeDie(TypeEntryBody *TypeDescriptor,
                       DIEGenerator &TypeDIEGenerator, dwarf::Tag DieTag,
                       bool IsDeclaration, bool IsParentDeclaration);

  /// Enumerate \p DieEntry children and assign names for them.
  Error assignTypeNamesRec(const DWARFDebugInfoEntry *DieEntry,
                           SyntheticTypeNameBuilder &NameBuilder);

  /// DWARFFile containing this compile unit.
  DWARFFile &File;

  /// Pointer to the paired compile unit from the input DWARF.
  DWARFUnit *OrigUnit = nullptr;

  /// The DW_AT_language of this unit.
  std::optional<uint16_t> Language;

  /// Line table for this unit.
  const DWARFDebugLine::LineTable *LineTablePtr = nullptr;

  /// Cached resolved paths from the line table.
  /// The key is <UniqueUnitID, FileIdx>.
  using ResolvedPathsMap = DenseMap<unsigned, StringEntry *>;
  ResolvedPathsMap ResolvedFullPaths;
  StringMap<StringEntry *> ResolvedParentPaths;

  /// Maps an address into the index inside .debug_addr section.
  IndexedValuesMap<uint64_t> DebugAddrIndexMap;

  std::unique_ptr<DependencyTracker> Dependencies;

  /// \defgroup Data Members accessed asinchronously.
  ///
  /// @{
  OffsetToUnitTy getUnitFromOffset;

  std::optional<uint64_t> LowPc;
  uint64_t HighPc = 0;

  /// Flag indicating whether type de-duplication is forbidden.
  bool NoODR = true;

  /// The ranges in that map are the PC ranges for functions in this unit,
  /// associated with the PC offset to apply to the addresses to get
  /// the linked address.
  RangesTy Ranges;
  std::mutex RangesMutex;

  /// The DW_AT_low_pc of each DW_TAG_label.
  using LabelMapTy = SmallDenseMap<uint64_t, uint64_t, 1>;
  LabelMapTy Labels;
  std::mutex LabelsMutex;

  /// This field keeps current stage of overall compile unit processing.
  std::atomic<Stage> Stage;

  /// DIE info indexed by DIE index.
  SmallVector<DIEInfo> DieInfoArray;
  SmallVector<uint64_t> OutDieOffsetArray;
  SmallVector<TypeEntry *> TypeEntries;

  /// The list of accelerator records for this unit.
  ArrayList<AccelInfo> AcceleratorRecords;
  /// @}
};

/// \returns list of attributes referencing type DIEs which might be
/// deduplicated.
/// Note: it does not include DW_AT_containing_type attribute to avoid
/// infinite recursion.
ArrayRef<dwarf::Attribute> getODRAttributes();

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_DWARFLINKERCOMPILEUNIT_H
