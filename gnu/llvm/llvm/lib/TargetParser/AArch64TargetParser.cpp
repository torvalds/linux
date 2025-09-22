//===-- AArch64TargetParser - Parser for AArch64 features -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AArch64 hardware features
// such as FPU/CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include "llvm/TargetParser/Triple.h"
#include <cctype>
#include <vector>

#define DEBUG_TYPE "target-parser"

using namespace llvm;

#define EMIT_FMV_INFO
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

static unsigned checkArchVersion(llvm::StringRef Arch) {
  if (Arch.size() >= 2 && Arch[0] == 'v' && std::isdigit(Arch[1]))
    return (Arch[1] - 48);
  return 0;
}

const AArch64::ArchInfo *AArch64::getArchForCpu(StringRef CPU) {
  // Note: this now takes cpu aliases into account
  std::optional<CpuInfo> Cpu = parseCpu(CPU);
  if (!Cpu)
    return nullptr;
  return &Cpu->Arch;
}

std::optional<AArch64::ArchInfo> AArch64::ArchInfo::findBySubArch(StringRef SubArch) {
  for (const auto *A : AArch64::ArchInfos)
    if (A->getSubArch() == SubArch)
      return *A;
  return {};
}

uint64_t AArch64::getCpuSupportsMask(ArrayRef<StringRef> FeatureStrs) {
  uint64_t FeaturesMask = 0;
  for (const StringRef &FeatureStr : FeatureStrs) {
    if (auto Ext = parseFMVExtension(FeatureStr))
      FeaturesMask |= (1ULL << Ext->Bit);
  }
  return FeaturesMask;
}

bool AArch64::getExtensionFeatures(
    const AArch64::ExtensionBitset &InputExts,
    std::vector<StringRef> &Features) {
  for (const auto &E : Extensions)
    /* INVALID and NONE have no feature name. */
    if (InputExts.test(E.ID) && !E.PosTargetFeature.empty())
      Features.push_back(E.PosTargetFeature);

  return true;
}

StringRef AArch64::resolveCPUAlias(StringRef Name) {
  for (const auto &A : CpuAliases)
    if (A.AltName == Name)
      return A.Name;
  return Name;
}

StringRef AArch64::getArchExtFeature(StringRef ArchExt) {
  bool IsNegated = ArchExt.starts_with("no");
  StringRef ArchExtBase = IsNegated ? ArchExt.drop_front(2) : ArchExt;

  if (auto AE = parseArchExtension(ArchExtBase)) {
    assert(!(AE.has_value() && AE->NegTargetFeature.empty()));
    return IsNegated ? AE->NegTargetFeature : AE->PosTargetFeature;
  }

  return StringRef();
}

void AArch64::fillValidCPUArchList(SmallVectorImpl<StringRef> &Values) {
  for (const auto &C : CpuInfos)
    Values.push_back(C.Name);

  for (const auto &Alias : CpuAliases)
    // The apple-latest alias is backend only, do not expose it to clang's -mcpu.
    if (Alias.AltName != "apple-latest")
      Values.push_back(Alias.AltName);

  llvm::sort(Values);
}

bool AArch64::isX18ReservedByDefault(const Triple &TT) {
  return TT.isAndroid() || TT.isOSDarwin() || TT.isOSFuchsia() ||
         TT.isOSWindows() || TT.isOHOSFamily();
}

// Allows partial match, ex. "v8a" matches "armv8a".
const AArch64::ArchInfo *AArch64::parseArch(StringRef Arch) {
  Arch = llvm::ARM::getCanonicalArchName(Arch);
  if (checkArchVersion(Arch) < 8)
    return {};

  StringRef Syn = llvm::ARM::getArchSynonym(Arch);
  for (const auto *A : ArchInfos) {
    if (A->Name.ends_with(Syn))
      return A;
  }
  return {};
}

