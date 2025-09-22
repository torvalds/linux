//===-- WebAssemblyTargetObjectFile.cpp - WebAssembly Object Info ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the functions of the WebAssembly-specific subclass
/// of TargetLoweringObjectFile.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetObjectFile.h"
#include "WebAssemblyTargetMachine.h"

using namespace llvm;

void WebAssemblyTargetObjectFile::Initialize(MCContext &Ctx,
                                             const TargetMachine &TM) {
  TargetLoweringObjectFileWasm::Initialize(Ctx, TM);
  InitializeWasm();
}
