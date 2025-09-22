//===------------------- RISCVCustomBehaviour.cpp ---------------*-C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods from the RISCVCustomBehaviour class.
///
//===----------------------------------------------------------------------===//

#include "RISCVCustomBehaviour.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCV.h"
#include "TargetInfo/RISCVTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "llvm-mca-riscv-custombehaviour"

namespace llvm {
namespace mca {

const llvm::StringRef RISCVLMULInstrument::DESC_NAME = "RISCV-LMUL";

bool RISCVLMULInstrument::isDataValid(llvm::StringRef Data) {
  // Return true if not one of the valid LMUL strings
  return StringSwitch<bool>(Data)
      .Cases("M1", "M2", "M4", "M8", "MF2", "MF4", "MF8", true)
      .Default(false);
}

uint8_t RISCVLMULInstrument::getLMUL() const {
  // assertion prevents us from needing llvm_unreachable in the StringSwitch
  // below
  assert(isDataValid(getData()) &&
         "Cannot get LMUL because invalid Data value");
  // These are the LMUL values that are used in RISC-V tablegen
  return StringSwitch<uint8_t>(getData())
      .Case("M1", 0b000)
      .Case("M2", 0b001)
      .Case("M4", 0b010)
      .Case("M8", 0b011)
      .Case("MF2", 0b111)
      .Case("MF4", 0b110)
      .Case("MF8", 0b101);
}

const llvm::StringRef RISCVSEWInstrument::DESC_NAME = "RISCV-SEW";

bool RISCVSEWInstrument::isDataValid(llvm::StringRef Data) {
  // Return true if not one of the valid SEW strings
  return StringSwitch<bool>(Data)
      .Cases("E8", "E16", "E32", "E64", true)
      .Default(false);
}

uint8_t RISCVSEWInstrument::getSEW() const {
  // assertion prevents us from needing llvm_unreachable in the StringSwitch
  // below
  assert(isDataValid(getData()) && "Cannot get SEW because invalid Data value");
  // These are the LMUL values that are used in RISC-V tablegen
  return StringSwitch<uint8_t>(getData())
      .Case("E8", 8)
      .Case("E16", 16)
      .Case("E32", 32)
      .Case("E64", 64);
}

bool RISCVInstrumentManager::supportsInstrumentType(
    llvm::StringRef Type) const {
  return Type == RISCVLMULInstrument::DESC_NAME ||
         Type == RISCVSEWInstrument::DESC_NAME;
}

UniqueInstrument
RISCVInstrumentManager::createInstrument(llvm::StringRef Desc,
                                         llvm::StringRef Data) {
  if (Desc == RISCVLMULInstrument::DESC_NAME) {
    if (!RISCVLMULInstrument::isDataValid(Data)) {
      LLVM_DEBUG(dbgs() << "RVCB: Bad data for instrument kind " << Desc << ": "
                        << Data << '\n');
      return nullptr;
    }
    return std::make_unique<RISCVLMULInstrument>(Data);
  }

  if (Desc == RISCVSEWInstrument::DESC_NAME) {
    if (!RISCVSEWInstrument::isDataValid(Data)) {
      LLVM_DEBUG(dbgs() << "RVCB: Bad data for instrument kind " << Desc << ": "
                        << Data << '\n');
      return nullptr;
    }
    return std::make_unique<RISCVSEWInstrument>(Data);
  }

  LLVM_DEBUG(dbgs() << "RVCB: Unknown instrumentation Desc: " << Desc << '\n');
  return nullptr;
}

SmallVector<UniqueInstrument>
RISCVInstrumentManager::createInstruments(const MCInst &Inst) {
  if (Inst.getOpcode() == RISCV::VSETVLI ||
      Inst.getOpcode() == RISCV::VSETIVLI) {
    LLVM_DEBUG(dbgs() << "RVCB: Found VSETVLI and creating instrument for it: "
                      << Inst << "\n");
    unsigned VTypeI = Inst.getOperand(2).getImm();
    RISCVII::VLMUL VLMUL = RISCVVType::getVLMUL(VTypeI);

    StringRef LMUL;
    switch (VLMUL) {
    case RISCVII::LMUL_1:
      LMUL = "M1";
      break;
    case RISCVII::LMUL_2:
      LMUL = "M2";
      break;
    case RISCVII::LMUL_4:
      LMUL = "M4";
      break;
    case RISCVII::LMUL_8:
      LMUL = "M8";
      break;
    case RISCVII::LMUL_F2:
      LMUL = "MF2";
      break;
    case RISCVII::LMUL_F4:
      LMUL = "MF4";
      break;
    case RISCVII::LMUL_F8:
      LMUL = "MF8";
      break;
    case RISCVII::LMUL_RESERVED:
      llvm_unreachable("Cannot create instrument for LMUL_RESERVED");
    }
    SmallVector<UniqueInstrument> Instruments;
    Instruments.emplace_back(
        createInstrument(RISCVLMULInstrument::DESC_NAME, LMUL));

    unsigned SEW = RISCVVType::getSEW(VTypeI);
    StringRef SEWStr;
    switch (SEW) {
    case 8:
      SEWStr = "E8";
      break;
    case 16:
      SEWStr = "E16";
      break;
    case 32:
      SEWStr = "E32";
      break;
    case 64:
      SEWStr = "E64";
      break;
    default:
      llvm_unreachable("Cannot create instrument for SEW");
    }
    Instruments.emplace_back(
        createInstrument(RISCVSEWInstrument::DESC_NAME, SEWStr));

    return Instruments;
  }
  return SmallVector<UniqueInstrument>();
}

static std::pair<uint8_t, uint8_t>
getEEWAndEMUL(unsigned Opcode, RISCVII::VLMUL LMUL, uint8_t SEW) {
  uint8_t EEW;
  switch (Opcode) {
  case RISCV::VLM_V:
  case RISCV::VSM_V:
  case RISCV::VLE8_V:
  case RISCV::VSE8_V:
  case RISCV::VLSE8_V:
  case RISCV::VSSE8_V:
    EEW = 8;
    break;
  case RISCV::VLE16_V:
  case RISCV::VSE16_V:
  case RISCV::VLSE16_V:
  case RISCV::VSSE16_V:
    EEW = 16;
    break;
  case RISCV::VLE32_V:
  case RISCV::VSE32_V:
  case RISCV::VLSE32_V:
  case RISCV::VSSE32_V:
    EEW = 32;
    break;
  case RISCV::VLE64_V:
  case RISCV::VSE64_V:
  case RISCV::VLSE64_V:
  case RISCV::VSSE64_V:
    EEW = 64;
    break;
  default:
    llvm_unreachable("Could not determine EEW from Opcode");
  }

  auto EMUL = RISCVVType::getSameRatioLMUL(SEW, LMUL, EEW);
  if (!EEW)
    llvm_unreachable("Invalid SEW or LMUL for new ratio");
  return std::make_pair(EEW, *EMUL);
}

bool opcodeHasEEWAndEMULInfo(unsigned short Opcode) {
  return Opcode == RISCV::VLM_V || Opcode == RISCV::VSM_V ||
         Opcode == RISCV::VLE8_V || Opcode == RISCV::VSE8_V ||
         Opcode == RISCV::VLE16_V || Opcode == RISCV::VSE16_V ||
         Opcode == RISCV::VLE32_V || Opcode == RISCV::VSE32_V ||
         Opcode == RISCV::VLE64_V || Opcode == RISCV::VSE64_V ||
         Opcode == RISCV::VLSE8_V || Opcode == RISCV::VSSE8_V ||
         Opcode == RISCV::VLSE16_V || Opcode == RISCV::VSSE16_V ||
         Opcode == RISCV::VLSE32_V || Opcode == RISCV::VSSE32_V ||
         Opcode == RISCV::VLSE64_V || Opcode == RISCV::VSSE64_V;
}

unsigned RISCVInstrumentManager::getSchedClassID(
    const MCInstrInfo &MCII, const MCInst &MCI,
    const llvm::SmallVector<Instrument *> &IVec) const {
  unsigned short Opcode = MCI.getOpcode();
  unsigned SchedClassID = MCII.get(Opcode).getSchedClass();

  // Unpack all possible RISC-V instruments from IVec.
  RISCVLMULInstrument *LI = nullptr;
  RISCVSEWInstrument *SI = nullptr;
  for (auto &I : IVec) {
    if (I->getDesc() == RISCVLMULInstrument::DESC_NAME)
      LI = static_cast<RISCVLMULInstrument *>(I);
    else if (I->getDesc() == RISCVSEWInstrument::DESC_NAME)
      SI = static_cast<RISCVSEWInstrument *>(I);
  }

  // Need LMUL or LMUL, SEW in order to override opcode. If no LMUL is provided,
  // then no option to override.
  if (!LI) {
    LLVM_DEBUG(
        dbgs() << "RVCB: Did not use instrumentation to override Opcode.\n");
    return SchedClassID;
  }
  uint8_t LMUL = LI->getLMUL();

  // getBaseInfo works with (Opcode, LMUL, 0) if no SEW instrument,
  // or (Opcode, LMUL, SEW) if SEW instrument is active, and depends on LMUL
  // and SEW, or (Opcode, LMUL, 0) if does not depend on SEW.
  uint8_t SEW = SI ? SI->getSEW() : 0;

  const RISCVVInversePseudosTable::PseudoInfo *RVV = nullptr;
  if (opcodeHasEEWAndEMULInfo(Opcode)) {
    RISCVII::VLMUL VLMUL = static_cast<RISCVII::VLMUL>(LMUL);
    auto [EEW, EMUL] = getEEWAndEMUL(Opcode, VLMUL, SEW);
    RVV = RISCVVInversePseudosTable::getBaseInfo(Opcode, EMUL, EEW);
  } else {
    // Check if it depends on LMUL and SEW
    RVV = RISCVVInversePseudosTable::getBaseInfo(Opcode, LMUL, SEW);
    // Check if it depends only on LMUL
    if (!RVV)
      RVV = RISCVVInversePseudosTable::getBaseInfo(Opcode, LMUL, 0);
  }

  // Not a RVV instr
  if (!RVV) {
    LLVM_DEBUG(
        dbgs() << "RVCB: Could not find PseudoInstruction for Opcode "
               << MCII.getName(Opcode)
               << ", LMUL=" << (LI ? LI->getData() : "Unspecified")
               << ", SEW=" << (SI ? SI->getData() : "Unspecified")
               << ". Ignoring instrumentation and using original SchedClassID="
               << SchedClassID << '\n');
    return SchedClassID;
  }

  // Override using pseudo
  LLVM_DEBUG(dbgs() << "RVCB: Found Pseudo Instruction for Opcode "
                    << MCII.getName(Opcode) << ", LMUL=" << LI->getData()
                    << ", SEW=" << (SI ? SI->getData() : "Unspecified")
                    << ". Overriding original SchedClassID=" << SchedClassID
                    << " with " << MCII.getName(RVV->Pseudo) << '\n');
  return MCII.get(RVV->Pseudo).getSchedClass();
}

} // namespace mca
} // namespace llvm

using namespace llvm;
using namespace mca;

static InstrumentManager *
createRISCVInstrumentManager(const MCSubtargetInfo &STI,
                             const MCInstrInfo &MCII) {
  return new RISCVInstrumentManager(STI, MCII);
}

/// Extern function to initialize the targets for the RISC-V backend
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCVTargetMCA() {
  TargetRegistry::RegisterInstrumentManager(getTheRISCV32Target(),
                                            createRISCVInstrumentManager);
  TargetRegistry::RegisterInstrumentManager(getTheRISCV64Target(),
                                            createRISCVInstrumentManager);
}
