//===-- ARMInstPrinter.cpp - Convert ARM MCInst to assembly syntax --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an ARM MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "ARMInstPrinter.h"
#include "Utils/ARMBaseInfo.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#define PRINT_ALIAS_INSTR
#include "ARMGenAsmWriter.inc"

/// translateShiftImm - Convert shift immediate from 0-31 to 1-32 for printing.
///
/// getSORegOffset returns an integer from 0-31, representing '32' as 0.
static unsigned translateShiftImm(unsigned imm) {
  // lsr #32 and asr #32 exist, but should be encoded as a 0.
  assert((imm & ~0x1f) == 0 && "Invalid shift encoding");

  if (imm == 0)
    return 32;
  return imm;
}

static void printRegImmShift(raw_ostream &O, ARM_AM::ShiftOpc ShOpc,
                             unsigned ShImm, const ARMInstPrinter &printer) {
  if (ShOpc == ARM_AM::no_shift || (ShOpc == ARM_AM::lsl && !ShImm))
    return;
  O << ", ";

  assert(!(ShOpc == ARM_AM::ror && !ShImm) && "Cannot have ror #0");
  O << getShiftOpcStr(ShOpc);

  if (ShOpc != ARM_AM::rrx) {
    O << " ";
    printer.markup(O, llvm::MCInstPrinter::Markup::Immediate)
        << "#" << translateShiftImm(ShImm);
  }
}

ARMInstPrinter::ARMInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                               const MCRegisterInfo &MRI)
    : MCInstPrinter(MAI, MII, MRI) {}

bool ARMInstPrinter::applyTargetSpecificCLOption(StringRef Opt) {
  if (Opt == "reg-names-std") {
    DefaultAltIdx = ARM::NoRegAltName;
    return true;
  }
  if (Opt == "reg-names-raw") {
    DefaultAltIdx = ARM::RegNamesRaw;
    return true;
  }
  return false;
}

void ARMInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) const {
  markup(OS, Markup::Register) << getRegisterName(Reg, DefaultAltIdx);
}

void ARMInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                               StringRef Annot, const MCSubtargetInfo &STI,
                               raw_ostream &O) {
  unsigned Opcode = MI->getOpcode();

  switch (Opcode) {
  case ARM::VLLDM: {
    const MCOperand &Reg = MI->getOperand(0);
    O << '\t' << "vlldm" << '\t';
    printRegName(O, Reg.getReg());
    O << ", "
      << "{d0 - d15}";
    return;
  }
  case ARM::VLLDM_T2: {
    const MCOperand &Reg = MI->getOperand(0);
    O << '\t' << "vlldm" << '\t';
    printRegName(O, Reg.getReg());
    O << ", "
      << "{d0 - d31}";
    return;
  }
  case ARM::VLSTM: {
    const MCOperand &Reg = MI->getOperand(0);
    O << '\t' << "vlstm" << '\t';
    printRegName(O, Reg.getReg());
    O << ", "
      << "{d0 - d15}";
    return;
  }
  case ARM::VLSTM_T2: {
    const MCOperand &Reg = MI->getOperand(0);
    O << '\t' << "vlstm" << '\t';
    printRegName(O, Reg.getReg());
    O << ", "
      << "{d0 - d31}";
    return;
  }
  // Check for MOVs and print canonical forms, instead.
  case ARM::MOVsr: {
    // FIXME: Thumb variants?
    const MCOperand &Dst = MI->getOperand(0);
    const MCOperand &MO1 = MI->getOperand(1);
    const MCOperand &MO2 = MI->getOperand(2);
    const MCOperand &MO3 = MI->getOperand(3);

    O << '\t' << ARM_AM::getShiftOpcStr(ARM_AM::getSORegShOp(MO3.getImm()));
    printSBitModifierOperand(MI, 6, STI, O);
    printPredicateOperand(MI, 4, STI, O);

    O << '\t';
    printRegName(O, Dst.getReg());
    O << ", ";
    printRegName(O, MO1.getReg());

    O << ", ";
    printRegName(O, MO2.getReg());
    assert(ARM_AM::getSORegOffset(MO3.getImm()) == 0);
    printAnnotation(O, Annot);
    return;
  }

  case ARM::MOVsi: {
    // FIXME: Thumb variants?
    const MCOperand &Dst = MI->getOperand(0);
    const MCOperand &MO1 = MI->getOperand(1);
    const MCOperand &MO2 = MI->getOperand(2);

    O << '\t' << ARM_AM::getShiftOpcStr(ARM_AM::getSORegShOp(MO2.getImm()));
    printSBitModifierOperand(MI, 5, STI, O);
    printPredicateOperand(MI, 3, STI, O);

    O << '\t';
    printRegName(O, Dst.getReg());
    O << ", ";
    printRegName(O, MO1.getReg());

    if (ARM_AM::getSORegShOp(MO2.getImm()) == ARM_AM::rrx) {
      printAnnotation(O, Annot);
      return;
    }

    O << ", ";
    markup(O, Markup::Immediate)
        << "#" << translateShiftImm(ARM_AM::getSORegOffset(MO2.getImm()));
    printAnnotation(O, Annot);
    return;
  }

  // A8.6.123 PUSH
  case ARM::STMDB_UPD:
  case ARM::t2STMDB_UPD:
    if (MI->getOperand(0).getReg() == ARM::SP && MI->getNumOperands() > 5) {
      // Should only print PUSH if there are at least two registers in the list.
      O << '\t' << "push";
      printPredicateOperand(MI, 2, STI, O);
      if (Opcode == ARM::t2STMDB_UPD)
        O << ".w";
      O << '\t';
      printRegisterList(MI, 4, STI, O);
      printAnnotation(O, Annot);
      return;
    } else
      break;

  case ARM::STR_PRE_IMM:
    if (MI->getOperand(2).getReg() == ARM::SP &&
        MI->getOperand(3).getImm() == -4) {
      O << '\t' << "push";
      printPredicateOperand(MI, 4, STI, O);
      O << "\t{";
      printRegName(O, MI->getOperand(1).getReg());
      O << "}";
      printAnnotation(O, Annot);
      return;
    } else
      break;

  // A8.6.122 POP
  case ARM::LDMIA_UPD:
  case ARM::t2LDMIA_UPD:
    if (MI->getOperand(0).getReg() == ARM::SP && MI->getNumOperands() > 5) {
      // Should only print POP if there are at least two registers in the list.
      O << '\t' << "pop";
      printPredicateOperand(MI, 2, STI, O);
      if (Opcode == ARM::t2LDMIA_UPD)
        O << ".w";
      O << '\t';
      printRegisterList(MI, 4, STI, O);
      printAnnotation(O, Annot);
      return;
    } else
      break;

  case ARM::LDR_POST_IMM:
    if (MI->getOperand(2).getReg() == ARM::SP &&
        MI->getOperand(4).getImm() == 4) {
      O << '\t' << "pop";
      printPredicateOperand(MI, 5, STI, O);
      O << "\t{";
      printRegName(O, MI->getOperand(0).getReg());
      O << "}";
      printAnnotation(O, Annot);
      return;
    } else
      break;

  // A8.6.355 VPUSH
  case ARM::VSTMSDB_UPD:
  case ARM::VSTMDDB_UPD:
    if (MI->getOperand(0).getReg() == ARM::SP) {
      O << '\t' << "vpush";
      printPredicateOperand(MI, 2, STI, O);
      O << '\t';
      printRegisterList(MI, 4, STI, O);
      printAnnotation(O, Annot);
      return;
    } else
      break;

  // A8.6.354 VPOP
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMDIA_UPD:
    if (MI->getOperand(0).getReg() == ARM::SP) {
      O << '\t' << "vpop";
      printPredicateOperand(MI, 2, STI, O);
      O << '\t';
      printRegisterList(MI, 4, STI, O);
      printAnnotation(O, Annot);
      return;
    } else
      break;

  case ARM::tLDMIA: {
    bool Writeback = true;
    unsigned BaseReg = MI->getOperand(0).getReg();
    for (unsigned i = 3; i < MI->getNumOperands(); ++i) {
      if (MI->getOperand(i).getReg() == BaseReg)
        Writeback = false;
    }

    O << "\tldm";

    printPredicateOperand(MI, 1, STI, O);
    O << '\t';
    printRegName(O, BaseReg);
    if (Writeback)
      O << "!";
    O << ", ";
    printRegisterList(MI, 3, STI, O);
    printAnnotation(O, Annot);
    return;
  }

  // Combine 2 GPRs from disassember into a GPRPair to match with instr def.
  // ldrexd/strexd require even/odd GPR pair. To enforce this constraint,
  // a single GPRPair reg operand is used in the .td file to replace the two
  // GPRs. However, when decoding them, the two GRPs cannot be automatically
  // expressed as a GPRPair, so we have to manually merge them.
  // FIXME: We would really like to be able to tablegen'erate this.
  case ARM::LDREXD:
  case ARM::STREXD:
  case ARM::LDAEXD:
  case ARM::STLEXD: {
    const MCRegisterClass &MRC = MRI.getRegClass(ARM::GPRRegClassID);
    bool isStore = Opcode == ARM::STREXD || Opcode == ARM::STLEXD;
    unsigned Reg = MI->getOperand(isStore ? 1 : 0).getReg();
    if (MRC.contains(Reg)) {
      MCInst NewMI;
      MCOperand NewReg;
      NewMI.setOpcode(Opcode);

      if (isStore)
        NewMI.addOperand(MI->getOperand(0));
      NewReg = MCOperand::createReg(MRI.getMatchingSuperReg(
          Reg, ARM::gsub_0, &MRI.getRegClass(ARM::GPRPairRegClassID)));
      NewMI.addOperand(NewReg);

      // Copy the rest operands into NewMI.
      for (unsigned i = isStore ? 3 : 2; i < MI->getNumOperands(); ++i)
        NewMI.addOperand(MI->getOperand(i));
      printInstruction(&NewMI, Address, STI, O);
      return;
    }
    break;
  }
  case ARM::TSB:
  case ARM::t2TSB:
    O << "\ttsb\tcsync";
    return;
  case ARM::t2DSB:
    switch (MI->getOperand(0).getImm()) {
    default:
      if (!printAliasInstr(MI, Address, STI, O))
        printInstruction(MI, Address, STI, O);
      break;
    case 0:
      O << "\tssbb";
      break;
    case 4:
      O << "\tpssbb";
      break;
    }
    printAnnotation(O, Annot);
    return;
  }

  if (!printAliasInstr(MI, Address, STI, O))
    printInstruction(MI, Address, STI, O);

  printAnnotation(O, Annot);
}

void ARMInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                  const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    unsigned Reg = Op.getReg();
    printRegName(O, Reg);
  } else if (Op.isImm()) {
    markup(O, Markup::Immediate) << '#' << formatImm(Op.getImm());
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    const MCExpr *Expr = Op.getExpr();
    switch (Expr->getKind()) {
    case MCExpr::Binary:
      O << '#';
      Expr->print(O, &MAI);
      break;
    case MCExpr::Constant: {
      // If a symbolic branch target was added as a constant expression then
      // print that address in hex. And only print 32 unsigned bits for the
      // address.
      const MCConstantExpr *Constant = cast<MCConstantExpr>(Expr);
      int64_t TargetAddress;
      if (!Constant->evaluateAsAbsolute(TargetAddress)) {
        O << '#';
        Expr->print(O, &MAI);
      } else {
        O << "0x";
        O.write_hex(static_cast<uint32_t>(TargetAddress));
      }
      break;
    }
    default:
      // FIXME: Should we always treat this as if it is a constant literal and
      // prefix it with '#'?
      Expr->print(O, &MAI);
      break;
    }
  }
}

void ARMInstPrinter::printOperand(const MCInst *MI, uint64_t Address,
                                  unsigned OpNum, const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  if (!Op.isImm() || !PrintBranchImmAsAddress || getUseMarkup())
    return printOperand(MI, OpNum, STI, O);
  uint64_t Target = ARM_MC::evaluateBranchTarget(MII.get(MI->getOpcode()),
                                                 Address, Op.getImm());
  Target &= 0xffffffff;
  O << formatHex(Target);
  if (CommentStream)
    *CommentStream << "imm = #" << formatImm(Op.getImm()) << '\n';
}

