//===- llvm/TextAPI/Platform.h - Platform -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Platforms supported by Tapi and helpers.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TEXTAPI_PLATFORM_H
#define LLVM_TEXTAPI_PLATFORM_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/VersionTuple.h"

namespace llvm {
namespace MachO {

using PlatformSet = SmallSet<PlatformType, 3>;
using PlatformVersionSet = SmallSet<std::pair<PlatformType, VersionTuple>, 3>;

PlatformType mapToPlatformType(PlatformType Platform, bool WantSim);
PlatformType mapToPlatformType(const Triple &Target);
PlatformSet mapToPlatformSet(ArrayRef<Triple> Targets);
StringRef getPlatformName(PlatformType Platform);
PlatformType getPlatformFromName(StringRef Name);
std::string getOSAndEnvironmentName(PlatformType Platform,
                                    std::string Version = "");
VersionTuple mapToSupportedOSVersion(const Triple &Triple);

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_PLATFORM_H
