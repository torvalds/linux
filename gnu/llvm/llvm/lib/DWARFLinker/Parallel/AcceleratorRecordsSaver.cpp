//=== AcceleratorRecordsSaver.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AcceleratorRecordsSaver.h"
#include "llvm/DWARFLinker/Utils.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/Support/DJB.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

static uint32_t hashFullyQualifiedName(CompileUnit &InputCU, DWARFDie &InputDIE,
                                       int ChildRecurseDepth = 0) {
  const char *Name = nullptr;
  CompileUnit *CU = &InputCU;
  std::optional<DWARFFormValue> RefVal;

  if (Error Err = finiteLoop([&]() -> Expected<bool> {
        if (const char *CurrentName = InputDIE.getName(DINameKind::ShortName))
          Name = CurrentName;

        if (!(RefVal = InputDIE.find(dwarf::DW_AT_specification)) &&
            !(RefVal = InputDIE.find(dwarf::DW_AT_abstract_origin)))
          return false;

        if (!RefVal->isFormClass(DWARFFormValue::FC_Reference))
          return false;

        std::optional<UnitEntryPairTy> RefDie = CU->resolveDIEReference(
            *RefVal, ResolveInterCUReferencesMode::Resolve);
        if (!RefDie)
          return false;

        if (!RefDie->DieEntry)
          return false;

        CU = RefDie->CU;
        InputDIE = RefDie->CU->getDIE(RefDie->DieEntry);
        return true;
      })) {
    consumeError(std::move(Err));
  }

  if (!Name && InputDIE.getTag() == dwarf::DW_TAG_namespace)
    Name = "(anonymous namespace)";

  DWARFDie ParentDie = InputDIE.getParent();
  if (!ParentDie.isValid() || ParentDie.getTag() == dwarf::DW_TAG_compile_unit)
    return djbHash(Name ? Name : "", djbHash(ChildRecurseDepth ? "" : "::"));

  return djbHash(
      (Name ? Name : ""),
      djbHash((Name ? "::" : ""),
              hashFullyQualifiedName(*CU, ParentDie, ++ChildRecurseDepth)));
}

