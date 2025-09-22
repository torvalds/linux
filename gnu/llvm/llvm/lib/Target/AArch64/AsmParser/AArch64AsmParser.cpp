//==- AArch64AsmParser.cpp - Parse AArch64 assembly to MCInst instructions -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64InstPrinter.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "MCTargetDesc/AArch64TargetStreamer.h"
#include "TargetInfo/AArch64TargetInfo.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

enum class RegKind {
  Scalar,
  NeonVector,
  SVEDataVector,
  SVEPredicateAsCounter,
  SVEPredicateVector,
  Matrix,
  LookupTable
};

enum class MatrixKind { Array, Tile, Row, Col };

enum RegConstraintEqualityTy {
  EqualsReg,
  EqualsSuperReg,
  EqualsSubReg
};

class AArch64AsmParser : public MCTargetAsmParser {
private:
  StringRef Mnemonic; ///< Instruction mnemonic.

  // Map of register aliases registers via the .req directive.
  StringMap<std::pair<RegKind, unsigned>> RegisterReqs;

  class PrefixInfo {
  public:
    static PrefixInfo CreateFromInst(const MCInst &Inst, uint64_t TSFlags) {
      PrefixInfo Prefix;
      switch (Inst.getOpcode()) {
      case AArch64::MOVPRFX_ZZ:
        Prefix.Active = true;
        Prefix.Dst = Inst.getOperand(0).getReg();
        break;
      case AArch64::MOVPRFX_ZPmZ_B:
      case AArch64::MOVPRFX_ZPmZ_H:
      case AArch64::MOVPRFX_ZPmZ_S:
      case AArch64::MOVPRFX_ZPmZ_D:
        Prefix.Active = true;
        Prefix.Predicated = true;
        Prefix.ElementSize = TSFlags & AArch64::ElementSizeMask;
        assert(Prefix.ElementSize != AArch64::ElementSizeNone &&
               "No destructive element size set for movprfx");
        Prefix.Dst = Inst.getOperand(0).getReg();
        Prefix.Pg = Inst.getOperand(2).getReg();
        break;
      case AArch64::MOVPRFX_ZPzZ_B:
      case AArch64::MOVPRFX_ZPzZ_H:
      case AArch64::MOVPRFX_ZPzZ_S:
      case AArch64::MOVPRFX_ZPzZ_D:
        Prefix.Active = true;
        Prefix.Predicated = true;
        Prefix.ElementSize = TSFlags & AArch64::ElementSizeMask;
        assert(Prefix.ElementSize != AArch64::ElementSizeNone &&
               "No destructive element size set for movprfx");
        Prefix.Dst = Inst.getOperand(0).getReg();
        Prefix.Pg = Inst.getOperand(1).getReg();
        break;
      default:
        break;
      }

      return Prefix;
    }

    PrefixInfo() = default;
    bool isActive() const { return Active; }
    bool isPredicated() const { return Predicated; }
    unsigned getElementSize() const {
      assert(Predicated);
      return ElementSize;
    }
    unsigned getDstReg() const { return Dst; }
    unsigned getPgReg() const {
      assert(Predicated);
      return Pg;
    }

  private:
    bool Active = false;
    bool Predicated = false;
    unsigned ElementSize;
    unsigned Dst;
    unsigned Pg;
  } NextPrefix;

  AArch64TargetStreamer &getTargetStreamer() {
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<AArch64TargetStreamer &>(TS);
  }

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

  bool parseSysAlias(StringRef Name, SMLoc NameLoc, OperandVector &Operands);
  bool parseSyspAlias(StringRef Name, SMLoc NameLoc, OperandVector &Operands);
  void createSysAlias(uint16_t Encoding, OperandVector &Operands, SMLoc S);
  AArch64CC::CondCode parseCondCodeString(StringRef Cond,
                                          std::string &Suggestion);
  bool parseCondCode(OperandVector &Operands, bool invertCondCode);
  unsigned matchRegisterNameAlias(StringRef Name, RegKind Kind);
  bool parseRegister(OperandVector &Operands);
  bool parseSymbolicImmVal(const MCExpr *&ImmVal);
  bool parseNeonVectorList(OperandVector &Operands);
  bool parseOptionalMulOperand(OperandVector &Operands);
  bool parseOptionalVGOperand(OperandVector &Operands, StringRef &VecGroup);
  bool parseKeywordOperand(OperandVector &Operands);
  bool parseOperand(OperandVector &Operands, bool isCondCode,
                    bool invertCondCode);
  bool parseImmExpr(int64_t &Out);
  bool parseComma();
  bool parseRegisterInRange(unsigned &Out, unsigned Base, unsigned First,
                            unsigned Last);

  bool showMatchError(SMLoc Loc, unsigned ErrCode, uint64_t ErrorInfo,
                      OperandVector &Operands);

  bool parseAuthExpr(const MCExpr *&Res, SMLoc &EndLoc);

  bool parseDirectiveArch(SMLoc L);
  bool parseDirectiveArchExtension(SMLoc L);
  bool parseDirectiveCPU(SMLoc L);
  bool parseDirectiveInst(SMLoc L);

  bool parseDirectiveTLSDescCall(SMLoc L);

  bool parseDirectiveLOH(StringRef LOH, SMLoc L);
  bool parseDirectiveLtorg(SMLoc L);

  bool parseDirectiveReq(StringRef Name, SMLoc L);
  bool parseDirectiveUnreq(SMLoc L);
  bool parseDirectiveCFINegateRAState();
  bool parseDirectiveCFIBKeyFrame();
  bool parseDirectiveCFIMTETaggedFrame();

  bool parseDirectiveVariantPCS(SMLoc L);

  bool parseDirectiveSEHAllocStack(SMLoc L);
  bool parseDirectiveSEHPrologEnd(SMLoc L);
  bool parseDirectiveSEHSaveR19R20X(SMLoc L);
  bool parseDirectiveSEHSaveFPLR(SMLoc L);
  bool parseDirectiveSEHSaveFPLRX(SMLoc L);
  bool parseDirectiveSEHSaveReg(SMLoc L);
  bool parseDirectiveSEHSaveRegX(SMLoc L);
  bool parseDirectiveSEHSaveRegP(SMLoc L);
  bool parseDirectiveSEHSaveRegPX(SMLoc L);
  bool parseDirectiveSEHSaveLRPair(SMLoc L);
  bool parseDirectiveSEHSaveFReg(SMLoc L);
  bool parseDirectiveSEHSaveFRegX(SMLoc L);
  bool parseDirectiveSEHSaveFRegP(SMLoc L);
  bool parseDirectiveSEHSaveFRegPX(SMLoc L);
  bool parseDirectiveSEHSetFP(SMLoc L);
  bool parseDirectiveSEHAddFP(SMLoc L);
  bool parseDirectiveSEHNop(SMLoc L);
  bool parseDirectiveSEHSaveNext(SMLoc L);
  bool parseDirectiveSEHEpilogStart(SMLoc L);
  bool parseDirectiveSEHEpilogEnd(SMLoc L);
  bool parseDirectiveSEHTrapFrame(SMLoc L);
  bool parseDirectiveSEHMachineFrame(SMLoc L);
  bool parseDirectiveSEHContext(SMLoc L);
  bool parseDirectiveSEHECContext(SMLoc L);
  bool parseDirectiveSEHClearUnwoundToCall(SMLoc L);
  bool parseDirectiveSEHPACSignLR(SMLoc L);
  bool parseDirectiveSEHSaveAnyReg(SMLoc L, bool Paired, bool Writeback);

  bool validateInstruction(MCInst &Inst, SMLoc &IDLoc,
                           SmallVectorImpl<SMLoc> &Loc);
  unsigned getNumRegsForRegKind(RegKind K);
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
/// @name Auto-generated Match Functions
/// {

#define GET_ASSEMBLER_HEADER
#include "AArch64GenAsmMatcher.inc"

  /// }

  ParseStatus tryParseScalarRegister(MCRegister &Reg);
  ParseStatus tryParseVectorRegister(MCRegister &Reg, StringRef &Kind,
                                     RegKind MatchKind);
  ParseStatus tryParseMatrixRegister(OperandVector &Operands);
  ParseStatus tryParseSVCR(OperandVector &Operands);
  ParseStatus tryParseOptionalShiftExtend(OperandVector &Operands);
  ParseStatus tryParseBarrierOperand(OperandVector &Operands);
  ParseStatus tryParseBarriernXSOperand(OperandVector &Operands);
  ParseStatus tryParseSysReg(OperandVector &Operands);
  ParseStatus tryParseSysCROperand(OperandVector &Operands);
  template <bool IsSVEPrefetch = false>
  ParseStatus tryParsePrefetch(OperandVector &Operands);
  ParseStatus tryParseRPRFMOperand(OperandVector &Operands);
  ParseStatus tryParsePSBHint(OperandVector &Operands);
  ParseStatus tryParseBTIHint(OperandVector &Operands);
  ParseStatus tryParseAdrpLabel(OperandVector &Operands);
  ParseStatus tryParseAdrLabel(OperandVector &Operands);
  template <bool AddFPZeroAsLiteral>
  ParseStatus tryParseFPImm(OperandVector &Operands);
  ParseStatus tryParseImmWithOptionalShift(OperandVector &Operands);
  ParseStatus tryParseGPR64sp0Operand(OperandVector &Operands);
  bool tryParseNeonVectorRegister(OperandVector &Operands);
  ParseStatus tryParseVectorIndex(OperandVector &Operands);
  ParseStatus tryParseGPRSeqPair(OperandVector &Operands);
  ParseStatus tryParseSyspXzrPair(OperandVector &Operands);
  template <bool ParseShiftExtend,
            RegConstraintEqualityTy EqTy = RegConstraintEqualityTy::EqualsReg>
  ParseStatus tryParseGPROperand(OperandVector &Operands);
  ParseStatus tryParseZTOperand(OperandVector &Operands);
  template <bool ParseShiftExtend, bool ParseSuffix>
  ParseStatus tryParseSVEDataVector(OperandVector &Operands);
  template <RegKind RK>
  ParseStatus tryParseSVEPredicateVector(OperandVector &Operands);
  ParseStatus
  tryParseSVEPredicateOrPredicateAsCounterVector(OperandVector &Operands);
  template <RegKind VectorKind>
  ParseStatus tryParseVectorList(OperandVector &Operands,
                                 bool ExpectMatch = false);
  ParseStatus tryParseMatrixTileList(OperandVector &Operands);
  ParseStatus tryParseSVEPattern(OperandVector &Operands);
  ParseStatus tryParseSVEVecLenSpecifier(OperandVector &Operands);
  ParseStatus tryParseGPR64x8(OperandVector &Operands);
  ParseStatus tryParseImmRange(OperandVector &Operands);

public:
  enum AArch64MatchResultTy {
    Match_InvalidSuffix = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "AArch64GenAsmMatcher.inc"
  };
  bool IsILP32;
  bool IsWindowsArm64EC;

  AArch64AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII, const MCTargetOptions &Options)
    : MCTargetAsmParser(Options, STI, MII) {
    IsILP32 = STI.getTargetTriple().getEnvironment() == Triple::GNUILP32;
    IsWindowsArm64EC = STI.getTargetTriple().isWindowsArm64EC();
    MCAsmParserExtension::Initialize(Parser);
    MCStreamer &S = getParser().getStreamer();
    if (S.getTargetStreamer() == nullptr)
      new AArch64TargetStreamer(S);

    // Alias .hword/.word/.[dx]word to the target-independent
    // .2byte/.4byte/.8byte directives as they have the same form and
    // semantics:
    ///  ::= (.hword | .word | .dword | .xword ) [ expression (, expression)* ]
    Parser.addAliasForDirective(".hword", ".2byte");
    Parser.addAliasForDirective(".word", ".4byte");
    Parser.addAliasForDirective(".dword", ".8byte");
    Parser.addAliasForDirective(".xword", ".8byte");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }

  bool areEqualRegs(const MCParsedAsmOperand &Op1,
                    const MCParsedAsmOperand &Op2) const override;
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  bool ParseDirective(AsmToken DirectiveID) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;

  static bool classifySymbolRef(const MCExpr *Expr,
                                AArch64MCExpr::VariantKind &ELFRefKind,
                                MCSymbolRefExpr::VariantKind &DarwinRefKind,
                                int64_t &Addend);
};

/// AArch64Operand - Instances of this class represent a parsed AArch64 machine
/// instruction.
class AArch64Operand : public MCParsedAsmOperand {
private:
  enum KindTy {
    k_Immediate,
    k_ShiftedImm,
    k_ImmRange,
    k_CondCode,
    k_Register,
    k_MatrixRegister,
    k_MatrixTileList,
    k_SVCR,
    k_VectorList,
    k_VectorIndex,
    k_Token,
    k_SysReg,
    k_SysCR,
    k_Prefetch,
    k_ShiftExtend,
    k_FPImm,
    k_Barrier,
    k_PSBHint,
    k_BTIHint,
  } Kind;

  SMLoc StartLoc, EndLoc;

  struct TokOp {
    const char *Data;
    unsigned Length;
    bool IsSuffix; // Is the operand actually a suffix on the mnemonic.
  };

  // Separate shift/extend operand.
  struct ShiftExtendOp {
    AArch64_AM::ShiftExtendType Type;
    unsigned Amount;
    bool HasExplicitAmount;
  };

  struct RegOp {
    unsigned RegNum;
    RegKind Kind;
    int ElementWidth;

    // The register may be allowed as a different register class,
    // e.g. for GPR64as32 or GPR32as64.
    RegConstraintEqualityTy EqualityTy;

    // In some cases the shift/extend needs to be explicitly parsed together
    // with the register, rather than as a separate operand. This is needed
    // for addressing modes where the instruction as a whole dictates the
    // scaling/extend, rather than specific bits in the instruction.
    // By parsing them as a single operand, we avoid the need to pass an
    // extra operand in all CodeGen patterns (because all operands need to
    // have an associated value), and we avoid the need to update TableGen to
    // accept operands that have no associated bits in the instruction.
    //
    // An added benefit of parsing them together is that the assembler
    // can give a sensible diagnostic if the scaling is not correct.
    //
    // The default is 'lsl #0' (HasExplicitAmount = false) if no
    // ShiftExtend is specified.
    ShiftExtendOp ShiftExtend;
  };

  struct MatrixRegOp {
    unsigned RegNum;
    unsigned ElementWidth;
    MatrixKind Kind;
  };

  struct MatrixTileListOp {
    unsigned RegMask = 0;
  };

  struct VectorListOp {
    unsigned RegNum;
    unsigned Count;
    unsigned Stride;
    unsigned NumElements;
    unsigned ElementWidth;
    RegKind  RegisterKind;
  };

  struct VectorIndexOp {
    int Val;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct ShiftedImmOp {
    const MCExpr *Val;
    unsigned ShiftAmount;
  };

  struct ImmRangeOp {
    unsigned First;
    unsigned Last;
  };

  struct CondCodeOp {
    AArch64CC::CondCode Code;
  };

  struct FPImmOp {
    uint64_t Val; // APFloat value bitcasted to uint64_t.
    bool IsExact; // describes whether parsed value was exact.
  };

  struct BarrierOp {
    const char *Data;
    unsigned Length;
    unsigned Val; // Not the enum since not all values have names.
    bool HasnXSModifier;
  };

  struct SysRegOp {
    const char *Data;
    unsigned Length;
    uint32_t MRSReg;
    uint32_t MSRReg;
    uint32_t PStateField;
  };

  struct SysCRImmOp {
    unsigned Val;
  };

  struct PrefetchOp {
    const char *Data;
    unsigned Length;
    unsigned Val;
  };

  struct PSBHintOp {
    const char *Data;
    unsigned Length;
    unsigned Val;
  };

  struct BTIHintOp {
    const char *Data;
    unsigned Length;
    unsigned Val;
  };

  struct SVCROp {
    const char *Data;
    unsigned Length;
    unsigned PStateField;
  };

  union {
    struct TokOp Tok;
    struct RegOp Reg;
    struct MatrixRegOp MatrixReg;
    struct MatrixTileListOp MatrixTileList;
    struct VectorListOp VectorList;
    struct VectorIndexOp VectorIndex;
    struct ImmOp Imm;
    struct ShiftedImmOp ShiftedImm;
    struct ImmRangeOp ImmRange;
    struct CondCodeOp CondCode;
    struct FPImmOp FPImm;
    struct BarrierOp Barrier;
    struct SysRegOp SysReg;
    struct SysCRImmOp SysCRImm;
    struct PrefetchOp Prefetch;
    struct PSBHintOp PSBHint;
    struct BTIHintOp BTIHint;
    struct ShiftExtendOp ShiftExtend;
    struct SVCROp SVCR;
  };

  // Keep the MCContext around as the MCExprs may need manipulated during
  // the add<>Operands() calls.
  MCContext &Ctx;

public:
  AArch64Operand(KindTy K, MCContext &Ctx) : Kind(K), Ctx(Ctx) {}

  AArch64Operand(const AArch64Operand &o) : MCParsedAsmOperand(), Ctx(o.Ctx) {
    Kind = o.Kind;
    StartLoc = o.StartLoc;
    EndLoc = o.EndLoc;
    switch (Kind) {
    case k_Token:
      Tok = o.Tok;
      break;
    case k_Immediate:
      Imm = o.Imm;
      break;
    case k_ShiftedImm:
      ShiftedImm = o.ShiftedImm;
      break;
    case k_ImmRange:
      ImmRange = o.ImmRange;
      break;
    case k_CondCode:
      CondCode = o.CondCode;
      break;
    case k_FPImm:
      FPImm = o.FPImm;
      break;
    case k_Barrier:
      Barrier = o.Barrier;
      break;
    case k_Register:
      Reg = o.Reg;
      break;
    case k_MatrixRegister:
      MatrixReg = o.MatrixReg;
      break;
    case k_MatrixTileList:
      MatrixTileList = o.MatrixTileList;
      break;
    case k_VectorList:
      VectorList = o.VectorList;
      break;
    case k_VectorIndex:
      VectorIndex = o.VectorIndex;
      break;
    case k_SysReg:
      SysReg = o.SysReg;
      break;
    case k_SysCR:
      SysCRImm = o.SysCRImm;
      break;
    case k_Prefetch:
      Prefetch = o.Prefetch;
      break;
    case k_PSBHint:
      PSBHint = o.PSBHint;
      break;
    case k_BTIHint:
      BTIHint = o.BTIHint;
      break;
    case k_ShiftExtend:
      ShiftExtend = o.ShiftExtend;
      break;
    case k_SVCR:
      SVCR = o.SVCR;
      break;
    }
  }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  bool isTokenSuffix() const {
    assert(Kind == k_Token && "Invalid access!");
    return Tok.IsSuffix;
  }

  const MCExpr *getImm() const {
    assert(Kind == k_Immediate && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getShiftedImmVal() const {
    assert(Kind == k_ShiftedImm && "Invalid access!");
    return ShiftedImm.Val;
  }

  unsigned getShiftedImmShift() const {
    assert(Kind == k_ShiftedImm && "Invalid access!");
    return ShiftedImm.ShiftAmount;
  }

  unsigned getFirstImmVal() const {
    assert(Kind == k_ImmRange && "Invalid access!");
    return ImmRange.First;
  }

  unsigned getLastImmVal() const {
    assert(Kind == k_ImmRange && "Invalid access!");
    return ImmRange.Last;
  }

  AArch64CC::CondCode getCondCode() const {
    assert(Kind == k_CondCode && "Invalid access!");
    return CondCode.Code;
  }

  APFloat getFPImm() const {
    assert (Kind == k_FPImm && "Invalid access!");
    return APFloat(APFloat::IEEEdouble(), APInt(64, FPImm.Val, true));
  }

  bool getFPImmIsExact() const {
    assert (Kind == k_FPImm && "Invalid access!");
    return FPImm.IsExact;
  }

  unsigned getBarrier() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return Barrier.Val;
  }

  StringRef getBarrierName() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return StringRef(Barrier.Data, Barrier.Length);
  }

  bool getBarriernXSModifier() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return Barrier.HasnXSModifier;
  }

  MCRegister getReg() const override {
    assert(Kind == k_Register && "Invalid access!");
    return Reg.RegNum;
  }

  unsigned getMatrixReg() const {
    assert(Kind == k_MatrixRegister && "Invalid access!");
    return MatrixReg.RegNum;
  }

  unsigned getMatrixElementWidth() const {
    assert(Kind == k_MatrixRegister && "Invalid access!");
    return MatrixReg.ElementWidth;
  }

  MatrixKind getMatrixKind() const {
    assert(Kind == k_MatrixRegister && "Invalid access!");
    return MatrixReg.Kind;
  }

  unsigned getMatrixTileListRegMask() const {
    assert(isMatrixTileList() && "Invalid access!");
    return MatrixTileList.RegMask;
  }

  RegConstraintEqualityTy getRegEqualityTy() const {
    assert(Kind == k_Register && "Invalid access!");
    return Reg.EqualityTy;
  }

