//===-- SystemZAsmParser.cpp - Parse SystemZ assembly instructions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SystemZInstPrinter.h"
#include "MCTargetDesc/SystemZMCAsmInfo.h"
#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "SystemZTargetStreamer.h"
#include "TargetInfo/SystemZTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

using namespace llvm;

// Return true if Expr is in the range [MinValue, MaxValue]. If AllowSymbol
// is true any MCExpr is accepted (address displacement).
static bool inRange(const MCExpr *Expr, int64_t MinValue, int64_t MaxValue,
                    bool AllowSymbol = false) {
  if (auto *CE = dyn_cast<MCConstantExpr>(Expr)) {
    int64_t Value = CE->getValue();
    return Value >= MinValue && Value <= MaxValue;
  }
  return AllowSymbol;
}

namespace {

enum RegisterKind {
  GR32Reg,
  GRH32Reg,
  GR64Reg,
  GR128Reg,
  FP32Reg,
  FP64Reg,
  FP128Reg,
  VR32Reg,
  VR64Reg,
  VR128Reg,
  AR32Reg,
  CR64Reg,
};

enum MemoryKind {
  BDMem,
  BDXMem,
  BDLMem,
  BDRMem,
  BDVMem
};

class SystemZOperand : public MCParsedAsmOperand {
private:
  enum OperandKind {
    KindInvalid,
    KindToken,
    KindReg,
    KindImm,
    KindImmTLS,
    KindMem
  };

  OperandKind Kind;
  SMLoc StartLoc, EndLoc;

  // A string of length Length, starting at Data.
  struct TokenOp {
    const char *Data;
    unsigned Length;
  };

  // LLVM register Num, which has kind Kind.  In some ways it might be
  // easier for this class to have a register bank (general, floating-point
  // or access) and a raw register number (0-15).  This would postpone the
  // interpretation of the operand to the add*() methods and avoid the need
  // for context-dependent parsing.  However, we do things the current way
  // because of the virtual getReg() method, which needs to distinguish
  // between (say) %r0 used as a single register and %r0 used as a pair.
  // Context-dependent parsing can also give us slightly better error
  // messages when invalid pairs like %r1 are used.
  struct RegOp {
    RegisterKind Kind;
    unsigned Num;
  };

  // Base + Disp + Index, where Base and Index are LLVM registers or 0.
  // MemKind says what type of memory this is and RegKind says what type
  // the base register has (GR32Reg or GR64Reg).  Length is the operand
  // length for D(L,B)-style operands, otherwise it is null.
  struct MemOp {
    unsigned Base : 12;
    unsigned Index : 12;
    unsigned MemKind : 4;
    unsigned RegKind : 4;
    const MCExpr *Disp;
    union {
      const MCExpr *Imm;
      unsigned Reg;
    } Length;
  };

  // Imm is an immediate operand, and Sym is an optional TLS symbol
  // for use with a __tls_get_offset marker relocation.
  struct ImmTLSOp {
    const MCExpr *Imm;
    const MCExpr *Sym;
  };

  union {
    TokenOp Token;
    RegOp Reg;
    const MCExpr *Imm;
    ImmTLSOp ImmTLS;
    MemOp Mem;
  };

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

public:
  SystemZOperand(OperandKind Kind, SMLoc StartLoc, SMLoc EndLoc)
      : Kind(Kind), StartLoc(StartLoc), EndLoc(EndLoc) {}

  // Create particular kinds of operand.
  static std::unique_ptr<SystemZOperand> createInvalid(SMLoc StartLoc,
                                                       SMLoc EndLoc) {
    return std::make_unique<SystemZOperand>(KindInvalid, StartLoc, EndLoc);
  }

  static std::unique_ptr<SystemZOperand> createToken(StringRef Str, SMLoc Loc) {
    auto Op = std::make_unique<SystemZOperand>(KindToken, Loc, Loc);
    Op->Token.Data = Str.data();
    Op->Token.Length = Str.size();
    return Op;
  }

  static std::unique_ptr<SystemZOperand>
  createReg(RegisterKind Kind, unsigned Num, SMLoc StartLoc, SMLoc EndLoc) {
    auto Op = std::make_unique<SystemZOperand>(KindReg, StartLoc, EndLoc);
    Op->Reg.Kind = Kind;
    Op->Reg.Num = Num;
    return Op;
  }

  static std::unique_ptr<SystemZOperand>
  createImm(const MCExpr *Expr, SMLoc StartLoc, SMLoc EndLoc) {
    auto Op = std::make_unique<SystemZOperand>(KindImm, StartLoc, EndLoc);
    Op->Imm = Expr;
    return Op;
  }

  static std::unique_ptr<SystemZOperand>
  createMem(MemoryKind MemKind, RegisterKind RegKind, unsigned Base,
            const MCExpr *Disp, unsigned Index, const MCExpr *LengthImm,
            unsigned LengthReg, SMLoc StartLoc, SMLoc EndLoc) {
    auto Op = std::make_unique<SystemZOperand>(KindMem, StartLoc, EndLoc);
    Op->Mem.MemKind = MemKind;
    Op->Mem.RegKind = RegKind;
    Op->Mem.Base = Base;
    Op->Mem.Index = Index;
    Op->Mem.Disp = Disp;
    if (MemKind == BDLMem)
      Op->Mem.Length.Imm = LengthImm;
    if (MemKind == BDRMem)
      Op->Mem.Length.Reg = LengthReg;
    return Op;
  }

  static std::unique_ptr<SystemZOperand>
  createImmTLS(const MCExpr *Imm, const MCExpr *Sym,
               SMLoc StartLoc, SMLoc EndLoc) {
    auto Op = std::make_unique<SystemZOperand>(KindImmTLS, StartLoc, EndLoc);
    Op->ImmTLS.Imm = Imm;
    Op->ImmTLS.Sym = Sym;
    return Op;
  }

  // Token operands
  bool isToken() const override {
    return Kind == KindToken;
  }
  StringRef getToken() const {
    assert(Kind == KindToken && "Not a token");
    return StringRef(Token.Data, Token.Length);
  }

  // Register operands.
  bool isReg() const override {
    return Kind == KindReg;
  }
  bool isReg(RegisterKind RegKind) const {
    return Kind == KindReg && Reg.Kind == RegKind;
  }
  MCRegister getReg() const override {
    assert(Kind == KindReg && "Not a register");
    return Reg.Num;
  }

  // Immediate operands.
  bool isImm() const override {
    return Kind == KindImm;
  }
  bool isImm(int64_t MinValue, int64_t MaxValue) const {
    return Kind == KindImm && inRange(Imm, MinValue, MaxValue, true);
  }
  const MCExpr *getImm() const {
    assert(Kind == KindImm && "Not an immediate");
    return Imm;
  }

  // Immediate operands with optional TLS symbol.
  bool isImmTLS() const {
    return Kind == KindImmTLS;
  }

  const ImmTLSOp getImmTLS() const {
    assert(Kind == KindImmTLS && "Not a TLS immediate");
    return ImmTLS;
  }

  // Memory operands.
  bool isMem() const override {
    return Kind == KindMem;
  }
  bool isMem(MemoryKind MemKind) const {
    return (Kind == KindMem &&
            (Mem.MemKind == MemKind ||
             // A BDMem can be treated as a BDXMem in which the index
             // register field is 0.
             (Mem.MemKind == BDMem && MemKind == BDXMem)));
  }
  bool isMem(MemoryKind MemKind, RegisterKind RegKind) const {
    return isMem(MemKind) && Mem.RegKind == RegKind;
  }
  bool isMemDisp12(MemoryKind MemKind, RegisterKind RegKind) const {
    return isMem(MemKind, RegKind) && inRange(Mem.Disp, 0, 0xfff, true);
  }
  bool isMemDisp20(MemoryKind MemKind, RegisterKind RegKind) const {
    return isMem(MemKind, RegKind) && inRange(Mem.Disp, -524288, 524287, true);
  }
  bool isMemDisp12Len4(RegisterKind RegKind) const {
    return isMemDisp12(BDLMem, RegKind) && inRange(Mem.Length.Imm, 1, 0x10);
  }
  bool isMemDisp12Len8(RegisterKind RegKind) const {
    return isMemDisp12(BDLMem, RegKind) && inRange(Mem.Length.Imm, 1, 0x100);
  }

