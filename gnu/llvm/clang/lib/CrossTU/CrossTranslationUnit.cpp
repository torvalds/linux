//===--- CrossTranslationUnit.cpp - -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the CrossTranslationUnit interface.
//
//===----------------------------------------------------------------------===//
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CrossTU/CrossTUDiagnostic.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <tuple>

namespace clang {
namespace cross_tu {

namespace {

#define DEBUG_TYPE "CrossTranslationUnit"
STATISTIC(NumGetCTUCalled, "The # of getCTUDefinition function called");
STATISTIC(
    NumNotInOtherTU,
    "The # of getCTUDefinition called but the function is not in any other TU");
STATISTIC(NumGetCTUSuccess,
          "The # of getCTUDefinition successfully returned the "
          "requested function's body");
STATISTIC(NumUnsupportedNodeFound, "The # of imports when the ASTImporter "
                                   "encountered an unsupported AST Node");
STATISTIC(NumNameConflicts, "The # of imports when the ASTImporter "
                            "encountered an ODR error");
STATISTIC(NumTripleMismatch, "The # of triple mismatches");
STATISTIC(NumLangMismatch, "The # of language mismatches");
STATISTIC(NumLangDialectMismatch, "The # of language dialect mismatches");
STATISTIC(NumASTLoadThresholdReached,
          "The # of ASTs not loaded because of threshold");

// Same as Triple's equality operator, but we check a field only if that is
// known in both instances.
bool hasEqualKnownFields(const llvm::Triple &Lhs, const llvm::Triple &Rhs) {
  using llvm::Triple;
  if (Lhs.getArch() != Triple::UnknownArch &&
      Rhs.getArch() != Triple::UnknownArch && Lhs.getArch() != Rhs.getArch())
    return false;
  if (Lhs.getSubArch() != Triple::NoSubArch &&
      Rhs.getSubArch() != Triple::NoSubArch &&
      Lhs.getSubArch() != Rhs.getSubArch())
    return false;
  if (Lhs.getVendor() != Triple::UnknownVendor &&
      Rhs.getVendor() != Triple::UnknownVendor &&
      Lhs.getVendor() != Rhs.getVendor())
    return false;
  if (!Lhs.isOSUnknown() && !Rhs.isOSUnknown() &&
      Lhs.getOS() != Rhs.getOS())
    return false;
  if (Lhs.getEnvironment() != Triple::UnknownEnvironment &&
      Rhs.getEnvironment() != Triple::UnknownEnvironment &&
      Lhs.getEnvironment() != Rhs.getEnvironment())
    return false;
  if (Lhs.getObjectFormat() != Triple::UnknownObjectFormat &&
      Rhs.getObjectFormat() != Triple::UnknownObjectFormat &&
      Lhs.getObjectFormat() != Rhs.getObjectFormat())
    return false;
  return true;
}

// FIXME: This class is will be removed after the transition to llvm::Error.
class IndexErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "clang.index"; }

  std::string message(int Condition) const override {
    switch (static_cast<index_error_code>(Condition)) {
    case index_error_code::success:
      // There should not be a success error. Jump to unreachable directly.
      // Add this case to make the compiler stop complaining.
      break;
    case index_error_code::unspecified:
      return "An unknown error has occurred.";
    case index_error_code::missing_index_file:
      return "The index file is missing.";
    case index_error_code::invalid_index_format:
      return "Invalid index file format.";
    case index_error_code::multiple_definitions:
      return "Multiple definitions in the index file.";
    case index_error_code::missing_definition:
      return "Missing definition from the index file.";
    case index_error_code::failed_import:
      return "Failed to import the definition.";
    case index_error_code::failed_to_get_external_ast:
      return "Failed to load external AST source.";
    case index_error_code::failed_to_generate_usr:
      return "Failed to generate USR.";
    case index_error_code::triple_mismatch:
      return "Triple mismatch";
    case index_error_code::lang_mismatch:
      return "Language mismatch";
    case index_error_code::lang_dialect_mismatch:
      return "Language dialect mismatch";
    case index_error_code::load_threshold_reached:
      return "Load threshold reached";
    case index_error_code::invocation_list_ambiguous:
      return "Invocation list file contains multiple references to the same "
             "source file.";
    case index_error_code::invocation_list_file_not_found:
      return "Invocation list file is not found.";
    case index_error_code::invocation_list_empty:
      return "Invocation list file is empty.";
    case index_error_code::invocation_list_wrong_format:
      return "Invocation list file is in wrong format.";
    case index_error_code::invocation_list_lookup_unsuccessful:
      return "Invocation list file does not contain the requested source file.";
    }
    llvm_unreachable("Unrecognized index_error_code.");
  }
};

static llvm::ManagedStatic<IndexErrorCategory> Category;
} // end anonymous namespace

