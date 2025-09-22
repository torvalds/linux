//===--- CompilerInstance.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Frontend/ChainedDiagnosticConsumer.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/LogDiagnosticPrinter.h"
#include "clang/Frontend/SARIFDiagnosticPrinter.h"
#include "clang/Frontend/SerializedDiagnosticPrinter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Frontend/VerifyDiagnosticConsumer.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <optional>
#include <time.h>
#include <utility>

using namespace clang;

CompilerInstance::CompilerInstance(
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    InMemoryModuleCache *SharedModuleCache)
    : ModuleLoader(/* BuildingModule = */ SharedModuleCache),
      Invocation(new CompilerInvocation()),
      ModuleCache(SharedModuleCache ? SharedModuleCache
                                    : new InMemoryModuleCache),
      ThePCHContainerOperations(std::move(PCHContainerOps)) {}

CompilerInstance::~CompilerInstance() {
  assert(OutputFiles.empty() && "Still output files in flight?");
}

void CompilerInstance::setInvocation(
    std::shared_ptr<CompilerInvocation> Value) {
  Invocation = std::move(Value);
}

bool CompilerInstance::shouldBuildGlobalModuleIndex() const {
  return (BuildGlobalModuleIndex ||
          (TheASTReader && TheASTReader->isGlobalIndexUnavailable() &&
           getFrontendOpts().GenerateGlobalModuleIndex)) &&
         !DisableGeneratingGlobalModuleIndex;
}

void CompilerInstance::setDiagnostics(DiagnosticsEngine *Value) {
  Diagnostics = Value;
}

void CompilerInstance::setVerboseOutputStream(raw_ostream &Value) {
  OwnedVerboseOutputStream.reset();
  VerboseOutputStream = &Value;
}

void CompilerInstance::setVerboseOutputStream(std::unique_ptr<raw_ostream> Value) {
  OwnedVerboseOutputStream.swap(Value);
  VerboseOutputStream = OwnedVerboseOutputStream.get();
}

void CompilerInstance::setTarget(TargetInfo *Value) { Target = Value; }
void CompilerInstance::setAuxTarget(TargetInfo *Value) { AuxTarget = Value; }

bool CompilerInstance::createTarget() {
  // Create the target instance.
  setTarget(TargetInfo::CreateTargetInfo(getDiagnostics(),
                                         getInvocation().TargetOpts));
  if (!hasTarget())
    return false;

  // Check whether AuxTarget exists, if not, then create TargetInfo for the
  // other side of CUDA/OpenMP/SYCL compilation.
  if (!getAuxTarget() &&
      (getLangOpts().CUDA || getLangOpts().OpenMPIsTargetDevice ||
       getLangOpts().SYCLIsDevice) &&
      !getFrontendOpts().AuxTriple.empty()) {
    auto TO = std::make_shared<TargetOptions>();
    TO->Triple = llvm::Triple::normalize(getFrontendOpts().AuxTriple);
    if (getFrontendOpts().AuxTargetCPU)
      TO->CPU = *getFrontendOpts().AuxTargetCPU;
    if (getFrontendOpts().AuxTargetFeatures)
      TO->FeaturesAsWritten = *getFrontendOpts().AuxTargetFeatures;
    TO->HostTriple = getTarget().getTriple().str();
    setAuxTarget(TargetInfo::CreateTargetInfo(getDiagnostics(), TO));
  }

  if (!getTarget().hasStrictFP() && !getLangOpts().ExpStrictFP) {
    if (getLangOpts().RoundingMath) {
      getDiagnostics().Report(diag::warn_fe_backend_unsupported_fp_rounding);
      getLangOpts().RoundingMath = false;
    }
    auto FPExc = getLangOpts().getFPExceptionMode();
    if (FPExc != LangOptions::FPE_Default && FPExc != LangOptions::FPE_Ignore) {
      getDiagnostics().Report(diag::warn_fe_backend_unsupported_fp_exceptions);
      getLangOpts().setFPExceptionMode(LangOptions::FPE_Ignore);
    }
    // FIXME: can we disable FEnvAccess?
  }

  // We should do it here because target knows nothing about
  // language options when it's being created.
  if (getLangOpts().OpenCL &&
      !getTarget().validateOpenCLTarget(getLangOpts(), getDiagnostics()))
    return false;

  // Inform the target of the language options.
  // FIXME: We shouldn't need to do this, the target should be immutable once
  // created. This complexity should be lifted elsewhere.
  getTarget().adjust(getDiagnostics(), getLangOpts());

  if (auto *Aux = getAuxTarget())
    getTarget().setAuxTarget(Aux);

  return true;
}

llvm::vfs::FileSystem &CompilerInstance::getVirtualFileSystem() const {
  return getFileManager().getVirtualFileSystem();
}

void CompilerInstance::setFileManager(FileManager *Value) {
  FileMgr = Value;
}

void CompilerInstance::setSourceManager(SourceManager *Value) {
  SourceMgr = Value;
}

void CompilerInstance::setPreprocessor(std::shared_ptr<Preprocessor> Value) {
  PP = std::move(Value);
}

void CompilerInstance::setASTContext(ASTContext *Value) {
  Context = Value;

  if (Context && Consumer)
    getASTConsumer().Initialize(getASTContext());
}

void CompilerInstance::setSema(Sema *S) {
  TheSema.reset(S);
}

void CompilerInstance::setASTConsumer(std::unique_ptr<ASTConsumer> Value) {
  Consumer = std::move(Value);

  if (Context && Consumer)
    getASTConsumer().Initialize(getASTContext());
}

void CompilerInstance::setCodeCompletionConsumer(CodeCompleteConsumer *Value) {
  CompletionConsumer.reset(Value);
}

std::unique_ptr<Sema> CompilerInstance::takeSema() {
  return std::move(TheSema);
}

IntrusiveRefCntPtr<ASTReader> CompilerInstance::getASTReader() const {
  return TheASTReader;
}
void CompilerInstance::setASTReader(IntrusiveRefCntPtr<ASTReader> Reader) {
  assert(ModuleCache.get() == &Reader->getModuleManager().getModuleCache() &&
         "Expected ASTReader to use the same PCM cache");
  TheASTReader = std::move(Reader);
}

std::shared_ptr<ModuleDependencyCollector>
CompilerInstance::getModuleDepCollector() const {
  return ModuleDepCollector;
}

void CompilerInstance::setModuleDepCollector(
    std::shared_ptr<ModuleDependencyCollector> Collector) {
  ModuleDepCollector = std::move(Collector);
}

static void collectHeaderMaps(const HeaderSearch &HS,
                              std::shared_ptr<ModuleDependencyCollector> MDC) {
  SmallVector<std::string, 4> HeaderMapFileNames;
  HS.getHeaderMapFileNames(HeaderMapFileNames);
  for (auto &Name : HeaderMapFileNames)
    MDC->addFile(Name);
}

static void collectIncludePCH(CompilerInstance &CI,
                              std::shared_ptr<ModuleDependencyCollector> MDC) {
  const PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
  if (PPOpts.ImplicitPCHInclude.empty())
    return;

  StringRef PCHInclude = PPOpts.ImplicitPCHInclude;
  FileManager &FileMgr = CI.getFileManager();
  auto PCHDir = FileMgr.getOptionalDirectoryRef(PCHInclude);
  if (!PCHDir) {
    MDC->addFile(PCHInclude);
    return;
  }

  std::error_code EC;
  SmallString<128> DirNative;
  llvm::sys::path::native(PCHDir->getName(), DirNative);
  llvm::vfs::FileSystem &FS = FileMgr.getVirtualFileSystem();
  SimpleASTReaderListener Validator(CI.getPreprocessor());
  for (llvm::vfs::directory_iterator Dir = FS.dir_begin(DirNative, EC), DirEnd;
       Dir != DirEnd && !EC; Dir.increment(EC)) {
    // Check whether this is an AST file. ASTReader::isAcceptableASTFile is not
    // used here since we're not interested in validating the PCH at this time,
    // but only to check whether this is a file containing an AST.
    if (!ASTReader::readASTFileControlBlock(
            Dir->path(), FileMgr, CI.getModuleCache(),
            CI.getPCHContainerReader(),
            /*FindModuleFileExtensions=*/false, Validator,
            /*ValidateDiagnosticOptions=*/false))
      MDC->addFile(Dir->path());
  }
}

static void collectVFSEntries(CompilerInstance &CI,
                              std::shared_ptr<ModuleDependencyCollector> MDC) {
  if (CI.getHeaderSearchOpts().VFSOverlayFiles.empty())
    return;

  // Collect all VFS found.
  SmallVector<llvm::vfs::YAMLVFSEntry, 16> VFSEntries;
  for (const std::string &VFSFile : CI.getHeaderSearchOpts().VFSOverlayFiles) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
        llvm::MemoryBuffer::getFile(VFSFile);
    if (!Buffer)
      return;
    llvm::vfs::collectVFSFromYAML(std::move(Buffer.get()),
                                  /*DiagHandler*/ nullptr, VFSFile, VFSEntries);
  }

  for (auto &E : VFSEntries)
    MDC->addFile(E.VPath, E.RPath);
}

// Diagnostics
static void SetUpDiagnosticLog(DiagnosticOptions *DiagOpts,
                               const CodeGenOptions *CodeGenOpts,
                               DiagnosticsEngine &Diags) {
  std::error_code EC;
  std::unique_ptr<raw_ostream> StreamOwner;
  raw_ostream *OS = &llvm::errs();
  if (DiagOpts->DiagnosticLogFile != "-") {
    // Create the output stream.
    auto FileOS = std::make_unique<llvm::raw_fd_ostream>(
        DiagOpts->DiagnosticLogFile, EC,
        llvm::sys::fs::OF_Append | llvm::sys::fs::OF_TextWithCRLF);
    if (EC) {
      Diags.Report(diag::warn_fe_cc_log_diagnostics_failure)
          << DiagOpts->DiagnosticLogFile << EC.message();
    } else {
      FileOS->SetUnbuffered();
      OS = FileOS.get();
      StreamOwner = std::move(FileOS);
    }
  }

  // Chain in the diagnostic client which will log the diagnostics.
  auto Logger = std::make_unique<LogDiagnosticPrinter>(*OS, DiagOpts,
                                                        std::move(StreamOwner));
  if (CodeGenOpts)
    Logger->setDwarfDebugFlags(CodeGenOpts->DwarfDebugFlags);
  if (Diags.ownsClient()) {
    Diags.setClient(
        new ChainedDiagnosticConsumer(Diags.takeClient(), std::move(Logger)));
  } else {
    Diags.setClient(
        new ChainedDiagnosticConsumer(Diags.getClient(), std::move(Logger)));
  }
}

static void SetupSerializedDiagnostics(DiagnosticOptions *DiagOpts,
                                       DiagnosticsEngine &Diags,
                                       StringRef OutputFile) {
  auto SerializedConsumer =
      clang::serialized_diags::create(OutputFile, DiagOpts);

  if (Diags.ownsClient()) {
    Diags.setClient(new ChainedDiagnosticConsumer(
        Diags.takeClient(), std::move(SerializedConsumer)));
  } else {
    Diags.setClient(new ChainedDiagnosticConsumer(
        Diags.getClient(), std::move(SerializedConsumer)));
  }
}

void CompilerInstance::createDiagnostics(DiagnosticConsumer *Client,
                                         bool ShouldOwnClient) {
  Diagnostics = createDiagnostics(&getDiagnosticOpts(), Client,
                                  ShouldOwnClient, &getCodeGenOpts());
}

