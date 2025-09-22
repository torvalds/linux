//===- X86Operand.h - Parsed X86 machine instruction ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_ASMPARSER_X86OPERAND_H
#define LLVM_LIB_TARGET_X86_ASMPARSER_X86OPERAND_H

#include "MCTargetDesc/X86IntelInstPrinter.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86AsmParserCommon.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>
#include <memory>

namespace llvm {

/// X86Operand - Instances of this class represent a parsed X86 machine
/// instruction.
struct X86Operand final : public MCParsedAsmOperand {
  enum KindTy { Token, Register, Immediate, Memory, Prefix, DXRegister } Kind;

  SMLoc StartLoc, EndLoc;
  SMLoc OffsetOfLoc;
  StringRef SymName;
  void *OpDecl;
  bool AddressOf;

  /// This used for inline asm which may specify base reg and index reg for
  /// MemOp. e.g. ARR[eax + ecx*4], so no extra reg can be used for MemOp.
  bool UseUpRegs = false;

  struct TokOp {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    unsigned RegNo;
  };

  struct PrefOp {
    unsigned Prefixes;
  };

  struct ImmOp {
    const MCExpr *Val;
    bool LocalRef;
  };

  struct MemOp {
    unsigned SegReg;
    const MCExpr *Disp;
    unsigned BaseReg;
    unsigned DefaultBaseReg;
    unsigned IndexReg;
    unsigned Scale;
    unsigned Size;
    unsigned ModeSize;

    /// If the memory operand is unsized and there are multiple instruction
    /// matches, prefer the one with this size.
    unsigned FrontendSize;

    /// If false, then this operand must be a memory operand for an indirect
    /// branch instruction. Otherwise, this operand may belong to either a
    /// direct or indirect branch instruction.
    bool MaybeDirectBranchDest;
  };

  union {
    struct TokOp Tok;
    struct RegOp Reg;
    struct ImmOp Imm;
    struct MemOp Mem;
    struct PrefOp Pref;
  };

  X86Operand(KindTy K, SMLoc Start, SMLoc End)
      : Kind(K), StartLoc(Start), EndLoc(End), OpDecl(nullptr),
        AddressOf(false) {}