  unsigned getVectorListStart() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.RegNum;
  }

  unsigned getVectorListCount() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.Count;
  }

  unsigned getVectorListStride() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.Stride;
  }

  int getVectorIndex() const {
    assert(Kind == k_VectorIndex && "Invalid access!");
    return VectorIndex.Val;
  }

  StringRef getSysReg() const {
    assert(Kind == k_SysReg && "Invalid access!");
    return StringRef(SysReg.Data, SysReg.Length);
  }

  unsigned getSysCR() const {
    assert(Kind == k_SysCR && "Invalid access!");
    return SysCRImm.Val;
  }

  unsigned getPrefetch() const {
    assert(Kind == k_Prefetch && "Invalid access!");
    return Prefetch.Val;
  }

  unsigned getPSBHint() const {
    assert(Kind == k_PSBHint && "Invalid access!");
    return PSBHint.Val;
  }

  StringRef getPSBHintName() const {
    assert(Kind == k_PSBHint && "Invalid access!");
    return StringRef(PSBHint.Data, PSBHint.Length);
  }

  unsigned getBTIHint() const {
    assert(Kind == k_BTIHint && "Invalid access!");
    return BTIHint.Val;
  }

  StringRef getBTIHintName() const {
    assert(Kind == k_BTIHint && "Invalid access!");
    return StringRef(BTIHint.Data, BTIHint.Length);
  }

  StringRef getSVCR() const {
    assert(Kind == k_SVCR && "Invalid access!");
    return StringRef(SVCR.Data, SVCR.Length);
  }

  StringRef getPrefetchName() const {
    assert(Kind == k_Prefetch && "Invalid access!");
    return StringRef(Prefetch.Data, Prefetch.Length);
  }

  AArch64_AM::ShiftExtendType getShiftExtendType() const {
    if (Kind == k_ShiftExtend)
      return ShiftExtend.Type;
    if (Kind == k_Register)
      return Reg.ShiftExtend.Type;
    llvm_unreachable("Invalid access!");
  }

  unsigned getShiftExtendAmount() const {
    if (Kind == k_ShiftExtend)
      return ShiftExtend.Amount;
    if (Kind == k_Register)
      return Reg.ShiftExtend.Amount;
    llvm_unreachable("Invalid access!");
  }

  bool hasShiftExtendAmount() const {
    if (Kind == k_ShiftExtend)
      return ShiftExtend.HasExplicitAmount;
    if (Kind == k_Register)
      return Reg.ShiftExtend.HasExplicitAmount;
    llvm_unreachable("Invalid access!");
  }

  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return false; }

  bool isUImm6() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 64);
  }

  template <int Width> bool isSImm() const { return isSImmScaled<Width, 1>(); }

  template <int Bits, int Scale> DiagnosticPredicate isSImmScaled() const {
    return isImmScaled<Bits, Scale>(true);
  }

  template <int Bits, int Scale, int Offset = 0, bool IsRange = false>
  DiagnosticPredicate isUImmScaled() const {
    if (IsRange && isImmRange() &&
        (getLastImmVal() != getFirstImmVal() + Offset))
      return DiagnosticPredicateTy::NoMatch;

    return isImmScaled<Bits, Scale, IsRange>(false);
  }

  template <int Bits, int Scale, bool IsRange = false>
  DiagnosticPredicate isImmScaled(bool Signed) const {
    if ((!isImm() && !isImmRange()) || (isImm() && IsRange) ||
        (isImmRange() && !IsRange))
      return DiagnosticPredicateTy::NoMatch;

    int64_t Val;
    if (isImmRange())
      Val = getFirstImmVal();
    else {
      const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
      if (!MCE)
        return DiagnosticPredicateTy::NoMatch;
      Val = MCE->getValue();
    }

    int64_t MinVal, MaxVal;
    if (Signed) {
      int64_t Shift = Bits - 1;
      MinVal = (int64_t(1) << Shift) * -Scale;
      MaxVal = ((int64_t(1) << Shift) - 1) * Scale;
    } else {
      MinVal = 0;
      MaxVal = ((int64_t(1) << Bits) - 1) * Scale;
    }

    if (Val >= MinVal && Val <= MaxVal && (Val % Scale) == 0)
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  DiagnosticPredicate isSVEPattern() const {
    if (!isImm())
      return DiagnosticPredicateTy::NoMatch;
    auto *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return DiagnosticPredicateTy::NoMatch;
    int64_t Val = MCE->getValue();
    if (Val >= 0 && Val < 32)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  DiagnosticPredicate isSVEVecLenSpecifier() const {
    if (!isImm())
      return DiagnosticPredicateTy::NoMatch;
    auto *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return DiagnosticPredicateTy::NoMatch;
    int64_t Val = MCE->getValue();
    if (Val >= 0 && Val <= 1)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  bool isSymbolicUImm12Offset(const MCExpr *Expr) const {
    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (!AArch64AsmParser::classifySymbolRef(Expr, ELFRefKind, DarwinRefKind,
                                           Addend)) {
      // If we don't understand the expression, assume the best and
      // let the fixup and relocation code deal with it.
      return true;
    }

    if (DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF ||
        ELFRefKind == AArch64MCExpr::VK_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_GOT_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_TPREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_GOTTPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_SECREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_SECREL_HI12 ||
        ELFRefKind == AArch64MCExpr::VK_GOT_PAGE_LO15) {
      // Note that we don't range-check the addend. It's adjusted modulo page
      // size when converted, so there is no "out of range" condition when using
      // @pageoff.
      return true;
    } else if (DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGEOFF ||
               DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF) {
      // @gotpageoff/@tlvppageoff can only be used directly, not with an addend.
      return Addend == 0;
    }

    return false;
  }

  template <int Scale> bool isUImm12Offset() const {
    if (!isImm())
      return false;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return isSymbolicUImm12Offset(getImm());

    int64_t Val = MCE->getValue();
    return (Val % Scale) == 0 && Val >= 0 && (Val / Scale) < 0x1000;
  }

  template <int N, int M>
  bool isImmInRange() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= N && Val <= M);
  }

  // NOTE: Also used for isLogicalImmNot as anything that can be represented as
  // a logical immediate can always be represented when inverted.
  template <typename T>
  bool isLogicalImm() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;

    int64_t Val = MCE->getValue();
    // Avoid left shift by 64 directly.
    uint64_t Upper = UINT64_C(-1) << (sizeof(T) * 4) << (sizeof(T) * 4);
    // Allow all-0 or all-1 in top bits to permit bitwise NOT.
    if ((Val & Upper) && (Val & Upper) != Upper)
      return false;

    return AArch64_AM::isLogicalImmediate(Val & ~Upper, sizeof(T) * 8);
  }

  bool isShiftedImm() const { return Kind == k_ShiftedImm; }

  bool isImmRange() const { return Kind == k_ImmRange; }

  /// Returns the immediate value as a pair of (imm, shift) if the immediate is
  /// a shifted immediate by value 'Shift' or '0', or if it is an unshifted
  /// immediate that can be shifted by 'Shift'.
  template <unsigned Width>
  std::optional<std::pair<int64_t, unsigned>> getShiftedVal() const {
    if (isShiftedImm() && Width == getShiftedImmShift())
      if (auto *CE = dyn_cast<MCConstantExpr>(getShiftedImmVal()))
        return std::make_pair(CE->getValue(), Width);

    if (isImm())
      if (auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
        int64_t Val = CE->getValue();
        if ((Val != 0) && (uint64_t(Val >> Width) << Width) == uint64_t(Val))
          return std::make_pair(Val >> Width, Width);
        else
          return std::make_pair(Val, 0u);
      }

    return {};
  }

  bool isAddSubImm() const {
    if (!isShiftedImm() && !isImm())
      return false;

    const MCExpr *Expr;

    // An ADD/SUB shifter is either 'lsl #0' or 'lsl #12'.
    if (isShiftedImm()) {
      unsigned Shift = ShiftedImm.ShiftAmount;
      Expr = ShiftedImm.Val;
      if (Shift != 0 && Shift != 12)
        return false;
    } else {
      Expr = getImm();
    }

    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (AArch64AsmParser::classifySymbolRef(Expr, ELFRefKind,
                                          DarwinRefKind, Addend)) {
      return DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF
          || DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF
          || (DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGEOFF && Addend == 0)
          || ELFRefKind == AArch64MCExpr::VK_LO12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC
          || ELFRefKind == AArch64MCExpr::VK_TPREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_TPREL_LO12
          || ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC
          || ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12
          || ELFRefKind == AArch64MCExpr::VK_SECREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_SECREL_LO12;
    }

    // If it's a constant, it should be a real immediate in range.
    if (auto ShiftedVal = getShiftedVal<12>())
      return ShiftedVal->first >= 0 && ShiftedVal->first <= 0xfff;

    // If it's an expression, we hope for the best and let the fixup/relocation
    // code deal with it.
    return true;
  }

  bool isAddSubImmNeg() const {
    if (!isShiftedImm() && !isImm())
      return false;

    // Otherwise it should be a real negative immediate in range.
    if (auto ShiftedVal = getShiftedVal<12>())
      return ShiftedVal->first < 0 && -ShiftedVal->first <= 0xfff;

    return false;
  }

  // Signed value in the range -128 to +127. For element widths of
  // 16 bits or higher it may also be a signed multiple of 256 in the
  // range -32768 to +32512.
  // For element-width of 8 bits a range of -128 to 255 is accepted,
  // since a copy of a byte can be either signed/unsigned.
  template <typename T>
  DiagnosticPredicate isSVECpyImm() const {
    if (!isShiftedImm() && (!isImm() || !isa<MCConstantExpr>(getImm())))
      return DiagnosticPredicateTy::NoMatch;

    bool IsByte = std::is_same<int8_t, std::make_signed_t<T>>::value ||
                  std::is_same<int8_t, T>::value;
    if (auto ShiftedImm = getShiftedVal<8>())
      if (!(IsByte && ShiftedImm->second) &&
          AArch64_AM::isSVECpyImm<T>(uint64_t(ShiftedImm->first)
                                     << ShiftedImm->second))
        return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  // Unsigned value in the range 0 to 255. For element widths of
  // 16 bits or higher it may also be a signed multiple of 256 in the
  // range 0 to 65280.
  template <typename T> DiagnosticPredicate isSVEAddSubImm() const {
    if (!isShiftedImm() && (!isImm() || !isa<MCConstantExpr>(getImm())))
      return DiagnosticPredicateTy::NoMatch;

    bool IsByte = std::is_same<int8_t, std::make_signed_t<T>>::value ||
                  std::is_same<int8_t, T>::value;
    if (auto ShiftedImm = getShiftedVal<8>())
      if (!(IsByte && ShiftedImm->second) &&
          AArch64_AM::isSVEAddSubImm<T>(ShiftedImm->first
                                        << ShiftedImm->second))
        return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <typename T> DiagnosticPredicate isSVEPreferredLogicalImm() const {
    if (isLogicalImm<T>() && !isSVECpyImm<T>())
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NoMatch;
  }

  bool isCondCode() const { return Kind == k_CondCode; }

  bool isSIMDImmType10() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    return AArch64_AM::isAdvSIMDModImmType10(MCE->getValue());
  }

  template<int N>
  bool isBranchTarget() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return true;
    int64_t Val = MCE->getValue();
    if (Val & 0x3)
      return false;
    assert(N > 0 && "Branch target immediate cannot be 0 bits!");
    return (Val >= -((1<<(N-1)) << 2) && Val <= (((1<<(N-1))-1) << 2));
  }

  bool
  isMovWSymbol(ArrayRef<AArch64MCExpr::VariantKind> AllowedModifiers) const {
    if (!isImm())
      return false;

    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (!AArch64AsmParser::classifySymbolRef(getImm(), ELFRefKind,
                                             DarwinRefKind, Addend)) {
      return false;
    }
    if (DarwinRefKind != MCSymbolRefExpr::VK_None)
      return false;

    return llvm::is_contained(AllowedModifiers, ELFRefKind);
  }

  bool isMovWSymbolG3() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G3, AArch64MCExpr::VK_PREL_G3});
  }

  bool isMovWSymbolG2() const {
    return isMovWSymbol(
        {AArch64MCExpr::VK_ABS_G2, AArch64MCExpr::VK_ABS_G2_S,
         AArch64MCExpr::VK_ABS_G2_NC, AArch64MCExpr::VK_PREL_G2,
         AArch64MCExpr::VK_PREL_G2_NC, AArch64MCExpr::VK_TPREL_G2,
         AArch64MCExpr::VK_DTPREL_G2});
  }

  bool isMovWSymbolG1() const {
    return isMovWSymbol(
        {AArch64MCExpr::VK_ABS_G1, AArch64MCExpr::VK_ABS_G1_S,
         AArch64MCExpr::VK_ABS_G1_NC, AArch64MCExpr::VK_PREL_G1,
         AArch64MCExpr::VK_PREL_G1_NC, AArch64MCExpr::VK_GOTTPREL_G1,
         AArch64MCExpr::VK_TPREL_G1, AArch64MCExpr::VK_TPREL_G1_NC,
         AArch64MCExpr::VK_DTPREL_G1, AArch64MCExpr::VK_DTPREL_G1_NC});
  }

  bool isMovWSymbolG0() const {
    return isMovWSymbol(
        {AArch64MCExpr::VK_ABS_G0, AArch64MCExpr::VK_ABS_G0_S,
         AArch64MCExpr::VK_ABS_G0_NC, AArch64MCExpr::VK_PREL_G0,
         AArch64MCExpr::VK_PREL_G0_NC, AArch64MCExpr::VK_GOTTPREL_G0_NC,
         AArch64MCExpr::VK_TPREL_G0, AArch64MCExpr::VK_TPREL_G0_NC,
         AArch64MCExpr::VK_DTPREL_G0, AArch64MCExpr::VK_DTPREL_G0_NC});
  }

  template<int RegWidth, int Shift>
  bool isMOVZMovAlias() const {
    if (!isImm()) return false;

    const MCExpr *E = getImm();
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(E)) {
      uint64_t Value = CE->getValue();

      return AArch64_AM::isMOVZMovAlias(Value, Shift, RegWidth);
    }
    // Only supports the case of Shift being 0 if an expression is used as an
    // operand
    return !Shift && E;
  }

  template<int RegWidth, int Shift>
  bool isMOVNMovAlias() const {
    if (!isImm()) return false;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    uint64_t Value = CE->getValue();

    return AArch64_AM::isMOVNMovAlias(Value, Shift, RegWidth);
  }

  bool isFPImm() const {
    return Kind == k_FPImm &&
           AArch64_AM::getFP64Imm(getFPImm().bitcastToAPInt()) != -1;
  }

  bool isBarrier() const {
    return Kind == k_Barrier && !getBarriernXSModifier();
  }
  bool isBarriernXS() const {
    return Kind == k_Barrier && getBarriernXSModifier();
  }
  bool isSysReg() const { return Kind == k_SysReg; }

  bool isMRSSystemRegister() const {
    if (!isSysReg()) return false;

    return SysReg.MRSReg != -1U;
  }

  bool isMSRSystemRegister() const {
    if (!isSysReg()) return false;
    return SysReg.MSRReg != -1U;
  }

  bool isSystemPStateFieldWithImm0_1() const {
    if (!isSysReg()) return false;
    return AArch64PState::lookupPStateImm0_1ByEncoding(SysReg.PStateField);
  }

  bool isSystemPStateFieldWithImm0_15() const {
    if (!isSysReg())
      return false;
    return AArch64PState::lookupPStateImm0_15ByEncoding(SysReg.PStateField);
  }

  bool isSVCR() const {
    if (Kind != k_SVCR)
      return false;
    return SVCR.PStateField != -1U;
  }

  bool isReg() const override {
    return Kind == k_Register;
  }

  bool isVectorList() const { return Kind == k_VectorList; }

  bool isScalarReg() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar;
  }

  bool isNeonVectorReg() const {
    return Kind == k_Register && Reg.Kind == RegKind::NeonVector;
  }

  bool isNeonVectorRegLo() const {
    return Kind == k_Register && Reg.Kind == RegKind::NeonVector &&
           (AArch64MCRegisterClasses[AArch64::FPR128_loRegClassID].contains(
                Reg.RegNum) ||
            AArch64MCRegisterClasses[AArch64::FPR64_loRegClassID].contains(
                Reg.RegNum));
  }

  bool isNeonVectorReg0to7() const {
    return Kind == k_Register && Reg.Kind == RegKind::NeonVector &&
           (AArch64MCRegisterClasses[AArch64::FPR128_0to7RegClassID].contains(
               Reg.RegNum));
  }

  bool isMatrix() const { return Kind == k_MatrixRegister; }
  bool isMatrixTileList() const { return Kind == k_MatrixTileList; }

  template <unsigned Class> bool isSVEPredicateAsCounterReg() const {
    RegKind RK;
    switch (Class) {
    case AArch64::PPRRegClassID:
    case AArch64::PPR_3bRegClassID:
    case AArch64::PPR_p8to15RegClassID:
    case AArch64::PNRRegClassID:
    case AArch64::PNR_p8to15RegClassID:
    case AArch64::PPRorPNRRegClassID:
      RK = RegKind::SVEPredicateAsCounter;
      break;
    default:
      llvm_unreachable("Unsupport register class");
    }

    return (Kind == k_Register && Reg.Kind == RK) &&
           AArch64MCRegisterClasses[Class].contains(getReg());
  }

  template <unsigned Class> bool isSVEVectorReg() const {
    RegKind RK;
    switch (Class) {
    case AArch64::ZPRRegClassID:
    case AArch64::ZPR_3bRegClassID:
    case AArch64::ZPR_4bRegClassID:
      RK = RegKind::SVEDataVector;
      break;
    case AArch64::PPRRegClassID:
    case AArch64::PPR_3bRegClassID:
    case AArch64::PPR_p8to15RegClassID:
    case AArch64::PNRRegClassID:
    case AArch64::PNR_p8to15RegClassID:
    case AArch64::PPRorPNRRegClassID:
      RK = RegKind::SVEPredicateVector;
      break;
    default:
      llvm_unreachable("Unsupport register class");
    }

    return (Kind == k_Register && Reg.Kind == RK) &&
           AArch64MCRegisterClasses[Class].contains(getReg());
  }

  template <unsigned Class> bool isFPRasZPR() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[Class].contains(getReg());
  }

  template <int ElementWidth, unsigned Class>
  DiagnosticPredicate isSVEPredicateVectorRegOfWidth() const {
    if (Kind != k_Register || Reg.Kind != RegKind::SVEPredicateVector)
      return DiagnosticPredicateTy::NoMatch;

    if (isSVEVectorReg<Class>() && (Reg.ElementWidth == ElementWidth))
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <int ElementWidth, unsigned Class>
  DiagnosticPredicate isSVEPredicateOrPredicateAsCounterRegOfWidth() const {
    if (Kind != k_Register || (Reg.Kind != RegKind::SVEPredicateAsCounter &&
                               Reg.Kind != RegKind::SVEPredicateVector))
      return DiagnosticPredicateTy::NoMatch;

    if ((isSVEPredicateAsCounterReg<Class>() ||
         isSVEPredicateVectorRegOfWidth<ElementWidth, Class>()) &&
        Reg.ElementWidth == ElementWidth)
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <int ElementWidth, unsigned Class>
  DiagnosticPredicate isSVEPredicateAsCounterRegOfWidth() const {
    if (Kind != k_Register || Reg.Kind != RegKind::SVEPredicateAsCounter)
      return DiagnosticPredicateTy::NoMatch;

    if (isSVEPredicateAsCounterReg<Class>() && (Reg.ElementWidth == ElementWidth))
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <int ElementWidth, unsigned Class>
  DiagnosticPredicate isSVEDataVectorRegOfWidth() const {
    if (Kind != k_Register || Reg.Kind != RegKind::SVEDataVector)
      return DiagnosticPredicateTy::NoMatch;

    if (isSVEVectorReg<Class>() && Reg.ElementWidth == ElementWidth)
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <int ElementWidth, unsigned Class,
            AArch64_AM::ShiftExtendType ShiftExtendTy, int ShiftWidth,
            bool ShiftWidthAlwaysSame>
  DiagnosticPredicate isSVEDataVectorRegWithShiftExtend() const {
    auto VectorMatch = isSVEDataVectorRegOfWidth<ElementWidth, Class>();
    if (!VectorMatch.isMatch())
      return DiagnosticPredicateTy::NoMatch;

    // Give a more specific diagnostic when the user has explicitly typed in
    // a shift-amount that does not match what is expected, but for which
    // there is also an unscaled addressing mode (e.g. sxtw/uxtw).
    bool MatchShift = getShiftExtendAmount() == Log2_32(ShiftWidth / 8);
    if (!MatchShift && (ShiftExtendTy == AArch64_AM::UXTW ||
                        ShiftExtendTy == AArch64_AM::SXTW) &&
        !ShiftWidthAlwaysSame && hasShiftExtendAmount() && ShiftWidth == 8)
      return DiagnosticPredicateTy::NoMatch;

    if (MatchShift && ShiftExtendTy == getShiftExtendType())
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  bool isGPR32as64() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
      AArch64MCRegisterClasses[AArch64::GPR64RegClassID].contains(Reg.RegNum);
  }

  bool isGPR64as32() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
      AArch64MCRegisterClasses[AArch64::GPR32RegClassID].contains(Reg.RegNum);
  }

  bool isGPR64x8() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[AArch64::GPR64x8ClassRegClassID].contains(
               Reg.RegNum);
  }

  bool isWSeqPair() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[AArch64::WSeqPairsClassRegClassID].contains(
               Reg.RegNum);
  }

  bool isXSeqPair() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[AArch64::XSeqPairsClassRegClassID].contains(
               Reg.RegNum);
  }

  bool isSyspXzrPair() const {
    return isGPR64<AArch64::GPR64RegClassID>() && Reg.RegNum == AArch64::XZR;
  }

  template<int64_t Angle, int64_t Remainder>
  DiagnosticPredicate isComplexRotation() const {
    if (!isImm()) return DiagnosticPredicateTy::NoMatch;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return DiagnosticPredicateTy::NoMatch;
    uint64_t Value = CE->getValue();

    if (Value % Angle == Remainder && Value <= 270)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  template <unsigned RegClassID> bool isGPR64() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[RegClassID].contains(getReg());
  }

  template <unsigned RegClassID, int ExtWidth>
  DiagnosticPredicate isGPR64WithShiftExtend() const {
    if (Kind != k_Register || Reg.Kind != RegKind::Scalar)
      return DiagnosticPredicateTy::NoMatch;

    if (isGPR64<RegClassID>() && getShiftExtendType() == AArch64_AM::LSL &&
        getShiftExtendAmount() == Log2_32(ExtWidth / 8))
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  /// Is this a vector list with the type implicit (presumably attached to the
  /// instruction itself)?
  template <RegKind VectorKind, unsigned NumRegs>
  bool isImplicitlyTypedVectorList() const {
    return Kind == k_VectorList && VectorList.Count == NumRegs &&
           VectorList.NumElements == 0 &&
           VectorList.RegisterKind == VectorKind;
  }

  template <RegKind VectorKind, unsigned NumRegs, unsigned NumElements,
            unsigned ElementWidth, unsigned Stride = 1>
  bool isTypedVectorList() const {
    if (Kind != k_VectorList)
      return false;
    if (VectorList.Count != NumRegs)
      return false;
    if (VectorList.RegisterKind != VectorKind)
      return false;
    if (VectorList.ElementWidth != ElementWidth)
      return false;
    if (VectorList.Stride != Stride)
      return false;
    return VectorList.NumElements == NumElements;
  }

  template <RegKind VectorKind, unsigned NumRegs, unsigned NumElements,
            unsigned ElementWidth>
  DiagnosticPredicate isTypedVectorListMultiple() const {
    bool Res =
        isTypedVectorList<VectorKind, NumRegs, NumElements, ElementWidth>();
    if (!Res)
      return DiagnosticPredicateTy::NoMatch;
    if (((VectorList.RegNum - AArch64::Z0) % NumRegs) != 0)
      return DiagnosticPredicateTy::NearMatch;
    return DiagnosticPredicateTy::Match;
  }

  template <RegKind VectorKind, unsigned NumRegs, unsigned Stride,
            unsigned ElementWidth>
  DiagnosticPredicate isTypedVectorListStrided() const {
    bool Res = isTypedVectorList<VectorKind, NumRegs, /*NumElements*/ 0,
                                 ElementWidth, Stride>();
    if (!Res)
      return DiagnosticPredicateTy::NoMatch;
    if ((VectorList.RegNum < (AArch64::Z0 + Stride)) ||
        ((VectorList.RegNum >= AArch64::Z16) &&
         (VectorList.RegNum < (AArch64::Z16 + Stride))))
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NoMatch;
  }

  template <int Min, int Max>
  DiagnosticPredicate isVectorIndex() const {
    if (Kind != k_VectorIndex)
      return DiagnosticPredicateTy::NoMatch;
    if (VectorIndex.Val >= Min && VectorIndex.Val <= Max)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  bool isToken() const override { return Kind == k_Token; }

  bool isTokenEqual(StringRef Str) const {
    return Kind == k_Token && getToken() == Str;
  }
  bool isSysCR() const { return Kind == k_SysCR; }
  bool isPrefetch() const { return Kind == k_Prefetch; }
  bool isPSBHint() const { return Kind == k_PSBHint; }
  bool isBTIHint() const { return Kind == k_BTIHint; }
  bool isShiftExtend() const { return Kind == k_ShiftExtend; }
  bool isShifter() const {
    if (!isShiftExtend())
      return false;

    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR || ST == AArch64_AM::ROR ||
            ST == AArch64_AM::MSL);
  }

  template <unsigned ImmEnum> DiagnosticPredicate isExactFPImm() const {
    if (Kind != k_FPImm)
      return DiagnosticPredicateTy::NoMatch;

    if (getFPImmIsExact()) {
      // Lookup the immediate from table of supported immediates.
      auto *Desc = AArch64ExactFPImm::lookupExactFPImmByEnum(ImmEnum);
      assert(Desc && "Unknown enum value");

      // Calculate its FP value.
      APFloat RealVal(APFloat::IEEEdouble());
      auto StatusOrErr =
          RealVal.convertFromString(Desc->Repr, APFloat::rmTowardZero);
      if (errorToBool(StatusOrErr.takeError()) || *StatusOrErr != APFloat::opOK)
        llvm_unreachable("FP immediate is not exact");

      if (getFPImm().bitwiseIsEqual(RealVal))
        return DiagnosticPredicateTy::Match;
    }

    return DiagnosticPredicateTy::NearMatch;
  }

  template <unsigned ImmA, unsigned ImmB>
  DiagnosticPredicate isExactFPImm() const {
    DiagnosticPredicate Res = DiagnosticPredicateTy::NoMatch;
    if ((Res = isExactFPImm<ImmA>()))
      return DiagnosticPredicateTy::Match;
    if ((Res = isExactFPImm<ImmB>()))
      return DiagnosticPredicateTy::Match;
    return Res;
  }

  bool isExtend() const {
    if (!isShiftExtend())
      return false;

    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTB || ET == AArch64_AM::SXTB ||
            ET == AArch64_AM::UXTH || ET == AArch64_AM::SXTH ||
            ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW ||
            ET == AArch64_AM::UXTX || ET == AArch64_AM::SXTX ||
            ET == AArch64_AM::LSL) &&
           getShiftExtendAmount() <= 4;
  }

  bool isExtend64() const {
    if (!isExtend())
      return false;
    // Make sure the extend expects a 32-bit source register.
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return ET == AArch64_AM::UXTB || ET == AArch64_AM::SXTB ||
           ET == AArch64_AM::UXTH || ET == AArch64_AM::SXTH ||
           ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW;
  }

  bool isExtendLSL64() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTX || ET == AArch64_AM::SXTX ||
            ET == AArch64_AM::LSL) &&
           getShiftExtendAmount() <= 4;
  }

  bool isLSLImm3Shift() const {
    if (!isShiftExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return ET == AArch64_AM::LSL && getShiftExtendAmount() <= 7;
  }

  template<int Width> bool isMemXExtend() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::LSL || ET == AArch64_AM::SXTX) &&
           (getShiftExtendAmount() == Log2_32(Width / 8) ||
            getShiftExtendAmount() == 0);
  }

  template<int Width> bool isMemWExtend() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW) &&
           (getShiftExtendAmount() == Log2_32(Width / 8) ||
            getShiftExtendAmount() == 0);
  }

  template <unsigned width>
  bool isArithmeticShifter() const {
    if (!isShifter())
      return false;

    // An arithmetic shifter is LSL, LSR, or ASR.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR) && getShiftExtendAmount() < width;
  }

  template <unsigned width>
  bool isLogicalShifter() const {
    if (!isShifter())
      return false;

    // A logical shifter is LSL, LSR, ASR or ROR.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR || ST == AArch64_AM::ROR) &&
           getShiftExtendAmount() < width;
  }

  bool isMovImm32Shifter() const {
    if (!isShifter())
      return false;

    // A MOVi shifter is LSL of 0, 16, 32, or 48.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    if (ST != AArch64_AM::LSL)
      return false;
    uint64_t Val = getShiftExtendAmount();
    return (Val == 0 || Val == 16);
  }

  bool isMovImm64Shifter() const {
    if (!isShifter())
      return false;

    // A MOVi shifter is LSL of 0 or 16.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    if (ST != AArch64_AM::LSL)
      return false;
    uint64_t Val = getShiftExtendAmount();
    return (Val == 0 || Val == 16 || Val == 32 || Val == 48);
  }

  bool isLogicalVecShifter() const {
    if (!isShifter())
      return false;

    // A logical vector shifter is a left shift by 0, 8, 16, or 24.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::LSL &&
           (Shift == 0 || Shift == 8 || Shift == 16 || Shift == 24);
  }

  bool isLogicalVecHalfWordShifter() const {
    if (!isLogicalVecShifter())
      return false;

    // A logical vector shifter is a left shift by 0 or 8.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::LSL &&
           (Shift == 0 || Shift == 8);
  }

  bool isMoveVecShifter() const {
    if (!isShiftExtend())
      return false;

    // A logical vector shifter is a left shift by 8 or 16.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::MSL &&
           (Shift == 8 || Shift == 16);
  }

  // Fallback unscaled operands are for aliases of LDR/STR that fall back
  // to LDUR/STUR when the offset is not legal for the former but is for
  // the latter. As such, in addition to checking for being a legal unscaled
  // address, also check that it is not a legal scaled address. This avoids
  // ambiguity in the matcher.
  template<int Width>
  bool isSImm9OffsetFB() const {
    return isSImm<9>() && !isUImm12Offset<Width / 8>();
  }

  bool isAdrpLabel() const {
    // Validation was handled during parsing, so we just verify that
    // something didn't go haywire.
    if (!isImm())
        return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Min = - (4096 * (1LL << (21 - 1)));
      int64_t Max = 4096 * ((1LL << (21 - 1)) - 1);
      return (Val % 4096) == 0 && Val >= Min && Val <= Max;
    }

    return true;
  }

  bool isAdrLabel() const {
    // Validation was handled during parsing, so we just verify that
    // something didn't go haywire.
    if (!isImm())
        return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Min = - (1LL << (21 - 1));
      int64_t Max = ((1LL << (21 - 1)) - 1);
      return Val >= Min && Val <= Max;
    }

    return true;
  }

  template <MatrixKind Kind, unsigned EltSize, unsigned RegClass>
  DiagnosticPredicate isMatrixRegOperand() const {
    if (!isMatrix())
      return DiagnosticPredicateTy::NoMatch;
    if (getMatrixKind() != Kind ||
        !AArch64MCRegisterClasses[RegClass].contains(getMatrixReg()) ||
        EltSize != getMatrixElementWidth())
      return DiagnosticPredicateTy::NearMatch;
    return DiagnosticPredicateTy::Match;
  }

  bool isPAuthPCRelLabel16Operand() const {
    // PAuth PCRel16 operands are similar to regular branch targets, but only
    // negative values are allowed for concrete immediates as signing instr
    // should be in a lower address.
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return true;
    int64_t Val = MCE->getValue();
    if (Val & 0b11)
      return false;
    return (Val <= 0) && (Val > -(1 << 18));
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addMatrixOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getMatrixReg()));
  }

  void addGPR32as64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::GPR64RegClassID].contains(getReg()));

    const MCRegisterInfo *RI = Ctx.getRegisterInfo();
    uint32_t Reg = RI->getRegClass(AArch64::GPR32RegClassID).getRegister(
        RI->getEncodingValue(getReg()));

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addGPR64as32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::GPR32RegClassID].contains(getReg()));

    const MCRegisterInfo *RI = Ctx.getRegisterInfo();
    uint32_t Reg = RI->getRegClass(AArch64::GPR64RegClassID).getRegister(
        RI->getEncodingValue(getReg()));

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  template <int Width>
  void addFPRasZPRRegOperands(MCInst &Inst, unsigned N) const {
    unsigned Base;
    switch (Width) {
    case 8:   Base = AArch64::B0; break;
    case 16:  Base = AArch64::H0; break;
    case 32:  Base = AArch64::S0; break;
    case 64:  Base = AArch64::D0; break;
    case 128: Base = AArch64::Q0; break;
    default:
      llvm_unreachable("Unsupported width");
    }
    Inst.addOperand(MCOperand::createReg(AArch64::Z0 + getReg() - Base));
  }

  void addPPRorPNRRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned Reg = getReg();
    // Normalise to PPR
    if (Reg >= AArch64::PN0 && Reg <= AArch64::PN15)
      Reg = Reg - AArch64::PN0 + AArch64::P0;
    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addPNRasPPRRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(
        MCOperand::createReg((getReg() - AArch64::PN0) + AArch64::P0));
  }

  void addVectorReg64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::FPR128RegClassID].contains(getReg()));
    Inst.addOperand(MCOperand::createReg(AArch64::D0 + getReg() - AArch64::Q0));
  }

  void addVectorReg128Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::FPR128RegClassID].contains(getReg()));
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addVectorRegLoOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addVectorReg0to7Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  enum VecListIndexType {
    VecListIdx_DReg = 0,
    VecListIdx_QReg = 1,
    VecListIdx_ZReg = 2,
    VecListIdx_PReg = 3,
  };

  template <VecListIndexType RegTy, unsigned NumRegs>
  void addVectorListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    static const unsigned FirstRegs[][5] = {
      /* DReg */ { AArch64::Q0,
                   AArch64::D0,       AArch64::D0_D1,
                   AArch64::D0_D1_D2, AArch64::D0_D1_D2_D3 },
      /* QReg */ { AArch64::Q0,
                   AArch64::Q0,       AArch64::Q0_Q1,
                   AArch64::Q0_Q1_Q2, AArch64::Q0_Q1_Q2_Q3 },
      /* ZReg */ { AArch64::Z0,
                   AArch64::Z0,       AArch64::Z0_Z1,
                   AArch64::Z0_Z1_Z2, AArch64::Z0_Z1_Z2_Z3 },
      /* PReg */ { AArch64::P0,
                   AArch64::P0,       AArch64::P0_P1 }
    };

    assert((RegTy != VecListIdx_ZReg || NumRegs <= 4) &&
           " NumRegs must be <= 4 for ZRegs");

    assert((RegTy != VecListIdx_PReg || NumRegs <= 2) &&
           " NumRegs must be <= 2 for PRegs");

    unsigned FirstReg = FirstRegs[(unsigned)RegTy][NumRegs];
    Inst.addOperand(MCOperand::createReg(FirstReg + getVectorListStart() -
                                         FirstRegs[(unsigned)RegTy][0]));
  }

  template <unsigned NumRegs>
  void addStridedVectorListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert((NumRegs == 2 || NumRegs == 4) && " NumRegs must be 2 or 4");

    switch (NumRegs) {
    case 2:
      if (getVectorListStart() < AArch64::Z16) {
        assert((getVectorListStart() < AArch64::Z8) &&
               (getVectorListStart() >= AArch64::Z0) && "Invalid Register");
        Inst.addOperand(MCOperand::createReg(
            AArch64::Z0_Z8 + getVectorListStart() - AArch64::Z0));
      } else {
        assert((getVectorListStart() < AArch64::Z24) &&
               (getVectorListStart() >= AArch64::Z16) && "Invalid Register");
        Inst.addOperand(MCOperand::createReg(
            AArch64::Z16_Z24 + getVectorListStart() - AArch64::Z16));
      }
      break;
    case 4:
      if (getVectorListStart() < AArch64::Z16) {
        assert((getVectorListStart() < AArch64::Z4) &&
               (getVectorListStart() >= AArch64::Z0) && "Invalid Register");
        Inst.addOperand(MCOperand::createReg(
            AArch64::Z0_Z4_Z8_Z12 + getVectorListStart() - AArch64::Z0));
      } else {
        assert((getVectorListStart() < AArch64::Z20) &&
               (getVectorListStart() >= AArch64::Z16) && "Invalid Register");
        Inst.addOperand(MCOperand::createReg(
            AArch64::Z16_Z20_Z24_Z28 + getVectorListStart() - AArch64::Z16));
      }
      break;
    default:
      llvm_unreachable("Unsupported number of registers for strided vec list");
    }
  }

  void addMatrixTileListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned RegMask = getMatrixTileListRegMask();
    assert(RegMask <= 0xFF && "Invalid mask!");
    Inst.addOperand(MCOperand::createImm(RegMask));
  }

  void addVectorIndexOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  template <unsigned ImmIs0, unsigned ImmIs1>
  void addExactFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(bool(isExactFPImm<ImmIs0, ImmIs1>()) && "Invalid operand");
    Inst.addOperand(MCOperand::createImm(bool(isExactFPImm<ImmIs1>())));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // If this is a pageoff symrefexpr with an addend, adjust the addend
    // to be only the page-offset portion. Otherwise, just add the expr
    // as-is.
    addExpr(Inst, getImm());
  }

  template <int Shift>
  void addImmWithOptionalShiftOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (auto ShiftedVal = getShiftedVal<Shift>()) {
      Inst.addOperand(MCOperand::createImm(ShiftedVal->first));
      Inst.addOperand(MCOperand::createImm(ShiftedVal->second));
    } else if (isShiftedImm()) {
      addExpr(Inst, getShiftedImmVal());
      Inst.addOperand(MCOperand::createImm(getShiftedImmShift()));
    } else {
      addExpr(Inst, getImm());
      Inst.addOperand(MCOperand::createImm(0));
    }
  }

  template <int Shift>
  void addImmNegWithOptionalShiftOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (auto ShiftedVal = getShiftedVal<Shift>()) {
      Inst.addOperand(MCOperand::createImm(-ShiftedVal->first));
      Inst.addOperand(MCOperand::createImm(ShiftedVal->second));
    } else
      llvm_unreachable("Not a shifted negative immediate");
  }

  void addCondCodeOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getCondCode()));
  }

  void addAdrpLabelOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      addExpr(Inst, getImm());
    else
      Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 12));
  }

  void addAdrLabelOperands(MCInst &Inst, unsigned N) const {
    addImmOperands(Inst, N);
  }

  template<int Scale>
  void addUImm12OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());

    if (!MCE) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      return;
    }
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / Scale));
  }

  void addUImm6Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  template <int Scale>
  void addImmScaledOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / Scale));
  }

  template <int Scale>
  void addImmScaledRangeOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getFirstImmVal() / Scale));
  }

  template <typename T>
  void addLogicalImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    std::make_unsigned_t<T> Val = MCE->getValue();
    uint64_t encoding = AArch64_AM::encodeLogicalImmediate(Val, sizeof(T) * 8);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  template <typename T>
  void addLogicalImmNotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    std::make_unsigned_t<T> Val = ~MCE->getValue();
    uint64_t encoding = AArch64_AM::encodeLogicalImmediate(Val, sizeof(T) * 8);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addSIMDImmType10Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    uint64_t encoding = AArch64_AM::encodeAdvSIMDModImmType10(MCE->getValue());
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addBranchTarget26Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addPAuthPCRelLabel16Operands(MCInst &Inst, unsigned N) const {
    // PC-relative operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addPCRelLabel19Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addBranchTarget14Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(
        AArch64_AM::getFP64Imm(getFPImm().bitcastToAPInt())));
  }

  void addBarrierOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getBarrier()));
  }

  void addBarriernXSOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getBarrier()));
  }

  void addMRSSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.MRSReg));
  }

  void addMSRSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.MSRReg));
  }

  void addSystemPStateFieldWithImm0_1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.PStateField));
  }

  void addSVCROperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SVCR.PStateField));
  }

  void addSystemPStateFieldWithImm0_15Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.PStateField));
  }

  void addSysCROperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getSysCR()));
  }

  void addPrefetchOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPrefetch()));
  }

  void addPSBHintOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPSBHint()));
  }

  void addBTIHintOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getBTIHint()));
  }

  void addShifterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned Imm =
        AArch64_AM::getShifterImm(getShiftExtendType(), getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addLSLImm3ShifterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned Imm = getShiftExtendAmount();
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addSyspXzrPairOperand(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    if (!isScalarReg())
      return;

    const MCRegisterInfo *RI = Ctx.getRegisterInfo();
    uint32_t Reg = RI->getRegClass(AArch64::GPR64RegClassID)
                       .getRegister(RI->getEncodingValue(getReg()));
    if (Reg != AArch64::XZR)
      llvm_unreachable("wrong register");

    Inst.addOperand(MCOperand::createReg(AArch64::XZR));
  }

  void addExtendOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    if (ET == AArch64_AM::LSL) ET = AArch64_AM::UXTW;
    unsigned Imm = AArch64_AM::getArithExtendImm(ET, getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addExtend64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    if (ET == AArch64_AM::LSL) ET = AArch64_AM::UXTX;
    unsigned Imm = AArch64_AM::getArithExtendImm(ET, getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addMemExtendOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    bool IsSigned = ET == AArch64_AM::SXTW || ET == AArch64_AM::SXTX;
    Inst.addOperand(MCOperand::createImm(IsSigned));
    Inst.addOperand(MCOperand::createImm(getShiftExtendAmount() != 0));
  }

  // For 8-bit load/store instructions with a register offset, both the
  // "DoShift" and "NoShift" variants have a shift of 0. Because of this,
  // they're disambiguated by whether the shift was explicit or implicit rather
  // than its size.
  void addMemExtend8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    bool IsSigned = ET == AArch64_AM::SXTW || ET == AArch64_AM::SXTX;
    Inst.addOperand(MCOperand::createImm(IsSigned));
    Inst.addOperand(MCOperand::createImm(hasShiftExtendAmount()));
  }

  template<int Shift>
  void addMOVZMovAliasOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (CE) {
      uint64_t Value = CE->getValue();
      Inst.addOperand(MCOperand::createImm((Value >> Shift) & 0xffff));
    } else {
      addExpr(Inst, getImm());
    }
  }

  template<int Shift>
  void addMOVNMovAliasOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    const MCConstantExpr *CE = cast<MCConstantExpr>(getImm());
    uint64_t Value = CE->getValue();
    Inst.addOperand(MCOperand::createImm((~Value >> Shift) & 0xffff));
  }

  void addComplexRotationEvenOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / 90));
  }

  void addComplexRotationOddOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm((MCE->getValue() - 90) / 180));
  }

  void print(raw_ostream &OS) const override;

  static std::unique_ptr<AArch64Operand>
  CreateToken(StringRef Str, SMLoc S, MCContext &Ctx, bool IsSuffix = false) {
    auto Op = std::make_unique<AArch64Operand>(k_Token, Ctx);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->Tok.IsSuffix = IsSuffix;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateReg(unsigned RegNum, RegKind Kind, SMLoc S, SMLoc E, MCContext &Ctx,
            RegConstraintEqualityTy EqTy = RegConstraintEqualityTy::EqualsReg,
            AArch64_AM::ShiftExtendType ExtTy = AArch64_AM::LSL,
            unsigned ShiftAmount = 0,
            unsigned HasExplicitAmount = false) {
    auto Op = std::make_unique<AArch64Operand>(k_Register, Ctx);
    Op->Reg.RegNum = RegNum;
    Op->Reg.Kind = Kind;
    Op->Reg.ElementWidth = 0;
    Op->Reg.EqualityTy = EqTy;
    Op->Reg.ShiftExtend.Type = ExtTy;
    Op->Reg.ShiftExtend.Amount = ShiftAmount;
    Op->Reg.ShiftExtend.HasExplicitAmount = HasExplicitAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorReg(unsigned RegNum, RegKind Kind, unsigned ElementWidth,
                  SMLoc S, SMLoc E, MCContext &Ctx,
                  AArch64_AM::ShiftExtendType ExtTy = AArch64_AM::LSL,
                  unsigned ShiftAmount = 0,
                  unsigned HasExplicitAmount = false) {
    assert((Kind == RegKind::NeonVector || Kind == RegKind::SVEDataVector ||
            Kind == RegKind::SVEPredicateVector ||
            Kind == RegKind::SVEPredicateAsCounter) &&
           "Invalid vector kind");
    auto Op = CreateReg(RegNum, Kind, S, E, Ctx, EqualsReg, ExtTy, ShiftAmount,
                        HasExplicitAmount);
    Op->Reg.ElementWidth = ElementWidth;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorList(unsigned RegNum, unsigned Count, unsigned Stride,
                   unsigned NumElements, unsigned ElementWidth,
                   RegKind RegisterKind, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_VectorList, Ctx);
    Op->VectorList.RegNum = RegNum;
    Op->VectorList.Count = Count;
    Op->VectorList.Stride = Stride;
    Op->VectorList.NumElements = NumElements;
    Op->VectorList.ElementWidth = ElementWidth;
    Op->VectorList.RegisterKind = RegisterKind;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorIndex(int Idx, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_VectorIndex, Ctx);
    Op->VectorIndex.Val = Idx;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateMatrixTileList(unsigned RegMask, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_MatrixTileList, Ctx);
    Op->MatrixTileList.RegMask = RegMask;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static void ComputeRegsForAlias(unsigned Reg, SmallSet<unsigned, 8> &OutRegs,
                                  const unsigned ElementWidth) {
    static std::map<std::pair<unsigned, unsigned>, std::vector<unsigned>>
        RegMap = {
            {{0, AArch64::ZAB0},
             {AArch64::ZAD0, AArch64::ZAD1, AArch64::ZAD2, AArch64::ZAD3,
              AArch64::ZAD4, AArch64::ZAD5, AArch64::ZAD6, AArch64::ZAD7}},
            {{8, AArch64::ZAB0},
             {AArch64::ZAD0, AArch64::ZAD1, AArch64::ZAD2, AArch64::ZAD3,
              AArch64::ZAD4, AArch64::ZAD5, AArch64::ZAD6, AArch64::ZAD7}},
            {{16, AArch64::ZAH0},
             {AArch64::ZAD0, AArch64::ZAD2, AArch64::ZAD4, AArch64::ZAD6}},
            {{16, AArch64::ZAH1},
             {AArch64::ZAD1, AArch64::ZAD3, AArch64::ZAD5, AArch64::ZAD7}},
            {{32, AArch64::ZAS0}, {AArch64::ZAD0, AArch64::ZAD4}},
            {{32, AArch64::ZAS1}, {AArch64::ZAD1, AArch64::ZAD5}},
            {{32, AArch64::ZAS2}, {AArch64::ZAD2, AArch64::ZAD6}},
            {{32, AArch64::ZAS3}, {AArch64::ZAD3, AArch64::ZAD7}},
        };

    if (ElementWidth == 64)
      OutRegs.insert(Reg);
    else {
      std::vector<unsigned> Regs = RegMap[std::make_pair(ElementWidth, Reg)];
      assert(!Regs.empty() && "Invalid tile or element width!");
      for (auto OutReg : Regs)
        OutRegs.insert(OutReg);
    }
  }

  static std::unique_ptr<AArch64Operand> CreateImm(const MCExpr *Val, SMLoc S,
                                                   SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_Immediate, Ctx);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateShiftedImm(const MCExpr *Val,
                                                          unsigned ShiftAmount,
                                                          SMLoc S, SMLoc E,
                                                          MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_ShiftedImm, Ctx);
    Op->ShiftedImm .Val = Val;
    Op->ShiftedImm.ShiftAmount = ShiftAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateImmRange(unsigned First,
                                                        unsigned Last, SMLoc S,
                                                        SMLoc E,
                                                        MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_ImmRange, Ctx);
    Op->ImmRange.First = First;
    Op->ImmRange.Last = Last;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateCondCode(AArch64CC::CondCode Code, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_CondCode, Ctx);
    Op->CondCode.Code = Code;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateFPImm(APFloat Val, bool IsExact, SMLoc S, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_FPImm, Ctx);
    Op->FPImm.Val = Val.bitcastToAPInt().getSExtValue();
    Op->FPImm.IsExact = IsExact;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateBarrier(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx,
                                                       bool HasnXSModifier) {
    auto Op = std::make_unique<AArch64Operand>(k_Barrier, Ctx);
    Op->Barrier.Val = Val;
    Op->Barrier.Data = Str.data();
    Op->Barrier.Length = Str.size();
    Op->Barrier.HasnXSModifier = HasnXSModifier;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateSysReg(StringRef Str, SMLoc S,
                                                      uint32_t MRSReg,
                                                      uint32_t MSRReg,
                                                      uint32_t PStateField,
                                                      MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_SysReg, Ctx);
    Op->SysReg.Data = Str.data();
    Op->SysReg.Length = Str.size();
    Op->SysReg.MRSReg = MRSReg;
    Op->SysReg.MSRReg = MSRReg;
    Op->SysReg.PStateField = PStateField;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateSysCR(unsigned Val, SMLoc S,
                                                     SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_SysCR, Ctx);
    Op->SysCRImm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreatePrefetch(unsigned Val,
                                                        StringRef Str,
                                                        SMLoc S,
                                                        MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_Prefetch, Ctx);
    Op->Prefetch.Val = Val;
    Op->Barrier.Data = Str.data();
    Op->Barrier.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreatePSBHint(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_PSBHint, Ctx);
    Op->PSBHint.Val = Val;
    Op->PSBHint.Data = Str.data();
    Op->PSBHint.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateBTIHint(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_BTIHint, Ctx);
    Op->BTIHint.Val = Val | 32;
    Op->BTIHint.Data = Str.data();
    Op->BTIHint.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateMatrixRegister(unsigned RegNum, unsigned ElementWidth, MatrixKind Kind,
                       SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_MatrixRegister, Ctx);
    Op->MatrixReg.RegNum = RegNum;
    Op->MatrixReg.ElementWidth = ElementWidth;
    Op->MatrixReg.Kind = Kind;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateSVCR(uint32_t PStateField, StringRef Str, SMLoc S, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_SVCR, Ctx);
    Op->SVCR.PStateField = PStateField;
    Op->SVCR.Data = Str.data();
    Op->SVCR.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateShiftExtend(AArch64_AM::ShiftExtendType ShOp, unsigned Val,
                    bool HasExplicitAmount, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = std::make_unique<AArch64Operand>(k_ShiftExtend, Ctx);
    Op->ShiftExtend.Type = ShOp;
    Op->ShiftExtend.Amount = Val;
    Op->ShiftExtend.HasExplicitAmount = HasExplicitAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }
};

} // end anonymous namespace.

void AArch64Operand::print(raw_ostream &OS) const {
  switch (Kind) {
  case k_FPImm:
    OS << "<fpimm " << getFPImm().bitcastToAPInt().getZExtValue();
    if (!getFPImmIsExact())
      OS << " (inexact)";
    OS << ">";
    break;
  case k_Barrier: {
    StringRef Name = getBarrierName();
    if (!Name.empty())
      OS << "<barrier " << Name << ">";
    else
      OS << "<barrier invalid #" << getBarrier() << ">";
    break;
  }
  case k_Immediate:
    OS << *getImm();
    break;
  case k_ShiftedImm: {
    unsigned Shift = getShiftedImmShift();
    OS << "<shiftedimm ";
    OS << *getShiftedImmVal();
    OS << ", lsl #" << AArch64_AM::getShiftValue(Shift) << ">";
    break;
  }
  case k_ImmRange: {
    OS << "<immrange ";
    OS << getFirstImmVal();
    OS << ":" << getLastImmVal() << ">";
    break;
  }
  case k_CondCode:
    OS << "<condcode " << getCondCode() << ">";
    break;
  case k_VectorList: {
    OS << "<vectorlist ";
    unsigned Reg = getVectorListStart();
    for (unsigned i = 0, e = getVectorListCount(); i != e; ++i)
      OS << Reg + i * getVectorListStride() << " ";
    OS << ">";
    break;
  }
  case k_VectorIndex:
    OS << "<vectorindex " << getVectorIndex() << ">";
    break;
  case k_SysReg:
    OS << "<sysreg: " << getSysReg() << '>';
    break;
  case k_Token:
    OS << "'" << getToken() << "'";
    break;
  case k_SysCR:
    OS << "c" << getSysCR();
    break;
  case k_Prefetch: {
    StringRef Name = getPrefetchName();
    if (!Name.empty())
      OS << "<prfop " << Name << ">";
    else
      OS << "<prfop invalid #" << getPrefetch() << ">";
    break;
  }
  case k_PSBHint:
    OS << getPSBHintName();
    break;
  case k_BTIHint:
    OS << getBTIHintName();
    break;
  case k_MatrixRegister:
    OS << "<matrix " << getMatrixReg() << ">";
    break;
  case k_MatrixTileList: {
    OS << "<matrixlist ";
    unsigned RegMask = getMatrixTileListRegMask();
    unsigned MaxBits = 8;
    for (unsigned I = MaxBits; I > 0; --I)
      OS << ((RegMask & (1 << (I - 1))) >> (I - 1));
    OS << '>';
    break;
  }
  case k_SVCR: {
    OS << getSVCR();
    break;
  }
  case k_Register:
    OS << "<register " << getReg() << ">";
    if (!getShiftExtendAmount() && !hasShiftExtendAmount())
      break;
    [[fallthrough]];
  case k_ShiftExtend:
    OS << "<" << AArch64_AM::getShiftExtendName(getShiftExtendType()) << " #"
       << getShiftExtendAmount();
    if (!hasShiftExtendAmount())
      OS << "<imp>";
    OS << '>';
    break;
  }
}

/// @name Auto-generated Match Functions
/// {

static MCRegister MatchRegisterName(StringRef Name);

/// }

static unsigned MatchNeonVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("v0", AArch64::Q0)
      .Case("v1", AArch64::Q1)
      .Case("v2", AArch64::Q2)
      .Case("v3", AArch64::Q3)
      .Case("v4", AArch64::Q4)
      .Case("v5", AArch64::Q5)
      .Case("v6", AArch64::Q6)
      .Case("v7", AArch64::Q7)
      .Case("v8", AArch64::Q8)
      .Case("v9", AArch64::Q9)
      .Case("v10", AArch64::Q10)
      .Case("v11", AArch64::Q11)
      .Case("v12", AArch64::Q12)
      .Case("v13", AArch64::Q13)
      .Case("v14", AArch64::Q14)
      .Case("v15", AArch64::Q15)
      .Case("v16", AArch64::Q16)
      .Case("v17", AArch64::Q17)
      .Case("v18", AArch64::Q18)
      .Case("v19", AArch64::Q19)
      .Case("v20", AArch64::Q20)
      .Case("v21", AArch64::Q21)
      .Case("v22", AArch64::Q22)
      .Case("v23", AArch64::Q23)
      .Case("v24", AArch64::Q24)
      .Case("v25", AArch64::Q25)
      .Case("v26", AArch64::Q26)
      .Case("v27", AArch64::Q27)
      .Case("v28", AArch64::Q28)
      .Case("v29", AArch64::Q29)
      .Case("v30", AArch64::Q30)
      .Case("v31", AArch64::Q31)
      .Default(0);
}

/// Returns an optional pair of (#elements, element-width) if Suffix
/// is a valid vector kind. Where the number of elements in a vector
/// or the vector width is implicit or explicitly unknown (but still a
/// valid suffix kind), 0 is used.
static std::optional<std::pair<int, int>> parseVectorKind(StringRef Suffix,
                                                          RegKind VectorKind) {
  std::pair<int, int> Res = {-1, -1};

  switch (VectorKind) {
  case RegKind::NeonVector:
    Res = StringSwitch<std::pair<int, int>>(Suffix.lower())
              .Case("", {0, 0})
              .Case(".1d", {1, 64})
              .Case(".1q", {1, 128})
              // '.2h' needed for fp16 scalar pairwise reductions
              .Case(".2h", {2, 16})
              .Case(".2b", {2, 8})
              .Case(".2s", {2, 32})
              .Case(".2d", {2, 64})
              // '.4b' is another special case for the ARMv8.2a dot product
              // operand
              .Case(".4b", {4, 8})
              .Case(".4h", {4, 16})
              .Case(".4s", {4, 32})
              .Case(".8b", {8, 8})
              .Case(".8h", {8, 16})
              .Case(".16b", {16, 8})
              // Accept the width neutral ones, too, for verbose syntax. If
              // those aren't used in the right places, the token operand won't
              // match so all will work out.
              .Case(".b", {0, 8})
              .Case(".h", {0, 16})
              .Case(".s", {0, 32})
              .Case(".d", {0, 64})
              .Default({-1, -1});
    break;
  case RegKind::SVEPredicateAsCounter:
  case RegKind::SVEPredicateVector:
  case RegKind::SVEDataVector:
  case RegKind::Matrix:
    Res = StringSwitch<std::pair<int, int>>(Suffix.lower())
              .Case("", {0, 0})
              .Case(".b", {0, 8})
              .Case(".h", {0, 16})
              .Case(".s", {0, 32})
              .Case(".d", {0, 64})
              .Case(".q", {0, 128})
              .Default({-1, -1});
    break;
  default:
    llvm_unreachable("Unsupported RegKind");
  }

  if (Res == std::make_pair(-1, -1))
    return std::nullopt;

  return std::optional<std::pair<int, int>>(Res);
}

static bool isValidVectorKind(StringRef Suffix, RegKind VectorKind) {
  return parseVectorKind(Suffix, VectorKind).has_value();
}

static unsigned matchSVEDataVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("z0", AArch64::Z0)
      .Case("z1", AArch64::Z1)
      .Case("z2", AArch64::Z2)
      .Case("z3", AArch64::Z3)
      .Case("z4", AArch64::Z4)
      .Case("z5", AArch64::Z5)
      .Case("z6", AArch64::Z6)
      .Case("z7", AArch64::Z7)
      .Case("z8", AArch64::Z8)
      .Case("z9", AArch64::Z9)
      .Case("z10", AArch64::Z10)
      .Case("z11", AArch64::Z11)
      .Case("z12", AArch64::Z12)
      .Case("z13", AArch64::Z13)
      .Case("z14", AArch64::Z14)
      .Case("z15", AArch64::Z15)
      .Case("z16", AArch64::Z16)
      .Case("z17", AArch64::Z17)
      .Case("z18", AArch64::Z18)
      .Case("z19", AArch64::Z19)
      .Case("z20", AArch64::Z20)
      .Case("z21", AArch64::Z21)
      .Case("z22", AArch64::Z22)
      .Case("z23", AArch64::Z23)
      .Case("z24", AArch64::Z24)
      .Case("z25", AArch64::Z25)
      .Case("z26", AArch64::Z26)
      .Case("z27", AArch64::Z27)
      .Case("z28", AArch64::Z28)
      .Case("z29", AArch64::Z29)
      .Case("z30", AArch64::Z30)
      .Case("z31", AArch64::Z31)
      .Default(0);
}

