//===--- PrecompiledPreamble.h - Build precompiled preambles ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper class to build precompiled preamble.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_PRECOMPILEDPREAMBLE_H
#define LLVM_CLANG_FRONTEND_PRECOMPILEDPREAMBLE_H

#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MD5.h"
#include <cstddef>
#include <memory>
#include <system_error>
#include <type_traits>

namespace llvm {
class MemoryBuffer;
class MemoryBufferRef;
namespace vfs {
class FileSystem;
}
} // namespace llvm

namespace clang {
class CompilerInstance;
class CompilerInvocation;
class Decl;
class DeclGroupRef;
class PCHContainerOperations;

/// Runs lexer to compute suggested preamble bounds.
PreambleBounds ComputePreambleBounds(const LangOptions &LangOpts,
                                     const llvm::MemoryBufferRef &Buffer,
                                     unsigned MaxLines);

class PreambleCallbacks;

/// A class holding a PCH and all information to check whether it is valid to
/// reuse the PCH for the subsequent runs. Use BuildPreamble to create PCH and
/// CanReusePreamble + AddImplicitPreamble to make use of it.
class PrecompiledPreamble {
  class PCHStorage;
  struct PreambleFileHash;

public:
  /// Try to build PrecompiledPreamble for \p Invocation. See
  /// BuildPreambleError for possible error codes.
  ///
  /// \param Invocation Original CompilerInvocation with options to compile the
  /// file.
  ///
  /// \param MainFileBuffer Buffer with the contents of the main file.
  ///
  /// \param Bounds Bounds of the preamble, result of calling
  /// ComputePreambleBounds.
  ///
  /// \param Diagnostics Diagnostics engine to be used while building the
  /// preamble.
  ///
  /// \param VFS An instance of vfs::FileSystem to be used for file
  /// accesses.
  ///
  /// \param PCHContainerOps An instance of PCHContainerOperations.
  ///
  /// \param StoreInMemory Store PCH in memory. If false, PCH will be stored in
  /// a temporary file.
  ///
  /// \param StoragePath The path to a directory, in which to create a temporary
  /// file to store PCH in. If empty, the default system temporary directory is
  /// used. This parameter is ignored if \p StoreInMemory is true.
  ///
  /// \param Callbacks A set of callbacks to be executed when building
  /// the preamble.
  static llvm::ErrorOr<PrecompiledPreamble>
  Build(const CompilerInvocation &Invocation,
        const llvm::MemoryBuffer *MainFileBuffer, PreambleBounds Bounds,
        DiagnosticsEngine &Diagnostics,
        IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
        std::shared_ptr<PCHContainerOperations> PCHContainerOps,
        bool StoreInMemory, StringRef StoragePath,
        PreambleCallbacks &Callbacks);

  PrecompiledPreamble(PrecompiledPreamble &&);
  PrecompiledPreamble &operator=(PrecompiledPreamble &&);
  ~PrecompiledPreamble();

  /// PreambleBounds used to build the preamble.
  PreambleBounds getBounds() const;

  /// Returns the size, in bytes, that preamble takes on disk or in memory.
  /// For on-disk preambles returns 0 if filesystem operations fail. Intended to
  /// be used for logging and debugging purposes only.
  std::size_t getSize() const;

  /// Returned string is not null-terminated.
  llvm::StringRef getContents() const {
    return {PreambleBytes.data(), PreambleBytes.size()};
  }

  /// Check whether PrecompiledPreamble can be reused for the new contents(\p
  /// MainFileBuffer) of the main file.
  bool CanReuse(const CompilerInvocation &Invocation,
                const llvm::MemoryBufferRef &MainFileBuffer,
                PreambleBounds Bounds, llvm::vfs::FileSystem &VFS) const;

  /// Changes options inside \p CI to use PCH from this preamble. Also remaps
  /// main file to \p MainFileBuffer and updates \p VFS to ensure the preamble
  /// is accessible.
  /// Requires that CanReuse() is true.
  /// For in-memory preambles, PrecompiledPreamble instance continues to own the
  /// MemoryBuffer with the Preamble after this method returns. The caller is
  /// responsible for making sure the PrecompiledPreamble instance outlives the
  /// compiler run and the AST that will be using the PCH.
  void AddImplicitPreamble(CompilerInvocation &CI,
                           IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
                           llvm::MemoryBuffer *MainFileBuffer) const;

  /// Configure \p CI to use this preamble.
  /// Like AddImplicitPreamble, but doesn't assume CanReuse() is true.
  /// If this preamble does not match the file, it may parse differently.
  void OverridePreamble(CompilerInvocation &CI,
                        IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
                        llvm::MemoryBuffer *MainFileBuffer) const;

private:
  PrecompiledPreamble(std::unique_ptr<PCHStorage> Storage,
                      std::vector<char> PreambleBytes,
                      bool PreambleEndsAtStartOfLine,
                      llvm::StringMap<PreambleFileHash> FilesInPreamble,
                      llvm::StringSet<> MissingFiles);

