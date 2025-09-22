//===--- NoSanitizeList.cpp - Ignored list for sanitizers ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// User-provided ignore-list used to disable/alter instrumentation done in
// sanitizers.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/NoSanitizeList.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SanitizerSpecialCaseList.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;

NoSanitizeList::NoSanitizeList(const std::vector<std::string> &NoSanitizePaths,
                               SourceManager &SM)
    : SSCL(SanitizerSpecialCaseList::createOrDie(
          NoSanitizePaths, SM.getFileManager().getVirtualFileSystem())),
      SM(SM) {}

NoSanitizeList::~NoSanitizeList() = default;

bool NoSanitizeList::containsGlobal(SanitizerMask Mask, StringRef GlobalName,
                                    StringRef Category) const {
  return SSCL->inSection(Mask, "global", GlobalName, Category);
}

bool NoSanitizeList::containsType(SanitizerMask Mask, StringRef MangledTypeName,
                                  StringRef Category) const {
  return SSCL->inSection(Mask, "type", MangledTypeName, Category);
}

bool NoSanitizeList::containsFunction(SanitizerMask Mask,
                                      StringRef FunctionName) const {
  return SSCL->inSection(Mask, "fun", FunctionName);
}

bool NoSanitizeList::containsFile(SanitizerMask Mask, StringRef FileName,
                                  StringRef Category) const {
  return SSCL->inSection(Mask, "src", FileName, Category);
}

bool NoSanitizeList::containsMainFile(SanitizerMask Mask, StringRef FileName,
                                      StringRef Category) const {
  return SSCL->inSection(Mask, "mainfile", FileName, Category);
}

bool NoSanitizeList::containsLocation(SanitizerMask Mask, SourceLocation Loc,
                                      StringRef Category) const {
  return Loc.isValid() &&
         containsFile(Mask, SM.getFilename(SM.getFileLoc(Loc)), Category);
}
