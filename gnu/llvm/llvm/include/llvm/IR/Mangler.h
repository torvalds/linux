//===-- llvm/IR/Mangler.h - Self-contained name mangler ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unified name mangler for various backends.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MANGLER_H
#define LLVM_IR_MANGLER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

class DataLayout;
class GlobalValue;
template <typename T> class SmallVectorImpl;
class Triple;
class Twine;
class raw_ostream;

class Mangler {
  /// We need to give global values the same name every time they are mangled.
  /// This keeps track of the number we give to anonymous ones.
  mutable DenseMap<const GlobalValue*, unsigned> AnonGlobalIDs;

public:
  /// Print the appropriate prefix and the specified global variable's name.
  /// If the global variable doesn't have a name, this fills in a unique name
  /// for the global.
  void getNameWithPrefix(raw_ostream &OS, const GlobalValue *GV,
                         bool CannotUsePrivateLabel) const;
  void getNameWithPrefix(SmallVectorImpl<char> &OutName, const GlobalValue *GV,
                         bool CannotUsePrivateLabel) const;

  /// Print the appropriate prefix and the specified name as the global variable
  /// name. GVName must not be empty.
  static void getNameWithPrefix(raw_ostream &OS, const Twine &GVName,
                                const DataLayout &DL);
  static void getNameWithPrefix(SmallVectorImpl<char> &OutName,
                                const Twine &GVName, const DataLayout &DL);
};

void emitLinkerFlagsForGlobalCOFF(raw_ostream &OS, const GlobalValue *GV,
                                  const Triple &TT, Mangler &Mangler);

void emitLinkerFlagsForUsedCOFF(raw_ostream &OS, const GlobalValue *GV,
                                const Triple &T, Mangler &M);

std::optional<std::string> getArm64ECMangledFunctionName(StringRef Name);
std::optional<std::string> getArm64ECDemangledFunctionName(StringRef Name);

/// Check if an ARM64EC function name is mangled.
bool inline isArm64ECMangledFunctionName(StringRef Name) {
  return Name[0] == '#' ||
         (Name[0] == '?' && Name.find("@$$h") != StringRef::npos);
}

} // End llvm namespace

#endif
