//===-- X86AsmParser.cpp - Parse X86 assembly to MCInst instructions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86EncodingOptimization.h"
#include "MCTargetDesc/X86IntelInstPrinter.h"
#include "MCTargetDesc/X86MCExpr.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "MCTargetDesc/X86TargetStreamer.h"
#include "TargetInfo/X86TargetInfo.h"
#include "X86AsmParserCommon.h"
#include "X86Operand.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>

using namespace llvm;

static cl::opt<bool> LVIInlineAsmHardening(
    "x86-experimental-lvi-inline-asm-hardening",
    cl::desc("Harden inline assembly code that may be vulnerable to Load Value"
             " Injection (LVI). This feature is experimental."), cl::Hidden);

static bool checkScale(unsigned Scale, StringRef &ErrMsg) {
  if (Scale != 1 && Scale != 2 && Scale != 4 && Scale != 8) {
    ErrMsg = "scale factor in address must be 1, 2, 4 or 8";
    return true;
  }
  return false;
}

namespace {

// Including the generated SSE2AVX compression tables.
#define GET_X86_SSE2AVX_TABLE
#include "X86GenInstrMapping.inc"

static const char OpPrecedence[] = {
    0,  // IC_OR
    1,  // IC_XOR
    2,  // IC_AND
    4,  // IC_LSHIFT
    4,  // IC_RSHIFT
    5,  // IC_PLUS
    5,  // IC_MINUS
    6,  // IC_MULTIPLY
    6,  // IC_DIVIDE
    6,  // IC_MOD
    7,  // IC_NOT
    8,  // IC_NEG
    9,  // IC_RPAREN
    10, // IC_LPAREN
    0,  // IC_IMM
    0,  // IC_REGISTER
    3,  // IC_EQ
    3,  // IC_NE
    3,  // IC_LT
    3,  // IC_LE
    3,  // IC_GT
    3   // IC_GE
};

class X86AsmParser : public MCTargetAsmParser {
  ParseInstructionInfo *InstInfo;
  bool Code16GCC;
  unsigned ForcedDataPrefix = 0;

  enum OpcodePrefix {
    OpcodePrefix_Default,
    OpcodePrefix_REX,
    OpcodePrefix_REX2,
    OpcodePrefix_VEX,
    OpcodePrefix_VEX2,
    OpcodePrefix_VEX3,
    OpcodePrefix_EVEX,
  };

  OpcodePrefix ForcedOpcodePrefix = OpcodePrefix_Default;

  enum DispEncoding {
    DispEncoding_Default,
    DispEncoding_Disp8,
    DispEncoding_Disp32,
  };

  DispEncoding ForcedDispEncoding = DispEncoding_Default;

  // Does this instruction use apx extended register?
  bool UseApxExtendedReg = false;
  // Is this instruction explicitly required not to update flags?
  bool ForcedNoFlag = false;

private:
  SMLoc consumeToken() {
    MCAsmParser &Parser = getParser();
    SMLoc Result = Parser.getTok().getLoc();
    Parser.Lex();
    return Result;
  }

  X86TargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<X86TargetStreamer &>(TS);
  }

  unsigned MatchInstruction(const OperandVector &Operands, MCInst &Inst,
                            uint64_t &ErrorInfo, FeatureBitset &MissingFeatures,
                            bool matchingInlineAsm, unsigned VariantID = 0) {
    // In Code16GCC mode, match as 32-bit.
    if (Code16GCC)
      SwitchMode(X86::Is32Bit);
    unsigned rv = MatchInstructionImpl(Operands, Inst, ErrorInfo,
                                       MissingFeatures, matchingInlineAsm,
                                       VariantID);
    if (Code16GCC)
      SwitchMode(X86::Is16Bit);
    return rv;
  }

  enum InfixCalculatorTok {
    IC_OR = 0,
    IC_XOR,
    IC_AND,
    IC_LSHIFT,
    IC_RSHIFT,
    IC_PLUS,
    IC_MINUS,
    IC_MULTIPLY,
    IC_DIVIDE,
    IC_MOD,
    IC_NOT,
    IC_NEG,
    IC_RPAREN,
    IC_LPAREN,
    IC_IMM,
    IC_REGISTER,
    IC_EQ,
    IC_NE,
    IC_LT,
    IC_LE,
    IC_GT,
    IC_GE
  };

  enum IntelOperatorKind {
    IOK_INVALID = 0,
    IOK_LENGTH,
    IOK_SIZE,
    IOK_TYPE,
  };

  enum MasmOperatorKind {
    MOK_INVALID = 0,
    MOK_LENGTHOF,
    MOK_SIZEOF,
    MOK_TYPE,
  };

  class InfixCalculator {
    typedef std::pair< InfixCalculatorTok, int64_t > ICToken;
    SmallVector<InfixCalculatorTok, 4> InfixOperatorStack;
    SmallVector<ICToken, 4> PostfixStack;

    bool isUnaryOperator(InfixCalculatorTok Op) const {
      return Op == IC_NEG || Op == IC_NOT;
    }

  public:
    int64_t popOperand() {
      assert (!PostfixStack.empty() && "Poped an empty stack!");
      ICToken Op = PostfixStack.pop_back_val();
      if (!(Op.first == IC_IMM || Op.first == IC_REGISTER))
        return -1; // The invalid Scale value will be caught later by checkScale
      return Op.second;
    }
    void pushOperand(InfixCalculatorTok Op, int64_t Val = 0) {
      assert ((Op == IC_IMM || Op == IC_REGISTER) &&
              "Unexpected operand!");
      PostfixStack.push_back(std::make_pair(Op, Val));
    }

    void popOperator() { InfixOperatorStack.pop_back(); }
    void pushOperator(InfixCalculatorTok Op) {
      // Push the new operator if the stack is empty.
      if (InfixOperatorStack.empty()) {
        InfixOperatorStack.push_back(Op);
        return;
      }

      // Push the new operator if it has a higher precedence than the operator
      // on the top of the stack or the operator on the top of the stack is a
      // left parentheses.
      unsigned Idx = InfixOperatorStack.size() - 1;
      InfixCalculatorTok StackOp = InfixOperatorStack[Idx];
      if (OpPrecedence[Op] > OpPrecedence[StackOp] || StackOp == IC_LPAREN) {
        InfixOperatorStack.push_back(Op);
        return;
      }

      // The operator on the top of the stack has higher precedence than the
      // new operator.
      unsigned ParenCount = 0;
      while (true) {
        // Nothing to process.
        if (InfixOperatorStack.empty())
          break;

        Idx = InfixOperatorStack.size() - 1;
        StackOp = InfixOperatorStack[Idx];
        if (!(OpPrecedence[StackOp] >= OpPrecedence[Op] || ParenCount))
          break;

        // If we have an even parentheses count and we see a left parentheses,
        // then stop processing.
        if (!ParenCount && StackOp == IC_LPAREN)
          break;

        if (StackOp == IC_RPAREN) {
          ++ParenCount;
          InfixOperatorStack.pop_back();
        } else if (StackOp == IC_LPAREN) {
          --ParenCount;
          InfixOperatorStack.pop_back();
        } else {
          InfixOperatorStack.pop_back();
          PostfixStack.push_back(std::make_pair(StackOp, 0));
        }
      }
      // Push the new operator.
      InfixOperatorStack.push_back(Op);
    }

    int64_t execute() {
      // Push any remaining operators onto the postfix stack.
      while (!InfixOperatorStack.empty()) {
        InfixCalculatorTok StackOp = InfixOperatorStack.pop_back_val();
        if (StackOp != IC_LPAREN && StackOp != IC_RPAREN)
          PostfixStack.push_back(std::make_pair(StackOp, 0));
      }

      if (PostfixStack.empty())
        return 0;

      SmallVector<ICToken, 16> OperandStack;
      for (const ICToken &Op : PostfixStack) {
        if (Op.first == IC_IMM || Op.first == IC_REGISTER) {
          OperandStack.push_back(Op);
        } else if (isUnaryOperator(Op.first)) {
          assert (OperandStack.size() > 0 && "Too few operands.");
          ICToken Operand = OperandStack.pop_back_val();
          assert (Operand.first == IC_IMM &&
                  "Unary operation with a register!");
          switch (Op.first) {
          default:
            report_fatal_error("Unexpected operator!");
            break;
          case IC_NEG:
            OperandStack.push_back(std::make_pair(IC_IMM, -Operand.second));
            break;
          case IC_NOT:
            OperandStack.push_back(std::make_pair(IC_IMM, ~Operand.second));
            break;
          }
        } else {
          assert (OperandStack.size() > 1 && "Too few operands.");
          int64_t Val;
          ICToken Op2 = OperandStack.pop_back_val();
          ICToken Op1 = OperandStack.pop_back_val();
          switch (Op.first) {
          default:
            report_fatal_error("Unexpected operator!");
            break;
          case IC_PLUS:
            Val = Op1.second + Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_MINUS:
            Val = Op1.second - Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_MULTIPLY:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Multiply operation with an immediate and a register!");
            Val = Op1.second * Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_DIVIDE:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Divide operation with an immediate and a register!");
            assert (Op2.second != 0 && "Division by zero!");
            Val = Op1.second / Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_MOD:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Modulo operation with an immediate and a register!");
            Val = Op1.second % Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_OR:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Or operation with an immediate and a register!");
            Val = Op1.second | Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_XOR:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
              "Xor operation with an immediate and a register!");
            Val = Op1.second ^ Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_AND:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "And operation with an immediate and a register!");
            Val = Op1.second & Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_LSHIFT:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Left shift operation with an immediate and a register!");
            Val = Op1.second << Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_RSHIFT:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Right shift operation with an immediate and a register!");
            Val = Op1.second >> Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_EQ:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
                   "Equals operation with an immediate and a register!");
            Val = (Op1.second == Op2.second) ? -1 : 0;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_NE:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
                   "Not-equals operation with an immediate and a register!");
            Val = (Op1.second != Op2.second) ? -1 : 0;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_LT:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
                   "Less-than operation with an immediate and a register!");
            Val = (Op1.second < Op2.second) ? -1 : 0;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_LE:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
                   "Less-than-or-equal operation with an immediate and a "
                   "register!");
            Val = (Op1.second <= Op2.second) ? -1 : 0;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_GT:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
                   "Greater-than operation with an immediate and a register!");
            Val = (Op1.second > Op2.second) ? -1 : 0;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_GE:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
                   "Greater-than-or-equal operation with an immediate and a "
                   "register!");
            Val = (Op1.second >= Op2.second) ? -1 : 0;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          }
        }
      }
      assert (OperandStack.size() == 1 && "Expected a single result.");
      return OperandStack.pop_back_val().second;
    }
  };

  enum IntelExprState {
    IES_INIT,
    IES_OR,
    IES_XOR,
    IES_AND,
    IES_EQ,
    IES_NE,
    IES_LT,
    IES_LE,
    IES_GT,
    IES_GE,
    IES_LSHIFT,
    IES_RSHIFT,
    IES_PLUS,
    IES_MINUS,
    IES_OFFSET,
    IES_CAST,
    IES_NOT,
    IES_MULTIPLY,
    IES_DIVIDE,
    IES_MOD,
    IES_LBRAC,
    IES_RBRAC,
    IES_LPAREN,
    IES_RPAREN,
    IES_REGISTER,
    IES_INTEGER,
    IES_ERROR
  };

  class IntelExprStateMachine {
    IntelExprState State = IES_INIT, PrevState = IES_ERROR;
    unsigned BaseReg = 0, IndexReg = 0, TmpReg = 0, Scale = 0;
    int64_t Imm = 0;
    const MCExpr *Sym = nullptr;
    StringRef SymName;
    InfixCalculator IC;
    InlineAsmIdentifierInfo Info;
    short BracCount = 0;
    bool MemExpr = false;
    bool BracketUsed = false;
    bool OffsetOperator = false;
    bool AttachToOperandIdx = false;
    bool IsPIC = false;
    SMLoc OffsetOperatorLoc;
    AsmTypeInfo CurType;

    bool setSymRef(const MCExpr *Val, StringRef ID, StringRef &ErrMsg) {
      if (Sym) {
        ErrMsg = "cannot use more than one symbol in memory operand";
        return true;
      }
      Sym = Val;
      SymName = ID;
      return false;
    }

  public:
    IntelExprStateMachine() = default;

    void addImm(int64_t imm) { Imm += imm; }
    short getBracCount() const { return BracCount; }
    bool isMemExpr() const { return MemExpr; }
    bool isBracketUsed() const { return BracketUsed; }
    bool isOffsetOperator() const { return OffsetOperator; }
    SMLoc getOffsetLoc() const { return OffsetOperatorLoc; }
    unsigned getBaseReg() const { return BaseReg; }
    unsigned getIndexReg() const { return IndexReg; }
    unsigned getScale() const { return Scale; }
    const MCExpr *getSym() const { return Sym; }
    StringRef getSymName() const { return SymName; }
    StringRef getType() const { return CurType.Name; }
    unsigned getSize() const { return CurType.Size; }
    unsigned getElementSize() const { return CurType.ElementSize; }
    unsigned getLength() const { return CurType.Length; }
    int64_t getImm() { return Imm + IC.execute(); }
    bool isValidEndState() const {
      return State == IES_RBRAC || State == IES_RPAREN ||
             State == IES_INTEGER || State == IES_REGISTER ||
             State == IES_OFFSET;
    }

    // Is the intel expression appended after an operand index.
    // [OperandIdx][Intel Expression]
    // This is neccessary for checking if it is an independent
    // intel expression at back end when parse inline asm.
    void setAppendAfterOperand() { AttachToOperandIdx = true; }

    bool isPIC() const { return IsPIC; }
    void setPIC() { IsPIC = true; }

    bool hadError() const { return State == IES_ERROR; }
    const InlineAsmIdentifierInfo &getIdentifierInfo() const { return Info; }

    bool regsUseUpError(StringRef &ErrMsg) {
      // This case mostly happen in inline asm, e.g. Arr[BaseReg + IndexReg]
      // can not intruduce additional register in inline asm in PIC model.
      if (IsPIC && AttachToOperandIdx)
        ErrMsg = "Don't use 2 or more regs for mem offset in PIC model!";
      else
        ErrMsg = "BaseReg/IndexReg already set!";
      return true;
    }

    void onOr() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_OR;
        IC.pushOperator(IC_OR);
        break;
      }
      PrevState = CurrState;
    }
    void onXor() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_XOR;
        IC.pushOperator(IC_XOR);
        break;
      }
      PrevState = CurrState;
    }
    void onAnd() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_AND;
        IC.pushOperator(IC_AND);
        break;
      }
      PrevState = CurrState;
    }
    void onEq() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_EQ;
        IC.pushOperator(IC_EQ);
        break;
      }
      PrevState = CurrState;
    }
    void onNE() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_NE;
        IC.pushOperator(IC_NE);
        break;
      }
      PrevState = CurrState;
    }
    void onLT() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_LT;
        IC.pushOperator(IC_LT);
        break;
      }
      PrevState = CurrState;
    }
    void onLE() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_LE;
        IC.pushOperator(IC_LE);
        break;
      }
      PrevState = CurrState;
    }
    void onGT() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_GT;
        IC.pushOperator(IC_GT);
        break;
      }
      PrevState = CurrState;
    }
    void onGE() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_GE;
        IC.pushOperator(IC_GE);
        break;
      }
      PrevState = CurrState;
    }
    void onLShift() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_LSHIFT;
        IC.pushOperator(IC_LSHIFT);
        break;
      }
      PrevState = CurrState;
    }
    void onRShift() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_RSHIFT;
        IC.pushOperator(IC_RSHIFT);
        break;
      }
      PrevState = CurrState;
    }
    bool onPlus(StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
      case IES_OFFSET:
        State = IES_PLUS;
        IC.pushOperator(IC_PLUS);
        if (CurrState == IES_REGISTER && PrevState != IES_MULTIPLY) {
          // If we already have a BaseReg, then assume this is the IndexReg with
          // no explicit scale.
          if (!BaseReg) {
            BaseReg = TmpReg;
          } else {
            if (IndexReg)
              return regsUseUpError(ErrMsg);
            IndexReg = TmpReg;
            Scale = 0;
          }
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    bool onMinus(StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_EQ:
      case IES_NE:
      case IES_LT:
      case IES_LE:
      case IES_GT:
      case IES_GE:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_PLUS:
      case IES_NOT:
      case IES_MULTIPLY:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_LPAREN:
      case IES_RPAREN:
      case IES_LBRAC:
      case IES_RBRAC:
      case IES_INTEGER:
      case IES_REGISTER:
      case IES_INIT:
      case IES_OFFSET:
        State = IES_MINUS;
        // push minus operator if it is not a negate operator
        if (CurrState == IES_REGISTER || CurrState == IES_RPAREN ||
            CurrState == IES_INTEGER  || CurrState == IES_RBRAC  ||
            CurrState == IES_OFFSET)
          IC.pushOperator(IC_MINUS);
        else if (PrevState == IES_REGISTER && CurrState == IES_MULTIPLY) {
          // We have negate operator for Scale: it's illegal
          ErrMsg = "Scale can't be negative";
          return true;
        } else
          IC.pushOperator(IC_NEG);
        if (CurrState == IES_REGISTER && PrevState != IES_MULTIPLY) {
          // If we already have a BaseReg, then assume this is the IndexReg with
          // no explicit scale.
          if (!BaseReg) {
            BaseReg = TmpReg;
          } else {
            if (IndexReg)
              return regsUseUpError(ErrMsg);
            IndexReg = TmpReg;
            Scale = 0;
          }
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    void onNot() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_EQ:
      case IES_NE:
      case IES_LT:
      case IES_LE:
      case IES_GT:
      case IES_GE:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_MULTIPLY:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_LPAREN:
      case IES_LBRAC:
      case IES_INIT:
        State = IES_NOT;
        IC.pushOperator(IC_NOT);
        break;
      }
      PrevState = CurrState;
    }
    bool onRegister(unsigned Reg, StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_LPAREN:
      case IES_LBRAC:
        State = IES_REGISTER;
        TmpReg = Reg;
        IC.pushOperand(IC_REGISTER);
        break;
      case IES_MULTIPLY:
        // Index Register - Scale * Register
        if (PrevState == IES_INTEGER) {
          if (IndexReg)
            return regsUseUpError(ErrMsg);
          State = IES_REGISTER;
          IndexReg = Reg;
          // Get the scale and replace the 'Scale * Register' with '0'.
          Scale = IC.popOperand();
          if (checkScale(Scale, ErrMsg))
            return true;
          IC.pushOperand(IC_IMM);
          IC.popOperator();
        } else {
          State = IES_ERROR;
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    bool onIdentifierExpr(const MCExpr *SymRef, StringRef SymRefName,
                          const InlineAsmIdentifierInfo &IDInfo,
                          const AsmTypeInfo &Type, bool ParsingMSInlineAsm,
                          StringRef &ErrMsg) {
      // InlineAsm: Treat an enum value as an integer
      if (ParsingMSInlineAsm)
        if (IDInfo.isKind(InlineAsmIdentifierInfo::IK_EnumVal))
          return onInteger(IDInfo.Enum.EnumVal, ErrMsg);
      // Treat a symbolic constant like an integer
      if (auto *CE = dyn_cast<MCConstantExpr>(SymRef))
        return onInteger(CE->getValue(), ErrMsg);
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_CAST:
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_INIT:
      case IES_LBRAC:
      case IES_LPAREN:
        if (setSymRef(SymRef, SymRefName, ErrMsg))
          return true;
        MemExpr = true;
        State = IES_INTEGER;
        IC.pushOperand(IC_IMM);
        if (ParsingMSInlineAsm)
          Info = IDInfo;
        setTypeInfo(Type);
        break;
      }
      return false;
    }
    bool onInteger(int64_t TmpInt, StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_EQ:
      case IES_NE:
      case IES_LT:
      case IES_LE:
      case IES_GT:
      case IES_GE:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_MULTIPLY:
      case IES_LPAREN:
      case IES_INIT:
      case IES_LBRAC:
        State = IES_INTEGER;
        if (PrevState == IES_REGISTER && CurrState == IES_MULTIPLY) {
          // Index Register - Register * Scale
          if (IndexReg)
            return regsUseUpError(ErrMsg);
          IndexReg = TmpReg;
          Scale = TmpInt;
          if (checkScale(Scale, ErrMsg))
            return true;
          // Get the scale and replace the 'Register * Scale' with '0'.
          IC.popOperator();
        } else {
          IC.pushOperand(IC_IMM, TmpInt);
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    void onStar() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_REGISTER:
      case IES_RPAREN:
        State = IES_MULTIPLY;
        IC.pushOperator(IC_MULTIPLY);
        break;
      }
    }
    void onDivide() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
        State = IES_DIVIDE;
        IC.pushOperator(IC_DIVIDE);
        break;
      }
    }
    void onMod() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
        State = IES_MOD;
        IC.pushOperator(IC_MOD);
        break;
      }
    }
    bool onLBrac() {
      if (BracCount)
        return true;
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_RBRAC:
      case IES_INTEGER:
      case IES_RPAREN:
        State = IES_PLUS;
        IC.pushOperator(IC_PLUS);
        CurType.Length = 1;
        CurType.Size = CurType.ElementSize;
        break;
      case IES_INIT:
      case IES_CAST:
        assert(!BracCount && "BracCount should be zero on parsing's start");
        State = IES_LBRAC;
        break;
      }
      MemExpr = true;
      BracketUsed = true;
      BracCount++;
      return false;
    }
    bool onRBrac(StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_OFFSET:
      case IES_REGISTER:
      case IES_RPAREN:
        if (BracCount-- != 1) {
          ErrMsg = "unexpected bracket encountered";
          return true;
        }
        State = IES_RBRAC;
        if (CurrState == IES_REGISTER && PrevState != IES_MULTIPLY) {
          // If we already have a BaseReg, then assume this is the IndexReg with
          // no explicit scale.
          if (!BaseReg) {
            BaseReg = TmpReg;
          } else {
            if (IndexReg)
              return regsUseUpError(ErrMsg);
            IndexReg = TmpReg;
            Scale = 0;
          }
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    void onLParen() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_EQ:
      case IES_NE:
      case IES_LT:
      case IES_LE:
      case IES_GT:
      case IES_GE:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_MULTIPLY:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_LPAREN:
      case IES_INIT:
      case IES_LBRAC:
        State = IES_LPAREN;
        IC.pushOperator(IC_LPAREN);
        break;
      }
      PrevState = CurrState;
    }
    void onRParen() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_OFFSET:
      case IES_REGISTER:
      case IES_RBRAC:
      case IES_RPAREN:
        State = IES_RPAREN;
        IC.pushOperator(IC_RPAREN);
        break;
      }
    }
    bool onOffset(const MCExpr *Val, SMLoc OffsetLoc, StringRef ID,
                  const InlineAsmIdentifierInfo &IDInfo,
                  bool ParsingMSInlineAsm, StringRef &ErrMsg) {
      PrevState = State;
      switch (State) {
      default:
        ErrMsg = "unexpected offset operator expression";
        return true;
      case IES_PLUS:
      case IES_INIT:
      case IES_LBRAC:
        if (setSymRef(Val, ID, ErrMsg))
          return true;
        OffsetOperator = true;
        OffsetOperatorLoc = OffsetLoc;
        State = IES_OFFSET;
        // As we cannot yet resolve the actual value (offset), we retain
        // the requested semantics by pushing a '0' to the operands stack
        IC.pushOperand(IC_IMM);
        if (ParsingMSInlineAsm) {
          Info = IDInfo;
        }
        break;
      }
      return false;
    }
    void onCast(AsmTypeInfo Info) {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_LPAREN:
        setTypeInfo(Info);
        State = IES_CAST;
        break;
      }
    }
    void setTypeInfo(AsmTypeInfo Type) { CurType = Type; }
  };

  bool Error(SMLoc L, const Twine &Msg, SMRange Range = std::nullopt,
             bool MatchingInlineAsm = false) {
    MCAsmParser &Parser = getParser();
    if (MatchingInlineAsm) {
      if (!getLexer().isAtStartOfStatement())
        Parser.eatToEndOfStatement();
      return false;
    }
    return Parser.Error(L, Msg, Range);
  }

  bool MatchRegisterByName(MCRegister &RegNo, StringRef RegName, SMLoc StartLoc,
                           SMLoc EndLoc);
  bool ParseRegister(MCRegister &RegNo, SMLoc &StartLoc, SMLoc &EndLoc,
                     bool RestoreOnFailure);

  std::unique_ptr<X86Operand> DefaultMemSIOperand(SMLoc Loc);
  std::unique_ptr<X86Operand> DefaultMemDIOperand(SMLoc Loc);
  bool IsSIReg(unsigned Reg);
  unsigned GetSIDIForRegClass(unsigned RegClassID, unsigned Reg, bool IsSIReg);
  void
  AddDefaultSrcDestOperands(OperandVector &Operands,
                            std::unique_ptr<llvm::MCParsedAsmOperand> &&Src,
                            std::unique_ptr<llvm::MCParsedAsmOperand> &&Dst);
  bool VerifyAndAdjustOperands(OperandVector &OrigOperands,
                               OperandVector &FinalOperands);
  bool parseOperand(OperandVector &Operands, StringRef Name);
  bool parseATTOperand(OperandVector &Operands);
  bool parseIntelOperand(OperandVector &Operands, StringRef Name);
  bool ParseIntelOffsetOperator(const MCExpr *&Val, StringRef &ID,
                                InlineAsmIdentifierInfo &Info, SMLoc &End);
  bool ParseIntelDotOperator(IntelExprStateMachine &SM, SMLoc &End);
  unsigned IdentifyIntelInlineAsmOperator(StringRef Name);
  unsigned ParseIntelInlineAsmOperator(unsigned OpKind);
  unsigned IdentifyMasmOperator(StringRef Name);
  bool ParseMasmOperator(unsigned OpKind, int64_t &Val);
  bool ParseRoundingModeOp(SMLoc Start, OperandVector &Operands);
  bool parseCFlagsOp(OperandVector &Operands);
  bool ParseIntelNamedOperator(StringRef Name, IntelExprStateMachine &SM,
                               bool &ParseError, SMLoc &End);
  bool ParseMasmNamedOperator(StringRef Name, IntelExprStateMachine &SM,
                              bool &ParseError, SMLoc &End);
  void RewriteIntelExpression(IntelExprStateMachine &SM, SMLoc Start,
                              SMLoc End);
  bool ParseIntelExpression(IntelExprStateMachine &SM, SMLoc &End);
  bool ParseIntelInlineAsmIdentifier(const MCExpr *&Val, StringRef &Identifier,
                                     InlineAsmIdentifierInfo &Info,
                                     bool IsUnevaluatedOperand, SMLoc &End,
                                     bool IsParsingOffsetOperator = false);
  void tryParseOperandIdx(AsmToken::TokenKind PrevTK,
                          IntelExprStateMachine &SM);

  bool ParseMemOperand(unsigned SegReg, const MCExpr *Disp, SMLoc StartLoc,
                       SMLoc EndLoc, OperandVector &Operands);

  X86::CondCode ParseConditionCode(StringRef CCode);

  bool ParseIntelMemoryOperandSize(unsigned &Size);
  bool CreateMemForMSInlineAsm(unsigned SegReg, const MCExpr *Disp,
                               unsigned BaseReg, unsigned IndexReg,
                               unsigned Scale, bool NonAbsMem, SMLoc Start,
                               SMLoc End, unsigned Size, StringRef Identifier,
                               const InlineAsmIdentifierInfo &Info,
                               OperandVector &Operands);

  bool parseDirectiveArch();
  bool parseDirectiveNops(SMLoc L);
  bool parseDirectiveEven(SMLoc L);
  bool ParseDirectiveCode(StringRef IDVal, SMLoc L);

  /// CodeView FPO data directives.
  bool parseDirectiveFPOProc(SMLoc L);
  bool parseDirectiveFPOSetFrame(SMLoc L);
  bool parseDirectiveFPOPushReg(SMLoc L);
  bool parseDirectiveFPOStackAlloc(SMLoc L);
  bool parseDirectiveFPOStackAlign(SMLoc L);
  bool parseDirectiveFPOEndPrologue(SMLoc L);
  bool parseDirectiveFPOEndProc(SMLoc L);

  /// SEH directives.
  bool parseSEHRegisterNumber(unsigned RegClassID, MCRegister &RegNo);
  bool parseDirectiveSEHPushReg(SMLoc);
  bool parseDirectiveSEHSetFrame(SMLoc);
  bool parseDirectiveSEHSaveReg(SMLoc);
  bool parseDirectiveSEHSaveXMM(SMLoc);
  bool parseDirectiveSEHPushFrame(SMLoc);

  unsigned checkTargetMatchPredicate(MCInst &Inst) override;

  bool validateInstruction(MCInst &Inst, const OperandVector &Ops);
  bool processInstruction(MCInst &Inst, const OperandVector &Ops);

  // Load Value Injection (LVI) Mitigations for machine code
  void emitWarningForSpecialLVIInstruction(SMLoc Loc);
  void applyLVICFIMitigation(MCInst &Inst, MCStreamer &Out);
  void applyLVILoadHardeningMitigation(MCInst &Inst, MCStreamer &Out);

  /// Wrapper around MCStreamer::emitInstruction(). Possibly adds
  /// instrumentation around Inst.
  void emitInstruction(MCInst &Inst, OperandVector &Operands, MCStreamer &Out);

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  void MatchFPUWaitAlias(SMLoc IDLoc, X86Operand &Op, OperandVector &Operands,
                         MCStreamer &Out, bool MatchingInlineAsm);

  bool ErrorMissingFeature(SMLoc IDLoc, const FeatureBitset &MissingFeatures,
                           bool MatchingInlineAsm);

  bool matchAndEmitATTInstruction(SMLoc IDLoc, unsigned &Opcode, MCInst &Inst,
                                  OperandVector &Operands, MCStreamer &Out,
                                  uint64_t &ErrorInfo, bool MatchingInlineAsm);

  bool matchAndEmitIntelInstruction(SMLoc IDLoc, unsigned &Opcode, MCInst &Inst,
                                    OperandVector &Operands, MCStreamer &Out,
                                    uint64_t &ErrorInfo,
                                    bool MatchingInlineAsm);

  bool OmitRegisterFromClobberLists(unsigned RegNo) override;

  /// Parses AVX512 specific operand primitives: masked registers ({%k<NUM>}, {z})
  /// and memory broadcasting ({1to<NUM>}) primitives, updating Operands vector if required.
  /// return false if no parsing errors occurred, true otherwise.
  bool HandleAVX512Operand(OperandVector &Operands);

  bool ParseZ(std::unique_ptr<X86Operand> &Z, const SMLoc &StartLoc);

  bool is64BitMode() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().hasFeature(X86::Is64Bit);
  }
  bool is32BitMode() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().hasFeature(X86::Is32Bit);
  }
  bool is16BitMode() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().hasFeature(X86::Is16Bit);
  }
  void SwitchMode(unsigned mode) {
    MCSubtargetInfo &STI = copySTI();
    FeatureBitset AllModes({X86::Is64Bit, X86::Is32Bit, X86::Is16Bit});
    FeatureBitset OldMode = STI.getFeatureBits() & AllModes;
    FeatureBitset FB = ComputeAvailableFeatures(
      STI.ToggleFeature(OldMode.flip(mode)));
    setAvailableFeatures(FB);

    assert(FeatureBitset({mode}) == (STI.getFeatureBits() & AllModes));
  }

  unsigned getPointerWidth() {
    if (is16BitMode()) return 16;
    if (is32BitMode()) return 32;
    if (is64BitMode()) return 64;
    llvm_unreachable("invalid mode");
  }

  bool isParsingIntelSyntax() {
    return getParser().getAssemblerDialect();
  }

  /// @name Auto-generated Matcher Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "X86GenAsmMatcher.inc"

  /// }