char IndexError::ID;

void IndexError::log(raw_ostream &OS) const {
  OS << Category->message(static_cast<int>(Code)) << '\n';
}

std::error_code IndexError::convertToErrorCode() const {
  return std::error_code(static_cast<int>(Code), *Category);
}

/// Parse one line of the input CTU index file.
///
/// @param[in]  LineRef     The input CTU index item in format
///                         "<USR-Length>:<USR> <File-Path>".
/// @param[out] LookupName  The lookup name in format "<USR-Length>:<USR>".
/// @param[out] FilePath    The file path "<File-Path>".
static bool parseCrossTUIndexItem(StringRef LineRef, StringRef &LookupName,
                                  StringRef &FilePath) {
  // `LineRef` is "<USR-Length>:<USR> <File-Path>" now.

  size_t USRLength = 0;
  if (LineRef.consumeInteger(10, USRLength))
    return false;
  assert(USRLength && "USRLength should be greater than zero.");

  if (!LineRef.consume_front(":"))
    return false;

  // `LineRef` is now just "<USR> <File-Path>".

  // Check LookupName length out of bound and incorrect delimiter.
  if (USRLength >= LineRef.size() || ' ' != LineRef[USRLength])
    return false;

  LookupName = LineRef.substr(0, USRLength);
  FilePath = LineRef.substr(USRLength + 1);
  return true;
}

llvm::Expected<llvm::StringMap<std::string>>
parseCrossTUIndex(StringRef IndexPath) {
  std::ifstream ExternalMapFile{std::string(IndexPath)};
  if (!ExternalMapFile)
    return llvm::make_error<IndexError>(index_error_code::missing_index_file,
                                        IndexPath.str());

  llvm::StringMap<std::string> Result;
  std::string Line;
  unsigned LineNo = 1;
  while (std::getline(ExternalMapFile, Line)) {
    // Split lookup name and file path
    StringRef LookupName, FilePathInIndex;
    if (!parseCrossTUIndexItem(Line, LookupName, FilePathInIndex))
      return llvm::make_error<IndexError>(
          index_error_code::invalid_index_format, IndexPath.str(), LineNo);

    // Store paths with posix-style directory separator.
    SmallString<32> FilePath(FilePathInIndex);
    llvm::sys::path::native(FilePath, llvm::sys::path::Style::posix);

    bool InsertionOccured;
    std::tie(std::ignore, InsertionOccured) =
        Result.try_emplace(LookupName, FilePath.begin(), FilePath.end());
    if (!InsertionOccured)
      return llvm::make_error<IndexError>(
          index_error_code::multiple_definitions, IndexPath.str(), LineNo);

    ++LineNo;
  }
  return Result;
}

std::string
createCrossTUIndexString(const llvm::StringMap<std::string> &Index) {
  std::ostringstream Result;
  for (const auto &E : Index)
    Result << E.getKey().size() << ':' << E.getKey().str() << ' '
           << E.getValue() << '\n';
  return Result.str();
}

bool shouldImport(const VarDecl *VD, const ASTContext &ACtx) {
  CanQualType CT = ACtx.getCanonicalType(VD->getType());
  return CT.isConstQualified() && VD->getType().isTrivialType(ACtx);
}

