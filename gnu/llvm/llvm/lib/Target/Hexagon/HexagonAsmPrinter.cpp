//===- HexagonAsmPrinter.cpp - Print machine instrs to Hexagon assembly ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to Hexagon assembly language. This printer is
// the output mechanism used by `llc'.
//
//===----------------------------------------------------------------------===//

#include "HexagonAsmPrinter.h"
#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetStreamer.h"
#include "MCTargetDesc/HexagonInstPrinter.h"
#include "MCTargetDesc/HexagonMCExpr.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "TargetInfo/HexagonTargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

using namespace llvm;

namespace llvm {

void HexagonLowerToMC(const MCInstrInfo &MCII, const MachineInstr *MI,
                      MCInst &MCB, HexagonAsmPrinter &AP);

} // end namespace llvm

#define DEBUG_TYPE "asm-printer"

// Given a scalar register return its pair.
inline static unsigned getHexagonRegisterPair(unsigned Reg,
      const MCRegisterInfo *RI) {
  assert(Hexagon::IntRegsRegClass.contains(Reg));
  unsigned Pair = *RI->superregs(Reg).begin();
  assert(Hexagon::DoubleRegsRegClass.contains(Pair));
  return Pair;
}

void HexagonAsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNo,
                                     raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNo);

  switch (MO.getType()) {
  default:
    llvm_unreachable ("<unknown operand type>");
  case MachineOperand::MO_Register:
    O << HexagonInstPrinter::getRegisterName(MO.getReg());
    return;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    return;
  case MachineOperand::MO_ConstantPoolIndex:
    GetCPISymbol(MO.getIndex())->print(O, MAI);
    return;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, O);
    return;
  }
}

// isBlockOnlyReachableByFallthrough - We need to override this since the
// default AsmPrinter does not print labels for any basic block that
// is only reachable by a fall through. That works for all cases except
// for the case in which the basic block is reachable by a fall through but
// through an indirect from a jump table. In this case, the jump table
// will contain a label not defined by AsmPrinter.
bool HexagonAsmPrinter::isBlockOnlyReachableByFallthrough(
      const MachineBasicBlock *MBB) const {
  if (MBB->hasAddressTaken())
    return false;
  return AsmPrinter::isBlockOnlyReachableByFallthrough(MBB);
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
bool HexagonAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                        const char *ExtraCode,
                                        raw_ostream &OS) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
    case 'L':
    case 'H': { // The highest-numbered register of a pair.
      const MachineOperand &MO = MI->getOperand(OpNo);
      const MachineFunction &MF = *MI->getParent()->getParent();
      const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
      if (!MO.isReg())
        return true;
      Register RegNumber = MO.getReg();
      // This should be an assert in the frontend.
      if (Hexagon::DoubleRegsRegClass.contains(RegNumber))
        RegNumber = TRI->getSubReg(RegNumber, ExtraCode[0] == 'L' ?
                                              Hexagon::isub_lo :
                                              Hexagon::isub_hi);
      OS << HexagonInstPrinter::getRegisterName(RegNumber);
      return false;
    }
    case 'I':
      // Write 'i' if an integer constant, otherwise nothing.  Used to print
      // addi vs add, etc.
      if (MI->getOperand(OpNo).isImm())
        OS << "i";
      return false;
    }
  }

  printOperand(MI, OpNo, OS);
  return false;
}

bool HexagonAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                              unsigned OpNo,
                                              const char *ExtraCode,
                                              raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier.

  const MachineOperand &Base  = MI->getOperand(OpNo);
  const MachineOperand &Offset = MI->getOperand(OpNo+1);

  if (Base.isReg())
    printOperand(MI, OpNo, O);
  else
    llvm_unreachable("Unimplemented");

  if (Offset.isImm()) {
    if (Offset.getImm())
      O << "+#" << Offset.getImm();
  } else {
    llvm_unreachable("Unimplemented");
  }

  return false;
}

