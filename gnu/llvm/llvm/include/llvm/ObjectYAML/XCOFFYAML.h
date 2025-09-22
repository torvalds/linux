//===----- XCOFFYAML.h - XCOFF YAMLIO implementation ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares classes for handling the YAML representation of XCOFF.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_OBJECTYAML_XCOFFYAML_H
#define LLVM_OBJECTYAML_XCOFFYAML_H

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/ObjectYAML/YAML.h"
#include <optional>
#include <vector>

namespace llvm {
namespace XCOFFYAML {

struct FileHeader {
  llvm::yaml::Hex16 Magic;
  uint16_t NumberOfSections;
  int32_t TimeStamp;
  llvm::yaml::Hex64 SymbolTableOffset;
  int32_t NumberOfSymTableEntries;
  uint16_t AuxHeaderSize;
  llvm::yaml::Hex16 Flags;
};

struct AuxiliaryHeader {
  std::optional<llvm::yaml::Hex16> Magic;
  std::optional<llvm::yaml::Hex16> Version;
  std::optional<llvm::yaml::Hex64> TextStartAddr;
  std::optional<llvm::yaml::Hex64> DataStartAddr;
  std::optional<llvm::yaml::Hex64> TOCAnchorAddr;
  std::optional<uint16_t> SecNumOfEntryPoint;
  std::optional<uint16_t> SecNumOfText;
  std::optional<uint16_t> SecNumOfData;
  std::optional<uint16_t> SecNumOfTOC;
  std::optional<uint16_t> SecNumOfLoader;
  std::optional<uint16_t> SecNumOfBSS;
  std::optional<llvm::yaml::Hex16> MaxAlignOfText;
  std::optional<llvm::yaml::Hex16> MaxAlignOfData;
  std::optional<llvm::yaml::Hex16> ModuleType;
  std::optional<llvm::yaml::Hex8> CpuFlag;
  std::optional<llvm::yaml::Hex8> CpuType;
  std::optional<llvm::yaml::Hex8> TextPageSize;
  std::optional<llvm::yaml::Hex8> DataPageSize;
  std::optional<llvm::yaml::Hex8> StackPageSize;
  std::optional<llvm::yaml::Hex8> FlagAndTDataAlignment;
  std::optional<llvm::yaml::Hex64> TextSize;
  std::optional<llvm::yaml::Hex64> InitDataSize;
  std::optional<llvm::yaml::Hex64> BssDataSize;
  std::optional<llvm::yaml::Hex64> EntryPointAddr;
  std::optional<llvm::yaml::Hex64> MaxStackSize;
  std::optional<llvm::yaml::Hex64> MaxDataSize;
  std::optional<uint16_t> SecNumOfTData;
  std::optional<uint16_t> SecNumOfTBSS;
  std::optional<llvm::yaml::Hex16> Flag;
};

struct Relocation {
  llvm::yaml::Hex64 VirtualAddress;
  llvm::yaml::Hex64 SymbolIndex;
  llvm::yaml::Hex8 Info;
  llvm::yaml::Hex8 Type;
};

struct Section {
  StringRef SectionName;
  llvm::yaml::Hex64 Address;
  llvm::yaml::Hex64 Size;
  llvm::yaml::Hex64 FileOffsetToData;
  llvm::yaml::Hex64 FileOffsetToRelocations;
  llvm::yaml::Hex64 FileOffsetToLineNumbers; // Line number pointer. Not supported yet.
  llvm::yaml::Hex16 NumberOfRelocations;
  llvm::yaml::Hex16 NumberOfLineNumbers; // Line number counts. Not supported yet.
  uint32_t Flags;
  std::optional<XCOFF::DwarfSectionSubtypeFlags> SectionSubtype;
  yaml::BinaryRef SectionData;
  std::vector<Relocation> Relocations;
};

enum AuxSymbolType : uint8_t {
  AUX_EXCEPT = 255,
  AUX_FCN = 254,
  AUX_SYM = 253,
  AUX_FILE = 252,
  AUX_CSECT = 251,
  AUX_SECT = 250,
  AUX_STAT = 249
};

struct AuxSymbolEnt {
  AuxSymbolType Type;

  explicit AuxSymbolEnt(AuxSymbolType T) : Type(T) {}
  virtual ~AuxSymbolEnt();
};

struct FileAuxEnt : AuxSymbolEnt {
  std::optional<StringRef> FileNameOrString;
  std::optional<XCOFF::CFileStringType> FileStringType;

  FileAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_FILE) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_FILE;
  }
};

struct CsectAuxEnt : AuxSymbolEnt {
  // Only for XCOFF32.
  std::optional<uint32_t> SectionOrLength;
  std::optional<uint32_t> StabInfoIndex;
  std::optional<uint16_t> StabSectNum;
  // Only for XCOFF64.
  std::optional<uint32_t> SectionOrLengthLo;
  std::optional<uint32_t> SectionOrLengthHi;
  // Common fields for both XCOFF32 and XCOFF64.
  std::optional<uint32_t> ParameterHashIndex;
  std::optional<uint16_t> TypeChkSectNum;
  std::optional<XCOFF::SymbolType> SymbolType;
  std::optional<uint8_t> SymbolAlignment;
  // The two previous values can be encoded as a single value.
  std::optional<uint8_t> SymbolAlignmentAndType;
  std::optional<XCOFF::StorageMappingClass> StorageMappingClass;

  CsectAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_CSECT) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_CSECT;
  }
};

struct FunctionAuxEnt : AuxSymbolEnt {
  std::optional<uint32_t> OffsetToExceptionTbl; // Only for XCOFF32.
  std::optional<uint64_t> PtrToLineNum;
  std::optional<uint32_t> SizeOfFunction;
  std::optional<int32_t> SymIdxOfNextBeyond;

  FunctionAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_FCN) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_FCN;
  }
};

struct ExcpetionAuxEnt : AuxSymbolEnt {
  std::optional<uint64_t> OffsetToExceptionTbl;
  std::optional<uint32_t> SizeOfFunction;
  std::optional<int32_t> SymIdxOfNextBeyond;

  ExcpetionAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_EXCEPT) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_EXCEPT;
  }
}; // Only for XCOFF64.

struct BlockAuxEnt : AuxSymbolEnt {
  // Only for XCOFF32.
  std::optional<uint16_t> LineNumHi;
  std::optional<uint16_t> LineNumLo;
  // Only for XCOFF64.
  std::optional<uint32_t> LineNum;

  BlockAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_SYM) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_SYM;
  }
};

struct SectAuxEntForDWARF : AuxSymbolEnt {
  std::optional<uint32_t> LengthOfSectionPortion;
  std::optional<uint32_t> NumberOfRelocEnt;

  SectAuxEntForDWARF() : AuxSymbolEnt(AuxSymbolType::AUX_SECT) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_SECT;
  }
};

struct SectAuxEntForStat : AuxSymbolEnt {
  std::optional<uint32_t> SectionLength;
  std::optional<uint16_t> NumberOfRelocEnt;
  std::optional<uint16_t> NumberOfLineNum;

  SectAuxEntForStat() : AuxSymbolEnt(AuxSymbolType::AUX_STAT) {}
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_STAT;
  }
}; // Only for XCOFF32.

struct Symbol {
  StringRef SymbolName;
  llvm::yaml::Hex64 Value; // Symbol value; storage class-dependent.
  std::optional<StringRef> SectionName;
  std::optional<uint16_t> SectionIndex;
  llvm::yaml::Hex16 Type;
  XCOFF::StorageClass StorageClass;
  std::optional<uint8_t> NumberOfAuxEntries;
  std::vector<std::unique_ptr<AuxSymbolEnt>> AuxEntries;
};

struct StringTable {
  std::optional<uint32_t> ContentSize; // The total size of the string table.
  std::optional<uint32_t> Length; // The value of the length field for the first
                                  // 4 bytes of the table.
  std::optional<std::vector<StringRef>> Strings;
  std::optional<yaml::BinaryRef> RawContent;
};

struct Object {
  FileHeader Header;
  std::optional<AuxiliaryHeader> AuxHeader;
  std::vector<Section> Sections;
  std::vector<Symbol> Symbols;
  StringTable StrTbl;
  Object();
};
} // namespace XCOFFYAML
} // namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(XCOFFYAML::Symbol)
LLVM_YAML_IS_SEQUENCE_VECTOR(XCOFFYAML::Relocation)
LLVM_YAML_IS_SEQUENCE_VECTOR(XCOFFYAML::Section)
LLVM_YAML_IS_SEQUENCE_VECTOR(std::unique_ptr<llvm::XCOFFYAML::AuxSymbolEnt>)

namespace llvm {
namespace yaml {

template <> struct ScalarBitSetTraits<XCOFF::SectionTypeFlags> {
  static void bitset(IO &IO, XCOFF::SectionTypeFlags &Value);
};

template <> struct ScalarEnumerationTraits<XCOFF::DwarfSectionSubtypeFlags> {
  static void enumeration(IO &IO, XCOFF::DwarfSectionSubtypeFlags &Value);
};

template <> struct ScalarEnumerationTraits<XCOFF::StorageClass> {
  static void enumeration(IO &IO, XCOFF::StorageClass &Value);
};

template <> struct ScalarEnumerationTraits<XCOFF::StorageMappingClass> {
  static void enumeration(IO &IO, XCOFF::StorageMappingClass &Value);
};

template <> struct ScalarEnumerationTraits<XCOFF::SymbolType> {
  static void enumeration(IO &IO, XCOFF::SymbolType &Value);
};

template <> struct ScalarEnumerationTraits<XCOFF::CFileStringType> {
  static void enumeration(IO &IO, XCOFF::CFileStringType &Type);
};

template <> struct ScalarEnumerationTraits<XCOFFYAML::AuxSymbolType> {
  static void enumeration(IO &IO, XCOFFYAML::AuxSymbolType &Type);
};

template <> struct MappingTraits<XCOFFYAML::FileHeader> {
  static void mapping(IO &IO, XCOFFYAML::FileHeader &H);
};

template <> struct MappingTraits<XCOFFYAML::AuxiliaryHeader> {
  static void mapping(IO &IO, XCOFFYAML::AuxiliaryHeader &AuxHdr);
};

template <> struct MappingTraits<std::unique_ptr<XCOFFYAML::AuxSymbolEnt>> {
  static void mapping(IO &IO, std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym);
};

template <> struct MappingTraits<XCOFFYAML::Symbol> {
  static void mapping(IO &IO, XCOFFYAML::Symbol &S);
};

template <> struct MappingTraits<XCOFFYAML::Relocation> {
  static void mapping(IO &IO, XCOFFYAML::Relocation &R);
};

template <> struct MappingTraits<XCOFFYAML::Section> {
  static void mapping(IO &IO, XCOFFYAML::Section &Sec);
};

template <> struct MappingTraits<XCOFFYAML::StringTable> {
  static void mapping(IO &IO, XCOFFYAML::StringTable &Str);
};

template <> struct MappingTraits<XCOFFYAML::Object> {
  static void mapping(IO &IO, XCOFFYAML::Object &Obj);
};

} // namespace yaml
} // namespace llvm

#endif // LLVM_OBJECTYAML_XCOFFYAML_H
