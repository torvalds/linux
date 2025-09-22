//=== DWARFLinkerCompileUnit.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFLinkerCompileUnit.h"
#include "AcceleratorRecordsSaver.h"
#include "DIEAttributeCloner.h"
#include "DIEGenerator.h"
#include "DependencyTracker.h"
#include "SyntheticTypeNameBuilder.h"
#include "llvm/DWARFLinker/Utils.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include <utility>

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

CompileUnit::CompileUnit(LinkingGlobalData &GlobalData, unsigned ID,
                         StringRef ClangModuleName, DWARFFile &File,
                         OffsetToUnitTy UnitFromOffset,
                         dwarf::FormParams Format, llvm::endianness Endianess)
    : DwarfUnit(GlobalData, ID, ClangModuleName), File(File),
      getUnitFromOffset(UnitFromOffset), Stage(Stage::CreatedNotLoaded),
      AcceleratorRecords(&GlobalData.getAllocator()) {
  UnitName = File.FileName;
  setOutputFormat(Format, Endianess);
  getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
}

CompileUnit::CompileUnit(LinkingGlobalData &GlobalData, DWARFUnit &OrigUnit,
                         unsigned ID, StringRef ClangModuleName,
                         DWARFFile &File, OffsetToUnitTy UnitFromOffset,
                         dwarf::FormParams Format, llvm::endianness Endianess)
    : DwarfUnit(GlobalData, ID, ClangModuleName), File(File),
      OrigUnit(&OrigUnit), getUnitFromOffset(UnitFromOffset),
      Stage(Stage::CreatedNotLoaded),
      AcceleratorRecords(&GlobalData.getAllocator()) {
  setOutputFormat(Format, Endianess);
  getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);

  DWARFDie CUDie = OrigUnit.getUnitDIE();
  if (!CUDie)
    return;

  if (std::optional<DWARFFormValue> Val = CUDie.find(dwarf::DW_AT_language)) {
    uint16_t LangVal = dwarf::toUnsigned(Val, 0);
    if (isODRLanguage(LangVal))
      Language = LangVal;
  }

  if (!GlobalData.getOptions().NoODR && Language.has_value())
    NoODR = false;

  if (const char *CUName = CUDie.getName(DINameKind::ShortName))
    UnitName = CUName;
  else
    UnitName = File.FileName;
  SysRoot = dwarf::toStringRef(CUDie.find(dwarf::DW_AT_LLVM_sysroot)).str();
}

void CompileUnit::loadLineTable() {
  LineTablePtr = File.Dwarf->getLineTableForUnit(&getOrigUnit());
}

void CompileUnit::maybeResetToLoadedStage() {
  // Nothing to reset if stage is less than "Loaded".
  if (getStage() < Stage::Loaded)
    return;

  // Note: We need to do erasing for "Loaded" stage because
  // if live analysys failed then we will have "Loaded" stage
  // with marking from "LivenessAnalysisDone" stage partially
  // done. That marking should be cleared.

  for (DIEInfo &Info : DieInfoArray)
    Info.unsetFlagsWhichSetDuringLiveAnalysis();

  LowPc = std::nullopt;
  HighPc = 0;
  Labels.clear();
  Ranges.clear();
  Dependencies.reset(nullptr);

  if (getStage() < Stage::Cloned) {
    setStage(Stage::Loaded);
    return;
  }

  AcceleratorRecords.erase();
  AbbreviationsSet.clear();
  Abbreviations.clear();
  OutUnitDIE = nullptr;
  DebugAddrIndexMap.clear();

  for (uint64_t &Offset : OutDieOffsetArray)
    Offset = 0;
  for (TypeEntry *&Name : TypeEntries)
    Name = nullptr;
  eraseSections();

  setStage(Stage::CreatedNotLoaded);
}

bool CompileUnit::loadInputDIEs() {
  DWARFDie InputUnitDIE = getUnitDIE(false);
  if (!InputUnitDIE)
    return false;

  // load input dies, resize Info structures array.
  DieInfoArray.resize(getOrigUnit().getNumDIEs());
  OutDieOffsetArray.resize(getOrigUnit().getNumDIEs(), 0);
  if (!NoODR)
    TypeEntries.resize(getOrigUnit().getNumDIEs());
  return true;
}

