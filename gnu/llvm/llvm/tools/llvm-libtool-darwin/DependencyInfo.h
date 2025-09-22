//===-- DependencyInfo.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

class DependencyInfo {
public:
  explicit DependencyInfo(std::string DependencyInfoPath)
      : DependencyInfoPath(DependencyInfoPath) {}

  virtual ~DependencyInfo(){};

  virtual void addMissingInput(llvm::StringRef Path) {
    NotFounds.insert(Path.str());
  }

  // Writes the dependencies to specified path. The content is first sorted by
  // OpCode and then by the filename (in alphabetical order).
  virtual void write(llvm::Twine Version,
                     const std::vector<std::string> &Inputs,
                     std::string Output) {
    std::error_code EC;
    llvm::raw_fd_ostream OS(DependencyInfoPath, EC, llvm::sys::fs::OF_None);
    if (EC) {
      llvm::WithColor::defaultErrorHandler(llvm::createStringError(
          EC,
          "failed to write to " + DependencyInfoPath + ": " + EC.message()));
      return;
    }

    auto AddDep = [&OS](DependencyInfoOpcode Opcode,
                        const llvm::StringRef &Path) {
      OS << static_cast<uint8_t>(Opcode);
      OS << Path;
      OS << '\0';
    };

    AddDep(DependencyInfoOpcode::Tool, Version.str());

    // Sort the input by its names.
    std::vector<llvm::StringRef> InputNames;
    InputNames.reserve(Inputs.size());
    for (const auto &F : Inputs)
      InputNames.push_back(F);
    llvm::sort(InputNames);

    for (const auto &In : InputNames)
      AddDep(DependencyInfoOpcode::InputFound, In);

    for (const std::string &F : NotFounds)
      AddDep(DependencyInfoOpcode::InputMissing, F);

    AddDep(DependencyInfoOpcode::Output, Output);
  }

private:
  enum DependencyInfoOpcode : uint8_t {
    Tool = 0x00,
    InputFound = 0x10,
    InputMissing = 0x11,
    Output = 0x40,
  };

  const std::string DependencyInfoPath;
  std::set<std::string> NotFounds;
};

// Subclass to avoid any overhead when not using this feature
class DummyDependencyInfo : public DependencyInfo {
public:
  DummyDependencyInfo() : DependencyInfo("") {}
  void addMissingInput(llvm::StringRef Path) override {}
  void write(llvm::Twine Version, const std::vector<std::string> &Inputs,
             std::string Output) override {}
};