IntrusiveRefCntPtr<DiagnosticsEngine>
CompilerInstance::createDiagnostics(DiagnosticOptions *Opts,
                                    DiagnosticConsumer *Client,
                                    bool ShouldOwnClient,
                                    const CodeGenOptions *CodeGenOpts) {
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine>
      Diags(new DiagnosticsEngine(DiagID, Opts));

  // Create the diagnostic client for reporting errors or for
  // implementing -verify.
  if (Client) {
    Diags->setClient(Client, ShouldOwnClient);
  } else if (Opts->getFormat() == DiagnosticOptions::SARIF) {
    Diags->setClient(new SARIFDiagnosticPrinter(llvm::errs(), Opts));
  } else
    Diags->setClient(new TextDiagnosticPrinter(llvm::errs(), Opts));

  // Chain in -verify checker, if requested.
  if (Opts->VerifyDiagnostics)
    Diags->setClient(new VerifyDiagnosticConsumer(*Diags));

  // Chain in -diagnostic-log-file dumper, if requested.
  if (!Opts->DiagnosticLogFile.empty())
    SetUpDiagnosticLog(Opts, CodeGenOpts, *Diags);

  if (!Opts->DiagnosticSerializationFile.empty())
    SetupSerializedDiagnostics(Opts, *Diags,
                               Opts->DiagnosticSerializationFile);

  // Configure our handling of diagnostics.
  ProcessWarningOptions(*Diags, *Opts);

  return Diags;
}

// File Manager

FileManager *CompilerInstance::createFileManager(
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  if (!VFS)
    VFS = FileMgr ? &FileMgr->getVirtualFileSystem()
                  : createVFSFromCompilerInvocation(getInvocation(),
                                                    getDiagnostics());
  assert(VFS && "FileManager has no VFS?");
  FileMgr = new FileManager(getFileSystemOpts(), std::move(VFS));
  return FileMgr.get();
}

// Source Manager

void CompilerInstance::createSourceManager(FileManager &FileMgr) {
  SourceMgr = new SourceManager(getDiagnostics(), FileMgr);
}

// Initialize the remapping of files to alternative contents, e.g.,
// those specified through other files.
static void InitializeFileRemapping(DiagnosticsEngine &Diags,
                                    SourceManager &SourceMgr,
                                    FileManager &FileMgr,
                                    const PreprocessorOptions &InitOpts) {
  // Remap files in the source manager (with buffers).
  for (const auto &RB : InitOpts.RemappedFileBuffers) {
    // Create the file entry for the file that we're mapping from.
    FileEntryRef FromFile =
        FileMgr.getVirtualFileRef(RB.first, RB.second->getBufferSize(), 0);

    // Override the contents of the "from" file with the contents of the
    // "to" file. If the caller owns the buffers, then pass a MemoryBufferRef;
    // otherwise, pass as a std::unique_ptr<MemoryBuffer> to transfer ownership
    // to the SourceManager.
    if (InitOpts.RetainRemappedFileBuffers)
      SourceMgr.overrideFileContents(FromFile, RB.second->getMemBufferRef());
    else
      SourceMgr.overrideFileContents(
          FromFile, std::unique_ptr<llvm::MemoryBuffer>(RB.second));
  }

  // Remap files in the source manager (with other files).
  for (const auto &RF : InitOpts.RemappedFiles) {
    // Find the file that we're mapping to.
    OptionalFileEntryRef ToFile = FileMgr.getOptionalFileRef(RF.second);
    if (!ToFile) {
      Diags.Report(diag::err_fe_remap_missing_to_file) << RF.first << RF.second;
      continue;
    }

    // Create the file entry for the file that we're mapping from.
    const FileEntry *FromFile =
        FileMgr.getVirtualFile(RF.first, ToFile->getSize(), 0);
    if (!FromFile) {
      Diags.Report(diag::err_fe_remap_missing_from_file) << RF.first;
      continue;
    }

    // Override the contents of the "from" file with the contents of
    // the "to" file.
    SourceMgr.overrideFileContents(FromFile, *ToFile);
  }

  SourceMgr.setOverridenFilesKeepOriginalName(
      InitOpts.RemappedFilesKeepOriginalName);
}

// Preprocessor

void CompilerInstance::createPreprocessor(TranslationUnitKind TUKind) {
  const PreprocessorOptions &PPOpts = getPreprocessorOpts();

  // The AST reader holds a reference to the old preprocessor (if any).
  TheASTReader.reset();

  // Create the Preprocessor.
  HeaderSearch *HeaderInfo =
      new HeaderSearch(getHeaderSearchOptsPtr(), getSourceManager(),
                       getDiagnostics(), getLangOpts(), &getTarget());
  PP = std::make_shared<Preprocessor>(Invocation->getPreprocessorOptsPtr(),
                                      getDiagnostics(), getLangOpts(),
                                      getSourceManager(), *HeaderInfo, *this,
                                      /*IdentifierInfoLookup=*/nullptr,
                                      /*OwnsHeaderSearch=*/true, TUKind);
  getTarget().adjust(getDiagnostics(), getLangOpts());
  PP->Initialize(getTarget(), getAuxTarget());

  if (PPOpts.DetailedRecord)
    PP->createPreprocessingRecord();

  // Apply remappings to the source manager.
  InitializeFileRemapping(PP->getDiagnostics(), PP->getSourceManager(),
                          PP->getFileManager(), PPOpts);

  // Predefine macros and configure the preprocessor.
  InitializePreprocessor(*PP, PPOpts, getPCHContainerReader(),
                         getFrontendOpts(), getCodeGenOpts());

  // Initialize the header search object.  In CUDA compilations, we use the aux
  // triple (the host triple) to initialize our header search, since we need to
  // find the host headers in order to compile the CUDA code.
  const llvm::Triple *HeaderSearchTriple = &PP->getTargetInfo().getTriple();
  if (PP->getTargetInfo().getTriple().getOS() == llvm::Triple::CUDA &&
      PP->getAuxTargetInfo())
    HeaderSearchTriple = &PP->getAuxTargetInfo()->getTriple();

  ApplyHeaderSearchOptions(PP->getHeaderSearchInfo(), getHeaderSearchOpts(),
                           PP->getLangOpts(), *HeaderSearchTriple);

  PP->setPreprocessedOutput(getPreprocessorOutputOpts().ShowCPP);

  if (PP->getLangOpts().Modules && PP->getLangOpts().ImplicitModules) {
    std::string ModuleHash = getInvocation().getModuleHash();
    PP->getHeaderSearchInfo().setModuleHash(ModuleHash);
    PP->getHeaderSearchInfo().setModuleCachePath(
        getSpecificModuleCachePath(ModuleHash));
  }

  // Handle generating dependencies, if requested.
  const DependencyOutputOptions &DepOpts = getDependencyOutputOpts();
  if (!DepOpts.OutputFile.empty())
    addDependencyCollector(std::make_shared<DependencyFileGenerator>(DepOpts));
  if (!DepOpts.DOTOutputFile.empty())
    AttachDependencyGraphGen(*PP, DepOpts.DOTOutputFile,
                             getHeaderSearchOpts().Sysroot);

  // If we don't have a collector, but we are collecting module dependencies,
  // then we're the top level compiler instance and need to create one.
  if (!ModuleDepCollector && !DepOpts.ModuleDependencyOutputDir.empty()) {
    ModuleDepCollector = std::make_shared<ModuleDependencyCollector>(
        DepOpts.ModuleDependencyOutputDir);
  }

  // If there is a module dep collector, register with other dep collectors
  // and also (a) collect header maps and (b) TODO: input vfs overlay files.
  if (ModuleDepCollector) {
    addDependencyCollector(ModuleDepCollector);
    collectHeaderMaps(PP->getHeaderSearchInfo(), ModuleDepCollector);
    collectIncludePCH(*this, ModuleDepCollector);
    collectVFSEntries(*this, ModuleDepCollector);
  }

  for (auto &Listener : DependencyCollectors)
    Listener->attachToPreprocessor(*PP);

  // Handle generating header include information, if requested.
  if (DepOpts.ShowHeaderIncludes)
    AttachHeaderIncludeGen(*PP, DepOpts);
  if (!DepOpts.HeaderIncludeOutputFile.empty()) {
    StringRef OutputPath = DepOpts.HeaderIncludeOutputFile;
    if (OutputPath == "-")
      OutputPath = "";
    AttachHeaderIncludeGen(*PP, DepOpts,
                           /*ShowAllHeaders=*/true, OutputPath,
                           /*ShowDepth=*/false);
  }

  if (DepOpts.ShowIncludesDest != ShowIncludesDestination::None) {
    AttachHeaderIncludeGen(*PP, DepOpts,
                           /*ShowAllHeaders=*/true, /*OutputPath=*/"",
                           /*ShowDepth=*/true, /*MSStyle=*/true);
  }
}

std::string CompilerInstance::getSpecificModuleCachePath(StringRef ModuleHash) {
  // Set up the module path, including the hash for the module-creation options.
  SmallString<256> SpecificModuleCache(getHeaderSearchOpts().ModuleCachePath);
  if (!SpecificModuleCache.empty() && !getHeaderSearchOpts().DisableModuleHash)
    llvm::sys::path::append(SpecificModuleCache, ModuleHash);
  return std::string(SpecificModuleCache);
}

// ASTContext

void CompilerInstance::createASTContext() {
  Preprocessor &PP = getPreprocessor();
  auto *Context = new ASTContext(getLangOpts(), PP.getSourceManager(),
                                 PP.getIdentifierTable(), PP.getSelectorTable(),
                                 PP.getBuiltinInfo(), PP.TUKind);
  Context->InitBuiltinTypes(getTarget(), getAuxTarget());
  setASTContext(Context);
}

// ExternalASTSource

namespace {
// Helper to recursively read the module names for all modules we're adding.
// We mark these as known and redirect any attempt to load that module to
// the files we were handed.
struct ReadModuleNames : ASTReaderListener {
  Preprocessor &PP;
  llvm::SmallVector<std::string, 8> LoadedModules;

  ReadModuleNames(Preprocessor &PP) : PP(PP) {}

  void ReadModuleName(StringRef ModuleName) override {
    // Keep the module name as a string for now. It's not safe to create a new
    // IdentifierInfo from an ASTReader callback.
    LoadedModules.push_back(ModuleName.str());
  }

  void registerAll() {
    ModuleMap &MM = PP.getHeaderSearchInfo().getModuleMap();
    for (const std::string &LoadedModule : LoadedModules)
      MM.cacheModuleLoad(*PP.getIdentifierInfo(LoadedModule),
                         MM.findModule(LoadedModule));
    LoadedModules.clear();
  }

  void markAllUnavailable() {
    for (const std::string &LoadedModule : LoadedModules) {
      if (Module *M = PP.getHeaderSearchInfo().getModuleMap().findModule(
              LoadedModule)) {
        M->HasIncompatibleModuleFile = true;

        // Mark module as available if the only reason it was unavailable
        // was missing headers.
        SmallVector<Module *, 2> Stack;
        Stack.push_back(M);
        while (!Stack.empty()) {
          Module *Current = Stack.pop_back_val();
          if (Current->IsUnimportable) continue;
          Current->IsAvailable = true;
          auto SubmodulesRange = Current->submodules();
          Stack.insert(Stack.end(), SubmodulesRange.begin(),
                       SubmodulesRange.end());
        }
      }
    }
    LoadedModules.clear();
  }
};
} // namespace

void CompilerInstance::createPCHExternalASTSource(
    StringRef Path, DisableValidationForModuleKind DisableValidation,
    bool AllowPCHWithCompilerErrors, void *DeserializationListener,
    bool OwnDeserializationListener) {
  bool Preamble = getPreprocessorOpts().PrecompiledPreambleBytes.first != 0;
  TheASTReader = createPCHExternalASTSource(
      Path, getHeaderSearchOpts().Sysroot, DisableValidation,
      AllowPCHWithCompilerErrors, getPreprocessor(), getModuleCache(),
      getASTContext(), getPCHContainerReader(),
      getFrontendOpts().ModuleFileExtensions, DependencyCollectors,
      DeserializationListener, OwnDeserializationListener, Preamble,
      getFrontendOpts().UseGlobalModuleIndex);
}

