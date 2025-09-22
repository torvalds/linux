//===- PPCLegalizerInfo.h ----------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the Machinelegalizer class for PowerPC
//===----------------------------------------------------------------------===//

#include "PPCLegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "ppc-legalinfo"

using namespace llvm;
using namespace LegalizeActions;
using namespace LegalizeMutations;
using namespace LegalityPredicates;

static LegalityPredicate isRegisterType(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    unsigned TypeSize = QueryTy.getSizeInBits();

    if (TypeSize % 32 == 1 || TypeSize > 128)
      return false;

    // Check if this is a legal PowerPC vector type.
    if (QueryTy.isVector()) {
      const int EltSize = QueryTy.getElementType().getSizeInBits();
      return (EltSize == 8 || EltSize == 16 || EltSize == 32 || EltSize == 64);
    }

    return true;
  };
}

PPCLegalizerInfo::PPCLegalizerInfo(const PPCSubtarget &ST) {
  using namespace TargetOpcode;
  const LLT P0 = LLT::pointer(0, 64);
  const LLT S1 = LLT::scalar(1);
  const LLT S8 = LLT::scalar(8);
  const LLT S16 = LLT::scalar(16);
  const LLT S32 = LLT::scalar(32);
  const LLT S64 = LLT::scalar(64);
  const LLT V16S8 = LLT::fixed_vector(16, 8);
  const LLT V8S16 = LLT::fixed_vector(8, 16);
  const LLT V4S32 = LLT::fixed_vector(4, 32);
  const LLT V2S64 = LLT::fixed_vector(2, 64);
  getActionDefinitionsBuilder(G_IMPLICIT_DEF).legalFor({S64});
  getActionDefinitionsBuilder(G_CONSTANT)
      .legalFor({S32, S64})
      .clampScalar(0, S64, S64);
  getActionDefinitionsBuilder({G_ZEXT, G_SEXT, G_ANYEXT})
      .legalForCartesianProduct({S64}, {S1, S8, S16, S32})
      .clampScalar(0, S64, S64);
  getActionDefinitionsBuilder({G_AND, G_OR, G_XOR})
      .legalFor({S64, V4S32})
      .clampScalar(0, S64, S64)
      .bitcastIf(typeIsNot(0, V4S32), changeTo(0, V4S32));
  getActionDefinitionsBuilder({G_ADD, G_SUB})
      .legalFor({S64, V16S8, V8S16, V4S32, V2S64})
      .clampScalar(0, S64, S64);
  getActionDefinitionsBuilder(G_BITCAST)
      .legalIf(all(isRegisterType(0), isRegisterType(1)))
      .lower();

  getActionDefinitionsBuilder({G_FADD, G_FSUB, G_FMUL, G_FDIV})
      .legalFor({S32, S64, V4S32, V2S64});

  getActionDefinitionsBuilder(G_FCMP).legalForCartesianProduct({S1},
                                                               {S32, S64});

  getActionDefinitionsBuilder({G_FPTOSI, G_FPTOUI})
      .legalForCartesianProduct({S64}, {S32, S64});

  getActionDefinitionsBuilder({G_SITOFP, G_UITOFP})
      .legalForCartesianProduct({S32, S64}, {S64});

  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalForTypesWithMemDesc({{S64, P0, S64, 8}, {S32, P0, S32, 4}});

  getActionDefinitionsBuilder(G_FCONSTANT).lowerFor({S32, S64});
  getActionDefinitionsBuilder(G_CONSTANT_POOL).legalFor({P0});

  getLegacyLegalizerInfo().computeTables();
}
