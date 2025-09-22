//===- MBFIWrapper.cpp - MachineBlockFrequencyInfo wrapper ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class keeps track of branch frequencies of newly created blocks and
// tail-merged blocks. Used by the TailDuplication and MachineBlockPlacement.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MBFIWrapper.h"
#include <optional>

using namespace llvm;

BlockFrequency MBFIWrapper::getBlockFreq(const MachineBasicBlock *MBB) const {
  auto I = MergedBBFreq.find(MBB);

  if (I != MergedBBFreq.end())
    return I->second;

  return MBFI.getBlockFreq(MBB);
}

void MBFIWrapper::setBlockFreq(const MachineBasicBlock *MBB,
                               BlockFrequency F) {
  MergedBBFreq[MBB] = F;
}

std::optional<uint64_t>
MBFIWrapper::getBlockProfileCount(const MachineBasicBlock *MBB) const {
  auto I = MergedBBFreq.find(MBB);

  // Modified block frequency also impacts profile count. So we should compute
  // profile count from new block frequency if it has been changed.
  if (I != MergedBBFreq.end())
    return MBFI.getProfileCountFromFreq(I->second);

  return MBFI.getBlockProfileCount(MBB);
}

void MBFIWrapper::view(const Twine &Name, bool isSimple) {
  MBFI.view(Name, isSimple);
}

BlockFrequency MBFIWrapper::getEntryFreq() const { return MBFI.getEntryFreq(); }