  const MemOp& getMem() const {
    assert(Kind == KindMem && "Not a Mem operand");
    return Mem;
  }

  // Override MCParsedAsmOperand.
  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }
  void print(raw_ostream &OS) const override;

  /// getLocRange - Get the range between the first and last token of this
  /// operand.
  SMRange getLocRange() const { return SMRange(StartLoc, EndLoc); }

  // Used by the TableGen code to add particular types of operand
  // to an instruction.
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }
  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands");
    addExpr(Inst, getImm());
  }
  void addBDAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
    assert(isMem(BDMem) && "Invalid operand type");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExpr(Inst, Mem.Disp);
  }
  void addBDXAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands");
    assert(isMem(BDXMem) && "Invalid operand type");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExpr(Inst, Mem.Disp);
    Inst.addOperand(MCOperand::createReg(Mem.Index));
  }
  void addBDLAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands");
    assert(isMem(BDLMem) && "Invalid operand type");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExpr(Inst, Mem.Disp);
    addExpr(Inst, Mem.Length.Imm);
  }
  void addBDRAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands");
    assert(isMem(BDRMem) && "Invalid operand type");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExpr(Inst, Mem.Disp);
    Inst.addOperand(MCOperand::createReg(Mem.Length.Reg));
  }
  void addBDVAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands");
    assert(isMem(BDVMem) && "Invalid operand type");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExpr(Inst, Mem.Disp);
    Inst.addOperand(MCOperand::createReg(Mem.Index));
  }
  void addImmTLSOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
    assert(Kind == KindImmTLS && "Invalid operand type");
    addExpr(Inst, ImmTLS.Imm);
    if (ImmTLS.Sym)
      addExpr(Inst, ImmTLS.Sym);
  }

  // Used by the TableGen code to check for particular operand types.
  bool isGR32() const { return isReg(GR32Reg); }
  bool isGRH32() const { return isReg(GRH32Reg); }
  bool isGRX32() const { return false; }
  bool isGR64() const { return isReg(GR64Reg); }
  bool isGR128() const { return isReg(GR128Reg); }
  bool isADDR32() const { return isReg(GR32Reg); }
  bool isADDR64() const { return isReg(GR64Reg); }
  bool isADDR128() const { return false; }
  bool isFP32() const { return isReg(FP32Reg); }
  bool isFP64() const { return isReg(FP64Reg); }
  bool isFP128() const { return isReg(FP128Reg); }
  bool isVR32() const { return isReg(VR32Reg); }
  bool isVR64() const { return isReg(VR64Reg); }
  bool isVF128() const { return false; }
  bool isVR128() const { return isReg(VR128Reg); }
  bool isAR32() const { return isReg(AR32Reg); }
  bool isCR64() const { return isReg(CR64Reg); }
  bool isAnyReg() const { return (isReg() || isImm(0, 15)); }
  bool isBDAddr32Disp12() const { return isMemDisp12(BDMem, GR32Reg); }
  bool isBDAddr32Disp20() const { return isMemDisp20(BDMem, GR32Reg); }
  bool isBDAddr64Disp12() const { return isMemDisp12(BDMem, GR64Reg); }
  bool isBDAddr64Disp20() const { return isMemDisp20(BDMem, GR64Reg); }
  bool isBDXAddr64Disp12() const { return isMemDisp12(BDXMem, GR64Reg); }
  bool isBDXAddr64Disp20() const { return isMemDisp20(BDXMem, GR64Reg); }
  bool isBDLAddr64Disp12Len4() const { return isMemDisp12Len4(GR64Reg); }
  bool isBDLAddr64Disp12Len8() const { return isMemDisp12Len8(GR64Reg); }
  bool isBDRAddr64Disp12() const { return isMemDisp12(BDRMem, GR64Reg); }
  bool isBDVAddr64Disp12() const { return isMemDisp12(BDVMem, GR64Reg); }
  bool isU1Imm() const { return isImm(0, 1); }
  bool isU2Imm() const { return isImm(0, 3); }
  bool isU3Imm() const { return isImm(0, 7); }
  bool isU4Imm() const { return isImm(0, 15); }
  bool isU8Imm() const { return isImm(0, 255); }
  bool isS8Imm() const { return isImm(-128, 127); }
  bool isU12Imm() const { return isImm(0, 4095); }
  bool isU16Imm() const { return isImm(0, 65535); }
  bool isS16Imm() const { return isImm(-32768, 32767); }
  bool isU32Imm() const { return isImm(0, (1LL << 32) - 1); }
  bool isS32Imm() const { return isImm(-(1LL << 31), (1LL << 31) - 1); }
  bool isU48Imm() const { return isImm(0, (1LL << 48) - 1); }
};

class SystemZAsmParser : public MCTargetAsmParser {
#define GET_ASSEMBLER_HEADER
#include "SystemZGenAsmMatcher.inc"

private:
  MCAsmParser &Parser;
  enum RegisterGroup {
    RegGR,
    RegFP,
    RegV,
    RegAR,
    RegCR
  };
  struct Register {
    RegisterGroup Group;
    unsigned Num;
    SMLoc StartLoc, EndLoc;
  };

  SystemZTargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<SystemZTargetStreamer &>(TS);
  }

  bool parseRegister(Register &Reg, bool RestoreOnFailure = false);

  bool parseIntegerRegister(Register &Reg, RegisterGroup Group);

  ParseStatus parseRegister(OperandVector &Operands, RegisterKind Kind);

  ParseStatus parseAnyRegister(OperandVector &Operands);

  bool parseAddress(bool &HaveReg1, Register &Reg1, bool &HaveReg2,
                    Register &Reg2, const MCExpr *&Disp, const MCExpr *&Length,
                    bool HasLength = false, bool HasVectorIndex = false);
  bool parseAddressRegister(Register &Reg);

  bool ParseDirectiveInsn(SMLoc L);
  bool ParseDirectiveMachine(SMLoc L);
  bool ParseGNUAttribute(SMLoc L);

  ParseStatus parseAddress(OperandVector &Operands, MemoryKind MemKind,
                           RegisterKind RegKind);

  ParseStatus parsePCRel(OperandVector &Operands, int64_t MinVal,
                         int64_t MaxVal, bool AllowTLS);

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);

  // Both the hlasm and att variants still rely on the basic gnu asm
  // format with respect to inputs, clobbers, outputs etc.
  //
  // However, calling the overriden getAssemblerDialect() method in
  // AsmParser is problematic. It either returns the AssemblerDialect field
  // in the MCAsmInfo instance if the AssemblerDialect field in AsmParser is
  // unset, otherwise it returns the private AssemblerDialect field in
  // AsmParser.
  //
  // The problematic part is because, we forcibly set the inline asm dialect
  // in the AsmParser instance in AsmPrinterInlineAsm.cpp. Soo any query
  // to the overriden getAssemblerDialect function in AsmParser.cpp, will
  // not return the assembler dialect set in the respective MCAsmInfo instance.
  //
  // For this purpose, we explicitly query the SystemZMCAsmInfo instance
  // here, to get the "correct" assembler dialect, and use it in various
  // functions.
  unsigned getMAIAssemblerDialect() {
    return Parser.getContext().getAsmInfo()->getAssemblerDialect();
  }

  // An alphabetic character in HLASM is a letter from 'A' through 'Z',
  // or from 'a' through 'z', or '$', '_','#', or '@'.
  inline bool isHLASMAlpha(char C) {
    return isAlpha(C) || llvm::is_contained("_@#$", C);
  }

  // A digit in HLASM is a number from 0 to 9.
  inline bool isHLASMAlnum(char C) { return isHLASMAlpha(C) || isDigit(C); }

  // Are we parsing using the AD_HLASM dialect?
  inline bool isParsingHLASM() { return getMAIAssemblerDialect() == AD_HLASM; }

  // Are we parsing using the AD_ATT dialect?
  inline bool isParsingATT() { return getMAIAssemblerDialect() == AD_ATT; }

