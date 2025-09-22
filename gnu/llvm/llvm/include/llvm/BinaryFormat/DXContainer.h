//===-- llvm/BinaryFormat/DXContainer.h - The DXBC file format --*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the DXContainer object file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_DXCONTAINER_H
#define LLVM_BINARYFORMAT_DXCONTAINER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/TargetParser/Triple.h"

#include <stdint.h>

namespace llvm {
template <typename T> struct EnumEntry;

// The DXContainer file format is arranged as a header and "parts". Semantically
// parts are similar to sections in other object file formats. The File format
// structure is roughly:

// ┌────────────────────────────────┐
// │             Header             │
// ├────────────────────────────────┤
// │              Part              │
// ├────────────────────────────────┤
// │              Part              │
// ├────────────────────────────────┤
// │              ...               │
// └────────────────────────────────┘

namespace dxbc {

inline Triple::EnvironmentType getShaderStage(uint32_t Kind) {
  assert(Kind <= Triple::Amplification - Triple::Pixel &&
         "Shader kind out of expected range.");
  return static_cast<Triple::EnvironmentType>(Triple::Pixel + Kind);
}

struct Hash {
  uint8_t Digest[16];
};

enum class HashFlags : uint32_t {
  None = 0,           // No flags defined.
  IncludesSource = 1, // This flag indicates that the shader hash was computed
                      // taking into account source information (-Zss)
};

struct ShaderHash {
  uint32_t Flags; // dxbc::HashFlags
  uint8_t Digest[16];

  bool isPopulated();

  void swapBytes() { sys::swapByteOrder(Flags); }
};

struct ContainerVersion {
  uint16_t Major;
  uint16_t Minor;

  void swapBytes() {
    sys::swapByteOrder(Major);
    sys::swapByteOrder(Minor);
  }
};

struct Header {
  uint8_t Magic[4]; // "DXBC"
  Hash FileHash;
  ContainerVersion Version;
  uint32_t FileSize;
  uint32_t PartCount;

  void swapBytes() {
    Version.swapBytes();
    sys::swapByteOrder(FileSize);
    sys::swapByteOrder(PartCount);
  }
  // Structure is followed by part offsets: uint32_t PartOffset[PartCount];
  // The offset is to a PartHeader, which is followed by the Part Data.
};

/// Use this type to describe the size and type of a DXIL container part.
struct PartHeader {
  uint8_t Name[4];
  uint32_t Size;

  void swapBytes() { sys::swapByteOrder(Size); }
  StringRef getName() const {
    return StringRef(reinterpret_cast<const char *>(&Name[0]), 4);
  }
  // Structure is followed directly by part data: uint8_t PartData[PartSize].
};

struct BitcodeHeader {
  uint8_t Magic[4];     // ACSII "DXIL".
  uint8_t MinorVersion; // DXIL version.
  uint8_t MajorVersion; // DXIL version.
  uint16_t Unused;
  uint32_t Offset; // Offset to LLVM bitcode (from start of header).
  uint32_t Size;   // Size of LLVM bitcode (in bytes).
  // Followed by uint8_t[BitcodeHeader.Size] at &BitcodeHeader + Header.Offset

  void swapBytes() {
    sys::swapByteOrder(MinorVersion);
    sys::swapByteOrder(MajorVersion);
    sys::swapByteOrder(Offset);
    sys::swapByteOrder(Size);
  }
};

struct ProgramHeader {
  uint8_t Version;
  uint8_t Unused;
  uint16_t ShaderKind;
  uint32_t Size; // Size in uint32_t words including this header.
  BitcodeHeader Bitcode;