public:
  enum X86MatchResultTy {
    Match_Unsupported = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "X86GenAsmMatcher.inc"
  };

  X86AsmParser(const MCSubtargetInfo &sti, MCAsmParser &Parser,
               const MCInstrInfo &mii, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, sti, mii),  InstInfo(nullptr),
        Code16GCC(false) {

    Parser.addAliasForDirective(".word", ".2byte");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool ParseDirective(AsmToken DirectiveID) override;
};
} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#include "X86GenAsmMatcher.inc"

static bool CheckBaseRegAndIndexRegAndScale(unsigned BaseReg, unsigned IndexReg,
                                            unsigned Scale, bool Is64BitMode,
                                            StringRef &ErrMsg) {
  // If we have both a base register and an index register make sure they are
  // both 64-bit or 32-bit registers.
  // To support VSIB, IndexReg can be 128-bit or 256-bit registers.

  if (BaseReg != 0 &&
      !(BaseReg == X86::RIP || BaseReg == X86::EIP ||
        X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg) ||
        X86MCRegisterClasses[X86::GR32RegClassID].contains(BaseReg) ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg))) {
    ErrMsg = "invalid base+index expression";
    return true;
  }

  if (IndexReg != 0 &&
      !(IndexReg == X86::EIZ || IndexReg == X86::RIZ ||
        X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR128XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR256XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR512RegClassID].contains(IndexReg))) {
    ErrMsg = "invalid base+index expression";
    return true;
  }

  if (((BaseReg == X86::RIP || BaseReg == X86::EIP) && IndexReg != 0) ||
      IndexReg == X86::EIP || IndexReg == X86::RIP ||
      IndexReg == X86::ESP || IndexReg == X86::RSP) {
    ErrMsg = "invalid base+index expression";
    return true;
  }

  // Check for use of invalid 16-bit registers. Only BX/BP/SI/DI are allowed,
  // and then only in non-64-bit modes.
  if (X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg) &&
      (Is64BitMode || (BaseReg != X86::BX && BaseReg != X86::BP &&
                       BaseReg != X86::SI && BaseReg != X86::DI))) {
    ErrMsg = "invalid 16-bit base register";
    return true;
  }

  if (BaseReg == 0 &&
      X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg)) {
    ErrMsg = "16-bit memory operand may not include only index register";
    return true;
  }

  if (BaseReg != 0 && IndexReg != 0) {
    if (X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg) &&
        (X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg) ||
         X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg) ||
         IndexReg == X86::EIZ)) {
      ErrMsg = "base register is 64-bit, but index register is not";
      return true;
    }
    if (X86MCRegisterClasses[X86::GR32RegClassID].contains(BaseReg) &&
        (X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg) ||
         X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg) ||
         IndexReg == X86::RIZ)) {
      ErrMsg = "base register is 32-bit, but index register is not";
      return true;
    }
    if (X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg)) {
      if (X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg) ||
          X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg)) {
        ErrMsg = "base register is 16-bit, but index register is not";
        return true;
      }
      if ((BaseReg != X86::BX && BaseReg != X86::BP) ||
          (IndexReg != X86::SI && IndexReg != X86::DI)) {
        ErrMsg = "invalid 16-bit base/index register combination";
        return true;
      }
    }
  }

  // RIP/EIP-relative addressing is only supported in 64-bit mode.
  if (!Is64BitMode && BaseReg != 0 &&
      (BaseReg == X86::RIP || BaseReg == X86::EIP)) {
    ErrMsg = "IP-relative addressing requires 64-bit mode";
    return true;
  }

  return checkScale(Scale, ErrMsg);
}

bool X86AsmParser::MatchRegisterByName(MCRegister &RegNo, StringRef RegName,
                                       SMLoc StartLoc, SMLoc EndLoc) {
  // If we encounter a %, ignore it. This code handles registers with and
  // without the prefix, unprefixed registers can occur in cfi directives.
  RegName.consume_front("%");

  RegNo = MatchRegisterName(RegName);

  // If the match failed, try the register name as lowercase.
  if (RegNo == 0)
    RegNo = MatchRegisterName(RegName.lower());

  // The "flags" and "mxcsr" registers cannot be referenced directly.
  // Treat it as an identifier instead.
  if (isParsingMSInlineAsm() && isParsingIntelSyntax() &&
      (RegNo == X86::EFLAGS || RegNo == X86::MXCSR))
    RegNo = 0;

  if (!is64BitMode()) {
    // FIXME: This should be done using Requires<Not64BitMode> and
    // Requires<In64BitMode> so "eiz" usage in 64-bit instructions can be also
    // checked.
    if (RegNo == X86::RIZ || RegNo == X86::RIP ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(RegNo) ||
        X86II::isX86_64NonExtLowByteReg(RegNo) ||
        X86II::isX86_64ExtendedReg(RegNo)) {
      return Error(StartLoc,
                   "register %" + RegName + " is only available in 64-bit mode",
                   SMRange(StartLoc, EndLoc));
    }
  }

  if (X86II::isApxExtendedReg(RegNo))
    UseApxExtendedReg = true;

  // If this is "db[0-15]", match it as an alias
  // for dr[0-15].
  if (RegNo == 0 && RegName.starts_with("db")) {
    if (RegName.size() == 3) {
      switch (RegName[2]) {
      case '0':
        RegNo = X86::DR0;
        break;
      case '1':
        RegNo = X86::DR1;
        break;
      case '2':
        RegNo = X86::DR2;
        break;
      case '3':
        RegNo = X86::DR3;
        break;
      case '4':
        RegNo = X86::DR4;
        break;
      case '5':
        RegNo = X86::DR5;
        break;
      case '6':
        RegNo = X86::DR6;
        break;
      case '7':
        RegNo = X86::DR7;
        break;
      case '8':
        RegNo = X86::DR8;
        break;
      case '9':
        RegNo = X86::DR9;
        break;
      }
    } else if (RegName.size() == 4 && RegName[2] == '1') {
      switch (RegName[3]) {
      case '0':
        RegNo = X86::DR10;
        break;
      case '1':
        RegNo = X86::DR11;
        break;
      case '2':
        RegNo = X86::DR12;
        break;
      case '3':
        RegNo = X86::DR13;
        break;
      case '4':
        RegNo = X86::DR14;
        break;
      case '5':
        RegNo = X86::DR15;
        break;
      }
    }
  }

  if (RegNo == 0) {
    if (isParsingIntelSyntax())
      return true;
    return Error(StartLoc, "invalid register name", SMRange(StartLoc, EndLoc));
  }
  return false;
}

bool X86AsmParser::ParseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                                 SMLoc &EndLoc, bool RestoreOnFailure) {
  MCAsmParser &Parser = getParser();
  MCAsmLexer &Lexer = getLexer();
  RegNo = 0;

  SmallVector<AsmToken, 5> Tokens;
  auto OnFailure = [RestoreOnFailure, &Lexer, &Tokens]() {
    if (RestoreOnFailure) {
      while (!Tokens.empty()) {
        Lexer.UnLex(Tokens.pop_back_val());
      }
    }
  };

  const AsmToken &PercentTok = Parser.getTok();
  StartLoc = PercentTok.getLoc();

  // If we encounter a %, ignore it. This code handles registers with and
  // without the prefix, unprefixed registers can occur in cfi directives.
  if (!isParsingIntelSyntax() && PercentTok.is(AsmToken::Percent)) {
    Tokens.push_back(PercentTok);
    Parser.Lex(); // Eat percent token.
  }

  const AsmToken &Tok = Parser.getTok();
  EndLoc = Tok.getEndLoc();

  if (Tok.isNot(AsmToken::Identifier)) {
    OnFailure();
    if (isParsingIntelSyntax()) return true;
    return Error(StartLoc, "invalid register name",
                 SMRange(StartLoc, EndLoc));
  }

  if (MatchRegisterByName(RegNo, Tok.getString(), StartLoc, EndLoc)) {
    OnFailure();
    return true;
  }

  // Parse "%st" as "%st(0)" and "%st(1)", which is multiple tokens.
  if (RegNo == X86::ST0) {
    Tokens.push_back(Tok);
    Parser.Lex(); // Eat 'st'

    // Check to see if we have '(4)' after %st.
    if (Lexer.isNot(AsmToken::LParen))
      return false;
    // Lex the paren.
    Tokens.push_back(Parser.getTok());
    Parser.Lex();

    const AsmToken &IntTok = Parser.getTok();
    if (IntTok.isNot(AsmToken::Integer)) {
      OnFailure();
      return Error(IntTok.getLoc(), "expected stack index");
    }
    switch (IntTok.getIntVal()) {
    case 0: RegNo = X86::ST0; break;
    case 1: RegNo = X86::ST1; break;
    case 2: RegNo = X86::ST2; break;
    case 3: RegNo = X86::ST3; break;
    case 4: RegNo = X86::ST4; break;
    case 5: RegNo = X86::ST5; break;
    case 6: RegNo = X86::ST6; break;
    case 7: RegNo = X86::ST7; break;
    default:
      OnFailure();
      return Error(IntTok.getLoc(), "invalid stack index");
    }

    // Lex IntTok
    Tokens.push_back(IntTok);
    Parser.Lex();
    if (Lexer.isNot(AsmToken::RParen)) {
      OnFailure();
      return Error(Parser.getTok().getLoc(), "expected ')'");
    }

    EndLoc = Parser.getTok().getEndLoc();
    Parser.Lex(); // Eat ')'
    return false;
  }

  EndLoc = Parser.getTok().getEndLoc();

  if (RegNo == 0) {
    OnFailure();
    if (isParsingIntelSyntax()) return true;
    return Error(StartLoc, "invalid register name",
                 SMRange(StartLoc, EndLoc));
  }

  Parser.Lex(); // Eat identifier token.
  return false;
}

bool X86AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                 SMLoc &EndLoc) {
  return ParseRegister(Reg, StartLoc, EndLoc, /*RestoreOnFailure=*/false);
}

ParseStatus X86AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
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

std::unique_ptr<X86Operand> X86AsmParser::DefaultMemSIOperand(SMLoc Loc) {
  bool Parse32 = is32BitMode() || Code16GCC;
  unsigned Basereg = is64BitMode() ? X86::RSI : (Parse32 ? X86::ESI : X86::SI);
  const MCExpr *Disp = MCConstantExpr::create(0, getContext());
  return X86Operand::CreateMem(getPointerWidth(), /*SegReg=*/0, Disp,
                               /*BaseReg=*/Basereg, /*IndexReg=*/0, /*Scale=*/1,
                               Loc, Loc, 0);
}

std::unique_ptr<X86Operand> X86AsmParser::DefaultMemDIOperand(SMLoc Loc) {
  bool Parse32 = is32BitMode() || Code16GCC;
  unsigned Basereg = is64BitMode() ? X86::RDI : (Parse32 ? X86::EDI : X86::DI);
  const MCExpr *Disp = MCConstantExpr::create(0, getContext());
  return X86Operand::CreateMem(getPointerWidth(), /*SegReg=*/0, Disp,
                               /*BaseReg=*/Basereg, /*IndexReg=*/0, /*Scale=*/1,
                               Loc, Loc, 0);
}

bool X86AsmParser::IsSIReg(unsigned Reg) {
  switch (Reg) {
  default: llvm_unreachable("Only (R|E)SI and (R|E)DI are expected!");
  case X86::RSI:
  case X86::ESI:
  case X86::SI:
    return true;
  case X86::RDI:
  case X86::EDI:
  case X86::DI:
    return false;
  }
}

unsigned X86AsmParser::GetSIDIForRegClass(unsigned RegClassID, unsigned Reg,
                                          bool IsSIReg) {
  switch (RegClassID) {
  default: llvm_unreachable("Unexpected register class");
  case X86::GR64RegClassID:
    return IsSIReg ? X86::RSI : X86::RDI;
  case X86::GR32RegClassID:
    return IsSIReg ? X86::ESI : X86::EDI;
  case X86::GR16RegClassID:
    return IsSIReg ? X86::SI : X86::DI;
  }
}

void X86AsmParser::AddDefaultSrcDestOperands(
    OperandVector& Operands, std::unique_ptr<llvm::MCParsedAsmOperand> &&Src,
    std::unique_ptr<llvm::MCParsedAsmOperand> &&Dst) {
  if (isParsingIntelSyntax()) {
    Operands.push_back(std::move(Dst));
    Operands.push_back(std::move(Src));
  }
  else {
    Operands.push_back(std::move(Src));
    Operands.push_back(std::move(Dst));
  }
}

