//===-- RISCVAsmParser.cpp - Parse RISC-V assembly to MCInst instructions -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVAsmBackend.h"
#include "MCTargetDesc/RISCVBaseInfo.h"
#include "MCTargetDesc/RISCVInstPrinter.h"
#include "MCTargetDesc/RISCVMCExpr.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "MCTargetDesc/RISCVMatInt.h"
#include "MCTargetDesc/RISCVTargetStreamer.h"
#include "TargetInfo/RISCVTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/RISCVAttributes.h"
#include "llvm/TargetParser/RISCVISAInfo.h"

#include <limits>

using namespace llvm;

#define DEBUG_TYPE "riscv-asm-parser"

STATISTIC(RISCVNumInstrsCompressed,
          "Number of RISC-V Compressed instructions emitted");

static cl::opt<bool> AddBuildAttributes("riscv-add-build-attributes",
                                        cl::init(false));

namespace llvm {
extern const SubtargetFeatureKV RISCVFeatureKV[RISCV::NumSubtargetFeatures];
} // namespace llvm

namespace {
struct RISCVOperand;

struct ParserOptionsSet {
  bool IsPicEnabled;
};

class RISCVAsmParser : public MCTargetAsmParser {
  // This tracks the parsing of the 4 operands that make up the vtype portion
  // of vset(i)vli instructions which are separated by commas. The state names
  // represent the next expected operand with Done meaning no other operands are
  // expected.
  enum VTypeState {
    VTypeState_SEW,
    VTypeState_LMUL,
    VTypeState_TailPolicy,
    VTypeState_MaskPolicy,
    VTypeState_Done,
  };

  SmallVector<FeatureBitset, 4> FeatureBitStack;

  SmallVector<ParserOptionsSet, 4> ParserOptionsStack;
  ParserOptionsSet ParserOptions;

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }
  bool isRV64() const { return getSTI().hasFeature(RISCV::Feature64Bit); }
  bool isRVE() const { return getSTI().hasFeature(RISCV::FeatureStdExtE); }
  bool enableExperimentalExtension() const {
    return getSTI().hasFeature(RISCV::Experimental);
  }

  RISCVTargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<RISCVTargetStreamer &>(TS);
  }

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;
  unsigned checkTargetMatchPredicate(MCInst &Inst) override;

  bool generateImmOutOfRangeError(OperandVector &Operands, uint64_t ErrorInfo,
                                  int64_t Lower, int64_t Upper,
                                  const Twine &Msg);
  bool generateImmOutOfRangeError(SMLoc ErrorLoc, int64_t Lower, int64_t Upper,
                                  const Twine &Msg);

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  MCRegister matchRegisterNameHelper(StringRef Name) const;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;

  bool parseVTypeToken(const AsmToken &Tok, VTypeState &State, unsigned &Sew,
                       unsigned &Lmul, bool &Fractional, bool &TailAgnostic,
                       bool &MaskAgnostic);
  bool generateVTypeError(SMLoc ErrorLoc);

  // Helper to actually emit an instruction to the MCStreamer. Also, when
  // possible, compression of the instruction is performed.
  void emitToStreamer(MCStreamer &S, const MCInst &Inst);

  // Helper to emit a combination of LUI, ADDI(W), and SLLI instructions that
  // synthesize the desired immedate value into the destination register.
  void emitLoadImm(MCRegister DestReg, int64_t Value, MCStreamer &Out);

  // Helper to emit a combination of AUIPC and SecondOpcode. Used to implement
  // helpers such as emitLoadLocalAddress and emitLoadAddress.
  void emitAuipcInstPair(MCOperand DestReg, MCOperand TmpReg,
                         const MCExpr *Symbol, RISCVMCExpr::VariantKind VKHi,
                         unsigned SecondOpcode, SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo instruction "lla" used in PC-rel addressing.
  void emitLoadLocalAddress(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo instruction "lga" used in GOT-rel addressing.
  void emitLoadGlobalAddress(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo instruction "la" used in GOT/PC-rel addressing.
  void emitLoadAddress(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo instruction "la.tls.ie" used in initial-exec TLS
  // addressing.
  void emitLoadTLSIEAddress(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo instruction "la.tls.gd" used in global-dynamic TLS
  // addressing.
  void emitLoadTLSGDAddress(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo load/store instruction with a symbol.
  void emitLoadStoreSymbol(MCInst &Inst, unsigned Opcode, SMLoc IDLoc,
                           MCStreamer &Out, bool HasTmpReg);

  // Helper to emit pseudo sign/zero extend instruction.
  void emitPseudoExtend(MCInst &Inst, bool SignExtend, int64_t Width,
                        SMLoc IDLoc, MCStreamer &Out);

  // Helper to emit pseudo vmsge{u}.vx instruction.
  void emitVMSGE(MCInst &Inst, unsigned Opcode, SMLoc IDLoc, MCStreamer &Out);

  // Checks that a PseudoAddTPRel is using x4/tp in its second input operand.
  // Enforcing this using a restricted register class for the second input
  // operand of PseudoAddTPRel results in a poor diagnostic due to the fact
  // 'add' is an overloaded mnemonic.
  bool checkPseudoAddTPRel(MCInst &Inst, OperandVector &Operands);

  // Checks that a PseudoTLSDESCCall is using x5/t0 in its output operand.
  // Enforcing this using a restricted register class for the output
  // operand of PseudoTLSDESCCall results in a poor diagnostic due to the fact
  // 'jalr' is an overloaded mnemonic.
  bool checkPseudoTLSDESCCall(MCInst &Inst, OperandVector &Operands);

  // Check instruction constraints.
  bool validateInstruction(MCInst &Inst, OperandVector &Operands);

  /// Helper for processing MC instructions that have been successfully matched
  /// by MatchAndEmitInstruction. Modifications to the emitted instructions,
  /// like the expansion of pseudo instructions (e.g., "li"), can be performed
  /// in this method.
  bool processInstruction(MCInst &Inst, SMLoc IDLoc, OperandVector &Operands,
                          MCStreamer &Out);

// Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "RISCVGenAsmMatcher.inc"

  ParseStatus parseCSRSystemRegister(OperandVector &Operands);
  ParseStatus parseFPImm(OperandVector &Operands);
  ParseStatus parseImmediate(OperandVector &Operands);
  ParseStatus parseRegister(OperandVector &Operands, bool AllowParens = false);
  ParseStatus parseMemOpBaseReg(OperandVector &Operands);
  ParseStatus parseZeroOffsetMemOp(OperandVector &Operands);
  ParseStatus parseOperandWithModifier(OperandVector &Operands);
  ParseStatus parseBareSymbol(OperandVector &Operands);
  ParseStatus parseCallSymbol(OperandVector &Operands);
  ParseStatus parsePseudoJumpSymbol(OperandVector &Operands);
  ParseStatus parseJALOffset(OperandVector &Operands);
  ParseStatus parseVTypeI(OperandVector &Operands);
  ParseStatus parseMaskReg(OperandVector &Operands);
  ParseStatus parseInsnDirectiveOpcode(OperandVector &Operands);
  ParseStatus parseInsnCDirectiveOpcode(OperandVector &Operands);
  ParseStatus parseGPRAsFPR(OperandVector &Operands);
  template <bool IsRV64Inst> ParseStatus parseGPRPair(OperandVector &Operands);
  ParseStatus parseGPRPair(OperandVector &Operands, bool IsRV64Inst);
  ParseStatus parseFRMArg(OperandVector &Operands);
  ParseStatus parseFenceArg(OperandVector &Operands);
  ParseStatus parseReglist(OperandVector &Operands);
  ParseStatus parseRegReg(OperandVector &Operands);
  ParseStatus parseRetval(OperandVector &Operands);
  ParseStatus parseZcmpStackAdj(OperandVector &Operands,
                                bool ExpectNegative = false);
  ParseStatus parseZcmpNegStackAdj(OperandVector &Operands) {
    return parseZcmpStackAdj(Operands, /*ExpectNegative*/ true);
  }

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);

  bool parseDirectiveOption();
  bool parseDirectiveAttribute();
  bool parseDirectiveInsn(SMLoc L);
  bool parseDirectiveVariantCC();

  /// Helper to reset target features for a new arch string. It
  /// also records the new arch string that is expanded by RISCVISAInfo
  /// and reports error for invalid arch string.
  bool resetToArch(StringRef Arch, SMLoc Loc, std::string &Result,
                   bool FromOptionDirective);

  void setFeatureBits(uint64_t Feature, StringRef FeatureString) {
    if (!(getSTI().hasFeature(Feature))) {
      MCSubtargetInfo &STI = copySTI();
      setAvailableFeatures(
          ComputeAvailableFeatures(STI.ToggleFeature(FeatureString)));
    }
  }

  void clearFeatureBits(uint64_t Feature, StringRef FeatureString) {
    if (getSTI().hasFeature(Feature)) {
      MCSubtargetInfo &STI = copySTI();
      setAvailableFeatures(
          ComputeAvailableFeatures(STI.ToggleFeature(FeatureString)));
    }
  }

  void pushFeatureBits() {
    assert(FeatureBitStack.size() == ParserOptionsStack.size() &&
           "These two stacks must be kept synchronized");
    FeatureBitStack.push_back(getSTI().getFeatureBits());
    ParserOptionsStack.push_back(ParserOptions);
  }

  bool popFeatureBits() {
    assert(FeatureBitStack.size() == ParserOptionsStack.size() &&
           "These two stacks must be kept synchronized");
    if (FeatureBitStack.empty())
      return true;

    FeatureBitset FeatureBits = FeatureBitStack.pop_back_val();
    copySTI().setFeatureBits(FeatureBits);
    setAvailableFeatures(ComputeAvailableFeatures(FeatureBits));

    ParserOptions = ParserOptionsStack.pop_back_val();

    return false;
  }

  std::unique_ptr<RISCVOperand> defaultMaskRegOp() const;
  std::unique_ptr<RISCVOperand> defaultFRMArgOp() const;
  std::unique_ptr<RISCVOperand> defaultFRMArgLegacyOp() const;

public:
  enum RISCVMatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
    Match_RequiresEvenGPRs,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "RISCVGenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  static bool classifySymbolRef(const MCExpr *Expr,
                                RISCVMCExpr::VariantKind &Kind);
  static bool isSymbolDiff(const MCExpr *Expr);

  RISCVAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {
    MCAsmParserExtension::Initialize(Parser);

    Parser.addAliasForDirective(".half", ".2byte");
    Parser.addAliasForDirective(".hword", ".2byte");
    Parser.addAliasForDirective(".word", ".4byte");
    Parser.addAliasForDirective(".dword", ".8byte");
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));

    auto ABIName = StringRef(Options.ABIName);
    if (ABIName.ends_with("f") && !getSTI().hasFeature(RISCV::FeatureStdExtF)) {
      errs() << "Hard-float 'f' ABI can't be used for a target that "
                "doesn't support the F instruction set extension (ignoring "
                "target-abi)\n";
    } else if (ABIName.ends_with("d") &&
               !getSTI().hasFeature(RISCV::FeatureStdExtD)) {
      errs() << "Hard-float 'd' ABI can't be used for a target that "
                "doesn't support the D instruction set extension (ignoring "
                "target-abi)\n";
    }

    // Use computeTargetABI to check if ABIName is valid. If invalid, output
    // error message.
    RISCVABI::computeTargetABI(STI.getTargetTriple(), STI.getFeatureBits(),
                               ABIName);

    const MCObjectFileInfo *MOFI = Parser.getContext().getObjectFileInfo();
    ParserOptions.IsPicEnabled = MOFI->isPositionIndependent();

    if (AddBuildAttributes)
      getTargetStreamer().emitTargetAttributes(STI, /*EmitStackAlign*/ false);
  }
};

/// RISCVOperand - Instances of this class represent a parsed machine
/// instruction
struct RISCVOperand final : public MCParsedAsmOperand {

  enum class KindTy {
    Token,
    Register,
    Immediate,
    FPImmediate,
    SystemRegister,
    VType,
    FRM,
    Fence,
    Rlist,
    Spimm,
    RegReg,
  } Kind;

  struct RegOp {
    MCRegister RegNum;
    bool IsGPRAsFPR;
  };

  struct ImmOp {
    const MCExpr *Val;
    bool IsRV64;
  };

  struct FPImmOp {
    uint64_t Val;
  };

  struct SysRegOp {
    const char *Data;
    unsigned Length;
    unsigned Encoding;
    // FIXME: Add the Encoding parsed fields as needed for checks,
    // e.g.: read/write or user/supervisor/machine privileges.
  };

  struct VTypeOp {
    unsigned Val;
  };

  struct FRMOp {
    RISCVFPRndMode::RoundingMode FRM;
  };

  struct FenceOp {
    unsigned Val;
  };

  struct RlistOp {
    unsigned Val;
  };

  struct SpimmOp {
    unsigned Val;
  };

  struct RegRegOp {
    MCRegister Reg1;
    MCRegister Reg2;
  };

  SMLoc StartLoc, EndLoc;
  union {
    StringRef Tok;
    RegOp Reg;
    ImmOp Imm;
    FPImmOp FPImm;
    struct SysRegOp SysReg;
    struct VTypeOp VType;
    struct FRMOp FRM;
    struct FenceOp Fence;
    struct RlistOp Rlist;
    struct SpimmOp Spimm;
    struct RegRegOp RegReg;
  };

  RISCVOperand(KindTy K) : Kind(K) {}

public:
  RISCVOperand(const RISCVOperand &o) : MCParsedAsmOperand() {
    Kind = o.Kind;
    StartLoc = o.StartLoc;
    EndLoc = o.EndLoc;
    switch (Kind) {
    case KindTy::Register:
      Reg = o.Reg;
      break;
    case KindTy::Immediate:
      Imm = o.Imm;
      break;
    case KindTy::FPImmediate:
      FPImm = o.FPImm;
      break;
    case KindTy::Token:
      Tok = o.Tok;
      break;
    case KindTy::SystemRegister:
      SysReg = o.SysReg;
      break;
    case KindTy::VType:
      VType = o.VType;
      break;
    case KindTy::FRM:
      FRM = o.FRM;
      break;
    case KindTy::Fence:
      Fence = o.Fence;
      break;
    case KindTy::Rlist:
      Rlist = o.Rlist;
      break;
    case KindTy::Spimm:
      Spimm = o.Spimm;
      break;
    case KindTy::RegReg:
      RegReg = o.RegReg;
      break;
    }
  }

  bool isToken() const override { return Kind == KindTy::Token; }
  bool isReg() const override { return Kind == KindTy::Register; }
  bool isV0Reg() const {
    return Kind == KindTy::Register && Reg.RegNum == RISCV::V0;
  }
  bool isAnyReg() const {
    return Kind == KindTy::Register &&
           (RISCVMCRegisterClasses[RISCV::GPRRegClassID].contains(Reg.RegNum) ||
            RISCVMCRegisterClasses[RISCV::FPR64RegClassID].contains(Reg.RegNum) ||
            RISCVMCRegisterClasses[RISCV::VRRegClassID].contains(Reg.RegNum));
  }
  bool isAnyRegC() const {
    return Kind == KindTy::Register &&
           (RISCVMCRegisterClasses[RISCV::GPRCRegClassID].contains(
                Reg.RegNum) ||
            RISCVMCRegisterClasses[RISCV::FPR64CRegClassID].contains(
                Reg.RegNum));
  }
  bool isImm() const override { return Kind == KindTy::Immediate; }
  bool isMem() const override { return false; }
  bool isSystemRegister() const { return Kind == KindTy::SystemRegister; }
  bool isRegReg() const { return Kind == KindTy::RegReg; }
  bool isRlist() const { return Kind == KindTy::Rlist; }
  bool isSpimm() const { return Kind == KindTy::Spimm; }

  bool isGPR() const {
    return Kind == KindTy::Register &&
           RISCVMCRegisterClasses[RISCV::GPRRegClassID].contains(Reg.RegNum);
  }

  bool isGPRAsFPR() const { return isGPR() && Reg.IsGPRAsFPR; }

  bool isGPRPair() const {
    return Kind == KindTy::Register &&
           RISCVMCRegisterClasses[RISCV::GPRPairRegClassID].contains(
               Reg.RegNum);
  }

  static bool evaluateConstantImm(const MCExpr *Expr, int64_t &Imm,
                                  RISCVMCExpr::VariantKind &VK) {
    if (auto *RE = dyn_cast<RISCVMCExpr>(Expr)) {
      VK = RE->getKind();
      return RE->evaluateAsConstant(Imm);
    }

    if (auto CE = dyn_cast<MCConstantExpr>(Expr)) {
      VK = RISCVMCExpr::VK_RISCV_None;
      Imm = CE->getValue();
      return true;
    }

    return false;
  }

  // True if operand is a symbol with no modifiers, or a constant with no
  // modifiers and isShiftedInt<N-1, 1>(Op).
  template <int N> bool isBareSimmNLsb0() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    bool IsValid;
    if (!IsConstantImm)
      IsValid = RISCVAsmParser::classifySymbolRef(getImm(), VK);
    else
      IsValid = isShiftedInt<N - 1, 1>(Imm);
    return IsValid && VK == RISCVMCExpr::VK_RISCV_None;
  }

  // Predicate methods for AsmOperands defined in RISCVInstrInfo.td

  bool isBareSymbol() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    // Must be of 'immediate' type but not a constant.
    if (!isImm() || evaluateConstantImm(getImm(), Imm, VK))
      return false;
    return RISCVAsmParser::classifySymbolRef(getImm(), VK) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isCallSymbol() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    // Must be of 'immediate' type but not a constant.
    if (!isImm() || evaluateConstantImm(getImm(), Imm, VK))
      return false;
    return RISCVAsmParser::classifySymbolRef(getImm(), VK) &&
           (VK == RISCVMCExpr::VK_RISCV_CALL ||
            VK == RISCVMCExpr::VK_RISCV_CALL_PLT);
  }

  bool isPseudoJumpSymbol() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    // Must be of 'immediate' type but not a constant.
    if (!isImm() || evaluateConstantImm(getImm(), Imm, VK))
      return false;
    return RISCVAsmParser::classifySymbolRef(getImm(), VK) &&
           VK == RISCVMCExpr::VK_RISCV_CALL;
  }

  bool isTPRelAddSymbol() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    // Must be of 'immediate' type but not a constant.
    if (!isImm() || evaluateConstantImm(getImm(), Imm, VK))
      return false;
    return RISCVAsmParser::classifySymbolRef(getImm(), VK) &&
           VK == RISCVMCExpr::VK_RISCV_TPREL_ADD;
  }

  bool isTLSDESCCallSymbol() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    // Must be of 'immediate' type but not a constant.
    if (!isImm() || evaluateConstantImm(getImm(), Imm, VK))
      return false;
    return RISCVAsmParser::classifySymbolRef(getImm(), VK) &&
           VK == RISCVMCExpr::VK_RISCV_TLSDESC_CALL;
  }

  bool isCSRSystemRegister() const { return isSystemRegister(); }

  bool isVTypeImm(unsigned N) const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isUIntN(N, Imm) && VK == RISCVMCExpr::VK_RISCV_None;
  }

  // If the last operand of the vsetvli/vsetvli instruction is a constant
  // expression, KindTy is Immediate.
  bool isVTypeI10() const {
    if (Kind == KindTy::Immediate)
      return isVTypeImm(10);
    return Kind == KindTy::VType;
  }
  bool isVTypeI11() const {
    if (Kind == KindTy::Immediate)
      return isVTypeImm(11);
    return Kind == KindTy::VType;
  }

  /// Return true if the operand is a valid for the fence instruction e.g.
  /// ('iorw').
  bool isFenceArg() const { return Kind == KindTy::Fence; }

  /// Return true if the operand is a valid floating point rounding mode.
  bool isFRMArg() const { return Kind == KindTy::FRM; }
  bool isFRMArgLegacy() const { return Kind == KindTy::FRM; }
  bool isRTZArg() const { return isFRMArg() && FRM.FRM == RISCVFPRndMode::RTZ; }

  /// Return true if the operand is a valid fli.s floating-point immediate.
  bool isLoadFPImm() const {
    if (isImm())
      return isUImm5();
    if (Kind != KindTy::FPImmediate)
      return false;
    int Idx = RISCVLoadFPImm::getLoadFPImm(
        APFloat(APFloat::IEEEdouble(), APInt(64, getFPConst())));
    // Don't allow decimal version of the minimum value. It is a different value
    // for each supported data type.
    return Idx >= 0 && Idx != 1;
  }

  bool isImmXLenLI() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    if (VK == RISCVMCExpr::VK_RISCV_LO ||
        VK == RISCVMCExpr::VK_RISCV_PCREL_LO ||
        VK == RISCVMCExpr::VK_RISCV_TLSDESC_LOAD_LO ||
        VK == RISCVMCExpr::VK_RISCV_TLSDESC_ADD_LO)
      return true;
    // Given only Imm, ensuring that the actually specified constant is either
    // a signed or unsigned 64-bit number is unfortunately impossible.
    if (IsConstantImm) {
      return VK == RISCVMCExpr::VK_RISCV_None &&
             (isRV64Imm() || (isInt<32>(Imm) || isUInt<32>(Imm)));
    }

    return RISCVAsmParser::isSymbolDiff(getImm());
  }

  bool isImmXLenLI_Restricted() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    // 'la imm' supports constant immediates only.
    return IsConstantImm && (VK == RISCVMCExpr::VK_RISCV_None) &&
           (isRV64Imm() || (isInt<32>(Imm) || isUInt<32>(Imm)));
  }

  bool isUImmLog2XLen() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    if (!evaluateConstantImm(getImm(), Imm, VK) ||
        VK != RISCVMCExpr::VK_RISCV_None)
      return false;
    return (isRV64Imm() && isUInt<6>(Imm)) || isUInt<5>(Imm);
  }

  bool isUImmLog2XLenNonZero() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    if (!evaluateConstantImm(getImm(), Imm, VK) ||
        VK != RISCVMCExpr::VK_RISCV_None)
      return false;
    if (Imm == 0)
      return false;
    return (isRV64Imm() && isUInt<6>(Imm)) || isUInt<5>(Imm);
  }

  bool isUImmLog2XLenHalf() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    if (!evaluateConstantImm(getImm(), Imm, VK) ||
        VK != RISCVMCExpr::VK_RISCV_None)
      return false;
    return (isRV64Imm() && isUInt<5>(Imm)) || isUInt<4>(Imm);
  }

  template <unsigned N> bool IsUImm() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isUInt<N>(Imm) && VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm1() const { return IsUImm<1>(); }
  bool isUImm2() const { return IsUImm<2>(); }
  bool isUImm3() const { return IsUImm<3>(); }
  bool isUImm4() const { return IsUImm<4>(); }
  bool isUImm5() const { return IsUImm<5>(); }
  bool isUImm6() const { return IsUImm<6>(); }
  bool isUImm7() const { return IsUImm<7>(); }
  bool isUImm8() const { return IsUImm<8>(); }
  bool isUImm16() const { return IsUImm<16>(); }
  bool isUImm20() const { return IsUImm<20>(); }
  bool isUImm32() const { return IsUImm<32>(); }

  bool isUImm8GE32() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isUInt<8>(Imm) && Imm >= 32 &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isRnumArg() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && Imm >= INT64_C(0) && Imm <= INT64_C(10) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isRnumArg_0_7() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && Imm >= INT64_C(0) && Imm <= INT64_C(7) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isRnumArg_1_10() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && Imm >= INT64_C(1) && Imm <= INT64_C(10) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isRnumArg_2_14() const {
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && Imm >= INT64_C(2) && Imm <= INT64_C(14) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isSImm5() const {
    if (!isImm())
      return false;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isInt<5>(fixImmediateForRV32(Imm, isRV64Imm())) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isSImm6() const {
    if (!isImm())
      return false;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isInt<6>(fixImmediateForRV32(Imm, isRV64Imm())) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isSImm6NonZero() const {
    if (!isImm())
      return false;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && Imm != 0 &&
           isInt<6>(fixImmediateForRV32(Imm, isRV64Imm())) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isCLUIImm() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && (Imm != 0) &&
           (isUInt<5>(Imm) || (Imm >= 0xfffe0 && Imm <= 0xfffff)) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm2Lsb0() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<1, 1>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm5Lsb0() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<4, 1>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm6Lsb0() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<5, 1>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm7Lsb00() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<5, 2>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm8Lsb00() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<6, 2>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm8Lsb000() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<5, 3>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isSImm9Lsb0() const { return isBareSimmNLsb0<9>(); }

  bool isUImm9Lsb000() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<6, 3>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm10Lsb00NonZero() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedUInt<8, 2>(Imm) && (Imm != 0) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  // If this a RV32 and the immediate is a uimm32, sign extend it to 32 bits.
  // This allows writing 'addi a0, a0, 0xffffffff'.
  static int64_t fixImmediateForRV32(int64_t Imm, bool IsRV64Imm) {
    if (IsRV64Imm || !isUInt<32>(Imm))
      return Imm;
    return SignExtend64<32>(Imm);
  }

  bool isSImm12() const {
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsValid;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    if (!IsConstantImm)
      IsValid = RISCVAsmParser::classifySymbolRef(getImm(), VK);
    else
      IsValid = isInt<12>(fixImmediateForRV32(Imm, isRV64Imm()));
    return IsValid && ((IsConstantImm && VK == RISCVMCExpr::VK_RISCV_None) ||
                       VK == RISCVMCExpr::VK_RISCV_LO ||
                       VK == RISCVMCExpr::VK_RISCV_PCREL_LO ||
                       VK == RISCVMCExpr::VK_RISCV_TPREL_LO ||
                       VK == RISCVMCExpr::VK_RISCV_TLSDESC_LOAD_LO ||
                       VK == RISCVMCExpr::VK_RISCV_TLSDESC_ADD_LO);
  }

  bool isSImm12Lsb0() const { return isBareSimmNLsb0<12>(); }

  bool isSImm12Lsb00000() const {
    if (!isImm())
      return false;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && isShiftedInt<7, 5>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isSImm13Lsb0() const { return isBareSimmNLsb0<13>(); }

  bool isSImm10Lsb0000NonZero() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && (Imm != 0) && isShiftedInt<6, 4>(Imm) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isUImm20LUI() const {
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsValid;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    if (!IsConstantImm) {
      IsValid = RISCVAsmParser::classifySymbolRef(getImm(), VK);
      return IsValid && (VK == RISCVMCExpr::VK_RISCV_HI ||
                         VK == RISCVMCExpr::VK_RISCV_TPREL_HI);
    } else {
      return isUInt<20>(Imm) && (VK == RISCVMCExpr::VK_RISCV_None ||
                                 VK == RISCVMCExpr::VK_RISCV_HI ||
                                 VK == RISCVMCExpr::VK_RISCV_TPREL_HI);
    }
  }

  bool isUImm20AUIPC() const {
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsValid;
    if (!isImm())
      return false;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    if (!IsConstantImm) {
      IsValid = RISCVAsmParser::classifySymbolRef(getImm(), VK);
      return IsValid && (VK == RISCVMCExpr::VK_RISCV_PCREL_HI ||
                         VK == RISCVMCExpr::VK_RISCV_GOT_HI ||
                         VK == RISCVMCExpr::VK_RISCV_TLS_GOT_HI ||
                         VK == RISCVMCExpr::VK_RISCV_TLS_GD_HI ||
                         VK == RISCVMCExpr::VK_RISCV_TLSDESC_HI);
    }

    return isUInt<20>(Imm) && (VK == RISCVMCExpr::VK_RISCV_None ||
                               VK == RISCVMCExpr::VK_RISCV_PCREL_HI ||
                               VK == RISCVMCExpr::VK_RISCV_GOT_HI ||
                               VK == RISCVMCExpr::VK_RISCV_TLS_GOT_HI ||
                               VK == RISCVMCExpr::VK_RISCV_TLS_GD_HI ||
                               VK == RISCVMCExpr::VK_RISCV_TLSDESC_HI);
  }

  bool isSImm21Lsb0JAL() const { return isBareSimmNLsb0<21>(); }

  bool isImmZero() const {
    if (!isImm())
      return false;
    int64_t Imm;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm && (Imm == 0) && VK == RISCVMCExpr::VK_RISCV_None;
  }

  bool isSImm5Plus1() const {
    if (!isImm())
      return false;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    int64_t Imm;
    bool IsConstantImm = evaluateConstantImm(getImm(), Imm, VK);
    return IsConstantImm &&
           isInt<5>(fixImmediateForRV32(Imm, isRV64Imm()) - 1) &&
           VK == RISCVMCExpr::VK_RISCV_None;
  }

  /// getStartLoc - Gets location of the first token of this operand
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Gets location of the last token of this operand
  SMLoc getEndLoc() const override { return EndLoc; }
  /// True if this operand is for an RV64 instruction
  bool isRV64Imm() const {
    assert(Kind == KindTy::Immediate && "Invalid type access!");
    return Imm.IsRV64;
  }

  MCRegister getReg() const override {
    assert(Kind == KindTy::Register && "Invalid type access!");
    return Reg.RegNum;
  }

  StringRef getSysReg() const {
    assert(Kind == KindTy::SystemRegister && "Invalid type access!");
    return StringRef(SysReg.Data, SysReg.Length);
  }

  const MCExpr *getImm() const {
    assert(Kind == KindTy::Immediate && "Invalid type access!");
    return Imm.Val;
  }

  uint64_t getFPConst() const {
    assert(Kind == KindTy::FPImmediate && "Invalid type access!");
    return FPImm.Val;
  }

  StringRef getToken() const {
    assert(Kind == KindTy::Token && "Invalid type access!");
    return Tok;
  }

  unsigned getVType() const {
    assert(Kind == KindTy::VType && "Invalid type access!");
    return VType.Val;
  }

  RISCVFPRndMode::RoundingMode getFRM() const {
    assert(Kind == KindTy::FRM && "Invalid type access!");
    return FRM.FRM;
  }

  unsigned getFence() const {
    assert(Kind == KindTy::Fence && "Invalid type access!");
    return Fence.Val;
  }

  void print(raw_ostream &OS) const override {
    auto RegName = [](MCRegister Reg) {
      if (Reg)
        return RISCVInstPrinter::getRegisterName(Reg);
      else
        return "noreg";
    };

    switch (Kind) {
    case KindTy::Immediate:
      OS << *getImm();
      break;
    case KindTy::FPImmediate:
      break;
    case KindTy::Register:
      OS << "<register " << RegName(getReg()) << ">";
      break;
    case KindTy::Token:
      OS << "'" << getToken() << "'";
      break;
    case KindTy::SystemRegister:
      OS << "<sysreg: " << getSysReg() << '>';
      break;
    case KindTy::VType:
      OS << "<vtype: ";
      RISCVVType::printVType(getVType(), OS);
      OS << '>';
      break;
    case KindTy::FRM:
      OS << "<frm: ";
      roundingModeToString(getFRM());
      OS << '>';
      break;
    case KindTy::Fence:
      OS << "<fence: ";
      OS << getFence();
      OS << '>';
      break;
    case KindTy::Rlist:
      OS << "<rlist: ";
      RISCVZC::printRlist(Rlist.Val, OS);
      OS << '>';
      break;
    case KindTy::Spimm:
      OS << "<Spimm: ";
      OS << Spimm.Val;
      OS << '>';
      break;
    case KindTy::RegReg:
      OS << "<RegReg:  Reg1 " << RegName(RegReg.Reg1);
      OS << " Reg2 " << RegName(RegReg.Reg2);
      break;
    }
  }

  static std::unique_ptr<RISCVOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::Token);
    Op->Tok = Str;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand>
  createReg(unsigned RegNo, SMLoc S, SMLoc E, bool IsGPRAsFPR = false) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::Register);
    Op->Reg.RegNum = RegNo;
    Op->Reg.IsGPRAsFPR = IsGPRAsFPR;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createImm(const MCExpr *Val, SMLoc S,
                                                 SMLoc E, bool IsRV64) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::Immediate);
    Op->Imm.Val = Val;
    Op->Imm.IsRV64 = IsRV64;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createFPImm(uint64_t Val, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::FPImmediate);
    Op->FPImm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createSysReg(StringRef Str, SMLoc S,
                                                    unsigned Encoding) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::SystemRegister);
    Op->SysReg.Data = Str.data();
    Op->SysReg.Length = Str.size();
    Op->SysReg.Encoding = Encoding;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand>
  createFRMArg(RISCVFPRndMode::RoundingMode FRM, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::FRM);
    Op->FRM.FRM = FRM;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createFenceArg(unsigned Val, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::Fence);
    Op->Fence.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createVType(unsigned VTypeI, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::VType);
    Op->VType.Val = VTypeI;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createRlist(unsigned RlistEncode,
                                                   SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::Rlist);
    Op->Rlist.Val = RlistEncode;
    Op->StartLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createRegReg(unsigned Reg1No,
                                                    unsigned Reg2No, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::RegReg);
    Op->RegReg.Reg1 = Reg1No;
    Op->RegReg.Reg2 = Reg2No;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<RISCVOperand> createSpimm(unsigned Spimm, SMLoc S) {
    auto Op = std::make_unique<RISCVOperand>(KindTy::Spimm);
    Op->Spimm.Val = Spimm;
    Op->StartLoc = S;
    return Op;
  }

  static void addExpr(MCInst &Inst, const MCExpr *Expr, bool IsRV64Imm) {
    assert(Expr && "Expr shouldn't be null!");
    int64_t Imm = 0;
    RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
    bool IsConstant = evaluateConstantImm(Expr, Imm, VK);

    if (IsConstant)
      Inst.addOperand(
          MCOperand::createImm(fixImmediateForRV32(Imm, IsRV64Imm)));
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
    addExpr(Inst, getImm(), isRV64Imm());
  }

  void addFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    if (isImm()) {
      addExpr(Inst, getImm(), isRV64Imm());
      return;
    }

    int Imm = RISCVLoadFPImm::getLoadFPImm(
        APFloat(APFloat::IEEEdouble(), APInt(64, getFPConst())));
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addFenceArgOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(Fence.Val));
  }

  void addCSRSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(SysReg.Encoding));
  }

  // Support non-canonical syntax:
  // "vsetivli rd, uimm, 0xabc" or "vsetvli rd, rs1, 0xabc"
  // "vsetivli rd, uimm, (0xc << N)" or "vsetvli rd, rs1, (0xc << N)"
  void addVTypeIOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    int64_t Imm = 0;
    if (Kind == KindTy::Immediate) {
      RISCVMCExpr::VariantKind VK = RISCVMCExpr::VK_RISCV_None;
      [[maybe_unused]] bool IsConstantImm =
          evaluateConstantImm(getImm(), Imm, VK);
      assert(IsConstantImm && "Invalid VTypeI Operand!");
    } else {
      Imm = getVType();
    }
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addRlistOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(Rlist.Val));
  }

  void addRegRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(RegReg.Reg1));
    Inst.addOperand(MCOperand::createReg(RegReg.Reg2));
  }

  void addSpimmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(Spimm.Val));
  }

  void addFRMArgOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getFRM()));
  }
};
} // end anonymous namespace.

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "RISCVGenAsmMatcher.inc"