  void swapBytes() {
    sys::swapByteOrder(ShaderKind);
    sys::swapByteOrder(Size);
    Bitcode.swapBytes();
  }
  uint8_t getMajorVersion() { return Version >> 4; }
  uint8_t getMinorVersion() { return Version & 0xF; }
  static uint8_t getVersion(uint8_t Major, uint8_t Minor) {
    return (Major << 4) | Minor;
  }
};

static_assert(sizeof(ProgramHeader) == 24, "ProgramHeader Size incorrect!");

#define CONTAINER_PART(Part) Part,
enum class PartType {
  Unknown = 0,
#include "DXContainerConstants.def"
};

#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str) Val = 1ull << Num,
enum class FeatureFlags : uint64_t {
#include "DXContainerConstants.def"
};
static_assert((uint64_t)FeatureFlags::NextUnusedBit <= 1ull << 63,
              "Shader flag bits exceed enum size.");

PartType parsePartType(StringRef S);

struct VertexPSVInfo {
  uint8_t OutputPositionPresent;
  uint8_t Unused[3];

  void swapBytes() {
    // nothing to swap
  }
};

struct HullPSVInfo {
  uint32_t InputControlPointCount;
  uint32_t OutputControlPointCount;
  uint32_t TessellatorDomain;
  uint32_t TessellatorOutputPrimitive;

  void swapBytes() {
    sys::swapByteOrder(InputControlPointCount);
    sys::swapByteOrder(OutputControlPointCount);
    sys::swapByteOrder(TessellatorDomain);
    sys::swapByteOrder(TessellatorOutputPrimitive);
  }
};

struct DomainPSVInfo {
  uint32_t InputControlPointCount;
  uint8_t OutputPositionPresent;
  uint8_t Unused[3];
  uint32_t TessellatorDomain;

  void swapBytes() {
    sys::swapByteOrder(InputControlPointCount);
    sys::swapByteOrder(TessellatorDomain);
  }
};

struct GeometryPSVInfo {
  uint32_t InputPrimitive;
  uint32_t OutputTopology;
  uint32_t OutputStreamMask;
  uint8_t OutputPositionPresent;
  uint8_t Unused[3];

  void swapBytes() {
    sys::swapByteOrder(InputPrimitive);
    sys::swapByteOrder(OutputTopology);
    sys::swapByteOrder(OutputStreamMask);
  }
};

struct PixelPSVInfo {
  uint8_t DepthOutput;
  uint8_t SampleFrequency;
  uint8_t Unused[2];

  void swapBytes() {
    // nothing to swap
  }
};

struct MeshPSVInfo {
  uint32_t GroupSharedBytesUsed;
  uint32_t GroupSharedBytesDependentOnViewID;
  uint32_t PayloadSizeInBytes;
  uint16_t MaxOutputVertices;
  uint16_t MaxOutputPrimitives;

  void swapBytes() {
    sys::swapByteOrder(GroupSharedBytesUsed);
    sys::swapByteOrder(GroupSharedBytesDependentOnViewID);
    sys::swapByteOrder(PayloadSizeInBytes);
    sys::swapByteOrder(MaxOutputVertices);
    sys::swapByteOrder(MaxOutputPrimitives);
  }
};

struct AmplificationPSVInfo {
  uint32_t PayloadSizeInBytes;

  void swapBytes() { sys::swapByteOrder(PayloadSizeInBytes); }
};

union PipelinePSVInfo {
  VertexPSVInfo VS;
  HullPSVInfo HS;
  DomainPSVInfo DS;
  GeometryPSVInfo GS;
  PixelPSVInfo PS;
  MeshPSVInfo MS;
  AmplificationPSVInfo AS;

