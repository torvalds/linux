//===- yaml2xcoff - Convert YAML to a xcoff object file -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The xcoff component of yaml2obj.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

namespace {

constexpr unsigned DefaultSectionAlign = 4;
constexpr int16_t MaxSectionIndex = INT16_MAX;
constexpr uint32_t MaxRawDataSize = UINT32_MAX;

class XCOFFWriter {
public:
  XCOFFWriter(XCOFFYAML::Object &Obj, raw_ostream &OS, yaml::ErrorHandler EH)
      : Obj(Obj), W(OS, llvm::endianness::big), ErrHandler(EH),
        StrTblBuilder(StringTableBuilder::XCOFF) {
    Is64Bit = Obj.Header.Magic == (llvm::yaml::Hex16)XCOFF::XCOFF64;
  }
  bool writeXCOFF();

private:
  void reportOverwrite(uint64_t currentOffset, uint64_t specifiedOffset,
                       const Twine &fieldName);
  bool nameShouldBeInStringTable(StringRef SymbolName);
  bool initFileHeader(uint64_t CurrentOffset);
  void initAuxFileHeader();
  bool initSectionHeaders(uint64_t &CurrentOffset);
  bool initRelocations(uint64_t &CurrentOffset);
  bool initStringTable();
  bool assignAddressesAndIndices();

  void writeFileHeader();
  void writeAuxFileHeader();
  void writeSectionHeaders();
  bool writeSectionData();
  bool writeRelocations();
  bool writeSymbols();
  void writeStringTable();

  bool writeAuxSymbol(const XCOFFYAML::CsectAuxEnt &AuxSym);
  bool writeAuxSymbol(const XCOFFYAML::FileAuxEnt &AuxSym);
  bool writeAuxSymbol(const XCOFFYAML::FunctionAuxEnt &AuxSym);
  bool writeAuxSymbol(const XCOFFYAML::ExcpetionAuxEnt &AuxSym);
  bool writeAuxSymbol(const XCOFFYAML::BlockAuxEnt &AuxSym);
  bool writeAuxSymbol(const XCOFFYAML::SectAuxEntForDWARF &AuxSym);
  bool writeAuxSymbol(const XCOFFYAML::SectAuxEntForStat &AuxSym);
  bool writeAuxSymbol(const std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym);

