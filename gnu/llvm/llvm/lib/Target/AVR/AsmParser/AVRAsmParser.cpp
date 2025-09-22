//===---- AVRAsmParser.cpp - Parse AVR assembly to MCInst instructions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "AVRRegisterInfo.h"
#include "MCTargetDesc/AVRMCELFStreamer.h"
#include "MCTargetDesc/AVRMCExpr.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"
#include "TargetInfo/AVRTargetInfo.h"

#include "llvm/ADT/APInt.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

#include <array>
#include <sstream>

#define DEBUG_TYPE "avr-asm-parser"

using namespace llvm;

namespace {
/// Parses AVR assembly from a stream.
class AVRAsmParser : public MCTargetAsmParser {
  const MCSubtargetInfo &STI;
  MCAsmParser &Parser;
  const MCRegisterInfo *MRI;
  const std::string GENERATE_STUBS = "gs";

  enum AVRMatchResultTy {
    Match_InvalidRegisterOnTiny = FIRST_TARGET_MATCH_RESULT_TY + 1,
  };

#define GET_ASSEMBLER_HEADER
#include "AVRGenAsmMatcher.inc"

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;

  ParseStatus parseMemriOperand(OperandVector &Operands);

  bool parseOperand(OperandVector &Operands, bool maybeReg);
  int parseRegisterName(MCRegister (*matchFn)(StringRef));
  int parseRegisterName();
  int parseRegister(bool RestoreOnFailure = false);
  bool tryParseRegisterOperand(OperandVector &Operands);
  bool tryParseExpression(OperandVector &Operands, int64_t offset);
  bool tryParseRelocExpression(OperandVector &Operands);
  void eatComma();

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  unsigned toDREG(unsigned Reg, unsigned From = AVR::sub_lo) {
    MCRegisterClass const *Class = &AVRMCRegisterClasses[AVR::DREGSRegClassID];
    return MRI->getMatchingSuperReg(Reg, From, Class);
  }

  bool emit(MCInst &Instruction, SMLoc const &Loc, MCStreamer &Out) const;
  bool invalidOperand(SMLoc const &Loc, OperandVector const &Operands,
                      uint64_t const &ErrorInfo);
  bool missingFeature(SMLoc const &Loc, uint64_t const &ErrorInfo);

  ParseStatus parseLiteralValues(unsigned SizeInBytes, SMLoc L);

public:
  AVRAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
               const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), STI(STI), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    MRI = getContext().getRegisterInfo();

    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

  MCAsmParser &getParser() const { return Parser; }
  MCAsmLexer &getLexer() const { return Parser.getLexer(); }
};

/// An parsed AVR assembly operand.
class AVROperand : public MCParsedAsmOperand {
  typedef MCParsedAsmOperand Base;
  enum KindTy { k_Immediate, k_Register, k_Token, k_Memri } Kind;

public:
  AVROperand(StringRef Tok, SMLoc const &S)
      : Kind(k_Token), Tok(Tok), Start(S), End(S) {}
  AVROperand(unsigned Reg, SMLoc const &S, SMLoc const &E)
      : Kind(k_Register), RegImm({Reg, nullptr}), Start(S), End(E) {}
  AVROperand(MCExpr const *Imm, SMLoc const &S, SMLoc const &E)
      : Kind(k_Immediate), RegImm({0, Imm}), Start(S), End(E) {}
  AVROperand(unsigned Reg, MCExpr const *Imm, SMLoc const &S, SMLoc const &E)
      : Kind(k_Memri), RegImm({Reg, Imm}), Start(S), End(E) {}

  struct RegisterImmediate {
    unsigned Reg;
    MCExpr const *Imm;
  };
  union {
    StringRef Tok;
    RegisterImmediate RegImm;
  };

