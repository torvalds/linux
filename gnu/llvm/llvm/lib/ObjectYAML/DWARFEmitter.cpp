//===- DWARFEmitter - Convert YAML to DWARF binary data -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The DWARF component of yaml2obj. Provided as library code for tests.
///
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/DWARFEmitter.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

template <typename T>
static void writeInteger(T Integer, raw_ostream &OS, bool IsLittleEndian) {
  if (IsLittleEndian != sys::IsLittleEndianHost)
    sys::swapByteOrder(Integer);
  OS.write(reinterpret_cast<char *>(&Integer), sizeof(T));
}

static Error writeVariableSizedInteger(uint64_t Integer, size_t Size,
                                       raw_ostream &OS, bool IsLittleEndian) {
  if (8 == Size)
    writeInteger((uint64_t)Integer, OS, IsLittleEndian);
  else if (4 == Size)
    writeInteger((uint32_t)Integer, OS, IsLittleEndian);
  else if (2 == Size)
    writeInteger((uint16_t)Integer, OS, IsLittleEndian);
  else if (1 == Size)
    writeInteger((uint8_t)Integer, OS, IsLittleEndian);
  else
    return createStringError(errc::not_supported,
                             "invalid integer write size: %zu", Size);

  return Error::success();
}

static void ZeroFillBytes(raw_ostream &OS, size_t Size) {
  std::vector<uint8_t> FillData(Size, 0);
  OS.write(reinterpret_cast<char *>(FillData.data()), Size);
}

static void writeInitialLength(const dwarf::DwarfFormat Format,
                               const uint64_t Length, raw_ostream &OS,
                               bool IsLittleEndian) {
  bool IsDWARF64 = Format == dwarf::DWARF64;
  if (IsDWARF64)
    cantFail(writeVariableSizedInteger(dwarf::DW_LENGTH_DWARF64, 4, OS,
                                       IsLittleEndian));
  cantFail(
      writeVariableSizedInteger(Length, IsDWARF64 ? 8 : 4, OS, IsLittleEndian));
}

static void writeDWARFOffset(uint64_t Offset, dwarf::DwarfFormat Format,
                             raw_ostream &OS, bool IsLittleEndian) {
  cantFail(writeVariableSizedInteger(Offset, Format == dwarf::DWARF64 ? 8 : 4,
                                     OS, IsLittleEndian));
}

Error DWARFYAML::emitDebugStr(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (StringRef Str : *DI.DebugStrings) {
    OS.write(Str.data(), Str.size());
    OS.write('\0');
  }

  return Error::success();
}

StringRef DWARFYAML::Data::getAbbrevTableContentByIndex(uint64_t Index) const {
  assert(Index < DebugAbbrev.size() &&
         "Index should be less than the size of DebugAbbrev array");
  auto It = AbbrevTableContents.find(Index);
  if (It != AbbrevTableContents.cend())
    return It->second;

  std::string AbbrevTableBuffer;
  raw_string_ostream OS(AbbrevTableBuffer);

  uint64_t AbbrevCode = 0;
  for (const DWARFYAML::Abbrev &AbbrevDecl : DebugAbbrev[Index].Table) {
    AbbrevCode = AbbrevDecl.Code ? (uint64_t)*AbbrevDecl.Code : AbbrevCode + 1;
    encodeULEB128(AbbrevCode, OS);
    encodeULEB128(AbbrevDecl.Tag, OS);
    OS.write(AbbrevDecl.Children);
    for (const auto &Attr : AbbrevDecl.Attributes) {
      encodeULEB128(Attr.Attribute, OS);
      encodeULEB128(Attr.Form, OS);
      if (Attr.Form == dwarf::DW_FORM_implicit_const)
        encodeSLEB128(Attr.Value, OS);
    }
    encodeULEB128(0, OS);
    encodeULEB128(0, OS);
  }

  // The abbreviations for a given compilation unit end with an entry
  // consisting of a 0 byte for the abbreviation code.
  OS.write_zeros(1);

  AbbrevTableContents.insert({Index, AbbrevTableBuffer});

  return AbbrevTableContents[Index];
}

Error DWARFYAML::emitDebugAbbrev(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (uint64_t I = 0; I < DI.DebugAbbrev.size(); ++I) {
    StringRef AbbrevTableContent = DI.getAbbrevTableContentByIndex(I);
    OS.write(AbbrevTableContent.data(), AbbrevTableContent.size());
  }

  return Error::success();
}

Error DWARFYAML::emitDebugAranges(raw_ostream &OS, const DWARFYAML::Data &DI) {
  assert(DI.DebugAranges && "unexpected emitDebugAranges() call");
  for (const auto &Range : *DI.DebugAranges) {
    uint8_t AddrSize;
    if (Range.AddrSize)
      AddrSize = *Range.AddrSize;
    else
      AddrSize = DI.Is64BitAddrSize ? 8 : 4;

    uint64_t Length = 4; // sizeof(version) 2 + sizeof(address_size) 1 +
                         // sizeof(segment_selector_size) 1
    Length +=
        Range.Format == dwarf::DWARF64 ? 8 : 4; // sizeof(debug_info_offset)

    const uint64_t HeaderLength =
        Length + (Range.Format == dwarf::DWARF64
                      ? 12
                      : 4); // sizeof(unit_header) = 12 (DWARF64) or 4 (DWARF32)
    const uint64_t PaddedHeaderLength = alignTo(HeaderLength, AddrSize * 2);

    if (Range.Length) {
      Length = *Range.Length;
    } else {
      Length += PaddedHeaderLength - HeaderLength;
      Length += AddrSize * 2 * (Range.Descriptors.size() + 1);
    }

    writeInitialLength(Range.Format, Length, OS, DI.IsLittleEndian);
    writeInteger((uint16_t)Range.Version, OS, DI.IsLittleEndian);
    writeDWARFOffset(Range.CuOffset, Range.Format, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)AddrSize, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)Range.SegSize, OS, DI.IsLittleEndian);
    ZeroFillBytes(OS, PaddedHeaderLength - HeaderLength);

    for (const auto &Descriptor : Range.Descriptors) {
      if (Error Err = writeVariableSizedInteger(Descriptor.Address, AddrSize,
                                                OS, DI.IsLittleEndian))
        return createStringError(errc::not_supported,
                                 "unable to write debug_aranges address: %s",
                                 toString(std::move(Err)).c_str());
      cantFail(writeVariableSizedInteger(Descriptor.Length, AddrSize, OS,
                                         DI.IsLittleEndian));
    }
    ZeroFillBytes(OS, AddrSize * 2);
  }

  return Error::success();
}