static bool hasBodyOrInit(const FunctionDecl *D, const FunctionDecl *&DefD) {
  return D->hasBody(DefD);
}
static bool hasBodyOrInit(const VarDecl *D, const VarDecl *&DefD) {
  return D->getAnyInitializer(DefD);
}
template <typename T> static bool hasBodyOrInit(const T *D) {
  const T *Unused;
  return hasBodyOrInit(D, Unused);
}

CrossTranslationUnitContext::CrossTranslationUnitContext(CompilerInstance &CI)
    : Context(CI.getASTContext()), ASTStorage(CI) {}

CrossTranslationUnitContext::~CrossTranslationUnitContext() {}

std::optional<std::string>
CrossTranslationUnitContext::getLookupName(const NamedDecl *ND) {
  SmallString<128> DeclUSR;
  bool Ret = index::generateUSRForDecl(ND, DeclUSR);
  if (Ret)
    return {};
  return std::string(DeclUSR);
}

/// Recursively visits the decls of a DeclContext, and returns one with the
/// given USR.
template <typename T>
const T *
CrossTranslationUnitContext::findDefInDeclContext(const DeclContext *DC,
                                                  StringRef LookupName) {
  assert(DC && "Declaration Context must not be null");
  for (const Decl *D : DC->decls()) {
    const auto *SubDC = dyn_cast<DeclContext>(D);
    if (SubDC)
      if (const auto *ND = findDefInDeclContext<T>(SubDC, LookupName))
        return ND;

    const auto *ND = dyn_cast<T>(D);
    const T *ResultDecl;
    if (!ND || !hasBodyOrInit(ND, ResultDecl))
      continue;
    std::optional<std::string> ResultLookupName = getLookupName(ResultDecl);
    if (!ResultLookupName || *ResultLookupName != LookupName)
      continue;
    return ResultDecl;
  }
  return nullptr;
}

template <typename T>
llvm::Expected<const T *> CrossTranslationUnitContext::getCrossTUDefinitionImpl(
    const T *D, StringRef CrossTUDir, StringRef IndexName,
    bool DisplayCTUProgress) {
  assert(D && "D is missing, bad call to this function!");
  assert(!hasBodyOrInit(D) &&
         "D has a body or init in current translation unit!");
  ++NumGetCTUCalled;
  const std::optional<std::string> LookupName = getLookupName(D);
  if (!LookupName)
    return llvm::make_error<IndexError>(
        index_error_code::failed_to_generate_usr);
  llvm::Expected<ASTUnit *> ASTUnitOrError =
      loadExternalAST(*LookupName, CrossTUDir, IndexName, DisplayCTUProgress);
  if (!ASTUnitOrError)
    return ASTUnitOrError.takeError();
  ASTUnit *Unit = *ASTUnitOrError;
  assert(&Unit->getFileManager() ==
         &Unit->getASTContext().getSourceManager().getFileManager());

  const llvm::Triple &TripleTo = Context.getTargetInfo().getTriple();
  const llvm::Triple &TripleFrom =
      Unit->getASTContext().getTargetInfo().getTriple();
  // The imported AST had been generated for a different target.
  // Some parts of the triple in the loaded ASTContext can be unknown while the
  // very same parts in the target ASTContext are known. Thus we check for the
  // known parts only.
  if (!hasEqualKnownFields(TripleTo, TripleFrom)) {
    // TODO: Pass the SourceLocation of the CallExpression for more precise
    // diagnostics.
    ++NumTripleMismatch;
    return llvm::make_error<IndexError>(index_error_code::triple_mismatch,
                                        std::string(Unit->getMainFileName()),
                                        TripleTo.str(), TripleFrom.str());
  }

  const auto &LangTo = Context.getLangOpts();
  const auto &LangFrom = Unit->getASTContext().getLangOpts();

  // FIXME: Currenty we do not support CTU across C++ and C and across
  // different dialects of C++.
  if (LangTo.CPlusPlus != LangFrom.CPlusPlus) {
    ++NumLangMismatch;
    return llvm::make_error<IndexError>(index_error_code::lang_mismatch);
  }

  // If CPP dialects are different then return with error.
  //
  // Consider this STL code:
  //   template<typename _Alloc>
  //     struct __alloc_traits
  //   #if __cplusplus >= 201103L
  //     : std::allocator_traits<_Alloc>
  //   #endif
  //     { // ...
  //     };
  // This class template would create ODR errors during merging the two units,
  // since in one translation unit the class template has a base class, however
  // in the other unit it has none.
  if (LangTo.CPlusPlus11 != LangFrom.CPlusPlus11 ||
      LangTo.CPlusPlus14 != LangFrom.CPlusPlus14 ||
      LangTo.CPlusPlus17 != LangFrom.CPlusPlus17 ||
      LangTo.CPlusPlus20 != LangFrom.CPlusPlus20) {
    ++NumLangDialectMismatch;
    return llvm::make_error<IndexError>(
        index_error_code::lang_dialect_mismatch);
  }

  TranslationUnitDecl *TU = Unit->getASTContext().getTranslationUnitDecl();
  if (const T *ResultDecl = findDefInDeclContext<T>(TU, *LookupName))
    return importDefinition(ResultDecl, Unit);
  return llvm::make_error<IndexError>(index_error_code::failed_import);
}

