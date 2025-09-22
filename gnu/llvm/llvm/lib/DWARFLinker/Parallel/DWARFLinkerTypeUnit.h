//===- DWARFLinkerTypeUnit.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_PARALLEL_DWARFLINKERTYPEUNIT_H
#define LLVM_DWARFLINKER_PARALLEL_DWARFLINKERTYPEUNIT_H

#include "DWARFLinkerUnit.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// Type Unit is used to represent an artificial compilation unit
/// which keeps all type information. This type information is referenced
/// from other compilation units.
class TypeUnit : public DwarfUnit {
public:
  TypeUnit(LinkingGlobalData &GlobalData, unsigned ID,
           std::optional<uint16_t> Language, dwarf::FormParams Format,
           llvm::endianness Endianess);

  /// Generates DIE tree based on information from TypesMap.
  void createDIETree(BumpPtrAllocator &Allocator);

  /// Emits resulting dwarf based on information from DIE tree.
  Error finishCloningAndEmit(const Triple &TargetTriple);

  /// Returns global type pool.
  TypePool &getTypePool() { return Types; }

  /// TypeUnitAccelInfo extends AccelInfo structure with type specific fileds.
  /// We need these additional fields to decide whether OutDIE should have an
  /// accelerator record or not. The TypeEntryBodyPtr can refer to the
  /// declaration DIE and definition DIE corresponding to the type entry.
  /// Only one of them would be used in final output. So if TypeUnitAccelInfo
  /// refers OutDIE which does not match with TypeEntryBodyPtr->getFinalDie()
  /// then such record should be skipped.
  struct TypeUnitAccelInfo : public AccelInfo {
    /// Pointer to the output DIE which owns this accelerator record.
    DIE *OutDIE = nullptr;

    /// Pointer to the type entry body.
    TypeEntryBody *TypeEntryBodyPtr = nullptr;
  };

  /// Enumerates all accelerator records and call \p Handler for each.
  void
  forEachAcceleratorRecord(function_ref<void(AccelInfo &)> Handler) override {
    AcceleratorRecords.forEach([&](TypeUnitAccelInfo &Info) {
      // Check whether current record is for the final DIE.
      assert(Info.TypeEntryBodyPtr != nullptr);

      if (&Info.TypeEntryBodyPtr->getFinalDie() != Info.OutDIE)
        return;

      Info.OutOffset = Info.OutDIE->getOffset();
      Handler(Info);
    });
  }

  /// Returns index for the specified \p String inside .debug_str_offsets.
  uint64_t getDebugStrIndex(const StringEntry *String) override {
    std::unique_lock<std::mutex> LockGuard(DebugStringIndexMapMutex);
    return DebugStringIndexMap.getValueIndex(String);
  }

  /// Adds \p Info to the unit's accelerator records.
  void saveAcceleratorInfo(const TypeUnitAccelInfo &Info) {
    AcceleratorRecords.add(Info);
  }

private:
  /// Type DIEs are partially created at clonning stage. They are organised
  /// as a tree using type entries. This function links DIEs(corresponding
  /// to the type entries) into the tree structure.
  uint64_t finalizeTypeEntryRec(uint64_t OutOffset, DIE *OutDIE,
                                TypeEntry *Entry);

  /// Prepares DIEs to be linked into the tree.
  void prepareDataForTreeCreation();

  /// Add specified \p Dir and \p Filename into the line table
  /// of this type unit.
  uint32_t addFileNameIntoLinetable(StringEntry *Dir, StringEntry *FileName);

  std::pair<dwarf::Form, uint8_t> getScalarFormForValue(uint64_t Value) const;

  uint8_t getSizeByAttrForm(dwarf::Form Form) const;

  struct CmpStringEntryRef {
    bool operator()(const StringEntry *LHS, const StringEntry *RHS) const {
      return LHS->first() < RHS->first();
    }
  };
  struct CmpDirIDStringEntryRef {
    bool operator()(const std::pair<StringEntry *, uint64_t> &LHS,
                    const std::pair<StringEntry *, uint64_t> &RHS) const {
      return LHS.second < RHS.second ||
             (!(RHS.second < LHS.second) &&
              LHS.first->first() < RHS.first->first());
    }
  };

  /// The DW_AT_language of this unit.
  std::optional<uint16_t> Language;

  /// This unit line table.
  DWARFDebugLine::LineTable LineTable;

  /// Data members keeping file names for line table.
  using DirectoriesMapTy = std::map<StringEntry *, size_t, CmpStringEntryRef>;
  using FilenamesMapTy = std::map<std::pair<StringEntry *, uint64_t>, size_t,
                                  CmpDirIDStringEntryRef>;

  DirectoriesMapTy DirectoriesMap;
  FilenamesMapTy FileNamesMap;

  /// Type DIEs tree.
  TypePool Types;

  /// List of accelerator entries for this unit.
  ArrayList<TypeUnitAccelInfo> AcceleratorRecords;

  /// Guard for DebugStringIndexMap.
  std::mutex DebugStringIndexMapMutex;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_PARALLEL_DWARFLINKERTYPEUNIT_H
