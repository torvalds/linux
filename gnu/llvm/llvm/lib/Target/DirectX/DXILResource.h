//===- DXILResource.h - DXIL Resource helper objects ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper objects for working with DXIL Resources.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_DIRECTX_DXILRESOURCE_H
#define LLVM_TARGET_DIRECTX_DXILRESOURCE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/HLSL/HLSLResource.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DXILABI.h"
#include <cstdint>

namespace llvm {
class Module;
class GlobalVariable;

namespace dxil {
class CBufferDataLayout;

class ResourceBase {
protected:
  uint32_t ID;
  GlobalVariable *GV;
  StringRef Name;
  uint32_t Space;
  uint32_t LowerBound;
  uint32_t RangeSize;
  ResourceBase(uint32_t I, hlsl::FrontendResource R);

  void write(LLVMContext &Ctx, MutableArrayRef<Metadata *> Entries) const;

  void print(raw_ostream &O, StringRef IDPrefix, StringRef BindingPrefix) const;
  static StringRef getKindName(dxil::ResourceKind Kind);
  static void printKind(dxil::ResourceKind Kind, unsigned Alignment,
                        raw_ostream &OS, bool SRV = false,
                        bool HasCounter = false, uint32_t SampleCount = 0);

  static StringRef getElementTypeName(dxil::ElementType CompType);
  static void printElementType(dxil::ResourceKind Kind,
                               dxil::ElementType CompType, unsigned Alignment,
                               raw_ostream &OS);

public:
  struct ExtendedProperties {
    std::optional<dxil::ElementType> ElementType;

    // The value ordering of this enumeration is part of the DXIL ABI. Elements
    // can only be added to the end, and not removed.
    enum Tags : uint32_t {
      TypedBufferElementType = 0,
      StructuredBufferElementStride,
      SamplerFeedbackKind,
      Atomic64Use
    };

    MDNode *write(LLVMContext &Ctx) const;
  };
};

class UAVResource : public ResourceBase {
  dxil::ResourceKind Shape;
  bool GloballyCoherent;
  bool HasCounter;
  bool IsROV;
  ResourceBase::ExtendedProperties ExtProps;

  void parseSourceType(StringRef S);

public:
  UAVResource(uint32_t I, hlsl::FrontendResource R)
      : ResourceBase(I, R), Shape(R.getResourceKind()), GloballyCoherent(false),
        HasCounter(false), IsROV(R.getIsROV()), ExtProps{R.getElementType()} {}

  MDNode *write() const;
  void print(raw_ostream &O) const;
};

class ConstantBuffer : public ResourceBase {
  uint32_t CBufferSizeInBytes = 0; // Cbuffer used size in bytes.
public:
  ConstantBuffer(uint32_t I, hlsl::FrontendResource R);
  void setSize(CBufferDataLayout &DL);
  MDNode *write() const;
  void print(raw_ostream &O) const;
};

template <typename T> class ResourceTable {
  StringRef MDName;

  llvm::SmallVector<T> Data;

public:
  ResourceTable(StringRef Name) : MDName(Name) {}
  void collect(Module &M);
  MDNode *write(Module &M) const;
  void print(raw_ostream &O) const;
};

// FIXME: Fully computing the resource structures requires analyzing the IR
// because some flags are set based on what operations are performed on the
// resource. This partial patch handles some of the leg work, but not all of it.
// See issue https://github.com/llvm/llvm-project/issues/57936.
class Resources {
  ResourceTable<UAVResource> UAVs = {"hlsl.uavs"};
  ResourceTable<ConstantBuffer> CBuffers = {"hlsl.cbufs"};

public:
  void collect(Module &M);
  void write(Module &M) const;
  void print(raw_ostream &O) const;
  LLVM_DUMP_METHOD void dump() const;
};

} // namespace dxil
} // namespace llvm

#endif // LLVM_TARGET_DIRECTX_DXILRESOURCE_H
