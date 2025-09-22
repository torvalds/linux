//===- RISCVMatInt.cpp - Immediate materialisation -------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCVMatInt.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;

static int getInstSeqCost(RISCVMatInt::InstSeq &Res, bool HasRVC) {
  if (!HasRVC)
    return Res.size();

  int Cost = 0;
  for (auto Instr : Res) {
    // Assume instructions that aren't listed aren't compressible.
    bool Compressed = false;
    switch (Instr.getOpcode()) {
    case RISCV::SLLI:
    case RISCV::SRLI:
      Compressed = true;
      break;
    case RISCV::ADDI:
    case RISCV::ADDIW:
    case RISCV::LUI:
      Compressed = isInt<6>(Instr.getImm());
      break;
    }
    // Two RVC instructions take the same space as one RVI instruction, but
    // can take longer to execute than the single RVI instruction. Thus, we
    // consider that two RVC instruction are slightly more costly than one
    // RVI instruction. For longer sequences of RVC instructions the space
    // savings can be worth it, though. The costs below try to model that.
    if (!Compressed)
      Cost += 100; // Baseline cost of one RVI instruction: 100%.
    else
      Cost += 70; // 70% cost of baseline.
  }
  return Cost;
}

// Recursively generate a sequence for materializing an integer.
static void generateInstSeqImpl(int64_t Val, const MCSubtargetInfo &STI,
                                RISCVMatInt::InstSeq &Res) {
  bool IsRV64 = STI.hasFeature(RISCV::Feature64Bit);

  // Use BSETI for a single bit that can't be expressed by a single LUI or ADDI.
  if (STI.hasFeature(RISCV::FeatureStdExtZbs) && isPowerOf2_64(Val) &&
      (!isInt<32>(Val) || Val == 0x800)) {
    Res.emplace_back(RISCV::BSETI, Log2_64(Val));
    return;
  }

  if (isInt<32>(Val)) {
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    int64_t Hi20 = ((Val + 0x800) >> 12) & 0xFFFFF;
    int64_t Lo12 = SignExtend64<12>(Val);

    if (Hi20)
      Res.emplace_back(RISCV::LUI, Hi20);

    if (Lo12 || Hi20 == 0) {
      unsigned AddiOpc = (IsRV64 && Hi20) ? RISCV::ADDIW : RISCV::ADDI;
      Res.emplace_back(AddiOpc, Lo12);
    }
    return;
  }

  assert(IsRV64 && "Can't emit >32-bit imm for non-RV64 target");

  // In the worst case, for a full 64-bit constant, a sequence of 8 instructions
  // (i.e., LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI) has to be emitted. Note
  // that the first two instructions (LUI+ADDIW) can contribute up to 32 bits
  // while the following ADDI instructions contribute up to 12 bits each.
  //
  // On the first glance, implementing this seems to be possible by simply
  // emitting the most significant 32 bits (LUI+ADDIW) followed by as many left
  // shift (SLLI) and immediate additions (ADDI) as needed. However, due to the
  // fact that ADDI performs a sign extended addition, doing it like that would
  // only be possible when at most 11 bits of the ADDI instructions are used.
  // Using all 12 bits of the ADDI instructions, like done by GAS, actually
  // requires that the constant is processed starting with the least significant
  // bit.
  //
  // In the following, constants are processed from LSB to MSB but instruction
  // emission is performed from MSB to LSB by recursively calling
  // generateInstSeq. In each recursion, first the lowest 12 bits are removed
  // from the constant and the optimal shift amount, which can be greater than
  // 12 bits if the constant is sparse, is determined. Then, the shifted
  // remaining constant is processed recursively and gets emitted as soon as it
  // fits into 32 bits. The emission of the shifts and additions is subsequently
  // performed when the recursion returns.

  int64_t Lo12 = SignExtend64<12>(Val);
  Val = (uint64_t)Val - (uint64_t)Lo12;

  int ShiftAmount = 0;
  bool Unsigned = false;

  // Val might now be valid for LUI without needing a shift.
  if (!isInt<32>(Val)) {
    ShiftAmount = llvm::countr_zero((uint64_t)Val);
    Val >>= ShiftAmount;

    // If the remaining bits don't fit in 12 bits, we might be able to reduce
    // the // shift amount in order to use LUI which will zero the lower 12
    // bits.
    if (ShiftAmount > 12 && !isInt<12>(Val)) {
      if (isInt<32>((uint64_t)Val << 12)) {
        // Reduce the shift amount and add zeros to the LSBs so it will match
        // LUI.
        ShiftAmount -= 12;
        Val = (uint64_t)Val << 12;
      } else if (isUInt<32>((uint64_t)Val << 12) &&
                 STI.hasFeature(RISCV::FeatureStdExtZba)) {
        // Reduce the shift amount and add zeros to the LSBs so it will match
        // LUI, then shift left with SLLI.UW to clear the upper 32 set bits.
        ShiftAmount -= 12;
        Val = ((uint64_t)Val << 12) | (0xffffffffull << 32);
        Unsigned = true;
      }
    }

    // Try to use SLLI_UW for Val when it is uint32 but not int32.
    if (isUInt<32>((uint64_t)Val) && !isInt<32>((uint64_t)Val) &&
        STI.hasFeature(RISCV::FeatureStdExtZba)) {
      // Use LUI+ADDI or LUI to compose, then clear the upper 32 bits with
      // SLLI_UW.
      Val = ((uint64_t)Val) | (0xffffffffull << 32);
      Unsigned = true;
    }
  }

  generateInstSeqImpl(Val, STI, Res);

  // Skip shift if we were able to use LUI directly.
  if (ShiftAmount) {
    unsigned Opc = Unsigned ? RISCV::SLLI_UW : RISCV::SLLI;
    Res.emplace_back(Opc, ShiftAmount);
  }

  if (Lo12)
    Res.emplace_back(RISCV::ADDI, Lo12);
}

