//===---------------- ARMTargetParserCommon ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Code that is common to ARMTargetParser and AArch64TargetParser.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_ARMTARGETPARSERCOMMON_H
#define LLVM_TARGETPARSER_ARMTARGETPARSERCOMMON_H

#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace ARM {

enum class ISAKind { INVALID = 0, ARM, THUMB, AARCH64 };

enum class EndianKind { INVALID = 0, LITTLE, BIG };

/// Converts e.g. "armv8" -> "armv8-a"
StringRef getArchSynonym(StringRef Arch);

/// MArch is expected to be of the form (arm|thumb)?(eb)?(v.+)?(eb)?, but
/// (iwmmxt|xscale)(eb)? is also permitted. If the former, return
/// "v.+", if the latter, return unmodified string, minus 'eb'.
/// If invalid, return empty string.
StringRef getCanonicalArchName(StringRef Arch);

// ARM, Thumb, AArch64
ISAKind parseArchISA(StringRef Arch);

// Little/Big endian
EndianKind parseArchEndian(StringRef Arch);

struct ParsedBranchProtection {
  StringRef Scope;
  StringRef Key;
  bool BranchTargetEnforcement;
  bool BranchProtectionPAuthLR;
  bool GuardedControlStack;
};

bool parseBranchProtection(StringRef Spec, ParsedBranchProtection &PBP,
                           StringRef &Err, bool EnablePAuthLR = false);

} // namespace ARM
} // namespace llvm
#endif
