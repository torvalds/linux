//===-- LoongArchMCTargetDesc.cpp - LoongArch Target Descriptions ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides LoongArch specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "LoongArchMCTargetDesc.h"
#include "LoongArchBaseInfo.h"
#include "LoongArchELFStreamer.h"
#include "LoongArchInstPrinter.h"
#include "LoongArchMCAsmInfo.h"
#include "TargetInfo/LoongArchTargetInfo.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "LoongArchGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "LoongArchGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "LoongArchGenSubtargetInfo.inc"

using namespace llvm;

static MCRegisterInfo *createLoongArchMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitLoongArchMCRegisterInfo(X, LoongArch::R1);
  return X;
}

static MCInstrInfo *createLoongArchMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitLoongArchMCInstrInfo(X);
  return X;
}

static MCSubtargetInfo *
createLoongArchMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty() || CPU == "generic")
    CPU = TT.isArch64Bit() ? "generic-la64" : "generic-la32";
  return createLoongArchMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCAsmInfo *createLoongArchMCAsmInfo(const MCRegisterInfo &MRI,
                                           const Triple &TT,
                                           const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new LoongArchMCAsmInfo(TT);

  // Initial state of the frame pointer is sp(r3).
  MCRegister SP = MRI.getDwarfRegNum(LoongArch::R3, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createLoongArchMCInstPrinter(const Triple &T,
                                                   unsigned SyntaxVariant,
                                                   const MCAsmInfo &MAI,
                                                   const MCInstrInfo &MII,
                                                   const MCRegisterInfo &MRI) {
  return new LoongArchInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *
createLoongArchObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return STI.getTargetTriple().isOSBinFormatELF()
             ? new LoongArchTargetELFStreamer(S, STI)
             : nullptr;
}

namespace {

class LoongArchMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit LoongArchMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    unsigned NumOps = Inst.getNumOperands();
    if ((isBranch(Inst) && !isIndirectBranch(Inst)) ||
        Inst.getOpcode() == LoongArch::BL) {
      Target = Addr + Inst.getOperand(NumOps - 1).getImm();
      return true;
    }

    return false;
  }

  bool isTerminator(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isTerminator(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case LoongArch::JIRL:
      return Inst.getOperand(0).getReg() == LoongArch::R0;
    }
  }

  bool isCall(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isCall(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case LoongArch::JIRL:
      return Inst.getOperand(0).getReg() != LoongArch::R0;
    }
  }

  bool isReturn(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isReturn(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case LoongArch::JIRL:
      return Inst.getOperand(0).getReg() == LoongArch::R0 &&
             Inst.getOperand(1).getReg() == LoongArch::R1;
    }
  }

  bool isBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isBranch(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case LoongArch::JIRL:
      return Inst.getOperand(0).getReg() == LoongArch::R0 &&
             Inst.getOperand(1).getReg() != LoongArch::R1;
    }
  }

  bool isUnconditionalBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isUnconditionalBranch(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case LoongArch::JIRL:
      return Inst.getOperand(0).getReg() == LoongArch::R0 &&
             Inst.getOperand(1).getReg() != LoongArch::R1;
    }
  }

  bool isIndirectBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isIndirectBranch(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case LoongArch::JIRL:
      return Inst.getOperand(0).getReg() == LoongArch::R0 &&
             Inst.getOperand(1).getReg() != LoongArch::R1;
    }
  }
};

} // end namespace

static MCInstrAnalysis *createLoongArchInstrAnalysis(const MCInstrInfo *Info) {
  return new LoongArchMCInstrAnalysis(Info);
}

namespace {
MCStreamer *createLoongArchELFStreamer(const Triple &T, MCContext &Context,
                                       std::unique_ptr<MCAsmBackend> &&MAB,
                                       std::unique_ptr<MCObjectWriter> &&MOW,
                                       std::unique_ptr<MCCodeEmitter> &&MCE) {
  return createLoongArchELFStreamer(Context, std::move(MAB), std::move(MOW),
                                    std::move(MCE));
}
} // end namespace

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLoongArchTargetMC() {
  for (Target *T : {&getTheLoongArch32Target(), &getTheLoongArch64Target()}) {
    TargetRegistry::RegisterMCRegInfo(*T, createLoongArchMCRegisterInfo);
    TargetRegistry::RegisterMCInstrInfo(*T, createLoongArchMCInstrInfo);
    TargetRegistry::RegisterMCSubtargetInfo(*T, createLoongArchMCSubtargetInfo);
    TargetRegistry::RegisterMCAsmInfo(*T, createLoongArchMCAsmInfo);
    TargetRegistry::RegisterMCCodeEmitter(*T, createLoongArchMCCodeEmitter);
    TargetRegistry::RegisterMCAsmBackend(*T, createLoongArchAsmBackend);
    TargetRegistry::RegisterMCInstPrinter(*T, createLoongArchMCInstPrinter);
    TargetRegistry::RegisterMCInstrAnalysis(*T, createLoongArchInstrAnalysis);
    TargetRegistry::RegisterELFStreamer(*T, createLoongArchELFStreamer);
    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createLoongArchObjectTargetStreamer);
  }
}