Error DWARFYAML::emitDebugRanges(raw_ostream &OS, const DWARFYAML::Data &DI) {
  const size_t RangesOffset = OS.tell();
  uint64_t EntryIndex = 0;
  for (const auto &DebugRanges : *DI.DebugRanges) {
    const size_t CurrOffset = OS.tell() - RangesOffset;
    if (DebugRanges.Offset && (uint64_t)*DebugRanges.Offset < CurrOffset)
      return createStringError(errc::invalid_argument,
                               "'Offset' for 'debug_ranges' with index " +
                                   Twine(EntryIndex) +
                                   " must be greater than or equal to the "
                                   "number of bytes written already (0x" +
                                   Twine::utohexstr(CurrOffset) + ")");
    if (DebugRanges.Offset)
      ZeroFillBytes(OS, *DebugRanges.Offset - CurrOffset);

    uint8_t AddrSize;
    if (DebugRanges.AddrSize)
      AddrSize = *DebugRanges.AddrSize;
    else
      AddrSize = DI.Is64BitAddrSize ? 8 : 4;
    for (const auto &Entry : DebugRanges.Entries) {
      if (Error Err = writeVariableSizedInteger(Entry.LowOffset, AddrSize, OS,
                                                DI.IsLittleEndian))
        return createStringError(
            errc::not_supported,
            "unable to write debug_ranges address offset: %s",
            toString(std::move(Err)).c_str());
      cantFail(writeVariableSizedInteger(Entry.HighOffset, AddrSize, OS,
                                         DI.IsLittleEndian));
    }
    ZeroFillBytes(OS, AddrSize * 2);
    ++EntryIndex;
  }

  return Error::success();
}

static Error emitPubSection(raw_ostream &OS, const DWARFYAML::PubSection &Sect,
                            bool IsLittleEndian, bool IsGNUPubSec = false) {
  writeInitialLength(Sect.Format, Sect.Length, OS, IsLittleEndian);
  writeInteger((uint16_t)Sect.Version, OS, IsLittleEndian);
  writeInteger((uint32_t)Sect.UnitOffset, OS, IsLittleEndian);
  writeInteger((uint32_t)Sect.UnitSize, OS, IsLittleEndian);
  for (const auto &Entry : Sect.Entries) {
    writeInteger((uint32_t)Entry.DieOffset, OS, IsLittleEndian);
    if (IsGNUPubSec)
      writeInteger((uint8_t)Entry.Descriptor, OS, IsLittleEndian);
    OS.write(Entry.Name.data(), Entry.Name.size());
    OS.write('\0');
  }
  return Error::success();
}

Error DWARFYAML::emitDebugPubnames(raw_ostream &OS, const Data &DI) {
  assert(DI.PubNames && "unexpected emitDebugPubnames() call");
  return emitPubSection(OS, *DI.PubNames, DI.IsLittleEndian);
}

Error DWARFYAML::emitDebugPubtypes(raw_ostream &OS, const Data &DI) {
  assert(DI.PubTypes && "unexpected emitDebugPubtypes() call");
  return emitPubSection(OS, *DI.PubTypes, DI.IsLittleEndian);
}

Error DWARFYAML::emitDebugGNUPubnames(raw_ostream &OS, const Data &DI) {
  assert(DI.GNUPubNames && "unexpected emitDebugGNUPubnames() call");
  return emitPubSection(OS, *DI.GNUPubNames, DI.IsLittleEndian,
                        /*IsGNUStyle=*/true);
}

Error DWARFYAML::emitDebugGNUPubtypes(raw_ostream &OS, const Data &DI) {
  assert(DI.GNUPubTypes && "unexpected emitDebugGNUPubtypes() call");
  return emitPubSection(OS, *DI.GNUPubTypes, DI.IsLittleEndian,
                        /*IsGNUStyle=*/true);
}

