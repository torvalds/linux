//===-- AArch64MCTargetDesc.cpp - AArch64 Target Descriptions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides AArch64 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AArch64MCTargetDesc.h"
#include "AArch64ELFStreamer.h"
#include "AArch64MCAsmInfo.h"
#include "AArch64WinCOFFStreamer.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64InstPrinter.h"
#include "TargetInfo/AArch64TargetInfo.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/AArch64TargetParser.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "AArch64GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AArch64GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "AArch64GenRegisterInfo.inc"

static MCInstrInfo *createAArch64MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAArch64MCInstrInfo(X);
  return X;
}

static MCSubtargetInfo *
createAArch64MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  CPU = AArch64::resolveCPUAlias(CPU);

  if (CPU.empty()) {
    CPU = "generic";
    if (FS.empty())
      FS = "+v8a";

    if (TT.isArm64e())
      CPU = "apple-a12";
  }

  return createAArch64MCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

void AArch64_MC::initLLVMToCVRegMapping(MCRegisterInfo *MRI) {
  // Mapping from CodeView to MC register id.
  static const struct {
    codeview::RegisterId CVReg;
    MCPhysReg Reg;
  } RegMap[] = {
      {codeview::RegisterId::ARM64_W0, AArch64::W0},
      {codeview::RegisterId::ARM64_W1, AArch64::W1},
      {codeview::RegisterId::ARM64_W2, AArch64::W2},
      {codeview::RegisterId::ARM64_W3, AArch64::W3},
      {codeview::RegisterId::ARM64_W4, AArch64::W4},
      {codeview::RegisterId::ARM64_W5, AArch64::W5},
      {codeview::RegisterId::ARM64_W6, AArch64::W6},
      {codeview::RegisterId::ARM64_W7, AArch64::W7},
      {codeview::RegisterId::ARM64_W8, AArch64::W8},
      {codeview::RegisterId::ARM64_W9, AArch64::W9},
      {codeview::RegisterId::ARM64_W10, AArch64::W10},
      {codeview::RegisterId::ARM64_W11, AArch64::W11},
      {codeview::RegisterId::ARM64_W12, AArch64::W12},
      {codeview::RegisterId::ARM64_W13, AArch64::W13},
      {codeview::RegisterId::ARM64_W14, AArch64::W14},
      {codeview::RegisterId::ARM64_W15, AArch64::W15},
      {codeview::RegisterId::ARM64_W16, AArch64::W16},
      {codeview::RegisterId::ARM64_W17, AArch64::W17},
      {codeview::RegisterId::ARM64_W18, AArch64::W18},
      {codeview::RegisterId::ARM64_W19, AArch64::W19},
      {codeview::RegisterId::ARM64_W20, AArch64::W20},
      {codeview::RegisterId::ARM64_W21, AArch64::W21},
      {codeview::RegisterId::ARM64_W22, AArch64::W22},
      {codeview::RegisterId::ARM64_W23, AArch64::W23},
      {codeview::RegisterId::ARM64_W24, AArch64::W24},
      {codeview::RegisterId::ARM64_W25, AArch64::W25},
      {codeview::RegisterId::ARM64_W26, AArch64::W26},
      {codeview::RegisterId::ARM64_W27, AArch64::W27},
      {codeview::RegisterId::ARM64_W28, AArch64::W28},
      {codeview::RegisterId::ARM64_W29, AArch64::W29},
      {codeview::RegisterId::ARM64_W30, AArch64::W30},
      {codeview::RegisterId::ARM64_WZR, AArch64::WZR},
      {codeview::RegisterId::ARM64_X0, AArch64::X0},
      {codeview::RegisterId::ARM64_X1, AArch64::X1},
      {codeview::RegisterId::ARM64_X2, AArch64::X2},
      {codeview::RegisterId::ARM64_X3, AArch64::X3},
      {codeview::RegisterId::ARM64_X4, AArch64::X4},
      {codeview::RegisterId::ARM64_X5, AArch64::X5},
      {codeview::RegisterId::ARM64_X6, AArch64::X6},
      {codeview::RegisterId::ARM64_X7, AArch64::X7},
      {codeview::RegisterId::ARM64_X8, AArch64::X8},
      {codeview::RegisterId::ARM64_X9, AArch64::X9},
      {codeview::RegisterId::ARM64_X10, AArch64::X10},
      {codeview::RegisterId::ARM64_X11, AArch64::X11},
      {codeview::RegisterId::ARM64_X12, AArch64::X12},
      {codeview::RegisterId::ARM64_X13, AArch64::X13},
      {codeview::RegisterId::ARM64_X14, AArch64::X14},
      {codeview::RegisterId::ARM64_X15, AArch64::X15},
      {codeview::RegisterId::ARM64_X16, AArch64::X16},
      {codeview::RegisterId::ARM64_X17, AArch64::X17},
      {codeview::RegisterId::ARM64_X18, AArch64::X18},
      {codeview::RegisterId::ARM64_X19, AArch64::X19},
      {codeview::RegisterId::ARM64_X20, AArch64::X20},
      {codeview::RegisterId::ARM64_X21, AArch64::X21},
      {codeview::RegisterId::ARM64_X22, AArch64::X22},
      {codeview::RegisterId::ARM64_X23, AArch64::X23},
      {codeview::RegisterId::ARM64_X24, AArch64::X24},
      {codeview::RegisterId::ARM64_X25, AArch64::X25},
      {codeview::RegisterId::ARM64_X26, AArch64::X26},
      {codeview::RegisterId::ARM64_X27, AArch64::X27},
      {codeview::RegisterId::ARM64_X28, AArch64::X28},
      {codeview::RegisterId::ARM64_FP, AArch64::FP},
      {codeview::RegisterId::ARM64_LR, AArch64::LR},
      {codeview::RegisterId::ARM64_SP, AArch64::SP},
      {codeview::RegisterId::ARM64_ZR, AArch64::XZR},
      {codeview::RegisterId::ARM64_NZCV, AArch64::NZCV},
      {codeview::RegisterId::ARM64_S0, AArch64::S0},
      {codeview::RegisterId::ARM64_S1, AArch64::S1},
      {codeview::RegisterId::ARM64_S2, AArch64::S2},
      {codeview::RegisterId::ARM64_S3, AArch64::S3},
      {codeview::RegisterId::ARM64_S4, AArch64::S4},
      {codeview::RegisterId::ARM64_S5, AArch64::S5},
      {codeview::RegisterId::ARM64_S6, AArch64::S6},
      {codeview::RegisterId::ARM64_S7, AArch64::S7},
      {codeview::RegisterId::ARM64_S8, AArch64::S8},
      {codeview::RegisterId::ARM64_S9, AArch64::S9},
      {codeview::RegisterId::ARM64_S10, AArch64::S10},
      {codeview::RegisterId::ARM64_S11, AArch64::S11},
      {codeview::RegisterId::ARM64_S12, AArch64::S12},
      {codeview::RegisterId::ARM64_S13, AArch64::S13},
      {codeview::RegisterId::ARM64_S14, AArch64::S14},
      {codeview::RegisterId::ARM64_S15, AArch64::S15},
      {codeview::RegisterId::ARM64_S16, AArch64::S16},
      {codeview::RegisterId::ARM64_S17, AArch64::S17},
      {codeview::RegisterId::ARM64_S18, AArch64::S18},
      {codeview::RegisterId::ARM64_S19, AArch64::S19},
      {codeview::RegisterId::ARM64_S20, AArch64::S20},
      {codeview::RegisterId::ARM64_S21, AArch64::S21},
      {codeview::RegisterId::ARM64_S22, AArch64::S22},
      {codeview::RegisterId::ARM64_S23, AArch64::S23},
      {codeview::RegisterId::ARM64_S24, AArch64::S24},
      {codeview::RegisterId::ARM64_S25, AArch64::S25},
      {codeview::RegisterId::ARM64_S26, AArch64::S26},
      {codeview::RegisterId::ARM64_S27, AArch64::S27},
      {codeview::RegisterId::ARM64_S28, AArch64::S28},
      {codeview::RegisterId::ARM64_S29, AArch64::S29},
      {codeview::RegisterId::ARM64_S30, AArch64::S30},
      {codeview::RegisterId::ARM64_S31, AArch64::S31},
      {codeview::RegisterId::ARM64_D0, AArch64::D0},
      {codeview::RegisterId::ARM64_D1, AArch64::D1},
      {codeview::RegisterId::ARM64_D2, AArch64::D2},
      {codeview::RegisterId::ARM64_D3, AArch64::D3},
      {codeview::RegisterId::ARM64_D4, AArch64::D4},
      {codeview::RegisterId::ARM64_D5, AArch64::D5},
      {codeview::RegisterId::ARM64_D6, AArch64::D6},
      {codeview::RegisterId::ARM64_D7, AArch64::D7},
      {codeview::RegisterId::ARM64_D8, AArch64::D8},
      {codeview::RegisterId::ARM64_D9, AArch64::D9},
      {codeview::RegisterId::ARM64_D10, AArch64::D10},
      {codeview::RegisterId::ARM64_D11, AArch64::D11},
      {codeview::RegisterId::ARM64_D12, AArch64::D12},
      {codeview::RegisterId::ARM64_D13, AArch64::D13},
      {codeview::RegisterId::ARM64_D14, AArch64::D14},
      {codeview::RegisterId::ARM64_D15, AArch64::D15},
      {codeview::RegisterId::ARM64_D16, AArch64::D16},
      {codeview::RegisterId::ARM64_D17, AArch64::D17},
      {codeview::RegisterId::ARM64_D18, AArch64::D18},
      {codeview::RegisterId::ARM64_D19, AArch64::D19},
      {codeview::RegisterId::ARM64_D20, AArch64::D20},
      {codeview::RegisterId::ARM64_D21, AArch64::D21},
      {codeview::RegisterId::ARM64_D22, AArch64::D22},
      {codeview::RegisterId::ARM64_D23, AArch64::D23},
      {codeview::RegisterId::ARM64_D24, AArch64::D24},
      {codeview::RegisterId::ARM64_D25, AArch64::D25},
      {codeview::RegisterId::ARM64_D26, AArch64::D26},
      {codeview::RegisterId::ARM64_D27, AArch64::D27},
      {codeview::RegisterId::ARM64_D28, AArch64::D28},
      {codeview::RegisterId::ARM64_D29, AArch64::D29},
      {codeview::RegisterId::ARM64_D30, AArch64::D30},
      {codeview::RegisterId::ARM64_D31, AArch64::D31},
      {codeview::RegisterId::ARM64_Q0, AArch64::Q0},
      {codeview::RegisterId::ARM64_Q1, AArch64::Q1},
      {codeview::RegisterId::ARM64_Q2, AArch64::Q2},
      {codeview::RegisterId::ARM64_Q3, AArch64::Q3},
      {codeview::RegisterId::ARM64_Q4, AArch64::Q4},
      {codeview::RegisterId::ARM64_Q5, AArch64::Q5},
      {codeview::RegisterId::ARM64_Q6, AArch64::Q6},
      {codeview::RegisterId::ARM64_Q7, AArch64::Q7},
      {codeview::RegisterId::ARM64_Q8, AArch64::Q8},
      {codeview::RegisterId::ARM64_Q9, AArch64::Q9},
      {codeview::RegisterId::ARM64_Q10, AArch64::Q10},
      {codeview::RegisterId::ARM64_Q11, AArch64::Q11},
      {codeview::RegisterId::ARM64_Q12, AArch64::Q12},
      {codeview::RegisterId::ARM64_Q13, AArch64::Q13},
      {codeview::RegisterId::ARM64_Q14, AArch64::Q14},
      {codeview::RegisterId::ARM64_Q15, AArch64::Q15},
      {codeview::RegisterId::ARM64_Q16, AArch64::Q16},
      {codeview::RegisterId::ARM64_Q17, AArch64::Q17},
      {codeview::RegisterId::ARM64_Q18, AArch64::Q18},
      {codeview::RegisterId::ARM64_Q19, AArch64::Q19},
      {codeview::RegisterId::ARM64_Q20, AArch64::Q20},
      {codeview::RegisterId::ARM64_Q21, AArch64::Q21},
      {codeview::RegisterId::ARM64_Q22, AArch64::Q22},
      {codeview::RegisterId::ARM64_Q23, AArch64::Q23},
      {codeview::RegisterId::ARM64_Q24, AArch64::Q24},
      {codeview::RegisterId::ARM64_Q25, AArch64::Q25},
      {codeview::RegisterId::ARM64_Q26, AArch64::Q26},
      {codeview::RegisterId::ARM64_Q27, AArch64::Q27},
      {codeview::RegisterId::ARM64_Q28, AArch64::Q28},
      {codeview::RegisterId::ARM64_Q29, AArch64::Q29},
      {codeview::RegisterId::ARM64_Q30, AArch64::Q30},
      {codeview::RegisterId::ARM64_Q31, AArch64::Q31},
      {codeview::RegisterId::ARM64_B0, AArch64::B0},
      {codeview::RegisterId::ARM64_B1, AArch64::B1},
      {codeview::RegisterId::ARM64_B2, AArch64::B2},
      {codeview::RegisterId::ARM64_B3, AArch64::B3},
      {codeview::RegisterId::ARM64_B4, AArch64::B4},
      {codeview::RegisterId::ARM64_B5, AArch64::B5},
      {codeview::RegisterId::ARM64_B6, AArch64::B6},
      {codeview::RegisterId::ARM64_B7, AArch64::B7},
      {codeview::RegisterId::ARM64_B8, AArch64::B8},
      {codeview::RegisterId::ARM64_B9, AArch64::B9},
      {codeview::RegisterId::ARM64_B10, AArch64::B10},
      {codeview::RegisterId::ARM64_B11, AArch64::B11},
      {codeview::RegisterId::ARM64_B12, AArch64::B12},
      {codeview::RegisterId::ARM64_B13, AArch64::B13},
      {codeview::RegisterId::ARM64_B14, AArch64::B14},
      {codeview::RegisterId::ARM64_B15, AArch64::B15},
      {codeview::RegisterId::ARM64_B16, AArch64::B16},
      {codeview::RegisterId::ARM64_B17, AArch64::B17},
      {codeview::RegisterId::ARM64_B18, AArch64::B18},
      {codeview::RegisterId::ARM64_B19, AArch64::B19},
      {codeview::RegisterId::ARM64_B20, AArch64::B20},
      {codeview::RegisterId::ARM64_B21, AArch64::B21},
      {codeview::RegisterId::ARM64_B22, AArch64::B22},
      {codeview::RegisterId::ARM64_B23, AArch64::B23},
      {codeview::RegisterId::ARM64_B24, AArch64::B24},
      {codeview::RegisterId::ARM64_B25, AArch64::B25},
      {codeview::RegisterId::ARM64_B26, AArch64::B26},
      {codeview::RegisterId::ARM64_B27, AArch64::B27},
      {codeview::RegisterId::ARM64_B28, AArch64::B28},
      {codeview::RegisterId::ARM64_B29, AArch64::B29},
      {codeview::RegisterId::ARM64_B30, AArch64::B30},
      {codeview::RegisterId::ARM64_B31, AArch64::B31},
      {codeview::RegisterId::ARM64_H0, AArch64::H0},
      {codeview::RegisterId::ARM64_H1, AArch64::H1},
      {codeview::RegisterId::ARM64_H2, AArch64::H2},
      {codeview::RegisterId::ARM64_H3, AArch64::H3},
      {codeview::RegisterId::ARM64_H4, AArch64::H4},
      {codeview::RegisterId::ARM64_H5, AArch64::H5},
      {codeview::RegisterId::ARM64_H6, AArch64::H6},
      {codeview::RegisterId::ARM64_H7, AArch64::H7},
      {codeview::RegisterId::ARM64_H8, AArch64::H8},
      {codeview::RegisterId::ARM64_H9, AArch64::H9},
      {codeview::RegisterId::ARM64_H10, AArch64::H10},
      {codeview::RegisterId::ARM64_H11, AArch64::H11},
      {codeview::RegisterId::ARM64_H12, AArch64::H12},
      {codeview::RegisterId::ARM64_H13, AArch64::H13},
      {codeview::RegisterId::ARM64_H14, AArch64::H14},
      {codeview::RegisterId::ARM64_H15, AArch64::H15},
      {codeview::RegisterId::ARM64_H16, AArch64::H16},
      {codeview::RegisterId::ARM64_H17, AArch64::H17},
      {codeview::RegisterId::ARM64_H18, AArch64::H18},
      {codeview::RegisterId::ARM64_H19, AArch64::H19},
      {codeview::RegisterId::ARM64_H20, AArch64::H20},
      {codeview::RegisterId::ARM64_H21, AArch64::H21},
      {codeview::RegisterId::ARM64_H22, AArch64::H22},
      {codeview::RegisterId::ARM64_H23, AArch64::H23},
      {codeview::RegisterId::ARM64_H24, AArch64::H24},
      {codeview::RegisterId::ARM64_H25, AArch64::H25},
      {codeview::RegisterId::ARM64_H26, AArch64::H26},
      {codeview::RegisterId::ARM64_H27, AArch64::H27},
      {codeview::RegisterId::ARM64_H28, AArch64::H28},
      {codeview::RegisterId::ARM64_H29, AArch64::H29},
      {codeview::RegisterId::ARM64_H30, AArch64::H30},
      {codeview::RegisterId::ARM64_H31, AArch64::H31},
  };
  for (const auto &I : RegMap)
    MRI->mapLLVMRegToCVReg(I.Reg, static_cast<int>(I.CVReg));
}

