//===--- NoSanitizeList.h - List of ignored entities for sanitizers --*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// User-provided list of ignored entities used to disable/alter
// instrumentation done in sanitizers.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_NOSANITIZELIST_H
#define LLVM_CLANG_BASIC_NOSANITIZELIST_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <vector>

namespace clang {

class SanitizerMask;
class SourceManager;
class SanitizerSpecialCaseList;

class NoSanitizeList {
  std::unique_ptr<SanitizerSpecialCaseList> SSCL;
  SourceManager &SM;

public:
  NoSanitizeList(const std::vector<std::string> &NoSanitizeListPaths,
                 SourceManager &SM);
  ~NoSanitizeList();
  bool containsGlobal(SanitizerMask Mask, StringRef GlobalName,
                      StringRef Category = StringRef()) const;
  bool containsType(SanitizerMask Mask, StringRef MangledTypeName,
                    StringRef Category = StringRef()) const;
  bool containsFunction(SanitizerMask Mask, StringRef FunctionName) const;
  bool containsFile(SanitizerMask Mask, StringRef FileName,
                    StringRef Category = StringRef()) const;
  bool containsMainFile(SanitizerMask Mask, StringRef FileName,
                        StringRef Category = StringRef()) const;
  bool containsLocation(SanitizerMask Mask, SourceLocation Loc,
                        StringRef Category = StringRef()) const;
};

} // end namespace clang

#endif
