//===-- X86AsmPrinter.cpp - Convert X86 LLVM code to AT&T assembly --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to X86 machine code.
//
//===----------------------------------------------------------------------===//

#include "X86AsmPrinter.h"
#include "MCTargetDesc/X86ATTInstPrinter.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "MCTargetDesc/X86TargetStreamer.h"
#include "TargetInfo/X86TargetInfo.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

X86AsmPrinter::X86AsmPrinter(TargetMachine &TM,
                             std::unique_ptr<MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), FM(*this) {}

//===----------------------------------------------------------------------===//
// Primitive Helper Functions.
//===----------------------------------------------------------------------===//

/// runOnMachineFunction - Emit the function body.
///
bool X86AsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<X86Subtarget>();

  SMShadowTracker.startFunction(MF);
  CodeEmitter.reset(TM.getTarget().createMCCodeEmitter(
      *Subtarget->getInstrInfo(), MF.getContext()));

  const Module *M = MF.getFunction().getParent();
  EmitFPOData = Subtarget->isTargetWin32() && M->getCodeViewFlag();

  IndCSPrefix = M->getModuleFlag("indirect_branch_cs_prefix");

  SetupMachineFunction(MF);

  if (Subtarget->isTargetCOFF()) {
    bool Local = MF.getFunction().hasLocalLinkage();
    OutStreamer->beginCOFFSymbolDef(CurrentFnSym);
    OutStreamer->emitCOFFSymbolStorageClass(
        Local ? COFF::IMAGE_SYM_CLASS_STATIC : COFF::IMAGE_SYM_CLASS_EXTERNAL);
    OutStreamer->emitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_FUNCTION
                                    << COFF::SCT_COMPLEX_TYPE_SHIFT);
    OutStreamer->endCOFFSymbolDef();
  }

  // Emit the rest of the function body.
  emitFunctionBody();

  // Emit the XRay table for this function.
  emitXRayTable();

  EmitFPOData = false;

  IndCSPrefix = false;

  // We didn't modify anything.
  return false;
}

void X86AsmPrinter::emitFunctionBodyStart() {
  if (EmitFPOData) {
    auto *XTS =
        static_cast<X86TargetStreamer *>(OutStreamer->getTargetStreamer());
    XTS->emitFPOProc(
        CurrentFnSym,
        MF->getInfo<X86MachineFunctionInfo>()->getArgumentStackSize());
  }
}

void X86AsmPrinter::emitFunctionBodyEnd() {
  if (EmitFPOData) {
    auto *XTS =
        static_cast<X86TargetStreamer *>(OutStreamer->getTargetStreamer());
    XTS->emitFPOEndProc();
  }
}

uint32_t X86AsmPrinter::MaskKCFIType(uint32_t Value) {
  // If the type hash matches an invalid pattern, mask the value.
  const uint32_t InvalidValues[] = {
      0xFA1E0FF3, /* ENDBR64 */
      0xFB1E0FF3, /* ENDBR32 */
  };
  for (uint32_t N : InvalidValues) {
    // LowerKCFI_CHECK emits -Value for indirect call checks, so we must also
    // mask that. Note that -(Value + 1) == ~Value.
    if (N == Value || -N == Value)
      return Value + 1;
  }
  return Value;
}

void X86AsmPrinter::EmitKCFITypePadding(const MachineFunction &MF,
                                        bool HasType) {
  // Keep the function entry aligned, taking patchable-function-prefix into
  // account if set.
  int64_t PrefixBytes = 0;
  (void)MF.getFunction()
      .getFnAttribute("patchable-function-prefix")
      .getValueAsString()
      .getAsInteger(10, PrefixBytes);

  // Also take the type identifier into account if we're emitting
  // one. Otherwise, just pad with nops. The X86::MOV32ri instruction emitted
  // in X86AsmPrinter::emitKCFITypeId is 5 bytes long.
  if (HasType)
    PrefixBytes += 5;

  emitNops(offsetToAlignment(PrefixBytes, MF.getAlignment()));
}

