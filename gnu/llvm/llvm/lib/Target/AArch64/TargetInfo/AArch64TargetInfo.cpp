//===-- AArch64TargetInfo.cpp - AArch64 Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/AArch64TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;
Target &llvm::getTheAArch64leTarget() {
  static Target TheAArch64leTarget;
  return TheAArch64leTarget;
}
Target &llvm::getTheAArch64beTarget() {
  static Target TheAArch64beTarget;
  return TheAArch64beTarget;
}
Target &llvm::getTheAArch64_32Target() {
  static Target TheAArch64leTarget;
  return TheAArch64leTarget;
}
Target &llvm::getTheARM64Target() {
  static Target TheARM64Target;
  return TheARM64Target;
}
Target &llvm::getTheARM64_32Target() {
  static Target TheARM64_32Target;
  return TheARM64_32Target;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAArch64TargetInfo() {
  // Now register the "arm64" name for use with "-march". We don't want it to
  // take possession of the Triple::aarch64 tags though.
  TargetRegistry::RegisterTarget(getTheARM64Target(), "arm64",
                                 "ARM64 (little endian)", "AArch64",
                                 [](Triple::ArchType) { return false; }, true);
  TargetRegistry::RegisterTarget(getTheARM64_32Target(), "arm64_32",
                                 "ARM64 (little endian ILP32)", "AArch64",
                                 [](Triple::ArchType) { return false; }, true);

  RegisterTarget<Triple::aarch64, /*HasJIT=*/true> Z(
      getTheAArch64leTarget(), "aarch64", "AArch64 (little endian)", "AArch64");
  RegisterTarget<Triple::aarch64_be, /*HasJIT=*/true> W(
      getTheAArch64beTarget(), "aarch64_be", "AArch64 (big endian)", "AArch64");
  RegisterTarget<Triple::aarch64_32, /*HasJIT=*/true> X(
      getTheAArch64_32Target(), "aarch64_32", "AArch64 (little endian ILP32)", "AArch64");
}
