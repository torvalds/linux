//==-- AArch64MCInstLower.cpp - Convert AArch64 MachineInstr to an MCInst --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower AArch64 MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "AArch64MCInstLower.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;
using namespace llvm::object;

extern cl::opt<bool> EnableAArch64ELFLocalDynamicTLSGeneration;

AArch64MCInstLower::AArch64MCInstLower(MCContext &ctx, AsmPrinter &printer)
    : Ctx(ctx), Printer(printer) {}

MCSymbol *
AArch64MCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  return GetGlobalValueSymbol(MO.getGlobal(), MO.getTargetFlags());
}

MCSymbol *AArch64MCInstLower::GetGlobalValueSymbol(const GlobalValue *GV,
                                                   unsigned TargetFlags) const {
  const Triple &TheTriple = Printer.TM.getTargetTriple();
  if (!TheTriple.isOSBinFormatCOFF())
    return Printer.getSymbolPreferLocal(*GV);

  assert(TheTriple.isOSWindows() &&
         "Windows is the only supported COFF target");

  bool IsIndirect =
      (TargetFlags & (AArch64II::MO_DLLIMPORT | AArch64II::MO_COFFSTUB));
  if (!IsIndirect) {
    // For ARM64EC, symbol lookup in the MSVC linker has limited awareness
    // of ARM64EC mangling ("#"/"$$h"). So object files need to refer to both
    // the mangled and unmangled names of ARM64EC symbols, even if they aren't
    // actually used by any relocations. Emit the necessary references here.
    if (!TheTriple.isWindowsArm64EC() || !isa<Function>(GV) ||
        !GV->hasExternalLinkage())
      return Printer.getSymbol(GV);

    StringRef Name = Printer.getSymbol(GV)->getName();
    // Don't mangle ARM64EC runtime functions.
    static constexpr StringLiteral ExcludedFns[] = {
        "__os_arm64x_check_icall_cfg", "__os_arm64x_dispatch_call_no_redirect",
        "__os_arm64x_check_icall"};
    if (is_contained(ExcludedFns, Name))
      return Printer.getSymbol(GV);

    if (std::optional<std::string> MangledName =
            getArm64ECMangledFunctionName(Name.str())) {
      MCSymbol *MangledSym = Ctx.getOrCreateSymbol(MangledName.value());
      if (!cast<Function>(GV)->hasMetadata("arm64ec_hasguestexit")) {
        Printer.OutStreamer->emitSymbolAttribute(Printer.getSymbol(GV),
                                                 MCSA_WeakAntiDep);
        Printer.OutStreamer->emitAssignment(
            Printer.getSymbol(GV),
            MCSymbolRefExpr::create(MangledSym, MCSymbolRefExpr::VK_WEAKREF,
                                    Ctx));
        Printer.OutStreamer->emitSymbolAttribute(MangledSym, MCSA_WeakAntiDep);
        Printer.OutStreamer->emitAssignment(
            MangledSym,
            MCSymbolRefExpr::create(Printer.getSymbol(GV),
                                    MCSymbolRefExpr::VK_WEAKREF, Ctx));
      }

      if (TargetFlags & AArch64II::MO_ARM64EC_CALLMANGLE)
        return MangledSym;
    }

    return Printer.getSymbol(GV);
  }

  SmallString<128> Name;

  if ((TargetFlags & AArch64II::MO_DLLIMPORT) &&
      TheTriple.isWindowsArm64EC() &&
      !(TargetFlags & AArch64II::MO_ARM64EC_CALLMANGLE) &&
      isa<Function>(GV)) {
    // __imp_aux is specific to arm64EC; it represents the actual address of
    // an imported function without any thunks.
    //
    // If we see a reference to an "aux" symbol, also emit a reference to the
    // corresponding non-aux symbol.  Otherwise, the Microsoft linker behaves
    // strangely when linking against x64 import libararies.
    //
    // emitSymbolAttribute() doesn't have any real effect here; it just
    // ensures the symbol name appears in the assembly without any
    // side-effects. It might make sense to design a cleaner way to express
    // this.
    Name = "__imp_";
    Printer.TM.getNameWithPrefix(Name, GV,
                                 Printer.getObjFileLowering().getMangler());
    MCSymbol *ExtraSym = Ctx.getOrCreateSymbol(Name);
    Printer.OutStreamer->emitSymbolAttribute(ExtraSym, MCSA_Global);

    Name = "__imp_aux_";
  } else if (TargetFlags & AArch64II::MO_DLLIMPORT) {
    Name = "__imp_";
  } else if (TargetFlags & AArch64II::MO_COFFSTUB) {
    Name = ".refptr.";
  }
  Printer.TM.getNameWithPrefix(Name, GV,
                               Printer.getObjFileLowering().getMangler());

  MCSymbol *MCSym = Ctx.getOrCreateSymbol(Name);

  if (TargetFlags & AArch64II::MO_COFFSTUB) {
    MachineModuleInfoCOFF &MMICOFF =
        Printer.MMI->getObjFileInfo<MachineModuleInfoCOFF>();
    MachineModuleInfoImpl::StubValueTy &StubSym =
        MMICOFF.getGVStubEntry(MCSym);

    if (!StubSym.getPointer())
      StubSym = MachineModuleInfoImpl::StubValueTy(Printer.getSymbol(GV), true);
  }

  return MCSym;
}

