//===-- RISCVISAInfo.h - RISC-V ISA Information -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RISCVISAINFO_H
#define LLVM_SUPPORT_RISCVISAINFO_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/RISCVISAUtils.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace llvm {

class RISCVISAInfo {
public:
  RISCVISAInfo(const RISCVISAInfo &) = delete;
  RISCVISAInfo &operator=(const RISCVISAInfo &) = delete;

  /// Parse RISC-V ISA info from arch string.
  /// If IgnoreUnknown is set, any unrecognised extension names or
  /// extensions with unrecognised versions will be silently dropped, except
  /// for the special case of the base 'i' and 'e' extensions, where the
  /// default version will be used (as ignoring the base is not possible).
  static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  parseArchString(StringRef Arch, bool EnableExperimentalExtension,
                  bool ExperimentalExtensionVersionCheck = true);

  /// Parse RISC-V ISA info from an arch string that is already in normalized
  /// form (as defined in the psABI). Unlike parseArchString, this function
  /// will not error for unrecognized extension names or extension versions.
  static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  parseNormalizedArchString(StringRef Arch);

  /// Parse RISC-V ISA info from feature vector.
  static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  parseFeatures(unsigned XLen, const std::vector<std::string> &Features);

  static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  createFromExtMap(unsigned XLen,
                   const RISCVISAUtils::OrderedExtensionMap &Exts);

  /// Convert RISC-V ISA info to a feature vector.
  std::vector<std::string> toFeatures(bool AddAllExtensions = false,
                                      bool IgnoreUnknown = true) const;

  const RISCVISAUtils::OrderedExtensionMap &getExtensions() const {
    return Exts;
  }

  unsigned getXLen() const { return XLen; }
  unsigned getFLen() const { return FLen; }
  unsigned getMinVLen() const { return MinVLen; }
  unsigned getMaxVLen() const { return 65536; }
  unsigned getMaxELen() const { return MaxELen; }
  unsigned getMaxELenFp() const { return MaxELenFp; }

  bool hasExtension(StringRef Ext) const;
  std::string toString() const;
  StringRef computeDefaultABI() const;

  static bool isSupportedExtensionFeature(StringRef Ext);
  static bool isSupportedExtension(StringRef Ext);
  static bool isSupportedExtensionWithVersion(StringRef Ext);
  static bool isSupportedExtension(StringRef Ext, unsigned MajorVersion,
                                   unsigned MinorVersion);
  static std::string getTargetFeatureForExtension(StringRef Ext);

  static void printSupportedExtensions(StringMap<StringRef> &DescMap);
  static void printEnabledExtensions(bool IsRV64,
                                     std::set<StringRef> &EnabledFeatureNames,
                                     StringMap<StringRef> &DescMap);

private:
  RISCVISAInfo(unsigned XLen) : XLen(XLen) {}

  unsigned XLen;
  unsigned FLen = 0;
  unsigned MinVLen = 0;
  unsigned MaxELen = 0, MaxELenFp = 0;

  RISCVISAUtils::OrderedExtensionMap Exts;

  Error checkDependency();

  void updateImplication();
  void updateCombination();

  /// Update FLen, MinVLen, MaxELen, and MaxELenFp.
  void updateImpliedLengths();

  static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  postProcessAndChecking(std::unique_ptr<RISCVISAInfo> &&ISAInfo);
};

} // namespace llvm

#endif
