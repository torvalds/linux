//===--- RuntimeDyldChecker.cpp - RuntimeDyld tester framework --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/RuntimeDyldChecker.h"
#include "RuntimeDyldCheckerImpl.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <cctype>
#include <memory>
#include <utility>

#define DEBUG_TYPE "rtdyld"

using namespace llvm;

namespace {
struct TargetInfo {
  const Target *TheTarget;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCDisassembler> Disassembler;
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCInstPrinter> InstPrinter;
};
} // anonymous namespace

namespace llvm {

// Helper class that implements the language evaluated by RuntimeDyldChecker.
class RuntimeDyldCheckerExprEval {
public:
  RuntimeDyldCheckerExprEval(const RuntimeDyldCheckerImpl &Checker,
                             raw_ostream &ErrStream)
      : Checker(Checker) {}

  bool evaluate(StringRef Expr) const {
    // Expect equality expression of the form 'LHS = RHS'.
    Expr = Expr.trim();
    size_t EQIdx = Expr.find('=');

    ParseContext OutsideLoad(false);

    // Evaluate LHS.
    StringRef LHSExpr = Expr.substr(0, EQIdx).rtrim();
    StringRef RemainingExpr;
    EvalResult LHSResult;
    std::tie(LHSResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(LHSExpr, OutsideLoad), OutsideLoad);
    if (LHSResult.hasError())
      return handleError(Expr, LHSResult);
    if (RemainingExpr != "")
      return handleError(Expr, unexpectedToken(RemainingExpr, LHSExpr, ""));

    // Evaluate RHS.
    StringRef RHSExpr = Expr.substr(EQIdx + 1).ltrim();
    EvalResult RHSResult;
    std::tie(RHSResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(RHSExpr, OutsideLoad), OutsideLoad);
    if (RHSResult.hasError())
      return handleError(Expr, RHSResult);
    if (RemainingExpr != "")
      return handleError(Expr, unexpectedToken(RemainingExpr, RHSExpr, ""));

    if (LHSResult.getValue() != RHSResult.getValue()) {
      Checker.ErrStream << "Expression '" << Expr << "' is false: "
                        << format("0x%" PRIx64, LHSResult.getValue())
                        << " != " << format("0x%" PRIx64, RHSResult.getValue())
                        << "\n";
      return false;
    }
    return true;
  }

private:
  // RuntimeDyldCheckerExprEval requires some context when parsing exprs. In
  // particular, it needs to know whether a symbol is being evaluated in the
  // context of a load, in which case we want the linker's local address for
  // the symbol, or outside of a load, in which case we want the symbol's
  // address in the remote target.

  struct ParseContext {
    bool IsInsideLoad;
    ParseContext(bool IsInsideLoad) : IsInsideLoad(IsInsideLoad) {}
  };

  const RuntimeDyldCheckerImpl &Checker;

  enum class BinOpToken : unsigned {
    Invalid,
    Add,
    Sub,
    BitwiseAnd,
    BitwiseOr,
    ShiftLeft,
    ShiftRight
  };

  class EvalResult {
  public:
    EvalResult() : Value(0) {}
    EvalResult(uint64_t Value) : Value(Value) {}
    EvalResult(std::string ErrorMsg)
        : Value(0), ErrorMsg(std::move(ErrorMsg)) {}
    uint64_t getValue() const { return Value; }
    bool hasError() const { return ErrorMsg != ""; }
    const std::string &getErrorMsg() const { return ErrorMsg; }

  private:
    uint64_t Value;
    std::string ErrorMsg;
  };

  StringRef getTokenForError(StringRef Expr) const {
    if (Expr.empty())
      return "";

    StringRef Token, Remaining;
    if (isalpha(Expr[0]))
      std::tie(Token, Remaining) = parseSymbol(Expr);
    else if (isdigit(Expr[0]))
      std::tie(Token, Remaining) = parseNumberString(Expr);
    else {
      unsigned TokLen = 1;
      if (Expr.starts_with("<<") || Expr.starts_with(">>"))
        TokLen = 2;
      Token = Expr.substr(0, TokLen);
    }
    return Token;
  }

  EvalResult unexpectedToken(StringRef TokenStart, StringRef SubExpr,
                             StringRef ErrText) const {
    std::string ErrorMsg("Encountered unexpected token '");
    ErrorMsg += getTokenForError(TokenStart);
    if (SubExpr != "") {
      ErrorMsg += "' while parsing subexpression '";
      ErrorMsg += SubExpr;
    }
    ErrorMsg += "'";
    if (ErrText != "") {
      ErrorMsg += " ";
      ErrorMsg += ErrText;
    }
    return EvalResult(std::move(ErrorMsg));
  }

  bool handleError(StringRef Expr, const EvalResult &R) const {
    assert(R.hasError() && "Not an error result.");
    Checker.ErrStream << "Error evaluating expression '" << Expr
                      << "': " << R.getErrorMsg() << "\n";
    return false;
  }