static unsigned matchSVEPredicateVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("p0", AArch64::P0)
      .Case("p1", AArch64::P1)
      .Case("p2", AArch64::P2)
      .Case("p3", AArch64::P3)
      .Case("p4", AArch64::P4)
      .Case("p5", AArch64::P5)
      .Case("p6", AArch64::P6)
      .Case("p7", AArch64::P7)
      .Case("p8", AArch64::P8)
      .Case("p9", AArch64::P9)
      .Case("p10", AArch64::P10)
      .Case("p11", AArch64::P11)
      .Case("p12", AArch64::P12)
      .Case("p13", AArch64::P13)
      .Case("p14", AArch64::P14)
      .Case("p15", AArch64::P15)
      .Default(0);
}

static unsigned matchSVEPredicateAsCounterRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("pn0", AArch64::PN0)
      .Case("pn1", AArch64::PN1)
      .Case("pn2", AArch64::PN2)
      .Case("pn3", AArch64::PN3)
      .Case("pn4", AArch64::PN4)
      .Case("pn5", AArch64::PN5)
      .Case("pn6", AArch64::PN6)
      .Case("pn7", AArch64::PN7)
      .Case("pn8", AArch64::PN8)
      .Case("pn9", AArch64::PN9)
      .Case("pn10", AArch64::PN10)
      .Case("pn11", AArch64::PN11)
      .Case("pn12", AArch64::PN12)
      .Case("pn13", AArch64::PN13)
      .Case("pn14", AArch64::PN14)
      .Case("pn15", AArch64::PN15)
      .Default(0);
}

static unsigned matchMatrixTileListRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("za0.d", AArch64::ZAD0)
      .Case("za1.d", AArch64::ZAD1)
      .Case("za2.d", AArch64::ZAD2)
      .Case("za3.d", AArch64::ZAD3)
      .Case("za4.d", AArch64::ZAD4)
      .Case("za5.d", AArch64::ZAD5)
      .Case("za6.d", AArch64::ZAD6)
      .Case("za7.d", AArch64::ZAD7)
      .Case("za0.s", AArch64::ZAS0)
      .Case("za1.s", AArch64::ZAS1)
      .Case("za2.s", AArch64::ZAS2)
      .Case("za3.s", AArch64::ZAS3)
      .Case("za0.h", AArch64::ZAH0)
      .Case("za1.h", AArch64::ZAH1)
      .Case("za0.b", AArch64::ZAB0)
      .Default(0);
}

static unsigned matchMatrixRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("za", AArch64::ZA)
      .Case("za0.q", AArch64::ZAQ0)
      .Case("za1.q", AArch64::ZAQ1)
      .Case("za2.q", AArch64::ZAQ2)
      .Case("za3.q", AArch64::ZAQ3)
      .Case("za4.q", AArch64::ZAQ4)
      .Case("za5.q", AArch64::ZAQ5)
      .Case("za6.q", AArch64::ZAQ6)
      .Case("za7.q", AArch64::ZAQ7)
      .Case("za8.q", AArch64::ZAQ8)
      .Case("za9.q", AArch64::ZAQ9)
      .Case("za10.q", AArch64::ZAQ10)
      .Case("za11.q", AArch64::ZAQ11)
      .Case("za12.q", AArch64::ZAQ12)
      .Case("za13.q", AArch64::ZAQ13)
      .Case("za14.q", AArch64::ZAQ14)
      .Case("za15.q", AArch64::ZAQ15)
      .Case("za0.d", AArch64::ZAD0)
      .Case("za1.d", AArch64::ZAD1)
      .Case("za2.d", AArch64::ZAD2)
      .Case("za3.d", AArch64::ZAD3)
      .Case("za4.d", AArch64::ZAD4)
      .Case("za5.d", AArch64::ZAD5)
      .Case("za6.d", AArch64::ZAD6)
      .Case("za7.d", AArch64::ZAD7)
      .Case("za0.s", AArch64::ZAS0)
      .Case("za1.s", AArch64::ZAS1)
      .Case("za2.s", AArch64::ZAS2)
      .Case("za3.s", AArch64::ZAS3)
      .Case("za0.h", AArch64::ZAH0)
      .Case("za1.h", AArch64::ZAH1)
      .Case("za0.b", AArch64::ZAB0)
      .Case("za0h.q", AArch64::ZAQ0)
      .Case("za1h.q", AArch64::ZAQ1)
      .Case("za2h.q", AArch64::ZAQ2)
      .Case("za3h.q", AArch64::ZAQ3)
      .Case("za4h.q", AArch64::ZAQ4)
      .Case("za5h.q", AArch64::ZAQ5)
      .Case("za6h.q", AArch64::ZAQ6)
      .Case("za7h.q", AArch64::ZAQ7)
      .Case("za8h.q", AArch64::ZAQ8)
      .Case("za9h.q", AArch64::ZAQ9)
      .Case("za10h.q", AArch64::ZAQ10)
      .Case("za11h.q", AArch64::ZAQ11)
      .Case("za12h.q", AArch64::ZAQ12)
      .Case("za13h.q", AArch64::ZAQ13)
      .Case("za14h.q", AArch64::ZAQ14)
      .Case("za15h.q", AArch64::ZAQ15)
      .Case("za0h.d", AArch64::ZAD0)
      .Case("za1h.d", AArch64::ZAD1)
      .Case("za2h.d", AArch64::ZAD2)
      .Case("za3h.d", AArch64::ZAD3)
      .Case("za4h.d", AArch64::ZAD4)
      .Case("za5h.d", AArch64::ZAD5)
      .Case("za6h.d", AArch64::ZAD6)
      .Case("za7h.d", AArch64::ZAD7)
      .Case("za0h.s", AArch64::ZAS0)
      .Case("za1h.s", AArch64::ZAS1)
      .Case("za2h.s", AArch64::ZAS2)
      .Case("za3h.s", AArch64::ZAS3)
      .Case("za0h.h", AArch64::ZAH0)
      .Case("za1h.h", AArch64::ZAH1)
      .Case("za0h.b", AArch64::ZAB0)
      .Case("za0v.q", AArch64::ZAQ0)
      .Case("za1v.q", AArch64::ZAQ1)
      .Case("za2v.q", AArch64::ZAQ2)
      .Case("za3v.q", AArch64::ZAQ3)
      .Case("za4v.q", AArch64::ZAQ4)
      .Case("za5v.q", AArch64::ZAQ5)
      .Case("za6v.q", AArch64::ZAQ6)
      .Case("za7v.q", AArch64::ZAQ7)
      .Case("za8v.q", AArch64::ZAQ8)
      .Case("za9v.q", AArch64::ZAQ9)
      .Case("za10v.q", AArch64::ZAQ10)
      .Case("za11v.q", AArch64::ZAQ11)
      .Case("za12v.q", AArch64::ZAQ12)
      .Case("za13v.q", AArch64::ZAQ13)
      .Case("za14v.q", AArch64::ZAQ14)
      .Case("za15v.q", AArch64::ZAQ15)
      .Case("za0v.d", AArch64::ZAD0)
      .Case("za1v.d", AArch64::ZAD1)
      .Case("za2v.d", AArch64::ZAD2)
      .Case("za3v.d", AArch64::ZAD3)
      .Case("za4v.d", AArch64::ZAD4)
      .Case("za5v.d", AArch64::ZAD5)
      .Case("za6v.d", AArch64::ZAD6)
      .Case("za7v.d", AArch64::ZAD7)
      .Case("za0v.s", AArch64::ZAS0)
      .Case("za1v.s", AArch64::ZAS1)
      .Case("za2v.s", AArch64::ZAS2)
      .Case("za3v.s", AArch64::ZAS3)
      .Case("za0v.h", AArch64::ZAH0)
      .Case("za1v.h", AArch64::ZAH1)
      .Case("za0v.b", AArch64::ZAB0)
      .Default(0);
}

bool AArch64AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                     SMLoc &EndLoc) {
  return !tryParseRegister(Reg, StartLoc, EndLoc).isSuccess();
}

ParseStatus AArch64AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                               SMLoc &EndLoc) {
  StartLoc = getLoc();
  ParseStatus Res = tryParseScalarRegister(Reg);
  EndLoc = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  return Res;
}

// Matches a register name or register alias previously defined by '.req'
unsigned AArch64AsmParser::matchRegisterNameAlias(StringRef Name,
                                                  RegKind Kind) {
  unsigned RegNum = 0;
  if ((RegNum = matchSVEDataVectorRegName(Name)))
    return Kind == RegKind::SVEDataVector ? RegNum : 0;

  if ((RegNum = matchSVEPredicateVectorRegName(Name)))
    return Kind == RegKind::SVEPredicateVector ? RegNum : 0;

  if ((RegNum = matchSVEPredicateAsCounterRegName(Name)))
    return Kind == RegKind::SVEPredicateAsCounter ? RegNum : 0;

  if ((RegNum = MatchNeonVectorRegName(Name)))
    return Kind == RegKind::NeonVector ? RegNum : 0;

  if ((RegNum = matchMatrixRegName(Name)))
    return Kind == RegKind::Matrix ? RegNum : 0;

 if (Name.equals_insensitive("zt0"))
    return Kind == RegKind::LookupTable ? AArch64::ZT0 : 0;

  // The parsed register must be of RegKind Scalar
  if ((RegNum = MatchRegisterName(Name)))
    return (Kind == RegKind::Scalar) ? RegNum : 0;

  if (!RegNum) {
    // Handle a few common aliases of registers.
    if (auto RegNum = StringSwitch<unsigned>(Name.lower())
                    .Case("fp", AArch64::FP)
                    .Case("lr",  AArch64::LR)
                    .Case("x31", AArch64::XZR)
                    .Case("w31", AArch64::WZR)
                    .Default(0))
      return Kind == RegKind::Scalar ? RegNum : 0;

    // Check for aliases registered via .req. Canonicalize to lower case.
    // That's more consistent since register names are case insensitive, and
    // it's how the original entry was passed in from MC/MCParser/AsmParser.
    auto Entry = RegisterReqs.find(Name.lower());
    if (Entry == RegisterReqs.end())
      return 0;

    // set RegNum if the match is the right kind of register
    if (Kind == Entry->getValue().first)
      RegNum = Entry->getValue().second;
  }
  return RegNum;
}

unsigned AArch64AsmParser::getNumRegsForRegKind(RegKind K) {
  switch (K) {
  case RegKind::Scalar:
  case RegKind::NeonVector:
  case RegKind::SVEDataVector:
    return 32;
  case RegKind::Matrix:
  case RegKind::SVEPredicateVector:
  case RegKind::SVEPredicateAsCounter:
    return 16;
  case RegKind::LookupTable:
    return 1;
  }
  llvm_unreachable("Unsupported RegKind");
}

