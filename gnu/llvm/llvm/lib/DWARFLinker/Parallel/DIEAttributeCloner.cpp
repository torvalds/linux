//=== DIEAttributeCloner.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DIEAttributeCloner.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

void DIEAttributeCloner::clone() {
  // Extract and clone every attribute.
  DWARFDataExtractor Data = InUnit.getOrigUnit().getDebugInfoExtractor();

  uint64_t Offset = InputDieEntry->getOffset();
  // Point to the next DIE (generally there is always at least a NULL
  // entry after the current one). If this is a lone
  // DW_TAG_compile_unit without any children, point to the next unit.
  uint64_t NextOffset = (InputDIEIdx + 1 < InUnit.getOrigUnit().getNumDIEs())
                            ? InUnit.getDIEAtIndex(InputDIEIdx + 1).getOffset()
                            : InUnit.getOrigUnit().getNextUnitOffset();

  // We could copy the data only if we need to apply a relocation to it. After
  // testing, it seems there is no performance downside to doing the copy
  // unconditionally, and it makes the code simpler.
  SmallString<40> DIECopy(Data.getData().substr(Offset, NextOffset - Offset));
  Data =
      DWARFDataExtractor(DIECopy, Data.isLittleEndian(), Data.getAddressSize());

  // Modify the copy with relocated addresses.
  InUnit.getContaingFile().Addresses->applyValidRelocs(DIECopy, Offset,
                                                       Data.isLittleEndian());

  // Reset the Offset to 0 as we will be working on the local copy of
  // the data.
  Offset = 0;

  const auto *Abbrev = InputDieEntry->getAbbreviationDeclarationPtr();
  Offset += getULEB128Size(Abbrev->getCode());

  // Set current output offset.
  AttrOutOffset = OutUnit.isCompileUnit() ? OutDIE->getOffset() : 0;
  for (const auto &AttrSpec : Abbrev->attributes()) {
    // Check whether current attribute should be skipped.
    if (shouldSkipAttribute(AttrSpec)) {
      DWARFFormValue::skipValue(AttrSpec.Form, Data, &Offset,
                                InUnit.getFormParams());
      continue;
    }

    DWARFFormValue Val = AttrSpec.getFormValue();
    Val.extractValue(Data, &Offset, InUnit.getFormParams(),
                     &InUnit.getOrigUnit());

    // Clone current attribute.
    switch (AttrSpec.Form) {
    case dwarf::DW_FORM_strp:
    case dwarf::DW_FORM_line_strp:
    case dwarf::DW_FORM_string:
    case dwarf::DW_FORM_strx:
    case dwarf::DW_FORM_strx1:
    case dwarf::DW_FORM_strx2:
    case dwarf::DW_FORM_strx3:
    case dwarf::DW_FORM_strx4:
      AttrOutOffset += cloneStringAttr(Val, AttrSpec);
      break;
    case dwarf::DW_FORM_ref_addr:
    case dwarf::DW_FORM_ref1:
    case dwarf::DW_FORM_ref2:
    case dwarf::DW_FORM_ref4:
    case dwarf::DW_FORM_ref8:
    case dwarf::DW_FORM_ref_udata:
      AttrOutOffset += cloneDieRefAttr(Val, AttrSpec);
      break;
    case dwarf::DW_FORM_data1:
    case dwarf::DW_FORM_data2:
    case dwarf::DW_FORM_data4:
    case dwarf::DW_FORM_data8:
    case dwarf::DW_FORM_udata:
    case dwarf::DW_FORM_sdata:
    case dwarf::DW_FORM_sec_offset:
    case dwarf::DW_FORM_flag:
    case dwarf::DW_FORM_flag_present:
    case dwarf::DW_FORM_rnglistx:
    case dwarf::DW_FORM_loclistx:
    case dwarf::DW_FORM_implicit_const:
      AttrOutOffset += cloneScalarAttr(Val, AttrSpec);
      break;
    case dwarf::DW_FORM_block:
    case dwarf::DW_FORM_block1:
    case dwarf::DW_FORM_block2:
    case dwarf::DW_FORM_block4:
    case dwarf::DW_FORM_exprloc:
      AttrOutOffset += cloneBlockAttr(Val, AttrSpec);
      break;
    case dwarf::DW_FORM_addr:
    case dwarf::DW_FORM_addrx:
    case dwarf::DW_FORM_addrx1:
    case dwarf::DW_FORM_addrx2:
    case dwarf::DW_FORM_addrx3:
    case dwarf::DW_FORM_addrx4:
      AttrOutOffset += cloneAddressAttr(Val, AttrSpec);
      break;
    default:
      InUnit.warn("unsupported attribute form " +
                      dwarf::FormEncodingString(AttrSpec.Form) +
                      " in DieAttributeCloner::clone(). Dropping.",
                  InputDieEntry);
    }
  }

  // We convert source strings into the indexed form for DWARFv5.
  // Check if original compile unit already has DW_AT_str_offsets_base
  // attribute.
  if (InputDieEntry->getTag() == dwarf::DW_TAG_compile_unit &&
      InUnit.getVersion() >= 5 && !AttrInfo.HasStringOffsetBaseAttr) {
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugOffsetPatch{AttrOutOffset,
                         &OutUnit->getOrCreateSectionDescriptor(
                             DebugSectionKind::DebugStrOffsets),
                         true},
        PatchesOffsets);

    AttrOutOffset +=
        Generator
            .addScalarAttribute(dwarf::DW_AT_str_offsets_base,
                                dwarf::DW_FORM_sec_offset,
                                OutUnit->getDebugStrOffsetsHeaderSize())
            .second;
  }
}

