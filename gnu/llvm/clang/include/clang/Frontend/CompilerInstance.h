//===-- CompilerInstance.h - Clang Compiler Instance ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMPILERINSTANCE_H_
#define LLVM_CLANG_FRONTEND_COMPILERINSTANCE_H_

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/ModuleLoader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/FileSystem.h"
#include <cassert>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
class raw_fd_ostream;
class Timer;
class TimerGroup;
}

namespace clang {
class ASTContext;
class ASTReader;

namespace serialization {
class ModuleFile;
}

class CodeCompleteConsumer;
class DiagnosticsEngine;
class DiagnosticConsumer;
class FileManager;
class FrontendAction;
class InMemoryModuleCache;
class Module;
class Preprocessor;
class Sema;
class SourceManager;
class TargetInfo;
enum class DisableValidationForModuleKind;

/// CompilerInstance - Helper class for managing a single instance of the Clang
/// compiler.
///
/// The CompilerInstance serves two purposes:
///  (1) It manages the various objects which are necessary to run the compiler,
///      for example the preprocessor, the target information, and the AST
///      context.
///  (2) It provides utility routines for constructing and manipulating the
///      common Clang objects.
///
/// The compiler instance generally owns the instance of all the objects that it
/// manages. However, clients can still share objects by manually setting the
/// object and retaking ownership prior to destroying the CompilerInstance.
///
/// The compiler instance is intended to simplify clients, but not to lock them
/// in to the compiler instance for everything. When possible, utility functions
/// come in two forms; a short form that reuses the CompilerInstance objects,
/// and a long form that takes explicit instances of any required objects.
class CompilerInstance : public ModuleLoader {
  /// The options used in this compiler instance.
  std::shared_ptr<CompilerInvocation> Invocation;

  /// The diagnostics engine instance.
  IntrusiveRefCntPtr<DiagnosticsEngine> Diagnostics;

  /// The target being compiled for.
  IntrusiveRefCntPtr<TargetInfo> Target;

  /// Auxiliary Target info.
  IntrusiveRefCntPtr<TargetInfo> AuxTarget;

  /// The file manager.
  IntrusiveRefCntPtr<FileManager> FileMgr;

  /// The source manager.
  IntrusiveRefCntPtr<SourceManager> SourceMgr;

  /// The cache of PCM files.
  IntrusiveRefCntPtr<InMemoryModuleCache> ModuleCache;

  /// The preprocessor.
  std::shared_ptr<Preprocessor> PP;

  /// The AST context.
  IntrusiveRefCntPtr<ASTContext> Context;

  /// An optional sema source that will be attached to sema.
  IntrusiveRefCntPtr<ExternalSemaSource> ExternalSemaSrc;

  /// The AST consumer.
  std::unique_ptr<ASTConsumer> Consumer;

  /// The code completion consumer.
  std::unique_ptr<CodeCompleteConsumer> CompletionConsumer;

  /// The semantic analysis object.
  std::unique_ptr<Sema> TheSema;

  /// The frontend timer group.
  std::unique_ptr<llvm::TimerGroup> FrontendTimerGroup;

  /// The frontend timer.
  std::unique_ptr<llvm::Timer> FrontendTimer;

  /// The ASTReader, if one exists.
  IntrusiveRefCntPtr<ASTReader> TheASTReader;

  /// The module dependency collector for crashdumps
  std::shared_ptr<ModuleDependencyCollector> ModuleDepCollector;

  /// The module provider.
  std::shared_ptr<PCHContainerOperations> ThePCHContainerOperations;

  std::vector<std::shared_ptr<DependencyCollector>> DependencyCollectors;

  /// Records the set of modules
  class FailedModulesSet {
    llvm::StringSet<> Failed;

  public:
    bool hasAlreadyFailed(StringRef module) { return Failed.count(module) > 0; }

    void addFailed(StringRef module) { Failed.insert(module); }
  };

  /// The set of modules that failed to build.
  ///
  /// This pointer will be shared among all of the compiler instances created
  /// to (re)build modules, so that once a module fails to build anywhere,
  /// other instances will see that the module has failed and won't try to
  /// build it again.
  std::shared_ptr<FailedModulesSet> FailedModules;

  /// The set of top-level modules that has already been built on the
  /// fly as part of this overall compilation action.
  std::map<std::string, std::string, std::less<>> BuiltModules;