void ARMInstPrinter::printThumbLdrLabelOperand(const MCInst *MI, unsigned OpNum,
                                               const MCSubtargetInfo &STI,
                                               raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  if (MO1.isExpr()) {
    MO1.getExpr()->print(O, &MAI);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[pc, ";

  int32_t OffImm = (int32_t)MO1.getImm();
  bool isSub = OffImm < 0;

  // Special value for #-0. All others are normal.
  if (OffImm == INT32_MIN)
    OffImm = 0;
  if (isSub) {
    markup(O, Markup::Immediate) << "#-" << formatImm(-OffImm);
  } else {
    markup(O, Markup::Immediate) << "#" << formatImm(OffImm);
  }
  O << "]";
}

// so_reg is a 4-operand unit corresponding to register forms of the A5.1
// "Addressing Mode 1 - Data-processing operands" forms.  This includes:
//    REG 0   0           - e.g. R5
//    REG REG 0,SH_OPC    - e.g. R5, ROR R3
//    REG 0   IMM,SH_OPC  - e.g. R5, LSL #3
void ARMInstPrinter::printSORegRegOperand(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);
  const MCOperand &MO3 = MI->getOperand(OpNum + 2);

  printRegName(O, MO1.getReg());

  // Print the shift opc.
  ARM_AM::ShiftOpc ShOpc = ARM_AM::getSORegShOp(MO3.getImm());
  O << ", " << ARM_AM::getShiftOpcStr(ShOpc);
  if (ShOpc == ARM_AM::rrx)
    return;

  O << ' ';
  printRegName(O, MO2.getReg());
  assert(ARM_AM::getSORegOffset(MO3.getImm()) == 0);
}

void ARMInstPrinter::printSORegImmOperand(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  printRegName(O, MO1.getReg());

  // Print the shift opc.
  printRegImmShift(O, ARM_AM::getSORegShOp(MO2.getImm()),
                   ARM_AM::getSORegOffset(MO2.getImm()), *this);
}

//===--------------------------------------------------------------------===//
// Addressing Mode #2
//===--------------------------------------------------------------------===//

void ARMInstPrinter::printAM2PreOrOffsetIndexOp(const MCInst *MI, unsigned Op,
                                                const MCSubtargetInfo &STI,
                                                raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(Op);
  const MCOperand &MO2 = MI->getOperand(Op + 1);
  const MCOperand &MO3 = MI->getOperand(Op + 2);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  if (!MO2.getReg()) {
    if (ARM_AM::getAM2Offset(MO3.getImm())) { // Don't print +0.
      O << ", ";
      markup(O, Markup::Immediate)
          << "#" << ARM_AM::getAddrOpcStr(ARM_AM::getAM2Op(MO3.getImm()))
          << ARM_AM::getAM2Offset(MO3.getImm());
    }
    O << "]";
    return;
  }

  O << ", ";
  O << ARM_AM::getAddrOpcStr(ARM_AM::getAM2Op(MO3.getImm()));
  printRegName(O, MO2.getReg());

  printRegImmShift(O, ARM_AM::getAM2ShiftOpc(MO3.getImm()),
                   ARM_AM::getAM2Offset(MO3.getImm()), *this);
  O << "]";
}

void ARMInstPrinter::printAddrModeTBB(const MCInst *MI, unsigned Op,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(Op);
  const MCOperand &MO2 = MI->getOperand(Op + 1);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  O << ", ";
  printRegName(O, MO2.getReg());
  O << "]";
}

void ARMInstPrinter::printAddrModeTBH(const MCInst *MI, unsigned Op,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(Op);
  const MCOperand &MO2 = MI->getOperand(Op + 1);
  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  O << ", ";
  printRegName(O, MO2.getReg());
  O << ", lsl ";
  markup(O, Markup::Immediate) << "#1";
  O << "]";
}

void ARMInstPrinter::printAddrMode2Operand(const MCInst *MI, unsigned Op,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(Op);

  if (!MO1.isReg()) { // FIXME: This is for CP entries, but isn't right.
    printOperand(MI, Op, STI, O);
    return;
  }

#ifndef NDEBUG
  const MCOperand &MO3 = MI->getOperand(Op + 2);
  unsigned IdxMode = ARM_AM::getAM2IdxMode(MO3.getImm());
  assert(IdxMode != ARMII::IndexModePost && "Should be pre or offset index op");
#endif

  printAM2PreOrOffsetIndexOp(MI, Op, STI, O);
}

void ARMInstPrinter::printAddrMode2OffsetOperand(const MCInst *MI,
                                                 unsigned OpNum,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  if (!MO1.getReg()) {
    unsigned ImmOffs = ARM_AM::getAM2Offset(MO2.getImm());
    markup(O, Markup::Immediate)
        << '#' << ARM_AM::getAddrOpcStr(ARM_AM::getAM2Op(MO2.getImm()))
        << ImmOffs;
    return;
  }

  O << ARM_AM::getAddrOpcStr(ARM_AM::getAM2Op(MO2.getImm()));
  printRegName(O, MO1.getReg());

  printRegImmShift(O, ARM_AM::getAM2ShiftOpc(MO2.getImm()),
                   ARM_AM::getAM2Offset(MO2.getImm()), *this);
}

//===--------------------------------------------------------------------===//
// Addressing Mode #3
//===--------------------------------------------------------------------===//

void ARMInstPrinter::printAM3PreOrOffsetIndexOp(const MCInst *MI, unsigned Op,
                                                raw_ostream &O,
                                                bool AlwaysPrintImm0) {
  const MCOperand &MO1 = MI->getOperand(Op);
  const MCOperand &MO2 = MI->getOperand(Op + 1);
  const MCOperand &MO3 = MI->getOperand(Op + 2);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << '[';
  printRegName(O, MO1.getReg());

  if (MO2.getReg()) {
    O << ", " << getAddrOpcStr(ARM_AM::getAM3Op(MO3.getImm()));
    printRegName(O, MO2.getReg());
    O << ']';
    return;
  }

  // If the op is sub we have to print the immediate even if it is 0
  unsigned ImmOffs = ARM_AM::getAM3Offset(MO3.getImm());
  ARM_AM::AddrOpc op = ARM_AM::getAM3Op(MO3.getImm());

  if (AlwaysPrintImm0 || ImmOffs || (op == ARM_AM::sub)) {
    O << ", ";
    markup(O, Markup::Immediate) << "#" << ARM_AM::getAddrOpcStr(op) << ImmOffs;
  }
  O << ']';
}

template <bool AlwaysPrintImm0>
void ARMInstPrinter::printAddrMode3Operand(const MCInst *MI, unsigned Op,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(Op);
  if (!MO1.isReg()) { //  For label symbolic references.
    printOperand(MI, Op, STI, O);
    return;
  }

  assert(ARM_AM::getAM3IdxMode(MI->getOperand(Op + 2).getImm()) !=
             ARMII::IndexModePost &&
         "unexpected idxmode");
  printAM3PreOrOffsetIndexOp(MI, Op, O, AlwaysPrintImm0);
}

void ARMInstPrinter::printAddrMode3OffsetOperand(const MCInst *MI,
                                                 unsigned OpNum,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  if (MO1.getReg()) {
    O << getAddrOpcStr(ARM_AM::getAM3Op(MO2.getImm()));
    printRegName(O, MO1.getReg());
    return;
  }

  unsigned ImmOffs = ARM_AM::getAM3Offset(MO2.getImm());
  markup(O, Markup::Immediate)
      << '#' << ARM_AM::getAddrOpcStr(ARM_AM::getAM3Op(MO2.getImm()))
      << ImmOffs;
}

void ARMInstPrinter::printPostIdxImm8Operand(const MCInst *MI, unsigned OpNum,
                                             const MCSubtargetInfo &STI,
                                             raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  unsigned Imm = MO.getImm();
  markup(O, Markup::Immediate)
      << '#' << ((Imm & 256) ? "" : "-") << (Imm & 0xff);
}

void ARMInstPrinter::printPostIdxRegOperand(const MCInst *MI, unsigned OpNum,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  O << (MO2.getImm() ? "" : "-");
  printRegName(O, MO1.getReg());
}

void ARMInstPrinter::printPostIdxImm8s4Operand(const MCInst *MI, unsigned OpNum,
                                               const MCSubtargetInfo &STI,
                                               raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  unsigned Imm = MO.getImm();
  markup(O, Markup::Immediate)
      << '#' << ((Imm & 256) ? "" : "-") << ((Imm & 0xff) << 2);
}

template<int shift>
void ARMInstPrinter::printMveAddrModeRQOperand(const MCInst *MI, unsigned OpNum,
                                               const MCSubtargetInfo &STI,
                                               raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  O << ", ";
  printRegName(O, MO2.getReg());

  if (shift > 0)
    printRegImmShift(O, ARM_AM::uxtw, shift, *this);

  O << "]";
}

void ARMInstPrinter::printLdStmModeOperand(const MCInst *MI, unsigned OpNum,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  ARM_AM::AMSubMode Mode =
      ARM_AM::getAM4SubMode(MI->getOperand(OpNum).getImm());
  O << ARM_AM::getAMSubModeStr(Mode);
}

template <bool AlwaysPrintImm0>
void ARMInstPrinter::printAddrMode5Operand(const MCInst *MI, unsigned OpNum,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  if (!MO1.isReg()) { // FIXME: This is for CP entries, but isn't right.
    printOperand(MI, OpNum, STI, O);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  unsigned ImmOffs = ARM_AM::getAM5Offset(MO2.getImm());
  ARM_AM::AddrOpc Op = ARM_AM::getAM5Op(MO2.getImm());
  if (AlwaysPrintImm0 || ImmOffs || Op == ARM_AM::sub) {
    O << ", ";
    markup(O, Markup::Immediate)
        << "#" << ARM_AM::getAddrOpcStr(Op) << ImmOffs * 4;
  }
  O << "]";
}

template <bool AlwaysPrintImm0>
void ARMInstPrinter::printAddrMode5FP16Operand(const MCInst *MI, unsigned OpNum,
                                               const MCSubtargetInfo &STI,
                                               raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum+1);

  if (!MO1.isReg()) {   // FIXME: This is for CP entries, but isn't right.
    printOperand(MI, OpNum, STI, O);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  unsigned ImmOffs = ARM_AM::getAM5FP16Offset(MO2.getImm());
  unsigned Op = ARM_AM::getAM5FP16Op(MO2.getImm());
  if (AlwaysPrintImm0 || ImmOffs || Op == ARM_AM::sub) {
    O << ", ";
    markup(O, Markup::Immediate)
        << "#" << ARM_AM::getAddrOpcStr(ARM_AM::getAM5FP16Op(MO2.getImm()))
        << ImmOffs * 2;
  }
  O << "]";
}

void ARMInstPrinter::printAddrMode6Operand(const MCInst *MI, unsigned OpNum,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  if (MO2.getImm()) {
    O << ":" << (MO2.getImm() << 3);
  }
  O << "]";
}

void ARMInstPrinter::printAddrMode7Operand(const MCInst *MI, unsigned OpNum,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  O << "]";
}

void ARMInstPrinter::printAddrMode6OffsetOperand(const MCInst *MI,
                                                 unsigned OpNum,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (MO.getReg() == 0)
    O << "!";
  else {
    O << ", ";
    printRegName(O, MO.getReg());
  }
}

void ARMInstPrinter::printBitfieldInvMaskImmOperand(const MCInst *MI,
                                                    unsigned OpNum,
                                                    const MCSubtargetInfo &STI,
                                                    raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  uint32_t v = ~MO.getImm();
  int32_t lsb = llvm::countr_zero(v);
  int32_t width = llvm::bit_width(v) - lsb;
  assert(MO.isImm() && "Not a valid bf_inv_mask_imm value!");
  markup(O, Markup::Immediate) << '#' << lsb;
  O << ", ";
  markup(O, Markup::Immediate) << '#' << width;
}

void ARMInstPrinter::printMemBOption(const MCInst *MI, unsigned OpNum,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  unsigned val = MI->getOperand(OpNum).getImm();
  O << ARM_MB::MemBOptToString(val, STI.hasFeature(ARM::HasV8Ops));
}

void ARMInstPrinter::printInstSyncBOption(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  unsigned val = MI->getOperand(OpNum).getImm();
  O << ARM_ISB::InstSyncBOptToString(val);
}

void ARMInstPrinter::printTraceSyncBOption(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  unsigned val = MI->getOperand(OpNum).getImm();
  O << ARM_TSB::TraceSyncBOptToString(val);
}

void ARMInstPrinter::printShiftImmOperand(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  unsigned ShiftOp = MI->getOperand(OpNum).getImm();
  bool isASR = (ShiftOp & (1 << 5)) != 0;
  unsigned Amt = ShiftOp & 0x1f;
  if (isASR) {
    O << ", asr ";
    markup(O, Markup::Immediate) << "#" << (Amt == 0 ? 32 : Amt);
  } else if (Amt) {
    O << ", lsl ";
    markup(O, Markup::Immediate) << "#" << Amt;
  }
}

void ARMInstPrinter::printPKHLSLShiftImm(const MCInst *MI, unsigned OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNum).getImm();
  if (Imm == 0)
    return;
  assert(Imm > 0 && Imm < 32 && "Invalid PKH shift immediate value!");
  O << ", lsl ";
  markup(O, Markup::Immediate) << "#" << Imm;
}

void ARMInstPrinter::printPKHASRShiftImm(const MCInst *MI, unsigned OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNum).getImm();
  // A shift amount of 32 is encoded as 0.
  if (Imm == 0)
    Imm = 32;
  assert(Imm > 0 && Imm <= 32 && "Invalid PKH shift immediate value!");
  O << ", asr ";
  markup(O, Markup::Immediate) << "#" << Imm;
}

