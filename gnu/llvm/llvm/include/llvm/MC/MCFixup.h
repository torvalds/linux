//===-- llvm/MC/MCFixup.h - Instruction Relocation and Patching -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCFIXUP_H
#define LLVM_MC_MCFIXUP_H

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>

namespace llvm {
class MCExpr;

/// Extensible enumeration to represent the type of a fixup.
enum MCFixupKind {
  FK_NONE = 0,    ///< A no-op fixup.
  FK_Data_1,      ///< A one-byte fixup.
  FK_Data_2,      ///< A two-byte fixup.
  FK_Data_4,      ///< A four-byte fixup.
  FK_Data_8,      ///< A eight-byte fixup.
  FK_Data_leb128, ///< A leb128 fixup.
  FK_PCRel_1,     ///< A one-byte pc relative fixup.
  FK_PCRel_2,     ///< A two-byte pc relative fixup.
  FK_PCRel_4,     ///< A four-byte pc relative fixup.
  FK_PCRel_8,     ///< A eight-byte pc relative fixup.
  FK_GPRel_1,     ///< A one-byte gp relative fixup.
  FK_GPRel_2,     ///< A two-byte gp relative fixup.
  FK_GPRel_4,     ///< A four-byte gp relative fixup.
  FK_GPRel_8,     ///< A eight-byte gp relative fixup.
  FK_DTPRel_4,    ///< A four-byte dtp relative fixup.
  FK_DTPRel_8,    ///< A eight-byte dtp relative fixup.
  FK_TPRel_4,     ///< A four-byte tp relative fixup.
  FK_TPRel_8,     ///< A eight-byte tp relative fixup.
  FK_SecRel_1,    ///< A one-byte section relative fixup.
  FK_SecRel_2,    ///< A two-byte section relative fixup.
  FK_SecRel_4,    ///< A four-byte section relative fixup.
  FK_SecRel_8,    ///< A eight-byte section relative fixup.

  FirstTargetFixupKind = 128,

  /// The range [FirstLiteralRelocationKind, MaxTargetFixupKind) is used for
  /// relocations coming from .reloc directive. Fixup kind
  /// FirstLiteralRelocationKind+V represents the relocation type with number V.
  FirstLiteralRelocationKind = 256,

  /// Set limit to accommodate the highest reloc type in use for all Targets,
  /// currently R_AARCH64_IRELATIVE at 1032, including room for expansion.
  MaxFixupKind = FirstLiteralRelocationKind + 1032 + 32,
};

/// Encode information on a single operation to perform on a byte
/// sequence (e.g., an encoded instruction) which requires assemble- or run-
/// time patching.
///
/// Fixups are used any time the target instruction encoder needs to represent
/// some value in an instruction which is not yet concrete. The encoder will
/// encode the instruction assuming the value is 0, and emit a fixup which
/// communicates to the assembler backend how it should rewrite the encoded
/// value.
///
/// During the process of relaxation, the assembler will apply fixups as
/// symbolic values become concrete. When relaxation is complete, any remaining
/// fixups become relocations in the object file (or errors, if the fixup cannot
/// be encoded on the target).
class MCFixup {
  /// The value to put into the fixup location. The exact interpretation of the
  /// expression is target dependent, usually it will be one of the operands to
  /// an instruction or an assembler directive.
  const MCExpr *Value = nullptr;

  /// The byte index of start of the relocation inside the MCFragment.
  uint32_t Offset = 0;

  /// The target dependent kind of fixup item this is. The kind is used to
  /// determine how the operand value should be encoded into the instruction.
  MCFixupKind Kind = FK_NONE;

  /// The source location which gave rise to the fixup, if any.
  SMLoc Loc;
public:
  static MCFixup create(uint32_t Offset, const MCExpr *Value,
                        MCFixupKind Kind, SMLoc Loc = SMLoc()) {
    assert(Kind <= MaxFixupKind && "Kind out of range!");
    MCFixup FI;
    FI.Value = Value;
    FI.Offset = Offset;
    FI.Kind = Kind;
    FI.Loc = Loc;
    return FI;
  }

  MCFixupKind getKind() const { return Kind; }

  unsigned getTargetKind() const { return Kind; }

  uint32_t getOffset() const { return Offset; }
  void setOffset(uint32_t Value) { Offset = Value; }

  const MCExpr *getValue() const { return Value; }

  /// Return the generic fixup kind for a value with the given size. It
  /// is an error to pass an unsupported size.
  static MCFixupKind getKindForSize(unsigned Size, bool IsPCRel) {
    switch (Size) {
    default: llvm_unreachable("Invalid generic fixup size!");
    case 1:
      return IsPCRel ? FK_PCRel_1 : FK_Data_1;
    case 2:
      return IsPCRel ? FK_PCRel_2 : FK_Data_2;
    case 4:
      return IsPCRel ? FK_PCRel_4 : FK_Data_4;
    case 8:
      return IsPCRel ? FK_PCRel_8 : FK_Data_8;
    }
  }

  SMLoc getLoc() const { return Loc; }
};

} // End llvm namespace

#endif
