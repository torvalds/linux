//===-- llvm/MC/MCFixupKindInfo.h - Fixup Descriptors -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCFIXUPKINDINFO_H
#define LLVM_MC_MCFIXUPKINDINFO_H

namespace llvm {

/// Target independent information on a fixup kind.
struct MCFixupKindInfo {
  enum FixupKindFlags {
    /// Is this fixup kind PCrelative? This is used by the assembler backend to
    /// evaluate fixup values in a target independent manner when possible.
    FKF_IsPCRel = (1 << 0),

    /// Should this fixup kind force a 4-byte aligned effective PC value?
    FKF_IsAlignedDownTo32Bits = (1 << 1),

    /// Should this fixup be evaluated in a target dependent manner?
    FKF_IsTarget = (1 << 2),

    /// This fixup kind should be resolved if defined.
    /// FIXME This is a workaround because we don't support certain ARM
    /// relocation types. This flag should eventually be removed.
    FKF_Constant = 1 << 3,
  };

  /// A target specific name for the fixup kind. The names will be unique for
  /// distinct kinds on any given target.
  const char *Name;

  /// The bit offset to write the relocation into.
  unsigned TargetOffset;

  /// The number of bits written by this fixup. The bits are assumed to be
  /// contiguous.
  unsigned TargetSize;

  /// Flags describing additional information on this fixup kind.
  unsigned Flags;
};

} // End llvm namespace

#endif
