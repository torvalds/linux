//===- DWARFLinkerUnit.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFLinkerUnit.h"
#include "DWARFEmitterImpl.h"
#include "DebugLineSectionEmitter.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

void DwarfUnit::assignAbbrev(DIEAbbrev &Abbrev) {
  // Check the set for priors.
  FoldingSetNodeID ID;
  Abbrev.Profile(ID);
  void *InsertToken;

  DIEAbbrev *InSet = AbbreviationsSet.FindNodeOrInsertPos(ID, InsertToken);
  // If it's newly added.
  if (InSet) {
    // Assign existing abbreviation number.
    Abbrev.setNumber(InSet->getNumber());
  } else {
    // Add to abbreviation list.
    Abbreviations.push_back(
        std::make_unique<DIEAbbrev>(Abbrev.getTag(), Abbrev.hasChildren()));
    for (const auto &Attr : Abbrev.getData())
      Abbreviations.back()->AddAttribute(Attr);
    AbbreviationsSet.InsertNode(Abbreviations.back().get(), InsertToken);
    // Assign the unique abbreviation number.
    Abbrev.setNumber(Abbreviations.size());
    Abbreviations.back()->setNumber(Abbreviations.size());
  }
}

Error DwarfUnit::emitAbbreviations() {
  const std::vector<std::unique_ptr<DIEAbbrev>> &Abbrevs = getAbbreviations();
  if (Abbrevs.empty())
    return Error::success();

  SectionDescriptor &AbbrevSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugAbbrev);

  // For each abbreviation.
  for (const auto &Abbrev : Abbrevs)
    emitDwarfAbbrevEntry(*Abbrev, AbbrevSection);

  // Mark end of abbreviations.
  encodeULEB128(0, AbbrevSection.OS);

  return Error::success();
}

void DwarfUnit::emitDwarfAbbrevEntry(const DIEAbbrev &Abbrev,
                                     SectionDescriptor &AbbrevSection) {
  // Emit the abbreviations code (base 1 index.)
  encodeULEB128(Abbrev.getNumber(), AbbrevSection.OS);

  // Emit the abbreviations data.
  // Emit its Dwarf tag type.
  encodeULEB128(Abbrev.getTag(), AbbrevSection.OS);

  // Emit whether it has children DIEs.
  encodeULEB128((unsigned)Abbrev.hasChildren(), AbbrevSection.OS);

  // For each attribute description.
  const SmallVectorImpl<DIEAbbrevData> &Data = Abbrev.getData();
  for (const DIEAbbrevData &AttrData : Data) {
    // Emit attribute type.
    encodeULEB128(AttrData.getAttribute(), AbbrevSection.OS);

    // Emit form type.
    encodeULEB128(AttrData.getForm(), AbbrevSection.OS);

    // Emit value for DW_FORM_implicit_const.
    if (AttrData.getForm() == dwarf::DW_FORM_implicit_const)
      encodeSLEB128(AttrData.getValue(), AbbrevSection.OS);
  }

  // Mark end of abbreviation.
  encodeULEB128(0, AbbrevSection.OS);
  encodeULEB128(0, AbbrevSection.OS);
}

Error DwarfUnit::emitDebugInfo(const Triple &TargetTriple) {
  DIE *OutUnitDIE = getOutUnitDIE();
  if (OutUnitDIE == nullptr)
    return Error::success();

  // FIXME: Remove dependence on DwarfEmitterImpl/AsmPrinter and emit DIEs
  // directly.

  SectionDescriptor &OutSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
  DwarfEmitterImpl Emitter(DWARFLinker::OutputFileType::Object, OutSection.OS);
  if (Error Err = Emitter.init(TargetTriple, "__DWARF"))
    return Err;

  // Emit compile unit header.
  Emitter.emitCompileUnitHeader(*this);
  size_t OffsetToAbbreviationTableOffset =
      (getFormParams().Version >= 5) ? 8 : 6;
  OutSection.notePatch(DebugOffsetPatch{
      OffsetToAbbreviationTableOffset,
      &getOrCreateSectionDescriptor(DebugSectionKind::DebugAbbrev)});

  // Emit DIEs.
  Emitter.emitDIE(*OutUnitDIE);
  Emitter.finish();

  // Set start offset ans size for .debug_info section.
  OutSection.setSizesForSectionCreatedByAsmPrinter();
  return Error::success();
}

Error DwarfUnit::emitDebugLine(const Triple &TargetTriple,
                               const DWARFDebugLine::LineTable &OutLineTable) {
  DebugLineSectionEmitter DebugLineEmitter(TargetTriple, *this);

  return DebugLineEmitter.emit(OutLineTable);
}

