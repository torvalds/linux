//===-- X86InstrFoldTables.h - X86 Instruction Folding Tables ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the interface to query the X86 memory folding tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86INSTRFOLDTABLES_H
#define LLVM_LIB_TARGET_X86_X86INSTRFOLDTABLES_H

#include <cstdint>
#include "llvm/Support/X86FoldTablesUtils.h"

namespace llvm {

// This struct is used for both the folding and unfold tables. They KeyOp
// is used to determine the sorting order.
struct X86FoldTableEntry {
  unsigned KeyOp;
  unsigned DstOp;
  uint16_t Flags;

  bool operator<(const X86FoldTableEntry &RHS) const {
    return KeyOp < RHS.KeyOp;
  }
  bool operator==(const X86FoldTableEntry &RHS) const {
    return KeyOp == RHS.KeyOp;
  }
  friend bool operator<(const X86FoldTableEntry &TE, unsigned Opcode) {
    return TE.KeyOp < Opcode;
  }
};

// Look up the memory folding table entry for folding a load and a store into
// operand 0.
const X86FoldTableEntry *lookupTwoAddrFoldTable(unsigned RegOp);

// Look up the memory folding table entry for folding a load or store with
// operand OpNum.
const X86FoldTableEntry *lookupFoldTable(unsigned RegOp, unsigned OpNum);

// Look up the broadcast folding table entry for folding a broadcast with
// operand OpNum.
const X86FoldTableEntry *lookupBroadcastFoldTable(unsigned RegOp,
                                                  unsigned OpNum);

// Look up the memory unfolding table entry for this instruction.
const X86FoldTableEntry *lookupUnfoldTable(unsigned MemOp);

// Look up the broadcast folding table entry for this instruction from
// the regular memory instruction.
const X86FoldTableEntry *lookupBroadcastFoldTableBySize(unsigned MemOp,
                                                        unsigned BroadcastBits);

bool matchBroadcastSize(const X86FoldTableEntry &Entry, unsigned BroadcastBits);
} // namespace llvm

#endif