static MCRegister convertFPR64ToFPR16(MCRegister Reg) {
  assert(Reg >= RISCV::F0_D && Reg <= RISCV::F31_D && "Invalid register");
  return Reg - RISCV::F0_D + RISCV::F0_H;
}

static MCRegister convertFPR64ToFPR32(MCRegister Reg) {
  assert(Reg >= RISCV::F0_D && Reg <= RISCV::F31_D && "Invalid register");
  return Reg - RISCV::F0_D + RISCV::F0_F;
}

static MCRegister convertVRToVRMx(const MCRegisterInfo &RI, MCRegister Reg,
                                  unsigned Kind) {
  unsigned RegClassID;
  if (Kind == MCK_VRM2)
    RegClassID = RISCV::VRM2RegClassID;
  else if (Kind == MCK_VRM4)
    RegClassID = RISCV::VRM4RegClassID;
  else if (Kind == MCK_VRM8)
    RegClassID = RISCV::VRM8RegClassID;
  else
    return 0;
  return RI.getMatchingSuperReg(Reg, RISCV::sub_vrm1_0,
                                &RISCVMCRegisterClasses[RegClassID]);
}

unsigned RISCVAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                    unsigned Kind) {
  RISCVOperand &Op = static_cast<RISCVOperand &>(AsmOp);
  if (!Op.isReg())
    return Match_InvalidOperand;

  MCRegister Reg = Op.getReg();
  bool IsRegFPR64 =
      RISCVMCRegisterClasses[RISCV::FPR64RegClassID].contains(Reg);
  bool IsRegFPR64C =
      RISCVMCRegisterClasses[RISCV::FPR64CRegClassID].contains(Reg);
  bool IsRegVR = RISCVMCRegisterClasses[RISCV::VRRegClassID].contains(Reg);

  // As the parser couldn't differentiate an FPR32 from an FPR64, coerce the
  // register from FPR64 to FPR32 or FPR64C to FPR32C if necessary.
  if ((IsRegFPR64 && Kind == MCK_FPR32) ||
      (IsRegFPR64C && Kind == MCK_FPR32C)) {
    Op.Reg.RegNum = convertFPR64ToFPR32(Reg);
    return Match_Success;
  }
  // As the parser couldn't differentiate an FPR16 from an FPR64, coerce the
  // register from FPR64 to FPR16 if necessary.
  if (IsRegFPR64 && Kind == MCK_FPR16) {
    Op.Reg.RegNum = convertFPR64ToFPR16(Reg);
    return Match_Success;
  }
  // As the parser couldn't differentiate an VRM2/VRM4/VRM8 from an VR, coerce
  // the register from VR to VRM2/VRM4/VRM8 if necessary.
  if (IsRegVR && (Kind == MCK_VRM2 || Kind == MCK_VRM4 || Kind == MCK_VRM8)) {
    Op.Reg.RegNum = convertVRToVRMx(*getContext().getRegisterInfo(), Reg, Kind);
    if (Op.Reg.RegNum == 0)
      return Match_InvalidOperand;
    return Match_Success;
  }
  return Match_InvalidOperand;
}

unsigned RISCVAsmParser::checkTargetMatchPredicate(MCInst &Inst) {
  const MCInstrDesc &MCID = MII.get(Inst.getOpcode());

  for (unsigned I = 0; I < MCID.NumOperands; ++I) {
    if (MCID.operands()[I].RegClass == RISCV::GPRPairRegClassID) {
      const auto &Op = Inst.getOperand(I);
      assert(Op.isReg());

      MCRegister Reg = Op.getReg();
      if (RISCVMCRegisterClasses[RISCV::GPRPairRegClassID].contains(Reg))
        continue;

      // FIXME: We should form a paired register during parsing/matching.
      if (((Reg.id() - RISCV::X0) & 1) != 0)
        return Match_RequiresEvenGPRs;
    }
  }

  return Match_Success;
}

bool RISCVAsmParser::generateImmOutOfRangeError(
    SMLoc ErrorLoc, int64_t Lower, int64_t Upper,
    const Twine &Msg = "immediate must be an integer in the range") {
  return Error(ErrorLoc, Msg + " [" + Twine(Lower) + ", " + Twine(Upper) + "]");
}

bool RISCVAsmParser::generateImmOutOfRangeError(
    OperandVector &Operands, uint64_t ErrorInfo, int64_t Lower, int64_t Upper,
    const Twine &Msg = "immediate must be an integer in the range") {
  SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
  return generateImmOutOfRangeError(ErrorLoc, Lower, Upper, Msg);
}

bool RISCVAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
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
    if (validateInstruction(Inst, Operands))
      return true;
    return processInstruction(Inst, IDLoc, Operands, Out);
  case Match_MissingFeature: {
    assert(MissingFeatures.any() && "Unknown missing features!");
    bool FirstFeature = true;
    std::string Msg = "instruction requires the following:";
    for (unsigned i = 0, e = MissingFeatures.size(); i != e; ++i) {
      if (MissingFeatures[i]) {
        Msg += FirstFeature ? " " : ", ";
        Msg += getSubtargetFeatureName(i);
        FirstFeature = false;
      }
    }
    return Error(IDLoc, Msg);
  }
  case Match_MnemonicFail: {
    FeatureBitset FBS = ComputeAvailableFeatures(getSTI().getFeatureBits());
    std::string Suggestion = RISCVMnemonicSpellCheck(
        ((RISCVOperand &)*Operands[0]).getToken(), FBS, 0);
    return Error(IDLoc, "unrecognized instruction mnemonic" + Suggestion);
  }
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");

      ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
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
    if (ErrorInfo != ~0ULL && ErrorInfo >= Operands.size())
      return Error(ErrorLoc, "too few operands for instruction");
  }

  switch (Result) {
  default:
    break;
  case Match_RequiresEvenGPRs:
    return Error(IDLoc,
                 "double precision floating point operands must use even "
                 "numbered X register");
  case Match_InvalidImmXLenLI:
    if (isRV64()) {
      SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
      return Error(ErrorLoc, "operand must be a constant 64-bit integer");
    }
    return generateImmOutOfRangeError(Operands, ErrorInfo,
                                      std::numeric_limits<int32_t>::min(),
                                      std::numeric_limits<uint32_t>::max());
  case Match_InvalidImmXLenLI_Restricted:
    if (isRV64()) {
      SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
      return Error(ErrorLoc, "operand either must be a constant 64-bit integer "
                             "or a bare symbol name");
    }
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<uint32_t>::max(),
        "operand either must be a bare symbol name or an immediate integer in "
        "the range");
  case Match_InvalidImmZero: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "immediate must be zero");
  }
  case Match_InvalidUImmLog2XLen:
    if (isRV64())
      return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 6) - 1);
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 5) - 1);
  case Match_InvalidUImmLog2XLenNonZero:
    if (isRV64())
      return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 6) - 1);
    return generateImmOutOfRangeError(Operands, ErrorInfo, 1, (1 << 5) - 1);
  case Match_InvalidUImmLog2XLenHalf:
    if (isRV64())
      return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 5) - 1);
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 4) - 1);
  case Match_InvalidUImm1:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 1) - 1);
  case Match_InvalidUImm2:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 2) - 1);
  case Match_InvalidUImm2Lsb0:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, 2,
                                      "immediate must be one of");
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
  case Match_InvalidUImm8GE32:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 32, (1 << 8) - 1);
  case Match_InvalidSImm5:
    return generateImmOutOfRangeError(Operands, ErrorInfo, -(1 << 4),
                                      (1 << 4) - 1);
  case Match_InvalidSImm6:
    return generateImmOutOfRangeError(Operands, ErrorInfo, -(1 << 5),
                                      (1 << 5) - 1);
  case Match_InvalidSImm6NonZero:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 5), (1 << 5) - 1,
        "immediate must be non-zero in the range");
  case Match_InvalidCLUIImm:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 1, (1 << 5) - 1,
        "immediate must be in [0xfffe0, 0xfffff] or");
  case Match_InvalidUImm5Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 5) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm6Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 6) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm7Lsb00:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 7) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidUImm8Lsb00:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 8) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidUImm8Lsb000:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 8) - 8,
        "immediate must be a multiple of 8 bytes in the range");
  case Match_InvalidSImm9Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 8), (1 << 8) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm9Lsb000:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 9) - 8,
        "immediate must be a multiple of 8 bytes in the range");
  case Match_InvalidUImm10Lsb00NonZero:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 4, (1 << 10) - 4,
        "immediate must be a multiple of 4 bytes in the range");
  case Match_InvalidSImm10Lsb0000NonZero:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 9), (1 << 9) - 16,
        "immediate must be a multiple of 16 bytes and non-zero in the range");
  case Match_InvalidSImm12:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 11), (1 << 11) - 1,
        "operand must be a symbol with %lo/%pcrel_lo/%tprel_lo modifier or an "
        "integer in the range");
  case Match_InvalidSImm12Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 11), (1 << 11) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidSImm12Lsb00000:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 11), (1 << 11) - 32,
        "immediate must be a multiple of 32 bytes in the range");
  case Match_InvalidSImm13Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 12), (1 << 12) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm20LUI:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 20) - 1,
                                      "operand must be a symbol with "
                                      "%hi/%tprel_hi modifier or an integer in "
                                      "the range");
  case Match_InvalidUImm20:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 20) - 1);
  case Match_InvalidUImm20AUIPC:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, 0, (1 << 20) - 1,
        "operand must be a symbol with a "
        "%pcrel_hi/%got_pcrel_hi/%tls_ie_pcrel_hi/%tls_gd_pcrel_hi modifier or "
        "an integer in the range");
  case Match_InvalidSImm21Lsb0JAL:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 20), (1 << 20) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidCSRSystemRegister: {
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 12) - 1,
                                      "operand must be a valid system register "
                                      "name or an integer in the range");
  }
  case Match_InvalidLoadFPImm: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a valid floating-point constant");
  }
  case Match_InvalidBareSymbol: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a bare symbol name");
  }
  case Match_InvalidPseudoJumpSymbol: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a valid jump target");
  }
  case Match_InvalidCallSymbol: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a bare symbol name");
  }
  case Match_InvalidTPRelAddSymbol: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be a symbol with %tprel_add modifier");
  }
  case Match_InvalidTLSDESCCallSymbol: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc,
                 "operand must be a symbol with %tlsdesc_call modifier");
  }
  case Match_InvalidRTZArg: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be 'rtz' floating-point rounding mode");
  }
  case Match_InvalidVTypeI: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return generateVTypeError(ErrorLoc);
  }
  case Match_InvalidVMaskRegister: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operand must be v0.t");
  }
  case Match_InvalidSImm5Plus1: {
    return generateImmOutOfRangeError(Operands, ErrorInfo, -(1 << 4) + 1,
                                      (1 << 4),
                                      "immediate must be in the range");
  }
  case Match_InvalidRlist: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(
        ErrorLoc,
        "operand must be {ra [, s0[-sN]]} or {x1 [, x8[-x9][, x18[-xN]]]}");
  }
  case Match_InvalidStackAdj: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(
        ErrorLoc,
        "stack adjustment is invalid for this instruction and register list; "
        "refer to Zc spec for a detailed range of stack adjustment");
  }
  case Match_InvalidRnumArg: {
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, 10);
  }
  case Match_InvalidRegReg: {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "operands must be register and register");
  }
  }

  llvm_unreachable("Unknown match type detected!");
}

