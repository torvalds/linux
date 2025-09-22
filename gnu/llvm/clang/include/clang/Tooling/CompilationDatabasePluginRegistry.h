//===- CompilationDatabasePluginRegistry.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_COMPILATIONDATABASEPLUGINREGISTRY_H
#define LLVM_CLANG_TOOLING_COMPILATIONDATABASEPLUGINREGISTRY_H

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/Registry.h"

namespace clang {
namespace tooling {

/// Interface for compilation database plugins.
///
/// A compilation database plugin allows the user to register custom compilation
/// databases that are picked up as compilation database if the corresponding
/// library is linked in. To register a plugin, declare a static variable like:
///
/// \code
/// static CompilationDatabasePluginRegistry::Add<MyDatabasePlugin>
/// X("my-compilation-database", "Reads my own compilation database");
/// \endcode
class CompilationDatabasePlugin {
public:
  virtual ~CompilationDatabasePlugin();

  /// Loads a compilation database from a build directory.
  ///
  /// \see CompilationDatabase::loadFromDirectory().
  virtual std::unique_ptr<CompilationDatabase>
  loadFromDirectory(StringRef Directory, std::string &ErrorMessage) = 0;
};

using CompilationDatabasePluginRegistry =
    llvm::Registry<CompilationDatabasePlugin>;

} // namespace tooling
} // namespace clang

#endif // LLVM_CLANG_TOOLING_COMPILATIONDATABASEPLUGINREGISTRY_H