void ARMInstPrinter::printRegisterList(const MCInst *MI, unsigned OpNum,
                                       const MCSubtargetInfo &STI,
                                       raw_ostream &O) {
  if (MI->getOpcode() != ARM::t2CLRM) {
    assert(is_sorted(drop_begin(*MI, OpNum),
                     [&](const MCOperand &LHS, const MCOperand &RHS) {
                       return MRI.getEncodingValue(LHS.getReg()) <
                              MRI.getEncodingValue(RHS.getReg());
                     }));
  }

  O << "{";
  for (unsigned i = OpNum, e = MI->getNumOperands(); i != e; ++i) {
    if (i != OpNum)
      O << ", ";
    printRegName(O, MI->getOperand(i).getReg());
  }
  O << "}";
}

void ARMInstPrinter::printGPRPairOperand(const MCInst *MI, unsigned OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  unsigned Reg = MI->getOperand(OpNum).getReg();
  printRegName(O, MRI.getSubReg(Reg, ARM::gsub_0));
  O << ", ";
  printRegName(O, MRI.getSubReg(Reg, ARM::gsub_1));
}

void ARMInstPrinter::printSetendOperand(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  if (Op.getImm())
    O << "be";
  else
    O << "le";
}

void ARMInstPrinter::printCPSIMod(const MCInst *MI, unsigned OpNum,
                                  const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  O << ARM_PROC::IModToString(Op.getImm());
}

