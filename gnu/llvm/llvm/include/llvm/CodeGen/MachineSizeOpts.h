//===- MachineSizeOpts.h - machine size optimization ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some shared machine IR code size optimization related
// code.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_MACHINESIZEOPTS_H
#define LLVM_CODEGEN_MACHINESIZEOPTS_H

#include "llvm/Transforms/Utils/SizeOpts.h"

namespace llvm {

class ProfileSummaryInfo;
class MachineBasicBlock;
class MachineBlockFrequencyInfo;
class MachineFunction;
class MBFIWrapper;

/// Returns true if machine function \p MF is suggested to be size-optimized
/// based on the profile.
bool shouldOptimizeForSize(const MachineFunction *MF, ProfileSummaryInfo *PSI,
                           const MachineBlockFrequencyInfo *BFI,
                           PGSOQueryType QueryType = PGSOQueryType::Other);
/// Returns true if machine basic block \p MBB is suggested to be size-optimized
/// based on the profile.
bool shouldOptimizeForSize(const MachineBasicBlock *MBB,
                           ProfileSummaryInfo *PSI,
                           const MachineBlockFrequencyInfo *MBFI,
                           PGSOQueryType QueryType = PGSOQueryType::Other);
/// Returns true if machine basic block \p MBB is suggested to be size-optimized
/// based on the profile.
bool shouldOptimizeForSize(const MachineBasicBlock *MBB,
                           ProfileSummaryInfo *PSI,
                           MBFIWrapper *MBFIWrapper,
                           PGSOQueryType QueryType = PGSOQueryType::Other);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINESIZEOPTS_H
