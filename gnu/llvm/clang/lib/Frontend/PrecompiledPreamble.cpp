//===--- PrecompiledPreamble.cpp - Build precompiled preambles --*- C++ -*-===//
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

#include "clang/Frontend/PrecompiledPreamble.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <limits>
#include <mutex>
#include <utility>

using namespace clang;

namespace {

StringRef getInMemoryPreamblePath() {
#if defined(LLVM_ON_UNIX)
  return "/__clang_tmp/___clang_inmemory_preamble___";
#elif defined(_WIN32)
  return "C:\\__clang_tmp\\___clang_inmemory_preamble___";
#else
#warning "Unknown platform. Defaulting to UNIX-style paths for in-memory PCHs"
  return "/__clang_tmp/___clang_inmemory_preamble___";
#endif
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSOverlayForPreamblePCH(StringRef PCHFilename,
                               std::unique_ptr<llvm::MemoryBuffer> PCHBuffer,
                               IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  // We want only the PCH file from the real filesystem to be available,
  // so we create an in-memory VFS with just that and overlay it on top.
  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> PCHFS(
      new llvm::vfs::InMemoryFileSystem());
  PCHFS->addFile(PCHFilename, 0, std::move(PCHBuffer));
  IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> Overlay(
      new llvm::vfs::OverlayFileSystem(VFS));
  Overlay->pushOverlay(PCHFS);
  return Overlay;
}

class PreambleDependencyCollector : public DependencyCollector {
public:
  // We want to collect all dependencies for correctness. Avoiding the real
  // system dependencies (e.g. stl from /usr/lib) would probably be a good idea,
  // but there is no way to distinguish between those and the ones that can be
  // spuriously added by '-isystem' (e.g. to suppress warnings from those
  // headers).
  bool needSystemDependencies() override { return true; }
};

// Collects files whose existence would invalidate the preamble.
// Collecting *all* of these would make validating it too slow though, so we
// just find all the candidates for 'file not found' diagnostics.
//
// A caveat that may be significant for generated files: we'll omit files under
// search path entries whose roots don't exist when the preamble is built.
// These are pruned by InitHeaderSearch and so we don't see the search path.
// It would be nice to include them but we don't want to duplicate all the rest
// of the InitHeaderSearch logic to reconstruct them.
class MissingFileCollector : public PPCallbacks {
  llvm::StringSet<> &Out;
  const HeaderSearch &Search;
  const SourceManager &SM;

public:
  MissingFileCollector(llvm::StringSet<> &Out, const HeaderSearch &Search,
                       const SourceManager &SM)
      : Out(Out), Search(Search), SM(SM) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override {
    // File is std::nullopt if it wasn't found.
    // (We have some false negatives if PP recovered e.g. <foo> -> "foo")
    if (File)
      return;

    // If it's a rare absolute include, we know the full path already.
    if (llvm::sys::path::is_absolute(FileName)) {
      Out.insert(FileName);
      return;
    }

    // Reconstruct the filenames that would satisfy this directive...
    llvm::SmallString<256> Buf;
    auto NotFoundRelativeTo = [&](DirectoryEntryRef DE) {
      Buf = DE.getName();
      llvm::sys::path::append(Buf, FileName);
      llvm::sys::path::remove_dots(Buf, /*remove_dot_dot=*/true);
      Out.insert(Buf);
    };
    // ...relative to the including file.
    if (!IsAngled) {
      if (OptionalFileEntryRef IncludingFile =
              SM.getFileEntryRefForID(SM.getFileID(IncludeTok.getLocation())))
        if (IncludingFile->getDir())
          NotFoundRelativeTo(IncludingFile->getDir());
    }
    // ...relative to the search paths.
    for (const auto &Dir : llvm::make_range(
             IsAngled ? Search.angled_dir_begin() : Search.search_dir_begin(),
             Search.search_dir_end())) {
      // No support for frameworks or header maps yet.
      if (Dir.isNormalDir())
        NotFoundRelativeTo(*Dir.getDirRef());
    }
  }
};

/// Keeps a track of files to be deleted in destructor.
class TemporaryFiles {
public:
  // A static instance to be used by all clients.
  static TemporaryFiles &getInstance();

private:
  // Disallow constructing the class directly.
  TemporaryFiles() = default;
  // Disallow copy.
  TemporaryFiles(const TemporaryFiles &) = delete;

public:
  ~TemporaryFiles();

  /// Adds \p File to a set of tracked files.
  void addFile(StringRef File);

  /// Remove \p File from disk and from the set of tracked files.
  void removeFile(StringRef File);

private:
  std::mutex Mutex;
  llvm::StringSet<> Files;
};

TemporaryFiles &TemporaryFiles::getInstance() {
  static TemporaryFiles Instance;
  return Instance;
}

TemporaryFiles::~TemporaryFiles() {
  std::lock_guard<std::mutex> Guard(Mutex);
  for (const auto &File : Files)
    llvm::sys::fs::remove(File.getKey());
}

void TemporaryFiles::addFile(StringRef File) {
  std::lock_guard<std::mutex> Guard(Mutex);
  auto IsInserted = Files.insert(File).second;
  (void)IsInserted;
  assert(IsInserted && "File has already been added");
}

void TemporaryFiles::removeFile(StringRef File) {
  std::lock_guard<std::mutex> Guard(Mutex);
  auto WasPresent = Files.erase(File);
  (void)WasPresent;
  assert(WasPresent && "File was not tracked");
  llvm::sys::fs::remove(File);
}

// A temp file that would be deleted on destructor call. If destructor is not
// called for any reason, the file will be deleted at static objects'
// destruction.
// An assertion will fire if two TempPCHFiles are created with the same name,
// so it's not intended to be used outside preamble-handling.
class TempPCHFile {
public:
  // A main method used to construct TempPCHFile.
  static std::unique_ptr<TempPCHFile> create(StringRef StoragePath) {
    // FIXME: This is a hack so that we can override the preamble file during
    // crash-recovery testing, which is the only case where the preamble files
    // are not necessarily cleaned up.
    if (const char *TmpFile = ::getenv("CINDEXTEST_PREAMBLE_FILE"))
      return std::unique_ptr<TempPCHFile>(new TempPCHFile(TmpFile));

    llvm::SmallString<128> File;
    // Using the versions of createTemporaryFile() and
    // createUniqueFile() with a file descriptor guarantees
    // that we would never get a race condition in a multi-threaded setting
    // (i.e., multiple threads getting the same temporary path).
    int FD;
    std::error_code EC;
    if (StoragePath.empty())
      EC = llvm::sys::fs::createTemporaryFile("preamble", "pch", FD, File);
    else {
      llvm::SmallString<128> TempPath = StoragePath;
      // Use the same filename model as fs::createTemporaryFile().
      llvm::sys::path::append(TempPath, "preamble-%%%%%%.pch");
      namespace fs = llvm::sys::fs;
      // Use the same owner-only file permissions as fs::createTemporaryFile().
      EC = fs::createUniqueFile(TempPath, FD, File, fs::OF_None,
                                fs::owner_read | fs::owner_write);
    }
    if (EC)
      return nullptr;
    // We only needed to make sure the file exists, close the file right away.
    llvm::sys::Process::SafelyCloseFileDescriptor(FD);
    return std::unique_ptr<TempPCHFile>(new TempPCHFile(File.str().str()));
  }

  TempPCHFile &operator=(const TempPCHFile &) = delete;
  TempPCHFile(const TempPCHFile &) = delete;
  ~TempPCHFile() { TemporaryFiles::getInstance().removeFile(FilePath); };

  /// A path where temporary file is stored.
  llvm::StringRef getFilePath() const { return FilePath; };

private:
  TempPCHFile(std::string FilePath) : FilePath(std::move(FilePath)) {
    TemporaryFiles::getInstance().addFile(this->FilePath);
  }

  std::string FilePath;
};

class PrecompilePreambleAction : public ASTFrontendAction {
public:
  PrecompilePreambleAction(std::shared_ptr<PCHBuffer> Buffer, bool WritePCHFile,
                           PreambleCallbacks &Callbacks)
      : Buffer(std::move(Buffer)), WritePCHFile(WritePCHFile),
        Callbacks(Callbacks) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  bool hasEmittedPreamblePCH() const { return HasEmittedPreamblePCH; }

  void setEmittedPreamblePCH(ASTWriter &Writer) {
    if (FileOS) {
      *FileOS << Buffer->Data;
      // Make sure it hits disk now.
      FileOS.reset();
    }

    this->HasEmittedPreamblePCH = true;
    Callbacks.AfterPCHEmitted(Writer);
  }

  bool BeginSourceFileAction(CompilerInstance &CI) override {
    assert(CI.getLangOpts().CompilingPCH);
    return ASTFrontendAction::BeginSourceFileAction(CI);
  }

  bool shouldEraseOutputFiles() override { return !hasEmittedPreamblePCH(); }
  bool hasCodeCompletionSupport() const override { return false; }
  bool hasASTFileSupport() const override { return false; }
  TranslationUnitKind getTranslationUnitKind() override { return TU_Prefix; }

private:
  friend class PrecompilePreambleConsumer;

  bool HasEmittedPreamblePCH = false;
  std::shared_ptr<PCHBuffer> Buffer;
  bool WritePCHFile; // otherwise the PCH is written into the PCHBuffer only.
  std::unique_ptr<llvm::raw_pwrite_stream> FileOS; // null if in-memory
  PreambleCallbacks &Callbacks;
};

class PrecompilePreambleConsumer : public PCHGenerator {
public:
  PrecompilePreambleConsumer(PrecompilePreambleAction &Action, Preprocessor &PP,
                             InMemoryModuleCache &ModuleCache,
                             StringRef isysroot,
                             std::shared_ptr<PCHBuffer> Buffer)
      : PCHGenerator(PP, ModuleCache, "", isysroot, std::move(Buffer),
                     ArrayRef<std::shared_ptr<ModuleFileExtension>>(),
                     /*AllowASTWithErrors=*/true),
        Action(Action) {}

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    Action.Callbacks.HandleTopLevelDecl(DG);
    return true;
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    PCHGenerator::HandleTranslationUnit(Ctx);
    if (!hasEmittedPCH())
      return;
    Action.setEmittedPreamblePCH(getWriter());
  }

  bool shouldSkipFunctionBody(Decl *D) override {
    return Action.Callbacks.shouldSkipFunctionBody(D);
  }

private:
  PrecompilePreambleAction &Action;
};

std::unique_ptr<ASTConsumer>
PrecompilePreambleAction::CreateASTConsumer(CompilerInstance &CI,
                                            StringRef InFile) {
  std::string Sysroot;
  if (!GeneratePCHAction::ComputeASTConsumerArguments(CI, Sysroot))
    return nullptr;

  if (WritePCHFile) {
    std::string OutputFile; // unused
    FileOS = GeneratePCHAction::CreateOutputFile(CI, InFile, OutputFile);
    if (!FileOS)
      return nullptr;
  }

  if (!CI.getFrontendOpts().RelocatablePCH)
    Sysroot.clear();

  return std::make_unique<PrecompilePreambleConsumer>(
      *this, CI.getPreprocessor(), CI.getModuleCache(), Sysroot, Buffer);
}

template <class T> bool moveOnNoError(llvm::ErrorOr<T> Val, T &Output) {
  if (!Val)
    return false;
  Output = std::move(*Val);
  return true;
}

} // namespace