void CompileUnit::analyzeDWARFStructureRec(const DWARFDebugInfoEntry *DieEntry,
                                           bool IsODRUnavailableFunctionScope) {
  CompileUnit::DIEInfo &DieInfo = getDIEInfo(DieEntry);

  for (const DWARFDebugInfoEntry *CurChild = getFirstChildEntry(DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = getSiblingEntry(CurChild)) {
    CompileUnit::DIEInfo &ChildInfo = getDIEInfo(CurChild);
    bool ChildIsODRUnavailableFunctionScope = IsODRUnavailableFunctionScope;

    if (DieInfo.getIsInMouduleScope())
      ChildInfo.setIsInMouduleScope();

    if (DieInfo.getIsInFunctionScope())
      ChildInfo.setIsInFunctionScope();

    if (DieInfo.getIsInAnonNamespaceScope())
      ChildInfo.setIsInAnonNamespaceScope();

    switch (CurChild->getTag()) {
    case dwarf::DW_TAG_module:
      ChildInfo.setIsInMouduleScope();
      if (DieEntry->getTag() == dwarf::DW_TAG_compile_unit &&
          dwarf::toString(find(CurChild, dwarf::DW_AT_name), "") !=
              getClangModuleName())
        analyzeImportedModule(CurChild);
      break;
    case dwarf::DW_TAG_subprogram:
      ChildInfo.setIsInFunctionScope();
      if (!ChildIsODRUnavailableFunctionScope &&
          !ChildInfo.getIsInMouduleScope()) {
        if (find(CurChild,
                 {dwarf::DW_AT_abstract_origin, dwarf::DW_AT_specification}))
          ChildIsODRUnavailableFunctionScope = true;
      }
      break;
    case dwarf::DW_TAG_namespace: {
      UnitEntryPairTy NamespaceEntry = {this, CurChild};

      if (find(CurChild, dwarf::DW_AT_extension))
        NamespaceEntry = NamespaceEntry.getNamespaceOrigin();

      if (!NamespaceEntry.CU->find(NamespaceEntry.DieEntry, dwarf::DW_AT_name))
        ChildInfo.setIsInAnonNamespaceScope();
    } break;
    default:
      break;
    }

    if (!isClangModule() && !getGlobalData().getOptions().UpdateIndexTablesOnly)
      ChildInfo.setTrackLiveness();

    if ((!ChildInfo.getIsInAnonNamespaceScope() &&
         !ChildIsODRUnavailableFunctionScope && !NoODR))
      ChildInfo.setODRAvailable();

    if (CurChild->hasChildren())
      analyzeDWARFStructureRec(CurChild, ChildIsODRUnavailableFunctionScope);
  }
}

StringEntry *CompileUnit::getFileName(unsigned FileIdx,
                                      StringPool &GlobalStrings) {
  if (LineTablePtr) {
    if (LineTablePtr->hasFileAtIndex(FileIdx)) {
      // Cache the resolved paths based on the index in the line table,
      // because calling realpath is expensive.
      ResolvedPathsMap::const_iterator It = ResolvedFullPaths.find(FileIdx);
      if (It == ResolvedFullPaths.end()) {
        std::string OrigFileName;
        bool FoundFileName = LineTablePtr->getFileNameByIndex(
            FileIdx, getOrigUnit().getCompilationDir(),
            DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
            OrigFileName);
        (void)FoundFileName;
        assert(FoundFileName && "Must get file name from line table");

        // Second level of caching, this time based on the file's parent
        // path.
        StringRef FileName = sys::path::filename(OrigFileName);
        StringRef ParentPath = sys::path::parent_path(OrigFileName);

        // If the ParentPath has not yet been resolved, resolve and cache it for
        // future look-ups.
        StringMap<StringEntry *>::iterator ParentIt =
            ResolvedParentPaths.find(ParentPath);
        if (ParentIt == ResolvedParentPaths.end()) {
          SmallString<256> RealPath;
          sys::fs::real_path(ParentPath, RealPath);
          ParentIt =
              ResolvedParentPaths
                  .insert({ParentPath, GlobalStrings.insert(RealPath).first})
                  .first;
        }

        // Join the file name again with the resolved path.
        SmallString<256> ResolvedPath(ParentIt->second->first());
        sys::path::append(ResolvedPath, FileName);

        It = ResolvedFullPaths
                 .insert(std::make_pair(
                     FileIdx, GlobalStrings.insert(ResolvedPath).first))
                 .first;
      }

      return It->second;
    }
  }

  return nullptr;
}

void CompileUnit::cleanupDataAfterClonning() {
  AbbreviationsSet.clear();
  ResolvedFullPaths.shrink_and_clear();
  ResolvedParentPaths.clear();
  FileNames.shrink_and_clear();
  DieInfoArray = SmallVector<DIEInfo>();
  OutDieOffsetArray = SmallVector<uint64_t>();
  TypeEntries = SmallVector<TypeEntry *>();
  Dependencies.reset(nullptr);
  getOrigUnit().clear();
}

/// Collect references to parseable Swift interfaces in imported
/// DW_TAG_module blocks.
void CompileUnit::analyzeImportedModule(const DWARFDebugInfoEntry *DieEntry) {
  if (!Language || Language != dwarf::DW_LANG_Swift)
    return;

  if (!GlobalData.getOptions().ParseableSwiftInterfaces)
    return;

  StringRef Path =
      dwarf::toStringRef(find(DieEntry, dwarf::DW_AT_LLVM_include_path));
  if (!Path.ends_with(".swiftinterface"))
    return;
  // Don't track interfaces that are part of the SDK.
  StringRef SysRoot =
      dwarf::toStringRef(find(DieEntry, dwarf::DW_AT_LLVM_sysroot));
  if (SysRoot.empty())
    SysRoot = getSysRoot();
  if (!SysRoot.empty() && Path.starts_with(SysRoot))
    return;
  // Don't track interfaces that are part of the toolchain.
  // For example: Swift, _Concurrency, ...
  StringRef DeveloperDir = guessDeveloperDir(SysRoot);
  if (!DeveloperDir.empty() && Path.starts_with(DeveloperDir))
    return;
  if (isInToolchainDir(Path))
    return;
  if (std::optional<DWARFFormValue> Val = find(DieEntry, dwarf::DW_AT_name)) {
    Expected<const char *> Name = Val->getAsCString();
    if (!Name) {
      warn(Name.takeError());
      return;
    }

    auto &Entry = (*GlobalData.getOptions().ParseableSwiftInterfaces)[*Name];
    // The prepend path is applied later when copying.
    SmallString<128> ResolvedPath;
    if (sys::path::is_relative(Path))
      sys::path::append(
          ResolvedPath,
          dwarf::toString(getUnitDIE().find(dwarf::DW_AT_comp_dir), ""));
    sys::path::append(ResolvedPath, Path);
    if (!Entry.empty() && Entry != ResolvedPath) {
      DWARFDie Die = getDIE(DieEntry);
      warn(Twine("conflicting parseable interfaces for Swift Module ") + *Name +
               ": " + Entry + " and " + Path + ".",
           &Die);
    }
    Entry = std::string(ResolvedPath);
  }
}

Error CompileUnit::assignTypeNames(TypePool &TypePoolRef) {
  if (!getUnitDIE().isValid())
    return Error::success();

  SyntheticTypeNameBuilder NameBuilder(TypePoolRef);
  return assignTypeNamesRec(getDebugInfoEntry(0), NameBuilder);
}

Error CompileUnit::assignTypeNamesRec(const DWARFDebugInfoEntry *DieEntry,
                                      SyntheticTypeNameBuilder &NameBuilder) {
  OrderedChildrenIndexAssigner ChildrenIndexAssigner(*this, DieEntry);
  for (const DWARFDebugInfoEntry *CurChild = getFirstChildEntry(DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = getSiblingEntry(CurChild)) {
    CompileUnit::DIEInfo &ChildInfo = getDIEInfo(CurChild);
    if (!ChildInfo.needToPlaceInTypeTable())
      continue;

    assert(ChildInfo.getODRAvailable());
    if (Error Err = NameBuilder.assignName(
            {this, CurChild},
            ChildrenIndexAssigner.getChildIndex(*this, CurChild)))
      return Err;

    if (Error Err = assignTypeNamesRec(CurChild, NameBuilder))
      return Err;
  }

  return Error::success();
}

void CompileUnit::updateDieRefPatchesWithClonedOffsets() {
  if (std::optional<SectionDescriptor *> DebugInfoSection =
          tryGetSectionDescriptor(DebugSectionKind::DebugInfo)) {

    (*DebugInfoSection)
        ->ListDebugDieRefPatch.forEach([&](DebugDieRefPatch &Patch) {
          /// Replace stored DIE indexes with DIE output offsets.
          Patch.RefDieIdxOrClonedOffset =
              Patch.RefCU.getPointer()->getDieOutOffset(
                  Patch.RefDieIdxOrClonedOffset);
        });

    (*DebugInfoSection)
        ->ListDebugULEB128DieRefPatch.forEach(
            [&](DebugULEB128DieRefPatch &Patch) {
              /// Replace stored DIE indexes with DIE output offsets.
              Patch.RefDieIdxOrClonedOffset =
                  Patch.RefCU.getPointer()->getDieOutOffset(
                      Patch.RefDieIdxOrClonedOffset);
            });
  }

  if (std::optional<SectionDescriptor *> DebugLocSection =
          tryGetSectionDescriptor(DebugSectionKind::DebugLoc)) {
    (*DebugLocSection)
        ->ListDebugULEB128DieRefPatch.forEach(
            [](DebugULEB128DieRefPatch &Patch) {
              /// Replace stored DIE indexes with DIE output offsets.
              Patch.RefDieIdxOrClonedOffset =
                  Patch.RefCU.getPointer()->getDieOutOffset(
                      Patch.RefDieIdxOrClonedOffset);
            });
  }

  if (std::optional<SectionDescriptor *> DebugLocListsSection =
          tryGetSectionDescriptor(DebugSectionKind::DebugLocLists)) {
    (*DebugLocListsSection)
        ->ListDebugULEB128DieRefPatch.forEach(
            [](DebugULEB128DieRefPatch &Patch) {
              /// Replace stored DIE indexes with DIE output offsets.
              Patch.RefDieIdxOrClonedOffset =
                  Patch.RefCU.getPointer()->getDieOutOffset(
                      Patch.RefDieIdxOrClonedOffset);
            });
  }
}

std::optional<UnitEntryPairTy> CompileUnit::resolveDIEReference(
    const DWARFFormValue &RefValue,
    ResolveInterCUReferencesMode CanResolveInterCUReferences) {
  CompileUnit *RefCU;
  uint64_t RefDIEOffset;
  if (std::optional<uint64_t> Offset = RefValue.getAsRelativeReference()) {
    RefCU = this;
    RefDIEOffset = RefValue.getUnit()->getOffset() + *Offset;
  } else if (Offset = RefValue.getAsDebugInfoReference(); Offset) {
    RefCU = getUnitFromOffset(*Offset);
    RefDIEOffset = *Offset;
  } else {
    return std::nullopt;
  }

  if (RefCU == this) {
    // Referenced DIE is in current compile unit.
    if (std::optional<uint32_t> RefDieIdx = getDIEIndexForOffset(RefDIEOffset))
      return UnitEntryPairTy{this, getDebugInfoEntry(*RefDieIdx)};
  } else if (RefCU && CanResolveInterCUReferences) {
    // Referenced DIE is in other compile unit.

    // Check whether DIEs are loaded for that compile unit.
    enum Stage ReferredCUStage = RefCU->getStage();
    if (ReferredCUStage < Stage::Loaded || ReferredCUStage > Stage::Cloned)
      return UnitEntryPairTy{RefCU, nullptr};

    if (std::optional<uint32_t> RefDieIdx =
            RefCU->getDIEIndexForOffset(RefDIEOffset))
      return UnitEntryPairTy{RefCU, RefCU->getDebugInfoEntry(*RefDieIdx)};
  } else {
    return UnitEntryPairTy{RefCU, nullptr};
  }
  return std::nullopt;
}

std::optional<UnitEntryPairTy> CompileUnit::resolveDIEReference(
    const DWARFDebugInfoEntry *DieEntry, dwarf::Attribute Attr,
    ResolveInterCUReferencesMode CanResolveInterCUReferences) {
  if (std::optional<DWARFFormValue> AttrVal = find(DieEntry, Attr))
    return resolveDIEReference(*AttrVal, CanResolveInterCUReferences);

  return std::nullopt;
}

void CompileUnit::addFunctionRange(uint64_t FuncLowPc, uint64_t FuncHighPc,
                                   int64_t PcOffset) {
  std::lock_guard<std::mutex> Guard(RangesMutex);

  Ranges.insert({FuncLowPc, FuncHighPc}, PcOffset);
  if (LowPc)
    LowPc = std::min(*LowPc, FuncLowPc + PcOffset);
  else
    LowPc = FuncLowPc + PcOffset;
  this->HighPc = std::max(HighPc, FuncHighPc + PcOffset);
}

void CompileUnit::addLabelLowPc(uint64_t LabelLowPc, int64_t PcOffset) {
  std::lock_guard<std::mutex> Guard(LabelsMutex);
  Labels.insert({LabelLowPc, PcOffset});
}

Error CompileUnit::cloneAndEmitDebugLocations() {
  if (getGlobalData().getOptions().UpdateIndexTablesOnly)
    return Error::success();

  if (getOrigUnit().getVersion() < 5) {
    emitLocations(DebugSectionKind::DebugLoc);
    return Error::success();
  }

  emitLocations(DebugSectionKind::DebugLocLists);
  return Error::success();
}

void CompileUnit::emitLocations(DebugSectionKind LocationSectionKind) {
  SectionDescriptor &DebugInfoSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);

  if (!DebugInfoSection.ListDebugLocPatch.empty()) {
    SectionDescriptor &OutLocationSection =
        getOrCreateSectionDescriptor(LocationSectionKind);
    DWARFUnit &OrigUnit = getOrigUnit();

    uint64_t OffsetAfterUnitLength = emitLocListHeader(OutLocationSection);

    DebugInfoSection.ListDebugLocPatch.forEach([&](DebugLocPatch &Patch) {
      // Get location expressions vector corresponding to the current
      // attribute from the source DWARF.
      uint64_t InputDebugLocSectionOffset = DebugInfoSection.getIntVal(
          Patch.PatchOffset,
          DebugInfoSection.getFormParams().getDwarfOffsetByteSize());
      Expected<DWARFLocationExpressionsVector> OriginalLocations =
          OrigUnit.findLoclistFromOffset(InputDebugLocSectionOffset);

      if (!OriginalLocations) {
        warn(OriginalLocations.takeError());
        return;
      }

      LinkedLocationExpressionsVector LinkedLocationExpressions;
      for (DWARFLocationExpression &CurExpression : *OriginalLocations) {
        LinkedLocationExpressionsWithOffsetPatches LinkedExpression;

        if (CurExpression.Range) {
          // Relocate address range.
          LinkedExpression.Expression.Range = {
              CurExpression.Range->LowPC + Patch.AddrAdjustmentValue,
              CurExpression.Range->HighPC + Patch.AddrAdjustmentValue};
        }

        DataExtractor Data(CurExpression.Expr, OrigUnit.isLittleEndian(),
                           OrigUnit.getAddressByteSize());

        DWARFExpression InputExpression(Data, OrigUnit.getAddressByteSize(),
                                        OrigUnit.getFormParams().Format);
        cloneDieAttrExpression(InputExpression,
                               LinkedExpression.Expression.Expr,
                               OutLocationSection, Patch.AddrAdjustmentValue,
                               LinkedExpression.Patches);

        LinkedLocationExpressions.push_back({LinkedExpression});
      }

      // Emit locations list table fragment corresponding to the CurLocAttr.
      DebugInfoSection.apply(Patch.PatchOffset, dwarf::DW_FORM_sec_offset,
                             OutLocationSection.OS.tell());
      emitLocListFragment(LinkedLocationExpressions, OutLocationSection);
    });

    if (OffsetAfterUnitLength > 0) {
      assert(OffsetAfterUnitLength -
                 OutLocationSection.getFormParams().getDwarfOffsetByteSize() <
             OffsetAfterUnitLength);
      OutLocationSection.apply(
          OffsetAfterUnitLength -
              OutLocationSection.getFormParams().getDwarfOffsetByteSize(),
          dwarf::DW_FORM_sec_offset,
          OutLocationSection.OS.tell() - OffsetAfterUnitLength);
    }
  }
}

/// Emit debug locations(.debug_loc, .debug_loclists) header.
uint64_t CompileUnit::emitLocListHeader(SectionDescriptor &OutLocationSection) {
  if (getOrigUnit().getVersion() < 5)
    return 0;

  // unit_length.
  OutLocationSection.emitUnitLength(0xBADDEF);
  uint64_t OffsetAfterUnitLength = OutLocationSection.OS.tell();

  // Version.
  OutLocationSection.emitIntVal(5, 2);

  // Address size.
  OutLocationSection.emitIntVal(OutLocationSection.getFormParams().AddrSize, 1);

  // Seg_size
  OutLocationSection.emitIntVal(0, 1);

  // Offset entry count
  OutLocationSection.emitIntVal(0, 4);

  return OffsetAfterUnitLength;
}

/// Emit debug locations(.debug_loc, .debug_loclists) fragment.
uint64_t CompileUnit::emitLocListFragment(
    const LinkedLocationExpressionsVector &LinkedLocationExpression,
    SectionDescriptor &OutLocationSection) {
  uint64_t OffsetBeforeLocationExpression = 0;

  if (getOrigUnit().getVersion() < 5) {
    uint64_t BaseAddress = 0;
    if (std::optional<uint64_t> LowPC = getLowPc())
      BaseAddress = *LowPC;

    for (const LinkedLocationExpressionsWithOffsetPatches &LocExpression :
         LinkedLocationExpression) {
      if (LocExpression.Expression.Range) {
        OutLocationSection.emitIntVal(
            LocExpression.Expression.Range->LowPC - BaseAddress,
            OutLocationSection.getFormParams().AddrSize);
        OutLocationSection.emitIntVal(
            LocExpression.Expression.Range->HighPC - BaseAddress,
            OutLocationSection.getFormParams().AddrSize);
      }

      OutLocationSection.emitIntVal(LocExpression.Expression.Expr.size(), 2);
      OffsetBeforeLocationExpression = OutLocationSection.OS.tell();
      for (uint64_t *OffsetPtr : LocExpression.Patches)
        *OffsetPtr += OffsetBeforeLocationExpression;

      OutLocationSection.OS
          << StringRef((const char *)LocExpression.Expression.Expr.data(),
                       LocExpression.Expression.Expr.size());
    }

    // Emit the terminator entry.
    OutLocationSection.emitIntVal(0,
                                  OutLocationSection.getFormParams().AddrSize);
    OutLocationSection.emitIntVal(0,
                                  OutLocationSection.getFormParams().AddrSize);
    return OffsetBeforeLocationExpression;
  }

  std::optional<uint64_t> BaseAddress;
  for (const LinkedLocationExpressionsWithOffsetPatches &LocExpression :
       LinkedLocationExpression) {
    if (LocExpression.Expression.Range) {
      // Check whether base address is set. If it is not set yet
      // then set current base address and emit base address selection entry.
      if (!BaseAddress) {
        BaseAddress = LocExpression.Expression.Range->LowPC;

        // Emit base address.
        OutLocationSection.emitIntVal(dwarf::DW_LLE_base_addressx, 1);
        encodeULEB128(DebugAddrIndexMap.getValueIndex(*BaseAddress),
                      OutLocationSection.OS);
      }

      // Emit type of entry.
      OutLocationSection.emitIntVal(dwarf::DW_LLE_offset_pair, 1);

      // Emit start offset relative to base address.
      encodeULEB128(LocExpression.Expression.Range->LowPC - *BaseAddress,
                    OutLocationSection.OS);

      // Emit end offset relative to base address.
      encodeULEB128(LocExpression.Expression.Range->HighPC - *BaseAddress,
                    OutLocationSection.OS);
    } else
      // Emit type of entry.
      OutLocationSection.emitIntVal(dwarf::DW_LLE_default_location, 1);

    encodeULEB128(LocExpression.Expression.Expr.size(), OutLocationSection.OS);
    OffsetBeforeLocationExpression = OutLocationSection.OS.tell();
    for (uint64_t *OffsetPtr : LocExpression.Patches)
      *OffsetPtr += OffsetBeforeLocationExpression;

    OutLocationSection.OS << StringRef(
        (const char *)LocExpression.Expression.Expr.data(),
        LocExpression.Expression.Expr.size());
  }

  // Emit the terminator entry.
  OutLocationSection.emitIntVal(dwarf::DW_LLE_end_of_list, 1);
  return OffsetBeforeLocationExpression;
}

Error CompileUnit::emitDebugAddrSection() {
  if (GlobalData.getOptions().UpdateIndexTablesOnly)
    return Error::success();

  if (getVersion() < 5)
    return Error::success();

  if (DebugAddrIndexMap.empty())
    return Error::success();

  SectionDescriptor &OutAddrSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugAddr);

  // Emit section header.

  //   Emit length.
  OutAddrSection.emitUnitLength(0xBADDEF);
  uint64_t OffsetAfterSectionLength = OutAddrSection.OS.tell();

  //   Emit version.
  OutAddrSection.emitIntVal(5, 2);

  //   Emit address size.
  OutAddrSection.emitIntVal(getFormParams().AddrSize, 1);

  //   Emit segment size.
  OutAddrSection.emitIntVal(0, 1);

  // Emit addresses.
  for (uint64_t AddrValue : DebugAddrIndexMap.getValues())
    OutAddrSection.emitIntVal(AddrValue, getFormParams().AddrSize);

  // Patch section length.
  OutAddrSection.apply(
      OffsetAfterSectionLength -
          OutAddrSection.getFormParams().getDwarfOffsetByteSize(),
      dwarf::DW_FORM_sec_offset,
      OutAddrSection.OS.tell() - OffsetAfterSectionLength);

  return Error::success();
}

