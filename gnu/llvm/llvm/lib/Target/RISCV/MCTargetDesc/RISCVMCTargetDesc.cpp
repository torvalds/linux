//===-- RISCVMCTargetDesc.cpp - RISC-V Target Descriptions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file provides RISC-V specific target descriptions.
///
//===----------------------------------------------------------------------===//

#include "RISCVMCTargetDesc.h"
#include "RISCVBaseInfo.h"
#include "RISCVELFStreamer.h"
#include "RISCVInstPrinter.h"
#include "RISCVMCAsmInfo.h"
#include "RISCVMCObjectFileInfo.h"
#include "RISCVTargetStreamer.h"
#include "TargetInfo/RISCVTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include <bitset>

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "RISCVGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "RISCVGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "RISCVGenSubtargetInfo.inc"

namespace llvm::RISCVVInversePseudosTable {

using namespace RISCV;

#define GET_RISCVVInversePseudosTable_IMPL
#include "RISCVGenSearchableTables.inc"

} // namespace llvm::RISCVVInversePseudosTable

using namespace llvm;

static MCInstrInfo *createRISCVMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitRISCVMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createRISCVMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitRISCVMCRegisterInfo(X, RISCV::X1);
  return X;
}

static MCAsmInfo *createRISCVMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new RISCVMCAsmInfo(TT);

  MCRegister SP = MRI.getDwarfRegNum(RISCV::X2, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCObjectFileInfo *
createRISCVMCObjectFileInfo(MCContext &Ctx, bool PIC,
                            bool LargeCodeModel = false) {
  MCObjectFileInfo *MOFI = new RISCVMCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

static MCSubtargetInfo *createRISCVMCSubtargetInfo(const Triple &TT,
                                                   StringRef CPU, StringRef FS) {
  if (CPU.empty() || CPU == "generic")
    CPU = TT.isArch64Bit() ? "generic-rv64" : "generic-rv32";

  return createRISCVMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCInstPrinter *createRISCVMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new RISCVInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *
createRISCVObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new RISCVTargetELFStreamer(S, STI);
  return nullptr;
}

static MCTargetStreamer *
createRISCVAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                             MCInstPrinter *InstPrint) {
  return new RISCVTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createRISCVNullTargetStreamer(MCStreamer &S) {
  return new RISCVTargetStreamer(S);
}

namespace {

class RISCVMCInstrAnalysis : public MCInstrAnalysis {
  int64_t GPRState[31] = {};
  std::bitset<31> GPRValidMask;

  static bool isGPR(unsigned Reg) {
    return Reg >= RISCV::X0 && Reg <= RISCV::X31;
  }

  static unsigned getRegIndex(unsigned Reg) {
    assert(isGPR(Reg) && Reg != RISCV::X0 && "Invalid GPR reg");
    return Reg - RISCV::X1;
  }

  void setGPRState(unsigned Reg, std::optional<int64_t> Value) {
    if (Reg == RISCV::X0)
      return;

    auto Index = getRegIndex(Reg);

    if (Value) {
      GPRState[Index] = *Value;
      GPRValidMask.set(Index);
    } else {
      GPRValidMask.reset(Index);
    }
  }

  std::optional<int64_t> getGPRState(unsigned Reg) const {
    if (Reg == RISCV::X0)
      return 0;

    auto Index = getRegIndex(Reg);

    if (GPRValidMask.test(Index))
      return GPRState[Index];
    return std::nullopt;
  }

public:
  explicit RISCVMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  void resetState() override { GPRValidMask.reset(); }

  void updateState(const MCInst &Inst, uint64_t Addr) override {
    // Terminators mark the end of a basic block which means the sequentially
    // next instruction will be the first of another basic block and the current
    // state will typically not be valid anymore. For calls, we assume all
    // registers may be clobbered by the callee (TODO: should we take the
    // calling convention into account?).
    if (isTerminator(Inst) || isCall(Inst)) {
      resetState();
      return;
    }

    switch (Inst.getOpcode()) {
    default: {
      // Clear the state of all defined registers for instructions that we don't
      // explicitly support.
      auto NumDefs = Info->get(Inst.getOpcode()).getNumDefs();
      for (unsigned I = 0; I < NumDefs; ++I) {
        auto DefReg = Inst.getOperand(I).getReg();
        if (isGPR(DefReg))
          setGPRState(DefReg, std::nullopt);
      }
      break;
    }
    case RISCV::AUIPC:
      setGPRState(Inst.getOperand(0).getReg(),
                  Addr + (Inst.getOperand(1).getImm() << 12));
      break;
    }
  }

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    if (isConditionalBranch(Inst)) {
      int64_t Imm;
      if (Size == 2)
        Imm = Inst.getOperand(1).getImm();
      else
        Imm = Inst.getOperand(2).getImm();
      Target = Addr + Imm;
      return true;
    }

    if (Inst.getOpcode() == RISCV::C_JAL || Inst.getOpcode() == RISCV::C_J) {
      Target = Addr + Inst.getOperand(0).getImm();
      return true;
    }

    if (Inst.getOpcode() == RISCV::JAL) {
      Target = Addr + Inst.getOperand(1).getImm();
      return true;
    }

    if (Inst.getOpcode() == RISCV::JALR) {
      if (auto TargetRegState = getGPRState(Inst.getOperand(1).getReg())) {
        Target = *TargetRegState + Inst.getOperand(2).getImm();
        return true;
      }

      return false;
    }

    return false;
  }

  bool isTerminator(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isTerminator(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JAL:
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0;
    }
  }

  bool isCall(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isCall(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JAL:
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() != RISCV::X0;
    }
  }

  bool isReturn(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isReturn(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0 &&
             maybeReturnAddress(Inst.getOperand(1).getReg());
    case RISCV::C_JR:
      return maybeReturnAddress(Inst.getOperand(0).getReg());
    }
  }

  bool isBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isBranch(Inst))
      return true;

    return isBranchImpl(Inst);
  }

  bool isUnconditionalBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isUnconditionalBranch(Inst))
      return true;

    return isBranchImpl(Inst);
  }

  bool isIndirectBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isIndirectBranch(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0 &&
             !maybeReturnAddress(Inst.getOperand(1).getReg());
    case RISCV::C_JR:
      return !maybeReturnAddress(Inst.getOperand(0).getReg());
    }
  }

