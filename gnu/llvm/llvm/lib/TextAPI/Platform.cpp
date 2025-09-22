//===- llvm/TextAPI/Platform.cpp - Platform ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of Platform Helper functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/Platform.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace MachO {

PlatformType mapToPlatformType(PlatformType Platform, bool WantSim) {
  switch (Platform) {
  default:
    return Platform;
  case PLATFORM_IOS:
    return WantSim ? PLATFORM_IOSSIMULATOR : PLATFORM_IOS;
  case PLATFORM_TVOS:
    return WantSim ? PLATFORM_TVOSSIMULATOR : PLATFORM_TVOS;
  case PLATFORM_WATCHOS:
    return WantSim ? PLATFORM_WATCHOSSIMULATOR : PLATFORM_WATCHOS;
  }
}

PlatformType mapToPlatformType(const Triple &Target) {
  switch (Target.getOS()) {
  default:
    return PLATFORM_UNKNOWN;
  case Triple::MacOSX:
    return PLATFORM_MACOS;
  case Triple::IOS:
    if (Target.isSimulatorEnvironment())
      return PLATFORM_IOSSIMULATOR;
    if (Target.getEnvironment() == Triple::MacABI)
      return PLATFORM_MACCATALYST;
    return PLATFORM_IOS;
  case Triple::TvOS:
    return Target.isSimulatorEnvironment() ? PLATFORM_TVOSSIMULATOR
                                           : PLATFORM_TVOS;
  case Triple::WatchOS:
    return Target.isSimulatorEnvironment() ? PLATFORM_WATCHOSSIMULATOR
                                           : PLATFORM_WATCHOS;
  case Triple::BridgeOS:
    return PLATFORM_BRIDGEOS;
  case Triple::DriverKit:
    return PLATFORM_DRIVERKIT;
  case Triple::XROS:
    return Target.isSimulatorEnvironment() ? PLATFORM_XROS_SIMULATOR
                                           : PLATFORM_XROS;
  }
}

PlatformSet mapToPlatformSet(ArrayRef<Triple> Targets) {
  PlatformSet Result;
  for (const auto &Target : Targets)
    Result.insert(mapToPlatformType(Target));
  return Result;
}

StringRef getPlatformName(PlatformType Platform) {
  switch (Platform) {
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  case PLATFORM_##platform:                                                    \
    return #marketing;
#include "llvm/BinaryFormat/MachO.def"
  }
  llvm_unreachable("Unknown llvm::MachO::PlatformType enum");
}

PlatformType getPlatformFromName(StringRef Name) {
  return StringSwitch<PlatformType>(Name)
      .Case("osx", PLATFORM_MACOS)
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  .Case(#target, PLATFORM_##platform)
#include "llvm/BinaryFormat/MachO.def"
      .Default(PLATFORM_UNKNOWN);
}

std::string getOSAndEnvironmentName(PlatformType Platform,
                                    std::string Version) {
  switch (Platform) {
  case PLATFORM_UNKNOWN:
    return "darwin" + Version;
  case PLATFORM_MACOS:
    return "macos" + Version;
  case PLATFORM_IOS:
    return "ios" + Version;
  case PLATFORM_TVOS:
    return "tvos" + Version;
  case PLATFORM_WATCHOS:
    return "watchos" + Version;
  case PLATFORM_BRIDGEOS:
    return "bridgeos" + Version;
  case PLATFORM_MACCATALYST:
    return "ios" + Version + "-macabi";
  case PLATFORM_IOSSIMULATOR:
    return "ios" + Version + "-simulator";
  case PLATFORM_TVOSSIMULATOR:
    return "tvos" + Version + "-simulator";
  case PLATFORM_WATCHOSSIMULATOR:
    return "watchos" + Version + "-simulator";
  case PLATFORM_DRIVERKIT:
    return "driverkit" + Version;
  case PLATFORM_XROS:
    return "xros" + Version;
  case PLATFORM_XROS_SIMULATOR:
    return "xros" + Version + "-simulator";
  }
  llvm_unreachable("Unknown llvm::MachO::PlatformType enum");
}

VersionTuple mapToSupportedOSVersion(const Triple &Triple) {
  const VersionTuple MinSupportedOS = Triple.getMinimumSupportedOSVersion();
  if (MinSupportedOS > Triple.getOSVersion())
    return MinSupportedOS;
  return Triple.getOSVersion();
}

} // end namespace MachO.
} // end namespace llvm.
