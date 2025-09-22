//===-- ARMUnwindOpAsm.h - ARM Unwind Opcodes Assembler ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the unwind opcode assembler for ARM exception handling
// table.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMUNWINDOPASM_H
#define LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMUNWINDOPASM_H

#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>

namespace llvm {

class MCSymbol;

class UnwindOpcodeAssembler {
private:
  SmallVector<uint8_t, 32> Ops;
  SmallVector<unsigned, 8> OpBegins;
  bool HasPersonality = false;

public:
  UnwindOpcodeAssembler() {
    OpBegins.push_back(0);
  }

  /// Reset the unwind opcode assembler.
  void Reset() {
    Ops.clear();
    OpBegins.clear();
    OpBegins.push_back(0);
    HasPersonality = false;
  }

  /// Set the personality
  void setPersonality(const MCSymbol *Per) {
    HasPersonality = true;
  }

  /// Emit unwind opcodes for .save directives
  void EmitRegSave(uint32_t RegSave);

  /// Emit unwind opcodes for .vsave directives
  void EmitVFPRegSave(uint32_t VFPRegSave);

  /// Emit unwind opcodes to copy address from source register to $sp.
  void EmitSetSP(uint16_t Reg);

  /// Emit unwind opcodes to add $sp with an offset.
  void EmitSPOffset(int64_t Offset);

  /// Emit unwind raw opcodes
  void EmitRaw(const SmallVectorImpl<uint8_t> &Opcodes) {
    Ops.insert(Ops.end(), Opcodes.begin(), Opcodes.end());
    OpBegins.push_back(OpBegins.back() + Opcodes.size());
  }

  /// Finalize the unwind opcode sequence for emitBytes()
  void Finalize(unsigned &PersonalityIndex,
                SmallVectorImpl<uint8_t> &Result);

private:
  void EmitInt8(unsigned Opcode) {
    Ops.push_back(Opcode & 0xff);
    OpBegins.push_back(OpBegins.back() + 1);
  }

  void EmitInt16(unsigned Opcode) {
    Ops.push_back((Opcode >> 8) & 0xff);
    Ops.push_back(Opcode & 0xff);
    OpBegins.push_back(OpBegins.back() + 2);
  }

  void emitBytes(const uint8_t *Opcode, size_t Size) {
    Ops.insert(Ops.end(), Opcode, Opcode + Size);
    OpBegins.push_back(OpBegins.back() + Size);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMUNWINDOPASM_H
