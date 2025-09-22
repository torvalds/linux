//===- DWARFYAML.h - DWARF YAMLIO implementation ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of DWARF Debug Info.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DWARFYAML_H
#define LLVM_OBJECTYAML_DWARFYAML_H

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace llvm {
namespace DWARFYAML {

struct AttributeAbbrev {
  llvm::dwarf::Attribute Attribute;
  llvm::dwarf::Form Form;
  llvm::yaml::Hex64 Value; // Some DWARF5 attributes have values
};

struct Abbrev {
  std::optional<yaml::Hex64> Code;
  llvm::dwarf::Tag Tag;
  llvm::dwarf::Constants Children;
  std::vector<AttributeAbbrev> Attributes;
};

struct AbbrevTable {
  std::optional<uint64_t> ID;
  std::vector<Abbrev> Table;
};

struct ARangeDescriptor {
  llvm::yaml::Hex64 Address;
  yaml::Hex64 Length;
};

struct ARange {
  dwarf::DwarfFormat Format;
  std::optional<yaml::Hex64> Length;
  uint16_t Version;
  yaml::Hex64 CuOffset;
  std::optional<yaml::Hex8> AddrSize;
  yaml::Hex8 SegSize;
  std::vector<ARangeDescriptor> Descriptors;
};

/// Class that describes a range list entry, or a base address selection entry
/// within a range list in the .debug_ranges section.
struct RangeEntry {
  llvm::yaml::Hex64 LowOffset;
  llvm::yaml::Hex64 HighOffset;
};

/// Class that describes a single range list inside the .debug_ranges section.
struct Ranges {
  std::optional<llvm::yaml::Hex64> Offset;
  std::optional<llvm::yaml::Hex8> AddrSize;
  std::vector<RangeEntry> Entries;
};

struct PubEntry {
  llvm::yaml::Hex32 DieOffset;
  llvm::yaml::Hex8 Descriptor;
  StringRef Name;
};

struct PubSection {
  dwarf::DwarfFormat Format;
  yaml::Hex64 Length;
  uint16_t Version;
  uint32_t UnitOffset;
  uint32_t UnitSize;
  std::vector<PubEntry> Entries;
};

struct FormValue {
  llvm::yaml::Hex64 Value;
  StringRef CStr;
  std::vector<llvm::yaml::Hex8> BlockData;
};

struct Entry {
  llvm::yaml::Hex32 AbbrCode;
  std::vector<FormValue> Values;
};

/// Class that contains helpful context information when mapping YAML into DWARF
/// data structures.
struct DWARFContext {
  bool IsGNUPubSec = false;
};

struct Unit {
  dwarf::DwarfFormat Format;
  std::optional<yaml::Hex64> Length;
  uint16_t Version;
  std::optional<uint8_t> AddrSize;
  llvm::dwarf::UnitType Type; // Added in DWARF 5
  std::optional<uint64_t> AbbrevTableID;
  std::optional<yaml::Hex64> AbbrOffset;
  std::vector<Entry> Entries;
};

struct IdxForm {
  dwarf::Index Idx;
  dwarf::Form Form;
};

struct DebugNameAbbreviation {
  yaml::Hex64 Code;
  dwarf::Tag Tag;
  std::vector<IdxForm> Indices;
};

struct DebugNameEntry {
  yaml::Hex32 NameStrp;
  yaml::Hex64 Code;
  std::vector<yaml::Hex64> Values;
};

struct DebugNamesSection {
  std::vector<DebugNameAbbreviation> Abbrevs;
  std::vector<DebugNameEntry> Entries;
};

struct File {
  StringRef Name;
  uint64_t DirIdx;
  uint64_t ModTime;
  uint64_t Length;
};

struct LineTableOpcode {
  dwarf::LineNumberOps Opcode;
  std::optional<uint64_t> ExtLen;
  dwarf::LineNumberExtendedOps SubOpcode;
  uint64_t Data;
  int64_t SData;
  File FileEntry;
  std::vector<llvm::yaml::Hex8> UnknownOpcodeData;
  std::vector<llvm::yaml::Hex64> StandardOpcodeData;
};

struct LineTable {
  dwarf::DwarfFormat Format;
  std::optional<uint64_t> Length;
  uint16_t Version;
  std::optional<uint64_t> PrologueLength;
  uint8_t MinInstLength;
  uint8_t MaxOpsPerInst;
  uint8_t DefaultIsStmt;
  uint8_t LineBase;
  uint8_t LineRange;
  std::optional<uint8_t> OpcodeBase;
  std::optional<std::vector<uint8_t>> StandardOpcodeLengths;
  std::vector<StringRef> IncludeDirs;
  std::vector<File> Files;
  std::vector<LineTableOpcode> Opcodes;
};

struct SegAddrPair {
  yaml::Hex64 Segment;
  yaml::Hex64 Address;
};

struct AddrTableEntry {
  dwarf::DwarfFormat Format;
  std::optional<yaml::Hex64> Length;
  yaml::Hex16 Version;
  std::optional<yaml::Hex8> AddrSize;
  yaml::Hex8 SegSelectorSize;
  std::vector<SegAddrPair> SegAddrPairs;
};

struct StringOffsetsTable {
  dwarf::DwarfFormat Format;
  std::optional<yaml::Hex64> Length;
  yaml::Hex16 Version;
  yaml::Hex16 Padding;
  std::vector<yaml::Hex64> Offsets;
};

struct DWARFOperation {
  dwarf::LocationAtom Operator;
  std::vector<yaml::Hex64> Values;
};

struct RnglistEntry {
  dwarf::RnglistEntries Operator;
  std::vector<yaml::Hex64> Values;
};

struct LoclistEntry {
  dwarf::LoclistEntries Operator;
  std::vector<yaml::Hex64> Values;
  std::optional<yaml::Hex64> DescriptionsLength;
  std::vector<DWARFOperation> Descriptions;
};

template <typename EntryType> struct ListEntries {
  std::optional<std::vector<EntryType>> Entries;
  std::optional<yaml::BinaryRef> Content;
};

template <typename EntryType> struct ListTable {
  dwarf::DwarfFormat Format;
  std::optional<yaml::Hex64> Length;
  yaml::Hex16 Version;
  std::optional<yaml::Hex8> AddrSize;
  yaml::Hex8 SegSelectorSize;
  std::optional<uint32_t> OffsetEntryCount;
  std::optional<std::vector<yaml::Hex64>> Offsets;
  std::vector<ListEntries<EntryType>> Lists;
};

struct Data {
  bool IsLittleEndian;
  bool Is64BitAddrSize;
  std::vector<AbbrevTable> DebugAbbrev;
  std::optional<std::vector<StringRef>> DebugStrings;
  std::optional<std::vector<StringOffsetsTable>> DebugStrOffsets;
  std::optional<std::vector<ARange>> DebugAranges;
  std::optional<std::vector<Ranges>> DebugRanges;
  std::optional<std::vector<AddrTableEntry>> DebugAddr;
  std::optional<PubSection> PubNames;
  std::optional<PubSection> PubTypes;

  std::optional<PubSection> GNUPubNames;
  std::optional<PubSection> GNUPubTypes;

  std::vector<Unit> CompileUnits;

  std::vector<LineTable> DebugLines;
  std::optional<std::vector<ListTable<RnglistEntry>>> DebugRnglists;
  std::optional<std::vector<ListTable<LoclistEntry>>> DebugLoclists;
  std::optional<DebugNamesSection> DebugNames;

  bool isEmpty() const;

  SetVector<StringRef> getNonEmptySectionNames() const;

  struct AbbrevTableInfo {
    uint64_t Index;
    uint64_t Offset;
  };
  Expected<AbbrevTableInfo> getAbbrevTableInfoByID(uint64_t ID) const;
  StringRef getAbbrevTableContentByIndex(uint64_t Index) const;

private:
  mutable std::unordered_map<uint64_t, AbbrevTableInfo> AbbrevTableInfoMap;
  mutable std::unordered_map<uint64_t, std::string> AbbrevTableContents;
};

} // end namespace DWARFYAML
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::AttributeAbbrev)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::Abbrev)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::AbbrevTable)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::ARangeDescriptor)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::ARange)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::RangeEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::Ranges)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::PubEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::Unit)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::FormValue)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::Entry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::File)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::LineTable)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::LineTableOpcode)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::SegAddrPair)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::AddrTableEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::StringOffsetsTable)
LLVM_YAML_IS_SEQUENCE_VECTOR(
    llvm::DWARFYAML::ListTable<DWARFYAML::RnglistEntry>)
