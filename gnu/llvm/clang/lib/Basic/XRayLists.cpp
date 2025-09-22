//===-- XRayLists.cpp - XRay automatic-attribution ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// User-provided filters for always/never XRay instrumenting certain functions.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/XRayLists.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/SpecialCaseList.h"

using namespace clang;

XRayFunctionFilter::XRayFunctionFilter(
    ArrayRef<std::string> AlwaysInstrumentPaths,
    ArrayRef<std::string> NeverInstrumentPaths,
    ArrayRef<std::string> AttrListPaths, SourceManager &SM)
    : AlwaysInstrument(llvm::SpecialCaseList::createOrDie(
          AlwaysInstrumentPaths, SM.getFileManager().getVirtualFileSystem())),
      NeverInstrument(llvm::SpecialCaseList::createOrDie(
          NeverInstrumentPaths, SM.getFileManager().getVirtualFileSystem())),
      AttrList(llvm::SpecialCaseList::createOrDie(
          AttrListPaths, SM.getFileManager().getVirtualFileSystem())),
      SM(SM) {}

XRayFunctionFilter::~XRayFunctionFilter() = default;

XRayFunctionFilter::ImbueAttribute
XRayFunctionFilter::shouldImbueFunction(StringRef FunctionName) const {
  // First apply the always instrument list, than if it isn't an "always" see
  // whether it's treated as a "never" instrument function.
  // TODO: Remove these as they're deprecated; use the AttrList exclusively.
  if (AlwaysInstrument->inSection("xray_always_instrument", "fun", FunctionName,
                                  "arg1") ||
      AttrList->inSection("always", "fun", FunctionName, "arg1"))
    return ImbueAttribute::ALWAYS_ARG1;
  if (AlwaysInstrument->inSection("xray_always_instrument", "fun",
                                  FunctionName) ||
      AttrList->inSection("always", "fun", FunctionName))
    return ImbueAttribute::ALWAYS;

  if (NeverInstrument->inSection("xray_never_instrument", "fun",
                                 FunctionName) ||
      AttrList->inSection("never", "fun", FunctionName))
    return ImbueAttribute::NEVER;

  return ImbueAttribute::NONE;
}

XRayFunctionFilter::ImbueAttribute
XRayFunctionFilter::shouldImbueFunctionsInFile(StringRef Filename,
                                               StringRef Category) const {
  if (AlwaysInstrument->inSection("xray_always_instrument", "src", Filename,
                                  Category) ||
      AttrList->inSection("always", "src", Filename, Category))
    return ImbueAttribute::ALWAYS;
  if (NeverInstrument->inSection("xray_never_instrument", "src", Filename,
                                 Category) ||
      AttrList->inSection("never", "src", Filename, Category))
    return ImbueAttribute::NEVER;
  return ImbueAttribute::NONE;
}

XRayFunctionFilter::ImbueAttribute
XRayFunctionFilter::shouldImbueLocation(SourceLocation Loc,
                                        StringRef Category) const {
  if (!Loc.isValid())
    return ImbueAttribute::NONE;
  return this->shouldImbueFunctionsInFile(SM.getFilename(SM.getFileLoc(Loc)),
                                          Category);
}
