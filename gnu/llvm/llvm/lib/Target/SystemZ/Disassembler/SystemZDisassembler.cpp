//===-- SystemZDisassembler.cpp - Disassembler for SystemZ ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "SystemZ.h"
#include "TargetInfo/SystemZTargetInfo.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "systemz-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

class SystemZDisassembler : public MCDisassembler {
public:
  SystemZDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
    : MCDisassembler(STI, Ctx) {}
  ~SystemZDisassembler() override = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static MCDisassembler *createSystemZDisassembler(const Target &T,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new SystemZDisassembler(STI, Ctx);
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSystemZDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheSystemZTarget(),
                                         createSystemZDisassembler);
}

/// tryAddingSymbolicOperand - trys to add a symbolic operand in place of the
/// immediate Value in the MCInst.
///
/// @param Value      - The immediate Value, has had any PC adjustment made by
///                     the caller.
/// @param isBranch   - If the instruction is a branch instruction
/// @param Address    - The starting address of the instruction
/// @param Offset     - The byte offset to this immediate in the instruction
/// @param Width      - The byte width of this immediate in the instruction
///
/// If the getOpInfo() function was set when setupForSymbolicDisassembly() was
/// called then that function is called to get any symbolic information for the
/// immediate in the instruction using the Address, Offset and Width.  If that
/// returns non-zero then the symbolic information it returns is used to create
/// an MCExpr and that is added as an operand to the MCInst.  If getOpInfo()
/// returns zero and isBranch is true then a symbol look up for immediate Value
/// is done and if a symbol is found an MCExpr is created with that, else
/// an MCExpr with the immediate Value is created.  This function returns true
/// if it adds an operand to the MCInst and false otherwise.
static bool tryAddingSymbolicOperand(int64_t Value, bool IsBranch,
                                     uint64_t Address, uint64_t Offset,
                                     uint64_t Width, MCInst &MI,
                                     const MCDisassembler *Decoder) {
  return Decoder->tryAddingSymbolicOperand(MI, Value, Address, IsBranch, Offset,
                                           Width, /*InstSize=*/0);
}

static DecodeStatus decodeRegisterClass(MCInst &Inst, uint64_t RegNo,
                                        const unsigned *Regs, unsigned Size,
                                        bool IsAddr = false) {
  assert(RegNo < Size && "Invalid register");
  if (IsAddr && RegNo == 0) {
    RegNo = SystemZ::NoRegister;
  } else {
    RegNo = Regs[RegNo];
    if (RegNo == 0)
      return MCDisassembler::Fail;
  }
  Inst.addOperand(MCOperand::createReg(RegNo));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGR32BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::GR32Regs, 16);
}

static DecodeStatus DecodeGRH32BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::GRH32Regs, 16);
}

static DecodeStatus DecodeGR64BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::GR64Regs, 16);
}

static DecodeStatus DecodeGR128BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::GR128Regs, 16);
}

static DecodeStatus
DecodeADDR32BitRegisterClass(MCInst &Inst, uint64_t RegNo, uint64_t Address,
                             const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::GR32Regs, 16, true);
}

static DecodeStatus
DecodeADDR64BitRegisterClass(MCInst &Inst, uint64_t RegNo, uint64_t Address,
                             const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::GR64Regs, 16, true);
}

static DecodeStatus DecodeFP32BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::FP32Regs, 16);
}

static DecodeStatus DecodeFP64BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::FP64Regs, 16);
}

static DecodeStatus DecodeFP128BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::FP128Regs, 16);
}

static DecodeStatus DecodeVR32BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::VR32Regs, 32);
}

static DecodeStatus DecodeVR64BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::VR64Regs, 32);
}

static DecodeStatus DecodeVR128BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::VR128Regs, 32);
}

static DecodeStatus DecodeAR32BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::AR32Regs, 16);
}

static DecodeStatus DecodeCR64BitRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodeRegisterClass(Inst, RegNo, SystemZMC::CR64Regs, 16);
}