Error CompileUnit::cloneAndEmitRanges() {
  if (getGlobalData().getOptions().UpdateIndexTablesOnly)
    return Error::success();

  // Build set of linked address ranges for unit function ranges.
  AddressRanges LinkedFunctionRanges;
  for (const AddressRangeValuePair &Range : getFunctionRanges())
    LinkedFunctionRanges.insert(
        {Range.Range.start() + Range.Value, Range.Range.end() + Range.Value});

  emitAranges(LinkedFunctionRanges);

  if (getOrigUnit().getVersion() < 5) {
    cloneAndEmitRangeList(DebugSectionKind::DebugRange, LinkedFunctionRanges);
    return Error::success();
  }

  cloneAndEmitRangeList(DebugSectionKind::DebugRngLists, LinkedFunctionRanges);
  return Error::success();
}

void CompileUnit::cloneAndEmitRangeList(DebugSectionKind RngSectionKind,
                                        AddressRanges &LinkedFunctionRanges) {
  SectionDescriptor &DebugInfoSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
  SectionDescriptor &OutRangeSection =
      getOrCreateSectionDescriptor(RngSectionKind);

  if (!DebugInfoSection.ListDebugRangePatch.empty()) {
    std::optional<AddressRangeValuePair> CachedRange;
    uint64_t OffsetAfterUnitLength = emitRangeListHeader(OutRangeSection);

    DebugRangePatch *CompileUnitRangePtr = nullptr;
    DebugInfoSection.ListDebugRangePatch.forEach([&](DebugRangePatch &Patch) {
      if (Patch.IsCompileUnitRanges) {
        CompileUnitRangePtr = &Patch;
      } else {
        // Get ranges from the source DWARF corresponding to the current
        // attribute.
        AddressRanges LinkedRanges;
        uint64_t InputDebugRangesSectionOffset = DebugInfoSection.getIntVal(
            Patch.PatchOffset,
            DebugInfoSection.getFormParams().getDwarfOffsetByteSize());
        if (Expected<DWARFAddressRangesVector> InputRanges =
                getOrigUnit().findRnglistFromOffset(
                    InputDebugRangesSectionOffset)) {
          // Apply relocation adjustment.
          for (const auto &Range : *InputRanges) {
            if (!CachedRange || !CachedRange->Range.contains(Range.LowPC))
              CachedRange =
                  getFunctionRanges().getRangeThatContains(Range.LowPC);

            // All range entries should lie in the function range.
            if (!CachedRange) {
              warn("inconsistent range data.");
              continue;
            }

            // Store range for emiting.
            LinkedRanges.insert({Range.LowPC + CachedRange->Value,
                                 Range.HighPC + CachedRange->Value});
          }
        } else {
          llvm::consumeError(InputRanges.takeError());
          warn("invalid range list ignored.");
        }

        // Emit linked ranges.
        DebugInfoSection.apply(Patch.PatchOffset, dwarf::DW_FORM_sec_offset,
                               OutRangeSection.OS.tell());
        emitRangeListFragment(LinkedRanges, OutRangeSection);
      }
    });

    if (CompileUnitRangePtr != nullptr) {
      // Emit compile unit ranges last to be binary compatible with classic
      // dsymutil.
      DebugInfoSection.apply(CompileUnitRangePtr->PatchOffset,
                             dwarf::DW_FORM_sec_offset,
                             OutRangeSection.OS.tell());
      emitRangeListFragment(LinkedFunctionRanges, OutRangeSection);
    }

    if (OffsetAfterUnitLength > 0) {
      assert(OffsetAfterUnitLength -
                 OutRangeSection.getFormParams().getDwarfOffsetByteSize() <
             OffsetAfterUnitLength);
      OutRangeSection.apply(
          OffsetAfterUnitLength -
              OutRangeSection.getFormParams().getDwarfOffsetByteSize(),
          dwarf::DW_FORM_sec_offset,
          OutRangeSection.OS.tell() - OffsetAfterUnitLength);
    }
  }
}