llvm::Expected<const FunctionDecl *>
CrossTranslationUnitContext::getCrossTUDefinition(const FunctionDecl *FD,
                                                  StringRef CrossTUDir,
                                                  StringRef IndexName,
                                                  bool DisplayCTUProgress) {
  return getCrossTUDefinitionImpl(FD, CrossTUDir, IndexName,
                                  DisplayCTUProgress);
}

llvm::Expected<const VarDecl *>
CrossTranslationUnitContext::getCrossTUDefinition(const VarDecl *VD,
                                                  StringRef CrossTUDir,
                                                  StringRef IndexName,
                                                  bool DisplayCTUProgress) {
  return getCrossTUDefinitionImpl(VD, CrossTUDir, IndexName,
                                  DisplayCTUProgress);
}

void CrossTranslationUnitContext::emitCrossTUDiagnostics(const IndexError &IE) {
  switch (IE.getCode()) {
  case index_error_code::missing_index_file:
    Context.getDiagnostics().Report(diag::err_ctu_error_opening)
        << IE.getFileName();
    break;
  case index_error_code::invalid_index_format:
    Context.getDiagnostics().Report(diag::err_extdefmap_parsing)
        << IE.getFileName() << IE.getLineNum();
    break;
  case index_error_code::multiple_definitions:
    Context.getDiagnostics().Report(diag::err_multiple_def_index)
        << IE.getLineNum();
    break;
  case index_error_code::triple_mismatch:
    Context.getDiagnostics().Report(diag::warn_ctu_incompat_triple)
        << IE.getFileName() << IE.getTripleToName() << IE.getTripleFromName();
    break;
  default:
    break;
  }
}

CrossTranslationUnitContext::ASTUnitStorage::ASTUnitStorage(
    CompilerInstance &CI)
    : Loader(CI, CI.getAnalyzerOpts().CTUDir,
             CI.getAnalyzerOpts().CTUInvocationList),
      LoadGuard(CI.getASTContext().getLangOpts().CPlusPlus
                    ? CI.getAnalyzerOpts().CTUImportCppThreshold
                    : CI.getAnalyzerOpts().CTUImportThreshold) {}