MCSymbol *
AArch64MCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  return Printer.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCOperand AArch64MCInstLower::lowerSymbolOperandMachO(const MachineOperand &MO,
                                                      MCSymbol *Sym) const {
  // FIXME: We would like an efficient form for this, so we don't have to do a
  // lot of extra uniquing.
  MCSymbolRefExpr::VariantKind RefKind = MCSymbolRefExpr::VK_None;
  if ((MO.getTargetFlags() & AArch64II::MO_GOT) != 0) {
    if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_PAGE)
      RefKind = MCSymbolRefExpr::VK_GOTPAGE;
    else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) ==
             AArch64II::MO_PAGEOFF)
      RefKind = MCSymbolRefExpr::VK_GOTPAGEOFF;
    else
      llvm_unreachable("Unexpected target flags with MO_GOT on GV operand");
  } else if ((MO.getTargetFlags() & AArch64II::MO_TLS) != 0) {
    if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_PAGE)
      RefKind = MCSymbolRefExpr::VK_TLVPPAGE;
    else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) ==
             AArch64II::MO_PAGEOFF)
      RefKind = MCSymbolRefExpr::VK_TLVPPAGEOFF;
    else
      llvm_unreachable("Unexpected target flags with MO_TLS on GV operand");
  } else {
    if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_PAGE)
      RefKind = MCSymbolRefExpr::VK_PAGE;
    else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) ==
             AArch64II::MO_PAGEOFF)
      RefKind = MCSymbolRefExpr::VK_PAGEOFF;
  }
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, RefKind, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

MCOperand AArch64MCInstLower::lowerSymbolOperandELF(const MachineOperand &MO,
                                                    MCSymbol *Sym) const {
  uint32_t RefFlags = 0;

  if (MO.getTargetFlags() & AArch64II::MO_GOT)
    RefFlags |= AArch64MCExpr::VK_GOT;
  else if (MO.getTargetFlags() & AArch64II::MO_TLS) {
    TLSModel::Model Model;
    if (MO.isGlobal()) {
      const GlobalValue *GV = MO.getGlobal();
      Model = Printer.TM.getTLSModel(GV);
      if (!EnableAArch64ELFLocalDynamicTLSGeneration &&
          Model == TLSModel::LocalDynamic)
        Model = TLSModel::GeneralDynamic;

    } else {
      assert(MO.isSymbol() &&
             StringRef(MO.getSymbolName()) == "_TLS_MODULE_BASE_" &&
             "unexpected external TLS symbol");
      // The general dynamic access sequence is used to get the
      // address of _TLS_MODULE_BASE_.
      Model = TLSModel::GeneralDynamic;
    }
    switch (Model) {
    case TLSModel::InitialExec:
      RefFlags |= AArch64MCExpr::VK_GOTTPREL;
      break;
    case TLSModel::LocalExec:
      RefFlags |= AArch64MCExpr::VK_TPREL;
      break;
    case TLSModel::LocalDynamic:
      RefFlags |= AArch64MCExpr::VK_DTPREL;
      break;
    case TLSModel::GeneralDynamic:
      RefFlags |= AArch64MCExpr::VK_TLSDESC;
      break;
    }
  } else if (MO.getTargetFlags() & AArch64II::MO_PREL) {
    RefFlags |= AArch64MCExpr::VK_PREL;
  } else {
    // No modifier means this is a generic reference, classified as absolute for
    // the cases where it matters (:abs_g0: etc).
    RefFlags |= AArch64MCExpr::VK_ABS;
  }

  if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_PAGE)
    RefFlags |= AArch64MCExpr::VK_PAGE;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) ==
           AArch64II::MO_PAGEOFF)
    RefFlags |= AArch64MCExpr::VK_PAGEOFF;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G3)
    RefFlags |= AArch64MCExpr::VK_G3;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G2)
    RefFlags |= AArch64MCExpr::VK_G2;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G1)
    RefFlags |= AArch64MCExpr::VK_G1;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G0)
    RefFlags |= AArch64MCExpr::VK_G0;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_HI12)
    RefFlags |= AArch64MCExpr::VK_HI12;

  if (MO.getTargetFlags() & AArch64II::MO_NC)
    RefFlags |= AArch64MCExpr::VK_NC;

  const MCExpr *Expr =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);

  AArch64MCExpr::VariantKind RefKind;
  RefKind = static_cast<AArch64MCExpr::VariantKind>(RefFlags);
  Expr = AArch64MCExpr::create(Expr, RefKind, Ctx);

  return MCOperand::createExpr(Expr);
}