uint64_t CompileUnit::emitRangeListHeader(SectionDescriptor &OutRangeSection) {
  if (OutRangeSection.getFormParams().Version < 5)
    return 0;

  // unit_length.
  OutRangeSection.emitUnitLength(0xBADDEF);
  uint64_t OffsetAfterUnitLength = OutRangeSection.OS.tell();

  // Version.
  OutRangeSection.emitIntVal(5, 2);

  // Address size.
  OutRangeSection.emitIntVal(OutRangeSection.getFormParams().AddrSize, 1);

  // Seg_size
  OutRangeSection.emitIntVal(0, 1);

  // Offset entry count
  OutRangeSection.emitIntVal(0, 4);

  return OffsetAfterUnitLength;
}

void CompileUnit::emitRangeListFragment(const AddressRanges &LinkedRanges,
                                        SectionDescriptor &OutRangeSection) {
  if (OutRangeSection.getFormParams().Version < 5) {
    // Emit ranges.
    uint64_t BaseAddress = 0;
    if (std::optional<uint64_t> LowPC = getLowPc())
      BaseAddress = *LowPC;

    for (const AddressRange &Range : LinkedRanges) {
      OutRangeSection.emitIntVal(Range.start() - BaseAddress,
                                 OutRangeSection.getFormParams().AddrSize);
      OutRangeSection.emitIntVal(Range.end() - BaseAddress,
                                 OutRangeSection.getFormParams().AddrSize);
    }

    // Add the terminator entry.
    OutRangeSection.emitIntVal(0, OutRangeSection.getFormParams().AddrSize);
    OutRangeSection.emitIntVal(0, OutRangeSection.getFormParams().AddrSize);
    return;
  }

  std::optional<uint64_t> BaseAddress;
  for (const AddressRange &Range : LinkedRanges) {
    if (!BaseAddress) {
      BaseAddress = Range.start();

      // Emit base address.
      OutRangeSection.emitIntVal(dwarf::DW_RLE_base_addressx, 1);
      encodeULEB128(getDebugAddrIndex(*BaseAddress), OutRangeSection.OS);
    }

    // Emit type of entry.
    OutRangeSection.emitIntVal(dwarf::DW_RLE_offset_pair, 1);

    // Emit start offset relative to base address.
    encodeULEB128(Range.start() - *BaseAddress, OutRangeSection.OS);

    // Emit end offset relative to base address.
    encodeULEB128(Range.end() - *BaseAddress, OutRangeSection.OS);
  }

  // Emit the terminator entry.
  OutRangeSection.emitIntVal(dwarf::DW_RLE_end_of_list, 1);
}

void CompileUnit::emitAranges(AddressRanges &LinkedFunctionRanges) {
  if (LinkedFunctionRanges.empty())
    return;

  SectionDescriptor &DebugInfoSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
  SectionDescriptor &OutArangesSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugARanges);

  // Emit Header.
  unsigned HeaderSize =
      sizeof(int32_t) + // Size of contents (w/o this field
      sizeof(int16_t) + // DWARF ARange version number
      sizeof(int32_t) + // Offset of CU in the .debug_info section
      sizeof(int8_t) +  // Pointer Size (in bytes)
      sizeof(int8_t);   // Segment Size (in bytes)

  unsigned TupleSize = OutArangesSection.getFormParams().AddrSize * 2;
  unsigned Padding = offsetToAlignment(HeaderSize, Align(TupleSize));

  OutArangesSection.emitOffset(0xBADDEF); // Aranges length
  uint64_t OffsetAfterArangesLengthField = OutArangesSection.OS.tell();

  OutArangesSection.emitIntVal(dwarf::DW_ARANGES_VERSION, 2); // Version number
  OutArangesSection.notePatch(
      DebugOffsetPatch{OutArangesSection.OS.tell(), &DebugInfoSection});
  OutArangesSection.emitOffset(0xBADDEF); // Corresponding unit's offset
  OutArangesSection.emitIntVal(OutArangesSection.getFormParams().AddrSize,
                               1);    // Address size
  OutArangesSection.emitIntVal(0, 1); // Segment size

  for (size_t Idx = 0; Idx < Padding; Idx++)
    OutArangesSection.emitIntVal(0, 1); // Padding

  // Emit linked ranges.
  for (const AddressRange &Range : LinkedFunctionRanges) {
    OutArangesSection.emitIntVal(Range.start(),
                                 OutArangesSection.getFormParams().AddrSize);
    OutArangesSection.emitIntVal(Range.end() - Range.start(),
                                 OutArangesSection.getFormParams().AddrSize);
  }

  // Emit terminator.
  OutArangesSection.emitIntVal(0, OutArangesSection.getFormParams().AddrSize);
  OutArangesSection.emitIntVal(0, OutArangesSection.getFormParams().AddrSize);

  uint64_t OffsetAfterArangesEnd = OutArangesSection.OS.tell();

  // Update Aranges lentgh.
  OutArangesSection.apply(
      OffsetAfterArangesLengthField -
          OutArangesSection.getFormParams().getDwarfOffsetByteSize(),
      dwarf::DW_FORM_sec_offset,
      OffsetAfterArangesEnd - OffsetAfterArangesLengthField);
}

Error CompileUnit::cloneAndEmitDebugMacro() {
  if (getOutUnitDIE() == nullptr)
    return Error::success();

  DWARFUnit &OrigUnit = getOrigUnit();
  DWARFDie OrigUnitDie = OrigUnit.getUnitDIE();

  // Check for .debug_macro table.
  if (std::optional<uint64_t> MacroAttr =
          dwarf::toSectionOffset(OrigUnitDie.find(dwarf::DW_AT_macros))) {
    if (const DWARFDebugMacro *Table =
            getContaingFile().Dwarf->getDebugMacro()) {
      emitMacroTableImpl(Table, *MacroAttr, true);
    }
  }

  // Check for .debug_macinfo table.
  if (std::optional<uint64_t> MacroAttr =
          dwarf::toSectionOffset(OrigUnitDie.find(dwarf::DW_AT_macro_info))) {
    if (const DWARFDebugMacro *Table =
            getContaingFile().Dwarf->getDebugMacinfo()) {
      emitMacroTableImpl(Table, *MacroAttr, false);
    }
  }

  return Error::success();
}