  SMLoc Start, End;

public:
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Register && "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediate when possible
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Immediate && "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  /// Adds the contained reg+imm operand to an instruction.
  void addMemriOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Memri && "Unexpected operand kind");
    assert(N == 2 && "Invalid number of operands");

    Inst.addOperand(MCOperand::createReg(getReg()));
    addExpr(Inst, getImm());
  }

  void addImmCom8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The operand is actually a imm8, but we have its bitwise
    // negation in the assembly source, so twiddle it here.
    const auto *CE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(~(uint8_t)CE->getValue()));
  }

  bool isImmCom8() const {
    if (!isImm())
      return false;
    const auto *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return false;
    int64_t Value = CE->getValue();
    return isUInt<8>(Value);
  }

  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isToken() const override { return Kind == k_Token; }
  bool isMem() const override { return Kind == k_Memri; }
  bool isMemri() const { return Kind == k_Memri; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return Tok;
  }

  MCRegister getReg() const override {
    assert((Kind == k_Register || Kind == k_Memri) && "Invalid access!");

    return RegImm.Reg;
  }

  const MCExpr *getImm() const {
    assert((Kind == k_Immediate || Kind == k_Memri) && "Invalid access!");
    return RegImm.Imm;
  }

  static std::unique_ptr<AVROperand> CreateToken(StringRef Str, SMLoc S) {
    return std::make_unique<AVROperand>(Str, S);
  }

  static std::unique_ptr<AVROperand> CreateReg(unsigned RegNum, SMLoc S,
                                               SMLoc E) {
    return std::make_unique<AVROperand>(RegNum, S, E);
  }

  static std::unique_ptr<AVROperand> CreateImm(const MCExpr *Val, SMLoc S,
                                               SMLoc E) {
    return std::make_unique<AVROperand>(Val, S, E);
  }

  static std::unique_ptr<AVROperand>
  CreateMemri(unsigned RegNum, const MCExpr *Val, SMLoc S, SMLoc E) {
    return std::make_unique<AVROperand>(RegNum, Val, S, E);
  }

  void makeToken(StringRef Token) {
    Kind = k_Token;
    Tok = Token;
  }

  void makeReg(unsigned RegNo) {
    Kind = k_Register;
    RegImm = {RegNo, nullptr};
  }

  void makeImm(MCExpr const *Ex) {
    Kind = k_Immediate;
    RegImm = {0, Ex};
  }

  void makeMemri(unsigned RegNo, MCExpr const *Imm) {
    Kind = k_Memri;
    RegImm = {RegNo, Imm};
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void print(raw_ostream &O) const override {
    switch (Kind) {
    case k_Token:
      O << "Token: \"" << getToken() << "\"";
      break;
    case k_Register:
      O << "Register: " << getReg();
      break;
    case k_Immediate:
      O << "Immediate: \"" << *getImm() << "\"";
      break;
    case k_Memri: {
      // only manually print the size for non-negative values,
      // as the sign is inserted automatically.
      O << "Memri: \"" << getReg() << '+' << *getImm() << "\"";
      break;
    }
    }
    O << "\n";
  }
};

} // end anonymous namespace.

// Auto-generated Match Functions

/// Maps from the set of all register names to a register number.
/// \note Generated by TableGen.
static MCRegister MatchRegisterName(StringRef Name);

/// Maps from the set of all alternative registernames to a register number.
/// \note Generated by TableGen.
static MCRegister MatchRegisterAltName(StringRef Name);

bool AVRAsmParser::invalidOperand(SMLoc const &Loc,
                                  OperandVector const &Operands,
                                  uint64_t const &ErrorInfo) {
  SMLoc ErrorLoc = Loc;
  char const *Diag = nullptr;

  if (ErrorInfo != ~0U) {
    if (ErrorInfo >= Operands.size()) {
      Diag = "too few operands for instruction.";
    } else {
      AVROperand const &Op = (AVROperand const &)*Operands[ErrorInfo];

      // TODO: See if we can do a better error than just "invalid ...".
      if (Op.getStartLoc() != SMLoc()) {
        ErrorLoc = Op.getStartLoc();
      }
    }
  }

  if (!Diag) {
    Diag = "invalid operand for instruction";
  }

  return Error(ErrorLoc, Diag);
}

