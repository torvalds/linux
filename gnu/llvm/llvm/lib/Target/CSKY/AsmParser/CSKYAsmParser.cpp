//===---- CSKYAsmParser.cpp - Parse CSKY assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/CSKYInstPrinter.h"
#include "MCTargetDesc/CSKYMCExpr.h"
#include "MCTargetDesc/CSKYMCTargetDesc.h"
#include "MCTargetDesc/CSKYTargetStreamer.h"
#include "TargetInfo/CSKYTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CSKYAttributes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/TargetParser/CSKYTargetParser.h"

using namespace llvm;

#define DEBUG_TYPE "csky-asm-parser"

// Include the auto-generated portion of the compress emitter.
#define GEN_COMPRESS_INSTR
#include "CSKYGenCompressInstEmitter.inc"

STATISTIC(CSKYNumInstrsCompressed,
          "Number of C-SKY Compressed instructions emitted");

static cl::opt<bool>
    EnableCompressedInst("enable-csky-asm-compressed-inst", cl::Hidden,
                         cl::init(false),
                         cl::desc("Enable C-SKY asm compressed instruction"));

namespace {
struct CSKYOperand;

class CSKYAsmParser : public MCTargetAsmParser {

  const MCRegisterInfo *MRI;

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  bool generateImmOutOfRangeError(OperandVector &Operands, uint64_t ErrorInfo,
                                  int64_t Lower, int64_t Upper,
                                  const Twine &Msg);

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;

  // Helper to actually emit an instruction to the MCStreamer. Also, when
  // possible, compression of the instruction is performed.
  void emitToStreamer(MCStreamer &S, const MCInst &Inst);

  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool processInstruction(MCInst &Inst, SMLoc IDLoc, OperandVector &Operands,
                          MCStreamer &Out);
  bool processLRW(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);
  bool processJSRI(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);
  bool processJMPI(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  CSKYTargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<CSKYTargetStreamer &>(TS);
  }

// Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "CSKYGenAsmMatcher.inc"

  ParseStatus parseImmediate(OperandVector &Operands);
  ParseStatus parseRegister(OperandVector &Operands);
  ParseStatus parseBaseRegImm(OperandVector &Operands);
  ParseStatus parseCSKYSymbol(OperandVector &Operands);
  ParseStatus parseConstpoolSymbol(OperandVector &Operands);
  ParseStatus parseDataSymbol(OperandVector &Operands);
  ParseStatus parsePSRFlag(OperandVector &Operands);
  ParseStatus parseRegSeq(OperandVector &Operands);
  ParseStatus parseRegList(OperandVector &Operands);

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);

  bool parseDirectiveAttribute();

public:
  enum CSKYMatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
    Match_RequiresSameSrcAndDst,
    Match_InvalidRegOutOfRange,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "CSKYGenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  CSKYAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {

    MCAsmParserExtension::Initialize(Parser);

    // Cache the MCRegisterInfo.
    MRI = getContext().getRegisterInfo();

    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
    getTargetStreamer().emitTargetAttributes(STI);
  }
};

/// Instances of this class represent a parsed machine instruction.
struct CSKYOperand : public MCParsedAsmOperand {

  enum KindTy {
    Token,
    Register,
    Immediate,
    RegisterSeq,
    CPOP,
    RegisterList
  } Kind;

  struct RegOp {
    unsigned RegNum;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct ConstpoolOp {
    const MCExpr *Val;
  };

  struct RegSeqOp {
    unsigned RegNumFrom;
    unsigned RegNumTo;
  };

  struct RegListOp {
    unsigned List1From = 0;
    unsigned List1To = 0;
    unsigned List2From = 0;
    unsigned List2To = 0;
    unsigned List3From = 0;
    unsigned List3To = 0;
    unsigned List4From = 0;
    unsigned List4To = 0;
  };

  SMLoc StartLoc, EndLoc;
  union {
    StringRef Tok;
    RegOp Reg;
    ImmOp Imm;
    ConstpoolOp CPool;
    RegSeqOp RegSeq;
    RegListOp RegList;
  };

  CSKYOperand(KindTy K) : MCParsedAsmOperand(), Kind(K) {}

public:
  CSKYOperand(const CSKYOperand &o) : MCParsedAsmOperand() {
    Kind = o.Kind;
    StartLoc = o.StartLoc;
    EndLoc = o.EndLoc;
    switch (Kind) {
    case Register:
      Reg = o.Reg;
      break;
    case RegisterSeq:
      RegSeq = o.RegSeq;
      break;
    case CPOP:
      CPool = o.CPool;
      break;
    case Immediate:
      Imm = o.Imm;
      break;
    case Token:
      Tok = o.Tok;
      break;
    case RegisterList:
      RegList = o.RegList;
      break;
    }
  }

  bool isToken() const override { return Kind == Token; }
  bool isReg() const override { return Kind == Register; }
  bool isImm() const override { return Kind == Immediate; }
  bool isRegisterSeq() const { return Kind == RegisterSeq; }
  bool isRegisterList() const { return Kind == RegisterList; }
  bool isConstPoolOp() const { return Kind == CPOP; }

  bool isMem() const override { return false; }

  static bool evaluateConstantImm(const MCExpr *Expr, int64_t &Imm) {
    if (auto CE = dyn_cast<MCConstantExpr>(Expr)) {
      Imm = CE->getValue();
      return true;
    }

    return false;
  }

  template <unsigned num, unsigned shift = 0> bool isUImm() const {
    if (!isImm())
      return false;

    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm);
    return IsConstantImm && isShiftedUInt<num, shift>(Imm);
  }

