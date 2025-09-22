//===- SubtargetFeatureInfo.h - Helpers for subtarget features --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTIL_TABLEGEN_SUBTARGETFEATUREINFO_H
#define LLVM_UTIL_TABLEGEN_SUBTARGETFEATUREINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TableGen/Record.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
struct SubtargetFeatureInfo;
using SubtargetFeatureInfoMap =
    std::map<Record *, SubtargetFeatureInfo, LessRecordByID>;

/// Helper class for storing information on a subtarget feature which
/// participates in instruction matching.
struct SubtargetFeatureInfo {
  /// The predicate record for this feature.
  Record *TheDef;

  /// An unique index assigned to represent this feature.
  uint64_t Index;

  SubtargetFeatureInfo(Record *D, uint64_t Idx) : TheDef(D), Index(Idx) {}

  /// The name of the enumerated constant identifying this feature.
  std::string getEnumName() const {
    return "Feature_" + TheDef->getName().str();
  }

  /// The name of the enumerated constant identifying the bitnumber for
  /// this feature.
  std::string getEnumBitName() const {
    return "Feature_" + TheDef->getName().str() + "Bit";
  }

  bool mustRecomputePerFunction() const {
    return TheDef->getValueAsBit("RecomputePerFunction");
  }

  void dump() const;
  static std::vector<std::pair<Record *, SubtargetFeatureInfo>>
  getAll(const RecordKeeper &Records);

  /// Emit the subtarget feature flag definitions.
  ///
  /// This version emits the bit index for the feature and can therefore support
  /// more than 64 feature bits.
  static void emitSubtargetFeatureBitEnumeration(
      const SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS,
      const std::map<std::string, unsigned> *HwModes = nullptr);

  static void emitNameTable(SubtargetFeatureInfoMap &SubtargetFeatures,
                            raw_ostream &OS);

  /// Emit the function to compute the list of available features given a
  /// subtarget.
  ///
  /// This version is used for subtarget features defined using Predicate<>
  /// and supports more than 64 feature bits.
  ///
  /// \param TargetName The name of the target as used in class prefixes (e.g.
  ///                   <TargetName>Subtarget)
  /// \param ClassName  The name of the class that will contain the generated
  ///                   functions (including the target prefix.)
  /// \param FuncName   The name of the function to emit.
  /// \param SubtargetFeatures A map of TableGen records to the
  ///                          SubtargetFeatureInfo equivalent.
  /// \param ExtraParams Additional arguments to the generated function.
  /// \param HwModes Map of HwMode conditions to check.
  static void emitComputeAvailableFeatures(
      StringRef TargetName, StringRef ClassName, StringRef FuncName,
      const SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS,
      StringRef ExtraParams = "",
      const std::map<std::string, unsigned> *HwModes = nullptr);

  /// Emit the function to compute the list of available features given a
  /// subtarget.
  ///
  /// This version is used for subtarget features defined using
  /// AssemblerPredicate<> and supports up to 64 feature bits.
  ///
  /// \param TargetName The name of the target as used in class prefixes (e.g.
  ///                   <TargetName>Subtarget)
  /// \param ClassName  The name of the class (without the <Target> prefix)
  ///                   that will contain the generated functions.
  /// \param FuncName   The name of the function to emit.
  /// \param SubtargetFeatures A map of TableGen records to the
  ///                          SubtargetFeatureInfo equivalent.
  static void emitComputeAssemblerAvailableFeatures(
      StringRef TargetName, StringRef ClassName, StringRef FuncName,
      SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS);
};
} // end namespace llvm

#endif // LLVM_UTIL_TABLEGEN_SUBTARGETFEATUREINFO_H
