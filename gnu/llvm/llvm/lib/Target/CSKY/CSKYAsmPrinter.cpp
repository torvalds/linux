//===-- CSKYAsmPrinter.cpp - CSKY LLVM assembly writer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the CSKY assembly language.
//
//===----------------------------------------------------------------------===//
#include "CSKYAsmPrinter.h"
#include "CSKY.h"
#include "CSKYConstantPoolValue.h"
#include "CSKYTargetMachine.h"
#include "MCTargetDesc/CSKYInstPrinter.h"
#include "MCTargetDesc/CSKYMCExpr.h"
#include "MCTargetDesc/CSKYTargetStreamer.h"
#include "TargetInfo/CSKYTargetInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "csky-asm-printer"

STATISTIC(CSKYNumInstrsCompressed,
          "Number of C-SKY Compressed instructions emitted");

CSKYAsmPrinter::CSKYAsmPrinter(llvm::TargetMachine &TM,
                               std::unique_ptr<llvm::MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), MCInstLowering(OutContext, *this) {}

bool CSKYAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  MCP = MF.getConstantPool();
  TII = MF.getSubtarget().getInstrInfo();

  // Set the current MCSubtargetInfo to a copy which has the correct
  // feature bits for the current MachineFunction
  MCSubtargetInfo &NewSTI =
      OutStreamer->getContext().getSubtargetCopy(*TM.getMCSubtargetInfo());
  NewSTI.setFeatureBits(MF.getSubtarget().getFeatureBits());
  Subtarget = &NewSTI;

  return AsmPrinter::runOnMachineFunction(MF);
}

#define GEN_COMPRESS_INSTR
#include "CSKYGenCompressInstEmitter.inc"
void CSKYAsmPrinter::EmitToStreamer(MCStreamer &S, const MCInst &Inst) {
  MCInst CInst;
  bool Res = compressInst(CInst, Inst, *Subtarget);
  if (Res)
    ++CSKYNumInstrsCompressed;
  AsmPrinter::EmitToStreamer(*OutStreamer, Res ? CInst : Inst);
}

// Simple pseudo-instructions have their lowering (with expansion to real
// instructions) auto-generated.
#include "CSKYGenMCPseudoLowering.inc"

void CSKYAsmPrinter::expandTLSLA(const MachineInstr *MI) {
  DebugLoc DL = MI->getDebugLoc();

  MCSymbol *PCLabel = OutContext.getOrCreateSymbol(
      Twine(MAI->getPrivateGlobalPrefix()) + "PC" + Twine(getFunctionNumber()) +
      "_" + Twine(MI->getOperand(3).getImm()));

  OutStreamer->emitLabel(PCLabel);

  auto Instr = BuildMI(*MF, DL, TII->get(CSKY::LRW32))
                   .add(MI->getOperand(0))
                   .add(MI->getOperand(2));
  MCInst LRWInst;
  MCInstLowering.Lower(Instr, LRWInst);
  EmitToStreamer(*OutStreamer, LRWInst);

  Instr = BuildMI(*MF, DL, TII->get(CSKY::GRS32))
              .add(MI->getOperand(1))
              .addSym(PCLabel);
  MCInst GRSInst;
  MCInstLowering.Lower(Instr, GRSInst);
  EmitToStreamer(*OutStreamer, GRSInst);
  return;
}

void CSKYAsmPrinter::emitCustomConstantPool(const MachineInstr *MI) {

  // This instruction represents a floating constant pool in the function.
  // The first operand is the ID# for this instruction, the second is the
  // index into the MachineConstantPool that this is, the third is the size
  // in bytes of this constant pool entry.
  // The required alignment is specified on the basic block holding this MI.
  unsigned LabelId = (unsigned)MI->getOperand(0).getImm();
  unsigned CPIdx = (unsigned)MI->getOperand(1).getIndex();

  // If this is the first entry of the pool, mark it.
  if (!InConstantPool) {
    OutStreamer->emitValueToAlignment(Align(4));
    InConstantPool = true;
  }

  OutStreamer->emitLabel(GetCPISymbol(LabelId));

  const MachineConstantPoolEntry &MCPE = MCP->getConstants()[CPIdx];
  if (MCPE.isMachineConstantPoolEntry())
    emitMachineConstantPoolValue(MCPE.Val.MachineCPVal);
  else
    emitGlobalConstant(MF->getDataLayout(), MCPE.Val.ConstVal);
  return;
}

