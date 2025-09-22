//===- Indexing.cpp - Higher level API functions --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIndexDiagnostic.h"
#include "CIndexer.h"
#include "CLog.h"
#include "CXCursor.h"
#include "CXIndexDataConsumer.h"
#include "CXSourceLocation.h"
#include "CXString.h"
#include "CXTranslationUnit.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/Utils.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/PPConditionalDirectiveRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstdio>
#include <mutex>
#include <utility>

using namespace clang;
using namespace clang::index;
using namespace cxtu;
using namespace cxindex;

namespace {

//===----------------------------------------------------------------------===//
// Skip Parsed Bodies
//===----------------------------------------------------------------------===//

/// A "region" in source code identified by the file/offset of the
/// preprocessor conditional directive that it belongs to.
/// Multiple, non-consecutive ranges can be parts of the same region.
///
/// As an example of different regions separated by preprocessor directives:
///
/// \code
///   #1
/// #ifdef BLAH
///   #2
/// #ifdef CAKE
///   #3
/// #endif
///   #2
/// #endif
///   #1
/// \endcode
///
/// There are 3 regions, with non-consecutive parts:
///   #1 is identified as the beginning of the file
///   #2 is identified as the location of "#ifdef BLAH"
///   #3 is identified as the location of "#ifdef CAKE"
///
class PPRegion {
  llvm::sys::fs::UniqueID UniqueID;
  time_t ModTime;
  unsigned Offset;
public:
  PPRegion() : UniqueID(0, 0), ModTime(), Offset() {}
  PPRegion(llvm::sys::fs::UniqueID UniqueID, unsigned offset, time_t modTime)
      : UniqueID(UniqueID), ModTime(modTime), Offset(offset) {}

  const llvm::sys::fs::UniqueID &getUniqueID() const { return UniqueID; }
  unsigned getOffset() const { return Offset; }
  time_t getModTime() const { return ModTime; }

  bool isInvalid() const { return *this == PPRegion(); }

  friend bool operator==(const PPRegion &lhs, const PPRegion &rhs) {
    return lhs.UniqueID == rhs.UniqueID && lhs.Offset == rhs.Offset &&
           lhs.ModTime == rhs.ModTime;
  }
};

} // end anonymous namespace

namespace llvm {

  template <>
  struct DenseMapInfo<PPRegion> {
    static inline PPRegion getEmptyKey() {
      return PPRegion(llvm::sys::fs::UniqueID(0, 0), unsigned(-1), 0);
    }
    static inline PPRegion getTombstoneKey() {
      return PPRegion(llvm::sys::fs::UniqueID(0, 0), unsigned(-2), 0);
    }

    static unsigned getHashValue(const PPRegion &S) {
      llvm::FoldingSetNodeID ID;
      const llvm::sys::fs::UniqueID &UniqueID = S.getUniqueID();
      ID.AddInteger(UniqueID.getFile());
      ID.AddInteger(UniqueID.getDevice());
      ID.AddInteger(S.getOffset());
      ID.AddInteger(S.getModTime());
      return ID.ComputeHash();
    }

    static bool isEqual(const PPRegion &LHS, const PPRegion &RHS) {
      return LHS == RHS;
    }
  };
}

namespace {

/// Keeps track of function bodies that have already been parsed.
///
/// Is thread-safe.
class ThreadSafeParsedRegions {
  mutable std::mutex Mutex;
  llvm::DenseSet<PPRegion> ParsedRegions;

public:
  ~ThreadSafeParsedRegions() = default;

  llvm::DenseSet<PPRegion> getParsedRegions() const {
    std::lock_guard<std::mutex> MG(Mutex);
    return ParsedRegions;
  }

  void addParsedRegions(ArrayRef<PPRegion> Regions) {
    std::lock_guard<std::mutex> MG(Mutex);
    ParsedRegions.insert(Regions.begin(), Regions.end());
  }
};

/// Provides information whether source locations have already been parsed in
/// another FrontendAction.
///
/// Is NOT thread-safe.
class ParsedSrcLocationsTracker {
  ThreadSafeParsedRegions &ParsedRegionsStorage;
  PPConditionalDirectiveRecord &PPRec;
  Preprocessor &PP;

  /// Snapshot of the shared state at the point when this instance was
  /// constructed.
  llvm::DenseSet<PPRegion> ParsedRegionsSnapshot;
  /// Regions that were queried during this instance lifetime.
  SmallVector<PPRegion, 32> NewParsedRegions;

