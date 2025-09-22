//===--- SPIRVCommandLine.h ---- Command Line Options -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains classes and functions needed for processing, parsing, and
// using CLI options for the SPIR-V backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_COMMANDLINE_H
#define LLVM_LIB_TARGET_SPIRV_COMMANDLINE_H

#include "MCTargetDesc/SPIRVBaseInfo.h"
#include "llvm/Support/CommandLine.h"
#include <set>

namespace llvm {

/// Command line parser for toggling SPIR-V extensions.
struct SPIRVExtensionsParser
    : public cl::parser<std::set<SPIRV::Extension::Extension>> {
public:
  SPIRVExtensionsParser(cl::Option &O)
      : cl::parser<std::set<SPIRV::Extension::Extension>>(O) {}

  /// Parses SPIR-V extension name from CLI arguments.
  ///
  /// \return Returns true on error.
  bool parse(cl::Option &O, StringRef ArgName, StringRef ArgValue,
             std::set<SPIRV::Extension::Extension> &Vals);
};

} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_COMMANDLINE_H
