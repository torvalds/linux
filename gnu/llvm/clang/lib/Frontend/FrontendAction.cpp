//===--- FrontendAction.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/FrontendAction.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/Sarif.h"
#include "clang/Basic/Stack.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/LayoutOverrideSource.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/SARIFDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Sema/HLSLExternalSemaSource.h"
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <system_error>
using namespace clang;

LLVM_INSTANTIATE_REGISTRY(FrontendPluginRegistry)

namespace {

class DelegatingDeserializationListener : public ASTDeserializationListener {
  ASTDeserializationListener *Previous;
  bool DeletePrevious;

public:
  explicit DelegatingDeserializationListener(
      ASTDeserializationListener *Previous, bool DeletePrevious)
      : Previous(Previous), DeletePrevious(DeletePrevious) {}
  ~DelegatingDeserializationListener() override {
    if (DeletePrevious)
      delete Previous;
  }

  DelegatingDeserializationListener(const DelegatingDeserializationListener &) =
      delete;
  DelegatingDeserializationListener &
  operator=(const DelegatingDeserializationListener &) = delete;

  void ReaderInitialized(ASTReader *Reader) override {
    if (Previous)
      Previous->ReaderInitialized(Reader);
  }
  void IdentifierRead(serialization::IdentifierID ID,
                      IdentifierInfo *II) override {
    if (Previous)
      Previous->IdentifierRead(ID, II);
  }
  void TypeRead(serialization::TypeIdx Idx, QualType T) override {
    if (Previous)
      Previous->TypeRead(Idx, T);
  }
  void DeclRead(GlobalDeclID ID, const Decl *D) override {
    if (Previous)
      Previous->DeclRead(ID, D);
  }
  void SelectorRead(serialization::SelectorID ID, Selector Sel) override {
    if (Previous)
      Previous->SelectorRead(ID, Sel);
  }
  void MacroDefinitionRead(serialization::PreprocessedEntityID PPID,
                           MacroDefinitionRecord *MD) override {
    if (Previous)
      Previous->MacroDefinitionRead(PPID, MD);
  }
};

/// Dumps deserialized declarations.
class DeserializedDeclsDumper : public DelegatingDeserializationListener {
public:
  explicit DeserializedDeclsDumper(ASTDeserializationListener *Previous,
                                   bool DeletePrevious)
      : DelegatingDeserializationListener(Previous, DeletePrevious) {}

  void DeclRead(GlobalDeclID ID, const Decl *D) override {
    llvm::outs() << "PCH DECL: " << D->getDeclKindName();
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
      llvm::outs() << " - ";
      ND->printQualifiedName(llvm::outs());
    }
    llvm::outs() << "\n";

    DelegatingDeserializationListener::DeclRead(ID, D);
  }
};

/// Checks deserialized declarations and emits error if a name
/// matches one given in command-line using -error-on-deserialized-decl.
class DeserializedDeclsChecker : public DelegatingDeserializationListener {
  ASTContext &Ctx;
  std::set<std::string> NamesToCheck;

public:
  DeserializedDeclsChecker(ASTContext &Ctx,
                           const std::set<std::string> &NamesToCheck,
                           ASTDeserializationListener *Previous,
                           bool DeletePrevious)
      : DelegatingDeserializationListener(Previous, DeletePrevious), Ctx(Ctx),
        NamesToCheck(NamesToCheck) {}

  void DeclRead(GlobalDeclID ID, const Decl *D) override {
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
      if (NamesToCheck.find(ND->getNameAsString()) != NamesToCheck.end()) {
        unsigned DiagID
          = Ctx.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error,
                                                 "%0 was deserialized");
        Ctx.getDiagnostics().Report(Ctx.getFullLoc(D->getLocation()), DiagID)
            << ND;
      }

    DelegatingDeserializationListener::DeclRead(ID, D);
  }
};

} // end anonymous namespace

FrontendAction::FrontendAction() : Instance(nullptr) {}

FrontendAction::~FrontendAction() {}

void FrontendAction::setCurrentInput(const FrontendInputFile &CurrentInput,
                                     std::unique_ptr<ASTUnit> AST) {
  this->CurrentInput = CurrentInput;
  CurrentASTUnit = std::move(AST);
}

Module *FrontendAction::getCurrentModule() const {
  CompilerInstance &CI = getCompilerInstance();
  return CI.getPreprocessor().getHeaderSearchInfo().lookupModule(
      CI.getLangOpts().CurrentModule, SourceLocation(), /*AllowSearch*/false);
}

std::unique_ptr<ASTConsumer>
FrontendAction::CreateWrappedASTConsumer(CompilerInstance &CI,
                                         StringRef InFile) {
  std::unique_ptr<ASTConsumer> Consumer = CreateASTConsumer(CI, InFile);
  if (!Consumer)
    return nullptr;

  // Validate -add-plugin args.
  bool FoundAllPlugins = true;
  for (const std::string &Arg : CI.getFrontendOpts().AddPluginActions) {
    bool Found = false;
    for (const FrontendPluginRegistry::entry &Plugin :
         FrontendPluginRegistry::entries()) {
      if (Plugin.getName() == Arg)
        Found = true;
    }
    if (!Found) {
      CI.getDiagnostics().Report(diag::err_fe_invalid_plugin_name) << Arg;
      FoundAllPlugins = false;
    }
  }
  if (!FoundAllPlugins)
    return nullptr;

  // If there are no registered plugins we don't need to wrap the consumer
  if (FrontendPluginRegistry::begin() == FrontendPluginRegistry::end())
    return Consumer;

  // If this is a code completion run, avoid invoking the plugin consumers
  if (CI.hasCodeCompletionConsumer())
    return Consumer;

  // Collect the list of plugins that go before the main action (in Consumers)
  // or after it (in AfterConsumers)
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;
  std::vector<std::unique_ptr<ASTConsumer>> AfterConsumers;
  for (const FrontendPluginRegistry::entry &Plugin :
       FrontendPluginRegistry::entries()) {
    std::unique_ptr<PluginASTAction> P = Plugin.instantiate();
    PluginASTAction::ActionType ActionType = P->getActionType();
    if (ActionType == PluginASTAction::CmdlineAfterMainAction ||
        ActionType == PluginASTAction::CmdlineBeforeMainAction) {
      // This is O(|plugins| * |add_plugins|), but since both numbers are
      // way below 50 in practice, that's ok.
      if (llvm::is_contained(CI.getFrontendOpts().AddPluginActions,
                             Plugin.getName())) {
        if (ActionType == PluginASTAction::CmdlineBeforeMainAction)
          ActionType = PluginASTAction::AddBeforeMainAction;
        else
          ActionType = PluginASTAction::AddAfterMainAction;
      }
    }
    if ((ActionType == PluginASTAction::AddBeforeMainAction ||
         ActionType == PluginASTAction::AddAfterMainAction) &&
        P->ParseArgs(
            CI,
            CI.getFrontendOpts().PluginArgs[std::string(Plugin.getName())])) {
      std::unique_ptr<ASTConsumer> PluginConsumer = P->CreateASTConsumer(CI, InFile);
      if (ActionType == PluginASTAction::AddBeforeMainAction) {
        Consumers.push_back(std::move(PluginConsumer));
      } else {
        AfterConsumers.push_back(std::move(PluginConsumer));
      }
    }
  }

  // Add to Consumers the main consumer, then all the plugins that go after it
  Consumers.push_back(std::move(Consumer));
  if (!AfterConsumers.empty()) {
    // If we have plugins after the main consumer, which may be the codegen
    // action, they likely will need the ASTContext, so don't clear it in the
    // codegen action.
    CI.getCodeGenOpts().ClearASTBeforeBackend = false;
    for (auto &C : AfterConsumers)
      Consumers.push_back(std::move(C));
  }

  return std::make_unique<MultiplexConsumer>(std::move(Consumers));
}