  /// Caching the last queried region.
  PPRegion LastRegion;
  bool LastIsParsed;

public:
  /// Creates snapshot of \p ParsedRegionsStorage.
  ParsedSrcLocationsTracker(ThreadSafeParsedRegions &ParsedRegionsStorage,
                            PPConditionalDirectiveRecord &ppRec,
                            Preprocessor &pp)
      : ParsedRegionsStorage(ParsedRegionsStorage), PPRec(ppRec), PP(pp),
        ParsedRegionsSnapshot(ParsedRegionsStorage.getParsedRegions()) {}

  /// \returns true iff \p Loc has already been parsed.
  ///
  /// Can provide false-negative in case the location was parsed after this
  /// instance had been constructed.
  bool hasAlredyBeenParsed(SourceLocation Loc, FileID FID, FileEntryRef FE) {
    PPRegion region = getRegion(Loc, FID, FE);
    if (region.isInvalid())
      return false;

    // Check common case, consecutive functions in the same region.
    if (LastRegion == region)
      return LastIsParsed;

    LastRegion = region;
    // Source locations can't be revisited during single TU parsing.
    // That means if we hit the same region again, it's a different location in
    // the same region and so the "is parsed" value from the snapshot is still
    // correct.
    LastIsParsed = ParsedRegionsSnapshot.count(region);
    if (!LastIsParsed)
      NewParsedRegions.emplace_back(std::move(region));
    return LastIsParsed;
  }

  /// Updates ParsedRegionsStorage with newly parsed regions.
  void syncWithStorage() {
    ParsedRegionsStorage.addParsedRegions(NewParsedRegions);
  }

private:
  PPRegion getRegion(SourceLocation Loc, FileID FID, FileEntryRef FE) {
    auto Bail = [this, FE]() {
      if (isParsedOnceInclude(FE)) {
        const llvm::sys::fs::UniqueID &ID = FE.getUniqueID();
        return PPRegion(ID, 0, FE.getModificationTime());
      }
      return PPRegion();
    };

    SourceLocation RegionLoc = PPRec.findConditionalDirectiveRegionLoc(Loc);
    assert(RegionLoc.isFileID());
    if (RegionLoc.isInvalid())
      return Bail();

    FileID RegionFID;
    unsigned RegionOffset;
    std::tie(RegionFID, RegionOffset) =
        PPRec.getSourceManager().getDecomposedLoc(RegionLoc);

    if (RegionFID != FID)
      return Bail();

    const llvm::sys::fs::UniqueID &ID = FE.getUniqueID();
    return PPRegion(ID, RegionOffset, FE.getModificationTime());
  }

  bool isParsedOnceInclude(FileEntryRef FE) {
    return PP.getHeaderSearchInfo().isFileMultipleIncludeGuarded(FE) ||
           PP.getHeaderSearchInfo().hasFileBeenImported(FE);
  }
};

//===----------------------------------------------------------------------===//
// IndexPPCallbacks
//===----------------------------------------------------------------------===//

class IndexPPCallbacks : public PPCallbacks {
  Preprocessor &PP;
  CXIndexDataConsumer &DataConsumer;
  bool IsMainFileEntered;

public:
  IndexPPCallbacks(Preprocessor &PP, CXIndexDataConsumer &dataConsumer)
    : PP(PP), DataConsumer(dataConsumer), IsMainFileEntered(false) { }

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                 SrcMgr::CharacteristicKind FileType, FileID PrevFID) override {
    if (IsMainFileEntered)
      return;

    SourceManager &SM = PP.getSourceManager();
    SourceLocation MainFileLoc = SM.getLocForStartOfFile(SM.getMainFileID());

    if (Loc == MainFileLoc && Reason == PPCallbacks::EnterFile) {
      IsMainFileEntered = true;
      DataConsumer.enteredMainFile(
          *SM.getFileEntryRefForID(SM.getMainFileID()));
    }
  }

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override {
    bool isImport = (IncludeTok.is(tok::identifier) &&
            IncludeTok.getIdentifierInfo()->getPPKeywordID() == tok::pp_import);
    DataConsumer.ppIncludedFile(HashLoc, FileName, File, isImport, IsAngled,
                                ModuleImported);
  }

  /// MacroDefined - This hook is called whenever a macro definition is seen.
  void MacroDefined(const Token &Id, const MacroDirective *MD) override {}

  /// MacroUndefined - This hook is called whenever a macro #undef is seen.
  /// MI is released immediately following this callback.
  void MacroUndefined(const Token &MacroNameTok,
                      const MacroDefinition &MD,
                      const MacroDirective *UD) override {}

  /// MacroExpands - This is called by when a macro invocation is found.
  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {}

