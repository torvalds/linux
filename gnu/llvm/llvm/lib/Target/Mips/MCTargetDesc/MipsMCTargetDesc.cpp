//===-- MipsMCTargetDesc.cpp - Mips Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Mips specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MipsMCTargetDesc.h"
#include "MipsAsmBackend.h"
#include "MipsBaseInfo.h"
#include "MipsELFStreamer.h"
#include "MipsInstPrinter.h"
#include "MipsMCAsmInfo.h"
#include "MipsMCNaCl.h"
#include "MipsTargetStreamer.h"
#include "TargetInfo/MipsTargetInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "MipsGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MipsGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MipsGenRegisterInfo.inc"

/// Select the Mips CPU for the given triple and cpu name.
StringRef MIPS_MC::selectMipsCPU(const Triple &TT, StringRef CPU) {
  if (CPU.empty() || CPU == "generic") {
    if (TT.getSubArch() == llvm::Triple::MipsSubArch_r6) {
      if (TT.isMIPS32())
        CPU = "mips32r6";
      else
        CPU = "mips64r6";
    } else {
      if (TT.isMIPS32())
        CPU = "mips32";
      else
        CPU = "mips64";
    }
  }
  return CPU;
}

static MCInstrInfo *createMipsMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitMipsMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMipsMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitMipsMCRegisterInfo(X, Mips::RA);
  return X;
}

static MCSubtargetInfo *createMipsMCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  CPU = MIPS_MC::selectMipsCPU(TT, CPU);
  return createMipsMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCAsmInfo *createMipsMCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new MipsMCAsmInfo(TT, Options);

  unsigned SP = MRI.getDwarfRegNum(Mips::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfaRegister(nullptr, SP);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createMipsMCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new MipsInstPrinter(MAI, MII, MRI);
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  MCStreamer *S;
  if (!T.isOSNaCl())
    S = createMipsELFStreamer(Context, std::move(MAB), std::move(OW),
                              std::move(Emitter));
  else
    S = createMipsNaClELFStreamer(Context, std::move(MAB), std::move(OW),
                                  std::move(Emitter));
  return S;
}

static MCTargetStreamer *createMipsAsmTargetStreamer(MCStreamer &S,
                                                     formatted_raw_ostream &OS,
                                                     MCInstPrinter *InstPrint) {
  return new MipsTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createMipsNullTargetStreamer(MCStreamer &S) {
  return new MipsTargetStreamer(S);
}

static MCTargetStreamer *
createMipsObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new MipsTargetELFStreamer(S, STI);
}

namespace {

class MipsMCInstrAnalysis : public MCInstrAnalysis {
public:
  MipsMCInstrAnalysis(const MCInstrInfo *Info) : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    unsigned NumOps = Inst.getNumOperands();
    if (NumOps == 0)
      return false;
    switch (Info->get(Inst.getOpcode()).operands()[NumOps - 1].OperandType) {
    case MCOI::OPERAND_UNKNOWN:
    case MCOI::OPERAND_IMMEDIATE: {
      // j, jal, jalx, jals
      // Absolute branch within the current 256 MB-aligned region
      uint64_t Region = Addr & ~uint64_t(0xfffffff);
      Target = Region + Inst.getOperand(NumOps - 1).getImm();
      return true;
    }
    case MCOI::OPERAND_PCREL:
      // b, beq ...
      Target = Addr + Inst.getOperand(NumOps - 1).getImm();
      return true;
    default:
      return false;
    }
  }
};
}

static MCInstrAnalysis *createMipsMCInstrAnalysis(const MCInstrInfo *Info) {
  return new MipsMCInstrAnalysis(Info);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMipsTargetMC() {
  for (Target *T : {&getTheMipsTarget(), &getTheMipselTarget(),
                    &getTheMips64Target(), &getTheMips64elTarget()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createMipsMCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createMipsMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createMipsMCRegisterInfo);

    // Register the elf streamer.
    TargetRegistry::RegisterELFStreamer(*T, createMCStreamer);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createMipsAsmTargetStreamer);

    TargetRegistry::RegisterNullTargetStreamer(*T,
                                               createMipsNullTargetStreamer);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createMipsMCSubtargetInfo);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createMipsMCInstrAnalysis);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createMipsMCInstPrinter);

    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createMipsObjectTargetStreamer);

    // Register the asm backend.
    TargetRegistry::RegisterMCAsmBackend(*T, createMipsAsmBackend);
  }

  // Register the MC Code Emitter
  for (Target *T : {&getTheMipsTarget(), &getTheMips64Target()})
    TargetRegistry::RegisterMCCodeEmitter(*T, createMipsMCCodeEmitterEB);

  for (Target *T : {&getTheMipselTarget(), &getTheMips64elTarget()})
    TargetRegistry::RegisterMCCodeEmitter(*T, createMipsMCCodeEmitterEL);
}