std::optional<AArch64::ExtensionInfo>
AArch64::parseArchExtension(StringRef ArchExt) {
  if (ArchExt.empty())
    return {};
  for (const auto &A : Extensions) {
    if (ArchExt == A.UserVisibleName || ArchExt == A.Alias)
      return A;
  }
  return {};
}

std::optional<AArch64::FMVInfo> AArch64::parseFMVExtension(StringRef FMVExt) {
  // FIXME introduce general alias functionality, or remove this exception.
  if (FMVExt == "rdma")
    FMVExt = "rdm";

  for (const auto &I : getFMVInfo()) {
    if (FMVExt == I.Name)
      return I;
  }
  return {};
}

std::optional<AArch64::ExtensionInfo>
AArch64::targetFeatureToExtension(StringRef TargetFeature) {
  for (const auto &E : Extensions)
    if (TargetFeature == E.PosTargetFeature)
      return E;
  return {};
}

std::optional<AArch64::CpuInfo> AArch64::parseCpu(StringRef Name) {
  // Resolve aliases first.
  Name = resolveCPUAlias(Name);

  // Then find the CPU name.
  for (const auto &C : CpuInfos)
    if (Name == C.Name)
      return C;

  return {};
}

void AArch64::PrintSupportedExtensions() {
  outs() << "All available -march extensions for AArch64\n\n"
         << "    " << left_justify("Name", 20)
         << left_justify("Architecture Feature(s)", 55)
         << "Description\n";
  for (const auto &Ext : Extensions) {
    // Extensions without a feature cannot be used with -march.
    if (!Ext.UserVisibleName.empty() && !Ext.PosTargetFeature.empty()) {
      outs() << "    "
             << format(Ext.Description.empty() ? "%-20s%s\n" : "%-20s%-55s%s\n",
                       Ext.UserVisibleName.str().c_str(),
                       Ext.ArchFeatureName.str().c_str(),
                       Ext.Description.str().c_str());
    }
  }
}

void
AArch64::printEnabledExtensions(const std::set<StringRef> &EnabledFeatureNames) {
  outs() << "Extensions enabled for the given AArch64 target\n\n"
         << "    " << left_justify("Architecture Feature(s)", 55)
         << "Description\n";
  std::vector<ExtensionInfo> EnabledExtensionsInfo;
  for (const auto &FeatureName : EnabledFeatureNames) {
    std::string PosFeatureName = '+' + FeatureName.str();
    if (auto ExtInfo = targetFeatureToExtension(PosFeatureName))
      EnabledExtensionsInfo.push_back(*ExtInfo);
  }

  std::sort(EnabledExtensionsInfo.begin(), EnabledExtensionsInfo.end(),
            [](const ExtensionInfo &Lhs, const ExtensionInfo &Rhs) {
              return Lhs.ArchFeatureName < Rhs.ArchFeatureName;
            });

  for (const auto &Ext : EnabledExtensionsInfo) {
    outs() << "    "
           << format("%-55s%s\n",
                     Ext.ArchFeatureName.str().c_str(),
                     Ext.Description.str().c_str());
  }
}

const llvm::AArch64::ExtensionInfo &
lookupExtensionByID(llvm::AArch64::ArchExtKind ExtID) {
  for (const auto &E : llvm::AArch64::Extensions)
    if (E.ID == ExtID)
      return E;
  llvm_unreachable("Invalid extension ID");
}

void AArch64::ExtensionSet::enable(ArchExtKind E) {
  if (Enabled.test(E))
    return;

  LLVM_DEBUG(llvm::dbgs() << "Enable " << lookupExtensionByID(E).UserVisibleName << "\n");

  Touched.set(E);
  Enabled.set(E);

  // Recursively enable all features that this one depends on. This handles all
  // of the simple cases, where the behaviour doesn't depend on the base
  // architecture version.
  for (auto Dep : ExtensionDependencies)
    if (E == Dep.Later)
      enable(Dep.Earlier);

  // Special cases for dependencies which vary depending on the base
  // architecture version.
  if (BaseArch) {
    // +fp16 implies +fp16fml for v8.4A+, but not v9.0-A+
    if (E == AEK_FP16 && BaseArch->is_superset(ARMV8_4A) &&
        !BaseArch->is_superset(ARMV9A))
      enable(AEK_FP16FML);

    // For v8.4A+ and v9.0A+, +crypto also enables +sha3 and +sm4.
    if (E == AEK_CRYPTO && BaseArch->is_superset(ARMV8_4A)) {
      enable(AEK_SHA3);
      enable(AEK_SM4);
    }
  }
}

