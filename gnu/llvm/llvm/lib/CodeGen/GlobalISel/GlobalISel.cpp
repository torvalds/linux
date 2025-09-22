//===-- llvm/CodeGen/GlobalISel/GlobalIsel.cpp --- GlobalISel ----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
// This file implements the common initialization routines for the
// GlobalISel library.
//===----------------------------------------------------------------------===//

#include "llvm/InitializePasses.h"

using namespace llvm;

void llvm::initializeGlobalISel(PassRegistry &Registry) {
  initializeIRTranslatorPass(Registry);
  initializeLegalizerPass(Registry);
  initializeLoadStoreOptPass(Registry);
  initializeLocalizerPass(Registry);
  initializeRegBankSelectPass(Registry);
  initializeInstructionSelectPass(Registry);
}
