//===- ExpandResponseFileCompilationDataBase.cpp --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace tooling {
namespace {

class ExpandResponseFilesDatabase : public CompilationDatabase {
public:
  ExpandResponseFilesDatabase(
      std::unique_ptr<CompilationDatabase> Base,
      llvm::cl::TokenizerCallback Tokenizer,
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
      : Base(std::move(Base)), Tokenizer(Tokenizer), FS(std::move(FS)) {
    assert(this->Base != nullptr);
    assert(this->Tokenizer != nullptr);
    assert(this->FS != nullptr);
  }

  std::vector<std::string> getAllFiles() const override {
    return Base->getAllFiles();
  }

  std::vector<CompileCommand>
  getCompileCommands(StringRef FilePath) const override {
    return expand(Base->getCompileCommands(FilePath));
  }

  std::vector<CompileCommand> getAllCompileCommands() const override {
    return expand(Base->getAllCompileCommands());
  }

private:
  std::vector<CompileCommand> expand(std::vector<CompileCommand> Cmds) const {
    for (auto &Cmd : Cmds)
      tooling::addExpandedResponseFiles(Cmd.CommandLine, Cmd.Directory,
                                        Tokenizer, *FS);
    return Cmds;
  }

private:
  std::unique_ptr<CompilationDatabase> Base;
  llvm::cl::TokenizerCallback Tokenizer;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
};

} // namespace

std::unique_ptr<CompilationDatabase>
expandResponseFiles(std::unique_ptr<CompilationDatabase> Base,
                    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS) {
  auto Tokenizer = llvm::Triple(llvm::sys::getProcessTriple()).isOSWindows()
                       ? llvm::cl::TokenizeWindowsCommandLine
                       : llvm::cl::TokenizeGNUCommandLine;
  return std::make_unique<ExpandResponseFilesDatabase>(
      std::move(Base), Tokenizer, std::move(FS));
}

} // namespace tooling
} // namespace clang
