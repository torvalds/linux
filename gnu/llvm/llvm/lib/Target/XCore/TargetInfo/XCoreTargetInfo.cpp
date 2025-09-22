//===-- XCoreTargetInfo.cpp - XCore Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/XCoreTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheXCoreTarget() {
  static Target TheXCoreTarget;
  return TheXCoreTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXCoreTargetInfo() {
  RegisterTarget<Triple::xcore> X(getTheXCoreTarget(), "xcore", "XCore",
                                  "XCore");
}