  void swapBytes(Triple::EnvironmentType Stage) {
    switch (Stage) {
    case Triple::EnvironmentType::Pixel:
      PS.swapBytes();
      break;
    case Triple::EnvironmentType::Vertex:
      VS.swapBytes();
      break;
    case Triple::EnvironmentType::Geometry:
      GS.swapBytes();
      break;
    case Triple::EnvironmentType::Hull:
      HS.swapBytes();
      break;
    case Triple::EnvironmentType::Domain:
      DS.swapBytes();
      break;
    case Triple::EnvironmentType::Mesh:
      MS.swapBytes();
      break;
    case Triple::EnvironmentType::Amplification:
      AS.swapBytes();
      break;
    default:
      break;
    }
  }
};

static_assert(sizeof(PipelinePSVInfo) == 4 * sizeof(uint32_t),
              "Pipeline-specific PSV info must fit in 16 bytes.");

namespace PSV {

#define SEMANTIC_KIND(Val, Enum) Enum = Val,
enum class SemanticKind : uint8_t {
#include "DXContainerConstants.def"
};

ArrayRef<EnumEntry<SemanticKind>> getSemanticKinds();

#define COMPONENT_TYPE(Val, Enum) Enum = Val,
enum class ComponentType : uint8_t {
#include "DXContainerConstants.def"
};

ArrayRef<EnumEntry<ComponentType>> getComponentTypes();

#define INTERPOLATION_MODE(Val, Enum) Enum = Val,
enum class InterpolationMode : uint8_t {
#include "DXContainerConstants.def"
};

ArrayRef<EnumEntry<InterpolationMode>> getInterpolationModes();

namespace v0 {
struct RuntimeInfo {
  PipelinePSVInfo StageInfo;
  uint32_t MinimumWaveLaneCount; // minimum lane count required, 0 if unused
  uint32_t MaximumWaveLaneCount; // maximum lane count required,
                                 // 0xffffffff if unused
  void swapBytes() {
    // Skip the union because we don't know which field it has
    sys::swapByteOrder(MinimumWaveLaneCount);
    sys::swapByteOrder(MaximumWaveLaneCount);
  }

  void swapBytes(Triple::EnvironmentType Stage) { StageInfo.swapBytes(Stage); }
};

struct ResourceBindInfo {
  uint32_t Type;
  uint32_t Space;
  uint32_t LowerBound;
  uint32_t UpperBound;

  void swapBytes() {
    sys::swapByteOrder(Type);
    sys::swapByteOrder(Space);
    sys::swapByteOrder(LowerBound);
    sys::swapByteOrder(UpperBound);
  }
};

struct SignatureElement {
  uint32_t NameOffset;
  uint32_t IndicesOffset;

  uint8_t Rows;
  uint8_t StartRow;
  uint8_t Cols : 4;
  uint8_t StartCol : 2;
  uint8_t Allocated : 1;
  uint8_t Unused : 1;
  SemanticKind Kind;

  ComponentType Type;
  InterpolationMode Mode;
  uint8_t DynamicMask : 4;
  uint8_t Stream : 2;
  uint8_t Unused2 : 2;
  uint8_t Reserved;

  void swapBytes() {
    sys::swapByteOrder(NameOffset);
    sys::swapByteOrder(IndicesOffset);
  }
};

static_assert(sizeof(SignatureElement) == 4 * sizeof(uint32_t),
              "PSV Signature elements must fit in 16 bytes.");

} // namespace v0

namespace v1 {

struct MeshRuntimeInfo {
  uint8_t SigPrimVectors; // Primitive output for MS
  uint8_t MeshOutputTopology;
};

union GeometryExtraInfo {
  uint16_t MaxVertexCount;            // MaxVertexCount for GS only (max 1024)
  uint8_t SigPatchConstOrPrimVectors; // Output for HS; Input for DS;
                                      // Primitive output for MS (overlaps
                                      // MeshInfo::SigPrimVectors)
  MeshRuntimeInfo MeshInfo;
};
struct RuntimeInfo : public v0::RuntimeInfo {
  uint8_t ShaderStage; // PSVShaderKind
  uint8_t UsesViewID;
  GeometryExtraInfo GeomData;

  // PSVSignatureElement counts
  uint8_t SigInputElements;
  uint8_t SigOutputElements;
  uint8_t SigPatchOrPrimElements;

  // Number of packed vectors per signature
  uint8_t SigInputVectors;
  uint8_t SigOutputVectors[4];

  void swapBytes() {
    // nothing to swap since everything is single-byte or a union field
  }