bool DIEAttributeCloner::shouldSkipAttribute(
    DWARFAbbreviationDeclaration::AttributeSpec AttrSpec) {
  switch (AttrSpec.Attr) {
  default:
    return false;
  case dwarf::DW_AT_low_pc:
  case dwarf::DW_AT_high_pc:
  case dwarf::DW_AT_ranges:
    if (InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly)
      return false;

    // Skip address attribute if we are in function scope and function does not
    // reference live address.
    return InUnit.getDIEInfo(InputDIEIdx).getIsInFunctionScope() &&
           !FuncAddressAdjustment.has_value();
  case dwarf::DW_AT_rnglists_base:
    // In case !Update the .debug_addr table is not generated/preserved.
    // Thus instead of DW_FORM_rnglistx the DW_FORM_sec_offset is used.
    // Since DW_AT_rnglists_base is used for only DW_FORM_rnglistx the
    // DW_AT_rnglists_base is removed.
    return !InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly;
  case dwarf::DW_AT_loclists_base:
    // In case !Update the .debug_addr table is not generated/preserved.
    // Thus instead of DW_FORM_loclistx the DW_FORM_sec_offset is used.
    // Since DW_AT_loclists_base is used for only DW_FORM_loclistx the
    // DW_AT_loclists_base is removed.
    return !InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly;
  case dwarf::DW_AT_location:
  case dwarf::DW_AT_frame_base:
    if (InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly)
      return false;

    // When location expression contains an address: skip this attribute
    // if it does not reference live address.
    if (HasLocationExpressionAddress)
      return !VarAddressAdjustment.has_value();

    // Skip location attribute if we are in function scope and function does not
    // reference live address.
    return InUnit.getDIEInfo(InputDIEIdx).getIsInFunctionScope() &&
           !FuncAddressAdjustment.has_value();
  }
}