// Attempts to match Name as a register (either using the default name or
// alternative ABI names), setting RegNo to the matching register. Upon
// failure, returns a non-valid MCRegister. If IsRVE, then registers x16-x31
// will be rejected.
MCRegister RISCVAsmParser::matchRegisterNameHelper(StringRef Name) const {
  MCRegister Reg = MatchRegisterName(Name);
  // The 16-/32- and 64-bit FPRs have the same asm name. Check that the initial
  // match always matches the 64-bit variant, and not the 16/32-bit one.
  assert(!(Reg >= RISCV::F0_H && Reg <= RISCV::F31_H));
  assert(!(Reg >= RISCV::F0_F && Reg <= RISCV::F31_F));
  // The default FPR register class is based on the tablegen enum ordering.
  static_assert(RISCV::F0_D < RISCV::F0_H, "FPR matching must be updated");
  static_assert(RISCV::F0_D < RISCV::F0_F, "FPR matching must be updated");
  if (!Reg)
    Reg = MatchRegisterAltName(Name);
  if (isRVE() && Reg >= RISCV::X16 && Reg <= RISCV::X31)
    Reg = RISCV::NoRegister;
  return Reg;
}

bool RISCVAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(StartLoc, "invalid register name");
  return false;
}

ParseStatus RISCVAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                             SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  StringRef Name = getLexer().getTok().getIdentifier();

  Reg = matchRegisterNameHelper(Name);
  if (!Reg)
    return ParseStatus::NoMatch;

  getParser().Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseRegister(OperandVector &Operands,
                                          bool AllowParens) {
  SMLoc FirstS = getLoc();
  bool HadParens = false;
  AsmToken LParen;

  // If this is an LParen and a parenthesised register name is allowed, parse it
  // atomically.
  if (AllowParens && getLexer().is(AsmToken::LParen)) {
    AsmToken Buf[2];
    size_t ReadCount = getLexer().peekTokens(Buf);
    if (ReadCount == 2 && Buf[1].getKind() == AsmToken::RParen) {
      HadParens = true;
      LParen = getParser().getTok();
      getParser().Lex(); // Eat '('
    }
  }

  switch (getLexer().getKind()) {
  default:
    if (HadParens)
      getLexer().UnLex(LParen);
    return ParseStatus::NoMatch;
  case AsmToken::Identifier:
    StringRef Name = getLexer().getTok().getIdentifier();
    MCRegister RegNo = matchRegisterNameHelper(Name);

    if (!RegNo) {
      if (HadParens)
        getLexer().UnLex(LParen);
      return ParseStatus::NoMatch;
    }
    if (HadParens)
      Operands.push_back(RISCVOperand::createToken("(", FirstS));
    SMLoc S = getLoc();
    SMLoc E = SMLoc::getFromPointer(S.getPointer() + Name.size());
    getLexer().Lex();
    Operands.push_back(RISCVOperand::createReg(RegNo, S, E));
  }

  if (HadParens) {
    getParser().Lex(); // Eat ')'
    Operands.push_back(RISCVOperand::createToken(")", getLoc()));
  }

  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseInsnDirectiveOpcode(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E;
  const MCExpr *Res;

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Exclaim:
  case AsmToken::Tilde:
  case AsmToken::Integer:
  case AsmToken::String: {
    if (getParser().parseExpression(Res, E))
      return ParseStatus::Failure;

    auto *CE = dyn_cast<MCConstantExpr>(Res);
    if (CE) {
      int64_t Imm = CE->getValue();
      if (isUInt<7>(Imm)) {
        Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
        return ParseStatus::Success;
      }
    }

    break;
  }
  case AsmToken::Identifier: {
    StringRef Identifier;
    if (getParser().parseIdentifier(Identifier))
      return ParseStatus::Failure;

    auto Opcode = RISCVInsnOpcode::lookupRISCVOpcodeByName(Identifier);
    if (Opcode) {
      assert(isUInt<7>(Opcode->Value) && (Opcode->Value & 0x3) == 3 &&
             "Unexpected opcode");
      Res = MCConstantExpr::create(Opcode->Value, getContext());
      E = SMLoc::getFromPointer(S.getPointer() + Identifier.size());
      Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
      return ParseStatus::Success;
    }

    break;
  }
  case AsmToken::Percent:
    break;
  }

  return generateImmOutOfRangeError(
      S, 0, 127,
      "opcode must be a valid opcode name or an immediate in the range");
}

ParseStatus RISCVAsmParser::parseInsnCDirectiveOpcode(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E;
  const MCExpr *Res;

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Exclaim:
  case AsmToken::Tilde:
  case AsmToken::Integer:
  case AsmToken::String: {
    if (getParser().parseExpression(Res, E))
      return ParseStatus::Failure;

    auto *CE = dyn_cast<MCConstantExpr>(Res);
    if (CE) {
      int64_t Imm = CE->getValue();
      if (Imm >= 0 && Imm <= 2) {
        Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
        return ParseStatus::Success;
      }
    }

    break;
  }
  case AsmToken::Identifier: {
    StringRef Identifier;
    if (getParser().parseIdentifier(Identifier))
      return ParseStatus::Failure;

    unsigned Opcode;
    if (Identifier == "C0")
      Opcode = 0;
    else if (Identifier == "C1")
      Opcode = 1;
    else if (Identifier == "C2")
      Opcode = 2;
    else
      break;

    Res = MCConstantExpr::create(Opcode, getContext());
    E = SMLoc::getFromPointer(S.getPointer() + Identifier.size());
    Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
    return ParseStatus::Success;
  }
  case AsmToken::Percent: {
    // Discard operand with modifier.
    break;
  }
  }

  return generateImmOutOfRangeError(
      S, 0, 2,
      "opcode must be a valid opcode name or an immediate in the range");
}

ParseStatus RISCVAsmParser::parseCSRSystemRegister(OperandVector &Operands) {
  SMLoc S = getLoc();
  const MCExpr *Res;

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Exclaim:
  case AsmToken::Tilde:
  case AsmToken::Integer:
  case AsmToken::String: {
    if (getParser().parseExpression(Res))
      return ParseStatus::Failure;

    auto *CE = dyn_cast<MCConstantExpr>(Res);
    if (CE) {
      int64_t Imm = CE->getValue();
      if (isUInt<12>(Imm)) {
        auto Range = RISCVSysReg::lookupSysRegByEncoding(Imm);
        // Accept an immediate representing a named Sys Reg if it satisfies the
        // the required features.
        for (auto &Reg : Range) {
          if (Reg.haveRequiredFeatures(STI->getFeatureBits())) {
            Operands.push_back(RISCVOperand::createSysReg(Reg.Name, S, Imm));
            return ParseStatus::Success;
          }
        }
        // Accept an immediate representing an un-named Sys Reg if the range is
        // valid, regardless of the required features.
        Operands.push_back(RISCVOperand::createSysReg("", S, Imm));
        return ParseStatus::Success;
      }
    }

    return generateImmOutOfRangeError(S, 0, (1 << 12) - 1);
  }
  case AsmToken::Identifier: {
    StringRef Identifier;
    if (getParser().parseIdentifier(Identifier))
      return ParseStatus::Failure;

    auto SysReg = RISCVSysReg::lookupSysRegByName(Identifier);
    if (!SysReg)
      SysReg = RISCVSysReg::lookupSysRegByAltName(Identifier);
    if (!SysReg)
      if ((SysReg = RISCVSysReg::lookupSysRegByDeprecatedName(Identifier)))
        Warning(S, "'" + Identifier + "' is a deprecated alias for '" +
                       SysReg->Name + "'");

    // Accept a named Sys Reg if the required features are present.
    if (SysReg) {
      if (!SysReg->haveRequiredFeatures(getSTI().getFeatureBits()))
        return Error(S, "system register use requires an option to be enabled");
      Operands.push_back(
          RISCVOperand::createSysReg(Identifier, S, SysReg->Encoding));
      return ParseStatus::Success;
    }

    return generateImmOutOfRangeError(S, 0, (1 << 12) - 1,
                                      "operand must be a valid system register "
                                      "name or an integer in the range");
  }
  case AsmToken::Percent: {
    // Discard operand with modifier.
    return generateImmOutOfRangeError(S, 0, (1 << 12) - 1);
  }
  }

  return ParseStatus::NoMatch;
}