  void swapBytes(Triple::EnvironmentType Stage) {
    v0::RuntimeInfo::swapBytes(Stage);
    if (Stage == Triple::EnvironmentType::Geometry)
      sys::swapByteOrder(GeomData.MaxVertexCount);
  }
};

} // namespace v1

namespace v2 {
struct RuntimeInfo : public v1::RuntimeInfo {
  uint32_t NumThreadsX;
  uint32_t NumThreadsY;
  uint32_t NumThreadsZ;

  void swapBytes() {
    sys::swapByteOrder(NumThreadsX);
    sys::swapByteOrder(NumThreadsY);
    sys::swapByteOrder(NumThreadsZ);
  }

  void swapBytes(Triple::EnvironmentType Stage) {
    v1::RuntimeInfo::swapBytes(Stage);
  }
};

struct ResourceBindInfo : public v0::ResourceBindInfo {
  uint32_t Kind;
  uint32_t Flags;

  void swapBytes() {
    v0::ResourceBindInfo::swapBytes();
    sys::swapByteOrder(Kind);
    sys::swapByteOrder(Flags);
  }
};

} // namespace v2

namespace v3 {
struct RuntimeInfo : public v2::RuntimeInfo {
  uint32_t EntryNameOffset;

  void swapBytes() {
    v2::RuntimeInfo::swapBytes();
    sys::swapByteOrder(EntryNameOffset);
  }

  void swapBytes(Triple::EnvironmentType Stage) {
    v2::RuntimeInfo::swapBytes(Stage);
  }
};

} // namespace v3
} // namespace PSV

#define COMPONENT_PRECISION(Val, Enum) Enum = Val,
enum class SigMinPrecision : uint32_t {
#include "DXContainerConstants.def"
};

ArrayRef<EnumEntry<SigMinPrecision>> getSigMinPrecisions();

#define D3D_SYSTEM_VALUE(Val, Enum) Enum = Val,
enum class D3DSystemValue : uint32_t {
#include "DXContainerConstants.def"
};

ArrayRef<EnumEntry<D3DSystemValue>> getD3DSystemValues();

#define COMPONENT_TYPE(Val, Enum) Enum = Val,
enum class SigComponentType : uint32_t {
#include "DXContainerConstants.def"
};

ArrayRef<EnumEntry<SigComponentType>> getSigComponentTypes();

struct ProgramSignatureHeader {
  uint32_t ParamCount;
  uint32_t FirstParamOffset;

  void swapBytes() {
    sys::swapByteOrder(ParamCount);
    sys::swapByteOrder(FirstParamOffset);
  }
};

struct ProgramSignatureElement {
  uint32_t Stream;     // Stream index (parameters must appear in non-decreasing
                       // stream order)
  uint32_t NameOffset; // Offset from the start of the ProgramSignatureHeader to
                       // the start of the null terminated string for the name.
  uint32_t Index;      // Semantic Index
  D3DSystemValue SystemValue; // Semantic type. Similar to PSV::SemanticKind.
  SigComponentType CompType;  // Type of bits.
  uint32_t Register;          // Register Index (row index)
  uint8_t Mask;               // Mask (column allocation)

  // The ExclusiveMask has a different meaning for input and output signatures.
  // For an output signature, masked components of the output register are never
  // written to.
  // For an input signature, masked components of the input register are always
  // read.
  uint8_t ExclusiveMask;

  uint16_t Unused;
  SigMinPrecision MinPrecision; // Minimum precision of input/output data

  void swapBytes() {
    sys::swapByteOrder(Stream);
    sys::swapByteOrder(NameOffset);
    sys::swapByteOrder(Index);
    sys::swapByteOrder(SystemValue);
    sys::swapByteOrder(CompType);
    sys::swapByteOrder(Register);
    sys::swapByteOrder(Mask);
    sys::swapByteOrder(ExclusiveMask);
    sys::swapByteOrder(MinPrecision);
  }
};

static_assert(sizeof(ProgramSignatureElement) == 32,
              "ProgramSignatureElement is misaligned");

} // namespace dxbc
} // namespace llvm

#endif // LLVM_BINARYFORMAT_DXCONTAINER_H
