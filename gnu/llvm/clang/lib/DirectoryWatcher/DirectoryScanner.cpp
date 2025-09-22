//===- DirectoryScanner.cpp - Utility functions for DirectoryWatcher ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectoryScanner.h"

#include "llvm/Support/Path.h"
#include <optional>

namespace clang {

using namespace llvm;

std::optional<sys::fs::file_status> getFileStatus(StringRef Path) {
  sys::fs::file_status Status;
  std::error_code EC = status(Path, Status);
  if (EC)
    return std::nullopt;
  return Status;
}

std::vector<std::string> scanDirectory(StringRef Path) {
  using namespace llvm::sys;
  std::vector<std::string> Result;

  std::error_code EC;
  for (auto It = fs::directory_iterator(Path, EC),
            End = fs::directory_iterator();
       !EC && It != End; It.increment(EC)) {
    auto status = getFileStatus(It->path());
    if (!status)
      continue;
    Result.emplace_back(sys::path::filename(It->path()));
  }

  return Result;
}

std::vector<DirectoryWatcher::Event>
getAsFileEvents(const std::vector<std::string> &Scan) {
  std::vector<DirectoryWatcher::Event> Events;
  Events.reserve(Scan.size());

  for (const auto &File : Scan) {
    Events.emplace_back(DirectoryWatcher::Event{
        DirectoryWatcher::Event::EventKind::Modified, File});
  }
  return Events;
}

} // namespace clang