/// For preprocessed files, if the first line is the linemarker and specifies
/// the original source file name, use that name as the input file name.
/// Returns the location of the first token after the line marker directive.
///
/// \param CI The compiler instance.
/// \param InputFile Populated with the filename from the line marker.
/// \param IsModuleMap If \c true, add a line note corresponding to this line
///        directive. (We need to do this because the directive will not be
///        visited by the preprocessor.)
static SourceLocation ReadOriginalFileName(CompilerInstance &CI,
                                           std::string &InputFile,
                                           bool IsModuleMap = false) {
  auto &SourceMgr = CI.getSourceManager();
  auto MainFileID = SourceMgr.getMainFileID();

  auto MainFileBuf = SourceMgr.getBufferOrNone(MainFileID);
  if (!MainFileBuf)
    return SourceLocation();

  std::unique_ptr<Lexer> RawLexer(
      new Lexer(MainFileID, *MainFileBuf, SourceMgr, CI.getLangOpts()));

  // If the first line has the syntax of
  //
  // # NUM "FILENAME"
  //
  // we use FILENAME as the input file name.
  Token T;
  if (RawLexer->LexFromRawLexer(T) || T.getKind() != tok::hash)
    return SourceLocation();
  if (RawLexer->LexFromRawLexer(T) || T.isAtStartOfLine() ||
      T.getKind() != tok::numeric_constant)
    return SourceLocation();

  unsigned LineNo;
  SourceLocation LineNoLoc = T.getLocation();
  if (IsModuleMap) {
    llvm::SmallString<16> Buffer;
    if (Lexer::getSpelling(LineNoLoc, Buffer, SourceMgr, CI.getLangOpts())
            .getAsInteger(10, LineNo))
      return SourceLocation();
  }

  RawLexer->LexFromRawLexer(T);
  if (T.isAtStartOfLine() || T.getKind() != tok::string_literal)
    return SourceLocation();

  StringLiteralParser Literal(T, CI.getPreprocessor());
  if (Literal.hadError)
    return SourceLocation();
  RawLexer->LexFromRawLexer(T);
  if (T.isNot(tok::eof) && !T.isAtStartOfLine())
    return SourceLocation();
  InputFile = Literal.GetString().str();

  if (IsModuleMap)
    CI.getSourceManager().AddLineNote(
        LineNoLoc, LineNo, SourceMgr.getLineTableFilenameID(InputFile), false,
        false, SrcMgr::C_User_ModuleMap);

  return T.getLocation();
}

static SmallVectorImpl<char> &
operator+=(SmallVectorImpl<char> &Includes, StringRef RHS) {
  Includes.append(RHS.begin(), RHS.end());
  return Includes;
}

static void addHeaderInclude(StringRef HeaderName,
                             SmallVectorImpl<char> &Includes,
                             const LangOptions &LangOpts,
                             bool IsExternC) {
  if (IsExternC && LangOpts.CPlusPlus)
    Includes += "extern \"C\" {\n";
  if (LangOpts.ObjC)
    Includes += "#import \"";
  else
    Includes += "#include \"";

  Includes += HeaderName;

  Includes += "\"\n";
  if (IsExternC && LangOpts.CPlusPlus)
    Includes += "}\n";
}