Error DwarfUnit::emitDebugStringOffsetSection() {
  if (getVersion() < 5)
    return Error::success();

  if (DebugStringIndexMap.empty())
    return Error::success();

  SectionDescriptor &OutDebugStrOffsetsSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugStrOffsets);

  // Emit section header.

  //   Emit length.
  OutDebugStrOffsetsSection.emitUnitLength(0xBADDEF);
  uint64_t OffsetAfterSectionLength = OutDebugStrOffsetsSection.OS.tell();

  //   Emit version.
  OutDebugStrOffsetsSection.emitIntVal(5, 2);

  //   Emit padding.
  OutDebugStrOffsetsSection.emitIntVal(0, 2);

  //   Emit index to offset map.
  for (const StringEntry *String : DebugStringIndexMap.getValues()) {
    // Note patch for string offset value.
    OutDebugStrOffsetsSection.notePatch(
        DebugStrPatch{{OutDebugStrOffsetsSection.OS.tell()}, String});

    // Emit placeholder for offset value.
    OutDebugStrOffsetsSection.emitOffset(0xBADDEF);
  }

  // Patch section length.
  OutDebugStrOffsetsSection.apply(
      OffsetAfterSectionLength -
          OutDebugStrOffsetsSection.getFormParams().getDwarfOffsetByteSize(),
      dwarf::DW_FORM_sec_offset,
      OutDebugStrOffsetsSection.OS.tell() - OffsetAfterSectionLength);

  return Error::success();
}

/// Emit the pubnames or pubtypes section contribution for \p
/// Unit into \p Sec. The data is provided in \p Info.
std::optional<uint64_t>
DwarfUnit::emitPubAcceleratorEntry(SectionDescriptor &OutSection,
                                   const DwarfUnit::AccelInfo &Info,
                                   std::optional<uint64_t> LengthOffset) {
  if (!LengthOffset) {
    // Emit the header.
    OutSection.emitIntVal(0xBADDEF,
                          getFormParams().getDwarfOffsetByteSize()); // Length
    LengthOffset = OutSection.OS.tell();

    OutSection.emitIntVal(dwarf::DW_PUBNAMES_VERSION, 2); // Version

    OutSection.notePatch(DebugOffsetPatch{
        OutSection.OS.tell(),
        &getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo)});
    OutSection.emitOffset(0xBADDEF); // Unit offset

    OutSection.emitIntVal(getUnitSize(), 4); // Size
  }
  OutSection.emitOffset(Info.OutOffset);

  // Emit the string itself.
  OutSection.emitInplaceString(Info.String->first());

  return LengthOffset;
}

/// Emit .debug_pubnames and .debug_pubtypes for \p Unit.
void DwarfUnit::emitPubAccelerators() {
  std::optional<uint64_t> NamesLengthOffset;
  std::optional<uint64_t> TypesLengthOffset;

  forEachAcceleratorRecord([&](const DwarfUnit::AccelInfo &Info) {
    if (Info.AvoidForPubSections)
      return;

    switch (Info.Type) {
    case DwarfUnit::AccelType::Name: {
      NamesLengthOffset = emitPubAcceleratorEntry(
          getOrCreateSectionDescriptor(DebugSectionKind::DebugPubNames), Info,
          NamesLengthOffset);
    } break;
    case DwarfUnit::AccelType::Type: {
      TypesLengthOffset = emitPubAcceleratorEntry(
          getOrCreateSectionDescriptor(DebugSectionKind::DebugPubTypes), Info,
          TypesLengthOffset);
    } break;
    default: {
      // Nothing to do.
    } break;
    }
  });

  if (NamesLengthOffset) {
    SectionDescriptor &OutSection =
        getOrCreateSectionDescriptor(DebugSectionKind::DebugPubNames);
    OutSection.emitIntVal(0, 4); // End marker.

    OutSection.apply(*NamesLengthOffset -
                         OutSection.getFormParams().getDwarfOffsetByteSize(),
                     dwarf::DW_FORM_sec_offset,
                     OutSection.OS.tell() - *NamesLengthOffset);
  }

  if (TypesLengthOffset) {
    SectionDescriptor &OutSection =
        getOrCreateSectionDescriptor(DebugSectionKind::DebugPubTypes);
    OutSection.emitIntVal(0, 4); // End marker.

    OutSection.apply(*TypesLengthOffset -
                         OutSection.getFormParams().getDwarfOffsetByteSize(),
                     dwarf::DW_FORM_sec_offset,
                     OutSection.OS.tell() - *TypesLengthOffset);
  }
}
