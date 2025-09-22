//===----- llvm/CodeGen/GlobalISel/LostDebugLocObserver.h -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Tracks DebugLocs between checkpoints and verifies that they are transferred.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_GLOBALISEL_LOSTDEBUGLOCOBSERVER_H
#define LLVM_CODEGEN_GLOBALISEL_LOSTDEBUGLOCOBSERVER_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"

namespace llvm {
class LostDebugLocObserver : public GISelChangeObserver {
  StringRef DebugType;
  SmallSet<DebugLoc, 4> LostDebugLocs;
  SmallPtrSet<MachineInstr *, 4> PotentialMIsForDebugLocs;
  unsigned NumLostDebugLocs = 0;

public:
  LostDebugLocObserver(StringRef DebugType) : DebugType(DebugType) {}

  unsigned getNumLostDebugLocs() const { return NumLostDebugLocs; }

  /// Call this to indicate that it's a good point to assess whether locations
  /// have been lost. Typically this will be when a logical change has been
  /// completed such as the caller has finished replacing some instructions with
  /// alternatives. When CheckDebugLocs is true, the locations will be checked
  /// to see if any have been lost since the last checkpoint. When
  /// CheckDebugLocs is false, it will just reset ready for the next checkpoint
  /// without checking anything. This can be helpful to limit the detection to
  /// easy-to-fix portions of an algorithm before allowing more difficult ones.
  void checkpoint(bool CheckDebugLocs = true);

  void createdInstr(MachineInstr &MI) override;
  void erasingInstr(MachineInstr &MI) override;
  void changingInstr(MachineInstr &MI) override;
  void changedInstr(MachineInstr &MI) override;

private:
  void analyzeDebugLocations();
};

} // namespace llvm
#endif // LLVM_CODEGEN_GLOBALISEL_LOSTDEBUGLOCOBSERVER_H
