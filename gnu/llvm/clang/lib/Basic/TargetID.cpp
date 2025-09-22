//===--- TargetID.cpp - Utilities for parsing target ID -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TargetID.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <map>
#include <optional>

namespace clang {

static llvm::SmallVector<llvm::StringRef, 4>
getAllPossibleAMDGPUTargetIDFeatures(const llvm::Triple &T,
                                     llvm::StringRef Proc) {
  // Entries in returned vector should be in alphabetical order.
  llvm::SmallVector<llvm::StringRef, 4> Ret;
  auto ProcKind = T.isAMDGCN() ? llvm::AMDGPU::parseArchAMDGCN(Proc)
                               : llvm::AMDGPU::parseArchR600(Proc);
  if (ProcKind == llvm::AMDGPU::GK_NONE)
    return Ret;
  auto Features = T.isAMDGCN() ? llvm::AMDGPU::getArchAttrAMDGCN(ProcKind)
                               : llvm::AMDGPU::getArchAttrR600(ProcKind);
  if (Features & llvm::AMDGPU::FEATURE_SRAMECC)
    Ret.push_back("sramecc");
  if (Features & llvm::AMDGPU::FEATURE_XNACK)
    Ret.push_back("xnack");
  return Ret;
}

llvm::SmallVector<llvm::StringRef, 4>
getAllPossibleTargetIDFeatures(const llvm::Triple &T,
                               llvm::StringRef Processor) {
  llvm::SmallVector<llvm::StringRef, 4> Ret;
  if (T.isAMDGPU())
    return getAllPossibleAMDGPUTargetIDFeatures(T, Processor);
  return Ret;
}

/// Returns canonical processor name or empty string if \p Processor is invalid.
static llvm::StringRef getCanonicalProcessorName(const llvm::Triple &T,
                                                 llvm::StringRef Processor) {
  if (T.isAMDGPU())
    return llvm::AMDGPU::getCanonicalArchName(T, Processor);
  return Processor;
}

llvm::StringRef getProcessorFromTargetID(const llvm::Triple &T,
                                         llvm::StringRef TargetID) {
  auto Split = TargetID.split(':');
  return getCanonicalProcessorName(T, Split.first);
}

// Parse a target ID with format checking only. Do not check whether processor
// name or features are valid for the processor.
//
// A target ID is a processor name followed by a list of target features
// delimited by colon. Each target feature is a string post-fixed by a plus
// or minus sign, e.g. gfx908:sramecc+:xnack-.
static std::optional<llvm::StringRef>
parseTargetIDWithFormatCheckingOnly(llvm::StringRef TargetID,
                                    llvm::StringMap<bool> *FeatureMap) {
  llvm::StringRef Processor;

  if (TargetID.empty())
    return llvm::StringRef();

  auto Split = TargetID.split(':');
  Processor = Split.first;
  if (Processor.empty())
    return std::nullopt;

  auto Features = Split.second;
  if (Features.empty())
    return Processor;

  llvm::StringMap<bool> LocalFeatureMap;
  if (!FeatureMap)
    FeatureMap = &LocalFeatureMap;

  while (!Features.empty()) {
    auto Splits = Features.split(':');
    auto Sign = Splits.first.back();
    auto Feature = Splits.first.drop_back();
    if (Sign != '+' && Sign != '-')
      return std::nullopt;
    bool IsOn = Sign == '+';
    auto Loc = FeatureMap->find(Feature);
    // Each feature can only show up at most once in target ID.
    if (Loc != FeatureMap->end())
      return std::nullopt;
    (*FeatureMap)[Feature] = IsOn;
    Features = Splits.second;
  }
  return Processor;
}

std::optional<llvm::StringRef>
parseTargetID(const llvm::Triple &T, llvm::StringRef TargetID,
              llvm::StringMap<bool> *FeatureMap) {
  auto OptionalProcessor =
      parseTargetIDWithFormatCheckingOnly(TargetID, FeatureMap);

  if (!OptionalProcessor)
    return std::nullopt;

  llvm::StringRef Processor = getCanonicalProcessorName(T, *OptionalProcessor);
  if (Processor.empty())
    return std::nullopt;

  llvm::SmallSet<llvm::StringRef, 4> AllFeatures;
  for (auto &&F : getAllPossibleTargetIDFeatures(T, Processor))
    AllFeatures.insert(F);

  for (auto &&F : *FeatureMap)
    if (!AllFeatures.count(F.first()))
      return std::nullopt;

  return Processor;
}

// A canonical target ID is a target ID containing a canonical processor name
// and features in alphabetical order.
std::string getCanonicalTargetID(llvm::StringRef Processor,
                                 const llvm::StringMap<bool> &Features) {
  std::string TargetID = Processor.str();
  std::map<const llvm::StringRef, bool> OrderedMap;
  for (const auto &F : Features)
    OrderedMap[F.first()] = F.second;
  for (const auto &F : OrderedMap)
    TargetID = TargetID + ':' + F.first.str() + (F.second ? "+" : "-");
  return TargetID;
}

// For a specific processor, a feature either shows up in all target IDs, or
// does not show up in any target IDs. Otherwise the target ID combination
// is invalid.
std::optional<std::pair<llvm::StringRef, llvm::StringRef>>
getConflictTargetIDCombination(const std::set<llvm::StringRef> &TargetIDs) {
  struct Info {
    llvm::StringRef TargetID;
    llvm::StringMap<bool> Features;
  };
  llvm::StringMap<Info> FeatureMap;
  for (auto &&ID : TargetIDs) {
    llvm::StringMap<bool> Features;
    llvm::StringRef Proc = *parseTargetIDWithFormatCheckingOnly(ID, &Features);
    auto Loc = FeatureMap.find(Proc);
    if (Loc == FeatureMap.end())
      FeatureMap[Proc] = Info{ID, Features};
    else {
      auto &ExistingFeatures = Loc->second.Features;
      if (llvm::any_of(Features, [&](auto &F) {
            return ExistingFeatures.count(F.first()) == 0;
          }))
        return std::make_pair(Loc->second.TargetID, ID);
    }
  }
  return std::nullopt;
}

bool isCompatibleTargetID(llvm::StringRef Provided, llvm::StringRef Requested) {
  llvm::StringMap<bool> ProvidedFeatures, RequestedFeatures;
  llvm::StringRef ProvidedProc =
      *parseTargetIDWithFormatCheckingOnly(Provided, &ProvidedFeatures);
  llvm::StringRef RequestedProc =
      *parseTargetIDWithFormatCheckingOnly(Requested, &RequestedFeatures);
  if (ProvidedProc != RequestedProc)
    return false;
  for (const auto &F : ProvidedFeatures) {
    auto Loc = RequestedFeatures.find(F.first());
    // The default (unspecified) value of a feature is 'All', which can match
    // either 'On' or 'Off'.
    if (Loc == RequestedFeatures.end())
      return false;
    // If a feature is specified, it must have exact match.
    if (Loc->second != F.second)
      return false;
  }
  return true;
}

} // namespace clang