/// emitKCFITypeId - Emit the KCFI type information in architecture specific
/// format.
void X86AsmPrinter::emitKCFITypeId(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  if (!F.getParent()->getModuleFlag("kcfi"))
    return;

  ConstantInt *Type = nullptr;
  if (const MDNode *MD = F.getMetadata(LLVMContext::MD_kcfi_type))
    Type = mdconst::extract<ConstantInt>(MD->getOperand(0));

  // If we don't have a type to emit, just emit padding if needed to maintain
  // the same alignment for all functions.
  if (!Type) {
    EmitKCFITypePadding(MF, /*HasType=*/false);
    return;
  }

  // Emit a function symbol for the type data to avoid unreachable instruction
  // warnings from binary validation tools, and use the same linkage as the
  // parent function. Note that using local linkage would result in duplicate
  // symbols for weak parent functions.
  MCSymbol *FnSym = OutContext.getOrCreateSymbol("__cfi_" + MF.getName());
  emitLinkage(&MF.getFunction(), FnSym);
  if (MAI->hasDotTypeDotSizeDirective())
    OutStreamer->emitSymbolAttribute(FnSym, MCSA_ELF_TypeFunction);
  OutStreamer->emitLabel(FnSym);

  // Embed the type hash in the X86::MOV32ri instruction to avoid special
  // casing object file parsers.
  EmitKCFITypePadding(MF);
  EmitAndCountInstruction(MCInstBuilder(X86::MOV32ri)
                              .addReg(X86::EAX)
                              .addImm(MaskKCFIType(Type->getZExtValue())));

  if (MAI->hasDotTypeDotSizeDirective()) {
    MCSymbol *EndSym = OutContext.createTempSymbol("cfi_func_end");
    OutStreamer->emitLabel(EndSym);

    const MCExpr *SizeExp = MCBinaryExpr::createSub(
        MCSymbolRefExpr::create(EndSym, OutContext),
        MCSymbolRefExpr::create(FnSym, OutContext), OutContext);
    OutStreamer->emitELFSize(FnSym, SizeExp);
  }
}

/// PrintSymbolOperand - Print a raw symbol reference operand.  This handles
/// jump tables, constant pools, global address and external symbols, all of
/// which print to a label with various suffixes for relocation types etc.
void X86AsmPrinter::PrintSymbolOperand(const MachineOperand &MO,
                                       raw_ostream &O) {
  switch (MO.getType()) {
  default: llvm_unreachable("unknown symbol type!");
  case MachineOperand::MO_ConstantPoolIndex:
    GetCPISymbol(MO.getIndex())->print(O, MAI);
    printOffset(MO.getOffset(), O);
    break;
  case MachineOperand::MO_GlobalAddress: {
    const GlobalValue *GV = MO.getGlobal();

    MCSymbol *GVSym;
    if (MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY ||
        MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY_PIC_BASE)
      GVSym = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr");
    else
      GVSym = getSymbolPreferLocal(*GV);

    // Handle dllimport linkage.
    if (MO.getTargetFlags() == X86II::MO_DLLIMPORT)
      GVSym = OutContext.getOrCreateSymbol(Twine("__imp_") + GVSym->getName());
    else if (MO.getTargetFlags() == X86II::MO_COFFSTUB)
      GVSym =
          OutContext.getOrCreateSymbol(Twine(".refptr.") + GVSym->getName());

    if (MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY ||
        MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY_PIC_BASE) {
      MCSymbol *Sym = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr");
      MachineModuleInfoImpl::StubValueTy &StubSym =
          MMI->getObjFileInfo<MachineModuleInfoMachO>().getGVStubEntry(Sym);
      if (!StubSym.getPointer())
        StubSym = MachineModuleInfoImpl::StubValueTy(getSymbol(GV),
                                                     !GV->hasInternalLinkage());
    }

    // If the name begins with a dollar-sign, enclose it in parens.  We do this
    // to avoid having it look like an integer immediate to the assembler.
    if (GVSym->getName()[0] != '$')
      GVSym->print(O, MAI);
    else {
      O << '(';
      GVSym->print(O, MAI);
      O << ')';
    }
    printOffset(MO.getOffset(), O);
    break;
  }
  }

  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag on GV operand");
  case X86II::MO_NO_FLAG:    // No flag.
    break;
  case X86II::MO_DARWIN_NONLAZY:
  case X86II::MO_DLLIMPORT:
  case X86II::MO_COFFSTUB:
    // These affect the name of the symbol, not any suffix.
    break;
  case X86II::MO_GOT_ABSOLUTE_ADDRESS:
    O << " + [.-";
    MF->getPICBaseSymbol()->print(O, MAI);
    O << ']';
    break;
  case X86II::MO_PIC_BASE_OFFSET:
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE:
    O << '-';
    MF->getPICBaseSymbol()->print(O, MAI);
    break;
  case X86II::MO_TLSGD:     O << "@TLSGD";     break;
  case X86II::MO_TLSLD:     O << "@TLSLD";     break;
  case X86II::MO_TLSLDM:    O << "@TLSLDM";    break;
  case X86II::MO_GOTTPOFF:  O << "@GOTTPOFF";  break;
  case X86II::MO_INDNTPOFF: O << "@INDNTPOFF"; break;
  case X86II::MO_TPOFF:     O << "@TPOFF";     break;
  case X86II::MO_DTPOFF:    O << "@DTPOFF";    break;
  case X86II::MO_NTPOFF:    O << "@NTPOFF";    break;
  case X86II::MO_GOTNTPOFF: O << "@GOTNTPOFF"; break;
  case X86II::MO_GOTPCREL:  O << "@GOTPCREL";  break;
  case X86II::MO_GOTPCREL_NORELAX: O << "@GOTPCREL_NORELAX"; break;
  case X86II::MO_GOT:       O << "@GOT";       break;
  case X86II::MO_GOTOFF:    O << "@GOTOFF";    break;
  case X86II::MO_PLT:       O << "@PLT";       break;
  case X86II::MO_TLVP:      O << "@TLVP";      break;
  case X86II::MO_TLVP_PIC_BASE:
    O << "@TLVP" << '-';
    MF->getPICBaseSymbol()->print(O, MAI);
    break;
  case X86II::MO_SECREL:    O << "@SECREL32";  break;
  }
}

