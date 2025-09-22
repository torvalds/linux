//===- MachOYAML.h - Mach-O YAMLIO implementation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of Mach-O.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_MACHOYAML_H
#define LLVM_OBJECTYAML_MACHOYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace MachOYAML {

struct Relocation {
  // Offset in the section to what is being relocated.
  llvm::yaml::Hex32 address;
  // Symbol index if r_extern == 1 else section index.
  uint32_t symbolnum;
  bool is_pcrel;
  // Real length = 2 ^ length.
  uint8_t length;
  bool is_extern;
  uint8_t type;
  bool is_scattered;
  int32_t value;
};

struct Section {
  char sectname[16];
  char segname[16];
  llvm::yaml::Hex64 addr;
  uint64_t size;
  llvm::yaml::Hex32 offset;
  uint32_t align;
  llvm::yaml::Hex32 reloff;
  uint32_t nreloc;
  llvm::yaml::Hex32 flags;
  llvm::yaml::Hex32 reserved1;
  llvm::yaml::Hex32 reserved2;
  llvm::yaml::Hex32 reserved3;
  std::optional<llvm::yaml::BinaryRef> content;
  std::vector<Relocation> relocations;
};

struct FileHeader {
  llvm::yaml::Hex32 magic;
  llvm::yaml::Hex32 cputype;
  llvm::yaml::Hex32 cpusubtype;
  llvm::yaml::Hex32 filetype;
  uint32_t ncmds;
  uint32_t sizeofcmds;
  llvm::yaml::Hex32 flags;
  llvm::yaml::Hex32 reserved;
};

struct LoadCommand {
  virtual ~LoadCommand();

  llvm::MachO::macho_load_command Data;
  std::vector<Section> Sections;
  std::vector<MachO::build_tool_version> Tools;
  std::vector<llvm::yaml::Hex8> PayloadBytes;
  std::string Content;
  uint64_t ZeroPadBytes;
};

struct NListEntry {
  uint32_t n_strx;
  llvm::yaml::Hex8 n_type;
  uint8_t n_sect;
  uint16_t n_desc;
  uint64_t n_value;
};

struct RebaseOpcode {
  MachO::RebaseOpcode Opcode;
  uint8_t Imm;
  std::vector<yaml::Hex64> ExtraData;
};

struct BindOpcode {
  MachO::BindOpcode Opcode;
  uint8_t Imm;
  std::vector<yaml::Hex64> ULEBExtraData;
  std::vector<int64_t> SLEBExtraData;
  StringRef Symbol;
};

struct ExportEntry {
  uint64_t TerminalSize = 0;
  uint64_t NodeOffset = 0;
  std::string Name;
  llvm::yaml::Hex64 Flags = 0;
  llvm::yaml::Hex64 Address = 0;
  llvm::yaml::Hex64 Other = 0;
  std::string ImportName;
  std::vector<MachOYAML::ExportEntry> Children;
};

struct DataInCodeEntry {
  llvm::yaml::Hex32 Offset;
  uint16_t Length;
  llvm::yaml::Hex16 Kind;
};

struct LinkEditData {
  std::vector<MachOYAML::RebaseOpcode> RebaseOpcodes;
  std::vector<MachOYAML::BindOpcode> BindOpcodes;
  std::vector<MachOYAML::BindOpcode> WeakBindOpcodes;
  std::vector<MachOYAML::BindOpcode> LazyBindOpcodes;
  MachOYAML::ExportEntry ExportTrie;
  std::vector<NListEntry> NameList;
  std::vector<StringRef> StringTable;
  std::vector<yaml::Hex32> IndirectSymbols;
  std::vector<yaml::Hex64> FunctionStarts;
  std::vector<DataInCodeEntry> DataInCode;
  std::vector<yaml::Hex8> ChainedFixups;

  bool isEmpty() const;
};

struct Object {
  bool IsLittleEndian;
  FileHeader Header;
  std::vector<LoadCommand> LoadCommands;
  std::vector<Section> Sections;
  LinkEditData LinkEdit;
  std::optional<llvm::yaml::BinaryRef> RawLinkEditSegment;
  DWARFYAML::Data DWARF;
};

struct FatHeader {
  llvm::yaml::Hex32 magic;
  uint32_t nfat_arch;
};

struct FatArch {
  llvm::yaml::Hex32 cputype;
  llvm::yaml::Hex32 cpusubtype;
  llvm::yaml::Hex64 offset;
  uint64_t size;
  uint32_t align;
  llvm::yaml::Hex32 reserved;
};

struct UniversalBinary {
  FatHeader Header;
  std::vector<FatArch> FatArchs;
  std::vector<Object> Slices;
};

} // end namespace MachOYAML
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::LoadCommand)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::Relocation)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::Section)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::RebaseOpcode)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::BindOpcode)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::ExportEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::NListEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::Object)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::FatArch)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachOYAML::DataInCodeEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MachO::build_tool_version)

