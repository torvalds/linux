//===- XtensaAsmParser.cpp - Parse Xtensa assembly to MCInst instructions -===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/XtensaMCExpr.h"
#include "MCTargetDesc/XtensaMCTargetDesc.h"
#include "MCTargetDesc/XtensaTargetStreamer.h"
#include "TargetInfo/XtensaTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

#define DEBUG_TYPE "xtensa-asm-parser"

struct XtensaOperand;

class XtensaAsmParser : public MCTargetAsmParser {

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

  XtensaTargetStreamer &getTargetStreamer() {
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<XtensaTargetStreamer &>(TS);
  }

  ParseStatus parseDirective(AsmToken DirectiveID) override;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  bool processInstruction(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out,
                          const MCSubtargetInfo *STI);

// Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "XtensaGenAsmMatcher.inc"

  ParseStatus parseImmediate(OperandVector &Operands);
  ParseStatus parseRegister(OperandVector &Operands, bool AllowParens = false,
                            bool SR = false);
  ParseStatus parseOperandWithModifier(OperandVector &Operands);
  bool parseOperand(OperandVector &Operands, StringRef Mnemonic,
                    bool SR = false);
  bool ParseInstructionWithSR(ParseInstructionInfo &Info, StringRef Name,
                              SMLoc NameLoc, OperandVector &Operands);
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override {
    return ParseStatus::NoMatch;
  }
  ParseStatus parsePCRelTarget(OperandVector &Operands);
  bool parseLiteralDirective(SMLoc L);

public:
  enum XtensaMatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "XtensaGenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  XtensaAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                  const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

// Return true if Expr is in the range [MinValue, MaxValue].
static bool inRange(const MCExpr *Expr, int64_t MinValue, int64_t MaxValue) {
  if (auto *CE = dyn_cast<MCConstantExpr>(Expr)) {
    int64_t Value = CE->getValue();
    return Value >= MinValue && Value <= MaxValue;
  }
  return false;
}

struct XtensaOperand : public MCParsedAsmOperand {

  enum KindTy {
    Token,
    Register,
    Immediate,
  } Kind;

  struct RegOp {
    unsigned RegNum;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  SMLoc StartLoc, EndLoc;
  union {
    StringRef Tok;
    RegOp Reg;
    ImmOp Imm;
  };

  XtensaOperand(KindTy K) : MCParsedAsmOperand(), Kind(K) {}

public:
  XtensaOperand(const XtensaOperand &o) : MCParsedAsmOperand() {
    Kind = o.Kind;
    StartLoc = o.StartLoc;
    EndLoc = o.EndLoc;
    switch (Kind) {
    case Register:
      Reg = o.Reg;
      break;
    case Immediate:
      Imm = o.Imm;
      break;
    case Token:
      Tok = o.Tok;
      break;
    }
  }

  bool isToken() const override { return Kind == Token; }
  bool isReg() const override { return Kind == Register; }
  bool isImm() const override { return Kind == Immediate; }
  bool isMem() const override { return false; }

  bool isImm(int64_t MinValue, int64_t MaxValue) const {
    return Kind == Immediate && inRange(getImm(), MinValue, MaxValue);
  }

  bool isImm8() const { return isImm(-128, 127); }

  bool isImm8_sh8() const {
    return isImm(-32768, 32512) &&
           ((cast<MCConstantExpr>(getImm())->getValue() & 0xFF) == 0);
  }

  bool isImm12() const { return isImm(-2048, 2047); }

  // Convert MOVI to literal load, when immediate is not in range (-2048, 2047)
  bool isImm12m() const { return Kind == Immediate; }

  bool isOffset4m32() const {
    return isImm(0, 60) &&
           ((cast<MCConstantExpr>(getImm())->getValue() & 0x3) == 0);
  }

  bool isOffset8m8() const { return isImm(0, 255); }

  bool isOffset8m16() const {
    return isImm(0, 510) &&
           ((cast<MCConstantExpr>(getImm())->getValue() & 0x1) == 0);
  }