/// tryParseScalarRegister - Try to parse a register name. The token must be an
/// Identifier when called, and if it is a register name the token is eaten and
/// the register is added to the operand list.
ParseStatus AArch64AsmParser::tryParseScalarRegister(MCRegister &RegNum) {
  const AsmToken &Tok = getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  std::string lowerCase = Tok.getString().lower();
  unsigned Reg = matchRegisterNameAlias(lowerCase, RegKind::Scalar);
  if (Reg == 0)
    return ParseStatus::NoMatch;

  RegNum = Reg;
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

/// tryParseSysCROperand - Try to parse a system instruction CR operand name.
ParseStatus AArch64AsmParser::tryParseSysCROperand(OperandVector &Operands) {
  SMLoc S = getLoc();

  if (getTok().isNot(AsmToken::Identifier))
    return Error(S, "Expected cN operand where 0 <= N <= 15");

  StringRef Tok = getTok().getIdentifier();
  if (Tok[0] != 'c' && Tok[0] != 'C')
    return Error(S, "Expected cN operand where 0 <= N <= 15");

  uint32_t CRNum;
  bool BadNum = Tok.drop_front().getAsInteger(10, CRNum);
  if (BadNum || CRNum > 15)
    return Error(S, "Expected cN operand where 0 <= N <= 15");

  Lex(); // Eat identifier token.
  Operands.push_back(
      AArch64Operand::CreateSysCR(CRNum, S, getLoc(), getContext()));
  return ParseStatus::Success;
}

// Either an identifier for named values or a 6-bit immediate.
ParseStatus AArch64AsmParser::tryParseRPRFMOperand(OperandVector &Operands) {
  SMLoc S = getLoc();
  const AsmToken &Tok = getTok();

  unsigned MaxVal = 63;

  // Immediate case, with optional leading hash:
  if (parseOptionalToken(AsmToken::Hash) ||
      Tok.is(AsmToken::Integer)) {
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return ParseStatus::Failure;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return TokError("immediate value expected for prefetch operand");
    unsigned prfop = MCE->getValue();
    if (prfop > MaxVal)
      return TokError("prefetch operand out of range, [0," + utostr(MaxVal) +
                      "] expected");

    auto RPRFM = AArch64RPRFM::lookupRPRFMByEncoding(MCE->getValue());
    Operands.push_back(AArch64Operand::CreatePrefetch(
        prfop, RPRFM ? RPRFM->Name : "", S, getContext()));
    return ParseStatus::Success;
  }

  if (Tok.isNot(AsmToken::Identifier))
    return TokError("prefetch hint expected");

  auto RPRFM = AArch64RPRFM::lookupRPRFMByName(Tok.getString());
  if (!RPRFM)
    return TokError("prefetch hint expected");

  Operands.push_back(AArch64Operand::CreatePrefetch(
      RPRFM->Encoding, Tok.getString(), S, getContext()));
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

/// tryParsePrefetch - Try to parse a prefetch operand.
template <bool IsSVEPrefetch>
ParseStatus AArch64AsmParser::tryParsePrefetch(OperandVector &Operands) {
  SMLoc S = getLoc();
  const AsmToken &Tok = getTok();

  auto LookupByName = [](StringRef N) {
    if (IsSVEPrefetch) {
      if (auto Res = AArch64SVEPRFM::lookupSVEPRFMByName(N))
        return std::optional<unsigned>(Res->Encoding);
    } else if (auto Res = AArch64PRFM::lookupPRFMByName(N))
      return std::optional<unsigned>(Res->Encoding);
    return std::optional<unsigned>();
  };

  auto LookupByEncoding = [](unsigned E) {
    if (IsSVEPrefetch) {
      if (auto Res = AArch64SVEPRFM::lookupSVEPRFMByEncoding(E))
        return std::optional<StringRef>(Res->Name);
    } else if (auto Res = AArch64PRFM::lookupPRFMByEncoding(E))
      return std::optional<StringRef>(Res->Name);
    return std::optional<StringRef>();
  };
  unsigned MaxVal = IsSVEPrefetch ? 15 : 31;

  // Either an identifier for named values or a 5-bit immediate.
  // Eat optional hash.
  if (parseOptionalToken(AsmToken::Hash) ||
      Tok.is(AsmToken::Integer)) {
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return ParseStatus::Failure;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return TokError("immediate value expected for prefetch operand");
    unsigned prfop = MCE->getValue();
    if (prfop > MaxVal)
      return TokError("prefetch operand out of range, [0," + utostr(MaxVal) +
                      "] expected");

    auto PRFM = LookupByEncoding(MCE->getValue());
    Operands.push_back(AArch64Operand::CreatePrefetch(prfop, PRFM.value_or(""),
                                                      S, getContext()));
    return ParseStatus::Success;
  }

  if (Tok.isNot(AsmToken::Identifier))
    return TokError("prefetch hint expected");

  auto PRFM = LookupByName(Tok.getString());
  if (!PRFM)
    return TokError("prefetch hint expected");

  Operands.push_back(AArch64Operand::CreatePrefetch(
      *PRFM, Tok.getString(), S, getContext()));
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

/// tryParsePSBHint - Try to parse a PSB operand, mapped to Hint command
ParseStatus AArch64AsmParser::tryParsePSBHint(OperandVector &Operands) {
  SMLoc S = getLoc();
  const AsmToken &Tok = getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return TokError("invalid operand for instruction");

  auto PSB = AArch64PSBHint::lookupPSBByName(Tok.getString());
  if (!PSB)
    return TokError("invalid operand for instruction");

  Operands.push_back(AArch64Operand::CreatePSBHint(
      PSB->Encoding, Tok.getString(), S, getContext()));
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseSyspXzrPair(OperandVector &Operands) {
  SMLoc StartLoc = getLoc();

  MCRegister RegNum;

  // The case where xzr, xzr is not present is handled by an InstAlias.

  auto RegTok = getTok(); // in case we need to backtrack
  if (!tryParseScalarRegister(RegNum).isSuccess())
    return ParseStatus::NoMatch;

  if (RegNum != AArch64::XZR) {
    getLexer().UnLex(RegTok);
    return ParseStatus::NoMatch;
  }

  if (parseComma())
    return ParseStatus::Failure;

  if (!tryParseScalarRegister(RegNum).isSuccess())
    return TokError("expected register operand");

  if (RegNum != AArch64::XZR)
    return TokError("xzr must be followed by xzr");

  // We need to push something, since we claim this is an operand in .td.
  // See also AArch64AsmParser::parseKeywordOperand.
  Operands.push_back(AArch64Operand::CreateReg(
      RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext()));

  return ParseStatus::Success;
}

/// tryParseBTIHint - Try to parse a BTI operand, mapped to Hint command
ParseStatus AArch64AsmParser::tryParseBTIHint(OperandVector &Operands) {
  SMLoc S = getLoc();
  const AsmToken &Tok = getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return TokError("invalid operand for instruction");

  auto BTI = AArch64BTIHint::lookupBTIByName(Tok.getString());
  if (!BTI)
    return TokError("invalid operand for instruction");

  Operands.push_back(AArch64Operand::CreateBTIHint(
      BTI->Encoding, Tok.getString(), S, getContext()));
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

/// tryParseAdrpLabel - Parse and validate a source label for the ADRP
/// instruction.
ParseStatus AArch64AsmParser::tryParseAdrpLabel(OperandVector &Operands) {
  SMLoc S = getLoc();
  const MCExpr *Expr = nullptr;

  if (getTok().is(AsmToken::Hash)) {
    Lex(); // Eat hash token.
  }

  if (parseSymbolicImmVal(Expr))
    return ParseStatus::Failure;

  AArch64MCExpr::VariantKind ELFRefKind;
  MCSymbolRefExpr::VariantKind DarwinRefKind;
  int64_t Addend;
  if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {
    if (DarwinRefKind == MCSymbolRefExpr::VK_None &&
        ELFRefKind == AArch64MCExpr::VK_INVALID) {
      // No modifier was specified at all; this is the syntax for an ELF basic
      // ADRP relocation (unfortunately).
      Expr =
          AArch64MCExpr::create(Expr, AArch64MCExpr::VK_ABS_PAGE, getContext());
    } else if ((DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGE ||
                DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGE) &&
               Addend != 0) {
      return Error(S, "gotpage label reference not allowed an addend");
    } else if (DarwinRefKind != MCSymbolRefExpr::VK_PAGE &&
               DarwinRefKind != MCSymbolRefExpr::VK_GOTPAGE &&
               DarwinRefKind != MCSymbolRefExpr::VK_TLVPPAGE &&
               ELFRefKind != AArch64MCExpr::VK_ABS_PAGE_NC &&
               ELFRefKind != AArch64MCExpr::VK_GOT_PAGE &&
               ELFRefKind != AArch64MCExpr::VK_GOT_PAGE_LO15 &&
               ELFRefKind != AArch64MCExpr::VK_GOTTPREL_PAGE &&
               ELFRefKind != AArch64MCExpr::VK_TLSDESC_PAGE) {
      // The operand must be an @page or @gotpage qualified symbolref.
      return Error(S, "page or gotpage label reference expected");
    }
  }

  // We have either a label reference possibly with addend or an immediate. The
  // addend is a raw value here. The linker will adjust it to only reference the
  // page.
  SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));

  return ParseStatus::Success;
}

/// tryParseAdrLabel - Parse and validate a source label for the ADR
/// instruction.
ParseStatus AArch64AsmParser::tryParseAdrLabel(OperandVector &Operands) {
  SMLoc S = getLoc();
  const MCExpr *Expr = nullptr;

  // Leave anything with a bracket to the default for SVE
  if (getTok().is(AsmToken::LBrac))
    return ParseStatus::NoMatch;

  if (getTok().is(AsmToken::Hash))
    Lex(); // Eat hash token.

  if (parseSymbolicImmVal(Expr))
    return ParseStatus::Failure;

  AArch64MCExpr::VariantKind ELFRefKind;
  MCSymbolRefExpr::VariantKind DarwinRefKind;
  int64_t Addend;
  if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {
    if (DarwinRefKind == MCSymbolRefExpr::VK_None &&
        ELFRefKind == AArch64MCExpr::VK_INVALID) {
      // No modifier was specified at all; this is the syntax for an ELF basic
      // ADR relocation (unfortunately).
      Expr = AArch64MCExpr::create(Expr, AArch64MCExpr::VK_ABS, getContext());
    } else {
      return Error(S, "unexpected adr label");
    }
  }

  SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));
  return ParseStatus::Success;
}

/// tryParseFPImm - A floating point immediate expression operand.
template <bool AddFPZeroAsLiteral>
ParseStatus AArch64AsmParser::tryParseFPImm(OperandVector &Operands) {
  SMLoc S = getLoc();

  bool Hash = parseOptionalToken(AsmToken::Hash);

  // Handle negation, as that still comes through as a separate token.
  bool isNegative = parseOptionalToken(AsmToken::Minus);

  const AsmToken &Tok = getTok();
  if (!Tok.is(AsmToken::Real) && !Tok.is(AsmToken::Integer)) {
    if (!Hash)
      return ParseStatus::NoMatch;
    return TokError("invalid floating point immediate");
  }

  // Parse hexadecimal representation.
  if (Tok.is(AsmToken::Integer) && Tok.getString().starts_with("0x")) {
    if (Tok.getIntVal() > 255 || isNegative)
      return TokError("encoded floating point value out of range");

    APFloat F((double)AArch64_AM::getFPImmFloat(Tok.getIntVal()));
    Operands.push_back(
        AArch64Operand::CreateFPImm(F, true, S, getContext()));
  } else {
    // Parse FP representation.
    APFloat RealVal(APFloat::IEEEdouble());
    auto StatusOrErr =
        RealVal.convertFromString(Tok.getString(), APFloat::rmTowardZero);
    if (errorToBool(StatusOrErr.takeError()))
      return TokError("invalid floating point representation");

    if (isNegative)
      RealVal.changeSign();

    if (AddFPZeroAsLiteral && RealVal.isPosZero()) {
      Operands.push_back(AArch64Operand::CreateToken("#0", S, getContext()));
      Operands.push_back(AArch64Operand::CreateToken(".0", S, getContext()));
    } else
      Operands.push_back(AArch64Operand::CreateFPImm(
          RealVal, *StatusOrErr == APFloat::opOK, S, getContext()));
  }

  Lex(); // Eat the token.

  return ParseStatus::Success;
}

/// tryParseImmWithOptionalShift - Parse immediate operand, optionally with
/// a shift suffix, for example '#1, lsl #12'.
ParseStatus
AArch64AsmParser::tryParseImmWithOptionalShift(OperandVector &Operands) {
  SMLoc S = getLoc();

  if (getTok().is(AsmToken::Hash))
    Lex(); // Eat '#'
  else if (getTok().isNot(AsmToken::Integer))
    // Operand should start from # or should be integer, emit error otherwise.
    return ParseStatus::NoMatch;

  if (getTok().is(AsmToken::Integer) &&
      getLexer().peekTok().is(AsmToken::Colon))
    return tryParseImmRange(Operands);

  const MCExpr *Imm = nullptr;
  if (parseSymbolicImmVal(Imm))
    return ParseStatus::Failure;
  else if (getTok().isNot(AsmToken::Comma)) {
    Operands.push_back(
        AArch64Operand::CreateImm(Imm, S, getLoc(), getContext()));
    return ParseStatus::Success;
  }

  // Eat ','
  Lex();
  StringRef VecGroup;
  if (!parseOptionalVGOperand(Operands, VecGroup)) {
    Operands.push_back(
        AArch64Operand::CreateImm(Imm, S, getLoc(), getContext()));
    Operands.push_back(
        AArch64Operand::CreateToken(VecGroup, getLoc(), getContext()));
    return ParseStatus::Success;
  }

  // The optional operand must be "lsl #N" where N is non-negative.
  if (!getTok().is(AsmToken::Identifier) ||
      !getTok().getIdentifier().equals_insensitive("lsl"))
    return Error(getLoc(), "only 'lsl #+N' valid after immediate");

  // Eat 'lsl'
  Lex();

  parseOptionalToken(AsmToken::Hash);

  if (getTok().isNot(AsmToken::Integer))
    return Error(getLoc(), "only 'lsl #+N' valid after immediate");

  int64_t ShiftAmount = getTok().getIntVal();

  if (ShiftAmount < 0)
    return Error(getLoc(), "positive shift amount required");
  Lex(); // Eat the number

  // Just in case the optional lsl #0 is used for immediates other than zero.
  if (ShiftAmount == 0 && Imm != nullptr) {
    Operands.push_back(
        AArch64Operand::CreateImm(Imm, S, getLoc(), getContext()));
    return ParseStatus::Success;
  }

  Operands.push_back(AArch64Operand::CreateShiftedImm(Imm, ShiftAmount, S,
                                                      getLoc(), getContext()));
  return ParseStatus::Success;
}

/// parseCondCodeString - Parse a Condition Code string, optionally returning a
/// suggestion to help common typos.
AArch64CC::CondCode
AArch64AsmParser::parseCondCodeString(StringRef Cond, std::string &Suggestion) {
  AArch64CC::CondCode CC = StringSwitch<AArch64CC::CondCode>(Cond.lower())
                    .Case("eq", AArch64CC::EQ)
                    .Case("ne", AArch64CC::NE)
                    .Case("cs", AArch64CC::HS)
                    .Case("hs", AArch64CC::HS)
                    .Case("cc", AArch64CC::LO)
                    .Case("lo", AArch64CC::LO)
                    .Case("mi", AArch64CC::MI)
                    .Case("pl", AArch64CC::PL)
                    .Case("vs", AArch64CC::VS)
                    .Case("vc", AArch64CC::VC)
                    .Case("hi", AArch64CC::HI)
                    .Case("ls", AArch64CC::LS)
                    .Case("ge", AArch64CC::GE)
                    .Case("lt", AArch64CC::LT)
                    .Case("gt", AArch64CC::GT)
                    .Case("le", AArch64CC::LE)
                    .Case("al", AArch64CC::AL)
                    .Case("nv", AArch64CC::NV)
                    .Default(AArch64CC::Invalid);

  if (CC == AArch64CC::Invalid && getSTI().hasFeature(AArch64::FeatureSVE)) {
    CC = StringSwitch<AArch64CC::CondCode>(Cond.lower())
                    .Case("none",  AArch64CC::EQ)
                    .Case("any",   AArch64CC::NE)
                    .Case("nlast", AArch64CC::HS)
                    .Case("last",  AArch64CC::LO)
                    .Case("first", AArch64CC::MI)
                    .Case("nfrst", AArch64CC::PL)
                    .Case("pmore", AArch64CC::HI)
                    .Case("plast", AArch64CC::LS)
                    .Case("tcont", AArch64CC::GE)
                    .Case("tstop", AArch64CC::LT)
                    .Default(AArch64CC::Invalid);

    if (CC == AArch64CC::Invalid && Cond.lower() == "nfirst")
      Suggestion = "nfrst";
  }
  return CC;
}

/// parseCondCode - Parse a Condition Code operand.
bool AArch64AsmParser::parseCondCode(OperandVector &Operands,
                                     bool invertCondCode) {
  SMLoc S = getLoc();
  const AsmToken &Tok = getTok();
  assert(Tok.is(AsmToken::Identifier) && "Token is not an Identifier");

  StringRef Cond = Tok.getString();
  std::string Suggestion;
  AArch64CC::CondCode CC = parseCondCodeString(Cond, Suggestion);
  if (CC == AArch64CC::Invalid) {
    std::string Msg = "invalid condition code";
    if (!Suggestion.empty())
      Msg += ", did you mean " + Suggestion + "?";
    return TokError(Msg);
  }
  Lex(); // Eat identifier token.

  if (invertCondCode) {
    if (CC == AArch64CC::AL || CC == AArch64CC::NV)
      return TokError("condition codes AL and NV are invalid for this instruction");
    CC = AArch64CC::getInvertedCondCode(AArch64CC::CondCode(CC));
  }

  Operands.push_back(
      AArch64Operand::CreateCondCode(CC, S, getLoc(), getContext()));
  return false;
}

ParseStatus AArch64AsmParser::tryParseSVCR(OperandVector &Operands) {
  const AsmToken &Tok = getTok();
  SMLoc S = getLoc();

  if (Tok.isNot(AsmToken::Identifier))
    return TokError("invalid operand for instruction");

  unsigned PStateImm = -1;
  const auto *SVCR = AArch64SVCR::lookupSVCRByName(Tok.getString());
  if (!SVCR)
    return ParseStatus::NoMatch;
  if (SVCR->haveFeatures(getSTI().getFeatureBits()))
    PStateImm = SVCR->Encoding;

  Operands.push_back(
      AArch64Operand::CreateSVCR(PStateImm, Tok.getString(), S, getContext()));
  Lex(); // Eat identifier token.
  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseMatrixRegister(OperandVector &Operands) {
  const AsmToken &Tok = getTok();
  SMLoc S = getLoc();

  StringRef Name = Tok.getString();

  if (Name.equals_insensitive("za") || Name.starts_with_insensitive("za.")) {
    Lex(); // eat "za[.(b|h|s|d)]"
    unsigned ElementWidth = 0;
    auto DotPosition = Name.find('.');
    if (DotPosition != StringRef::npos) {
      const auto &KindRes =
          parseVectorKind(Name.drop_front(DotPosition), RegKind::Matrix);
      if (!KindRes)
        return TokError(
            "Expected the register to be followed by element width suffix");
      ElementWidth = KindRes->second;
    }
    Operands.push_back(AArch64Operand::CreateMatrixRegister(
        AArch64::ZA, ElementWidth, MatrixKind::Array, S, getLoc(),
        getContext()));
    if (getLexer().is(AsmToken::LBrac)) {
      // There's no comma after matrix operand, so we can parse the next operand
      // immediately.
      if (parseOperand(Operands, false, false))
        return ParseStatus::NoMatch;
    }
    return ParseStatus::Success;
  }

  // Try to parse matrix register.
  unsigned Reg = matchRegisterNameAlias(Name, RegKind::Matrix);
  if (!Reg)
    return ParseStatus::NoMatch;

  size_t DotPosition = Name.find('.');
  assert(DotPosition != StringRef::npos && "Unexpected register");

  StringRef Head = Name.take_front(DotPosition);
  StringRef Tail = Name.drop_front(DotPosition);
  StringRef RowOrColumn = Head.take_back();

  MatrixKind Kind = StringSwitch<MatrixKind>(RowOrColumn.lower())
                        .Case("h", MatrixKind::Row)
                        .Case("v", MatrixKind::Col)
                        .Default(MatrixKind::Tile);

  // Next up, parsing the suffix
  const auto &KindRes = parseVectorKind(Tail, RegKind::Matrix);
  if (!KindRes)
    return TokError(
        "Expected the register to be followed by element width suffix");
  unsigned ElementWidth = KindRes->second;

  Lex();

  Operands.push_back(AArch64Operand::CreateMatrixRegister(
      Reg, ElementWidth, Kind, S, getLoc(), getContext()));

  if (getLexer().is(AsmToken::LBrac)) {
    // There's no comma after matrix operand, so we can parse the next operand
    // immediately.
    if (parseOperand(Operands, false, false))
      return ParseStatus::NoMatch;
  }
  return ParseStatus::Success;
}

/// tryParseOptionalShift - Some operands take an optional shift argument. Parse
/// them if present.
ParseStatus
AArch64AsmParser::tryParseOptionalShiftExtend(OperandVector &Operands) {
  const AsmToken &Tok = getTok();
  std::string LowerID = Tok.getString().lower();
  AArch64_AM::ShiftExtendType ShOp =
      StringSwitch<AArch64_AM::ShiftExtendType>(LowerID)
          .Case("lsl", AArch64_AM::LSL)
          .Case("lsr", AArch64_AM::LSR)
          .Case("asr", AArch64_AM::ASR)
          .Case("ror", AArch64_AM::ROR)
          .Case("msl", AArch64_AM::MSL)
          .Case("uxtb", AArch64_AM::UXTB)
          .Case("uxth", AArch64_AM::UXTH)
          .Case("uxtw", AArch64_AM::UXTW)
          .Case("uxtx", AArch64_AM::UXTX)
          .Case("sxtb", AArch64_AM::SXTB)
          .Case("sxth", AArch64_AM::SXTH)
          .Case("sxtw", AArch64_AM::SXTW)
          .Case("sxtx", AArch64_AM::SXTX)
          .Default(AArch64_AM::InvalidShiftExtend);

  if (ShOp == AArch64_AM::InvalidShiftExtend)
    return ParseStatus::NoMatch;

  SMLoc S = Tok.getLoc();
  Lex();

  bool Hash = parseOptionalToken(AsmToken::Hash);

  if (!Hash && getLexer().isNot(AsmToken::Integer)) {
    if (ShOp == AArch64_AM::LSL || ShOp == AArch64_AM::LSR ||
        ShOp == AArch64_AM::ASR || ShOp == AArch64_AM::ROR ||
        ShOp == AArch64_AM::MSL) {
      // We expect a number here.
      return TokError("expected #imm after shift specifier");
    }

    // "extend" type operations don't need an immediate, #0 is implicit.
    SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(
        AArch64Operand::CreateShiftExtend(ShOp, 0, false, S, E, getContext()));
    return ParseStatus::Success;
  }

  // Make sure we do actually have a number, identifier or a parenthesized
  // expression.
  SMLoc E = getLoc();
  if (!getTok().is(AsmToken::Integer) && !getTok().is(AsmToken::LParen) &&
      !getTok().is(AsmToken::Identifier))
    return Error(E, "expected integer shift amount");

  const MCExpr *ImmVal;
  if (getParser().parseExpression(ImmVal))
    return ParseStatus::Failure;

  const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
  if (!MCE)
    return Error(E, "expected constant '#imm' after shift specifier");

  E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateShiftExtend(
      ShOp, MCE->getValue(), true, S, E, getContext()));
  return ParseStatus::Success;
}

static const struct Extension {
  const char *Name;
  const FeatureBitset Features;
} ExtensionMap[] = {
    {"crc", {AArch64::FeatureCRC}},
    {"sm4", {AArch64::FeatureSM4}},
    {"sha3", {AArch64::FeatureSHA3}},
    {"sha2", {AArch64::FeatureSHA2}},
    {"aes", {AArch64::FeatureAES}},
    {"crypto", {AArch64::FeatureCrypto}},
    {"fp", {AArch64::FeatureFPARMv8}},
    {"simd", {AArch64::FeatureNEON}},
    {"ras", {AArch64::FeatureRAS}},
    {"rasv2", {AArch64::FeatureRASv2}},
    {"lse", {AArch64::FeatureLSE}},
    {"predres", {AArch64::FeaturePredRes}},
    {"predres2", {AArch64::FeatureSPECRES2}},
    {"ccdp", {AArch64::FeatureCacheDeepPersist}},
    {"mte", {AArch64::FeatureMTE}},
    {"memtag", {AArch64::FeatureMTE}},
    {"tlb-rmi", {AArch64::FeatureTLB_RMI}},
    {"pan", {AArch64::FeaturePAN}},
    {"pan-rwv", {AArch64::FeaturePAN_RWV}},
    {"ccpp", {AArch64::FeatureCCPP}},
    {"rcpc", {AArch64::FeatureRCPC}},
    {"rng", {AArch64::FeatureRandGen}},
    {"sve", {AArch64::FeatureSVE}},
    {"sve2", {AArch64::FeatureSVE2}},
    {"sve2-aes", {AArch64::FeatureSVE2AES}},
    {"sve2-sm4", {AArch64::FeatureSVE2SM4}},
    {"sve2-sha3", {AArch64::FeatureSVE2SHA3}},
    {"sve2-bitperm", {AArch64::FeatureSVE2BitPerm}},
    {"sve2p1", {AArch64::FeatureSVE2p1}},
    {"b16b16", {AArch64::FeatureB16B16}},
    {"ls64", {AArch64::FeatureLS64}},
    {"xs", {AArch64::FeatureXS}},
    {"pauth", {AArch64::FeaturePAuth}},
    {"flagm", {AArch64::FeatureFlagM}},
    {"rme", {AArch64::FeatureRME}},
    {"sme", {AArch64::FeatureSME}},
    {"sme-f64f64", {AArch64::FeatureSMEF64F64}},
    {"sme-f16f16", {AArch64::FeatureSMEF16F16}},
    {"sme-i16i64", {AArch64::FeatureSMEI16I64}},
    {"sme2", {AArch64::FeatureSME2}},
    {"sme2p1", {AArch64::FeatureSME2p1}},
    {"hbc", {AArch64::FeatureHBC}},
    {"mops", {AArch64::FeatureMOPS}},
    {"mec", {AArch64::FeatureMEC}},
    {"the", {AArch64::FeatureTHE}},
    {"d128", {AArch64::FeatureD128}},
    {"lse128", {AArch64::FeatureLSE128}},
    {"ite", {AArch64::FeatureITE}},
    {"cssc", {AArch64::FeatureCSSC}},
    {"rcpc3", {AArch64::FeatureRCPC3}},
    {"gcs", {AArch64::FeatureGCS}},
    {"bf16", {AArch64::FeatureBF16}},
    {"compnum", {AArch64::FeatureComplxNum}},
    {"dotprod", {AArch64::FeatureDotProd}},
    {"f32mm", {AArch64::FeatureMatMulFP32}},
    {"f64mm", {AArch64::FeatureMatMulFP64}},
    {"fp16", {AArch64::FeatureFullFP16}},
    {"fp16fml", {AArch64::FeatureFP16FML}},
    {"i8mm", {AArch64::FeatureMatMulInt8}},
    {"lor", {AArch64::FeatureLOR}},
    {"profile", {AArch64::FeatureSPE}},
    // "rdma" is the name documented by binutils for the feature, but
    // binutils also accepts incomplete prefixes of features, so "rdm"
    // works too. Support both spellings here.
    {"rdm", {AArch64::FeatureRDM}},
    {"rdma", {AArch64::FeatureRDM}},
    {"sb", {AArch64::FeatureSB}},
    {"ssbs", {AArch64::FeatureSSBS}},
    {"tme", {AArch64::FeatureTME}},
    {"fp8", {AArch64::FeatureFP8}},
    {"faminmax", {AArch64::FeatureFAMINMAX}},
    {"fp8fma", {AArch64::FeatureFP8FMA}},
    {"ssve-fp8fma", {AArch64::FeatureSSVE_FP8FMA}},
    {"fp8dot2", {AArch64::FeatureFP8DOT2}},
    {"ssve-fp8dot2", {AArch64::FeatureSSVE_FP8DOT2}},
    {"fp8dot4", {AArch64::FeatureFP8DOT4}},
    {"ssve-fp8dot4", {AArch64::FeatureSSVE_FP8DOT4}},
    {"lut", {AArch64::FeatureLUT}},
    {"sme-lutv2", {AArch64::FeatureSME_LUTv2}},
    {"sme-f8f16", {AArch64::FeatureSMEF8F16}},
    {"sme-f8f32", {AArch64::FeatureSMEF8F32}},
    {"sme-fa64", {AArch64::FeatureSMEFA64}},
    {"cpa", {AArch64::FeatureCPA}},
    {"tlbiw", {AArch64::FeatureTLBIW}},
};

static void setRequiredFeatureString(FeatureBitset FBS, std::string &Str) {
  if (FBS[AArch64::HasV8_0aOps])
    Str += "ARMv8a";
  if (FBS[AArch64::HasV8_1aOps])
    Str += "ARMv8.1a";
  else if (FBS[AArch64::HasV8_2aOps])
    Str += "ARMv8.2a";
  else if (FBS[AArch64::HasV8_3aOps])
    Str += "ARMv8.3a";
  else if (FBS[AArch64::HasV8_4aOps])
    Str += "ARMv8.4a";
  else if (FBS[AArch64::HasV8_5aOps])
    Str += "ARMv8.5a";
  else if (FBS[AArch64::HasV8_6aOps])
    Str += "ARMv8.6a";
  else if (FBS[AArch64::HasV8_7aOps])
    Str += "ARMv8.7a";
  else if (FBS[AArch64::HasV8_8aOps])
    Str += "ARMv8.8a";
  else if (FBS[AArch64::HasV8_9aOps])
    Str += "ARMv8.9a";
  else if (FBS[AArch64::HasV9_0aOps])
    Str += "ARMv9-a";
  else if (FBS[AArch64::HasV9_1aOps])
    Str += "ARMv9.1a";
  else if (FBS[AArch64::HasV9_2aOps])
    Str += "ARMv9.2a";
  else if (FBS[AArch64::HasV9_3aOps])
    Str += "ARMv9.3a";
  else if (FBS[AArch64::HasV9_4aOps])
    Str += "ARMv9.4a";
  else if (FBS[AArch64::HasV9_5aOps])
    Str += "ARMv9.5a";
  else if (FBS[AArch64::HasV8_0rOps])
    Str += "ARMv8r";
  else {
    SmallVector<std::string, 2> ExtMatches;
    for (const auto& Ext : ExtensionMap) {
      // Use & in case multiple features are enabled
      if ((FBS & Ext.Features) != FeatureBitset())
        ExtMatches.push_back(Ext.Name);
    }
    Str += !ExtMatches.empty() ? llvm::join(ExtMatches, ", ") : "(unknown)";
  }
}

void AArch64AsmParser::createSysAlias(uint16_t Encoding, OperandVector &Operands,
                                      SMLoc S) {
  const uint16_t Op2 = Encoding & 7;
  const uint16_t Cm = (Encoding & 0x78) >> 3;
  const uint16_t Cn = (Encoding & 0x780) >> 7;
  const uint16_t Op1 = (Encoding & 0x3800) >> 11;

  const MCExpr *Expr = MCConstantExpr::create(Op1, getContext());

  Operands.push_back(
      AArch64Operand::CreateImm(Expr, S, getLoc(), getContext()));
  Operands.push_back(
      AArch64Operand::CreateSysCR(Cn, S, getLoc(), getContext()));
  Operands.push_back(
      AArch64Operand::CreateSysCR(Cm, S, getLoc(), getContext()));
  Expr = MCConstantExpr::create(Op2, getContext());
  Operands.push_back(
      AArch64Operand::CreateImm(Expr, S, getLoc(), getContext()));
}

/// parseSysAlias - The IC, DC, AT, and TLBI instructions are simple aliases for
/// the SYS instruction. Parse them specially so that we create a SYS MCInst.
bool AArch64AsmParser::parseSysAlias(StringRef Name, SMLoc NameLoc,
                                   OperandVector &Operands) {
  if (Name.contains('.'))
    return TokError("invalid operand");

  Mnemonic = Name;
  Operands.push_back(AArch64Operand::CreateToken("sys", NameLoc, getContext()));

  const AsmToken &Tok = getTok();
  StringRef Op = Tok.getString();
  SMLoc S = Tok.getLoc();

  if (Mnemonic == "ic") {
    const AArch64IC::IC *IC = AArch64IC::lookupICByName(Op);
    if (!IC)
      return TokError("invalid operand for IC instruction");
    else if (!IC->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("IC " + std::string(IC->Name) + " requires: ");
      setRequiredFeatureString(IC->getRequiredFeatures(), Str);
      return TokError(Str);
    }
    createSysAlias(IC->Encoding, Operands, S);
  } else if (Mnemonic == "dc") {
    const AArch64DC::DC *DC = AArch64DC::lookupDCByName(Op);
    if (!DC)
      return TokError("invalid operand for DC instruction");
    else if (!DC->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("DC " + std::string(DC->Name) + " requires: ");
      setRequiredFeatureString(DC->getRequiredFeatures(), Str);
      return TokError(Str);
    }
    createSysAlias(DC->Encoding, Operands, S);
  } else if (Mnemonic == "at") {
    const AArch64AT::AT *AT = AArch64AT::lookupATByName(Op);
    if (!AT)
      return TokError("invalid operand for AT instruction");
    else if (!AT->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("AT " + std::string(AT->Name) + " requires: ");
      setRequiredFeatureString(AT->getRequiredFeatures(), Str);
      return TokError(Str);
    }
    createSysAlias(AT->Encoding, Operands, S);
  } else if (Mnemonic == "tlbi") {
    const AArch64TLBI::TLBI *TLBI = AArch64TLBI::lookupTLBIByName(Op);
    if (!TLBI)
      return TokError("invalid operand for TLBI instruction");
    else if (!TLBI->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("TLBI " + std::string(TLBI->Name) + " requires: ");
      setRequiredFeatureString(TLBI->getRequiredFeatures(), Str);
      return TokError(Str);
    }
    createSysAlias(TLBI->Encoding, Operands, S);
  } else if (Mnemonic == "cfp" || Mnemonic == "dvp" || Mnemonic == "cpp" || Mnemonic == "cosp") {

    if (Op.lower() != "rctx")
      return TokError("invalid operand for prediction restriction instruction");

    bool hasAll = getSTI().hasFeature(AArch64::FeatureAll);
    bool hasPredres = hasAll || getSTI().hasFeature(AArch64::FeaturePredRes);
    bool hasSpecres2 = hasAll || getSTI().hasFeature(AArch64::FeatureSPECRES2);

    if (Mnemonic == "cosp" && !hasSpecres2)
      return TokError("COSP requires: predres2");
    if (!hasPredres)
      return TokError(Mnemonic.upper() + "RCTX requires: predres");

    uint16_t PRCTX_Op2 = Mnemonic == "cfp"    ? 0b100
                         : Mnemonic == "dvp"  ? 0b101
                         : Mnemonic == "cosp" ? 0b110
                         : Mnemonic == "cpp"  ? 0b111
                                              : 0;
    assert(PRCTX_Op2 &&
           "Invalid mnemonic for prediction restriction instruction");
    const auto SYS_3_7_3 = 0b01101110011; // op=3, CRn=7, CRm=3
    const auto Encoding = SYS_3_7_3 << 3 | PRCTX_Op2;

    createSysAlias(Encoding, Operands, S);
  }

  Lex(); // Eat operand.

  bool ExpectRegister = !Op.contains_insensitive("all");
  bool HasRegister = false;

  // Check for the optional register operand.
  if (parseOptionalToken(AsmToken::Comma)) {
    if (Tok.isNot(AsmToken::Identifier) || parseRegister(Operands))
      return TokError("expected register operand");
    HasRegister = true;
  }

  if (ExpectRegister && !HasRegister)
    return TokError("specified " + Mnemonic + " op requires a register");
  else if (!ExpectRegister && HasRegister)
    return TokError("specified " + Mnemonic + " op does not use a register");

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in argument list"))
    return true;

  return false;
}

/// parseSyspAlias - The TLBIP instructions are simple aliases for
/// the SYSP instruction. Parse them specially so that we create a SYSP MCInst.
bool AArch64AsmParser::parseSyspAlias(StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  if (Name.contains('.'))
    return TokError("invalid operand");

  Mnemonic = Name;
  Operands.push_back(
      AArch64Operand::CreateToken("sysp", NameLoc, getContext()));

  const AsmToken &Tok = getTok();
  StringRef Op = Tok.getString();
  SMLoc S = Tok.getLoc();

  if (Mnemonic == "tlbip") {
    bool HasnXSQualifier = Op.ends_with_insensitive("nXS");
    if (HasnXSQualifier) {
      Op = Op.drop_back(3);
    }
    const AArch64TLBI::TLBI *TLBIorig = AArch64TLBI::lookupTLBIByName(Op);
    if (!TLBIorig)
      return TokError("invalid operand for TLBIP instruction");
    const AArch64TLBI::TLBI TLBI(
        TLBIorig->Name, TLBIorig->Encoding | (HasnXSQualifier ? (1 << 7) : 0),
        TLBIorig->NeedsReg,
        HasnXSQualifier
            ? TLBIorig->FeaturesRequired | FeatureBitset({AArch64::FeatureXS})
            : TLBIorig->FeaturesRequired);
    if (!TLBI.haveFeatures(getSTI().getFeatureBits())) {
      std::string Name =
          std::string(TLBI.Name) + (HasnXSQualifier ? "nXS" : "");
      std::string Str("TLBIP " + Name + " requires: ");
      setRequiredFeatureString(TLBI.getRequiredFeatures(), Str);
      return TokError(Str);
    }
    createSysAlias(TLBI.Encoding, Operands, S);
  }

  Lex(); // Eat operand.

  if (parseComma())
    return true;

  if (Tok.isNot(AsmToken::Identifier))
    return TokError("expected register identifier");
  auto Result = tryParseSyspXzrPair(Operands);
  if (Result.isNoMatch())
    Result = tryParseGPRSeqPair(Operands);
  if (!Result.isSuccess())
    return TokError("specified " + Mnemonic +
                    " op requires a pair of registers");

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in argument list"))
    return true;

  return false;
}

ParseStatus AArch64AsmParser::tryParseBarrierOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = getTok();

  if (Mnemonic == "tsb" && Tok.isNot(AsmToken::Identifier))
    return TokError("'csync' operand expected");
  if (parseOptionalToken(AsmToken::Hash) || Tok.is(AsmToken::Integer)) {
    // Immediate operand.
    const MCExpr *ImmVal;
    SMLoc ExprLoc = getLoc();
    AsmToken IntTok = Tok;
    if (getParser().parseExpression(ImmVal))
      return ParseStatus::Failure;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return Error(ExprLoc, "immediate value expected for barrier operand");
    int64_t Value = MCE->getValue();
    if (Mnemonic == "dsb" && Value > 15) {
      // This case is a no match here, but it might be matched by the nXS
      // variant. Deliberately not unlex the optional '#' as it is not necessary
      // to characterize an integer immediate.
      Parser.getLexer().UnLex(IntTok);
      return ParseStatus::NoMatch;
    }
    if (Value < 0 || Value > 15)
      return Error(ExprLoc, "barrier operand out of range");
    auto DB = AArch64DB::lookupDBByEncoding(Value);
    Operands.push_back(AArch64Operand::CreateBarrier(Value, DB ? DB->Name : "",
                                                     ExprLoc, getContext(),
                                                     false /*hasnXSModifier*/));
    return ParseStatus::Success;
  }

  if (Tok.isNot(AsmToken::Identifier))
    return TokError("invalid operand for instruction");

  StringRef Operand = Tok.getString();
  auto TSB = AArch64TSB::lookupTSBByName(Operand);
  auto DB = AArch64DB::lookupDBByName(Operand);
  // The only valid named option for ISB is 'sy'
  if (Mnemonic == "isb" && (!DB || DB->Encoding != AArch64DB::sy))
    return TokError("'sy' or #imm operand expected");
  // The only valid named option for TSB is 'csync'
  if (Mnemonic == "tsb" && (!TSB || TSB->Encoding != AArch64TSB::csync))
    return TokError("'csync' operand expected");
  if (!DB && !TSB) {
    if (Mnemonic == "dsb") {
      // This case is a no match here, but it might be matched by the nXS
      // variant.
      return ParseStatus::NoMatch;
    }
    return TokError("invalid barrier option name");
  }

  Operands.push_back(AArch64Operand::CreateBarrier(
      DB ? DB->Encoding : TSB->Encoding, Tok.getString(), getLoc(),
      getContext(), false /*hasnXSModifier*/));
  Lex(); // Consume the option

  return ParseStatus::Success;
}

