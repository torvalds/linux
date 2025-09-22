//===-- SparcAsmParser.cpp - Parse Sparc assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SparcMCExpr.h"
#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "TargetInfo/SparcTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmMacro.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>

using namespace llvm;

// The generated AsmMatcher SparcGenAsmMatcher uses "Sparc" as the target
// namespace. But SPARC backend uses "SP" as its namespace.
namespace llvm {
namespace Sparc {

    using namespace SP;

} // end namespace Sparc
} // end namespace llvm

namespace {

class SparcOperand;

class SparcAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;
  const MCRegisterInfo &MRI;

  enum class TailRelocKind { Load_GOT, Add_TLS, Load_TLS, Call_TLS };

  /// @name Auto-generated Match Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "SparcGenAsmMatcher.inc"

  /// }

  // public interface of the MCTargetAsmParser.
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

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  // Custom parse functions for Sparc specific operands.
  ParseStatus parseMEMOperand(OperandVector &Operands);

  ParseStatus parseMembarTag(OperandVector &Operands);

  ParseStatus parseASITag(OperandVector &Operands);

  ParseStatus parsePrefetchTag(OperandVector &Operands);

  template <TailRelocKind Kind>
  ParseStatus parseTailRelocSym(OperandVector &Operands);

  template <unsigned N> ParseStatus parseShiftAmtImm(OperandVector &Operands);

  ParseStatus parseCallTarget(OperandVector &Operands);

  ParseStatus parseOperand(OperandVector &Operands, StringRef Name);

  ParseStatus parseSparcAsmOperand(std::unique_ptr<SparcOperand> &Operand,
                                   bool isCall = false);

  ParseStatus parseBranchModifiers(OperandVector &Operands);

  ParseStatus parseExpression(int64_t &Val);

  // Helper function for dealing with %lo / %hi in PIC mode.
  const SparcMCExpr *adjustPICRelocation(SparcMCExpr::VariantKind VK,
                                         const MCExpr *subExpr);

  // Helper function to see if current token can start an expression.
  bool isPossibleExpression(const AsmToken &Token);

  // Check if mnemonic is valid.
  MatchResultTy mnemonicIsValid(StringRef Mnemonic, unsigned VariantID);

  // returns true if Tok is matched to a register and returns register in RegNo.
  MCRegister matchRegisterName(const AsmToken &Tok, unsigned &RegKind);

  bool matchSparcAsmModifiers(const MCExpr *&EVal, SMLoc &EndLoc);

  bool is64Bit() const {
    return getSTI().getTargetTriple().getArch() == Triple::sparcv9;
  }

  bool expandSET(MCInst &Inst, SMLoc IDLoc,
                 SmallVectorImpl<MCInst> &Instructions);

  bool expandSETX(MCInst &Inst, SMLoc IDLoc,
                  SmallVectorImpl<MCInst> &Instructions);

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

public:
  SparcAsmParser(const MCSubtargetInfo &sti, MCAsmParser &parser,
                 const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, sti, MII), Parser(parser),
        MRI(*Parser.getContext().getRegisterInfo()) {
    Parser.addAliasForDirective(".half", ".2byte");
    Parser.addAliasForDirective(".uahalf", ".2byte");
    Parser.addAliasForDirective(".word", ".4byte");
    Parser.addAliasForDirective(".uaword", ".4byte");
    Parser.addAliasForDirective(".nword", is64Bit() ? ".8byte" : ".4byte");
    if (is64Bit())
      Parser.addAliasForDirective(".xword", ".8byte");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }
};

} // end anonymous namespace

  static const MCPhysReg IntRegs[32] = {
    Sparc::G0, Sparc::G1, Sparc::G2, Sparc::G3,
    Sparc::G4, Sparc::G5, Sparc::G6, Sparc::G7,
    Sparc::O0, Sparc::O1, Sparc::O2, Sparc::O3,
    Sparc::O4, Sparc::O5, Sparc::O6, Sparc::O7,
    Sparc::L0, Sparc::L1, Sparc::L2, Sparc::L3,
    Sparc::L4, Sparc::L5, Sparc::L6, Sparc::L7,
    Sparc::I0, Sparc::I1, Sparc::I2, Sparc::I3,
    Sparc::I4, Sparc::I5, Sparc::I6, Sparc::I7 };

  static const MCPhysReg DoubleRegs[32] = {
    Sparc::D0,  Sparc::D1,  Sparc::D2,  Sparc::D3,
    Sparc::D4,  Sparc::D5,  Sparc::D6,  Sparc::D7,
    Sparc::D8,  Sparc::D9,  Sparc::D10, Sparc::D11,
    Sparc::D12, Sparc::D13, Sparc::D14, Sparc::D15,
    Sparc::D16, Sparc::D17, Sparc::D18, Sparc::D19,
    Sparc::D20, Sparc::D21, Sparc::D22, Sparc::D23,
    Sparc::D24, Sparc::D25, Sparc::D26, Sparc::D27,
    Sparc::D28, Sparc::D29, Sparc::D30, Sparc::D31 };

  static const MCPhysReg QuadFPRegs[32] = {
    Sparc::Q0,  Sparc::Q1,  Sparc::Q2,  Sparc::Q3,
    Sparc::Q4,  Sparc::Q5,  Sparc::Q6,  Sparc::Q7,
    Sparc::Q8,  Sparc::Q9,  Sparc::Q10, Sparc::Q11,
    Sparc::Q12, Sparc::Q13, Sparc::Q14, Sparc::Q15 };

  static const MCPhysReg IntPairRegs[] = {
    Sparc::G0_G1, Sparc::G2_G3, Sparc::G4_G5, Sparc::G6_G7,
    Sparc::O0_O1, Sparc::O2_O3, Sparc::O4_O5, Sparc::O6_O7,
    Sparc::L0_L1, Sparc::L2_L3, Sparc::L4_L5, Sparc::L6_L7,
    Sparc::I0_I1, Sparc::I2_I3, Sparc::I4_I5, Sparc::I6_I7};

  static const MCPhysReg CoprocPairRegs[] = {
    Sparc::C0_C1,   Sparc::C2_C3,   Sparc::C4_C5,   Sparc::C6_C7,
    Sparc::C8_C9,   Sparc::C10_C11, Sparc::C12_C13, Sparc::C14_C15,
    Sparc::C16_C17, Sparc::C18_C19, Sparc::C20_C21, Sparc::C22_C23,
    Sparc::C24_C25, Sparc::C26_C27, Sparc::C28_C29, Sparc::C30_C31};

namespace {

/// SparcOperand - Instances of this class represent a parsed Sparc machine
/// instruction.
class SparcOperand : public MCParsedAsmOperand {
public:
  enum RegisterKind {
    rk_None,
    rk_IntReg,
    rk_IntPairReg,
    rk_FloatReg,
    rk_DoubleReg,
    rk_QuadReg,
    rk_CoprocReg,
    rk_CoprocPairReg,
    rk_Special,
  };

private:
  enum KindTy {
    k_Token,
    k_Register,
    k_Immediate,
    k_MemoryReg,
    k_MemoryImm,
    k_ASITag,
    k_PrefetchTag,
  } Kind;

