//===-- CSKY.h - Top-level interface for CSKY--------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// CSKY back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKY_H
#define LLVM_LIB_TARGET_CSKY_CSKY_H

#include "llvm/PassRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class CSKYTargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createCSKYISelDag(CSKYTargetMachine &TM,
                                CodeGenOptLevel OptLevel);
FunctionPass *createCSKYConstantIslandPass();

void initializeCSKYConstantIslandsPass(PassRegistry &);
void initializeCSKYDAGToDAGISelLegacyPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKY_H