  /// SourceRangeSkipped - This hook is called when a source range is skipped.
  /// \param Range The SourceRange that was skipped. The range begins at the
  /// #if/#else directive and ends after the #endif/#else directive.
  void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) override {
  }
};

//===----------------------------------------------------------------------===//
// IndexingConsumer
//===----------------------------------------------------------------------===//

class IndexingConsumer : public ASTConsumer {
  CXIndexDataConsumer &DataConsumer;

public:
  IndexingConsumer(CXIndexDataConsumer &dataConsumer,
                   ParsedSrcLocationsTracker *parsedLocsTracker)
      : DataConsumer(dataConsumer) {}

  void Initialize(ASTContext &Context) override {
    DataConsumer.setASTContext(Context);
    DataConsumer.startedTranslationUnit();
  }

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    return !DataConsumer.shouldAbort();
  }
};

//===----------------------------------------------------------------------===//
// CaptureDiagnosticConsumer
//===----------------------------------------------------------------------===//

class CaptureDiagnosticConsumer : public DiagnosticConsumer {
  SmallVector<StoredDiagnostic, 4> Errors;
public:

  void HandleDiagnostic(DiagnosticsEngine::Level level,
                        const Diagnostic &Info) override {
    if (level >= DiagnosticsEngine::Error)
      Errors.push_back(StoredDiagnostic(level, Info));
  }
};

//===----------------------------------------------------------------------===//
// IndexingFrontendAction
//===----------------------------------------------------------------------===//

class IndexingFrontendAction : public ASTFrontendAction {
  std::shared_ptr<CXIndexDataConsumer> DataConsumer;
  IndexingOptions Opts;

  ThreadSafeParsedRegions *SKData;
  std::unique_ptr<ParsedSrcLocationsTracker> ParsedLocsTracker;

public:
  IndexingFrontendAction(std::shared_ptr<CXIndexDataConsumer> dataConsumer,
                         const IndexingOptions &Opts,
                         ThreadSafeParsedRegions *skData)
      : DataConsumer(std::move(dataConsumer)), Opts(Opts), SKData(skData) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();

    if (!PPOpts.ImplicitPCHInclude.empty()) {
      if (auto File =
              CI.getFileManager().getOptionalFileRef(PPOpts.ImplicitPCHInclude))
        DataConsumer->importedPCH(*File);
    }

    DataConsumer->setASTContext(CI.getASTContext());
    Preprocessor &PP = CI.getPreprocessor();
    PP.addPPCallbacks(std::make_unique<IndexPPCallbacks>(PP, *DataConsumer));
    DataConsumer->setPreprocessor(CI.getPreprocessorPtr());

    if (SKData) {
      auto *PPRec = new PPConditionalDirectiveRecord(PP.getSourceManager());
      PP.addPPCallbacks(std::unique_ptr<PPCallbacks>(PPRec));
      ParsedLocsTracker =
          std::make_unique<ParsedSrcLocationsTracker>(*SKData, *PPRec, PP);
    }

    std::vector<std::unique_ptr<ASTConsumer>> Consumers;
    Consumers.push_back(std::make_unique<IndexingConsumer>(
        *DataConsumer, ParsedLocsTracker.get()));
    Consumers.push_back(createIndexingASTConsumer(
        DataConsumer, Opts, CI.getPreprocessorPtr(),
        [this](const Decl *D) { return this->shouldSkipFunctionBody(D); }));
    return std::make_unique<MultiplexConsumer>(std::move(Consumers));
  }

  bool shouldSkipFunctionBody(const Decl *D) {
    if (!ParsedLocsTracker) {
      // Always skip bodies.
      return true;
    }

    const SourceManager &SM = D->getASTContext().getSourceManager();
    SourceLocation Loc = D->getLocation();
    if (Loc.isMacroID())
      return false;
    if (SM.isInSystemHeader(Loc))
      return true; // always skip bodies from system headers.

    FileID FID;
    unsigned Offset;
    std::tie(FID, Offset) = SM.getDecomposedLoc(Loc);
    // Don't skip bodies from main files; this may be revisited.
    if (SM.getMainFileID() == FID)
      return false;
    OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID);
    if (!FE)
      return false;

    return ParsedLocsTracker->hasAlredyBeenParsed(Loc, FID, *FE);
  }

  TranslationUnitKind getTranslationUnitKind() override {
    if (DataConsumer->shouldIndexImplicitTemplateInsts())
      return TU_Complete;
    else
      return TU_Prefix;
  }
  bool hasCodeCompletionSupport() const override { return false; }

  void EndSourceFileAction() override {
    if (ParsedLocsTracker)
      ParsedLocsTracker->syncWithStorage();
  }
};

