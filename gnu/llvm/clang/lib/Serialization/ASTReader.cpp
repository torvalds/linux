//===- ASTReader.cpp - AST File Reader ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTReader class, which reads AST files.
//
//===----------------------------------------------------------------------===//

#include "ASTCommon.h"
#include "ASTReaderInternals.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/AbstractTypeReader.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/ODRDiagsEmitter.h"
#include "clang/AST/OpenACCClause.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/ASTSourceDescriptor.h"
#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticError.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/OpenACCKinds.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PragmaKinds.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/Weak.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include "clang/Serialization/ASTRecordReader.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Serialization/ModuleManager.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "clang/Serialization/SerializationDiagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::serialization;
using namespace clang::serialization::reader;
using llvm::BitstreamCursor;

//===----------------------------------------------------------------------===//
// ChainedASTReaderListener implementation
//===----------------------------------------------------------------------===//

bool
ChainedASTReaderListener::ReadFullVersionInformation(StringRef FullVersion) {
  return First->ReadFullVersionInformation(FullVersion) ||
         Second->ReadFullVersionInformation(FullVersion);
}

void ChainedASTReaderListener::ReadModuleName(StringRef ModuleName) {
  First->ReadModuleName(ModuleName);
  Second->ReadModuleName(ModuleName);
}

void ChainedASTReaderListener::ReadModuleMapFile(StringRef ModuleMapPath) {
  First->ReadModuleMapFile(ModuleMapPath);
  Second->ReadModuleMapFile(ModuleMapPath);
}

bool
ChainedASTReaderListener::ReadLanguageOptions(const LangOptions &LangOpts,
                                              bool Complain,
                                              bool AllowCompatibleDifferences) {
  return First->ReadLanguageOptions(LangOpts, Complain,
                                    AllowCompatibleDifferences) ||
         Second->ReadLanguageOptions(LangOpts, Complain,
                                     AllowCompatibleDifferences);
}

bool ChainedASTReaderListener::ReadTargetOptions(
    const TargetOptions &TargetOpts, bool Complain,
    bool AllowCompatibleDifferences) {
  return First->ReadTargetOptions(TargetOpts, Complain,
                                  AllowCompatibleDifferences) ||
         Second->ReadTargetOptions(TargetOpts, Complain,
                                   AllowCompatibleDifferences);
}

bool ChainedASTReaderListener::ReadDiagnosticOptions(
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts, bool Complain) {
  return First->ReadDiagnosticOptions(DiagOpts, Complain) ||
         Second->ReadDiagnosticOptions(DiagOpts, Complain);
}

bool
ChainedASTReaderListener::ReadFileSystemOptions(const FileSystemOptions &FSOpts,
                                                bool Complain) {
  return First->ReadFileSystemOptions(FSOpts, Complain) ||
         Second->ReadFileSystemOptions(FSOpts, Complain);
}

bool ChainedASTReaderListener::ReadHeaderSearchOptions(
    const HeaderSearchOptions &HSOpts, StringRef SpecificModuleCachePath,
    bool Complain) {
  return First->ReadHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                        Complain) ||
         Second->ReadHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                         Complain);
}

bool ChainedASTReaderListener::ReadPreprocessorOptions(
    const PreprocessorOptions &PPOpts, bool ReadMacros, bool Complain,
    std::string &SuggestedPredefines) {
  return First->ReadPreprocessorOptions(PPOpts, ReadMacros, Complain,
                                        SuggestedPredefines) ||
         Second->ReadPreprocessorOptions(PPOpts, ReadMacros, Complain,
                                         SuggestedPredefines);
}

void ChainedASTReaderListener::ReadCounter(const serialization::ModuleFile &M,
                                           unsigned Value) {
  First->ReadCounter(M, Value);
  Second->ReadCounter(M, Value);
}

bool ChainedASTReaderListener::needsInputFileVisitation() {
  return First->needsInputFileVisitation() ||
         Second->needsInputFileVisitation();
}

bool ChainedASTReaderListener::needsSystemInputFileVisitation() {
  return First->needsSystemInputFileVisitation() ||
  Second->needsSystemInputFileVisitation();
}

void ChainedASTReaderListener::visitModuleFile(StringRef Filename,
                                               ModuleKind Kind) {
  First->visitModuleFile(Filename, Kind);
  Second->visitModuleFile(Filename, Kind);
}

bool ChainedASTReaderListener::visitInputFile(StringRef Filename,
                                              bool isSystem,
                                              bool isOverridden,
                                              bool isExplicitModule) {
  bool Continue = false;
  if (First->needsInputFileVisitation() &&
      (!isSystem || First->needsSystemInputFileVisitation()))
    Continue |= First->visitInputFile(Filename, isSystem, isOverridden,
                                      isExplicitModule);
  if (Second->needsInputFileVisitation() &&
      (!isSystem || Second->needsSystemInputFileVisitation()))
    Continue |= Second->visitInputFile(Filename, isSystem, isOverridden,
                                       isExplicitModule);
  return Continue;
}

void ChainedASTReaderListener::readModuleFileExtension(
       const ModuleFileExtensionMetadata &Metadata) {
  First->readModuleFileExtension(Metadata);
  Second->readModuleFileExtension(Metadata);
}

//===----------------------------------------------------------------------===//
// PCH validator implementation
//===----------------------------------------------------------------------===//

ASTReaderListener::~ASTReaderListener() = default;

/// Compare the given set of language options against an existing set of
/// language options.
///
/// \param Diags If non-NULL, diagnostics will be emitted via this engine.
/// \param AllowCompatibleDifferences If true, differences between compatible
///        language options will be permitted.
///
/// \returns true if the languagae options mis-match, false otherwise.
static bool checkLanguageOptions(const LangOptions &LangOpts,
                                 const LangOptions &ExistingLangOpts,
                                 DiagnosticsEngine *Diags,
                                 bool AllowCompatibleDifferences = true) {
#define LANGOPT(Name, Bits, Default, Description)                   \
  if (ExistingLangOpts.Name != LangOpts.Name) {                     \
    if (Diags) {                                                    \
      if (Bits == 1)                                                \
        Diags->Report(diag::err_pch_langopt_mismatch)               \
          << Description << LangOpts.Name << ExistingLangOpts.Name; \
      else                                                          \
        Diags->Report(diag::err_pch_langopt_value_mismatch)         \
          << Description;                                           \
    }                                                               \
    return true;                                                    \
  }

#define VALUE_LANGOPT(Name, Bits, Default, Description)   \
  if (ExistingLangOpts.Name != LangOpts.Name) {           \
    if (Diags)                                            \
      Diags->Report(diag::err_pch_langopt_value_mismatch) \
        << Description;                                   \
    return true;                                          \
  }

#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)   \
  if (ExistingLangOpts.get##Name() != LangOpts.get##Name()) {  \
    if (Diags)                                                 \
      Diags->Report(diag::err_pch_langopt_value_mismatch)      \
        << Description;                                        \
    return true;                                               \
  }

#define COMPATIBLE_LANGOPT(Name, Bits, Default, Description)  \
  if (!AllowCompatibleDifferences)                            \
    LANGOPT(Name, Bits, Default, Description)

#define COMPATIBLE_ENUM_LANGOPT(Name, Bits, Default, Description)  \
  if (!AllowCompatibleDifferences)                                 \
    ENUM_LANGOPT(Name, Bits, Default, Description)

#define COMPATIBLE_VALUE_LANGOPT(Name, Bits, Default, Description) \
  if (!AllowCompatibleDifferences)                                 \
    VALUE_LANGOPT(Name, Bits, Default, Description)

#define BENIGN_LANGOPT(Name, Bits, Default, Description)
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#define BENIGN_VALUE_LANGOPT(Name, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"

  if (ExistingLangOpts.ModuleFeatures != LangOpts.ModuleFeatures) {
    if (Diags)
      Diags->Report(diag::err_pch_langopt_value_mismatch) << "module features";
    return true;
  }

  if (ExistingLangOpts.ObjCRuntime != LangOpts.ObjCRuntime) {
    if (Diags)
      Diags->Report(diag::err_pch_langopt_value_mismatch)
      << "target Objective-C runtime";
    return true;
  }

  if (ExistingLangOpts.CommentOpts.BlockCommandNames !=
      LangOpts.CommentOpts.BlockCommandNames) {
    if (Diags)
      Diags->Report(diag::err_pch_langopt_value_mismatch)
        << "block command names";
    return true;
  }

  // Sanitizer feature mismatches are treated as compatible differences. If
  // compatible differences aren't allowed, we still only want to check for
  // mismatches of non-modular sanitizers (the only ones which can affect AST
  // generation).
  if (!AllowCompatibleDifferences) {
    SanitizerMask ModularSanitizers = getPPTransparentSanitizers();
    SanitizerSet ExistingSanitizers = ExistingLangOpts.Sanitize;
    SanitizerSet ImportedSanitizers = LangOpts.Sanitize;
    ExistingSanitizers.clear(ModularSanitizers);
    ImportedSanitizers.clear(ModularSanitizers);
    if (ExistingSanitizers.Mask != ImportedSanitizers.Mask) {
      const std::string Flag = "-fsanitize=";
      if (Diags) {
#define SANITIZER(NAME, ID)                                                    \
  {                                                                            \
    bool InExistingModule = ExistingSanitizers.has(SanitizerKind::ID);         \
    bool InImportedModule = ImportedSanitizers.has(SanitizerKind::ID);         \
    if (InExistingModule != InImportedModule)                                  \
      Diags->Report(diag::err_pch_targetopt_feature_mismatch)                  \
          << InExistingModule << (Flag + NAME);                                \
  }
#include "clang/Basic/Sanitizers.def"
      }
      return true;
    }
  }

  return false;
}

/// Compare the given set of target options against an existing set of
/// target options.
///
/// \param Diags If non-NULL, diagnostics will be emitted via this engine.
///
/// \returns true if the target options mis-match, false otherwise.
static bool checkTargetOptions(const TargetOptions &TargetOpts,
                               const TargetOptions &ExistingTargetOpts,
                               DiagnosticsEngine *Diags,
                               bool AllowCompatibleDifferences = true) {
#define CHECK_TARGET_OPT(Field, Name)                             \
  if (TargetOpts.Field != ExistingTargetOpts.Field) {             \
    if (Diags)                                                    \
      Diags->Report(diag::err_pch_targetopt_mismatch)             \
        << Name << TargetOpts.Field << ExistingTargetOpts.Field;  \
    return true;                                                  \
  }

  // The triple and ABI must match exactly.
  CHECK_TARGET_OPT(Triple, "target");
  CHECK_TARGET_OPT(ABI, "target ABI");

  // We can tolerate different CPUs in many cases, notably when one CPU
  // supports a strict superset of another. When allowing compatible
  // differences skip this check.
  if (!AllowCompatibleDifferences) {
    CHECK_TARGET_OPT(CPU, "target CPU");
    CHECK_TARGET_OPT(TuneCPU, "tune CPU");
  }

#undef CHECK_TARGET_OPT

  // Compare feature sets.
  SmallVector<StringRef, 4> ExistingFeatures(
                                             ExistingTargetOpts.FeaturesAsWritten.begin(),
                                             ExistingTargetOpts.FeaturesAsWritten.end());
  SmallVector<StringRef, 4> ReadFeatures(TargetOpts.FeaturesAsWritten.begin(),
                                         TargetOpts.FeaturesAsWritten.end());
  llvm::sort(ExistingFeatures);
  llvm::sort(ReadFeatures);

  // We compute the set difference in both directions explicitly so that we can
  // diagnose the differences differently.
  SmallVector<StringRef, 4> UnmatchedExistingFeatures, UnmatchedReadFeatures;
  std::set_difference(
      ExistingFeatures.begin(), ExistingFeatures.end(), ReadFeatures.begin(),
      ReadFeatures.end(), std::back_inserter(UnmatchedExistingFeatures));
  std::set_difference(ReadFeatures.begin(), ReadFeatures.end(),
                      ExistingFeatures.begin(), ExistingFeatures.end(),
                      std::back_inserter(UnmatchedReadFeatures));

  // If we are allowing compatible differences and the read feature set is
  // a strict subset of the existing feature set, there is nothing to diagnose.
  if (AllowCompatibleDifferences && UnmatchedReadFeatures.empty())
    return false;

  if (Diags) {
    for (StringRef Feature : UnmatchedReadFeatures)
      Diags->Report(diag::err_pch_targetopt_feature_mismatch)
          << /* is-existing-feature */ false << Feature;
    for (StringRef Feature : UnmatchedExistingFeatures)
      Diags->Report(diag::err_pch_targetopt_feature_mismatch)
          << /* is-existing-feature */ true << Feature;
  }

  return !UnmatchedReadFeatures.empty() || !UnmatchedExistingFeatures.empty();
}

bool
PCHValidator::ReadLanguageOptions(const LangOptions &LangOpts,
                                  bool Complain,
                                  bool AllowCompatibleDifferences) {
  const LangOptions &ExistingLangOpts = PP.getLangOpts();
  return checkLanguageOptions(LangOpts, ExistingLangOpts,
                              Complain ? &Reader.Diags : nullptr,
                              AllowCompatibleDifferences);
}

bool PCHValidator::ReadTargetOptions(const TargetOptions &TargetOpts,
                                     bool Complain,
                                     bool AllowCompatibleDifferences) {
  const TargetOptions &ExistingTargetOpts = PP.getTargetInfo().getTargetOpts();
  return checkTargetOptions(TargetOpts, ExistingTargetOpts,
                            Complain ? &Reader.Diags : nullptr,
                            AllowCompatibleDifferences);
}

namespace {

using MacroDefinitionsMap =
    llvm::StringMap<std::pair<StringRef, bool /*IsUndef*/>>;
using DeclsMap = llvm::DenseMap<DeclarationName, SmallVector<NamedDecl *, 8>>;

} // namespace

static bool checkDiagnosticGroupMappings(DiagnosticsEngine &StoredDiags,
                                         DiagnosticsEngine &Diags,
                                         bool Complain) {
  using Level = DiagnosticsEngine::Level;

  // Check current mappings for new -Werror mappings, and the stored mappings
  // for cases that were explicitly mapped to *not* be errors that are now
  // errors because of options like -Werror.
  DiagnosticsEngine *MappingSources[] = { &Diags, &StoredDiags };

  for (DiagnosticsEngine *MappingSource : MappingSources) {
    for (auto DiagIDMappingPair : MappingSource->getDiagnosticMappings()) {
      diag::kind DiagID = DiagIDMappingPair.first;
      Level CurLevel = Diags.getDiagnosticLevel(DiagID, SourceLocation());
      if (CurLevel < DiagnosticsEngine::Error)
        continue; // not significant
      Level StoredLevel =
          StoredDiags.getDiagnosticLevel(DiagID, SourceLocation());
      if (StoredLevel < DiagnosticsEngine::Error) {
        if (Complain)
          Diags.Report(diag::err_pch_diagopt_mismatch) << "-Werror=" +
              Diags.getDiagnosticIDs()->getWarningOptionForDiag(DiagID).str();
        return true;
      }
    }
  }

  return false;
}

static bool isExtHandlingFromDiagsError(DiagnosticsEngine &Diags) {
  diag::Severity Ext = Diags.getExtensionHandlingBehavior();
  if (Ext == diag::Severity::Warning && Diags.getWarningsAsErrors())
    return true;
  return Ext >= diag::Severity::Error;
}

static bool checkDiagnosticMappings(DiagnosticsEngine &StoredDiags,
                                    DiagnosticsEngine &Diags, bool IsSystem,
                                    bool SystemHeaderWarningsInModule,
                                    bool Complain) {
  // Top-level options
  if (IsSystem) {
    if (Diags.getSuppressSystemWarnings())
      return false;
    // If -Wsystem-headers was not enabled before, and it was not explicit,
    // be conservative
    if (StoredDiags.getSuppressSystemWarnings() &&
        !SystemHeaderWarningsInModule) {
      if (Complain)
        Diags.Report(diag::err_pch_diagopt_mismatch) << "-Wsystem-headers";
      return true;
    }
  }

  if (Diags.getWarningsAsErrors() && !StoredDiags.getWarningsAsErrors()) {
    if (Complain)
      Diags.Report(diag::err_pch_diagopt_mismatch) << "-Werror";
    return true;
  }

  if (Diags.getWarningsAsErrors() && Diags.getEnableAllWarnings() &&
      !StoredDiags.getEnableAllWarnings()) {
    if (Complain)
      Diags.Report(diag::err_pch_diagopt_mismatch) << "-Weverything -Werror";
    return true;
  }

  if (isExtHandlingFromDiagsError(Diags) &&
      !isExtHandlingFromDiagsError(StoredDiags)) {
    if (Complain)
      Diags.Report(diag::err_pch_diagopt_mismatch) << "-pedantic-errors";
    return true;
  }

  return checkDiagnosticGroupMappings(StoredDiags, Diags, Complain);
}

/// Return the top import module if it is implicit, nullptr otherwise.
static Module *getTopImportImplicitModule(ModuleManager &ModuleMgr,
                                          Preprocessor &PP) {
  // If the original import came from a file explicitly generated by the user,
  // don't check the diagnostic mappings.
  // FIXME: currently this is approximated by checking whether this is not a
  // module import of an implicitly-loaded module file.
  // Note: ModuleMgr.rbegin() may not be the current module, but it must be in
  // the transitive closure of its imports, since unrelated modules cannot be
  // imported until after this module finishes validation.
  ModuleFile *TopImport = &*ModuleMgr.rbegin();
  while (!TopImport->ImportedBy.empty())
    TopImport = TopImport->ImportedBy[0];
  if (TopImport->Kind != MK_ImplicitModule)
    return nullptr;

  StringRef ModuleName = TopImport->ModuleName;
  assert(!ModuleName.empty() && "diagnostic options read before module name");

  Module *M =
      PP.getHeaderSearchInfo().lookupModule(ModuleName, TopImport->ImportLoc);
  assert(M && "missing module");
  return M;
}

bool PCHValidator::ReadDiagnosticOptions(
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts, bool Complain) {
  DiagnosticsEngine &ExistingDiags = PP.getDiagnostics();
  IntrusiveRefCntPtr<DiagnosticIDs> DiagIDs(ExistingDiags.getDiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagIDs, DiagOpts.get()));
  // This should never fail, because we would have processed these options
  // before writing them to an ASTFile.
  ProcessWarningOptions(*Diags, *DiagOpts, /*Report*/false);

  ModuleManager &ModuleMgr = Reader.getModuleManager();
  assert(ModuleMgr.size() >= 1 && "what ASTFile is this then");

  Module *TopM = getTopImportImplicitModule(ModuleMgr, PP);
  if (!TopM)
    return false;

  Module *Importer = PP.getCurrentModule();

  DiagnosticOptions &ExistingOpts = ExistingDiags.getDiagnosticOptions();
  bool SystemHeaderWarningsInModule =
      Importer && llvm::is_contained(ExistingOpts.SystemHeaderWarningsModules,
                                     Importer->Name);

  // FIXME: if the diagnostics are incompatible, save a DiagnosticOptions that
  // contains the union of their flags.
  return checkDiagnosticMappings(*Diags, ExistingDiags, TopM->IsSystem,
                                 SystemHeaderWarningsInModule, Complain);
}

/// Collect the macro definitions provided by the given preprocessor
/// options.
static void
collectMacroDefinitions(const PreprocessorOptions &PPOpts,
                        MacroDefinitionsMap &Macros,
                        SmallVectorImpl<StringRef> *MacroNames = nullptr) {
  for (unsigned I = 0, N = PPOpts.Macros.size(); I != N; ++I) {
    StringRef Macro = PPOpts.Macros[I].first;
    bool IsUndef = PPOpts.Macros[I].second;

    std::pair<StringRef, StringRef> MacroPair = Macro.split('=');
    StringRef MacroName = MacroPair.first;
    StringRef MacroBody = MacroPair.second;

    // For an #undef'd macro, we only care about the name.
    if (IsUndef) {
      if (MacroNames && !Macros.count(MacroName))
        MacroNames->push_back(MacroName);

      Macros[MacroName] = std::make_pair("", true);
      continue;
    }

    // For a #define'd macro, figure out the actual definition.
    if (MacroName.size() == Macro.size())
      MacroBody = "1";
    else {
      // Note: GCC drops anything following an end-of-line character.
      StringRef::size_type End = MacroBody.find_first_of("\n\r");
      MacroBody = MacroBody.substr(0, End);
    }

    if (MacroNames && !Macros.count(MacroName))
      MacroNames->push_back(MacroName);
    Macros[MacroName] = std::make_pair(MacroBody, false);
  }
}

enum OptionValidation {
  OptionValidateNone,
  OptionValidateContradictions,
  OptionValidateStrictMatches,
};

/// Check the preprocessor options deserialized from the control block
/// against the preprocessor options in an existing preprocessor.
///
/// \param Diags If non-null, produce diagnostics for any mismatches incurred.
/// \param Validation If set to OptionValidateNone, ignore differences in
///        preprocessor options. If set to OptionValidateContradictions,
///        require that options passed both in the AST file and on the command
///        line (-D or -U) match, but tolerate options missing in one or the
///        other. If set to OptionValidateContradictions, require that there
///        are no differences in the options between the two.
static bool checkPreprocessorOptions(
    const PreprocessorOptions &PPOpts,
    const PreprocessorOptions &ExistingPPOpts, bool ReadMacros,
    DiagnosticsEngine *Diags, FileManager &FileMgr,
    std::string &SuggestedPredefines, const LangOptions &LangOpts,
    OptionValidation Validation = OptionValidateContradictions) {
  if (ReadMacros) {
    // Check macro definitions.
    MacroDefinitionsMap ASTFileMacros;
    collectMacroDefinitions(PPOpts, ASTFileMacros);
    MacroDefinitionsMap ExistingMacros;
    SmallVector<StringRef, 4> ExistingMacroNames;
    collectMacroDefinitions(ExistingPPOpts, ExistingMacros,
                            &ExistingMacroNames);

    // Use a line marker to enter the <command line> file, as the defines and
    // undefines here will have come from the command line.
    SuggestedPredefines += "# 1 \"<command line>\" 1\n";

    for (unsigned I = 0, N = ExistingMacroNames.size(); I != N; ++I) {
      // Dig out the macro definition in the existing preprocessor options.
      StringRef MacroName = ExistingMacroNames[I];
      std::pair<StringRef, bool> Existing = ExistingMacros[MacroName];

      // Check whether we know anything about this macro name or not.
      llvm::StringMap<std::pair<StringRef, bool /*IsUndef*/>>::iterator Known =
          ASTFileMacros.find(MacroName);
      if (Validation == OptionValidateNone || Known == ASTFileMacros.end()) {
        if (Validation == OptionValidateStrictMatches) {
          // If strict matches are requested, don't tolerate any extra defines
          // on the command line that are missing in the AST file.
          if (Diags) {
            Diags->Report(diag::err_pch_macro_def_undef) << MacroName << true;
          }
          return true;
        }
        // FIXME: Check whether this identifier was referenced anywhere in the
        // AST file. If so, we should reject the AST file. Unfortunately, this
        // information isn't in the control block. What shall we do about it?

        if (Existing.second) {
          SuggestedPredefines += "#undef ";
          SuggestedPredefines += MacroName.str();
          SuggestedPredefines += '\n';
        } else {
          SuggestedPredefines += "#define ";
          SuggestedPredefines += MacroName.str();
          SuggestedPredefines += ' ';
          SuggestedPredefines += Existing.first.str();
          SuggestedPredefines += '\n';
        }
        continue;
      }

      // If the macro was defined in one but undef'd in the other, we have a
      // conflict.
      if (Existing.second != Known->second.second) {
        if (Diags) {
          Diags->Report(diag::err_pch_macro_def_undef)
              << MacroName << Known->second.second;
        }
        return true;
      }

      // If the macro was #undef'd in both, or if the macro bodies are
      // identical, it's fine.
      if (Existing.second || Existing.first == Known->second.first) {
        ASTFileMacros.erase(Known);
        continue;
      }

      // The macro bodies differ; complain.
      if (Diags) {
        Diags->Report(diag::err_pch_macro_def_conflict)
            << MacroName << Known->second.first << Existing.first;
      }
      return true;
    }

    // Leave the <command line> file and return to <built-in>.
    SuggestedPredefines += "# 1 \"<built-in>\" 2\n";

    if (Validation == OptionValidateStrictMatches) {
      // If strict matches are requested, don't tolerate any extra defines in
      // the AST file that are missing on the command line.
      for (const auto &MacroName : ASTFileMacros.keys()) {
        if (Diags) {
          Diags->Report(diag::err_pch_macro_def_undef) << MacroName << false;
        }
        return true;
      }
    }
  }

  // Check whether we're using predefines.
  if (PPOpts.UsePredefines != ExistingPPOpts.UsePredefines &&
      Validation != OptionValidateNone) {
    if (Diags) {
      Diags->Report(diag::err_pch_undef) << ExistingPPOpts.UsePredefines;
    }
    return true;
  }

  // Detailed record is important since it is used for the module cache hash.
  if (LangOpts.Modules &&
      PPOpts.DetailedRecord != ExistingPPOpts.DetailedRecord &&
      Validation != OptionValidateNone) {
    if (Diags) {
      Diags->Report(diag::err_pch_pp_detailed_record) << PPOpts.DetailedRecord;
    }
    return true;
  }

  // Compute the #include and #include_macros lines we need.
  for (unsigned I = 0, N = ExistingPPOpts.Includes.size(); I != N; ++I) {
    StringRef File = ExistingPPOpts.Includes[I];

    if (!ExistingPPOpts.ImplicitPCHInclude.empty() &&
        !ExistingPPOpts.PCHThroughHeader.empty()) {
      // In case the through header is an include, we must add all the includes
      // to the predefines so the start point can be determined.
      SuggestedPredefines += "#include \"";
      SuggestedPredefines += File;
      SuggestedPredefines += "\"\n";
      continue;
    }

    if (File == ExistingPPOpts.ImplicitPCHInclude)
      continue;

    if (llvm::is_contained(PPOpts.Includes, File))
      continue;

    SuggestedPredefines += "#include \"";
    SuggestedPredefines += File;
    SuggestedPredefines += "\"\n";
  }

  for (unsigned I = 0, N = ExistingPPOpts.MacroIncludes.size(); I != N; ++I) {
    StringRef File = ExistingPPOpts.MacroIncludes[I];
    if (llvm::is_contained(PPOpts.MacroIncludes, File))
      continue;

    SuggestedPredefines += "#__include_macros \"";
    SuggestedPredefines += File;
    SuggestedPredefines += "\"\n##\n";
  }

  return false;
}

bool PCHValidator::ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                           bool ReadMacros, bool Complain,
                                           std::string &SuggestedPredefines) {
  const PreprocessorOptions &ExistingPPOpts = PP.getPreprocessorOpts();

  return checkPreprocessorOptions(
      PPOpts, ExistingPPOpts, ReadMacros, Complain ? &Reader.Diags : nullptr,
      PP.getFileManager(), SuggestedPredefines, PP.getLangOpts());
}

bool SimpleASTReaderListener::ReadPreprocessorOptions(
    const PreprocessorOptions &PPOpts, bool ReadMacros, bool Complain,
    std::string &SuggestedPredefines) {
  return checkPreprocessorOptions(PPOpts, PP.getPreprocessorOpts(), ReadMacros,
                                  nullptr, PP.getFileManager(),
                                  SuggestedPredefines, PP.getLangOpts(),
                                  OptionValidateNone);
}

/// Check that the specified and the existing module cache paths are equivalent.
///
/// \param Diags If non-null, produce diagnostics for any mismatches incurred.
/// \returns true when the module cache paths differ.
static bool checkModuleCachePath(llvm::vfs::FileSystem &VFS,
                                 StringRef SpecificModuleCachePath,
                                 StringRef ExistingModuleCachePath,
                                 DiagnosticsEngine *Diags,
                                 const LangOptions &LangOpts,
                                 const PreprocessorOptions &PPOpts) {
  if (!LangOpts.Modules || PPOpts.AllowPCHWithDifferentModulesCachePath ||
      SpecificModuleCachePath == ExistingModuleCachePath)
    return false;
  auto EqualOrErr =
      VFS.equivalent(SpecificModuleCachePath, ExistingModuleCachePath);
  if (EqualOrErr && *EqualOrErr)
    return false;
  if (Diags)
    Diags->Report(diag::err_pch_modulecache_mismatch)
        << SpecificModuleCachePath << ExistingModuleCachePath;
  return true;
}

bool PCHValidator::ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                           StringRef SpecificModuleCachePath,
                                           bool Complain) {
  return checkModuleCachePath(Reader.getFileManager().getVirtualFileSystem(),
                              SpecificModuleCachePath,
                              PP.getHeaderSearchInfo().getModuleCachePath(),
                              Complain ? &Reader.Diags : nullptr,
                              PP.getLangOpts(), PP.getPreprocessorOpts());
}

void PCHValidator::ReadCounter(const ModuleFile &M, unsigned Value) {
  PP.setCounterValue(Value);
}

//===----------------------------------------------------------------------===//
// AST reader implementation
//===----------------------------------------------------------------------===//

static uint64_t readULEB(const unsigned char *&P) {
  unsigned Length = 0;
  const char *Error = nullptr;

  uint64_t Val = llvm::decodeULEB128(P, &Length, nullptr, &Error);
  if (Error)
    llvm::report_fatal_error(Error);
  P += Length;
  return Val;
}

/// Read ULEB-encoded key length and data length.
static std::pair<unsigned, unsigned>
readULEBKeyDataLength(const unsigned char *&P) {
  unsigned KeyLen = readULEB(P);
  if ((unsigned)KeyLen != KeyLen)
    llvm::report_fatal_error("key too large");

  unsigned DataLen = readULEB(P);
  if ((unsigned)DataLen != DataLen)
    llvm::report_fatal_error("data too large");

  return std::make_pair(KeyLen, DataLen);
}

void ASTReader::setDeserializationListener(ASTDeserializationListener *Listener,
                                           bool TakeOwnership) {
  DeserializationListener = Listener;
  OwnsDeserializationListener = TakeOwnership;
}

unsigned ASTSelectorLookupTrait::ComputeHash(Selector Sel) {
  return serialization::ComputeHash(Sel);
}

LocalDeclID LocalDeclID::get(ASTReader &Reader, ModuleFile &MF, DeclID Value) {
  LocalDeclID ID(Value);
#ifndef NDEBUG
  if (!MF.ModuleOffsetMap.empty())
    Reader.ReadModuleOffsetMap(MF);

  unsigned ModuleFileIndex = ID.getModuleFileIndex();
  unsigned LocalDeclID = ID.getLocalDeclIndex();

  assert(ModuleFileIndex <= MF.TransitiveImports.size());

  ModuleFile *OwningModuleFile =
      ModuleFileIndex == 0 ? &MF : MF.TransitiveImports[ModuleFileIndex - 1];
  assert(OwningModuleFile);

  unsigned LocalNumDecls = OwningModuleFile->LocalNumDecls;

  if (!ModuleFileIndex)
    LocalNumDecls += NUM_PREDEF_DECL_IDS;

  assert(LocalDeclID < LocalNumDecls);
#endif
  (void)Reader;
  (void)MF;
  return ID;
}

LocalDeclID LocalDeclID::get(ASTReader &Reader, ModuleFile &MF,
                             unsigned ModuleFileIndex, unsigned LocalDeclID) {
  DeclID Value = (DeclID)ModuleFileIndex << 32 | (DeclID)LocalDeclID;
  return LocalDeclID::get(Reader, MF, Value);
}

std::pair<unsigned, unsigned>
ASTSelectorLookupTrait::ReadKeyDataLength(const unsigned char*& d) {
  return readULEBKeyDataLength(d);
}

ASTSelectorLookupTrait::internal_key_type
ASTSelectorLookupTrait::ReadKey(const unsigned char* d, unsigned) {
  using namespace llvm::support;

  SelectorTable &SelTable = Reader.getContext().Selectors;
  unsigned N = endian::readNext<uint16_t, llvm::endianness::little>(d);
  const IdentifierInfo *FirstII = Reader.getLocalIdentifier(
      F, endian::readNext<IdentifierID, llvm::endianness::little>(d));
  if (N == 0)
    return SelTable.getNullarySelector(FirstII);
  else if (N == 1)
    return SelTable.getUnarySelector(FirstII);

  SmallVector<const IdentifierInfo *, 16> Args;
  Args.push_back(FirstII);
  for (unsigned I = 1; I != N; ++I)
    Args.push_back(Reader.getLocalIdentifier(
        F, endian::readNext<IdentifierID, llvm::endianness::little>(d)));

  return SelTable.getSelector(N, Args.data());
}

ASTSelectorLookupTrait::data_type
ASTSelectorLookupTrait::ReadData(Selector, const unsigned char* d,
                                 unsigned DataLen) {
  using namespace llvm::support;

  data_type Result;

  Result.ID = Reader.getGlobalSelectorID(
      F, endian::readNext<uint32_t, llvm::endianness::little>(d));
  unsigned FullInstanceBits =
      endian::readNext<uint16_t, llvm::endianness::little>(d);
  unsigned FullFactoryBits =
      endian::readNext<uint16_t, llvm::endianness::little>(d);
  Result.InstanceBits = FullInstanceBits & 0x3;
  Result.InstanceHasMoreThanOneDecl = (FullInstanceBits >> 2) & 0x1;
  Result.FactoryBits = FullFactoryBits & 0x3;
  Result.FactoryHasMoreThanOneDecl = (FullFactoryBits >> 2) & 0x1;
  unsigned NumInstanceMethods = FullInstanceBits >> 3;
  unsigned NumFactoryMethods = FullFactoryBits >> 3;

  // Load instance methods
  for (unsigned I = 0; I != NumInstanceMethods; ++I) {
    if (ObjCMethodDecl *Method = Reader.GetLocalDeclAs<ObjCMethodDecl>(
            F, LocalDeclID::get(
                   Reader, F,
                   endian::readNext<DeclID, llvm::endianness::little>(d))))
      Result.Instance.push_back(Method);
  }

  // Load factory methods
  for (unsigned I = 0; I != NumFactoryMethods; ++I) {
    if (ObjCMethodDecl *Method = Reader.GetLocalDeclAs<ObjCMethodDecl>(
            F, LocalDeclID::get(
                   Reader, F,
                   endian::readNext<DeclID, llvm::endianness::little>(d))))
      Result.Factory.push_back(Method);
  }

  return Result;
}

unsigned ASTIdentifierLookupTraitBase::ComputeHash(const internal_key_type& a) {
  return llvm::djbHash(a);
}

std::pair<unsigned, unsigned>
ASTIdentifierLookupTraitBase::ReadKeyDataLength(const unsigned char*& d) {
  return readULEBKeyDataLength(d);
}

ASTIdentifierLookupTraitBase::internal_key_type
ASTIdentifierLookupTraitBase::ReadKey(const unsigned char* d, unsigned n) {
  assert(n >= 2 && d[n-1] == '\0');
  return StringRef((const char*) d, n-1);
}

/// Whether the given identifier is "interesting".
static bool isInterestingIdentifier(ASTReader &Reader, const IdentifierInfo &II,
                                    bool IsModule) {
  bool IsInteresting =
      II.getNotableIdentifierID() != tok::NotableIdentifierKind::not_notable ||
      II.getBuiltinID() != Builtin::ID::NotBuiltin ||
      II.getObjCKeywordID() != tok::ObjCKeywordKind::objc_not_keyword;
  return II.hadMacroDefinition() || II.isPoisoned() ||
         (!IsModule && IsInteresting) || II.hasRevertedTokenIDToIdentifier() ||
         (!(IsModule && Reader.getPreprocessor().getLangOpts().CPlusPlus) &&
          II.getFETokenInfo());
}

static bool readBit(unsigned &Bits) {
  bool Value = Bits & 0x1;
  Bits >>= 1;
  return Value;
}

IdentifierID ASTIdentifierLookupTrait::ReadIdentifierID(const unsigned char *d) {
  using namespace llvm::support;

  IdentifierID RawID =
      endian::readNext<IdentifierID, llvm::endianness::little>(d);
  return Reader.getGlobalIdentifierID(F, RawID >> 1);
}

static void markIdentifierFromAST(ASTReader &Reader, IdentifierInfo &II) {
  if (!II.isFromAST()) {
    II.setIsFromAST();
    bool IsModule = Reader.getPreprocessor().getCurrentModule() != nullptr;
    if (isInterestingIdentifier(Reader, II, IsModule))
      II.setChangedSinceDeserialization();
  }
}

IdentifierInfo *ASTIdentifierLookupTrait::ReadData(const internal_key_type& k,
                                                   const unsigned char* d,
                                                   unsigned DataLen) {
  using namespace llvm::support;

  IdentifierID RawID =
      endian::readNext<IdentifierID, llvm::endianness::little>(d);
  bool IsInteresting = RawID & 0x01;

  DataLen -= sizeof(IdentifierID);

  // Wipe out the "is interesting" bit.
  RawID = RawID >> 1;

  // Build the IdentifierInfo and link the identifier ID with it.
  IdentifierInfo *II = KnownII;
  if (!II) {
    II = &Reader.getIdentifierTable().getOwn(k);
    KnownII = II;
  }
  markIdentifierFromAST(Reader, *II);
  Reader.markIdentifierUpToDate(II);

  IdentifierID ID = Reader.getGlobalIdentifierID(F, RawID);
  if (!IsInteresting) {
    // For uninteresting identifiers, there's nothing else to do. Just notify
    // the reader that we've finished loading this identifier.
    Reader.SetIdentifierInfo(ID, II);
    return II;
  }

  unsigned ObjCOrBuiltinID =
      endian::readNext<uint16_t, llvm::endianness::little>(d);
  unsigned Bits = endian::readNext<uint16_t, llvm::endianness::little>(d);
  bool CPlusPlusOperatorKeyword = readBit(Bits);
  bool HasRevertedTokenIDToIdentifier = readBit(Bits);
  bool Poisoned = readBit(Bits);
  bool ExtensionToken = readBit(Bits);
  bool HadMacroDefinition = readBit(Bits);

  assert(Bits == 0 && "Extra bits in the identifier?");
  DataLen -= sizeof(uint16_t) * 2;

  // Set or check the various bits in the IdentifierInfo structure.
  // Token IDs are read-only.
  if (HasRevertedTokenIDToIdentifier && II->getTokenID() != tok::identifier)
    II->revertTokenIDToIdentifier();
  if (!F.isModule())
    II->setObjCOrBuiltinID(ObjCOrBuiltinID);
  assert(II->isExtensionToken() == ExtensionToken &&
         "Incorrect extension token flag");
  (void)ExtensionToken;
  if (Poisoned)
    II->setIsPoisoned(true);
  assert(II->isCPlusPlusOperatorKeyword() == CPlusPlusOperatorKeyword &&
         "Incorrect C++ operator keyword flag");
  (void)CPlusPlusOperatorKeyword;

  // If this identifier is a macro, deserialize the macro
  // definition.
  if (HadMacroDefinition) {
    uint32_t MacroDirectivesOffset =
        endian::readNext<uint32_t, llvm::endianness::little>(d);
    DataLen -= 4;

    Reader.addPendingMacro(II, &F, MacroDirectivesOffset);
  }

  Reader.SetIdentifierInfo(ID, II);

  // Read all of the declarations visible at global scope with this
  // name.
  if (DataLen > 0) {
    SmallVector<GlobalDeclID, 4> DeclIDs;
    for (; DataLen > 0; DataLen -= sizeof(DeclID))
      DeclIDs.push_back(Reader.getGlobalDeclID(
          F, LocalDeclID::get(
                 Reader, F,
                 endian::readNext<DeclID, llvm::endianness::little>(d))));
    Reader.SetGloballyVisibleDecls(II, DeclIDs);
  }

  return II;
}

DeclarationNameKey::DeclarationNameKey(DeclarationName Name)
    : Kind(Name.getNameKind()) {
  switch (Kind) {
  case DeclarationName::Identifier:
    Data = (uint64_t)Name.getAsIdentifierInfo();
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    Data = (uint64_t)Name.getObjCSelector().getAsOpaquePtr();
    break;
  case DeclarationName::CXXOperatorName:
    Data = Name.getCXXOverloadedOperator();
    break;
  case DeclarationName::CXXLiteralOperatorName:
    Data = (uint64_t)Name.getCXXLiteralIdentifier();
    break;
  case DeclarationName::CXXDeductionGuideName:
    Data = (uint64_t)Name.getCXXDeductionGuideTemplate()
               ->getDeclName().getAsIdentifierInfo();
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXUsingDirective:
    Data = 0;
    break;
  }
}

unsigned DeclarationNameKey::getHash() const {
  llvm::FoldingSetNodeID ID;
  ID.AddInteger(Kind);

  switch (Kind) {
  case DeclarationName::Identifier:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXDeductionGuideName:
    ID.AddString(((IdentifierInfo*)Data)->getName());
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    ID.AddInteger(serialization::ComputeHash(Selector(Data)));
    break;
  case DeclarationName::CXXOperatorName:
    ID.AddInteger((OverloadedOperatorKind)Data);
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXUsingDirective:
    break;
  }

  return ID.computeStableHash();
}

ModuleFile *
ASTDeclContextNameLookupTrait::ReadFileRef(const unsigned char *&d) {
  using namespace llvm::support;

  uint32_t ModuleFileID =
      endian::readNext<uint32_t, llvm::endianness::little>(d);
  return Reader.getLocalModuleFile(F, ModuleFileID);
}

std::pair<unsigned, unsigned>
ASTDeclContextNameLookupTrait::ReadKeyDataLength(const unsigned char *&d) {
  return readULEBKeyDataLength(d);
}

ASTDeclContextNameLookupTrait::internal_key_type
ASTDeclContextNameLookupTrait::ReadKey(const unsigned char *d, unsigned) {
  using namespace llvm::support;

  auto Kind = (DeclarationName::NameKind)*d++;
  uint64_t Data;
  switch (Kind) {
  case DeclarationName::Identifier:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXDeductionGuideName:
    Data = (uint64_t)Reader.getLocalIdentifier(
        F, endian::readNext<IdentifierID, llvm::endianness::little>(d));
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    Data = (uint64_t)Reader
               .getLocalSelector(
                   F, endian::readNext<uint32_t, llvm::endianness::little>(d))
               .getAsOpaquePtr();
    break;
  case DeclarationName::CXXOperatorName:
    Data = *d++; // OverloadedOperatorKind
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXUsingDirective:
    Data = 0;
    break;
  }

  return DeclarationNameKey(Kind, Data);
}

void ASTDeclContextNameLookupTrait::ReadDataInto(internal_key_type,
                                                 const unsigned char *d,
                                                 unsigned DataLen,
                                                 data_type_builder &Val) {
  using namespace llvm::support;

  for (unsigned NumDecls = DataLen / sizeof(DeclID); NumDecls; --NumDecls) {
    LocalDeclID ID = LocalDeclID::get(
        Reader, F, endian::readNext<DeclID, llvm::endianness::little>(d));
    Val.insert(Reader.getGlobalDeclID(F, ID));
  }
}

bool ASTReader::ReadLexicalDeclContextStorage(ModuleFile &M,
                                              BitstreamCursor &Cursor,
                                              uint64_t Offset,
                                              DeclContext *DC) {
  assert(Offset != 0);

  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(Offset)) {
    Error(std::move(Err));
    return true;
  }

  RecordData Record;
  StringRef Blob;
  Expected<unsigned> MaybeCode = Cursor.ReadCode();
  if (!MaybeCode) {
    Error(MaybeCode.takeError());
    return true;
  }
  unsigned Code = MaybeCode.get();

  Expected<unsigned> MaybeRecCode = Cursor.readRecord(Code, Record, &Blob);
  if (!MaybeRecCode) {
    Error(MaybeRecCode.takeError());
    return true;
  }
  unsigned RecCode = MaybeRecCode.get();
  if (RecCode != DECL_CONTEXT_LEXICAL) {
    Error("Expected lexical block");
    return true;
  }

  assert(!isa<TranslationUnitDecl>(DC) &&
         "expected a TU_UPDATE_LEXICAL record for TU");
  // If we are handling a C++ class template instantiation, we can see multiple
  // lexical updates for the same record. It's important that we select only one
  // of them, so that field numbering works properly. Just pick the first one we
  // see.
  auto &Lex = LexicalDecls[DC];
  if (!Lex.first) {
    Lex = std::make_pair(
        &M, llvm::ArrayRef(
                reinterpret_cast<const unaligned_decl_id_t *>(Blob.data()),
                Blob.size() / sizeof(DeclID)));
  }
  DC->setHasExternalLexicalStorage(true);
  return false;
}

bool ASTReader::ReadVisibleDeclContextStorage(ModuleFile &M,
                                              BitstreamCursor &Cursor,
                                              uint64_t Offset,
                                              GlobalDeclID ID) {
  assert(Offset != 0);

  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(Offset)) {
    Error(std::move(Err));
    return true;
  }

  RecordData Record;
  StringRef Blob;
  Expected<unsigned> MaybeCode = Cursor.ReadCode();
  if (!MaybeCode) {
    Error(MaybeCode.takeError());
    return true;
  }
  unsigned Code = MaybeCode.get();

  Expected<unsigned> MaybeRecCode = Cursor.readRecord(Code, Record, &Blob);
  if (!MaybeRecCode) {
    Error(MaybeRecCode.takeError());
    return true;
  }
  unsigned RecCode = MaybeRecCode.get();
  if (RecCode != DECL_CONTEXT_VISIBLE) {
    Error("Expected visible lookup table block");
    return true;
  }

  // We can't safely determine the primary context yet, so delay attaching the
  // lookup table until we're done with recursive deserialization.
  auto *Data = (const unsigned char*)Blob.data();
  PendingVisibleUpdates[ID].push_back(PendingVisibleUpdate{&M, Data});
  return false;
}

void ASTReader::Error(StringRef Msg) const {
  Error(diag::err_fe_pch_malformed, Msg);
  if (PP.getLangOpts().Modules && !Diags.isDiagnosticInFlight() &&
      !PP.getHeaderSearchInfo().getModuleCachePath().empty()) {
    Diag(diag::note_module_cache_path)
      << PP.getHeaderSearchInfo().getModuleCachePath();
  }
}

void ASTReader::Error(unsigned DiagID, StringRef Arg1, StringRef Arg2,
                      StringRef Arg3) const {
  if (Diags.isDiagnosticInFlight())
    Diags.SetDelayedDiagnostic(DiagID, Arg1, Arg2, Arg3);
  else
    Diag(DiagID) << Arg1 << Arg2 << Arg3;
}

void ASTReader::Error(llvm::Error &&Err) const {
  llvm::Error RemainingErr =
      handleErrors(std::move(Err), [this](const DiagnosticError &E) {
        auto Diag = E.getDiagnostic().second;

        // Ideally we'd just emit it, but have to handle a possible in-flight
        // diagnostic. Note that the location is currently ignored as well.
        auto NumArgs = Diag.getStorage()->NumDiagArgs;
        assert(NumArgs <= 3 && "Can only have up to 3 arguments");
        StringRef Arg1, Arg2, Arg3;
        switch (NumArgs) {
        case 3:
          Arg3 = Diag.getStringArg(2);
          [[fallthrough]];
        case 2:
          Arg2 = Diag.getStringArg(1);
          [[fallthrough]];
        case 1:
          Arg1 = Diag.getStringArg(0);
        }
        Error(Diag.getDiagID(), Arg1, Arg2, Arg3);
      });
  if (RemainingErr)
    Error(toString(std::move(RemainingErr)));
}

//===----------------------------------------------------------------------===//
// Source Manager Deserialization
//===----------------------------------------------------------------------===//

/// Read the line table in the source manager block.
void ASTReader::ParseLineTable(ModuleFile &F, const RecordData &Record) {
  unsigned Idx = 0;
  LineTableInfo &LineTable = SourceMgr.getLineTable();

  // Parse the file names
  std::map<int, int> FileIDs;
  FileIDs[-1] = -1; // For unspecified filenames.
  for (unsigned I = 0; Record[Idx]; ++I) {
    // Extract the file name
    auto Filename = ReadPath(F, Record, Idx);
    FileIDs[I] = LineTable.getLineTableFilenameID(Filename);
  }
  ++Idx;

  // Parse the line entries
  std::vector<LineEntry> Entries;
  while (Idx < Record.size()) {
    FileID FID = ReadFileID(F, Record, Idx);

    // Extract the line entries
    unsigned NumEntries = Record[Idx++];
    assert(NumEntries && "no line entries for file ID");
    Entries.clear();
    Entries.reserve(NumEntries);
    for (unsigned I = 0; I != NumEntries; ++I) {
      unsigned FileOffset = Record[Idx++];
      unsigned LineNo = Record[Idx++];
      int FilenameID = FileIDs[Record[Idx++]];
      SrcMgr::CharacteristicKind FileKind
        = (SrcMgr::CharacteristicKind)Record[Idx++];
      unsigned IncludeOffset = Record[Idx++];
      Entries.push_back(LineEntry::get(FileOffset, LineNo, FilenameID,
                                       FileKind, IncludeOffset));
    }
    LineTable.AddEntry(FID, Entries);
  }
}

/// Read a source manager block
llvm::Error ASTReader::ReadSourceManagerBlock(ModuleFile &F) {
  using namespace SrcMgr;

  BitstreamCursor &SLocEntryCursor = F.SLocEntryCursor;

  // Set the source-location entry cursor to the current position in
  // the stream. This cursor will be used to read the contents of the
  // source manager block initially, and then lazily read
  // source-location entries as needed.
  SLocEntryCursor = F.Stream;

  // The stream itself is going to skip over the source manager block.
  if (llvm::Error Err = F.Stream.SkipBlock())
    return Err;

  // Enter the source manager block.
  if (llvm::Error Err = SLocEntryCursor.EnterSubBlock(SOURCE_MANAGER_BLOCK_ID))
    return Err;
  F.SourceManagerBlockStartOffset = SLocEntryCursor.GetCurrentBitNo();

  RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeE =
        SLocEntryCursor.advanceSkippingSubblocks();
    if (!MaybeE)
      return MaybeE.takeError();
    llvm::BitstreamEntry E = MaybeE.get();

    switch (E.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      return llvm::createStringError(std::errc::illegal_byte_sequence,
                                     "malformed block record in AST file");
    case llvm::BitstreamEntry::EndBlock:
      return llvm::Error::success();
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecord =
        SLocEntryCursor.readRecord(E.ID, Record, &Blob);
    if (!MaybeRecord)
      return MaybeRecord.takeError();
    switch (MaybeRecord.get()) {
    default:  // Default behavior: ignore.
      break;

    case SM_SLOC_FILE_ENTRY:
    case SM_SLOC_BUFFER_ENTRY:
    case SM_SLOC_EXPANSION_ENTRY:
      // Once we hit one of the source location entries, we're done.
      return llvm::Error::success();
    }
  }
}

llvm::Expected<SourceLocation::UIntTy>
ASTReader::readSLocOffset(ModuleFile *F, unsigned Index) {
  BitstreamCursor &Cursor = F->SLocEntryCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(F->SLocEntryOffsetsBase +
                                         F->SLocEntryOffsets[Index]))
    return std::move(Err);

  Expected<llvm::BitstreamEntry> MaybeEntry = Cursor.advance();
  if (!MaybeEntry)
    return MaybeEntry.takeError();

  llvm::BitstreamEntry Entry = MaybeEntry.get();
  if (Entry.Kind != llvm::BitstreamEntry::Record)
    return llvm::createStringError(
        std::errc::illegal_byte_sequence,
        "incorrectly-formatted source location entry in AST file");

  RecordData Record;
  StringRef Blob;
  Expected<unsigned> MaybeSLOC = Cursor.readRecord(Entry.ID, Record, &Blob);
  if (!MaybeSLOC)
    return MaybeSLOC.takeError();

  switch (MaybeSLOC.get()) {
  default:
    return llvm::createStringError(
        std::errc::illegal_byte_sequence,
        "incorrectly-formatted source location entry in AST file");
  case SM_SLOC_FILE_ENTRY:
  case SM_SLOC_BUFFER_ENTRY:
  case SM_SLOC_EXPANSION_ENTRY:
    return F->SLocEntryBaseOffset + Record[0];
  }
}

int ASTReader::getSLocEntryID(SourceLocation::UIntTy SLocOffset) {
  auto SLocMapI =
      GlobalSLocOffsetMap.find(SourceManager::MaxLoadedOffset - SLocOffset - 1);
  assert(SLocMapI != GlobalSLocOffsetMap.end() &&
         "Corrupted global sloc offset map");
  ModuleFile *F = SLocMapI->second;

  bool Invalid = false;

  auto It = llvm::upper_bound(
      llvm::index_range(0, F->LocalNumSLocEntries), SLocOffset,
      [&](SourceLocation::UIntTy Offset, std::size_t LocalIndex) {
        int ID = F->SLocEntryBaseID + LocalIndex;
        std::size_t Index = -ID - 2;
        if (!SourceMgr.SLocEntryOffsetLoaded[Index]) {
          assert(!SourceMgr.SLocEntryLoaded[Index]);
          auto MaybeEntryOffset = readSLocOffset(F, LocalIndex);
          if (!MaybeEntryOffset) {
            Error(MaybeEntryOffset.takeError());
            Invalid = true;
            return true;
          }
          SourceMgr.LoadedSLocEntryTable[Index] =
              SrcMgr::SLocEntry::getOffsetOnly(*MaybeEntryOffset);
          SourceMgr.SLocEntryOffsetLoaded[Index] = true;
        }
        return Offset < SourceMgr.LoadedSLocEntryTable[Index].getOffset();
      });

  if (Invalid)
    return 0;

  // The iterator points to the first entry with start offset greater than the
  // offset of interest. The previous entry must contain the offset of interest.
  return F->SLocEntryBaseID + *std::prev(It);
}

bool ASTReader::ReadSLocEntry(int ID) {
  if (ID == 0)
    return false;

  if (unsigned(-ID) - 2 >= getTotalNumSLocs() || ID > 0) {
    Error("source location entry ID out-of-range for AST file");
    return true;
  }

  // Local helper to read the (possibly-compressed) buffer data following the
  // entry record.
  auto ReadBuffer = [this](
      BitstreamCursor &SLocEntryCursor,
      StringRef Name) -> std::unique_ptr<llvm::MemoryBuffer> {
    RecordData Record;
    StringRef Blob;
    Expected<unsigned> MaybeCode = SLocEntryCursor.ReadCode();
    if (!MaybeCode) {
      Error(MaybeCode.takeError());
      return nullptr;
    }
    unsigned Code = MaybeCode.get();

    Expected<unsigned> MaybeRecCode =
        SLocEntryCursor.readRecord(Code, Record, &Blob);
    if (!MaybeRecCode) {
      Error(MaybeRecCode.takeError());
      return nullptr;
    }
    unsigned RecCode = MaybeRecCode.get();

    if (RecCode == SM_SLOC_BUFFER_BLOB_COMPRESSED) {
      // Inspect the first byte to differentiate zlib (\x78) and zstd
      // (little-endian 0xFD2FB528).
      const llvm::compression::Format F =
          Blob.size() > 0 && Blob.data()[0] == 0x78
              ? llvm::compression::Format::Zlib
              : llvm::compression::Format::Zstd;
      if (const char *Reason = llvm::compression::getReasonIfUnsupported(F)) {
        Error(Reason);
        return nullptr;
      }
      SmallVector<uint8_t, 0> Decompressed;
      if (llvm::Error E = llvm::compression::decompress(
              F, llvm::arrayRefFromStringRef(Blob), Decompressed, Record[0])) {
        Error("could not decompress embedded file contents: " +
              llvm::toString(std::move(E)));
        return nullptr;
      }
      return llvm::MemoryBuffer::getMemBufferCopy(
          llvm::toStringRef(Decompressed), Name);
    } else if (RecCode == SM_SLOC_BUFFER_BLOB) {
      return llvm::MemoryBuffer::getMemBuffer(Blob.drop_back(1), Name, true);
    } else {
      Error("AST record has invalid code");
      return nullptr;
    }
  };

  ModuleFile *F = GlobalSLocEntryMap.find(-ID)->second;
  if (llvm::Error Err = F->SLocEntryCursor.JumpToBit(
          F->SLocEntryOffsetsBase +
          F->SLocEntryOffsets[ID - F->SLocEntryBaseID])) {
    Error(std::move(Err));
    return true;
  }

  BitstreamCursor &SLocEntryCursor = F->SLocEntryCursor;
  SourceLocation::UIntTy BaseOffset = F->SLocEntryBaseOffset;

  ++NumSLocEntriesRead;
  Expected<llvm::BitstreamEntry> MaybeEntry = SLocEntryCursor.advance();
  if (!MaybeEntry) {
    Error(MaybeEntry.takeError());
    return true;
  }
  llvm::BitstreamEntry Entry = MaybeEntry.get();

  if (Entry.Kind != llvm::BitstreamEntry::Record) {
    Error("incorrectly-formatted source location entry in AST file");
    return true;
  }

  RecordData Record;
  StringRef Blob;
  Expected<unsigned> MaybeSLOC =
      SLocEntryCursor.readRecord(Entry.ID, Record, &Blob);
  if (!MaybeSLOC) {
    Error(MaybeSLOC.takeError());
    return true;
  }
  switch (MaybeSLOC.get()) {
  default:
    Error("incorrectly-formatted source location entry in AST file");
    return true;

  case SM_SLOC_FILE_ENTRY: {
    // We will detect whether a file changed and return 'Failure' for it, but
    // we will also try to fail gracefully by setting up the SLocEntry.
    unsigned InputID = Record[4];
    InputFile IF = getInputFile(*F, InputID);
    OptionalFileEntryRef File = IF.getFile();
    bool OverriddenBuffer = IF.isOverridden();

    // Note that we only check if a File was returned. If it was out-of-date
    // we have complained but we will continue creating a FileID to recover
    // gracefully.
    if (!File)
      return true;

    SourceLocation IncludeLoc = ReadSourceLocation(*F, Record[1]);
    if (IncludeLoc.isInvalid() && F->Kind != MK_MainFile) {
      // This is the module's main file.
      IncludeLoc = getImportLocation(F);
    }
    SrcMgr::CharacteristicKind
      FileCharacter = (SrcMgr::CharacteristicKind)Record[2];
    FileID FID = SourceMgr.createFileID(*File, IncludeLoc, FileCharacter, ID,
                                        BaseOffset + Record[0]);
    SrcMgr::FileInfo &FileInfo = SourceMgr.getSLocEntry(FID).getFile();
    FileInfo.NumCreatedFIDs = Record[5];
    if (Record[3])
      FileInfo.setHasLineDirectives();

    unsigned NumFileDecls = Record[7];
    if (NumFileDecls && ContextObj) {
      const unaligned_decl_id_t *FirstDecl = F->FileSortedDecls + Record[6];
      assert(F->FileSortedDecls && "FILE_SORTED_DECLS not encountered yet ?");
      FileDeclIDs[FID] =
          FileDeclsInfo(F, llvm::ArrayRef(FirstDecl, NumFileDecls));
    }

    const SrcMgr::ContentCache &ContentCache =
        SourceMgr.getOrCreateContentCache(*File, isSystem(FileCharacter));
    if (OverriddenBuffer && !ContentCache.BufferOverridden &&
        ContentCache.ContentsEntry == ContentCache.OrigEntry &&
        !ContentCache.getBufferIfLoaded()) {
      auto Buffer = ReadBuffer(SLocEntryCursor, File->getName());
      if (!Buffer)
        return true;
      SourceMgr.overrideFileContents(*File, std::move(Buffer));
    }

    break;
  }

  case SM_SLOC_BUFFER_ENTRY: {
    const char *Name = Blob.data();
    unsigned Offset = Record[0];
    SrcMgr::CharacteristicKind
      FileCharacter = (SrcMgr::CharacteristicKind)Record[2];
    SourceLocation IncludeLoc = ReadSourceLocation(*F, Record[1]);
    if (IncludeLoc.isInvalid() && F->isModule()) {
      IncludeLoc = getImportLocation(F);
    }

    auto Buffer = ReadBuffer(SLocEntryCursor, Name);
    if (!Buffer)
      return true;
    FileID FID = SourceMgr.createFileID(std::move(Buffer), FileCharacter, ID,
                                        BaseOffset + Offset, IncludeLoc);
    if (Record[3]) {
      auto &FileInfo = SourceMgr.getSLocEntry(FID).getFile();
      FileInfo.setHasLineDirectives();
    }
    break;
  }

  case SM_SLOC_EXPANSION_ENTRY: {
    LocSeq::State Seq;
    SourceLocation SpellingLoc = ReadSourceLocation(*F, Record[1], Seq);
    SourceLocation ExpansionBegin = ReadSourceLocation(*F, Record[2], Seq);
    SourceLocation ExpansionEnd = ReadSourceLocation(*F, Record[3], Seq);
    SourceMgr.createExpansionLoc(SpellingLoc, ExpansionBegin, ExpansionEnd,
                                 Record[5], Record[4], ID,
                                 BaseOffset + Record[0]);
    break;
  }
  }

  return false;
}

std::pair<SourceLocation, StringRef> ASTReader::getModuleImportLoc(int ID) {
  if (ID == 0)
    return std::make_pair(SourceLocation(), "");

  if (unsigned(-ID) - 2 >= getTotalNumSLocs() || ID > 0) {
    Error("source location entry ID out-of-range for AST file");
    return std::make_pair(SourceLocation(), "");
  }

  // Find which module file this entry lands in.
  ModuleFile *M = GlobalSLocEntryMap.find(-ID)->second;
  if (!M->isModule())
    return std::make_pair(SourceLocation(), "");

  // FIXME: Can we map this down to a particular submodule? That would be
  // ideal.
  return std::make_pair(M->ImportLoc, StringRef(M->ModuleName));
}

/// Find the location where the module F is imported.
SourceLocation ASTReader::getImportLocation(ModuleFile *F) {
  if (F->ImportLoc.isValid())
    return F->ImportLoc;

  // Otherwise we have a PCH. It's considered to be "imported" at the first
  // location of its includer.
  if (F->ImportedBy.empty() || !F->ImportedBy[0]) {
    // Main file is the importer.
    assert(SourceMgr.getMainFileID().isValid() && "missing main file");
    return SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID());
  }
  return F->ImportedBy[0]->FirstLoc;
}

/// Enter a subblock of the specified BlockID with the specified cursor. Read
/// the abbreviations that are at the top of the block and then leave the cursor
/// pointing into the block.
llvm::Error ASTReader::ReadBlockAbbrevs(BitstreamCursor &Cursor,
                                        unsigned BlockID,
                                        uint64_t *StartOfBlockOffset) {
  if (llvm::Error Err = Cursor.EnterSubBlock(BlockID))
    return Err;

  if (StartOfBlockOffset)
    *StartOfBlockOffset = Cursor.GetCurrentBitNo();

  while (true) {
    uint64_t Offset = Cursor.GetCurrentBitNo();
    Expected<unsigned> MaybeCode = Cursor.ReadCode();
    if (!MaybeCode)
      return MaybeCode.takeError();
    unsigned Code = MaybeCode.get();

    // We expect all abbrevs to be at the start of the block.
    if (Code != llvm::bitc::DEFINE_ABBREV) {
      if (llvm::Error Err = Cursor.JumpToBit(Offset))
        return Err;
      return llvm::Error::success();
    }
    if (llvm::Error Err = Cursor.ReadAbbrevRecord())
      return Err;
  }
}

Token ASTReader::ReadToken(ModuleFile &M, const RecordDataImpl &Record,
                           unsigned &Idx) {
  Token Tok;
  Tok.startToken();
  Tok.setLocation(ReadSourceLocation(M, Record, Idx));
  Tok.setKind((tok::TokenKind)Record[Idx++]);
  Tok.setFlag((Token::TokenFlags)Record[Idx++]);

  if (Tok.isAnnotation()) {
    Tok.setAnnotationEndLoc(ReadSourceLocation(M, Record, Idx));
    switch (Tok.getKind()) {
    case tok::annot_pragma_loop_hint: {
      auto *Info = new (PP.getPreprocessorAllocator()) PragmaLoopHintInfo;
      Info->PragmaName = ReadToken(M, Record, Idx);
      Info->Option = ReadToken(M, Record, Idx);
      unsigned NumTokens = Record[Idx++];
      SmallVector<Token, 4> Toks;
      Toks.reserve(NumTokens);
      for (unsigned I = 0; I < NumTokens; ++I)
        Toks.push_back(ReadToken(M, Record, Idx));
      Info->Toks = llvm::ArrayRef(Toks).copy(PP.getPreprocessorAllocator());
      Tok.setAnnotationValue(static_cast<void *>(Info));
      break;
    }
    case tok::annot_pragma_pack: {
      auto *Info = new (PP.getPreprocessorAllocator()) Sema::PragmaPackInfo;
      Info->Action = static_cast<Sema::PragmaMsStackAction>(Record[Idx++]);
      auto SlotLabel = ReadString(Record, Idx);
      Info->SlotLabel =
          llvm::StringRef(SlotLabel).copy(PP.getPreprocessorAllocator());
      Info->Alignment = ReadToken(M, Record, Idx);
      Tok.setAnnotationValue(static_cast<void *>(Info));
      break;
    }
    // Some annotation tokens do not use the PtrData field.
    case tok::annot_pragma_openmp:
    case tok::annot_pragma_openmp_end:
    case tok::annot_pragma_unused:
    case tok::annot_pragma_openacc:
    case tok::annot_pragma_openacc_end:
      break;
    default:
      llvm_unreachable("missing deserialization code for annotation token");
    }
  } else {
    Tok.setLength(Record[Idx++]);
    if (IdentifierInfo *II = getLocalIdentifier(M, Record[Idx++]))
      Tok.setIdentifierInfo(II);
  }
  return Tok;
}

MacroInfo *ASTReader::ReadMacroRecord(ModuleFile &F, uint64_t Offset) {
  BitstreamCursor &Stream = F.MacroCursor;

  // Keep track of where we are in the stream, then jump back there
  // after reading this macro.
  SavedStreamPosition SavedPosition(Stream);

  if (llvm::Error Err = Stream.JumpToBit(Offset)) {
    // FIXME this drops errors on the floor.
    consumeError(std::move(Err));
    return nullptr;
  }
  RecordData Record;
  SmallVector<IdentifierInfo*, 16> MacroParams;
  MacroInfo *Macro = nullptr;
  llvm::MutableArrayRef<Token> MacroTokens;

  while (true) {
    // Advance to the next record, but if we get to the end of the block, don't
    // pop it (removing all the abbreviations from the cursor) since we want to
    // be able to reseek within the block and read entries.
    unsigned Flags = BitstreamCursor::AF_DontPopBlockAtEnd;
    Expected<llvm::BitstreamEntry> MaybeEntry =
        Stream.advanceSkippingSubblocks(Flags);
    if (!MaybeEntry) {
      Error(MaybeEntry.takeError());
      return Macro;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return Macro;
    case llvm::BitstreamEntry::EndBlock:
      return Macro;
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    PreprocessorRecordTypes RecType;
    if (Expected<unsigned> MaybeRecType = Stream.readRecord(Entry.ID, Record))
      RecType = (PreprocessorRecordTypes)MaybeRecType.get();
    else {
      Error(MaybeRecType.takeError());
      return Macro;
    }
    switch (RecType) {
    case PP_MODULE_MACRO:
    case PP_MACRO_DIRECTIVE_HISTORY:
      return Macro;

    case PP_MACRO_OBJECT_LIKE:
    case PP_MACRO_FUNCTION_LIKE: {
      // If we already have a macro, that means that we've hit the end
      // of the definition of the macro we were looking for. We're
      // done.
      if (Macro)
        return Macro;

      unsigned NextIndex = 1; // Skip identifier ID.
      SourceLocation Loc = ReadSourceLocation(F, Record, NextIndex);
      MacroInfo *MI = PP.AllocateMacroInfo(Loc);
      MI->setDefinitionEndLoc(ReadSourceLocation(F, Record, NextIndex));
      MI->setIsUsed(Record[NextIndex++]);
      MI->setUsedForHeaderGuard(Record[NextIndex++]);
      MacroTokens = MI->allocateTokens(Record[NextIndex++],
                                       PP.getPreprocessorAllocator());
      if (RecType == PP_MACRO_FUNCTION_LIKE) {
        // Decode function-like macro info.
        bool isC99VarArgs = Record[NextIndex++];
        bool isGNUVarArgs = Record[NextIndex++];
        bool hasCommaPasting = Record[NextIndex++];
        MacroParams.clear();
        unsigned NumArgs = Record[NextIndex++];
        for (unsigned i = 0; i != NumArgs; ++i)
          MacroParams.push_back(getLocalIdentifier(F, Record[NextIndex++]));

        // Install function-like macro info.
        MI->setIsFunctionLike();
        if (isC99VarArgs) MI->setIsC99Varargs();
        if (isGNUVarArgs) MI->setIsGNUVarargs();
        if (hasCommaPasting) MI->setHasCommaPasting();
        MI->setParameterList(MacroParams, PP.getPreprocessorAllocator());
      }

      // Remember that we saw this macro last so that we add the tokens that
      // form its body to it.
      Macro = MI;

      if (NextIndex + 1 == Record.size() && PP.getPreprocessingRecord() &&
          Record[NextIndex]) {
        // We have a macro definition. Register the association
        PreprocessedEntityID
            GlobalID = getGlobalPreprocessedEntityID(F, Record[NextIndex]);
        PreprocessingRecord &PPRec = *PP.getPreprocessingRecord();
        PreprocessingRecord::PPEntityID PPID =
            PPRec.getPPEntityID(GlobalID - 1, /*isLoaded=*/true);
        MacroDefinitionRecord *PPDef = cast_or_null<MacroDefinitionRecord>(
            PPRec.getPreprocessedEntity(PPID));
        if (PPDef)
          PPRec.RegisterMacroDefinition(Macro, PPDef);
      }

      ++NumMacrosRead;
      break;
    }

    case PP_TOKEN: {
      // If we see a TOKEN before a PP_MACRO_*, then the file is
      // erroneous, just pretend we didn't see this.
      if (!Macro) break;
      if (MacroTokens.empty()) {
        Error("unexpected number of macro tokens for a macro in AST file");
        return Macro;
      }

      unsigned Idx = 0;
      MacroTokens[0] = ReadToken(F, Record, Idx);
      MacroTokens = MacroTokens.drop_front();
      break;
    }
    }
  }
}

PreprocessedEntityID
ASTReader::getGlobalPreprocessedEntityID(ModuleFile &M,
                                         unsigned LocalID) const {
  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::const_iterator
    I = M.PreprocessedEntityRemap.find(LocalID - NUM_PREDEF_PP_ENTITY_IDS);
  assert(I != M.PreprocessedEntityRemap.end()
         && "Invalid index into preprocessed entity index remap");

  return LocalID + I->second;
}

const FileEntry *HeaderFileInfoTrait::getFile(const internal_key_type &Key) {
  FileManager &FileMgr = Reader.getFileManager();
  if (!Key.Imported) {
    if (auto File = FileMgr.getFile(Key.Filename))
      return *File;
    return nullptr;
  }

  std::string Resolved = std::string(Key.Filename);
  Reader.ResolveImportedPath(M, Resolved);
  if (auto File = FileMgr.getFile(Resolved))
    return *File;
  return nullptr;
}

unsigned HeaderFileInfoTrait::ComputeHash(internal_key_ref ikey) {
  uint8_t buf[sizeof(ikey.Size) + sizeof(ikey.ModTime)];
  memcpy(buf, &ikey.Size, sizeof(ikey.Size));
  memcpy(buf + sizeof(ikey.Size), &ikey.ModTime, sizeof(ikey.ModTime));
  return llvm::xxh3_64bits(buf);
}

HeaderFileInfoTrait::internal_key_type
HeaderFileInfoTrait::GetInternalKey(external_key_type ekey) {
  internal_key_type ikey = {ekey.getSize(),
                            M.HasTimestamps ? ekey.getModificationTime() : 0,
                            ekey.getName(), /*Imported*/ false};
  return ikey;
}

bool HeaderFileInfoTrait::EqualKey(internal_key_ref a, internal_key_ref b) {
  if (a.Size != b.Size || (a.ModTime && b.ModTime && a.ModTime != b.ModTime))
    return false;

  if (llvm::sys::path::is_absolute(a.Filename) && a.Filename == b.Filename)
    return true;

  // Determine whether the actual files are equivalent.
  const FileEntry *FEA = getFile(a);
  const FileEntry *FEB = getFile(b);
  return FEA && FEA == FEB;
}

std::pair<unsigned, unsigned>
HeaderFileInfoTrait::ReadKeyDataLength(const unsigned char*& d) {
  return readULEBKeyDataLength(d);
}

HeaderFileInfoTrait::internal_key_type
HeaderFileInfoTrait::ReadKey(const unsigned char *d, unsigned) {
  using namespace llvm::support;

  internal_key_type ikey;
  ikey.Size = off_t(endian::readNext<uint64_t, llvm::endianness::little>(d));
  ikey.ModTime =
      time_t(endian::readNext<uint64_t, llvm::endianness::little>(d));
  ikey.Filename = (const char *)d;
  ikey.Imported = true;
  return ikey;
}

HeaderFileInfoTrait::data_type
HeaderFileInfoTrait::ReadData(internal_key_ref key, const unsigned char *d,
                              unsigned DataLen) {
  using namespace llvm::support;

  const unsigned char *End = d + DataLen;
  HeaderFileInfo HFI;
  unsigned Flags = *d++;

  bool Included = (Flags >> 6) & 0x01;
  if (Included)
    if (const FileEntry *FE = getFile(key))
      // Not using \c Preprocessor::markIncluded(), since that would attempt to
      // deserialize this header file info again.
      Reader.getPreprocessor().getIncludedFiles().insert(FE);

  // FIXME: Refactor with mergeHeaderFileInfo in HeaderSearch.cpp.
  HFI.isImport |= (Flags >> 5) & 0x01;
  HFI.isPragmaOnce |= (Flags >> 4) & 0x01;
  HFI.DirInfo = (Flags >> 1) & 0x07;
  HFI.IndexHeaderMapHeader = Flags & 0x01;
  HFI.LazyControllingMacro = Reader.getGlobalIdentifierID(
      M, endian::readNext<IdentifierID, llvm::endianness::little>(d));
  if (unsigned FrameworkOffset =
          endian::readNext<uint32_t, llvm::endianness::little>(d)) {
    // The framework offset is 1 greater than the actual offset,
    // since 0 is used as an indicator for "no framework name".
    StringRef FrameworkName(FrameworkStrings + FrameworkOffset - 1);
    HFI.Framework = HS->getUniqueFrameworkName(FrameworkName);
  }

  assert((End - d) % 4 == 0 &&
         "Wrong data length in HeaderFileInfo deserialization");
  while (d != End) {
    uint32_t LocalSMID =
        endian::readNext<uint32_t, llvm::endianness::little>(d);
    auto HeaderRole = static_cast<ModuleMap::ModuleHeaderRole>(LocalSMID & 7);
    LocalSMID >>= 3;

    // This header is part of a module. Associate it with the module to enable
    // implicit module import.
    SubmoduleID GlobalSMID = Reader.getGlobalSubmoduleID(M, LocalSMID);
    Module *Mod = Reader.getSubmodule(GlobalSMID);
    FileManager &FileMgr = Reader.getFileManager();
    ModuleMap &ModMap =
        Reader.getPreprocessor().getHeaderSearchInfo().getModuleMap();

    std::string Filename = std::string(key.Filename);
    if (key.Imported)
      Reader.ResolveImportedPath(M, Filename);
    if (auto FE = FileMgr.getOptionalFileRef(Filename)) {
      // FIXME: NameAsWritten
      Module::Header H = {std::string(key.Filename), "", *FE};
      ModMap.addHeader(Mod, H, HeaderRole, /*Imported=*/true);
    }
    HFI.mergeModuleMembership(HeaderRole);
  }

  // This HeaderFileInfo was externally loaded.
  HFI.External = true;
  HFI.IsValid = true;
  return HFI;
}

void ASTReader::addPendingMacro(IdentifierInfo *II, ModuleFile *M,
                                uint32_t MacroDirectivesOffset) {
  assert(NumCurrentElementsDeserializing > 0 &&"Missing deserialization guard");
  PendingMacroIDs[II].push_back(PendingMacroInfo(M, MacroDirectivesOffset));
}

void ASTReader::ReadDefinedMacros() {
  // Note that we are loading defined macros.
  Deserializing Macros(this);

  for (ModuleFile &I : llvm::reverse(ModuleMgr)) {
    BitstreamCursor &MacroCursor = I.MacroCursor;

    // If there was no preprocessor block, skip this file.
    if (MacroCursor.getBitcodeBytes().empty())
      continue;

    BitstreamCursor Cursor = MacroCursor;
    if (llvm::Error Err = Cursor.JumpToBit(I.MacroStartOffset)) {
      Error(std::move(Err));
      return;
    }

    RecordData Record;
    while (true) {
      Expected<llvm::BitstreamEntry> MaybeE = Cursor.advanceSkippingSubblocks();
      if (!MaybeE) {
        Error(MaybeE.takeError());
        return;
      }
      llvm::BitstreamEntry E = MaybeE.get();

      switch (E.Kind) {
      case llvm::BitstreamEntry::SubBlock: // Handled for us already.
      case llvm::BitstreamEntry::Error:
        Error("malformed block record in AST file");
        return;
      case llvm::BitstreamEntry::EndBlock:
        goto NextCursor;

      case llvm::BitstreamEntry::Record: {
        Record.clear();
        Expected<unsigned> MaybeRecord = Cursor.readRecord(E.ID, Record);
        if (!MaybeRecord) {
          Error(MaybeRecord.takeError());
          return;
        }
        switch (MaybeRecord.get()) {
        default:  // Default behavior: ignore.
          break;

        case PP_MACRO_OBJECT_LIKE:
        case PP_MACRO_FUNCTION_LIKE: {
          IdentifierInfo *II = getLocalIdentifier(I, Record[0]);
          if (II->isOutOfDate())
            updateOutOfDateIdentifier(*II);
          break;
        }

        case PP_TOKEN:
          // Ignore tokens.
          break;
        }
        break;
      }
      }
    }
    NextCursor:  ;
  }
}

namespace {

  /// Visitor class used to look up identifirs in an AST file.
  class IdentifierLookupVisitor {
    StringRef Name;
    unsigned NameHash;
    unsigned PriorGeneration;
    unsigned &NumIdentifierLookups;
    unsigned &NumIdentifierLookupHits;
    IdentifierInfo *Found = nullptr;

  public:
    IdentifierLookupVisitor(StringRef Name, unsigned PriorGeneration,
                            unsigned &NumIdentifierLookups,
                            unsigned &NumIdentifierLookupHits)
      : Name(Name), NameHash(ASTIdentifierLookupTrait::ComputeHash(Name)),
        PriorGeneration(PriorGeneration),
        NumIdentifierLookups(NumIdentifierLookups),
        NumIdentifierLookupHits(NumIdentifierLookupHits) {}

    bool operator()(ModuleFile &M) {
      // If we've already searched this module file, skip it now.
      if (M.Generation <= PriorGeneration)
        return true;

      ASTIdentifierLookupTable *IdTable
        = (ASTIdentifierLookupTable *)M.IdentifierLookupTable;
      if (!IdTable)
        return false;

      ASTIdentifierLookupTrait Trait(IdTable->getInfoObj().getReader(), M,
                                     Found);
      ++NumIdentifierLookups;
      ASTIdentifierLookupTable::iterator Pos =
          IdTable->find_hashed(Name, NameHash, &Trait);
      if (Pos == IdTable->end())
        return false;

      // Dereferencing the iterator has the effect of building the
      // IdentifierInfo node and populating it with the various
      // declarations it needs.
      ++NumIdentifierLookupHits;
      Found = *Pos;
      return true;
    }

    // Retrieve the identifier info found within the module
    // files.
    IdentifierInfo *getIdentifierInfo() const { return Found; }
  };

} // namespace

void ASTReader::updateOutOfDateIdentifier(const IdentifierInfo &II) {
  // Note that we are loading an identifier.
  Deserializing AnIdentifier(this);

  unsigned PriorGeneration = 0;
  if (getContext().getLangOpts().Modules)
    PriorGeneration = IdentifierGeneration[&II];

  // If there is a global index, look there first to determine which modules
  // provably do not have any results for this identifier.
  GlobalModuleIndex::HitSet Hits;
  GlobalModuleIndex::HitSet *HitsPtr = nullptr;
  if (!loadGlobalIndex()) {
    if (GlobalIndex->lookupIdentifier(II.getName(), Hits)) {
      HitsPtr = &Hits;
    }
  }

  IdentifierLookupVisitor Visitor(II.getName(), PriorGeneration,
                                  NumIdentifierLookups,
                                  NumIdentifierLookupHits);
  ModuleMgr.visit(Visitor, HitsPtr);
  markIdentifierUpToDate(&II);
}

void ASTReader::markIdentifierUpToDate(const IdentifierInfo *II) {
  if (!II)
    return;

  const_cast<IdentifierInfo *>(II)->setOutOfDate(false);

  // Update the generation for this identifier.
  if (getContext().getLangOpts().Modules)
    IdentifierGeneration[II] = getGeneration();
}

void ASTReader::resolvePendingMacro(IdentifierInfo *II,
                                    const PendingMacroInfo &PMInfo) {
  ModuleFile &M = *PMInfo.M;

  BitstreamCursor &Cursor = M.MacroCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err =
          Cursor.JumpToBit(M.MacroOffsetsBase + PMInfo.MacroDirectivesOffset)) {
    Error(std::move(Err));
    return;
  }

  struct ModuleMacroRecord {
    SubmoduleID SubModID;
    MacroInfo *MI;
    SmallVector<SubmoduleID, 8> Overrides;
  };
  llvm::SmallVector<ModuleMacroRecord, 8> ModuleMacros;

  // We expect to see a sequence of PP_MODULE_MACRO records listing exported
  // macros, followed by a PP_MACRO_DIRECTIVE_HISTORY record with the complete
  // macro histroy.
  RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry =
        Cursor.advance(BitstreamCursor::AF_DontPopBlockAtEnd);
    if (!MaybeEntry) {
      Error(MaybeEntry.takeError());
      return;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    if (Entry.Kind != llvm::BitstreamEntry::Record) {
      Error("malformed block record in AST file");
      return;
    }

    Record.clear();
    Expected<unsigned> MaybePP = Cursor.readRecord(Entry.ID, Record);
    if (!MaybePP) {
      Error(MaybePP.takeError());
      return;
    }
    switch ((PreprocessorRecordTypes)MaybePP.get()) {
    case PP_MACRO_DIRECTIVE_HISTORY:
      break;

    case PP_MODULE_MACRO: {
      ModuleMacros.push_back(ModuleMacroRecord());
      auto &Info = ModuleMacros.back();
      Info.SubModID = getGlobalSubmoduleID(M, Record[0]);
      Info.MI = getMacro(getGlobalMacroID(M, Record[1]));
      for (int I = 2, N = Record.size(); I != N; ++I)
        Info.Overrides.push_back(getGlobalSubmoduleID(M, Record[I]));
      continue;
    }

    default:
      Error("malformed block record in AST file");
      return;
    }

    // We found the macro directive history; that's the last record
    // for this macro.
    break;
  }

  // Module macros are listed in reverse dependency order.
  {
    std::reverse(ModuleMacros.begin(), ModuleMacros.end());
    llvm::SmallVector<ModuleMacro*, 8> Overrides;
    for (auto &MMR : ModuleMacros) {
      Overrides.clear();
      for (unsigned ModID : MMR.Overrides) {
        Module *Mod = getSubmodule(ModID);
        auto *Macro = PP.getModuleMacro(Mod, II);
        assert(Macro && "missing definition for overridden macro");
        Overrides.push_back(Macro);
      }

      bool Inserted = false;
      Module *Owner = getSubmodule(MMR.SubModID);
      PP.addModuleMacro(Owner, II, MMR.MI, Overrides, Inserted);
    }
  }

  // Don't read the directive history for a module; we don't have anywhere
  // to put it.
  if (M.isModule())
    return;

  // Deserialize the macro directives history in reverse source-order.
  MacroDirective *Latest = nullptr, *Earliest = nullptr;
  unsigned Idx = 0, N = Record.size();
  while (Idx < N) {
    MacroDirective *MD = nullptr;
    SourceLocation Loc = ReadSourceLocation(M, Record, Idx);
    MacroDirective::Kind K = (MacroDirective::Kind)Record[Idx++];
    switch (K) {
    case MacroDirective::MD_Define: {
      MacroInfo *MI = getMacro(getGlobalMacroID(M, Record[Idx++]));
      MD = PP.AllocateDefMacroDirective(MI, Loc);
      break;
    }
    case MacroDirective::MD_Undefine:
      MD = PP.AllocateUndefMacroDirective(Loc);
      break;
    case MacroDirective::MD_Visibility:
      bool isPublic = Record[Idx++];
      MD = PP.AllocateVisibilityMacroDirective(Loc, isPublic);
      break;
    }

    if (!Latest)
      Latest = MD;
    if (Earliest)
      Earliest->setPrevious(MD);
    Earliest = MD;
  }

  if (Latest)
    PP.setLoadedMacroDirective(II, Earliest, Latest);
}

bool ASTReader::shouldDisableValidationForFile(
    const serialization::ModuleFile &M) const {
  if (DisableValidationKind == DisableValidationForModuleKind::None)
    return false;

  // If a PCH is loaded and validation is disabled for PCH then disable
  // validation for the PCH and the modules it loads.
  ModuleKind K = CurrentDeserializingModuleKind.value_or(M.Kind);

  switch (K) {
  case MK_MainFile:
  case MK_Preamble:
  case MK_PCH:
    return bool(DisableValidationKind & DisableValidationForModuleKind::PCH);
  case MK_ImplicitModule:
  case MK_ExplicitModule:
  case MK_PrebuiltModule:
    return bool(DisableValidationKind & DisableValidationForModuleKind::Module);
  }

  return false;
}

InputFileInfo ASTReader::getInputFileInfo(ModuleFile &F, unsigned ID) {
  // If this ID is bogus, just return an empty input file.
  if (ID == 0 || ID > F.InputFileInfosLoaded.size())
    return InputFileInfo();

  // If we've already loaded this input file, return it.
  if (!F.InputFileInfosLoaded[ID - 1].Filename.empty())
    return F.InputFileInfosLoaded[ID - 1];

  // Go find this input file.
  BitstreamCursor &Cursor = F.InputFilesCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(F.InputFilesOffsetBase +
                                         F.InputFileOffsets[ID - 1])) {
    // FIXME this drops errors on the floor.
    consumeError(std::move(Err));
  }

  Expected<unsigned> MaybeCode = Cursor.ReadCode();
  if (!MaybeCode) {
    // FIXME this drops errors on the floor.
    consumeError(MaybeCode.takeError());
  }
  unsigned Code = MaybeCode.get();
  RecordData Record;
  StringRef Blob;

  if (Expected<unsigned> Maybe = Cursor.readRecord(Code, Record, &Blob))
    assert(static_cast<InputFileRecordTypes>(Maybe.get()) == INPUT_FILE &&
           "invalid record type for input file");
  else {
    // FIXME this drops errors on the floor.
    consumeError(Maybe.takeError());
  }

  assert(Record[0] == ID && "Bogus stored ID or offset");
  InputFileInfo R;
  R.StoredSize = static_cast<off_t>(Record[1]);
  R.StoredTime = static_cast<time_t>(Record[2]);
  R.Overridden = static_cast<bool>(Record[3]);
  R.Transient = static_cast<bool>(Record[4]);
  R.TopLevel = static_cast<bool>(Record[5]);
  R.ModuleMap = static_cast<bool>(Record[6]);
  std::tie(R.FilenameAsRequested, R.Filename) = [&]() {
    uint16_t AsRequestedLength = Record[7];

    std::string NameAsRequested = Blob.substr(0, AsRequestedLength).str();
    std::string Name = Blob.substr(AsRequestedLength).str();

    ResolveImportedPath(F, NameAsRequested);
    ResolveImportedPath(F, Name);

    if (Name.empty())
      Name = NameAsRequested;

    return std::make_pair(std::move(NameAsRequested), std::move(Name));
  }();

  Expected<llvm::BitstreamEntry> MaybeEntry = Cursor.advance();
  if (!MaybeEntry) // FIXME this drops errors on the floor.
    consumeError(MaybeEntry.takeError());
  llvm::BitstreamEntry Entry = MaybeEntry.get();
  assert(Entry.Kind == llvm::BitstreamEntry::Record &&
         "expected record type for input file hash");

  Record.clear();
  if (Expected<unsigned> Maybe = Cursor.readRecord(Entry.ID, Record))
    assert(static_cast<InputFileRecordTypes>(Maybe.get()) == INPUT_FILE_HASH &&
           "invalid record type for input file hash");
  else {
    // FIXME this drops errors on the floor.
    consumeError(Maybe.takeError());
  }
  R.ContentHash = (static_cast<uint64_t>(Record[1]) << 32) |
                  static_cast<uint64_t>(Record[0]);

  // Note that we've loaded this input file info.
  F.InputFileInfosLoaded[ID - 1] = R;
  return R;
}

static unsigned moduleKindForDiagnostic(ModuleKind Kind);
InputFile ASTReader::getInputFile(ModuleFile &F, unsigned ID, bool Complain) {
  // If this ID is bogus, just return an empty input file.
  if (ID == 0 || ID > F.InputFilesLoaded.size())
    return InputFile();

  // If we've already loaded this input file, return it.
  if (F.InputFilesLoaded[ID-1].getFile())
    return F.InputFilesLoaded[ID-1];

  if (F.InputFilesLoaded[ID-1].isNotFound())
    return InputFile();

  // Go find this input file.
  BitstreamCursor &Cursor = F.InputFilesCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(F.InputFilesOffsetBase +
                                         F.InputFileOffsets[ID - 1])) {
    // FIXME this drops errors on the floor.
    consumeError(std::move(Err));
  }

  InputFileInfo FI = getInputFileInfo(F, ID);
  off_t StoredSize = FI.StoredSize;
  time_t StoredTime = FI.StoredTime;
  bool Overridden = FI.Overridden;
  bool Transient = FI.Transient;
  StringRef Filename = FI.FilenameAsRequested;
  uint64_t StoredContentHash = FI.ContentHash;

  // For standard C++ modules, we don't need to check the inputs.
  bool SkipChecks = F.StandardCXXModule;

  const HeaderSearchOptions &HSOpts =
      PP.getHeaderSearchInfo().getHeaderSearchOpts();

  // The option ForceCheckCXX20ModulesInputFiles is only meaningful for C++20
  // modules.
  if (F.StandardCXXModule && HSOpts.ForceCheckCXX20ModulesInputFiles) {
    SkipChecks = false;
    Overridden = false;
  }

  auto File = FileMgr.getOptionalFileRef(Filename, /*OpenFile=*/false);

  // For an overridden file, create a virtual file with the stored
  // size/timestamp.
  if ((Overridden || Transient || SkipChecks) && !File)
    File = FileMgr.getVirtualFileRef(Filename, StoredSize, StoredTime);

  if (!File) {
    if (Complain) {
      std::string ErrorStr = "could not find file '";
      ErrorStr += Filename;
      ErrorStr += "' referenced by AST file '";
      ErrorStr += F.FileName;
      ErrorStr += "'";
      Error(ErrorStr);
    }
    // Record that we didn't find the file.
    F.InputFilesLoaded[ID-1] = InputFile::getNotFound();
    return InputFile();
  }

  // Check if there was a request to override the contents of the file
  // that was part of the precompiled header. Overriding such a file
  // can lead to problems when lexing using the source locations from the
  // PCH.
  SourceManager &SM = getSourceManager();
  // FIXME: Reject if the overrides are different.
  if ((!Overridden && !Transient) && !SkipChecks &&
      SM.isFileOverridden(*File)) {
    if (Complain)
      Error(diag::err_fe_pch_file_overridden, Filename);

    // After emitting the diagnostic, bypass the overriding file to recover
    // (this creates a separate FileEntry).
    File = SM.bypassFileContentsOverride(*File);
    if (!File) {
      F.InputFilesLoaded[ID - 1] = InputFile::getNotFound();
      return InputFile();
    }
  }

  struct Change {
    enum ModificationKind {
      Size,
      ModTime,
      Content,
      None,
    } Kind;
    std::optional<int64_t> Old = std::nullopt;
    std::optional<int64_t> New = std::nullopt;
  };
  auto HasInputContentChanged = [&](Change OriginalChange) {
    assert(ValidateASTInputFilesContent &&
           "We should only check the content of the inputs with "
           "ValidateASTInputFilesContent enabled.");

    if (StoredContentHash == 0)
      return OriginalChange;

    auto MemBuffOrError = FileMgr.getBufferForFile(*File);
    if (!MemBuffOrError) {
      if (!Complain)
        return OriginalChange;
      std::string ErrorStr = "could not get buffer for file '";
      ErrorStr += File->getName();
      ErrorStr += "'";
      Error(ErrorStr);
      return OriginalChange;
    }

    auto ContentHash = xxh3_64bits(MemBuffOrError.get()->getBuffer());
    if (StoredContentHash == static_cast<uint64_t>(ContentHash))
      return Change{Change::None};

    return Change{Change::Content};
  };
  auto HasInputFileChanged = [&]() {
    if (StoredSize != File->getSize())
      return Change{Change::Size, StoredSize, File->getSize()};
    if (!shouldDisableValidationForFile(F) && StoredTime &&
        StoredTime != File->getModificationTime()) {
      Change MTimeChange = {Change::ModTime, StoredTime,
                            File->getModificationTime()};

      // In case the modification time changes but not the content,
      // accept the cached file as legit.
      if (ValidateASTInputFilesContent)
        return HasInputContentChanged(MTimeChange);

      return MTimeChange;
    }
    return Change{Change::None};
  };

  bool IsOutOfDate = false;
  auto FileChange = SkipChecks ? Change{Change::None} : HasInputFileChanged();
  // When ForceCheckCXX20ModulesInputFiles and ValidateASTInputFilesContent
  // enabled, it is better to check the contents of the inputs. Since we can't
  // get correct modified time information for inputs from overriden inputs.
  if (HSOpts.ForceCheckCXX20ModulesInputFiles && ValidateASTInputFilesContent &&
      F.StandardCXXModule && FileChange.Kind == Change::None)
    FileChange = HasInputContentChanged(FileChange);

  // When we have StoredTime equal to zero and ValidateASTInputFilesContent,
  // it is better to check the content of the input files because we cannot rely
  // on the file modification time, which will be the same (zero) for these
  // files.
  if (!StoredTime && ValidateASTInputFilesContent &&
      FileChange.Kind == Change::None)
    FileChange = HasInputContentChanged(FileChange);

  // For an overridden file, there is nothing to validate.
  if (!Overridden && FileChange.Kind != Change::None) {
    if (Complain && !Diags.isDiagnosticInFlight()) {
      // Build a list of the PCH imports that got us here (in reverse).
      SmallVector<ModuleFile *, 4> ImportStack(1, &F);
      while (!ImportStack.back()->ImportedBy.empty())
        ImportStack.push_back(ImportStack.back()->ImportedBy[0]);

      // The top-level PCH is stale.
      StringRef TopLevelPCHName(ImportStack.back()->FileName);
      Diag(diag::err_fe_ast_file_modified)
          << Filename << moduleKindForDiagnostic(ImportStack.back()->Kind)
          << TopLevelPCHName << FileChange.Kind
          << (FileChange.Old && FileChange.New)
          << llvm::itostr(FileChange.Old.value_or(0))
          << llvm::itostr(FileChange.New.value_or(0));

      // Print the import stack.
      if (ImportStack.size() > 1) {
        Diag(diag::note_pch_required_by)
          << Filename << ImportStack[0]->FileName;
        for (unsigned I = 1; I < ImportStack.size(); ++I)
          Diag(diag::note_pch_required_by)
            << ImportStack[I-1]->FileName << ImportStack[I]->FileName;
      }

      Diag(diag::note_pch_rebuild_required) << TopLevelPCHName;
    }

    IsOutOfDate = true;
  }
  // FIXME: If the file is overridden and we've already opened it,
  // issue an error (or split it into a separate FileEntry).

  InputFile IF = InputFile(*File, Overridden || Transient, IsOutOfDate);

  // Note that we've loaded this input file.
  F.InputFilesLoaded[ID-1] = IF;
  return IF;
}

/// If we are loading a relocatable PCH or module file, and the filename
/// is not an absolute path, add the system or module root to the beginning of
/// the file name.
void ASTReader::ResolveImportedPath(ModuleFile &M, std::string &Filename) {
  // Resolve relative to the base directory, if we have one.
  if (!M.BaseDirectory.empty())
    return ResolveImportedPath(Filename, M.BaseDirectory);
}

void ASTReader::ResolveImportedPath(std::string &Filename, StringRef Prefix) {
  if (Filename.empty() || llvm::sys::path::is_absolute(Filename) ||
      Filename == "<built-in>" || Filename == "<command line>")
    return;

  SmallString<128> Buffer;
  llvm::sys::path::append(Buffer, Prefix, Filename);
  Filename.assign(Buffer.begin(), Buffer.end());
}

static bool isDiagnosedResult(ASTReader::ASTReadResult ARR, unsigned Caps) {
  switch (ARR) {
  case ASTReader::Failure: return true;
  case ASTReader::Missing: return !(Caps & ASTReader::ARR_Missing);
  case ASTReader::OutOfDate: return !(Caps & ASTReader::ARR_OutOfDate);
  case ASTReader::VersionMismatch: return !(Caps & ASTReader::ARR_VersionMismatch);
  case ASTReader::ConfigurationMismatch:
    return !(Caps & ASTReader::ARR_ConfigurationMismatch);
  case ASTReader::HadErrors: return true;
  case ASTReader::Success: return false;
  }

  llvm_unreachable("unknown ASTReadResult");
}

ASTReader::ASTReadResult ASTReader::ReadOptionsBlock(
    BitstreamCursor &Stream, unsigned ClientLoadCapabilities,
    bool AllowCompatibleConfigurationMismatch, ASTReaderListener &Listener,
    std::string &SuggestedPredefines) {
  if (llvm::Error Err = Stream.EnterSubBlock(OPTIONS_BLOCK_ID)) {
    // FIXME this drops errors on the floor.
    consumeError(std::move(Err));
    return Failure;
  }

  // Read all of the records in the options block.
  RecordData Record;
  ASTReadResult Result = Success;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry) {
      // FIXME this drops errors on the floor.
      consumeError(MaybeEntry.takeError());
      return Failure;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::SubBlock:
      return Failure;

    case llvm::BitstreamEntry::EndBlock:
      return Result;

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    Expected<unsigned> MaybeRecordType = Stream.readRecord(Entry.ID, Record);
    if (!MaybeRecordType) {
      // FIXME this drops errors on the floor.
      consumeError(MaybeRecordType.takeError());
      return Failure;
    }
    switch ((OptionsRecordTypes)MaybeRecordType.get()) {
    case LANGUAGE_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (ParseLanguageOptions(Record, Complain, Listener,
                               AllowCompatibleConfigurationMismatch))
        Result = ConfigurationMismatch;
      break;
    }

    case TARGET_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (ParseTargetOptions(Record, Complain, Listener,
                             AllowCompatibleConfigurationMismatch))
        Result = ConfigurationMismatch;
      break;
    }

    case FILE_SYSTEM_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (!AllowCompatibleConfigurationMismatch &&
          ParseFileSystemOptions(Record, Complain, Listener))
        Result = ConfigurationMismatch;
      break;
    }

    case HEADER_SEARCH_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (!AllowCompatibleConfigurationMismatch &&
          ParseHeaderSearchOptions(Record, Complain, Listener))
        Result = ConfigurationMismatch;
      break;
    }

    case PREPROCESSOR_OPTIONS:
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (!AllowCompatibleConfigurationMismatch &&
          ParsePreprocessorOptions(Record, Complain, Listener,
                                   SuggestedPredefines))
        Result = ConfigurationMismatch;
      break;
    }
  }
}

ASTReader::ASTReadResult
ASTReader::ReadControlBlock(ModuleFile &F,
                            SmallVectorImpl<ImportedModule> &Loaded,
                            const ModuleFile *ImportedBy,
                            unsigned ClientLoadCapabilities) {
  BitstreamCursor &Stream = F.Stream;

  if (llvm::Error Err = Stream.EnterSubBlock(CONTROL_BLOCK_ID)) {
    Error(std::move(Err));
    return Failure;
  }

  // Lambda to read the unhashed control block the first time it's called.
  //
  // For PCM files, the unhashed control block cannot be read until after the
  // MODULE_NAME record.  However, PCH files have no MODULE_NAME, and yet still
  // need to look ahead before reading the IMPORTS record.  For consistency,
  // this block is always read somehow (see BitstreamEntry::EndBlock).
  bool HasReadUnhashedControlBlock = false;
  auto readUnhashedControlBlockOnce = [&]() {
    if (!HasReadUnhashedControlBlock) {
      HasReadUnhashedControlBlock = true;
      if (ASTReadResult Result =
              readUnhashedControlBlock(F, ImportedBy, ClientLoadCapabilities))
        return Result;
    }
    return Success;
  };

  bool DisableValidation = shouldDisableValidationForFile(F);

  // Read all of the records and blocks in the control block.
  RecordData Record;
  unsigned NumInputs = 0;
  unsigned NumUserInputs = 0;
  StringRef BaseDirectoryAsWritten;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry) {
      Error(MaybeEntry.takeError());
      return Failure;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return Failure;
    case llvm::BitstreamEntry::EndBlock: {
      // Validate the module before returning.  This call catches an AST with
      // no module name and no imports.
      if (ASTReadResult Result = readUnhashedControlBlockOnce())
        return Result;

      // Validate input files.
      const HeaderSearchOptions &HSOpts =
          PP.getHeaderSearchInfo().getHeaderSearchOpts();

      // All user input files reside at the index range [0, NumUserInputs), and
      // system input files reside at [NumUserInputs, NumInputs). For explicitly
      // loaded module files, ignore missing inputs.
      if (!DisableValidation && F.Kind != MK_ExplicitModule &&
          F.Kind != MK_PrebuiltModule) {
        bool Complain = (ClientLoadCapabilities & ARR_OutOfDate) == 0;

        // If we are reading a module, we will create a verification timestamp,
        // so we verify all input files.  Otherwise, verify only user input
        // files.

        unsigned N = ValidateSystemInputs ? NumInputs : NumUserInputs;
        if (HSOpts.ModulesValidateOncePerBuildSession &&
            F.InputFilesValidationTimestamp > HSOpts.BuildSessionTimestamp &&
            F.Kind == MK_ImplicitModule)
          N = NumUserInputs;

        for (unsigned I = 0; I < N; ++I) {
          InputFile IF = getInputFile(F, I+1, Complain);
          if (!IF.getFile() || IF.isOutOfDate())
            return OutOfDate;
        }
      }

      if (Listener)
        Listener->visitModuleFile(F.FileName, F.Kind);

      if (Listener && Listener->needsInputFileVisitation()) {
        unsigned N = Listener->needsSystemInputFileVisitation() ? NumInputs
                                                                : NumUserInputs;
        for (unsigned I = 0; I < N; ++I) {
          bool IsSystem = I >= NumUserInputs;
          InputFileInfo FI = getInputFileInfo(F, I + 1);
          Listener->visitInputFile(
              FI.FilenameAsRequested, IsSystem, FI.Overridden,
              F.Kind == MK_ExplicitModule || F.Kind == MK_PrebuiltModule);
        }
      }

      return Success;
    }

    case llvm::BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      case INPUT_FILES_BLOCK_ID:
        F.InputFilesCursor = Stream;
        if (llvm::Error Err = Stream.SkipBlock()) {
          Error(std::move(Err));
          return Failure;
        }
        if (ReadBlockAbbrevs(F.InputFilesCursor, INPUT_FILES_BLOCK_ID)) {
          Error("malformed block record in AST file");
          return Failure;
        }
        F.InputFilesOffsetBase = F.InputFilesCursor.GetCurrentBitNo();
        continue;

      case OPTIONS_BLOCK_ID:
        // If we're reading the first module for this group, check its options
        // are compatible with ours. For modules it imports, no further checking
        // is required, because we checked them when we built it.
        if (Listener && !ImportedBy) {
          // Should we allow the configuration of the module file to differ from
          // the configuration of the current translation unit in a compatible
          // way?
          //
          // FIXME: Allow this for files explicitly specified with -include-pch.
          bool AllowCompatibleConfigurationMismatch =
              F.Kind == MK_ExplicitModule || F.Kind == MK_PrebuiltModule;

          ASTReadResult Result =
              ReadOptionsBlock(Stream, ClientLoadCapabilities,
                               AllowCompatibleConfigurationMismatch, *Listener,
                               SuggestedPredefines);
          if (Result == Failure) {
            Error("malformed block record in AST file");
            return Result;
          }

          if (DisableValidation ||
              (AllowConfigurationMismatch && Result == ConfigurationMismatch))
            Result = Success;

          // If we can't load the module, exit early since we likely
          // will rebuild the module anyway. The stream may be in the
          // middle of a block.
          if (Result != Success)
            return Result;
        } else if (llvm::Error Err = Stream.SkipBlock()) {
          Error(std::move(Err));
          return Failure;
        }
        continue;

      default:
        if (llvm::Error Err = Stream.SkipBlock()) {
          Error(std::move(Err));
          return Failure;
        }
        continue;
      }

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecordType =
        Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecordType) {
      Error(MaybeRecordType.takeError());
      return Failure;
    }
    switch ((ControlRecordTypes)MaybeRecordType.get()) {
    case METADATA: {
      if (Record[0] != VERSION_MAJOR && !DisableValidation) {
        if ((ClientLoadCapabilities & ARR_VersionMismatch) == 0)
          Diag(Record[0] < VERSION_MAJOR? diag::err_pch_version_too_old
                                        : diag::err_pch_version_too_new);
        return VersionMismatch;
      }

      bool hasErrors = Record[7];
      if (hasErrors && !DisableValidation) {
        // If requested by the caller and the module hasn't already been read
        // or compiled, mark modules on error as out-of-date.
        if ((ClientLoadCapabilities & ARR_TreatModuleWithErrorsAsOutOfDate) &&
            canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities))
          return OutOfDate;

        if (!AllowASTWithCompilerErrors) {
          Diag(diag::err_pch_with_compiler_errors);
          return HadErrors;
        }
      }
      if (hasErrors) {
        Diags.ErrorOccurred = true;
        Diags.UncompilableErrorOccurred = true;
        Diags.UnrecoverableErrorOccurred = true;
      }

      F.RelocatablePCH = Record[4];
      // Relative paths in a relocatable PCH are relative to our sysroot.
      if (F.RelocatablePCH)
        F.BaseDirectory = isysroot.empty() ? "/" : isysroot;

      F.StandardCXXModule = Record[5];

      F.HasTimestamps = Record[6];

      const std::string &CurBranch = getClangFullRepositoryVersion();
      StringRef ASTBranch = Blob;
      if (StringRef(CurBranch) != ASTBranch && !DisableValidation) {
        if ((ClientLoadCapabilities & ARR_VersionMismatch) == 0)
          Diag(diag::err_pch_different_branch) << ASTBranch << CurBranch;
        return VersionMismatch;
      }
      break;
    }

    case IMPORTS: {
      // Validate the AST before processing any imports (otherwise, untangling
      // them can be error-prone and expensive).  A module will have a name and
      // will already have been validated, but this catches the PCH case.
      if (ASTReadResult Result = readUnhashedControlBlockOnce())
        return Result;

      // Load each of the imported PCH files.
      unsigned Idx = 0, N = Record.size();
      while (Idx < N) {
        // Read information about the AST file.
        ModuleKind ImportedKind = (ModuleKind)Record[Idx++];
        // Whether we're importing a standard c++ module.
        bool IsImportingStdCXXModule = Record[Idx++];
        // The import location will be the local one for now; we will adjust
        // all import locations of module imports after the global source
        // location info are setup, in ReadAST.
        auto [ImportLoc, ImportModuleFileIndex] =
            ReadUntranslatedSourceLocation(Record[Idx++]);
        // The import location must belong to the current module file itself.
        assert(ImportModuleFileIndex == 0);
        off_t StoredSize = !IsImportingStdCXXModule ? (off_t)Record[Idx++] : 0;
        time_t StoredModTime =
            !IsImportingStdCXXModule ? (time_t)Record[Idx++] : 0;

        ASTFileSignature StoredSignature;
        if (!IsImportingStdCXXModule) {
          auto FirstSignatureByte = Record.begin() + Idx;
          StoredSignature = ASTFileSignature::create(
              FirstSignatureByte, FirstSignatureByte + ASTFileSignature::size);
          Idx += ASTFileSignature::size;
        }

        std::string ImportedName = ReadString(Record, Idx);
        std::string ImportedFile;

        // For prebuilt and explicit modules first consult the file map for
        // an override. Note that here we don't search prebuilt module
        // directories if we're not importing standard c++ module, only the
        // explicit name to file mappings. Also, we will still verify the
        // size/signature making sure it is essentially the same file but
        // perhaps in a different location.
        if (ImportedKind == MK_PrebuiltModule || ImportedKind == MK_ExplicitModule)
          ImportedFile = PP.getHeaderSearchInfo().getPrebuiltModuleFileName(
              ImportedName, /*FileMapOnly*/ !IsImportingStdCXXModule);

        // For C++20 Modules, we won't record the path to the imported modules
        // in the BMI
        if (!IsImportingStdCXXModule) {
          if (ImportedFile.empty()) {
            // Use BaseDirectoryAsWritten to ensure we use the same path in the
            // ModuleCache as when writing.
            ImportedFile = ReadPath(BaseDirectoryAsWritten, Record, Idx);
          } else
            SkipPath(Record, Idx);
        } else if (ImportedFile.empty()) {
          Diag(clang::diag::err_failed_to_find_module_file) << ImportedName;
          return Missing;
        }

        // If our client can't cope with us being out of date, we can't cope with
        // our dependency being missing.
        unsigned Capabilities = ClientLoadCapabilities;
        if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
          Capabilities &= ~ARR_Missing;

        // Load the AST file.
        auto Result = ReadASTCore(ImportedFile, ImportedKind, ImportLoc, &F,
                                  Loaded, StoredSize, StoredModTime,
                                  StoredSignature, Capabilities);

        // If we diagnosed a problem, produce a backtrace.
        bool recompilingFinalized =
            Result == OutOfDate && (Capabilities & ARR_OutOfDate) &&
            getModuleManager().getModuleCache().isPCMFinal(F.FileName);
        if (isDiagnosedResult(Result, Capabilities) || recompilingFinalized)
          Diag(diag::note_module_file_imported_by)
              << F.FileName << !F.ModuleName.empty() << F.ModuleName;
        if (recompilingFinalized)
          Diag(diag::note_module_file_conflict);

        switch (Result) {
        case Failure: return Failure;
          // If we have to ignore the dependency, we'll have to ignore this too.
        case Missing:
        case OutOfDate: return OutOfDate;
        case VersionMismatch: return VersionMismatch;
        case ConfigurationMismatch: return ConfigurationMismatch;
        case HadErrors: return HadErrors;
        case Success: break;
        }
      }
      break;
    }

    case ORIGINAL_FILE:
      F.OriginalSourceFileID = FileID::get(Record[0]);
      F.ActualOriginalSourceFileName = std::string(Blob);
      F.OriginalSourceFileName = F.ActualOriginalSourceFileName;
      ResolveImportedPath(F, F.OriginalSourceFileName);
      break;

    case ORIGINAL_FILE_ID:
      F.OriginalSourceFileID = FileID::get(Record[0]);
      break;

    case MODULE_NAME:
      F.ModuleName = std::string(Blob);
      Diag(diag::remark_module_import)
          << F.ModuleName << F.FileName << (ImportedBy ? true : false)
          << (ImportedBy ? StringRef(ImportedBy->ModuleName) : StringRef());
      if (Listener)
        Listener->ReadModuleName(F.ModuleName);

      // Validate the AST as soon as we have a name so we can exit early on
      // failure.
      if (ASTReadResult Result = readUnhashedControlBlockOnce())
        return Result;

      break;

    case MODULE_DIRECTORY: {
      // Save the BaseDirectory as written in the PCM for computing the module
      // filename for the ModuleCache.
      BaseDirectoryAsWritten = Blob;
      assert(!F.ModuleName.empty() &&
             "MODULE_DIRECTORY found before MODULE_NAME");
      F.BaseDirectory = std::string(Blob);
      if (!PP.getPreprocessorOpts().ModulesCheckRelocated)
        break;
      // If we've already loaded a module map file covering this module, we may
      // have a better path for it (relative to the current build).
      Module *M = PP.getHeaderSearchInfo().lookupModule(
          F.ModuleName, SourceLocation(), /*AllowSearch*/ true,
          /*AllowExtraModuleMapSearch*/ true);
      if (M && M->Directory) {
        // If we're implicitly loading a module, the base directory can't
        // change between the build and use.
        // Don't emit module relocation error if we have -fno-validate-pch
        if (!bool(PP.getPreprocessorOpts().DisablePCHOrModuleValidation &
                  DisableValidationForModuleKind::Module) &&
            F.Kind != MK_ExplicitModule && F.Kind != MK_PrebuiltModule) {
          auto BuildDir = PP.getFileManager().getOptionalDirectoryRef(Blob);
          if (!BuildDir || *BuildDir != M->Directory) {
            if (!canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities))
              Diag(diag::err_imported_module_relocated)
                  << F.ModuleName << Blob << M->Directory->getName();
            return OutOfDate;
          }
        }
        F.BaseDirectory = std::string(M->Directory->getName());
      }
      break;
    }

    case MODULE_MAP_FILE:
      if (ASTReadResult Result =
              ReadModuleMapFileBlock(Record, F, ImportedBy, ClientLoadCapabilities))
        return Result;
      break;

    case INPUT_FILE_OFFSETS:
      NumInputs = Record[0];
      NumUserInputs = Record[1];
      F.InputFileOffsets =
          (const llvm::support::unaligned_uint64_t *)Blob.data();
      F.InputFilesLoaded.resize(NumInputs);
      F.InputFileInfosLoaded.resize(NumInputs);
      F.NumUserInputFiles = NumUserInputs;
      break;
    }
  }
}

llvm::Error ASTReader::ReadASTBlock(ModuleFile &F,
                                    unsigned ClientLoadCapabilities) {
  BitstreamCursor &Stream = F.Stream;

  if (llvm::Error Err = Stream.EnterSubBlock(AST_BLOCK_ID))
    return Err;
  F.ASTBlockStartOffset = Stream.GetCurrentBitNo();

  // Read all of the records and blocks for the AST file.
  RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry)
      return MaybeEntry.takeError();
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
      return llvm::createStringError(
          std::errc::illegal_byte_sequence,
          "error at end of module block in AST file");
    case llvm::BitstreamEntry::EndBlock:
      // Outside of C++, we do not store a lookup map for the translation unit.
      // Instead, mark it as needing a lookup map to be built if this module
      // contains any declarations lexically within it (which it always does!).
      // This usually has no cost, since we very rarely need the lookup map for
      // the translation unit outside C++.
      if (ASTContext *Ctx = ContextObj) {
        DeclContext *DC = Ctx->getTranslationUnitDecl();
        if (DC->hasExternalLexicalStorage() && !Ctx->getLangOpts().CPlusPlus)
          DC->setMustBuildLookupTable();
      }

      return llvm::Error::success();
    case llvm::BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      case DECLTYPES_BLOCK_ID:
        // We lazily load the decls block, but we want to set up the
        // DeclsCursor cursor to point into it.  Clone our current bitcode
        // cursor to it, enter the block and read the abbrevs in that block.
        // With the main cursor, we just skip over it.
        F.DeclsCursor = Stream;
        if (llvm::Error Err = Stream.SkipBlock())
          return Err;
        if (llvm::Error Err = ReadBlockAbbrevs(
                F.DeclsCursor, DECLTYPES_BLOCK_ID, &F.DeclsBlockStartOffset))
          return Err;
        break;

      case PREPROCESSOR_BLOCK_ID:
        F.MacroCursor = Stream;
        if (!PP.getExternalSource())
          PP.setExternalSource(this);

        if (llvm::Error Err = Stream.SkipBlock())
          return Err;
        if (llvm::Error Err =
                ReadBlockAbbrevs(F.MacroCursor, PREPROCESSOR_BLOCK_ID))
          return Err;
        F.MacroStartOffset = F.MacroCursor.GetCurrentBitNo();
        break;

      case PREPROCESSOR_DETAIL_BLOCK_ID:
        F.PreprocessorDetailCursor = Stream;

        if (llvm::Error Err = Stream.SkipBlock()) {
          return Err;
        }
        if (llvm::Error Err = ReadBlockAbbrevs(F.PreprocessorDetailCursor,
                                               PREPROCESSOR_DETAIL_BLOCK_ID))
          return Err;
        F.PreprocessorDetailStartOffset
        = F.PreprocessorDetailCursor.GetCurrentBitNo();

        if (!PP.getPreprocessingRecord())
          PP.createPreprocessingRecord();
        if (!PP.getPreprocessingRecord()->getExternalSource())
          PP.getPreprocessingRecord()->SetExternalSource(*this);
        break;

      case SOURCE_MANAGER_BLOCK_ID:
        if (llvm::Error Err = ReadSourceManagerBlock(F))
          return Err;
        break;

      case SUBMODULE_BLOCK_ID:
        if (llvm::Error Err = ReadSubmoduleBlock(F, ClientLoadCapabilities))
          return Err;
        break;

      case COMMENTS_BLOCK_ID: {
        BitstreamCursor C = Stream;

        if (llvm::Error Err = Stream.SkipBlock())
          return Err;
        if (llvm::Error Err = ReadBlockAbbrevs(C, COMMENTS_BLOCK_ID))
          return Err;
        CommentsCursors.push_back(std::make_pair(C, &F));
        break;
      }

      default:
        if (llvm::Error Err = Stream.SkipBlock())
          return Err;
        break;
      }
      continue;

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecordType =
        Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecordType)
      return MaybeRecordType.takeError();
    ASTRecordTypes RecordType = (ASTRecordTypes)MaybeRecordType.get();

    // If we're not loading an AST context, we don't care about most records.
    if (!ContextObj) {
      switch (RecordType) {
      case IDENTIFIER_TABLE:
      case IDENTIFIER_OFFSET:
      case INTERESTING_IDENTIFIERS:
      case STATISTICS:
      case PP_ASSUME_NONNULL_LOC:
      case PP_CONDITIONAL_STACK:
      case PP_COUNTER_VALUE:
      case SOURCE_LOCATION_OFFSETS:
      case MODULE_OFFSET_MAP:
      case SOURCE_MANAGER_LINE_TABLE:
      case PPD_ENTITIES_OFFSETS:
      case HEADER_SEARCH_TABLE:
      case IMPORTED_MODULES:
      case MACRO_OFFSET:
        break;
      default:
        continue;
      }
    }

    switch (RecordType) {
    default:  // Default behavior: ignore.
      break;

    case TYPE_OFFSET: {
      if (F.LocalNumTypes != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "duplicate TYPE_OFFSET record in AST file");
      F.TypeOffsets = reinterpret_cast<const UnalignedUInt64 *>(Blob.data());
      F.LocalNumTypes = Record[0];
      F.BaseTypeIndex = getTotalNumTypes();

      if (F.LocalNumTypes > 0)
        TypesLoaded.resize(TypesLoaded.size() + F.LocalNumTypes);

      break;
    }

    case DECL_OFFSET: {
      if (F.LocalNumDecls != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "duplicate DECL_OFFSET record in AST file");
      F.DeclOffsets = (const DeclOffset *)Blob.data();
      F.LocalNumDecls = Record[0];
      F.BaseDeclIndex = getTotalNumDecls();

      if (F.LocalNumDecls > 0)
        DeclsLoaded.resize(DeclsLoaded.size() + F.LocalNumDecls);

      break;
    }

    case TU_UPDATE_LEXICAL: {
      DeclContext *TU = ContextObj->getTranslationUnitDecl();
      LexicalContents Contents(
          reinterpret_cast<const unaligned_decl_id_t *>(Blob.data()),
          static_cast<unsigned int>(Blob.size() / sizeof(DeclID)));
      TULexicalDecls.push_back(std::make_pair(&F, Contents));
      TU->setHasExternalLexicalStorage(true);
      break;
    }

    case UPDATE_VISIBLE: {
      unsigned Idx = 0;
      GlobalDeclID ID = ReadDeclID(F, Record, Idx);
      auto *Data = (const unsigned char*)Blob.data();
      PendingVisibleUpdates[ID].push_back(PendingVisibleUpdate{&F, Data});
      // If we've already loaded the decl, perform the updates when we finish
      // loading this block.
      if (Decl *D = GetExistingDecl(ID))
        PendingUpdateRecords.push_back(
            PendingUpdateRecord(ID, D, /*JustLoaded=*/false));
      break;
    }

    case IDENTIFIER_TABLE:
      F.IdentifierTableData =
          reinterpret_cast<const unsigned char *>(Blob.data());
      if (Record[0]) {
        F.IdentifierLookupTable = ASTIdentifierLookupTable::Create(
            F.IdentifierTableData + Record[0],
            F.IdentifierTableData + sizeof(uint32_t),
            F.IdentifierTableData,
            ASTIdentifierLookupTrait(*this, F));

        PP.getIdentifierTable().setExternalIdentifierLookup(this);
      }
      break;

    case IDENTIFIER_OFFSET: {
      if (F.LocalNumIdentifiers != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "duplicate IDENTIFIER_OFFSET record in AST file");
      F.IdentifierOffsets = (const uint32_t *)Blob.data();
      F.LocalNumIdentifiers = Record[0];
      F.BaseIdentifierID = getTotalNumIdentifiers();

      if (F.LocalNumIdentifiers > 0)
        IdentifiersLoaded.resize(IdentifiersLoaded.size()
                                 + F.LocalNumIdentifiers);
      break;
    }

    case INTERESTING_IDENTIFIERS:
      F.PreloadIdentifierOffsets.assign(Record.begin(), Record.end());
      break;

    case EAGERLY_DESERIALIZED_DECLS:
      // FIXME: Skip reading this record if our ASTConsumer doesn't care
      // about "interesting" decls (for instance, if we're building a module).
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        EagerlyDeserializedDecls.push_back(ReadDeclID(F, Record, I));
      break;

    case MODULAR_CODEGEN_DECLS:
      // FIXME: Skip reading this record if our ASTConsumer doesn't care about
      // them (ie: if we're not codegenerating this module).
      if (F.Kind == MK_MainFile ||
          getContext().getLangOpts().BuildingPCHWithObjectFile)
        for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
          EagerlyDeserializedDecls.push_back(ReadDeclID(F, Record, I));
      break;

    case SPECIAL_TYPES:
      if (SpecialTypes.empty()) {
        for (unsigned I = 0, N = Record.size(); I != N; ++I)
          SpecialTypes.push_back(getGlobalTypeID(F, Record[I]));
        break;
      }

      if (SpecialTypes.size() != Record.size())
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid special-types record");

      for (unsigned I = 0, N = Record.size(); I != N; ++I) {
        serialization::TypeID ID = getGlobalTypeID(F, Record[I]);
        if (!SpecialTypes[I])
          SpecialTypes[I] = ID;
        // FIXME: If ID && SpecialTypes[I] != ID, do we need a separate
        // merge step?
      }
      break;

    case STATISTICS:
      TotalNumStatements += Record[0];
      TotalNumMacros += Record[1];
      TotalLexicalDeclContexts += Record[2];
      TotalVisibleDeclContexts += Record[3];
      break;

    case UNUSED_FILESCOPED_DECLS:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        UnusedFileScopedDecls.push_back(ReadDeclID(F, Record, I));
      break;

    case DELEGATING_CTORS:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        DelegatingCtorDecls.push_back(ReadDeclID(F, Record, I));
      break;

    case WEAK_UNDECLARED_IDENTIFIERS:
      if (Record.size() % 3 != 0)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid weak identifiers record");

      // FIXME: Ignore weak undeclared identifiers from non-original PCH
      // files. This isn't the way to do it :)
      WeakUndeclaredIdentifiers.clear();

      // Translate the weak, undeclared identifiers into global IDs.
      for (unsigned I = 0, N = Record.size(); I < N; /* in loop */) {
        WeakUndeclaredIdentifiers.push_back(
          getGlobalIdentifierID(F, Record[I++]));
        WeakUndeclaredIdentifiers.push_back(
          getGlobalIdentifierID(F, Record[I++]));
        WeakUndeclaredIdentifiers.push_back(
            ReadSourceLocation(F, Record, I).getRawEncoding());
      }
      break;

    case SELECTOR_OFFSETS: {
      F.SelectorOffsets = (const uint32_t *)Blob.data();
      F.LocalNumSelectors = Record[0];
      unsigned LocalBaseSelectorID = Record[1];
      F.BaseSelectorID = getTotalNumSelectors();

      if (F.LocalNumSelectors > 0) {
        // Introduce the global -> local mapping for selectors within this
        // module.
        GlobalSelectorMap.insert(std::make_pair(getTotalNumSelectors()+1, &F));

        // Introduce the local -> global mapping for selectors within this
        // module.
        F.SelectorRemap.insertOrReplace(
          std::make_pair(LocalBaseSelectorID,
                         F.BaseSelectorID - LocalBaseSelectorID));

        SelectorsLoaded.resize(SelectorsLoaded.size() + F.LocalNumSelectors);
      }
      break;
    }

    case METHOD_POOL:
      F.SelectorLookupTableData = (const unsigned char *)Blob.data();
      if (Record[0])
        F.SelectorLookupTable
          = ASTSelectorLookupTable::Create(
                        F.SelectorLookupTableData + Record[0],
                        F.SelectorLookupTableData,
                        ASTSelectorLookupTrait(*this, F));
      TotalNumMethodPoolEntries += Record[1];
      break;

    case REFERENCED_SELECTOR_POOL:
      if (!Record.empty()) {
        for (unsigned Idx = 0, N = Record.size() - 1; Idx < N; /* in loop */) {
          ReferencedSelectorsData.push_back(getGlobalSelectorID(F,
                                                                Record[Idx++]));
          ReferencedSelectorsData.push_back(ReadSourceLocation(F, Record, Idx).
                                              getRawEncoding());
        }
      }
      break;

    case PP_ASSUME_NONNULL_LOC: {
      unsigned Idx = 0;
      if (!Record.empty())
        PP.setPreambleRecordedPragmaAssumeNonNullLoc(
            ReadSourceLocation(F, Record, Idx));
      break;
    }

    case PP_UNSAFE_BUFFER_USAGE: {
      if (!Record.empty()) {
        SmallVector<SourceLocation, 64> SrcLocs;
        unsigned Idx = 0;
        while (Idx < Record.size())
          SrcLocs.push_back(ReadSourceLocation(F, Record, Idx));
        PP.setDeserializedSafeBufferOptOutMap(SrcLocs);
      }
      break;
    }

    case PP_CONDITIONAL_STACK:
      if (!Record.empty()) {
        unsigned Idx = 0, End = Record.size() - 1;
        bool ReachedEOFWhileSkipping = Record[Idx++];
        std::optional<Preprocessor::PreambleSkipInfo> SkipInfo;
        if (ReachedEOFWhileSkipping) {
          SourceLocation HashToken = ReadSourceLocation(F, Record, Idx);
          SourceLocation IfTokenLoc = ReadSourceLocation(F, Record, Idx);
          bool FoundNonSkipPortion = Record[Idx++];
          bool FoundElse = Record[Idx++];
          SourceLocation ElseLoc = ReadSourceLocation(F, Record, Idx);
          SkipInfo.emplace(HashToken, IfTokenLoc, FoundNonSkipPortion,
                           FoundElse, ElseLoc);
        }
        SmallVector<PPConditionalInfo, 4> ConditionalStack;
        while (Idx < End) {
          auto Loc = ReadSourceLocation(F, Record, Idx);
          bool WasSkipping = Record[Idx++];
          bool FoundNonSkip = Record[Idx++];
          bool FoundElse = Record[Idx++];
          ConditionalStack.push_back(
              {Loc, WasSkipping, FoundNonSkip, FoundElse});
        }
        PP.setReplayablePreambleConditionalStack(ConditionalStack, SkipInfo);
      }
      break;

    case PP_COUNTER_VALUE:
      if (!Record.empty() && Listener)
        Listener->ReadCounter(F, Record[0]);
      break;

    case FILE_SORTED_DECLS:
      F.FileSortedDecls = (const unaligned_decl_id_t *)Blob.data();
      F.NumFileSortedDecls = Record[0];
      break;

    case SOURCE_LOCATION_OFFSETS: {
      F.SLocEntryOffsets = (const uint32_t *)Blob.data();
      F.LocalNumSLocEntries = Record[0];
      SourceLocation::UIntTy SLocSpaceSize = Record[1];
      F.SLocEntryOffsetsBase = Record[2] + F.SourceManagerBlockStartOffset;
      std::tie(F.SLocEntryBaseID, F.SLocEntryBaseOffset) =
          SourceMgr.AllocateLoadedSLocEntries(F.LocalNumSLocEntries,
                                              SLocSpaceSize);
      if (!F.SLocEntryBaseID) {
        if (!Diags.isDiagnosticInFlight()) {
          Diags.Report(SourceLocation(), diag::remark_sloc_usage);
          SourceMgr.noteSLocAddressSpaceUsage(Diags);
        }
        return llvm::createStringError(std::errc::invalid_argument,
                                       "ran out of source locations");
      }
      // Make our entry in the range map. BaseID is negative and growing, so
      // we invert it. Because we invert it, though, we need the other end of
      // the range.
      unsigned RangeStart =
          unsigned(-F.SLocEntryBaseID) - F.LocalNumSLocEntries + 1;
      GlobalSLocEntryMap.insert(std::make_pair(RangeStart, &F));
      F.FirstLoc = SourceLocation::getFromRawEncoding(F.SLocEntryBaseOffset);

      // SLocEntryBaseOffset is lower than MaxLoadedOffset and decreasing.
      assert((F.SLocEntryBaseOffset & SourceLocation::MacroIDBit) == 0);
      GlobalSLocOffsetMap.insert(
          std::make_pair(SourceManager::MaxLoadedOffset - F.SLocEntryBaseOffset
                           - SLocSpaceSize,&F));

      TotalNumSLocEntries += F.LocalNumSLocEntries;
      break;
    }

    case MODULE_OFFSET_MAP:
      F.ModuleOffsetMap = Blob;
      break;

    case SOURCE_MANAGER_LINE_TABLE:
      ParseLineTable(F, Record);
      break;

    case EXT_VECTOR_DECLS:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        ExtVectorDecls.push_back(ReadDeclID(F, Record, I));
      break;

    case VTABLE_USES:
      if (Record.size() % 3 != 0)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "Invalid VTABLE_USES record");

      // Later tables overwrite earlier ones.
      // FIXME: Modules will have some trouble with this. This is clearly not
      // the right way to do this.
      VTableUses.clear();

      for (unsigned Idx = 0, N = Record.size(); Idx != N; /* In loop */) {
        VTableUses.push_back(
            {ReadDeclID(F, Record, Idx),
             ReadSourceLocation(F, Record, Idx).getRawEncoding(),
             (bool)Record[Idx++]});
      }
      break;

    case PENDING_IMPLICIT_INSTANTIATIONS:

      if (Record.size() % 2 != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "Invalid PENDING_IMPLICIT_INSTANTIATIONS block");

      for (unsigned I = 0, N = Record.size(); I != N; /* in loop */) {
        PendingInstantiations.push_back(
            {ReadDeclID(F, Record, I),
             ReadSourceLocation(F, Record, I).getRawEncoding()});
      }
      break;

    case SEMA_DECL_REFS:
      if (Record.size() != 3)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "Invalid SEMA_DECL_REFS block");
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        SemaDeclRefs.push_back(ReadDeclID(F, Record, I));
      break;

    case PPD_ENTITIES_OFFSETS: {
      F.PreprocessedEntityOffsets = (const PPEntityOffset *)Blob.data();
      assert(Blob.size() % sizeof(PPEntityOffset) == 0);
      F.NumPreprocessedEntities = Blob.size() / sizeof(PPEntityOffset);

      unsigned LocalBasePreprocessedEntityID = Record[0];

      unsigned StartingID;
      if (!PP.getPreprocessingRecord())
        PP.createPreprocessingRecord();
      if (!PP.getPreprocessingRecord()->getExternalSource())
        PP.getPreprocessingRecord()->SetExternalSource(*this);
      StartingID
        = PP.getPreprocessingRecord()
            ->allocateLoadedEntities(F.NumPreprocessedEntities);
      F.BasePreprocessedEntityID = StartingID;

      if (F.NumPreprocessedEntities > 0) {
        // Introduce the global -> local mapping for preprocessed entities in
        // this module.
        GlobalPreprocessedEntityMap.insert(std::make_pair(StartingID, &F));

        // Introduce the local -> global mapping for preprocessed entities in
        // this module.
        F.PreprocessedEntityRemap.insertOrReplace(
          std::make_pair(LocalBasePreprocessedEntityID,
            F.BasePreprocessedEntityID - LocalBasePreprocessedEntityID));
      }

      break;
    }

    case PPD_SKIPPED_RANGES: {
      F.PreprocessedSkippedRangeOffsets = (const PPSkippedRange*)Blob.data();
      assert(Blob.size() % sizeof(PPSkippedRange) == 0);
      F.NumPreprocessedSkippedRanges = Blob.size() / sizeof(PPSkippedRange);

      if (!PP.getPreprocessingRecord())
        PP.createPreprocessingRecord();
      if (!PP.getPreprocessingRecord()->getExternalSource())
        PP.getPreprocessingRecord()->SetExternalSource(*this);
      F.BasePreprocessedSkippedRangeID = PP.getPreprocessingRecord()
          ->allocateSkippedRanges(F.NumPreprocessedSkippedRanges);

      if (F.NumPreprocessedSkippedRanges > 0)
        GlobalSkippedRangeMap.insert(
            std::make_pair(F.BasePreprocessedSkippedRangeID, &F));
      break;
    }

    case DECL_UPDATE_OFFSETS:
      if (Record.size() % 2 != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "invalid DECL_UPDATE_OFFSETS block in AST file");
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/) {
        GlobalDeclID ID = ReadDeclID(F, Record, I);
        DeclUpdateOffsets[ID].push_back(std::make_pair(&F, Record[I++]));

        // If we've already loaded the decl, perform the updates when we finish
        // loading this block.
        if (Decl *D = GetExistingDecl(ID))
          PendingUpdateRecords.push_back(
              PendingUpdateRecord(ID, D, /*JustLoaded=*/false));
      }
      break;

    case DELAYED_NAMESPACE_LEXICAL_VISIBLE_RECORD: {
      if (Record.size() % 3 != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "invalid DELAYED_NAMESPACE_LEXICAL_VISIBLE_RECORD block in AST "
            "file");
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/) {
        GlobalDeclID ID = ReadDeclID(F, Record, I);

        uint64_t BaseOffset = F.DeclsBlockStartOffset;
        assert(BaseOffset && "Invalid DeclsBlockStartOffset for module file!");
        uint64_t LocalLexicalOffset = Record[I++];
        uint64_t LexicalOffset =
            LocalLexicalOffset ? BaseOffset + LocalLexicalOffset : 0;
        uint64_t LocalVisibleOffset = Record[I++];
        uint64_t VisibleOffset =
            LocalVisibleOffset ? BaseOffset + LocalVisibleOffset : 0;

        DelayedNamespaceOffsetMap[ID] = {LexicalOffset, VisibleOffset};

        assert(!GetExistingDecl(ID) &&
               "We shouldn't load the namespace in the front of delayed "
               "namespace lexical and visible block");
      }
      break;
    }

    case OBJC_CATEGORIES_MAP:
      if (F.LocalNumObjCCategoriesInMap != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "duplicate OBJC_CATEGORIES_MAP record in AST file");

      F.LocalNumObjCCategoriesInMap = Record[0];
      F.ObjCCategoriesMap = (const ObjCCategoriesInfo *)Blob.data();
      break;

    case OBJC_CATEGORIES:
      F.ObjCCategories.swap(Record);
      break;

    case CUDA_SPECIAL_DECL_REFS:
      // Later tables overwrite earlier ones.
      // FIXME: Modules will have trouble with this.
      CUDASpecialDeclRefs.clear();
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        CUDASpecialDeclRefs.push_back(ReadDeclID(F, Record, I));
      break;

    case HEADER_SEARCH_TABLE:
      F.HeaderFileInfoTableData = Blob.data();
      F.LocalNumHeaderFileInfos = Record[1];
      if (Record[0]) {
        F.HeaderFileInfoTable
          = HeaderFileInfoLookupTable::Create(
                   (const unsigned char *)F.HeaderFileInfoTableData + Record[0],
                   (const unsigned char *)F.HeaderFileInfoTableData,
                   HeaderFileInfoTrait(*this, F,
                                       &PP.getHeaderSearchInfo(),
                                       Blob.data() + Record[2]));

        PP.getHeaderSearchInfo().SetExternalSource(this);
        if (!PP.getHeaderSearchInfo().getExternalLookup())
          PP.getHeaderSearchInfo().SetExternalLookup(this);
      }
      break;

    case FP_PRAGMA_OPTIONS:
      // Later tables overwrite earlier ones.
      FPPragmaOptions.swap(Record);
      break;

    case OPENCL_EXTENSIONS:
      for (unsigned I = 0, E = Record.size(); I != E; ) {
        auto Name = ReadString(Record, I);
        auto &OptInfo = OpenCLExtensions.OptMap[Name];
        OptInfo.Supported = Record[I++] != 0;
        OptInfo.Enabled = Record[I++] != 0;
        OptInfo.WithPragma = Record[I++] != 0;
        OptInfo.Avail = Record[I++];
        OptInfo.Core = Record[I++];
        OptInfo.Opt = Record[I++];
      }
      break;

    case TENTATIVE_DEFINITIONS:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        TentativeDefinitions.push_back(ReadDeclID(F, Record, I));
      break;

    case KNOWN_NAMESPACES:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        KnownNamespaces.push_back(ReadDeclID(F, Record, I));
      break;

    case UNDEFINED_BUT_USED:
      if (Record.size() % 2 != 0)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid undefined-but-used record");
      for (unsigned I = 0, N = Record.size(); I != N; /* in loop */) {
        UndefinedButUsed.push_back(
            {ReadDeclID(F, Record, I),
             ReadSourceLocation(F, Record, I).getRawEncoding()});
      }
      break;

    case DELETE_EXPRS_TO_ANALYZE:
      for (unsigned I = 0, N = Record.size(); I != N;) {
        DelayedDeleteExprs.push_back(ReadDeclID(F, Record, I).getRawValue());
        const uint64_t Count = Record[I++];
        DelayedDeleteExprs.push_back(Count);
        for (uint64_t C = 0; C < Count; ++C) {
          DelayedDeleteExprs.push_back(ReadSourceLocation(F, Record, I).getRawEncoding());
          bool IsArrayForm = Record[I++] == 1;
          DelayedDeleteExprs.push_back(IsArrayForm);
        }
      }
      break;

    case VTABLES_TO_EMIT:
      if (F.Kind == MK_MainFile ||
          getContext().getLangOpts().BuildingPCHWithObjectFile)
        for (unsigned I = 0, N = Record.size(); I != N;)
          VTablesToEmit.push_back(ReadDeclID(F, Record, I));
      break;

    case IMPORTED_MODULES:
      if (!F.isModule()) {
        // If we aren't loading a module (which has its own exports), make
        // all of the imported modules visible.
        // FIXME: Deal with macros-only imports.
        for (unsigned I = 0, N = Record.size(); I != N; /**/) {
          unsigned GlobalID = getGlobalSubmoduleID(F, Record[I++]);
          SourceLocation Loc = ReadSourceLocation(F, Record, I);
          if (GlobalID) {
            PendingImportedModules.push_back(ImportedSubmodule(GlobalID, Loc));
            if (DeserializationListener)
              DeserializationListener->ModuleImportRead(GlobalID, Loc);
          }
        }
      }
      break;

    case MACRO_OFFSET: {
      if (F.LocalNumMacros != 0)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "duplicate MACRO_OFFSET record in AST file");
      F.MacroOffsets = (const uint32_t *)Blob.data();
      F.LocalNumMacros = Record[0];
      unsigned LocalBaseMacroID = Record[1];
      F.MacroOffsetsBase = Record[2] + F.ASTBlockStartOffset;
      F.BaseMacroID = getTotalNumMacros();

      if (F.LocalNumMacros > 0) {
        // Introduce the global -> local mapping for macros within this module.
        GlobalMacroMap.insert(std::make_pair(getTotalNumMacros() + 1, &F));

        // Introduce the local -> global mapping for macros within this module.
        F.MacroRemap.insertOrReplace(
          std::make_pair(LocalBaseMacroID,
                         F.BaseMacroID - LocalBaseMacroID));

        MacrosLoaded.resize(MacrosLoaded.size() + F.LocalNumMacros);
      }
      break;
    }

    case LATE_PARSED_TEMPLATE:
      LateParsedTemplates.emplace_back(
          std::piecewise_construct, std::forward_as_tuple(&F),
          std::forward_as_tuple(Record.begin(), Record.end()));
      break;

    case OPTIMIZE_PRAGMA_OPTIONS:
      if (Record.size() != 1)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid pragma optimize record");
      OptimizeOffPragmaLocation = ReadSourceLocation(F, Record[0]);
      break;

    case MSSTRUCT_PRAGMA_OPTIONS:
      if (Record.size() != 1)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid pragma ms_struct record");
      PragmaMSStructState = Record[0];
      break;

    case POINTERS_TO_MEMBERS_PRAGMA_OPTIONS:
      if (Record.size() != 2)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "invalid pragma pointers to members record");
      PragmaMSPointersToMembersState = Record[0];
      PointersToMembersPragmaLocation = ReadSourceLocation(F, Record[1]);
      break;

    case UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        UnusedLocalTypedefNameCandidates.push_back(ReadDeclID(F, Record, I));
      break;

    case CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH:
      if (Record.size() != 1)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid cuda pragma options record");
      ForceHostDeviceDepth = Record[0];
      break;

    case ALIGN_PACK_PRAGMA_OPTIONS: {
      if (Record.size() < 3)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid pragma pack record");
      PragmaAlignPackCurrentValue = ReadAlignPackInfo(Record[0]);
      PragmaAlignPackCurrentLocation = ReadSourceLocation(F, Record[1]);
      unsigned NumStackEntries = Record[2];
      unsigned Idx = 3;
      // Reset the stack when importing a new module.
      PragmaAlignPackStack.clear();
      for (unsigned I = 0; I < NumStackEntries; ++I) {
        PragmaAlignPackStackEntry Entry;
        Entry.Value = ReadAlignPackInfo(Record[Idx++]);
        Entry.Location = ReadSourceLocation(F, Record[Idx++]);
        Entry.PushLocation = ReadSourceLocation(F, Record[Idx++]);
        PragmaAlignPackStrings.push_back(ReadString(Record, Idx));
        Entry.SlotLabel = PragmaAlignPackStrings.back();
        PragmaAlignPackStack.push_back(Entry);
      }
      break;
    }

    case FLOAT_CONTROL_PRAGMA_OPTIONS: {
      if (Record.size() < 3)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "invalid pragma float control record");
      FpPragmaCurrentValue = FPOptionsOverride::getFromOpaqueInt(Record[0]);
      FpPragmaCurrentLocation = ReadSourceLocation(F, Record[1]);
      unsigned NumStackEntries = Record[2];
      unsigned Idx = 3;
      // Reset the stack when importing a new module.
      FpPragmaStack.clear();
      for (unsigned I = 0; I < NumStackEntries; ++I) {
        FpPragmaStackEntry Entry;
        Entry.Value = FPOptionsOverride::getFromOpaqueInt(Record[Idx++]);
        Entry.Location = ReadSourceLocation(F, Record[Idx++]);
        Entry.PushLocation = ReadSourceLocation(F, Record[Idx++]);
        FpPragmaStrings.push_back(ReadString(Record, Idx));
        Entry.SlotLabel = FpPragmaStrings.back();
        FpPragmaStack.push_back(Entry);
      }
      break;
    }

    case DECLS_TO_CHECK_FOR_DEFERRED_DIAGS:
      for (unsigned I = 0, N = Record.size(); I != N; /*in loop*/)
        DeclsToCheckForDeferredDiags.insert(ReadDeclID(F, Record, I));
      break;
    }
  }
}

void ASTReader::ReadModuleOffsetMap(ModuleFile &F) const {
  assert(!F.ModuleOffsetMap.empty() && "no module offset map to read");

  // Additional remapping information.
  const unsigned char *Data = (const unsigned char*)F.ModuleOffsetMap.data();
  const unsigned char *DataEnd = Data + F.ModuleOffsetMap.size();
  F.ModuleOffsetMap = StringRef();

  using RemapBuilder = ContinuousRangeMap<uint32_t, int, 2>::Builder;
  RemapBuilder MacroRemap(F.MacroRemap);
  RemapBuilder PreprocessedEntityRemap(F.PreprocessedEntityRemap);
  RemapBuilder SubmoduleRemap(F.SubmoduleRemap);
  RemapBuilder SelectorRemap(F.SelectorRemap);

  auto &ImportedModuleVector = F.TransitiveImports;
  assert(ImportedModuleVector.empty());

  while (Data < DataEnd) {
    // FIXME: Looking up dependency modules by filename is horrible. Let's
    // start fixing this with prebuilt, explicit and implicit modules and see
    // how it goes...
    using namespace llvm::support;
    ModuleKind Kind = static_cast<ModuleKind>(
        endian::readNext<uint8_t, llvm::endianness::little>(Data));
    uint16_t Len = endian::readNext<uint16_t, llvm::endianness::little>(Data);
    StringRef Name = StringRef((const char*)Data, Len);
    Data += Len;
    ModuleFile *OM = (Kind == MK_PrebuiltModule || Kind == MK_ExplicitModule ||
                              Kind == MK_ImplicitModule
                          ? ModuleMgr.lookupByModuleName(Name)
                          : ModuleMgr.lookupByFileName(Name));
    if (!OM) {
      std::string Msg = "refers to unknown module, cannot find ";
      Msg.append(std::string(Name));
      Error(Msg);
      return;
    }

    ImportedModuleVector.push_back(OM);

    uint32_t MacroIDOffset =
        endian::readNext<uint32_t, llvm::endianness::little>(Data);
    uint32_t PreprocessedEntityIDOffset =
        endian::readNext<uint32_t, llvm::endianness::little>(Data);
    uint32_t SubmoduleIDOffset =
        endian::readNext<uint32_t, llvm::endianness::little>(Data);
    uint32_t SelectorIDOffset =
        endian::readNext<uint32_t, llvm::endianness::little>(Data);

    auto mapOffset = [&](uint32_t Offset, uint32_t BaseOffset,
                         RemapBuilder &Remap) {
      constexpr uint32_t None = std::numeric_limits<uint32_t>::max();
      if (Offset != None)
        Remap.insert(std::make_pair(Offset,
                                    static_cast<int>(BaseOffset - Offset)));
    };

    mapOffset(MacroIDOffset, OM->BaseMacroID, MacroRemap);
    mapOffset(PreprocessedEntityIDOffset, OM->BasePreprocessedEntityID,
              PreprocessedEntityRemap);
    mapOffset(SubmoduleIDOffset, OM->BaseSubmoduleID, SubmoduleRemap);
    mapOffset(SelectorIDOffset, OM->BaseSelectorID, SelectorRemap);
  }
}

ASTReader::ASTReadResult
ASTReader::ReadModuleMapFileBlock(RecordData &Record, ModuleFile &F,
                                  const ModuleFile *ImportedBy,
                                  unsigned ClientLoadCapabilities) {
  unsigned Idx = 0;
  F.ModuleMapPath = ReadPath(F, Record, Idx);

  // Try to resolve ModuleName in the current header search context and
  // verify that it is found in the same module map file as we saved. If the
  // top-level AST file is a main file, skip this check because there is no
  // usable header search context.
  assert(!F.ModuleName.empty() &&
         "MODULE_NAME should come before MODULE_MAP_FILE");
  if (PP.getPreprocessorOpts().ModulesCheckRelocated &&
      F.Kind == MK_ImplicitModule && ModuleMgr.begin()->Kind != MK_MainFile) {
    // An implicitly-loaded module file should have its module listed in some
    // module map file that we've already loaded.
    Module *M =
        PP.getHeaderSearchInfo().lookupModule(F.ModuleName, F.ImportLoc);
    auto &Map = PP.getHeaderSearchInfo().getModuleMap();
    OptionalFileEntryRef ModMap =
        M ? Map.getModuleMapFileForUniquing(M) : std::nullopt;
    // Don't emit module relocation error if we have -fno-validate-pch
    if (!bool(PP.getPreprocessorOpts().DisablePCHOrModuleValidation &
              DisableValidationForModuleKind::Module) &&
        !ModMap) {
      if (!canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities)) {
        if (auto ASTFE = M ? M->getASTFile() : std::nullopt) {
          // This module was defined by an imported (explicit) module.
          Diag(diag::err_module_file_conflict) << F.ModuleName << F.FileName
                                               << ASTFE->getName();
        } else {
          // This module was built with a different module map.
          Diag(diag::err_imported_module_not_found)
              << F.ModuleName << F.FileName
              << (ImportedBy ? ImportedBy->FileName : "") << F.ModuleMapPath
              << !ImportedBy;
          // In case it was imported by a PCH, there's a chance the user is
          // just missing to include the search path to the directory containing
          // the modulemap.
          if (ImportedBy && ImportedBy->Kind == MK_PCH)
            Diag(diag::note_imported_by_pch_module_not_found)
                << llvm::sys::path::parent_path(F.ModuleMapPath);
        }
      }
      return OutOfDate;
    }

    assert(M && M->Name == F.ModuleName && "found module with different name");

    // Check the primary module map file.
    auto StoredModMap = FileMgr.getFile(F.ModuleMapPath);
    if (!StoredModMap || *StoredModMap != ModMap) {
      assert(ModMap && "found module is missing module map file");
      assert((ImportedBy || F.Kind == MK_ImplicitModule) &&
             "top-level import should be verified");
      bool NotImported = F.Kind == MK_ImplicitModule && !ImportedBy;
      if (!canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities))
        Diag(diag::err_imported_module_modmap_changed)
            << F.ModuleName << (NotImported ? F.FileName : ImportedBy->FileName)
            << ModMap->getName() << F.ModuleMapPath << NotImported;
      return OutOfDate;
    }

    ModuleMap::AdditionalModMapsSet AdditionalStoredMaps;
    for (unsigned I = 0, N = Record[Idx++]; I < N; ++I) {
      // FIXME: we should use input files rather than storing names.
      std::string Filename = ReadPath(F, Record, Idx);
      auto SF = FileMgr.getOptionalFileRef(Filename, false, false);
      if (!SF) {
        if (!canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities))
          Error("could not find file '" + Filename +"' referenced by AST file");
        return OutOfDate;
      }
      AdditionalStoredMaps.insert(*SF);
    }

    // Check any additional module map files (e.g. module.private.modulemap)
    // that are not in the pcm.
    if (auto *AdditionalModuleMaps = Map.getAdditionalModuleMapFiles(M)) {
      for (FileEntryRef ModMap : *AdditionalModuleMaps) {
        // Remove files that match
        // Note: SmallPtrSet::erase is really remove
        if (!AdditionalStoredMaps.erase(ModMap)) {
          if (!canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities))
            Diag(diag::err_module_different_modmap)
              << F.ModuleName << /*new*/0 << ModMap.getName();
          return OutOfDate;
        }
      }
    }

    // Check any additional module map files that are in the pcm, but not
    // found in header search. Cases that match are already removed.
    for (FileEntryRef ModMap : AdditionalStoredMaps) {
      if (!canRecoverFromOutOfDate(F.FileName, ClientLoadCapabilities))
        Diag(diag::err_module_different_modmap)
          << F.ModuleName << /*not new*/1 << ModMap.getName();
      return OutOfDate;
    }
  }

  if (Listener)
    Listener->ReadModuleMapFile(F.ModuleMapPath);
  return Success;
}

/// Move the given method to the back of the global list of methods.
static void moveMethodToBackOfGlobalList(Sema &S, ObjCMethodDecl *Method) {
  // Find the entry for this selector in the method pool.
  SemaObjC::GlobalMethodPool::iterator Known =
      S.ObjC().MethodPool.find(Method->getSelector());
  if (Known == S.ObjC().MethodPool.end())
    return;

  // Retrieve the appropriate method list.
  ObjCMethodList &Start = Method->isInstanceMethod()? Known->second.first
                                                    : Known->second.second;
  bool Found = false;
  for (ObjCMethodList *List = &Start; List; List = List->getNext()) {
    if (!Found) {
      if (List->getMethod() == Method) {
        Found = true;
      } else {
        // Keep searching.
        continue;
      }
    }

    if (List->getNext())
      List->setMethod(List->getNext()->getMethod());
    else
      List->setMethod(Method);
  }
}

void ASTReader::makeNamesVisible(const HiddenNames &Names, Module *Owner) {
  assert(Owner->NameVisibility != Module::Hidden && "nothing to make visible?");
  for (Decl *D : Names) {
    bool wasHidden = !D->isUnconditionallyVisible();
    D->setVisibleDespiteOwningModule();

    if (wasHidden && SemaObj) {
      if (ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(D)) {
        moveMethodToBackOfGlobalList(*SemaObj, Method);
      }
    }
  }
}

void ASTReader::makeModuleVisible(Module *Mod,
                                  Module::NameVisibilityKind NameVisibility,
                                  SourceLocation ImportLoc) {
  llvm::SmallPtrSet<Module *, 4> Visited;
  SmallVector<Module *, 4> Stack;
  Stack.push_back(Mod);
  while (!Stack.empty()) {
    Mod = Stack.pop_back_val();

    if (NameVisibility <= Mod->NameVisibility) {
      // This module already has this level of visibility (or greater), so
      // there is nothing more to do.
      continue;
    }

    if (Mod->isUnimportable()) {
      // Modules that aren't importable cannot be made visible.
      continue;
    }

    // Update the module's name visibility.
    Mod->NameVisibility = NameVisibility;

    // If we've already deserialized any names from this module,
    // mark them as visible.
    HiddenNamesMapType::iterator Hidden = HiddenNamesMap.find(Mod);
    if (Hidden != HiddenNamesMap.end()) {
      auto HiddenNames = std::move(*Hidden);
      HiddenNamesMap.erase(Hidden);
      makeNamesVisible(HiddenNames.second, HiddenNames.first);
      assert(!HiddenNamesMap.contains(Mod) &&
             "making names visible added hidden names");
    }

    // Push any exported modules onto the stack to be marked as visible.
    SmallVector<Module *, 16> Exports;
    Mod->getExportedModules(Exports);
    for (SmallVectorImpl<Module *>::iterator
           I = Exports.begin(), E = Exports.end(); I != E; ++I) {
      Module *Exported = *I;
      if (Visited.insert(Exported).second)
        Stack.push_back(Exported);
    }
  }
}

/// We've merged the definition \p MergedDef into the existing definition
/// \p Def. Ensure that \p Def is made visible whenever \p MergedDef is made
/// visible.
void ASTReader::mergeDefinitionVisibility(NamedDecl *Def,
                                          NamedDecl *MergedDef) {
  if (!Def->isUnconditionallyVisible()) {
    // If MergedDef is visible or becomes visible, make the definition visible.
    if (MergedDef->isUnconditionallyVisible())
      Def->setVisibleDespiteOwningModule();
    else {
      getContext().mergeDefinitionIntoModule(
          Def, MergedDef->getImportedOwningModule(),
          /*NotifyListeners*/ false);
      PendingMergedDefinitionsToDeduplicate.insert(Def);
    }
  }
}

bool ASTReader::loadGlobalIndex() {
  if (GlobalIndex)
    return false;

  if (TriedLoadingGlobalIndex || !UseGlobalIndex ||
      !PP.getLangOpts().Modules)
    return true;

  // Try to load the global index.
  TriedLoadingGlobalIndex = true;
  StringRef ModuleCachePath
    = getPreprocessor().getHeaderSearchInfo().getModuleCachePath();
  std::pair<GlobalModuleIndex *, llvm::Error> Result =
      GlobalModuleIndex::readIndex(ModuleCachePath);
  if (llvm::Error Err = std::move(Result.second)) {
    assert(!Result.first);
    consumeError(std::move(Err)); // FIXME this drops errors on the floor.
    return true;
  }

  GlobalIndex.reset(Result.first);
  ModuleMgr.setGlobalIndex(GlobalIndex.get());
  return false;
}

bool ASTReader::isGlobalIndexUnavailable() const {
  return PP.getLangOpts().Modules && UseGlobalIndex &&
         !hasGlobalIndex() && TriedLoadingGlobalIndex;
}

static void updateModuleTimestamp(ModuleFile &MF) {
  // Overwrite the timestamp file contents so that file's mtime changes.
  std::string TimestampFilename = MF.getTimestampFilename();
  std::error_code EC;
  llvm::raw_fd_ostream OS(TimestampFilename, EC,
                          llvm::sys::fs::OF_TextWithCRLF);
  if (EC)
    return;
  OS << "Timestamp file\n";
  OS.close();
  OS.clear_error(); // Avoid triggering a fatal error.
}

/// Given a cursor at the start of an AST file, scan ahead and drop the
/// cursor into the start of the given block ID, returning false on success and
/// true on failure.
static bool SkipCursorToBlock(BitstreamCursor &Cursor, unsigned BlockID) {
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Cursor.advance();
    if (!MaybeEntry) {
      // FIXME this drops errors on the floor.
      consumeError(MaybeEntry.takeError());
      return true;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::EndBlock:
      return true;

    case llvm::BitstreamEntry::Record:
      // Ignore top-level records.
      if (Expected<unsigned> Skipped = Cursor.skipRecord(Entry.ID))
        break;
      else {
        // FIXME this drops errors on the floor.
        consumeError(Skipped.takeError());
        return true;
      }

    case llvm::BitstreamEntry::SubBlock:
      if (Entry.ID == BlockID) {
        if (llvm::Error Err = Cursor.EnterSubBlock(BlockID)) {
          // FIXME this drops the error on the floor.
          consumeError(std::move(Err));
          return true;
        }
        // Found it!
        return false;
      }

      if (llvm::Error Err = Cursor.SkipBlock()) {
        // FIXME this drops the error on the floor.
        consumeError(std::move(Err));
        return true;
      }
    }
  }
}

ASTReader::ASTReadResult ASTReader::ReadAST(StringRef FileName, ModuleKind Type,
                                            SourceLocation ImportLoc,
                                            unsigned ClientLoadCapabilities,
                                            ModuleFile **NewLoadedModuleFile) {
  llvm::TimeTraceScope scope("ReadAST", FileName);

  llvm::SaveAndRestore SetCurImportLocRAII(CurrentImportLoc, ImportLoc);
  llvm::SaveAndRestore<std::optional<ModuleKind>> SetCurModuleKindRAII(
      CurrentDeserializingModuleKind, Type);

  // Defer any pending actions until we get to the end of reading the AST file.
  Deserializing AnASTFile(this);

  // Bump the generation number.
  unsigned PreviousGeneration = 0;
  if (ContextObj)
    PreviousGeneration = incrementGeneration(*ContextObj);

  unsigned NumModules = ModuleMgr.size();
  SmallVector<ImportedModule, 4> Loaded;
  if (ASTReadResult ReadResult =
          ReadASTCore(FileName, Type, ImportLoc,
                      /*ImportedBy=*/nullptr, Loaded, 0, 0, ASTFileSignature(),
                      ClientLoadCapabilities)) {
    ModuleMgr.removeModules(ModuleMgr.begin() + NumModules);

    // If we find that any modules are unusable, the global index is going
    // to be out-of-date. Just remove it.
    GlobalIndex.reset();
    ModuleMgr.setGlobalIndex(nullptr);
    return ReadResult;
  }

  if (NewLoadedModuleFile && !Loaded.empty())
    *NewLoadedModuleFile = Loaded.back().Mod;

  // Here comes stuff that we only do once the entire chain is loaded. Do *not*
  // remove modules from this point. Various fields are updated during reading
  // the AST block and removing the modules would result in dangling pointers.
  // They are generally only incidentally dereferenced, ie. a binary search
  // runs over `GlobalSLocEntryMap`, which could cause an invalid module to
  // be dereferenced but it wouldn't actually be used.

  // Load the AST blocks of all of the modules that we loaded. We can still
  // hit errors parsing the ASTs at this point.
  for (ImportedModule &M : Loaded) {
    ModuleFile &F = *M.Mod;
    llvm::TimeTraceScope Scope2("Read Loaded AST", F.ModuleName);

    // Read the AST block.
    if (llvm::Error Err = ReadASTBlock(F, ClientLoadCapabilities)) {
      Error(std::move(Err));
      return Failure;
    }

    // The AST block should always have a definition for the main module.
    if (F.isModule() && !F.DidReadTopLevelSubmodule) {
      Error(diag::err_module_file_missing_top_level_submodule, F.FileName);
      return Failure;
    }

    // Read the extension blocks.
    while (!SkipCursorToBlock(F.Stream, EXTENSION_BLOCK_ID)) {
      if (llvm::Error Err = ReadExtensionBlock(F)) {
        Error(std::move(Err));
        return Failure;
      }
    }

    // Once read, set the ModuleFile bit base offset and update the size in
    // bits of all files we've seen.
    F.GlobalBitOffset = TotalModulesSizeInBits;
    TotalModulesSizeInBits += F.SizeInBits;
    GlobalBitOffsetsMap.insert(std::make_pair(F.GlobalBitOffset, &F));
  }

  // Preload source locations and interesting indentifiers.
  for (ImportedModule &M : Loaded) {
    ModuleFile &F = *M.Mod;

    // Map the original source file ID into the ID space of the current
    // compilation.
    if (F.OriginalSourceFileID.isValid())
      F.OriginalSourceFileID = TranslateFileID(F, F.OriginalSourceFileID);

    for (auto Offset : F.PreloadIdentifierOffsets) {
      const unsigned char *Data = F.IdentifierTableData + Offset;

      ASTIdentifierLookupTrait Trait(*this, F);
      auto KeyDataLen = Trait.ReadKeyDataLength(Data);
      auto Key = Trait.ReadKey(Data, KeyDataLen.first);

      IdentifierInfo *II;
      if (!PP.getLangOpts().CPlusPlus) {
        // Identifiers present in both the module file and the importing
        // instance are marked out-of-date so that they can be deserialized
        // on next use via ASTReader::updateOutOfDateIdentifier().
        // Identifiers present in the module file but not in the importing
        // instance are ignored for now, preventing growth of the identifier
        // table. They will be deserialized on first use via ASTReader::get().
        auto It = PP.getIdentifierTable().find(Key);
        if (It == PP.getIdentifierTable().end())
          continue;
        II = It->second;
      } else {
        // With C++ modules, not many identifiers are considered interesting.
        // All identifiers in the module file can be placed into the identifier
        // table of the importing instance and marked as out-of-date. This makes
        // ASTReader::get() a no-op, and deserialization will take place on
        // first/next use via ASTReader::updateOutOfDateIdentifier().
        II = &PP.getIdentifierTable().getOwn(Key);
      }

      II->setOutOfDate(true);

      // Mark this identifier as being from an AST file so that we can track
      // whether we need to serialize it.
      markIdentifierFromAST(*this, *II);

      // Associate the ID with the identifier so that the writer can reuse it.
      auto ID = Trait.ReadIdentifierID(Data + KeyDataLen.first);
      SetIdentifierInfo(ID, II);
    }
  }

  // Builtins and library builtins have already been initialized. Mark all
  // identifiers as out-of-date, so that they are deserialized on first use.
  if (Type == MK_PCH || Type == MK_Preamble || Type == MK_MainFile)
    for (auto &Id : PP.getIdentifierTable())
      Id.second->setOutOfDate(true);

  // Mark selectors as out of date.
  for (const auto &Sel : SelectorGeneration)
    SelectorOutOfDate[Sel.first] = true;

  // Setup the import locations and notify the module manager that we've
  // committed to these module files.
  for (ImportedModule &M : Loaded) {
    ModuleFile &F = *M.Mod;

    ModuleMgr.moduleFileAccepted(&F);

    // Set the import location.
    F.DirectImportLoc = ImportLoc;
    // FIXME: We assume that locations from PCH / preamble do not need
    // any translation.
    if (!M.ImportedBy)
      F.ImportLoc = M.ImportLoc;
    else
      F.ImportLoc = TranslateSourceLocation(*M.ImportedBy, M.ImportLoc);
  }

  // Resolve any unresolved module exports.
  for (unsigned I = 0, N = UnresolvedModuleRefs.size(); I != N; ++I) {
    UnresolvedModuleRef &Unresolved = UnresolvedModuleRefs[I];
    SubmoduleID GlobalID = getGlobalSubmoduleID(*Unresolved.File,Unresolved.ID);
    Module *ResolvedMod = getSubmodule(GlobalID);

    switch (Unresolved.Kind) {
    case UnresolvedModuleRef::Conflict:
      if (ResolvedMod) {
        Module::Conflict Conflict;
        Conflict.Other = ResolvedMod;
        Conflict.Message = Unresolved.String.str();
        Unresolved.Mod->Conflicts.push_back(Conflict);
      }
      continue;

    case UnresolvedModuleRef::Import:
      if (ResolvedMod)
        Unresolved.Mod->Imports.insert(ResolvedMod);
      continue;

    case UnresolvedModuleRef::Affecting:
      if (ResolvedMod)
        Unresolved.Mod->AffectingClangModules.insert(ResolvedMod);
      continue;

    case UnresolvedModuleRef::Export:
      if (ResolvedMod || Unresolved.IsWildcard)
        Unresolved.Mod->Exports.push_back(
          Module::ExportDecl(ResolvedMod, Unresolved.IsWildcard));
      continue;
    }
  }
  UnresolvedModuleRefs.clear();

  // FIXME: How do we load the 'use'd modules? They may not be submodules.
  // Might be unnecessary as use declarations are only used to build the
  // module itself.

  if (ContextObj)
    InitializeContext();

  if (SemaObj)
    UpdateSema();

  if (DeserializationListener)
    DeserializationListener->ReaderInitialized(this);

  ModuleFile &PrimaryModule = ModuleMgr.getPrimaryModule();
  if (PrimaryModule.OriginalSourceFileID.isValid()) {
    // If this AST file is a precompiled preamble, then set the
    // preamble file ID of the source manager to the file source file
    // from which the preamble was built.
    if (Type == MK_Preamble) {
      SourceMgr.setPreambleFileID(PrimaryModule.OriginalSourceFileID);
    } else if (Type == MK_MainFile) {
      SourceMgr.setMainFileID(PrimaryModule.OriginalSourceFileID);
    }
  }

  // For any Objective-C class definitions we have already loaded, make sure
  // that we load any additional categories.
  if (ContextObj) {
    for (unsigned I = 0, N = ObjCClassesLoaded.size(); I != N; ++I) {
      loadObjCCategories(ObjCClassesLoaded[I]->getGlobalID(),
                         ObjCClassesLoaded[I], PreviousGeneration);
    }
  }

  HeaderSearchOptions &HSOpts = PP.getHeaderSearchInfo().getHeaderSearchOpts();
  if (HSOpts.ModulesValidateOncePerBuildSession) {
    // Now we are certain that the module and all modules it depends on are
    // up-to-date. For implicitly-built module files, ensure the corresponding
    // timestamp files are up-to-date in this build session.
    for (unsigned I = 0, N = Loaded.size(); I != N; ++I) {
      ImportedModule &M = Loaded[I];
      if (M.Mod->Kind == MK_ImplicitModule &&
          M.Mod->InputFilesValidationTimestamp < HSOpts.BuildSessionTimestamp)
        updateModuleTimestamp(*M.Mod);
    }
  }

  return Success;
}

static ASTFileSignature readASTFileSignature(StringRef PCH);

/// Whether \p Stream doesn't start with the AST/PCH file magic number 'CPCH'.
static llvm::Error doesntStartWithASTFileMagic(BitstreamCursor &Stream) {
  // FIXME checking magic headers is done in other places such as
  // SerializedDiagnosticReader and GlobalModuleIndex, but error handling isn't
  // always done the same. Unify it all with a helper.
  if (!Stream.canSkipToPos(4))
    return llvm::createStringError(std::errc::illegal_byte_sequence,
                                   "file too small to contain AST file magic");
  for (unsigned C : {'C', 'P', 'C', 'H'})
    if (Expected<llvm::SimpleBitstreamCursor::word_t> Res = Stream.Read(8)) {
      if (Res.get() != C)
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "file doesn't start with AST file magic");
    } else
      return Res.takeError();
  return llvm::Error::success();
}

static unsigned moduleKindForDiagnostic(ModuleKind Kind) {
  switch (Kind) {
  case MK_PCH:
    return 0; // PCH
  case MK_ImplicitModule:
  case MK_ExplicitModule:
  case MK_PrebuiltModule:
    return 1; // module
  case MK_MainFile:
  case MK_Preamble:
    return 2; // main source file
  }
  llvm_unreachable("unknown module kind");
}

ASTReader::ASTReadResult
ASTReader::ReadASTCore(StringRef FileName,
                       ModuleKind Type,
                       SourceLocation ImportLoc,
                       ModuleFile *ImportedBy,
                       SmallVectorImpl<ImportedModule> &Loaded,
                       off_t ExpectedSize, time_t ExpectedModTime,
                       ASTFileSignature ExpectedSignature,
                       unsigned ClientLoadCapabilities) {
  ModuleFile *M;
  std::string ErrorStr;
  ModuleManager::AddModuleResult AddResult
    = ModuleMgr.addModule(FileName, Type, ImportLoc, ImportedBy,
                          getGeneration(), ExpectedSize, ExpectedModTime,
                          ExpectedSignature, readASTFileSignature,
                          M, ErrorStr);

  switch (AddResult) {
  case ModuleManager::AlreadyLoaded:
    Diag(diag::remark_module_import)
        << M->ModuleName << M->FileName << (ImportedBy ? true : false)
        << (ImportedBy ? StringRef(ImportedBy->ModuleName) : StringRef());
    return Success;

  case ModuleManager::NewlyLoaded:
    // Load module file below.
    break;

  case ModuleManager::Missing:
    // The module file was missing; if the client can handle that, return
    // it.
    if (ClientLoadCapabilities & ARR_Missing)
      return Missing;

    // Otherwise, return an error.
    Diag(diag::err_ast_file_not_found)
        << moduleKindForDiagnostic(Type) << FileName << !ErrorStr.empty()
        << ErrorStr;
    return Failure;

  case ModuleManager::OutOfDate:
    // We couldn't load the module file because it is out-of-date. If the
    // client can handle out-of-date, return it.
    if (ClientLoadCapabilities & ARR_OutOfDate)
      return OutOfDate;

    // Otherwise, return an error.
    Diag(diag::err_ast_file_out_of_date)
        << moduleKindForDiagnostic(Type) << FileName << !ErrorStr.empty()
        << ErrorStr;
    return Failure;
  }

  assert(M && "Missing module file");

  bool ShouldFinalizePCM = false;
  auto FinalizeOrDropPCM = llvm::make_scope_exit([&]() {
    auto &MC = getModuleManager().getModuleCache();
    if (ShouldFinalizePCM)
      MC.finalizePCM(FileName);
    else
      MC.tryToDropPCM(FileName);
  });
  ModuleFile &F = *M;
  BitstreamCursor &Stream = F.Stream;
  Stream = BitstreamCursor(PCHContainerRdr.ExtractPCH(*F.Buffer));
  F.SizeInBits = F.Buffer->getBufferSize() * 8;

  // Sniff for the signature.
  if (llvm::Error Err = doesntStartWithASTFileMagic(Stream)) {
    Diag(diag::err_ast_file_invalid)
        << moduleKindForDiagnostic(Type) << FileName << std::move(Err);
    return Failure;
  }

  // This is used for compatibility with older PCH formats.
  bool HaveReadControlBlock = false;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry) {
      Error(MaybeEntry.takeError());
      return Failure;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::Record:
    case llvm::BitstreamEntry::EndBlock:
      Error("invalid record at top-level of AST file");
      return Failure;

    case llvm::BitstreamEntry::SubBlock:
      break;
    }

    switch (Entry.ID) {
    case CONTROL_BLOCK_ID:
      HaveReadControlBlock = true;
      switch (ReadControlBlock(F, Loaded, ImportedBy, ClientLoadCapabilities)) {
      case Success:
        // Check that we didn't try to load a non-module AST file as a module.
        //
        // FIXME: Should we also perform the converse check? Loading a module as
        // a PCH file sort of works, but it's a bit wonky.
        if ((Type == MK_ImplicitModule || Type == MK_ExplicitModule ||
             Type == MK_PrebuiltModule) &&
            F.ModuleName.empty()) {
          auto Result = (Type == MK_ImplicitModule) ? OutOfDate : Failure;
          if (Result != OutOfDate ||
              (ClientLoadCapabilities & ARR_OutOfDate) == 0)
            Diag(diag::err_module_file_not_module) << FileName;
          return Result;
        }
        break;

      case Failure: return Failure;
      case Missing: return Missing;
      case OutOfDate: return OutOfDate;
      case VersionMismatch: return VersionMismatch;
      case ConfigurationMismatch: return ConfigurationMismatch;
      case HadErrors: return HadErrors;
      }
      break;

    case AST_BLOCK_ID:
      if (!HaveReadControlBlock) {
        if ((ClientLoadCapabilities & ARR_VersionMismatch) == 0)
          Diag(diag::err_pch_version_too_old);
        return VersionMismatch;
      }

      // Record that we've loaded this module.
      Loaded.push_back(ImportedModule(M, ImportedBy, ImportLoc));
      ShouldFinalizePCM = true;
      return Success;

    default:
      if (llvm::Error Err = Stream.SkipBlock()) {
        Error(std::move(Err));
        return Failure;
      }
      break;
    }
  }

  llvm_unreachable("unexpected break; expected return");
}

ASTReader::ASTReadResult
ASTReader::readUnhashedControlBlock(ModuleFile &F, bool WasImportedBy,
                                    unsigned ClientLoadCapabilities) {
  const HeaderSearchOptions &HSOpts =
      PP.getHeaderSearchInfo().getHeaderSearchOpts();
  bool AllowCompatibleConfigurationMismatch =
      F.Kind == MK_ExplicitModule || F.Kind == MK_PrebuiltModule;
  bool DisableValidation = shouldDisableValidationForFile(F);

  ASTReadResult Result = readUnhashedControlBlockImpl(
      &F, F.Data, ClientLoadCapabilities, AllowCompatibleConfigurationMismatch,
      Listener.get(),
      WasImportedBy ? false : HSOpts.ModulesValidateDiagnosticOptions);

  // If F was directly imported by another module, it's implicitly validated by
  // the importing module.
  if (DisableValidation || WasImportedBy ||
      (AllowConfigurationMismatch && Result == ConfigurationMismatch))
    return Success;

  if (Result == Failure) {
    Error("malformed block record in AST file");
    return Failure;
  }

  if (Result == OutOfDate && F.Kind == MK_ImplicitModule) {
    // If this module has already been finalized in the ModuleCache, we're stuck
    // with it; we can only load a single version of each module.
    //
    // This can happen when a module is imported in two contexts: in one, as a
    // user module; in another, as a system module (due to an import from
    // another module marked with the [system] flag).  It usually indicates a
    // bug in the module map: this module should also be marked with [system].
    //
    // If -Wno-system-headers (the default), and the first import is as a
    // system module, then validation will fail during the as-user import,
    // since -Werror flags won't have been validated.  However, it's reasonable
    // to treat this consistently as a system module.
    //
    // If -Wsystem-headers, the PCM on disk was built with
    // -Wno-system-headers, and the first import is as a user module, then
    // validation will fail during the as-system import since the PCM on disk
    // doesn't guarantee that -Werror was respected.  However, the -Werror
    // flags were checked during the initial as-user import.
    if (getModuleManager().getModuleCache().isPCMFinal(F.FileName)) {
      Diag(diag::warn_module_system_bit_conflict) << F.FileName;
      return Success;
    }
  }

  return Result;
}

ASTReader::ASTReadResult ASTReader::readUnhashedControlBlockImpl(
    ModuleFile *F, llvm::StringRef StreamData, unsigned ClientLoadCapabilities,
    bool AllowCompatibleConfigurationMismatch, ASTReaderListener *Listener,
    bool ValidateDiagnosticOptions) {
  // Initialize a stream.
  BitstreamCursor Stream(StreamData);

  // Sniff for the signature.
  if (llvm::Error Err = doesntStartWithASTFileMagic(Stream)) {
    // FIXME this drops the error on the floor.
    consumeError(std::move(Err));
    return Failure;
  }

  // Scan for the UNHASHED_CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, UNHASHED_CONTROL_BLOCK_ID))
    return Failure;

  // Read all of the records in the options block.
  RecordData Record;
  ASTReadResult Result = Success;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeEntry.takeError());
      return Failure;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::SubBlock:
      return Failure;

    case llvm::BitstreamEntry::EndBlock:
      return Result;

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecordType =
        Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecordType) {
      // FIXME this drops the error.
      return Failure;
    }
    switch ((UnhashedControlBlockRecordTypes)MaybeRecordType.get()) {
    case SIGNATURE:
      if (F) {
        F->Signature = ASTFileSignature::create(Blob.begin(), Blob.end());
        assert(F->Signature != ASTFileSignature::createDummy() &&
               "Dummy AST file signature not backpatched in ASTWriter.");
      }
      break;
    case AST_BLOCK_HASH:
      if (F) {
        F->ASTBlockHash = ASTFileSignature::create(Blob.begin(), Blob.end());
        assert(F->ASTBlockHash != ASTFileSignature::createDummy() &&
               "Dummy AST block hash not backpatched in ASTWriter.");
      }
      break;
    case DIAGNOSTIC_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_OutOfDate) == 0;
      if (Listener && ValidateDiagnosticOptions &&
          !AllowCompatibleConfigurationMismatch &&
          ParseDiagnosticOptions(Record, Complain, *Listener))
        Result = OutOfDate; // Don't return early.  Read the signature.
      break;
    }
    case HEADER_SEARCH_PATHS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (Listener && !AllowCompatibleConfigurationMismatch &&
          ParseHeaderSearchPaths(Record, Complain, *Listener))
        Result = ConfigurationMismatch;
      break;
    }
    case DIAG_PRAGMA_MAPPINGS:
      if (!F)
        break;
      if (F->PragmaDiagMappings.empty())
        F->PragmaDiagMappings.swap(Record);
      else
        F->PragmaDiagMappings.insert(F->PragmaDiagMappings.end(),
                                     Record.begin(), Record.end());
      break;
    case HEADER_SEARCH_ENTRY_USAGE:
      if (F)
        F->SearchPathUsage = ReadBitVector(Record, Blob);
      break;
    case VFS_USAGE:
      if (F)
        F->VFSUsage = ReadBitVector(Record, Blob);
      break;
    }
  }
}

/// Parse a record and blob containing module file extension metadata.
static bool parseModuleFileExtensionMetadata(
              const SmallVectorImpl<uint64_t> &Record,
              StringRef Blob,
              ModuleFileExtensionMetadata &Metadata) {
  if (Record.size() < 4) return true;

  Metadata.MajorVersion = Record[0];
  Metadata.MinorVersion = Record[1];

  unsigned BlockNameLen = Record[2];
  unsigned UserInfoLen = Record[3];

  if (BlockNameLen + UserInfoLen > Blob.size()) return true;

  Metadata.BlockName = std::string(Blob.data(), Blob.data() + BlockNameLen);
  Metadata.UserInfo = std::string(Blob.data() + BlockNameLen,
                                  Blob.data() + BlockNameLen + UserInfoLen);
  return false;
}

llvm::Error ASTReader::ReadExtensionBlock(ModuleFile &F) {
  BitstreamCursor &Stream = F.Stream;

  RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry)
      return MaybeEntry.takeError();
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock:
      if (llvm::Error Err = Stream.SkipBlock())
        return Err;
      continue;
    case llvm::BitstreamEntry::EndBlock:
      return llvm::Error::success();
    case llvm::BitstreamEntry::Error:
      return llvm::createStringError(std::errc::illegal_byte_sequence,
                                     "malformed block record in AST file");
    case llvm::BitstreamEntry::Record:
      break;
    }

    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecCode =
        Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecCode)
      return MaybeRecCode.takeError();
    switch (MaybeRecCode.get()) {
    case EXTENSION_METADATA: {
      ModuleFileExtensionMetadata Metadata;
      if (parseModuleFileExtensionMetadata(Record, Blob, Metadata))
        return llvm::createStringError(
            std::errc::illegal_byte_sequence,
            "malformed EXTENSION_METADATA in AST file");

      // Find a module file extension with this block name.
      auto Known = ModuleFileExtensions.find(Metadata.BlockName);
      if (Known == ModuleFileExtensions.end()) break;

      // Form a reader.
      if (auto Reader = Known->second->createExtensionReader(Metadata, *this,
                                                             F, Stream)) {
        F.ExtensionReaders.push_back(std::move(Reader));
      }

      break;
    }
    }
  }

  return llvm::Error::success();
}

void ASTReader::InitializeContext() {
  assert(ContextObj && "no context to initialize");
  ASTContext &Context = *ContextObj;

  // If there's a listener, notify them that we "read" the translation unit.
  if (DeserializationListener)
    DeserializationListener->DeclRead(
        GlobalDeclID(PREDEF_DECL_TRANSLATION_UNIT_ID),
        Context.getTranslationUnitDecl());

  // FIXME: Find a better way to deal with collisions between these
  // built-in types. Right now, we just ignore the problem.

  // Load the special types.
  if (SpecialTypes.size() >= NumSpecialTypeIDs) {
    if (TypeID String = SpecialTypes[SPECIAL_TYPE_CF_CONSTANT_STRING]) {
      if (!Context.CFConstantStringTypeDecl)
        Context.setCFConstantStringType(GetType(String));
    }

    if (TypeID File = SpecialTypes[SPECIAL_TYPE_FILE]) {
      QualType FileType = GetType(File);
      if (FileType.isNull()) {
        Error("FILE type is NULL");
        return;
      }

      if (!Context.FILEDecl) {
        if (const TypedefType *Typedef = FileType->getAs<TypedefType>())
          Context.setFILEDecl(Typedef->getDecl());
        else {
          const TagType *Tag = FileType->getAs<TagType>();
          if (!Tag) {
            Error("Invalid FILE type in AST file");
            return;
          }
          Context.setFILEDecl(Tag->getDecl());
        }
      }
    }

    if (TypeID Jmp_buf = SpecialTypes[SPECIAL_TYPE_JMP_BUF]) {
      QualType Jmp_bufType = GetType(Jmp_buf);
      if (Jmp_bufType.isNull()) {
        Error("jmp_buf type is NULL");
        return;
      }

      if (!Context.jmp_bufDecl) {
        if (const TypedefType *Typedef = Jmp_bufType->getAs<TypedefType>())
          Context.setjmp_bufDecl(Typedef->getDecl());
        else {
          const TagType *Tag = Jmp_bufType->getAs<TagType>();
          if (!Tag) {
            Error("Invalid jmp_buf type in AST file");
            return;
          }
          Context.setjmp_bufDecl(Tag->getDecl());
        }
      }
    }

    if (TypeID Sigjmp_buf = SpecialTypes[SPECIAL_TYPE_SIGJMP_BUF]) {
      QualType Sigjmp_bufType = GetType(Sigjmp_buf);
      if (Sigjmp_bufType.isNull()) {
        Error("sigjmp_buf type is NULL");
        return;
      }

      if (!Context.sigjmp_bufDecl) {
        if (const TypedefType *Typedef = Sigjmp_bufType->getAs<TypedefType>())
          Context.setsigjmp_bufDecl(Typedef->getDecl());
        else {
          const TagType *Tag = Sigjmp_bufType->getAs<TagType>();
          assert(Tag && "Invalid sigjmp_buf type in AST file");
          Context.setsigjmp_bufDecl(Tag->getDecl());
        }
      }
    }

    if (TypeID ObjCIdRedef = SpecialTypes[SPECIAL_TYPE_OBJC_ID_REDEFINITION]) {
      if (Context.ObjCIdRedefinitionType.isNull())
        Context.ObjCIdRedefinitionType = GetType(ObjCIdRedef);
    }

    if (TypeID ObjCClassRedef =
            SpecialTypes[SPECIAL_TYPE_OBJC_CLASS_REDEFINITION]) {
      if (Context.ObjCClassRedefinitionType.isNull())
        Context.ObjCClassRedefinitionType = GetType(ObjCClassRedef);
    }

    if (TypeID ObjCSelRedef =
            SpecialTypes[SPECIAL_TYPE_OBJC_SEL_REDEFINITION]) {
      if (Context.ObjCSelRedefinitionType.isNull())
        Context.ObjCSelRedefinitionType = GetType(ObjCSelRedef);
    }

    if (TypeID Ucontext_t = SpecialTypes[SPECIAL_TYPE_UCONTEXT_T]) {
      QualType Ucontext_tType = GetType(Ucontext_t);
      if (Ucontext_tType.isNull()) {
        Error("ucontext_t type is NULL");
        return;
      }

      if (!Context.ucontext_tDecl) {
        if (const TypedefType *Typedef = Ucontext_tType->getAs<TypedefType>())
          Context.setucontext_tDecl(Typedef->getDecl());
        else {
          const TagType *Tag = Ucontext_tType->getAs<TagType>();
          assert(Tag && "Invalid ucontext_t type in AST file");
          Context.setucontext_tDecl(Tag->getDecl());
        }
      }
    }
  }

  ReadPragmaDiagnosticMappings(Context.getDiagnostics());

  // If there were any CUDA special declarations, deserialize them.
  if (!CUDASpecialDeclRefs.empty()) {
    assert(CUDASpecialDeclRefs.size() == 1 && "More decl refs than expected!");
    Context.setcudaConfigureCallDecl(
                           cast<FunctionDecl>(GetDecl(CUDASpecialDeclRefs[0])));
  }

  // Re-export any modules that were imported by a non-module AST file.
  // FIXME: This does not make macro-only imports visible again.
  for (auto &Import : PendingImportedModules) {
    if (Module *Imported = getSubmodule(Import.ID)) {
      makeModuleVisible(Imported, Module::AllVisible,
                        /*ImportLoc=*/Import.ImportLoc);
      if (Import.ImportLoc.isValid())
        PP.makeModuleVisible(Imported, Import.ImportLoc);
      // This updates visibility for Preprocessor only. For Sema, which can be
      // nullptr here, we do the same later, in UpdateSema().
    }
  }

  // Hand off these modules to Sema.
  PendingImportedModulesSema.append(PendingImportedModules);
  PendingImportedModules.clear();
}

void ASTReader::finalizeForWriting() {
  // Nothing to do for now.
}

/// Reads and return the signature record from \p PCH's control block, or
/// else returns 0.
static ASTFileSignature readASTFileSignature(StringRef PCH) {
  BitstreamCursor Stream(PCH);
  if (llvm::Error Err = doesntStartWithASTFileMagic(Stream)) {
    // FIXME this drops the error on the floor.
    consumeError(std::move(Err));
    return ASTFileSignature();
  }

  // Scan for the UNHASHED_CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, UNHASHED_CONTROL_BLOCK_ID))
    return ASTFileSignature();

  // Scan for SIGNATURE inside the diagnostic options block.
  ASTReader::RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry =
        Stream.advanceSkippingSubblocks();
    if (!MaybeEntry) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeEntry.takeError());
      return ASTFileSignature();
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    if (Entry.Kind != llvm::BitstreamEntry::Record)
      return ASTFileSignature();

    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecord = Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecord) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeRecord.takeError());
      return ASTFileSignature();
    }
    if (SIGNATURE == MaybeRecord.get()) {
      auto Signature = ASTFileSignature::create(Blob.begin(), Blob.end());
      assert(Signature != ASTFileSignature::createDummy() &&
             "Dummy AST file signature not backpatched in ASTWriter.");
      return Signature;
    }
  }
}

/// Retrieve the name of the original source file name
/// directly from the AST file, without actually loading the AST
/// file.
std::string ASTReader::getOriginalSourceFile(
    const std::string &ASTFileName, FileManager &FileMgr,
    const PCHContainerReader &PCHContainerRdr, DiagnosticsEngine &Diags) {
  // Open the AST file.
  auto Buffer = FileMgr.getBufferForFile(ASTFileName, /*IsVolatile=*/false,
                                         /*RequiresNullTerminator=*/false);
  if (!Buffer) {
    Diags.Report(diag::err_fe_unable_to_read_pch_file)
        << ASTFileName << Buffer.getError().message();
    return std::string();
  }

  // Initialize the stream
  BitstreamCursor Stream(PCHContainerRdr.ExtractPCH(**Buffer));

  // Sniff for the signature.
  if (llvm::Error Err = doesntStartWithASTFileMagic(Stream)) {
    Diags.Report(diag::err_fe_not_a_pch_file) << ASTFileName << std::move(Err);
    return std::string();
  }

  // Scan for the CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, CONTROL_BLOCK_ID)) {
    Diags.Report(diag::err_fe_pch_malformed_block) << ASTFileName;
    return std::string();
  }

  // Scan for ORIGINAL_FILE inside the control block.
  RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry =
        Stream.advanceSkippingSubblocks();
    if (!MaybeEntry) {
      // FIXME this drops errors on the floor.
      consumeError(MaybeEntry.takeError());
      return std::string();
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    if (Entry.Kind == llvm::BitstreamEntry::EndBlock)
      return std::string();

    if (Entry.Kind != llvm::BitstreamEntry::Record) {
      Diags.Report(diag::err_fe_pch_malformed_block) << ASTFileName;
      return std::string();
    }

    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecord = Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecord) {
      // FIXME this drops the errors on the floor.
      consumeError(MaybeRecord.takeError());
      return std::string();
    }
    if (ORIGINAL_FILE == MaybeRecord.get())
      return Blob.str();
  }
}

namespace {

  class SimplePCHValidator : public ASTReaderListener {
    const LangOptions &ExistingLangOpts;
    const TargetOptions &ExistingTargetOpts;
    const PreprocessorOptions &ExistingPPOpts;
    std::string ExistingModuleCachePath;
    FileManager &FileMgr;
    bool StrictOptionMatches;

  public:
    SimplePCHValidator(const LangOptions &ExistingLangOpts,
                       const TargetOptions &ExistingTargetOpts,
                       const PreprocessorOptions &ExistingPPOpts,
                       StringRef ExistingModuleCachePath, FileManager &FileMgr,
                       bool StrictOptionMatches)
        : ExistingLangOpts(ExistingLangOpts),
          ExistingTargetOpts(ExistingTargetOpts),
          ExistingPPOpts(ExistingPPOpts),
          ExistingModuleCachePath(ExistingModuleCachePath), FileMgr(FileMgr),
          StrictOptionMatches(StrictOptionMatches) {}

    bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                             bool AllowCompatibleDifferences) override {
      return checkLanguageOptions(ExistingLangOpts, LangOpts, nullptr,
                                  AllowCompatibleDifferences);
    }

    bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                           bool AllowCompatibleDifferences) override {
      return checkTargetOptions(ExistingTargetOpts, TargetOpts, nullptr,
                                AllowCompatibleDifferences);
    }

    bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                 StringRef SpecificModuleCachePath,
                                 bool Complain) override {
      return checkModuleCachePath(
          FileMgr.getVirtualFileSystem(), SpecificModuleCachePath,
          ExistingModuleCachePath, nullptr, ExistingLangOpts, ExistingPPOpts);
    }

    bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                 bool ReadMacros, bool Complain,
                                 std::string &SuggestedPredefines) override {
      return checkPreprocessorOptions(
          PPOpts, ExistingPPOpts, ReadMacros, /*Diags=*/nullptr, FileMgr,
          SuggestedPredefines, ExistingLangOpts,
          StrictOptionMatches ? OptionValidateStrictMatches
                              : OptionValidateContradictions);
    }
  };

} // namespace

bool ASTReader::readASTFileControlBlock(
    StringRef Filename, FileManager &FileMgr,
    const InMemoryModuleCache &ModuleCache,
    const PCHContainerReader &PCHContainerRdr, bool FindModuleFileExtensions,
    ASTReaderListener &Listener, bool ValidateDiagnosticOptions,
    unsigned ClientLoadCapabilities) {
  // Open the AST file.
  std::unique_ptr<llvm::MemoryBuffer> OwnedBuffer;
  llvm::MemoryBuffer *Buffer = ModuleCache.lookupPCM(Filename);
  if (!Buffer) {
    // FIXME: We should add the pcm to the InMemoryModuleCache if it could be
    // read again later, but we do not have the context here to determine if it
    // is safe to change the result of InMemoryModuleCache::getPCMState().

    // FIXME: This allows use of the VFS; we do not allow use of the
    // VFS when actually loading a module.
    auto BufferOrErr = FileMgr.getBufferForFile(Filename);
    if (!BufferOrErr)
      return true;
    OwnedBuffer = std::move(*BufferOrErr);
    Buffer = OwnedBuffer.get();
  }

  // Initialize the stream
  StringRef Bytes = PCHContainerRdr.ExtractPCH(*Buffer);
  BitstreamCursor Stream(Bytes);

  // Sniff for the signature.
  if (llvm::Error Err = doesntStartWithASTFileMagic(Stream)) {
    consumeError(std::move(Err)); // FIXME this drops errors on the floor.
    return true;
  }

  // Scan for the CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, CONTROL_BLOCK_ID))
    return true;

  bool NeedsInputFiles = Listener.needsInputFileVisitation();
  bool NeedsSystemInputFiles = Listener.needsSystemInputFileVisitation();
  bool NeedsImports = Listener.needsImportVisitation();
  BitstreamCursor InputFilesCursor;
  uint64_t InputFilesOffsetBase = 0;

  RecordData Record;
  std::string ModuleDir;
  bool DoneWithControlBlock = false;
  while (!DoneWithControlBlock) {
    Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
    if (!MaybeEntry) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeEntry.takeError());
      return true;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: {
      switch (Entry.ID) {
      case OPTIONS_BLOCK_ID: {
        std::string IgnoredSuggestedPredefines;
        if (ReadOptionsBlock(Stream, ClientLoadCapabilities,
                             /*AllowCompatibleConfigurationMismatch*/ false,
                             Listener, IgnoredSuggestedPredefines) != Success)
          return true;
        break;
      }

      case INPUT_FILES_BLOCK_ID:
        InputFilesCursor = Stream;
        if (llvm::Error Err = Stream.SkipBlock()) {
          // FIXME this drops the error on the floor.
          consumeError(std::move(Err));
          return true;
        }
        if (NeedsInputFiles &&
            ReadBlockAbbrevs(InputFilesCursor, INPUT_FILES_BLOCK_ID))
          return true;
        InputFilesOffsetBase = InputFilesCursor.GetCurrentBitNo();
        break;

      default:
        if (llvm::Error Err = Stream.SkipBlock()) {
          // FIXME this drops the error on the floor.
          consumeError(std::move(Err));
          return true;
        }
        break;
      }

      continue;
    }

    case llvm::BitstreamEntry::EndBlock:
      DoneWithControlBlock = true;
      break;

    case llvm::BitstreamEntry::Error:
      return true;

    case llvm::BitstreamEntry::Record:
      break;
    }

    if (DoneWithControlBlock) break;

    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecCode =
        Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecCode) {
      // FIXME this drops the error.
      return Failure;
    }
    switch ((ControlRecordTypes)MaybeRecCode.get()) {
    case METADATA:
      if (Record[0] != VERSION_MAJOR)
        return true;
      if (Listener.ReadFullVersionInformation(Blob))
        return true;
      break;
    case MODULE_NAME:
      Listener.ReadModuleName(Blob);
      break;
    case MODULE_DIRECTORY:
      ModuleDir = std::string(Blob);
      break;
    case MODULE_MAP_FILE: {
      unsigned Idx = 0;
      auto Path = ReadString(Record, Idx);
      ResolveImportedPath(Path, ModuleDir);
      Listener.ReadModuleMapFile(Path);
      break;
    }
    case INPUT_FILE_OFFSETS: {
      if (!NeedsInputFiles)
        break;

      unsigned NumInputFiles = Record[0];
      unsigned NumUserFiles = Record[1];
      const llvm::support::unaligned_uint64_t *InputFileOffs =
          (const llvm::support::unaligned_uint64_t *)Blob.data();
      for (unsigned I = 0; I != NumInputFiles; ++I) {
        // Go find this input file.
        bool isSystemFile = I >= NumUserFiles;

        if (isSystemFile && !NeedsSystemInputFiles)
          break; // the rest are system input files

        BitstreamCursor &Cursor = InputFilesCursor;
        SavedStreamPosition SavedPosition(Cursor);
        if (llvm::Error Err =
                Cursor.JumpToBit(InputFilesOffsetBase + InputFileOffs[I])) {
          // FIXME this drops errors on the floor.
          consumeError(std::move(Err));
        }

        Expected<unsigned> MaybeCode = Cursor.ReadCode();
        if (!MaybeCode) {
          // FIXME this drops errors on the floor.
          consumeError(MaybeCode.takeError());
        }
        unsigned Code = MaybeCode.get();

        RecordData Record;
        StringRef Blob;
        bool shouldContinue = false;
        Expected<unsigned> MaybeRecordType =
            Cursor.readRecord(Code, Record, &Blob);
        if (!MaybeRecordType) {
          // FIXME this drops errors on the floor.
          consumeError(MaybeRecordType.takeError());
        }
        switch ((InputFileRecordTypes)MaybeRecordType.get()) {
        case INPUT_FILE_HASH:
          break;
        case INPUT_FILE:
          bool Overridden = static_cast<bool>(Record[3]);
          std::string Filename = std::string(Blob);
          ResolveImportedPath(Filename, ModuleDir);
          shouldContinue = Listener.visitInputFile(
              Filename, isSystemFile, Overridden, /*IsExplicitModule*/false);
          break;
        }
        if (!shouldContinue)
          break;
      }
      break;
    }

    case IMPORTS: {
      if (!NeedsImports)
        break;

      unsigned Idx = 0, N = Record.size();
      while (Idx < N) {
        // Read information about the AST file.

        // Skip Kind
        Idx++;
        bool IsStandardCXXModule = Record[Idx++];

        // Skip ImportLoc
        Idx++;

        // In C++20 Modules, we don't record the path to imported
        // modules in the BMI files.
        if (IsStandardCXXModule) {
          std::string ModuleName = ReadString(Record, Idx);
          Listener.visitImport(ModuleName, /*Filename=*/"");
          continue;
        }

        // Skip Size, ModTime and Signature
        Idx += 1 + 1 + ASTFileSignature::size;
        std::string ModuleName = ReadString(Record, Idx);
        std::string Filename = ReadString(Record, Idx);
        ResolveImportedPath(Filename, ModuleDir);
        Listener.visitImport(ModuleName, Filename);
      }
      break;
    }

    default:
      // No other validation to perform.
      break;
    }
  }

  // Look for module file extension blocks, if requested.
  if (FindModuleFileExtensions) {
    BitstreamCursor SavedStream = Stream;
    while (!SkipCursorToBlock(Stream, EXTENSION_BLOCK_ID)) {
      bool DoneWithExtensionBlock = false;
      while (!DoneWithExtensionBlock) {
        Expected<llvm::BitstreamEntry> MaybeEntry = Stream.advance();
        if (!MaybeEntry) {
          // FIXME this drops the error.
          return true;
        }
        llvm::BitstreamEntry Entry = MaybeEntry.get();

        switch (Entry.Kind) {
        case llvm::BitstreamEntry::SubBlock:
          if (llvm::Error Err = Stream.SkipBlock()) {
            // FIXME this drops the error on the floor.
            consumeError(std::move(Err));
            return true;
          }
          continue;

        case llvm::BitstreamEntry::EndBlock:
          DoneWithExtensionBlock = true;
          continue;

        case llvm::BitstreamEntry::Error:
          return true;

        case llvm::BitstreamEntry::Record:
          break;
        }

       Record.clear();
       StringRef Blob;
       Expected<unsigned> MaybeRecCode =
           Stream.readRecord(Entry.ID, Record, &Blob);
       if (!MaybeRecCode) {
         // FIXME this drops the error.
         return true;
       }
       switch (MaybeRecCode.get()) {
       case EXTENSION_METADATA: {
         ModuleFileExtensionMetadata Metadata;
         if (parseModuleFileExtensionMetadata(Record, Blob, Metadata))
           return true;

         Listener.readModuleFileExtension(Metadata);
         break;
       }
       }
      }
    }
    Stream = SavedStream;
  }

  // Scan for the UNHASHED_CONTROL_BLOCK_ID block.
  if (readUnhashedControlBlockImpl(
          nullptr, Bytes, ClientLoadCapabilities,
          /*AllowCompatibleConfigurationMismatch*/ false, &Listener,
          ValidateDiagnosticOptions) != Success)
    return true;

  return false;
}

bool ASTReader::isAcceptableASTFile(StringRef Filename, FileManager &FileMgr,
                                    const InMemoryModuleCache &ModuleCache,
                                    const PCHContainerReader &PCHContainerRdr,
                                    const LangOptions &LangOpts,
                                    const TargetOptions &TargetOpts,
                                    const PreprocessorOptions &PPOpts,
                                    StringRef ExistingModuleCachePath,
                                    bool RequireStrictOptionMatches) {
  SimplePCHValidator validator(LangOpts, TargetOpts, PPOpts,
                               ExistingModuleCachePath, FileMgr,
                               RequireStrictOptionMatches);
  return !readASTFileControlBlock(Filename, FileMgr, ModuleCache,
                                  PCHContainerRdr,
                                  /*FindModuleFileExtensions=*/false, validator,
                                  /*ValidateDiagnosticOptions=*/true);
}

llvm::Error ASTReader::ReadSubmoduleBlock(ModuleFile &F,
                                          unsigned ClientLoadCapabilities) {
  // Enter the submodule block.
  if (llvm::Error Err = F.Stream.EnterSubBlock(SUBMODULE_BLOCK_ID))
    return Err;

  ModuleMap &ModMap = PP.getHeaderSearchInfo().getModuleMap();
  bool First = true;
  Module *CurrentModule = nullptr;
  RecordData Record;
  while (true) {
    Expected<llvm::BitstreamEntry> MaybeEntry =
        F.Stream.advanceSkippingSubblocks();
    if (!MaybeEntry)
      return MaybeEntry.takeError();
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      return llvm::createStringError(std::errc::illegal_byte_sequence,
                                     "malformed block record in AST file");
    case llvm::BitstreamEntry::EndBlock:
      return llvm::Error::success();
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    StringRef Blob;
    Record.clear();
    Expected<unsigned> MaybeKind = F.Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeKind)
      return MaybeKind.takeError();
    unsigned Kind = MaybeKind.get();

    if ((Kind == SUBMODULE_METADATA) != First)
      return llvm::createStringError(
          std::errc::illegal_byte_sequence,
          "submodule metadata record should be at beginning of block");
    First = false;

    // Submodule information is only valid if we have a current module.
    // FIXME: Should we error on these cases?
    if (!CurrentModule && Kind != SUBMODULE_METADATA &&
        Kind != SUBMODULE_DEFINITION)
      continue;

    switch (Kind) {
    default:  // Default behavior: ignore.
      break;

    case SUBMODULE_DEFINITION: {
      if (Record.size() < 13)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "malformed module definition");

      StringRef Name = Blob;
      unsigned Idx = 0;
      SubmoduleID GlobalID = getGlobalSubmoduleID(F, Record[Idx++]);
      SubmoduleID Parent = getGlobalSubmoduleID(F, Record[Idx++]);
      Module::ModuleKind Kind = (Module::ModuleKind)Record[Idx++];
      SourceLocation DefinitionLoc = ReadSourceLocation(F, Record[Idx++]);
      bool IsFramework = Record[Idx++];
      bool IsExplicit = Record[Idx++];
      bool IsSystem = Record[Idx++];
      bool IsExternC = Record[Idx++];
      bool InferSubmodules = Record[Idx++];
      bool InferExplicitSubmodules = Record[Idx++];
      bool InferExportWildcard = Record[Idx++];
      bool ConfigMacrosExhaustive = Record[Idx++];
      bool ModuleMapIsPrivate = Record[Idx++];
      bool NamedModuleHasInit = Record[Idx++];

      Module *ParentModule = nullptr;
      if (Parent)
        ParentModule = getSubmodule(Parent);

      // Retrieve this (sub)module from the module map, creating it if
      // necessary.
      CurrentModule =
          ModMap.findOrCreateModule(Name, ParentModule, IsFramework, IsExplicit)
              .first;

      // FIXME: Call ModMap.setInferredModuleAllowedBy()

      SubmoduleID GlobalIndex = GlobalID - NUM_PREDEF_SUBMODULE_IDS;
      if (GlobalIndex >= SubmodulesLoaded.size() ||
          SubmodulesLoaded[GlobalIndex])
        return llvm::createStringError(std::errc::invalid_argument,
                                       "too many submodules");

      if (!ParentModule) {
        if (OptionalFileEntryRef CurFile = CurrentModule->getASTFile()) {
          // Don't emit module relocation error if we have -fno-validate-pch
          if (!bool(PP.getPreprocessorOpts().DisablePCHOrModuleValidation &
                    DisableValidationForModuleKind::Module) &&
              CurFile != F.File) {
            auto ConflictError =
                PartialDiagnostic(diag::err_module_file_conflict,
                                  ContextObj->DiagAllocator)
                << CurrentModule->getTopLevelModuleName() << CurFile->getName()
                << F.File.getName();
            return DiagnosticError::create(CurrentImportLoc, ConflictError);
          }
        }

        F.DidReadTopLevelSubmodule = true;
        CurrentModule->setASTFile(F.File);
        CurrentModule->PresumedModuleMapFile = F.ModuleMapPath;
      }

      CurrentModule->Kind = Kind;
      CurrentModule->DefinitionLoc = DefinitionLoc;
      CurrentModule->Signature = F.Signature;
      CurrentModule->IsFromModuleFile = true;
      CurrentModule->IsSystem = IsSystem || CurrentModule->IsSystem;
      CurrentModule->IsExternC = IsExternC;
      CurrentModule->InferSubmodules = InferSubmodules;
      CurrentModule->InferExplicitSubmodules = InferExplicitSubmodules;
      CurrentModule->InferExportWildcard = InferExportWildcard;
      CurrentModule->ConfigMacrosExhaustive = ConfigMacrosExhaustive;
      CurrentModule->ModuleMapIsPrivate = ModuleMapIsPrivate;
      CurrentModule->NamedModuleHasInit = NamedModuleHasInit;
      if (DeserializationListener)
        DeserializationListener->ModuleRead(GlobalID, CurrentModule);

      SubmodulesLoaded[GlobalIndex] = CurrentModule;

      // Clear out data that will be replaced by what is in the module file.
      CurrentModule->LinkLibraries.clear();
      CurrentModule->ConfigMacros.clear();
      CurrentModule->UnresolvedConflicts.clear();
      CurrentModule->Conflicts.clear();

      // The module is available unless it's missing a requirement; relevant
      // requirements will be (re-)added by SUBMODULE_REQUIRES records.
      // Missing headers that were present when the module was built do not
      // make it unavailable -- if we got this far, this must be an explicitly
      // imported module file.
      CurrentModule->Requirements.clear();
      CurrentModule->MissingHeaders.clear();
      CurrentModule->IsUnimportable =
          ParentModule && ParentModule->IsUnimportable;
      CurrentModule->IsAvailable = !CurrentModule->IsUnimportable;
      break;
    }

    case SUBMODULE_UMBRELLA_HEADER: {
      // FIXME: This doesn't work for framework modules as `Filename` is the
      //        name as written in the module file and does not include
      //        `Headers/`, so this path will never exist.
      std::string Filename = std::string(Blob);
      ResolveImportedPath(F, Filename);
      if (auto Umbrella = PP.getFileManager().getOptionalFileRef(Filename)) {
        if (!CurrentModule->getUmbrellaHeaderAsWritten()) {
          // FIXME: NameAsWritten
          ModMap.setUmbrellaHeaderAsWritten(CurrentModule, *Umbrella, Blob, "");
        }
        // Note that it's too late at this point to return out of date if the
        // name from the PCM doesn't match up with the one in the module map,
        // but also quite unlikely since we will have already checked the
        // modification time and size of the module map file itself.
      }
      break;
    }

    case SUBMODULE_HEADER:
    case SUBMODULE_EXCLUDED_HEADER:
    case SUBMODULE_PRIVATE_HEADER:
      // We lazily associate headers with their modules via the HeaderInfo table.
      // FIXME: Re-evaluate this section; maybe only store InputFile IDs instead
      // of complete filenames or remove it entirely.
      break;

    case SUBMODULE_TEXTUAL_HEADER:
    case SUBMODULE_PRIVATE_TEXTUAL_HEADER:
      // FIXME: Textual headers are not marked in the HeaderInfo table. Load
      // them here.
      break;

    case SUBMODULE_TOPHEADER: {
      std::string HeaderName(Blob);
      ResolveImportedPath(F, HeaderName);
      CurrentModule->addTopHeaderFilename(HeaderName);
      break;
    }

    case SUBMODULE_UMBRELLA_DIR: {
      // See comments in SUBMODULE_UMBRELLA_HEADER
      std::string Dirname = std::string(Blob);
      ResolveImportedPath(F, Dirname);
      if (auto Umbrella =
              PP.getFileManager().getOptionalDirectoryRef(Dirname)) {
        if (!CurrentModule->getUmbrellaDirAsWritten()) {
          // FIXME: NameAsWritten
          ModMap.setUmbrellaDirAsWritten(CurrentModule, *Umbrella, Blob, "");
        }
      }
      break;
    }

    case SUBMODULE_METADATA: {
      F.BaseSubmoduleID = getTotalNumSubmodules();
      F.LocalNumSubmodules = Record[0];
      unsigned LocalBaseSubmoduleID = Record[1];
      if (F.LocalNumSubmodules > 0) {
        // Introduce the global -> local mapping for submodules within this
        // module.
        GlobalSubmoduleMap.insert(std::make_pair(getTotalNumSubmodules()+1,&F));

        // Introduce the local -> global mapping for submodules within this
        // module.
        F.SubmoduleRemap.insertOrReplace(
          std::make_pair(LocalBaseSubmoduleID,
                         F.BaseSubmoduleID - LocalBaseSubmoduleID));

        SubmodulesLoaded.resize(SubmodulesLoaded.size() + F.LocalNumSubmodules);
      }
      break;
    }

    case SUBMODULE_IMPORTS:
      for (unsigned Idx = 0; Idx != Record.size(); ++Idx) {
        UnresolvedModuleRef Unresolved;
        Unresolved.File = &F;
        Unresolved.Mod = CurrentModule;
        Unresolved.ID = Record[Idx];
        Unresolved.Kind = UnresolvedModuleRef::Import;
        Unresolved.IsWildcard = false;
        UnresolvedModuleRefs.push_back(Unresolved);
      }
      break;

    case SUBMODULE_AFFECTING_MODULES:
      for (unsigned Idx = 0; Idx != Record.size(); ++Idx) {
        UnresolvedModuleRef Unresolved;
        Unresolved.File = &F;
        Unresolved.Mod = CurrentModule;
        Unresolved.ID = Record[Idx];
        Unresolved.Kind = UnresolvedModuleRef::Affecting;
        Unresolved.IsWildcard = false;
        UnresolvedModuleRefs.push_back(Unresolved);
      }
      break;

    case SUBMODULE_EXPORTS:
      for (unsigned Idx = 0; Idx + 1 < Record.size(); Idx += 2) {
        UnresolvedModuleRef Unresolved;
        Unresolved.File = &F;
        Unresolved.Mod = CurrentModule;
        Unresolved.ID = Record[Idx];
        Unresolved.Kind = UnresolvedModuleRef::Export;
        Unresolved.IsWildcard = Record[Idx + 1];
        UnresolvedModuleRefs.push_back(Unresolved);
      }

      // Once we've loaded the set of exports, there's no reason to keep
      // the parsed, unresolved exports around.
      CurrentModule->UnresolvedExports.clear();
      break;

    case SUBMODULE_REQUIRES:
      CurrentModule->addRequirement(Blob, Record[0], PP.getLangOpts(),
                                    PP.getTargetInfo());
      break;

    case SUBMODULE_LINK_LIBRARY:
      ModMap.resolveLinkAsDependencies(CurrentModule);
      CurrentModule->LinkLibraries.push_back(
          Module::LinkLibrary(std::string(Blob), Record[0]));
      break;

    case SUBMODULE_CONFIG_MACRO:
      CurrentModule->ConfigMacros.push_back(Blob.str());
      break;

    case SUBMODULE_CONFLICT: {
      UnresolvedModuleRef Unresolved;
      Unresolved.File = &F;
      Unresolved.Mod = CurrentModule;
      Unresolved.ID = Record[0];
      Unresolved.Kind = UnresolvedModuleRef::Conflict;
      Unresolved.IsWildcard = false;
      Unresolved.String = Blob;
      UnresolvedModuleRefs.push_back(Unresolved);
      break;
    }

    case SUBMODULE_INITIALIZERS: {
      if (!ContextObj)
        break;
      SmallVector<GlobalDeclID, 16> Inits;
      for (unsigned I = 0; I < Record.size(); /*in loop*/)
        Inits.push_back(ReadDeclID(F, Record, I));
      ContextObj->addLazyModuleInitializers(CurrentModule, Inits);
      break;
    }

    case SUBMODULE_EXPORT_AS:
      CurrentModule->ExportAsModule = Blob.str();
      ModMap.addLinkAsDependency(CurrentModule);
      break;
    }
  }
}

/// Parse the record that corresponds to a LangOptions data
/// structure.
///
/// This routine parses the language options from the AST file and then gives
/// them to the AST listener if one is set.
///
/// \returns true if the listener deems the file unacceptable, false otherwise.
bool ASTReader::ParseLanguageOptions(const RecordData &Record,
                                     bool Complain,
                                     ASTReaderListener &Listener,
                                     bool AllowCompatibleDifferences) {
  LangOptions LangOpts;
  unsigned Idx = 0;
#define LANGOPT(Name, Bits, Default, Description) \
  LangOpts.Name = Record[Idx++];
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
  LangOpts.set##Name(static_cast<LangOptions::Type>(Record[Idx++]));
#include "clang/Basic/LangOptions.def"
#define SANITIZER(NAME, ID)                                                    \
  LangOpts.Sanitize.set(SanitizerKind::ID, Record[Idx++]);
#include "clang/Basic/Sanitizers.def"

  for (unsigned N = Record[Idx++]; N; --N)
    LangOpts.ModuleFeatures.push_back(ReadString(Record, Idx));

  ObjCRuntime::Kind runtimeKind = (ObjCRuntime::Kind) Record[Idx++];
  VersionTuple runtimeVersion = ReadVersionTuple(Record, Idx);
  LangOpts.ObjCRuntime = ObjCRuntime(runtimeKind, runtimeVersion);

  LangOpts.CurrentModule = ReadString(Record, Idx);

  // Comment options.
  for (unsigned N = Record[Idx++]; N; --N) {
    LangOpts.CommentOpts.BlockCommandNames.push_back(
      ReadString(Record, Idx));
  }
  LangOpts.CommentOpts.ParseAllComments = Record[Idx++];

  // OpenMP offloading options.
  for (unsigned N = Record[Idx++]; N; --N) {
    LangOpts.OMPTargetTriples.push_back(llvm::Triple(ReadString(Record, Idx)));
  }

  LangOpts.OMPHostIRFile = ReadString(Record, Idx);

  return Listener.ReadLanguageOptions(LangOpts, Complain,
                                      AllowCompatibleDifferences);
}

bool ASTReader::ParseTargetOptions(const RecordData &Record, bool Complain,
                                   ASTReaderListener &Listener,
                                   bool AllowCompatibleDifferences) {
  unsigned Idx = 0;
  TargetOptions TargetOpts;
  TargetOpts.Triple = ReadString(Record, Idx);
  TargetOpts.CPU = ReadString(Record, Idx);
  TargetOpts.TuneCPU = ReadString(Record, Idx);
  TargetOpts.ABI = ReadString(Record, Idx);
  for (unsigned N = Record[Idx++]; N; --N) {
    TargetOpts.FeaturesAsWritten.push_back(ReadString(Record, Idx));
  }
  for (unsigned N = Record[Idx++]; N; --N) {
    TargetOpts.Features.push_back(ReadString(Record, Idx));
  }

  return Listener.ReadTargetOptions(TargetOpts, Complain,
                                    AllowCompatibleDifferences);
}

bool ASTReader::ParseDiagnosticOptions(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener) {
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts(new DiagnosticOptions);
  unsigned Idx = 0;
#define DIAGOPT(Name, Bits, Default) DiagOpts->Name = Record[Idx++];
#define ENUM_DIAGOPT(Name, Type, Bits, Default) \
  DiagOpts->set##Name(static_cast<Type>(Record[Idx++]));
#include "clang/Basic/DiagnosticOptions.def"

  for (unsigned N = Record[Idx++]; N; --N)
    DiagOpts->Warnings.push_back(ReadString(Record, Idx));
  for (unsigned N = Record[Idx++]; N; --N)
    DiagOpts->Remarks.push_back(ReadString(Record, Idx));

  return Listener.ReadDiagnosticOptions(DiagOpts, Complain);
}

bool ASTReader::ParseFileSystemOptions(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener) {
  FileSystemOptions FSOpts;
  unsigned Idx = 0;
  FSOpts.WorkingDir = ReadString(Record, Idx);
  return Listener.ReadFileSystemOptions(FSOpts, Complain);
}

bool ASTReader::ParseHeaderSearchOptions(const RecordData &Record,
                                         bool Complain,
                                         ASTReaderListener &Listener) {
  HeaderSearchOptions HSOpts;
  unsigned Idx = 0;
  HSOpts.Sysroot = ReadString(Record, Idx);

  HSOpts.ResourceDir = ReadString(Record, Idx);
  HSOpts.ModuleCachePath = ReadString(Record, Idx);
  HSOpts.ModuleUserBuildPath = ReadString(Record, Idx);
  HSOpts.DisableModuleHash = Record[Idx++];
  HSOpts.ImplicitModuleMaps = Record[Idx++];
  HSOpts.ModuleMapFileHomeIsCwd = Record[Idx++];
  HSOpts.EnablePrebuiltImplicitModules = Record[Idx++];
  HSOpts.UseBuiltinIncludes = Record[Idx++];
  HSOpts.UseStandardSystemIncludes = Record[Idx++];
  HSOpts.UseStandardCXXIncludes = Record[Idx++];
  HSOpts.UseLibcxx = Record[Idx++];
  std::string SpecificModuleCachePath = ReadString(Record, Idx);

  return Listener.ReadHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                          Complain);
}

bool ASTReader::ParseHeaderSearchPaths(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener) {
  HeaderSearchOptions HSOpts;
  unsigned Idx = 0;

  // Include entries.
  for (unsigned N = Record[Idx++]; N; --N) {
    std::string Path = ReadString(Record, Idx);
    frontend::IncludeDirGroup Group
      = static_cast<frontend::IncludeDirGroup>(Record[Idx++]);
    bool IsFramework = Record[Idx++];
    bool IgnoreSysRoot = Record[Idx++];
    HSOpts.UserEntries.emplace_back(std::move(Path), Group, IsFramework,
                                    IgnoreSysRoot);
  }

  // System header prefixes.
  for (unsigned N = Record[Idx++]; N; --N) {
    std::string Prefix = ReadString(Record, Idx);
    bool IsSystemHeader = Record[Idx++];
    HSOpts.SystemHeaderPrefixes.emplace_back(std::move(Prefix), IsSystemHeader);
  }

  // VFS overlay files.
  for (unsigned N = Record[Idx++]; N; --N) {
    std::string VFSOverlayFile = ReadString(Record, Idx);
    HSOpts.VFSOverlayFiles.emplace_back(std::move(VFSOverlayFile));
  }

  return Listener.ReadHeaderSearchPaths(HSOpts, Complain);
}

bool ASTReader::ParsePreprocessorOptions(const RecordData &Record,
                                         bool Complain,
                                         ASTReaderListener &Listener,
                                         std::string &SuggestedPredefines) {
  PreprocessorOptions PPOpts;
  unsigned Idx = 0;

  // Macro definitions/undefs
  bool ReadMacros = Record[Idx++];
  if (ReadMacros) {
    for (unsigned N = Record[Idx++]; N; --N) {
      std::string Macro = ReadString(Record, Idx);
      bool IsUndef = Record[Idx++];
      PPOpts.Macros.push_back(std::make_pair(Macro, IsUndef));
    }
  }

  // Includes
  for (unsigned N = Record[Idx++]; N; --N) {
    PPOpts.Includes.push_back(ReadString(Record, Idx));
  }

  // Macro Includes
  for (unsigned N = Record[Idx++]; N; --N) {
    PPOpts.MacroIncludes.push_back(ReadString(Record, Idx));
  }

  PPOpts.UsePredefines = Record[Idx++];
  PPOpts.DetailedRecord = Record[Idx++];
  PPOpts.ImplicitPCHInclude = ReadString(Record, Idx);
  PPOpts.ObjCXXARCStandardLibrary =
    static_cast<ObjCXXARCStandardLibraryKind>(Record[Idx++]);
  SuggestedPredefines.clear();
  return Listener.ReadPreprocessorOptions(PPOpts, ReadMacros, Complain,
                                          SuggestedPredefines);
}

std::pair<ModuleFile *, unsigned>
ASTReader::getModulePreprocessedEntity(unsigned GlobalIndex) {
  GlobalPreprocessedEntityMapType::iterator
  I = GlobalPreprocessedEntityMap.find(GlobalIndex);
  assert(I != GlobalPreprocessedEntityMap.end() &&
         "Corrupted global preprocessed entity map");
  ModuleFile *M = I->second;
  unsigned LocalIndex = GlobalIndex - M->BasePreprocessedEntityID;
  return std::make_pair(M, LocalIndex);
}

llvm::iterator_range<PreprocessingRecord::iterator>
ASTReader::getModulePreprocessedEntities(ModuleFile &Mod) const {
  if (PreprocessingRecord *PPRec = PP.getPreprocessingRecord())
    return PPRec->getIteratorsForLoadedRange(Mod.BasePreprocessedEntityID,
                                             Mod.NumPreprocessedEntities);

  return llvm::make_range(PreprocessingRecord::iterator(),
                          PreprocessingRecord::iterator());
}

bool ASTReader::canRecoverFromOutOfDate(StringRef ModuleFileName,
                                        unsigned int ClientLoadCapabilities) {
  return ClientLoadCapabilities & ARR_OutOfDate &&
         !getModuleManager().getModuleCache().isPCMFinal(ModuleFileName);
}

llvm::iterator_range<ASTReader::ModuleDeclIterator>
ASTReader::getModuleFileLevelDecls(ModuleFile &Mod) {
  return llvm::make_range(
      ModuleDeclIterator(this, &Mod, Mod.FileSortedDecls),
      ModuleDeclIterator(this, &Mod,
                         Mod.FileSortedDecls + Mod.NumFileSortedDecls));
}

SourceRange ASTReader::ReadSkippedRange(unsigned GlobalIndex) {
  auto I = GlobalSkippedRangeMap.find(GlobalIndex);
  assert(I != GlobalSkippedRangeMap.end() &&
    "Corrupted global skipped range map");
  ModuleFile *M = I->second;
  unsigned LocalIndex = GlobalIndex - M->BasePreprocessedSkippedRangeID;
  assert(LocalIndex < M->NumPreprocessedSkippedRanges);
  PPSkippedRange RawRange = M->PreprocessedSkippedRangeOffsets[LocalIndex];
  SourceRange Range(ReadSourceLocation(*M, RawRange.getBegin()),
                    ReadSourceLocation(*M, RawRange.getEnd()));
  assert(Range.isValid());
  return Range;
}

PreprocessedEntity *ASTReader::ReadPreprocessedEntity(unsigned Index) {
  PreprocessedEntityID PPID = Index+1;
  std::pair<ModuleFile *, unsigned> PPInfo = getModulePreprocessedEntity(Index);
  ModuleFile &M = *PPInfo.first;
  unsigned LocalIndex = PPInfo.second;
  const PPEntityOffset &PPOffs = M.PreprocessedEntityOffsets[LocalIndex];

  if (!PP.getPreprocessingRecord()) {
    Error("no preprocessing record");
    return nullptr;
  }

  SavedStreamPosition SavedPosition(M.PreprocessorDetailCursor);
  if (llvm::Error Err = M.PreprocessorDetailCursor.JumpToBit(
          M.MacroOffsetsBase + PPOffs.getOffset())) {
    Error(std::move(Err));
    return nullptr;
  }

  Expected<llvm::BitstreamEntry> MaybeEntry =
      M.PreprocessorDetailCursor.advance(BitstreamCursor::AF_DontPopBlockAtEnd);
  if (!MaybeEntry) {
    Error(MaybeEntry.takeError());
    return nullptr;
  }
  llvm::BitstreamEntry Entry = MaybeEntry.get();

  if (Entry.Kind != llvm::BitstreamEntry::Record)
    return nullptr;

  // Read the record.
  SourceRange Range(ReadSourceLocation(M, PPOffs.getBegin()),
                    ReadSourceLocation(M, PPOffs.getEnd()));
  PreprocessingRecord &PPRec = *PP.getPreprocessingRecord();
  StringRef Blob;
  RecordData Record;
  Expected<unsigned> MaybeRecType =
      M.PreprocessorDetailCursor.readRecord(Entry.ID, Record, &Blob);
  if (!MaybeRecType) {
    Error(MaybeRecType.takeError());
    return nullptr;
  }
  switch ((PreprocessorDetailRecordTypes)MaybeRecType.get()) {
  case PPD_MACRO_EXPANSION: {
    bool isBuiltin = Record[0];
    IdentifierInfo *Name = nullptr;
    MacroDefinitionRecord *Def = nullptr;
    if (isBuiltin)
      Name = getLocalIdentifier(M, Record[1]);
    else {
      PreprocessedEntityID GlobalID =
          getGlobalPreprocessedEntityID(M, Record[1]);
      Def = cast<MacroDefinitionRecord>(
          PPRec.getLoadedPreprocessedEntity(GlobalID - 1));
    }

    MacroExpansion *ME;
    if (isBuiltin)
      ME = new (PPRec) MacroExpansion(Name, Range);
    else
      ME = new (PPRec) MacroExpansion(Def, Range);

    return ME;
  }

  case PPD_MACRO_DEFINITION: {
    // Decode the identifier info and then check again; if the macro is
    // still defined and associated with the identifier,
    IdentifierInfo *II = getLocalIdentifier(M, Record[0]);
    MacroDefinitionRecord *MD = new (PPRec) MacroDefinitionRecord(II, Range);

    if (DeserializationListener)
      DeserializationListener->MacroDefinitionRead(PPID, MD);

    return MD;
  }

  case PPD_INCLUSION_DIRECTIVE: {
    const char *FullFileNameStart = Blob.data() + Record[0];
    StringRef FullFileName(FullFileNameStart, Blob.size() - Record[0]);
    OptionalFileEntryRef File;
    if (!FullFileName.empty())
      File = PP.getFileManager().getOptionalFileRef(FullFileName);

    // FIXME: Stable encoding
    InclusionDirective::InclusionKind Kind
      = static_cast<InclusionDirective::InclusionKind>(Record[2]);
    InclusionDirective *ID
      = new (PPRec) InclusionDirective(PPRec, Kind,
                                       StringRef(Blob.data(), Record[0]),
                                       Record[1], Record[3],
                                       File,
                                       Range);
    return ID;
  }
  }

  llvm_unreachable("Invalid PreprocessorDetailRecordTypes");
}

/// Find the next module that contains entities and return the ID
/// of the first entry.
///
/// \param SLocMapI points at a chunk of a module that contains no
/// preprocessed entities or the entities it contains are not the ones we are
/// looking for.
PreprocessedEntityID ASTReader::findNextPreprocessedEntity(
                       GlobalSLocOffsetMapType::const_iterator SLocMapI) const {
  ++SLocMapI;
  for (GlobalSLocOffsetMapType::const_iterator
         EndI = GlobalSLocOffsetMap.end(); SLocMapI != EndI; ++SLocMapI) {
    ModuleFile &M = *SLocMapI->second;
    if (M.NumPreprocessedEntities)
      return M.BasePreprocessedEntityID;
  }

  return getTotalNumPreprocessedEntities();
}

namespace {

struct PPEntityComp {
  const ASTReader &Reader;
  ModuleFile &M;

  PPEntityComp(const ASTReader &Reader, ModuleFile &M) : Reader(Reader), M(M) {}

  bool operator()(const PPEntityOffset &L, const PPEntityOffset &R) const {
    SourceLocation LHS = getLoc(L);
    SourceLocation RHS = getLoc(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(const PPEntityOffset &L, SourceLocation RHS) const {
    SourceLocation LHS = getLoc(L);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(SourceLocation LHS, const PPEntityOffset &R) const {
    SourceLocation RHS = getLoc(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  SourceLocation getLoc(const PPEntityOffset &PPE) const {
    return Reader.ReadSourceLocation(M, PPE.getBegin());
  }
};

} // namespace

PreprocessedEntityID ASTReader::findPreprocessedEntity(SourceLocation Loc,
                                                       bool EndsAfter) const {
  if (SourceMgr.isLocalSourceLocation(Loc))
    return getTotalNumPreprocessedEntities();

  GlobalSLocOffsetMapType::const_iterator SLocMapI = GlobalSLocOffsetMap.find(
      SourceManager::MaxLoadedOffset - Loc.getOffset() - 1);
  assert(SLocMapI != GlobalSLocOffsetMap.end() &&
         "Corrupted global sloc offset map");

  if (SLocMapI->second->NumPreprocessedEntities == 0)
    return findNextPreprocessedEntity(SLocMapI);

  ModuleFile &M = *SLocMapI->second;

  using pp_iterator = const PPEntityOffset *;

  pp_iterator pp_begin = M.PreprocessedEntityOffsets;
  pp_iterator pp_end = pp_begin + M.NumPreprocessedEntities;

  size_t Count = M.NumPreprocessedEntities;
  size_t Half;
  pp_iterator First = pp_begin;
  pp_iterator PPI;

  if (EndsAfter) {
    PPI = std::upper_bound(pp_begin, pp_end, Loc,
                           PPEntityComp(*this, M));
  } else {
    // Do a binary search manually instead of using std::lower_bound because
    // The end locations of entities may be unordered (when a macro expansion
    // is inside another macro argument), but for this case it is not important
    // whether we get the first macro expansion or its containing macro.
    while (Count > 0) {
      Half = Count / 2;
      PPI = First;
      std::advance(PPI, Half);
      if (SourceMgr.isBeforeInTranslationUnit(
              ReadSourceLocation(M, PPI->getEnd()), Loc)) {
        First = PPI;
        ++First;
        Count = Count - Half - 1;
      } else
        Count = Half;
    }
  }

  if (PPI == pp_end)
    return findNextPreprocessedEntity(SLocMapI);

  return M.BasePreprocessedEntityID + (PPI - pp_begin);
}

/// Returns a pair of [Begin, End) indices of preallocated
/// preprocessed entities that \arg Range encompasses.
std::pair<unsigned, unsigned>
    ASTReader::findPreprocessedEntitiesInRange(SourceRange Range) {
  if (Range.isInvalid())
    return std::make_pair(0,0);
  assert(!SourceMgr.isBeforeInTranslationUnit(Range.getEnd(),Range.getBegin()));

  PreprocessedEntityID BeginID =
      findPreprocessedEntity(Range.getBegin(), false);
  PreprocessedEntityID EndID = findPreprocessedEntity(Range.getEnd(), true);
  return std::make_pair(BeginID, EndID);
}

/// Optionally returns true or false if the preallocated preprocessed
/// entity with index \arg Index came from file \arg FID.
std::optional<bool> ASTReader::isPreprocessedEntityInFileID(unsigned Index,
                                                            FileID FID) {
  if (FID.isInvalid())
    return false;

  std::pair<ModuleFile *, unsigned> PPInfo = getModulePreprocessedEntity(Index);
  ModuleFile &M = *PPInfo.first;
  unsigned LocalIndex = PPInfo.second;
  const PPEntityOffset &PPOffs = M.PreprocessedEntityOffsets[LocalIndex];

  SourceLocation Loc = ReadSourceLocation(M, PPOffs.getBegin());
  if (Loc.isInvalid())
    return false;

  if (SourceMgr.isInFileID(SourceMgr.getFileLoc(Loc), FID))
    return true;
  else
    return false;
}

namespace {

  /// Visitor used to search for information about a header file.
  class HeaderFileInfoVisitor {
  FileEntryRef FE;
    std::optional<HeaderFileInfo> HFI;

  public:
    explicit HeaderFileInfoVisitor(FileEntryRef FE) : FE(FE) {}

    bool operator()(ModuleFile &M) {
      HeaderFileInfoLookupTable *Table
        = static_cast<HeaderFileInfoLookupTable *>(M.HeaderFileInfoTable);
      if (!Table)
        return false;

      // Look in the on-disk hash table for an entry for this file name.
      HeaderFileInfoLookupTable::iterator Pos = Table->find(FE);
      if (Pos == Table->end())
        return false;

      HFI = *Pos;
      return true;
    }

    std::optional<HeaderFileInfo> getHeaderFileInfo() const { return HFI; }
  };

} // namespace

HeaderFileInfo ASTReader::GetHeaderFileInfo(FileEntryRef FE) {
  HeaderFileInfoVisitor Visitor(FE);
  ModuleMgr.visit(Visitor);
  if (std::optional<HeaderFileInfo> HFI = Visitor.getHeaderFileInfo())
      return *HFI;

  return HeaderFileInfo();
}

void ASTReader::ReadPragmaDiagnosticMappings(DiagnosticsEngine &Diag) {
  using DiagState = DiagnosticsEngine::DiagState;
  SmallVector<DiagState *, 32> DiagStates;

  for (ModuleFile &F : ModuleMgr) {
    unsigned Idx = 0;
    auto &Record = F.PragmaDiagMappings;
    if (Record.empty())
      continue;

    DiagStates.clear();

    auto ReadDiagState = [&](const DiagState &BasedOn,
                             bool IncludeNonPragmaStates) {
      unsigned BackrefID = Record[Idx++];
      if (BackrefID != 0)
        return DiagStates[BackrefID - 1];

      // A new DiagState was created here.
      Diag.DiagStates.push_back(BasedOn);
      DiagState *NewState = &Diag.DiagStates.back();
      DiagStates.push_back(NewState);
      unsigned Size = Record[Idx++];
      assert(Idx + Size * 2 <= Record.size() &&
             "Invalid data, not enough diag/map pairs");
      while (Size--) {
        unsigned DiagID = Record[Idx++];
        DiagnosticMapping NewMapping =
            DiagnosticMapping::deserialize(Record[Idx++]);
        if (!NewMapping.isPragma() && !IncludeNonPragmaStates)
          continue;

        DiagnosticMapping &Mapping = NewState->getOrAddMapping(DiagID);

        // If this mapping was specified as a warning but the severity was
        // upgraded due to diagnostic settings, simulate the current diagnostic
        // settings (and use a warning).
        if (NewMapping.wasUpgradedFromWarning() && !Mapping.isErrorOrFatal()) {
          NewMapping.setSeverity(diag::Severity::Warning);
          NewMapping.setUpgradedFromWarning(false);
        }

        Mapping = NewMapping;
      }
      return NewState;
    };

    // Read the first state.
    DiagState *FirstState;
    if (F.Kind == MK_ImplicitModule) {
      // Implicitly-built modules are reused with different diagnostic
      // settings.  Use the initial diagnostic state from Diag to simulate this
      // compilation's diagnostic settings.
      FirstState = Diag.DiagStatesByLoc.FirstDiagState;
      DiagStates.push_back(FirstState);

      // Skip the initial diagnostic state from the serialized module.
      assert(Record[1] == 0 &&
             "Invalid data, unexpected backref in initial state");
      Idx = 3 + Record[2] * 2;
      assert(Idx < Record.size() &&
             "Invalid data, not enough state change pairs in initial state");
    } else if (F.isModule()) {
      // For an explicit module, preserve the flags from the module build
      // command line (-w, -Weverything, -Werror, ...) along with any explicit
      // -Wblah flags.
      unsigned Flags = Record[Idx++];
      DiagState Initial;
      Initial.SuppressSystemWarnings = Flags & 1; Flags >>= 1;
      Initial.ErrorsAsFatal = Flags & 1; Flags >>= 1;
      Initial.WarningsAsErrors = Flags & 1; Flags >>= 1;
      Initial.EnableAllWarnings = Flags & 1; Flags >>= 1;
      Initial.IgnoreAllWarnings = Flags & 1; Flags >>= 1;
      Initial.ExtBehavior = (diag::Severity)Flags;
      FirstState = ReadDiagState(Initial, true);

      assert(F.OriginalSourceFileID.isValid());

      // Set up the root buffer of the module to start with the initial
      // diagnostic state of the module itself, to cover files that contain no
      // explicit transitions (for which we did not serialize anything).
      Diag.DiagStatesByLoc.Files[F.OriginalSourceFileID]
          .StateTransitions.push_back({FirstState, 0});
    } else {
      // For prefix ASTs, start with whatever the user configured on the
      // command line.
      Idx++; // Skip flags.
      FirstState = ReadDiagState(*Diag.DiagStatesByLoc.CurDiagState, false);
    }

    // Read the state transitions.
    unsigned NumLocations = Record[Idx++];
    while (NumLocations--) {
      assert(Idx < Record.size() &&
             "Invalid data, missing pragma diagnostic states");
      FileID FID = ReadFileID(F, Record, Idx);
      assert(FID.isValid() && "invalid FileID for transition");
      unsigned Transitions = Record[Idx++];

      // Note that we don't need to set up Parent/ParentOffset here, because
      // we won't be changing the diagnostic state within imported FileIDs
      // (other than perhaps appending to the main source file, which has no
      // parent).
      auto &F = Diag.DiagStatesByLoc.Files[FID];
      F.StateTransitions.reserve(F.StateTransitions.size() + Transitions);
      for (unsigned I = 0; I != Transitions; ++I) {
        unsigned Offset = Record[Idx++];
        auto *State = ReadDiagState(*FirstState, false);
        F.StateTransitions.push_back({State, Offset});
      }
    }

    // Read the final state.
    assert(Idx < Record.size() &&
           "Invalid data, missing final pragma diagnostic state");
    SourceLocation CurStateLoc = ReadSourceLocation(F, Record[Idx++]);
    auto *CurState = ReadDiagState(*FirstState, false);

    if (!F.isModule()) {
      Diag.DiagStatesByLoc.CurDiagState = CurState;
      Diag.DiagStatesByLoc.CurDiagStateLoc = CurStateLoc;

      // Preserve the property that the imaginary root file describes the
      // current state.
      FileID NullFile;
      auto &T = Diag.DiagStatesByLoc.Files[NullFile].StateTransitions;
      if (T.empty())
        T.push_back({CurState, 0});
      else
        T[0].State = CurState;
    }

    // Don't try to read these mappings again.
    Record.clear();
  }
}

/// Get the correct cursor and offset for loading a type.
ASTReader::RecordLocation ASTReader::TypeCursorForIndex(TypeID ID) {
  auto [M, Index] = translateTypeIDToIndex(ID);
  return RecordLocation(M, M->TypeOffsets[Index - M->BaseTypeIndex].get() +
                               M->DeclsBlockStartOffset);
}

static std::optional<Type::TypeClass> getTypeClassForCode(TypeCode code) {
  switch (code) {
#define TYPE_BIT_CODE(CLASS_ID, CODE_ID, CODE_VALUE) \
  case TYPE_##CODE_ID: return Type::CLASS_ID;
#include "clang/Serialization/TypeBitCodes.def"
  default:
    return std::nullopt;
  }
}

/// Read and return the type with the given index..
///
/// The index is the type ID, shifted and minus the number of predefs. This
/// routine actually reads the record corresponding to the type at the given
/// location. It is a helper routine for GetType, which deals with reading type
/// IDs.
QualType ASTReader::readTypeRecord(TypeID ID) {
  assert(ContextObj && "reading type with no AST context");
  ASTContext &Context = *ContextObj;
  RecordLocation Loc = TypeCursorForIndex(ID);
  BitstreamCursor &DeclsCursor = Loc.F->DeclsCursor;

  // Keep track of where we are in the stream, then jump back there
  // after reading this type.
  SavedStreamPosition SavedPosition(DeclsCursor);

  ReadingKindTracker ReadingKind(Read_Type, *this);

  // Note that we are loading a type record.
  Deserializing AType(this);

  if (llvm::Error Err = DeclsCursor.JumpToBit(Loc.Offset)) {
    Error(std::move(Err));
    return QualType();
  }
  Expected<unsigned> RawCode = DeclsCursor.ReadCode();
  if (!RawCode) {
    Error(RawCode.takeError());
    return QualType();
  }

  ASTRecordReader Record(*this, *Loc.F);
  Expected<unsigned> Code = Record.readRecord(DeclsCursor, RawCode.get());
  if (!Code) {
    Error(Code.takeError());
    return QualType();
  }
  if (Code.get() == TYPE_EXT_QUAL) {
    QualType baseType = Record.readQualType();
    Qualifiers quals = Record.readQualifiers();
    return Context.getQualifiedType(baseType, quals);
  }

  auto maybeClass = getTypeClassForCode((TypeCode) Code.get());
  if (!maybeClass) {
    Error("Unexpected code for type");
    return QualType();
  }

  serialization::AbstractTypeReader<ASTRecordReader> TypeReader(Record);
  return TypeReader.read(*maybeClass);
}

namespace clang {

class TypeLocReader : public TypeLocVisitor<TypeLocReader> {
  using LocSeq = SourceLocationSequence;

  ASTRecordReader &Reader;
  LocSeq *Seq;

  SourceLocation readSourceLocation() { return Reader.readSourceLocation(Seq); }
  SourceRange readSourceRange() { return Reader.readSourceRange(Seq); }

  TypeSourceInfo *GetTypeSourceInfo() {
    return Reader.readTypeSourceInfo();
  }

  NestedNameSpecifierLoc ReadNestedNameSpecifierLoc() {
    return Reader.readNestedNameSpecifierLoc();
  }

  Attr *ReadAttr() {
    return Reader.readAttr();
  }

public:
  TypeLocReader(ASTRecordReader &Reader, LocSeq *Seq)
      : Reader(Reader), Seq(Seq) {}

  // We want compile-time assurance that we've enumerated all of
  // these, so unfortunately we have to declare them first, then
  // define them out-of-line.
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  void Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc);
#include "clang/AST/TypeLocNodes.def"

  void VisitFunctionTypeLoc(FunctionTypeLoc);
  void VisitArrayTypeLoc(ArrayTypeLoc);
};

} // namespace clang

void TypeLocReader::VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
  TL.setBuiltinLoc(readSourceLocation());
  if (TL.needsExtraLocalData()) {
    TL.setWrittenTypeSpec(static_cast<DeclSpec::TST>(Reader.readInt()));
    TL.setWrittenSignSpec(static_cast<TypeSpecifierSign>(Reader.readInt()));
    TL.setWrittenWidthSpec(static_cast<TypeSpecifierWidth>(Reader.readInt()));
    TL.setModeAttr(Reader.readInt());
  }
}

void TypeLocReader::VisitComplexTypeLoc(ComplexTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitPointerTypeLoc(PointerTypeLoc TL) {
  TL.setStarLoc(readSourceLocation());
}

void TypeLocReader::VisitDecayedTypeLoc(DecayedTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitArrayParameterTypeLoc(ArrayParameterTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
  TL.setExpansionLoc(readSourceLocation());
}

void TypeLocReader::VisitBlockPointerTypeLoc(BlockPointerTypeLoc TL) {
  TL.setCaretLoc(readSourceLocation());
}

void TypeLocReader::VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc TL) {
  TL.setAmpLoc(readSourceLocation());
}

void TypeLocReader::VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc TL) {
  TL.setAmpAmpLoc(readSourceLocation());
}

void TypeLocReader::VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
  TL.setStarLoc(readSourceLocation());
  TL.setClassTInfo(GetTypeSourceInfo());
}

void TypeLocReader::VisitArrayTypeLoc(ArrayTypeLoc TL) {
  TL.setLBracketLoc(readSourceLocation());
  TL.setRBracketLoc(readSourceLocation());
  if (Reader.readBool())
    TL.setSizeExpr(Reader.readExpr());
  else
    TL.setSizeExpr(nullptr);
}

void TypeLocReader::VisitConstantArrayTypeLoc(ConstantArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitIncompleteArrayTypeLoc(IncompleteArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitVariableArrayTypeLoc(VariableArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitDependentSizedArrayTypeLoc(
                                            DependentSizedArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitDependentAddressSpaceTypeLoc(
    DependentAddressSpaceTypeLoc TL) {

    TL.setAttrNameLoc(readSourceLocation());
    TL.setAttrOperandParensRange(readSourceRange());
    TL.setAttrExprOperand(Reader.readExpr());
}

void TypeLocReader::VisitDependentSizedExtVectorTypeLoc(
                                        DependentSizedExtVectorTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitVectorTypeLoc(VectorTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitDependentVectorTypeLoc(
    DependentVectorTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitExtVectorTypeLoc(ExtVectorTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitConstantMatrixTypeLoc(ConstantMatrixTypeLoc TL) {
  TL.setAttrNameLoc(readSourceLocation());
  TL.setAttrOperandParensRange(readSourceRange());
  TL.setAttrRowOperand(Reader.readExpr());
  TL.setAttrColumnOperand(Reader.readExpr());
}

void TypeLocReader::VisitDependentSizedMatrixTypeLoc(
    DependentSizedMatrixTypeLoc TL) {
  TL.setAttrNameLoc(readSourceLocation());
  TL.setAttrOperandParensRange(readSourceRange());
  TL.setAttrRowOperand(Reader.readExpr());
  TL.setAttrColumnOperand(Reader.readExpr());
}

void TypeLocReader::VisitFunctionTypeLoc(FunctionTypeLoc TL) {
  TL.setLocalRangeBegin(readSourceLocation());
  TL.setLParenLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
  TL.setExceptionSpecRange(readSourceRange());
  TL.setLocalRangeEnd(readSourceLocation());
  for (unsigned i = 0, e = TL.getNumParams(); i != e; ++i) {
    TL.setParam(i, Reader.readDeclAs<ParmVarDecl>());
  }
}

void TypeLocReader::VisitFunctionProtoTypeLoc(FunctionProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocReader::VisitFunctionNoProtoTypeLoc(FunctionNoProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocReader::VisitUnresolvedUsingTypeLoc(UnresolvedUsingTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitUsingTypeLoc(UsingTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitTypedefTypeLoc(TypedefTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
  TL.setTypeofLoc(readSourceLocation());
  TL.setLParenLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
}

void TypeLocReader::VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
  TL.setTypeofLoc(readSourceLocation());
  TL.setLParenLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
  TL.setUnmodifiedTInfo(GetTypeSourceInfo());
}

void TypeLocReader::VisitDecltypeTypeLoc(DecltypeTypeLoc TL) {
  TL.setDecltypeLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
}

void TypeLocReader::VisitPackIndexingTypeLoc(PackIndexingTypeLoc TL) {
  TL.setEllipsisLoc(readSourceLocation());
}

void TypeLocReader::VisitUnaryTransformTypeLoc(UnaryTransformTypeLoc TL) {
  TL.setKWLoc(readSourceLocation());
  TL.setLParenLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
  TL.setUnderlyingTInfo(GetTypeSourceInfo());
}

ConceptReference *ASTRecordReader::readConceptReference() {
  auto NNS = readNestedNameSpecifierLoc();
  auto TemplateKWLoc = readSourceLocation();
  auto ConceptNameLoc = readDeclarationNameInfo();
  auto FoundDecl = readDeclAs<NamedDecl>();
  auto NamedConcept = readDeclAs<ConceptDecl>();
  auto *CR = ConceptReference::Create(
      getContext(), NNS, TemplateKWLoc, ConceptNameLoc, FoundDecl, NamedConcept,
      (readBool() ? readASTTemplateArgumentListInfo() : nullptr));
  return CR;
}

void TypeLocReader::VisitAutoTypeLoc(AutoTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
  if (Reader.readBool())
    TL.setConceptReference(Reader.readConceptReference());
  if (Reader.readBool())
    TL.setRParenLoc(readSourceLocation());
}

void TypeLocReader::VisitDeducedTemplateSpecializationTypeLoc(
    DeducedTemplateSpecializationTypeLoc TL) {
  TL.setTemplateNameLoc(readSourceLocation());
}

void TypeLocReader::VisitRecordTypeLoc(RecordTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitEnumTypeLoc(EnumTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitAttributedTypeLoc(AttributedTypeLoc TL) {
  TL.setAttr(ReadAttr());
}

void TypeLocReader::VisitCountAttributedTypeLoc(CountAttributedTypeLoc TL) {
  // Nothing to do
}

void TypeLocReader::VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
  // Nothing to do.
}

void TypeLocReader::VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitSubstTemplateTypeParmTypeLoc(
                                            SubstTemplateTypeParmTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitSubstTemplateTypeParmPackTypeLoc(
                                          SubstTemplateTypeParmPackTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitTemplateSpecializationTypeLoc(
                                           TemplateSpecializationTypeLoc TL) {
  TL.setTemplateKeywordLoc(readSourceLocation());
  TL.setTemplateNameLoc(readSourceLocation());
  TL.setLAngleLoc(readSourceLocation());
  TL.setRAngleLoc(readSourceLocation());
  for (unsigned i = 0, e = TL.getNumArgs(); i != e; ++i)
    TL.setArgLocInfo(i,
                     Reader.readTemplateArgumentLocInfo(
                         TL.getTypePtr()->template_arguments()[i].getKind()));
}

void TypeLocReader::VisitParenTypeLoc(ParenTypeLoc TL) {
  TL.setLParenLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
}

void TypeLocReader::VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
  TL.setElaboratedKeywordLoc(readSourceLocation());
  TL.setQualifierLoc(ReadNestedNameSpecifierLoc());
}

void TypeLocReader::VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
  TL.setElaboratedKeywordLoc(readSourceLocation());
  TL.setQualifierLoc(ReadNestedNameSpecifierLoc());
  TL.setNameLoc(readSourceLocation());
}

void TypeLocReader::VisitDependentTemplateSpecializationTypeLoc(
       DependentTemplateSpecializationTypeLoc TL) {
  TL.setElaboratedKeywordLoc(readSourceLocation());
  TL.setQualifierLoc(ReadNestedNameSpecifierLoc());
  TL.setTemplateKeywordLoc(readSourceLocation());
  TL.setTemplateNameLoc(readSourceLocation());
  TL.setLAngleLoc(readSourceLocation());
  TL.setRAngleLoc(readSourceLocation());
  for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I)
    TL.setArgLocInfo(I,
                     Reader.readTemplateArgumentLocInfo(
                         TL.getTypePtr()->template_arguments()[I].getKind()));
}

void TypeLocReader::VisitPackExpansionTypeLoc(PackExpansionTypeLoc TL) {
  TL.setEllipsisLoc(readSourceLocation());
}

void TypeLocReader::VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
  TL.setNameEndLoc(readSourceLocation());
}

void TypeLocReader::VisitObjCTypeParamTypeLoc(ObjCTypeParamTypeLoc TL) {
  if (TL.getNumProtocols()) {
    TL.setProtocolLAngleLoc(readSourceLocation());
    TL.setProtocolRAngleLoc(readSourceLocation());
  }
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    TL.setProtocolLoc(i, readSourceLocation());
}

void TypeLocReader::VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
  TL.setHasBaseTypeAsWritten(Reader.readBool());
  TL.setTypeArgsLAngleLoc(readSourceLocation());
  TL.setTypeArgsRAngleLoc(readSourceLocation());
  for (unsigned i = 0, e = TL.getNumTypeArgs(); i != e; ++i)
    TL.setTypeArgTInfo(i, GetTypeSourceInfo());
  TL.setProtocolLAngleLoc(readSourceLocation());
  TL.setProtocolRAngleLoc(readSourceLocation());
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    TL.setProtocolLoc(i, readSourceLocation());
}

void TypeLocReader::VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
  TL.setStarLoc(readSourceLocation());
}

void TypeLocReader::VisitAtomicTypeLoc(AtomicTypeLoc TL) {
  TL.setKWLoc(readSourceLocation());
  TL.setLParenLoc(readSourceLocation());
  TL.setRParenLoc(readSourceLocation());
}

void TypeLocReader::VisitPipeTypeLoc(PipeTypeLoc TL) {
  TL.setKWLoc(readSourceLocation());
}

void TypeLocReader::VisitBitIntTypeLoc(clang::BitIntTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}
void TypeLocReader::VisitDependentBitIntTypeLoc(
    clang::DependentBitIntTypeLoc TL) {
  TL.setNameLoc(readSourceLocation());
}

void ASTRecordReader::readTypeLoc(TypeLoc TL, LocSeq *ParentSeq) {
  LocSeq::State Seq(ParentSeq);
  TypeLocReader TLR(*this, Seq);
  for (; !TL.isNull(); TL = TL.getNextTypeLoc())
    TLR.Visit(TL);
}

TypeSourceInfo *ASTRecordReader::readTypeSourceInfo() {
  QualType InfoTy = readType();
  if (InfoTy.isNull())
    return nullptr;

  TypeSourceInfo *TInfo = getContext().CreateTypeSourceInfo(InfoTy);
  readTypeLoc(TInfo->getTypeLoc());
  return TInfo;
}

static unsigned getIndexForTypeID(serialization::TypeID ID) {
  return (ID & llvm::maskTrailingOnes<TypeID>(32)) >> Qualifiers::FastWidth;
}

static unsigned getModuleFileIndexForTypeID(serialization::TypeID ID) {
  return ID >> 32;
}

static bool isPredefinedType(serialization::TypeID ID) {
  // We don't need to erase the higher bits since if these bits are not 0,
  // it must be larger than NUM_PREDEF_TYPE_IDS.
  return (ID >> Qualifiers::FastWidth) < NUM_PREDEF_TYPE_IDS;
}

std::pair<ModuleFile *, unsigned>
ASTReader::translateTypeIDToIndex(serialization::TypeID ID) const {
  assert(!isPredefinedType(ID) &&
         "Predefined type shouldn't be in TypesLoaded");
  unsigned ModuleFileIndex = getModuleFileIndexForTypeID(ID);
  assert(ModuleFileIndex && "Untranslated Local Decl?");

  ModuleFile *OwningModuleFile = &getModuleManager()[ModuleFileIndex - 1];
  assert(OwningModuleFile &&
         "untranslated type ID or local type ID shouldn't be in TypesLoaded");

  return {OwningModuleFile,
          OwningModuleFile->BaseTypeIndex + getIndexForTypeID(ID)};
}

QualType ASTReader::GetType(TypeID ID) {
  assert(ContextObj && "reading type with no AST context");
  ASTContext &Context = *ContextObj;

  unsigned FastQuals = ID & Qualifiers::FastMask;

  if (isPredefinedType(ID)) {
    QualType T;
    unsigned Index = getIndexForTypeID(ID);
    switch ((PredefinedTypeIDs)Index) {
    case PREDEF_TYPE_LAST_ID:
      // We should never use this one.
      llvm_unreachable("Invalid predefined type");
      break;
    case PREDEF_TYPE_NULL_ID:
      return QualType();
    case PREDEF_TYPE_VOID_ID:
      T = Context.VoidTy;
      break;
    case PREDEF_TYPE_BOOL_ID:
      T = Context.BoolTy;
      break;
    case PREDEF_TYPE_CHAR_U_ID:
    case PREDEF_TYPE_CHAR_S_ID:
      // FIXME: Check that the signedness of CharTy is correct!
      T = Context.CharTy;
      break;
    case PREDEF_TYPE_UCHAR_ID:
      T = Context.UnsignedCharTy;
      break;
    case PREDEF_TYPE_USHORT_ID:
      T = Context.UnsignedShortTy;
      break;
    case PREDEF_TYPE_UINT_ID:
      T = Context.UnsignedIntTy;
      break;
    case PREDEF_TYPE_ULONG_ID:
      T = Context.UnsignedLongTy;
      break;
    case PREDEF_TYPE_ULONGLONG_ID:
      T = Context.UnsignedLongLongTy;
      break;
    case PREDEF_TYPE_UINT128_ID:
      T = Context.UnsignedInt128Ty;
      break;
    case PREDEF_TYPE_SCHAR_ID:
      T = Context.SignedCharTy;
      break;
    case PREDEF_TYPE_WCHAR_ID:
      T = Context.WCharTy;
      break;
    case PREDEF_TYPE_SHORT_ID:
      T = Context.ShortTy;
      break;
    case PREDEF_TYPE_INT_ID:
      T = Context.IntTy;
      break;
    case PREDEF_TYPE_LONG_ID:
      T = Context.LongTy;
      break;
    case PREDEF_TYPE_LONGLONG_ID:
      T = Context.LongLongTy;
      break;
    case PREDEF_TYPE_INT128_ID:
      T = Context.Int128Ty;
      break;
    case PREDEF_TYPE_BFLOAT16_ID:
      T = Context.BFloat16Ty;
      break;
    case PREDEF_TYPE_HALF_ID:
      T = Context.HalfTy;
      break;
    case PREDEF_TYPE_FLOAT_ID:
      T = Context.FloatTy;
      break;
    case PREDEF_TYPE_DOUBLE_ID:
      T = Context.DoubleTy;
      break;
    case PREDEF_TYPE_LONGDOUBLE_ID:
      T = Context.LongDoubleTy;
      break;
    case PREDEF_TYPE_SHORT_ACCUM_ID:
      T = Context.ShortAccumTy;
      break;
    case PREDEF_TYPE_ACCUM_ID:
      T = Context.AccumTy;
      break;
    case PREDEF_TYPE_LONG_ACCUM_ID:
      T = Context.LongAccumTy;
      break;
    case PREDEF_TYPE_USHORT_ACCUM_ID:
      T = Context.UnsignedShortAccumTy;
      break;
    case PREDEF_TYPE_UACCUM_ID:
      T = Context.UnsignedAccumTy;
      break;
    case PREDEF_TYPE_ULONG_ACCUM_ID:
      T = Context.UnsignedLongAccumTy;
      break;
    case PREDEF_TYPE_SHORT_FRACT_ID:
      T = Context.ShortFractTy;
      break;
    case PREDEF_TYPE_FRACT_ID:
      T = Context.FractTy;
      break;
    case PREDEF_TYPE_LONG_FRACT_ID:
      T = Context.LongFractTy;
      break;
    case PREDEF_TYPE_USHORT_FRACT_ID:
      T = Context.UnsignedShortFractTy;
      break;
    case PREDEF_TYPE_UFRACT_ID:
      T = Context.UnsignedFractTy;
      break;
    case PREDEF_TYPE_ULONG_FRACT_ID:
      T = Context.UnsignedLongFractTy;
      break;
    case PREDEF_TYPE_SAT_SHORT_ACCUM_ID:
      T = Context.SatShortAccumTy;
      break;
    case PREDEF_TYPE_SAT_ACCUM_ID:
      T = Context.SatAccumTy;
      break;
    case PREDEF_TYPE_SAT_LONG_ACCUM_ID:
      T = Context.SatLongAccumTy;
      break;
    case PREDEF_TYPE_SAT_USHORT_ACCUM_ID:
      T = Context.SatUnsignedShortAccumTy;
      break;
    case PREDEF_TYPE_SAT_UACCUM_ID:
      T = Context.SatUnsignedAccumTy;
      break;
    case PREDEF_TYPE_SAT_ULONG_ACCUM_ID:
      T = Context.SatUnsignedLongAccumTy;
      break;
    case PREDEF_TYPE_SAT_SHORT_FRACT_ID:
      T = Context.SatShortFractTy;
      break;
    case PREDEF_TYPE_SAT_FRACT_ID:
      T = Context.SatFractTy;
      break;
    case PREDEF_TYPE_SAT_LONG_FRACT_ID:
      T = Context.SatLongFractTy;
      break;
    case PREDEF_TYPE_SAT_USHORT_FRACT_ID:
      T = Context.SatUnsignedShortFractTy;
      break;
    case PREDEF_TYPE_SAT_UFRACT_ID:
      T = Context.SatUnsignedFractTy;
      break;
    case PREDEF_TYPE_SAT_ULONG_FRACT_ID:
      T = Context.SatUnsignedLongFractTy;
      break;
    case PREDEF_TYPE_FLOAT16_ID:
      T = Context.Float16Ty;
      break;
    case PREDEF_TYPE_FLOAT128_ID:
      T = Context.Float128Ty;
      break;
    case PREDEF_TYPE_IBM128_ID:
      T = Context.Ibm128Ty;
      break;
    case PREDEF_TYPE_OVERLOAD_ID:
      T = Context.OverloadTy;
      break;
    case PREDEF_TYPE_UNRESOLVED_TEMPLATE:
      T = Context.UnresolvedTemplateTy;
      break;
    case PREDEF_TYPE_BOUND_MEMBER:
      T = Context.BoundMemberTy;
      break;
    case PREDEF_TYPE_PSEUDO_OBJECT:
      T = Context.PseudoObjectTy;
      break;
    case PREDEF_TYPE_DEPENDENT_ID:
      T = Context.DependentTy;
      break;
    case PREDEF_TYPE_UNKNOWN_ANY:
      T = Context.UnknownAnyTy;
      break;
    case PREDEF_TYPE_NULLPTR_ID:
      T = Context.NullPtrTy;
      break;
    case PREDEF_TYPE_CHAR8_ID:
      T = Context.Char8Ty;
      break;
    case PREDEF_TYPE_CHAR16_ID:
      T = Context.Char16Ty;
      break;
    case PREDEF_TYPE_CHAR32_ID:
      T = Context.Char32Ty;
      break;
    case PREDEF_TYPE_OBJC_ID:
      T = Context.ObjCBuiltinIdTy;
      break;
    case PREDEF_TYPE_OBJC_CLASS:
      T = Context.ObjCBuiltinClassTy;
      break;
    case PREDEF_TYPE_OBJC_SEL:
      T = Context.ObjCBuiltinSelTy;
      break;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.SingletonId; \
      break;
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.Id##Ty; \
      break;
#include "clang/Basic/OpenCLExtensionTypes.def"
    case PREDEF_TYPE_SAMPLER_ID:
      T = Context.OCLSamplerTy;
      break;
    case PREDEF_TYPE_EVENT_ID:
      T = Context.OCLEventTy;
      break;
    case PREDEF_TYPE_CLK_EVENT_ID:
      T = Context.OCLClkEventTy;
      break;
    case PREDEF_TYPE_QUEUE_ID:
      T = Context.OCLQueueTy;
      break;
    case PREDEF_TYPE_RESERVE_ID_ID:
      T = Context.OCLReserveIDTy;
      break;
    case PREDEF_TYPE_AUTO_DEDUCT:
      T = Context.getAutoDeductType();
      break;
    case PREDEF_TYPE_AUTO_RREF_DEDUCT:
      T = Context.getAutoRRefDeductType();
      break;
    case PREDEF_TYPE_ARC_UNBRIDGED_CAST:
      T = Context.ARCUnbridgedCastTy;
      break;
    case PREDEF_TYPE_BUILTIN_FN:
      T = Context.BuiltinFnTy;
      break;
    case PREDEF_TYPE_INCOMPLETE_MATRIX_IDX:
      T = Context.IncompleteMatrixIdxTy;
      break;
    case PREDEF_TYPE_ARRAY_SECTION:
      T = Context.ArraySectionTy;
      break;
    case PREDEF_TYPE_OMP_ARRAY_SHAPING:
      T = Context.OMPArrayShapingTy;
      break;
    case PREDEF_TYPE_OMP_ITERATOR:
      T = Context.OMPIteratorTy;
      break;
#define SVE_TYPE(Name, Id, SingletonId) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.SingletonId; \
      break;
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.Id##Ty; \
      break;
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.SingletonId; \
      break;
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId)                                       \
  case PREDEF_TYPE_##Id##_ID:                                                  \
    T = Context.SingletonId;                                                   \
    break;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId)                                     \
  case PREDEF_TYPE_##Id##_ID:                                                  \
    T = Context.SingletonId;                                                   \
    break;
#include "clang/Basic/AMDGPUTypes.def"
    }

    assert(!T.isNull() && "Unknown predefined type");
    return T.withFastQualifiers(FastQuals);
  }

  unsigned Index = translateTypeIDToIndex(ID).second;

  assert(Index < TypesLoaded.size() && "Type index out-of-range");
  if (TypesLoaded[Index].isNull()) {
    TypesLoaded[Index] = readTypeRecord(ID);
    if (TypesLoaded[Index].isNull())
      return QualType();

    TypesLoaded[Index]->setFromAST();
    if (DeserializationListener)
      DeserializationListener->TypeRead(TypeIdx::fromTypeID(ID),
                                        TypesLoaded[Index]);
  }

  return TypesLoaded[Index].withFastQualifiers(FastQuals);
}

QualType ASTReader::getLocalType(ModuleFile &F, LocalTypeID LocalID) {
  return GetType(getGlobalTypeID(F, LocalID));
}

serialization::TypeID ASTReader::getGlobalTypeID(ModuleFile &F,
                                                 LocalTypeID LocalID) const {
  if (isPredefinedType(LocalID))
    return LocalID;

  if (!F.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(F);

  unsigned ModuleFileIndex = getModuleFileIndexForTypeID(LocalID);
  LocalID &= llvm::maskTrailingOnes<TypeID>(32);

  if (ModuleFileIndex == 0)
    LocalID -= NUM_PREDEF_TYPE_IDS << Qualifiers::FastWidth;

  ModuleFile &MF =
      ModuleFileIndex ? *F.TransitiveImports[ModuleFileIndex - 1] : F;
  ModuleFileIndex = MF.Index + 1;
  return ((uint64_t)ModuleFileIndex << 32) | LocalID;
}

TemplateArgumentLocInfo
ASTRecordReader::readTemplateArgumentLocInfo(TemplateArgument::ArgKind Kind) {
  switch (Kind) {
  case TemplateArgument::Expression:
    return readExpr();
  case TemplateArgument::Type:
    return readTypeSourceInfo();
  case TemplateArgument::Template: {
    NestedNameSpecifierLoc QualifierLoc =
      readNestedNameSpecifierLoc();
    SourceLocation TemplateNameLoc = readSourceLocation();
    return TemplateArgumentLocInfo(getASTContext(), QualifierLoc,
                                   TemplateNameLoc, SourceLocation());
  }
  case TemplateArgument::TemplateExpansion: {
    NestedNameSpecifierLoc QualifierLoc = readNestedNameSpecifierLoc();
    SourceLocation TemplateNameLoc = readSourceLocation();
    SourceLocation EllipsisLoc = readSourceLocation();
    return TemplateArgumentLocInfo(getASTContext(), QualifierLoc,
                                   TemplateNameLoc, EllipsisLoc);
  }
  case TemplateArgument::Null:
  case TemplateArgument::Integral:
  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::Pack:
    // FIXME: Is this right?
    return TemplateArgumentLocInfo();
  }
  llvm_unreachable("unexpected template argument loc");
}

TemplateArgumentLoc ASTRecordReader::readTemplateArgumentLoc() {
  TemplateArgument Arg = readTemplateArgument();

  if (Arg.getKind() == TemplateArgument::Expression) {
    if (readBool()) // bool InfoHasSameExpr.
      return TemplateArgumentLoc(Arg, TemplateArgumentLocInfo(Arg.getAsExpr()));
  }
  return TemplateArgumentLoc(Arg, readTemplateArgumentLocInfo(Arg.getKind()));
}

void ASTRecordReader::readTemplateArgumentListInfo(
    TemplateArgumentListInfo &Result) {
  Result.setLAngleLoc(readSourceLocation());
  Result.setRAngleLoc(readSourceLocation());
  unsigned NumArgsAsWritten = readInt();
  for (unsigned i = 0; i != NumArgsAsWritten; ++i)
    Result.addArgument(readTemplateArgumentLoc());
}

const ASTTemplateArgumentListInfo *
ASTRecordReader::readASTTemplateArgumentListInfo() {
  TemplateArgumentListInfo Result;
  readTemplateArgumentListInfo(Result);
  return ASTTemplateArgumentListInfo::Create(getContext(), Result);
}

Decl *ASTReader::GetExternalDecl(GlobalDeclID ID) { return GetDecl(ID); }

void ASTReader::CompleteRedeclChain(const Decl *D) {
  if (NumCurrentElementsDeserializing) {
    // We arrange to not care about the complete redeclaration chain while we're
    // deserializing. Just remember that the AST has marked this one as complete
    // but that it's not actually complete yet, so we know we still need to
    // complete it later.
    PendingIncompleteDeclChains.push_back(const_cast<Decl*>(D));
    return;
  }

  if (!D->getDeclContext()) {
    assert(isa<TranslationUnitDecl>(D) && "Not a TU?");
    return;
  }

  const DeclContext *DC = D->getDeclContext()->getRedeclContext();

  // If this is a named declaration, complete it by looking it up
  // within its context.
  //
  // FIXME: Merging a function definition should merge
  // all mergeable entities within it.
  if (isa<TranslationUnitDecl, NamespaceDecl, RecordDecl, EnumDecl>(DC)) {
    if (DeclarationName Name = cast<NamedDecl>(D)->getDeclName()) {
      if (!getContext().getLangOpts().CPlusPlus &&
          isa<TranslationUnitDecl>(DC)) {
        // Outside of C++, we don't have a lookup table for the TU, so update
        // the identifier instead. (For C++ modules, we don't store decls
        // in the serialized identifier table, so we do the lookup in the TU.)
        auto *II = Name.getAsIdentifierInfo();
        assert(II && "non-identifier name in C?");
        if (II->isOutOfDate())
          updateOutOfDateIdentifier(*II);
      } else
        DC->lookup(Name);
    } else if (needsAnonymousDeclarationNumber(cast<NamedDecl>(D))) {
      // Find all declarations of this kind from the relevant context.
      for (auto *DCDecl : cast<Decl>(D->getLexicalDeclContext())->redecls()) {
        auto *DC = cast<DeclContext>(DCDecl);
        SmallVector<Decl*, 8> Decls;
        FindExternalLexicalDecls(
            DC, [&](Decl::Kind K) { return K == D->getKind(); }, Decls);
      }
    }
  }

  if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
    CTSD->getSpecializedTemplate()->LoadLazySpecializations();
  if (auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(D))
    VTSD->getSpecializedTemplate()->LoadLazySpecializations();
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (auto *Template = FD->getPrimaryTemplate())
      Template->LoadLazySpecializations();
  }
}

CXXCtorInitializer **
ASTReader::GetExternalCXXCtorInitializers(uint64_t Offset) {
  RecordLocation Loc = getLocalBitOffset(Offset);
  BitstreamCursor &Cursor = Loc.F->DeclsCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(Loc.Offset)) {
    Error(std::move(Err));
    return nullptr;
  }
  ReadingKindTracker ReadingKind(Read_Decl, *this);
  Deserializing D(this);

  Expected<unsigned> MaybeCode = Cursor.ReadCode();
  if (!MaybeCode) {
    Error(MaybeCode.takeError());
    return nullptr;
  }
  unsigned Code = MaybeCode.get();

  ASTRecordReader Record(*this, *Loc.F);
  Expected<unsigned> MaybeRecCode = Record.readRecord(Cursor, Code);
  if (!MaybeRecCode) {
    Error(MaybeRecCode.takeError());
    return nullptr;
  }
  if (MaybeRecCode.get() != DECL_CXX_CTOR_INITIALIZERS) {
    Error("malformed AST file: missing C++ ctor initializers");
    return nullptr;
  }

  return Record.readCXXCtorInitializers();
}

CXXBaseSpecifier *ASTReader::GetExternalCXXBaseSpecifiers(uint64_t Offset) {
  assert(ContextObj && "reading base specifiers with no AST context");
  ASTContext &Context = *ContextObj;

  RecordLocation Loc = getLocalBitOffset(Offset);
  BitstreamCursor &Cursor = Loc.F->DeclsCursor;
  SavedStreamPosition SavedPosition(Cursor);
  if (llvm::Error Err = Cursor.JumpToBit(Loc.Offset)) {
    Error(std::move(Err));
    return nullptr;
  }
  ReadingKindTracker ReadingKind(Read_Decl, *this);
  Deserializing D(this);

  Expected<unsigned> MaybeCode = Cursor.ReadCode();
  if (!MaybeCode) {
    Error(MaybeCode.takeError());
    return nullptr;
  }
  unsigned Code = MaybeCode.get();

  ASTRecordReader Record(*this, *Loc.F);
  Expected<unsigned> MaybeRecCode = Record.readRecord(Cursor, Code);
  if (!MaybeRecCode) {
    Error(MaybeCode.takeError());
    return nullptr;
  }
  unsigned RecCode = MaybeRecCode.get();

  if (RecCode != DECL_CXX_BASE_SPECIFIERS) {
    Error("malformed AST file: missing C++ base specifiers");
    return nullptr;
  }

  unsigned NumBases = Record.readInt();
  void *Mem = Context.Allocate(sizeof(CXXBaseSpecifier) * NumBases);
  CXXBaseSpecifier *Bases = new (Mem) CXXBaseSpecifier [NumBases];
  for (unsigned I = 0; I != NumBases; ++I)
    Bases[I] = Record.readCXXBaseSpecifier();
  return Bases;
}

GlobalDeclID ASTReader::getGlobalDeclID(ModuleFile &F,
                                        LocalDeclID LocalID) const {
  if (LocalID < NUM_PREDEF_DECL_IDS)
    return GlobalDeclID(LocalID.getRawValue());

  unsigned OwningModuleFileIndex = LocalID.getModuleFileIndex();
  DeclID ID = LocalID.getLocalDeclIndex();

  if (!F.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(F);

  ModuleFile *OwningModuleFile =
      OwningModuleFileIndex == 0
          ? &F
          : F.TransitiveImports[OwningModuleFileIndex - 1];

  if (OwningModuleFileIndex == 0)
    ID -= NUM_PREDEF_DECL_IDS;

  uint64_t NewModuleFileIndex = OwningModuleFile->Index + 1;
  return GlobalDeclID(NewModuleFileIndex, ID);
}

bool ASTReader::isDeclIDFromModule(GlobalDeclID ID, ModuleFile &M) const {
  // Predefined decls aren't from any module.
  if (ID < NUM_PREDEF_DECL_IDS)
    return false;

  unsigned ModuleFileIndex = ID.getModuleFileIndex();
  return M.Index == ModuleFileIndex - 1;
}

ModuleFile *ASTReader::getOwningModuleFile(GlobalDeclID ID) const {
  // Predefined decls aren't from any module.
  if (ID < NUM_PREDEF_DECL_IDS)
    return nullptr;

  uint64_t ModuleFileIndex = ID.getModuleFileIndex();
  assert(ModuleFileIndex && "Untranslated Local Decl?");

  return &getModuleManager()[ModuleFileIndex - 1];
}

ModuleFile *ASTReader::getOwningModuleFile(const Decl *D) const {
  if (!D->isFromASTFile())
    return nullptr;

  return getOwningModuleFile(D->getGlobalID());
}

SourceLocation ASTReader::getSourceLocationForDeclID(GlobalDeclID ID) {
  if (ID < NUM_PREDEF_DECL_IDS)
    return SourceLocation();

  if (Decl *D = GetExistingDecl(ID))
    return D->getLocation();

  SourceLocation Loc;
  DeclCursorForID(ID, Loc);
  return Loc;
}

static Decl *getPredefinedDecl(ASTContext &Context, PredefinedDeclIDs ID) {
  switch (ID) {
  case PREDEF_DECL_NULL_ID:
    return nullptr;

  case PREDEF_DECL_TRANSLATION_UNIT_ID:
    return Context.getTranslationUnitDecl();

  case PREDEF_DECL_OBJC_ID_ID:
    return Context.getObjCIdDecl();

  case PREDEF_DECL_OBJC_SEL_ID:
    return Context.getObjCSelDecl();

  case PREDEF_DECL_OBJC_CLASS_ID:
    return Context.getObjCClassDecl();

  case PREDEF_DECL_OBJC_PROTOCOL_ID:
    return Context.getObjCProtocolDecl();

  case PREDEF_DECL_INT_128_ID:
    return Context.getInt128Decl();

  case PREDEF_DECL_UNSIGNED_INT_128_ID:
    return Context.getUInt128Decl();

  case PREDEF_DECL_OBJC_INSTANCETYPE_ID:
    return Context.getObjCInstanceTypeDecl();

  case PREDEF_DECL_BUILTIN_VA_LIST_ID:
    return Context.getBuiltinVaListDecl();

  case PREDEF_DECL_VA_LIST_TAG:
    return Context.getVaListTagDecl();

  case PREDEF_DECL_BUILTIN_MS_VA_LIST_ID:
    return Context.getBuiltinMSVaListDecl();

  case PREDEF_DECL_BUILTIN_MS_GUID_ID:
    return Context.getMSGuidTagDecl();

  case PREDEF_DECL_EXTERN_C_CONTEXT_ID:
    return Context.getExternCContextDecl();

  case PREDEF_DECL_MAKE_INTEGER_SEQ_ID:
    return Context.getMakeIntegerSeqDecl();

  case PREDEF_DECL_CF_CONSTANT_STRING_ID:
    return Context.getCFConstantStringDecl();

  case PREDEF_DECL_CF_CONSTANT_STRING_TAG_ID:
    return Context.getCFConstantStringTagDecl();

  case PREDEF_DECL_TYPE_PACK_ELEMENT_ID:
    return Context.getTypePackElementDecl();
  }
  llvm_unreachable("PredefinedDeclIDs unknown enum value");
}

unsigned ASTReader::translateGlobalDeclIDToIndex(GlobalDeclID GlobalID) const {
  ModuleFile *OwningModuleFile = getOwningModuleFile(GlobalID);
  if (!OwningModuleFile) {
    assert(GlobalID < NUM_PREDEF_DECL_IDS && "Untransalted Global ID?");
    return GlobalID.getRawValue();
  }

  return OwningModuleFile->BaseDeclIndex + GlobalID.getLocalDeclIndex();
}

Decl *ASTReader::GetExistingDecl(GlobalDeclID ID) {
  assert(ContextObj && "reading decl with no AST context");

  if (ID < NUM_PREDEF_DECL_IDS) {
    Decl *D = getPredefinedDecl(*ContextObj, (PredefinedDeclIDs)ID);
    if (D) {
      // Track that we have merged the declaration with ID \p ID into the
      // pre-existing predefined declaration \p D.
      auto &Merged = KeyDecls[D->getCanonicalDecl()];
      if (Merged.empty())
        Merged.push_back(ID);
    }
    return D;
  }

  unsigned Index = translateGlobalDeclIDToIndex(ID);

  if (Index >= DeclsLoaded.size()) {
    assert(0 && "declaration ID out-of-range for AST file");
    Error("declaration ID out-of-range for AST file");
    return nullptr;
  }

  return DeclsLoaded[Index];
}

Decl *ASTReader::GetDecl(GlobalDeclID ID) {
  if (ID < NUM_PREDEF_DECL_IDS)
    return GetExistingDecl(ID);

  unsigned Index = translateGlobalDeclIDToIndex(ID);

  if (Index >= DeclsLoaded.size()) {
    assert(0 && "declaration ID out-of-range for AST file");
    Error("declaration ID out-of-range for AST file");
    return nullptr;
  }

  if (!DeclsLoaded[Index]) {
    ReadDeclRecord(ID);
    if (DeserializationListener)
      DeserializationListener->DeclRead(ID, DeclsLoaded[Index]);
  }

  return DeclsLoaded[Index];
}

LocalDeclID ASTReader::mapGlobalIDToModuleFileGlobalID(ModuleFile &M,
                                                       GlobalDeclID GlobalID) {
  if (GlobalID < NUM_PREDEF_DECL_IDS)
    return LocalDeclID::get(*this, M, GlobalID.getRawValue());

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ModuleFile *Owner = getOwningModuleFile(GlobalID);
  DeclID ID = GlobalID.getLocalDeclIndex();

  if (Owner == &M) {
    ID += NUM_PREDEF_DECL_IDS;
    return LocalDeclID::get(*this, M, ID);
  }

  uint64_t OrignalModuleFileIndex = 0;
  for (unsigned I = 0; I < M.TransitiveImports.size(); I++)
    if (M.TransitiveImports[I] == Owner) {
      OrignalModuleFileIndex = I + 1;
      break;
    }

  if (!OrignalModuleFileIndex)
    return LocalDeclID();

  return LocalDeclID::get(*this, M, OrignalModuleFileIndex, ID);
}

GlobalDeclID ASTReader::ReadDeclID(ModuleFile &F, const RecordDataImpl &Record,
                                   unsigned &Idx) {
  if (Idx >= Record.size()) {
    Error("Corrupted AST file");
    return GlobalDeclID(0);
  }

  return getGlobalDeclID(F, LocalDeclID::get(*this, F, Record[Idx++]));
}

/// Resolve the offset of a statement into a statement.
///
/// This operation will read a new statement from the external
/// source each time it is called, and is meant to be used via a
/// LazyOffsetPtr (which is used by Decls for the body of functions, etc).
Stmt *ASTReader::GetExternalDeclStmt(uint64_t Offset) {
  // Switch case IDs are per Decl.
  ClearSwitchCaseIDs();

  // Offset here is a global offset across the entire chain.
  RecordLocation Loc = getLocalBitOffset(Offset);
  if (llvm::Error Err = Loc.F->DeclsCursor.JumpToBit(Loc.Offset)) {
    Error(std::move(Err));
    return nullptr;
  }
  assert(NumCurrentElementsDeserializing == 0 &&
         "should not be called while already deserializing");
  Deserializing D(this);
  return ReadStmtFromStream(*Loc.F);
}

void ASTReader::FindExternalLexicalDecls(
    const DeclContext *DC, llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
    SmallVectorImpl<Decl *> &Decls) {
  bool PredefsVisited[NUM_PREDEF_DECL_IDS] = {};

  auto Visit = [&] (ModuleFile *M, LexicalContents LexicalDecls) {
    assert(LexicalDecls.size() % 2 == 0 && "expected an even number of entries");
    for (int I = 0, N = LexicalDecls.size(); I != N; I += 2) {
      auto K = (Decl::Kind)+LexicalDecls[I];
      if (!IsKindWeWant(K))
        continue;

      auto ID = (DeclID) + LexicalDecls[I + 1];

      // Don't add predefined declarations to the lexical context more
      // than once.
      if (ID < NUM_PREDEF_DECL_IDS) {
        if (PredefsVisited[ID])
          continue;

        PredefsVisited[ID] = true;
      }

      if (Decl *D = GetLocalDecl(*M, LocalDeclID::get(*this, *M, ID))) {
        assert(D->getKind() == K && "wrong kind for lexical decl");
        if (!DC->isDeclInLexicalTraversal(D))
          Decls.push_back(D);
      }
    }
  };

  if (isa<TranslationUnitDecl>(DC)) {
    for (const auto &Lexical : TULexicalDecls)
      Visit(Lexical.first, Lexical.second);
  } else {
    auto I = LexicalDecls.find(DC);
    if (I != LexicalDecls.end())
      Visit(I->second.first, I->second.second);
  }

  ++NumLexicalDeclContextsRead;
}

namespace {

class UnalignedDeclIDComp {
  ASTReader &Reader;
  ModuleFile &Mod;

public:
  UnalignedDeclIDComp(ASTReader &Reader, ModuleFile &M)
      : Reader(Reader), Mod(M) {}

  bool operator()(unaligned_decl_id_t L, unaligned_decl_id_t R) const {
    SourceLocation LHS = getLocation(L);
    SourceLocation RHS = getLocation(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(SourceLocation LHS, unaligned_decl_id_t R) const {
    SourceLocation RHS = getLocation(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(unaligned_decl_id_t L, SourceLocation RHS) const {
    SourceLocation LHS = getLocation(L);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  SourceLocation getLocation(unaligned_decl_id_t ID) const {
    return Reader.getSourceManager().getFileLoc(
        Reader.getSourceLocationForDeclID(
            Reader.getGlobalDeclID(Mod, LocalDeclID::get(Reader, Mod, ID))));
  }
};

} // namespace

void ASTReader::FindFileRegionDecls(FileID File,
                                    unsigned Offset, unsigned Length,
                                    SmallVectorImpl<Decl *> &Decls) {
  SourceManager &SM = getSourceManager();

  llvm::DenseMap<FileID, FileDeclsInfo>::iterator I = FileDeclIDs.find(File);
  if (I == FileDeclIDs.end())
    return;

  FileDeclsInfo &DInfo = I->second;
  if (DInfo.Decls.empty())
    return;

  SourceLocation
    BeginLoc = SM.getLocForStartOfFile(File).getLocWithOffset(Offset);
  SourceLocation EndLoc = BeginLoc.getLocWithOffset(Length);

  UnalignedDeclIDComp DIDComp(*this, *DInfo.Mod);
  ArrayRef<unaligned_decl_id_t>::iterator BeginIt =
      llvm::lower_bound(DInfo.Decls, BeginLoc, DIDComp);
  if (BeginIt != DInfo.Decls.begin())
    --BeginIt;

  // If we are pointing at a top-level decl inside an objc container, we need
  // to backtrack until we find it otherwise we will fail to report that the
  // region overlaps with an objc container.
  while (BeginIt != DInfo.Decls.begin() &&
         GetDecl(getGlobalDeclID(*DInfo.Mod,
                                 LocalDeclID::get(*this, *DInfo.Mod, *BeginIt)))
             ->isTopLevelDeclInObjCContainer())
    --BeginIt;

  ArrayRef<unaligned_decl_id_t>::iterator EndIt =
      llvm::upper_bound(DInfo.Decls, EndLoc, DIDComp);
  if (EndIt != DInfo.Decls.end())
    ++EndIt;

  for (ArrayRef<unaligned_decl_id_t>::iterator DIt = BeginIt; DIt != EndIt;
       ++DIt)
    Decls.push_back(GetDecl(getGlobalDeclID(
        *DInfo.Mod, LocalDeclID::get(*this, *DInfo.Mod, *DIt))));
}

bool
ASTReader::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                          DeclarationName Name) {
  assert(DC->hasExternalVisibleStorage() && DC == DC->getPrimaryContext() &&
         "DeclContext has no visible decls in storage");
  if (!Name)
    return false;

  auto It = Lookups.find(DC);
  if (It == Lookups.end())
    return false;

  Deserializing LookupResults(this);

  // Load the list of declarations.
  SmallVector<NamedDecl *, 64> Decls;
  llvm::SmallPtrSet<NamedDecl *, 8> Found;

  for (GlobalDeclID ID : It->second.Table.find(Name)) {
    NamedDecl *ND = cast<NamedDecl>(GetDecl(ID));
    if (ND->getDeclName() == Name && Found.insert(ND).second)
      Decls.push_back(ND);
  }

  ++NumVisibleDeclContextsRead;
  SetExternalVisibleDeclsForName(DC, Name, Decls);
  return !Decls.empty();
}

void ASTReader::completeVisibleDeclsMap(const DeclContext *DC) {
  if (!DC->hasExternalVisibleStorage())
    return;

  auto It = Lookups.find(DC);
  assert(It != Lookups.end() &&
         "have external visible storage but no lookup tables");

  DeclsMap Decls;

  for (GlobalDeclID ID : It->second.Table.findAll()) {
    NamedDecl *ND = cast<NamedDecl>(GetDecl(ID));
    Decls[ND->getDeclName()].push_back(ND);
  }

  ++NumVisibleDeclContextsRead;

  for (DeclsMap::iterator I = Decls.begin(), E = Decls.end(); I != E; ++I) {
    SetExternalVisibleDeclsForName(DC, I->first, I->second);
  }
  const_cast<DeclContext *>(DC)->setHasExternalVisibleStorage(false);
}

const serialization::reader::DeclContextLookupTable *
ASTReader::getLoadedLookupTables(DeclContext *Primary) const {
  auto I = Lookups.find(Primary);
  return I == Lookups.end() ? nullptr : &I->second;
}

/// Under non-PCH compilation the consumer receives the objc methods
/// before receiving the implementation, and codegen depends on this.
/// We simulate this by deserializing and passing to consumer the methods of the
/// implementation before passing the deserialized implementation decl.
static void PassObjCImplDeclToConsumer(ObjCImplDecl *ImplD,
                                       ASTConsumer *Consumer) {
  assert(ImplD && Consumer);

  for (auto *I : ImplD->methods())
    Consumer->HandleInterestingDecl(DeclGroupRef(I));

  Consumer->HandleInterestingDecl(DeclGroupRef(ImplD));
}

void ASTReader::PassInterestingDeclToConsumer(Decl *D) {
  if (ObjCImplDecl *ImplD = dyn_cast<ObjCImplDecl>(D))
    PassObjCImplDeclToConsumer(ImplD, Consumer);
  else
    Consumer->HandleInterestingDecl(DeclGroupRef(D));
}

void ASTReader::PassVTableToConsumer(CXXRecordDecl *RD) {
  Consumer->HandleVTable(RD);
}

void ASTReader::StartTranslationUnit(ASTConsumer *Consumer) {
  this->Consumer = Consumer;

  if (Consumer)
    PassInterestingDeclsToConsumer();

  if (DeserializationListener)
    DeserializationListener->ReaderInitialized(this);
}

void ASTReader::PrintStats() {
  std::fprintf(stderr, "*** AST File Statistics:\n");

  unsigned NumTypesLoaded =
      TypesLoaded.size() - llvm::count(TypesLoaded.materialized(), QualType());
  unsigned NumDeclsLoaded =
      DeclsLoaded.size() -
      llvm::count(DeclsLoaded.materialized(), (Decl *)nullptr);
  unsigned NumIdentifiersLoaded =
      IdentifiersLoaded.size() -
      llvm::count(IdentifiersLoaded, (IdentifierInfo *)nullptr);
  unsigned NumMacrosLoaded =
      MacrosLoaded.size() - llvm::count(MacrosLoaded, (MacroInfo *)nullptr);
  unsigned NumSelectorsLoaded =
      SelectorsLoaded.size() - llvm::count(SelectorsLoaded, Selector());

  if (unsigned TotalNumSLocEntries = getTotalNumSLocs())
    std::fprintf(stderr, "  %u/%u source location entries read (%f%%)\n",
                 NumSLocEntriesRead, TotalNumSLocEntries,
                 ((float)NumSLocEntriesRead/TotalNumSLocEntries * 100));
  if (!TypesLoaded.empty())
    std::fprintf(stderr, "  %u/%u types read (%f%%)\n",
                 NumTypesLoaded, (unsigned)TypesLoaded.size(),
                 ((float)NumTypesLoaded/TypesLoaded.size() * 100));
  if (!DeclsLoaded.empty())
    std::fprintf(stderr, "  %u/%u declarations read (%f%%)\n",
                 NumDeclsLoaded, (unsigned)DeclsLoaded.size(),
                 ((float)NumDeclsLoaded/DeclsLoaded.size() * 100));
  if (!IdentifiersLoaded.empty())
    std::fprintf(stderr, "  %u/%u identifiers read (%f%%)\n",
                 NumIdentifiersLoaded, (unsigned)IdentifiersLoaded.size(),
                 ((float)NumIdentifiersLoaded/IdentifiersLoaded.size() * 100));
  if (!MacrosLoaded.empty())
    std::fprintf(stderr, "  %u/%u macros read (%f%%)\n",
                 NumMacrosLoaded, (unsigned)MacrosLoaded.size(),
                 ((float)NumMacrosLoaded/MacrosLoaded.size() * 100));
  if (!SelectorsLoaded.empty())
    std::fprintf(stderr, "  %u/%u selectors read (%f%%)\n",
                 NumSelectorsLoaded, (unsigned)SelectorsLoaded.size(),
                 ((float)NumSelectorsLoaded/SelectorsLoaded.size() * 100));
  if (TotalNumStatements)
    std::fprintf(stderr, "  %u/%u statements read (%f%%)\n",
                 NumStatementsRead, TotalNumStatements,
                 ((float)NumStatementsRead/TotalNumStatements * 100));
  if (TotalNumMacros)
    std::fprintf(stderr, "  %u/%u macros read (%f%%)\n",
                 NumMacrosRead, TotalNumMacros,
                 ((float)NumMacrosRead/TotalNumMacros * 100));
  if (TotalLexicalDeclContexts)
    std::fprintf(stderr, "  %u/%u lexical declcontexts read (%f%%)\n",
                 NumLexicalDeclContextsRead, TotalLexicalDeclContexts,
                 ((float)NumLexicalDeclContextsRead/TotalLexicalDeclContexts
                  * 100));
  if (TotalVisibleDeclContexts)
    std::fprintf(stderr, "  %u/%u visible declcontexts read (%f%%)\n",
                 NumVisibleDeclContextsRead, TotalVisibleDeclContexts,
                 ((float)NumVisibleDeclContextsRead/TotalVisibleDeclContexts
                  * 100));
  if (TotalNumMethodPoolEntries)
    std::fprintf(stderr, "  %u/%u method pool entries read (%f%%)\n",
                 NumMethodPoolEntriesRead, TotalNumMethodPoolEntries,
                 ((float)NumMethodPoolEntriesRead/TotalNumMethodPoolEntries
                  * 100));
  if (NumMethodPoolLookups)
    std::fprintf(stderr, "  %u/%u method pool lookups succeeded (%f%%)\n",
                 NumMethodPoolHits, NumMethodPoolLookups,
                 ((float)NumMethodPoolHits/NumMethodPoolLookups * 100.0));
  if (NumMethodPoolTableLookups)
    std::fprintf(stderr, "  %u/%u method pool table lookups succeeded (%f%%)\n",
                 NumMethodPoolTableHits, NumMethodPoolTableLookups,
                 ((float)NumMethodPoolTableHits/NumMethodPoolTableLookups
                  * 100.0));
  if (NumIdentifierLookupHits)
    std::fprintf(stderr,
                 "  %u / %u identifier table lookups succeeded (%f%%)\n",
                 NumIdentifierLookupHits, NumIdentifierLookups,
                 (double)NumIdentifierLookupHits*100.0/NumIdentifierLookups);

  if (GlobalIndex) {
    std::fprintf(stderr, "\n");
    GlobalIndex->printStats();
  }

  std::fprintf(stderr, "\n");
  dump();
  std::fprintf(stderr, "\n");
}

template<typename Key, typename ModuleFile, unsigned InitialCapacity>
LLVM_DUMP_METHOD static void
dumpModuleIDMap(StringRef Name,
                const ContinuousRangeMap<Key, ModuleFile *,
                                         InitialCapacity> &Map) {
  if (Map.begin() == Map.end())
    return;

  using MapType = ContinuousRangeMap<Key, ModuleFile *, InitialCapacity>;

  llvm::errs() << Name << ":\n";
  for (typename MapType::const_iterator I = Map.begin(), IEnd = Map.end();
       I != IEnd; ++I)
    llvm::errs() << "  " << (DeclID)I->first << " -> " << I->second->FileName
                 << "\n";
}

LLVM_DUMP_METHOD void ASTReader::dump() {
  llvm::errs() << "*** PCH/ModuleFile Remappings:\n";
  dumpModuleIDMap("Global bit offset map", GlobalBitOffsetsMap);
  dumpModuleIDMap("Global source location entry map", GlobalSLocEntryMap);
  dumpModuleIDMap("Global macro map", GlobalMacroMap);
  dumpModuleIDMap("Global submodule map", GlobalSubmoduleMap);
  dumpModuleIDMap("Global selector map", GlobalSelectorMap);
  dumpModuleIDMap("Global preprocessed entity map",
                  GlobalPreprocessedEntityMap);

  llvm::errs() << "\n*** PCH/Modules Loaded:";
  for (ModuleFile &M : ModuleMgr)
    M.dump();
}

/// Return the amount of memory used by memory buffers, breaking down
/// by heap-backed versus mmap'ed memory.
void ASTReader::getMemoryBufferSizes(MemoryBufferSizes &sizes) const {
  for (ModuleFile &I : ModuleMgr) {
    if (llvm::MemoryBuffer *buf = I.Buffer) {
      size_t bytes = buf->getBufferSize();
      switch (buf->getBufferKind()) {
        case llvm::MemoryBuffer::MemoryBuffer_Malloc:
          sizes.malloc_bytes += bytes;
          break;
        case llvm::MemoryBuffer::MemoryBuffer_MMap:
          sizes.mmap_bytes += bytes;
          break;
      }
    }
  }
}

void ASTReader::InitializeSema(Sema &S) {
  SemaObj = &S;
  S.addExternalSource(this);

  // Makes sure any declarations that were deserialized "too early"
  // still get added to the identifier's declaration chains.
  for (GlobalDeclID ID : PreloadedDeclIDs) {
    NamedDecl *D = cast<NamedDecl>(GetDecl(ID));
    pushExternalDeclIntoScope(D, D->getDeclName());
  }
  PreloadedDeclIDs.clear();

  // FIXME: What happens if these are changed by a module import?
  if (!FPPragmaOptions.empty()) {
    assert(FPPragmaOptions.size() == 1 && "Wrong number of FP_PRAGMA_OPTIONS");
    FPOptionsOverride NewOverrides =
        FPOptionsOverride::getFromOpaqueInt(FPPragmaOptions[0]);
    SemaObj->CurFPFeatures =
        NewOverrides.applyOverrides(SemaObj->getLangOpts());
  }

  SemaObj->OpenCLFeatures = OpenCLExtensions;

  UpdateSema();
}

void ASTReader::UpdateSema() {
  assert(SemaObj && "no Sema to update");

  // Load the offsets of the declarations that Sema references.
  // They will be lazily deserialized when needed.
  if (!SemaDeclRefs.empty()) {
    assert(SemaDeclRefs.size() % 3 == 0);
    for (unsigned I = 0; I != SemaDeclRefs.size(); I += 3) {
      if (!SemaObj->StdNamespace)
        SemaObj->StdNamespace = SemaDeclRefs[I].getRawValue();
      if (!SemaObj->StdBadAlloc)
        SemaObj->StdBadAlloc = SemaDeclRefs[I + 1].getRawValue();
      if (!SemaObj->StdAlignValT)
        SemaObj->StdAlignValT = SemaDeclRefs[I + 2].getRawValue();
    }
    SemaDeclRefs.clear();
  }

  // Update the state of pragmas. Use the same API as if we had encountered the
  // pragma in the source.
  if(OptimizeOffPragmaLocation.isValid())
    SemaObj->ActOnPragmaOptimize(/* On = */ false, OptimizeOffPragmaLocation);
  if (PragmaMSStructState != -1)
    SemaObj->ActOnPragmaMSStruct((PragmaMSStructKind)PragmaMSStructState);
  if (PointersToMembersPragmaLocation.isValid()) {
    SemaObj->ActOnPragmaMSPointersToMembers(
        (LangOptions::PragmaMSPointersToMembersKind)
            PragmaMSPointersToMembersState,
        PointersToMembersPragmaLocation);
  }
  SemaObj->CUDA().ForceHostDeviceDepth = ForceHostDeviceDepth;

  if (PragmaAlignPackCurrentValue) {
    // The bottom of the stack might have a default value. It must be adjusted
    // to the current value to ensure that the packing state is preserved after
    // popping entries that were included/imported from a PCH/module.
    bool DropFirst = false;
    if (!PragmaAlignPackStack.empty() &&
        PragmaAlignPackStack.front().Location.isInvalid()) {
      assert(PragmaAlignPackStack.front().Value ==
                 SemaObj->AlignPackStack.DefaultValue &&
             "Expected a default alignment value");
      SemaObj->AlignPackStack.Stack.emplace_back(
          PragmaAlignPackStack.front().SlotLabel,
          SemaObj->AlignPackStack.CurrentValue,
          SemaObj->AlignPackStack.CurrentPragmaLocation,
          PragmaAlignPackStack.front().PushLocation);
      DropFirst = true;
    }
    for (const auto &Entry :
         llvm::ArrayRef(PragmaAlignPackStack).drop_front(DropFirst ? 1 : 0)) {
      SemaObj->AlignPackStack.Stack.emplace_back(
          Entry.SlotLabel, Entry.Value, Entry.Location, Entry.PushLocation);
    }
    if (PragmaAlignPackCurrentLocation.isInvalid()) {
      assert(*PragmaAlignPackCurrentValue ==
                 SemaObj->AlignPackStack.DefaultValue &&
             "Expected a default align and pack value");
      // Keep the current values.
    } else {
      SemaObj->AlignPackStack.CurrentValue = *PragmaAlignPackCurrentValue;
      SemaObj->AlignPackStack.CurrentPragmaLocation =
          PragmaAlignPackCurrentLocation;
    }
  }
  if (FpPragmaCurrentValue) {
    // The bottom of the stack might have a default value. It must be adjusted
    // to the current value to ensure that fp-pragma state is preserved after
    // popping entries that were included/imported from a PCH/module.
    bool DropFirst = false;
    if (!FpPragmaStack.empty() && FpPragmaStack.front().Location.isInvalid()) {
      assert(FpPragmaStack.front().Value ==
                 SemaObj->FpPragmaStack.DefaultValue &&
             "Expected a default pragma float_control value");
      SemaObj->FpPragmaStack.Stack.emplace_back(
          FpPragmaStack.front().SlotLabel, SemaObj->FpPragmaStack.CurrentValue,
          SemaObj->FpPragmaStack.CurrentPragmaLocation,
          FpPragmaStack.front().PushLocation);
      DropFirst = true;
    }
    for (const auto &Entry :
         llvm::ArrayRef(FpPragmaStack).drop_front(DropFirst ? 1 : 0))
      SemaObj->FpPragmaStack.Stack.emplace_back(
          Entry.SlotLabel, Entry.Value, Entry.Location, Entry.PushLocation);
    if (FpPragmaCurrentLocation.isInvalid()) {
      assert(*FpPragmaCurrentValue == SemaObj->FpPragmaStack.DefaultValue &&
             "Expected a default pragma float_control value");
      // Keep the current values.
    } else {
      SemaObj->FpPragmaStack.CurrentValue = *FpPragmaCurrentValue;
      SemaObj->FpPragmaStack.CurrentPragmaLocation = FpPragmaCurrentLocation;
    }
  }

  // For non-modular AST files, restore visiblity of modules.
  for (auto &Import : PendingImportedModulesSema) {
    if (Import.ImportLoc.isInvalid())
      continue;
    if (Module *Imported = getSubmodule(Import.ID)) {
      SemaObj->makeModuleVisible(Imported, Import.ImportLoc);
    }
  }
  PendingImportedModulesSema.clear();
}

IdentifierInfo *ASTReader::get(StringRef Name) {
  // Note that we are loading an identifier.
  Deserializing AnIdentifier(this);

  IdentifierLookupVisitor Visitor(Name, /*PriorGeneration=*/0,
                                  NumIdentifierLookups,
                                  NumIdentifierLookupHits);

  // We don't need to do identifier table lookups in C++ modules (we preload
  // all interesting declarations, and don't need to use the scope for name
  // lookups). Perform the lookup in PCH files, though, since we don't build
  // a complete initial identifier table if we're carrying on from a PCH.
  if (PP.getLangOpts().CPlusPlus) {
    for (auto *F : ModuleMgr.pch_modules())
      if (Visitor(*F))
        break;
  } else {
    // If there is a global index, look there first to determine which modules
    // provably do not have any results for this identifier.
    GlobalModuleIndex::HitSet Hits;
    GlobalModuleIndex::HitSet *HitsPtr = nullptr;
    if (!loadGlobalIndex()) {
      if (GlobalIndex->lookupIdentifier(Name, Hits)) {
        HitsPtr = &Hits;
      }
    }

    ModuleMgr.visit(Visitor, HitsPtr);
  }

  IdentifierInfo *II = Visitor.getIdentifierInfo();
  markIdentifierUpToDate(II);
  return II;
}

namespace clang {

  /// An identifier-lookup iterator that enumerates all of the
  /// identifiers stored within a set of AST files.
  class ASTIdentifierIterator : public IdentifierIterator {
    /// The AST reader whose identifiers are being enumerated.
    const ASTReader &Reader;

    /// The current index into the chain of AST files stored in
    /// the AST reader.
    unsigned Index;

    /// The current position within the identifier lookup table
    /// of the current AST file.
    ASTIdentifierLookupTable::key_iterator Current;

    /// The end position within the identifier lookup table of
    /// the current AST file.
    ASTIdentifierLookupTable::key_iterator End;

    /// Whether to skip any modules in the ASTReader.
    bool SkipModules;

  public:
    explicit ASTIdentifierIterator(const ASTReader &Reader,
                                   bool SkipModules = false);

    StringRef Next() override;
  };

} // namespace clang

ASTIdentifierIterator::ASTIdentifierIterator(const ASTReader &Reader,
                                             bool SkipModules)
    : Reader(Reader), Index(Reader.ModuleMgr.size()), SkipModules(SkipModules) {
}

StringRef ASTIdentifierIterator::Next() {
  while (Current == End) {
    // If we have exhausted all of our AST files, we're done.
    if (Index == 0)
      return StringRef();

    --Index;
    ModuleFile &F = Reader.ModuleMgr[Index];
    if (SkipModules && F.isModule())
      continue;

    ASTIdentifierLookupTable *IdTable =
        (ASTIdentifierLookupTable *)F.IdentifierLookupTable;
    Current = IdTable->key_begin();
    End = IdTable->key_end();
  }

  // We have any identifiers remaining in the current AST file; return
  // the next one.
  StringRef Result = *Current;
  ++Current;
  return Result;
}

namespace {

/// A utility for appending two IdentifierIterators.
class ChainedIdentifierIterator : public IdentifierIterator {
  std::unique_ptr<IdentifierIterator> Current;
  std::unique_ptr<IdentifierIterator> Queued;

public:
  ChainedIdentifierIterator(std::unique_ptr<IdentifierIterator> First,
                            std::unique_ptr<IdentifierIterator> Second)
      : Current(std::move(First)), Queued(std::move(Second)) {}

  StringRef Next() override {
    if (!Current)
      return StringRef();

    StringRef result = Current->Next();
    if (!result.empty())
      return result;

    // Try the queued iterator, which may itself be empty.
    Current.reset();
    std::swap(Current, Queued);
    return Next();
  }
};

} // namespace

IdentifierIterator *ASTReader::getIdentifiers() {
  if (!loadGlobalIndex()) {
    std::unique_ptr<IdentifierIterator> ReaderIter(
        new ASTIdentifierIterator(*this, /*SkipModules=*/true));
    std::unique_ptr<IdentifierIterator> ModulesIter(
        GlobalIndex->createIdentifierIterator());
    return new ChainedIdentifierIterator(std::move(ReaderIter),
                                         std::move(ModulesIter));
  }

  return new ASTIdentifierIterator(*this);
}

namespace clang {
namespace serialization {

  class ReadMethodPoolVisitor {
    ASTReader &Reader;
    Selector Sel;
    unsigned PriorGeneration;
    unsigned InstanceBits = 0;
    unsigned FactoryBits = 0;
    bool InstanceHasMoreThanOneDecl = false;
    bool FactoryHasMoreThanOneDecl = false;
    SmallVector<ObjCMethodDecl *, 4> InstanceMethods;
    SmallVector<ObjCMethodDecl *, 4> FactoryMethods;

  public:
    ReadMethodPoolVisitor(ASTReader &Reader, Selector Sel,
                          unsigned PriorGeneration)
        : Reader(Reader), Sel(Sel), PriorGeneration(PriorGeneration) {}

    bool operator()(ModuleFile &M) {
      if (!M.SelectorLookupTable)
        return false;

      // If we've already searched this module file, skip it now.
      if (M.Generation <= PriorGeneration)
        return true;

      ++Reader.NumMethodPoolTableLookups;
      ASTSelectorLookupTable *PoolTable
        = (ASTSelectorLookupTable*)M.SelectorLookupTable;
      ASTSelectorLookupTable::iterator Pos = PoolTable->find(Sel);
      if (Pos == PoolTable->end())
        return false;

      ++Reader.NumMethodPoolTableHits;
      ++Reader.NumSelectorsRead;
      // FIXME: Not quite happy with the statistics here. We probably should
      // disable this tracking when called via LoadSelector.
      // Also, should entries without methods count as misses?
      ++Reader.NumMethodPoolEntriesRead;
      ASTSelectorLookupTrait::data_type Data = *Pos;
      if (Reader.DeserializationListener)
        Reader.DeserializationListener->SelectorRead(Data.ID, Sel);

      // Append methods in the reverse order, so that later we can process them
      // in the order they appear in the source code by iterating through
      // the vector in the reverse order.
      InstanceMethods.append(Data.Instance.rbegin(), Data.Instance.rend());
      FactoryMethods.append(Data.Factory.rbegin(), Data.Factory.rend());
      InstanceBits = Data.InstanceBits;
      FactoryBits = Data.FactoryBits;
      InstanceHasMoreThanOneDecl = Data.InstanceHasMoreThanOneDecl;
      FactoryHasMoreThanOneDecl = Data.FactoryHasMoreThanOneDecl;
      return false;
    }

    /// Retrieve the instance methods found by this visitor.
    ArrayRef<ObjCMethodDecl *> getInstanceMethods() const {
      return InstanceMethods;
    }

    /// Retrieve the instance methods found by this visitor.
    ArrayRef<ObjCMethodDecl *> getFactoryMethods() const {
      return FactoryMethods;
    }

    unsigned getInstanceBits() const { return InstanceBits; }
    unsigned getFactoryBits() const { return FactoryBits; }

    bool instanceHasMoreThanOneDecl() const {
      return InstanceHasMoreThanOneDecl;
    }

    bool factoryHasMoreThanOneDecl() const { return FactoryHasMoreThanOneDecl; }
  };

} // namespace serialization
} // namespace clang

/// Add the given set of methods to the method list.
static void addMethodsToPool(Sema &S, ArrayRef<ObjCMethodDecl *> Methods,
                             ObjCMethodList &List) {
  for (ObjCMethodDecl *M : llvm::reverse(Methods))
    S.ObjC().addMethodToGlobalList(&List, M);
}

void ASTReader::ReadMethodPool(Selector Sel) {
  // Get the selector generation and update it to the current generation.
  unsigned &Generation = SelectorGeneration[Sel];
  unsigned PriorGeneration = Generation;
  Generation = getGeneration();
  SelectorOutOfDate[Sel] = false;

  // Search for methods defined with this selector.
  ++NumMethodPoolLookups;
  ReadMethodPoolVisitor Visitor(*this, Sel, PriorGeneration);
  ModuleMgr.visit(Visitor);

  if (Visitor.getInstanceMethods().empty() &&
      Visitor.getFactoryMethods().empty())
    return;

  ++NumMethodPoolHits;

  if (!getSema())
    return;

  Sema &S = *getSema();
  SemaObjC::GlobalMethodPool::iterator Pos =
      S.ObjC()
          .MethodPool
          .insert(std::make_pair(Sel, SemaObjC::GlobalMethodPool::Lists()))
          .first;

  Pos->second.first.setBits(Visitor.getInstanceBits());
  Pos->second.first.setHasMoreThanOneDecl(Visitor.instanceHasMoreThanOneDecl());
  Pos->second.second.setBits(Visitor.getFactoryBits());
  Pos->second.second.setHasMoreThanOneDecl(Visitor.factoryHasMoreThanOneDecl());

  // Add methods to the global pool *after* setting hasMoreThanOneDecl, since
  // when building a module we keep every method individually and may need to
  // update hasMoreThanOneDecl as we add the methods.
  addMethodsToPool(S, Visitor.getInstanceMethods(), Pos->second.first);
  addMethodsToPool(S, Visitor.getFactoryMethods(), Pos->second.second);
}

void ASTReader::updateOutOfDateSelector(Selector Sel) {
  if (SelectorOutOfDate[Sel])
    ReadMethodPool(Sel);
}

void ASTReader::ReadKnownNamespaces(
                          SmallVectorImpl<NamespaceDecl *> &Namespaces) {
  Namespaces.clear();

  for (unsigned I = 0, N = KnownNamespaces.size(); I != N; ++I) {
    if (NamespaceDecl *Namespace
                = dyn_cast_or_null<NamespaceDecl>(GetDecl(KnownNamespaces[I])))
      Namespaces.push_back(Namespace);
  }
}

void ASTReader::ReadUndefinedButUsed(
    llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) {
  for (unsigned Idx = 0, N = UndefinedButUsed.size(); Idx != N;) {
    UndefinedButUsedDecl &U = UndefinedButUsed[Idx++];
    NamedDecl *D = cast<NamedDecl>(GetDecl(U.ID));
    SourceLocation Loc = SourceLocation::getFromRawEncoding(U.RawLoc);
    Undefined.insert(std::make_pair(D, Loc));
  }
  UndefinedButUsed.clear();
}

void ASTReader::ReadMismatchingDeleteExpressions(llvm::MapVector<
    FieldDecl *, llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &
                                                     Exprs) {
  for (unsigned Idx = 0, N = DelayedDeleteExprs.size(); Idx != N;) {
    FieldDecl *FD =
        cast<FieldDecl>(GetDecl(GlobalDeclID(DelayedDeleteExprs[Idx++])));
    uint64_t Count = DelayedDeleteExprs[Idx++];
    for (uint64_t C = 0; C < Count; ++C) {
      SourceLocation DeleteLoc =
          SourceLocation::getFromRawEncoding(DelayedDeleteExprs[Idx++]);
      const bool IsArrayForm = DelayedDeleteExprs[Idx++];
      Exprs[FD].push_back(std::make_pair(DeleteLoc, IsArrayForm));
    }
  }
}

void ASTReader::ReadTentativeDefinitions(
                  SmallVectorImpl<VarDecl *> &TentativeDefs) {
  for (unsigned I = 0, N = TentativeDefinitions.size(); I != N; ++I) {
    VarDecl *Var = dyn_cast_or_null<VarDecl>(GetDecl(TentativeDefinitions[I]));
    if (Var)
      TentativeDefs.push_back(Var);
  }
  TentativeDefinitions.clear();
}

void ASTReader::ReadUnusedFileScopedDecls(
                               SmallVectorImpl<const DeclaratorDecl *> &Decls) {
  for (unsigned I = 0, N = UnusedFileScopedDecls.size(); I != N; ++I) {
    DeclaratorDecl *D
      = dyn_cast_or_null<DeclaratorDecl>(GetDecl(UnusedFileScopedDecls[I]));
    if (D)
      Decls.push_back(D);
  }
  UnusedFileScopedDecls.clear();
}

void ASTReader::ReadDelegatingConstructors(
                                 SmallVectorImpl<CXXConstructorDecl *> &Decls) {
  for (unsigned I = 0, N = DelegatingCtorDecls.size(); I != N; ++I) {
    CXXConstructorDecl *D
      = dyn_cast_or_null<CXXConstructorDecl>(GetDecl(DelegatingCtorDecls[I]));
    if (D)
      Decls.push_back(D);
  }
  DelegatingCtorDecls.clear();
}

void ASTReader::ReadExtVectorDecls(SmallVectorImpl<TypedefNameDecl *> &Decls) {
  for (unsigned I = 0, N = ExtVectorDecls.size(); I != N; ++I) {
    TypedefNameDecl *D
      = dyn_cast_or_null<TypedefNameDecl>(GetDecl(ExtVectorDecls[I]));
    if (D)
      Decls.push_back(D);
  }
  ExtVectorDecls.clear();
}

void ASTReader::ReadUnusedLocalTypedefNameCandidates(
    llvm::SmallSetVector<const TypedefNameDecl *, 4> &Decls) {
  for (unsigned I = 0, N = UnusedLocalTypedefNameCandidates.size(); I != N;
       ++I) {
    TypedefNameDecl *D = dyn_cast_or_null<TypedefNameDecl>(
        GetDecl(UnusedLocalTypedefNameCandidates[I]));
    if (D)
      Decls.insert(D);
  }
  UnusedLocalTypedefNameCandidates.clear();
}

void ASTReader::ReadDeclsToCheckForDeferredDiags(
    llvm::SmallSetVector<Decl *, 4> &Decls) {
  for (auto I : DeclsToCheckForDeferredDiags) {
    auto *D = dyn_cast_or_null<Decl>(GetDecl(I));
    if (D)
      Decls.insert(D);
  }
  DeclsToCheckForDeferredDiags.clear();
}

void ASTReader::ReadReferencedSelectors(
       SmallVectorImpl<std::pair<Selector, SourceLocation>> &Sels) {
  if (ReferencedSelectorsData.empty())
    return;

  // If there are @selector references added them to its pool. This is for
  // implementation of -Wselector.
  unsigned int DataSize = ReferencedSelectorsData.size()-1;
  unsigned I = 0;
  while (I < DataSize) {
    Selector Sel = DecodeSelector(ReferencedSelectorsData[I++]);
    SourceLocation SelLoc
      = SourceLocation::getFromRawEncoding(ReferencedSelectorsData[I++]);
    Sels.push_back(std::make_pair(Sel, SelLoc));
  }
  ReferencedSelectorsData.clear();
}

void ASTReader::ReadWeakUndeclaredIdentifiers(
       SmallVectorImpl<std::pair<IdentifierInfo *, WeakInfo>> &WeakIDs) {
  if (WeakUndeclaredIdentifiers.empty())
    return;

  for (unsigned I = 0, N = WeakUndeclaredIdentifiers.size(); I < N; /*none*/) {
    IdentifierInfo *WeakId
      = DecodeIdentifierInfo(WeakUndeclaredIdentifiers[I++]);
    IdentifierInfo *AliasId
      = DecodeIdentifierInfo(WeakUndeclaredIdentifiers[I++]);
    SourceLocation Loc =
        SourceLocation::getFromRawEncoding(WeakUndeclaredIdentifiers[I++]);
    WeakInfo WI(AliasId, Loc);
    WeakIDs.push_back(std::make_pair(WeakId, WI));
  }
  WeakUndeclaredIdentifiers.clear();
}

void ASTReader::ReadUsedVTables(SmallVectorImpl<ExternalVTableUse> &VTables) {
  for (unsigned Idx = 0, N = VTableUses.size(); Idx < N; /* In loop */) {
    ExternalVTableUse VT;
    VTableUse &TableInfo = VTableUses[Idx++];
    VT.Record = dyn_cast_or_null<CXXRecordDecl>(GetDecl(TableInfo.ID));
    VT.Location = SourceLocation::getFromRawEncoding(TableInfo.RawLoc);
    VT.DefinitionRequired = TableInfo.Used;
    VTables.push_back(VT);
  }

  VTableUses.clear();
}

void ASTReader::ReadPendingInstantiations(
       SmallVectorImpl<std::pair<ValueDecl *, SourceLocation>> &Pending) {
  for (unsigned Idx = 0, N = PendingInstantiations.size(); Idx < N;) {
    PendingInstantiation &Inst = PendingInstantiations[Idx++];
    ValueDecl *D = cast<ValueDecl>(GetDecl(Inst.ID));
    SourceLocation Loc = SourceLocation::getFromRawEncoding(Inst.RawLoc);

    Pending.push_back(std::make_pair(D, Loc));
  }
  PendingInstantiations.clear();
}

void ASTReader::ReadLateParsedTemplates(
    llvm::MapVector<const FunctionDecl *, std::unique_ptr<LateParsedTemplate>>
        &LPTMap) {
  for (auto &LPT : LateParsedTemplates) {
    ModuleFile *FMod = LPT.first;
    RecordDataImpl &LateParsed = LPT.second;
    for (unsigned Idx = 0, N = LateParsed.size(); Idx < N;
         /* In loop */) {
      FunctionDecl *FD = ReadDeclAs<FunctionDecl>(*FMod, LateParsed, Idx);

      auto LT = std::make_unique<LateParsedTemplate>();
      LT->D = ReadDecl(*FMod, LateParsed, Idx);
      LT->FPO = FPOptions::getFromOpaqueInt(LateParsed[Idx++]);

      ModuleFile *F = getOwningModuleFile(LT->D);
      assert(F && "No module");

      unsigned TokN = LateParsed[Idx++];
      LT->Toks.reserve(TokN);
      for (unsigned T = 0; T < TokN; ++T)
        LT->Toks.push_back(ReadToken(*F, LateParsed, Idx));

      LPTMap.insert(std::make_pair(FD, std::move(LT)));
    }
  }

  LateParsedTemplates.clear();
}

void ASTReader::AssignedLambdaNumbering(const CXXRecordDecl *Lambda) {
  if (Lambda->getLambdaContextDecl()) {
    // Keep track of this lambda so it can be merged with another lambda that
    // is loaded later.
    LambdaDeclarationsForMerging.insert(
        {{Lambda->getLambdaContextDecl()->getCanonicalDecl(),
          Lambda->getLambdaIndexInContext()},
         const_cast<CXXRecordDecl *>(Lambda)});
  }
}

void ASTReader::LoadSelector(Selector Sel) {
  // It would be complicated to avoid reading the methods anyway. So don't.
  ReadMethodPool(Sel);
}

void ASTReader::SetIdentifierInfo(IdentifierID ID, IdentifierInfo *II) {
  assert(ID && "Non-zero identifier ID required");
  unsigned Index = translateIdentifierIDToIndex(ID).second;
  assert(Index < IdentifiersLoaded.size() && "identifier ID out of range");
  IdentifiersLoaded[Index] = II;
  if (DeserializationListener)
    DeserializationListener->IdentifierRead(ID, II);
}

/// Set the globally-visible declarations associated with the given
/// identifier.
///
/// If the AST reader is currently in a state where the given declaration IDs
/// cannot safely be resolved, they are queued until it is safe to resolve
/// them.
///
/// \param II an IdentifierInfo that refers to one or more globally-visible
/// declarations.
///
/// \param DeclIDs the set of declaration IDs with the name @p II that are
/// visible at global scope.
///
/// \param Decls if non-null, this vector will be populated with the set of
/// deserialized declarations. These declarations will not be pushed into
/// scope.
void ASTReader::SetGloballyVisibleDecls(
    IdentifierInfo *II, const SmallVectorImpl<GlobalDeclID> &DeclIDs,
    SmallVectorImpl<Decl *> *Decls) {
  if (NumCurrentElementsDeserializing && !Decls) {
    PendingIdentifierInfos[II].append(DeclIDs.begin(), DeclIDs.end());
    return;
  }

  for (unsigned I = 0, N = DeclIDs.size(); I != N; ++I) {
    if (!SemaObj) {
      // Queue this declaration so that it will be added to the
      // translation unit scope and identifier's declaration chain
      // once a Sema object is known.
      PreloadedDeclIDs.push_back(DeclIDs[I]);
      continue;
    }

    NamedDecl *D = cast<NamedDecl>(GetDecl(DeclIDs[I]));

    // If we're simply supposed to record the declarations, do so now.
    if (Decls) {
      Decls->push_back(D);
      continue;
    }

    // Introduce this declaration into the translation-unit scope
    // and add it to the declaration chain for this identifier, so
    // that (unqualified) name lookup will find it.
    pushExternalDeclIntoScope(D, II);
  }
}

std::pair<ModuleFile *, unsigned>
ASTReader::translateIdentifierIDToIndex(IdentifierID ID) const {
  if (ID == 0)
    return {nullptr, 0};

  unsigned ModuleFileIndex = ID >> 32;
  unsigned LocalID = ID & llvm::maskTrailingOnes<IdentifierID>(32);

  assert(ModuleFileIndex && "not translating loaded IdentifierID?");
  assert(getModuleManager().size() > ModuleFileIndex - 1);

  ModuleFile &MF = getModuleManager()[ModuleFileIndex - 1];
  assert(LocalID < MF.LocalNumIdentifiers);
  return {&MF, MF.BaseIdentifierID + LocalID};
}

IdentifierInfo *ASTReader::DecodeIdentifierInfo(IdentifierID ID) {
  if (ID == 0)
    return nullptr;

  if (IdentifiersLoaded.empty()) {
    Error("no identifier table in AST file");
    return nullptr;
  }

  auto [M, Index] = translateIdentifierIDToIndex(ID);
  if (!IdentifiersLoaded[Index]) {
    assert(M != nullptr && "Untranslated Identifier ID?");
    assert(Index >= M->BaseIdentifierID);
    unsigned LocalIndex = Index - M->BaseIdentifierID;
    const unsigned char *Data =
        M->IdentifierTableData + M->IdentifierOffsets[LocalIndex];

    ASTIdentifierLookupTrait Trait(*this, *M);
    auto KeyDataLen = Trait.ReadKeyDataLength(Data);
    auto Key = Trait.ReadKey(Data, KeyDataLen.first);
    auto &II = PP.getIdentifierTable().get(Key);
    IdentifiersLoaded[Index] = &II;
    markIdentifierFromAST(*this,  II);
    if (DeserializationListener)
      DeserializationListener->IdentifierRead(ID, &II);
  }

  return IdentifiersLoaded[Index];
}

IdentifierInfo *ASTReader::getLocalIdentifier(ModuleFile &M, uint64_t LocalID) {
  return DecodeIdentifierInfo(getGlobalIdentifierID(M, LocalID));
}

IdentifierID ASTReader::getGlobalIdentifierID(ModuleFile &M, uint64_t LocalID) {
  if (LocalID < NUM_PREDEF_IDENT_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  unsigned ModuleFileIndex = LocalID >> 32;
  LocalID &= llvm::maskTrailingOnes<IdentifierID>(32);
  ModuleFile *MF =
      ModuleFileIndex ? M.TransitiveImports[ModuleFileIndex - 1] : &M;
  assert(MF && "malformed identifier ID encoding?");

  if (!ModuleFileIndex)
    LocalID -= NUM_PREDEF_IDENT_IDS;

  return ((IdentifierID)(MF->Index + 1) << 32) | LocalID;
}

MacroInfo *ASTReader::getMacro(MacroID ID) {
  if (ID == 0)
    return nullptr;

  if (MacrosLoaded.empty()) {
    Error("no macro table in AST file");
    return nullptr;
  }

  ID -= NUM_PREDEF_MACRO_IDS;
  if (!MacrosLoaded[ID]) {
    GlobalMacroMapType::iterator I
      = GlobalMacroMap.find(ID + NUM_PREDEF_MACRO_IDS);
    assert(I != GlobalMacroMap.end() && "Corrupted global macro map");
    ModuleFile *M = I->second;
    unsigned Index = ID - M->BaseMacroID;
    MacrosLoaded[ID] =
        ReadMacroRecord(*M, M->MacroOffsetsBase + M->MacroOffsets[Index]);

    if (DeserializationListener)
      DeserializationListener->MacroRead(ID + NUM_PREDEF_MACRO_IDS,
                                         MacrosLoaded[ID]);
  }

  return MacrosLoaded[ID];
}

MacroID ASTReader::getGlobalMacroID(ModuleFile &M, unsigned LocalID) {
  if (LocalID < NUM_PREDEF_MACRO_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.MacroRemap.find(LocalID - NUM_PREDEF_MACRO_IDS);
  assert(I != M.MacroRemap.end() && "Invalid index into macro index remap");

  return LocalID + I->second;
}

serialization::SubmoduleID
ASTReader::getGlobalSubmoduleID(ModuleFile &M, unsigned LocalID) const {
  if (LocalID < NUM_PREDEF_SUBMODULE_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.SubmoduleRemap.find(LocalID - NUM_PREDEF_SUBMODULE_IDS);
  assert(I != M.SubmoduleRemap.end()
         && "Invalid index into submodule index remap");

  return LocalID + I->second;
}

Module *ASTReader::getSubmodule(SubmoduleID GlobalID) {
  if (GlobalID < NUM_PREDEF_SUBMODULE_IDS) {
    assert(GlobalID == 0 && "Unhandled global submodule ID");
    return nullptr;
  }

  if (GlobalID > SubmodulesLoaded.size()) {
    Error("submodule ID out of range in AST file");
    return nullptr;
  }

  return SubmodulesLoaded[GlobalID - NUM_PREDEF_SUBMODULE_IDS];
}

Module *ASTReader::getModule(unsigned ID) {
  return getSubmodule(ID);
}

ModuleFile *ASTReader::getLocalModuleFile(ModuleFile &M, unsigned ID) const {
  if (ID & 1) {
    // It's a module, look it up by submodule ID.
    auto I = GlobalSubmoduleMap.find(getGlobalSubmoduleID(M, ID >> 1));
    return I == GlobalSubmoduleMap.end() ? nullptr : I->second;
  } else {
    // It's a prefix (preamble, PCH, ...). Look it up by index.
    unsigned IndexFromEnd = ID >> 1;
    assert(IndexFromEnd && "got reference to unknown module file");
    return getModuleManager().pch_modules().end()[-IndexFromEnd];
  }
}

unsigned ASTReader::getModuleFileID(ModuleFile *M) {
  if (!M)
    return 1;

  // For a file representing a module, use the submodule ID of the top-level
  // module as the file ID. For any other kind of file, the number of such
  // files loaded beforehand will be the same on reload.
  // FIXME: Is this true even if we have an explicit module file and a PCH?
  if (M->isModule())
    return ((M->BaseSubmoduleID + NUM_PREDEF_SUBMODULE_IDS) << 1) | 1;

  auto PCHModules = getModuleManager().pch_modules();
  auto I = llvm::find(PCHModules, M);
  assert(I != PCHModules.end() && "emitting reference to unknown file");
  return (I - PCHModules.end()) << 1;
}

std::optional<ASTSourceDescriptor> ASTReader::getSourceDescriptor(unsigned ID) {
  if (Module *M = getSubmodule(ID))
    return ASTSourceDescriptor(*M);

  // If there is only a single PCH, return it instead.
  // Chained PCH are not supported.
  const auto &PCHChain = ModuleMgr.pch_modules();
  if (std::distance(std::begin(PCHChain), std::end(PCHChain))) {
    ModuleFile &MF = ModuleMgr.getPrimaryModule();
    StringRef ModuleName = llvm::sys::path::filename(MF.OriginalSourceFileName);
    StringRef FileName = llvm::sys::path::filename(MF.FileName);
    return ASTSourceDescriptor(ModuleName,
                               llvm::sys::path::parent_path(MF.FileName),
                               FileName, MF.Signature);
  }
  return std::nullopt;
}

ExternalASTSource::ExtKind ASTReader::hasExternalDefinitions(const Decl *FD) {
  auto I = DefinitionSource.find(FD);
  if (I == DefinitionSource.end())
    return EK_ReplyHazy;
  return I->second ? EK_Never : EK_Always;
}

Selector ASTReader::getLocalSelector(ModuleFile &M, unsigned LocalID) {
  return DecodeSelector(getGlobalSelectorID(M, LocalID));
}

Selector ASTReader::DecodeSelector(serialization::SelectorID ID) {
  if (ID == 0)
    return Selector();

  if (ID > SelectorsLoaded.size()) {
    Error("selector ID out of range in AST file");
    return Selector();
  }

  if (SelectorsLoaded[ID - 1].getAsOpaquePtr() == nullptr) {
    // Load this selector from the selector table.
    GlobalSelectorMapType::iterator I = GlobalSelectorMap.find(ID);
    assert(I != GlobalSelectorMap.end() && "Corrupted global selector map");
    ModuleFile &M = *I->second;
    ASTSelectorLookupTrait Trait(*this, M);
    unsigned Idx = ID - M.BaseSelectorID - NUM_PREDEF_SELECTOR_IDS;
    SelectorsLoaded[ID - 1] =
      Trait.ReadKey(M.SelectorLookupTableData + M.SelectorOffsets[Idx], 0);
    if (DeserializationListener)
      DeserializationListener->SelectorRead(ID, SelectorsLoaded[ID - 1]);
  }

  return SelectorsLoaded[ID - 1];
}

Selector ASTReader::GetExternalSelector(serialization::SelectorID ID) {
  return DecodeSelector(ID);
}

uint32_t ASTReader::GetNumExternalSelectors() {
  // ID 0 (the null selector) is considered an external selector.
  return getTotalNumSelectors() + 1;
}

serialization::SelectorID
ASTReader::getGlobalSelectorID(ModuleFile &M, unsigned LocalID) const {
  if (LocalID < NUM_PREDEF_SELECTOR_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.SelectorRemap.find(LocalID - NUM_PREDEF_SELECTOR_IDS);
  assert(I != M.SelectorRemap.end()
         && "Invalid index into selector index remap");

  return LocalID + I->second;
}

DeclarationNameLoc
ASTRecordReader::readDeclarationNameLoc(DeclarationName Name) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    return DeclarationNameLoc::makeNamedTypeLoc(readTypeSourceInfo());

  case DeclarationName::CXXOperatorName:
    return DeclarationNameLoc::makeCXXOperatorNameLoc(readSourceRange());

  case DeclarationName::CXXLiteralOperatorName:
    return DeclarationNameLoc::makeCXXLiteralOperatorNameLoc(
        readSourceLocation());

  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    break;
  }
  return DeclarationNameLoc();
}

DeclarationNameInfo ASTRecordReader::readDeclarationNameInfo() {
  DeclarationNameInfo NameInfo;
  NameInfo.setName(readDeclarationName());
  NameInfo.setLoc(readSourceLocation());
  NameInfo.setInfo(readDeclarationNameLoc(NameInfo.getName()));
  return NameInfo;
}

TypeCoupledDeclRefInfo ASTRecordReader::readTypeCoupledDeclRefInfo() {
  return TypeCoupledDeclRefInfo(readDeclAs<ValueDecl>(), readBool());
}

void ASTRecordReader::readQualifierInfo(QualifierInfo &Info) {
  Info.QualifierLoc = readNestedNameSpecifierLoc();
  unsigned NumTPLists = readInt();
  Info.NumTemplParamLists = NumTPLists;
  if (NumTPLists) {
    Info.TemplParamLists =
        new (getContext()) TemplateParameterList *[NumTPLists];
    for (unsigned i = 0; i != NumTPLists; ++i)
      Info.TemplParamLists[i] = readTemplateParameterList();
  }
}

TemplateParameterList *
ASTRecordReader::readTemplateParameterList() {
  SourceLocation TemplateLoc = readSourceLocation();
  SourceLocation LAngleLoc = readSourceLocation();
  SourceLocation RAngleLoc = readSourceLocation();

  unsigned NumParams = readInt();
  SmallVector<NamedDecl *, 16> Params;
  Params.reserve(NumParams);
  while (NumParams--)
    Params.push_back(readDeclAs<NamedDecl>());

  bool HasRequiresClause = readBool();
  Expr *RequiresClause = HasRequiresClause ? readExpr() : nullptr;

  TemplateParameterList *TemplateParams = TemplateParameterList::Create(
      getContext(), TemplateLoc, LAngleLoc, Params, RAngleLoc, RequiresClause);
  return TemplateParams;
}

void ASTRecordReader::readTemplateArgumentList(
                        SmallVectorImpl<TemplateArgument> &TemplArgs,
                        bool Canonicalize) {
  unsigned NumTemplateArgs = readInt();
  TemplArgs.reserve(NumTemplateArgs);
  while (NumTemplateArgs--)
    TemplArgs.push_back(readTemplateArgument(Canonicalize));
}

/// Read a UnresolvedSet structure.
void ASTRecordReader::readUnresolvedSet(LazyASTUnresolvedSet &Set) {
  unsigned NumDecls = readInt();
  Set.reserve(getContext(), NumDecls);
  while (NumDecls--) {
    GlobalDeclID ID = readDeclID();
    AccessSpecifier AS = (AccessSpecifier) readInt();
    Set.addLazyDecl(getContext(), ID, AS);
  }
}

CXXBaseSpecifier
ASTRecordReader::readCXXBaseSpecifier() {
  bool isVirtual = readBool();
  bool isBaseOfClass = readBool();
  AccessSpecifier AS = static_cast<AccessSpecifier>(readInt());
  bool inheritConstructors = readBool();
  TypeSourceInfo *TInfo = readTypeSourceInfo();
  SourceRange Range = readSourceRange();
  SourceLocation EllipsisLoc = readSourceLocation();
  CXXBaseSpecifier Result(Range, isVirtual, isBaseOfClass, AS, TInfo,
                          EllipsisLoc);
  Result.setInheritConstructors(inheritConstructors);
  return Result;
}

CXXCtorInitializer **
ASTRecordReader::readCXXCtorInitializers() {
  ASTContext &Context = getContext();
  unsigned NumInitializers = readInt();
  assert(NumInitializers && "wrote ctor initializers but have no inits");
  auto **CtorInitializers = new (Context) CXXCtorInitializer*[NumInitializers];
  for (unsigned i = 0; i != NumInitializers; ++i) {
    TypeSourceInfo *TInfo = nullptr;
    bool IsBaseVirtual = false;
    FieldDecl *Member = nullptr;
    IndirectFieldDecl *IndirectMember = nullptr;

    CtorInitializerType Type = (CtorInitializerType) readInt();
    switch (Type) {
    case CTOR_INITIALIZER_BASE:
      TInfo = readTypeSourceInfo();
      IsBaseVirtual = readBool();
      break;

    case CTOR_INITIALIZER_DELEGATING:
      TInfo = readTypeSourceInfo();
      break;

     case CTOR_INITIALIZER_MEMBER:
      Member = readDeclAs<FieldDecl>();
      break;

     case CTOR_INITIALIZER_INDIRECT_MEMBER:
      IndirectMember = readDeclAs<IndirectFieldDecl>();
      break;
    }

    SourceLocation MemberOrEllipsisLoc = readSourceLocation();
    Expr *Init = readExpr();
    SourceLocation LParenLoc = readSourceLocation();
    SourceLocation RParenLoc = readSourceLocation();

    CXXCtorInitializer *BOMInit;
    if (Type == CTOR_INITIALIZER_BASE)
      BOMInit = new (Context)
          CXXCtorInitializer(Context, TInfo, IsBaseVirtual, LParenLoc, Init,
                             RParenLoc, MemberOrEllipsisLoc);
    else if (Type == CTOR_INITIALIZER_DELEGATING)
      BOMInit = new (Context)
          CXXCtorInitializer(Context, TInfo, LParenLoc, Init, RParenLoc);
    else if (Member)
      BOMInit = new (Context)
          CXXCtorInitializer(Context, Member, MemberOrEllipsisLoc, LParenLoc,
                             Init, RParenLoc);
    else
      BOMInit = new (Context)
          CXXCtorInitializer(Context, IndirectMember, MemberOrEllipsisLoc,
                             LParenLoc, Init, RParenLoc);

    if (/*IsWritten*/readBool()) {
      unsigned SourceOrder = readInt();
      BOMInit->setSourceOrder(SourceOrder);
    }

    CtorInitializers[i] = BOMInit;
  }

  return CtorInitializers;
}

NestedNameSpecifierLoc
ASTRecordReader::readNestedNameSpecifierLoc() {
  ASTContext &Context = getContext();
  unsigned N = readInt();
  NestedNameSpecifierLocBuilder Builder;
  for (unsigned I = 0; I != N; ++I) {
    auto Kind = readNestedNameSpecifierKind();
    switch (Kind) {
    case NestedNameSpecifier::Identifier: {
      IdentifierInfo *II = readIdentifier();
      SourceRange Range = readSourceRange();
      Builder.Extend(Context, II, Range.getBegin(), Range.getEnd());
      break;
    }

    case NestedNameSpecifier::Namespace: {
      NamespaceDecl *NS = readDeclAs<NamespaceDecl>();
      SourceRange Range = readSourceRange();
      Builder.Extend(Context, NS, Range.getBegin(), Range.getEnd());
      break;
    }

    case NestedNameSpecifier::NamespaceAlias: {
      NamespaceAliasDecl *Alias = readDeclAs<NamespaceAliasDecl>();
      SourceRange Range = readSourceRange();
      Builder.Extend(Context, Alias, Range.getBegin(), Range.getEnd());
      break;
    }

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate: {
      bool Template = readBool();
      TypeSourceInfo *T = readTypeSourceInfo();
      if (!T)
        return NestedNameSpecifierLoc();
      SourceLocation ColonColonLoc = readSourceLocation();

      // FIXME: 'template' keyword location not saved anywhere, so we fake it.
      Builder.Extend(Context,
                     Template? T->getTypeLoc().getBeginLoc() : SourceLocation(),
                     T->getTypeLoc(), ColonColonLoc);
      break;
    }

    case NestedNameSpecifier::Global: {
      SourceLocation ColonColonLoc = readSourceLocation();
      Builder.MakeGlobal(Context, ColonColonLoc);
      break;
    }

    case NestedNameSpecifier::Super: {
      CXXRecordDecl *RD = readDeclAs<CXXRecordDecl>();
      SourceRange Range = readSourceRange();
      Builder.MakeSuper(Context, RD, Range.getBegin(), Range.getEnd());
      break;
    }
    }
  }

  return Builder.getWithLocInContext(Context);
}

SourceRange ASTReader::ReadSourceRange(ModuleFile &F, const RecordData &Record,
                                       unsigned &Idx, LocSeq *Seq) {
  SourceLocation beg = ReadSourceLocation(F, Record, Idx, Seq);
  SourceLocation end = ReadSourceLocation(F, Record, Idx, Seq);
  return SourceRange(beg, end);
}

llvm::BitVector ASTReader::ReadBitVector(const RecordData &Record,
                                         const StringRef Blob) {
  unsigned Count = Record[0];
  const char *Byte = Blob.data();
  llvm::BitVector Ret = llvm::BitVector(Count, false);
  for (unsigned I = 0; I < Count; ++Byte)
    for (unsigned Bit = 0; Bit < 8 && I < Count; ++Bit, ++I)
      if (*Byte & (1 << Bit))
        Ret[I] = true;
  return Ret;
}

/// Read a floating-point value
llvm::APFloat ASTRecordReader::readAPFloat(const llvm::fltSemantics &Sem) {
  return llvm::APFloat(Sem, readAPInt());
}

// Read a string
std::string ASTReader::ReadString(const RecordDataImpl &Record, unsigned &Idx) {
  unsigned Len = Record[Idx++];
  std::string Result(Record.data() + Idx, Record.data() + Idx + Len);
  Idx += Len;
  return Result;
}

std::string ASTReader::ReadPath(ModuleFile &F, const RecordData &Record,
                                unsigned &Idx) {
  std::string Filename = ReadString(Record, Idx);
  ResolveImportedPath(F, Filename);
  return Filename;
}

std::string ASTReader::ReadPath(StringRef BaseDirectory,
                                const RecordData &Record, unsigned &Idx) {
  std::string Filename = ReadString(Record, Idx);
  if (!BaseDirectory.empty())
    ResolveImportedPath(Filename, BaseDirectory);
  return Filename;
}

VersionTuple ASTReader::ReadVersionTuple(const RecordData &Record,
                                         unsigned &Idx) {
  unsigned Major = Record[Idx++];
  unsigned Minor = Record[Idx++];
  unsigned Subminor = Record[Idx++];
  if (Minor == 0)
    return VersionTuple(Major);
  if (Subminor == 0)
    return VersionTuple(Major, Minor - 1);
  return VersionTuple(Major, Minor - 1, Subminor - 1);
}

CXXTemporary *ASTReader::ReadCXXTemporary(ModuleFile &F,
                                          const RecordData &Record,
                                          unsigned &Idx) {
  CXXDestructorDecl *Decl = ReadDeclAs<CXXDestructorDecl>(F, Record, Idx);
  return CXXTemporary::Create(getContext(), Decl);
}

DiagnosticBuilder ASTReader::Diag(unsigned DiagID) const {
  return Diag(CurrentImportLoc, DiagID);
}

DiagnosticBuilder ASTReader::Diag(SourceLocation Loc, unsigned DiagID) const {
  return Diags.Report(Loc, DiagID);
}

void ASTReader::warnStackExhausted(SourceLocation Loc) {
  // When Sema is available, avoid duplicate errors.
  if (SemaObj) {
    SemaObj->warnStackExhausted(Loc);
    return;
  }

  if (WarnedStackExhausted)
    return;
  WarnedStackExhausted = true;

  Diag(Loc, diag::warn_stack_exhausted);
}

/// Retrieve the identifier table associated with the
/// preprocessor.
IdentifierTable &ASTReader::getIdentifierTable() {
  return PP.getIdentifierTable();
}

/// Record that the given ID maps to the given switch-case
/// statement.
void ASTReader::RecordSwitchCaseID(SwitchCase *SC, unsigned ID) {
  assert((*CurrSwitchCaseStmts)[ID] == nullptr &&
         "Already have a SwitchCase with this ID");
  (*CurrSwitchCaseStmts)[ID] = SC;
}

/// Retrieve the switch-case statement with the given ID.
SwitchCase *ASTReader::getSwitchCaseWithID(unsigned ID) {
  assert((*CurrSwitchCaseStmts)[ID] != nullptr && "No SwitchCase with this ID");
  return (*CurrSwitchCaseStmts)[ID];
}

void ASTReader::ClearSwitchCaseIDs() {
  CurrSwitchCaseStmts->clear();
}

void ASTReader::ReadComments() {
  ASTContext &Context = getContext();
  std::vector<RawComment *> Comments;
  for (SmallVectorImpl<std::pair<BitstreamCursor,
                                 serialization::ModuleFile *>>::iterator
       I = CommentsCursors.begin(),
       E = CommentsCursors.end();
       I != E; ++I) {
    Comments.clear();
    BitstreamCursor &Cursor = I->first;
    serialization::ModuleFile &F = *I->second;
    SavedStreamPosition SavedPosition(Cursor);

    RecordData Record;
    while (true) {
      Expected<llvm::BitstreamEntry> MaybeEntry =
          Cursor.advanceSkippingSubblocks(
              BitstreamCursor::AF_DontPopBlockAtEnd);
      if (!MaybeEntry) {
        Error(MaybeEntry.takeError());
        return;
      }
      llvm::BitstreamEntry Entry = MaybeEntry.get();

      switch (Entry.Kind) {
      case llvm::BitstreamEntry::SubBlock: // Handled for us already.
      case llvm::BitstreamEntry::Error:
        Error("malformed block record in AST file");
        return;
      case llvm::BitstreamEntry::EndBlock:
        goto NextCursor;
      case llvm::BitstreamEntry::Record:
        // The interesting case.
        break;
      }

      // Read a record.
      Record.clear();
      Expected<unsigned> MaybeComment = Cursor.readRecord(Entry.ID, Record);
      if (!MaybeComment) {
        Error(MaybeComment.takeError());
        return;
      }
      switch ((CommentRecordTypes)MaybeComment.get()) {
      case COMMENTS_RAW_COMMENT: {
        unsigned Idx = 0;
        SourceRange SR = ReadSourceRange(F, Record, Idx);
        RawComment::CommentKind Kind =
            (RawComment::CommentKind) Record[Idx++];
        bool IsTrailingComment = Record[Idx++];
        bool IsAlmostTrailingComment = Record[Idx++];
        Comments.push_back(new (Context) RawComment(
            SR, Kind, IsTrailingComment, IsAlmostTrailingComment));
        break;
      }
      }
    }
  NextCursor:
    llvm::DenseMap<FileID, std::map<unsigned, RawComment *>>
        FileToOffsetToComment;
    for (RawComment *C : Comments) {
      SourceLocation CommentLoc = C->getBeginLoc();
      if (CommentLoc.isValid()) {
        std::pair<FileID, unsigned> Loc =
            SourceMgr.getDecomposedLoc(CommentLoc);
        if (Loc.first.isValid())
          Context.Comments.OrderedComments[Loc.first].emplace(Loc.second, C);
      }
    }
  }
}

void ASTReader::visitInputFileInfos(
    serialization::ModuleFile &MF, bool IncludeSystem,
    llvm::function_ref<void(const serialization::InputFileInfo &IFI,
                            bool IsSystem)>
        Visitor) {
  unsigned NumUserInputs = MF.NumUserInputFiles;
  unsigned NumInputs = MF.InputFilesLoaded.size();
  assert(NumUserInputs <= NumInputs);
  unsigned N = IncludeSystem ? NumInputs : NumUserInputs;
  for (unsigned I = 0; I < N; ++I) {
    bool IsSystem = I >= NumUserInputs;
    InputFileInfo IFI = getInputFileInfo(MF, I+1);
    Visitor(IFI, IsSystem);
  }
}

void ASTReader::visitInputFiles(serialization::ModuleFile &MF,
                                bool IncludeSystem, bool Complain,
                    llvm::function_ref<void(const serialization::InputFile &IF,
                                            bool isSystem)> Visitor) {
  unsigned NumUserInputs = MF.NumUserInputFiles;
  unsigned NumInputs = MF.InputFilesLoaded.size();
  assert(NumUserInputs <= NumInputs);
  unsigned N = IncludeSystem ? NumInputs : NumUserInputs;
  for (unsigned I = 0; I < N; ++I) {
    bool IsSystem = I >= NumUserInputs;
    InputFile IF = getInputFile(MF, I+1, Complain);
    Visitor(IF, IsSystem);
  }
}

void ASTReader::visitTopLevelModuleMaps(
    serialization::ModuleFile &MF,
    llvm::function_ref<void(FileEntryRef FE)> Visitor) {
  unsigned NumInputs = MF.InputFilesLoaded.size();
  for (unsigned I = 0; I < NumInputs; ++I) {
    InputFileInfo IFI = getInputFileInfo(MF, I + 1);
    if (IFI.TopLevel && IFI.ModuleMap)
      if (auto FE = getInputFile(MF, I + 1).getFile())
        Visitor(*FE);
  }
}

void ASTReader::finishPendingActions() {
  while (
      !PendingIdentifierInfos.empty() || !PendingDeducedFunctionTypes.empty() ||
      !PendingDeducedVarTypes.empty() || !PendingIncompleteDeclChains.empty() ||
      !PendingDeclChains.empty() || !PendingMacroIDs.empty() ||
      !PendingDeclContextInfos.empty() || !PendingUpdateRecords.empty() ||
      !PendingObjCExtensionIvarRedeclarations.empty()) {
    // If any identifiers with corresponding top-level declarations have
    // been loaded, load those declarations now.
    using TopLevelDeclsMap =
        llvm::DenseMap<IdentifierInfo *, SmallVector<Decl *, 2>>;
    TopLevelDeclsMap TopLevelDecls;

    while (!PendingIdentifierInfos.empty()) {
      IdentifierInfo *II = PendingIdentifierInfos.back().first;
      SmallVector<GlobalDeclID, 4> DeclIDs =
          std::move(PendingIdentifierInfos.back().second);
      PendingIdentifierInfos.pop_back();

      SetGloballyVisibleDecls(II, DeclIDs, &TopLevelDecls[II]);
    }

    // Load each function type that we deferred loading because it was a
    // deduced type that might refer to a local type declared within itself.
    for (unsigned I = 0; I != PendingDeducedFunctionTypes.size(); ++I) {
      auto *FD = PendingDeducedFunctionTypes[I].first;
      FD->setType(GetType(PendingDeducedFunctionTypes[I].second));

      if (auto *DT = FD->getReturnType()->getContainedDeducedType()) {
        // If we gave a function a deduced return type, remember that we need to
        // propagate that along the redeclaration chain.
        if (DT->isDeduced()) {
          PendingDeducedTypeUpdates.insert(
              {FD->getCanonicalDecl(), FD->getReturnType()});
          continue;
        }

        // The function has undeduced DeduceType return type. We hope we can
        // find the deduced type by iterating the redecls in other modules
        // later.
        PendingUndeducedFunctionDecls.push_back(FD);
        continue;
      }
    }
    PendingDeducedFunctionTypes.clear();

    // Load each variable type that we deferred loading because it was a
    // deduced type that might refer to a local type declared within itself.
    for (unsigned I = 0; I != PendingDeducedVarTypes.size(); ++I) {
      auto *VD = PendingDeducedVarTypes[I].first;
      VD->setType(GetType(PendingDeducedVarTypes[I].second));
    }
    PendingDeducedVarTypes.clear();

    // For each decl chain that we wanted to complete while deserializing, mark
    // it as "still needs to be completed".
    for (unsigned I = 0; I != PendingIncompleteDeclChains.size(); ++I) {
      markIncompleteDeclChain(PendingIncompleteDeclChains[I]);
    }
    PendingIncompleteDeclChains.clear();

    // Load pending declaration chains.
    for (unsigned I = 0; I != PendingDeclChains.size(); ++I)
      loadPendingDeclChain(PendingDeclChains[I].first,
                           PendingDeclChains[I].second);
    PendingDeclChains.clear();

    // Make the most recent of the top-level declarations visible.
    for (TopLevelDeclsMap::iterator TLD = TopLevelDecls.begin(),
           TLDEnd = TopLevelDecls.end(); TLD != TLDEnd; ++TLD) {
      IdentifierInfo *II = TLD->first;
      for (unsigned I = 0, N = TLD->second.size(); I != N; ++I) {
        pushExternalDeclIntoScope(cast<NamedDecl>(TLD->second[I]), II);
      }
    }

    // Load any pending macro definitions.
    for (unsigned I = 0; I != PendingMacroIDs.size(); ++I) {
      IdentifierInfo *II = PendingMacroIDs.begin()[I].first;
      SmallVector<PendingMacroInfo, 2> GlobalIDs;
      GlobalIDs.swap(PendingMacroIDs.begin()[I].second);
      // Initialize the macro history from chained-PCHs ahead of module imports.
      for (unsigned IDIdx = 0, NumIDs = GlobalIDs.size(); IDIdx != NumIDs;
           ++IDIdx) {
        const PendingMacroInfo &Info = GlobalIDs[IDIdx];
        if (!Info.M->isModule())
          resolvePendingMacro(II, Info);
      }
      // Handle module imports.
      for (unsigned IDIdx = 0, NumIDs = GlobalIDs.size(); IDIdx != NumIDs;
           ++IDIdx) {
        const PendingMacroInfo &Info = GlobalIDs[IDIdx];
        if (Info.M->isModule())
          resolvePendingMacro(II, Info);
      }
    }
    PendingMacroIDs.clear();

    // Wire up the DeclContexts for Decls that we delayed setting until
    // recursive loading is completed.
    while (!PendingDeclContextInfos.empty()) {
      PendingDeclContextInfo Info = PendingDeclContextInfos.front();
      PendingDeclContextInfos.pop_front();
      DeclContext *SemaDC = cast<DeclContext>(GetDecl(Info.SemaDC));
      DeclContext *LexicalDC = cast<DeclContext>(GetDecl(Info.LexicalDC));
      Info.D->setDeclContextsImpl(SemaDC, LexicalDC, getContext());
    }

    // Perform any pending declaration updates.
    while (!PendingUpdateRecords.empty()) {
      auto Update = PendingUpdateRecords.pop_back_val();
      ReadingKindTracker ReadingKind(Read_Decl, *this);
      loadDeclUpdateRecords(Update);
    }

    while (!PendingObjCExtensionIvarRedeclarations.empty()) {
      auto ExtensionsPair = PendingObjCExtensionIvarRedeclarations.back().first;
      auto DuplicateIvars =
          PendingObjCExtensionIvarRedeclarations.back().second;
      llvm::DenseSet<std::pair<Decl *, Decl *>> NonEquivalentDecls;
      StructuralEquivalenceContext Ctx(
          ExtensionsPair.first->getASTContext(),
          ExtensionsPair.second->getASTContext(), NonEquivalentDecls,
          StructuralEquivalenceKind::Default, /*StrictTypeSpelling =*/false,
          /*Complain =*/false,
          /*ErrorOnTagTypeMismatch =*/true);
      if (Ctx.IsEquivalent(ExtensionsPair.first, ExtensionsPair.second)) {
        // Merge redeclared ivars with their predecessors.
        for (auto IvarPair : DuplicateIvars) {
          ObjCIvarDecl *Ivar = IvarPair.first, *PrevIvar = IvarPair.second;
          // Change semantic DeclContext but keep the lexical one.
          Ivar->setDeclContextsImpl(PrevIvar->getDeclContext(),
                                    Ivar->getLexicalDeclContext(),
                                    getContext());
          getContext().setPrimaryMergedDecl(Ivar, PrevIvar->getCanonicalDecl());
        }
        // Invalidate duplicate extension and the cached ivar list.
        ExtensionsPair.first->setInvalidDecl();
        ExtensionsPair.second->getClassInterface()
            ->getDefinition()
            ->setIvarList(nullptr);
      } else {
        for (auto IvarPair : DuplicateIvars) {
          Diag(IvarPair.first->getLocation(),
               diag::err_duplicate_ivar_declaration)
              << IvarPair.first->getIdentifier();
          Diag(IvarPair.second->getLocation(), diag::note_previous_definition);
        }
      }
      PendingObjCExtensionIvarRedeclarations.pop_back();
    }
  }

  // At this point, all update records for loaded decls are in place, so any
  // fake class definitions should have become real.
  assert(PendingFakeDefinitionData.empty() &&
         "faked up a class definition but never saw the real one");

  // If we deserialized any C++ or Objective-C class definitions, any
  // Objective-C protocol definitions, or any redeclarable templates, make sure
  // that all redeclarations point to the definitions. Note that this can only
  // happen now, after the redeclaration chains have been fully wired.
  for (Decl *D : PendingDefinitions) {
    if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
      if (const TagType *TagT = dyn_cast<TagType>(TD->getTypeForDecl())) {
        // Make sure that the TagType points at the definition.
        const_cast<TagType*>(TagT)->decl = TD;
      }

      if (auto RD = dyn_cast<CXXRecordDecl>(D)) {
        for (auto *R = getMostRecentExistingDecl(RD); R;
             R = R->getPreviousDecl()) {
          assert((R == D) ==
                     cast<CXXRecordDecl>(R)->isThisDeclarationADefinition() &&
                 "declaration thinks it's the definition but it isn't");
          cast<CXXRecordDecl>(R)->DefinitionData = RD->DefinitionData;
        }
      }

      continue;
    }

    if (auto ID = dyn_cast<ObjCInterfaceDecl>(D)) {
      // Make sure that the ObjCInterfaceType points at the definition.
      const_cast<ObjCInterfaceType *>(cast<ObjCInterfaceType>(ID->TypeForDecl))
        ->Decl = ID;

      for (auto *R = getMostRecentExistingDecl(ID); R; R = R->getPreviousDecl())
        cast<ObjCInterfaceDecl>(R)->Data = ID->Data;

      continue;
    }

    if (auto PD = dyn_cast<ObjCProtocolDecl>(D)) {
      for (auto *R = getMostRecentExistingDecl(PD); R; R = R->getPreviousDecl())
        cast<ObjCProtocolDecl>(R)->Data = PD->Data;

      continue;
    }

    auto RTD = cast<RedeclarableTemplateDecl>(D)->getCanonicalDecl();
    for (auto *R = getMostRecentExistingDecl(RTD); R; R = R->getPreviousDecl())
      cast<RedeclarableTemplateDecl>(R)->Common = RTD->Common;
  }
  PendingDefinitions.clear();

  // Load the bodies of any functions or methods we've encountered. We do
  // this now (delayed) so that we can be sure that the declaration chains
  // have been fully wired up (hasBody relies on this).
  // FIXME: We shouldn't require complete redeclaration chains here.
  for (PendingBodiesMap::iterator PB = PendingBodies.begin(),
                               PBEnd = PendingBodies.end();
       PB != PBEnd; ++PB) {
    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(PB->first)) {
      // For a function defined inline within a class template, force the
      // canonical definition to be the one inside the canonical definition of
      // the template. This ensures that we instantiate from a correct view
      // of the template.
      //
      // Sadly we can't do this more generally: we can't be sure that all
      // copies of an arbitrary class definition will have the same members
      // defined (eg, some member functions may not be instantiated, and some
      // special members may or may not have been implicitly defined).
      if (auto *RD = dyn_cast<CXXRecordDecl>(FD->getLexicalParent()))
        if (RD->isDependentContext() && !RD->isThisDeclarationADefinition())
          continue;

      // FIXME: Check for =delete/=default?
      const FunctionDecl *Defn = nullptr;
      if (!getContext().getLangOpts().Modules || !FD->hasBody(Defn)) {
        FD->setLazyBody(PB->second);
      } else {
        auto *NonConstDefn = const_cast<FunctionDecl*>(Defn);
        mergeDefinitionVisibility(NonConstDefn, FD);

        if (!FD->isLateTemplateParsed() &&
            !NonConstDefn->isLateTemplateParsed() &&
            // We only perform ODR checks for decls not in the explicit
            // global module fragment.
            !shouldSkipCheckingODR(FD) &&
            !shouldSkipCheckingODR(NonConstDefn) &&
            FD->getODRHash() != NonConstDefn->getODRHash()) {
          if (!isa<CXXMethodDecl>(FD)) {
            PendingFunctionOdrMergeFailures[FD].push_back(NonConstDefn);
          } else if (FD->getLexicalParent()->isFileContext() &&
                     NonConstDefn->getLexicalParent()->isFileContext()) {
            // Only diagnose out-of-line method definitions.  If they are
            // in class definitions, then an error will be generated when
            // processing the class bodies.
            PendingFunctionOdrMergeFailures[FD].push_back(NonConstDefn);
          }
        }
      }
      continue;
    }

    ObjCMethodDecl *MD = cast<ObjCMethodDecl>(PB->first);
    if (!getContext().getLangOpts().Modules || !MD->hasBody())
      MD->setLazyBody(PB->second);
  }
  PendingBodies.clear();

  // Inform any classes that had members added that they now have more members.
  for (auto [RD, MD] : PendingAddedClassMembers) {
    RD->addedMember(MD);
  }
  PendingAddedClassMembers.clear();

  // Do some cleanup.
  for (auto *ND : PendingMergedDefinitionsToDeduplicate)
    getContext().deduplicateMergedDefinitonsFor(ND);
  PendingMergedDefinitionsToDeduplicate.clear();
}

void ASTReader::diagnoseOdrViolations() {
  if (PendingOdrMergeFailures.empty() && PendingOdrMergeChecks.empty() &&
      PendingRecordOdrMergeFailures.empty() &&
      PendingFunctionOdrMergeFailures.empty() &&
      PendingEnumOdrMergeFailures.empty() &&
      PendingObjCInterfaceOdrMergeFailures.empty() &&
      PendingObjCProtocolOdrMergeFailures.empty())
    return;

  // Trigger the import of the full definition of each class that had any
  // odr-merging problems, so we can produce better diagnostics for them.
  // These updates may in turn find and diagnose some ODR failures, so take
  // ownership of the set first.
  auto OdrMergeFailures = std::move(PendingOdrMergeFailures);
  PendingOdrMergeFailures.clear();
  for (auto &Merge : OdrMergeFailures) {
    Merge.first->buildLookup();
    Merge.first->decls_begin();
    Merge.first->bases_begin();
    Merge.first->vbases_begin();
    for (auto &RecordPair : Merge.second) {
      auto *RD = RecordPair.first;
      RD->decls_begin();
      RD->bases_begin();
      RD->vbases_begin();
    }
  }

  // Trigger the import of the full definition of each record in C/ObjC.
  auto RecordOdrMergeFailures = std::move(PendingRecordOdrMergeFailures);
  PendingRecordOdrMergeFailures.clear();
  for (auto &Merge : RecordOdrMergeFailures) {
    Merge.first->decls_begin();
    for (auto &D : Merge.second)
      D->decls_begin();
  }

  // Trigger the import of the full interface definition.
  auto ObjCInterfaceOdrMergeFailures =
      std::move(PendingObjCInterfaceOdrMergeFailures);
  PendingObjCInterfaceOdrMergeFailures.clear();
  for (auto &Merge : ObjCInterfaceOdrMergeFailures) {
    Merge.first->decls_begin();
    for (auto &InterfacePair : Merge.second)
      InterfacePair.first->decls_begin();
  }

  // Trigger the import of functions.
  auto FunctionOdrMergeFailures = std::move(PendingFunctionOdrMergeFailures);
  PendingFunctionOdrMergeFailures.clear();
  for (auto &Merge : FunctionOdrMergeFailures) {
    Merge.first->buildLookup();
    Merge.first->decls_begin();
    Merge.first->getBody();
    for (auto &FD : Merge.second) {
      FD->buildLookup();
      FD->decls_begin();
      FD->getBody();
    }
  }

  // Trigger the import of enums.
  auto EnumOdrMergeFailures = std::move(PendingEnumOdrMergeFailures);
  PendingEnumOdrMergeFailures.clear();
  for (auto &Merge : EnumOdrMergeFailures) {
    Merge.first->decls_begin();
    for (auto &Enum : Merge.second) {
      Enum->decls_begin();
    }
  }

  // Trigger the import of the full protocol definition.
  auto ObjCProtocolOdrMergeFailures =
      std::move(PendingObjCProtocolOdrMergeFailures);
  PendingObjCProtocolOdrMergeFailures.clear();
  for (auto &Merge : ObjCProtocolOdrMergeFailures) {
    Merge.first->decls_begin();
    for (auto &ProtocolPair : Merge.second)
      ProtocolPair.first->decls_begin();
  }

  // For each declaration from a merged context, check that the canonical
  // definition of that context also contains a declaration of the same
  // entity.
  //
  // Caution: this loop does things that might invalidate iterators into
  // PendingOdrMergeChecks. Don't turn this into a range-based for loop!
  while (!PendingOdrMergeChecks.empty()) {
    NamedDecl *D = PendingOdrMergeChecks.pop_back_val();

    // FIXME: Skip over implicit declarations for now. This matters for things
    // like implicitly-declared special member functions. This isn't entirely
    // correct; we can end up with multiple unmerged declarations of the same
    // implicit entity.
    if (D->isImplicit())
      continue;

    DeclContext *CanonDef = D->getDeclContext();

    bool Found = false;
    const Decl *DCanon = D->getCanonicalDecl();

    for (auto *RI : D->redecls()) {
      if (RI->getLexicalDeclContext() == CanonDef) {
        Found = true;
        break;
      }
    }
    if (Found)
      continue;

    // Quick check failed, time to do the slow thing. Note, we can't just
    // look up the name of D in CanonDef here, because the member that is
    // in CanonDef might not be found by name lookup (it might have been
    // replaced by a more recent declaration in the lookup table), and we
    // can't necessarily find it in the redeclaration chain because it might
    // be merely mergeable, not redeclarable.
    llvm::SmallVector<const NamedDecl*, 4> Candidates;
    for (auto *CanonMember : CanonDef->decls()) {
      if (CanonMember->getCanonicalDecl() == DCanon) {
        // This can happen if the declaration is merely mergeable and not
        // actually redeclarable (we looked for redeclarations earlier).
        //
        // FIXME: We should be able to detect this more efficiently, without
        // pulling in all of the members of CanonDef.
        Found = true;
        break;
      }
      if (auto *ND = dyn_cast<NamedDecl>(CanonMember))
        if (ND->getDeclName() == D->getDeclName())
          Candidates.push_back(ND);
    }

    if (!Found) {
      // The AST doesn't like TagDecls becoming invalid after they've been
      // completed. We only really need to mark FieldDecls as invalid here.
      if (!isa<TagDecl>(D))
        D->setInvalidDecl();

      // Ensure we don't accidentally recursively enter deserialization while
      // we're producing our diagnostic.
      Deserializing RecursionGuard(this);

      std::string CanonDefModule =
          ODRDiagsEmitter::getOwningModuleNameForDiagnostic(
              cast<Decl>(CanonDef));
      Diag(D->getLocation(), diag::err_module_odr_violation_missing_decl)
        << D << ODRDiagsEmitter::getOwningModuleNameForDiagnostic(D)
        << CanonDef << CanonDefModule.empty() << CanonDefModule;

      if (Candidates.empty())
        Diag(cast<Decl>(CanonDef)->getLocation(),
             diag::note_module_odr_violation_no_possible_decls) << D;
      else {
        for (unsigned I = 0, N = Candidates.size(); I != N; ++I)
          Diag(Candidates[I]->getLocation(),
               diag::note_module_odr_violation_possible_decl)
            << Candidates[I];
      }

      DiagnosedOdrMergeFailures.insert(CanonDef);
    }
  }

  if (OdrMergeFailures.empty() && RecordOdrMergeFailures.empty() &&
      FunctionOdrMergeFailures.empty() && EnumOdrMergeFailures.empty() &&
      ObjCInterfaceOdrMergeFailures.empty() &&
      ObjCProtocolOdrMergeFailures.empty())
    return;

  ODRDiagsEmitter DiagsEmitter(Diags, getContext(),
                               getPreprocessor().getLangOpts());

  // Issue any pending ODR-failure diagnostics.
  for (auto &Merge : OdrMergeFailures) {
    // If we've already pointed out a specific problem with this class, don't
    // bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    bool Diagnosed = false;
    CXXRecordDecl *FirstRecord = Merge.first;
    for (auto &RecordPair : Merge.second) {
      if (DiagsEmitter.diagnoseMismatch(FirstRecord, RecordPair.first,
                                        RecordPair.second)) {
        Diagnosed = true;
        break;
      }
    }

    if (!Diagnosed) {
      // All definitions are updates to the same declaration. This happens if a
      // module instantiates the declaration of a class template specialization
      // and two or more other modules instantiate its definition.
      //
      // FIXME: Indicate which modules had instantiations of this definition.
      // FIXME: How can this even happen?
      Diag(Merge.first->getLocation(),
           diag::err_module_odr_violation_different_instantiations)
          << Merge.first;
    }
  }

  // Issue any pending ODR-failure diagnostics for RecordDecl in C/ObjC. Note
  // that in C++ this is done as a part of CXXRecordDecl ODR checking.
  for (auto &Merge : RecordOdrMergeFailures) {
    // If we've already pointed out a specific problem with this class, don't
    // bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    RecordDecl *FirstRecord = Merge.first;
    bool Diagnosed = false;
    for (auto *SecondRecord : Merge.second) {
      if (DiagsEmitter.diagnoseMismatch(FirstRecord, SecondRecord)) {
        Diagnosed = true;
        break;
      }
    }
    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }

  // Issue ODR failures diagnostics for functions.
  for (auto &Merge : FunctionOdrMergeFailures) {
    FunctionDecl *FirstFunction = Merge.first;
    bool Diagnosed = false;
    for (auto &SecondFunction : Merge.second) {
      if (DiagsEmitter.diagnoseMismatch(FirstFunction, SecondFunction)) {
        Diagnosed = true;
        break;
      }
    }
    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }

  // Issue ODR failures diagnostics for enums.
  for (auto &Merge : EnumOdrMergeFailures) {
    // If we've already pointed out a specific problem with this enum, don't
    // bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    EnumDecl *FirstEnum = Merge.first;
    bool Diagnosed = false;
    for (auto &SecondEnum : Merge.second) {
      if (DiagsEmitter.diagnoseMismatch(FirstEnum, SecondEnum)) {
        Diagnosed = true;
        break;
      }
    }
    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }

  for (auto &Merge : ObjCInterfaceOdrMergeFailures) {
    // If we've already pointed out a specific problem with this interface,
    // don't bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    bool Diagnosed = false;
    ObjCInterfaceDecl *FirstID = Merge.first;
    for (auto &InterfacePair : Merge.second) {
      if (DiagsEmitter.diagnoseMismatch(FirstID, InterfacePair.first,
                                        InterfacePair.second)) {
        Diagnosed = true;
        break;
      }
    }
    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }

  for (auto &Merge : ObjCProtocolOdrMergeFailures) {
    // If we've already pointed out a specific problem with this protocol,
    // don't bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    ObjCProtocolDecl *FirstProtocol = Merge.first;
    bool Diagnosed = false;
    for (auto &ProtocolPair : Merge.second) {
      if (DiagsEmitter.diagnoseMismatch(FirstProtocol, ProtocolPair.first,
                                        ProtocolPair.second)) {
        Diagnosed = true;
        break;
      }
    }
    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }
}

void ASTReader::StartedDeserializing() {
  if (++NumCurrentElementsDeserializing == 1 && ReadTimer.get())
    ReadTimer->startTimer();
}

void ASTReader::FinishedDeserializing() {
  assert(NumCurrentElementsDeserializing &&
         "FinishedDeserializing not paired with StartedDeserializing");
  if (NumCurrentElementsDeserializing == 1) {
    // We decrease NumCurrentElementsDeserializing only after pending actions
    // are finished, to avoid recursively re-calling finishPendingActions().
    finishPendingActions();
  }
  --NumCurrentElementsDeserializing;

  if (NumCurrentElementsDeserializing == 0) {
    // Propagate exception specification and deduced type updates along
    // redeclaration chains.
    //
    // We do this now rather than in finishPendingActions because we want to
    // be able to walk the complete redeclaration chains of the updated decls.
    while (!PendingExceptionSpecUpdates.empty() ||
           !PendingDeducedTypeUpdates.empty()) {
      auto ESUpdates = std::move(PendingExceptionSpecUpdates);
      PendingExceptionSpecUpdates.clear();
      for (auto Update : ESUpdates) {
        ProcessingUpdatesRAIIObj ProcessingUpdates(*this);
        auto *FPT = Update.second->getType()->castAs<FunctionProtoType>();
        auto ESI = FPT->getExtProtoInfo().ExceptionSpec;
        if (auto *Listener = getContext().getASTMutationListener())
          Listener->ResolvedExceptionSpec(cast<FunctionDecl>(Update.second));
        for (auto *Redecl : Update.second->redecls())
          getContext().adjustExceptionSpec(cast<FunctionDecl>(Redecl), ESI);
      }

      auto DTUpdates = std::move(PendingDeducedTypeUpdates);
      PendingDeducedTypeUpdates.clear();
      for (auto Update : DTUpdates) {
        ProcessingUpdatesRAIIObj ProcessingUpdates(*this);
        // FIXME: If the return type is already deduced, check that it matches.
        getContext().adjustDeducedFunctionResultType(Update.first,
                                                     Update.second);
      }

      auto UDTUpdates = std::move(PendingUndeducedFunctionDecls);
      PendingUndeducedFunctionDecls.clear();
      // We hope we can find the deduced type for the functions by iterating
      // redeclarations in other modules.
      for (FunctionDecl *UndeducedFD : UDTUpdates)
        (void)UndeducedFD->getMostRecentDecl();
    }

    if (ReadTimer)
      ReadTimer->stopTimer();

    diagnoseOdrViolations();

    // We are not in recursive loading, so it's safe to pass the "interesting"
    // decls to the consumer.
    if (Consumer)
      PassInterestingDeclsToConsumer();
  }
}

void ASTReader::pushExternalDeclIntoScope(NamedDecl *D, DeclarationName Name) {
  if (const IdentifierInfo *II = Name.getAsIdentifierInfo()) {
    // Remove any fake results before adding any real ones.
    auto It = PendingFakeLookupResults.find(II);
    if (It != PendingFakeLookupResults.end()) {
      for (auto *ND : It->second)
        SemaObj->IdResolver.RemoveDecl(ND);
      // FIXME: this works around module+PCH performance issue.
      // Rather than erase the result from the map, which is O(n), just clear
      // the vector of NamedDecls.
      It->second.clear();
    }
  }

  if (SemaObj->IdResolver.tryAddTopLevelDecl(D, Name) && SemaObj->TUScope) {
    SemaObj->TUScope->AddDecl(D);
  } else if (SemaObj->TUScope) {
    // Adding the decl to IdResolver may have failed because it was already in
    // (even though it was not added in scope). If it is already in, make sure
    // it gets in the scope as well.
    if (llvm::is_contained(SemaObj->IdResolver.decls(Name), D))
      SemaObj->TUScope->AddDecl(D);
  }
}

ASTReader::ASTReader(Preprocessor &PP, InMemoryModuleCache &ModuleCache,
                     ASTContext *Context,
                     const PCHContainerReader &PCHContainerRdr,
                     ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
                     StringRef isysroot,
                     DisableValidationForModuleKind DisableValidationKind,
                     bool AllowASTWithCompilerErrors,
                     bool AllowConfigurationMismatch, bool ValidateSystemInputs,
                     bool ValidateASTInputFilesContent, bool UseGlobalIndex,
                     std::unique_ptr<llvm::Timer> ReadTimer)
    : Listener(bool(DisableValidationKind &DisableValidationForModuleKind::PCH)
                   ? cast<ASTReaderListener>(new SimpleASTReaderListener(PP))
                   : cast<ASTReaderListener>(new PCHValidator(PP, *this))),
      SourceMgr(PP.getSourceManager()), FileMgr(PP.getFileManager()),
      PCHContainerRdr(PCHContainerRdr), Diags(PP.getDiagnostics()), PP(PP),
      ContextObj(Context), ModuleMgr(PP.getFileManager(), ModuleCache,
                                     PCHContainerRdr, PP.getHeaderSearchInfo()),
      DummyIdResolver(PP), ReadTimer(std::move(ReadTimer)), isysroot(isysroot),
      DisableValidationKind(DisableValidationKind),
      AllowASTWithCompilerErrors(AllowASTWithCompilerErrors),
      AllowConfigurationMismatch(AllowConfigurationMismatch),
      ValidateSystemInputs(ValidateSystemInputs),
      ValidateASTInputFilesContent(ValidateASTInputFilesContent),
      UseGlobalIndex(UseGlobalIndex), CurrSwitchCaseStmts(&SwitchCaseStmts) {
  SourceMgr.setExternalSLocEntrySource(this);

  for (const auto &Ext : Extensions) {
    auto BlockName = Ext->getExtensionMetadata().BlockName;
    auto Known = ModuleFileExtensions.find(BlockName);
    if (Known != ModuleFileExtensions.end()) {
      Diags.Report(diag::warn_duplicate_module_file_extension)
        << BlockName;
      continue;
    }

    ModuleFileExtensions.insert({BlockName, Ext});
  }
}

ASTReader::~ASTReader() {
  if (OwnsDeserializationListener)
    delete DeserializationListener;
}

IdentifierResolver &ASTReader::getIdResolver() {
  return SemaObj ? SemaObj->IdResolver : DummyIdResolver;
}

Expected<unsigned> ASTRecordReader::readRecord(llvm::BitstreamCursor &Cursor,
                                               unsigned AbbrevID) {
  Idx = 0;
  Record.clear();
  return Cursor.readRecord(AbbrevID, Record);
}
//===----------------------------------------------------------------------===//
//// OMPClauseReader implementation
////===----------------------------------------------------------------------===//

// This has to be in namespace clang because it's friended by all
// of the OMP clauses.
namespace clang {

class OMPClauseReader : public OMPClauseVisitor<OMPClauseReader> {
  ASTRecordReader &Record;
  ASTContext &Context;

public:
  OMPClauseReader(ASTRecordReader &Record)
      : Record(Record), Context(Record.getContext()) {}
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) void Visit##Class(Class *C);
#include "llvm/Frontend/OpenMP/OMP.inc"
  OMPClause *readClause();
  void VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C);
  void VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C);
};

} // end namespace clang

OMPClause *ASTRecordReader::readOMPClause() {
  return OMPClauseReader(*this).readClause();
}

OMPClause *OMPClauseReader::readClause() {
  OMPClause *C = nullptr;
  switch (llvm::omp::Clause(Record.readInt())) {
  case llvm::omp::OMPC_if:
    C = new (Context) OMPIfClause();
    break;
  case llvm::omp::OMPC_final:
    C = new (Context) OMPFinalClause();
    break;
  case llvm::omp::OMPC_num_threads:
    C = new (Context) OMPNumThreadsClause();
    break;
  case llvm::omp::OMPC_safelen:
    C = new (Context) OMPSafelenClause();
    break;
  case llvm::omp::OMPC_simdlen:
    C = new (Context) OMPSimdlenClause();
    break;
  case llvm::omp::OMPC_sizes: {
    unsigned NumSizes = Record.readInt();
    C = OMPSizesClause::CreateEmpty(Context, NumSizes);
    break;
  }
  case llvm::omp::OMPC_full:
    C = OMPFullClause::CreateEmpty(Context);
    break;
  case llvm::omp::OMPC_partial:
    C = OMPPartialClause::CreateEmpty(Context);
    break;
  case llvm::omp::OMPC_allocator:
    C = new (Context) OMPAllocatorClause();
    break;
  case llvm::omp::OMPC_collapse:
    C = new (Context) OMPCollapseClause();
    break;
  case llvm::omp::OMPC_default:
    C = new (Context) OMPDefaultClause();
    break;
  case llvm::omp::OMPC_proc_bind:
    C = new (Context) OMPProcBindClause();
    break;
  case llvm::omp::OMPC_schedule:
    C = new (Context) OMPScheduleClause();
    break;
  case llvm::omp::OMPC_ordered:
    C = OMPOrderedClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_nowait:
    C = new (Context) OMPNowaitClause();
    break;
  case llvm::omp::OMPC_untied:
    C = new (Context) OMPUntiedClause();
    break;
  case llvm::omp::OMPC_mergeable:
    C = new (Context) OMPMergeableClause();
    break;
  case llvm::omp::OMPC_read:
    C = new (Context) OMPReadClause();
    break;
  case llvm::omp::OMPC_write:
    C = new (Context) OMPWriteClause();
    break;
  case llvm::omp::OMPC_update:
    C = OMPUpdateClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_capture:
    C = new (Context) OMPCaptureClause();
    break;
  case llvm::omp::OMPC_compare:
    C = new (Context) OMPCompareClause();
    break;
  case llvm::omp::OMPC_fail:
    C = new (Context) OMPFailClause();
    break;
  case llvm::omp::OMPC_seq_cst:
    C = new (Context) OMPSeqCstClause();
    break;
  case llvm::omp::OMPC_acq_rel:
    C = new (Context) OMPAcqRelClause();
    break;
  case llvm::omp::OMPC_acquire:
    C = new (Context) OMPAcquireClause();
    break;
  case llvm::omp::OMPC_release:
    C = new (Context) OMPReleaseClause();
    break;
  case llvm::omp::OMPC_relaxed:
    C = new (Context) OMPRelaxedClause();
    break;
  case llvm::omp::OMPC_weak:
    C = new (Context) OMPWeakClause();
    break;
  case llvm::omp::OMPC_threads:
    C = new (Context) OMPThreadsClause();
    break;
  case llvm::omp::OMPC_simd:
    C = new (Context) OMPSIMDClause();
    break;
  case llvm::omp::OMPC_nogroup:
    C = new (Context) OMPNogroupClause();
    break;
  case llvm::omp::OMPC_unified_address:
    C = new (Context) OMPUnifiedAddressClause();
    break;
  case llvm::omp::OMPC_unified_shared_memory:
    C = new (Context) OMPUnifiedSharedMemoryClause();
    break;
  case llvm::omp::OMPC_reverse_offload:
    C = new (Context) OMPReverseOffloadClause();
    break;
  case llvm::omp::OMPC_dynamic_allocators:
    C = new (Context) OMPDynamicAllocatorsClause();
    break;
  case llvm::omp::OMPC_atomic_default_mem_order:
    C = new (Context) OMPAtomicDefaultMemOrderClause();
    break;
  case llvm::omp::OMPC_at:
    C = new (Context) OMPAtClause();
    break;
  case llvm::omp::OMPC_severity:
    C = new (Context) OMPSeverityClause();
    break;
  case llvm::omp::OMPC_message:
    C = new (Context) OMPMessageClause();
    break;
  case llvm::omp::OMPC_private:
    C = OMPPrivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_firstprivate:
    C = OMPFirstprivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_lastprivate:
    C = OMPLastprivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_shared:
    C = OMPSharedClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_reduction: {
    unsigned N = Record.readInt();
    auto Modifier = Record.readEnum<OpenMPReductionClauseModifier>();
    C = OMPReductionClause::CreateEmpty(Context, N, Modifier);
    break;
  }
  case llvm::omp::OMPC_task_reduction:
    C = OMPTaskReductionClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_in_reduction:
    C = OMPInReductionClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_linear:
    C = OMPLinearClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_aligned:
    C = OMPAlignedClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_copyin:
    C = OMPCopyinClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_copyprivate:
    C = OMPCopyprivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_flush:
    C = OMPFlushClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_depobj:
    C = OMPDepobjClause::CreateEmpty(Context);
    break;
  case llvm::omp::OMPC_depend: {
    unsigned NumVars = Record.readInt();
    unsigned NumLoops = Record.readInt();
    C = OMPDependClause::CreateEmpty(Context, NumVars, NumLoops);
    break;
  }
  case llvm::omp::OMPC_device:
    C = new (Context) OMPDeviceClause();
    break;
  case llvm::omp::OMPC_map: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPMapClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_num_teams:
    C = new (Context) OMPNumTeamsClause();
    break;
  case llvm::omp::OMPC_thread_limit:
    C = new (Context) OMPThreadLimitClause();
    break;
  case llvm::omp::OMPC_priority:
    C = new (Context) OMPPriorityClause();
    break;
  case llvm::omp::OMPC_grainsize:
    C = new (Context) OMPGrainsizeClause();
    break;
  case llvm::omp::OMPC_num_tasks:
    C = new (Context) OMPNumTasksClause();
    break;
  case llvm::omp::OMPC_hint:
    C = new (Context) OMPHintClause();
    break;
  case llvm::omp::OMPC_dist_schedule:
    C = new (Context) OMPDistScheduleClause();
    break;
  case llvm::omp::OMPC_defaultmap:
    C = new (Context) OMPDefaultmapClause();
    break;
  case llvm::omp::OMPC_to: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPToClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_from: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPFromClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_use_device_ptr: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPUseDevicePtrClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_use_device_addr: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPUseDeviceAddrClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_is_device_ptr: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPIsDevicePtrClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_has_device_addr: {
    OMPMappableExprListSizeTy Sizes;
    Sizes.NumVars = Record.readInt();
    Sizes.NumUniqueDeclarations = Record.readInt();
    Sizes.NumComponentLists = Record.readInt();
    Sizes.NumComponents = Record.readInt();
    C = OMPHasDeviceAddrClause::CreateEmpty(Context, Sizes);
    break;
  }
  case llvm::omp::OMPC_allocate:
    C = OMPAllocateClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_nontemporal:
    C = OMPNontemporalClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_inclusive:
    C = OMPInclusiveClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_exclusive:
    C = OMPExclusiveClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_order:
    C = new (Context) OMPOrderClause();
    break;
  case llvm::omp::OMPC_init:
    C = OMPInitClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_use:
    C = new (Context) OMPUseClause();
    break;
  case llvm::omp::OMPC_destroy:
    C = new (Context) OMPDestroyClause();
    break;
  case llvm::omp::OMPC_novariants:
    C = new (Context) OMPNovariantsClause();
    break;
  case llvm::omp::OMPC_nocontext:
    C = new (Context) OMPNocontextClause();
    break;
  case llvm::omp::OMPC_detach:
    C = new (Context) OMPDetachClause();
    break;
  case llvm::omp::OMPC_uses_allocators:
    C = OMPUsesAllocatorsClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_affinity:
    C = OMPAffinityClause::CreateEmpty(Context, Record.readInt());
    break;
  case llvm::omp::OMPC_filter:
    C = new (Context) OMPFilterClause();
    break;
  case llvm::omp::OMPC_bind:
    C = OMPBindClause::CreateEmpty(Context);
    break;
  case llvm::omp::OMPC_align:
    C = new (Context) OMPAlignClause();
    break;
  case llvm::omp::OMPC_ompx_dyn_cgroup_mem:
    C = new (Context) OMPXDynCGroupMemClause();
    break;
  case llvm::omp::OMPC_doacross: {
    unsigned NumVars = Record.readInt();
    unsigned NumLoops = Record.readInt();
    C = OMPDoacrossClause::CreateEmpty(Context, NumVars, NumLoops);
    break;
  }
  case llvm::omp::OMPC_ompx_attribute:
    C = new (Context) OMPXAttributeClause();
    break;
  case llvm::omp::OMPC_ompx_bare:
    C = new (Context) OMPXBareClause();
    break;
#define OMP_CLAUSE_NO_CLASS(Enum, Str)                                         \
  case llvm::omp::Enum:                                                        \
    break;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  default:
    break;
  }
  assert(C && "Unknown OMPClause type");

  Visit(C);
  C->setLocStart(Record.readSourceLocation());
  C->setLocEnd(Record.readSourceLocation());

  return C;
}

void OMPClauseReader::VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C) {
  C->setPreInitStmt(Record.readSubStmt(),
                    static_cast<OpenMPDirectiveKind>(Record.readInt()));
}

void OMPClauseReader::VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C) {
  VisitOMPClauseWithPreInit(C);
  C->setPostUpdateExpr(Record.readSubExpr());
}

void OMPClauseReader::VisitOMPIfClause(OMPIfClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setNameModifier(static_cast<OpenMPDirectiveKind>(Record.readInt()));
  C->setNameModifierLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  C->setCondition(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPFinalClause(OMPFinalClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setCondition(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNumThreadsClause(OMPNumThreadsClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setNumThreads(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPSafelenClause(OMPSafelenClause *C) {
  C->setSafelen(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPSimdlenClause(OMPSimdlenClause *C) {
  C->setSimdlen(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPSizesClause(OMPSizesClause *C) {
  for (Expr *&E : C->getSizesRefs())
    E = Record.readSubExpr();
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPFullClause(OMPFullClause *C) {}

void OMPClauseReader::VisitOMPPartialClause(OMPPartialClause *C) {
  C->setFactor(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPAllocatorClause(OMPAllocatorClause *C) {
  C->setAllocator(Record.readExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPCollapseClause(OMPCollapseClause *C) {
  C->setNumForLoops(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDefaultClause(OMPDefaultClause *C) {
  C->setDefaultKind(static_cast<llvm::omp::DefaultKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setDefaultKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPProcBindClause(OMPProcBindClause *C) {
  C->setProcBindKind(static_cast<llvm::omp::ProcBindKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setProcBindKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPScheduleClause(OMPScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setScheduleKind(
       static_cast<OpenMPScheduleClauseKind>(Record.readInt()));
  C->setFirstScheduleModifier(
      static_cast<OpenMPScheduleClauseModifier>(Record.readInt()));
  C->setSecondScheduleModifier(
      static_cast<OpenMPScheduleClauseModifier>(Record.readInt()));
  C->setChunkSize(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
  C->setFirstScheduleModifierLoc(Record.readSourceLocation());
  C->setSecondScheduleModifierLoc(Record.readSourceLocation());
  C->setScheduleKindLoc(Record.readSourceLocation());
  C->setCommaLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPOrderedClause(OMPOrderedClause *C) {
  C->setNumForLoops(Record.readSubExpr());
  for (unsigned I = 0, E = C->NumberOfLoops; I < E; ++I)
    C->setLoopNumIterations(I, Record.readSubExpr());
  for (unsigned I = 0, E = C->NumberOfLoops; I < E; ++I)
    C->setLoopCounter(I, Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDetachClause(OMPDetachClause *C) {
  C->setEventHandler(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNowaitClause(OMPNowaitClause *) {}

void OMPClauseReader::VisitOMPUntiedClause(OMPUntiedClause *) {}

void OMPClauseReader::VisitOMPMergeableClause(OMPMergeableClause *) {}

void OMPClauseReader::VisitOMPReadClause(OMPReadClause *) {}

void OMPClauseReader::VisitOMPWriteClause(OMPWriteClause *) {}

void OMPClauseReader::VisitOMPUpdateClause(OMPUpdateClause *C) {
  if (C->isExtended()) {
    C->setLParenLoc(Record.readSourceLocation());
    C->setArgumentLoc(Record.readSourceLocation());
    C->setDependencyKind(Record.readEnum<OpenMPDependClauseKind>());
  }
}

void OMPClauseReader::VisitOMPCaptureClause(OMPCaptureClause *) {}

void OMPClauseReader::VisitOMPCompareClause(OMPCompareClause *) {}

// Read the parameter of fail clause. This will have been saved when
// OMPClauseWriter is called.
void OMPClauseReader::VisitOMPFailClause(OMPFailClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  SourceLocation FailParameterLoc = Record.readSourceLocation();
  C->setFailParameterLoc(FailParameterLoc);
  OpenMPClauseKind CKind = Record.readEnum<OpenMPClauseKind>();
  C->setFailParameter(CKind);
}

void OMPClauseReader::VisitOMPSeqCstClause(OMPSeqCstClause *) {}

void OMPClauseReader::VisitOMPAcqRelClause(OMPAcqRelClause *) {}

void OMPClauseReader::VisitOMPAcquireClause(OMPAcquireClause *) {}

void OMPClauseReader::VisitOMPReleaseClause(OMPReleaseClause *) {}

void OMPClauseReader::VisitOMPRelaxedClause(OMPRelaxedClause *) {}

void OMPClauseReader::VisitOMPWeakClause(OMPWeakClause *) {}

void OMPClauseReader::VisitOMPThreadsClause(OMPThreadsClause *) {}

void OMPClauseReader::VisitOMPSIMDClause(OMPSIMDClause *) {}

void OMPClauseReader::VisitOMPNogroupClause(OMPNogroupClause *) {}

void OMPClauseReader::VisitOMPInitClause(OMPInitClause *C) {
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  C->setIsTarget(Record.readBool());
  C->setIsTargetSync(Record.readBool());
  C->setLParenLoc(Record.readSourceLocation());
  C->setVarLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPUseClause(OMPUseClause *C) {
  C->setInteropVar(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
  C->setVarLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDestroyClause(OMPDestroyClause *C) {
  C->setInteropVar(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
  C->setVarLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNovariantsClause(OMPNovariantsClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setCondition(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNocontextClause(OMPNocontextClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setCondition(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPUnifiedAddressClause(OMPUnifiedAddressClause *) {}

void OMPClauseReader::VisitOMPUnifiedSharedMemoryClause(
    OMPUnifiedSharedMemoryClause *) {}

void OMPClauseReader::VisitOMPReverseOffloadClause(OMPReverseOffloadClause *) {}

void
OMPClauseReader::VisitOMPDynamicAllocatorsClause(OMPDynamicAllocatorsClause *) {
}

void OMPClauseReader::VisitOMPAtomicDefaultMemOrderClause(
    OMPAtomicDefaultMemOrderClause *C) {
  C->setAtomicDefaultMemOrderKind(
      static_cast<OpenMPAtomicDefaultMemOrderClauseKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setAtomicDefaultMemOrderKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPAtClause(OMPAtClause *C) {
  C->setAtKind(static_cast<OpenMPAtClauseKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setAtKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPSeverityClause(OMPSeverityClause *C) {
  C->setSeverityKind(static_cast<OpenMPSeverityClauseKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setSeverityKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPMessageClause(OMPMessageClause *C) {
  C->setMessageString(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPPrivateClause(OMPPrivateClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
}

void OMPClauseReader::VisitOMPFirstprivateClause(OMPFirstprivateClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setInits(Vars);
}

void OMPClauseReader::VisitOMPLastprivateClause(OMPLastprivateClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setKind(Record.readEnum<OpenMPLastprivateModifier>());
  C->setKindLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setSourceExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setDestinationExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setAssignmentOps(Vars);
}

void OMPClauseReader::VisitOMPSharedClause(OMPSharedClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPReductionClause(OMPReductionClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setModifierLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  NestedNameSpecifierLoc NNSL = Record.readNestedNameSpecifierLoc();
  DeclarationNameInfo DNI = Record.readDeclarationNameInfo();
  C->setQualifierLoc(NNSL);
  C->setNameInfo(DNI);

  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setLHSExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setRHSExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setReductionOps(Vars);
  if (C->getModifier() == OMPC_REDUCTION_inscan) {
    Vars.clear();
    for (unsigned i = 0; i != NumVars; ++i)
      Vars.push_back(Record.readSubExpr());
    C->setInscanCopyOps(Vars);
    Vars.clear();
    for (unsigned i = 0; i != NumVars; ++i)
      Vars.push_back(Record.readSubExpr());
    C->setInscanCopyArrayTemps(Vars);
    Vars.clear();
    for (unsigned i = 0; i != NumVars; ++i)
      Vars.push_back(Record.readSubExpr());
    C->setInscanCopyArrayElems(Vars);
  }
}

void OMPClauseReader::VisitOMPTaskReductionClause(OMPTaskReductionClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  NestedNameSpecifierLoc NNSL = Record.readNestedNameSpecifierLoc();
  DeclarationNameInfo DNI = Record.readDeclarationNameInfo();
  C->setQualifierLoc(NNSL);
  C->setNameInfo(DNI);

  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setLHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setRHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setReductionOps(Vars);
}

void OMPClauseReader::VisitOMPInReductionClause(OMPInReductionClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  NestedNameSpecifierLoc NNSL = Record.readNestedNameSpecifierLoc();
  DeclarationNameInfo DNI = Record.readDeclarationNameInfo();
  C->setQualifierLoc(NNSL);
  C->setNameInfo(DNI);

  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setLHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setRHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setReductionOps(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setTaskgroupDescriptors(Vars);
}

void OMPClauseReader::VisitOMPLinearClause(OMPLinearClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  C->setModifier(static_cast<OpenMPLinearClauseKind>(Record.readInt()));
  C->setModifierLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setInits(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setUpdates(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setFinals(Vars);
  C->setStep(Record.readSubExpr());
  C->setCalcStep(Record.readSubExpr());
  Vars.clear();
  for (unsigned I = 0; I != NumVars + 1; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setUsedExprs(Vars);
}

void OMPClauseReader::VisitOMPAlignedClause(OMPAlignedClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  C->setAlignment(Record.readSubExpr());
}

void OMPClauseReader::VisitOMPCopyinClause(OMPCopyinClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Exprs;
  Exprs.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setVarRefs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setSourceExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setDestinationExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setAssignmentOps(Exprs);
}

void OMPClauseReader::VisitOMPCopyprivateClause(OMPCopyprivateClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Exprs;
  Exprs.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setVarRefs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setSourceExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setDestinationExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setAssignmentOps(Exprs);
}

void OMPClauseReader::VisitOMPFlushClause(OMPFlushClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPDepobjClause(OMPDepobjClause *C) {
  C->setDepobj(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDependClause(OMPDependClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setModifier(Record.readSubExpr());
  C->setDependencyKind(
      static_cast<OpenMPDependClauseKind>(Record.readInt()));
  C->setDependencyLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  C->setOmpAllMemoryLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I)
    C->setLoopData(I, Record.readSubExpr());
}

void OMPClauseReader::VisitOMPDeviceClause(OMPDeviceClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setModifier(Record.readEnum<OpenMPDeviceClauseModifier>());
  C->setDevice(Record.readSubExpr());
  C->setModifierLoc(Record.readSourceLocation());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPMapClause(OMPMapClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  bool HasIteratorModifier = false;
  for (unsigned I = 0; I < NumberOfOMPMapClauseModifiers; ++I) {
    C->setMapTypeModifier(
        I, static_cast<OpenMPMapModifierKind>(Record.readInt()));
    C->setMapTypeModifierLoc(I, Record.readSourceLocation());
    if (C->getMapTypeModifier(I) == OMPC_MAP_MODIFIER_iterator)
      HasIteratorModifier = true;
  }
  C->setMapperQualifierLoc(Record.readNestedNameSpecifierLoc());
  C->setMapperIdInfo(Record.readDeclarationNameInfo());
  C->setMapType(
     static_cast<OpenMPMapClauseKind>(Record.readInt()));
  C->setMapLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readExpr());
  C->setVarRefs(Vars);

  SmallVector<Expr *, 16> UDMappers;
  UDMappers.reserve(NumVars);
  for (unsigned I = 0; I < NumVars; ++I)
    UDMappers.push_back(Record.readExpr());
  C->setUDMapperRefs(UDMappers);

  if (HasIteratorModifier)
    C->setIteratorModifier(Record.readExpr());

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExprPr = Record.readExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExprPr, AssociatedDecl,
                            /*IsNonContiguous=*/false);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPAllocateClause(OMPAllocateClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  C->setAllocator(Record.readSubExpr());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPNumTeamsClause(OMPNumTeamsClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setNumTeams(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPThreadLimitClause(OMPThreadLimitClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setThreadLimit(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPPriorityClause(OMPPriorityClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setPriority(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPGrainsizeClause(OMPGrainsizeClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setModifier(Record.readEnum<OpenMPGrainsizeClauseModifier>());
  C->setGrainsize(Record.readSubExpr());
  C->setModifierLoc(Record.readSourceLocation());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNumTasksClause(OMPNumTasksClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setModifier(Record.readEnum<OpenMPNumTasksClauseModifier>());
  C->setNumTasks(Record.readSubExpr());
  C->setModifierLoc(Record.readSourceLocation());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPHintClause(OMPHintClause *C) {
  C->setHint(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDistScheduleClause(OMPDistScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setDistScheduleKind(
      static_cast<OpenMPDistScheduleClauseKind>(Record.readInt()));
  C->setChunkSize(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
  C->setDistScheduleKindLoc(Record.readSourceLocation());
  C->setCommaLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDefaultmapClause(OMPDefaultmapClause *C) {
  C->setDefaultmapKind(
       static_cast<OpenMPDefaultmapClauseKind>(Record.readInt()));
  C->setDefaultmapModifier(
      static_cast<OpenMPDefaultmapClauseModifier>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setDefaultmapModifierLoc(Record.readSourceLocation());
  C->setDefaultmapKindLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPToClause(OMPToClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  for (unsigned I = 0; I < NumberOfOMPMotionModifiers; ++I) {
    C->setMotionModifier(
        I, static_cast<OpenMPMotionModifierKind>(Record.readInt()));
    C->setMotionModifierLoc(I, Record.readSourceLocation());
  }
  C->setMapperQualifierLoc(Record.readNestedNameSpecifierLoc());
  C->setMapperIdInfo(Record.readDeclarationNameInfo());
  C->setColonLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);

  SmallVector<Expr *, 16> UDMappers;
  UDMappers.reserve(NumVars);
  for (unsigned I = 0; I < NumVars; ++I)
    UDMappers.push_back(Record.readSubExpr());
  C->setUDMapperRefs(UDMappers);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExprPr = Record.readSubExpr();
    bool IsNonContiguous = Record.readBool();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExprPr, AssociatedDecl, IsNonContiguous);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPFromClause(OMPFromClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  for (unsigned I = 0; I < NumberOfOMPMotionModifiers; ++I) {
    C->setMotionModifier(
        I, static_cast<OpenMPMotionModifierKind>(Record.readInt()));
    C->setMotionModifierLoc(I, Record.readSourceLocation());
  }
  C->setMapperQualifierLoc(Record.readNestedNameSpecifierLoc());
  C->setMapperIdInfo(Record.readDeclarationNameInfo());
  C->setColonLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);

  SmallVector<Expr *, 16> UDMappers;
  UDMappers.reserve(NumVars);
  for (unsigned I = 0; I < NumVars; ++I)
    UDMappers.push_back(Record.readSubExpr());
  C->setUDMapperRefs(UDMappers);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExprPr = Record.readSubExpr();
    bool IsNonContiguous = Record.readBool();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExprPr, AssociatedDecl, IsNonContiguous);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPUseDevicePtrClause(OMPUseDevicePtrClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setInits(Vars);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    auto *AssociatedExprPr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExprPr, AssociatedDecl,
                            /*IsNonContiguous=*/false);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPUseDeviceAddrClause(OMPUseDeviceAddrClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExpr, AssociatedDecl,
                            /*IsNonContiguous*/ false);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPIsDevicePtrClause(OMPIsDevicePtrClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExpr, AssociatedDecl,
                            /*IsNonContiguous=*/false);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPHasDeviceAddrClause(OMPHasDeviceAddrClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned I = 0; I < UniqueDecls; ++I)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned I = 0; I < UniqueDecls; ++I)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned I = 0; I < TotalComponents; ++I) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.emplace_back(AssociatedExpr, AssociatedDecl,
                            /*IsNonContiguous=*/false);
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPNontemporalClause(OMPNontemporalClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateRefs(Vars);
}

void OMPClauseReader::VisitOMPInclusiveClause(OMPInclusiveClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPExclusiveClause(OMPExclusiveClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPUsesAllocatorsClause(OMPUsesAllocatorsClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumOfAllocators = C->getNumberOfAllocators();
  SmallVector<OMPUsesAllocatorsClause::Data, 4> Data;
  Data.reserve(NumOfAllocators);
  for (unsigned I = 0; I != NumOfAllocators; ++I) {
    OMPUsesAllocatorsClause::Data &D = Data.emplace_back();
    D.Allocator = Record.readSubExpr();
    D.AllocatorTraits = Record.readSubExpr();
    D.LParenLoc = Record.readSourceLocation();
    D.RParenLoc = Record.readSourceLocation();
  }
  C->setAllocatorsData(Data);
}

void OMPClauseReader::VisitOMPAffinityClause(OMPAffinityClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setModifier(Record.readSubExpr());
  C->setColonLoc(Record.readSourceLocation());
  unsigned NumOfLocators = C->varlist_size();
  SmallVector<Expr *, 4> Locators;
  Locators.reserve(NumOfLocators);
  for (unsigned I = 0; I != NumOfLocators; ++I)
    Locators.push_back(Record.readSubExpr());
  C->setVarRefs(Locators);
}

void OMPClauseReader::VisitOMPOrderClause(OMPOrderClause *C) {
  C->setKind(Record.readEnum<OpenMPOrderClauseKind>());
  C->setModifier(Record.readEnum<OpenMPOrderClauseModifier>());
  C->setLParenLoc(Record.readSourceLocation());
  C->setKindKwLoc(Record.readSourceLocation());
  C->setModifierKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPFilterClause(OMPFilterClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setThreadID(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPBindClause(OMPBindClause *C) {
  C->setBindKind(Record.readEnum<OpenMPBindClauseKind>());
  C->setLParenLoc(Record.readSourceLocation());
  C->setBindKindLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPAlignClause(OMPAlignClause *C) {
  C->setAlignment(Record.readExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPXDynCGroupMemClause(OMPXDynCGroupMemClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setSize(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDoacrossClause(OMPDoacrossClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setDependenceType(
      static_cast<OpenMPDoacrossClauseModifier>(Record.readInt()));
  C->setDependenceLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I)
    C->setLoopData(I, Record.readSubExpr());
}

void OMPClauseReader::VisitOMPXAttributeClause(OMPXAttributeClause *C) {
  AttrVec Attrs;
  Record.readAttributes(Attrs);
  C->setAttrs(Attrs);
  C->setLocStart(Record.readSourceLocation());
  C->setLParenLoc(Record.readSourceLocation());
  C->setLocEnd(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPXBareClause(OMPXBareClause *C) {}

OMPTraitInfo *ASTRecordReader::readOMPTraitInfo() {
  OMPTraitInfo &TI = getContext().getNewOMPTraitInfo();
  TI.Sets.resize(readUInt32());
  for (auto &Set : TI.Sets) {
    Set.Kind = readEnum<llvm::omp::TraitSet>();
    Set.Selectors.resize(readUInt32());
    for (auto &Selector : Set.Selectors) {
      Selector.Kind = readEnum<llvm::omp::TraitSelector>();
      Selector.ScoreOrCondition = nullptr;
      if (readBool())
        Selector.ScoreOrCondition = readExprRef();
      Selector.Properties.resize(readUInt32());
      for (auto &Property : Selector.Properties)
        Property.Kind = readEnum<llvm::omp::TraitProperty>();
    }
  }
  return &TI;
}

void ASTRecordReader::readOMPChildren(OMPChildren *Data) {
  if (!Data)
    return;
  if (Reader->ReadingKind == ASTReader::Read_Stmt) {
    // Skip NumClauses, NumChildren and HasAssociatedStmt fields.
    skipInts(3);
  }
  SmallVector<OMPClause *, 4> Clauses(Data->getNumClauses());
  for (unsigned I = 0, E = Data->getNumClauses(); I < E; ++I)
    Clauses[I] = readOMPClause();
  Data->setClauses(Clauses);
  if (Data->hasAssociatedStmt())
    Data->setAssociatedStmt(readStmt());
  for (unsigned I = 0, E = Data->getNumChildren(); I < E; ++I)
    Data->getChildren()[I] = readStmt();
}

SmallVector<Expr *> ASTRecordReader::readOpenACCVarList() {
  unsigned NumVars = readInt();
  llvm::SmallVector<Expr *> VarList;
  for (unsigned I = 0; I < NumVars; ++I)
    VarList.push_back(readSubExpr());
  return VarList;
}

SmallVector<Expr *> ASTRecordReader::readOpenACCIntExprList() {
  unsigned NumExprs = readInt();
  llvm::SmallVector<Expr *> ExprList;
  for (unsigned I = 0; I < NumExprs; ++I)
    ExprList.push_back(readSubExpr());
  return ExprList;
}

OpenACCClause *ASTRecordReader::readOpenACCClause() {
  OpenACCClauseKind ClauseKind = readEnum<OpenACCClauseKind>();
  SourceLocation BeginLoc = readSourceLocation();
  SourceLocation EndLoc = readSourceLocation();

  switch (ClauseKind) {
  case OpenACCClauseKind::Default: {
    SourceLocation LParenLoc = readSourceLocation();
    OpenACCDefaultClauseKind DCK = readEnum<OpenACCDefaultClauseKind>();
    return OpenACCDefaultClause::Create(getContext(), DCK, BeginLoc, LParenLoc,
                                        EndLoc);
  }
  case OpenACCClauseKind::If: {
    SourceLocation LParenLoc = readSourceLocation();
    Expr *CondExpr = readSubExpr();
    return OpenACCIfClause::Create(getContext(), BeginLoc, LParenLoc, CondExpr,
                                   EndLoc);
  }
  case OpenACCClauseKind::Self: {
    SourceLocation LParenLoc = readSourceLocation();
    Expr *CondExpr = readBool() ? readSubExpr() : nullptr;
    return OpenACCSelfClause::Create(getContext(), BeginLoc, LParenLoc,
                                     CondExpr, EndLoc);
  }
  case OpenACCClauseKind::NumGangs: {
    SourceLocation LParenLoc = readSourceLocation();
    unsigned NumClauses = readInt();
    llvm::SmallVector<Expr *> IntExprs;
    for (unsigned I = 0; I < NumClauses; ++I)
      IntExprs.push_back(readSubExpr());
    return OpenACCNumGangsClause::Create(getContext(), BeginLoc, LParenLoc,
                                         IntExprs, EndLoc);
  }
  case OpenACCClauseKind::NumWorkers: {
    SourceLocation LParenLoc = readSourceLocation();
    Expr *IntExpr = readSubExpr();
    return OpenACCNumWorkersClause::Create(getContext(), BeginLoc, LParenLoc,
                                           IntExpr, EndLoc);
  }
  case OpenACCClauseKind::VectorLength: {
    SourceLocation LParenLoc = readSourceLocation();
    Expr *IntExpr = readSubExpr();
    return OpenACCVectorLengthClause::Create(getContext(), BeginLoc, LParenLoc,
                                             IntExpr, EndLoc);
  }
  case OpenACCClauseKind::Private: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCPrivateClause::Create(getContext(), BeginLoc, LParenLoc,
                                        VarList, EndLoc);
  }
  case OpenACCClauseKind::FirstPrivate: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCFirstPrivateClause::Create(getContext(), BeginLoc, LParenLoc,
                                             VarList, EndLoc);
  }
  case OpenACCClauseKind::Attach: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCAttachClause::Create(getContext(), BeginLoc, LParenLoc,
                                       VarList, EndLoc);
  }
  case OpenACCClauseKind::DevicePtr: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCDevicePtrClause::Create(getContext(), BeginLoc, LParenLoc,
                                          VarList, EndLoc);
  }
  case OpenACCClauseKind::NoCreate: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCNoCreateClause::Create(getContext(), BeginLoc, LParenLoc,
                                         VarList, EndLoc);
  }
  case OpenACCClauseKind::Present: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCPresentClause::Create(getContext(), BeginLoc, LParenLoc,
                                        VarList, EndLoc);
  }
  case OpenACCClauseKind::PCopy:
  case OpenACCClauseKind::PresentOrCopy:
  case OpenACCClauseKind::Copy: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCCopyClause::Create(getContext(), ClauseKind, BeginLoc,
                                     LParenLoc, VarList, EndLoc);
  }
  case OpenACCClauseKind::CopyIn:
  case OpenACCClauseKind::PCopyIn:
  case OpenACCClauseKind::PresentOrCopyIn: {
    SourceLocation LParenLoc = readSourceLocation();
    bool IsReadOnly = readBool();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCCopyInClause::Create(getContext(), ClauseKind, BeginLoc,
                                       LParenLoc, IsReadOnly, VarList, EndLoc);
  }
  case OpenACCClauseKind::CopyOut:
  case OpenACCClauseKind::PCopyOut:
  case OpenACCClauseKind::PresentOrCopyOut: {
    SourceLocation LParenLoc = readSourceLocation();
    bool IsZero = readBool();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCCopyOutClause::Create(getContext(), ClauseKind, BeginLoc,
                                        LParenLoc, IsZero, VarList, EndLoc);
  }
  case OpenACCClauseKind::Create:
  case OpenACCClauseKind::PCreate:
  case OpenACCClauseKind::PresentOrCreate: {
    SourceLocation LParenLoc = readSourceLocation();
    bool IsZero = readBool();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCCreateClause::Create(getContext(), ClauseKind, BeginLoc,
                                       LParenLoc, IsZero, VarList, EndLoc);
  }
  case OpenACCClauseKind::Async: {
    SourceLocation LParenLoc = readSourceLocation();
    Expr *AsyncExpr = readBool() ? readSubExpr() : nullptr;
    return OpenACCAsyncClause::Create(getContext(), BeginLoc, LParenLoc,
                                      AsyncExpr, EndLoc);
  }
  case OpenACCClauseKind::Wait: {
    SourceLocation LParenLoc = readSourceLocation();
    Expr *DevNumExpr = readBool() ? readSubExpr() : nullptr;
    SourceLocation QueuesLoc = readSourceLocation();
    llvm::SmallVector<Expr *> QueueIdExprs = readOpenACCIntExprList();
    return OpenACCWaitClause::Create(getContext(), BeginLoc, LParenLoc,
                                     DevNumExpr, QueuesLoc, QueueIdExprs,
                                     EndLoc);
  }
  case OpenACCClauseKind::DeviceType:
  case OpenACCClauseKind::DType: {
    SourceLocation LParenLoc = readSourceLocation();
    llvm::SmallVector<DeviceTypeArgument> Archs;
    unsigned NumArchs = readInt();

    for (unsigned I = 0; I < NumArchs; ++I) {
      IdentifierInfo *Ident = readBool() ? readIdentifier() : nullptr;
      SourceLocation Loc = readSourceLocation();
      Archs.emplace_back(Ident, Loc);
    }

    return OpenACCDeviceTypeClause::Create(getContext(), ClauseKind, BeginLoc,
                                           LParenLoc, Archs, EndLoc);
  }
  case OpenACCClauseKind::Reduction: {
    SourceLocation LParenLoc = readSourceLocation();
    OpenACCReductionOperator Op = readEnum<OpenACCReductionOperator>();
    llvm::SmallVector<Expr *> VarList = readOpenACCVarList();
    return OpenACCReductionClause::Create(getContext(), BeginLoc, LParenLoc, Op,
                                          VarList, EndLoc);
  }
  case OpenACCClauseKind::Seq:
    return OpenACCSeqClause::Create(getContext(), BeginLoc, EndLoc);
  case OpenACCClauseKind::Independent:
    return OpenACCIndependentClause::Create(getContext(), BeginLoc, EndLoc);
  case OpenACCClauseKind::Auto:
    return OpenACCAutoClause::Create(getContext(), BeginLoc, EndLoc);

  case OpenACCClauseKind::Finalize:
  case OpenACCClauseKind::IfPresent:
  case OpenACCClauseKind::Worker:
  case OpenACCClauseKind::Vector:
  case OpenACCClauseKind::NoHost:
  case OpenACCClauseKind::UseDevice:
  case OpenACCClauseKind::Delete:
  case OpenACCClauseKind::Detach:
  case OpenACCClauseKind::Device:
  case OpenACCClauseKind::DeviceResident:
  case OpenACCClauseKind::Host:
  case OpenACCClauseKind::Link:
  case OpenACCClauseKind::Collapse:
  case OpenACCClauseKind::Bind:
  case OpenACCClauseKind::DeviceNum:
  case OpenACCClauseKind::DefaultAsync:
  case OpenACCClauseKind::Tile:
  case OpenACCClauseKind::Gang:
  case OpenACCClauseKind::Invalid:
    llvm_unreachable("Clause serialization not yet implemented");
  }
  llvm_unreachable("Invalid Clause Kind");
}

void ASTRecordReader::readOpenACCClauseList(
    MutableArrayRef<const OpenACCClause *> Clauses) {
  for (unsigned I = 0; I < Clauses.size(); ++I)
    Clauses[I] = readOpenACCClause();
}