LLVM_YAML_IS_SEQUENCE_VECTOR(
    llvm::DWARFYAML::ListEntries<DWARFYAML::RnglistEntry>)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::RnglistEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(
    llvm::DWARFYAML::ListTable<DWARFYAML::LoclistEntry>)
LLVM_YAML_IS_SEQUENCE_VECTOR(
    llvm::DWARFYAML::ListEntries<DWARFYAML::LoclistEntry>)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::LoclistEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::DWARFOperation)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::DebugNameEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::DebugNameAbbreviation)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DWARFYAML::IdxForm)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<DWARFYAML::Data> {
  static void mapping(IO &IO, DWARFYAML::Data &DWARF);
};

template <> struct MappingTraits<DWARFYAML::AbbrevTable> {
  static void mapping(IO &IO, DWARFYAML::AbbrevTable &AbbrevTable);
};

template <> struct MappingTraits<DWARFYAML::Abbrev> {
  static void mapping(IO &IO, DWARFYAML::Abbrev &Abbrev);
};

template <> struct MappingTraits<DWARFYAML::AttributeAbbrev> {
  static void mapping(IO &IO, DWARFYAML::AttributeAbbrev &AttAbbrev);
};

template <> struct MappingTraits<DWARFYAML::ARangeDescriptor> {
  static void mapping(IO &IO, DWARFYAML::ARangeDescriptor &Descriptor);
};