/// Collect the set of header includes needed to construct the given
/// module and update the TopHeaders file set of the module.
///
/// \param Module The module we're collecting includes from.
///
/// \param Includes Will be augmented with the set of \#includes or \#imports
/// needed to load all of the named headers.
static std::error_code collectModuleHeaderIncludes(
    const LangOptions &LangOpts, FileManager &FileMgr, DiagnosticsEngine &Diag,
    ModuleMap &ModMap, clang::Module *Module, SmallVectorImpl<char> &Includes) {
  // Don't collect any headers for unavailable modules.
  if (!Module->isAvailable())
    return std::error_code();

  // Resolve all lazy header directives to header files.
  ModMap.resolveHeaderDirectives(Module, /*File=*/std::nullopt);

  // If any headers are missing, we can't build this module. In most cases,
  // diagnostics for this should have already been produced; we only get here
  // if explicit stat information was provided.
  // FIXME: If the name resolves to a file with different stat information,
  // produce a better diagnostic.
  if (!Module->MissingHeaders.empty()) {
    auto &MissingHeader = Module->MissingHeaders.front();
    Diag.Report(MissingHeader.FileNameLoc, diag::err_module_header_missing)
      << MissingHeader.IsUmbrella << MissingHeader.FileName;
    return std::error_code();
  }

  // Add includes for each of these headers.
  for (auto HK : {Module::HK_Normal, Module::HK_Private}) {
    for (Module::Header &H : Module->Headers[HK]) {
      Module->addTopHeader(H.Entry);
      // Use the path as specified in the module map file. We'll look for this
      // file relative to the module build directory (the directory containing
      // the module map file) so this will find the same file that we found
      // while parsing the module map.
      addHeaderInclude(H.PathRelativeToRootModuleDirectory, Includes, LangOpts,
                       Module->IsExternC);
    }
  }
  // Note that Module->PrivateHeaders will not be a TopHeader.

  if (std::optional<Module::Header> UmbrellaHeader =
          Module->getUmbrellaHeaderAsWritten()) {
    Module->addTopHeader(UmbrellaHeader->Entry);
    if (Module->Parent)
      // Include the umbrella header for submodules.
      addHeaderInclude(UmbrellaHeader->PathRelativeToRootModuleDirectory,
                       Includes, LangOpts, Module->IsExternC);
  } else if (std::optional<Module::DirectoryName> UmbrellaDir =
                 Module->getUmbrellaDirAsWritten()) {
    // Add all of the headers we find in this subdirectory.
    std::error_code EC;
    SmallString<128> DirNative;
    llvm::sys::path::native(UmbrellaDir->Entry.getName(), DirNative);

    llvm::vfs::FileSystem &FS = FileMgr.getVirtualFileSystem();
    SmallVector<std::pair<std::string, FileEntryRef>, 8> Headers;
    for (llvm::vfs::recursive_directory_iterator Dir(FS, DirNative, EC), End;
         Dir != End && !EC; Dir.increment(EC)) {
      // Check whether this entry has an extension typically associated with
      // headers.
      if (!llvm::StringSwitch<bool>(llvm::sys::path::extension(Dir->path()))
               .Cases(".h", ".H", ".hh", ".hpp", true)
               .Default(false))
        continue;

      auto Header = FileMgr.getOptionalFileRef(Dir->path());
      // FIXME: This shouldn't happen unless there is a file system race. Is
      // that worth diagnosing?
      if (!Header)
        continue;

      // If this header is marked 'unavailable' in this module, don't include
      // it.
      if (ModMap.isHeaderUnavailableInModule(*Header, Module))
        continue;

      // Compute the relative path from the directory to this file.
      SmallVector<StringRef, 16> Components;
      auto PathIt = llvm::sys::path::rbegin(Dir->path());
      for (int I = 0; I != Dir.level() + 1; ++I, ++PathIt)
        Components.push_back(*PathIt);
      SmallString<128> RelativeHeader(
          UmbrellaDir->PathRelativeToRootModuleDirectory);
      for (auto It = Components.rbegin(), End = Components.rend(); It != End;
           ++It)
        llvm::sys::path::append(RelativeHeader, *It);

      std::string RelName = RelativeHeader.c_str();
      Headers.push_back(std::make_pair(RelName, *Header));
    }

    if (EC)
      return EC;

    // Sort header paths and make the header inclusion order deterministic
    // across different OSs and filesystems.
    llvm::sort(Headers, llvm::less_first());
    for (auto &H : Headers) {
      // Include this header as part of the umbrella directory.
      Module->addTopHeader(H.second);
      addHeaderInclude(H.first, Includes, LangOpts, Module->IsExternC);
    }
  }

  // Recurse into submodules.
  for (auto *Submodule : Module->submodules())
    if (std::error_code Err = collectModuleHeaderIncludes(
            LangOpts, FileMgr, Diag, ModMap, Submodule, Includes))
      return Err;

  return std::error_code();
}

static bool loadModuleMapForModuleBuild(CompilerInstance &CI, bool IsSystem,
                                        bool IsPreprocessed,
                                        std::string &PresumedModuleMapFile,
                                        unsigned &Offset) {
  auto &SrcMgr = CI.getSourceManager();
  HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();

  // Map the current input to a file.
  FileID ModuleMapID = SrcMgr.getMainFileID();
  OptionalFileEntryRef ModuleMap = SrcMgr.getFileEntryRefForID(ModuleMapID);
  assert(ModuleMap && "MainFileID without FileEntry");

  // If the module map is preprocessed, handle the initial line marker;
  // line directives are not part of the module map syntax in general.
  Offset = 0;
  if (IsPreprocessed) {
    SourceLocation EndOfLineMarker =
        ReadOriginalFileName(CI, PresumedModuleMapFile, /*IsModuleMap*/ true);
    if (EndOfLineMarker.isValid())
      Offset = CI.getSourceManager().getDecomposedLoc(EndOfLineMarker).second;
  }

  // Load the module map file.
  if (HS.loadModuleMapFile(*ModuleMap, IsSystem, ModuleMapID, &Offset,
                           PresumedModuleMapFile))
    return true;

  if (SrcMgr.getBufferOrFake(ModuleMapID).getBufferSize() == Offset)
    Offset = 0;

  // Infer framework module if possible.
  if (HS.getModuleMap().canInferFrameworkModule(ModuleMap->getDir())) {
    SmallString<128> InferredFrameworkPath = ModuleMap->getDir().getName();
    llvm::sys::path::append(InferredFrameworkPath,
                            CI.getLangOpts().ModuleName + ".framework");
    if (auto Dir =
            CI.getFileManager().getOptionalDirectoryRef(InferredFrameworkPath))
      (void)HS.getModuleMap().inferFrameworkModule(*Dir, IsSystem, nullptr);
  }

  return false;
}

