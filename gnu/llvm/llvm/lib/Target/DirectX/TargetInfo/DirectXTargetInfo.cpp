//===- DirectXTargetInfo.cpp - DirectX Target Implementation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains DirectX target initializer.
///
//===----------------------------------------------------------------------===//

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
Target &getTheDirectXTarget() {
  static Target TheDirectXTarget;
  return TheDirectXTarget;
}
} // namespace llvm

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXTargetInfo() {
  RegisterTarget<Triple::dxil, /*HasJIT=*/false> X(
      getTheDirectXTarget(), "dxil", "DirectX Intermediate Language", "DXIL");
}
