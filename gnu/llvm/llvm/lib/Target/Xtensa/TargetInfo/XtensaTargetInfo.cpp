//===-- XtensaTargetInfo.cpp - Xtensa Target Implementation ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/XtensaTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheXtensaTarget() {
  static Target TheXtensaTarget;
  return TheXtensaTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXtensaTargetInfo() {
  RegisterTarget<Triple::xtensa> X(getTheXtensaTarget(), "xtensa", "Xtensa 32",
                                   "XTENSA");
}
