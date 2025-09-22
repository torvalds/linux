//===-- RISCVInstPrinter.cpp - Convert RISC-V MCInst to asm syntax --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an RISC-V MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "RISCVInstPrinter.h"
#include "RISCVBaseInfo.h"
#include "RISCVMCExpr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "RISCVGenAsmWriter.inc"

static cl::opt<bool>
    NoAliases("riscv-no-aliases",
              cl::desc("Disable the emission of assembler pseudo instructions"),
              cl::init(false), cl::Hidden);

// Print architectural register names rather than the ABI names (such as x2
// instead of sp).
// TODO: Make RISCVInstPrinter::getRegisterName non-static so that this can a
// member.
static bool ArchRegNames;

// The command-line flags above are used by llvm-mc and llc. They can be used by
// `llvm-objdump`, but we override their values here to handle options passed to
// `llvm-objdump` with `-M` (which matches GNU objdump). There did not seem to
// be an easier way to allow these options in all these tools, without doing it
// this way.
bool RISCVInstPrinter::applyTargetSpecificCLOption(StringRef Opt) {
  if (Opt == "no-aliases") {
    PrintAliases = false;
    return true;
  }
  if (Opt == "numeric") {
    ArchRegNames = true;
    return true;
  }

  return false;
}

void RISCVInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &O) {
  bool Res = false;
  const MCInst *NewMI = MI;
  MCInst UncompressedMI;
  if (PrintAliases && !NoAliases)
    Res = RISCVRVC::uncompress(UncompressedMI, *MI, STI);
  if (Res)
    NewMI = const_cast<MCInst *>(&UncompressedMI);
  if (!PrintAliases || NoAliases || !printAliasInstr(NewMI, Address, STI, O))
    printInstruction(NewMI, Address, STI, O);
  printAnnotation(O, Annot);
}

void RISCVInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) const {
  markup(O, Markup::Register) << getRegisterName(Reg);
}

void RISCVInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    const MCSubtargetInfo &STI, raw_ostream &O,
                                    const char *Modifier) {
  assert((Modifier == nullptr || Modifier[0] == 0) && "No modifiers supported");
  const MCOperand &MO = MI->getOperand(OpNo);

  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }

  if (MO.isImm()) {
    markup(O, Markup::Immediate) << formatImm(MO.getImm());
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MO.getExpr()->print(O, &MAI);
}

void RISCVInstPrinter::printBranchOperand(const MCInst *MI, uint64_t Address,
                                          unsigned OpNo,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  if (!MO.isImm())
    return printOperand(MI, OpNo, STI, O);

  if (PrintBranchImmAsAddress) {
    uint64_t Target = Address + MO.getImm();
    if (!STI.hasFeature(RISCV::Feature64Bit))
      Target &= 0xffffffff;
    markup(O, Markup::Target) << formatHex(Target);
  } else {
    markup(O, Markup::Target) << formatImm(MO.getImm());
  }
}

void RISCVInstPrinter::printCSRSystemRegister(const MCInst *MI, unsigned OpNo,
                                              const MCSubtargetInfo &STI,
                                              raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNo).getImm();
  auto Range = RISCVSysReg::lookupSysRegByEncoding(Imm);
  for (auto &Reg : Range) {
    if (Reg.haveRequiredFeatures(STI.getFeatureBits())) {
      markup(O, Markup::Register) << Reg.Name;
      return;
    }
  }
  markup(O, Markup::Register) << formatImm(Imm);
}

void RISCVInstPrinter::printFenceArg(const MCInst *MI, unsigned OpNo,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  unsigned FenceArg = MI->getOperand(OpNo).getImm();
  assert (((FenceArg >> 4) == 0) && "Invalid immediate in printFenceArg");

  if ((FenceArg & RISCVFenceField::I) != 0)
    O << 'i';
  if ((FenceArg & RISCVFenceField::O) != 0)
    O << 'o';
  if ((FenceArg & RISCVFenceField::R) != 0)
    O << 'r';
  if ((FenceArg & RISCVFenceField::W) != 0)
    O << 'w';
  if (FenceArg == 0)
    O << "0";
}

void RISCVInstPrinter::printFRMArg(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  auto FRMArg =
      static_cast<RISCVFPRndMode::RoundingMode>(MI->getOperand(OpNo).getImm());
  if (PrintAliases && !NoAliases && FRMArg == RISCVFPRndMode::RoundingMode::DYN)
    return;
  O << ", " << RISCVFPRndMode::roundingModeToString(FRMArg);
}

void RISCVInstPrinter::printFRMArgLegacy(const MCInst *MI, unsigned OpNo,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  auto FRMArg =
      static_cast<RISCVFPRndMode::RoundingMode>(MI->getOperand(OpNo).getImm());
  // Never print rounding mode if it's the default 'rne'. This ensures the
  // output can still be parsed by older tools that erroneously failed to
  // accept a rounding mode.
  if (FRMArg == RISCVFPRndMode::RoundingMode::RNE)
    return;
  O << ", " << RISCVFPRndMode::roundingModeToString(FRMArg);
}