bool X86AsmParser::VerifyAndAdjustOperands(OperandVector &OrigOperands,
                                           OperandVector &FinalOperands) {

  if (OrigOperands.size() > 1) {
    // Check if sizes match, OrigOperands also contains the instruction name
    assert(OrigOperands.size() == FinalOperands.size() + 1 &&
           "Operand size mismatch");

    SmallVector<std::pair<SMLoc, std::string>, 2> Warnings;
    // Verify types match
    int RegClassID = -1;
    for (unsigned int i = 0; i < FinalOperands.size(); ++i) {
      X86Operand &OrigOp = static_cast<X86Operand &>(*OrigOperands[i + 1]);
      X86Operand &FinalOp = static_cast<X86Operand &>(*FinalOperands[i]);

      if (FinalOp.isReg() &&
          (!OrigOp.isReg() || FinalOp.getReg() != OrigOp.getReg()))
        // Return false and let a normal complaint about bogus operands happen
        return false;

      if (FinalOp.isMem()) {

        if (!OrigOp.isMem())
          // Return false and let a normal complaint about bogus operands happen
          return false;

        unsigned OrigReg = OrigOp.Mem.BaseReg;
        unsigned FinalReg = FinalOp.Mem.BaseReg;

        // If we've already encounterd a register class, make sure all register
        // bases are of the same register class
        if (RegClassID != -1 &&
            !X86MCRegisterClasses[RegClassID].contains(OrigReg)) {
          return Error(OrigOp.getStartLoc(),
                       "mismatching source and destination index registers");
        }

        if (X86MCRegisterClasses[X86::GR64RegClassID].contains(OrigReg))
          RegClassID = X86::GR64RegClassID;
        else if (X86MCRegisterClasses[X86::GR32RegClassID].contains(OrigReg))
          RegClassID = X86::GR32RegClassID;
        else if (X86MCRegisterClasses[X86::GR16RegClassID].contains(OrigReg))
          RegClassID = X86::GR16RegClassID;
        else
          // Unexpected register class type
          // Return false and let a normal complaint about bogus operands happen
          return false;

        bool IsSI = IsSIReg(FinalReg);
        FinalReg = GetSIDIForRegClass(RegClassID, FinalReg, IsSI);

        if (FinalReg != OrigReg) {
          std::string RegName = IsSI ? "ES:(R|E)SI" : "ES:(R|E)DI";
          Warnings.push_back(std::make_pair(
              OrigOp.getStartLoc(),
              "memory operand is only for determining the size, " + RegName +
                  " will be used for the location"));
        }

        FinalOp.Mem.Size = OrigOp.Mem.Size;
        FinalOp.Mem.SegReg = OrigOp.Mem.SegReg;
        FinalOp.Mem.BaseReg = FinalReg;
      }
    }

    // Produce warnings only if all the operands passed the adjustment - prevent
    // legal cases like "movsd (%rax), %xmm0" mistakenly produce warnings
    for (auto &WarningMsg : Warnings) {
      Warning(WarningMsg.first, WarningMsg.second);
    }

    // Remove old operands
    for (unsigned int i = 0; i < FinalOperands.size(); ++i)
      OrigOperands.pop_back();
  }
  // OrigOperands.append(FinalOperands.begin(), FinalOperands.end());
  for (auto &Op : FinalOperands)
    OrigOperands.push_back(std::move(Op));

  return false;
}

bool X86AsmParser::parseOperand(OperandVector &Operands, StringRef Name) {
  if (isParsingIntelSyntax())
    return parseIntelOperand(Operands, Name);

  return parseATTOperand(Operands);
}

bool X86AsmParser::CreateMemForMSInlineAsm(unsigned SegReg, const MCExpr *Disp,
                                           unsigned BaseReg, unsigned IndexReg,
                                           unsigned Scale, bool NonAbsMem,
                                           SMLoc Start, SMLoc End,
                                           unsigned Size, StringRef Identifier,
                                           const InlineAsmIdentifierInfo &Info,
                                           OperandVector &Operands) {
  // If we found a decl other than a VarDecl, then assume it is a FuncDecl or
  // some other label reference.
  if (Info.isKind(InlineAsmIdentifierInfo::IK_Label)) {
    // Create an absolute memory reference in order to match against
    // instructions taking a PC relative operand.
    Operands.push_back(X86Operand::CreateMem(getPointerWidth(), Disp, Start,
                                             End, Size, Identifier,
                                             Info.Label.Decl));
    return false;
  }
  // We either have a direct symbol reference, or an offset from a symbol.  The
  // parser always puts the symbol on the LHS, so look there for size
  // calculation purposes.
  unsigned FrontendSize = 0;
  void *Decl = nullptr;
  bool IsGlobalLV = false;
  if (Info.isKind(InlineAsmIdentifierInfo::IK_Var)) {
    // Size is in terms of bits in this context.
    FrontendSize = Info.Var.Type * 8;
    Decl = Info.Var.Decl;
    IsGlobalLV = Info.Var.IsGlobalLV;
  }
  // It is widely common for MS InlineAsm to use a global variable and one/two
  // registers in a mmory expression, and though unaccessible via rip/eip.
  if (IsGlobalLV) {
    if (BaseReg || IndexReg) {
      Operands.push_back(X86Operand::CreateMem(getPointerWidth(), Disp, Start,
                                               End, Size, Identifier, Decl, 0,
                                               BaseReg && IndexReg));
      return false;
    }
    if (NonAbsMem)
      BaseReg = 1; // Make isAbsMem() false
  }
  Operands.push_back(X86Operand::CreateMem(
      getPointerWidth(), SegReg, Disp, BaseReg, IndexReg, Scale, Start, End,
      Size,
      /*DefaultBaseReg=*/X86::RIP, Identifier, Decl, FrontendSize));
  return false;
}

// Some binary bitwise operators have a named synonymous
// Query a candidate string for being such a named operator
// and if so - invoke the appropriate handler
bool X86AsmParser::ParseIntelNamedOperator(StringRef Name,
                                           IntelExprStateMachine &SM,
                                           bool &ParseError, SMLoc &End) {
  // A named operator should be either lower or upper case, but not a mix...
  // except in MASM, which uses full case-insensitivity.
  if (Name != Name.lower() && Name != Name.upper() &&
      !getParser().isParsingMasm())
    return false;
  if (Name.equals_insensitive("not")) {
    SM.onNot();
  } else if (Name.equals_insensitive("or")) {
    SM.onOr();
  } else if (Name.equals_insensitive("shl")) {
    SM.onLShift();
  } else if (Name.equals_insensitive("shr")) {
    SM.onRShift();
  } else if (Name.equals_insensitive("xor")) {
    SM.onXor();
  } else if (Name.equals_insensitive("and")) {
    SM.onAnd();
  } else if (Name.equals_insensitive("mod")) {
    SM.onMod();
  } else if (Name.equals_insensitive("offset")) {
    SMLoc OffsetLoc = getTok().getLoc();
    const MCExpr *Val = nullptr;
    StringRef ID;
    InlineAsmIdentifierInfo Info;
    ParseError = ParseIntelOffsetOperator(Val, ID, Info, End);
    if (ParseError)
      return true;
    StringRef ErrMsg;
    ParseError =
        SM.onOffset(Val, OffsetLoc, ID, Info, isParsingMSInlineAsm(), ErrMsg);
    if (ParseError)
      return Error(SMLoc::getFromPointer(Name.data()), ErrMsg);
  } else {
    return false;
  }
  if (!Name.equals_insensitive("offset"))
    End = consumeToken();
  return true;
}
bool X86AsmParser::ParseMasmNamedOperator(StringRef Name,
                                          IntelExprStateMachine &SM,
                                          bool &ParseError, SMLoc &End) {
  if (Name.equals_insensitive("eq")) {
    SM.onEq();
  } else if (Name.equals_insensitive("ne")) {
    SM.onNE();
  } else if (Name.equals_insensitive("lt")) {
    SM.onLT();
  } else if (Name.equals_insensitive("le")) {
    SM.onLE();
  } else if (Name.equals_insensitive("gt")) {
    SM.onGT();
  } else if (Name.equals_insensitive("ge")) {
    SM.onGE();
  } else {
    return false;
  }
  End = consumeToken();
  return true;
}

// Check if current intel expression append after an operand.
// Like: [Operand][Intel Expression]
void X86AsmParser::tryParseOperandIdx(AsmToken::TokenKind PrevTK,
                                      IntelExprStateMachine &SM) {
  if (PrevTK != AsmToken::RBrac)
    return;

  SM.setAppendAfterOperand();
}

bool X86AsmParser::ParseIntelExpression(IntelExprStateMachine &SM, SMLoc &End) {
  MCAsmParser &Parser = getParser();
  StringRef ErrMsg;

  AsmToken::TokenKind PrevTK = AsmToken::Error;

  if (getContext().getObjectFileInfo()->isPositionIndependent())
    SM.setPIC();

  bool Done = false;
  while (!Done) {
    // Get a fresh reference on each loop iteration in case the previous
    // iteration moved the token storage during UnLex().
    const AsmToken &Tok = Parser.getTok();

    bool UpdateLocLex = true;
    AsmToken::TokenKind TK = getLexer().getKind();

    switch (TK) {
    default:
      if ((Done = SM.isValidEndState()))
        break;
      return Error(Tok.getLoc(), "unknown token in expression");
    case AsmToken::Error:
      return Error(getLexer().getErrLoc(), getLexer().getErr());
      break;
    case AsmToken::Real:
      // DotOperator: [ebx].0
      UpdateLocLex = false;
      if (ParseIntelDotOperator(SM, End))
        return true;
      break;
    case AsmToken::Dot:
      if (!Parser.isParsingMasm()) {
        if ((Done = SM.isValidEndState()))
          break;
        return Error(Tok.getLoc(), "unknown token in expression");
      }
      // MASM allows spaces around the dot operator (e.g., "var . x")
      Lex();
      UpdateLocLex = false;
      if (ParseIntelDotOperator(SM, End))
        return true;
      break;
    case AsmToken::Dollar:
      if (!Parser.isParsingMasm()) {
        if ((Done = SM.isValidEndState()))
          break;
        return Error(Tok.getLoc(), "unknown token in expression");
      }
      [[fallthrough]];
    case AsmToken::String: {
      if (Parser.isParsingMasm()) {
        // MASM parsers handle strings in expressions as constants.
        SMLoc ValueLoc = Tok.getLoc();
        int64_t Res;
        const MCExpr *Val;
        if (Parser.parsePrimaryExpr(Val, End, nullptr))
          return true;
        UpdateLocLex = false;
        if (!Val->evaluateAsAbsolute(Res, getStreamer().getAssemblerPtr()))
          return Error(ValueLoc, "expected absolute value");
        if (SM.onInteger(Res, ErrMsg))
          return Error(ValueLoc, ErrMsg);
        break;
      }
      [[fallthrough]];
    }
    case AsmToken::At:
    case AsmToken::Identifier: {
      SMLoc IdentLoc = Tok.getLoc();
      StringRef Identifier = Tok.getString();
      UpdateLocLex = false;
      if (Parser.isParsingMasm()) {
        size_t DotOffset = Identifier.find_first_of('.');
        if (DotOffset != StringRef::npos) {
          consumeToken();
          StringRef LHS = Identifier.slice(0, DotOffset);
          StringRef Dot = Identifier.slice(DotOffset, DotOffset + 1);
          StringRef RHS = Identifier.slice(DotOffset + 1, StringRef::npos);
          if (!RHS.empty()) {
            getLexer().UnLex(AsmToken(AsmToken::Identifier, RHS));
          }
          getLexer().UnLex(AsmToken(AsmToken::Dot, Dot));
          if (!LHS.empty()) {
            getLexer().UnLex(AsmToken(AsmToken::Identifier, LHS));
          }
          break;
        }
      }
      // (MASM only) <TYPE> PTR operator
      if (Parser.isParsingMasm()) {
        const AsmToken &NextTok = getLexer().peekTok();
        if (NextTok.is(AsmToken::Identifier) &&
            NextTok.getIdentifier().equals_insensitive("ptr")) {
          AsmTypeInfo Info;
          if (Parser.lookUpType(Identifier, Info))
            return Error(Tok.getLoc(), "unknown type");
          SM.onCast(Info);
          // Eat type and PTR.
          consumeToken();
          End = consumeToken();
          break;
        }
      }
      // Register, or (MASM only) <register>.<field>
      MCRegister Reg;
      if (Tok.is(AsmToken::Identifier)) {
        if (!ParseRegister(Reg, IdentLoc, End, /*RestoreOnFailure=*/true)) {
          if (SM.onRegister(Reg, ErrMsg))
            return Error(IdentLoc, ErrMsg);
          break;
        }
        if (Parser.isParsingMasm()) {
          const std::pair<StringRef, StringRef> IDField =
              Tok.getString().split('.');
          const StringRef ID = IDField.first, Field = IDField.second;
          SMLoc IDEndLoc = SMLoc::getFromPointer(ID.data() + ID.size());
          if (!Field.empty() &&
              !MatchRegisterByName(Reg, ID, IdentLoc, IDEndLoc)) {
            if (SM.onRegister(Reg, ErrMsg))
              return Error(IdentLoc, ErrMsg);

            AsmFieldInfo Info;
            SMLoc FieldStartLoc = SMLoc::getFromPointer(Field.data());
            if (Parser.lookUpField(Field, Info))
              return Error(FieldStartLoc, "unknown offset");
            else if (SM.onPlus(ErrMsg))
              return Error(getTok().getLoc(), ErrMsg);
            else if (SM.onInteger(Info.Offset, ErrMsg))
              return Error(IdentLoc, ErrMsg);
            SM.setTypeInfo(Info.Type);

            End = consumeToken();
            break;
          }
        }
      }
      // Operator synonymous ("not", "or" etc.)
      bool ParseError = false;
      if (ParseIntelNamedOperator(Identifier, SM, ParseError, End)) {
        if (ParseError)
          return true;
        break;
      }
      if (Parser.isParsingMasm() &&
          ParseMasmNamedOperator(Identifier, SM, ParseError, End)) {
        if (ParseError)
          return true;
        break;
      }
      // Symbol reference, when parsing assembly content
      InlineAsmIdentifierInfo Info;
      AsmFieldInfo FieldInfo;
      const MCExpr *Val;
      if (isParsingMSInlineAsm() || Parser.isParsingMasm()) {
        // MS Dot Operator expression
        if (Identifier.count('.') &&
            (PrevTK == AsmToken::RBrac || PrevTK == AsmToken::RParen)) {
          if (ParseIntelDotOperator(SM, End))
            return true;
          break;
        }
      }
      if (isParsingMSInlineAsm()) {
        // MS InlineAsm operators (TYPE/LENGTH/SIZE)
        if (unsigned OpKind = IdentifyIntelInlineAsmOperator(Identifier)) {
          if (int64_t Val = ParseIntelInlineAsmOperator(OpKind)) {
            if (SM.onInteger(Val, ErrMsg))
              return Error(IdentLoc, ErrMsg);
          } else {
            return true;
          }
          break;
        }
        // MS InlineAsm identifier
        // Call parseIdentifier() to combine @ with the identifier behind it.
        if (TK == AsmToken::At && Parser.parseIdentifier(Identifier))
          return Error(IdentLoc, "expected identifier");
        if (ParseIntelInlineAsmIdentifier(Val, Identifier, Info, false, End))
          return true;
        else if (SM.onIdentifierExpr(Val, Identifier, Info, FieldInfo.Type,
                                     true, ErrMsg))
          return Error(IdentLoc, ErrMsg);
        break;
      }
      if (Parser.isParsingMasm()) {
        if (unsigned OpKind = IdentifyMasmOperator(Identifier)) {
          int64_t Val;
          if (ParseMasmOperator(OpKind, Val))
            return true;
          if (SM.onInteger(Val, ErrMsg))
            return Error(IdentLoc, ErrMsg);
          break;
        }
        if (!getParser().lookUpType(Identifier, FieldInfo.Type)) {
          // Field offset immediate; <TYPE>.<field specification>
          Lex(); // eat type
          bool EndDot = parseOptionalToken(AsmToken::Dot);
          while (EndDot || (getTok().is(AsmToken::Identifier) &&
                            getTok().getString().starts_with("."))) {
            getParser().parseIdentifier(Identifier);
            if (!EndDot)
              Identifier.consume_front(".");
            EndDot = Identifier.consume_back(".");
            if (getParser().lookUpField(FieldInfo.Type.Name, Identifier,
                                        FieldInfo)) {
              SMLoc IDEnd =
                  SMLoc::getFromPointer(Identifier.data() + Identifier.size());
              return Error(IdentLoc, "Unable to lookup field reference!",
                           SMRange(IdentLoc, IDEnd));
            }
            if (!EndDot)
              EndDot = parseOptionalToken(AsmToken::Dot);
          }
          if (SM.onInteger(FieldInfo.Offset, ErrMsg))
            return Error(IdentLoc, ErrMsg);
          break;
        }
      }
      if (getParser().parsePrimaryExpr(Val, End, &FieldInfo.Type)) {
        return Error(Tok.getLoc(), "Unexpected identifier!");
      } else if (SM.onIdentifierExpr(Val, Identifier, Info, FieldInfo.Type,
                                     false, ErrMsg)) {
        return Error(IdentLoc, ErrMsg);
      }
      break;
    }
    case AsmToken::Integer: {
      // Look for 'b' or 'f' following an Integer as a directional label
      SMLoc Loc = getTok().getLoc();
      int64_t IntVal = getTok().getIntVal();
      End = consumeToken();
      UpdateLocLex = false;
      if (getLexer().getKind() == AsmToken::Identifier) {
        StringRef IDVal = getTok().getString();
        if (IDVal == "f" || IDVal == "b") {
          MCSymbol *Sym =
              getContext().getDirectionalLocalSymbol(IntVal, IDVal == "b");
          MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
          const MCExpr *Val =
              MCSymbolRefExpr::create(Sym, Variant, getContext());
          if (IDVal == "b" && Sym->isUndefined())
            return Error(Loc, "invalid reference to undefined symbol");
          StringRef Identifier = Sym->getName();
          InlineAsmIdentifierInfo Info;
          AsmTypeInfo Type;
          if (SM.onIdentifierExpr(Val, Identifier, Info, Type,
                                  isParsingMSInlineAsm(), ErrMsg))
            return Error(Loc, ErrMsg);
          End = consumeToken();
        } else {
          if (SM.onInteger(IntVal, ErrMsg))
            return Error(Loc, ErrMsg);
        }
      } else {
        if (SM.onInteger(IntVal, ErrMsg))
          return Error(Loc, ErrMsg);
      }
      break;
    }
    case AsmToken::Plus:
      if (SM.onPlus(ErrMsg))
        return Error(getTok().getLoc(), ErrMsg);
      break;
    case AsmToken::Minus:
      if (SM.onMinus(ErrMsg))
        return Error(getTok().getLoc(), ErrMsg);
      break;
    case AsmToken::Tilde:   SM.onNot(); break;
    case AsmToken::Star:    SM.onStar(); break;
    case AsmToken::Slash:   SM.onDivide(); break;
    case AsmToken::Percent: SM.onMod(); break;
    case AsmToken::Pipe:    SM.onOr(); break;
    case AsmToken::Caret:   SM.onXor(); break;
    case AsmToken::Amp:     SM.onAnd(); break;
    case AsmToken::LessLess:
                            SM.onLShift(); break;
    case AsmToken::GreaterGreater:
                            SM.onRShift(); break;
    case AsmToken::LBrac:
      if (SM.onLBrac())
        return Error(Tok.getLoc(), "unexpected bracket encountered");
      tryParseOperandIdx(PrevTK, SM);
      break;
    case AsmToken::RBrac:
      if (SM.onRBrac(ErrMsg)) {
        return Error(Tok.getLoc(), ErrMsg);
      }
      break;
    case AsmToken::LParen:  SM.onLParen(); break;
    case AsmToken::RParen:  SM.onRParen(); break;
    }
    if (SM.hadError())
      return Error(Tok.getLoc(), "unknown token in expression");

    if (!Done && UpdateLocLex)
      End = consumeToken();

    PrevTK = TK;
  }
  return false;
}

void X86AsmParser::RewriteIntelExpression(IntelExprStateMachine &SM,
                                          SMLoc Start, SMLoc End) {
  SMLoc Loc = Start;
  unsigned ExprLen = End.getPointer() - Start.getPointer();
  // Skip everything before a symbol displacement (if we have one)
  if (SM.getSym() && !SM.isOffsetOperator()) {
    StringRef SymName = SM.getSymName();
    if (unsigned Len = SymName.data() - Start.getPointer())
      InstInfo->AsmRewrites->emplace_back(AOK_Skip, Start, Len);
    Loc = SMLoc::getFromPointer(SymName.data() + SymName.size());
    ExprLen = End.getPointer() - (SymName.data() + SymName.size());
    // If we have only a symbol than there's no need for complex rewrite,
    // simply skip everything after it
    if (!(SM.getBaseReg() || SM.getIndexReg() || SM.getImm())) {
      if (ExprLen)
        InstInfo->AsmRewrites->emplace_back(AOK_Skip, Loc, ExprLen);
      return;
    }
  }
  // Build an Intel Expression rewrite
  StringRef BaseRegStr;
  StringRef IndexRegStr;
  StringRef OffsetNameStr;
  if (SM.getBaseReg())
    BaseRegStr = X86IntelInstPrinter::getRegisterName(SM.getBaseReg());
  if (SM.getIndexReg())
    IndexRegStr = X86IntelInstPrinter::getRegisterName(SM.getIndexReg());
  if (SM.isOffsetOperator())
    OffsetNameStr = SM.getSymName();
  // Emit it
  IntelExpr Expr(BaseRegStr, IndexRegStr, SM.getScale(), OffsetNameStr,
                 SM.getImm(), SM.isMemExpr());
  InstInfo->AsmRewrites->emplace_back(Loc, ExprLen, Expr);
}

// Inline assembly may use variable names with namespace alias qualifiers.
bool X86AsmParser::ParseIntelInlineAsmIdentifier(
    const MCExpr *&Val, StringRef &Identifier, InlineAsmIdentifierInfo &Info,
    bool IsUnevaluatedOperand, SMLoc &End, bool IsParsingOffsetOperator) {
  MCAsmParser &Parser = getParser();
  assert(isParsingMSInlineAsm() && "Expected to be parsing inline assembly.");
  Val = nullptr;

  StringRef LineBuf(Identifier.data());
  SemaCallback->LookupInlineAsmIdentifier(LineBuf, Info, IsUnevaluatedOperand);

  const AsmToken &Tok = Parser.getTok();
  SMLoc Loc = Tok.getLoc();

  // Advance the token stream until the end of the current token is
  // after the end of what the frontend claimed.
  const char *EndPtr = Tok.getLoc().getPointer() + LineBuf.size();
  do {
    End = Tok.getEndLoc();
    getLexer().Lex();
  } while (End.getPointer() < EndPtr);
  Identifier = LineBuf;

  // The frontend should end parsing on an assembler token boundary, unless it
  // failed parsing.
  assert((End.getPointer() == EndPtr ||
          Info.isKind(InlineAsmIdentifierInfo::IK_Invalid)) &&
          "frontend claimed part of a token?");

  // If the identifier lookup was unsuccessful, assume that we are dealing with
  // a label.
  if (Info.isKind(InlineAsmIdentifierInfo::IK_Invalid)) {
    StringRef InternalName =
      SemaCallback->LookupInlineAsmLabel(Identifier, getSourceManager(),
                                         Loc, false);
    assert(InternalName.size() && "We should have an internal name here.");
    // Push a rewrite for replacing the identifier name with the internal name,
    // unless we are parsing the operand of an offset operator
    if (!IsParsingOffsetOperator)
      InstInfo->AsmRewrites->emplace_back(AOK_Label, Loc, Identifier.size(),
                                          InternalName);
    else
      Identifier = InternalName;
  } else if (Info.isKind(InlineAsmIdentifierInfo::IK_EnumVal))
    return false;
  // Create the symbol reference.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Identifier);
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  Val = MCSymbolRefExpr::create(Sym, Variant, getParser().getContext());
  return false;
}