PreambleBounds clang::ComputePreambleBounds(const LangOptions &LangOpts,
                                            const llvm::MemoryBufferRef &Buffer,
                                            unsigned MaxLines) {
  return Lexer::ComputePreamble(Buffer.getBuffer(), LangOpts, MaxLines);
}

class PrecompiledPreamble::PCHStorage {
public:
  static std::unique_ptr<PCHStorage> file(std::unique_ptr<TempPCHFile> File) {
    assert(File);
    std::unique_ptr<PCHStorage> S(new PCHStorage());
    S->File = std::move(File);
    return S;
  }
  static std::unique_ptr<PCHStorage> inMemory(std::shared_ptr<PCHBuffer> Buf) {
    std::unique_ptr<PCHStorage> S(new PCHStorage());
    S->Memory = std::move(Buf);
    return S;
  }

  enum class Kind { InMemory, TempFile };
  Kind getKind() const {
    if (Memory)
      return Kind::InMemory;
    if (File)
      return Kind::TempFile;
    llvm_unreachable("Neither Memory nor File?");
  }
  llvm::StringRef filePath() const {
    assert(getKind() == Kind::TempFile);
    return File->getFilePath();
  }
  llvm::StringRef memoryContents() const {
    assert(getKind() == Kind::InMemory);
    return StringRef(Memory->Data.data(), Memory->Data.size());
  }

