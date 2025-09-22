//===--- CSKY.cpp - Implement CSKY target feature support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements CSKY TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "CSKY.h"

using namespace clang;
using namespace clang::targets;

bool CSKYTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::CSKY::parseCPUArch(Name) != llvm::CSKY::ArchKind::INVALID;
}

bool CSKYTargetInfo::setCPU(const std::string &Name) {
  llvm::CSKY::ArchKind archKind = llvm::CSKY::parseCPUArch(Name);
  bool isValid = (archKind != llvm::CSKY::ArchKind::INVALID);

  if (isValid) {
    CPU = Name;
    Arch = archKind;
  }

  return isValid;
}

void CSKYTargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  Builder.defineMacro("__csky__", "2");
  Builder.defineMacro("__CSKY__", "2");
  Builder.defineMacro("__ckcore__", "2");
  Builder.defineMacro("__CKCORE__", "2");

  Builder.defineMacro("__CSKYABI__", ABI == "abiv2" ? "2" : "1");
  Builder.defineMacro("__cskyabi__", ABI == "abiv2" ? "2" : "1");

  StringRef ArchName = "ck810";
  StringRef CPUName = "ck810";

  if (Arch != llvm::CSKY::ArchKind::INVALID) {
    ArchName = llvm::CSKY::getArchName(Arch);
    CPUName = CPU;
  }

  Builder.defineMacro("__" + ArchName.upper() + "__");
  Builder.defineMacro("__" + ArchName.lower() + "__");
  if (ArchName != CPUName) {
    Builder.defineMacro("__" + CPUName.upper() + "__");
    Builder.defineMacro("__" + CPUName.lower() + "__");
  }

  // TODO: Add support for BE if BE was supported later
  StringRef endian = "__cskyLE__";

  Builder.defineMacro(endian);
  Builder.defineMacro(endian.upper());
  Builder.defineMacro(endian.lower());

  if (DSPV2) {
    StringRef dspv2 = "__CSKY_DSPV2__";
    Builder.defineMacro(dspv2);
    Builder.defineMacro(dspv2.lower());
  }

  if (VDSPV2) {
    StringRef vdspv2 = "__CSKY_VDSPV2__";
    Builder.defineMacro(vdspv2);
    Builder.defineMacro(vdspv2.lower());

    if (HardFloat) {
      StringRef vdspv2_f = "__CSKY_VDSPV2_F__";
      Builder.defineMacro(vdspv2_f);
      Builder.defineMacro(vdspv2_f.lower());
    }
  }
  if (VDSPV1) {
    StringRef vdspv1_64 = "__CSKY_VDSP64__";
    StringRef vdspv1_128 = "__CSKY_VDSP128__";

    Builder.defineMacro(vdspv1_64);
    Builder.defineMacro(vdspv1_64.lower());
    Builder.defineMacro(vdspv1_128);
    Builder.defineMacro(vdspv1_128.lower());
  }
  if (is3E3R1) {
    StringRef is3e3r1 = "__CSKY_3E3R1__";
    Builder.defineMacro(is3e3r1);
    Builder.defineMacro(is3e3r1.lower());
  }
}

bool CSKYTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("hard-float", HardFloat)
      .Case("hard-float-abi", HardFloatABI)
      .Case("fpuv2_sf", FPUV2_SF)
      .Case("fpuv2_df", FPUV2_DF)
      .Case("fpuv3_sf", FPUV3_SF)
      .Case("fpuv3_df", FPUV3_DF)
      .Case("vdspv2", VDSPV2)
      .Case("dspv2", DSPV2)
      .Case("vdspv1", VDSPV1)
      .Case("3e3r1", is3E3R1)
      .Default(false);
}

bool CSKYTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                          DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature == "+hard-float")
      HardFloat = true;
    if (Feature == "+hard-float-abi")
      HardFloatABI = true;
    if (Feature == "+fpuv2_sf")
      FPUV2_SF = true;
    if (Feature == "+fpuv2_df")
      FPUV2_DF = true;
    if (Feature == "+fpuv3_sf")
      FPUV3_SF = true;
    if (Feature == "+fpuv3_df")
      FPUV3_DF = true;
    if (Feature == "+vdspv2")
      VDSPV2 = true;
    if (Feature == "+dspv2")
      DSPV2 = true;
    if (Feature == "+vdspv1")
      VDSPV1 = true;
    if (Feature == "+3e3r1")
      is3E3R1 = true;
  }

  return true;
}

ArrayRef<Builtin::Info> CSKYTargetInfo::getTargetBuiltins() const {
  return ArrayRef<Builtin::Info>();
}