void RISCVInstPrinter::printFPImmOperand(const MCInst *MI, unsigned OpNo,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNo).getImm();
  if (Imm == 1) {
    markup(O, Markup::Immediate) << "min";
  } else if (Imm == 30) {
    markup(O, Markup::Immediate) << "inf";
  } else if (Imm == 31) {
    markup(O, Markup::Immediate) << "nan";
  } else {
    float FPVal = RISCVLoadFPImm::getFPImm(Imm);
    // If the value is an integer, print a .0 fraction. Otherwise, use %g to
    // which will not print trailing zeros and will use scientific notation
    // if it is shorter than printing as a decimal. The smallest value requires
    // 12 digits of precision including the decimal.
    if (FPVal == (int)(FPVal))
      markup(O, Markup::Immediate) << format("%.1f", FPVal);
    else
      markup(O, Markup::Immediate) << format("%.12g", FPVal);
  }
}

void RISCVInstPrinter::printZeroOffsetMemOp(const MCInst *MI, unsigned OpNo,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  assert(MO.isReg() && "printZeroOffsetMemOp can only print register operands");
  O << "(";
  printRegName(O, MO.getReg());
  O << ")";
}

void RISCVInstPrinter::printVTypeI(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNo).getImm();
  // Print the raw immediate for reserved values: vlmul[2:0]=4, vsew[2:0]=0b1xx,
  // or non-zero in bits 8 and above.
  if (RISCVVType::getVLMUL(Imm) == RISCVII::VLMUL::LMUL_RESERVED ||
      RISCVVType::getSEW(Imm) > 64 || (Imm >> 8) != 0) {
    O << formatImm(Imm);
    return;
  }
  // Print the text form.
  RISCVVType::printVType(Imm, O);
}

// Print a Zcmp RList. If we are printing architectural register names rather
// than ABI register names, we need to print "{x1, x8-x9, x18-x27}" for all
// registers. Otherwise, we print "{ra, s0-s11}".
void RISCVInstPrinter::printRlist(const MCInst *MI, unsigned OpNo,
                                  const MCSubtargetInfo &STI, raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNo).getImm();
  O << "{";
  printRegName(O, RISCV::X1);

  if (Imm >= RISCVZC::RLISTENCODE::RA_S0) {
    O << ", ";
    printRegName(O, RISCV::X8);
  }

  if (Imm >= RISCVZC::RLISTENCODE::RA_S0_S1) {
    O << '-';
    if (Imm == RISCVZC::RLISTENCODE::RA_S0_S1 || ArchRegNames)
      printRegName(O, RISCV::X9);
  }

  if (Imm >= RISCVZC::RLISTENCODE::RA_S0_S2) {
    if (ArchRegNames)
      O << ", ";
    if (Imm == RISCVZC::RLISTENCODE::RA_S0_S2 || ArchRegNames)
      printRegName(O, RISCV::X18);
  }

  if (Imm >= RISCVZC::RLISTENCODE::RA_S0_S3) {
    if (ArchRegNames)
      O << '-';
    unsigned Offset = (Imm - RISCVZC::RLISTENCODE::RA_S0_S3);
    // Encodings for S3-S9 are contiguous. There is no encoding for S10, so we
    // must skip to S11(X27).
    if (Imm == RISCVZC::RLISTENCODE::RA_S0_S11)
      ++Offset;
    printRegName(O, RISCV::X19 + Offset);
  }

  O << "}";
}

void RISCVInstPrinter::printRegReg(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  assert(MO.isReg() && "printRegReg can only print register operands");
  if (MO.getReg() == RISCV::NoRegister)
    return;
  printRegName(O, MO.getReg());

  O << "(";
  const MCOperand &MO1 = MI->getOperand(OpNo + 1);
  assert(MO1.isReg() && "printRegReg can only print register operands");
  printRegName(O, MO1.getReg());
  O << ")";
}

void RISCVInstPrinter::printStackAdj(const MCInst *MI, unsigned OpNo,
                                     const MCSubtargetInfo &STI, raw_ostream &O,
                                     bool Negate) {
  int64_t Imm = MI->getOperand(OpNo).getImm();
  bool IsRV64 = STI.hasFeature(RISCV::Feature64Bit);
  int64_t StackAdj = 0;
  auto RlistVal = MI->getOperand(0).getImm();
  assert(RlistVal != 16 && "Incorrect rlist.");
  auto Base = RISCVZC::getStackAdjBase(RlistVal, IsRV64);
  StackAdj = Imm + Base;
  assert((StackAdj >= Base && StackAdj <= Base + 48) &&
         "Incorrect stack adjust");
  if (Negate)
    StackAdj = -StackAdj;

  // RAII guard for ANSI color escape sequences
  WithMarkup ScopedMarkup = markup(O, Markup::Immediate);
  O << StackAdj;
}

void RISCVInstPrinter::printVMaskReg(const MCInst *MI, unsigned OpNo,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  assert(MO.isReg() && "printVMaskReg can only print register operands");
  if (MO.getReg() == RISCV::NoRegister)
    return;
  O << ", ";
  printRegName(O, MO.getReg());
  O << ".t";
}

const char *RISCVInstPrinter::getRegisterName(MCRegister Reg) {
  return getRegisterName(Reg, ArchRegNames ? RISCV::NoRegAltName
                                           : RISCV::ABIRegAltName);
}