  bool isOffset8m32() const {
    return isImm(0, 1020) &&
           ((cast<MCConstantExpr>(getImm())->getValue() & 0x3) == 0);
  }

  bool isUimm4() const { return isImm(0, 15); }

  bool isUimm5() const { return isImm(0, 31); }

  bool isImm8n_7() const { return isImm(-8, 7); }

  bool isShimm1_31() const { return isImm(1, 31); }

  bool isImm16_31() const { return isImm(16, 31); }

  bool isImm1_16() const { return isImm(1, 16); }

  bool isB4const() const {
    if (Kind != Immediate)
      return false;
    if (auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      switch (Value) {
      case -1:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 10:
      case 12:
      case 16:
      case 32:
      case 64:
      case 128:
      case 256:
        return true;
      default:
        return false;
      }
    }
    return false;
  }

  bool isB4constu() const {
    if (Kind != Immediate)
      return false;
    if (auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      switch (Value) {
      case 32768:
      case 65536:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 10:
      case 12:
      case 16:
      case 32:
      case 64:
      case 128:
      case 256:
        return true;
      default:
        return false;
      }
    }
    return false;
  }

  /// getStartLoc - Gets location of the first token of this operand
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Gets location of the last token of this operand
  SMLoc getEndLoc() const override { return EndLoc; }

  MCRegister getReg() const override {
    assert(Kind == Register && "Invalid type access!");
    return Reg.RegNum;
  }

  const MCExpr *getImm() const {
    assert(Kind == Immediate && "Invalid type access!");
    return Imm.Val;
  }

  StringRef getToken() const {
    assert(Kind == Token && "Invalid type access!");
    return Tok;
  }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case Immediate:
      OS << *getImm();
      break;
    case Register:
      OS << "<register x";
      OS << getReg() << ">";
      break;
    case Token:
      OS << "'" << getToken() << "'";
      break;
    }
  }

  static std::unique_ptr<XtensaOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<XtensaOperand>(Token);
    Op->Tok = Str;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<XtensaOperand> createReg(unsigned RegNo, SMLoc S,
                                                  SMLoc E) {
    auto Op = std::make_unique<XtensaOperand>(Register);
    Op->Reg.RegNum = RegNo;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<XtensaOperand> createImm(const MCExpr *Val, SMLoc S,
                                                  SMLoc E) {
    auto Op = std::make_unique<XtensaOperand>(Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    assert(Expr && "Expr shouldn't be null!");
    int64_t Imm = 0;
    bool IsConstant = false;

    if (auto *CE = dyn_cast<MCConstantExpr>(Expr)) {
      IsConstant = true;
      Imm = CE->getValue();
    }

    if (IsConstant)
      Inst.addOperand(MCOperand::createImm(Imm));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  // Used by the TableGen Code
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }
};

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "XtensaGenAsmMatcher.inc"

unsigned XtensaAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                     unsigned Kind) {
  return Match_InvalidOperand;
}

static SMLoc RefineErrorLoc(const SMLoc Loc, const OperandVector &Operands,
                            uint64_t ErrorInfo) {
  if (ErrorInfo != ~0ULL && ErrorInfo < Operands.size()) {
    SMLoc ErrorLoc = Operands[ErrorInfo]->getStartLoc();
    if (ErrorLoc == SMLoc())
      return Loc;
    return ErrorLoc;
  }
  return Loc;
}

bool XtensaAsmParser::processInstruction(MCInst &Inst, SMLoc IDLoc,
                                         MCStreamer &Out,
                                         const MCSubtargetInfo *STI) {
  Inst.setLoc(IDLoc);
  const unsigned Opcode = Inst.getOpcode();
  switch (Opcode) {
  case Xtensa::L32R: {
    const MCSymbolRefExpr *OpExpr =
        static_cast<const MCSymbolRefExpr *>(Inst.getOperand(1).getExpr());
    XtensaMCExpr::VariantKind Kind = XtensaMCExpr::VK_Xtensa_None;
    const MCExpr *NewOpExpr = XtensaMCExpr::create(OpExpr, Kind, getContext());
    Inst.getOperand(1).setExpr(NewOpExpr);
    break;
  }
  case Xtensa::MOVI: {
    XtensaTargetStreamer &TS = this->getTargetStreamer();

    // Expand MOVI operand
    if (!Inst.getOperand(1).isExpr()) {
      uint64_t ImmOp64 = Inst.getOperand(1).getImm();
      int32_t Imm = ImmOp64;
      if (!isInt<12>(Imm)) {
        XtensaTargetStreamer &TS = this->getTargetStreamer();
        MCInst TmpInst;
        TmpInst.setLoc(IDLoc);
        TmpInst.setOpcode(Xtensa::L32R);
        const MCExpr *Value = MCConstantExpr::create(ImmOp64, getContext());
        MCSymbol *Sym = getContext().createTempSymbol();
        const MCExpr *Expr = MCSymbolRefExpr::create(
            Sym, MCSymbolRefExpr::VK_None, getContext());
        const MCExpr *OpExpr = XtensaMCExpr::create(
            Expr, XtensaMCExpr::VK_Xtensa_None, getContext());
        TmpInst.addOperand(Inst.getOperand(0));
        MCOperand Op1 = MCOperand::createExpr(OpExpr);
        TmpInst.addOperand(Op1);
        TS.emitLiteral(Sym, Value, true, IDLoc);
        Inst = TmpInst;
      }
    } else {
      MCInst TmpInst;
      TmpInst.setLoc(IDLoc);
      TmpInst.setOpcode(Xtensa::L32R);
      const MCExpr *Value = Inst.getOperand(1).getExpr();
      MCSymbol *Sym = getContext().createTempSymbol();
      const MCExpr *Expr =
          MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());
      const MCExpr *OpExpr = XtensaMCExpr::create(
          Expr, XtensaMCExpr::VK_Xtensa_None, getContext());
      TmpInst.addOperand(Inst.getOperand(0));
      MCOperand Op1 = MCOperand::createExpr(OpExpr);
      TmpInst.addOperand(Op1);
      Inst = TmpInst;
      TS.emitLiteral(Sym, Value, true, IDLoc);
    }
    break;
  }
  default:
    break;
  }