void ARMInstPrinter::printCPSIFlag(const MCInst *MI, unsigned OpNum,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  unsigned IFlags = Op.getImm();
  for (int i = 2; i >= 0; --i)
    if (IFlags & (1 << i))
      O << ARM_PROC::IFlagsToString(1 << i);

  if (IFlags == 0)
    O << "none";
}

void ARMInstPrinter::printMSRMaskOperand(const MCInst *MI, unsigned OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  const FeatureBitset &FeatureBits = STI.getFeatureBits();
  if (FeatureBits[ARM::FeatureMClass]) {

    unsigned SYSm = Op.getImm() & 0xFFF; // 12-bit SYSm
    unsigned Opcode = MI->getOpcode();

    // For writes, handle extended mask bits if the DSP extension is present.
    if (Opcode == ARM::t2MSR_M && FeatureBits[ARM::FeatureDSP]) {
      auto TheReg =ARMSysReg::lookupMClassSysRegBy12bitSYSmValue(SYSm);
      if (TheReg && TheReg->isInRequiredFeatures({ARM::FeatureDSP})) {
          O << TheReg->Name;
          return;
      }
    }

    // Handle the basic 8-bit mask.
    SYSm &= 0xff;
    if (Opcode == ARM::t2MSR_M && FeatureBits [ARM::HasV7Ops]) {
      // ARMv7-M deprecates using MSR APSR without a _<bits> qualifier as an
      // alias for MSR APSR_nzcvq.
      auto TheReg = ARMSysReg::lookupMClassSysRegAPSRNonDeprecated(SYSm);
      if (TheReg) {
          O << TheReg->Name;
          return;
      }
    }

    auto TheReg = ARMSysReg::lookupMClassSysRegBy8bitSYSmValue(SYSm);
    if (TheReg) {
      O << TheReg->Name;
      return;
    }

    O << SYSm;

    return;
  }

  // As special cases, CPSR_f, CPSR_s and CPSR_fs prefer printing as
  // APSR_nzcvq, APSR_g and APSRnzcvqg, respectively.
  unsigned SpecRegRBit = Op.getImm() >> 4;
  unsigned Mask = Op.getImm() & 0xf;

  if (!SpecRegRBit && (Mask == 8 || Mask == 4 || Mask == 12)) {
    O << "APSR_";
    switch (Mask) {
    default:
      llvm_unreachable("Unexpected mask value!");
    case 4:
      O << "g";
      return;
    case 8:
      O << "nzcvq";
      return;
    case 12:
      O << "nzcvqg";
      return;
    }
  }

  if (SpecRegRBit)
    O << "SPSR";
  else
    O << "CPSR";

  if (Mask) {
    O << '_';
    if (Mask & 8)
      O << 'f';
    if (Mask & 4)
      O << 's';
    if (Mask & 2)
      O << 'x';
    if (Mask & 1)
      O << 'c';
  }
}

void ARMInstPrinter::printBankedRegOperand(const MCInst *MI, unsigned OpNum,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  uint32_t Banked = MI->getOperand(OpNum).getImm();
  auto TheReg = ARMBankedReg::lookupBankedRegByEncoding(Banked);
  assert(TheReg && "invalid banked register operand");
  std::string Name = TheReg->Name;

  uint32_t isSPSR = (Banked & 0x20) >> 5;
  if (isSPSR)
    Name.replace(0, 4, "SPSR"); // convert 'spsr_' to 'SPSR_'
  O << Name;
}

void ARMInstPrinter::printPredicateOperand(const MCInst *MI, unsigned OpNum,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  ARMCC::CondCodes CC = (ARMCC::CondCodes)MI->getOperand(OpNum).getImm();
  // Handle the undefined 15 CC value here for printing so we don't abort().
  if ((unsigned)CC == 15)
    O << "<und>";
  else if (CC != ARMCC::AL)
    O << ARMCondCodeToString(CC);
}

void ARMInstPrinter::printMandatoryRestrictedPredicateOperand(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  if ((ARMCC::CondCodes)MI->getOperand(OpNum).getImm() == ARMCC::HS)
    O << "cs";
  else
    printMandatoryPredicateOperand(MI, OpNum, STI, O);
}

void ARMInstPrinter::printMandatoryPredicateOperand(const MCInst *MI,
                                                    unsigned OpNum,
                                                    const MCSubtargetInfo &STI,
                                                    raw_ostream &O) {
  ARMCC::CondCodes CC = (ARMCC::CondCodes)MI->getOperand(OpNum).getImm();
  O << ARMCondCodeToString(CC);
}

void ARMInstPrinter::printMandatoryInvertedPredicateOperand(const MCInst *MI,
                                                            unsigned OpNum,
                                                            const MCSubtargetInfo &STI,
                                                            raw_ostream &O) {
  ARMCC::CondCodes CC = (ARMCC::CondCodes)MI->getOperand(OpNum).getImm();
  O << ARMCondCodeToString(ARMCC::getOppositeCondition(CC));
}