  SMLoc StartLoc, EndLoc;

  struct Token {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    unsigned RegNum;
    RegisterKind Kind;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct MemOp {
    unsigned Base;
    unsigned OffsetReg;
    const MCExpr *Off;
  };

  union {
    struct Token Tok;
    struct RegOp Reg;
    struct ImmOp Imm;
    struct MemOp Mem;
    unsigned ASI;
    unsigned Prefetch;
  };

public:
  SparcOperand(KindTy K) : Kind(K) {}

  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return isMEMrr() || isMEMri(); }
  bool isMEMrr() const { return Kind == k_MemoryReg; }
  bool isMEMri() const { return Kind == k_MemoryImm; }
  bool isMembarTag() const { return Kind == k_Immediate; }
  bool isASITag() const { return Kind == k_ASITag; }
  bool isPrefetchTag() const { return Kind == k_PrefetchTag; }
  bool isTailRelocSym() const { return Kind == k_Immediate; }

  bool isCallTarget() const {
    if (!isImm())
      return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val))
      return CE->getValue() % 4 == 0;

    return true;
  }

  bool isShiftAmtImm5() const {
    if (!isImm())
      return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val))
      return isUInt<5>(CE->getValue());

    return false;
  }

  bool isShiftAmtImm6() const {
    if (!isImm())
      return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val))
      return isUInt<6>(CE->getValue());

    return false;
  }

  bool isIntReg() const {
    return (Kind == k_Register && Reg.Kind == rk_IntReg);
  }

  bool isFloatReg() const {
    return (Kind == k_Register && Reg.Kind == rk_FloatReg);
  }

  bool isFloatOrDoubleReg() const {
    return (Kind == k_Register && (Reg.Kind == rk_FloatReg
                                   || Reg.Kind == rk_DoubleReg));
  }

  bool isCoprocReg() const {
    return (Kind == k_Register && Reg.Kind == rk_CoprocReg);
  }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  MCRegister getReg() const override {
    assert((Kind == k_Register) && "Invalid access!");
    return Reg.RegNum;
  }

  const MCExpr *getImm() const {
    assert((Kind == k_Immediate) && "Invalid access!");
    return Imm.Val;
  }

  unsigned getMemBase() const {
    assert((Kind == k_MemoryReg || Kind == k_MemoryImm) && "Invalid access!");
    return Mem.Base;
  }

  unsigned getMemOffsetReg() const {
    assert((Kind == k_MemoryReg) && "Invalid access!");
    return Mem.OffsetReg;
  }

  const MCExpr *getMemOff() const {
    assert((Kind == k_MemoryImm) && "Invalid access!");
    return Mem.Off;
  }

  unsigned getASITag() const {
    assert((Kind == k_ASITag) && "Invalid access!");
    return ASI;
  }

  unsigned getPrefetchTag() const {
    assert((Kind == k_PrefetchTag) && "Invalid access!");
    return Prefetch;
  }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override {
    return StartLoc;
  }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override {
    return EndLoc;
  }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case k_Token:     OS << "Token: " << getToken() << "\n"; break;
    case k_Register:  OS << "Reg: #" << getReg() << "\n"; break;
    case k_Immediate: OS << "Imm: " << getImm() << "\n"; break;
    case k_MemoryReg: OS << "Mem: " << getMemBase() << "+"
                         << getMemOffsetReg() << "\n"; break;
    case k_MemoryImm: assert(getMemOff() != nullptr);
      OS << "Mem: " << getMemBase()
         << "+" << *getMemOff()
         << "\n"; break;
    case k_ASITag:
      OS << "ASI tag: " << getASITag() << "\n";
      break;
    case k_PrefetchTag:
      OS << "Prefetch tag: " << getPrefetchTag() << "\n";
      break;
    }
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  void addShiftAmtImm5Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }
  void addShiftAmtImm6Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const{
    // Add as immediate when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addMEMrrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(getMemBase()));

    assert(getMemOffsetReg() != 0 && "Invalid offset");
    Inst.addOperand(MCOperand::createReg(getMemOffsetReg()));
  }

  void addMEMriOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(getMemBase()));

    const MCExpr *Expr = getMemOff();
    addExpr(Inst, Expr);
  }

  void addASITagOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getASITag()));
  }

  void addPrefetchTagOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPrefetchTag()));
  }

  void addMembarTagOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  void addCallTargetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addTailRelocSymOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  static std::unique_ptr<SparcOperand> CreateToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<SparcOperand>(k_Token);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<SparcOperand> CreateReg(unsigned RegNum, unsigned Kind,
                                                 SMLoc S, SMLoc E) {
    auto Op = std::make_unique<SparcOperand>(k_Register);
    Op->Reg.RegNum = RegNum;
    Op->Reg.Kind   = (SparcOperand::RegisterKind)Kind;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SparcOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                                 SMLoc E) {
    auto Op = std::make_unique<SparcOperand>(k_Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SparcOperand> CreateASITag(unsigned Val, SMLoc S,
                                                    SMLoc E) {
    auto Op = std::make_unique<SparcOperand>(k_ASITag);
    Op->ASI = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SparcOperand> CreatePrefetchTag(unsigned Val, SMLoc S,
                                                         SMLoc E) {
    auto Op = std::make_unique<SparcOperand>(k_PrefetchTag);
    Op->Prefetch = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static bool MorphToIntPairReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    assert(Op.Reg.Kind == rk_IntReg);
    unsigned regIdx = 32;
    if (Reg >= Sparc::G0 && Reg <= Sparc::G7)
      regIdx = Reg - Sparc::G0;
    else if (Reg >= Sparc::O0 && Reg <= Sparc::O7)
      regIdx = Reg - Sparc::O0 + 8;
    else if (Reg >= Sparc::L0 && Reg <= Sparc::L7)
      regIdx = Reg - Sparc::L0 + 16;
    else if (Reg >= Sparc::I0 && Reg <= Sparc::I7)
      regIdx = Reg - Sparc::I0 + 24;
    if (regIdx % 2 || regIdx > 31)
      return false;
    Op.Reg.RegNum = IntPairRegs[regIdx / 2];
    Op.Reg.Kind = rk_IntPairReg;
    return true;
  }

  static bool MorphToDoubleReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    assert(Op.Reg.Kind == rk_FloatReg);
    unsigned regIdx = Reg - Sparc::F0;
    if (regIdx % 2 || regIdx > 31)
      return false;
    Op.Reg.RegNum = DoubleRegs[regIdx / 2];
    Op.Reg.Kind = rk_DoubleReg;
    return true;
  }

  static bool MorphToQuadReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    unsigned regIdx = 0;
    switch (Op.Reg.Kind) {
    default: llvm_unreachable("Unexpected register kind!");
    case rk_FloatReg:
      regIdx = Reg - Sparc::F0;
      if (regIdx % 4 || regIdx > 31)
        return false;
      Reg = QuadFPRegs[regIdx / 4];
      break;
    case rk_DoubleReg:
      regIdx =  Reg - Sparc::D0;
      if (regIdx % 2 || regIdx > 31)
        return false;
      Reg = QuadFPRegs[regIdx / 2];
      break;
    }
    Op.Reg.RegNum = Reg;
    Op.Reg.Kind = rk_QuadReg;
    return true;
  }

  static bool MorphToCoprocPairReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    assert(Op.Reg.Kind == rk_CoprocReg);
    unsigned regIdx = 32;
    if (Reg >= Sparc::C0 && Reg <= Sparc::C31)
      regIdx = Reg - Sparc::C0;
    if (regIdx % 2 || regIdx > 31)
      return false;
    Op.Reg.RegNum = CoprocPairRegs[regIdx / 2];
    Op.Reg.Kind = rk_CoprocPairReg;
    return true;
  }

  static std::unique_ptr<SparcOperand>
  MorphToMEMrr(unsigned Base, std::unique_ptr<SparcOperand> Op) {
    unsigned offsetReg = Op->getReg();
    Op->Kind = k_MemoryReg;
    Op->Mem.Base = Base;
    Op->Mem.OffsetReg = offsetReg;
    Op->Mem.Off = nullptr;
    return Op;
  }

  static std::unique_ptr<SparcOperand>
  CreateMEMr(unsigned Base, SMLoc S, SMLoc E) {
    auto Op = std::make_unique<SparcOperand>(k_MemoryReg);
    Op->Mem.Base = Base;
    Op->Mem.OffsetReg = Sparc::G0;  // always 0
    Op->Mem.Off = nullptr;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SparcOperand>
  MorphToMEMri(unsigned Base, std::unique_ptr<SparcOperand> Op) {
    const MCExpr *Imm  = Op->getImm();
    Op->Kind = k_MemoryImm;
    Op->Mem.Base = Base;
    Op->Mem.OffsetReg = 0;
    Op->Mem.Off = Imm;
    return Op;
  }
};

} // end anonymous namespace