  template <unsigned num> bool isOImm() const {
    if (!isImm())
      return false;

    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm);
    return IsConstantImm && isUInt<num>(Imm - 1);
  }

  template <unsigned num, unsigned shift = 0> bool isSImm() const {
    if (!isImm())
      return false;

    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm);
    return IsConstantImm && isShiftedInt<num, shift>(Imm);
  }

  bool isUImm1() const { return isUImm<1>(); }
  bool isUImm2() const { return isUImm<2>(); }
  bool isUImm3() const { return isUImm<3>(); }
  bool isUImm4() const { return isUImm<4>(); }
  bool isUImm5() const { return isUImm<5>(); }
  bool isUImm6() const { return isUImm<6>(); }
  bool isUImm7() const { return isUImm<7>(); }
  bool isUImm8() const { return isUImm<8>(); }
  bool isUImm12() const { return isUImm<12>(); }
  bool isUImm16() const { return isUImm<16>(); }
  bool isUImm20() const { return isUImm<20>(); }
  bool isUImm24() const { return isUImm<24>(); }

  bool isOImm3() const { return isOImm<3>(); }
  bool isOImm4() const { return isOImm<4>(); }
  bool isOImm5() const { return isOImm<5>(); }
  bool isOImm6() const { return isOImm<6>(); }
  bool isOImm8() const { return isOImm<8>(); }
  bool isOImm12() const { return isOImm<12>(); }
  bool isOImm16() const { return isOImm<16>(); }

  bool isSImm8() const { return isSImm<8>(); }

  bool isUImm5Shift1() { return isUImm<5, 1>(); }
  bool isUImm5Shift2() { return isUImm<5, 2>(); }
  bool isUImm7Shift1() { return isUImm<7, 1>(); }
  bool isUImm7Shift2() { return isUImm<7, 2>(); }
  bool isUImm7Shift3() { return isUImm<7, 3>(); }
  bool isUImm8Shift2() { return isUImm<8, 2>(); }
  bool isUImm8Shift3() { return isUImm<8, 3>(); }
  bool isUImm8Shift8() { return isUImm<8, 8>(); }
  bool isUImm8Shift16() { return isUImm<8, 16>(); }
  bool isUImm8Shift24() { return isUImm<8, 24>(); }
  bool isUImm12Shift1() { return isUImm<12, 1>(); }
  bool isUImm12Shift2() { return isUImm<12, 2>(); }
  bool isUImm16Shift8() { return isUImm<16, 8>(); }
  bool isUImm16Shift16() { return isUImm<16, 16>(); }
  bool isUImm24Shift8() { return isUImm<24, 8>(); }

  bool isSImm16Shift1() { return isSImm<16, 1>(); }

  bool isCSKYSymbol() const { return isImm(); }

  bool isConstpool() const { return isConstPoolOp(); }
  bool isDataSymbol() const { return isConstPoolOp(); }

  bool isPSRFlag() const {
    int64_t Imm;
    // Must be of 'immediate' type and a constant.
    if (!isImm() || !evaluateConstantImm(getImm(), Imm))
      return false;

    return isUInt<5>(Imm);
  }

  template <unsigned MIN, unsigned MAX> bool isRegSeqTemplate() const {
    if (!isRegisterSeq())
      return false;

    std::pair<unsigned, unsigned> regSeq = getRegSeq();

    return MIN <= regSeq.first && regSeq.first <= regSeq.second &&
           regSeq.second <= MAX;
  }

  bool isRegSeq() const { return isRegSeqTemplate<CSKY::R0, CSKY::R31>(); }

  bool isRegSeqV1() const {
    return isRegSeqTemplate<CSKY::F0_32, CSKY::F15_32>();
  }

  bool isRegSeqV2() const {
    return isRegSeqTemplate<CSKY::F0_32, CSKY::F31_32>();
  }

  static bool isLegalRegList(unsigned from, unsigned to) {
    if (from == 0 && to == 0)
      return true;

    if (from == to) {
      if (from != CSKY::R4 && from != CSKY::R15 && from != CSKY::R16 &&
          from != CSKY::R28)
        return false;

      return true;
    } else {
      if (from != CSKY::R4 && from != CSKY::R16)
        return false;

      if (from == CSKY::R4 && to > CSKY::R4 && to < CSKY::R12)
        return true;
      else if (from == CSKY::R16 && to > CSKY::R16 && to < CSKY::R18)
        return true;
      else
        return false;
    }
  }

  bool isRegList() const {
    if (!isRegisterList())
      return false;

    auto regList = getRegList();

    if (!isLegalRegList(regList.List1From, regList.List1To))
      return false;
    if (!isLegalRegList(regList.List2From, regList.List2To))
      return false;
    if (!isLegalRegList(regList.List3From, regList.List3To))
      return false;
    if (!isLegalRegList(regList.List4From, regList.List4To))
      return false;

    return true;
  }

  bool isExtImm6() {
    if (!isImm())
      return false;

    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm);
    if (!IsConstantImm)
      return false;

    int uimm4 = Imm & 0xf;

    return isShiftedUInt<6, 0>(Imm) && uimm4 >= 0 && uimm4 <= 14;
  }

  /// Gets location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }
  /// Gets location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }

  MCRegister getReg() const override {
    assert(Kind == Register && "Invalid type access!");
    return Reg.RegNum;
  }

  std::pair<unsigned, unsigned> getRegSeq() const {
    assert(Kind == RegisterSeq && "Invalid type access!");
    return std::pair<unsigned, unsigned>(RegSeq.RegNumFrom, RegSeq.RegNumTo);
  }

  RegListOp getRegList() const {
    assert(Kind == RegisterList && "Invalid type access!");
    return RegList;
  }

  const MCExpr *getImm() const {
    assert(Kind == Immediate && "Invalid type access!");
    return Imm.Val;
  }

  const MCExpr *getConstpoolOp() const {
    assert(Kind == CPOP && "Invalid type access!");
    return CPool.Val;
  }

  StringRef getToken() const {
    assert(Kind == Token && "Invalid type access!");
    return Tok;
  }

  void print(raw_ostream &OS) const override {
    auto RegName = [](MCRegister Reg) {
      if (Reg)
        return CSKYInstPrinter::getRegisterName(Reg);
      else
        return "noreg";
    };

    switch (Kind) {
    case CPOP:
      OS << *getConstpoolOp();
      break;
    case Immediate:
      OS << *getImm();
      break;
    case KindTy::Register:
      OS << "<register " << RegName(getReg()) << ">";
      break;
    case RegisterSeq:
      OS << "<register-seq ";
      OS << RegName(getRegSeq().first) << "-" << RegName(getRegSeq().second)
         << ">";
      break;
    case RegisterList:
      OS << "<register-list ";
      OS << RegName(getRegList().List1From) << "-"
         << RegName(getRegList().List1To) << ",";
      OS << RegName(getRegList().List2From) << "-"
         << RegName(getRegList().List2To) << ",";
      OS << RegName(getRegList().List3From) << "-"
         << RegName(getRegList().List3To) << ",";
      OS << RegName(getRegList().List4From) << "-"
         << RegName(getRegList().List4To);
      break;
    case Token:
      OS << "'" << getToken() << "'";
      break;
    }
  }

  static std::unique_ptr<CSKYOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<CSKYOperand>(Token);
    Op->Tok = Str;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<CSKYOperand> createReg(unsigned RegNo, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<CSKYOperand>(Register);
    Op->Reg.RegNum = RegNo;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<CSKYOperand> createRegSeq(unsigned RegNoFrom,
                                                   unsigned RegNoTo, SMLoc S) {
    auto Op = std::make_unique<CSKYOperand>(RegisterSeq);
    Op->RegSeq.RegNumFrom = RegNoFrom;
    Op->RegSeq.RegNumTo = RegNoTo;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<CSKYOperand>
  createRegList(SmallVector<unsigned, 4> reglist, SMLoc S) {
    auto Op = std::make_unique<CSKYOperand>(RegisterList);
    Op->RegList.List1From = 0;
    Op->RegList.List1To = 0;
    Op->RegList.List2From = 0;
    Op->RegList.List2To = 0;
    Op->RegList.List3From = 0;
    Op->RegList.List3To = 0;
    Op->RegList.List4From = 0;
    Op->RegList.List4To = 0;

    for (unsigned i = 0; i < reglist.size(); i += 2) {
      if (Op->RegList.List1From == 0) {
        Op->RegList.List1From = reglist[i];
        Op->RegList.List1To = reglist[i + 1];
      } else if (Op->RegList.List2From == 0) {
        Op->RegList.List2From = reglist[i];
        Op->RegList.List2To = reglist[i + 1];
      } else if (Op->RegList.List3From == 0) {
        Op->RegList.List3From = reglist[i];
        Op->RegList.List3To = reglist[i + 1];
      } else if (Op->RegList.List4From == 0) {
        Op->RegList.List4From = reglist[i];
        Op->RegList.List4To = reglist[i + 1];
      } else {
        assert(0);
      }
    }

    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<CSKYOperand> createImm(const MCExpr *Val, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<CSKYOperand>(Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<CSKYOperand> createConstpoolOp(const MCExpr *Val,
                                                        SMLoc S, SMLoc E) {
    auto Op = std::make_unique<CSKYOperand>(CPOP);
    Op->CPool.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    assert(Expr && "Expr shouldn't be null!");
    if (auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  // Used by the TableGen Code.
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addConstpoolOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createExpr(getConstpoolOp()));
  }

  void addRegSeqOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    auto regSeq = getRegSeq();

    Inst.addOperand(MCOperand::createReg(regSeq.first));
    Inst.addOperand(MCOperand::createReg(regSeq.second));
  }

  static unsigned getListValue(unsigned ListFrom, unsigned ListTo) {
    if (ListFrom == ListTo && ListFrom == CSKY::R15)
      return (1 << 4);
    else if (ListFrom == ListTo && ListFrom == CSKY::R28)
      return (1 << 8);
    else if (ListFrom == CSKY::R4)
      return ListTo - ListFrom + 1;
    else if (ListFrom == CSKY::R16)
      return ((ListTo - ListFrom + 1) << 5);
    else
      return 0;
  }

  void addRegListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    auto regList = getRegList();

    unsigned V = 0;

    unsigned T = getListValue(regList.List1From, regList.List1To);
    if (T != 0)
      V = V | T;

    T = getListValue(regList.List2From, regList.List2To);
    if (T != 0)
      V = V | T;

    T = getListValue(regList.List3From, regList.List3To);
    if (T != 0)
      V = V | T;

    T = getListValue(regList.List4From, regList.List4To);
    if (T != 0)
      V = V | T;

    Inst.addOperand(MCOperand::createImm(V));
  }

  bool isValidForTie(const CSKYOperand &Other) const {
    if (Kind != Other.Kind)
      return false;

    switch (Kind) {
    default:
      llvm_unreachable("Unexpected kind");
      return false;
    case Register:
      return Reg.RegNum == Other.Reg.RegNum;
    }
  }
};
} // end anonymous namespace.

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "CSKYGenAsmMatcher.inc"

static MCRegister convertFPR32ToFPR64(MCRegister Reg) {
  assert(Reg >= CSKY::F0_32 && Reg <= CSKY::F31_32 && "Invalid register");
  return Reg - CSKY::F0_32 + CSKY::F0_64;
}

static std::string CSKYMnemonicSpellCheck(StringRef S, const FeatureBitset &FBS,
                                          unsigned VariantID = 0);

bool CSKYAsmParser::generateImmOutOfRangeError(
    OperandVector &Operands, uint64_t ErrorInfo, int64_t Lower, int64_t Upper,
    const Twine &Msg = "immediate must be an integer in the range") {
  SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
  return Error(ErrorLoc, Msg + " [" + Twine(Lower) + ", " + Twine(Upper) + "]");
}

bool CSKYAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                            OperandVector &Operands,
                                            MCStreamer &Out,
                                            uint64_t &ErrorInfo,
                                            bool MatchingInlineAsm) {
  MCInst Inst;
  FeatureBitset MissingFeatures;

  auto Result = MatchInstructionImpl(Operands, Inst, ErrorInfo, MissingFeatures,
                                     MatchingInlineAsm);
  switch (Result) {
  default:
    break;
  case Match_Success:
    return processInstruction(Inst, IDLoc, Operands, Out);
  case Match_MissingFeature: {
    assert(MissingFeatures.any() && "Unknown missing features!");
    ListSeparator LS;
    std::string Msg = "instruction requires the following: ";
    for (unsigned i = 0, e = MissingFeatures.size(); i != e; ++i) {
      if (MissingFeatures[i]) {
        Msg += LS;
        Msg += getSubtargetFeatureName(i);
      }
    }
    return Error(IDLoc, Msg);
  }
  case Match_MnemonicFail: {
    FeatureBitset FBS = ComputeAvailableFeatures(getSTI().getFeatureBits());
    std::string Suggestion =
        CSKYMnemonicSpellCheck(((CSKYOperand &)*Operands[0]).getToken(), FBS);
    return Error(IDLoc, "unrecognized instruction mnemonic" + Suggestion);
  }
  case Match_InvalidTiedOperand:
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0U) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");

      ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  }

  // Handle the case when the error message is of specific type
  // other than the generic Match_InvalidOperand, and the
  // corresponding operand is missing.
  if (Result > FIRST_TARGET_MATCH_RESULT_TY) {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0U && ErrorInfo >= Operands.size())
      return Error(ErrorLoc, "too few operands for instruction");
  }

  switch (Result) {
  default:
    break;
  case Match_InvalidSImm8:
    return generateImmOutOfRangeError(Operands, ErrorInfo, -(1 << 7),
                                      (1 << 7) - 1);
  case Match_InvalidOImm3:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 3));
  case Match_InvalidOImm4:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 4));
  case Match_InvalidOImm5:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 5));
  case Match_InvalidOImm6:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 6));
  case Match_InvalidOImm8:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 8));
  case Match_InvalidOImm12:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 12));
  case Match_InvalidOImm16:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 16));
  case Match_InvalidUImm1:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 1) - 1);
  case Match_InvalidUImm2:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 2) - 1);
  case Match_InvalidUImm3:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 3) - 1);
  case Match_InvalidUImm4:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 4) - 1);
  case Match_InvalidUImm5:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 5) - 1);
  case Match_InvalidUImm6:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 6) - 1);
  case Match_InvalidUImm7:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 7) - 1);
  case Match_InvalidUImm8:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 8) - 1);
  case Match_InvalidUImm12:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 12) - 1);
  case Match_InvalidUImm16:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 16) - 1);
  case Match_InvalidUImm5Shift1:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 5) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm12Shift1:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 12) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm5Shift2:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 5) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidUImm7Shift1:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 7) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm7Shift2:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 7) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidUImm8Shift2:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 8) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidUImm8Shift3:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 8) - 8,
        "immediate must be a multiple of 8 bytes in the range");
  case Match_InvalidUImm8Shift8:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 8) - 256,
        "immediate must be a multiple of 256 bytes in the range");
  case Match_InvalidUImm12Shift2:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 12) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidCSKYSymbol: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a symbol name");
  }
  case Match_InvalidConstpool: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a constpool symbol name");
  }
  case Match_InvalidPSRFlag: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "psrset operand is not valid");
  }
  case Match_InvalidRegSeq: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "Register sequence is not valid");
  }
  case Match_InvalidRegOutOfRange: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "register is out of range");
  }
  case Match_RequiresSameSrcAndDst: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "src and dst operand must be same");
  }
  case Match_InvalidRegList: {
    SMLoc ErrorLoc = ((CSKYOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "invalid register list");
  }
  }
  LLVM_DEBUG(dbgs() << "Result = " << Result);
  llvm_unreachable("Unknown match type detected!");
}