ParseStatus RISCVAsmParser::parseFPImm(OperandVector &Operands) {
  SMLoc S = getLoc();

  // Parse special floats (inf/nan/min) representation.
  if (getTok().is(AsmToken::Identifier)) {
    StringRef Identifier = getTok().getIdentifier();
    if (Identifier.compare_insensitive("inf") == 0) {
      Operands.push_back(
          RISCVOperand::createImm(MCConstantExpr::create(30, getContext()), S,
                                  getTok().getEndLoc(), isRV64()));
    } else if (Identifier.compare_insensitive("nan") == 0) {
      Operands.push_back(
          RISCVOperand::createImm(MCConstantExpr::create(31, getContext()), S,
                                  getTok().getEndLoc(), isRV64()));
    } else if (Identifier.compare_insensitive("min") == 0) {
      Operands.push_back(
          RISCVOperand::createImm(MCConstantExpr::create(1, getContext()), S,
                                  getTok().getEndLoc(), isRV64()));
    } else {
      return TokError("invalid floating point literal");
    }

    Lex(); // Eat the token.

    return ParseStatus::Success;
  }

  // Handle negation, as that still comes through as a separate token.
  bool IsNegative = parseOptionalToken(AsmToken::Minus);

  const AsmToken &Tok = getTok();
  if (!Tok.is(AsmToken::Real))
    return TokError("invalid floating point immediate");

  // Parse FP representation.
  APFloat RealVal(APFloat::IEEEdouble());
  auto StatusOrErr =
      RealVal.convertFromString(Tok.getString(), APFloat::rmTowardZero);
  if (errorToBool(StatusOrErr.takeError()))
    return TokError("invalid floating point representation");

  if (IsNegative)
    RealVal.changeSign();

  Operands.push_back(RISCVOperand::createFPImm(
      RealVal.bitcastToAPInt().getZExtValue(), S));

  Lex(); // Eat the token.

  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseImmediate(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E;
  const MCExpr *Res;

  switch (getLexer().getKind()) {
  default:
    return ParseStatus::NoMatch;
  case AsmToken::LParen:
  case AsmToken::Dot:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Exclaim:
  case AsmToken::Tilde:
  case AsmToken::Integer:
  case AsmToken::String:
  case AsmToken::Identifier:
    if (getParser().parseExpression(Res, E))
      return ParseStatus::Failure;
    break;
  case AsmToken::Percent:
    return parseOperandWithModifier(Operands);
  }

  Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseOperandWithModifier(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E;

  if (parseToken(AsmToken::Percent, "expected '%' for operand modifier"))
    return ParseStatus::Failure;

  if (getLexer().getKind() != AsmToken::Identifier)
    return Error(getLoc(), "expected valid identifier for operand modifier");
  StringRef Identifier = getParser().getTok().getIdentifier();
  RISCVMCExpr::VariantKind VK = RISCVMCExpr::getVariantKindForName(Identifier);
  if (VK == RISCVMCExpr::VK_RISCV_Invalid)
    return Error(getLoc(), "unrecognized operand modifier");

  getParser().Lex(); // Eat the identifier
  if (parseToken(AsmToken::LParen, "expected '('"))
    return ParseStatus::Failure;

  const MCExpr *SubExpr;
  if (getParser().parseParenExpression(SubExpr, E))
    return ParseStatus::Failure;

  const MCExpr *ModExpr = RISCVMCExpr::create(SubExpr, VK, getContext());
  Operands.push_back(RISCVOperand::createImm(ModExpr, S, E, isRV64()));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseBareSymbol(OperandVector &Operands) {
  SMLoc S = getLoc();
  const MCExpr *Res;

  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::NoMatch;

  StringRef Identifier;
  AsmToken Tok = getLexer().getTok();

  if (getParser().parseIdentifier(Identifier))
    return ParseStatus::Failure;

  SMLoc E = SMLoc::getFromPointer(S.getPointer() + Identifier.size());

  if (Identifier.consume_back("@plt"))
    return Error(getLoc(), "'@plt' operand not valid for instruction");

  MCSymbol *Sym = getContext().getOrCreateSymbol(Identifier);

  if (Sym->isVariable()) {
    const MCExpr *V = Sym->getVariableValue(/*SetUsed=*/false);
    if (!isa<MCSymbolRefExpr>(V)) {
      getLexer().UnLex(Tok); // Put back if it's not a bare symbol.
      return ParseStatus::NoMatch;
    }
    Res = V;
  } else
    Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());

  MCBinaryExpr::Opcode Opcode;
  switch (getLexer().getKind()) {
  default:
    Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
    return ParseStatus::Success;
  case AsmToken::Plus:
    Opcode = MCBinaryExpr::Add;
    getLexer().Lex();
    break;
  case AsmToken::Minus:
    Opcode = MCBinaryExpr::Sub;
    getLexer().Lex();
    break;
  }

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr, E))
    return ParseStatus::Failure;
  Res = MCBinaryExpr::create(Opcode, Res, Expr, getContext());
  Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseCallSymbol(OperandVector &Operands) {
  SMLoc S = getLoc();
  const MCExpr *Res;

  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::NoMatch;

  // Avoid parsing the register in `call rd, foo` as a call symbol.
  if (getLexer().peekTok().getKind() != AsmToken::EndOfStatement)
    return ParseStatus::NoMatch;

  StringRef Identifier;
  if (getParser().parseIdentifier(Identifier))
    return ParseStatus::Failure;

  SMLoc E = SMLoc::getFromPointer(S.getPointer() + Identifier.size());

  RISCVMCExpr::VariantKind Kind = RISCVMCExpr::VK_RISCV_CALL_PLT;
  (void)Identifier.consume_back("@plt");

  MCSymbol *Sym = getContext().getOrCreateSymbol(Identifier);
  Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext());
  Res = RISCVMCExpr::create(Res, Kind, getContext());
  Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parsePseudoJumpSymbol(OperandVector &Operands) {
  SMLoc S = getLoc();
  SMLoc E;
  const MCExpr *Res;

  if (getParser().parseExpression(Res, E))
    return ParseStatus::Failure;

  if (Res->getKind() != MCExpr::ExprKind::SymbolRef ||
      cast<MCSymbolRefExpr>(Res)->getKind() ==
          MCSymbolRefExpr::VariantKind::VK_PLT)
    return Error(S, "operand must be a valid jump target");

  Res = RISCVMCExpr::create(Res, RISCVMCExpr::VK_RISCV_CALL, getContext());
  Operands.push_back(RISCVOperand::createImm(Res, S, E, isRV64()));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseJALOffset(OperandVector &Operands) {
  // Parsing jal operands is fiddly due to the `jal foo` and `jal ra, foo`
  // both being acceptable forms. When parsing `jal ra, foo` this function
  // will be called for the `ra` register operand in an attempt to match the
  // single-operand alias. parseJALOffset must fail for this case. It would
  // seem logical to try parse the operand using parseImmediate and return
  // NoMatch if the next token is a comma (meaning we must be parsing a jal in
  // the second form rather than the first). We can't do this as there's no
  // way of rewinding the lexer state. Instead, return NoMatch if this operand
  // is an identifier and is followed by a comma.
  if (getLexer().is(AsmToken::Identifier) &&
      getLexer().peekTok().is(AsmToken::Comma))
    return ParseStatus::NoMatch;

  return parseImmediate(Operands);
}

bool RISCVAsmParser::parseVTypeToken(const AsmToken &Tok, VTypeState &State,
                                     unsigned &Sew, unsigned &Lmul,
                                     bool &Fractional, bool &TailAgnostic,
                                     bool &MaskAgnostic) {
  if (Tok.isNot(AsmToken::Identifier))
    return true;

  StringRef Identifier = Tok.getIdentifier();

  switch (State) {
  case VTypeState_SEW:
    if (!Identifier.consume_front("e"))
      break;
    if (Identifier.getAsInteger(10, Sew))
      break;
    if (!RISCVVType::isValidSEW(Sew))
      break;
    State = VTypeState_LMUL;
    return false;
  case VTypeState_LMUL: {
    if (!Identifier.consume_front("m"))
      break;
    Fractional = Identifier.consume_front("f");
    if (Identifier.getAsInteger(10, Lmul))
      break;
    if (!RISCVVType::isValidLMUL(Lmul, Fractional))
      break;

    if (Fractional) {
      unsigned ELEN = STI->hasFeature(RISCV::FeatureStdExtZve64x) ? 64 : 32;
      unsigned MinLMUL = ELEN / 8;
      if (Lmul > MinLMUL)
        Warning(Tok.getLoc(),
                "use of vtype encodings with LMUL < SEWMIN/ELEN == mf" +
                    Twine(MinLMUL) + " is reserved");
    }

    State = VTypeState_TailPolicy;
    return false;
  }
  case VTypeState_TailPolicy:
    if (Identifier == "ta")
      TailAgnostic = true;
    else if (Identifier == "tu")
      TailAgnostic = false;
    else
      break;
    State = VTypeState_MaskPolicy;
    return false;
  case VTypeState_MaskPolicy:
    if (Identifier == "ma")
      MaskAgnostic = true;
    else if (Identifier == "mu")
      MaskAgnostic = false;
    else
      break;
    State = VTypeState_Done;
    return false;
  case VTypeState_Done:
    // Extra token?
    break;
  }

  return true;
}

ParseStatus RISCVAsmParser::parseVTypeI(OperandVector &Operands) {
  SMLoc S = getLoc();

  unsigned Sew = 0;
  unsigned Lmul = 0;
  bool Fractional = false;
  bool TailAgnostic = false;
  bool MaskAgnostic = false;

  VTypeState State = VTypeState_SEW;
  SMLoc SEWLoc = S;

  if (parseVTypeToken(getTok(), State, Sew, Lmul, Fractional, TailAgnostic,
                      MaskAgnostic))
    return ParseStatus::NoMatch;

  getLexer().Lex();

  while (parseOptionalToken(AsmToken::Comma)) {
    if (parseVTypeToken(getTok(), State, Sew, Lmul, Fractional, TailAgnostic,
                        MaskAgnostic))
      break;

    getLexer().Lex();
  }

  if (getLexer().is(AsmToken::EndOfStatement) && State == VTypeState_Done) {
    RISCVII::VLMUL VLMUL = RISCVVType::encodeLMUL(Lmul, Fractional);
    if (Fractional) {
      unsigned ELEN = STI->hasFeature(RISCV::FeatureStdExtZve64x) ? 64 : 32;
      unsigned MaxSEW = ELEN / Lmul;
      // If MaxSEW < 8, we should have printed warning about reserved LMUL.
      if (MaxSEW >= 8 && Sew > MaxSEW)
        Warning(SEWLoc,
                "use of vtype encodings with SEW > " + Twine(MaxSEW) +
                    " and LMUL == mf" + Twine(Lmul) +
                    " may not be compatible with all RVV implementations");
    }

    unsigned VTypeI =
        RISCVVType::encodeVTYPE(VLMUL, Sew, TailAgnostic, MaskAgnostic);
    Operands.push_back(RISCVOperand::createVType(VTypeI, S));
    return ParseStatus::Success;
  }

  return generateVTypeError(S);
}

bool RISCVAsmParser::generateVTypeError(SMLoc ErrorLoc) {
  return Error(
      ErrorLoc,
      "operand must be "
      "e[8|16|32|64],m[1|2|4|8|f2|f4|f8],[ta|tu],[ma|mu]");
}

ParseStatus RISCVAsmParser::parseMaskReg(OperandVector &Operands) {
  if (getLexer().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  StringRef Name = getLexer().getTok().getIdentifier();
  if (!Name.consume_back(".t"))
    return Error(getLoc(), "expected '.t' suffix");
  MCRegister RegNo = matchRegisterNameHelper(Name);

  if (!RegNo)
    return ParseStatus::NoMatch;
  if (RegNo != RISCV::V0)
    return ParseStatus::NoMatch;
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() + Name.size());
  getLexer().Lex();
  Operands.push_back(RISCVOperand::createReg(RegNo, S, E));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseGPRAsFPR(OperandVector &Operands) {
  if (getLexer().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  StringRef Name = getLexer().getTok().getIdentifier();
  MCRegister RegNo = matchRegisterNameHelper(Name);

  if (!RegNo)
    return ParseStatus::NoMatch;
  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() + Name.size());
  getLexer().Lex();
  Operands.push_back(RISCVOperand::createReg(
      RegNo, S, E, !getSTI().hasFeature(RISCV::FeatureStdExtF)));
  return ParseStatus::Success;
}

template <bool IsRV64>
ParseStatus RISCVAsmParser::parseGPRPair(OperandVector &Operands) {
  return parseGPRPair(Operands, IsRV64);
}

ParseStatus RISCVAsmParser::parseGPRPair(OperandVector &Operands,
                                         bool IsRV64Inst) {
  // If this is not an RV64 GPRPair instruction, don't parse as a GPRPair on
  // RV64 as it will prevent matching the RV64 version of the same instruction
  // that doesn't use a GPRPair.
  // If this is an RV64 GPRPair instruction, there is no RV32 version so we can
  // still parse as a pair.
  if (!IsRV64Inst && isRV64())
    return ParseStatus::NoMatch;

  if (getLexer().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  StringRef Name = getLexer().getTok().getIdentifier();
  MCRegister RegNo = matchRegisterNameHelper(Name);

  if (!RegNo)
    return ParseStatus::NoMatch;

  if (!RISCVMCRegisterClasses[RISCV::GPRRegClassID].contains(RegNo))
    return ParseStatus::NoMatch;

  if ((RegNo - RISCV::X0) & 1)
    return TokError("register must be even");

  SMLoc S = getLoc();
  SMLoc E = SMLoc::getFromPointer(S.getPointer() + Name.size());
  getLexer().Lex();

  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  unsigned Pair = RI->getMatchingSuperReg(
      RegNo, RISCV::sub_gpr_even,
      &RISCVMCRegisterClasses[RISCV::GPRPairRegClassID]);
  Operands.push_back(RISCVOperand::createReg(Pair, S, E));
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseFRMArg(OperandVector &Operands) {
  if (getLexer().isNot(AsmToken::Identifier))
    return TokError(
        "operand must be a valid floating point rounding mode mnemonic");

  StringRef Str = getLexer().getTok().getIdentifier();
  RISCVFPRndMode::RoundingMode FRM = RISCVFPRndMode::stringToRoundingMode(Str);

  if (FRM == RISCVFPRndMode::Invalid)
    return TokError(
        "operand must be a valid floating point rounding mode mnemonic");

  Operands.push_back(RISCVOperand::createFRMArg(FRM, getLoc()));
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseFenceArg(OperandVector &Operands) {
  const AsmToken &Tok = getLexer().getTok();

  if (Tok.is(AsmToken::Integer)) {
    if (Tok.getIntVal() != 0)
      goto ParseFail;

    Operands.push_back(RISCVOperand::createFenceArg(0, getLoc()));
    Lex();
    return ParseStatus::Success;
  }

  if (Tok.is(AsmToken::Identifier)) {
    StringRef Str = Tok.getIdentifier();

    // Letters must be unique, taken from 'iorw', and in ascending order. This
    // holds as long as each individual character is one of 'iorw' and is
    // greater than the previous character.
    unsigned Imm = 0;
    bool Valid = true;
    char Prev = '\0';
    for (char c : Str) {
      switch (c) {
      default:
        Valid = false;
        break;
      case 'i':
        Imm |= RISCVFenceField::I;
        break;
      case 'o':
        Imm |= RISCVFenceField::O;
        break;
      case 'r':
        Imm |= RISCVFenceField::R;
        break;
      case 'w':
        Imm |= RISCVFenceField::W;
        break;
      }

      if (c <= Prev) {
        Valid = false;
        break;
      }
      Prev = c;
    }

    if (!Valid)
      goto ParseFail;

    Operands.push_back(RISCVOperand::createFenceArg(Imm, getLoc()));
    Lex();
    return ParseStatus::Success;
  }

ParseFail:
  return TokError("operand must be formed of letters selected in-order from "
                  "'iorw' or be 0");
}

ParseStatus RISCVAsmParser::parseMemOpBaseReg(OperandVector &Operands) {
  if (parseToken(AsmToken::LParen, "expected '('"))
    return ParseStatus::Failure;
  Operands.push_back(RISCVOperand::createToken("(", getLoc()));

  if (!parseRegister(Operands).isSuccess())
    return Error(getLoc(), "expected register");

  if (parseToken(AsmToken::RParen, "expected ')'"))
    return ParseStatus::Failure;
  Operands.push_back(RISCVOperand::createToken(")", getLoc()));

  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseZeroOffsetMemOp(OperandVector &Operands) {
  // Atomic operations such as lr.w, sc.w, and amo*.w accept a "memory operand"
  // as one of their register operands, such as `(a0)`. This just denotes that
  // the register (in this case `a0`) contains a memory address.
  //
  // Normally, we would be able to parse these by putting the parens into the
  // instruction string. However, GNU as also accepts a zero-offset memory
  // operand (such as `0(a0)`), and ignores the 0. Normally this would be parsed
  // with parseImmediate followed by parseMemOpBaseReg, but these instructions
  // do not accept an immediate operand, and we do not want to add a "dummy"
  // operand that is silently dropped.
  //
  // Instead, we use this custom parser. This will: allow (and discard) an
  // offset if it is zero; require (and discard) parentheses; and add only the
  // parsed register operand to `Operands`.
  //
  // These operands are printed with RISCVInstPrinter::printZeroOffsetMemOp,
  // which will only print the register surrounded by parentheses (which GNU as
  // also uses as its canonical representation for these operands).
  std::unique_ptr<RISCVOperand> OptionalImmOp;

  if (getLexer().isNot(AsmToken::LParen)) {
    // Parse an Integer token. We do not accept arbritrary constant expressions
    // in the offset field (because they may include parens, which complicates
    // parsing a lot).
    int64_t ImmVal;
    SMLoc ImmStart = getLoc();
    if (getParser().parseIntToken(ImmVal,
                                  "expected '(' or optional integer offset"))
      return ParseStatus::Failure;

    // Create a RISCVOperand for checking later (so the error messages are
    // nicer), but we don't add it to Operands.
    SMLoc ImmEnd = getLoc();
    OptionalImmOp =
        RISCVOperand::createImm(MCConstantExpr::create(ImmVal, getContext()),
                                ImmStart, ImmEnd, isRV64());
  }

  if (parseToken(AsmToken::LParen,
                 OptionalImmOp ? "expected '(' after optional integer offset"
                               : "expected '(' or optional integer offset"))
    return ParseStatus::Failure;

  if (!parseRegister(Operands).isSuccess())
    return Error(getLoc(), "expected register");

  if (parseToken(AsmToken::RParen, "expected ')'"))
    return ParseStatus::Failure;

  // Deferred Handling of non-zero offsets. This makes the error messages nicer.
  if (OptionalImmOp && !OptionalImmOp->isImmZero())
    return Error(
        OptionalImmOp->getStartLoc(), "optional integer offset must be 0",
        SMRange(OptionalImmOp->getStartLoc(), OptionalImmOp->getEndLoc()));

  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseRegReg(OperandVector &Operands) {
  // RR : a2(a1)
  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::NoMatch;

  StringRef RegName = getLexer().getTok().getIdentifier();
  MCRegister Reg = matchRegisterNameHelper(RegName);
  if (!Reg)
    return Error(getLoc(), "invalid register");
  getLexer().Lex();

  if (parseToken(AsmToken::LParen, "expected '(' or invalid operand"))
    return ParseStatus::Failure;

  if (getLexer().getKind() != AsmToken::Identifier)
    return Error(getLoc(), "expected register");

  StringRef Reg2Name = getLexer().getTok().getIdentifier();
  MCRegister Reg2 = matchRegisterNameHelper(Reg2Name);
  if (!Reg2)
    return Error(getLoc(), "invalid register");
  getLexer().Lex();

  if (parseToken(AsmToken::RParen, "expected ')'"))
    return ParseStatus::Failure;

  Operands.push_back(RISCVOperand::createRegReg(Reg, Reg2, getLoc()));

  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseReglist(OperandVector &Operands) {
  // Rlist: {ra [, s0[-sN]]}
  // XRlist: {x1 [, x8[-x9][, x18[-xN]]]}
  SMLoc S = getLoc();

  if (parseToken(AsmToken::LCurly, "register list must start with '{'"))
    return ParseStatus::Failure;

  bool IsEABI = isRVE();

  if (getLexer().isNot(AsmToken::Identifier))
    return Error(getLoc(), "register list must start from 'ra' or 'x1'");

  StringRef RegName = getLexer().getTok().getIdentifier();
  MCRegister RegStart = matchRegisterNameHelper(RegName);
  MCRegister RegEnd;
  if (RegStart != RISCV::X1)
    return Error(getLoc(), "register list must start from 'ra' or 'x1'");
  getLexer().Lex();

  // parse case like ,s0
  if (parseOptionalToken(AsmToken::Comma)) {
    if (getLexer().isNot(AsmToken::Identifier))
      return Error(getLoc(), "invalid register");
    StringRef RegName = getLexer().getTok().getIdentifier();
    RegStart = matchRegisterNameHelper(RegName);
    if (!RegStart)
      return Error(getLoc(), "invalid register");
    if (RegStart != RISCV::X8)
      return Error(getLoc(),
                   "continuous register list must start from 's0' or 'x8'");
    getLexer().Lex(); // eat reg
  }

  // parse case like -s1
  if (parseOptionalToken(AsmToken::Minus)) {
    StringRef EndName = getLexer().getTok().getIdentifier();
    // FIXME: the register mapping and checks of EABI is wrong
    RegEnd = matchRegisterNameHelper(EndName);
    if (!RegEnd)
      return Error(getLoc(), "invalid register");
    if (IsEABI && RegEnd != RISCV::X9)
      return Error(getLoc(), "contiguous register list of EABI can only be "
                             "'s0-s1' or 'x8-x9' pair");
    getLexer().Lex();
  }

  if (!IsEABI) {
    // parse extra part like ', x18[-x20]' for XRegList
    if (parseOptionalToken(AsmToken::Comma)) {
      if (RegEnd != RISCV::X9)
        return Error(
            getLoc(),
            "first contiguous registers pair of register list must be 'x8-x9'");

      // parse ', x18' for extra part
      if (getLexer().isNot(AsmToken::Identifier))
        return Error(getLoc(), "invalid register");
      StringRef EndName = getLexer().getTok().getIdentifier();
      if (MatchRegisterName(EndName) != RISCV::X18)
        return Error(getLoc(),
                     "second contiguous registers pair of register list "
                     "must start from 'x18'");
      getLexer().Lex();

      // parse '-x20' for extra part
      if (parseOptionalToken(AsmToken::Minus)) {
        if (getLexer().isNot(AsmToken::Identifier))
          return Error(getLoc(), "invalid register");
        EndName = getLexer().getTok().getIdentifier();
        if (MatchRegisterName(EndName) == RISCV::NoRegister)
          return Error(getLoc(), "invalid register");
        getLexer().Lex();
      }
      RegEnd = MatchRegisterName(EndName);
    }
  }

  if (RegEnd == RISCV::X26)
    return Error(getLoc(), "invalid register list, {ra, s0-s10} or {x1, x8-x9, "
                           "x18-x26} is not supported");

  if (parseToken(AsmToken::RCurly, "register list must end with '}'"))
    return ParseStatus::Failure;

  if (RegEnd == RISCV::NoRegister)
    RegEnd = RegStart;

  auto Encode = RISCVZC::encodeRlist(RegEnd, IsEABI);
  if (Encode == RISCVZC::INVALID_RLIST)
    return Error(S, "invalid register list");
  Operands.push_back(RISCVOperand::createRlist(Encode, S));

  return ParseStatus::Success;
}

ParseStatus RISCVAsmParser::parseZcmpStackAdj(OperandVector &Operands,
                                              bool ExpectNegative) {
  bool Negative = parseOptionalToken(AsmToken::Minus);

  SMLoc S = getLoc();
  int64_t StackAdjustment = getLexer().getTok().getIntVal();
  unsigned Spimm = 0;
  unsigned RlistVal = static_cast<RISCVOperand *>(Operands[1].get())->Rlist.Val;

  if (Negative != ExpectNegative ||
      !RISCVZC::getSpimm(RlistVal, Spimm, StackAdjustment, isRV64()))
    return ParseStatus::NoMatch;
  Operands.push_back(RISCVOperand::createSpimm(Spimm << 4, S));
  getLexer().Lex();
  return ParseStatus::Success;
}

/// Looks at a token type and creates the relevant operand from this
/// information, adding to Operands. If operand was parsed, returns false, else
/// true.
bool RISCVAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  ParseStatus Result =
      MatchOperandParserImpl(Operands, Mnemonic, /*ParseForAllFeatures=*/true);
  if (Result.isSuccess())
    return false;
  if (Result.isFailure())
    return true;

  // Attempt to parse token as a register.
  if (parseRegister(Operands, true).isSuccess())
    return false;

  // Attempt to parse token as an immediate
  if (parseImmediate(Operands).isSuccess()) {
    // Parse memory base register if present
    if (getLexer().is(AsmToken::LParen))
      return !parseMemOpBaseReg(Operands).isSuccess();
    return false;
  }

  // Finally we have exhausted all options and must declare defeat.
  Error(getLoc(), "unknown operand");
  return true;
}

bool RISCVAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  // Ensure that if the instruction occurs when relaxation is enabled,
  // relocations are forced for the file. Ideally this would be done when there
  // is enough information to reliably determine if the instruction itself may
  // cause relaxations. Unfortunately instruction processing stage occurs in the
  // same pass as relocation emission, so it's too late to set a 'sticky bit'
  // for the entire file.
  if (getSTI().hasFeature(RISCV::FeatureRelax)) {
    auto *Assembler = getTargetStreamer().getStreamer().getAssemblerPtr();
    if (Assembler != nullptr) {
      RISCVAsmBackend &MAB =
          static_cast<RISCVAsmBackend &>(Assembler->getBackend());
      MAB.setForceRelocs();
    }
  }

  // First operand is token for instruction
  Operands.push_back(RISCVOperand::createToken(Name, NameLoc));

  // If there are no more operands, then finish
  if (getLexer().is(AsmToken::EndOfStatement)) {
    getParser().Lex(); // Consume the EndOfStatement.
    return false;
  }

  // Parse first operand
  if (parseOperand(Operands, Name))
    return true;

  // Parse until end of statement, consuming commas between operands
  while (parseOptionalToken(AsmToken::Comma)) {
    // Parse next operand
    if (parseOperand(Operands, Name))
      return true;
  }

  if (getParser().parseEOL("unexpected token")) {
    getParser().eatToEndOfStatement();
    return true;
  }
  return false;
}

bool RISCVAsmParser::classifySymbolRef(const MCExpr *Expr,
                                       RISCVMCExpr::VariantKind &Kind) {
  Kind = RISCVMCExpr::VK_RISCV_None;

  if (const RISCVMCExpr *RE = dyn_cast<RISCVMCExpr>(Expr)) {
    Kind = RE->getKind();
    Expr = RE->getSubExpr();
  }

  MCValue Res;
  MCFixup Fixup;
  if (Expr->evaluateAsRelocatable(Res, nullptr, &Fixup))
    return Res.getRefKind() == RISCVMCExpr::VK_RISCV_None;
  return false;
}

bool RISCVAsmParser::isSymbolDiff(const MCExpr *Expr) {
  MCValue Res;
  MCFixup Fixup;
  if (Expr->evaluateAsRelocatable(Res, nullptr, &Fixup)) {
    return Res.getRefKind() == RISCVMCExpr::VK_RISCV_None && Res.getSymA() &&
           Res.getSymB();
  }
  return false;
}

ParseStatus RISCVAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getString();

  if (IDVal == ".option")
    return parseDirectiveOption();
  if (IDVal == ".attribute")
    return parseDirectiveAttribute();
  if (IDVal == ".insn")
    return parseDirectiveInsn(DirectiveID.getLoc());
  if (IDVal == ".variant_cc")
    return parseDirectiveVariantCC();

  return ParseStatus::NoMatch;
}

bool RISCVAsmParser::resetToArch(StringRef Arch, SMLoc Loc, std::string &Result,
                                 bool FromOptionDirective) {
  for (auto &Feature : RISCVFeatureKV)
    if (llvm::RISCVISAInfo::isSupportedExtensionFeature(Feature.Key))
      clearFeatureBits(Feature.Value, Feature.Key);

  auto ParseResult = llvm::RISCVISAInfo::parseArchString(
      Arch, /*EnableExperimentalExtension=*/true,
      /*ExperimentalExtensionVersionCheck=*/true);
  if (!ParseResult) {
    std::string Buffer;
    raw_string_ostream OutputErrMsg(Buffer);
    handleAllErrors(ParseResult.takeError(), [&](llvm::StringError &ErrMsg) {
      OutputErrMsg << "invalid arch name '" << Arch << "', "
                   << ErrMsg.getMessage();
    });

    return Error(Loc, OutputErrMsg.str());
  }
  auto &ISAInfo = *ParseResult;

  for (auto &Feature : RISCVFeatureKV)
    if (ISAInfo->hasExtension(Feature.Key))
      setFeatureBits(Feature.Value, Feature.Key);

  if (FromOptionDirective) {
    if (ISAInfo->getXLen() == 32 && isRV64())
      return Error(Loc, "bad arch string switching from rv64 to rv32");
    else if (ISAInfo->getXLen() == 64 && !isRV64())
      return Error(Loc, "bad arch string switching from rv32 to rv64");
  }

  if (ISAInfo->getXLen() == 32)
    clearFeatureBits(RISCV::Feature64Bit, "64bit");
  else if (ISAInfo->getXLen() == 64)
    setFeatureBits(RISCV::Feature64Bit, "64bit");
  else
    return Error(Loc, "bad arch string " + Arch);

  Result = ISAInfo->toString();
  return false;
}

bool RISCVAsmParser::parseDirectiveOption() {
  MCAsmParser &Parser = getParser();
  // Get the option token.
  AsmToken Tok = Parser.getTok();

  // At the moment only identifiers are supported.
  if (parseToken(AsmToken::Identifier, "expected identifier"))
    return true;

  StringRef Option = Tok.getIdentifier();

  if (Option == "push") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionPush();
    pushFeatureBits();
    return false;
  }

  if (Option == "pop") {
    SMLoc StartLoc = Parser.getTok().getLoc();
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionPop();
    if (popFeatureBits())
      return Error(StartLoc, ".option pop with no .option push");

    return false;
  }

  if (Option == "arch") {
    SmallVector<RISCVOptionArchArg> Args;
    do {
      if (Parser.parseComma())
        return true;

      RISCVOptionArchArgType Type;
      if (parseOptionalToken(AsmToken::Plus))
        Type = RISCVOptionArchArgType::Plus;
      else if (parseOptionalToken(AsmToken::Minus))
        Type = RISCVOptionArchArgType::Minus;
      else if (!Args.empty())
        return Error(Parser.getTok().getLoc(),
                     "unexpected token, expected + or -");
      else
        Type = RISCVOptionArchArgType::Full;

      if (Parser.getTok().isNot(AsmToken::Identifier))
        return Error(Parser.getTok().getLoc(),
                     "unexpected token, expected identifier");

      StringRef Arch = Parser.getTok().getString();
      SMLoc Loc = Parser.getTok().getLoc();
      Parser.Lex();

      if (Type == RISCVOptionArchArgType::Full) {
        std::string Result;
        if (resetToArch(Arch, Loc, Result, true))
          return true;

        Args.emplace_back(Type, Result);
        break;
      }

      if (isDigit(Arch.back()))
        return Error(
            Loc, "extension version number parsing not currently implemented");

      std::string Feature = RISCVISAInfo::getTargetFeatureForExtension(Arch);
      if (!enableExperimentalExtension() &&
          StringRef(Feature).starts_with("experimental-"))
        return Error(Loc, "unexpected experimental extensions");
      auto Ext = llvm::lower_bound(RISCVFeatureKV, Feature);
      if (Ext == std::end(RISCVFeatureKV) || StringRef(Ext->Key) != Feature)
        return Error(Loc, "unknown extension feature");

      Args.emplace_back(Type, Arch.str());

      if (Type == RISCVOptionArchArgType::Plus) {
        FeatureBitset OldFeatureBits = STI->getFeatureBits();

        setFeatureBits(Ext->Value, Ext->Key);
        auto ParseResult = RISCVFeatures::parseFeatureBits(isRV64(), STI->getFeatureBits());
        if (!ParseResult) {
          copySTI().setFeatureBits(OldFeatureBits);
          setAvailableFeatures(ComputeAvailableFeatures(OldFeatureBits));

          std::string Buffer;
          raw_string_ostream OutputErrMsg(Buffer);
          handleAllErrors(ParseResult.takeError(), [&](llvm::StringError &ErrMsg) {
            OutputErrMsg << ErrMsg.getMessage();
          });

          return Error(Loc, OutputErrMsg.str());
        }
      } else {
        assert(Type == RISCVOptionArchArgType::Minus);
        // It is invalid to disable an extension that there are other enabled
        // extensions depend on it.
        // TODO: Make use of RISCVISAInfo to handle this
        for (auto &Feature : RISCVFeatureKV) {
          if (getSTI().hasFeature(Feature.Value) &&
              Feature.Implies.test(Ext->Value))
            return Error(Loc, Twine("can't disable ") + Ext->Key +
                                  " extension; " + Feature.Key +
                                  " extension requires " + Ext->Key +
                                  " extension");
        }

        clearFeatureBits(Ext->Value, Ext->Key);
      }
    } while (Parser.getTok().isNot(AsmToken::EndOfStatement));

    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionArch(Args);
    return false;
  }

  if (Option == "rvc") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionRVC();
    setFeatureBits(RISCV::FeatureStdExtC, "c");
    return false;
  }

  if (Option == "norvc") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionNoRVC();
    clearFeatureBits(RISCV::FeatureStdExtC, "c");
    clearFeatureBits(RISCV::FeatureStdExtZca, "zca");
    return false;
  }

  if (Option == "pic") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionPIC();
    ParserOptions.IsPicEnabled = true;
    return false;
  }

  if (Option == "nopic") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionNoPIC();
    ParserOptions.IsPicEnabled = false;
    return false;
  }

  if (Option == "relax") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionRelax();
    setFeatureBits(RISCV::FeatureRelax, "relax");
    return false;
  }

  if (Option == "norelax") {
    if (Parser.parseEOL())
      return true;

    getTargetStreamer().emitDirectiveOptionNoRelax();
    clearFeatureBits(RISCV::FeatureRelax, "relax");
    return false;
  }

  // Unknown option.
  Warning(Parser.getTok().getLoc(), "unknown option, expected 'push', 'pop', "
                                    "'rvc', 'norvc', 'arch', 'relax' or "
                                    "'norelax'");
  Parser.eatToEndOfStatement();
  return false;
}

