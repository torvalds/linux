//===-- AsmPrinterInlineAsm.cpp - AsmPrinter Inline Asm Handling ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the inline assembler pieces of the AsmPrinter class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

unsigned AsmPrinter::addInlineAsmDiagBuffer(StringRef AsmStr,
                                            const MDNode *LocMDNode) const {
  MCContext &Context = MMI->getContext();
  Context.initInlineSourceManager();
  SourceMgr &SrcMgr = *Context.getInlineSourceManager();
  std::vector<const MDNode *> &LocInfos = Context.getLocInfos();

  std::unique_ptr<MemoryBuffer> Buffer;
  // The inline asm source manager will outlive AsmStr, so make a copy of the
  // string for SourceMgr to own.
  Buffer = MemoryBuffer::getMemBufferCopy(AsmStr, "<inline asm>");

  // Tell SrcMgr about this buffer, it takes ownership of the buffer.
  unsigned BufNum = SrcMgr.AddNewSourceBuffer(std::move(Buffer), SMLoc());

  // Store LocMDNode in DiagInfo, using BufNum as an identifier.
  if (LocMDNode) {
    LocInfos.resize(BufNum);
    LocInfos[BufNum - 1] = LocMDNode;
  }

  return BufNum;
}


/// EmitInlineAsm - Emit a blob of inline asm to the output streamer.
void AsmPrinter::emitInlineAsm(StringRef Str, const MCSubtargetInfo &STI,
                               const MCTargetOptions &MCOptions,
                               const MDNode *LocMDNode,
                               InlineAsm::AsmDialect Dialect) const {
  assert(!Str.empty() && "Can't emit empty inline asm block");

  // Remember if the buffer is nul terminated or not so we can avoid a copy.
  bool isNullTerminated = Str.back() == 0;
  if (isNullTerminated)
    Str = Str.substr(0, Str.size()-1);

  // If the output streamer does not have mature MC support or the integrated
  // assembler has been disabled or not required, just emit the blob textually.
  // Otherwise parse the asm and emit it via MC support.
  // This is useful in case the asm parser doesn't handle something but the
  // system assembler does.
  const MCAsmInfo *MCAI = TM.getMCAsmInfo();
  assert(MCAI && "No MCAsmInfo");
  if (!MCAI->useIntegratedAssembler() &&
      !MCAI->parseInlineAsmUsingAsmParser() &&
      !OutStreamer->isIntegratedAssemblerRequired()) {
    emitInlineAsmStart();
    OutStreamer->emitRawText(Str);
    emitInlineAsmEnd(STI, nullptr);
    return;
  }

  unsigned BufNum = addInlineAsmDiagBuffer(Str, LocMDNode);
  SourceMgr &SrcMgr = *MMI->getContext().getInlineSourceManager();
  SrcMgr.setIncludeDirs(MCOptions.IASSearchPaths);

  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, OutContext, *OutStreamer, *MAI, BufNum));

  // We create a new MCInstrInfo here since we might be at the module level
  // and not have a MachineFunction to initialize the TargetInstrInfo from and
  // we only need MCInstrInfo for asm parsing. We create one unconditionally
  // because it's not subtarget dependent.
  std::unique_ptr<MCInstrInfo> MII(TM.getTarget().createMCInstrInfo());
  assert(MII && "Failed to create instruction info");
  std::unique_ptr<MCTargetAsmParser> TAP(TM.getTarget().createMCAsmParser(
      STI, *Parser, *MII, MCOptions));
  if (!TAP)
    report_fatal_error("Inline asm not supported by this streamer because"
                       " we don't have an asm parser for this target\n");

  // Respect inlineasm dialect on X86 targets only
  if (TM.getTargetTriple().isX86()) {
    Parser->setAssemblerDialect(Dialect);
    // Enable lexing Masm binary and hex integer literals in intel inline
    // assembly.
    if (Dialect == InlineAsm::AD_Intel)
      Parser->getLexer().setLexMasmIntegers(true);
  }
  Parser->setTargetParser(*TAP);

  emitInlineAsmStart();
  // Don't implicitly switch to the text section before the asm.
  (void)Parser->Run(/*NoInitialTextSection*/ true,
                    /*NoFinalize*/ true);
  emitInlineAsmEnd(STI, &TAP->getSTI());
}

