//===-- NVPTXTargetInfo.cpp - NVPTX Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/NVPTXTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheNVPTXTarget32() {
  static Target TheNVPTXTarget32;
  return TheNVPTXTarget32;
}
Target &llvm::getTheNVPTXTarget64() {
  static Target TheNVPTXTarget64;
  return TheNVPTXTarget64;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeNVPTXTargetInfo() {
  RegisterTarget<Triple::nvptx> X(getTheNVPTXTarget32(), "nvptx",
                                  "NVIDIA PTX 32-bit", "NVPTX");
  RegisterTarget<Triple::nvptx64> Y(getTheNVPTXTarget64(), "nvptx64",
                                    "NVIDIA PTX 64-bit", "NVPTX");
}