ParseStatus
AArch64AsmParser::tryParseBarriernXSOperand(OperandVector &Operands) {
  const AsmToken &Tok = getTok();

  assert(Mnemonic == "dsb" && "Instruction does not accept nXS operands");
  if (Mnemonic != "dsb")
    return ParseStatus::Failure;

  if (parseOptionalToken(AsmToken::Hash) || Tok.is(AsmToken::Integer)) {
    // Immediate operand.
    const MCExpr *ImmVal;
    SMLoc ExprLoc = getLoc();
    if (getParser().parseExpression(ImmVal))
      return ParseStatus::Failure;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return Error(ExprLoc, "immediate value expected for barrier operand");
    int64_t Value = MCE->getValue();
    // v8.7-A DSB in the nXS variant accepts only the following immediate
    // values: 16, 20, 24, 28.
    if (Value != 16 && Value != 20 && Value != 24 && Value != 28)
      return Error(ExprLoc, "barrier operand out of range");
    auto DB = AArch64DBnXS::lookupDBnXSByImmValue(Value);
    Operands.push_back(AArch64Operand::CreateBarrier(DB->Encoding, DB->Name,
                                                     ExprLoc, getContext(),
                                                     true /*hasnXSModifier*/));
    return ParseStatus::Success;
  }

  if (Tok.isNot(AsmToken::Identifier))
    return TokError("invalid operand for instruction");

  StringRef Operand = Tok.getString();
  auto DB = AArch64DBnXS::lookupDBnXSByName(Operand);

  if (!DB)
    return TokError("invalid barrier option name");

  Operands.push_back(
      AArch64Operand::CreateBarrier(DB->Encoding, Tok.getString(), getLoc(),
                                    getContext(), true /*hasnXSModifier*/));
  Lex(); // Consume the option

  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseSysReg(OperandVector &Operands) {
  const AsmToken &Tok = getTok();

  if (Tok.isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  if (AArch64SVCR::lookupSVCRByName(Tok.getString()))
    return ParseStatus::NoMatch;

  int MRSReg, MSRReg;
  auto SysReg = AArch64SysReg::lookupSysRegByName(Tok.getString());
  if (SysReg && SysReg->haveFeatures(getSTI().getFeatureBits())) {
    MRSReg = SysReg->Readable ? SysReg->Encoding : -1;
    MSRReg = SysReg->Writeable ? SysReg->Encoding : -1;
  } else
    MRSReg = MSRReg = AArch64SysReg::parseGenericRegister(Tok.getString());

  unsigned PStateImm = -1;
  auto PState15 = AArch64PState::lookupPStateImm0_15ByName(Tok.getString());
  if (PState15 && PState15->haveFeatures(getSTI().getFeatureBits()))
    PStateImm = PState15->Encoding;
  if (!PState15) {
    auto PState1 = AArch64PState::lookupPStateImm0_1ByName(Tok.getString());
    if (PState1 && PState1->haveFeatures(getSTI().getFeatureBits()))
      PStateImm = PState1->Encoding;
  }

  Operands.push_back(
      AArch64Operand::CreateSysReg(Tok.getString(), getLoc(), MRSReg, MSRReg,
                                   PStateImm, getContext()));
  Lex(); // Eat identifier

  return ParseStatus::Success;
}

/// tryParseNeonVectorRegister - Parse a vector register operand.
bool AArch64AsmParser::tryParseNeonVectorRegister(OperandVector &Operands) {
  if (getTok().isNot(AsmToken::Identifier))
    return true;

  SMLoc S = getLoc();
  // Check for a vector register specifier first.
  StringRef Kind;
  MCRegister Reg;
  ParseStatus Res = tryParseVectorRegister(Reg, Kind, RegKind::NeonVector);
  if (!Res.isSuccess())
    return true;

  const auto &KindRes = parseVectorKind(Kind, RegKind::NeonVector);
  if (!KindRes)
    return true;

  unsigned ElementWidth = KindRes->second;
  Operands.push_back(
      AArch64Operand::CreateVectorReg(Reg, RegKind::NeonVector, ElementWidth,
                                      S, getLoc(), getContext()));

  // If there was an explicit qualifier, that goes on as a literal text
  // operand.
  if (!Kind.empty())
    Operands.push_back(AArch64Operand::CreateToken(Kind, S, getContext()));

  return tryParseVectorIndex(Operands).isFailure();
}

ParseStatus AArch64AsmParser::tryParseVectorIndex(OperandVector &Operands) {
  SMLoc SIdx = getLoc();
  if (parseOptionalToken(AsmToken::LBrac)) {
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return ParseStatus::NoMatch;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return TokError("immediate value expected for vector index");

    SMLoc E = getLoc();

    if (parseToken(AsmToken::RBrac, "']' expected"))
      return ParseStatus::Failure;

    Operands.push_back(AArch64Operand::CreateVectorIndex(MCE->getValue(), SIdx,
                                                         E, getContext()));
    return ParseStatus::Success;
  }

  return ParseStatus::NoMatch;
}

// tryParseVectorRegister - Try to parse a vector register name with
// optional kind specifier. If it is a register specifier, eat the token
// and return it.
ParseStatus AArch64AsmParser::tryParseVectorRegister(MCRegister &Reg,
                                                     StringRef &Kind,
                                                     RegKind MatchKind) {
  const AsmToken &Tok = getTok();

  if (Tok.isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  StringRef Name = Tok.getString();
  // If there is a kind specifier, it's separated from the register name by
  // a '.'.
  size_t Start = 0, Next = Name.find('.');
  StringRef Head = Name.slice(Start, Next);
  unsigned RegNum = matchRegisterNameAlias(Head, MatchKind);

  if (RegNum) {
    if (Next != StringRef::npos) {
      Kind = Name.slice(Next, StringRef::npos);
      if (!isValidVectorKind(Kind, MatchKind))
        return TokError("invalid vector kind qualifier");
    }
    Lex(); // Eat the register token.

    Reg = RegNum;
    return ParseStatus::Success;
  }

  return ParseStatus::NoMatch;
}

ParseStatus AArch64AsmParser::tryParseSVEPredicateOrPredicateAsCounterVector(
    OperandVector &Operands) {
  ParseStatus Status =
      tryParseSVEPredicateVector<RegKind::SVEPredicateAsCounter>(Operands);
  if (!Status.isSuccess())
    Status = tryParseSVEPredicateVector<RegKind::SVEPredicateVector>(Operands);
  return Status;
}

/// tryParseSVEPredicateVector - Parse a SVE predicate register operand.
template <RegKind RK>
ParseStatus
AArch64AsmParser::tryParseSVEPredicateVector(OperandVector &Operands) {
  // Check for a SVE predicate register specifier first.
  const SMLoc S = getLoc();
  StringRef Kind;
  MCRegister RegNum;
  auto Res = tryParseVectorRegister(RegNum, Kind, RK);
  if (!Res.isSuccess())
    return Res;

  const auto &KindRes = parseVectorKind(Kind, RK);
  if (!KindRes)
    return ParseStatus::NoMatch;

  unsigned ElementWidth = KindRes->second;
  Operands.push_back(AArch64Operand::CreateVectorReg(
      RegNum, RK, ElementWidth, S,
      getLoc(), getContext()));

  if (getLexer().is(AsmToken::LBrac)) {
    if (RK == RegKind::SVEPredicateAsCounter) {
      ParseStatus ResIndex = tryParseVectorIndex(Operands);
      if (ResIndex.isSuccess())
        return ParseStatus::Success;
    } else {
      // Indexed predicate, there's no comma so try parse the next operand
      // immediately.
      if (parseOperand(Operands, false, false))
        return ParseStatus::NoMatch;
    }
  }

  // Not all predicates are followed by a '/m' or '/z'.
  if (getTok().isNot(AsmToken::Slash))
    return ParseStatus::Success;

  // But when they do they shouldn't have an element type suffix.
  if (!Kind.empty())
    return Error(S, "not expecting size suffix");

  // Add a literal slash as operand
  Operands.push_back(AArch64Operand::CreateToken("/", getLoc(), getContext()));

  Lex(); // Eat the slash.

  // Zeroing or merging?
  auto Pred = getTok().getString().lower();
  if (RK == RegKind::SVEPredicateAsCounter && Pred != "z")
    return Error(getLoc(), "expecting 'z' predication");

  if (RK == RegKind::SVEPredicateVector && Pred != "z" && Pred != "m")
    return Error(getLoc(), "expecting 'm' or 'z' predication");

  // Add zero/merge token.
  const char *ZM = Pred == "z" ? "z" : "m";
  Operands.push_back(AArch64Operand::CreateToken(ZM, getLoc(), getContext()));

  Lex(); // Eat zero/merge token.
  return ParseStatus::Success;
}

/// parseRegister - Parse a register operand.
bool AArch64AsmParser::parseRegister(OperandVector &Operands) {
  // Try for a Neon vector register.
  if (!tryParseNeonVectorRegister(Operands))
    return false;

  if (tryParseZTOperand(Operands).isSuccess())
    return false;

  // Otherwise try for a scalar register.
  if (tryParseGPROperand<false>(Operands).isSuccess())
    return false;

  return true;
}

bool AArch64AsmParser::parseSymbolicImmVal(const MCExpr *&ImmVal) {
  bool HasELFModifier = false;
  AArch64MCExpr::VariantKind RefKind;

  if (parseOptionalToken(AsmToken::Colon)) {
    HasELFModifier = true;

    if (getTok().isNot(AsmToken::Identifier))
      return TokError("expect relocation specifier in operand after ':'");

    std::string LowerCase = getTok().getIdentifier().lower();
    RefKind = StringSwitch<AArch64MCExpr::VariantKind>(LowerCase)
                  .Case("lo12", AArch64MCExpr::VK_LO12)
                  .Case("abs_g3", AArch64MCExpr::VK_ABS_G3)
                  .Case("abs_g2", AArch64MCExpr::VK_ABS_G2)
                  .Case("abs_g2_s", AArch64MCExpr::VK_ABS_G2_S)
                  .Case("abs_g2_nc", AArch64MCExpr::VK_ABS_G2_NC)
                  .Case("abs_g1", AArch64MCExpr::VK_ABS_G1)
                  .Case("abs_g1_s", AArch64MCExpr::VK_ABS_G1_S)
                  .Case("abs_g1_nc", AArch64MCExpr::VK_ABS_G1_NC)
                  .Case("abs_g0", AArch64MCExpr::VK_ABS_G0)
                  .Case("abs_g0_s", AArch64MCExpr::VK_ABS_G0_S)
                  .Case("abs_g0_nc", AArch64MCExpr::VK_ABS_G0_NC)
                  .Case("prel_g3", AArch64MCExpr::VK_PREL_G3)
                  .Case("prel_g2", AArch64MCExpr::VK_PREL_G2)
                  .Case("prel_g2_nc", AArch64MCExpr::VK_PREL_G2_NC)
                  .Case("prel_g1", AArch64MCExpr::VK_PREL_G1)
                  .Case("prel_g1_nc", AArch64MCExpr::VK_PREL_G1_NC)
                  .Case("prel_g0", AArch64MCExpr::VK_PREL_G0)
                  .Case("prel_g0_nc", AArch64MCExpr::VK_PREL_G0_NC)
                  .Case("dtprel_g2", AArch64MCExpr::VK_DTPREL_G2)
                  .Case("dtprel_g1", AArch64MCExpr::VK_DTPREL_G1)
                  .Case("dtprel_g1_nc", AArch64MCExpr::VK_DTPREL_G1_NC)
                  .Case("dtprel_g0", AArch64MCExpr::VK_DTPREL_G0)
                  .Case("dtprel_g0_nc", AArch64MCExpr::VK_DTPREL_G0_NC)
                  .Case("dtprel_hi12", AArch64MCExpr::VK_DTPREL_HI12)
                  .Case("dtprel_lo12", AArch64MCExpr::VK_DTPREL_LO12)
                  .Case("dtprel_lo12_nc", AArch64MCExpr::VK_DTPREL_LO12_NC)
                  .Case("pg_hi21_nc", AArch64MCExpr::VK_ABS_PAGE_NC)
                  .Case("tprel_g2", AArch64MCExpr::VK_TPREL_G2)
                  .Case("tprel_g1", AArch64MCExpr::VK_TPREL_G1)
                  .Case("tprel_g1_nc", AArch64MCExpr::VK_TPREL_G1_NC)
                  .Case("tprel_g0", AArch64MCExpr::VK_TPREL_G0)
                  .Case("tprel_g0_nc", AArch64MCExpr::VK_TPREL_G0_NC)
                  .Case("tprel_hi12", AArch64MCExpr::VK_TPREL_HI12)
                  .Case("tprel_lo12", AArch64MCExpr::VK_TPREL_LO12)
                  .Case("tprel_lo12_nc", AArch64MCExpr::VK_TPREL_LO12_NC)
                  .Case("tlsdesc_lo12", AArch64MCExpr::VK_TLSDESC_LO12)
                  .Case("got", AArch64MCExpr::VK_GOT_PAGE)
                  .Case("gotpage_lo15", AArch64MCExpr::VK_GOT_PAGE_LO15)
                  .Case("got_lo12", AArch64MCExpr::VK_GOT_LO12)
                  .Case("gottprel", AArch64MCExpr::VK_GOTTPREL_PAGE)
                  .Case("gottprel_lo12", AArch64MCExpr::VK_GOTTPREL_LO12_NC)
                  .Case("gottprel_g1", AArch64MCExpr::VK_GOTTPREL_G1)
                  .Case("gottprel_g0_nc", AArch64MCExpr::VK_GOTTPREL_G0_NC)
                  .Case("tlsdesc", AArch64MCExpr::VK_TLSDESC_PAGE)
                  .Case("secrel_lo12", AArch64MCExpr::VK_SECREL_LO12)
                  .Case("secrel_hi12", AArch64MCExpr::VK_SECREL_HI12)
                  .Default(AArch64MCExpr::VK_INVALID);

    if (RefKind == AArch64MCExpr::VK_INVALID)
      return TokError("expect relocation specifier in operand after ':'");

    Lex(); // Eat identifier

    if (parseToken(AsmToken::Colon, "expect ':' after relocation specifier"))
      return true;
  }

  if (getParser().parseExpression(ImmVal))
    return true;

  if (HasELFModifier)
    ImmVal = AArch64MCExpr::create(ImmVal, RefKind, getContext());

  return false;
}

ParseStatus AArch64AsmParser::tryParseMatrixTileList(OperandVector &Operands) {
  if (getTok().isNot(AsmToken::LCurly))
    return ParseStatus::NoMatch;

  auto ParseMatrixTile = [this](unsigned &Reg,
                                unsigned &ElementWidth) -> ParseStatus {
    StringRef Name = getTok().getString();
    size_t DotPosition = Name.find('.');
    if (DotPosition == StringRef::npos)
      return ParseStatus::NoMatch;

    unsigned RegNum = matchMatrixTileListRegName(Name);
    if (!RegNum)
      return ParseStatus::NoMatch;

    StringRef Tail = Name.drop_front(DotPosition);
    const std::optional<std::pair<int, int>> &KindRes =
        parseVectorKind(Tail, RegKind::Matrix);
    if (!KindRes)
      return TokError(
          "Expected the register to be followed by element width suffix");
    ElementWidth = KindRes->second;
    Reg = RegNum;
    Lex(); // Eat the register.
    return ParseStatus::Success;
  };

  SMLoc S = getLoc();
  auto LCurly = getTok();
  Lex(); // Eat left bracket token.

  // Empty matrix list
  if (parseOptionalToken(AsmToken::RCurly)) {
    Operands.push_back(AArch64Operand::CreateMatrixTileList(
        /*RegMask=*/0, S, getLoc(), getContext()));
    return ParseStatus::Success;
  }

  // Try parse {za} alias early
  if (getTok().getString().equals_insensitive("za")) {
    Lex(); // Eat 'za'

    if (parseToken(AsmToken::RCurly, "'}' expected"))
      return ParseStatus::Failure;

    Operands.push_back(AArch64Operand::CreateMatrixTileList(
        /*RegMask=*/0xFF, S, getLoc(), getContext()));
    return ParseStatus::Success;
  }

  SMLoc TileLoc = getLoc();

  unsigned FirstReg, ElementWidth;
  auto ParseRes = ParseMatrixTile(FirstReg, ElementWidth);
  if (!ParseRes.isSuccess()) {
    getLexer().UnLex(LCurly);
    return ParseRes;
  }

  const MCRegisterInfo *RI = getContext().getRegisterInfo();

  unsigned PrevReg = FirstReg;

  SmallSet<unsigned, 8> DRegs;
  AArch64Operand::ComputeRegsForAlias(FirstReg, DRegs, ElementWidth);

  SmallSet<unsigned, 8> SeenRegs;
  SeenRegs.insert(FirstReg);

  while (parseOptionalToken(AsmToken::Comma)) {
    TileLoc = getLoc();
    unsigned Reg, NextElementWidth;
    ParseRes = ParseMatrixTile(Reg, NextElementWidth);
    if (!ParseRes.isSuccess())
      return ParseRes;

    // Element size must match on all regs in the list.
    if (ElementWidth != NextElementWidth)
      return Error(TileLoc, "mismatched register size suffix");

    if (RI->getEncodingValue(Reg) <= (RI->getEncodingValue(PrevReg)))
      Warning(TileLoc, "tile list not in ascending order");

    if (SeenRegs.contains(Reg))
      Warning(TileLoc, "duplicate tile in list");
    else {
      SeenRegs.insert(Reg);
      AArch64Operand::ComputeRegsForAlias(Reg, DRegs, ElementWidth);
    }

    PrevReg = Reg;
  }

  if (parseToken(AsmToken::RCurly, "'}' expected"))
    return ParseStatus::Failure;

  unsigned RegMask = 0;
  for (auto Reg : DRegs)
    RegMask |= 0x1 << (RI->getEncodingValue(Reg) -
                       RI->getEncodingValue(AArch64::ZAD0));
  Operands.push_back(
      AArch64Operand::CreateMatrixTileList(RegMask, S, getLoc(), getContext()));

  return ParseStatus::Success;
}

template <RegKind VectorKind>
ParseStatus AArch64AsmParser::tryParseVectorList(OperandVector &Operands,
                                                 bool ExpectMatch) {
  MCAsmParser &Parser = getParser();
  if (!getTok().is(AsmToken::LCurly))
    return ParseStatus::NoMatch;

  // Wrapper around parse function
  auto ParseVector = [this](MCRegister &Reg, StringRef &Kind, SMLoc Loc,
                            bool NoMatchIsError) -> ParseStatus {
    auto RegTok = getTok();
    auto ParseRes = tryParseVectorRegister(Reg, Kind, VectorKind);
    if (ParseRes.isSuccess()) {
      if (parseVectorKind(Kind, VectorKind))
        return ParseRes;
      llvm_unreachable("Expected a valid vector kind");
    }

    if (RegTok.is(AsmToken::Identifier) && ParseRes.isNoMatch() &&
        RegTok.getString().equals_insensitive("zt0"))
      return ParseStatus::NoMatch;

    if (RegTok.isNot(AsmToken::Identifier) || ParseRes.isFailure() ||
        (ParseRes.isNoMatch() && NoMatchIsError &&
         !RegTok.getString().starts_with_insensitive("za")))
      return Error(Loc, "vector register expected");

    return ParseStatus::NoMatch;
  };

  int NumRegs = getNumRegsForRegKind(VectorKind);
  SMLoc S = getLoc();
  auto LCurly = getTok();
  Lex(); // Eat left bracket token.

  StringRef Kind;
  MCRegister FirstReg;
  auto ParseRes = ParseVector(FirstReg, Kind, getLoc(), ExpectMatch);

  // Put back the original left bracket if there was no match, so that
  // different types of list-operands can be matched (e.g. SVE, Neon).
  if (ParseRes.isNoMatch())
    Parser.getLexer().UnLex(LCurly);

  if (!ParseRes.isSuccess())
    return ParseRes;

  int64_t PrevReg = FirstReg;
  unsigned Count = 1;

  int Stride = 1;
  if (parseOptionalToken(AsmToken::Minus)) {
    SMLoc Loc = getLoc();
    StringRef NextKind;

    MCRegister Reg;
    ParseRes = ParseVector(Reg, NextKind, getLoc(), true);
    if (!ParseRes.isSuccess())
      return ParseRes;

    // Any Kind suffices must match on all regs in the list.
    if (Kind != NextKind)
      return Error(Loc, "mismatched register size suffix");

    unsigned Space =
        (PrevReg < Reg) ? (Reg - PrevReg) : (Reg + NumRegs - PrevReg);

    if (Space == 0 || Space > 3)
      return Error(Loc, "invalid number of vectors");

    Count += Space;
  }
  else {
    bool HasCalculatedStride = false;
    while (parseOptionalToken(AsmToken::Comma)) {
      SMLoc Loc = getLoc();
      StringRef NextKind;
      MCRegister Reg;
      ParseRes = ParseVector(Reg, NextKind, getLoc(), true);
      if (!ParseRes.isSuccess())
        return ParseRes;

      // Any Kind suffices must match on all regs in the list.
      if (Kind != NextKind)
        return Error(Loc, "mismatched register size suffix");

      unsigned RegVal = getContext().getRegisterInfo()->getEncodingValue(Reg);
      unsigned PrevRegVal =
          getContext().getRegisterInfo()->getEncodingValue(PrevReg);
      if (!HasCalculatedStride) {
        Stride = (PrevRegVal < RegVal) ? (RegVal - PrevRegVal)
                                       : (RegVal + NumRegs - PrevRegVal);
        HasCalculatedStride = true;
      }

      // Register must be incremental (with a wraparound at last register).
      if (Stride == 0 || RegVal != ((PrevRegVal + Stride) % NumRegs))
        return Error(Loc, "registers must have the same sequential stride");

      PrevReg = Reg;
      ++Count;
    }
  }

  if (parseToken(AsmToken::RCurly, "'}' expected"))
    return ParseStatus::Failure;

  if (Count > 4)
    return Error(S, "invalid number of vectors");

  unsigned NumElements = 0;
  unsigned ElementWidth = 0;
  if (!Kind.empty()) {
    if (const auto &VK = parseVectorKind(Kind, VectorKind))
      std::tie(NumElements, ElementWidth) = *VK;
  }

  Operands.push_back(AArch64Operand::CreateVectorList(
      FirstReg, Count, Stride, NumElements, ElementWidth, VectorKind, S,
      getLoc(), getContext()));

  return ParseStatus::Success;
}

/// parseNeonVectorList - Parse a vector list operand for AdvSIMD instructions.
bool AArch64AsmParser::parseNeonVectorList(OperandVector &Operands) {
  auto ParseRes = tryParseVectorList<RegKind::NeonVector>(Operands, true);
  if (!ParseRes.isSuccess())
    return true;

  return tryParseVectorIndex(Operands).isFailure();
}

ParseStatus AArch64AsmParser::tryParseGPR64sp0Operand(OperandVector &Operands) {
  SMLoc StartLoc = getLoc();

  MCRegister RegNum;
  ParseStatus Res = tryParseScalarRegister(RegNum);
  if (!Res.isSuccess())
    return Res;

  if (!parseOptionalToken(AsmToken::Comma)) {
    Operands.push_back(AArch64Operand::CreateReg(
        RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext()));
    return ParseStatus::Success;
  }

  parseOptionalToken(AsmToken::Hash);

  if (getTok().isNot(AsmToken::Integer))
    return Error(getLoc(), "index must be absent or #0");

  const MCExpr *ImmVal;
  if (getParser().parseExpression(ImmVal) || !isa<MCConstantExpr>(ImmVal) ||
      cast<MCConstantExpr>(ImmVal)->getValue() != 0)
    return Error(getLoc(), "index must be absent or #0");

  Operands.push_back(AArch64Operand::CreateReg(
      RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext()));
  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseZTOperand(OperandVector &Operands) {
  SMLoc StartLoc = getLoc();
  const AsmToken &Tok = getTok();
  std::string Name = Tok.getString().lower();

  unsigned RegNum = matchRegisterNameAlias(Name, RegKind::LookupTable);

  if (RegNum == 0)
    return ParseStatus::NoMatch;

  Operands.push_back(AArch64Operand::CreateReg(
      RegNum, RegKind::LookupTable, StartLoc, getLoc(), getContext()));
  Lex(); // Eat register.

  // Check if register is followed by an index
  if (parseOptionalToken(AsmToken::LBrac)) {
    Operands.push_back(
        AArch64Operand::CreateToken("[", getLoc(), getContext()));
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return ParseStatus::NoMatch;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return TokError("immediate value expected for vector index");
    Operands.push_back(AArch64Operand::CreateImm(
        MCConstantExpr::create(MCE->getValue(), getContext()), StartLoc,
        getLoc(), getContext()));
    if (parseOptionalToken(AsmToken::Comma))
      if (parseOptionalMulOperand(Operands))
        return ParseStatus::Failure;
    if (parseToken(AsmToken::RBrac, "']' expected"))
      return ParseStatus::Failure;
    Operands.push_back(
        AArch64Operand::CreateToken("]", getLoc(), getContext()));
  }
  return ParseStatus::Success;
}

template <bool ParseShiftExtend, RegConstraintEqualityTy EqTy>
ParseStatus AArch64AsmParser::tryParseGPROperand(OperandVector &Operands) {
  SMLoc StartLoc = getLoc();

  MCRegister RegNum;
  ParseStatus Res = tryParseScalarRegister(RegNum);
  if (!Res.isSuccess())
    return Res;

  // No shift/extend is the default.
  if (!ParseShiftExtend || getTok().isNot(AsmToken::Comma)) {
    Operands.push_back(AArch64Operand::CreateReg(
        RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext(), EqTy));
    return ParseStatus::Success;
  }

  // Eat the comma
  Lex();

  // Match the shift
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 1> ExtOpnd;
  Res = tryParseOptionalShiftExtend(ExtOpnd);
  if (!Res.isSuccess())
    return Res;

  auto Ext = static_cast<AArch64Operand*>(ExtOpnd.back().get());
  Operands.push_back(AArch64Operand::CreateReg(
      RegNum, RegKind::Scalar, StartLoc, Ext->getEndLoc(), getContext(), EqTy,
      Ext->getShiftExtendType(), Ext->getShiftExtendAmount(),
      Ext->hasShiftExtendAmount()));

  return ParseStatus::Success;
}

bool AArch64AsmParser::parseOptionalMulOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  // Some SVE instructions have a decoration after the immediate, i.e.
  // "mul vl". We parse them here and add tokens, which must be present in the
  // asm string in the tablegen instruction.
  bool NextIsVL =
      Parser.getLexer().peekTok().getString().equals_insensitive("vl");
  bool NextIsHash = Parser.getLexer().peekTok().is(AsmToken::Hash);
  if (!getTok().getString().equals_insensitive("mul") ||
      !(NextIsVL || NextIsHash))
    return true;

  Operands.push_back(
      AArch64Operand::CreateToken("mul", getLoc(), getContext()));
  Lex(); // Eat the "mul"

  if (NextIsVL) {
    Operands.push_back(
        AArch64Operand::CreateToken("vl", getLoc(), getContext()));
    Lex(); // Eat the "vl"
    return false;
  }

  if (NextIsHash) {
    Lex(); // Eat the #
    SMLoc S = getLoc();

    // Parse immediate operand.
    const MCExpr *ImmVal;
    if (!Parser.parseExpression(ImmVal))
      if (const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal)) {
        Operands.push_back(AArch64Operand::CreateImm(
            MCConstantExpr::create(MCE->getValue(), getContext()), S, getLoc(),
            getContext()));
        return false;
      }
  }

  return Error(getLoc(), "expected 'vl' or '#<imm>'");
}

bool AArch64AsmParser::parseOptionalVGOperand(OperandVector &Operands,
                                              StringRef &VecGroup) {
  MCAsmParser &Parser = getParser();
  auto Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return true;

  StringRef VG = StringSwitch<StringRef>(Tok.getString().lower())
                     .Case("vgx2", "vgx2")
                     .Case("vgx4", "vgx4")
                     .Default("");

  if (VG.empty())
    return true;

  VecGroup = VG;
  Parser.Lex(); // Eat vgx[2|4]
  return false;
}

bool AArch64AsmParser::parseKeywordOperand(OperandVector &Operands) {
  auto Tok = getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return true;

  auto Keyword = Tok.getString();
  Keyword = StringSwitch<StringRef>(Keyword.lower())
                .Case("sm", "sm")
                .Case("za", "za")
                .Default(Keyword);
  Operands.push_back(
      AArch64Operand::CreateToken(Keyword, Tok.getLoc(), getContext()));

  Lex();
  return false;
}

/// parseOperand - Parse a arm instruction operand.  For now this parses the
/// operand regardless of the mnemonic.
bool AArch64AsmParser::parseOperand(OperandVector &Operands, bool isCondCode,
                                  bool invertCondCode) {
  MCAsmParser &Parser = getParser();

  ParseStatus ResTy =
      MatchOperandParserImpl(Operands, Mnemonic, /*ParseForAllFeatures=*/true);

  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  if (ResTy.isSuccess())
    return false;
  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (ResTy.isFailure())
    return true;

  // Nothing custom, so do general case parsing.
  SMLoc S, E;
  auto parseOptionalShiftExtend = [&](AsmToken SavedTok) {
    if (parseOptionalToken(AsmToken::Comma)) {
      ParseStatus Res = tryParseOptionalShiftExtend(Operands);
      if (!Res.isNoMatch())
        return Res.isFailure();
      getLexer().UnLex(SavedTok);
    }
    return false;
  };
  switch (getLexer().getKind()) {
  default: {
    SMLoc S = getLoc();
    const MCExpr *Expr;
    if (parseSymbolicImmVal(Expr))
      return Error(S, "invalid operand");

    SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));
    return parseOptionalShiftExtend(getTok());
  }
  case AsmToken::LBrac: {
    Operands.push_back(
        AArch64Operand::CreateToken("[", getLoc(), getContext()));
    Lex(); // Eat '['

    // There's no comma after a '[', so we can parse the next operand
    // immediately.
    return parseOperand(Operands, false, false);
  }
  case AsmToken::LCurly: {
    if (!parseNeonVectorList(Operands))
      return false;

    Operands.push_back(
        AArch64Operand::CreateToken("{", getLoc(), getContext()));
    Lex(); // Eat '{'

    // There's no comma after a '{', so we can parse the next operand
    // immediately.
    return parseOperand(Operands, false, false);
  }
  case AsmToken::Identifier: {
    // See if this is a "VG" decoration used by SME instructions.
    StringRef VecGroup;
    if (!parseOptionalVGOperand(Operands, VecGroup)) {
      Operands.push_back(
          AArch64Operand::CreateToken(VecGroup, getLoc(), getContext()));
      return false;
    }
    // If we're expecting a Condition Code operand, then just parse that.
    if (isCondCode)
      return parseCondCode(Operands, invertCondCode);

    // If it's a register name, parse it.
    if (!parseRegister(Operands)) {
      // Parse an optional shift/extend modifier.
      AsmToken SavedTok = getTok();
      if (parseOptionalToken(AsmToken::Comma)) {
        // The operand after the register may be a label (e.g. ADR/ADRP).  Check
        // such cases and don't report an error when <label> happens to match a
        // shift/extend modifier.
        ParseStatus Res = MatchOperandParserImpl(Operands, Mnemonic,
                                                 /*ParseForAllFeatures=*/true);
        if (!Res.isNoMatch())
          return Res.isFailure();
        Res = tryParseOptionalShiftExtend(Operands);
        if (!Res.isNoMatch())
          return Res.isFailure();
        getLexer().UnLex(SavedTok);
      }
      return false;
    }

    // See if this is a "mul vl" decoration or "mul #<int>" operand used
    // by SVE instructions.
    if (!parseOptionalMulOperand(Operands))
      return false;

    // If this is a two-word mnemonic, parse its special keyword
    // operand as an identifier.
    if (Mnemonic == "brb" || Mnemonic == "smstart" || Mnemonic == "smstop" ||
        Mnemonic == "gcsb")
      return parseKeywordOperand(Operands);

    // This was not a register so parse other operands that start with an
    // identifier (like labels) as expressions and create them as immediates.
    const MCExpr *IdVal;
    S = getLoc();
    if (getParser().parseExpression(IdVal))
      return true;
    E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(IdVal, S, E, getContext()));
    return false;
  }
  case AsmToken::Integer:
  case AsmToken::Real:
  case AsmToken::Hash: {
    // #42 -> immediate.
    S = getLoc();

    parseOptionalToken(AsmToken::Hash);

    // Parse a negative sign
    bool isNegative = false;
    if (getTok().is(AsmToken::Minus)) {
      isNegative = true;
      // We need to consume this token only when we have a Real, otherwise
      // we let parseSymbolicImmVal take care of it
      if (Parser.getLexer().peekTok().is(AsmToken::Real))
        Lex();
    }

    // The only Real that should come through here is a literal #0.0 for
    // the fcmp[e] r, #0.0 instructions. They expect raw token operands,
    // so convert the value.
    const AsmToken &Tok = getTok();
    if (Tok.is(AsmToken::Real)) {
      APFloat RealVal(APFloat::IEEEdouble(), Tok.getString());
      uint64_t IntVal = RealVal.bitcastToAPInt().getZExtValue();
      if (Mnemonic != "fcmp" && Mnemonic != "fcmpe" && Mnemonic != "fcmeq" &&
          Mnemonic != "fcmge" && Mnemonic != "fcmgt" && Mnemonic != "fcmle" &&
          Mnemonic != "fcmlt" && Mnemonic != "fcmne")
        return TokError("unexpected floating point literal");
      else if (IntVal != 0 || isNegative)
        return TokError("expected floating-point constant #0.0");
      Lex(); // Eat the token.

      Operands.push_back(AArch64Operand::CreateToken("#0", S, getContext()));
      Operands.push_back(AArch64Operand::CreateToken(".0", S, getContext()));
      return false;
    }

    const MCExpr *ImmVal;
    if (parseSymbolicImmVal(ImmVal))
      return true;

    E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(ImmVal, S, E, getContext()));

    // Parse an optional shift/extend modifier.
    return parseOptionalShiftExtend(Tok);
  }
  case AsmToken::Equal: {
    SMLoc Loc = getLoc();
    if (Mnemonic != "ldr") // only parse for ldr pseudo (e.g. ldr r0, =val)
      return TokError("unexpected token in operand");
    Lex(); // Eat '='
    const MCExpr *SubExprVal;
    if (getParser().parseExpression(SubExprVal))
      return true;

    if (Operands.size() < 2 ||
        !static_cast<AArch64Operand &>(*Operands[1]).isScalarReg())
      return Error(Loc, "Only valid when first operand is register");

    bool IsXReg =
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Operands[1]->getReg());

    MCContext& Ctx = getContext();
    E = SMLoc::getFromPointer(Loc.getPointer() - 1);
    // If the op is an imm and can be fit into a mov, then replace ldr with mov.
    if (isa<MCConstantExpr>(SubExprVal)) {
      uint64_t Imm = (cast<MCConstantExpr>(SubExprVal))->getValue();
      uint32_t ShiftAmt = 0, MaxShiftAmt = IsXReg ? 48 : 16;
      while (Imm > 0xFFFF && llvm::countr_zero(Imm) >= 16) {
        ShiftAmt += 16;
        Imm >>= 16;
      }
      if (ShiftAmt <= MaxShiftAmt && Imm <= 0xFFFF) {
        Operands[0] = AArch64Operand::CreateToken("movz", Loc, Ctx);
        Operands.push_back(AArch64Operand::CreateImm(
            MCConstantExpr::create(Imm, Ctx), S, E, Ctx));
        if (ShiftAmt)
          Operands.push_back(AArch64Operand::CreateShiftExtend(AArch64_AM::LSL,
                     ShiftAmt, true, S, E, Ctx));
        return false;
      }
      APInt Simm = APInt(64, Imm << ShiftAmt);
      // check if the immediate is an unsigned or signed 32-bit int for W regs
      if (!IsXReg && !(Simm.isIntN(32) || Simm.isSignedIntN(32)))
        return Error(Loc, "Immediate too large for register");
    }
    // If it is a label or an imm that cannot fit in a movz, put it into CP.
    const MCExpr *CPLoc =
        getTargetStreamer().addConstantPoolEntry(SubExprVal, IsXReg ? 8 : 4, Loc);
    Operands.push_back(AArch64Operand::CreateImm(CPLoc, S, E, Ctx));
    return false;
  }
  }
}

bool AArch64AsmParser::parseImmExpr(int64_t &Out) {
  const MCExpr *Expr = nullptr;
  SMLoc L = getLoc();
  if (check(getParser().parseExpression(Expr), L, "expected expression"))
    return true;
  const MCConstantExpr *Value = dyn_cast_or_null<MCConstantExpr>(Expr);
  if (check(!Value, L, "expected constant expression"))
    return true;
  Out = Value->getValue();
  return false;
}

bool AArch64AsmParser::parseComma() {
  if (check(getTok().isNot(AsmToken::Comma), getLoc(), "expected comma"))
    return true;
  // Eat the comma
  Lex();
  return false;
}

bool AArch64AsmParser::parseRegisterInRange(unsigned &Out, unsigned Base,
                                            unsigned First, unsigned Last) {
  MCRegister Reg;
  SMLoc Start, End;
  if (check(parseRegister(Reg, Start, End), getLoc(), "expected register"))
    return true;

  // Special handling for FP and LR; they aren't linearly after x28 in
  // the registers enum.
  unsigned RangeEnd = Last;
  if (Base == AArch64::X0) {
    if (Last == AArch64::FP) {
      RangeEnd = AArch64::X28;
      if (Reg == AArch64::FP) {
        Out = 29;
        return false;
      }
    }
    if (Last == AArch64::LR) {
      RangeEnd = AArch64::X28;
      if (Reg == AArch64::FP) {
        Out = 29;
        return false;
      } else if (Reg == AArch64::LR) {
        Out = 30;
        return false;
      }
    }
  }

  if (check(Reg < First || Reg > RangeEnd, Start,
            Twine("expected register in range ") +
                AArch64InstPrinter::getRegisterName(First) + " to " +
                AArch64InstPrinter::getRegisterName(Last)))
    return true;
  Out = Reg - Base;
  return false;
}

bool AArch64AsmParser::areEqualRegs(const MCParsedAsmOperand &Op1,
                                    const MCParsedAsmOperand &Op2) const {
  auto &AOp1 = static_cast<const AArch64Operand&>(Op1);
  auto &AOp2 = static_cast<const AArch64Operand&>(Op2);

  if (AOp1.isVectorList() && AOp2.isVectorList())
    return AOp1.getVectorListCount() == AOp2.getVectorListCount() &&
           AOp1.getVectorListStart() == AOp2.getVectorListStart() &&
           AOp1.getVectorListStride() == AOp2.getVectorListStride();

  if (!AOp1.isReg() || !AOp2.isReg())
    return false;

  if (AOp1.getRegEqualityTy() == RegConstraintEqualityTy::EqualsReg &&
      AOp2.getRegEqualityTy() == RegConstraintEqualityTy::EqualsReg)
    return MCTargetAsmParser::areEqualRegs(Op1, Op2);

  assert(AOp1.isScalarReg() && AOp2.isScalarReg() &&
         "Testing equality of non-scalar registers not supported");

  // Check if a registers match their sub/super register classes.
  if (AOp1.getRegEqualityTy() == EqualsSuperReg)
    return getXRegFromWReg(Op1.getReg()) == Op2.getReg();
  if (AOp1.getRegEqualityTy() == EqualsSubReg)
    return getWRegFromXReg(Op1.getReg()) == Op2.getReg();
  if (AOp2.getRegEqualityTy() == EqualsSuperReg)
    return getXRegFromWReg(Op2.getReg()) == Op1.getReg();
  if (AOp2.getRegEqualityTy() == EqualsSubReg)
    return getWRegFromXReg(Op2.getReg()) == Op1.getReg();

  return false;
}

/// ParseInstruction - Parse an AArch64 instruction mnemonic followed by its
/// operands.
bool AArch64AsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {
  Name = StringSwitch<StringRef>(Name.lower())
             .Case("beq", "b.eq")
             .Case("bne", "b.ne")
             .Case("bhs", "b.hs")
             .Case("bcs", "b.cs")
             .Case("blo", "b.lo")
             .Case("bcc", "b.cc")
             .Case("bmi", "b.mi")
             .Case("bpl", "b.pl")
             .Case("bvs", "b.vs")
             .Case("bvc", "b.vc")
             .Case("bhi", "b.hi")
             .Case("bls", "b.ls")
             .Case("bge", "b.ge")
             .Case("blt", "b.lt")
             .Case("bgt", "b.gt")
             .Case("ble", "b.le")
             .Case("bal", "b.al")
             .Case("bnv", "b.nv")
             .Default(Name);

  // First check for the AArch64-specific .req directive.
  if (getTok().is(AsmToken::Identifier) &&
      getTok().getIdentifier().lower() == ".req") {
    parseDirectiveReq(Name, NameLoc);
    // We always return 'error' for this, as we're done with this
    // statement and don't need to match the 'instruction."
    return true;
  }

  // Create the leading tokens for the mnemonic, split by '.' characters.
  size_t Start = 0, Next = Name.find('.');
  StringRef Head = Name.slice(Start, Next);

  // IC, DC, AT, TLBI and Prediction invalidation instructions are aliases for
  // the SYS instruction.
  if (Head == "ic" || Head == "dc" || Head == "at" || Head == "tlbi" ||
      Head == "cfp" || Head == "dvp" || Head == "cpp" || Head == "cosp")
    return parseSysAlias(Head, NameLoc, Operands);

  // TLBIP instructions are aliases for the SYSP instruction.
  if (Head == "tlbip")
    return parseSyspAlias(Head, NameLoc, Operands);

  Operands.push_back(AArch64Operand::CreateToken(Head, NameLoc, getContext()));
  Mnemonic = Head;

  // Handle condition codes for a branch mnemonic
  if ((Head == "b" || Head == "bc") && Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    Head = Name.slice(Start + 1, Next);

    SMLoc SuffixLoc = SMLoc::getFromPointer(NameLoc.getPointer() +
                                            (Head.data() - Name.data()));
    std::string Suggestion;
    AArch64CC::CondCode CC = parseCondCodeString(Head, Suggestion);
    if (CC == AArch64CC::Invalid) {
      std::string Msg = "invalid condition code";
      if (!Suggestion.empty())
        Msg += ", did you mean " + Suggestion + "?";
      return Error(SuffixLoc, Msg);
    }
    Operands.push_back(AArch64Operand::CreateToken(".", SuffixLoc, getContext(),
                                                   /*IsSuffix=*/true));
    Operands.push_back(
        AArch64Operand::CreateCondCode(CC, NameLoc, NameLoc, getContext()));
  }

  // Add the remaining tokens in the mnemonic.
  while (Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    Head = Name.slice(Start, Next);
    SMLoc SuffixLoc = SMLoc::getFromPointer(NameLoc.getPointer() +
                                            (Head.data() - Name.data()) + 1);
    Operands.push_back(AArch64Operand::CreateToken(
        Head, SuffixLoc, getContext(), /*IsSuffix=*/true));
  }

  // Conditional compare instructions have a Condition Code operand, which needs
  // to be parsed and an immediate operand created.
  bool condCodeFourthOperand =
      (Head == "ccmp" || Head == "ccmn" || Head == "fccmp" ||
       Head == "fccmpe" || Head == "fcsel" || Head == "csel" ||
       Head == "csinc" || Head == "csinv" || Head == "csneg");

  // These instructions are aliases to some of the conditional select
  // instructions. However, the condition code is inverted in the aliased
  // instruction.
  //
  // FIXME: Is this the correct way to handle these? Or should the parser
  //        generate the aliased instructions directly?
  bool condCodeSecondOperand = (Head == "cset" || Head == "csetm");
  bool condCodeThirdOperand =
      (Head == "cinc" || Head == "cinv" || Head == "cneg");

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {

    unsigned N = 1;
    do {
      // Parse and remember the operand.
      if (parseOperand(Operands, (N == 4 && condCodeFourthOperand) ||
                                     (N == 3 && condCodeThirdOperand) ||
                                     (N == 2 && condCodeSecondOperand),
                       condCodeSecondOperand || condCodeThirdOperand)) {
        return true;
      }

      // After successfully parsing some operands there are three special cases
      // to consider (i.e. notional operands not separated by commas). Two are
      // due to memory specifiers:
      //  + An RBrac will end an address for load/store/prefetch
      //  + An '!' will indicate a pre-indexed operation.
      //
      // And a further case is '}', which ends a group of tokens specifying the
      // SME accumulator array 'ZA' or tile vector, i.e.
      //
      //   '{ ZA }' or '{ <ZAt><HV>.<BHSDQ>[<Wv>, #<imm>] }'
      //
      // It's someone else's responsibility to make sure these tokens are sane
      // in the given context!

      if (parseOptionalToken(AsmToken::RBrac))
        Operands.push_back(
            AArch64Operand::CreateToken("]", getLoc(), getContext()));
      if (parseOptionalToken(AsmToken::Exclaim))
        Operands.push_back(
            AArch64Operand::CreateToken("!", getLoc(), getContext()));
      if (parseOptionalToken(AsmToken::RCurly))
        Operands.push_back(
            AArch64Operand::CreateToken("}", getLoc(), getContext()));

      ++N;
    } while (parseOptionalToken(AsmToken::Comma));
  }

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in argument list"))
    return true;

  return false;
}

static inline bool isMatchingOrAlias(unsigned ZReg, unsigned Reg) {
  assert((ZReg >= AArch64::Z0) && (ZReg <= AArch64::Z31));
  return (ZReg == ((Reg - AArch64::B0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::H0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::S0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::D0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::Q0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::Z0) + AArch64::Z0));
}

