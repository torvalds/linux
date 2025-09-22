//===- DWARFLinkerUnit.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_DWARFLINKERUNIT_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_DWARFLINKERUNIT_H

#include "DWARFLinkerGlobalData.h"
#include "OutputSections.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/DWARFLinker/IndexedValuesMap.h"
#include "llvm/DWARFLinker/Parallel/DWARFLinker.h"
#include "llvm/DWARFLinker/StringPool.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/LEB128.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

class DwarfUnit;
using MacroOffset2UnitMapTy = DenseMap<uint64_t, DwarfUnit *>;

/// Base class for all Dwarf units(Compile unit/Type table unit).
class DwarfUnit : public OutputSections {
public:
  virtual ~DwarfUnit() {}
  DwarfUnit(LinkingGlobalData &GlobalData, unsigned ID,
            StringRef ClangModuleName)
      : OutputSections(GlobalData), ID(ID), ClangModuleName(ClangModuleName),
        OutUnitDIE(nullptr) {}

  /// Unique id of the unit.
  unsigned getUniqueID() const { return ID; }

  /// Returns size of this(newly generated) compile unit.
  uint64_t getUnitSize() const { return UnitSize; }

  /// Returns this unit name.
  StringRef getUnitName() const { return UnitName; }

  /// Return the DW_AT_LLVM_sysroot of the compile unit or an empty StringRef.
  StringRef getSysRoot() { return SysRoot; }

  /// Return true if this compile unit is from Clang module.
  bool isClangModule() const { return !ClangModuleName.empty(); }

  /// Return Clang module name;
  const std::string &getClangModuleName() const { return ClangModuleName; }

  /// Return global data.
  LinkingGlobalData &getGlobalData() { return GlobalData; }

  /// Returns true if unit is inter-connected(it references/referenced by other
  /// unit).
  bool isInterconnectedCU() const { return IsInterconnectedCU; }

  /// Mark this unit as inter-connected(it references/referenced by other unit).
  void setInterconnectedCU() { IsInterconnectedCU = true; }

  /// Adds \p Abbrev into unit`s abbreviation table.
  void assignAbbrev(DIEAbbrev &Abbrev);

  /// Returns abbreviations for this compile unit.
  const std::vector<std::unique_ptr<DIEAbbrev>> &getAbbreviations() const {
    return Abbreviations;
  }

  /// Returns output unit DIE.
  DIE *getOutUnitDIE() { return OutUnitDIE; }

  /// Set output unit DIE.
  void setOutUnitDIE(DIE *UnitDie) {
    OutUnitDIE = UnitDie;

    if (OutUnitDIE != nullptr) {
      UnitSize = getDebugInfoHeaderSize() + OutUnitDIE->getSize();
      UnitTag = OutUnitDIE->getTag();
    }
  }

  /// Returns unit DWARF tag.
  dwarf::Tag getTag() const { return UnitTag; }

  /// \defgroup Methods used to emit unit's debug info:
  ///
  /// @{
  /// Emit unit's abbreviations.
  Error emitAbbreviations();

  /// Emit .debug_info section for unit DIEs.
  Error emitDebugInfo(const Triple &TargetTriple);

  /// Emit .debug_line section.
  Error emitDebugLine(const Triple &TargetTriple,
                      const DWARFDebugLine::LineTable &OutLineTable);

  /// Emit the .debug_str_offsets section for current unit.
  Error emitDebugStringOffsetSection();
  /// @}

  /// \defgroup Methods used for reporting warnings and errors:
  ///
  /// @{
  void warn(const Twine &Warning) { GlobalData.warn(Warning, getUnitName()); }

  void error(const Twine &Err) { GlobalData.warn(Err, getUnitName()); }
  /// @}

  /// \defgroup Methods and data members used for building accelerator tables:
  ///
  /// @{

  enum class AccelType : uint8_t { None, Name, Namespace, ObjC, Type };

  /// This structure keeps fields which would be used for creating accelerator
  /// table.
  struct AccelInfo {
    AccelInfo() {
      AvoidForPubSections = false;
      ObjcClassImplementation = false;
    }

    /// Name of the entry.
    StringEntry *String = nullptr;

    /// Output offset of the DIE this entry describes.
    uint64_t OutOffset;

    /// Hash of the fully qualified name.
    uint32_t QualifiedNameHash = 0;

    /// Tag of the DIE this entry describes.
    dwarf::Tag Tag = dwarf::DW_TAG_null;

    /// Type of this accelerator record.
    AccelType Type = AccelType::None;

    /// Avoid emitting this entry for pub sections.
    bool AvoidForPubSections : 1;

    /// Is this an ObjC class implementation?
    bool ObjcClassImplementation : 1;
  };

  /// Emit .debug_pubnames and .debug_pubtypes for \p Unit.
  void emitPubAccelerators();

  /// Enumerates accelerator data.
  virtual void
  forEachAcceleratorRecord(function_ref<void(AccelInfo &)> Handler) = 0;

  /// @}

  /// Returns index(inside .debug_str_offsets) of specified string.
  virtual uint64_t getDebugStrIndex(const StringEntry *String) {
    return DebugStringIndexMap.getValueIndex(String);
  }

protected:
  /// Emit single abbreviation entry.
  void emitDwarfAbbrevEntry(const DIEAbbrev &Abbrev,
                            SectionDescriptor &AbbrevSection);

  /// Emit single pubnames/pubtypes accelerator entry.
  std::optional<uint64_t>
  emitPubAcceleratorEntry(SectionDescriptor &OutSection, const AccelInfo &Info,
                          std::optional<uint64_t> LengthOffset);

  /// Unique ID for the unit.
  unsigned ID = 0;

  /// The name of this unit.
  std::string UnitName;

  /// The DW_AT_LLVM_sysroot of this unit.
  std::string SysRoot;

  /// If this is a Clang module, this holds the module's name.
  std::string ClangModuleName;

  uint64_t UnitSize = 0;

  /// DWARF unit tag.
  dwarf::Tag UnitTag = dwarf::DW_TAG_null;

  /// true if current unit references_to/is_referenced by other unit.
  std::atomic<bool> IsInterconnectedCU = {false};

  /// FoldingSet that uniques the abbreviations.
  FoldingSet<DIEAbbrev> AbbreviationsSet;

  /// Storage for the unique Abbreviations.
  std::vector<std::unique_ptr<DIEAbbrev>> Abbreviations;

  /// Output unit DIE.
  DIE *OutUnitDIE = nullptr;

  /// Cache for file names for this unit.
  using FileNamesCache =
      DenseMap<uint64_t, std::pair<std::string, std::string>>;
  FileNamesCache FileNames;

  /// Maps a string into the index inside .debug_str_offsets section.
  IndexedValuesMap<const StringEntry *> DebugStringIndexMap;
};

inline bool isODRLanguage(uint16_t Language) {
  switch (Language) {
  case dwarf::DW_LANG_C_plus_plus:
  case dwarf::DW_LANG_C_plus_plus_03:
  case dwarf::DW_LANG_C_plus_plus_11:
  case dwarf::DW_LANG_C_plus_plus_14:
  case dwarf::DW_LANG_ObjC_plus_plus:
    return true;
  default:
    return false;
  };

  return false;
}

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_DWARFLINKERUNIT_H