//ParseRoundingModeOp - Parse AVX-512 rounding mode operand
bool X86AsmParser::ParseRoundingModeOp(SMLoc Start, OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  // Eat "{" and mark the current place.
  const SMLoc consumedToken = consumeToken();
  if (Tok.isNot(AsmToken::Identifier))
    return Error(Tok.getLoc(), "Expected an identifier after {");
  if (Tok.getIdentifier().starts_with("r")) {
    int rndMode = StringSwitch<int>(Tok.getIdentifier())
      .Case("rn", X86::STATIC_ROUNDING::TO_NEAREST_INT)
      .Case("rd", X86::STATIC_ROUNDING::TO_NEG_INF)
      .Case("ru", X86::STATIC_ROUNDING::TO_POS_INF)
      .Case("rz", X86::STATIC_ROUNDING::TO_ZERO)
      .Default(-1);
    if (-1 == rndMode)
      return Error(Tok.getLoc(), "Invalid rounding mode.");
     Parser.Lex();  // Eat "r*" of r*-sae
    if (!getLexer().is(AsmToken::Minus))
      return Error(Tok.getLoc(), "Expected - at this point");
    Parser.Lex();  // Eat "-"
    Parser.Lex();  // Eat the sae
    if (!getLexer().is(AsmToken::RCurly))
      return Error(Tok.getLoc(), "Expected } at this point");
    SMLoc End = Tok.getEndLoc();
    Parser.Lex();  // Eat "}"
    const MCExpr *RndModeOp =
      MCConstantExpr::create(rndMode, Parser.getContext());
    Operands.push_back(X86Operand::CreateImm(RndModeOp, Start, End));
    return false;
  }
  if (Tok.getIdentifier() == "sae") {
    Parser.Lex();  // Eat the sae
    if (!getLexer().is(AsmToken::RCurly))
      return Error(Tok.getLoc(), "Expected } at this point");
    Parser.Lex();  // Eat "}"
    Operands.push_back(X86Operand::CreateToken("{sae}", consumedToken));
    return false;
  }
  return Error(Tok.getLoc(), "unknown token in expression");
}

/// Parse condtional flags for CCMP/CTEST, e.g {dfv=of,sf,zf,cf} right after
/// mnemonic.
bool X86AsmParser::parseCFlagsOp(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  AsmToken Tok = Parser.getTok();
  const SMLoc Start = Tok.getLoc();
  if (!Tok.is(AsmToken::LCurly))
    return Error(Tok.getLoc(), "Expected { at this point");
  Parser.Lex(); // Eat "{"
  Tok = Parser.getTok();
  if (Tok.getIdentifier().lower() != "dfv")
    return Error(Tok.getLoc(), "Expected dfv at this point");
  Parser.Lex(); // Eat "dfv"
  Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Equal))
    return Error(Tok.getLoc(), "Expected = at this point");
  Parser.Lex(); // Eat "="

  Tok = Parser.getTok();
  SMLoc End;
  if (Tok.is(AsmToken::RCurly)) {
    End = Tok.getEndLoc();
    Operands.push_back(X86Operand::CreateImm(
        MCConstantExpr::create(0, Parser.getContext()), Start, End));
    Parser.Lex(); // Eat "}"
    return false;
  }
  unsigned CFlags = 0;
  for (unsigned I = 0; I < 4; ++I) {
    Tok = Parser.getTok();
    unsigned CFlag = StringSwitch<unsigned>(Tok.getIdentifier().lower())
                         .Case("of", 0x8)
                         .Case("sf", 0x4)
                         .Case("zf", 0x2)
                         .Case("cf", 0x1)
                         .Default(~0U);
    if (CFlag == ~0U)
      return Error(Tok.getLoc(), "Invalid conditional flags");

    if (CFlags & CFlag)
      return Error(Tok.getLoc(), "Duplicated conditional flag");
    CFlags |= CFlag;

    Parser.Lex(); // Eat one conditional flag
    Tok = Parser.getTok();
    if (Tok.is(AsmToken::RCurly)) {
      End = Tok.getEndLoc();
      Operands.push_back(X86Operand::CreateImm(
          MCConstantExpr::create(CFlags, Parser.getContext()), Start, End));
      Parser.Lex(); // Eat "}"
      return false;
    } else if (I == 3) {
      return Error(Tok.getLoc(), "Expected } at this point");
    } else if (Tok.isNot(AsmToken::Comma)) {
      return Error(Tok.getLoc(), "Expected } or , at this point");
    }
    Parser.Lex(); // Eat ","
  }
  llvm_unreachable("Unexpected control flow");
}

/// Parse the '.' operator.
bool X86AsmParser::ParseIntelDotOperator(IntelExprStateMachine &SM,
                                         SMLoc &End) {
  const AsmToken &Tok = getTok();
  AsmFieldInfo Info;

  // Drop the optional '.'.
  StringRef DotDispStr = Tok.getString();
  DotDispStr.consume_front(".");
  StringRef TrailingDot;

  // .Imm gets lexed as a real.
  if (Tok.is(AsmToken::Real)) {
    APInt DotDisp;
    if (DotDispStr.getAsInteger(10, DotDisp))
      return Error(Tok.getLoc(), "Unexpected offset");
    Info.Offset = DotDisp.getZExtValue();
  } else if ((isParsingMSInlineAsm() || getParser().isParsingMasm()) &&
             Tok.is(AsmToken::Identifier)) {
    if (DotDispStr.ends_with(".")) {
      TrailingDot = DotDispStr.substr(DotDispStr.size() - 1);
      DotDispStr = DotDispStr.drop_back(1);
    }
    const std::pair<StringRef, StringRef> BaseMember = DotDispStr.split('.');
    const StringRef Base = BaseMember.first, Member = BaseMember.second;
    if (getParser().lookUpField(SM.getType(), DotDispStr, Info) &&
        getParser().lookUpField(SM.getSymName(), DotDispStr, Info) &&
        getParser().lookUpField(DotDispStr, Info) &&
        (!SemaCallback ||
         SemaCallback->LookupInlineAsmField(Base, Member, Info.Offset)))
      return Error(Tok.getLoc(), "Unable to lookup field reference!");
  } else {
    return Error(Tok.getLoc(), "Unexpected token type!");
  }

  // Eat the DotExpression and update End
  End = SMLoc::getFromPointer(DotDispStr.data());
  const char *DotExprEndLoc = DotDispStr.data() + DotDispStr.size();
  while (Tok.getLoc().getPointer() < DotExprEndLoc)
    Lex();
  if (!TrailingDot.empty())
    getLexer().UnLex(AsmToken(AsmToken::Dot, TrailingDot));
  SM.addImm(Info.Offset);
  SM.setTypeInfo(Info.Type);
  return false;
}

/// Parse the 'offset' operator.
/// This operator is used to specify the location of a given operand
bool X86AsmParser::ParseIntelOffsetOperator(const MCExpr *&Val, StringRef &ID,
                                            InlineAsmIdentifierInfo &Info,
                                            SMLoc &End) {
  // Eat offset, mark start of identifier.
  SMLoc Start = Lex().getLoc();
  ID = getTok().getString();
  if (!isParsingMSInlineAsm()) {
    if ((getTok().isNot(AsmToken::Identifier) &&
         getTok().isNot(AsmToken::String)) ||
        getParser().parsePrimaryExpr(Val, End, nullptr))
      return Error(Start, "unexpected token!");
  } else if (ParseIntelInlineAsmIdentifier(Val, ID, Info, false, End, true)) {
    return Error(Start, "unable to lookup expression");
  } else if (Info.isKind(InlineAsmIdentifierInfo::IK_EnumVal)) {
    return Error(Start, "offset operator cannot yet handle constants");
  }
  return false;
}

// Query a candidate string for being an Intel assembly operator
// Report back its kind, or IOK_INVALID if does not evaluated as a known one
unsigned X86AsmParser::IdentifyIntelInlineAsmOperator(StringRef Name) {
  return StringSwitch<unsigned>(Name)
    .Cases("TYPE","type",IOK_TYPE)
    .Cases("SIZE","size",IOK_SIZE)
    .Cases("LENGTH","length",IOK_LENGTH)
    .Default(IOK_INVALID);
}

/// Parse the 'LENGTH', 'TYPE' and 'SIZE' operators.  The LENGTH operator
/// returns the number of elements in an array.  It returns the value 1 for
/// non-array variables.  The SIZE operator returns the size of a C or C++
/// variable.  A variable's size is the product of its LENGTH and TYPE.  The
/// TYPE operator returns the size of a C or C++ type or variable. If the
/// variable is an array, TYPE returns the size of a single element.
unsigned X86AsmParser::ParseIntelInlineAsmOperator(unsigned OpKind) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  Parser.Lex(); // Eat operator.

  const MCExpr *Val = nullptr;
  InlineAsmIdentifierInfo Info;
  SMLoc Start = Tok.getLoc(), End;
  StringRef Identifier = Tok.getString();
  if (ParseIntelInlineAsmIdentifier(Val, Identifier, Info,
                                    /*IsUnevaluatedOperand=*/true, End))
    return 0;

  if (!Info.isKind(InlineAsmIdentifierInfo::IK_Var)) {
    Error(Start, "unable to lookup expression");
    return 0;
  }

  unsigned CVal = 0;
  switch(OpKind) {
  default: llvm_unreachable("Unexpected operand kind!");
  case IOK_LENGTH: CVal = Info.Var.Length; break;
  case IOK_SIZE: CVal = Info.Var.Size; break;
  case IOK_TYPE: CVal = Info.Var.Type; break;
  }

  return CVal;
}

// Query a candidate string for being an Intel assembly operator
// Report back its kind, or IOK_INVALID if does not evaluated as a known one
unsigned X86AsmParser::IdentifyMasmOperator(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("type", MOK_TYPE)
      .Cases("size", "sizeof", MOK_SIZEOF)
      .Cases("length", "lengthof", MOK_LENGTHOF)
      .Default(MOK_INVALID);
}

/// Parse the 'LENGTHOF', 'SIZEOF', and 'TYPE' operators.  The LENGTHOF operator
/// returns the number of elements in an array.  It returns the value 1 for
/// non-array variables.  The SIZEOF operator returns the size of a type or
/// variable in bytes.  A variable's size is the product of its LENGTH and TYPE.
/// The TYPE operator returns the size of a variable. If the variable is an
/// array, TYPE returns the size of a single element.
bool X86AsmParser::ParseMasmOperator(unsigned OpKind, int64_t &Val) {
  MCAsmParser &Parser = getParser();
  SMLoc OpLoc = Parser.getTok().getLoc();
  Parser.Lex(); // Eat operator.

  Val = 0;
  if (OpKind == MOK_SIZEOF || OpKind == MOK_TYPE) {
    // Check for SIZEOF(<type>) and TYPE(<type>).
    bool InParens = Parser.getTok().is(AsmToken::LParen);
    const AsmToken &IDTok = InParens ? getLexer().peekTok() : Parser.getTok();
    AsmTypeInfo Type;
    if (IDTok.is(AsmToken::Identifier) &&
        !Parser.lookUpType(IDTok.getIdentifier(), Type)) {
      Val = Type.Size;

      // Eat tokens.
      if (InParens)
        parseToken(AsmToken::LParen);
      parseToken(AsmToken::Identifier);
      if (InParens)
        parseToken(AsmToken::RParen);
    }
  }

  if (!Val) {
    IntelExprStateMachine SM;
    SMLoc End, Start = Parser.getTok().getLoc();
    if (ParseIntelExpression(SM, End))
      return true;

    switch (OpKind) {
    default:
      llvm_unreachable("Unexpected operand kind!");
    case MOK_SIZEOF:
      Val = SM.getSize();
      break;
    case MOK_LENGTHOF:
      Val = SM.getLength();
      break;
    case MOK_TYPE:
      Val = SM.getElementSize();
      break;
    }

    if (!Val)
      return Error(OpLoc, "expression has unknown type", SMRange(Start, End));
  }

  return false;
}

bool X86AsmParser::ParseIntelMemoryOperandSize(unsigned &Size) {
  Size = StringSwitch<unsigned>(getTok().getString())
    .Cases("BYTE", "byte", 8)
    .Cases("WORD", "word", 16)
    .Cases("DWORD", "dword", 32)
    .Cases("FLOAT", "float", 32)
    .Cases("LONG", "long", 32)
    .Cases("FWORD", "fword", 48)
    .Cases("DOUBLE", "double", 64)
    .Cases("QWORD", "qword", 64)
    .Cases("MMWORD","mmword", 64)
    .Cases("XWORD", "xword", 80)
    .Cases("TBYTE", "tbyte", 80)
    .Cases("XMMWORD", "xmmword", 128)
    .Cases("YMMWORD", "ymmword", 256)
    .Cases("ZMMWORD", "zmmword", 512)
    .Default(0);
  if (Size) {
    const AsmToken &Tok = Lex(); // Eat operand size (e.g., byte, word).
    if (!(Tok.getString() == "PTR" || Tok.getString() == "ptr"))
      return Error(Tok.getLoc(), "Expected 'PTR' or 'ptr' token!");
    Lex(); // Eat ptr.
  }
  return false;
}

bool X86AsmParser::parseIntelOperand(OperandVector &Operands, StringRef Name) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc Start, End;

  // Parse optional Size directive.
  unsigned Size;
  if (ParseIntelMemoryOperandSize(Size))
    return true;
  bool PtrInOperand = bool(Size);

  Start = Tok.getLoc();

  // Rounding mode operand.
  if (getLexer().is(AsmToken::LCurly))
    return ParseRoundingModeOp(Start, Operands);

  // Register operand.
  MCRegister RegNo;
  if (Tok.is(AsmToken::Identifier) && !parseRegister(RegNo, Start, End)) {
    if (RegNo == X86::RIP)
      return Error(Start, "rip can only be used as a base register");
    // A Register followed by ':' is considered a segment override
    if (Tok.isNot(AsmToken::Colon)) {
      if (PtrInOperand)
        return Error(Start, "expected memory operand after 'ptr', "
                            "found register operand instead");
      Operands.push_back(X86Operand::CreateReg(RegNo, Start, End));
      return false;
    }
    // An alleged segment override. check if we have a valid segment register
    if (!X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(RegNo))
      return Error(Start, "invalid segment register");
    // Eat ':' and update Start location
    Start = Lex().getLoc();
  }

  // Immediates and Memory
  IntelExprStateMachine SM;
  if (ParseIntelExpression(SM, End))
    return true;

  if (isParsingMSInlineAsm())
    RewriteIntelExpression(SM, Start, Tok.getLoc());

  int64_t Imm = SM.getImm();
  const MCExpr *Disp = SM.getSym();
  const MCExpr *ImmDisp = MCConstantExpr::create(Imm, getContext());
  if (Disp && Imm)
    Disp = MCBinaryExpr::createAdd(Disp, ImmDisp, getContext());
  if (!Disp)
    Disp = ImmDisp;

  // RegNo != 0 specifies a valid segment register,
  // and we are parsing a segment override
  if (!SM.isMemExpr() && !RegNo) {
    if (isParsingMSInlineAsm() && SM.isOffsetOperator()) {
      const InlineAsmIdentifierInfo &Info = SM.getIdentifierInfo();
      if (Info.isKind(InlineAsmIdentifierInfo::IK_Var)) {
        // Disp includes the address of a variable; make sure this is recorded
        // for later handling.
        Operands.push_back(X86Operand::CreateImm(Disp, Start, End,
                                                 SM.getSymName(), Info.Var.Decl,
                                                 Info.Var.IsGlobalLV));
        return false;
      }
    }

    Operands.push_back(X86Operand::CreateImm(Disp, Start, End));
    return false;
  }

  StringRef ErrMsg;
  unsigned BaseReg = SM.getBaseReg();
  unsigned IndexReg = SM.getIndexReg();
  if (IndexReg && BaseReg == X86::RIP)
    BaseReg = 0;
  unsigned Scale = SM.getScale();
  if (!PtrInOperand)
    Size = SM.getElementSize() << 3;

  if (Scale == 0 && BaseReg != X86::ESP && BaseReg != X86::RSP &&
      (IndexReg == X86::ESP || IndexReg == X86::RSP))
    std::swap(BaseReg, IndexReg);

  // If BaseReg is a vector register and IndexReg is not, swap them unless
  // Scale was specified in which case it would be an error.
  if (Scale == 0 &&
      !(X86MCRegisterClasses[X86::VR128XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR256XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR512RegClassID].contains(IndexReg)) &&
      (X86MCRegisterClasses[X86::VR128XRegClassID].contains(BaseReg) ||
       X86MCRegisterClasses[X86::VR256XRegClassID].contains(BaseReg) ||
       X86MCRegisterClasses[X86::VR512RegClassID].contains(BaseReg)))
    std::swap(BaseReg, IndexReg);

  if (Scale != 0 &&
      X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg))
    return Error(Start, "16-bit addresses cannot have a scale");

  // If there was no explicit scale specified, change it to 1.
  if (Scale == 0)
    Scale = 1;

  // If this is a 16-bit addressing mode with the base and index in the wrong
  // order, swap them so CheckBaseRegAndIndexRegAndScale doesn't fail. It is
  // shared with att syntax where order matters.
  if ((BaseReg == X86::SI || BaseReg == X86::DI) &&
      (IndexReg == X86::BX || IndexReg == X86::BP))
    std::swap(BaseReg, IndexReg);

  if ((BaseReg || IndexReg) &&
      CheckBaseRegAndIndexRegAndScale(BaseReg, IndexReg, Scale, is64BitMode(),
                                      ErrMsg))
    return Error(Start, ErrMsg);
  bool IsUnconditionalBranch =
      Name.equals_insensitive("jmp") || Name.equals_insensitive("call");
  if (isParsingMSInlineAsm())
    return CreateMemForMSInlineAsm(RegNo, Disp, BaseReg, IndexReg, Scale,
                                   IsUnconditionalBranch && is64BitMode(),
                                   Start, End, Size, SM.getSymName(),
                                   SM.getIdentifierInfo(), Operands);

  // When parsing x64 MS-style assembly, all non-absolute references to a named
  // variable default to RIP-relative.
  unsigned DefaultBaseReg = X86::NoRegister;
  bool MaybeDirectBranchDest = true;

  if (Parser.isParsingMasm()) {
    if (is64BitMode() &&
        ((PtrInOperand && !IndexReg) || SM.getElementSize() > 0)) {
      DefaultBaseReg = X86::RIP;
    }
    if (IsUnconditionalBranch) {
      if (PtrInOperand) {
        MaybeDirectBranchDest = false;
        if (is64BitMode())
          DefaultBaseReg = X86::RIP;
      } else if (!BaseReg && !IndexReg && Disp &&
                 Disp->getKind() == MCExpr::SymbolRef) {
        if (is64BitMode()) {
          if (SM.getSize() == 8) {
            MaybeDirectBranchDest = false;
            DefaultBaseReg = X86::RIP;
          }
        } else {
          if (SM.getSize() == 4 || SM.getSize() == 2)
            MaybeDirectBranchDest = false;
        }
      }
    }
  } else if (IsUnconditionalBranch) {
    // Treat `call [offset fn_ref]` (or `jmp`) syntax as an error.
    if (!PtrInOperand && SM.isOffsetOperator())
      return Error(
          Start, "`OFFSET` operator cannot be used in an unconditional branch");
    if (PtrInOperand || SM.isBracketUsed())
      MaybeDirectBranchDest = false;
  }

  if ((BaseReg || IndexReg || RegNo || DefaultBaseReg != X86::NoRegister))
    Operands.push_back(X86Operand::CreateMem(
        getPointerWidth(), RegNo, Disp, BaseReg, IndexReg, Scale, Start, End,
        Size, DefaultBaseReg, /*SymName=*/StringRef(), /*OpDecl=*/nullptr,
        /*FrontendSize=*/0, /*UseUpRegs=*/false, MaybeDirectBranchDest));
  else
    Operands.push_back(X86Operand::CreateMem(
        getPointerWidth(), Disp, Start, End, Size, /*SymName=*/StringRef(),
        /*OpDecl=*/nullptr, /*FrontendSize=*/0, /*UseUpRegs=*/false,
        MaybeDirectBranchDest));
  return false;
}

bool X86AsmParser::parseATTOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  switch (getLexer().getKind()) {
  case AsmToken::Dollar: {
    // $42 or $ID -> immediate.
    SMLoc Start = Parser.getTok().getLoc(), End;
    Parser.Lex();
    const MCExpr *Val;
    // This is an immediate, so we should not parse a register. Do a precheck
    // for '%' to supercede intra-register parse errors.
    SMLoc L = Parser.getTok().getLoc();
    if (check(getLexer().is(AsmToken::Percent), L,
              "expected immediate expression") ||
        getParser().parseExpression(Val, End) ||
        check(isa<X86MCExpr>(Val), L, "expected immediate expression"))
      return true;
    Operands.push_back(X86Operand::CreateImm(Val, Start, End));
    return false;
  }
  case AsmToken::LCurly: {
    SMLoc Start = Parser.getTok().getLoc();
    return ParseRoundingModeOp(Start, Operands);
  }
  default: {
    // This a memory operand or a register. We have some parsing complications
    // as a '(' may be part of an immediate expression or the addressing mode
    // block. This is complicated by the fact that an assembler-level variable
    // may refer either to a register or an immediate expression.

    SMLoc Loc = Parser.getTok().getLoc(), EndLoc;
    const MCExpr *Expr = nullptr;
    unsigned Reg = 0;
    if (getLexer().isNot(AsmToken::LParen)) {
      // No '(' so this is either a displacement expression or a register.
      if (Parser.parseExpression(Expr, EndLoc))
        return true;
      if (auto *RE = dyn_cast<X86MCExpr>(Expr)) {
        // Segment Register. Reset Expr and copy value to register.
        Expr = nullptr;
        Reg = RE->getRegNo();

        // Check the register.
        if (Reg == X86::EIZ || Reg == X86::RIZ)
          return Error(
              Loc, "%eiz and %riz can only be used as index registers",
              SMRange(Loc, EndLoc));
        if (Reg == X86::RIP)
          return Error(Loc, "%rip can only be used as a base register",
                       SMRange(Loc, EndLoc));
        // Return register that are not segment prefixes immediately.
        if (!Parser.parseOptionalToken(AsmToken::Colon)) {
          Operands.push_back(X86Operand::CreateReg(Reg, Loc, EndLoc));
          return false;
        }
        if (!X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(Reg))
          return Error(Loc, "invalid segment register");
        // Accept a '*' absolute memory reference after the segment. Place it
        // before the full memory operand.
        if (getLexer().is(AsmToken::Star))
          Operands.push_back(X86Operand::CreateToken("*", consumeToken()));
      }
    }
    // This is a Memory operand.
    return ParseMemOperand(Reg, Expr, Loc, EndLoc, Operands);
  }
  }
}

