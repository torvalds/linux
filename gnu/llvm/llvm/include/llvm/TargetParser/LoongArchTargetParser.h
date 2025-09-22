//==-- LoongArch64TargetParser - Parser for LoongArch64 features --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise LoongArch hardware features
// such as CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_LOONGARCHTARGETPARSER_H
#define LLVM_TARGETPARSER_LOONGARCHTARGETPARSER_H

#include "llvm/TargetParser/Triple.h"
#include <vector>

namespace llvm {
class StringRef;

namespace LoongArch {

enum FeatureKind : uint32_t {
  // 64-bit ISA is available.
  FK_64BIT = 1 << 1,

  // Single-precision floating-point instructions are available.
  FK_FP32 = 1 << 2,

  // Double-precision floating-point instructions are available.
  FK_FP64 = 1 << 3,

  // Loongson SIMD Extension is available.
  FK_LSX = 1 << 4,

  // Loongson Advanced SIMD Extension is available.
  FK_LASX = 1 << 5,

  // Loongson Binary Translation Extension is available.
  FK_LBT = 1 << 6,

  // Loongson Virtualization Extension is available.
  FK_LVZ = 1 << 7,

  // Allow memory accesses to be unaligned.
  FK_UAL = 1 << 8,

  // Floating-point approximate reciprocal instructions are available.
  FK_FRECIPE = 1 << 9,
};

struct FeatureInfo {
  StringRef Name;
  FeatureKind Kind;
};

enum class ArchKind {
#define LOONGARCH_ARCH(NAME, KIND, FEATURES) KIND,
#include "LoongArchTargetParser.def"
};

struct ArchInfo {
  StringRef Name;
  ArchKind Kind;
  uint32_t Features;
};

bool isValidArchName(StringRef Arch);
bool getArchFeatures(StringRef Arch, std::vector<StringRef> &Features);
bool isValidCPUName(StringRef TuneCPU);
void fillValidCPUList(SmallVectorImpl<StringRef> &Values);
StringRef getDefaultArch(bool Is64Bit);

} // namespace LoongArch

} // namespace llvm

#endif // LLVM_TARGETPARSER_LOONGARCHTARGETPARSER_H