bool CSKYAsmParser::processLRW(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out) {
  Inst.setLoc(IDLoc);

  unsigned Opcode;
  MCOperand Op;
  if (Inst.getOpcode() == CSKY::PseudoLRW16)
    Opcode = CSKY::LRW16;
  else
    Opcode = CSKY::LRW32;

  if (Inst.getOperand(1).isImm()) {
    if (isUInt<8>(Inst.getOperand(1).getImm()) &&
        Inst.getOperand(0).getReg() <= CSKY::R7) {
      Opcode = CSKY::MOVI16;
    } else if (getSTI().hasFeature(CSKY::HasE2) &&
               isUInt<16>(Inst.getOperand(1).getImm())) {
      Opcode = CSKY::MOVI32;
    } else {
      auto *Expr = getTargetStreamer().addConstantPoolEntry(
          MCConstantExpr::create(Inst.getOperand(1).getImm(), getContext()),
          Inst.getLoc());
      Inst.erase(std::prev(Inst.end()));
      Inst.addOperand(MCOperand::createExpr(Expr));
    }
  } else {
    const MCExpr *AdjustExpr = nullptr;
    if (const CSKYMCExpr *CSKYExpr =
            dyn_cast<CSKYMCExpr>(Inst.getOperand(1).getExpr())) {
      if (CSKYExpr->getKind() == CSKYMCExpr::VK_CSKY_TLSGD ||
          CSKYExpr->getKind() == CSKYMCExpr::VK_CSKY_TLSIE ||
          CSKYExpr->getKind() == CSKYMCExpr::VK_CSKY_TLSLDM) {
        MCSymbol *Dot = getContext().createNamedTempSymbol();
        Out.emitLabel(Dot);
        AdjustExpr = MCSymbolRefExpr::create(Dot, getContext());
      }
    }
    auto *Expr = getTargetStreamer().addConstantPoolEntry(
        Inst.getOperand(1).getExpr(), Inst.getLoc(), AdjustExpr);
    Inst.erase(std::prev(Inst.end()));
    Inst.addOperand(MCOperand::createExpr(Expr));
  }

  Inst.setOpcode(Opcode);

  Out.emitInstruction(Inst, getSTI());
  return false;
}

