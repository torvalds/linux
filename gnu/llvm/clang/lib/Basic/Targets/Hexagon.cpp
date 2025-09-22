//===--- Hexagon.cpp - Implement Hexagon target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Hexagon TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "Targets.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

void HexagonTargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__qdsp6__", "1");
  Builder.defineMacro("__hexagon__", "1");

  // The macro __HVXDBL__ is deprecated.
  bool DefineHvxDbl = false;

  if (CPU == "hexagonv5") {
    Builder.defineMacro("__HEXAGON_V5__");
    Builder.defineMacro("__HEXAGON_ARCH__", "5");
    if (Opts.HexagonQdsp6Compat) {
      Builder.defineMacro("__QDSP6_V5__");
      Builder.defineMacro("__QDSP6_ARCH__", "5");
    }
  } else if (CPU == "hexagonv55") {
    Builder.defineMacro("__HEXAGON_V55__");
    Builder.defineMacro("__HEXAGON_ARCH__", "55");
    Builder.defineMacro("__QDSP6_V55__");
    Builder.defineMacro("__QDSP6_ARCH__", "55");
  } else if (CPU == "hexagonv60") {
    DefineHvxDbl = true;
    Builder.defineMacro("__HEXAGON_V60__");
    Builder.defineMacro("__HEXAGON_ARCH__", "60");
    Builder.defineMacro("__QDSP6_V60__");
    Builder.defineMacro("__QDSP6_ARCH__", "60");
  } else if (CPU == "hexagonv62") {
    DefineHvxDbl = true;
    Builder.defineMacro("__HEXAGON_V62__");
    Builder.defineMacro("__HEXAGON_ARCH__", "62");
  } else if (CPU == "hexagonv65") {
    DefineHvxDbl = true;
    Builder.defineMacro("__HEXAGON_V65__");
    Builder.defineMacro("__HEXAGON_ARCH__", "65");
  } else if (CPU == "hexagonv66") {
    DefineHvxDbl = true;
    Builder.defineMacro("__HEXAGON_V66__");
    Builder.defineMacro("__HEXAGON_ARCH__", "66");
  } else if (CPU == "hexagonv67") {
    Builder.defineMacro("__HEXAGON_V67__");
    Builder.defineMacro("__HEXAGON_ARCH__", "67");
  } else if (CPU == "hexagonv67t") {
    Builder.defineMacro("__HEXAGON_V67T__");
    Builder.defineMacro("__HEXAGON_ARCH__", "67");
  } else if (CPU == "hexagonv68") {
    Builder.defineMacro("__HEXAGON_V68__");
    Builder.defineMacro("__HEXAGON_ARCH__", "68");
  } else if (CPU == "hexagonv69") {
    Builder.defineMacro("__HEXAGON_V69__");
    Builder.defineMacro("__HEXAGON_ARCH__", "69");
  } else if (CPU == "hexagonv71") {
    Builder.defineMacro("__HEXAGON_V71__");
    Builder.defineMacro("__HEXAGON_ARCH__", "71");
  } else if (CPU == "hexagonv71t") {
    Builder.defineMacro("__HEXAGON_V71T__");
    Builder.defineMacro("__HEXAGON_ARCH__", "71");
  } else if (CPU == "hexagonv73") {
    Builder.defineMacro("__HEXAGON_V73__");
    Builder.defineMacro("__HEXAGON_ARCH__", "73");
  }

  if (hasFeature("hvx-length64b")) {
    Builder.defineMacro("__HVX__");
    Builder.defineMacro("__HVX_ARCH__", HVXVersion);
    Builder.defineMacro("__HVX_LENGTH__", "64");
  }

  if (hasFeature("hvx-length128b")) {
    Builder.defineMacro("__HVX__");
    Builder.defineMacro("__HVX_ARCH__", HVXVersion);
    Builder.defineMacro("__HVX_LENGTH__", "128");
    if (DefineHvxDbl)
      Builder.defineMacro("__HVXDBL__");
  }

  if (hasFeature("audio")) {
    Builder.defineMacro("__HEXAGON_AUDIO__");
  }

  std::string NumPhySlots = isTinyCore() ? "3" : "4";
  Builder.defineMacro("__HEXAGON_PHYSICAL_SLOTS__", NumPhySlots);

  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}

bool HexagonTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  if (isTinyCore())
    Features["audio"] = true;

  StringRef CPUFeature = CPU;
  CPUFeature.consume_front("hexagon");
  CPUFeature.consume_back("t");
  if (!CPUFeature.empty())
    Features[CPUFeature] = true;

  Features["long-calls"] = false;

  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

bool HexagonTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                             DiagnosticsEngine &Diags) {
  for (auto &F : Features) {
    if (F == "+hvx-length64b")
      HasHVX = HasHVX64B = true;
    else if (F == "+hvx-length128b")
      HasHVX = HasHVX128B = true;
    else if (F.find("+hvxv") != std::string::npos) {
      HasHVX = true;
      HVXVersion = F.substr(std::string("+hvxv").length());
    } else if (F == "-hvx")
      HasHVX = HasHVX64B = HasHVX128B = false;
    else if (F == "+long-calls")
      UseLongCalls = true;
    else if (F == "-long-calls")
      UseLongCalls = false;
    else if (F == "+audio")
      HasAudio = true;
  }
  if (CPU.compare("hexagonv68") >= 0) {
    HasLegalHalfType = true;
    HasFloat16 = true;
  }
  return true;
}