IntrusiveRefCntPtr<ASTReader> CompilerInstance::createPCHExternalASTSource(
    StringRef Path, StringRef Sysroot,
    DisableValidationForModuleKind DisableValidation,
    bool AllowPCHWithCompilerErrors, Preprocessor &PP,
    InMemoryModuleCache &ModuleCache, ASTContext &Context,
    const PCHContainerReader &PCHContainerRdr,
    ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
    ArrayRef<std::shared_ptr<DependencyCollector>> DependencyCollectors,
    void *DeserializationListener, bool OwnDeserializationListener,
    bool Preamble, bool UseGlobalModuleIndex) {
  HeaderSearchOptions &HSOpts = PP.getHeaderSearchInfo().getHeaderSearchOpts();

  IntrusiveRefCntPtr<ASTReader> Reader(new ASTReader(
      PP, ModuleCache, &Context, PCHContainerRdr, Extensions,
      Sysroot.empty() ? "" : Sysroot.data(), DisableValidation,
      AllowPCHWithCompilerErrors, /*AllowConfigurationMismatch*/ false,
      HSOpts.ModulesValidateSystemHeaders, HSOpts.ValidateASTInputFilesContent,
      UseGlobalModuleIndex));

  // We need the external source to be set up before we read the AST, because
  // eagerly-deserialized declarations may use it.
  Context.setExternalSource(Reader.get());

  Reader->setDeserializationListener(
      static_cast<ASTDeserializationListener *>(DeserializationListener),
      /*TakeOwnership=*/OwnDeserializationListener);

  for (auto &Listener : DependencyCollectors)
    Listener->attachToASTReader(*Reader);

  auto Listener = std::make_unique<ReadModuleNames>(PP);
  auto &ListenerRef = *Listener;
  ASTReader::ListenerScope ReadModuleNamesListener(*Reader,
                                                   std::move(Listener));

  switch (Reader->ReadAST(Path,
                          Preamble ? serialization::MK_Preamble
                                   : serialization::MK_PCH,
                          SourceLocation(),
                          ASTReader::ARR_None)) {
  case ASTReader::Success:
    // Set the predefines buffer as suggested by the PCH reader. Typically, the
    // predefines buffer will be empty.
    PP.setPredefines(Reader->getSuggestedPredefines());
    ListenerRef.registerAll();
    return Reader;

  case ASTReader::Failure:
    // Unrecoverable failure: don't even try to process the input file.
    break;

  case ASTReader::Missing:
  case ASTReader::OutOfDate:
  case ASTReader::VersionMismatch:
  case ASTReader::ConfigurationMismatch:
  case ASTReader::HadErrors:
    // No suitable PCH file could be found. Return an error.
    break;
  }

  ListenerRef.markAllUnavailable();
  Context.setExternalSource(nullptr);
  return nullptr;
}

// Code Completion

static bool EnableCodeCompletion(Preprocessor &PP,
                                 StringRef Filename,
                                 unsigned Line,
                                 unsigned Column) {
  // Tell the source manager to chop off the given file at a specific
  // line and column.
  auto Entry = PP.getFileManager().getOptionalFileRef(Filename);
  if (!Entry) {
    PP.getDiagnostics().Report(diag::err_fe_invalid_code_complete_file)
      << Filename;
    return true;
  }

  // Truncate the named file at the given line/column.
  PP.SetCodeCompletionPoint(*Entry, Line, Column);
  return false;
}

void CompilerInstance::createCodeCompletionConsumer() {
  const ParsedSourceLocation &Loc = getFrontendOpts().CodeCompletionAt;
  if (!CompletionConsumer) {
    setCodeCompletionConsumer(createCodeCompletionConsumer(
        getPreprocessor(), Loc.FileName, Loc.Line, Loc.Column,
        getFrontendOpts().CodeCompleteOpts, llvm::outs()));
    return;
  } else if (EnableCodeCompletion(getPreprocessor(), Loc.FileName,
                                  Loc.Line, Loc.Column)) {
    setCodeCompletionConsumer(nullptr);
    return;
  }
}

void CompilerInstance::createFrontendTimer() {
  FrontendTimerGroup.reset(
      new llvm::TimerGroup("frontend", "Clang front-end time report"));
  FrontendTimer.reset(
      new llvm::Timer("frontend", "Clang front-end timer",
                      *FrontendTimerGroup));
}

CodeCompleteConsumer *
CompilerInstance::createCodeCompletionConsumer(Preprocessor &PP,
                                               StringRef Filename,
                                               unsigned Line,
                                               unsigned Column,
                                               const CodeCompleteOptions &Opts,
                                               raw_ostream &OS) {
  if (EnableCodeCompletion(PP, Filename, Line, Column))
    return nullptr;

  // Set up the creation routine for code-completion.
  return new PrintingCodeCompleteConsumer(Opts, OS);
}

void CompilerInstance::createSema(TranslationUnitKind TUKind,
                                  CodeCompleteConsumer *CompletionConsumer) {
  TheSema.reset(new Sema(getPreprocessor(), getASTContext(), getASTConsumer(),
                         TUKind, CompletionConsumer));

  // Set up API notes.
  TheSema->APINotes.setSwiftVersion(getAPINotesOpts().SwiftVersion);

  // Attach the external sema source if there is any.
  if (ExternalSemaSrc) {
    TheSema->addExternalSource(ExternalSemaSrc.get());
    ExternalSemaSrc->InitializeSema(*TheSema);
  }

  // If we're building a module and are supposed to load API notes,
  // notify the API notes manager.
  if (auto *currentModule = getPreprocessor().getCurrentModule()) {
    (void)TheSema->APINotes.loadCurrentModuleAPINotes(
        currentModule, getLangOpts().APINotesModules,
        getAPINotesOpts().ModuleSearchPaths);
  }
}

// Output Files

void CompilerInstance::clearOutputFiles(bool EraseFiles) {
  // The ASTConsumer can own streams that write to the output files.
  assert(!hasASTConsumer() && "ASTConsumer should be reset");
  // Ignore errors that occur when trying to discard the temp file.
  for (OutputFile &OF : OutputFiles) {
    if (EraseFiles) {
      if (OF.File)
        consumeError(OF.File->discard());
      if (!OF.Filename.empty())
        llvm::sys::fs::remove(OF.Filename);
      continue;
    }

    if (!OF.File)
      continue;

    if (OF.File->TmpName.empty()) {
      consumeError(OF.File->discard());
      continue;
    }

    llvm::Error E = OF.File->keep(OF.Filename);
    if (!E)
      continue;

    getDiagnostics().Report(diag::err_unable_to_rename_temp)
        << OF.File->TmpName << OF.Filename << std::move(E);

    llvm::sys::fs::remove(OF.File->TmpName);
  }
  OutputFiles.clear();
  if (DeleteBuiltModules) {
    for (auto &Module : BuiltModules)
      llvm::sys::fs::remove(Module.second);
    BuiltModules.clear();
  }
}

std::unique_ptr<raw_pwrite_stream> CompilerInstance::createDefaultOutputFile(
    bool Binary, StringRef InFile, StringRef Extension, bool RemoveFileOnSignal,
    bool CreateMissingDirectories, bool ForceUseTemporary) {
  StringRef OutputPath = getFrontendOpts().OutputFile;
  std::optional<SmallString<128>> PathStorage;
  if (OutputPath.empty()) {
    if (InFile == "-" || Extension.empty()) {
      OutputPath = "-";
    } else {
      PathStorage.emplace(InFile);
      llvm::sys::path::replace_extension(*PathStorage, Extension);
      OutputPath = *PathStorage;
    }
  }

  return createOutputFile(OutputPath, Binary, RemoveFileOnSignal,
                          getFrontendOpts().UseTemporary || ForceUseTemporary,
                          CreateMissingDirectories);
}

std::unique_ptr<raw_pwrite_stream> CompilerInstance::createNullOutputFile() {
  return std::make_unique<llvm::raw_null_ostream>();
}

std::unique_ptr<raw_pwrite_stream>
CompilerInstance::createOutputFile(StringRef OutputPath, bool Binary,
                                   bool RemoveFileOnSignal, bool UseTemporary,
                                   bool CreateMissingDirectories) {
  Expected<std::unique_ptr<raw_pwrite_stream>> OS =
      createOutputFileImpl(OutputPath, Binary, RemoveFileOnSignal, UseTemporary,
                           CreateMissingDirectories);
  if (OS)
    return std::move(*OS);
  getDiagnostics().Report(diag::err_fe_unable_to_open_output)
      << OutputPath << errorToErrorCode(OS.takeError()).message();
  return nullptr;
}

Expected<std::unique_ptr<llvm::raw_pwrite_stream>>
CompilerInstance::createOutputFileImpl(StringRef OutputPath, bool Binary,
                                       bool RemoveFileOnSignal,
                                       bool UseTemporary,
                                       bool CreateMissingDirectories) {
  assert((!CreateMissingDirectories || UseTemporary) &&
         "CreateMissingDirectories is only allowed when using temporary files");

  // If '-working-directory' was passed, the output filename should be
  // relative to that.
  std::optional<SmallString<128>> AbsPath;
  if (OutputPath != "-" && !llvm::sys::path::is_absolute(OutputPath)) {
    assert(hasFileManager() &&
           "File Manager is required to fix up relative path.\n");

    AbsPath.emplace(OutputPath);
    FileMgr->FixupRelativePath(*AbsPath);
    OutputPath = *AbsPath;
  }

  std::unique_ptr<llvm::raw_fd_ostream> OS;
  std::optional<StringRef> OSFile;

  if (UseTemporary) {
    if (OutputPath == "-")
      UseTemporary = false;
    else {
      llvm::sys::fs::file_status Status;
      llvm::sys::fs::status(OutputPath, Status);
      if (llvm::sys::fs::exists(Status)) {
        // Fail early if we can't write to the final destination.
        if (!llvm::sys::fs::can_write(OutputPath))
          return llvm::errorCodeToError(
              make_error_code(llvm::errc::operation_not_permitted));

        // Don't use a temporary if the output is a special file. This handles
        // things like '-o /dev/null'
        if (!llvm::sys::fs::is_regular_file(Status))
          UseTemporary = false;
      }
    }
  }

  std::optional<llvm::sys::fs::TempFile> Temp;
  if (UseTemporary) {
    // Create a temporary file.
    // Insert -%%%%%%%% before the extension (if any), and because some tools
    // (noticeable, clang's own GlobalModuleIndex.cpp) glob for build
    // artifacts, also append .tmp.
    StringRef OutputExtension = llvm::sys::path::extension(OutputPath);
    SmallString<128> TempPath =
        StringRef(OutputPath).drop_back(OutputExtension.size());
    TempPath += "-%%%%%%%%";
    TempPath += OutputExtension;
    TempPath += ".tmp";
    llvm::sys::fs::OpenFlags BinaryFlags =
        Binary ? llvm::sys::fs::OF_None : llvm::sys::fs::OF_Text;
    Expected<llvm::sys::fs::TempFile> ExpectedFile =
        llvm::sys::fs::TempFile::create(
            TempPath, llvm::sys::fs::all_read | llvm::sys::fs::all_write,
            BinaryFlags);

    llvm::Error E = handleErrors(
        ExpectedFile.takeError(), [&](const llvm::ECError &E) -> llvm::Error {
          std::error_code EC = E.convertToErrorCode();
          if (CreateMissingDirectories &&
              EC == llvm::errc::no_such_file_or_directory) {
            StringRef Parent = llvm::sys::path::parent_path(OutputPath);
            EC = llvm::sys::fs::create_directories(Parent);
            if (!EC) {
              ExpectedFile = llvm::sys::fs::TempFile::create(
                  TempPath, llvm::sys::fs::all_read | llvm::sys::fs::all_write,
                  BinaryFlags);
              if (!ExpectedFile)
                return llvm::errorCodeToError(
                    llvm::errc::no_such_file_or_directory);
            }
          }
          return llvm::errorCodeToError(EC);
        });

    if (E) {
      consumeError(std::move(E));
    } else {
      Temp = std::move(ExpectedFile.get());
      OS.reset(new llvm::raw_fd_ostream(Temp->FD, /*shouldClose=*/false));
      OSFile = Temp->TmpName;
    }
    // If we failed to create the temporary, fallback to writing to the file
    // directly. This handles the corner case where we cannot write to the
    // directory, but can write to the file.
  }

  if (!OS) {
    OSFile = OutputPath;
    std::error_code EC;
    OS.reset(new llvm::raw_fd_ostream(
        *OSFile, EC,
        (Binary ? llvm::sys::fs::OF_None : llvm::sys::fs::OF_TextWithCRLF)));
    if (EC)
      return llvm::errorCodeToError(EC);
  }

  // Add the output file -- but don't try to remove "-", since this means we are
  // using stdin.
  OutputFiles.emplace_back(((OutputPath != "-") ? OutputPath : "").str(),
                           std::move(Temp));

  if (!Binary || OS->supportsSeeking())
    return std::move(OS);

  return std::make_unique<llvm::buffer_unique_ostream>(std::move(OS));
}