  /// Data used to determine if a file used in the preamble has been changed.
  struct PreambleFileHash {
    /// All files have size set.
    off_t Size = 0;

    /// Modification time is set for files that are on disk.  For memory
    /// buffers it is zero.
    time_t ModTime = 0;

    /// Memory buffers have MD5 instead of modification time.  We don't
    /// compute MD5 for on-disk files because we hope that modification time is
    /// enough to tell if the file was changed.
    llvm::MD5::MD5Result MD5 = {};

    static PreambleFileHash createForFile(off_t Size, time_t ModTime);
    static PreambleFileHash
    createForMemoryBuffer(const llvm::MemoryBufferRef &Buffer);

    friend bool operator==(const PreambleFileHash &LHS,
                           const PreambleFileHash &RHS) {
      return LHS.Size == RHS.Size && LHS.ModTime == RHS.ModTime &&
             LHS.MD5 == RHS.MD5;
    }
    friend bool operator!=(const PreambleFileHash &LHS,
                           const PreambleFileHash &RHS) {
      return !(LHS == RHS);
    }
  };

  /// Helper function to set up PCH for the preamble into \p CI and \p VFS to
  /// with the specified \p Bounds.
  void configurePreamble(PreambleBounds Bounds, CompilerInvocation &CI,
                         IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
                         llvm::MemoryBuffer *MainFileBuffer) const;

  /// Sets up the PreprocessorOptions and changes VFS, so that PCH stored in \p
  /// Storage is accessible to clang. This method is an implementation detail of
  /// AddImplicitPreamble.
  static void
  setupPreambleStorage(const PCHStorage &Storage,
                       PreprocessorOptions &PreprocessorOpts,
                       IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS);

  /// Manages the memory buffer or temporary file that stores the PCH.
  std::unique_ptr<PCHStorage> Storage;
  /// Keeps track of the files that were used when computing the
  /// preamble, with both their buffer size and their modification time.
  ///
  /// If any of the files have changed from one compile to the next,
  /// the preamble must be thrown away.
  llvm::StringMap<PreambleFileHash> FilesInPreamble;
  /// Files that were not found during preamble building. If any of these now
  /// exist then the preamble should not be reused.
  ///
  /// Storing *all* the missing files that could invalidate the preamble would
  /// make it too expensive to revalidate (when the include path has many
  /// entries, each #include will miss half of them on average).
  /// Instead, we track only files that could have satisfied an #include that
  /// was ultimately not found.
  llvm::StringSet<> MissingFiles;
  /// The contents of the file that was used to precompile the preamble. Only
  /// contains first PreambleBounds::Size bytes. Used to compare if the relevant
  /// part of the file has not changed, so that preamble can be reused.
  std::vector<char> PreambleBytes;
  /// See PreambleBounds::PreambleEndsAtStartOfLine
  bool PreambleEndsAtStartOfLine;
};

/// A set of callbacks to gather useful information while building a preamble.
class PreambleCallbacks {
public:
  virtual ~PreambleCallbacks() = default;

  /// Called before FrontendAction::Execute.
  /// Can be used to store references to various CompilerInstance fields
  /// (e.g. SourceManager) that may be interesting to the consumers of other
  /// callbacks.
  virtual void BeforeExecute(CompilerInstance &CI);
  /// Called after FrontendAction::Execute(), but before
  /// FrontendAction::EndSourceFile(). Can be used to transfer ownership of
  /// various CompilerInstance fields before they are destroyed.
  virtual void AfterExecute(CompilerInstance &CI);
  /// Called after PCH has been emitted. \p Writer may be used to retrieve
  /// information about AST, serialized in PCH.
  virtual void AfterPCHEmitted(ASTWriter &Writer);
  /// Called for each TopLevelDecl.
  /// NOTE: To allow more flexibility a custom ASTConsumer could probably be
  /// used instead, but having only this method allows a simpler API.
  virtual void HandleTopLevelDecl(DeclGroupRef DG);
  /// Creates wrapper class for PPCallbacks so we can also process information
  /// about includes that are inside of a preamble. Called after BeforeExecute.
  virtual std::unique_ptr<PPCallbacks> createPPCallbacks();
  /// The returned CommentHandler will be added to the preprocessor if not null.
  virtual CommentHandler *getCommentHandler();
  /// Determines which function bodies are parsed, by default skips everything.
  /// Only used if FrontendOpts::SkipFunctionBodies is true.
  /// See ASTConsumer::shouldSkipFunctionBody.
  virtual bool shouldSkipFunctionBody(Decl *D) { return true; }
};

enum class BuildPreambleError {
  CouldntCreateTempFile = 1,
  CouldntCreateTargetInfo,
  BeginSourceFileFailed,
  CouldntEmitPCH,
  BadInputs
};

class BuildPreambleErrorCategory final : public std::error_category {
public:
  const char *name() const noexcept override;
  std::string message(int condition) const override;
};

std::error_code make_error_code(BuildPreambleError Error);
} // namespace clang

template <>
struct std::is_error_code_enum<clang::BuildPreambleError> : std::true_type {};

#endif