static Module *prepareToBuildModule(CompilerInstance &CI,
                                    StringRef ModuleMapFilename) {
  if (CI.getLangOpts().CurrentModule.empty()) {
    CI.getDiagnostics().Report(diag::err_missing_module_name);

    // FIXME: Eventually, we could consider asking whether there was just
    // a single module described in the module map, and use that as a
    // default. Then it would be fairly trivial to just "compile" a module
    // map with a single module (the common case).
    return nullptr;
  }

  // Dig out the module definition.
  HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();
  Module *M = HS.lookupModule(CI.getLangOpts().CurrentModule, SourceLocation(),
                              /*AllowSearch=*/true);
  if (!M) {
    CI.getDiagnostics().Report(diag::err_missing_module)
      << CI.getLangOpts().CurrentModule << ModuleMapFilename;

    return nullptr;
  }

  // Check whether we can build this module at all.
  if (Preprocessor::checkModuleIsAvailable(CI.getLangOpts(), CI.getTarget(), *M,
                                           CI.getDiagnostics()))
    return nullptr;

  // Inform the preprocessor that includes from within the input buffer should
  // be resolved relative to the build directory of the module map file.
  CI.getPreprocessor().setMainFileDir(*M->Directory);

  // If the module was inferred from a different module map (via an expanded
  // umbrella module definition), track that fact.
  // FIXME: It would be preferable to fill this in as part of processing
  // the module map, rather than adding it after the fact.
  StringRef OriginalModuleMapName = CI.getFrontendOpts().OriginalModuleMap;
  if (!OriginalModuleMapName.empty()) {
    auto OriginalModuleMap =
        CI.getFileManager().getOptionalFileRef(OriginalModuleMapName,
                                               /*openFile*/ true);
    if (!OriginalModuleMap) {
      CI.getDiagnostics().Report(diag::err_module_map_not_found)
        << OriginalModuleMapName;
      return nullptr;
    }
    if (*OriginalModuleMap != CI.getSourceManager().getFileEntryRefForID(
                                  CI.getSourceManager().getMainFileID())) {
      M->IsInferred = true;
      auto FileCharacter =
          M->IsSystem ? SrcMgr::C_System_ModuleMap : SrcMgr::C_User_ModuleMap;
      FileID OriginalModuleMapFID = CI.getSourceManager().getOrCreateFileID(
          *OriginalModuleMap, FileCharacter);
      CI.getPreprocessor()
          .getHeaderSearchInfo()
          .getModuleMap()
          .setInferredModuleAllowedBy(M, OriginalModuleMapFID);
    }
  }

  // If we're being run from the command-line, the module build stack will not
  // have been filled in yet, so complete it now in order to allow us to detect
  // module cycles.
  SourceManager &SourceMgr = CI.getSourceManager();
  if (SourceMgr.getModuleBuildStack().empty())
    SourceMgr.pushModuleBuildStack(CI.getLangOpts().CurrentModule,
                                   FullSourceLoc(SourceLocation(), SourceMgr));
  return M;
}

/// Compute the input buffer that should be used to build the specified module.
static std::unique_ptr<llvm::MemoryBuffer>
getInputBufferForModule(CompilerInstance &CI, Module *M) {
  FileManager &FileMgr = CI.getFileManager();

  // Collect the set of #includes we need to build the module.
  SmallString<256> HeaderContents;
  std::error_code Err = std::error_code();
  if (std::optional<Module::Header> UmbrellaHeader =
          M->getUmbrellaHeaderAsWritten())
    addHeaderInclude(UmbrellaHeader->PathRelativeToRootModuleDirectory,
                     HeaderContents, CI.getLangOpts(), M->IsExternC);
  Err = collectModuleHeaderIncludes(
      CI.getLangOpts(), FileMgr, CI.getDiagnostics(),
      CI.getPreprocessor().getHeaderSearchInfo().getModuleMap(), M,
      HeaderContents);

  if (Err) {
    CI.getDiagnostics().Report(diag::err_module_cannot_create_includes)
      << M->getFullModuleName() << Err.message();
    return nullptr;
  }

  return llvm::MemoryBuffer::getMemBufferCopy(
      HeaderContents, Module::getModuleInputBufferName());
}