static Expected<uint64_t> writeDIE(const DWARFYAML::Data &DI, uint64_t CUIndex,
                                   uint64_t AbbrevTableID,
                                   const dwarf::FormParams &Params,
                                   const DWARFYAML::Entry &Entry,
                                   raw_ostream &OS, bool IsLittleEndian) {
  uint64_t EntryBegin = OS.tell();
  encodeULEB128(Entry.AbbrCode, OS);
  uint32_t AbbrCode = Entry.AbbrCode;
  if (AbbrCode == 0 || Entry.Values.empty())
    return OS.tell() - EntryBegin;

  Expected<DWARFYAML::Data::AbbrevTableInfo> AbbrevTableInfoOrErr =
      DI.getAbbrevTableInfoByID(AbbrevTableID);
  if (!AbbrevTableInfoOrErr)
    return createStringError(errc::invalid_argument,
                             toString(AbbrevTableInfoOrErr.takeError()) +
                                 " for compilation unit with index " +
                                 utostr(CUIndex));

  ArrayRef<DWARFYAML::Abbrev> AbbrevDecls(
      DI.DebugAbbrev[AbbrevTableInfoOrErr->Index].Table);

  if (AbbrCode > AbbrevDecls.size())
    return createStringError(
        errc::invalid_argument,
        "abbrev code must be less than or equal to the number of "
        "entries in abbreviation table");
  const DWARFYAML::Abbrev &Abbrev = AbbrevDecls[AbbrCode - 1];
  auto FormVal = Entry.Values.begin();
  auto AbbrForm = Abbrev.Attributes.begin();
  for (; FormVal != Entry.Values.end() && AbbrForm != Abbrev.Attributes.end();
       ++FormVal, ++AbbrForm) {
    dwarf::Form Form = AbbrForm->Form;
    bool Indirect;
    do {
      Indirect = false;
      switch (Form) {
      case dwarf::DW_FORM_addr:
        // TODO: Test this error.
        if (Error Err = writeVariableSizedInteger(
                FormVal->Value, Params.AddrSize, OS, IsLittleEndian))
          return std::move(Err);
        break;
      case dwarf::DW_FORM_ref_addr:
        // TODO: Test this error.
        if (Error Err = writeVariableSizedInteger(FormVal->Value,
                                                  Params.getRefAddrByteSize(),
                                                  OS, IsLittleEndian))
          return std::move(Err);
        break;
      case dwarf::DW_FORM_exprloc:
      case dwarf::DW_FORM_block:
        encodeULEB128(FormVal->BlockData.size(), OS);
        OS.write((const char *)FormVal->BlockData.data(),
                 FormVal->BlockData.size());
        break;
      case dwarf::DW_FORM_block1: {
        writeInteger((uint8_t)FormVal->BlockData.size(), OS, IsLittleEndian);
        OS.write((const char *)FormVal->BlockData.data(),
                 FormVal->BlockData.size());
        break;
      }
      case dwarf::DW_FORM_block2: {
        writeInteger((uint16_t)FormVal->BlockData.size(), OS, IsLittleEndian);
        OS.write((const char *)FormVal->BlockData.data(),
                 FormVal->BlockData.size());
        break;
      }
      case dwarf::DW_FORM_block4: {
        writeInteger((uint32_t)FormVal->BlockData.size(), OS, IsLittleEndian);
        OS.write((const char *)FormVal->BlockData.data(),
                 FormVal->BlockData.size());
        break;
      }
      case dwarf::DW_FORM_strx:
      case dwarf::DW_FORM_addrx:
      case dwarf::DW_FORM_rnglistx:
      case dwarf::DW_FORM_loclistx:
      case dwarf::DW_FORM_udata:
      case dwarf::DW_FORM_ref_udata:
      case dwarf::DW_FORM_GNU_addr_index:
      case dwarf::DW_FORM_GNU_str_index:
        encodeULEB128(FormVal->Value, OS);
        break;
      case dwarf::DW_FORM_data1:
      case dwarf::DW_FORM_ref1:
      case dwarf::DW_FORM_flag:
      case dwarf::DW_FORM_strx1:
      case dwarf::DW_FORM_addrx1:
        writeInteger((uint8_t)FormVal->Value, OS, IsLittleEndian);
        break;
      case dwarf::DW_FORM_data2:
      case dwarf::DW_FORM_ref2:
      case dwarf::DW_FORM_strx2:
      case dwarf::DW_FORM_addrx2:
        writeInteger((uint16_t)FormVal->Value, OS, IsLittleEndian);
        break;
      case dwarf::DW_FORM_data4:
      case dwarf::DW_FORM_ref4:
      case dwarf::DW_FORM_ref_sup4:
      case dwarf::DW_FORM_strx4:
      case dwarf::DW_FORM_addrx4:
        writeInteger((uint32_t)FormVal->Value, OS, IsLittleEndian);
        break;
      case dwarf::DW_FORM_data8:
      case dwarf::DW_FORM_ref8:
      case dwarf::DW_FORM_ref_sup8:
      case dwarf::DW_FORM_ref_sig8:
        writeInteger((uint64_t)FormVal->Value, OS, IsLittleEndian);
        break;
      case dwarf::DW_FORM_sdata:
        encodeSLEB128(FormVal->Value, OS);
        break;
      case dwarf::DW_FORM_string:
        OS.write(FormVal->CStr.data(), FormVal->CStr.size());
        OS.write('\0');
        break;
      case dwarf::DW_FORM_indirect:
        encodeULEB128(FormVal->Value, OS);
        Indirect = true;
        Form = static_cast<dwarf::Form>((uint64_t)FormVal->Value);
        ++FormVal;
        break;
      case dwarf::DW_FORM_strp:
      case dwarf::DW_FORM_sec_offset:
      case dwarf::DW_FORM_GNU_ref_alt:
      case dwarf::DW_FORM_GNU_strp_alt:
      case dwarf::DW_FORM_line_strp:
      case dwarf::DW_FORM_strp_sup:
        cantFail(writeVariableSizedInteger(FormVal->Value,
                                           Params.getDwarfOffsetByteSize(), OS,
                                           IsLittleEndian));
        break;
      default:
        break;
      }
    } while (Indirect);
  }

  return OS.tell() - EntryBegin;
}

Error DWARFYAML::emitDebugInfo(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (uint64_t I = 0; I < DI.CompileUnits.size(); ++I) {
    const DWARFYAML::Unit &Unit = DI.CompileUnits[I];
    uint8_t AddrSize;
    if (Unit.AddrSize)
      AddrSize = *Unit.AddrSize;
    else
      AddrSize = DI.Is64BitAddrSize ? 8 : 4;
    dwarf::FormParams Params = {Unit.Version, AddrSize, Unit.Format};
    uint64_t Length = 3; // sizeof(version) + sizeof(address_size)
    Length += Unit.Version >= 5 ? 1 : 0;       // sizeof(unit_type)
    Length += Params.getDwarfOffsetByteSize(); // sizeof(debug_abbrev_offset)

    // Since the length of the current compilation unit is undetermined yet, we
    // firstly write the content of the compilation unit to a buffer to
    // calculate it and then serialize the buffer content to the actual output
    // stream.
    std::string EntryBuffer;
    raw_string_ostream EntryBufferOS(EntryBuffer);

    uint64_t AbbrevTableID = Unit.AbbrevTableID.value_or(I);
    for (const DWARFYAML::Entry &Entry : Unit.Entries) {
      if (Expected<uint64_t> EntryLength =
              writeDIE(DI, I, AbbrevTableID, Params, Entry, EntryBufferOS,
                       DI.IsLittleEndian))
        Length += *EntryLength;
      else
        return EntryLength.takeError();
    }

    // If the length is specified in the YAML description, we use it instead of
    // the actual length.
    if (Unit.Length)
      Length = *Unit.Length;

    writeInitialLength(Unit.Format, Length, OS, DI.IsLittleEndian);
    writeInteger((uint16_t)Unit.Version, OS, DI.IsLittleEndian);

    uint64_t AbbrevTableOffset = 0;
    if (Unit.AbbrOffset) {
      AbbrevTableOffset = *Unit.AbbrOffset;
    } else {
      if (Expected<DWARFYAML::Data::AbbrevTableInfo> AbbrevTableInfoOrErr =
              DI.getAbbrevTableInfoByID(AbbrevTableID)) {
        AbbrevTableOffset = AbbrevTableInfoOrErr->Offset;
      } else {
        // The current compilation unit may not have DIEs and it will not be
        // able to find the associated abbrev table. We consume the error and
        // assign 0 to the debug_abbrev_offset in such circumstances.
        consumeError(AbbrevTableInfoOrErr.takeError());
      }
    }

    if (Unit.Version >= 5) {
      writeInteger((uint8_t)Unit.Type, OS, DI.IsLittleEndian);
      writeInteger((uint8_t)AddrSize, OS, DI.IsLittleEndian);
      writeDWARFOffset(AbbrevTableOffset, Unit.Format, OS, DI.IsLittleEndian);
    } else {
      writeDWARFOffset(AbbrevTableOffset, Unit.Format, OS, DI.IsLittleEndian);
      writeInteger((uint8_t)AddrSize, OS, DI.IsLittleEndian);
    }

    OS.write(EntryBuffer.data(), EntryBuffer.size());
  }

  return Error::success();
}

static void emitFileEntry(raw_ostream &OS, const DWARFYAML::File &File) {
  OS.write(File.Name.data(), File.Name.size());
  OS.write('\0');
  encodeULEB128(File.DirIdx, OS);
  encodeULEB128(File.ModTime, OS);
  encodeULEB128(File.Length, OS);
}