size_t DIEAttributeCloner::cloneStringAttr(
    const DWARFFormValue &Val,
    const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec) {
  std::optional<const char *> String = dwarf::toString(Val);
  if (!String) {
    InUnit.warn("cann't read string attribute.");
    return 0;
  }

  StringEntry *StringInPool =
      InUnit.getGlobalData().getStringPool().insert(*String).first;

  // Update attributes info.
  if (AttrSpec.Attr == dwarf::DW_AT_name)
    AttrInfo.Name = StringInPool;
  else if (AttrSpec.Attr == dwarf::DW_AT_MIPS_linkage_name ||
           AttrSpec.Attr == dwarf::DW_AT_linkage_name)
    AttrInfo.MangledName = StringInPool;

  if (AttrSpec.Form == dwarf::DW_FORM_line_strp) {
    if (OutUnit.isTypeUnit()) {
      DebugInfoOutputSection.notePatch(DebugTypeLineStrPatch{
          AttrOutOffset, OutDIE, InUnit.getDieTypeEntry(InputDIEIdx),
          StringInPool});
    } else {
      DebugInfoOutputSection.notePatchWithOffsetUpdate(
          DebugLineStrPatch{{AttrOutOffset}, StringInPool}, PatchesOffsets);
    }
    return Generator
        .addStringPlaceholderAttribute(AttrSpec.Attr, dwarf::DW_FORM_line_strp)
        .second;
  }

  if (Use_DW_FORM_strp) {
    if (OutUnit.isTypeUnit()) {
      DebugInfoOutputSection.notePatch(
          DebugTypeStrPatch{AttrOutOffset, OutDIE,
                            InUnit.getDieTypeEntry(InputDIEIdx), StringInPool});
    } else {
      DebugInfoOutputSection.notePatchWithOffsetUpdate(
          DebugStrPatch{{AttrOutOffset}, StringInPool}, PatchesOffsets);
    }

    return Generator
        .addStringPlaceholderAttribute(AttrSpec.Attr, dwarf::DW_FORM_strp)
        .second;
  }

  return Generator
      .addIndexedStringAttribute(AttrSpec.Attr, dwarf::DW_FORM_strx,
                                 OutUnit->getDebugStrIndex(StringInPool))
      .second;
}

size_t DIEAttributeCloner::cloneDieRefAttr(
    const DWARFFormValue &Val,
    const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec) {
  if (AttrSpec.Attr == dwarf::DW_AT_sibling)
    return 0;

  std::optional<UnitEntryPairTy> RefDiePair =
      InUnit.resolveDIEReference(Val, ResolveInterCUReferencesMode::Resolve);
  if (!RefDiePair || !RefDiePair->DieEntry) {
    // If the referenced DIE is not found,  drop the attribute.
    InUnit.warn("cann't find referenced DIE.", InputDieEntry);
    return 0;
  }

  TypeEntry *RefTypeName = nullptr;
  const CompileUnit::DIEInfo &RefDIEInfo =
      RefDiePair->CU->getDIEInfo(RefDiePair->DieEntry);
  if (RefDIEInfo.needToPlaceInTypeTable())
    RefTypeName = RefDiePair->CU->getDieTypeEntry(RefDiePair->DieEntry);

  if (OutUnit.isTypeUnit()) {
    assert(RefTypeName && "Type name for referenced DIE is not set");
    assert(InUnit.getDieTypeEntry(InputDIEIdx) &&
           "Type name for DIE is not set");

    DebugInfoOutputSection.notePatch(DebugType2TypeDieRefPatch{
        AttrOutOffset, OutDIE, InUnit.getDieTypeEntry(InputDIEIdx),
        RefTypeName});

    return Generator
        .addScalarAttribute(AttrSpec.Attr, dwarf::DW_FORM_ref4, 0xBADDEF)
        .second;
  }

  if (RefTypeName) {
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugDieTypeRefPatch{AttrOutOffset, RefTypeName}, PatchesOffsets);

    return Generator
        .addScalarAttribute(AttrSpec.Attr, dwarf::DW_FORM_ref_addr, 0xBADDEF)
        .second;
  }

  // Get output offset for referenced DIE.
  uint64_t OutDieOffset = RefDiePair->CU->getDieOutOffset(RefDiePair->DieEntry);

  // Examine whether referenced DIE is in current compile unit.
  bool IsLocal = OutUnit->getUniqueID() == RefDiePair->CU->getUniqueID();

  // Set attribute form basing on the kind of referenced DIE(local or not?).
  dwarf::Form NewForm = IsLocal ? dwarf::DW_FORM_ref4 : dwarf::DW_FORM_ref_addr;

  // Check whether current attribute references already cloned DIE inside
  // the same compilation unit. If true - write the already known offset value.
  if (IsLocal && (OutDieOffset != 0))
    return Generator.addScalarAttribute(AttrSpec.Attr, NewForm, OutDieOffset)
        .second;

  // If offset value is not known at this point then create patch for the
  // reference value and write dummy value into the attribute.
  DebugInfoOutputSection.notePatchWithOffsetUpdate(
      DebugDieRefPatch{AttrOutOffset, OutUnit.getAsCompileUnit(),
                       RefDiePair->CU,
                       RefDiePair->CU->getDIEIndex(RefDiePair->DieEntry)},
      PatchesOffsets);
  return Generator.addScalarAttribute(AttrSpec.Attr, NewForm, 0xBADDEF).second;
}

