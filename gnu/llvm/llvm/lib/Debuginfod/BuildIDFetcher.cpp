//===- llvm/DebugInfod/BuildIDFetcher.cpp - Build ID fetcher --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a DIFetcher implementation for obtaining debug info
/// from debuginfod.
///
//===----------------------------------------------------------------------===//

#include "llvm/Debuginfod/BuildIDFetcher.h"

#include "llvm/Debuginfod/Debuginfod.h"

using namespace llvm;

std::optional<std::string>
DebuginfodFetcher::fetch(ArrayRef<uint8_t> BuildID) const {
  if (std::optional<std::string> Path = BuildIDFetcher::fetch(BuildID))
    return std::move(*Path);

  Expected<std::string> PathOrErr = getCachedOrDownloadDebuginfo(BuildID);
  if (PathOrErr)
    return *PathOrErr;
  consumeError(PathOrErr.takeError());
  return std::nullopt;
}
