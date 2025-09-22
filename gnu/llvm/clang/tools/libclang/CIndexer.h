//===- CIndexer.h - Clang-C Source Indexing Library -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines CIndexer, a subclass of Indexer that provides extra
// functionality needed by the CIndex library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CINDEXER_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CINDEXER_H

#include "clang-c/Index.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "llvm/ADT/STLExtras.h"
#include <utility>

namespace llvm {
  class CrashRecoveryContext;
}

namespace clang {
class ASTUnit;
class MacroInfo;
class MacroDefinitionRecord;
class SourceLocation;
class Token;
class IdentifierInfo;

class CIndexer {
  bool OnlyLocalDecls;
  bool DisplayDiagnostics;
  bool StorePreamblesInMemory = false;
  unsigned Options; // CXGlobalOptFlags.

  std::string ResourcesPath;
  std::shared_ptr<PCHContainerOperations> PCHContainerOps;

  std::string ToolchainPath;

  std::string PreambleStoragePath;
  std::string InvocationEmissionPath;

public:
  CIndexer(std::shared_ptr<PCHContainerOperations> PCHContainerOps =
               std::make_shared<PCHContainerOperations>())
      : OnlyLocalDecls(false), DisplayDiagnostics(false),
        Options(CXGlobalOpt_None), PCHContainerOps(std::move(PCHContainerOps)) {
  }

  /// Whether we only want to see "local" declarations (that did not
  /// come from a previous precompiled header). If false, we want to see all
  /// declarations.
  bool getOnlyLocalDecls() const { return OnlyLocalDecls; }
  void setOnlyLocalDecls(bool Local = true) { OnlyLocalDecls = Local; }
  
  bool getDisplayDiagnostics() const { return DisplayDiagnostics; }
  void setDisplayDiagnostics(bool Display = true) {
    DisplayDiagnostics = Display;
  }

  std::shared_ptr<PCHContainerOperations> getPCHContainerOperations() const {
    return PCHContainerOps;
  }

  unsigned getCXGlobalOptFlags() const { return Options; }
  void setCXGlobalOptFlags(unsigned options) { Options = options; }

  bool isOptEnabled(CXGlobalOptFlags opt) const {
    return Options & opt;
  }

  /// Get the path of the clang resource files.
  const std::string &getClangResourcesPath();

  StringRef getClangToolchainPath();

  void setStorePreamblesInMemory(bool StoreInMemory) {
    StorePreamblesInMemory = StoreInMemory;
  }
  bool getStorePreamblesInMemory() const { return StorePreamblesInMemory; }

  void setPreambleStoragePath(StringRef Str) {
    PreambleStoragePath = Str.str();
  }

  StringRef getPreambleStoragePath() const { return PreambleStoragePath; }

  void setInvocationEmissionPath(StringRef Str) {
    InvocationEmissionPath = std::string(Str);
  }

  StringRef getInvocationEmissionPath() const { return InvocationEmissionPath; }
};

/// Logs information about a particular libclang operation like parsing to
/// a new file in the invocation emission path.
class LibclangInvocationReporter {
public:
  enum class OperationKind { ParseOperation, CompletionOperation };

  LibclangInvocationReporter(CIndexer &Idx, OperationKind Op,
                             unsigned ParseOptions,
                             llvm::ArrayRef<const char *> Args,
                             llvm::ArrayRef<std::string> InvocationArgs,
                             llvm::ArrayRef<CXUnsavedFile> UnsavedFiles);
  ~LibclangInvocationReporter();

private:
  std::string File;
};

  /// Return the current size to request for "safety".
  unsigned GetSafetyThreadStackSize();

  /// Set the current size to request for "safety" (or 0, if safety
  /// threads should not be used).
  void SetSafetyThreadStackSize(unsigned Value);

  /// Execution the given code "safely", using crash recovery or safety
  /// threads when possible.
  ///
  /// \return False if a crash was detected.
  bool RunSafely(llvm::CrashRecoveryContext &CRC, llvm::function_ref<void()> Fn,
                 unsigned Size = 0);

  /// Set the thread priority to background.
  /// FIXME: Move to llvm/Support.
  void setThreadBackgroundPriority();

  /// Print libclang's resource usage to standard error.
  void PrintLibclangResourceUsage(CXTranslationUnit TU);

  namespace cxindex {
    void printDiagsToStderr(ASTUnit *Unit);

    /// If \c MacroDefLoc points at a macro definition with \c II as
    /// its name, this retrieves its MacroInfo.
    MacroInfo *getMacroInfo(const IdentifierInfo &II,
                            SourceLocation MacroDefLoc, CXTranslationUnit TU);

    /// Retrieves the corresponding MacroInfo of a MacroDefinitionRecord.
    const MacroInfo *getMacroInfo(const MacroDefinitionRecord *MacroDef,
                                  CXTranslationUnit TU);

    /// If \c Loc resides inside the definition of \c MI and it points at
    /// an identifier that has ever been a macro name, this returns the latest
    /// MacroDefinitionRecord for that name, otherwise it returns NULL.
    MacroDefinitionRecord *checkForMacroInMacroDefinition(const MacroInfo *MI,
                                                          SourceLocation Loc,
                                                          CXTranslationUnit TU);

    /// If \c Tok resides inside the definition of \c MI and it points at
    /// an identifier that has ever been a macro name, this returns the latest
    /// MacroDefinitionRecord for that name, otherwise it returns NULL.
    MacroDefinitionRecord *checkForMacroInMacroDefinition(const MacroInfo *MI,
                                                          const Token &Tok,
                                                          CXTranslationUnit TU);
    }
    }

#endif
