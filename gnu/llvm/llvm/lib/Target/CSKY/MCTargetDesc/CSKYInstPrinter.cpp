//===-- CSKYInstPrinter.cpp - Convert CSKY MCInst to asm syntax ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an CSKY MCInst to a .s file.
//
//===----------------------------------------------------------------------===//
#include "CSKYInstPrinter.h"
#include "MCTargetDesc/CSKYBaseInfo.h"
#include "MCTargetDesc/CSKYMCExpr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

#define DEBUG_TYPE "csky-asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "CSKYGenAsmWriter.inc"

static cl::opt<bool>
    NoAliases("csky-no-aliases",
              cl::desc("Disable the emission of assembler pseudo instructions"),
              cl::init(false), cl::Hidden);

static cl::opt<bool>
    ArchRegNames("csky-arch-reg-names",
                 cl::desc("Print architectural register names rather than the "
                          "ABI names (such as r14 instead of sp)"),
                 cl::init(false), cl::Hidden);

// The command-line flags above are used by llvm-mc and llc. They can be used by
// `llvm-objdump`, but we override their values here to handle options passed to
// `llvm-objdump` with `-M` (which matches GNU objdump). There did not seem to
// be an easier way to allow these options in all these tools, without doing it
// this way.
bool CSKYInstPrinter::applyTargetSpecificCLOption(StringRef Opt) {
  if (Opt == "no-aliases") {
    NoAliases = true;
    return true;
  }
  if (Opt == "numeric") {
    ArchRegNames = true;
    return true;
  }
  if (Opt == "debug") {
    DebugFlag = true;
    return true;
  }
  if (Opt == "abi-names") {
    ABIRegNames = true;
    return true;
  }

  return false;
}

void CSKYInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  const MCInst *NewMI = MI;

  if (NoAliases || !printAliasInstr(NewMI, Address, STI, O))
    printInstruction(NewMI, Address, STI, O);
  printAnnotation(O, Annot);
}

void CSKYInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) const {
  if (PrintBranchImmAsAddress)
    O << getRegisterName(Reg, ABIRegNames ? CSKY::ABIRegAltName
                                          : CSKY::NoRegAltName);
  else
    O << getRegisterName(Reg);
}

void CSKYInstPrinter::printFPRRegName(raw_ostream &O, unsigned RegNo) const {
  if (PrintBranchImmAsAddress)
    O << getRegisterName(RegNo, CSKY::NoRegAltName);
  else
    O << getRegisterName(RegNo);
}

void CSKYInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O,
                                   const char *Modifier) {
  assert((Modifier == 0 || Modifier[0] == 0) && "No modifiers supported");
  const MCOperand &MO = MI->getOperand(OpNo);

  if (MO.isReg()) {
    unsigned Reg = MO.getReg();
    bool useABIName = false;
    if (PrintBranchImmAsAddress)
      useABIName = ABIRegNames;
    else
      useABIName = !ArchRegNames;

    if (Reg == CSKY::C)
      O << "";
    else if (STI.hasFeature(CSKY::FeatureJAVA)) {
      if (Reg == CSKY::R23)
        O << (useABIName ? "fp" : "r23");
      else if (Reg == CSKY::R24)
        O << (useABIName ? "top" : "r24");
      else if (Reg == CSKY::R25)
        O << (useABIName ? "bsp" : "r25");
      else
        printRegName(O, Reg);
    } else
      printRegName(O, Reg);

    return;
  }

  if (MO.isImm()) {
    uint64_t TSFlags = MII.get(MI->getOpcode()).TSFlags;

    if (((TSFlags & CSKYII::AddrModeMask) != CSKYII::AddrModeNone) &&
        PrintBranchImmAsAddress)
      O << formatHex(MO.getImm());
    else
      O << MO.getImm();
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MO.getExpr()->print(O, &MAI);
}

void CSKYInstPrinter::printDataSymbol(const MCInst *MI, unsigned OpNo,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  O << "[";
  if (MO.isImm())
    O << MO.getImm();
  else
    MO.getExpr()->print(O, &MAI);
  O << "]";
}

void CSKYInstPrinter::printConstpool(const MCInst *MI, uint64_t Address,
                                     unsigned OpNo, const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  if (MO.isImm()) {
    if (PrintBranchImmAsAddress) {
      uint64_t Target = Address + MO.getImm();
      Target &= 0xfffffffc;
      O << formatHex(Target);
    } else {
      O << MO.getImm();
    }
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printConstpool");

  O << "[";
  MO.getExpr()->print(O, &MAI);
  O << "]";
}

void CSKYInstPrinter::printCSKYSymbolOperand(const MCInst *MI, uint64_t Address,
                                             unsigned OpNo,
                                             const MCSubtargetInfo &STI,
                                             raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  if (!MO.isImm()) {
    return printOperand(MI, OpNo, STI, O);
  }

  if (PrintBranchImmAsAddress) {
    uint64_t Target = Address + MO.getImm();
    Target &= 0xffffffff;
    O << formatHex(Target);
  } else {
    O << MO.getImm();
  }
}

void CSKYInstPrinter::printPSRFlag(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  auto V = MI->getOperand(OpNo).getImm();

  ListSeparator LS;

  if ((V >> 3) & 0x1)
    O << LS << "ee";
  if ((V >> 2) & 0x1)
    O << LS << "ie";
  if ((V >> 1) & 0x1)
    O << LS << "fe";
  if ((V >> 0) & 0x1)
    O << LS << "af";
}

void CSKYInstPrinter::printRegisterSeq(const MCInst *MI, unsigned OpNum,
                                       const MCSubtargetInfo &STI,
                                       raw_ostream &O) {
  printRegName(O, MI->getOperand(OpNum).getReg());
  O << "-";
  printRegName(O, MI->getOperand(OpNum + 1).getReg());
}

void CSKYInstPrinter::printRegisterList(const MCInst *MI, unsigned OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  auto V = MI->getOperand(OpNum).getImm();
  ListSeparator LS;

  if (V & 0xf) {
    O << LS;
    printRegName(O, CSKY::R4);
    auto Offset = (V & 0xf) - 1;
    if (Offset) {
      O << "-";
      printRegName(O, CSKY::R4 + Offset);
    }
  }

  if ((V >> 4) & 0x1) {
    O << LS;
    printRegName(O, CSKY::R15);
  }

  if ((V >> 5) & 0x7) {
    O << LS;
    printRegName(O, CSKY::R16);

    auto Offset = ((V >> 5) & 0x7) - 1;

    if (Offset) {
      O << "-";
      printRegName(O, CSKY::R16 + Offset);
    }
  }

  if ((V >> 8) & 0x1) {
    O << LS;
    printRegName(O, CSKY::R28);
  }
}

const char *CSKYInstPrinter::getRegisterName(MCRegister Reg) {
  return getRegisterName(Reg, ArchRegNames ? CSKY::NoRegAltName
                                           : CSKY::ABIRegAltName);
}

void CSKYInstPrinter::printFPR(const MCInst *MI, unsigned OpNo,
                               const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  assert(MO.isReg());

  printFPRRegName(O, MO.getReg());
}
