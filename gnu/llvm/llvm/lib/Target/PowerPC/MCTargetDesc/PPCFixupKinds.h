//===-- PPCFixupKinds.h - PPC Specific Fixup Entries ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCFIXUPKINDS_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

#undef PPC

namespace llvm {
namespace PPC {
enum Fixups {
  // 24-bit PC relative relocation for direct branches like 'b' and 'bl'.
  fixup_ppc_br24 = FirstTargetFixupKind,

  // 24-bit PC relative relocation for direct branches like 'b' and 'bl' where
  // the caller does not use the TOC.
  fixup_ppc_br24_notoc,

  /// 14-bit PC relative relocation for conditional branches.
  fixup_ppc_brcond14,

  /// 24-bit absolute relocation for direct branches like 'ba' and 'bla'.
  fixup_ppc_br24abs,

  /// 14-bit absolute relocation for conditional branches.
  fixup_ppc_brcond14abs,

  /// A 16-bit fixup corresponding to lo16(_foo) or ha16(_foo) for instrs like
  /// 'li' or 'addis'.
  fixup_ppc_half16,

  /// A 14-bit fixup corresponding to lo16(_foo) with implied 2 zero bits for
  /// instrs like 'std'.
  fixup_ppc_half16ds,

  // A 34-bit fixup corresponding to PC-relative paddi.
  fixup_ppc_pcrel34,

  // A 34-bit fixup corresponding to Non-PC-relative paddi.
  fixup_ppc_imm34,

  /// Not a true fixup, but ties a symbol to a call to __tls_get_addr for the
  /// TLS general and local dynamic models, or inserts the thread-pointer
  /// register number. It can also be used to tie the ref symbol to prevent it
  /// from being garbage collected on AIX.
  fixup_ppc_nofixup,

  /// A 16-bit fixup corresponding to lo16(_foo) with implied 3 zero bits for
  /// instrs like 'lxv'. Produces the same relocation as fixup_ppc_half16ds.
  fixup_ppc_half16dq,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
}
}

#endif