/// parseDirectiveAttribute
///  ::= .attribute expression ',' ( expression | "string" )
///  ::= .attribute identifier ',' ( expression | "string" )
bool RISCVAsmParser::parseDirectiveAttribute() {
  MCAsmParser &Parser = getParser();
  int64_t Tag;
  SMLoc TagLoc;
  TagLoc = Parser.getTok().getLoc();
  if (Parser.getTok().is(AsmToken::Identifier)) {
    StringRef Name = Parser.getTok().getIdentifier();
    std::optional<unsigned> Ret =
        ELFAttrs::attrTypeFromString(Name, RISCVAttrs::getRISCVAttributeTags());
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
    if (check(!CE, TagLoc, "expected numeric constant"))
      return true;

    Tag = CE->getValue();
  }

  if (Parser.parseComma())
    return true;

  StringRef StringValue;
  int64_t IntegerValue = 0;
  bool IsIntegerValue = true;

  // RISC-V attributes have a string value if the tag number is odd
  // and an integer value if the tag number is even.
  if (Tag % 2)
    IsIntegerValue = false;

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
  else if (Tag != RISCVAttrs::ARCH)
    getTargetStreamer().emitTextAttribute(Tag, StringValue);
  else {
    std::string Result;
    if (resetToArch(StringValue, ValueExprLoc, Result, false))
      return true;

    // Then emit the arch string.
    getTargetStreamer().emitTextAttribute(Tag, Result);
  }

  return false;
}

bool isValidInsnFormat(StringRef Format, bool AllowC) {
  return StringSwitch<bool>(Format)
      .Cases("r", "r4", "i", "b", "sb", "u", "j", "uj", "s", true)
      .Cases("cr", "ci", "ciw", "css", "cl", "cs", "ca", "cb", "cj", AllowC)
      .Default(false);
}