// Initialization Utilities

bool CompilerInstance::InitializeSourceManager(const FrontendInputFile &Input){
  return InitializeSourceManager(Input, getDiagnostics(), getFileManager(),
                                 getSourceManager());
}

// static
bool CompilerInstance::InitializeSourceManager(const FrontendInputFile &Input,
                                               DiagnosticsEngine &Diags,
                                               FileManager &FileMgr,
                                               SourceManager &SourceMgr) {
  SrcMgr::CharacteristicKind Kind =
      Input.getKind().getFormat() == InputKind::ModuleMap
          ? Input.isSystem() ? SrcMgr::C_System_ModuleMap
                             : SrcMgr::C_User_ModuleMap
          : Input.isSystem() ? SrcMgr::C_System : SrcMgr::C_User;

  if (Input.isBuffer()) {
    SourceMgr.setMainFileID(SourceMgr.createFileID(Input.getBuffer(), Kind));
    assert(SourceMgr.getMainFileID().isValid() &&
           "Couldn't establish MainFileID!");
    return true;
  }

  StringRef InputFile = Input.getFile();

  // Figure out where to get and map in the main file.
  auto FileOrErr = InputFile == "-"
                       ? FileMgr.getSTDIN()
                       : FileMgr.getFileRef(InputFile, /*OpenFile=*/true);
  if (!FileOrErr) {
    auto EC = llvm::errorToErrorCode(FileOrErr.takeError());
    if (InputFile != "-")
      Diags.Report(diag::err_fe_error_reading) << InputFile << EC.message();
    else
      Diags.Report(diag::err_fe_error_reading_stdin) << EC.message();
    return false;
  }

  SourceMgr.setMainFileID(
      SourceMgr.createFileID(*FileOrErr, SourceLocation(), Kind));

  assert(SourceMgr.getMainFileID().isValid() &&
         "Couldn't establish MainFileID!");
  return true;
}

// High-Level Operations

bool CompilerInstance::ExecuteAction(FrontendAction &Act) {
  assert(hasDiagnostics() && "Diagnostics engine is not initialized!");
  assert(!getFrontendOpts().ShowHelp && "Client must handle '-help'!");
  assert(!getFrontendOpts().ShowVersion && "Client must handle '-version'!");

  // Mark this point as the bottom of the stack if we don't have somewhere
  // better. We generally expect frontend actions to be invoked with (nearly)
  // DesiredStackSpace available.
  noteBottomOfStack();

  auto FinishDiagnosticClient = llvm::make_scope_exit([&]() {
    // Notify the diagnostic client that all files were processed.
    getDiagnosticClient().finish();
  });

  raw_ostream &OS = getVerboseOutputStream();

  if (!Act.PrepareToExecute(*this))
    return false;

  if (!createTarget())
    return false;

  // rewriter project will change target built-in bool type from its default.
  if (getFrontendOpts().ProgramAction == frontend::RewriteObjC)
    getTarget().noSignedCharForObjCBool();

  // Validate/process some options.
  if (getHeaderSearchOpts().Verbose)
    OS << "clang -cc1 version " CLANG_VERSION_STRING << " based upon LLVM "
       << LLVM_VERSION_STRING << " default target "
       << llvm::sys::getDefaultTargetTriple() << "\n";

  if (getCodeGenOpts().TimePasses)
    createFrontendTimer();

  if (getFrontendOpts().ShowStats || !getFrontendOpts().StatsFile.empty())
    llvm::EnableStatistics(false);

  // Sort vectors containing toc data and no toc data variables to facilitate
  // binary search later.
  llvm::sort(getCodeGenOpts().TocDataVarsUserSpecified);
  llvm::sort(getCodeGenOpts().NoTocDataVars);

  for (const FrontendInputFile &FIF : getFrontendOpts().Inputs) {
    // Reset the ID tables if we are reusing the SourceManager and parsing
    // regular files.
    if (hasSourceManager() && !Act.isModelParsingAction())
      getSourceManager().clearIDTables();

    if (Act.BeginSourceFile(*this, FIF)) {
      if (llvm::Error Err = Act.Execute()) {
        consumeError(std::move(Err)); // FIXME this drops errors on the floor.
      }
      Act.EndSourceFile();
    }
  }

  printDiagnosticStats();

  if (getFrontendOpts().ShowStats) {
    if (hasFileManager()) {
      getFileManager().PrintStats();
      OS << '\n';
    }
    llvm::PrintStatistics(OS);
  }
  StringRef StatsFile = getFrontendOpts().StatsFile;
  if (!StatsFile.empty()) {
    llvm::sys::fs::OpenFlags FileFlags = llvm::sys::fs::OF_TextWithCRLF;
    if (getFrontendOpts().AppendStats)
      FileFlags |= llvm::sys::fs::OF_Append;
    std::error_code EC;
    auto StatS =
        std::make_unique<llvm::raw_fd_ostream>(StatsFile, EC, FileFlags);
    if (EC) {
      getDiagnostics().Report(diag::warn_fe_unable_to_open_stats_file)
          << StatsFile << EC.message();
    } else {
      llvm::PrintStatisticsJSON(*StatS);
    }
  }

  return !getDiagnostics().getClient()->getNumErrors();
}

void CompilerInstance::printDiagnosticStats() {
  if (!getDiagnosticOpts().ShowCarets)
    return;

  raw_ostream &OS = getVerboseOutputStream();

  // We can have multiple diagnostics sharing one diagnostic client.
  // Get the total number of warnings/errors from the client.
  unsigned NumWarnings = getDiagnostics().getClient()->getNumWarnings();
  unsigned NumErrors = getDiagnostics().getClient()->getNumErrors();

  if (NumWarnings)
    OS << NumWarnings << " warning" << (NumWarnings == 1 ? "" : "s");
  if (NumWarnings && NumErrors)
    OS << " and ";
  if (NumErrors)
    OS << NumErrors << " error" << (NumErrors == 1 ? "" : "s");
  if (NumWarnings || NumErrors) {
    OS << " generated";
    if (getLangOpts().CUDA) {
      if (!getLangOpts().CUDAIsDevice) {
        OS << " when compiling for host";
      } else {
        OS << " when compiling for " << getTargetOpts().CPU;
      }
    }
    OS << ".\n";
  }
}

void CompilerInstance::LoadRequestedPlugins() {
  // Load any requested plugins.
  for (const std::string &Path : getFrontendOpts().Plugins) {
    std::string Error;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(Path.c_str(), &Error))
      getDiagnostics().Report(diag::err_fe_unable_to_load_plugin)
          << Path << Error;
  }

  // Check if any of the loaded plugins replaces the main AST action
  for (const FrontendPluginRegistry::entry &Plugin :
       FrontendPluginRegistry::entries()) {
    std::unique_ptr<PluginASTAction> P(Plugin.instantiate());
    if (P->getActionType() == PluginASTAction::ReplaceAction) {
      getFrontendOpts().ProgramAction = clang::frontend::PluginAction;
      getFrontendOpts().ActionName = Plugin.getName().str();
      break;
    }
  }
}

/// Determine the appropriate source input kind based on language
/// options.
static Language getLanguageFromOptions(const LangOptions &LangOpts) {
  if (LangOpts.OpenCL)
    return Language::OpenCL;
  if (LangOpts.CUDA)
    return Language::CUDA;
  if (LangOpts.ObjC)
    return LangOpts.CPlusPlus ? Language::ObjCXX : Language::ObjC;
  return LangOpts.CPlusPlus ? Language::CXX : Language::C;
}