void X86AsmPrinter::PrintOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  const bool IsATT = MI->getInlineAsmDialect() == InlineAsm::AD_ATT;
  switch (MO.getType()) {
  default: llvm_unreachable("unknown operand type!");
  case MachineOperand::MO_Register: {
    if (IsATT)
      O << '%';
    O << X86ATTInstPrinter::getRegisterName(MO.getReg());
    return;
  }

  case MachineOperand::MO_Immediate:
    if (IsATT)
      O << '$';
    O << MO.getImm();
    return;

  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_GlobalAddress: {
    switch (MI->getInlineAsmDialect()) {
    case InlineAsm::AD_ATT:
      O << '$';
      break;
    case InlineAsm::AD_Intel:
      O << "offset ";
      break;
    }
    PrintSymbolOperand(MO, O);
    break;
  }
  case MachineOperand::MO_BlockAddress: {
    MCSymbol *Sym = GetBlockAddressSymbol(MO.getBlockAddress());
    Sym->print(O, MAI);
    break;
  }
  }
}

/// PrintModifiedOperand - Print subregisters based on supplied modifier,
/// deferring to PrintOperand() if no modifier was supplied or if operand is not
/// a register.
void X86AsmPrinter::PrintModifiedOperand(const MachineInstr *MI, unsigned OpNo,
                                         raw_ostream &O, const char *Modifier) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  if (!Modifier || !MO.isReg())
    return PrintOperand(MI, OpNo, O);
  if (MI->getInlineAsmDialect() == InlineAsm::AD_ATT)
    O << '%';
  Register Reg = MO.getReg();
  if (strncmp(Modifier, "subreg", strlen("subreg")) == 0) {
    unsigned Size = (strcmp(Modifier+6,"64") == 0) ? 64 :
        (strcmp(Modifier+6,"32") == 0) ? 32 :
        (strcmp(Modifier+6,"16") == 0) ? 16 : 8;
    Reg = getX86SubSuperRegister(Reg, Size);
  }
  O << X86ATTInstPrinter::getRegisterName(Reg);
}

/// PrintPCRelImm - This is used to print an immediate value that ends up
/// being encoded as a pc-relative value.  These print slightly differently, for
/// example, a $ is not emitted.
void X86AsmPrinter::PrintPCRelImm(const MachineInstr *MI, unsigned OpNo,
                                  raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  default: llvm_unreachable("Unknown pcrel immediate operand");
  case MachineOperand::MO_Register:
    // pc-relativeness was handled when computing the value in the reg.
    PrintOperand(MI, OpNo, O);
    return;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, O);
    return;
  }
}

void X86AsmPrinter::PrintLeaMemReference(const MachineInstr *MI, unsigned OpNo,
                                         raw_ostream &O, const char *Modifier) {
  const MachineOperand &BaseReg = MI->getOperand(OpNo + X86::AddrBaseReg);
  const MachineOperand &IndexReg = MI->getOperand(OpNo + X86::AddrIndexReg);
  const MachineOperand &DispSpec = MI->getOperand(OpNo + X86::AddrDisp);

  // If we really don't want to print out (rip), don't.
  bool HasBaseReg = BaseReg.getReg() != 0;
  if (HasBaseReg && Modifier && !strcmp(Modifier, "no-rip") &&
      BaseReg.getReg() == X86::RIP)
    HasBaseReg = false;

  // HasParenPart - True if we will print out the () part of the mem ref.
  bool HasParenPart = IndexReg.getReg() || HasBaseReg;

  switch (DispSpec.getType()) {
  default:
    llvm_unreachable("unknown operand type!");
  case MachineOperand::MO_Immediate: {
    int DispVal = DispSpec.getImm();
    if (DispVal || !HasParenPart)
      O << DispVal;
    break;
  }
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ConstantPoolIndex:
    PrintSymbolOperand(DispSpec, O);
    break;
  }

  if (Modifier && strcmp(Modifier, "H") == 0)
    O << "+8";

  if (HasParenPart) {
    assert(IndexReg.getReg() != X86::ESP &&
           "X86 doesn't allow scaling by ESP");

    O << '(';
    if (HasBaseReg)
      PrintModifiedOperand(MI, OpNo + X86::AddrBaseReg, O, Modifier);

    if (IndexReg.getReg()) {
      O << ',';
      PrintModifiedOperand(MI, OpNo + X86::AddrIndexReg, O, Modifier);
      unsigned ScaleVal = MI->getOperand(OpNo + X86::AddrScaleAmt).getImm();
      if (ScaleVal != 1)
        O << ',' << ScaleVal;
    }
    O << ')';
  }
}