static unsigned extractRotateInfo(int64_t Val) {
  // for case: 0b111..1..xxxxxx1..1..
  unsigned LeadingOnes = llvm::countl_one((uint64_t)Val);
  unsigned TrailingOnes = llvm::countr_one((uint64_t)Val);
  if (TrailingOnes > 0 && TrailingOnes < 64 &&
      (LeadingOnes + TrailingOnes) > (64 - 12))
    return 64 - TrailingOnes;

  // for case: 0bxxx1..1..1...xxx
  unsigned UpperTrailingOnes = llvm::countr_one(Hi_32(Val));
  unsigned LowerLeadingOnes = llvm::countl_one(Lo_32(Val));
  if (UpperTrailingOnes < 32 &&
      (UpperTrailingOnes + LowerLeadingOnes) > (64 - 12))
    return 32 - UpperTrailingOnes;

  return 0;
}

static void generateInstSeqLeadingZeros(int64_t Val, const MCSubtargetInfo &STI,
                                        RISCVMatInt::InstSeq &Res) {
  assert(Val > 0 && "Expected postive val");

  unsigned LeadingZeros = llvm::countl_zero((uint64_t)Val);
  uint64_t ShiftedVal = (uint64_t)Val << LeadingZeros;
  // Fill in the bits that will be shifted out with 1s. An example where this
  // helps is trailing one masks with 32 or more ones. This will generate
  // ADDI -1 and an SRLI.
  ShiftedVal |= maskTrailingOnes<uint64_t>(LeadingZeros);

  RISCVMatInt::InstSeq TmpSeq;
  generateInstSeqImpl(ShiftedVal, STI, TmpSeq);

  // Keep the new sequence if it is an improvement or the original is empty.
  if ((TmpSeq.size() + 1) < Res.size() ||
      (Res.empty() && TmpSeq.size() < 8)) {
    TmpSeq.emplace_back(RISCV::SRLI, LeadingZeros);
    Res = TmpSeq;
  }

  // Some cases can benefit from filling the lower bits with zeros instead.
  ShiftedVal &= maskTrailingZeros<uint64_t>(LeadingZeros);
  TmpSeq.clear();
  generateInstSeqImpl(ShiftedVal, STI, TmpSeq);

  // Keep the new sequence if it is an improvement or the original is empty.
  if ((TmpSeq.size() + 1) < Res.size() ||
      (Res.empty() && TmpSeq.size() < 8)) {
    TmpSeq.emplace_back(RISCV::SRLI, LeadingZeros);
    Res = TmpSeq;
  }

  // If we have exactly 32 leading zeros and Zba, we can try using zext.w at
  // the end of the sequence.
  if (LeadingZeros == 32 && STI.hasFeature(RISCV::FeatureStdExtZba)) {
    // Try replacing upper bits with 1.
    uint64_t LeadingOnesVal = Val | maskLeadingOnes<uint64_t>(LeadingZeros);
    TmpSeq.clear();
    generateInstSeqImpl(LeadingOnesVal, STI, TmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((TmpSeq.size() + 1) < Res.size() ||
        (Res.empty() && TmpSeq.size() < 8)) {
      TmpSeq.emplace_back(RISCV::ADD_UW, 0);
      Res = TmpSeq;
    }
  }
}

namespace llvm::RISCVMatInt {
InstSeq generateInstSeq(int64_t Val, const MCSubtargetInfo &STI) {
  RISCVMatInt::InstSeq Res;
  generateInstSeqImpl(Val, STI, Res);

  // If the low 12 bits are non-zero, the first expansion may end with an ADDI
  // or ADDIW. If there are trailing zeros, try generating a sign extended
  // constant with no trailing zeros and use a final SLLI to restore them.
  if ((Val & 0xfff) != 0 && (Val & 1) == 0 && Res.size() >= 2) {
    unsigned TrailingZeros = llvm::countr_zero((uint64_t)Val);
    int64_t ShiftedVal = Val >> TrailingZeros;
    // If we can use C.LI+C.SLLI instead of LUI+ADDI(W) prefer that since
    // its more compressible. But only if LUI+ADDI(W) isn't fusable.
    // NOTE: We don't check for C extension to minimize differences in generated
    // code.
    bool IsShiftedCompressible =
        isInt<6>(ShiftedVal) && !STI.hasFeature(RISCV::TuneLUIADDIFusion);
    RISCVMatInt::InstSeq TmpSeq;
    generateInstSeqImpl(ShiftedVal, STI, TmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((TmpSeq.size() + 1) < Res.size() || IsShiftedCompressible) {
      TmpSeq.emplace_back(RISCV::SLLI, TrailingZeros);
      Res = TmpSeq;
    }
  }

  // If we have a 1 or 2 instruction sequence this is the best we can do. This
  // will always be true for RV32 and will often be true for RV64.
  if (Res.size() <= 2)
    return Res;

  assert(STI.hasFeature(RISCV::Feature64Bit) &&
         "Expected RV32 to only need 2 instructions");

  // If the lower 13 bits are something like 0x17ff, try to add 1 to change the
  // lower 13 bits to 0x1800. We can restore this with an ADDI of -1 at the end
  // of the sequence. Call generateInstSeqImpl on the new constant which may
  // subtract 0xfffffffffffff800 to create another ADDI. This will leave a
  // constant with more than 12 trailing zeros for the next recursive step.
  if ((Val & 0xfff) != 0 && (Val & 0x1800) == 0x1000) {
    int64_t Imm12 = -(0x800 - (Val & 0xfff));
    int64_t AdjustedVal = Val - Imm12;
    RISCVMatInt::InstSeq TmpSeq;
    generateInstSeqImpl(AdjustedVal, STI, TmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((TmpSeq.size() + 1) < Res.size()) {
      TmpSeq.emplace_back(RISCV::ADDI, Imm12);
      Res = TmpSeq;
    }
  }

  // If the constant is positive we might be able to generate a shifted constant
  // with no leading zeros and use a final SRLI to restore them.
  if (Val > 0 && Res.size() > 2) {
    generateInstSeqLeadingZeros(Val, STI, Res);
  }

  // If the constant is negative, trying inverting and using our trailing zero
  // optimizations. Use an xori to invert the final value.
  if (Val < 0 && Res.size() > 3) {
    uint64_t InvertedVal = ~(uint64_t)Val;
    RISCVMatInt::InstSeq TmpSeq;
    generateInstSeqLeadingZeros(InvertedVal, STI, TmpSeq);

    // Keep it if we found a sequence that is smaller after inverting.
    if (!TmpSeq.empty() && (TmpSeq.size() + 1) < Res.size()) {
      TmpSeq.emplace_back(RISCV::XORI, -1);
      Res = TmpSeq;
    }
  }

  // If the Low and High halves are the same, use pack. The pack instruction
  // packs the XLEN/2-bit lower halves of rs1 and rs2 into rd, with rs1 in the
  // lower half and rs2 in the upper half.
  if (Res.size() > 2 && STI.hasFeature(RISCV::FeatureStdExtZbkb)) {
    int64_t LoVal = SignExtend64<32>(Val);
    int64_t HiVal = SignExtend64<32>(Val >> 32);
    if (LoVal == HiVal) {
      RISCVMatInt::InstSeq TmpSeq;
      generateInstSeqImpl(LoVal, STI, TmpSeq);
      if ((TmpSeq.size() + 1) < Res.size()) {
        TmpSeq.emplace_back(RISCV::PACK, 0);
        Res = TmpSeq;
      }
    }
  }

  // Perform optimization with BSETI in the Zbs extension.
  if (Res.size() > 2 && STI.hasFeature(RISCV::FeatureStdExtZbs)) {
    // Create a simm32 value for LUI+ADDIW by forcing the upper 33 bits to zero.
    // Xor that with original value to get which bits should be set by BSETI.
    uint64_t Lo = Val & 0x7fffffff;
    uint64_t Hi = Val ^ Lo;
    assert(Hi != 0);
    RISCVMatInt::InstSeq TmpSeq;

    if (Lo != 0)
      generateInstSeqImpl(Lo, STI, TmpSeq);

    if (TmpSeq.size() + llvm::popcount(Hi) < Res.size()) {
      do {
        TmpSeq.emplace_back(RISCV::BSETI, llvm::countr_zero(Hi));
        Hi &= (Hi - 1); // Clear lowest set bit.
      } while (Hi != 0);
      Res = TmpSeq;
    }
  }

  // Perform optimization with BCLRI in the Zbs extension.
  if (Res.size() > 2 && STI.hasFeature(RISCV::FeatureStdExtZbs)) {
    // Create a simm32 value for LUI+ADDIW by forcing the upper 33 bits to one.
    // Xor that with original value to get which bits should be cleared by
    // BCLRI.
    uint64_t Lo = Val | 0xffffffff80000000;
    uint64_t Hi = Val ^ Lo;
    assert(Hi != 0);

    RISCVMatInt::InstSeq TmpSeq;
    generateInstSeqImpl(Lo, STI, TmpSeq);

    if (TmpSeq.size() + llvm::popcount(Hi) < Res.size()) {
      do {
        TmpSeq.emplace_back(RISCV::BCLRI, llvm::countr_zero(Hi));
        Hi &= (Hi - 1); // Clear lowest set bit.
      } while (Hi != 0);
      Res = TmpSeq;
    }
  }

  // Perform optimization with SH*ADD in the Zba extension.
  if (Res.size() > 2 && STI.hasFeature(RISCV::FeatureStdExtZba)) {
    int64_t Div = 0;
    unsigned Opc = 0;
    RISCVMatInt::InstSeq TmpSeq;
    // Select the opcode and divisor.
    if ((Val % 3) == 0 && isInt<32>(Val / 3)) {
      Div = 3;
      Opc = RISCV::SH1ADD;
    } else if ((Val % 5) == 0 && isInt<32>(Val / 5)) {
      Div = 5;
      Opc = RISCV::SH2ADD;
    } else if ((Val % 9) == 0 && isInt<32>(Val / 9)) {
      Div = 9;
      Opc = RISCV::SH3ADD;
    }
    // Build the new instruction sequence.
    if (Div > 0) {
      generateInstSeqImpl(Val / Div, STI, TmpSeq);
      if ((TmpSeq.size() + 1) < Res.size()) {
        TmpSeq.emplace_back(Opc, 0);
        Res = TmpSeq;
      }
    } else {
      // Try to use LUI+SH*ADD+ADDI.
      int64_t Hi52 = ((uint64_t)Val + 0x800ull) & ~0xfffull;
      int64_t Lo12 = SignExtend64<12>(Val);
      Div = 0;
      if (isInt<32>(Hi52 / 3) && (Hi52 % 3) == 0) {
        Div = 3;
        Opc = RISCV::SH1ADD;
      } else if (isInt<32>(Hi52 / 5) && (Hi52 % 5) == 0) {
        Div = 5;
        Opc = RISCV::SH2ADD;
      } else if (isInt<32>(Hi52 / 9) && (Hi52 % 9) == 0) {
        Div = 9;
        Opc = RISCV::SH3ADD;
      }
      // Build the new instruction sequence.
      if (Div > 0) {
        // For Val that has zero Lo12 (implies Val equals to Hi52) should has
        // already been processed to LUI+SH*ADD by previous optimization.
        assert(Lo12 != 0 &&
               "unexpected instruction sequence for immediate materialisation");
        assert(TmpSeq.empty() && "Expected empty TmpSeq");
        generateInstSeqImpl(Hi52 / Div, STI, TmpSeq);
        if ((TmpSeq.size() + 2) < Res.size()) {
          TmpSeq.emplace_back(Opc, 0);
          TmpSeq.emplace_back(RISCV::ADDI, Lo12);
          Res = TmpSeq;
        }
      }
    }
  }

  // Perform optimization with rori in the Zbb and th.srri in the XTheadBb
  // extension.
  if (Res.size() > 2 && (STI.hasFeature(RISCV::FeatureStdExtZbb) ||
                         STI.hasFeature(RISCV::FeatureVendorXTHeadBb))) {
    if (unsigned Rotate = extractRotateInfo(Val)) {
      RISCVMatInt::InstSeq TmpSeq;
      uint64_t NegImm12 = llvm::rotl<uint64_t>(Val, Rotate);
      assert(isInt<12>(NegImm12));
      TmpSeq.emplace_back(RISCV::ADDI, NegImm12);
      TmpSeq.emplace_back(STI.hasFeature(RISCV::FeatureStdExtZbb)
                              ? RISCV::RORI
                              : RISCV::TH_SRRI,
                          Rotate);
      Res = TmpSeq;
    }
  }
  return Res;
}

void generateMCInstSeq(int64_t Val, const MCSubtargetInfo &STI,
                       MCRegister DestReg, SmallVectorImpl<MCInst> &Insts) {
  RISCVMatInt::InstSeq Seq = RISCVMatInt::generateInstSeq(Val, STI);

  MCRegister SrcReg = RISCV::X0;
  for (RISCVMatInt::Inst &Inst : Seq) {
    switch (Inst.getOpndKind()) {
    case RISCVMatInt::Imm:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addImm(Inst.getImm()));
      break;
    case RISCVMatInt::RegX0:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addReg(SrcReg)
                          .addReg(RISCV::X0));
      break;
    case RISCVMatInt::RegReg:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addReg(SrcReg)
                          .addReg(SrcReg));
      break;
    case RISCVMatInt::RegImm:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addReg(SrcReg)
                          .addImm(Inst.getImm()));
      break;
    }

    // Only the first instruction has X0 as its source.
    SrcReg = DestReg;
  }
}