void ARMInstPrinter::printSBitModifierOperand(const MCInst *MI, unsigned OpNum,
                                              const MCSubtargetInfo &STI,
                                              raw_ostream &O) {
  if (MI->getOperand(OpNum).getReg()) {
    assert(MI->getOperand(OpNum).getReg() == ARM::CPSR &&
           "Expect ARM CPSR register!");
    O << 's';
  }
}

void ARMInstPrinter::printNoHashImmediate(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  O << MI->getOperand(OpNum).getImm();
}

void ARMInstPrinter::printPImmediate(const MCInst *MI, unsigned OpNum,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  O << "p" << MI->getOperand(OpNum).getImm();
}

void ARMInstPrinter::printCImmediate(const MCInst *MI, unsigned OpNum,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  O << "c" << MI->getOperand(OpNum).getImm();
}

void ARMInstPrinter::printCoprocOptionImm(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  O << "{" << MI->getOperand(OpNum).getImm() << "}";
}

void ARMInstPrinter::printPCLabel(const MCInst *MI, unsigned OpNum,
                                  const MCSubtargetInfo &STI, raw_ostream &O) {
  llvm_unreachable("Unhandled PC-relative pseudo-instruction!");
}

template <unsigned scale>
void ARMInstPrinter::printAdrLabelOperand(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);

  if (MO.isExpr()) {
    MO.getExpr()->print(O, &MAI);
    return;
  }

  int32_t OffImm = (int32_t)MO.getImm() << scale;

  WithMarkup ScopedMarkup = markup(O, Markup::Immediate);
  if (OffImm == INT32_MIN)
    O << "#-0";
  else if (OffImm < 0)
    O << "#-" << -OffImm;
  else
    O << "#" << OffImm;
}

void ARMInstPrinter::printThumbS4ImmOperand(const MCInst *MI, unsigned OpNum,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  markup(O, Markup::Immediate)
      << "#" << formatImm(MI->getOperand(OpNum).getImm() * 4);
}

void ARMInstPrinter::printThumbSRImm(const MCInst *MI, unsigned OpNum,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNum).getImm();
  markup(O, Markup::Immediate) << "#" << formatImm((Imm == 0 ? 32 : Imm));
}

void ARMInstPrinter::printThumbITMask(const MCInst *MI, unsigned OpNum,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  // (3 - the number of trailing zeros) is the number of then / else.
  unsigned Mask = MI->getOperand(OpNum).getImm();
  unsigned NumTZ = llvm::countr_zero(Mask);
  assert(NumTZ <= 3 && "Invalid IT mask!");
  for (unsigned Pos = 3, e = NumTZ; Pos > e; --Pos) {
    if ((Mask >> Pos) & 1)
      O << 'e';
    else
      O << 't';
  }
}