  // Shrink in-memory buffers to fit.
  // This incurs a copy, but preambles tend to be long-lived.
  // Only safe to call once nothing can alias the buffer.
  void shrink() {
    if (!Memory)
      return;
    Memory->Data = decltype(Memory->Data)(Memory->Data);
  }

private:
  PCHStorage() = default;
  PCHStorage(const PCHStorage &) = delete;
  PCHStorage &operator=(const PCHStorage &) = delete;

  std::shared_ptr<PCHBuffer> Memory;
  std::unique_ptr<TempPCHFile> File;
};

PrecompiledPreamble::~PrecompiledPreamble() = default;
PrecompiledPreamble::PrecompiledPreamble(PrecompiledPreamble &&) = default;
PrecompiledPreamble &
PrecompiledPreamble::operator=(PrecompiledPreamble &&) = default;

llvm::ErrorOr<PrecompiledPreamble> PrecompiledPreamble::Build(
    const CompilerInvocation &Invocation,
    const llvm::MemoryBuffer *MainFileBuffer, PreambleBounds Bounds,
    DiagnosticsEngine &Diagnostics,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps, bool StoreInMemory,
    StringRef StoragePath, PreambleCallbacks &Callbacks) {
  assert(VFS && "VFS is null");

  auto PreambleInvocation = std::make_shared<CompilerInvocation>(Invocation);
  FrontendOptions &FrontendOpts = PreambleInvocation->getFrontendOpts();
  PreprocessorOptions &PreprocessorOpts =
      PreambleInvocation->getPreprocessorOpts();

  std::shared_ptr<PCHBuffer> Buffer = std::make_shared<PCHBuffer>();
  std::unique_ptr<PCHStorage> Storage;
  if (StoreInMemory) {
    Storage = PCHStorage::inMemory(Buffer);
  } else {
    // Create a temporary file for the precompiled preamble. In rare
    // circumstances, this can fail.
    std::unique_ptr<TempPCHFile> PreamblePCHFile =
        TempPCHFile::create(StoragePath);
    if (!PreamblePCHFile)
      return BuildPreambleError::CouldntCreateTempFile;
    Storage = PCHStorage::file(std::move(PreamblePCHFile));
  }

  // Save the preamble text for later; we'll need to compare against it for
  // subsequent reparses.
  std::vector<char> PreambleBytes(MainFileBuffer->getBufferStart(),
                                  MainFileBuffer->getBufferStart() +
                                      Bounds.Size);
  bool PreambleEndsAtStartOfLine = Bounds.PreambleEndsAtStartOfLine;

  // Tell the compiler invocation to generate a temporary precompiled header.
  FrontendOpts.ProgramAction = frontend::GeneratePCH;
  FrontendOpts.OutputFile = std::string(
      StoreInMemory ? getInMemoryPreamblePath() : Storage->filePath());
  PreprocessorOpts.PrecompiledPreambleBytes.first = 0;
  PreprocessorOpts.PrecompiledPreambleBytes.second = false;
  // Inform preprocessor to record conditional stack when building the preamble.
  PreprocessorOpts.GeneratePreamble = true;

  // Create the compiler instance to use for building the precompiled preamble.
  std::unique_ptr<CompilerInstance> Clang(
      new CompilerInstance(std::move(PCHContainerOps)));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<CompilerInstance> CICleanup(
      Clang.get());

  Clang->setInvocation(std::move(PreambleInvocation));
  Clang->setDiagnostics(&Diagnostics);

  // Create the target instance.
  if (!Clang->createTarget())
    return BuildPreambleError::CouldntCreateTargetInfo;

  if (Clang->getFrontendOpts().Inputs.size() != 1 ||
      Clang->getFrontendOpts().Inputs[0].getKind().getFormat() !=
          InputKind::Source ||
      Clang->getFrontendOpts().Inputs[0].getKind().getLanguage() ==
          Language::LLVM_IR) {
    return BuildPreambleError::BadInputs;
  }

  // Clear out old caches and data.
  Diagnostics.Reset();
  ProcessWarningOptions(Diagnostics, Clang->getDiagnosticOpts());

  VFS =
      createVFSFromCompilerInvocation(Clang->getInvocation(), Diagnostics, VFS);

  // Create a file manager object to provide access to and cache the filesystem.
  Clang->setFileManager(new FileManager(Clang->getFileSystemOpts(), VFS));

  // Create the source manager.
  Clang->setSourceManager(
      new SourceManager(Diagnostics, Clang->getFileManager()));

  auto PreambleDepCollector = std::make_shared<PreambleDependencyCollector>();
  Clang->addDependencyCollector(PreambleDepCollector);

  Clang->getLangOpts().CompilingPCH = true;

  // Remap the main source file to the preamble buffer.
  StringRef MainFilePath = FrontendOpts.Inputs[0].getFile();
  auto PreambleInputBuffer = llvm::MemoryBuffer::getMemBufferCopy(
      MainFileBuffer->getBuffer().slice(0, Bounds.Size), MainFilePath);
  if (PreprocessorOpts.RetainRemappedFileBuffers) {
    // MainFileBuffer will be deleted by unique_ptr after leaving the method.
    PreprocessorOpts.addRemappedFile(MainFilePath, PreambleInputBuffer.get());
  } else {
    // In that case, remapped buffer will be deleted by CompilerInstance on
    // BeginSourceFile, so we call release() to avoid double deletion.
    PreprocessorOpts.addRemappedFile(MainFilePath,
                                     PreambleInputBuffer.release());
  }

  auto Act = std::make_unique<PrecompilePreambleAction>(
      std::move(Buffer),
      /*WritePCHFile=*/Storage->getKind() == PCHStorage::Kind::TempFile,
      Callbacks);
  if (!Act->BeginSourceFile(*Clang.get(), Clang->getFrontendOpts().Inputs[0]))
    return BuildPreambleError::BeginSourceFileFailed;

  // Performed after BeginSourceFile to ensure Clang->Preprocessor can be
  // referenced in the callback.
  Callbacks.BeforeExecute(*Clang);

  std::unique_ptr<PPCallbacks> DelegatedPPCallbacks =
      Callbacks.createPPCallbacks();
  if (DelegatedPPCallbacks)
    Clang->getPreprocessor().addPPCallbacks(std::move(DelegatedPPCallbacks));
  if (auto CommentHandler = Callbacks.getCommentHandler())
    Clang->getPreprocessor().addCommentHandler(CommentHandler);
  llvm::StringSet<> MissingFiles;
  Clang->getPreprocessor().addPPCallbacks(
      std::make_unique<MissingFileCollector>(
          MissingFiles, Clang->getPreprocessor().getHeaderSearchInfo(),
          Clang->getSourceManager()));

  if (llvm::Error Err = Act->Execute())
    return errorToErrorCode(std::move(Err));

  // Run the callbacks.
  Callbacks.AfterExecute(*Clang);

  Act->EndSourceFile();

  if (!Act->hasEmittedPreamblePCH())
    return BuildPreambleError::CouldntEmitPCH;
  Act.reset(); // Frees the PCH buffer, unless Storage keeps it in memory.

  // Keep track of all of the files that the source manager knows about,
  // so we can verify whether they have changed or not.
  llvm::StringMap<PrecompiledPreamble::PreambleFileHash> FilesInPreamble;

  SourceManager &SourceMgr = Clang->getSourceManager();
  for (auto &Filename : PreambleDepCollector->getDependencies()) {
    auto MaybeFile = Clang->getFileManager().getOptionalFileRef(Filename);
    if (!MaybeFile ||
        MaybeFile == SourceMgr.getFileEntryRefForID(SourceMgr.getMainFileID()))
      continue;
    auto File = *MaybeFile;
    if (time_t ModTime = File.getModificationTime()) {
      FilesInPreamble[File.getName()] =
          PrecompiledPreamble::PreambleFileHash::createForFile(File.getSize(),
                                                               ModTime);
    } else {
      llvm::MemoryBufferRef Buffer =
          SourceMgr.getMemoryBufferForFileOrFake(File);
      FilesInPreamble[File.getName()] =
          PrecompiledPreamble::PreambleFileHash::createForMemoryBuffer(Buffer);
    }
  }

  // Shrinking the storage requires extra temporary memory.
  // Destroying clang first reduces peak memory usage.
  CICleanup.unregister();
  Clang.reset();
  Storage->shrink();
  return PrecompiledPreamble(
      std::move(Storage), std::move(PreambleBytes), PreambleEndsAtStartOfLine,
      std::move(FilesInPreamble), std::move(MissingFiles));
}