  std::pair<BinOpToken, StringRef> parseBinOpToken(StringRef Expr) const {
    if (Expr.empty())
      return std::make_pair(BinOpToken::Invalid, "");

    // Handle the two 2-character tokens.
    if (Expr.starts_with("<<"))
      return std::make_pair(BinOpToken::ShiftLeft, Expr.substr(2).ltrim());
    if (Expr.starts_with(">>"))
      return std::make_pair(BinOpToken::ShiftRight, Expr.substr(2).ltrim());

    // Handle one-character tokens.
    BinOpToken Op;
    switch (Expr[0]) {
    default:
      return std::make_pair(BinOpToken::Invalid, Expr);
    case '+':
      Op = BinOpToken::Add;
      break;
    case '-':
      Op = BinOpToken::Sub;
      break;
    case '&':
      Op = BinOpToken::BitwiseAnd;
      break;
    case '|':
      Op = BinOpToken::BitwiseOr;
      break;
    }

    return std::make_pair(Op, Expr.substr(1).ltrim());
  }

  EvalResult computeBinOpResult(BinOpToken Op, const EvalResult &LHSResult,
                                const EvalResult &RHSResult) const {
    switch (Op) {
    default:
      llvm_unreachable("Tried to evaluate unrecognized operation.");
    case BinOpToken::Add:
      return EvalResult(LHSResult.getValue() + RHSResult.getValue());
    case BinOpToken::Sub:
      return EvalResult(LHSResult.getValue() - RHSResult.getValue());
    case BinOpToken::BitwiseAnd:
      return EvalResult(LHSResult.getValue() & RHSResult.getValue());
    case BinOpToken::BitwiseOr:
      return EvalResult(LHSResult.getValue() | RHSResult.getValue());
    case BinOpToken::ShiftLeft:
      return EvalResult(LHSResult.getValue() << RHSResult.getValue());
    case BinOpToken::ShiftRight:
      return EvalResult(LHSResult.getValue() >> RHSResult.getValue());
    }
  }

  // Parse a symbol and return a (string, string) pair representing the symbol
  // name and expression remaining to be parsed.
  std::pair<StringRef, StringRef> parseSymbol(StringRef Expr) const {
    size_t FirstNonSymbol = Expr.find_first_not_of("0123456789"
                                                   "abcdefghijklmnopqrstuvwxyz"
                                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                   ":_.$");
    return std::make_pair(Expr.substr(0, FirstNonSymbol),
                          Expr.substr(FirstNonSymbol).ltrim());
  }

