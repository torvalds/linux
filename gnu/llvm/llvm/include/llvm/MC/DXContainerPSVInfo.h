//===- llvm/MC/DXContainerPSVInfo.h - DXContainer PSVInfo -*- C++ -------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_DXCONTAINERPSVINFO_H
#define LLVM_MC_DXCONTAINERPSVINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/TargetParser/Triple.h"

#include <array>
#include <numeric>
#include <stdint.h>

namespace llvm {

class raw_ostream;

namespace mcdxbc {

struct PSVSignatureElement {
  StringRef Name;
  SmallVector<uint32_t> Indices;
  uint8_t StartRow;
  uint8_t Cols;
  uint8_t StartCol;
  bool Allocated;
  dxbc::PSV::SemanticKind Kind;
  dxbc::PSV::ComponentType Type;
  dxbc::PSV::InterpolationMode Mode;
  uint8_t DynamicMask;
  uint8_t Stream;
};

// This data structure is a helper for reading and writing PSV RuntimeInfo data.
// It is implemented in the BinaryFormat library so that it can be used by both
// the MC layer and Object tools.
// This structure is used to represent the extracted data in an inspectable and
// modifiable format, and can be used to serialize the data back into valid PSV
// RuntimeInfo.
struct PSVRuntimeInfo {
  PSVRuntimeInfo() : DXConStrTabBuilder(StringTableBuilder::DXContainer) {
    memset((void *)&BaseData, 0, sizeof(dxbc::PSV::v3::RuntimeInfo));
  }
  bool IsFinalized = false;
  dxbc::PSV::v3::RuntimeInfo BaseData;
  SmallVector<dxbc::PSV::v2::ResourceBindInfo> Resources;
  SmallVector<PSVSignatureElement> InputElements;
  SmallVector<PSVSignatureElement> OutputElements;
  SmallVector<PSVSignatureElement> PatchOrPrimElements;

  // TODO: Make this interface user-friendly.
  // The interface here is bad, and we'll want to change this in the future. We
  // probably will want to build out these mask vectors as vectors of bools and
  // have this utility object convert them to the bit masks. I don't want to
  // over-engineer this API now since we don't know what the data coming in to
  // feed it will look like, so I kept it extremely simple for the immediate use
  // case.
  std::array<SmallVector<uint32_t>, 4> OutputVectorMasks;
  SmallVector<uint32_t> PatchOrPrimMasks;
  std::array<SmallVector<uint32_t>, 4> InputOutputMap;
  SmallVector<uint32_t> InputPatchMap;
  SmallVector<uint32_t> PatchOutputMap;
  llvm::StringRef EntryName;

  // Serialize PSVInfo into the provided raw_ostream. The version field
  // specifies the data version to encode, the default value specifies encoding
  // the highest supported version.
  void write(raw_ostream &OS,
             uint32_t Version = std::numeric_limits<uint32_t>::max()) const;

  void finalize(Triple::EnvironmentType Stage);

private:
  SmallVector<uint32_t, 64> IndexBuffer;
  SmallVector<llvm::dxbc::PSV::v0::SignatureElement, 32> SignatureElements;
  StringTableBuilder DXConStrTabBuilder;
};

class Signature {
  struct Parameter {
    uint32_t Stream;
    StringRef Name;
    uint32_t Index;
    dxbc::D3DSystemValue SystemValue;
    dxbc::SigComponentType CompType;
    uint32_t Register;
    uint8_t Mask;
    uint8_t ExclusiveMask;
    dxbc::SigMinPrecision MinPrecision;
  };

  SmallVector<Parameter> Params;

public:
  void addParam(uint32_t Stream, StringRef Name, uint32_t Index,
                dxbc::D3DSystemValue SystemValue,
                dxbc::SigComponentType CompType, uint32_t Register,
                uint8_t Mask, uint8_t ExclusiveMask,
                dxbc::SigMinPrecision MinPrecision) {
    Params.push_back(Parameter{Stream, Name, Index, SystemValue, CompType,
                               Register, Mask, ExclusiveMask, MinPrecision});
  }

  void write(raw_ostream &OS);
};

} // namespace mcdxbc
} // namespace llvm

#endif // LLVM_MC_DXCONTAINERPSVINFO_H