MCOperand AArch64MCInstLower::lowerSymbolOperandCOFF(const MachineOperand &MO,
                                                     MCSymbol *Sym) const {
  uint32_t RefFlags = 0;

  if (MO.getTargetFlags() & AArch64II::MO_TLS) {
    if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_PAGEOFF)
      RefFlags |= AArch64MCExpr::VK_SECREL_LO12;
    else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) ==
             AArch64II::MO_HI12)
      RefFlags |= AArch64MCExpr::VK_SECREL_HI12;

  } else if (MO.getTargetFlags() & AArch64II::MO_S) {
    RefFlags |= AArch64MCExpr::VK_SABS;
  } else {
    RefFlags |= AArch64MCExpr::VK_ABS;

    if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_PAGE)
      RefFlags |= AArch64MCExpr::VK_PAGE;
    else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) ==
             AArch64II::MO_PAGEOFF)
      RefFlags |= AArch64MCExpr::VK_PAGEOFF | AArch64MCExpr::VK_NC;
  }

  if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G3)
    RefFlags |= AArch64MCExpr::VK_G3;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G2)
    RefFlags |= AArch64MCExpr::VK_G2;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G1)
    RefFlags |= AArch64MCExpr::VK_G1;
  else if ((MO.getTargetFlags() & AArch64II::MO_FRAGMENT) == AArch64II::MO_G0)
    RefFlags |= AArch64MCExpr::VK_G0;

  // FIXME: Currently we only set VK_NC for MO_G3/MO_G2/MO_G1/MO_G0. This is
  // because setting VK_NC for others would mean setting their respective
  // RefFlags correctly.  We should do this in a separate patch.
  if (MO.getTargetFlags() & AArch64II::MO_NC) {
    auto MOFrag = (MO.getTargetFlags() & AArch64II::MO_FRAGMENT);
    if (MOFrag == AArch64II::MO_G3 || MOFrag == AArch64II::MO_G2 ||
        MOFrag == AArch64II::MO_G1 || MOFrag == AArch64II::MO_G0)
      RefFlags |= AArch64MCExpr::VK_NC;
  }

  const MCExpr *Expr =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);

  auto RefKind = static_cast<AArch64MCExpr::VariantKind>(RefFlags);
  assert(RefKind != AArch64MCExpr::VK_INVALID &&
         "Invalid relocation requested");
  Expr = AArch64MCExpr::create(Expr, RefKind, Ctx);

  return MCOperand::createExpr(Expr);
}

MCOperand AArch64MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                                 MCSymbol *Sym) const {
  if (Printer.TM.getTargetTriple().isOSBinFormatMachO())
    return lowerSymbolOperandMachO(MO, Sym);
  if (Printer.TM.getTargetTriple().isOSBinFormatCOFF())
    return lowerSymbolOperandCOFF(MO, Sym);

  assert(Printer.TM.getTargetTriple().isOSBinFormatELF() && "Invalid target");
  return lowerSymbolOperandELF(MO, Sym);
}

bool AArch64MCInstLower::lowerOperand(const MachineOperand &MO,
                                      MCOperand &MCOp) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    // Ignore all implicit register operands.
    if (MO.isImplicit())
      return false;
    MCOp = MCOperand::createReg(MO.getReg());
    break;
  case MachineOperand::MO_RegisterMask:
    // Regmasks are like implicit defs.
    return false;
  case MachineOperand::MO_Immediate:
    MCOp = MCOperand::createImm(MO.getImm());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MCOp = MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
    break;
  case MachineOperand::MO_GlobalAddress:
    MCOp = LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO));
    break;
  case MachineOperand::MO_ExternalSymbol:
    MCOp = LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO));
    break;
  case MachineOperand::MO_MCSymbol:
    MCOp = LowerSymbolOperand(MO, MO.getMCSymbol());
    break;
  case MachineOperand::MO_JumpTableIndex:
    MCOp = LowerSymbolOperand(MO, Printer.GetJTISymbol(MO.getIndex()));
    break;
  case MachineOperand::MO_ConstantPoolIndex:
    MCOp = LowerSymbolOperand(MO, Printer.GetCPISymbol(MO.getIndex()));
    break;
  case MachineOperand::MO_BlockAddress:
    MCOp = LowerSymbolOperand(
        MO, Printer.GetBlockAddressSymbol(MO.getBlockAddress()));
    break;
  }
  return true;
}

void AArch64MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    if (lowerOperand(MO, MCOp))
      OutMI.addOperand(MCOp);
  }

  switch (OutMI.getOpcode()) {
  case AArch64::CATCHRET:
    OutMI = MCInst();
    OutMI.setOpcode(AArch64::RET);
    OutMI.addOperand(MCOperand::createReg(AArch64::LR));
    break;
  case AArch64::CLEANUPRET:
    OutMI = MCInst();
    OutMI.setOpcode(AArch64::RET);
    OutMI.addOperand(MCOperand::createReg(AArch64::LR));
    break;
  }
}