// FIXME: This entire function is a giant hack to provide us with decent
// operand range validation/diagnostics until TableGen/MC can be extended
// to support autogeneration of this kind of validation.
bool AArch64AsmParser::validateInstruction(MCInst &Inst, SMLoc &IDLoc,
                                           SmallVectorImpl<SMLoc> &Loc) {
  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  const MCInstrDesc &MCID = MII.get(Inst.getOpcode());

  // A prefix only applies to the instruction following it.  Here we extract
  // prefix information for the next instruction before validating the current
  // one so that in the case of failure we don't erronously continue using the
  // current prefix.
  PrefixInfo Prefix = NextPrefix;
  NextPrefix = PrefixInfo::CreateFromInst(Inst, MCID.TSFlags);

  // Before validating the instruction in isolation we run through the rules
  // applicable when it follows a prefix instruction.
  // NOTE: brk & hlt can be prefixed but require no additional validation.
  if (Prefix.isActive() &&
      (Inst.getOpcode() != AArch64::BRK) &&
      (Inst.getOpcode() != AArch64::HLT)) {

    // Prefixed intructions must have a destructive operand.
    if ((MCID.TSFlags & AArch64::DestructiveInstTypeMask) ==
        AArch64::NotDestructive)
      return Error(IDLoc, "instruction is unpredictable when following a"
                   " movprfx, suggest replacing movprfx with mov");

    // Destination operands must match.
    if (Inst.getOperand(0).getReg() != Prefix.getDstReg())
      return Error(Loc[0], "instruction is unpredictable when following a"
                   " movprfx writing to a different destination");

    // Destination operand must not be used in any other location.
    for (unsigned i = 1; i < Inst.getNumOperands(); ++i) {
      if (Inst.getOperand(i).isReg() &&
          (MCID.getOperandConstraint(i, MCOI::TIED_TO) == -1) &&
          isMatchingOrAlias(Prefix.getDstReg(), Inst.getOperand(i).getReg()))
        return Error(Loc[0], "instruction is unpredictable when following a"
                     " movprfx and destination also used as non-destructive"
                     " source");
    }

    auto PPRRegClass = AArch64MCRegisterClasses[AArch64::PPRRegClassID];
    if (Prefix.isPredicated()) {
      int PgIdx = -1;

      // Find the instructions general predicate.
      for (unsigned i = 1; i < Inst.getNumOperands(); ++i)
        if (Inst.getOperand(i).isReg() &&
            PPRRegClass.contains(Inst.getOperand(i).getReg())) {
          PgIdx = i;
          break;
        }

      // Instruction must be predicated if the movprfx is predicated.
      if (PgIdx == -1 ||
          (MCID.TSFlags & AArch64::ElementSizeMask) == AArch64::ElementSizeNone)
        return Error(IDLoc, "instruction is unpredictable when following a"
                     " predicated movprfx, suggest using unpredicated movprfx");

      // Instruction must use same general predicate as the movprfx.
      if (Inst.getOperand(PgIdx).getReg() != Prefix.getPgReg())
        return Error(IDLoc, "instruction is unpredictable when following a"
                     " predicated movprfx using a different general predicate");

      // Instruction element type must match the movprfx.
      if ((MCID.TSFlags & AArch64::ElementSizeMask) != Prefix.getElementSize())
        return Error(IDLoc, "instruction is unpredictable when following a"
                     " predicated movprfx with a different element size");
    }
  }

  // On ARM64EC, only valid registers may be used. Warn against using
  // explicitly disallowed registers.
  if (IsWindowsArm64EC) {
    for (unsigned i = 0; i < Inst.getNumOperands(); ++i) {
      if (Inst.getOperand(i).isReg()) {
        unsigned Reg = Inst.getOperand(i).getReg();
        // At this point, vector registers are matched to their
        // appropriately sized alias.
        if ((Reg == AArch64::W13 || Reg == AArch64::X13) ||
            (Reg == AArch64::W14 || Reg == AArch64::X14) ||
            (Reg == AArch64::W23 || Reg == AArch64::X23) ||
            (Reg == AArch64::W24 || Reg == AArch64::X24) ||
            (Reg == AArch64::W28 || Reg == AArch64::X28) ||
            (Reg >= AArch64::Q16 && Reg <= AArch64::Q31) ||
            (Reg >= AArch64::D16 && Reg <= AArch64::D31) ||
            (Reg >= AArch64::S16 && Reg <= AArch64::S31) ||
            (Reg >= AArch64::H16 && Reg <= AArch64::H31) ||
            (Reg >= AArch64::B16 && Reg <= AArch64::B31)) {
          Warning(IDLoc, "register " + Twine(RI->getName(Reg)) +
                             " is disallowed on ARM64EC.");
        }
      }
    }
  }

  // Check for indexed addressing modes w/ the base register being the
  // same as a destination/source register or pair load where
  // the Rt == Rt2. All of those are undefined behaviour.
  switch (Inst.getOpcode()) {
  case AArch64::LDPSWpre:
  case AArch64::LDPWpost:
  case AArch64::LDPWpre:
  case AArch64::LDPXpost:
  case AArch64::LDPXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable LDP instruction, writeback base "
                           "is also a destination");
    if (RI->isSubRegisterEq(Rn, Rt2))
      return Error(Loc[1], "unpredictable LDP instruction, writeback base "
                           "is also a destination");
    [[fallthrough]];
  }
  case AArch64::LDR_ZA:
  case AArch64::STR_ZA: {
    if (Inst.getOperand(2).isImm() && Inst.getOperand(4).isImm() &&
        Inst.getOperand(2).getImm() != Inst.getOperand(4).getImm())
      return Error(Loc[1],
                   "unpredictable instruction, immediate and offset mismatch.");
    break;
  }
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::LDPXi: {
    unsigned Rt = Inst.getOperand(0).getReg();
    unsigned Rt2 = Inst.getOperand(1).getReg();
    if (Rt == Rt2)
      return Error(Loc[1], "unpredictable LDP instruction, Rt2==Rt");
    break;
  }
  case AArch64::LDPDpost:
  case AArch64::LDPDpre:
  case AArch64::LDPQpost:
  case AArch64::LDPQpre:
  case AArch64::LDPSpost:
  case AArch64::LDPSpre:
  case AArch64::LDPSWpost: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    if (Rt == Rt2)
      return Error(Loc[1], "unpredictable LDP instruction, Rt2==Rt");
    break;
  }
  case AArch64::STPDpost:
  case AArch64::STPDpre:
  case AArch64::STPQpost:
  case AArch64::STPQpre:
  case AArch64::STPSpost:
  case AArch64::STPSpre:
  case AArch64::STPWpost:
  case AArch64::STPWpre:
  case AArch64::STPXpost:
  case AArch64::STPXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable STP instruction, writeback base "
                           "is also a source");
    if (RI->isSubRegisterEq(Rn, Rt2))
      return Error(Loc[1], "unpredictable STP instruction, writeback base "
                           "is also a source");
    break;
  }
  case AArch64::LDRBBpre:
  case AArch64::LDRBpre:
  case AArch64::LDRHHpre:
  case AArch64::LDRHpre:
  case AArch64::LDRSBWpre:
  case AArch64::LDRSBXpre:
  case AArch64::LDRSHWpre:
  case AArch64::LDRSHXpre:
  case AArch64::LDRSWpre:
  case AArch64::LDRWpre:
  case AArch64::LDRXpre:
  case AArch64::LDRBBpost:
  case AArch64::LDRBpost:
  case AArch64::LDRHHpost:
  case AArch64::LDRHpost:
  case AArch64::LDRSBWpost:
  case AArch64::LDRSBXpost:
  case AArch64::LDRSHWpost:
  case AArch64::LDRSHXpost:
  case AArch64::LDRSWpost:
  case AArch64::LDRWpost:
  case AArch64::LDRXpost: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable LDR instruction, writeback base "
                           "is also a source");
    break;
  }
  case AArch64::STRBBpost:
  case AArch64::STRBpost:
  case AArch64::STRHHpost:
  case AArch64::STRHpost:
  case AArch64::STRWpost:
  case AArch64::STRXpost:
  case AArch64::STRBBpre:
  case AArch64::STRBpre:
  case AArch64::STRHHpre:
  case AArch64::STRHpre:
  case AArch64::STRWpre:
  case AArch64::STRXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable STR instruction, writeback base "
                           "is also a source");
    break;
  }
  case AArch64::STXRB:
  case AArch64::STXRH:
  case AArch64::STXRW:
  case AArch64::STXRX:
  case AArch64::STLXRB:
  case AArch64::STLXRH:
  case AArch64::STLXRW:
  case AArch64::STLXRX: {
    unsigned Rs = Inst.getOperand(0).getReg();
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rt, Rs) ||
        (RI->isSubRegisterEq(Rn, Rs) && Rn != AArch64::SP))
      return Error(Loc[0],
                   "unpredictable STXR instruction, status is also a source");
    break;
  }
  case AArch64::STXPW:
  case AArch64::STXPX:
  case AArch64::STLXPW:
  case AArch64::STLXPX: {
    unsigned Rs = Inst.getOperand(0).getReg();
    unsigned Rt1 = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rt1, Rs) || RI->isSubRegisterEq(Rt2, Rs) ||
        (RI->isSubRegisterEq(Rn, Rs) && Rn != AArch64::SP))
      return Error(Loc[0],
                   "unpredictable STXP instruction, status is also a source");
    break;
  }
  case AArch64::LDRABwriteback:
  case AArch64::LDRAAwriteback: {
    unsigned Xt = Inst.getOperand(0).getReg();
    unsigned Xn = Inst.getOperand(1).getReg();
    if (Xt == Xn)
      return Error(Loc[0],
          "unpredictable LDRA instruction, writeback base"
          " is also a destination");
    break;
  }
  }

  // Check v8.8-A memops instructions.
  switch (Inst.getOpcode()) {
  case AArch64::CPYFP:
  case AArch64::CPYFPWN:
  case AArch64::CPYFPRN:
  case AArch64::CPYFPN:
  case AArch64::CPYFPWT:
  case AArch64::CPYFPWTWN:
  case AArch64::CPYFPWTRN:
  case AArch64::CPYFPWTN:
  case AArch64::CPYFPRT:
  case AArch64::CPYFPRTWN:
  case AArch64::CPYFPRTRN:
  case AArch64::CPYFPRTN:
  case AArch64::CPYFPT:
  case AArch64::CPYFPTWN:
  case AArch64::CPYFPTRN:
  case AArch64::CPYFPTN:
  case AArch64::CPYFM:
  case AArch64::CPYFMWN:
  case AArch64::CPYFMRN:
  case AArch64::CPYFMN:
  case AArch64::CPYFMWT:
  case AArch64::CPYFMWTWN:
  case AArch64::CPYFMWTRN:
  case AArch64::CPYFMWTN:
  case AArch64::CPYFMRT:
  case AArch64::CPYFMRTWN:
  case AArch64::CPYFMRTRN:
  case AArch64::CPYFMRTN:
  case AArch64::CPYFMT:
  case AArch64::CPYFMTWN:
  case AArch64::CPYFMTRN:
  case AArch64::CPYFMTN:
  case AArch64::CPYFE:
  case AArch64::CPYFEWN:
  case AArch64::CPYFERN:
  case AArch64::CPYFEN:
  case AArch64::CPYFEWT:
  case AArch64::CPYFEWTWN:
  case AArch64::CPYFEWTRN:
  case AArch64::CPYFEWTN:
  case AArch64::CPYFERT:
  case AArch64::CPYFERTWN:
  case AArch64::CPYFERTRN:
  case AArch64::CPYFERTN:
  case AArch64::CPYFET:
  case AArch64::CPYFETWN:
  case AArch64::CPYFETRN:
  case AArch64::CPYFETN:
  case AArch64::CPYP:
  case AArch64::CPYPWN:
  case AArch64::CPYPRN:
  case AArch64::CPYPN:
  case AArch64::CPYPWT:
  case AArch64::CPYPWTWN:
  case AArch64::CPYPWTRN:
  case AArch64::CPYPWTN:
  case AArch64::CPYPRT:
  case AArch64::CPYPRTWN:
  case AArch64::CPYPRTRN:
  case AArch64::CPYPRTN:
  case AArch64::CPYPT:
  case AArch64::CPYPTWN:
  case AArch64::CPYPTRN:
  case AArch64::CPYPTN:
  case AArch64::CPYM:
  case AArch64::CPYMWN:
  case AArch64::CPYMRN:
  case AArch64::CPYMN:
  case AArch64::CPYMWT:
  case AArch64::CPYMWTWN:
  case AArch64::CPYMWTRN:
  case AArch64::CPYMWTN:
  case AArch64::CPYMRT:
  case AArch64::CPYMRTWN:
  case AArch64::CPYMRTRN:
  case AArch64::CPYMRTN:
  case AArch64::CPYMT:
  case AArch64::CPYMTWN:
  case AArch64::CPYMTRN:
  case AArch64::CPYMTN:
  case AArch64::CPYE:
  case AArch64::CPYEWN:
  case AArch64::CPYERN:
  case AArch64::CPYEN:
  case AArch64::CPYEWT:
  case AArch64::CPYEWTWN:
  case AArch64::CPYEWTRN:
  case AArch64::CPYEWTN:
  case AArch64::CPYERT:
  case AArch64::CPYERTWN:
  case AArch64::CPYERTRN:
  case AArch64::CPYERTN:
  case AArch64::CPYET:
  case AArch64::CPYETWN:
  case AArch64::CPYETRN:
  case AArch64::CPYETN: {
    unsigned Xd_wb = Inst.getOperand(0).getReg();
    unsigned Xs_wb = Inst.getOperand(1).getReg();
    unsigned Xn_wb = Inst.getOperand(2).getReg();
    unsigned Xd = Inst.getOperand(3).getReg();
    unsigned Xs = Inst.getOperand(4).getReg();
    unsigned Xn = Inst.getOperand(5).getReg();
    if (Xd_wb != Xd)
      return Error(Loc[0],
                   "invalid CPY instruction, Xd_wb and Xd do not match");
    if (Xs_wb != Xs)
      return Error(Loc[0],
                   "invalid CPY instruction, Xs_wb and Xs do not match");
    if (Xn_wb != Xn)
      return Error(Loc[0],
                   "invalid CPY instruction, Xn_wb and Xn do not match");
    if (Xd == Xs)
      return Error(Loc[0], "invalid CPY instruction, destination and source"
                           " registers are the same");
    if (Xd == Xn)
      return Error(Loc[0], "invalid CPY instruction, destination and size"
                           " registers are the same");
    if (Xs == Xn)
      return Error(Loc[0], "invalid CPY instruction, source and size"
                           " registers are the same");
    break;
  }
  case AArch64::SETP:
  case AArch64::SETPT:
  case AArch64::SETPN:
  case AArch64::SETPTN:
  case AArch64::SETM:
  case AArch64::SETMT:
  case AArch64::SETMN:
  case AArch64::SETMTN:
  case AArch64::SETE:
  case AArch64::SETET:
  case AArch64::SETEN:
  case AArch64::SETETN:
  case AArch64::SETGP:
  case AArch64::SETGPT:
  case AArch64::SETGPN:
  case AArch64::SETGPTN:
  case AArch64::SETGM:
  case AArch64::SETGMT:
  case AArch64::SETGMN:
  case AArch64::SETGMTN:
  case AArch64::MOPSSETGE:
  case AArch64::MOPSSETGET:
  case AArch64::MOPSSETGEN:
  case AArch64::MOPSSETGETN: {
    unsigned Xd_wb = Inst.getOperand(0).getReg();
    unsigned Xn_wb = Inst.getOperand(1).getReg();
    unsigned Xd = Inst.getOperand(2).getReg();
    unsigned Xn = Inst.getOperand(3).getReg();
    unsigned Xm = Inst.getOperand(4).getReg();
    if (Xd_wb != Xd)
      return Error(Loc[0],
                   "invalid SET instruction, Xd_wb and Xd do not match");
    if (Xn_wb != Xn)
      return Error(Loc[0],
                   "invalid SET instruction, Xn_wb and Xn do not match");
    if (Xd == Xn)
      return Error(Loc[0], "invalid SET instruction, destination and size"
                           " registers are the same");
    if (Xd == Xm)
      return Error(Loc[0], "invalid SET instruction, destination and source"
                           " registers are the same");
    if (Xn == Xm)
      return Error(Loc[0], "invalid SET instruction, source and size"
                           " registers are the same");
    break;
  }
  }

  // Now check immediate ranges. Separate from the above as there is overlap
  // in the instructions being checked and this keeps the nested conditionals
  // to a minimum.
  switch (Inst.getOpcode()) {
  case AArch64::ADDSWri:
  case AArch64::ADDSXri:
  case AArch64::ADDWri:
  case AArch64::ADDXri:
  case AArch64::SUBSWri:
  case AArch64::SUBSXri:
  case AArch64::SUBWri:
  case AArch64::SUBXri: {
    // Annoyingly we can't do this in the isAddSubImm predicate, so there is
    // some slight duplication here.
    if (Inst.getOperand(2).isExpr()) {
      const MCExpr *Expr = Inst.getOperand(2).getExpr();
      AArch64MCExpr::VariantKind ELFRefKind;
      MCSymbolRefExpr::VariantKind DarwinRefKind;
      int64_t Addend;
      if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {

        // Only allow these with ADDXri.
        if ((DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF ||
             DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF) &&
            Inst.getOpcode() == AArch64::ADDXri)
          return false;

        // Only allow these with ADDXri/ADDWri
        if ((ELFRefKind == AArch64MCExpr::VK_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_DTPREL_HI12 ||
             ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC ||
             ELFRefKind == AArch64MCExpr::VK_TPREL_HI12 ||
             ELFRefKind == AArch64MCExpr::VK_TPREL_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC ||
             ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_SECREL_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_SECREL_HI12) &&
            (Inst.getOpcode() == AArch64::ADDXri ||
             Inst.getOpcode() == AArch64::ADDWri))
          return false;

        // Don't allow symbol refs in the immediate field otherwise
        // Note: Loc.back() may be Loc[1] or Loc[2] depending on the number of
        // operands of the original instruction (i.e. 'add w0, w1, borked' vs
        // 'cmp w0, 'borked')
        return Error(Loc.back(), "invalid immediate expression");
      }
      // We don't validate more complex expressions here
    }
    return false;
  }
  default:
    return false;
  }
}

static std::string AArch64MnemonicSpellCheck(StringRef S,
                                             const FeatureBitset &FBS,
                                             unsigned VariantID = 0);

bool AArch64AsmParser::showMatchError(SMLoc Loc, unsigned ErrCode,
                                      uint64_t ErrorInfo,
                                      OperandVector &Operands) {
  switch (ErrCode) {
  case Match_InvalidTiedOperand: {
    auto &Op = static_cast<const AArch64Operand &>(*Operands[ErrorInfo]);
    if (Op.isVectorList())
      return Error(Loc, "operand must match destination register list");

    assert(Op.isReg() && "Unexpected operand type");
    switch (Op.getRegEqualityTy()) {
    case RegConstraintEqualityTy::EqualsSubReg:
      return Error(Loc, "operand must be 64-bit form of destination register");
    case RegConstraintEqualityTy::EqualsSuperReg:
      return Error(Loc, "operand must be 32-bit form of destination register");
    case RegConstraintEqualityTy::EqualsReg:
      return Error(Loc, "operand must match destination register");
    }
    llvm_unreachable("Unknown RegConstraintEqualityTy");
  }
  case Match_MissingFeature:
    return Error(Loc,
                 "instruction requires a CPU feature not currently enabled");
  case Match_InvalidOperand:
    return Error(Loc, "invalid operand for instruction");
  case Match_InvalidSuffix:
    return Error(Loc, "invalid type suffix for instruction");
  case Match_InvalidCondCode:
    return Error(Loc, "expected AArch64 condition code");
  case Match_AddSubRegExtendSmall:
    return Error(Loc,
      "expected '[su]xt[bhw]' with optional integer in range [0, 4]");
  case Match_AddSubRegExtendLarge:
    return Error(Loc,
      "expected 'sxtx' 'uxtx' or 'lsl' with optional integer in range [0, 4]");
  case Match_AddSubSecondSource:
    return Error(Loc,
      "expected compatible register, symbol or integer in range [0, 4095]");
  case Match_LogicalSecondSource:
    return Error(Loc, "expected compatible register or logical immediate");
  case Match_InvalidMovImm32Shift:
    return Error(Loc, "expected 'lsl' with optional integer 0 or 16");
  case Match_InvalidMovImm64Shift:
    return Error(Loc, "expected 'lsl' with optional integer 0, 16, 32 or 48");
  case Match_AddSubRegShift32:
    return Error(Loc,
       "expected 'lsl', 'lsr' or 'asr' with optional integer in range [0, 31]");
  case Match_AddSubRegShift64:
    return Error(Loc,
       "expected 'lsl', 'lsr' or 'asr' with optional integer in range [0, 63]");
  case Match_InvalidFPImm:
    return Error(Loc,
                 "expected compatible register or floating-point constant");
  case Match_InvalidMemoryIndexedSImm6:
    return Error(Loc, "index must be an integer in range [-32, 31].");
  case Match_InvalidMemoryIndexedSImm5:
    return Error(Loc, "index must be an integer in range [-16, 15].");
  case Match_InvalidMemoryIndexed1SImm4:
    return Error(Loc, "index must be an integer in range [-8, 7].");
  case Match_InvalidMemoryIndexed2SImm4:
    return Error(Loc, "index must be a multiple of 2 in range [-16, 14].");
  case Match_InvalidMemoryIndexed3SImm4:
    return Error(Loc, "index must be a multiple of 3 in range [-24, 21].");
  case Match_InvalidMemoryIndexed4SImm4:
    return Error(Loc, "index must be a multiple of 4 in range [-32, 28].");
  case Match_InvalidMemoryIndexed16SImm4:
    return Error(Loc, "index must be a multiple of 16 in range [-128, 112].");
  case Match_InvalidMemoryIndexed32SImm4:
    return Error(Loc, "index must be a multiple of 32 in range [-256, 224].");
  case Match_InvalidMemoryIndexed1SImm6:
    return Error(Loc, "index must be an integer in range [-32, 31].");
  case Match_InvalidMemoryIndexedSImm8:
    return Error(Loc, "index must be an integer in range [-128, 127].");
  case Match_InvalidMemoryIndexedSImm9:
    return Error(Loc, "index must be an integer in range [-256, 255].");
  case Match_InvalidMemoryIndexed16SImm9:
    return Error(Loc, "index must be a multiple of 16 in range [-4096, 4080].");
  case Match_InvalidMemoryIndexed8SImm10:
    return Error(Loc, "index must be a multiple of 8 in range [-4096, 4088].");
  case Match_InvalidMemoryIndexed4SImm7:
    return Error(Loc, "index must be a multiple of 4 in range [-256, 252].");
  case Match_InvalidMemoryIndexed8SImm7:
    return Error(Loc, "index must be a multiple of 8 in range [-512, 504].");
  case Match_InvalidMemoryIndexed16SImm7:
    return Error(Loc, "index must be a multiple of 16 in range [-1024, 1008].");
  case Match_InvalidMemoryIndexed8UImm5:
    return Error(Loc, "index must be a multiple of 8 in range [0, 248].");
  case Match_InvalidMemoryIndexed8UImm3:
    return Error(Loc, "index must be a multiple of 8 in range [0, 56].");
  case Match_InvalidMemoryIndexed4UImm5:
    return Error(Loc, "index must be a multiple of 4 in range [0, 124].");
  case Match_InvalidMemoryIndexed2UImm5:
    return Error(Loc, "index must be a multiple of 2 in range [0, 62].");
  case Match_InvalidMemoryIndexed8UImm6:
    return Error(Loc, "index must be a multiple of 8 in range [0, 504].");
  case Match_InvalidMemoryIndexed16UImm6:
    return Error(Loc, "index must be a multiple of 16 in range [0, 1008].");
  case Match_InvalidMemoryIndexed4UImm6:
    return Error(Loc, "index must be a multiple of 4 in range [0, 252].");
  case Match_InvalidMemoryIndexed2UImm6:
    return Error(Loc, "index must be a multiple of 2 in range [0, 126].");
  case Match_InvalidMemoryIndexed1UImm6:
    return Error(Loc, "index must be in range [0, 63].");
  case Match_InvalidMemoryWExtend8:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0");
  case Match_InvalidMemoryWExtend16:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #1");
  case Match_InvalidMemoryWExtend32:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #2");
  case Match_InvalidMemoryWExtend64:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #3");
  case Match_InvalidMemoryWExtend128:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #4");
  case Match_InvalidMemoryXExtend8:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0");
  case Match_InvalidMemoryXExtend16:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #1");
  case Match_InvalidMemoryXExtend32:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #2");
  case Match_InvalidMemoryXExtend64:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #3");
  case Match_InvalidMemoryXExtend128:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #4");
  case Match_InvalidMemoryIndexed1:
    return Error(Loc, "index must be an integer in range [0, 4095].");
  case Match_InvalidMemoryIndexed2:
    return Error(Loc, "index must be a multiple of 2 in range [0, 8190].");
  case Match_InvalidMemoryIndexed4:
    return Error(Loc, "index must be a multiple of 4 in range [0, 16380].");
  case Match_InvalidMemoryIndexed8:
    return Error(Loc, "index must be a multiple of 8 in range [0, 32760].");
  case Match_InvalidMemoryIndexed16:
    return Error(Loc, "index must be a multiple of 16 in range [0, 65520].");
  case Match_InvalidImm0_0:
    return Error(Loc, "immediate must be 0.");
  case Match_InvalidImm0_1:
    return Error(Loc, "immediate must be an integer in range [0, 1].");
  case Match_InvalidImm0_3:
    return Error(Loc, "immediate must be an integer in range [0, 3].");
  case Match_InvalidImm0_7:
    return Error(Loc, "immediate must be an integer in range [0, 7].");
  case Match_InvalidImm0_15:
    return Error(Loc, "immediate must be an integer in range [0, 15].");
  case Match_InvalidImm0_31:
    return Error(Loc, "immediate must be an integer in range [0, 31].");
  case Match_InvalidImm0_63:
    return Error(Loc, "immediate must be an integer in range [0, 63].");
  case Match_InvalidImm0_127:
    return Error(Loc, "immediate must be an integer in range [0, 127].");
  case Match_InvalidImm0_255:
    return Error(Loc, "immediate must be an integer in range [0, 255].");
  case Match_InvalidImm0_65535:
    return Error(Loc, "immediate must be an integer in range [0, 65535].");
  case Match_InvalidImm1_8:
    return Error(Loc, "immediate must be an integer in range [1, 8].");
  case Match_InvalidImm1_16:
    return Error(Loc, "immediate must be an integer in range [1, 16].");
  case Match_InvalidImm1_32:
    return Error(Loc, "immediate must be an integer in range [1, 32].");
  case Match_InvalidImm1_64:
    return Error(Loc, "immediate must be an integer in range [1, 64].");
  case Match_InvalidMemoryIndexedRange2UImm0:
    return Error(Loc, "vector select offset must be the immediate range 0:1.");
  case Match_InvalidMemoryIndexedRange2UImm1:
    return Error(Loc, "vector select offset must be an immediate range of the "
                      "form <immf>:<imml>, where the first "
                      "immediate is a multiple of 2 in the range [0, 2], and "
                      "the second immediate is immf + 1.");
  case Match_InvalidMemoryIndexedRange2UImm2:
  case Match_InvalidMemoryIndexedRange2UImm3:
    return Error(
        Loc,
        "vector select offset must be an immediate range of the form "
        "<immf>:<imml>, "
        "where the first immediate is a multiple of 2 in the range [0, 6] or "
        "[0, 14] "
        "depending on the instruction, and the second immediate is immf + 1.");
  case Match_InvalidMemoryIndexedRange4UImm0:
    return Error(Loc, "vector select offset must be the immediate range 0:3.");
  case Match_InvalidMemoryIndexedRange4UImm1:
  case Match_InvalidMemoryIndexedRange4UImm2:
    return Error(
        Loc,
        "vector select offset must be an immediate range of the form "
        "<immf>:<imml>, "
        "where the first immediate is a multiple of 4 in the range [0, 4] or "
        "[0, 12] "
        "depending on the instruction, and the second immediate is immf + 3.");
  case Match_InvalidSVEAddSubImm8:
    return Error(Loc, "immediate must be an integer in range [0, 255]"
                      " with a shift amount of 0");
  case Match_InvalidSVEAddSubImm16:
  case Match_InvalidSVEAddSubImm32:
  case Match_InvalidSVEAddSubImm64:
    return Error(Loc, "immediate must be an integer in range [0, 255] or a "
                      "multiple of 256 in range [256, 65280]");
  case Match_InvalidSVECpyImm8:
    return Error(Loc, "immediate must be an integer in range [-128, 255]"
                      " with a shift amount of 0");
  case Match_InvalidSVECpyImm16:
    return Error(Loc, "immediate must be an integer in range [-128, 127] or a "
                      "multiple of 256 in range [-32768, 65280]");
  case Match_InvalidSVECpyImm32:
  case Match_InvalidSVECpyImm64:
    return Error(Loc, "immediate must be an integer in range [-128, 127] or a "
                      "multiple of 256 in range [-32768, 32512]");
  case Match_InvalidIndexRange0_0:
    return Error(Loc, "expected lane specifier '[0]'");
  case Match_InvalidIndexRange1_1:
    return Error(Loc, "expected lane specifier '[1]'");
  case Match_InvalidIndexRange0_15:
    return Error(Loc, "vector lane must be an integer in range [0, 15].");
  case Match_InvalidIndexRange0_7:
    return Error(Loc, "vector lane must be an integer in range [0, 7].");
  case Match_InvalidIndexRange0_3:
    return Error(Loc, "vector lane must be an integer in range [0, 3].");
  case Match_InvalidIndexRange0_1:
    return Error(Loc, "vector lane must be an integer in range [0, 1].");
  case Match_InvalidSVEIndexRange0_63:
    return Error(Loc, "vector lane must be an integer in range [0, 63].");
  case Match_InvalidSVEIndexRange0_31:
    return Error(Loc, "vector lane must be an integer in range [0, 31].");
  case Match_InvalidSVEIndexRange0_15:
    return Error(Loc, "vector lane must be an integer in range [0, 15].");
  case Match_InvalidSVEIndexRange0_7:
    return Error(Loc, "vector lane must be an integer in range [0, 7].");
  case Match_InvalidSVEIndexRange0_3:
    return Error(Loc, "vector lane must be an integer in range [0, 3].");
  case Match_InvalidLabel:
    return Error(Loc, "expected label or encodable integer pc offset");
  case Match_MRS:
    return Error(Loc, "expected readable system register");
  case Match_MSR:
  case Match_InvalidSVCR:
    return Error(Loc, "expected writable system register or pstate");
  case Match_InvalidComplexRotationEven:
    return Error(Loc, "complex rotation must be 0, 90, 180 or 270.");
  case Match_InvalidComplexRotationOdd:
    return Error(Loc, "complex rotation must be 90 or 270.");
  case Match_MnemonicFail: {
    std::string Suggestion = AArch64MnemonicSpellCheck(
        ((AArch64Operand &)*Operands[0]).getToken(),
        ComputeAvailableFeatures(STI->getFeatureBits()));
    return Error(Loc, "unrecognized instruction mnemonic" + Suggestion);
  }
  case Match_InvalidGPR64shifted8:
    return Error(Loc, "register must be x0..x30 or xzr, without shift");
  case Match_InvalidGPR64shifted16:
    return Error(Loc, "register must be x0..x30 or xzr, with required shift 'lsl #1'");
  case Match_InvalidGPR64shifted32:
    return Error(Loc, "register must be x0..x30 or xzr, with required shift 'lsl #2'");
  case Match_InvalidGPR64shifted64:
    return Error(Loc, "register must be x0..x30 or xzr, with required shift 'lsl #3'");
  case Match_InvalidGPR64shifted128:
    return Error(
        Loc, "register must be x0..x30 or xzr, with required shift 'lsl #4'");
  case Match_InvalidGPR64NoXZRshifted8:
    return Error(Loc, "register must be x0..x30 without shift");
  case Match_InvalidGPR64NoXZRshifted16:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #1'");
  case Match_InvalidGPR64NoXZRshifted32:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #2'");
  case Match_InvalidGPR64NoXZRshifted64:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #3'");
  case Match_InvalidGPR64NoXZRshifted128:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #4'");
  case Match_InvalidZPR32UXTW8:
  case Match_InvalidZPR32SXTW8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw)'");
  case Match_InvalidZPR32UXTW16:
  case Match_InvalidZPR32SXTW16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw) #1'");
  case Match_InvalidZPR32UXTW32:
  case Match_InvalidZPR32SXTW32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw) #2'");
  case Match_InvalidZPR32UXTW64:
  case Match_InvalidZPR32SXTW64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw) #3'");
  case Match_InvalidZPR64UXTW8:
  case Match_InvalidZPR64SXTW8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (uxtw|sxtw)'");
  case Match_InvalidZPR64UXTW16:
  case Match_InvalidZPR64SXTW16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (lsl|uxtw|sxtw) #1'");
  case Match_InvalidZPR64UXTW32:
  case Match_InvalidZPR64SXTW32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (lsl|uxtw|sxtw) #2'");
  case Match_InvalidZPR64UXTW64:
  case Match_InvalidZPR64SXTW64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (lsl|uxtw|sxtw) #3'");
  case Match_InvalidZPR32LSL8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s'");
  case Match_InvalidZPR32LSL16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, lsl #1'");
  case Match_InvalidZPR32LSL32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, lsl #2'");
  case Match_InvalidZPR32LSL64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, lsl #3'");
  case Match_InvalidZPR64LSL8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d'");
  case Match_InvalidZPR64LSL16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, lsl #1'");
  case Match_InvalidZPR64LSL32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, lsl #2'");
  case Match_InvalidZPR64LSL64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, lsl #3'");
  case Match_InvalidZPR0:
    return Error(Loc, "expected register without element width suffix");
  case Match_InvalidZPR8:
  case Match_InvalidZPR16:
  case Match_InvalidZPR32:
  case Match_InvalidZPR64:
  case Match_InvalidZPR128:
    return Error(Loc, "invalid element width");
  case Match_InvalidZPR_3b8:
    return Error(Loc, "Invalid restricted vector register, expected z0.b..z7.b");
  case Match_InvalidZPR_3b16:
    return Error(Loc, "Invalid restricted vector register, expected z0.h..z7.h");
  case Match_InvalidZPR_3b32:
    return Error(Loc, "Invalid restricted vector register, expected z0.s..z7.s");
  case Match_InvalidZPR_4b8:
    return Error(Loc,
                 "Invalid restricted vector register, expected z0.b..z15.b");
  case Match_InvalidZPR_4b16:
    return Error(Loc, "Invalid restricted vector register, expected z0.h..z15.h");
  case Match_InvalidZPR_4b32:
    return Error(Loc, "Invalid restricted vector register, expected z0.s..z15.s");
  case Match_InvalidZPR_4b64:
    return Error(Loc, "Invalid restricted vector register, expected z0.d..z15.d");
  case Match_InvalidSVEPattern:
    return Error(Loc, "invalid predicate pattern");
  case Match_InvalidSVEPPRorPNRAnyReg:
  case Match_InvalidSVEPPRorPNRBReg:
  case Match_InvalidSVEPredicateAnyReg:
  case Match_InvalidSVEPredicateBReg:
  case Match_InvalidSVEPredicateHReg:
  case Match_InvalidSVEPredicateSReg:
  case Match_InvalidSVEPredicateDReg:
    return Error(Loc, "invalid predicate register.");
  case Match_InvalidSVEPredicate3bAnyReg:
    return Error(Loc, "invalid restricted predicate register, expected p0..p7 (without element suffix)");
  case Match_InvalidSVEPNPredicateB_p8to15Reg:
  case Match_InvalidSVEPNPredicateH_p8to15Reg:
  case Match_InvalidSVEPNPredicateS_p8to15Reg:
  case Match_InvalidSVEPNPredicateD_p8to15Reg:
    return Error(Loc, "Invalid predicate register, expected PN in range "
                      "pn8..pn15 with element suffix.");
  case Match_InvalidSVEPNPredicateAny_p8to15Reg:
    return Error(Loc, "invalid restricted predicate-as-counter register "
                      "expected pn8..pn15");
  case Match_InvalidSVEPNPredicateBReg:
  case Match_InvalidSVEPNPredicateHReg:
  case Match_InvalidSVEPNPredicateSReg:
  case Match_InvalidSVEPNPredicateDReg:
    return Error(Loc, "Invalid predicate register, expected PN in range "
                      "pn0..pn15 with element suffix.");
  case Match_InvalidSVEVecLenSpecifier:
    return Error(Loc, "Invalid vector length specifier, expected VLx2 or VLx4");
  case Match_InvalidSVEPredicateListMul2x8:
  case Match_InvalidSVEPredicateListMul2x16:
  case Match_InvalidSVEPredicateListMul2x32:
  case Match_InvalidSVEPredicateListMul2x64:
    return Error(Loc, "Invalid vector list, expected list with 2 consecutive "
                      "predicate registers, where the first vector is a multiple of 2 "
                      "and with correct element type");
  case Match_InvalidSVEExactFPImmOperandHalfOne:
    return Error(Loc, "Invalid floating point constant, expected 0.5 or 1.0.");
  case Match_InvalidSVEExactFPImmOperandHalfTwo:
    return Error(Loc, "Invalid floating point constant, expected 0.5 or 2.0.");
  case Match_InvalidSVEExactFPImmOperandZeroOne:
    return Error(Loc, "Invalid floating point constant, expected 0.0 or 1.0.");
  case Match_InvalidMatrixTileVectorH8:
  case Match_InvalidMatrixTileVectorV8:
    return Error(Loc, "invalid matrix operand, expected za0h.b or za0v.b");
  case Match_InvalidMatrixTileVectorH16:
  case Match_InvalidMatrixTileVectorV16:
    return Error(Loc,
                 "invalid matrix operand, expected za[0-1]h.h or za[0-1]v.h");
  case Match_InvalidMatrixTileVectorH32:
  case Match_InvalidMatrixTileVectorV32:
    return Error(Loc,
                 "invalid matrix operand, expected za[0-3]h.s or za[0-3]v.s");
  case Match_InvalidMatrixTileVectorH64:
  case Match_InvalidMatrixTileVectorV64:
    return Error(Loc,
                 "invalid matrix operand, expected za[0-7]h.d or za[0-7]v.d");
  case Match_InvalidMatrixTileVectorH128:
  case Match_InvalidMatrixTileVectorV128:
    return Error(Loc,
                 "invalid matrix operand, expected za[0-15]h.q or za[0-15]v.q");
  case Match_InvalidMatrixTile32:
    return Error(Loc, "invalid matrix operand, expected za[0-3].s");
  case Match_InvalidMatrixTile64:
    return Error(Loc, "invalid matrix operand, expected za[0-7].d");
  case Match_InvalidMatrix:
    return Error(Loc, "invalid matrix operand, expected za");
  case Match_InvalidMatrix8:
    return Error(Loc, "invalid matrix operand, expected suffix .b");
  case Match_InvalidMatrix16:
    return Error(Loc, "invalid matrix operand, expected suffix .h");
  case Match_InvalidMatrix32:
    return Error(Loc, "invalid matrix operand, expected suffix .s");
  case Match_InvalidMatrix64:
    return Error(Loc, "invalid matrix operand, expected suffix .d");
  case Match_InvalidMatrixIndexGPR32_12_15:
    return Error(Loc, "operand must be a register in range [w12, w15]");
  case Match_InvalidMatrixIndexGPR32_8_11:
    return Error(Loc, "operand must be a register in range [w8, w11]");
  case Match_InvalidSVEVectorListMul2x8:
  case Match_InvalidSVEVectorListMul2x16:
  case Match_InvalidSVEVectorListMul2x32:
  case Match_InvalidSVEVectorListMul2x64:
  case Match_InvalidSVEVectorListMul2x128:
    return Error(Loc, "Invalid vector list, expected list with 2 consecutive "
                      "SVE vectors, where the first vector is a multiple of 2 "
                      "and with matching element types");
  case Match_InvalidSVEVectorListMul4x8:
  case Match_InvalidSVEVectorListMul4x16:
  case Match_InvalidSVEVectorListMul4x32:
  case Match_InvalidSVEVectorListMul4x64:
  case Match_InvalidSVEVectorListMul4x128:
    return Error(Loc, "Invalid vector list, expected list with 4 consecutive "
                      "SVE vectors, where the first vector is a multiple of 4 "
                      "and with matching element types");
  case Match_InvalidLookupTable:
    return Error(Loc, "Invalid lookup table, expected zt0");
  case Match_InvalidSVEVectorListStrided2x8:
  case Match_InvalidSVEVectorListStrided2x16:
  case Match_InvalidSVEVectorListStrided2x32:
  case Match_InvalidSVEVectorListStrided2x64:
    return Error(
        Loc,
        "Invalid vector list, expected list with each SVE vector in the list "
        "8 registers apart, and the first register in the range [z0, z7] or "
        "[z16, z23] and with correct element type");
  case Match_InvalidSVEVectorListStrided4x8:
  case Match_InvalidSVEVectorListStrided4x16:
  case Match_InvalidSVEVectorListStrided4x32:
  case Match_InvalidSVEVectorListStrided4x64:
    return Error(
        Loc,
        "Invalid vector list, expected list with each SVE vector in the list "
        "4 registers apart, and the first register in the range [z0, z3] or "
        "[z16, z19] and with correct element type");
  case Match_AddSubLSLImm3ShiftLarge:
    return Error(Loc,
      "expected 'lsl' with optional integer in range [0, 7]");
  default:
    llvm_unreachable("unexpected error code!");
  }
}