void ARMInstPrinter::printThumbAddrModeRROperand(const MCInst *MI, unsigned Op,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(Op);
  const MCOperand &MO2 = MI->getOperand(Op + 1);

  if (!MO1.isReg()) { // FIXME: This is for CP entries, but isn't right.
    printOperand(MI, Op, STI, O);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  if (unsigned RegNum = MO2.getReg()) {
    O << ", ";
    printRegName(O, RegNum);
  }
  O << "]";
}

void ARMInstPrinter::printThumbAddrModeImm5SOperand(const MCInst *MI,
                                                    unsigned Op,
                                                    const MCSubtargetInfo &STI,
                                                    raw_ostream &O,
                                                    unsigned Scale) {
  const MCOperand &MO1 = MI->getOperand(Op);
  const MCOperand &MO2 = MI->getOperand(Op + 1);

  if (!MO1.isReg()) { // FIXME: This is for CP entries, but isn't right.
    printOperand(MI, Op, STI, O);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  if (unsigned ImmOffs = MO2.getImm()) {
    O << ", ";
    markup(O, Markup::Immediate) << "#" << formatImm(ImmOffs * Scale);
  }
  O << "]";
}

void ARMInstPrinter::printThumbAddrModeImm5S1Operand(const MCInst *MI,
                                                     unsigned Op,
                                                     const MCSubtargetInfo &STI,
                                                     raw_ostream &O) {
  printThumbAddrModeImm5SOperand(MI, Op, STI, O, 1);
}

void ARMInstPrinter::printThumbAddrModeImm5S2Operand(const MCInst *MI,
                                                     unsigned Op,
                                                     const MCSubtargetInfo &STI,
                                                     raw_ostream &O) {
  printThumbAddrModeImm5SOperand(MI, Op, STI, O, 2);
}

void ARMInstPrinter::printThumbAddrModeImm5S4Operand(const MCInst *MI,
                                                     unsigned Op,
                                                     const MCSubtargetInfo &STI,
                                                     raw_ostream &O) {
  printThumbAddrModeImm5SOperand(MI, Op, STI, O, 4);
}

void ARMInstPrinter::printThumbAddrModeSPOperand(const MCInst *MI, unsigned Op,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  printThumbAddrModeImm5SOperand(MI, Op, STI, O, 4);
}

// Constant shifts t2_so_reg is a 2-operand unit corresponding to the Thumb2
// register with shift forms.
// REG 0   0           - e.g. R5
// REG IMM, SH_OPC     - e.g. R5, LSL #3
void ARMInstPrinter::printT2SOOperand(const MCInst *MI, unsigned OpNum,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  unsigned Reg = MO1.getReg();
  printRegName(O, Reg);

  // Print the shift opc.
  assert(MO2.isImm() && "Not a valid t2_so_reg value!");
  printRegImmShift(O, ARM_AM::getSORegShOp(MO2.getImm()),
                   ARM_AM::getSORegOffset(MO2.getImm()), *this);
}

template <bool AlwaysPrintImm0>
void ARMInstPrinter::printAddrModeImm12Operand(const MCInst *MI, unsigned OpNum,
                                               const MCSubtargetInfo &STI,
                                               raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  if (!MO1.isReg()) { // FIXME: This is for CP entries, but isn't right.
    printOperand(MI, OpNum, STI, O);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  int32_t OffImm = (int32_t)MO2.getImm();
  bool isSub = OffImm < 0;
  // Special value for #-0. All others are normal.
  if (OffImm == INT32_MIN)
    OffImm = 0;
  if (isSub) {
    O << ", ";
    markup(O, Markup::Immediate) << "#-" << formatImm(-OffImm);
  } else if (AlwaysPrintImm0 || OffImm > 0) {
    O << ", ";
    markup(O, Markup::Immediate) << "#" << formatImm(OffImm);
  }
  O << "]";
}

template <bool AlwaysPrintImm0>
void ARMInstPrinter::printT2AddrModeImm8Operand(const MCInst *MI,
                                                unsigned OpNum,
                                                const MCSubtargetInfo &STI,
                                                raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  int32_t OffImm = (int32_t)MO2.getImm();
  bool isSub = OffImm < 0;
  // Don't print +0.
  if (OffImm == INT32_MIN)
    OffImm = 0;
  if (isSub) {
    O << ", ";
    markup(O, Markup::Immediate) << "#-" << -OffImm;
  } else if (AlwaysPrintImm0 || OffImm > 0) {
    O << ", ";
    markup(O, Markup::Immediate) << "#" << OffImm;
  }
  O << "]";
}

template <bool AlwaysPrintImm0>
void ARMInstPrinter::printT2AddrModeImm8s4Operand(const MCInst *MI,
                                                  unsigned OpNum,
                                                  const MCSubtargetInfo &STI,
                                                  raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  if (!MO1.isReg()) { //  For label symbolic references.
    printOperand(MI, OpNum, STI, O);
    return;
  }

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  int32_t OffImm = (int32_t)MO2.getImm();
  bool isSub = OffImm < 0;

  assert(((OffImm & 0x3) == 0) && "Not a valid immediate!");

  // Don't print +0.
  if (OffImm == INT32_MIN)
    OffImm = 0;
  if (isSub) {
    O << ", ";
    markup(O, Markup::Immediate) << "#-" << -OffImm;
  } else if (AlwaysPrintImm0 || OffImm > 0) {
    O << ", ";
    markup(O, Markup::Immediate) << "#" << OffImm;
  }
  O << "]";
}

void ARMInstPrinter::printT2AddrModeImm0_1020s4Operand(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());
  if (MO2.getImm()) {
    O << ", ";
    markup(O, Markup::Immediate) << "#" << formatImm(MO2.getImm() * 4);
  }
  O << "]";
}

void ARMInstPrinter::printT2AddrModeImm8OffsetOperand(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  int32_t OffImm = (int32_t)MO1.getImm();
  O << ", ";
  WithMarkup ScopedMarkup = markup(O, Markup::Immediate);
  if (OffImm == INT32_MIN)
    O << "#-0";
  else if (OffImm < 0)
    O << "#-" << -OffImm;
  else
    O << "#" << OffImm;
}

void ARMInstPrinter::printT2AddrModeImm8s4OffsetOperand(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  int32_t OffImm = (int32_t)MO1.getImm();

  assert(((OffImm & 0x3) == 0) && "Not a valid immediate!");

  O << ", ";
  WithMarkup ScopedMarkup = markup(O, Markup::Immediate);
  if (OffImm == INT32_MIN)
    O << "#-0";
  else if (OffImm < 0)
    O << "#-" << -OffImm;
  else
    O << "#" << OffImm;
}

void ARMInstPrinter::printT2AddrModeSoRegOperand(const MCInst *MI,
                                                 unsigned OpNum,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  const MCOperand &MO1 = MI->getOperand(OpNum);
  const MCOperand &MO2 = MI->getOperand(OpNum + 1);
  const MCOperand &MO3 = MI->getOperand(OpNum + 2);

  WithMarkup ScopedMarkup = markup(O, Markup::Memory);
  O << "[";
  printRegName(O, MO1.getReg());

  assert(MO2.getReg() && "Invalid so_reg load / store address!");
  O << ", ";
  printRegName(O, MO2.getReg());

  unsigned ShAmt = MO3.getImm();
  if (ShAmt) {
    assert(ShAmt <= 3 && "Not a valid Thumb2 addressing mode!");
    O << ", lsl ";
    markup(O, Markup::Immediate) << "#" << ShAmt;
  }
  O << "]";
}

void ARMInstPrinter::printFPImmOperand(const MCInst *MI, unsigned OpNum,
                                       const MCSubtargetInfo &STI,
                                       raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  markup(O, Markup::Immediate) << '#' << ARM_AM::getFPImmFloat(MO.getImm());
}

void ARMInstPrinter::printVMOVModImmOperand(const MCInst *MI, unsigned OpNum,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  unsigned EncodedImm = MI->getOperand(OpNum).getImm();
  unsigned EltBits;
  uint64_t Val = ARM_AM::decodeVMOVModImm(EncodedImm, EltBits);

  WithMarkup ScopedMarkup = markup(O, Markup::Immediate);
  O << "#0x";
  O.write_hex(Val);
}

void ARMInstPrinter::printImmPlusOneOperand(const MCInst *MI, unsigned OpNum,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNum).getImm();
  markup(O, Markup::Immediate) << "#" << formatImm(Imm + 1);
}

void ARMInstPrinter::printRotImmOperand(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  unsigned Imm = MI->getOperand(OpNum).getImm();
  if (Imm == 0)
    return;
  assert(Imm <= 3 && "illegal ror immediate!");
  O << ", ror ";
  markup(O, Markup::Immediate) << "#" << 8 * Imm;
}

void ARMInstPrinter::printModImmOperand(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  MCOperand Op = MI->getOperand(OpNum);

  // Support for fixups (MCFixup)
  if (Op.isExpr())
    return printOperand(MI, OpNum, STI, O);

  unsigned Bits = Op.getImm() & 0xFF;
  unsigned Rot = (Op.getImm() & 0xF00) >> 7;

  bool PrintUnsigned = false;
  switch (MI->getOpcode()) {
  case ARM::MOVi:
    // Movs to PC should be treated unsigned
    PrintUnsigned = (MI->getOperand(OpNum - 1).getReg() == ARM::PC);
    break;
  case ARM::MSRi:
    // Movs to special registers should be treated unsigned
    PrintUnsigned = true;
    break;
  }

  int32_t Rotated = llvm::rotr<uint32_t>(Bits, Rot);
  if (ARM_AM::getSOImmVal(Rotated) == Op.getImm()) {
    // #rot has the least possible value
    O << "#";
    if (PrintUnsigned)
      markup(O, Markup::Immediate) << static_cast<uint32_t>(Rotated);
    else
      markup(O, Markup::Immediate) << Rotated;
    return;
  }

  // Explicit #bits, #rot implied
  O << "#";
  markup(O, Markup::Immediate) << Bits;
  O << ", #";
  markup(O, Markup::Immediate) << Rot;
}

void ARMInstPrinter::printFBits16(const MCInst *MI, unsigned OpNum,
                                  const MCSubtargetInfo &STI, raw_ostream &O) {
  markup(O, Markup::Immediate) << "#" << 16 - MI->getOperand(OpNum).getImm();
}

void ARMInstPrinter::printFBits32(const MCInst *MI, unsigned OpNum,
                                  const MCSubtargetInfo &STI, raw_ostream &O) {
  markup(O, Markup::Immediate) << "#" << 32 - MI->getOperand(OpNum).getImm();
}

void ARMInstPrinter::printVectorIndex(const MCInst *MI, unsigned OpNum,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  O << "[" << MI->getOperand(OpNum).getImm() << "]";
}

void ARMInstPrinter::printVectorListOne(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "}";
}

void ARMInstPrinter::printVectorListTwo(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  unsigned Reg = MI->getOperand(OpNum).getReg();
  unsigned Reg0 = MRI.getSubReg(Reg, ARM::dsub_0);
  unsigned Reg1 = MRI.getSubReg(Reg, ARM::dsub_1);
  O << "{";
  printRegName(O, Reg0);
  O << ", ";
  printRegName(O, Reg1);
  O << "}";
}

void ARMInstPrinter::printVectorListTwoSpaced(const MCInst *MI, unsigned OpNum,
                                              const MCSubtargetInfo &STI,
                                              raw_ostream &O) {
  unsigned Reg = MI->getOperand(OpNum).getReg();
  unsigned Reg0 = MRI.getSubReg(Reg, ARM::dsub_0);
  unsigned Reg1 = MRI.getSubReg(Reg, ARM::dsub_2);
  O << "{";
  printRegName(O, Reg0);
  O << ", ";
  printRegName(O, Reg1);
  O << "}";
}

void ARMInstPrinter::printVectorListThree(const MCInst *MI, unsigned OpNum,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 1);
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << "}";
}

