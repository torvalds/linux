//===- llvm/CodeGen/AddressPool.h - Dwarf Debug Framework -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_ADDRESSPOOL_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_ADDRESSPOOL_H

#include "llvm/ADT/DenseMap.h"

namespace llvm {

class AsmPrinter;
class MCSection;
class MCSymbol;

// Collection of addresses for this unit and assorted labels.
// A Symbol->unsigned mapping of addresses used by indirect
// references.
class AddressPool {
  struct AddressPoolEntry {
    unsigned Number;
    bool TLS;

    AddressPoolEntry(unsigned Number, bool TLS) : Number(Number), TLS(TLS) {}
  };
  DenseMap<const MCSymbol *, AddressPoolEntry> Pool;

  /// Record whether the AddressPool has been queried for an address index since
  /// the last "resetUsedFlag" call. Used to implement type unit fallback - a
  /// type that references addresses cannot be placed in a type unit when using
  /// fission.
  bool HasBeenUsed = false;

public:
  AddressPool() = default;

  /// Returns the index into the address pool with the given
  /// label/symbol.
  unsigned getIndex(const MCSymbol *Sym, bool TLS = false);

  void emit(AsmPrinter &Asm, MCSection *AddrSection);

  bool isEmpty() { return Pool.empty(); }

  bool hasBeenUsed() const { return HasBeenUsed; }

  void resetUsedFlag(bool HasBeenUsed = false) { this->HasBeenUsed = HasBeenUsed; }

  MCSymbol *getLabel() { return AddressTableBaseSym; }
  void setLabel(MCSymbol *Sym) { AddressTableBaseSym = Sym; }

private:
  MCSymbol *emitHeader(AsmPrinter &Asm, MCSection *Section);

  /// Symbol designates the start of the contribution to the address table.
  MCSymbol *AddressTableBaseSym = nullptr;
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_ASMPRINTER_ADDRESSPOOL_H
