//===-- WebAssemblySubtarget.cpp - WebAssembly Subtarget Information ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the WebAssembly-specific subclass of
/// TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblySubtarget.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssemblyInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "WebAssemblyGenSubtargetInfo.inc"

WebAssemblySubtarget &
WebAssemblySubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                      StringRef FS) {
  // Determine default and user-specified characteristics
  LLVM_DEBUG(llvm::dbgs() << "initializeSubtargetDependencies\n");

  if (CPU.empty())
    CPU = "generic";

  ParseSubtargetFeatures(CPU, /*TuneCPU*/ CPU, FS);
  return *this;
}

WebAssemblySubtarget::WebAssemblySubtarget(const Triple &TT,
                                           const std::string &CPU,
                                           const std::string &FS,
                                           const TargetMachine &TM)
    : WebAssemblyGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS),
      TargetTriple(TT), InstrInfo(initializeSubtargetDependencies(CPU, FS)),
      TLInfo(TM, *this) {}

bool WebAssemblySubtarget::enableAtomicExpand() const {
  // If atomics are disabled, atomic ops are lowered instead of expanded
  return hasAtomics();
}

bool WebAssemblySubtarget::enableMachineScheduler() const {
  // Disable the MachineScheduler for now. Even with ShouldTrackPressure set and
  // enableMachineSchedDefaultSched overridden, it appears to have an overall
  // negative effect for the kinds of register optimizations we're doing.
  return false;
}

bool WebAssemblySubtarget::useAA() const { return true; }
