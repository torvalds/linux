//===-- PPCMCTargetDesc.cpp - PowerPC Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides PowerPC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "MCTargetDesc/PPCInstPrinter.h"
#include "MCTargetDesc/PPCMCAsmInfo.h"
#include "PPCELFStreamer.h"
#include "PPCTargetStreamer.h"
#include "PPCXCOFFStreamer.h"
#include "TargetInfo/PowerPCTargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "PPCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "PPCGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "PPCGenRegisterInfo.inc"

/// stripRegisterPrefix - This method strips the character prefix from a
/// register name so that only the number is left.  Used by for linux asm.
const char *PPC::stripRegisterPrefix(const char *RegName) {
  switch (RegName[0]) {
    case 'a':
      if (RegName[1] == 'c' && RegName[2] == 'c')
        return RegName + 3;
      break;
    case 'f':
      if (RegName[1] == 'p')
        return RegName + 2;
      [[fallthrough]];
    case 'r':
    case 'v':
      if (RegName[1] == 's') {
        if (RegName[2] == 'p')
          return RegName + 3;
        return RegName + 2;
      }
      return RegName + 1;
    case 'c':
      if (RegName[1] == 'r')
        return RegName + 2;
      break;
    case 'w':
      // For wacc and wacc_hi
      if (RegName[1] == 'a' && RegName[2] == 'c' && RegName[3] == 'c') {
        if (RegName[4] == '_')
          return RegName + 7;
        else
          return RegName + 4;
      }
      break;
    case 'd':
      // For dmr, dmrp, dmrrow, dmrrowp
      if (RegName[1] == 'm' && RegName[2] == 'r') {
        if (RegName[3] == 'r' && RegName[4] == 'o' && RegName[5] == 'w' &&
            RegName[6] == 'p')
          return RegName + 7;
        else if (RegName[3] == 'r' && RegName[4] == 'o' && RegName[5] == 'w')
          return RegName + 6;
        else if (RegName[3] == 'p')
          return RegName + 4;
        else
          return RegName + 3;
      }
      break;
  }

  return RegName;
}

/// getRegNumForOperand - some operands use different numbering schemes
/// for the same registers. For example, a VSX instruction may have any of
/// vs0-vs63 allocated whereas an Altivec instruction could only have
/// vs32-vs63 allocated (numbered as v0-v31). This function returns the actual
/// register number needed for the opcode/operand number combination.
/// The operand number argument will be useful when we need to extend this
/// to instructions that use both Altivec and VSX numbering (for different
/// operands).
unsigned PPC::getRegNumForOperand(const MCInstrDesc &Desc, unsigned Reg,
                                  unsigned OpNo) {
  int16_t regClass = Desc.operands()[OpNo].RegClass;
  switch (regClass) {
    // We store F0-F31, VF0-VF31 in MCOperand and it should be F0-F31,
    // VSX32-VSX63 during encoding/disassembling
    case PPC::VSSRCRegClassID:
    case PPC::VSFRCRegClassID:
      if (PPC::isVFRegister(Reg))
	return PPC::VSX32 + (Reg - PPC::VF0);
      break;
    // We store VSL0-VSL31, V0-V31 in MCOperand and it should be VSL0-VSL31,
    // VSX32-VSX63 during encoding/disassembling
    case PPC::VSRCRegClassID:
      if (PPC::isVRRegister(Reg))
	return PPC::VSX32 + (Reg - PPC::V0);
      break;
    // Other RegClass doesn't need mapping
    default:
      break;
  }
  return Reg;
}

PPCTargetStreamer::PPCTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

// Pin the vtable to this file.
PPCTargetStreamer::~PPCTargetStreamer() = default;

static MCInstrInfo *createPPCMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitPPCMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createPPCMCRegisterInfo(const Triple &TT) {
  bool isPPC64 =
      (TT.getArch() == Triple::ppc64 || TT.getArch() == Triple::ppc64le);
  unsigned Flavour = isPPC64 ? 0 : 1;
  unsigned RA = isPPC64 ? PPC::LR8 : PPC::LR;

  MCRegisterInfo *X = new MCRegisterInfo();
  InitPPCMCRegisterInfo(X, RA, Flavour, Flavour);
  return X;
}