//===----------------------------------------------------------------------===//
// clang_indexSourceFileUnit Implementation
//===----------------------------------------------------------------------===//

static IndexingOptions getIndexingOptionsFromCXOptions(unsigned index_options) {
  IndexingOptions IdxOpts;
  if (index_options & CXIndexOpt_IndexFunctionLocalSymbols)
    IdxOpts.IndexFunctionLocals = true;
  if (index_options & CXIndexOpt_IndexImplicitTemplateInstantiations)
    IdxOpts.IndexImplicitInstantiation = true;
  return IdxOpts;
}

struct IndexSessionData {
  CXIndex CIdx;
  std::unique_ptr<ThreadSafeParsedRegions> SkipBodyData =
      std::make_unique<ThreadSafeParsedRegions>();

  explicit IndexSessionData(CXIndex cIdx) : CIdx(cIdx) {}
};

} // anonymous namespace

static CXErrorCode clang_indexSourceFile_Impl(
    CXIndexAction cxIdxAction, CXClientData client_data,
    IndexerCallbacks *client_index_callbacks, unsigned index_callbacks_size,
    unsigned index_options, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    ArrayRef<CXUnsavedFile> unsaved_files, CXTranslationUnit *out_TU,
    unsigned TU_options) {
  if (out_TU)
    *out_TU = nullptr;
  bool requestedToGetTU = (out_TU != nullptr);

  if (!cxIdxAction) {
    return CXError_InvalidArguments;
  }
  if (!client_index_callbacks || index_callbacks_size == 0) {
    return CXError_InvalidArguments;
  }

  IndexerCallbacks CB;
  memset(&CB, 0, sizeof(CB));
  unsigned ClientCBSize = index_callbacks_size < sizeof(CB)
                                  ? index_callbacks_size : sizeof(CB);
  memcpy(&CB, client_index_callbacks, ClientCBSize);

  IndexSessionData *IdxSession = static_cast<IndexSessionData *>(cxIdxAction);
  CIndexer *CXXIdx = static_cast<CIndexer *>(IdxSession->CIdx);

  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForIndexing))
    setThreadBackgroundPriority();

  CaptureDiagsKind CaptureDiagnostics = CaptureDiagsKind::All;
  if (TU_options & CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles)
    CaptureDiagnostics = CaptureDiagsKind::AllWithoutNonErrorsFromIncludes;
  if (Logger::isLoggingEnabled())
    CaptureDiagnostics = CaptureDiagsKind::None;

  CaptureDiagnosticConsumer *CaptureDiag = nullptr;
  if (CaptureDiagnostics != CaptureDiagsKind::None)
    CaptureDiag = new CaptureDiagnosticConsumer();

  // Configure the diagnostics.
  IntrusiveRefCntPtr<DiagnosticsEngine>
    Diags(CompilerInstance::createDiagnostics(new DiagnosticOptions,
                                              CaptureDiag,
                                              /*ShouldOwnClient=*/true));

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<DiagnosticsEngine,
    llvm::CrashRecoveryContextReleaseRefCleanup<DiagnosticsEngine> >
    DiagCleanup(Diags.get());

  std::unique_ptr<std::vector<const char *>> Args(
      new std::vector<const char *>());

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<std::vector<const char*> >
    ArgsCleanup(Args.get());
  
  Args->insert(Args->end(), command_line_args,
               command_line_args + num_command_line_args);

  // The 'source_filename' argument is optional.  If the caller does not
  // specify it then it is assumed that the source file is specified
  // in the actual argument list.
  // Put the source file after command_line_args otherwise if '-x' flag is
  // present it will be unused.
  if (source_filename)
    Args->push_back(source_filename);

  CreateInvocationOptions CIOpts;
  CIOpts.Diags = Diags;
  CIOpts.ProbePrecompiled = true; // FIXME: historical default. Needed?
  std::shared_ptr<CompilerInvocation> CInvok =
      createInvocation(*Args, std::move(CIOpts));

  if (!CInvok)
    return CXError_Failure;

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<
      std::shared_ptr<CompilerInvocation>,
      llvm::CrashRecoveryContextDestructorCleanup<
          std::shared_ptr<CompilerInvocation>>>
      CInvokCleanup(&CInvok);

  if (CInvok->getFrontendOpts().Inputs.empty())
    return CXError_Failure;

  typedef SmallVector<std::unique_ptr<llvm::MemoryBuffer>, 8> MemBufferOwner;
  std::unique_ptr<MemBufferOwner> BufOwner(new MemBufferOwner);

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<MemBufferOwner> BufOwnerCleanup(
      BufOwner.get());

  for (auto &UF : unsaved_files) {
    std::unique_ptr<llvm::MemoryBuffer> MB =
        llvm::MemoryBuffer::getMemBufferCopy(getContents(UF), UF.Filename);
    CInvok->getPreprocessorOpts().addRemappedFile(UF.Filename, MB.get());
    BufOwner->push_back(std::move(MB));
  }

  // Since libclang is primarily used by batch tools dealing with
  // (often very broken) source code, where spell-checking can have a
  // significant negative impact on performance (particularly when 
  // precompiled headers are involved), we disable it.
  CInvok->getLangOpts().SpellChecking = false;

  if (index_options & CXIndexOpt_SuppressWarnings)
    CInvok->getDiagnosticOpts().IgnoreWarnings = true;

  // Make sure to use the raw module format.
  CInvok->getHeaderSearchOpts().ModuleFormat = std::string(
      CXXIdx->getPCHContainerOperations()->getRawReader().getFormats().front());

  auto Unit = ASTUnit::create(CInvok, Diags, CaptureDiagnostics,
                              /*UserFilesAreVolatile=*/true);
  if (!Unit)
    return CXError_InvalidArguments;

  auto *UPtr = Unit.get();
  std::unique_ptr<CXTUOwner> CXTU(
      new CXTUOwner(MakeCXTranslationUnit(CXXIdx, std::move(Unit))));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<CXTUOwner>
    CXTUCleanup(CXTU.get());

  // Enable the skip-parsed-bodies optimization only for C++; this may be
  // revisited.
  bool SkipBodies = (index_options & CXIndexOpt_SkipParsedBodiesInSession) &&
      CInvok->getLangOpts().CPlusPlus;
  if (SkipBodies)
    CInvok->getFrontendOpts().SkipFunctionBodies = true;

  auto DataConsumer =
    std::make_shared<CXIndexDataConsumer>(client_data, CB, index_options,
                                          CXTU->getTU());
  auto IndexAction = std::make_unique<IndexingFrontendAction>(
      DataConsumer, getIndexingOptionsFromCXOptions(index_options),
      SkipBodies ? IdxSession->SkipBodyData.get() : nullptr);

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<FrontendAction>
    IndexActionCleanup(IndexAction.get());

  bool Persistent = requestedToGetTU;
  bool OnlyLocalDecls = false;
  bool PrecompilePreamble = false;
  bool CreatePreambleOnFirstParse = false;
  bool CacheCodeCompletionResults = false;
  PreprocessorOptions &PPOpts = CInvok->getPreprocessorOpts(); 
  PPOpts.AllowPCHWithCompilerErrors = true;

  if (requestedToGetTU) {
    OnlyLocalDecls = CXXIdx->getOnlyLocalDecls();
    PrecompilePreamble = TU_options & CXTranslationUnit_PrecompiledPreamble;
    CreatePreambleOnFirstParse =
        TU_options & CXTranslationUnit_CreatePreambleOnFirstParse;
    // FIXME: Add a flag for modules.
    CacheCodeCompletionResults
      = TU_options & CXTranslationUnit_CacheCompletionResults;
  }

  if (TU_options & CXTranslationUnit_DetailedPreprocessingRecord) {
    PPOpts.DetailedRecord = true;
  }

  if (!requestedToGetTU && !CInvok->getLangOpts().Modules)
    PPOpts.DetailedRecord = false;

  // Unless the user specified that they want the preamble on the first parse
  // set it up to be created on the first reparse. This makes the first parse
  // faster, trading for a slower (first) reparse.
  unsigned PrecompilePreambleAfterNParses =
      !PrecompilePreamble ? 0 : 2 - CreatePreambleOnFirstParse;
  DiagnosticErrorTrap DiagTrap(*Diags);
  bool Success = ASTUnit::LoadFromCompilerInvocationAction(
      std::move(CInvok), CXXIdx->getPCHContainerOperations(), Diags,
      IndexAction.get(), UPtr, Persistent, CXXIdx->getClangResourcesPath(),
      OnlyLocalDecls, CaptureDiagnostics, PrecompilePreambleAfterNParses,
      CacheCodeCompletionResults, /*UserFilesAreVolatile=*/true);
  if (DiagTrap.hasErrorOccurred() && CXXIdx->getDisplayDiagnostics())
    printDiagsToStderr(UPtr);

  if (isASTReadError(UPtr))
    return CXError_ASTReadError;

  if (!Success)
    return CXError_Failure;

  if (out_TU)
    *out_TU = CXTU->takeTU();

  return CXError_Success;
}

