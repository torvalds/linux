//===- MIRYamlMapping.cpp - Describe mapping between MIR and YAML ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the mapping between various MIR data structures and
// their corresponding YAML representation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::yaml;

FrameIndex::FrameIndex(int FI, const llvm::MachineFrameInfo &MFI) {
  IsFixed = MFI.isFixedObjectIndex(FI);
  if (IsFixed)
    FI -= MFI.getObjectIndexBegin();
  this->FI = FI;
}

// Returns the value and if the frame index is fixed or not.
Expected<int> FrameIndex::getFI(const llvm::MachineFrameInfo &MFI) const {
  int FI = this->FI;
  if (IsFixed) {
    if (unsigned(FI) >= MFI.getNumFixedObjects())
      return make_error<StringError>(
          formatv("invalid fixed frame index {0}", FI).str(),
          inconvertibleErrorCode());
    FI += MFI.getObjectIndexBegin();
  }
  if (unsigned(FI + MFI.getNumFixedObjects()) >= MFI.getNumObjects())
    return make_error<StringError>(formatv("invalid frame index {0}", FI).str(),
                                   inconvertibleErrorCode());
  return FI;
}