/// Compile a module file for the given module, using the options
/// provided by the importing compiler instance. Returns true if the module
/// was built without errors.
static bool
compileModuleImpl(CompilerInstance &ImportingInstance, SourceLocation ImportLoc,
                  StringRef ModuleName, FrontendInputFile Input,
                  StringRef OriginalModuleMapFile, StringRef ModuleFileName,
                  llvm::function_ref<void(CompilerInstance &)> PreBuildStep =
                      [](CompilerInstance &) {},
                  llvm::function_ref<void(CompilerInstance &)> PostBuildStep =
                      [](CompilerInstance &) {}) {
  llvm::TimeTraceScope TimeScope("Module Compile", ModuleName);

  // Never compile a module that's already finalized - this would cause the
  // existing module to be freed, causing crashes if it is later referenced
  if (ImportingInstance.getModuleCache().isPCMFinal(ModuleFileName)) {
    ImportingInstance.getDiagnostics().Report(
        ImportLoc, diag::err_module_rebuild_finalized)
        << ModuleName;
    return false;
  }

  // Construct a compiler invocation for creating this module.
  auto Invocation =
      std::make_shared<CompilerInvocation>(ImportingInstance.getInvocation());

  PreprocessorOptions &PPOpts = Invocation->getPreprocessorOpts();

  // For any options that aren't intended to affect how a module is built,
  // reset them to their default values.
  Invocation->resetNonModularOptions();

  // Remove any macro definitions that are explicitly ignored by the module.
  // They aren't supposed to affect how the module is built anyway.
  HeaderSearchOptions &HSOpts = Invocation->getHeaderSearchOpts();
  llvm::erase_if(PPOpts.Macros,
                 [&HSOpts](const std::pair<std::string, bool> &def) {
                   StringRef MacroDef = def.first;
                   return HSOpts.ModulesIgnoreMacros.contains(
                       llvm::CachedHashString(MacroDef.split('=').first));
                 });

  // If the original compiler invocation had -fmodule-name, pass it through.
  Invocation->getLangOpts().ModuleName =
      ImportingInstance.getInvocation().getLangOpts().ModuleName;

  // Note the name of the module we're building.
  Invocation->getLangOpts().CurrentModule = std::string(ModuleName);

  // If there is a module map file, build the module using the module map.
  // Set up the inputs/outputs so that we build the module from its umbrella
  // header.
  FrontendOptions &FrontendOpts = Invocation->getFrontendOpts();
  FrontendOpts.OutputFile = ModuleFileName.str();
  FrontendOpts.DisableFree = false;
  FrontendOpts.GenerateGlobalModuleIndex = false;
  FrontendOpts.BuildingImplicitModule = true;
  FrontendOpts.OriginalModuleMap = std::string(OriginalModuleMapFile);
  // Force implicitly-built modules to hash the content of the module file.
  HSOpts.ModulesHashContent = true;
  FrontendOpts.Inputs = {Input};

  // Don't free the remapped file buffers; they are owned by our caller.
  PPOpts.RetainRemappedFileBuffers = true;

  DiagnosticOptions &DiagOpts = Invocation->getDiagnosticOpts();

  DiagOpts.VerifyDiagnostics = 0;
  assert(ImportingInstance.getInvocation().getModuleHash() ==
         Invocation->getModuleHash() && "Module hash mismatch!");

  // Construct a compiler instance that will be used to actually create the
  // module.  Since we're sharing an in-memory module cache,
  // CompilerInstance::CompilerInstance is responsible for finalizing the
  // buffers to prevent use-after-frees.
  CompilerInstance Instance(ImportingInstance.getPCHContainerOperations(),
                            &ImportingInstance.getModuleCache());
  auto &Inv = *Invocation;
  Instance.setInvocation(std::move(Invocation));

  Instance.createDiagnostics(new ForwardingDiagnosticConsumer(
                                   ImportingInstance.getDiagnosticClient()),
                             /*ShouldOwnClient=*/true);

  if (llvm::is_contained(DiagOpts.SystemHeaderWarningsModules, ModuleName))
    Instance.getDiagnostics().setSuppressSystemWarnings(false);

  if (FrontendOpts.ModulesShareFileManager) {
    Instance.setFileManager(&ImportingInstance.getFileManager());
  } else {
    Instance.createFileManager(&ImportingInstance.getVirtualFileSystem());
  }
  Instance.createSourceManager(Instance.getFileManager());
  SourceManager &SourceMgr = Instance.getSourceManager();

  // Note that this module is part of the module build stack, so that we
  // can detect cycles in the module graph.
  SourceMgr.setModuleBuildStack(
    ImportingInstance.getSourceManager().getModuleBuildStack());
  SourceMgr.pushModuleBuildStack(ModuleName,
    FullSourceLoc(ImportLoc, ImportingInstance.getSourceManager()));

  // Make sure that the failed-module structure has been allocated in
  // the importing instance, and propagate the pointer to the newly-created
  // instance.
  if (!ImportingInstance.hasFailedModulesSet())
    ImportingInstance.createFailedModulesSet();
  Instance.setFailedModulesSet(ImportingInstance.getFailedModulesSetPtr());

  // If we're collecting module dependencies, we need to share a collector
  // between all of the module CompilerInstances. Other than that, we don't
  // want to produce any dependency output from the module build.
  Instance.setModuleDepCollector(ImportingInstance.getModuleDepCollector());
  Inv.getDependencyOutputOpts() = DependencyOutputOptions();

  ImportingInstance.getDiagnostics().Report(ImportLoc,
                                            diag::remark_module_build)
    << ModuleName << ModuleFileName;

  PreBuildStep(Instance);

  // Execute the action to actually build the module in-place. Use a separate
  // thread so that we get a stack large enough.
  bool Crashed = !llvm::CrashRecoveryContext().RunSafelyOnThread(
      [&]() {
        GenerateModuleFromModuleMapAction Action;
        Instance.ExecuteAction(Action);
      },
      DesiredStackSize);

  PostBuildStep(Instance);

  ImportingInstance.getDiagnostics().Report(ImportLoc,
                                            diag::remark_module_build_done)
    << ModuleName;

  // Propagate the statistics to the parent FileManager.
  if (!FrontendOpts.ModulesShareFileManager)
    ImportingInstance.getFileManager().AddStats(Instance.getFileManager());

  if (Crashed) {
    // Clear the ASTConsumer if it hasn't been already, in case it owns streams
    // that must be closed before clearing output files.
    Instance.setSema(nullptr);
    Instance.setASTConsumer(nullptr);

    // Delete any remaining temporary files related to Instance.
    Instance.clearOutputFiles(/*EraseFiles=*/true);
  }

  // If \p AllowPCMWithCompilerErrors is set return 'success' even if errors
  // occurred.
  return !Instance.getDiagnostics().hasErrorOccurred() ||
         Instance.getFrontendOpts().AllowPCMWithCompilerErrors;
}

static OptionalFileEntryRef getPublicModuleMap(FileEntryRef File,
                                               FileManager &FileMgr) {
  StringRef Filename = llvm::sys::path::filename(File.getName());
  SmallString<128> PublicFilename(File.getDir().getName());
  if (Filename == "module_private.map")
    llvm::sys::path::append(PublicFilename, "module.map");
  else if (Filename == "module.private.modulemap")
    llvm::sys::path::append(PublicFilename, "module.modulemap");
  else
    return std::nullopt;
  return FileMgr.getOptionalFileRef(PublicFilename);
}

/// Compile a module file for the given module in a separate compiler instance,
/// using the options provided by the importing compiler instance. Returns true
/// if the module was built without errors.
static bool compileModule(CompilerInstance &ImportingInstance,
                          SourceLocation ImportLoc, Module *Module,
                          StringRef ModuleFileName) {
  InputKind IK(getLanguageFromOptions(ImportingInstance.getLangOpts()),
               InputKind::ModuleMap);

  // Get or create the module map that we'll use to build this module.
  ModuleMap &ModMap
    = ImportingInstance.getPreprocessor().getHeaderSearchInfo().getModuleMap();
  SourceManager &SourceMgr = ImportingInstance.getSourceManager();
  bool Result;
  if (FileID ModuleMapFID = ModMap.getContainingModuleMapFileID(Module);
      ModuleMapFID.isValid()) {
    // We want to use the top-level module map. If we don't, the compiling
    // instance may think the containing module map is a top-level one, while
    // the importing instance knows it's included from a parent module map via
    // the extern directive. This mismatch could bite us later.
    SourceLocation Loc = SourceMgr.getIncludeLoc(ModuleMapFID);
    while (Loc.isValid() && isModuleMap(SourceMgr.getFileCharacteristic(Loc))) {
      ModuleMapFID = SourceMgr.getFileID(Loc);
      Loc = SourceMgr.getIncludeLoc(ModuleMapFID);
    }

    OptionalFileEntryRef ModuleMapFile =
        SourceMgr.getFileEntryRefForID(ModuleMapFID);
    assert(ModuleMapFile && "Top-level module map with no FileID");

    // Canonicalize compilation to start with the public module map. This is
    // vital for submodules declarations in the private module maps to be
    // correctly parsed when depending on a top level module in the public one.
    if (OptionalFileEntryRef PublicMMFile = getPublicModuleMap(
            *ModuleMapFile, ImportingInstance.getFileManager()))
      ModuleMapFile = PublicMMFile;

    StringRef ModuleMapFilePath = ModuleMapFile->getNameAsRequested();

    // Use the module map where this module resides.
    Result = compileModuleImpl(
        ImportingInstance, ImportLoc, Module->getTopLevelModuleName(),
        FrontendInputFile(ModuleMapFilePath, IK, +Module->IsSystem),
        ModMap.getModuleMapFileForUniquing(Module)->getName(), ModuleFileName);
  } else {
    // FIXME: We only need to fake up an input file here as a way of
    // transporting the module's directory to the module map parser. We should
    // be able to do that more directly, and parse from a memory buffer without
    // inventing this file.
    SmallString<128> FakeModuleMapFile(Module->Directory->getName());
    llvm::sys::path::append(FakeModuleMapFile, "__inferred_module.map");

    std::string InferredModuleMapContent;
    llvm::raw_string_ostream OS(InferredModuleMapContent);
    Module->print(OS);
    OS.flush();

    Result = compileModuleImpl(
        ImportingInstance, ImportLoc, Module->getTopLevelModuleName(),
        FrontendInputFile(FakeModuleMapFile, IK, +Module->IsSystem),
        ModMap.getModuleMapFileForUniquing(Module)->getName(),
        ModuleFileName,
        [&](CompilerInstance &Instance) {
      std::unique_ptr<llvm::MemoryBuffer> ModuleMapBuffer =
          llvm::MemoryBuffer::getMemBuffer(InferredModuleMapContent);
      FileEntryRef ModuleMapFile = Instance.getFileManager().getVirtualFileRef(
          FakeModuleMapFile, InferredModuleMapContent.size(), 0);
      Instance.getSourceManager().overrideFileContents(
          ModuleMapFile, std::move(ModuleMapBuffer));
    });
  }

  // We've rebuilt a module. If we're allowed to generate or update the global
  // module index, record that fact in the importing compiler instance.
  if (ImportingInstance.getFrontendOpts().GenerateGlobalModuleIndex) {
    ImportingInstance.setBuildGlobalModuleIndex(true);
  }

  return Result;
}

/// Read the AST right after compiling the module.
static bool readASTAfterCompileModule(CompilerInstance &ImportingInstance,
                                      SourceLocation ImportLoc,
                                      SourceLocation ModuleNameLoc,
                                      Module *Module, StringRef ModuleFileName,
                                      bool *OutOfDate) {
  DiagnosticsEngine &Diags = ImportingInstance.getDiagnostics();

  unsigned ModuleLoadCapabilities = ASTReader::ARR_Missing;
  if (OutOfDate)
    ModuleLoadCapabilities |= ASTReader::ARR_OutOfDate;

  // Try to read the module file, now that we've compiled it.
  ASTReader::ASTReadResult ReadResult =
      ImportingInstance.getASTReader()->ReadAST(
          ModuleFileName, serialization::MK_ImplicitModule, ImportLoc,
          ModuleLoadCapabilities);
  if (ReadResult == ASTReader::Success)
    return true;

  // The caller wants to handle out-of-date failures.
  if (OutOfDate && ReadResult == ASTReader::OutOfDate) {
    *OutOfDate = true;
    return false;
  }

  // The ASTReader didn't diagnose the error, so conservatively report it.
  if (ReadResult == ASTReader::Missing || !Diags.hasErrorOccurred())
    Diags.Report(ModuleNameLoc, diag::err_module_not_built)
      << Module->Name << SourceRange(ImportLoc, ModuleNameLoc);

  return false;
}

/// Compile a module in a separate compiler instance and read the AST,
/// returning true if the module compiles without errors.
static bool compileModuleAndReadASTImpl(CompilerInstance &ImportingInstance,
                                        SourceLocation ImportLoc,
                                        SourceLocation ModuleNameLoc,
                                        Module *Module,
                                        StringRef ModuleFileName) {
  if (!compileModule(ImportingInstance, ModuleNameLoc, Module,
                     ModuleFileName)) {
    ImportingInstance.getDiagnostics().Report(ModuleNameLoc,
                                              diag::err_module_not_built)
        << Module->Name << SourceRange(ImportLoc, ModuleNameLoc);
    return false;
  }

  return readASTAfterCompileModule(ImportingInstance, ImportLoc, ModuleNameLoc,
                                   Module, ModuleFileName,
                                   /*OutOfDate=*/nullptr);
}

/// Compile a module in a separate compiler instance and read the AST,
/// returning true if the module compiles without errors, using a lock manager
/// to avoid building the same module in multiple compiler instances.
///
/// Uses a lock file manager and exponential backoff to reduce the chances that
/// multiple instances will compete to create the same module.  On timeout,
/// deletes the lock file in order to avoid deadlock from crashing processes or
/// bugs in the lock file manager.
static bool compileModuleAndReadASTBehindLock(
    CompilerInstance &ImportingInstance, SourceLocation ImportLoc,
    SourceLocation ModuleNameLoc, Module *Module, StringRef ModuleFileName) {
  DiagnosticsEngine &Diags = ImportingInstance.getDiagnostics();

  Diags.Report(ModuleNameLoc, diag::remark_module_lock)
      << ModuleFileName << Module->Name;

  // FIXME: have LockFileManager return an error_code so that we can
  // avoid the mkdir when the directory already exists.
  StringRef Dir = llvm::sys::path::parent_path(ModuleFileName);
  llvm::sys::fs::create_directories(Dir);

  while (true) {
    llvm::LockFileManager Locked(ModuleFileName);
    switch (Locked) {
    case llvm::LockFileManager::LFS_Error:
      // ModuleCache takes care of correctness and locks are only necessary for
      // performance. Fallback to building the module in case of any lock
      // related errors.
      Diags.Report(ModuleNameLoc, diag::remark_module_lock_failure)
          << Module->Name << Locked.getErrorMessage();
      // Clear out any potential leftover.
      Locked.unsafeRemoveLockFile();
      [[fallthrough]];
    case llvm::LockFileManager::LFS_Owned:
      // We're responsible for building the module ourselves.
      return compileModuleAndReadASTImpl(ImportingInstance, ImportLoc,
                                         ModuleNameLoc, Module, ModuleFileName);

    case llvm::LockFileManager::LFS_Shared:
      break; // The interesting case.
    }

    // Someone else is responsible for building the module. Wait for them to
    // finish.
    switch (Locked.waitForUnlock()) {
    case llvm::LockFileManager::Res_Success:
      break; // The interesting case.
    case llvm::LockFileManager::Res_OwnerDied:
      continue; // try again to get the lock.
    case llvm::LockFileManager::Res_Timeout:
      // Since ModuleCache takes care of correctness, we try waiting for
      // another process to complete the build so clang does not do it done
      // twice. If case of timeout, build it ourselves.
      Diags.Report(ModuleNameLoc, diag::remark_module_lock_timeout)
          << Module->Name;
      // Clear the lock file so that future invocations can make progress.
      Locked.unsafeRemoveLockFile();
      continue;
    }

    // Read the module that was just written by someone else.
    bool OutOfDate = false;
    if (readASTAfterCompileModule(ImportingInstance, ImportLoc, ModuleNameLoc,
                                  Module, ModuleFileName, &OutOfDate))
      return true;
    if (!OutOfDate)
      return false;

    // The module may be out of date in the presence of file system races,
    // or if one of its imports depends on header search paths that are not
    // consistent with this ImportingInstance.  Try again...
  }
}