llvm::Expected<ASTUnit *>
CrossTranslationUnitContext::ASTUnitStorage::getASTUnitForFile(
    StringRef FileName, bool DisplayCTUProgress) {
  // Try the cache first.
  auto ASTCacheEntry = FileASTUnitMap.find(FileName);
  if (ASTCacheEntry == FileASTUnitMap.end()) {

    // Do not load if the limit is reached.
    if (!LoadGuard) {
      ++NumASTLoadThresholdReached;
      return llvm::make_error<IndexError>(
          index_error_code::load_threshold_reached);
    }

    auto LoadAttempt = Loader.load(FileName);

    if (!LoadAttempt)
      return LoadAttempt.takeError();

    std::unique_ptr<ASTUnit> LoadedUnit = std::move(LoadAttempt.get());

    // Need the raw pointer and the unique_ptr as well.
    ASTUnit *Unit = LoadedUnit.get();

    // Update the cache.
    FileASTUnitMap[FileName] = std::move(LoadedUnit);

    LoadGuard.indicateLoadSuccess();

    if (DisplayCTUProgress)
      llvm::errs() << "CTU loaded AST file: " << FileName << "\n";

    return Unit;

  } else {
    // Found in the cache.
    return ASTCacheEntry->second.get();
  }
}

llvm::Expected<ASTUnit *>
CrossTranslationUnitContext::ASTUnitStorage::getASTUnitForFunction(
    StringRef FunctionName, StringRef CrossTUDir, StringRef IndexName,
    bool DisplayCTUProgress) {
  // Try the cache first.
  auto ASTCacheEntry = NameASTUnitMap.find(FunctionName);
  if (ASTCacheEntry == NameASTUnitMap.end()) {
    // Load the ASTUnit from the pre-dumped AST file specified by ASTFileName.

    // Ensure that the Index is loaded, as we need to search in it.
    if (llvm::Error IndexLoadError =
            ensureCTUIndexLoaded(CrossTUDir, IndexName))
      return std::move(IndexLoadError);

    // Check if there is an entry in the index for the function.
    if (!NameFileMap.count(FunctionName)) {
      ++NumNotInOtherTU;
      return llvm::make_error<IndexError>(index_error_code::missing_definition);
    }

    // Search in the index for the filename where the definition of FunctionName
    // resides.
    if (llvm::Expected<ASTUnit *> FoundForFile =
            getASTUnitForFile(NameFileMap[FunctionName], DisplayCTUProgress)) {

      // Update the cache.
      NameASTUnitMap[FunctionName] = *FoundForFile;
      return *FoundForFile;

    } else {
      return FoundForFile.takeError();
    }
  } else {
    // Found in the cache.
    return ASTCacheEntry->second;
  }
}

llvm::Expected<std::string>
CrossTranslationUnitContext::ASTUnitStorage::getFileForFunction(
    StringRef FunctionName, StringRef CrossTUDir, StringRef IndexName) {
  if (llvm::Error IndexLoadError = ensureCTUIndexLoaded(CrossTUDir, IndexName))
    return std::move(IndexLoadError);
  return NameFileMap[FunctionName];
}

llvm::Error CrossTranslationUnitContext::ASTUnitStorage::ensureCTUIndexLoaded(
    StringRef CrossTUDir, StringRef IndexName) {
  // Dont initialize if the map is filled.
  if (!NameFileMap.empty())
    return llvm::Error::success();

  // Get the absolute path to the index file.
  SmallString<256> IndexFile = CrossTUDir;
  if (llvm::sys::path::is_absolute(IndexName))
    IndexFile = IndexName;
  else
    llvm::sys::path::append(IndexFile, IndexName);

  if (auto IndexMapping = parseCrossTUIndex(IndexFile)) {
    // Initialize member map.
    NameFileMap = *IndexMapping;
    return llvm::Error::success();
  } else {
    // Error while parsing CrossTU index file.
    return IndexMapping.takeError();
  };
}

llvm::Expected<ASTUnit *> CrossTranslationUnitContext::loadExternalAST(
    StringRef LookupName, StringRef CrossTUDir, StringRef IndexName,
    bool DisplayCTUProgress) {
  // FIXME: The current implementation only supports loading decls with
  //        a lookup name from a single translation unit. If multiple
  //        translation units contains decls with the same lookup name an
  //        error will be returned.

  // Try to get the value from the heavily cached storage.
  llvm::Expected<ASTUnit *> Unit = ASTStorage.getASTUnitForFunction(
      LookupName, CrossTUDir, IndexName, DisplayCTUProgress);

  if (!Unit)
    return Unit.takeError();

  // Check whether the backing pointer of the Expected is a nullptr.
  if (!*Unit)
    return llvm::make_error<IndexError>(
        index_error_code::failed_to_get_external_ast);

  return Unit;
}