InstSeq generateTwoRegInstSeq(int64_t Val, const MCSubtargetInfo &STI,
                              unsigned &ShiftAmt, unsigned &AddOpc) {
  int64_t LoVal = SignExtend64<32>(Val);
  if (LoVal == 0)
    return RISCVMatInt::InstSeq();

  // Subtract the LoVal to emulate the effect of the final ADD.
  uint64_t Tmp = (uint64_t)Val - (uint64_t)LoVal;
  assert(Tmp != 0);

  // Use trailing zero counts to figure how far we need to shift LoVal to line
  // up with the remaining constant.
  // TODO: This algorithm assumes all non-zero bits in the low 32 bits of the
  // final constant come from LoVal.
  unsigned TzLo = llvm::countr_zero((uint64_t)LoVal);
  unsigned TzHi = llvm::countr_zero(Tmp);
  assert(TzLo < 32 && TzHi >= 32);
  ShiftAmt = TzHi - TzLo;
  AddOpc = RISCV::ADD;

  if (Tmp == ((uint64_t)LoVal << ShiftAmt))
    return RISCVMatInt::generateInstSeq(LoVal, STI);

  // If we have Zba, we can use (ADD_UW X, (SLLI X, 32)).
  if (STI.hasFeature(RISCV::FeatureStdExtZba) && Lo_32(Val) == Hi_32(Val)) {
    ShiftAmt = 32;
    AddOpc = RISCV::ADD_UW;
    return RISCVMatInt::generateInstSeq(LoVal, STI);
  }

  return RISCVMatInt::InstSeq();
}

