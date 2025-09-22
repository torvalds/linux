//===- AArch64ExternalSymbolizer.h - Symbolizer for AArch64 -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Symbolize AArch64 assembly code during disassembly using callbacks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_DISASSEMBLER_AARCH64EXTERNALSYMBOLIZER_H
#define LLVM_LIB_TARGET_AARCH64_DISASSEMBLER_AARCH64EXTERNALSYMBOLIZER_H

#include "llvm/MC/MCDisassembler/MCExternalSymbolizer.h"

namespace llvm {

class AArch64ExternalSymbolizer : public MCExternalSymbolizer {
public:
  AArch64ExternalSymbolizer(MCContext &Ctx,
                            std::unique_ptr<MCRelocationInfo> RelInfo,
                            LLVMOpInfoCallback GetOpInfo,
                            LLVMSymbolLookupCallback SymbolLookUp,
                            void *DisInfo)
      : MCExternalSymbolizer(Ctx, std::move(RelInfo), GetOpInfo, SymbolLookUp,
                             DisInfo) {}

  bool tryAddingSymbolicOperand(MCInst &MI, raw_ostream &CommentStream,
                                int64_t Value, uint64_t Address, bool IsBranch,
                                uint64_t Offset, uint64_t OpSize,
                                uint64_t InstSize) override;
};

} // namespace llvm

#endif
