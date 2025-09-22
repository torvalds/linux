//===--- DarwinSDKInfo.cpp - SDK Information parser for darwin - ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DarwinSDKInfo.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace clang;

std::optional<VersionTuple> DarwinSDKInfo::RelatedTargetVersionMapping::map(
    const VersionTuple &Key, const VersionTuple &MinimumValue,
    std::optional<VersionTuple> MaximumValue) const {
  if (Key < MinimumKeyVersion)
    return MinimumValue;
  if (Key > MaximumKeyVersion)
    return MaximumValue;
  auto KV = Mapping.find(Key.normalize());
  if (KV != Mapping.end())
    return KV->getSecond();
  // If no exact entry found, try just the major key version. Only do so when
  // a minor version number is present, to avoid recursing indefinitely into
  // the major-only check.
  if (Key.getMinor())
    return map(VersionTuple(Key.getMajor()), MinimumValue, MaximumValue);
  // If this a major only key, return std::nullopt for a missing entry.
  return std::nullopt;
}

std::optional<DarwinSDKInfo::RelatedTargetVersionMapping>
DarwinSDKInfo::RelatedTargetVersionMapping::parseJSON(
    const llvm::json::Object &Obj, VersionTuple MaximumDeploymentTarget) {
  VersionTuple Min = VersionTuple(std::numeric_limits<unsigned>::max());
  VersionTuple Max = VersionTuple(0);
  VersionTuple MinValue = Min;
  llvm::DenseMap<VersionTuple, VersionTuple> Mapping;
  for (const auto &KV : Obj) {
    if (auto Val = KV.getSecond().getAsString()) {
      llvm::VersionTuple KeyVersion;
      llvm::VersionTuple ValueVersion;
      if (KeyVersion.tryParse(KV.getFirst()) || ValueVersion.tryParse(*Val))
        return std::nullopt;
      Mapping[KeyVersion.normalize()] = ValueVersion;
      if (KeyVersion < Min)
        Min = KeyVersion;
      if (KeyVersion > Max)
        Max = KeyVersion;
      if (ValueVersion < MinValue)
        MinValue = ValueVersion;
    }
  }
  if (Mapping.empty())
    return std::nullopt;
  return RelatedTargetVersionMapping(
      Min, Max, MinValue, MaximumDeploymentTarget, std::move(Mapping));
}

static std::optional<VersionTuple> getVersionKey(const llvm::json::Object &Obj,
                                                 StringRef Key) {
  auto Value = Obj.getString(Key);
  if (!Value)
    return std::nullopt;
  VersionTuple Version;
  if (Version.tryParse(*Value))
    return std::nullopt;
  return Version;
}

std::optional<DarwinSDKInfo>
DarwinSDKInfo::parseDarwinSDKSettingsJSON(const llvm::json::Object *Obj) {
  auto Version = getVersionKey(*Obj, "Version");
  if (!Version)
    return std::nullopt;
  auto MaximumDeploymentVersion =
      getVersionKey(*Obj, "MaximumDeploymentTarget");
  if (!MaximumDeploymentVersion)
    return std::nullopt;
  llvm::DenseMap<OSEnvPair::StorageType,
                 std::optional<RelatedTargetVersionMapping>>
      VersionMappings;
  if (const auto *VM = Obj->getObject("VersionMap")) {
    // FIXME: Generalize this out beyond iOS-deriving targets.
    // Look for ios_<targetos> version mapping for targets that derive from ios.
    for (const auto &KV : *VM) {
      auto Pair = StringRef(KV.getFirst()).split("_");
      if (Pair.first.compare_insensitive("ios") == 0) {
        llvm::Triple TT(llvm::Twine("--") + Pair.second.lower());
        if (TT.getOS() != llvm::Triple::UnknownOS) {
          auto Mapping = RelatedTargetVersionMapping::parseJSON(
              *KV.getSecond().getAsObject(), *MaximumDeploymentVersion);
          if (Mapping)
            VersionMappings[OSEnvPair(llvm::Triple::IOS,
                                      llvm::Triple::UnknownEnvironment,
                                      TT.getOS(),
                                      llvm::Triple::UnknownEnvironment)
                                .Value] = std::move(Mapping);
        }
      }
    }

    if (const auto *Mapping = VM->getObject("macOS_iOSMac")) {
      auto VersionMap = RelatedTargetVersionMapping::parseJSON(
          *Mapping, *MaximumDeploymentVersion);
      if (!VersionMap)
        return std::nullopt;
      VersionMappings[OSEnvPair::macOStoMacCatalystPair().Value] =
          std::move(VersionMap);
    }
    if (const auto *Mapping = VM->getObject("iOSMac_macOS")) {
      auto VersionMap = RelatedTargetVersionMapping::parseJSON(
          *Mapping, *MaximumDeploymentVersion);
      if (!VersionMap)
        return std::nullopt;
      VersionMappings[OSEnvPair::macCatalystToMacOSPair().Value] =
          std::move(VersionMap);
    }
  }

  return DarwinSDKInfo(std::move(*Version),
                       std::move(*MaximumDeploymentVersion),
                       std::move(VersionMappings));
}

Expected<std::optional<DarwinSDKInfo>>
clang::parseDarwinSDKInfo(llvm::vfs::FileSystem &VFS, StringRef SDKRootPath) {
  llvm::SmallString<256> Filepath = SDKRootPath;
  llvm::sys::path::append(Filepath, "SDKSettings.json");
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile(Filepath);
  if (!File) {
    // If the file couldn't be read, assume it just doesn't exist.
    return std::nullopt;
  }
  Expected<llvm::json::Value> Result =
      llvm::json::parse(File.get()->getBuffer());
  if (!Result)
    return Result.takeError();

  if (const auto *Obj = Result->getAsObject()) {
    if (auto SDKInfo = DarwinSDKInfo::parseDarwinSDKSettingsJSON(Obj))
      return std::move(SDKInfo);
  }
  return llvm::make_error<llvm::StringError>("invalid SDKSettings.json",
                                             llvm::inconvertibleErrorCode());
}
