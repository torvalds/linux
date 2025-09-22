//===-- R600MCTargetDesc.cpp - R600 Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief This file provides R600 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "R600MCTargetDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/TargetParser/SubtargetFeature.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "R600GenInstrInfo.inc"

MCInstrInfo *llvm::createR600MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitR600MCInstrInfo(X);
  return X;
}
