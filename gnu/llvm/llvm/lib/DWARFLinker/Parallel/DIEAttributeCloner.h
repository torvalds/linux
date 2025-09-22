//===- DIEAttributeCloner.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_DIEATTRIBUTECLONER_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_DIEATTRIBUTECLONER_H

#include "ArrayList.h"
#include "DIEGenerator.h"
#include "DWARFLinkerCompileUnit.h"
#include "DWARFLinkerGlobalData.h"
#include "DWARFLinkerTypeUnit.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// Information gathered and exchanged between the various
/// clone*Attr helpers about the attributes of a particular DIE.
struct AttributesInfo {
  /// Short Name.
  StringEntry *Name = nullptr;

  /// Mangled Name.
  StringEntry *MangledName = nullptr;

  /// Does the DIE have an address pointing to live code section?
  bool HasLiveAddress = false;

  /// Is this DIE only a declaration?
  bool IsDeclaration = false;

  /// Does the DIE have a ranges attribute?
  bool HasRanges = false;

  /// Does the DIE have a string offset attribute?
  bool HasStringOffsetBaseAttr = false;
};

/// This class creates clones of input DIE attributes.
/// It enumerates attributes of input DIE, creates clone for each
/// attribute, adds cloned attribute to the output DIE.
class DIEAttributeCloner {
public:
  DIEAttributeCloner(DIE *OutDIE, CompileUnit &InUnit, CompileUnit *OutUnit,
                     const DWARFDebugInfoEntry *InputDieEntry,
                     DIEGenerator &Generator,
                     std::optional<int64_t> FuncAddressAdjustment,
                     std::optional<int64_t> VarAddressAdjustment,
                     bool HasLocationExpressionAddress)
      : DIEAttributeCloner(OutDIE, InUnit,
                           CompileUnit::OutputUnitVariantPtr(OutUnit),
                           InputDieEntry, Generator, FuncAddressAdjustment,
                           VarAddressAdjustment, HasLocationExpressionAddress) {
  }

  DIEAttributeCloner(DIE *OutDIE, CompileUnit &InUnit, TypeUnit *OutUnit,
                     const DWARFDebugInfoEntry *InputDieEntry,
                     DIEGenerator &Generator,
                     std::optional<int64_t> FuncAddressAdjustment,
                     std::optional<int64_t> VarAddressAdjustment,
                     bool HasLocationExpressionAddress)
      : DIEAttributeCloner(OutDIE, InUnit,
                           CompileUnit::OutputUnitVariantPtr(OutUnit),
                           InputDieEntry, Generator, FuncAddressAdjustment,
                           VarAddressAdjustment, HasLocationExpressionAddress) {
  }

  /// Clone attributes of input DIE.
  void clone();

  /// Create abbreviations for the output DIE after all attributes are cloned.
  unsigned finalizeAbbreviations(bool HasChildrenToClone);

  /// Cannot be used concurrently.
  AttributesInfo AttrInfo;

  unsigned getOutOffset() { return AttrOutOffset; }

protected:
  DIEAttributeCloner(DIE *OutDIE, CompileUnit &InUnit,
                     CompileUnit::OutputUnitVariantPtr OutUnit,
                     const DWARFDebugInfoEntry *InputDieEntry,
                     DIEGenerator &Generator,
                     std::optional<int64_t> FuncAddressAdjustment,
                     std::optional<int64_t> VarAddressAdjustment,
                     bool HasLocationExpressionAddress)
      : OutDIE(OutDIE), InUnit(InUnit), OutUnit(OutUnit),
        DebugInfoOutputSection(
            OutUnit->getSectionDescriptor(DebugSectionKind::DebugInfo)),
        InputDieEntry(InputDieEntry), Generator(Generator),
        FuncAddressAdjustment(FuncAddressAdjustment),
        VarAddressAdjustment(VarAddressAdjustment),
        HasLocationExpressionAddress(HasLocationExpressionAddress) {
    InputDIEIdx = InUnit.getDIEIndex(InputDieEntry);

    // Use DW_FORM_strp form for string attributes for DWARF version less than 5
    // or if output unit is type unit and we need to produce deterministic
    // result. (We can not generate deterministic results for debug_str_offsets
    // section when attributes are cloned parallelly).
    Use_DW_FORM_strp =
        (InUnit.getVersion() < 5) ||
        (OutUnit.isTypeUnit() &&
         ((InUnit.getGlobalData().getOptions().Threads != 1) &&
          !InUnit.getGlobalData().getOptions().AllowNonDeterministicOutput));
  }

  /// Clone string attribute.
  size_t
  cloneStringAttr(const DWARFFormValue &Val,
                  const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec);

  /// Clone attribute referencing another DIE.
  size_t
  cloneDieRefAttr(const DWARFFormValue &Val,
                  const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec);

  /// Clone scalar attribute.
  size_t
  cloneScalarAttr(const DWARFFormValue &Val,
                  const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec);

  /// Clone block or exprloc attribute.
  size_t
  cloneBlockAttr(const DWARFFormValue &Val,
                 const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec);

  /// Clone address attribute.
  size_t
  cloneAddressAttr(const DWARFFormValue &Val,
                   const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec);

  /// Returns true if attribute should be skipped.
  bool
  shouldSkipAttribute(DWARFAbbreviationDeclaration::AttributeSpec AttrSpec);

  /// Output DIE.
  DIE *OutDIE = nullptr;

  /// Input compilation unit.
  CompileUnit &InUnit;

  /// Output unit(either "plain" compilation unit, either artificial type unit).
  CompileUnit::OutputUnitVariantPtr OutUnit;

  /// .debug_info section descriptor.
  SectionDescriptor &DebugInfoOutputSection;

  /// Input DIE entry.
  const DWARFDebugInfoEntry *InputDieEntry = nullptr;

  /// Input DIE index.
  uint32_t InputDIEIdx = 0;

  /// Output DIE generator.
  DIEGenerator &Generator;

  /// Relocation adjustment for the function address ranges.
  std::optional<int64_t> FuncAddressAdjustment;

  /// Relocation adjustment for the variable locations.
  std::optional<int64_t> VarAddressAdjustment;

  /// Indicates whether InputDieEntry has an location attribute
  /// containg address expression.
  bool HasLocationExpressionAddress = false;

  /// Output offset after all attributes.
  unsigned AttrOutOffset = 0;

  /// Patches for the cloned attributes.
  OffsetsPtrVector PatchesOffsets;

  /// This flag forces using DW_FORM_strp for string attributes.
  bool Use_DW_FORM_strp = false;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_DIEATTRIBUTECLONER_H