template<unsigned N>
static DecodeStatus decodeUImmOperand(MCInst &Inst, uint64_t Imm) {
  if (!isUInt<N>(Imm))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

template<unsigned N>
static DecodeStatus decodeSImmOperand(MCInst &Inst, uint64_t Imm) {
  if (!isUInt<N>(Imm))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeU1ImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return decodeUImmOperand<1>(Inst, Imm);
}

static DecodeStatus decodeU2ImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return decodeUImmOperand<2>(Inst, Imm);
}

static DecodeStatus decodeU3ImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return decodeUImmOperand<3>(Inst, Imm);
}

static DecodeStatus decodeU4ImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return decodeUImmOperand<4>(Inst, Imm);
}

static DecodeStatus decodeU8ImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return decodeUImmOperand<8>(Inst, Imm);
}

static DecodeStatus decodeU12ImmOperand(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeUImmOperand<12>(Inst, Imm);
}

static DecodeStatus decodeU16ImmOperand(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeUImmOperand<16>(Inst, Imm);
}

static DecodeStatus decodeU32ImmOperand(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeUImmOperand<32>(Inst, Imm);
}

static DecodeStatus decodeS8ImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return decodeSImmOperand<8>(Inst, Imm);
}

static DecodeStatus decodeS16ImmOperand(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeSImmOperand<16>(Inst, Imm);
}

static DecodeStatus decodeS20ImmOperand(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeSImmOperand<20>(Inst, Imm);
}

static DecodeStatus decodeS32ImmOperand(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeSImmOperand<32>(Inst, Imm);
}

template <unsigned N>
static DecodeStatus decodeLenOperand(MCInst &Inst, uint64_t Imm,
                                     uint64_t Address,
                                     const MCDisassembler *Decoder) {
  if (!isUInt<N>(Imm))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(Imm + 1));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodePCDBLOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address, bool isBranch,
                                       const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid PC-relative offset");
  uint64_t Value = SignExtend64<N>(Imm) * 2 + Address;

  if (!tryAddingSymbolicOperand(Value, isBranch, Address, 2, N / 8,
                                Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(Value));

  return MCDisassembler::Success;
}

static DecodeStatus decodePC12DBLBranchOperand(MCInst &Inst, uint64_t Imm,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodePCDBLOperand<12>(Inst, Imm, Address, true, Decoder);
}

static DecodeStatus decodePC16DBLBranchOperand(MCInst &Inst, uint64_t Imm,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodePCDBLOperand<16>(Inst, Imm, Address, true, Decoder);
}

static DecodeStatus decodePC24DBLBranchOperand(MCInst &Inst, uint64_t Imm,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodePCDBLOperand<24>(Inst, Imm, Address, true, Decoder);
}

static DecodeStatus decodePC32DBLBranchOperand(MCInst &Inst, uint64_t Imm,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return decodePCDBLOperand<32>(Inst, Imm, Address, true, Decoder);
}

static DecodeStatus decodePC32DBLOperand(MCInst &Inst, uint64_t Imm,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  return decodePCDBLOperand<32>(Inst, Imm, Address, false, Decoder);
}

#include "SystemZGenDisassemblerTables.inc"

DecodeStatus SystemZDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CS) const {
  // Get the first two bytes of the instruction.
  Size = 0;
  if (Bytes.size() < 2)
    return MCDisassembler::Fail;

  // The top 2 bits of the first byte specify the size.
  const uint8_t *Table;
  if (Bytes[0] < 0x40) {
    Size = 2;
    Table = DecoderTable16;
  } else if (Bytes[0] < 0xc0) {
    Size = 4;
    Table = DecoderTable32;
  } else {
    Size = 6;
    Table = DecoderTable48;
  }

  // Read any remaining bytes.
  if (Bytes.size() < Size) {
    Size = Bytes.size();
    return MCDisassembler::Fail;
  }

  // Construct the instruction.
  uint64_t Inst = 0;
  for (uint64_t I = 0; I < Size; ++I)
    Inst = (Inst << 8) | Bytes[I];

  return decodeInstruction(Table, MI, Inst, Address, this, STI);
}