void CompileUnit::emitMacroTableImpl(const DWARFDebugMacro *MacroTable,
                                     uint64_t OffsetToMacroTable,
                                     bool hasDWARFv5Header) {
  SectionDescriptor &OutSection =
      hasDWARFv5Header
          ? getOrCreateSectionDescriptor(DebugSectionKind::DebugMacro)
          : getOrCreateSectionDescriptor(DebugSectionKind::DebugMacinfo);

  bool DefAttributeIsReported = false;
  bool UndefAttributeIsReported = false;
  bool ImportAttributeIsReported = false;

  for (const DWARFDebugMacro::MacroList &List : MacroTable->MacroLists) {
    if (OffsetToMacroTable == List.Offset) {
      // Write DWARFv5 header.
      if (hasDWARFv5Header) {
        // Write header version.
        OutSection.emitIntVal(List.Header.Version, sizeof(List.Header.Version));

        uint8_t Flags = List.Header.Flags;

        // Check for OPCODE_OPERANDS_TABLE.
        if (Flags &
            DWARFDebugMacro::HeaderFlagMask::MACRO_OPCODE_OPERANDS_TABLE) {
          Flags &=
              ~DWARFDebugMacro::HeaderFlagMask::MACRO_OPCODE_OPERANDS_TABLE;
          warn("opcode_operands_table is not supported yet.");
        }

        // Check for DEBUG_LINE_OFFSET.
        std::optional<uint64_t> StmtListOffset;
        if (Flags & DWARFDebugMacro::HeaderFlagMask::MACRO_DEBUG_LINE_OFFSET) {
          // Get offset to the line table from the cloned compile unit.
          for (auto &V : getOutUnitDIE()->values()) {
            if (V.getAttribute() == dwarf::DW_AT_stmt_list) {
              StmtListOffset = V.getDIEInteger().getValue();
              break;
            }
          }

          if (!StmtListOffset) {
            Flags &= ~DWARFDebugMacro::HeaderFlagMask::MACRO_DEBUG_LINE_OFFSET;
            warn("couldn`t find line table for macro table.");
          }
        }

        // Write flags.
        OutSection.emitIntVal(Flags, sizeof(Flags));

        // Write offset to line table.
        if (StmtListOffset) {
          OutSection.notePatch(DebugOffsetPatch{
              OutSection.OS.tell(),
              &getOrCreateSectionDescriptor(DebugSectionKind::DebugLine)});
          // TODO: check that List.Header.getOffsetByteSize() and
          // DebugOffsetPatch agree on size.
          OutSection.emitIntVal(0xBADDEF, List.Header.getOffsetByteSize());
        }
      }

      // Write macro entries.
      for (const DWARFDebugMacro::Entry &MacroEntry : List.Macros) {
        if (MacroEntry.Type == 0) {
          encodeULEB128(MacroEntry.Type, OutSection.OS);
          continue;
        }

        uint8_t MacroType = MacroEntry.Type;
        switch (MacroType) {
        default: {
          bool HasVendorSpecificExtension =
              (!hasDWARFv5Header &&
               MacroType == dwarf::DW_MACINFO_vendor_ext) ||
              (hasDWARFv5Header && (MacroType >= dwarf::DW_MACRO_lo_user &&
                                    MacroType <= dwarf::DW_MACRO_hi_user));

          if (HasVendorSpecificExtension) {
            // Write macinfo type.
            OutSection.emitIntVal(MacroType, 1);

            // Write vendor extension constant.
            encodeULEB128(MacroEntry.ExtConstant, OutSection.OS);

            // Write vendor extension string.
            OutSection.emitString(dwarf::DW_FORM_string, MacroEntry.ExtStr);
          } else
            warn("unknown macro type. skip.");
        } break;
        // debug_macro and debug_macinfo share some common encodings.
        // DW_MACRO_define     == DW_MACINFO_define
        // DW_MACRO_undef      == DW_MACINFO_undef
        // DW_MACRO_start_file == DW_MACINFO_start_file
        // DW_MACRO_end_file   == DW_MACINFO_end_file
        // For readibility/uniformity we are using DW_MACRO_*.
        case dwarf::DW_MACRO_define:
        case dwarf::DW_MACRO_undef: {
          // Write macinfo type.
          OutSection.emitIntVal(MacroType, 1);

          // Write source line.
          encodeULEB128(MacroEntry.Line, OutSection.OS);

          // Write macro string.
          OutSection.emitString(dwarf::DW_FORM_string, MacroEntry.MacroStr);
        } break;
        case dwarf::DW_MACRO_define_strp:
        case dwarf::DW_MACRO_undef_strp:
        case dwarf::DW_MACRO_define_strx:
        case dwarf::DW_MACRO_undef_strx: {
          // DW_MACRO_*_strx forms are not supported currently.
          // Convert to *_strp.
          switch (MacroType) {
          case dwarf::DW_MACRO_define_strx: {
            MacroType = dwarf::DW_MACRO_define_strp;
            if (!DefAttributeIsReported) {
              warn("DW_MACRO_define_strx unsupported yet. Convert to "
                   "DW_MACRO_define_strp.");
              DefAttributeIsReported = true;
            }
          } break;
          case dwarf::DW_MACRO_undef_strx: {
            MacroType = dwarf::DW_MACRO_undef_strp;
            if (!UndefAttributeIsReported) {
              warn("DW_MACRO_undef_strx unsupported yet. Convert to "
                   "DW_MACRO_undef_strp.");
              UndefAttributeIsReported = true;
            }
          } break;
          default:
            // Nothing to do.
            break;
          }

          // Write macinfo type.
          OutSection.emitIntVal(MacroType, 1);

          // Write source line.
          encodeULEB128(MacroEntry.Line, OutSection.OS);

          // Write macro string.
          OutSection.emitString(dwarf::DW_FORM_strp, MacroEntry.MacroStr);
          break;
        }
        case dwarf::DW_MACRO_start_file: {
          // Write macinfo type.
          OutSection.emitIntVal(MacroType, 1);
          // Write source line.
          encodeULEB128(MacroEntry.Line, OutSection.OS);
          // Write source file id.
          encodeULEB128(MacroEntry.File, OutSection.OS);
        } break;
        case dwarf::DW_MACRO_end_file: {
          // Write macinfo type.
          OutSection.emitIntVal(MacroType, 1);
        } break;
        case dwarf::DW_MACRO_import:
        case dwarf::DW_MACRO_import_sup: {
          if (!ImportAttributeIsReported) {
            warn("DW_MACRO_import and DW_MACRO_import_sup are unsupported "
                 "yet. remove.");
            ImportAttributeIsReported = true;
          }
        } break;
        }
      }

      return;
    }
  }
}

void CompileUnit::cloneDieAttrExpression(
    const DWARFExpression &InputExpression,
    SmallVectorImpl<uint8_t> &OutputExpression, SectionDescriptor &Section,
    std::optional<int64_t> VarAddressAdjustment,
    OffsetsPtrVector &PatchesOffsets) {
  using Encoding = DWARFExpression::Operation::Encoding;

  DWARFUnit &OrigUnit = getOrigUnit();
  uint8_t OrigAddressByteSize = OrigUnit.getAddressByteSize();

  uint64_t OpOffset = 0;
  for (auto &Op : InputExpression) {
    auto Desc = Op.getDescription();
    // DW_OP_const_type is variable-length and has 3
    // operands. Thus far we only support 2.
    if ((Desc.Op.size() == 2 && Desc.Op[0] == Encoding::BaseTypeRef) ||
        (Desc.Op.size() == 2 && Desc.Op[1] == Encoding::BaseTypeRef &&
         Desc.Op[0] != Encoding::Size1))
      warn("unsupported DW_OP encoding.");

    if ((Desc.Op.size() == 1 && Desc.Op[0] == Encoding::BaseTypeRef) ||
        (Desc.Op.size() == 2 && Desc.Op[1] == Encoding::BaseTypeRef &&
         Desc.Op[0] == Encoding::Size1)) {
      // This code assumes that the other non-typeref operand fits into 1 byte.
      assert(OpOffset < Op.getEndOffset());
      uint32_t ULEBsize = Op.getEndOffset() - OpOffset - 1;
      assert(ULEBsize <= 16);

      // Copy over the operation.
      assert(!Op.getSubCode() && "SubOps not yet supported");
      OutputExpression.push_back(Op.getCode());
      uint64_t RefOffset;
      if (Desc.Op.size() == 1) {
        RefOffset = Op.getRawOperand(0);
      } else {
        OutputExpression.push_back(Op.getRawOperand(0));
        RefOffset = Op.getRawOperand(1);
      }
      uint8_t ULEB[16];
      uint32_t Offset = 0;
      unsigned RealSize = 0;
      // Look up the base type. For DW_OP_convert, the operand may be 0 to
      // instead indicate the generic type. The same holds for
      // DW_OP_reinterpret, which is currently not supported.
      if (RefOffset > 0 || Op.getCode() != dwarf::DW_OP_convert) {
        RefOffset += OrigUnit.getOffset();
        uint32_t RefDieIdx = 0;
        if (std::optional<uint32_t> Idx =
                OrigUnit.getDIEIndexForOffset(RefOffset))
          RefDieIdx = *Idx;

        // Use fixed size for ULEB128 data, since we need to update that size
        // later with the proper offsets. Use 5 for DWARF32, 9 for DWARF64.
        ULEBsize = getFormParams().getDwarfOffsetByteSize() + 1;

        RealSize = encodeULEB128(0xBADDEF, ULEB, ULEBsize);

        Section.notePatchWithOffsetUpdate(
            DebugULEB128DieRefPatch(OutputExpression.size(), this, this,
                                    RefDieIdx),
            PatchesOffsets);
      } else
        RealSize = encodeULEB128(Offset, ULEB, ULEBsize);

      if (RealSize > ULEBsize) {
        // Emit the generic type as a fallback.
        RealSize = encodeULEB128(0, ULEB, ULEBsize);
        warn("base type ref doesn't fit.");
      }
      assert(RealSize == ULEBsize && "padding failed");
      ArrayRef<uint8_t> ULEBbytes(ULEB, ULEBsize);
      OutputExpression.append(ULEBbytes.begin(), ULEBbytes.end());
    } else if (!getGlobalData().getOptions().UpdateIndexTablesOnly &&
               Op.getCode() == dwarf::DW_OP_addrx) {
      if (std::optional<object::SectionedAddress> SA =
              OrigUnit.getAddrOffsetSectionItem(Op.getRawOperand(0))) {
        // DWARFLinker does not use addrx forms since it generates relocated
        // addresses. Replace DW_OP_addrx with DW_OP_addr here.
        // Argument of DW_OP_addrx should be relocated here as it is not
        // processed by applyValidRelocs.
        OutputExpression.push_back(dwarf::DW_OP_addr);
        uint64_t LinkedAddress =
            SA->Address + (VarAddressAdjustment ? *VarAddressAdjustment : 0);
        if (getEndianness() != llvm::endianness::native)
          sys::swapByteOrder(LinkedAddress);
        ArrayRef<uint8_t> AddressBytes(
            reinterpret_cast<const uint8_t *>(&LinkedAddress),
            OrigAddressByteSize);
        OutputExpression.append(AddressBytes.begin(), AddressBytes.end());
      } else
        warn("cann't read DW_OP_addrx operand.");
    } else if (!getGlobalData().getOptions().UpdateIndexTablesOnly &&
               Op.getCode() == dwarf::DW_OP_constx) {
      if (std::optional<object::SectionedAddress> SA =
              OrigUnit.getAddrOffsetSectionItem(Op.getRawOperand(0))) {
        // DWARFLinker does not use constx forms since it generates relocated
        // addresses. Replace DW_OP_constx with DW_OP_const[*]u here.
        // Argument of DW_OP_constx should be relocated here as it is not
        // processed by applyValidRelocs.
        std::optional<uint8_t> OutOperandKind;
        switch (OrigAddressByteSize) {
        case 2:
          OutOperandKind = dwarf::DW_OP_const2u;
          break;
        case 4:
          OutOperandKind = dwarf::DW_OP_const4u;
          break;
        case 8:
          OutOperandKind = dwarf::DW_OP_const8u;
          break;
        default:
          warn(
              formatv(("unsupported address size: {0}."), OrigAddressByteSize));
          break;
        }

        if (OutOperandKind) {
          OutputExpression.push_back(*OutOperandKind);
          uint64_t LinkedAddress =
              SA->Address + (VarAddressAdjustment ? *VarAddressAdjustment : 0);
          if (getEndianness() != llvm::endianness::native)
            sys::swapByteOrder(LinkedAddress);
          ArrayRef<uint8_t> AddressBytes(
              reinterpret_cast<const uint8_t *>(&LinkedAddress),
              OrigAddressByteSize);
          OutputExpression.append(AddressBytes.begin(), AddressBytes.end());
        }
      } else
        warn("cann't read DW_OP_constx operand.");
    } else {
      // Copy over everything else unmodified.
      StringRef Bytes =
          InputExpression.getData().slice(OpOffset, Op.getEndOffset());
      OutputExpression.append(Bytes.begin(), Bytes.end());
    }
    OpOffset = Op.getEndOffset();
  }
}

