//===- BPFLegalizerInfo.h ----------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the Machinelegalizer class for BPF
//===----------------------------------------------------------------------===//

#include "BPFLegalizerInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "bpf-legalinfo"

using namespace llvm;
using namespace LegalizeActions;

BPFLegalizerInfo::BPFLegalizerInfo(const BPFSubtarget &ST) {
  getLegacyLegalizerInfo().computeTables();
}