bool AVRAsmParser::missingFeature(llvm::SMLoc const &Loc,
                                  uint64_t const &ErrorInfo) {
  return Error(Loc, "instruction requires a CPU feature not currently enabled");
}

bool AVRAsmParser::emit(MCInst &Inst, SMLoc const &Loc, MCStreamer &Out) const {
  Inst.setLoc(Loc);
  Out.emitInstruction(Inst, STI);

  return false;
}

bool AVRAsmParser::MatchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                           OperandVector &Operands,
                                           MCStreamer &Out, uint64_t &ErrorInfo,
                                           bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    return emit(Inst, Loc, Out);
  case Match_MissingFeature:
    return missingFeature(Loc, ErrorInfo);
  case Match_InvalidOperand:
    return invalidOperand(Loc, Operands, ErrorInfo);
  case Match_MnemonicFail:
    return Error(Loc, "invalid instruction");
  case Match_InvalidRegisterOnTiny:
    return Error(Loc, "invalid register on avrtiny");
  default:
    return true;
  }
}

/// Parses a register name using a given matching function.
/// Checks for lowercase or uppercase if necessary.
int AVRAsmParser::parseRegisterName(MCRegister (*matchFn)(StringRef)) {
  StringRef Name = Parser.getTok().getString();

  int RegNum = matchFn(Name);

  // GCC supports case insensitive register names. Some of the AVR registers
  // are all lower case, some are all upper case but non are mixed. We prefer
  // to use the original names in the register definitions. That is why we
  // have to test both upper and lower case here.
  if (RegNum == AVR::NoRegister) {
    RegNum = matchFn(Name.lower());
  }
  if (RegNum == AVR::NoRegister) {
    RegNum = matchFn(Name.upper());
  }

  return RegNum;
}

int AVRAsmParser::parseRegisterName() {
  int RegNum = parseRegisterName(&MatchRegisterName);

  if (RegNum == AVR::NoRegister)
    RegNum = parseRegisterName(&MatchRegisterAltName);

  return RegNum;
}

int AVRAsmParser::parseRegister(bool RestoreOnFailure) {
  int RegNum = AVR::NoRegister;

  if (Parser.getTok().is(AsmToken::Identifier)) {
    // Check for register pair syntax
    if (Parser.getLexer().peekTok().is(AsmToken::Colon)) {
      AsmToken HighTok = Parser.getTok();
      Parser.Lex();
      AsmToken ColonTok = Parser.getTok();
      Parser.Lex(); // Eat high (odd) register and colon

      if (Parser.getTok().is(AsmToken::Identifier)) {
        // Convert lower (even) register to DREG
        RegNum = toDREG(parseRegisterName());
      }
      if (RegNum == AVR::NoRegister && RestoreOnFailure) {
        getLexer().UnLex(std::move(ColonTok));
        getLexer().UnLex(std::move(HighTok));
      }
    } else {
      RegNum = parseRegisterName();
    }
  }
  return RegNum;
}

bool AVRAsmParser::tryParseRegisterOperand(OperandVector &Operands) {
  int RegNo = parseRegister();

  if (RegNo == AVR::NoRegister)
    return true;

  // Reject R0~R15 on avrtiny.
  if (AVR::R0 <= RegNo && RegNo <= AVR::R15 &&
      STI.hasFeature(AVR::FeatureTinyEncoding))
    return Error(Parser.getTok().getLoc(), "invalid register on avrtiny");

  AsmToken const &T = Parser.getTok();
  Operands.push_back(AVROperand::CreateReg(RegNo, T.getLoc(), T.getEndLoc()));
  Parser.Lex(); // Eat register token.

  return false;
}

bool AVRAsmParser::tryParseExpression(OperandVector &Operands, int64_t offset) {
  SMLoc S = Parser.getTok().getLoc();

  if (!tryParseRelocExpression(Operands))
    return false;

  if ((Parser.getTok().getKind() == AsmToken::Plus ||
       Parser.getTok().getKind() == AsmToken::Minus) &&
      Parser.getLexer().peekTok().getKind() == AsmToken::Identifier) {
    // Don't handle this case - it should be split into two
    // separate tokens.
    return true;
  }

  // Parse (potentially inner) expression
  MCExpr const *Expression;
  if (getParser().parseExpression(Expression))
    return true;

  if (offset) {
    Expression = MCBinaryExpr::createAdd(
        Expression, MCConstantExpr::create(offset, getContext()), getContext());
  }

  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  Operands.push_back(AVROperand::CreateImm(Expression, S, E));
  return false;
}

