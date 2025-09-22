//===- llvm/CodeGen/MBFIWrapper.h -------------------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_MBFIWRAPPER_H
#define LLVM_CODEGEN_MBFIWRAPPER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/BlockFrequency.h"
#include <optional>

namespace llvm {

class MachineBasicBlock;
class MachineBlockFrequencyInfo;

class MBFIWrapper {
 public:
  MBFIWrapper(const MachineBlockFrequencyInfo &I) : MBFI(I) {}

  BlockFrequency getBlockFreq(const MachineBasicBlock *MBB) const;
  void setBlockFreq(const MachineBasicBlock *MBB, BlockFrequency F);
  std::optional<uint64_t>
  getBlockProfileCount(const MachineBasicBlock *MBB) const;

  void view(const Twine &Name, bool isSimple = true);
  BlockFrequency getEntryFreq() const;
  const MachineBlockFrequencyInfo &getMBFI() const { return MBFI; }

private:
  const MachineBlockFrequencyInfo &MBFI;
  DenseMap<const MachineBasicBlock *, BlockFrequency> MergedBBFreq;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MBFIWRAPPER_H
