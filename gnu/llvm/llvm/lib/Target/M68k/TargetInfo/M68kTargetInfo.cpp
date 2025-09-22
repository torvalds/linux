//===-- M68kTargetInfo.cpp - M68k Target Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains M68k target initializer.
///
//===----------------------------------------------------------------------===//
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

namespace llvm {
Target &getTheM68kTarget() {
  static Target TheM68kTarget;
  return TheM68kTarget;
}
} // namespace llvm

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM68kTargetInfo() {
  RegisterTarget<Triple::m68k, /*HasJIT=*/true> X(
      getTheM68kTarget(), "m68k", "Motorola 68000 family", "M68k");
}