void AcceleratorRecordsSaver::save(const DWARFDebugInfoEntry *InputDieEntry,
                                   DIE *OutDIE, AttributesInfo &AttrInfo,
                                   TypeEntry *TypeEntry) {
  if (GlobalData.getOptions().AccelTables.empty())
    return;

  DWARFDie InputDIE = InUnit.getDIE(InputDieEntry);

  // Look for short name recursively if short name is not known yet.
  if (AttrInfo.Name == nullptr)
    if (const char *ShortName = InputDIE.getShortName())
      AttrInfo.Name = GlobalData.getStringPool().insert(ShortName).first;

  switch (InputDieEntry->getTag()) {
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_pointer_type:
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_string_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_ptr_to_member_type:
  case dwarf::DW_TAG_set_type:
  case dwarf::DW_TAG_subrange_type:
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_constant:
  case dwarf::DW_TAG_file_type:
  case dwarf::DW_TAG_namelist:
  case dwarf::DW_TAG_packed_type:
  case dwarf::DW_TAG_volatile_type:
  case dwarf::DW_TAG_restrict_type:
  case dwarf::DW_TAG_atomic_type:
  case dwarf::DW_TAG_interface_type:
  case dwarf::DW_TAG_unspecified_type:
  case dwarf::DW_TAG_shared_type:
  case dwarf::DW_TAG_immutable_type:
  case dwarf::DW_TAG_rvalue_reference_type: {
    if (!AttrInfo.IsDeclaration && AttrInfo.Name != nullptr &&
        !AttrInfo.Name->getKey().empty()) {
      uint32_t Hash = hashFullyQualifiedName(InUnit, InputDIE);

      uint64_t RuntimeLang =
          dwarf::toUnsigned(InputDIE.find(dwarf::DW_AT_APPLE_runtime_class))
              .value_or(0);

      bool ObjCClassIsImplementation =
          (RuntimeLang == dwarf::DW_LANG_ObjC ||
           RuntimeLang == dwarf::DW_LANG_ObjC_plus_plus) &&
          dwarf::toUnsigned(
              InputDIE.find(dwarf::DW_AT_APPLE_objc_complete_type))
              .value_or(0);

      saveTypeRecord(AttrInfo.Name, OutDIE, InputDieEntry->getTag(), Hash,
                     ObjCClassIsImplementation, TypeEntry);
    }
  } break;
  case dwarf::DW_TAG_namespace: {
    if (AttrInfo.Name == nullptr)
      AttrInfo.Name =
          GlobalData.getStringPool().insert("(anonymous namespace)").first;

    saveNamespaceRecord(AttrInfo.Name, OutDIE, InputDieEntry->getTag(),
                        TypeEntry);
  } break;
  case dwarf::DW_TAG_imported_declaration: {
    if (AttrInfo.Name != nullptr)
      saveNamespaceRecord(AttrInfo.Name, OutDIE, InputDieEntry->getTag(),
                          TypeEntry);
  } break;
  case dwarf::DW_TAG_compile_unit:
  case dwarf::DW_TAG_lexical_block: {
    // Nothing to do.
  } break;
  default:
    if (TypeEntry)
      // Do not store this kind of accelerator entries for type entries.
      return;

    if (AttrInfo.HasLiveAddress || AttrInfo.HasRanges) {
      if (AttrInfo.Name)
        saveNameRecord(AttrInfo.Name, OutDIE, InputDieEntry->getTag(),
                       InputDieEntry->getTag() ==
                           dwarf::DW_TAG_inlined_subroutine);

      // Look for mangled name recursively if mangled name is not known yet.
      if (!AttrInfo.MangledName)
        if (const char *LinkageName = InputDIE.getLinkageName())
          AttrInfo.MangledName =
              GlobalData.getStringPool().insert(LinkageName).first;

      if (AttrInfo.MangledName && AttrInfo.MangledName != AttrInfo.Name)
        saveNameRecord(AttrInfo.MangledName, OutDIE, InputDieEntry->getTag(),
                       InputDieEntry->getTag() ==
                           dwarf::DW_TAG_inlined_subroutine);

      // Strip template parameters from the short name.
      if (AttrInfo.Name && AttrInfo.MangledName != AttrInfo.Name &&
          (InputDieEntry->getTag() != dwarf::DW_TAG_inlined_subroutine)) {
        if (std::optional<StringRef> Name =
                StripTemplateParameters(AttrInfo.Name->getKey())) {
          StringEntry *NameWithoutTemplateParams =
              GlobalData.getStringPool().insert(*Name).first;

          saveNameRecord(NameWithoutTemplateParams, OutDIE,
                         InputDieEntry->getTag(), true);
        }
      }

      if (AttrInfo.Name)
        saveObjC(InputDieEntry, OutDIE, AttrInfo);
    }
    break;
  }
}

void AcceleratorRecordsSaver::saveObjC(const DWARFDebugInfoEntry *InputDieEntry,
                                       DIE *OutDIE, AttributesInfo &AttrInfo) {
  std::optional<ObjCSelectorNames> Names =
      getObjCNamesIfSelector(AttrInfo.Name->getKey());
  if (!Names)
    return;

  StringEntry *Selector =
      GlobalData.getStringPool().insert(Names->Selector).first;
  saveNameRecord(Selector, OutDIE, InputDieEntry->getTag(), true);
  StringEntry *ClassName =
      GlobalData.getStringPool().insert(Names->ClassName).first;
  saveObjCNameRecord(ClassName, OutDIE, InputDieEntry->getTag());
  if (Names->ClassNameNoCategory) {
    StringEntry *ClassNameNoCategory =
        GlobalData.getStringPool().insert(*Names->ClassNameNoCategory).first;
    saveObjCNameRecord(ClassNameNoCategory, OutDIE, InputDieEntry->getTag());
  }
  if (Names->MethodNameNoCategory) {
    StringEntry *MethodNameNoCategory =
        GlobalData.getStringPool().insert(*Names->MethodNameNoCategory).first;
    saveNameRecord(MethodNameNoCategory, OutDIE, InputDieEntry->getTag(), true);
  }
}