size_t DIEAttributeCloner::cloneScalarAttr(
    const DWARFFormValue &Val,
    const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec) {

  // Create patches for attribute referencing other non invariant section.
  // Invariant section could not be updated here as this section and
  // reference to it do not change value in case --update.
  switch (AttrSpec.Attr) {
  case dwarf::DW_AT_macro_info: {
    if (std::optional<uint64_t> Offset = Val.getAsSectionOffset()) {
      const DWARFDebugMacro *Macro =
          InUnit.getContaingFile().Dwarf->getDebugMacinfo();
      if (Macro == nullptr || !Macro->hasEntryForOffset(*Offset))
        return 0;

      DebugInfoOutputSection.notePatchWithOffsetUpdate(
          DebugOffsetPatch{AttrOutOffset,
                           &OutUnit->getOrCreateSectionDescriptor(
                               DebugSectionKind::DebugMacinfo)},
          PatchesOffsets);
    }
  } break;
  case dwarf::DW_AT_macros: {
    if (std::optional<uint64_t> Offset = Val.getAsSectionOffset()) {
      const DWARFDebugMacro *Macro =
          InUnit.getContaingFile().Dwarf->getDebugMacro();
      if (Macro == nullptr || !Macro->hasEntryForOffset(*Offset))
        return 0;

      DebugInfoOutputSection.notePatchWithOffsetUpdate(
          DebugOffsetPatch{AttrOutOffset,
                           &OutUnit->getOrCreateSectionDescriptor(
                               DebugSectionKind::DebugMacro)},
          PatchesOffsets);
    }
  } break;
  case dwarf::DW_AT_stmt_list: {
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugOffsetPatch{AttrOutOffset, &OutUnit->getOrCreateSectionDescriptor(
                                            DebugSectionKind::DebugLine)},
        PatchesOffsets);
  } break;
  case dwarf::DW_AT_str_offsets_base: {
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugOffsetPatch{AttrOutOffset,
                         &OutUnit->getOrCreateSectionDescriptor(
                             DebugSectionKind::DebugStrOffsets),
                         true},
        PatchesOffsets);

    // Use size of .debug_str_offsets header as attribute value. The offset
    // to .debug_str_offsets would be added later while patching.
    AttrInfo.HasStringOffsetBaseAttr = true;
    return Generator
        .addScalarAttribute(AttrSpec.Attr, AttrSpec.Form,
                            OutUnit->getDebugStrOffsetsHeaderSize())
        .second;
  } break;
  case dwarf::DW_AT_decl_file: {
    // Value of DW_AT_decl_file may exceed original form. Longer
    // form can affect offsets to the following attributes. To not
    // update offsets of the following attributes we always remove
    // original DW_AT_decl_file and attach it to the last position
    // later.
    if (OutUnit.isTypeUnit()) {
      if (std::optional<std::pair<StringRef, StringRef>> DirAndFilename =
              InUnit.getDirAndFilenameFromLineTable(Val))
        DebugInfoOutputSection.notePatch(DebugTypeDeclFilePatch{
            OutDIE,
            InUnit.getDieTypeEntry(InputDIEIdx),
            OutUnit->getGlobalData()
                .getStringPool()
                .insert(DirAndFilename->first)
                .first,
            OutUnit->getGlobalData()
                .getStringPool()
                .insert(DirAndFilename->second)
                .first,
        });
      return 0;
    }
  } break;
  default: {
  } break;
  };

  uint64_t Value;
  if (AttrSpec.Attr == dwarf::DW_AT_const_value &&
      (InputDieEntry->getTag() == dwarf::DW_TAG_variable ||
       InputDieEntry->getTag() == dwarf::DW_TAG_constant))
    AttrInfo.HasLiveAddress = true;

  if (InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly) {
    if (auto OptionalValue = Val.getAsUnsignedConstant())
      Value = *OptionalValue;
    else if (auto OptionalValue = Val.getAsSignedConstant())
      Value = *OptionalValue;
    else if (auto OptionalValue = Val.getAsSectionOffset())
      Value = *OptionalValue;
    else {
      InUnit.warn("unsupported scalar attribute form. Dropping attribute.",
                  InputDieEntry);
      return 0;
    }

    if (AttrSpec.Attr == dwarf::DW_AT_declaration && Value)
      AttrInfo.IsDeclaration = true;

    if (AttrSpec.Form == dwarf::DW_FORM_loclistx)
      return Generator.addLocListAttribute(AttrSpec.Attr, AttrSpec.Form, Value)
          .second;

    return Generator.addScalarAttribute(AttrSpec.Attr, AttrSpec.Form, Value)
        .second;
  }

  dwarf::Form ResultingForm = AttrSpec.Form;
  if (AttrSpec.Form == dwarf::DW_FORM_rnglistx) {
    // DWARFLinker does not generate .debug_addr table. Thus we need to change
    // all "addrx" related forms to "addr" version. Change DW_FORM_rnglistx
    // to DW_FORM_sec_offset here.
    std::optional<uint64_t> Index = Val.getAsSectionOffset();
    if (!Index) {
      InUnit.warn("cann't read the attribute. Dropping.", InputDieEntry);
      return 0;
    }
    std::optional<uint64_t> Offset =
        InUnit.getOrigUnit().getRnglistOffset(*Index);
    if (!Offset) {
      InUnit.warn("cann't read the attribute. Dropping.", InputDieEntry);
      return 0;
    }

    Value = *Offset;
    ResultingForm = dwarf::DW_FORM_sec_offset;
  } else if (AttrSpec.Form == dwarf::DW_FORM_loclistx) {
    // DWARFLinker does not generate .debug_addr table. Thus we need to change
    // all "addrx" related forms to "addr" version. Change DW_FORM_loclistx
    // to DW_FORM_sec_offset here.
    std::optional<uint64_t> Index = Val.getAsSectionOffset();
    if (!Index) {
      InUnit.warn("cann't read the attribute. Dropping.", InputDieEntry);
      return 0;
    }
    std::optional<uint64_t> Offset =
        InUnit.getOrigUnit().getLoclistOffset(*Index);
    if (!Offset) {
      InUnit.warn("cann't read the attribute. Dropping.", InputDieEntry);
      return 0;
    }

    Value = *Offset;
    ResultingForm = dwarf::DW_FORM_sec_offset;
  } else if (AttrSpec.Attr == dwarf::DW_AT_high_pc &&
             InputDieEntry->getTag() == dwarf::DW_TAG_compile_unit) {
    if (!OutUnit.isCompileUnit())
      return 0;

    std::optional<uint64_t> LowPC = OutUnit.getAsCompileUnit()->getLowPc();
    if (!LowPC)
      return 0;
    // Dwarf >= 4 high_pc is an size, not an address.
    Value = OutUnit.getAsCompileUnit()->getHighPc() - *LowPC;
  } else if (AttrSpec.Form == dwarf::DW_FORM_sec_offset)
    Value = *Val.getAsSectionOffset();
  else if (AttrSpec.Form == dwarf::DW_FORM_sdata)
    Value = *Val.getAsSignedConstant();
  else if (auto OptionalValue = Val.getAsUnsignedConstant())
    Value = *OptionalValue;
  else {
    InUnit.warn("unsupported scalar attribute form. Dropping attribute.",
                InputDieEntry);
    return 0;
  }

  if (AttrSpec.Attr == dwarf::DW_AT_ranges ||
      AttrSpec.Attr == dwarf::DW_AT_start_scope) {
    // Create patch for the range offset value.
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugRangePatch{{AttrOutOffset},
                        InputDieEntry->getTag() == dwarf::DW_TAG_compile_unit},
        PatchesOffsets);
    AttrInfo.HasRanges = true;
  } else if (DWARFAttribute::mayHaveLocationList(AttrSpec.Attr) &&
             dwarf::doesFormBelongToClass(AttrSpec.Form,
                                          DWARFFormValue::FC_SectionOffset,
                                          InUnit.getOrigUnit().getVersion())) {
    int64_t AddrAdjustmentValue = 0;
    if (VarAddressAdjustment)
      AddrAdjustmentValue = *VarAddressAdjustment;
    else if (FuncAddressAdjustment)
      AddrAdjustmentValue = *FuncAddressAdjustment;

    // Create patch for the location offset value.
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugLocPatch{{AttrOutOffset}, AddrAdjustmentValue}, PatchesOffsets);
  } else if (AttrSpec.Attr == dwarf::DW_AT_addr_base) {
    DebugInfoOutputSection.notePatchWithOffsetUpdate(
        DebugOffsetPatch{
            AttrOutOffset,
            &OutUnit->getOrCreateSectionDescriptor(DebugSectionKind::DebugAddr),
            true},
        PatchesOffsets);

    // Use size of .debug_addr header as attribute value. The offset to
    // .debug_addr would be added later while patching.
    return Generator
        .addScalarAttribute(AttrSpec.Attr, AttrSpec.Form,
                            OutUnit->getDebugAddrHeaderSize())
        .second;
  } else if (AttrSpec.Attr == dwarf::DW_AT_declaration && Value)
    AttrInfo.IsDeclaration = true;

  return Generator.addScalarAttribute(AttrSpec.Attr, ResultingForm, Value)
      .second;
}

