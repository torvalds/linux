//===------ dwarf2yaml.cpp - obj2yaml conversion tool -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAddr.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugArangeSet.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/ObjectYAML/DWARFYAML.h"

#include <algorithm>
#include <optional>

using namespace llvm;

Error dumpDebugAbbrev(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  auto AbbrevSetPtr = DCtx.getDebugAbbrev();
  if (AbbrevSetPtr) {
    uint64_t AbbrevTableID = 0;
    if (Error Err = AbbrevSetPtr->parse())
      return Err;
    for (const auto &AbbrvDeclSet : *AbbrevSetPtr) {
      Y.DebugAbbrev.emplace_back();
      Y.DebugAbbrev.back().ID = AbbrevTableID++;
      for (const DWARFAbbreviationDeclaration &AbbrvDecl :
           AbbrvDeclSet.second) {
        DWARFYAML::Abbrev Abbrv;
        Abbrv.Code = AbbrvDecl.getCode();
        Abbrv.Tag = AbbrvDecl.getTag();
        Abbrv.Children = AbbrvDecl.hasChildren() ? dwarf::DW_CHILDREN_yes
                                                 : dwarf::DW_CHILDREN_no;
        for (auto Attribute : AbbrvDecl.attributes()) {
          DWARFYAML::AttributeAbbrev AttAbrv;
          AttAbrv.Attribute = Attribute.Attr;
          AttAbrv.Form = Attribute.Form;
          if (AttAbrv.Form == dwarf::DW_FORM_implicit_const)
            AttAbrv.Value = Attribute.getImplicitConstValue();
          Abbrv.Attributes.push_back(AttAbrv);
        }
        Y.DebugAbbrev.back().Table.push_back(Abbrv);
      }
    }
  }
  return Error::success();
}

Error dumpDebugAddr(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  DWARFDebugAddrTable AddrTable;
  DWARFDataExtractor AddrData(DCtx.getDWARFObj(),
                              DCtx.getDWARFObj().getAddrSection(),
                              DCtx.isLittleEndian(), /*AddressSize=*/0);
  std::vector<DWARFYAML::AddrTableEntry> AddrTables;
  uint64_t Offset = 0;
  while (AddrData.isValidOffset(Offset)) {
    // We ignore any errors that don't prevent parsing the section, since we can
    // still represent such sections.
    if (Error Err = AddrTable.extractV5(AddrData, &Offset, /*CUAddrSize=*/0,
                                        consumeError))
      return Err;
    AddrTables.emplace_back();
    for (uint64_t Addr : AddrTable.getAddressEntries()) {
      // Currently, the parser doesn't support parsing an address table with non
      // linear addresses (segment_selector_size != 0). The segment selectors
      // are specified to be zero.
      AddrTables.back().SegAddrPairs.push_back(
          {/*SegmentSelector=*/0, /*Address=*/Addr});
    }

    AddrTables.back().Format = AddrTable.getFormat();
    AddrTables.back().Length = AddrTable.getLength();
    AddrTables.back().Version = AddrTable.getVersion();
    AddrTables.back().AddrSize = AddrTable.getAddressSize();
    AddrTables.back().SegSelectorSize = AddrTable.getSegmentSelectorSize();
  }
  Y.DebugAddr = std::move(AddrTables);
  return Error::success();
}

Error dumpDebugStrings(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  DataExtractor StrData = DCtx.getStringExtractor();
  uint64_t Offset = 0;
  std::vector<StringRef> DebugStr;
  Error Err = Error::success();
  while (StrData.isValidOffset(Offset)) {
    const char *CStr = StrData.getCStr(&Offset, &Err);
    if (Err)
      return Err;
    DebugStr.push_back(CStr);
  }

  Y.DebugStrings = DebugStr;
  return Err;
}

