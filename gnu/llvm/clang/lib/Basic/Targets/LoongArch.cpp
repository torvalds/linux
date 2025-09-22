//===--- LoongArch.cpp - Implement LoongArch target feature support -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements LoongArch TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "LoongArch.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/LoongArchTargetParser.h"

using namespace clang;
using namespace clang::targets;

ArrayRef<const char *> LoongArchTargetInfo::getGCCRegNames() const {
  static const char *const GCCRegNames[] = {
      // General purpose registers.
      "$r0", "$r1", "$r2", "$r3", "$r4", "$r5", "$r6", "$r7", "$r8", "$r9",
      "$r10", "$r11", "$r12", "$r13", "$r14", "$r15", "$r16", "$r17", "$r18",
      "$r19", "$r20", "$r21", "$r22", "$r23", "$r24", "$r25", "$r26", "$r27",
      "$r28", "$r29", "$r30", "$r31",
      // Floating point registers.
      "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "$f8", "$f9",
      "$f10", "$f11", "$f12", "$f13", "$f14", "$f15", "$f16", "$f17", "$f18",
      "$f19", "$f20", "$f21", "$f22", "$f23", "$f24", "$f25", "$f26", "$f27",
      "$f28", "$f29", "$f30", "$f31",
      // Condition flag registers.
      "$fcc0", "$fcc1", "$fcc2", "$fcc3", "$fcc4", "$fcc5", "$fcc6", "$fcc7",
      // 128-bit vector registers.
      "$vr0", "$vr1", "$vr2", "$vr3", "$vr4", "$vr5", "$vr6", "$vr7", "$vr8",
      "$vr9", "$vr10", "$vr11", "$vr12", "$vr13", "$vr14", "$vr15", "$vr16",
      "$vr17", "$vr18", "$vr19", "$vr20", "$vr21", "$vr22", "$vr23", "$vr24",
      "$vr25", "$vr26", "$vr27", "$vr28", "$vr29", "$vr30", "$vr31",
      // 256-bit vector registers.
      "$xr0", "$xr1", "$xr2", "$xr3", "$xr4", "$xr5", "$xr6", "$xr7", "$xr8",
      "$xr9", "$xr10", "$xr11", "$xr12", "$xr13", "$xr14", "$xr15", "$xr16",
      "$xr17", "$xr18", "$xr19", "$xr20", "$xr21", "$xr22", "$xr23", "$xr24",
      "$xr25", "$xr26", "$xr27", "$xr28", "$xr29", "$xr30", "$xr31"};
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias>
LoongArchTargetInfo::getGCCRegAliases() const {
  static const TargetInfo::GCCRegAlias GCCRegAliases[] = {
      {{"zero", "$zero", "r0"}, "$r0"},
      {{"ra", "$ra", "r1"}, "$r1"},
      {{"tp", "$tp", "r2"}, "$r2"},
      {{"sp", "$sp", "r3"}, "$r3"},
      {{"a0", "$a0", "r4"}, "$r4"},
      {{"a1", "$a1", "r5"}, "$r5"},
      {{"a2", "$a2", "r6"}, "$r6"},
      {{"a3", "$a3", "r7"}, "$r7"},
      {{"a4", "$a4", "r8"}, "$r8"},
      {{"a5", "$a5", "r9"}, "$r9"},
      {{"a6", "$a6", "r10"}, "$r10"},
      {{"a7", "$a7", "r11"}, "$r11"},
      {{"t0", "$t0", "r12"}, "$r12"},
      {{"t1", "$t1", "r13"}, "$r13"},
      {{"t2", "$t2", "r14"}, "$r14"},
      {{"t3", "$t3", "r15"}, "$r15"},
      {{"t4", "$t4", "r16"}, "$r16"},
      {{"t5", "$t5", "r17"}, "$r17"},
      {{"t6", "$t6", "r18"}, "$r18"},
      {{"t7", "$t7", "r19"}, "$r19"},
      {{"t8", "$t8", "r20"}, "$r20"},
      {{"r21"}, "$r21"},
      {{"s9", "$s9", "r22", "fp", "$fp"}, "$r22"},
      {{"s0", "$s0", "r23"}, "$r23"},
      {{"s1", "$s1", "r24"}, "$r24"},
      {{"s2", "$s2", "r25"}, "$r25"},
      {{"s3", "$s3", "r26"}, "$r26"},
      {{"s4", "$s4", "r27"}, "$r27"},
      {{"s5", "$s5", "r28"}, "$r28"},
      {{"s6", "$s6", "r29"}, "$r29"},
      {{"s7", "$s7", "r30"}, "$r30"},
      {{"s8", "$s8", "r31"}, "$r31"},
      {{"$fa0"}, "$f0"},
      {{"$fa1"}, "$f1"},
      {{"$fa2"}, "$f2"},
      {{"$fa3"}, "$f3"},
      {{"$fa4"}, "$f4"},
      {{"$fa5"}, "$f5"},
      {{"$fa6"}, "$f6"},
      {{"$fa7"}, "$f7"},
      {{"$ft0"}, "$f8"},
      {{"$ft1"}, "$f9"},
      {{"$ft2"}, "$f10"},
      {{"$ft3"}, "$f11"},
      {{"$ft4"}, "$f12"},
      {{"$ft5"}, "$f13"},
      {{"$ft6"}, "$f14"},
      {{"$ft7"}, "$f15"},
      {{"$ft8"}, "$f16"},
      {{"$ft9"}, "$f17"},
      {{"$ft10"}, "$f18"},
      {{"$ft11"}, "$f19"},
      {{"$ft12"}, "$f20"},
      {{"$ft13"}, "$f21"},
      {{"$ft14"}, "$f22"},
      {{"$ft15"}, "$f23"},
      {{"$fs0"}, "$f24"},
      {{"$fs1"}, "$f25"},
      {{"$fs2"}, "$f26"},
      {{"$fs3"}, "$f27"},
      {{"$fs4"}, "$f28"},
      {{"$fs5"}, "$f29"},
      {{"$fs6"}, "$f30"},
      {{"$fs7"}, "$f31"},
  };
  return llvm::ArrayRef(GCCRegAliases);
}

bool LoongArchTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  // See the GCC definitions here:
  // https://gcc.gnu.org/onlinedocs/gccint/Machine-Constraints.html
  // Note that the 'm' constraint is handled in TargetInfo.
  switch (*Name) {
  default:
    return false;
  case 'f':
    // A floating-point register (if available).
    Info.setAllowsRegister();
    return true;
  case 'k':
    // A memory operand whose address is formed by a base register and
    // (optionally scaled) index register.
    Info.setAllowsMemory();
    return true;
  case 'l':
    // A signed 16-bit constant.
    Info.setRequiresImmediate(-32768, 32767);
    return true;
  case 'I':
    // A signed 12-bit constant (for arithmetic instructions).
    Info.setRequiresImmediate(-2048, 2047);
    return true;
  case 'J':
    // Integer zero.
    Info.setRequiresImmediate(0);
    return true;
  case 'K':
    // An unsigned 12-bit constant (for logic instructions).
    Info.setRequiresImmediate(0, 4095);
    return true;
  case 'Z':
    // ZB: An address that is held in a general-purpose register. The offset is
    //     zero.
    // ZC: A memory operand whose address is formed by a base register
    //     and offset that is suitable for use in instructions with the same
    //     addressing mode as ll.w and sc.w.
    if (Name[1] == 'C' || Name[1] == 'B') {
      Info.setAllowsMemory();
      ++Name; // Skip over 'Z'.
      return true;
    }
    return false;
  }
}

