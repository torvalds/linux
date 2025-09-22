//===- SyntheticTypeNameBuilder.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SyntheticTypeNameBuilder.h"
#include "DWARFLinkerCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

Error SyntheticTypeNameBuilder::assignName(
    UnitEntryPairTy InputUnitEntryPair,
    std::optional<std::pair<size_t, size_t>> ChildIndex) {
  [[maybe_unused]] const CompileUnit::DIEInfo &Info =
      InputUnitEntryPair.CU->getDIEInfo(InputUnitEntryPair.DieEntry);
  assert(Info.needToPlaceInTypeTable() &&
         "Cann't assign name for non-type DIE");

  if (InputUnitEntryPair.CU->getDieTypeEntry(InputUnitEntryPair.DieEntry) !=
      nullptr)
    return Error::success();

  SyntheticName.resize(0);
  RecursionDepth = 0;
  return addDIETypeName(InputUnitEntryPair, ChildIndex, true);
}

void SyntheticTypeNameBuilder::addArrayDimension(
    UnitEntryPairTy InputUnitEntryPair) {
  for (const DWARFDebugInfoEntry *CurChild =
           InputUnitEntryPair.CU->getFirstChildEntry(
               InputUnitEntryPair.DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = InputUnitEntryPair.CU->getSiblingEntry(CurChild)) {
    if (CurChild->getTag() == dwarf::DW_TAG_subrange_type ||
        CurChild->getTag() == dwarf::DW_TAG_generic_subrange) {
      SyntheticName += "[";
      if (std::optional<DWARFFormValue> Val =
              InputUnitEntryPair.CU->find(CurChild, dwarf::DW_AT_count)) {
        if (std::optional<uint64_t> ConstVal = Val->getAsUnsignedConstant()) {
          SyntheticName += std::to_string(*ConstVal);
        } else if (std::optional<int64_t> ConstVal =
                       Val->getAsSignedConstant()) {
          SyntheticName += std::to_string(*ConstVal);
        }
      }

      SyntheticName += "]";
    }
  }
}

static dwarf::Attribute TypeAttr[] = {dwarf::DW_AT_type};
Error SyntheticTypeNameBuilder::addSignature(UnitEntryPairTy InputUnitEntryPair,
                                             bool addTemplateParameters) {
  // Add entry type.
  if (Error Err = addReferencedODRDies(InputUnitEntryPair, false, TypeAttr))
    return Err;
  SyntheticName += ':';

  SmallVector<const DWARFDebugInfoEntry *, 10> TemplateParameters;
  SmallVector<const DWARFDebugInfoEntry *, 20> FunctionParameters;
  for (const DWARFDebugInfoEntry *CurChild =
           InputUnitEntryPair.CU->getFirstChildEntry(
               InputUnitEntryPair.DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = InputUnitEntryPair.CU->getSiblingEntry(CurChild)) {
    dwarf::Tag ChildTag = CurChild->getTag();
    if (addTemplateParameters &&
        (ChildTag == dwarf::DW_TAG_template_type_parameter ||
         ChildTag == dwarf::DW_TAG_template_value_parameter))
      TemplateParameters.push_back(CurChild);
    else if (ChildTag == dwarf::DW_TAG_formal_parameter ||
             ChildTag == dwarf::DW_TAG_unspecified_parameters)
      FunctionParameters.push_back(CurChild);
    else if (addTemplateParameters &&
             ChildTag == dwarf::DW_TAG_GNU_template_parameter_pack) {
      for (const DWARFDebugInfoEntry *CurGNUChild =
               InputUnitEntryPair.CU->getFirstChildEntry(CurChild);
           CurGNUChild && CurGNUChild->getAbbreviationDeclarationPtr();
           CurGNUChild = InputUnitEntryPair.CU->getSiblingEntry(CurGNUChild))
        TemplateParameters.push_back(CurGNUChild);
    } else if (ChildTag == dwarf::DW_TAG_GNU_formal_parameter_pack) {
      for (const DWARFDebugInfoEntry *CurGNUChild =
               InputUnitEntryPair.CU->getFirstChildEntry(CurChild);
           CurGNUChild && CurGNUChild->getAbbreviationDeclarationPtr();
           CurGNUChild = InputUnitEntryPair.CU->getSiblingEntry(CurGNUChild))
        FunctionParameters.push_back(CurGNUChild);
    }
  }

  // Add parameters.
  if (Error Err = addParamNames(*InputUnitEntryPair.CU, FunctionParameters))
    return Err;

  // Add template parameters.
  if (Error Err =
          addTemplateParamNames(*InputUnitEntryPair.CU, TemplateParameters))
    return Err;

  return Error::success();
}

Error SyntheticTypeNameBuilder::addParamNames(
    CompileUnit &CU,
    SmallVector<const DWARFDebugInfoEntry *, 20> &FunctionParameters) {
  SyntheticName += '(';
  for (const DWARFDebugInfoEntry *FunctionParameter : FunctionParameters) {
    if (SyntheticName.back() != '(')
      SyntheticName += ", ";
    if (dwarf::toUnsigned(CU.find(FunctionParameter, dwarf::DW_AT_artificial),
                          0))
      SyntheticName += "^";
    if (Error Err = addReferencedODRDies(
            UnitEntryPairTy{&CU, FunctionParameter}, false, TypeAttr))
      return Err;
  }
  SyntheticName += ')';
  return Error::success();
}

Error SyntheticTypeNameBuilder::addTemplateParamNames(
    CompileUnit &CU,
    SmallVector<const DWARFDebugInfoEntry *, 10> &TemplateParameters) {
  if (!TemplateParameters.empty()) {
    SyntheticName += '<';
    for (const DWARFDebugInfoEntry *Parameter : TemplateParameters) {
      if (SyntheticName.back() != '<')
        SyntheticName += ", ";

      if (Parameter->getTag() == dwarf::DW_TAG_template_value_parameter) {
        if (std::optional<DWARFFormValue> Val =
                CU.find(Parameter, dwarf::DW_AT_const_value)) {
          if (std::optional<uint64_t> ConstVal = Val->getAsUnsignedConstant())
            SyntheticName += std::to_string(*ConstVal);
          else if (std::optional<int64_t> ConstVal = Val->getAsSignedConstant())
            SyntheticName += std::to_string(*ConstVal);
        }
      }

      if (Error Err = addReferencedODRDies(UnitEntryPairTy{&CU, Parameter},
                                           false, TypeAttr))
        return Err;
    }
    SyntheticName += '>';
  }
  return Error::success();
}

void SyntheticTypeNameBuilder::addOrderedName(
    std::pair<size_t, size_t> ChildIdx) {
  std::string Name;
  llvm::raw_string_ostream stream(Name);
  stream << format_hex_no_prefix(ChildIdx.first, ChildIdx.second);
  SyntheticName += Name;
}

// Examine DIE and return type deduplication candidate: some DIEs could not be
// deduplicated, namespace may refer to another namespace.
static std::optional<UnitEntryPairTy>
getTypeDeduplicationCandidate(UnitEntryPairTy UnitEntryPair) {
  switch (UnitEntryPair.DieEntry->getTag()) {
  case dwarf::DW_TAG_null:
  case dwarf::DW_TAG_compile_unit:
  case dwarf::DW_TAG_partial_unit:
  case dwarf::DW_TAG_type_unit:
  case dwarf::DW_TAG_skeleton_unit: {
    return std::nullopt;
  }
  case dwarf::DW_TAG_namespace: {
    // Check if current namespace refers another.
    if (UnitEntryPair.CU->find(UnitEntryPair.DieEntry, dwarf::DW_AT_extension))
      UnitEntryPair = UnitEntryPair.getNamespaceOrigin();

    // Content of anonimous namespaces should not be deduplicated.
    if (!UnitEntryPair.CU->find(UnitEntryPair.DieEntry, dwarf::DW_AT_name))
      llvm_unreachable("Cann't deduplicate anonimous namespace");

    return UnitEntryPair;
  }
  default:
    return UnitEntryPair;
  }
}

Error SyntheticTypeNameBuilder::addParentName(
    UnitEntryPairTy &InputUnitEntryPair) {
  std::optional<UnitEntryPairTy> UnitEntryPair = InputUnitEntryPair.getParent();
  if (!UnitEntryPair)
    return Error::success();

  UnitEntryPair = getTypeDeduplicationCandidate(*UnitEntryPair);
  if (!UnitEntryPair)
    return Error::success();

  if (TypeEntry *ImmediateParentName =
          UnitEntryPair->CU->getDieTypeEntry(UnitEntryPair->DieEntry)) {
    SyntheticName += ImmediateParentName->getKey();
    SyntheticName += ".";
    return Error::success();
  }

  // Collect parent entries.
  SmallVector<UnitEntryPairTy, 10> Parents;
  do {
    Parents.push_back(*UnitEntryPair);

    UnitEntryPair = UnitEntryPair->getParent();
    if (!UnitEntryPair)
      break;

    UnitEntryPair = getTypeDeduplicationCandidate(*UnitEntryPair);
    if (!UnitEntryPair)
      break;

  } while (!UnitEntryPair->CU->getDieTypeEntry(UnitEntryPair->DieEntry));

  // Assign name for each parent entry.
  size_t NameStart = SyntheticName.size();
  for (UnitEntryPairTy Parent : reverse(Parents)) {
    SyntheticName.resize(NameStart);
    if (Error Err = addDIETypeName(Parent, std::nullopt, true))
      return Err;
  }

  // Add parents delimiter.
  SyntheticName += ".";
  return Error::success();
}

void SyntheticTypeNameBuilder::addDieNameFromDeclFileAndDeclLine(
    UnitEntryPairTy &InputUnitEntryPair, bool &HasDeclFileName) {
  if (std::optional<DWARFFormValue> DeclFileVal = InputUnitEntryPair.CU->find(
          InputUnitEntryPair.DieEntry, dwarf::DW_AT_decl_file)) {
    if (std::optional<DWARFFormValue> DeclLineVal = InputUnitEntryPair.CU->find(
            InputUnitEntryPair.DieEntry, dwarf::DW_AT_decl_line)) {
      if (std::optional<std::pair<StringRef, StringRef>> DirAndFilename =
              InputUnitEntryPair.CU->getDirAndFilenameFromLineTable(
                  *DeclFileVal)) {
        SyntheticName += DirAndFilename->first;
        SyntheticName += DirAndFilename->second;

        if (std::optional<uint64_t> DeclLineIntVal =
                dwarf::toUnsigned(*DeclLineVal)) {
          SyntheticName += " ";
          SyntheticName += utohexstr(*DeclLineIntVal);
        }

        HasDeclFileName = true;
      }
    }
  }
}

void SyntheticTypeNameBuilder::addValueName(UnitEntryPairTy InputUnitEntryPair,
                                            dwarf::Attribute Attr) {
  if (std::optional<DWARFFormValue> Val =
          InputUnitEntryPair.CU->find(InputUnitEntryPair.DieEntry, Attr)) {
    if (std::optional<uint64_t> ConstVal = Val->getAsUnsignedConstant()) {
      SyntheticName += " ";
      SyntheticName += std::to_string(*ConstVal);
    } else if (std::optional<int64_t> ConstVal = Val->getAsSignedConstant()) {
      SyntheticName += " ";
      SyntheticName += std::to_string(*ConstVal);
    }
  }
}

Error SyntheticTypeNameBuilder::addReferencedODRDies(
    UnitEntryPairTy InputUnitEntryPair, bool AssignNameToTypeDescriptor,
    ArrayRef<dwarf::Attribute> ODRAttrs) {
  bool FirstIteration = true;
  for (dwarf::Attribute Attr : ODRAttrs) {
    if (std::optional<DWARFFormValue> AttrValue =
            InputUnitEntryPair.CU->find(InputUnitEntryPair.DieEntry, Attr)) {
      std::optional<UnitEntryPairTy> RefDie =
          InputUnitEntryPair.CU->resolveDIEReference(
              *AttrValue, ResolveInterCUReferencesMode::Resolve);

      if (!RefDie)
        continue;

      if (!RefDie->DieEntry)
        return createStringError(std::errc::invalid_argument,
                                 "Cann't resolve DIE reference");

      if (!FirstIteration)
        SyntheticName += ",";

      RecursionDepth++;
      if (RecursionDepth > 1000)
        return createStringError(
            std::errc::invalid_argument,
            "Cann't parse input DWARF. Recursive dependence.");

      if (Error Err =
              addDIETypeName(*RefDie, std::nullopt, AssignNameToTypeDescriptor))
        return Err;
      RecursionDepth--;
      FirstIteration = false;
    }
  }

  return Error::success();
}

Error SyntheticTypeNameBuilder::addTypeName(UnitEntryPairTy InputUnitEntryPair,
                                            bool AddParentNames) {
  bool HasLinkageName = false;
  bool HasShortName = false;
  bool HasTemplatesInShortName = false;
  bool HasDeclFileName = false;

  // Try to get name from the DIE.
  if (std::optional<DWARFFormValue> Val = InputUnitEntryPair.CU->find(
          InputUnitEntryPair.DieEntry,
          {dwarf::DW_AT_MIPS_linkage_name, dwarf::DW_AT_linkage_name})) {
    // Firstly check for linkage name.
    SyntheticName += dwarf::toStringRef(Val);
    HasLinkageName = true;
  } else if (std::optional<DWARFFormValue> Val = InputUnitEntryPair.CU->find(
                 InputUnitEntryPair.DieEntry, dwarf::DW_AT_name)) {
    // Then check for short name.
    StringRef Name = dwarf::toStringRef(Val);
    SyntheticName += Name;

    HasShortName = true;
    HasTemplatesInShortName =
        Name.ends_with(">") && Name.count("<") != 0 && !Name.ends_with("<=>");
  } else {
    // Finally check for declaration attributes.
    addDieNameFromDeclFileAndDeclLine(InputUnitEntryPair, HasDeclFileName);
  }

  // Add additional name parts for some DIEs.
  switch (InputUnitEntryPair.DieEntry->getTag()) {
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_interface_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_subprogram: {
    if (InputUnitEntryPair.CU->find(InputUnitEntryPair.DieEntry,
                                    dwarf::DW_AT_artificial))
      SyntheticName += "^";

    // No need to add signature information for linkage name,
    // also no need to add template parameters name if short name already
    // includes them.
    if (!HasLinkageName)
      if (Error Err =
              addSignature(InputUnitEntryPair, !HasTemplatesInShortName))
        return Err;
  } break;
  case dwarf::DW_TAG_coarray_type:
  case dwarf::DW_TAG_array_type: {
    addArrayDimension(InputUnitEntryPair);
  } break;
  case dwarf::DW_TAG_subrange_type: {
    addValueName(InputUnitEntryPair, dwarf::DW_AT_count);
  } break;
  case dwarf::DW_TAG_template_value_parameter: {
    if (!HasTemplatesInShortName) {
      // TODO add support for DW_AT_location
      addValueName(InputUnitEntryPair, dwarf::DW_AT_const_value);
    }
  } break;
  default: {
    // Nothing to do.
  } break;
  }

  // If name for the DIE is not determined yet add referenced types to the name.
  if (!HasLinkageName && !HasShortName && !HasDeclFileName) {
    if (InputUnitEntryPair.CU->find(InputUnitEntryPair.DieEntry,
                                    getODRAttributes()))
      if (Error Err = addReferencedODRDies(InputUnitEntryPair, AddParentNames,
                                           getODRAttributes()))
        return Err;
  }

  return Error::success();
}

Error SyntheticTypeNameBuilder::addDIETypeName(
    UnitEntryPairTy InputUnitEntryPair,
    std::optional<std::pair<size_t, size_t>> ChildIndex,
    bool AssignNameToTypeDescriptor) {
  std::optional<UnitEntryPairTy> UnitEntryPair =
      getTypeDeduplicationCandidate(InputUnitEntryPair);
  if (!UnitEntryPair)
    return Error::success();

  TypeEntry *TypeEntryPtr =
      InputUnitEntryPair.CU->getDieTypeEntry(InputUnitEntryPair.DieEntry);
  // Check if DIE already has a name.
  if (!TypeEntryPtr) {
    size_t NameStart = SyntheticName.size();
    if (AssignNameToTypeDescriptor) {
      if (Error Err = addParentName(*UnitEntryPair))
        return Err;
    }
    addTypePrefix(UnitEntryPair->DieEntry);

    if (ChildIndex) {
      addOrderedName(*ChildIndex);
    } else {
      if (Error Err = addTypeName(*UnitEntryPair, AssignNameToTypeDescriptor))
        return Err;
    }

    if (AssignNameToTypeDescriptor) {
      // Add built name to the DIE.
      TypeEntryPtr = TypePoolRef.insert(SyntheticName.substr(NameStart));
      InputUnitEntryPair.CU->setDieTypeEntry(InputUnitEntryPair.DieEntry,
                                             TypeEntryPtr);
    }
  } else
    SyntheticName += TypeEntryPtr->getKey();

  return Error::success();
}

void SyntheticTypeNameBuilder::addTypePrefix(
    const DWARFDebugInfoEntry *DieEntry) {
  switch (DieEntry->getTag()) {
  case dwarf::DW_TAG_base_type: {
    SyntheticName += "{0}";
  } break;
  case dwarf::DW_TAG_namespace: {
    SyntheticName += "{1}";
  } break;
  case dwarf::DW_TAG_formal_parameter: {
    SyntheticName += "{2}";
  } break;
  // dwarf::DW_TAG_unspecified_parameters have the same prefix as before.
  case dwarf::DW_TAG_unspecified_parameters: {
    SyntheticName += "{2}";
  } break;
  case dwarf::DW_TAG_template_type_parameter: {
    SyntheticName += "{3}";
  } break;
  // dwarf::DW_TAG_template_value_parameter have the same prefix as before.
  case dwarf::DW_TAG_template_value_parameter: {
    SyntheticName += "{3}";
  } break;
  case dwarf::DW_TAG_GNU_formal_parameter_pack: {
    SyntheticName += "{4}";
  } break;
  case dwarf::DW_TAG_GNU_template_parameter_pack: {
    SyntheticName += "{5}";
  } break;
  case dwarf::DW_TAG_inheritance: {
    SyntheticName += "{6}";
  } break;
  case dwarf::DW_TAG_array_type: {
    SyntheticName += "{7}";
  } break;
  case dwarf::DW_TAG_class_type: {
    SyntheticName += "{8}";
  } break;
  case dwarf::DW_TAG_enumeration_type: {
    SyntheticName += "{9}";
  } break;
  case dwarf::DW_TAG_imported_declaration: {
    SyntheticName += "{A}";
  } break;
  case dwarf::DW_TAG_member: {
    SyntheticName += "{B}";
  } break;
  case dwarf::DW_TAG_pointer_type: {
    SyntheticName += "{C}";
  } break;
  case dwarf::DW_TAG_reference_type: {
    SyntheticName += "{D}";
  } break;
  case dwarf::DW_TAG_string_type: {
    SyntheticName += "{E}";
  } break;
  case dwarf::DW_TAG_structure_type: {
    SyntheticName += "{F}";
  } break;
  case dwarf::DW_TAG_subroutine_type: {
    SyntheticName += "{G}";
  } break;
  case dwarf::DW_TAG_typedef: {
    SyntheticName += "{H}";
  } break;
  case dwarf::DW_TAG_union_type: {
    SyntheticName += "{I}";
  } break;
  case dwarf::DW_TAG_variant: {
    SyntheticName += "{J}";
  } break;
  case dwarf::DW_TAG_inlined_subroutine: {
    SyntheticName += "{K}";
  } break;
  case dwarf::DW_TAG_module: {
    SyntheticName += "{L}";
  } break;
  case dwarf::DW_TAG_ptr_to_member_type: {
    SyntheticName += "{M}";
  } break;
  case dwarf::DW_TAG_set_type: {
    SyntheticName += "{N}";
  } break;
  case dwarf::DW_TAG_subrange_type: {
    SyntheticName += "{O}";
  } break;
  case dwarf::DW_TAG_with_stmt: {
    SyntheticName += "{P}";
  } break;
  case dwarf::DW_TAG_access_declaration: {
    SyntheticName += "{Q}";
  } break;
  case dwarf::DW_TAG_catch_block: {
    SyntheticName += "{R}";
  } break;
  case dwarf::DW_TAG_const_type: {
    SyntheticName += "{S}";
  } break;
  case dwarf::DW_TAG_constant: {
    SyntheticName += "{T}";
  } break;
  case dwarf::DW_TAG_enumerator: {
    SyntheticName += "{U}";
  } break;
  case dwarf::DW_TAG_file_type: {
    SyntheticName += "{V}";
  } break;
  case dwarf::DW_TAG_friend: {
    SyntheticName += "{W}";
  } break;
  case dwarf::DW_TAG_namelist: {
    SyntheticName += "{X}";
  } break;
  case dwarf::DW_TAG_namelist_item: {
    SyntheticName += "{Y}";
  } break;
  case dwarf::DW_TAG_packed_type: {
    SyntheticName += "{Z}";
  } break;
  case dwarf::DW_TAG_subprogram: {
    SyntheticName += "{a}";
  } break;
  case dwarf::DW_TAG_thrown_type: {
    SyntheticName += "{b}";
  } break;
  case dwarf::DW_TAG_variant_part: {
    SyntheticName += "{c}";
  } break;
  case dwarf::DW_TAG_variable: {
    SyntheticName += "{d}";
  } break;
  case dwarf::DW_TAG_volatile_type: {
    SyntheticName += "{e}";
  } break;
  case dwarf::DW_TAG_dwarf_procedure: {
    SyntheticName += "{f}";
  } break;
  case dwarf::DW_TAG_restrict_type: {
    SyntheticName += "{g}";
  } break;
  case dwarf::DW_TAG_interface_type: {
    SyntheticName += "{h}";
  } break;
  case dwarf::DW_TAG_imported_module: {
    SyntheticName += "{i}";
  } break;
  case dwarf::DW_TAG_unspecified_type: {
    SyntheticName += "{j}";
  } break;
  case dwarf::DW_TAG_imported_unit: {
    SyntheticName += "{k}";
  } break;
  case dwarf::DW_TAG_condition: {
    SyntheticName += "{l}";
  } break;
  case dwarf::DW_TAG_shared_type: {
    SyntheticName += "{m}";
  } break;
  case dwarf::DW_TAG_rvalue_reference_type: {
    SyntheticName += "{n}";
  } break;
  case dwarf::DW_TAG_template_alias: {
    SyntheticName += "{o}";
  } break;
  case dwarf::DW_TAG_coarray_type: {
    SyntheticName += "{p}";
  } break;
  case dwarf::DW_TAG_generic_subrange: {
    SyntheticName += "{q}";
  } break;
  case dwarf::DW_TAG_dynamic_type: {
    SyntheticName += "{r}";
  } break;
  case dwarf::DW_TAG_atomic_type: {
    SyntheticName += "{s}";
  } break;
  case dwarf::DW_TAG_call_site: {
    SyntheticName += "{t}";
  } break;
  case dwarf::DW_TAG_call_site_parameter: {
    SyntheticName += "{u}";
  } break;
  case dwarf::DW_TAG_immutable_type: {
    SyntheticName += "{v}";
  } break;
  case dwarf::DW_TAG_entry_point: {
    SyntheticName += "{w}";
  } break;
  case dwarf::DW_TAG_label: {
    SyntheticName += "{x}";
  } break;
  case dwarf::DW_TAG_lexical_block: {
    SyntheticName += "{y}";
  } break;
  case dwarf::DW_TAG_common_block: {
    SyntheticName += "{z}";
  } break;
  case dwarf::DW_TAG_common_inclusion: {
    SyntheticName += "{|}";
  } break;
  case dwarf::DW_TAG_try_block: {
    SyntheticName += "{~}";
  } break;

  case dwarf::DW_TAG_null: {
    llvm_unreachable("No type prefix for DW_TAG_null");
  } break;
  case dwarf::DW_TAG_compile_unit: {
    llvm_unreachable("No type prefix for DW_TAG_compile_unit");
  } break;
  case dwarf::DW_TAG_partial_unit: {
    llvm_unreachable("No type prefix for DW_TAG_partial_unit");
  } break;
  case dwarf::DW_TAG_type_unit: {
    llvm_unreachable("No type prefix for DW_TAG_type_unit");
  } break;
  case dwarf::DW_TAG_skeleton_unit: {
    llvm_unreachable("No type prefix for DW_TAG_skeleton_unit");
  } break;

  default: {
    SyntheticName += "{~~";
    SyntheticName += utohexstr(DieEntry->getTag());
    SyntheticName += "}";
  } break;
  }
}

OrderedChildrenIndexAssigner::OrderedChildrenIndexAssigner(
    CompileUnit &CU, const DWARFDebugInfoEntry *DieEntry) {
  switch (DieEntry->getTag()) {
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_coarray_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_common_block:
  case dwarf::DW_TAG_lexical_block:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subprogram:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_GNU_template_template_param:
  case dwarf::DW_TAG_GNU_formal_parameter_pack: {
    NeedCountChildren = true;
  } break;
  case dwarf::DW_TAG_enumeration_type: {
    // TODO : do we need to add condition
    NeedCountChildren = true;
  } break;
  default: {
    // Nothing to do.
  }
  }

  // Calculate maximal index value
  if (NeedCountChildren) {
    for (const DWARFDebugInfoEntry *CurChild = CU.getFirstChildEntry(DieEntry);
         CurChild && CurChild->getAbbreviationDeclarationPtr();
         CurChild = CU.getSiblingEntry(CurChild)) {
      std::optional<size_t> ArrayIndex = tagToArrayIndex(CU, CurChild);
      if (!ArrayIndex)
        continue;

      assert((*ArrayIndex < ChildIndexesWidth.size()) &&
             "Wrong index for ChildIndexesWidth");
      ChildIndexesWidth[*ArrayIndex]++;
    }

    // Calculate index field width(number of digits in hexadecimal
    // representation).
    for (size_t &Width : ChildIndexesWidth) {
      size_t digitsCounter = 1;
      size_t NumToCompare = 15;

      while (NumToCompare < Width) {
        NumToCompare <<= 4;
        digitsCounter++;
      }

      Width = digitsCounter;
    }
  }
}

std::optional<size_t> OrderedChildrenIndexAssigner::tagToArrayIndex(
    CompileUnit &CU, const DWARFDebugInfoEntry *DieEntry) {
  if (!NeedCountChildren)
    return std::nullopt;

  switch (DieEntry->getTag()) {
  case dwarf::DW_TAG_unspecified_parameters:
  case dwarf::DW_TAG_formal_parameter:
    return 0;
  case dwarf::DW_TAG_template_value_parameter:
  case dwarf::DW_TAG_template_type_parameter:
    return 1;
  case dwarf::DW_TAG_enumeration_type:
    if (std::optional<uint32_t> ParentIdx = DieEntry->getParentIdx()) {
      if (*ParentIdx && CU.getDebugInfoEntry(*ParentIdx)->getTag() ==
                            dwarf::DW_TAG_array_type)
        return 2;
    }
    return std::nullopt;
  case dwarf::DW_TAG_subrange_type:
    return 3;
  case dwarf::DW_TAG_generic_subrange:
    return 4;
  case dwarf::DW_TAG_enumerator:
    return 5;
  case dwarf::DW_TAG_namelist_item:
    return 6;
  case dwarf::DW_TAG_member:
    return 7;
  default:
    return std::nullopt;
  };
}

std::optional<std::pair<size_t, size_t>>
OrderedChildrenIndexAssigner::getChildIndex(
    CompileUnit &CU, const DWARFDebugInfoEntry *ChildDieEntry) {
  std::optional<size_t> ArrayIndex = tagToArrayIndex(CU, ChildDieEntry);
  if (!ArrayIndex)
    return std::nullopt;

  assert((*ArrayIndex < OrderedChildIdxs.size()) &&
         "Wrong index for ChildIndexesWidth");
  assert(ChildIndexesWidth[*ArrayIndex] < 16 &&
         "Index width exceeds 16 digits.");

  std::pair<size_t, size_t> Result = std::make_pair(
      OrderedChildIdxs[*ArrayIndex], ChildIndexesWidth[*ArrayIndex]);
  OrderedChildIdxs[*ArrayIndex]++;
  return Result;
}