bool AVRAsmParser::tryParseRelocExpression(OperandVector &Operands) {
  bool isNegated = false;
  AVRMCExpr::VariantKind ModifierKind = AVRMCExpr::VK_AVR_None;

  SMLoc S = Parser.getTok().getLoc();

  // Reject the form in which sign comes first. This behaviour is
  // in accordance with avr-gcc.
  AsmToken::TokenKind CurTok = Parser.getLexer().getKind();
  if (CurTok == AsmToken::Minus || CurTok == AsmToken::Plus)
    return true;

  // Check for sign.
  AsmToken tokens[2];
  if (Parser.getLexer().peekTokens(tokens) == 2)
    if (tokens[0].getKind() == AsmToken::LParen &&
        tokens[1].getKind() == AsmToken::Minus)
      isNegated = true;

  // Check if we have a target specific modifier (lo8, hi8, &c)
  if (CurTok != AsmToken::Identifier ||
      Parser.getLexer().peekTok().getKind() != AsmToken::LParen) {
    // Not a reloc expr
    return true;
  }
  StringRef ModifierName = Parser.getTok().getString();
  ModifierKind = AVRMCExpr::getKindByName(ModifierName);

  if (ModifierKind != AVRMCExpr::VK_AVR_None) {
    Parser.Lex();
    Parser.Lex(); // Eat modifier name and parenthesis
    if (Parser.getTok().getString() == GENERATE_STUBS &&
        Parser.getTok().getKind() == AsmToken::Identifier) {
      std::string GSModName = ModifierName.str() + "_" + GENERATE_STUBS;
      ModifierKind = AVRMCExpr::getKindByName(GSModName);
      if (ModifierKind != AVRMCExpr::VK_AVR_None)
        Parser.Lex(); // Eat gs modifier name
    }
  } else {
    return Error(Parser.getTok().getLoc(), "unknown modifier");
  }

  if (tokens[1].getKind() == AsmToken::Minus ||
      tokens[1].getKind() == AsmToken::Plus) {
    Parser.Lex();
    assert(Parser.getTok().getKind() == AsmToken::LParen);
    Parser.Lex(); // Eat the sign and parenthesis
  }

  MCExpr const *InnerExpression;
  if (getParser().parseExpression(InnerExpression))
    return true;

  if (tokens[1].getKind() == AsmToken::Minus ||
      tokens[1].getKind() == AsmToken::Plus) {
    assert(Parser.getTok().getKind() == AsmToken::RParen);
    Parser.Lex(); // Eat closing parenthesis
  }

  // If we have a modifier wrap the inner expression
  assert(Parser.getTok().getKind() == AsmToken::RParen);
  Parser.Lex(); // Eat closing parenthesis

  MCExpr const *Expression =
      AVRMCExpr::create(ModifierKind, InnerExpression, isNegated, getContext());

  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  Operands.push_back(AVROperand::CreateImm(Expression, S, E));

  return false;
}

bool AVRAsmParser::parseOperand(OperandVector &Operands, bool maybeReg) {
  LLVM_DEBUG(dbgs() << "parseOperand\n");

  switch (getLexer().getKind()) {
  default:
    return Error(Parser.getTok().getLoc(), "unexpected token in operand");

  case AsmToken::Identifier:
    // Try to parse a register, fall through to the next case if it fails.
    if (maybeReg && !tryParseRegisterOperand(Operands)) {
      return false;
    }
    [[fallthrough]];
  case AsmToken::LParen:
  case AsmToken::Integer:
    return tryParseExpression(Operands, 0);
  case AsmToken::Dot:
    return tryParseExpression(Operands, 2);
  case AsmToken::Plus:
  case AsmToken::Minus: {
    // If the sign preceeds a number, parse the number,
    // otherwise treat the sign a an independent token.
    switch (getLexer().peekTok().getKind()) {
    case AsmToken::Integer:
    case AsmToken::BigNum:
    case AsmToken::Identifier:
    case AsmToken::Real:
      if (!tryParseExpression(Operands, 0))
        return false;
      break;
    default:
      break;
    }
    // Treat the token as an independent token.
    Operands.push_back(AVROperand::CreateToken(Parser.getTok().getString(),
                                               Parser.getTok().getLoc()));
    Parser.Lex(); // Eat the token.
    return false;
  }
  }

  // Could not parse operand
  return true;
}

