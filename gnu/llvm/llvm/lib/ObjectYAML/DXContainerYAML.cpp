//===- DXContainerYAML.cpp - DXContainer YAMLIO implementation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of
// DXContainerYAML.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {

// This assert is duplicated here to leave a breadcrumb of the places that need
// to be updated if flags grow past 64-bits.
static_assert((uint64_t)dxbc::FeatureFlags::NextUnusedBit <= 1ull << 63,
              "Shader flag bits exceed enum size.");

DXContainerYAML::ShaderFeatureFlags::ShaderFeatureFlags(uint64_t FlagData) {
#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str)                      \
  Val = (FlagData & (uint64_t)dxbc::FeatureFlags::Val) > 0;
#include "llvm/BinaryFormat/DXContainerConstants.def"
}

uint64_t DXContainerYAML::ShaderFeatureFlags::getEncodedFlags() {
  uint64_t Flag = 0;
#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str)                      \
  if (Val)                                                                     \
    Flag |= (uint64_t)dxbc::FeatureFlags::Val;
#include "llvm/BinaryFormat/DXContainerConstants.def"
  return Flag;
}

DXContainerYAML::ShaderHash::ShaderHash(const dxbc::ShaderHash &Data)
    : IncludesSource((Data.Flags & static_cast<uint32_t>(
                                       dxbc::HashFlags::IncludesSource)) != 0),
      Digest(16, 0) {
  memcpy(Digest.data(), &Data.Digest[0], 16);
}

DXContainerYAML::PSVInfo::PSVInfo() : Version(0) {
  memset(&Info, 0, sizeof(Info));
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v0::RuntimeInfo *P,
                                  uint16_t Stage)
    : Version(0) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v0::RuntimeInfo));

  assert(Stage < std::numeric_limits<uint8_t>::max() &&
         "Stage should be a very small number");
  // We need to bring the stage in separately since it isn't part of the v1 data
  // structure.
  Info.ShaderStage = static_cast<uint8_t>(Stage);
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v1::RuntimeInfo *P)
    : Version(1) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v1::RuntimeInfo));
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v2::RuntimeInfo *P)
    : Version(2) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v2::RuntimeInfo));
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v3::RuntimeInfo *P,
                                  StringRef StringTable)
    : Version(3),
      EntryName(StringTable.substr(P->EntryNameOffset,
                                   StringTable.find('\0', P->EntryNameOffset) -
                                       P->EntryNameOffset)) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v3::RuntimeInfo));
}

