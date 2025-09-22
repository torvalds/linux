//===- DXContainerYAML.h - DXContainer YAMLIO implementation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of DXContainer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DXCONTAINERYAML_H
#define LLVM_OBJECTYAML_DXCONTAINERYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace DXContainerYAML {

struct VersionTuple {
  uint16_t Major;
  uint16_t Minor;
};

// The optional header fields are required in the binary and will be populated
// when reading from binary, but can be omitted in the YAML text because the
// emitter can calculate them.
struct FileHeader {
  std::vector<llvm::yaml::Hex8> Hash;
  VersionTuple Version;
  std::optional<uint32_t> FileSize;
  uint32_t PartCount;
  std::optional<std::vector<uint32_t>> PartOffsets;
};

struct DXILProgram {
  uint8_t MajorVersion;
  uint8_t MinorVersion;
  uint16_t ShaderKind;
  std::optional<uint32_t> Size;
  uint16_t DXILMajorVersion;
  uint16_t DXILMinorVersion;
  std::optional<uint32_t> DXILOffset;
  std::optional<uint32_t> DXILSize;
  std::optional<std::vector<llvm::yaml::Hex8>> DXIL;
};

#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str) bool Val = false;
struct ShaderFeatureFlags {
  ShaderFeatureFlags() = default;
  ShaderFeatureFlags(uint64_t FlagData);
  uint64_t getEncodedFlags();
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

struct ShaderHash {
  ShaderHash() = default;
  ShaderHash(const dxbc::ShaderHash &Data);

  bool IncludesSource;
  std::vector<llvm::yaml::Hex8> Digest;
};

using ResourceBindInfo = dxbc::PSV::v2::ResourceBindInfo;

struct SignatureElement {
  SignatureElement() = default;

  SignatureElement(dxbc::PSV::v0::SignatureElement El, StringRef StringTable,
                   ArrayRef<uint32_t> IdxTable)
      : Name(StringTable.substr(El.NameOffset,
                                StringTable.find('\0', El.NameOffset) -
                                    El.NameOffset)),
        Indices(IdxTable.slice(El.IndicesOffset, El.Rows)),
        StartRow(El.StartRow), Cols(El.Cols), StartCol(El.StartCol),
        Allocated(El.Allocated != 0), Kind(El.Kind), Type(El.Type),
        Mode(El.Mode), DynamicMask(El.DynamicMask), Stream(El.Stream) {}
  StringRef Name;
  SmallVector<uint32_t> Indices;

  uint8_t StartRow;
  uint8_t Cols;
  uint8_t StartCol;
  bool Allocated;
  dxbc::PSV::SemanticKind Kind;

  dxbc::PSV::ComponentType Type;
  dxbc::PSV::InterpolationMode Mode;
  llvm::yaml::Hex8 DynamicMask;
  uint8_t Stream;
};

struct PSVInfo {
  // The version field isn't actually encoded in the file, but it is inferred by
  // the size of data regions. We include it in the yaml because it simplifies
  // the format.
  uint32_t Version;

  dxbc::PSV::v3::RuntimeInfo Info;
  uint32_t ResourceStride;
  SmallVector<ResourceBindInfo> Resources;
  SmallVector<SignatureElement> SigInputElements;
  SmallVector<SignatureElement> SigOutputElements;
  SmallVector<SignatureElement> SigPatchOrPrimElements;

  using MaskVector = SmallVector<llvm::yaml::Hex32>;
  std::array<MaskVector, 4> OutputVectorMasks;
  MaskVector PatchOrPrimMasks;
  std::array<MaskVector, 4> InputOutputMap;
  MaskVector InputPatchMap;
  MaskVector PatchOutputMap;

  StringRef EntryName;

  void mapInfoForVersion(yaml::IO &IO);