static void writeExtendedOpcode(const DWARFYAML::LineTableOpcode &Op,
                                uint8_t AddrSize, bool IsLittleEndian,
                                raw_ostream &OS) {
  // The first byte of extended opcodes is a zero byte. The next bytes are an
  // ULEB128 integer giving the number of bytes in the instruction itself (does
  // not include the first zero byte or the size). We serialize the instruction
  // itself into the OpBuffer and then write the size of the buffer and the
  // buffer to the real output stream.
  std::string OpBuffer;
  raw_string_ostream OpBufferOS(OpBuffer);
  writeInteger((uint8_t)Op.SubOpcode, OpBufferOS, IsLittleEndian);
  switch (Op.SubOpcode) {
  case dwarf::DW_LNE_set_address:
    cantFail(writeVariableSizedInteger(Op.Data, AddrSize, OpBufferOS,
                                       IsLittleEndian));
    break;
  case dwarf::DW_LNE_define_file:
    emitFileEntry(OpBufferOS, Op.FileEntry);
    break;
  case dwarf::DW_LNE_set_discriminator:
    encodeULEB128(Op.Data, OpBufferOS);
    break;
  case dwarf::DW_LNE_end_sequence:
    break;
  default:
    for (auto OpByte : Op.UnknownOpcodeData)
      writeInteger((uint8_t)OpByte, OpBufferOS, IsLittleEndian);
  }
  uint64_t ExtLen = Op.ExtLen.value_or(OpBuffer.size());
  encodeULEB128(ExtLen, OS);
  OS.write(OpBuffer.data(), OpBuffer.size());
}

static void writeLineTableOpcode(const DWARFYAML::LineTableOpcode &Op,
                                 uint8_t OpcodeBase, uint8_t AddrSize,
                                 raw_ostream &OS, bool IsLittleEndian) {
  writeInteger((uint8_t)Op.Opcode, OS, IsLittleEndian);
  if (Op.Opcode == 0) {
    writeExtendedOpcode(Op, AddrSize, IsLittleEndian, OS);
  } else if (Op.Opcode < OpcodeBase) {
    switch (Op.Opcode) {
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
      encodeULEB128(Op.Data, OS);
      break;

    case dwarf::DW_LNS_advance_line:
      encodeSLEB128(Op.SData, OS);
      break;

    case dwarf::DW_LNS_fixed_advance_pc:
      writeInteger((uint16_t)Op.Data, OS, IsLittleEndian);
      break;

    default:
      for (auto OpData : Op.StandardOpcodeData) {
        encodeULEB128(OpData, OS);
      }
    }
  }
}

static std::vector<uint8_t>
getStandardOpcodeLengths(uint16_t Version, std::optional<uint8_t> OpcodeBase) {
  // If the opcode_base field isn't specified, we returns the
  // standard_opcode_lengths array according to the version by default.
  std::vector<uint8_t> StandardOpcodeLengths{0, 1, 1, 1, 1, 0,
                                             0, 0, 1, 0, 0, 1};
  if (Version == 2) {
    // DWARF v2 uses the same first 9 standard opcodes as v3-5.
    StandardOpcodeLengths.resize(9);
  } else if (OpcodeBase) {
    StandardOpcodeLengths.resize(*OpcodeBase > 0 ? *OpcodeBase - 1 : 0, 0);
  }
  return StandardOpcodeLengths;
}

Error DWARFYAML::emitDebugLine(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (const DWARFYAML::LineTable &LineTable : DI.DebugLines) {
    // Buffer holds the bytes following the header_length (or prologue_length in
    // DWARFv2) field to the end of the line number program itself.
    std::string Buffer;
    raw_string_ostream BufferOS(Buffer);

    writeInteger(LineTable.MinInstLength, BufferOS, DI.IsLittleEndian);
    // TODO: Add support for emitting DWARFv5 line table.
    if (LineTable.Version >= 4)
      writeInteger(LineTable.MaxOpsPerInst, BufferOS, DI.IsLittleEndian);
    writeInteger(LineTable.DefaultIsStmt, BufferOS, DI.IsLittleEndian);
    writeInteger(LineTable.LineBase, BufferOS, DI.IsLittleEndian);
    writeInteger(LineTable.LineRange, BufferOS, DI.IsLittleEndian);

    std::vector<uint8_t> StandardOpcodeLengths =
        LineTable.StandardOpcodeLengths.value_or(
            getStandardOpcodeLengths(LineTable.Version, LineTable.OpcodeBase));
    uint8_t OpcodeBase = LineTable.OpcodeBase
                             ? *LineTable.OpcodeBase
                             : StandardOpcodeLengths.size() + 1;
    writeInteger(OpcodeBase, BufferOS, DI.IsLittleEndian);
    for (uint8_t OpcodeLength : StandardOpcodeLengths)
      writeInteger(OpcodeLength, BufferOS, DI.IsLittleEndian);

    for (StringRef IncludeDir : LineTable.IncludeDirs) {
      BufferOS.write(IncludeDir.data(), IncludeDir.size());
      BufferOS.write('\0');
    }
    BufferOS.write('\0');

    for (const DWARFYAML::File &File : LineTable.Files)
      emitFileEntry(BufferOS, File);
    BufferOS.write('\0');

    uint64_t HeaderLength =
        LineTable.PrologueLength ? *LineTable.PrologueLength : Buffer.size();

    for (const DWARFYAML::LineTableOpcode &Op : LineTable.Opcodes)
      writeLineTableOpcode(Op, OpcodeBase, DI.Is64BitAddrSize ? 8 : 4, BufferOS,
                           DI.IsLittleEndian);

    uint64_t Length;
    if (LineTable.Length) {
      Length = *LineTable.Length;
    } else {
      Length = 2; // sizeof(version)
      Length +=
          (LineTable.Format == dwarf::DWARF64 ? 8 : 4); // sizeof(header_length)
      Length += Buffer.size();
    }

    writeInitialLength(LineTable.Format, Length, OS, DI.IsLittleEndian);
    writeInteger(LineTable.Version, OS, DI.IsLittleEndian);
    writeDWARFOffset(HeaderLength, LineTable.Format, OS, DI.IsLittleEndian);
    OS.write(Buffer.data(), Buffer.size());
  }

  return Error::success();
}

