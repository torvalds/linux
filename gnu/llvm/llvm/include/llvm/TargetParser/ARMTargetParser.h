//===-- ARMTargetParser - Parser for ARM target features --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise ARM hardware features
// such as FPU/CPU/ARCH/extensions and specific support such as HWDIV.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_ARMTARGETPARSER_H
#define LLVM_TARGETPARSER_ARMTARGETPARSER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include <vector>

namespace llvm {

class Triple;

namespace ARM {

// Arch extension modifiers for CPUs.
// Note that this is not the same as the AArch64 list
enum ArchExtKind : uint64_t {
  AEK_INVALID = 0,
  AEK_NONE = 1,
  AEK_CRC = 1 << 1,
  AEK_CRYPTO = 1 << 2,
  AEK_FP = 1 << 3,
  AEK_HWDIVTHUMB = 1 << 4,
  AEK_HWDIVARM = 1 << 5,
  AEK_MP = 1 << 6,
  AEK_SIMD = 1 << 7,
  AEK_SEC = 1 << 8,
  AEK_VIRT = 1 << 9,
  AEK_DSP = 1 << 10,
  AEK_FP16 = 1 << 11,
  AEK_RAS = 1 << 12,
  AEK_DOTPROD = 1 << 13,
  AEK_SHA2 = 1 << 14,
  AEK_AES = 1 << 15,
  AEK_FP16FML = 1 << 16,
  AEK_SB = 1 << 17,
  AEK_FP_DP = 1 << 18,
  AEK_LOB = 1 << 19,
  AEK_BF16 = 1 << 20,
  AEK_I8MM = 1 << 21,
  AEK_CDECP0 = 1 << 22,
  AEK_CDECP1 = 1 << 23,
  AEK_CDECP2 = 1 << 24,
  AEK_CDECP3 = 1 << 25,
  AEK_CDECP4 = 1 << 26,
  AEK_CDECP5 = 1 << 27,
  AEK_CDECP6 = 1 << 28,
  AEK_CDECP7 = 1 << 29,
  AEK_PACBTI = 1 << 30,
  // Unsupported extensions.
  AEK_OS = 1ULL << 59,
  AEK_IWMMXT = 1ULL << 60,
  AEK_IWMMXT2 = 1ULL << 61,
  AEK_MAVERICK = 1ULL << 62,
  AEK_XSCALE = 1ULL << 63,
};

// List of Arch Extension names.
struct ExtName {
  StringRef Name;
  uint64_t ID;
  StringRef Feature;
  StringRef NegFeature;
};

const ExtName ARCHExtNames[] = {
#define ARM_ARCH_EXT_NAME(NAME, ID, FEATURE, NEGFEATURE)                       \
  {NAME, ID, FEATURE, NEGFEATURE},
#include "ARMTargetParser.def"
};

// List of HWDiv names (use getHWDivSynonym) and which architectural
// features they correspond to (use getHWDivFeatures).
const struct {
  StringRef Name;
  uint64_t ID;
} HWDivNames[] = {
#define ARM_HW_DIV_NAME(NAME, ID) {NAME, ID},
#include "ARMTargetParser.def"
};

// Arch names.
enum class ArchKind {
#define ARM_ARCH(NAME, ID, CPU_ATTR, ARCH_FEATURE, ARCH_ATTR, ARCH_FPU,        \
                 ARCH_BASE_EXT)                                                \
  ID,
#include "ARMTargetParser.def"
};

// List of CPU names and their arches.
// The same CPU can have multiple arches and can be default on multiple arches.
// When finding the Arch for a CPU, first-found prevails. Sort them accordingly.
// When this becomes table-generated, we'd probably need two tables.
struct CpuNames {
  StringRef Name;
  ArchKind ArchID;
  bool Default; // is $Name the default CPU for $ArchID ?
  uint64_t DefaultExtensions;
};

const CpuNames CPUNames[] = {
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  {NAME, ARM::ArchKind::ID, IS_DEFAULT, DEFAULT_EXT},
#include "ARMTargetParser.def"
};

// FPU names.
enum FPUKind {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION) KIND,
#include "ARMTargetParser.def"
  FK_LAST
};

// FPU Version
enum class FPUVersion {
  NONE,
  VFPV2,
  VFPV3,
  VFPV3_FP16,
  VFPV4,
  VFPV5,
  VFPV5_FULLFP16,
};

// An FPU name restricts the FPU in one of three ways:
enum class FPURestriction {
  None = 0, ///< No restriction
  D16,      ///< Only 16 D registers
  SP_D16    ///< Only single-precision instructions, with 16 D registers
};

inline bool isDoublePrecision(const FPURestriction restriction) {
  return restriction != FPURestriction::SP_D16;
}

inline bool has32Regs(const FPURestriction restriction) {
  return restriction == FPURestriction::None;
}

// An FPU name implies one of three levels of Neon support:
enum class NeonSupportLevel {
  None = 0, ///< No Neon
  Neon,     ///< Neon
  Crypto    ///< Neon with Crypto
};