bool CSKYAsmParser::processJSRI(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out) {
  Inst.setLoc(IDLoc);

  if (Inst.getOperand(0).isImm()) {
    const MCExpr *Expr = getTargetStreamer().addConstantPoolEntry(
        MCConstantExpr::create(Inst.getOperand(0).getImm(), getContext()),
        Inst.getLoc());
    Inst.setOpcode(CSKY::JSRI32);
    Inst.erase(std::prev(Inst.end()));
    Inst.addOperand(MCOperand::createExpr(Expr));
  } else {
    const MCExpr *Expr = getTargetStreamer().addConstantPoolEntry(
        Inst.getOperand(0).getExpr(), Inst.getLoc());
    Inst.setOpcode(CSKY::JBSR32);
    Inst.addOperand(MCOperand::createExpr(Expr));
  }

  Out.emitInstruction(Inst, getSTI());
  return false;
}

bool CSKYAsmParser::processJMPI(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out) {
  Inst.setLoc(IDLoc);

  if (Inst.getOperand(0).isImm()) {
    const MCExpr *Expr = getTargetStreamer().addConstantPoolEntry(
        MCConstantExpr::create(Inst.getOperand(0).getImm(), getContext()),
        Inst.getLoc());
    Inst.setOpcode(CSKY::JMPI32);
    Inst.erase(std::prev(Inst.end()));
    Inst.addOperand(MCOperand::createExpr(Expr));
  } else {
    const MCExpr *Expr = getTargetStreamer().addConstantPoolEntry(
        Inst.getOperand(0).getExpr(), Inst.getLoc());
    Inst.setOpcode(CSKY::JBR32);
    Inst.addOperand(MCOperand::createExpr(Expr));
  }

  Out.emitInstruction(Inst, getSTI());
  return false;
}

