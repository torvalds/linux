//===- ARMMacroFusion.cpp - ARM Macro Fusion ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the ARM implementation of the DAG scheduling
///  mutation to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#include "ARMMacroFusion.h"
#include "ARMSubtarget.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

namespace llvm {

// Fuse AES crypto encoding or decoding.
static bool isAESPair(const MachineInstr *FirstMI,
                      const MachineInstr &SecondMI) {
  // Assume the 1st instr to be a wildcard if it is unspecified.
  switch(SecondMI.getOpcode()) {
  // AES encode.
  case ARM::AESMC :
    return FirstMI == nullptr || FirstMI->getOpcode() == ARM::AESE;
  // AES decode.
  case ARM::AESIMC:
    return FirstMI == nullptr || FirstMI->getOpcode() == ARM::AESD;
  }

  return false;
}

// Fuse literal generation.
static bool isLiteralsPair(const MachineInstr *FirstMI,
                           const MachineInstr &SecondMI) {
  // Assume the 1st instr to be a wildcard if it is unspecified.
  if ((FirstMI == nullptr || FirstMI->getOpcode() == ARM::MOVi16) &&
      SecondMI.getOpcode() == ARM::MOVTi16)
    return true;

  return false;
}

/// Check if the instr pair, FirstMI and SecondMI, should be fused
/// together. Given SecondMI, when FirstMI is unspecified, then check if
/// SecondMI may be part of a fused pair at all.
static bool shouldScheduleAdjacent(const TargetInstrInfo &TII,
                                   const TargetSubtargetInfo &TSI,
                                   const MachineInstr *FirstMI,
                                   const MachineInstr &SecondMI) {
  const ARMSubtarget &ST = static_cast<const ARMSubtarget&>(TSI);

  if (ST.hasFuseAES() && isAESPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseLiterals() && isLiteralsPair(FirstMI, SecondMI))
    return true;

  return false;
}

std::unique_ptr<ScheduleDAGMutation> createARMMacroFusionDAGMutation() {
  return createMacroFusionDAGMutation(shouldScheduleAdjacent);
}

} // end namespace llvm