Error DWARFYAML::emitDebugAddr(raw_ostream &OS, const Data &DI) {
  for (const AddrTableEntry &TableEntry : *DI.DebugAddr) {
    uint8_t AddrSize;
    if (TableEntry.AddrSize)
      AddrSize = *TableEntry.AddrSize;
    else
      AddrSize = DI.Is64BitAddrSize ? 8 : 4;

    uint64_t Length;
    if (TableEntry.Length)
      Length = (uint64_t)*TableEntry.Length;
    else
      // 2 (version) + 1 (address_size) + 1 (segment_selector_size) = 4
      Length = 4 + (AddrSize + TableEntry.SegSelectorSize) *
                       TableEntry.SegAddrPairs.size();

    writeInitialLength(TableEntry.Format, Length, OS, DI.IsLittleEndian);
    writeInteger((uint16_t)TableEntry.Version, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)AddrSize, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)TableEntry.SegSelectorSize, OS, DI.IsLittleEndian);

    for (const SegAddrPair &Pair : TableEntry.SegAddrPairs) {
      if (TableEntry.SegSelectorSize != yaml::Hex8{0})
        if (Error Err = writeVariableSizedInteger(Pair.Segment,
                                                  TableEntry.SegSelectorSize,
                                                  OS, DI.IsLittleEndian))
          return createStringError(errc::not_supported,
                                   "unable to write debug_addr segment: %s",
                                   toString(std::move(Err)).c_str());
      if (AddrSize != 0)
        if (Error Err = writeVariableSizedInteger(Pair.Address, AddrSize, OS,
                                                  DI.IsLittleEndian))
          return createStringError(errc::not_supported,
                                   "unable to write debug_addr address: %s",
                                   toString(std::move(Err)).c_str());
    }
  }

  return Error::success();
}

Error DWARFYAML::emitDebugStrOffsets(raw_ostream &OS, const Data &DI) {
  assert(DI.DebugStrOffsets && "unexpected emitDebugStrOffsets() call");
  for (const DWARFYAML::StringOffsetsTable &Table : *DI.DebugStrOffsets) {
    uint64_t Length;
    if (Table.Length)
      Length = *Table.Length;
    else
      // sizeof(version) + sizeof(padding) = 4
      Length =
          4 + Table.Offsets.size() * (Table.Format == dwarf::DWARF64 ? 8 : 4);

    writeInitialLength(Table.Format, Length, OS, DI.IsLittleEndian);
    writeInteger((uint16_t)Table.Version, OS, DI.IsLittleEndian);
    writeInteger((uint16_t)Table.Padding, OS, DI.IsLittleEndian);

    for (uint64_t Offset : Table.Offsets)
      writeDWARFOffset(Offset, Table.Format, OS, DI.IsLittleEndian);
  }

  return Error::success();
}

namespace {
/// Emits the header for a DebugNames section.
void emitDebugNamesHeader(raw_ostream &OS, bool IsLittleEndian,
                          uint32_t NameCount, uint32_t AbbrevSize,
                          uint32_t CombinedSizeOtherParts) {
  // Use the same AugmentationString as AsmPrinter.
  StringRef AugmentationString = "LLVM0700";
  size_t TotalSize = CombinedSizeOtherParts + 5 * sizeof(uint32_t) +
                     2 * sizeof(uint16_t) + sizeof(NameCount) +
                     sizeof(AbbrevSize) + AugmentationString.size();
  writeInteger(uint32_t(TotalSize), OS, IsLittleEndian); // Unit length

  // Everything below is included in total size.
  writeInteger(uint16_t(5), OS, IsLittleEndian); // Version
  writeInteger(uint16_t(0), OS, IsLittleEndian); // Padding
  writeInteger(uint32_t(1), OS, IsLittleEndian); // Compilation Unit count
  writeInteger(uint32_t(0), OS, IsLittleEndian); // Local Type Unit count
  writeInteger(uint32_t(0), OS, IsLittleEndian); // Foreign Type Unit count
  writeInteger(uint32_t(0), OS, IsLittleEndian); // Bucket count
  writeInteger(NameCount, OS, IsLittleEndian);
  writeInteger(AbbrevSize, OS, IsLittleEndian);
  writeInteger(uint32_t(AugmentationString.size()), OS, IsLittleEndian);
  OS.write(AugmentationString.data(), AugmentationString.size());
  return;
}

/// Emits the abbreviations for a DebugNames section.
std::string
emitDebugNamesAbbrev(ArrayRef<DWARFYAML::DebugNameAbbreviation> Abbrevs) {
  std::string Data;
  raw_string_ostream OS(Data);
  for (const DWARFYAML::DebugNameAbbreviation &Abbrev : Abbrevs) {
    encodeULEB128(Abbrev.Code, OS);
    encodeULEB128(Abbrev.Tag, OS);
    for (auto [Idx, Form] : Abbrev.Indices) {
      encodeULEB128(Idx, OS);
      encodeULEB128(Form, OS);
    }
    encodeULEB128(0, OS);
    encodeULEB128(0, OS);
  }
  encodeULEB128(0, OS);
  return Data;
}

/// Emits a simple CU offsets list for a DebugNames section containing a single
/// CU at offset 0.
std::string emitDebugNamesCUOffsets(bool IsLittleEndian) {
  std::string Data;
  raw_string_ostream OS(Data);
  writeInteger(uint32_t(0), OS, IsLittleEndian);
  return Data;
}

/// Emits the "NameTable" for a DebugNames section; according to the spec, it
/// consists of two arrays: an array of string offsets, followed immediately by
/// an array of entry offsets. The string offsets are emitted in the order
/// provided in `Entries`.
std::string emitDebugNamesNameTable(
    bool IsLittleEndian,
    const DenseMap<uint32_t, std::vector<DWARFYAML::DebugNameEntry>> &Entries,
    ArrayRef<uint32_t> EntryPoolOffsets) {
  assert(Entries.size() == EntryPoolOffsets.size());

  std::string Data;
  raw_string_ostream OS(Data);

  for (uint32_t Strp : make_first_range(Entries))
    writeInteger(Strp, OS, IsLittleEndian);
  for (uint32_t PoolOffset : EntryPoolOffsets)
    writeInteger(PoolOffset, OS, IsLittleEndian);
  return Data;
}

/// Groups entries based on their name (strp) code and returns a map.
DenseMap<uint32_t, std::vector<DWARFYAML::DebugNameEntry>>
groupEntries(ArrayRef<DWARFYAML::DebugNameEntry> Entries) {
  DenseMap<uint32_t, std::vector<DWARFYAML::DebugNameEntry>> StrpToEntries;
  for (const DWARFYAML::DebugNameEntry &Entry : Entries)
    StrpToEntries[Entry.NameStrp].push_back(Entry);
  return StrpToEntries;
}

/// Finds the abbreviation whose code is AbbrevCode and returns a list
/// containing the expected size of all non-zero-length forms.
Expected<SmallVector<uint8_t>>
getNonZeroDataSizesFor(uint32_t AbbrevCode,
                       ArrayRef<DWARFYAML::DebugNameAbbreviation> Abbrevs) {
  const auto *AbbrevIt = find_if(Abbrevs, [&](const auto &Abbrev) {
    return Abbrev.Code.value == AbbrevCode;
  });
  if (AbbrevIt == Abbrevs.end())
    return createStringError(inconvertibleErrorCode(),
                             "did not find an Abbreviation for this code");

  SmallVector<uint8_t> DataSizes;
  dwarf::FormParams Params{/*Version=*/5, /*AddrSize=*/4, dwarf::DWARF32};
  for (auto [Idx, Form] : AbbrevIt->Indices) {
    std::optional<uint8_t> FormSize = dwarf::getFixedFormByteSize(Form, Params);
    if (!FormSize)
      return createStringError(inconvertibleErrorCode(),
                               "unsupported Form for YAML debug_names emitter");
    if (FormSize == 0)
      continue;
    DataSizes.push_back(*FormSize);
  }
  return DataSizes;
}

struct PoolOffsetsAndData {
  std::string PoolData;
  std::vector<uint32_t> PoolOffsets;
};

/// Emits the entry pool and returns an array of offsets containing the start
/// offset for the entries of each unique name.
/// Verifies that the provided number of data values match those expected by
/// the abbreviation table.
Expected<PoolOffsetsAndData> emitDebugNamesEntryPool(
    bool IsLittleEndian,
    const DenseMap<uint32_t, std::vector<DWARFYAML::DebugNameEntry>>
        &StrpToEntries,
    ArrayRef<DWARFYAML::DebugNameAbbreviation> Abbrevs) {
  PoolOffsetsAndData Result;
  raw_string_ostream OS(Result.PoolData);

  for (ArrayRef<DWARFYAML::DebugNameEntry> EntriesWithSameName :
       make_second_range(StrpToEntries)) {
    Result.PoolOffsets.push_back(Result.PoolData.size());

    for (const DWARFYAML::DebugNameEntry &Entry : EntriesWithSameName) {
      encodeULEB128(Entry.Code, OS);

      Expected<SmallVector<uint8_t>> DataSizes =
          getNonZeroDataSizesFor(Entry.Code, Abbrevs);
      if (!DataSizes)
        return DataSizes.takeError();
      if (DataSizes->size() != Entry.Values.size())
        return createStringError(
            inconvertibleErrorCode(),
            "mismatch between provided and required number of values");

      for (auto [Value, ValueSize] : zip_equal(Entry.Values, *DataSizes))
        if (Error E =
                writeVariableSizedInteger(Value, ValueSize, OS, IsLittleEndian))
          return std::move(E);
    }
    encodeULEB128(0, OS);
  }

  return Result;
}
} // namespace