  StringRef getSymName() override { return SymName; }
  void *getOpDecl() override { return OpDecl; }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }

  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }

  /// getLocRange - Get the range between the first and last token of this
  /// operand.
  SMRange getLocRange() const { return SMRange(StartLoc, EndLoc); }

  /// getOffsetOfLoc - Get the location of the offset operator.
  SMLoc getOffsetOfLoc() const override { return OffsetOfLoc; }

  void print(raw_ostream &OS) const override {

    auto PrintImmValue = [&](const MCExpr *Val, const char *VName) {
      if (Val->getKind() == MCExpr::Constant) {
        if (auto Imm = cast<MCConstantExpr>(Val)->getValue())
          OS << VName << Imm;
      } else if (Val->getKind() == MCExpr::SymbolRef) {
        if (auto *SRE = dyn_cast<MCSymbolRefExpr>(Val)) {
          const MCSymbol &Sym = SRE->getSymbol();
          if (const char *SymNameStr = Sym.getName().data())
            OS << VName << SymNameStr;
        }
      }
    };

    switch (Kind) {
    case Token:
      OS << Tok.Data;
      break;
    case Register:
      OS << "Reg:" << X86IntelInstPrinter::getRegisterName(Reg.RegNo);
      break;
    case DXRegister:
      OS << "DXReg";
      break;
    case Immediate:
      PrintImmValue(Imm.Val, "Imm:");
      break;
    case Prefix:
      OS << "Prefix:" << Pref.Prefixes;
      break;
    case Memory:
      OS << "Memory: ModeSize=" << Mem.ModeSize;
      if (Mem.Size)
        OS << ",Size=" << Mem.Size;
      if (Mem.BaseReg)
        OS << ",BaseReg=" << X86IntelInstPrinter::getRegisterName(Mem.BaseReg);
      if (Mem.IndexReg)
        OS << ",IndexReg="
           << X86IntelInstPrinter::getRegisterName(Mem.IndexReg);
      if (Mem.Scale)
        OS << ",Scale=" << Mem.Scale;
      if (Mem.Disp)
        PrintImmValue(Mem.Disp, ",Disp=");
      if (Mem.SegReg)
        OS << ",SegReg=" << X86IntelInstPrinter::getRegisterName(Mem.SegReg);
      break;
    }
  }

  StringRef getToken() const {
    assert(Kind == Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }
  void setTokenValue(StringRef Value) {
    assert(Kind == Token && "Invalid access!");
    Tok.Data = Value.data();
    Tok.Length = Value.size();
  }

  MCRegister getReg() const override {
    assert(Kind == Register && "Invalid access!");
    return Reg.RegNo;
  }

  unsigned getPrefix() const {
    assert(Kind == Prefix && "Invalid access!");
    return Pref.Prefixes;
  }

  const MCExpr *getImm() const {
    assert(Kind == Immediate && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getMemDisp() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.Disp;
  }
  unsigned getMemSegReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.SegReg;
  }
  unsigned getMemBaseReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.BaseReg;
  }
  unsigned getMemDefaultBaseReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.DefaultBaseReg;
  }
  unsigned getMemIndexReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.IndexReg;
  }
  unsigned getMemScale() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.Scale;
  }
  unsigned getMemModeSize() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.ModeSize;
  }
  unsigned getMemFrontendSize() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.FrontendSize;
  }
  bool isMaybeDirectBranchDest() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.MaybeDirectBranchDest;
  }

  bool isToken() const override {return Kind == Token; }

  bool isImm() const override { return Kind == Immediate; }

  bool isImmSExti16i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti16i8Value(CE->getValue());
  }
  bool isImmSExti32i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti32i8Value(CE->getValue());
  }
  bool isImmSExti64i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti64i8Value(CE->getValue());
  }
  bool isImmSExti64i32() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti64i32Value(CE->getValue());
  }

  bool isImmUnsignedi4() const {
    if (!isImm()) return false;
    // If this isn't a constant expr, reject it. The immediate byte is shared
    // with a register encoding. We can't have it affected by a relocation.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    return isImmUnsignedi4Value(CE->getValue());
  }

  bool isImmUnsignedi8() const {
    if (!isImm()) return false;
    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return true;
    return isImmUnsignedi8Value(CE->getValue());
  }

  bool isOffsetOfLocal() const override { return isImm() && Imm.LocalRef; }

  bool needAddressOf() const override { return AddressOf; }

  bool isMem() const override { return Kind == Memory; }
  bool isMemUnsized() const {
    return Kind == Memory && Mem.Size == 0;
  }
  bool isMem8() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 8);
  }
  bool isMem16() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 16);
  }
  bool isMem32() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 32);
  }
  bool isMem64() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 64);
  }
  bool isMem80() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 80);
  }
  bool isMem128() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 128);
  }
  bool isMem256() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 256);
  }
  bool isMem512() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 512);
  }

  bool isSibMem() const {
    return isMem() && Mem.BaseReg != X86::RIP && Mem.BaseReg != X86::EIP;
  }

  bool isMemIndexReg(unsigned LowR, unsigned HighR) const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.IndexReg >= LowR && Mem.IndexReg <= HighR;
  }

  bool isMem64_RC128() const {
    return isMem64() && isMemIndexReg(X86::XMM0, X86::XMM15);
  }
  bool isMem128_RC128() const {
    return isMem128() && isMemIndexReg(X86::XMM0, X86::XMM15);
  }
  bool isMem128_RC256() const {
    return isMem128() && isMemIndexReg(X86::YMM0, X86::YMM15);
  }
  bool isMem256_RC128() const {
    return isMem256() && isMemIndexReg(X86::XMM0, X86::XMM15);
  }
  bool isMem256_RC256() const {
    return isMem256() && isMemIndexReg(X86::YMM0, X86::YMM15);
  }

  bool isMem64_RC128X() const {
    return isMem64() && X86II::isXMMReg(Mem.IndexReg);
  }
  bool isMem128_RC128X() const {
    return isMem128() && X86II::isXMMReg(Mem.IndexReg);
  }
  bool isMem128_RC256X() const {
    return isMem128() && X86II::isYMMReg(Mem.IndexReg);
  }
  bool isMem256_RC128X() const {
    return isMem256() && X86II::isXMMReg(Mem.IndexReg);
  }
  bool isMem256_RC256X() const {
    return isMem256() && X86II::isYMMReg(Mem.IndexReg);
  }
  bool isMem256_RC512() const {
    return isMem256() && X86II::isZMMReg(Mem.IndexReg);
  }
  bool isMem512_RC256X() const {
    return isMem512() && X86II::isYMMReg(Mem.IndexReg);
  }
  bool isMem512_RC512() const {
    return isMem512() && X86II::isZMMReg(Mem.IndexReg);
  }
  bool isMem512_GR16() const {
    if (!isMem512())
      return false;
    if (getMemBaseReg() &&
        !X86MCRegisterClasses[X86::GR16RegClassID].contains(getMemBaseReg()))
      return false;
    return true;
  }
  bool isMem512_GR32() const {
    if (!isMem512())
      return false;
    if (getMemBaseReg() &&
        !X86MCRegisterClasses[X86::GR32RegClassID].contains(getMemBaseReg()) &&
        getMemBaseReg() != X86::EIP)
      return false;
    if (getMemIndexReg() &&
        !X86MCRegisterClasses[X86::GR32RegClassID].contains(getMemIndexReg()) &&
        getMemIndexReg() != X86::EIZ)
      return false;
    return true;
  }
  bool isMem512_GR64() const {
    if (!isMem512())
      return false;
    if (getMemBaseReg() &&
        !X86MCRegisterClasses[X86::GR64RegClassID].contains(getMemBaseReg()) &&
        getMemBaseReg() != X86::RIP)
      return false;
    if (getMemIndexReg() &&
        !X86MCRegisterClasses[X86::GR64RegClassID].contains(getMemIndexReg()) &&
        getMemIndexReg() != X86::RIZ)
      return false;
    return true;
  }

  bool isAbsMem() const {
    return Kind == Memory && !getMemSegReg() && !getMemBaseReg() &&
           !getMemIndexReg() && getMemScale() == 1 && isMaybeDirectBranchDest();
  }

  bool isAVX512RC() const{
      return isImm();
  }

  bool isAbsMem16() const {
    return isAbsMem() && Mem.ModeSize == 16;
  }

  bool isMemUseUpRegs() const override { return UseUpRegs; }

  bool isSrcIdx() const {
    return !getMemIndexReg() && getMemScale() == 1 &&
      (getMemBaseReg() == X86::RSI || getMemBaseReg() == X86::ESI ||
       getMemBaseReg() == X86::SI) && isa<MCConstantExpr>(getMemDisp()) &&
      cast<MCConstantExpr>(getMemDisp())->getValue() == 0;
  }
  bool isSrcIdx8() const {
    return isMem8() && isSrcIdx();
  }
  bool isSrcIdx16() const {
    return isMem16() && isSrcIdx();
  }
  bool isSrcIdx32() const {
    return isMem32() && isSrcIdx();
  }
  bool isSrcIdx64() const {
    return isMem64() && isSrcIdx();
  }

  bool isDstIdx() const {
    return !getMemIndexReg() && getMemScale() == 1 &&
      (getMemSegReg() == 0 || getMemSegReg() == X86::ES) &&
      (getMemBaseReg() == X86::RDI || getMemBaseReg() == X86::EDI ||
       getMemBaseReg() == X86::DI) && isa<MCConstantExpr>(getMemDisp()) &&
      cast<MCConstantExpr>(getMemDisp())->getValue() == 0;
  }
  bool isDstIdx8() const {
    return isMem8() && isDstIdx();
  }
  bool isDstIdx16() const {
    return isMem16() && isDstIdx();
  }
  bool isDstIdx32() const {
    return isMem32() && isDstIdx();
  }
  bool isDstIdx64() const {
    return isMem64() && isDstIdx();
  }

  bool isMemOffs() const {
    return Kind == Memory && !getMemBaseReg() && !getMemIndexReg() &&
      getMemScale() == 1;
  }

  bool isMemOffs16_8() const {
    return isMemOffs() && Mem.ModeSize == 16 && (!Mem.Size || Mem.Size == 8);
  }
  bool isMemOffs16_16() const {
    return isMemOffs() && Mem.ModeSize == 16 && (!Mem.Size || Mem.Size == 16);
  }
  bool isMemOffs16_32() const {
    return isMemOffs() && Mem.ModeSize == 16 && (!Mem.Size || Mem.Size == 32);
  }
  bool isMemOffs32_8() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 8);
  }
  bool isMemOffs32_16() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 16);
  }
  bool isMemOffs32_32() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 32);
  }
  bool isMemOffs32_64() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 64);
  }
  bool isMemOffs64_8() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 8);
  }
  bool isMemOffs64_16() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 16);
  }
  bool isMemOffs64_32() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 32);
  }
  bool isMemOffs64_64() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 64);
  }

  bool isPrefix() const { return Kind == Prefix; }
  bool isReg() const override { return Kind == Register; }
  bool isDXReg() const { return Kind == DXRegister; }

  bool isGR32orGR64() const {
    return Kind == Register &&
      (X86MCRegisterClasses[X86::GR32RegClassID].contains(getReg()) ||
       X86MCRegisterClasses[X86::GR64RegClassID].contains(getReg()));
  }

  bool isGR16orGR32orGR64() const {
    return Kind == Register &&
      (X86MCRegisterClasses[X86::GR16RegClassID].contains(getReg()) ||
       X86MCRegisterClasses[X86::GR32RegClassID].contains(getReg()) ||
       X86MCRegisterClasses[X86::GR64RegClassID].contains(getReg()));
  }

  bool isVectorReg() const {
    return Kind == Register &&
           (X86MCRegisterClasses[X86::VR64RegClassID].contains(getReg()) ||
            X86MCRegisterClasses[X86::VR128XRegClassID].contains(getReg()) ||
            X86MCRegisterClasses[X86::VR256XRegClassID].contains(getReg()) ||
            X86MCRegisterClasses[X86::VR512RegClassID].contains(getReg()));
  }

  bool isVK1Pair() const {
    return Kind == Register &&
      X86MCRegisterClasses[X86::VK1RegClassID].contains(getReg());
  }

  bool isVK2Pair() const {
    return Kind == Register &&
      X86MCRegisterClasses[X86::VK2RegClassID].contains(getReg());
  }

  bool isVK4Pair() const {
    return Kind == Register &&
      X86MCRegisterClasses[X86::VK4RegClassID].contains(getReg());
  }

  bool isVK8Pair() const {
    return Kind == Register &&
      X86MCRegisterClasses[X86::VK8RegClassID].contains(getReg());
  }

  bool isVK16Pair() const {
    return Kind == Register &&
      X86MCRegisterClasses[X86::VK16RegClassID].contains(getReg());
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addGR32orGR64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    MCRegister RegNo = getReg();
    if (X86MCRegisterClasses[X86::GR64RegClassID].contains(RegNo))
      RegNo = getX86SubSuperRegister(RegNo, 32);
    Inst.addOperand(MCOperand::createReg(RegNo));
  }

  void addGR16orGR32orGR64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    MCRegister RegNo = getReg();
    if (X86MCRegisterClasses[X86::GR32RegClassID].contains(RegNo) ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(RegNo))
      RegNo = getX86SubSuperRegister(RegNo, 16);
    Inst.addOperand(MCOperand::createReg(RegNo));
  }

  void addAVX512RCOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addMaskPairOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned Reg = getReg();
    switch (Reg) {
    case X86::K0:
    case X86::K1:
      Reg = X86::K0_K1;
      break;
    case X86::K2:
    case X86::K3:
      Reg = X86::K2_K3;
      break;
    case X86::K4:
    case X86::K5:
      Reg = X86::K4_K5;
      break;
    case X86::K6:
    case X86::K7:
      Reg = X86::K6_K7;
      break;
    }
    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert((N == 5) && "Invalid number of operands!");
    if (getMemBaseReg())
      Inst.addOperand(MCOperand::createReg(getMemBaseReg()));
    else
      Inst.addOperand(MCOperand::createReg(getMemDefaultBaseReg()));
    Inst.addOperand(MCOperand::createImm(getMemScale()));
    Inst.addOperand(MCOperand::createReg(getMemIndexReg()));
    addExpr(Inst, getMemDisp());
    Inst.addOperand(MCOperand::createReg(getMemSegReg()));
  }

  void addAbsMemOperands(MCInst &Inst, unsigned N) const {
    assert((N == 1) && "Invalid number of operands!");
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getMemDisp()))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(getMemDisp()));
  }

  void addSrcIdxOperands(MCInst &Inst, unsigned N) const {
    assert((N == 2) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getMemBaseReg()));
    Inst.addOperand(MCOperand::createReg(getMemSegReg()));
  }

  void addDstIdxOperands(MCInst &Inst, unsigned N) const {
    assert((N == 1) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getMemBaseReg()));
  }

  void addMemOffsOperands(MCInst &Inst, unsigned N) const {
    assert((N == 2) && "Invalid number of operands!");
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getMemDisp()))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(getMemDisp()));
    Inst.addOperand(MCOperand::createReg(getMemSegReg()));
  }

  static std::unique_ptr<X86Operand> CreateToken(StringRef Str, SMLoc Loc) {
    SMLoc EndLoc = SMLoc::getFromPointer(Loc.getPointer() + Str.size());
    auto Res = std::make_unique<X86Operand>(Token, Loc, EndLoc);
    Res->Tok.Data = Str.data();
    Res->Tok.Length = Str.size();
    return Res;
  }

  static std::unique_ptr<X86Operand>
  CreateReg(unsigned RegNo, SMLoc StartLoc, SMLoc EndLoc,
            bool AddressOf = false, SMLoc OffsetOfLoc = SMLoc(),
            StringRef SymName = StringRef(), void *OpDecl = nullptr) {
    auto Res = std::make_unique<X86Operand>(Register, StartLoc, EndLoc);
    Res->Reg.RegNo = RegNo;
    Res->AddressOf = AddressOf;
    Res->OffsetOfLoc = OffsetOfLoc;
    Res->SymName = SymName;
    Res->OpDecl = OpDecl;
    return Res;
  }

  static std::unique_ptr<X86Operand>
  CreateDXReg(SMLoc StartLoc, SMLoc EndLoc) {
    return std::make_unique<X86Operand>(DXRegister, StartLoc, EndLoc);
  }

  static std::unique_ptr<X86Operand>
  CreatePrefix(unsigned Prefixes, SMLoc StartLoc, SMLoc EndLoc) {
    auto Res = std::make_unique<X86Operand>(Prefix, StartLoc, EndLoc);
    Res->Pref.Prefixes = Prefixes;
    return Res;
  }

  static std::unique_ptr<X86Operand> CreateImm(const MCExpr *Val,
                                               SMLoc StartLoc, SMLoc EndLoc,
                                               StringRef SymName = StringRef(),
                                               void *OpDecl = nullptr,
                                               bool GlobalRef = true) {
    auto Res = std::make_unique<X86Operand>(Immediate, StartLoc, EndLoc);
    Res->Imm.Val      = Val;
    Res->Imm.LocalRef = !GlobalRef;
    Res->SymName      = SymName;
    Res->OpDecl       = OpDecl;
    Res->AddressOf    = true;
    return Res;
  }

  /// Create an absolute memory operand.
  static std::unique_ptr<X86Operand>
  CreateMem(unsigned ModeSize, const MCExpr *Disp, SMLoc StartLoc, SMLoc EndLoc,
            unsigned Size = 0, StringRef SymName = StringRef(),
            void *OpDecl = nullptr, unsigned FrontendSize = 0,
            bool UseUpRegs = false, bool MaybeDirectBranchDest = true) {
    auto Res = std::make_unique<X86Operand>(Memory, StartLoc, EndLoc);
    Res->Mem.SegReg   = 0;
    Res->Mem.Disp     = Disp;
    Res->Mem.BaseReg  = 0;
    Res->Mem.DefaultBaseReg = 0;
    Res->Mem.IndexReg = 0;
    Res->Mem.Scale    = 1;
    Res->Mem.Size     = Size;
    Res->Mem.ModeSize = ModeSize;
    Res->Mem.FrontendSize = FrontendSize;
    Res->Mem.MaybeDirectBranchDest = MaybeDirectBranchDest;
    Res->UseUpRegs = UseUpRegs;
    Res->SymName      = SymName;
    Res->OpDecl       = OpDecl;
    Res->AddressOf    = false;
    return Res;
  }

  /// Create a generalized memory operand.
  static std::unique_ptr<X86Operand>
  CreateMem(unsigned ModeSize, unsigned SegReg, const MCExpr *Disp,
            unsigned BaseReg, unsigned IndexReg, unsigned Scale, SMLoc StartLoc,
            SMLoc EndLoc, unsigned Size = 0,
            unsigned DefaultBaseReg = X86::NoRegister,
            StringRef SymName = StringRef(), void *OpDecl = nullptr,
            unsigned FrontendSize = 0, bool UseUpRegs = false,
            bool MaybeDirectBranchDest = true) {
    // We should never just have a displacement, that should be parsed as an
    // absolute memory operand.
    assert((SegReg || BaseReg || IndexReg || DefaultBaseReg) &&
           "Invalid memory operand!");

    // The scale should always be one of {1,2,4,8}.
    assert(((Scale == 1 || Scale == 2 || Scale == 4 || Scale == 8)) &&
           "Invalid scale!");
    auto Res = std::make_unique<X86Operand>(Memory, StartLoc, EndLoc);
    Res->Mem.SegReg   = SegReg;
    Res->Mem.Disp     = Disp;
    Res->Mem.BaseReg  = BaseReg;
    Res->Mem.DefaultBaseReg = DefaultBaseReg;
    Res->Mem.IndexReg = IndexReg;
    Res->Mem.Scale    = Scale;
    Res->Mem.Size     = Size;
    Res->Mem.ModeSize = ModeSize;
    Res->Mem.FrontendSize = FrontendSize;
    Res->Mem.MaybeDirectBranchDest = MaybeDirectBranchDest;
    Res->UseUpRegs = UseUpRegs;
    Res->SymName      = SymName;
    Res->OpDecl       = OpDecl;
    Res->AddressOf    = false;
    return Res;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_X86_ASMPARSER_X86OPERAND_H
