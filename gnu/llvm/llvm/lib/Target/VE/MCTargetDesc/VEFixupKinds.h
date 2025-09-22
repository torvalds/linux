//===-- VEFixupKinds.h - VE Specific Fixup Entries --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_MCTARGETDESC_VEFIXUPKINDS_H
#define LLVM_LIB_TARGET_VE_MCTARGETDESC_VEFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace VE {
enum Fixups {
  /// fixup_ve_reflong - 32-bit fixup corresponding to foo
  fixup_ve_reflong = FirstTargetFixupKind,

  /// fixup_ve_srel32 - 32-bit fixup corresponding to foo for relative branch
  fixup_ve_srel32,

  /// fixup_ve_hi32 - 32-bit fixup corresponding to foo\@hi
  fixup_ve_hi32,

  /// fixup_ve_lo32 - 32-bit fixup corresponding to foo\@lo
  fixup_ve_lo32,

  /// fixup_ve_pc_hi32 - 32-bit fixup corresponding to foo\@pc_hi
  fixup_ve_pc_hi32,

  /// fixup_ve_pc_lo32 - 32-bit fixup corresponding to foo\@pc_lo
  fixup_ve_pc_lo32,

  /// fixup_ve_got_hi32 - 32-bit fixup corresponding to foo\@got_hi
  fixup_ve_got_hi32,

  /// fixup_ve_got_lo32 - 32-bit fixup corresponding to foo\@got_lo
  fixup_ve_got_lo32,

  /// fixup_ve_gotoff_hi32 - 32-bit fixup corresponding to foo\@gotoff_hi
  fixup_ve_gotoff_hi32,

  /// fixup_ve_gotoff_lo32 - 32-bit fixup corresponding to foo\@gotoff_lo
  fixup_ve_gotoff_lo32,

  /// fixup_ve_plt_hi32/lo32
  fixup_ve_plt_hi32,
  fixup_ve_plt_lo32,

  /// fixups for Thread Local Storage
  fixup_ve_tls_gd_hi32,
  fixup_ve_tls_gd_lo32,
  fixup_ve_tpoff_hi32,
  fixup_ve_tpoff_lo32,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
} // namespace VE
} // namespace llvm

#endif