static bool isSimpleReturn(const MachineInstr &MI) {
  // We exclude all tail calls here which set both isReturn and isCall.
  return MI.getDesc().isReturn() && !MI.getDesc().isCall();
}

static bool isIndirectBranchOrTailCall(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return MI.getDesc().isIndirectBranch() /*Make below code in a good shape*/ ||
         Opc == X86::TAILJMPr || Opc == X86::TAILJMPm ||
         Opc == X86::TAILJMPr64 || Opc == X86::TAILJMPm64 ||
         Opc == X86::TCRETURNri || Opc == X86::TCRETURNmi ||
         Opc == X86::TCRETURNri64 || Opc == X86::TCRETURNmi64 ||
         Opc == X86::TAILJMPr64_REX || Opc == X86::TAILJMPm64_REX;
}

void X86AsmPrinter::emitBasicBlockEnd(const MachineBasicBlock &MBB) {
  if (Subtarget->hardenSlsRet() || Subtarget->hardenSlsIJmp()) {
    auto I = MBB.getLastNonDebugInstr();
    if (I != MBB.end()) {
      if ((Subtarget->hardenSlsRet() && isSimpleReturn(*I)) ||
          (Subtarget->hardenSlsIJmp() && isIndirectBranchOrTailCall(*I))) {
        MCInst TmpInst;
        TmpInst.setOpcode(X86::INT3);
        EmitToStreamer(*OutStreamer, TmpInst);
      }
    }
  }
  AsmPrinter::emitBasicBlockEnd(MBB);
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
}

void X86AsmPrinter::PrintMemReference(const MachineInstr *MI, unsigned OpNo,
                                      raw_ostream &O, const char *Modifier) {
  assert(isMem(*MI, OpNo) && "Invalid memory reference!");
  const MachineOperand &Segment = MI->getOperand(OpNo + X86::AddrSegmentReg);
  if (Segment.getReg()) {
    PrintModifiedOperand(MI, OpNo + X86::AddrSegmentReg, O, Modifier);
    O << ':';
  }
  PrintLeaMemReference(MI, OpNo, O, Modifier);
}


void X86AsmPrinter::PrintIntelMemReference(const MachineInstr *MI,
                                           unsigned OpNo, raw_ostream &O,
                                           const char *Modifier) {
  const MachineOperand &BaseReg = MI->getOperand(OpNo + X86::AddrBaseReg);
  unsigned ScaleVal = MI->getOperand(OpNo + X86::AddrScaleAmt).getImm();
  const MachineOperand &IndexReg = MI->getOperand(OpNo + X86::AddrIndexReg);
  const MachineOperand &DispSpec = MI->getOperand(OpNo + X86::AddrDisp);
  const MachineOperand &SegReg = MI->getOperand(OpNo + X86::AddrSegmentReg);

  // If we really don't want to print out (rip), don't.
  bool HasBaseReg = BaseReg.getReg() != 0;
  if (HasBaseReg && Modifier && !strcmp(Modifier, "no-rip") &&
      BaseReg.getReg() == X86::RIP)
    HasBaseReg = false;

  // If we really just want to print out displacement.
  if (Modifier && (DispSpec.isGlobal() || DispSpec.isSymbol()) &&
      !strcmp(Modifier, "disp-only")) {
    HasBaseReg = false;
  }

  // If this has a segment register, print it.
  if (SegReg.getReg()) {
    PrintOperand(MI, OpNo + X86::AddrSegmentReg, O);
    O << ':';
  }

  O << '[';

  bool NeedPlus = false;
  if (HasBaseReg) {
    PrintOperand(MI, OpNo + X86::AddrBaseReg, O);
    NeedPlus = true;
  }

  if (IndexReg.getReg()) {
    if (NeedPlus) O << " + ";
    if (ScaleVal != 1)
      O << ScaleVal << '*';
    PrintOperand(MI, OpNo + X86::AddrIndexReg, O);
    NeedPlus = true;
  }

  if (!DispSpec.isImm()) {
    if (NeedPlus) O << " + ";
    // Do not add `offset` operator. Matches the behaviour of
    // X86IntelInstPrinter::printMemReference.
    PrintSymbolOperand(DispSpec, O);
  } else {
    int64_t DispVal = DispSpec.getImm();
    if (DispVal || (!IndexReg.getReg() && !HasBaseReg)) {
      if (NeedPlus) {
        if (DispVal > 0)
          O << " + ";
        else {
          O << " - ";
          DispVal = -DispVal;
        }
      }
      O << DispVal;
    }
  }
  O << ']';
}