  return true;
}

bool XtensaAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  MCInst Inst;
  auto Result =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (Result) {
  default:
    break;
  case Match_Success:
    processInstruction(Inst, IDLoc, Out, STI);
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  case Match_MissingFeature:
    return Error(IDLoc, "instruction use requires an option to be enabled");
  case Match_MnemonicFail:
    return Error(IDLoc, "unrecognized instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0U) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");

      ErrorLoc = ((XtensaOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_InvalidImm8:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [-128, 127]");
  case Match_InvalidImm8_sh8:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [-32768, 32512], first 8 bits "
                 "should be zero");
  case Match_InvalidB4const:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected b4const immediate");
  case Match_InvalidB4constu:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected b4constu immediate");
  case Match_InvalidImm12:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [-2048, 2047]");
  case Match_InvalidImm12m:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [-2048, 2047]");
  case Match_InvalidImm1_16:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [1, 16]");
  case Match_InvalidShimm1_31:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [1, 31]");
  case Match_InvalidUimm4:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [0, 15]");
  case Match_InvalidUimm5:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [0, 31]");
  case Match_InvalidOffset8m8:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [0, 255]");
  case Match_InvalidOffset8m16:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [0, 510], first bit "
                 "should be zero");
  case Match_InvalidOffset8m32:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [0, 1020], first 2 bits "
                 "should be zero");
  case Match_InvalidOffset4m32:
    return Error(RefineErrorLoc(IDLoc, Operands, ErrorInfo),
                 "expected immediate in range [0, 60], first 2 bits "
                 "should be zero");
  }

  report_fatal_error("Unknown match type detected!");
}

