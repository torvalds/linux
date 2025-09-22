//===- PdbYAML.h ---------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H
#define LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H

#include "OutputStyle.h"

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/ObjectYAML/CodeViewYAMLDebugSections.h"
#include "llvm/ObjectYAML/CodeViewYAMLSymbols.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/YAMLTraits.h"

#include <optional>
#include <vector>

namespace llvm {
namespace pdb {

namespace yaml {

struct MSFHeaders {
  msf::SuperBlock SuperBlock;
  uint32_t NumDirectoryBlocks = 0;
  std::vector<uint32_t> DirectoryBlocks;
  uint32_t NumStreams = 0;
  uint64_t FileSize = 0;
};

struct StreamBlockList {
  std::vector<uint32_t> Blocks;
};

struct NamedStreamMapping {
  StringRef StreamName;
  uint32_t StreamNumber;
};

struct PdbInfoStream {
  PdbRaw_ImplVer Version = PdbImplVC70;
  uint32_t Signature = 0;
  uint32_t Age = 1;
  codeview::GUID Guid;
  std::vector<PdbRaw_FeatureSig> Features;
  std::vector<NamedStreamMapping> NamedStreams;
};

struct PdbModiStream {
  uint32_t Signature;
  std::vector<CodeViewYAML::SymbolRecord> Symbols;
};

struct PdbDbiModuleInfo {
  StringRef Obj;
  StringRef Mod;
  std::vector<StringRef> SourceFiles;
  std::vector<CodeViewYAML::YAMLDebugSubsection> Subsections;
  std::optional<PdbModiStream> Modi;
};

struct PdbDbiStream {
  PdbRaw_DbiVer VerHeader = PdbDbiV70;
  uint32_t Age = 1;
  uint16_t BuildNumber = 0;
  uint32_t PdbDllVersion = 0;
  uint16_t PdbDllRbld = 0;
  uint16_t Flags = 1;
  PDB_Machine MachineType = PDB_Machine::x86;

  std::vector<PdbDbiModuleInfo> ModInfos;
};

struct PdbTpiStream {
  PdbRaw_TpiVer Version = PdbTpiV80;
  std::vector<CodeViewYAML::LeafRecord> Records;
};

struct PdbPublicsStream {
  std::vector<CodeViewYAML::SymbolRecord> PubSyms;
};

struct PdbObject {
  explicit PdbObject(BumpPtrAllocator &Allocator) : Allocator(Allocator) {}

  std::optional<MSFHeaders> Headers;
  std::optional<std::vector<uint32_t>> StreamSizes;
  std::optional<std::vector<StreamBlockList>> StreamMap;
  std::optional<PdbInfoStream> PdbStream;
  std::optional<PdbDbiStream> DbiStream;
  std::optional<PdbTpiStream> TpiStream;
  std::optional<PdbTpiStream> IpiStream;
  std::optional<PdbPublicsStream> PublicsStream;

  std::optional<std::vector<StringRef>> StringTable;

  BumpPtrAllocator &Allocator;
};
}
}
}

LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbObject)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::MSFHeaders)
LLVM_YAML_DECLARE_MAPPING_TRAITS(msf::SuperBlock)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::StreamBlockList)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbInfoStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbDbiStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbTpiStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbPublicsStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::NamedStreamMapping)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbModiStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbDbiModuleInfo)

#endif // LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H