static const char *getSubtargetFeatureName(uint64_t Val);

bool AArch64AsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  assert(!Operands.empty() && "Unexpect empty operand list!");
  AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[0]);
  assert(Op.isToken() && "Leading operand should always be a mnemonic!");

  StringRef Tok = Op.getToken();
  unsigned NumOperands = Operands.size();

  if (NumOperands == 4 && Tok == "lsl") {
    AArch64Operand &Op2 = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
    if (Op2.isScalarReg() && Op3.isImm()) {
      const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
      if (Op3CE) {
        uint64_t Op3Val = Op3CE->getValue();
        uint64_t NewOp3Val = 0;
        uint64_t NewOp4Val = 0;
        if (AArch64MCRegisterClasses[AArch64::GPR32allRegClassID].contains(
                Op2.getReg())) {
          NewOp3Val = (32 - Op3Val) & 0x1f;
          NewOp4Val = 31 - Op3Val;
        } else {
          NewOp3Val = (64 - Op3Val) & 0x3f;
          NewOp4Val = 63 - Op3Val;
        }

        const MCExpr *NewOp3 = MCConstantExpr::create(NewOp3Val, getContext());
        const MCExpr *NewOp4 = MCConstantExpr::create(NewOp4Val, getContext());

        Operands[0] =
            AArch64Operand::CreateToken("ubfm", Op.getStartLoc(), getContext());
        Operands.push_back(AArch64Operand::CreateImm(
            NewOp4, Op3.getStartLoc(), Op3.getEndLoc(), getContext()));
        Operands[3] = AArch64Operand::CreateImm(NewOp3, Op3.getStartLoc(),
                                                Op3.getEndLoc(), getContext());
      }
    }
  } else if (NumOperands == 4 && Tok == "bfc") {
    // FIXME: Horrible hack to handle BFC->BFM alias.
    AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
    AArch64Operand LSBOp = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand WidthOp = static_cast<AArch64Operand &>(*Operands[3]);

    if (Op1.isScalarReg() && LSBOp.isImm() && WidthOp.isImm()) {
      const MCConstantExpr *LSBCE = dyn_cast<MCConstantExpr>(LSBOp.getImm());
      const MCConstantExpr *WidthCE = dyn_cast<MCConstantExpr>(WidthOp.getImm());

      if (LSBCE && WidthCE) {
        uint64_t LSB = LSBCE->getValue();
        uint64_t Width = WidthCE->getValue();

        uint64_t RegWidth = 0;
        if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                Op1.getReg()))
          RegWidth = 64;
        else
          RegWidth = 32;

        if (LSB >= RegWidth)
          return Error(LSBOp.getStartLoc(),
                       "expected integer in range [0, 31]");
        if (Width < 1 || Width > RegWidth)
          return Error(WidthOp.getStartLoc(),
                       "expected integer in range [1, 32]");

        uint64_t ImmR = 0;
        if (RegWidth == 32)
          ImmR = (32 - LSB) & 0x1f;
        else
          ImmR = (64 - LSB) & 0x3f;

        uint64_t ImmS = Width - 1;

        if (ImmR != 0 && ImmS >= ImmR)
          return Error(WidthOp.getStartLoc(),
                       "requested insert overflows register");

        const MCExpr *ImmRExpr = MCConstantExpr::create(ImmR, getContext());
        const MCExpr *ImmSExpr = MCConstantExpr::create(ImmS, getContext());
        Operands[0] =
            AArch64Operand::CreateToken("bfm", Op.getStartLoc(), getContext());
        Operands[2] = AArch64Operand::CreateReg(
            RegWidth == 32 ? AArch64::WZR : AArch64::XZR, RegKind::Scalar,
            SMLoc(), SMLoc(), getContext());
        Operands[3] = AArch64Operand::CreateImm(
            ImmRExpr, LSBOp.getStartLoc(), LSBOp.getEndLoc(), getContext());
        Operands.emplace_back(
            AArch64Operand::CreateImm(ImmSExpr, WidthOp.getStartLoc(),
                                      WidthOp.getEndLoc(), getContext()));
      }
    }
  } else if (NumOperands == 5) {
    // FIXME: Horrible hack to handle the BFI -> BFM, SBFIZ->SBFM, and
    // UBFIZ -> UBFM aliases.
    if (Tok == "bfi" || Tok == "sbfiz" || Tok == "ubfiz") {
      AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
      AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
      AArch64Operand &Op4 = static_cast<AArch64Operand &>(*Operands[4]);

      if (Op1.isScalarReg() && Op3.isImm() && Op4.isImm()) {
        const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
        const MCConstantExpr *Op4CE = dyn_cast<MCConstantExpr>(Op4.getImm());

        if (Op3CE && Op4CE) {
          uint64_t Op3Val = Op3CE->getValue();
          uint64_t Op4Val = Op4CE->getValue();

          uint64_t RegWidth = 0;
          if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                  Op1.getReg()))
            RegWidth = 64;
          else
            RegWidth = 32;

          if (Op3Val >= RegWidth)
            return Error(Op3.getStartLoc(),
                         "expected integer in range [0, 31]");
          if (Op4Val < 1 || Op4Val > RegWidth)
            return Error(Op4.getStartLoc(),
                         "expected integer in range [1, 32]");

          uint64_t NewOp3Val = 0;
          if (RegWidth == 32)
            NewOp3Val = (32 - Op3Val) & 0x1f;
          else
            NewOp3Val = (64 - Op3Val) & 0x3f;

          uint64_t NewOp4Val = Op4Val - 1;

          if (NewOp3Val != 0 && NewOp4Val >= NewOp3Val)
            return Error(Op4.getStartLoc(),
                         "requested insert overflows register");

          const MCExpr *NewOp3 =
              MCConstantExpr::create(NewOp3Val, getContext());
          const MCExpr *NewOp4 =
              MCConstantExpr::create(NewOp4Val, getContext());
          Operands[3] = AArch64Operand::CreateImm(
              NewOp3, Op3.getStartLoc(), Op3.getEndLoc(), getContext());
          Operands[4] = AArch64Operand::CreateImm(
              NewOp4, Op4.getStartLoc(), Op4.getEndLoc(), getContext());
          if (Tok == "bfi")
            Operands[0] = AArch64Operand::CreateToken("bfm", Op.getStartLoc(),
                                                      getContext());
          else if (Tok == "sbfiz")
            Operands[0] = AArch64Operand::CreateToken("sbfm", Op.getStartLoc(),
                                                      getContext());
          else if (Tok == "ubfiz")
            Operands[0] = AArch64Operand::CreateToken("ubfm", Op.getStartLoc(),
                                                      getContext());
          else
            llvm_unreachable("No valid mnemonic for alias?");
        }
      }

      // FIXME: Horrible hack to handle the BFXIL->BFM, SBFX->SBFM, and
      // UBFX -> UBFM aliases.
    } else if (NumOperands == 5 &&
               (Tok == "bfxil" || Tok == "sbfx" || Tok == "ubfx")) {
      AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
      AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
      AArch64Operand &Op4 = static_cast<AArch64Operand &>(*Operands[4]);

      if (Op1.isScalarReg() && Op3.isImm() && Op4.isImm()) {
        const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
        const MCConstantExpr *Op4CE = dyn_cast<MCConstantExpr>(Op4.getImm());

        if (Op3CE && Op4CE) {
          uint64_t Op3Val = Op3CE->getValue();
          uint64_t Op4Val = Op4CE->getValue();

          uint64_t RegWidth = 0;
          if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                  Op1.getReg()))
            RegWidth = 64;
          else
            RegWidth = 32;

          if (Op3Val >= RegWidth)
            return Error(Op3.getStartLoc(),
                         "expected integer in range [0, 31]");
          if (Op4Val < 1 || Op4Val > RegWidth)
            return Error(Op4.getStartLoc(),
                         "expected integer in range [1, 32]");

          uint64_t NewOp4Val = Op3Val + Op4Val - 1;

          if (NewOp4Val >= RegWidth || NewOp4Val < Op3Val)
            return Error(Op4.getStartLoc(),
                         "requested extract overflows register");

          const MCExpr *NewOp4 =
              MCConstantExpr::create(NewOp4Val, getContext());
          Operands[4] = AArch64Operand::CreateImm(
              NewOp4, Op4.getStartLoc(), Op4.getEndLoc(), getContext());
          if (Tok == "bfxil")
            Operands[0] = AArch64Operand::CreateToken("bfm", Op.getStartLoc(),
                                                      getContext());
          else if (Tok == "sbfx")
            Operands[0] = AArch64Operand::CreateToken("sbfm", Op.getStartLoc(),
                                                      getContext());
          else if (Tok == "ubfx")
            Operands[0] = AArch64Operand::CreateToken("ubfm", Op.getStartLoc(),
                                                      getContext());
          else
            llvm_unreachable("No valid mnemonic for alias?");
        }
      }
    }
  }

  // The Cyclone CPU and early successors didn't execute the zero-cycle zeroing
  // instruction for FP registers correctly in some rare circumstances. Convert
  // it to a safe instruction and warn (because silently changing someone's
  // assembly is rude).
  if (getSTI().hasFeature(AArch64::FeatureZCZeroingFPWorkaround) &&
      NumOperands == 4 && Tok == "movi") {
    AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
    AArch64Operand &Op2 = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
    if ((Op1.isToken() && Op2.isNeonVectorReg() && Op3.isImm()) ||
        (Op1.isNeonVectorReg() && Op2.isToken() && Op3.isImm())) {
      StringRef Suffix = Op1.isToken() ? Op1.getToken() : Op2.getToken();
      if (Suffix.lower() == ".2d" &&
          cast<MCConstantExpr>(Op3.getImm())->getValue() == 0) {
        Warning(IDLoc, "instruction movi.2d with immediate #0 may not function"
                " correctly on this CPU, converting to equivalent movi.16b");
        // Switch the suffix to .16b.
        unsigned Idx = Op1.isToken() ? 1 : 2;
        Operands[Idx] =
            AArch64Operand::CreateToken(".16b", IDLoc, getContext());
      }
    }
  }

  // FIXME: Horrible hack for sxtw and uxtw with Wn src and Xd dst operands.
  //        InstAlias can't quite handle this since the reg classes aren't
  //        subclasses.
  if (NumOperands == 3 && (Tok == "sxtw" || Tok == "uxtw")) {
    // The source register can be Wn here, but the matcher expects a
    // GPR64. Twiddle it here if necessary.
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[2]);
    if (Op.isScalarReg()) {
      unsigned Reg = getXRegFromWReg(Op.getReg());
      Operands[2] = AArch64Operand::CreateReg(Reg, RegKind::Scalar,
                                              Op.getStartLoc(), Op.getEndLoc(),
                                              getContext());
    }
  }
  // FIXME: Likewise for sxt[bh] with a Xd dst operand
  else if (NumOperands == 3 && (Tok == "sxtb" || Tok == "sxth")) {
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
    if (Op.isScalarReg() &&
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Op.getReg())) {
      // The source register can be Wn here, but the matcher expects a
      // GPR64. Twiddle it here if necessary.
      AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[2]);
      if (Op.isScalarReg()) {
        unsigned Reg = getXRegFromWReg(Op.getReg());
        Operands[2] = AArch64Operand::CreateReg(Reg, RegKind::Scalar,
                                                Op.getStartLoc(),
                                                Op.getEndLoc(), getContext());
      }
    }
  }
  // FIXME: Likewise for uxt[bh] with a Xd dst operand
  else if (NumOperands == 3 && (Tok == "uxtb" || Tok == "uxth")) {
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
    if (Op.isScalarReg() &&
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Op.getReg())) {
      // The source register can be Wn here, but the matcher expects a
      // GPR32. Twiddle it here if necessary.
      AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
      if (Op.isScalarReg()) {
        unsigned Reg = getWRegFromXReg(Op.getReg());
        Operands[1] = AArch64Operand::CreateReg(Reg, RegKind::Scalar,
                                                Op.getStartLoc(),
                                                Op.getEndLoc(), getContext());
      }
    }
  }

  MCInst Inst;
  FeatureBitset MissingFeatures;
  // First try to match against the secondary set of tables containing the
  // short-form NEON instructions (e.g. "fadd.2s v0, v1, v2").
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MissingFeatures,
                           MatchingInlineAsm, 1);

  // If that fails, try against the alternate table containing long-form NEON:
  // "fadd v0.2s, v1.2s, v2.2s"
  if (MatchResult != Match_Success) {
    // But first, save the short-form match result: we can use it in case the
    // long-form match also fails.
    auto ShortFormNEONErrorInfo = ErrorInfo;
    auto ShortFormNEONMatchResult = MatchResult;
    auto ShortFormNEONMissingFeatures = MissingFeatures;

    MatchResult =
        MatchInstructionImpl(Operands, Inst, ErrorInfo, MissingFeatures,
                             MatchingInlineAsm, 0);

    // Now, both matches failed, and the long-form match failed on the mnemonic
    // suffix token operand.  The short-form match failure is probably more
    // relevant: use it instead.
    if (MatchResult == Match_InvalidOperand && ErrorInfo == 1 &&
        Operands.size() > 1 && ((AArch64Operand &)*Operands[1]).isToken() &&
        ((AArch64Operand &)*Operands[1]).isTokenSuffix()) {
      MatchResult = ShortFormNEONMatchResult;
      ErrorInfo = ShortFormNEONErrorInfo;
      MissingFeatures = ShortFormNEONMissingFeatures;
    }
  }

  switch (MatchResult) {
  case Match_Success: {
    // Perform range checking and other semantic validations
    SmallVector<SMLoc, 8> OperandLocs;
    NumOperands = Operands.size();
    for (unsigned i = 1; i < NumOperands; ++i)
      OperandLocs.push_back(Operands[i]->getStartLoc());
    if (validateInstruction(Inst, IDLoc, OperandLocs))
      return true;

    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  }
  case Match_MissingFeature: {
    assert(MissingFeatures.any() && "Unknown missing feature!");
    // Special case the error message for the very common case where only
    // a single subtarget feature is missing (neon, e.g.).
    std::string Msg = "instruction requires:";
    for (unsigned i = 0, e = MissingFeatures.size(); i != e; ++i) {
      if (MissingFeatures[i]) {
        Msg += " ";
        Msg += getSubtargetFeatureName(i);
      }
    }
    return Error(IDLoc, Msg);
  }
  case Match_MnemonicFail:
    return showMatchError(IDLoc, MatchResult, ErrorInfo, Operands);
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;

    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction",
                     SMRange(IDLoc, getTok().getLoc()));

      ErrorLoc = ((AArch64Operand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    // If the match failed on a suffix token operand, tweak the diagnostic
    // accordingly.
    if (((AArch64Operand &)*Operands[ErrorInfo]).isToken() &&
        ((AArch64Operand &)*Operands[ErrorInfo]).isTokenSuffix())
      MatchResult = Match_InvalidSuffix;

    return showMatchError(ErrorLoc, MatchResult, ErrorInfo, Operands);
  }
  case Match_InvalidTiedOperand:
  case Match_InvalidMemoryIndexed1:
  case Match_InvalidMemoryIndexed2:
  case Match_InvalidMemoryIndexed4:
  case Match_InvalidMemoryIndexed8:
  case Match_InvalidMemoryIndexed16:
  case Match_InvalidCondCode:
  case Match_AddSubLSLImm3ShiftLarge:
  case Match_AddSubRegExtendSmall:
  case Match_AddSubRegExtendLarge:
  case Match_AddSubSecondSource:
  case Match_LogicalSecondSource:
  case Match_AddSubRegShift32:
  case Match_AddSubRegShift64:
  case Match_InvalidMovImm32Shift:
  case Match_InvalidMovImm64Shift:
  case Match_InvalidFPImm:
  case Match_InvalidMemoryWExtend8:
  case Match_InvalidMemoryWExtend16:
  case Match_InvalidMemoryWExtend32:
  case Match_InvalidMemoryWExtend64:
  case Match_InvalidMemoryWExtend128:
  case Match_InvalidMemoryXExtend8:
  case Match_InvalidMemoryXExtend16:
  case Match_InvalidMemoryXExtend32:
  case Match_InvalidMemoryXExtend64:
  case Match_InvalidMemoryXExtend128:
  case Match_InvalidMemoryIndexed1SImm4:
  case Match_InvalidMemoryIndexed2SImm4:
  case Match_InvalidMemoryIndexed3SImm4:
  case Match_InvalidMemoryIndexed4SImm4:
  case Match_InvalidMemoryIndexed1SImm6:
  case Match_InvalidMemoryIndexed16SImm4:
  case Match_InvalidMemoryIndexed32SImm4:
  case Match_InvalidMemoryIndexed4SImm7:
  case Match_InvalidMemoryIndexed8SImm7:
  case Match_InvalidMemoryIndexed16SImm7:
  case Match_InvalidMemoryIndexed8UImm5:
  case Match_InvalidMemoryIndexed8UImm3:
  case Match_InvalidMemoryIndexed4UImm5:
  case Match_InvalidMemoryIndexed2UImm5:
  case Match_InvalidMemoryIndexed1UImm6:
  case Match_InvalidMemoryIndexed2UImm6:
  case Match_InvalidMemoryIndexed4UImm6:
  case Match_InvalidMemoryIndexed8UImm6:
  case Match_InvalidMemoryIndexed16UImm6:
  case Match_InvalidMemoryIndexedSImm6:
  case Match_InvalidMemoryIndexedSImm5:
  case Match_InvalidMemoryIndexedSImm8:
  case Match_InvalidMemoryIndexedSImm9:
  case Match_InvalidMemoryIndexed16SImm9:
  case Match_InvalidMemoryIndexed8SImm10:
  case Match_InvalidImm0_0:
  case Match_InvalidImm0_1:
  case Match_InvalidImm0_3:
  case Match_InvalidImm0_7:
  case Match_InvalidImm0_15:
  case Match_InvalidImm0_31:
  case Match_InvalidImm0_63:
  case Match_InvalidImm0_127:
  case Match_InvalidImm0_255:
  case Match_InvalidImm0_65535:
  case Match_InvalidImm1_8:
  case Match_InvalidImm1_16:
  case Match_InvalidImm1_32:
  case Match_InvalidImm1_64:
  case Match_InvalidMemoryIndexedRange2UImm0:
  case Match_InvalidMemoryIndexedRange2UImm1:
  case Match_InvalidMemoryIndexedRange2UImm2:
  case Match_InvalidMemoryIndexedRange2UImm3:
  case Match_InvalidMemoryIndexedRange4UImm0:
  case Match_InvalidMemoryIndexedRange4UImm1:
  case Match_InvalidMemoryIndexedRange4UImm2:
  case Match_InvalidSVEAddSubImm8:
  case Match_InvalidSVEAddSubImm16:
  case Match_InvalidSVEAddSubImm32:
  case Match_InvalidSVEAddSubImm64:
  case Match_InvalidSVECpyImm8:
  case Match_InvalidSVECpyImm16:
  case Match_InvalidSVECpyImm32:
  case Match_InvalidSVECpyImm64:
  case Match_InvalidIndexRange0_0:
  case Match_InvalidIndexRange1_1:
  case Match_InvalidIndexRange0_15:
  case Match_InvalidIndexRange0_7:
  case Match_InvalidIndexRange0_3:
  case Match_InvalidIndexRange0_1:
  case Match_InvalidSVEIndexRange0_63:
  case Match_InvalidSVEIndexRange0_31:
  case Match_InvalidSVEIndexRange0_15:
  case Match_InvalidSVEIndexRange0_7:
  case Match_InvalidSVEIndexRange0_3:
  case Match_InvalidLabel:
  case Match_InvalidComplexRotationEven:
  case Match_InvalidComplexRotationOdd:
  case Match_InvalidGPR64shifted8:
  case Match_InvalidGPR64shifted16:
  case Match_InvalidGPR64shifted32:
  case Match_InvalidGPR64shifted64:
  case Match_InvalidGPR64shifted128:
  case Match_InvalidGPR64NoXZRshifted8:
  case Match_InvalidGPR64NoXZRshifted16:
  case Match_InvalidGPR64NoXZRshifted32:
  case Match_InvalidGPR64NoXZRshifted64:
  case Match_InvalidGPR64NoXZRshifted128:
  case Match_InvalidZPR32UXTW8:
  case Match_InvalidZPR32UXTW16:
  case Match_InvalidZPR32UXTW32:
  case Match_InvalidZPR32UXTW64:
  case Match_InvalidZPR32SXTW8:
  case Match_InvalidZPR32SXTW16:
  case Match_InvalidZPR32SXTW32:
  case Match_InvalidZPR32SXTW64:
  case Match_InvalidZPR64UXTW8:
  case Match_InvalidZPR64SXTW8:
  case Match_InvalidZPR64UXTW16:
  case Match_InvalidZPR64SXTW16:
  case Match_InvalidZPR64UXTW32:
  case Match_InvalidZPR64SXTW32:
  case Match_InvalidZPR64UXTW64:
  case Match_InvalidZPR64SXTW64:
  case Match_InvalidZPR32LSL8:
  case Match_InvalidZPR32LSL16:
  case Match_InvalidZPR32LSL32:
  case Match_InvalidZPR32LSL64:
  case Match_InvalidZPR64LSL8:
  case Match_InvalidZPR64LSL16:
  case Match_InvalidZPR64LSL32:
  case Match_InvalidZPR64LSL64:
  case Match_InvalidZPR0:
  case Match_InvalidZPR8:
  case Match_InvalidZPR16:
  case Match_InvalidZPR32:
  case Match_InvalidZPR64:
  case Match_InvalidZPR128:
  case Match_InvalidZPR_3b8:
  case Match_InvalidZPR_3b16:
  case Match_InvalidZPR_3b32:
  case Match_InvalidZPR_4b8:
  case Match_InvalidZPR_4b16:
  case Match_InvalidZPR_4b32:
  case Match_InvalidZPR_4b64:
  case Match_InvalidSVEPPRorPNRAnyReg:
  case Match_InvalidSVEPPRorPNRBReg:
  case Match_InvalidSVEPredicateAnyReg:
  case Match_InvalidSVEPattern:
  case Match_InvalidSVEVecLenSpecifier:
  case Match_InvalidSVEPredicateBReg:
  case Match_InvalidSVEPredicateHReg:
  case Match_InvalidSVEPredicateSReg:
  case Match_InvalidSVEPredicateDReg:
  case Match_InvalidSVEPredicate3bAnyReg:
  case Match_InvalidSVEPNPredicateB_p8to15Reg:
  case Match_InvalidSVEPNPredicateH_p8to15Reg:
  case Match_InvalidSVEPNPredicateS_p8to15Reg:
  case Match_InvalidSVEPNPredicateD_p8to15Reg:
  case Match_InvalidSVEPNPredicateAny_p8to15Reg:
  case Match_InvalidSVEPNPredicateBReg:
  case Match_InvalidSVEPNPredicateHReg:
  case Match_InvalidSVEPNPredicateSReg:
  case Match_InvalidSVEPNPredicateDReg:
  case Match_InvalidSVEPredicateListMul2x8:
  case Match_InvalidSVEPredicateListMul2x16:
  case Match_InvalidSVEPredicateListMul2x32:
  case Match_InvalidSVEPredicateListMul2x64:
  case Match_InvalidSVEExactFPImmOperandHalfOne:
  case Match_InvalidSVEExactFPImmOperandHalfTwo:
  case Match_InvalidSVEExactFPImmOperandZeroOne:
  case Match_InvalidMatrixTile32:
  case Match_InvalidMatrixTile64:
  case Match_InvalidMatrix:
  case Match_InvalidMatrix8:
  case Match_InvalidMatrix16:
  case Match_InvalidMatrix32:
  case Match_InvalidMatrix64:
  case Match_InvalidMatrixTileVectorH8:
  case Match_InvalidMatrixTileVectorH16:
  case Match_InvalidMatrixTileVectorH32:
  case Match_InvalidMatrixTileVectorH64:
  case Match_InvalidMatrixTileVectorH128:
  case Match_InvalidMatrixTileVectorV8:
  case Match_InvalidMatrixTileVectorV16:
  case Match_InvalidMatrixTileVectorV32:
  case Match_InvalidMatrixTileVectorV64:
  case Match_InvalidMatrixTileVectorV128:
  case Match_InvalidSVCR:
  case Match_InvalidMatrixIndexGPR32_12_15:
  case Match_InvalidMatrixIndexGPR32_8_11:
  case Match_InvalidLookupTable:
  case Match_InvalidSVEVectorListMul2x8:
  case Match_InvalidSVEVectorListMul2x16:
  case Match_InvalidSVEVectorListMul2x32:
  case Match_InvalidSVEVectorListMul2x64:
  case Match_InvalidSVEVectorListMul2x128:
  case Match_InvalidSVEVectorListMul4x8:
  case Match_InvalidSVEVectorListMul4x16:
  case Match_InvalidSVEVectorListMul4x32:
  case Match_InvalidSVEVectorListMul4x64:
  case Match_InvalidSVEVectorListMul4x128:
  case Match_InvalidSVEVectorListStrided2x8:
  case Match_InvalidSVEVectorListStrided2x16:
  case Match_InvalidSVEVectorListStrided2x32:
  case Match_InvalidSVEVectorListStrided2x64:
  case Match_InvalidSVEVectorListStrided4x8:
  case Match_InvalidSVEVectorListStrided4x16:
  case Match_InvalidSVEVectorListStrided4x32:
  case Match_InvalidSVEVectorListStrided4x64:
  case Match_MSR:
  case Match_MRS: {
    if (ErrorInfo >= Operands.size())
      return Error(IDLoc, "too few operands for instruction", SMRange(IDLoc, (*Operands.back()).getEndLoc()));
    // Any time we get here, there's nothing fancy to do. Just get the
    // operand SMLoc and display the diagnostic.
    SMLoc ErrorLoc = ((AArch64Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
    return showMatchError(ErrorLoc, MatchResult, ErrorInfo, Operands);
  }
  }

  llvm_unreachable("Implement any new match types added!");
}

/// ParseDirective parses the arm specific directives
bool AArch64AsmParser::ParseDirective(AsmToken DirectiveID) {
  const MCContext::Environment Format = getContext().getObjectFileType();
  bool IsMachO = Format == MCContext::IsMachO;
  bool IsCOFF = Format == MCContext::IsCOFF;

  auto IDVal = DirectiveID.getIdentifier().lower();
  SMLoc Loc = DirectiveID.getLoc();
  if (IDVal == ".arch")
    parseDirectiveArch(Loc);
  else if (IDVal == ".cpu")
    parseDirectiveCPU(Loc);
  else if (IDVal == ".tlsdesccall")
    parseDirectiveTLSDescCall(Loc);
  else if (IDVal == ".ltorg" || IDVal == ".pool")
    parseDirectiveLtorg(Loc);
  else if (IDVal == ".unreq")
    parseDirectiveUnreq(Loc);
  else if (IDVal == ".inst")
    parseDirectiveInst(Loc);
  else if (IDVal == ".cfi_negate_ra_state")
    parseDirectiveCFINegateRAState();
  else if (IDVal == ".cfi_b_key_frame")
    parseDirectiveCFIBKeyFrame();
  else if (IDVal == ".cfi_mte_tagged_frame")
    parseDirectiveCFIMTETaggedFrame();
  else if (IDVal == ".arch_extension")
    parseDirectiveArchExtension(Loc);
  else if (IDVal == ".variant_pcs")
    parseDirectiveVariantPCS(Loc);
  else if (IsMachO) {
    if (IDVal == MCLOHDirectiveName())
      parseDirectiveLOH(IDVal, Loc);
    else
      return true;
  } else if (IsCOFF) {
    if (IDVal == ".seh_stackalloc")
      parseDirectiveSEHAllocStack(Loc);
    else if (IDVal == ".seh_endprologue")
      parseDirectiveSEHPrologEnd(Loc);
    else if (IDVal == ".seh_save_r19r20_x")
      parseDirectiveSEHSaveR19R20X(Loc);
    else if (IDVal == ".seh_save_fplr")
      parseDirectiveSEHSaveFPLR(Loc);
    else if (IDVal == ".seh_save_fplr_x")
      parseDirectiveSEHSaveFPLRX(Loc);
    else if (IDVal == ".seh_save_reg")
      parseDirectiveSEHSaveReg(Loc);
    else if (IDVal == ".seh_save_reg_x")
      parseDirectiveSEHSaveRegX(Loc);
    else if (IDVal == ".seh_save_regp")
      parseDirectiveSEHSaveRegP(Loc);
    else if (IDVal == ".seh_save_regp_x")
      parseDirectiveSEHSaveRegPX(Loc);
    else if (IDVal == ".seh_save_lrpair")
      parseDirectiveSEHSaveLRPair(Loc);
    else if (IDVal == ".seh_save_freg")
      parseDirectiveSEHSaveFReg(Loc);
    else if (IDVal == ".seh_save_freg_x")
      parseDirectiveSEHSaveFRegX(Loc);
    else if (IDVal == ".seh_save_fregp")
      parseDirectiveSEHSaveFRegP(Loc);
    else if (IDVal == ".seh_save_fregp_x")
      parseDirectiveSEHSaveFRegPX(Loc);
    else if (IDVal == ".seh_set_fp")
      parseDirectiveSEHSetFP(Loc);
    else if (IDVal == ".seh_add_fp")
      parseDirectiveSEHAddFP(Loc);
    else if (IDVal == ".seh_nop")
      parseDirectiveSEHNop(Loc);
    else if (IDVal == ".seh_save_next")
      parseDirectiveSEHSaveNext(Loc);
    else if (IDVal == ".seh_startepilogue")
      parseDirectiveSEHEpilogStart(Loc);
    else if (IDVal == ".seh_endepilogue")
      parseDirectiveSEHEpilogEnd(Loc);
    else if (IDVal == ".seh_trap_frame")
      parseDirectiveSEHTrapFrame(Loc);
    else if (IDVal == ".seh_pushframe")
      parseDirectiveSEHMachineFrame(Loc);
    else if (IDVal == ".seh_context")
      parseDirectiveSEHContext(Loc);
    else if (IDVal == ".seh_ec_context")
      parseDirectiveSEHECContext(Loc);
    else if (IDVal == ".seh_clear_unwound_to_call")
      parseDirectiveSEHClearUnwoundToCall(Loc);
    else if (IDVal == ".seh_pac_sign_lr")
      parseDirectiveSEHPACSignLR(Loc);
    else if (IDVal == ".seh_save_any_reg")
      parseDirectiveSEHSaveAnyReg(Loc, false, false);
    else if (IDVal == ".seh_save_any_reg_p")
      parseDirectiveSEHSaveAnyReg(Loc, true, false);
    else if (IDVal == ".seh_save_any_reg_x")
      parseDirectiveSEHSaveAnyReg(Loc, false, true);
    else if (IDVal == ".seh_save_any_reg_px")
      parseDirectiveSEHSaveAnyReg(Loc, true, true);
    else
      return true;
  } else
    return true;
  return false;
}

static void ExpandCryptoAEK(const AArch64::ArchInfo &ArchInfo,
                            SmallVector<StringRef, 4> &RequestedExtensions) {
  const bool NoCrypto = llvm::is_contained(RequestedExtensions, "nocrypto");
  const bool Crypto = llvm::is_contained(RequestedExtensions, "crypto");

  if (!NoCrypto && Crypto) {
    // Map 'generic' (and others) to sha2 and aes, because
    // that was the traditional meaning of crypto.
    if (ArchInfo == AArch64::ARMV8_1A || ArchInfo == AArch64::ARMV8_2A ||
        ArchInfo == AArch64::ARMV8_3A) {
      RequestedExtensions.push_back("sha2");
      RequestedExtensions.push_back("aes");
    }
    if (ArchInfo == AArch64::ARMV8_4A || ArchInfo == AArch64::ARMV8_5A ||
        ArchInfo == AArch64::ARMV8_6A || ArchInfo == AArch64::ARMV8_7A ||
        ArchInfo == AArch64::ARMV8_8A || ArchInfo == AArch64::ARMV8_9A ||
        ArchInfo == AArch64::ARMV9A || ArchInfo == AArch64::ARMV9_1A ||
        ArchInfo == AArch64::ARMV9_2A || ArchInfo == AArch64::ARMV9_3A ||
        ArchInfo == AArch64::ARMV9_4A || ArchInfo == AArch64::ARMV8R) {
      RequestedExtensions.push_back("sm4");
      RequestedExtensions.push_back("sha3");
      RequestedExtensions.push_back("sha2");
      RequestedExtensions.push_back("aes");
    }
  } else if (NoCrypto) {
    // Map 'generic' (and others) to sha2 and aes, because
    // that was the traditional meaning of crypto.
    if (ArchInfo == AArch64::ARMV8_1A || ArchInfo == AArch64::ARMV8_2A ||
        ArchInfo == AArch64::ARMV8_3A) {
      RequestedExtensions.push_back("nosha2");
      RequestedExtensions.push_back("noaes");
    }
    if (ArchInfo == AArch64::ARMV8_4A || ArchInfo == AArch64::ARMV8_5A ||
        ArchInfo == AArch64::ARMV8_6A || ArchInfo == AArch64::ARMV8_7A ||
        ArchInfo == AArch64::ARMV8_8A || ArchInfo == AArch64::ARMV8_9A ||
        ArchInfo == AArch64::ARMV9A || ArchInfo == AArch64::ARMV9_1A ||
        ArchInfo == AArch64::ARMV9_2A || ArchInfo == AArch64::ARMV9_3A ||
        ArchInfo == AArch64::ARMV9_4A) {
      RequestedExtensions.push_back("nosm4");
      RequestedExtensions.push_back("nosha3");
      RequestedExtensions.push_back("nosha2");
      RequestedExtensions.push_back("noaes");
    }
  }
}

/// parseDirectiveArch
///   ::= .arch token
bool AArch64AsmParser::parseDirectiveArch(SMLoc L) {
  SMLoc ArchLoc = getLoc();

  StringRef Arch, ExtensionString;
  std::tie(Arch, ExtensionString) =
      getParser().parseStringToEndOfStatement().trim().split('+');

  const AArch64::ArchInfo *ArchInfo = AArch64::parseArch(Arch);
  if (!ArchInfo)
    return Error(ArchLoc, "unknown arch name");

  if (parseToken(AsmToken::EndOfStatement))
    return true;

  // Get the architecture and extension features.
  std::vector<StringRef> AArch64Features;
  AArch64Features.push_back(ArchInfo->ArchFeature);
  AArch64::getExtensionFeatures(ArchInfo->DefaultExts, AArch64Features);

  MCSubtargetInfo &STI = copySTI();
  std::vector<std::string> ArchFeatures(AArch64Features.begin(), AArch64Features.end());
  STI.setDefaultFeatures("generic", /*TuneCPU*/ "generic",
                         join(ArchFeatures.begin(), ArchFeatures.end(), ","));

  SmallVector<StringRef, 4> RequestedExtensions;
  if (!ExtensionString.empty())
    ExtensionString.split(RequestedExtensions, '+');

  ExpandCryptoAEK(*ArchInfo, RequestedExtensions);

  FeatureBitset Features = STI.getFeatureBits();
  setAvailableFeatures(ComputeAvailableFeatures(Features));
  for (auto Name : RequestedExtensions) {
    bool EnableFeature = !Name.consume_front_insensitive("no");

    for (const auto &Extension : ExtensionMap) {
      if (Extension.Name != Name)
        continue;

      if (Extension.Features.none())
        report_fatal_error("unsupported architectural extension: " + Name);

      FeatureBitset ToggleFeatures =
          EnableFeature
              ? STI.SetFeatureBitsTransitively(~Features & Extension.Features)
              : STI.ToggleFeature(Features & Extension.Features);
      setAvailableFeatures(ComputeAvailableFeatures(ToggleFeatures));
      break;
    }
  }
  return false;
}

/// parseDirectiveArchExtension
///   ::= .arch_extension [no]feature
bool AArch64AsmParser::parseDirectiveArchExtension(SMLoc L) {
  SMLoc ExtLoc = getLoc();

  StringRef Name = getParser().parseStringToEndOfStatement().trim();

  if (parseEOL())
    return true;

  bool EnableFeature = true;
  if (Name.starts_with_insensitive("no")) {
    EnableFeature = false;
    Name = Name.substr(2);
  }

  MCSubtargetInfo &STI = copySTI();
  FeatureBitset Features = STI.getFeatureBits();
  for (const auto &Extension : ExtensionMap) {
    if (Extension.Name != Name)
      continue;

    if (Extension.Features.none())
      return Error(ExtLoc, "unsupported architectural extension: " + Name);

    FeatureBitset ToggleFeatures =
        EnableFeature
            ? STI.SetFeatureBitsTransitively(~Features & Extension.Features)
            : STI.ToggleFeature(Features & Extension.Features);
    setAvailableFeatures(ComputeAvailableFeatures(ToggleFeatures));
    return false;
  }

  return Error(ExtLoc, "unknown architectural extension: " + Name);
}

static SMLoc incrementLoc(SMLoc L, int Offset) {
  return SMLoc::getFromPointer(L.getPointer() + Offset);
}

/// parseDirectiveCPU
///   ::= .cpu id
bool AArch64AsmParser::parseDirectiveCPU(SMLoc L) {
  SMLoc CurLoc = getLoc();

  StringRef CPU, ExtensionString;
  std::tie(CPU, ExtensionString) =
      getParser().parseStringToEndOfStatement().trim().split('+');

  if (parseToken(AsmToken::EndOfStatement))
    return true;

  SmallVector<StringRef, 4> RequestedExtensions;
  if (!ExtensionString.empty())
    ExtensionString.split(RequestedExtensions, '+');

  const llvm::AArch64::ArchInfo *CpuArch = llvm::AArch64::getArchForCpu(CPU);
  if (!CpuArch) {
    Error(CurLoc, "unknown CPU name");
    return false;
  }
  ExpandCryptoAEK(*CpuArch, RequestedExtensions);

  MCSubtargetInfo &STI = copySTI();
  STI.setDefaultFeatures(CPU, /*TuneCPU*/ CPU, "");
  CurLoc = incrementLoc(CurLoc, CPU.size());

  for (auto Name : RequestedExtensions) {
    // Advance source location past '+'.
    CurLoc = incrementLoc(CurLoc, 1);

    bool EnableFeature = !Name.consume_front_insensitive("no");

    bool FoundExtension = false;
    for (const auto &Extension : ExtensionMap) {
      if (Extension.Name != Name)
        continue;

      if (Extension.Features.none())
        report_fatal_error("unsupported architectural extension: " + Name);

      FeatureBitset Features = STI.getFeatureBits();
      FeatureBitset ToggleFeatures =
          EnableFeature
              ? STI.SetFeatureBitsTransitively(~Features & Extension.Features)
              : STI.ToggleFeature(Features & Extension.Features);
      setAvailableFeatures(ComputeAvailableFeatures(ToggleFeatures));
      FoundExtension = true;

      break;
    }

    if (!FoundExtension)
      Error(CurLoc, "unsupported architectural extension");

    CurLoc = incrementLoc(CurLoc, Name.size());
  }
  return false;
}

/// parseDirectiveInst
///  ::= .inst opcode [, ...]
bool AArch64AsmParser::parseDirectiveInst(SMLoc Loc) {
  if (getLexer().is(AsmToken::EndOfStatement))
    return Error(Loc, "expected expression following '.inst' directive");

  auto parseOp = [&]() -> bool {
    SMLoc L = getLoc();
    const MCExpr *Expr = nullptr;
    if (check(getParser().parseExpression(Expr), L, "expected expression"))
      return true;
    const MCConstantExpr *Value = dyn_cast_or_null<MCConstantExpr>(Expr);
    if (check(!Value, L, "expected constant expression"))
      return true;
    getTargetStreamer().emitInst(Value->getValue());
    return false;
  };

  return parseMany(parseOp);
}

// parseDirectiveTLSDescCall:
//   ::= .tlsdesccall symbol
bool AArch64AsmParser::parseDirectiveTLSDescCall(SMLoc L) {
  StringRef Name;
  if (check(getParser().parseIdentifier(Name), L, "expected symbol") ||
      parseToken(AsmToken::EndOfStatement))
    return true;

  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, getContext());
  Expr = AArch64MCExpr::create(Expr, AArch64MCExpr::VK_TLSDESC, getContext());

  MCInst Inst;
  Inst.setOpcode(AArch64::TLSDESCCALL);
  Inst.addOperand(MCOperand::createExpr(Expr));

  getParser().getStreamer().emitInstruction(Inst, getSTI());
  return false;
}