CrossTranslationUnitContext::ASTLoader::ASTLoader(
    CompilerInstance &CI, StringRef CTUDir, StringRef InvocationListFilePath)
    : CI(CI), CTUDir(CTUDir), InvocationListFilePath(InvocationListFilePath) {}

CrossTranslationUnitContext::LoadResultTy
CrossTranslationUnitContext::ASTLoader::load(StringRef Identifier) {
  llvm::SmallString<256> Path;
  if (llvm::sys::path::is_absolute(Identifier, PathStyle)) {
    Path = Identifier;
  } else {
    Path = CTUDir;
    llvm::sys::path::append(Path, PathStyle, Identifier);
  }

  // The path is stored in the InvocationList member in posix style. To
  // successfully lookup an entry based on filepath, it must be converted.
  llvm::sys::path::native(Path, PathStyle);

  // Normalize by removing relative path components.
  llvm::sys::path::remove_dots(Path, /*remove_dot_dot*/ true, PathStyle);

  if (Path.ends_with(".ast"))
    return loadFromDump(Path);
  else
    return loadFromSource(Path);
}

CrossTranslationUnitContext::LoadResultTy
CrossTranslationUnitContext::ASTLoader::loadFromDump(StringRef ASTDumpPath) {
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, &*DiagOpts, DiagClient));
  return ASTUnit::LoadFromASTFile(
      std::string(ASTDumpPath.str()),
      CI.getPCHContainerOperations()->getRawReader(), ASTUnit::LoadEverything,
      Diags, CI.getFileSystemOpts(), CI.getHeaderSearchOptsPtr());
}

/// Load the AST from a source-file, which is supposed to be located inside the
/// YAML formatted invocation list file under the filesystem path specified by
/// \p InvocationList. The invocation list should contain absolute paths.
/// \p SourceFilePath is the absolute path of the source file that contains the
/// function definition the analysis is looking for. The Index is built by the
/// \p clang-extdef-mapping tool, which is also supposed to be generating
/// absolute paths.
///
/// Proper diagnostic emission requires absolute paths, so even if a future
/// change introduces the handling of relative paths, this must be taken into
/// consideration.
CrossTranslationUnitContext::LoadResultTy
CrossTranslationUnitContext::ASTLoader::loadFromSource(
    StringRef SourceFilePath) {

  if (llvm::Error InitError = lazyInitInvocationList())
    return std::move(InitError);
  assert(InvocationList);

  auto Invocation = InvocationList->find(SourceFilePath);
  if (Invocation == InvocationList->end())
    return llvm::make_error<IndexError>(
        index_error_code::invocation_list_lookup_unsuccessful);

  const InvocationListTy::mapped_type &InvocationCommand = Invocation->second;

  SmallVector<const char *, 32> CommandLineArgs(InvocationCommand.size());
  std::transform(InvocationCommand.begin(), InvocationCommand.end(),
                 CommandLineArgs.begin(),
                 [](auto &&CmdPart) { return CmdPart.c_str(); });

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts{&CI.getDiagnosticOpts()};
  auto *DiagClient = new ForwardingDiagnosticConsumer{CI.getDiagnosticClient()};
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID{
      CI.getDiagnostics().getDiagnosticIDs()};
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine{DiagID, &*DiagOpts, DiagClient});

  return ASTUnit::LoadFromCommandLine(CommandLineArgs.begin(),
                                      (CommandLineArgs.end()),
                                      CI.getPCHContainerOperations(), Diags,
                                      CI.getHeaderSearchOpts().ResourceDir);
}