Error DWARFYAML::emitDebugNames(raw_ostream &OS, const Data &DI) {
  assert(DI.DebugNames && "unexpected emitDebugNames() call");
  const DebugNamesSection DebugNames = DI.DebugNames.value();

  DenseMap<uint32_t, std::vector<DebugNameEntry>> StrpToEntries =
      groupEntries(DebugNames.Entries);

  // Emit all sub-sections into individual strings so that we may compute
  // relative offsets and sizes.
  Expected<PoolOffsetsAndData> PoolInfo = emitDebugNamesEntryPool(
      DI.IsLittleEndian, StrpToEntries, DebugNames.Abbrevs);
  if (!PoolInfo)
    return PoolInfo.takeError();
  std::string NamesTableData = emitDebugNamesNameTable(
      DI.IsLittleEndian, StrpToEntries, PoolInfo->PoolOffsets);

  std::string AbbrevData = emitDebugNamesAbbrev(DebugNames.Abbrevs);
  std::string CUOffsetsData = emitDebugNamesCUOffsets(DI.IsLittleEndian);

  size_t TotalSize = PoolInfo->PoolData.size() + NamesTableData.size() +
                     AbbrevData.size() + CUOffsetsData.size();

  // Start real emission by combining all individual strings.
  emitDebugNamesHeader(OS, DI.IsLittleEndian, StrpToEntries.size(),
                       AbbrevData.size(), TotalSize);
  OS.write(CUOffsetsData.data(), CUOffsetsData.size());
  // No local TUs, no foreign TUs, no hash lookups table.
  OS.write(NamesTableData.data(), NamesTableData.size());
  OS.write(AbbrevData.data(), AbbrevData.size());
  OS.write(PoolInfo->PoolData.data(), PoolInfo->PoolData.size());

  return Error::success();
}

static Error checkOperandCount(StringRef EncodingString,
                               ArrayRef<yaml::Hex64> Values,
                               uint64_t ExpectedOperands) {
  if (Values.size() != ExpectedOperands)
    return createStringError(
        errc::invalid_argument,
        "invalid number (%zu) of operands for the operator: %s, %" PRIu64
        " expected",
        Values.size(), EncodingString.str().c_str(), ExpectedOperands);

  return Error::success();
}

static Error writeListEntryAddress(StringRef EncodingName, raw_ostream &OS,
                                   uint64_t Addr, uint8_t AddrSize,
                                   bool IsLittleEndian) {
  if (Error Err = writeVariableSizedInteger(Addr, AddrSize, OS, IsLittleEndian))
    return createStringError(errc::invalid_argument,
                             "unable to write address for the operator %s: %s",
                             EncodingName.str().c_str(),
                             toString(std::move(Err)).c_str());

  return Error::success();
}

static Expected<uint64_t>
writeDWARFExpression(raw_ostream &OS,
                     const DWARFYAML::DWARFOperation &Operation,
                     uint8_t AddrSize, bool IsLittleEndian) {
  auto CheckOperands = [&](uint64_t ExpectedOperands) -> Error {
    return checkOperandCount(dwarf::OperationEncodingString(Operation.Operator),
                             Operation.Values, ExpectedOperands);
  };

  uint64_t ExpressionBegin = OS.tell();
  writeInteger((uint8_t)Operation.Operator, OS, IsLittleEndian);
  switch (Operation.Operator) {
  case dwarf::DW_OP_consts:
    if (Error Err = CheckOperands(1))
      return std::move(Err);
    encodeSLEB128(Operation.Values[0], OS);
    break;
  case dwarf::DW_OP_stack_value:
    if (Error Err = CheckOperands(0))
      return std::move(Err);
    break;
  default:
    StringRef EncodingStr = dwarf::OperationEncodingString(Operation.Operator);
    return createStringError(errc::not_supported,
                             "DWARF expression: " +
                                 (EncodingStr.empty()
                                      ? "0x" + utohexstr(Operation.Operator)
                                      : EncodingStr) +
                                 " is not supported");
  }
  return OS.tell() - ExpressionBegin;
}