static void EmitInlineAsmStr(const char *AsmStr, const MachineInstr *MI,
                             MachineModuleInfo *MMI, const MCAsmInfo *MAI,
                             AsmPrinter *AP, uint64_t LocCookie,
                             raw_ostream &OS) {
  bool InputIsIntelDialect = MI->getInlineAsmDialect() == InlineAsm::AD_Intel;

  if (InputIsIntelDialect) {
    // Switch to the inline assembly variant.
    OS << "\t.intel_syntax\n\t";
  }

  int CurVariant = -1; // The number of the {.|.|.} region we are in.
  const char *LastEmitted = AsmStr; // One past the last character emitted.
  unsigned NumOperands = MI->getNumOperands();

  int AsmPrinterVariant;
  if (InputIsIntelDialect)
    AsmPrinterVariant = 1; // X86MCAsmInfo.cpp's AsmWriterFlavorTy::Intel.
  else
    AsmPrinterVariant = MMI->getTarget().unqualifiedInlineAsmVariant();

  // FIXME: Should this happen for `asm inteldialect` as well?
  if (!InputIsIntelDialect && MAI->getEmitGNUAsmStartIndentationMarker())
    OS << '\t';

  while (*LastEmitted) {
    switch (*LastEmitted) {
    default: {
      // Not a special case, emit the string section literally.
      const char *LiteralEnd = LastEmitted+1;
      while (*LiteralEnd && *LiteralEnd != '{' && *LiteralEnd != '|' &&
             *LiteralEnd != '}' && *LiteralEnd != '$' && *LiteralEnd != '\n')
        ++LiteralEnd;
      if (CurVariant == -1 || CurVariant == AsmPrinterVariant)
        OS.write(LastEmitted, LiteralEnd - LastEmitted);
      LastEmitted = LiteralEnd;
      break;
    }
    case '\n':
      ++LastEmitted;   // Consume newline character.
      OS << '\n';      // Indent code with newline.
      break;
    case '$': {
      ++LastEmitted;   // Consume '$' character.
      bool Done = true;

      // Handle escapes.
      switch (*LastEmitted) {
      default: Done = false; break;
      case '$':     // $$ -> $
        if (!InputIsIntelDialect)
          if (CurVariant == -1 || CurVariant == AsmPrinterVariant)
            OS << '$';
        ++LastEmitted;  // Consume second '$' character.
        break;
      case '(':        // $( -> same as GCC's { character.
        ++LastEmitted; // Consume '(' character.
        if (CurVariant != -1)
          report_fatal_error("Nested variants found in inline asm string: '" +
                             Twine(AsmStr) + "'");
        CurVariant = 0; // We're in the first variant now.
        break;
      case '|':
        ++LastEmitted; // Consume '|' character.
        if (CurVariant == -1)
          OS << '|'; // This is gcc's behavior for | outside a variant.
        else
          ++CurVariant; // We're in the next variant.
        break;
      case ')':        // $) -> same as GCC's } char.
        ++LastEmitted; // Consume ')' character.
        if (CurVariant == -1)
          OS << '}'; // This is gcc's behavior for } outside a variant.
        else
          CurVariant = -1;
        break;
      }
      if (Done) break;

      bool HasCurlyBraces = false;
      if (*LastEmitted == '{') {     // ${variable}
        ++LastEmitted;               // Consume '{' character.
        HasCurlyBraces = true;
      }

      // If we have ${:foo}, then this is not a real operand reference, it is a
      // "magic" string reference, just like in .td files.  Arrange to call
      // PrintSpecial.
      if (HasCurlyBraces && *LastEmitted == ':') {
        ++LastEmitted;
        const char *StrStart = LastEmitted;
        const char *StrEnd = strchr(StrStart, '}');
        if (!StrEnd)
          report_fatal_error("Unterminated ${:foo} operand in inline asm"
                             " string: '" + Twine(AsmStr) + "'");
        if (CurVariant == -1 || CurVariant == AsmPrinterVariant)
          AP->PrintSpecial(MI, OS, StringRef(StrStart, StrEnd - StrStart));
        LastEmitted = StrEnd+1;
        break;
      }

      const char *IDStart = LastEmitted;
      const char *IDEnd = IDStart;
      while (isDigit(*IDEnd))
        ++IDEnd;

      unsigned Val;
      if (StringRef(IDStart, IDEnd-IDStart).getAsInteger(10, Val))
        report_fatal_error("Bad $ operand number in inline asm string: '" +
                           Twine(AsmStr) + "'");
      LastEmitted = IDEnd;

      if (Val >= NumOperands - 1)
        report_fatal_error("Invalid $ operand number in inline asm string: '" +
                           Twine(AsmStr) + "'");

      char Modifier[2] = { 0, 0 };

      if (HasCurlyBraces) {
        // If we have curly braces, check for a modifier character.  This
        // supports syntax like ${0:u}, which correspond to "%u0" in GCC asm.
        if (*LastEmitted == ':') {
          ++LastEmitted;    // Consume ':' character.
          if (*LastEmitted == 0)
            report_fatal_error("Bad ${:} expression in inline asm string: '" +
                               Twine(AsmStr) + "'");

          Modifier[0] = *LastEmitted;
          ++LastEmitted;    // Consume modifier character.
        }

        if (*LastEmitted != '}')
          report_fatal_error("Bad ${} expression in inline asm string: '" +
                             Twine(AsmStr) + "'");
        ++LastEmitted;    // Consume '}' character.
      }

      // Okay, we finally have a value number.  Ask the target to print this
      // operand!
      if (CurVariant == -1 || CurVariant == AsmPrinterVariant) {
        unsigned OpNo = InlineAsm::MIOp_FirstOperand;

        bool Error = false;

        // Scan to find the machine operand number for the operand.
        for (; Val; --Val) {
          if (OpNo >= MI->getNumOperands())
            break;
          const InlineAsm::Flag F(MI->getOperand(OpNo).getImm());
          OpNo += F.getNumOperandRegisters() + 1;
        }

        // We may have a location metadata attached to the end of the
        // instruction, and at no point should see metadata at any
        // other point while processing. It's an error if so.
        if (OpNo >= MI->getNumOperands() || MI->getOperand(OpNo).isMetadata()) {
          Error = true;
        } else {
          const InlineAsm::Flag F(MI->getOperand(OpNo).getImm());
          ++OpNo; // Skip over the ID number.

          // FIXME: Shouldn't arch-independent output template handling go into
          // PrintAsmOperand?
          // Labels are target independent.
          if (MI->getOperand(OpNo).isBlockAddress()) {
            const BlockAddress *BA = MI->getOperand(OpNo).getBlockAddress();
            MCSymbol *Sym = AP->GetBlockAddressSymbol(BA);
            Sym->print(OS, AP->MAI);
            MMI->getContext().registerInlineAsmLabel(Sym);
          } else if (MI->getOperand(OpNo).isMBB()) {
            const MCSymbol *Sym = MI->getOperand(OpNo).getMBB()->getSymbol();
            Sym->print(OS, AP->MAI);
          } else if (F.isMemKind()) {
            Error = AP->PrintAsmMemoryOperand(
                MI, OpNo, Modifier[0] ? Modifier : nullptr, OS);
          } else {
            Error = AP->PrintAsmOperand(MI, OpNo,
                                        Modifier[0] ? Modifier : nullptr, OS);
          }
        }
        if (Error) {
          std::string msg;
          raw_string_ostream Msg(msg);
          Msg << "invalid operand in inline asm: '" << AsmStr << "'";
          MMI->getModule()->getContext().emitError(LocCookie, msg);
        }
      }
      break;
    }
    }
  }
  if (InputIsIntelDialect)
    OS << "\n\t.att_syntax";
  OS << '\n' << (char)0;  // null terminate string.
}

