//===- ARC.h - Top-level interface for ARC representation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// ARC back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARC_H
#define LLVM_LIB_TARGET_ARC_ARC_H

#include "MCTargetDesc/ARCMCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class ARCTargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createARCISelDag(ARCTargetMachine &TM, CodeGenOptLevel OptLevel);
FunctionPass *createARCExpandPseudosPass();
FunctionPass *createARCOptAddrMode();
FunctionPass *createARCBranchFinalizePass();
void initializeARCDAGToDAGISelLegacyPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARC_H