PreambleBounds PrecompiledPreamble::getBounds() const {
  return PreambleBounds(PreambleBytes.size(), PreambleEndsAtStartOfLine);
}

std::size_t PrecompiledPreamble::getSize() const {
  switch (Storage->getKind()) {
  case PCHStorage::Kind::InMemory:
    return Storage->memoryContents().size();
  case PCHStorage::Kind::TempFile: {
    uint64_t Result;
    if (llvm::sys::fs::file_size(Storage->filePath(), Result))
      return 0;

    assert(Result <= std::numeric_limits<std::size_t>::max() &&
           "file size did not fit into size_t");
    return Result;
  }
  }
  llvm_unreachable("Unhandled storage kind");
}

bool PrecompiledPreamble::CanReuse(const CompilerInvocation &Invocation,
                                   const llvm::MemoryBufferRef &MainFileBuffer,
                                   PreambleBounds Bounds,
                                   llvm::vfs::FileSystem &VFS) const {

  assert(
      Bounds.Size <= MainFileBuffer.getBufferSize() &&
      "Buffer is too large. Bounds were calculated from a different buffer?");

  auto PreambleInvocation = std::make_shared<CompilerInvocation>(Invocation);
  PreprocessorOptions &PreprocessorOpts =
      PreambleInvocation->getPreprocessorOpts();

  // We've previously computed a preamble. Check whether we have the same
  // preamble now that we did before, and that there's enough space in
  // the main-file buffer within the precompiled preamble to fit the
  // new main file.
  if (PreambleBytes.size() != Bounds.Size ||
      PreambleEndsAtStartOfLine != Bounds.PreambleEndsAtStartOfLine ||
      !std::equal(PreambleBytes.begin(), PreambleBytes.end(),
                  MainFileBuffer.getBuffer().begin()))
    return false;
  // The preamble has not changed. We may be able to re-use the precompiled
  // preamble.

  // Check that none of the files used by the preamble have changed.
  // First, make a record of those files that have been overridden via
  // remapping or unsaved_files.
  std::map<llvm::sys::fs::UniqueID, PreambleFileHash> OverriddenFiles;
  llvm::StringSet<> OverriddenAbsPaths; // Either by buffers or files.
  for (const auto &R : PreprocessorOpts.RemappedFiles) {
    llvm::vfs::Status Status;
    if (!moveOnNoError(VFS.status(R.second), Status)) {
      // If we can't stat the file we're remapping to, assume that something
      // horrible happened.
      return false;
    }
    // If a mapped file was previously missing, then it has changed.
    llvm::SmallString<128> MappedPath(R.first);
    if (!VFS.makeAbsolute(MappedPath))
      OverriddenAbsPaths.insert(MappedPath);

    OverriddenFiles[Status.getUniqueID()] = PreambleFileHash::createForFile(
        Status.getSize(), llvm::sys::toTimeT(Status.getLastModificationTime()));
  }

  // OverridenFileBuffers tracks only the files not found in VFS.
  llvm::StringMap<PreambleFileHash> OverridenFileBuffers;
  for (const auto &RB : PreprocessorOpts.RemappedFileBuffers) {
    const PrecompiledPreamble::PreambleFileHash PreambleHash =
        PreambleFileHash::createForMemoryBuffer(RB.second->getMemBufferRef());
    llvm::vfs::Status Status;
    if (moveOnNoError(VFS.status(RB.first), Status))
      OverriddenFiles[Status.getUniqueID()] = PreambleHash;
    else
      OverridenFileBuffers[RB.first] = PreambleHash;

    llvm::SmallString<128> MappedPath(RB.first);
    if (!VFS.makeAbsolute(MappedPath))
      OverriddenAbsPaths.insert(MappedPath);
  }

  // Check whether anything has changed.
  for (const auto &F : FilesInPreamble) {
    auto OverridenFileBuffer = OverridenFileBuffers.find(F.first());
    if (OverridenFileBuffer != OverridenFileBuffers.end()) {
      // The file's buffer was remapped and the file was not found in VFS.
      // Check whether it matches up with the previous mapping.
      if (OverridenFileBuffer->second != F.second)
        return false;
      continue;
    }

    llvm::vfs::Status Status;
    if (!moveOnNoError(VFS.status(F.first()), Status)) {
      // If the file's buffer is not remapped and we can't stat it,
      // assume that something horrible happened.
      return false;
    }

    std::map<llvm::sys::fs::UniqueID, PreambleFileHash>::iterator Overridden =
        OverriddenFiles.find(Status.getUniqueID());
    if (Overridden != OverriddenFiles.end()) {
      // This file was remapped; check whether the newly-mapped file
      // matches up with the previous mapping.
      if (Overridden->second != F.second)
        return false;
      continue;
    }

    // Neither the file's buffer nor the file itself was remapped;
    // check whether it has changed on disk.
    if (Status.getSize() != uint64_t(F.second.Size) ||
        llvm::sys::toTimeT(Status.getLastModificationTime()) !=
            F.second.ModTime)
      return false;
  }
  for (const auto &F : MissingFiles) {
    // A missing file may be "provided" by an override buffer or file.
    if (OverriddenAbsPaths.count(F.getKey()))
      return false;
    // If a file previously recorded as missing exists as a regular file, then
    // consider the preamble out-of-date.
    if (auto Status = VFS.status(F.getKey())) {
      if (Status->isRegularFile())
        return false;
    }
  }
  return true;
}

