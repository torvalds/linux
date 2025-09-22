//===- MCDisassembler.cpp - Disassembler interface ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/ADT/ArrayRef.h"

using namespace llvm;

MCDisassembler::~MCDisassembler() = default;

Expected<bool> MCDisassembler::onSymbolStart(SymbolInfoTy &Symbol,
                                             uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address) const {
  return false;
}

uint64_t MCDisassembler::suggestBytesToSkip(ArrayRef<uint8_t> Bytes,
                                            uint64_t Address) const {
  return 1;
}

bool MCDisassembler::tryAddingSymbolicOperand(MCInst &Inst, int64_t Value,
                                              uint64_t Address, bool IsBranch,
                                              uint64_t Offset, uint64_t OpSize,
                                              uint64_t InstSize) const {
  if (Symbolizer)
    return Symbolizer->tryAddingSymbolicOperand(Inst, *CommentStream, Value,
                                                Address, IsBranch, Offset,
                                                OpSize, InstSize);
  return false;
}

void MCDisassembler::tryAddingPcLoadReferenceComment(int64_t Value,
                                                     uint64_t Address) const {
  if (Symbolizer)
    Symbolizer->tryAddingPcLoadReferenceComment(*CommentStream, Value, Address);
}

void MCDisassembler::setSymbolizer(std::unique_ptr<MCSymbolizer> Symzer) {
  Symbolizer = std::move(Symzer);
}

#define SMC_PCASE(A, P)                                                         \
  case XCOFF::XMC_##A:                                                         \
    return P;

static uint8_t getSMCPriority(XCOFF::StorageMappingClass SMC) {
  switch (SMC) {
    SMC_PCASE(PR, 1)
    SMC_PCASE(RO, 1)
    SMC_PCASE(DB, 1)
    SMC_PCASE(GL, 1)
    SMC_PCASE(XO, 1)
    SMC_PCASE(SV, 1)
    SMC_PCASE(SV64, 1)
    SMC_PCASE(SV3264, 1)
    SMC_PCASE(TI, 1)
    SMC_PCASE(TB, 1)
    SMC_PCASE(RW, 1)
    SMC_PCASE(TC0, 0)
    SMC_PCASE(TC, 1)
    SMC_PCASE(TD, 1)
    SMC_PCASE(DS, 1)
    SMC_PCASE(UA, 1)
    SMC_PCASE(BS, 1)
    SMC_PCASE(UC, 1)
    SMC_PCASE(TL, 1)
    SMC_PCASE(UL, 1)
    SMC_PCASE(TE, 1)
#undef SMC_PCASE
  }
  return 0;
}

/// The function is for symbol sorting when symbols have the same address.
/// The symbols in the same section are sorted in ascending order.
/// llvm-objdump -D will choose the highest priority symbol to display when
/// there are symbols with the same address.
bool XCOFFSymbolInfoTy::operator<(const XCOFFSymbolInfoTy &SymInfo) const {
  // Label symbols have higher priority than non-label symbols.
  if (IsLabel != SymInfo.IsLabel)
    return SymInfo.IsLabel;

  // Symbols with a StorageMappingClass have higher priority than those without.
  if (StorageMappingClass.has_value() !=
      SymInfo.StorageMappingClass.has_value())
    return SymInfo.StorageMappingClass.has_value();

  if (StorageMappingClass) {
    return getSMCPriority(*StorageMappingClass) <
           getSMCPriority(*SymInfo.StorageMappingClass);
  }

  return false;
}
