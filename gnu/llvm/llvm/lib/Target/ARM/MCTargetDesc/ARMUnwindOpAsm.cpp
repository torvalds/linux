//===-- ARMUnwindOpAsm.cpp - ARM Unwind Opcodes Assembler -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the unwind opcode assembler for ARM exception handling
// table.
//
//===----------------------------------------------------------------------===//

#include "ARMUnwindOpAsm.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/ARMEHABI.h"
#include "llvm/Support/LEB128.h"
#include <cassert>

using namespace llvm;

namespace {

  /// UnwindOpcodeStreamer - The simple wrapper over SmallVector to emit bytes
  /// with MSB to LSB per uint32_t ordering.  For example, the first byte will
  /// be placed in Vec[3], and the following bytes will be placed in 2, 1, 0,
  /// 7, 6, 5, 4, 11, 10, 9, 8, and so on.
  class UnwindOpcodeStreamer {
  private:
    SmallVectorImpl<uint8_t> &Vec;
    size_t Pos = 3;

  public:
    UnwindOpcodeStreamer(SmallVectorImpl<uint8_t> &V) : Vec(V) {}

    /// Emit the byte in MSB to LSB per uint32_t order.
    void EmitByte(uint8_t elem) {
      Vec[Pos] = elem;
      Pos = (((Pos ^ 0x3u) + 1) ^ 0x3u);
    }

    /// Emit the size prefix.
    void EmitSize(size_t Size) {
      size_t SizeInWords = (Size + 3) / 4;
      assert(SizeInWords <= 0x100u &&
             "Only 256 additional words are allowed for unwind opcodes");
      EmitByte(static_cast<uint8_t>(SizeInWords - 1));
    }

    /// Emit the personality index prefix.
    void EmitPersonalityIndex(unsigned PI) {
      assert(PI < ARM::EHABI::NUM_PERSONALITY_INDEX &&
             "Invalid personality prefix");
      EmitByte(ARM::EHABI::EHT_COMPACT | PI);
    }

    /// Fill the rest of bytes with FINISH opcode.
    void FillFinishOpcode() {
      while (Pos < Vec.size())
        EmitByte(ARM::EHABI::UNWIND_OPCODE_FINISH);
    }
  };

} // end anonymous namespace

void UnwindOpcodeAssembler::EmitRegSave(uint32_t RegSave) {
  if (RegSave == 0u) {
    // That's the special case for RA PAC.
    EmitInt8(ARM::EHABI::UNWIND_OPCODE_POP_RA_AUTH_CODE);
    return;
  }

  // One byte opcode to save register r14 and r11-r4
  if (RegSave & (1u << 4)) {
    // The one byte opcode will always save r4, thus we can't use the one byte
    // opcode when r4 is not in .save directive.

    // Compute the consecutive registers from r4 to r11.
    uint32_t Mask = RegSave & 0xff0u;
    uint32_t Range = llvm::countr_one(Mask >> 5); // Exclude r4.
    // Mask off non-consecutive registers. Keep r4.
    Mask &= ~(0xffffffe0u << Range);

    // Emit this opcode when the mask covers every registers.
    uint32_t UnmaskedReg = RegSave & 0xfff0u & (~Mask);
    if (UnmaskedReg == 0u) {
      // Pop r[4 : (4 + n)]
      EmitInt8(ARM::EHABI::UNWIND_OPCODE_POP_REG_RANGE_R4 | Range);
      RegSave &= 0x000fu;
    } else if (UnmaskedReg == (1u << 14)) {
      // Pop r[14] + r[4 : (4 + n)]
      EmitInt8(ARM::EHABI::UNWIND_OPCODE_POP_REG_RANGE_R4_R14 | Range);
      RegSave &= 0x000fu;
    }
  }

  // Two bytes opcode to save register r15-r4
  if ((RegSave & 0xfff0u) != 0)
    EmitInt16(ARM::EHABI::UNWIND_OPCODE_POP_REG_MASK_R4 | (RegSave >> 4));

  // Opcode to save register r3-r0
  if ((RegSave & 0x000fu) != 0)
    EmitInt16(ARM::EHABI::UNWIND_OPCODE_POP_REG_MASK | (RegSave & 0x000fu));
}