void PrecompiledPreamble::AddImplicitPreamble(
    CompilerInvocation &CI, IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
    llvm::MemoryBuffer *MainFileBuffer) const {
  PreambleBounds Bounds(PreambleBytes.size(), PreambleEndsAtStartOfLine);
  configurePreamble(Bounds, CI, VFS, MainFileBuffer);
}

void PrecompiledPreamble::OverridePreamble(
    CompilerInvocation &CI, IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
    llvm::MemoryBuffer *MainFileBuffer) const {
  auto Bounds = ComputePreambleBounds(CI.getLangOpts(), *MainFileBuffer, 0);
  configurePreamble(Bounds, CI, VFS, MainFileBuffer);
}

PrecompiledPreamble::PrecompiledPreamble(
    std::unique_ptr<PCHStorage> Storage, std::vector<char> PreambleBytes,
    bool PreambleEndsAtStartOfLine,
    llvm::StringMap<PreambleFileHash> FilesInPreamble,
    llvm::StringSet<> MissingFiles)
    : Storage(std::move(Storage)), FilesInPreamble(std::move(FilesInPreamble)),
      MissingFiles(std::move(MissingFiles)),
      PreambleBytes(std::move(PreambleBytes)),
      PreambleEndsAtStartOfLine(PreambleEndsAtStartOfLine) {
  assert(this->Storage != nullptr);
}