bool CSKYAsmParser::processInstruction(MCInst &Inst, SMLoc IDLoc,
                                       OperandVector &Operands,
                                       MCStreamer &Out) {

  switch (Inst.getOpcode()) {
  default:
    break;
  case CSKY::LDQ32:
  case CSKY::STQ32:
    if (Inst.getOperand(1).getReg() != CSKY::R4 ||
        Inst.getOperand(2).getReg() != CSKY::R7) {
      return Error(IDLoc, "Register sequence is not valid. 'r4-r7' expected");
    }
    Inst.setOpcode(Inst.getOpcode() == CSKY::LDQ32 ? CSKY::LDM32 : CSKY::STM32);
    break;
  case CSKY::SEXT32:
  case CSKY::ZEXT32:
    if (Inst.getOperand(2).getImm() < Inst.getOperand(3).getImm())
      return Error(IDLoc, "msb must be greater or equal to lsb");
    break;
  case CSKY::INS32:
    if (Inst.getOperand(3).getImm() < Inst.getOperand(4).getImm())
      return Error(IDLoc, "msb must be greater or equal to lsb");
    break;
  case CSKY::IDLY32:
    if (Inst.getOperand(0).getImm() > 32 || Inst.getOperand(0).getImm() < 0)
      return Error(IDLoc, "n must be in range [0,32]");
    break;
  case CSKY::ADDC32:
  case CSKY::SUBC32:
  case CSKY::ADDC16:
  case CSKY::SUBC16:
    Inst.erase(std::next(Inst.begin()));
    Inst.erase(std::prev(Inst.end()));
    Inst.insert(std::next(Inst.begin()), MCOperand::createReg(CSKY::C));
    Inst.insert(Inst.end(), MCOperand::createReg(CSKY::C));
    break;
  case CSKY::CMPNEI32:
  case CSKY::CMPNEI16:
  case CSKY::CMPNE32:
  case CSKY::CMPNE16:
  case CSKY::CMPHSI32:
  case CSKY::CMPHSI16:
  case CSKY::CMPHS32:
  case CSKY::CMPHS16:
  case CSKY::CMPLTI32:
  case CSKY::CMPLTI16:
  case CSKY::CMPLT32:
  case CSKY::CMPLT16:
  case CSKY::BTSTI32:
    Inst.erase(Inst.begin());
    Inst.insert(Inst.begin(), MCOperand::createReg(CSKY::C));
    break;
  case CSKY::MVCV32:
    Inst.erase(std::next(Inst.begin()));
    Inst.insert(Inst.end(), MCOperand::createReg(CSKY::C));
    break;
  case CSKY::PseudoLRW16:
  case CSKY::PseudoLRW32:
    return processLRW(Inst, IDLoc, Out);
  case CSKY::PseudoJSRI32:
    return processJSRI(Inst, IDLoc, Out);
  case CSKY::PseudoJMPI32:
    return processJMPI(Inst, IDLoc, Out);
  case CSKY::JBSR32:
  case CSKY::JBR16:
  case CSKY::JBT16:
  case CSKY::JBF16:
  case CSKY::JBR32:
  case CSKY::JBT32:
  case CSKY::JBF32:
    unsigned Num = Inst.getNumOperands() - 1;
    assert(Inst.getOperand(Num).isExpr());

    const MCExpr *Expr = getTargetStreamer().addConstantPoolEntry(
        Inst.getOperand(Num).getExpr(), Inst.getLoc());

    Inst.addOperand(MCOperand::createExpr(Expr));
    break;
  }

  emitToStreamer(Out, Inst);
  return false;
}

// Attempts to match Name as a register (either using the default name or
// alternative ABI names), setting RegNo to the matching register. Upon
// failure, returns true and sets RegNo to 0.
static bool matchRegisterNameHelper(const MCSubtargetInfo &STI, MCRegister &Reg,
                                    StringRef Name) {
  Reg = MatchRegisterName(Name);

  if (Reg == CSKY::NoRegister)
    Reg = MatchRegisterAltName(Name);

  return Reg == CSKY::NoRegister;
}

bool CSKYAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                  SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  StringRef Name = getLexer().getTok().getIdentifier();

  if (!matchRegisterNameHelper(getSTI(), Reg, Name)) {
    getParser().Lex(); // Eat identifier token.
    return false;
  }

  return true;
}

ParseStatus CSKYAsmParser::parseRegister(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::Identifier: {
    StringRef Name = getLexer().getTok().getIdentifier();
    MCRegister Reg;

    if (matchRegisterNameHelper(getSTI(), Reg, Name))
      return ParseStatus::NoMatch;

    getLexer().Lex();
    Operands.push_back(CSKYOperand::createReg(Reg, S, E));

    return ParseStatus::Success;
  }
  }
}