const MCSubtargetInfo *X86AsmPrinter::getIFuncMCSubtargetInfo() const {
  assert(Subtarget);
  return Subtarget;
}

void X86AsmPrinter::emitMachOIFuncStubBody(Module &M, const GlobalIFunc &GI,
                                           MCSymbol *LazyPointer) {
  // _ifunc:
  //   jmpq *lazy_pointer(%rip)

  OutStreamer->emitInstruction(
      MCInstBuilder(X86::JMP32m)
          .addReg(X86::RIP)
          .addImm(1)
          .addReg(0)
          .addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(LazyPointer, OutContext)))
          .addReg(0),
      *Subtarget);
}

void X86AsmPrinter::emitMachOIFuncStubHelperBody(Module &M,
                                                 const GlobalIFunc &GI,
                                                 MCSymbol *LazyPointer) {
  // _ifunc.stub_helper:
  //   push %rax
  //   push %rdi
  //   push %rsi
  //   push %rdx
  //   push %rcx
  //   push %r8
  //   push %r9
  //   callq foo
  //   movq %rax,lazy_pointer(%rip)
  //   pop %r9
  //   pop %r8
  //   pop %rcx
  //   pop %rdx
  //   pop %rsi
  //   pop %rdi
  //   pop %rax
  //   jmpq *lazy_pointer(%rip)

  for (int Reg :
       {X86::RAX, X86::RDI, X86::RSI, X86::RDX, X86::RCX, X86::R8, X86::R9})
    OutStreamer->emitInstruction(MCInstBuilder(X86::PUSH64r).addReg(Reg),
                                 *Subtarget);

  OutStreamer->emitInstruction(
      MCInstBuilder(X86::CALL64pcrel32)
          .addOperand(MCOperand::createExpr(lowerConstant(GI.getResolver()))),
      *Subtarget);

  OutStreamer->emitInstruction(
      MCInstBuilder(X86::MOV64mr)
          .addReg(X86::RIP)
          .addImm(1)
          .addReg(0)
          .addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(LazyPointer, OutContext)))
          .addReg(0)
          .addReg(X86::RAX),
      *Subtarget);

  for (int Reg :
       {X86::R9, X86::R8, X86::RCX, X86::RDX, X86::RSI, X86::RDI, X86::RAX})
    OutStreamer->emitInstruction(MCInstBuilder(X86::POP64r).addReg(Reg),
                                 *Subtarget);

  OutStreamer->emitInstruction(
      MCInstBuilder(X86::JMP32m)
          .addReg(X86::RIP)
          .addImm(1)
          .addReg(0)
          .addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(LazyPointer, OutContext)))
          .addReg(0),
      *Subtarget);
}

static bool printAsmMRegister(const X86AsmPrinter &P, const MachineOperand &MO,
                              char Mode, raw_ostream &O) {
  Register Reg = MO.getReg();
  bool EmitPercent = MO.getParent()->getInlineAsmDialect() == InlineAsm::AD_ATT;

  if (!X86::GR8RegClass.contains(Reg) &&
      !X86::GR16RegClass.contains(Reg) &&
      !X86::GR32RegClass.contains(Reg) &&
      !X86::GR64RegClass.contains(Reg))
    return true;

  switch (Mode) {
  default: return true;  // Unknown mode.
  case 'b': // Print QImode register
    Reg = getX86SubSuperRegister(Reg, 8);
    break;
  case 'h': // Print QImode high register
    Reg = getX86SubSuperRegister(Reg, 8, true);
    if (!Reg.isValid())
      return true;
    break;
  case 'w': // Print HImode register
    Reg = getX86SubSuperRegister(Reg, 16);
    break;
  case 'k': // Print SImode register
    Reg = getX86SubSuperRegister(Reg, 32);
    break;
  case 'V':
    EmitPercent = false;
    [[fallthrough]];
  case 'q':
    // Print 64-bit register names if 64-bit integer registers are available.
    // Otherwise, print 32-bit register names.
    Reg = getX86SubSuperRegister(Reg, P.getSubtarget().is64Bit() ? 64 : 32);
    break;
  }

  if (EmitPercent)
    O << '%';

  O << X86ATTInstPrinter::getRegisterName(Reg);
  return false;
}

