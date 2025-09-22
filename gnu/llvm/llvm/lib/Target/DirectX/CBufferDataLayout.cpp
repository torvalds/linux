//===- Target/DirectX/CBufferDataLayout.cpp - Cbuffer layout helper -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utils to help cbuffer layout.
//
//===----------------------------------------------------------------------===//

#include "CBufferDataLayout.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {
namespace dxil {

// Implement cbuffer layout in
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
class LegacyCBufferLayout {
  struct LegacyStructLayout {
    StructType *ST;
    SmallVector<uint32_t> Offsets;
    TypeSize Size = {0, false};
    std::pair<uint32_t, uint32_t> getElementLegacyOffset(unsigned Idx) const {
      assert(Idx < Offsets.size() && "Invalid element idx!");
      uint32_t Offset = Offsets[Idx];
      uint32_t Ch = Offset & (RowAlign - 1);
      return std::make_pair((Offset - Ch) / RowAlign, Ch);
    }
  };

public:
  LegacyCBufferLayout(const DataLayout &DL) : DL(DL) {}
  TypeSize getTypeAllocSizeInBytes(Type *Ty);

private:
  TypeSize applyRowAlign(TypeSize Offset, Type *EltTy);
  TypeSize getTypeAllocSize(Type *Ty);
  LegacyStructLayout &getStructLayout(StructType *ST);
  const DataLayout &DL;
  SmallDenseMap<StructType *, LegacyStructLayout> StructLayouts;
  // 4 Dwords align.
  static const uint32_t RowAlign = 16;
  static TypeSize alignTo4Dwords(TypeSize Offset) {
    return alignTo(Offset, RowAlign);
  }
};

TypeSize LegacyCBufferLayout::getTypeAllocSizeInBytes(Type *Ty) {
  return getTypeAllocSize(Ty);
}

TypeSize LegacyCBufferLayout::applyRowAlign(TypeSize Offset, Type *EltTy) {
  TypeSize AlignedOffset = alignTo4Dwords(Offset);

  if (AlignedOffset == Offset)
    return Offset;

  if (isa<StructType>(EltTy) || isa<ArrayType>(EltTy))
    return AlignedOffset;
  TypeSize Size = DL.getTypeStoreSize(EltTy);
  if ((Offset + Size) > AlignedOffset)
    return AlignedOffset;
  else
    return Offset;
}

TypeSize LegacyCBufferLayout::getTypeAllocSize(Type *Ty) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    LegacyStructLayout &Layout = getStructLayout(ST);
    return Layout.Size;
  } else if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    unsigned NumElts = AT->getNumElements();
    if (NumElts == 0)
      return TypeSize::getFixed(0);

    TypeSize EltSize = getTypeAllocSize(AT->getElementType());
    TypeSize AlignedEltSize = alignTo4Dwords(EltSize);
    // Each new element start 4 dwords aligned.
    return TypeSize::getFixed(AlignedEltSize * (NumElts - 1) + EltSize);
  } else {
    // NOTE: Use type store size, not align to ABI on basic types for legacy
    // layout.
    return DL.getTypeStoreSize(Ty);
  }
}

LegacyCBufferLayout::LegacyStructLayout &
LegacyCBufferLayout::getStructLayout(StructType *ST) {
  auto it = StructLayouts.find(ST);
  if (it != StructLayouts.end())
    return it->second;

  TypeSize Offset = TypeSize::getFixed(0);
  LegacyStructLayout Layout;
  Layout.ST = ST;
  for (Type *EltTy : ST->elements()) {
    TypeSize EltSize = getTypeAllocSize(EltTy);
    if (TypeSize ScalarSize = EltTy->getScalarType()->getPrimitiveSizeInBits())
      Offset = alignTo(Offset, ScalarSize >> 3);
    Offset = applyRowAlign(Offset, EltTy);
    Layout.Offsets.emplace_back(Offset);
    Offset = Offset.getWithIncrement(EltSize);
  }
  Layout.Size = Offset;
  StructLayouts[ST] = Layout;
  return StructLayouts[ST];
}

CBufferDataLayout::CBufferDataLayout(const DataLayout &DL, const bool IsLegacy)
    : DL(DL), IsLegacyLayout(IsLegacy),
      LegacyDL(IsLegacy ? std::make_unique<LegacyCBufferLayout>(DL) : nullptr) {
}

CBufferDataLayout::~CBufferDataLayout() = default;

llvm::TypeSize CBufferDataLayout::getTypeAllocSizeInBytes(Type *Ty) {
  if (IsLegacyLayout)
    return LegacyDL->getTypeAllocSizeInBytes(Ty);
  else
    return DL.getTypeAllocSize(Ty);
}

} // namespace dxil
} // namespace llvm
