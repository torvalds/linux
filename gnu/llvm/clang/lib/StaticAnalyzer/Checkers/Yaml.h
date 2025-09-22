//== Yaml.h ---------------------------------------------------- -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines convenience functions for handling YAML configuration files
// for checkers/packages.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKER_YAML_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKER_YAML_H

#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLTraits.h"
#include <optional>

namespace clang {
namespace ento {

/// Read the given file from the filesystem and parse it as a yaml file. The
/// template parameter must have a yaml MappingTraits.
/// Emit diagnostic error in case of any failure.
template <class T, class Checker>
std::optional<T> getConfiguration(CheckerManager &Mgr, Checker *Chk,
                                  StringRef Option, StringRef ConfigFile) {
  if (ConfigFile.trim().empty())
    return std::nullopt;

  llvm::vfs::FileSystem *FS = llvm::vfs::getRealFileSystem().get();
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
      FS->getBufferForFile(ConfigFile.str());

  if (Buffer.getError()) {
    Mgr.reportInvalidCheckerOptionValue(Chk, Option,
                                        "a valid filename instead of '" +
                                            std::string(ConfigFile) + "'");
    return std::nullopt;
  }

  llvm::yaml::Input Input(Buffer.get()->getBuffer());
  T Config;
  Input >> Config;

  if (std::error_code ec = Input.error()) {
    Mgr.reportInvalidCheckerOptionValue(Chk, Option,
                                        "a valid yaml file: " + ec.message());
    return std::nullopt;
  }

  return Config;
}

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_LIB_STATICANALYZER_CHECKER_YAML_H
