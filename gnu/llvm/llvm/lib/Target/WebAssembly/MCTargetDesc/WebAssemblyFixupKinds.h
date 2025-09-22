//=- WebAssemblyFixupKinds.h - WebAssembly Specific Fixup Entries -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYFIXUPKINDS_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace WebAssembly {
enum Fixups {
  fixup_sleb128_i32 = FirstTargetFixupKind, // 32-bit signed
  fixup_sleb128_i64,                        // 64-bit signed
  fixup_uleb128_i32,                        // 32-bit unsigned
  fixup_uleb128_i64,                        // 64-bit unsigned

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
} // end namespace WebAssembly
} // end namespace llvm

#endif