static MCSymbol *smallData(AsmPrinter &AP, const MachineInstr &MI,
                           MCStreamer &OutStreamer, const MCOperand &Imm,
                           int AlignSize, const MCSubtargetInfo& STI) {
  MCSymbol *Sym;
  int64_t Value;
  if (Imm.getExpr()->evaluateAsAbsolute(Value)) {
    StringRef sectionPrefix;
    std::string ImmString;
    StringRef Name;
    if (AlignSize == 8) {
       Name = ".CONST_0000000000000000";
       sectionPrefix = ".gnu.linkonce.l8";
       ImmString = utohexstr(Value);
    } else {
       Name = ".CONST_00000000";
       sectionPrefix = ".gnu.linkonce.l4";
       ImmString = utohexstr(static_cast<uint32_t>(Value));
    }

    std::string symbolName =   // Yes, leading zeros are kept.
      Name.drop_back(ImmString.size()).str() + ImmString;
    std::string sectionName = sectionPrefix.str() + symbolName;

    MCSectionELF *Section = OutStreamer.getContext().getELFSection(
        sectionName, ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
    OutStreamer.switchSection(Section);

    Sym = AP.OutContext.getOrCreateSymbol(Twine(symbolName));
    if (Sym->isUndefined()) {
      OutStreamer.emitLabel(Sym);
      OutStreamer.emitSymbolAttribute(Sym, MCSA_Global);
      OutStreamer.emitIntValue(Value, AlignSize);
      OutStreamer.emitCodeAlignment(Align(AlignSize), &STI);
    }
  } else {
    assert(Imm.isExpr() && "Expected expression and found none");
    const MachineOperand &MO = MI.getOperand(1);
    assert(MO.isGlobal() || MO.isCPI() || MO.isJTI());
    MCSymbol *MOSymbol = nullptr;
    if (MO.isGlobal())
      MOSymbol = AP.getSymbol(MO.getGlobal());
    else if (MO.isCPI())
      MOSymbol = AP.GetCPISymbol(MO.getIndex());
    else if (MO.isJTI())
      MOSymbol = AP.GetJTISymbol(MO.getIndex());
    else
      llvm_unreachable("Unknown operand type!");

    StringRef SymbolName = MOSymbol->getName();
    std::string LitaName = ".CONST_" + SymbolName.str();

    MCSectionELF *Section = OutStreamer.getContext().getELFSection(
        ".lita", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);

    OutStreamer.switchSection(Section);
    Sym = AP.OutContext.getOrCreateSymbol(Twine(LitaName));
    if (Sym->isUndefined()) {
      OutStreamer.emitLabel(Sym);
      OutStreamer.emitSymbolAttribute(Sym, MCSA_Local);
      OutStreamer.emitValue(Imm.getExpr(), AlignSize);
      OutStreamer.emitCodeAlignment(Align(AlignSize), &STI);
    }
  }
  return Sym;
}

static MCInst ScaleVectorOffset(MCInst &Inst, unsigned OpNo,
                                unsigned VectorSize, MCContext &Ctx) {
  MCInst T;
  T.setOpcode(Inst.getOpcode());
  for (unsigned i = 0, n = Inst.getNumOperands(); i != n; ++i) {
    if (i != OpNo) {
      T.addOperand(Inst.getOperand(i));
      continue;
    }
    MCOperand &ImmOp = Inst.getOperand(i);
    const auto *HE = static_cast<const HexagonMCExpr*>(ImmOp.getExpr());
    int32_t V = cast<MCConstantExpr>(HE->getExpr())->getValue();
    auto *NewCE = MCConstantExpr::create(V / int32_t(VectorSize), Ctx);
    auto *NewHE = HexagonMCExpr::create(NewCE, Ctx);
    T.addOperand(MCOperand::createExpr(NewHE));
  }
  return T;
}

void HexagonAsmPrinter::HexagonProcessInstruction(MCInst &Inst,
                                                  const MachineInstr &MI) {
  MCInst &MappedInst = static_cast <MCInst &>(Inst);
  const MCRegisterInfo *RI = OutStreamer->getContext().getRegisterInfo();
  const MachineFunction &MF = *MI.getParent()->getParent();
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  unsigned VectorSize = HRI.getRegSizeInBits(Hexagon::HvxVRRegClass) / 8;

  switch (Inst.getOpcode()) {
  default:
    return;

  case Hexagon::A2_iconst: {
    Inst.setOpcode(Hexagon::A2_addi);
    MCOperand Reg = Inst.getOperand(0);
    MCOperand S16 = Inst.getOperand(1);
    HexagonMCInstrInfo::setMustNotExtend(*S16.getExpr());
    HexagonMCInstrInfo::setS27_2_reloc(*S16.getExpr());
    Inst.clear();
    Inst.addOperand(Reg);
    Inst.addOperand(MCOperand::createReg(Hexagon::R0));
    Inst.addOperand(S16);
    break;
  }

  case Hexagon::A2_tfrf: {
    const MCConstantExpr *Zero = MCConstantExpr::create(0, OutContext);
    Inst.setOpcode(Hexagon::A2_paddif);
    Inst.addOperand(MCOperand::createExpr(Zero));
    break;
  }

  case Hexagon::A2_tfrt: {
    const MCConstantExpr *Zero = MCConstantExpr::create(0, OutContext);
    Inst.setOpcode(Hexagon::A2_paddit);
    Inst.addOperand(MCOperand::createExpr(Zero));
    break;
  }

  case Hexagon::A2_tfrfnew: {
    const MCConstantExpr *Zero = MCConstantExpr::create(0, OutContext);
    Inst.setOpcode(Hexagon::A2_paddifnew);
    Inst.addOperand(MCOperand::createExpr(Zero));
    break;
  }

  case Hexagon::A2_tfrtnew: {
    const MCConstantExpr *Zero = MCConstantExpr::create(0, OutContext);
    Inst.setOpcode(Hexagon::A2_padditnew);
    Inst.addOperand(MCOperand::createExpr(Zero));
    break;
  }

  case Hexagon::A2_zxtb: {
    const MCConstantExpr *C255 = MCConstantExpr::create(255, OutContext);
    Inst.setOpcode(Hexagon::A2_andir);
    Inst.addOperand(MCOperand::createExpr(C255));
    break;
  }

  // "$dst = CONST64(#$src1)",
  case Hexagon::CONST64:
    if (!OutStreamer->hasRawTextSupport()) {
      const MCOperand &Imm = MappedInst.getOperand(1);
      MCSectionSubPair Current = OutStreamer->getCurrentSection();

      MCSymbol *Sym =
          smallData(*this, MI, *OutStreamer, Imm, 8, getSubtargetInfo());

      OutStreamer->switchSection(Current.first, Current.second);
      MCInst TmpInst;
      MCOperand &Reg = MappedInst.getOperand(0);
      TmpInst.setOpcode(Hexagon::L2_loadrdgp);
      TmpInst.addOperand(Reg);
      TmpInst.addOperand(MCOperand::createExpr(
                         MCSymbolRefExpr::create(Sym, OutContext)));
      MappedInst = TmpInst;

    }
    break;
  case Hexagon::CONST32:
    if (!OutStreamer->hasRawTextSupport()) {
      MCOperand &Imm = MappedInst.getOperand(1);
      MCSectionSubPair Current = OutStreamer->getCurrentSection();
      MCSymbol *Sym =
          smallData(*this, MI, *OutStreamer, Imm, 4, getSubtargetInfo());
      OutStreamer->switchSection(Current.first, Current.second);
      MCInst TmpInst;
      MCOperand &Reg = MappedInst.getOperand(0);
      TmpInst.setOpcode(Hexagon::L2_loadrigp);
      TmpInst.addOperand(Reg);
      TmpInst.addOperand(MCOperand::createExpr(HexagonMCExpr::create(
          MCSymbolRefExpr::create(Sym, OutContext), OutContext)));
      MappedInst = TmpInst;
    }
    break;

  // C2_pxfer_map maps to C2_or instruction. Though, it's possible to use
  // C2_or during instruction selection itself but it results
  // into suboptimal code.
  case Hexagon::C2_pxfer_map: {
    MCOperand &Ps = Inst.getOperand(1);
    MappedInst.setOpcode(Hexagon::C2_or);
    MappedInst.addOperand(Ps);
    return;
  }

  // Vector reduce complex multiply by scalar, Rt & 1 map to :hi else :lo
  // The insn is mapped from the 4 operand to the 3 operand raw form taking
  // 3 register pairs.
  case Hexagon::M2_vrcmpys_acc_s1: {
    MCOperand &Rt = Inst.getOperand(3);
    assert(Rt.isReg() && "Expected register and none was found");
    unsigned Reg = RI->getEncodingValue(Rt.getReg());
    if (Reg & 1)
      MappedInst.setOpcode(Hexagon::M2_vrcmpys_acc_s1_h);
    else
      MappedInst.setOpcode(Hexagon::M2_vrcmpys_acc_s1_l);
    Rt.setReg(getHexagonRegisterPair(Rt.getReg(), RI));
    return;
  }
  case Hexagon::M2_vrcmpys_s1: {
    MCOperand &Rt = Inst.getOperand(2);
    assert(Rt.isReg() && "Expected register and none was found");
    unsigned Reg = RI->getEncodingValue(Rt.getReg());
    if (Reg & 1)
      MappedInst.setOpcode(Hexagon::M2_vrcmpys_s1_h);
    else
      MappedInst.setOpcode(Hexagon::M2_vrcmpys_s1_l);
    Rt.setReg(getHexagonRegisterPair(Rt.getReg(), RI));
    return;
  }

  case Hexagon::M2_vrcmpys_s1rp: {
    MCOperand &Rt = Inst.getOperand(2);
    assert(Rt.isReg() && "Expected register and none was found");
    unsigned Reg = RI->getEncodingValue(Rt.getReg());
    if (Reg & 1)
      MappedInst.setOpcode(Hexagon::M2_vrcmpys_s1rp_h);
    else
      MappedInst.setOpcode(Hexagon::M2_vrcmpys_s1rp_l);
    Rt.setReg(getHexagonRegisterPair(Rt.getReg(), RI));
    return;
  }

  case Hexagon::A4_boundscheck: {
    MCOperand &Rs = Inst.getOperand(1);
    assert(Rs.isReg() && "Expected register and none was found");
    unsigned Reg = RI->getEncodingValue(Rs.getReg());
    if (Reg & 1) // Odd mapped to raw:hi, regpair is rodd:odd-1, like r3:2
      MappedInst.setOpcode(Hexagon::A4_boundscheck_hi);
    else         // raw:lo
      MappedInst.setOpcode(Hexagon::A4_boundscheck_lo);
    Rs.setReg(getHexagonRegisterPair(Rs.getReg(), RI));
    return;
  }

  case Hexagon::PS_call_nr:
    Inst.setOpcode(Hexagon::J2_call);
    break;

  case Hexagon::S5_asrhub_rnd_sat_goodsyntax: {
    MCOperand &MO = MappedInst.getOperand(2);
    int64_t Imm;
    MCExpr const *Expr = MO.getExpr();
    bool Success = Expr->evaluateAsAbsolute(Imm);
    assert(Success && "Expected immediate and none was found");
    (void)Success;
    MCInst TmpInst;
    if (Imm == 0) {
      TmpInst.setOpcode(Hexagon::S2_vsathub);
      TmpInst.addOperand(MappedInst.getOperand(0));
      TmpInst.addOperand(MappedInst.getOperand(1));
      MappedInst = TmpInst;
      return;
    }
    TmpInst.setOpcode(Hexagon::S5_asrhub_rnd_sat);
    TmpInst.addOperand(MappedInst.getOperand(0));
    TmpInst.addOperand(MappedInst.getOperand(1));
    const MCExpr *One = MCConstantExpr::create(1, OutContext);
    const MCExpr *Sub = MCBinaryExpr::createSub(Expr, One, OutContext);
    TmpInst.addOperand(
        MCOperand::createExpr(HexagonMCExpr::create(Sub, OutContext)));
    MappedInst = TmpInst;
    return;
  }

  case Hexagon::S5_vasrhrnd_goodsyntax:
  case Hexagon::S2_asr_i_p_rnd_goodsyntax: {
    MCOperand &MO2 = MappedInst.getOperand(2);
    MCExpr const *Expr = MO2.getExpr();
    int64_t Imm;
    bool Success = Expr->evaluateAsAbsolute(Imm);
    assert(Success && "Expected immediate and none was found");
    (void)Success;
    MCInst TmpInst;
    if (Imm == 0) {
      TmpInst.setOpcode(Hexagon::A2_combinew);
      TmpInst.addOperand(MappedInst.getOperand(0));
      MCOperand &MO1 = MappedInst.getOperand(1);
      unsigned High = RI->getSubReg(MO1.getReg(), Hexagon::isub_hi);
      unsigned Low = RI->getSubReg(MO1.getReg(), Hexagon::isub_lo);
      // Add a new operand for the second register in the pair.
      TmpInst.addOperand(MCOperand::createReg(High));
      TmpInst.addOperand(MCOperand::createReg(Low));
      MappedInst = TmpInst;
      return;
    }

    if (Inst.getOpcode() == Hexagon::S2_asr_i_p_rnd_goodsyntax)
      TmpInst.setOpcode(Hexagon::S2_asr_i_p_rnd);
    else
      TmpInst.setOpcode(Hexagon::S5_vasrhrnd);
    TmpInst.addOperand(MappedInst.getOperand(0));
    TmpInst.addOperand(MappedInst.getOperand(1));
    const MCExpr *One = MCConstantExpr::create(1, OutContext);
    const MCExpr *Sub = MCBinaryExpr::createSub(Expr, One, OutContext);
    TmpInst.addOperand(
        MCOperand::createExpr(HexagonMCExpr::create(Sub, OutContext)));
    MappedInst = TmpInst;
    return;
  }

  // if ("#u5==0") Assembler mapped to: "Rd=Rs"; else Rd=asr(Rs,#u5-1):rnd
  case Hexagon::S2_asr_i_r_rnd_goodsyntax: {
    MCOperand &MO = Inst.getOperand(2);
    MCExpr const *Expr = MO.getExpr();
    int64_t Imm;
    bool Success = Expr->evaluateAsAbsolute(Imm);
    assert(Success && "Expected immediate and none was found");
    (void)Success;
    MCInst TmpInst;
    if (Imm == 0) {
      TmpInst.setOpcode(Hexagon::A2_tfr);
      TmpInst.addOperand(MappedInst.getOperand(0));
      TmpInst.addOperand(MappedInst.getOperand(1));
      MappedInst = TmpInst;
      return;
    }
    TmpInst.setOpcode(Hexagon::S2_asr_i_r_rnd);
    TmpInst.addOperand(MappedInst.getOperand(0));
    TmpInst.addOperand(MappedInst.getOperand(1));
    const MCExpr *One = MCConstantExpr::create(1, OutContext);
    const MCExpr *Sub = MCBinaryExpr::createSub(Expr, One, OutContext);
    TmpInst.addOperand(
        MCOperand::createExpr(HexagonMCExpr::create(Sub, OutContext)));
    MappedInst = TmpInst;
    return;
  }

  // Translate a "$Rdd = #imm" to "$Rdd = combine(#[-1,0], #imm)"
  case Hexagon::A2_tfrpi: {
    MCInst TmpInst;
    MCOperand &Rdd = MappedInst.getOperand(0);
    MCOperand &MO = MappedInst.getOperand(1);

    TmpInst.setOpcode(Hexagon::A2_combineii);
    TmpInst.addOperand(Rdd);
    int64_t Imm;
    bool Success = MO.getExpr()->evaluateAsAbsolute(Imm);
    if (Success && Imm < 0) {
      const MCExpr *MOne = MCConstantExpr::create(-1, OutContext);
      const HexagonMCExpr *E = HexagonMCExpr::create(MOne, OutContext);
      TmpInst.addOperand(MCOperand::createExpr(E));
    } else {
      const MCExpr *Zero = MCConstantExpr::create(0, OutContext);
      const HexagonMCExpr *E = HexagonMCExpr::create(Zero, OutContext);
      TmpInst.addOperand(MCOperand::createExpr(E));
    }
    TmpInst.addOperand(MO);
    MappedInst = TmpInst;
    return;
  }

  // Translate a "$Rdd = $Rss" to "$Rdd = combine($Rs, $Rt)"
  case Hexagon::A2_tfrp: {
    MCOperand &MO = MappedInst.getOperand(1);
    unsigned High = RI->getSubReg(MO.getReg(), Hexagon::isub_hi);
    unsigned Low = RI->getSubReg(MO.getReg(), Hexagon::isub_lo);
    MO.setReg(High);
    // Add a new operand for the second register in the pair.
    MappedInst.addOperand(MCOperand::createReg(Low));
    MappedInst.setOpcode(Hexagon::A2_combinew);
    return;
  }

  case Hexagon::A2_tfrpt:
  case Hexagon::A2_tfrpf: {
    MCOperand &MO = MappedInst.getOperand(2);
    unsigned High = RI->getSubReg(MO.getReg(), Hexagon::isub_hi);
    unsigned Low = RI->getSubReg(MO.getReg(), Hexagon::isub_lo);
    MO.setReg(High);
    // Add a new operand for the second register in the pair.
    MappedInst.addOperand(MCOperand::createReg(Low));
    MappedInst.setOpcode((Inst.getOpcode() == Hexagon::A2_tfrpt)
                          ? Hexagon::C2_ccombinewt
                          : Hexagon::C2_ccombinewf);
    return;
  }

  case Hexagon::A2_tfrptnew:
  case Hexagon::A2_tfrpfnew: {
    MCOperand &MO = MappedInst.getOperand(2);
    unsigned High = RI->getSubReg(MO.getReg(), Hexagon::isub_hi);
    unsigned Low = RI->getSubReg(MO.getReg(), Hexagon::isub_lo);
    MO.setReg(High);
    // Add a new operand for the second register in the pair.
    MappedInst.addOperand(MCOperand::createReg(Low));
    MappedInst.setOpcode(Inst.getOpcode() == Hexagon::A2_tfrptnew
                            ? Hexagon::C2_ccombinewnewt
                            : Hexagon::C2_ccombinewnewf);
    return;
  }

  case Hexagon::M2_mpysmi: {
    MCOperand &Imm = MappedInst.getOperand(2);
    MCExpr const *Expr = Imm.getExpr();
    int64_t Value;
    bool Success = Expr->evaluateAsAbsolute(Value);
    assert(Success);
    (void)Success;
    if (Value < 0 && Value > -256) {
      MappedInst.setOpcode(Hexagon::M2_mpysin);
      Imm.setExpr(HexagonMCExpr::create(
          MCUnaryExpr::createMinus(Expr, OutContext), OutContext));
    } else
      MappedInst.setOpcode(Hexagon::M2_mpysip);
    return;
  }

  case Hexagon::A2_addsp: {
    MCOperand &Rt = Inst.getOperand(1);
    assert(Rt.isReg() && "Expected register and none was found");
    unsigned Reg = RI->getEncodingValue(Rt.getReg());
    if (Reg & 1)
      MappedInst.setOpcode(Hexagon::A2_addsph);
    else
      MappedInst.setOpcode(Hexagon::A2_addspl);
    Rt.setReg(getHexagonRegisterPair(Rt.getReg(), RI));
    return;
  }

  case Hexagon::V6_vd0: {
    MCInst TmpInst;
    assert(Inst.getOperand(0).isReg() &&
           "Expected register and none was found");

    TmpInst.setOpcode(Hexagon::V6_vxor);
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(0));
    MappedInst = TmpInst;
    return;
  }

  case Hexagon::V6_vdd0: {
    MCInst TmpInst;
    assert (Inst.getOperand(0).isReg() &&
            "Expected register and none was found");

    TmpInst.setOpcode(Hexagon::V6_vsubw_dv);
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(0));
    MappedInst = TmpInst;
    return;
  }

  case Hexagon::V6_vL32Ub_pi:
  case Hexagon::V6_vL32b_cur_pi:
  case Hexagon::V6_vL32b_nt_cur_pi:
  case Hexagon::V6_vL32b_pi:
  case Hexagon::V6_vL32b_nt_pi:
  case Hexagon::V6_vL32b_nt_tmp_pi:
  case Hexagon::V6_vL32b_tmp_pi:
    MappedInst = ScaleVectorOffset(Inst, 3, VectorSize, OutContext);
    return;

  case Hexagon::V6_vL32Ub_ai:
  case Hexagon::V6_vL32b_ai:
  case Hexagon::V6_vL32b_cur_ai:
  case Hexagon::V6_vL32b_nt_ai:
  case Hexagon::V6_vL32b_nt_cur_ai:
  case Hexagon::V6_vL32b_nt_tmp_ai:
  case Hexagon::V6_vL32b_tmp_ai:
    MappedInst = ScaleVectorOffset(Inst, 2, VectorSize, OutContext);
    return;

  case Hexagon::V6_vS32Ub_pi:
  case Hexagon::V6_vS32b_new_pi:
  case Hexagon::V6_vS32b_nt_new_pi:
  case Hexagon::V6_vS32b_nt_pi:
  case Hexagon::V6_vS32b_pi:
    MappedInst = ScaleVectorOffset(Inst, 2, VectorSize, OutContext);
    return;

  case Hexagon::V6_vS32Ub_ai:
  case Hexagon::V6_vS32b_ai:
  case Hexagon::V6_vS32b_new_ai:
  case Hexagon::V6_vS32b_nt_ai:
  case Hexagon::V6_vS32b_nt_new_ai:
    MappedInst = ScaleVectorOffset(Inst, 1, VectorSize, OutContext);
    return;

  case Hexagon::V6_vL32b_cur_npred_pi:
  case Hexagon::V6_vL32b_cur_pred_pi:
  case Hexagon::V6_vL32b_npred_pi:
  case Hexagon::V6_vL32b_nt_cur_npred_pi:
  case Hexagon::V6_vL32b_nt_cur_pred_pi:
  case Hexagon::V6_vL32b_nt_npred_pi:
  case Hexagon::V6_vL32b_nt_pred_pi:
  case Hexagon::V6_vL32b_nt_tmp_npred_pi:
  case Hexagon::V6_vL32b_nt_tmp_pred_pi:
  case Hexagon::V6_vL32b_pred_pi:
  case Hexagon::V6_vL32b_tmp_npred_pi:
  case Hexagon::V6_vL32b_tmp_pred_pi:
    MappedInst = ScaleVectorOffset(Inst, 4, VectorSize, OutContext);
    return;

  case Hexagon::V6_vL32b_cur_npred_ai:
  case Hexagon::V6_vL32b_cur_pred_ai:
  case Hexagon::V6_vL32b_npred_ai:
  case Hexagon::V6_vL32b_nt_cur_npred_ai:
  case Hexagon::V6_vL32b_nt_cur_pred_ai:
  case Hexagon::V6_vL32b_nt_npred_ai:
  case Hexagon::V6_vL32b_nt_pred_ai:
  case Hexagon::V6_vL32b_nt_tmp_npred_ai:
  case Hexagon::V6_vL32b_nt_tmp_pred_ai:
  case Hexagon::V6_vL32b_pred_ai:
  case Hexagon::V6_vL32b_tmp_npred_ai:
  case Hexagon::V6_vL32b_tmp_pred_ai:
    MappedInst = ScaleVectorOffset(Inst, 3, VectorSize, OutContext);
    return;

  case Hexagon::V6_vS32Ub_npred_pi:
  case Hexagon::V6_vS32Ub_pred_pi:
  case Hexagon::V6_vS32b_new_npred_pi:
  case Hexagon::V6_vS32b_new_pred_pi:
  case Hexagon::V6_vS32b_npred_pi:
  case Hexagon::V6_vS32b_nqpred_pi:
  case Hexagon::V6_vS32b_nt_new_npred_pi:
  case Hexagon::V6_vS32b_nt_new_pred_pi:
  case Hexagon::V6_vS32b_nt_npred_pi:
  case Hexagon::V6_vS32b_nt_nqpred_pi:
  case Hexagon::V6_vS32b_nt_pred_pi:
  case Hexagon::V6_vS32b_nt_qpred_pi:
  case Hexagon::V6_vS32b_pred_pi:
  case Hexagon::V6_vS32b_qpred_pi:
    MappedInst = ScaleVectorOffset(Inst, 3, VectorSize, OutContext);
    return;

  case Hexagon::V6_vS32Ub_npred_ai:
  case Hexagon::V6_vS32Ub_pred_ai:
  case Hexagon::V6_vS32b_new_npred_ai:
  case Hexagon::V6_vS32b_new_pred_ai:
  case Hexagon::V6_vS32b_npred_ai:
  case Hexagon::V6_vS32b_nqpred_ai:
  case Hexagon::V6_vS32b_nt_new_npred_ai:
  case Hexagon::V6_vS32b_nt_new_pred_ai:
  case Hexagon::V6_vS32b_nt_npred_ai:
  case Hexagon::V6_vS32b_nt_nqpred_ai:
  case Hexagon::V6_vS32b_nt_pred_ai:
  case Hexagon::V6_vS32b_nt_qpred_ai:
  case Hexagon::V6_vS32b_pred_ai:
  case Hexagon::V6_vS32b_qpred_ai:
    MappedInst = ScaleVectorOffset(Inst, 2, VectorSize, OutContext);
    return;

  // V65+
  case Hexagon::V6_vS32b_srls_ai:
    MappedInst = ScaleVectorOffset(Inst, 1, VectorSize, OutContext);
    return;

  case Hexagon::V6_vS32b_srls_pi:
    MappedInst = ScaleVectorOffset(Inst, 2, VectorSize, OutContext);
    return;
  }
}