ArrayRef<const char *> CSKYTargetInfo::getGCCRegNames() const {
  static const char *const GCCRegNames[] = {
      // Integer registers
      "r0",
      "r1",
      "r2",
      "r3",
      "r4",
      "r5",
      "r6",
      "r7",
      "r8",
      "r9",
      "r10",
      "r11",
      "r12",
      "r13",
      "r14",
      "r15",
      "r16",
      "r17",
      "r18",
      "r19",
      "r20",
      "r21",
      "r22",
      "r23",
      "r24",
      "r25",
      "r26",
      "r27",
      "r28",
      "r29",
      "r30",
      "r31",

      // Floating point registers
      "fr0",
      "fr1",
      "fr2",
      "fr3",
      "fr4",
      "fr5",
      "fr6",
      "fr7",
      "fr8",
      "fr9",
      "fr10",
      "fr11",
      "fr12",
      "fr13",
      "fr14",
      "fr15",
      "fr16",
      "fr17",
      "fr18",
      "fr19",
      "fr20",
      "fr21",
      "fr22",
      "fr23",
      "fr24",
      "fr25",
      "fr26",
      "fr27",
      "fr28",
      "fr29",
      "fr30",
      "fr31",

  };
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> CSKYTargetInfo::getGCCRegAliases() const {
  static const TargetInfo::GCCRegAlias GCCRegAliases[] = {
      {{"a0"}, "r0"},
      {{"a1"}, "r1"},
      {{"a2"}, "r2"},
      {{"a3"}, "r3"},
      {{"l0"}, "r4"},
      {{"l1"}, "r5"},
      {{"l2"}, "r6"},
      {{"l3"}, "r7"},
      {{"l4"}, "r8"},
      {{"l5"}, "r9"},
      {{"l6"}, "r10"},
      {{"l7"}, "r11"},
      {{"t0"}, "r12"},
      {{"t1"}, "r13"},
      {{"sp"}, "r14"},
      {{"lr"}, "r15"},
      {{"l8"}, "r16"},
      {{"l9"}, "r17"},
      {{"t2"}, "r18"},
      {{"t3"}, "r19"},
      {{"t4"}, "r20"},
      {{"t5"}, "r21"},
      {{"t6"}, "r22"},
      {{"t7", "fp"}, "r23"},
      {{"t8", "top"}, "r24"},
      {{"t9", "bsp"}, "r25"},
      {{"r26"}, "r26"},
      {{"r27"}, "r27"},
      {{"gb", "rgb", "rdb"}, "r28"},
      {{"tb", "rtb"}, "r29"},
      {{"svbr"}, "r30"},
      {{"tls"}, "r31"},

      {{"vr0"}, "fr0"},
      {{"vr1"}, "fr1"},
      {{"vr2"}, "fr2"},
      {{"vr3"}, "fr3"},
      {{"vr4"}, "fr4"},
      {{"vr5"}, "fr5"},
      {{"vr6"}, "fr6"},
      {{"vr7"}, "fr7"},
      {{"vr8"}, "fr8"},
      {{"vr9"}, "fr9"},
      {{"vr10"}, "fr10"},
      {{"vr11"}, "fr11"},
      {{"vr12"}, "fr12"},
      {{"vr13"}, "fr13"},
      {{"vr14"}, "fr14"},
      {{"vr15"}, "fr15"},
      {{"vr16"}, "fr16"},
      {{"vr17"}, "fr17"},
      {{"vr18"}, "fr18"},
      {{"vr19"}, "fr19"},
      {{"vr20"}, "fr20"},
      {{"vr21"}, "fr21"},
      {{"vr22"}, "fr22"},
      {{"vr23"}, "fr23"},
      {{"vr24"}, "fr24"},
      {{"vr25"}, "fr25"},
      {{"vr26"}, "fr26"},
      {{"vr27"}, "fr27"},
      {{"vr28"}, "fr28"},
      {{"vr29"}, "fr29"},
      {{"vr30"}, "fr30"},
      {{"vr31"}, "fr31"},

  };
  return llvm::ArrayRef(GCCRegAliases);
}

bool CSKYTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  case 'a':
  case 'b':
  case 'c':
  case 'y':
  case 'l':
  case 'h':
  case 'w':
  case 'v': // A floating-point and vector register.
  case 'z':
    Info.setAllowsRegister();
    return true;
  }
}

unsigned CSKYTargetInfo::getMinGlobalAlign(uint64_t Size,
                                           bool HasNonWeakDef) const {
  if (Size >= 32)
    return 32;
  return 0;
}
