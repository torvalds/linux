//===-- DirectXContainerObjectWriter.cpp - DX object writer ----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains DXContainer object writers for the DirectX backend.
//
//===----------------------------------------------------------------------===//

#include "DirectXContainerObjectWriter.h"
#include "llvm/MC/MCDXContainerWriter.h"

using namespace llvm;

namespace {
class DirectXContainerObjectWriter : public MCDXContainerTargetWriter {
public:
  DirectXContainerObjectWriter() : MCDXContainerTargetWriter() {}
};
} // namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createDXContainerTargetObjectWriter() {
  return std::make_unique<DirectXContainerObjectWriter>();
}
