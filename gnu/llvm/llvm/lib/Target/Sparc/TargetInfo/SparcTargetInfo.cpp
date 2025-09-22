//===-- SparcTargetInfo.cpp - Sparc Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/SparcTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheSparcTarget() {
  static Target TheSparcTarget;
  return TheSparcTarget;
}
Target &llvm::getTheSparcV9Target() {
  static Target TheSparcV9Target;
  return TheSparcV9Target;
}
Target &llvm::getTheSparcelTarget() {
  static Target TheSparcelTarget;
  return TheSparcelTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSparcTargetInfo() {
  RegisterTarget<Triple::sparc, /*HasJIT=*/false> X(getTheSparcTarget(),
                                                    "sparc", "Sparc", "Sparc");
  RegisterTarget<Triple::sparcv9, /*HasJIT=*/false> Y(
      getTheSparcV9Target(), "sparcv9", "Sparc V9", "Sparc");
  RegisterTarget<Triple::sparcel, /*HasJIT=*/false> Z(
      getTheSparcelTarget(), "sparcel", "Sparc LE", "Sparc");
}