#define GET_MATCHER_IMPLEMENTATION
#define GET_REGISTER_MATCHER
#define GET_MNEMONIC_SPELL_CHECKER
#include "SparcGenAsmMatcher.inc"

// Use a custom function instead of the one from SparcGenAsmMatcher
// so we can differentiate between unavailable and unknown instructions.
SparcAsmParser::MatchResultTy
SparcAsmParser::mnemonicIsValid(StringRef Mnemonic, unsigned VariantID) {
  // Process all MnemonicAliases to remap the mnemonic.
  applyMnemonicAliases(Mnemonic, getAvailableFeatures(), VariantID);

  // Find the appropriate table for this asm variant.
  const MatchEntry *Start, *End;
  switch (VariantID) {
  default:
    llvm_unreachable("invalid variant!");
  case 0:
    Start = std::begin(MatchTable0);
    End = std::end(MatchTable0);
    break;
  }

  // Search the table.
  auto MnemonicRange = std::equal_range(Start, End, Mnemonic, LessOpcode());

  if (MnemonicRange.first == MnemonicRange.second)
    return Match_MnemonicFail;

  for (const MatchEntry *it = MnemonicRange.first, *ie = MnemonicRange.second;
       it != ie; ++it) {
    const FeatureBitset &RequiredFeatures =
        FeatureBitsets[it->RequiredFeaturesIdx];
    if ((getAvailableFeatures() & RequiredFeatures) == RequiredFeatures)
      return Match_Success;
  }
  return Match_MissingFeature;
}

bool SparcAsmParser::expandSET(MCInst &Inst, SMLoc IDLoc,
                               SmallVectorImpl<MCInst> &Instructions) {
  MCOperand MCRegOp = Inst.getOperand(0);
  MCOperand MCValOp = Inst.getOperand(1);
  assert(MCRegOp.isReg());
  assert(MCValOp.isImm() || MCValOp.isExpr());

  // the imm operand can be either an expression or an immediate.
  bool IsImm = Inst.getOperand(1).isImm();
  int64_t RawImmValue = IsImm ? MCValOp.getImm() : 0;

  // Allow either a signed or unsigned 32-bit immediate.
  if (RawImmValue < -2147483648LL || RawImmValue > 4294967295LL) {
    return Error(IDLoc,
                 "set: argument must be between -2147483648 and 4294967295");
  }

  // If the value was expressed as a large unsigned number, that's ok.
  // We want to see if it "looks like" a small signed number.
  int32_t ImmValue = RawImmValue;
  // For 'set' you can't use 'or' with a negative operand on V9 because
  // that would splat the sign bit across the upper half of the destination
  // register, whereas 'set' is defined to zero the high 32 bits.
  bool IsEffectivelyImm13 =
      IsImm && ((is64Bit() ? 0 : -4096) <= ImmValue && ImmValue < 4096);
  const MCExpr *ValExpr;
  if (IsImm)
    ValExpr = MCConstantExpr::create(ImmValue, getContext());
  else
    ValExpr = MCValOp.getExpr();

  MCOperand PrevReg = MCOperand::createReg(Sparc::G0);

  // If not just a signed imm13 value, then either we use a 'sethi' with a
  // following 'or', or a 'sethi' by itself if there are no more 1 bits.
  // In either case, start with the 'sethi'.
  if (!IsEffectivelyImm13) {
    MCInst TmpInst;
    const MCExpr *Expr = adjustPICRelocation(SparcMCExpr::VK_Sparc_HI, ValExpr);
    TmpInst.setLoc(IDLoc);
    TmpInst.setOpcode(SP::SETHIi);
    TmpInst.addOperand(MCRegOp);
    TmpInst.addOperand(MCOperand::createExpr(Expr));
    Instructions.push_back(TmpInst);
    PrevReg = MCRegOp;
  }

  // The low bits require touching in 3 cases:
  // * A non-immediate value will always require both instructions.
  // * An effectively imm13 value needs only an 'or' instruction.
  // * Otherwise, an immediate that is not effectively imm13 requires the
  //   'or' only if bits remain after clearing the 22 bits that 'sethi' set.
  // If the low bits are known zeros, there's nothing to do.
  // In the second case, and only in that case, must we NOT clear
  // bits of the immediate value via the %lo() assembler function.
  // Note also, the 'or' instruction doesn't mind a large value in the case
  // where the operand to 'set' was 0xFFFFFzzz - it does exactly what you mean.
  if (!IsImm || IsEffectivelyImm13 || (ImmValue & 0x3ff)) {
    MCInst TmpInst;
    const MCExpr *Expr;
    if (IsEffectivelyImm13)
      Expr = ValExpr;
    else
      Expr = adjustPICRelocation(SparcMCExpr::VK_Sparc_LO, ValExpr);
    TmpInst.setLoc(IDLoc);
    TmpInst.setOpcode(SP::ORri);
    TmpInst.addOperand(MCRegOp);
    TmpInst.addOperand(PrevReg);
    TmpInst.addOperand(MCOperand::createExpr(Expr));
    Instructions.push_back(TmpInst);
  }
  return false;
}