llvm::Expected<InvocationListTy>
parseInvocationList(StringRef FileContent, llvm::sys::path::Style PathStyle) {
  InvocationListTy InvocationList;

  /// LLVM YAML parser is used to extract information from invocation list file.
  llvm::SourceMgr SM;
  llvm::yaml::Stream InvocationFile(FileContent, SM);

  /// Only the first document is processed.
  llvm::yaml::document_iterator FirstInvocationFile = InvocationFile.begin();

  /// There has to be at least one document available.
  if (FirstInvocationFile == InvocationFile.end())
    return llvm::make_error<IndexError>(
        index_error_code::invocation_list_empty);

  llvm::yaml::Node *DocumentRoot = FirstInvocationFile->getRoot();
  if (!DocumentRoot)
    return llvm::make_error<IndexError>(
        index_error_code::invocation_list_wrong_format);

  /// According to the format specified the document must be a mapping, where
  /// the keys are paths to source files, and values are sequences of invocation
  /// parts.
  auto *Mappings = dyn_cast<llvm::yaml::MappingNode>(DocumentRoot);
  if (!Mappings)
    return llvm::make_error<IndexError>(
        index_error_code::invocation_list_wrong_format);

  for (auto &NextMapping : *Mappings) {
    /// The keys should be strings, which represent a source-file path.
    auto *Key = dyn_cast<llvm::yaml::ScalarNode>(NextMapping.getKey());
    if (!Key)
      return llvm::make_error<IndexError>(
          index_error_code::invocation_list_wrong_format);

    SmallString<32> ValueStorage;
    StringRef SourcePath = Key->getValue(ValueStorage);

    // Store paths with PathStyle directory separator.
    SmallString<32> NativeSourcePath(SourcePath);
    llvm::sys::path::native(NativeSourcePath, PathStyle);

    StringRef InvocationKey = NativeSourcePath;

    if (InvocationList.contains(InvocationKey))
      return llvm::make_error<IndexError>(
          index_error_code::invocation_list_ambiguous);

    /// The values should be sequences of strings, each representing a part of
    /// the invocation.
    auto *Args = dyn_cast<llvm::yaml::SequenceNode>(NextMapping.getValue());
    if (!Args)
      return llvm::make_error<IndexError>(
          index_error_code::invocation_list_wrong_format);

    for (auto &Arg : *Args) {
      auto *CmdString = dyn_cast<llvm::yaml::ScalarNode>(&Arg);
      if (!CmdString)
        return llvm::make_error<IndexError>(
            index_error_code::invocation_list_wrong_format);
      /// Every conversion starts with an empty working storage, as it is not
      /// clear if this is a requirement of the YAML parser.
      ValueStorage.clear();
      InvocationList[InvocationKey].emplace_back(
          CmdString->getValue(ValueStorage));
    }

    if (InvocationList[InvocationKey].empty())
      return llvm::make_error<IndexError>(
          index_error_code::invocation_list_wrong_format);
  }

  return InvocationList;
}

llvm::Error CrossTranslationUnitContext::ASTLoader::lazyInitInvocationList() {
  /// Lazily initialize the invocation list member used for on-demand parsing.
  if (InvocationList)
    return llvm::Error::success();
  if (index_error_code::success != PreviousParsingResult)
    return llvm::make_error<IndexError>(PreviousParsingResult);

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> FileContent =
      llvm::MemoryBuffer::getFile(InvocationListFilePath);
  if (!FileContent) {
    PreviousParsingResult = index_error_code::invocation_list_file_not_found;
    return llvm::make_error<IndexError>(PreviousParsingResult);
  }
  std::unique_ptr<llvm::MemoryBuffer> ContentBuffer = std::move(*FileContent);
  assert(ContentBuffer && "If no error was produced after loading, the pointer "
                          "should not be nullptr.");

  llvm::Expected<InvocationListTy> ExpectedInvocationList =
      parseInvocationList(ContentBuffer->getBuffer(), PathStyle);

  // Handle the error to store the code for next call to this function.
  if (!ExpectedInvocationList) {
    llvm::handleAllErrors(
        ExpectedInvocationList.takeError(),
        [&](const IndexError &E) { PreviousParsingResult = E.getCode(); });
    return llvm::make_error<IndexError>(PreviousParsingResult);
  }

  InvocationList = *ExpectedInvocationList;

  return llvm::Error::success();
}