std::string
LoongArchTargetInfo::convertConstraint(const char *&Constraint) const {
  std::string R;
  switch (*Constraint) {
  case 'Z':
    // "ZC"/"ZB" are two-character constraints; add "^" hint for later
    // parsing.
    R = "^" + std::string(Constraint, 2);
    ++Constraint;
    break;
  default:
    R = TargetInfo::convertConstraint(Constraint);
    break;
  }
  return R;
}

void LoongArchTargetInfo::getTargetDefines(const LangOptions &Opts,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__loongarch__");
  unsigned GRLen = getRegisterWidth();
  Builder.defineMacro("__loongarch_grlen", Twine(GRLen));
  if (GRLen == 64)
    Builder.defineMacro("__loongarch64");

  if (HasFeatureD)
    Builder.defineMacro("__loongarch_frlen", "64");
  else if (HasFeatureF)
    Builder.defineMacro("__loongarch_frlen", "32");
  else
    Builder.defineMacro("__loongarch_frlen", "0");

  // Define __loongarch_arch.
  StringRef ArchName = getCPU();
  if (ArchName == "loongarch64") {
    if (HasFeatureLSX) {
      // TODO: As more features of the V1.1 ISA are supported, a unified "v1.1"
      // arch feature set will be used to include all sub-features belonging to
      // the V1.1 ISA version.
      if (HasFeatureFrecipe)
        Builder.defineMacro("__loongarch_arch",
                            Twine('"') + "la64v1.1" + Twine('"'));
      else
        Builder.defineMacro("__loongarch_arch",
                            Twine('"') + "la64v1.0" + Twine('"'));
    } else {
      Builder.defineMacro("__loongarch_arch",
                          Twine('"') + ArchName + Twine('"'));
    }
  } else {
    Builder.defineMacro("__loongarch_arch", Twine('"') + ArchName + Twine('"'));
  }

  // Define __loongarch_tune.
  StringRef TuneCPU = getTargetOpts().TuneCPU;
  if (TuneCPU.empty())
    TuneCPU = ArchName;
  Builder.defineMacro("__loongarch_tune", Twine('"') + TuneCPU + Twine('"'));

  if (HasFeatureLASX) {
    Builder.defineMacro("__loongarch_simd_width", "256");
    Builder.defineMacro("__loongarch_sx", Twine(1));
    Builder.defineMacro("__loongarch_asx", Twine(1));
  } else if (HasFeatureLSX) {
    Builder.defineMacro("__loongarch_simd_width", "128");
    Builder.defineMacro("__loongarch_sx", Twine(1));
  }
  if (HasFeatureFrecipe)
    Builder.defineMacro("__loongarch_frecipe", Twine(1));

  StringRef ABI = getABI();
  if (ABI == "lp64d" || ABI == "lp64f" || ABI == "lp64s")
    Builder.defineMacro("__loongarch_lp64");

  if (ABI == "lp64d" || ABI == "ilp32d") {
    Builder.defineMacro("__loongarch_hard_float");
    Builder.defineMacro("__loongarch_double_float");
  } else if (ABI == "lp64f" || ABI == "ilp32f") {
    Builder.defineMacro("__loongarch_hard_float");
    Builder.defineMacro("__loongarch_single_float");
  } else if (ABI == "lp64s" || ABI == "ilp32s") {
    Builder.defineMacro("__loongarch_soft_float");
  }

  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  if (GRLen == 64)
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}

