//===-- RISCVBaseInfo.cpp - Top level definitions for RISC-V MC -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone enum definitions for the RISC-V target
// useful for the compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//

#include "RISCVBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

extern const SubtargetFeatureKV RISCVFeatureKV[RISCV::NumSubtargetFeatures];

namespace RISCVSysReg {
#define GET_SysRegsList_IMPL
#include "RISCVGenSearchableTables.inc"
} // namespace RISCVSysReg

namespace RISCVInsnOpcode {
#define GET_RISCVOpcodesList_IMPL
#include "RISCVGenSearchableTables.inc"
} // namespace RISCVInsnOpcode

namespace RISCVABI {
ABI computeTargetABI(const Triple &TT, const FeatureBitset &FeatureBits,
                     StringRef ABIName) {
  auto TargetABI = getTargetABI(ABIName);
  bool IsRV64 = TT.isArch64Bit();
  bool IsRVE = FeatureBits[RISCV::FeatureStdExtE];

  if (!ABIName.empty() && TargetABI == ABI_Unknown) {
    errs()
        << "'" << ABIName
        << "' is not a recognized ABI for this target (ignoring target-abi)\n";
  } else if (ABIName.starts_with("ilp32") && IsRV64) {
    errs() << "32-bit ABIs are not supported for 64-bit targets (ignoring "
              "target-abi)\n";
    TargetABI = ABI_Unknown;
  } else if (ABIName.starts_with("lp64") && !IsRV64) {
    errs() << "64-bit ABIs are not supported for 32-bit targets (ignoring "
              "target-abi)\n";
    TargetABI = ABI_Unknown;
  } else if (!IsRV64 && IsRVE && TargetABI != ABI_ILP32E &&
             TargetABI != ABI_Unknown) {
    // TODO: move this checking to RISCVTargetLowering and RISCVAsmParser
    errs()
        << "Only the ilp32e ABI is supported for RV32E (ignoring target-abi)\n";
    TargetABI = ABI_Unknown;
  } else if (IsRV64 && IsRVE && TargetABI != ABI_LP64E &&
             TargetABI != ABI_Unknown) {
    // TODO: move this checking to RISCVTargetLowering and RISCVAsmParser
    errs()
        << "Only the lp64e ABI is supported for RV64E (ignoring target-abi)\n";
    TargetABI = ABI_Unknown;
  }

  if ((TargetABI == RISCVABI::ABI::ABI_ILP32E ||
       (TargetABI == ABI_Unknown && IsRVE && !IsRV64)) &&
      FeatureBits[RISCV::FeatureStdExtD])
    report_fatal_error("ILP32E cannot be used with the D ISA extension");

  if (TargetABI != ABI_Unknown)
    return TargetABI;

  // If no explicit ABI is given, try to compute the default ABI.
  auto ISAInfo = RISCVFeatures::parseFeatureBits(IsRV64, FeatureBits);
  if (!ISAInfo)
    report_fatal_error(ISAInfo.takeError());
  return getTargetABI((*ISAInfo)->computeDefaultABI());
}

ABI getTargetABI(StringRef ABIName) {
  auto TargetABI = StringSwitch<ABI>(ABIName)
                       .Case("ilp32", ABI_ILP32)
                       .Case("ilp32f", ABI_ILP32F)
                       .Case("ilp32d", ABI_ILP32D)
                       .Case("ilp32e", ABI_ILP32E)
                       .Case("lp64", ABI_LP64)
                       .Case("lp64f", ABI_LP64F)
                       .Case("lp64d", ABI_LP64D)
                       .Case("lp64e", ABI_LP64E)
                       .Default(ABI_Unknown);
  return TargetABI;
}

// To avoid the BP value clobbered by a function call, we need to choose a
// callee saved register to save the value. RV32E only has X8 and X9 as callee
// saved registers and X8 will be used as fp. So we choose X9 as bp.
MCRegister getBPReg() { return RISCV::X9; }

// Returns the register holding shadow call stack pointer.
MCRegister getSCSPReg() { return RISCV::X3; }

} // namespace RISCVABI

namespace RISCVFeatures {

void validate(const Triple &TT, const FeatureBitset &FeatureBits) {
  if (TT.isArch64Bit() && !FeatureBits[RISCV::Feature64Bit])
    report_fatal_error("RV64 target requires an RV64 CPU");
  if (!TT.isArch64Bit() && !FeatureBits[RISCV::Feature32Bit])
    report_fatal_error("RV32 target requires an RV32 CPU");
  if (FeatureBits[RISCV::Feature32Bit] &&
      FeatureBits[RISCV::Feature64Bit])
    report_fatal_error("RV32 and RV64 can't be combined");
}

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
parseFeatureBits(bool IsRV64, const FeatureBitset &FeatureBits) {
  unsigned XLen = IsRV64 ? 64 : 32;
  std::vector<std::string> FeatureVector;
  // Convert FeatureBitset to FeatureVector.
  for (auto Feature : RISCVFeatureKV) {
    if (FeatureBits[Feature.Value] &&
        llvm::RISCVISAInfo::isSupportedExtensionFeature(Feature.Key))
      FeatureVector.push_back(std::string("+") + Feature.Key);
  }
  return llvm::RISCVISAInfo::parseFeatures(XLen, FeatureVector);
}

} // namespace RISCVFeatures