ParseStatus CSKYAsmParser::parseBaseRegImm(OperandVector &Operands) {
  assert(getLexer().is(AsmToken::LParen));

  Operands.push_back(CSKYOperand::createToken("(", getLoc()));

  auto Tok = getParser().Lex(); // Eat '('

  if (!parseRegister(Operands).isSuccess()) {
    getLexer().UnLex(Tok);
    Operands.pop_back();
    return ParseStatus::NoMatch;
  }

  if (getLexer().is(AsmToken::RParen)) {
    Operands.push_back(CSKYOperand::createToken(")", getLoc()));
    getParser().Lex(); // Eat ')'
    return ParseStatus::Success;
  }

  if (getLexer().isNot(AsmToken::Comma))
    return Error(getLoc(), "expected ','");

  getParser().Lex(); // Eat ','

  if (parseRegister(Operands).isSuccess()) {
    if (getLexer().isNot(AsmToken::LessLess))
      return Error(getLoc(), "expected '<<'");

    Operands.push_back(CSKYOperand::createToken("<<", getLoc()));

    getParser().Lex(); // Eat '<<'

    if (!parseImmediate(Operands).isSuccess())
      return Error(getLoc(), "expected imm");

  } else if (!parseImmediate(Operands).isSuccess()) {
    return Error(getLoc(), "expected imm");
  }

  if (getLexer().isNot(AsmToken::RParen))
    return Error(getLoc(), "expected ')'");

  Operands.push_back(CSKYOperand::createToken(")", getLoc()));

  getParser().Lex(); // Eat ')'

  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parseImmediate(OperandVector &Operands) {
  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Integer:
  case AsmToken::String:
    break;
  }

  const MCExpr *IdVal;
  SMLoc S = getLoc();
  if (getParser().parseExpression(IdVal))
    return Error(getLoc(), "unknown expression");

  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);
  Operands.push_back(CSKYOperand::createImm(IdVal, S, E));
  return ParseStatus::Success;
}

/// Looks at a token type and creates the relevant operand from this
/// information, adding to Operands. If operand was parsed, returns false, else
/// true.
bool CSKYAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  ParseStatus Result =
      MatchOperandParserImpl(Operands, Mnemonic, /*ParseForAllFeatures=*/true);
  if (Result.isSuccess())
    return false;
  if (Result.isFailure())
    return true;

  // Attempt to parse token as register
  auto Res = parseRegister(Operands);
  if (Res.isSuccess())
    return false;
  if (Res.isFailure())
    return true;

  // Attempt to parse token as (register, imm)
  if (getLexer().is(AsmToken::LParen)) {
    Res = parseBaseRegImm(Operands);
    if (Res.isSuccess())
      return false;
    if (Res.isFailure())
      return true;
  }

  Res = parseImmediate(Operands);
  if (Res.isSuccess())
    return false;
  if (Res.isFailure())
    return true;

  // Finally we have exhausted all options and must declare defeat.
  Error(getLoc(), "unknown operand");
  return true;
}