static MCSubtargetInfo *createPPCMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  // Set some default feature to MC layer.
  std::string FullFS = std::string(FS);

  if (TT.isOSAIX()) {
    if (!FullFS.empty())
      FullFS = "+aix," + FullFS;
    else
      FullFS = "+aix";
  }

  return createPPCMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FullFS);
}

static MCAsmInfo *createPPCMCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple,
                                     const MCTargetOptions &Options) {
  bool isPPC64 = (TheTriple.getArch() == Triple::ppc64 ||
                  TheTriple.getArch() == Triple::ppc64le);

  MCAsmInfo *MAI;
  if (TheTriple.isOSBinFormatXCOFF())
    MAI = new PPCXCOFFMCAsmInfo(isPPC64, TheTriple);
  else
    MAI = new PPCELFMCAsmInfo(isPPC64, TheTriple);

  // Initial state of the frame pointer is R1.
  unsigned Reg = isPPC64 ? PPC::X1 : PPC::R1;
  MCCFIInstruction Inst =
      MCCFIInstruction::cfiDefCfa(nullptr, MRI.getDwarfRegNum(Reg, true), 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCStreamer *
createPPCELFStreamer(const Triple &T, MCContext &Context,
                     std::unique_ptr<MCAsmBackend> &&MAB,
                     std::unique_ptr<MCObjectWriter> &&OW,
                     std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createPPCELFStreamer(Context, std::move(MAB), std::move(OW),
                              std::move(Emitter));
}

static MCStreamer *
createPPCXCOFFStreamer(const Triple &T, MCContext &Context,
                       std::unique_ptr<MCAsmBackend> &&MAB,
                       std::unique_ptr<MCObjectWriter> &&OW,
                       std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createPPCXCOFFStreamer(Context, std::move(MAB), std::move(OW),
                                std::move(Emitter));
}

namespace {

class PPCTargetAsmStreamer : public PPCTargetStreamer {
  formatted_raw_ostream &OS;

public:
  PPCTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS)
      : PPCTargetStreamer(S), OS(OS) {}

  void emitTCEntry(const MCSymbol &S,
                   MCSymbolRefExpr::VariantKind Kind) override {
    if (const MCSymbolXCOFF *XSym = dyn_cast<MCSymbolXCOFF>(&S)) {
      MCSymbolXCOFF *TCSym =
          cast<MCSectionXCOFF>(Streamer.getCurrentSectionOnly())
              ->getQualNameSymbol();
      // On AIX, we have TLS variable offsets (symbol@({gd|ie|le|ld}) depending
      // on the TLS access method (or model). For the general-dynamic access
      // method, we also have region handle (symbol@m) for each variable. For
      // local-dynamic, there is a module handle (_$TLSML[TC]@ml) for all
      // variables. Finally for local-exec and initial-exec, we have a thread
      // pointer, in r13 for 64-bit mode and returned by .__get_tpointer for
      // 32-bit mode.
      if (Kind == MCSymbolRefExpr::VariantKind::VK_PPC_AIX_TLSGD ||
          Kind == MCSymbolRefExpr::VariantKind::VK_PPC_AIX_TLSGDM ||
          Kind == MCSymbolRefExpr::VariantKind::VK_PPC_AIX_TLSIE ||
          Kind == MCSymbolRefExpr::VariantKind::VK_PPC_AIX_TLSLE ||
          Kind == MCSymbolRefExpr::VariantKind::VK_PPC_AIX_TLSLD ||
          Kind == MCSymbolRefExpr::VariantKind::VK_PPC_AIX_TLSML)
        OS << "\t.tc " << TCSym->getName() << "," << XSym->getName() << "@"
           << MCSymbolRefExpr::getVariantKindName(Kind) << '\n';
      else
        OS << "\t.tc " << TCSym->getName() << "," << XSym->getName() << '\n';

      if (TCSym->hasRename())
        Streamer.emitXCOFFRenameDirective(TCSym, TCSym->getSymbolTableName());
      return;
    }

    OS << "\t.tc " << S.getName() << "[TC]," << S.getName() << '\n';
  }

  void emitMachine(StringRef CPU) override {
    OS << "\t.machine " << CPU << '\n';
  }

  void emitAbiVersion(int AbiVersion) override {
    OS << "\t.abiversion " << AbiVersion << '\n';
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {
    const MCAsmInfo *MAI = Streamer.getContext().getAsmInfo();

    OS << "\t.localentry\t";
    S->print(OS, MAI);
    OS << ", ";
    LocalOffset->print(OS, MAI);
    OS << '\n';
  }
};

class PPCTargetELFStreamer : public PPCTargetStreamer {
public:
  PPCTargetELFStreamer(MCStreamer &S) : PPCTargetStreamer(S) {}

  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }

  void emitTCEntry(const MCSymbol &S,
                   MCSymbolRefExpr::VariantKind Kind) override {
    // Creates a R_PPC64_TOC relocation
    Streamer.emitValueToAlignment(Align(8));
    Streamer.emitSymbolValue(&S, 8);
  }

  void emitMachine(StringRef CPU) override {
    // FIXME: Is there anything to do in here or does this directive only
    // limit the parser?
  }

  void emitAbiVersion(int AbiVersion) override {
    ELFObjectWriter &W = getStreamer().getWriter();
    unsigned Flags = W.getELFHeaderEFlags();
    Flags &= ~ELF::EF_PPC64_ABI;
    Flags |= (AbiVersion & ELF::EF_PPC64_ABI);
    W.setELFHeaderEFlags(Flags);
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {

    // encodePPC64LocalEntryOffset will report an error if it cannot
    // encode LocalOffset.
    unsigned Encoded = encodePPC64LocalEntryOffset(LocalOffset);

    unsigned Other = S->getOther();
    Other &= ~ELF::STO_PPC64_LOCAL_MASK;
    Other |= Encoded;
    S->setOther(Other);

    // For GAS compatibility, unless we already saw a .abiversion directive,
    // set e_flags to indicate ELFv2 ABI.
    ELFObjectWriter &W = getStreamer().getWriter();
    unsigned Flags = W.getELFHeaderEFlags();
    if ((Flags & ELF::EF_PPC64_ABI) == 0)
      W.setELFHeaderEFlags(Flags | 2);
  }

  void emitAssignment(MCSymbol *S, const MCExpr *Value) override {
    auto *Symbol = cast<MCSymbolELF>(S);

    // When encoding an assignment to set symbol A to symbol B, also copy
    // the st_other bits encoding the local entry point offset.
    if (copyLocalEntry(Symbol, Value))
      UpdateOther.insert(Symbol);
    else
      UpdateOther.erase(Symbol);
  }

  void finish() override {
    for (auto *Sym : UpdateOther)
      if (Sym->isVariable())
        copyLocalEntry(Sym, Sym->getVariableValue());

    // Clear the set of symbols that needs to be updated so the streamer can
    // be reused without issues.
    UpdateOther.clear();
  }

private:
  SmallPtrSet<MCSymbolELF *, 32> UpdateOther;

  bool copyLocalEntry(MCSymbolELF *D, const MCExpr *S) {
    auto *Ref = dyn_cast<const MCSymbolRefExpr>(S);
    if (!Ref)
      return false;
    const auto &RhsSym = cast<MCSymbolELF>(Ref->getSymbol());
    unsigned Other = D->getOther();
    Other &= ~ELF::STO_PPC64_LOCAL_MASK;
    Other |= RhsSym.getOther() & ELF::STO_PPC64_LOCAL_MASK;
    D->setOther(Other);
    return true;
  }

  unsigned encodePPC64LocalEntryOffset(const MCExpr *LocalOffset) {
    MCAssembler &MCA = getStreamer().getAssembler();
    int64_t Offset;
    if (!LocalOffset->evaluateAsAbsolute(Offset, MCA))
      MCA.getContext().reportError(LocalOffset->getLoc(),
                                   ".localentry expression must be absolute");

    switch (Offset) {
    default:
      MCA.getContext().reportError(
          LocalOffset->getLoc(), ".localentry expression must be a power of 2");
      return 0;
    case 0:
      return 0;
    case 1:
      return 1 << ELF::STO_PPC64_LOCAL_BIT;
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
      return Log2_32(Offset) << ELF::STO_PPC64_LOCAL_BIT;
    }
  }
};

class PPCTargetMachOStreamer : public PPCTargetStreamer {
public:
  PPCTargetMachOStreamer(MCStreamer &S) : PPCTargetStreamer(S) {}

  void emitTCEntry(const MCSymbol &S,
                   MCSymbolRefExpr::VariantKind Kind) override {
    llvm_unreachable("Unknown pseudo-op: .tc");
  }

  void emitMachine(StringRef CPU) override {
    // FIXME: We should update the CPUType, CPUSubType in the Object file if
    // the new values are different from the defaults.
  }

  void emitAbiVersion(int AbiVersion) override {
    llvm_unreachable("Unknown pseudo-op: .abiversion");
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {
    llvm_unreachable("Unknown pseudo-op: .localentry");
  }
};

class PPCTargetXCOFFStreamer : public PPCTargetStreamer {
public:
  PPCTargetXCOFFStreamer(MCStreamer &S) : PPCTargetStreamer(S) {}

  void emitTCEntry(const MCSymbol &S,
                   MCSymbolRefExpr::VariantKind Kind) override {
    const MCAsmInfo *MAI = Streamer.getContext().getAsmInfo();
    const unsigned PointerSize = MAI->getCodePointerSize();
    Streamer.emitValueToAlignment(Align(PointerSize));
    Streamer.emitValue(MCSymbolRefExpr::create(&S, Kind, Streamer.getContext()),
                       PointerSize);
  }

  void emitMachine(StringRef CPU) override {
    llvm_unreachable("Machine pseudo-ops are invalid for XCOFF.");
  }

  void emitAbiVersion(int AbiVersion) override {
    llvm_unreachable("ABI-version pseudo-ops are invalid for XCOFF.");
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {
    llvm_unreachable("Local-entry pseudo-ops are invalid for XCOFF.");
  }
};

} // end anonymous namespace

static MCTargetStreamer *createAsmTargetStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint) {
  return new PPCTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) {
  return new PPCTargetStreamer(S);
}

static MCTargetStreamer *
createObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new PPCTargetELFStreamer(S);
  if (TT.isOSBinFormatXCOFF())
    return new PPCTargetXCOFFStreamer(S);
  return new PPCTargetMachOStreamer(S);
}

static MCInstPrinter *createPPCMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  return new PPCInstPrinter(MAI, MII, MRI, T);
}

namespace {

class PPCMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit PPCMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    unsigned NumOps = Inst.getNumOperands();
    if (NumOps == 0 ||
        Info->get(Inst.getOpcode()).operands()[NumOps - 1].OperandType !=
            MCOI::OPERAND_PCREL)
      return false;
    Target = Addr + Inst.getOperand(NumOps - 1).getImm() * Size;
    return true;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createPPCMCInstrAnalysis(const MCInstrInfo *Info) {
  return new PPCMCInstrAnalysis(Info);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializePowerPCTargetMC() {
  for (Target *T : {&getThePPC32Target(), &getThePPC32LETarget(),
                    &getThePPC64Target(), &getThePPC64LETarget()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn C(*T, createPPCMCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createPPCMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createPPCMCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createPPCMCSubtargetInfo);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createPPCMCInstrAnalysis);

    // Register the MC Code Emitter
    TargetRegistry::RegisterMCCodeEmitter(*T, createPPCMCCodeEmitter);

    // Register the asm backend.
    TargetRegistry::RegisterMCAsmBackend(*T, createPPCAsmBackend);

    // Register the elf streamer.
    TargetRegistry::RegisterELFStreamer(*T, createPPCELFStreamer);

    // Register the XCOFF streamer.
    TargetRegistry::RegisterXCOFFStreamer(*T, createPPCXCOFFStreamer);

    // Register the object target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createObjectTargetStreamer);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createAsmTargetStreamer);

    // Register the null target streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T, createNullTargetStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createPPCMCInstPrinter);
  }
}