int getIntMatCost(const APInt &Val, unsigned Size, const MCSubtargetInfo &STI,
                  bool CompressionCost, bool FreeZeroes) {
  bool IsRV64 = STI.hasFeature(RISCV::Feature64Bit);
  bool HasRVC = CompressionCost && (STI.hasFeature(RISCV::FeatureStdExtC) ||
                                    STI.hasFeature(RISCV::FeatureStdExtZca));
  int PlatRegSize = IsRV64 ? 64 : 32;

  // Split the constant into platform register sized chunks, and calculate cost
  // of each chunk.
  int Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < Size; ShiftVal += PlatRegSize) {
    APInt Chunk = Val.ashr(ShiftVal).sextOrTrunc(PlatRegSize);
    if (FreeZeroes && Chunk.getSExtValue() == 0)
      continue;
    InstSeq MatSeq = generateInstSeq(Chunk.getSExtValue(), STI);
    Cost += getInstSeqCost(MatSeq, HasRVC);
  }
  return std::max(FreeZeroes ? 0 : 1, Cost);
}

OpndKind Inst::getOpndKind() const {
  switch (Opc) {
  default:
    llvm_unreachable("Unexpected opcode!");
  case RISCV::LUI:
    return RISCVMatInt::Imm;
  case RISCV::ADD_UW:
    return RISCVMatInt::RegX0;
  case RISCV::SH1ADD:
  case RISCV::SH2ADD:
  case RISCV::SH3ADD:
  case RISCV::PACK:
    return RISCVMatInt::RegReg;
  case RISCV::ADDI:
  case RISCV::ADDIW:
  case RISCV::XORI:
  case RISCV::SLLI:
  case RISCV::SRLI:
  case RISCV::SLLI_UW:
  case RISCV::RORI:
  case RISCV::BSETI:
  case RISCV::BCLRI:
  case RISCV::TH_SRRI:
    return RISCVMatInt::RegImm;
  }
}

} // namespace llvm::RISCVMatInt