  XCOFFYAML::Object &Obj;
  bool Is64Bit = false;
  support::endian::Writer W;
  yaml::ErrorHandler ErrHandler;
  StringTableBuilder StrTblBuilder;
  uint64_t StartOffset = 0u;
  // Map the section name to its corrresponding section index.
  DenseMap<StringRef, int16_t> SectionIndexMap = {
      {StringRef("N_DEBUG"), XCOFF::N_DEBUG},
      {StringRef("N_ABS"), XCOFF::N_ABS},
      {StringRef("N_UNDEF"), XCOFF::N_UNDEF}};
  XCOFFYAML::FileHeader InitFileHdr = Obj.Header;
  XCOFFYAML::AuxiliaryHeader InitAuxFileHdr;
  std::vector<XCOFFYAML::Section> InitSections = Obj.Sections;
};

static void writeName(StringRef StrName, support::endian::Writer W) {
  char Name[XCOFF::NameSize];
  memset(Name, 0, XCOFF::NameSize);
  char SrcName[] = "";
  memcpy(Name, StrName.size() ? StrName.data() : SrcName, StrName.size());
  ArrayRef<char> NameRef(Name, XCOFF::NameSize);
  W.write(NameRef);
}

void XCOFFWriter::reportOverwrite(uint64_t CurrentOffset,
                                  uint64_t specifiedOffset,
                                  const Twine &fieldName) {
  ErrHandler("current file offset (" + Twine(CurrentOffset) +
             ") is bigger than the specified " + fieldName + " (" +
             Twine(specifiedOffset) + ") ");
}

bool XCOFFWriter::nameShouldBeInStringTable(StringRef SymbolName) {
  // For XCOFF64: The symbol name is always in the string table.
  return (SymbolName.size() > XCOFF::NameSize) || Is64Bit;
}

bool XCOFFWriter::initRelocations(uint64_t &CurrentOffset) {
  for (XCOFFYAML::Section &InitSection : InitSections) {
    if (!InitSection.Relocations.empty()) {
      uint64_t RelSize = Is64Bit ? XCOFF::RelocationSerializationSize64
                                 : XCOFF::RelocationSerializationSize32;
      uint64_t UsedSize = RelSize * InitSection.Relocations.size();

      // If NumberOfRelocations was specified, we use it, even if it's
      // not consistent with the number of provided relocations.
      if (!InitSection.NumberOfRelocations)
        InitSection.NumberOfRelocations = InitSection.Relocations.size();

      // If the YAML file specified an offset to relocations, we use it.
      if (InitSection.FileOffsetToRelocations) {
        if (CurrentOffset > InitSection.FileOffsetToRelocations) {
          reportOverwrite(CurrentOffset, InitSection.FileOffsetToRelocations,
                          "FileOffsetToRelocations for the " +
                              InitSection.SectionName + " section");
          return false;
        }
        CurrentOffset = InitSection.FileOffsetToRelocations;
      } else
        InitSection.FileOffsetToRelocations = CurrentOffset;
      CurrentOffset += UsedSize;
      if (CurrentOffset > MaxRawDataSize) {
        ErrHandler("maximum object size (" + Twine(MaxRawDataSize) +
                   ") exceeded when writing relocation data for section " +
                   Twine(InitSection.SectionName));
        return false;
      }
    }
  }
  return true;
}

bool XCOFFWriter::initSectionHeaders(uint64_t &CurrentOffset) {
  uint64_t CurrentEndDataAddr = 0;
  uint64_t CurrentEndTDataAddr = 0;
  for (uint16_t I = 0, E = InitSections.size(); I < E; ++I) {
    // Assign indices for sections.
    if (InitSections[I].SectionName.size() &&
        !SectionIndexMap[InitSections[I].SectionName]) {
      // The section index starts from 1.
      SectionIndexMap[InitSections[I].SectionName] = I + 1;
      if ((I + 1) > MaxSectionIndex) {
        ErrHandler("exceeded the maximum permitted section index of " +
                   Twine(MaxSectionIndex));
        return false;
      }
    }

    if (!InitSections[I].Size)
      InitSections[I].Size = InitSections[I].SectionData.binary_size();

    // Section data addresses (physical/virtual) are related to symbol
    // addresses and alignments. Furthermore, it is possible to specify the
    // same starting addresses for the .text, .data, and .tdata sections.
    // Without examining all the symbols and their addreses and alignments,
    // it is not possible to compute valid section addresses. The only
    // condition required by XCOFF is that the .bss section immediately
    // follows the .data section, and the .tbss section immediately follows
    // the .tdata section. Therefore, we only assign addresses to the .bss
    // and .tbss sections if they do not already have non-zero addresses.
    // (If the YAML file is being used to generate a valid object file, we
    // expect all section addresses to be specified explicitly.)
    switch (InitSections[I].Flags) {
    case XCOFF::STYP_DATA:
      CurrentEndDataAddr = InitSections[I].Address + InitSections[I].Size;
      break;
    case XCOFF::STYP_BSS:
      if (!InitSections[I].Address)
        InitSections[I].Address = CurrentEndDataAddr;
      break;
    case XCOFF::STYP_TDATA:
      CurrentEndTDataAddr = InitSections[I].Address + InitSections[I].Size;
      break;
    case XCOFF::STYP_TBSS:
      if (!InitSections[I].Address)
        InitSections[I].Address = CurrentEndTDataAddr;
      break;
    }

    if (InitSections[I].SectionData.binary_size()) {
      if (InitSections[I].FileOffsetToData) {
        // Use the providedFileOffsetToData.
        if (CurrentOffset > InitSections[I].FileOffsetToData) {
          reportOverwrite(CurrentOffset, InitSections[I].FileOffsetToData,
                          "FileOffsetToData for the " +
                              InitSections[I].SectionName + " section");
          return false;
        }
        CurrentOffset = InitSections[I].FileOffsetToData;
      } else {
        CurrentOffset = alignTo(CurrentOffset, DefaultSectionAlign);
        InitSections[I].FileOffsetToData = CurrentOffset;
      }
      CurrentOffset += InitSections[I].SectionData.binary_size();
      if (CurrentOffset > MaxRawDataSize) {
        ErrHandler("maximum object size (" + Twine(MaxRawDataSize) +
                   ") exceeded when writing data for section " + Twine(I + 1) +
                   " (" + Twine(InitSections[I].SectionName) + ")");
        return false;
      }
    }
    if (InitSections[I].SectionSubtype) {
      uint32_t DWARFSubtype =
          static_cast<uint32_t>(*InitSections[I].SectionSubtype);
      if (InitSections[I].Flags != XCOFF::STYP_DWARF) {
        ErrHandler("a DWARFSectionSubtype is only allowed for a DWARF section");
        return false;
      }
      unsigned Mask = Is64Bit ? XCOFFSectionHeader64::SectionFlagsTypeMask
                              : XCOFFSectionHeader32::SectionFlagsTypeMask;
      if (DWARFSubtype & Mask) {
        ErrHandler("the low-order bits of DWARFSectionSubtype must be 0");
        return false;
      }
      InitSections[I].Flags |= DWARFSubtype;
    }
  }
  return initRelocations(CurrentOffset);
}

bool XCOFFWriter::initStringTable() {
  if (Obj.StrTbl.RawContent) {
    size_t RawSize = Obj.StrTbl.RawContent->binary_size();
    if (Obj.StrTbl.Strings || Obj.StrTbl.Length) {
      ErrHandler(
          "can't specify Strings or Length when RawContent is specified");
      return false;
    }
    if (Obj.StrTbl.ContentSize && *Obj.StrTbl.ContentSize < RawSize) {
      ErrHandler("specified ContentSize (" + Twine(*Obj.StrTbl.ContentSize) +
                 ") is less than the RawContent data size (" + Twine(RawSize) +
                 ")");
      return false;
    }
    return true;
  }
  if (Obj.StrTbl.ContentSize && *Obj.StrTbl.ContentSize <= 3) {
    ErrHandler("ContentSize shouldn't be less than 4 without RawContent");
    return false;
  }

  // Build the string table.
  StrTblBuilder.clear();

  if (Obj.StrTbl.Strings) {
    // Add all specified strings to the string table.
    for (StringRef StringEnt : *Obj.StrTbl.Strings)
      StrTblBuilder.add(StringEnt);

    size_t StrTblIdx = 0;
    size_t NumOfStrings = Obj.StrTbl.Strings->size();
    for (XCOFFYAML::Symbol &YamlSym : Obj.Symbols) {
      if (nameShouldBeInStringTable(YamlSym.SymbolName)) {
        if (StrTblIdx < NumOfStrings) {
          // Overwrite the symbol name with the specified string.
          YamlSym.SymbolName = (*Obj.StrTbl.Strings)[StrTblIdx];
          ++StrTblIdx;
        } else
          // Names that are not overwritten are still stored in the string
          // table.
          StrTblBuilder.add(YamlSym.SymbolName);
      }
    }
  } else {
    for (const XCOFFYAML::Symbol &YamlSym : Obj.Symbols) {
      if (nameShouldBeInStringTable(YamlSym.SymbolName))
        StrTblBuilder.add(YamlSym.SymbolName);
    }
  }

  // Check if the file name in the File Auxiliary Entry should be added to the
  // string table.
  for (const XCOFFYAML::Symbol &YamlSym : Obj.Symbols) {
    for (const std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym :
         YamlSym.AuxEntries) {
      if (auto AS = dyn_cast<XCOFFYAML::FileAuxEnt>(AuxSym.get()))
        if (nameShouldBeInStringTable(AS->FileNameOrString.value_or("")))
          StrTblBuilder.add(AS->FileNameOrString.value_or(""));
    }
  }

  StrTblBuilder.finalize();

  size_t StrTblSize = StrTblBuilder.getSize();
  if (Obj.StrTbl.ContentSize && *Obj.StrTbl.ContentSize < StrTblSize) {
    ErrHandler("specified ContentSize (" + Twine(*Obj.StrTbl.ContentSize) +
               ") is less than the size of the data that would otherwise be "
               "written (" +
               Twine(StrTblSize) + ")");
    return false;
  }

  return true;
}

bool XCOFFWriter::initFileHeader(uint64_t CurrentOffset) {
  // The default format of the object file is XCOFF32.
  InitFileHdr.Magic = XCOFF::XCOFF32;
  InitFileHdr.NumberOfSections = Obj.Sections.size();
  InitFileHdr.NumberOfSymTableEntries = Obj.Symbols.size();

  for (XCOFFYAML::Symbol &YamlSym : Obj.Symbols) {
    uint32_t AuxCount = YamlSym.AuxEntries.size();
    if (YamlSym.NumberOfAuxEntries && *YamlSym.NumberOfAuxEntries < AuxCount) {
      ErrHandler("specified NumberOfAuxEntries " +
                 Twine(static_cast<uint32_t>(*YamlSym.NumberOfAuxEntries)) +
                 " is less than the actual number "
                 "of auxiliary entries " +
                 Twine(AuxCount));
      return false;
    }
    YamlSym.NumberOfAuxEntries = YamlSym.NumberOfAuxEntries.value_or(AuxCount);
    // Add the number of auxiliary symbols to the total number.
    InitFileHdr.NumberOfSymTableEntries += *YamlSym.NumberOfAuxEntries;
  }

  // Calculate SymbolTableOffset for the file header.
  if (InitFileHdr.NumberOfSymTableEntries) {
    if (Obj.Header.SymbolTableOffset) {
      if (CurrentOffset > Obj.Header.SymbolTableOffset) {
        reportOverwrite(CurrentOffset, Obj.Header.SymbolTableOffset,
                        "SymbolTableOffset");
        return false;
      }
      CurrentOffset = Obj.Header.SymbolTableOffset;
    }
    InitFileHdr.SymbolTableOffset = CurrentOffset;
    CurrentOffset +=
        InitFileHdr.NumberOfSymTableEntries * XCOFF::SymbolTableEntrySize;
    if (CurrentOffset > MaxRawDataSize) {
      ErrHandler("maximum object size of " + Twine(MaxRawDataSize) +
                 " exceeded when writing symbols");
      return false;
    }
  }
  // TODO: Calculate FileOffsetToLineNumbers when line number supported.
  return true;
}

void XCOFFWriter::initAuxFileHeader() {
  if (Obj.AuxHeader)
    InitAuxFileHdr = *Obj.AuxHeader;
  // In general, an object file might contain multiple sections of a given type,
  // but in a loadable module, there must be exactly one .text, .data, .bss, and
  // .loader section. A loadable object might also have one .tdata section and
  // one .tbss section.
  // Set these section-related values if not set explicitly. We assume that the
  // input YAML matches the format of the loadable object, but if multiple input
  // sections still have the same type, the first section with that type
  // prevails.
  for (uint16_t I = 0, E = InitSections.size(); I < E; ++I) {
    switch (InitSections[I].Flags) {
    case XCOFF::STYP_TEXT:
      if (!InitAuxFileHdr.TextSize)
        InitAuxFileHdr.TextSize = InitSections[I].Size;
      if (!InitAuxFileHdr.TextStartAddr)
        InitAuxFileHdr.TextStartAddr = InitSections[I].Address;
      if (!InitAuxFileHdr.SecNumOfText)
        InitAuxFileHdr.SecNumOfText = I + 1;
      break;
    case XCOFF::STYP_DATA:
      if (!InitAuxFileHdr.InitDataSize)
        InitAuxFileHdr.InitDataSize = InitSections[I].Size;
      if (!InitAuxFileHdr.DataStartAddr)
        InitAuxFileHdr.DataStartAddr = InitSections[I].Address;
      if (!InitAuxFileHdr.SecNumOfData)
        InitAuxFileHdr.SecNumOfData = I + 1;
      break;
    case XCOFF::STYP_BSS:
      if (!InitAuxFileHdr.BssDataSize)
        InitAuxFileHdr.BssDataSize = InitSections[I].Size;
      if (!InitAuxFileHdr.SecNumOfBSS)
        InitAuxFileHdr.SecNumOfBSS = I + 1;
      break;
    case XCOFF::STYP_TDATA:
      if (!InitAuxFileHdr.SecNumOfTData)
        InitAuxFileHdr.SecNumOfTData = I + 1;
      break;
    case XCOFF::STYP_TBSS:
      if (!InitAuxFileHdr.SecNumOfTBSS)
        InitAuxFileHdr.SecNumOfTBSS = I + 1;
      break;
    case XCOFF::STYP_LOADER:
      if (!InitAuxFileHdr.SecNumOfLoader)
        InitAuxFileHdr.SecNumOfLoader = I + 1;
      break;
    default:
      break;
    }
  }
}

bool XCOFFWriter::assignAddressesAndIndices() {
  uint64_t FileHdrSize =
      Is64Bit ? XCOFF::FileHeaderSize64 : XCOFF::FileHeaderSize32;

  // If AuxHeaderSize is specified in the YAML file, we construct
  // an auxiliary header.
  uint64_t AuxFileHdrSize = 0;

  if (Obj.Header.AuxHeaderSize)
    AuxFileHdrSize = Obj.Header.AuxHeaderSize;
  else if (Obj.AuxHeader)
    AuxFileHdrSize =
        (Is64Bit ? XCOFF::AuxFileHeaderSize64 : XCOFF::AuxFileHeaderSize32);
  uint64_t SecHdrSize =
      Is64Bit ? XCOFF::SectionHeaderSize64 : XCOFF::SectionHeaderSize32;
  uint64_t CurrentOffset =
      FileHdrSize + AuxFileHdrSize + InitSections.size() * SecHdrSize;

  // Calculate section header info.
  if (!initSectionHeaders(CurrentOffset))
    return false;

  // Calculate file header info.
  if (!initFileHeader(CurrentOffset))
    return false;
  InitFileHdr.AuxHeaderSize = AuxFileHdrSize;

  // Initialize the auxiliary file header.
  if (AuxFileHdrSize)
    initAuxFileHeader();

  // Initialize the string table.
  return initStringTable();
}

void XCOFFWriter::writeFileHeader() {
  W.write<uint16_t>(Obj.Header.Magic ? Obj.Header.Magic : InitFileHdr.Magic);
  W.write<uint16_t>(Obj.Header.NumberOfSections ? Obj.Header.NumberOfSections
                                                : InitFileHdr.NumberOfSections);
  W.write<int32_t>(Obj.Header.TimeStamp);
  if (Is64Bit) {
    W.write<uint64_t>(InitFileHdr.SymbolTableOffset);
    W.write<uint16_t>(InitFileHdr.AuxHeaderSize);
    W.write<uint16_t>(Obj.Header.Flags);
    W.write<int32_t>(Obj.Header.NumberOfSymTableEntries
                         ? Obj.Header.NumberOfSymTableEntries
                         : InitFileHdr.NumberOfSymTableEntries);
  } else {
    W.write<uint32_t>(InitFileHdr.SymbolTableOffset);
    W.write<int32_t>(Obj.Header.NumberOfSymTableEntries
                         ? Obj.Header.NumberOfSymTableEntries
                         : InitFileHdr.NumberOfSymTableEntries);
    W.write<uint16_t>(InitFileHdr.AuxHeaderSize);
    W.write<uint16_t>(Obj.Header.Flags);
  }
}

void XCOFFWriter::writeAuxFileHeader() {
  W.write<uint16_t>(InitAuxFileHdr.Magic.value_or(yaml::Hex16(1)));
  W.write<uint16_t>(InitAuxFileHdr.Version.value_or(yaml::Hex16(1)));
  if (Is64Bit) {
    W.OS.write_zeros(4); // Reserved for debugger.
    W.write<uint64_t>(InitAuxFileHdr.TextStartAddr.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.DataStartAddr.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.TOCAnchorAddr.value_or(yaml::Hex64(0)));
  } else {
    W.write<uint32_t>(InitAuxFileHdr.TextSize.value_or(yaml::Hex64(0)));
    W.write<uint32_t>(InitAuxFileHdr.InitDataSize.value_or(yaml::Hex64(0)));
    W.write<uint32_t>(InitAuxFileHdr.BssDataSize.value_or(yaml::Hex64(0)));
    W.write<uint32_t>(InitAuxFileHdr.EntryPointAddr.value_or(yaml::Hex64(0)));
    W.write<uint32_t>(InitAuxFileHdr.TextStartAddr.value_or(yaml::Hex64(0)));
    W.write<uint32_t>(InitAuxFileHdr.DataStartAddr.value_or(yaml::Hex64(0)));
    // A short 32-bit auxiliary header ends here.
    if (InitFileHdr.AuxHeaderSize == XCOFF::AuxFileHeaderSizeShort)
      return;
    W.write<uint32_t>(InitAuxFileHdr.TOCAnchorAddr.value_or(yaml::Hex64(0)));
  }
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfEntryPoint.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfText.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfData.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfTOC.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfLoader.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfBSS.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.MaxAlignOfText.value_or(yaml::Hex16(0)));
  W.write<uint16_t>(InitAuxFileHdr.MaxAlignOfData.value_or(yaml::Hex16(0)));
  W.write<uint16_t>(InitAuxFileHdr.ModuleType.value_or(yaml::Hex16(0)));
  W.write<uint8_t>(InitAuxFileHdr.CpuFlag.value_or(yaml::Hex8(0)));
  W.write<uint8_t>(0); // Reserved for CPU type.
  if (Is64Bit) {
    W.write<uint8_t>(InitAuxFileHdr.TextPageSize.value_or(yaml::Hex8(0)));
    W.write<uint8_t>(InitAuxFileHdr.DataPageSize.value_or(yaml::Hex8(0)));
    W.write<uint8_t>(InitAuxFileHdr.StackPageSize.value_or(yaml::Hex8(0)));
    W.write<uint8_t>(
        InitAuxFileHdr.FlagAndTDataAlignment.value_or(yaml::Hex8(0x80)));
    W.write<uint64_t>(InitAuxFileHdr.TextSize.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.InitDataSize.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.BssDataSize.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.EntryPointAddr.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.MaxStackSize.value_or(yaml::Hex64(0)));
    W.write<uint64_t>(InitAuxFileHdr.MaxDataSize.value_or(yaml::Hex64(0)));
  } else {
    W.write<uint32_t>(InitAuxFileHdr.MaxStackSize.value_or(yaml::Hex64(0)));
    W.write<uint32_t>(InitAuxFileHdr.MaxDataSize.value_or(yaml::Hex64(0)));
    W.OS.write_zeros(4); // Reserved for debugger.
    W.write<uint8_t>(InitAuxFileHdr.TextPageSize.value_or(yaml::Hex8(0)));
    W.write<uint8_t>(InitAuxFileHdr.DataPageSize.value_or(yaml::Hex8(0)));
    W.write<uint8_t>(InitAuxFileHdr.StackPageSize.value_or(yaml::Hex8(0)));
    W.write<uint8_t>(
        InitAuxFileHdr.FlagAndTDataAlignment.value_or(yaml::Hex8(0)));
  }
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfTData.value_or(0));
  W.write<uint16_t>(InitAuxFileHdr.SecNumOfTBSS.value_or(0));
  if (Is64Bit) {
    W.write<uint16_t>(
        InitAuxFileHdr.Flag.value_or(yaml::Hex16(XCOFF::SHR_SYMTAB)));
    if (InitFileHdr.AuxHeaderSize > XCOFF::AuxFileHeaderSize64)
      W.OS.write_zeros(InitFileHdr.AuxHeaderSize - XCOFF::AuxFileHeaderSize64);
  } else {
    if (InitFileHdr.AuxHeaderSize > XCOFF::AuxFileHeaderSize32)
      W.OS.write_zeros(InitFileHdr.AuxHeaderSize - XCOFF::AuxFileHeaderSize32);
  }
}

