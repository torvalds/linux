//===-- llvm/MC/MCExternalSymbolizer.h - ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCExternalSymbolizer class, which
// enables library users to provide callbacks (through the C API) to do the
// symbolization externally.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCEXTERNALSYMBOLIZER_H
#define LLVM_MC_MCDISASSEMBLER_MCEXTERNALSYMBOLIZER_H

#include "llvm-c/DisassemblerTypes.h"
#include "llvm/MC/MCDisassembler/MCSymbolizer.h"
#include <memory>

namespace llvm {

/// Symbolize using user-provided, C API, callbacks.
///
/// See llvm-c/Disassembler.h.
class MCExternalSymbolizer : public MCSymbolizer {
protected:
  /// \name Hooks for symbolic disassembly via the public 'C' interface.
  /// @{
  /// The function to get the symbolic information for operands.
  LLVMOpInfoCallback GetOpInfo;
  /// The function to lookup a symbol name.
  LLVMSymbolLookupCallback SymbolLookUp;
  /// The pointer to the block of symbolic information for above call back.
  void *DisInfo;
  /// @}

public:
  MCExternalSymbolizer(MCContext &Ctx,
                       std::unique_ptr<MCRelocationInfo> RelInfo,
                       LLVMOpInfoCallback getOpInfo,
                       LLVMSymbolLookupCallback symbolLookUp, void *disInfo)
    : MCSymbolizer(Ctx, std::move(RelInfo)), GetOpInfo(getOpInfo),
      SymbolLookUp(symbolLookUp), DisInfo(disInfo) {}

  bool tryAddingSymbolicOperand(MCInst &MI, raw_ostream &CommentStream,
                                int64_t Value, uint64_t Address, bool IsBranch,
                                uint64_t Offset, uint64_t OpSize,
                                uint64_t InstSize) override;
  void tryAddingPcLoadReferenceComment(raw_ostream &CommentStream,
                                       int64_t Value,
                                       uint64_t Address) override;
};

}

#endif