  /// Should we delete the BuiltModules when we're done?
  bool DeleteBuiltModules = true;

  /// The location of the module-import keyword for the last module
  /// import.
  SourceLocation LastModuleImportLoc;

  /// The result of the last module import.
  ///
  ModuleLoadResult LastModuleImportResult;

  /// Whether we should (re)build the global module index once we
  /// have finished with this translation unit.
  bool BuildGlobalModuleIndex = false;

  /// We have a full global module index, with all modules.
  bool HaveFullGlobalModuleIndex = false;

  /// One or more modules failed to build.
  bool DisableGeneratingGlobalModuleIndex = false;

  /// The stream for verbose output if owned, otherwise nullptr.
  std::unique_ptr<raw_ostream> OwnedVerboseOutputStream;

  /// The stream for verbose output.
  raw_ostream *VerboseOutputStream = &llvm::errs();

  /// Holds information about the output file.
  ///
  /// If TempFilename is not empty we must rename it to Filename at the end.
  /// TempFilename may be empty and Filename non-empty if creating the temporary
  /// failed.
  struct OutputFile {
    std::string Filename;
    std::optional<llvm::sys::fs::TempFile> File;

    OutputFile(std::string filename,
               std::optional<llvm::sys::fs::TempFile> file)
        : Filename(std::move(filename)), File(std::move(file)) {}
  };

  /// The list of active output files.
  std::list<OutputFile> OutputFiles;

  /// Force an output buffer.
  std::unique_ptr<llvm::raw_pwrite_stream> OutputStream;

  CompilerInstance(const CompilerInstance &) = delete;
  void operator=(const CompilerInstance &) = delete;
public:
  explicit CompilerInstance(
      std::shared_ptr<PCHContainerOperations> PCHContainerOps =
          std::make_shared<PCHContainerOperations>(),
      InMemoryModuleCache *SharedModuleCache = nullptr);
  ~CompilerInstance() override;

  /// @name High-Level Operations
  /// @{

  /// ExecuteAction - Execute the provided action against the compiler's
  /// CompilerInvocation object.
  ///
  /// This function makes the following assumptions:
  ///
  ///  - The invocation options should be initialized. This function does not
  ///    handle the '-help' or '-version' options, clients should handle those
  ///    directly.
  ///
  ///  - The diagnostics engine should have already been created by the client.
  ///
  ///  - No other CompilerInstance state should have been initialized (this is
  ///    an unchecked error).
  ///
  ///  - Clients should have initialized any LLVM target features that may be
  ///    required.
  ///
  ///  - Clients should eventually call llvm_shutdown() upon the completion of
  ///    this routine to ensure that any managed objects are properly destroyed.
  ///
  /// Note that this routine may write output to 'stderr'.
  ///
  /// \param Act - The action to execute.
  /// \return - True on success.
  //
  // FIXME: Eliminate the llvm_shutdown requirement, that should either be part
  // of the context or else not CompilerInstance specific.
  bool ExecuteAction(FrontendAction &Act);

  /// At the end of a compilation, print the number of warnings/errors.
  void printDiagnosticStats();

  /// Load the list of plugins requested in the \c FrontendOptions.
  void LoadRequestedPlugins();

  /// @}
  /// @name Compiler Invocation and Options
  /// @{

  bool hasInvocation() const { return Invocation != nullptr; }

  CompilerInvocation &getInvocation() {
    assert(Invocation && "Compiler instance has no invocation!");
    return *Invocation;
  }

  std::shared_ptr<CompilerInvocation> getInvocationPtr() { return Invocation; }

  /// setInvocation - Replace the current invocation.
  void setInvocation(std::shared_ptr<CompilerInvocation> Value);

  /// Indicates whether we should (re)build the global module index.
  bool shouldBuildGlobalModuleIndex() const;

  /// Set the flag indicating whether we should (re)build the global
  /// module index.
  void setBuildGlobalModuleIndex(bool Build) {
    BuildGlobalModuleIndex = Build;
  }

  /// @}
  /// @name Forwarding Methods
  /// @{

  AnalyzerOptions &getAnalyzerOpts() { return Invocation->getAnalyzerOpts(); }