ParseStatus CSKYAsmParser::parseCSKYSymbol(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);
  const MCExpr *Res;

  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::NoMatch;

  StringRef Identifier;
  AsmToken Tok = getLexer().getTok();

  if (getParser().parseIdentifier(Identifier))
    return Error(getLoc(), "unknown identifier");

  CSKYMCExpr::VariantKind Kind = CSKYMCExpr::VK_CSKY_None;
  if (Identifier.consume_back("@GOT"))
    Kind = CSKYMCExpr::VK_CSKY_GOT;
  else if (Identifier.consume_back("@GOTOFF"))
    Kind = CSKYMCExpr::VK_CSKY_GOTOFF;
  else if (Identifier.consume_back("@PLT"))
    Kind = CSKYMCExpr::VK_CSKY_PLT;
  else if (Identifier.consume_back("@GOTPC"))
    Kind = CSKYMCExpr::VK_CSKY_GOTPC;
  else if (Identifier.consume_back("@TLSGD32"))
    Kind = CSKYMCExpr::VK_CSKY_TLSGD;
  else if (Identifier.consume_back("@GOTTPOFF"))
    Kind = CSKYMCExpr::VK_CSKY_TLSIE;
  else if (Identifier.consume_back("@TPOFF"))
    Kind = CSKYMCExpr::VK_CSKY_TLSLE;
  else if (Identifier.consume_back("@TLSLDM32"))
    Kind = CSKYMCExpr::VK_CSKY_TLSLDM;
  else if (Identifier.consume_back("@TLSLDO32"))
    Kind = CSKYMCExpr::VK_CSKY_TLSLDO;

  MCSymbol *Sym = getContext().getInlineAsmLabel(Identifier);

  if (!Sym)
    Sym = getContext().getOrCreateSymbol(Identifier);

  if (Sym->isVariable()) {
    const MCExpr *V = Sym->getVariableValue(/*SetUsed=*/false);
    if (!isa<MCSymbolRefExpr>(V)) {
      getLexer().UnLex(Tok); // Put back if it's not a bare symbol.
      return Error(getLoc(), "unknown symbol");
    }
    Res = V;
  } else
    Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());

  MCBinaryExpr::Opcode Opcode;
  switch (getLexer().getKind()) {
  default:
    if (Kind != CSKYMCExpr::VK_CSKY_None)
      Res = CSKYMCExpr::create(Res, Kind, getContext());

    Operands.push_back(CSKYOperand::createImm(Res, S, E));
    return ParseStatus::Success;
  case AsmToken::Plus:
    Opcode = MCBinaryExpr::Add;
    break;
  case AsmToken::Minus:
    Opcode = MCBinaryExpr::Sub;
    break;
  }

  getLexer().Lex(); // eat + or -

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return Error(getLoc(), "unknown expression");
  Res = MCBinaryExpr::create(Opcode, Res, Expr, getContext());
  Operands.push_back(CSKYOperand::createImm(Res, S, E));
  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parseDataSymbol(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);
  const MCExpr *Res;

  if (!parseOptionalToken(AsmToken::LBrac))
    return ParseStatus::NoMatch;
  if (getLexer().getKind() != AsmToken::Identifier) {
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return Error(getLoc(), "unknown expression");

    if (parseToken(AsmToken::RBrac, "expected ']'"))
      return ParseStatus::Failure;

    Operands.push_back(CSKYOperand::createConstpoolOp(Expr, S, E));
    return ParseStatus::Success;
  }

  AsmToken Tok = getLexer().getTok();
  StringRef Identifier;

  if (getParser().parseIdentifier(Identifier))
    return Error(getLoc(), "unknown identifier " + Identifier);

  CSKYMCExpr::VariantKind Kind = CSKYMCExpr::VK_CSKY_None;
  if (Identifier.consume_back("@GOT"))
    Kind = CSKYMCExpr::VK_CSKY_GOT_IMM18_BY4;
  else if (Identifier.consume_back("@PLT"))
    Kind = CSKYMCExpr::VK_CSKY_PLT_IMM18_BY4;

  MCSymbol *Sym = getContext().getInlineAsmLabel(Identifier);

  if (!Sym)
    Sym = getContext().getOrCreateSymbol(Identifier);

  if (Sym->isVariable()) {
    const MCExpr *V = Sym->getVariableValue(/*SetUsed=*/false);
    if (!isa<MCSymbolRefExpr>(V)) {
      getLexer().UnLex(Tok); // Put back if it's not a bare symbol.
      return Error(getLoc(), "unknown symbol");
    }
    Res = V;
  } else {
    Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());
  }

  MCBinaryExpr::Opcode Opcode;
  switch (getLexer().getKind()) {
  default:
    return Error(getLoc(), "unknown symbol");
  case AsmToken::RBrac:

    getLexer().Lex(); // Eat ']'.

    if (Kind != CSKYMCExpr::VK_CSKY_None)
      Res = CSKYMCExpr::create(Res, Kind, getContext());

    Operands.push_back(CSKYOperand::createConstpoolOp(Res, S, E));
    return ParseStatus::Success;
  case AsmToken::Plus:
    Opcode = MCBinaryExpr::Add;
    break;
  case AsmToken::Minus:
    Opcode = MCBinaryExpr::Sub;
    break;
  }

  getLexer().Lex(); // eat + or -

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return Error(getLoc(), "unknown expression");
  if (parseToken(AsmToken::RBrac, "expected ']'"))
    return ParseStatus::Failure;

  Res = MCBinaryExpr::create(Opcode, Res, Expr, getContext());
  Operands.push_back(CSKYOperand::createConstpoolOp(Res, S, E));
  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parseConstpoolSymbol(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);
  const MCExpr *Res;

  if (!parseOptionalToken(AsmToken::LBrac))
    return ParseStatus::NoMatch;

  if (getLexer().getKind() != AsmToken::Identifier) {
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return Error(getLoc(), "unknown expression");
    if (parseToken(AsmToken::RBrac))
      return ParseStatus::Failure;

    Operands.push_back(CSKYOperand::createConstpoolOp(Expr, S, E));
    return ParseStatus::Success;
  }

  AsmToken Tok = getLexer().getTok();
  StringRef Identifier;

  if (getParser().parseIdentifier(Identifier))
    return Error(getLoc(), "unknown identifier");

  MCSymbol *Sym = getContext().getInlineAsmLabel(Identifier);

  if (!Sym)
    Sym = getContext().getOrCreateSymbol(Identifier);

  if (Sym->isVariable()) {
    const MCExpr *V = Sym->getVariableValue(/*SetUsed=*/false);
    if (!isa<MCSymbolRefExpr>(V)) {
      getLexer().UnLex(Tok); // Put back if it's not a bare symbol.
      return Error(getLoc(), "unknown symbol");
    }
    Res = V;
  } else {
    Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());
  }

  MCBinaryExpr::Opcode Opcode;
  switch (getLexer().getKind()) {
  default:
    return Error(getLoc(), "unknown symbol");
  case AsmToken::RBrac:

    getLexer().Lex(); // Eat ']'.

    Operands.push_back(CSKYOperand::createConstpoolOp(Res, S, E));
    return ParseStatus::Success;
  case AsmToken::Plus:
    Opcode = MCBinaryExpr::Add;
    break;
  case AsmToken::Minus:
    Opcode = MCBinaryExpr::Sub;
    break;
  }

  getLexer().Lex(); // eat + or -

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return Error(getLoc(), "unknown expression");
  if (parseToken(AsmToken::RBrac, "expected ']'"))
    return ParseStatus::Failure;

  Res = MCBinaryExpr::create(Opcode, Res, Expr, getContext());
  Operands.push_back(CSKYOperand::createConstpoolOp(Res, S, E));
  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parsePSRFlag(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);

  unsigned Flag = 0;

  while (getLexer().isNot(AsmToken::EndOfStatement)) {
    StringRef Identifier;
    if (getParser().parseIdentifier(Identifier))
      return Error(getLoc(), "unknown identifier " + Identifier);

    if (Identifier == "sie")
      Flag = (1 << 4) | Flag;
    else if (Identifier == "ee")
      Flag = (1 << 3) | Flag;
    else if (Identifier == "ie")
      Flag = (1 << 2) | Flag;
    else if (Identifier == "fe")
      Flag = (1 << 1) | Flag;
    else if (Identifier == "af")
      Flag = (1 << 0) | Flag;
    else
      return Error(getLoc(), "expected " + Identifier);

    if (getLexer().is(AsmToken::EndOfStatement))
      break;

    if (parseToken(AsmToken::Comma, "expected ','"))
      return ParseStatus::Failure;
  }

  Operands.push_back(
      CSKYOperand::createImm(MCConstantExpr::create(Flag, getContext()), S, E));
  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parseRegSeq(OperandVector &Operands) {
  SMLoc S = getLoc();

  if (!parseRegister(Operands).isSuccess())
    return ParseStatus::NoMatch;

  auto Ry = Operands.back()->getReg();
  Operands.pop_back();

  if (parseToken(AsmToken::Minus, "expected '-'"))
    return ParseStatus::Failure;
  if (!parseRegister(Operands).isSuccess())
    return Error(getLoc(), "invalid register");

  auto Rz = Operands.back()->getReg();
  Operands.pop_back();

  Operands.push_back(CSKYOperand::createRegSeq(Ry, Rz, S));
  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parseRegList(OperandVector &Operands) {
  SMLoc S = getLoc();

  SmallVector<unsigned, 4> reglist;

  while (true) {

    if (!parseRegister(Operands).isSuccess())
      return Error(getLoc(), "invalid register");

    auto Ry = Operands.back()->getReg();
    Operands.pop_back();

    if (parseOptionalToken(AsmToken::Minus)) {
      if (!parseRegister(Operands).isSuccess())
        return Error(getLoc(), "invalid register");

      auto Rz = Operands.back()->getReg();
      Operands.pop_back();

      reglist.push_back(Ry);
      reglist.push_back(Rz);

      if (getLexer().is(AsmToken::EndOfStatement))
        break;
      (void)parseOptionalToken(AsmToken::Comma);
    } else if (parseOptionalToken(AsmToken::Comma)) {
      reglist.push_back(Ry);
      reglist.push_back(Ry);
    } else if (getLexer().is(AsmToken::EndOfStatement)) {
      reglist.push_back(Ry);
      reglist.push_back(Ry);
      break;
    } else {
      return Error(getLoc(), "invalid register list");
    }
  }

  Operands.push_back(CSKYOperand::createRegList(reglist, S));
  return ParseStatus::Success;
}

bool CSKYAsmParser::ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                     SMLoc NameLoc, OperandVector &Operands) {
  // First operand is token for instruction.
  Operands.push_back(CSKYOperand::createToken(Name, NameLoc));

  // If there are no more operands, then finish.
  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  // Parse first operand.
  if (parseOperand(Operands, Name))
    return true;

  // Parse until end of statement, consuming commas between operands.
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

ParseStatus CSKYAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                            SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();

  StringRef Name = getLexer().getTok().getIdentifier();

  if (matchRegisterNameHelper(getSTI(), Reg, Name))
    return ParseStatus::NoMatch;

  getParser().Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

ParseStatus CSKYAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getString();

  if (IDVal == ".csky_attribute")
    return parseDirectiveAttribute();

  return ParseStatus::NoMatch;
}