ParseStatus XtensaAsmParser::parsePCRelTarget(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  LLVM_DEBUG(dbgs() << "parsePCRelTarget\n");

  SMLoc S = getLexer().getLoc();

  // Expressions are acceptable
  const MCExpr *Expr = nullptr;
  if (Parser.parseExpression(Expr)) {
    // We have no way of knowing if a symbol was consumed so we must ParseFail
    return ParseStatus::Failure;
  }

  // Currently not support constants
  if (Expr->getKind() == MCExpr::ExprKind::Constant)
    return Error(getLoc(), "unknown operand");

  Operands.push_back(XtensaOperand::createImm(Expr, S, getLexer().getLoc()));
  return ParseStatus::Success;
}

bool XtensaAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                    SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  Reg = Xtensa::NoRegister;
  StringRef Name = getLexer().getTok().getIdentifier();

  if (!MatchRegisterName(Name) && !MatchRegisterAltName(Name)) {
    getParser().Lex(); // Eat identifier token.
    return false;
  }

  return Error(StartLoc, "invalid register name");
}

ParseStatus XtensaAsmParser::parseRegister(OperandVector &Operands,
                                           bool AllowParens, bool SR) {
  SMLoc FirstS = getLoc();
  bool HadParens = false;
  AsmToken Buf[2];
  StringRef RegName;

  // If this a parenthesised register name is allowed, parse it atomically
  if (AllowParens && getLexer().is(AsmToken::LParen)) {
    size_t ReadCount = getLexer().peekTokens(Buf);
    if (ReadCount == 2 && Buf[1].getKind() == AsmToken::RParen) {
      if ((Buf[0].getKind() == AsmToken::Integer) && (!SR))
        return ParseStatus::NoMatch;
      HadParens = true;
      getParser().Lex(); // Eat '('
    }
  }

  unsigned RegNo = 0;

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::Integer:
    if (!SR)
      return ParseStatus::NoMatch;
    RegName = getLexer().getTok().getString();
    RegNo = MatchRegisterName(RegName);
    if (RegNo == 0)
      RegNo = MatchRegisterAltName(RegName);
    break;
  case AsmToken::Identifier:
    RegName = getLexer().getTok().getIdentifier();
    RegNo = MatchRegisterName(RegName);
    if (RegNo == 0)
      RegNo = MatchRegisterAltName(RegName);
    break;
  }

  if (RegNo == 0) {
    if (HadParens)
      getLexer().UnLex(Buf[0]);
    return ParseStatus::NoMatch;
  }
  if (HadParens)
    Operands.push_back(XtensaOperand::createToken("(", FirstS));
  SMLoc S = getLoc();
  SMLoc E = getParser().getTok().getEndLoc();
  getLexer().Lex();
  Operands.push_back(XtensaOperand::createReg(RegNo, S, E));

  if (HadParens) {
    getParser().Lex(); // Eat ')'
    Operands.push_back(XtensaOperand::createToken(")", getLoc()));
  }

  return ParseStatus::Success;
}

ParseStatus XtensaAsmParser::parseImmediate(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E;
  const MCExpr *Res;

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Tilde:
  case AsmToken::Integer:
  case AsmToken::String:
    if (getParser().parseExpression(Res))
      return ParseStatus::Failure;
    break;
  case AsmToken::Identifier: {
    StringRef Identifier;
    if (getParser().parseIdentifier(Identifier))
      return ParseStatus::Failure;

    MCSymbol *Sym = getContext().getOrCreateSymbol(Identifier);
    Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());
    break;
  }
  case AsmToken::Percent:
    return parseOperandWithModifier(Operands);
  }

  E = SMLoc::getFromPointer(S.getPointer() - 1);
  Operands.push_back(XtensaOperand::createImm(Res, S, E));
  return ParseStatus::Success;
}

ParseStatus XtensaAsmParser::parseOperandWithModifier(OperandVector &Operands) {
  return ParseStatus::Failure;
}

/// Looks at a token type and creates the relevant operand
/// from this information, adding to Operands.
/// If operand was parsed, returns false, else true.
bool XtensaAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic,
                                   bool SR) {
  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  ParseStatus Res = MatchOperandParserImpl(Operands, Mnemonic);
  if (Res.isSuccess())
    return false;

  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (Res.isFailure())
    return true;

  // Attempt to parse token as register
  if (parseRegister(Operands, true, SR).isSuccess())
    return false;

  // Attempt to parse token as an immediate
  if (parseImmediate(Operands).isSuccess())
    return false;

  // Finally we have exhausted all options and must declare defeat.
  return Error(getLoc(), "unknown operand");
}