/// This method formats and emits the specified machine instruction that is an
/// inline asm.
void AsmPrinter::emitInlineAsm(const MachineInstr *MI) const {
  assert(MI->isInlineAsm() && "printInlineAsm only works on inline asms");

  // Disassemble the AsmStr, printing out the literal pieces, the operands, etc.
  const char *AsmStr = MI->getOperand(0).getSymbolName();

  // If this asmstr is empty, just print the #APP/#NOAPP markers.
  // These are useful to see where empty asm's wound up.
  if (AsmStr[0] == 0) {
    OutStreamer->emitRawComment(MAI->getInlineAsmStart());
    OutStreamer->emitRawComment(MAI->getInlineAsmEnd());
    return;
  }

  // Emit the #APP start marker.  This has to happen even if verbose-asm isn't
  // enabled, so we use emitRawComment.
  OutStreamer->emitRawComment(MAI->getInlineAsmStart());

  // Get the !srcloc metadata node if we have it, and decode the loc cookie from
  // it.
  uint64_t LocCookie = 0;
  const MDNode *LocMD = nullptr;
  for (const MachineOperand &MO : llvm::reverse(MI->operands())) {
    if (MO.isMetadata() && (LocMD = MO.getMetadata()) &&
        LocMD->getNumOperands() != 0) {
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(LocMD->getOperand(0))) {
        LocCookie = CI->getZExtValue();
        break;
      }
    }
  }

  // Emit the inline asm to a temporary string so we can emit it through
  // EmitInlineAsm.
  SmallString<256> StringData;
  raw_svector_ostream OS(StringData);

  AsmPrinter *AP = const_cast<AsmPrinter*>(this);
  EmitInlineAsmStr(AsmStr, MI, MMI, MAI, AP, LocCookie, OS);

  // Emit warnings if we use reserved registers on the clobber list, as
  // that might lead to undefined behaviour.
  SmallVector<Register, 8> RestrRegs;
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  // Start with the first operand descriptor, and iterate over them.
  for (unsigned I = InlineAsm::MIOp_FirstOperand, NumOps = MI->getNumOperands();
       I < NumOps; ++I) {
    const MachineOperand &MO = MI->getOperand(I);
    if (!MO.isImm())
      continue;
    const InlineAsm::Flag F(MO.getImm());
    if (F.isClobberKind()) {
      Register Reg = MI->getOperand(I + 1).getReg();
      if (!TRI->isAsmClobberable(*MF, Reg))
        RestrRegs.push_back(Reg);
    }
    // Skip to one before the next operand descriptor, if it exists.
    I += F.getNumOperandRegisters();
  }

  if (!RestrRegs.empty()) {
    std::string Msg = "inline asm clobber list contains reserved registers: ";
    ListSeparator LS;
    for (const Register RR : RestrRegs) {
      Msg += LS;
      Msg += TRI->getRegAsmName(RR);
    }
    const char *Note =
        "Reserved registers on the clobber list may not be "
        "preserved across the asm statement, and clobbering them may "
        "lead to undefined behaviour.";
    MMI->getModule()->getContext().diagnose(DiagnosticInfoInlineAsm(
        LocCookie, Msg, DiagnosticSeverity::DS_Warning));
    MMI->getModule()->getContext().diagnose(
        DiagnosticInfoInlineAsm(LocCookie, Note, DiagnosticSeverity::DS_Note));

    for (const Register RR : RestrRegs) {
      if (std::optional<std::string> reason =
              TRI->explainReservedReg(*MF, RR)) {
        MMI->getModule()->getContext().diagnose(DiagnosticInfoInlineAsm(
            LocCookie, *reason, DiagnosticSeverity::DS_Note));
      }
    }
  }

  emitInlineAsm(StringData, getSubtargetInfo(), TM.Options.MCOptions, LocMD,
                MI->getInlineAsmDialect());

  // Emit the #NOAPP end marker.  This has to happen even if verbose-asm isn't
  // enabled, so we use emitRawComment.
  OutStreamer->emitRawComment(MAI->getInlineAsmEnd());
}