bool AArch64_MC::isHForm(const MCInst &MI, const MCInstrInfo *MCII) {
  const auto &FPR16 = AArch64MCRegisterClasses[AArch64::FPR16RegClassID];
  return llvm::any_of(MI, [&](const MCOperand &Op) {
    return Op.isReg() && FPR16.contains(Op.getReg());
  });
}

bool AArch64_MC::isQForm(const MCInst &MI, const MCInstrInfo *MCII) {
  const auto &FPR128 = AArch64MCRegisterClasses[AArch64::FPR128RegClassID];
  return llvm::any_of(MI, [&](const MCOperand &Op) {
    return Op.isReg() && FPR128.contains(Op.getReg());
  });
}

bool AArch64_MC::isFpOrNEON(const MCInst &MI, const MCInstrInfo *MCII) {
  const auto &FPR128 = AArch64MCRegisterClasses[AArch64::FPR128RegClassID];
  const auto &FPR64 = AArch64MCRegisterClasses[AArch64::FPR64RegClassID];
  const auto &FPR32 = AArch64MCRegisterClasses[AArch64::FPR32RegClassID];
  const auto &FPR16 = AArch64MCRegisterClasses[AArch64::FPR16RegClassID];
  const auto &FPR8 = AArch64MCRegisterClasses[AArch64::FPR8RegClassID];

  auto IsFPR = [&](const MCOperand &Op) {
    if (!Op.isReg())
      return false;
    auto Reg = Op.getReg();
    return FPR128.contains(Reg) || FPR64.contains(Reg) || FPR32.contains(Reg) ||
           FPR16.contains(Reg) || FPR8.contains(Reg);
  };

  return llvm::any_of(MI, IsFPR);
}