  CodeGenOptions &getCodeGenOpts() {
    return Invocation->getCodeGenOpts();
  }
  const CodeGenOptions &getCodeGenOpts() const {
    return Invocation->getCodeGenOpts();
  }

  DependencyOutputOptions &getDependencyOutputOpts() {
    return Invocation->getDependencyOutputOpts();
  }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return Invocation->getDependencyOutputOpts();
  }

  DiagnosticOptions &getDiagnosticOpts() {
    return Invocation->getDiagnosticOpts();
  }
  const DiagnosticOptions &getDiagnosticOpts() const {
    return Invocation->getDiagnosticOpts();
  }

  FileSystemOptions &getFileSystemOpts() {
    return Invocation->getFileSystemOpts();
  }
  const FileSystemOptions &getFileSystemOpts() const {
    return Invocation->getFileSystemOpts();
  }

  FrontendOptions &getFrontendOpts() {
    return Invocation->getFrontendOpts();
  }
  const FrontendOptions &getFrontendOpts() const {
    return Invocation->getFrontendOpts();
  }

  HeaderSearchOptions &getHeaderSearchOpts() {
    return Invocation->getHeaderSearchOpts();
  }
  const HeaderSearchOptions &getHeaderSearchOpts() const {
    return Invocation->getHeaderSearchOpts();
  }
  std::shared_ptr<HeaderSearchOptions> getHeaderSearchOptsPtr() const {
    return Invocation->getHeaderSearchOptsPtr();
  }

  APINotesOptions &getAPINotesOpts() { return Invocation->getAPINotesOpts(); }
  const APINotesOptions &getAPINotesOpts() const {
    return Invocation->getAPINotesOpts();
  }

  LangOptions &getLangOpts() { return Invocation->getLangOpts(); }
  const LangOptions &getLangOpts() const { return Invocation->getLangOpts(); }
  std::shared_ptr<LangOptions> getLangOptsPtr() const {
    return Invocation->getLangOptsPtr();
  }

  PreprocessorOptions &getPreprocessorOpts() {
    return Invocation->getPreprocessorOpts();
  }
  const PreprocessorOptions &getPreprocessorOpts() const {
    return Invocation->getPreprocessorOpts();
  }

  PreprocessorOutputOptions &getPreprocessorOutputOpts() {
    return Invocation->getPreprocessorOutputOpts();
  }
  const PreprocessorOutputOptions &getPreprocessorOutputOpts() const {
    return Invocation->getPreprocessorOutputOpts();
  }

  TargetOptions &getTargetOpts() {
    return Invocation->getTargetOpts();
  }
  const TargetOptions &getTargetOpts() const {
    return Invocation->getTargetOpts();
  }

  /// @}
  /// @name Diagnostics Engine
  /// @{

  bool hasDiagnostics() const { return Diagnostics != nullptr; }

  /// Get the current diagnostics engine.
  DiagnosticsEngine &getDiagnostics() const {
    assert(Diagnostics && "Compiler instance has no diagnostics!");
    return *Diagnostics;
  }

  IntrusiveRefCntPtr<DiagnosticsEngine> getDiagnosticsPtr() const {
    assert(Diagnostics && "Compiler instance has no diagnostics!");
    return Diagnostics;
  }

  /// setDiagnostics - Replace the current diagnostics engine.
  void setDiagnostics(DiagnosticsEngine *Value);

  DiagnosticConsumer &getDiagnosticClient() const {
    assert(Diagnostics && Diagnostics->getClient() &&
           "Compiler instance has no diagnostic client!");
    return *Diagnostics->getClient();
  }

  /// @}
  /// @name VerboseOutputStream
  /// @{

  /// Replace the current stream for verbose output.
  void setVerboseOutputStream(raw_ostream &Value);

  /// Replace the current stream for verbose output.
  void setVerboseOutputStream(std::unique_ptr<raw_ostream> Value);

  /// Get the current stream for verbose output.
  raw_ostream &getVerboseOutputStream() {
    return *VerboseOutputStream;
  }

  /// @}
  /// @name Target Info
  /// @{

  bool hasTarget() const { return Target != nullptr; }

  TargetInfo &getTarget() const {
    assert(Target && "Compiler instance has no target!");
    return *Target;
  }

  IntrusiveRefCntPtr<TargetInfo> getTargetPtr() const {
    assert(Target && "Compiler instance has no target!");
    return Target;
  }

