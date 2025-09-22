//===--- Sparc.cpp - Implement Sparc target feature support ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Sparc TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Sparc.h"
#include "Targets.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

const char *const SparcTargetInfo::GCCRegNames[] = {
    // Integer registers
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9",  "r10",
    "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21",
    "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",

    // Floating-point registers
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",  "f8",  "f9",  "f10",
    "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21",
    "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31", "f32",
    "f34", "f36", "f38", "f40", "f42", "f44", "f46", "f48", "f50", "f52", "f54",
    "f56", "f58", "f60", "f62",
};

ArrayRef<const char *> SparcTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias SparcTargetInfo::GCCRegAliases[] = {
    {{"g0"}, "r0"},  {{"g1"}, "r1"},  {{"g2"}, "r2"},        {{"g3"}, "r3"},
    {{"g4"}, "r4"},  {{"g5"}, "r5"},  {{"g6"}, "r6"},        {{"g7"}, "r7"},
    {{"o0"}, "r8"},  {{"o1"}, "r9"},  {{"o2"}, "r10"},       {{"o3"}, "r11"},
    {{"o4"}, "r12"}, {{"o5"}, "r13"}, {{"o6", "sp"}, "r14"}, {{"o7"}, "r15"},
    {{"l0"}, "r16"}, {{"l1"}, "r17"}, {{"l2"}, "r18"},       {{"l3"}, "r19"},
    {{"l4"}, "r20"}, {{"l5"}, "r21"}, {{"l6"}, "r22"},       {{"l7"}, "r23"},
    {{"i0"}, "r24"}, {{"i1"}, "r25"}, {{"i2"}, "r26"},       {{"i3"}, "r27"},
    {{"i4"}, "r28"}, {{"i5"}, "r29"}, {{"i6", "fp"}, "r30"}, {{"i7"}, "r31"},
};

ArrayRef<TargetInfo::GCCRegAlias> SparcTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

bool SparcTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("softfloat", SoftFloat)
      .Case("sparc", true)
      .Default(false);
}

struct SparcCPUInfo {
  llvm::StringLiteral Name;
  SparcTargetInfo::CPUKind Kind;
  SparcTargetInfo::CPUGeneration Generation;
};

static constexpr SparcCPUInfo CPUInfo[] = {
    {{"v8"}, SparcTargetInfo::CK_V8, SparcTargetInfo::CG_V8},
    {{"supersparc"}, SparcTargetInfo::CK_SUPERSPARC, SparcTargetInfo::CG_V8},
    {{"sparclite"}, SparcTargetInfo::CK_SPARCLITE, SparcTargetInfo::CG_V8},
    {{"f934"}, SparcTargetInfo::CK_F934, SparcTargetInfo::CG_V8},
    {{"hypersparc"}, SparcTargetInfo::CK_HYPERSPARC, SparcTargetInfo::CG_V8},
    {{"sparclite86x"},
     SparcTargetInfo::CK_SPARCLITE86X,
     SparcTargetInfo::CG_V8},
    {{"sparclet"}, SparcTargetInfo::CK_SPARCLET, SparcTargetInfo::CG_V8},
    {{"tsc701"}, SparcTargetInfo::CK_TSC701, SparcTargetInfo::CG_V8},
    {{"v9"}, SparcTargetInfo::CK_V9, SparcTargetInfo::CG_V9},
    {{"ultrasparc"}, SparcTargetInfo::CK_ULTRASPARC, SparcTargetInfo::CG_V9},
    {{"ultrasparc3"}, SparcTargetInfo::CK_ULTRASPARC3, SparcTargetInfo::CG_V9},
    {{"niagara"}, SparcTargetInfo::CK_NIAGARA, SparcTargetInfo::CG_V9},
    {{"niagara2"}, SparcTargetInfo::CK_NIAGARA2, SparcTargetInfo::CG_V9},
    {{"niagara3"}, SparcTargetInfo::CK_NIAGARA3, SparcTargetInfo::CG_V9},
    {{"niagara4"}, SparcTargetInfo::CK_NIAGARA4, SparcTargetInfo::CG_V9},
    {{"ma2100"}, SparcTargetInfo::CK_MYRIAD2100, SparcTargetInfo::CG_V8},
    {{"ma2150"}, SparcTargetInfo::CK_MYRIAD2150, SparcTargetInfo::CG_V8},
    {{"ma2155"}, SparcTargetInfo::CK_MYRIAD2155, SparcTargetInfo::CG_V8},
    {{"ma2450"}, SparcTargetInfo::CK_MYRIAD2450, SparcTargetInfo::CG_V8},
    {{"ma2455"}, SparcTargetInfo::CK_MYRIAD2455, SparcTargetInfo::CG_V8},
    {{"ma2x5x"}, SparcTargetInfo::CK_MYRIAD2x5x, SparcTargetInfo::CG_V8},
    {{"ma2080"}, SparcTargetInfo::CK_MYRIAD2080, SparcTargetInfo::CG_V8},
    {{"ma2085"}, SparcTargetInfo::CK_MYRIAD2085, SparcTargetInfo::CG_V8},
    {{"ma2480"}, SparcTargetInfo::CK_MYRIAD2480, SparcTargetInfo::CG_V8},
    {{"ma2485"}, SparcTargetInfo::CK_MYRIAD2485, SparcTargetInfo::CG_V8},
    {{"ma2x8x"}, SparcTargetInfo::CK_MYRIAD2x8x, SparcTargetInfo::CG_V8},
    {{"leon2"}, SparcTargetInfo::CK_LEON2, SparcTargetInfo::CG_V8},
    {{"at697e"}, SparcTargetInfo::CK_LEON2_AT697E, SparcTargetInfo::CG_V8},
    {{"at697f"}, SparcTargetInfo::CK_LEON2_AT697F, SparcTargetInfo::CG_V8},
    {{"leon3"}, SparcTargetInfo::CK_LEON3, SparcTargetInfo::CG_V8},
    {{"ut699"}, SparcTargetInfo::CK_LEON3_UT699, SparcTargetInfo::CG_V8},
    {{"gr712rc"}, SparcTargetInfo::CK_LEON3_GR712RC, SparcTargetInfo::CG_V8},
    {{"leon4"}, SparcTargetInfo::CK_LEON4, SparcTargetInfo::CG_V8},
    {{"gr740"}, SparcTargetInfo::CK_LEON4_GR740, SparcTargetInfo::CG_V8},
};