static MCRegisterInfo *createAArch64MCRegisterInfo(const Triple &Triple) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAArch64MCRegisterInfo(X, AArch64::LR);
  AArch64_MC::initLLVMToCVRegMapping(X);
  return X;
}

static MCAsmInfo *createAArch64MCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TheTriple,
                                         const MCTargetOptions &Options) {
  MCAsmInfo *MAI;
  if (TheTriple.isOSBinFormatMachO())
    MAI = new AArch64MCAsmInfoDarwin(TheTriple.getArch() == Triple::aarch64_32);
  else if (TheTriple.isWindowsMSVCEnvironment())
    MAI = new AArch64MCAsmInfoMicrosoftCOFF();
  else if (TheTriple.isOSBinFormatCOFF())
    MAI = new AArch64MCAsmInfoGNUCOFF();
  else {
    assert(TheTriple.isOSBinFormatELF() && "Invalid target");
    MAI = new AArch64MCAsmInfoELF(TheTriple);
  }

  // Initial state of the frame pointer is SP.
  unsigned Reg = MRI.getDwarfRegNum(AArch64::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, Reg, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createAArch64MCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new AArch64InstPrinter(MAI, MII, MRI);
  if (SyntaxVariant == 1)
    return new AArch64AppleInstPrinter(MAI, MII, MRI);

  return nullptr;
}

static MCStreamer *createELFStreamer(const Triple &T, MCContext &Ctx,
                                     std::unique_ptr<MCAsmBackend> &&TAB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createAArch64ELFStreamer(Ctx, std::move(TAB), std::move(OW),
                                  std::move(Emitter));
}

static MCStreamer *
createMachOStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                    std::unique_ptr<MCObjectWriter> &&OW,
                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createMachOStreamer(Ctx, std::move(TAB), std::move(OW),
                             std::move(Emitter), /*ignore=*/false,
                             /*LabelSections*/ true);
}