static Expected<uint64_t> writeListEntry(raw_ostream &OS,
                                         const DWARFYAML::RnglistEntry &Entry,
                                         uint8_t AddrSize,
                                         bool IsLittleEndian) {
  uint64_t BeginOffset = OS.tell();
  writeInteger((uint8_t)Entry.Operator, OS, IsLittleEndian);

  StringRef EncodingName = dwarf::RangeListEncodingString(Entry.Operator);

  auto CheckOperands = [&](uint64_t ExpectedOperands) -> Error {
    return checkOperandCount(EncodingName, Entry.Values, ExpectedOperands);
  };

  auto WriteAddress = [&](uint64_t Addr) -> Error {
    return writeListEntryAddress(EncodingName, OS, Addr, AddrSize,
                                 IsLittleEndian);
  };

  switch (Entry.Operator) {
  case dwarf::DW_RLE_end_of_list:
    if (Error Err = CheckOperands(0))
      return std::move(Err);
    break;
  case dwarf::DW_RLE_base_addressx:
    if (Error Err = CheckOperands(1))
      return std::move(Err);
    encodeULEB128(Entry.Values[0], OS);
    break;
  case dwarf::DW_RLE_startx_endx:
  case dwarf::DW_RLE_startx_length:
  case dwarf::DW_RLE_offset_pair:
    if (Error Err = CheckOperands(2))
      return std::move(Err);
    encodeULEB128(Entry.Values[0], OS);
    encodeULEB128(Entry.Values[1], OS);
    break;
  case dwarf::DW_RLE_base_address:
    if (Error Err = CheckOperands(1))
      return std::move(Err);
    if (Error Err = WriteAddress(Entry.Values[0]))
      return std::move(Err);
    break;
  case dwarf::DW_RLE_start_end:
    if (Error Err = CheckOperands(2))
      return std::move(Err);
    if (Error Err = WriteAddress(Entry.Values[0]))
      return std::move(Err);
    cantFail(WriteAddress(Entry.Values[1]));
    break;
  case dwarf::DW_RLE_start_length:
    if (Error Err = CheckOperands(2))
      return std::move(Err);
    if (Error Err = WriteAddress(Entry.Values[0]))
      return std::move(Err);
    encodeULEB128(Entry.Values[1], OS);
    break;
  }

  return OS.tell() - BeginOffset;
}

static Expected<uint64_t> writeListEntry(raw_ostream &OS,
                                         const DWARFYAML::LoclistEntry &Entry,
                                         uint8_t AddrSize,
                                         bool IsLittleEndian) {
  uint64_t BeginOffset = OS.tell();
  writeInteger((uint8_t)Entry.Operator, OS, IsLittleEndian);

  StringRef EncodingName = dwarf::LocListEncodingString(Entry.Operator);

  auto CheckOperands = [&](uint64_t ExpectedOperands) -> Error {
    return checkOperandCount(EncodingName, Entry.Values, ExpectedOperands);
  };

  auto WriteAddress = [&](uint64_t Addr) -> Error {
    return writeListEntryAddress(EncodingName, OS, Addr, AddrSize,
                                 IsLittleEndian);
  };

  auto WriteDWARFOperations = [&]() -> Error {
    std::string OpBuffer;
    raw_string_ostream OpBufferOS(OpBuffer);
    uint64_t DescriptionsLength = 0;

    for (const DWARFYAML::DWARFOperation &Op : Entry.Descriptions) {
      if (Expected<uint64_t> OpSize =
              writeDWARFExpression(OpBufferOS, Op, AddrSize, IsLittleEndian))
        DescriptionsLength += *OpSize;
      else
        return OpSize.takeError();
    }

    if (Entry.DescriptionsLength)
      DescriptionsLength = *Entry.DescriptionsLength;
    else
      DescriptionsLength = OpBuffer.size();

    encodeULEB128(DescriptionsLength, OS);
    OS.write(OpBuffer.data(), OpBuffer.size());

    return Error::success();
  };

  switch (Entry.Operator) {
  case dwarf::DW_LLE_end_of_list:
    if (Error Err = CheckOperands(0))
      return std::move(Err);
    break;
  case dwarf::DW_LLE_base_addressx:
    if (Error Err = CheckOperands(1))
      return std::move(Err);
    encodeULEB128(Entry.Values[0], OS);
    break;
  case dwarf::DW_LLE_startx_endx:
  case dwarf::DW_LLE_startx_length:
  case dwarf::DW_LLE_offset_pair:
    if (Error Err = CheckOperands(2))
      return std::move(Err);
    encodeULEB128(Entry.Values[0], OS);
    encodeULEB128(Entry.Values[1], OS);
    if (Error Err = WriteDWARFOperations())
      return std::move(Err);
    break;
  case dwarf::DW_LLE_default_location:
    if (Error Err = CheckOperands(0))
      return std::move(Err);
    if (Error Err = WriteDWARFOperations())
      return std::move(Err);
    break;
  case dwarf::DW_LLE_base_address:
    if (Error Err = CheckOperands(1))
      return std::move(Err);
    if (Error Err = WriteAddress(Entry.Values[0]))
      return std::move(Err);
    break;
  case dwarf::DW_LLE_start_end:
    if (Error Err = CheckOperands(2))
      return std::move(Err);
    if (Error Err = WriteAddress(Entry.Values[0]))
      return std::move(Err);
    cantFail(WriteAddress(Entry.Values[1]));
    if (Error Err = WriteDWARFOperations())
      return std::move(Err);
    break;
  case dwarf::DW_LLE_start_length:
    if (Error Err = CheckOperands(2))
      return std::move(Err);
    if (Error Err = WriteAddress(Entry.Values[0]))
      return std::move(Err);
    encodeULEB128(Entry.Values[1], OS);
    if (Error Err = WriteDWARFOperations())
      return std::move(Err);
    break;
  }

  return OS.tell() - BeginOffset;
}