size_t DIEAttributeCloner::cloneBlockAttr(
    const DWARFFormValue &Val,
    const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec) {

  if (OutUnit.isTypeUnit())
    return 0;

  size_t NumberOfPatchesAtStart = PatchesOffsets.size();

  // If the block is a DWARF Expression, clone it into the temporary
  // buffer using cloneExpression(), otherwise copy the data directly.
  SmallVector<uint8_t, 32> Buffer;
  ArrayRef<uint8_t> Bytes = *Val.getAsBlock();
  if (DWARFAttribute::mayHaveLocationExpr(AttrSpec.Attr) &&
      (Val.isFormClass(DWARFFormValue::FC_Block) ||
       Val.isFormClass(DWARFFormValue::FC_Exprloc))) {
    DataExtractor Data(StringRef((const char *)Bytes.data(), Bytes.size()),
                       InUnit.getOrigUnit().isLittleEndian(),
                       InUnit.getOrigUnit().getAddressByteSize());
    DWARFExpression Expr(Data, InUnit.getOrigUnit().getAddressByteSize(),
                         InUnit.getFormParams().Format);

    InUnit.cloneDieAttrExpression(Expr, Buffer, DebugInfoOutputSection,
                                  VarAddressAdjustment, PatchesOffsets);
    Bytes = Buffer;
  }

  // The expression location data might be updated and exceed the original size.
  // Check whether the new data fits into the original form.
  dwarf::Form ResultForm = AttrSpec.Form;
  if ((ResultForm == dwarf::DW_FORM_block1 && Bytes.size() > UINT8_MAX) ||
      (ResultForm == dwarf::DW_FORM_block2 && Bytes.size() > UINT16_MAX) ||
      (ResultForm == dwarf::DW_FORM_block4 && Bytes.size() > UINT32_MAX))
    ResultForm = dwarf::DW_FORM_block;

  size_t FinalAttributeSize;
  if (AttrSpec.Form == dwarf::DW_FORM_exprloc)
    FinalAttributeSize =
        Generator.addLocationAttribute(AttrSpec.Attr, ResultForm, Bytes).second;
  else
    FinalAttributeSize =
        Generator.addBlockAttribute(AttrSpec.Attr, ResultForm, Bytes).second;

  // Update patches offsets with the size of length field for Bytes.
  for (size_t Idx = NumberOfPatchesAtStart; Idx < PatchesOffsets.size();
       Idx++) {
    assert(FinalAttributeSize > Bytes.size());
    *PatchesOffsets[Idx] +=
        (AttrOutOffset + (FinalAttributeSize - Bytes.size()));
  }

  if (HasLocationExpressionAddress)
    AttrInfo.HasLiveAddress =
        VarAddressAdjustment.has_value() ||
        InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly;

  return FinalAttributeSize;
}

