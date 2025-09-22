//===-- X86TargetParser - Parser for X86 features ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise X86 hardware features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_X86TARGETPARSER_H
#define LLVM_TARGETPARSER_X86TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include <array>

namespace llvm {
template <typename T> class SmallVectorImpl;
class StringRef;

namespace X86 {

// This should be kept in sync with libcc/compiler-rt as its included by clang
// as a proxy for what's in libgcc/compiler-rt.
enum ProcessorVendors : unsigned {
  VENDOR_DUMMY,
#define X86_VENDOR(ENUM, STRING) \
  ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  VENDOR_OTHER
};

// This should be kept in sync with libcc/compiler-rt as its included by clang
// as a proxy for what's in libgcc/compiler-rt.
enum ProcessorTypes : unsigned {
  CPU_TYPE_DUMMY,
#define X86_CPU_TYPE(ENUM, STRING) \
  ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_TYPE_MAX
};

// This should be kept in sync with libcc/compiler-rt as its included by clang
// as a proxy for what's in libgcc/compiler-rt.
enum ProcessorSubtypes : unsigned {
  CPU_SUBTYPE_DUMMY,
#define X86_CPU_SUBTYPE(ENUM, STRING) \
  ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_SUBTYPE_MAX
};

// This should be kept in sync with libcc/compiler-rt as it should be used
// by clang as a proxy for what's in libgcc/compiler-rt.
enum ProcessorFeatures {
#define X86_FEATURE(ENUM, STRING) FEATURE_##ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_FEATURE_MAX,

#define X86_MICROARCH_LEVEL(ENUM, STRING, PRIORITY) FEATURE_##ENUM = PRIORITY,
#include "llvm/TargetParser/X86TargetParser.def"
};

enum CPUKind {
  CK_None,
  CK_i386,
  CK_i486,
  CK_WinChipC6,
  CK_WinChip2,
  CK_C3,
  CK_i586,
  CK_Pentium,
  CK_PentiumMMX,
  CK_PentiumPro,
  CK_i686,
  CK_Pentium2,
  CK_Pentium3,
  CK_PentiumM,
  CK_C3_2,
  CK_Yonah,
  CK_Pentium4,
  CK_Prescott,
  CK_Nocona,
  CK_Core2,
  CK_Penryn,
  CK_Bonnell,
  CK_Silvermont,
  CK_Goldmont,
  CK_GoldmontPlus,
  CK_Tremont,
  CK_Gracemont,
  CK_Nehalem,
  CK_Westmere,
  CK_SandyBridge,
  CK_IvyBridge,
  CK_Haswell,
  CK_Broadwell,
  CK_SkylakeClient,
  CK_SkylakeServer,
  CK_Cascadelake,
  CK_Cooperlake,
  CK_Cannonlake,
  CK_IcelakeClient,
  CK_Rocketlake,
  CK_IcelakeServer,
  CK_Tigerlake,
  CK_SapphireRapids,
  CK_Alderlake,
  CK_Raptorlake,
  CK_Meteorlake,
  CK_Arrowlake,
  CK_ArrowlakeS,
  CK_Lunarlake,
  CK_Pantherlake,
  CK_Sierraforest,
  CK_Grandridge,
  CK_Graniterapids,
  CK_GraniterapidsD,
  CK_Emeraldrapids,
  CK_Clearwaterforest,
  CK_KNL,
  CK_KNM,
  CK_Lakemont,
  CK_K6,
  CK_K6_2,
  CK_K6_3,
  CK_Athlon,
  CK_AthlonXP,
  CK_K8,
  CK_K8SSE3,
  CK_AMDFAM10,
  CK_BTVER1,
  CK_BTVER2,
  CK_BDVER1,
  CK_BDVER2,
  CK_BDVER3,
  CK_BDVER4,
  CK_ZNVER1,
  CK_ZNVER2,
  CK_ZNVER3,
  CK_ZNVER4,
  CK_x86_64,
  CK_x86_64_v2,
  CK_x86_64_v3,
  CK_x86_64_v4,
  CK_Geode,
  CK_ZNVER5,
};

/// Parse \p CPU string into a CPUKind. Will only accept 64-bit capable CPUs if
/// \p Only64Bit is true.
CPUKind parseArchX86(StringRef CPU, bool Only64Bit = false);
CPUKind parseTuneCPU(StringRef CPU, bool Only64Bit = false);

/// Provide a list of valid CPU names. If \p Only64Bit is true, the list will
/// only contain 64-bit capable CPUs.
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values,
                          bool Only64Bit = false);
/// Provide a list of valid -mtune names.
void fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values,
                          bool Only64Bit = false);

/// Get the key feature prioritizing target multiversioning.
ProcessorFeatures getKeyFeature(CPUKind Kind);

/// Fill in the features that \p CPU supports into \p Features.
/// "+" will be append in front of each feature if NeedPlus is true.
void getFeaturesForCPU(StringRef CPU, SmallVectorImpl<StringRef> &Features,
                       bool NeedPlus = false);

/// Set or clear entries in \p Features that are implied to be enabled/disabled
/// by the provided \p Feature.
void updateImpliedFeatures(StringRef Feature, bool Enabled,
                           StringMap<bool> &Features);

char getCPUDispatchMangling(StringRef Name);
bool validateCPUSpecificCPUDispatch(StringRef Name);
std::array<uint32_t, 4> getCpuSupportsMask(ArrayRef<StringRef> FeatureStrs);
unsigned getFeaturePriority(ProcessorFeatures Feat);

} // namespace X86
} // namespace llvm

#endif