namespace llvm {

class raw_ostream;

namespace yaml {

template <> struct MappingTraits<MachOYAML::FileHeader> {
  static void mapping(IO &IO, MachOYAML::FileHeader &FileHeader);
};

template <> struct MappingTraits<MachOYAML::Object> {
  static void mapping(IO &IO, MachOYAML::Object &Object);
};

template <> struct MappingTraits<MachOYAML::FatHeader> {
  static void mapping(IO &IO, MachOYAML::FatHeader &FatHeader);
};

template <> struct MappingTraits<MachOYAML::FatArch> {
  static void mapping(IO &IO, MachOYAML::FatArch &FatArch);
};

template <> struct MappingTraits<MachOYAML::UniversalBinary> {
  static void mapping(IO &IO, MachOYAML::UniversalBinary &UniversalBinary);
};

template <> struct MappingTraits<MachOYAML::LoadCommand> {
  static void mapping(IO &IO, MachOYAML::LoadCommand &LoadCommand);
};

template <> struct MappingTraits<MachOYAML::LinkEditData> {
  static void mapping(IO &IO, MachOYAML::LinkEditData &LinkEditData);
};

template <> struct MappingTraits<MachOYAML::RebaseOpcode> {
  static void mapping(IO &IO, MachOYAML::RebaseOpcode &RebaseOpcode);
};

template <> struct MappingTraits<MachOYAML::BindOpcode> {
  static void mapping(IO &IO, MachOYAML::BindOpcode &BindOpcode);
};

template <> struct MappingTraits<MachOYAML::ExportEntry> {
  static void mapping(IO &IO, MachOYAML::ExportEntry &ExportEntry);
};

template <> struct MappingTraits<MachOYAML::Relocation> {
  static void mapping(IO &IO, MachOYAML::Relocation &R);
};

template <> struct MappingTraits<MachOYAML::Section> {
  static void mapping(IO &IO, MachOYAML::Section &Section);
  static std::string validate(IO &io, MachOYAML::Section &Section);
};

template <> struct MappingTraits<MachOYAML::NListEntry> {
  static void mapping(IO &IO, MachOYAML::NListEntry &NListEntry);
};

template <> struct MappingTraits<MachO::build_tool_version> {
  static void mapping(IO &IO, MachO::build_tool_version &tool);
};

template <> struct MappingTraits<MachOYAML::DataInCodeEntry> {
  static void mapping(IO &IO, MachOYAML::DataInCodeEntry &DataInCodeEntry);
};

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  io.enumCase(value, #LCName, MachO::LCName);

template <> struct ScalarEnumerationTraits<MachO::LoadCommandType> {
  static void enumeration(IO &io, MachO::LoadCommandType &value) {
#include "llvm/BinaryFormat/MachO.def"
    io.enumFallback<Hex32>(value);
  }
};

#define ENUM_CASE(Enum) io.enumCase(value, #Enum, MachO::Enum);

template <> struct ScalarEnumerationTraits<MachO::RebaseOpcode> {
  static void enumeration(IO &io, MachO::RebaseOpcode &value) {
    ENUM_CASE(REBASE_OPCODE_DONE)
    ENUM_CASE(REBASE_OPCODE_SET_TYPE_IMM)
    ENUM_CASE(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB)
    ENUM_CASE(REBASE_OPCODE_ADD_ADDR_ULEB)
    ENUM_CASE(REBASE_OPCODE_ADD_ADDR_IMM_SCALED)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_IMM_TIMES)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_ULEB_TIMES)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB)
    io.enumFallback<Hex8>(value);
  }
};

template <> struct ScalarEnumerationTraits<MachO::BindOpcode> {
  static void enumeration(IO &io, MachO::BindOpcode &value) {
    ENUM_CASE(BIND_OPCODE_DONE)
    ENUM_CASE(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM)
    ENUM_CASE(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB)
    ENUM_CASE(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM)
    ENUM_CASE(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM)
    ENUM_CASE(BIND_OPCODE_SET_TYPE_IMM)
    ENUM_CASE(BIND_OPCODE_SET_ADDEND_SLEB)
    ENUM_CASE(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB)
    ENUM_CASE(BIND_OPCODE_ADD_ADDR_ULEB)
    ENUM_CASE(BIND_OPCODE_DO_BIND)
    ENUM_CASE(BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB)
    ENUM_CASE(BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED)
    ENUM_CASE(BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB)
    io.enumFallback<Hex8>(value);
  }
};

// This trait is used for 16-byte chars in Mach structures used for strings
using char_16 = char[16];

template <> struct ScalarTraits<char_16> {
  static void output(const char_16 &Val, void *, raw_ostream &Out);
  static StringRef input(StringRef Scalar, void *, char_16 &Val);
  static QuotingType mustQuote(StringRef S);
};

// This trait is used for UUIDs. It reads and writes them matching otool's
// formatting style.
using uuid_t = raw_ostream::uuid_t;

template <> struct ScalarTraits<uuid_t> {
  static void output(const uuid_t &Val, void *, raw_ostream &Out);
  static StringRef input(StringRef Scalar, void *, uuid_t &Val);
  static QuotingType mustQuote(StringRef S);
};

// Load Command struct mapping traits

#define LOAD_COMMAND_STRUCT(LCStruct)                                          \
  template <> struct MappingTraits<MachO::LCStruct> {                          \
    static void mapping(IO &IO, MachO::LCStruct &LoadCommand);                 \
  };

#include "llvm/BinaryFormat/MachO.def"

// Extra structures used by load commands
template <> struct MappingTraits<MachO::dylib> {
  static void mapping(IO &IO, MachO::dylib &LoadCommand);
};

template <> struct MappingTraits<MachO::fvmlib> {
  static void mapping(IO &IO, MachO::fvmlib &LoadCommand);
};

template <> struct MappingTraits<MachO::section> {
  static void mapping(IO &IO, MachO::section &LoadCommand);
};

template <> struct MappingTraits<MachO::section_64> {
  static void mapping(IO &IO, MachO::section_64 &LoadCommand);
};

} // end namespace yaml

} // end namespace llvm

#endif // LLVM_OBJECTYAML_MACHOYAML_H
