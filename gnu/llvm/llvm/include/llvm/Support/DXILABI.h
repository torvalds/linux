//===-- DXILABI.h - ABI Sensitive Values for DXIL ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions of various constants and enums that are
// required to remain stable as per the DXIL format's requirements.
//
// Documentation for DXIL can be found in
// https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DXILABI_H
#define LLVM_SUPPORT_DXILABI_H

#include "llvm/ADT/StringSwitch.h"

namespace llvm {
namespace dxil {

enum class ParameterKind : uint8_t {
  Invalid = 0,
  Void,
  Half,
  Float,
  Double,
  I1,
  I8,
  I16,
  I32,
  I64,
  Overload,
  CBufferRet,
  ResourceRet,
  DXILHandle,
};

enum class ResourceClass : uint8_t {
  SRV = 0,
  UAV,
  CBuffer,
  Sampler,
};

/// The kind of resource for an SRV or UAV resource. Sometimes referred to as
/// "Shape" in the DXIL docs.
enum class ResourceKind : uint32_t {
  Invalid = 0,
  Texture1D,
  Texture2D,
  Texture2DMS,
  Texture3D,
  TextureCube,
  Texture1DArray,
  Texture2DArray,
  Texture2DMSArray,
  TextureCubeArray,
  TypedBuffer,
  RawBuffer,
  StructuredBuffer,
  CBuffer,
  Sampler,
  TBuffer,
  RTAccelerationStructure,
  FeedbackTexture2D,
  FeedbackTexture2DArray,
  NumEntries,
};

/// The element type of an SRV or UAV resource.
enum class ElementType : uint32_t {
  Invalid = 0,
  I1,
  I16,
  U16,
  I32,
  U32,
  I64,
  U64,
  F16,
  F32,
  F64,
  SNormF16,
  UNormF16,
  SNormF32,
  UNormF32,
  SNormF64,
  UNormF64,
  PackedS8x32,
  PackedU8x32,
};

/// Metadata tags for extra resource properties.
enum class ExtPropTags : uint32_t {
  ElementType = 0,
  StructuredBufferStride = 1,
  SamplerFeedbackKind = 2,
  Atomic64Use = 3,
};

enum class SamplerType : uint32_t {
  Default = 0,
  Comparison = 1,
  Mono = 2, // Note: Seems to be unused.
};

enum class SamplerFeedbackType : uint32_t {
  MinMip = 0,
  MipRegionUsed = 1,
};

} // namespace dxil
} // namespace llvm

#endif // LLVM_SUPPORT_DXILABI_H
