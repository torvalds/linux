//===-- llvm/MC/MCSymbolTableEntry.h - Symbol table entry -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOLTABLEENTRY_H
#define LLVM_MC_MCSYMBOLTABLEENTRY_H

#include "llvm/ADT/StringMapEntry.h"

namespace llvm {

class MCSymbol;

/// The value for an entry in the symbol table of an MCContext.
///
/// This is in a separate file, because MCSymbol uses MCSymbolTableEntry (see
/// below) to reuse the name that is stored in the symbol table.
struct MCSymbolTableValue {
  /// The symbol associated with the name, if any.
  MCSymbol *Symbol = nullptr;

  /// The next ID to dole out to an unnamed assembler temporary symbol with
  /// the prefix (symbol table key).
  unsigned NextUniqueID = 0;

  /// Whether the name associated with this value is used for a symbol. This is
  /// not necessarily true: sometimes, we use a symbol table value without an
  /// associated symbol for accessing NextUniqueID when a suffix is added to a
  /// name. However, Used might be true even if Symbol is nullptr: temporary
  /// named symbols are not added to the symbol table.
  bool Used = false;
};

/// MCContext stores MCSymbolTableValue in a string map (see MCSymbol::operator
/// new). To avoid redundant storage of the name, MCSymbol stores a pointer (8
/// bytes -- half the size of a StringRef) to the entry to access it.
using MCSymbolTableEntry = StringMapEntry<MCSymbolTableValue>;

} // end namespace llvm

#endif
