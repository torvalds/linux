//===-- X86FixupKinds.h - X86 Specific Fixup Entries ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_MCTARGETDESC_X86FIXUPKINDS_H
#define LLVM_LIB_TARGET_X86_MCTARGETDESC_X86FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace X86 {
enum Fixups {
  reloc_riprel_4byte = FirstTargetFixupKind, // 32-bit rip-relative
  reloc_riprel_4byte_movq_load,              // 32-bit rip-relative in movq
  reloc_riprel_4byte_relax,                  // 32-bit rip-relative in relaxable
                                             // instruction
  reloc_riprel_4byte_relax_rex,              // 32-bit rip-relative in relaxable
                                             // instruction with rex prefix
  reloc_signed_4byte,                        // 32-bit signed. Unlike FK_Data_4
                                             // this will be sign extended at
                                             // runtime.
  reloc_signed_4byte_relax,                  // like reloc_signed_4byte, but
                                             // in a relaxable instruction.
  reloc_global_offset_table,                 // 32-bit, relative to the start
                                             // of the instruction. Used only
                                             // for _GLOBAL_OFFSET_TABLE_.
  reloc_global_offset_table8,                // 64-bit variant.
  reloc_branch_4byte_pcrel,                  // 32-bit PC relative branch.
  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
}
}

#endif