// X86::COND_INVALID if not a recognized condition code or alternate mnemonic,
// otherwise the EFLAGS Condition Code enumerator.
X86::CondCode X86AsmParser::ParseConditionCode(StringRef CC) {
  return StringSwitch<X86::CondCode>(CC)
      .Case("o", X86::COND_O)          // Overflow
      .Case("no", X86::COND_NO)        // No Overflow
      .Cases("b", "nae", X86::COND_B)  // Below/Neither Above nor Equal
      .Cases("ae", "nb", X86::COND_AE) // Above or Equal/Not Below
      .Cases("e", "z", X86::COND_E)    // Equal/Zero
      .Cases("ne", "nz", X86::COND_NE) // Not Equal/Not Zero
      .Cases("be", "na", X86::COND_BE) // Below or Equal/Not Above
      .Cases("a", "nbe", X86::COND_A)  // Above/Neither Below nor Equal
      .Case("s", X86::COND_S)          // Sign
      .Case("ns", X86::COND_NS)        // No Sign
      .Cases("p", "pe", X86::COND_P)   // Parity/Parity Even
      .Cases("np", "po", X86::COND_NP) // No Parity/Parity Odd
      .Cases("l", "nge", X86::COND_L)  // Less/Neither Greater nor Equal
      .Cases("ge", "nl", X86::COND_GE) // Greater or Equal/Not Less
      .Cases("le", "ng", X86::COND_LE) // Less or Equal/Not Greater
      .Cases("g", "nle", X86::COND_G)  // Greater/Neither Less nor Equal
      .Default(X86::COND_INVALID);
}

// true on failure, false otherwise
// If no {z} mark was found - Parser doesn't advance
bool X86AsmParser::ParseZ(std::unique_ptr<X86Operand> &Z,
                          const SMLoc &StartLoc) {
  MCAsmParser &Parser = getParser();
  // Assuming we are just pass the '{' mark, quering the next token
  // Searched for {z}, but none was found. Return false, as no parsing error was
  // encountered
  if (!(getLexer().is(AsmToken::Identifier) &&
        (getLexer().getTok().getIdentifier() == "z")))
    return false;
  Parser.Lex(); // Eat z
  // Query and eat the '}' mark
  if (!getLexer().is(AsmToken::RCurly))
    return Error(getLexer().getLoc(), "Expected } at this point");
  Parser.Lex(); // Eat '}'
  // Assign Z with the {z} mark operand
  Z = X86Operand::CreateToken("{z}", StartLoc);
  return false;
}

// true on failure, false otherwise
bool X86AsmParser::HandleAVX512Operand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  if (getLexer().is(AsmToken::LCurly)) {
    // Eat "{" and mark the current place.
    const SMLoc consumedToken = consumeToken();
    // Distinguish {1to<NUM>} from {%k<NUM>}.
    if(getLexer().is(AsmToken::Integer)) {
      // Parse memory broadcasting ({1to<NUM>}).
      if (getLexer().getTok().getIntVal() != 1)
        return TokError("Expected 1to<NUM> at this point");
      StringRef Prefix = getLexer().getTok().getString();
      Parser.Lex(); // Eat first token of 1to8
      if (!getLexer().is(AsmToken::Identifier))
        return TokError("Expected 1to<NUM> at this point");
      // Recognize only reasonable suffixes.
      SmallVector<char, 5> BroadcastVector;
      StringRef BroadcastString = (Prefix + getLexer().getTok().getIdentifier())
                                      .toStringRef(BroadcastVector);
      if (!BroadcastString.starts_with("1to"))
        return TokError("Expected 1to<NUM> at this point");
      const char *BroadcastPrimitive =
          StringSwitch<const char *>(BroadcastString)
              .Case("1to2", "{1to2}")
              .Case("1to4", "{1to4}")
              .Case("1to8", "{1to8}")
              .Case("1to16", "{1to16}")
              .Case("1to32", "{1to32}")
              .Default(nullptr);
      if (!BroadcastPrimitive)
        return TokError("Invalid memory broadcast primitive.");
      Parser.Lex(); // Eat trailing token of 1toN
      if (!getLexer().is(AsmToken::RCurly))
        return TokError("Expected } at this point");
      Parser.Lex();  // Eat "}"
      Operands.push_back(X86Operand::CreateToken(BroadcastPrimitive,
                                                 consumedToken));
      // No AVX512 specific primitives can pass
      // after memory broadcasting, so return.
      return false;
    } else {
      // Parse either {k}{z}, {z}{k}, {k} or {z}
      // last one have no meaning, but GCC accepts it
      // Currently, we're just pass a '{' mark
      std::unique_ptr<X86Operand> Z;
      if (ParseZ(Z, consumedToken))
        return true;
      // Reaching here means that parsing of the allegadly '{z}' mark yielded
      // no errors.
      // Query for the need of further parsing for a {%k<NUM>} mark
      if (!Z || getLexer().is(AsmToken::LCurly)) {
        SMLoc StartLoc = Z ? consumeToken() : consumedToken;
        // Parse an op-mask register mark ({%k<NUM>}), which is now to be
        // expected
        MCRegister RegNo;
        SMLoc RegLoc;
        if (!parseRegister(RegNo, RegLoc, StartLoc) &&
            X86MCRegisterClasses[X86::VK1RegClassID].contains(RegNo)) {
          if (RegNo == X86::K0)
            return Error(RegLoc, "Register k0 can't be used as write mask");
          if (!getLexer().is(AsmToken::RCurly))
            return Error(getLexer().getLoc(), "Expected } at this point");
          Operands.push_back(X86Operand::CreateToken("{", StartLoc));
          Operands.push_back(
              X86Operand::CreateReg(RegNo, StartLoc, StartLoc));
          Operands.push_back(X86Operand::CreateToken("}", consumeToken()));
        } else
          return Error(getLexer().getLoc(),
                        "Expected an op-mask register at this point");
        // {%k<NUM>} mark is found, inquire for {z}
        if (getLexer().is(AsmToken::LCurly) && !Z) {
          // Have we've found a parsing error, or found no (expected) {z} mark
          // - report an error
          if (ParseZ(Z, consumeToken()) || !Z)
            return Error(getLexer().getLoc(),
                         "Expected a {z} mark at this point");

        }
        // '{z}' on its own is meaningless, hence should be ignored.
        // on the contrary - have it been accompanied by a K register,
        // allow it.
        if (Z)
          Operands.push_back(std::move(Z));
      }
    }
  }
  return false;
}

/// ParseMemOperand: 'seg : disp(basereg, indexreg, scale)'.  The '%ds:' prefix
/// has already been parsed if present. disp may be provided as well.
bool X86AsmParser::ParseMemOperand(unsigned SegReg, const MCExpr *Disp,
                                   SMLoc StartLoc, SMLoc EndLoc,
                                   OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc Loc;
  // Based on the initial passed values, we may be in any of these cases, we are
  // in one of these cases (with current position (*)):

  //   1. seg : * disp  (base-index-scale-expr)
  //   2. seg : *(disp) (base-index-scale-expr)
  //   3. seg :       *(base-index-scale-expr)
  //   4.        disp  *(base-index-scale-expr)
  //   5.      *(disp)  (base-index-scale-expr)
  //   6.             *(base-index-scale-expr)
  //   7.  disp *
  //   8. *(disp)

  // If we do not have an displacement yet, check if we're in cases 4 or 6 by
  // checking if the first object after the parenthesis is a register (or an
  // identifier referring to a register) and parse the displacement or default
  // to 0 as appropriate.
  auto isAtMemOperand = [this]() {
    if (this->getLexer().isNot(AsmToken::LParen))
      return false;
    AsmToken Buf[2];
    StringRef Id;
    auto TokCount = this->getLexer().peekTokens(Buf, true);
    if (TokCount == 0)
      return false;
    switch (Buf[0].getKind()) {
    case AsmToken::Percent:
    case AsmToken::Comma:
      return true;
    // These lower cases are doing a peekIdentifier.
    case AsmToken::At:
    case AsmToken::Dollar:
      if ((TokCount > 1) &&
          (Buf[1].is(AsmToken::Identifier) || Buf[1].is(AsmToken::String)) &&
          (Buf[0].getLoc().getPointer() + 1 == Buf[1].getLoc().getPointer()))
        Id = StringRef(Buf[0].getLoc().getPointer(),
                       Buf[1].getIdentifier().size() + 1);
      break;
    case AsmToken::Identifier:
    case AsmToken::String:
      Id = Buf[0].getIdentifier();
      break;
    default:
      return false;
    }
    // We have an ID. Check if it is bound to a register.
    if (!Id.empty()) {
      MCSymbol *Sym = this->getContext().getOrCreateSymbol(Id);
      if (Sym->isVariable()) {
        auto V = Sym->getVariableValue(/*SetUsed*/ false);
        return isa<X86MCExpr>(V);
      }
    }
    return false;
  };

  if (!Disp) {
    // Parse immediate if we're not at a mem operand yet.
    if (!isAtMemOperand()) {
      if (Parser.parseTokenLoc(Loc) || Parser.parseExpression(Disp, EndLoc))
        return true;
      assert(!isa<X86MCExpr>(Disp) && "Expected non-register here.");
    } else {
      // Disp is implicitly zero if we haven't parsed it yet.
      Disp = MCConstantExpr::create(0, Parser.getContext());
    }
  }

  // We are now either at the end of the operand or at the '(' at the start of a
  // base-index-scale-expr.

  if (!parseOptionalToken(AsmToken::LParen)) {
    if (SegReg == 0)
      Operands.push_back(
          X86Operand::CreateMem(getPointerWidth(), Disp, StartLoc, EndLoc));
    else
      Operands.push_back(X86Operand::CreateMem(getPointerWidth(), SegReg, Disp,
                                               0, 0, 1, StartLoc, EndLoc));
    return false;
  }

  // If we reached here, then eat the '(' and Process
  // the rest of the memory operand.
  unsigned BaseReg = 0, IndexReg = 0, Scale = 1;
  SMLoc BaseLoc = getLexer().getLoc();
  const MCExpr *E;
  StringRef ErrMsg;

  // Parse BaseReg if one is provided.
  if (getLexer().isNot(AsmToken::Comma) && getLexer().isNot(AsmToken::RParen)) {
    if (Parser.parseExpression(E, EndLoc) ||
        check(!isa<X86MCExpr>(E), BaseLoc, "expected register here"))
      return true;

    // Check the register.
    BaseReg = cast<X86MCExpr>(E)->getRegNo();
    if (BaseReg == X86::EIZ || BaseReg == X86::RIZ)
      return Error(BaseLoc, "eiz and riz can only be used as index registers",
                   SMRange(BaseLoc, EndLoc));
  }

  if (parseOptionalToken(AsmToken::Comma)) {
    // Following the comma we should have either an index register, or a scale
    // value. We don't support the later form, but we want to parse it
    // correctly.
    //
    // Even though it would be completely consistent to support syntax like
    // "1(%eax,,1)", the assembler doesn't. Use "eiz" or "riz" for this.
    if (getLexer().isNot(AsmToken::RParen)) {
      if (Parser.parseTokenLoc(Loc) || Parser.parseExpression(E, EndLoc))
        return true;

      if (!isa<X86MCExpr>(E)) {
        // We've parsed an unexpected Scale Value instead of an index
        // register. Interpret it as an absolute.
        int64_t ScaleVal;
        if (!E->evaluateAsAbsolute(ScaleVal, getStreamer().getAssemblerPtr()))
          return Error(Loc, "expected absolute expression");
        if (ScaleVal != 1)
          Warning(Loc, "scale factor without index register is ignored");
        Scale = 1;
      } else { // IndexReg Found.
        IndexReg = cast<X86MCExpr>(E)->getRegNo();

        if (BaseReg == X86::RIP)
          return Error(Loc,
                       "%rip as base register can not have an index register");
        if (IndexReg == X86::RIP)
          return Error(Loc, "%rip is not allowed as an index register");

        if (parseOptionalToken(AsmToken::Comma)) {
          // Parse the scale amount:
          //  ::= ',' [scale-expression]

          // A scale amount without an index is ignored.
          if (getLexer().isNot(AsmToken::RParen)) {
            int64_t ScaleVal;
            if (Parser.parseTokenLoc(Loc) ||
                Parser.parseAbsoluteExpression(ScaleVal))
              return Error(Loc, "expected scale expression");
            Scale = (unsigned)ScaleVal;
            // Validate the scale amount.
            if (X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg) &&
                Scale != 1)
              return Error(Loc, "scale factor in 16-bit address must be 1");
            if (checkScale(Scale, ErrMsg))
              return Error(Loc, ErrMsg);
          }
        }
      }
    }
  }

  // Ok, we've eaten the memory operand, verify we have a ')' and eat it too.
  if (parseToken(AsmToken::RParen, "unexpected token in memory operand"))
    return true;

  // This is to support otherwise illegal operand (%dx) found in various
  // unofficial manuals examples (e.g. "out[s]?[bwl]? %al, (%dx)") and must now
  // be supported. Mark such DX variants separately fix only in special cases.
  if (BaseReg == X86::DX && IndexReg == 0 && Scale == 1 && SegReg == 0 &&
      isa<MCConstantExpr>(Disp) &&
      cast<MCConstantExpr>(Disp)->getValue() == 0) {
    Operands.push_back(X86Operand::CreateDXReg(BaseLoc, BaseLoc));
    return false;
  }

  if (CheckBaseRegAndIndexRegAndScale(BaseReg, IndexReg, Scale, is64BitMode(),
                                      ErrMsg))
    return Error(BaseLoc, ErrMsg);

  // If the displacement is a constant, check overflows. For 64-bit addressing,
  // gas requires isInt<32> and otherwise reports an error. For others, gas
  // reports a warning and allows a wider range. E.g. gas allows
  // [-0xffffffff,0xffffffff] for 32-bit addressing (e.g. Linux kernel uses
  // `leal -__PAGE_OFFSET(%ecx),%esp` where __PAGE_OFFSET is 0xc0000000).
  if (BaseReg || IndexReg) {
    if (auto CE = dyn_cast<MCConstantExpr>(Disp)) {
      auto Imm = CE->getValue();
      bool Is64 = X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg) ||
                  X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg);
      bool Is16 = X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg);
      if (Is64) {
        if (!isInt<32>(Imm))
          return Error(BaseLoc, "displacement " + Twine(Imm) +
                                    " is not within [-2147483648, 2147483647]");
      } else if (!Is16) {
        if (!isUInt<32>(Imm < 0 ? -uint64_t(Imm) : uint64_t(Imm))) {
          Warning(BaseLoc, "displacement " + Twine(Imm) +
                               " shortened to 32-bit signed " +
                               Twine(static_cast<int32_t>(Imm)));
        }
      } else if (!isUInt<16>(Imm < 0 ? -uint64_t(Imm) : uint64_t(Imm))) {
        Warning(BaseLoc, "displacement " + Twine(Imm) +
                             " shortened to 16-bit signed " +
                             Twine(static_cast<int16_t>(Imm)));
      }
    }
  }

  if (SegReg || BaseReg || IndexReg)
    Operands.push_back(X86Operand::CreateMem(getPointerWidth(), SegReg, Disp,
                                             BaseReg, IndexReg, Scale, StartLoc,
                                             EndLoc));
  else
    Operands.push_back(
        X86Operand::CreateMem(getPointerWidth(), Disp, StartLoc, EndLoc));
  return false;
}

// Parse either a standard primary expression or a register.
bool X86AsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  // See if this is a register first.
  if (getTok().is(AsmToken::Percent) ||
      (isParsingIntelSyntax() && getTok().is(AsmToken::Identifier) &&
       MatchRegisterName(Parser.getTok().getString()))) {
    SMLoc StartLoc = Parser.getTok().getLoc();
    MCRegister RegNo;
    if (parseRegister(RegNo, StartLoc, EndLoc))
      return true;
    Res = X86MCExpr::create(RegNo, Parser.getContext());
    return false;
  }
  return Parser.parsePrimaryExpr(Res, EndLoc, nullptr);
}