// v6/v7/v8 Profile
enum class ProfileKind { INVALID = 0, A, R, M };

// List of canonical FPU names (use getFPUSynonym) and which architectural
// features they correspond to (use getFPUFeatures).
// The entries must appear in the order listed in ARM::FPUKind for correct
// indexing
struct FPUName {
  StringRef Name;
  FPUKind ID;
  FPUVersion FPUVer;
  NeonSupportLevel NeonSupport;
  FPURestriction Restriction;
};

static const FPUName FPUNames[] = {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION)                \
  {NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION},
#include "llvm/TargetParser/ARMTargetParser.def"
};

// List of canonical arch names (use getArchSynonym).
// This table also provides the build attribute fields for CPU arch
// and Arch ID, according to the Addenda to the ARM ABI, chapters
// 2.4 and 2.3.5.2 respectively.
// FIXME: SubArch values were simplified to fit into the expectations
// of the triples and are not conforming with their official names.
// Check to see if the expectation should be changed.
struct ArchNames {
  StringRef Name;
  StringRef CPUAttr; // CPU class in build attributes.
  StringRef ArchFeature;
  FPUKind DefaultFPU;
  uint64_t ArchBaseExtensions;
  ArchKind ID;
  ARMBuildAttrs::CPUArch ArchAttr; // Arch ID in build attributes.

  // Return ArchFeature without the leading "+".
  StringRef getSubArch() const { return ArchFeature.substr(1); }
};

static const ArchNames ARMArchNames[] = {
#define ARM_ARCH(NAME, ID, CPU_ATTR, ARCH_FEATURE, ARCH_ATTR, ARCH_FPU,        \
                 ARCH_BASE_EXT)                                                \
  {NAME,          CPU_ATTR,     ARCH_FEATURE, ARCH_FPU,                        \
   ARCH_BASE_EXT, ArchKind::ID, ARCH_ATTR},
#include "llvm/TargetParser/ARMTargetParser.def"
};

inline ArchKind &operator--(ArchKind &Kind) {
  assert((Kind >= ArchKind::ARMV8A && Kind <= ArchKind::ARMV9_3A) &&
         "We only expect operator-- to be called with ARMV8/V9");
  if (Kind == ArchKind::INVALID || Kind == ArchKind::ARMV8A ||
      Kind == ArchKind::ARMV8_1A || Kind == ArchKind::ARMV9A ||
      Kind == ArchKind::ARMV8R)
    Kind = ArchKind::INVALID;
  else {
    unsigned KindAsInteger = static_cast<unsigned>(Kind);
    Kind = static_cast<ArchKind>(--KindAsInteger);
  }
  return Kind;
}

// Information by ID
StringRef getFPUName(FPUKind FPUKind);
FPUVersion getFPUVersion(FPUKind FPUKind);
NeonSupportLevel getFPUNeonSupportLevel(FPUKind FPUKind);
FPURestriction getFPURestriction(FPUKind FPUKind);

bool getFPUFeatures(FPUKind FPUKind, std::vector<StringRef> &Features);
bool getHWDivFeatures(uint64_t HWDivKind, std::vector<StringRef> &Features);
bool getExtensionFeatures(uint64_t Extensions,
                          std::vector<StringRef> &Features);

StringRef getArchName(ArchKind AK);
unsigned getArchAttr(ArchKind AK);
StringRef getCPUAttr(ArchKind AK);
StringRef getSubArch(ArchKind AK);
StringRef getArchExtName(uint64_t ArchExtKind);
StringRef getArchExtFeature(StringRef ArchExt);
bool appendArchExtFeatures(StringRef CPU, ARM::ArchKind AK, StringRef ArchExt,
                           std::vector<StringRef> &Features,
                           FPUKind &ArgFPUKind);
ArchKind convertV9toV8(ArchKind AK);

// Information by Name
FPUKind getDefaultFPU(StringRef CPU, ArchKind AK);
uint64_t getDefaultExtensions(StringRef CPU, ArchKind AK);
StringRef getDefaultCPU(StringRef Arch);
StringRef getCanonicalArchName(StringRef Arch);
StringRef getFPUSynonym(StringRef FPU);

// Parser
uint64_t parseHWDiv(StringRef HWDiv);
FPUKind parseFPU(StringRef FPU);
ArchKind parseArch(StringRef Arch);
uint64_t parseArchExt(StringRef ArchExt);
ArchKind parseCPUArch(StringRef CPU);
ProfileKind parseArchProfile(StringRef Arch);
unsigned parseArchVersion(StringRef Arch);

void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);
StringRef computeDefaultTargetABI(const Triple &TT, StringRef CPU);

/// Get the (LLVM) name of the minimum ARM CPU for the arch we are targeting.
///
/// \param Arch the architecture name (e.g., "armv7s"). If it is an empty
/// string then the triple's arch name is used.
StringRef getARMCPUForArch(const llvm::Triple &Triple, StringRef MArch = {});

void PrintSupportedExtensions(StringMap<StringRef> DescMap);

} // namespace ARM
} // namespace llvm

#endif