Error CompileUnit::cloneAndEmit(
    std::optional<std::reference_wrapper<const Triple>> TargetTriple,
    TypeUnit *ArtificialTypeUnit) {
  BumpPtrAllocator Allocator;

  DWARFDie OrigUnitDIE = getOrigUnit().getUnitDIE();
  if (!OrigUnitDIE.isValid())
    return Error::success();

  TypeEntry *RootEntry = nullptr;
  if (ArtificialTypeUnit)
    RootEntry = ArtificialTypeUnit->getTypePool().getRoot();

  // Clone input DIE entry recursively.
  std::pair<DIE *, TypeEntry *> OutCUDie = cloneDIE(
      OrigUnitDIE.getDebugInfoEntry(), RootEntry, getDebugInfoHeaderSize(),
      std::nullopt, std::nullopt, Allocator, ArtificialTypeUnit);
  setOutUnitDIE(OutCUDie.first);

  if (!TargetTriple.has_value() || (OutCUDie.first == nullptr))
    return Error::success();

  if (Error Err = cloneAndEmitLineTable((*TargetTriple).get()))
    return Err;

  if (Error Err = cloneAndEmitDebugMacro())
    return Err;

  getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
  if (Error Err = emitDebugInfo((*TargetTriple).get()))
    return Err;

  // ASSUMPTION: .debug_info section should already be emitted at this point.
  // cloneAndEmitRanges & cloneAndEmitDebugLocations use .debug_info section
  // data.

  if (Error Err = cloneAndEmitRanges())
    return Err;

  if (Error Err = cloneAndEmitDebugLocations())
    return Err;

  if (Error Err = emitDebugAddrSection())
    return Err;

  // Generate Pub accelerator tables.
  if (llvm::is_contained(GlobalData.getOptions().AccelTables,
                         DWARFLinker::AccelTableKind::Pub))
    emitPubAccelerators();

  if (Error Err = emitDebugStringOffsetSection())
    return Err;

  return emitAbbreviations();
}

std::pair<DIE *, TypeEntry *> CompileUnit::cloneDIE(
    const DWARFDebugInfoEntry *InputDieEntry, TypeEntry *ClonedParentTypeDIE,
    uint64_t OutOffset, std::optional<int64_t> FuncAddressAdjustment,
    std::optional<int64_t> VarAddressAdjustment, BumpPtrAllocator &Allocator,
    TypeUnit *ArtificialTypeUnit) {
  uint32_t InputDieIdx = getDIEIndex(InputDieEntry);
  CompileUnit::DIEInfo &Info = getDIEInfo(InputDieIdx);

  bool NeedToClonePlainDIE = Info.needToKeepInPlainDwarf();
  bool NeedToCloneTypeDIE =
      (InputDieEntry->getTag() != dwarf::DW_TAG_compile_unit) &&
      Info.needToPlaceInTypeTable();
  std::pair<DIE *, TypeEntry *> ClonedDIE;

  DIEGenerator PlainDIEGenerator(Allocator, *this);

  if (NeedToClonePlainDIE)
    // Create a cloned DIE which would be placed into the cloned version
    // of input compile unit.
    ClonedDIE.first = createPlainDIEandCloneAttributes(
        InputDieEntry, PlainDIEGenerator, OutOffset, FuncAddressAdjustment,
        VarAddressAdjustment);
  if (NeedToCloneTypeDIE) {
    // Create a cloned DIE which would be placed into the artificial type
    // unit.
    assert(ArtificialTypeUnit != nullptr);
    DIEGenerator TypeDIEGenerator(
        ArtificialTypeUnit->getTypePool().getThreadLocalAllocator(), *this);

    ClonedDIE.second = createTypeDIEandCloneAttributes(
        InputDieEntry, TypeDIEGenerator, ClonedParentTypeDIE,
        ArtificialTypeUnit);
  }
  TypeEntry *TypeParentForChild =
      ClonedDIE.second ? ClonedDIE.second : ClonedParentTypeDIE;

  bool HasPlainChildrenToClone =
      (ClonedDIE.first && Info.getKeepPlainChildren());

  bool HasTypeChildrenToClone =
      ((ClonedDIE.second ||
        InputDieEntry->getTag() == dwarf::DW_TAG_compile_unit) &&
       Info.getKeepTypeChildren());

  // Recursively clone children.
  if (HasPlainChildrenToClone || HasTypeChildrenToClone) {
    for (const DWARFDebugInfoEntry *CurChild =
             getFirstChildEntry(InputDieEntry);
         CurChild && CurChild->getAbbreviationDeclarationPtr();
         CurChild = getSiblingEntry(CurChild)) {
      std::pair<DIE *, TypeEntry *> ClonedChild = cloneDIE(
          CurChild, TypeParentForChild, OutOffset, FuncAddressAdjustment,
          VarAddressAdjustment, Allocator, ArtificialTypeUnit);

      if (ClonedChild.first) {
        OutOffset =
            ClonedChild.first->getOffset() + ClonedChild.first->getSize();
        PlainDIEGenerator.addChild(ClonedChild.first);
      }
    }
    assert(ClonedDIE.first == nullptr ||
           HasPlainChildrenToClone == ClonedDIE.first->hasChildren());

    // Account for the end of children marker.
    if (HasPlainChildrenToClone)
      OutOffset += sizeof(int8_t);
  }

  // Update our size.
  if (ClonedDIE.first != nullptr)
    ClonedDIE.first->setSize(OutOffset - ClonedDIE.first->getOffset());

  return ClonedDIE;
}

DIE *CompileUnit::createPlainDIEandCloneAttributes(
    const DWARFDebugInfoEntry *InputDieEntry, DIEGenerator &PlainDIEGenerator,
    uint64_t &OutOffset, std::optional<int64_t> &FuncAddressAdjustment,
    std::optional<int64_t> &VarAddressAdjustment) {
  uint32_t InputDieIdx = getDIEIndex(InputDieEntry);
  CompileUnit::DIEInfo &Info = getDIEInfo(InputDieIdx);
  DIE *ClonedDIE = nullptr;
  bool HasLocationExpressionAddress = false;
  if (InputDieEntry->getTag() == dwarf::DW_TAG_subprogram) {
    // Get relocation adjustment value for the current function.
    FuncAddressAdjustment =
        getContaingFile().Addresses->getSubprogramRelocAdjustment(
            getDIE(InputDieEntry), false);
  } else if (InputDieEntry->getTag() == dwarf::DW_TAG_label) {
    // Get relocation adjustment value for the current label.
    std::optional<uint64_t> lowPC =
        dwarf::toAddress(find(InputDieEntry, dwarf::DW_AT_low_pc));
    if (lowPC) {
      LabelMapTy::iterator It = Labels.find(*lowPC);
      if (It != Labels.end())
        FuncAddressAdjustment = It->second;
    }
  } else if (InputDieEntry->getTag() == dwarf::DW_TAG_variable) {
    // Get relocation adjustment value for the current variable.
    std::pair<bool, std::optional<int64_t>> LocExprAddrAndRelocAdjustment =
        getContaingFile().Addresses->getVariableRelocAdjustment(
            getDIE(InputDieEntry), false);

    HasLocationExpressionAddress = LocExprAddrAndRelocAdjustment.first;
    if (LocExprAddrAndRelocAdjustment.first &&
        LocExprAddrAndRelocAdjustment.second)
      VarAddressAdjustment = *LocExprAddrAndRelocAdjustment.second;
  }

  ClonedDIE = PlainDIEGenerator.createDIE(InputDieEntry->getTag(), OutOffset);

  // Offset to the DIE would be used after output DIE tree is deleted.
  // Thus we need to remember DIE offset separately.
  rememberDieOutOffset(InputDieIdx, OutOffset);

  // Clone Attributes.
  DIEAttributeCloner AttributesCloner(ClonedDIE, *this, this, InputDieEntry,
                                      PlainDIEGenerator, FuncAddressAdjustment,
                                      VarAddressAdjustment,
                                      HasLocationExpressionAddress);
  AttributesCloner.clone();

  // Remember accelerator info.
  AcceleratorRecordsSaver AccelRecordsSaver(getGlobalData(), *this, this);
  AccelRecordsSaver.save(InputDieEntry, ClonedDIE, AttributesCloner.AttrInfo,
                         nullptr);

  OutOffset =
      AttributesCloner.finalizeAbbreviations(Info.getKeepPlainChildren());

  return ClonedDIE;
}