public:
  SystemZAsmParser(const MCSubtargetInfo &sti, MCAsmParser &parser,
                   const MCInstrInfo &MII,
                   const MCTargetOptions &Options)
    : MCTargetAsmParser(Options, sti, MII), Parser(parser) {
    MCAsmParserExtension::Initialize(Parser);

    // Alias the .word directive to .short.
    parser.addAliasForDirective(".word", ".short");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }

  // Override MCTargetAsmParser.
  ParseStatus parseDirective(AsmToken DirectiveID) override;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  bool ParseRegister(MCRegister &RegNo, SMLoc &StartLoc, SMLoc &EndLoc,
                     bool RestoreOnFailure);
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  bool isLabel(AsmToken &Token) override;

  // Used by the TableGen code to parse particular operand types.
  ParseStatus parseGR32(OperandVector &Operands) {
    return parseRegister(Operands, GR32Reg);
  }
  ParseStatus parseGRH32(OperandVector &Operands) {
    return parseRegister(Operands, GRH32Reg);
  }
  ParseStatus parseGRX32(OperandVector &Operands) {
    llvm_unreachable("GRX32 should only be used for pseudo instructions");
  }
  ParseStatus parseGR64(OperandVector &Operands) {
    return parseRegister(Operands, GR64Reg);
  }
  ParseStatus parseGR128(OperandVector &Operands) {
    return parseRegister(Operands, GR128Reg);
  }
  ParseStatus parseADDR32(OperandVector &Operands) {
    // For the AsmParser, we will accept %r0 for ADDR32 as well.
    return parseRegister(Operands, GR32Reg);
  }
  ParseStatus parseADDR64(OperandVector &Operands) {
    // For the AsmParser, we will accept %r0 for ADDR64 as well.
    return parseRegister(Operands, GR64Reg);
  }
  ParseStatus parseADDR128(OperandVector &Operands) {
    llvm_unreachable("Shouldn't be used as an operand");
  }
  ParseStatus parseFP32(OperandVector &Operands) {
    return parseRegister(Operands, FP32Reg);
  }
  ParseStatus parseFP64(OperandVector &Operands) {
    return parseRegister(Operands, FP64Reg);
  }
  ParseStatus parseFP128(OperandVector &Operands) {
    return parseRegister(Operands, FP128Reg);
  }
  ParseStatus parseVR32(OperandVector &Operands) {
    return parseRegister(Operands, VR32Reg);
  }
  ParseStatus parseVR64(OperandVector &Operands) {
    return parseRegister(Operands, VR64Reg);
  }
  ParseStatus parseVF128(OperandVector &Operands) {
    llvm_unreachable("Shouldn't be used as an operand");
  }
  ParseStatus parseVR128(OperandVector &Operands) {
    return parseRegister(Operands, VR128Reg);
  }
  ParseStatus parseAR32(OperandVector &Operands) {
    return parseRegister(Operands, AR32Reg);
  }
  ParseStatus parseCR64(OperandVector &Operands) {
    return parseRegister(Operands, CR64Reg);
  }
  ParseStatus parseAnyReg(OperandVector &Operands) {
    return parseAnyRegister(Operands);
  }
  ParseStatus parseBDAddr32(OperandVector &Operands) {
    return parseAddress(Operands, BDMem, GR32Reg);
  }
  ParseStatus parseBDAddr64(OperandVector &Operands) {
    return parseAddress(Operands, BDMem, GR64Reg);
  }
  ParseStatus parseBDXAddr64(OperandVector &Operands) {
    return parseAddress(Operands, BDXMem, GR64Reg);
  }
  ParseStatus parseBDLAddr64(OperandVector &Operands) {
    return parseAddress(Operands, BDLMem, GR64Reg);
  }
  ParseStatus parseBDRAddr64(OperandVector &Operands) {
    return parseAddress(Operands, BDRMem, GR64Reg);
  }
  ParseStatus parseBDVAddr64(OperandVector &Operands) {
    return parseAddress(Operands, BDVMem, GR64Reg);
  }
  ParseStatus parsePCRel12(OperandVector &Operands) {
    return parsePCRel(Operands, -(1LL << 12), (1LL << 12) - 1, false);
  }
  ParseStatus parsePCRel16(OperandVector &Operands) {
    return parsePCRel(Operands, -(1LL << 16), (1LL << 16) - 1, false);
  }
  ParseStatus parsePCRel24(OperandVector &Operands) {
    return parsePCRel(Operands, -(1LL << 24), (1LL << 24) - 1, false);
  }
  ParseStatus parsePCRel32(OperandVector &Operands) {
    return parsePCRel(Operands, -(1LL << 32), (1LL << 32) - 1, false);
  }
  ParseStatus parsePCRelTLS16(OperandVector &Operands) {
    return parsePCRel(Operands, -(1LL << 16), (1LL << 16) - 1, true);
  }
  ParseStatus parsePCRelTLS32(OperandVector &Operands) {
    return parsePCRel(Operands, -(1LL << 32), (1LL << 32) - 1, true);
  }
};

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "SystemZGenAsmMatcher.inc"

// Used for the .insn directives; contains information needed to parse the
// operands in the directive.
struct InsnMatchEntry {
  StringRef Format;
  uint64_t Opcode;
  int32_t NumOperands;
  MatchClassKind OperandKinds[7];
};

// For equal_range comparison.
struct CompareInsn {
  bool operator() (const InsnMatchEntry &LHS, StringRef RHS) {
    return LHS.Format < RHS;
  }
  bool operator() (StringRef LHS, const InsnMatchEntry &RHS) {
    return LHS < RHS.Format;
  }
  bool operator() (const InsnMatchEntry &LHS, const InsnMatchEntry &RHS) {
    return LHS.Format < RHS.Format;
  }
};

