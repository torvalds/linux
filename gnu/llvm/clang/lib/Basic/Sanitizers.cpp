//===- Sanitizers.cpp - C Language Family Language Options ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the classes from Sanitizers.h
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Sanitizers.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/MathExtras.h"

using namespace clang;

// Once LLVM switches to C++17, the constexpr variables can be inline and we
// won't need this.
#define SANITIZER(NAME, ID) constexpr SanitizerMask SanitizerKind::ID;
#define SANITIZER_GROUP(NAME, ID, ALIAS)                                       \
  constexpr SanitizerMask SanitizerKind::ID;                                   \
  constexpr SanitizerMask SanitizerKind::ID##Group;
#include "clang/Basic/Sanitizers.def"

SanitizerMask clang::parseSanitizerValue(StringRef Value, bool AllowGroups) {
  SanitizerMask ParsedKind = llvm::StringSwitch<SanitizerMask>(Value)
#define SANITIZER(NAME, ID) .Case(NAME, SanitizerKind::ID)
#define SANITIZER_GROUP(NAME, ID, ALIAS)                                       \
  .Case(NAME, AllowGroups ? SanitizerKind::ID##Group : SanitizerMask())
#include "clang/Basic/Sanitizers.def"
    .Default(SanitizerMask());
  return ParsedKind;
}

void clang::serializeSanitizerSet(SanitizerSet Set,
                                  SmallVectorImpl<StringRef> &Values) {
#define SANITIZER(NAME, ID)                                                    \
  if (Set.has(SanitizerKind::ID))                                              \
    Values.push_back(NAME);
#include "clang/Basic/Sanitizers.def"
}

SanitizerMask clang::expandSanitizerGroups(SanitizerMask Kinds) {
#define SANITIZER(NAME, ID)
#define SANITIZER_GROUP(NAME, ID, ALIAS)                                       \
  if (Kinds & SanitizerKind::ID##Group)                                        \
    Kinds |= SanitizerKind::ID;
#include "clang/Basic/Sanitizers.def"
  return Kinds;
}

llvm::hash_code SanitizerMask::hash_value() const {
  return llvm::hash_combine_range(&maskLoToHigh[0], &maskLoToHigh[kNumElem]);
}

namespace clang {
unsigned SanitizerMask::countPopulation() const {
  unsigned total = 0;
  for (const auto &Val : maskLoToHigh)
    total += llvm::popcount(Val);
  return total;
}

llvm::hash_code hash_value(const clang::SanitizerMask &Arg) {
  return Arg.hash_value();
}

StringRef AsanDtorKindToString(llvm::AsanDtorKind kind) {
  switch (kind) {
  case llvm::AsanDtorKind::None:
    return "none";
  case llvm::AsanDtorKind::Global:
    return "global";
  case llvm::AsanDtorKind::Invalid:
    return "invalid";
  }
  return "invalid";
}

llvm::AsanDtorKind AsanDtorKindFromString(StringRef kindStr) {
  return llvm::StringSwitch<llvm::AsanDtorKind>(kindStr)
      .Case("none", llvm::AsanDtorKind::None)
      .Case("global", llvm::AsanDtorKind::Global)
      .Default(llvm::AsanDtorKind::Invalid);
}

StringRef AsanDetectStackUseAfterReturnModeToString(
    llvm::AsanDetectStackUseAfterReturnMode mode) {
  switch (mode) {
  case llvm::AsanDetectStackUseAfterReturnMode::Always:
    return "always";
  case llvm::AsanDetectStackUseAfterReturnMode::Runtime:
    return "runtime";
  case llvm::AsanDetectStackUseAfterReturnMode::Never:
    return "never";
  case llvm::AsanDetectStackUseAfterReturnMode::Invalid:
    return "invalid";
  }
  return "invalid";
}

llvm::AsanDetectStackUseAfterReturnMode
AsanDetectStackUseAfterReturnModeFromString(StringRef modeStr) {
  return llvm::StringSwitch<llvm::AsanDetectStackUseAfterReturnMode>(modeStr)
      .Case("always", llvm::AsanDetectStackUseAfterReturnMode::Always)
      .Case("runtime", llvm::AsanDetectStackUseAfterReturnMode::Runtime)
      .Case("never", llvm::AsanDetectStackUseAfterReturnMode::Never)
      .Default(llvm::AsanDetectStackUseAfterReturnMode::Invalid);
}

} // namespace clang