static constexpr Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsLoongArch.def"
};

bool LoongArchTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  if (getTriple().getArch() == llvm::Triple::loongarch64)
    Features["64bit"] = true;
  if (getTriple().getArch() == llvm::Triple::loongarch32)
    Features["32bit"] = true;

  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

/// Return true if has this feature.
bool LoongArchTargetInfo::hasFeature(StringRef Feature) const {
  bool Is64Bit = getTriple().getArch() == llvm::Triple::loongarch64;
  // TODO: Handle more features.
  return llvm::StringSwitch<bool>(Feature)
      .Case("loongarch32", !Is64Bit)
      .Case("loongarch64", Is64Bit)
      .Case("32bit", !Is64Bit)
      .Case("64bit", Is64Bit)
      .Case("lsx", HasFeatureLSX)
      .Case("lasx", HasFeatureLASX)
      .Default(false);
}

ArrayRef<Builtin::Info> LoongArchTargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfo, clang::LoongArch::LastTSBuiltin -
                                         Builtin::FirstTSBuiltin);
}

bool LoongArchTargetInfo::handleTargetFeatures(
    std::vector<std::string> &Features, DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature == "+d" || Feature == "+f") {
      // "d" implies "f".
      HasFeatureF = true;
      if (Feature == "+d") {
        HasFeatureD = true;
      }
    } else if (Feature == "+lsx")
      HasFeatureLSX = true;
    else if (Feature == "+lasx")
      HasFeatureLASX = true;
    else if (Feature == "-ual")
      HasUnalignedAccess = false;
    else if (Feature == "+frecipe")
      HasFeatureFrecipe = true;
  }
  return true;
}

bool LoongArchTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::LoongArch::isValidCPUName(Name);
}

void LoongArchTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  llvm::LoongArch::fillValidCPUList(Values);
}