Error dumpDebugARanges(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  DWARFDataExtractor ArangesData(DCtx.getDWARFObj().getArangesSection(),
                                 DCtx.isLittleEndian(), 0);
  uint64_t Offset = 0;
  DWARFDebugArangeSet Set;
  std::vector<DWARFYAML::ARange> DebugAranges;

  // We ignore any errors that don't prevent parsing the section, since we can
  // still represent such sections. These errors are recorded via the
  // WarningHandler parameter of Set.extract().
  auto DiscardError = [](Error Err) { consumeError(std::move(Err)); };

  while (ArangesData.isValidOffset(Offset)) {
    if (Error E = Set.extract(ArangesData, &Offset, DiscardError))
      return E;
    DWARFYAML::ARange Range;
    Range.Format = Set.getHeader().Format;
    Range.Length = Set.getHeader().Length;
    Range.Version = Set.getHeader().Version;
    Range.CuOffset = Set.getHeader().CuOffset;
    Range.AddrSize = Set.getHeader().AddrSize;
    Range.SegSize = Set.getHeader().SegSize;
    for (auto Descriptor : Set.descriptors()) {
      DWARFYAML::ARangeDescriptor Desc;
      Desc.Address = Descriptor.Address;
      Desc.Length = Descriptor.Length;
      Range.Descriptors.push_back(Desc);
    }
    DebugAranges.push_back(Range);
  }

  Y.DebugAranges = DebugAranges;
  return ErrorSuccess();
}

Error dumpDebugRanges(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  // We are assuming all address byte sizes will be consistent across all
  // compile units.
  uint8_t AddrSize = 0;
  for (const auto &CU : DCtx.compile_units()) {
    const uint8_t CUAddrSize = CU->getAddressByteSize();
    if (AddrSize == 0)
      AddrSize = CUAddrSize;
    else if (CUAddrSize != AddrSize)
      return createStringError(std::errc::invalid_argument,
                               "address sizes vary in different compile units");
  }

  DWARFDataExtractor Data(DCtx.getDWARFObj().getRangesSection().Data,
                          DCtx.isLittleEndian(), AddrSize);
  uint64_t Offset = 0;
  DWARFDebugRangeList DwarfRanges;
  std::vector<DWARFYAML::Ranges> DebugRanges;

  while (Data.isValidOffset(Offset)) {
    DWARFYAML::Ranges YamlRanges;
    YamlRanges.Offset = Offset;
    YamlRanges.AddrSize = AddrSize;
    if (Error E = DwarfRanges.extract(Data, &Offset))
      return E;
    for (const auto &RLE : DwarfRanges.getEntries())
      YamlRanges.Entries.push_back({RLE.StartAddress, RLE.EndAddress});
    DebugRanges.push_back(std::move(YamlRanges));
  }

  Y.DebugRanges = DebugRanges;
  return ErrorSuccess();
}

static std::optional<DWARFYAML::PubSection>
dumpPubSection(const DWARFContext &DCtx, const DWARFSection &Section,
               bool IsGNUStyle) {
  DWARFYAML::PubSection Y;
  DWARFDataExtractor PubSectionData(DCtx.getDWARFObj(), Section,
                                    DCtx.isLittleEndian(), 0);
  DWARFDebugPubTable Table;
  // We ignore any errors that don't prevent parsing the section, since we can
  // still represent such sections.
  Table.extract(PubSectionData, IsGNUStyle,
                [](Error Err) { consumeError(std::move(Err)); });
  ArrayRef<DWARFDebugPubTable::Set> Sets = Table.getData();
  if (Sets.empty())
    return std::nullopt;

  // FIXME: Currently, obj2yaml only supports dumping the first pubtable.
  Y.Format = Sets[0].Format;
  Y.Length = Sets[0].Length;
  Y.Version = Sets[0].Version;
  Y.UnitOffset = Sets[0].Offset;
  Y.UnitSize = Sets[0].Size;

  for (const DWARFDebugPubTable::Entry &E : Sets[0].Entries)
    Y.Entries.push_back(DWARFYAML::PubEntry{(uint32_t)E.SecOffset,
                                            E.Descriptor.toBits(), E.Name});

  return Y;
}