/// Print out a single Hexagon MI to the current output stream.
void HexagonAsmPrinter::emitInstruction(const MachineInstr *MI) {
  Hexagon_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());

  MCInst MCB;
  MCB.setOpcode(Hexagon::BUNDLE);
  MCB.addOperand(MCOperand::createImm(0));
  const MCInstrInfo &MCII = *Subtarget->getInstrInfo();

  if (MI->isBundle()) {
    const MachineBasicBlock* MBB = MI->getParent();
    MachineBasicBlock::const_instr_iterator MII = MI->getIterator();

    for (++MII; MII != MBB->instr_end() && MII->isInsideBundle(); ++MII)
      if (!MII->isDebugInstr() && !MII->isImplicitDef())
        HexagonLowerToMC(MCII, &*MII, MCB, *this);
  } else {
    HexagonLowerToMC(MCII, MI, MCB, *this);
  }

  const MachineFunction &MF = *MI->getParent()->getParent();
  const auto &HII = *MF.getSubtarget<HexagonSubtarget>().getInstrInfo();
  if (MI->isBundle() && HII.getBundleNoShuf(*MI))
    HexagonMCInstrInfo::setMemReorderDisabled(MCB);

  MCContext &Ctx = OutStreamer->getContext();
  bool Ok = HexagonMCInstrInfo::canonicalizePacket(MCII, *Subtarget, Ctx,
                                                   MCB, nullptr);
  assert(Ok); (void)Ok;
  if (HexagonMCInstrInfo::bundleSize(MCB) == 0)
    return;
  OutStreamer->emitInstruction(MCB, getSubtargetInfo());
}