  // Evaluate a call to decode_operand. Decode the instruction operand at the
  // given symbol and get the value of the requested operand.
  // Returns an error if the instruction cannot be decoded, or the requested
  // operand is not an immediate.
  // On success, returns a pair containing the value of the operand, plus
  // the expression remaining to be evaluated.
  std::pair<EvalResult, StringRef> evalDecodeOperand(StringRef Expr) const {
    if (!Expr.starts_with("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();
    StringRef Symbol;
    std::tie(Symbol, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!Checker.isSymbolValid(Symbol))
      return std::make_pair(
          EvalResult(("Cannot decode unknown symbol '" + Symbol + "'").str()),
          "");

    // if there is an offset number expr
    int64_t Offset = 0;
    BinOpToken BinOp;
    std::tie(BinOp, RemainingExpr) = parseBinOpToken(RemainingExpr);
    switch (BinOp) {
    case BinOpToken::Add: {
      EvalResult Number;
      std::tie(Number, RemainingExpr) = evalNumberExpr(RemainingExpr);
      Offset = Number.getValue();
      break;
    }
    case BinOpToken::Invalid:
      break;
    default:
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr,
                          "expected '+' for offset or ',' if no offset"),
          "");
    }

    if (!RemainingExpr.starts_with(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    EvalResult OpIdxExpr;
    std::tie(OpIdxExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);
    if (OpIdxExpr.hasError())
      return std::make_pair(OpIdxExpr, "");

    if (!RemainingExpr.starts_with(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    MCInst Inst;
    uint64_t Size;
    if (!decodeInst(Symbol, Inst, Size, Offset))
      return std::make_pair(
          EvalResult(("Couldn't decode instruction at '" + Symbol + "'").str()),
          "");

    unsigned OpIdx = OpIdxExpr.getValue();

    auto printInst = [this](StringRef Symbol, MCInst Inst,
                            raw_string_ostream &ErrMsgStream) {
      auto TT = Checker.getTripleForSymbol(Checker.getTargetFlag(Symbol));
      auto TI = getTargetInfo(TT, Checker.getCPU(), Checker.getFeatures());
      if (auto E = TI.takeError()) {
        errs() << "Error obtaining instruction printer: "
               << toString(std::move(E)) << "\n";
        return std::make_pair(EvalResult(ErrMsgStream.str()), "");
      }
      Inst.dump_pretty(ErrMsgStream, TI->InstPrinter.get());
      return std::make_pair(EvalResult(ErrMsgStream.str()), "");
    };

    if (OpIdx >= Inst.getNumOperands()) {
      std::string ErrMsg;
      raw_string_ostream ErrMsgStream(ErrMsg);
      ErrMsgStream << "Invalid operand index '" << format("%i", OpIdx)
                   << "' for instruction '" << Symbol
                   << "'. Instruction has only "
                   << format("%i", Inst.getNumOperands())
                   << " operands.\nInstruction is:\n  ";

      return printInst(Symbol, Inst, ErrMsgStream);
    }

    const MCOperand &Op = Inst.getOperand(OpIdx);
    if (!Op.isImm()) {
      std::string ErrMsg;
      raw_string_ostream ErrMsgStream(ErrMsg);
      ErrMsgStream << "Operand '" << format("%i", OpIdx) << "' of instruction '"
                   << Symbol << "' is not an immediate.\nInstruction is:\n  ";

      return printInst(Symbol, Inst, ErrMsgStream);
    }

    return std::make_pair(EvalResult(Op.getImm()), RemainingExpr);
  }

  // Evaluate a call to next_pc.
  // Decode the instruction at the given symbol and return the following program
  // counter.
  // Returns an error if the instruction cannot be decoded.
  // On success, returns a pair containing the next PC, plus of the
  // expression remaining to be evaluated.
  std::pair<EvalResult, StringRef> evalNextPC(StringRef Expr,
                                              ParseContext PCtx) const {
    if (!Expr.starts_with("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();
    StringRef Symbol;
    std::tie(Symbol, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!Checker.isSymbolValid(Symbol))
      return std::make_pair(
          EvalResult(("Cannot decode unknown symbol '" + Symbol + "'").str()),
          "");

    if (!RemainingExpr.starts_with(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    MCInst Inst;
    uint64_t InstSize;
    if (!decodeInst(Symbol, Inst, InstSize, 0))
      return std::make_pair(
          EvalResult(("Couldn't decode instruction at '" + Symbol + "'").str()),
          "");

    uint64_t SymbolAddr = PCtx.IsInsideLoad
                              ? Checker.getSymbolLocalAddr(Symbol)
                              : Checker.getSymbolRemoteAddr(Symbol);

    // ARM PC offset is 8 instead of 4, because it accounts for an additional
    // prefetch instruction that increments PC even though it is implicit.
    auto TT = Checker.getTripleForSymbol(Checker.getTargetFlag(Symbol));
    uint64_t PCOffset = TT.getArch() == Triple::ArchType::arm ? 4 : 0;

    uint64_t NextPC = SymbolAddr + InstSize + PCOffset;

    return std::make_pair(EvalResult(NextPC), RemainingExpr);
  }

  // Evaluate a call to stub_addr/got_addr.
  // Look up and return the address of the stub for the given
  // (<file name>, <section name>, <symbol name>) tuple.
  // On success, returns a pair containing the stub address, plus the expression
  // remaining to be evaluated.
  std::pair<EvalResult, StringRef>
  evalStubOrGOTAddr(StringRef Expr, ParseContext PCtx, bool IsStubAddr) const {
    if (!Expr.starts_with("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();

    // Handle file-name specially, as it may contain characters that aren't
    // legal for symbols.
    StringRef StubContainerName;
    size_t ComaIdx = RemainingExpr.find(',');
    StubContainerName = RemainingExpr.substr(0, ComaIdx).rtrim();
    RemainingExpr = RemainingExpr.substr(ComaIdx).ltrim();

    if (!RemainingExpr.starts_with(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    StringRef Symbol;
    std::tie(Symbol, RemainingExpr) = parseSymbol(RemainingExpr);

    // Parse optional parameter to filter by stub kind
    StringRef KindNameFilter;
    if (RemainingExpr.starts_with(",")) {
      RemainingExpr = RemainingExpr.substr(1).ltrim();
      size_t ClosingBracket = RemainingExpr.find(")");
      KindNameFilter = RemainingExpr.substr(0, ClosingBracket);
      RemainingExpr = RemainingExpr.substr(ClosingBracket);
    }

    if (!RemainingExpr.starts_with(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    uint64_t StubAddr;
    std::string ErrorMsg;
    std::tie(StubAddr, ErrorMsg) =
        Checker.getStubOrGOTAddrFor(StubContainerName, Symbol, KindNameFilter,
                                    PCtx.IsInsideLoad, IsStubAddr);

    if (ErrorMsg != "")
      return std::make_pair(EvalResult(ErrorMsg), "");

    return std::make_pair(EvalResult(StubAddr), RemainingExpr);
  }

  std::pair<EvalResult, StringRef> evalSectionAddr(StringRef Expr,
                                                   ParseContext PCtx) const {
    if (!Expr.starts_with("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();

    // Handle file-name specially, as it may contain characters that aren't
    // legal for symbols.
    StringRef FileName;
    size_t ComaIdx = RemainingExpr.find(',');
    FileName = RemainingExpr.substr(0, ComaIdx).rtrim();
    RemainingExpr = RemainingExpr.substr(ComaIdx).ltrim();

    if (!RemainingExpr.starts_with(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    StringRef SectionName;
    size_t CloseParensIdx = RemainingExpr.find(')');
    SectionName = RemainingExpr.substr(0, CloseParensIdx).rtrim();
    RemainingExpr = RemainingExpr.substr(CloseParensIdx).ltrim();

    if (!RemainingExpr.starts_with(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    uint64_t StubAddr;
    std::string ErrorMsg;
    std::tie(StubAddr, ErrorMsg) = Checker.getSectionAddr(
        FileName, SectionName, PCtx.IsInsideLoad);

    if (ErrorMsg != "")
      return std::make_pair(EvalResult(ErrorMsg), "");

    return std::make_pair(EvalResult(StubAddr), RemainingExpr);
  }

  // Evaluate an identifier expr, which may be a symbol, or a call to
  // one of the builtin functions: get_insn_opcode or get_insn_length.
  // Return the result, plus the expression remaining to be parsed.
  std::pair<EvalResult, StringRef> evalIdentifierExpr(StringRef Expr,
                                                      ParseContext PCtx) const {
    StringRef Symbol;
    StringRef RemainingExpr;
    std::tie(Symbol, RemainingExpr) = parseSymbol(Expr);

    // Check for builtin function calls.
    if (Symbol == "decode_operand")
      return evalDecodeOperand(RemainingExpr);
    else if (Symbol == "next_pc")
      return evalNextPC(RemainingExpr, PCtx);
    else if (Symbol == "stub_addr")
      return evalStubOrGOTAddr(RemainingExpr, PCtx, true);
    else if (Symbol == "got_addr")
      return evalStubOrGOTAddr(RemainingExpr, PCtx, false);
    else if (Symbol == "section_addr")
      return evalSectionAddr(RemainingExpr, PCtx);

    if (!Checker.isSymbolValid(Symbol)) {
      std::string ErrMsg("No known address for symbol '");
      ErrMsg += Symbol;
      ErrMsg += "'";
      if (Symbol.starts_with("L"))
        ErrMsg += " (this appears to be an assembler local label - "
                  " perhaps drop the 'L'?)";

      return std::make_pair(EvalResult(ErrMsg), "");
    }

    // The value for the symbol depends on the context we're evaluating in:
    // Inside a load this is the address in the linker's memory, outside a
    // load it's the address in the target processes memory.
    uint64_t Value = PCtx.IsInsideLoad ? Checker.getSymbolLocalAddr(Symbol)
                                       : Checker.getSymbolRemoteAddr(Symbol);

    // Looks like a plain symbol reference.
    return std::make_pair(EvalResult(Value), RemainingExpr);
  }

  // Parse a number (hexadecimal or decimal) and return a (string, string)
  // pair representing the number and the expression remaining to be parsed.
  std::pair<StringRef, StringRef> parseNumberString(StringRef Expr) const {
    size_t FirstNonDigit = StringRef::npos;
    if (Expr.starts_with("0x")) {
      FirstNonDigit = Expr.find_first_not_of("0123456789abcdefABCDEF", 2);
      if (FirstNonDigit == StringRef::npos)
        FirstNonDigit = Expr.size();
    } else {
      FirstNonDigit = Expr.find_first_not_of("0123456789");
      if (FirstNonDigit == StringRef::npos)
        FirstNonDigit = Expr.size();
    }
    return std::make_pair(Expr.substr(0, FirstNonDigit),
                          Expr.substr(FirstNonDigit));
  }

  // Evaluate a constant numeric expression (hexadecimal or decimal) and
  // return a pair containing the result, and the expression remaining to be
  // evaluated.
  std::pair<EvalResult, StringRef> evalNumberExpr(StringRef Expr) const {
    StringRef ValueStr;
    StringRef RemainingExpr;
    std::tie(ValueStr, RemainingExpr) = parseNumberString(Expr);

    if (ValueStr.empty() || !isdigit(ValueStr[0]))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected number"), "");
    uint64_t Value;
    ValueStr.getAsInteger(0, Value);
    return std::make_pair(EvalResult(Value), RemainingExpr);
  }

  // Evaluate an expression of the form "(<expr>)" and return a pair
  // containing the result of evaluating <expr>, plus the expression
  // remaining to be parsed.
  std::pair<EvalResult, StringRef> evalParensExpr(StringRef Expr,
                                                  ParseContext PCtx) const {
    assert(Expr.starts_with("(") && "Not a parenthesized expression");
    EvalResult SubExprResult;
    StringRef RemainingExpr;
    std::tie(SubExprResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(Expr.substr(1).ltrim(), PCtx), PCtx);
    if (SubExprResult.hasError())
      return std::make_pair(SubExprResult, "");
    if (!RemainingExpr.starts_with(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();
    return std::make_pair(SubExprResult, RemainingExpr);
  }

  // Evaluate an expression in one of the following forms:
  //   *{<number>}<expr>
  // Return a pair containing the result, plus the expression remaining to be
  // parsed.
  std::pair<EvalResult, StringRef> evalLoadExpr(StringRef Expr) const {
    assert(Expr.starts_with("*") && "Not a load expression");
    StringRef RemainingExpr = Expr.substr(1).ltrim();

    // Parse read size.
    if (!RemainingExpr.starts_with("{"))
      return std::make_pair(EvalResult("Expected '{' following '*'."), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();
    EvalResult ReadSizeExpr;
    std::tie(ReadSizeExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);
    if (ReadSizeExpr.hasError())
      return std::make_pair(ReadSizeExpr, RemainingExpr);
    uint64_t ReadSize = ReadSizeExpr.getValue();
    if (ReadSize < 1 || ReadSize > 8)
      return std::make_pair(EvalResult("Invalid size for dereference."), "");
    if (!RemainingExpr.starts_with("}"))
      return std::make_pair(EvalResult("Missing '}' for dereference."), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    // Evaluate the expression representing the load address.
    ParseContext LoadCtx(true);
    EvalResult LoadAddrExprResult;
    std::tie(LoadAddrExprResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(RemainingExpr, LoadCtx), LoadCtx);

    if (LoadAddrExprResult.hasError())
      return std::make_pair(LoadAddrExprResult, "");

    uint64_t LoadAddr = LoadAddrExprResult.getValue();

    // If there is no error but the content pointer is null then this is a
    // zero-fill symbol/section.
    if (LoadAddr == 0)
      return std::make_pair(0, RemainingExpr);

    return std::make_pair(
        EvalResult(Checker.readMemoryAtAddr(LoadAddr, ReadSize)),
        RemainingExpr);
  }

  // Evaluate a "simple" expression. This is any expression that _isn't_ an
  // un-parenthesized binary expression.
  //
  // "Simple" expressions can be optionally bit-sliced. See evalSlicedExpr.
  //
  // Returns a pair containing the result of the evaluation, plus the
  // expression remaining to be parsed.
  std::pair<EvalResult, StringRef> evalSimpleExpr(StringRef Expr,
                                                  ParseContext PCtx) const {
    EvalResult SubExprResult;
    StringRef RemainingExpr;

    if (Expr.empty())
      return std::make_pair(EvalResult("Unexpected end of expression"), "");

    if (Expr[0] == '(')
      std::tie(SubExprResult, RemainingExpr) = evalParensExpr(Expr, PCtx);
    else if (Expr[0] == '*')
      std::tie(SubExprResult, RemainingExpr) = evalLoadExpr(Expr);
    else if (isalpha(Expr[0]) || Expr[0] == '_')
      std::tie(SubExprResult, RemainingExpr) = evalIdentifierExpr(Expr, PCtx);
    else if (isdigit(Expr[0]))
      std::tie(SubExprResult, RemainingExpr) = evalNumberExpr(Expr);
    else
      return std::make_pair(
          unexpectedToken(Expr, Expr,
                          "expected '(', '*', identifier, or number"), "");

    if (SubExprResult.hasError())
      return std::make_pair(SubExprResult, RemainingExpr);

    // Evaluate bit-slice if present.
    if (RemainingExpr.starts_with("["))
      std::tie(SubExprResult, RemainingExpr) =
          evalSliceExpr(std::make_pair(SubExprResult, RemainingExpr));

    return std::make_pair(SubExprResult, RemainingExpr);
  }

  // Evaluate a bit-slice of an expression.
  // A bit-slice has the form "<expr>[high:low]". The result of evaluating a
  // slice is the bits between high and low (inclusive) in the original
  // expression, right shifted so that the "low" bit is in position 0 in the
  // result.
  // Returns a pair containing the result of the slice operation, plus the
  // expression remaining to be parsed.
  std::pair<EvalResult, StringRef>
  evalSliceExpr(const std::pair<EvalResult, StringRef> &Ctx) const {
    EvalResult SubExprResult;
    StringRef RemainingExpr;
    std::tie(SubExprResult, RemainingExpr) = Ctx;

    assert(RemainingExpr.starts_with("[") && "Not a slice expr.");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    EvalResult HighBitExpr;
    std::tie(HighBitExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);

    if (HighBitExpr.hasError())
      return std::make_pair(HighBitExpr, RemainingExpr);

    if (!RemainingExpr.starts_with(":"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ':'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    EvalResult LowBitExpr;
    std::tie(LowBitExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);

    if (LowBitExpr.hasError())
      return std::make_pair(LowBitExpr, RemainingExpr);

    if (!RemainingExpr.starts_with("]"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ']'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    unsigned HighBit = HighBitExpr.getValue();
    unsigned LowBit = LowBitExpr.getValue();
    uint64_t Mask = ((uint64_t)1 << (HighBit - LowBit + 1)) - 1;
    uint64_t SlicedValue = (SubExprResult.getValue() >> LowBit) & Mask;
    return std::make_pair(EvalResult(SlicedValue), RemainingExpr);
  }

  // Evaluate a "complex" expression.
  // Takes an already evaluated subexpression and checks for the presence of a
  // binary operator, computing the result of the binary operation if one is
  // found. Used to make arithmetic expressions left-associative.
  // Returns a pair containing the ultimate result of evaluating the
  // expression, plus the expression remaining to be evaluated.
  std::pair<EvalResult, StringRef>
  evalComplexExpr(const std::pair<EvalResult, StringRef> &LHSAndRemaining,
                  ParseContext PCtx) const {
    EvalResult LHSResult;
    StringRef RemainingExpr;
    std::tie(LHSResult, RemainingExpr) = LHSAndRemaining;

    // If there was an error, or there's nothing left to evaluate, return the
    // result.
    if (LHSResult.hasError() || RemainingExpr == "")
      return std::make_pair(LHSResult, RemainingExpr);

    // Otherwise check if this is a binary expression.
    BinOpToken BinOp;
    std::tie(BinOp, RemainingExpr) = parseBinOpToken(RemainingExpr);

    // If this isn't a recognized expression just return.
    if (BinOp == BinOpToken::Invalid)
      return std::make_pair(LHSResult, RemainingExpr);

    // This is a recognized bin-op. Evaluate the RHS, then evaluate the binop.
    EvalResult RHSResult;
    std::tie(RHSResult, RemainingExpr) = evalSimpleExpr(RemainingExpr, PCtx);

    // If there was an error evaluating the RHS, return it.
    if (RHSResult.hasError())
      return std::make_pair(RHSResult, RemainingExpr);

    // This is a binary expression - evaluate and try to continue as a
    // complex expr.
    EvalResult ThisResult(computeBinOpResult(BinOp, LHSResult, RHSResult));

    return evalComplexExpr(std::make_pair(ThisResult, RemainingExpr), PCtx);
  }

  bool decodeInst(StringRef Symbol, MCInst &Inst, uint64_t &Size,
                  int64_t Offset) const {
    auto TT = Checker.getTripleForSymbol(Checker.getTargetFlag(Symbol));
    auto TI = getTargetInfo(TT, Checker.getCPU(), Checker.getFeatures());

    if (auto E = TI.takeError()) {
      errs() << "Error obtaining disassembler: " << toString(std::move(E))
             << "\n";
      return false;
    }

    StringRef SymbolMem = Checker.getSymbolContent(Symbol);
    ArrayRef<uint8_t> SymbolBytes(SymbolMem.bytes_begin() + Offset,
                                  SymbolMem.size() - Offset);

    MCDisassembler::DecodeStatus S =
        TI->Disassembler->getInstruction(Inst, Size, SymbolBytes, 0, nulls());

    return (S == MCDisassembler::Success);
  }

  Expected<TargetInfo> getTargetInfo(const Triple &TT, const StringRef &CPU,
                                     const SubtargetFeatures &TF) const {

    auto TripleName = TT.str();
    std::string ErrorStr;
    const Target *TheTarget =
        TargetRegistry::lookupTarget(TripleName, ErrorStr);
    if (!TheTarget)
      return make_error<StringError>("Error accessing target '" + TripleName +
                                         "': " + ErrorStr,
                                     inconvertibleErrorCode());

    std::unique_ptr<MCSubtargetInfo> STI(
        TheTarget->createMCSubtargetInfo(TripleName, CPU, TF.getString()));
    if (!STI)
      return make_error<StringError>("Unable to create subtarget for " +
                                         TripleName,
                                     inconvertibleErrorCode());

    std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
    if (!MRI)
      return make_error<StringError>("Unable to create target register info "
                                     "for " +
                                         TripleName,
                                     inconvertibleErrorCode());

    MCTargetOptions MCOptions;
    std::unique_ptr<MCAsmInfo> MAI(
        TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
    if (!MAI)
      return make_error<StringError>("Unable to create target asm info " +
                                         TripleName,
                                     inconvertibleErrorCode());

    auto Ctx = std::make_unique<MCContext>(Triple(TripleName), MAI.get(),
                                           MRI.get(), STI.get());

    std::unique_ptr<MCDisassembler> Disassembler(
        TheTarget->createMCDisassembler(*STI, *Ctx));
    if (!Disassembler)
      return make_error<StringError>("Unable to create disassembler for " +
                                         TripleName,
                                     inconvertibleErrorCode());

    std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());
    if (!MII)
      return make_error<StringError>("Unable to create instruction info for" +
                                         TripleName,
                                     inconvertibleErrorCode());

    std::unique_ptr<MCInstPrinter> InstPrinter(TheTarget->createMCInstPrinter(
        Triple(TripleName), 0, *MAI, *MII, *MRI));
    if (!InstPrinter)
      return make_error<StringError>(
          "Unable to create instruction printer for" + TripleName,
          inconvertibleErrorCode());

    return TargetInfo({TheTarget, std::move(STI), std::move(MRI),
                       std::move(MAI), std::move(Ctx), std::move(Disassembler),
                       std::move(MII), std::move(InstPrinter)});
  }
};
} // namespace llvm

RuntimeDyldCheckerImpl::RuntimeDyldCheckerImpl(
    IsSymbolValidFunction IsSymbolValid, GetSymbolInfoFunction GetSymbolInfo,
    GetSectionInfoFunction GetSectionInfo, GetStubInfoFunction GetStubInfo,
    GetGOTInfoFunction GetGOTInfo, llvm::endianness Endianness, Triple TT,
    StringRef CPU, SubtargetFeatures TF, raw_ostream &ErrStream)
    : IsSymbolValid(std::move(IsSymbolValid)),
      GetSymbolInfo(std::move(GetSymbolInfo)),
      GetSectionInfo(std::move(GetSectionInfo)),
      GetStubInfo(std::move(GetStubInfo)), GetGOTInfo(std::move(GetGOTInfo)),
      Endianness(Endianness), TT(std::move(TT)), CPU(std::move(CPU)),
      TF(std::move(TF)), ErrStream(ErrStream) {}

bool RuntimeDyldCheckerImpl::check(StringRef CheckExpr) const {
  CheckExpr = CheckExpr.trim();
  LLVM_DEBUG(dbgs() << "RuntimeDyldChecker: Checking '" << CheckExpr
                    << "'...\n");
  RuntimeDyldCheckerExprEval P(*this, ErrStream);
  bool Result = P.evaluate(CheckExpr);
  (void)Result;
  LLVM_DEBUG(dbgs() << "RuntimeDyldChecker: '" << CheckExpr << "' "
                    << (Result ? "passed" : "FAILED") << ".\n");
  return Result;
}

bool RuntimeDyldCheckerImpl::checkAllRulesInBuffer(StringRef RulePrefix,
                                                   MemoryBuffer *MemBuf) const {
  bool DidAllTestsPass = true;
  unsigned NumRules = 0;

  std::string CheckExpr;
  const char *LineStart = MemBuf->getBufferStart();

  // Eat whitespace.
  while (LineStart != MemBuf->getBufferEnd() && isSpace(*LineStart))
    ++LineStart;

  while (LineStart != MemBuf->getBufferEnd() && *LineStart != '\0') {
    const char *LineEnd = LineStart;
    while (LineEnd != MemBuf->getBufferEnd() && *LineEnd != '\r' &&
           *LineEnd != '\n')
      ++LineEnd;

    StringRef Line(LineStart, LineEnd - LineStart);
    if (Line.starts_with(RulePrefix))
      CheckExpr += Line.substr(RulePrefix.size()).str();

    // If there's a check expr string...
    if (!CheckExpr.empty()) {
      // ... and it's complete then run it, otherwise remove the trailer '\'.
      if (CheckExpr.back() != '\\') {
        DidAllTestsPass &= check(CheckExpr);
        CheckExpr.clear();
        ++NumRules;
      } else
        CheckExpr.pop_back();
    }

    // Eat whitespace.
    LineStart = LineEnd;
    while (LineStart != MemBuf->getBufferEnd() && isSpace(*LineStart))
      ++LineStart;
  }
  return DidAllTestsPass && (NumRules != 0);
}

bool RuntimeDyldCheckerImpl::isSymbolValid(StringRef Symbol) const {
  return IsSymbolValid(Symbol);
}

uint64_t RuntimeDyldCheckerImpl::getSymbolLocalAddr(StringRef Symbol) const {
  auto SymInfo = GetSymbolInfo(Symbol);
  if (!SymInfo) {
    logAllUnhandledErrors(SymInfo.takeError(), errs(), "RTDyldChecker: ");
    return 0;
  }

  if (SymInfo->isZeroFill())
    return 0;

  return static_cast<uint64_t>(
      reinterpret_cast<uintptr_t>(SymInfo->getContent().data()));
}

uint64_t RuntimeDyldCheckerImpl::getSymbolRemoteAddr(StringRef Symbol) const {
  auto SymInfo = GetSymbolInfo(Symbol);
  if (!SymInfo) {
    logAllUnhandledErrors(SymInfo.takeError(), errs(), "RTDyldChecker: ");
    return 0;
  }

  return SymInfo->getTargetAddress();
}

uint64_t RuntimeDyldCheckerImpl::readMemoryAtAddr(uint64_t SrcAddr,
                                                  unsigned Size) const {
  uintptr_t PtrSizedAddr = static_cast<uintptr_t>(SrcAddr);
  assert(PtrSizedAddr == SrcAddr && "Linker memory pointer out-of-range.");
  void *Ptr = reinterpret_cast<void*>(PtrSizedAddr);

  switch (Size) {
  case 1:
    return support::endian::read<uint8_t>(Ptr, Endianness);
  case 2:
    return support::endian::read<uint16_t>(Ptr, Endianness);
  case 4:
    return support::endian::read<uint32_t>(Ptr, Endianness);
  case 8:
    return support::endian::read<uint64_t>(Ptr, Endianness);
  }
  llvm_unreachable("Unsupported read size");
}

StringRef RuntimeDyldCheckerImpl::getSymbolContent(StringRef Symbol) const {
  auto SymInfo = GetSymbolInfo(Symbol);
  if (!SymInfo) {
    logAllUnhandledErrors(SymInfo.takeError(), errs(), "RTDyldChecker: ");
    return StringRef();
  }
  return {SymInfo->getContent().data(), SymInfo->getContent().size()};
}

TargetFlagsType RuntimeDyldCheckerImpl::getTargetFlag(StringRef Symbol) const {
  auto SymInfo = GetSymbolInfo(Symbol);
  if (!SymInfo) {
    logAllUnhandledErrors(SymInfo.takeError(), errs(), "RTDyldChecker: ");
    return TargetFlagsType{};
  }
  return SymInfo->getTargetFlags();
}

Triple
RuntimeDyldCheckerImpl::getTripleForSymbol(TargetFlagsType Flag) const {
  Triple TheTriple = TT;

  switch (TT.getArch()) {
  case Triple::ArchType::arm:
    if (~Flag & 0x1)
      return TT;
    TheTriple.setArchName((Twine("thumb") + TT.getArchName().substr(3)).str());
    return TheTriple;
  case Triple::ArchType::thumb:
    if (Flag & 0x1)
      return TT;
    TheTriple.setArchName((Twine("arm") + TT.getArchName().substr(5)).str());
    return TheTriple;

  default:
    return TT;
  }
}

std::pair<uint64_t, std::string> RuntimeDyldCheckerImpl::getSectionAddr(
    StringRef FileName, StringRef SectionName, bool IsInsideLoad) const {

  auto SecInfo = GetSectionInfo(FileName, SectionName);
  if (!SecInfo) {
    std::string ErrMsg;
    {
      raw_string_ostream ErrMsgStream(ErrMsg);
      logAllUnhandledErrors(SecInfo.takeError(), ErrMsgStream,
                            "RTDyldChecker: ");
    }
    return std::make_pair(0, std::move(ErrMsg));
  }

  // If this address is being looked up in "load" mode, return the content
  // pointer, otherwise return the target address.

  uint64_t Addr = 0;

  if (IsInsideLoad) {
    if (SecInfo->isZeroFill())
      Addr = 0;
    else
      Addr = pointerToJITTargetAddress(SecInfo->getContent().data());
  } else
    Addr = SecInfo->getTargetAddress();

  return std::make_pair(Addr, "");
}

std::pair<uint64_t, std::string> RuntimeDyldCheckerImpl::getStubOrGOTAddrFor(
    StringRef StubContainerName, StringRef SymbolName, StringRef StubKindFilter,
    bool IsInsideLoad, bool IsStubAddr) const {

  assert((StubKindFilter.empty() || IsStubAddr) &&
         "Kind name filter only supported for stubs");
  auto StubInfo =
      IsStubAddr ? GetStubInfo(StubContainerName, SymbolName, StubKindFilter)
                 : GetGOTInfo(StubContainerName, SymbolName);

  if (!StubInfo) {
    std::string ErrMsg;
    {
      raw_string_ostream ErrMsgStream(ErrMsg);
      logAllUnhandledErrors(StubInfo.takeError(), ErrMsgStream,
                            "RTDyldChecker: ");
    }
    return std::make_pair((uint64_t)0, std::move(ErrMsg));
  }

  uint64_t Addr = 0;

  if (IsInsideLoad) {
    if (StubInfo->isZeroFill())
      return std::make_pair((uint64_t)0, "Detected zero-filled stub/GOT entry");
    Addr = pointerToJITTargetAddress(StubInfo->getContent().data());
  } else
    Addr = StubInfo->getTargetAddress();

  return std::make_pair(Addr, "");
}

RuntimeDyldChecker::RuntimeDyldChecker(
    IsSymbolValidFunction IsSymbolValid, GetSymbolInfoFunction GetSymbolInfo,
    GetSectionInfoFunction GetSectionInfo, GetStubInfoFunction GetStubInfo,
    GetGOTInfoFunction GetGOTInfo, llvm::endianness Endianness, Triple TT,
    StringRef CPU, SubtargetFeatures TF, raw_ostream &ErrStream)
    : Impl(::std::make_unique<RuntimeDyldCheckerImpl>(
          std::move(IsSymbolValid), std::move(GetSymbolInfo),
          std::move(GetSectionInfo), std::move(GetStubInfo),
          std::move(GetGOTInfo), Endianness, std::move(TT), std::move(CPU),
          std::move(TF), ErrStream)) {}

RuntimeDyldChecker::~RuntimeDyldChecker() = default;

bool RuntimeDyldChecker::check(StringRef CheckExpr) const {
  return Impl->check(CheckExpr);
}

bool RuntimeDyldChecker::checkAllRulesInBuffer(StringRef RulePrefix,
                                               MemoryBuffer *MemBuf) const {
  return Impl->checkAllRulesInBuffer(RulePrefix, MemBuf);
}

std::pair<uint64_t, std::string>
RuntimeDyldChecker::getSectionAddr(StringRef FileName, StringRef SectionName,
                                   bool LocalAddress) {
  return Impl->getSectionAddr(FileName, SectionName, LocalAddress);
}