bool SparcAsmParser::expandSETX(MCInst &Inst, SMLoc IDLoc,
                                SmallVectorImpl<MCInst> &Instructions) {
  MCOperand MCRegOp = Inst.getOperand(0);
  MCOperand MCValOp = Inst.getOperand(1);
  MCOperand MCTmpOp = Inst.getOperand(2);
  assert(MCRegOp.isReg() && MCTmpOp.isReg());
  assert(MCValOp.isImm() || MCValOp.isExpr());

  // the imm operand can be either an expression or an immediate.
  bool IsImm = MCValOp.isImm();
  int64_t ImmValue = IsImm ? MCValOp.getImm() : 0;

  const MCExpr *ValExpr = IsImm ? MCConstantExpr::create(ImmValue, getContext())
                                : MCValOp.getExpr();

  // Very small immediates can be expressed directly as a single `or`.
  if (IsImm && isInt<13>(ImmValue)) {
    // or rd, val, rd
    Instructions.push_back(MCInstBuilder(SP::ORri)
                               .addReg(MCRegOp.getReg())
                               .addReg(Sparc::G0)
                               .addExpr(ValExpr));
    return false;
  }

  // Otherwise, first we set the lower half of the register.

  // sethi %hi(val), rd
  Instructions.push_back(
      MCInstBuilder(SP::SETHIi)
          .addReg(MCRegOp.getReg())
          .addExpr(adjustPICRelocation(SparcMCExpr::VK_Sparc_HI, ValExpr)));
  // or    rd, %lo(val), rd
  Instructions.push_back(
      MCInstBuilder(SP::ORri)
          .addReg(MCRegOp.getReg())
          .addReg(MCRegOp.getReg())
          .addExpr(adjustPICRelocation(SparcMCExpr::VK_Sparc_LO, ValExpr)));

  // Small positive immediates can be expressed as a single `sethi`+`or`
  // combination, so we can just return here.
  if (IsImm && isUInt<32>(ImmValue))
    return false;

  // For bigger immediates, we need to generate the upper half, then shift and
  // merge it with the lower half that has just been generated above.

  // sethi %hh(val), tmp
  Instructions.push_back(
      MCInstBuilder(SP::SETHIi)
          .addReg(MCTmpOp.getReg())
          .addExpr(adjustPICRelocation(SparcMCExpr::VK_Sparc_HH, ValExpr)));
  // or    tmp, %hm(val), tmp
  Instructions.push_back(
      MCInstBuilder(SP::ORri)
          .addReg(MCTmpOp.getReg())
          .addReg(MCTmpOp.getReg())
          .addExpr(adjustPICRelocation(SparcMCExpr::VK_Sparc_HM, ValExpr)));
  // sllx  tmp, 32, tmp
  Instructions.push_back(MCInstBuilder(SP::SLLXri)
                             .addReg(MCTmpOp.getReg())
                             .addReg(MCTmpOp.getReg())
                             .addImm(32));
  // or    tmp, rd, rd
  Instructions.push_back(MCInstBuilder(SP::ORrr)
                             .addReg(MCRegOp.getReg())
                             .addReg(MCTmpOp.getReg())
                             .addReg(MCRegOp.getReg()));

  return false;
}

bool SparcAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  MCInst Inst;
  SmallVector<MCInst, 8> Instructions;
  unsigned MatchResult = MatchInstructionImpl(Operands, Inst, ErrorInfo,
                                              MatchingInlineAsm);
  switch (MatchResult) {
  case Match_Success: {
    switch (Inst.getOpcode()) {
    default:
      Inst.setLoc(IDLoc);
      Instructions.push_back(Inst);
      break;
    case SP::SET:
      if (expandSET(Inst, IDLoc, Instructions))
        return true;
      break;
    case SP::SETX:
      if (expandSETX(Inst, IDLoc, Instructions))
        return true;
      break;
    }

    for (const MCInst &I : Instructions) {
      Out.emitInstruction(I, getSTI());
    }
    return false;
  }

  case Match_MissingFeature:
    return Error(IDLoc,
                 "instruction requires a CPU feature not currently enabled");

  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");

      ErrorLoc = ((SparcOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }

    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");
  }
  llvm_unreachable("Implement any new match types added!");
}

bool SparcAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(StartLoc, "invalid register name");
  return false;
}

ParseStatus SparcAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                             SMLoc &EndLoc) {
  const AsmToken &Tok = Parser.getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  Reg = Sparc::NoRegister;
  if (getLexer().getKind() != AsmToken::Percent)
    return ParseStatus::NoMatch;
  Parser.Lex();
  unsigned RegKind = SparcOperand::rk_None;
  Reg = matchRegisterName(Tok, RegKind);
  if (Reg) {
    Parser.Lex();
    return ParseStatus::Success;
  }

  getLexer().UnLex(Tok);
  return ParseStatus::NoMatch;
}

bool SparcAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  // Validate and reject unavailable mnemonics early before
  // running any operand parsing.
  // This is needed because some operands (mainly memory ones)
  // differ between V8 and V9 ISA and so any operand parsing errors
  // will cause IAS to bail out before it reaches MatchAndEmitInstruction
  // (where the instruction as a whole, including the mnemonic, is validated
  // once again just before emission).
  // As a nice side effect this also allows us to reject unknown
  // instructions and suggest replacements.
  MatchResultTy MS = mnemonicIsValid(Name, 0);
  switch (MS) {
  case Match_Success:
    break;
  case Match_MissingFeature:
    return Error(NameLoc,
                 "instruction requires a CPU feature not currently enabled");
  case Match_MnemonicFail:
    return Error(NameLoc,
                 "invalid instruction mnemonic" +
                     SparcMnemonicSpellCheck(Name, getAvailableFeatures(), 0));
  default:
    llvm_unreachable("invalid return status!");
  }

  // First operand in MCInst is instruction mnemonic.
  Operands.push_back(SparcOperand::CreateToken(Name, NameLoc));

  // apply mnemonic aliases, if any, so that we can parse operands correctly.
  applyMnemonicAliases(Name, getAvailableFeatures(), 0);

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (getLexer().is(AsmToken::Comma)) {
      if (!parseBranchModifiers(Operands).isSuccess()) {
        SMLoc Loc = getLexer().getLoc();
        return Error(Loc, "unexpected token");
      }
    }
    if (!parseOperand(Operands, Name).isSuccess()) {
      SMLoc Loc = getLexer().getLoc();
      return Error(Loc, "unexpected token");
    }

    while (getLexer().is(AsmToken::Comma) || getLexer().is(AsmToken::Plus)) {
      if (getLexer().is(AsmToken::Plus)) {
      // Plus tokens are significant in software_traps (p83, sparcv8.pdf). We must capture them.
        Operands.push_back(SparcOperand::CreateToken("+", Parser.getTok().getLoc()));
      }
      Parser.Lex(); // Eat the comma or plus.
      // Parse and remember the operand.
      if (!parseOperand(Operands, Name).isSuccess()) {
        SMLoc Loc = getLexer().getLoc();
        return Error(Loc, "unexpected token");
      }
    }
  }
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    return Error(Loc, "unexpected token");
  }
  Parser.Lex(); // Consume the EndOfStatement.
  return false;
}