void XCOFFWriter::writeSectionHeaders() {
  for (uint16_t I = 0, E = Obj.Sections.size(); I < E; ++I) {
    XCOFFYAML::Section DerivedSec = InitSections[I];
    writeName(DerivedSec.SectionName, W);
    if (Is64Bit) {
      // Virtual address is the same as physical address.
      W.write<uint64_t>(DerivedSec.Address); // Physical address
      W.write<uint64_t>(DerivedSec.Address); // Virtual address
      W.write<uint64_t>(DerivedSec.Size);
      W.write<uint64_t>(DerivedSec.FileOffsetToData);
      W.write<uint64_t>(DerivedSec.FileOffsetToRelocations);
      W.write<uint64_t>(DerivedSec.FileOffsetToLineNumbers);
      W.write<uint32_t>(DerivedSec.NumberOfRelocations);
      W.write<uint32_t>(DerivedSec.NumberOfLineNumbers);
      W.write<int32_t>(DerivedSec.Flags);
      W.OS.write_zeros(4);
    } else {
      // Virtual address is the same as physical address.
      W.write<uint32_t>(DerivedSec.Address); // Physical address
      W.write<uint32_t>(DerivedSec.Address); // Virtual address
      W.write<uint32_t>(DerivedSec.Size);
      W.write<uint32_t>(DerivedSec.FileOffsetToData);
      W.write<uint32_t>(DerivedSec.FileOffsetToRelocations);
      W.write<uint32_t>(DerivedSec.FileOffsetToLineNumbers);
      W.write<uint16_t>(DerivedSec.NumberOfRelocations);
      W.write<uint16_t>(DerivedSec.NumberOfLineNumbers);
      W.write<int32_t>(DerivedSec.Flags);
    }
  }
}