ParseStatus AVRAsmParser::parseMemriOperand(OperandVector &Operands) {
  LLVM_DEBUG(dbgs() << "parseMemriOperand()\n");

  SMLoc E, S;
  MCExpr const *Expression;
  int RegNo;

  // Parse register.
  {
    RegNo = parseRegister();

    if (RegNo == AVR::NoRegister)
      return ParseStatus::Failure;

    S = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
    Parser.Lex(); // Eat register token.
  }

  // Parse immediate;
  {
    if (getParser().parseExpression(Expression))
      return ParseStatus::Failure;

    E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  }

  Operands.push_back(AVROperand::CreateMemri(RegNo, Expression, S, E));

  return ParseStatus::Success;
}

bool AVRAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                 SMLoc &EndLoc) {
  StartLoc = Parser.getTok().getLoc();
  Reg = parseRegister(/*RestoreOnFailure=*/false);
  EndLoc = Parser.getTok().getLoc();

  return Reg == AVR::NoRegister;
}

ParseStatus AVRAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                           SMLoc &EndLoc) {
  StartLoc = Parser.getTok().getLoc();
  Reg = parseRegister(/*RestoreOnFailure=*/true);
  EndLoc = Parser.getTok().getLoc();

  if (Reg == AVR::NoRegister)
    return ParseStatus::NoMatch;
  return ParseStatus::Success;
}

void AVRAsmParser::eatComma() {
  if (getLexer().is(AsmToken::Comma)) {
    Parser.Lex();
  } else {
    // GCC allows commas to be omitted.
  }
}

bool AVRAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                    StringRef Mnemonic, SMLoc NameLoc,
                                    OperandVector &Operands) {
  Operands.push_back(AVROperand::CreateToken(Mnemonic, NameLoc));

  int OperandNum = -1;
  while (getLexer().isNot(AsmToken::EndOfStatement)) {
    OperandNum++;
    if (OperandNum > 0)
      eatComma();

    ParseStatus ParseRes = MatchOperandParserImpl(Operands, Mnemonic);

    if (ParseRes.isSuccess())
      continue;

    if (ParseRes.isFailure()) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();

      return Error(Loc, "failed to parse register and immediate pair");
    }

    // These specific operands should be treated as addresses/symbols/labels,
    // other than registers.
    bool maybeReg = true;

    if (OperandNum == 1) {
      std::array<StringRef, 8> Insts = {"lds", "adiw", "sbiw", "ldi"};
      for (auto Inst : Insts) {
        if (Inst == Mnemonic) {
          maybeReg = false;
          break;
        }
      }
    } else if (OperandNum == 0) {
      std::array<StringRef, 8> Insts = {"sts", "call", "rcall", "rjmp", "jmp"};
      for (auto Inst : Insts) {
        if (Inst == Mnemonic) {
          maybeReg = false;
          break;
        }
      }
    }

    if (parseOperand(Operands, maybeReg)) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();
      return Error(Loc, "unexpected token in argument list");
    }
  }
  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

ParseStatus AVRAsmParser::parseDirective(llvm::AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal.lower() == ".long")
    return parseLiteralValues(SIZE_LONG, DirectiveID.getLoc());
  if (IDVal.lower() == ".word" || IDVal.lower() == ".short")
    return parseLiteralValues(SIZE_WORD, DirectiveID.getLoc());
  if (IDVal.lower() == ".byte")
    return parseLiteralValues(1, DirectiveID.getLoc());
  return ParseStatus::NoMatch;
}