  /// Replace the current Target.
  void setTarget(TargetInfo *Value);

  /// @}
  /// @name AuxTarget Info
  /// @{

  TargetInfo *getAuxTarget() const { return AuxTarget.get(); }

  /// Replace the current AuxTarget.
  void setAuxTarget(TargetInfo *Value);

  // Create Target and AuxTarget based on current options
  bool createTarget();

  /// @}
  /// @name Virtual File System
  /// @{

  llvm::vfs::FileSystem &getVirtualFileSystem() const;

  /// @}
  /// @name File Manager
  /// @{

  bool hasFileManager() const { return FileMgr != nullptr; }

  /// Return the current file manager to the caller.
  FileManager &getFileManager() const {
    assert(FileMgr && "Compiler instance has no file manager!");
    return *FileMgr;
  }

  IntrusiveRefCntPtr<FileManager> getFileManagerPtr() const {
    assert(FileMgr && "Compiler instance has no file manager!");
    return FileMgr;
  }

  void resetAndLeakFileManager() {
    llvm::BuryPointer(FileMgr.get());
    FileMgr.resetWithoutRelease();
  }

  /// Replace the current file manager and virtual file system.
  void setFileManager(FileManager *Value);

  /// @}
  /// @name Source Manager
  /// @{

  bool hasSourceManager() const { return SourceMgr != nullptr; }

  /// Return the current source manager.
  SourceManager &getSourceManager() const {
    assert(SourceMgr && "Compiler instance has no source manager!");
    return *SourceMgr;
  }

  IntrusiveRefCntPtr<SourceManager> getSourceManagerPtr() const {
    assert(SourceMgr && "Compiler instance has no source manager!");
    return SourceMgr;
  }

  void resetAndLeakSourceManager() {
    llvm::BuryPointer(SourceMgr.get());
    SourceMgr.resetWithoutRelease();
  }

  /// setSourceManager - Replace the current source manager.
  void setSourceManager(SourceManager *Value);

  /// @}
  /// @name Preprocessor
  /// @{

  bool hasPreprocessor() const { return PP != nullptr; }

  /// Return the current preprocessor.
  Preprocessor &getPreprocessor() const {
    assert(PP && "Compiler instance has no preprocessor!");
    return *PP;
  }

  std::shared_ptr<Preprocessor> getPreprocessorPtr() { return PP; }

  void resetAndLeakPreprocessor() {
    llvm::BuryPointer(new std::shared_ptr<Preprocessor>(PP));
  }

  /// Replace the current preprocessor.
  void setPreprocessor(std::shared_ptr<Preprocessor> Value);

  /// @}
  /// @name ASTContext
  /// @{

  bool hasASTContext() const { return Context != nullptr; }

  ASTContext &getASTContext() const {
    assert(Context && "Compiler instance has no AST context!");
    return *Context;
  }

  IntrusiveRefCntPtr<ASTContext> getASTContextPtr() const {
    assert(Context && "Compiler instance has no AST context!");
    return Context;
  }

  void resetAndLeakASTContext() {
    llvm::BuryPointer(Context.get());
    Context.resetWithoutRelease();
  }

  /// setASTContext - Replace the current AST context.
  void setASTContext(ASTContext *Value);

  /// Replace the current Sema; the compiler instance takes ownership
  /// of S.
  void setSema(Sema *S);

  /// @}
  /// @name ASTConsumer
  /// @{

  bool hasASTConsumer() const { return (bool)Consumer; }

  ASTConsumer &getASTConsumer() const {
    assert(Consumer && "Compiler instance has no AST consumer!");
    return *Consumer;
  }

  /// takeASTConsumer - Remove the current AST consumer and give ownership to
  /// the caller.
  std::unique_ptr<ASTConsumer> takeASTConsumer() { return std::move(Consumer); }

  /// setASTConsumer - Replace the current AST consumer; the compiler instance
  /// takes ownership of \p Value.
  void setASTConsumer(std::unique_ptr<ASTConsumer> Value);

  /// @}
  /// @name Semantic analysis
  /// @{
  bool hasSema() const { return (bool)TheSema; }

  Sema &getSema() const {
    assert(TheSema && "Compiler instance has no Sema object!");
    return *TheSema;
  }