void AArch64::ExtensionSet::disable(ArchExtKind E) {
  // -crypto always disables aes, sha2, sha3 and sm4, even for architectures
  // where the latter two would not be enabled by +crypto.
  if (E == AEK_CRYPTO) {
    disable(AEK_AES);
    disable(AEK_SHA2);
    disable(AEK_SHA3);
    disable(AEK_SM4);
  }

  if (!Enabled.test(E))
    return;

  LLVM_DEBUG(llvm::dbgs() << "Disable " << lookupExtensionByID(E).UserVisibleName << "\n");

  Touched.set(E);
  Enabled.reset(E);

  // Recursively disable all features that depends on this one.
  for (auto Dep : ExtensionDependencies)
    if (E == Dep.Earlier)
      disable(Dep.Later);
}

void AArch64::ExtensionSet::addCPUDefaults(const CpuInfo &CPU) {
  LLVM_DEBUG(llvm::dbgs() << "addCPUDefaults(" << CPU.Name << ")\n");
  BaseArch = &CPU.Arch;

  AArch64::ExtensionBitset CPUExtensions = CPU.getImpliedExtensions();
  for (const auto &E : Extensions)
    if (CPUExtensions.test(E.ID))
      enable(E.ID);
}

void AArch64::ExtensionSet::addArchDefaults(const ArchInfo &Arch) {
  LLVM_DEBUG(llvm::dbgs() << "addArchDefaults(" << Arch.Name << ")\n");
  BaseArch = &Arch;

  for (const auto &E : Extensions)
    if (Arch.DefaultExts.test(E.ID))
      enable(E.ID);
}

bool AArch64::ExtensionSet::parseModifier(StringRef Modifier,
                                          const bool AllowNoDashForm) {
  LLVM_DEBUG(llvm::dbgs() << "parseModifier(" << Modifier << ")\n");

  size_t NChars = 0;
  // The "no-feat" form is allowed in the target attribute but nowhere else.
  if (AllowNoDashForm && Modifier.starts_with("no-"))
    NChars = 3;
  else if (Modifier.starts_with("no"))
    NChars = 2;
  bool IsNegated = NChars != 0;
  StringRef ArchExt = Modifier.drop_front(NChars);

  if (auto AE = parseArchExtension(ArchExt)) {
    if (AE->PosTargetFeature.empty() || AE->NegTargetFeature.empty())
      return false;
    if (IsNegated)
      disable(AE->ID);
    else
      enable(AE->ID);
    return true;
  }
  return false;
}

void AArch64::ExtensionSet::reconstructFromParsedFeatures(
    const std::vector<std::string> &Features,
    std::vector<std::string> &NonExtensions) {
  assert(Touched.none() && "Bitset already initialized");
  for (auto &F : Features) {
    bool IsNegated = F[0] == '-';
    if (auto AE = targetFeatureToExtension(F)) {
      Touched.set(AE->ID);
      if (IsNegated)
        Enabled.reset(AE->ID);
      else
        Enabled.set(AE->ID);
      continue;
    }
    NonExtensions.push_back(F);
  }
}

void AArch64::ExtensionSet::dump() const {
  std::vector<StringRef> Features;
  toLLVMFeatureList(Features);
  for (StringRef F : Features)
    llvm::outs() << F << " ";
  llvm::outs() << "\n";
}

const AArch64::ExtensionInfo &
AArch64::getExtensionByID(AArch64::ArchExtKind ExtID) {
  return lookupExtensionByID(ExtID);
}