ParseStatus SparcAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getString();

  if (IDVal == ".register") {
    // For now, ignore .register directive.
    Parser.eatToEndOfStatement();
    return ParseStatus::Success;
  }
  if (IDVal == ".proc") {
    // For compatibility, ignore this directive.
    // (It's supposed to be an "optimization" in the Sun assembler)
    Parser.eatToEndOfStatement();
    return ParseStatus::Success;
  }

  // Let the MC layer to handle other directives.
  return ParseStatus::NoMatch;
}

ParseStatus SparcAsmParser::parseMEMOperand(OperandVector &Operands) {
  SMLoc S, E;

  std::unique_ptr<SparcOperand> LHS;
  if (!parseSparcAsmOperand(LHS).isSuccess())
    return ParseStatus::NoMatch;

  // Single immediate operand
  if (LHS->isImm()) {
    Operands.push_back(SparcOperand::MorphToMEMri(Sparc::G0, std::move(LHS)));
    return ParseStatus::Success;
  }

  if (!LHS->isIntReg())
    return Error(LHS->getStartLoc(), "invalid register kind for this operand");

  AsmToken Tok = getLexer().getTok();
  // The plus token may be followed by a register or an immediate value, the
  // minus one is always interpreted as sign for the immediate value
  if (Tok.is(AsmToken::Plus) || Tok.is(AsmToken::Minus)) {
    (void)Parser.parseOptionalToken(AsmToken::Plus);

    std::unique_ptr<SparcOperand> RHS;
    if (!parseSparcAsmOperand(RHS).isSuccess())
      return ParseStatus::NoMatch;

    if (RHS->isReg() && !RHS->isIntReg())
      return Error(RHS->getStartLoc(),
                   "invalid register kind for this operand");

    Operands.push_back(
        RHS->isImm()
            ? SparcOperand::MorphToMEMri(LHS->getReg(), std::move(RHS))
            : SparcOperand::MorphToMEMrr(LHS->getReg(), std::move(RHS)));

    return ParseStatus::Success;
  }

  Operands.push_back(SparcOperand::CreateMEMr(LHS->getReg(), S, E));
  return ParseStatus::Success;
}

template <unsigned N>
ParseStatus SparcAsmParser::parseShiftAmtImm(OperandVector &Operands) {
  SMLoc S = Parser.getTok().getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);

  // This is a register, not an immediate
  if (getLexer().getKind() == AsmToken::Percent)
    return ParseStatus::NoMatch;

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return ParseStatus::Failure;

  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr);
  if (!CE)
    return Error(S, "constant expression expected");

  if (!isUInt<N>(CE->getValue()))
    return Error(S, "immediate shift value out of range");

  Operands.push_back(SparcOperand::CreateImm(Expr, S, E));
  return ParseStatus::Success;
}

template <SparcAsmParser::TailRelocKind Kind>
ParseStatus SparcAsmParser::parseTailRelocSym(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);

  auto MatchesKind = [](SparcMCExpr::VariantKind VK) -> bool {
    switch (Kind) {
    case TailRelocKind::Load_GOT:
      // Non-TLS relocations on ld (or ldx).
      // ld [%rr + %rr], %rr, %rel(sym)
      return VK == SparcMCExpr::VK_Sparc_GOTDATA_OP;
    case TailRelocKind::Add_TLS:
      // TLS relocations on add.
      // add %rr, %rr, %rr, %rel(sym)
      switch (VK) {
      case SparcMCExpr::VK_Sparc_TLS_GD_ADD:
      case SparcMCExpr::VK_Sparc_TLS_IE_ADD:
      case SparcMCExpr::VK_Sparc_TLS_LDM_ADD:
      case SparcMCExpr::VK_Sparc_TLS_LDO_ADD:
        return true;
      default:
        return false;
      }
    case TailRelocKind::Load_TLS:
      // TLS relocations on ld (or ldx).
      // ld[x] %addr, %rr, %rel(sym)
      switch (VK) {
      case SparcMCExpr::VK_Sparc_TLS_IE_LD:
      case SparcMCExpr::VK_Sparc_TLS_IE_LDX:
        return true;
      default:
        return false;
      }
    case TailRelocKind::Call_TLS:
      // TLS relocations on call.
      // call sym, %rel(sym)
      switch (VK) {
      case SparcMCExpr::VK_Sparc_TLS_GD_CALL:
      case SparcMCExpr::VK_Sparc_TLS_LDM_CALL:
        return true;
      default:
        return false;
      }
    }
    llvm_unreachable("Unhandled SparcAsmParser::TailRelocKind enum");
  };

  if (getLexer().getKind() != AsmToken::Percent)
    return Error(getLoc(), "expected '%' for operand modifier");

  const AsmToken Tok = Parser.getTok();
  getParser().Lex(); // Eat '%'

  if (getLexer().getKind() != AsmToken::Identifier)
    return Error(getLoc(), "expected valid identifier for operand modifier");

  StringRef Name = getParser().getTok().getIdentifier();
  SparcMCExpr::VariantKind VK = SparcMCExpr::parseVariantKind(Name);
  if (VK == SparcMCExpr::VK_Sparc_None)
    return Error(getLoc(), "invalid operand modifier");

  if (!MatchesKind(VK)) {
    // Did not match the specified set of relocation types, put '%' back.
    getLexer().UnLex(Tok);
    return ParseStatus::NoMatch;
  }

  Parser.Lex(); // Eat the identifier.
  if (getLexer().getKind() != AsmToken::LParen)
    return Error(getLoc(), "expected '('");

  getParser().Lex(); // Eat '('
  const MCExpr *SubExpr;
  if (getParser().parseParenExpression(SubExpr, E))
    return ParseStatus::Failure;

  const MCExpr *Val = adjustPICRelocation(VK, SubExpr);
  Operands.push_back(SparcOperand::CreateImm(Val, S, E));
  return ParseStatus::Success;
}

ParseStatus SparcAsmParser::parseMembarTag(OperandVector &Operands) {
  SMLoc S = Parser.getTok().getLoc();
  const MCExpr *EVal;
  int64_t ImmVal = 0;

  std::unique_ptr<SparcOperand> Mask;
  if (parseSparcAsmOperand(Mask).isSuccess()) {
    if (!Mask->isImm() || !Mask->getImm()->evaluateAsAbsolute(ImmVal) ||
        ImmVal < 0 || ImmVal > 127)
      return Error(S, "invalid membar mask number");
  }

  while (getLexer().getKind() == AsmToken::Hash) {
    SMLoc TagStart = getLexer().getLoc();
    Parser.Lex(); // Eat the '#'.
    unsigned MaskVal = StringSwitch<unsigned>(Parser.getTok().getString())
      .Case("LoadLoad", 0x1)
      .Case("StoreLoad", 0x2)
      .Case("LoadStore", 0x4)
      .Case("StoreStore", 0x8)
      .Case("Lookaside", 0x10)
      .Case("MemIssue", 0x20)
      .Case("Sync", 0x40)
      .Default(0);

    Parser.Lex(); // Eat the identifier token.

    if (!MaskVal)
      return Error(TagStart, "unknown membar tag");

    ImmVal |= MaskVal;

    if (getLexer().getKind() == AsmToken::Pipe)
      Parser.Lex(); // Eat the '|'.
  }

  EVal = MCConstantExpr::create(ImmVal, getContext());
  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  Operands.push_back(SparcOperand::CreateImm(EVal, S, E));
  return ParseStatus::Success;
}