private:
  static bool maybeReturnAddress(unsigned Reg) {
    // X1 is used for normal returns, X5 for returns from outlined functions.
    return Reg == RISCV::X1 || Reg == RISCV::X5;
  }

  static bool isBranchImpl(const MCInst &Inst) {
    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JAL:
      return Inst.getOperand(0).getReg() == RISCV::X0;
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0 &&
             !maybeReturnAddress(Inst.getOperand(1).getReg());
    case RISCV::C_JR:
      return !maybeReturnAddress(Inst.getOperand(0).getReg());
    }
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createRISCVInstrAnalysis(const MCInstrInfo *Info) {
  return new RISCVMCInstrAnalysis(Info);
}

namespace {
MCStreamer *createRISCVELFStreamer(const Triple &T, MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> &&MAB,
                                   std::unique_ptr<MCObjectWriter> &&MOW,
                                   std::unique_ptr<MCCodeEmitter> &&MCE) {
  return createRISCVELFStreamer(Context, std::move(MAB), std::move(MOW),
                                std::move(MCE));
}
} // end anonymous namespace

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCVTargetMC() {
  for (Target *T : {&getTheRISCV32Target(), &getTheRISCV64Target()}) {
    TargetRegistry::RegisterMCAsmInfo(*T, createRISCVMCAsmInfo);
    TargetRegistry::RegisterMCObjectFileInfo(*T, createRISCVMCObjectFileInfo);
    TargetRegistry::RegisterMCInstrInfo(*T, createRISCVMCInstrInfo);
    TargetRegistry::RegisterMCRegInfo(*T, createRISCVMCRegisterInfo);
    TargetRegistry::RegisterMCAsmBackend(*T, createRISCVAsmBackend);
    TargetRegistry::RegisterMCCodeEmitter(*T, createRISCVMCCodeEmitter);
    TargetRegistry::RegisterMCInstPrinter(*T, createRISCVMCInstPrinter);
    TargetRegistry::RegisterMCSubtargetInfo(*T, createRISCVMCSubtargetInfo);
    TargetRegistry::RegisterELFStreamer(*T, createRISCVELFStreamer);
    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createRISCVObjectTargetStreamer);
    TargetRegistry::RegisterMCInstrAnalysis(*T, createRISCVInstrAnalysis);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createRISCVAsmTargetStreamer);
    // Register the null target streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T,
                                               createRISCVNullTargetStreamer);
  }
}
