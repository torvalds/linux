//===-- AMDGPUMCTargetDesc.cpp - AMDGPU Target Descriptions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides AMDGPU specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMCTargetDesc.h"
#include "AMDGPUELFStreamer.h"
#include "AMDGPUInstPrinter.h"
#include "AMDGPUMCAsmInfo.h"
#include "AMDGPUTargetStreamer.h"
#include "R600InstPrinter.h"
#include "R600MCTargetDesc.h"
#include "TargetInfo/AMDGPUTargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "AMDGPUGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AMDGPUGenSubtargetInfo.inc"

#define NoSchedModel NoSchedModelR600
#define GET_SUBTARGETINFO_MC_DESC
#include "R600GenSubtargetInfo.inc"
#undef NoSchedModelR600

#define GET_REGINFO_MC_DESC
#include "AMDGPUGenRegisterInfo.inc"

#define GET_REGINFO_MC_DESC
#include "R600GenRegisterInfo.inc"

static MCInstrInfo *createAMDGPUMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAMDGPUMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createAMDGPUMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  if (TT.getArch() == Triple::r600)
    InitR600MCRegisterInfo(X, 0);
  else
    InitAMDGPUMCRegisterInfo(X, AMDGPU::PC_REG);
  return X;
}

MCRegisterInfo *llvm::createGCNMCRegisterInfo(AMDGPUDwarfFlavour DwarfFlavour) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAMDGPUMCRegisterInfo(X, AMDGPU::PC_REG, DwarfFlavour, DwarfFlavour);
  return X;
}

static MCSubtargetInfo *
createAMDGPUMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (TT.getArch() == Triple::r600)
    return createR600MCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
  return createAMDGPUMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCInstPrinter *createAMDGPUMCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  if (T.getArch() == Triple::r600)
    return new R600InstPrinter(MAI, MII, MRI);
  return new AMDGPUInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *
createAMDGPUAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                              MCInstPrinter *InstPrint) {
  return new AMDGPUTargetAsmStreamer(S, OS);
}

static MCTargetStreamer * createAMDGPUObjectTargetStreamer(
                                                   MCStreamer &S,
                                                   const MCSubtargetInfo &STI) {
  return new AMDGPUTargetELFStreamer(S, STI);
}

static MCTargetStreamer *createAMDGPUNullTargetStreamer(MCStreamer &S) {
  return new AMDGPUTargetStreamer(S);
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createAMDGPUELFStreamer(T, Context, std::move(MAB), std::move(OW),
                                 std::move(Emitter));
}

namespace {

class AMDGPUMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit AMDGPUMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    if (Inst.getNumOperands() == 0 || !Inst.getOperand(0).isImm() ||
        Info->get(Inst.getOpcode()).operands()[0].OperandType !=
            MCOI::OPERAND_PCREL)
      return false;

    int64_t Imm = Inst.getOperand(0).getImm();
    // Our branches take a simm16, but we need two extra bits to account for
    // the factor of 4.
    APInt SignedOffset(18, Imm * 4, true);
    Target = (SignedOffset.sext(64) + Addr + Size).getZExtValue();
    return true;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createAMDGPUMCInstrAnalysis(const MCInstrInfo *Info) {
  return new AMDGPUMCInstrAnalysis(Info);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUTargetMC() {

  TargetRegistry::RegisterMCInstrInfo(getTheGCNTarget(), createAMDGPUMCInstrInfo);
  TargetRegistry::RegisterMCInstrInfo(getTheR600Target(),
                                      createR600MCInstrInfo);
  for (Target *T : {&getTheR600Target(), &getTheGCNTarget()}) {
    RegisterMCAsmInfo<AMDGPUMCAsmInfo> X(*T);

    TargetRegistry::RegisterMCRegInfo(*T, createAMDGPUMCRegisterInfo);
    TargetRegistry::RegisterMCSubtargetInfo(*T, createAMDGPUMCSubtargetInfo);
    TargetRegistry::RegisterMCInstPrinter(*T, createAMDGPUMCInstPrinter);
    TargetRegistry::RegisterMCInstrAnalysis(*T, createAMDGPUMCInstrAnalysis);
    TargetRegistry::RegisterMCAsmBackend(*T, createAMDGPUAsmBackend);
    TargetRegistry::RegisterELFStreamer(*T, createMCStreamer);
  }

  // R600 specific registration
  TargetRegistry::RegisterMCCodeEmitter(getTheR600Target(),
                                        createR600MCCodeEmitter);
  TargetRegistry::RegisterObjectTargetStreamer(
      getTheR600Target(), createAMDGPUObjectTargetStreamer);

  // GCN specific registration
  TargetRegistry::RegisterMCCodeEmitter(getTheGCNTarget(),
                                        createAMDGPUMCCodeEmitter);

  TargetRegistry::RegisterAsmTargetStreamer(getTheGCNTarget(),
                                            createAMDGPUAsmTargetStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(
      getTheGCNTarget(), createAMDGPUObjectTargetStreamer);
  TargetRegistry::RegisterNullTargetStreamer(getTheGCNTarget(),
                                             createAMDGPUNullTargetStreamer);
}