//===----------------------------------------------------------------------===//
// clang_indexTranslationUnit Implementation
//===----------------------------------------------------------------------===//

static void indexPreprocessingRecord(ASTUnit &Unit, CXIndexDataConsumer &IdxCtx) {
  Preprocessor &PP = Unit.getPreprocessor();
  if (!PP.getPreprocessingRecord())
    return;

  // FIXME: Only deserialize inclusion directives.

  bool isModuleFile = Unit.isModuleFile();
  for (PreprocessedEntity *PPE : Unit.getLocalPreprocessingEntities()) {
    if (InclusionDirective *ID = dyn_cast<InclusionDirective>(PPE)) {
      SourceLocation Loc = ID->getSourceRange().getBegin();
      // Modules have synthetic main files as input, give an invalid location
      // if the location points to such a file.
      if (isModuleFile && Unit.isInMainFileID(Loc))
        Loc = SourceLocation();
      IdxCtx.ppIncludedFile(Loc, ID->getFileName(),
                            ID->getFile(),
                            ID->getKind() == InclusionDirective::Import,
                            !ID->wasInQuotes(), ID->importedModule());
    }
  }
}

static CXErrorCode clang_indexTranslationUnit_Impl(
    CXIndexAction idxAction, CXClientData client_data,
    IndexerCallbacks *client_index_callbacks, unsigned index_callbacks_size,
    unsigned index_options, CXTranslationUnit TU) {
  // Check arguments.
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return CXError_InvalidArguments;
  }
  if (!client_index_callbacks || index_callbacks_size == 0) {
    return CXError_InvalidArguments;
  }

  CIndexer *CXXIdx = TU->CIdx;
  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForIndexing))
    setThreadBackgroundPriority();

  IndexerCallbacks CB;
  memset(&CB, 0, sizeof(CB));
  unsigned ClientCBSize = index_callbacks_size < sizeof(CB)
                                  ? index_callbacks_size : sizeof(CB);
  memcpy(&CB, client_index_callbacks, ClientCBSize);

  CXIndexDataConsumer DataConsumer(client_data, CB, index_options, TU);

  ASTUnit *Unit = cxtu::getASTUnit(TU);
  if (!Unit)
    return CXError_Failure;

  ASTUnit::ConcurrencyCheck Check(*Unit);

  if (OptionalFileEntryRef PCHFile = Unit->getPCHFile())
    DataConsumer.importedPCH(*PCHFile);

  FileManager &FileMgr = Unit->getFileManager();

  if (Unit->getOriginalSourceFileName().empty())
    DataConsumer.enteredMainFile(std::nullopt);
  else if (auto MainFile =
               FileMgr.getFileRef(Unit->getOriginalSourceFileName()))
    DataConsumer.enteredMainFile(*MainFile);
  else
    DataConsumer.enteredMainFile(std::nullopt);

  DataConsumer.setASTContext(Unit->getASTContext());
  DataConsumer.startedTranslationUnit();

  indexPreprocessingRecord(*Unit, DataConsumer);
  indexASTUnit(*Unit, DataConsumer, getIndexingOptionsFromCXOptions(index_options));
  DataConsumer.indexDiagnostics();

  return CXError_Success;
}