  std::unique_ptr<Sema> takeSema();
  void resetAndLeakSema();

  /// @}
  /// @name Module Management
  /// @{

  IntrusiveRefCntPtr<ASTReader> getASTReader() const;
  void setASTReader(IntrusiveRefCntPtr<ASTReader> Reader);

  std::shared_ptr<ModuleDependencyCollector> getModuleDepCollector() const;
  void setModuleDepCollector(
      std::shared_ptr<ModuleDependencyCollector> Collector);

  std::shared_ptr<PCHContainerOperations> getPCHContainerOperations() const {
    return ThePCHContainerOperations;
  }

  /// Return the appropriate PCHContainerWriter depending on the
  /// current CodeGenOptions.
  const PCHContainerWriter &getPCHContainerWriter() const {
    assert(Invocation && "cannot determine module format without invocation");
    StringRef Format = getHeaderSearchOpts().ModuleFormat;
    auto *Writer = ThePCHContainerOperations->getWriterOrNull(Format);
    if (!Writer) {
      if (Diagnostics)
        Diagnostics->Report(diag::err_module_format_unhandled) << Format;
      llvm::report_fatal_error("unknown module format");
    }
    return *Writer;
  }

  /// Return the appropriate PCHContainerReader depending on the
  /// current CodeGenOptions.
  const PCHContainerReader &getPCHContainerReader() const {
    assert(Invocation && "cannot determine module format without invocation");
    StringRef Format = getHeaderSearchOpts().ModuleFormat;
    auto *Reader = ThePCHContainerOperations->getReaderOrNull(Format);
    if (!Reader) {
      if (Diagnostics)
        Diagnostics->Report(diag::err_module_format_unhandled) << Format;
      llvm::report_fatal_error("unknown module format");
    }
    return *Reader;
  }

  /// @}
  /// @name Code Completion
  /// @{

  bool hasCodeCompletionConsumer() const { return (bool)CompletionConsumer; }

  CodeCompleteConsumer &getCodeCompletionConsumer() const {
    assert(CompletionConsumer &&
           "Compiler instance has no code completion consumer!");
    return *CompletionConsumer;
  }

  /// setCodeCompletionConsumer - Replace the current code completion consumer;
  /// the compiler instance takes ownership of \p Value.
  void setCodeCompletionConsumer(CodeCompleteConsumer *Value);

  /// @}
  /// @name Frontend timer
  /// @{

  bool hasFrontendTimer() const { return (bool)FrontendTimer; }

  llvm::Timer &getFrontendTimer() const {
    assert(FrontendTimer && "Compiler instance has no frontend timer!");
    return *FrontendTimer;
  }

  /// @}
  /// @name Failed modules set
  /// @{

  bool hasFailedModulesSet() const { return (bool)FailedModules; }

  void createFailedModulesSet() {
    FailedModules = std::make_shared<FailedModulesSet>();
  }

  std::shared_ptr<FailedModulesSet> getFailedModulesSetPtr() const {
    return FailedModules;
  }

  void setFailedModulesSet(std::shared_ptr<FailedModulesSet> FMS) {
    FailedModules = FMS;
  }

  /// }
  /// @name Output Files
  /// @{

  /// clearOutputFiles - Clear the output file list. The underlying output
  /// streams must have been closed beforehand.
  ///
  /// \param EraseFiles - If true, attempt to erase the files from disk.
  void clearOutputFiles(bool EraseFiles);

  /// @}
  /// @name Construction Utility Methods
  /// @{

  /// Create the diagnostics engine using the invocation's diagnostic options
  /// and replace any existing one with it.
  ///
  /// Note that this routine also replaces the diagnostic client,
  /// allocating one if one is not provided.
  ///
  /// \param Client If non-NULL, a diagnostic client that will be
  /// attached to (and, then, owned by) the DiagnosticsEngine inside this AST
  /// unit.
  ///
  /// \param ShouldOwnClient If Client is non-NULL, specifies whether
  /// the diagnostic object should take ownership of the client.
  void createDiagnostics(DiagnosticConsumer *Client = nullptr,
                         bool ShouldOwnClient = true);