static bool printAsmVRegister(const MachineOperand &MO, char Mode,
                              raw_ostream &O) {
  Register Reg = MO.getReg();
  bool EmitPercent = MO.getParent()->getInlineAsmDialect() == InlineAsm::AD_ATT;

  unsigned Index;
  if (X86::VR128XRegClass.contains(Reg))
    Index = Reg - X86::XMM0;
  else if (X86::VR256XRegClass.contains(Reg))
    Index = Reg - X86::YMM0;
  else if (X86::VR512RegClass.contains(Reg))
    Index = Reg - X86::ZMM0;
  else
    return true;

  switch (Mode) {
  default: // Unknown mode.
    return true;
  case 'x': // Print V4SFmode register
    Reg = X86::XMM0 + Index;
    break;
  case 't': // Print V8SFmode register
    Reg = X86::YMM0 + Index;
    break;
  case 'g': // Print V16SFmode register
    Reg = X86::ZMM0 + Index;
    break;
  }

  if (EmitPercent)
    O << '%';

  O << X86ATTInstPrinter::getRegisterName(Reg);
  return false;
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
///
bool X86AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                    const char *ExtraCode, raw_ostream &O) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    const MachineOperand &MO = MI->getOperand(OpNo);

    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);
    case 'a': // This is an address.  Currently only 'i' and 'r' are expected.
      switch (MO.getType()) {
      default:
        return true;
      case MachineOperand::MO_Immediate:
        O << MO.getImm();
        return false;
      case MachineOperand::MO_ConstantPoolIndex:
      case MachineOperand::MO_JumpTableIndex:
      case MachineOperand::MO_ExternalSymbol:
        llvm_unreachable("unexpected operand type!");
      case MachineOperand::MO_GlobalAddress:
        PrintSymbolOperand(MO, O);
        if (Subtarget->isPICStyleRIPRel())
          O << "(%rip)";
        return false;
      case MachineOperand::MO_Register:
        O << '(';
        PrintOperand(MI, OpNo, O);
        O << ')';
        return false;
      }

    case 'c': // Don't print "$" before a global var name or constant.
      switch (MO.getType()) {
      default:
        PrintOperand(MI, OpNo, O);
        break;
      case MachineOperand::MO_Immediate:
        O << MO.getImm();
        break;
      case MachineOperand::MO_ConstantPoolIndex:
      case MachineOperand::MO_JumpTableIndex:
      case MachineOperand::MO_ExternalSymbol:
        llvm_unreachable("unexpected operand type!");
      case MachineOperand::MO_GlobalAddress:
        PrintSymbolOperand(MO, O);
        break;
      }
      return false;

    case 'A': // Print '*' before a register (it must be a register)
      if (MO.isReg()) {
        O << '*';
        PrintOperand(MI, OpNo, O);
        return false;
      }
      return true;

    case 'b': // Print QImode register
    case 'h': // Print QImode high register
    case 'w': // Print HImode register
    case 'k': // Print SImode register
    case 'q': // Print DImode register
    case 'V': // Print native register without '%'
      if (MO.isReg())
        return printAsmMRegister(*this, MO, ExtraCode[0], O);
      PrintOperand(MI, OpNo, O);
      return false;

    case 'x': // Print V4SFmode register
    case 't': // Print V8SFmode register
    case 'g': // Print V16SFmode register
      if (MO.isReg())
        return printAsmVRegister(MO, ExtraCode[0], O);
      PrintOperand(MI, OpNo, O);
      return false;

    case 'p': {
      const MachineOperand &MO = MI->getOperand(OpNo);
      if (MO.getType() != MachineOperand::MO_GlobalAddress)
        return true;
      PrintSymbolOperand(MO, O);
      return false;
    }

    case 'P': // This is the operand of a call, treat specially.
      PrintPCRelImm(MI, OpNo, O);
      return false;

    case 'n': // Negate the immediate or print a '-' before the operand.
      // Note: this is a temporary solution. It should be handled target
      // independently as part of the 'MC' work.
      if (MO.isImm()) {
        O << -MO.getImm();
        return false;
      }
      O << '-';
    }
  }

  PrintOperand(MI, OpNo, O);
  return false;
}

bool X86AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                          const char *ExtraCode,
                                          raw_ostream &O) {
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default: return true;  // Unknown modifier.
    case 'b': // Print QImode register
    case 'h': // Print QImode high register
    case 'w': // Print HImode register
    case 'k': // Print SImode register
    case 'q': // Print SImode register
      // These only apply to registers, ignore on mem.
      break;
    case 'H':
      if (MI->getInlineAsmDialect() == InlineAsm::AD_Intel) {
        return true;  // Unsupported modifier in Intel inline assembly.
      } else {
        PrintMemReference(MI, OpNo, O, "H");
      }
      return false;
   // Print memory only with displacement. The Modifer 'P' is used in inline
   // asm to present a call symbol or a global symbol which can not use base
   // reg or index reg.
    case 'P':
      if (MI->getInlineAsmDialect() == InlineAsm::AD_Intel) {
        PrintIntelMemReference(MI, OpNo, O, "disp-only");
      } else {
        PrintMemReference(MI, OpNo, O, "disp-only");
      }
      return false;
    }
  }
  if (MI->getInlineAsmDialect() == InlineAsm::AD_Intel) {
    PrintIntelMemReference(MI, OpNo, O, nullptr);
  } else {
    PrintMemReference(MI, OpNo, O, nullptr);
  }
  return false;
}

