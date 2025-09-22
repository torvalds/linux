//===-- RISCVTargetParser - Parser for target features ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features
// for RISC-V CPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_RISCVTARGETPARSER_H
#define LLVM_TARGETPARSER_RISCVTARGETPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Triple;

namespace RISCV {

namespace RISCVExtensionBitmaskTable {
struct RISCVExtensionBitmask {
  const char *Name;
  unsigned GroupID;
  unsigned BitPosition;
};
} // namespace RISCVExtensionBitmaskTable

// We use 64 bits as the known part in the scalable vector types.
static constexpr unsigned RVVBitsPerBlock = 64;

void getFeaturesForCPU(StringRef CPU,
                       SmallVectorImpl<std::string> &EnabledFeatures,
                       bool NeedPlus = false);
bool parseCPU(StringRef CPU, bool IsRV64);
bool parseTuneCPU(StringRef CPU, bool IsRV64);
StringRef getMArchFromMcpu(StringRef CPU);
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values, bool IsRV64);
void fillValidTuneCPUArchList(SmallVectorImpl<StringRef> &Values, bool IsRV64);
bool hasFastScalarUnalignedAccess(StringRef CPU);
bool hasFastVectorUnalignedAccess(StringRef CPU);

} // namespace RISCV

namespace RISCVII {
enum VLMUL : uint8_t {
  LMUL_1 = 0,
  LMUL_2,
  LMUL_4,
  LMUL_8,
  LMUL_RESERVED,
  LMUL_F8,
  LMUL_F4,
  LMUL_F2
};

enum {
  TAIL_UNDISTURBED_MASK_UNDISTURBED = 0,
  TAIL_AGNOSTIC = 1,
  MASK_AGNOSTIC = 2,
};
} // namespace RISCVII

namespace RISCVVType {
// Is this a SEW value that can be encoded into the VTYPE format.
inline static bool isValidSEW(unsigned SEW) {
  return isPowerOf2_32(SEW) && SEW >= 8 && SEW <= 64;
}

// Is this a LMUL value that can be encoded into the VTYPE format.
inline static bool isValidLMUL(unsigned LMUL, bool Fractional) {
  return isPowerOf2_32(LMUL) && LMUL <= 8 && (!Fractional || LMUL != 1);
}

unsigned encodeVTYPE(RISCVII::VLMUL VLMUL, unsigned SEW, bool TailAgnostic,
                     bool MaskAgnostic);

inline static RISCVII::VLMUL getVLMUL(unsigned VType) {
  unsigned VLMUL = VType & 0x7;
  return static_cast<RISCVII::VLMUL>(VLMUL);
}

// Decode VLMUL into 1,2,4,8 and fractional indicator.
std::pair<unsigned, bool> decodeVLMUL(RISCVII::VLMUL VLMUL);

inline static RISCVII::VLMUL encodeLMUL(unsigned LMUL, bool Fractional) {
  assert(isValidLMUL(LMUL, Fractional) && "Unsupported LMUL");
  unsigned LmulLog2 = Log2_32(LMUL);
  return static_cast<RISCVII::VLMUL>(Fractional ? 8 - LmulLog2 : LmulLog2);
}

inline static unsigned decodeVSEW(unsigned VSEW) {
  assert(VSEW < 8 && "Unexpected VSEW value");
  return 1 << (VSEW + 3);
}

inline static unsigned encodeSEW(unsigned SEW) {
  assert(isValidSEW(SEW) && "Unexpected SEW value");
  return Log2_32(SEW) - 3;
}

inline static unsigned getSEW(unsigned VType) {
  unsigned VSEW = (VType >> 3) & 0x7;
  return decodeVSEW(VSEW);
}

inline static bool isTailAgnostic(unsigned VType) { return VType & 0x40; }

inline static bool isMaskAgnostic(unsigned VType) { return VType & 0x80; }

void printVType(unsigned VType, raw_ostream &OS);

unsigned getSEWLMULRatio(unsigned SEW, RISCVII::VLMUL VLMul);

std::optional<RISCVII::VLMUL>
getSameRatioLMUL(unsigned SEW, RISCVII::VLMUL VLMUL, unsigned EEW);
} // namespace RISCVVType

} // namespace llvm

#endif