bool FrontendAction::BeginSourceFile(CompilerInstance &CI,
                                     const FrontendInputFile &RealInput) {
  FrontendInputFile Input(RealInput);
  assert(!Instance && "Already processing a source file!");
  assert(!Input.isEmpty() && "Unexpected empty filename!");
  setCurrentInput(Input);
  setCompilerInstance(&CI);

  bool HasBegunSourceFile = false;
  bool ReplayASTFile = Input.getKind().getFormat() == InputKind::Precompiled &&
                       usesPreprocessorOnly();

  // If we fail, reset state since the client will not end up calling the
  // matching EndSourceFile(). All paths that return true should release this.
  auto FailureCleanup = llvm::make_scope_exit([&]() {
    if (HasBegunSourceFile)
      CI.getDiagnosticClient().EndSourceFile();
    CI.setASTConsumer(nullptr);
    CI.clearOutputFiles(/*EraseFiles=*/true);
    CI.getLangOpts().setCompilingModule(LangOptions::CMK_None);
    setCurrentInput(FrontendInputFile());
    setCompilerInstance(nullptr);
  });

  if (!BeginInvocation(CI))
    return false;

  // If we're replaying the build of an AST file, import it and set up
  // the initial state from its build.
  if (ReplayASTFile) {
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(&CI.getDiagnostics());

    // The AST unit populates its own diagnostics engine rather than ours.
    IntrusiveRefCntPtr<DiagnosticsEngine> ASTDiags(
        new DiagnosticsEngine(Diags->getDiagnosticIDs(),
                              &Diags->getDiagnosticOptions()));
    ASTDiags->setClient(Diags->getClient(), /*OwnsClient*/false);

    // FIXME: What if the input is a memory buffer?
    StringRef InputFile = Input.getFile();

    std::unique_ptr<ASTUnit> AST = ASTUnit::LoadFromASTFile(
        std::string(InputFile), CI.getPCHContainerReader(),
        ASTUnit::LoadPreprocessorOnly, ASTDiags, CI.getFileSystemOpts(),
        /*HeaderSearchOptions=*/nullptr);
    if (!AST)
      return false;

    // Options relating to how we treat the input (but not what we do with it)
    // are inherited from the AST unit.
    CI.getHeaderSearchOpts() = AST->getHeaderSearchOpts();
    CI.getPreprocessorOpts() = AST->getPreprocessorOpts();
    CI.getLangOpts() = AST->getLangOpts();

    // Set the shared objects, these are reset when we finish processing the
    // file, otherwise the CompilerInstance will happily destroy them.
    CI.setFileManager(&AST->getFileManager());
    CI.createSourceManager(CI.getFileManager());
    CI.getSourceManager().initializeForReplay(AST->getSourceManager());

    // Preload all the module files loaded transitively by the AST unit. Also
    // load all module map files that were parsed as part of building the AST
    // unit.
    if (auto ASTReader = AST->getASTReader()) {
      auto &MM = ASTReader->getModuleManager();
      auto &PrimaryModule = MM.getPrimaryModule();

      for (serialization::ModuleFile &MF : MM)
        if (&MF != &PrimaryModule)
          CI.getFrontendOpts().ModuleFiles.push_back(MF.FileName);

      ASTReader->visitTopLevelModuleMaps(PrimaryModule, [&](FileEntryRef FE) {
        CI.getFrontendOpts().ModuleMapFiles.push_back(
            std::string(FE.getName()));
      });
    }

    // Set up the input file for replay purposes.
    auto Kind = AST->getInputKind();
    if (Kind.getFormat() == InputKind::ModuleMap) {
      Module *ASTModule =
          AST->getPreprocessor().getHeaderSearchInfo().lookupModule(
              AST->getLangOpts().CurrentModule, SourceLocation(),
              /*AllowSearch*/ false);
      assert(ASTModule && "module file does not define its own module");
      Input = FrontendInputFile(ASTModule->PresumedModuleMapFile, Kind);
    } else {
      auto &OldSM = AST->getSourceManager();
      FileID ID = OldSM.getMainFileID();
      if (auto File = OldSM.getFileEntryRefForID(ID))
        Input = FrontendInputFile(File->getName(), Kind);
      else
        Input = FrontendInputFile(OldSM.getBufferOrFake(ID), Kind);
    }
    setCurrentInput(Input, std::move(AST));
  }

  // AST files follow a very different path, since they share objects via the
  // AST unit.
  if (Input.getKind().getFormat() == InputKind::Precompiled) {
    assert(!usesPreprocessorOnly() && "this case was handled above");
    assert(hasASTFileSupport() &&
           "This action does not have AST file support!");

    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(&CI.getDiagnostics());

    // FIXME: What if the input is a memory buffer?
    StringRef InputFile = Input.getFile();

    std::unique_ptr<ASTUnit> AST = ASTUnit::LoadFromASTFile(
        std::string(InputFile), CI.getPCHContainerReader(),
        ASTUnit::LoadEverything, Diags, CI.getFileSystemOpts(),
        CI.getHeaderSearchOptsPtr(), CI.getLangOptsPtr());

    if (!AST)
      return false;

    // Inform the diagnostic client we are processing a source file.
    CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(), nullptr);
    HasBegunSourceFile = true;

    // Set the shared objects, these are reset when we finish processing the
    // file, otherwise the CompilerInstance will happily destroy them.
    CI.setFileManager(&AST->getFileManager());
    CI.setSourceManager(&AST->getSourceManager());
    CI.setPreprocessor(AST->getPreprocessorPtr());
    Preprocessor &PP = CI.getPreprocessor();
    PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOpts());
    CI.setASTContext(&AST->getASTContext());

    setCurrentInput(Input, std::move(AST));

    // Initialize the action.
    if (!BeginSourceFileAction(CI))
      return false;

    // Create the AST consumer.
    CI.setASTConsumer(CreateWrappedASTConsumer(CI, InputFile));
    if (!CI.hasASTConsumer())
      return false;

    FailureCleanup.release();
    return true;
  }

  // Set up the file and source managers, if needed.
  if (!CI.hasFileManager()) {
    if (!CI.createFileManager()) {
      return false;
    }
  }
  if (!CI.hasSourceManager()) {
    CI.createSourceManager(CI.getFileManager());
    if (CI.getDiagnosticOpts().getFormat() == DiagnosticOptions::SARIF) {
      static_cast<SARIFDiagnosticPrinter *>(&CI.getDiagnosticClient())
          ->setSarifWriter(
              std::make_unique<SarifDocumentWriter>(CI.getSourceManager()));
    }
  }

  // Set up embedding for any specified files. Do this before we load any
  // source files, including the primary module map for the compilation.
  for (const auto &F : CI.getFrontendOpts().ModulesEmbedFiles) {
    if (auto FE = CI.getFileManager().getOptionalFileRef(F, /*openFile*/true))
      CI.getSourceManager().setFileIsTransient(*FE);
    else
      CI.getDiagnostics().Report(diag::err_modules_embed_file_not_found) << F;
  }
  if (CI.getFrontendOpts().ModulesEmbedAllFiles)
    CI.getSourceManager().setAllFilesAreTransient(true);

  // IR files bypass the rest of initialization.
  if (Input.getKind().getLanguage() == Language::LLVM_IR) {
    if (!hasIRSupport()) {
      CI.getDiagnostics().Report(diag::err_ast_action_on_llvm_ir)
          << Input.getFile();
      return false;
    }

    // Inform the diagnostic client we are processing a source file.
    CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(), nullptr);
    HasBegunSourceFile = true;

    // Initialize the action.
    if (!BeginSourceFileAction(CI))
      return false;

    // Initialize the main file entry.
    if (!CI.InitializeSourceManager(CurrentInput))
      return false;

    FailureCleanup.release();
    return true;
  }

  // If the implicit PCH include is actually a directory, rather than
  // a single file, search for a suitable PCH file in that directory.
  if (!CI.getPreprocessorOpts().ImplicitPCHInclude.empty()) {
    FileManager &FileMgr = CI.getFileManager();
    PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
    StringRef PCHInclude = PPOpts.ImplicitPCHInclude;
    std::string SpecificModuleCachePath = CI.getSpecificModuleCachePath();
    if (auto PCHDir = FileMgr.getOptionalDirectoryRef(PCHInclude)) {
      std::error_code EC;
      SmallString<128> DirNative;
      llvm::sys::path::native(PCHDir->getName(), DirNative);
      bool Found = false;
      llvm::vfs::FileSystem &FS = FileMgr.getVirtualFileSystem();
      for (llvm::vfs::directory_iterator Dir = FS.dir_begin(DirNative, EC),
                                         DirEnd;
           Dir != DirEnd && !EC; Dir.increment(EC)) {
        // Check whether this is an acceptable AST file.
        if (ASTReader::isAcceptableASTFile(
                Dir->path(), FileMgr, CI.getModuleCache(),
                CI.getPCHContainerReader(), CI.getLangOpts(),
                CI.getTargetOpts(), CI.getPreprocessorOpts(),
                SpecificModuleCachePath, /*RequireStrictOptionMatches=*/true)) {
          PPOpts.ImplicitPCHInclude = std::string(Dir->path());
          Found = true;
          break;
        }
      }

      if (!Found) {
        CI.getDiagnostics().Report(diag::err_fe_no_pch_in_dir) << PCHInclude;
        return false;
      }
    }
  }

  // Set up the preprocessor if needed. When parsing model files the
  // preprocessor of the original source is reused.
  if (!isModelParsingAction())
    CI.createPreprocessor(getTranslationUnitKind());

  // Inform the diagnostic client we are processing a source file.
  CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(),
                                           &CI.getPreprocessor());
  HasBegunSourceFile = true;

  // Handle C++20 header units.
  // Here, the user has the option to specify that the header name should be
  // looked up in the pre-processor search paths (and the main filename as
  // passed by the driver might therefore be incomplete until that look-up).
  if (CI.getLangOpts().CPlusPlusModules && Input.getKind().isHeaderUnit() &&
      !Input.getKind().isPreprocessed()) {
    StringRef FileName = Input.getFile();
    InputKind Kind = Input.getKind();
    if (Kind.getHeaderUnitKind() != InputKind::HeaderUnit_Abs) {
      assert(CI.hasPreprocessor() &&
             "trying to build a header unit without a Pre-processor?");
      HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();
      // Relative searches begin from CWD.
      auto Dir = CI.getFileManager().getOptionalDirectoryRef(".");
      SmallVector<std::pair<OptionalFileEntryRef, DirectoryEntryRef>, 1> CWD;
      CWD.push_back({std::nullopt, *Dir});
      OptionalFileEntryRef FE =
          HS.LookupFile(FileName, SourceLocation(),
                        /*Angled*/ Input.getKind().getHeaderUnitKind() ==
                            InputKind::HeaderUnit_System,
                        nullptr, nullptr, CWD, nullptr, nullptr, nullptr,
                        nullptr, nullptr, nullptr);
      if (!FE) {
        CI.getDiagnostics().Report(diag::err_module_header_file_not_found)
            << FileName;
        return false;
      }
      // We now have the filename...
      FileName = FE->getName();
      // ... still a header unit, but now use the path as written.
      Kind = Input.getKind().withHeaderUnit(InputKind::HeaderUnit_Abs);
      Input = FrontendInputFile(FileName, Kind, Input.isSystem());
    }
    // Unless the user has overridden the name, the header unit module name is
    // the pathname for the file.
    if (CI.getLangOpts().ModuleName.empty())
      CI.getLangOpts().ModuleName = std::string(FileName);
    CI.getLangOpts().CurrentModule = CI.getLangOpts().ModuleName;
  }

  if (!CI.InitializeSourceManager(Input))
    return false;

  if (CI.getLangOpts().CPlusPlusModules && Input.getKind().isHeaderUnit() &&
      Input.getKind().isPreprocessed() && !usesPreprocessorOnly()) {
    // We have an input filename like foo.iih, but we want to find the right
    // module name (and original file, to build the map entry).
    // Check if the first line specifies the original source file name with a
    // linemarker.
    std::string PresumedInputFile = std::string(getCurrentFileOrBufferName());
    ReadOriginalFileName(CI, PresumedInputFile);
    // Unless the user overrides this, the module name is the name by which the
    // original file was known.
    if (CI.getLangOpts().ModuleName.empty())
      CI.getLangOpts().ModuleName = std::string(PresumedInputFile);
    CI.getLangOpts().CurrentModule = CI.getLangOpts().ModuleName;
  }

  // For module map files, we first parse the module map and synthesize a
  // "<module-includes>" buffer before more conventional processing.
  if (Input.getKind().getFormat() == InputKind::ModuleMap) {
    CI.getLangOpts().setCompilingModule(LangOptions::CMK_ModuleMap);

    std::string PresumedModuleMapFile;
    unsigned OffsetToContents;
    if (loadModuleMapForModuleBuild(CI, Input.isSystem(),
                                    Input.isPreprocessed(),
                                    PresumedModuleMapFile, OffsetToContents))
      return false;

    auto *CurrentModule = prepareToBuildModule(CI, Input.getFile());
    if (!CurrentModule)
      return false;

    CurrentModule->PresumedModuleMapFile = PresumedModuleMapFile;

    if (OffsetToContents)
      // If the module contents are in the same file, skip to them.
      CI.getPreprocessor().setSkipMainFilePreamble(OffsetToContents, true);
    else {
      // Otherwise, convert the module description to a suitable input buffer.
      auto Buffer = getInputBufferForModule(CI, CurrentModule);
      if (!Buffer)
        return false;

      // Reinitialize the main file entry to refer to the new input.
      auto Kind = CurrentModule->IsSystem ? SrcMgr::C_System : SrcMgr::C_User;
      auto &SourceMgr = CI.getSourceManager();
      auto BufferID = SourceMgr.createFileID(std::move(Buffer), Kind);
      assert(BufferID.isValid() && "couldn't create module buffer ID");
      SourceMgr.setMainFileID(BufferID);
    }
  }

  // Initialize the action.
  if (!BeginSourceFileAction(CI))
    return false;

  // If we were asked to load any module map files, do so now.
  for (const auto &Filename : CI.getFrontendOpts().ModuleMapFiles) {
    if (auto File = CI.getFileManager().getOptionalFileRef(Filename))
      CI.getPreprocessor().getHeaderSearchInfo().loadModuleMapFile(
          *File, /*IsSystem*/false);
    else
      CI.getDiagnostics().Report(diag::err_module_map_not_found) << Filename;
  }

  // If compiling implementation of a module, load its module map file now.
  (void)CI.getPreprocessor().getCurrentModuleImplementation();

  // Add a module declaration scope so that modules from -fmodule-map-file
  // arguments may shadow modules found implicitly in search paths.
  CI.getPreprocessor()
      .getHeaderSearchInfo()
      .getModuleMap()
      .finishModuleDeclarationScope();

  // Create the AST context and consumer unless this is a preprocessor only
  // action.
  if (!usesPreprocessorOnly()) {
    // Parsing a model file should reuse the existing ASTContext.
    if (!isModelParsingAction())
      CI.createASTContext();

    // For preprocessed files, check if the first line specifies the original
    // source file name with a linemarker.
    std::string PresumedInputFile = std::string(getCurrentFileOrBufferName());
    if (Input.isPreprocessed())
      ReadOriginalFileName(CI, PresumedInputFile);

    std::unique_ptr<ASTConsumer> Consumer =
        CreateWrappedASTConsumer(CI, PresumedInputFile);
    if (!Consumer)
      return false;

    // FIXME: should not overwrite ASTMutationListener when parsing model files?
    if (!isModelParsingAction())
      CI.getASTContext().setASTMutationListener(Consumer->GetASTMutationListener());

    if (!CI.getPreprocessorOpts().ChainedIncludes.empty()) {
      // Convert headers to PCH and chain them.
      IntrusiveRefCntPtr<ExternalSemaSource> source, FinalReader;
      source = createChainedIncludesSource(CI, FinalReader);
      if (!source)
        return false;
      CI.setASTReader(static_cast<ASTReader *>(FinalReader.get()));
      CI.getASTContext().setExternalSource(source);
    } else if (CI.getLangOpts().Modules ||
               !CI.getPreprocessorOpts().ImplicitPCHInclude.empty()) {
      // Use PCM or PCH.
      assert(hasPCHSupport() && "This action does not have PCH support!");
      ASTDeserializationListener *DeserialListener =
          Consumer->GetASTDeserializationListener();
      bool DeleteDeserialListener = false;
      if (CI.getPreprocessorOpts().DumpDeserializedPCHDecls) {
        DeserialListener = new DeserializedDeclsDumper(DeserialListener,
                                                       DeleteDeserialListener);
        DeleteDeserialListener = true;
      }
      if (!CI.getPreprocessorOpts().DeserializedPCHDeclsToErrorOn.empty()) {
        DeserialListener = new DeserializedDeclsChecker(
            CI.getASTContext(),
            CI.getPreprocessorOpts().DeserializedPCHDeclsToErrorOn,
            DeserialListener, DeleteDeserialListener);
        DeleteDeserialListener = true;
      }
      if (!CI.getPreprocessorOpts().ImplicitPCHInclude.empty()) {
        CI.createPCHExternalASTSource(
            CI.getPreprocessorOpts().ImplicitPCHInclude,
            CI.getPreprocessorOpts().DisablePCHOrModuleValidation,
            CI.getPreprocessorOpts().AllowPCHWithCompilerErrors,
            DeserialListener, DeleteDeserialListener);
        if (!CI.getASTContext().getExternalSource())
          return false;
      }
      // If modules are enabled, create the AST reader before creating
      // any builtins, so that all declarations know that they might be
      // extended by an external source.
      if (CI.getLangOpts().Modules || !CI.hasASTContext() ||
          !CI.getASTContext().getExternalSource()) {
        CI.createASTReader();
        CI.getASTReader()->setDeserializationListener(DeserialListener,
                                                      DeleteDeserialListener);
      }
    }

    CI.setASTConsumer(std::move(Consumer));
    if (!CI.hasASTConsumer())
      return false;
  }

  // Initialize built-in info as long as we aren't using an external AST
  // source.
  if (CI.getLangOpts().Modules || !CI.hasASTContext() ||
      !CI.getASTContext().getExternalSource()) {
    Preprocessor &PP = CI.getPreprocessor();
    PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOpts());
  } else {
    // FIXME: If this is a problem, recover from it by creating a multiplex
    // source.
    assert((!CI.getLangOpts().Modules || CI.getASTReader()) &&
           "modules enabled but created an external source that "
           "doesn't support modules");
  }

  // If we were asked to load any module files, do so now.
  for (const auto &ModuleFile : CI.getFrontendOpts().ModuleFiles) {
    serialization::ModuleFile *Loaded = nullptr;
    if (!CI.loadModuleFile(ModuleFile, Loaded))
      return false;

    if (Loaded && Loaded->StandardCXXModule)
      CI.getDiagnostics().Report(
          diag::warn_eagerly_load_for_standard_cplusplus_modules);
  }

  // If there is a layout overrides file, attach an external AST source that
  // provides the layouts from that file.
  if (!CI.getFrontendOpts().OverrideRecordLayoutsFile.empty() &&
      CI.hasASTContext() && !CI.getASTContext().getExternalSource()) {
    IntrusiveRefCntPtr<ExternalASTSource>
      Override(new LayoutOverrideSource(
                     CI.getFrontendOpts().OverrideRecordLayoutsFile));
    CI.getASTContext().setExternalSource(Override);
  }

  // Setup HLSL External Sema Source
  if (CI.getLangOpts().HLSL && CI.hasASTContext()) {
    IntrusiveRefCntPtr<ExternalSemaSource> HLSLSema(
        new HLSLExternalSemaSource());
    if (auto *SemaSource = dyn_cast_if_present<ExternalSemaSource>(
            CI.getASTContext().getExternalSource())) {
      IntrusiveRefCntPtr<ExternalSemaSource> MultiSema(
          new MultiplexExternalSemaSource(SemaSource, HLSLSema.get()));
      CI.getASTContext().setExternalSource(MultiSema);
    } else
      CI.getASTContext().setExternalSource(HLSLSema);
  }

  FailureCleanup.release();
  return true;
}