bool XtensaAsmParser::ParseInstructionWithSR(ParseInstructionInfo &Info,
                                             StringRef Name, SMLoc NameLoc,
                                             OperandVector &Operands) {
  if ((Name.starts_with("wsr.") || Name.starts_with("rsr.") ||
       Name.starts_with("xsr.")) &&
      (Name.size() > 4)) {
    // Parse case when instruction name is concatenated with SR register
    // name, like "wsr.sar a1"

    // First operand is token for instruction
    Operands.push_back(XtensaOperand::createToken(Name.take_front(3), NameLoc));

    StringRef RegName = Name.drop_front(4);
    unsigned RegNo = MatchRegisterName(RegName);

    if (RegNo == 0)
      RegNo = MatchRegisterAltName(RegName);

    if (RegNo == 0)
      return Error(NameLoc, "invalid register name");

    // Parse operand
    if (parseOperand(Operands, Name))
      return true;

    SMLoc S = getLoc();
    SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);
    Operands.push_back(XtensaOperand::createReg(RegNo, S, E));
  } else {
    // First operand is token for instruction
    Operands.push_back(XtensaOperand::createToken(Name, NameLoc));

    // Parse first operand
    if (parseOperand(Operands, Name))
      return true;

    if (!parseOptionalToken(AsmToken::Comma)) {
      SMLoc Loc = getLexer().getLoc();
      getParser().eatToEndOfStatement();
      return Error(Loc, "unexpected token");
    }

    // Parse second operand
    if (parseOperand(Operands, Name, true))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool XtensaAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name, SMLoc NameLoc,
                                       OperandVector &Operands) {
  if (Name.starts_with("wsr") || Name.starts_with("rsr") ||
      Name.starts_with("xsr")) {
    return ParseInstructionWithSR(Info, Name, NameLoc, Operands);
  }

  // First operand is token for instruction
  Operands.push_back(XtensaOperand::createToken(Name, NameLoc));

  // If there are no more operands, then finish
  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  // Parse first operand
  if (parseOperand(Operands, Name))
    return true;

  // Parse until end of statement, consuming commas between operands
  while (parseOptionalToken(AsmToken::Comma))
    if (parseOperand(Operands, Name))
      return true;

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool XtensaAsmParser::parseLiteralDirective(SMLoc L) {
  MCAsmParser &Parser = getParser();
  const MCExpr *Value;
  SMLoc LiteralLoc = getLexer().getLoc();
  XtensaTargetStreamer &TS = this->getTargetStreamer();

  if (Parser.parseExpression(Value))
    return true;

  const MCSymbolRefExpr *SE = dyn_cast<MCSymbolRefExpr>(Value);

  if (!SE)
    return Error(LiteralLoc, "literal label must be a symbol");

  if (Parser.parseComma())
    return true;

  SMLoc OpcodeLoc = getLexer().getLoc();
  if (parseOptionalToken(AsmToken::EndOfStatement))
    return Error(OpcodeLoc, "expected value");

  if (Parser.parseExpression(Value))
    return true;

  if (parseEOL())
    return true;

  MCSymbol *Sym = getContext().getOrCreateSymbol(SE->getSymbol().getName());

  TS.emitLiteral(Sym, Value, true, LiteralLoc);

  return false;
}

ParseStatus XtensaAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getString();
  SMLoc Loc = getLexer().getLoc();

  if (IDVal == ".literal_position") {
    XtensaTargetStreamer &TS = this->getTargetStreamer();
    TS.emitLiteralPosition();
    return parseEOL();
  }

  if (IDVal == ".literal") {
    return parseLiteralDirective(Loc);
  }

  return ParseStatus::NoMatch;
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXtensaAsmParser() {
  RegisterMCAsmParser<XtensaAsmParser> X(getTheXtensaTarget());
}