/// Compile a module in a separate compiler instance and read the AST,
/// returning true if the module compiles without errors, potentially using a
/// lock manager to avoid building the same module in multiple compiler
/// instances.
static bool compileModuleAndReadAST(CompilerInstance &ImportingInstance,
                                    SourceLocation ImportLoc,
                                    SourceLocation ModuleNameLoc,
                                    Module *Module, StringRef ModuleFileName) {
  return ImportingInstance.getInvocation()
                 .getFrontendOpts()
                 .BuildingImplicitModuleUsesLock
             ? compileModuleAndReadASTBehindLock(ImportingInstance, ImportLoc,
                                                 ModuleNameLoc, Module,
                                                 ModuleFileName)
             : compileModuleAndReadASTImpl(ImportingInstance, ImportLoc,
                                           ModuleNameLoc, Module,
                                           ModuleFileName);
}

/// Diagnose differences between the current definition of the given
/// configuration macro and the definition provided on the command line.
static void checkConfigMacro(Preprocessor &PP, StringRef ConfigMacro,
                             Module *Mod, SourceLocation ImportLoc) {
  IdentifierInfo *Id = PP.getIdentifierInfo(ConfigMacro);
  SourceManager &SourceMgr = PP.getSourceManager();

  // If this identifier has never had a macro definition, then it could
  // not have changed.
  if (!Id->hadMacroDefinition())
    return;
  auto *LatestLocalMD = PP.getLocalMacroDirectiveHistory(Id);

  // Find the macro definition from the command line.
  MacroInfo *CmdLineDefinition = nullptr;
  for (auto *MD = LatestLocalMD; MD; MD = MD->getPrevious()) {
    // We only care about the predefines buffer.
    FileID FID = SourceMgr.getFileID(MD->getLocation());
    if (FID.isInvalid() || FID != PP.getPredefinesFileID())
      continue;
    if (auto *DMD = dyn_cast<DefMacroDirective>(MD))
      CmdLineDefinition = DMD->getMacroInfo();
    break;
  }

  auto *CurrentDefinition = PP.getMacroInfo(Id);
  if (CurrentDefinition == CmdLineDefinition) {
    // Macro matches. Nothing to do.
  } else if (!CurrentDefinition) {
    // This macro was defined on the command line, then #undef'd later.
    // Complain.
    PP.Diag(ImportLoc, diag::warn_module_config_macro_undef)
      << true << ConfigMacro << Mod->getFullModuleName();
    auto LatestDef = LatestLocalMD->getDefinition();
    assert(LatestDef.isUndefined() &&
           "predefined macro went away with no #undef?");
    PP.Diag(LatestDef.getUndefLocation(), diag::note_module_def_undef_here)
      << true;
    return;
  } else if (!CmdLineDefinition) {
    // There was no definition for this macro in the predefines buffer,
    // but there was a local definition. Complain.
    PP.Diag(ImportLoc, diag::warn_module_config_macro_undef)
      << false << ConfigMacro << Mod->getFullModuleName();
    PP.Diag(CurrentDefinition->getDefinitionLoc(),
            diag::note_module_def_undef_here)
      << false;
  } else if (!CurrentDefinition->isIdenticalTo(*CmdLineDefinition, PP,
                                               /*Syntactically=*/true)) {
    // The macro definitions differ.
    PP.Diag(ImportLoc, diag::warn_module_config_macro_undef)
      << false << ConfigMacro << Mod->getFullModuleName();
    PP.Diag(CurrentDefinition->getDefinitionLoc(),
            diag::note_module_def_undef_here)
      << false;
  }
}

static void checkConfigMacros(Preprocessor &PP, Module *M,
                              SourceLocation ImportLoc) {
  clang::Module *TopModule = M->getTopLevelModule();
  for (const StringRef ConMacro : TopModule->ConfigMacros) {
    checkConfigMacro(PP, ConMacro, M, ImportLoc);
  }
}

/// Write a new timestamp file with the given path.
static void writeTimestampFile(StringRef TimestampFile) {
  std::error_code EC;
  llvm::raw_fd_ostream Out(TimestampFile.str(), EC, llvm::sys::fs::OF_None);
}

/// Prune the module cache of modules that haven't been accessed in
/// a long time.
static void pruneModuleCache(const HeaderSearchOptions &HSOpts) {
  llvm::sys::fs::file_status StatBuf;
  llvm::SmallString<128> TimestampFile;
  TimestampFile = HSOpts.ModuleCachePath;
  assert(!TimestampFile.empty());
  llvm::sys::path::append(TimestampFile, "modules.timestamp");

  // Try to stat() the timestamp file.
  if (std::error_code EC = llvm::sys::fs::status(TimestampFile, StatBuf)) {
    // If the timestamp file wasn't there, create one now.
    if (EC == std::errc::no_such_file_or_directory) {
      writeTimestampFile(TimestampFile);
    }
    return;
  }

  // Check whether the time stamp is older than our pruning interval.
  // If not, do nothing.
  time_t TimeStampModTime =
      llvm::sys::toTimeT(StatBuf.getLastModificationTime());
  time_t CurrentTime = time(nullptr);
  if (CurrentTime - TimeStampModTime <= time_t(HSOpts.ModuleCachePruneInterval))
    return;

  // Write a new timestamp file so that nobody else attempts to prune.
  // There is a benign race condition here, if two Clang instances happen to
  // notice at the same time that the timestamp is out-of-date.
  writeTimestampFile(TimestampFile);

  // Walk the entire module cache, looking for unused module files and module
  // indices.
  std::error_code EC;
  SmallString<128> ModuleCachePathNative;
  llvm::sys::path::native(HSOpts.ModuleCachePath, ModuleCachePathNative);
  for (llvm::sys::fs::directory_iterator Dir(ModuleCachePathNative, EC), DirEnd;
       Dir != DirEnd && !EC; Dir.increment(EC)) {
    // If we don't have a directory, there's nothing to look into.
    if (!llvm::sys::fs::is_directory(Dir->path()))
      continue;

    // Walk all of the files within this directory.
    for (llvm::sys::fs::directory_iterator File(Dir->path(), EC), FileEnd;
         File != FileEnd && !EC; File.increment(EC)) {
      // We only care about module and global module index files.
      StringRef Extension = llvm::sys::path::extension(File->path());
      if (Extension != ".pcm" && Extension != ".timestamp" &&
          llvm::sys::path::filename(File->path()) != "modules.idx")
        continue;

      // Look at this file. If we can't stat it, there's nothing interesting
      // there.
      if (llvm::sys::fs::status(File->path(), StatBuf))
        continue;

      // If the file has been used recently enough, leave it there.
      time_t FileAccessTime = llvm::sys::toTimeT(StatBuf.getLastAccessedTime());
      if (CurrentTime - FileAccessTime <=
              time_t(HSOpts.ModuleCachePruneAfter)) {
        continue;
      }

      // Remove the file.
      llvm::sys::fs::remove(File->path());

      // Remove the timestamp file.
      std::string TimpestampFilename = File->path() + ".timestamp";
      llvm::sys::fs::remove(TimpestampFilename);
    }

    // If we removed all of the files in the directory, remove the directory
    // itself.
    if (llvm::sys::fs::directory_iterator(Dir->path(), EC) ==
            llvm::sys::fs::directory_iterator() && !EC)
      llvm::sys::fs::remove(Dir->path());
  }
}

void CompilerInstance::createASTReader() {
  if (TheASTReader)
    return;

  if (!hasASTContext())
    createASTContext();

  // If we're implicitly building modules but not currently recursively
  // building a module, check whether we need to prune the module cache.
  if (getSourceManager().getModuleBuildStack().empty() &&
      !getPreprocessor().getHeaderSearchInfo().getModuleCachePath().empty() &&
      getHeaderSearchOpts().ModuleCachePruneInterval > 0 &&
      getHeaderSearchOpts().ModuleCachePruneAfter > 0) {
    pruneModuleCache(getHeaderSearchOpts());
  }

  HeaderSearchOptions &HSOpts = getHeaderSearchOpts();
  std::string Sysroot = HSOpts.Sysroot;
  const PreprocessorOptions &PPOpts = getPreprocessorOpts();
  const FrontendOptions &FEOpts = getFrontendOpts();
  std::unique_ptr<llvm::Timer> ReadTimer;

  if (FrontendTimerGroup)
    ReadTimer = std::make_unique<llvm::Timer>("reading_modules",
                                                "Reading modules",
                                                *FrontendTimerGroup);
  TheASTReader = new ASTReader(
      getPreprocessor(), getModuleCache(), &getASTContext(),
      getPCHContainerReader(), getFrontendOpts().ModuleFileExtensions,
      Sysroot.empty() ? "" : Sysroot.c_str(),
      PPOpts.DisablePCHOrModuleValidation,
      /*AllowASTWithCompilerErrors=*/FEOpts.AllowPCMWithCompilerErrors,
      /*AllowConfigurationMismatch=*/false, HSOpts.ModulesValidateSystemHeaders,
      HSOpts.ValidateASTInputFilesContent,
      getFrontendOpts().UseGlobalModuleIndex, std::move(ReadTimer));
  if (hasASTConsumer()) {
    TheASTReader->setDeserializationListener(
        getASTConsumer().GetASTDeserializationListener());
    getASTContext().setASTMutationListener(
      getASTConsumer().GetASTMutationListener());
  }
  getASTContext().setExternalSource(TheASTReader);
  if (hasSema())
    TheASTReader->InitializeSema(getSema());
  if (hasASTConsumer())
    TheASTReader->StartTranslationUnit(&getASTConsumer());

  for (auto &Listener : DependencyCollectors)
    Listener->attachToASTReader(*TheASTReader);
}

