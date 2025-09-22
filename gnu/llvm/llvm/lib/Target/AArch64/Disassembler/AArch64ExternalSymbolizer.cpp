//===- AArch64ExternalSymbolizer.cpp - Symbolizer for AArch64 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64ExternalSymbolizer.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-disassembler"

static MCSymbolRefExpr::VariantKind
getVariant(uint64_t LLVMDisassembler_VariantKind) {
  switch (LLVMDisassembler_VariantKind) {
  case LLVMDisassembler_VariantKind_None:
    return MCSymbolRefExpr::VK_None;
  case LLVMDisassembler_VariantKind_ARM64_PAGE:
    return MCSymbolRefExpr::VK_PAGE;
  case LLVMDisassembler_VariantKind_ARM64_PAGEOFF:
    return MCSymbolRefExpr::VK_PAGEOFF;
  case LLVMDisassembler_VariantKind_ARM64_GOTPAGE:
    return MCSymbolRefExpr::VK_GOTPAGE;
  case LLVMDisassembler_VariantKind_ARM64_GOTPAGEOFF:
    return MCSymbolRefExpr::VK_GOTPAGEOFF;
  case LLVMDisassembler_VariantKind_ARM64_TLVP:
    return MCSymbolRefExpr::VK_TLVPPAGE;
  case LLVMDisassembler_VariantKind_ARM64_TLVOFF:
    return MCSymbolRefExpr::VK_TLVPPAGEOFF;
  default:
    llvm_unreachable("bad LLVMDisassembler_VariantKind");
  }
}