static MCStreamer *
createWinCOFFStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createAArch64WinCOFFStreamer(Ctx, std::move(TAB), std::move(OW),
                                      std::move(Emitter));
}

namespace {

class AArch64MCInstrAnalysis : public MCInstrAnalysis {
public:
  AArch64MCInstrAnalysis(const MCInstrInfo *Info) : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    // Search for a PC-relative argument.
    // This will handle instructions like bcc (where the first argument is the
    // condition code) and cbz (where it is a register).
    const auto &Desc = Info->get(Inst.getOpcode());
    for (unsigned i = 0, e = Inst.getNumOperands(); i != e; i++) {
      if (Desc.operands()[i].OperandType == MCOI::OPERAND_PCREL) {
        int64_t Imm = Inst.getOperand(i).getImm();
        if (Inst.getOpcode() == AArch64::ADR)
          Target = Addr + Imm;
        else if (Inst.getOpcode() == AArch64::ADRP)
          Target = (Addr & -4096) + Imm * 4096;
        else
          Target = Addr + Imm * 4;
        return true;
      }
    }
    return false;
  }

  bool clearsSuperRegisters(const MCRegisterInfo &MRI, const MCInst &Inst,
                            APInt &Mask) const override {
    const MCInstrDesc &Desc = Info->get(Inst.getOpcode());
    unsigned NumDefs = Desc.getNumDefs();
    unsigned NumImplicitDefs = Desc.implicit_defs().size();
    assert(Mask.getBitWidth() == NumDefs + NumImplicitDefs &&
           "Unexpected number of bits in the mask!");
    // 32-bit General Purpose Register class.
    const MCRegisterClass &GPR32RC = MRI.getRegClass(AArch64::GPR32RegClassID);
    // Floating Point Register classes.
    const MCRegisterClass &FPR8RC = MRI.getRegClass(AArch64::FPR8RegClassID);
    const MCRegisterClass &FPR16RC = MRI.getRegClass(AArch64::FPR16RegClassID);
    const MCRegisterClass &FPR32RC = MRI.getRegClass(AArch64::FPR32RegClassID);
    const MCRegisterClass &FPR64RC = MRI.getRegClass(AArch64::FPR64RegClassID);
    const MCRegisterClass &FPR128RC =
        MRI.getRegClass(AArch64::FPR128RegClassID);

    auto ClearsSuperReg = [=](unsigned RegID) {
      // An update to the lower 32 bits of a 64 bit integer register is
      // architecturally defined to zero extend the upper 32 bits on a write.
      if (GPR32RC.contains(RegID))
        return true;
      // SIMD&FP instructions operating on scalar data only acccess the lower
      // bits of a register, the upper bits are zero extended on a write. For
      // SIMD vector registers smaller than 128-bits, the upper 64-bits of the
      // register are zero extended on a write.
      // When VL is higher than 128 bits, any write to a SIMD&FP register sets
      // bits higher than 128 to zero.
      return FPR8RC.contains(RegID) || FPR16RC.contains(RegID) ||
             FPR32RC.contains(RegID) || FPR64RC.contains(RegID) ||
             FPR128RC.contains(RegID);
    };

    Mask.clearAllBits();
    for (unsigned I = 0, E = NumDefs; I < E; ++I) {
      const MCOperand &Op = Inst.getOperand(I);
      if (ClearsSuperReg(Op.getReg()))
        Mask.setBit(I);
    }

    for (unsigned I = 0, E = NumImplicitDefs; I < E; ++I) {
      const MCPhysReg Reg = Desc.implicit_defs()[I];
      if (ClearsSuperReg(Reg))
        Mask.setBit(NumDefs + I);
    }

    return Mask.getBoolValue();
  }