/// parseDirectiveAttribute
///  ::= .attribute expression ',' ( expression | "string" )
bool CSKYAsmParser::parseDirectiveAttribute() {
  MCAsmParser &Parser = getParser();
  int64_t Tag;
  SMLoc TagLoc;
  TagLoc = Parser.getTok().getLoc();
  if (Parser.getTok().is(AsmToken::Identifier)) {
    StringRef Name = Parser.getTok().getIdentifier();
    std::optional<unsigned> Ret =
        ELFAttrs::attrTypeFromString(Name, CSKYAttrs::getCSKYAttributeTags());
    if (!Ret)
      return Error(TagLoc, "attribute name not recognised: " + Name);
    Tag = *Ret;
    Parser.Lex();
  } else {
    const MCExpr *AttrExpr;

    TagLoc = Parser.getTok().getLoc();
    if (Parser.parseExpression(AttrExpr))
      return true;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(AttrExpr);
    if (!CE)
      return Error(TagLoc, "expected numeric constant");

    Tag = CE->getValue();
  }

  if (Parser.parseComma())
    return true;

  StringRef StringValue;
  int64_t IntegerValue = 0;
  bool IsIntegerValue = ((Tag != CSKYAttrs::CSKY_ARCH_NAME) &&
                         (Tag != CSKYAttrs::CSKY_CPU_NAME) &&
                         (Tag != CSKYAttrs::CSKY_FPU_NUMBER_MODULE));

  SMLoc ValueExprLoc = Parser.getTok().getLoc();
  if (IsIntegerValue) {
    const MCExpr *ValueExpr;
    if (Parser.parseExpression(ValueExpr))
      return true;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ValueExpr);
    if (!CE)
      return Error(ValueExprLoc, "expected numeric constant");
    IntegerValue = CE->getValue();
  } else {
    if (Parser.getTok().isNot(AsmToken::String))
      return Error(Parser.getTok().getLoc(), "expected string constant");

    StringValue = Parser.getTok().getStringContents();
    Parser.Lex();
  }

  if (Parser.parseEOL())
    return true;

  if (IsIntegerValue)
    getTargetStreamer().emitAttribute(Tag, IntegerValue);
  else if (Tag != CSKYAttrs::CSKY_ARCH_NAME && Tag != CSKYAttrs::CSKY_CPU_NAME)
    getTargetStreamer().emitTextAttribute(Tag, StringValue);
  else {
    CSKY::ArchKind ID = (Tag == CSKYAttrs::CSKY_ARCH_NAME)
                            ? CSKY::parseArch(StringValue)
                            : CSKY::parseCPUArch(StringValue);
    if (ID == CSKY::ArchKind::INVALID)
      return Error(ValueExprLoc, (Tag == CSKYAttrs::CSKY_ARCH_NAME)
                                     ? "unknown arch name"
                                     : "unknown cpu name");

    getTargetStreamer().emitTextAttribute(Tag, StringValue);
  }

  return false;
}

unsigned CSKYAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                   unsigned Kind) {
  CSKYOperand &Op = static_cast<CSKYOperand &>(AsmOp);

  if (!Op.isReg())
    return Match_InvalidOperand;

  MCRegister Reg = Op.getReg();

  if (CSKYMCRegisterClasses[CSKY::FPR32RegClassID].contains(Reg)) {
    // As the parser couldn't differentiate an FPR64 from an FPR32, coerce the
    // register from FPR32 to FPR64 if necessary.
    if (Kind == MCK_FPR64 || Kind == MCK_sFPR64) {
      Op.Reg.RegNum = convertFPR32ToFPR64(Reg);
      if (Kind == MCK_sFPR64 &&
          (Op.Reg.RegNum < CSKY::F0_64 || Op.Reg.RegNum > CSKY::F15_64))
        return Match_InvalidRegOutOfRange;
      if (Kind == MCK_FPR64 &&
          (Op.Reg.RegNum < CSKY::F0_64 || Op.Reg.RegNum > CSKY::F31_64))
        return Match_InvalidRegOutOfRange;
      return Match_Success;
    }
  }

  if (CSKYMCRegisterClasses[CSKY::GPRRegClassID].contains(Reg)) {
    if (Kind == MCK_GPRPair) {
      Op.Reg.RegNum = MRI->getEncodingValue(Reg) + CSKY::R0_R1;
      return Match_Success;
    }
  }

  return Match_InvalidOperand;
}

void CSKYAsmParser::emitToStreamer(MCStreamer &S, const MCInst &Inst) {
  MCInst CInst;
  bool Res = false;
  if (EnableCompressedInst)
    Res = compressInst(CInst, Inst, getSTI());
  if (Res)
    ++CSKYNumInstrsCompressed;
  S.emitInstruction((Res ? CInst : Inst), getSTI());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeCSKYAsmParser() {
  RegisterMCAsmParser<CSKYAsmParser> X(getTheCSKYTarget());
}