void HexagonAsmPrinter::emitStartOfAsmFile(Module &M) {
  if (TM.getTargetTriple().isOSBinFormatELF())
    emitAttributes();
}

void HexagonAsmPrinter::emitEndOfAsmFile(Module &M) {
  HexagonTargetStreamer &HTS =
      static_cast<HexagonTargetStreamer &>(*OutStreamer->getTargetStreamer());
  if (TM.getTargetTriple().isOSBinFormatELF())
    HTS.finishAttributeSection();
}

void HexagonAsmPrinter::emitAttributes() {
  HexagonTargetStreamer &HTS =
      static_cast<HexagonTargetStreamer &>(*OutStreamer->getTargetStreamer());
  HTS.emitTargetAttributes(*TM.getMCSubtargetInfo());
}

void HexagonAsmPrinter::EmitSled(const MachineInstr &MI, SledKind Kind) {
  static const int8_t NoopsInSledCount = 4;
  // We want to emit the following pattern:
  //
  // .L_xray_sled_N:
  // <xray_sled_base>:
  // { 	jump .Ltmp0 }
  // {  nop
  //    nop
  //    nop
  //    nop }
  // .Ltmp0:
  //
  // We need the 4 nop words because at runtime, we'd be patching over the
  // full 5 words with the following pattern:
  //
  // <xray_sled_n>:
  // { 	immext(#...) // upper 26-bits of trampoline
  //    r6 = ##...   // lower  6-bits of trampoline
  //    immext(#...) // upper 26-bits of func id
  //    r7 = ##... }  // lower 6 bits of func id
  // { 	callr r6 }
  //
  //
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->emitLabel(CurSled);

  MCInst *SledJump = new (OutContext) MCInst();
  SledJump->setOpcode(Hexagon::J2_jump);
  auto PostSled = OutContext.createTempSymbol();
  SledJump->addOperand(MCOperand::createExpr(HexagonMCExpr::create(
      MCSymbolRefExpr::create(PostSled, OutContext), OutContext)));

  // Emit "jump PostSled" instruction, which jumps over the nop series.
  MCInst SledJumpPacket;
  SledJumpPacket.setOpcode(Hexagon::BUNDLE);
  SledJumpPacket.addOperand(MCOperand::createImm(0));
  SledJumpPacket.addOperand(MCOperand::createInst(SledJump));

  EmitToStreamer(*OutStreamer, SledJumpPacket);

  // FIXME: this will emit individual packets, we should
  // special-case this and combine them into a single packet.
  emitNops(NoopsInSledCount);

  OutStreamer->emitLabel(PostSled);
  recordSled(CurSled, MI, Kind, 2);
}

void HexagonAsmPrinter::LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI) {
  EmitSled(MI, SledKind::FUNCTION_ENTER);
}

void HexagonAsmPrinter::LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI) {
  EmitSled(MI, SledKind::FUNCTION_EXIT);
}

void HexagonAsmPrinter::LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI) {
  EmitSled(MI, SledKind::TAIL_CALL);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHexagonAsmPrinter() {
  RegisterAsmPrinter<HexagonAsmPrinter> X(getTheHexagonTarget());
}
