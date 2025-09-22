//===--- ProfileList.h - ProfileList filter ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// User-provided filters include/exclude profile instrumentation in certain
// functions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_PROFILELIST_H
#define LLVM_CLANG_BASIC_PROFILELIST_H

#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <optional>

namespace clang {

class ProfileSpecialCaseList;

class ProfileList {
public:
  /// Represents if an how something should be excluded from profiling.
  enum ExclusionType {
    /// Profiling is allowed.
    Allow,
    /// Profiling is skipped using the \p skipprofile attribute.
    Skip,
    /// Profiling is forbidden using the \p noprofile attribute.
    Forbid,
  };

private:
  std::unique_ptr<ProfileSpecialCaseList> SCL;
  const bool Empty;
  SourceManager &SM;
  std::optional<ExclusionType> inSection(StringRef Section, StringRef Prefix,
                                         StringRef Query) const;

public:
  ProfileList(ArrayRef<std::string> Paths, SourceManager &SM);
  ~ProfileList();

  bool isEmpty() const { return Empty; }
  ExclusionType getDefault(CodeGenOptions::ProfileInstrKind Kind) const;

  std::optional<ExclusionType>
  isFunctionExcluded(StringRef FunctionName,
                     CodeGenOptions::ProfileInstrKind Kind) const;
  std::optional<ExclusionType>
  isLocationExcluded(SourceLocation Loc,
                     CodeGenOptions::ProfileInstrKind Kind) const;
  std::optional<ExclusionType>
  isFileExcluded(StringRef FileName,
                 CodeGenOptions::ProfileInstrKind Kind) const;
};

} // namespace clang

#endif