bool X86AsmParser::ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                    SMLoc NameLoc, OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  InstInfo = &Info;

  // Reset the forced VEX encoding.
  ForcedOpcodePrefix = OpcodePrefix_Default;
  ForcedDispEncoding = DispEncoding_Default;
  UseApxExtendedReg = false;
  ForcedNoFlag = false;

  // Parse pseudo prefixes.
  while (true) {
    if (Name == "{") {
      if (getLexer().isNot(AsmToken::Identifier))
        return Error(Parser.getTok().getLoc(), "Unexpected token after '{'");
      std::string Prefix = Parser.getTok().getString().lower();
      Parser.Lex(); // Eat identifier.
      if (getLexer().isNot(AsmToken::RCurly))
        return Error(Parser.getTok().getLoc(), "Expected '}'");
      Parser.Lex(); // Eat curly.

      if (Prefix == "rex")
        ForcedOpcodePrefix = OpcodePrefix_REX;
      else if (Prefix == "rex2")
        ForcedOpcodePrefix = OpcodePrefix_REX2;
      else if (Prefix == "vex")
        ForcedOpcodePrefix = OpcodePrefix_VEX;
      else if (Prefix == "vex2")
        ForcedOpcodePrefix = OpcodePrefix_VEX2;
      else if (Prefix == "vex3")
        ForcedOpcodePrefix = OpcodePrefix_VEX3;
      else if (Prefix == "evex")
        ForcedOpcodePrefix = OpcodePrefix_EVEX;
      else if (Prefix == "disp8")
        ForcedDispEncoding = DispEncoding_Disp8;
      else if (Prefix == "disp32")
        ForcedDispEncoding = DispEncoding_Disp32;
      else if (Prefix == "nf")
        ForcedNoFlag = true;
      else
        return Error(NameLoc, "unknown prefix");

      NameLoc = Parser.getTok().getLoc();
      if (getLexer().is(AsmToken::LCurly)) {
        Parser.Lex();
        Name = "{";
      } else {
        if (getLexer().isNot(AsmToken::Identifier))
          return Error(Parser.getTok().getLoc(), "Expected identifier");
        // FIXME: The mnemonic won't match correctly if its not in lower case.
        Name = Parser.getTok().getString();
        Parser.Lex();
      }
      continue;
    }
    // Parse MASM style pseudo prefixes.
    if (isParsingMSInlineAsm()) {
      if (Name.equals_insensitive("vex"))
        ForcedOpcodePrefix = OpcodePrefix_VEX;
      else if (Name.equals_insensitive("vex2"))
        ForcedOpcodePrefix = OpcodePrefix_VEX2;
      else if (Name.equals_insensitive("vex3"))
        ForcedOpcodePrefix = OpcodePrefix_VEX3;
      else if (Name.equals_insensitive("evex"))
        ForcedOpcodePrefix = OpcodePrefix_EVEX;

      if (ForcedOpcodePrefix != OpcodePrefix_Default) {
        if (getLexer().isNot(AsmToken::Identifier))
          return Error(Parser.getTok().getLoc(), "Expected identifier");
        // FIXME: The mnemonic won't match correctly if its not in lower case.
        Name = Parser.getTok().getString();
        NameLoc = Parser.getTok().getLoc();
        Parser.Lex();
      }
    }
    break;
  }

  // Support the suffix syntax for overriding displacement size as well.
  if (Name.consume_back(".d32")) {
    ForcedDispEncoding = DispEncoding_Disp32;
  } else if (Name.consume_back(".d8")) {
    ForcedDispEncoding = DispEncoding_Disp8;
  }

  StringRef PatchedName = Name;

  // Hack to skip "short" following Jcc.
  if (isParsingIntelSyntax() &&
      (PatchedName == "jmp" || PatchedName == "jc" || PatchedName == "jnc" ||
       PatchedName == "jcxz" || PatchedName == "jecxz" ||
       (PatchedName.starts_with("j") &&
        ParseConditionCode(PatchedName.substr(1)) != X86::COND_INVALID))) {
    StringRef NextTok = Parser.getTok().getString();
    if (Parser.isParsingMasm() ? NextTok.equals_insensitive("short")
                               : NextTok == "short") {
      SMLoc NameEndLoc =
          NameLoc.getFromPointer(NameLoc.getPointer() + Name.size());
      // Eat the short keyword.
      Parser.Lex();
      // MS and GAS ignore the short keyword; they both determine the jmp type
      // based on the distance of the label. (NASM does emit different code with
      // and without "short," though.)
      InstInfo->AsmRewrites->emplace_back(AOK_Skip, NameEndLoc,
                                          NextTok.size() + 1);
    }
  }

  // FIXME: Hack to recognize setneb as setne.
  if (PatchedName.starts_with("set") && PatchedName.ends_with("b") &&
      PatchedName != "setzub" && PatchedName != "setzunb" &&
      PatchedName != "setb" && PatchedName != "setnb")
    PatchedName = PatchedName.substr(0, Name.size()-1);

  unsigned ComparisonPredicate = ~0U;

  // FIXME: Hack to recognize cmp<comparison code>{sh,ss,sd,ph,ps,pd}.
  if ((PatchedName.starts_with("cmp") || PatchedName.starts_with("vcmp")) &&
      (PatchedName.ends_with("ss") || PatchedName.ends_with("sd") ||
       PatchedName.ends_with("sh") || PatchedName.ends_with("ph") ||
       PatchedName.ends_with("ps") || PatchedName.ends_with("pd"))) {
    bool IsVCMP = PatchedName[0] == 'v';
    unsigned CCIdx = IsVCMP ? 4 : 3;
    unsigned CC = StringSwitch<unsigned>(
      PatchedName.slice(CCIdx, PatchedName.size() - 2))
      .Case("eq",       0x00)
      .Case("eq_oq",    0x00)
      .Case("lt",       0x01)
      .Case("lt_os",    0x01)
      .Case("le",       0x02)
      .Case("le_os",    0x02)
      .Case("unord",    0x03)
      .Case("unord_q",  0x03)
      .Case("neq",      0x04)
      .Case("neq_uq",   0x04)
      .Case("nlt",      0x05)
      .Case("nlt_us",   0x05)
      .Case("nle",      0x06)
      .Case("nle_us",   0x06)
      .Case("ord",      0x07)
      .Case("ord_q",    0x07)
      /* AVX only from here */
      .Case("eq_uq",    0x08)
      .Case("nge",      0x09)
      .Case("nge_us",   0x09)
      .Case("ngt",      0x0A)
      .Case("ngt_us",   0x0A)
      .Case("false",    0x0B)
      .Case("false_oq", 0x0B)
      .Case("neq_oq",   0x0C)
      .Case("ge",       0x0D)
      .Case("ge_os",    0x0D)
      .Case("gt",       0x0E)
      .Case("gt_os",    0x0E)
      .Case("true",     0x0F)
      .Case("true_uq",  0x0F)
      .Case("eq_os",    0x10)
      .Case("lt_oq",    0x11)
      .Case("le_oq",    0x12)
      .Case("unord_s",  0x13)
      .Case("neq_us",   0x14)
      .Case("nlt_uq",   0x15)
      .Case("nle_uq",   0x16)
      .Case("ord_s",    0x17)
      .Case("eq_us",    0x18)
      .Case("nge_uq",   0x19)
      .Case("ngt_uq",   0x1A)
      .Case("false_os", 0x1B)
      .Case("neq_os",   0x1C)
      .Case("ge_oq",    0x1D)
      .Case("gt_oq",    0x1E)
      .Case("true_us",  0x1F)
      .Default(~0U);
    if (CC != ~0U && (IsVCMP || CC < 8) &&
        (IsVCMP || PatchedName.back() != 'h')) {
      if (PatchedName.ends_with("ss"))
        PatchedName = IsVCMP ? "vcmpss" : "cmpss";
      else if (PatchedName.ends_with("sd"))
        PatchedName = IsVCMP ? "vcmpsd" : "cmpsd";
      else if (PatchedName.ends_with("ps"))
        PatchedName = IsVCMP ? "vcmpps" : "cmpps";
      else if (PatchedName.ends_with("pd"))
        PatchedName = IsVCMP ? "vcmppd" : "cmppd";
      else if (PatchedName.ends_with("sh"))
        PatchedName = "vcmpsh";
      else if (PatchedName.ends_with("ph"))
        PatchedName = "vcmpph";
      else
        llvm_unreachable("Unexpected suffix!");

      ComparisonPredicate = CC;
    }
  }

  // FIXME: Hack to recognize vpcmp<comparison code>{ub,uw,ud,uq,b,w,d,q}.
  if (PatchedName.starts_with("vpcmp") &&
      (PatchedName.back() == 'b' || PatchedName.back() == 'w' ||
       PatchedName.back() == 'd' || PatchedName.back() == 'q')) {
    unsigned SuffixSize = PatchedName.drop_back().back() == 'u' ? 2 : 1;
    unsigned CC = StringSwitch<unsigned>(
      PatchedName.slice(5, PatchedName.size() - SuffixSize))
      .Case("eq",    0x0) // Only allowed on unsigned. Checked below.
      .Case("lt",    0x1)
      .Case("le",    0x2)
      //.Case("false", 0x3) // Not a documented alias.
      .Case("neq",   0x4)
      .Case("nlt",   0x5)
      .Case("nle",   0x6)
      //.Case("true",  0x7) // Not a documented alias.
      .Default(~0U);
    if (CC != ~0U && (CC != 0 || SuffixSize == 2)) {
      switch (PatchedName.back()) {
      default: llvm_unreachable("Unexpected character!");
      case 'b': PatchedName = SuffixSize == 2 ? "vpcmpub" : "vpcmpb"; break;
      case 'w': PatchedName = SuffixSize == 2 ? "vpcmpuw" : "vpcmpw"; break;
      case 'd': PatchedName = SuffixSize == 2 ? "vpcmpud" : "vpcmpd"; break;
      case 'q': PatchedName = SuffixSize == 2 ? "vpcmpuq" : "vpcmpq"; break;
      }
      // Set up the immediate to push into the operands later.
      ComparisonPredicate = CC;
    }
  }

  // FIXME: Hack to recognize vpcom<comparison code>{ub,uw,ud,uq,b,w,d,q}.
  if (PatchedName.starts_with("vpcom") &&
      (PatchedName.back() == 'b' || PatchedName.back() == 'w' ||
       PatchedName.back() == 'd' || PatchedName.back() == 'q')) {
    unsigned SuffixSize = PatchedName.drop_back().back() == 'u' ? 2 : 1;
    unsigned CC = StringSwitch<unsigned>(
      PatchedName.slice(5, PatchedName.size() - SuffixSize))
      .Case("lt",    0x0)
      .Case("le",    0x1)
      .Case("gt",    0x2)
      .Case("ge",    0x3)
      .Case("eq",    0x4)
      .Case("neq",   0x5)
      .Case("false", 0x6)
      .Case("true",  0x7)
      .Default(~0U);
    if (CC != ~0U) {
      switch (PatchedName.back()) {
      default: llvm_unreachable("Unexpected character!");
      case 'b': PatchedName = SuffixSize == 2 ? "vpcomub" : "vpcomb"; break;
      case 'w': PatchedName = SuffixSize == 2 ? "vpcomuw" : "vpcomw"; break;
      case 'd': PatchedName = SuffixSize == 2 ? "vpcomud" : "vpcomd"; break;
      case 'q': PatchedName = SuffixSize == 2 ? "vpcomuq" : "vpcomq"; break;
      }
      // Set up the immediate to push into the operands later.
      ComparisonPredicate = CC;
    }
  }

  // Determine whether this is an instruction prefix.
  // FIXME:
  // Enhance prefixes integrity robustness. for example, following forms
  // are currently tolerated:
  // repz repnz <insn>    ; GAS errors for the use of two similar prefixes
  // lock addq %rax, %rbx ; Destination operand must be of memory type
  // xacquire <insn>      ; xacquire must be accompanied by 'lock'
  bool IsPrefix =
      StringSwitch<bool>(Name)
          .Cases("cs", "ds", "es", "fs", "gs", "ss", true)
          .Cases("rex64", "data32", "data16", "addr32", "addr16", true)
          .Cases("xacquire", "xrelease", true)
          .Cases("acquire", "release", isParsingIntelSyntax())
          .Default(false);

  auto isLockRepeatNtPrefix = [](StringRef N) {
    return StringSwitch<bool>(N)
        .Cases("lock", "rep", "repe", "repz", "repne", "repnz", "notrack", true)
        .Default(false);
  };

  bool CurlyAsEndOfStatement = false;

  unsigned Flags = X86::IP_NO_PREFIX;
  while (isLockRepeatNtPrefix(Name.lower())) {
    unsigned Prefix =
        StringSwitch<unsigned>(Name)
            .Cases("lock", "lock", X86::IP_HAS_LOCK)
            .Cases("rep", "repe", "repz", X86::IP_HAS_REPEAT)
            .Cases("repne", "repnz", X86::IP_HAS_REPEAT_NE)
            .Cases("notrack", "notrack", X86::IP_HAS_NOTRACK)
            .Default(X86::IP_NO_PREFIX); // Invalid prefix (impossible)
    Flags |= Prefix;
    if (getLexer().is(AsmToken::EndOfStatement)) {
      // We don't have real instr with the given prefix
      //  let's use the prefix as the instr.
      // TODO: there could be several prefixes one after another
      Flags = X86::IP_NO_PREFIX;
      break;
    }
    // FIXME: The mnemonic won't match correctly if its not in lower case.
    Name = Parser.getTok().getString();
    Parser.Lex(); // eat the prefix
    // Hack: we could have something like "rep # some comment" or
    //    "lock; cmpxchg16b $1" or "lock\0A\09incl" or "lock/incl"
    while (Name.starts_with(";") || Name.starts_with("\n") ||
           Name.starts_with("#") || Name.starts_with("\t") ||
           Name.starts_with("/")) {
      // FIXME: The mnemonic won't match correctly if its not in lower case.
      Name = Parser.getTok().getString();
      Parser.Lex(); // go to next prefix or instr
    }
  }

  if (Flags)
    PatchedName = Name;

  // Hacks to handle 'data16' and 'data32'
  if (PatchedName == "data16" && is16BitMode()) {
    return Error(NameLoc, "redundant data16 prefix");
  }
  if (PatchedName == "data32") {
    if (is32BitMode())
      return Error(NameLoc, "redundant data32 prefix");
    if (is64BitMode())
      return Error(NameLoc, "'data32' is not supported in 64-bit mode");
    // Hack to 'data16' for the table lookup.
    PatchedName = "data16";

    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      StringRef Next = Parser.getTok().getString();
      getLexer().Lex();
      // data32 effectively changes the instruction suffix.
      // TODO Generalize.
      if (Next == "callw")
        Next = "calll";
      if (Next == "ljmpw")
        Next = "ljmpl";

      Name = Next;
      PatchedName = Name;
      ForcedDataPrefix = X86::Is32Bit;
      IsPrefix = false;
    }
  }

  Operands.push_back(X86Operand::CreateToken(PatchedName, NameLoc));

  // Push the immediate if we extracted one from the mnemonic.
  if (ComparisonPredicate != ~0U && !isParsingIntelSyntax()) {
    const MCExpr *ImmOp = MCConstantExpr::create(ComparisonPredicate,
                                                 getParser().getContext());
    Operands.push_back(X86Operand::CreateImm(ImmOp, NameLoc, NameLoc));
  }

  // Parse condtional flags after mnemonic.
  if ((Name.starts_with("ccmp") || Name.starts_with("ctest")) &&
      parseCFlagsOp(Operands))
    return true;

  // This does the actual operand parsing.  Don't parse any more if we have a
  // prefix juxtaposed with an operation like "lock incl 4(%rax)", because we
  // just want to parse the "lock" as the first instruction and the "incl" as
  // the next one.
  if (getLexer().isNot(AsmToken::EndOfStatement) && !IsPrefix) {
    // Parse '*' modifier.
    if (getLexer().is(AsmToken::Star))
      Operands.push_back(X86Operand::CreateToken("*", consumeToken()));

    // Read the operands.
    while (true) {
      if (parseOperand(Operands, Name))
        return true;
      if (HandleAVX512Operand(Operands))
        return true;

      // check for comma and eat it
      if (getLexer().is(AsmToken::Comma))
        Parser.Lex();
      else
        break;
     }

    // In MS inline asm curly braces mark the beginning/end of a block,
    // therefore they should be interepreted as end of statement
    CurlyAsEndOfStatement =
        isParsingIntelSyntax() && isParsingMSInlineAsm() &&
        (getLexer().is(AsmToken::LCurly) || getLexer().is(AsmToken::RCurly));
    if (getLexer().isNot(AsmToken::EndOfStatement) && !CurlyAsEndOfStatement)
      return TokError("unexpected token in argument list");
  }

  // Push the immediate if we extracted one from the mnemonic.
  if (ComparisonPredicate != ~0U && isParsingIntelSyntax()) {
    const MCExpr *ImmOp = MCConstantExpr::create(ComparisonPredicate,
                                                 getParser().getContext());
    Operands.push_back(X86Operand::CreateImm(ImmOp, NameLoc, NameLoc));
  }

  // Consume the EndOfStatement or the prefix separator Slash
  if (getLexer().is(AsmToken::EndOfStatement) ||
      (IsPrefix && getLexer().is(AsmToken::Slash)))
    Parser.Lex();
  else if (CurlyAsEndOfStatement)
    // Add an actual EndOfStatement before the curly brace
    Info.AsmRewrites->emplace_back(AOK_EndOfStatement,
                                   getLexer().getTok().getLoc(), 0);

  // This is for gas compatibility and cannot be done in td.
  // Adding "p" for some floating point with no argument.
  // For example: fsub --> fsubp
  bool IsFp =
    Name == "fsub" || Name == "fdiv" || Name == "fsubr" || Name == "fdivr";
  if (IsFp && Operands.size() == 1) {
    const char *Repl = StringSwitch<const char *>(Name)
      .Case("fsub", "fsubp")
      .Case("fdiv", "fdivp")
      .Case("fsubr", "fsubrp")
      .Case("fdivr", "fdivrp");
    static_cast<X86Operand &>(*Operands[0]).setTokenValue(Repl);
  }

  if ((Name == "mov" || Name == "movw" || Name == "movl") &&
      (Operands.size() == 3)) {
    X86Operand &Op1 = (X86Operand &)*Operands[1];
    X86Operand &Op2 = (X86Operand &)*Operands[2];
    SMLoc Loc = Op1.getEndLoc();
    // Moving a 32 or 16 bit value into a segment register has the same
    // behavior. Modify such instructions to always take shorter form.
    if (Op1.isReg() && Op2.isReg() &&
        X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(
            Op2.getReg()) &&
        (X86MCRegisterClasses[X86::GR16RegClassID].contains(Op1.getReg()) ||
         X86MCRegisterClasses[X86::GR32RegClassID].contains(Op1.getReg()))) {
      // Change instruction name to match new instruction.
      if (Name != "mov" && Name[3] == (is16BitMode() ? 'l' : 'w')) {
        Name = is16BitMode() ? "movw" : "movl";
        Operands[0] = X86Operand::CreateToken(Name, NameLoc);
      }
      // Select the correct equivalent 16-/32-bit source register.
      MCRegister Reg =
          getX86SubSuperRegister(Op1.getReg(), is16BitMode() ? 16 : 32);
      Operands[1] = X86Operand::CreateReg(Reg, Loc, Loc);
    }
  }

  // This is a terrible hack to handle "out[s]?[bwl]? %al, (%dx)" ->
  // "outb %al, %dx".  Out doesn't take a memory form, but this is a widely
  // documented form in various unofficial manuals, so a lot of code uses it.
  if ((Name == "outb" || Name == "outsb" || Name == "outw" || Name == "outsw" ||
       Name == "outl" || Name == "outsl" || Name == "out" || Name == "outs") &&
      Operands.size() == 3) {
    X86Operand &Op = (X86Operand &)*Operands.back();
    if (Op.isDXReg())
      Operands.back() = X86Operand::CreateReg(X86::DX, Op.getStartLoc(),
                                              Op.getEndLoc());
  }
  // Same hack for "in[s]?[bwl]? (%dx), %al" -> "inb %dx, %al".
  if ((Name == "inb" || Name == "insb" || Name == "inw" || Name == "insw" ||
       Name == "inl" || Name == "insl" || Name == "in" || Name == "ins") &&
      Operands.size() == 3) {
    X86Operand &Op = (X86Operand &)*Operands[1];
    if (Op.isDXReg())
      Operands[1] = X86Operand::CreateReg(X86::DX, Op.getStartLoc(),
                                          Op.getEndLoc());
  }

  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 2> TmpOperands;
  bool HadVerifyError = false;

  // Append default arguments to "ins[bwld]"
  if (Name.starts_with("ins") &&
      (Operands.size() == 1 || Operands.size() == 3) &&
      (Name == "insb" || Name == "insw" || Name == "insl" || Name == "insd" ||
       Name == "ins")) {

    AddDefaultSrcDestOperands(TmpOperands,
                              X86Operand::CreateReg(X86::DX, NameLoc, NameLoc),
                              DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Append default arguments to "outs[bwld]"
  if (Name.starts_with("outs") &&
      (Operands.size() == 1 || Operands.size() == 3) &&
      (Name == "outsb" || Name == "outsw" || Name == "outsl" ||
       Name == "outsd" || Name == "outs")) {
    AddDefaultSrcDestOperands(TmpOperands, DefaultMemSIOperand(NameLoc),
                              X86Operand::CreateReg(X86::DX, NameLoc, NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Transform "lods[bwlq]" into "lods[bwlq] ($SIREG)" for appropriate
  // values of $SIREG according to the mode. It would be nice if this
  // could be achieved with InstAlias in the tables.
  if (Name.starts_with("lods") &&
      (Operands.size() == 1 || Operands.size() == 2) &&
      (Name == "lods" || Name == "lodsb" || Name == "lodsw" ||
       Name == "lodsl" || Name == "lodsd" || Name == "lodsq")) {
    TmpOperands.push_back(DefaultMemSIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Transform "stos[bwlq]" into "stos[bwlq] ($DIREG)" for appropriate
  // values of $DIREG according to the mode. It would be nice if this
  // could be achieved with InstAlias in the tables.
  if (Name.starts_with("stos") &&
      (Operands.size() == 1 || Operands.size() == 2) &&
      (Name == "stos" || Name == "stosb" || Name == "stosw" ||
       Name == "stosl" || Name == "stosd" || Name == "stosq")) {
    TmpOperands.push_back(DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Transform "scas[bwlq]" into "scas[bwlq] ($DIREG)" for appropriate
  // values of $DIREG according to the mode. It would be nice if this
  // could be achieved with InstAlias in the tables.
  if (Name.starts_with("scas") &&
      (Operands.size() == 1 || Operands.size() == 2) &&
      (Name == "scas" || Name == "scasb" || Name == "scasw" ||
       Name == "scasl" || Name == "scasd" || Name == "scasq")) {
    TmpOperands.push_back(DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Add default SI and DI operands to "cmps[bwlq]".
  if (Name.starts_with("cmps") &&
      (Operands.size() == 1 || Operands.size() == 3) &&
      (Name == "cmps" || Name == "cmpsb" || Name == "cmpsw" ||
       Name == "cmpsl" || Name == "cmpsd" || Name == "cmpsq")) {
    AddDefaultSrcDestOperands(TmpOperands, DefaultMemDIOperand(NameLoc),
                              DefaultMemSIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Add default SI and DI operands to "movs[bwlq]".
  if (((Name.starts_with("movs") &&
        (Name == "movs" || Name == "movsb" || Name == "movsw" ||
         Name == "movsl" || Name == "movsd" || Name == "movsq")) ||
       (Name.starts_with("smov") &&
        (Name == "smov" || Name == "smovb" || Name == "smovw" ||
         Name == "smovl" || Name == "smovd" || Name == "smovq"))) &&
      (Operands.size() == 1 || Operands.size() == 3)) {
    if (Name == "movsd" && Operands.size() == 1 && !isParsingIntelSyntax())
      Operands.back() = X86Operand::CreateToken("movsl", NameLoc);
    AddDefaultSrcDestOperands(TmpOperands, DefaultMemSIOperand(NameLoc),
                              DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Check if we encountered an error for one the string insturctions
  if (HadVerifyError) {
    return HadVerifyError;
  }

  // Transforms "xlat mem8" into "xlatb"
  if ((Name == "xlat" || Name == "xlatb") && Operands.size() == 2) {
    X86Operand &Op1 = static_cast<X86Operand &>(*Operands[1]);
    if (Op1.isMem8()) {
      Warning(Op1.getStartLoc(), "memory operand is only for determining the "
                                 "size, (R|E)BX will be used for the location");
      Operands.pop_back();
      static_cast<X86Operand &>(*Operands[0]).setTokenValue("xlatb");
    }
  }

  if (Flags)
    Operands.push_back(X86Operand::CreatePrefix(Flags, NameLoc, NameLoc));
  return false;
}

static bool convertSSEToAVX(MCInst &Inst) {
  ArrayRef<X86TableEntry> Table{X86SSE2AVXTable};
  unsigned Opcode = Inst.getOpcode();
  const auto I = llvm::lower_bound(Table, Opcode);
  if (I == Table.end() || I->OldOpc != Opcode)
    return false;

  Inst.setOpcode(I->NewOpc);
  // AVX variant of BLENDVPD/BLENDVPS/PBLENDVB instructions has more
  // operand compare to SSE variant, which is added below
  if (X86::isBLENDVPD(Opcode) || X86::isBLENDVPS(Opcode) ||
      X86::isPBLENDVB(Opcode))
    Inst.addOperand(Inst.getOperand(2));

  return true;
}

bool X86AsmParser::processInstruction(MCInst &Inst, const OperandVector &Ops) {
  if (MCOptions.X86Sse2Avx && convertSSEToAVX(Inst))
    return true;

  if (ForcedOpcodePrefix != OpcodePrefix_VEX3 &&
      X86::optimizeInstFromVEX3ToVEX2(Inst, MII.get(Inst.getOpcode())))
    return true;

  if (X86::optimizeShiftRotateWithImmediateOne(Inst))
    return true;

  switch (Inst.getOpcode()) {
  default: return false;
  case X86::JMP_1:
    // {disp32} forces a larger displacement as if the instruction was relaxed.
    // NOTE: 16-bit mode uses 16-bit displacement even though it says {disp32}.
    // This matches GNU assembler.
    if (ForcedDispEncoding == DispEncoding_Disp32) {
      Inst.setOpcode(is16BitMode() ? X86::JMP_2 : X86::JMP_4);
      return true;
    }

    return false;
  case X86::JCC_1:
    // {disp32} forces a larger displacement as if the instruction was relaxed.
    // NOTE: 16-bit mode uses 16-bit displacement even though it says {disp32}.
    // This matches GNU assembler.
    if (ForcedDispEncoding == DispEncoding_Disp32) {
      Inst.setOpcode(is16BitMode() ? X86::JCC_2 : X86::JCC_4);
      return true;
    }

    return false;
  case X86::INT: {
    // Transforms "int $3" into "int3" as a size optimization.
    // We can't write this as an InstAlias.
    if (!Inst.getOperand(0).isImm() || Inst.getOperand(0).getImm() != 3)
      return false;
    Inst.clear();
    Inst.setOpcode(X86::INT3);
    return true;
  }
  }
}

bool X86AsmParser::validateInstruction(MCInst &Inst, const OperandVector &Ops) {
  using namespace X86;
  const MCRegisterInfo *MRI = getContext().getRegisterInfo();
  unsigned Opcode = Inst.getOpcode();
  uint64_t TSFlags = MII.get(Opcode).TSFlags;
  if (isVFCMADDCPH(Opcode) || isVFCMADDCSH(Opcode) || isVFMADDCPH(Opcode) ||
      isVFMADDCSH(Opcode)) {
    unsigned Dest = Inst.getOperand(0).getReg();
    for (unsigned i = 2; i < Inst.getNumOperands(); i++)
      if (Inst.getOperand(i).isReg() && Dest == Inst.getOperand(i).getReg())
        return Warning(Ops[0]->getStartLoc(), "Destination register should be "
                                              "distinct from source registers");
  } else if (isVFCMULCPH(Opcode) || isVFCMULCSH(Opcode) || isVFMULCPH(Opcode) ||
             isVFMULCSH(Opcode)) {
    unsigned Dest = Inst.getOperand(0).getReg();
    // The mask variants have different operand list. Scan from the third
    // operand to avoid emitting incorrect warning.
    //    VFMULCPHZrr   Dest, Src1, Src2
    //    VFMULCPHZrrk  Dest, Dest, Mask, Src1, Src2
    //    VFMULCPHZrrkz Dest, Mask, Src1, Src2
    for (unsigned i = ((TSFlags & X86II::EVEX_K) ? 2 : 1);
         i < Inst.getNumOperands(); i++)
      if (Inst.getOperand(i).isReg() && Dest == Inst.getOperand(i).getReg())
        return Warning(Ops[0]->getStartLoc(), "Destination register should be "
                                              "distinct from source registers");
  } else if (isV4FMADDPS(Opcode) || isV4FMADDSS(Opcode) ||
             isV4FNMADDPS(Opcode) || isV4FNMADDSS(Opcode) ||
             isVP4DPWSSDS(Opcode) || isVP4DPWSSD(Opcode)) {
    unsigned Src2 = Inst.getOperand(Inst.getNumOperands() -
                                    X86::AddrNumOperands - 1).getReg();
    unsigned Src2Enc = MRI->getEncodingValue(Src2);
    if (Src2Enc % 4 != 0) {
      StringRef RegName = X86IntelInstPrinter::getRegisterName(Src2);
      unsigned GroupStart = (Src2Enc / 4) * 4;
      unsigned GroupEnd = GroupStart + 3;
      return Warning(Ops[0]->getStartLoc(),
                     "source register '" + RegName + "' implicitly denotes '" +
                     RegName.take_front(3) + Twine(GroupStart) + "' to '" +
                     RegName.take_front(3) + Twine(GroupEnd) +
                     "' source group");
    }
  } else if (isVGATHERDPD(Opcode) || isVGATHERDPS(Opcode) ||
             isVGATHERQPD(Opcode) || isVGATHERQPS(Opcode) ||
             isVPGATHERDD(Opcode) || isVPGATHERDQ(Opcode) ||
             isVPGATHERQD(Opcode) || isVPGATHERQQ(Opcode)) {
    bool HasEVEX = (TSFlags & X86II::EncodingMask) == X86II::EVEX;
    if (HasEVEX) {
      unsigned Dest = MRI->getEncodingValue(Inst.getOperand(0).getReg());
      unsigned Index = MRI->getEncodingValue(
          Inst.getOperand(4 + X86::AddrIndexReg).getReg());
      if (Dest == Index)
        return Warning(Ops[0]->getStartLoc(), "index and destination registers "
                                              "should be distinct");
    } else {
      unsigned Dest = MRI->getEncodingValue(Inst.getOperand(0).getReg());
      unsigned Mask = MRI->getEncodingValue(Inst.getOperand(1).getReg());
      unsigned Index = MRI->getEncodingValue(
          Inst.getOperand(3 + X86::AddrIndexReg).getReg());
      if (Dest == Mask || Dest == Index || Mask == Index)
        return Warning(Ops[0]->getStartLoc(), "mask, index, and destination "
                                              "registers should be distinct");
    }
  } else if (isTCMMIMFP16PS(Opcode) || isTCMMRLFP16PS(Opcode) ||
             isTDPBF16PS(Opcode) || isTDPFP16PS(Opcode) || isTDPBSSD(Opcode) ||
             isTDPBSUD(Opcode) || isTDPBUSD(Opcode) || isTDPBUUD(Opcode)) {
    unsigned SrcDest = Inst.getOperand(0).getReg();
    unsigned Src1 = Inst.getOperand(2).getReg();
    unsigned Src2 = Inst.getOperand(3).getReg();
    if (SrcDest == Src1 || SrcDest == Src2 || Src1 == Src2)
      return Error(Ops[0]->getStartLoc(), "all tmm registers must be distinct");
  }

  // Check that we aren't mixing AH/BH/CH/DH with REX prefix. We only need to
  // check this with the legacy encoding, VEX/EVEX/XOP don't use REX.
  if ((TSFlags & X86II::EncodingMask) == 0) {
    MCPhysReg HReg = X86::NoRegister;
    bool UsesRex = TSFlags & X86II::REX_W;
    unsigned NumOps = Inst.getNumOperands();
    for (unsigned i = 0; i != NumOps; ++i) {
      const MCOperand &MO = Inst.getOperand(i);
      if (!MO.isReg())
        continue;
      unsigned Reg = MO.getReg();
      if (Reg == X86::AH || Reg == X86::BH || Reg == X86::CH || Reg == X86::DH)
        HReg = Reg;
      if (X86II::isX86_64NonExtLowByteReg(Reg) ||
          X86II::isX86_64ExtendedReg(Reg))
        UsesRex = true;
    }

    if (UsesRex && HReg != X86::NoRegister) {
      StringRef RegName = X86IntelInstPrinter::getRegisterName(HReg);
      return Error(Ops[0]->getStartLoc(),
                   "can't encode '" + RegName + "' in an instruction requiring "
                   "REX prefix");
    }
  }

  if ((Opcode == X86::PREFETCHIT0 || Opcode == X86::PREFETCHIT1)) {
    const MCOperand &MO = Inst.getOperand(X86::AddrBaseReg);
    if (!MO.isReg() || MO.getReg() != X86::RIP)
      return Warning(
          Ops[0]->getStartLoc(),
          Twine((Inst.getOpcode() == X86::PREFETCHIT0 ? "'prefetchit0'"
                                                      : "'prefetchit1'")) +
              " only supports RIP-relative address");
  }
  return false;
}

void X86AsmParser::emitWarningForSpecialLVIInstruction(SMLoc Loc) {
  Warning(Loc, "Instruction may be vulnerable to LVI and "
               "requires manual mitigation");
  Note(SMLoc(), "See https://software.intel.com/"
                "security-software-guidance/insights/"
                "deep-dive-load-value-injection#specialinstructions"
                " for more information");
}

/// RET instructions and also instructions that indirect calls/jumps from memory
/// combine a load and a branch within a single instruction. To mitigate these
/// instructions against LVI, they must be decomposed into separate load and
/// branch instructions, with an LFENCE in between. For more details, see:
/// - X86LoadValueInjectionRetHardening.cpp
/// - X86LoadValueInjectionIndirectThunks.cpp
/// - https://software.intel.com/security-software-guidance/insights/deep-dive-load-value-injection
///
/// Returns `true` if a mitigation was applied or warning was emitted.
void X86AsmParser::applyLVICFIMitigation(MCInst &Inst, MCStreamer &Out) {
  // Information on control-flow instructions that require manual mitigation can
  // be found here:
  // https://software.intel.com/security-software-guidance/insights/deep-dive-load-value-injection#specialinstructions
  switch (Inst.getOpcode()) {
  case X86::RET16:
  case X86::RET32:
  case X86::RET64:
  case X86::RETI16:
  case X86::RETI32:
  case X86::RETI64: {
    MCInst ShlInst, FenceInst;
    bool Parse32 = is32BitMode() || Code16GCC;
    unsigned Basereg =
        is64BitMode() ? X86::RSP : (Parse32 ? X86::ESP : X86::SP);
    const MCExpr *Disp = MCConstantExpr::create(0, getContext());
    auto ShlMemOp = X86Operand::CreateMem(getPointerWidth(), /*SegReg=*/0, Disp,
                                          /*BaseReg=*/Basereg, /*IndexReg=*/0,
                                          /*Scale=*/1, SMLoc{}, SMLoc{}, 0);
    ShlInst.setOpcode(X86::SHL64mi);
    ShlMemOp->addMemOperands(ShlInst, 5);
    ShlInst.addOperand(MCOperand::createImm(0));
    FenceInst.setOpcode(X86::LFENCE);
    Out.emitInstruction(ShlInst, getSTI());
    Out.emitInstruction(FenceInst, getSTI());
    return;
  }
  case X86::JMP16m:
  case X86::JMP32m:
  case X86::JMP64m:
  case X86::CALL16m:
  case X86::CALL32m:
  case X86::CALL64m:
    emitWarningForSpecialLVIInstruction(Inst.getLoc());
    return;
  }
}

/// To mitigate LVI, every instruction that performs a load can be followed by
/// an LFENCE instruction to squash any potential mis-speculation. There are
/// some instructions that require additional considerations, and may requre
/// manual mitigation. For more details, see:
/// https://software.intel.com/security-software-guidance/insights/deep-dive-load-value-injection
///
/// Returns `true` if a mitigation was applied or warning was emitted.
void X86AsmParser::applyLVILoadHardeningMitigation(MCInst &Inst,
                                                   MCStreamer &Out) {
  auto Opcode = Inst.getOpcode();
  auto Flags = Inst.getFlags();
  if ((Flags & X86::IP_HAS_REPEAT) || (Flags & X86::IP_HAS_REPEAT_NE)) {
    // Information on REP string instructions that require manual mitigation can
    // be found here:
    // https://software.intel.com/security-software-guidance/insights/deep-dive-load-value-injection#specialinstructions
    switch (Opcode) {
    case X86::CMPSB:
    case X86::CMPSW:
    case X86::CMPSL:
    case X86::CMPSQ:
    case X86::SCASB:
    case X86::SCASW:
    case X86::SCASL:
    case X86::SCASQ:
      emitWarningForSpecialLVIInstruction(Inst.getLoc());
      return;
    }
  } else if (Opcode == X86::REP_PREFIX || Opcode == X86::REPNE_PREFIX) {
    // If a REP instruction is found on its own line, it may or may not be
    // followed by a vulnerable instruction. Emit a warning just in case.
    emitWarningForSpecialLVIInstruction(Inst.getLoc());
    return;
  }

  const MCInstrDesc &MCID = MII.get(Inst.getOpcode());

  // Can't mitigate after terminators or calls. A control flow change may have
  // already occurred.
  if (MCID.isTerminator() || MCID.isCall())
    return;

  // LFENCE has the mayLoad property, don't double fence.
  if (MCID.mayLoad() && Inst.getOpcode() != X86::LFENCE) {
    MCInst FenceInst;
    FenceInst.setOpcode(X86::LFENCE);
    Out.emitInstruction(FenceInst, getSTI());
  }
}

void X86AsmParser::emitInstruction(MCInst &Inst, OperandVector &Operands,
                                   MCStreamer &Out) {
  if (LVIInlineAsmHardening &&
      getSTI().hasFeature(X86::FeatureLVIControlFlowIntegrity))
    applyLVICFIMitigation(Inst, Out);

  Out.emitInstruction(Inst, getSTI());

  if (LVIInlineAsmHardening &&
      getSTI().hasFeature(X86::FeatureLVILoadHardening))
    applyLVILoadHardeningMitigation(Inst, Out);
}

static unsigned getPrefixes(OperandVector &Operands) {
  unsigned Result = 0;
  X86Operand &Prefix = static_cast<X86Operand &>(*Operands.back());
  if (Prefix.isPrefix()) {
    Result = Prefix.getPrefix();
    Operands.pop_back();
  }
  return Result;
}

bool X86AsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                           OperandVector &Operands,
                                           MCStreamer &Out, uint64_t &ErrorInfo,
                                           bool MatchingInlineAsm) {
  assert(!Operands.empty() && "Unexpect empty operand list!");
  assert((*Operands[0]).isToken() && "Leading operand should always be a mnemonic!");

  // First, handle aliases that expand to multiple instructions.
  MatchFPUWaitAlias(IDLoc, static_cast<X86Operand &>(*Operands[0]), Operands,
                    Out, MatchingInlineAsm);
  unsigned Prefixes = getPrefixes(Operands);

  MCInst Inst;

  // If REX/REX2/VEX/EVEX encoding is forced, we need to pass the USE_* flag to
  // the encoder and printer.
  if (ForcedOpcodePrefix == OpcodePrefix_REX)
    Prefixes |= X86::IP_USE_REX;
  else if (ForcedOpcodePrefix == OpcodePrefix_REX2)
    Prefixes |= X86::IP_USE_REX2;
  else if (ForcedOpcodePrefix == OpcodePrefix_VEX)
    Prefixes |= X86::IP_USE_VEX;
  else if (ForcedOpcodePrefix == OpcodePrefix_VEX2)
    Prefixes |= X86::IP_USE_VEX2;
  else if (ForcedOpcodePrefix == OpcodePrefix_VEX3)
    Prefixes |= X86::IP_USE_VEX3;
  else if (ForcedOpcodePrefix == OpcodePrefix_EVEX)
    Prefixes |= X86::IP_USE_EVEX;

  // Set encoded flags for {disp8} and {disp32}.
  if (ForcedDispEncoding == DispEncoding_Disp8)
    Prefixes |= X86::IP_USE_DISP8;
  else if (ForcedDispEncoding == DispEncoding_Disp32)
    Prefixes |= X86::IP_USE_DISP32;

  if (Prefixes)
    Inst.setFlags(Prefixes);

  return isParsingIntelSyntax()
             ? matchAndEmitIntelInstruction(IDLoc, Opcode, Inst, Operands, Out,
                                            ErrorInfo, MatchingInlineAsm)
             : matchAndEmitATTInstruction(IDLoc, Opcode, Inst, Operands, Out,
                                          ErrorInfo, MatchingInlineAsm);
}

void X86AsmParser::MatchFPUWaitAlias(SMLoc IDLoc, X86Operand &Op,
                                     OperandVector &Operands, MCStreamer &Out,
                                     bool MatchingInlineAsm) {
  // FIXME: This should be replaced with a real .td file alias mechanism.
  // Also, MatchInstructionImpl should actually *do* the EmitInstruction
  // call.
  const char *Repl = StringSwitch<const char *>(Op.getToken())
                         .Case("finit", "fninit")
                         .Case("fsave", "fnsave")
                         .Case("fstcw", "fnstcw")
                         .Case("fstcww", "fnstcw")
                         .Case("fstenv", "fnstenv")
                         .Case("fstsw", "fnstsw")
                         .Case("fstsww", "fnstsw")
                         .Case("fclex", "fnclex")
                         .Default(nullptr);
  if (Repl) {
    MCInst Inst;
    Inst.setOpcode(X86::WAIT);
    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      emitInstruction(Inst, Operands, Out);
    Operands[0] = X86Operand::CreateToken(Repl, IDLoc);
  }
}

bool X86AsmParser::ErrorMissingFeature(SMLoc IDLoc,
                                       const FeatureBitset &MissingFeatures,
                                       bool MatchingInlineAsm) {
  assert(MissingFeatures.any() && "Unknown missing feature!");
  SmallString<126> Msg;
  raw_svector_ostream OS(Msg);
  OS << "instruction requires:";
  for (unsigned i = 0, e = MissingFeatures.size(); i != e; ++i) {
    if (MissingFeatures[i])
      OS << ' ' << getSubtargetFeatureName(i);
  }
  return Error(IDLoc, OS.str(), SMRange(), MatchingInlineAsm);
}

unsigned X86AsmParser::checkTargetMatchPredicate(MCInst &Inst) {
  unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &MCID = MII.get(Opc);
  uint64_t TSFlags = MCID.TSFlags;

  if (UseApxExtendedReg && !X86II::canUseApxExtendedReg(MCID))
    return Match_Unsupported;
  if (ForcedNoFlag == !(TSFlags & X86II::EVEX_NF) && !X86::isCFCMOVCC(Opc))
    return Match_Unsupported;

  switch (ForcedOpcodePrefix) {
  case OpcodePrefix_Default:
    break;
  case OpcodePrefix_REX:
  case OpcodePrefix_REX2:
    if (TSFlags & X86II::EncodingMask)
      return Match_Unsupported;
    break;
  case OpcodePrefix_VEX:
  case OpcodePrefix_VEX2:
  case OpcodePrefix_VEX3:
    if ((TSFlags & X86II::EncodingMask) != X86II::VEX)
      return Match_Unsupported;
    break;
  case OpcodePrefix_EVEX:
    if ((TSFlags & X86II::EncodingMask) != X86II::EVEX)
      return Match_Unsupported;
    break;
  }

  if ((TSFlags & X86II::ExplicitOpPrefixMask) == X86II::ExplicitVEXPrefix &&
      (ForcedOpcodePrefix != OpcodePrefix_VEX &&
       ForcedOpcodePrefix != OpcodePrefix_VEX2 &&
       ForcedOpcodePrefix != OpcodePrefix_VEX3))
    return Match_Unsupported;

  return Match_Success;
}

bool X86AsmParser::matchAndEmitATTInstruction(
    SMLoc IDLoc, unsigned &Opcode, MCInst &Inst, OperandVector &Operands,
    MCStreamer &Out, uint64_t &ErrorInfo, bool MatchingInlineAsm) {
  X86Operand &Op = static_cast<X86Operand &>(*Operands[0]);
  SMRange EmptyRange = std::nullopt;
  // In 16-bit mode, if data32 is specified, temporarily switch to 32-bit mode
  // when matching the instruction.
  if (ForcedDataPrefix == X86::Is32Bit)
    SwitchMode(X86::Is32Bit);
  // First, try a direct match.
  FeatureBitset MissingFeatures;
  unsigned OriginalError = MatchInstruction(Operands, Inst, ErrorInfo,
                                            MissingFeatures, MatchingInlineAsm,
                                            isParsingIntelSyntax());
  if (ForcedDataPrefix == X86::Is32Bit) {
    SwitchMode(X86::Is16Bit);
    ForcedDataPrefix = 0;
  }
  switch (OriginalError) {
  default: llvm_unreachable("Unexpected match result!");
  case Match_Success:
    if (!MatchingInlineAsm && validateInstruction(Inst, Operands))
      return true;
    // Some instructions need post-processing to, for example, tweak which
    // encoding is selected. Loop on it while changes happen so the
    // individual transformations can chain off each other.
    if (!MatchingInlineAsm)
      while (processInstruction(Inst, Operands))
        ;

    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      emitInstruction(Inst, Operands, Out);
    Opcode = Inst.getOpcode();
    return false;
  case Match_InvalidImmUnsignedi4: {
    SMLoc ErrorLoc = ((X86Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
    return Error(ErrorLoc, "immediate must be an integer in range [0, 15]",
                 EmptyRange, MatchingInlineAsm);
  }
  case Match_MissingFeature:
    return ErrorMissingFeature(IDLoc, MissingFeatures, MatchingInlineAsm);
  case Match_InvalidOperand:
  case Match_MnemonicFail:
  case Match_Unsupported:
    break;
  }
  if (Op.getToken().empty()) {
    Error(IDLoc, "instruction must have size higher than 0", EmptyRange,
          MatchingInlineAsm);
    return true;
  }

  // FIXME: Ideally, we would only attempt suffix matches for things which are
  // valid prefixes, and we could just infer the right unambiguous
  // type. However, that requires substantially more matcher support than the
  // following hack.

  // Change the operand to point to a temporary token.
  StringRef Base = Op.getToken();
  SmallString<16> Tmp;
  Tmp += Base;
  Tmp += ' ';
  Op.setTokenValue(Tmp);

  // If this instruction starts with an 'f', then it is a floating point stack
  // instruction.  These come in up to three forms for 32-bit, 64-bit, and
  // 80-bit floating point, which use the suffixes s,l,t respectively.
  //
  // Otherwise, we assume that this may be an integer instruction, which comes
  // in 8/16/32/64-bit forms using the b,w,l,q suffixes respectively.
  const char *Suffixes = Base[0] != 'f' ? "bwlq" : "slt\0";
  // MemSize corresponding to Suffixes.  { 8, 16, 32, 64 }    { 32, 64, 80, 0 }
  const char *MemSize = Base[0] != 'f' ? "\x08\x10\x20\x40" : "\x20\x40\x50\0";

  // Check for the various suffix matches.
  uint64_t ErrorInfoIgnore;
  FeatureBitset ErrorInfoMissingFeatures; // Init suppresses compiler warnings.
  unsigned Match[4];

  // Some instruction like VPMULDQ is NOT the variant of VPMULD but a new one.
  // So we should make sure the suffix matcher only works for memory variant
  // that has the same size with the suffix.
  // FIXME: This flag is a workaround for legacy instructions that didn't
  // declare non suffix variant assembly.
  bool HasVectorReg = false;
  X86Operand *MemOp = nullptr;
  for (const auto &Op : Operands) {
    X86Operand *X86Op = static_cast<X86Operand *>(Op.get());
    if (X86Op->isVectorReg())
      HasVectorReg = true;
    else if (X86Op->isMem()) {
      MemOp = X86Op;
      assert(MemOp->Mem.Size == 0 && "Memory size always 0 under ATT syntax");
      // Have we found an unqualified memory operand,
      // break. IA allows only one memory operand.
      break;
    }
  }

  for (unsigned I = 0, E = std::size(Match); I != E; ++I) {
    Tmp.back() = Suffixes[I];
    if (MemOp && HasVectorReg)
      MemOp->Mem.Size = MemSize[I];
    Match[I] = Match_MnemonicFail;
    if (MemOp || !HasVectorReg) {
      Match[I] =
          MatchInstruction(Operands, Inst, ErrorInfoIgnore, MissingFeatures,
                           MatchingInlineAsm, isParsingIntelSyntax());
      // If this returned as a missing feature failure, remember that.
      if (Match[I] == Match_MissingFeature)
        ErrorInfoMissingFeatures = MissingFeatures;
    }
  }

  // Restore the old token.
  Op.setTokenValue(Base);

  // If exactly one matched, then we treat that as a successful match (and the
  // instruction will already have been filled in correctly, since the failing
  // matches won't have modified it).
  unsigned NumSuccessfulMatches = llvm::count(Match, Match_Success);
  if (NumSuccessfulMatches == 1) {
    if (!MatchingInlineAsm && validateInstruction(Inst, Operands))
      return true;
    // Some instructions need post-processing to, for example, tweak which
    // encoding is selected. Loop on it while changes happen so the
    // individual transformations can chain off each other.
    if (!MatchingInlineAsm)
      while (processInstruction(Inst, Operands))
        ;

    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      emitInstruction(Inst, Operands, Out);
    Opcode = Inst.getOpcode();
    return false;
  }

  // Otherwise, the match failed, try to produce a decent error message.

  // If we had multiple suffix matches, then identify this as an ambiguous
  // match.
  if (NumSuccessfulMatches > 1) {
    char MatchChars[4];
    unsigned NumMatches = 0;
    for (unsigned I = 0, E = std::size(Match); I != E; ++I)
      if (Match[I] == Match_Success)
        MatchChars[NumMatches++] = Suffixes[I];

    SmallString<126> Msg;
    raw_svector_ostream OS(Msg);
    OS << "ambiguous instructions require an explicit suffix (could be ";
    for (unsigned i = 0; i != NumMatches; ++i) {
      if (i != 0)
        OS << ", ";
      if (i + 1 == NumMatches)
        OS << "or ";
      OS << "'" << Base << MatchChars[i] << "'";
    }
    OS << ")";
    Error(IDLoc, OS.str(), EmptyRange, MatchingInlineAsm);
    return true;
  }

  // Okay, we know that none of the variants matched successfully.

  // If all of the instructions reported an invalid mnemonic, then the original
  // mnemonic was invalid.
  if (llvm::count(Match, Match_MnemonicFail) == 4) {
    if (OriginalError == Match_MnemonicFail)
      return Error(IDLoc, "invalid instruction mnemonic '" + Base + "'",
                   Op.getLocRange(), MatchingInlineAsm);

    if (OriginalError == Match_Unsupported)
      return Error(IDLoc, "unsupported instruction", EmptyRange,
                   MatchingInlineAsm);

    assert(OriginalError == Match_InvalidOperand && "Unexpected error");
    // Recover location info for the operand if we know which was the problem.
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction", EmptyRange,
                     MatchingInlineAsm);

      X86Operand &Operand = (X86Operand &)*Operands[ErrorInfo];
      if (Operand.getStartLoc().isValid()) {
        SMRange OperandRange = Operand.getLocRange();
        return Error(Operand.getStartLoc(), "invalid operand for instruction",
                     OperandRange, MatchingInlineAsm);
      }
    }

    return Error(IDLoc, "invalid operand for instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If one instruction matched as unsupported, report this as unsupported.
  if (llvm::count(Match, Match_Unsupported) == 1) {
    return Error(IDLoc, "unsupported instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If one instruction matched with a missing feature, report this as a
  // missing feature.
  if (llvm::count(Match, Match_MissingFeature) == 1) {
    ErrorInfo = Match_MissingFeature;
    return ErrorMissingFeature(IDLoc, ErrorInfoMissingFeatures,
                               MatchingInlineAsm);
  }

  // If one instruction matched with an invalid operand, report this as an
  // operand failure.
  if (llvm::count(Match, Match_InvalidOperand) == 1) {
    return Error(IDLoc, "invalid operand for instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If all of these were an outright failure, report it in a useless way.
  Error(IDLoc, "unknown use of instruction mnemonic without a size suffix",
        EmptyRange, MatchingInlineAsm);
  return true;
}

bool X86AsmParser::matchAndEmitIntelInstruction(
    SMLoc IDLoc, unsigned &Opcode, MCInst &Inst, OperandVector &Operands,
    MCStreamer &Out, uint64_t &ErrorInfo, bool MatchingInlineAsm) {
  X86Operand &Op = static_cast<X86Operand &>(*Operands[0]);
  SMRange EmptyRange = std::nullopt;
  // Find one unsized memory operand, if present.
  X86Operand *UnsizedMemOp = nullptr;
  for (const auto &Op : Operands) {
    X86Operand *X86Op = static_cast<X86Operand *>(Op.get());
    if (X86Op->isMemUnsized()) {
      UnsizedMemOp = X86Op;
      // Have we found an unqualified memory operand,
      // break. IA allows only one memory operand.
      break;
    }
  }

  // Allow some instructions to have implicitly pointer-sized operands.  This is
  // compatible with gas.
  StringRef Mnemonic = (static_cast<X86Operand &>(*Operands[0])).getToken();
  if (UnsizedMemOp) {
    static const char *const PtrSizedInstrs[] = {"call", "jmp", "push"};
    for (const char *Instr : PtrSizedInstrs) {
      if (Mnemonic == Instr) {
        UnsizedMemOp->Mem.Size = getPointerWidth();
        break;
      }
    }
  }

  SmallVector<unsigned, 8> Match;
  FeatureBitset ErrorInfoMissingFeatures;
  FeatureBitset MissingFeatures;
  StringRef Base = (static_cast<X86Operand &>(*Operands[0])).getToken();

  // If unsized push has immediate operand we should default the default pointer
  // size for the size.
  if (Mnemonic == "push" && Operands.size() == 2) {
    auto *X86Op = static_cast<X86Operand *>(Operands[1].get());
    if (X86Op->isImm()) {
      // If it's not a constant fall through and let remainder take care of it.
      const auto *CE = dyn_cast<MCConstantExpr>(X86Op->getImm());
      unsigned Size = getPointerWidth();
      if (CE &&
          (isIntN(Size, CE->getValue()) || isUIntN(Size, CE->getValue()))) {
        SmallString<16> Tmp;
        Tmp += Base;
        Tmp += (is64BitMode())
                   ? "q"
                   : (is32BitMode()) ? "l" : (is16BitMode()) ? "w" : " ";
        Op.setTokenValue(Tmp);
        // Do match in ATT mode to allow explicit suffix usage.
        Match.push_back(MatchInstruction(Operands, Inst, ErrorInfo,
                                         MissingFeatures, MatchingInlineAsm,
                                         false /*isParsingIntelSyntax()*/));
        Op.setTokenValue(Base);
      }
    }
  }

  // If an unsized memory operand is present, try to match with each memory
  // operand size.  In Intel assembly, the size is not part of the instruction
  // mnemonic.
  if (UnsizedMemOp && UnsizedMemOp->isMemUnsized()) {
    static const unsigned MopSizes[] = {8, 16, 32, 64, 80, 128, 256, 512};
    for (unsigned Size : MopSizes) {
      UnsizedMemOp->Mem.Size = Size;
      uint64_t ErrorInfoIgnore;
      unsigned LastOpcode = Inst.getOpcode();
      unsigned M = MatchInstruction(Operands, Inst, ErrorInfoIgnore,
                                    MissingFeatures, MatchingInlineAsm,
                                    isParsingIntelSyntax());
      if (Match.empty() || LastOpcode != Inst.getOpcode())
        Match.push_back(M);

      // If this returned as a missing feature failure, remember that.
      if (Match.back() == Match_MissingFeature)
        ErrorInfoMissingFeatures = MissingFeatures;
    }

    // Restore the size of the unsized memory operand if we modified it.
    UnsizedMemOp->Mem.Size = 0;
  }

  // If we haven't matched anything yet, this is not a basic integer or FPU
  // operation.  There shouldn't be any ambiguity in our mnemonic table, so try
  // matching with the unsized operand.
  if (Match.empty()) {
    Match.push_back(MatchInstruction(
        Operands, Inst, ErrorInfo, MissingFeatures, MatchingInlineAsm,
        isParsingIntelSyntax()));
    // If this returned as a missing feature failure, remember that.
    if (Match.back() == Match_MissingFeature)
      ErrorInfoMissingFeatures = MissingFeatures;
  }

  // Restore the size of the unsized memory operand if we modified it.
  if (UnsizedMemOp)
    UnsizedMemOp->Mem.Size = 0;

  // If it's a bad mnemonic, all results will be the same.
  if (Match.back() == Match_MnemonicFail) {
    return Error(IDLoc, "invalid instruction mnemonic '" + Mnemonic + "'",
                 Op.getLocRange(), MatchingInlineAsm);
  }

  unsigned NumSuccessfulMatches = llvm::count(Match, Match_Success);

  // If matching was ambiguous and we had size information from the frontend,
  // try again with that. This handles cases like "movxz eax, m8/m16".
  if (UnsizedMemOp && NumSuccessfulMatches > 1 &&
      UnsizedMemOp->getMemFrontendSize()) {
    UnsizedMemOp->Mem.Size = UnsizedMemOp->getMemFrontendSize();
    unsigned M = MatchInstruction(
        Operands, Inst, ErrorInfo, MissingFeatures, MatchingInlineAsm,
        isParsingIntelSyntax());
    if (M == Match_Success)
      NumSuccessfulMatches = 1;

    // Add a rewrite that encodes the size information we used from the
    // frontend.
    InstInfo->AsmRewrites->emplace_back(
        AOK_SizeDirective, UnsizedMemOp->getStartLoc(),
        /*Len=*/0, UnsizedMemOp->getMemFrontendSize());
  }

  // If exactly one matched, then we treat that as a successful match (and the
  // instruction will already have been filled in correctly, since the failing
  // matches won't have modified it).
  if (NumSuccessfulMatches == 1) {
    if (!MatchingInlineAsm && validateInstruction(Inst, Operands))
      return true;
    // Some instructions need post-processing to, for example, tweak which
    // encoding is selected. Loop on it while changes happen so the individual
    // transformations can chain off each other.
    if (!MatchingInlineAsm)
      while (processInstruction(Inst, Operands))
        ;
    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      emitInstruction(Inst, Operands, Out);
    Opcode = Inst.getOpcode();
    return false;
  } else if (NumSuccessfulMatches > 1) {
    assert(UnsizedMemOp &&
           "multiple matches only possible with unsized memory operands");
    return Error(UnsizedMemOp->getStartLoc(),
                 "ambiguous operand size for instruction '" + Mnemonic + "\'",
                 UnsizedMemOp->getLocRange());
  }

  // If one instruction matched as unsupported, report this as unsupported.
  if (llvm::count(Match, Match_Unsupported) == 1) {
    return Error(IDLoc, "unsupported instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If one instruction matched with a missing feature, report this as a
  // missing feature.
  if (llvm::count(Match, Match_MissingFeature) == 1) {
    ErrorInfo = Match_MissingFeature;
    return ErrorMissingFeature(IDLoc, ErrorInfoMissingFeatures,
                               MatchingInlineAsm);
  }

  // If one instruction matched with an invalid operand, report this as an
  // operand failure.
  if (llvm::count(Match, Match_InvalidOperand) == 1) {
    return Error(IDLoc, "invalid operand for instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  if (llvm::count(Match, Match_InvalidImmUnsignedi4) == 1) {
    SMLoc ErrorLoc = ((X86Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
    return Error(ErrorLoc, "immediate must be an integer in range [0, 15]",
                 EmptyRange, MatchingInlineAsm);
  }

  // If all of these were an outright failure, report it in a useless way.
  return Error(IDLoc, "unknown instruction mnemonic", EmptyRange,
               MatchingInlineAsm);
}

bool X86AsmParser::OmitRegisterFromClobberLists(unsigned RegNo) {
  return X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(RegNo);
}

bool X86AsmParser::ParseDirective(AsmToken DirectiveID) {
  MCAsmParser &Parser = getParser();
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal.starts_with(".arch"))
    return parseDirectiveArch();
  if (IDVal.starts_with(".code"))
    return ParseDirectiveCode(IDVal, DirectiveID.getLoc());
  else if (IDVal.starts_with(".att_syntax")) {
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      if (Parser.getTok().getString() == "prefix")
        Parser.Lex();
      else if (Parser.getTok().getString() == "noprefix")
        return Error(DirectiveID.getLoc(), "'.att_syntax noprefix' is not "
                                           "supported: registers must have a "
                                           "'%' prefix in .att_syntax");
    }
    getParser().setAssemblerDialect(0);
    return false;
  } else if (IDVal.starts_with(".intel_syntax")) {
    getParser().setAssemblerDialect(1);
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      if (Parser.getTok().getString() == "noprefix")
        Parser.Lex();
      else if (Parser.getTok().getString() == "prefix")
        return Error(DirectiveID.getLoc(), "'.intel_syntax prefix' is not "
                                           "supported: registers must not have "
                                           "a '%' prefix in .intel_syntax");
    }
    return false;
  } else if (IDVal == ".nops")
    return parseDirectiveNops(DirectiveID.getLoc());
  else if (IDVal == ".even")
    return parseDirectiveEven(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_proc")
    return parseDirectiveFPOProc(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_setframe")
    return parseDirectiveFPOSetFrame(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_pushreg")
    return parseDirectiveFPOPushReg(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_stackalloc")
    return parseDirectiveFPOStackAlloc(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_stackalign")
    return parseDirectiveFPOStackAlign(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_endprologue")
    return parseDirectiveFPOEndPrologue(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_endproc")
    return parseDirectiveFPOEndProc(DirectiveID.getLoc());
  else if (IDVal == ".seh_pushreg" ||
           (Parser.isParsingMasm() && IDVal.equals_insensitive(".pushreg")))
    return parseDirectiveSEHPushReg(DirectiveID.getLoc());
  else if (IDVal == ".seh_setframe" ||
           (Parser.isParsingMasm() && IDVal.equals_insensitive(".setframe")))
    return parseDirectiveSEHSetFrame(DirectiveID.getLoc());
  else if (IDVal == ".seh_savereg" ||
           (Parser.isParsingMasm() && IDVal.equals_insensitive(".savereg")))
    return parseDirectiveSEHSaveReg(DirectiveID.getLoc());
  else if (IDVal == ".seh_savexmm" ||
           (Parser.isParsingMasm() && IDVal.equals_insensitive(".savexmm128")))
    return parseDirectiveSEHSaveXMM(DirectiveID.getLoc());
  else if (IDVal == ".seh_pushframe" ||
           (Parser.isParsingMasm() && IDVal.equals_insensitive(".pushframe")))
    return parseDirectiveSEHPushFrame(DirectiveID.getLoc());

  return true;
}

bool X86AsmParser::parseDirectiveArch() {
  // Ignore .arch for now.
  getParser().parseStringToEndOfStatement();
  return false;
}

/// parseDirectiveNops
///  ::= .nops size[, control]
bool X86AsmParser::parseDirectiveNops(SMLoc L) {
  int64_t NumBytes = 0, Control = 0;
  SMLoc NumBytesLoc, ControlLoc;
  const MCSubtargetInfo& STI = getSTI();
  NumBytesLoc = getTok().getLoc();
  if (getParser().checkForValidSection() ||
      getParser().parseAbsoluteExpression(NumBytes))
    return true;

  if (parseOptionalToken(AsmToken::Comma)) {
    ControlLoc = getTok().getLoc();
    if (getParser().parseAbsoluteExpression(Control))
      return true;
  }
  if (getParser().parseEOL())
    return true;

  if (NumBytes <= 0) {
    Error(NumBytesLoc, "'.nops' directive with non-positive size");
    return false;
  }

  if (Control < 0) {
    Error(ControlLoc, "'.nops' directive with negative NOP size");
    return false;
  }

  /// Emit nops
  getParser().getStreamer().emitNops(NumBytes, Control, L, STI);

  return false;
}

/// parseDirectiveEven
///  ::= .even
bool X86AsmParser::parseDirectiveEven(SMLoc L) {
  if (parseEOL())
    return false;

  const MCSection *Section = getStreamer().getCurrentSectionOnly();
  if (!Section) {
    getStreamer().initSections(false, getSTI());
    Section = getStreamer().getCurrentSectionOnly();
  }
  if (Section->useCodeAlign())
    getStreamer().emitCodeAlignment(Align(2), &getSTI(), 0);
  else
    getStreamer().emitValueToAlignment(Align(2), 0, 1, 0);
  return false;
}

/// ParseDirectiveCode
///  ::= .code16 | .code32 | .code64
bool X86AsmParser::ParseDirectiveCode(StringRef IDVal, SMLoc L) {
  MCAsmParser &Parser = getParser();
  Code16GCC = false;
  if (IDVal == ".code16") {
    Parser.Lex();
    if (!is16BitMode()) {
      SwitchMode(X86::Is16Bit);
      getParser().getStreamer().emitAssemblerFlag(MCAF_Code16);
    }
  } else if (IDVal == ".code16gcc") {
    // .code16gcc parses as if in 32-bit mode, but emits code in 16-bit mode.
    Parser.Lex();
    Code16GCC = true;
    if (!is16BitMode()) {
      SwitchMode(X86::Is16Bit);
      getParser().getStreamer().emitAssemblerFlag(MCAF_Code16);
    }
  } else if (IDVal == ".code32") {
    Parser.Lex();
    if (!is32BitMode()) {
      SwitchMode(X86::Is32Bit);
      getParser().getStreamer().emitAssemblerFlag(MCAF_Code32);
    }
  } else if (IDVal == ".code64") {
    Parser.Lex();
    if (!is64BitMode()) {
      SwitchMode(X86::Is64Bit);
      getParser().getStreamer().emitAssemblerFlag(MCAF_Code64);
    }
  } else {
    Error(L, "unknown directive " + IDVal);
    return false;
  }

  return false;
}

// .cv_fpo_proc foo
bool X86AsmParser::parseDirectiveFPOProc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  StringRef ProcName;
  int64_t ParamsSize;
  if (Parser.parseIdentifier(ProcName))
    return Parser.TokError("expected symbol name");
  if (Parser.parseIntToken(ParamsSize, "expected parameter byte count"))
    return true;
  if (!isUIntN(32, ParamsSize))
    return Parser.TokError("parameters size out of range");
  if (parseEOL())
    return true;
  MCSymbol *ProcSym = getContext().getOrCreateSymbol(ProcName);
  return getTargetStreamer().emitFPOProc(ProcSym, ParamsSize, L);
}

// .cv_fpo_setframe ebp
bool X86AsmParser::parseDirectiveFPOSetFrame(SMLoc L) {
  MCRegister Reg;
  SMLoc DummyLoc;
  if (parseRegister(Reg, DummyLoc, DummyLoc) || parseEOL())
    return true;
  return getTargetStreamer().emitFPOSetFrame(Reg, L);
}

// .cv_fpo_pushreg ebx
bool X86AsmParser::parseDirectiveFPOPushReg(SMLoc L) {
  MCRegister Reg;
  SMLoc DummyLoc;
  if (parseRegister(Reg, DummyLoc, DummyLoc) || parseEOL())
    return true;
  return getTargetStreamer().emitFPOPushReg(Reg, L);
}

// .cv_fpo_stackalloc 20
bool X86AsmParser::parseDirectiveFPOStackAlloc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  int64_t Offset;
  if (Parser.parseIntToken(Offset, "expected offset") || parseEOL())
    return true;
  return getTargetStreamer().emitFPOStackAlloc(Offset, L);
}

// .cv_fpo_stackalign 8
bool X86AsmParser::parseDirectiveFPOStackAlign(SMLoc L) {
  MCAsmParser &Parser = getParser();
  int64_t Offset;
  if (Parser.parseIntToken(Offset, "expected offset") || parseEOL())
    return true;
  return getTargetStreamer().emitFPOStackAlign(Offset, L);
}

// .cv_fpo_endprologue
bool X86AsmParser::parseDirectiveFPOEndPrologue(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (Parser.parseEOL())
    return true;
  return getTargetStreamer().emitFPOEndPrologue(L);
}

// .cv_fpo_endproc
bool X86AsmParser::parseDirectiveFPOEndProc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (Parser.parseEOL())
    return true;
  return getTargetStreamer().emitFPOEndProc(L);
}

bool X86AsmParser::parseSEHRegisterNumber(unsigned RegClassID,
                                          MCRegister &RegNo) {
  SMLoc startLoc = getLexer().getLoc();
  const MCRegisterInfo *MRI = getContext().getRegisterInfo();

  // Try parsing the argument as a register first.
  if (getLexer().getTok().isNot(AsmToken::Integer)) {
    SMLoc endLoc;
    if (parseRegister(RegNo, startLoc, endLoc))
      return true;

    if (!X86MCRegisterClasses[RegClassID].contains(RegNo)) {
      return Error(startLoc,
                   "register is not supported for use with this directive");
    }
  } else {
    // Otherwise, an integer number matching the encoding of the desired
    // register may appear.
    int64_t EncodedReg;
    if (getParser().parseAbsoluteExpression(EncodedReg))
      return true;

    // The SEH register number is the same as the encoding register number. Map
    // from the encoding back to the LLVM register number.
    RegNo = 0;
    for (MCPhysReg Reg : X86MCRegisterClasses[RegClassID]) {
      if (MRI->getEncodingValue(Reg) == EncodedReg) {
        RegNo = Reg;
        break;
      }
    }
    if (RegNo == 0) {
      return Error(startLoc,
                   "incorrect register number for use with this directive");
    }
  }

  return false;
}

bool X86AsmParser::parseDirectiveSEHPushReg(SMLoc Loc) {
  MCRegister Reg;
  if (parseSEHRegisterNumber(X86::GR64RegClassID, Reg))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");

  getParser().Lex();
  getStreamer().emitWinCFIPushReg(Reg, Loc);
  return false;
}

bool X86AsmParser::parseDirectiveSEHSetFrame(SMLoc Loc) {
  MCRegister Reg;
  int64_t Off;
  if (parseSEHRegisterNumber(X86::GR64RegClassID, Reg))
    return true;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify a stack pointer offset");

  getParser().Lex();
  if (getParser().parseAbsoluteExpression(Off))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");

  getParser().Lex();
  getStreamer().emitWinCFISetFrame(Reg, Off, Loc);
  return false;
}

bool X86AsmParser::parseDirectiveSEHSaveReg(SMLoc Loc) {
  MCRegister Reg;
  int64_t Off;
  if (parseSEHRegisterNumber(X86::GR64RegClassID, Reg))
    return true;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify an offset on the stack");

  getParser().Lex();
  if (getParser().parseAbsoluteExpression(Off))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");

  getParser().Lex();
  getStreamer().emitWinCFISaveReg(Reg, Off, Loc);
  return false;
}

bool X86AsmParser::parseDirectiveSEHSaveXMM(SMLoc Loc) {
  MCRegister Reg;
  int64_t Off;
  if (parseSEHRegisterNumber(X86::VR128XRegClassID, Reg))
    return true;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify an offset on the stack");

  getParser().Lex();
  if (getParser().parseAbsoluteExpression(Off))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");

  getParser().Lex();
  getStreamer().emitWinCFISaveXMM(Reg, Off, Loc);
  return false;
}

bool X86AsmParser::parseDirectiveSEHPushFrame(SMLoc Loc) {
  bool Code = false;
  StringRef CodeID;
  if (getLexer().is(AsmToken::At)) {
    SMLoc startLoc = getLexer().getLoc();
    getParser().Lex();
    if (!getParser().parseIdentifier(CodeID)) {
      if (CodeID != "code")
        return Error(startLoc, "expected @code");
      Code = true;
    }
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");

  getParser().Lex();
  getStreamer().emitWinCFIPushFrame(Code, Loc);
  return false;
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeX86AsmParser() {
  RegisterMCAsmParser<X86AsmParser> X(getTheX86_32Target());
  RegisterMCAsmParser<X86AsmParser> Y(getTheX86_64Target());
}

#define GET_MATCHER_IMPLEMENTATION
#include "X86GenAsmMatcher.inc"