ParseStatus AVRAsmParser::parseLiteralValues(unsigned SizeInBytes, SMLoc L) {
  MCAsmParser &Parser = getParser();
  AVRMCELFStreamer &AVRStreamer =
      static_cast<AVRMCELFStreamer &>(Parser.getStreamer());
  AsmToken Tokens[2];
  size_t ReadCount = Parser.getLexer().peekTokens(Tokens);
  if (ReadCount == 2 && Parser.getTok().getKind() == AsmToken::Identifier &&
      Tokens[0].getKind() == AsmToken::Minus &&
      Tokens[1].getKind() == AsmToken::Identifier) {
    MCSymbol *Symbol = getContext().getOrCreateSymbol(".text");
    AVRStreamer.emitValueForModiferKind(Symbol, SizeInBytes, L,
                                        AVRMCExpr::VK_AVR_None);
    return ParseStatus::NoMatch;
  }

  if (Parser.getTok().getKind() == AsmToken::Identifier &&
      Parser.getLexer().peekTok().getKind() == AsmToken::LParen) {
    StringRef ModifierName = Parser.getTok().getString();
    AVRMCExpr::VariantKind ModifierKind =
        AVRMCExpr::getKindByName(ModifierName);
    if (ModifierKind != AVRMCExpr::VK_AVR_None) {
      Parser.Lex();
      Parser.Lex(); // Eat the modifier and parenthesis
    } else {
      return Error(Parser.getTok().getLoc(), "unknown modifier");
    }
    MCSymbol *Symbol =
        getContext().getOrCreateSymbol(Parser.getTok().getString());
    AVRStreamer.emitValueForModiferKind(Symbol, SizeInBytes, L, ModifierKind);
    Lex(); // Eat the symbol name.
    if (parseToken(AsmToken::RParen))
      return ParseStatus::Failure;
    return parseEOL();
  }

  auto parseOne = [&]() -> bool {
    const MCExpr *Value;
    if (Parser.parseExpression(Value))
      return true;
    Parser.getStreamer().emitValue(Value, SizeInBytes, L);
    return false;
  };
  return (parseMany(parseOne));
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAVRAsmParser() {
  RegisterMCAsmParser<AVRAsmParser> X(getTheAVRTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "AVRGenAsmMatcher.inc"

// Uses enums defined in AVRGenAsmMatcher.inc
unsigned AVRAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                  unsigned ExpectedKind) {
  AVROperand &Op = static_cast<AVROperand &>(AsmOp);
  MatchClassKind Expected = static_cast<MatchClassKind>(ExpectedKind);

  // If need be, GCC converts bare numbers to register names
  // It's ugly, but GCC supports it.
  if (Op.isImm()) {
    if (MCConstantExpr const *Const = dyn_cast<MCConstantExpr>(Op.getImm())) {
      int64_t RegNum = Const->getValue();

      // Reject R0~R15 on avrtiny.
      if (0 <= RegNum && RegNum <= 15 &&
          STI.hasFeature(AVR::FeatureTinyEncoding))
        return Match_InvalidRegisterOnTiny;

      std::ostringstream RegName;
      RegName << "r" << RegNum;
      RegNum = MatchRegisterName(RegName.str());
      if (RegNum != AVR::NoRegister) {
        Op.makeReg(RegNum);
        if (validateOperandClass(Op, Expected) == Match_Success) {
          return Match_Success;
        }
      }
      // Let the other quirks try their magic.
    }
  }

  if (Op.isReg()) {
    // If the instruction uses a register pair but we got a single, lower
    // register we perform a "class cast".
    if (isSubclass(Expected, MCK_DREGS)) {
      unsigned correspondingDREG = toDREG(Op.getReg());

      if (correspondingDREG != AVR::NoRegister) {
        Op.makeReg(correspondingDREG);
        return validateOperandClass(Op, Expected);
      }
    }
  }
  return Match_InvalidOperand;
}