template <> struct MappingTraits<DWARFYAML::ARange> {
  static void mapping(IO &IO, DWARFYAML::ARange &ARange);
};

template <> struct MappingTraits<DWARFYAML::RangeEntry> {
  static void mapping(IO &IO, DWARFYAML::RangeEntry &Entry);
};

template <> struct MappingTraits<DWARFYAML::Ranges> {
  static void mapping(IO &IO, DWARFYAML::Ranges &Ranges);
};

template <> struct MappingTraits<DWARFYAML::PubEntry> {
  static void mapping(IO &IO, DWARFYAML::PubEntry &Entry);
};

template <> struct MappingTraits<DWARFYAML::PubSection> {
  static void mapping(IO &IO, DWARFYAML::PubSection &Section);
};

template <> struct MappingTraits<DWARFYAML::Unit> {
  static void mapping(IO &IO, DWARFYAML::Unit &Unit);
};

template <> struct MappingTraits<DWARFYAML::DebugNamesSection> {
  static void mapping(IO &IO, DWARFYAML::DebugNamesSection &);
};
template <> struct MappingTraits<DWARFYAML::DebugNameEntry> {
  static void mapping(IO &IO, DWARFYAML::DebugNameEntry &);
};
template <> struct MappingTraits<DWARFYAML::DebugNameAbbreviation> {
  static void mapping(IO &IO, DWARFYAML::DebugNameAbbreviation &);
};
template <> struct MappingTraits<DWARFYAML::IdxForm> {
  static void mapping(IO &IO, DWARFYAML::IdxForm &);
};

template <> struct MappingTraits<DWARFYAML::Entry> {
  static void mapping(IO &IO, DWARFYAML::Entry &Entry);
};

template <> struct MappingTraits<DWARFYAML::FormValue> {
  static void mapping(IO &IO, DWARFYAML::FormValue &FormValue);
};

template <> struct MappingTraits<DWARFYAML::File> {
  static void mapping(IO &IO, DWARFYAML::File &File);
};

template <> struct MappingTraits<DWARFYAML::LineTableOpcode> {
  static void mapping(IO &IO, DWARFYAML::LineTableOpcode &LineTableOpcode);
};

template <> struct MappingTraits<DWARFYAML::LineTable> {
  static void mapping(IO &IO, DWARFYAML::LineTable &LineTable);
};

template <> struct MappingTraits<DWARFYAML::SegAddrPair> {
  static void mapping(IO &IO, DWARFYAML::SegAddrPair &SegAddrPair);
};

template <> struct MappingTraits<DWARFYAML::DWARFOperation> {
  static void mapping(IO &IO, DWARFYAML::DWARFOperation &DWARFOperation);
};

template <typename EntryType>
struct MappingTraits<DWARFYAML::ListTable<EntryType>> {
  static void mapping(IO &IO, DWARFYAML::ListTable<EntryType> &ListTable);
};

template <typename EntryType>
struct MappingTraits<DWARFYAML::ListEntries<EntryType>> {
  static void mapping(IO &IO, DWARFYAML::ListEntries<EntryType> &ListEntries);
  static std::string validate(IO &IO,
                              DWARFYAML::ListEntries<EntryType> &ListEntries);
};

template <> struct MappingTraits<DWARFYAML::RnglistEntry> {
  static void mapping(IO &IO, DWARFYAML::RnglistEntry &RnglistEntry);
};

