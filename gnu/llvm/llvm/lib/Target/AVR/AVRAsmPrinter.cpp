//===-- AVRAsmPrinter.cpp - AVR LLVM assembly writer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format AVR assembly language.
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "AVRMCInstLower.h"
#include "AVRSubtarget.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRInstPrinter.h"
#include "MCTargetDesc/AVRMCExpr.h"
#include "TargetInfo/AVRTargetInfo.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

#define DEBUG_TYPE "avr-asm-printer"

namespace llvm {

/// An AVR assembly code printer.
class AVRAsmPrinter : public AsmPrinter {
public:
  AVRAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), MRI(*TM.getMCRegisterInfo()) {}

  StringRef getPassName() const override { return "AVR Assembly Printer"; }

  void printOperand(const MachineInstr *MI, unsigned OpNo, raw_ostream &O);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                       const char *ExtraCode, raw_ostream &O) override;

  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNum,
                             const char *ExtraCode, raw_ostream &O) override;

  void emitInstruction(const MachineInstr *MI) override;

  const MCExpr *lowerConstant(const Constant *CV) override;

  void emitXXStructor(const DataLayout &DL, const Constant *CV) override;

  bool doFinalization(Module &M) override;

  void emitStartOfAsmFile(Module &M) override;

private:
  const MCRegisterInfo &MRI;
  bool EmittedStructorSymbolAttrs = false;
};

void AVRAsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNo);

  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << AVRInstPrinter::getPrettyRegisterName(MO.getReg(), MRI);
    break;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    break;
  case MachineOperand::MO_GlobalAddress:
    O << getSymbol(MO.getGlobal());
    break;
  case MachineOperand::MO_ExternalSymbol:
    O << *GetExternalSymbolSymbol(MO.getSymbolName());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    O << *MO.getMBB()->getSymbol();
    break;
  default:
    llvm_unreachable("Not implemented yet!");
  }
}

bool AVRAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                                    const char *ExtraCode, raw_ostream &O) {
  // Default asm printer can only deal with some extra codes,
  // so try it first.
  if (!AsmPrinter::PrintAsmOperand(MI, OpNum, ExtraCode, O))
    return false;

  const MachineOperand &MO = MI->getOperand(OpNum);

  if (ExtraCode && ExtraCode[0]) {
    // Unknown extra code.
    if (ExtraCode[1] != 0 || ExtraCode[0] < 'A' || ExtraCode[0] > 'Z')
      return true;

    // Operand must be a register when using 'A' ~ 'Z' extra code.
    if (!MO.isReg())
      return true;

    Register Reg = MO.getReg();

    unsigned ByteNumber = ExtraCode[0] - 'A';
    const InlineAsm::Flag OpFlags(MI->getOperand(OpNum - 1).getImm());
    const unsigned NumOpRegs = OpFlags.getNumOperandRegisters();

    const AVRSubtarget &STI = MF->getSubtarget<AVRSubtarget>();
    const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

    const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(Reg);
    unsigned BytesPerReg = TRI.getRegSizeInBits(*RC) / 8;
    assert(BytesPerReg <= 2 && "Only 8 and 16 bit regs are supported.");

    unsigned RegIdx = ByteNumber / BytesPerReg;
    if (RegIdx >= NumOpRegs)
      return true;
    Reg = MI->getOperand(OpNum + RegIdx).getReg();

    if (BytesPerReg == 2) {
      Reg = TRI.getSubReg(Reg, (ByteNumber % BytesPerReg) ? AVR::sub_hi
                                                          : AVR::sub_lo);
    }

    O << AVRInstPrinter::getPrettyRegisterName(Reg, MRI);
    return false;
  }

  if (MO.getType() == MachineOperand::MO_GlobalAddress)
    PrintSymbolOperand(MO, O); // Print global symbols.
  else
    printOperand(MI, OpNum, O); // Fallback to ordinary cases.

  return false;
}