PrecompiledPreamble::PreambleFileHash
PrecompiledPreamble::PreambleFileHash::createForFile(off_t Size,
                                                     time_t ModTime) {
  PreambleFileHash Result;
  Result.Size = Size;
  Result.ModTime = ModTime;
  Result.MD5 = {};
  return Result;
}

PrecompiledPreamble::PreambleFileHash
PrecompiledPreamble::PreambleFileHash::createForMemoryBuffer(
    const llvm::MemoryBufferRef &Buffer) {
  PreambleFileHash Result;
  Result.Size = Buffer.getBufferSize();
  Result.ModTime = 0;

  llvm::MD5 MD5Ctx;
  MD5Ctx.update(Buffer.getBuffer().data());
  MD5Ctx.final(Result.MD5);

  return Result;
}

void PrecompiledPreamble::configurePreamble(
    PreambleBounds Bounds, CompilerInvocation &CI,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
    llvm::MemoryBuffer *MainFileBuffer) const {
  assert(VFS);

  auto &PreprocessorOpts = CI.getPreprocessorOpts();

  // Remap main file to point to MainFileBuffer.
  auto MainFilePath = CI.getFrontendOpts().Inputs[0].getFile();
  PreprocessorOpts.addRemappedFile(MainFilePath, MainFileBuffer);

  // Configure ImpicitPCHInclude.
  PreprocessorOpts.PrecompiledPreambleBytes.first = Bounds.Size;
  PreprocessorOpts.PrecompiledPreambleBytes.second =
      Bounds.PreambleEndsAtStartOfLine;
  PreprocessorOpts.DisablePCHOrModuleValidation =
      DisableValidationForModuleKind::PCH;

  // Don't bother generating the long version of the predefines buffer.
  // The preamble is going to overwrite it anyway.
  PreprocessorOpts.UsePredefines = false;

  setupPreambleStorage(*Storage, PreprocessorOpts, VFS);
}

