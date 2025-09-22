//===- AcceleratorRecordsSaver.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_ACCELERATORRECORDSSAVER_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_ACCELERATORRECORDSSAVER_H

#include "DIEAttributeCloner.h"
#include "DWARFLinkerCompileUnit.h"
#include "DWARFLinkerGlobalData.h"
#include "DWARFLinkerTypeUnit.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// This class helps to store information for accelerator entries.
/// It prepares accelerator info for the certain DIE and store it inside
/// OutUnit.
class AcceleratorRecordsSaver {
public:
  AcceleratorRecordsSaver(LinkingGlobalData &GlobalData, CompileUnit &InUnit,
                          CompileUnit *OutUnit)
      : AcceleratorRecordsSaver(GlobalData, InUnit,
                                CompileUnit::OutputUnitVariantPtr(OutUnit)) {}

  AcceleratorRecordsSaver(LinkingGlobalData &GlobalData, CompileUnit &InUnit,
                          TypeUnit *OutUnit)
      : AcceleratorRecordsSaver(GlobalData, InUnit,
                                CompileUnit::OutputUnitVariantPtr(OutUnit)) {}

  /// Save accelerator info for the specified \p OutDIE inside OutUnit.
  /// Side effects: set attributes in \p AttrInfo.
  void save(const DWARFDebugInfoEntry *InputDieEntry, DIE *OutDIE,
            AttributesInfo &AttrInfo, TypeEntry *TypeEntry);

protected:
  AcceleratorRecordsSaver(LinkingGlobalData &GlobalData, CompileUnit &InUnit,
                          CompileUnit::OutputUnitVariantPtr OutUnit)
      : GlobalData(GlobalData), InUnit(InUnit), OutUnit(OutUnit) {}

  void saveObjC(const DWARFDebugInfoEntry *InputDieEntry, DIE *OutDIE,
                AttributesInfo &AttrInfo);

  void saveNameRecord(StringEntry *Name, DIE *OutDIE, dwarf::Tag Tag,
                      bool AvoidForPubSections);
  void saveNamespaceRecord(StringEntry *Name, DIE *OutDIE, dwarf::Tag Tag,
                           TypeEntry *TypeEntry);
  void saveObjCNameRecord(StringEntry *Name, DIE *OutDIE, dwarf::Tag Tag);
  void saveTypeRecord(StringEntry *Name, DIE *OutDIE, dwarf::Tag Tag,
                      uint32_t QualifiedNameHash, bool ObjcClassImplementation,
                      TypeEntry *TypeEntry);

  /// Global linking data.
  LinkingGlobalData &GlobalData;

  /// Comiple unit corresponding to input DWARF.
  CompileUnit &InUnit;

  /// Compile unit or Artificial type unit corresponding to the output DWARF.
  CompileUnit::OutputUnitVariantPtr OutUnit;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_ACCELERATORRECORDSSAVER_H