  PSVInfo();
  PSVInfo(const dxbc::PSV::v0::RuntimeInfo *P, uint16_t Stage);
  PSVInfo(const dxbc::PSV::v1::RuntimeInfo *P);
  PSVInfo(const dxbc::PSV::v2::RuntimeInfo *P);
  PSVInfo(const dxbc::PSV::v3::RuntimeInfo *P, StringRef StringTable);
};

struct SignatureParameter {
  uint32_t Stream;
  std::string Name;
  uint32_t Index;
  dxbc::D3DSystemValue SystemValue;
  dxbc::SigComponentType CompType;
  uint32_t Register;
  uint8_t Mask;
  uint8_t ExclusiveMask;
  dxbc::SigMinPrecision MinPrecision;
};

struct Signature {
  llvm::SmallVector<SignatureParameter> Parameters;
};

struct Part {
  Part() = default;
  Part(std::string N, uint32_t S) : Name(N), Size(S) {}
  std::string Name;
  uint32_t Size;
  std::optional<DXILProgram> Program;
  std::optional<ShaderFeatureFlags> Flags;
  std::optional<ShaderHash> Hash;
  std::optional<PSVInfo> Info;
  std::optional<DXContainerYAML::Signature> Signature;
};

struct Object {
  FileHeader Header;
  std::vector<Part> Parts;
};

} // namespace DXContainerYAML
} // namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DXContainerYAML::Part)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DXContainerYAML::ResourceBindInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DXContainerYAML::SignatureElement)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DXContainerYAML::PSVInfo::MaskVector)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::DXContainerYAML::SignatureParameter)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::dxbc::PSV::SemanticKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::dxbc::PSV::ComponentType)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::dxbc::PSV::InterpolationMode)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::dxbc::D3DSystemValue)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::dxbc::SigComponentType)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::dxbc::SigMinPrecision)

namespace llvm {

class raw_ostream;

namespace yaml {

template <> struct MappingTraits<DXContainerYAML::VersionTuple> {
  static void mapping(IO &IO, DXContainerYAML::VersionTuple &Version);
};

template <> struct MappingTraits<DXContainerYAML::FileHeader> {
  static void mapping(IO &IO, DXContainerYAML::FileHeader &Header);
};

template <> struct MappingTraits<DXContainerYAML::DXILProgram> {
  static void mapping(IO &IO, DXContainerYAML::DXILProgram &Program);
};

template <> struct MappingTraits<DXContainerYAML::ShaderFeatureFlags> {
  static void mapping(IO &IO, DXContainerYAML::ShaderFeatureFlags &Flags);
};

template <> struct MappingTraits<DXContainerYAML::ShaderHash> {
  static void mapping(IO &IO, DXContainerYAML::ShaderHash &Hash);
};

template <> struct MappingTraits<DXContainerYAML::PSVInfo> {
  static void mapping(IO &IO, DXContainerYAML::PSVInfo &PSV);
};

template <> struct MappingTraits<DXContainerYAML::Part> {
  static void mapping(IO &IO, DXContainerYAML::Part &Version);
};

template <> struct MappingTraits<DXContainerYAML::Object> {
  static void mapping(IO &IO, DXContainerYAML::Object &Obj);
};

template <> struct MappingTraits<DXContainerYAML::ResourceBindInfo> {
  static void mapping(IO &IO, DXContainerYAML::ResourceBindInfo &Res);
};

template <> struct MappingTraits<DXContainerYAML::SignatureElement> {
  static void mapping(IO &IO, llvm::DXContainerYAML::SignatureElement &El);
};

template <> struct MappingTraits<DXContainerYAML::SignatureParameter> {
  static void mapping(IO &IO, llvm::DXContainerYAML::SignatureParameter &El);
};

template <> struct MappingTraits<DXContainerYAML::Signature> {
  static void mapping(IO &IO, llvm::DXContainerYAML::Signature &El);
};

} // namespace yaml

} // namespace llvm

#endif // LLVM_OBJECTYAML_DXCONTAINERYAML_H