  std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 const Triple &TargetTriple) const override {
    // Do a lightweight parsing of PLT entries.
    std::vector<std::pair<uint64_t, uint64_t>> Result;
    for (uint64_t Byte = 0, End = PltContents.size(); Byte + 7 < End;
         Byte += 4) {
      uint32_t Insn = support::endian::read32le(PltContents.data() + Byte);
      uint64_t Off = 0;
      // Check for optional bti c that prefixes adrp in BTI enabled entries
      if (Insn == 0xd503245f) {
         Off = 4;
         Insn = support::endian::read32le(PltContents.data() + Byte + Off);
      }
      // Check for adrp.
      if ((Insn & 0x9f000000) != 0x90000000)
        continue;
      Off += 4;
      uint64_t Imm = (((PltSectionVA + Byte) >> 12) << 12) +
            (((Insn >> 29) & 3) << 12) + (((Insn >> 5) & 0x3ffff) << 14);
      uint32_t Insn2 =
          support::endian::read32le(PltContents.data() + Byte + Off);
      // Check for: ldr Xt, [Xn, #pimm].
      if (Insn2 >> 22 == 0x3e5) {
        Imm += ((Insn2 >> 10) & 0xfff) << 3;
        Result.push_back(std::make_pair(PltSectionVA + Byte, Imm));
        Byte += 4;
      }
    }
    return Result;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createAArch64InstrAnalysis(const MCInstrInfo *Info) {
  return new AArch64MCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAArch64TargetMC() {
  for (Target *T : {&getTheAArch64leTarget(), &getTheAArch64beTarget(),
                    &getTheAArch64_32Target(), &getTheARM64Target(),
                    &getTheARM64_32Target()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createAArch64MCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createAArch64MCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createAArch64MCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createAArch64MCSubtargetInfo);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createAArch64InstrAnalysis);

    // Register the MC Code Emitter
    TargetRegistry::RegisterMCCodeEmitter(*T, createAArch64MCCodeEmitter);

    // Register the obj streamers.
    TargetRegistry::RegisterELFStreamer(*T, createELFStreamer);
    TargetRegistry::RegisterMachOStreamer(*T, createMachOStreamer);
    TargetRegistry::RegisterCOFFStreamer(*T, createWinCOFFStreamer);

    // Register the obj target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createAArch64ObjectTargetStreamer);

    // Register the asm streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T,
                                              createAArch64AsmTargetStreamer);
    // Register the null streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T,
                                               createAArch64NullTargetStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createAArch64MCInstPrinter);
  }

  // Register the asm backend.
  for (Target *T : {&getTheAArch64leTarget(), &getTheAArch64_32Target(),
                    &getTheARM64Target(), &getTheARM64_32Target()})
    TargetRegistry::RegisterMCAsmBackend(*T, createAArch64leAsmBackend);
  TargetRegistry::RegisterMCAsmBackend(getTheAArch64beTarget(),
                                       createAArch64beAsmBackend);
}
