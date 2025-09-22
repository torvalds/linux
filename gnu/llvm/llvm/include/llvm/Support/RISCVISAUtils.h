//===-- RISCVISAUtils.h - RISC-V ISA Utilities ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities shared by TableGen and RISCVISAInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RISCVISAUTILS_H
#define LLVM_SUPPORT_RISCVISAUTILS_H

#include "llvm/ADT/StringRef.h"
#include <map>
#include <string>

namespace llvm {

namespace RISCVISAUtils {
constexpr StringLiteral AllStdExts = "mafdqlcbkjtpvnh";

/// Represents the major and version number components of a RISC-V extension.
struct ExtensionVersion {
  unsigned Major;
  unsigned Minor;
};

bool compareExtension(const std::string &LHS, const std::string &RHS);

/// Helper class for OrderedExtensionMap.
struct ExtensionComparator {
  bool operator()(const std::string &LHS, const std::string &RHS) const {
    return compareExtension(LHS, RHS);
  }
};

/// OrderedExtensionMap is std::map, it's specialized to keep entries
/// in canonical order of extension.
typedef std::map<std::string, ExtensionVersion, ExtensionComparator>
    OrderedExtensionMap;

} // namespace RISCVISAUtils

} // namespace llvm

#endif