/// Emit unwind opcodes for .vsave directives
void UnwindOpcodeAssembler::EmitVFPRegSave(uint32_t VFPRegSave) {
  // We only have 4 bits to save the offset in the opcode so look at the lower
  // and upper 16 bits separately.
  for (uint32_t Regs : {VFPRegSave & 0xffff0000u, VFPRegSave & 0x0000ffffu}) {
    while (Regs) {
      // Now look for a run of set bits. Remember the MSB and LSB of the run.
      auto RangeMSB = llvm::bit_width(Regs);
      auto RangeLen = llvm::countl_one(Regs << (32 - RangeMSB));
      auto RangeLSB = RangeMSB - RangeLen;

      int Opcode = RangeLSB >= 16
                       ? ARM::EHABI::UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD_D16
                       : ARM::EHABI::UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD;

      EmitInt16(Opcode | ((RangeLSB % 16) << 4) | (RangeLen - 1));

      // Zero out bits we're done with.
      Regs &= ~(-1u << RangeLSB);
    }
  }
}

/// Emit unwind opcodes to copy address from source register to $sp.
void UnwindOpcodeAssembler::EmitSetSP(uint16_t Reg) {
  EmitInt8(ARM::EHABI::UNWIND_OPCODE_SET_VSP | Reg);
}

/// Emit unwind opcodes to add $sp with an offset.
void UnwindOpcodeAssembler::EmitSPOffset(int64_t Offset) {
  if (Offset > 0x200) {
    uint8_t Buff[16];
    Buff[0] = ARM::EHABI::UNWIND_OPCODE_INC_VSP_ULEB128;
    size_t ULEBSize = encodeULEB128((Offset - 0x204) >> 2, Buff + 1);
    emitBytes(Buff, ULEBSize + 1);
  } else if (Offset > 0) {
    if (Offset > 0x100) {
      EmitInt8(ARM::EHABI::UNWIND_OPCODE_INC_VSP | 0x3fu);
      Offset -= 0x100;
    }
    EmitInt8(ARM::EHABI::UNWIND_OPCODE_INC_VSP |
             static_cast<uint8_t>((Offset - 4) >> 2));
  } else if (Offset < 0) {
    while (Offset < -0x100) {
      EmitInt8(ARM::EHABI::UNWIND_OPCODE_DEC_VSP | 0x3fu);
      Offset += 0x100;
    }
    EmitInt8(ARM::EHABI::UNWIND_OPCODE_DEC_VSP |
             static_cast<uint8_t>(((-Offset) - 4) >> 2));
  }
}

void UnwindOpcodeAssembler::Finalize(unsigned &PersonalityIndex,
                                     SmallVectorImpl<uint8_t> &Result) {
  UnwindOpcodeStreamer OpStreamer(Result);

  if (HasPersonality) {
    // User-specifed personality routine: [ SIZE , OP1 , OP2 , ... ]
    PersonalityIndex = ARM::EHABI::NUM_PERSONALITY_INDEX;
    size_t TotalSize = Ops.size() + 1;
    size_t RoundUpSize = (TotalSize + 3) / 4 * 4;
    Result.resize(RoundUpSize);
    OpStreamer.EmitSize(RoundUpSize);
  } else {
    // If no personalityindex is specified, select ane
    if (PersonalityIndex == ARM::EHABI::NUM_PERSONALITY_INDEX)
      PersonalityIndex = (Ops.size() <= 3) ? ARM::EHABI::AEABI_UNWIND_CPP_PR0
                                           : ARM::EHABI::AEABI_UNWIND_CPP_PR1;
    if (PersonalityIndex == ARM::EHABI::AEABI_UNWIND_CPP_PR0) {
      // __aeabi_unwind_cpp_pr0: [ 0x80 , OP1 , OP2 , OP3 ]
      assert(Ops.size() <= 3 && "too many opcodes for __aeabi_unwind_cpp_pr0");
      Result.resize(4);
      OpStreamer.EmitPersonalityIndex(PersonalityIndex);
    } else {
      // __aeabi_unwind_cpp_pr{1,2}: [ {0x81,0x82} , SIZE , OP1 , OP2 , ... ]
      size_t TotalSize = Ops.size() + 2;
      size_t RoundUpSize = (TotalSize + 3) / 4 * 4;
      Result.resize(RoundUpSize);
      OpStreamer.EmitPersonalityIndex(PersonalityIndex);
      OpStreamer.EmitSize(RoundUpSize);
    }
  }

  // Copy the unwind opcodes
  for (size_t i = OpBegins.size() - 1; i > 0; --i)
    for (size_t j = OpBegins[i - 1], end = OpBegins[i]; j < end; ++j)
      OpStreamer.EmitByte(Ops[j]);

  // Emit the padding finish opcodes if the size is not multiple of 4.
  OpStreamer.FillFinishOpcode();

  // Reset the assembler state
  Reset();
}