/// tryAddingSymbolicOperand - tryAddingSymbolicOperand trys to add a symbolic
/// operand in place of the immediate Value in the MCInst.  The immediate
/// Value has not had any PC adjustment made by the caller. If the instruction
/// is a branch that adds the PC to the immediate Value then isBranch is
/// Success, else Fail. If GetOpInfo is non-null, then it is called to get any
/// symbolic information at the Address for this instrution.  If that returns
/// non-zero then the symbolic information it returns is used to create an
/// MCExpr and that is added as an operand to the MCInst.  If GetOpInfo()
/// returns zero and isBranch is Success then a symbol look up for
/// Address + Value is done and if a symbol is found an MCExpr is created with
/// that, else an MCExpr with Address + Value is created.  If GetOpInfo()
/// returns zero and isBranch is Fail then the Opcode of the MCInst is
/// tested and for ADRP an other instructions that help to load of pointers
/// a symbol look up is done to see it is returns a specific reference type
/// to add to the comment stream.  This function returns Success if it adds
/// an operand to the MCInst and Fail otherwise.
bool AArch64ExternalSymbolizer::tryAddingSymbolicOperand(
    MCInst &MI, raw_ostream &CommentStream, int64_t Value, uint64_t Address,
    bool IsBranch, uint64_t Offset, uint64_t OpSize, uint64_t InstSize) {
  if (!SymbolLookUp)
    return false;
  // FIXME: This method shares a lot of code with
  //        MCExternalSymbolizer::tryAddingSymbolicOperand. It may be possible
  //        refactor the MCExternalSymbolizer interface to allow more of this
  //        implementation to be shared.
  //
  struct LLVMOpInfo1 SymbolicOp;
  memset(&SymbolicOp, '\0', sizeof(struct LLVMOpInfo1));
  SymbolicOp.Value = Value;
  uint64_t ReferenceType;
  const char *ReferenceName;
  if (!GetOpInfo || !GetOpInfo(DisInfo, Address, /*Offset=*/0, OpSize, InstSize,
                               1, &SymbolicOp)) {
    if (IsBranch) {
      ReferenceType = LLVMDisassembler_ReferenceType_In_Branch;
      const char *Name = SymbolLookUp(DisInfo, Address + Value, &ReferenceType,
                                      Address, &ReferenceName);
      if (Name) {
        SymbolicOp.AddSymbol.Name = Name;
        SymbolicOp.AddSymbol.Present = true;
        SymbolicOp.Value = 0;
      } else {
        SymbolicOp.Value = Address + Value;
      }
      if (ReferenceType == LLVMDisassembler_ReferenceType_Out_SymbolStub)
        CommentStream << "symbol stub for: " << ReferenceName;
      else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_Objc_Message)
        CommentStream << "Objc message: " << ReferenceName;
    } else if (MI.getOpcode() == AArch64::ADRP) {
        ReferenceType = LLVMDisassembler_ReferenceType_In_ARM64_ADRP;
        // otool expects the fully encoded ADRP instruction to be passed in as
        // the value here, so reconstruct it:
        const MCRegisterInfo &MCRI = *Ctx.getRegisterInfo();
        uint32_t EncodedInst = 0x90000000;
        EncodedInst |= (Value & 0x3) << 29; // immlo
        EncodedInst |= ((Value >> 2) & 0x7FFFF) << 5; // immhi
        EncodedInst |= MCRI.getEncodingValue(MI.getOperand(0).getReg()); // reg
        SymbolLookUp(DisInfo, EncodedInst, &ReferenceType, Address,
                     &ReferenceName);
        CommentStream << format("0x%llx", (0xfffffffffffff000LL & Address) +
                                              Value * 0x1000);
    } else if (MI.getOpcode() == AArch64::ADDXri ||
               MI.getOpcode() == AArch64::LDRXui ||
               MI.getOpcode() == AArch64::LDRXl ||
               MI.getOpcode() == AArch64::ADR) {
      if (MI.getOpcode() == AArch64::ADDXri)
        ReferenceType = LLVMDisassembler_ReferenceType_In_ARM64_ADDXri;
      else if (MI.getOpcode() == AArch64::LDRXui)
        ReferenceType = LLVMDisassembler_ReferenceType_In_ARM64_LDRXui;
      if (MI.getOpcode() == AArch64::LDRXl) {
        ReferenceType = LLVMDisassembler_ReferenceType_In_ARM64_LDRXl;
        SymbolLookUp(DisInfo, Address + Value, &ReferenceType, Address,
                     &ReferenceName);
      } else if (MI.getOpcode() == AArch64::ADR) {
        ReferenceType = LLVMDisassembler_ReferenceType_In_ARM64_ADR;
        SymbolLookUp(DisInfo, Address + Value, &ReferenceType, Address,
                            &ReferenceName);
      } else {
        const MCRegisterInfo &MCRI = *Ctx.getRegisterInfo();
        // otool expects the fully encoded ADD/LDR instruction to be passed in
        // as the value here, so reconstruct it:
        unsigned EncodedInst =
          MI.getOpcode() == AArch64::ADDXri ? 0x91000000: 0xF9400000;
        EncodedInst |= Value << 10; // imm12 [+ shift:2 for ADD]
        EncodedInst |=
          MCRI.getEncodingValue(MI.getOperand(1).getReg()) << 5; // Rn
        EncodedInst |= MCRI.getEncodingValue(MI.getOperand(0).getReg()); // Rd

        SymbolLookUp(DisInfo, EncodedInst, &ReferenceType, Address,
                     &ReferenceName);
      }
      if (ReferenceType == LLVMDisassembler_ReferenceType_Out_LitPool_SymAddr)
        CommentStream << "literal pool symbol address: " << ReferenceName;
      else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_LitPool_CstrAddr) {
        CommentStream << "literal pool for: \"";
        CommentStream.write_escaped(ReferenceName);
        CommentStream << "\"";
      } else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_Objc_CFString_Ref)
        CommentStream << "Objc cfstring ref: @\"" << ReferenceName << "\"";
      else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_Objc_Message)
        CommentStream << "Objc message: " << ReferenceName;
      else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_Objc_Message_Ref)
        CommentStream << "Objc message ref: " << ReferenceName;
      else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_Objc_Selector_Ref)
        CommentStream << "Objc selector ref: " << ReferenceName;
      else if (ReferenceType ==
               LLVMDisassembler_ReferenceType_Out_Objc_Class_Ref)
        CommentStream << "Objc class ref: " << ReferenceName;
      // For these instructions, the SymbolLookUp() above is just to get the
      // ReferenceType and ReferenceName.  We want to make sure not to
      // fall through so we don't build an MCExpr to leave the disassembly
      // of the immediate values of these instructions to the InstPrinter.
      return false;
    } else {
      return false;
    }
  }

  const MCExpr *Add = nullptr;
  if (SymbolicOp.AddSymbol.Present) {
    if (SymbolicOp.AddSymbol.Name) {
      StringRef Name(SymbolicOp.AddSymbol.Name);
      MCSymbol *Sym = Ctx.getOrCreateSymbol(Name);
      MCSymbolRefExpr::VariantKind Variant = getVariant(SymbolicOp.VariantKind);
      if (Variant != MCSymbolRefExpr::VK_None)
        Add = MCSymbolRefExpr::create(Sym, Variant, Ctx);
      else
        Add = MCSymbolRefExpr::create(Sym, Ctx);
    } else {
      Add = MCConstantExpr::create(SymbolicOp.AddSymbol.Value, Ctx);
    }
  }

  const MCExpr *Sub = nullptr;
  if (SymbolicOp.SubtractSymbol.Present) {
    if (SymbolicOp.SubtractSymbol.Name) {
      StringRef Name(SymbolicOp.SubtractSymbol.Name);
      MCSymbol *Sym = Ctx.getOrCreateSymbol(Name);
      Sub = MCSymbolRefExpr::create(Sym, Ctx);
    } else {
      Sub = MCConstantExpr::create(SymbolicOp.SubtractSymbol.Value, Ctx);
    }
  }

  const MCExpr *Off = nullptr;
  if (SymbolicOp.Value != 0)
    Off = MCConstantExpr::create(SymbolicOp.Value, Ctx);

  const MCExpr *Expr;
  if (Sub) {
    const MCExpr *LHS;
    if (Add)
      LHS = MCBinaryExpr::createSub(Add, Sub, Ctx);
    else
      LHS = MCUnaryExpr::createMinus(Sub, Ctx);
    if (Off)
      Expr = MCBinaryExpr::createAdd(LHS, Off, Ctx);
    else
      Expr = LHS;
  } else if (Add) {
    if (Off)
      Expr = MCBinaryExpr::createAdd(Add, Off, Ctx);
    else
      Expr = Add;
  } else {
    if (Off)
      Expr = Off;
    else
      Expr = MCConstantExpr::create(0, Ctx);
  }

  MI.addOperand(MCOperand::createExpr(Expr));

  return true;
}