// Table initializing information for parsing the .insn directive.
static struct InsnMatchEntry InsnMatchTable[] = {
  /* Format, Opcode, NumOperands, OperandKinds */
  { "e", SystemZ::InsnE, 1,
    { MCK_U16Imm } },
  { "ri", SystemZ::InsnRI, 3,
    { MCK_U32Imm, MCK_AnyReg, MCK_S16Imm } },
  { "rie", SystemZ::InsnRIE, 4,
    { MCK_U48Imm, MCK_AnyReg, MCK_AnyReg, MCK_PCRel16 } },
  { "ril", SystemZ::InsnRIL, 3,
    { MCK_U48Imm, MCK_AnyReg, MCK_PCRel32 } },
  { "rilu", SystemZ::InsnRILU, 3,
    { MCK_U48Imm, MCK_AnyReg, MCK_U32Imm } },
  { "ris", SystemZ::InsnRIS, 5,
    { MCK_U48Imm, MCK_AnyReg, MCK_S8Imm, MCK_U4Imm, MCK_BDAddr64Disp12 } },
  { "rr", SystemZ::InsnRR, 3,
    { MCK_U16Imm, MCK_AnyReg, MCK_AnyReg } },
  { "rre", SystemZ::InsnRRE, 3,
    { MCK_U32Imm, MCK_AnyReg, MCK_AnyReg } },
  { "rrf", SystemZ::InsnRRF, 5,
    { MCK_U32Imm, MCK_AnyReg, MCK_AnyReg, MCK_AnyReg, MCK_U4Imm } },
  { "rrs", SystemZ::InsnRRS, 5,
    { MCK_U48Imm, MCK_AnyReg, MCK_AnyReg, MCK_U4Imm, MCK_BDAddr64Disp12 } },
  { "rs", SystemZ::InsnRS, 4,
    { MCK_U32Imm, MCK_AnyReg, MCK_AnyReg, MCK_BDAddr64Disp12 } },
  { "rse", SystemZ::InsnRSE, 4,
    { MCK_U48Imm, MCK_AnyReg, MCK_AnyReg, MCK_BDAddr64Disp12 } },
  { "rsi", SystemZ::InsnRSI, 4,
    { MCK_U48Imm, MCK_AnyReg, MCK_AnyReg, MCK_PCRel16 } },
  { "rsy", SystemZ::InsnRSY, 4,
    { MCK_U48Imm, MCK_AnyReg, MCK_AnyReg, MCK_BDAddr64Disp20 } },
  { "rx", SystemZ::InsnRX, 3,
    { MCK_U32Imm, MCK_AnyReg, MCK_BDXAddr64Disp12 } },
  { "rxe", SystemZ::InsnRXE, 3,
    { MCK_U48Imm, MCK_AnyReg, MCK_BDXAddr64Disp12 } },
  { "rxf", SystemZ::InsnRXF, 4,
    { MCK_U48Imm, MCK_AnyReg, MCK_AnyReg, MCK_BDXAddr64Disp12 } },
  { "rxy", SystemZ::InsnRXY, 3,
    { MCK_U48Imm, MCK_AnyReg, MCK_BDXAddr64Disp20 } },
  { "s", SystemZ::InsnS, 2,
    { MCK_U32Imm, MCK_BDAddr64Disp12 } },
  { "si", SystemZ::InsnSI, 3,
    { MCK_U32Imm, MCK_BDAddr64Disp12, MCK_S8Imm } },
  { "sil", SystemZ::InsnSIL, 3,
    { MCK_U48Imm, MCK_BDAddr64Disp12, MCK_U16Imm } },
  { "siy", SystemZ::InsnSIY, 3,
    { MCK_U48Imm, MCK_BDAddr64Disp20, MCK_U8Imm } },
  { "ss", SystemZ::InsnSS, 4,
    { MCK_U48Imm, MCK_BDXAddr64Disp12, MCK_BDAddr64Disp12, MCK_AnyReg } },
  { "sse", SystemZ::InsnSSE, 3,
    { MCK_U48Imm, MCK_BDAddr64Disp12, MCK_BDAddr64Disp12 } },
  { "ssf", SystemZ::InsnSSF, 4,
    { MCK_U48Imm, MCK_BDAddr64Disp12, MCK_BDAddr64Disp12, MCK_AnyReg } },
  { "vri", SystemZ::InsnVRI, 6,
    { MCK_U48Imm, MCK_VR128, MCK_VR128, MCK_U12Imm, MCK_U4Imm, MCK_U4Imm } },
  { "vrr", SystemZ::InsnVRR, 7,
    { MCK_U48Imm, MCK_VR128, MCK_VR128, MCK_VR128, MCK_U4Imm, MCK_U4Imm,
      MCK_U4Imm } },
  { "vrs", SystemZ::InsnVRS, 5,
    { MCK_U48Imm, MCK_AnyReg, MCK_VR128, MCK_BDAddr64Disp12, MCK_U4Imm } },
  { "vrv", SystemZ::InsnVRV, 4,
    { MCK_U48Imm, MCK_VR128, MCK_BDVAddr64Disp12, MCK_U4Imm } },
  { "vrx", SystemZ::InsnVRX, 4,
    { MCK_U48Imm, MCK_VR128, MCK_BDXAddr64Disp12, MCK_U4Imm } },
  { "vsi", SystemZ::InsnVSI, 4,
    { MCK_U48Imm, MCK_VR128, MCK_BDAddr64Disp12, MCK_U8Imm } }
};

static void printMCExpr(const MCExpr *E, raw_ostream &OS) {
  if (!E)
    return;
  if (auto *CE = dyn_cast<MCConstantExpr>(E))
    OS << *CE;
  else if (auto *UE = dyn_cast<MCUnaryExpr>(E))
    OS << *UE;
  else if (auto *BE = dyn_cast<MCBinaryExpr>(E))
    OS << *BE;
  else if (auto *SRE = dyn_cast<MCSymbolRefExpr>(E))
    OS << *SRE;
  else
    OS << *E;
}

void SystemZOperand::print(raw_ostream &OS) const {
  switch (Kind) {
  case KindToken:
    OS << "Token:" << getToken();
    break;
  case KindReg:
    OS << "Reg:" << SystemZInstPrinter::getRegisterName(getReg());
    break;
  case KindImm:
    OS << "Imm:";
    printMCExpr(getImm(), OS);
    break;
  case KindImmTLS:
    OS << "ImmTLS:";
    printMCExpr(getImmTLS().Imm, OS);
    if (getImmTLS().Sym) {
      OS << ", ";
      printMCExpr(getImmTLS().Sym, OS);
    }
    break;
  case KindMem: {
    const MemOp &Op = getMem();
    OS << "Mem:" << *cast<MCConstantExpr>(Op.Disp);
    if (Op.Base) {
      OS << "(";
      if (Op.MemKind == BDLMem)
        OS << *cast<MCConstantExpr>(Op.Length.Imm) << ",";
      else if (Op.MemKind == BDRMem)
        OS << SystemZInstPrinter::getRegisterName(Op.Length.Reg) << ",";
      if (Op.Index)
        OS << SystemZInstPrinter::getRegisterName(Op.Index) << ",";
      OS << SystemZInstPrinter::getRegisterName(Op.Base);
      OS << ")";
    }
    break;
  }
  case KindInvalid:
    break;
  }
}

// Parse one register of the form %<prefix><number>.
bool SystemZAsmParser::parseRegister(Register &Reg, bool RestoreOnFailure) {
  Reg.StartLoc = Parser.getTok().getLoc();

  // Eat the % prefix.
  if (Parser.getTok().isNot(AsmToken::Percent))
    return Error(Parser.getTok().getLoc(), "register expected");
  const AsmToken &PercentTok = Parser.getTok();
  Parser.Lex();

  // Expect a register name.
  if (Parser.getTok().isNot(AsmToken::Identifier)) {
    if (RestoreOnFailure)
      getLexer().UnLex(PercentTok);
    return Error(Reg.StartLoc, "invalid register");
  }

  // Check that there's a prefix.
  StringRef Name = Parser.getTok().getString();
  if (Name.size() < 2) {
    if (RestoreOnFailure)
      getLexer().UnLex(PercentTok);
    return Error(Reg.StartLoc, "invalid register");
  }
  char Prefix = Name[0];

  // Treat the rest of the register name as a register number.
  if (Name.substr(1).getAsInteger(10, Reg.Num)) {
    if (RestoreOnFailure)
      getLexer().UnLex(PercentTok);
    return Error(Reg.StartLoc, "invalid register");
  }

  // Look for valid combinations of prefix and number.
  if (Prefix == 'r' && Reg.Num < 16)
    Reg.Group = RegGR;
  else if (Prefix == 'f' && Reg.Num < 16)
    Reg.Group = RegFP;
  else if (Prefix == 'v' && Reg.Num < 32)
    Reg.Group = RegV;
  else if (Prefix == 'a' && Reg.Num < 16)
    Reg.Group = RegAR;
  else if (Prefix == 'c' && Reg.Num < 16)
    Reg.Group = RegCR;
  else {
    if (RestoreOnFailure)
      getLexer().UnLex(PercentTok);
    return Error(Reg.StartLoc, "invalid register");
  }

  Reg.EndLoc = Parser.getTok().getLoc();
  Parser.Lex();
  return false;
}