namespace yaml {

void MappingTraits<DXContainerYAML::VersionTuple>::mapping(
    IO &IO, DXContainerYAML::VersionTuple &Version) {
  IO.mapRequired("Major", Version.Major);
  IO.mapRequired("Minor", Version.Minor);
}

void MappingTraits<DXContainerYAML::FileHeader>::mapping(
    IO &IO, DXContainerYAML::FileHeader &Header) {
  IO.mapRequired("Hash", Header.Hash);
  IO.mapRequired("Version", Header.Version);
  IO.mapOptional("FileSize", Header.FileSize);
  IO.mapRequired("PartCount", Header.PartCount);
  IO.mapOptional("PartOffsets", Header.PartOffsets);
}

void MappingTraits<DXContainerYAML::DXILProgram>::mapping(
    IO &IO, DXContainerYAML::DXILProgram &Program) {
  IO.mapRequired("MajorVersion", Program.MajorVersion);
  IO.mapRequired("MinorVersion", Program.MinorVersion);
  IO.mapRequired("ShaderKind", Program.ShaderKind);
  IO.mapOptional("Size", Program.Size);
  IO.mapRequired("DXILMajorVersion", Program.DXILMajorVersion);
  IO.mapRequired("DXILMinorVersion", Program.DXILMinorVersion);
  IO.mapOptional("DXILSize", Program.DXILSize);
  IO.mapOptional("DXIL", Program.DXIL);
}

void MappingTraits<DXContainerYAML::ShaderFeatureFlags>::mapping(
    IO &IO, DXContainerYAML::ShaderFeatureFlags &Flags) {
#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str)                      \
  IO.mapRequired(#Val, Flags.Val);
#include "llvm/BinaryFormat/DXContainerConstants.def"
}

void MappingTraits<DXContainerYAML::ShaderHash>::mapping(
    IO &IO, DXContainerYAML::ShaderHash &Hash) {
  IO.mapRequired("IncludesSource", Hash.IncludesSource);
  IO.mapRequired("Digest", Hash.Digest);
}

void MappingTraits<DXContainerYAML::PSVInfo>::mapping(
    IO &IO, DXContainerYAML::PSVInfo &PSV) {
  IO.mapRequired("Version", PSV.Version);

  // Store the PSV version in the YAML context.
  void *OldContext = IO.getContext();
  uint32_t Version = PSV.Version;
  IO.setContext(&Version);

  // Restore the YAML context on function exit.
  auto RestoreContext = make_scope_exit([&]() { IO.setContext(OldContext); });

  // Shader stage is only included in binaries for v1 and later, but we always
  // include it since it simplifies parsing and file construction.
  IO.mapRequired("ShaderStage", PSV.Info.ShaderStage);
  PSV.mapInfoForVersion(IO);

  IO.mapRequired("ResourceStride", PSV.ResourceStride);
  IO.mapRequired("Resources", PSV.Resources);
  if (PSV.Version == 0)
    return;
  IO.mapRequired("SigInputElements", PSV.SigInputElements);
  IO.mapRequired("SigOutputElements", PSV.SigOutputElements);
  IO.mapRequired("SigPatchOrPrimElements", PSV.SigPatchOrPrimElements);

  Triple::EnvironmentType Stage = dxbc::getShaderStage(PSV.Info.ShaderStage);
  if (PSV.Info.UsesViewID) {
    MutableArrayRef<SmallVector<llvm::yaml::Hex32>> MutableOutMasks(
        PSV.OutputVectorMasks);
    IO.mapRequired("OutputVectorMasks", MutableOutMasks);
    if (Stage == Triple::EnvironmentType::Hull)
      IO.mapRequired("PatchOrPrimMasks", PSV.PatchOrPrimMasks);
  }
  MutableArrayRef<SmallVector<llvm::yaml::Hex32>> MutableIOMap(
      PSV.InputOutputMap);
  IO.mapRequired("InputOutputMap", MutableIOMap);

  if (Stage == Triple::EnvironmentType::Hull)
    IO.mapRequired("InputPatchMap", PSV.InputPatchMap);

  if (Stage == Triple::EnvironmentType::Domain)
    IO.mapRequired("PatchOutputMap", PSV.PatchOutputMap);
}

void MappingTraits<DXContainerYAML::SignatureParameter>::mapping(
    IO &IO, DXContainerYAML::SignatureParameter &S) {
  IO.mapRequired("Stream", S.Stream);
  IO.mapRequired("Name", S.Name);
  IO.mapRequired("Index", S.Index);
  IO.mapRequired("SystemValue", S.SystemValue);
  IO.mapRequired("CompType", S.CompType);
  IO.mapRequired("Register", S.Register);
  IO.mapRequired("Mask", S.Mask);
  IO.mapRequired("ExclusiveMask", S.ExclusiveMask);
  IO.mapRequired("MinPrecision", S.MinPrecision);
}

void MappingTraits<DXContainerYAML::Signature>::mapping(
    IO &IO, DXContainerYAML::Signature &S) {
  IO.mapRequired("Parameters", S.Parameters);
}

void MappingTraits<DXContainerYAML::Part>::mapping(IO &IO,
                                                   DXContainerYAML::Part &P) {
  IO.mapRequired("Name", P.Name);
  IO.mapRequired("Size", P.Size);
  IO.mapOptional("Program", P.Program);
  IO.mapOptional("Flags", P.Flags);
  IO.mapOptional("Hash", P.Hash);
  IO.mapOptional("PSVInfo", P.Info);
  IO.mapOptional("Signature", P.Signature);
}

void MappingTraits<DXContainerYAML::Object>::mapping(
    IO &IO, DXContainerYAML::Object &Obj) {
  IO.mapTag("!dxcontainer", true);
  IO.mapRequired("Header", Obj.Header);
  IO.mapRequired("Parts", Obj.Parts);
}

void MappingTraits<DXContainerYAML::ResourceBindInfo>::mapping(
    IO &IO, DXContainerYAML::ResourceBindInfo &Res) {
  IO.mapRequired("Type", Res.Type);
  IO.mapRequired("Space", Res.Space);
  IO.mapRequired("LowerBound", Res.LowerBound);
  IO.mapRequired("UpperBound", Res.UpperBound);

  const uint32_t *PSVVersion = static_cast<uint32_t *>(IO.getContext());
  if (*PSVVersion < 2)
    return;

  IO.mapRequired("Kind", Res.Kind);
  IO.mapRequired("Flags", Res.Flags);
}

void MappingTraits<DXContainerYAML::SignatureElement>::mapping(
    IO &IO, DXContainerYAML::SignatureElement &El) {
  IO.mapRequired("Name", El.Name);
  IO.mapRequired("Indices", El.Indices);
  IO.mapRequired("StartRow", El.StartRow);
  IO.mapRequired("Cols", El.Cols);
  IO.mapRequired("StartCol", El.StartCol);
  IO.mapRequired("Allocated", El.Allocated);
  IO.mapRequired("Kind", El.Kind);
  IO.mapRequired("ComponentType", El.Type);
  IO.mapRequired("Interpolation", El.Mode);
  IO.mapRequired("DynamicMask", El.DynamicMask);
  IO.mapRequired("Stream", El.Stream);
}

void ScalarEnumerationTraits<dxbc::PSV::SemanticKind>::enumeration(
    IO &IO, dxbc::PSV::SemanticKind &Value) {
  for (const auto &E : dxbc::PSV::getSemanticKinds())
    IO.enumCase(Value, E.Name.str().c_str(), E.Value);
}

void ScalarEnumerationTraits<dxbc::PSV::ComponentType>::enumeration(
    IO &IO, dxbc::PSV::ComponentType &Value) {
  for (const auto &E : dxbc::PSV::getComponentTypes())
    IO.enumCase(Value, E.Name.str().c_str(), E.Value);
}

void ScalarEnumerationTraits<dxbc::PSV::InterpolationMode>::enumeration(
    IO &IO, dxbc::PSV::InterpolationMode &Value) {
  for (const auto &E : dxbc::PSV::getInterpolationModes())
    IO.enumCase(Value, E.Name.str().c_str(), E.Value);
}

void ScalarEnumerationTraits<dxbc::D3DSystemValue>::enumeration(
    IO &IO, dxbc::D3DSystemValue &Value) {
  for (const auto &E : dxbc::getD3DSystemValues())
    IO.enumCase(Value, E.Name.str().c_str(), E.Value);
}

void ScalarEnumerationTraits<dxbc::SigMinPrecision>::enumeration(
    IO &IO, dxbc::SigMinPrecision &Value) {
  for (const auto &E : dxbc::getSigMinPrecisions())
    IO.enumCase(Value, E.Name.str().c_str(), E.Value);
}

void ScalarEnumerationTraits<dxbc::SigComponentType>::enumeration(
    IO &IO, dxbc::SigComponentType &Value) {
  for (const auto &E : dxbc::getSigComponentTypes())
    IO.enumCase(Value, E.Name.str().c_str(), E.Value);
}

} // namespace yaml

void DXContainerYAML::PSVInfo::mapInfoForVersion(yaml::IO &IO) {
  dxbc::PipelinePSVInfo &StageInfo = Info.StageInfo;
  Triple::EnvironmentType Stage = dxbc::getShaderStage(Info.ShaderStage);

  switch (Stage) {
  case Triple::EnvironmentType::Pixel:
    IO.mapRequired("DepthOutput", StageInfo.PS.DepthOutput);
    IO.mapRequired("SampleFrequency", StageInfo.PS.SampleFrequency);
    break;
  case Triple::EnvironmentType::Vertex:
    IO.mapRequired("OutputPositionPresent", StageInfo.VS.OutputPositionPresent);
    break;
  case Triple::EnvironmentType::Geometry:
    IO.mapRequired("InputPrimitive", StageInfo.GS.InputPrimitive);
    IO.mapRequired("OutputTopology", StageInfo.GS.OutputTopology);
    IO.mapRequired("OutputStreamMask", StageInfo.GS.OutputStreamMask);
    IO.mapRequired("OutputPositionPresent", StageInfo.GS.OutputPositionPresent);
    break;
  case Triple::EnvironmentType::Hull:
    IO.mapRequired("InputControlPointCount",
                   StageInfo.HS.InputControlPointCount);
    IO.mapRequired("OutputControlPointCount",
                   StageInfo.HS.OutputControlPointCount);
    IO.mapRequired("TessellatorDomain", StageInfo.HS.TessellatorDomain);
    IO.mapRequired("TessellatorOutputPrimitive",
                   StageInfo.HS.TessellatorOutputPrimitive);
    break;
  case Triple::EnvironmentType::Domain:
    IO.mapRequired("InputControlPointCount",
                   StageInfo.DS.InputControlPointCount);
    IO.mapRequired("OutputPositionPresent", StageInfo.DS.OutputPositionPresent);
    IO.mapRequired("TessellatorDomain", StageInfo.DS.TessellatorDomain);
    break;
  case Triple::EnvironmentType::Mesh:
    IO.mapRequired("GroupSharedBytesUsed", StageInfo.MS.GroupSharedBytesUsed);
    IO.mapRequired("GroupSharedBytesDependentOnViewID",
                   StageInfo.MS.GroupSharedBytesDependentOnViewID);
    IO.mapRequired("PayloadSizeInBytes", StageInfo.MS.PayloadSizeInBytes);
    IO.mapRequired("MaxOutputVertices", StageInfo.MS.MaxOutputVertices);
    IO.mapRequired("MaxOutputPrimitives", StageInfo.MS.MaxOutputPrimitives);
    break;
  case Triple::EnvironmentType::Amplification:
    IO.mapRequired("PayloadSizeInBytes", StageInfo.AS.PayloadSizeInBytes);
    break;
  default:
    break;
  }

  IO.mapRequired("MinimumWaveLaneCount", Info.MinimumWaveLaneCount);
  IO.mapRequired("MaximumWaveLaneCount", Info.MaximumWaveLaneCount);

  if (Version == 0)
    return;

  IO.mapRequired("UsesViewID", Info.UsesViewID);

  switch (Stage) {
  case Triple::EnvironmentType::Geometry:
    IO.mapRequired("MaxVertexCount", Info.GeomData.MaxVertexCount);
    break;
  case Triple::EnvironmentType::Hull:
  case Triple::EnvironmentType::Domain:
    IO.mapRequired("SigPatchConstOrPrimVectors",
                   Info.GeomData.SigPatchConstOrPrimVectors);
    break;
  case Triple::EnvironmentType::Mesh:
    IO.mapRequired("SigPrimVectors", Info.GeomData.MeshInfo.SigPrimVectors);
    IO.mapRequired("MeshOutputTopology",
                   Info.GeomData.MeshInfo.MeshOutputTopology);
    break;
  default:
    break;
  }

  IO.mapRequired("SigInputVectors", Info.SigInputVectors);
  MutableArrayRef<uint8_t> Vec(Info.SigOutputVectors);
  IO.mapRequired("SigOutputVectors", Vec);

  if (Version == 1)
    return;

  IO.mapRequired("NumThreadsX", Info.NumThreadsX);
  IO.mapRequired("NumThreadsY", Info.NumThreadsY);
  IO.mapRequired("NumThreadsZ", Info.NumThreadsZ);

  if (Version == 2)
    return;

  IO.mapRequired("EntryName", EntryName);
}

} // namespace llvm