bool AVRAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                          unsigned OpNum, const char *ExtraCode,
                                          raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier

  const MachineOperand &MO = MI->getOperand(OpNum);
  (void)MO;
  assert(MO.isReg() && "Unexpected inline asm memory operand");

  // TODO: We should be able to look up the alternative name for
  // the register if it's given.
  // TableGen doesn't expose a way of getting retrieving names
  // for registers.
  if (MI->getOperand(OpNum).getReg() == AVR::R31R30) {
    O << "Z";
  } else if (MI->getOperand(OpNum).getReg() == AVR::R29R28) {
    O << "Y";
  } else if (MI->getOperand(OpNum).getReg() == AVR::R27R26) {
    O << "X";
  } else {
    assert(false && "Wrong register class for memory operand.");
  }

  // If NumOpRegs == 2, then we assume it is product of a FrameIndex expansion
  // and the second operand is an Imm.
  const InlineAsm::Flag OpFlags(MI->getOperand(OpNum - 1).getImm());
  const unsigned NumOpRegs = OpFlags.getNumOperandRegisters();

  if (NumOpRegs == 2) {
    assert(MI->getOperand(OpNum).getReg() != AVR::R27R26 &&
           "Base register X can not have offset/displacement.");
    O << '+' << MI->getOperand(OpNum + 1).getImm();
  }

  return false;
}

void AVRAsmPrinter::emitInstruction(const MachineInstr *MI) {
  AVR_MC::verifyInstructionPredicates(MI->getOpcode(),
                                      getSubtargetInfo().getFeatureBits());

  AVRMCInstLower MCInstLowering(OutContext, *this);

  MCInst I;
  MCInstLowering.lowerInstruction(*MI, I);
  EmitToStreamer(*OutStreamer, I);
}

const MCExpr *AVRAsmPrinter::lowerConstant(const Constant *CV) {
  MCContext &Ctx = OutContext;

  if (const GlobalValue *GV = dyn_cast<GlobalValue>(CV)) {
    bool IsProgMem = GV->getAddressSpace() == AVR::ProgramMemory;
    if (IsProgMem) {
      const MCExpr *Expr = MCSymbolRefExpr::create(getSymbol(GV), Ctx);
      return AVRMCExpr::create(AVRMCExpr::VK_AVR_PM, Expr, false, Ctx);
    }
  }

  return AsmPrinter::lowerConstant(CV);
}

void AVRAsmPrinter::emitXXStructor(const DataLayout &DL, const Constant *CV) {
  if (!EmittedStructorSymbolAttrs) {
    OutStreamer->emitRawComment(
        " Emitting these undefined symbol references causes us to link the"
        " libgcc code that runs our constructors/destructors");
    OutStreamer->emitRawComment(" This matches GCC's behavior");

    MCSymbol *CtorsSym = OutContext.getOrCreateSymbol("__do_global_ctors");
    OutStreamer->emitSymbolAttribute(CtorsSym, MCSA_Global);

    MCSymbol *DtorsSym = OutContext.getOrCreateSymbol("__do_global_dtors");
    OutStreamer->emitSymbolAttribute(DtorsSym, MCSA_Global);

    EmittedStructorSymbolAttrs = true;
  }

  AsmPrinter::emitXXStructor(DL, CV);
}