// Parse a register of kind Kind and add it to Operands.
ParseStatus SystemZAsmParser::parseRegister(OperandVector &Operands,
                                            RegisterKind Kind) {
  Register Reg;
  RegisterGroup Group;
  switch (Kind) {
  case GR32Reg:
  case GRH32Reg:
  case GR64Reg:
  case GR128Reg:
    Group = RegGR;
    break;
  case FP32Reg:
  case FP64Reg:
  case FP128Reg:
    Group = RegFP;
    break;
  case VR32Reg:
  case VR64Reg:
  case VR128Reg:
    Group = RegV;
    break;
  case AR32Reg:
    Group = RegAR;
    break;
  case CR64Reg:
    Group = RegCR;
    break;
  }

  // Handle register names of the form %<prefix><number>
  if (isParsingATT() && Parser.getTok().is(AsmToken::Percent)) {
    if (parseRegister(Reg))
      return ParseStatus::Failure;

    // Check the parsed register group "Reg.Group" with the expected "Group"
    // Have to error out if user specified wrong prefix.
    switch (Group) {
    case RegGR:
    case RegFP:
    case RegAR:
    case RegCR:
      if (Group != Reg.Group)
        return Error(Reg.StartLoc, "invalid operand for instruction");
      break;
    case RegV:
      if (Reg.Group != RegV && Reg.Group != RegFP)
        return Error(Reg.StartLoc, "invalid operand for instruction");
      break;
    }
  } else if (Parser.getTok().is(AsmToken::Integer)) {
    if (parseIntegerRegister(Reg, Group))
      return ParseStatus::Failure;
  }
  // Otherwise we didn't match a register operand.
  else
    return ParseStatus::NoMatch;

  // Determine the LLVM register number according to Kind.
  const unsigned *Regs;
  switch (Kind) {
  case GR32Reg:  Regs = SystemZMC::GR32Regs;  break;
  case GRH32Reg: Regs = SystemZMC::GRH32Regs; break;
  case GR64Reg:  Regs = SystemZMC::GR64Regs;  break;
  case GR128Reg: Regs = SystemZMC::GR128Regs; break;
  case FP32Reg:  Regs = SystemZMC::FP32Regs;  break;
  case FP64Reg:  Regs = SystemZMC::FP64Regs;  break;
  case FP128Reg: Regs = SystemZMC::FP128Regs; break;
  case VR32Reg:  Regs = SystemZMC::VR32Regs;  break;
  case VR64Reg:  Regs = SystemZMC::VR64Regs;  break;
  case VR128Reg: Regs = SystemZMC::VR128Regs; break;
  case AR32Reg:  Regs = SystemZMC::AR32Regs;  break;
  case CR64Reg:  Regs = SystemZMC::CR64Regs;  break;
  }
  if (Regs[Reg.Num] == 0)
    return Error(Reg.StartLoc, "invalid register pair");

  Operands.push_back(
      SystemZOperand::createReg(Kind, Regs[Reg.Num], Reg.StartLoc, Reg.EndLoc));
  return ParseStatus::Success;
}

// Parse any type of register (including integers) and add it to Operands.
ParseStatus SystemZAsmParser::parseAnyRegister(OperandVector &Operands) {
  SMLoc StartLoc = Parser.getTok().getLoc();

  // Handle integer values.
  if (Parser.getTok().is(AsmToken::Integer)) {
    const MCExpr *Register;
    if (Parser.parseExpression(Register))
      return ParseStatus::Failure;

    if (auto *CE = dyn_cast<MCConstantExpr>(Register)) {
      int64_t Value = CE->getValue();
      if (Value < 0 || Value > 15)
        return Error(StartLoc, "invalid register");
    }

    SMLoc EndLoc =
      SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

    Operands.push_back(SystemZOperand::createImm(Register, StartLoc, EndLoc));
  }
  else {
    if (isParsingHLASM())
      return ParseStatus::NoMatch;

    Register Reg;
    if (parseRegister(Reg))
      return ParseStatus::Failure;

    if (Reg.Num > 15)
      return Error(StartLoc, "invalid register");

    // Map to the correct register kind.
    RegisterKind Kind;
    unsigned RegNo;
    if (Reg.Group == RegGR) {
      Kind = GR64Reg;
      RegNo = SystemZMC::GR64Regs[Reg.Num];
    }
    else if (Reg.Group == RegFP) {
      Kind = FP64Reg;
      RegNo = SystemZMC::FP64Regs[Reg.Num];
    }
    else if (Reg.Group == RegV) {
      Kind = VR128Reg;
      RegNo = SystemZMC::VR128Regs[Reg.Num];
    }
    else if (Reg.Group == RegAR) {
      Kind = AR32Reg;
      RegNo = SystemZMC::AR32Regs[Reg.Num];
    }
    else if (Reg.Group == RegCR) {
      Kind = CR64Reg;
      RegNo = SystemZMC::CR64Regs[Reg.Num];
    }
    else {
      return ParseStatus::Failure;
    }

    Operands.push_back(SystemZOperand::createReg(Kind, RegNo,
                                                 Reg.StartLoc, Reg.EndLoc));
  }
  return ParseStatus::Success;
}

bool SystemZAsmParser::parseIntegerRegister(Register &Reg,
                                            RegisterGroup Group) {
  Reg.StartLoc = Parser.getTok().getLoc();
  // We have an integer token
  const MCExpr *Register;
  if (Parser.parseExpression(Register))
    return true;

  const auto *CE = dyn_cast<MCConstantExpr>(Register);
  if (!CE)
    return true;

  int64_t MaxRegNum = (Group == RegV) ? 31 : 15;
  int64_t Value = CE->getValue();
  if (Value < 0 || Value > MaxRegNum) {
    Error(Parser.getTok().getLoc(), "invalid register");
    return true;
  }

  // Assign the Register Number
  Reg.Num = (unsigned)Value;
  Reg.Group = Group;
  Reg.EndLoc = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

  // At this point, successfully parsed an integer register.
  return false;
}

// Parse a memory operand into Reg1, Reg2, Disp, and Length.
bool SystemZAsmParser::parseAddress(bool &HaveReg1, Register &Reg1,
                                    bool &HaveReg2, Register &Reg2,
                                    const MCExpr *&Disp, const MCExpr *&Length,
                                    bool HasLength, bool HasVectorIndex) {
  // Parse the displacement, which must always be present.
  if (getParser().parseExpression(Disp))
    return true;

  // Parse the optional base and index.
  HaveReg1 = false;
  HaveReg2 = false;
  Length = nullptr;

  // If we have a scenario as below:
  //   vgef %v0, 0(0), 0
  // This is an example of a "BDVMem" instruction type.
  //
  // So when we parse this as an integer register, the register group
  // needs to be tied to "RegV". Usually when the prefix is passed in
  // as %<prefix><reg-number> its easy to check which group it should belong to
  // However, if we're passing in just the integer there's no real way to
  // "check" what register group it should belong to.
  //
  // When the user passes in the register as an integer, the user assumes that
  // the compiler is responsible for substituting it as the right kind of
  // register. Whereas, when the user specifies a "prefix", the onus is on
  // the user to make sure they pass in the right kind of register.
  //
  // The restriction only applies to the first Register (i.e. Reg1). Reg2 is
  // always a general register. Reg1 should be of group RegV if "HasVectorIndex"
  // (i.e. insn is of type BDVMem) is true.
  RegisterGroup RegGroup = HasVectorIndex ? RegV : RegGR;

  if (getLexer().is(AsmToken::LParen)) {
    Parser.Lex();

    if (isParsingATT() && getLexer().is(AsmToken::Percent)) {
      // Parse the first register.
      HaveReg1 = true;
      if (parseRegister(Reg1))
        return true;
    }
    // So if we have an integer as the first token in ([tok1], ..), it could:
    // 1. Refer to a "Register" (i.e X,R,V fields in BD[X|R|V]Mem type of
    // instructions)
    // 2. Refer to a "Length" field (i.e L field in BDLMem type of instructions)
    else if (getLexer().is(AsmToken::Integer)) {
      if (HasLength) {
        // Instruction has a "Length" field, safe to parse the first token as
        // the "Length" field
        if (getParser().parseExpression(Length))
          return true;
      } else {
        // Otherwise, if the instruction has no "Length" field, parse the
        // token as a "Register". We don't have to worry about whether the
        // instruction is invalid here, because the caller will take care of
        // error reporting.
        HaveReg1 = true;
        if (parseIntegerRegister(Reg1, RegGroup))
          return true;
      }
    } else {
      // If its not an integer or a percent token, then if the instruction
      // is reported to have a "Length" then, parse it as "Length".
      if (HasLength) {
        if (getParser().parseExpression(Length))
          return true;
      }
    }

    // Check whether there's a second register.
    if (getLexer().is(AsmToken::Comma)) {
      Parser.Lex();
      HaveReg2 = true;

      if (getLexer().is(AsmToken::Integer)) {
        if (parseIntegerRegister(Reg2, RegGR))
          return true;
      } else {
        if (isParsingATT() && parseRegister(Reg2))
          return true;
      }
    }

    // Consume the closing bracket.
    if (getLexer().isNot(AsmToken::RParen))
      return Error(Parser.getTok().getLoc(), "unexpected token in address");
    Parser.Lex();
  }
  return false;
}