void PrecompiledPreamble::setupPreambleStorage(
    const PCHStorage &Storage, PreprocessorOptions &PreprocessorOpts,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS) {
  if (Storage.getKind() == PCHStorage::Kind::TempFile) {
    llvm::StringRef PCHPath = Storage.filePath();
    PreprocessorOpts.ImplicitPCHInclude = PCHPath.str();

    // Make sure we can access the PCH file even if we're using a VFS
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> RealFS =
        llvm::vfs::getRealFileSystem();
    if (VFS == RealFS || VFS->exists(PCHPath))
      return;
    auto Buf = RealFS->getBufferForFile(PCHPath);
    if (!Buf) {
      // We can't read the file even from RealFS, this is clearly an error,
      // but we'll just leave the current VFS as is and let clang's code
      // figure out what to do with missing PCH.
      return;
    }

    // We have a slight inconsistency here -- we're using the VFS to
    // read files, but the PCH was generated in the real file system.
    VFS = createVFSOverlayForPreamblePCH(PCHPath, std::move(*Buf), VFS);
  } else {
    assert(Storage.getKind() == PCHStorage::Kind::InMemory);
    // For in-memory preamble, we have to provide a VFS overlay that makes it
    // accessible.
    StringRef PCHPath = getInMemoryPreamblePath();
    PreprocessorOpts.ImplicitPCHInclude = std::string(PCHPath);

    auto Buf = llvm::MemoryBuffer::getMemBuffer(
        Storage.memoryContents(), PCHPath, /*RequiresNullTerminator=*/false);
    VFS = createVFSOverlayForPreamblePCH(PCHPath, std::move(Buf), VFS);
  }
}