bool AVRAsmPrinter::doFinalization(Module &M) {
  const TargetLoweringObjectFile &TLOF = getObjFileLowering();
  const AVRTargetMachine &TM = (const AVRTargetMachine &)MMI->getTarget();
  const AVRSubtarget *SubTM = (const AVRSubtarget *)TM.getSubtargetImpl();

  bool NeedsCopyData = false;
  bool NeedsClearBSS = false;
  for (const auto &GO : M.globals()) {
    if (!GO.hasInitializer() || GO.hasAvailableExternallyLinkage())
      // These globals aren't defined in the current object file.
      continue;

    if (GO.hasCommonLinkage()) {
      // COMMON symbols are put in .bss.
      NeedsClearBSS = true;
      continue;
    }

    auto *Section = cast<MCSectionELF>(TLOF.SectionForGlobal(&GO, TM));
    if (Section->getName().starts_with(".data"))
      NeedsCopyData = true;
    else if (Section->getName().starts_with(".rodata") && SubTM->hasLPM())
      // AVRs that have a separate program memory (that's most AVRs) store
      // .rodata sections in RAM.
      NeedsCopyData = true;
    else if (Section->getName().starts_with(".bss"))
      NeedsClearBSS = true;
  }

  MCSymbol *DoCopyData = OutContext.getOrCreateSymbol("__do_copy_data");
  MCSymbol *DoClearBss = OutContext.getOrCreateSymbol("__do_clear_bss");

  if (NeedsCopyData) {
    OutStreamer->emitRawComment(
        " Declaring this symbol tells the CRT that it should");
    OutStreamer->emitRawComment(
        "copy all variables from program memory to RAM on startup");
    OutStreamer->emitSymbolAttribute(DoCopyData, MCSA_Global);
  }

  if (NeedsClearBSS) {
    OutStreamer->emitRawComment(
        " Declaring this symbol tells the CRT that it should");
    OutStreamer->emitRawComment("clear the zeroed data section on startup");
    OutStreamer->emitSymbolAttribute(DoClearBss, MCSA_Global);
  }

  return AsmPrinter::doFinalization(M);
}

void AVRAsmPrinter::emitStartOfAsmFile(Module &M) {
  const AVRTargetMachine &TM = (const AVRTargetMachine &)MMI->getTarget();
  const AVRSubtarget *SubTM = (const AVRSubtarget *)TM.getSubtargetImpl();
  if (!SubTM)
    return;

  // Emit __tmp_reg__.
  OutStreamer->emitAssignment(
      MMI->getContext().getOrCreateSymbol(StringRef("__tmp_reg__")),
      MCConstantExpr::create(SubTM->getRegTmpIndex(), MMI->getContext()));
  // Emit __zero_reg__.
  OutStreamer->emitAssignment(
      MMI->getContext().getOrCreateSymbol(StringRef("__zero_reg__")),
      MCConstantExpr::create(SubTM->getRegZeroIndex(), MMI->getContext()));
  // Emit __SREG__.
  OutStreamer->emitAssignment(
      MMI->getContext().getOrCreateSymbol(StringRef("__SREG__")),
      MCConstantExpr::create(SubTM->getIORegSREG(), MMI->getContext()));
  // Emit __SP_H__ if available.
  if (!SubTM->hasSmallStack())
    OutStreamer->emitAssignment(
        MMI->getContext().getOrCreateSymbol(StringRef("__SP_H__")),
        MCConstantExpr::create(SubTM->getIORegSPH(), MMI->getContext()));
  // Emit __SP_L__.
  OutStreamer->emitAssignment(
      MMI->getContext().getOrCreateSymbol(StringRef("__SP_L__")),
      MCConstantExpr::create(SubTM->getIORegSPL(), MMI->getContext()));
  // Emit __EIND__ if available.
  if (SubTM->hasEIJMPCALL())
    OutStreamer->emitAssignment(
        MMI->getContext().getOrCreateSymbol(StringRef("__EIND__")),
        MCConstantExpr::create(SubTM->getIORegEIND(), MMI->getContext()));
  // Emit __RAMPZ__ if available.
  if (SubTM->hasELPM())
    OutStreamer->emitAssignment(
        MMI->getContext().getOrCreateSymbol(StringRef("__RAMPZ__")),
        MCConstantExpr::create(SubTM->getIORegRAMPZ(), MMI->getContext()));
}

} // end of namespace llvm

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAVRAsmPrinter() {
  llvm::RegisterAsmPrinter<llvm::AVRAsmPrinter> X(llvm::getTheAVRTarget());
}
