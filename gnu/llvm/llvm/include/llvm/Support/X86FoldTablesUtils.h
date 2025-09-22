//===-- X86FoldTablesUtils.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_X86FOLDTABLESUTILS_H
#define LLVM_SUPPORT_X86FOLDTABLESUTILS_H

namespace llvm {
enum {
  // Select which memory operand is being unfolded.
  // (stored in bits 0 - 2)
  TB_INDEX_0 = 0,
  TB_INDEX_1 = 1,
  TB_INDEX_2 = 2,
  TB_INDEX_3 = 3,
  TB_INDEX_4 = 4,
  TB_INDEX_MASK = 0x7,

  // Do not insert the reverse map (MemOp -> RegOp) into the table.
  // This may be needed because there is a many -> one mapping.
  TB_NO_REVERSE = 1 << 3,

  // Do not insert the forward map (RegOp -> MemOp) into the table.
  // This is needed for Native Client, which prohibits branch
  // instructions from using a memory operand.
  TB_NO_FORWARD = 1 << 4,

  TB_FOLDED_LOAD = 1 << 5,
  TB_FOLDED_STORE = 1 << 6,

  // Minimum alignment required for load/store.
  // Used for RegOp->MemOp conversion. Encoded as Log2(Align)
  // (stored in bits 8 - 10)
  TB_ALIGN_SHIFT = 7,
  TB_ALIGN_1 = 0 << TB_ALIGN_SHIFT,
  TB_ALIGN_16 = 4 << TB_ALIGN_SHIFT,
  TB_ALIGN_32 = 5 << TB_ALIGN_SHIFT,
  TB_ALIGN_64 = 6 << TB_ALIGN_SHIFT,
  TB_ALIGN_MASK = 0x7 << TB_ALIGN_SHIFT,

  // Broadcast type.
  // (stored in bits 11 - 13)
  TB_BCAST_TYPE_SHIFT = TB_ALIGN_SHIFT + 3,
  TB_BCAST_W = 1 << TB_BCAST_TYPE_SHIFT,
  TB_BCAST_D = 2 << TB_BCAST_TYPE_SHIFT,
  TB_BCAST_Q = 3 << TB_BCAST_TYPE_SHIFT,
  TB_BCAST_SS = 4 << TB_BCAST_TYPE_SHIFT,
  TB_BCAST_SD = 5 << TB_BCAST_TYPE_SHIFT,
  TB_BCAST_SH = 6 << TB_BCAST_TYPE_SHIFT,
  TB_BCAST_MASK = 0x7 << TB_BCAST_TYPE_SHIFT,

  // Unused bits 14-16
};
} // namespace llvm
#endif // LLVM_SUPPORT_X86FOLDTABLESUTILS_H