// Verify that Reg is a valid address register (base or index).
bool
SystemZAsmParser::parseAddressRegister(Register &Reg) {
  if (Reg.Group == RegV) {
    Error(Reg.StartLoc, "invalid use of vector addressing");
    return true;
  }
  if (Reg.Group != RegGR) {
    Error(Reg.StartLoc, "invalid address register");
    return true;
  }
  return false;
}

// Parse a memory operand and add it to Operands.  The other arguments
// are as above.
ParseStatus SystemZAsmParser::parseAddress(OperandVector &Operands,
                                           MemoryKind MemKind,
                                           RegisterKind RegKind) {
  SMLoc StartLoc = Parser.getTok().getLoc();
  unsigned Base = 0, Index = 0, LengthReg = 0;
  Register Reg1, Reg2;
  bool HaveReg1, HaveReg2;
  const MCExpr *Disp;
  const MCExpr *Length;

  bool HasLength = (MemKind == BDLMem) ? true : false;
  bool HasVectorIndex = (MemKind == BDVMem) ? true : false;
  if (parseAddress(HaveReg1, Reg1, HaveReg2, Reg2, Disp, Length, HasLength,
                   HasVectorIndex))
    return ParseStatus::Failure;

  const unsigned *Regs;
  switch (RegKind) {
  case GR32Reg: Regs = SystemZMC::GR32Regs; break;
  case GR64Reg: Regs = SystemZMC::GR64Regs; break;
  default: llvm_unreachable("invalid RegKind");
  }

  switch (MemKind) {
  case BDMem:
    // If we have Reg1, it must be an address register.
    if (HaveReg1) {
      if (parseAddressRegister(Reg1))
        return ParseStatus::Failure;
      Base = Reg1.Num == 0 ? 0 : Regs[Reg1.Num];
    }
    // There must be no Reg2.
    if (HaveReg2)
      return Error(StartLoc, "invalid use of indexed addressing");
    break;
  case BDXMem:
    // If we have Reg1, it must be an address register.
    if (HaveReg1) {
      if (parseAddressRegister(Reg1))
        return ParseStatus::Failure;
      // If the are two registers, the first one is the index and the
      // second is the base.
      if (HaveReg2)
        Index = Reg1.Num == 0 ? 0 : Regs[Reg1.Num];
      else
        Base = Reg1.Num == 0 ? 0 : Regs[Reg1.Num];
    }
    // If we have Reg2, it must be an address register.
    if (HaveReg2) {
      if (parseAddressRegister(Reg2))
        return ParseStatus::Failure;
      Base = Reg2.Num == 0 ? 0 : Regs[Reg2.Num];
    }
    break;
  case BDLMem:
    // If we have Reg2, it must be an address register.
    if (HaveReg2) {
      if (parseAddressRegister(Reg2))
        return ParseStatus::Failure;
      Base = Reg2.Num == 0 ? 0 : Regs[Reg2.Num];
    }
    // We cannot support base+index addressing.
    if (HaveReg1 && HaveReg2)
      return Error(StartLoc, "invalid use of indexed addressing");
    // We must have a length.
    if (!Length)
      return Error(StartLoc, "missing length in address");
    break;
  case BDRMem:
    // We must have Reg1, and it must be a GPR.
    if (!HaveReg1 || Reg1.Group != RegGR)
      return Error(StartLoc, "invalid operand for instruction");
    LengthReg = SystemZMC::GR64Regs[Reg1.Num];
    // If we have Reg2, it must be an address register.
    if (HaveReg2) {
      if (parseAddressRegister(Reg2))
        return ParseStatus::Failure;
      Base = Reg2.Num == 0 ? 0 : Regs[Reg2.Num];
    }
    break;
  case BDVMem:
    // We must have Reg1, and it must be a vector register.
    if (!HaveReg1 || Reg1.Group != RegV)
      return Error(StartLoc, "vector index required in address");
    Index = SystemZMC::VR128Regs[Reg1.Num];
    // If we have Reg2, it must be an address register.
    if (HaveReg2) {
      if (parseAddressRegister(Reg2))
        return ParseStatus::Failure;
      Base = Reg2.Num == 0 ? 0 : Regs[Reg2.Num];
    }
    break;
  }

  SMLoc EndLoc =
      SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  Operands.push_back(SystemZOperand::createMem(MemKind, RegKind, Base, Disp,
                                               Index, Length, LengthReg,
                                               StartLoc, EndLoc));
  return ParseStatus::Success;
}

ParseStatus SystemZAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();

  if (IDVal == ".insn")
    return ParseDirectiveInsn(DirectiveID.getLoc());
  if (IDVal == ".machine")
    return ParseDirectiveMachine(DirectiveID.getLoc());
  if (IDVal.starts_with(".gnu_attribute"))
    return ParseGNUAttribute(DirectiveID.getLoc());

  return ParseStatus::NoMatch;
}