template <> struct MappingTraits<DWARFYAML::LoclistEntry> {
  static void mapping(IO &IO, DWARFYAML::LoclistEntry &LoclistEntry);
};

template <> struct MappingTraits<DWARFYAML::AddrTableEntry> {
  static void mapping(IO &IO, DWARFYAML::AddrTableEntry &AddrTable);
};

template <> struct MappingTraits<DWARFYAML::StringOffsetsTable> {
  static void mapping(IO &IO, DWARFYAML::StringOffsetsTable &StrOffsetsTable);
};

template <> struct ScalarEnumerationTraits<dwarf::DwarfFormat> {
  static void enumeration(IO &IO, dwarf::DwarfFormat &Format) {
    IO.enumCase(Format, "DWARF32", dwarf::DWARF32);
    IO.enumCase(Format, "DWARF64", dwarf::DWARF64);
  }
};

#define HANDLE_DW_TAG(unused, name, unused2, unused3, unused4)                 \
  io.enumCase(value, "DW_TAG_" #name, dwarf::DW_TAG_##name);

template <> struct ScalarEnumerationTraits<dwarf::Tag> {
  static void enumeration(IO &io, dwarf::Tag &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_LNS(unused, name)                                            \
  io.enumCase(value, "DW_LNS_" #name, dwarf::DW_LNS_##name);

template <> struct ScalarEnumerationTraits<dwarf::LineNumberOps> {
  static void enumeration(IO &io, dwarf::LineNumberOps &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex8>(value);
  }
};

#define HANDLE_DW_LNE(unused, name)                                            \
  io.enumCase(value, "DW_LNE_" #name, dwarf::DW_LNE_##name);

template <> struct ScalarEnumerationTraits<dwarf::LineNumberExtendedOps> {
  static void enumeration(IO &io, dwarf::LineNumberExtendedOps &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_AT(unused, name, unused2, unused3)                           \
  io.enumCase(value, "DW_AT_" #name, dwarf::DW_AT_##name);

template <> struct ScalarEnumerationTraits<dwarf::Attribute> {
  static void enumeration(IO &io, dwarf::Attribute &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_FORM(unused, name, unused2, unused3)                         \
  io.enumCase(value, "DW_FORM_" #name, dwarf::DW_FORM_##name);

template <> struct ScalarEnumerationTraits<dwarf::Form> {
  static void enumeration(IO &io, dwarf::Form &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_IDX(unused, name)                                            \
  io.enumCase(value, "DW_IDX_" #name, dwarf::DW_IDX_##name);

template <> struct ScalarEnumerationTraits<dwarf::Index> {
  static void enumeration(IO &io, dwarf::Index &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_UT(unused, name)                                             \
  io.enumCase(value, "DW_UT_" #name, dwarf::DW_UT_##name);

template <> struct ScalarEnumerationTraits<dwarf::UnitType> {
  static void enumeration(IO &io, dwarf::UnitType &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex8>(value);
  }
};

template <> struct ScalarEnumerationTraits<dwarf::Constants> {
  static void enumeration(IO &io, dwarf::Constants &value) {
    io.enumCase(value, "DW_CHILDREN_no", dwarf::DW_CHILDREN_no);
    io.enumCase(value, "DW_CHILDREN_yes", dwarf::DW_CHILDREN_yes);
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_RLE(unused, name)                                            \
  io.enumCase(value, "DW_RLE_" #name, dwarf::DW_RLE_##name);

template <> struct ScalarEnumerationTraits<dwarf::RnglistEntries> {
  static void enumeration(IO &io, dwarf::RnglistEntries &value) {
#include "llvm/BinaryFormat/Dwarf.def"
  }
};

#define HANDLE_DW_LLE(unused, name)                                            \
  io.enumCase(value, "DW_LLE_" #name, dwarf::DW_LLE_##name);

template <> struct ScalarEnumerationTraits<dwarf::LoclistEntries> {
  static void enumeration(IO &io, dwarf::LoclistEntries &value) {
#include "llvm/BinaryFormat/Dwarf.def"
  }
};

#define HANDLE_DW_OP(id, name, operands, arity, version, vendor)               \
  io.enumCase(value, "DW_OP_" #name, dwarf::DW_OP_##name);

template <> struct ScalarEnumerationTraits<dwarf::LocationAtom> {
  static void enumeration(IO &io, dwarf::LocationAtom &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<yaml::Hex8>(value);
  }
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_DWARFYAML_H