ParseStatus SparcAsmParser::parseASITag(OperandVector &Operands) {
  SMLoc S = Parser.getTok().getLoc();
  SMLoc E = Parser.getTok().getEndLoc();
  int64_t ASIVal = 0;

  if (getLexer().getKind() != AsmToken::Hash) {
    // If the ASI tag provided is not a named tag, then it
    // must be a constant expression.
    ParseStatus ParseExprStatus = parseExpression(ASIVal);
    if (!ParseExprStatus.isSuccess())
      return ParseExprStatus;

    if (!isUInt<8>(ASIVal))
      return Error(S, "invalid ASI number, must be between 0 and 255");

    Operands.push_back(SparcOperand::CreateASITag(ASIVal, S, E));
    return ParseStatus::Success;
  }

  // For now we only support named tags for 64-bit/V9 systems.
  // TODO: add support for 32-bit/V8 systems.
  SMLoc TagStart = getLexer().peekTok(false).getLoc();
  Parser.Lex(); // Eat the '#'.
  const StringRef ASIName = Parser.getTok().getString();
  const SparcASITag::ASITag *ASITag = SparcASITag::lookupASITagByName(ASIName);
  if (!ASITag)
    ASITag = SparcASITag::lookupASITagByAltName(ASIName);
  Parser.Lex(); // Eat the identifier token.

  if (!ASITag)
    return Error(TagStart, "unknown ASI tag");

  ASIVal = ASITag->Encoding;

  Operands.push_back(SparcOperand::CreateASITag(ASIVal, S, E));
  return ParseStatus::Success;
}

ParseStatus SparcAsmParser::parsePrefetchTag(OperandVector &Operands) {
  SMLoc S = Parser.getTok().getLoc();
  SMLoc E = Parser.getTok().getEndLoc();
  int64_t PrefetchVal = 0;

  if (getLexer().getKind() != AsmToken::Hash) {
    // If the prefetch tag provided is not a named tag, then it
    // must be a constant expression.
    ParseStatus ParseExprStatus = parseExpression(PrefetchVal);
    if (!ParseExprStatus.isSuccess())
      return ParseExprStatus;

    if (!isUInt<8>(PrefetchVal))
      return Error(S, "invalid prefetch number, must be between 0 and 31");

    Operands.push_back(SparcOperand::CreatePrefetchTag(PrefetchVal, S, E));
    return ParseStatus::Success;
  }

  SMLoc TagStart = getLexer().peekTok(false).getLoc();
  Parser.Lex(); // Eat the '#'.
  const StringRef PrefetchName = Parser.getTok().getString();
  const SparcPrefetchTag::PrefetchTag *PrefetchTag =
      SparcPrefetchTag::lookupPrefetchTagByName(PrefetchName);
  Parser.Lex(); // Eat the identifier token.

  if (!PrefetchTag)
    return Error(TagStart, "unknown prefetch tag");

  PrefetchVal = PrefetchTag->Encoding;

  Operands.push_back(SparcOperand::CreatePrefetchTag(PrefetchVal, S, E));
  return ParseStatus::Success;
}

ParseStatus SparcAsmParser::parseCallTarget(OperandVector &Operands) {
  SMLoc S = Parser.getTok().getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() - 1);

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Integer:
  case AsmToken::Identifier:
  case AsmToken::Dot:
    break;
  }

  const MCExpr *DestValue;
  if (getParser().parseExpression(DestValue))
    return ParseStatus::NoMatch;

  bool IsPic = getContext().getObjectFileInfo()->isPositionIndependent();
  SparcMCExpr::VariantKind Kind =
      IsPic ? SparcMCExpr::VK_Sparc_WPLT30 : SparcMCExpr::VK_Sparc_WDISP30;

  const MCExpr *DestExpr = SparcMCExpr::create(Kind, DestValue, getContext());
  Operands.push_back(SparcOperand::CreateImm(DestExpr, S, E));
  return ParseStatus::Success;
}

ParseStatus SparcAsmParser::parseOperand(OperandVector &Operands,
                                         StringRef Mnemonic) {

  ParseStatus Res = MatchOperandParserImpl(Operands, Mnemonic);

  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (Res.isSuccess() || Res.isFailure())
    return Res;

  if (getLexer().is(AsmToken::LBrac)) {
    // Memory operand
    Operands.push_back(SparcOperand::CreateToken("[",
                                                 Parser.getTok().getLoc()));
    Parser.Lex(); // Eat the [

    if (Mnemonic == "cas" || Mnemonic == "casl" || Mnemonic == "casa" ||
        Mnemonic == "casx" || Mnemonic == "casxl" || Mnemonic == "casxa") {
      SMLoc S = Parser.getTok().getLoc();
      if (getLexer().getKind() != AsmToken::Percent)
        return ParseStatus::NoMatch;
      Parser.Lex(); // eat %

      unsigned RegKind;
      MCRegister Reg = matchRegisterName(Parser.getTok(), RegKind);
      if (!Reg)
        return ParseStatus::NoMatch;

      Parser.Lex(); // Eat the identifier token.
      SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer()-1);
      Operands.push_back(SparcOperand::CreateReg(Reg, RegKind, S, E));
      Res = ParseStatus::Success;
    } else {
      Res = parseMEMOperand(Operands);
    }

    if (!Res.isSuccess())
      return Res;

    if (!getLexer().is(AsmToken::RBrac))
      return ParseStatus::Failure;

    Operands.push_back(SparcOperand::CreateToken("]",
                                                 Parser.getTok().getLoc()));
    Parser.Lex(); // Eat the ]

    // Parse an optional address-space identifier after the address.
    // This will be either an immediate constant expression, or, on 64-bit
    // processors, the %asi register.
    if (getLexer().is(AsmToken::Percent)) {
      SMLoc S = Parser.getTok().getLoc();
      if (!is64Bit())
        return Error(
            S, "malformed ASI tag, must be a constant integer expression");

      Parser.Lex(); // Eat the %.
      const AsmToken Tok = Parser.getTok();
      if (Tok.is(AsmToken::Identifier) && Tok.getString() == "asi") {
        // Here we patch the MEM operand from [base + %g0] into [base + 0]
        // as memory operations with ASI tag stored in %asi register needs
        // to use immediate offset. We need to do this because Reg addressing
        // will be parsed as Reg+G0 initially.
        // This allows forms such as `ldxa [%o0] %asi, %o0` to parse correctly.
        SparcOperand &OldMemOp = (SparcOperand &)*Operands[Operands.size() - 2];
        if (OldMemOp.isMEMrr()) {
          if (OldMemOp.getMemOffsetReg() != Sparc::G0) {
            return Error(S, "invalid operand for instruction");
          }
          Operands[Operands.size() - 2] = SparcOperand::MorphToMEMri(
              OldMemOp.getMemBase(),
              SparcOperand::CreateImm(MCConstantExpr::create(0, getContext()),
                                      OldMemOp.getStartLoc(),
                                      OldMemOp.getEndLoc()));
        }
        Parser.Lex(); // Eat the identifier.
        // In this context, we convert the register operand into
        // a plain "%asi" token since the register access is already
        // implicit in the instruction definition and encoding.
        // See LoadASI/StoreASI in SparcInstrInfo.td.
        Operands.push_back(SparcOperand::CreateToken("%asi", S));
        return ParseStatus::Success;
      }

      return Error(S, "malformed ASI tag, must be %asi, a constant integer "
                      "expression, or a named tag");
    }

    // If we're not at the end of statement and the next token is not a comma,
    // then it is an immediate ASI value.
    if (getLexer().isNot(AsmToken::EndOfStatement) &&
        getLexer().isNot(AsmToken::Comma))
      return parseASITag(Operands);
    return ParseStatus::Success;
  }

  std::unique_ptr<SparcOperand> Op;

  Res = parseSparcAsmOperand(Op, (Mnemonic == "call"));
  if (!Res.isSuccess() || !Op)
    return ParseStatus::Failure;

  // Push the parsed operand into the list of operands
  Operands.push_back(std::move(Op));

  return ParseStatus::Success;
}