bool CompilerInstance::loadModuleFile(
    StringRef FileName, serialization::ModuleFile *&LoadedModuleFile) {
  llvm::Timer Timer;
  if (FrontendTimerGroup)
    Timer.init("preloading." + FileName.str(), "Preloading " + FileName.str(),
               *FrontendTimerGroup);
  llvm::TimeRegion TimeLoading(FrontendTimerGroup ? &Timer : nullptr);

  // If we don't already have an ASTReader, create one now.
  if (!TheASTReader)
    createASTReader();

  // If -Wmodule-file-config-mismatch is mapped as an error or worse, allow the
  // ASTReader to diagnose it, since it can produce better errors that we can.
  bool ConfigMismatchIsRecoverable =
      getDiagnostics().getDiagnosticLevel(diag::warn_module_config_mismatch,
                                          SourceLocation())
        <= DiagnosticsEngine::Warning;

  auto Listener = std::make_unique<ReadModuleNames>(*PP);
  auto &ListenerRef = *Listener;
  ASTReader::ListenerScope ReadModuleNamesListener(*TheASTReader,
                                                   std::move(Listener));

  // Try to load the module file.
  switch (TheASTReader->ReadAST(
      FileName, serialization::MK_ExplicitModule, SourceLocation(),
      ConfigMismatchIsRecoverable ? ASTReader::ARR_ConfigurationMismatch : 0,
      &LoadedModuleFile)) {
  case ASTReader::Success:
    // We successfully loaded the module file; remember the set of provided
    // modules so that we don't try to load implicit modules for them.
    ListenerRef.registerAll();
    return true;

  case ASTReader::ConfigurationMismatch:
    // Ignore unusable module files.
    getDiagnostics().Report(SourceLocation(), diag::warn_module_config_mismatch)
        << FileName;
    // All modules provided by any files we tried and failed to load are now
    // unavailable; includes of those modules should now be handled textually.
    ListenerRef.markAllUnavailable();
    return true;

  default:
    return false;
  }
}

namespace {
enum ModuleSource {
  MS_ModuleNotFound,
  MS_ModuleCache,
  MS_PrebuiltModulePath,
  MS_ModuleBuildPragma
};
} // end namespace

/// Select a source for loading the named module and compute the filename to
/// load it from.
static ModuleSource selectModuleSource(
    Module *M, StringRef ModuleName, std::string &ModuleFilename,
    const std::map<std::string, std::string, std::less<>> &BuiltModules,
    HeaderSearch &HS) {
  assert(ModuleFilename.empty() && "Already has a module source?");

  // Check to see if the module has been built as part of this compilation
  // via a module build pragma.
  auto BuiltModuleIt = BuiltModules.find(ModuleName);
  if (BuiltModuleIt != BuiltModules.end()) {
    ModuleFilename = BuiltModuleIt->second;
    return MS_ModuleBuildPragma;
  }

  // Try to load the module from the prebuilt module path.
  const HeaderSearchOptions &HSOpts = HS.getHeaderSearchOpts();
  if (!HSOpts.PrebuiltModuleFiles.empty() ||
      !HSOpts.PrebuiltModulePaths.empty()) {
    ModuleFilename = HS.getPrebuiltModuleFileName(ModuleName);
    if (HSOpts.EnablePrebuiltImplicitModules && ModuleFilename.empty())
      ModuleFilename = HS.getPrebuiltImplicitModuleFileName(M);
    if (!ModuleFilename.empty())
      return MS_PrebuiltModulePath;
  }

  // Try to load the module from the module cache.
  if (M) {
    ModuleFilename = HS.getCachedModuleFileName(M);
    return MS_ModuleCache;
  }

  return MS_ModuleNotFound;
}

ModuleLoadResult CompilerInstance::findOrCompileModuleAndReadAST(
    StringRef ModuleName, SourceLocation ImportLoc,
    SourceLocation ModuleNameLoc, bool IsInclusionDirective) {
  // Search for a module with the given name.
  HeaderSearch &HS = PP->getHeaderSearchInfo();
  Module *M =
      HS.lookupModule(ModuleName, ImportLoc, true, !IsInclusionDirective);

  // Check for any configuration macros that have changed. This is done
  // immediately before potentially building a module in case this module
  // depends on having one of its configuration macros defined to successfully
  // build. If this is not done the user will never see the warning.
  if (M)
    checkConfigMacros(getPreprocessor(), M, ImportLoc);

  // Select the source and filename for loading the named module.
  std::string ModuleFilename;
  ModuleSource Source =
      selectModuleSource(M, ModuleName, ModuleFilename, BuiltModules, HS);
  if (Source == MS_ModuleNotFound) {
    // We can't find a module, error out here.
    getDiagnostics().Report(ModuleNameLoc, diag::err_module_not_found)
        << ModuleName << SourceRange(ImportLoc, ModuleNameLoc);
    return nullptr;
  }
  if (ModuleFilename.empty()) {
    if (M && M->HasIncompatibleModuleFile) {
      // We tried and failed to load a module file for this module. Fall
      // back to textual inclusion for its headers.
      return ModuleLoadResult::ConfigMismatch;
    }

    getDiagnostics().Report(ModuleNameLoc, diag::err_module_build_disabled)
        << ModuleName;
    return nullptr;
  }

  // Create an ASTReader on demand.
  if (!getASTReader())
    createASTReader();

  // Time how long it takes to load the module.
  llvm::Timer Timer;
  if (FrontendTimerGroup)
    Timer.init("loading." + ModuleFilename, "Loading " + ModuleFilename,
               *FrontendTimerGroup);
  llvm::TimeRegion TimeLoading(FrontendTimerGroup ? &Timer : nullptr);
  llvm::TimeTraceScope TimeScope("Module Load", ModuleName);

  // Try to load the module file. If we are not trying to load from the
  // module cache, we don't know how to rebuild modules.
  unsigned ARRFlags = Source == MS_ModuleCache
                          ? ASTReader::ARR_OutOfDate | ASTReader::ARR_Missing |
                                ASTReader::ARR_TreatModuleWithErrorsAsOutOfDate
                          : Source == MS_PrebuiltModulePath
                                ? 0
                                : ASTReader::ARR_ConfigurationMismatch;
  switch (getASTReader()->ReadAST(ModuleFilename,
                                  Source == MS_PrebuiltModulePath
                                      ? serialization::MK_PrebuiltModule
                                      : Source == MS_ModuleBuildPragma
                                            ? serialization::MK_ExplicitModule
                                            : serialization::MK_ImplicitModule,
                                  ImportLoc, ARRFlags)) {
  case ASTReader::Success: {
    if (M)
      return M;
    assert(Source != MS_ModuleCache &&
           "missing module, but file loaded from cache");

    // A prebuilt module is indexed as a ModuleFile; the Module does not exist
    // until the first call to ReadAST.  Look it up now.
    M = HS.lookupModule(ModuleName, ImportLoc, true, !IsInclusionDirective);

    // Check whether M refers to the file in the prebuilt module path.
    if (M && M->getASTFile())
      if (auto ModuleFile = FileMgr->getFile(ModuleFilename))
        if (*ModuleFile == M->getASTFile())
          return M;

    getDiagnostics().Report(ModuleNameLoc, diag::err_module_prebuilt)
        << ModuleName;
    return ModuleLoadResult();
  }

  case ASTReader::OutOfDate:
  case ASTReader::Missing:
    // The most interesting case.
    break;

  case ASTReader::ConfigurationMismatch:
    if (Source == MS_PrebuiltModulePath)
      // FIXME: We shouldn't be setting HadFatalFailure below if we only
      // produce a warning here!
      getDiagnostics().Report(SourceLocation(),
                              diag::warn_module_config_mismatch)
          << ModuleFilename;
    // Fall through to error out.
    [[fallthrough]];
  case ASTReader::VersionMismatch:
  case ASTReader::HadErrors:
    ModuleLoader::HadFatalFailure = true;
    // FIXME: The ASTReader will already have complained, but can we shoehorn
    // that diagnostic information into a more useful form?
    return ModuleLoadResult();

  case ASTReader::Failure:
    ModuleLoader::HadFatalFailure = true;
    return ModuleLoadResult();
  }

  // ReadAST returned Missing or OutOfDate.
  if (Source != MS_ModuleCache) {
    // We don't know the desired configuration for this module and don't
    // necessarily even have a module map. Since ReadAST already produces
    // diagnostics for these two cases, we simply error out here.
    return ModuleLoadResult();
  }

  // The module file is missing or out-of-date. Build it.
  assert(M && "missing module, but trying to compile for cache");

  // Check whether there is a cycle in the module graph.
  ModuleBuildStack ModPath = getSourceManager().getModuleBuildStack();
  ModuleBuildStack::iterator Pos = ModPath.begin(), PosEnd = ModPath.end();
  for (; Pos != PosEnd; ++Pos) {
    if (Pos->first == ModuleName)
      break;
  }

  if (Pos != PosEnd) {
    SmallString<256> CyclePath;
    for (; Pos != PosEnd; ++Pos) {
      CyclePath += Pos->first;
      CyclePath += " -> ";
    }
    CyclePath += ModuleName;

    getDiagnostics().Report(ModuleNameLoc, diag::err_module_cycle)
        << ModuleName << CyclePath;
    return nullptr;
  }

  // Check whether we have already attempted to build this module (but failed).
  if (FailedModules && FailedModules->hasAlreadyFailed(ModuleName)) {
    getDiagnostics().Report(ModuleNameLoc, diag::err_module_not_built)
        << ModuleName << SourceRange(ImportLoc, ModuleNameLoc);
    return nullptr;
  }

  // Try to compile and then read the AST.
  if (!compileModuleAndReadAST(*this, ImportLoc, ModuleNameLoc, M,
                               ModuleFilename)) {
    assert(getDiagnostics().hasErrorOccurred() &&
           "undiagnosed error in compileModuleAndReadAST");
    if (FailedModules)
      FailedModules->addFailed(ModuleName);
    return nullptr;
  }

  // Okay, we've rebuilt and now loaded the module.
  return M;
}

