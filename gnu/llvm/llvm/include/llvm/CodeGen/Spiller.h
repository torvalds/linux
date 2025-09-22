//===- llvm/CodeGen/Spiller.h - Spiller -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SPILLER_H
#define LLVM_CODEGEN_SPILLER_H

namespace llvm {

class LiveRangeEdit;
class MachineFunction;
class MachineFunctionPass;
class VirtRegMap;
class VirtRegAuxInfo;

/// Spiller interface.
///
/// Implementations are utility classes which insert spill or remat code on
/// demand.
class Spiller {
  virtual void anchor();

public:
  virtual ~Spiller() = 0;

  /// spill - Spill the LRE.getParent() live interval.
  virtual void spill(LiveRangeEdit &LRE) = 0;

  virtual void postOptimization() {}
};

/// Create and return a spiller that will insert spill code directly instead
/// of deferring though VirtRegMap.
Spiller *createInlineSpiller(MachineFunctionPass &Pass, MachineFunction &MF,
                             VirtRegMap &VRM, VirtRegAuxInfo &VRAI);

} // end namespace llvm

#endif // LLVM_CODEGEN_SPILLER_H
