//===-- LoongArchTargetInfo.cpp - LoongArch Target Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/LoongArchTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheLoongArch32Target() {
  static Target TheLoongArch32Target;
  return TheLoongArch32Target;
}

Target &llvm::getTheLoongArch64Target() {
  static Target TheLoongArch64Target;
  return TheLoongArch64Target;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLoongArchTargetInfo() {
  RegisterTarget<Triple::loongarch32, /*HasJIT=*/false> X(
      getTheLoongArch32Target(), "loongarch32", "32-bit LoongArch",
      "LoongArch");
  RegisterTarget<Triple::loongarch64, /*HasJIT=*/true> Y(
      getTheLoongArch64Target(), "loongarch64", "64-bit LoongArch",
      "LoongArch");
}