llvm::Error FrontendAction::Execute() {
  CompilerInstance &CI = getCompilerInstance();

  if (CI.hasFrontendTimer()) {
    llvm::TimeRegion Timer(CI.getFrontendTimer());
    ExecuteAction();
  }
  else ExecuteAction();

  // If we are supposed to rebuild the global module index, do so now unless
  // there were any module-build failures.
  if (CI.shouldBuildGlobalModuleIndex() && CI.hasFileManager() &&
      CI.hasPreprocessor()) {
    StringRef Cache =
        CI.getPreprocessor().getHeaderSearchInfo().getModuleCachePath();
    if (!Cache.empty()) {
      if (llvm::Error Err = GlobalModuleIndex::writeIndex(
              CI.getFileManager(), CI.getPCHContainerReader(), Cache)) {
        // FIXME this drops the error on the floor, but
        // Index/pch-from-libclang.c seems to rely on dropping at least some of
        // the error conditions!
        consumeError(std::move(Err));
      }
    }
  }

  return llvm::Error::success();
}

void FrontendAction::EndSourceFile() {
  CompilerInstance &CI = getCompilerInstance();

  // Inform the diagnostic client we are done with this source file.
  CI.getDiagnosticClient().EndSourceFile();

  // Inform the preprocessor we are done.
  if (CI.hasPreprocessor())
    CI.getPreprocessor().EndSourceFile();

  // Finalize the action.
  EndSourceFileAction();

  // Sema references the ast consumer, so reset sema first.
  //
  // FIXME: There is more per-file stuff we could just drop here?
  bool DisableFree = CI.getFrontendOpts().DisableFree;
  if (DisableFree) {
    CI.resetAndLeakSema();
    CI.resetAndLeakASTContext();
    llvm::BuryPointer(CI.takeASTConsumer().get());
  } else {
    CI.setSema(nullptr);
    CI.setASTContext(nullptr);
    CI.setASTConsumer(nullptr);
  }

  if (CI.getFrontendOpts().ShowStats) {
    llvm::errs() << "\nSTATISTICS FOR '" << getCurrentFileOrBufferName() << "':\n";
    CI.getPreprocessor().PrintStats();
    CI.getPreprocessor().getIdentifierTable().PrintStats();
    CI.getPreprocessor().getHeaderSearchInfo().PrintStats();
    CI.getSourceManager().PrintStats();
    llvm::errs() << "\n";
  }

  // Cleanup the output streams, and erase the output files if instructed by the
  // FrontendAction.
  CI.clearOutputFiles(/*EraseFiles=*/shouldEraseOutputFiles());

  // The resources are owned by AST when the current file is AST.
  // So we reset the resources here to avoid users accessing it
  // accidently.
  if (isCurrentFileAST()) {
    if (DisableFree) {
      CI.resetAndLeakPreprocessor();
      CI.resetAndLeakSourceManager();
      CI.resetAndLeakFileManager();
      llvm::BuryPointer(std::move(CurrentASTUnit));
    } else {
      CI.setPreprocessor(nullptr);
      CI.setSourceManager(nullptr);
      CI.setFileManager(nullptr);
    }
  }

  setCompilerInstance(nullptr);
  setCurrentInput(FrontendInputFile());
  CI.getLangOpts().setCompilingModule(LangOptions::CMK_None);
}

