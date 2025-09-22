//===- SyntheticTypeNameBuilder.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_SYNTHETICTYPENAMEBUILDER_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_SYNTHETICTYPENAMEBUILDER_H

#include "DWARFLinkerCompileUnit.h"
#include "DWARFLinkerGlobalData.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class DWARFDebugInfoEntry;

namespace dwarf_linker {
namespace parallel {
struct LinkContext;
class TypeTableUnit;
class CompileUnit;

/// The helper class to build type name based on DIE properties.
/// It builds synthetic name based on explicit attributes: DW_AT_name,
/// DW_AT_linkage_name or based on implicit attributes(DW_AT_decl*).
/// Names for specific DIEs(like subprograms, template classes...) include
/// additional attributes: subprogram parameters, template parameters,
/// array ranges. Examples of built name:
///
/// class A {  }                    : {8}A
///
/// namspace llvm { class A {  } }  : {1}llvm{8}A
///
/// template <int> structure B { }  : {F}B<{0}int>
///
/// void foo ( int p1, float p3 )   : {a}void foo({0}int, {0}int)
///
/// int *ptr;                       : {c}ptr {0}int
///
/// int var;                        : {d}var
///
/// These names is used to refer DIEs describing types.
class SyntheticTypeNameBuilder {
public:
  SyntheticTypeNameBuilder(TypePool &TypePoolRef) : TypePoolRef(TypePoolRef) {}

  /// Create synthetic name for the specified DIE \p InputUnitEntryPair
  /// and assign created name to the DIE type info. \p ChildIndex is used
  /// to create name for ordered DIEs(function arguments f.e.).
  Error assignName(UnitEntryPairTy InputUnitEntryPair,
                   std::optional<std::pair<size_t, size_t>> ChildIndex);

protected:
  /// Add array type dimension.
  void addArrayDimension(UnitEntryPairTy InputUnitEntryPair);

  /// Add signature( entry type plus type of parameters plus type of template
  /// parameters(if \p addTemplateParameters is true).
  Error addSignature(UnitEntryPairTy InputUnitEntryPair,
                     bool addTemplateParameters);

  /// Add specified \p FunctionParameters to the built name.
  Error addParamNames(
      CompileUnit &CU,
      SmallVector<const DWARFDebugInfoEntry *, 20> &FunctionParameters);

  /// Add specified \p TemplateParameters to the built name.
  Error addTemplateParamNames(
      CompileUnit &CU,
      SmallVector<const DWARFDebugInfoEntry *, 10> &TemplateParameters);

  /// Add ordered name to the built name.
  void addOrderedName(CompileUnit &CU, const DWARFDebugInfoEntry *DieEntry);

  /// Analyze \p InputUnitEntryPair's ODR attributes and put names
  /// of the referenced type dies to the built name.
  Error addReferencedODRDies(UnitEntryPairTy InputUnitEntryPair,
                             bool AssignNameToTypeDescriptor,
                             ArrayRef<dwarf::Attribute> ODRAttrs);

  /// Add names of parent dies to the built name.
  Error addParentName(UnitEntryPairTy &InputUnitEntryPair);

  /// \returns synthetic name of the specified \p DieEntry.
  /// The name is constructed from the dwarf::DW_AT_decl_file
  /// and dwarf::DW_AT_decl_line attributes.
  void addDieNameFromDeclFileAndDeclLine(UnitEntryPairTy &InputUnitEntryPair,
                                         bool &HasDeclFileName);

  /// Add type prefix to the built name.
  void addTypePrefix(const DWARFDebugInfoEntry *DieEntry);

  /// Add type name to the built name.
  Error addTypeName(UnitEntryPairTy InputUnitEntryPair, bool AddParentNames);

  /// Analyze \p InputUnitEntryPair for the type name and possibly assign
  /// built type name to the DIE's type info.
  /// NOTE: while analyzing types we may create different kind of names
  /// for the same type depending on whether the type is part of another type.
  /// f.e. DW_TAG_formal_parameter would receive "{02}01" name when
  /// examined alone. Or "{0}int" name when it is a part of a function name:
  /// {a}void foo({0}int). The \p AssignNameToTypeDescriptor tells whether
  /// the type name is part of another type name and then should not be assigned
  /// to DIE type descriptor.
  Error addDIETypeName(UnitEntryPairTy InputUnitEntryPair,
                       std::optional<std::pair<size_t, size_t>> ChildIndex,
                       bool AssignNameToTypeDescriptor);

  /// Add ordered name to the built name.
  void addOrderedName(std::pair<size_t, size_t> ChildIdx);

  /// Add value name to the built name.
  void addValueName(UnitEntryPairTy InputUnitEntryPair, dwarf::Attribute Attr);

  /// Buffer keeping bult name.
  SmallString<1000> SyntheticName;

  /// Recursion counter
  size_t RecursionDepth = 0;

  /// Type pool
  TypePool &TypePoolRef;
};

/// This class helps to assign indexes for DIE children.
/// Indexes are used to create type name for children which
/// should be presented in the original order(function parameters,
/// array dimensions, enumeration members, class/structure members).
class OrderedChildrenIndexAssigner {
public:
  OrderedChildrenIndexAssigner(CompileUnit &CU,
                               const DWARFDebugInfoEntry *DieEntry);

  /// Returns index of the specified child and width of hexadecimal
  /// representation.
  std::optional<std::pair<size_t, size_t>>
  getChildIndex(CompileUnit &CU, const DWARFDebugInfoEntry *ChildDieEntry);

protected:
  using OrderedChildrenIndexesArrayTy = std::array<size_t, 8>;

  std::optional<size_t> tagToArrayIndex(CompileUnit &CU,
                                        const DWARFDebugInfoEntry *DieEntry);

  bool NeedCountChildren = false;
  OrderedChildrenIndexesArrayTy OrderedChildIdxs = {0};
  OrderedChildrenIndexesArrayTy ChildIndexesWidth = {0};
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_SYNTHETICTYPENAMEBUILDER_H