void ARMInstPrinter::printVectorListFour(const MCInst *MI, unsigned OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 1);
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 3);
  O << "}";
}

void ARMInstPrinter::printVectorListOneAllLanes(const MCInst *MI,
                                                unsigned OpNum,
                                                const MCSubtargetInfo &STI,
                                                raw_ostream &O) {
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "[]}";
}

void ARMInstPrinter::printVectorListTwoAllLanes(const MCInst *MI,
                                                unsigned OpNum,
                                                const MCSubtargetInfo &STI,
                                                raw_ostream &O) {
  unsigned Reg = MI->getOperand(OpNum).getReg();
  unsigned Reg0 = MRI.getSubReg(Reg, ARM::dsub_0);
  unsigned Reg1 = MRI.getSubReg(Reg, ARM::dsub_1);
  O << "{";
  printRegName(O, Reg0);
  O << "[], ";
  printRegName(O, Reg1);
  O << "[]}";
}

void ARMInstPrinter::printVectorListThreeAllLanes(const MCInst *MI,
                                                  unsigned OpNum,
                                                  const MCSubtargetInfo &STI,
                                                  raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 1);
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << "[]}";
}

void ARMInstPrinter::printVectorListFourAllLanes(const MCInst *MI,
                                                 unsigned OpNum,
                                                 const MCSubtargetInfo &STI,
                                                 raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 1);
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 3);
  O << "[]}";
}

void ARMInstPrinter::printVectorListTwoSpacedAllLanes(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  unsigned Reg = MI->getOperand(OpNum).getReg();
  unsigned Reg0 = MRI.getSubReg(Reg, ARM::dsub_0);
  unsigned Reg1 = MRI.getSubReg(Reg, ARM::dsub_2);
  O << "{";
  printRegName(O, Reg0);
  O << "[], ";
  printRegName(O, Reg1);
  O << "[]}";
}

void ARMInstPrinter::printVectorListThreeSpacedAllLanes(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 4);
  O << "[]}";
}

void ARMInstPrinter::printVectorListFourSpacedAllLanes(
    const MCInst *MI, unsigned OpNum, const MCSubtargetInfo &STI,
    raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 4);
  O << "[], ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 6);
  O << "[]}";
}

void ARMInstPrinter::printVectorListThreeSpaced(const MCInst *MI,
                                                unsigned OpNum,
                                                const MCSubtargetInfo &STI,
                                                raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 4);
  O << "}";
}

void ARMInstPrinter::printVectorListFourSpaced(const MCInst *MI, unsigned OpNum,
                                               const MCSubtargetInfo &STI,
                                               raw_ostream &O) {
  // Normally, it's not safe to use register enum values directly with
  // addition to get the next register, but for VFP registers, the
  // sort order is guaranteed because they're all of the form D<n>.
  O << "{";
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 2);
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 4);
  O << ", ";
  printRegName(O, MI->getOperand(OpNum).getReg() + 6);
  O << "}";
}

template<unsigned NumRegs>
void ARMInstPrinter::printMVEVectorList(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  unsigned Reg = MI->getOperand(OpNum).getReg();
  const char *Prefix = "{";
  for (unsigned i = 0; i < NumRegs; i++) {
    O << Prefix;
    printRegName(O, MRI.getSubReg(Reg, ARM::qsub_0 + i));
    Prefix = ", ";
  }
  O << "}";
}

template<int64_t Angle, int64_t Remainder>
void ARMInstPrinter::printComplexRotationOp(const MCInst *MI, unsigned OpNo,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  unsigned Val = MI->getOperand(OpNo).getImm();
  O << "#" << (Val * Angle) + Remainder;
}

void ARMInstPrinter::printVPTPredicateOperand(const MCInst *MI, unsigned OpNum,
                                              const MCSubtargetInfo &STI,
                                              raw_ostream &O) {
  ARMVCC::VPTCodes CC = (ARMVCC::VPTCodes)MI->getOperand(OpNum).getImm();
  if (CC != ARMVCC::None)
    O << ARMVPTPredToString(CC);
}

void ARMInstPrinter::printVPTMask(const MCInst *MI, unsigned OpNum,
                                  const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  // (3 - the number of trailing zeroes) is the number of them / else.
  unsigned Mask = MI->getOperand(OpNum).getImm();
  unsigned NumTZ = llvm::countr_zero(Mask);
  assert(NumTZ <= 3 && "Invalid VPT mask!");
  for (unsigned Pos = 3, e = NumTZ; Pos > e; --Pos) {
    bool T = ((Mask >> Pos) & 1) == 0;
    if (T)
      O << 't';
    else
      O << 'e';
  }
}

void ARMInstPrinter::printMveSaturateOp(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  uint32_t Val = MI->getOperand(OpNum).getImm();
  assert(Val <= 1 && "Invalid MVE saturate operand");
  O << "#" << (Val == 1 ? 48 : 64);
}