ParseStatus
SparcAsmParser::parseSparcAsmOperand(std::unique_ptr<SparcOperand> &Op,
                                     bool isCall) {
  SMLoc S = Parser.getTok().getLoc();
  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  const MCExpr *EVal;

  Op = nullptr;
  switch (getLexer().getKind()) {
  default:  break;

  case AsmToken::Percent: {
    Parser.Lex(); // Eat the '%'.
    unsigned RegKind;
    if (MCRegister Reg = matchRegisterName(Parser.getTok(), RegKind)) {
      StringRef Name = Parser.getTok().getString();
      Parser.Lex(); // Eat the identifier token.
      E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
      if (Reg == Sparc::ICC && Name == "xcc")
        Op = SparcOperand::CreateToken("%xcc", S);
      else
        Op = SparcOperand::CreateReg(Reg, RegKind, S, E);
      break;
    }
    if (matchSparcAsmModifiers(EVal, E)) {
      E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
      Op = SparcOperand::CreateImm(EVal, S, E);
    }
    break;
  }

  case AsmToken::Plus:
  case AsmToken::Minus:
  case AsmToken::Integer:
  case AsmToken::LParen:
  case AsmToken::Dot:
  case AsmToken::Identifier:
    if (getParser().parseExpression(EVal, E))
      break;

    int64_t Res;
    if (!EVal->evaluateAsAbsolute(Res)) {
      SparcMCExpr::VariantKind Kind = SparcMCExpr::VK_Sparc_13;

      if (getContext().getObjectFileInfo()->isPositionIndependent()) {
        if (isCall)
          Kind = SparcMCExpr::VK_Sparc_WPLT30;
        else
          Kind = SparcMCExpr::VK_Sparc_GOT13;
      }
      EVal = SparcMCExpr::create(Kind, EVal, getContext());
    }
    Op = SparcOperand::CreateImm(EVal, S, E);
    break;
  }
  return Op ? ParseStatus::Success : ParseStatus::Failure;
}

ParseStatus SparcAsmParser::parseBranchModifiers(OperandVector &Operands) {
  // parse (,a|,pn|,pt)+

  while (getLexer().is(AsmToken::Comma)) {
    Parser.Lex(); // Eat the comma

    if (!getLexer().is(AsmToken::Identifier))
      return ParseStatus::Failure;
    StringRef modName = Parser.getTok().getString();
    if (modName == "a" || modName == "pn" || modName == "pt") {
      Operands.push_back(SparcOperand::CreateToken(modName,
                                                   Parser.getTok().getLoc()));
      Parser.Lex(); // eat the identifier.
    }
  }
  return ParseStatus::Success;
}

ParseStatus SparcAsmParser::parseExpression(int64_t &Val) {
  AsmToken Tok = getLexer().getTok();

  if (!isPossibleExpression(Tok))
    return ParseStatus::NoMatch;

  return getParser().parseAbsoluteExpression(Val);
}

MCRegister SparcAsmParser::matchRegisterName(const AsmToken &Tok,
                                             unsigned &RegKind) {
  RegKind = SparcOperand::rk_None;
  if (!Tok.is(AsmToken::Identifier))
    return SP::NoRegister;

  StringRef Name = Tok.getString();
  MCRegister Reg = MatchRegisterName(Name.lower());
  if (!Reg)
    Reg = MatchRegisterAltName(Name.lower());

  if (Reg) {
    // Some registers have identical spellings. The generated matcher might
    // have chosen one or another spelling, e.g. "%fp" or "%i6" might have been
    // matched to either SP::I6 or SP::I6_I7. Other parts of SparcAsmParser
    // are not prepared for this, so we do some canonicalization.

    // See the note in SparcRegisterInfo.td near ASRRegs register class.
    if (Reg == SP::ASR4 && Name == "tick") {
      RegKind = SparcOperand::rk_Special;
      return SP::TICK;
    }

    if (MRI.getRegClass(SP::IntRegsRegClassID).contains(Reg)) {
      RegKind = SparcOperand::rk_IntReg;
      return Reg;
    }
    if (MRI.getRegClass(SP::FPRegsRegClassID).contains(Reg)) {
      RegKind = SparcOperand::rk_FloatReg;
      return Reg;
    }
    if (MRI.getRegClass(SP::CoprocRegsRegClassID).contains(Reg)) {
      RegKind = SparcOperand::rk_CoprocReg;
      return Reg;
    }

    // Canonicalize G0_G1 ... G30_G31 etc. to G0 ... G30.
    if (MRI.getRegClass(SP::IntPairRegClassID).contains(Reg)) {
      RegKind = SparcOperand::rk_IntReg;
      return MRI.getSubReg(Reg, SP::sub_even);
    }

    // Canonicalize D0 ... D15 to F0 ... F30.
    if (MRI.getRegClass(SP::DFPRegsRegClassID).contains(Reg)) {
      // D16 ... D31 do not have sub-registers.
      if (MCRegister SubReg = MRI.getSubReg(Reg, SP::sub_even)) {
        RegKind = SparcOperand::rk_FloatReg;
        return SubReg;
      }
      RegKind = SparcOperand::rk_DoubleReg;
      return Reg;
    }

    // The generated matcher does not currently return QFP registers.
    // If it changes, we will need to handle them in a similar way.
    assert(!MRI.getRegClass(SP::QFPRegsRegClassID).contains(Reg));

    // Canonicalize C0_C1 ... C30_C31 to C0 ... C30.
    if (MRI.getRegClass(SP::CoprocPairRegClassID).contains(Reg)) {
      RegKind = SparcOperand::rk_CoprocReg;
      return MRI.getSubReg(Reg, SP::sub_even);
    }

    // Other registers do not need special handling.
    RegKind = SparcOperand::rk_Special;
    return Reg;
  }

  // If we still have no match, try custom parsing.
  // Not all registers and their spellings are modeled in td files.

  // %r0 - %r31
  int64_t RegNo = 0;
  if (Name.starts_with_insensitive("r") &&
      !Name.substr(1, 2).getAsInteger(10, RegNo) && RegNo < 31) {
    RegKind = SparcOperand::rk_IntReg;
    return IntRegs[RegNo];
  }

  if (Name == "xcc") {
    // FIXME:: check 64bit.
    RegKind = SparcOperand::rk_Special;
    return SP::ICC;
  }

  // JPS1 extension - aliases for ASRs
  // Section 5.2.11 - Ancillary State Registers (ASRs)
  if (Name == "pcr") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR16;
  }
  if (Name == "pic") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR17;
  }
  if (Name == "dcr") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR18;
  }
  if (Name == "gsr") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR19;
  }
  if (Name == "set_softint") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR20;
  }
  if (Name == "clear_softint") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR21;
  }
  if (Name == "softint") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR22;
  }
  if (Name == "tick_cmpr") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR23;
  }
  if (Name == "stick" || Name == "sys_tick") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR24;
  }
  if (Name == "stick_cmpr" || Name == "sys_tick_cmpr") {
    RegKind = SparcOperand::rk_Special;
    return SP::ASR25;
  }

  return SP::NoRegister;
}