SparcTargetInfo::CPUGeneration
SparcTargetInfo::getCPUGeneration(CPUKind Kind) const {
  if (Kind == CK_GENERIC)
    return CG_V8;
  const SparcCPUInfo *Item = llvm::find_if(
      CPUInfo, [Kind](const SparcCPUInfo &Info) { return Info.Kind == Kind; });
  if (Item == std::end(CPUInfo))
    llvm_unreachable("Unexpected CPU kind");
  return Item->Generation;
}

SparcTargetInfo::CPUKind SparcTargetInfo::getCPUKind(StringRef Name) const {
  const SparcCPUInfo *Item = llvm::find_if(
      CPUInfo, [Name](const SparcCPUInfo &Info) { return Info.Name == Name; });

  if (Item == std::end(CPUInfo))
    return CK_GENERIC;
  return Item->Kind;
}

void SparcTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  for (const SparcCPUInfo &Info : CPUInfo)
    Values.push_back(Info.Name);
}

void SparcTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  DefineStd(Builder, "sparc", Opts);
  Builder.defineMacro("__REGISTER_PREFIX__", "");

  if (SoftFloat)
    Builder.defineMacro("SOFT_FLOAT", "1");
}

void SparcV8TargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  SparcTargetInfo::getTargetDefines(Opts, Builder);
  if (getTriple().isOSSolaris())
    Builder.defineMacro("__sparcv8");
  else {
    switch (getCPUGeneration(CPU)) {
    case CG_V8:
      Builder.defineMacro("__sparcv8");
      Builder.defineMacro("__sparcv8__");
      break;
    case CG_V9:
      Builder.defineMacro("__sparc_v9__");
      break;
    }
  }
  if (getCPUGeneration(CPU) == CG_V9) {
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
  }
}

void SparcV9TargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  SparcTargetInfo::getTargetDefines(Opts, Builder);
  Builder.defineMacro("__sparcv9");
  Builder.defineMacro("__arch64__");
  // Solaris doesn't need these variants, but the BSDs do.
  if (!getTriple().isOSSolaris()) {
    Builder.defineMacro("__sparc64__");
    Builder.defineMacro("__sparc_v9__");
    Builder.defineMacro("__sparcv9__");
  }

  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}

void SparcV9TargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  for (const SparcCPUInfo &Info : CPUInfo)
    if (Info.Generation == CG_V9)
      Values.push_back(Info.Name);
}