/// ParseDirectiveInsn
/// ::= .insn [ format, encoding, (operands (, operands)*) ]
bool SystemZAsmParser::ParseDirectiveInsn(SMLoc L) {
  MCAsmParser &Parser = getParser();

  // Expect instruction format as identifier.
  StringRef Format;
  SMLoc ErrorLoc = Parser.getTok().getLoc();
  if (Parser.parseIdentifier(Format))
    return Error(ErrorLoc, "expected instruction format");

  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> Operands;

  // Find entry for this format in InsnMatchTable.
  auto EntryRange =
    std::equal_range(std::begin(InsnMatchTable), std::end(InsnMatchTable),
                     Format, CompareInsn());

  // If first == second, couldn't find a match in the table.
  if (EntryRange.first == EntryRange.second)
    return Error(ErrorLoc, "unrecognized format");

  struct InsnMatchEntry *Entry = EntryRange.first;

  // Format should match from equal_range.
  assert(Entry->Format == Format);

  // Parse the following operands using the table's information.
  for (int I = 0; I < Entry->NumOperands; I++) {
    MatchClassKind Kind = Entry->OperandKinds[I];

    SMLoc StartLoc = Parser.getTok().getLoc();

    // Always expect commas as separators for operands.
    if (getLexer().isNot(AsmToken::Comma))
      return Error(StartLoc, "unexpected token in directive");
    Lex();

    // Parse operands.
    ParseStatus ResTy;
    if (Kind == MCK_AnyReg)
      ResTy = parseAnyReg(Operands);
    else if (Kind == MCK_VR128)
      ResTy = parseVR128(Operands);
    else if (Kind == MCK_BDXAddr64Disp12 || Kind == MCK_BDXAddr64Disp20)
      ResTy = parseBDXAddr64(Operands);
    else if (Kind == MCK_BDAddr64Disp12 || Kind == MCK_BDAddr64Disp20)
      ResTy = parseBDAddr64(Operands);
    else if (Kind == MCK_BDVAddr64Disp12)
      ResTy = parseBDVAddr64(Operands);
    else if (Kind == MCK_PCRel32)
      ResTy = parsePCRel32(Operands);
    else if (Kind == MCK_PCRel16)
      ResTy = parsePCRel16(Operands);
    else {
      // Only remaining operand kind is an immediate.
      const MCExpr *Expr;
      SMLoc StartLoc = Parser.getTok().getLoc();

      // Expect immediate expression.
      if (Parser.parseExpression(Expr))
        return Error(StartLoc, "unexpected token in directive");

      SMLoc EndLoc =
        SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

      Operands.push_back(SystemZOperand::createImm(Expr, StartLoc, EndLoc));
      ResTy = ParseStatus::Success;
    }

    if (!ResTy.isSuccess())
      return true;
  }

  // Build the instruction with the parsed operands.
  MCInst Inst = MCInstBuilder(Entry->Opcode);

  for (size_t I = 0; I < Operands.size(); I++) {
    MCParsedAsmOperand &Operand = *Operands[I];
    MatchClassKind Kind = Entry->OperandKinds[I];

    // Verify operand.
    unsigned Res = validateOperandClass(Operand, Kind);
    if (Res != Match_Success)
      return Error(Operand.getStartLoc(), "unexpected operand type");

    // Add operands to instruction.
    SystemZOperand &ZOperand = static_cast<SystemZOperand &>(Operand);
    if (ZOperand.isReg())
      ZOperand.addRegOperands(Inst, 1);
    else if (ZOperand.isMem(BDMem))
      ZOperand.addBDAddrOperands(Inst, 2);
    else if (ZOperand.isMem(BDXMem))
      ZOperand.addBDXAddrOperands(Inst, 3);
    else if (ZOperand.isMem(BDVMem))
      ZOperand.addBDVAddrOperands(Inst, 3);
    else if (ZOperand.isImm())
      ZOperand.addImmOperands(Inst, 1);
    else
      llvm_unreachable("unexpected operand type");
  }

  // Emit as a regular instruction.
  Parser.getStreamer().emitInstruction(Inst, getSTI());

  return false;
}

/// ParseDirectiveMachine
/// ::= .machine [ mcpu ]
bool SystemZAsmParser::ParseDirectiveMachine(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Identifier) &&
      Parser.getTok().isNot(AsmToken::String))
    return TokError("unexpected token in '.machine' directive");

  StringRef CPU = Parser.getTok().getIdentifier();
  Parser.Lex();
  if (parseEOL())
    return true;

  MCSubtargetInfo &STI = copySTI();
  STI.setDefaultFeatures(CPU, /*TuneCPU*/ CPU, "");
  setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));

  getTargetStreamer().emitMachine(CPU);

  return false;
}

bool SystemZAsmParser::ParseGNUAttribute(SMLoc L) {
  int64_t Tag;
  int64_t IntegerValue;
  if (!Parser.parseGNUAttribute(L, Tag, IntegerValue))
    return Error(L, "malformed .gnu_attribute directive");

  // Tag_GNU_S390_ABI_Vector tag is '8' and can be 0, 1, or 2.
  if (Tag != 8 || (IntegerValue < 0 || IntegerValue > 2))
    return Error(L, "unrecognized .gnu_attribute tag/value pair.");

  Parser.getStreamer().emitGNUAttribute(Tag, IntegerValue);

  return parseEOL();
}

bool SystemZAsmParser::ParseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                                     SMLoc &EndLoc, bool RestoreOnFailure) {
  Register Reg;
  if (parseRegister(Reg, RestoreOnFailure))
    return true;
  if (Reg.Group == RegGR)
    RegNo = SystemZMC::GR64Regs[Reg.Num];
  else if (Reg.Group == RegFP)
    RegNo = SystemZMC::FP64Regs[Reg.Num];
  else if (Reg.Group == RegV)
    RegNo = SystemZMC::VR128Regs[Reg.Num];
  else if (Reg.Group == RegAR)
    RegNo = SystemZMC::AR32Regs[Reg.Num];
  else if (Reg.Group == RegCR)
    RegNo = SystemZMC::CR64Regs[Reg.Num];
  StartLoc = Reg.StartLoc;
  EndLoc = Reg.EndLoc;
  return false;
}

bool SystemZAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                     SMLoc &EndLoc) {
  return ParseRegister(Reg, StartLoc, EndLoc, /*RestoreOnFailure=*/false);
}

ParseStatus SystemZAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                               SMLoc &EndLoc) {
  bool Result = ParseRegister(Reg, StartLoc, EndLoc, /*RestoreOnFailure=*/true);
  bool PendingErrors = getParser().hasPendingError();
  getParser().clearPendingErrors();
  if (PendingErrors)
    return ParseStatus::Failure;
  if (Result)
    return ParseStatus::NoMatch;
  return ParseStatus::Success;
}

bool SystemZAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {

  // Apply mnemonic aliases first, before doing anything else, in
  // case the target uses it.
  applyMnemonicAliases(Name, getAvailableFeatures(), getMAIAssemblerDialect());

  Operands.push_back(SystemZOperand::createToken(Name, NameLoc));

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (parseOperand(Operands, Name)) {
      return true;
    }

    // Read any subsequent operands.
    while (getLexer().is(AsmToken::Comma)) {
      Parser.Lex();

      if (isParsingHLASM() && getLexer().is(AsmToken::Space))
        return Error(
            Parser.getTok().getLoc(),
            "No space allowed between comma that separates operand entries");

      if (parseOperand(Operands, Name)) {
        return true;
      }
    }

    // Under the HLASM variant, we could have the remark field
    // The remark field occurs after the operation entries
    // There is a space that separates the operation entries and the
    // remark field.
    if (isParsingHLASM() && getTok().is(AsmToken::Space)) {
      // We've confirmed that there is a Remark field.
      StringRef Remark(getLexer().LexUntilEndOfStatement());
      Parser.Lex();

      // If there is nothing after the space, then there is nothing to emit
      // We could have a situation as this:
      // "  \n"
      // After lexing above, we will have
      // "\n"
      // This isn't an explicit remark field, so we don't have to output
      // this as a comment.
      if (Remark.size())
        // Output the entire Remarks Field as a comment
        getStreamer().AddComment(Remark);
    }

    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      SMLoc Loc = getLexer().getLoc();
      return Error(Loc, "unexpected token in argument list");
    }
  }

  // Consume the EndOfStatement.
  Parser.Lex();
  return false;
}

bool SystemZAsmParser::parseOperand(OperandVector &Operands,
                                    StringRef Mnemonic) {
  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.  Force all
  // features to be available during the operand check, or else we will fail to
  // find the custom parser, and then we will later get an InvalidOperand error
  // instead of a MissingFeature errror.
  FeatureBitset AvailableFeatures = getAvailableFeatures();
  FeatureBitset All;
  All.set();
  setAvailableFeatures(All);
  ParseStatus Res = MatchOperandParserImpl(Operands, Mnemonic);
  setAvailableFeatures(AvailableFeatures);
  if (Res.isSuccess())
    return false;

  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (Res.isFailure())
    return true;

  // Check for a register.  All real register operands should have used
  // a context-dependent parse routine, which gives the required register
  // class.  The code is here to mop up other cases, like those where
  // the instruction isn't recognized.
  if (isParsingATT() && Parser.getTok().is(AsmToken::Percent)) {
    Register Reg;
    if (parseRegister(Reg))
      return true;
    Operands.push_back(SystemZOperand::createInvalid(Reg.StartLoc, Reg.EndLoc));
    return false;
  }

  // The only other type of operand is an immediate or address.  As above,
  // real address operands should have used a context-dependent parse routine,
  // so we treat any plain expression as an immediate.
  SMLoc StartLoc = Parser.getTok().getLoc();
  Register Reg1, Reg2;
  bool HaveReg1, HaveReg2;
  const MCExpr *Expr;
  const MCExpr *Length;
  if (parseAddress(HaveReg1, Reg1, HaveReg2, Reg2, Expr, Length,
                   /*HasLength*/ true, /*HasVectorIndex*/ true))
    return true;
  // If the register combination is not valid for any instruction, reject it.
  // Otherwise, fall back to reporting an unrecognized instruction.
  if (HaveReg1 && Reg1.Group != RegGR && Reg1.Group != RegV
      && parseAddressRegister(Reg1))
    return true;
  if (HaveReg2 && parseAddressRegister(Reg2))
    return true;

  SMLoc EndLoc =
    SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  if (HaveReg1 || HaveReg2 || Length)
    Operands.push_back(SystemZOperand::createInvalid(StartLoc, EndLoc));
  else
    Operands.push_back(SystemZOperand::createImm(Expr, StartLoc, EndLoc));
  return false;
}

