//===- Target.cpp -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/Target.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace MachO {

Expected<Target> Target::create(StringRef TargetValue) {
  auto Result = TargetValue.split('-');
  auto ArchitectureStr = Result.first;
  auto Architecture = getArchitectureFromName(ArchitectureStr);
  auto PlatformStr = Result.second;
  PlatformType Platform;
  Platform = StringSwitch<PlatformType>(PlatformStr)
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  .Case(#tapi_target, PLATFORM_##platform)
#include "llvm/BinaryFormat/MachO.def"
                 .Default(PLATFORM_UNKNOWN);

  if (Platform == PLATFORM_UNKNOWN) {
    if (PlatformStr.starts_with("<") && PlatformStr.ends_with(">")) {
      PlatformStr = PlatformStr.drop_front().drop_back();
      unsigned long long RawValue;
      if (!PlatformStr.getAsInteger(10, RawValue))
        Platform = (PlatformType)RawValue;
    }
  }

  return Target{Architecture, Platform};
}

Target::operator std::string() const {
  auto Version = MinDeployment.empty() ? "" : MinDeployment.getAsString();

  return (getArchitectureName(Arch) + " (" + getPlatformName(Platform) +
          Version + ")")
      .str();
}

raw_ostream &operator<<(raw_ostream &OS, const Target &Target) {
  OS << std::string(Target);
  return OS;
}

PlatformVersionSet mapToPlatformVersionSet(ArrayRef<Target> Targets) {
  PlatformVersionSet Result;
  for (const auto &Target : Targets)
    Result.insert({Target.Platform, Target.MinDeployment});
  return Result;
}

PlatformSet mapToPlatformSet(ArrayRef<Target> Targets) {
  PlatformSet Result;
  for (const auto &Target : Targets)
    Result.insert(Target.Platform);
  return Result;
}

ArchitectureSet mapToArchitectureSet(ArrayRef<Target> Targets) {
  ArchitectureSet Result;
  for (const auto &Target : Targets)
    Result.set(Target.Arch);
  return Result;
}

std::string getTargetTripleName(const Target &Targ) {
  auto Version =
      Targ.MinDeployment.empty() ? "" : Targ.MinDeployment.getAsString();

  return (getArchitectureName(Targ.Arch) + "-apple-" +
          getOSAndEnvironmentName(Targ.Platform, Version))
      .str();
}

} // end namespace MachO.
} // end namespace llvm.
