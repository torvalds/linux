//===-- CSKYTargetParser - Parser for CSKY target features --------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise CSKY hardware features
// such as FPU/CPU/ARCH/extensions and specific support such as HWDIV.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_CSKYTARGETPARSER_H
#define LLVM_TARGETPARSER_CSKYTARGETPARSER_H

#include "llvm/TargetParser/Triple.h"
#include <vector>

namespace llvm {
class StringRef;

namespace CSKY {

// Arch extension modifiers for CPUs.
enum ArchExtKind : uint64_t {
  AEK_INVALID = 0,
  AEK_NONE = 1,
  AEK_FPUV2SF = 1 << 1,
  AEK_FPUV2DF = 1 << 2,
  AEK_FDIVDU = 1 << 3,
  AEK_FPUV3HI = 1 << 4,
  AEK_FPUV3HF = 1 << 5,
  AEK_FPUV3SF = 1 << 6,
  AEK_FPUV3DF = 1 << 7,
  AEK_FLOATE1 = 1 << 8,
  AEK_FLOAT1E2 = 1 << 9,
  AEK_FLOAT1E3 = 1 << 10,
  AEK_FLOAT3E4 = 1 << 11,
  AEK_FLOAT7E60 = 1 << 12,
  AEK_HWDIV = 1 << 13,
  AEK_STLD = 1 << 14,
  AEK_PUSHPOP = 1 << 15,
  AEK_EDSP = 1 << 16,
  AEK_DSP1E2 = 1 << 17,
  AEK_DSPE60 = 1 << 18,
  AEK_DSPV2 = 1 << 19,
  AEK_DSPSILAN = 1 << 20,
  AEK_ELRW = 1 << 21,
  AEK_TRUST = 1 << 22,
  AEK_JAVA = 1 << 23,
  AEK_CACHE = 1 << 24,
  AEK_NVIC = 1 << 25,
  AEK_DOLOOP = 1 << 26,
  AEK_HIGHREG = 1 << 27,
  AEK_SMART = 1 << 28,
  AEK_VDSP2E3 = 1 << 29,
  AEK_VDSP2E60F = 1 << 30,
  AEK_VDSPV2 = 1ULL << 31,
  AEK_HARDTP = 1ULL << 32,
  AEK_SOFTTP = 1ULL << 33,
  AEK_ISTACK = 1ULL << 34,
  AEK_CONSTPOOL = 1ULL << 35,
  AEK_STACKSIZE = 1ULL << 36,
  AEK_CCRT = 1ULL << 37,
  AEK_VDSPV1 = 1ULL << 38,
  AEK_E1 = 1ULL << 39,
  AEK_E2 = 1ULL << 40,
  AEK_2E3 = 1ULL << 41,
  AEK_MP = 1ULL << 42,
  AEK_3E3R1 = 1ULL << 43,
  AEK_3E3R2 = 1ULL << 44,
  AEK_3E3R3 = 1ULL << 45,
  AEK_3E7 = 1ULL << 46,
  AEK_MP1E2 = 1ULL << 47,
  AEK_7E10 = 1ULL << 48,
  AEK_10E60 = 1ULL << 49

};

// Arch extension modifiers for CPUs.
enum MultiArchExtKind : uint64_t {
  MAEK_E1 = CSKY::AEK_E1 | CSKY::AEK_ELRW,
  MAEK_E2 = CSKY::AEK_E2 | CSKY::MAEK_E1,
  MAEK_2E3 = CSKY::AEK_2E3 | CSKY::MAEK_E2,
  MAEK_MP = CSKY::AEK_MP | CSKY::MAEK_2E3,
  MAEK_3E3R1 = CSKY::AEK_3E3R1,
  MAEK_3E3R2 = CSKY::AEK_3E3R1 | CSKY::AEK_3E3R2 | CSKY::AEK_DOLOOP,
  MAEK_3E7 = CSKY::AEK_3E7 | CSKY::MAEK_2E3,
  MAEK_MP1E2 = CSKY::AEK_MP1E2 | CSKY::MAEK_3E7,
  MAEK_7E10 = CSKY::AEK_7E10 | CSKY::MAEK_3E7,
  MAEK_10E60 = CSKY::AEK_10E60 | CSKY::MAEK_7E10,
};
// FPU names.
enum CSKYFPUKind {
#define CSKY_FPU(NAME, KIND, VERSION) KIND,
#include "CSKYTargetParser.def"
  FK_LAST
};

// FPU Version
enum class FPUVersion {
  NONE,
  FPV2,
  FPV3,
};

// Arch names.
enum class ArchKind {
#define CSKY_ARCH(NAME, ID, ARCH_BASE_EXT) ID,
#include "CSKYTargetParser.def"
};

// List of Arch Extension names.
// FIXME: TableGen this.
struct ExtName {
  const char *NameCStr;
  size_t NameLength;
  uint64_t ID;
  const char *Feature;
  const char *NegFeature;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

const CSKY::ExtName CSKYARCHExtNames[] = {
#define CSKY_ARCH_EXT_NAME(NAME, ID, FEATURE, NEGFEATURE)                      \
  {NAME, sizeof(NAME) - 1, ID, FEATURE, NEGFEATURE},
#include "CSKYTargetParser.def"
};

// List of CPU names and their arches.
template <typename T> struct CpuNames {
  const char *NameCStr;
  size_t NameLength;
  T ArchID;
  uint64_t defaultExt;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};
const CpuNames<CSKY::ArchKind> CPUNames[] = {
#define CSKY_CPU_NAME(NAME, ARCH_ID, DEFAULT_EXT)                              \
  {NAME, sizeof(NAME) - 1, CSKY::ArchKind::ARCH_ID, DEFAULT_EXT},
#include "llvm/TargetParser/CSKYTargetParser.def"
};

// FIXME: TableGen this.
// The entries must appear in the order listed in CSKY::CSKYFPUKind for correct
// indexing
struct FPUName {
  const char *NameCStr;
  size_t NameLength;
  CSKYFPUKind ID;
  FPUVersion FPUVer;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

static const FPUName FPUNames[] = {
#define CSKY_FPU(NAME, KIND, VERSION) {NAME, sizeof(NAME) - 1, KIND, VERSION},
#include "llvm/TargetParser/CSKYTargetParser.def"
};

// List of canonical arch names.
template <typename T> struct ArchNames {
  const char *NameCStr;
  size_t NameLength;
  T ID;
  uint64_t archBaseExt;
  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};
const ArchNames<CSKY::ArchKind> ARCHNames[] = {
#define CSKY_ARCH(NAME, ID, ARCH_BASE_EXT)                                     \
  {NAME, sizeof(NAME) - 1, CSKY::ArchKind::ID, ARCH_BASE_EXT},
#include "llvm/TargetParser/CSKYTargetParser.def"
};

StringRef getArchName(ArchKind AK);
StringRef getDefaultCPU(StringRef Arch);
StringRef getArchExtName(uint64_t ArchExtKind);
StringRef getArchExtFeature(StringRef ArchExt);
uint64_t getDefaultExtensions(StringRef CPU);
bool getExtensionFeatures(uint64_t Extensions,
                          std::vector<StringRef> &Features);

// Information by ID
StringRef getFPUName(unsigned FPUKind);
FPUVersion getFPUVersion(unsigned FPUKind);

bool getFPUFeatures(CSKYFPUKind Kind, std::vector<StringRef> &Features);

// Parser
ArchKind parseArch(StringRef Arch);
ArchKind parseCPUArch(StringRef CPU);
uint64_t parseArchExt(StringRef ArchExt);
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);

} // namespace CSKY

} // namespace llvm

#endif