// Determine if an expression contains a reference to the symbol
// "_GLOBAL_OFFSET_TABLE_".
static bool hasGOTReference(const MCExpr *Expr) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    if (const SparcMCExpr *SE = dyn_cast<SparcMCExpr>(Expr))
      return hasGOTReference(SE->getSubExpr());
    break;

  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    return hasGOTReference(BE->getLHS()) || hasGOTReference(BE->getRHS());
  }

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &SymRef = *cast<MCSymbolRefExpr>(Expr);
    return (SymRef.getSymbol().getName() == "_GLOBAL_OFFSET_TABLE_");
  }

  case MCExpr::Unary:
    return hasGOTReference(cast<MCUnaryExpr>(Expr)->getSubExpr());
  }
  return false;
}

const SparcMCExpr *
SparcAsmParser::adjustPICRelocation(SparcMCExpr::VariantKind VK,
                                    const MCExpr *subExpr) {
  // When in PIC mode, "%lo(...)" and "%hi(...)" behave differently.
  // If the expression refers contains _GLOBAL_OFFSET_TABLE, it is
  // actually a %pc10 or %pc22 relocation. Otherwise, they are interpreted
  // as %got10 or %got22 relocation.

  if (getContext().getObjectFileInfo()->isPositionIndependent()) {
    switch(VK) {
    default: break;
    case SparcMCExpr::VK_Sparc_LO:
      VK = (hasGOTReference(subExpr) ? SparcMCExpr::VK_Sparc_PC10
                                     : SparcMCExpr::VK_Sparc_GOT10);
      break;
    case SparcMCExpr::VK_Sparc_HI:
      VK = (hasGOTReference(subExpr) ? SparcMCExpr::VK_Sparc_PC22
                                     : SparcMCExpr::VK_Sparc_GOT22);
      break;
    }
  }

  return SparcMCExpr::create(VK, subExpr, getContext());
}

bool SparcAsmParser::matchSparcAsmModifiers(const MCExpr *&EVal,
                                            SMLoc &EndLoc) {
  AsmToken Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return false;

  StringRef name = Tok.getString();

  SparcMCExpr::VariantKind VK = SparcMCExpr::parseVariantKind(name);
  switch (VK) {
  case SparcMCExpr::VK_Sparc_None:
    Error(getLoc(), "invalid operand modifier");
    return false;

  case SparcMCExpr::VK_Sparc_GOTDATA_OP:
  case SparcMCExpr::VK_Sparc_TLS_GD_ADD:
  case SparcMCExpr::VK_Sparc_TLS_GD_CALL:
  case SparcMCExpr::VK_Sparc_TLS_IE_ADD:
  case SparcMCExpr::VK_Sparc_TLS_IE_LD:
  case SparcMCExpr::VK_Sparc_TLS_IE_LDX:
  case SparcMCExpr::VK_Sparc_TLS_LDM_ADD:
  case SparcMCExpr::VK_Sparc_TLS_LDM_CALL:
  case SparcMCExpr::VK_Sparc_TLS_LDO_ADD:
    // These are special-cased at tablegen level.
    return false;

  default:
    break;
  }

  Parser.Lex(); // Eat the identifier.
  if (Parser.getTok().getKind() != AsmToken::LParen)
    return false;

  Parser.Lex(); // Eat the LParen token.
  const MCExpr *subExpr;
  if (Parser.parseParenExpression(subExpr, EndLoc))
    return false;

  EVal = adjustPICRelocation(VK, subExpr);
  return true;
}

bool SparcAsmParser::isPossibleExpression(const AsmToken &Token) {
  switch (Token.getKind()) {
  case AsmToken::LParen:
  case AsmToken::Integer:
  case AsmToken::Identifier:
  case AsmToken::Plus:
  case AsmToken::Minus:
  case AsmToken::Tilde:
    return true;
  default:
    return false;
  }
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSparcAsmParser() {
  RegisterMCAsmParser<SparcAsmParser> A(getTheSparcTarget());
  RegisterMCAsmParser<SparcAsmParser> B(getTheSparcV9Target());
  RegisterMCAsmParser<SparcAsmParser> C(getTheSparcelTarget());
}

unsigned SparcAsmParser::validateTargetOperandClass(MCParsedAsmOperand &GOp,
                                                    unsigned Kind) {
  SparcOperand &Op = (SparcOperand &)GOp;
  if (Op.isFloatOrDoubleReg()) {
    switch (Kind) {
    default: break;
    case MCK_DFPRegs:
      if (!Op.isFloatReg() || SparcOperand::MorphToDoubleReg(Op))
        return MCTargetAsmParser::Match_Success;
      break;
    case MCK_QFPRegs:
      if (SparcOperand::MorphToQuadReg(Op))
        return MCTargetAsmParser::Match_Success;
      break;
    }
  }
  if (Op.isIntReg() && Kind == MCK_IntPair) {
    if (SparcOperand::MorphToIntPairReg(Op))
      return MCTargetAsmParser::Match_Success;
  }
  if (Op.isCoprocReg() && Kind == MCK_CoprocPair) {
     if (SparcOperand::MorphToCoprocPairReg(Op))
       return MCTargetAsmParser::Match_Success;
   }
  return Match_InvalidOperand;
}