bool SystemZAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult;

  unsigned Dialect = getMAIAssemblerDialect();

  FeatureBitset MissingFeatures;
  MatchResult = MatchInstructionImpl(Operands, Inst, ErrorInfo, MissingFeatures,
                                     MatchingInlineAsm, Dialect);
  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;

  case Match_MissingFeature: {
    assert(MissingFeatures.any() && "Unknown missing feature!");
    // Special case the error message for the very common case where only
    // a single subtarget feature is missing
    std::string Msg = "instruction requires:";
    for (unsigned I = 0, E = MissingFeatures.size(); I != E; ++I) {
      if (MissingFeatures[I]) {
        Msg += " ";
        Msg += getSubtargetFeatureName(I);
      }
    }
    return Error(IDLoc, Msg);
  }

  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");

      ErrorLoc = ((SystemZOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }

  case Match_MnemonicFail: {
    FeatureBitset FBS = ComputeAvailableFeatures(getSTI().getFeatureBits());
    std::string Suggestion = SystemZMnemonicSpellCheck(
        ((SystemZOperand &)*Operands[0]).getToken(), FBS, Dialect);
    return Error(IDLoc, "invalid instruction" + Suggestion,
                 ((SystemZOperand &)*Operands[0]).getLocRange());
  }
  }

  llvm_unreachable("Unexpected match type");
}

ParseStatus SystemZAsmParser::parsePCRel(OperandVector &Operands,
                                         int64_t MinVal, int64_t MaxVal,
                                         bool AllowTLS) {
  MCContext &Ctx = getContext();
  MCStreamer &Out = getStreamer();
  const MCExpr *Expr;
  SMLoc StartLoc = Parser.getTok().getLoc();
  if (getParser().parseExpression(Expr))
    return ParseStatus::NoMatch;

  auto IsOutOfRangeConstant = [&](const MCExpr *E, bool Negate) -> bool {
    if (auto *CE = dyn_cast<MCConstantExpr>(E)) {
      int64_t Value = CE->getValue();
      if (Negate)
        Value = -Value;
      if ((Value & 1) || Value < MinVal || Value > MaxVal)
        return true;
    }
    return false;
  };

  // For consistency with the GNU assembler, treat immediates as offsets
  // from ".".
  if (auto *CE = dyn_cast<MCConstantExpr>(Expr)) {
    if (isParsingHLASM())
      return Error(StartLoc, "Expected PC-relative expression");
    if (IsOutOfRangeConstant(CE, false))
      return Error(StartLoc, "offset out of range");
    int64_t Value = CE->getValue();
    MCSymbol *Sym = Ctx.createTempSymbol();
    Out.emitLabel(Sym);
    const MCExpr *Base = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None,
                                                 Ctx);
    Expr = Value == 0 ? Base : MCBinaryExpr::createAdd(Base, Expr, Ctx);
  }

  // For consistency with the GNU assembler, conservatively assume that a
  // constant offset must by itself be within the given size range.
  if (const auto *BE = dyn_cast<MCBinaryExpr>(Expr))
    if (IsOutOfRangeConstant(BE->getLHS(), false) ||
        IsOutOfRangeConstant(BE->getRHS(),
                             BE->getOpcode() == MCBinaryExpr::Sub))
      return Error(StartLoc, "offset out of range");

  // Optionally match :tls_gdcall: or :tls_ldcall: followed by a TLS symbol.
  const MCExpr *Sym = nullptr;
  if (AllowTLS && getLexer().is(AsmToken::Colon)) {
    Parser.Lex();

    if (Parser.getTok().isNot(AsmToken::Identifier))
      return Error(Parser.getTok().getLoc(), "unexpected token");

    MCSymbolRefExpr::VariantKind Kind = MCSymbolRefExpr::VK_None;
    StringRef Name = Parser.getTok().getString();
    if (Name == "tls_gdcall")
      Kind = MCSymbolRefExpr::VK_TLSGD;
    else if (Name == "tls_ldcall")
      Kind = MCSymbolRefExpr::VK_TLSLDM;
    else
      return Error(Parser.getTok().getLoc(), "unknown TLS tag");
    Parser.Lex();

    if (Parser.getTok().isNot(AsmToken::Colon))
      return Error(Parser.getTok().getLoc(), "unexpected token");
    Parser.Lex();

    if (Parser.getTok().isNot(AsmToken::Identifier))
      return Error(Parser.getTok().getLoc(), "unexpected token");

    StringRef Identifier = Parser.getTok().getString();
    Sym = MCSymbolRefExpr::create(Ctx.getOrCreateSymbol(Identifier),
                                  Kind, Ctx);
    Parser.Lex();
  }

  SMLoc EndLoc =
    SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

  if (AllowTLS)
    Operands.push_back(SystemZOperand::createImmTLS(Expr, Sym,
                                                    StartLoc, EndLoc));
  else
    Operands.push_back(SystemZOperand::createImm(Expr, StartLoc, EndLoc));

  return ParseStatus::Success;
}

bool SystemZAsmParser::isLabel(AsmToken &Token) {
  if (isParsingATT())
    return true;

  // HLASM labels are ordinary symbols.
  // An HLASM label always starts at column 1.
  // An ordinary symbol syntax is laid out as follows:
  // Rules:
  // 1. Has to start with an "alphabetic character". Can be followed by up to
  //    62 alphanumeric characters. An "alphabetic character", in this scenario,
  //    is a letter from 'A' through 'Z', or from 'a' through 'z',
  //    or '$', '_', '#', or '@'
  // 2. Labels are case-insensitive. E.g. "lab123", "LAB123", "lAb123", etc.
  //    are all treated as the same symbol. However, the processing for the case
  //    folding will not be done in this function.
  StringRef RawLabel = Token.getString();
  SMLoc Loc = Token.getLoc();

  // An HLASM label cannot be empty.
  if (!RawLabel.size())
    return !Error(Loc, "HLASM Label cannot be empty");

  // An HLASM label cannot exceed greater than 63 characters.
  if (RawLabel.size() > 63)
    return !Error(Loc, "Maximum length for HLASM Label is 63 characters");

  // A label must start with an "alphabetic character".
  if (!isHLASMAlpha(RawLabel[0]))
    return !Error(Loc, "HLASM Label has to start with an alphabetic "
                       "character or the underscore character");

  // Now, we've established that the length is valid
  // and the first character is alphabetic.
  // Check whether remaining string is alphanumeric.
  for (unsigned I = 1; I < RawLabel.size(); ++I)
    if (!isHLASMAlnum(RawLabel[I]))
      return !Error(Loc, "HLASM Label has to be alphanumeric");

  return true;
}

// Force static initialization.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSystemZAsmParser() {
  RegisterMCAsmParser<SystemZAsmParser> X(getTheSystemZTarget());
}
