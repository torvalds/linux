//===- AMDGPUAsmParser.cpp - Parse SI asm to MCInst instructions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDKernelCodeT.h"
#include "MCTargetDesc/AMDGPUMCExpr.h"
#include "MCTargetDesc/AMDGPUMCKernelDescriptor.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "MCTargetDesc/AMDGPUTargetStreamer.h"
#include "SIDefines.h"
#include "SIInstrInfo.h"
#include "SIRegisterInfo.h"
#include "TargetInfo/AMDGPUTargetInfo.h"
#include "Utils/AMDGPUAsmUtils.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "Utils/AMDKernelCodeTUtils.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/AMDHSAKernelDescriptor.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/TargetParser.h"
#include <optional>

using namespace llvm;
using namespace llvm::AMDGPU;
using namespace llvm::amdhsa;

namespace {

class AMDGPUAsmParser;

enum RegisterKind { IS_UNKNOWN, IS_VGPR, IS_SGPR, IS_AGPR, IS_TTMP, IS_SPECIAL };

//===----------------------------------------------------------------------===//
// Operand
//===----------------------------------------------------------------------===//

class AMDGPUOperand : public MCParsedAsmOperand {
  enum KindTy {
    Token,
    Immediate,
    Register,
    Expression
  } Kind;

  SMLoc StartLoc, EndLoc;
  const AMDGPUAsmParser *AsmParser;

public:
  AMDGPUOperand(KindTy Kind_, const AMDGPUAsmParser *AsmParser_)
      : Kind(Kind_), AsmParser(AsmParser_) {}

  using Ptr = std::unique_ptr<AMDGPUOperand>;

  struct Modifiers {
    bool Abs = false;
    bool Neg = false;
    bool Sext = false;
    bool Lit = false;

    bool hasFPModifiers() const { return Abs || Neg; }
    bool hasIntModifiers() const { return Sext; }
    bool hasModifiers() const { return hasFPModifiers() || hasIntModifiers(); }

    int64_t getFPModifiersOperand() const {
      int64_t Operand = 0;
      Operand |= Abs ? SISrcMods::ABS : 0u;
      Operand |= Neg ? SISrcMods::NEG : 0u;
      return Operand;
    }

    int64_t getIntModifiersOperand() const {
      int64_t Operand = 0;
      Operand |= Sext ? SISrcMods::SEXT : 0u;
      return Operand;
    }

    int64_t getModifiersOperand() const {
      assert(!(hasFPModifiers() && hasIntModifiers())
           && "fp and int modifiers should not be used simultaneously");
      if (hasFPModifiers())
        return getFPModifiersOperand();
      if (hasIntModifiers())
        return getIntModifiersOperand();
      return 0;
    }

    friend raw_ostream &operator <<(raw_ostream &OS, AMDGPUOperand::Modifiers Mods);
  };

  enum ImmTy {
    ImmTyNone,
    ImmTyGDS,
    ImmTyLDS,
    ImmTyOffen,
    ImmTyIdxen,
    ImmTyAddr64,
    ImmTyOffset,
    ImmTyInstOffset,
    ImmTyOffset0,
    ImmTyOffset1,
    ImmTySMEMOffsetMod,
    ImmTyCPol,
    ImmTyTFE,
    ImmTyD16,
    ImmTyClamp,
    ImmTyOModSI,
    ImmTySDWADstSel,
    ImmTySDWASrc0Sel,
    ImmTySDWASrc1Sel,
    ImmTySDWADstUnused,
    ImmTyDMask,
    ImmTyDim,
    ImmTyUNorm,
    ImmTyDA,
    ImmTyR128A16,
    ImmTyA16,
    ImmTyLWE,
    ImmTyExpTgt,
    ImmTyExpCompr,
    ImmTyExpVM,
    ImmTyFORMAT,
    ImmTyHwreg,
    ImmTyOff,
    ImmTySendMsg,
    ImmTyInterpSlot,
    ImmTyInterpAttr,
    ImmTyInterpAttrChan,
    ImmTyOpSel,
    ImmTyOpSelHi,
    ImmTyNegLo,
    ImmTyNegHi,
    ImmTyIndexKey8bit,
    ImmTyIndexKey16bit,
    ImmTyDPP8,
    ImmTyDppCtrl,
    ImmTyDppRowMask,
    ImmTyDppBankMask,
    ImmTyDppBoundCtrl,
    ImmTyDppFI,
    ImmTySwizzle,
    ImmTyGprIdxMode,
    ImmTyHigh,
    ImmTyBLGP,
    ImmTyCBSZ,
    ImmTyABID,
    ImmTyEndpgm,
    ImmTyWaitVDST,
    ImmTyWaitEXP,
    ImmTyWaitVAVDst,
    ImmTyWaitVMVSrc,
    ImmTyByteSel,
  };

  // Immediate operand kind.
  // It helps to identify the location of an offending operand after an error.
  // Note that regular literals and mandatory literals (KImm) must be handled
  // differently. When looking for an offending operand, we should usually
  // ignore mandatory literals because they are part of the instruction and
  // cannot be changed. Report location of mandatory operands only for VOPD,
  // when both OpX and OpY have a KImm and there are no other literals.
  enum ImmKindTy {
    ImmKindTyNone,
    ImmKindTyLiteral,
    ImmKindTyMandatoryLiteral,
    ImmKindTyConst,
  };

private:
  struct TokOp {
    const char *Data;
    unsigned Length;
  };

  struct ImmOp {
    int64_t Val;
    ImmTy Type;
    bool IsFPImm;
    mutable ImmKindTy Kind;
    Modifiers Mods;
  };

  struct RegOp {
    unsigned RegNo;
    Modifiers Mods;
  };

  union {
    TokOp Tok;
    ImmOp Imm;
    RegOp Reg;
    const MCExpr *Expr;
  };

public:
  bool isToken() const override { return Kind == Token; }

  bool isSymbolRefExpr() const {
    return isExpr() && Expr && isa<MCSymbolRefExpr>(Expr);
  }

  bool isImm() const override {
    return Kind == Immediate;
  }

  void setImmKindNone() const {
    assert(isImm());
    Imm.Kind = ImmKindTyNone;
  }

  void setImmKindLiteral() const {
    assert(isImm());
    Imm.Kind = ImmKindTyLiteral;
  }

  void setImmKindMandatoryLiteral() const {
    assert(isImm());
    Imm.Kind = ImmKindTyMandatoryLiteral;
  }

  void setImmKindConst() const {
    assert(isImm());
    Imm.Kind = ImmKindTyConst;
  }

  bool IsImmKindLiteral() const {
    return isImm() && Imm.Kind == ImmKindTyLiteral;
  }

  bool IsImmKindMandatoryLiteral() const {
    return isImm() && Imm.Kind == ImmKindTyMandatoryLiteral;
  }

  bool isImmKindConst() const {
    return isImm() && Imm.Kind == ImmKindTyConst;
  }

  bool isInlinableImm(MVT type) const;
  bool isLiteralImm(MVT type) const;

  bool isRegKind() const {
    return Kind == Register;
  }

  bool isReg() const override {
    return isRegKind() && !hasModifiers();
  }

  bool isRegOrInline(unsigned RCID, MVT type) const {
    return isRegClass(RCID) || isInlinableImm(type);
  }

  bool isRegOrImmWithInputMods(unsigned RCID, MVT type) const {
    return isRegOrInline(RCID, type) || isLiteralImm(type);
  }

  bool isRegOrImmWithInt16InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_32RegClassID, MVT::i16);
  }

  bool isRegOrImmWithIntT16InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_16RegClassID, MVT::i16);
  }

  bool isRegOrImmWithInt32InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_32RegClassID, MVT::i32);
  }

  bool isRegOrInlineImmWithInt16InputMods() const {
    return isRegOrInline(AMDGPU::VS_32RegClassID, MVT::i16);
  }

  bool isRegOrInlineImmWithInt32InputMods() const {
    return isRegOrInline(AMDGPU::VS_32RegClassID, MVT::i32);
  }

  bool isRegOrImmWithInt64InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_64RegClassID, MVT::i64);
  }

  bool isRegOrImmWithFP16InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_32RegClassID, MVT::f16);
  }

  bool isRegOrImmWithFPT16InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_16RegClassID, MVT::f16);
  }

  bool isRegOrImmWithFP32InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_32RegClassID, MVT::f32);
  }

  bool isRegOrImmWithFP64InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_64RegClassID, MVT::f64);
  }

  template <bool IsFake16> bool isRegOrInlineImmWithFP16InputMods() const {
    return isRegOrInline(
        IsFake16 ? AMDGPU::VS_32RegClassID : AMDGPU::VS_16RegClassID, MVT::f16);
  }

  bool isRegOrInlineImmWithFP32InputMods() const {
    return isRegOrInline(AMDGPU::VS_32RegClassID, MVT::f32);
  }

  bool isPackedFP16InputMods() const {
    return isRegOrImmWithInputMods(AMDGPU::VS_32RegClassID, MVT::v2f16);
  }

  bool isVReg() const {
    return isRegClass(AMDGPU::VGPR_32RegClassID) ||
           isRegClass(AMDGPU::VReg_64RegClassID) ||
           isRegClass(AMDGPU::VReg_96RegClassID) ||
           isRegClass(AMDGPU::VReg_128RegClassID) ||
           isRegClass(AMDGPU::VReg_160RegClassID) ||
           isRegClass(AMDGPU::VReg_192RegClassID) ||
           isRegClass(AMDGPU::VReg_256RegClassID) ||
           isRegClass(AMDGPU::VReg_512RegClassID) ||
           isRegClass(AMDGPU::VReg_1024RegClassID);
  }

  bool isVReg32() const {
    return isRegClass(AMDGPU::VGPR_32RegClassID);
  }

  bool isVReg32OrOff() const {
    return isOff() || isVReg32();
  }

  bool isNull() const {
    return isRegKind() && getReg() == AMDGPU::SGPR_NULL;
  }

  bool isVRegWithInputMods() const;
  template <bool IsFake16> bool isT16VRegWithInputMods() const;

  bool isSDWAOperand(MVT type) const;
  bool isSDWAFP16Operand() const;
  bool isSDWAFP32Operand() const;
  bool isSDWAInt16Operand() const;
  bool isSDWAInt32Operand() const;

  bool isImmTy(ImmTy ImmT) const {
    return isImm() && Imm.Type == ImmT;
  }

  template <ImmTy Ty> bool isImmTy() const { return isImmTy(Ty); }

  bool isImmLiteral() const { return isImmTy(ImmTyNone); }

  bool isImmModifier() const {
    return isImm() && Imm.Type != ImmTyNone;
  }

  bool isOModSI() const { return isImmTy(ImmTyOModSI); }
  bool isDim() const { return isImmTy(ImmTyDim); }
  bool isR128A16() const { return isImmTy(ImmTyR128A16); }
  bool isOff() const { return isImmTy(ImmTyOff); }
  bool isExpTgt() const { return isImmTy(ImmTyExpTgt); }
  bool isOffen() const { return isImmTy(ImmTyOffen); }
  bool isIdxen() const { return isImmTy(ImmTyIdxen); }
  bool isAddr64() const { return isImmTy(ImmTyAddr64); }
  bool isSMEMOffsetMod() const { return isImmTy(ImmTySMEMOffsetMod); }
  bool isFlatOffset() const { return isImmTy(ImmTyOffset) || isImmTy(ImmTyInstOffset); }
  bool isGDS() const { return isImmTy(ImmTyGDS); }
  bool isLDS() const { return isImmTy(ImmTyLDS); }
  bool isCPol() const { return isImmTy(ImmTyCPol); }
  bool isIndexKey8bit() const { return isImmTy(ImmTyIndexKey8bit); }
  bool isIndexKey16bit() const { return isImmTy(ImmTyIndexKey16bit); }
  bool isTFE() const { return isImmTy(ImmTyTFE); }
  bool isFORMAT() const { return isImmTy(ImmTyFORMAT) && isUInt<7>(getImm()); }
  bool isDppFI() const { return isImmTy(ImmTyDppFI); }
  bool isSDWADstSel() const { return isImmTy(ImmTySDWADstSel); }
  bool isSDWASrc0Sel() const { return isImmTy(ImmTySDWASrc0Sel); }
  bool isSDWASrc1Sel() const { return isImmTy(ImmTySDWASrc1Sel); }
  bool isSDWADstUnused() const { return isImmTy(ImmTySDWADstUnused); }
  bool isInterpSlot() const { return isImmTy(ImmTyInterpSlot); }
  bool isInterpAttr() const { return isImmTy(ImmTyInterpAttr); }
  bool isInterpAttrChan() const { return isImmTy(ImmTyInterpAttrChan); }
  bool isOpSel() const { return isImmTy(ImmTyOpSel); }
  bool isOpSelHi() const { return isImmTy(ImmTyOpSelHi); }
  bool isNegLo() const { return isImmTy(ImmTyNegLo); }
  bool isNegHi() const { return isImmTy(ImmTyNegHi); }

  bool isRegOrImm() const {
    return isReg() || isImm();
  }

  bool isRegClass(unsigned RCID) const;

  bool isInlineValue() const;

  bool isRegOrInlineNoMods(unsigned RCID, MVT type) const {
    return isRegOrInline(RCID, type) && !hasModifiers();
  }

  bool isSCSrcB16() const {
    return isRegOrInlineNoMods(AMDGPU::SReg_32RegClassID, MVT::i16);
  }

  bool isSCSrcV2B16() const {
    return isSCSrcB16();
  }

  bool isSCSrc_b32() const {
    return isRegOrInlineNoMods(AMDGPU::SReg_32RegClassID, MVT::i32);
  }

  bool isSCSrc_b64() const {
    return isRegOrInlineNoMods(AMDGPU::SReg_64RegClassID, MVT::i64);
  }

  bool isBoolReg() const;

  bool isSCSrcF16() const {
    return isRegOrInlineNoMods(AMDGPU::SReg_32RegClassID, MVT::f16);
  }

  bool isSCSrcV2F16() const {
    return isSCSrcF16();
  }

  bool isSCSrcF32() const {
    return isRegOrInlineNoMods(AMDGPU::SReg_32RegClassID, MVT::f32);
  }

  bool isSCSrcF64() const {
    return isRegOrInlineNoMods(AMDGPU::SReg_64RegClassID, MVT::f64);
  }

  bool isSSrc_b32() const {
    return isSCSrc_b32() || isLiteralImm(MVT::i32) || isExpr();
  }

  bool isSSrc_b16() const { return isSCSrcB16() || isLiteralImm(MVT::i16); }

  bool isSSrcV2B16() const {
    llvm_unreachable("cannot happen");
    return isSSrc_b16();
  }

  bool isSSrc_b64() const {
    // TODO: Find out how SALU supports extension of 32-bit literals to 64 bits.
    // See isVSrc64().
    return isSCSrc_b64() || isLiteralImm(MVT::i64);
  }

  bool isSSrc_f32() const {
    return isSCSrc_b32() || isLiteralImm(MVT::f32) || isExpr();
  }

  bool isSSrcF64() const { return isSCSrc_b64() || isLiteralImm(MVT::f64); }

  bool isSSrc_bf16() const { return isSCSrcB16() || isLiteralImm(MVT::bf16); }

  bool isSSrc_f16() const { return isSCSrcB16() || isLiteralImm(MVT::f16); }

  bool isSSrcV2F16() const {
    llvm_unreachable("cannot happen");
    return isSSrc_f16();
  }

  bool isSSrcV2FP32() const {
    llvm_unreachable("cannot happen");
    return isSSrc_f32();
  }

  bool isSCSrcV2FP32() const {
    llvm_unreachable("cannot happen");
    return isSCSrcF32();
  }

  bool isSSrcV2INT32() const {
    llvm_unreachable("cannot happen");
    return isSSrc_b32();
  }

  bool isSCSrcV2INT32() const {
    llvm_unreachable("cannot happen");
    return isSCSrc_b32();
  }

  bool isSSrcOrLds_b32() const {
    return isRegOrInlineNoMods(AMDGPU::SRegOrLds_32RegClassID, MVT::i32) ||
           isLiteralImm(MVT::i32) || isExpr();
  }

  bool isVCSrc_b32() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32RegClassID, MVT::i32);
  }

  bool isVCSrcB64() const {
    return isRegOrInlineNoMods(AMDGPU::VS_64RegClassID, MVT::i64);
  }

  bool isVCSrcTB16() const {
    return isRegOrInlineNoMods(AMDGPU::VS_16RegClassID, MVT::i16);
  }

  bool isVCSrcTB16_Lo128() const {
    return isRegOrInlineNoMods(AMDGPU::VS_16_Lo128RegClassID, MVT::i16);
  }

  bool isVCSrcFake16B16_Lo128() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32_Lo128RegClassID, MVT::i16);
  }

  bool isVCSrc_b16() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32RegClassID, MVT::i16);
  }

  bool isVCSrc_v2b16() const { return isVCSrc_b16(); }

  bool isVCSrc_f32() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32RegClassID, MVT::f32);
  }

  bool isVCSrcF64() const {
    return isRegOrInlineNoMods(AMDGPU::VS_64RegClassID, MVT::f64);
  }

  bool isVCSrcTBF16() const {
    return isRegOrInlineNoMods(AMDGPU::VS_16RegClassID, MVT::bf16);
  }

  bool isVCSrcTF16() const {
    return isRegOrInlineNoMods(AMDGPU::VS_16RegClassID, MVT::f16);
  }

  bool isVCSrcTBF16_Lo128() const {
    return isRegOrInlineNoMods(AMDGPU::VS_16_Lo128RegClassID, MVT::bf16);
  }

  bool isVCSrcTF16_Lo128() const {
    return isRegOrInlineNoMods(AMDGPU::VS_16_Lo128RegClassID, MVT::f16);
  }

  bool isVCSrcFake16BF16_Lo128() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32_Lo128RegClassID, MVT::bf16);
  }

  bool isVCSrcFake16F16_Lo128() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32_Lo128RegClassID, MVT::f16);
  }

  bool isVCSrc_bf16() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32RegClassID, MVT::bf16);
  }

  bool isVCSrc_f16() const {
    return isRegOrInlineNoMods(AMDGPU::VS_32RegClassID, MVT::f16);
  }

  bool isVCSrc_v2bf16() const { return isVCSrc_bf16(); }

  bool isVCSrc_v2f16() const { return isVCSrc_f16(); }

  bool isVSrc_b32() const {
    return isVCSrc_f32() || isLiteralImm(MVT::i32) || isExpr();
  }

  bool isVSrc_b64() const { return isVCSrcF64() || isLiteralImm(MVT::i64); }

  bool isVSrcT_b16() const { return isVCSrcTB16() || isLiteralImm(MVT::i16); }

  bool isVSrcT_b16_Lo128() const {
    return isVCSrcTB16_Lo128() || isLiteralImm(MVT::i16);
  }

  bool isVSrcFake16_b16_Lo128() const {
    return isVCSrcFake16B16_Lo128() || isLiteralImm(MVT::i16);
  }

  bool isVSrc_b16() const { return isVCSrc_b16() || isLiteralImm(MVT::i16); }

  bool isVSrc_v2b16() const { return isVSrc_b16() || isLiteralImm(MVT::v2i16); }

  bool isVCSrcV2FP32() const {
    return isVCSrcF64();
  }

  bool isVSrc_v2f32() const { return isVSrc_f64() || isLiteralImm(MVT::v2f32); }

  bool isVCSrcV2INT32() const {
    return isVCSrcB64();
  }

  bool isVSrc_v2b32() const { return isVSrc_b64() || isLiteralImm(MVT::v2i32); }

  bool isVSrc_f32() const {
    return isVCSrc_f32() || isLiteralImm(MVT::f32) || isExpr();
  }

  bool isVSrc_f64() const { return isVCSrcF64() || isLiteralImm(MVT::f64); }

  bool isVSrcT_bf16() const { return isVCSrcTBF16() || isLiteralImm(MVT::bf16); }

  bool isVSrcT_f16() const { return isVCSrcTF16() || isLiteralImm(MVT::f16); }

  bool isVSrcT_bf16_Lo128() const {
    return isVCSrcTBF16_Lo128() || isLiteralImm(MVT::bf16);
  }

  bool isVSrcT_f16_Lo128() const {
    return isVCSrcTF16_Lo128() || isLiteralImm(MVT::f16);
  }

  bool isVSrcFake16_bf16_Lo128() const {
    return isVCSrcFake16BF16_Lo128() || isLiteralImm(MVT::bf16);
  }

  bool isVSrcFake16_f16_Lo128() const {
    return isVCSrcFake16F16_Lo128() || isLiteralImm(MVT::f16);
  }

  bool isVSrc_bf16() const { return isVCSrc_bf16() || isLiteralImm(MVT::bf16); }

  bool isVSrc_f16() const { return isVCSrc_f16() || isLiteralImm(MVT::f16); }

  bool isVSrc_v2bf16() const {
    return isVSrc_bf16() || isLiteralImm(MVT::v2bf16);
  }

  bool isVSrc_v2f16() const { return isVSrc_f16() || isLiteralImm(MVT::v2f16); }

  bool isVISrcB32() const {
    return isRegOrInlineNoMods(AMDGPU::VGPR_32RegClassID, MVT::i32);
  }

  bool isVISrcB16() const {
    return isRegOrInlineNoMods(AMDGPU::VGPR_32RegClassID, MVT::i16);
  }

  bool isVISrcV2B16() const {
    return isVISrcB16();
  }

  bool isVISrcF32() const {
    return isRegOrInlineNoMods(AMDGPU::VGPR_32RegClassID, MVT::f32);
  }

  bool isVISrcF16() const {
    return isRegOrInlineNoMods(AMDGPU::VGPR_32RegClassID, MVT::f16);
  }

  bool isVISrcV2F16() const {
    return isVISrcF16() || isVISrcB32();
  }

  bool isVISrc_64_bf16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::bf16);
  }

  bool isVISrc_64_f16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::f16);
  }

  bool isVISrc_64_b32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::i32);
  }

  bool isVISrc_64B64() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::i64);
  }

  bool isVISrc_64_f64() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::f64);
  }

  bool isVISrc_64V2FP32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::f32);
  }

  bool isVISrc_64V2INT32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_64RegClassID, MVT::i32);
  }

  bool isVISrc_256_b32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_256RegClassID, MVT::i32);
  }

  bool isVISrc_256_f32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_256RegClassID, MVT::f32);
  }

  bool isVISrc_256B64() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_256RegClassID, MVT::i64);
  }

  bool isVISrc_256_f64() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_256RegClassID, MVT::f64);
  }

  bool isVISrc_128B16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_128RegClassID, MVT::i16);
  }

  bool isVISrc_128V2B16() const {
    return isVISrc_128B16();
  }

  bool isVISrc_128_b32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_128RegClassID, MVT::i32);
  }

  bool isVISrc_128_f32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_128RegClassID, MVT::f32);
  }

  bool isVISrc_256V2FP32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_256RegClassID, MVT::f32);
  }

  bool isVISrc_256V2INT32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_256RegClassID, MVT::i32);
  }

  bool isVISrc_512_b32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_512RegClassID, MVT::i32);
  }

  bool isVISrc_512B16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_512RegClassID, MVT::i16);
  }

  bool isVISrc_512V2B16() const {
    return isVISrc_512B16();
  }

  bool isVISrc_512_f32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_512RegClassID, MVT::f32);
  }

  bool isVISrc_512F16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_512RegClassID, MVT::f16);
  }

  bool isVISrc_512V2F16() const {
    return isVISrc_512F16() || isVISrc_512_b32();
  }

  bool isVISrc_1024_b32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_1024RegClassID, MVT::i32);
  }

  bool isVISrc_1024B16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_1024RegClassID, MVT::i16);
  }

  bool isVISrc_1024V2B16() const {
    return isVISrc_1024B16();
  }

  bool isVISrc_1024_f32() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_1024RegClassID, MVT::f32);
  }

  bool isVISrc_1024F16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_1024RegClassID, MVT::f16);
  }

  bool isVISrc_1024V2F16() const {
    return isVISrc_1024F16() || isVISrc_1024_b32();
  }

  bool isAISrcB32() const {
    return isRegOrInlineNoMods(AMDGPU::AGPR_32RegClassID, MVT::i32);
  }

  bool isAISrcB16() const {
    return isRegOrInlineNoMods(AMDGPU::AGPR_32RegClassID, MVT::i16);
  }

  bool isAISrcV2B16() const {
    return isAISrcB16();
  }

  bool isAISrcF32() const {
    return isRegOrInlineNoMods(AMDGPU::AGPR_32RegClassID, MVT::f32);
  }

  bool isAISrcF16() const {
    return isRegOrInlineNoMods(AMDGPU::AGPR_32RegClassID, MVT::f16);
  }

  bool isAISrcV2F16() const {
    return isAISrcF16() || isAISrcB32();
  }

  bool isAISrc_64B64() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_64RegClassID, MVT::i64);
  }

  bool isAISrc_64_f64() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_64RegClassID, MVT::f64);
  }

  bool isAISrc_128_b32() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_128RegClassID, MVT::i32);
  }

  bool isAISrc_128B16() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_128RegClassID, MVT::i16);
  }

  bool isAISrc_128V2B16() const {
    return isAISrc_128B16();
  }

  bool isAISrc_128_f32() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_128RegClassID, MVT::f32);
  }

  bool isAISrc_128F16() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_128RegClassID, MVT::f16);
  }

  bool isAISrc_128V2F16() const {
    return isAISrc_128F16() || isAISrc_128_b32();
  }

  bool isVISrc_128_bf16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_128RegClassID, MVT::bf16);
  }

  bool isVISrc_128_f16() const {
    return isRegOrInlineNoMods(AMDGPU::VReg_128RegClassID, MVT::f16);
  }

  bool isVISrc_128V2F16() const {
    return isVISrc_128_f16() || isVISrc_128_b32();
  }

  bool isAISrc_256B64() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_256RegClassID, MVT::i64);
  }

  bool isAISrc_256_f64() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_256RegClassID, MVT::f64);
  }

  bool isAISrc_512_b32() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_512RegClassID, MVT::i32);
  }

  bool isAISrc_512B16() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_512RegClassID, MVT::i16);
  }

  bool isAISrc_512V2B16() const {
    return isAISrc_512B16();
  }

  bool isAISrc_512_f32() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_512RegClassID, MVT::f32);
  }

  bool isAISrc_512F16() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_512RegClassID, MVT::f16);
  }

  bool isAISrc_512V2F16() const {
    return isAISrc_512F16() || isAISrc_512_b32();
  }

  bool isAISrc_1024_b32() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_1024RegClassID, MVT::i32);
  }

  bool isAISrc_1024B16() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_1024RegClassID, MVT::i16);
  }

  bool isAISrc_1024V2B16() const {
    return isAISrc_1024B16();
  }

  bool isAISrc_1024_f32() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_1024RegClassID, MVT::f32);
  }

  bool isAISrc_1024F16() const {
    return isRegOrInlineNoMods(AMDGPU::AReg_1024RegClassID, MVT::f16);
  }

  bool isAISrc_1024V2F16() const {
    return isAISrc_1024F16() || isAISrc_1024_b32();
  }

  bool isKImmFP32() const {
    return isLiteralImm(MVT::f32);
  }

  bool isKImmFP16() const {
    return isLiteralImm(MVT::f16);
  }

  bool isMem() const override {
    return false;
  }

  bool isExpr() const {
    return Kind == Expression;
  }

  bool isSOPPBrTarget() const { return isExpr() || isImm(); }

  bool isSWaitCnt() const;
  bool isDepCtr() const;
  bool isSDelayALU() const;
  bool isHwreg() const;
  bool isSendMsg() const;
  bool isSplitBarrier() const;
  bool isSwizzle() const;
  bool isSMRDOffset8() const;
  bool isSMEMOffset() const;
  bool isSMRDLiteralOffset() const;
  bool isDPP8() const;
  bool isDPPCtrl() const;
  bool isBLGP() const;
  bool isGPRIdxMode() const;
  bool isS16Imm() const;
  bool isU16Imm() const;
  bool isEndpgm() const;

  auto getPredicate(std::function<bool(const AMDGPUOperand &Op)> P) const {
    return [=](){ return P(*this); };
  }

  StringRef getToken() const {
    assert(isToken());
    return StringRef(Tok.Data, Tok.Length);
  }

  int64_t getImm() const {
    assert(isImm());
    return Imm.Val;
  }

  void setImm(int64_t Val) {
    assert(isImm());
    Imm.Val = Val;
  }

  ImmTy getImmTy() const {
    assert(isImm());
    return Imm.Type;
  }

  MCRegister getReg() const override {
    assert(isRegKind());
    return Reg.RegNo;
  }

  SMLoc getStartLoc() const override {
    return StartLoc;
  }

  SMLoc getEndLoc() const override {
    return EndLoc;
  }

  SMRange getLocRange() const {
    return SMRange(StartLoc, EndLoc);
  }

  Modifiers getModifiers() const {
    assert(isRegKind() || isImmTy(ImmTyNone));
    return isRegKind() ? Reg.Mods : Imm.Mods;
  }

  void setModifiers(Modifiers Mods) {
    assert(isRegKind() || isImmTy(ImmTyNone));
    if (isRegKind())
      Reg.Mods = Mods;
    else
      Imm.Mods = Mods;
  }

  bool hasModifiers() const {
    return getModifiers().hasModifiers();
  }

  bool hasFPModifiers() const {
    return getModifiers().hasFPModifiers();
  }

  bool hasIntModifiers() const {
    return getModifiers().hasIntModifiers();
  }

  uint64_t applyInputFPModifiers(uint64_t Val, unsigned Size) const;

  void addImmOperands(MCInst &Inst, unsigned N, bool ApplyModifiers = true) const;

  void addLiteralImmOperand(MCInst &Inst, int64_t Val, bool ApplyModifiers) const;

  void addRegOperands(MCInst &Inst, unsigned N) const;

  void addRegOrImmOperands(MCInst &Inst, unsigned N) const {
    if (isRegKind())
      addRegOperands(Inst, N);
    else
      addImmOperands(Inst, N);
  }

  void addRegOrImmWithInputModsOperands(MCInst &Inst, unsigned N) const {
    Modifiers Mods = getModifiers();
    Inst.addOperand(MCOperand::createImm(Mods.getModifiersOperand()));
    if (isRegKind()) {
      addRegOperands(Inst, N);
    } else {
      addImmOperands(Inst, N, false);
    }
  }

  void addRegOrImmWithFPInputModsOperands(MCInst &Inst, unsigned N) const {
    assert(!hasIntModifiers());
    addRegOrImmWithInputModsOperands(Inst, N);
  }

  void addRegOrImmWithIntInputModsOperands(MCInst &Inst, unsigned N) const {
    assert(!hasFPModifiers());
    addRegOrImmWithInputModsOperands(Inst, N);
  }

  void addRegWithInputModsOperands(MCInst &Inst, unsigned N) const {
    Modifiers Mods = getModifiers();
    Inst.addOperand(MCOperand::createImm(Mods.getModifiersOperand()));
    assert(isRegKind());
    addRegOperands(Inst, N);
  }

  void addRegWithFPInputModsOperands(MCInst &Inst, unsigned N) const {
    assert(!hasIntModifiers());
    addRegWithInputModsOperands(Inst, N);
  }

  void addRegWithIntInputModsOperands(MCInst &Inst, unsigned N) const {
    assert(!hasFPModifiers());
    addRegWithInputModsOperands(Inst, N);
  }

  static void printImmTy(raw_ostream& OS, ImmTy Type) {
    // clang-format off
    switch (Type) {
    case ImmTyNone: OS << "None"; break;
    case ImmTyGDS: OS << "GDS"; break;
    case ImmTyLDS: OS << "LDS"; break;
    case ImmTyOffen: OS << "Offen"; break;
    case ImmTyIdxen: OS << "Idxen"; break;
    case ImmTyAddr64: OS << "Addr64"; break;
    case ImmTyOffset: OS << "Offset"; break;
    case ImmTyInstOffset: OS << "InstOffset"; break;
    case ImmTyOffset0: OS << "Offset0"; break;
    case ImmTyOffset1: OS << "Offset1"; break;
    case ImmTySMEMOffsetMod: OS << "SMEMOffsetMod"; break;
    case ImmTyCPol: OS << "CPol"; break;
    case ImmTyIndexKey8bit: OS << "index_key"; break;
    case ImmTyIndexKey16bit: OS << "index_key"; break;
    case ImmTyTFE: OS << "TFE"; break;
    case ImmTyD16: OS << "D16"; break;
    case ImmTyFORMAT: OS << "FORMAT"; break;
    case ImmTyClamp: OS << "Clamp"; break;
    case ImmTyOModSI: OS << "OModSI"; break;
    case ImmTyDPP8: OS << "DPP8"; break;
    case ImmTyDppCtrl: OS << "DppCtrl"; break;
    case ImmTyDppRowMask: OS << "DppRowMask"; break;
    case ImmTyDppBankMask: OS << "DppBankMask"; break;
    case ImmTyDppBoundCtrl: OS << "DppBoundCtrl"; break;
    case ImmTyDppFI: OS << "DppFI"; break;
    case ImmTySDWADstSel: OS << "SDWADstSel"; break;
    case ImmTySDWASrc0Sel: OS << "SDWASrc0Sel"; break;
    case ImmTySDWASrc1Sel: OS << "SDWASrc1Sel"; break;
    case ImmTySDWADstUnused: OS << "SDWADstUnused"; break;
    case ImmTyDMask: OS << "DMask"; break;
    case ImmTyDim: OS << "Dim"; break;
    case ImmTyUNorm: OS << "UNorm"; break;
    case ImmTyDA: OS << "DA"; break;
    case ImmTyR128A16: OS << "R128A16"; break;
    case ImmTyA16: OS << "A16"; break;
    case ImmTyLWE: OS << "LWE"; break;
    case ImmTyOff: OS << "Off"; break;
    case ImmTyExpTgt: OS << "ExpTgt"; break;
    case ImmTyExpCompr: OS << "ExpCompr"; break;
    case ImmTyExpVM: OS << "ExpVM"; break;
    case ImmTyHwreg: OS << "Hwreg"; break;
    case ImmTySendMsg: OS << "SendMsg"; break;
    case ImmTyInterpSlot: OS << "InterpSlot"; break;
    case ImmTyInterpAttr: OS << "InterpAttr"; break;
    case ImmTyInterpAttrChan: OS << "InterpAttrChan"; break;
    case ImmTyOpSel: OS << "OpSel"; break;
    case ImmTyOpSelHi: OS << "OpSelHi"; break;
    case ImmTyNegLo: OS << "NegLo"; break;
    case ImmTyNegHi: OS << "NegHi"; break;
    case ImmTySwizzle: OS << "Swizzle"; break;
    case ImmTyGprIdxMode: OS << "GprIdxMode"; break;
    case ImmTyHigh: OS << "High"; break;
    case ImmTyBLGP: OS << "BLGP"; break;
    case ImmTyCBSZ: OS << "CBSZ"; break;
    case ImmTyABID: OS << "ABID"; break;
    case ImmTyEndpgm: OS << "Endpgm"; break;
    case ImmTyWaitVDST: OS << "WaitVDST"; break;
    case ImmTyWaitEXP: OS << "WaitEXP"; break;
    case ImmTyWaitVAVDst: OS << "WaitVAVDst"; break;
    case ImmTyWaitVMVSrc: OS << "WaitVMVSrc"; break;
    case ImmTyByteSel: OS << "ByteSel" ; break;
    }
    // clang-format on
  }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case Register:
      OS << "<register " << getReg() << " mods: " << Reg.Mods << '>';
      break;
    case Immediate:
      OS << '<' << getImm();
      if (getImmTy() != ImmTyNone) {
        OS << " type: "; printImmTy(OS, getImmTy());
      }
      OS << " mods: " << Imm.Mods << '>';
      break;
    case Token:
      OS << '\'' << getToken() << '\'';
      break;
    case Expression:
      OS << "<expr " << *Expr << '>';
      break;
    }
  }

  static AMDGPUOperand::Ptr CreateImm(const AMDGPUAsmParser *AsmParser,
                                      int64_t Val, SMLoc Loc,
                                      ImmTy Type = ImmTyNone,
                                      bool IsFPImm = false) {
    auto Op = std::make_unique<AMDGPUOperand>(Immediate, AsmParser);
    Op->Imm.Val = Val;
    Op->Imm.IsFPImm = IsFPImm;
    Op->Imm.Kind = ImmKindTyNone;
    Op->Imm.Type = Type;
    Op->Imm.Mods = Modifiers();
    Op->StartLoc = Loc;
    Op->EndLoc = Loc;
    return Op;
  }

  static AMDGPUOperand::Ptr CreateToken(const AMDGPUAsmParser *AsmParser,
                                        StringRef Str, SMLoc Loc,
                                        bool HasExplicitEncodingSize = true) {
    auto Res = std::make_unique<AMDGPUOperand>(Token, AsmParser);
    Res->Tok.Data = Str.data();
    Res->Tok.Length = Str.size();
    Res->StartLoc = Loc;
    Res->EndLoc = Loc;
    return Res;
  }

  static AMDGPUOperand::Ptr CreateReg(const AMDGPUAsmParser *AsmParser,
                                      unsigned RegNo, SMLoc S,
                                      SMLoc E) {
    auto Op = std::make_unique<AMDGPUOperand>(Register, AsmParser);
    Op->Reg.RegNo = RegNo;
    Op->Reg.Mods = Modifiers();
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static AMDGPUOperand::Ptr CreateExpr(const AMDGPUAsmParser *AsmParser,
                                       const class MCExpr *Expr, SMLoc S) {
    auto Op = std::make_unique<AMDGPUOperand>(Expression, AsmParser);
    Op->Expr = Expr;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }
};

raw_ostream &operator <<(raw_ostream &OS, AMDGPUOperand::Modifiers Mods) {
  OS << "abs:" << Mods.Abs << " neg: " << Mods.Neg << " sext:" << Mods.Sext;
  return OS;
}

//===----------------------------------------------------------------------===//
// AsmParser
//===----------------------------------------------------------------------===//

// Holds info related to the current kernel, e.g. count of SGPRs used.
// Kernel scope begins at .amdgpu_hsa_kernel directive, ends at next
// .amdgpu_hsa_kernel or at EOF.
class KernelScopeInfo {
  int SgprIndexUnusedMin = -1;
  int VgprIndexUnusedMin = -1;
  int AgprIndexUnusedMin = -1;
  MCContext *Ctx = nullptr;
  MCSubtargetInfo const *MSTI = nullptr;

  void usesSgprAt(int i) {
    if (i >= SgprIndexUnusedMin) {
      SgprIndexUnusedMin = ++i;
      if (Ctx) {
        MCSymbol* const Sym =
          Ctx->getOrCreateSymbol(Twine(".kernel.sgpr_count"));
        Sym->setVariableValue(MCConstantExpr::create(SgprIndexUnusedMin, *Ctx));
      }
    }
  }

  void usesVgprAt(int i) {
    if (i >= VgprIndexUnusedMin) {
      VgprIndexUnusedMin = ++i;
      if (Ctx) {
        MCSymbol* const Sym =
          Ctx->getOrCreateSymbol(Twine(".kernel.vgpr_count"));
        int totalVGPR = getTotalNumVGPRs(isGFX90A(*MSTI), AgprIndexUnusedMin,
                                         VgprIndexUnusedMin);
        Sym->setVariableValue(MCConstantExpr::create(totalVGPR, *Ctx));
      }
    }
  }

  void usesAgprAt(int i) {
    // Instruction will error in AMDGPUAsmParser::MatchAndEmitInstruction
    if (!hasMAIInsts(*MSTI))
      return;

    if (i >= AgprIndexUnusedMin) {
      AgprIndexUnusedMin = ++i;
      if (Ctx) {
        MCSymbol* const Sym =
          Ctx->getOrCreateSymbol(Twine(".kernel.agpr_count"));
        Sym->setVariableValue(MCConstantExpr::create(AgprIndexUnusedMin, *Ctx));

        // Also update vgpr_count (dependent on agpr_count for gfx908/gfx90a)
        MCSymbol* const vSym =
          Ctx->getOrCreateSymbol(Twine(".kernel.vgpr_count"));
        int totalVGPR = getTotalNumVGPRs(isGFX90A(*MSTI), AgprIndexUnusedMin,
                                         VgprIndexUnusedMin);
        vSym->setVariableValue(MCConstantExpr::create(totalVGPR, *Ctx));
      }
    }
  }

public:
  KernelScopeInfo() = default;

  void initialize(MCContext &Context) {
    Ctx = &Context;
    MSTI = Ctx->getSubtargetInfo();

    usesSgprAt(SgprIndexUnusedMin = -1);
    usesVgprAt(VgprIndexUnusedMin = -1);
    if (hasMAIInsts(*MSTI)) {
      usesAgprAt(AgprIndexUnusedMin = -1);
    }
  }

  void usesRegister(RegisterKind RegKind, unsigned DwordRegIndex,
                    unsigned RegWidth) {
    switch (RegKind) {
    case IS_SGPR:
      usesSgprAt(DwordRegIndex + divideCeil(RegWidth, 32) - 1);
      break;
    case IS_AGPR:
      usesAgprAt(DwordRegIndex + divideCeil(RegWidth, 32) - 1);
      break;
    case IS_VGPR:
      usesVgprAt(DwordRegIndex + divideCeil(RegWidth, 32) - 1);
      break;
    default:
      break;
    }
  }
};

class AMDGPUAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  unsigned ForcedEncodingSize = 0;
  bool ForcedDPP = false;
  bool ForcedSDWA = false;
  KernelScopeInfo KernelScope;

  /// @name Auto-generated Match Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "AMDGPUGenAsmMatcher.inc"

  /// }

private:
  void createConstantSymbol(StringRef Id, int64_t Val);

  bool ParseAsAbsoluteExpression(uint32_t &Ret);
  bool OutOfRangeError(SMRange Range);
  /// Calculate VGPR/SGPR blocks required for given target, reserved
  /// registers, and user-specified NextFreeXGPR values.
  ///
  /// \param Features [in] Target features, used for bug corrections.
  /// \param VCCUsed [in] Whether VCC special SGPR is reserved.
  /// \param FlatScrUsed [in] Whether FLAT_SCRATCH special SGPR is reserved.
  /// \param XNACKUsed [in] Whether XNACK_MASK special SGPR is reserved.
  /// \param EnableWavefrontSize32 [in] Value of ENABLE_WAVEFRONT_SIZE32 kernel
  /// descriptor field, if valid.
  /// \param NextFreeVGPR [in] Max VGPR number referenced, plus one.
  /// \param VGPRRange [in] Token range, used for VGPR diagnostics.
  /// \param NextFreeSGPR [in] Max SGPR number referenced, plus one.
  /// \param SGPRRange [in] Token range, used for SGPR diagnostics.
  /// \param VGPRBlocks [out] Result VGPR block count.
  /// \param SGPRBlocks [out] Result SGPR block count.
  bool calculateGPRBlocks(const FeatureBitset &Features, const MCExpr *VCCUsed,
                          const MCExpr *FlatScrUsed, bool XNACKUsed,
                          std::optional<bool> EnableWavefrontSize32,
                          const MCExpr *NextFreeVGPR, SMRange VGPRRange,
                          const MCExpr *NextFreeSGPR, SMRange SGPRRange,
                          const MCExpr *&VGPRBlocks, const MCExpr *&SGPRBlocks);
  bool ParseDirectiveAMDGCNTarget();
  bool ParseDirectiveAMDHSACodeObjectVersion();
  bool ParseDirectiveAMDHSAKernel();
  bool ParseAMDKernelCodeTValue(StringRef ID, AMDGPUMCKernelCodeT &Header);
  bool ParseDirectiveAMDKernelCodeT();
  // TODO: Possibly make subtargetHasRegister const.
  bool subtargetHasRegister(const MCRegisterInfo &MRI, unsigned RegNo);
  bool ParseDirectiveAMDGPUHsaKernel();

  bool ParseDirectiveISAVersion();
  bool ParseDirectiveHSAMetadata();
  bool ParseDirectivePALMetadataBegin();
  bool ParseDirectivePALMetadata();
  bool ParseDirectiveAMDGPULDS();

  /// Common code to parse out a block of text (typically YAML) between start and
  /// end directives.
  bool ParseToEndDirective(const char *AssemblerDirectiveBegin,
                           const char *AssemblerDirectiveEnd,
                           std::string &CollectString);

  bool AddNextRegisterToList(unsigned& Reg, unsigned& RegWidth,
                             RegisterKind RegKind, unsigned Reg1, SMLoc Loc);
  bool ParseAMDGPURegister(RegisterKind &RegKind, unsigned &Reg,
                           unsigned &RegNum, unsigned &RegWidth,
                           bool RestoreOnFailure = false);
  bool ParseAMDGPURegister(RegisterKind &RegKind, unsigned &Reg,
                           unsigned &RegNum, unsigned &RegWidth,
                           SmallVectorImpl<AsmToken> &Tokens);
  unsigned ParseRegularReg(RegisterKind &RegKind, unsigned &RegNum,
                           unsigned &RegWidth,
                           SmallVectorImpl<AsmToken> &Tokens);
  unsigned ParseSpecialReg(RegisterKind &RegKind, unsigned &RegNum,
                           unsigned &RegWidth,
                           SmallVectorImpl<AsmToken> &Tokens);
  unsigned ParseRegList(RegisterKind &RegKind, unsigned &RegNum,
                        unsigned &RegWidth, SmallVectorImpl<AsmToken> &Tokens);
  bool ParseRegRange(unsigned& Num, unsigned& Width);
  unsigned getRegularReg(RegisterKind RegKind, unsigned RegNum, unsigned SubReg,
                         unsigned RegWidth, SMLoc Loc);

  bool isRegister();
  bool isRegister(const AsmToken &Token, const AsmToken &NextToken) const;
  std::optional<StringRef> getGprCountSymbolName(RegisterKind RegKind);
  void initializeGprCountSymbol(RegisterKind RegKind);
  bool updateGprCountSymbols(RegisterKind RegKind, unsigned DwordRegIndex,
                             unsigned RegWidth);
  void cvtMubufImpl(MCInst &Inst, const OperandVector &Operands,
                    bool IsAtomic);

public:
  enum OperandMode {
    OperandMode_Default,
    OperandMode_NSA,
  };

  using OptionalImmIndexMap = std::map<AMDGPUOperand::ImmTy, unsigned>;

  AMDGPUAsmParser(const MCSubtargetInfo &STI, MCAsmParser &_Parser,
               const MCInstrInfo &MII,
               const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), Parser(_Parser) {
    MCAsmParserExtension::Initialize(Parser);

    if (getFeatureBits().none()) {
      // Set default features.
      copySTI().ToggleFeature("southern-islands");
    }

    FeatureBitset FB = getFeatureBits();
    if (!FB[AMDGPU::FeatureWavefrontSize64] &&
        !FB[AMDGPU::FeatureWavefrontSize32]) {
      // If there is no default wave size it must be a generation before gfx10,
      // these have FeatureWavefrontSize64 in their definition already. For
      // gfx10+ set wave32 as a default.
      copySTI().ToggleFeature(AMDGPU::FeatureWavefrontSize32);
    }

    setAvailableFeatures(ComputeAvailableFeatures(getFeatureBits()));

    AMDGPU::IsaVersion ISA = AMDGPU::getIsaVersion(getSTI().getCPU());
    if (ISA.Major >= 6 && isHsaAbi(getSTI())) {
      createConstantSymbol(".amdgcn.gfx_generation_number", ISA.Major);
      createConstantSymbol(".amdgcn.gfx_generation_minor", ISA.Minor);
      createConstantSymbol(".amdgcn.gfx_generation_stepping", ISA.Stepping);
    } else {
      createConstantSymbol(".option.machine_version_major", ISA.Major);
      createConstantSymbol(".option.machine_version_minor", ISA.Minor);
      createConstantSymbol(".option.machine_version_stepping", ISA.Stepping);
    }
    if (ISA.Major >= 6 && isHsaAbi(getSTI())) {
      initializeGprCountSymbol(IS_VGPR);
      initializeGprCountSymbol(IS_SGPR);
    } else
      KernelScope.initialize(getContext());

    for (auto [Symbol, Code] : AMDGPU::UCVersion::getGFXVersions())
      createConstantSymbol(Symbol, Code);

    createConstantSymbol("UC_VERSION_W64_BIT", 0x2000);
    createConstantSymbol("UC_VERSION_W32_BIT", 0x4000);
    createConstantSymbol("UC_VERSION_MDP_BIT", 0x8000);
  }

  bool hasMIMG_R128() const {
    return AMDGPU::hasMIMG_R128(getSTI());
  }

  bool hasPackedD16() const {
    return AMDGPU::hasPackedD16(getSTI());
  }

  bool hasA16() const { return AMDGPU::hasA16(getSTI()); }

  bool hasG16() const { return AMDGPU::hasG16(getSTI()); }

  bool hasGDS() const { return AMDGPU::hasGDS(getSTI()); }

  bool isSI() const {
    return AMDGPU::isSI(getSTI());
  }

  bool isCI() const {
    return AMDGPU::isCI(getSTI());
  }

  bool isVI() const {
    return AMDGPU::isVI(getSTI());
  }

  bool isGFX9() const {
    return AMDGPU::isGFX9(getSTI());
  }

  // TODO: isGFX90A is also true for GFX940. We need to clean it.
  bool isGFX90A() const {
    return AMDGPU::isGFX90A(getSTI());
  }

  bool isGFX940() const {
    return AMDGPU::isGFX940(getSTI());
  }

  bool isGFX9Plus() const {
    return AMDGPU::isGFX9Plus(getSTI());
  }

  bool isGFX10() const {
    return AMDGPU::isGFX10(getSTI());
  }

  bool isGFX10Plus() const { return AMDGPU::isGFX10Plus(getSTI()); }

  bool isGFX11() const {
    return AMDGPU::isGFX11(getSTI());
  }

  bool isGFX11Plus() const {
    return AMDGPU::isGFX11Plus(getSTI());
  }

  bool isGFX12() const { return AMDGPU::isGFX12(getSTI()); }

  bool isGFX12Plus() const { return AMDGPU::isGFX12Plus(getSTI()); }

  bool isGFX10_AEncoding() const { return AMDGPU::isGFX10_AEncoding(getSTI()); }

  bool isGFX10_BEncoding() const {
    return AMDGPU::isGFX10_BEncoding(getSTI());
  }

  bool hasInv2PiInlineImm() const {
    return getFeatureBits()[AMDGPU::FeatureInv2PiInlineImm];
  }

  bool hasFlatOffsets() const {
    return getFeatureBits()[AMDGPU::FeatureFlatInstOffsets];
  }

  bool hasArchitectedFlatScratch() const {
    return getFeatureBits()[AMDGPU::FeatureArchitectedFlatScratch];
  }

  bool hasSGPR102_SGPR103() const {
    return !isVI() && !isGFX9();
  }

  bool hasSGPR104_SGPR105() const { return isGFX10Plus(); }

  bool hasIntClamp() const {
    return getFeatureBits()[AMDGPU::FeatureIntClamp];
  }

  bool hasPartialNSAEncoding() const {
    return getFeatureBits()[AMDGPU::FeaturePartialNSAEncoding];
  }

  unsigned getNSAMaxSize(bool HasSampler = false) const {
    return AMDGPU::getNSAMaxSize(getSTI(), HasSampler);
  }

  unsigned getMaxNumUserSGPRs() const {
    return AMDGPU::getMaxNumUserSGPRs(getSTI());
  }

  bool hasKernargPreload() const { return AMDGPU::hasKernargPreload(getSTI()); }

  AMDGPUTargetStreamer &getTargetStreamer() {
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<AMDGPUTargetStreamer &>(TS);
  }

  const MCRegisterInfo *getMRI() const {
    // We need this const_cast because for some reason getContext() is not const
    // in MCAsmParser.
    return const_cast<AMDGPUAsmParser*>(this)->getContext().getRegisterInfo();
  }

  const MCInstrInfo *getMII() const {
    return &MII;
  }

  const FeatureBitset &getFeatureBits() const {
    return getSTI().getFeatureBits();
  }

  void setForcedEncodingSize(unsigned Size) { ForcedEncodingSize = Size; }
  void setForcedDPP(bool ForceDPP_) { ForcedDPP = ForceDPP_; }
  void setForcedSDWA(bool ForceSDWA_) { ForcedSDWA = ForceSDWA_; }

  unsigned getForcedEncodingSize() const { return ForcedEncodingSize; }
  bool isForcedVOP3() const { return ForcedEncodingSize == 64; }
  bool isForcedDPP() const { return ForcedDPP; }
  bool isForcedSDWA() const { return ForcedSDWA; }
  ArrayRef<unsigned> getMatchedVariants() const;
  StringRef getMatchedVariantName() const;

  std::unique_ptr<AMDGPUOperand> parseRegister(bool RestoreOnFailure = false);
  bool ParseRegister(MCRegister &RegNo, SMLoc &StartLoc, SMLoc &EndLoc,
                     bool RestoreOnFailure);
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  unsigned checkTargetMatchPredicate(MCInst &Inst) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  bool ParseDirective(AsmToken DirectiveID) override;
  ParseStatus parseOperand(OperandVector &Operands, StringRef Mnemonic,
                           OperandMode Mode = OperandMode_Default);
  StringRef parseMnemonicSuffix(StringRef Name);
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  //bool ProcessInstruction(MCInst &Inst);

  ParseStatus parseTokenOp(StringRef Name, OperandVector &Operands);

  ParseStatus parseIntWithPrefix(const char *Prefix, int64_t &Int);

  ParseStatus
  parseIntWithPrefix(const char *Prefix, OperandVector &Operands,
                     AMDGPUOperand::ImmTy ImmTy = AMDGPUOperand::ImmTyNone,
                     std::function<bool(int64_t &)> ConvertResult = nullptr);

  ParseStatus parseOperandArrayWithPrefix(
      const char *Prefix, OperandVector &Operands,
      AMDGPUOperand::ImmTy ImmTy = AMDGPUOperand::ImmTyNone,
      bool (*ConvertResult)(int64_t &) = nullptr);

  ParseStatus
  parseNamedBit(StringRef Name, OperandVector &Operands,
                AMDGPUOperand::ImmTy ImmTy = AMDGPUOperand::ImmTyNone);
  unsigned getCPolKind(StringRef Id, StringRef Mnemo, bool &Disabling) const;
  ParseStatus parseCPol(OperandVector &Operands);
  ParseStatus parseScope(OperandVector &Operands, int64_t &Scope);
  ParseStatus parseTH(OperandVector &Operands, int64_t &TH);
  ParseStatus parseStringWithPrefix(StringRef Prefix, StringRef &Value,
                                    SMLoc &StringLoc);

  bool isModifier();
  bool isOperandModifier(const AsmToken &Token, const AsmToken &NextToken) const;
  bool isRegOrOperandModifier(const AsmToken &Token, const AsmToken &NextToken) const;
  bool isNamedOperandModifier(const AsmToken &Token, const AsmToken &NextToken) const;
  bool isOpcodeModifierWithVal(const AsmToken &Token, const AsmToken &NextToken) const;
  bool parseSP3NegModifier();
  ParseStatus parseImm(OperandVector &Operands, bool HasSP3AbsModifier = false,
                       bool HasLit = false);
  ParseStatus parseReg(OperandVector &Operands);
  ParseStatus parseRegOrImm(OperandVector &Operands, bool HasSP3AbsMod = false,
                            bool HasLit = false);
  ParseStatus parseRegOrImmWithFPInputMods(OperandVector &Operands,
                                           bool AllowImm = true);
  ParseStatus parseRegOrImmWithIntInputMods(OperandVector &Operands,
                                            bool AllowImm = true);
  ParseStatus parseRegWithFPInputMods(OperandVector &Operands);
  ParseStatus parseRegWithIntInputMods(OperandVector &Operands);
  ParseStatus parseVReg32OrOff(OperandVector &Operands);
  ParseStatus tryParseIndexKey(OperandVector &Operands,
                               AMDGPUOperand::ImmTy ImmTy);
  ParseStatus parseIndexKey8bit(OperandVector &Operands);
  ParseStatus parseIndexKey16bit(OperandVector &Operands);

  ParseStatus parseDfmtNfmt(int64_t &Format);
  ParseStatus parseUfmt(int64_t &Format);
  ParseStatus parseSymbolicSplitFormat(StringRef FormatStr, SMLoc Loc,
                                       int64_t &Format);
  ParseStatus parseSymbolicUnifiedFormat(StringRef FormatStr, SMLoc Loc,
                                         int64_t &Format);
  ParseStatus parseFORMAT(OperandVector &Operands);
  ParseStatus parseSymbolicOrNumericFormat(int64_t &Format);
  ParseStatus parseNumericFormat(int64_t &Format);
  ParseStatus parseFlatOffset(OperandVector &Operands);
  ParseStatus parseR128A16(OperandVector &Operands);
  ParseStatus parseBLGP(OperandVector &Operands);
  bool tryParseFmt(const char *Pref, int64_t MaxVal, int64_t &Val);
  bool matchDfmtNfmt(int64_t &Dfmt, int64_t &Nfmt, StringRef FormatStr, SMLoc Loc);

  void cvtExp(MCInst &Inst, const OperandVector &Operands);

  bool parseCnt(int64_t &IntVal);
  ParseStatus parseSWaitCnt(OperandVector &Operands);

  bool parseDepCtr(int64_t &IntVal, unsigned &Mask);
  void depCtrError(SMLoc Loc, int ErrorId, StringRef DepCtrName);
  ParseStatus parseDepCtr(OperandVector &Operands);

  bool parseDelay(int64_t &Delay);
  ParseStatus parseSDelayALU(OperandVector &Operands);

  ParseStatus parseHwreg(OperandVector &Operands);

private:
  struct OperandInfoTy {
    SMLoc Loc;
    int64_t Val;
    bool IsSymbolic = false;
    bool IsDefined = false;

    OperandInfoTy(int64_t Val) : Val(Val) {}
  };

  struct StructuredOpField : OperandInfoTy {
    StringLiteral Id;
    StringLiteral Desc;
    unsigned Width;
    bool IsDefined = false;

    StructuredOpField(StringLiteral Id, StringLiteral Desc, unsigned Width,
                      int64_t Default)
        : OperandInfoTy(Default), Id(Id), Desc(Desc), Width(Width) {}
    virtual ~StructuredOpField() = default;

    bool Error(AMDGPUAsmParser &Parser, const Twine &Err) const {
      Parser.Error(Loc, "invalid " + Desc + ": " + Err);
      return false;
    }

    virtual bool validate(AMDGPUAsmParser &Parser) const {
      if (IsSymbolic && Val == OPR_ID_UNSUPPORTED)
        return Error(Parser, "not supported on this GPU");
      if (!isUIntN(Width, Val))
        return Error(Parser, "only " + Twine(Width) + "-bit values are legal");
      return true;
    }
  };

  ParseStatus parseStructuredOpFields(ArrayRef<StructuredOpField *> Fields);
  bool validateStructuredOpFields(ArrayRef<const StructuredOpField *> Fields);

  bool parseSendMsgBody(OperandInfoTy &Msg, OperandInfoTy &Op, OperandInfoTy &Stream);
  bool validateSendMsg(const OperandInfoTy &Msg,
                       const OperandInfoTy &Op,
                       const OperandInfoTy &Stream);

  ParseStatus parseHwregFunc(OperandInfoTy &HwReg, OperandInfoTy &Offset,
                             OperandInfoTy &Width);

  SMLoc getFlatOffsetLoc(const OperandVector &Operands) const;
  SMLoc getSMEMOffsetLoc(const OperandVector &Operands) const;
  SMLoc getBLGPLoc(const OperandVector &Operands) const;

  SMLoc getOperandLoc(std::function<bool(const AMDGPUOperand&)> Test,
                      const OperandVector &Operands) const;
  SMLoc getImmLoc(AMDGPUOperand::ImmTy Type, const OperandVector &Operands) const;
  SMLoc getRegLoc(unsigned Reg, const OperandVector &Operands) const;
  SMLoc getLitLoc(const OperandVector &Operands,
                  bool SearchMandatoryLiterals = false) const;
  SMLoc getMandatoryLitLoc(const OperandVector &Operands) const;
  SMLoc getConstLoc(const OperandVector &Operands) const;
  SMLoc getInstLoc(const OperandVector &Operands) const;

  bool validateInstruction(const MCInst &Inst, const SMLoc &IDLoc, const OperandVector &Operands);
  bool validateOffset(const MCInst &Inst, const OperandVector &Operands);
  bool validateFlatOffset(const MCInst &Inst, const OperandVector &Operands);
  bool validateSMEMOffset(const MCInst &Inst, const OperandVector &Operands);
  bool validateSOPLiteral(const MCInst &Inst) const;
  bool validateConstantBusLimitations(const MCInst &Inst, const OperandVector &Operands);
  bool validateVOPDRegBankConstraints(const MCInst &Inst,
                                      const OperandVector &Operands);
  bool validateIntClampSupported(const MCInst &Inst);
  bool validateMIMGAtomicDMask(const MCInst &Inst);
  bool validateMIMGGatherDMask(const MCInst &Inst);
  bool validateMovrels(const MCInst &Inst, const OperandVector &Operands);
  bool validateMIMGDataSize(const MCInst &Inst, const SMLoc &IDLoc);
  bool validateMIMGAddrSize(const MCInst &Inst, const SMLoc &IDLoc);
  bool validateMIMGD16(const MCInst &Inst);
  bool validateMIMGMSAA(const MCInst &Inst);
  bool validateOpSel(const MCInst &Inst);
  bool validateNeg(const MCInst &Inst, int OpName);
  bool validateDPP(const MCInst &Inst, const OperandVector &Operands);
  bool validateVccOperand(unsigned Reg) const;
  bool validateVOPLiteral(const MCInst &Inst, const OperandVector &Operands);
  bool validateMAIAccWrite(const MCInst &Inst, const OperandVector &Operands);
  bool validateMAISrc2(const MCInst &Inst, const OperandVector &Operands);
  bool validateMFMA(const MCInst &Inst, const OperandVector &Operands);
  bool validateAGPRLdSt(const MCInst &Inst) const;
  bool validateVGPRAlign(const MCInst &Inst) const;
  bool validateBLGP(const MCInst &Inst, const OperandVector &Operands);
  bool validateDS(const MCInst &Inst, const OperandVector &Operands);
  bool validateGWS(const MCInst &Inst, const OperandVector &Operands);
  bool validateDivScale(const MCInst &Inst);
  bool validateWaitCnt(const MCInst &Inst, const OperandVector &Operands);
  bool validateCoherencyBits(const MCInst &Inst, const OperandVector &Operands,
                             const SMLoc &IDLoc);
  bool validateTHAndScopeBits(const MCInst &Inst, const OperandVector &Operands,
                              const unsigned CPol);
  bool validateExeczVcczOperands(const OperandVector &Operands);
  bool validateTFE(const MCInst &Inst, const OperandVector &Operands);
  std::optional<StringRef> validateLdsDirect(const MCInst &Inst);
  unsigned getConstantBusLimit(unsigned Opcode) const;
  bool usesConstantBus(const MCInst &Inst, unsigned OpIdx);
  bool isInlineConstant(const MCInst &Inst, unsigned OpIdx) const;
  unsigned findImplicitSGPRReadInVOP(const MCInst &Inst) const;

  bool isSupportedMnemo(StringRef Mnemo,
                        const FeatureBitset &FBS);
  bool isSupportedMnemo(StringRef Mnemo,
                        const FeatureBitset &FBS,
                        ArrayRef<unsigned> Variants);
  bool checkUnsupportedInstruction(StringRef Name, const SMLoc &IDLoc);

  bool isId(const StringRef Id) const;
  bool isId(const AsmToken &Token, const StringRef Id) const;
  bool isToken(const AsmToken::TokenKind Kind) const;
  StringRef getId() const;
  bool trySkipId(const StringRef Id);
  bool trySkipId(const StringRef Pref, const StringRef Id);
  bool trySkipId(const StringRef Id, const AsmToken::TokenKind Kind);
  bool trySkipToken(const AsmToken::TokenKind Kind);
  bool skipToken(const AsmToken::TokenKind Kind, const StringRef ErrMsg);
  bool parseString(StringRef &Val, const StringRef ErrMsg = "expected a string");
  bool parseId(StringRef &Val, const StringRef ErrMsg = "");

  void peekTokens(MutableArrayRef<AsmToken> Tokens);
  AsmToken::TokenKind getTokenKind() const;
  bool parseExpr(int64_t &Imm, StringRef Expected = "");
  bool parseExpr(OperandVector &Operands);
  StringRef getTokenStr() const;
  AsmToken peekToken(bool ShouldSkipSpace = true);
  AsmToken getToken() const;
  SMLoc getLoc() const;
  void lex();

public:
  void onBeginOfFile() override;
  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;

  ParseStatus parseCustomOperand(OperandVector &Operands, unsigned MCK);

  ParseStatus parseExpTgt(OperandVector &Operands);
  ParseStatus parseSendMsg(OperandVector &Operands);
  ParseStatus parseInterpSlot(OperandVector &Operands);
  ParseStatus parseInterpAttr(OperandVector &Operands);
  ParseStatus parseSOPPBrTarget(OperandVector &Operands);
  ParseStatus parseBoolReg(OperandVector &Operands);

  bool parseSwizzleOperand(int64_t &Op,
                           const unsigned MinVal,
                           const unsigned MaxVal,
                           const StringRef ErrMsg,
                           SMLoc &Loc);
  bool parseSwizzleOperands(const unsigned OpNum, int64_t* Op,
                            const unsigned MinVal,
                            const unsigned MaxVal,
                            const StringRef ErrMsg);
  ParseStatus parseSwizzle(OperandVector &Operands);
  bool parseSwizzleOffset(int64_t &Imm);
  bool parseSwizzleMacro(int64_t &Imm);
  bool parseSwizzleQuadPerm(int64_t &Imm);
  bool parseSwizzleBitmaskPerm(int64_t &Imm);
  bool parseSwizzleBroadcast(int64_t &Imm);
  bool parseSwizzleSwap(int64_t &Imm);
  bool parseSwizzleReverse(int64_t &Imm);

  ParseStatus parseGPRIdxMode(OperandVector &Operands);
  int64_t parseGPRIdxMacro();

  void cvtMubuf(MCInst &Inst, const OperandVector &Operands) { cvtMubufImpl(Inst, Operands, false); }
  void cvtMubufAtomic(MCInst &Inst, const OperandVector &Operands) { cvtMubufImpl(Inst, Operands, true); }

  ParseStatus parseOModSI(OperandVector &Operands);

  void cvtVOP3(MCInst &Inst, const OperandVector &Operands,
               OptionalImmIndexMap &OptionalIdx);
  void cvtVOP3OpSel(MCInst &Inst, const OperandVector &Operands);
  void cvtVOP3(MCInst &Inst, const OperandVector &Operands);
  void cvtVOP3P(MCInst &Inst, const OperandVector &Operands);
  void cvtSWMMAC(MCInst &Inst, const OperandVector &Operands);

  void cvtVOPD(MCInst &Inst, const OperandVector &Operands);
  void cvtVOP3OpSel(MCInst &Inst, const OperandVector &Operands,
                    OptionalImmIndexMap &OptionalIdx);
  void cvtVOP3P(MCInst &Inst, const OperandVector &Operands,
                OptionalImmIndexMap &OptionalIdx);

  void cvtVOP3Interp(MCInst &Inst, const OperandVector &Operands);
  void cvtVINTERP(MCInst &Inst, const OperandVector &Operands);

  bool parseDimId(unsigned &Encoding);
  ParseStatus parseDim(OperandVector &Operands);
  bool convertDppBoundCtrl(int64_t &BoundCtrl);
  ParseStatus parseDPP8(OperandVector &Operands);
  ParseStatus parseDPPCtrl(OperandVector &Operands);
  bool isSupportedDPPCtrl(StringRef Ctrl, const OperandVector &Operands);
  int64_t parseDPPCtrlSel(StringRef Ctrl);
  int64_t parseDPPCtrlPerm();
  void cvtDPP(MCInst &Inst, const OperandVector &Operands, bool IsDPP8 = false);
  void cvtDPP8(MCInst &Inst, const OperandVector &Operands) {
    cvtDPP(Inst, Operands, true);
  }
  void cvtVOP3DPP(MCInst &Inst, const OperandVector &Operands,
                  bool IsDPP8 = false);
  void cvtVOP3DPP8(MCInst &Inst, const OperandVector &Operands) {
    cvtVOP3DPP(Inst, Operands, true);
  }

  ParseStatus parseSDWASel(OperandVector &Operands, StringRef Prefix,
                           AMDGPUOperand::ImmTy Type);
  ParseStatus parseSDWADstUnused(OperandVector &Operands);
  void cvtSdwaVOP1(MCInst &Inst, const OperandVector &Operands);
  void cvtSdwaVOP2(MCInst &Inst, const OperandVector &Operands);
  void cvtSdwaVOP2b(MCInst &Inst, const OperandVector &Operands);
  void cvtSdwaVOP2e(MCInst &Inst, const OperandVector &Operands);
  void cvtSdwaVOPC(MCInst &Inst, const OperandVector &Operands);
  void cvtSDWA(MCInst &Inst, const OperandVector &Operands,
               uint64_t BasicInstType,
               bool SkipDstVcc = false,
               bool SkipSrcVcc = false);

  ParseStatus parseEndpgm(OperandVector &Operands);

  ParseStatus parseVOPD(OperandVector &Operands);
};

} // end anonymous namespace

// May be called with integer type with equivalent bitwidth.
static const fltSemantics *getFltSemantics(unsigned Size) {
  switch (Size) {
  case 4:
    return &APFloat::IEEEsingle();
  case 8:
    return &APFloat::IEEEdouble();
  case 2:
    return &APFloat::IEEEhalf();
  default:
    llvm_unreachable("unsupported fp type");
  }
}

static const fltSemantics *getFltSemantics(MVT VT) {
  return getFltSemantics(VT.getSizeInBits() / 8);
}

static const fltSemantics *getOpFltSemantics(uint8_t OperandType) {
  switch (OperandType) {
  // When floating-point immediate is used as operand of type i16, the 32-bit
   // representation of the constant truncated to the 16 LSBs should be used.
  case AMDGPU::OPERAND_REG_IMM_INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT16:
  case AMDGPU::OPERAND_REG_IMM_INT32:
  case AMDGPU::OPERAND_REG_IMM_FP32:
  case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_FP32:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT32:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP32:
  case AMDGPU::OPERAND_REG_IMM_V2FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT32:
  case AMDGPU::OPERAND_REG_IMM_V2INT32:
  case AMDGPU::OPERAND_REG_IMM_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2INT16:
  case AMDGPU::OPERAND_KIMM32:
  case AMDGPU::OPERAND_INLINE_SPLIT_BARRIER_INT32:
    return &APFloat::IEEEsingle();
  case AMDGPU::OPERAND_REG_IMM_INT64:
  case AMDGPU::OPERAND_REG_IMM_FP64:
  case AMDGPU::OPERAND_REG_INLINE_C_INT64:
  case AMDGPU::OPERAND_REG_INLINE_C_FP64:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP64:
    return &APFloat::IEEEdouble();
  case AMDGPU::OPERAND_REG_IMM_FP16:
  case AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16:
  case AMDGPU::OPERAND_REG_IMM_V2FP16:
  case AMDGPU::OPERAND_KIMM16:
    return &APFloat::IEEEhalf();
  case AMDGPU::OPERAND_REG_IMM_BF16:
  case AMDGPU::OPERAND_REG_IMM_BF16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_BF16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2BF16:
  case AMDGPU::OPERAND_REG_IMM_V2BF16:
    return &APFloat::BFloat();
  default:
    llvm_unreachable("unsupported fp type");
  }
}

//===----------------------------------------------------------------------===//
// Operand
//===----------------------------------------------------------------------===//

static bool canLosslesslyConvertToFPType(APFloat &FPLiteral, MVT VT) {
  bool Lost;

  // Convert literal to single precision
  APFloat::opStatus Status = FPLiteral.convert(*getFltSemantics(VT),
                                               APFloat::rmNearestTiesToEven,
                                               &Lost);
  // We allow precision lost but not overflow or underflow
  if (Status != APFloat::opOK &&
      Lost &&
      ((Status & APFloat::opOverflow)  != 0 ||
       (Status & APFloat::opUnderflow) != 0)) {
    return false;
  }

  return true;
}

static bool isSafeTruncation(int64_t Val, unsigned Size) {
  return isUIntN(Size, Val) || isIntN(Size, Val);
}

static bool isInlineableLiteralOp16(int64_t Val, MVT VT, bool HasInv2Pi) {
  if (VT.getScalarType() == MVT::i16)
    return isInlinableLiteral32(Val, HasInv2Pi);

  if (VT.getScalarType() == MVT::f16)
    return AMDGPU::isInlinableLiteralFP16(Val, HasInv2Pi);

  assert(VT.getScalarType() == MVT::bf16);

  return AMDGPU::isInlinableLiteralBF16(Val, HasInv2Pi);
}

bool AMDGPUOperand::isInlinableImm(MVT type) const {

  // This is a hack to enable named inline values like
  // shared_base with both 32-bit and 64-bit operands.
  // Note that these values are defined as
  // 32-bit operands only.
  if (isInlineValue()) {
    return true;
  }

  if (!isImmTy(ImmTyNone)) {
    // Only plain immediates are inlinable (e.g. "clamp" attribute is not)
    return false;
  }
  // TODO: We should avoid using host float here. It would be better to
  // check the float bit values which is what a few other places do.
  // We've had bot failures before due to weird NaN support on mips hosts.

  APInt Literal(64, Imm.Val);

  if (Imm.IsFPImm) { // We got fp literal token
    if (type == MVT::f64 || type == MVT::i64) { // Expected 64-bit operand
      return AMDGPU::isInlinableLiteral64(Imm.Val,
                                          AsmParser->hasInv2PiInlineImm());
    }

    APFloat FPLiteral(APFloat::IEEEdouble(), APInt(64, Imm.Val));
    if (!canLosslesslyConvertToFPType(FPLiteral, type))
      return false;

    if (type.getScalarSizeInBits() == 16) {
      bool Lost = false;
      switch (type.getScalarType().SimpleTy) {
      default:
        llvm_unreachable("unknown 16-bit type");
      case MVT::bf16:
        FPLiteral.convert(APFloatBase::BFloat(), APFloat::rmNearestTiesToEven,
                          &Lost);
        break;
      case MVT::f16:
        FPLiteral.convert(APFloatBase::IEEEhalf(), APFloat::rmNearestTiesToEven,
                          &Lost);
        break;
      case MVT::i16:
        FPLiteral.convert(APFloatBase::IEEEsingle(),
                          APFloat::rmNearestTiesToEven, &Lost);
        break;
      }
      // We need to use 32-bit representation here because when a floating-point
      // inline constant is used as an i16 operand, its 32-bit representation
      // representation will be used. We will need the 32-bit value to check if
      // it is FP inline constant.
      uint32_t ImmVal = FPLiteral.bitcastToAPInt().getZExtValue();
      return isInlineableLiteralOp16(ImmVal, type,
                                     AsmParser->hasInv2PiInlineImm());
    }

    // Check if single precision literal is inlinable
    return AMDGPU::isInlinableLiteral32(
      static_cast<int32_t>(FPLiteral.bitcastToAPInt().getZExtValue()),
      AsmParser->hasInv2PiInlineImm());
  }

  // We got int literal token.
  if (type == MVT::f64 || type == MVT::i64) { // Expected 64-bit operand
    return AMDGPU::isInlinableLiteral64(Imm.Val,
                                        AsmParser->hasInv2PiInlineImm());
  }

  if (!isSafeTruncation(Imm.Val, type.getScalarSizeInBits())) {
    return false;
  }

  if (type.getScalarSizeInBits() == 16) {
    return isInlineableLiteralOp16(
      static_cast<int16_t>(Literal.getLoBits(16).getSExtValue()),
      type, AsmParser->hasInv2PiInlineImm());
  }

  return AMDGPU::isInlinableLiteral32(
    static_cast<int32_t>(Literal.getLoBits(32).getZExtValue()),
    AsmParser->hasInv2PiInlineImm());
}

bool AMDGPUOperand::isLiteralImm(MVT type) const {
  // Check that this immediate can be added as literal
  if (!isImmTy(ImmTyNone)) {
    return false;
  }

  if (!Imm.IsFPImm) {
    // We got int literal token.

    if (type == MVT::f64 && hasFPModifiers()) {
      // Cannot apply fp modifiers to int literals preserving the same semantics
      // for VOP1/2/C and VOP3 because of integer truncation. To avoid ambiguity,
      // disable these cases.
      return false;
    }

    unsigned Size = type.getSizeInBits();
    if (Size == 64)
      Size = 32;

    // FIXME: 64-bit operands can zero extend, sign extend, or pad zeroes for FP
    // types.
    return isSafeTruncation(Imm.Val, Size);
  }

  // We got fp literal token
  if (type == MVT::f64) { // Expected 64-bit fp operand
    // We would set low 64-bits of literal to zeroes but we accept this literals
    return true;
  }

  if (type == MVT::i64) { // Expected 64-bit int operand
    // We don't allow fp literals in 64-bit integer instructions. It is
    // unclear how we should encode them.
    return false;
  }

  // We allow fp literals with f16x2 operands assuming that the specified
  // literal goes into the lower half and the upper half is zero. We also
  // require that the literal may be losslessly converted to f16.
  //
  // For i16x2 operands, we assume that the specified literal is encoded as a
  // single-precision float. This is pretty odd, but it matches SP3 and what
  // happens in hardware.
  MVT ExpectedType = (type == MVT::v2f16)   ? MVT::f16
                     : (type == MVT::v2i16) ? MVT::f32
                     : (type == MVT::v2f32) ? MVT::f32
                                            : type;

  APFloat FPLiteral(APFloat::IEEEdouble(), APInt(64, Imm.Val));
  return canLosslesslyConvertToFPType(FPLiteral, ExpectedType);
}

bool AMDGPUOperand::isRegClass(unsigned RCID) const {
  return isRegKind() && AsmParser->getMRI()->getRegClass(RCID).contains(getReg());
}

bool AMDGPUOperand::isVRegWithInputMods() const {
  return isRegClass(AMDGPU::VGPR_32RegClassID) ||
         // GFX90A allows DPP on 64-bit operands.
         (isRegClass(AMDGPU::VReg_64RegClassID) &&
          AsmParser->getFeatureBits()[AMDGPU::FeatureDPALU_DPP]);
}

template <bool IsFake16> bool AMDGPUOperand::isT16VRegWithInputMods() const {
  return isRegClass(IsFake16 ? AMDGPU::VGPR_32_Lo128RegClassID
                             : AMDGPU::VGPR_16_Lo128RegClassID);
}

bool AMDGPUOperand::isSDWAOperand(MVT type) const {
  if (AsmParser->isVI())
    return isVReg32();
  if (AsmParser->isGFX9Plus())
    return isRegClass(AMDGPU::VS_32RegClassID) || isInlinableImm(type);
  return false;
}

bool AMDGPUOperand::isSDWAFP16Operand() const {
  return isSDWAOperand(MVT::f16);
}

bool AMDGPUOperand::isSDWAFP32Operand() const {
  return isSDWAOperand(MVT::f32);
}

bool AMDGPUOperand::isSDWAInt16Operand() const {
  return isSDWAOperand(MVT::i16);
}

bool AMDGPUOperand::isSDWAInt32Operand() const {
  return isSDWAOperand(MVT::i32);
}

bool AMDGPUOperand::isBoolReg() const {
  auto FB = AsmParser->getFeatureBits();
  return isReg() && ((FB[AMDGPU::FeatureWavefrontSize64] && isSCSrc_b64()) ||
                     (FB[AMDGPU::FeatureWavefrontSize32] && isSCSrc_b32()));
}

uint64_t AMDGPUOperand::applyInputFPModifiers(uint64_t Val, unsigned Size) const
{
  assert(isImmTy(ImmTyNone) && Imm.Mods.hasFPModifiers());
  assert(Size == 2 || Size == 4 || Size == 8);

  const uint64_t FpSignMask = (1ULL << (Size * 8 - 1));

  if (Imm.Mods.Abs) {
    Val &= ~FpSignMask;
  }
  if (Imm.Mods.Neg) {
    Val ^= FpSignMask;
  }

  return Val;
}

void AMDGPUOperand::addImmOperands(MCInst &Inst, unsigned N, bool ApplyModifiers) const {
  if (isExpr()) {
    Inst.addOperand(MCOperand::createExpr(Expr));
    return;
  }

  if (AMDGPU::isSISrcOperand(AsmParser->getMII()->get(Inst.getOpcode()),
                             Inst.getNumOperands())) {
    addLiteralImmOperand(Inst, Imm.Val,
                         ApplyModifiers &
                         isImmTy(ImmTyNone) && Imm.Mods.hasFPModifiers());
  } else {
    assert(!isImmTy(ImmTyNone) || !hasModifiers());
    Inst.addOperand(MCOperand::createImm(Imm.Val));
    setImmKindNone();
  }
}

void AMDGPUOperand::addLiteralImmOperand(MCInst &Inst, int64_t Val, bool ApplyModifiers) const {
  const auto& InstDesc = AsmParser->getMII()->get(Inst.getOpcode());
  auto OpNum = Inst.getNumOperands();
  // Check that this operand accepts literals
  assert(AMDGPU::isSISrcOperand(InstDesc, OpNum));

  if (ApplyModifiers) {
    assert(AMDGPU::isSISrcFPOperand(InstDesc, OpNum));
    const unsigned Size = Imm.IsFPImm ? sizeof(double) : getOperandSize(InstDesc, OpNum);
    Val = applyInputFPModifiers(Val, Size);
  }

  APInt Literal(64, Val);
  uint8_t OpTy = InstDesc.operands()[OpNum].OperandType;

  if (Imm.IsFPImm) { // We got fp literal token
    switch (OpTy) {
    case AMDGPU::OPERAND_REG_IMM_INT64:
    case AMDGPU::OPERAND_REG_IMM_FP64:
    case AMDGPU::OPERAND_REG_INLINE_C_INT64:
    case AMDGPU::OPERAND_REG_INLINE_C_FP64:
    case AMDGPU::OPERAND_REG_INLINE_AC_FP64:
      if (AMDGPU::isInlinableLiteral64(Literal.getZExtValue(),
                                       AsmParser->hasInv2PiInlineImm())) {
        Inst.addOperand(MCOperand::createImm(Literal.getZExtValue()));
        setImmKindConst();
        return;
      }

      // Non-inlineable
      if (AMDGPU::isSISrcFPOperand(InstDesc, OpNum)) { // Expected 64-bit fp operand
        // For fp operands we check if low 32 bits are zeros
        if (Literal.getLoBits(32) != 0) {
          const_cast<AMDGPUAsmParser *>(AsmParser)->Warning(Inst.getLoc(),
          "Can't encode literal as exact 64-bit floating-point operand. "
          "Low 32-bits will be set to zero");
          Val &= 0xffffffff00000000u;
        }

        Inst.addOperand(MCOperand::createImm(Val));
        setImmKindLiteral();
        return;
      }

      // We don't allow fp literals in 64-bit integer instructions. It is
      // unclear how we should encode them. This case should be checked earlier
      // in predicate methods (isLiteralImm())
      llvm_unreachable("fp literal in 64-bit integer instruction.");

    case AMDGPU::OPERAND_REG_IMM_BF16:
    case AMDGPU::OPERAND_REG_IMM_BF16_DEFERRED:
    case AMDGPU::OPERAND_REG_INLINE_C_BF16:
    case AMDGPU::OPERAND_REG_INLINE_C_V2BF16:
    case AMDGPU::OPERAND_REG_INLINE_AC_BF16:
    case AMDGPU::OPERAND_REG_INLINE_AC_V2BF16:
    case AMDGPU::OPERAND_REG_IMM_V2BF16:
      if (AsmParser->hasInv2PiInlineImm() && Literal == 0x3fc45f306725feed) {
        // This is the 1/(2*pi) which is going to be truncated to bf16 with the
        // loss of precision. The constant represents ideomatic fp32 value of
        // 1/(2*pi) = 0.15915494 since bf16 is in fact fp32 with cleared low 16
        // bits. Prevent rounding below.
        Inst.addOperand(MCOperand::createImm(0x3e22));
        setImmKindLiteral();
        return;
      }
      [[fallthrough]];

    case AMDGPU::OPERAND_REG_IMM_INT32:
    case AMDGPU::OPERAND_REG_IMM_FP32:
    case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
    case AMDGPU::OPERAND_REG_INLINE_C_INT32:
    case AMDGPU::OPERAND_REG_INLINE_C_FP32:
    case AMDGPU::OPERAND_REG_INLINE_AC_INT32:
    case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
    case AMDGPU::OPERAND_REG_IMM_INT16:
    case AMDGPU::OPERAND_REG_IMM_FP16:
    case AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED:
    case AMDGPU::OPERAND_REG_INLINE_C_INT16:
    case AMDGPU::OPERAND_REG_INLINE_C_FP16:
    case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
    case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
    case AMDGPU::OPERAND_REG_INLINE_AC_INT16:
    case AMDGPU::OPERAND_REG_INLINE_AC_FP16:
    case AMDGPU::OPERAND_REG_INLINE_AC_V2INT16:
    case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16:
    case AMDGPU::OPERAND_REG_IMM_V2INT16:
    case AMDGPU::OPERAND_REG_IMM_V2FP16:
    case AMDGPU::OPERAND_REG_INLINE_C_V2FP32:
    case AMDGPU::OPERAND_REG_IMM_V2FP32:
    case AMDGPU::OPERAND_REG_INLINE_C_V2INT32:
    case AMDGPU::OPERAND_REG_IMM_V2INT32:
    case AMDGPU::OPERAND_KIMM32:
    case AMDGPU::OPERAND_KIMM16:
    case AMDGPU::OPERAND_INLINE_SPLIT_BARRIER_INT32: {
      bool lost;
      APFloat FPLiteral(APFloat::IEEEdouble(), Literal);
      // Convert literal to single precision
      FPLiteral.convert(*getOpFltSemantics(OpTy),
                        APFloat::rmNearestTiesToEven, &lost);
      // We allow precision lost but not overflow or underflow. This should be
      // checked earlier in isLiteralImm()

      uint64_t ImmVal = FPLiteral.bitcastToAPInt().getZExtValue();
      Inst.addOperand(MCOperand::createImm(ImmVal));
      if (OpTy == AMDGPU::OPERAND_KIMM32 || OpTy == AMDGPU::OPERAND_KIMM16) {
        setImmKindMandatoryLiteral();
      } else {
        setImmKindLiteral();
      }
      return;
    }
    default:
      llvm_unreachable("invalid operand size");
    }

    return;
  }

  // We got int literal token.
  // Only sign extend inline immediates.
  switch (OpTy) {
  case AMDGPU::OPERAND_REG_IMM_INT32:
  case AMDGPU::OPERAND_REG_IMM_FP32:
  case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_FP32:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT32:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
  case AMDGPU::OPERAND_REG_IMM_V2INT16:
  case AMDGPU::OPERAND_REG_IMM_V2BF16:
  case AMDGPU::OPERAND_REG_IMM_V2FP16:
  case AMDGPU::OPERAND_REG_IMM_V2FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP32:
  case AMDGPU::OPERAND_REG_IMM_V2INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT32:
  case AMDGPU::OPERAND_INLINE_SPLIT_BARRIER_INT32:
    if (isSafeTruncation(Val, 32) &&
        AMDGPU::isInlinableLiteral32(static_cast<int32_t>(Val),
                                     AsmParser->hasInv2PiInlineImm())) {
      Inst.addOperand(MCOperand::createImm(Val));
      setImmKindConst();
      return;
    }

    Inst.addOperand(MCOperand::createImm(Val & 0xffffffff));
    setImmKindLiteral();
    return;

  case AMDGPU::OPERAND_REG_IMM_INT64:
  case AMDGPU::OPERAND_REG_IMM_FP64:
  case AMDGPU::OPERAND_REG_INLINE_C_INT64:
  case AMDGPU::OPERAND_REG_INLINE_C_FP64:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP64:
    if (AMDGPU::isInlinableLiteral64(Val, AsmParser->hasInv2PiInlineImm())) {
      Inst.addOperand(MCOperand::createImm(Val));
      setImmKindConst();
      return;
    }

    Val = AMDGPU::isSISrcFPOperand(InstDesc, OpNum) ? (uint64_t)Val << 32
                                                    : Lo_32(Val);

    Inst.addOperand(MCOperand::createImm(Val));
    setImmKindLiteral();
    return;

  case AMDGPU::OPERAND_REG_IMM_INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT16:
    if (isSafeTruncation(Val, 16) &&
        AMDGPU::isInlinableIntLiteral(static_cast<int16_t>(Val))) {
      Inst.addOperand(MCOperand::createImm(Val & 0xffffffff));
      setImmKindConst();
      return;
    }

    Inst.addOperand(MCOperand::createImm(Val & 0xffff));
    setImmKindLiteral();
    return;

  case AMDGPU::OPERAND_REG_INLINE_C_FP16:
  case AMDGPU::OPERAND_REG_IMM_FP16:
  case AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP16:
    if (isSafeTruncation(Val, 16) &&
        AMDGPU::isInlinableLiteralFP16(static_cast<int16_t>(Val),
                                       AsmParser->hasInv2PiInlineImm())) {
      Inst.addOperand(MCOperand::createImm(Val));
      setImmKindConst();
      return;
    }

    Inst.addOperand(MCOperand::createImm(Val & 0xffff));
    setImmKindLiteral();
    return;

  case AMDGPU::OPERAND_REG_IMM_BF16:
  case AMDGPU::OPERAND_REG_IMM_BF16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_BF16:
    if (isSafeTruncation(Val, 16) &&
        AMDGPU::isInlinableLiteralBF16(static_cast<int16_t>(Val),
                                     AsmParser->hasInv2PiInlineImm())) {
      Inst.addOperand(MCOperand::createImm(Val));
      setImmKindConst();
      return;
    }

    Inst.addOperand(MCOperand::createImm(Val & 0xffff));
    setImmKindLiteral();
    return;

  case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2INT16: {
    assert(isSafeTruncation(Val, 16));
    assert(AMDGPU::isInlinableIntLiteral(static_cast<int16_t>(Val)));
    Inst.addOperand(MCOperand::createImm(Val));
    return;
  }
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16: {
    assert(isSafeTruncation(Val, 16));
    assert(AMDGPU::isInlinableLiteralFP16(static_cast<int16_t>(Val),
                                          AsmParser->hasInv2PiInlineImm()));

    Inst.addOperand(MCOperand::createImm(Val));
    return;
  }

  case AMDGPU::OPERAND_REG_INLINE_C_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2BF16: {
    assert(isSafeTruncation(Val, 16));
    assert(AMDGPU::isInlinableLiteralBF16(static_cast<int16_t>(Val),
                                          AsmParser->hasInv2PiInlineImm()));

    Inst.addOperand(MCOperand::createImm(Val));
    return;
  }

  case AMDGPU::OPERAND_KIMM32:
    Inst.addOperand(MCOperand::createImm(Literal.getLoBits(32).getZExtValue()));
    setImmKindMandatoryLiteral();
    return;
  case AMDGPU::OPERAND_KIMM16:
    Inst.addOperand(MCOperand::createImm(Literal.getLoBits(16).getZExtValue()));
    setImmKindMandatoryLiteral();
    return;
  default:
    llvm_unreachable("invalid operand size");
  }
}

void AMDGPUOperand::addRegOperands(MCInst &Inst, unsigned N) const {
  Inst.addOperand(MCOperand::createReg(AMDGPU::getMCReg(getReg(), AsmParser->getSTI())));
}

bool AMDGPUOperand::isInlineValue() const {
  return isRegKind() && ::isInlineValue(getReg());
}

//===----------------------------------------------------------------------===//
// AsmParser
//===----------------------------------------------------------------------===//

void AMDGPUAsmParser::createConstantSymbol(StringRef Id, int64_t Val) {
  // TODO: make those pre-defined variables read-only.
  // Currently there is none suitable machinery in the core llvm-mc for this.
  // MCSymbol::isRedefinable is intended for another purpose, and
  // AsmParser::parseDirectiveSet() cannot be specialized for specific target.
  MCContext &Ctx = getContext();
  MCSymbol *Sym = Ctx.getOrCreateSymbol(Id);
  Sym->setVariableValue(MCConstantExpr::create(Val, Ctx));
}

static int getRegClass(RegisterKind Is, unsigned RegWidth) {
  if (Is == IS_VGPR) {
    switch (RegWidth) {
      default: return -1;
      case 32:
        return AMDGPU::VGPR_32RegClassID;
      case 64:
        return AMDGPU::VReg_64RegClassID;
      case 96:
        return AMDGPU::VReg_96RegClassID;
      case 128:
        return AMDGPU::VReg_128RegClassID;
      case 160:
        return AMDGPU::VReg_160RegClassID;
      case 192:
        return AMDGPU::VReg_192RegClassID;
      case 224:
        return AMDGPU::VReg_224RegClassID;
      case 256:
        return AMDGPU::VReg_256RegClassID;
      case 288:
        return AMDGPU::VReg_288RegClassID;
      case 320:
        return AMDGPU::VReg_320RegClassID;
      case 352:
        return AMDGPU::VReg_352RegClassID;
      case 384:
        return AMDGPU::VReg_384RegClassID;
      case 512:
        return AMDGPU::VReg_512RegClassID;
      case 1024:
        return AMDGPU::VReg_1024RegClassID;
    }
  } else if (Is == IS_TTMP) {
    switch (RegWidth) {
      default: return -1;
      case 32:
        return AMDGPU::TTMP_32RegClassID;
      case 64:
        return AMDGPU::TTMP_64RegClassID;
      case 128:
        return AMDGPU::TTMP_128RegClassID;
      case 256:
        return AMDGPU::TTMP_256RegClassID;
      case 512:
        return AMDGPU::TTMP_512RegClassID;
    }
  } else if (Is == IS_SGPR) {
    switch (RegWidth) {
      default: return -1;
      case 32:
        return AMDGPU::SGPR_32RegClassID;
      case 64:
        return AMDGPU::SGPR_64RegClassID;
      case 96:
        return AMDGPU::SGPR_96RegClassID;
      case 128:
        return AMDGPU::SGPR_128RegClassID;
      case 160:
        return AMDGPU::SGPR_160RegClassID;
      case 192:
        return AMDGPU::SGPR_192RegClassID;
      case 224:
        return AMDGPU::SGPR_224RegClassID;
      case 256:
        return AMDGPU::SGPR_256RegClassID;
      case 288:
        return AMDGPU::SGPR_288RegClassID;
      case 320:
        return AMDGPU::SGPR_320RegClassID;
      case 352:
        return AMDGPU::SGPR_352RegClassID;
      case 384:
        return AMDGPU::SGPR_384RegClassID;
      case 512:
        return AMDGPU::SGPR_512RegClassID;
    }
  } else if (Is == IS_AGPR) {
    switch (RegWidth) {
      default: return -1;
      case 32:
        return AMDGPU::AGPR_32RegClassID;
      case 64:
        return AMDGPU::AReg_64RegClassID;
      case 96:
        return AMDGPU::AReg_96RegClassID;
      case 128:
        return AMDGPU::AReg_128RegClassID;
      case 160:
        return AMDGPU::AReg_160RegClassID;
      case 192:
        return AMDGPU::AReg_192RegClassID;
      case 224:
        return AMDGPU::AReg_224RegClassID;
      case 256:
        return AMDGPU::AReg_256RegClassID;
      case 288:
        return AMDGPU::AReg_288RegClassID;
      case 320:
        return AMDGPU::AReg_320RegClassID;
      case 352:
        return AMDGPU::AReg_352RegClassID;
      case 384:
        return AMDGPU::AReg_384RegClassID;
      case 512:
        return AMDGPU::AReg_512RegClassID;
      case 1024:
        return AMDGPU::AReg_1024RegClassID;
    }
  }
  return -1;
}

static unsigned getSpecialRegForName(StringRef RegName) {
  return StringSwitch<unsigned>(RegName)
    .Case("exec", AMDGPU::EXEC)
    .Case("vcc", AMDGPU::VCC)
    .Case("flat_scratch", AMDGPU::FLAT_SCR)
    .Case("xnack_mask", AMDGPU::XNACK_MASK)
    .Case("shared_base", AMDGPU::SRC_SHARED_BASE)
    .Case("src_shared_base", AMDGPU::SRC_SHARED_BASE)
    .Case("shared_limit", AMDGPU::SRC_SHARED_LIMIT)
    .Case("src_shared_limit", AMDGPU::SRC_SHARED_LIMIT)
    .Case("private_base", AMDGPU::SRC_PRIVATE_BASE)
    .Case("src_private_base", AMDGPU::SRC_PRIVATE_BASE)
    .Case("private_limit", AMDGPU::SRC_PRIVATE_LIMIT)
    .Case("src_private_limit", AMDGPU::SRC_PRIVATE_LIMIT)
    .Case("pops_exiting_wave_id", AMDGPU::SRC_POPS_EXITING_WAVE_ID)
    .Case("src_pops_exiting_wave_id", AMDGPU::SRC_POPS_EXITING_WAVE_ID)
    .Case("lds_direct", AMDGPU::LDS_DIRECT)
    .Case("src_lds_direct", AMDGPU::LDS_DIRECT)
    .Case("m0", AMDGPU::M0)
    .Case("vccz", AMDGPU::SRC_VCCZ)
    .Case("src_vccz", AMDGPU::SRC_VCCZ)
    .Case("execz", AMDGPU::SRC_EXECZ)
    .Case("src_execz", AMDGPU::SRC_EXECZ)
    .Case("scc", AMDGPU::SRC_SCC)
    .Case("src_scc", AMDGPU::SRC_SCC)
    .Case("tba", AMDGPU::TBA)
    .Case("tma", AMDGPU::TMA)
    .Case("flat_scratch_lo", AMDGPU::FLAT_SCR_LO)
    .Case("flat_scratch_hi", AMDGPU::FLAT_SCR_HI)
    .Case("xnack_mask_lo", AMDGPU::XNACK_MASK_LO)
    .Case("xnack_mask_hi", AMDGPU::XNACK_MASK_HI)
    .Case("vcc_lo", AMDGPU::VCC_LO)
    .Case("vcc_hi", AMDGPU::VCC_HI)
    .Case("exec_lo", AMDGPU::EXEC_LO)
    .Case("exec_hi", AMDGPU::EXEC_HI)
    .Case("tma_lo", AMDGPU::TMA_LO)
    .Case("tma_hi", AMDGPU::TMA_HI)
    .Case("tba_lo", AMDGPU::TBA_LO)
    .Case("tba_hi", AMDGPU::TBA_HI)
    .Case("pc", AMDGPU::PC_REG)
    .Case("null", AMDGPU::SGPR_NULL)
    .Default(AMDGPU::NoRegister);
}

bool AMDGPUAsmParser::ParseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                                    SMLoc &EndLoc, bool RestoreOnFailure) {
  auto R = parseRegister();
  if (!R) return true;
  assert(R->isReg());
  RegNo = R->getReg();
  StartLoc = R->getStartLoc();
  EndLoc = R->getEndLoc();
  return false;
}

bool AMDGPUAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                    SMLoc &EndLoc) {
  return ParseRegister(Reg, StartLoc, EndLoc, /*RestoreOnFailure=*/false);
}

ParseStatus AMDGPUAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
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

bool AMDGPUAsmParser::AddNextRegisterToList(unsigned &Reg, unsigned &RegWidth,
                                            RegisterKind RegKind, unsigned Reg1,
                                            SMLoc Loc) {
  switch (RegKind) {
  case IS_SPECIAL:
    if (Reg == AMDGPU::EXEC_LO && Reg1 == AMDGPU::EXEC_HI) {
      Reg = AMDGPU::EXEC;
      RegWidth = 64;
      return true;
    }
    if (Reg == AMDGPU::FLAT_SCR_LO && Reg1 == AMDGPU::FLAT_SCR_HI) {
      Reg = AMDGPU::FLAT_SCR;
      RegWidth = 64;
      return true;
    }
    if (Reg == AMDGPU::XNACK_MASK_LO && Reg1 == AMDGPU::XNACK_MASK_HI) {
      Reg = AMDGPU::XNACK_MASK;
      RegWidth = 64;
      return true;
    }
    if (Reg == AMDGPU::VCC_LO && Reg1 == AMDGPU::VCC_HI) {
      Reg = AMDGPU::VCC;
      RegWidth = 64;
      return true;
    }
    if (Reg == AMDGPU::TBA_LO && Reg1 == AMDGPU::TBA_HI) {
      Reg = AMDGPU::TBA;
      RegWidth = 64;
      return true;
    }
    if (Reg == AMDGPU::TMA_LO && Reg1 == AMDGPU::TMA_HI) {
      Reg = AMDGPU::TMA;
      RegWidth = 64;
      return true;
    }
    Error(Loc, "register does not fit in the list");
    return false;
  case IS_VGPR:
  case IS_SGPR:
  case IS_AGPR:
  case IS_TTMP:
    if (Reg1 != Reg + RegWidth / 32) {
      Error(Loc, "registers in a list must have consecutive indices");
      return false;
    }
    RegWidth += 32;
    return true;
  default:
    llvm_unreachable("unexpected register kind");
  }
}

struct RegInfo {
  StringLiteral Name;
  RegisterKind Kind;
};

static constexpr RegInfo RegularRegisters[] = {
  {{"v"},    IS_VGPR},
  {{"s"},    IS_SGPR},
  {{"ttmp"}, IS_TTMP},
  {{"acc"},  IS_AGPR},
  {{"a"},    IS_AGPR},
};

static bool isRegularReg(RegisterKind Kind) {
  return Kind == IS_VGPR ||
         Kind == IS_SGPR ||
         Kind == IS_TTMP ||
         Kind == IS_AGPR;
}

static const RegInfo* getRegularRegInfo(StringRef Str) {
  for (const RegInfo &Reg : RegularRegisters)
    if (Str.starts_with(Reg.Name))
      return &Reg;
  return nullptr;
}

static bool getRegNum(StringRef Str, unsigned& Num) {
  return !Str.getAsInteger(10, Num);
}

bool
AMDGPUAsmParser::isRegister(const AsmToken &Token,
                            const AsmToken &NextToken) const {

  // A list of consecutive registers: [s0,s1,s2,s3]
  if (Token.is(AsmToken::LBrac))
    return true;

  if (!Token.is(AsmToken::Identifier))
    return false;

  // A single register like s0 or a range of registers like s[0:1]

  StringRef Str = Token.getString();
  const RegInfo *Reg = getRegularRegInfo(Str);
  if (Reg) {
    StringRef RegName = Reg->Name;
    StringRef RegSuffix = Str.substr(RegName.size());
    if (!RegSuffix.empty()) {
      RegSuffix.consume_back(".l");
      RegSuffix.consume_back(".h");
      unsigned Num;
      // A single register with an index: rXX
      if (getRegNum(RegSuffix, Num))
        return true;
    } else {
      // A range of registers: r[XX:YY].
      if (NextToken.is(AsmToken::LBrac))
        return true;
    }
  }

  return getSpecialRegForName(Str) != AMDGPU::NoRegister;
}

bool
AMDGPUAsmParser::isRegister()
{
  return isRegister(getToken(), peekToken());
}

unsigned AMDGPUAsmParser::getRegularReg(RegisterKind RegKind, unsigned RegNum,
                                        unsigned SubReg, unsigned RegWidth,
                                        SMLoc Loc) {
  assert(isRegularReg(RegKind));

  unsigned AlignSize = 1;
  if (RegKind == IS_SGPR || RegKind == IS_TTMP) {
    // SGPR and TTMP registers must be aligned.
    // Max required alignment is 4 dwords.
    AlignSize = std::min(llvm::bit_ceil(RegWidth / 32), 4u);
  }

  if (RegNum % AlignSize != 0) {
    Error(Loc, "invalid register alignment");
    return AMDGPU::NoRegister;
  }

  unsigned RegIdx = RegNum / AlignSize;
  int RCID = getRegClass(RegKind, RegWidth);
  if (RCID == -1) {
    Error(Loc, "invalid or unsupported register size");
    return AMDGPU::NoRegister;
  }

  const MCRegisterInfo *TRI = getContext().getRegisterInfo();
  const MCRegisterClass RC = TRI->getRegClass(RCID);
  if (RegIdx >= RC.getNumRegs()) {
    Error(Loc, "register index is out of range");
    return AMDGPU::NoRegister;
  }

  unsigned Reg = RC.getRegister(RegIdx);

  if (SubReg) {
    Reg = TRI->getSubReg(Reg, SubReg);

    // Currently all regular registers have their .l and .h subregisters, so
    // we should never need to generate an error here.
    assert(Reg && "Invalid subregister!");
  }

  return Reg;
}

bool AMDGPUAsmParser::ParseRegRange(unsigned &Num, unsigned &RegWidth) {
  int64_t RegLo, RegHi;
  if (!skipToken(AsmToken::LBrac, "missing register index"))
    return false;

  SMLoc FirstIdxLoc = getLoc();
  SMLoc SecondIdxLoc;

  if (!parseExpr(RegLo))
    return false;

  if (trySkipToken(AsmToken::Colon)) {
    SecondIdxLoc = getLoc();
    if (!parseExpr(RegHi))
      return false;
  } else {
    RegHi = RegLo;
  }

  if (!skipToken(AsmToken::RBrac, "expected a closing square bracket"))
    return false;

  if (!isUInt<32>(RegLo)) {
    Error(FirstIdxLoc, "invalid register index");
    return false;
  }

  if (!isUInt<32>(RegHi)) {
    Error(SecondIdxLoc, "invalid register index");
    return false;
  }

  if (RegLo > RegHi) {
    Error(FirstIdxLoc, "first register index should not exceed second index");
    return false;
  }

  Num = static_cast<unsigned>(RegLo);
  RegWidth = 32 * ((RegHi - RegLo) + 1);
  return true;
}

unsigned AMDGPUAsmParser::ParseSpecialReg(RegisterKind &RegKind,
                                          unsigned &RegNum, unsigned &RegWidth,
                                          SmallVectorImpl<AsmToken> &Tokens) {
  assert(isToken(AsmToken::Identifier));
  unsigned Reg = getSpecialRegForName(getTokenStr());
  if (Reg) {
    RegNum = 0;
    RegWidth = 32;
    RegKind = IS_SPECIAL;
    Tokens.push_back(getToken());
    lex(); // skip register name
  }
  return Reg;
}

unsigned AMDGPUAsmParser::ParseRegularReg(RegisterKind &RegKind,
                                          unsigned &RegNum, unsigned &RegWidth,
                                          SmallVectorImpl<AsmToken> &Tokens) {
  assert(isToken(AsmToken::Identifier));
  StringRef RegName = getTokenStr();
  auto Loc = getLoc();

  const RegInfo *RI = getRegularRegInfo(RegName);
  if (!RI) {
    Error(Loc, "invalid register name");
    return AMDGPU::NoRegister;
  }

  Tokens.push_back(getToken());
  lex(); // skip register name

  RegKind = RI->Kind;
  StringRef RegSuffix = RegName.substr(RI->Name.size());
  unsigned SubReg = NoSubRegister;
  if (!RegSuffix.empty()) {
    // We don't know the opcode till we are done parsing, so we don't know if
    // registers should be 16 or 32 bit. It is therefore mandatory to put .l or
    // .h to correctly specify 16 bit registers. We also can't determine class
    // VGPR_16_Lo128 or VGPR_16, so always parse them as VGPR_16.
    if (RegSuffix.consume_back(".l"))
      SubReg = AMDGPU::lo16;
    else if (RegSuffix.consume_back(".h"))
      SubReg = AMDGPU::hi16;

    // Single 32-bit register: vXX.
    if (!getRegNum(RegSuffix, RegNum)) {
      Error(Loc, "invalid register index");
      return AMDGPU::NoRegister;
    }
    RegWidth = 32;
  } else {
    // Range of registers: v[XX:YY]. ":YY" is optional.
    if (!ParseRegRange(RegNum, RegWidth))
      return AMDGPU::NoRegister;
  }

  return getRegularReg(RegKind, RegNum, SubReg, RegWidth, Loc);
}

unsigned AMDGPUAsmParser::ParseRegList(RegisterKind &RegKind, unsigned &RegNum,
                                       unsigned &RegWidth,
                                       SmallVectorImpl<AsmToken> &Tokens) {
  unsigned Reg = AMDGPU::NoRegister;
  auto ListLoc = getLoc();

  if (!skipToken(AsmToken::LBrac,
                 "expected a register or a list of registers")) {
    return AMDGPU::NoRegister;
  }

  // List of consecutive registers, e.g.: [s0,s1,s2,s3]

  auto Loc = getLoc();
  if (!ParseAMDGPURegister(RegKind, Reg, RegNum, RegWidth))
    return AMDGPU::NoRegister;
  if (RegWidth != 32) {
    Error(Loc, "expected a single 32-bit register");
    return AMDGPU::NoRegister;
  }

  for (; trySkipToken(AsmToken::Comma); ) {
    RegisterKind NextRegKind;
    unsigned NextReg, NextRegNum, NextRegWidth;
    Loc = getLoc();

    if (!ParseAMDGPURegister(NextRegKind, NextReg,
                             NextRegNum, NextRegWidth,
                             Tokens)) {
      return AMDGPU::NoRegister;
    }
    if (NextRegWidth != 32) {
      Error(Loc, "expected a single 32-bit register");
      return AMDGPU::NoRegister;
    }
    if (NextRegKind != RegKind) {
      Error(Loc, "registers in a list must be of the same kind");
      return AMDGPU::NoRegister;
    }
    if (!AddNextRegisterToList(Reg, RegWidth, RegKind, NextReg, Loc))
      return AMDGPU::NoRegister;
  }

  if (!skipToken(AsmToken::RBrac,
                 "expected a comma or a closing square bracket")) {
    return AMDGPU::NoRegister;
  }

  if (isRegularReg(RegKind))
    Reg = getRegularReg(RegKind, RegNum, NoSubRegister, RegWidth, ListLoc);

  return Reg;
}

bool AMDGPUAsmParser::ParseAMDGPURegister(RegisterKind &RegKind, unsigned &Reg,
                                          unsigned &RegNum, unsigned &RegWidth,
                                          SmallVectorImpl<AsmToken> &Tokens) {
  auto Loc = getLoc();
  Reg = AMDGPU::NoRegister;

  if (isToken(AsmToken::Identifier)) {
    Reg = ParseSpecialReg(RegKind, RegNum, RegWidth, Tokens);
    if (Reg == AMDGPU::NoRegister)
      Reg = ParseRegularReg(RegKind, RegNum, RegWidth, Tokens);
  } else {
    Reg = ParseRegList(RegKind, RegNum, RegWidth, Tokens);
  }

  const MCRegisterInfo *TRI = getContext().getRegisterInfo();
  if (Reg == AMDGPU::NoRegister) {
    assert(Parser.hasPendingError());
    return false;
  }

  if (!subtargetHasRegister(*TRI, Reg)) {
    if (Reg == AMDGPU::SGPR_NULL) {
      Error(Loc, "'null' operand is not supported on this GPU");
    } else {
      Error(Loc, "register not available on this GPU");
    }
    return false;
  }

  return true;
}

bool AMDGPUAsmParser::ParseAMDGPURegister(RegisterKind &RegKind, unsigned &Reg,
                                          unsigned &RegNum, unsigned &RegWidth,
                                          bool RestoreOnFailure /*=false*/) {
  Reg = AMDGPU::NoRegister;

  SmallVector<AsmToken, 1> Tokens;
  if (ParseAMDGPURegister(RegKind, Reg, RegNum, RegWidth, Tokens)) {
    if (RestoreOnFailure) {
      while (!Tokens.empty()) {
        getLexer().UnLex(Tokens.pop_back_val());
      }
    }
    return true;
  }
  return false;
}

std::optional<StringRef>
AMDGPUAsmParser::getGprCountSymbolName(RegisterKind RegKind) {
  switch (RegKind) {
  case IS_VGPR:
    return StringRef(".amdgcn.next_free_vgpr");
  case IS_SGPR:
    return StringRef(".amdgcn.next_free_sgpr");
  default:
    return std::nullopt;
  }
}

void AMDGPUAsmParser::initializeGprCountSymbol(RegisterKind RegKind) {
  auto SymbolName = getGprCountSymbolName(RegKind);
  assert(SymbolName && "initializing invalid register kind");
  MCSymbol *Sym = getContext().getOrCreateSymbol(*SymbolName);
  Sym->setVariableValue(MCConstantExpr::create(0, getContext()));
}

bool AMDGPUAsmParser::updateGprCountSymbols(RegisterKind RegKind,
                                            unsigned DwordRegIndex,
                                            unsigned RegWidth) {
  // Symbols are only defined for GCN targets
  if (AMDGPU::getIsaVersion(getSTI().getCPU()).Major < 6)
    return true;

  auto SymbolName = getGprCountSymbolName(RegKind);
  if (!SymbolName)
    return true;
  MCSymbol *Sym = getContext().getOrCreateSymbol(*SymbolName);

  int64_t NewMax = DwordRegIndex + divideCeil(RegWidth, 32) - 1;
  int64_t OldCount;

  if (!Sym->isVariable())
    return !Error(getLoc(),
                  ".amdgcn.next_free_{v,s}gpr symbols must be variable");
  if (!Sym->getVariableValue(false)->evaluateAsAbsolute(OldCount))
    return !Error(
        getLoc(),
        ".amdgcn.next_free_{v,s}gpr symbols must be absolute expressions");

  if (OldCount <= NewMax)
    Sym->setVariableValue(MCConstantExpr::create(NewMax + 1, getContext()));

  return true;
}

std::unique_ptr<AMDGPUOperand>
AMDGPUAsmParser::parseRegister(bool RestoreOnFailure) {
  const auto &Tok = getToken();
  SMLoc StartLoc = Tok.getLoc();
  SMLoc EndLoc = Tok.getEndLoc();
  RegisterKind RegKind;
  unsigned Reg, RegNum, RegWidth;

  if (!ParseAMDGPURegister(RegKind, Reg, RegNum, RegWidth)) {
    return nullptr;
  }
  if (isHsaAbi(getSTI())) {
    if (!updateGprCountSymbols(RegKind, RegNum, RegWidth))
      return nullptr;
  } else
    KernelScope.usesRegister(RegKind, RegNum, RegWidth);
  return AMDGPUOperand::CreateReg(this, Reg, StartLoc, EndLoc);
}

ParseStatus AMDGPUAsmParser::parseImm(OperandVector &Operands,
                                      bool HasSP3AbsModifier, bool HasLit) {
  // TODO: add syntactic sugar for 1/(2*PI)

  if (isRegister())
    return ParseStatus::NoMatch;
  assert(!isModifier());

  if (!HasLit) {
    HasLit = trySkipId("lit");
    if (HasLit) {
      if (!skipToken(AsmToken::LParen, "expected left paren after lit"))
        return ParseStatus::Failure;
      ParseStatus S = parseImm(Operands, HasSP3AbsModifier, HasLit);
      if (S.isSuccess() &&
          !skipToken(AsmToken::RParen, "expected closing parentheses"))
        return ParseStatus::Failure;
      return S;
    }
  }

  const auto& Tok = getToken();
  const auto& NextTok = peekToken();
  bool IsReal = Tok.is(AsmToken::Real);
  SMLoc S = getLoc();
  bool Negate = false;

  if (!IsReal && Tok.is(AsmToken::Minus) && NextTok.is(AsmToken::Real)) {
    lex();
    IsReal = true;
    Negate = true;
  }

  AMDGPUOperand::Modifiers Mods;
  Mods.Lit = HasLit;

  if (IsReal) {
    // Floating-point expressions are not supported.
    // Can only allow floating-point literals with an
    // optional sign.

    StringRef Num = getTokenStr();
    lex();

    APFloat RealVal(APFloat::IEEEdouble());
    auto roundMode = APFloat::rmNearestTiesToEven;
    if (errorToBool(RealVal.convertFromString(Num, roundMode).takeError()))
      return ParseStatus::Failure;
    if (Negate)
      RealVal.changeSign();

    Operands.push_back(
      AMDGPUOperand::CreateImm(this, RealVal.bitcastToAPInt().getZExtValue(), S,
                               AMDGPUOperand::ImmTyNone, true));
    AMDGPUOperand &Op = static_cast<AMDGPUOperand &>(*Operands.back());
    Op.setModifiers(Mods);

    return ParseStatus::Success;

  } else {
    int64_t IntVal;
    const MCExpr *Expr;
    SMLoc S = getLoc();

    if (HasSP3AbsModifier) {
      // This is a workaround for handling expressions
      // as arguments of SP3 'abs' modifier, for example:
      //     |1.0|
      //     |-1|
      //     |1+x|
      // This syntax is not compatible with syntax of standard
      // MC expressions (due to the trailing '|').
      SMLoc EndLoc;
      if (getParser().parsePrimaryExpr(Expr, EndLoc, nullptr))
        return ParseStatus::Failure;
    } else {
      if (Parser.parseExpression(Expr))
        return ParseStatus::Failure;
    }

    if (Expr->evaluateAsAbsolute(IntVal)) {
      Operands.push_back(AMDGPUOperand::CreateImm(this, IntVal, S));
      AMDGPUOperand &Op = static_cast<AMDGPUOperand &>(*Operands.back());
      Op.setModifiers(Mods);
    } else {
      if (HasLit)
        return ParseStatus::NoMatch;
      Operands.push_back(AMDGPUOperand::CreateExpr(this, Expr, S));
    }

    return ParseStatus::Success;
  }

  return ParseStatus::NoMatch;
}

ParseStatus AMDGPUAsmParser::parseReg(OperandVector &Operands) {
  if (!isRegister())
    return ParseStatus::NoMatch;

  if (auto R = parseRegister()) {
    assert(R->isReg());
    Operands.push_back(std::move(R));
    return ParseStatus::Success;
  }
  return ParseStatus::Failure;
}

ParseStatus AMDGPUAsmParser::parseRegOrImm(OperandVector &Operands,
                                           bool HasSP3AbsMod, bool HasLit) {
  ParseStatus Res = parseReg(Operands);
  if (!Res.isNoMatch())
    return Res;
  if (isModifier())
    return ParseStatus::NoMatch;
  return parseImm(Operands, HasSP3AbsMod, HasLit);
}

bool
AMDGPUAsmParser::isNamedOperandModifier(const AsmToken &Token, const AsmToken &NextToken) const {
  if (Token.is(AsmToken::Identifier) && NextToken.is(AsmToken::LParen)) {
    const auto &str = Token.getString();
    return str == "abs" || str == "neg" || str == "sext";
  }
  return false;
}

bool
AMDGPUAsmParser::isOpcodeModifierWithVal(const AsmToken &Token, const AsmToken &NextToken) const {
  return Token.is(AsmToken::Identifier) && NextToken.is(AsmToken::Colon);
}

bool
AMDGPUAsmParser::isOperandModifier(const AsmToken &Token, const AsmToken &NextToken) const {
  return isNamedOperandModifier(Token, NextToken) || Token.is(AsmToken::Pipe);
}

bool
AMDGPUAsmParser::isRegOrOperandModifier(const AsmToken &Token, const AsmToken &NextToken) const {
  return isRegister(Token, NextToken) || isOperandModifier(Token, NextToken);
}

// Check if this is an operand modifier or an opcode modifier
// which may look like an expression but it is not. We should
// avoid parsing these modifiers as expressions. Currently
// recognized sequences are:
//   |...|
//   abs(...)
//   neg(...)
//   sext(...)
//   -reg
//   -|...|
//   -abs(...)
//   name:...
//
bool
AMDGPUAsmParser::isModifier() {

  AsmToken Tok = getToken();
  AsmToken NextToken[2];
  peekTokens(NextToken);

  return isOperandModifier(Tok, NextToken[0]) ||
         (Tok.is(AsmToken::Minus) && isRegOrOperandModifier(NextToken[0], NextToken[1])) ||
         isOpcodeModifierWithVal(Tok, NextToken[0]);
}

// Check if the current token is an SP3 'neg' modifier.
// Currently this modifier is allowed in the following context:
//
// 1. Before a register, e.g. "-v0", "-v[...]" or "-[v0,v1]".
// 2. Before an 'abs' modifier: -abs(...)
// 3. Before an SP3 'abs' modifier: -|...|
//
// In all other cases "-" is handled as a part
// of an expression that follows the sign.
//
// Note: When "-" is followed by an integer literal,
// this is interpreted as integer negation rather
// than a floating-point NEG modifier applied to N.
// Beside being contr-intuitive, such use of floating-point
// NEG modifier would have resulted in different meaning
// of integer literals used with VOP1/2/C and VOP3,
// for example:
//    v_exp_f32_e32 v5, -1 // VOP1: src0 = 0xFFFFFFFF
//    v_exp_f32_e64 v5, -1 // VOP3: src0 = 0x80000001
// Negative fp literals with preceding "-" are
// handled likewise for uniformity
//
bool
AMDGPUAsmParser::parseSP3NegModifier() {

  AsmToken NextToken[2];
  peekTokens(NextToken);

  if (isToken(AsmToken::Minus) &&
      (isRegister(NextToken[0], NextToken[1]) ||
       NextToken[0].is(AsmToken::Pipe) ||
       isId(NextToken[0], "abs"))) {
    lex();
    return true;
  }

  return false;
}

ParseStatus
AMDGPUAsmParser::parseRegOrImmWithFPInputMods(OperandVector &Operands,
                                              bool AllowImm) {
  bool Neg, SP3Neg;
  bool Abs, SP3Abs;
  bool Lit;
  SMLoc Loc;

  // Disable ambiguous constructs like '--1' etc. Should use neg(-1) instead.
  if (isToken(AsmToken::Minus) && peekToken().is(AsmToken::Minus))
    return Error(getLoc(), "invalid syntax, expected 'neg' modifier");

  SP3Neg = parseSP3NegModifier();

  Loc = getLoc();
  Neg = trySkipId("neg");
  if (Neg && SP3Neg)
    return Error(Loc, "expected register or immediate");
  if (Neg && !skipToken(AsmToken::LParen, "expected left paren after neg"))
    return ParseStatus::Failure;

  Abs = trySkipId("abs");
  if (Abs && !skipToken(AsmToken::LParen, "expected left paren after abs"))
    return ParseStatus::Failure;

  Lit = trySkipId("lit");
  if (Lit && !skipToken(AsmToken::LParen, "expected left paren after lit"))
    return ParseStatus::Failure;

  Loc = getLoc();
  SP3Abs = trySkipToken(AsmToken::Pipe);
  if (Abs && SP3Abs)
    return Error(Loc, "expected register or immediate");

  ParseStatus Res;
  if (AllowImm) {
    Res = parseRegOrImm(Operands, SP3Abs, Lit);
  } else {
    Res = parseReg(Operands);
  }
  if (!Res.isSuccess())
    return (SP3Neg || Neg || SP3Abs || Abs || Lit) ? ParseStatus::Failure : Res;

  if (Lit && !Operands.back()->isImm())
    Error(Loc, "expected immediate with lit modifier");

  if (SP3Abs && !skipToken(AsmToken::Pipe, "expected vertical bar"))
    return ParseStatus::Failure;
  if (Abs && !skipToken(AsmToken::RParen, "expected closing parentheses"))
    return ParseStatus::Failure;
  if (Neg && !skipToken(AsmToken::RParen, "expected closing parentheses"))
    return ParseStatus::Failure;
  if (Lit && !skipToken(AsmToken::RParen, "expected closing parentheses"))
    return ParseStatus::Failure;

  AMDGPUOperand::Modifiers Mods;
  Mods.Abs = Abs || SP3Abs;
  Mods.Neg = Neg || SP3Neg;
  Mods.Lit = Lit;

  if (Mods.hasFPModifiers() || Lit) {
    AMDGPUOperand &Op = static_cast<AMDGPUOperand &>(*Operands.back());
    if (Op.isExpr())
      return Error(Op.getStartLoc(), "expected an absolute expression");
    Op.setModifiers(Mods);
  }
  return ParseStatus::Success;
}

ParseStatus
AMDGPUAsmParser::parseRegOrImmWithIntInputMods(OperandVector &Operands,
                                               bool AllowImm) {
  bool Sext = trySkipId("sext");
  if (Sext && !skipToken(AsmToken::LParen, "expected left paren after sext"))
    return ParseStatus::Failure;

  ParseStatus Res;
  if (AllowImm) {
    Res = parseRegOrImm(Operands);
  } else {
    Res = parseReg(Operands);
  }
  if (!Res.isSuccess())
    return Sext ? ParseStatus::Failure : Res;

  if (Sext && !skipToken(AsmToken::RParen, "expected closing parentheses"))
    return ParseStatus::Failure;

  AMDGPUOperand::Modifiers Mods;
  Mods.Sext = Sext;

  if (Mods.hasIntModifiers()) {
    AMDGPUOperand &Op = static_cast<AMDGPUOperand &>(*Operands.back());
    if (Op.isExpr())
      return Error(Op.getStartLoc(), "expected an absolute expression");
    Op.setModifiers(Mods);
  }

  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseRegWithFPInputMods(OperandVector &Operands) {
  return parseRegOrImmWithFPInputMods(Operands, false);
}

ParseStatus AMDGPUAsmParser::parseRegWithIntInputMods(OperandVector &Operands) {
  return parseRegOrImmWithIntInputMods(Operands, false);
}

ParseStatus AMDGPUAsmParser::parseVReg32OrOff(OperandVector &Operands) {
  auto Loc = getLoc();
  if (trySkipId("off")) {
    Operands.push_back(AMDGPUOperand::CreateImm(this, 0, Loc,
                                                AMDGPUOperand::ImmTyOff, false));
    return ParseStatus::Success;
  }

  if (!isRegister())
    return ParseStatus::NoMatch;

  std::unique_ptr<AMDGPUOperand> Reg = parseRegister();
  if (Reg) {
    Operands.push_back(std::move(Reg));
    return ParseStatus::Success;
  }

  return ParseStatus::Failure;
}

unsigned AMDGPUAsmParser::checkTargetMatchPredicate(MCInst &Inst) {
  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;

  if ((getForcedEncodingSize() == 32 && (TSFlags & SIInstrFlags::VOP3)) ||
      (getForcedEncodingSize() == 64 && !(TSFlags & SIInstrFlags::VOP3)) ||
      (isForcedDPP() && !(TSFlags & SIInstrFlags::DPP)) ||
      (isForcedSDWA() && !(TSFlags & SIInstrFlags::SDWA)) )
    return Match_InvalidOperand;

  if (Inst.getOpcode() == AMDGPU::V_MAC_F32_sdwa_vi ||
      Inst.getOpcode() == AMDGPU::V_MAC_F16_sdwa_vi) {
    // v_mac_f32/16 allow only dst_sel == DWORD;
    auto OpNum =
        AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::dst_sel);
    const auto &Op = Inst.getOperand(OpNum);
    if (!Op.isImm() || Op.getImm() != AMDGPU::SDWA::SdwaSel::DWORD) {
      return Match_InvalidOperand;
    }
  }

  return Match_Success;
}

static ArrayRef<unsigned> getAllVariants() {
  static const unsigned Variants[] = {
    AMDGPUAsmVariants::DEFAULT, AMDGPUAsmVariants::VOP3,
    AMDGPUAsmVariants::SDWA, AMDGPUAsmVariants::SDWA9,
    AMDGPUAsmVariants::DPP, AMDGPUAsmVariants::VOP3_DPP
  };

  return ArrayRef(Variants);
}

// What asm variants we should check
ArrayRef<unsigned> AMDGPUAsmParser::getMatchedVariants() const {
  if (isForcedDPP() && isForcedVOP3()) {
    static const unsigned Variants[] = {AMDGPUAsmVariants::VOP3_DPP};
    return ArrayRef(Variants);
  }
  if (getForcedEncodingSize() == 32) {
    static const unsigned Variants[] = {AMDGPUAsmVariants::DEFAULT};
    return ArrayRef(Variants);
  }

  if (isForcedVOP3()) {
    static const unsigned Variants[] = {AMDGPUAsmVariants::VOP3};
    return ArrayRef(Variants);
  }

  if (isForcedSDWA()) {
    static const unsigned Variants[] = {AMDGPUAsmVariants::SDWA,
                                        AMDGPUAsmVariants::SDWA9};
    return ArrayRef(Variants);
  }

  if (isForcedDPP()) {
    static const unsigned Variants[] = {AMDGPUAsmVariants::DPP};
    return ArrayRef(Variants);
  }

  return getAllVariants();
}

StringRef AMDGPUAsmParser::getMatchedVariantName() const {
  if (isForcedDPP() && isForcedVOP3())
    return "e64_dpp";

  if (getForcedEncodingSize() == 32)
    return "e32";

  if (isForcedVOP3())
    return "e64";

  if (isForcedSDWA())
    return "sdwa";

  if (isForcedDPP())
    return "dpp";

  return "";
}

unsigned AMDGPUAsmParser::findImplicitSGPRReadInVOP(const MCInst &Inst) const {
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (MCPhysReg Reg : Desc.implicit_uses()) {
    switch (Reg) {
    case AMDGPU::FLAT_SCR:
    case AMDGPU::VCC:
    case AMDGPU::VCC_LO:
    case AMDGPU::VCC_HI:
    case AMDGPU::M0:
      return Reg;
    default:
      break;
    }
  }
  return AMDGPU::NoRegister;
}

// NB: This code is correct only when used to check constant
// bus limitations because GFX7 support no f16 inline constants.
// Note that there are no cases when a GFX7 opcode violates
// constant bus limitations due to the use of an f16 constant.
bool AMDGPUAsmParser::isInlineConstant(const MCInst &Inst,
                                       unsigned OpIdx) const {
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());

  if (!AMDGPU::isSISrcOperand(Desc, OpIdx) ||
      AMDGPU::isKImmOperand(Desc, OpIdx)) {
    return false;
  }

  const MCOperand &MO = Inst.getOperand(OpIdx);

  int64_t Val = MO.getImm();
  auto OpSize = AMDGPU::getOperandSize(Desc, OpIdx);

  switch (OpSize) { // expected operand size
  case 8:
    return AMDGPU::isInlinableLiteral64(Val, hasInv2PiInlineImm());
  case 4:
    return AMDGPU::isInlinableLiteral32(Val, hasInv2PiInlineImm());
  case 2: {
    const unsigned OperandType = Desc.operands()[OpIdx].OperandType;
    if (OperandType == AMDGPU::OPERAND_REG_IMM_INT16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_C_INT16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_AC_INT16)
      return AMDGPU::isInlinableLiteralI16(Val, hasInv2PiInlineImm());

    if (OperandType == AMDGPU::OPERAND_REG_INLINE_C_V2INT16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_AC_V2INT16 ||
        OperandType == AMDGPU::OPERAND_REG_IMM_V2INT16)
      return AMDGPU::isInlinableLiteralV2I16(Val);

    if (OperandType == AMDGPU::OPERAND_REG_INLINE_C_V2FP16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_AC_V2FP16 ||
        OperandType == AMDGPU::OPERAND_REG_IMM_V2FP16)
      return AMDGPU::isInlinableLiteralV2F16(Val);

    if (OperandType == AMDGPU::OPERAND_REG_INLINE_C_V2BF16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_AC_V2BF16 ||
        OperandType == AMDGPU::OPERAND_REG_IMM_V2BF16)
      return AMDGPU::isInlinableLiteralV2BF16(Val);

    if (OperandType == AMDGPU::OPERAND_REG_IMM_FP16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_C_FP16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_AC_FP16 ||
        OperandType == AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED)
      return AMDGPU::isInlinableLiteralFP16(Val, hasInv2PiInlineImm());

    if (OperandType == AMDGPU::OPERAND_REG_IMM_BF16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_C_BF16 ||
        OperandType == AMDGPU::OPERAND_REG_INLINE_AC_BF16 ||
        OperandType == AMDGPU::OPERAND_REG_IMM_BF16_DEFERRED)
      return AMDGPU::isInlinableLiteralBF16(Val, hasInv2PiInlineImm());

    llvm_unreachable("invalid operand type");
  }
  default:
    llvm_unreachable("invalid operand size");
  }
}

unsigned AMDGPUAsmParser::getConstantBusLimit(unsigned Opcode) const {
  if (!isGFX10Plus())
    return 1;

  switch (Opcode) {
  // 64-bit shift instructions can use only one scalar value input
  case AMDGPU::V_LSHLREV_B64_e64:
  case AMDGPU::V_LSHLREV_B64_gfx10:
  case AMDGPU::V_LSHLREV_B64_e64_gfx11:
  case AMDGPU::V_LSHLREV_B64_e32_gfx12:
  case AMDGPU::V_LSHLREV_B64_e64_gfx12:
  case AMDGPU::V_LSHRREV_B64_e64:
  case AMDGPU::V_LSHRREV_B64_gfx10:
  case AMDGPU::V_LSHRREV_B64_e64_gfx11:
  case AMDGPU::V_LSHRREV_B64_e64_gfx12:
  case AMDGPU::V_ASHRREV_I64_e64:
  case AMDGPU::V_ASHRREV_I64_gfx10:
  case AMDGPU::V_ASHRREV_I64_e64_gfx11:
  case AMDGPU::V_ASHRREV_I64_e64_gfx12:
  case AMDGPU::V_LSHL_B64_e64:
  case AMDGPU::V_LSHR_B64_e64:
  case AMDGPU::V_ASHR_I64_e64:
    return 1;
  default:
    return 2;
  }
}

constexpr unsigned MAX_SRC_OPERANDS_NUM = 6;
using OperandIndices = SmallVector<int16_t, MAX_SRC_OPERANDS_NUM>;

// Get regular operand indices in the same order as specified
// in the instruction (but append mandatory literals to the end).
static OperandIndices getSrcOperandIndices(unsigned Opcode,
                                           bool AddMandatoryLiterals = false) {

  int16_t ImmIdx =
      AddMandatoryLiterals ? getNamedOperandIdx(Opcode, OpName::imm) : -1;

  if (isVOPD(Opcode)) {
    int16_t ImmDeferredIdx =
        AddMandatoryLiterals ? getNamedOperandIdx(Opcode, OpName::immDeferred)
                             : -1;

    return {getNamedOperandIdx(Opcode, OpName::src0X),
            getNamedOperandIdx(Opcode, OpName::vsrc1X),
            getNamedOperandIdx(Opcode, OpName::src0Y),
            getNamedOperandIdx(Opcode, OpName::vsrc1Y),
            ImmDeferredIdx,
            ImmIdx};
  }

  return {getNamedOperandIdx(Opcode, OpName::src0),
          getNamedOperandIdx(Opcode, OpName::src1),
          getNamedOperandIdx(Opcode, OpName::src2), ImmIdx};
}

bool AMDGPUAsmParser::usesConstantBus(const MCInst &Inst, unsigned OpIdx) {
  const MCOperand &MO = Inst.getOperand(OpIdx);
  if (MO.isImm())
    return !isInlineConstant(Inst, OpIdx);
  if (MO.isReg()) {
    auto Reg = MO.getReg();
    if (!Reg)
      return false;
    const MCRegisterInfo *TRI = getContext().getRegisterInfo();
    auto PReg = mc2PseudoReg(Reg);
    return isSGPR(PReg, TRI) && PReg != SGPR_NULL;
  }
  return true;
}

// Based on the comment for `AMDGPUInstructionSelector::selectWritelane`:
// Writelane is special in that it can use SGPR and M0 (which would normally
// count as using the constant bus twice - but in this case it is allowed since
// the lane selector doesn't count as a use of the constant bus). However, it is
// still required to abide by the 1 SGPR rule.
static bool checkWriteLane(const MCInst &Inst) {
  const unsigned Opcode = Inst.getOpcode();
  if (Opcode != V_WRITELANE_B32_gfx6_gfx7 && Opcode != V_WRITELANE_B32_vi)
    return false;
  const MCOperand &LaneSelOp = Inst.getOperand(2);
  if (!LaneSelOp.isReg())
    return false;
  auto LaneSelReg = mc2PseudoReg(LaneSelOp.getReg());
  return LaneSelReg == M0 || LaneSelReg == M0_gfxpre11;
}

bool AMDGPUAsmParser::validateConstantBusLimitations(
    const MCInst &Inst, const OperandVector &Operands) {
  const unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opcode);
  unsigned LastSGPR = AMDGPU::NoRegister;
  unsigned ConstantBusUseCount = 0;
  unsigned NumLiterals = 0;
  unsigned LiteralSize;

  if (!(Desc.TSFlags &
        (SIInstrFlags::VOPC | SIInstrFlags::VOP1 | SIInstrFlags::VOP2 |
         SIInstrFlags::VOP3 | SIInstrFlags::VOP3P | SIInstrFlags::SDWA)) &&
      !isVOPD(Opcode))
    return true;

  if (checkWriteLane(Inst))
    return true;

  // Check special imm operands (used by madmk, etc)
  if (AMDGPU::hasNamedOperand(Opcode, AMDGPU::OpName::imm)) {
    ++NumLiterals;
    LiteralSize = 4;
  }

  SmallDenseSet<unsigned> SGPRsUsed;
  unsigned SGPRUsed = findImplicitSGPRReadInVOP(Inst);
  if (SGPRUsed != AMDGPU::NoRegister) {
    SGPRsUsed.insert(SGPRUsed);
    ++ConstantBusUseCount;
  }

  OperandIndices OpIndices = getSrcOperandIndices(Opcode);

  for (int OpIdx : OpIndices) {
    if (OpIdx == -1)
      continue;

    const MCOperand &MO = Inst.getOperand(OpIdx);
    if (usesConstantBus(Inst, OpIdx)) {
      if (MO.isReg()) {
        LastSGPR = mc2PseudoReg(MO.getReg());
        // Pairs of registers with a partial intersections like these
        //   s0, s[0:1]
        //   flat_scratch_lo, flat_scratch
        //   flat_scratch_lo, flat_scratch_hi
        // are theoretically valid but they are disabled anyway.
        // Note that this code mimics SIInstrInfo::verifyInstruction
        if (SGPRsUsed.insert(LastSGPR).second) {
          ++ConstantBusUseCount;
        }
      } else { // Expression or a literal

        if (Desc.operands()[OpIdx].OperandType == MCOI::OPERAND_IMMEDIATE)
          continue; // special operand like VINTERP attr_chan

        // An instruction may use only one literal.
        // This has been validated on the previous step.
        // See validateVOPLiteral.
        // This literal may be used as more than one operand.
        // If all these operands are of the same size,
        // this literal counts as one scalar value.
        // Otherwise it counts as 2 scalar values.
        // See "GFX10 Shader Programming", section 3.6.2.3.

        unsigned Size = AMDGPU::getOperandSize(Desc, OpIdx);
        if (Size < 4)
          Size = 4;

        if (NumLiterals == 0) {
          NumLiterals = 1;
          LiteralSize = Size;
        } else if (LiteralSize != Size) {
          NumLiterals = 2;
        }
      }
    }
  }
  ConstantBusUseCount += NumLiterals;

  if (ConstantBusUseCount <= getConstantBusLimit(Opcode))
    return true;

  SMLoc LitLoc = getLitLoc(Operands);
  SMLoc RegLoc = getRegLoc(LastSGPR, Operands);
  SMLoc Loc = (LitLoc.getPointer() < RegLoc.getPointer()) ? RegLoc : LitLoc;
  Error(Loc, "invalid operand (violates constant bus restrictions)");
  return false;
}

bool AMDGPUAsmParser::validateVOPDRegBankConstraints(
    const MCInst &Inst, const OperandVector &Operands) {

  const unsigned Opcode = Inst.getOpcode();
  if (!isVOPD(Opcode))
    return true;

  const MCRegisterInfo *TRI = getContext().getRegisterInfo();

  auto getVRegIdx = [&](unsigned, unsigned OperandIdx) {
    const MCOperand &Opr = Inst.getOperand(OperandIdx);
    return (Opr.isReg() && !isSGPR(mc2PseudoReg(Opr.getReg()), TRI))
               ? Opr.getReg()
               : MCRegister::NoRegister;
  };

  // On GFX12 if both OpX and OpY are V_MOV_B32 then OPY uses SRC2 source-cache.
  bool SkipSrc = Opcode == AMDGPU::V_DUAL_MOV_B32_e32_X_MOV_B32_e32_gfx12;

  const auto &InstInfo = getVOPDInstInfo(Opcode, &MII);
  auto InvalidCompOprIdx =
      InstInfo.getInvalidCompOperandIndex(getVRegIdx, SkipSrc);
  if (!InvalidCompOprIdx)
    return true;

  auto CompOprIdx = *InvalidCompOprIdx;
  auto ParsedIdx =
      std::max(InstInfo[VOPD::X].getIndexInParsedOperands(CompOprIdx),
               InstInfo[VOPD::Y].getIndexInParsedOperands(CompOprIdx));
  assert(ParsedIdx > 0 && ParsedIdx < Operands.size());

  auto Loc = ((AMDGPUOperand &)*Operands[ParsedIdx]).getStartLoc();
  if (CompOprIdx == VOPD::Component::DST) {
    Error(Loc, "one dst register must be even and the other odd");
  } else {
    auto CompSrcIdx = CompOprIdx - VOPD::Component::DST_NUM;
    Error(Loc, Twine("src") + Twine(CompSrcIdx) +
                   " operands must use different VGPR banks");
  }

  return false;
}

bool AMDGPUAsmParser::validateIntClampSupported(const MCInst &Inst) {

  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & SIInstrFlags::IntClamp) != 0 && !hasIntClamp()) {
    int ClampIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::clamp);
    assert(ClampIdx != -1);
    return Inst.getOperand(ClampIdx).getImm() == 0;
  }

  return true;
}

constexpr uint64_t MIMGFlags =
    SIInstrFlags::MIMG | SIInstrFlags::VIMAGE | SIInstrFlags::VSAMPLE;

bool AMDGPUAsmParser::validateMIMGDataSize(const MCInst &Inst,
                                           const SMLoc &IDLoc) {

  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & MIMGFlags) == 0)
    return true;

  int VDataIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdata);
  int DMaskIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dmask);
  int TFEIdx   = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::tfe);

  if (VDataIdx == -1 && isGFX10Plus()) // no return image_sample
    return true;

  if ((DMaskIdx == -1 || TFEIdx == -1) && isGFX10_AEncoding()) // intersect_ray
    return true;

  unsigned VDataSize = AMDGPU::getRegOperandSize(getMRI(), Desc, VDataIdx);
  unsigned TFESize = (TFEIdx != -1 && Inst.getOperand(TFEIdx).getImm()) ? 1 : 0;
  unsigned DMask = Inst.getOperand(DMaskIdx).getImm() & 0xf;
  if (DMask == 0)
    DMask = 1;

  bool IsPackedD16 = false;
  unsigned DataSize =
      (Desc.TSFlags & SIInstrFlags::Gather4) ? 4 : llvm::popcount(DMask);
  if (hasPackedD16()) {
    int D16Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::d16);
    IsPackedD16 = D16Idx >= 0;
    if (IsPackedD16 && Inst.getOperand(D16Idx).getImm())
      DataSize = (DataSize + 1) / 2;
  }

  if ((VDataSize / 4) == DataSize + TFESize)
    return true;

  StringRef Modifiers;
  if (isGFX90A())
    Modifiers = IsPackedD16 ? "dmask and d16" : "dmask";
  else
    Modifiers = IsPackedD16 ? "dmask, d16 and tfe" : "dmask and tfe";

  Error(IDLoc, Twine("image data size does not match ") + Modifiers);
  return false;
}

bool AMDGPUAsmParser::validateMIMGAddrSize(const MCInst &Inst,
                                           const SMLoc &IDLoc) {
  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & MIMGFlags) == 0 || !isGFX10Plus())
    return true;

  const AMDGPU::MIMGInfo *Info = AMDGPU::getMIMGInfo(Opc);

  const AMDGPU::MIMGBaseOpcodeInfo *BaseOpcode =
      AMDGPU::getMIMGBaseOpcodeInfo(Info->BaseOpcode);
  int VAddr0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vaddr0);
  int RSrcOpName = (Desc.TSFlags & SIInstrFlags::MIMG) ? AMDGPU::OpName::srsrc
                                                       : AMDGPU::OpName::rsrc;
  int SrsrcIdx = AMDGPU::getNamedOperandIdx(Opc, RSrcOpName);
  int DimIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dim);
  int A16Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::a16);

  assert(VAddr0Idx != -1);
  assert(SrsrcIdx != -1);
  assert(SrsrcIdx > VAddr0Idx);

  bool IsA16 = (A16Idx != -1 && Inst.getOperand(A16Idx).getImm());
  if (BaseOpcode->BVH) {
    if (IsA16 == BaseOpcode->A16)
      return true;
    Error(IDLoc, "image address size does not match a16");
    return false;
  }

  unsigned Dim = Inst.getOperand(DimIdx).getImm();
  const AMDGPU::MIMGDimInfo *DimInfo = AMDGPU::getMIMGDimInfoByEncoding(Dim);
  bool IsNSA = SrsrcIdx - VAddr0Idx > 1;
  unsigned ActualAddrSize =
      IsNSA ? SrsrcIdx - VAddr0Idx
            : AMDGPU::getRegOperandSize(getMRI(), Desc, VAddr0Idx) / 4;

  unsigned ExpectedAddrSize =
      AMDGPU::getAddrSizeMIMGOp(BaseOpcode, DimInfo, IsA16, hasG16());

  if (IsNSA) {
    if (hasPartialNSAEncoding() &&
        ExpectedAddrSize >
            getNSAMaxSize(Desc.TSFlags & SIInstrFlags::VSAMPLE)) {
      int VAddrLastIdx = SrsrcIdx - 1;
      unsigned VAddrLastSize =
          AMDGPU::getRegOperandSize(getMRI(), Desc, VAddrLastIdx) / 4;

      ActualAddrSize = VAddrLastIdx - VAddr0Idx + VAddrLastSize;
    }
  } else {
    if (ExpectedAddrSize > 12)
      ExpectedAddrSize = 16;

    // Allow oversized 8 VGPR vaddr when only 5/6/7 VGPRs are required.
    // This provides backward compatibility for assembly created
    // before 160b/192b/224b types were directly supported.
    if (ActualAddrSize == 8 && (ExpectedAddrSize >= 5 && ExpectedAddrSize <= 7))
      return true;
  }

  if (ActualAddrSize == ExpectedAddrSize)
    return true;

  Error(IDLoc, "image address size does not match dim and a16");
  return false;
}

bool AMDGPUAsmParser::validateMIMGAtomicDMask(const MCInst &Inst) {

  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & MIMGFlags) == 0)
    return true;
  if (!Desc.mayLoad() || !Desc.mayStore())
    return true; // Not atomic

  int DMaskIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dmask);
  unsigned DMask = Inst.getOperand(DMaskIdx).getImm() & 0xf;

  // This is an incomplete check because image_atomic_cmpswap
  // may only use 0x3 and 0xf while other atomic operations
  // may use 0x1 and 0x3. However these limitations are
  // verified when we check that dmask matches dst size.
  return DMask == 0x1 || DMask == 0x3 || DMask == 0xf;
}

bool AMDGPUAsmParser::validateMIMGGatherDMask(const MCInst &Inst) {

  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & SIInstrFlags::Gather4) == 0)
    return true;

  int DMaskIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dmask);
  unsigned DMask = Inst.getOperand(DMaskIdx).getImm() & 0xf;

  // GATHER4 instructions use dmask in a different fashion compared to
  // other MIMG instructions. The only useful DMASK values are
  // 1=red, 2=green, 4=blue, 8=alpha. (e.g. 1 returns
  // (red,red,red,red) etc.) The ISA document doesn't mention
  // this.
  return DMask == 0x1 || DMask == 0x2 || DMask == 0x4 || DMask == 0x8;
}

bool AMDGPUAsmParser::validateMIMGMSAA(const MCInst &Inst) {
  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & MIMGFlags) == 0)
    return true;

  const AMDGPU::MIMGInfo *Info = AMDGPU::getMIMGInfo(Opc);
  const AMDGPU::MIMGBaseOpcodeInfo *BaseOpcode =
      AMDGPU::getMIMGBaseOpcodeInfo(Info->BaseOpcode);

  if (!BaseOpcode->MSAA)
    return true;

  int DimIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dim);
  assert(DimIdx != -1);

  unsigned Dim = Inst.getOperand(DimIdx).getImm();
  const AMDGPU::MIMGDimInfo *DimInfo = AMDGPU::getMIMGDimInfoByEncoding(Dim);

  return DimInfo->MSAA;
}

static bool IsMovrelsSDWAOpcode(const unsigned Opcode)
{
  switch (Opcode) {
  case AMDGPU::V_MOVRELS_B32_sdwa_gfx10:
  case AMDGPU::V_MOVRELSD_B32_sdwa_gfx10:
  case AMDGPU::V_MOVRELSD_2_B32_sdwa_gfx10:
    return true;
  default:
    return false;
  }
}

// movrels* opcodes should only allow VGPRS as src0.
// This is specified in .td description for vop1/vop3,
// but sdwa is handled differently. See isSDWAOperand.
bool AMDGPUAsmParser::validateMovrels(const MCInst &Inst,
                                      const OperandVector &Operands) {

  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & SIInstrFlags::SDWA) == 0 || !IsMovrelsSDWAOpcode(Opc))
    return true;

  const int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
  assert(Src0Idx != -1);

  SMLoc ErrLoc;
  const MCOperand &Src0 = Inst.getOperand(Src0Idx);
  if (Src0.isReg()) {
    auto Reg = mc2PseudoReg(Src0.getReg());
    const MCRegisterInfo *TRI = getContext().getRegisterInfo();
    if (!isSGPR(Reg, TRI))
      return true;
    ErrLoc = getRegLoc(Reg, Operands);
  } else {
    ErrLoc = getConstLoc(Operands);
  }

  Error(ErrLoc, "source operand must be a VGPR");
  return false;
}

bool AMDGPUAsmParser::validateMAIAccWrite(const MCInst &Inst,
                                          const OperandVector &Operands) {

  const unsigned Opc = Inst.getOpcode();

  if (Opc != AMDGPU::V_ACCVGPR_WRITE_B32_vi)
    return true;

  const int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
  assert(Src0Idx != -1);

  const MCOperand &Src0 = Inst.getOperand(Src0Idx);
  if (!Src0.isReg())
    return true;

  auto Reg = mc2PseudoReg(Src0.getReg());
  const MCRegisterInfo *TRI = getContext().getRegisterInfo();
  if (!isGFX90A() && isSGPR(Reg, TRI)) {
    Error(getRegLoc(Reg, Operands),
          "source operand must be either a VGPR or an inline constant");
    return false;
  }

  return true;
}

bool AMDGPUAsmParser::validateMAISrc2(const MCInst &Inst,
                                      const OperandVector &Operands) {
  unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opcode);

  if (!(Desc.TSFlags & SIInstrFlags::IsMAI) ||
      !getFeatureBits()[FeatureMFMAInlineLiteralBug])
    return true;

  const int Src2Idx = getNamedOperandIdx(Opcode, OpName::src2);
  if (Src2Idx == -1)
    return true;

  if (Inst.getOperand(Src2Idx).isImm() && isInlineConstant(Inst, Src2Idx)) {
    Error(getConstLoc(Operands),
          "inline constants are not allowed for this operand");
    return false;
  }

  return true;
}

bool AMDGPUAsmParser::validateMFMA(const MCInst &Inst,
                                   const OperandVector &Operands) {
  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & SIInstrFlags::IsMAI) == 0)
    return true;

  const int Src2Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2);
  if (Src2Idx == -1)
    return true;

  const MCOperand &Src2 = Inst.getOperand(Src2Idx);
  if (!Src2.isReg())
    return true;

  MCRegister Src2Reg = Src2.getReg();
  MCRegister DstReg = Inst.getOperand(0).getReg();
  if (Src2Reg == DstReg)
    return true;

  const MCRegisterInfo *TRI = getContext().getRegisterInfo();
  if (TRI->getRegClass(Desc.operands()[0].RegClass).getSizeInBits() <= 128)
    return true;

  if (TRI->regsOverlap(Src2Reg, DstReg)) {
    Error(getRegLoc(mc2PseudoReg(Src2Reg), Operands),
          "source 2 operand must not partially overlap with dst");
    return false;
  }

  return true;
}

bool AMDGPUAsmParser::validateDivScale(const MCInst &Inst) {
  switch (Inst.getOpcode()) {
  default:
    return true;
  case V_DIV_SCALE_F32_gfx6_gfx7:
  case V_DIV_SCALE_F32_vi:
  case V_DIV_SCALE_F32_gfx10:
  case V_DIV_SCALE_F64_gfx6_gfx7:
  case V_DIV_SCALE_F64_vi:
  case V_DIV_SCALE_F64_gfx10:
    break;
  }

  // TODO: Check that src0 = src1 or src2.

  for (auto Name : {AMDGPU::OpName::src0_modifiers,
                    AMDGPU::OpName::src2_modifiers,
                    AMDGPU::OpName::src2_modifiers}) {
    if (Inst.getOperand(AMDGPU::getNamedOperandIdx(Inst.getOpcode(), Name))
            .getImm() &
        SISrcMods::ABS) {
      return false;
    }
  }

  return true;
}

bool AMDGPUAsmParser::validateMIMGD16(const MCInst &Inst) {

  const unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  if ((Desc.TSFlags & MIMGFlags) == 0)
    return true;

  int D16Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::d16);
  if (D16Idx >= 0 && Inst.getOperand(D16Idx).getImm()) {
    if (isCI() || isSI())
      return false;
  }

  return true;
}

static bool IsRevOpcode(const unsigned Opcode)
{
  switch (Opcode) {
  case AMDGPU::V_SUBREV_F32_e32:
  case AMDGPU::V_SUBREV_F32_e64:
  case AMDGPU::V_SUBREV_F32_e32_gfx10:
  case AMDGPU::V_SUBREV_F32_e32_gfx6_gfx7:
  case AMDGPU::V_SUBREV_F32_e32_vi:
  case AMDGPU::V_SUBREV_F32_e64_gfx10:
  case AMDGPU::V_SUBREV_F32_e64_gfx6_gfx7:
  case AMDGPU::V_SUBREV_F32_e64_vi:

  case AMDGPU::V_SUBREV_CO_U32_e32:
  case AMDGPU::V_SUBREV_CO_U32_e64:
  case AMDGPU::V_SUBREV_I32_e32_gfx6_gfx7:
  case AMDGPU::V_SUBREV_I32_e64_gfx6_gfx7:

  case AMDGPU::V_SUBBREV_U32_e32:
  case AMDGPU::V_SUBBREV_U32_e64:
  case AMDGPU::V_SUBBREV_U32_e32_gfx6_gfx7:
  case AMDGPU::V_SUBBREV_U32_e32_vi:
  case AMDGPU::V_SUBBREV_U32_e64_gfx6_gfx7:
  case AMDGPU::V_SUBBREV_U32_e64_vi:

  case AMDGPU::V_SUBREV_U32_e32:
  case AMDGPU::V_SUBREV_U32_e64:
  case AMDGPU::V_SUBREV_U32_e32_gfx9:
  case AMDGPU::V_SUBREV_U32_e32_vi:
  case AMDGPU::V_SUBREV_U32_e64_gfx9:
  case AMDGPU::V_SUBREV_U32_e64_vi:

  case AMDGPU::V_SUBREV_F16_e32:
  case AMDGPU::V_SUBREV_F16_e64:
  case AMDGPU::V_SUBREV_F16_e32_gfx10:
  case AMDGPU::V_SUBREV_F16_e32_vi:
  case AMDGPU::V_SUBREV_F16_e64_gfx10:
  case AMDGPU::V_SUBREV_F16_e64_vi:

  case AMDGPU::V_SUBREV_U16_e32:
  case AMDGPU::V_SUBREV_U16_e64:
  case AMDGPU::V_SUBREV_U16_e32_vi:
  case AMDGPU::V_SUBREV_U16_e64_vi:

  case AMDGPU::V_SUBREV_CO_U32_e32_gfx9:
  case AMDGPU::V_SUBREV_CO_U32_e64_gfx10:
  case AMDGPU::V_SUBREV_CO_U32_e64_gfx9:

  case AMDGPU::V_SUBBREV_CO_U32_e32_gfx9:
  case AMDGPU::V_SUBBREV_CO_U32_e64_gfx9:

  case AMDGPU::V_SUBREV_NC_U32_e32_gfx10:
  case AMDGPU::V_SUBREV_NC_U32_e64_gfx10:

  case AMDGPU::V_SUBREV_CO_CI_U32_e32_gfx10:
  case AMDGPU::V_SUBREV_CO_CI_U32_e64_gfx10:

  case AMDGPU::V_LSHRREV_B32_e32:
  case AMDGPU::V_LSHRREV_B32_e64:
  case AMDGPU::V_LSHRREV_B32_e32_gfx6_gfx7:
  case AMDGPU::V_LSHRREV_B32_e64_gfx6_gfx7:
  case AMDGPU::V_LSHRREV_B32_e32_vi:
  case AMDGPU::V_LSHRREV_B32_e64_vi:
  case AMDGPU::V_LSHRREV_B32_e32_gfx10:
  case AMDGPU::V_LSHRREV_B32_e64_gfx10:

  case AMDGPU::V_ASHRREV_I32_e32:
  case AMDGPU::V_ASHRREV_I32_e64:
  case AMDGPU::V_ASHRREV_I32_e32_gfx10:
  case AMDGPU::V_ASHRREV_I32_e32_gfx6_gfx7:
  case AMDGPU::V_ASHRREV_I32_e32_vi:
  case AMDGPU::V_ASHRREV_I32_e64_gfx10:
  case AMDGPU::V_ASHRREV_I32_e64_gfx6_gfx7:
  case AMDGPU::V_ASHRREV_I32_e64_vi:

  case AMDGPU::V_LSHLREV_B32_e32:
  case AMDGPU::V_LSHLREV_B32_e64:
  case AMDGPU::V_LSHLREV_B32_e32_gfx10:
  case AMDGPU::V_LSHLREV_B32_e32_gfx6_gfx7:
  case AMDGPU::V_LSHLREV_B32_e32_vi:
  case AMDGPU::V_LSHLREV_B32_e64_gfx10:
  case AMDGPU::V_LSHLREV_B32_e64_gfx6_gfx7:
  case AMDGPU::V_LSHLREV_B32_e64_vi:

  case AMDGPU::V_LSHLREV_B16_e32:
  case AMDGPU::V_LSHLREV_B16_e64:
  case AMDGPU::V_LSHLREV_B16_e32_vi:
  case AMDGPU::V_LSHLREV_B16_e64_vi:
  case AMDGPU::V_LSHLREV_B16_gfx10:

  case AMDGPU::V_LSHRREV_B16_e32:
  case AMDGPU::V_LSHRREV_B16_e64:
  case AMDGPU::V_LSHRREV_B16_e32_vi:
  case AMDGPU::V_LSHRREV_B16_e64_vi:
  case AMDGPU::V_LSHRREV_B16_gfx10:

  case AMDGPU::V_ASHRREV_I16_e32:
  case AMDGPU::V_ASHRREV_I16_e64:
  case AMDGPU::V_ASHRREV_I16_e32_vi:
  case AMDGPU::V_ASHRREV_I16_e64_vi:
  case AMDGPU::V_ASHRREV_I16_gfx10:

  case AMDGPU::V_LSHLREV_B64_e64:
  case AMDGPU::V_LSHLREV_B64_gfx10:
  case AMDGPU::V_LSHLREV_B64_vi:

  case AMDGPU::V_LSHRREV_B64_e64:
  case AMDGPU::V_LSHRREV_B64_gfx10:
  case AMDGPU::V_LSHRREV_B64_vi:

  case AMDGPU::V_ASHRREV_I64_e64:
  case AMDGPU::V_ASHRREV_I64_gfx10:
  case AMDGPU::V_ASHRREV_I64_vi:

  case AMDGPU::V_PK_LSHLREV_B16:
  case AMDGPU::V_PK_LSHLREV_B16_gfx10:
  case AMDGPU::V_PK_LSHLREV_B16_vi:

  case AMDGPU::V_PK_LSHRREV_B16:
  case AMDGPU::V_PK_LSHRREV_B16_gfx10:
  case AMDGPU::V_PK_LSHRREV_B16_vi:
  case AMDGPU::V_PK_ASHRREV_I16:
  case AMDGPU::V_PK_ASHRREV_I16_gfx10:
  case AMDGPU::V_PK_ASHRREV_I16_vi:
    return true;
  default:
    return false;
  }
}

std::optional<StringRef>
AMDGPUAsmParser::validateLdsDirect(const MCInst &Inst) {

  using namespace SIInstrFlags;
  const unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opcode);

  // lds_direct register is defined so that it can be used
  // with 9-bit operands only. Ignore encodings which do not accept these.
  const auto Enc = VOP1 | VOP2 | VOP3 | VOPC | VOP3P | SIInstrFlags::SDWA;
  if ((Desc.TSFlags & Enc) == 0)
    return std::nullopt;

  for (auto SrcName : {OpName::src0, OpName::src1, OpName::src2}) {
    auto SrcIdx = getNamedOperandIdx(Opcode, SrcName);
    if (SrcIdx == -1)
      break;
    const auto &Src = Inst.getOperand(SrcIdx);
    if (Src.isReg() && Src.getReg() == LDS_DIRECT) {

      if (isGFX90A() || isGFX11Plus())
        return StringRef("lds_direct is not supported on this GPU");

      if (IsRevOpcode(Opcode) || (Desc.TSFlags & SIInstrFlags::SDWA))
        return StringRef("lds_direct cannot be used with this instruction");

      if (SrcName != OpName::src0)
        return StringRef("lds_direct may be used as src0 only");
    }
  }

  return std::nullopt;
}

SMLoc AMDGPUAsmParser::getFlatOffsetLoc(const OperandVector &Operands) const {
  for (unsigned i = 1, e = Operands.size(); i != e; ++i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);
    if (Op.isFlatOffset())
      return Op.getStartLoc();
  }
  return getLoc();
}

bool AMDGPUAsmParser::validateOffset(const MCInst &Inst,
                                     const OperandVector &Operands) {
  auto Opcode = Inst.getOpcode();
  auto OpNum = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::offset);
  if (OpNum == -1)
    return true;

  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;
  if ((TSFlags & SIInstrFlags::FLAT))
    return validateFlatOffset(Inst, Operands);

  if ((TSFlags & SIInstrFlags::SMRD))
    return validateSMEMOffset(Inst, Operands);

  const auto &Op = Inst.getOperand(OpNum);
  if (isGFX12Plus() &&
      (TSFlags & (SIInstrFlags::MUBUF | SIInstrFlags::MTBUF))) {
    const unsigned OffsetSize = 24;
    if (!isIntN(OffsetSize, Op.getImm())) {
      Error(getFlatOffsetLoc(Operands),
            Twine("expected a ") + Twine(OffsetSize) + "-bit signed offset");
      return false;
    }
  } else {
    const unsigned OffsetSize = 16;
    if (!isUIntN(OffsetSize, Op.getImm())) {
      Error(getFlatOffsetLoc(Operands),
            Twine("expected a ") + Twine(OffsetSize) + "-bit unsigned offset");
      return false;
    }
  }
  return true;
}

bool AMDGPUAsmParser::validateFlatOffset(const MCInst &Inst,
                                         const OperandVector &Operands) {
  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;
  if ((TSFlags & SIInstrFlags::FLAT) == 0)
    return true;

  auto Opcode = Inst.getOpcode();
  auto OpNum = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::offset);
  assert(OpNum != -1);

  const auto &Op = Inst.getOperand(OpNum);
  if (!hasFlatOffsets() && Op.getImm() != 0) {
    Error(getFlatOffsetLoc(Operands),
          "flat offset modifier is not supported on this GPU");
    return false;
  }

  // For pre-GFX12 FLAT instructions the offset must be positive;
  // MSB is ignored and forced to zero.
  unsigned OffsetSize = AMDGPU::getNumFlatOffsetBits(getSTI());
  bool AllowNegative =
      (TSFlags & (SIInstrFlags::FlatGlobal | SIInstrFlags::FlatScratch)) ||
      isGFX12Plus();
  if (!isIntN(OffsetSize, Op.getImm()) || (!AllowNegative && Op.getImm() < 0)) {
    Error(getFlatOffsetLoc(Operands),
          Twine("expected a ") +
              (AllowNegative ? Twine(OffsetSize) + "-bit signed offset"
                             : Twine(OffsetSize - 1) + "-bit unsigned offset"));
    return false;
  }

  return true;
}

SMLoc AMDGPUAsmParser::getSMEMOffsetLoc(const OperandVector &Operands) const {
  // Start with second operand because SMEM Offset cannot be dst or src0.
  for (unsigned i = 2, e = Operands.size(); i != e; ++i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);
    if (Op.isSMEMOffset() || Op.isSMEMOffsetMod())
      return Op.getStartLoc();
  }
  return getLoc();
}

bool AMDGPUAsmParser::validateSMEMOffset(const MCInst &Inst,
                                         const OperandVector &Operands) {
  if (isCI() || isSI())
    return true;

  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;
  if ((TSFlags & SIInstrFlags::SMRD) == 0)
    return true;

  auto Opcode = Inst.getOpcode();
  auto OpNum = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::offset);
  if (OpNum == -1)
    return true;

  const auto &Op = Inst.getOperand(OpNum);
  if (!Op.isImm())
    return true;

  uint64_t Offset = Op.getImm();
  bool IsBuffer = AMDGPU::getSMEMIsBuffer(Opcode);
  if (AMDGPU::isLegalSMRDEncodedUnsignedOffset(getSTI(), Offset) ||
      AMDGPU::isLegalSMRDEncodedSignedOffset(getSTI(), Offset, IsBuffer))
    return true;

  Error(getSMEMOffsetLoc(Operands),
        isGFX12Plus()          ? "expected a 24-bit signed offset"
        : (isVI() || IsBuffer) ? "expected a 20-bit unsigned offset"
                               : "expected a 21-bit signed offset");

  return false;
}

bool AMDGPUAsmParser::validateSOPLiteral(const MCInst &Inst) const {
  unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opcode);
  if (!(Desc.TSFlags & (SIInstrFlags::SOP2 | SIInstrFlags::SOPC)))
    return true;

  const int Src0Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src0);
  const int Src1Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src1);

  const int OpIndices[] = { Src0Idx, Src1Idx };

  unsigned NumExprs = 0;
  unsigned NumLiterals = 0;
  uint32_t LiteralValue;

  for (int OpIdx : OpIndices) {
    if (OpIdx == -1) break;

    const MCOperand &MO = Inst.getOperand(OpIdx);
    // Exclude special imm operands (like that used by s_set_gpr_idx_on)
    if (AMDGPU::isSISrcOperand(Desc, OpIdx)) {
      if (MO.isImm() && !isInlineConstant(Inst, OpIdx)) {
        uint32_t Value = static_cast<uint32_t>(MO.getImm());
        if (NumLiterals == 0 || LiteralValue != Value) {
          LiteralValue = Value;
          ++NumLiterals;
        }
      } else if (MO.isExpr()) {
        ++NumExprs;
      }
    }
  }

  return NumLiterals + NumExprs <= 1;
}

bool AMDGPUAsmParser::validateOpSel(const MCInst &Inst) {
  const unsigned Opc = Inst.getOpcode();
  if (isPermlane16(Opc)) {
    int OpSelIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel);
    unsigned OpSel = Inst.getOperand(OpSelIdx).getImm();

    if (OpSel & ~3)
      return false;
  }

  uint64_t TSFlags = MII.get(Opc).TSFlags;

  if (isGFX940() && (TSFlags & SIInstrFlags::IsDOT)) {
    int OpSelIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel);
    if (OpSelIdx != -1) {
      if (Inst.getOperand(OpSelIdx).getImm() != 0)
        return false;
    }
    int OpSelHiIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel_hi);
    if (OpSelHiIdx != -1) {
      if (Inst.getOperand(OpSelHiIdx).getImm() != -1)
        return false;
    }
  }

  // op_sel[0:1] must be 0 for v_dot2_bf16_bf16 and v_dot2_f16_f16 (VOP3 Dot).
  if (isGFX11Plus() && (TSFlags & SIInstrFlags::IsDOT) &&
      (TSFlags & SIInstrFlags::VOP3) && !(TSFlags & SIInstrFlags::VOP3P)) {
    int OpSelIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel);
    unsigned OpSel = Inst.getOperand(OpSelIdx).getImm();
    if (OpSel & 3)
      return false;
  }

  return true;
}

bool AMDGPUAsmParser::validateNeg(const MCInst &Inst, int OpName) {
  assert(OpName == AMDGPU::OpName::neg_lo || OpName == AMDGPU::OpName::neg_hi);

  const unsigned Opc = Inst.getOpcode();
  uint64_t TSFlags = MII.get(Opc).TSFlags;

  // v_dot4 fp8/bf8 neg_lo/neg_hi not allowed on src0 and src1 (allowed on src2)
  // v_wmma iu4/iu8 neg_lo not allowed on src2 (allowed on src0, src1)
  // v_swmmac f16/bf16 neg_lo/neg_hi not allowed on src2 (allowed on src0, src1)
  // other wmma/swmmac instructions don't have neg_lo/neg_hi operand.
  if (!(TSFlags & SIInstrFlags::IsDOT) && !(TSFlags & SIInstrFlags::IsWMMA) &&
      !(TSFlags & SIInstrFlags::IsSWMMAC))
    return true;

  int NegIdx = AMDGPU::getNamedOperandIdx(Opc, OpName);
  if (NegIdx == -1)
    return true;

  unsigned Neg = Inst.getOperand(NegIdx).getImm();

  // Instructions that have neg_lo or neg_hi operand but neg modifier is allowed
  // on some src operands but not allowed on other.
  // It is convenient that such instructions don't have src_modifiers operand
  // for src operands that don't allow neg because they also don't allow opsel.

  int SrcMods[3] = {AMDGPU::OpName::src0_modifiers,
                    AMDGPU::OpName::src1_modifiers,
                    AMDGPU::OpName::src2_modifiers};

  for (unsigned i = 0; i < 3; ++i) {
    if (!AMDGPU::hasNamedOperand(Opc, SrcMods[i])) {
      if (Neg & (1 << i))
        return false;
    }
  }

  return true;
}

bool AMDGPUAsmParser::validateDPP(const MCInst &Inst,
                                  const OperandVector &Operands) {
  const unsigned Opc = Inst.getOpcode();
  int DppCtrlIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dpp_ctrl);
  if (DppCtrlIdx >= 0) {
    unsigned DppCtrl = Inst.getOperand(DppCtrlIdx).getImm();

    if (!AMDGPU::isLegalDPALU_DPPControl(DppCtrl) &&
        AMDGPU::isDPALU_DPP(MII.get(Opc))) {
      // DP ALU DPP is supported for row_newbcast only on GFX9*
      SMLoc S = getImmLoc(AMDGPUOperand::ImmTyDppCtrl, Operands);
      Error(S, "DP ALU dpp only supports row_newbcast");
      return false;
    }
  }

  int Dpp8Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::dpp8);
  bool IsDPP = DppCtrlIdx >= 0 || Dpp8Idx >= 0;

  if (IsDPP && !hasDPPSrc1SGPR(getSTI())) {
    int Src1Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1);
    if (Src1Idx >= 0) {
      const MCOperand &Src1 = Inst.getOperand(Src1Idx);
      const MCRegisterInfo *TRI = getContext().getRegisterInfo();
      if (Src1.isReg() && isSGPR(mc2PseudoReg(Src1.getReg()), TRI)) {
        auto Reg = mc2PseudoReg(Inst.getOperand(Src1Idx).getReg());
        SMLoc S = getRegLoc(Reg, Operands);
        Error(S, "invalid operand for instruction");
        return false;
      }
      if (Src1.isImm()) {
        Error(getInstLoc(Operands),
              "src1 immediate operand invalid for instruction");
        return false;
      }
    }
  }

  return true;
}

// Check if VCC register matches wavefront size
bool AMDGPUAsmParser::validateVccOperand(unsigned Reg) const {
  auto FB = getFeatureBits();
  return (FB[AMDGPU::FeatureWavefrontSize64] && Reg == AMDGPU::VCC) ||
    (FB[AMDGPU::FeatureWavefrontSize32] && Reg == AMDGPU::VCC_LO);
}

// One unique literal can be used. VOP3 literal is only allowed in GFX10+
bool AMDGPUAsmParser::validateVOPLiteral(const MCInst &Inst,
                                         const OperandVector &Operands) {
  unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opcode);
  bool HasMandatoryLiteral = getNamedOperandIdx(Opcode, OpName::imm) != -1;
  if (!(Desc.TSFlags & (SIInstrFlags::VOP3 | SIInstrFlags::VOP3P)) &&
      !HasMandatoryLiteral && !isVOPD(Opcode))
    return true;

  OperandIndices OpIndices = getSrcOperandIndices(Opcode, HasMandatoryLiteral);

  unsigned NumExprs = 0;
  unsigned NumLiterals = 0;
  uint32_t LiteralValue;

  for (int OpIdx : OpIndices) {
    if (OpIdx == -1)
      continue;

    const MCOperand &MO = Inst.getOperand(OpIdx);
    if (!MO.isImm() && !MO.isExpr())
      continue;
    if (!isSISrcOperand(Desc, OpIdx))
      continue;

    if (MO.isImm() && !isInlineConstant(Inst, OpIdx)) {
      uint64_t Value = static_cast<uint64_t>(MO.getImm());
      bool IsFP64 = AMDGPU::isSISrcFPOperand(Desc, OpIdx) &&
                    AMDGPU::getOperandSize(Desc.operands()[OpIdx]) == 8;
      bool IsValid32Op = AMDGPU::isValid32BitLiteral(Value, IsFP64);

      if (!IsValid32Op && !isInt<32>(Value) && !isUInt<32>(Value)) {
        Error(getLitLoc(Operands), "invalid operand for instruction");
        return false;
      }

      if (IsFP64 && IsValid32Op)
        Value = Hi_32(Value);

      if (NumLiterals == 0 || LiteralValue != Value) {
        LiteralValue = Value;
        ++NumLiterals;
      }
    } else if (MO.isExpr()) {
      ++NumExprs;
    }
  }
  NumLiterals += NumExprs;

  if (!NumLiterals)
    return true;

  if (!HasMandatoryLiteral && !getFeatureBits()[FeatureVOP3Literal]) {
    Error(getLitLoc(Operands), "literal operands are not supported");
    return false;
  }

  if (NumLiterals > 1) {
    Error(getLitLoc(Operands, true), "only one unique literal operand is allowed");
    return false;
  }

  return true;
}

// Returns -1 if not a register, 0 if VGPR and 1 if AGPR.
static int IsAGPROperand(const MCInst &Inst, uint16_t NameIdx,
                         const MCRegisterInfo *MRI) {
  int OpIdx = AMDGPU::getNamedOperandIdx(Inst.getOpcode(), NameIdx);
  if (OpIdx < 0)
    return -1;

  const MCOperand &Op = Inst.getOperand(OpIdx);
  if (!Op.isReg())
    return -1;

  unsigned Sub = MRI->getSubReg(Op.getReg(), AMDGPU::sub0);
  auto Reg = Sub ? Sub : Op.getReg();
  const MCRegisterClass &AGPR32 = MRI->getRegClass(AMDGPU::AGPR_32RegClassID);
  return AGPR32.contains(Reg) ? 1 : 0;
}

bool AMDGPUAsmParser::validateAGPRLdSt(const MCInst &Inst) const {
  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;
  if ((TSFlags & (SIInstrFlags::FLAT | SIInstrFlags::MUBUF |
                  SIInstrFlags::MTBUF | SIInstrFlags::MIMG |
                  SIInstrFlags::DS)) == 0)
    return true;

  uint16_t DataNameIdx = (TSFlags & SIInstrFlags::DS) ? AMDGPU::OpName::data0
                                                      : AMDGPU::OpName::vdata;

  const MCRegisterInfo *MRI = getMRI();
  int DstAreg = IsAGPROperand(Inst, AMDGPU::OpName::vdst, MRI);
  int DataAreg = IsAGPROperand(Inst, DataNameIdx, MRI);

  if ((TSFlags & SIInstrFlags::DS) && DataAreg >= 0) {
    int Data2Areg = IsAGPROperand(Inst, AMDGPU::OpName::data1, MRI);
    if (Data2Areg >= 0 && Data2Areg != DataAreg)
      return false;
  }

  auto FB = getFeatureBits();
  if (FB[AMDGPU::FeatureGFX90AInsts]) {
    if (DataAreg < 0 || DstAreg < 0)
      return true;
    return DstAreg == DataAreg;
  }

  return DstAreg < 1 && DataAreg < 1;
}

bool AMDGPUAsmParser::validateVGPRAlign(const MCInst &Inst) const {
  auto FB = getFeatureBits();
  if (!FB[AMDGPU::FeatureGFX90AInsts])
    return true;

  const MCRegisterInfo *MRI = getMRI();
  const MCRegisterClass &VGPR32 = MRI->getRegClass(AMDGPU::VGPR_32RegClassID);
  const MCRegisterClass &AGPR32 = MRI->getRegClass(AMDGPU::AGPR_32RegClassID);
  for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (!Op.isReg())
      continue;

    unsigned Sub = MRI->getSubReg(Op.getReg(), AMDGPU::sub0);
    if (!Sub)
      continue;

    if (VGPR32.contains(Sub) && ((Sub - AMDGPU::VGPR0) & 1))
      return false;
    if (AGPR32.contains(Sub) && ((Sub - AMDGPU::AGPR0) & 1))
      return false;
  }

  return true;
}

SMLoc AMDGPUAsmParser::getBLGPLoc(const OperandVector &Operands) const {
  for (unsigned i = 1, e = Operands.size(); i != e; ++i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);
    if (Op.isBLGP())
      return Op.getStartLoc();
  }
  return SMLoc();
}

bool AMDGPUAsmParser::validateBLGP(const MCInst &Inst,
                                   const OperandVector &Operands) {
  unsigned Opc = Inst.getOpcode();
  int BlgpIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::blgp);
  if (BlgpIdx == -1)
    return true;
  SMLoc BLGPLoc = getBLGPLoc(Operands);
  if (!BLGPLoc.isValid())
    return true;
  bool IsNeg = StringRef(BLGPLoc.getPointer()).starts_with("neg:");
  auto FB = getFeatureBits();
  bool UsesNeg = false;
  if (FB[AMDGPU::FeatureGFX940Insts]) {
    switch (Opc) {
    case AMDGPU::V_MFMA_F64_16X16X4F64_gfx940_acd:
    case AMDGPU::V_MFMA_F64_16X16X4F64_gfx940_vcd:
    case AMDGPU::V_MFMA_F64_4X4X4F64_gfx940_acd:
    case AMDGPU::V_MFMA_F64_4X4X4F64_gfx940_vcd:
      UsesNeg = true;
    }
  }

  if (IsNeg == UsesNeg)
    return true;

  Error(BLGPLoc,
        UsesNeg ? "invalid modifier: blgp is not supported"
                : "invalid modifier: neg is not supported");

  return false;
}

bool AMDGPUAsmParser::validateWaitCnt(const MCInst &Inst,
                                      const OperandVector &Operands) {
  if (!isGFX11Plus())
    return true;

  unsigned Opc = Inst.getOpcode();
  if (Opc != AMDGPU::S_WAITCNT_EXPCNT_gfx11 &&
      Opc != AMDGPU::S_WAITCNT_LGKMCNT_gfx11 &&
      Opc != AMDGPU::S_WAITCNT_VMCNT_gfx11 &&
      Opc != AMDGPU::S_WAITCNT_VSCNT_gfx11)
    return true;

  int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::sdst);
  assert(Src0Idx >= 0 && Inst.getOperand(Src0Idx).isReg());
  auto Reg = mc2PseudoReg(Inst.getOperand(Src0Idx).getReg());
  if (Reg == AMDGPU::SGPR_NULL)
    return true;

  SMLoc RegLoc = getRegLoc(Reg, Operands);
  Error(RegLoc, "src0 must be null");
  return false;
}

bool AMDGPUAsmParser::validateDS(const MCInst &Inst,
                                 const OperandVector &Operands) {
  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;
  if ((TSFlags & SIInstrFlags::DS) == 0)
    return true;
  if (TSFlags & SIInstrFlags::GWS)
    return validateGWS(Inst, Operands);
  // Only validate GDS for non-GWS instructions.
  if (hasGDS())
    return true;
  int GDSIdx =
      AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::gds);
  if (GDSIdx < 0)
    return true;
  unsigned GDS = Inst.getOperand(GDSIdx).getImm();
  if (GDS) {
    SMLoc S = getImmLoc(AMDGPUOperand::ImmTyGDS, Operands);
    Error(S, "gds modifier is not supported on this GPU");
    return false;
  }
  return true;
}

// gfx90a has an undocumented limitation:
// DS_GWS opcodes must use even aligned registers.
bool AMDGPUAsmParser::validateGWS(const MCInst &Inst,
                                  const OperandVector &Operands) {
  if (!getFeatureBits()[AMDGPU::FeatureGFX90AInsts])
    return true;

  int Opc = Inst.getOpcode();
  if (Opc != AMDGPU::DS_GWS_INIT_vi && Opc != AMDGPU::DS_GWS_BARRIER_vi &&
      Opc != AMDGPU::DS_GWS_SEMA_BR_vi)
    return true;

  const MCRegisterInfo *MRI = getMRI();
  const MCRegisterClass &VGPR32 = MRI->getRegClass(AMDGPU::VGPR_32RegClassID);
  int Data0Pos =
      AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::data0);
  assert(Data0Pos != -1);
  auto Reg = Inst.getOperand(Data0Pos).getReg();
  auto RegIdx = Reg - (VGPR32.contains(Reg) ? AMDGPU::VGPR0 : AMDGPU::AGPR0);
  if (RegIdx & 1) {
    SMLoc RegLoc = getRegLoc(Reg, Operands);
    Error(RegLoc, "vgpr must be even aligned");
    return false;
  }

  return true;
}

bool AMDGPUAsmParser::validateCoherencyBits(const MCInst &Inst,
                                            const OperandVector &Operands,
                                            const SMLoc &IDLoc) {
  int CPolPos = AMDGPU::getNamedOperandIdx(Inst.getOpcode(),
                                           AMDGPU::OpName::cpol);
  if (CPolPos == -1)
    return true;

  unsigned CPol = Inst.getOperand(CPolPos).getImm();

  if (isGFX12Plus())
    return validateTHAndScopeBits(Inst, Operands, CPol);

  uint64_t TSFlags = MII.get(Inst.getOpcode()).TSFlags;
  if (TSFlags & SIInstrFlags::SMRD) {
    if (CPol && (isSI() || isCI())) {
      SMLoc S = getImmLoc(AMDGPUOperand::ImmTyCPol, Operands);
      Error(S, "cache policy is not supported for SMRD instructions");
      return false;
    }
    if (CPol & ~(AMDGPU::CPol::GLC | AMDGPU::CPol::DLC)) {
      Error(IDLoc, "invalid cache policy for SMEM instruction");
      return false;
    }
  }

  if (isGFX90A() && !isGFX940() && (CPol & CPol::SCC)) {
    const uint64_t AllowSCCModifier = SIInstrFlags::MUBUF |
                                      SIInstrFlags::MTBUF | SIInstrFlags::MIMG |
                                      SIInstrFlags::FLAT;
    if (!(TSFlags & AllowSCCModifier)) {
      SMLoc S = getImmLoc(AMDGPUOperand::ImmTyCPol, Operands);
      StringRef CStr(S.getPointer());
      S = SMLoc::getFromPointer(&CStr.data()[CStr.find("scc")]);
      Error(S,
            "scc modifier is not supported for this instruction on this GPU");
      return false;
    }
  }

  if (!(TSFlags & (SIInstrFlags::IsAtomicNoRet | SIInstrFlags::IsAtomicRet)))
    return true;

  if (TSFlags & SIInstrFlags::IsAtomicRet) {
    if (!(TSFlags & SIInstrFlags::MIMG) && !(CPol & CPol::GLC)) {
      Error(IDLoc, isGFX940() ? "instruction must use sc0"
                              : "instruction must use glc");
      return false;
    }
  } else {
    if (CPol & CPol::GLC) {
      SMLoc S = getImmLoc(AMDGPUOperand::ImmTyCPol, Operands);
      StringRef CStr(S.getPointer());
      S = SMLoc::getFromPointer(
          &CStr.data()[CStr.find(isGFX940() ? "sc0" : "glc")]);
      Error(S, isGFX940() ? "instruction must not use sc0"
                          : "instruction must not use glc");
      return false;
    }
  }

  return true;
}

bool AMDGPUAsmParser::validateTHAndScopeBits(const MCInst &Inst,
                                             const OperandVector &Operands,
                                             const unsigned CPol) {
  const unsigned TH = CPol & AMDGPU::CPol::TH;
  const unsigned Scope = CPol & AMDGPU::CPol::SCOPE;

  const unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &TID = MII.get(Opcode);

  auto PrintError = [&](StringRef Msg) {
    SMLoc S = getImmLoc(AMDGPUOperand::ImmTyCPol, Operands);
    Error(S, Msg);
    return false;
  };

  if ((TID.TSFlags & SIInstrFlags::IsAtomicRet) &&
      (TID.TSFlags & (SIInstrFlags::FLAT | SIInstrFlags::MUBUF)) &&
      (!(TH & AMDGPU::CPol::TH_ATOMIC_RETURN)))
    return PrintError("instruction must use th:TH_ATOMIC_RETURN");

  if (TH == 0)
    return true;

  if ((TID.TSFlags & SIInstrFlags::SMRD) &&
      ((TH == AMDGPU::CPol::TH_NT_RT) || (TH == AMDGPU::CPol::TH_RT_NT) ||
       (TH == AMDGPU::CPol::TH_NT_HT)))
    return PrintError("invalid th value for SMEM instruction");

  if (TH == AMDGPU::CPol::TH_BYPASS) {
    if ((Scope != AMDGPU::CPol::SCOPE_SYS &&
         CPol & AMDGPU::CPol::TH_REAL_BYPASS) ||
        (Scope == AMDGPU::CPol::SCOPE_SYS &&
         !(CPol & AMDGPU::CPol::TH_REAL_BYPASS)))
      return PrintError("scope and th combination is not valid");
  }

  bool IsStore = TID.mayStore();
  bool IsAtomic =
      TID.TSFlags & (SIInstrFlags::IsAtomicNoRet | SIInstrFlags::IsAtomicRet);

  if (IsAtomic) {
    if (!(CPol & AMDGPU::CPol::TH_TYPE_ATOMIC))
      return PrintError("invalid th value for atomic instructions");
  } else if (IsStore) {
    if (!(CPol & AMDGPU::CPol::TH_TYPE_STORE))
      return PrintError("invalid th value for store instructions");
  } else {
    if (!(CPol & AMDGPU::CPol::TH_TYPE_LOAD))
      return PrintError("invalid th value for load instructions");
  }

  return true;
}

bool AMDGPUAsmParser::validateExeczVcczOperands(const OperandVector &Operands) {
  if (!isGFX11Plus())
    return true;
  for (auto &Operand : Operands) {
    if (!Operand->isReg())
      continue;
    unsigned Reg = Operand->getReg();
    if (Reg == SRC_EXECZ || Reg == SRC_VCCZ) {
      Error(getRegLoc(Reg, Operands),
            "execz and vccz are not supported on this GPU");
      return false;
    }
  }
  return true;
}

bool AMDGPUAsmParser::validateTFE(const MCInst &Inst,
                                  const OperandVector &Operands) {
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  if (Desc.mayStore() &&
      (Desc.TSFlags & (SIInstrFlags::MUBUF | SIInstrFlags::MTBUF))) {
    SMLoc Loc = getImmLoc(AMDGPUOperand::ImmTyTFE, Operands);
    if (Loc != getInstLoc(Operands)) {
      Error(Loc, "TFE modifier has no meaning for store instructions");
      return false;
    }
  }

  return true;
}

bool AMDGPUAsmParser::validateInstruction(const MCInst &Inst,
                                          const SMLoc &IDLoc,
                                          const OperandVector &Operands) {
  if (auto ErrMsg = validateLdsDirect(Inst)) {
    Error(getRegLoc(LDS_DIRECT, Operands), *ErrMsg);
    return false;
  }
  if (!validateSOPLiteral(Inst)) {
    Error(getLitLoc(Operands),
      "only one unique literal operand is allowed");
    return false;
  }
  if (!validateVOPLiteral(Inst, Operands)) {
    return false;
  }
  if (!validateConstantBusLimitations(Inst, Operands)) {
    return false;
  }
  if (!validateVOPDRegBankConstraints(Inst, Operands)) {
    return false;
  }
  if (!validateIntClampSupported(Inst)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyClamp, Operands),
          "integer clamping is not supported on this GPU");
    return false;
  }
  if (!validateOpSel(Inst)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyOpSel, Operands),
      "invalid op_sel operand");
    return false;
  }
  if (!validateNeg(Inst, AMDGPU::OpName::neg_lo)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyNegLo, Operands),
          "invalid neg_lo operand");
    return false;
  }
  if (!validateNeg(Inst, AMDGPU::OpName::neg_hi)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyNegHi, Operands),
          "invalid neg_hi operand");
    return false;
  }
  if (!validateDPP(Inst, Operands)) {
    return false;
  }
  // For MUBUF/MTBUF d16 is a part of opcode, so there is nothing to validate.
  if (!validateMIMGD16(Inst)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyD16, Operands),
      "d16 modifier is not supported on this GPU");
    return false;
  }
  if (!validateMIMGMSAA(Inst)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyDim, Operands),
          "invalid dim; must be MSAA type");
    return false;
  }
  if (!validateMIMGDataSize(Inst, IDLoc)) {
    return false;
  }
  if (!validateMIMGAddrSize(Inst, IDLoc))
    return false;
  if (!validateMIMGAtomicDMask(Inst)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyDMask, Operands),
      "invalid atomic image dmask");
    return false;
  }
  if (!validateMIMGGatherDMask(Inst)) {
    Error(getImmLoc(AMDGPUOperand::ImmTyDMask, Operands),
      "invalid image_gather dmask: only one bit must be set");
    return false;
  }
  if (!validateMovrels(Inst, Operands)) {
    return false;
  }
  if (!validateOffset(Inst, Operands)) {
    return false;
  }
  if (!validateMAIAccWrite(Inst, Operands)) {
    return false;
  }
  if (!validateMAISrc2(Inst, Operands)) {
    return false;
  }
  if (!validateMFMA(Inst, Operands)) {
    return false;
  }
  if (!validateCoherencyBits(Inst, Operands, IDLoc)) {
    return false;
  }

  if (!validateAGPRLdSt(Inst)) {
    Error(IDLoc, getFeatureBits()[AMDGPU::FeatureGFX90AInsts]
    ? "invalid register class: data and dst should be all VGPR or AGPR"
    : "invalid register class: agpr loads and stores not supported on this GPU"
    );
    return false;
  }
  if (!validateVGPRAlign(Inst)) {
    Error(IDLoc,
      "invalid register class: vgpr tuples must be 64 bit aligned");
    return false;
  }
  if (!validateDS(Inst, Operands)) {
    return false;
  }

  if (!validateBLGP(Inst, Operands)) {
    return false;
  }

  if (!validateDivScale(Inst)) {
    Error(IDLoc, "ABS not allowed in VOP3B instructions");
    return false;
  }
  if (!validateWaitCnt(Inst, Operands)) {
    return false;
  }
  if (!validateExeczVcczOperands(Operands)) {
    return false;
  }
  if (!validateTFE(Inst, Operands)) {
    return false;
  }

  return true;
}

static std::string AMDGPUMnemonicSpellCheck(StringRef S,
                                            const FeatureBitset &FBS,
                                            unsigned VariantID = 0);

static bool AMDGPUCheckMnemonic(StringRef Mnemonic,
                                const FeatureBitset &AvailableFeatures,
                                unsigned VariantID);

bool AMDGPUAsmParser::isSupportedMnemo(StringRef Mnemo,
                                       const FeatureBitset &FBS) {
  return isSupportedMnemo(Mnemo, FBS, getAllVariants());
}

bool AMDGPUAsmParser::isSupportedMnemo(StringRef Mnemo,
                                       const FeatureBitset &FBS,
                                       ArrayRef<unsigned> Variants) {
  for (auto Variant : Variants) {
    if (AMDGPUCheckMnemonic(Mnemo, FBS, Variant))
      return true;
  }

  return false;
}

bool AMDGPUAsmParser::checkUnsupportedInstruction(StringRef Mnemo,
                                                  const SMLoc &IDLoc) {
  FeatureBitset FBS = ComputeAvailableFeatures(getFeatureBits());

  // Check if requested instruction variant is supported.
  if (isSupportedMnemo(Mnemo, FBS, getMatchedVariants()))
    return false;

  // This instruction is not supported.
  // Clear any other pending errors because they are no longer relevant.
  getParser().clearPendingErrors();

  // Requested instruction variant is not supported.
  // Check if any other variants are supported.
  StringRef VariantName = getMatchedVariantName();
  if (!VariantName.empty() && isSupportedMnemo(Mnemo, FBS)) {
    return Error(IDLoc,
                 Twine(VariantName,
                       " variant of this instruction is not supported"));
  }

  // Check if this instruction may be used with a different wavesize.
  if (isGFX10Plus() && getFeatureBits()[AMDGPU::FeatureWavefrontSize64] &&
      !getFeatureBits()[AMDGPU::FeatureWavefrontSize32]) {

    FeatureBitset FeaturesWS32 = getFeatureBits();
    FeaturesWS32.flip(AMDGPU::FeatureWavefrontSize64)
        .flip(AMDGPU::FeatureWavefrontSize32);
    FeatureBitset AvailableFeaturesWS32 =
        ComputeAvailableFeatures(FeaturesWS32);

    if (isSupportedMnemo(Mnemo, AvailableFeaturesWS32, getMatchedVariants()))
      return Error(IDLoc, "instruction requires wavesize=32");
  }

  // Finally check if this instruction is supported on any other GPU.
  if (isSupportedMnemo(Mnemo, FeatureBitset().set())) {
    return Error(IDLoc, "instruction not supported on this GPU");
  }

  // Instruction not supported on any GPU. Probably a typo.
  std::string Suggestion = AMDGPUMnemonicSpellCheck(Mnemo, FBS);
  return Error(IDLoc, "invalid instruction" + Suggestion);
}

static bool isInvalidVOPDY(const OperandVector &Operands,
                           uint64_t InvalidOprIdx) {
  assert(InvalidOprIdx < Operands.size());
  const auto &Op = ((AMDGPUOperand &)*Operands[InvalidOprIdx]);
  if (Op.isToken() && InvalidOprIdx > 1) {
    const auto &PrevOp = ((AMDGPUOperand &)*Operands[InvalidOprIdx - 1]);
    return PrevOp.isToken() && PrevOp.getToken() == "::";
  }
  return false;
}

bool AMDGPUAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned Result = Match_Success;
  for (auto Variant : getMatchedVariants()) {
    uint64_t EI;
    auto R = MatchInstructionImpl(Operands, Inst, EI, MatchingInlineAsm,
                                  Variant);
    // We order match statuses from least to most specific. We use most specific
    // status as resulting
    // Match_MnemonicFail < Match_InvalidOperand < Match_MissingFeature
    if (R == Match_Success || R == Match_MissingFeature ||
        (R == Match_InvalidOperand && Result != Match_MissingFeature) ||
        (R == Match_MnemonicFail && Result != Match_InvalidOperand &&
         Result != Match_MissingFeature)) {
      Result = R;
      ErrorInfo = EI;
    }
    if (R == Match_Success)
      break;
  }

  if (Result == Match_Success) {
    if (!validateInstruction(Inst, IDLoc, Operands)) {
      return true;
    }
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  }

  StringRef Mnemo = ((AMDGPUOperand &)*Operands[0]).getToken();
  if (checkUnsupportedInstruction(Mnemo, IDLoc)) {
    return true;
  }

  switch (Result) {
  default: break;
  case Match_MissingFeature:
    // It has been verified that the specified instruction
    // mnemonic is valid. A match was found but it requires
    // features which are not supported on this GPU.
    return Error(IDLoc, "operands are not valid for this GPU or mode");

  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size()) {
        return Error(IDLoc, "too few operands for instruction");
      }
      ErrorLoc = ((AMDGPUOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;

      if (isInvalidVOPDY(Operands, ErrorInfo))
        return Error(ErrorLoc, "invalid VOPDY instruction");
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }

  case Match_MnemonicFail:
    llvm_unreachable("Invalid instructions should have been handled already");
  }
  llvm_unreachable("Implement any new match types added!");
}

bool AMDGPUAsmParser::ParseAsAbsoluteExpression(uint32_t &Ret) {
  int64_t Tmp = -1;
  if (!isToken(AsmToken::Integer) && !isToken(AsmToken::Identifier)) {
    return true;
  }
  if (getParser().parseAbsoluteExpression(Tmp)) {
    return true;
  }
  Ret = static_cast<uint32_t>(Tmp);
  return false;
}

bool AMDGPUAsmParser::ParseDirectiveAMDGCNTarget() {
  if (getSTI().getTargetTriple().getArch() != Triple::amdgcn)
    return TokError("directive only supported for amdgcn architecture");

  std::string TargetIDDirective;
  SMLoc TargetStart = getTok().getLoc();
  if (getParser().parseEscapedString(TargetIDDirective))
    return true;

  SMRange TargetRange = SMRange(TargetStart, getTok().getLoc());
  if (getTargetStreamer().getTargetID()->toString() != TargetIDDirective)
    return getParser().Error(TargetRange.Start,
        (Twine(".amdgcn_target directive's target id ") +
         Twine(TargetIDDirective) +
         Twine(" does not match the specified target id ") +
         Twine(getTargetStreamer().getTargetID()->toString())).str());

  return false;
}

bool AMDGPUAsmParser::OutOfRangeError(SMRange Range) {
  return Error(Range.Start, "value out of range", Range);
}

bool AMDGPUAsmParser::calculateGPRBlocks(
    const FeatureBitset &Features, const MCExpr *VCCUsed,
    const MCExpr *FlatScrUsed, bool XNACKUsed,
    std::optional<bool> EnableWavefrontSize32, const MCExpr *NextFreeVGPR,
    SMRange VGPRRange, const MCExpr *NextFreeSGPR, SMRange SGPRRange,
    const MCExpr *&VGPRBlocks, const MCExpr *&SGPRBlocks) {
  // TODO(scott.linder): These calculations are duplicated from
  // AMDGPUAsmPrinter::getSIProgramInfo and could be unified.
  IsaVersion Version = getIsaVersion(getSTI().getCPU());
  MCContext &Ctx = getContext();

  const MCExpr *NumSGPRs = NextFreeSGPR;
  int64_t EvaluatedSGPRs;

  if (Version.Major >= 10)
    NumSGPRs = MCConstantExpr::create(0, Ctx);
  else {
    unsigned MaxAddressableNumSGPRs =
        IsaInfo::getAddressableNumSGPRs(&getSTI());

    if (NumSGPRs->evaluateAsAbsolute(EvaluatedSGPRs) && Version.Major >= 8 &&
        !Features.test(FeatureSGPRInitBug) &&
        static_cast<uint64_t>(EvaluatedSGPRs) > MaxAddressableNumSGPRs)
      return OutOfRangeError(SGPRRange);

    const MCExpr *ExtraSGPRs =
        AMDGPUMCExpr::createExtraSGPRs(VCCUsed, FlatScrUsed, XNACKUsed, Ctx);
    NumSGPRs = MCBinaryExpr::createAdd(NumSGPRs, ExtraSGPRs, Ctx);

    if (NumSGPRs->evaluateAsAbsolute(EvaluatedSGPRs) &&
        (Version.Major <= 7 || Features.test(FeatureSGPRInitBug)) &&
        static_cast<uint64_t>(EvaluatedSGPRs) > MaxAddressableNumSGPRs)
      return OutOfRangeError(SGPRRange);

    if (Features.test(FeatureSGPRInitBug))
      NumSGPRs =
          MCConstantExpr::create(IsaInfo::FIXED_NUM_SGPRS_FOR_INIT_BUG, Ctx);
  }

  // The MCExpr equivalent of getNumSGPRBlocks/getNumVGPRBlocks:
  // (alignTo(max(1u, NumGPR), GPREncodingGranule) / GPREncodingGranule) - 1
  auto GetNumGPRBlocks = [&Ctx](const MCExpr *NumGPR,
                                unsigned Granule) -> const MCExpr * {
    const MCExpr *OneConst = MCConstantExpr::create(1ul, Ctx);
    const MCExpr *GranuleConst = MCConstantExpr::create(Granule, Ctx);
    const MCExpr *MaxNumGPR = AMDGPUMCExpr::createMax({NumGPR, OneConst}, Ctx);
    const MCExpr *AlignToGPR =
        AMDGPUMCExpr::createAlignTo(MaxNumGPR, GranuleConst, Ctx);
    const MCExpr *DivGPR =
        MCBinaryExpr::createDiv(AlignToGPR, GranuleConst, Ctx);
    const MCExpr *SubGPR = MCBinaryExpr::createSub(DivGPR, OneConst, Ctx);
    return SubGPR;
  };

  VGPRBlocks = GetNumGPRBlocks(
      NextFreeVGPR,
      IsaInfo::getVGPREncodingGranule(&getSTI(), EnableWavefrontSize32));
  SGPRBlocks =
      GetNumGPRBlocks(NumSGPRs, IsaInfo::getSGPREncodingGranule(&getSTI()));

  return false;
}

bool AMDGPUAsmParser::ParseDirectiveAMDHSAKernel() {
  if (getSTI().getTargetTriple().getArch() != Triple::amdgcn)
    return TokError("directive only supported for amdgcn architecture");

  if (!isHsaAbi(getSTI()))
    return TokError("directive only supported for amdhsa OS");

  StringRef KernelName;
  if (getParser().parseIdentifier(KernelName))
    return true;

  AMDGPU::MCKernelDescriptor KD =
      AMDGPU::MCKernelDescriptor::getDefaultAmdhsaKernelDescriptor(
          &getSTI(), getContext());

  StringSet<> Seen;

  IsaVersion IVersion = getIsaVersion(getSTI().getCPU());

  const MCExpr *ZeroExpr = MCConstantExpr::create(0, getContext());
  const MCExpr *OneExpr = MCConstantExpr::create(1, getContext());

  SMRange VGPRRange;
  const MCExpr *NextFreeVGPR = ZeroExpr;
  const MCExpr *AccumOffset = MCConstantExpr::create(0, getContext());
  uint64_t SharedVGPRCount = 0;
  uint64_t PreloadLength = 0;
  uint64_t PreloadOffset = 0;
  SMRange SGPRRange;
  const MCExpr *NextFreeSGPR = ZeroExpr;

  // Count the number of user SGPRs implied from the enabled feature bits.
  unsigned ImpliedUserSGPRCount = 0;

  // Track if the asm explicitly contains the directive for the user SGPR
  // count.
  std::optional<unsigned> ExplicitUserSGPRCount;
  const MCExpr *ReserveVCC = OneExpr;
  const MCExpr *ReserveFlatScr = OneExpr;
  std::optional<bool> EnableWavefrontSize32;

  while (true) {
    while (trySkipToken(AsmToken::EndOfStatement));

    StringRef ID;
    SMRange IDRange = getTok().getLocRange();
    if (!parseId(ID, "expected .amdhsa_ directive or .end_amdhsa_kernel"))
      return true;

    if (ID == ".end_amdhsa_kernel")
      break;

    if (!Seen.insert(ID).second)
      return TokError(".amdhsa_ directives cannot be repeated");

    SMLoc ValStart = getLoc();
    const MCExpr *ExprVal;
    if (getParser().parseExpression(ExprVal))
      return true;
    SMLoc ValEnd = getLoc();
    SMRange ValRange = SMRange(ValStart, ValEnd);

    int64_t IVal = 0;
    uint64_t Val = IVal;
    bool EvaluatableExpr;
    if ((EvaluatableExpr = ExprVal->evaluateAsAbsolute(IVal))) {
      if (IVal < 0)
        return OutOfRangeError(ValRange);
      Val = IVal;
    }

#define PARSE_BITS_ENTRY(FIELD, ENTRY, VALUE, RANGE)                           \
  if (!isUInt<ENTRY##_WIDTH>(Val))                                             \
    return OutOfRangeError(RANGE);                                             \
  AMDGPU::MCKernelDescriptor::bits_set(FIELD, VALUE, ENTRY##_SHIFT, ENTRY,     \
                                       getContext());

// Some fields use the parsed value immediately which requires the expression to
// be solvable.
#define EXPR_RESOLVE_OR_ERROR(RESOLVED)                                        \
  if (!(RESOLVED))                                                             \
    return Error(IDRange.Start, "directive should have resolvable expression", \
                 IDRange);

    if (ID == ".amdhsa_group_segment_fixed_size") {
      if (!isUInt<sizeof(kernel_descriptor_t::group_segment_fixed_size) *
                  CHAR_BIT>(Val))
        return OutOfRangeError(ValRange);
      KD.group_segment_fixed_size = ExprVal;
    } else if (ID == ".amdhsa_private_segment_fixed_size") {
      if (!isUInt<sizeof(kernel_descriptor_t::private_segment_fixed_size) *
                  CHAR_BIT>(Val))
        return OutOfRangeError(ValRange);
      KD.private_segment_fixed_size = ExprVal;
    } else if (ID == ".amdhsa_kernarg_size") {
      if (!isUInt<sizeof(kernel_descriptor_t::kernarg_size) * CHAR_BIT>(Val))
        return OutOfRangeError(ValRange);
      KD.kernarg_size = ExprVal;
    } else if (ID == ".amdhsa_user_sgpr_count") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      ExplicitUserSGPRCount = Val;
    } else if (ID == ".amdhsa_user_sgpr_private_segment_buffer") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      if (hasArchitectedFlatScratch())
        return Error(IDRange.Start,
                     "directive is not supported with architected flat scratch",
                     IDRange);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER,
                       ExprVal, ValRange);
      if (Val)
        ImpliedUserSGPRCount += 4;
    } else if (ID == ".amdhsa_user_sgpr_kernarg_preload_length") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      if (!hasKernargPreload())
        return Error(IDRange.Start, "directive requires gfx90a+", IDRange);

      if (Val > getMaxNumUserSGPRs())
        return OutOfRangeError(ValRange);
      PARSE_BITS_ENTRY(KD.kernarg_preload, KERNARG_PRELOAD_SPEC_LENGTH, ExprVal,
                       ValRange);
      if (Val) {
        ImpliedUserSGPRCount += Val;
        PreloadLength = Val;
      }
    } else if (ID == ".amdhsa_user_sgpr_kernarg_preload_offset") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      if (!hasKernargPreload())
        return Error(IDRange.Start, "directive requires gfx90a+", IDRange);

      if (Val >= 1024)
        return OutOfRangeError(ValRange);
      PARSE_BITS_ENTRY(KD.kernarg_preload, KERNARG_PRELOAD_SPEC_OFFSET, ExprVal,
                       ValRange);
      if (Val)
        PreloadOffset = Val;
    } else if (ID == ".amdhsa_user_sgpr_dispatch_ptr") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR, ExprVal,
                       ValRange);
      if (Val)
        ImpliedUserSGPRCount += 2;
    } else if (ID == ".amdhsa_user_sgpr_queue_ptr") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR, ExprVal,
                       ValRange);
      if (Val)
        ImpliedUserSGPRCount += 2;
    } else if (ID == ".amdhsa_user_sgpr_kernarg_segment_ptr") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR,
                       ExprVal, ValRange);
      if (Val)
        ImpliedUserSGPRCount += 2;
    } else if (ID == ".amdhsa_user_sgpr_dispatch_id") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID, ExprVal,
                       ValRange);
      if (Val)
        ImpliedUserSGPRCount += 2;
    } else if (ID == ".amdhsa_user_sgpr_flat_scratch_init") {
      if (hasArchitectedFlatScratch())
        return Error(IDRange.Start,
                     "directive is not supported with architected flat scratch",
                     IDRange);
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT,
                       ExprVal, ValRange);
      if (Val)
        ImpliedUserSGPRCount += 2;
    } else if (ID == ".amdhsa_user_sgpr_private_segment_size") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE,
                       ExprVal, ValRange);
      if (Val)
        ImpliedUserSGPRCount += 1;
    } else if (ID == ".amdhsa_wavefront_size32") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      if (IVersion.Major < 10)
        return Error(IDRange.Start, "directive requires gfx10+", IDRange);
      EnableWavefrontSize32 = Val;
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_ENABLE_WAVEFRONT_SIZE32, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_uses_dynamic_stack") {
      PARSE_BITS_ENTRY(KD.kernel_code_properties,
                       KERNEL_CODE_PROPERTY_USES_DYNAMIC_STACK, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_system_sgpr_private_segment_wavefront_offset") {
      if (hasArchitectedFlatScratch())
        return Error(IDRange.Start,
                     "directive is not supported with architected flat scratch",
                     IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_PRIVATE_SEGMENT, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_enable_private_segment") {
      if (!hasArchitectedFlatScratch())
        return Error(
            IDRange.Start,
            "directive is not supported without architected flat scratch",
            IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_PRIVATE_SEGMENT, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_system_sgpr_workgroup_id_x") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_SGPR_WORKGROUP_ID_X, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_system_sgpr_workgroup_id_y") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_SGPR_WORKGROUP_ID_Y, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_system_sgpr_workgroup_id_z") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_SGPR_WORKGROUP_ID_Z, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_system_sgpr_workgroup_info") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_SGPR_WORKGROUP_INFO, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_system_vgpr_workitem_id") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_VGPR_WORKITEM_ID, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_next_free_vgpr") {
      VGPRRange = ValRange;
      NextFreeVGPR = ExprVal;
    } else if (ID == ".amdhsa_next_free_sgpr") {
      SGPRRange = ValRange;
      NextFreeSGPR = ExprVal;
    } else if (ID == ".amdhsa_accum_offset") {
      if (!isGFX90A())
        return Error(IDRange.Start, "directive requires gfx90a+", IDRange);
      AccumOffset = ExprVal;
    } else if (ID == ".amdhsa_reserve_vcc") {
      if (EvaluatableExpr && !isUInt<1>(Val))
        return OutOfRangeError(ValRange);
      ReserveVCC = ExprVal;
    } else if (ID == ".amdhsa_reserve_flat_scratch") {
      if (IVersion.Major < 7)
        return Error(IDRange.Start, "directive requires gfx7+", IDRange);
      if (hasArchitectedFlatScratch())
        return Error(IDRange.Start,
                     "directive is not supported with architected flat scratch",
                     IDRange);
      if (EvaluatableExpr && !isUInt<1>(Val))
        return OutOfRangeError(ValRange);
      ReserveFlatScr = ExprVal;
    } else if (ID == ".amdhsa_reserve_xnack_mask") {
      if (IVersion.Major < 8)
        return Error(IDRange.Start, "directive requires gfx8+", IDRange);
      if (!isUInt<1>(Val))
        return OutOfRangeError(ValRange);
      if (Val != getTargetStreamer().getTargetID()->isXnackOnOrAny())
        return getParser().Error(IDRange.Start, ".amdhsa_reserve_xnack_mask does not match target id",
                                 IDRange);
    } else if (ID == ".amdhsa_float_round_mode_32") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_FLOAT_ROUND_MODE_32, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_float_round_mode_16_64") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_FLOAT_ROUND_MODE_16_64, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_float_denorm_mode_32") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_FLOAT_DENORM_MODE_32, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_float_denorm_mode_16_64") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_FLOAT_DENORM_MODE_16_64, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_dx10_clamp") {
      if (IVersion.Major >= 12)
        return Error(IDRange.Start, "directive unsupported on gfx12+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX6_GFX11_ENABLE_DX10_CLAMP, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_ieee_mode") {
      if (IVersion.Major >= 12)
        return Error(IDRange.Start, "directive unsupported on gfx12+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX6_GFX11_ENABLE_IEEE_MODE, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_fp16_overflow") {
      if (IVersion.Major < 9)
        return Error(IDRange.Start, "directive requires gfx9+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX9_PLUS_FP16_OVFL, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_tg_split") {
      if (!isGFX90A())
        return Error(IDRange.Start, "directive requires gfx90a+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc3, COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT,
                       ExprVal, ValRange);
    } else if (ID == ".amdhsa_workgroup_processor_mode") {
      if (IVersion.Major < 10)
        return Error(IDRange.Start, "directive requires gfx10+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX10_PLUS_WGP_MODE, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_memory_ordered") {
      if (IVersion.Major < 10)
        return Error(IDRange.Start, "directive requires gfx10+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX10_PLUS_MEM_ORDERED, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_forward_progress") {
      if (IVersion.Major < 10)
        return Error(IDRange.Start, "directive requires gfx10+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX10_PLUS_FWD_PROGRESS, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_shared_vgpr_count") {
      EXPR_RESOLVE_OR_ERROR(EvaluatableExpr);
      if (IVersion.Major < 10 || IVersion.Major >= 12)
        return Error(IDRange.Start, "directive requires gfx10 or gfx11",
                     IDRange);
      SharedVGPRCount = Val;
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc3,
                       COMPUTE_PGM_RSRC3_GFX10_GFX11_SHARED_VGPR_COUNT, ExprVal,
                       ValRange);
    } else if (ID == ".amdhsa_exception_fp_ieee_invalid_op") {
      PARSE_BITS_ENTRY(
          KD.compute_pgm_rsrc2,
          COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_IEEE_754_FP_INVALID_OPERATION,
          ExprVal, ValRange);
    } else if (ID == ".amdhsa_exception_fp_denorm_src") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_FP_DENORMAL_SOURCE,
                       ExprVal, ValRange);
    } else if (ID == ".amdhsa_exception_fp_ieee_div_zero") {
      PARSE_BITS_ENTRY(
          KD.compute_pgm_rsrc2,
          COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_IEEE_754_FP_DIVISION_BY_ZERO,
          ExprVal, ValRange);
    } else if (ID == ".amdhsa_exception_fp_ieee_overflow") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_IEEE_754_FP_OVERFLOW,
                       ExprVal, ValRange);
    } else if (ID == ".amdhsa_exception_fp_ieee_underflow") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_IEEE_754_FP_UNDERFLOW,
                       ExprVal, ValRange);
    } else if (ID == ".amdhsa_exception_fp_ieee_inexact") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_IEEE_754_FP_INEXACT,
                       ExprVal, ValRange);
    } else if (ID == ".amdhsa_exception_int_div_zero") {
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc2,
                       COMPUTE_PGM_RSRC2_ENABLE_EXCEPTION_INT_DIVIDE_BY_ZERO,
                       ExprVal, ValRange);
    } else if (ID == ".amdhsa_round_robin_scheduling") {
      if (IVersion.Major < 12)
        return Error(IDRange.Start, "directive requires gfx12+", IDRange);
      PARSE_BITS_ENTRY(KD.compute_pgm_rsrc1,
                       COMPUTE_PGM_RSRC1_GFX12_PLUS_ENABLE_WG_RR_EN, ExprVal,
                       ValRange);
    } else {
      return Error(IDRange.Start, "unknown .amdhsa_kernel directive", IDRange);
    }

#undef PARSE_BITS_ENTRY
  }

  if (!Seen.contains(".amdhsa_next_free_vgpr"))
    return TokError(".amdhsa_next_free_vgpr directive is required");

  if (!Seen.contains(".amdhsa_next_free_sgpr"))
    return TokError(".amdhsa_next_free_sgpr directive is required");

  const MCExpr *VGPRBlocks;
  const MCExpr *SGPRBlocks;
  if (calculateGPRBlocks(getFeatureBits(), ReserveVCC, ReserveFlatScr,
                         getTargetStreamer().getTargetID()->isXnackOnOrAny(),
                         EnableWavefrontSize32, NextFreeVGPR,
                         VGPRRange, NextFreeSGPR, SGPRRange, VGPRBlocks,
                         SGPRBlocks))
    return true;

  int64_t EvaluatedVGPRBlocks;
  bool VGPRBlocksEvaluatable =
      VGPRBlocks->evaluateAsAbsolute(EvaluatedVGPRBlocks);
  if (VGPRBlocksEvaluatable &&
      !isUInt<COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT_WIDTH>(
          static_cast<uint64_t>(EvaluatedVGPRBlocks))) {
    return OutOfRangeError(VGPRRange);
  }
  AMDGPU::MCKernelDescriptor::bits_set(
      KD.compute_pgm_rsrc1, VGPRBlocks,
      COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT_SHIFT,
      COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT, getContext());

  int64_t EvaluatedSGPRBlocks;
  if (SGPRBlocks->evaluateAsAbsolute(EvaluatedSGPRBlocks) &&
      !isUInt<COMPUTE_PGM_RSRC1_GRANULATED_WAVEFRONT_SGPR_COUNT_WIDTH>(
          static_cast<uint64_t>(EvaluatedSGPRBlocks)))
    return OutOfRangeError(SGPRRange);
  AMDGPU::MCKernelDescriptor::bits_set(
      KD.compute_pgm_rsrc1, SGPRBlocks,
      COMPUTE_PGM_RSRC1_GRANULATED_WAVEFRONT_SGPR_COUNT_SHIFT,
      COMPUTE_PGM_RSRC1_GRANULATED_WAVEFRONT_SGPR_COUNT, getContext());

  if (ExplicitUserSGPRCount && ImpliedUserSGPRCount > *ExplicitUserSGPRCount)
    return TokError("amdgpu_user_sgpr_count smaller than than implied by "
                    "enabled user SGPRs");

  unsigned UserSGPRCount =
      ExplicitUserSGPRCount ? *ExplicitUserSGPRCount : ImpliedUserSGPRCount;

  if (!isUInt<COMPUTE_PGM_RSRC2_USER_SGPR_COUNT_WIDTH>(UserSGPRCount))
    return TokError("too many user SGPRs enabled");
  AMDGPU::MCKernelDescriptor::bits_set(
      KD.compute_pgm_rsrc2, MCConstantExpr::create(UserSGPRCount, getContext()),
      COMPUTE_PGM_RSRC2_USER_SGPR_COUNT_SHIFT,
      COMPUTE_PGM_RSRC2_USER_SGPR_COUNT, getContext());

  int64_t IVal = 0;
  if (!KD.kernarg_size->evaluateAsAbsolute(IVal))
    return TokError("Kernarg size should be resolvable");
  uint64_t kernarg_size = IVal;
  if (PreloadLength && kernarg_size &&
      (PreloadLength * 4 + PreloadOffset * 4 > kernarg_size))
    return TokError("Kernarg preload length + offset is larger than the "
                    "kernarg segment size");

  if (isGFX90A()) {
    if (!Seen.contains(".amdhsa_accum_offset"))
      return TokError(".amdhsa_accum_offset directive is required");
    int64_t EvaluatedAccum;
    bool AccumEvaluatable = AccumOffset->evaluateAsAbsolute(EvaluatedAccum);
    uint64_t UEvaluatedAccum = EvaluatedAccum;
    if (AccumEvaluatable &&
        (UEvaluatedAccum < 4 || UEvaluatedAccum > 256 || (UEvaluatedAccum & 3)))
      return TokError("accum_offset should be in range [4..256] in "
                      "increments of 4");

    int64_t EvaluatedNumVGPR;
    if (NextFreeVGPR->evaluateAsAbsolute(EvaluatedNumVGPR) &&
        AccumEvaluatable &&
        UEvaluatedAccum >
            alignTo(std::max((uint64_t)1, (uint64_t)EvaluatedNumVGPR), 4))
      return TokError("accum_offset exceeds total VGPR allocation");
    const MCExpr *AdjustedAccum = MCBinaryExpr::createSub(
        MCBinaryExpr::createDiv(
            AccumOffset, MCConstantExpr::create(4, getContext()), getContext()),
        MCConstantExpr::create(1, getContext()), getContext());
    MCKernelDescriptor::bits_set(KD.compute_pgm_rsrc3, AdjustedAccum,
                                 COMPUTE_PGM_RSRC3_GFX90A_ACCUM_OFFSET_SHIFT,
                                 COMPUTE_PGM_RSRC3_GFX90A_ACCUM_OFFSET,
                                 getContext());
  }

  if (IVersion.Major >= 10 && IVersion.Major < 12) {
    // SharedVGPRCount < 16 checked by PARSE_ENTRY_BITS
    if (SharedVGPRCount && EnableWavefrontSize32 && *EnableWavefrontSize32) {
      return TokError("shared_vgpr_count directive not valid on "
                      "wavefront size 32");
    }

    if (VGPRBlocksEvaluatable &&
        (SharedVGPRCount * 2 + static_cast<uint64_t>(EvaluatedVGPRBlocks) >
         63)) {
      return TokError("shared_vgpr_count*2 + "
                      "compute_pgm_rsrc1.GRANULATED_WORKITEM_VGPR_COUNT cannot "
                      "exceed 63\n");
    }
  }

  getTargetStreamer().EmitAmdhsaKernelDescriptor(getSTI(), KernelName, KD,
                                                 NextFreeVGPR, NextFreeSGPR,
                                                 ReserveVCC, ReserveFlatScr);
  return false;
}

bool AMDGPUAsmParser::ParseDirectiveAMDHSACodeObjectVersion() {
  uint32_t Version;
  if (ParseAsAbsoluteExpression(Version))
    return true;

  getTargetStreamer().EmitDirectiveAMDHSACodeObjectVersion(Version);
  return false;
}

bool AMDGPUAsmParser::ParseAMDKernelCodeTValue(StringRef ID,
                                               AMDGPUMCKernelCodeT &C) {
  // max_scratch_backing_memory_byte_size is deprecated. Ignore it while parsing
  // assembly for backwards compatibility.
  if (ID == "max_scratch_backing_memory_byte_size") {
    Parser.eatToEndOfStatement();
    return false;
  }

  SmallString<40> ErrStr;
  raw_svector_ostream Err(ErrStr);
  if (!C.ParseKernelCodeT(ID, getParser(), Err)) {
    return TokError(Err.str());
  }
  Lex();

  if (ID == "enable_wavefront_size32") {
    if (C.code_properties & AMD_CODE_PROPERTY_ENABLE_WAVEFRONT_SIZE32) {
      if (!isGFX10Plus())
        return TokError("enable_wavefront_size32=1 is only allowed on GFX10+");
      if (!getFeatureBits()[AMDGPU::FeatureWavefrontSize32])
        return TokError("enable_wavefront_size32=1 requires +WavefrontSize32");
    } else {
      if (!getFeatureBits()[AMDGPU::FeatureWavefrontSize64])
        return TokError("enable_wavefront_size32=0 requires +WavefrontSize64");
    }
  }

  if (ID == "wavefront_size") {
    if (C.wavefront_size == 5) {
      if (!isGFX10Plus())
        return TokError("wavefront_size=5 is only allowed on GFX10+");
      if (!getFeatureBits()[AMDGPU::FeatureWavefrontSize32])
        return TokError("wavefront_size=5 requires +WavefrontSize32");
    } else if (C.wavefront_size == 6) {
      if (!getFeatureBits()[AMDGPU::FeatureWavefrontSize64])
        return TokError("wavefront_size=6 requires +WavefrontSize64");
    }
  }

  return false;
}

bool AMDGPUAsmParser::ParseDirectiveAMDKernelCodeT() {
  AMDGPUMCKernelCodeT KernelCode;
  KernelCode.initDefault(&getSTI(), getContext());

  while (true) {
    // Lex EndOfStatement.  This is in a while loop, because lexing a comment
    // will set the current token to EndOfStatement.
    while(trySkipToken(AsmToken::EndOfStatement));

    StringRef ID;
    if (!parseId(ID, "expected value identifier or .end_amd_kernel_code_t"))
      return true;

    if (ID == ".end_amd_kernel_code_t")
      break;

    if (ParseAMDKernelCodeTValue(ID, KernelCode))
      return true;
  }

  KernelCode.validate(&getSTI(), getContext());
  getTargetStreamer().EmitAMDKernelCodeT(KernelCode);

  return false;
}

bool AMDGPUAsmParser::ParseDirectiveAMDGPUHsaKernel() {
  StringRef KernelName;
  if (!parseId(KernelName, "expected symbol name"))
    return true;

  getTargetStreamer().EmitAMDGPUSymbolType(KernelName,
                                           ELF::STT_AMDGPU_HSA_KERNEL);

  KernelScope.initialize(getContext());
  return false;
}

bool AMDGPUAsmParser::ParseDirectiveISAVersion() {
  if (getSTI().getTargetTriple().getArch() != Triple::amdgcn) {
    return Error(getLoc(),
                 ".amd_amdgpu_isa directive is not available on non-amdgcn "
                 "architectures");
  }

  auto TargetIDDirective = getLexer().getTok().getStringContents();
  if (getTargetStreamer().getTargetID()->toString() != TargetIDDirective)
    return Error(getParser().getTok().getLoc(), "target id must match options");

  getTargetStreamer().EmitISAVersion();
  Lex();

  return false;
}

bool AMDGPUAsmParser::ParseDirectiveHSAMetadata() {
  assert(isHsaAbi(getSTI()));

  std::string HSAMetadataString;
  if (ParseToEndDirective(HSAMD::V3::AssemblerDirectiveBegin,
                          HSAMD::V3::AssemblerDirectiveEnd, HSAMetadataString))
    return true;

  if (!getTargetStreamer().EmitHSAMetadataV3(HSAMetadataString))
    return Error(getLoc(), "invalid HSA metadata");

  return false;
}

/// Common code to parse out a block of text (typically YAML) between start and
/// end directives.
bool AMDGPUAsmParser::ParseToEndDirective(const char *AssemblerDirectiveBegin,
                                          const char *AssemblerDirectiveEnd,
                                          std::string &CollectString) {

  raw_string_ostream CollectStream(CollectString);

  getLexer().setSkipSpace(false);

  bool FoundEnd = false;
  while (!isToken(AsmToken::Eof)) {
    while (isToken(AsmToken::Space)) {
      CollectStream << getTokenStr();
      Lex();
    }

    if (trySkipId(AssemblerDirectiveEnd)) {
      FoundEnd = true;
      break;
    }

    CollectStream << Parser.parseStringToEndOfStatement()
                  << getContext().getAsmInfo()->getSeparatorString();

    Parser.eatToEndOfStatement();
  }

  getLexer().setSkipSpace(true);

  if (isToken(AsmToken::Eof) && !FoundEnd) {
    return TokError(Twine("expected directive ") +
                    Twine(AssemblerDirectiveEnd) + Twine(" not found"));
  }

  CollectStream.flush();
  return false;
}

/// Parse the assembler directive for new MsgPack-format PAL metadata.
bool AMDGPUAsmParser::ParseDirectivePALMetadataBegin() {
  std::string String;
  if (ParseToEndDirective(AMDGPU::PALMD::AssemblerDirectiveBegin,
                          AMDGPU::PALMD::AssemblerDirectiveEnd, String))
    return true;

  auto PALMetadata = getTargetStreamer().getPALMetadata();
  if (!PALMetadata->setFromString(String))
    return Error(getLoc(), "invalid PAL metadata");
  return false;
}

/// Parse the assembler directive for old linear-format PAL metadata.
bool AMDGPUAsmParser::ParseDirectivePALMetadata() {
  if (getSTI().getTargetTriple().getOS() != Triple::AMDPAL) {
    return Error(getLoc(),
                 (Twine(PALMD::AssemblerDirective) + Twine(" directive is "
                 "not available on non-amdpal OSes")).str());
  }

  auto PALMetadata = getTargetStreamer().getPALMetadata();
  PALMetadata->setLegacy();
  for (;;) {
    uint32_t Key, Value;
    if (ParseAsAbsoluteExpression(Key)) {
      return TokError(Twine("invalid value in ") +
                      Twine(PALMD::AssemblerDirective));
    }
    if (!trySkipToken(AsmToken::Comma)) {
      return TokError(Twine("expected an even number of values in ") +
                      Twine(PALMD::AssemblerDirective));
    }
    if (ParseAsAbsoluteExpression(Value)) {
      return TokError(Twine("invalid value in ") +
                      Twine(PALMD::AssemblerDirective));
    }
    PALMetadata->setRegister(Key, Value);
    if (!trySkipToken(AsmToken::Comma))
      break;
  }
  return false;
}

/// ParseDirectiveAMDGPULDS
///  ::= .amdgpu_lds identifier ',' size_expression [',' align_expression]
bool AMDGPUAsmParser::ParseDirectiveAMDGPULDS() {
  if (getParser().checkForValidSection())
    return true;

  StringRef Name;
  SMLoc NameLoc = getLoc();
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in directive");

  MCSymbol *Symbol = getContext().getOrCreateSymbol(Name);
  if (getParser().parseComma())
    return true;

  unsigned LocalMemorySize = AMDGPU::IsaInfo::getLocalMemorySize(&getSTI());

  int64_t Size;
  SMLoc SizeLoc = getLoc();
  if (getParser().parseAbsoluteExpression(Size))
    return true;
  if (Size < 0)
    return Error(SizeLoc, "size must be non-negative");
  if (Size > LocalMemorySize)
    return Error(SizeLoc, "size is too large");

  int64_t Alignment = 4;
  if (trySkipToken(AsmToken::Comma)) {
    SMLoc AlignLoc = getLoc();
    if (getParser().parseAbsoluteExpression(Alignment))
      return true;
    if (Alignment < 0 || !isPowerOf2_64(Alignment))
      return Error(AlignLoc, "alignment must be a power of two");

    // Alignment larger than the size of LDS is possible in theory, as long
    // as the linker manages to place to symbol at address 0, but we do want
    // to make sure the alignment fits nicely into a 32-bit integer.
    if (Alignment >= 1u << 31)
      return Error(AlignLoc, "alignment is too large");
  }

  if (parseEOL())
    return true;

  Symbol->redefineIfPossible();
  if (!Symbol->isUndefined())
    return Error(NameLoc, "invalid symbol redefinition");

  getTargetStreamer().emitAMDGPULDS(Symbol, Size, Align(Alignment));
  return false;
}

bool AMDGPUAsmParser::ParseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getString();

  if (isHsaAbi(getSTI())) {
    if (IDVal == ".amdhsa_kernel")
     return ParseDirectiveAMDHSAKernel();

    if (IDVal == ".amdhsa_code_object_version")
      return ParseDirectiveAMDHSACodeObjectVersion();

    // TODO: Restructure/combine with PAL metadata directive.
    if (IDVal == AMDGPU::HSAMD::V3::AssemblerDirectiveBegin)
      return ParseDirectiveHSAMetadata();
  } else {
    if (IDVal == ".amd_kernel_code_t")
      return ParseDirectiveAMDKernelCodeT();

    if (IDVal == ".amdgpu_hsa_kernel")
      return ParseDirectiveAMDGPUHsaKernel();

    if (IDVal == ".amd_amdgpu_isa")
      return ParseDirectiveISAVersion();

    if (IDVal == AMDGPU::HSAMD::AssemblerDirectiveBegin) {
      return Error(getLoc(), (Twine(HSAMD::AssemblerDirectiveBegin) +
                              Twine(" directive is "
                                    "not available on non-amdhsa OSes"))
                                 .str());
    }
  }

  if (IDVal == ".amdgcn_target")
    return ParseDirectiveAMDGCNTarget();

  if (IDVal == ".amdgpu_lds")
    return ParseDirectiveAMDGPULDS();

  if (IDVal == PALMD::AssemblerDirectiveBegin)
    return ParseDirectivePALMetadataBegin();

  if (IDVal == PALMD::AssemblerDirective)
    return ParseDirectivePALMetadata();

  return true;
}

bool AMDGPUAsmParser::subtargetHasRegister(const MCRegisterInfo &MRI,
                                           unsigned RegNo) {

  if (MRI.regsOverlap(AMDGPU::TTMP12_TTMP13_TTMP14_TTMP15, RegNo))
    return isGFX9Plus();

  // GFX10+ has 2 more SGPRs 104 and 105.
  if (MRI.regsOverlap(AMDGPU::SGPR104_SGPR105, RegNo))
    return hasSGPR104_SGPR105();

  switch (RegNo) {
  case AMDGPU::SRC_SHARED_BASE_LO:
  case AMDGPU::SRC_SHARED_BASE:
  case AMDGPU::SRC_SHARED_LIMIT_LO:
  case AMDGPU::SRC_SHARED_LIMIT:
  case AMDGPU::SRC_PRIVATE_BASE_LO:
  case AMDGPU::SRC_PRIVATE_BASE:
  case AMDGPU::SRC_PRIVATE_LIMIT_LO:
  case AMDGPU::SRC_PRIVATE_LIMIT:
    return isGFX9Plus();
  case AMDGPU::SRC_POPS_EXITING_WAVE_ID:
    return isGFX9Plus() && !isGFX11Plus();
  case AMDGPU::TBA:
  case AMDGPU::TBA_LO:
  case AMDGPU::TBA_HI:
  case AMDGPU::TMA:
  case AMDGPU::TMA_LO:
  case AMDGPU::TMA_HI:
    return !isGFX9Plus();
  case AMDGPU::XNACK_MASK:
  case AMDGPU::XNACK_MASK_LO:
  case AMDGPU::XNACK_MASK_HI:
    return (isVI() || isGFX9()) && getTargetStreamer().getTargetID()->isXnackSupported();
  case AMDGPU::SGPR_NULL:
    return isGFX10Plus();
  default:
    break;
  }

  if (isCI())
    return true;

  if (isSI() || isGFX10Plus()) {
    // No flat_scr on SI.
    // On GFX10Plus flat scratch is not a valid register operand and can only be
    // accessed with s_setreg/s_getreg.
    switch (RegNo) {
    case AMDGPU::FLAT_SCR:
    case AMDGPU::FLAT_SCR_LO:
    case AMDGPU::FLAT_SCR_HI:
      return false;
    default:
      return true;
    }
  }

  // VI only has 102 SGPRs, so make sure we aren't trying to use the 2 more that
  // SI/CI have.
  if (MRI.regsOverlap(AMDGPU::SGPR102_SGPR103, RegNo))
    return hasSGPR102_SGPR103();

  return true;
}

ParseStatus AMDGPUAsmParser::parseOperand(OperandVector &Operands,
                                          StringRef Mnemonic,
                                          OperandMode Mode) {
  ParseStatus Res = parseVOPD(Operands);
  if (Res.isSuccess() || Res.isFailure() || isToken(AsmToken::EndOfStatement))
    return Res;

  // Try to parse with a custom parser
  Res = MatchOperandParserImpl(Operands, Mnemonic);

  // If we successfully parsed the operand or if there as an error parsing,
  // we are done.
  //
  // If we are parsing after we reach EndOfStatement then this means we
  // are appending default values to the Operands list.  This is only done
  // by custom parser, so we shouldn't continue on to the generic parsing.
  if (Res.isSuccess() || Res.isFailure() || isToken(AsmToken::EndOfStatement))
    return Res;

  SMLoc RBraceLoc;
  SMLoc LBraceLoc = getLoc();
  if (Mode == OperandMode_NSA && trySkipToken(AsmToken::LBrac)) {
    unsigned Prefix = Operands.size();

    for (;;) {
      auto Loc = getLoc();
      Res = parseReg(Operands);
      if (Res.isNoMatch())
        Error(Loc, "expected a register");
      if (!Res.isSuccess())
        return ParseStatus::Failure;

      RBraceLoc = getLoc();
      if (trySkipToken(AsmToken::RBrac))
        break;

      if (!skipToken(AsmToken::Comma,
                     "expected a comma or a closing square bracket"))
        return ParseStatus::Failure;
    }

    if (Operands.size() - Prefix > 1) {
      Operands.insert(Operands.begin() + Prefix,
                      AMDGPUOperand::CreateToken(this, "[", LBraceLoc));
      Operands.push_back(AMDGPUOperand::CreateToken(this, "]", RBraceLoc));
    }

    return ParseStatus::Success;
  }

  return parseRegOrImm(Operands);
}

StringRef AMDGPUAsmParser::parseMnemonicSuffix(StringRef Name) {
  // Clear any forced encodings from the previous instruction.
  setForcedEncodingSize(0);
  setForcedDPP(false);
  setForcedSDWA(false);

  if (Name.ends_with("_e64_dpp")) {
    setForcedDPP(true);
    setForcedEncodingSize(64);
    return Name.substr(0, Name.size() - 8);
  }
  if (Name.ends_with("_e64")) {
    setForcedEncodingSize(64);
    return Name.substr(0, Name.size() - 4);
  }
  if (Name.ends_with("_e32")) {
    setForcedEncodingSize(32);
    return Name.substr(0, Name.size() - 4);
  }
  if (Name.ends_with("_dpp")) {
    setForcedDPP(true);
    return Name.substr(0, Name.size() - 4);
  }
  if (Name.ends_with("_sdwa")) {
    setForcedSDWA(true);
    return Name.substr(0, Name.size() - 5);
  }
  return Name;
}

static void applyMnemonicAliases(StringRef &Mnemonic,
                                 const FeatureBitset &Features,
                                 unsigned VariantID);

bool AMDGPUAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name,
                                       SMLoc NameLoc, OperandVector &Operands) {
  // Add the instruction mnemonic
  Name = parseMnemonicSuffix(Name);

  // If the target architecture uses MnemonicAlias, call it here to parse
  // operands correctly.
  applyMnemonicAliases(Name, getAvailableFeatures(), 0);

  Operands.push_back(AMDGPUOperand::CreateToken(this, Name, NameLoc));

  bool IsMIMG = Name.starts_with("image_");

  while (!trySkipToken(AsmToken::EndOfStatement)) {
    OperandMode Mode = OperandMode_Default;
    if (IsMIMG && isGFX10Plus() && Operands.size() == 2)
      Mode = OperandMode_NSA;
    ParseStatus Res = parseOperand(Operands, Name, Mode);

    if (!Res.isSuccess()) {
      checkUnsupportedInstruction(Name, NameLoc);
      if (!Parser.hasPendingError()) {
        // FIXME: use real operand location rather than the current location.
        StringRef Msg = Res.isFailure() ? "failed parsing operand."
                                        : "not a valid operand.";
        Error(getLoc(), Msg);
      }
      while (!trySkipToken(AsmToken::EndOfStatement)) {
        lex();
      }
      return true;
    }

    // Eat the comma or space if there is one.
    trySkipToken(AsmToken::Comma);
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Utility functions
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseTokenOp(StringRef Name,
                                          OperandVector &Operands) {
  SMLoc S = getLoc();
  if (!trySkipId(Name))
    return ParseStatus::NoMatch;

  Operands.push_back(AMDGPUOperand::CreateToken(this, Name, S));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseIntWithPrefix(const char *Prefix,
                                                int64_t &IntVal) {

  if (!trySkipId(Prefix, AsmToken::Colon))
    return ParseStatus::NoMatch;

  return parseExpr(IntVal) ? ParseStatus::Success : ParseStatus::Failure;
}

ParseStatus AMDGPUAsmParser::parseIntWithPrefix(
    const char *Prefix, OperandVector &Operands, AMDGPUOperand::ImmTy ImmTy,
    std::function<bool(int64_t &)> ConvertResult) {
  SMLoc S = getLoc();
  int64_t Value = 0;

  ParseStatus Res = parseIntWithPrefix(Prefix, Value);
  if (!Res.isSuccess())
    return Res;

  if (ConvertResult && !ConvertResult(Value)) {
    Error(S, "invalid " + StringRef(Prefix) + " value.");
  }

  Operands.push_back(AMDGPUOperand::CreateImm(this, Value, S, ImmTy));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseOperandArrayWithPrefix(
    const char *Prefix, OperandVector &Operands, AMDGPUOperand::ImmTy ImmTy,
    bool (*ConvertResult)(int64_t &)) {
  SMLoc S = getLoc();
  if (!trySkipId(Prefix, AsmToken::Colon))
    return ParseStatus::NoMatch;

  if (!skipToken(AsmToken::LBrac, "expected a left square bracket"))
    return ParseStatus::Failure;

  unsigned Val = 0;
  const unsigned MaxSize = 4;

  // FIXME: How to verify the number of elements matches the number of src
  // operands?
  for (int I = 0; ; ++I) {
    int64_t Op;
    SMLoc Loc = getLoc();
    if (!parseExpr(Op))
      return ParseStatus::Failure;

    if (Op != 0 && Op != 1)
      return Error(Loc, "invalid " + StringRef(Prefix) + " value.");

    Val |= (Op << I);

    if (trySkipToken(AsmToken::RBrac))
      break;

    if (I + 1 == MaxSize)
      return Error(getLoc(), "expected a closing square bracket");

    if (!skipToken(AsmToken::Comma, "expected a comma"))
      return ParseStatus::Failure;
  }

  Operands.push_back(AMDGPUOperand::CreateImm(this, Val, S, ImmTy));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseNamedBit(StringRef Name,
                                           OperandVector &Operands,
                                           AMDGPUOperand::ImmTy ImmTy) {
  int64_t Bit;
  SMLoc S = getLoc();

  if (trySkipId(Name)) {
    Bit = 1;
  } else if (trySkipId("no", Name)) {
    Bit = 0;
  } else {
    return ParseStatus::NoMatch;
  }

  if (Name == "r128" && !hasMIMG_R128())
    return Error(S, "r128 modifier is not supported on this GPU");
  if (Name == "a16" && !hasA16())
    return Error(S, "a16 modifier is not supported on this GPU");

  if (isGFX9() && ImmTy == AMDGPUOperand::ImmTyA16)
    ImmTy = AMDGPUOperand::ImmTyR128A16;

  Operands.push_back(AMDGPUOperand::CreateImm(this, Bit, S, ImmTy));
  return ParseStatus::Success;
}

unsigned AMDGPUAsmParser::getCPolKind(StringRef Id, StringRef Mnemo,
                                      bool &Disabling) const {
  Disabling = Id.consume_front("no");

  if (isGFX940() && !Mnemo.starts_with("s_")) {
    return StringSwitch<unsigned>(Id)
        .Case("nt", AMDGPU::CPol::NT)
        .Case("sc0", AMDGPU::CPol::SC0)
        .Case("sc1", AMDGPU::CPol::SC1)
        .Default(0);
  }

  return StringSwitch<unsigned>(Id)
      .Case("dlc", AMDGPU::CPol::DLC)
      .Case("glc", AMDGPU::CPol::GLC)
      .Case("scc", AMDGPU::CPol::SCC)
      .Case("slc", AMDGPU::CPol::SLC)
      .Default(0);
}

ParseStatus AMDGPUAsmParser::parseCPol(OperandVector &Operands) {
  if (isGFX12Plus()) {
    SMLoc StringLoc = getLoc();

    int64_t CPolVal = 0;
    ParseStatus ResTH = ParseStatus::NoMatch;
    ParseStatus ResScope = ParseStatus::NoMatch;

    for (;;) {
      if (ResTH.isNoMatch()) {
        int64_t TH;
        ResTH = parseTH(Operands, TH);
        if (ResTH.isFailure())
          return ResTH;
        if (ResTH.isSuccess()) {
          CPolVal |= TH;
          continue;
        }
      }

      if (ResScope.isNoMatch()) {
        int64_t Scope;
        ResScope = parseScope(Operands, Scope);
        if (ResScope.isFailure())
          return ResScope;
        if (ResScope.isSuccess()) {
          CPolVal |= Scope;
          continue;
        }
      }

      break;
    }

    if (ResTH.isNoMatch() && ResScope.isNoMatch())
      return ParseStatus::NoMatch;

    Operands.push_back(AMDGPUOperand::CreateImm(this, CPolVal, StringLoc,
                                                AMDGPUOperand::ImmTyCPol));
    return ParseStatus::Success;
  }

  StringRef Mnemo = ((AMDGPUOperand &)*Operands[0]).getToken();
  SMLoc OpLoc = getLoc();
  unsigned Enabled = 0, Seen = 0;
  for (;;) {
    SMLoc S = getLoc();
    bool Disabling;
    unsigned CPol = getCPolKind(getId(), Mnemo, Disabling);
    if (!CPol)
      break;

    lex();

    if (!isGFX10Plus() && CPol == AMDGPU::CPol::DLC)
      return Error(S, "dlc modifier is not supported on this GPU");

    if (!isGFX90A() && CPol == AMDGPU::CPol::SCC)
      return Error(S, "scc modifier is not supported on this GPU");

    if (Seen & CPol)
      return Error(S, "duplicate cache policy modifier");

    if (!Disabling)
      Enabled |= CPol;

    Seen |= CPol;
  }

  if (!Seen)
    return ParseStatus::NoMatch;

  Operands.push_back(
      AMDGPUOperand::CreateImm(this, Enabled, OpLoc, AMDGPUOperand::ImmTyCPol));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseScope(OperandVector &Operands,
                                        int64_t &Scope) {
  Scope = AMDGPU::CPol::SCOPE_CU; // default;

  StringRef Value;
  SMLoc StringLoc;
  ParseStatus Res;

  Res = parseStringWithPrefix("scope", Value, StringLoc);
  if (!Res.isSuccess())
    return Res;

  Scope = StringSwitch<int64_t>(Value)
              .Case("SCOPE_CU", AMDGPU::CPol::SCOPE_CU)
              .Case("SCOPE_SE", AMDGPU::CPol::SCOPE_SE)
              .Case("SCOPE_DEV", AMDGPU::CPol::SCOPE_DEV)
              .Case("SCOPE_SYS", AMDGPU::CPol::SCOPE_SYS)
              .Default(0xffffffff);

  if (Scope == 0xffffffff)
    return Error(StringLoc, "invalid scope value");

  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseTH(OperandVector &Operands, int64_t &TH) {
  TH = AMDGPU::CPol::TH_RT; // default

  StringRef Value;
  SMLoc StringLoc;
  ParseStatus Res = parseStringWithPrefix("th", Value, StringLoc);
  if (!Res.isSuccess())
    return Res;

  if (Value == "TH_DEFAULT")
    TH = AMDGPU::CPol::TH_RT;
  else if (Value == "TH_STORE_LU" || Value == "TH_LOAD_RT_WB" ||
           Value == "TH_LOAD_NT_WB") {
    return Error(StringLoc, "invalid th value");
  } else if (Value.consume_front("TH_ATOMIC_")) {
    TH = AMDGPU::CPol::TH_TYPE_ATOMIC;
  } else if (Value.consume_front("TH_LOAD_")) {
    TH = AMDGPU::CPol::TH_TYPE_LOAD;
  } else if (Value.consume_front("TH_STORE_")) {
    TH = AMDGPU::CPol::TH_TYPE_STORE;
  } else {
    return Error(StringLoc, "invalid th value");
  }

  if (Value == "BYPASS")
    TH |= AMDGPU::CPol::TH_REAL_BYPASS;

  if (TH != 0) {
    if (TH & AMDGPU::CPol::TH_TYPE_ATOMIC)
      TH |= StringSwitch<int64_t>(Value)
                .Case("RETURN", AMDGPU::CPol::TH_ATOMIC_RETURN)
                .Case("RT", AMDGPU::CPol::TH_RT)
                .Case("RT_RETURN", AMDGPU::CPol::TH_ATOMIC_RETURN)
                .Case("NT", AMDGPU::CPol::TH_ATOMIC_NT)
                .Case("NT_RETURN", AMDGPU::CPol::TH_ATOMIC_NT |
                                       AMDGPU::CPol::TH_ATOMIC_RETURN)
                .Case("CASCADE_RT", AMDGPU::CPol::TH_ATOMIC_CASCADE)
                .Case("CASCADE_NT", AMDGPU::CPol::TH_ATOMIC_CASCADE |
                                        AMDGPU::CPol::TH_ATOMIC_NT)
                .Default(0xffffffff);
    else
      TH |= StringSwitch<int64_t>(Value)
                .Case("RT", AMDGPU::CPol::TH_RT)
                .Case("NT", AMDGPU::CPol::TH_NT)
                .Case("HT", AMDGPU::CPol::TH_HT)
                .Case("LU", AMDGPU::CPol::TH_LU)
                .Case("RT_WB", AMDGPU::CPol::TH_RT_WB)
                .Case("NT_RT", AMDGPU::CPol::TH_NT_RT)
                .Case("RT_NT", AMDGPU::CPol::TH_RT_NT)
                .Case("NT_HT", AMDGPU::CPol::TH_NT_HT)
                .Case("NT_WB", AMDGPU::CPol::TH_NT_WB)
                .Case("BYPASS", AMDGPU::CPol::TH_BYPASS)
                .Default(0xffffffff);
  }

  if (TH == 0xffffffff)
    return Error(StringLoc, "invalid th value");

  return ParseStatus::Success;
}

static void addOptionalImmOperand(
  MCInst& Inst, const OperandVector& Operands,
  AMDGPUAsmParser::OptionalImmIndexMap& OptionalIdx,
  AMDGPUOperand::ImmTy ImmT,
  int64_t Default = 0) {
  auto i = OptionalIdx.find(ImmT);
  if (i != OptionalIdx.end()) {
    unsigned Idx = i->second;
    ((AMDGPUOperand &)*Operands[Idx]).addImmOperands(Inst, 1);
  } else {
    Inst.addOperand(MCOperand::createImm(Default));
  }
}

ParseStatus AMDGPUAsmParser::parseStringWithPrefix(StringRef Prefix,
                                                   StringRef &Value,
                                                   SMLoc &StringLoc) {
  if (!trySkipId(Prefix, AsmToken::Colon))
    return ParseStatus::NoMatch;

  StringLoc = getLoc();
  return parseId(Value, "expected an identifier") ? ParseStatus::Success
                                                  : ParseStatus::Failure;
}

//===----------------------------------------------------------------------===//
// MTBUF format
//===----------------------------------------------------------------------===//

bool AMDGPUAsmParser::tryParseFmt(const char *Pref,
                                  int64_t MaxVal,
                                  int64_t &Fmt) {
  int64_t Val;
  SMLoc Loc = getLoc();

  auto Res = parseIntWithPrefix(Pref, Val);
  if (Res.isFailure())
    return false;
  if (Res.isNoMatch())
    return true;

  if (Val < 0 || Val > MaxVal) {
    Error(Loc, Twine("out of range ", StringRef(Pref)));
    return false;
  }

  Fmt = Val;
  return true;
}

ParseStatus AMDGPUAsmParser::tryParseIndexKey(OperandVector &Operands,
                                              AMDGPUOperand::ImmTy ImmTy) {
  const char *Pref = "index_key";
  int64_t ImmVal = 0;
  SMLoc Loc = getLoc();
  auto Res = parseIntWithPrefix(Pref, ImmVal);
  if (!Res.isSuccess())
    return Res;

  if (ImmTy == AMDGPUOperand::ImmTyIndexKey16bit && (ImmVal < 0 || ImmVal > 1))
    return Error(Loc, Twine("out of range ", StringRef(Pref)));

  if (ImmTy == AMDGPUOperand::ImmTyIndexKey8bit && (ImmVal < 0 || ImmVal > 3))
    return Error(Loc, Twine("out of range ", StringRef(Pref)));

  Operands.push_back(AMDGPUOperand::CreateImm(this, ImmVal, Loc, ImmTy));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseIndexKey8bit(OperandVector &Operands) {
  return tryParseIndexKey(Operands, AMDGPUOperand::ImmTyIndexKey8bit);
}

ParseStatus AMDGPUAsmParser::parseIndexKey16bit(OperandVector &Operands) {
  return tryParseIndexKey(Operands, AMDGPUOperand::ImmTyIndexKey16bit);
}

// dfmt and nfmt (in a tbuffer instruction) are parsed as one to allow their
// values to live in a joint format operand in the MCInst encoding.
ParseStatus AMDGPUAsmParser::parseDfmtNfmt(int64_t &Format) {
  using namespace llvm::AMDGPU::MTBUFFormat;

  int64_t Dfmt = DFMT_UNDEF;
  int64_t Nfmt = NFMT_UNDEF;

  // dfmt and nfmt can appear in either order, and each is optional.
  for (int I = 0; I < 2; ++I) {
    if (Dfmt == DFMT_UNDEF && !tryParseFmt("dfmt", DFMT_MAX, Dfmt))
      return ParseStatus::Failure;

    if (Nfmt == NFMT_UNDEF && !tryParseFmt("nfmt", NFMT_MAX, Nfmt))
      return ParseStatus::Failure;

    // Skip optional comma between dfmt/nfmt
    // but guard against 2 commas following each other.
    if ((Dfmt == DFMT_UNDEF) != (Nfmt == NFMT_UNDEF) &&
        !peekToken().is(AsmToken::Comma)) {
      trySkipToken(AsmToken::Comma);
    }
  }

  if (Dfmt == DFMT_UNDEF && Nfmt == NFMT_UNDEF)
    return ParseStatus::NoMatch;

  Dfmt = (Dfmt == DFMT_UNDEF) ? DFMT_DEFAULT : Dfmt;
  Nfmt = (Nfmt == NFMT_UNDEF) ? NFMT_DEFAULT : Nfmt;

  Format = encodeDfmtNfmt(Dfmt, Nfmt);
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseUfmt(int64_t &Format) {
  using namespace llvm::AMDGPU::MTBUFFormat;

  int64_t Fmt = UFMT_UNDEF;

  if (!tryParseFmt("format", UFMT_MAX, Fmt))
    return ParseStatus::Failure;

  if (Fmt == UFMT_UNDEF)
    return ParseStatus::NoMatch;

  Format = Fmt;
  return ParseStatus::Success;
}

bool AMDGPUAsmParser::matchDfmtNfmt(int64_t &Dfmt,
                                    int64_t &Nfmt,
                                    StringRef FormatStr,
                                    SMLoc Loc) {
  using namespace llvm::AMDGPU::MTBUFFormat;
  int64_t Format;

  Format = getDfmt(FormatStr);
  if (Format != DFMT_UNDEF) {
    Dfmt = Format;
    return true;
  }

  Format = getNfmt(FormatStr, getSTI());
  if (Format != NFMT_UNDEF) {
    Nfmt = Format;
    return true;
  }

  Error(Loc, "unsupported format");
  return false;
}

ParseStatus AMDGPUAsmParser::parseSymbolicSplitFormat(StringRef FormatStr,
                                                      SMLoc FormatLoc,
                                                      int64_t &Format) {
  using namespace llvm::AMDGPU::MTBUFFormat;

  int64_t Dfmt = DFMT_UNDEF;
  int64_t Nfmt = NFMT_UNDEF;
  if (!matchDfmtNfmt(Dfmt, Nfmt, FormatStr, FormatLoc))
    return ParseStatus::Failure;

  if (trySkipToken(AsmToken::Comma)) {
    StringRef Str;
    SMLoc Loc = getLoc();
    if (!parseId(Str, "expected a format string") ||
        !matchDfmtNfmt(Dfmt, Nfmt, Str, Loc))
      return ParseStatus::Failure;
    if (Dfmt == DFMT_UNDEF)
      return Error(Loc, "duplicate numeric format");
    if (Nfmt == NFMT_UNDEF)
      return Error(Loc, "duplicate data format");
  }

  Dfmt = (Dfmt == DFMT_UNDEF) ? DFMT_DEFAULT : Dfmt;
  Nfmt = (Nfmt == NFMT_UNDEF) ? NFMT_DEFAULT : Nfmt;

  if (isGFX10Plus()) {
    auto Ufmt = convertDfmtNfmt2Ufmt(Dfmt, Nfmt, getSTI());
    if (Ufmt == UFMT_UNDEF)
      return Error(FormatLoc, "unsupported format");
    Format = Ufmt;
  } else {
    Format = encodeDfmtNfmt(Dfmt, Nfmt);
  }

  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseSymbolicUnifiedFormat(StringRef FormatStr,
                                                        SMLoc Loc,
                                                        int64_t &Format) {
  using namespace llvm::AMDGPU::MTBUFFormat;

  auto Id = getUnifiedFormat(FormatStr, getSTI());
  if (Id == UFMT_UNDEF)
    return ParseStatus::NoMatch;

  if (!isGFX10Plus())
    return Error(Loc, "unified format is not supported on this GPU");

  Format = Id;
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseNumericFormat(int64_t &Format) {
  using namespace llvm::AMDGPU::MTBUFFormat;
  SMLoc Loc = getLoc();

  if (!parseExpr(Format))
    return ParseStatus::Failure;
  if (!isValidFormatEncoding(Format, getSTI()))
    return Error(Loc, "out of range format");

  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseSymbolicOrNumericFormat(int64_t &Format) {
  using namespace llvm::AMDGPU::MTBUFFormat;

  if (!trySkipId("format", AsmToken::Colon))
    return ParseStatus::NoMatch;

  if (trySkipToken(AsmToken::LBrac)) {
    StringRef FormatStr;
    SMLoc Loc = getLoc();
    if (!parseId(FormatStr, "expected a format string"))
      return ParseStatus::Failure;

    auto Res = parseSymbolicUnifiedFormat(FormatStr, Loc, Format);
    if (Res.isNoMatch())
      Res = parseSymbolicSplitFormat(FormatStr, Loc, Format);
    if (!Res.isSuccess())
      return Res;

    if (!skipToken(AsmToken::RBrac, "expected a closing square bracket"))
      return ParseStatus::Failure;

    return ParseStatus::Success;
  }

  return parseNumericFormat(Format);
}

ParseStatus AMDGPUAsmParser::parseFORMAT(OperandVector &Operands) {
  using namespace llvm::AMDGPU::MTBUFFormat;

  int64_t Format = getDefaultFormatEncoding(getSTI());
  ParseStatus Res;
  SMLoc Loc = getLoc();

  // Parse legacy format syntax.
  Res = isGFX10Plus() ? parseUfmt(Format) : parseDfmtNfmt(Format);
  if (Res.isFailure())
    return Res;

  bool FormatFound = Res.isSuccess();

  Operands.push_back(
    AMDGPUOperand::CreateImm(this, Format, Loc, AMDGPUOperand::ImmTyFORMAT));

  if (FormatFound)
    trySkipToken(AsmToken::Comma);

  if (isToken(AsmToken::EndOfStatement)) {
    // We are expecting an soffset operand,
    // but let matcher handle the error.
    return ParseStatus::Success;
  }

  // Parse soffset.
  Res = parseRegOrImm(Operands);
  if (!Res.isSuccess())
    return Res;

  trySkipToken(AsmToken::Comma);

  if (!FormatFound) {
    Res = parseSymbolicOrNumericFormat(Format);
    if (Res.isFailure())
      return Res;
    if (Res.isSuccess()) {
      auto Size = Operands.size();
      AMDGPUOperand &Op = static_cast<AMDGPUOperand &>(*Operands[Size - 2]);
      assert(Op.isImm() && Op.getImmTy() == AMDGPUOperand::ImmTyFORMAT);
      Op.setImm(Format);
    }
    return ParseStatus::Success;
  }

  if (isId("format") && peekToken().is(AsmToken::Colon))
    return Error(getLoc(), "duplicate format");
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseFlatOffset(OperandVector &Operands) {
  ParseStatus Res =
      parseIntWithPrefix("offset", Operands, AMDGPUOperand::ImmTyOffset);
  if (Res.isNoMatch()) {
    Res = parseIntWithPrefix("inst_offset", Operands,
                             AMDGPUOperand::ImmTyInstOffset);
  }
  return Res;
}

ParseStatus AMDGPUAsmParser::parseR128A16(OperandVector &Operands) {
  ParseStatus Res =
      parseNamedBit("r128", Operands, AMDGPUOperand::ImmTyR128A16);
  if (Res.isNoMatch())
    Res = parseNamedBit("a16", Operands, AMDGPUOperand::ImmTyA16);
  return Res;
}

ParseStatus AMDGPUAsmParser::parseBLGP(OperandVector &Operands) {
  ParseStatus Res =
      parseIntWithPrefix("blgp", Operands, AMDGPUOperand::ImmTyBLGP);
  if (Res.isNoMatch()) {
    Res =
        parseOperandArrayWithPrefix("neg", Operands, AMDGPUOperand::ImmTyBLGP);
  }
  return Res;
}

//===----------------------------------------------------------------------===//
// Exp
//===----------------------------------------------------------------------===//

void AMDGPUAsmParser::cvtExp(MCInst &Inst, const OperandVector &Operands) {
  OptionalImmIndexMap OptionalIdx;

  unsigned OperandIdx[4];
  unsigned EnMask = 0;
  int SrcIdx = 0;

  for (unsigned i = 1, e = Operands.size(); i != e; ++i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);

    // Add the register arguments
    if (Op.isReg()) {
      assert(SrcIdx < 4);
      OperandIdx[SrcIdx] = Inst.size();
      Op.addRegOperands(Inst, 1);
      ++SrcIdx;
      continue;
    }

    if (Op.isOff()) {
      assert(SrcIdx < 4);
      OperandIdx[SrcIdx] = Inst.size();
      Inst.addOperand(MCOperand::createReg(AMDGPU::NoRegister));
      ++SrcIdx;
      continue;
    }

    if (Op.isImm() && Op.getImmTy() == AMDGPUOperand::ImmTyExpTgt) {
      Op.addImmOperands(Inst, 1);
      continue;
    }

    if (Op.isToken() && (Op.getToken() == "done" || Op.getToken() == "row_en"))
      continue;

    // Handle optional arguments
    OptionalIdx[Op.getImmTy()] = i;
  }

  assert(SrcIdx == 4);

  bool Compr = false;
  if (OptionalIdx.find(AMDGPUOperand::ImmTyExpCompr) != OptionalIdx.end()) {
    Compr = true;
    Inst.getOperand(OperandIdx[1]) = Inst.getOperand(OperandIdx[2]);
    Inst.getOperand(OperandIdx[2]).setReg(AMDGPU::NoRegister);
    Inst.getOperand(OperandIdx[3]).setReg(AMDGPU::NoRegister);
  }

  for (auto i = 0; i < SrcIdx; ++i) {
    if (Inst.getOperand(OperandIdx[i]).getReg() != AMDGPU::NoRegister) {
      EnMask |= Compr? (0x3 << i * 2) : (0x1 << i);
    }
  }

  addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyExpVM);
  addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyExpCompr);

  Inst.addOperand(MCOperand::createImm(EnMask));
}

//===----------------------------------------------------------------------===//
// s_waitcnt
//===----------------------------------------------------------------------===//

static bool
encodeCnt(
  const AMDGPU::IsaVersion ISA,
  int64_t &IntVal,
  int64_t CntVal,
  bool Saturate,
  unsigned (*encode)(const IsaVersion &Version, unsigned, unsigned),
  unsigned (*decode)(const IsaVersion &Version, unsigned))
{
  bool Failed = false;

  IntVal = encode(ISA, IntVal, CntVal);
  if (CntVal != decode(ISA, IntVal)) {
    if (Saturate) {
      IntVal = encode(ISA, IntVal, -1);
    } else {
      Failed = true;
    }
  }
  return Failed;
}

bool AMDGPUAsmParser::parseCnt(int64_t &IntVal) {

  SMLoc CntLoc = getLoc();
  StringRef CntName = getTokenStr();

  if (!skipToken(AsmToken::Identifier, "expected a counter name") ||
      !skipToken(AsmToken::LParen, "expected a left parenthesis"))
    return false;

  int64_t CntVal;
  SMLoc ValLoc = getLoc();
  if (!parseExpr(CntVal))
    return false;

  AMDGPU::IsaVersion ISA = AMDGPU::getIsaVersion(getSTI().getCPU());

  bool Failed = true;
  bool Sat = CntName.ends_with("_sat");

  if (CntName == "vmcnt" || CntName == "vmcnt_sat") {
    Failed = encodeCnt(ISA, IntVal, CntVal, Sat, encodeVmcnt, decodeVmcnt);
  } else if (CntName == "expcnt" || CntName == "expcnt_sat") {
    Failed = encodeCnt(ISA, IntVal, CntVal, Sat, encodeExpcnt, decodeExpcnt);
  } else if (CntName == "lgkmcnt" || CntName == "lgkmcnt_sat") {
    Failed = encodeCnt(ISA, IntVal, CntVal, Sat, encodeLgkmcnt, decodeLgkmcnt);
  } else {
    Error(CntLoc, "invalid counter name " + CntName);
    return false;
  }

  if (Failed) {
    Error(ValLoc, "too large value for " + CntName);
    return false;
  }

  if (!skipToken(AsmToken::RParen, "expected a closing parenthesis"))
    return false;

  if (trySkipToken(AsmToken::Amp) || trySkipToken(AsmToken::Comma)) {
    if (isToken(AsmToken::EndOfStatement)) {
      Error(getLoc(), "expected a counter name");
      return false;
    }
  }

  return true;
}

ParseStatus AMDGPUAsmParser::parseSWaitCnt(OperandVector &Operands) {
  AMDGPU::IsaVersion ISA = AMDGPU::getIsaVersion(getSTI().getCPU());
  int64_t Waitcnt = getWaitcntBitMask(ISA);
  SMLoc S = getLoc();

  if (isToken(AsmToken::Identifier) && peekToken().is(AsmToken::LParen)) {
    while (!isToken(AsmToken::EndOfStatement)) {
      if (!parseCnt(Waitcnt))
        return ParseStatus::Failure;
    }
  } else {
    if (!parseExpr(Waitcnt))
      return ParseStatus::Failure;
  }

  Operands.push_back(AMDGPUOperand::CreateImm(this, Waitcnt, S));
  return ParseStatus::Success;
}

bool AMDGPUAsmParser::parseDelay(int64_t &Delay) {
  SMLoc FieldLoc = getLoc();
  StringRef FieldName = getTokenStr();
  if (!skipToken(AsmToken::Identifier, "expected a field name") ||
      !skipToken(AsmToken::LParen, "expected a left parenthesis"))
    return false;

  SMLoc ValueLoc = getLoc();
  StringRef ValueName = getTokenStr();
  if (!skipToken(AsmToken::Identifier, "expected a value name") ||
      !skipToken(AsmToken::RParen, "expected a right parenthesis"))
    return false;

  unsigned Shift;
  if (FieldName == "instid0") {
    Shift = 0;
  } else if (FieldName == "instskip") {
    Shift = 4;
  } else if (FieldName == "instid1") {
    Shift = 7;
  } else {
    Error(FieldLoc, "invalid field name " + FieldName);
    return false;
  }

  int Value;
  if (Shift == 4) {
    // Parse values for instskip.
    Value = StringSwitch<int>(ValueName)
                .Case("SAME", 0)
                .Case("NEXT", 1)
                .Case("SKIP_1", 2)
                .Case("SKIP_2", 3)
                .Case("SKIP_3", 4)
                .Case("SKIP_4", 5)
                .Default(-1);
  } else {
    // Parse values for instid0 and instid1.
    Value = StringSwitch<int>(ValueName)
                .Case("NO_DEP", 0)
                .Case("VALU_DEP_1", 1)
                .Case("VALU_DEP_2", 2)
                .Case("VALU_DEP_3", 3)
                .Case("VALU_DEP_4", 4)
                .Case("TRANS32_DEP_1", 5)
                .Case("TRANS32_DEP_2", 6)
                .Case("TRANS32_DEP_3", 7)
                .Case("FMA_ACCUM_CYCLE_1", 8)
                .Case("SALU_CYCLE_1", 9)
                .Case("SALU_CYCLE_2", 10)
                .Case("SALU_CYCLE_3", 11)
                .Default(-1);
  }
  if (Value < 0) {
    Error(ValueLoc, "invalid value name " + ValueName);
    return false;
  }

  Delay |= Value << Shift;
  return true;
}

ParseStatus AMDGPUAsmParser::parseSDelayALU(OperandVector &Operands) {
  int64_t Delay = 0;
  SMLoc S = getLoc();

  if (isToken(AsmToken::Identifier) && peekToken().is(AsmToken::LParen)) {
    do {
      if (!parseDelay(Delay))
        return ParseStatus::Failure;
    } while (trySkipToken(AsmToken::Pipe));
  } else {
    if (!parseExpr(Delay))
      return ParseStatus::Failure;
  }

  Operands.push_back(AMDGPUOperand::CreateImm(this, Delay, S));
  return ParseStatus::Success;
}

bool
AMDGPUOperand::isSWaitCnt() const {
  return isImm();
}

bool AMDGPUOperand::isSDelayALU() const { return isImm(); }

//===----------------------------------------------------------------------===//
// DepCtr
//===----------------------------------------------------------------------===//

void AMDGPUAsmParser::depCtrError(SMLoc Loc, int ErrorId,
                                  StringRef DepCtrName) {
  switch (ErrorId) {
  case OPR_ID_UNKNOWN:
    Error(Loc, Twine("invalid counter name ", DepCtrName));
    return;
  case OPR_ID_UNSUPPORTED:
    Error(Loc, Twine(DepCtrName, " is not supported on this GPU"));
    return;
  case OPR_ID_DUPLICATE:
    Error(Loc, Twine("duplicate counter name ", DepCtrName));
    return;
  case OPR_VAL_INVALID:
    Error(Loc, Twine("invalid value for ", DepCtrName));
    return;
  default:
    assert(false);
  }
}

bool AMDGPUAsmParser::parseDepCtr(int64_t &DepCtr, unsigned &UsedOprMask) {

  using namespace llvm::AMDGPU::DepCtr;

  SMLoc DepCtrLoc = getLoc();
  StringRef DepCtrName = getTokenStr();

  if (!skipToken(AsmToken::Identifier, "expected a counter name") ||
      !skipToken(AsmToken::LParen, "expected a left parenthesis"))
    return false;

  int64_t ExprVal;
  if (!parseExpr(ExprVal))
    return false;

  unsigned PrevOprMask = UsedOprMask;
  int CntVal = encodeDepCtr(DepCtrName, ExprVal, UsedOprMask, getSTI());

  if (CntVal < 0) {
    depCtrError(DepCtrLoc, CntVal, DepCtrName);
    return false;
  }

  if (!skipToken(AsmToken::RParen, "expected a closing parenthesis"))
    return false;

  if (trySkipToken(AsmToken::Amp) || trySkipToken(AsmToken::Comma)) {
    if (isToken(AsmToken::EndOfStatement)) {
      Error(getLoc(), "expected a counter name");
      return false;
    }
  }

  unsigned CntValMask = PrevOprMask ^ UsedOprMask;
  DepCtr = (DepCtr & ~CntValMask) | CntVal;
  return true;
}

ParseStatus AMDGPUAsmParser::parseDepCtr(OperandVector &Operands) {
  using namespace llvm::AMDGPU::DepCtr;

  int64_t DepCtr = getDefaultDepCtrEncoding(getSTI());
  SMLoc Loc = getLoc();

  if (isToken(AsmToken::Identifier) && peekToken().is(AsmToken::LParen)) {
    unsigned UsedOprMask = 0;
    while (!isToken(AsmToken::EndOfStatement)) {
      if (!parseDepCtr(DepCtr, UsedOprMask))
        return ParseStatus::Failure;
    }
  } else {
    if (!parseExpr(DepCtr))
      return ParseStatus::Failure;
  }

  Operands.push_back(AMDGPUOperand::CreateImm(this, DepCtr, Loc));
  return ParseStatus::Success;
}

bool AMDGPUOperand::isDepCtr() const { return isS16Imm(); }

//===----------------------------------------------------------------------===//
// hwreg
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseHwregFunc(OperandInfoTy &HwReg,
                                            OperandInfoTy &Offset,
                                            OperandInfoTy &Width) {
  using namespace llvm::AMDGPU::Hwreg;

  if (!trySkipId("hwreg", AsmToken::LParen))
    return ParseStatus::NoMatch;

  // The register may be specified by name or using a numeric code
  HwReg.Loc = getLoc();
  if (isToken(AsmToken::Identifier) &&
      (HwReg.Val = getHwregId(getTokenStr(), getSTI())) != OPR_ID_UNKNOWN) {
    HwReg.IsSymbolic = true;
    lex(); // skip register name
  } else if (!parseExpr(HwReg.Val, "a register name")) {
    return ParseStatus::Failure;
  }

  if (trySkipToken(AsmToken::RParen))
    return ParseStatus::Success;

  // parse optional params
  if (!skipToken(AsmToken::Comma, "expected a comma or a closing parenthesis"))
    return ParseStatus::Failure;

  Offset.Loc = getLoc();
  if (!parseExpr(Offset.Val))
    return ParseStatus::Failure;

  if (!skipToken(AsmToken::Comma, "expected a comma"))
    return ParseStatus::Failure;

  Width.Loc = getLoc();
  if (!parseExpr(Width.Val) ||
      !skipToken(AsmToken::RParen, "expected a closing parenthesis"))
    return ParseStatus::Failure;

  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseHwreg(OperandVector &Operands) {
  using namespace llvm::AMDGPU::Hwreg;

  int64_t ImmVal = 0;
  SMLoc Loc = getLoc();

  StructuredOpField HwReg("id", "hardware register", HwregId::Width,
                          HwregId::Default);
  StructuredOpField Offset("offset", "bit offset", HwregOffset::Width,
                           HwregOffset::Default);
  struct : StructuredOpField {
    using StructuredOpField::StructuredOpField;
    bool validate(AMDGPUAsmParser &Parser) const override {
      if (!isUIntN(Width, Val - 1))
        return Error(Parser, "only values from 1 to 32 are legal");
      return true;
    }
  } Width("size", "bitfield width", HwregSize::Width, HwregSize::Default);
  ParseStatus Res = parseStructuredOpFields({&HwReg, &Offset, &Width});

  if (Res.isNoMatch())
    Res = parseHwregFunc(HwReg, Offset, Width);

  if (Res.isSuccess()) {
    if (!validateStructuredOpFields({&HwReg, &Offset, &Width}))
      return ParseStatus::Failure;
    ImmVal = HwregEncoding::encode(HwReg.Val, Offset.Val, Width.Val);
  }

  if (Res.isNoMatch() &&
      parseExpr(ImmVal, "a hwreg macro, structured immediate"))
    Res = ParseStatus::Success;

  if (!Res.isSuccess())
    return ParseStatus::Failure;

  if (!isUInt<16>(ImmVal))
    return Error(Loc, "invalid immediate: only 16-bit values are legal");
  Operands.push_back(
      AMDGPUOperand::CreateImm(this, ImmVal, Loc, AMDGPUOperand::ImmTyHwreg));
  return ParseStatus::Success;
}

bool AMDGPUOperand::isHwreg() const {
  return isImmTy(ImmTyHwreg);
}

//===----------------------------------------------------------------------===//
// sendmsg
//===----------------------------------------------------------------------===//

bool
AMDGPUAsmParser::parseSendMsgBody(OperandInfoTy &Msg,
                                  OperandInfoTy &Op,
                                  OperandInfoTy &Stream) {
  using namespace llvm::AMDGPU::SendMsg;

  Msg.Loc = getLoc();
  if (isToken(AsmToken::Identifier) &&
      (Msg.Val = getMsgId(getTokenStr(), getSTI())) != OPR_ID_UNKNOWN) {
    Msg.IsSymbolic = true;
    lex(); // skip message name
  } else if (!parseExpr(Msg.Val, "a message name")) {
    return false;
  }

  if (trySkipToken(AsmToken::Comma)) {
    Op.IsDefined = true;
    Op.Loc = getLoc();
    if (isToken(AsmToken::Identifier) &&
        (Op.Val = getMsgOpId(Msg.Val, getTokenStr(), getSTI())) !=
            OPR_ID_UNKNOWN) {
      lex(); // skip operation name
    } else if (!parseExpr(Op.Val, "an operation name")) {
      return false;
    }

    if (trySkipToken(AsmToken::Comma)) {
      Stream.IsDefined = true;
      Stream.Loc = getLoc();
      if (!parseExpr(Stream.Val))
        return false;
    }
  }

  return skipToken(AsmToken::RParen, "expected a closing parenthesis");
}

bool
AMDGPUAsmParser::validateSendMsg(const OperandInfoTy &Msg,
                                 const OperandInfoTy &Op,
                                 const OperandInfoTy &Stream) {
  using namespace llvm::AMDGPU::SendMsg;

  // Validation strictness depends on whether message is specified
  // in a symbolic or in a numeric form. In the latter case
  // only encoding possibility is checked.
  bool Strict = Msg.IsSymbolic;

  if (Strict) {
    if (Msg.Val == OPR_ID_UNSUPPORTED) {
      Error(Msg.Loc, "specified message id is not supported on this GPU");
      return false;
    }
  } else {
    if (!isValidMsgId(Msg.Val, getSTI())) {
      Error(Msg.Loc, "invalid message id");
      return false;
    }
  }
  if (Strict && (msgRequiresOp(Msg.Val, getSTI()) != Op.IsDefined)) {
    if (Op.IsDefined) {
      Error(Op.Loc, "message does not support operations");
    } else {
      Error(Msg.Loc, "missing message operation");
    }
    return false;
  }
  if (!isValidMsgOp(Msg.Val, Op.Val, getSTI(), Strict)) {
    if (Op.Val == OPR_ID_UNSUPPORTED)
      Error(Op.Loc, "specified operation id is not supported on this GPU");
    else
      Error(Op.Loc, "invalid operation id");
    return false;
  }
  if (Strict && !msgSupportsStream(Msg.Val, Op.Val, getSTI()) &&
      Stream.IsDefined) {
    Error(Stream.Loc, "message operation does not support streams");
    return false;
  }
  if (!isValidMsgStream(Msg.Val, Op.Val, Stream.Val, getSTI(), Strict)) {
    Error(Stream.Loc, "invalid message stream id");
    return false;
  }
  return true;
}

ParseStatus AMDGPUAsmParser::parseSendMsg(OperandVector &Operands) {
  using namespace llvm::AMDGPU::SendMsg;

  int64_t ImmVal = 0;
  SMLoc Loc = getLoc();

  if (trySkipId("sendmsg", AsmToken::LParen)) {
    OperandInfoTy Msg(OPR_ID_UNKNOWN);
    OperandInfoTy Op(OP_NONE_);
    OperandInfoTy Stream(STREAM_ID_NONE_);
    if (parseSendMsgBody(Msg, Op, Stream) &&
        validateSendMsg(Msg, Op, Stream)) {
      ImmVal = encodeMsg(Msg.Val, Op.Val, Stream.Val);
    } else {
      return ParseStatus::Failure;
    }
  } else if (parseExpr(ImmVal, "a sendmsg macro")) {
    if (ImmVal < 0 || !isUInt<16>(ImmVal))
      return Error(Loc, "invalid immediate: only 16-bit values are legal");
  } else {
    return ParseStatus::Failure;
  }

  Operands.push_back(AMDGPUOperand::CreateImm(this, ImmVal, Loc, AMDGPUOperand::ImmTySendMsg));
  return ParseStatus::Success;
}

bool AMDGPUOperand::isSendMsg() const {
  return isImmTy(ImmTySendMsg);
}

//===----------------------------------------------------------------------===//
// v_interp
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseInterpSlot(OperandVector &Operands) {
  StringRef Str;
  SMLoc S = getLoc();

  if (!parseId(Str))
    return ParseStatus::NoMatch;

  int Slot = StringSwitch<int>(Str)
    .Case("p10", 0)
    .Case("p20", 1)
    .Case("p0", 2)
    .Default(-1);

  if (Slot == -1)
    return Error(S, "invalid interpolation slot");

  Operands.push_back(AMDGPUOperand::CreateImm(this, Slot, S,
                                              AMDGPUOperand::ImmTyInterpSlot));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseInterpAttr(OperandVector &Operands) {
  StringRef Str;
  SMLoc S = getLoc();

  if (!parseId(Str))
    return ParseStatus::NoMatch;

  if (!Str.starts_with("attr"))
    return Error(S, "invalid interpolation attribute");

  StringRef Chan = Str.take_back(2);
  int AttrChan = StringSwitch<int>(Chan)
    .Case(".x", 0)
    .Case(".y", 1)
    .Case(".z", 2)
    .Case(".w", 3)
    .Default(-1);
  if (AttrChan == -1)
    return Error(S, "invalid or missing interpolation attribute channel");

  Str = Str.drop_back(2).drop_front(4);

  uint8_t Attr;
  if (Str.getAsInteger(10, Attr))
    return Error(S, "invalid or missing interpolation attribute number");

  if (Attr > 32)
    return Error(S, "out of bounds interpolation attribute number");

  SMLoc SChan = SMLoc::getFromPointer(Chan.data());

  Operands.push_back(AMDGPUOperand::CreateImm(this, Attr, S,
                                              AMDGPUOperand::ImmTyInterpAttr));
  Operands.push_back(AMDGPUOperand::CreateImm(
      this, AttrChan, SChan, AMDGPUOperand::ImmTyInterpAttrChan));
  return ParseStatus::Success;
}

//===----------------------------------------------------------------------===//
// exp
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseExpTgt(OperandVector &Operands) {
  using namespace llvm::AMDGPU::Exp;

  StringRef Str;
  SMLoc S = getLoc();

  if (!parseId(Str))
    return ParseStatus::NoMatch;

  unsigned Id = getTgtId(Str);
  if (Id == ET_INVALID || !isSupportedTgtId(Id, getSTI()))
    return Error(S, (Id == ET_INVALID)
                        ? "invalid exp target"
                        : "exp target is not supported on this GPU");

  Operands.push_back(AMDGPUOperand::CreateImm(this, Id, S,
                                              AMDGPUOperand::ImmTyExpTgt));
  return ParseStatus::Success;
}

//===----------------------------------------------------------------------===//
// parser helpers
//===----------------------------------------------------------------------===//

bool
AMDGPUAsmParser::isId(const AsmToken &Token, const StringRef Id) const {
  return Token.is(AsmToken::Identifier) && Token.getString() == Id;
}

bool
AMDGPUAsmParser::isId(const StringRef Id) const {
  return isId(getToken(), Id);
}

bool
AMDGPUAsmParser::isToken(const AsmToken::TokenKind Kind) const {
  return getTokenKind() == Kind;
}

StringRef AMDGPUAsmParser::getId() const {
  return isToken(AsmToken::Identifier) ? getTokenStr() : StringRef();
}

bool
AMDGPUAsmParser::trySkipId(const StringRef Id) {
  if (isId(Id)) {
    lex();
    return true;
  }
  return false;
}

bool
AMDGPUAsmParser::trySkipId(const StringRef Pref, const StringRef Id) {
  if (isToken(AsmToken::Identifier)) {
    StringRef Tok = getTokenStr();
    if (Tok.starts_with(Pref) && Tok.drop_front(Pref.size()) == Id) {
      lex();
      return true;
    }
  }
  return false;
}

bool
AMDGPUAsmParser::trySkipId(const StringRef Id, const AsmToken::TokenKind Kind) {
  if (isId(Id) && peekToken().is(Kind)) {
    lex();
    lex();
    return true;
  }
  return false;
}

bool
AMDGPUAsmParser::trySkipToken(const AsmToken::TokenKind Kind) {
  if (isToken(Kind)) {
    lex();
    return true;
  }
  return false;
}

bool
AMDGPUAsmParser::skipToken(const AsmToken::TokenKind Kind,
                           const StringRef ErrMsg) {
  if (!trySkipToken(Kind)) {
    Error(getLoc(), ErrMsg);
    return false;
  }
  return true;
}

bool
AMDGPUAsmParser::parseExpr(int64_t &Imm, StringRef Expected) {
  SMLoc S = getLoc();

  const MCExpr *Expr;
  if (Parser.parseExpression(Expr))
    return false;

  if (Expr->evaluateAsAbsolute(Imm))
    return true;

  if (Expected.empty()) {
    Error(S, "expected absolute expression");
  } else {
    Error(S, Twine("expected ", Expected) +
             Twine(" or an absolute expression"));
  }
  return false;
}

bool
AMDGPUAsmParser::parseExpr(OperandVector &Operands) {
  SMLoc S = getLoc();

  const MCExpr *Expr;
  if (Parser.parseExpression(Expr))
    return false;

  int64_t IntVal;
  if (Expr->evaluateAsAbsolute(IntVal)) {
    Operands.push_back(AMDGPUOperand::CreateImm(this, IntVal, S));
  } else {
    Operands.push_back(AMDGPUOperand::CreateExpr(this, Expr, S));
  }
  return true;
}

bool
AMDGPUAsmParser::parseString(StringRef &Val, const StringRef ErrMsg) {
  if (isToken(AsmToken::String)) {
    Val = getToken().getStringContents();
    lex();
    return true;
  }
  Error(getLoc(), ErrMsg);
  return false;
}

bool
AMDGPUAsmParser::parseId(StringRef &Val, const StringRef ErrMsg) {
  if (isToken(AsmToken::Identifier)) {
    Val = getTokenStr();
    lex();
    return true;
  }
  if (!ErrMsg.empty())
    Error(getLoc(), ErrMsg);
  return false;
}

AsmToken
AMDGPUAsmParser::getToken() const {
  return Parser.getTok();
}

AsmToken AMDGPUAsmParser::peekToken(bool ShouldSkipSpace) {
  return isToken(AsmToken::EndOfStatement)
             ? getToken()
             : getLexer().peekTok(ShouldSkipSpace);
}

void
AMDGPUAsmParser::peekTokens(MutableArrayRef<AsmToken> Tokens) {
  auto TokCount = getLexer().peekTokens(Tokens);

  for (auto Idx = TokCount; Idx < Tokens.size(); ++Idx)
    Tokens[Idx] = AsmToken(AsmToken::Error, "");
}

AsmToken::TokenKind
AMDGPUAsmParser::getTokenKind() const {
  return getLexer().getKind();
}

SMLoc
AMDGPUAsmParser::getLoc() const {
  return getToken().getLoc();
}

StringRef
AMDGPUAsmParser::getTokenStr() const {
  return getToken().getString();
}

void
AMDGPUAsmParser::lex() {
  Parser.Lex();
}

SMLoc AMDGPUAsmParser::getInstLoc(const OperandVector &Operands) const {
  return ((AMDGPUOperand &)*Operands[0]).getStartLoc();
}

SMLoc
AMDGPUAsmParser::getOperandLoc(std::function<bool(const AMDGPUOperand&)> Test,
                               const OperandVector &Operands) const {
  for (unsigned i = Operands.size() - 1; i > 0; --i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);
    if (Test(Op))
      return Op.getStartLoc();
  }
  return getInstLoc(Operands);
}

SMLoc
AMDGPUAsmParser::getImmLoc(AMDGPUOperand::ImmTy Type,
                           const OperandVector &Operands) const {
  auto Test = [=](const AMDGPUOperand& Op) { return Op.isImmTy(Type); };
  return getOperandLoc(Test, Operands);
}

SMLoc
AMDGPUAsmParser::getRegLoc(unsigned Reg,
                           const OperandVector &Operands) const {
  auto Test = [=](const AMDGPUOperand& Op) {
    return Op.isRegKind() && Op.getReg() == Reg;
  };
  return getOperandLoc(Test, Operands);
}

SMLoc AMDGPUAsmParser::getLitLoc(const OperandVector &Operands,
                                 bool SearchMandatoryLiterals) const {
  auto Test = [](const AMDGPUOperand& Op) {
    return Op.IsImmKindLiteral() || Op.isExpr();
  };
  SMLoc Loc = getOperandLoc(Test, Operands);
  if (SearchMandatoryLiterals && Loc == getInstLoc(Operands))
    Loc = getMandatoryLitLoc(Operands);
  return Loc;
}

SMLoc AMDGPUAsmParser::getMandatoryLitLoc(const OperandVector &Operands) const {
  auto Test = [](const AMDGPUOperand &Op) {
    return Op.IsImmKindMandatoryLiteral();
  };
  return getOperandLoc(Test, Operands);
}

SMLoc
AMDGPUAsmParser::getConstLoc(const OperandVector &Operands) const {
  auto Test = [](const AMDGPUOperand& Op) {
    return Op.isImmKindConst();
  };
  return getOperandLoc(Test, Operands);
}

ParseStatus
AMDGPUAsmParser::parseStructuredOpFields(ArrayRef<StructuredOpField *> Fields) {
  if (!trySkipToken(AsmToken::LCurly))
    return ParseStatus::NoMatch;

  bool First = true;
  while (!trySkipToken(AsmToken::RCurly)) {
    if (!First &&
        !skipToken(AsmToken::Comma, "comma or closing brace expected"))
      return ParseStatus::Failure;

    StringRef Id = getTokenStr();
    SMLoc IdLoc = getLoc();
    if (!skipToken(AsmToken::Identifier, "field name expected") ||
        !skipToken(AsmToken::Colon, "colon expected"))
      return ParseStatus::Failure;

    auto I =
        find_if(Fields, [Id](StructuredOpField *F) { return F->Id == Id; });
    if (I == Fields.end())
      return Error(IdLoc, "unknown field");
    if ((*I)->IsDefined)
      return Error(IdLoc, "duplicate field");

    // TODO: Support symbolic values.
    (*I)->Loc = getLoc();
    if (!parseExpr((*I)->Val))
      return ParseStatus::Failure;
    (*I)->IsDefined = true;

    First = false;
  }
  return ParseStatus::Success;
}

bool AMDGPUAsmParser::validateStructuredOpFields(
    ArrayRef<const StructuredOpField *> Fields) {
  return all_of(Fields, [this](const StructuredOpField *F) {
    return F->validate(*this);
  });
}

//===----------------------------------------------------------------------===//
// swizzle
//===----------------------------------------------------------------------===//

LLVM_READNONE
static unsigned
encodeBitmaskPerm(const unsigned AndMask,
                  const unsigned OrMask,
                  const unsigned XorMask) {
  using namespace llvm::AMDGPU::Swizzle;

  return BITMASK_PERM_ENC |
         (AndMask << BITMASK_AND_SHIFT) |
         (OrMask  << BITMASK_OR_SHIFT)  |
         (XorMask << BITMASK_XOR_SHIFT);
}

bool
AMDGPUAsmParser::parseSwizzleOperand(int64_t &Op,
                                     const unsigned MinVal,
                                     const unsigned MaxVal,
                                     const StringRef ErrMsg,
                                     SMLoc &Loc) {
  if (!skipToken(AsmToken::Comma, "expected a comma")) {
    return false;
  }
  Loc = getLoc();
  if (!parseExpr(Op)) {
    return false;
  }
  if (Op < MinVal || Op > MaxVal) {
    Error(Loc, ErrMsg);
    return false;
  }

  return true;
}

bool
AMDGPUAsmParser::parseSwizzleOperands(const unsigned OpNum, int64_t* Op,
                                      const unsigned MinVal,
                                      const unsigned MaxVal,
                                      const StringRef ErrMsg) {
  SMLoc Loc;
  for (unsigned i = 0; i < OpNum; ++i) {
    if (!parseSwizzleOperand(Op[i], MinVal, MaxVal, ErrMsg, Loc))
      return false;
  }

  return true;
}

bool
AMDGPUAsmParser::parseSwizzleQuadPerm(int64_t &Imm) {
  using namespace llvm::AMDGPU::Swizzle;

  int64_t Lane[LANE_NUM];
  if (parseSwizzleOperands(LANE_NUM, Lane, 0, LANE_MAX,
                           "expected a 2-bit lane id")) {
    Imm = QUAD_PERM_ENC;
    for (unsigned I = 0; I < LANE_NUM; ++I) {
      Imm |= Lane[I] << (LANE_SHIFT * I);
    }
    return true;
  }
  return false;
}

bool
AMDGPUAsmParser::parseSwizzleBroadcast(int64_t &Imm) {
  using namespace llvm::AMDGPU::Swizzle;

  SMLoc Loc;
  int64_t GroupSize;
  int64_t LaneIdx;

  if (!parseSwizzleOperand(GroupSize,
                           2, 32,
                           "group size must be in the interval [2,32]",
                           Loc)) {
    return false;
  }
  if (!isPowerOf2_64(GroupSize)) {
    Error(Loc, "group size must be a power of two");
    return false;
  }
  if (parseSwizzleOperand(LaneIdx,
                          0, GroupSize - 1,
                          "lane id must be in the interval [0,group size - 1]",
                          Loc)) {
    Imm = encodeBitmaskPerm(BITMASK_MAX - GroupSize + 1, LaneIdx, 0);
    return true;
  }
  return false;
}

bool
AMDGPUAsmParser::parseSwizzleReverse(int64_t &Imm) {
  using namespace llvm::AMDGPU::Swizzle;

  SMLoc Loc;
  int64_t GroupSize;

  if (!parseSwizzleOperand(GroupSize,
                           2, 32,
                           "group size must be in the interval [2,32]",
                           Loc)) {
    return false;
  }
  if (!isPowerOf2_64(GroupSize)) {
    Error(Loc, "group size must be a power of two");
    return false;
  }

  Imm = encodeBitmaskPerm(BITMASK_MAX, 0, GroupSize - 1);
  return true;
}

bool
AMDGPUAsmParser::parseSwizzleSwap(int64_t &Imm) {
  using namespace llvm::AMDGPU::Swizzle;

  SMLoc Loc;
  int64_t GroupSize;

  if (!parseSwizzleOperand(GroupSize,
                           1, 16,
                           "group size must be in the interval [1,16]",
                           Loc)) {
    return false;
  }
  if (!isPowerOf2_64(GroupSize)) {
    Error(Loc, "group size must be a power of two");
    return false;
  }

  Imm = encodeBitmaskPerm(BITMASK_MAX, 0, GroupSize);
  return true;
}

bool
AMDGPUAsmParser::parseSwizzleBitmaskPerm(int64_t &Imm) {
  using namespace llvm::AMDGPU::Swizzle;

  if (!skipToken(AsmToken::Comma, "expected a comma")) {
    return false;
  }

  StringRef Ctl;
  SMLoc StrLoc = getLoc();
  if (!parseString(Ctl)) {
    return false;
  }
  if (Ctl.size() != BITMASK_WIDTH) {
    Error(StrLoc, "expected a 5-character mask");
    return false;
  }

  unsigned AndMask = 0;
  unsigned OrMask = 0;
  unsigned XorMask = 0;

  for (size_t i = 0; i < Ctl.size(); ++i) {
    unsigned Mask = 1 << (BITMASK_WIDTH - 1 - i);
    switch(Ctl[i]) {
    default:
      Error(StrLoc, "invalid mask");
      return false;
    case '0':
      break;
    case '1':
      OrMask |= Mask;
      break;
    case 'p':
      AndMask |= Mask;
      break;
    case 'i':
      AndMask |= Mask;
      XorMask |= Mask;
      break;
    }
  }

  Imm = encodeBitmaskPerm(AndMask, OrMask, XorMask);
  return true;
}

bool
AMDGPUAsmParser::parseSwizzleOffset(int64_t &Imm) {

  SMLoc OffsetLoc = getLoc();

  if (!parseExpr(Imm, "a swizzle macro")) {
    return false;
  }
  if (!isUInt<16>(Imm)) {
    Error(OffsetLoc, "expected a 16-bit offset");
    return false;
  }
  return true;
}

bool
AMDGPUAsmParser::parseSwizzleMacro(int64_t &Imm) {
  using namespace llvm::AMDGPU::Swizzle;

  if (skipToken(AsmToken::LParen, "expected a left parentheses")) {

    SMLoc ModeLoc = getLoc();
    bool Ok = false;

    if (trySkipId(IdSymbolic[ID_QUAD_PERM])) {
      Ok = parseSwizzleQuadPerm(Imm);
    } else if (trySkipId(IdSymbolic[ID_BITMASK_PERM])) {
      Ok = parseSwizzleBitmaskPerm(Imm);
    } else if (trySkipId(IdSymbolic[ID_BROADCAST])) {
      Ok = parseSwizzleBroadcast(Imm);
    } else if (trySkipId(IdSymbolic[ID_SWAP])) {
      Ok = parseSwizzleSwap(Imm);
    } else if (trySkipId(IdSymbolic[ID_REVERSE])) {
      Ok = parseSwizzleReverse(Imm);
    } else {
      Error(ModeLoc, "expected a swizzle mode");
    }

    return Ok && skipToken(AsmToken::RParen, "expected a closing parentheses");
  }

  return false;
}

ParseStatus AMDGPUAsmParser::parseSwizzle(OperandVector &Operands) {
  SMLoc S = getLoc();
  int64_t Imm = 0;

  if (trySkipId("offset")) {

    bool Ok = false;
    if (skipToken(AsmToken::Colon, "expected a colon")) {
      if (trySkipId("swizzle")) {
        Ok = parseSwizzleMacro(Imm);
      } else {
        Ok = parseSwizzleOffset(Imm);
      }
    }

    Operands.push_back(AMDGPUOperand::CreateImm(this, Imm, S, AMDGPUOperand::ImmTySwizzle));

    return Ok ? ParseStatus::Success : ParseStatus::Failure;
  }
  return ParseStatus::NoMatch;
}

bool
AMDGPUOperand::isSwizzle() const {
  return isImmTy(ImmTySwizzle);
}

//===----------------------------------------------------------------------===//
// VGPR Index Mode
//===----------------------------------------------------------------------===//

int64_t AMDGPUAsmParser::parseGPRIdxMacro() {

  using namespace llvm::AMDGPU::VGPRIndexMode;

  if (trySkipToken(AsmToken::RParen)) {
    return OFF;
  }

  int64_t Imm = 0;

  while (true) {
    unsigned Mode = 0;
    SMLoc S = getLoc();

    for (unsigned ModeId = ID_MIN; ModeId <= ID_MAX; ++ModeId) {
      if (trySkipId(IdSymbolic[ModeId])) {
        Mode = 1 << ModeId;
        break;
      }
    }

    if (Mode == 0) {
      Error(S, (Imm == 0)?
               "expected a VGPR index mode or a closing parenthesis" :
               "expected a VGPR index mode");
      return UNDEF;
    }

    if (Imm & Mode) {
      Error(S, "duplicate VGPR index mode");
      return UNDEF;
    }
    Imm |= Mode;

    if (trySkipToken(AsmToken::RParen))
      break;
    if (!skipToken(AsmToken::Comma,
                   "expected a comma or a closing parenthesis"))
      return UNDEF;
  }

  return Imm;
}

ParseStatus AMDGPUAsmParser::parseGPRIdxMode(OperandVector &Operands) {

  using namespace llvm::AMDGPU::VGPRIndexMode;

  int64_t Imm = 0;
  SMLoc S = getLoc();

  if (trySkipId("gpr_idx", AsmToken::LParen)) {
    Imm = parseGPRIdxMacro();
    if (Imm == UNDEF)
      return ParseStatus::Failure;
  } else {
    if (getParser().parseAbsoluteExpression(Imm))
      return ParseStatus::Failure;
    if (Imm < 0 || !isUInt<4>(Imm))
      return Error(S, "invalid immediate: only 4-bit values are legal");
  }

  Operands.push_back(
      AMDGPUOperand::CreateImm(this, Imm, S, AMDGPUOperand::ImmTyGprIdxMode));
  return ParseStatus::Success;
}

bool AMDGPUOperand::isGPRIdxMode() const {
  return isImmTy(ImmTyGprIdxMode);
}

//===----------------------------------------------------------------------===//
// sopp branch targets
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseSOPPBrTarget(OperandVector &Operands) {

  // Make sure we are not parsing something
  // that looks like a label or an expression but is not.
  // This will improve error messages.
  if (isRegister() || isModifier())
    return ParseStatus::NoMatch;

  if (!parseExpr(Operands))
    return ParseStatus::Failure;

  AMDGPUOperand &Opr = ((AMDGPUOperand &)*Operands[Operands.size() - 1]);
  assert(Opr.isImm() || Opr.isExpr());
  SMLoc Loc = Opr.getStartLoc();

  // Currently we do not support arbitrary expressions as branch targets.
  // Only labels and absolute expressions are accepted.
  if (Opr.isExpr() && !Opr.isSymbolRefExpr()) {
    Error(Loc, "expected an absolute expression or a label");
  } else if (Opr.isImm() && !Opr.isS16Imm()) {
    Error(Loc, "expected a 16-bit signed jump offset");
  }

  return ParseStatus::Success;
}

//===----------------------------------------------------------------------===//
// Boolean holding registers
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseBoolReg(OperandVector &Operands) {
  return parseReg(Operands);
}

//===----------------------------------------------------------------------===//
// mubuf
//===----------------------------------------------------------------------===//

void AMDGPUAsmParser::cvtMubufImpl(MCInst &Inst,
                                   const OperandVector &Operands,
                                   bool IsAtomic) {
  OptionalImmIndexMap OptionalIdx;
  unsigned FirstOperandIdx = 1;
  bool IsAtomicReturn = false;

  if (IsAtomic) {
    IsAtomicReturn =  MII.get(Inst.getOpcode()).TSFlags &
                      SIInstrFlags::IsAtomicRet;
  }

  for (unsigned i = FirstOperandIdx, e = Operands.size(); i != e; ++i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);

    // Add the register arguments
    if (Op.isReg()) {
      Op.addRegOperands(Inst, 1);
      // Insert a tied src for atomic return dst.
      // This cannot be postponed as subsequent calls to
      // addImmOperands rely on correct number of MC operands.
      if (IsAtomicReturn && i == FirstOperandIdx)
        Op.addRegOperands(Inst, 1);
      continue;
    }

    // Handle the case where soffset is an immediate
    if (Op.isImm() && Op.getImmTy() == AMDGPUOperand::ImmTyNone) {
      Op.addImmOperands(Inst, 1);
      continue;
    }

    // Handle tokens like 'offen' which are sometimes hard-coded into the
    // asm string.  There are no MCInst operands for these.
    if (Op.isToken()) {
      continue;
    }
    assert(Op.isImm());

    // Handle optional arguments
    OptionalIdx[Op.getImmTy()] = i;
  }

  addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyOffset);
  addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyCPol, 0);
}

//===----------------------------------------------------------------------===//
// smrd
//===----------------------------------------------------------------------===//

bool AMDGPUOperand::isSMRDOffset8() const {
  return isImmLiteral() && isUInt<8>(getImm());
}

bool AMDGPUOperand::isSMEMOffset() const {
  // Offset range is checked later by validator.
  return isImmLiteral();
}

bool AMDGPUOperand::isSMRDLiteralOffset() const {
  // 32-bit literals are only supported on CI and we only want to use them
  // when the offset is > 8-bits.
  return isImmLiteral() && !isUInt<8>(getImm()) && isUInt<32>(getImm());
}

//===----------------------------------------------------------------------===//
// vop3
//===----------------------------------------------------------------------===//

static bool ConvertOmodMul(int64_t &Mul) {
  if (Mul != 1 && Mul != 2 && Mul != 4)
    return false;

  Mul >>= 1;
  return true;
}

static bool ConvertOmodDiv(int64_t &Div) {
  if (Div == 1) {
    Div = 0;
    return true;
  }

  if (Div == 2) {
    Div = 3;
    return true;
  }

  return false;
}

// For pre-gfx11 targets, both bound_ctrl:0 and bound_ctrl:1 are encoded as 1.
// This is intentional and ensures compatibility with sp3.
// See bug 35397 for details.
bool AMDGPUAsmParser::convertDppBoundCtrl(int64_t &BoundCtrl) {
  if (BoundCtrl == 0 || BoundCtrl == 1) {
    if (!isGFX11Plus())
      BoundCtrl = 1;
    return true;
  }
  return false;
}

void AMDGPUAsmParser::onBeginOfFile() {
  if (!getParser().getStreamer().getTargetStreamer() ||
      getSTI().getTargetTriple().getArch() == Triple::r600)
    return;

  if (!getTargetStreamer().getTargetID())
    getTargetStreamer().initializeTargetID(getSTI(),
                                           getSTI().getFeatureString());

  if (isHsaAbi(getSTI()))
    getTargetStreamer().EmitDirectiveAMDGCNTarget();
}

/// Parse AMDGPU specific expressions.
///
///  expr ::= or(expr, ...) |
///           max(expr, ...)
///
bool AMDGPUAsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  using AGVK = AMDGPUMCExpr::VariantKind;

  if (isToken(AsmToken::Identifier)) {
    StringRef TokenId = getTokenStr();
    AGVK VK = StringSwitch<AGVK>(TokenId)
                  .Case("max", AGVK::AGVK_Max)
                  .Case("or", AGVK::AGVK_Or)
                  .Case("extrasgprs", AGVK::AGVK_ExtraSGPRs)
                  .Case("totalnumvgprs", AGVK::AGVK_TotalNumVGPRs)
                  .Case("alignto", AGVK::AGVK_AlignTo)
                  .Case("occupancy", AGVK::AGVK_Occupancy)
                  .Default(AGVK::AGVK_None);

    if (VK != AGVK::AGVK_None && peekToken().is(AsmToken::LParen)) {
      SmallVector<const MCExpr *, 4> Exprs;
      uint64_t CommaCount = 0;
      lex(); // Eat Arg ('or', 'max', 'occupancy', etc.)
      lex(); // Eat '('
      while (true) {
        if (trySkipToken(AsmToken::RParen)) {
          if (Exprs.empty()) {
            Error(getToken().getLoc(),
                  "empty " + Twine(TokenId) + " expression");
            return true;
          }
          if (CommaCount + 1 != Exprs.size()) {
            Error(getToken().getLoc(),
                  "mismatch of commas in " + Twine(TokenId) + " expression");
            return true;
          }
          Res = AMDGPUMCExpr::create(VK, Exprs, getContext());
          return false;
        }
        const MCExpr *Expr;
        if (getParser().parseExpression(Expr, EndLoc))
          return true;
        Exprs.push_back(Expr);
        bool LastTokenWasComma = trySkipToken(AsmToken::Comma);
        if (LastTokenWasComma)
          CommaCount++;
        if (!LastTokenWasComma && !isToken(AsmToken::RParen)) {
          Error(getToken().getLoc(),
                "unexpected token in " + Twine(TokenId) + " expression");
          return true;
        }
      }
    }
  }
  return getParser().parsePrimaryExpr(Res, EndLoc, nullptr);
}

ParseStatus AMDGPUAsmParser::parseOModSI(OperandVector &Operands) {
  StringRef Name = getTokenStr();
  if (Name == "mul") {
    return parseIntWithPrefix("mul", Operands,
                              AMDGPUOperand::ImmTyOModSI, ConvertOmodMul);
  }

  if (Name == "div") {
    return parseIntWithPrefix("div", Operands,
                              AMDGPUOperand::ImmTyOModSI, ConvertOmodDiv);
  }

  return ParseStatus::NoMatch;
}

// Determines which bit DST_OP_SEL occupies in the op_sel operand according to
// the number of src operands present, then copies that bit into src0_modifiers.
static void cvtVOP3DstOpSelOnly(MCInst &Inst, const MCRegisterInfo &MRI) {
  int Opc = Inst.getOpcode();
  int OpSelIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel);
  if (OpSelIdx == -1)
    return;

  int SrcNum;
  const int Ops[] = { AMDGPU::OpName::src0,
                      AMDGPU::OpName::src1,
                      AMDGPU::OpName::src2 };
  for (SrcNum = 0; SrcNum < 3 && AMDGPU::hasNamedOperand(Opc, Ops[SrcNum]);
       ++SrcNum)
    ;
  assert(SrcNum > 0);

  unsigned OpSel = Inst.getOperand(OpSelIdx).getImm();

  int DstIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst);
  if (DstIdx == -1)
    return;

  const MCOperand &DstOp = Inst.getOperand(DstIdx);
  int ModIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0_modifiers);
  uint32_t ModVal = Inst.getOperand(ModIdx).getImm();
  if (DstOp.isReg() &&
      MRI.getRegClass(AMDGPU::VGPR_16RegClassID).contains(DstOp.getReg())) {
    if (AMDGPU::isHi(DstOp.getReg(), MRI))
      ModVal |= SISrcMods::DST_OP_SEL;
  } else {
    if ((OpSel & (1 << SrcNum)) != 0)
      ModVal |= SISrcMods::DST_OP_SEL;
  }
  Inst.getOperand(ModIdx).setImm(ModVal);
}

void AMDGPUAsmParser::cvtVOP3OpSel(MCInst &Inst,
                                   const OperandVector &Operands) {
  cvtVOP3P(Inst, Operands);
  cvtVOP3DstOpSelOnly(Inst, *getMRI());
}

void AMDGPUAsmParser::cvtVOP3OpSel(MCInst &Inst, const OperandVector &Operands,
                                   OptionalImmIndexMap &OptionalIdx) {
  cvtVOP3P(Inst, Operands, OptionalIdx);
  cvtVOP3DstOpSelOnly(Inst, *getMRI());
}

static bool isRegOrImmWithInputMods(const MCInstrDesc &Desc, unsigned OpNum) {
  return
      // 1. This operand is input modifiers
      Desc.operands()[OpNum].OperandType == AMDGPU::OPERAND_INPUT_MODS
      // 2. This is not last operand
      && Desc.NumOperands > (OpNum + 1)
      // 3. Next operand is register class
      && Desc.operands()[OpNum + 1].RegClass != -1
      // 4. Next register is not tied to any other operand
      && Desc.getOperandConstraint(OpNum + 1,
                                   MCOI::OperandConstraint::TIED_TO) == -1;
}

void AMDGPUAsmParser::cvtVOP3Interp(MCInst &Inst, const OperandVector &Operands)
{
  OptionalImmIndexMap OptionalIdx;
  unsigned Opc = Inst.getOpcode();

  unsigned I = 1;
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned J = 0; J < Desc.getNumDefs(); ++J) {
    ((AMDGPUOperand &)*Operands[I++]).addRegOperands(Inst, 1);
  }

  for (unsigned E = Operands.size(); I != E; ++I) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[I]);
    if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
      Op.addRegOrImmWithFPInputModsOperands(Inst, 2);
    } else if (Op.isInterpSlot() || Op.isInterpAttr() ||
               Op.isInterpAttrChan()) {
      Inst.addOperand(MCOperand::createImm(Op.getImm()));
    } else if (Op.isImmModifier()) {
      OptionalIdx[Op.getImmTy()] = I;
    } else {
      llvm_unreachable("unhandled operand type");
    }
  }

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::high))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyHigh);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::clamp))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyClamp);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::omod))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyOModSI);
}

void AMDGPUAsmParser::cvtVINTERP(MCInst &Inst, const OperandVector &Operands)
{
  OptionalImmIndexMap OptionalIdx;
  unsigned Opc = Inst.getOpcode();

  unsigned I = 1;
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned J = 0; J < Desc.getNumDefs(); ++J) {
    ((AMDGPUOperand &)*Operands[I++]).addRegOperands(Inst, 1);
  }

  for (unsigned E = Operands.size(); I != E; ++I) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[I]);
    if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
      Op.addRegOrImmWithFPInputModsOperands(Inst, 2);
    } else if (Op.isImmModifier()) {
      OptionalIdx[Op.getImmTy()] = I;
    } else {
      llvm_unreachable("unhandled operand type");
    }
  }

  addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyClamp);

  int OpSelIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel);
  if (OpSelIdx != -1)
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyOpSel);

  addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyWaitEXP);

  if (OpSelIdx == -1)
    return;

  const int Ops[] = { AMDGPU::OpName::src0,
                      AMDGPU::OpName::src1,
                      AMDGPU::OpName::src2 };
  const int ModOps[] = { AMDGPU::OpName::src0_modifiers,
                         AMDGPU::OpName::src1_modifiers,
                         AMDGPU::OpName::src2_modifiers };

  unsigned OpSel = Inst.getOperand(OpSelIdx).getImm();

  for (int J = 0; J < 3; ++J) {
    int OpIdx = AMDGPU::getNamedOperandIdx(Opc, Ops[J]);
    if (OpIdx == -1)
      break;

    int ModIdx = AMDGPU::getNamedOperandIdx(Opc, ModOps[J]);
    uint32_t ModVal = Inst.getOperand(ModIdx).getImm();

    if ((OpSel & (1 << J)) != 0)
      ModVal |= SISrcMods::OP_SEL_0;
    if (ModOps[J] == AMDGPU::OpName::src0_modifiers &&
        (OpSel & (1 << 3)) != 0)
      ModVal |= SISrcMods::DST_OP_SEL;

    Inst.getOperand(ModIdx).setImm(ModVal);
  }
}

void AMDGPUAsmParser::cvtVOP3(MCInst &Inst, const OperandVector &Operands,
                              OptionalImmIndexMap &OptionalIdx) {
  unsigned Opc = Inst.getOpcode();

  unsigned I = 1;
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned J = 0; J < Desc.getNumDefs(); ++J) {
    ((AMDGPUOperand &)*Operands[I++]).addRegOperands(Inst, 1);
  }

  for (unsigned E = Operands.size(); I != E; ++I) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[I]);
    if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
      Op.addRegOrImmWithFPInputModsOperands(Inst, 2);
    } else if (Op.isImmModifier()) {
      OptionalIdx[Op.getImmTy()] = I;
    } else if (Op.isRegOrImm()) {
      Op.addRegOrImmOperands(Inst, 1);
    } else {
      llvm_unreachable("unhandled operand type");
    }
  }

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::byte_sel)) {
    if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::vdst_in))
      Inst.addOperand(Inst.getOperand(0));
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyByteSel);
  }

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::clamp))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyClamp);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::omod))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyOModSI);

  // Special case v_mac_{f16, f32} and v_fmac_{f16, f32} (gfx906/gfx10+):
  // it has src2 register operand that is tied to dst operand
  // we don't allow modifiers for this operand in assembler so src2_modifiers
  // should be 0.
  if (isMAC(Opc)) {
    auto it = Inst.begin();
    std::advance(it, AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2_modifiers));
    it = Inst.insert(it, MCOperand::createImm(0)); // no modifiers for src2
    ++it;
    // Copy the operand to ensure it's not invalidated when Inst grows.
    Inst.insert(it, MCOperand(Inst.getOperand(0))); // src2 = dst
  }
}

void AMDGPUAsmParser::cvtVOP3(MCInst &Inst, const OperandVector &Operands) {
  OptionalImmIndexMap OptionalIdx;
  cvtVOP3(Inst, Operands, OptionalIdx);
}

void AMDGPUAsmParser::cvtVOP3P(MCInst &Inst, const OperandVector &Operands,
                               OptionalImmIndexMap &OptIdx) {
  const int Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Opc);

  const bool IsPacked = (Desc.TSFlags & SIInstrFlags::IsPacked) != 0;

  if (Opc == AMDGPU::V_CVT_SR_BF8_F32_vi ||
      Opc == AMDGPU::V_CVT_SR_FP8_F32_vi ||
      Opc == AMDGPU::V_CVT_SR_BF8_F32_gfx12_e64_gfx12 ||
      Opc == AMDGPU::V_CVT_SR_FP8_F32_gfx12_e64_gfx12) {
    Inst.addOperand(MCOperand::createImm(0)); // Placeholder for src2_mods
    Inst.addOperand(Inst.getOperand(0));
  }

  // Adding vdst_in operand is already covered for these DPP instructions in
  // cvtVOP3DPP.
  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::vdst_in) &&
      !(Opc == AMDGPU::V_CVT_PK_BF8_F32_e64_dpp_gfx12 ||
        Opc == AMDGPU::V_CVT_PK_FP8_F32_e64_dpp_gfx12 ||
        Opc == AMDGPU::V_CVT_PK_BF8_F32_e64_dpp8_gfx12 ||
        Opc == AMDGPU::V_CVT_PK_FP8_F32_e64_dpp8_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_FP8_F32_gfx12_e64_dpp_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_FP8_F32_gfx12_e64_dpp8_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_BF8_F32_gfx12_e64_dpp_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_BF8_F32_gfx12_e64_dpp8_gfx12)) {
    assert(!IsPacked);
    Inst.addOperand(Inst.getOperand(0));
  }

  // FIXME: This is messy. Parse the modifiers as if it was a normal VOP3
  // instruction, and then figure out where to actually put the modifiers

  int OpSelIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel);
  if (OpSelIdx != -1) {
    addOptionalImmOperand(Inst, Operands, OptIdx, AMDGPUOperand::ImmTyOpSel);
  }

  int OpSelHiIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::op_sel_hi);
  if (OpSelHiIdx != -1) {
    int DefaultVal = IsPacked ? -1 : 0;
    addOptionalImmOperand(Inst, Operands, OptIdx, AMDGPUOperand::ImmTyOpSelHi,
                          DefaultVal);
  }

  int NegLoIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::neg_lo);
  if (NegLoIdx != -1)
    addOptionalImmOperand(Inst, Operands, OptIdx, AMDGPUOperand::ImmTyNegLo);

  int NegHiIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::neg_hi);
  if (NegHiIdx != -1)
    addOptionalImmOperand(Inst, Operands, OptIdx, AMDGPUOperand::ImmTyNegHi);

  const int Ops[] = { AMDGPU::OpName::src0,
                      AMDGPU::OpName::src1,
                      AMDGPU::OpName::src2 };
  const int ModOps[] = { AMDGPU::OpName::src0_modifiers,
                         AMDGPU::OpName::src1_modifiers,
                         AMDGPU::OpName::src2_modifiers };

  unsigned OpSel = 0;
  unsigned OpSelHi = 0;
  unsigned NegLo = 0;
  unsigned NegHi = 0;

  if (OpSelIdx != -1)
    OpSel = Inst.getOperand(OpSelIdx).getImm();

  if (OpSelHiIdx != -1)
    OpSelHi = Inst.getOperand(OpSelHiIdx).getImm();

  if (NegLoIdx != -1)
    NegLo = Inst.getOperand(NegLoIdx).getImm();

  if (NegHiIdx != -1)
    NegHi = Inst.getOperand(NegHiIdx).getImm();

  for (int J = 0; J < 3; ++J) {
    int OpIdx = AMDGPU::getNamedOperandIdx(Opc, Ops[J]);
    if (OpIdx == -1)
      break;

    int ModIdx = AMDGPU::getNamedOperandIdx(Opc, ModOps[J]);

    if (ModIdx == -1)
      continue;

    uint32_t ModVal = 0;

    const MCOperand &SrcOp = Inst.getOperand(OpIdx);
    if (SrcOp.isReg() && getMRI()
                             ->getRegClass(AMDGPU::VGPR_16RegClassID)
                             .contains(SrcOp.getReg())) {
      bool VGPRSuffixIsHi = AMDGPU::isHi(SrcOp.getReg(), *getMRI());
      if (VGPRSuffixIsHi)
        ModVal |= SISrcMods::OP_SEL_0;
    } else {
      if ((OpSel & (1 << J)) != 0)
        ModVal |= SISrcMods::OP_SEL_0;
    }

    if ((OpSelHi & (1 << J)) != 0)
      ModVal |= SISrcMods::OP_SEL_1;

    if ((NegLo & (1 << J)) != 0)
      ModVal |= SISrcMods::NEG;

    if ((NegHi & (1 << J)) != 0)
      ModVal |= SISrcMods::NEG_HI;

    Inst.getOperand(ModIdx).setImm(Inst.getOperand(ModIdx).getImm() | ModVal);
  }
}

void AMDGPUAsmParser::cvtVOP3P(MCInst &Inst, const OperandVector &Operands) {
  OptionalImmIndexMap OptIdx;
  cvtVOP3(Inst, Operands, OptIdx);
  cvtVOP3P(Inst, Operands, OptIdx);
}

static void addSrcModifiersAndSrc(MCInst &Inst, const OperandVector &Operands,
                                  unsigned i, unsigned Opc, unsigned OpName) {
  if (AMDGPU::getNamedOperandIdx(Opc, OpName) != -1)
    ((AMDGPUOperand &)*Operands[i]).addRegOrImmWithFPInputModsOperands(Inst, 2);
  else
    ((AMDGPUOperand &)*Operands[i]).addRegOperands(Inst, 1);
}

void AMDGPUAsmParser::cvtSWMMAC(MCInst &Inst, const OperandVector &Operands) {
  unsigned Opc = Inst.getOpcode();

  ((AMDGPUOperand &)*Operands[1]).addRegOperands(Inst, 1);
  addSrcModifiersAndSrc(Inst, Operands, 2, Opc, AMDGPU::OpName::src0_modifiers);
  addSrcModifiersAndSrc(Inst, Operands, 3, Opc, AMDGPU::OpName::src1_modifiers);
  ((AMDGPUOperand &)*Operands[1]).addRegOperands(Inst, 1); // srcTiedDef
  ((AMDGPUOperand &)*Operands[4]).addRegOperands(Inst, 1); // src2

  OptionalImmIndexMap OptIdx;
  for (unsigned i = 5; i < Operands.size(); ++i) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[i]);
    OptIdx[Op.getImmTy()] = i;
  }

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::index_key_8bit))
    addOptionalImmOperand(Inst, Operands, OptIdx,
                          AMDGPUOperand::ImmTyIndexKey8bit);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::index_key_16bit))
    addOptionalImmOperand(Inst, Operands, OptIdx,
                          AMDGPUOperand::ImmTyIndexKey16bit);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::clamp))
    addOptionalImmOperand(Inst, Operands, OptIdx, AMDGPUOperand::ImmTyClamp);

  cvtVOP3P(Inst, Operands, OptIdx);
}

//===----------------------------------------------------------------------===//
// VOPD
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseVOPD(OperandVector &Operands) {
  if (!hasVOPD(getSTI()))
    return ParseStatus::NoMatch;

  if (isToken(AsmToken::Colon) && peekToken(false).is(AsmToken::Colon)) {
    SMLoc S = getLoc();
    lex();
    lex();
    Operands.push_back(AMDGPUOperand::CreateToken(this, "::", S));
    SMLoc OpYLoc = getLoc();
    StringRef OpYName;
    if (isToken(AsmToken::Identifier) && !Parser.parseIdentifier(OpYName)) {
      Operands.push_back(AMDGPUOperand::CreateToken(this, OpYName, OpYLoc));
      return ParseStatus::Success;
    }
    return Error(OpYLoc, "expected a VOPDY instruction after ::");
  }
  return ParseStatus::NoMatch;
}

// Create VOPD MCInst operands using parsed assembler operands.
void AMDGPUAsmParser::cvtVOPD(MCInst &Inst, const OperandVector &Operands) {
  auto addOp = [&](uint16_t ParsedOprIdx) { // NOLINT:function pointer
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[ParsedOprIdx]);
    if (Op.isReg()) {
      Op.addRegOperands(Inst, 1);
      return;
    }
    if (Op.isImm()) {
      Op.addImmOperands(Inst, 1);
      return;
    }
    llvm_unreachable("Unhandled operand type in cvtVOPD");
  };

  const auto &InstInfo = getVOPDInstInfo(Inst.getOpcode(), &MII);

  // MCInst operands are ordered as follows:
  //   dstX, dstY, src0X [, other OpX operands], src0Y [, other OpY operands]

  for (auto CompIdx : VOPD::COMPONENTS) {
    addOp(InstInfo[CompIdx].getIndexOfDstInParsedOperands());
  }

  for (auto CompIdx : VOPD::COMPONENTS) {
    const auto &CInfo = InstInfo[CompIdx];
    auto CompSrcOperandsNum = InstInfo[CompIdx].getCompParsedSrcOperandsNum();
    for (unsigned CompSrcIdx = 0; CompSrcIdx < CompSrcOperandsNum; ++CompSrcIdx)
      addOp(CInfo.getIndexOfSrcInParsedOperands(CompSrcIdx));
    if (CInfo.hasSrc2Acc())
      addOp(CInfo.getIndexOfDstInParsedOperands());
  }
}

//===----------------------------------------------------------------------===//
// dpp
//===----------------------------------------------------------------------===//

bool AMDGPUOperand::isDPP8() const {
  return isImmTy(ImmTyDPP8);
}

bool AMDGPUOperand::isDPPCtrl() const {
  using namespace AMDGPU::DPP;

  bool result = isImm() && getImmTy() == ImmTyDppCtrl && isUInt<9>(getImm());
  if (result) {
    int64_t Imm = getImm();
    return (Imm >= DppCtrl::QUAD_PERM_FIRST && Imm <= DppCtrl::QUAD_PERM_LAST) ||
           (Imm >= DppCtrl::ROW_SHL_FIRST && Imm <= DppCtrl::ROW_SHL_LAST) ||
           (Imm >= DppCtrl::ROW_SHR_FIRST && Imm <= DppCtrl::ROW_SHR_LAST) ||
           (Imm >= DppCtrl::ROW_ROR_FIRST && Imm <= DppCtrl::ROW_ROR_LAST) ||
           (Imm == DppCtrl::WAVE_SHL1) ||
           (Imm == DppCtrl::WAVE_ROL1) ||
           (Imm == DppCtrl::WAVE_SHR1) ||
           (Imm == DppCtrl::WAVE_ROR1) ||
           (Imm == DppCtrl::ROW_MIRROR) ||
           (Imm == DppCtrl::ROW_HALF_MIRROR) ||
           (Imm == DppCtrl::BCAST15) ||
           (Imm == DppCtrl::BCAST31) ||
           (Imm >= DppCtrl::ROW_SHARE_FIRST && Imm <= DppCtrl::ROW_SHARE_LAST) ||
           (Imm >= DppCtrl::ROW_XMASK_FIRST && Imm <= DppCtrl::ROW_XMASK_LAST);
  }
  return false;
}

//===----------------------------------------------------------------------===//
// mAI
//===----------------------------------------------------------------------===//

bool AMDGPUOperand::isBLGP() const {
  return isImm() && getImmTy() == ImmTyBLGP && isUInt<3>(getImm());
}

bool AMDGPUOperand::isS16Imm() const {
  return isImmLiteral() && (isInt<16>(getImm()) || isUInt<16>(getImm()));
}

bool AMDGPUOperand::isU16Imm() const {
  return isImmLiteral() && isUInt<16>(getImm());
}

//===----------------------------------------------------------------------===//
// dim
//===----------------------------------------------------------------------===//

bool AMDGPUAsmParser::parseDimId(unsigned &Encoding) {
  // We want to allow "dim:1D" etc.,
  // but the initial 1 is tokenized as an integer.
  std::string Token;
  if (isToken(AsmToken::Integer)) {
    SMLoc Loc = getToken().getEndLoc();
    Token = std::string(getTokenStr());
    lex();
    if (getLoc() != Loc)
      return false;
  }

  StringRef Suffix;
  if (!parseId(Suffix))
    return false;
  Token += Suffix;

  StringRef DimId = Token;
  if (DimId.starts_with("SQ_RSRC_IMG_"))
    DimId = DimId.drop_front(12);

  const AMDGPU::MIMGDimInfo *DimInfo = AMDGPU::getMIMGDimInfoByAsmSuffix(DimId);
  if (!DimInfo)
    return false;

  Encoding = DimInfo->Encoding;
  return true;
}

ParseStatus AMDGPUAsmParser::parseDim(OperandVector &Operands) {
  if (!isGFX10Plus())
    return ParseStatus::NoMatch;

  SMLoc S = getLoc();

  if (!trySkipId("dim", AsmToken::Colon))
    return ParseStatus::NoMatch;

  unsigned Encoding;
  SMLoc Loc = getLoc();
  if (!parseDimId(Encoding))
    return Error(Loc, "invalid dim value");

  Operands.push_back(AMDGPUOperand::CreateImm(this, Encoding, S,
                                              AMDGPUOperand::ImmTyDim));
  return ParseStatus::Success;
}

//===----------------------------------------------------------------------===//
// dpp
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseDPP8(OperandVector &Operands) {
  SMLoc S = getLoc();

  if (!isGFX10Plus() || !trySkipId("dpp8", AsmToken::Colon))
    return ParseStatus::NoMatch;

  // dpp8:[%d,%d,%d,%d,%d,%d,%d,%d]

  int64_t Sels[8];

  if (!skipToken(AsmToken::LBrac, "expected an opening square bracket"))
    return ParseStatus::Failure;

  for (size_t i = 0; i < 8; ++i) {
    if (i > 0 && !skipToken(AsmToken::Comma, "expected a comma"))
      return ParseStatus::Failure;

    SMLoc Loc = getLoc();
    if (getParser().parseAbsoluteExpression(Sels[i]))
      return ParseStatus::Failure;
    if (0 > Sels[i] || 7 < Sels[i])
      return Error(Loc, "expected a 3-bit value");
  }

  if (!skipToken(AsmToken::RBrac, "expected a closing square bracket"))
    return ParseStatus::Failure;

  unsigned DPP8 = 0;
  for (size_t i = 0; i < 8; ++i)
    DPP8 |= (Sels[i] << (i * 3));

  Operands.push_back(AMDGPUOperand::CreateImm(this, DPP8, S, AMDGPUOperand::ImmTyDPP8));
  return ParseStatus::Success;
}

bool
AMDGPUAsmParser::isSupportedDPPCtrl(StringRef Ctrl,
                                    const OperandVector &Operands) {
  if (Ctrl == "row_newbcast")
    return isGFX90A();

  if (Ctrl == "row_share" ||
      Ctrl == "row_xmask")
    return isGFX10Plus();

  if (Ctrl == "wave_shl" ||
      Ctrl == "wave_shr" ||
      Ctrl == "wave_rol" ||
      Ctrl == "wave_ror" ||
      Ctrl == "row_bcast")
    return isVI() || isGFX9();

  return Ctrl == "row_mirror" ||
         Ctrl == "row_half_mirror" ||
         Ctrl == "quad_perm" ||
         Ctrl == "row_shl" ||
         Ctrl == "row_shr" ||
         Ctrl == "row_ror";
}

int64_t
AMDGPUAsmParser::parseDPPCtrlPerm() {
  // quad_perm:[%d,%d,%d,%d]

  if (!skipToken(AsmToken::LBrac, "expected an opening square bracket"))
    return -1;

  int64_t Val = 0;
  for (int i = 0; i < 4; ++i) {
    if (i > 0 && !skipToken(AsmToken::Comma, "expected a comma"))
      return -1;

    int64_t Temp;
    SMLoc Loc = getLoc();
    if (getParser().parseAbsoluteExpression(Temp))
      return -1;
    if (Temp < 0 || Temp > 3) {
      Error(Loc, "expected a 2-bit value");
      return -1;
    }

    Val += (Temp << i * 2);
  }

  if (!skipToken(AsmToken::RBrac, "expected a closing square bracket"))
    return -1;

  return Val;
}

int64_t
AMDGPUAsmParser::parseDPPCtrlSel(StringRef Ctrl) {
  using namespace AMDGPU::DPP;

  // sel:%d

  int64_t Val;
  SMLoc Loc = getLoc();

  if (getParser().parseAbsoluteExpression(Val))
    return -1;

  struct DppCtrlCheck {
    int64_t Ctrl;
    int Lo;
    int Hi;
  };

  DppCtrlCheck Check = StringSwitch<DppCtrlCheck>(Ctrl)
    .Case("wave_shl",  {DppCtrl::WAVE_SHL1,       1,  1})
    .Case("wave_rol",  {DppCtrl::WAVE_ROL1,       1,  1})
    .Case("wave_shr",  {DppCtrl::WAVE_SHR1,       1,  1})
    .Case("wave_ror",  {DppCtrl::WAVE_ROR1,       1,  1})
    .Case("row_shl",   {DppCtrl::ROW_SHL0,        1, 15})
    .Case("row_shr",   {DppCtrl::ROW_SHR0,        1, 15})
    .Case("row_ror",   {DppCtrl::ROW_ROR0,        1, 15})
    .Case("row_share", {DppCtrl::ROW_SHARE_FIRST, 0, 15})
    .Case("row_xmask", {DppCtrl::ROW_XMASK_FIRST, 0, 15})
    .Case("row_newbcast", {DppCtrl::ROW_NEWBCAST_FIRST, 0, 15})
    .Default({-1, 0, 0});

  bool Valid;
  if (Check.Ctrl == -1) {
    Valid = (Ctrl == "row_bcast" && (Val == 15 || Val == 31));
    Val = (Val == 15)? DppCtrl::BCAST15 : DppCtrl::BCAST31;
  } else {
    Valid = Check.Lo <= Val && Val <= Check.Hi;
    Val = (Check.Lo == Check.Hi) ? Check.Ctrl : (Check.Ctrl | Val);
  }

  if (!Valid) {
    Error(Loc, Twine("invalid ", Ctrl) + Twine(" value"));
    return -1;
  }

  return Val;
}

ParseStatus AMDGPUAsmParser::parseDPPCtrl(OperandVector &Operands) {
  using namespace AMDGPU::DPP;

  if (!isToken(AsmToken::Identifier) ||
      !isSupportedDPPCtrl(getTokenStr(), Operands))
    return ParseStatus::NoMatch;

  SMLoc S = getLoc();
  int64_t Val = -1;
  StringRef Ctrl;

  parseId(Ctrl);

  if (Ctrl == "row_mirror") {
    Val = DppCtrl::ROW_MIRROR;
  } else if (Ctrl == "row_half_mirror") {
    Val = DppCtrl::ROW_HALF_MIRROR;
  } else {
    if (skipToken(AsmToken::Colon, "expected a colon")) {
      if (Ctrl == "quad_perm") {
        Val = parseDPPCtrlPerm();
      } else {
        Val = parseDPPCtrlSel(Ctrl);
      }
    }
  }

  if (Val == -1)
    return ParseStatus::Failure;

  Operands.push_back(
    AMDGPUOperand::CreateImm(this, Val, S, AMDGPUOperand::ImmTyDppCtrl));
  return ParseStatus::Success;
}

void AMDGPUAsmParser::cvtVOP3DPP(MCInst &Inst, const OperandVector &Operands,
                                 bool IsDPP8) {
  OptionalImmIndexMap OptionalIdx;
  unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());

  // MAC instructions are special because they have 'old'
  // operand which is not tied to dst (but assumed to be).
  // They also have dummy unused src2_modifiers.
  int OldIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::old);
  int Src2ModIdx =
      AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2_modifiers);
  bool IsMAC = OldIdx != -1 && Src2ModIdx != -1 &&
               Desc.getOperandConstraint(OldIdx, MCOI::TIED_TO) == -1;

  unsigned I = 1;
  for (unsigned J = 0; J < Desc.getNumDefs(); ++J) {
    ((AMDGPUOperand &)*Operands[I++]).addRegOperands(Inst, 1);
  }

  int Fi = 0;
  for (unsigned E = Operands.size(); I != E; ++I) {

    if (IsMAC) {
      int NumOperands = Inst.getNumOperands();
      if (OldIdx == NumOperands) {
        // Handle old operand
        constexpr int DST_IDX = 0;
        Inst.addOperand(Inst.getOperand(DST_IDX));
      } else if (Src2ModIdx == NumOperands) {
        // Add unused dummy src2_modifiers
        Inst.addOperand(MCOperand::createImm(0));
      }
    }

    int VdstInIdx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::vdst_in);
    if (VdstInIdx == static_cast<int>(Inst.getNumOperands())) {
      Inst.addOperand(Inst.getOperand(0));
    }

    bool IsVOP3CvtSrDpp =
        Opc == AMDGPU::V_CVT_SR_BF8_F32_gfx12_e64_dpp8_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_FP8_F32_gfx12_e64_dpp8_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_BF8_F32_gfx12_e64_dpp_gfx12 ||
        Opc == AMDGPU::V_CVT_SR_FP8_F32_gfx12_e64_dpp_gfx12;
    if (IsVOP3CvtSrDpp) {
      if (Src2ModIdx == static_cast<int>(Inst.getNumOperands())) {
        Inst.addOperand(MCOperand::createImm(0));
        Inst.addOperand(MCOperand::createReg(0));
      }
    }

    auto TiedTo = Desc.getOperandConstraint(Inst.getNumOperands(),
                                            MCOI::TIED_TO);
    if (TiedTo != -1) {
      assert((unsigned)TiedTo < Inst.getNumOperands());
      // handle tied old or src2 for MAC instructions
      Inst.addOperand(Inst.getOperand(TiedTo));
    }
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[I]);
    // Add the register arguments
    if (IsDPP8 && Op.isDppFI()) {
      Fi = Op.getImm();
    } else if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
      Op.addRegOrImmWithFPInputModsOperands(Inst, 2);
    } else if (Op.isReg()) {
      Op.addRegOperands(Inst, 1);
    } else if (Op.isImm() &&
               Desc.operands()[Inst.getNumOperands()].RegClass != -1) {
      assert(!Op.IsImmKindLiteral() && "Cannot use literal with DPP");
      Op.addImmOperands(Inst, 1);
    } else if (Op.isImm()) {
      OptionalIdx[Op.getImmTy()] = I;
    } else {
      llvm_unreachable("unhandled operand type");
    }
  }

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::byte_sel))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyByteSel);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::clamp))
    addOptionalImmOperand(Inst, Operands, OptionalIdx,
                          AMDGPUOperand::ImmTyClamp);

  if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::omod))
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyOModSI);

  if (Desc.TSFlags & SIInstrFlags::VOP3P)
    cvtVOP3P(Inst, Operands, OptionalIdx);
  else if (Desc.TSFlags & SIInstrFlags::VOP3)
    cvtVOP3OpSel(Inst, Operands, OptionalIdx);
  else if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::op_sel)) {
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyOpSel);
  }

  if (IsDPP8) {
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDPP8);
    using namespace llvm::AMDGPU::DPP;
    Inst.addOperand(MCOperand::createImm(Fi? DPP8_FI_1 : DPP8_FI_0));
  } else {
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppCtrl, 0xe4);
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppRowMask, 0xf);
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppBankMask, 0xf);
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppBoundCtrl);

    if (AMDGPU::hasNamedOperand(Inst.getOpcode(), AMDGPU::OpName::fi))
      addOptionalImmOperand(Inst, Operands, OptionalIdx,
                            AMDGPUOperand::ImmTyDppFI);
  }
}

void AMDGPUAsmParser::cvtDPP(MCInst &Inst, const OperandVector &Operands, bool IsDPP8) {
  OptionalImmIndexMap OptionalIdx;

  unsigned I = 1;
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned J = 0; J < Desc.getNumDefs(); ++J) {
    ((AMDGPUOperand &)*Operands[I++]).addRegOperands(Inst, 1);
  }

  int Fi = 0;
  for (unsigned E = Operands.size(); I != E; ++I) {
    auto TiedTo = Desc.getOperandConstraint(Inst.getNumOperands(),
                                            MCOI::TIED_TO);
    if (TiedTo != -1) {
      assert((unsigned)TiedTo < Inst.getNumOperands());
      // handle tied old or src2 for MAC instructions
      Inst.addOperand(Inst.getOperand(TiedTo));
    }
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[I]);
    // Add the register arguments
    if (Op.isReg() && validateVccOperand(Op.getReg())) {
      // VOP2b (v_add_u32, v_sub_u32 ...) dpp use "vcc" token.
      // Skip it.
      continue;
    }

    if (IsDPP8) {
      if (Op.isDPP8()) {
        Op.addImmOperands(Inst, 1);
      } else if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
        Op.addRegWithFPInputModsOperands(Inst, 2);
      } else if (Op.isDppFI()) {
        Fi = Op.getImm();
      } else if (Op.isReg()) {
        Op.addRegOperands(Inst, 1);
      } else {
        llvm_unreachable("Invalid operand type");
      }
    } else {
      if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
        Op.addRegWithFPInputModsOperands(Inst, 2);
      } else if (Op.isReg()) {
        Op.addRegOperands(Inst, 1);
      } else if (Op.isDPPCtrl()) {
        Op.addImmOperands(Inst, 1);
      } else if (Op.isImm()) {
        // Handle optional arguments
        OptionalIdx[Op.getImmTy()] = I;
      } else {
        llvm_unreachable("Invalid operand type");
      }
    }
  }

  if (IsDPP8) {
    using namespace llvm::AMDGPU::DPP;
    Inst.addOperand(MCOperand::createImm(Fi? DPP8_FI_1 : DPP8_FI_0));
  } else {
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppRowMask, 0xf);
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppBankMask, 0xf);
    addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyDppBoundCtrl);
    if (AMDGPU::hasNamedOperand(Inst.getOpcode(), AMDGPU::OpName::fi)) {
      addOptionalImmOperand(Inst, Operands, OptionalIdx,
                            AMDGPUOperand::ImmTyDppFI);
    }
  }
}

//===----------------------------------------------------------------------===//
// sdwa
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseSDWASel(OperandVector &Operands,
                                          StringRef Prefix,
                                          AMDGPUOperand::ImmTy Type) {
  using namespace llvm::AMDGPU::SDWA;

  SMLoc S = getLoc();
  StringRef Value;

  SMLoc StringLoc;
  ParseStatus Res = parseStringWithPrefix(Prefix, Value, StringLoc);
  if (!Res.isSuccess())
    return Res;

  int64_t Int;
  Int = StringSwitch<int64_t>(Value)
        .Case("BYTE_0", SdwaSel::BYTE_0)
        .Case("BYTE_1", SdwaSel::BYTE_1)
        .Case("BYTE_2", SdwaSel::BYTE_2)
        .Case("BYTE_3", SdwaSel::BYTE_3)
        .Case("WORD_0", SdwaSel::WORD_0)
        .Case("WORD_1", SdwaSel::WORD_1)
        .Case("DWORD", SdwaSel::DWORD)
        .Default(0xffffffff);

  if (Int == 0xffffffff)
    return Error(StringLoc, "invalid " + Twine(Prefix) + " value");

  Operands.push_back(AMDGPUOperand::CreateImm(this, Int, S, Type));
  return ParseStatus::Success;
}

ParseStatus AMDGPUAsmParser::parseSDWADstUnused(OperandVector &Operands) {
  using namespace llvm::AMDGPU::SDWA;

  SMLoc S = getLoc();
  StringRef Value;

  SMLoc StringLoc;
  ParseStatus Res = parseStringWithPrefix("dst_unused", Value, StringLoc);
  if (!Res.isSuccess())
    return Res;

  int64_t Int;
  Int = StringSwitch<int64_t>(Value)
        .Case("UNUSED_PAD", DstUnused::UNUSED_PAD)
        .Case("UNUSED_SEXT", DstUnused::UNUSED_SEXT)
        .Case("UNUSED_PRESERVE", DstUnused::UNUSED_PRESERVE)
        .Default(0xffffffff);

  if (Int == 0xffffffff)
    return Error(StringLoc, "invalid dst_unused value");

  Operands.push_back(AMDGPUOperand::CreateImm(this, Int, S, AMDGPUOperand::ImmTySDWADstUnused));
  return ParseStatus::Success;
}

void AMDGPUAsmParser::cvtSdwaVOP1(MCInst &Inst, const OperandVector &Operands) {
  cvtSDWA(Inst, Operands, SIInstrFlags::VOP1);
}

void AMDGPUAsmParser::cvtSdwaVOP2(MCInst &Inst, const OperandVector &Operands) {
  cvtSDWA(Inst, Operands, SIInstrFlags::VOP2);
}

void AMDGPUAsmParser::cvtSdwaVOP2b(MCInst &Inst, const OperandVector &Operands) {
  cvtSDWA(Inst, Operands, SIInstrFlags::VOP2, true, true);
}

void AMDGPUAsmParser::cvtSdwaVOP2e(MCInst &Inst, const OperandVector &Operands) {
  cvtSDWA(Inst, Operands, SIInstrFlags::VOP2, false, true);
}

void AMDGPUAsmParser::cvtSdwaVOPC(MCInst &Inst, const OperandVector &Operands) {
  cvtSDWA(Inst, Operands, SIInstrFlags::VOPC, isVI());
}

void AMDGPUAsmParser::cvtSDWA(MCInst &Inst, const OperandVector &Operands,
                              uint64_t BasicInstType,
                              bool SkipDstVcc,
                              bool SkipSrcVcc) {
  using namespace llvm::AMDGPU::SDWA;

  OptionalImmIndexMap OptionalIdx;
  bool SkipVcc = SkipDstVcc || SkipSrcVcc;
  bool SkippedVcc = false;

  unsigned I = 1;
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned J = 0; J < Desc.getNumDefs(); ++J) {
    ((AMDGPUOperand &)*Operands[I++]).addRegOperands(Inst, 1);
  }

  for (unsigned E = Operands.size(); I != E; ++I) {
    AMDGPUOperand &Op = ((AMDGPUOperand &)*Operands[I]);
    if (SkipVcc && !SkippedVcc && Op.isReg() &&
        (Op.getReg() == AMDGPU::VCC || Op.getReg() == AMDGPU::VCC_LO)) {
      // VOP2b (v_add_u32, v_sub_u32 ...) sdwa use "vcc" token as dst.
      // Skip it if it's 2nd (e.g. v_add_i32_sdwa v1, vcc, v2, v3)
      // or 4th (v_addc_u32_sdwa v1, vcc, v2, v3, vcc) operand.
      // Skip VCC only if we didn't skip it on previous iteration.
      // Note that src0 and src1 occupy 2 slots each because of modifiers.
      if (BasicInstType == SIInstrFlags::VOP2 &&
          ((SkipDstVcc && Inst.getNumOperands() == 1) ||
           (SkipSrcVcc && Inst.getNumOperands() == 5))) {
        SkippedVcc = true;
        continue;
      }
      if (BasicInstType == SIInstrFlags::VOPC && Inst.getNumOperands() == 0) {
        SkippedVcc = true;
        continue;
      }
    }
    if (isRegOrImmWithInputMods(Desc, Inst.getNumOperands())) {
      Op.addRegOrImmWithInputModsOperands(Inst, 2);
    } else if (Op.isImm()) {
      // Handle optional arguments
      OptionalIdx[Op.getImmTy()] = I;
    } else {
      llvm_unreachable("Invalid operand type");
    }
    SkippedVcc = false;
  }

  const unsigned Opc = Inst.getOpcode();
  if (Opc != AMDGPU::V_NOP_sdwa_gfx10 && Opc != AMDGPU::V_NOP_sdwa_gfx9 &&
      Opc != AMDGPU::V_NOP_sdwa_vi) {
    // v_nop_sdwa_sdwa_vi/gfx9 has no optional sdwa arguments
    switch (BasicInstType) {
    case SIInstrFlags::VOP1:
      if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::clamp))
        addOptionalImmOperand(Inst, Operands, OptionalIdx,
                              AMDGPUOperand::ImmTyClamp, 0);

      if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::omod))
        addOptionalImmOperand(Inst, Operands, OptionalIdx,
                              AMDGPUOperand::ImmTyOModSI, 0);

      if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::dst_sel))
        addOptionalImmOperand(Inst, Operands, OptionalIdx,
                              AMDGPUOperand::ImmTySDWADstSel, SdwaSel::DWORD);

      if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::dst_unused))
        addOptionalImmOperand(Inst, Operands, OptionalIdx,
                              AMDGPUOperand::ImmTySDWADstUnused,
                              DstUnused::UNUSED_PRESERVE);

      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWASrc0Sel, SdwaSel::DWORD);
      break;

    case SIInstrFlags::VOP2:
      addOptionalImmOperand(Inst, Operands, OptionalIdx,
                            AMDGPUOperand::ImmTyClamp, 0);

      if (AMDGPU::hasNamedOperand(Inst.getOpcode(), AMDGPU::OpName::omod))
        addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTyOModSI, 0);

      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWADstSel, SdwaSel::DWORD);
      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWADstUnused, DstUnused::UNUSED_PRESERVE);
      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWASrc0Sel, SdwaSel::DWORD);
      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWASrc1Sel, SdwaSel::DWORD);
      break;

    case SIInstrFlags::VOPC:
      if (AMDGPU::hasNamedOperand(Inst.getOpcode(), AMDGPU::OpName::clamp))
        addOptionalImmOperand(Inst, Operands, OptionalIdx,
                              AMDGPUOperand::ImmTyClamp, 0);
      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWASrc0Sel, SdwaSel::DWORD);
      addOptionalImmOperand(Inst, Operands, OptionalIdx, AMDGPUOperand::ImmTySDWASrc1Sel, SdwaSel::DWORD);
      break;

    default:
      llvm_unreachable("Invalid instruction type. Only VOP1, VOP2 and VOPC allowed");
    }
  }

  // special case v_mac_{f16, f32}:
  // it has src2 register operand that is tied to dst operand
  if (Inst.getOpcode() == AMDGPU::V_MAC_F32_sdwa_vi ||
      Inst.getOpcode() == AMDGPU::V_MAC_F16_sdwa_vi)  {
    auto it = Inst.begin();
    std::advance(
      it, AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::src2));
    Inst.insert(it, Inst.getOperand(0)); // src2 = dst
  }
}

/// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUAsmParser() {
  RegisterMCAsmParser<AMDGPUAsmParser> A(getTheR600Target());
  RegisterMCAsmParser<AMDGPUAsmParser> B(getTheGCNTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#define GET_MNEMONIC_CHECKER
#include "AMDGPUGenAsmMatcher.inc"

ParseStatus AMDGPUAsmParser::parseCustomOperand(OperandVector &Operands,
                                                unsigned MCK) {
  switch (MCK) {
  case MCK_addr64:
    return parseTokenOp("addr64", Operands);
  case MCK_done:
    return parseTokenOp("done", Operands);
  case MCK_idxen:
    return parseTokenOp("idxen", Operands);
  case MCK_lds:
    return parseTokenOp("lds", Operands);
  case MCK_offen:
    return parseTokenOp("offen", Operands);
  case MCK_off:
    return parseTokenOp("off", Operands);
  case MCK_row_95_en:
    return parseTokenOp("row_en", Operands);
  case MCK_gds:
    return parseNamedBit("gds", Operands, AMDGPUOperand::ImmTyGDS);
  case MCK_tfe:
    return parseNamedBit("tfe", Operands, AMDGPUOperand::ImmTyTFE);
  }
  return tryCustomParseOperand(Operands, MCK);
}

// This function should be defined after auto-generated include so that we have
// MatchClassKind enum defined
unsigned AMDGPUAsmParser::validateTargetOperandClass(MCParsedAsmOperand &Op,
                                                     unsigned Kind) {
  // Tokens like "glc" would be parsed as immediate operands in ParseOperand().
  // But MatchInstructionImpl() expects to meet token and fails to validate
  // operand. This method checks if we are given immediate operand but expect to
  // get corresponding token.
  AMDGPUOperand &Operand = (AMDGPUOperand&)Op;
  switch (Kind) {
  case MCK_addr64:
    return Operand.isAddr64() ? Match_Success : Match_InvalidOperand;
  case MCK_gds:
    return Operand.isGDS() ? Match_Success : Match_InvalidOperand;
  case MCK_lds:
    return Operand.isLDS() ? Match_Success : Match_InvalidOperand;
  case MCK_idxen:
    return Operand.isIdxen() ? Match_Success : Match_InvalidOperand;
  case MCK_offen:
    return Operand.isOffen() ? Match_Success : Match_InvalidOperand;
  case MCK_tfe:
    return Operand.isTFE() ? Match_Success : Match_InvalidOperand;
  case MCK_SSrc_b32:
    // When operands have expression values, they will return true for isToken,
    // because it is not possible to distinguish between a token and an
    // expression at parse time. MatchInstructionImpl() will always try to
    // match an operand as a token, when isToken returns true, and when the
    // name of the expression is not a valid token, the match will fail,
    // so we need to handle it here.
    return Operand.isSSrc_b32() ? Match_Success : Match_InvalidOperand;
  case MCK_SSrc_f32:
    return Operand.isSSrc_f32() ? Match_Success : Match_InvalidOperand;
  case MCK_SOPPBrTarget:
    return Operand.isSOPPBrTarget() ? Match_Success : Match_InvalidOperand;
  case MCK_VReg32OrOff:
    return Operand.isVReg32OrOff() ? Match_Success : Match_InvalidOperand;
  case MCK_InterpSlot:
    return Operand.isInterpSlot() ? Match_Success : Match_InvalidOperand;
  case MCK_InterpAttr:
    return Operand.isInterpAttr() ? Match_Success : Match_InvalidOperand;
  case MCK_InterpAttrChan:
    return Operand.isInterpAttrChan() ? Match_Success : Match_InvalidOperand;
  case MCK_SReg_64:
  case MCK_SReg_64_XEXEC:
    // Null is defined as a 32-bit register but
    // it should also be enabled with 64-bit operands.
    // The following code enables it for SReg_64 operands
    // used as source and destination. Remaining source
    // operands are handled in isInlinableImm.
    return Operand.isNull() ? Match_Success : Match_InvalidOperand;
  default:
    return Match_InvalidOperand;
  }
}

//===----------------------------------------------------------------------===//
// endpgm
//===----------------------------------------------------------------------===//

ParseStatus AMDGPUAsmParser::parseEndpgm(OperandVector &Operands) {
  SMLoc S = getLoc();
  int64_t Imm = 0;

  if (!parseExpr(Imm)) {
    // The operand is optional, if not present default to 0
    Imm = 0;
  }

  if (!isUInt<16>(Imm))
    return Error(S, "expected a 16-bit value");

  Operands.push_back(
      AMDGPUOperand::CreateImm(this, Imm, S, AMDGPUOperand::ImmTyEndpgm));
  return ParseStatus::Success;
}

bool AMDGPUOperand::isEndpgm() const { return isImmTy(ImmTyEndpgm); }

//===----------------------------------------------------------------------===//
// Split Barrier
//===----------------------------------------------------------------------===//

bool AMDGPUOperand::isSplitBarrier() const { return isInlinableImm(MVT::i32); }
