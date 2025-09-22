//===- Driver.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_DRIVER_H
#define LLD_ELF_DRIVER_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include <optional>

namespace lld::elf {
// Parses command line options.
class ELFOptTable : public llvm::opt::GenericOptTable {
public:
  ELFOptTable();
  llvm::opt::InputArgList parse(ArrayRef<const char *> argv);
};

// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

void printHelp();
std::string createResponseFile(const llvm::opt::InputArgList &args);

std::optional<std::string> findFromSearchPaths(StringRef path);
std::optional<std::string> searchScript(StringRef path);
std::optional<std::string> searchLibraryBaseName(StringRef path);
std::optional<std::string> searchLibrary(StringRef path);

} // namespace lld::elf

#endif