void AcceleratorRecordsSaver::saveNameRecord(StringEntry *Name, DIE *OutDIE,
                                             dwarf::Tag Tag,
                                             bool AvoidForPubSections) {
  DwarfUnit::AccelInfo Info;

  Info.Type = DwarfUnit::AccelType::Name;
  Info.String = Name;
  Info.OutOffset = OutDIE->getOffset();
  Info.Tag = Tag;
  Info.AvoidForPubSections = AvoidForPubSections;

  OutUnit.getAsCompileUnit()->saveAcceleratorInfo(Info);
}
void AcceleratorRecordsSaver::saveNamespaceRecord(StringEntry *Name,
                                                  DIE *OutDIE, dwarf::Tag Tag,
                                                  TypeEntry *TypeEntry) {
  if (OutUnit.isCompileUnit()) {
    assert(TypeEntry == nullptr);
    DwarfUnit::AccelInfo Info;

    Info.Type = DwarfUnit::AccelType::Namespace;
    Info.String = Name;
    Info.OutOffset = OutDIE->getOffset();
    Info.Tag = Tag;

    OutUnit.getAsCompileUnit()->saveAcceleratorInfo(Info);
    return;
  }

  assert(TypeEntry != nullptr);
  TypeUnit::TypeUnitAccelInfo Info;
  Info.Type = DwarfUnit::AccelType::Namespace;
  Info.String = Name;
  Info.OutOffset = 0xbaddef;
  Info.Tag = Tag;
  Info.OutDIE = OutDIE;
  Info.TypeEntryBodyPtr = TypeEntry->getValue().load();

  OutUnit.getAsTypeUnit()->saveAcceleratorInfo(Info);
}

void AcceleratorRecordsSaver::saveObjCNameRecord(StringEntry *Name, DIE *OutDIE,
                                                 dwarf::Tag Tag) {
  DwarfUnit::AccelInfo Info;

  Info.Type = DwarfUnit::AccelType::ObjC;
  Info.String = Name;
  Info.OutOffset = OutDIE->getOffset();
  Info.Tag = Tag;
  Info.AvoidForPubSections = true;

  OutUnit.getAsCompileUnit()->saveAcceleratorInfo(Info);
}

void AcceleratorRecordsSaver::saveTypeRecord(StringEntry *Name, DIE *OutDIE,
                                             dwarf::Tag Tag,
                                             uint32_t QualifiedNameHash,
                                             bool ObjcClassImplementation,
                                             TypeEntry *TypeEntry) {
  if (OutUnit.isCompileUnit()) {
    assert(TypeEntry == nullptr);
    DwarfUnit::AccelInfo Info;

    Info.Type = DwarfUnit::AccelType::Type;
    Info.String = Name;
    Info.OutOffset = OutDIE->getOffset();
    Info.Tag = Tag;
    Info.QualifiedNameHash = QualifiedNameHash;
    Info.ObjcClassImplementation = ObjcClassImplementation;

    OutUnit.getAsCompileUnit()->saveAcceleratorInfo(Info);
    return;
  }

  assert(TypeEntry != nullptr);
  TypeUnit::TypeUnitAccelInfo Info;

  Info.Type = DwarfUnit::AccelType::Type;
  Info.String = Name;
  Info.OutOffset = 0xbaddef;
  Info.Tag = Tag;
  Info.QualifiedNameHash = QualifiedNameHash;
  Info.ObjcClassImplementation = ObjcClassImplementation;
  Info.OutDIE = OutDIE;
  Info.TypeEntryBodyPtr = TypeEntry->getValue().load();
  OutUnit.getAsTypeUnit()->saveAcceleratorInfo(Info);
}