const char *const HexagonTargetInfo::GCCRegNames[] = {
    // Scalar registers:
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11",
    "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21",
    "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    "r1:0", "r3:2", "r5:4", "r7:6", "r9:8", "r11:10", "r13:12", "r15:14",
    "r17:16", "r19:18", "r21:20", "r23:22", "r25:24", "r27:26", "r29:28",
    "r31:30",
    // Predicate registers:
    "p0", "p1", "p2", "p3",
    // Control registers:
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "c10", "c11",
    "c12", "c13", "c14", "c15", "c16", "c17", "c18", "c19", "c20", "c21",
    "c22", "c23", "c24", "c25", "c26", "c27", "c28", "c29", "c30", "c31",
    "c1:0", "c3:2", "c5:4", "c7:6", "c9:8", "c11:10", "c13:12", "c15:14",
    "c17:16", "c19:18", "c21:20", "c23:22", "c25:24", "c27:26", "c29:28",
    "c31:30",
    // Control register aliases:
    "sa0", "lc0", "sa1", "lc1", "p3:0", "m0",  "m1",  "usr", "pc", "ugp",
    "gp", "cs0", "cs1", "upcyclelo", "upcyclehi", "framelimit", "framekey",
    "pktcountlo", "pktcounthi", "utimerlo", "utimerhi",
    "upcycle", "pktcount", "utimer",
    // HVX vector registers:
    "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
    "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21",
    "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
    "v1:0", "v3:2", "v5:4", "v7:6", "v9:8", "v11:10", "v13:12", "v15:14",
    "v17:16", "v19:18", "v21:20", "v23:22", "v25:24", "v27:26", "v29:28",
    "v31:30",
    "v3:0", "v7:4", "v11:8", "v15:12", "v19:16", "v23:20", "v27:24", "v31:28",
    // HVX vector predicates:
    "q0", "q1", "q2", "q3",
};

ArrayRef<const char *> HexagonTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias HexagonTargetInfo::GCCRegAliases[] = {
    {{"sp"}, "r29"},
    {{"fp"}, "r30"},
    {{"lr"}, "r31"},
};

ArrayRef<TargetInfo::GCCRegAlias> HexagonTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

static constexpr Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER)                                    \
  {#ID, TYPE, ATTRS, nullptr, HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsHexagon.def"
};

bool HexagonTargetInfo::hasFeature(StringRef Feature) const {
  std::string VS = "hvxv" + HVXVersion;
  if (Feature == VS)
    return true;

  return llvm::StringSwitch<bool>(Feature)
      .Case("hexagon", true)
      .Case("hvx", HasHVX)
      .Case("hvx-length64b", HasHVX64B)
      .Case("hvx-length128b", HasHVX128B)
      .Case("long-calls", UseLongCalls)
      .Case("audio", HasAudio)
      .Default(false);
}

struct CPUSuffix {
  llvm::StringLiteral Name;
  llvm::StringLiteral Suffix;
};

static constexpr CPUSuffix Suffixes[] = {
    {{"hexagonv5"},  {"5"}},  {{"hexagonv55"},  {"55"}},
    {{"hexagonv60"}, {"60"}}, {{"hexagonv62"},  {"62"}},
    {{"hexagonv65"}, {"65"}}, {{"hexagonv66"},  {"66"}},
    {{"hexagonv67"}, {"67"}}, {{"hexagonv67t"}, {"67t"}},
    {{"hexagonv68"}, {"68"}}, {{"hexagonv69"},  {"69"}},
    {{"hexagonv71"}, {"71"}}, {{"hexagonv71t"},  {"71t"}},
    {{"hexagonv73"}, {"73"}},
};

std::optional<unsigned> HexagonTargetInfo::getHexagonCPURev(StringRef Name) {
  StringRef Arch = Name;
  Arch.consume_front("hexagonv");
  Arch.consume_back("t");

  unsigned Val;
  if (!Arch.getAsInteger(0, Val))
    return Val;

  return std::nullopt;
}

const char *HexagonTargetInfo::getHexagonCPUSuffix(StringRef Name) {
  const CPUSuffix *Item = llvm::find_if(
      Suffixes, [Name](const CPUSuffix &S) { return S.Name == Name; });
  if (Item == std::end(Suffixes))
    return nullptr;
  return Item->Suffix.data();
}

void HexagonTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  for (const CPUSuffix &Suffix : Suffixes)
    Values.push_back(Suffix.Name);
}

ArrayRef<Builtin::Info> HexagonTargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfo, clang::Hexagon::LastTSBuiltin -
                                         Builtin::FirstTSBuiltin);
}