bool FrontendAction::shouldEraseOutputFiles() {
  return getCompilerInstance().getDiagnostics().hasErrorOccurred();
}

//===----------------------------------------------------------------------===//
// Utility Actions
//===----------------------------------------------------------------------===//

void ASTFrontendAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  if (!CI.hasPreprocessor())
    return;
  // This is a fallback: If the client forgets to invoke this, we mark the
  // current stack as the bottom. Though not optimal, this could help prevent
  // stack overflow during deep recursion.
  clang::noteBottomOfStack();

  // FIXME: Move the truncation aspect of this into Sema, we delayed this till
  // here so the source manager would be initialized.
  if (hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();

  // Use a code completion consumer?
  CodeCompleteConsumer *CompletionConsumer = nullptr;
  if (CI.hasCodeCompletionConsumer())
    CompletionConsumer = &CI.getCodeCompletionConsumer();

  if (!CI.hasSema())
    CI.createSema(getTranslationUnitKind(), CompletionConsumer);

  ParseAST(CI.getSema(), CI.getFrontendOpts().ShowStats,
           CI.getFrontendOpts().SkipFunctionBodies);
}

void PluginASTAction::anchor() { }

std::unique_ptr<ASTConsumer>
PreprocessorFrontendAction::CreateASTConsumer(CompilerInstance &CI,
                                              StringRef InFile) {
  llvm_unreachable("Invalid CreateASTConsumer on preprocessor action!");
}

