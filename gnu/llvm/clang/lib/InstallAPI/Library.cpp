//===- Library.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/Library.h"

using namespace llvm;
namespace clang::installapi {

const Regex Rule("(.+)/(.+)\\.framework/");
StringRef Library::getFrameworkNameFromInstallName(StringRef InstallName) {
  assert(InstallName.contains(".framework") && "expected a framework");
  SmallVector<StringRef, 3> Match;
  Rule.match(InstallName, &Match);
  if (Match.empty())
    return "";
  return Match.back();
}

StringRef Library::getName() const {
  assert(!IsUnwrappedDylib && "expected a framework");
  StringRef Path = BaseDirectory;

  // Return the framework name extracted from path.
  while (!Path.empty()) {
    if (Path.ends_with(".framework"))
      return sys::path::filename(Path);
    Path = sys::path::parent_path(Path);
  }

  // Otherwise, return the name of the BaseDirectory.
  Path = BaseDirectory;
  return sys::path::filename(Path.rtrim("/"));
}

} // namespace clang::installapi