bool XCOFFWriter::writeSectionData() {
  for (uint16_t I = 0, E = Obj.Sections.size(); I < E; ++I) {
    XCOFFYAML::Section YamlSec = Obj.Sections[I];
    if (YamlSec.SectionData.binary_size()) {
      // Fill the padding size with zeros.
      int64_t PaddingSize = (uint64_t)InitSections[I].FileOffsetToData -
                            (W.OS.tell() - StartOffset);
      if (PaddingSize < 0) {
        ErrHandler("redundant data was written before section data");
        return false;
      }
      W.OS.write_zeros(PaddingSize);
      YamlSec.SectionData.writeAsBinary(W.OS);
    }
  }
  return true;
}

bool XCOFFWriter::writeRelocations() {
  for (uint16_t I = 0, E = Obj.Sections.size(); I < E; ++I) {
    XCOFFYAML::Section YamlSec = Obj.Sections[I];
    if (!YamlSec.Relocations.empty()) {
      int64_t PaddingSize =
          InitSections[I].FileOffsetToRelocations - (W.OS.tell() - StartOffset);
      if (PaddingSize < 0) {
        ErrHandler("redundant data was written before relocations");
        return false;
      }
      W.OS.write_zeros(PaddingSize);
      for (const XCOFFYAML::Relocation &YamlRel : YamlSec.Relocations) {
        if (Is64Bit)
          W.write<uint64_t>(YamlRel.VirtualAddress);
        else
          W.write<uint32_t>(YamlRel.VirtualAddress);
        W.write<uint32_t>(YamlRel.SymbolIndex);
        W.write<uint8_t>(YamlRel.Info);
        W.write<uint8_t>(YamlRel.Type);
      }
    }
  }
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::CsectAuxEnt &AuxSym) {
  uint8_t SymAlignAndType = 0;
  if (AuxSym.SymbolAlignmentAndType) {
    if (AuxSym.SymbolType || AuxSym.SymbolAlignment) {
      ErrHandler("cannot specify SymbolType or SymbolAlignment if "
                 "SymbolAlignmentAndType is specified");
      return false;
    }
    SymAlignAndType = *AuxSym.SymbolAlignmentAndType;
  } else {
    if (AuxSym.SymbolType) {
      uint8_t SymbolType = *AuxSym.SymbolType;
      if (SymbolType & ~XCOFFCsectAuxRef::SymbolTypeMask) {
        ErrHandler("symbol type must be less than " +
                   Twine(1 + XCOFFCsectAuxRef::SymbolTypeMask));
        return false;
      }
      SymAlignAndType = SymbolType;
    }
    if (AuxSym.SymbolAlignment) {
      const uint8_t ShiftedSymbolAlignmentMask =
          XCOFFCsectAuxRef::SymbolAlignmentMask >>
          XCOFFCsectAuxRef::SymbolAlignmentBitOffset;

      if (*AuxSym.SymbolAlignment & ~ShiftedSymbolAlignmentMask) {
        ErrHandler("symbol alignment must be less than " +
                   Twine(1 + ShiftedSymbolAlignmentMask));
        return false;
      }
      SymAlignAndType |= (*AuxSym.SymbolAlignment
                          << XCOFFCsectAuxRef::SymbolAlignmentBitOffset);
    }
  }
  if (Is64Bit) {
    W.write<uint32_t>(AuxSym.SectionOrLengthLo.value_or(0));
    W.write<uint32_t>(AuxSym.ParameterHashIndex.value_or(0));
    W.write<uint16_t>(AuxSym.TypeChkSectNum.value_or(0));
    W.write<uint8_t>(SymAlignAndType);
    W.write<uint8_t>(AuxSym.StorageMappingClass.value_or(XCOFF::XMC_PR));
    W.write<uint32_t>(AuxSym.SectionOrLengthHi.value_or(0));
    W.write<uint8_t>(0);
    W.write<uint8_t>(XCOFF::AUX_CSECT);
  } else {
    W.write<uint32_t>(AuxSym.SectionOrLength.value_or(0));
    W.write<uint32_t>(AuxSym.ParameterHashIndex.value_or(0));
    W.write<uint16_t>(AuxSym.TypeChkSectNum.value_or(0));
    W.write<uint8_t>(SymAlignAndType);
    W.write<uint8_t>(AuxSym.StorageMappingClass.value_or(XCOFF::XMC_PR));
    W.write<uint32_t>(AuxSym.StabInfoIndex.value_or(0));
    W.write<uint16_t>(AuxSym.StabSectNum.value_or(0));
  }
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::ExcpetionAuxEnt &AuxSym) {
  assert(Is64Bit && "can't write the exception auxiliary symbol for XCOFF32");
  W.write<uint64_t>(AuxSym.OffsetToExceptionTbl.value_or(0));
  W.write<uint32_t>(AuxSym.SizeOfFunction.value_or(0));
  W.write<uint32_t>(AuxSym.SymIdxOfNextBeyond.value_or(0));
  W.write<uint8_t>(0);
  W.write<uint8_t>(XCOFF::AUX_EXCEPT);
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::FunctionAuxEnt &AuxSym) {
  if (Is64Bit) {
    W.write<uint64_t>(AuxSym.PtrToLineNum.value_or(0));
    W.write<uint32_t>(AuxSym.SizeOfFunction.value_or(0));
    W.write<uint32_t>(AuxSym.SymIdxOfNextBeyond.value_or(0));
    W.write<uint8_t>(0);
    W.write<uint8_t>(XCOFF::AUX_FCN);
  } else {
    W.write<uint32_t>(AuxSym.OffsetToExceptionTbl.value_or(0));
    W.write<uint32_t>(AuxSym.SizeOfFunction.value_or(0));
    W.write<uint32_t>(AuxSym.PtrToLineNum.value_or(0));
    W.write<uint32_t>(AuxSym.SymIdxOfNextBeyond.value_or(0));
    W.OS.write_zeros(2);
  }
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::FileAuxEnt &AuxSym) {
  StringRef FileName = AuxSym.FileNameOrString.value_or("");
  if (nameShouldBeInStringTable(FileName)) {
    W.write<int32_t>(0);
    W.write<uint32_t>(StrTblBuilder.getOffset(FileName));
  } else {
    writeName(FileName, W);
  }
  W.OS.write_zeros(XCOFF::FileNamePadSize);
  W.write<uint8_t>(AuxSym.FileStringType.value_or(XCOFF::XFT_FN));
  if (Is64Bit) {
    W.OS.write_zeros(2);
    W.write<uint8_t>(XCOFF::AUX_FILE);
  } else {
    W.OS.write_zeros(3);
  }
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::BlockAuxEnt &AuxSym) {
  if (Is64Bit) {
    W.write<uint32_t>(AuxSym.LineNum.value_or(0));
    W.OS.write_zeros(13);
    W.write<uint8_t>(XCOFF::AUX_SYM);
  } else {
    W.OS.write_zeros(2);
    W.write<uint16_t>(AuxSym.LineNumHi.value_or(0));
    W.write<uint16_t>(AuxSym.LineNumLo.value_or(0));
    W.OS.write_zeros(12);
  }
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::SectAuxEntForDWARF &AuxSym) {
  if (Is64Bit) {
    W.write<uint64_t>(AuxSym.LengthOfSectionPortion.value_or(0));
    W.write<uint64_t>(AuxSym.NumberOfRelocEnt.value_or(0));
    W.write<uint8_t>(0);
    W.write<uint8_t>(XCOFF::AUX_SECT);
  } else {
    W.write<uint32_t>(AuxSym.LengthOfSectionPortion.value_or(0));
    W.OS.write_zeros(4);
    W.write<uint32_t>(AuxSym.NumberOfRelocEnt.value_or(0));
    W.OS.write_zeros(6);
  }
  return true;
}