void CSKYAsmPrinter::emitFunctionBodyEnd() {
  // Make sure to terminate any constant pools that were at the end
  // of the function.
  if (!InConstantPool)
    return;
  InConstantPool = false;
}

void CSKYAsmPrinter::emitStartOfAsmFile(Module &M) {
  if (TM.getTargetTriple().isOSBinFormatELF())
    emitAttributes();
}

void CSKYAsmPrinter::emitEndOfAsmFile(Module &M) {
  CSKYTargetStreamer &CTS =
      static_cast<CSKYTargetStreamer &>(*OutStreamer->getTargetStreamer());

  if (TM.getTargetTriple().isOSBinFormatELF())
    CTS.finishAttributeSection();
}

void CSKYAsmPrinter::emitInstruction(const MachineInstr *MI) {
  CSKY_MC::verifyInstructionPredicates(MI->getOpcode(),
                                       getSubtargetInfo().getFeatureBits());

  // Do any auto-generated pseudo lowerings.
  if (emitPseudoExpansionLowering(*OutStreamer, MI))
    return;

  // If we just ended a constant pool, mark it as such.
  if (InConstantPool && MI->getOpcode() != CSKY::CONSTPOOL_ENTRY) {
    InConstantPool = false;
  }

  if (MI->getOpcode() == CSKY::PseudoTLSLA32)
    return expandTLSLA(MI);

  if (MI->getOpcode() == CSKY::CONSTPOOL_ENTRY)
    return emitCustomConstantPool(MI);

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

// Convert a CSKY-specific constant pool modifier into the associated
// MCSymbolRefExpr variant kind.
static CSKYMCExpr::VariantKind
getModifierVariantKind(CSKYCP::CSKYCPModifier Modifier) {
  switch (Modifier) {
  case CSKYCP::NO_MOD:
    return CSKYMCExpr::VK_CSKY_None;
  case CSKYCP::ADDR:
    return CSKYMCExpr::VK_CSKY_ADDR;
  case CSKYCP::GOT:
    return CSKYMCExpr::VK_CSKY_GOT;
  case CSKYCP::GOTOFF:
    return CSKYMCExpr::VK_CSKY_GOTOFF;
  case CSKYCP::PLT:
    return CSKYMCExpr::VK_CSKY_PLT;
  case CSKYCP::TLSGD:
    return CSKYMCExpr::VK_CSKY_TLSGD;
  case CSKYCP::TLSLE:
    return CSKYMCExpr::VK_CSKY_TLSLE;
  case CSKYCP::TLSIE:
    return CSKYMCExpr::VK_CSKY_TLSIE;
  }
  llvm_unreachable("Invalid CSKYCPModifier!");
}

void CSKYAsmPrinter::emitMachineConstantPoolValue(
    MachineConstantPoolValue *MCPV) {
  int Size = getDataLayout().getTypeAllocSize(MCPV->getType());
  CSKYConstantPoolValue *CCPV = static_cast<CSKYConstantPoolValue *>(MCPV);
  MCSymbol *MCSym;

  if (CCPV->isBlockAddress()) {
    const BlockAddress *BA =
        cast<CSKYConstantPoolConstant>(CCPV)->getBlockAddress();
    MCSym = GetBlockAddressSymbol(BA);
  } else if (CCPV->isGlobalValue()) {
    const GlobalValue *GV = cast<CSKYConstantPoolConstant>(CCPV)->getGV();
    MCSym = getSymbol(GV);
  } else if (CCPV->isMachineBasicBlock()) {
    const MachineBasicBlock *MBB = cast<CSKYConstantPoolMBB>(CCPV)->getMBB();
    MCSym = MBB->getSymbol();
  } else if (CCPV->isJT()) {
    signed JTI = cast<CSKYConstantPoolJT>(CCPV)->getJTI();
    MCSym = GetJTISymbol(JTI);
  } else if (CCPV->isConstPool()) {
    const Constant *C = cast<CSKYConstantPoolConstant>(CCPV)->getConstantPool();
    MCSym = GetCPISymbol(MCP->getConstantPoolIndex(C, Align(4)));
  } else {
    assert(CCPV->isExtSymbol() && "unrecognized constant pool value");
    StringRef Sym = cast<CSKYConstantPoolSymbol>(CCPV)->getSymbol();
    MCSym = GetExternalSymbolSymbol(Sym);
  }
  // Create an MCSymbol for the reference.
  const MCExpr *Expr =
      MCSymbolRefExpr::create(MCSym, MCSymbolRefExpr::VK_None, OutContext);

  if (CCPV->getPCAdjustment()) {

    MCSymbol *PCLabel = OutContext.getOrCreateSymbol(
        Twine(MAI->getPrivateGlobalPrefix()) + "PC" +
        Twine(getFunctionNumber()) + "_" + Twine(CCPV->getLabelID()));

    const MCExpr *PCRelExpr = MCSymbolRefExpr::create(PCLabel, OutContext);
    if (CCPV->mustAddCurrentAddress()) {
      // We want "(<expr> - .)", but MC doesn't have a concept of the '.'
      // label, so just emit a local label end reference that instead.
      MCSymbol *DotSym = OutContext.createTempSymbol();
      OutStreamer->emitLabel(DotSym);
      const MCExpr *DotExpr = MCSymbolRefExpr::create(DotSym, OutContext);
      PCRelExpr = MCBinaryExpr::createSub(PCRelExpr, DotExpr, OutContext);
    }
    Expr = MCBinaryExpr::createSub(Expr, PCRelExpr, OutContext);
  }

  // Create an MCSymbol for the reference.
  Expr = CSKYMCExpr::create(Expr, getModifierVariantKind(CCPV->getModifier()),
                            OutContext);

  OutStreamer->emitValue(Expr, Size);
}

void CSKYAsmPrinter::emitAttributes() {
  CSKYTargetStreamer &CTS =
      static_cast<CSKYTargetStreamer &>(*OutStreamer->getTargetStreamer());

  const Triple &TT = TM.getTargetTriple();
  StringRef CPU = TM.getTargetCPU();
  StringRef FS = TM.getTargetFeatureString();
  const CSKYTargetMachine &CTM = static_cast<const CSKYTargetMachine &>(TM);
  /* TuneCPU doesn't impact emission of ELF attributes, ELF attributes only
     care about arch related features, so we can set TuneCPU as CPU.  */
  const CSKYSubtarget STI(TT, CPU, /*TuneCPU=*/CPU, FS, CTM);

  CTS.emitTargetAttributes(STI);
}

bool CSKYAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &OS) {
  // First try the generic code, which knows about modifiers like 'c' and 'n'.
  if (!AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS))
    return false;

  const MachineOperand &MO = MI->getOperand(OpNo);
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      return true; // Unknown modifier.
    case 'R':
      if (MO.getType() == MachineOperand::MO_Register) {
        OS << CSKYInstPrinter::getRegisterName(MO.getReg() + 1);
        return false;
      }
    }
  }

  switch (MO.getType()) {
  case MachineOperand::MO_Immediate:
    OS << MO.getImm();
    return false;
  case MachineOperand::MO_Register:
    if (MO.getReg() == CSKY::C)
      return false;
    OS << CSKYInstPrinter::getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    return false;
  case MachineOperand::MO_BlockAddress: {
    MCSymbol *Sym = GetBlockAddressSymbol(MO.getBlockAddress());
    Sym->print(OS, MAI);
    return false;
  }
  default:
    break;
  }

  return true;
}

bool CSKYAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                           unsigned OpNo, const char *ExtraCode,
                                           raw_ostream &OS) {
  if (!ExtraCode) {
    const MachineOperand &MO = MI->getOperand(OpNo);
    // For now, we only support register memory operands in registers and
    // assume there is no addend
    if (!MO.isReg())
      return true;

    OS << "(" << CSKYInstPrinter::getRegisterName(MO.getReg()) << ", 0)";
    return false;
  }

  return AsmPrinter::PrintAsmMemoryOperand(MI, OpNo, ExtraCode, OS);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeCSKYAsmPrinter() {
  RegisterAsmPrinter<CSKYAsmPrinter> X(getTheCSKYTarget());
}