//===----------------------------------------------------------------------===//
// libclang public APIs.
//===----------------------------------------------------------------------===//

int clang_index_isEntityObjCContainerKind(CXIdxEntityKind K) {
  return CXIdxEntity_ObjCClass <= K && K <= CXIdxEntity_ObjCCategory;
}

const CXIdxObjCContainerDeclInfo *
clang_index_getObjCContainerDeclInfo(const CXIdxDeclInfo *DInfo) {
  if (!DInfo)
    return nullptr;

  const DeclInfo *DI = static_cast<const DeclInfo *>(DInfo);
  if (const ObjCContainerDeclInfo *
        ContInfo = dyn_cast<ObjCContainerDeclInfo>(DI))
    return &ContInfo->ObjCContDeclInfo;

  return nullptr;
}

const CXIdxObjCInterfaceDeclInfo *
clang_index_getObjCInterfaceDeclInfo(const CXIdxDeclInfo *DInfo) {
  if (!DInfo)
    return nullptr;

  const DeclInfo *DI = static_cast<const DeclInfo *>(DInfo);
  if (const ObjCInterfaceDeclInfo *
        InterInfo = dyn_cast<ObjCInterfaceDeclInfo>(DI))
    return &InterInfo->ObjCInterDeclInfo;

  return nullptr;
}