ModuleLoadResult
CompilerInstance::loadModule(SourceLocation ImportLoc,
                             ModuleIdPath Path,
                             Module::NameVisibilityKind Visibility,
                             bool IsInclusionDirective) {
  // Determine what file we're searching from.
  StringRef ModuleName = Path[0].first->getName();
  SourceLocation ModuleNameLoc = Path[0].second;

  // If we've already handled this import, just return the cached result.
  // This one-element cache is important to eliminate redundant diagnostics
  // when both the preprocessor and parser see the same import declaration.
  if (ImportLoc.isValid() && LastModuleImportLoc == ImportLoc) {
    // Make the named module visible.
    if (LastModuleImportResult && ModuleName != getLangOpts().CurrentModule)
      TheASTReader->makeModuleVisible(LastModuleImportResult, Visibility,
                                      ImportLoc);
    return LastModuleImportResult;
  }

  // If we don't already have information on this module, load the module now.
  Module *Module = nullptr;
  ModuleMap &MM = getPreprocessor().getHeaderSearchInfo().getModuleMap();
  if (auto MaybeModule = MM.getCachedModuleLoad(*Path[0].first)) {
    // Use the cached result, which may be nullptr.
    Module = *MaybeModule;
    // Config macros are already checked before building a module, but they need
    // to be checked at each import location in case any of the config macros
    // have a new value at the current `ImportLoc`.
    if (Module)
      checkConfigMacros(getPreprocessor(), Module, ImportLoc);
  } else if (ModuleName == getLangOpts().CurrentModule) {
    // This is the module we're building.
    Module = PP->getHeaderSearchInfo().lookupModule(
        ModuleName, ImportLoc, /*AllowSearch*/ true,
        /*AllowExtraModuleMapSearch*/ !IsInclusionDirective);

    // Config macros do not need to be checked here for two reasons.
    // * This will always be textual inclusion, and thus the config macros
    //   actually do impact the content of the header.
    // * `Preprocessor::HandleHeaderIncludeOrImport` will never call this
    //   function as the `#include` or `#import` is textual.

    MM.cacheModuleLoad(*Path[0].first, Module);
  } else {
    ModuleLoadResult Result = findOrCompileModuleAndReadAST(
        ModuleName, ImportLoc, ModuleNameLoc, IsInclusionDirective);
    if (!Result.isNormal())
      return Result;
    if (!Result)
      DisableGeneratingGlobalModuleIndex = true;
    Module = Result;
    MM.cacheModuleLoad(*Path[0].first, Module);
  }

  // If we never found the module, fail.  Otherwise, verify the module and link
  // it up.
  if (!Module)
    return ModuleLoadResult();

  // Verify that the rest of the module path actually corresponds to
  // a submodule.
  bool MapPrivateSubModToTopLevel = false;
  for (unsigned I = 1, N = Path.size(); I != N; ++I) {
    StringRef Name = Path[I].first->getName();
    clang::Module *Sub = Module->findSubmodule(Name);

    // If the user is requesting Foo.Private and it doesn't exist, try to
    // match Foo_Private and emit a warning asking for the user to write
    // @import Foo_Private instead. FIXME: remove this when existing clients
    // migrate off of Foo.Private syntax.
    if (!Sub && Name == "Private" && Module == Module->getTopLevelModule()) {
      SmallString<128> PrivateModule(Module->Name);
      PrivateModule.append("_Private");

      SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> PrivPath;
      auto &II = PP->getIdentifierTable().get(
          PrivateModule, PP->getIdentifierInfo(Module->Name)->getTokenID());
      PrivPath.push_back(std::make_pair(&II, Path[0].second));

      std::string FileName;
      // If there is a modulemap module or prebuilt module, load it.
      if (PP->getHeaderSearchInfo().lookupModule(PrivateModule, ImportLoc, true,
                                                 !IsInclusionDirective) ||
          selectModuleSource(nullptr, PrivateModule, FileName, BuiltModules,
                             PP->getHeaderSearchInfo()) != MS_ModuleNotFound)
        Sub = loadModule(ImportLoc, PrivPath, Visibility, IsInclusionDirective);
      if (Sub) {
        MapPrivateSubModToTopLevel = true;
        PP->markClangModuleAsAffecting(Module);
        if (!getDiagnostics().isIgnored(
                diag::warn_no_priv_submodule_use_toplevel, ImportLoc)) {
          getDiagnostics().Report(Path[I].second,
                                  diag::warn_no_priv_submodule_use_toplevel)
              << Path[I].first << Module->getFullModuleName() << PrivateModule
              << SourceRange(Path[0].second, Path[I].second)
              << FixItHint::CreateReplacement(SourceRange(Path[0].second),
                                              PrivateModule);
          getDiagnostics().Report(Sub->DefinitionLoc,
                                  diag::note_private_top_level_defined);
        }
      }
    }

    if (!Sub) {
      // Attempt to perform typo correction to find a module name that works.
      SmallVector<StringRef, 2> Best;
      unsigned BestEditDistance = (std::numeric_limits<unsigned>::max)();

      for (class Module *SubModule : Module->submodules()) {
        unsigned ED =
            Name.edit_distance(SubModule->Name,
                               /*AllowReplacements=*/true, BestEditDistance);
        if (ED <= BestEditDistance) {
          if (ED < BestEditDistance) {
            Best.clear();
            BestEditDistance = ED;
          }

          Best.push_back(SubModule->Name);
        }
      }

      // If there was a clear winner, user it.
      if (Best.size() == 1) {
        getDiagnostics().Report(Path[I].second, diag::err_no_submodule_suggest)
            << Path[I].first << Module->getFullModuleName() << Best[0]
            << SourceRange(Path[0].second, Path[I - 1].second)
            << FixItHint::CreateReplacement(SourceRange(Path[I].second),
                                            Best[0]);

        Sub = Module->findSubmodule(Best[0]);
      }
    }

    if (!Sub) {
      // No submodule by this name. Complain, and don't look for further
      // submodules.
      getDiagnostics().Report(Path[I].second, diag::err_no_submodule)
          << Path[I].first << Module->getFullModuleName()
          << SourceRange(Path[0].second, Path[I - 1].second);
      break;
    }

    Module = Sub;
  }

  // Make the named module visible, if it's not already part of the module
  // we are parsing.
  if (ModuleName != getLangOpts().CurrentModule) {
    if (!Module->IsFromModuleFile && !MapPrivateSubModToTopLevel) {
      // We have an umbrella header or directory that doesn't actually include
      // all of the headers within the directory it covers. Complain about
      // this missing submodule and recover by forgetting that we ever saw
      // this submodule.
      // FIXME: Should we detect this at module load time? It seems fairly
      // expensive (and rare).
      getDiagnostics().Report(ImportLoc, diag::warn_missing_submodule)
        << Module->getFullModuleName()
        << SourceRange(Path.front().second, Path.back().second);

      return ModuleLoadResult(Module, ModuleLoadResult::MissingExpected);
    }

    // Check whether this module is available.
    if (Preprocessor::checkModuleIsAvailable(getLangOpts(), getTarget(),
                                             *Module, getDiagnostics())) {
      getDiagnostics().Report(ImportLoc, diag::note_module_import_here)
        << SourceRange(Path.front().second, Path.back().second);
      LastModuleImportLoc = ImportLoc;
      LastModuleImportResult = ModuleLoadResult();
      return ModuleLoadResult();
    }

    TheASTReader->makeModuleVisible(Module, Visibility, ImportLoc);
  }

  // Resolve any remaining module using export_as for this one.
  getPreprocessor()
      .getHeaderSearchInfo()
      .getModuleMap()
      .resolveLinkAsDependencies(Module->getTopLevelModule());

  LastModuleImportLoc = ImportLoc;
  LastModuleImportResult = ModuleLoadResult(Module);
  return LastModuleImportResult;
}

void CompilerInstance::createModuleFromSource(SourceLocation ImportLoc,
                                              StringRef ModuleName,
                                              StringRef Source) {
  // Avoid creating filenames with special characters.
  SmallString<128> CleanModuleName(ModuleName);
  for (auto &C : CleanModuleName)
    if (!isAlphanumeric(C))
      C = '_';

  // FIXME: Using a randomized filename here means that our intermediate .pcm
  // output is nondeterministic (as .pcm files refer to each other by name).
  // Can this affect the output in any way?
  SmallString<128> ModuleFileName;
  if (std::error_code EC = llvm::sys::fs::createTemporaryFile(
          CleanModuleName, "pcm", ModuleFileName)) {
    getDiagnostics().Report(ImportLoc, diag::err_fe_unable_to_open_output)
        << ModuleFileName << EC.message();
    return;
  }
  std::string ModuleMapFileName = (CleanModuleName + ".map").str();

  FrontendInputFile Input(
      ModuleMapFileName,
      InputKind(getLanguageFromOptions(Invocation->getLangOpts()),
                InputKind::ModuleMap, /*Preprocessed*/true));

  std::string NullTerminatedSource(Source.str());

  auto PreBuildStep = [&](CompilerInstance &Other) {
    // Create a virtual file containing our desired source.
    // FIXME: We shouldn't need to do this.
    FileEntryRef ModuleMapFile = Other.getFileManager().getVirtualFileRef(
        ModuleMapFileName, NullTerminatedSource.size(), 0);
    Other.getSourceManager().overrideFileContents(
        ModuleMapFile, llvm::MemoryBuffer::getMemBuffer(NullTerminatedSource));

    Other.BuiltModules = std::move(BuiltModules);
    Other.DeleteBuiltModules = false;
  };

  auto PostBuildStep = [this](CompilerInstance &Other) {
    BuiltModules = std::move(Other.BuiltModules);
  };

  // Build the module, inheriting any modules that we've built locally.
  if (compileModuleImpl(*this, ImportLoc, ModuleName, Input, StringRef(),
                        ModuleFileName, PreBuildStep, PostBuildStep)) {
    BuiltModules[std::string(ModuleName)] = std::string(ModuleFileName);
    llvm::sys::RemoveFileOnSignal(ModuleFileName);
  }
}

void CompilerInstance::makeModuleVisible(Module *Mod,
                                         Module::NameVisibilityKind Visibility,
                                         SourceLocation ImportLoc) {
  if (!TheASTReader)
    createASTReader();
  if (!TheASTReader)
    return;

  TheASTReader->makeModuleVisible(Mod, Visibility, ImportLoc);
}

GlobalModuleIndex *CompilerInstance::loadGlobalModuleIndex(
    SourceLocation TriggerLoc) {
  if (getPreprocessor().getHeaderSearchInfo().getModuleCachePath().empty())
    return nullptr;
  if (!TheASTReader)
    createASTReader();
  // Can't do anything if we don't have the module manager.
  if (!TheASTReader)
    return nullptr;
  // Get an existing global index.  This loads it if not already
  // loaded.
  TheASTReader->loadGlobalIndex();
  GlobalModuleIndex *GlobalIndex = TheASTReader->getGlobalIndex();
  // If the global index doesn't exist, create it.
  if (!GlobalIndex && shouldBuildGlobalModuleIndex() && hasFileManager() &&
      hasPreprocessor()) {
    llvm::sys::fs::create_directories(
      getPreprocessor().getHeaderSearchInfo().getModuleCachePath());
    if (llvm::Error Err = GlobalModuleIndex::writeIndex(
            getFileManager(), getPCHContainerReader(),
            getPreprocessor().getHeaderSearchInfo().getModuleCachePath())) {
      // FIXME this drops the error on the floor. This code is only used for
      // typo correction and drops more than just this one source of errors
      // (such as the directory creation failure above). It should handle the
      // error.
      consumeError(std::move(Err));
      return nullptr;
    }
    TheASTReader->resetForReload();
    TheASTReader->loadGlobalIndex();
    GlobalIndex = TheASTReader->getGlobalIndex();
  }
  // For finding modules needing to be imported for fixit messages,
  // we need to make the global index cover all modules, so we do that here.
  if (!HaveFullGlobalModuleIndex && GlobalIndex && !buildingModule()) {
    ModuleMap &MMap = getPreprocessor().getHeaderSearchInfo().getModuleMap();
    bool RecreateIndex = false;
    for (ModuleMap::module_iterator I = MMap.module_begin(),
        E = MMap.module_end(); I != E; ++I) {
      Module *TheModule = I->second;
      OptionalFileEntryRef Entry = TheModule->getASTFile();
      if (!Entry) {
        SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
        Path.push_back(std::make_pair(
            getPreprocessor().getIdentifierInfo(TheModule->Name), TriggerLoc));
        std::reverse(Path.begin(), Path.end());
        // Load a module as hidden.  This also adds it to the global index.
        loadModule(TheModule->DefinitionLoc, Path, Module::Hidden, false);
        RecreateIndex = true;
      }
    }
    if (RecreateIndex) {
      if (llvm::Error Err = GlobalModuleIndex::writeIndex(
              getFileManager(), getPCHContainerReader(),
              getPreprocessor().getHeaderSearchInfo().getModuleCachePath())) {
        // FIXME As above, this drops the error on the floor.
        consumeError(std::move(Err));
        return nullptr;
      }
      TheASTReader->resetForReload();
      TheASTReader->loadGlobalIndex();
      GlobalIndex = TheASTReader->getGlobalIndex();
    }
    HaveFullGlobalModuleIndex = true;
  }
  return GlobalIndex;
}

// Check global module index for missing imports.
bool
CompilerInstance::lookupMissingImports(StringRef Name,
                                       SourceLocation TriggerLoc) {
  // Look for the symbol in non-imported modules, but only if an error
  // actually occurred.
  if (!buildingModule()) {
    // Load global module index, or retrieve a previously loaded one.
    GlobalModuleIndex *GlobalIndex = loadGlobalModuleIndex(
      TriggerLoc);

    // Only if we have a global index.
    if (GlobalIndex) {
      GlobalModuleIndex::HitSet FoundModules;

      // Find the modules that reference the identifier.
      // Note that this only finds top-level modules.
      // We'll let diagnoseTypo find the actual declaration module.
      if (GlobalIndex->lookupIdentifier(Name, FoundModules))
        return true;
    }
  }

  return false;
}
void CompilerInstance::resetAndLeakSema() { llvm::BuryPointer(takeSema()); }

void CompilerInstance::setExternalSemaSource(
    IntrusiveRefCntPtr<ExternalSemaSource> ESS) {
  ExternalSemaSrc = std::move(ESS);
}