template <typename T>
llvm::Expected<const T *>
CrossTranslationUnitContext::importDefinitionImpl(const T *D, ASTUnit *Unit) {
  assert(hasBodyOrInit(D) && "Decls to be imported should have body or init.");

  assert(&D->getASTContext() == &Unit->getASTContext() &&
         "ASTContext of Decl and the unit should match.");
  ASTImporter &Importer = getOrCreateASTImporter(Unit);

  auto ToDeclOrError = Importer.Import(D);
  if (!ToDeclOrError) {
    handleAllErrors(ToDeclOrError.takeError(), [&](const ASTImportError &IE) {
      switch (IE.Error) {
      case ASTImportError::NameConflict:
        ++NumNameConflicts;
        break;
      case ASTImportError::UnsupportedConstruct:
        ++NumUnsupportedNodeFound;
        break;
      case ASTImportError::Unknown:
        llvm_unreachable("Unknown import error happened.");
        break;
      }
    });
    return llvm::make_error<IndexError>(index_error_code::failed_import);
  }
  auto *ToDecl = cast<T>(*ToDeclOrError);
  assert(hasBodyOrInit(ToDecl) && "Imported Decl should have body or init.");
  ++NumGetCTUSuccess;

  // Parent map is invalidated after changing the AST.
  ToDecl->getASTContext().getParentMapContext().clear();

  return ToDecl;
}

llvm::Expected<const FunctionDecl *>
CrossTranslationUnitContext::importDefinition(const FunctionDecl *FD,
                                              ASTUnit *Unit) {
  return importDefinitionImpl(FD, Unit);
}

llvm::Expected<const VarDecl *>
CrossTranslationUnitContext::importDefinition(const VarDecl *VD,
                                              ASTUnit *Unit) {
  return importDefinitionImpl(VD, Unit);
}

void CrossTranslationUnitContext::lazyInitImporterSharedSt(
    TranslationUnitDecl *ToTU) {
  if (!ImporterSharedSt)
    ImporterSharedSt = std::make_shared<ASTImporterSharedState>(*ToTU);
}

ASTImporter &
CrossTranslationUnitContext::getOrCreateASTImporter(ASTUnit *Unit) {
  ASTContext &From = Unit->getASTContext();

  auto I = ASTUnitImporterMap.find(From.getTranslationUnitDecl());
  if (I != ASTUnitImporterMap.end())
    return *I->second;
  lazyInitImporterSharedSt(Context.getTranslationUnitDecl());
  ASTImporter *NewImporter = new ASTImporter(
      Context, Context.getSourceManager().getFileManager(), From,
      From.getSourceManager().getFileManager(), false, ImporterSharedSt);
  ASTUnitImporterMap[From.getTranslationUnitDecl()].reset(NewImporter);
  return *NewImporter;
}

std::optional<clang::MacroExpansionContext>
CrossTranslationUnitContext::getMacroExpansionContextForSourceLocation(
    const clang::SourceLocation &ToLoc) const {
  // FIXME: Implement: Record such a context for every imported ASTUnit; lookup.
  return std::nullopt;
}

bool CrossTranslationUnitContext::isImportedAsNew(const Decl *ToDecl) const {
  if (!ImporterSharedSt)
    return false;
  return ImporterSharedSt->isNewDecl(const_cast<Decl *>(ToDecl));
}

bool CrossTranslationUnitContext::hasError(const Decl *ToDecl) const {
  if (!ImporterSharedSt)
    return false;
  return static_cast<bool>(
      ImporterSharedSt->getImportDeclErrorIfAny(const_cast<Decl *>(ToDecl)));
}

} // namespace cross_tu
} // namespace clang