/// PrintSpecial - Print information related to the specified machine instr
/// that is independent of the operand, and may be independent of the instr
/// itself.  This can be useful for portably encoding the comment character
/// or other bits of target-specific knowledge into the asmstrings.  The
/// syntax used is ${:comment}.  Targets can override this to add support
/// for their own strange codes.
void AsmPrinter::PrintSpecial(const MachineInstr *MI, raw_ostream &OS,
                              StringRef Code) const {
  if (Code == "private") {
    const DataLayout &DL = MF->getDataLayout();
    OS << DL.getPrivateGlobalPrefix();
  } else if (Code == "comment") {
    OS << MAI->getCommentString();
  } else if (Code == "uid") {
    // Comparing the address of MI isn't sufficient, because machineinstrs may
    // be allocated to the same address across functions.

    // If this is a new LastFn instruction, bump the counter.
    if (LastMI != MI || LastFn != getFunctionNumber()) {
      ++Counter;
      LastMI = MI;
      LastFn = getFunctionNumber();
    }
    OS << Counter;
  } else {
    std::string msg;
    raw_string_ostream Msg(msg);
    Msg << "Unknown special formatter '" << Code
         << "' for machine instr: " << *MI;
    report_fatal_error(Twine(Msg.str()));
  }
}

void AsmPrinter::PrintSymbolOperand(const MachineOperand &MO, raw_ostream &OS) {
  assert(MO.isGlobal() && "caller should check MO.isGlobal");
  getSymbolPreferLocal(*MO.getGlobal())->print(OS, MAI);
  printOffset(MO.getOffset(), OS);
}

