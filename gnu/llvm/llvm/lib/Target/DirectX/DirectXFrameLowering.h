//===-- DirectXFrameLowering.h - Frame lowering for DirectX --*- C++ ---*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements DirectX-specific bits of TargetFrameLowering class.
// This is just a stub because the current DXIL backend does not actually lower
// through the MC layer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_DIRECTXFRAMELOWERING_H
#define LLVM_DIRECTX_DIRECTXFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/Alignment.h"

namespace llvm {
class DirectXSubtarget;

class DirectXFrameLowering : public TargetFrameLowering {
public:
  explicit DirectXFrameLowering(const DirectXSubtarget &STI)
      : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(8), 0) {}

  void emitPrologue(MachineFunction &, MachineBasicBlock &) const override {}
  void emitEpilogue(MachineFunction &, MachineBasicBlock &) const override {}

  bool hasFP(const MachineFunction &) const override { return false; }
};
} // namespace llvm
#endif // LLVM_DIRECTX_DIRECTXFRAMELOWERING_H
