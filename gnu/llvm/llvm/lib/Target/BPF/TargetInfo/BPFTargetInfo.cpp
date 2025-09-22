//===-- BPFTargetInfo.cpp - BPF Target Implementation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/BPFTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheBPFleTarget() {
  static Target TheBPFleTarget;
  return TheBPFleTarget;
}
Target &llvm::getTheBPFbeTarget() {
  static Target TheBPFbeTarget;
  return TheBPFbeTarget;
}
Target &llvm::getTheBPFTarget() {
  static Target TheBPFTarget;
  return TheBPFTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeBPFTargetInfo() {
  TargetRegistry::RegisterTarget(getTheBPFTarget(), "bpf", "BPF (host endian)",
                                 "BPF", [](Triple::ArchType) { return false; },
                                 true);
  RegisterTarget<Triple::bpfel, /*HasJIT=*/true> X(
      getTheBPFleTarget(), "bpfel", "BPF (little endian)", "BPF");
  RegisterTarget<Triple::bpfeb, /*HasJIT=*/true> Y(getTheBPFbeTarget(), "bpfeb",
                                                   "BPF (big endian)", "BPF");
}