/// ::= .loh <lohName | lohId> label1, ..., labelN
/// The number of arguments depends on the loh identifier.
bool AArch64AsmParser::parseDirectiveLOH(StringRef IDVal, SMLoc Loc) {
  MCLOHType Kind;
  if (getTok().isNot(AsmToken::Identifier)) {
    if (getTok().isNot(AsmToken::Integer))
      return TokError("expected an identifier or a number in directive");
    // We successfully get a numeric value for the identifier.
    // Check if it is valid.
    int64_t Id = getTok().getIntVal();
    if (Id <= -1U && !isValidMCLOHType(Id))
      return TokError("invalid numeric identifier in directive");
    Kind = (MCLOHType)Id;
  } else {
    StringRef Name = getTok().getIdentifier();
    // We successfully parse an identifier.
    // Check if it is a recognized one.
    int Id = MCLOHNameToId(Name);

    if (Id == -1)
      return TokError("invalid identifier in directive");
    Kind = (MCLOHType)Id;
  }
  // Consume the identifier.
  Lex();
  // Get the number of arguments of this LOH.
  int NbArgs = MCLOHIdToNbArgs(Kind);

  assert(NbArgs != -1 && "Invalid number of arguments");

  SmallVector<MCSymbol *, 3> Args;
  for (int Idx = 0; Idx < NbArgs; ++Idx) {
    StringRef Name;
    if (getParser().parseIdentifier(Name))
      return TokError("expected identifier in directive");
    Args.push_back(getContext().getOrCreateSymbol(Name));

    if (Idx + 1 == NbArgs)
      break;
    if (parseComma())
      return true;
  }
  if (parseEOL())
    return true;

  getStreamer().emitLOHDirective((MCLOHType)Kind, Args);
  return false;
}

/// parseDirectiveLtorg
///  ::= .ltorg | .pool
bool AArch64AsmParser::parseDirectiveLtorg(SMLoc L) {
  if (parseEOL())
    return true;
  getTargetStreamer().emitCurrentConstantPool();
  return false;
}

/// parseDirectiveReq
///  ::= name .req registername
bool AArch64AsmParser::parseDirectiveReq(StringRef Name, SMLoc L) {
  Lex(); // Eat the '.req' token.
  SMLoc SRegLoc = getLoc();
  RegKind RegisterKind = RegKind::Scalar;
  MCRegister RegNum;
  ParseStatus ParseRes = tryParseScalarRegister(RegNum);

  if (!ParseRes.isSuccess()) {
    StringRef Kind;
    RegisterKind = RegKind::NeonVector;
    ParseRes = tryParseVectorRegister(RegNum, Kind, RegKind::NeonVector);

    if (ParseRes.isFailure())
      return true;

    if (ParseRes.isSuccess() && !Kind.empty())
      return Error(SRegLoc, "vector register without type specifier expected");
  }

  if (!ParseRes.isSuccess()) {
    StringRef Kind;
    RegisterKind = RegKind::SVEDataVector;
    ParseRes =
        tryParseVectorRegister(RegNum, Kind, RegKind::SVEDataVector);

    if (ParseRes.isFailure())
      return true;

    if (ParseRes.isSuccess() && !Kind.empty())
      return Error(SRegLoc,
                   "sve vector register without type specifier expected");
  }

  if (!ParseRes.isSuccess()) {
    StringRef Kind;
    RegisterKind = RegKind::SVEPredicateVector;
    ParseRes = tryParseVectorRegister(RegNum, Kind, RegKind::SVEPredicateVector);

    if (ParseRes.isFailure())
      return true;

    if (ParseRes.isSuccess() && !Kind.empty())
      return Error(SRegLoc,
                   "sve predicate register without type specifier expected");
  }

  if (!ParseRes.isSuccess())
    return Error(SRegLoc, "register name or alias expected");

  // Shouldn't be anything else.
  if (parseEOL())
    return true;

  auto pair = std::make_pair(RegisterKind, (unsigned) RegNum);
  if (RegisterReqs.insert(std::make_pair(Name, pair)).first->second != pair)
    Warning(L, "ignoring redefinition of register alias '" + Name + "'");

  return false;
}

/// parseDirectiveUneq
///  ::= .unreq registername
bool AArch64AsmParser::parseDirectiveUnreq(SMLoc L) {
  if (getTok().isNot(AsmToken::Identifier))
    return TokError("unexpected input in .unreq directive.");
  RegisterReqs.erase(getTok().getIdentifier().lower());
  Lex(); // Eat the identifier.
  return parseToken(AsmToken::EndOfStatement);
}

bool AArch64AsmParser::parseDirectiveCFINegateRAState() {
  if (parseEOL())
    return true;
  getStreamer().emitCFINegateRAState();
  return false;
}

/// parseDirectiveCFIBKeyFrame
/// ::= .cfi_b_key
bool AArch64AsmParser::parseDirectiveCFIBKeyFrame() {
  if (parseEOL())
    return true;
  getStreamer().emitCFIBKeyFrame();
  return false;
}

/// parseDirectiveCFIMTETaggedFrame
/// ::= .cfi_mte_tagged_frame
bool AArch64AsmParser::parseDirectiveCFIMTETaggedFrame() {
  if (parseEOL())
    return true;
  getStreamer().emitCFIMTETaggedFrame();
  return false;
}

/// parseDirectiveVariantPCS
/// ::= .variant_pcs symbolname
bool AArch64AsmParser::parseDirectiveVariantPCS(SMLoc L) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected symbol name");
  if (parseEOL())
    return true;
  getTargetStreamer().emitDirectiveVariantPCS(
      getContext().getOrCreateSymbol(Name));
  return false;
}

/// parseDirectiveSEHAllocStack
/// ::= .seh_stackalloc
bool AArch64AsmParser::parseDirectiveSEHAllocStack(SMLoc L) {
  int64_t Size;
  if (parseImmExpr(Size))
    return true;
  getTargetStreamer().emitARM64WinCFIAllocStack(Size);
  return false;
}

/// parseDirectiveSEHPrologEnd
/// ::= .seh_endprologue
bool AArch64AsmParser::parseDirectiveSEHPrologEnd(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIPrologEnd();
  return false;
}

/// parseDirectiveSEHSaveR19R20X
/// ::= .seh_save_r19r20_x
bool AArch64AsmParser::parseDirectiveSEHSaveR19R20X(SMLoc L) {
  int64_t Offset;
  if (parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveR19R20X(Offset);
  return false;
}

/// parseDirectiveSEHSaveFPLR
/// ::= .seh_save_fplr
bool AArch64AsmParser::parseDirectiveSEHSaveFPLR(SMLoc L) {
  int64_t Offset;
  if (parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveFPLR(Offset);
  return false;
}

/// parseDirectiveSEHSaveFPLRX
/// ::= .seh_save_fplr_x
bool AArch64AsmParser::parseDirectiveSEHSaveFPLRX(SMLoc L) {
  int64_t Offset;
  if (parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveFPLRX(Offset);
  return false;
}

/// parseDirectiveSEHSaveReg
/// ::= .seh_save_reg
bool AArch64AsmParser::parseDirectiveSEHSaveReg(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::X0, AArch64::X19, AArch64::LR) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveReg(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveRegX
/// ::= .seh_save_reg_x
bool AArch64AsmParser::parseDirectiveSEHSaveRegX(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::X0, AArch64::X19, AArch64::LR) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveRegX(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveRegP
/// ::= .seh_save_regp
bool AArch64AsmParser::parseDirectiveSEHSaveRegP(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::X0, AArch64::X19, AArch64::FP) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveRegP(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveRegPX
/// ::= .seh_save_regp_x
bool AArch64AsmParser::parseDirectiveSEHSaveRegPX(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::X0, AArch64::X19, AArch64::FP) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveRegPX(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveLRPair
/// ::= .seh_save_lrpair
bool AArch64AsmParser::parseDirectiveSEHSaveLRPair(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  L = getLoc();
  if (parseRegisterInRange(Reg, AArch64::X0, AArch64::X19, AArch64::LR) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  if (check(((Reg - 19) % 2 != 0), L,
            "expected register with even offset from x19"))
    return true;
  getTargetStreamer().emitARM64WinCFISaveLRPair(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveFReg
/// ::= .seh_save_freg
bool AArch64AsmParser::parseDirectiveSEHSaveFReg(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::D0, AArch64::D8, AArch64::D15) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveFReg(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveFRegX
/// ::= .seh_save_freg_x
bool AArch64AsmParser::parseDirectiveSEHSaveFRegX(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::D0, AArch64::D8, AArch64::D15) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveFRegX(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveFRegP
/// ::= .seh_save_fregp
bool AArch64AsmParser::parseDirectiveSEHSaveFRegP(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::D0, AArch64::D8, AArch64::D14) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveFRegP(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSaveFRegPX
/// ::= .seh_save_fregp_x
bool AArch64AsmParser::parseDirectiveSEHSaveFRegPX(SMLoc L) {
  unsigned Reg;
  int64_t Offset;
  if (parseRegisterInRange(Reg, AArch64::D0, AArch64::D8, AArch64::D14) ||
      parseComma() || parseImmExpr(Offset))
    return true;
  getTargetStreamer().emitARM64WinCFISaveFRegPX(Reg, Offset);
  return false;
}

/// parseDirectiveSEHSetFP
/// ::= .seh_set_fp
bool AArch64AsmParser::parseDirectiveSEHSetFP(SMLoc L) {
  getTargetStreamer().emitARM64WinCFISetFP();
  return false;
}

/// parseDirectiveSEHAddFP
/// ::= .seh_add_fp
bool AArch64AsmParser::parseDirectiveSEHAddFP(SMLoc L) {
  int64_t Size;
  if (parseImmExpr(Size))
    return true;
  getTargetStreamer().emitARM64WinCFIAddFP(Size);
  return false;
}

/// parseDirectiveSEHNop
/// ::= .seh_nop
bool AArch64AsmParser::parseDirectiveSEHNop(SMLoc L) {
  getTargetStreamer().emitARM64WinCFINop();
  return false;
}

/// parseDirectiveSEHSaveNext
/// ::= .seh_save_next
bool AArch64AsmParser::parseDirectiveSEHSaveNext(SMLoc L) {
  getTargetStreamer().emitARM64WinCFISaveNext();
  return false;
}

/// parseDirectiveSEHEpilogStart
/// ::= .seh_startepilogue
bool AArch64AsmParser::parseDirectiveSEHEpilogStart(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIEpilogStart();
  return false;
}

/// parseDirectiveSEHEpilogEnd
/// ::= .seh_endepilogue
bool AArch64AsmParser::parseDirectiveSEHEpilogEnd(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIEpilogEnd();
  return false;
}

/// parseDirectiveSEHTrapFrame
/// ::= .seh_trap_frame
bool AArch64AsmParser::parseDirectiveSEHTrapFrame(SMLoc L) {
  getTargetStreamer().emitARM64WinCFITrapFrame();
  return false;
}

/// parseDirectiveSEHMachineFrame
/// ::= .seh_pushframe
bool AArch64AsmParser::parseDirectiveSEHMachineFrame(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIMachineFrame();
  return false;
}

/// parseDirectiveSEHContext
/// ::= .seh_context
bool AArch64AsmParser::parseDirectiveSEHContext(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIContext();
  return false;
}

/// parseDirectiveSEHECContext
/// ::= .seh_ec_context
bool AArch64AsmParser::parseDirectiveSEHECContext(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIECContext();
  return false;
}

/// parseDirectiveSEHClearUnwoundToCall
/// ::= .seh_clear_unwound_to_call
bool AArch64AsmParser::parseDirectiveSEHClearUnwoundToCall(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIClearUnwoundToCall();
  return false;
}

/// parseDirectiveSEHPACSignLR
/// ::= .seh_pac_sign_lr
bool AArch64AsmParser::parseDirectiveSEHPACSignLR(SMLoc L) {
  getTargetStreamer().emitARM64WinCFIPACSignLR();
  return false;
}

/// parseDirectiveSEHSaveAnyReg
/// ::= .seh_save_any_reg
/// ::= .seh_save_any_reg_p
/// ::= .seh_save_any_reg_x
/// ::= .seh_save_any_reg_px
bool AArch64AsmParser::parseDirectiveSEHSaveAnyReg(SMLoc L, bool Paired,
                                                   bool Writeback) {
  MCRegister Reg;
  SMLoc Start, End;
  int64_t Offset;
  if (check(parseRegister(Reg, Start, End), getLoc(), "expected register") ||
      parseComma() || parseImmExpr(Offset))
    return true;

  if (Reg == AArch64::FP || Reg == AArch64::LR ||
      (Reg >= AArch64::X0 && Reg <= AArch64::X28)) {
    if (Offset < 0 || Offset % (Paired || Writeback ? 16 : 8))
      return Error(L, "invalid save_any_reg offset");
    unsigned EncodedReg;
    if (Reg == AArch64::FP)
      EncodedReg = 29;
    else if (Reg == AArch64::LR)
      EncodedReg = 30;
    else
      EncodedReg = Reg - AArch64::X0;
    if (Paired) {
      if (Reg == AArch64::LR)
        return Error(Start, "lr cannot be paired with another register");
      if (Writeback)
        getTargetStreamer().emitARM64WinCFISaveAnyRegIPX(EncodedReg, Offset);
      else
        getTargetStreamer().emitARM64WinCFISaveAnyRegIP(EncodedReg, Offset);
    } else {
      if (Writeback)
        getTargetStreamer().emitARM64WinCFISaveAnyRegIX(EncodedReg, Offset);
      else
        getTargetStreamer().emitARM64WinCFISaveAnyRegI(EncodedReg, Offset);
    }
  } else if (Reg >= AArch64::D0 && Reg <= AArch64::D31) {
    unsigned EncodedReg = Reg - AArch64::D0;
    if (Offset < 0 || Offset % (Paired || Writeback ? 16 : 8))
      return Error(L, "invalid save_any_reg offset");
    if (Paired) {
      if (Reg == AArch64::D31)
        return Error(Start, "d31 cannot be paired with another register");
      if (Writeback)
        getTargetStreamer().emitARM64WinCFISaveAnyRegDPX(EncodedReg, Offset);
      else
        getTargetStreamer().emitARM64WinCFISaveAnyRegDP(EncodedReg, Offset);
    } else {
      if (Writeback)
        getTargetStreamer().emitARM64WinCFISaveAnyRegDX(EncodedReg, Offset);
      else
        getTargetStreamer().emitARM64WinCFISaveAnyRegD(EncodedReg, Offset);
    }
  } else if (Reg >= AArch64::Q0 && Reg <= AArch64::Q31) {
    unsigned EncodedReg = Reg - AArch64::Q0;
    if (Offset < 0 || Offset % 16)
      return Error(L, "invalid save_any_reg offset");
    if (Paired) {
      if (Reg == AArch64::Q31)
        return Error(Start, "q31 cannot be paired with another register");
      if (Writeback)
        getTargetStreamer().emitARM64WinCFISaveAnyRegQPX(EncodedReg, Offset);
      else
        getTargetStreamer().emitARM64WinCFISaveAnyRegQP(EncodedReg, Offset);
    } else {
      if (Writeback)
        getTargetStreamer().emitARM64WinCFISaveAnyRegQX(EncodedReg, Offset);
      else
        getTargetStreamer().emitARM64WinCFISaveAnyRegQ(EncodedReg, Offset);
    }
  } else {
    return Error(Start, "save_any_reg register must be x, q or d register");
  }
  return false;
}

bool AArch64AsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  // Try @AUTH expressions: they're more complex than the usual symbol variants.
  if (!parseAuthExpr(Res, EndLoc))
    return false;
  return getParser().parsePrimaryExpr(Res, EndLoc, nullptr);
}

///  parseAuthExpr
///  ::= _sym@AUTH(ib,123[,addr])
///  ::= (_sym + 5)@AUTH(ib,123[,addr])
///  ::= (_sym - 5)@AUTH(ib,123[,addr])
bool AArch64AsmParser::parseAuthExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  MCContext &Ctx = getContext();

  AsmToken Tok = Parser.getTok();

  // Look for '_sym@AUTH' ...
  if (Tok.is(AsmToken::Identifier) && Tok.getIdentifier().ends_with("@AUTH")) {
    StringRef SymName = Tok.getIdentifier().drop_back(strlen("@AUTH"));
    if (SymName.contains('@'))
      return TokError(
          "combination of @AUTH with other modifiers not supported");
    Res = MCSymbolRefExpr::create(Ctx.getOrCreateSymbol(SymName), Ctx);

    Parser.Lex(); // Eat the identifier.
  } else {
    // ... or look for a more complex symbol reference, such as ...
    SmallVector<AsmToken, 6> Tokens;

    // ... '"_long sym"@AUTH' ...
    if (Tok.is(AsmToken::String))
      Tokens.resize(2);
    // ... or '(_sym + 5)@AUTH'.
    else if (Tok.is(AsmToken::LParen))
      Tokens.resize(6);
    else
      return true;

    if (Parser.getLexer().peekTokens(Tokens) != Tokens.size())
      return true;

    // In either case, the expression ends with '@' 'AUTH'.
    if (Tokens[Tokens.size() - 2].isNot(AsmToken::At) ||
        Tokens[Tokens.size() - 1].isNot(AsmToken::Identifier) ||
        Tokens[Tokens.size() - 1].getIdentifier() != "AUTH")
      return true;

    if (Tok.is(AsmToken::String)) {
      StringRef SymName;
      if (Parser.parseIdentifier(SymName))
        return true;
      Res = MCSymbolRefExpr::create(Ctx.getOrCreateSymbol(SymName), Ctx);
    } else {
      if (Parser.parsePrimaryExpr(Res, EndLoc, nullptr))
        return true;
    }

    Parser.Lex(); // '@'
    Parser.Lex(); // 'AUTH'
  }

  // At this point, we encountered "<id>@AUTH". There is no fallback anymore.
  if (parseToken(AsmToken::LParen, "expected '('"))
    return true;

  if (Parser.getTok().isNot(AsmToken::Identifier))
    return TokError("expected key name");

  StringRef KeyStr = Parser.getTok().getIdentifier();
  auto KeyIDOrNone = AArch64StringToPACKeyID(KeyStr);
  if (!KeyIDOrNone)
    return TokError("invalid key '" + KeyStr + "'");
  Parser.Lex();

  if (parseToken(AsmToken::Comma, "expected ','"))
    return true;

  if (Parser.getTok().isNot(AsmToken::Integer))
    return TokError("expected integer discriminator");
  int64_t Discriminator = Parser.getTok().getIntVal();

  if (!isUInt<16>(Discriminator))
    return TokError("integer discriminator " + Twine(Discriminator) +
                    " out of range [0, 0xFFFF]");
  Parser.Lex();

  bool UseAddressDiversity = false;
  if (Parser.getTok().is(AsmToken::Comma)) {
    Parser.Lex();
    if (Parser.getTok().isNot(AsmToken::Identifier) ||
        Parser.getTok().getIdentifier() != "addr")
      return TokError("expected 'addr'");
    UseAddressDiversity = true;
    Parser.Lex();
  }

  EndLoc = Parser.getTok().getEndLoc();
  if (parseToken(AsmToken::RParen, "expected ')'"))
    return true;

  Res = AArch64AuthMCExpr::create(Res, Discriminator, *KeyIDOrNone,
                                  UseAddressDiversity, Ctx);
  return false;
}

bool
AArch64AsmParser::classifySymbolRef(const MCExpr *Expr,
                                    AArch64MCExpr::VariantKind &ELFRefKind,
                                    MCSymbolRefExpr::VariantKind &DarwinRefKind,
                                    int64_t &Addend) {
  ELFRefKind = AArch64MCExpr::VK_INVALID;
  DarwinRefKind = MCSymbolRefExpr::VK_None;
  Addend = 0;

  if (const AArch64MCExpr *AE = dyn_cast<AArch64MCExpr>(Expr)) {
    ELFRefKind = AE->getKind();
    Expr = AE->getSubExpr();
  }

  const MCSymbolRefExpr *SE = dyn_cast<MCSymbolRefExpr>(Expr);
  if (SE) {
    // It's a simple symbol reference with no addend.
    DarwinRefKind = SE->getKind();
    return true;
  }

  // Check that it looks like a symbol + an addend
  MCValue Res;
  bool Relocatable = Expr->evaluateAsRelocatable(Res, nullptr, nullptr);
  if (!Relocatable || Res.getSymB())
    return false;

  // Treat expressions with an ELFRefKind (like ":abs_g1:3", or
  // ":abs_g1:x" where x is constant) as symbolic even if there is no symbol.
  if (!Res.getSymA() && ELFRefKind == AArch64MCExpr::VK_INVALID)
    return false;

  if (Res.getSymA())
    DarwinRefKind = Res.getSymA()->getKind();
  Addend = Res.getConstant();

  // It's some symbol reference + a constant addend, but really
  // shouldn't use both Darwin and ELF syntax.
  return ELFRefKind == AArch64MCExpr::VK_INVALID ||
         DarwinRefKind == MCSymbolRefExpr::VK_None;
}

/// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAArch64AsmParser() {
  RegisterMCAsmParser<AArch64AsmParser> X(getTheAArch64leTarget());
  RegisterMCAsmParser<AArch64AsmParser> Y(getTheAArch64beTarget());
  RegisterMCAsmParser<AArch64AsmParser> Z(getTheARM64Target());
  RegisterMCAsmParser<AArch64AsmParser> W(getTheARM64_32Target());
  RegisterMCAsmParser<AArch64AsmParser> V(getTheAArch64_32Target());
}

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "AArch64GenAsmMatcher.inc"

// Define this matcher function after the auto-generated include so we
// have the match class enum definitions.
unsigned AArch64AsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                      unsigned Kind) {
  AArch64Operand &Op = static_cast<AArch64Operand &>(AsmOp);

  auto MatchesOpImmediate = [&](int64_t ExpectedVal) -> MatchResultTy {
    if (!Op.isImm())
      return Match_InvalidOperand;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Op.getImm());
    if (!CE)
      return Match_InvalidOperand;
    if (CE->getValue() == ExpectedVal)
      return Match_Success;
    return Match_InvalidOperand;
  };

  switch (Kind) {
  default:
    return Match_InvalidOperand;
  case MCK_MPR:
    // If the Kind is a token for the MPR register class which has the "za"
    // register (SME accumulator array), check if the asm is a literal "za"
    // token. This is for the "smstart za" alias that defines the register
    // as a literal token.
    if (Op.isTokenEqual("za"))
      return Match_Success;
    return Match_InvalidOperand;

    // If the kind is a token for a literal immediate, check if our asm operand
    // matches. This is for InstAliases which have a fixed-value immediate in
    // the asm string, such as hints which are parsed into a specific
    // instruction definition.
#define MATCH_HASH(N)                                                          \
  case MCK__HASH_##N:                                                          \
    return MatchesOpImmediate(N);
    MATCH_HASH(0)
    MATCH_HASH(1)
    MATCH_HASH(2)
    MATCH_HASH(3)
    MATCH_HASH(4)
    MATCH_HASH(6)
    MATCH_HASH(7)
    MATCH_HASH(8)
    MATCH_HASH(10)
    MATCH_HASH(12)
    MATCH_HASH(14)
    MATCH_HASH(16)
    MATCH_HASH(24)
    MATCH_HASH(25)
    MATCH_HASH(26)
    MATCH_HASH(27)
    MATCH_HASH(28)
    MATCH_HASH(29)
    MATCH_HASH(30)
    MATCH_HASH(31)
    MATCH_HASH(32)
    MATCH_HASH(40)
    MATCH_HASH(48)
    MATCH_HASH(64)
#undef MATCH_HASH
#define MATCH_HASH_MINUS(N)                                                    \
  case MCK__HASH__MINUS_##N:                                                   \
    return MatchesOpImmediate(-N);
    MATCH_HASH_MINUS(4)
    MATCH_HASH_MINUS(8)
    MATCH_HASH_MINUS(16)
#undef MATCH_HASH_MINUS
  }
}

ParseStatus AArch64AsmParser::tryParseGPRSeqPair(OperandVector &Operands) {

  SMLoc S = getLoc();

  if (getTok().isNot(AsmToken::Identifier))
    return Error(S, "expected register");

  MCRegister FirstReg;
  ParseStatus Res = tryParseScalarRegister(FirstReg);
  if (!Res.isSuccess())
    return Error(S, "expected first even register of a consecutive same-size "
                    "even/odd register pair");

  const MCRegisterClass &WRegClass =
      AArch64MCRegisterClasses[AArch64::GPR32RegClassID];
  const MCRegisterClass &XRegClass =
      AArch64MCRegisterClasses[AArch64::GPR64RegClassID];

  bool isXReg = XRegClass.contains(FirstReg),
       isWReg = WRegClass.contains(FirstReg);
  if (!isXReg && !isWReg)
    return Error(S, "expected first even register of a consecutive same-size "
                    "even/odd register pair");

  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  unsigned FirstEncoding = RI->getEncodingValue(FirstReg);

  if (FirstEncoding & 0x1)
    return Error(S, "expected first even register of a consecutive same-size "
                    "even/odd register pair");

  if (getTok().isNot(AsmToken::Comma))
    return Error(getLoc(), "expected comma");
  // Eat the comma
  Lex();

  SMLoc E = getLoc();
  MCRegister SecondReg;
  Res = tryParseScalarRegister(SecondReg);
  if (!Res.isSuccess())
    return Error(E, "expected second odd register of a consecutive same-size "
                    "even/odd register pair");

  if (RI->getEncodingValue(SecondReg) != FirstEncoding + 1 ||
      (isXReg && !XRegClass.contains(SecondReg)) ||
      (isWReg && !WRegClass.contains(SecondReg)))
    return Error(E, "expected second odd register of a consecutive same-size "
                    "even/odd register pair");

  unsigned Pair = 0;
  if (isXReg) {
    Pair = RI->getMatchingSuperReg(FirstReg, AArch64::sube64,
           &AArch64MCRegisterClasses[AArch64::XSeqPairsClassRegClassID]);
  } else {
    Pair = RI->getMatchingSuperReg(FirstReg, AArch64::sube32,
           &AArch64MCRegisterClasses[AArch64::WSeqPairsClassRegClassID]);
  }

  Operands.push_back(AArch64Operand::CreateReg(Pair, RegKind::Scalar, S,
      getLoc(), getContext()));

  return ParseStatus::Success;
}

template <bool ParseShiftExtend, bool ParseSuffix>
ParseStatus AArch64AsmParser::tryParseSVEDataVector(OperandVector &Operands) {
  const SMLoc S = getLoc();
  // Check for a SVE vector register specifier first.
  MCRegister RegNum;
  StringRef Kind;

  ParseStatus Res =
      tryParseVectorRegister(RegNum, Kind, RegKind::SVEDataVector);

  if (!Res.isSuccess())
    return Res;

  if (ParseSuffix && Kind.empty())
    return ParseStatus::NoMatch;

  const auto &KindRes = parseVectorKind(Kind, RegKind::SVEDataVector);
  if (!KindRes)
    return ParseStatus::NoMatch;

  unsigned ElementWidth = KindRes->second;

  // No shift/extend is the default.
  if (!ParseShiftExtend || getTok().isNot(AsmToken::Comma)) {
    Operands.push_back(AArch64Operand::CreateVectorReg(
        RegNum, RegKind::SVEDataVector, ElementWidth, S, S, getContext()));

    ParseStatus Res = tryParseVectorIndex(Operands);
    if (Res.isFailure())
      return ParseStatus::Failure;
    return ParseStatus::Success;
  }

  // Eat the comma
  Lex();

  // Match the shift
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 1> ExtOpnd;
  Res = tryParseOptionalShiftExtend(ExtOpnd);
  if (!Res.isSuccess())
    return Res;

  auto Ext = static_cast<AArch64Operand *>(ExtOpnd.back().get());
  Operands.push_back(AArch64Operand::CreateVectorReg(
      RegNum, RegKind::SVEDataVector, ElementWidth, S, Ext->getEndLoc(),
      getContext(), Ext->getShiftExtendType(), Ext->getShiftExtendAmount(),
      Ext->hasShiftExtendAmount()));

  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseSVEPattern(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  SMLoc SS = getLoc();
  const AsmToken &TokE = getTok();
  bool IsHash = TokE.is(AsmToken::Hash);

  if (!IsHash && TokE.isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  int64_t Pattern;
  if (IsHash) {
    Lex(); // Eat hash

    // Parse the immediate operand.
    const MCExpr *ImmVal;
    SS = getLoc();
    if (Parser.parseExpression(ImmVal))
      return ParseStatus::Failure;

    auto *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return TokError("invalid operand for instruction");

    Pattern = MCE->getValue();
  } else {
    // Parse the pattern
    auto Pat = AArch64SVEPredPattern::lookupSVEPREDPATByName(TokE.getString());
    if (!Pat)
      return ParseStatus::NoMatch;

    Lex();
    Pattern = Pat->Encoding;
    assert(Pattern >= 0 && Pattern < 32);
  }

  Operands.push_back(
      AArch64Operand::CreateImm(MCConstantExpr::create(Pattern, getContext()),
                                SS, getLoc(), getContext()));

  return ParseStatus::Success;
}

ParseStatus
AArch64AsmParser::tryParseSVEVecLenSpecifier(OperandVector &Operands) {
  int64_t Pattern;
  SMLoc SS = getLoc();
  const AsmToken &TokE = getTok();
  // Parse the pattern
  auto Pat = AArch64SVEVecLenSpecifier::lookupSVEVECLENSPECIFIERByName(
      TokE.getString());
  if (!Pat)
    return ParseStatus::NoMatch;

  Lex();
  Pattern = Pat->Encoding;
  assert(Pattern >= 0 && Pattern <= 1 && "Pattern does not exist");

  Operands.push_back(
      AArch64Operand::CreateImm(MCConstantExpr::create(Pattern, getContext()),
                                SS, getLoc(), getContext()));

  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseGPR64x8(OperandVector &Operands) {
  SMLoc SS = getLoc();

  MCRegister XReg;
  if (!tryParseScalarRegister(XReg).isSuccess())
    return ParseStatus::NoMatch;

  MCContext &ctx = getContext();
  const MCRegisterInfo *RI = ctx.getRegisterInfo();
  int X8Reg = RI->getMatchingSuperReg(
      XReg, AArch64::x8sub_0,
      &AArch64MCRegisterClasses[AArch64::GPR64x8ClassRegClassID]);
  if (!X8Reg)
    return Error(SS,
                 "expected an even-numbered x-register in the range [x0,x22]");

  Operands.push_back(
      AArch64Operand::CreateReg(X8Reg, RegKind::Scalar, SS, getLoc(), ctx));
  return ParseStatus::Success;
}

ParseStatus AArch64AsmParser::tryParseImmRange(OperandVector &Operands) {
  SMLoc S = getLoc();

  if (getTok().isNot(AsmToken::Integer))
    return ParseStatus::NoMatch;

  if (getLexer().peekTok().isNot(AsmToken::Colon))
    return ParseStatus::NoMatch;

  const MCExpr *ImmF;
  if (getParser().parseExpression(ImmF))
    return ParseStatus::NoMatch;

  if (getTok().isNot(AsmToken::Colon))
    return ParseStatus::NoMatch;

  Lex(); // Eat ':'
  if (getTok().isNot(AsmToken::Integer))
    return ParseStatus::NoMatch;

  SMLoc E = getTok().getLoc();
  const MCExpr *ImmL;
  if (getParser().parseExpression(ImmL))
    return ParseStatus::NoMatch;

  unsigned ImmFVal = cast<MCConstantExpr>(ImmF)->getValue();
  unsigned ImmLVal = cast<MCConstantExpr>(ImmL)->getValue();

  Operands.push_back(
      AArch64Operand::CreateImmRange(ImmFVal, ImmLVal, S, E, getContext()));
  return ParseStatus::Success;
}