// Include the auto-generated portion of the compress emitter.
#define GEN_UNCOMPRESS_INSTR
#define GEN_COMPRESS_INSTR
#include "RISCVGenCompressInstEmitter.inc"

bool RISCVRVC::compress(MCInst &OutInst, const MCInst &MI,
                        const MCSubtargetInfo &STI) {
  return compressInst(OutInst, MI, STI);
}

bool RISCVRVC::uncompress(MCInst &OutInst, const MCInst &MI,
                          const MCSubtargetInfo &STI) {
  return uncompressInst(OutInst, MI, STI);
}

// Lookup table for fli.s for entries 2-31.
static constexpr std::pair<uint8_t, uint8_t> LoadFP32ImmArr[] = {
    {0b01101111, 0b00}, {0b01110000, 0b00}, {0b01110111, 0b00},
    {0b01111000, 0b00}, {0b01111011, 0b00}, {0b01111100, 0b00},
    {0b01111101, 0b00}, {0b01111101, 0b01}, {0b01111101, 0b10},
    {0b01111101, 0b11}, {0b01111110, 0b00}, {0b01111110, 0b01},
    {0b01111110, 0b10}, {0b01111110, 0b11}, {0b01111111, 0b00},
    {0b01111111, 0b01}, {0b01111111, 0b10}, {0b01111111, 0b11},
    {0b10000000, 0b00}, {0b10000000, 0b01}, {0b10000000, 0b10},
    {0b10000001, 0b00}, {0b10000010, 0b00}, {0b10000011, 0b00},
    {0b10000110, 0b00}, {0b10000111, 0b00}, {0b10001110, 0b00},
    {0b10001111, 0b00}, {0b11111111, 0b00}, {0b11111111, 0b10},
};

int RISCVLoadFPImm::getLoadFPImm(APFloat FPImm) {
  assert((&FPImm.getSemantics() == &APFloat::IEEEsingle() ||
          &FPImm.getSemantics() == &APFloat::IEEEdouble() ||
          &FPImm.getSemantics() == &APFloat::IEEEhalf()) &&
         "Unexpected semantics");

  // Handle the minimum normalized value which is different for each type.
  if (FPImm.isSmallestNormalized() && !FPImm.isNegative())
    return 1;

  // Convert to single precision to use its lookup table.
  bool LosesInfo;
  APFloat::opStatus Status = FPImm.convert(
      APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven, &LosesInfo);
  if (Status != APFloat::opOK || LosesInfo)
    return -1;

  APInt Imm = FPImm.bitcastToAPInt();

  if (Imm.extractBitsAsZExtValue(21, 0) != 0)
    return -1;

  bool Sign = Imm.extractBitsAsZExtValue(1, 31);
  uint8_t Mantissa = Imm.extractBitsAsZExtValue(2, 21);
  uint8_t Exp = Imm.extractBitsAsZExtValue(8, 23);

  auto EMI = llvm::lower_bound(LoadFP32ImmArr, std::make_pair(Exp, Mantissa));
  if (EMI == std::end(LoadFP32ImmArr) || EMI->first != Exp ||
      EMI->second != Mantissa)
    return -1;

  // Table doesn't have entry 0 or 1.
  int Entry = std::distance(std::begin(LoadFP32ImmArr), EMI) + 2;

  // The only legal negative value is -1.0(entry 0). 1.0 is entry 16.
  if (Sign) {
    if (Entry == 16)
      return 0;
    return -1;
  }

  return Entry;
}

float RISCVLoadFPImm::getFPImm(unsigned Imm) {
  assert(Imm != 1 && Imm != 30 && Imm != 31 && "Unsupported immediate");

  // Entry 0 is -1.0, the only negative value. Entry 16 is 1.0.
  uint32_t Sign = 0;
  if (Imm == 0) {
    Sign = 0b1;
    Imm = 16;
  }

  uint32_t Exp = LoadFP32ImmArr[Imm - 2].first;
  uint32_t Mantissa = LoadFP32ImmArr[Imm - 2].second;

  uint32_t I = Sign << 31 | Exp << 23 | Mantissa << 21;
  return bit_cast<float>(I);
}

void RISCVZC::printRlist(unsigned SlistEncode, raw_ostream &OS) {
  OS << "{ra";
  if (SlistEncode > 4) {
    OS << ", s0";
    if (SlistEncode == 15)
      OS << "-s11";
    else if (SlistEncode > 5 && SlistEncode <= 14)
      OS << "-s" << (SlistEncode - 5);
  }
  OS << "}";
}

} // namespace llvm