size_t DIEAttributeCloner::cloneAddressAttr(
    const DWARFFormValue &Val,
    const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec) {
  if (AttrSpec.Attr == dwarf::DW_AT_low_pc)
    AttrInfo.HasLiveAddress = true;

  if (InUnit.getGlobalData().getOptions().UpdateIndexTablesOnly)
    return Generator
        .addScalarAttribute(AttrSpec.Attr, AttrSpec.Form, Val.getRawUValue())
        .second;

  if (OutUnit.isTypeUnit())
    return 0;

  // Cloned Die may have address attributes relocated to a
  // totally unrelated value. This can happen:
  //   - If high_pc is an address (Dwarf version == 2), then it might have been
  //     relocated to a totally unrelated value (because the end address in the
  //     object file might be start address of another function which got moved
  //     independently by the linker).
  //   - If address relocated in an inline_subprogram that happens at the
  //     beginning of its inlining function.
  //  To avoid above cases and to not apply relocation twice (in
  //  applyValidRelocs and here), read address attribute from InputDIE and apply
  //  Info.PCOffset here.

  std::optional<DWARFFormValue> AddrAttribute =
      InUnit.find(InputDieEntry, AttrSpec.Attr);
  if (!AddrAttribute)
    llvm_unreachable("Cann't find attribute");

  std::optional<uint64_t> Addr = AddrAttribute->getAsAddress();
  if (!Addr) {
    InUnit.warn("cann't read address attribute value.");
    return 0;
  }

  if (InputDieEntry->getTag() == dwarf::DW_TAG_compile_unit &&
      AttrSpec.Attr == dwarf::DW_AT_low_pc) {
    if (std::optional<uint64_t> LowPC = OutUnit.getAsCompileUnit()->getLowPc())
      Addr = *LowPC;
    else
      return 0;
  } else if (InputDieEntry->getTag() == dwarf::DW_TAG_compile_unit &&
             AttrSpec.Attr == dwarf::DW_AT_high_pc) {
    if (uint64_t HighPc = OutUnit.getAsCompileUnit()->getHighPc())
      Addr = HighPc;
    else
      return 0;
  } else {
    if (VarAddressAdjustment)
      *Addr += *VarAddressAdjustment;
    else if (FuncAddressAdjustment)
      *Addr += *FuncAddressAdjustment;
  }

  if (AttrSpec.Form == dwarf::DW_FORM_addr) {
    return Generator.addScalarAttribute(AttrSpec.Attr, AttrSpec.Form, *Addr)
        .second;
  }

  return Generator
      .addScalarAttribute(AttrSpec.Attr, dwarf::Form::DW_FORM_addrx,
                          OutUnit.getAsCompileUnit()->getDebugAddrIndex(*Addr))
      .second;
}

unsigned DIEAttributeCloner::finalizeAbbreviations(bool HasChildrenToClone) {
  // Add the size of the abbreviation number to the output offset.
  AttrOutOffset +=
      Generator.finalizeAbbreviations(HasChildrenToClone, &PatchesOffsets);

  return AttrOutOffset;
}
