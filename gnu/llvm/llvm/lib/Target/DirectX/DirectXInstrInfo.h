//===-- DirectXInstrInfo.h - Define InstrInfo for DirectX -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DirectX specific subclass of TargetInstrInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_DIRECTXINSTRINFO_H
#define LLVM_DIRECTX_DIRECTXINSTRINFO_H

#include "DirectXRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "DirectXGenInstrInfo.inc"

namespace llvm {
struct DirectXInstrInfo : public DirectXGenInstrInfo {
  explicit DirectXInstrInfo() : DirectXGenInstrInfo() {}

  ~DirectXInstrInfo() override;
};
} // namespace llvm

#endif // LLVM_DIRECTX_DIRECTXINSTRINFO_H
