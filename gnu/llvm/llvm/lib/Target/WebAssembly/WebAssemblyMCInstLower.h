//===-- WebAssemblyMCInstLower.h - Lower MachineInstr to MCInst -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the class to lower WebAssembly MachineInstrs to
/// their corresponding MCInst records.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYMCINSTLOWER_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYMCINSTLOWER_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class WebAssemblyAsmPrinter;
class MCContext;
class MCSymbol;
class MachineInstr;
class MachineOperand;

/// This class is used to lower an MachineInstr into an MCInst.
class LLVM_LIBRARY_VISIBILITY WebAssemblyMCInstLower {
  MCContext &Ctx;
  WebAssemblyAsmPrinter &Printer;

  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
  MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;
  MCOperand lowerTypeIndexOperand(SmallVectorImpl<wasm::ValType> &&,
                                  SmallVectorImpl<wasm::ValType> &&) const;

public:
  WebAssemblyMCInstLower(MCContext &ctx, WebAssemblyAsmPrinter &printer)
      : Ctx(ctx), Printer(printer) {}
  void lower(const MachineInstr *MI, MCInst &OutMI) const;
};
} // end namespace llvm

#endif