const CXIdxObjCCategoryDeclInfo *
clang_index_getObjCCategoryDeclInfo(const CXIdxDeclInfo *DInfo){
  if (!DInfo)
    return nullptr;

  const DeclInfo *DI = static_cast<const DeclInfo *>(DInfo);
  if (const ObjCCategoryDeclInfo *
        CatInfo = dyn_cast<ObjCCategoryDeclInfo>(DI))
    return &CatInfo->ObjCCatDeclInfo;

  return nullptr;
}

const CXIdxObjCProtocolRefListInfo *
clang_index_getObjCProtocolRefListInfo(const CXIdxDeclInfo *DInfo) {
  if (!DInfo)
    return nullptr;

  const DeclInfo *DI = static_cast<const DeclInfo *>(DInfo);
  
  if (const ObjCInterfaceDeclInfo *
        InterInfo = dyn_cast<ObjCInterfaceDeclInfo>(DI))
    return InterInfo->ObjCInterDeclInfo.protocols;
  
  if (const ObjCProtocolDeclInfo *
        ProtInfo = dyn_cast<ObjCProtocolDeclInfo>(DI))
    return &ProtInfo->ObjCProtoRefListInfo;

  if (const ObjCCategoryDeclInfo *CatInfo = dyn_cast<ObjCCategoryDeclInfo>(DI))
    return CatInfo->ObjCCatDeclInfo.protocols;

  return nullptr;
}

const CXIdxObjCPropertyDeclInfo *
clang_index_getObjCPropertyDeclInfo(const CXIdxDeclInfo *DInfo) {
  if (!DInfo)
    return nullptr;

  const DeclInfo *DI = static_cast<const DeclInfo *>(DInfo);
  if (const ObjCPropertyDeclInfo *PropInfo = dyn_cast<ObjCPropertyDeclInfo>(DI))
    return &PropInfo->ObjCPropDeclInfo;

  return nullptr;
}

const CXIdxIBOutletCollectionAttrInfo *
clang_index_getIBOutletCollectionAttrInfo(const CXIdxAttrInfo *AInfo) {
  if (!AInfo)
    return nullptr;

  const AttrInfo *DI = static_cast<const AttrInfo *>(AInfo);
  if (const IBOutletCollectionInfo *
        IBInfo = dyn_cast<IBOutletCollectionInfo>(DI))
    return &IBInfo->IBCollInfo;

  return nullptr;
}

const CXIdxCXXClassDeclInfo *
clang_index_getCXXClassDeclInfo(const CXIdxDeclInfo *DInfo) {
  if (!DInfo)
    return nullptr;

  const DeclInfo *DI = static_cast<const DeclInfo *>(DInfo);
  if (const CXXClassDeclInfo *ClassInfo = dyn_cast<CXXClassDeclInfo>(DI))
    return &ClassInfo->CXXClassInfo;

  return nullptr;
}

CXIdxClientContainer
clang_index_getClientContainer(const CXIdxContainerInfo *info) {
  if (!info)
    return nullptr;
  const ContainerInfo *Container = static_cast<const ContainerInfo *>(info);
  return Container->IndexCtx->getClientContainerForDC(Container->DC);
}

void clang_index_setClientContainer(const CXIdxContainerInfo *info,
                                    CXIdxClientContainer client) {
  if (!info)
    return;
  const ContainerInfo *Container = static_cast<const ContainerInfo *>(info);
  Container->IndexCtx->addContainerInMap(Container->DC, client);
}

CXIdxClientEntity clang_index_getClientEntity(const CXIdxEntityInfo *info) {
  if (!info)
    return nullptr;
  const EntityInfo *Entity = static_cast<const EntityInfo *>(info);
  return Entity->IndexCtx->getClientEntity(Entity->Dcl);
}

void clang_index_setClientEntity(const CXIdxEntityInfo *info,
                                 CXIdxClientEntity client) {
  if (!info)
    return;
  const EntityInfo *Entity = static_cast<const EntityInfo *>(info);
  Entity->IndexCtx->setClientEntity(Entity->Dcl, client);
}

CXIndexAction clang_IndexAction_create(CXIndex CIdx) {
  return new IndexSessionData(CIdx);
}

void clang_IndexAction_dispose(CXIndexAction idxAction) {
  if (idxAction)
    delete static_cast<IndexSessionData *>(idxAction);
}

int clang_indexSourceFile(CXIndexAction idxAction,
                          CXClientData client_data,
                          IndexerCallbacks *index_callbacks,
                          unsigned index_callbacks_size,
                          unsigned index_options,
                          const char *source_filename,
                          const char * const *command_line_args,
                          int num_command_line_args,
                          struct CXUnsavedFile *unsaved_files,
                          unsigned num_unsaved_files,
                          CXTranslationUnit *out_TU,
                          unsigned TU_options) {
  SmallVector<const char *, 4> Args;
  Args.push_back("clang");
  Args.append(command_line_args, command_line_args + num_command_line_args);
  return clang_indexSourceFileFullArgv(
      idxAction, client_data, index_callbacks, index_callbacks_size,
      index_options, source_filename, Args.data(), Args.size(), unsaved_files,
      num_unsaved_files, out_TU, TU_options);
}