bool WrapperFrontendAction::PrepareToExecuteAction(CompilerInstance &CI) {
  return WrappedAction->PrepareToExecuteAction(CI);
}
std::unique_ptr<ASTConsumer>
WrapperFrontendAction::CreateASTConsumer(CompilerInstance &CI,
                                         StringRef InFile) {
  return WrappedAction->CreateASTConsumer(CI, InFile);
}
bool WrapperFrontendAction::BeginInvocation(CompilerInstance &CI) {
  return WrappedAction->BeginInvocation(CI);
}
bool WrapperFrontendAction::BeginSourceFileAction(CompilerInstance &CI) {
  WrappedAction->setCurrentInput(getCurrentInput());
  WrappedAction->setCompilerInstance(&CI);
  auto Ret = WrappedAction->BeginSourceFileAction(CI);
  // BeginSourceFileAction may change CurrentInput, e.g. during module builds.
  setCurrentInput(WrappedAction->getCurrentInput());
  return Ret;
}
void WrapperFrontendAction::ExecuteAction() {
  WrappedAction->ExecuteAction();
}
void WrapperFrontendAction::EndSourceFile() { WrappedAction->EndSourceFile(); }
void WrapperFrontendAction::EndSourceFileAction() {
  WrappedAction->EndSourceFileAction();
}
bool WrapperFrontendAction::shouldEraseOutputFiles() {
  return WrappedAction->shouldEraseOutputFiles();
}

bool WrapperFrontendAction::usesPreprocessorOnly() const {
  return WrappedAction->usesPreprocessorOnly();
}
TranslationUnitKind WrapperFrontendAction::getTranslationUnitKind() {
  return WrappedAction->getTranslationUnitKind();
}
bool WrapperFrontendAction::hasPCHSupport() const {
  return WrappedAction->hasPCHSupport();
}
bool WrapperFrontendAction::hasASTFileSupport() const {
  return WrappedAction->hasASTFileSupport();
}
bool WrapperFrontendAction::hasIRSupport() const {
  return WrappedAction->hasIRSupport();
}
bool WrapperFrontendAction::hasCodeCompletionSupport() const {
  return WrappedAction->hasCodeCompletionSupport();
}

WrapperFrontendAction::WrapperFrontendAction(
    std::unique_ptr<FrontendAction> WrappedAction)
  : WrappedAction(std::move(WrappedAction)) {}
