//===-- R600AsmPrinter.cpp - R600 Assembly printer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// The R600AsmPrinter is used to print both assembly string and also binary
/// code.  When passed an MCAsmStreamer it prints assembly and when passed
/// an MCObjectStreamer it outputs binary code.
//
//===----------------------------------------------------------------------===//

#include "R600AsmPrinter.h"
#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600Defines.h"
#include "R600MachineFunctionInfo.h"
#include "R600Subtarget.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

using namespace llvm;

AsmPrinter *
llvm::createR600AsmPrinterPass(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> &&Streamer) {
  return new R600AsmPrinter(TM, std::move(Streamer));
}

R600AsmPrinter::R600AsmPrinter(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> Streamer)
  : AsmPrinter(TM, std::move(Streamer)) { }

StringRef R600AsmPrinter::getPassName() const {
  return "R600 Assembly Printer";
}

void R600AsmPrinter::EmitProgramInfoR600(const MachineFunction &MF) {
  unsigned MaxGPR = 0;
  bool killPixel = false;
  const R600Subtarget &STM = MF.getSubtarget<R600Subtarget>();
  const R600RegisterInfo *RI = STM.getRegisterInfo();
  const R600MachineFunctionInfo *MFI = MF.getInfo<R600MachineFunctionInfo>();

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.getOpcode() == R600::KILLGT)
        killPixel = true;
      unsigned numOperands = MI.getNumOperands();
      for (unsigned op_idx = 0; op_idx < numOperands; op_idx++) {
        const MachineOperand &MO = MI.getOperand(op_idx);
        if (!MO.isReg())
          continue;
        unsigned HWReg = RI->getHWRegIndex(MO.getReg());

        // Register with value > 127 aren't GPR
        if (HWReg > 127)
          continue;
        MaxGPR = std::max(MaxGPR, HWReg);
      }
    }
  }

  unsigned RsrcReg;
  if (STM.getGeneration() >= AMDGPUSubtarget::EVERGREEN) {
    // Evergreen / Northern Islands
    switch (MF.getFunction().getCallingConv()) {
    default: [[fallthrough]];
    case CallingConv::AMDGPU_CS: RsrcReg = R_0288D4_SQ_PGM_RESOURCES_LS; break;
    case CallingConv::AMDGPU_GS: RsrcReg = R_028878_SQ_PGM_RESOURCES_GS; break;
    case CallingConv::AMDGPU_PS: RsrcReg = R_028844_SQ_PGM_RESOURCES_PS; break;
    case CallingConv::AMDGPU_VS: RsrcReg = R_028860_SQ_PGM_RESOURCES_VS; break;
    }
  } else {
    // R600 / R700
    switch (MF.getFunction().getCallingConv()) {
    default: [[fallthrough]];
    case CallingConv::AMDGPU_GS: [[fallthrough]];
    case CallingConv::AMDGPU_CS: [[fallthrough]];
    case CallingConv::AMDGPU_VS: RsrcReg = R_028868_SQ_PGM_RESOURCES_VS; break;
    case CallingConv::AMDGPU_PS: RsrcReg = R_028850_SQ_PGM_RESOURCES_PS; break;
    }
  }

  OutStreamer->emitInt32(RsrcReg);
  OutStreamer->emitIntValue(S_NUM_GPRS(MaxGPR + 1) |
                           S_STACK_SIZE(MFI->CFStackSize), 4);
  OutStreamer->emitInt32(R_02880C_DB_SHADER_CONTROL);
  OutStreamer->emitInt32(S_02880C_KILL_ENABLE(killPixel));

  if (AMDGPU::isCompute(MF.getFunction().getCallingConv())) {
    OutStreamer->emitInt32(R_0288E8_SQ_LDS_ALLOC);
    OutStreamer->emitIntValue(alignTo(MFI->getLDSSize(), 4) >> 2, 4);
  }
}

bool R600AsmPrinter::runOnMachineFunction(MachineFunction &MF) {


  // Functions needs to be cacheline (256B) aligned.
  MF.ensureAlignment(Align(256));

  SetupMachineFunction(MF);

  MCContext &Context = getObjFileLowering().getContext();
  MCSectionELF *ConfigSection =
      Context.getELFSection(".AMDGPU.config", ELF::SHT_PROGBITS, 0);
  OutStreamer->switchSection(ConfigSection);

  EmitProgramInfoR600(MF);

  emitFunctionBody();

  if (isVerbose()) {
    MCSectionELF *CommentSection =
        Context.getELFSection(".AMDGPU.csdata", ELF::SHT_PROGBITS, 0);
    OutStreamer->switchSection(CommentSection);

    R600MachineFunctionInfo *MFI = MF.getInfo<R600MachineFunctionInfo>();
    OutStreamer->emitRawComment(
      Twine("SQ_PGM_RESOURCES:STACK_SIZE = " + Twine(MFI->CFStackSize)));
  }

  return false;
}