void X86AsmPrinter::emitStartOfAsmFile(Module &M) {
  const Triple &TT = TM.getTargetTriple();

  if (TT.isOSBinFormatELF()) {
    // Assemble feature flags that may require creation of a note section.
    unsigned FeatureFlagsAnd = 0;
    if (M.getModuleFlag("cf-protection-branch"))
      FeatureFlagsAnd |= ELF::GNU_PROPERTY_X86_FEATURE_1_IBT;
    if (M.getModuleFlag("cf-protection-return"))
      FeatureFlagsAnd |= ELF::GNU_PROPERTY_X86_FEATURE_1_SHSTK;

    if (FeatureFlagsAnd) {
      // Emit a .note.gnu.property section with the flags.
      assert((TT.isArch32Bit() || TT.isArch64Bit()) &&
             "CFProtection used on invalid architecture!");
      MCSection *Cur = OutStreamer->getCurrentSectionOnly();
      MCSection *Nt = MMI->getContext().getELFSection(
          ".note.gnu.property", ELF::SHT_NOTE, ELF::SHF_ALLOC);
      OutStreamer->switchSection(Nt);

      // Emitting note header.
      const int WordSize = TT.isArch64Bit() && !TT.isX32() ? 8 : 4;
      emitAlignment(WordSize == 4 ? Align(4) : Align(8));
      OutStreamer->emitIntValue(4, 4 /*size*/); // data size for "GNU\0"
      OutStreamer->emitIntValue(8 + WordSize, 4 /*size*/); // Elf_Prop size
      OutStreamer->emitIntValue(ELF::NT_GNU_PROPERTY_TYPE_0, 4 /*size*/);
      OutStreamer->emitBytes(StringRef("GNU", 4)); // note name

      // Emitting an Elf_Prop for the CET properties.
      OutStreamer->emitInt32(ELF::GNU_PROPERTY_X86_FEATURE_1_AND);
      OutStreamer->emitInt32(4);                          // data size
      OutStreamer->emitInt32(FeatureFlagsAnd);            // data
      emitAlignment(WordSize == 4 ? Align(4) : Align(8)); // padding

      OutStreamer->switchSection(Cur);
    }
  }

  if (TT.isOSBinFormatMachO())
    OutStreamer->switchSection(getObjFileLowering().getTextSection());

  if (TT.isOSBinFormatCOFF()) {
    // Emit an absolute @feat.00 symbol.
    MCSymbol *S = MMI->getContext().getOrCreateSymbol(StringRef("@feat.00"));
    OutStreamer->beginCOFFSymbolDef(S);
    OutStreamer->emitCOFFSymbolStorageClass(COFF::IMAGE_SYM_CLASS_STATIC);
    OutStreamer->emitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_NULL);
    OutStreamer->endCOFFSymbolDef();
    int64_t Feat00Value = 0;

    if (TT.getArch() == Triple::x86) {
      // According to the PE-COFF spec, the LSB of this value marks the object
      // for "registered SEH".  This means that all SEH handler entry points
      // must be registered in .sxdata.  Use of any unregistered handlers will
      // cause the process to terminate immediately.  LLVM does not know how to
      // register any SEH handlers, so its object files should be safe.
      Feat00Value |= COFF::Feat00Flags::SafeSEH;
    }

    if (M.getModuleFlag("cfguard")) {
      // Object is CFG-aware.
      Feat00Value |= COFF::Feat00Flags::GuardCF;
    }

    if (M.getModuleFlag("ehcontguard")) {
      // Object also has EHCont.
      Feat00Value |= COFF::Feat00Flags::GuardEHCont;
    }

    if (M.getModuleFlag("ms-kernel")) {
      // Object is compiled with /kernel.
      Feat00Value |= COFF::Feat00Flags::Kernel;
    }

    OutStreamer->emitSymbolAttribute(S, MCSA_Global);
    OutStreamer->emitAssignment(
        S, MCConstantExpr::create(Feat00Value, MMI->getContext()));
  }
  OutStreamer->emitSyntaxDirective();

  // If this is not inline asm and we're in 16-bit
  // mode prefix assembly with .code16.
  bool is16 = TT.getEnvironment() == Triple::CODE16;
  if (M.getModuleInlineAsm().empty() && is16)
    OutStreamer->emitAssemblerFlag(MCAF_Code16);
}

