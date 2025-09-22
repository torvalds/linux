//===- SPIRVObjectTargetWriter.cpp - SPIR-V Object Target Writer *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SPIRVMCTargetDesc.h"
#include "llvm/MC/MCSPIRVObjectWriter.h"

using namespace llvm;

namespace {

class SPIRVObjectTargetWriter : public MCSPIRVObjectTargetWriter {
public:
  SPIRVObjectTargetWriter() = default;
};

} // namespace

std::unique_ptr<MCObjectTargetWriter> llvm::createSPIRVObjectTargetWriter() {
  return std::make_unique<SPIRVObjectTargetWriter>();
}
