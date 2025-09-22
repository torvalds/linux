//===- llvm/TargetParser/Unix/Host.inc --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the UNIX Host support.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include <cctype>
#include <string>
#include <sys/utsname.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

using namespace llvm;

static std::string getOSVersion() {
  struct utsname info;

  if (uname(&info))
    return "";

  return info.release;
}

static std::string updateTripleOSVersion(std::string TargetTripleString) {
  // On darwin, we want to update the version to match that of the target.
  std::string::size_type DarwinDashIdx = TargetTripleString.find("-darwin");
  if (DarwinDashIdx != std::string::npos) {
    TargetTripleString.resize(DarwinDashIdx + strlen("-darwin"));
    TargetTripleString += getOSVersion();
    return TargetTripleString;
  }
  std::string::size_type MacOSDashIdx = TargetTripleString.find("-macos");
  if (MacOSDashIdx != std::string::npos) {
    TargetTripleString.resize(MacOSDashIdx);
    // Reset the OS to darwin as the OS version from `uname` doesn't use the
    // macOS version scheme.
    TargetTripleString += "-darwin";
    TargetTripleString += getOSVersion();
  }
  // On AIX, the AIX version and release should be that of the current host
  // unless if the version has already been specified.
  if (Triple(LLVM_HOST_TRIPLE).getOS() == Triple::AIX) {
    Triple TT(TargetTripleString);
    if (TT.getOS() == Triple::AIX && !TT.getOSMajorVersion()) {
      struct utsname name;
      if (uname(&name) != -1) {
        std::string NewOSName = std::string(Triple::getOSTypeName(Triple::AIX));
        NewOSName += name.version;
        NewOSName += '.';
        NewOSName += name.release;
        NewOSName += ".0.0";
        TT.setOSName(NewOSName);
        return TT.str();
      }
    }
  }
  return TargetTripleString;
}

std::string sys::getDefaultTargetTriple() {
  std::string TargetTripleString =
      updateTripleOSVersion(LLVM_DEFAULT_TARGET_TRIPLE);

  // Override the default target with an environment variable named by
  // LLVM_TARGET_TRIPLE_ENV.
#if defined(LLVM_TARGET_TRIPLE_ENV)
  if (const char *EnvTriple = std::getenv(LLVM_TARGET_TRIPLE_ENV))
    TargetTripleString = EnvTriple;
#endif

  return TargetTripleString;
}
