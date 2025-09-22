//===- GuessTargetAndModeCompilationDatabase.cpp --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include <memory>

namespace clang {
namespace tooling {

namespace {
class LocationAdderDatabase : public CompilationDatabase {
public:
  LocationAdderDatabase(std::unique_ptr<CompilationDatabase> Base)
      : Base(std::move(Base)) {
    assert(this->Base != nullptr);
  }

  std::vector<std::string> getAllFiles() const override {
    return Base->getAllFiles();
  }

  std::vector<CompileCommand> getAllCompileCommands() const override {
    return addLocation(Base->getAllCompileCommands());
  }

  std::vector<CompileCommand>
  getCompileCommands(StringRef FilePath) const override {
    return addLocation(Base->getCompileCommands(FilePath));
  }

private:
  std::vector<CompileCommand>
  addLocation(std::vector<CompileCommand> Cmds) const {
    for (auto &Cmd : Cmds) {
      if (Cmd.CommandLine.empty())
        continue;
      std::string &Driver = Cmd.CommandLine.front();
      // If the driver name already is absolute, we don't need to do anything.
      if (llvm::sys::path::is_absolute(Driver))
        continue;
      // If the name is a relative path, like bin/clang, we assume it's
      // possible to resolve it and don't do anything about it either.
      if (llvm::any_of(Driver,
                       [](char C) { return llvm::sys::path::is_separator(C); }))
        continue;
      auto Absolute = llvm::sys::findProgramByName(Driver);
      // If we found it in path, update the entry in Cmd.CommandLine
      if (Absolute && llvm::sys::path::is_absolute(*Absolute))
        Driver = std::move(*Absolute);
    }
    return Cmds;
  }
  std::unique_ptr<CompilationDatabase> Base;
};
} // namespace

std::unique_ptr<CompilationDatabase>
inferToolLocation(std::unique_ptr<CompilationDatabase> Base) {
  return std::make_unique<LocationAdderDatabase>(std::move(Base));
}

} // namespace tooling
} // namespace clang
