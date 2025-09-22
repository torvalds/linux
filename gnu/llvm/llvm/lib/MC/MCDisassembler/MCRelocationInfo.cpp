//===-- MCRelocationInfo.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm-c/DisassemblerTypes.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

MCRelocationInfo::MCRelocationInfo(MCContext &Ctx) : Ctx(Ctx) {}

MCRelocationInfo::~MCRelocationInfo() = default;

const MCExpr *
MCRelocationInfo::createExprForCAPIVariantKind(const MCExpr *SubExpr,
                                               unsigned VariantKind) {
  if (VariantKind != LLVMDisassembler_VariantKind_None)
    return nullptr;
  return SubExpr;
}

MCRelocationInfo *llvm::createMCRelocationInfo(const Triple &TT,
                                               MCContext &Ctx) {
  return new MCRelocationInfo(Ctx);
}
