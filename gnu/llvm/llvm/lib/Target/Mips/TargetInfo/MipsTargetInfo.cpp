//===-- MipsTargetInfo.cpp - Mips Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/MipsTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheMipsTarget() {
  static Target TheMipsTarget;
  return TheMipsTarget;
}
Target &llvm::getTheMipselTarget() {
  static Target TheMipselTarget;
  return TheMipselTarget;
}
Target &llvm::getTheMips64Target() {
  static Target TheMips64Target;
  return TheMips64Target;
}
Target &llvm::getTheMips64elTarget() {
  static Target TheMips64elTarget;
  return TheMips64elTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMipsTargetInfo() {
  RegisterTarget<Triple::mips,
                 /*HasJIT=*/true>
      X(getTheMipsTarget(), "mips", "MIPS (32-bit big endian)", "Mips");

  RegisterTarget<Triple::mipsel,
                 /*HasJIT=*/true>
      Y(getTheMipselTarget(), "mipsel", "MIPS (32-bit little endian)", "Mips");

  RegisterTarget<Triple::mips64,
                 /*HasJIT=*/true>
      A(getTheMips64Target(), "mips64", "MIPS (64-bit big endian)", "Mips");

  RegisterTarget<Triple::mips64el,
                 /*HasJIT=*/true>
      B(getTheMips64elTarget(), "mips64el", "MIPS (64-bit little endian)",
        "Mips");
}