/// parseDirectiveInsn
/// ::= .insn [ format encoding, (operands (, operands)*) ]
/// ::= .insn [ length, value ]
/// ::= .insn [ value ]
bool RISCVAsmParser::parseDirectiveInsn(SMLoc L) {
  MCAsmParser &Parser = getParser();

  bool AllowC = getSTI().hasFeature(RISCV::FeatureStdExtC) ||
                getSTI().hasFeature(RISCV::FeatureStdExtZca);

  // Expect instruction format as identifier.
  StringRef Format;
  SMLoc ErrorLoc = Parser.getTok().getLoc();
  if (Parser.parseIdentifier(Format)) {
    // Try parsing .insn [length], value
    int64_t Length = 0;
    int64_t Value = 0;
    if (Parser.parseIntToken(
            Value, "expected instruction format or an integer constant"))
      return true;
    if (Parser.parseOptionalToken(AsmToken::Comma)) {
      Length = Value;
      if (Parser.parseIntToken(Value, "expected an integer constant"))
        return true;
    }

    // TODO: Add support for long instructions
    int64_t RealLength = (Value & 3) == 3 ? 4 : 2;
    if (!isUIntN(RealLength * 8, Value))
      return Error(ErrorLoc, "invalid operand for instruction");
    if (RealLength == 2 && !AllowC)
      return Error(ErrorLoc, "compressed instructions are not allowed");
    if (Length != 0 && Length != RealLength)
      return Error(ErrorLoc, "instruction length mismatch");

    if (getParser().parseEOL("invalid operand for instruction")) {
      getParser().eatToEndOfStatement();
      return true;
    }

    emitToStreamer(getStreamer(), MCInstBuilder(RealLength == 2 ? RISCV::Insn16
                                                                : RISCV::Insn32)
                                      .addImm(Value));
    return false;
  }

  if (!isValidInsnFormat(Format, AllowC))
    return Error(ErrorLoc, "invalid instruction format");

  std::string FormatName = (".insn_" + Format).str();

  ParseInstructionInfo Info;
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> Operands;

  if (ParseInstruction(Info, FormatName, L, Operands))
    return true;

  unsigned Opcode;
  uint64_t ErrorInfo;
  return MatchAndEmitInstruction(L, Opcode, Operands, Parser.getStreamer(),
                                 ErrorInfo,
                                 /*MatchingInlineAsm=*/false);
}

/// parseDirectiveVariantCC
///  ::= .variant_cc symbol
bool RISCVAsmParser::parseDirectiveVariantCC() {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected symbol name");
  if (parseEOL())
    return true;
  getTargetStreamer().emitDirectiveVariantCC(
      *getContext().getOrCreateSymbol(Name));
  return false;
}

void RISCVAsmParser::emitToStreamer(MCStreamer &S, const MCInst &Inst) {
  MCInst CInst;
  bool Res = RISCVRVC::compress(CInst, Inst, getSTI());
  if (Res)
    ++RISCVNumInstrsCompressed;
  S.emitInstruction((Res ? CInst : Inst), getSTI());
}

void RISCVAsmParser::emitLoadImm(MCRegister DestReg, int64_t Value,
                                 MCStreamer &Out) {
  SmallVector<MCInst, 8> Seq;
  RISCVMatInt::generateMCInstSeq(Value, getSTI(), DestReg, Seq);

  for (MCInst &Inst : Seq) {
    emitToStreamer(Out, Inst);
  }
}

void RISCVAsmParser::emitAuipcInstPair(MCOperand DestReg, MCOperand TmpReg,
                                       const MCExpr *Symbol,
                                       RISCVMCExpr::VariantKind VKHi,
                                       unsigned SecondOpcode, SMLoc IDLoc,
                                       MCStreamer &Out) {
  // A pair of instructions for PC-relative addressing; expands to
  //   TmpLabel: AUIPC TmpReg, VKHi(symbol)
  //             OP DestReg, TmpReg, %pcrel_lo(TmpLabel)
  MCContext &Ctx = getContext();

  MCSymbol *TmpLabel = Ctx.createNamedTempSymbol("pcrel_hi");
  Out.emitLabel(TmpLabel);

  const RISCVMCExpr *SymbolHi = RISCVMCExpr::create(Symbol, VKHi, Ctx);
  emitToStreamer(
      Out, MCInstBuilder(RISCV::AUIPC).addOperand(TmpReg).addExpr(SymbolHi));

  const MCExpr *RefToLinkTmpLabel =
      RISCVMCExpr::create(MCSymbolRefExpr::create(TmpLabel, Ctx),
                          RISCVMCExpr::VK_RISCV_PCREL_LO, Ctx);

  emitToStreamer(Out, MCInstBuilder(SecondOpcode)
                          .addOperand(DestReg)
                          .addOperand(TmpReg)
                          .addExpr(RefToLinkTmpLabel));
}

void RISCVAsmParser::emitLoadLocalAddress(MCInst &Inst, SMLoc IDLoc,
                                          MCStreamer &Out) {
  // The load local address pseudo-instruction "lla" is used in PC-relative
  // addressing of local symbols:
  //   lla rdest, symbol
  // expands to
  //   TmpLabel: AUIPC rdest, %pcrel_hi(symbol)
  //             ADDI rdest, rdest, %pcrel_lo(TmpLabel)
  MCOperand DestReg = Inst.getOperand(0);
  const MCExpr *Symbol = Inst.getOperand(1).getExpr();
  emitAuipcInstPair(DestReg, DestReg, Symbol, RISCVMCExpr::VK_RISCV_PCREL_HI,
                    RISCV::ADDI, IDLoc, Out);
}

void RISCVAsmParser::emitLoadGlobalAddress(MCInst &Inst, SMLoc IDLoc,
                                           MCStreamer &Out) {
  // The load global address pseudo-instruction "lga" is used in GOT-indirect
  // addressing of global symbols:
  //   lga rdest, symbol
  // expands to
  //   TmpLabel: AUIPC rdest, %got_pcrel_hi(symbol)
  //             Lx rdest, %pcrel_lo(TmpLabel)(rdest)
  MCOperand DestReg = Inst.getOperand(0);
  const MCExpr *Symbol = Inst.getOperand(1).getExpr();
  unsigned SecondOpcode = isRV64() ? RISCV::LD : RISCV::LW;
  emitAuipcInstPair(DestReg, DestReg, Symbol, RISCVMCExpr::VK_RISCV_GOT_HI,
                    SecondOpcode, IDLoc, Out);
}

void RISCVAsmParser::emitLoadAddress(MCInst &Inst, SMLoc IDLoc,
                                     MCStreamer &Out) {
  // The load address pseudo-instruction "la" is used in PC-relative and
  // GOT-indirect addressing of global symbols:
  //   la rdest, symbol
  // is an alias for either (for non-PIC)
  //   lla rdest, symbol
  // or (for PIC)
  //   lga rdest, symbol
  if (ParserOptions.IsPicEnabled)
    emitLoadGlobalAddress(Inst, IDLoc, Out);
  else
    emitLoadLocalAddress(Inst, IDLoc, Out);
}

void RISCVAsmParser::emitLoadTLSIEAddress(MCInst &Inst, SMLoc IDLoc,
                                          MCStreamer &Out) {
  // The load TLS IE address pseudo-instruction "la.tls.ie" is used in
  // initial-exec TLS model addressing of global symbols:
  //   la.tls.ie rdest, symbol
  // expands to
  //   TmpLabel: AUIPC rdest, %tls_ie_pcrel_hi(symbol)
  //             Lx rdest, %pcrel_lo(TmpLabel)(rdest)
  MCOperand DestReg = Inst.getOperand(0);
  const MCExpr *Symbol = Inst.getOperand(1).getExpr();
  unsigned SecondOpcode = isRV64() ? RISCV::LD : RISCV::LW;
  emitAuipcInstPair(DestReg, DestReg, Symbol, RISCVMCExpr::VK_RISCV_TLS_GOT_HI,
                    SecondOpcode, IDLoc, Out);
}

void RISCVAsmParser::emitLoadTLSGDAddress(MCInst &Inst, SMLoc IDLoc,
                                          MCStreamer &Out) {
  // The load TLS GD address pseudo-instruction "la.tls.gd" is used in
  // global-dynamic TLS model addressing of global symbols:
  //   la.tls.gd rdest, symbol
  // expands to
  //   TmpLabel: AUIPC rdest, %tls_gd_pcrel_hi(symbol)
  //             ADDI rdest, rdest, %pcrel_lo(TmpLabel)
  MCOperand DestReg = Inst.getOperand(0);
  const MCExpr *Symbol = Inst.getOperand(1).getExpr();
  emitAuipcInstPair(DestReg, DestReg, Symbol, RISCVMCExpr::VK_RISCV_TLS_GD_HI,
                    RISCV::ADDI, IDLoc, Out);
}

void RISCVAsmParser::emitLoadStoreSymbol(MCInst &Inst, unsigned Opcode,
                                         SMLoc IDLoc, MCStreamer &Out,
                                         bool HasTmpReg) {
  // The load/store pseudo-instruction does a pc-relative load with
  // a symbol.
  //
  // The expansion looks like this
  //
  //   TmpLabel: AUIPC tmp, %pcrel_hi(symbol)
  //             [S|L]X    rd, %pcrel_lo(TmpLabel)(tmp)
  unsigned DestRegOpIdx = HasTmpReg ? 1 : 0;
  MCOperand DestReg = Inst.getOperand(DestRegOpIdx);
  unsigned SymbolOpIdx = HasTmpReg ? 2 : 1;
  MCOperand TmpReg = Inst.getOperand(0);
  const MCExpr *Symbol = Inst.getOperand(SymbolOpIdx).getExpr();
  emitAuipcInstPair(DestReg, TmpReg, Symbol, RISCVMCExpr::VK_RISCV_PCREL_HI,
                    Opcode, IDLoc, Out);
}

void RISCVAsmParser::emitPseudoExtend(MCInst &Inst, bool SignExtend,
                                      int64_t Width, SMLoc IDLoc,
                                      MCStreamer &Out) {
  // The sign/zero extend pseudo-instruction does two shifts, with the shift
  // amounts dependent on the XLEN.
  //
  // The expansion looks like this
  //
  //    SLLI rd, rs, XLEN - Width
  //    SR[A|R]I rd, rd, XLEN - Width
  MCOperand DestReg = Inst.getOperand(0);
  MCOperand SourceReg = Inst.getOperand(1);

  unsigned SecondOpcode = SignExtend ? RISCV::SRAI : RISCV::SRLI;
  int64_t ShAmt = (isRV64() ? 64 : 32) - Width;

  assert(ShAmt > 0 && "Shift amount must be non-zero.");

  emitToStreamer(Out, MCInstBuilder(RISCV::SLLI)
                          .addOperand(DestReg)
                          .addOperand(SourceReg)
                          .addImm(ShAmt));

  emitToStreamer(Out, MCInstBuilder(SecondOpcode)
                          .addOperand(DestReg)
                          .addOperand(DestReg)
                          .addImm(ShAmt));
}

void RISCVAsmParser::emitVMSGE(MCInst &Inst, unsigned Opcode, SMLoc IDLoc,
                               MCStreamer &Out) {
  if (Inst.getNumOperands() == 3) {
    // unmasked va >= x
    //
    //  pseudoinstruction: vmsge{u}.vx vd, va, x
    //  expansion: vmslt{u}.vx vd, va, x; vmnand.mm vd, vd, vd
    emitToStreamer(Out, MCInstBuilder(Opcode)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(1))
                            .addOperand(Inst.getOperand(2))
                            .addReg(RISCV::NoRegister)
                            .setLoc(IDLoc));
    emitToStreamer(Out, MCInstBuilder(RISCV::VMNAND_MM)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(0))
                            .setLoc(IDLoc));
  } else if (Inst.getNumOperands() == 4) {
    // masked va >= x, vd != v0
    //
    //  pseudoinstruction: vmsge{u}.vx vd, va, x, v0.t
    //  expansion: vmslt{u}.vx vd, va, x, v0.t; vmxor.mm vd, vd, v0
    assert(Inst.getOperand(0).getReg() != RISCV::V0 &&
           "The destination register should not be V0.");
    emitToStreamer(Out, MCInstBuilder(Opcode)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(1))
                            .addOperand(Inst.getOperand(2))
                            .addOperand(Inst.getOperand(3))
                            .setLoc(IDLoc));
    emitToStreamer(Out, MCInstBuilder(RISCV::VMXOR_MM)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(0))
                            .addReg(RISCV::V0)
                            .setLoc(IDLoc));
  } else if (Inst.getNumOperands() == 5 &&
             Inst.getOperand(0).getReg() == RISCV::V0) {
    // masked va >= x, vd == v0
    //
    //  pseudoinstruction: vmsge{u}.vx vd, va, x, v0.t, vt
    //  expansion: vmslt{u}.vx vt, va, x;  vmandn.mm vd, vd, vt
    assert(Inst.getOperand(0).getReg() == RISCV::V0 &&
           "The destination register should be V0.");
    assert(Inst.getOperand(1).getReg() != RISCV::V0 &&
           "The temporary vector register should not be V0.");
    emitToStreamer(Out, MCInstBuilder(Opcode)
                            .addOperand(Inst.getOperand(1))
                            .addOperand(Inst.getOperand(2))
                            .addOperand(Inst.getOperand(3))
                            .addReg(RISCV::NoRegister)
                            .setLoc(IDLoc));
    emitToStreamer(Out, MCInstBuilder(RISCV::VMANDN_MM)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(1))
                            .setLoc(IDLoc));
  } else if (Inst.getNumOperands() == 5) {
    // masked va >= x, any vd
    //
    // pseudoinstruction: vmsge{u}.vx vd, va, x, v0.t, vt
    // expansion: vmslt{u}.vx vt, va, x; vmandn.mm vt, v0, vt;
    //            vmandn.mm vd, vd, v0;  vmor.mm vd, vt, vd
    assert(Inst.getOperand(1).getReg() != RISCV::V0 &&
           "The temporary vector register should not be V0.");
    emitToStreamer(Out, MCInstBuilder(Opcode)
                            .addOperand(Inst.getOperand(1))
                            .addOperand(Inst.getOperand(2))
                            .addOperand(Inst.getOperand(3))
                            .addReg(RISCV::NoRegister)
                            .setLoc(IDLoc));
    emitToStreamer(Out, MCInstBuilder(RISCV::VMANDN_MM)
                            .addOperand(Inst.getOperand(1))
                            .addReg(RISCV::V0)
                            .addOperand(Inst.getOperand(1))
                            .setLoc(IDLoc));
    emitToStreamer(Out, MCInstBuilder(RISCV::VMANDN_MM)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(0))
                            .addReg(RISCV::V0)
                            .setLoc(IDLoc));
    emitToStreamer(Out, MCInstBuilder(RISCV::VMOR_MM)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(1))
                            .addOperand(Inst.getOperand(0))
                            .setLoc(IDLoc));
  }
}