void PreambleCallbacks::BeforeExecute(CompilerInstance &CI) {}
void PreambleCallbacks::AfterExecute(CompilerInstance &CI) {}
void PreambleCallbacks::AfterPCHEmitted(ASTWriter &Writer) {}
void PreambleCallbacks::HandleTopLevelDecl(DeclGroupRef DG) {}
std::unique_ptr<PPCallbacks> PreambleCallbacks::createPPCallbacks() {
  return nullptr;
}
CommentHandler *PreambleCallbacks::getCommentHandler() { return nullptr; }

static llvm::ManagedStatic<BuildPreambleErrorCategory> BuildPreambleErrCategory;

std::error_code clang::make_error_code(BuildPreambleError Error) {
  return std::error_code(static_cast<int>(Error), *BuildPreambleErrCategory);
}

const char *BuildPreambleErrorCategory::name() const noexcept {
  return "build-preamble.error";
}

std::string BuildPreambleErrorCategory::message(int condition) const {
  switch (static_cast<BuildPreambleError>(condition)) {
  case BuildPreambleError::CouldntCreateTempFile:
    return "Could not create temporary file for PCH";
  case BuildPreambleError::CouldntCreateTargetInfo:
    return "CreateTargetInfo() return null";
  case BuildPreambleError::BeginSourceFileFailed:
    return "BeginSourceFile() return an error";
  case BuildPreambleError::CouldntEmitPCH:
    return "Could not emit PCH";
  case BuildPreambleError::BadInputs:
    return "Command line arguments must contain exactly one source file";
  }
  llvm_unreachable("unexpected BuildPreambleError");
}
