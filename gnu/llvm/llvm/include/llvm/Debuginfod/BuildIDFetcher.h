//===- llvm/DebugInfod/BuildIDFetcher.h - Build ID fetcher ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares a Build ID fetcher implementation for obtaining debug
/// info from debuginfod.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFOD_DIFETCHER_H
#define LLVM_DEBUGINFOD_DIFETCHER_H

#include "llvm/Object/BuildID.h"
#include <optional>

namespace llvm {

class DebuginfodFetcher : public object::BuildIDFetcher {
public:
  DebuginfodFetcher(std::vector<std::string> DebugFileDirectories)
      : BuildIDFetcher(std::move(DebugFileDirectories)) {}
  virtual ~DebuginfodFetcher() = default;

  /// Fetches the given Build ID using debuginfod and returns a local path to
  /// the resulting file.
  std::optional<std::string> fetch(object::BuildIDRef BuildID) const override;
};

} // namespace llvm

#endif // LLVM_DEBUGINFOD_DIFETCHER_H