void dumpDebugPubSections(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  const DWARFObject &D = DCtx.getDWARFObj();

  Y.PubNames =
      dumpPubSection(DCtx, D.getPubnamesSection(), /*IsGNUStyle=*/false);
  Y.PubTypes =
      dumpPubSection(DCtx, D.getPubtypesSection(), /*IsGNUStyle=*/false);
  // TODO: Test dumping .debug_gnu_pubnames section.
  Y.GNUPubNames =
      dumpPubSection(DCtx, D.getGnuPubnamesSection(), /*IsGNUStyle=*/true);
  // TODO: Test dumping .debug_gnu_pubtypes section.
  Y.GNUPubTypes =
      dumpPubSection(DCtx, D.getGnuPubtypesSection(), /*IsGNUStyle=*/true);
}

void dumpDebugInfo(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  for (const auto &CU : DCtx.compile_units()) {
    DWARFYAML::Unit NewUnit;
    NewUnit.Format = CU->getFormat();
    NewUnit.Length = CU->getLength();
    NewUnit.Version = CU->getVersion();
    if (NewUnit.Version >= 5)
      NewUnit.Type = (dwarf::UnitType)CU->getUnitType();
    const DWARFDebugAbbrev *DebugAbbrev = DCtx.getDebugAbbrev();
    // FIXME: Ideally we would propagate this error upwards, but that would
    // prevent us from displaying any debug info at all. For now we just consume
    // the error and display everything that was parsed successfully.
    if (Error Err = DebugAbbrev->parse())
      llvm::consumeError(std::move(Err));

    NewUnit.AbbrevTableID = std::distance(
        DebugAbbrev->begin(),
        llvm::find_if(
            *DebugAbbrev,
            [&](const std::pair<uint64_t, DWARFAbbreviationDeclarationSet> &P) {
              return P.first == CU->getAbbreviations()->getOffset();
            }));
    NewUnit.AbbrOffset = CU->getAbbreviations()->getOffset();
    NewUnit.AddrSize = CU->getAddressByteSize();
    for (auto DIE : CU->dies()) {
      DWARFYAML::Entry NewEntry;
      DataExtractor EntryData = CU->getDebugInfoExtractor();
      uint64_t offset = DIE.getOffset();

      assert(EntryData.isValidOffset(offset) && "Invalid DIE Offset");
      if (!EntryData.isValidOffset(offset))
        continue;

      NewEntry.AbbrCode = EntryData.getULEB128(&offset);

      auto AbbrevDecl = DIE.getAbbreviationDeclarationPtr();
      if (AbbrevDecl) {
        for (const auto &AttrSpec : AbbrevDecl->attributes()) {
          DWARFYAML::FormValue NewValue;
          NewValue.Value = 0xDEADBEEFDEADBEEF;
          DWARFDie DIEWrapper(CU.get(), &DIE);
          auto FormValue = DIEWrapper.find(AttrSpec.Attr);
          if (!FormValue)
            return;
          auto Form = FormValue->getForm();
          bool indirect = false;
          do {
            indirect = false;
            switch (Form) {
            case dwarf::DW_FORM_addr:
            case dwarf::DW_FORM_GNU_addr_index:
              if (auto Val = FormValue->getAsAddress())
                NewValue.Value = *Val;
              break;
            case dwarf::DW_FORM_ref_addr:
            case dwarf::DW_FORM_ref1:
            case dwarf::DW_FORM_ref2:
            case dwarf::DW_FORM_ref4:
            case dwarf::DW_FORM_ref8:
            case dwarf::DW_FORM_ref_udata:
            case dwarf::DW_FORM_ref_sig8:
              if (auto Val = FormValue->getAsReferenceUVal())
                NewValue.Value = *Val;
              break;
            case dwarf::DW_FORM_exprloc:
            case dwarf::DW_FORM_block:
            case dwarf::DW_FORM_block1:
            case dwarf::DW_FORM_block2:
            case dwarf::DW_FORM_block4:
              if (auto Val = FormValue->getAsBlock()) {
                auto BlockData = *Val;
                std::copy(BlockData.begin(), BlockData.end(),
                          std::back_inserter(NewValue.BlockData));
              }
              NewValue.Value = NewValue.BlockData.size();
              break;
            case dwarf::DW_FORM_data1:
            case dwarf::DW_FORM_flag:
            case dwarf::DW_FORM_data2:
            case dwarf::DW_FORM_data4:
            case dwarf::DW_FORM_data8:
            case dwarf::DW_FORM_sdata:
            case dwarf::DW_FORM_udata:
            case dwarf::DW_FORM_ref_sup4:
            case dwarf::DW_FORM_ref_sup8:
              if (auto Val = FormValue->getAsUnsignedConstant())
                NewValue.Value = *Val;
              break;
            case dwarf::DW_FORM_string:
              if (auto Val = dwarf::toString(FormValue))
                NewValue.CStr = *Val;
              break;
            case dwarf::DW_FORM_indirect:
              indirect = true;
              if (auto Val = FormValue->getAsUnsignedConstant()) {
                NewValue.Value = *Val;
                NewEntry.Values.push_back(NewValue);
                Form = static_cast<dwarf::Form>(*Val);
              }
              break;
            case dwarf::DW_FORM_strp:
            case dwarf::DW_FORM_sec_offset:
            case dwarf::DW_FORM_GNU_ref_alt:
            case dwarf::DW_FORM_GNU_strp_alt:
            case dwarf::DW_FORM_line_strp:
            case dwarf::DW_FORM_strp_sup:
            case dwarf::DW_FORM_GNU_str_index:
            case dwarf::DW_FORM_strx:
              if (auto Val = FormValue->getAsCStringOffset())
                NewValue.Value = *Val;
              break;
            case dwarf::DW_FORM_flag_present:
              NewValue.Value = 1;
              break;
            default:
              break;
            }
          } while (indirect);
          NewEntry.Values.push_back(NewValue);
        }
      }

      NewUnit.Entries.push_back(NewEntry);
    }
    Y.CompileUnits.push_back(NewUnit);
  }
}