  /// Create a DiagnosticsEngine object with a the TextDiagnosticPrinter.
  ///
  /// If no diagnostic client is provided, this creates a
  /// DiagnosticConsumer that is owned by the returned diagnostic
  /// object, if using directly the caller is responsible for
  /// releasing the returned DiagnosticsEngine's client eventually.
  ///
  /// \param Opts - The diagnostic options; note that the created text
  /// diagnostic object contains a reference to these options.
  ///
  /// \param Client If non-NULL, a diagnostic client that will be
  /// attached to (and, then, owned by) the returned DiagnosticsEngine
  /// object.
  ///
  /// \param CodeGenOpts If non-NULL, the code gen options in use, which may be
  /// used by some diagnostics printers (for logging purposes only).
  ///
  /// \return The new object on success, or null on failure.
  static IntrusiveRefCntPtr<DiagnosticsEngine>
  createDiagnostics(DiagnosticOptions *Opts,
                    DiagnosticConsumer *Client = nullptr,
                    bool ShouldOwnClient = true,
                    const CodeGenOptions *CodeGenOpts = nullptr);

  /// Create the file manager and replace any existing one with it.
  ///
  /// \return The new file manager on success, or null on failure.
  FileManager *
  createFileManager(IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr);

  /// Create the source manager and replace any existing one with it.
  void createSourceManager(FileManager &FileMgr);

  /// Create the preprocessor, using the invocation, file, and source managers,
  /// and replace any existing one with it.
  void createPreprocessor(TranslationUnitKind TUKind);

  std::string getSpecificModuleCachePath(StringRef ModuleHash);
  std::string getSpecificModuleCachePath() {
    return getSpecificModuleCachePath(getInvocation().getModuleHash());
  }

  /// Create the AST context.
  void createASTContext();

  /// Create an external AST source to read a PCH file and attach it to the AST
  /// context.
  void createPCHExternalASTSource(
      StringRef Path, DisableValidationForModuleKind DisableValidation,
      bool AllowPCHWithCompilerErrors, void *DeserializationListener,
      bool OwnDeserializationListener);

  /// Create an external AST source to read a PCH file.
  ///
  /// \return - The new object on success, or null on failure.
  static IntrusiveRefCntPtr<ASTReader> createPCHExternalASTSource(
      StringRef Path, StringRef Sysroot,
      DisableValidationForModuleKind DisableValidation,
      bool AllowPCHWithCompilerErrors, Preprocessor &PP,
      InMemoryModuleCache &ModuleCache, ASTContext &Context,
      const PCHContainerReader &PCHContainerRdr,
      ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
      ArrayRef<std::shared_ptr<DependencyCollector>> DependencyCollectors,
      void *DeserializationListener, bool OwnDeserializationListener,
      bool Preamble, bool UseGlobalModuleIndex);

  /// Create a code completion consumer using the invocation; note that this
  /// will cause the source manager to truncate the input source file at the
  /// completion point.
  void createCodeCompletionConsumer();

  /// Create a code completion consumer to print code completion results, at
  /// \p Filename, \p Line, and \p Column, to the given output stream \p OS.
  static CodeCompleteConsumer *createCodeCompletionConsumer(
      Preprocessor &PP, StringRef Filename, unsigned Line, unsigned Column,
      const CodeCompleteOptions &Opts, raw_ostream &OS);

  /// Create the Sema object to be used for parsing.
  void createSema(TranslationUnitKind TUKind,
                  CodeCompleteConsumer *CompletionConsumer);

  /// Create the frontend timer and replace any existing one with it.
  void createFrontendTimer();

  /// Create the default output file (from the invocation's options) and add it
  /// to the list of tracked output files.
  ///
  /// The files created by this are usually removed on signal, and, depending
  /// on FrontendOptions, may also use a temporary file (that is, the data is
  /// written to a temporary file which will atomically replace the target
  /// output on success).
  ///
  /// \return - Null on error.
  std::unique_ptr<raw_pwrite_stream> createDefaultOutputFile(
      bool Binary = true, StringRef BaseInput = "", StringRef Extension = "",
      bool RemoveFileOnSignal = true, bool CreateMissingDirectories = false,
      bool ForceUseTemporary = false);