bool XCOFFWriter::writeAuxSymbol(const XCOFFYAML::SectAuxEntForStat &AuxSym) {
  assert(!Is64Bit && "can't write the stat auxiliary symbol for XCOFF64");
  W.write<uint32_t>(AuxSym.SectionLength.value_or(0));
  W.write<uint16_t>(AuxSym.NumberOfRelocEnt.value_or(0));
  W.write<uint16_t>(AuxSym.NumberOfLineNum.value_or(0));
  W.OS.write_zeros(10);
  return true;
}

bool XCOFFWriter::writeAuxSymbol(
    const std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym) {
  if (auto AS = dyn_cast<XCOFFYAML::CsectAuxEnt>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  else if (auto AS = dyn_cast<XCOFFYAML::FunctionAuxEnt>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  else if (auto AS = dyn_cast<XCOFFYAML::ExcpetionAuxEnt>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  else if (auto AS = dyn_cast<XCOFFYAML::FileAuxEnt>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  else if (auto AS = dyn_cast<XCOFFYAML::BlockAuxEnt>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  else if (auto AS = dyn_cast<XCOFFYAML::SectAuxEntForDWARF>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  else if (auto AS = dyn_cast<XCOFFYAML::SectAuxEntForStat>(AuxSym.get()))
    return writeAuxSymbol(*AS);
  llvm_unreachable("unknown auxiliary symbol type");
  return false;
}

bool XCOFFWriter::writeSymbols() {
  int64_t PaddingSize =
      InitFileHdr.SymbolTableOffset - (W.OS.tell() - StartOffset);
  if (PaddingSize < 0) {
    ErrHandler("redundant data was written before symbols");
    return false;
  }
  W.OS.write_zeros(PaddingSize);
  for (const XCOFFYAML::Symbol &YamlSym : Obj.Symbols) {
    if (Is64Bit) {
      W.write<uint64_t>(YamlSym.Value);
      W.write<uint32_t>(StrTblBuilder.getOffset(YamlSym.SymbolName));
    } else {
      if (nameShouldBeInStringTable(YamlSym.SymbolName)) {
        // For XCOFF32: A value of 0 indicates that the symbol name is in the
        // string table.
        W.write<int32_t>(0);
        W.write<uint32_t>(StrTblBuilder.getOffset(YamlSym.SymbolName));
      } else {
        writeName(YamlSym.SymbolName, W);
      }
      W.write<uint32_t>(YamlSym.Value);
    }
    if (YamlSym.SectionName) {
      if (!SectionIndexMap.count(*YamlSym.SectionName)) {
        ErrHandler("the SectionName " + *YamlSym.SectionName +
                   " specified in the symbol does not exist");
        return false;
      }
      if (YamlSym.SectionIndex &&
          SectionIndexMap[*YamlSym.SectionName] != *YamlSym.SectionIndex) {
        ErrHandler("the SectionName " + *YamlSym.SectionName +
                   " and the SectionIndex (" + Twine(*YamlSym.SectionIndex) +
                   ") refer to different sections");
        return false;
      }
      W.write<int16_t>(SectionIndexMap[*YamlSym.SectionName]);
    } else {
      W.write<int16_t>(YamlSym.SectionIndex ? *YamlSym.SectionIndex : 0);
    }
    W.write<uint16_t>(YamlSym.Type);
    W.write<uint8_t>(YamlSym.StorageClass);

    uint8_t NumOfAuxSym = YamlSym.NumberOfAuxEntries.value_or(0);
    W.write<uint8_t>(NumOfAuxSym);

    if (!NumOfAuxSym && !YamlSym.AuxEntries.size())
      continue;

    // Now write auxiliary entries.
    if (!YamlSym.AuxEntries.size()) {
      W.OS.write_zeros(XCOFF::SymbolTableEntrySize * NumOfAuxSym);
    } else {
      for (const std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym :
           YamlSym.AuxEntries) {
        if (!writeAuxSymbol(AuxSym))
          return false;
      }
      // Pad with zeros.
      if (NumOfAuxSym > YamlSym.AuxEntries.size())
        W.OS.write_zeros(XCOFF::SymbolTableEntrySize *
                         (NumOfAuxSym - YamlSym.AuxEntries.size()));
    }
  }
  return true;
}

void XCOFFWriter::writeStringTable() {
  if (Obj.StrTbl.RawContent) {
    Obj.StrTbl.RawContent->writeAsBinary(W.OS);
    if (Obj.StrTbl.ContentSize) {
      assert(*Obj.StrTbl.ContentSize >= Obj.StrTbl.RawContent->binary_size() &&
             "Specified ContentSize is less than the RawContent size.");
      W.OS.write_zeros(*Obj.StrTbl.ContentSize -
                       Obj.StrTbl.RawContent->binary_size());
    }
    return;
  }

  size_t StrTblBuilderSize = StrTblBuilder.getSize();
  // If neither Length nor ContentSize is specified, write the StrTblBuilder
  // directly, which contains the auto-generated Length value.
  if (!Obj.StrTbl.Length && !Obj.StrTbl.ContentSize) {
    if (StrTblBuilderSize <= 4)
      return;
    StrTblBuilder.write(W.OS);
    return;
  }

  // Serialize the string table's content to a temporary buffer.
  std::unique_ptr<WritableMemoryBuffer> Buf =
      WritableMemoryBuffer::getNewMemBuffer(StrTblBuilderSize);
  uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart());
  StrTblBuilder.write(Ptr);
  // Replace the first 4 bytes, which contain the auto-generated Length value,
  // with the specified value.
  memset(Ptr, 0, 4);
  support::endian::write32be(Ptr, Obj.StrTbl.Length ? *Obj.StrTbl.Length
                                                    : *Obj.StrTbl.ContentSize);
  // Copy the buffer content to the actual output stream.
  W.OS.write(Buf->getBufferStart(), Buf->getBufferSize());
  // Add zeros as padding after strings.
  if (Obj.StrTbl.ContentSize) {
    assert(*Obj.StrTbl.ContentSize >= StrTblBuilderSize &&
           "Specified ContentSize is less than the StringTableBuilder size.");
    W.OS.write_zeros(*Obj.StrTbl.ContentSize - StrTblBuilderSize);
  }
}

bool XCOFFWriter::writeXCOFF() {
  if (!assignAddressesAndIndices())
    return false;
  StartOffset = W.OS.tell();
  writeFileHeader();
  if (InitFileHdr.AuxHeaderSize)
    writeAuxFileHeader();
  if (!Obj.Sections.empty()) {
    writeSectionHeaders();
    if (!writeSectionData())
      return false;
    if (!writeRelocations())
      return false;
  }
  if (!Obj.Symbols.empty() && !writeSymbols())
    return false;
  writeStringTable();
  return true;
}

} // end anonymous namespace

namespace llvm {
namespace yaml {

bool yaml2xcoff(XCOFFYAML::Object &Doc, raw_ostream &Out, ErrorHandler EH) {
  XCOFFWriter Writer(Doc, Out, EH);
  return Writer.writeXCOFF();
}

} // namespace yaml
} // namespace llvm