bool dumpFileEntry(DataExtractor &Data, uint64_t &Offset,
                   DWARFYAML::File &File) {
  File.Name = Data.getCStr(&Offset);
  if (File.Name.empty())
    return false;
  File.DirIdx = Data.getULEB128(&Offset);
  File.ModTime = Data.getULEB128(&Offset);
  File.Length = Data.getULEB128(&Offset);
  return true;
}

void dumpDebugLines(DWARFContext &DCtx, DWARFYAML::Data &Y) {
  for (const auto &CU : DCtx.compile_units()) {
    auto CUDIE = CU->getUnitDIE();
    if (!CUDIE)
      continue;
    if (auto StmtOffset =
            dwarf::toSectionOffset(CUDIE.find(dwarf::DW_AT_stmt_list))) {
      DWARFYAML::LineTable DebugLines;
      DataExtractor LineData(DCtx.getDWARFObj().getLineSection().Data,
                             DCtx.isLittleEndian(), CU->getAddressByteSize());
      uint64_t Offset = *StmtOffset;
      uint64_t LengthOrDWARF64Prefix = LineData.getU32(&Offset);
      if (LengthOrDWARF64Prefix == dwarf::DW_LENGTH_DWARF64) {
        DebugLines.Format = dwarf::DWARF64;
        DebugLines.Length = LineData.getU64(&Offset);
      } else {
        DebugLines.Format = dwarf::DWARF32;
        DebugLines.Length = LengthOrDWARF64Prefix;
      }
      assert(DebugLines.Length);
      uint64_t LineTableLength = *DebugLines.Length;
      uint64_t SizeOfPrologueLength =
          DebugLines.Format == dwarf::DWARF64 ? 8 : 4;
      DebugLines.Version = LineData.getU16(&Offset);
      DebugLines.PrologueLength =
          LineData.getUnsigned(&Offset, SizeOfPrologueLength);
      assert(DebugLines.PrologueLength);
      const uint64_t EndPrologue = *DebugLines.PrologueLength + Offset;

      DebugLines.MinInstLength = LineData.getU8(&Offset);
      if (DebugLines.Version >= 4)
        DebugLines.MaxOpsPerInst = LineData.getU8(&Offset);
      DebugLines.DefaultIsStmt = LineData.getU8(&Offset);
      DebugLines.LineBase = LineData.getU8(&Offset);
      DebugLines.LineRange = LineData.getU8(&Offset);
      DebugLines.OpcodeBase = LineData.getU8(&Offset);

      DebugLines.StandardOpcodeLengths.emplace();
      for (uint8_t i = 1; i < DebugLines.OpcodeBase; ++i)
        DebugLines.StandardOpcodeLengths->push_back(LineData.getU8(&Offset));

      while (Offset < EndPrologue) {
        StringRef Dir = LineData.getCStr(&Offset);
        if (!Dir.empty())
          DebugLines.IncludeDirs.push_back(Dir);
        else
          break;
      }

      while (Offset < EndPrologue) {
        DWARFYAML::File TmpFile;
        if (dumpFileEntry(LineData, Offset, TmpFile))
          DebugLines.Files.push_back(TmpFile);
        else
          break;
      }

      const uint64_t LineEnd =
          LineTableLength + *StmtOffset + SizeOfPrologueLength;
      while (Offset < LineEnd) {
        DWARFYAML::LineTableOpcode NewOp = {};
        NewOp.Opcode = (dwarf::LineNumberOps)LineData.getU8(&Offset);
        if (NewOp.Opcode == 0) {
          auto StartExt = Offset;
          NewOp.ExtLen = LineData.getULEB128(&Offset);
          NewOp.SubOpcode =
              (dwarf::LineNumberExtendedOps)LineData.getU8(&Offset);
          switch (NewOp.SubOpcode) {
          case dwarf::DW_LNE_set_address:
          case dwarf::DW_LNE_set_discriminator:
            NewOp.Data = LineData.getAddress(&Offset);
            break;
          case dwarf::DW_LNE_define_file:
            dumpFileEntry(LineData, Offset, NewOp.FileEntry);
            break;
          case dwarf::DW_LNE_end_sequence:
            break;
          default:
            while (Offset < StartExt + *NewOp.ExtLen)
              NewOp.UnknownOpcodeData.push_back(LineData.getU8(&Offset));
          }
        } else if (NewOp.Opcode < *DebugLines.OpcodeBase) {
          switch (NewOp.Opcode) {
          case dwarf::DW_LNS_copy:
          case dwarf::DW_LNS_negate_stmt:
          case dwarf::DW_LNS_set_basic_block:
          case dwarf::DW_LNS_const_add_pc:
          case dwarf::DW_LNS_set_prologue_end:
          case dwarf::DW_LNS_set_epilogue_begin:
            break;

          case dwarf::DW_LNS_advance_pc:
          case dwarf::DW_LNS_set_file:
          case dwarf::DW_LNS_set_column:
          case dwarf::DW_LNS_set_isa:
            NewOp.Data = LineData.getULEB128(&Offset);
            break;

          case dwarf::DW_LNS_advance_line:
            NewOp.SData = LineData.getSLEB128(&Offset);
            break;

          case dwarf::DW_LNS_fixed_advance_pc:
            NewOp.Data = LineData.getU16(&Offset);
            break;

          default:
            for (uint8_t i = 0;
                 i < (*DebugLines.StandardOpcodeLengths)[NewOp.Opcode - 1]; ++i)
              NewOp.StandardOpcodeData.push_back(LineData.getULEB128(&Offset));
          }
        }
        DebugLines.Opcodes.push_back(NewOp);
      }
      Y.DebugLines.push_back(DebugLines);
    }
  }
}