template <typename EntryType>
static Error writeDWARFLists(raw_ostream &OS,
                             ArrayRef<DWARFYAML::ListTable<EntryType>> Tables,
                             bool IsLittleEndian, bool Is64BitAddrSize) {
  for (const DWARFYAML::ListTable<EntryType> &Table : Tables) {
    // sizeof(version) + sizeof(address_size) + sizeof(segment_selector_size) +
    // sizeof(offset_entry_count) = 8
    uint64_t Length = 8;

    uint8_t AddrSize;
    if (Table.AddrSize)
      AddrSize = *Table.AddrSize;
    else
      AddrSize = Is64BitAddrSize ? 8 : 4;

    // Since the length of the current range/location lists entry is
    // undetermined yet, we firstly write the content of the range/location
    // lists to a buffer to calculate the length and then serialize the buffer
    // content to the actual output stream.
    std::string ListBuffer;
    raw_string_ostream ListBufferOS(ListBuffer);

    // Offsets holds offsets for each range/location list. The i-th element is
    // the offset from the beginning of the first range/location list to the
    // location of the i-th range list.
    std::vector<uint64_t> Offsets;

    for (const DWARFYAML::ListEntries<EntryType> &List : Table.Lists) {
      Offsets.push_back(ListBufferOS.tell());
      if (List.Content) {
        List.Content->writeAsBinary(ListBufferOS, UINT64_MAX);
        Length += List.Content->binary_size();
      } else if (List.Entries) {
        for (const EntryType &Entry : *List.Entries) {
          Expected<uint64_t> EntrySize =
              writeListEntry(ListBufferOS, Entry, AddrSize, IsLittleEndian);
          if (!EntrySize)
            return EntrySize.takeError();
          Length += *EntrySize;
        }
      }
    }

    // If the offset_entry_count field isn't specified, yaml2obj will infer it
    // from the 'Offsets' field in the YAML description. If the 'Offsets' field
    // isn't specified either, yaml2obj will infer it from the auto-generated
    // offsets.
    uint32_t OffsetEntryCount;
    if (Table.OffsetEntryCount)
      OffsetEntryCount = *Table.OffsetEntryCount;
    else
      OffsetEntryCount = Table.Offsets ? Table.Offsets->size() : Offsets.size();
    uint64_t OffsetsSize =
        OffsetEntryCount * (Table.Format == dwarf::DWARF64 ? 8 : 4);
    Length += OffsetsSize;

    // If the length is specified in the YAML description, we use it instead of
    // the actual length.
    if (Table.Length)
      Length = *Table.Length;

    writeInitialLength(Table.Format, Length, OS, IsLittleEndian);
    writeInteger((uint16_t)Table.Version, OS, IsLittleEndian);
    writeInteger((uint8_t)AddrSize, OS, IsLittleEndian);
    writeInteger((uint8_t)Table.SegSelectorSize, OS, IsLittleEndian);
    writeInteger((uint32_t)OffsetEntryCount, OS, IsLittleEndian);

    auto EmitOffsets = [&](ArrayRef<uint64_t> Offsets, uint64_t OffsetsSize) {
      for (uint64_t Offset : Offsets)
        writeDWARFOffset(OffsetsSize + Offset, Table.Format, OS,
                         IsLittleEndian);
    };

    if (Table.Offsets)
      EmitOffsets(ArrayRef<uint64_t>((const uint64_t *)Table.Offsets->data(),
                                     Table.Offsets->size()),
                  0);
    else if (OffsetEntryCount != 0)
      EmitOffsets(Offsets, OffsetsSize);

    OS.write(ListBuffer.data(), ListBuffer.size());
  }

  return Error::success();
}

Error DWARFYAML::emitDebugRnglists(raw_ostream &OS, const Data &DI) {
  assert(DI.DebugRnglists && "unexpected emitDebugRnglists() call");
  return writeDWARFLists<DWARFYAML::RnglistEntry>(
      OS, *DI.DebugRnglists, DI.IsLittleEndian, DI.Is64BitAddrSize);
}

Error DWARFYAML::emitDebugLoclists(raw_ostream &OS, const Data &DI) {
  assert(DI.DebugLoclists && "unexpected emitDebugRnglists() call");
  return writeDWARFLists<DWARFYAML::LoclistEntry>(
      OS, *DI.DebugLoclists, DI.IsLittleEndian, DI.Is64BitAddrSize);
}

std::function<Error(raw_ostream &, const DWARFYAML::Data &)>
DWARFYAML::getDWARFEmitterByName(StringRef SecName) {
  auto EmitFunc =
      StringSwitch<
          std::function<Error(raw_ostream &, const DWARFYAML::Data &)>>(SecName)
          .Case("debug_abbrev", DWARFYAML::emitDebugAbbrev)
          .Case("debug_addr", DWARFYAML::emitDebugAddr)
          .Case("debug_aranges", DWARFYAML::emitDebugAranges)
          .Case("debug_gnu_pubnames", DWARFYAML::emitDebugGNUPubnames)
          .Case("debug_gnu_pubtypes", DWARFYAML::emitDebugGNUPubtypes)
          .Case("debug_info", DWARFYAML::emitDebugInfo)
          .Case("debug_line", DWARFYAML::emitDebugLine)
          .Case("debug_loclists", DWARFYAML::emitDebugLoclists)
          .Case("debug_pubnames", DWARFYAML::emitDebugPubnames)
          .Case("debug_pubtypes", DWARFYAML::emitDebugPubtypes)
          .Case("debug_ranges", DWARFYAML::emitDebugRanges)
          .Case("debug_rnglists", DWARFYAML::emitDebugRnglists)
          .Case("debug_str", DWARFYAML::emitDebugStr)
          .Case("debug_str_offsets", DWARFYAML::emitDebugStrOffsets)
          .Case("debug_names", DWARFYAML::emitDebugNames)
          .Default([&](raw_ostream &, const DWARFYAML::Data &) {
            return createStringError(errc::not_supported,
                                     SecName + " is not supported");
          });

  return EmitFunc;
}

static Error
emitDebugSectionImpl(const DWARFYAML::Data &DI, StringRef Sec,
                     StringMap<std::unique_ptr<MemoryBuffer>> &OutputBuffers) {
  std::string Data;
  raw_string_ostream DebugInfoStream(Data);

  auto EmitFunc = DWARFYAML::getDWARFEmitterByName(Sec);

  if (Error Err = EmitFunc(DebugInfoStream, DI))
    return Err;
  DebugInfoStream.flush();
  if (!Data.empty())
    OutputBuffers[Sec] = MemoryBuffer::getMemBufferCopy(Data);

  return Error::success();
}

Expected<StringMap<std::unique_ptr<MemoryBuffer>>>
DWARFYAML::emitDebugSections(StringRef YAMLString, bool IsLittleEndian,
                             bool Is64BitAddrSize) {
  auto CollectDiagnostic = [](const SMDiagnostic &Diag, void *DiagContext) {
    *static_cast<SMDiagnostic *>(DiagContext) = Diag;
  };

  SMDiagnostic GeneratedDiag;
  yaml::Input YIn(YAMLString, /*Ctxt=*/nullptr, CollectDiagnostic,
                  &GeneratedDiag);

  DWARFYAML::Data DI;
  DI.IsLittleEndian = IsLittleEndian;
  DI.Is64BitAddrSize = Is64BitAddrSize;

  YIn >> DI;
  if (YIn.error())
    return createStringError(YIn.error(), GeneratedDiag.getMessage());

  StringMap<std::unique_ptr<MemoryBuffer>> DebugSections;
  Error Err = Error::success();

  for (StringRef SecName : DI.getNonEmptySectionNames())
    Err = joinErrors(std::move(Err),
                     emitDebugSectionImpl(DI, SecName, DebugSections));

  if (Err)
    return std::move(Err);
  return std::move(DebugSections);
}
