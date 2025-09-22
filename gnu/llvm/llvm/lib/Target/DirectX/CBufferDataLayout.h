//===- Target/DirectX/CBufferDataLayout.h - Cbuffer layout helper ---------===//
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

#ifndef LLVM_TARGET_DIRECTX_CBUFFERDATALAYOUT_H
#define LLVM_TARGET_DIRECTX_CBUFFERDATALAYOUT_H

#include "llvm/Support/TypeSize.h"

#include <memory>
#include <stdint.h>

namespace llvm {
class DataLayout;
class Type;

namespace dxil {

class LegacyCBufferLayout;

class CBufferDataLayout {
  const DataLayout &DL;
  const bool IsLegacyLayout;
  std::unique_ptr<LegacyCBufferLayout> LegacyDL;

public:
  CBufferDataLayout(const DataLayout &DL, const bool IsLegacy);
  ~CBufferDataLayout();
  llvm::TypeSize getTypeAllocSizeInBytes(Type *Ty);
};

} // namespace dxil
} // namespace llvm

#endif