  /// Create a new output file, optionally deriving the output path name, and
  /// add it to the list of tracked output files.
  ///
  /// \return - Null on error.
  std::unique_ptr<raw_pwrite_stream>
  createOutputFile(StringRef OutputPath, bool Binary, bool RemoveFileOnSignal,
                   bool UseTemporary, bool CreateMissingDirectories = false);

private:
  /// Create a new output file and add it to the list of tracked output files.
  ///
  /// If \p OutputPath is empty, then createOutputFile will derive an output
  /// path location as \p BaseInput, with any suffix removed, and \p Extension
  /// appended. If \p OutputPath is not stdout and \p UseTemporary
  /// is true, createOutputFile will create a new temporary file that must be
  /// renamed to \p OutputPath in the end.
  ///
  /// \param OutputPath - If given, the path to the output file.
  /// \param Binary - The mode to open the file in.
  /// \param RemoveFileOnSignal - Whether the file should be registered with
  /// llvm::sys::RemoveFileOnSignal. Note that this is not safe for
  /// multithreaded use, as the underlying signal mechanism is not reentrant
  /// \param UseTemporary - Create a new temporary file that must be renamed to
  /// OutputPath in the end.
  /// \param CreateMissingDirectories - When \p UseTemporary is true, create
  /// missing directories in the output path.
  Expected<std::unique_ptr<raw_pwrite_stream>>
  createOutputFileImpl(StringRef OutputPath, bool Binary,
                       bool RemoveFileOnSignal, bool UseTemporary,
                       bool CreateMissingDirectories);

public:
  std::unique_ptr<raw_pwrite_stream> createNullOutputFile();

  /// @}
  /// @name Initialization Utility Methods
  /// @{

  /// InitializeSourceManager - Initialize the source manager to set InputFile
  /// as the main file.
  ///
  /// \return True on success.
  bool InitializeSourceManager(const FrontendInputFile &Input);

  /// InitializeSourceManager - Initialize the source manager to set InputFile
  /// as the main file.
  ///
  /// \return True on success.
  static bool InitializeSourceManager(const FrontendInputFile &Input,
                                      DiagnosticsEngine &Diags,
                                      FileManager &FileMgr,
                                      SourceManager &SourceMgr);

  /// @}

  void setOutputStream(std::unique_ptr<llvm::raw_pwrite_stream> OutStream) {
    OutputStream = std::move(OutStream);
  }

  std::unique_ptr<llvm::raw_pwrite_stream> takeOutputStream() {
    return std::move(OutputStream);
  }

  void createASTReader();

  bool loadModuleFile(StringRef FileName,
                      serialization::ModuleFile *&LoadedModuleFile);

private:
  /// Find a module, potentially compiling it, before reading its AST.  This is
  /// the guts of loadModule.
  ///
  /// For prebuilt modules, the Module is not expected to exist in
  /// HeaderSearch's ModuleMap.  If a ModuleFile by that name is in the
  /// ModuleManager, then it will be loaded and looked up.
  ///
  /// For implicit modules, the Module is expected to already be in the
  /// ModuleMap.  First attempt to load it from the given path on disk.  If that
  /// fails, defer to compileModuleAndReadAST, which will first build and then
  /// load it.
  ModuleLoadResult findOrCompileModuleAndReadAST(StringRef ModuleName,
                                                 SourceLocation ImportLoc,
                                                 SourceLocation ModuleNameLoc,
                                                 bool IsInclusionDirective);

public:
  ModuleLoadResult loadModule(SourceLocation ImportLoc, ModuleIdPath Path,
                              Module::NameVisibilityKind Visibility,
                              bool IsInclusionDirective) override;

  void createModuleFromSource(SourceLocation ImportLoc, StringRef ModuleName,
                              StringRef Source) override;

  void makeModuleVisible(Module *Mod, Module::NameVisibilityKind Visibility,
                         SourceLocation ImportLoc) override;

  bool hadModuleLoaderFatalFailure() const {
    return ModuleLoader::HadFatalFailure;
  }

  GlobalModuleIndex *loadGlobalModuleIndex(SourceLocation TriggerLoc) override;

  bool lookupMissingImports(StringRef Name, SourceLocation TriggerLoc) override;

  void addDependencyCollector(std::shared_ptr<DependencyCollector> Listener) {
    DependencyCollectors.push_back(std::move(Listener));
  }

  void setExternalSemaSource(IntrusiveRefCntPtr<ExternalSemaSource> ESS);

  InMemoryModuleCache &getModuleCache() const { return *ModuleCache; }
};

} // end namespace clang

#endif