bool RISCVAsmParser::checkPseudoAddTPRel(MCInst &Inst,
                                         OperandVector &Operands) {
  assert(Inst.getOpcode() == RISCV::PseudoAddTPRel && "Invalid instruction");
  assert(Inst.getOperand(2).isReg() && "Unexpected second operand kind");
  if (Inst.getOperand(2).getReg() != RISCV::X4) {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[3]).getStartLoc();
    return Error(ErrorLoc, "the second input operand must be tp/x4 when using "
                           "%tprel_add modifier");
  }

  return false;
}

bool RISCVAsmParser::checkPseudoTLSDESCCall(MCInst &Inst,
                                            OperandVector &Operands) {
  assert(Inst.getOpcode() == RISCV::PseudoTLSDESCCall && "Invalid instruction");
  assert(Inst.getOperand(0).isReg() && "Unexpected operand kind");
  if (Inst.getOperand(0).getReg() != RISCV::X5) {
    SMLoc ErrorLoc = ((RISCVOperand &)*Operands[3]).getStartLoc();
    return Error(ErrorLoc, "the output operand must be t0/x5 when using "
                           "%tlsdesc_call modifier");
  }

  return false;
}

std::unique_ptr<RISCVOperand> RISCVAsmParser::defaultMaskRegOp() const {
  return RISCVOperand::createReg(RISCV::NoRegister, llvm::SMLoc(),
                                 llvm::SMLoc());
}

std::unique_ptr<RISCVOperand> RISCVAsmParser::defaultFRMArgOp() const {
  return RISCVOperand::createFRMArg(RISCVFPRndMode::RoundingMode::DYN,
                                    llvm::SMLoc());
}

std::unique_ptr<RISCVOperand> RISCVAsmParser::defaultFRMArgLegacyOp() const {
  return RISCVOperand::createFRMArg(RISCVFPRndMode::RoundingMode::RNE,
                                    llvm::SMLoc());
}

bool RISCVAsmParser::validateInstruction(MCInst &Inst,
                                         OperandVector &Operands) {
  unsigned Opcode = Inst.getOpcode();

  if (Opcode == RISCV::PseudoVMSGEU_VX_M_T ||
      Opcode == RISCV::PseudoVMSGE_VX_M_T) {
    unsigned DestReg = Inst.getOperand(0).getReg();
    unsigned TempReg = Inst.getOperand(1).getReg();
    if (DestReg == TempReg) {
      SMLoc Loc = Operands.back()->getStartLoc();
      return Error(Loc, "the temporary vector register cannot be the same as "
                        "the destination register");
    }
  }

  if (Opcode == RISCV::TH_LDD || Opcode == RISCV::TH_LWUD ||
      Opcode == RISCV::TH_LWD) {
    unsigned Rd1 = Inst.getOperand(0).getReg();
    unsigned Rd2 = Inst.getOperand(1).getReg();
    unsigned Rs1 = Inst.getOperand(2).getReg();
    // The encoding with rd1 == rd2 == rs1 is reserved for XTHead load pair.
    if (Rs1 == Rd1 && Rs1 == Rd2) {
      SMLoc Loc = Operands[1]->getStartLoc();
      return Error(Loc, "rs1, rd1, and rd2 cannot all be the same");
    }
  }

  if (Opcode == RISCV::CM_MVSA01) {
    unsigned Rd1 = Inst.getOperand(0).getReg();
    unsigned Rd2 = Inst.getOperand(1).getReg();
    if (Rd1 == Rd2) {
      SMLoc Loc = Operands[1]->getStartLoc();
      return Error(Loc, "rs1 and rs2 must be different");
    }
  }

  bool IsTHeadMemPair32 = (Opcode == RISCV::TH_LWD ||
                           Opcode == RISCV::TH_LWUD || Opcode == RISCV::TH_SWD);
  bool IsTHeadMemPair64 = (Opcode == RISCV::TH_LDD || Opcode == RISCV::TH_SDD);
  // The last operand of XTHeadMemPair instructions must be constant 3 or 4
  // depending on the data width.
  if (IsTHeadMemPair32 && Inst.getOperand(4).getImm() != 3) {
    SMLoc Loc = Operands.back()->getStartLoc();
    return Error(Loc, "operand must be constant 3");
  } else if (IsTHeadMemPair64 && Inst.getOperand(4).getImm() != 4) {
    SMLoc Loc = Operands.back()->getStartLoc();
    return Error(Loc, "operand must be constant 4");
  }

  const MCInstrDesc &MCID = MII.get(Opcode);
  if (!(MCID.TSFlags & RISCVII::ConstraintMask))
    return false;

  if (Opcode == RISCV::VC_V_XVW || Opcode == RISCV::VC_V_IVW ||
      Opcode == RISCV::VC_V_FVW || Opcode == RISCV::VC_V_VVW) {
    // Operands Opcode, Dst, uimm, Dst, Rs2, Rs1 for VC_V_XVW.
    unsigned VCIXDst = Inst.getOperand(0).getReg();
    SMLoc VCIXDstLoc = Operands[2]->getStartLoc();
    if (MCID.TSFlags & RISCVII::VS1Constraint) {
      unsigned VCIXRs1 = Inst.getOperand(Inst.getNumOperands() - 1).getReg();
      if (VCIXDst == VCIXRs1)
        return Error(VCIXDstLoc, "the destination vector register group cannot"
                                 " overlap the source vector register group");
    }
    if (MCID.TSFlags & RISCVII::VS2Constraint) {
      unsigned VCIXRs2 = Inst.getOperand(Inst.getNumOperands() - 2).getReg();
      if (VCIXDst == VCIXRs2)
        return Error(VCIXDstLoc, "the destination vector register group cannot"
                                 " overlap the source vector register group");
    }
    return false;
  }

  unsigned DestReg = Inst.getOperand(0).getReg();
  unsigned Offset = 0;
  int TiedOp = MCID.getOperandConstraint(1, MCOI::TIED_TO);
  if (TiedOp == 0)
    Offset = 1;

  // Operands[1] will be the first operand, DestReg.
  SMLoc Loc = Operands[1]->getStartLoc();
  if (MCID.TSFlags & RISCVII::VS2Constraint) {
    unsigned CheckReg = Inst.getOperand(Offset + 1).getReg();
    if (DestReg == CheckReg)
      return Error(Loc, "the destination vector register group cannot overlap"
                        " the source vector register group");
  }
  if ((MCID.TSFlags & RISCVII::VS1Constraint) && Inst.getOperand(Offset + 2).isReg()) {
    unsigned CheckReg = Inst.getOperand(Offset + 2).getReg();
    if (DestReg == CheckReg)
      return Error(Loc, "the destination vector register group cannot overlap"
                        " the source vector register group");
  }
  if ((MCID.TSFlags & RISCVII::VMConstraint) && (DestReg == RISCV::V0)) {
    // vadc, vsbc are special cases. These instructions have no mask register.
    // The destination register could not be V0.
    if (Opcode == RISCV::VADC_VVM || Opcode == RISCV::VADC_VXM ||
        Opcode == RISCV::VADC_VIM || Opcode == RISCV::VSBC_VVM ||
        Opcode == RISCV::VSBC_VXM || Opcode == RISCV::VFMERGE_VFM ||
        Opcode == RISCV::VMERGE_VIM || Opcode == RISCV::VMERGE_VVM ||
        Opcode == RISCV::VMERGE_VXM)
      return Error(Loc, "the destination vector register group cannot be V0");

    // Regardless masked or unmasked version, the number of operands is the
    // same. For example, "viota.m v0, v2" is "viota.m v0, v2, NoRegister"
    // actually. We need to check the last operand to ensure whether it is
    // masked or not.
    unsigned CheckReg = Inst.getOperand(Inst.getNumOperands() - 1).getReg();
    assert((CheckReg == RISCV::V0 || CheckReg == RISCV::NoRegister) &&
           "Unexpected register for mask operand");

    if (DestReg == CheckReg)
      return Error(Loc, "the destination vector register group cannot overlap"
                        " the mask register");
  }
  return false;
}

bool RISCVAsmParser::processInstruction(MCInst &Inst, SMLoc IDLoc,
                                        OperandVector &Operands,
                                        MCStreamer &Out) {
  Inst.setLoc(IDLoc);

  switch (Inst.getOpcode()) {
  default:
    break;
  case RISCV::PseudoLLAImm:
  case RISCV::PseudoLAImm:
  case RISCV::PseudoLI: {
    MCRegister Reg = Inst.getOperand(0).getReg();
    const MCOperand &Op1 = Inst.getOperand(1);
    if (Op1.isExpr()) {
      // We must have li reg, %lo(sym) or li reg, %pcrel_lo(sym) or similar.
      // Just convert to an addi. This allows compatibility with gas.
      emitToStreamer(Out, MCInstBuilder(RISCV::ADDI)
                              .addReg(Reg)
                              .addReg(RISCV::X0)
                              .addExpr(Op1.getExpr()));
      return false;
    }
    int64_t Imm = Inst.getOperand(1).getImm();
    // On RV32 the immediate here can either be a signed or an unsigned
    // 32-bit number. Sign extension has to be performed to ensure that Imm
    // represents the expected signed 64-bit number.
    if (!isRV64())
      Imm = SignExtend64<32>(Imm);
    emitLoadImm(Reg, Imm, Out);
    return false;
  }
  case RISCV::PseudoLLA:
    emitLoadLocalAddress(Inst, IDLoc, Out);
    return false;
  case RISCV::PseudoLGA:
    emitLoadGlobalAddress(Inst, IDLoc, Out);
    return false;
  case RISCV::PseudoLA:
    emitLoadAddress(Inst, IDLoc, Out);
    return false;
  case RISCV::PseudoLA_TLS_IE:
    emitLoadTLSIEAddress(Inst, IDLoc, Out);
    return false;
  case RISCV::PseudoLA_TLS_GD:
    emitLoadTLSGDAddress(Inst, IDLoc, Out);
    return false;
  case RISCV::PseudoLB:
    emitLoadStoreSymbol(Inst, RISCV::LB, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoLBU:
    emitLoadStoreSymbol(Inst, RISCV::LBU, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoLH:
    emitLoadStoreSymbol(Inst, RISCV::LH, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoLHU:
    emitLoadStoreSymbol(Inst, RISCV::LHU, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoLW:
    emitLoadStoreSymbol(Inst, RISCV::LW, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoLWU:
    emitLoadStoreSymbol(Inst, RISCV::LWU, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoLD:
    emitLoadStoreSymbol(Inst, RISCV::LD, IDLoc, Out, /*HasTmpReg=*/false);
    return false;
  case RISCV::PseudoFLH:
    emitLoadStoreSymbol(Inst, RISCV::FLH, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoFLW:
    emitLoadStoreSymbol(Inst, RISCV::FLW, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoFLD:
    emitLoadStoreSymbol(Inst, RISCV::FLD, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoSB:
    emitLoadStoreSymbol(Inst, RISCV::SB, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoSH:
    emitLoadStoreSymbol(Inst, RISCV::SH, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoSW:
    emitLoadStoreSymbol(Inst, RISCV::SW, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoSD:
    emitLoadStoreSymbol(Inst, RISCV::SD, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoFSH:
    emitLoadStoreSymbol(Inst, RISCV::FSH, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoFSW:
    emitLoadStoreSymbol(Inst, RISCV::FSW, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoFSD:
    emitLoadStoreSymbol(Inst, RISCV::FSD, IDLoc, Out, /*HasTmpReg=*/true);
    return false;
  case RISCV::PseudoAddTPRel:
    if (checkPseudoAddTPRel(Inst, Operands))
      return true;
    break;
  case RISCV::PseudoTLSDESCCall:
    if (checkPseudoTLSDESCCall(Inst, Operands))
      return true;
    break;
  case RISCV::PseudoSEXT_B:
    emitPseudoExtend(Inst, /*SignExtend=*/true, /*Width=*/8, IDLoc, Out);
    return false;
  case RISCV::PseudoSEXT_H:
    emitPseudoExtend(Inst, /*SignExtend=*/true, /*Width=*/16, IDLoc, Out);
    return false;
  case RISCV::PseudoZEXT_H:
    emitPseudoExtend(Inst, /*SignExtend=*/false, /*Width=*/16, IDLoc, Out);
    return false;
  case RISCV::PseudoZEXT_W:
    emitPseudoExtend(Inst, /*SignExtend=*/false, /*Width=*/32, IDLoc, Out);
    return false;
  case RISCV::PseudoVMSGEU_VX:
  case RISCV::PseudoVMSGEU_VX_M:
  case RISCV::PseudoVMSGEU_VX_M_T:
    emitVMSGE(Inst, RISCV::VMSLTU_VX, IDLoc, Out);
    return false;
  case RISCV::PseudoVMSGE_VX:
  case RISCV::PseudoVMSGE_VX_M:
  case RISCV::PseudoVMSGE_VX_M_T:
    emitVMSGE(Inst, RISCV::VMSLT_VX, IDLoc, Out);
    return false;
  case RISCV::PseudoVMSGE_VI:
  case RISCV::PseudoVMSLT_VI: {
    // These instructions are signed and so is immediate so we can subtract one
    // and change the opcode.
    int64_t Imm = Inst.getOperand(2).getImm();
    unsigned Opc = Inst.getOpcode() == RISCV::PseudoVMSGE_VI ? RISCV::VMSGT_VI
                                                             : RISCV::VMSLE_VI;
    emitToStreamer(Out, MCInstBuilder(Opc)
                            .addOperand(Inst.getOperand(0))
                            .addOperand(Inst.getOperand(1))
                            .addImm(Imm - 1)
                            .addOperand(Inst.getOperand(3))
                            .setLoc(IDLoc));
    return false;
  }
  case RISCV::PseudoVMSGEU_VI:
  case RISCV::PseudoVMSLTU_VI: {
    int64_t Imm = Inst.getOperand(2).getImm();
    // Unsigned comparisons are tricky because the immediate is signed. If the
    // immediate is 0 we can't just subtract one. vmsltu.vi v0, v1, 0 is always
    // false, but vmsle.vi v0, v1, -1 is always true. Instead we use
    // vmsne v0, v1, v1 which is always false.
    if (Imm == 0) {
      unsigned Opc = Inst.getOpcode() == RISCV::PseudoVMSGEU_VI
                         ? RISCV::VMSEQ_VV
                         : RISCV::VMSNE_VV;
      emitToStreamer(Out, MCInstBuilder(Opc)
                              .addOperand(Inst.getOperand(0))
                              .addOperand(Inst.getOperand(1))
                              .addOperand(Inst.getOperand(1))
                              .addOperand(Inst.getOperand(3))
                              .setLoc(IDLoc));
    } else {
      // Other immediate values can subtract one like signed.
      unsigned Opc = Inst.getOpcode() == RISCV::PseudoVMSGEU_VI
                         ? RISCV::VMSGTU_VI
                         : RISCV::VMSLEU_VI;
      emitToStreamer(Out, MCInstBuilder(Opc)
                              .addOperand(Inst.getOperand(0))
                              .addOperand(Inst.getOperand(1))
                              .addImm(Imm - 1)
                              .addOperand(Inst.getOperand(3))
                              .setLoc(IDLoc));
    }

    return false;
  }
  }

  emitToStreamer(Out, Inst);
  return false;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCVAsmParser() {
  RegisterMCAsmParser<RISCVAsmParser> X(getTheRISCV32Target());
  RegisterMCAsmParser<RISCVAsmParser> Y(getTheRISCV64Target());
}