/// PrintAsmOperand - Print the specified operand of MI, an INLINEASM
/// instruction, using the specified assembler variant.  Targets should
/// override this to format as appropriate for machine specific ExtraCodes
/// or when the arch-independent handling would be too complex otherwise.
bool AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                 const char *ExtraCode, raw_ostream &O) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    // https://gcc.gnu.org/onlinedocs/gccint/Output-Template.html
    const MachineOperand &MO = MI->getOperand(OpNo);
    switch (ExtraCode[0]) {
    default:
      return true;  // Unknown modifier.
    case 'a': // Print as memory address.
      if (MO.isReg()) {
        PrintAsmMemoryOperand(MI, OpNo, nullptr, O);
        return false;
      }
      [[fallthrough]]; // GCC allows '%a' to behave like '%c' with immediates.
    case 'c': // Substitute immediate value without immediate syntax
      if (MO.isImm()) {
        O << MO.getImm();
        return false;
      }
      if (MO.isGlobal()) {
        PrintSymbolOperand(MO, O);
        return false;
      }
      return true;
    case 'n':  // Negate the immediate constant.
      if (!MO.isImm())
        return true;
      O << -MO.getImm();
      return false;
    case 's':  // The GCC deprecated s modifier
      if (!MO.isImm())
        return true;
      O << ((32 - MO.getImm()) & 31);
      return false;
    }
  }
  return true;
}

bool AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                       const char *ExtraCode, raw_ostream &O) {
  // Target doesn't support this yet!
  return true;
}

void AsmPrinter::emitInlineAsmStart() const {}

void AsmPrinter::emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                  const MCSubtargetInfo *EndInfo) const {}