int clang_indexSourceFileFullArgv(
    CXIndexAction idxAction, CXClientData client_data,
    IndexerCallbacks *index_callbacks, unsigned index_callbacks_size,
    unsigned index_options, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    CXTranslationUnit *out_TU, unsigned TU_options) {
  LOG_FUNC_SECTION {
    *Log << source_filename << ": ";
    for (int i = 0; i != num_command_line_args; ++i)
      *Log << command_line_args[i] << " ";
  }

  if (num_unsaved_files && !unsaved_files)
    return CXError_InvalidArguments;

  CXErrorCode result = CXError_Failure;
  auto IndexSourceFileImpl = [=, &result]() {
    result = clang_indexSourceFile_Impl(
        idxAction, client_data, index_callbacks, index_callbacks_size,
        index_options, source_filename, command_line_args,
        num_command_line_args, llvm::ArrayRef(unsaved_files, num_unsaved_files),
        out_TU, TU_options);
  };

  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, IndexSourceFileImpl)) {
    fprintf(stderr, "libclang: crash detected during indexing source file: {\n");
    fprintf(stderr, "  'source_filename' : '%s'\n", source_filename);
    fprintf(stderr, "  'command_line_args' : [");
    for (int i = 0; i != num_command_line_args; ++i) {
      if (i)
        fprintf(stderr, ", ");
      fprintf(stderr, "'%s'", command_line_args[i]);
    }
    fprintf(stderr, "],\n");
    fprintf(stderr, "  'unsaved_files' : [");
    for (unsigned i = 0; i != num_unsaved_files; ++i) {
      if (i)
        fprintf(stderr, ", ");
      fprintf(stderr, "('%s', '...', %ld)", unsaved_files[i].Filename,
              unsaved_files[i].Length);
    }
    fprintf(stderr, "],\n");
    fprintf(stderr, "  'options' : %d,\n", TU_options);
    fprintf(stderr, "}\n");
    
    return 1;
  } else if (getenv("LIBCLANG_RESOURCE_USAGE")) {
    if (out_TU)
      PrintLibclangResourceUsage(*out_TU);
  }

  return result;
}

int clang_indexTranslationUnit(CXIndexAction idxAction,
                               CXClientData client_data,
                               IndexerCallbacks *index_callbacks,
                               unsigned index_callbacks_size,
                               unsigned index_options,
                               CXTranslationUnit TU) {
  LOG_FUNC_SECTION {
    *Log << TU;
  }

  CXErrorCode result;
  auto IndexTranslationUnitImpl = [=, &result]() {
    result = clang_indexTranslationUnit_Impl(
        idxAction, client_data, index_callbacks, index_callbacks_size,
        index_options, TU);
  };

  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, IndexTranslationUnitImpl)) {
    fprintf(stderr, "libclang: crash detected during indexing TU\n");
    
    return 1;
  }

  return result;
}

void clang_indexLoc_getFileLocation(CXIdxLoc location,
                                    CXIdxClientFile *indexFile,
                                    CXFile *file,
                                    unsigned *line,
                                    unsigned *column,
                                    unsigned *offset) {
  if (indexFile) *indexFile = nullptr;
  if (file)   *file = nullptr;
  if (line)   *line = 0;
  if (column) *column = 0;
  if (offset) *offset = 0;

  SourceLocation Loc = SourceLocation::getFromRawEncoding(location.int_data);
  if (!location.ptr_data[0] || Loc.isInvalid())
    return;

  CXIndexDataConsumer &DataConsumer =
      *static_cast<CXIndexDataConsumer*>(location.ptr_data[0]);
  DataConsumer.translateLoc(Loc, indexFile, file, line, column, offset);
}

CXSourceLocation clang_indexLoc_getCXSourceLocation(CXIdxLoc location) {
  SourceLocation Loc = SourceLocation::getFromRawEncoding(location.int_data);
  if (!location.ptr_data[0] || Loc.isInvalid())
    return clang_getNullLocation();

  CXIndexDataConsumer &DataConsumer =
      *static_cast<CXIndexDataConsumer*>(location.ptr_data[0]);
  return cxloc::translateSourceLocation(DataConsumer.getASTContext(), Loc);
}