/// Allocates output DIE for the specified \p TypeDescriptor.
DIE *CompileUnit::allocateTypeDie(TypeEntryBody *TypeDescriptor,
                                  DIEGenerator &TypeDIEGenerator,
                                  dwarf::Tag DieTag, bool IsDeclaration,
                                  bool IsParentDeclaration) {
  DIE *DefinitionDie = TypeDescriptor->Die;
  // Do not allocate any new DIE if definition DIE is already met.
  if (DefinitionDie)
    return nullptr;

  DIE *DeclarationDie = TypeDescriptor->DeclarationDie;
  bool OldParentIsDeclaration = TypeDescriptor->ParentIsDeclaration;

  if (IsDeclaration && !DeclarationDie) {
    // Alocate declaration DIE.
    DIE *NewDie = TypeDIEGenerator.createDIE(DieTag, 0);
    if (TypeDescriptor->DeclarationDie.compare_exchange_weak(DeclarationDie,
                                                             NewDie))
      return NewDie;
  } else if (IsDeclaration && !IsParentDeclaration && OldParentIsDeclaration) {
    // Overwrite existing declaration DIE if it's parent is also an declaration
    // while parent of current declaration DIE is a definition.
    if (TypeDescriptor->ParentIsDeclaration.compare_exchange_weak(
            OldParentIsDeclaration, false)) {
      DIE *NewDie = TypeDIEGenerator.createDIE(DieTag, 0);
      TypeDescriptor->DeclarationDie = NewDie;
      return NewDie;
    }
  } else if (!IsDeclaration && IsParentDeclaration && !DeclarationDie) {
    // Alocate declaration DIE since parent of current DIE is marked as
    // declaration.
    DIE *NewDie = TypeDIEGenerator.createDIE(DieTag, 0);
    if (TypeDescriptor->DeclarationDie.compare_exchange_weak(DeclarationDie,
                                                             NewDie))
      return NewDie;
  } else if (!IsDeclaration && !IsParentDeclaration) {
    // Allocate definition DIE.
    DIE *NewDie = TypeDIEGenerator.createDIE(DieTag, 0);
    if (TypeDescriptor->Die.compare_exchange_weak(DefinitionDie, NewDie)) {
      TypeDescriptor->ParentIsDeclaration = false;
      return NewDie;
    }
  }

  return nullptr;
}

TypeEntry *CompileUnit::createTypeDIEandCloneAttributes(
    const DWARFDebugInfoEntry *InputDieEntry, DIEGenerator &TypeDIEGenerator,
    TypeEntry *ClonedParentTypeDIE, TypeUnit *ArtificialTypeUnit) {
  assert(ArtificialTypeUnit != nullptr);
  uint32_t InputDieIdx = getDIEIndex(InputDieEntry);

  TypeEntry *Entry = getDieTypeEntry(InputDieIdx);
  assert(Entry != nullptr);
  assert(ClonedParentTypeDIE != nullptr);
  TypeEntryBody *EntryBody =
      ArtificialTypeUnit->getTypePool().getOrCreateTypeEntryBody(
          Entry, ClonedParentTypeDIE);
  assert(EntryBody);

  bool IsDeclaration =
      dwarf::toUnsigned(find(InputDieEntry, dwarf::DW_AT_declaration), 0);

  bool ParentIsDeclaration = false;
  if (std::optional<uint32_t> ParentIdx = InputDieEntry->getParentIdx())
    ParentIsDeclaration =
        dwarf::toUnsigned(find(*ParentIdx, dwarf::DW_AT_declaration), 0);

  DIE *OutDIE =
      allocateTypeDie(EntryBody, TypeDIEGenerator, InputDieEntry->getTag(),
                      IsDeclaration, ParentIsDeclaration);

  if (OutDIE != nullptr) {
    assert(ArtificialTypeUnit != nullptr);
    ArtificialTypeUnit->getSectionDescriptor(DebugSectionKind::DebugInfo);

    DIEAttributeCloner AttributesCloner(OutDIE, *this, ArtificialTypeUnit,
                                        InputDieEntry, TypeDIEGenerator,
                                        std::nullopt, std::nullopt, false);
    AttributesCloner.clone();

    // Remember accelerator info.
    AcceleratorRecordsSaver AccelRecordsSaver(getGlobalData(), *this,
                                              ArtificialTypeUnit);
    AccelRecordsSaver.save(InputDieEntry, OutDIE, AttributesCloner.AttrInfo,
                           Entry);

    // if AttributesCloner.getOutOffset() == 0 then we need to add
    // 1 to avoid assertion for zero size. We will subtract it back later.
    OutDIE->setSize(AttributesCloner.getOutOffset() + 1);
  }

  return Entry;
}

Error CompileUnit::cloneAndEmitLineTable(const Triple &TargetTriple) {
  const DWARFDebugLine::LineTable *InputLineTable =
      getContaingFile().Dwarf->getLineTableForUnit(&getOrigUnit());
  if (InputLineTable == nullptr) {
    if (getOrigUnit().getUnitDIE().find(dwarf::DW_AT_stmt_list))
      warn("cann't load line table.");
    return Error::success();
  }

  DWARFDebugLine::LineTable OutLineTable;

  // Set Line Table header.
  OutLineTable.Prologue = InputLineTable->Prologue;
  OutLineTable.Prologue.FormParams.AddrSize = getFormParams().AddrSize;

  // Set Line Table Rows.
  if (getGlobalData().getOptions().UpdateIndexTablesOnly) {
    OutLineTable.Rows = InputLineTable->Rows;
    // If all the line table contains is a DW_LNE_end_sequence, clear the line
    // table rows, it will be inserted again in the DWARFStreamer.
    if (OutLineTable.Rows.size() == 1 && OutLineTable.Rows[0].EndSequence)
      OutLineTable.Rows.clear();

    OutLineTable.Sequences = InputLineTable->Sequences;
  } else {
    // This vector is the output line table.
    std::vector<DWARFDebugLine::Row> NewRows;
    NewRows.reserve(InputLineTable->Rows.size());

    // Current sequence of rows being extracted, before being inserted
    // in NewRows.
    std::vector<DWARFDebugLine::Row> Seq;

    const auto &FunctionRanges = getFunctionRanges();
    std::optional<AddressRangeValuePair> CurrRange;

    // FIXME: This logic is meant to generate exactly the same output as
    // Darwin's classic dsymutil. There is a nicer way to implement this
    // by simply putting all the relocated line info in NewRows and simply
    // sorting NewRows before passing it to emitLineTableForUnit. This
    // should be correct as sequences for a function should stay
    // together in the sorted output. There are a few corner cases that
    // look suspicious though, and that required to implement the logic
    // this way. Revisit that once initial validation is finished.

    // Iterate over the object file line info and extract the sequences
    // that correspond to linked functions.
    for (DWARFDebugLine::Row Row : InputLineTable->Rows) {
      // Check whether we stepped out of the range. The range is
      // half-open, but consider accept the end address of the range if
      // it is marked as end_sequence in the input (because in that
      // case, the relocation offset is accurate and that entry won't
      // serve as the start of another function).
      if (!CurrRange || !CurrRange->Range.contains(Row.Address.Address)) {
        // We just stepped out of a known range. Insert a end_sequence
        // corresponding to the end of the range.
        uint64_t StopAddress =
            CurrRange ? CurrRange->Range.end() + CurrRange->Value : -1ULL;
        CurrRange = FunctionRanges.getRangeThatContains(Row.Address.Address);
        if (StopAddress != -1ULL && !Seq.empty()) {
          // Insert end sequence row with the computed end address, but
          // the same line as the previous one.
          auto NextLine = Seq.back();
          NextLine.Address.Address = StopAddress;
          NextLine.EndSequence = 1;
          NextLine.PrologueEnd = 0;
          NextLine.BasicBlock = 0;
          NextLine.EpilogueBegin = 0;
          Seq.push_back(NextLine);
          insertLineSequence(Seq, NewRows);
        }

        if (!CurrRange)
          continue;
      }

      // Ignore empty sequences.
      if (Row.EndSequence && Seq.empty())
        continue;

      // Relocate row address and add it to the current sequence.
      Row.Address.Address += CurrRange->Value;
      Seq.emplace_back(Row);

      if (Row.EndSequence)
        insertLineSequence(Seq, NewRows);
    }

    OutLineTable.Rows = std::move(NewRows);
  }

  return emitDebugLine(TargetTriple, OutLineTable);
}