static void
emitNonLazySymbolPointer(MCStreamer &OutStreamer, MCSymbol *StubLabel,
                         MachineModuleInfoImpl::StubValueTy &MCSym) {
  // L_foo$stub:
  OutStreamer.emitLabel(StubLabel);
  //   .indirect_symbol _foo
  OutStreamer.emitSymbolAttribute(MCSym.getPointer(), MCSA_IndirectSymbol);

  if (MCSym.getInt())
    // External to current translation unit.
    OutStreamer.emitIntValue(0, 4/*size*/);
  else
    // Internal to current translation unit.
    //
    // When we place the LSDA into the TEXT section, the type info
    // pointers need to be indirect and pc-rel. We accomplish this by
    // using NLPs; however, sometimes the types are local to the file.
    // We need to fill in the value for the NLP in those cases.
    OutStreamer.emitValue(
        MCSymbolRefExpr::create(MCSym.getPointer(), OutStreamer.getContext()),
        4 /*size*/);
}

static void emitNonLazyStubs(MachineModuleInfo *MMI, MCStreamer &OutStreamer) {

  MachineModuleInfoMachO &MMIMacho =
      MMI->getObjFileInfo<MachineModuleInfoMachO>();

  // Output stubs for dynamically-linked functions.
  MachineModuleInfoMachO::SymbolListTy Stubs;

  // Output stubs for external and common global variables.
  Stubs = MMIMacho.GetGVStubList();
  if (!Stubs.empty()) {
    OutStreamer.switchSection(MMI->getContext().getMachOSection(
        "__IMPORT", "__pointers", MachO::S_NON_LAZY_SYMBOL_POINTERS,
        SectionKind::getMetadata()));

    for (auto &Stub : Stubs)
      emitNonLazySymbolPointer(OutStreamer, Stub.first, Stub.second);

    Stubs.clear();
    OutStreamer.addBlankLine();
  }
}

void X86AsmPrinter::emitEndOfAsmFile(Module &M) {
  const Triple &TT = TM.getTargetTriple();

  if (TT.isOSBinFormatMachO()) {
    // Mach-O uses non-lazy symbol stubs to encode per-TU information into
    // global table for symbol lookup.
    emitNonLazyStubs(MMI, *OutStreamer);

    // Emit fault map information.
    FM.serializeToFaultMapSection();

    // This flag tells the linker that no global symbols contain code that fall
    // through to other global symbols (e.g. an implementation of multiple entry
    // points). If this doesn't occur, the linker can safely perform dead code
    // stripping. Since LLVM never generates code that does this, it is always
    // safe to set.
    OutStreamer->emitAssemblerFlag(MCAF_SubsectionsViaSymbols);
  } else if (TT.isOSBinFormatCOFF()) {
    if (MMI->usesMSVCFloatingPoint()) {
      // In Windows' libcmt.lib, there is a file which is linked in only if the
      // symbol _fltused is referenced. Linking this in causes some
      // side-effects:
      //
      // 1. For x86-32, it will set the x87 rounding mode to 53-bit instead of
      // 64-bit mantissas at program start.
      //
      // 2. It links in support routines for floating-point in scanf and printf.
      //
      // MSVC emits an undefined reference to _fltused when there are any
      // floating point operations in the program (including calls). A program
      // that only has: `scanf("%f", &global_float);` may fail to trigger this,
      // but oh well...that's a documented issue.
      StringRef SymbolName =
          (TT.getArch() == Triple::x86) ? "__fltused" : "_fltused";
      MCSymbol *S = MMI->getContext().getOrCreateSymbol(SymbolName);
      OutStreamer->emitSymbolAttribute(S, MCSA_Global);
      return;
    }
  } else if (TT.isOSBinFormatELF()) {
    FM.serializeToFaultMapSection();
  }

  // Emit __morestack address if needed for indirect calls.
  if (TT.getArch() == Triple::x86_64 && TM.getCodeModel() == CodeModel::Large) {
    if (MCSymbol *AddrSymbol = OutContext.lookupSymbol("__morestack_addr")) {
      Align Alignment(1);
      MCSection *ReadOnlySection = getObjFileLowering().getSectionForConstant(
          getDataLayout(), SectionKind::getReadOnly(),
          /*C=*/nullptr, Alignment);
      OutStreamer->switchSection(ReadOnlySection);
      OutStreamer->emitLabel(AddrSymbol);

      unsigned PtrSize = MAI->getCodePointerSize();
      OutStreamer->emitSymbolValue(GetExternalSymbolSymbol("__morestack"),
                                   PtrSize);
    }
  }
}

//===----------------------------------------------------------------------===//
// Target Registry Stuff
//===----------------------------------------------------------------------===//

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeX86AsmPrinter() {
  RegisterAsmPrinter<X86AsmPrinter> X(getTheX86_32Target());
  RegisterAsmPrinter<X86AsmPrinter> Y(getTheX86_64Target());
}
