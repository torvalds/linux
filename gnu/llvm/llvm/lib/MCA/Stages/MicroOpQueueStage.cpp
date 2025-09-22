//===---------------------- MicroOpQueueStage.cpp ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the MicroOpQueueStage.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/MicroOpQueueStage.h"

namespace llvm {
namespace mca {

#define DEBUG_TYPE "llvm-mca"

Error MicroOpQueueStage::moveInstructions() {
  InstRef IR = Buffer[CurrentInstructionSlotIdx];
  while (IR && checkNextStage(IR)) {
    if (llvm::Error Val = moveToTheNextStage(IR))
      return Val;

    Buffer[CurrentInstructionSlotIdx].invalidate();
    unsigned NormalizedOpcodes = getNormalizedOpcodes(IR);
    CurrentInstructionSlotIdx += NormalizedOpcodes;
    CurrentInstructionSlotIdx %= Buffer.size();
    AvailableEntries += NormalizedOpcodes;
    IR = Buffer[CurrentInstructionSlotIdx];
  }

  return llvm::ErrorSuccess();
}

MicroOpQueueStage::MicroOpQueueStage(unsigned Size, unsigned IPC,
                                     bool ZeroLatencyStage)
    : NextAvailableSlotIdx(0), CurrentInstructionSlotIdx(0), MaxIPC(IPC),
      CurrentIPC(0), IsZeroLatencyStage(ZeroLatencyStage) {
  Buffer.resize(Size ? Size : 1);
  AvailableEntries = Buffer.size();
}

Error MicroOpQueueStage::execute(InstRef &IR) {
  Buffer[NextAvailableSlotIdx] = IR;
  unsigned NormalizedOpcodes = getNormalizedOpcodes(IR);
  NextAvailableSlotIdx += NormalizedOpcodes;
  NextAvailableSlotIdx %= Buffer.size();
  AvailableEntries -= NormalizedOpcodes;
  ++CurrentIPC;
  return llvm::ErrorSuccess();
}

Error MicroOpQueueStage::cycleStart() {
  CurrentIPC = 0;
  if (!IsZeroLatencyStage)
    return moveInstructions();
  return llvm::ErrorSuccess();
}

Error MicroOpQueueStage::cycleEnd() {
  if (IsZeroLatencyStage)
    return moveInstructions();
  return llvm::ErrorSuccess();
}

} // namespace mca
} // namespace llvm