void CompileUnit::insertLineSequence(std::vector<DWARFDebugLine::Row> &Seq,
                                     std::vector<DWARFDebugLine::Row> &Rows) {
  if (Seq.empty())
    return;

  if (!Rows.empty() && Rows.back().Address < Seq.front().Address) {
    llvm::append_range(Rows, Seq);
    Seq.clear();
    return;
  }

  object::SectionedAddress Front = Seq.front().Address;
  auto InsertPoint = partition_point(
      Rows, [=](const DWARFDebugLine::Row &O) { return O.Address < Front; });

  // FIXME: this only removes the unneeded end_sequence if the
  // sequences have been inserted in order. Using a global sort like
  // described in cloneAndEmitLineTable() and delaying the end_sequene
  // elimination to DebugLineEmitter::emit() we can get rid of all of them.
  if (InsertPoint != Rows.end() && InsertPoint->Address == Front &&
      InsertPoint->EndSequence) {
    *InsertPoint = Seq.front();
    Rows.insert(InsertPoint + 1, Seq.begin() + 1, Seq.end());
  } else {
    Rows.insert(InsertPoint, Seq.begin(), Seq.end());
  }

  Seq.clear();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void CompileUnit::DIEInfo::dump() {
  llvm::errs() << "{";
  llvm::errs() << "  Placement: ";
  switch (getPlacement()) {
  case NotSet:
    llvm::errs() << "NotSet";
    break;
  case TypeTable:
    llvm::errs() << "TypeTable";
    break;
  case PlainDwarf:
    llvm::errs() << "PlainDwarf";
    break;
  case Both:
    llvm::errs() << "Both";
    break;
  }

  llvm::errs() << "  Keep: " << getKeep();
  llvm::errs() << "  KeepPlainChildren: " << getKeepPlainChildren();
  llvm::errs() << "  KeepTypeChildren: " << getKeepTypeChildren();
  llvm::errs() << "  IsInMouduleScope: " << getIsInMouduleScope();
  llvm::errs() << "  IsInFunctionScope: " << getIsInFunctionScope();
  llvm::errs() << "  IsInAnonNamespaceScope: " << getIsInAnonNamespaceScope();
  llvm::errs() << "  ODRAvailable: " << getODRAvailable();
  llvm::errs() << "  TrackLiveness: " << getTrackLiveness();
  llvm::errs() << "}\n";
}
#endif // if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)

std::optional<std::pair<StringRef, StringRef>>
CompileUnit::getDirAndFilenameFromLineTable(
    const DWARFFormValue &FileIdxValue) {
  uint64_t FileIdx;
  if (std::optional<uint64_t> Val = FileIdxValue.getAsUnsignedConstant())
    FileIdx = *Val;
  else if (std::optional<int64_t> Val = FileIdxValue.getAsSignedConstant())
    FileIdx = *Val;
  else if (std::optional<uint64_t> Val = FileIdxValue.getAsSectionOffset())
    FileIdx = *Val;
  else
    return std::nullopt;

  return getDirAndFilenameFromLineTable(FileIdx);
}

std::optional<std::pair<StringRef, StringRef>>
CompileUnit::getDirAndFilenameFromLineTable(uint64_t FileIdx) {
  FileNamesCache::iterator FileData = FileNames.find(FileIdx);
  if (FileData != FileNames.end())
    return std::make_pair(StringRef(FileData->second.first),
                          StringRef(FileData->second.second));

  if (const DWARFDebugLine::LineTable *LineTable =
          getOrigUnit().getContext().getLineTableForUnit(&getOrigUnit())) {
    if (LineTable->hasFileAtIndex(FileIdx)) {

      const llvm::DWARFDebugLine::FileNameEntry &Entry =
          LineTable->Prologue.getFileNameEntry(FileIdx);

      Expected<const char *> Name = Entry.Name.getAsCString();
      if (!Name) {
        warn(Name.takeError());
        return std::nullopt;
      }

      std::string FileName = *Name;
      if (isPathAbsoluteOnWindowsOrPosix(FileName)) {
        FileNamesCache::iterator FileData =
            FileNames
                .insert(std::make_pair(
                    FileIdx,
                    std::make_pair(std::string(""), std::move(FileName))))
                .first;
        return std::make_pair(StringRef(FileData->second.first),
                              StringRef(FileData->second.second));
      }

      SmallString<256> FilePath;
      StringRef IncludeDir;
      // Be defensive about the contents of Entry.
      if (getVersion() >= 5) {
        // DirIdx 0 is the compilation directory, so don't include it for
        // relative names.
        if ((Entry.DirIdx != 0) &&
            Entry.DirIdx < LineTable->Prologue.IncludeDirectories.size()) {
          Expected<const char *> DirName =
              LineTable->Prologue.IncludeDirectories[Entry.DirIdx]
                  .getAsCString();
          if (DirName)
            IncludeDir = *DirName;
          else {
            warn(DirName.takeError());
            return std::nullopt;
          }
        }
      } else {
        if (0 < Entry.DirIdx &&
            Entry.DirIdx <= LineTable->Prologue.IncludeDirectories.size()) {
          Expected<const char *> DirName =
              LineTable->Prologue.IncludeDirectories[Entry.DirIdx - 1]
                  .getAsCString();
          if (DirName)
            IncludeDir = *DirName;
          else {
            warn(DirName.takeError());
            return std::nullopt;
          }
        }
      }

      StringRef CompDir = getOrigUnit().getCompilationDir();

      if (!CompDir.empty() && !isPathAbsoluteOnWindowsOrPosix(IncludeDir)) {
        sys::path::append(FilePath, sys::path::Style::native, CompDir);
      }

      sys::path::append(FilePath, sys::path::Style::native, IncludeDir);

      FileNamesCache::iterator FileData =
          FileNames
              .insert(
                  std::make_pair(FileIdx, std::make_pair(std::string(FilePath),
                                                         std::move(FileName))))
              .first;
      return std::make_pair(StringRef(FileData->second.first),
                            StringRef(FileData->second.second));
    }
  }

  return std::nullopt;
}

#define MAX_REFERENCIES_DEPTH 1000
UnitEntryPairTy UnitEntryPairTy::getNamespaceOrigin() {
  UnitEntryPairTy CUDiePair(*this);
  std::optional<UnitEntryPairTy> RefDiePair;
  int refDepth = 0;
  do {
    RefDiePair = CUDiePair.CU->resolveDIEReference(
        CUDiePair.DieEntry, dwarf::DW_AT_extension,
        ResolveInterCUReferencesMode::Resolve);
    if (!RefDiePair || !RefDiePair->DieEntry)
      return CUDiePair;

    CUDiePair = *RefDiePair;
  } while (refDepth++ < MAX_REFERENCIES_DEPTH);

  return CUDiePair;
}

std::optional<UnitEntryPairTy> UnitEntryPairTy::getParent() {
  if (std::optional<uint32_t> ParentIdx = DieEntry->getParentIdx())
    return UnitEntryPairTy{CU, CU->getDebugInfoEntry(*ParentIdx)};

  return std::nullopt;
}

CompileUnit::OutputUnitVariantPtr::OutputUnitVariantPtr(CompileUnit *U)
    : Ptr(U) {
  assert(U != nullptr);
}

CompileUnit::OutputUnitVariantPtr::OutputUnitVariantPtr(TypeUnit *U) : Ptr(U) {
  assert(U != nullptr);
}

DwarfUnit *CompileUnit::OutputUnitVariantPtr::operator->() {
  if (isCompileUnit())
    return getAsCompileUnit();
  else
    return getAsTypeUnit();
}

bool CompileUnit::OutputUnitVariantPtr::isCompileUnit() {
  return Ptr.is<CompileUnit *>();
}

bool CompileUnit::OutputUnitVariantPtr::isTypeUnit() {
  return Ptr.is<TypeUnit *>();
}

CompileUnit *CompileUnit::OutputUnitVariantPtr::getAsCompileUnit() {
  return Ptr.get<CompileUnit *>();
}

TypeUnit *CompileUnit::OutputUnitVariantPtr::getAsTypeUnit() {
  return Ptr.get<TypeUnit *>();
}

bool CompileUnit::resolveDependenciesAndMarkLiveness(
    bool InterCUProcessingStarted, std::atomic<bool> &HasNewInterconnectedCUs) {
  if (!Dependencies)
    Dependencies.reset(new DependencyTracker(*this));

  return Dependencies->resolveDependenciesAndMarkLiveness(
      InterCUProcessingStarted, HasNewInterconnectedCUs);
}

bool CompileUnit::updateDependenciesCompleteness() {
  assert(Dependencies.get());

  return Dependencies->updateDependenciesCompleteness();
}

void CompileUnit::verifyDependencies() {
  assert(Dependencies.get());

  Dependencies->verifyKeepChain();
}

ArrayRef<dwarf::Attribute> dwarf_linker::parallel::getODRAttributes() {
  static dwarf::Attribute ODRAttributes[] = {
      dwarf::DW_AT_type, dwarf::DW_AT_specification,
      dwarf::DW_AT_abstract_origin, dwarf::DW_AT_import};

  return ODRAttributes;
}
