//===- ASTUnit.cpp - ASTUnit utility --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ASTUnit Implementation.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/ASTUnit.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/PrecompiledPreamble.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;

using llvm::TimeRecord;

namespace {

  class SimpleTimer {
    bool WantTiming;
    TimeRecord Start;
    std::string Output;

  public:
    explicit SimpleTimer(bool WantTiming) : WantTiming(WantTiming) {
      if (WantTiming)
        Start = TimeRecord::getCurrentTime();
    }

    ~SimpleTimer() {
      if (WantTiming) {
        TimeRecord Elapsed = TimeRecord::getCurrentTime();
        Elapsed -= Start;
        llvm::errs() << Output << ':';
        Elapsed.print(Elapsed, llvm::errs());
        llvm::errs() << '\n';
      }
    }

    void setOutput(const Twine &Output) {
      if (WantTiming)
        this->Output = Output.str();
    }
  };

} // namespace

template <class T>
static std::unique_ptr<T> valueOrNull(llvm::ErrorOr<std::unique_ptr<T>> Val) {
  if (!Val)
    return nullptr;
  return std::move(*Val);
}

template <class T>
static bool moveOnNoError(llvm::ErrorOr<T> Val, T &Output) {
  if (!Val)
    return false;
  Output = std::move(*Val);
  return true;
}

/// Get a source buffer for \p MainFilePath, handling all file-to-file
/// and file-to-buffer remappings inside \p Invocation.
static std::unique_ptr<llvm::MemoryBuffer>
getBufferForFileHandlingRemapping(const CompilerInvocation &Invocation,
                                  llvm::vfs::FileSystem *VFS,
                                  StringRef FilePath, bool isVolatile) {
  const auto &PreprocessorOpts = Invocation.getPreprocessorOpts();

  // Try to determine if the main file has been remapped, either from the
  // command line (to another file) or directly through the compiler
  // invocation (to a memory buffer).
  llvm::MemoryBuffer *Buffer = nullptr;
  std::unique_ptr<llvm::MemoryBuffer> BufferOwner;
  auto FileStatus = VFS->status(FilePath);
  if (FileStatus) {
    llvm::sys::fs::UniqueID MainFileID = FileStatus->getUniqueID();

    // Check whether there is a file-file remapping of the main file
    for (const auto &RF : PreprocessorOpts.RemappedFiles) {
      std::string MPath(RF.first);
      auto MPathStatus = VFS->status(MPath);
      if (MPathStatus) {
        llvm::sys::fs::UniqueID MID = MPathStatus->getUniqueID();
        if (MainFileID == MID) {
          // We found a remapping. Try to load the resulting, remapped source.
          BufferOwner = valueOrNull(VFS->getBufferForFile(RF.second, -1, true, isVolatile));
          if (!BufferOwner)
            return nullptr;
        }
      }
    }

    // Check whether there is a file-buffer remapping. It supercedes the
    // file-file remapping.
    for (const auto &RB : PreprocessorOpts.RemappedFileBuffers) {
      std::string MPath(RB.first);
      auto MPathStatus = VFS->status(MPath);
      if (MPathStatus) {
        llvm::sys::fs::UniqueID MID = MPathStatus->getUniqueID();
        if (MainFileID == MID) {
          // We found a remapping.
          BufferOwner.reset();
          Buffer = const_cast<llvm::MemoryBuffer *>(RB.second);
        }
      }
    }
  }

  // If the main source file was not remapped, load it now.
  if (!Buffer && !BufferOwner) {
    BufferOwner = valueOrNull(VFS->getBufferForFile(FilePath, -1, true, isVolatile));
    if (!BufferOwner)
      return nullptr;
  }

  if (BufferOwner)
    return BufferOwner;
  if (!Buffer)
    return nullptr;
  return llvm::MemoryBuffer::getMemBufferCopy(Buffer->getBuffer(), FilePath);
}

struct ASTUnit::ASTWriterData {
  SmallString<128> Buffer;
  llvm::BitstreamWriter Stream;
  ASTWriter Writer;

  ASTWriterData(InMemoryModuleCache &ModuleCache)
      : Stream(Buffer), Writer(Stream, Buffer, ModuleCache, {}) {}
};

void ASTUnit::clearFileLevelDecls() {
  FileDecls.clear();
}

/// After failing to build a precompiled preamble (due to
/// errors in the source that occurs in the preamble), the number of
/// reparses during which we'll skip even trying to precompile the
/// preamble.
const unsigned DefaultPreambleRebuildInterval = 5;

/// Tracks the number of ASTUnit objects that are currently active.
///
/// Used for debugging purposes only.
static std::atomic<unsigned> ActiveASTUnitObjects;

ASTUnit::ASTUnit(bool _MainFileIsAST)
    : MainFileIsAST(_MainFileIsAST), WantTiming(getenv("LIBCLANG_TIMING")),
      ShouldCacheCodeCompletionResults(false),
      IncludeBriefCommentsInCodeCompletion(false), UserFilesAreVolatile(false),
      UnsafeToFree(false) {
  if (getenv("LIBCLANG_OBJTRACKING"))
    fprintf(stderr, "+++ %u translation units\n", ++ActiveASTUnitObjects);
}

ASTUnit::~ASTUnit() {
  // If we loaded from an AST file, balance out the BeginSourceFile call.
  if (MainFileIsAST && getDiagnostics().getClient()) {
    getDiagnostics().getClient()->EndSourceFile();
  }

  clearFileLevelDecls();

  // Free the buffers associated with remapped files. We are required to
  // perform this operation here because we explicitly request that the
  // compiler instance *not* free these buffers for each invocation of the
  // parser.
  if (Invocation && OwnsRemappedFileBuffers) {
    PreprocessorOptions &PPOpts = Invocation->getPreprocessorOpts();
    for (const auto &RB : PPOpts.RemappedFileBuffers)
      delete RB.second;
  }

  ClearCachedCompletionResults();

  if (getenv("LIBCLANG_OBJTRACKING"))
    fprintf(stderr, "--- %u translation units\n", --ActiveASTUnitObjects);
}

void ASTUnit::setPreprocessor(std::shared_ptr<Preprocessor> PP) {
  this->PP = std::move(PP);
}

void ASTUnit::enableSourceFileDiagnostics() {
  assert(getDiagnostics().getClient() && Ctx &&
      "Bad context for source file");
  getDiagnostics().getClient()->BeginSourceFile(Ctx->getLangOpts(), PP.get());
}

/// Determine the set of code-completion contexts in which this
/// declaration should be shown.
static uint64_t getDeclShowContexts(const NamedDecl *ND,
                                    const LangOptions &LangOpts,
                                    bool &IsNestedNameSpecifier) {
  IsNestedNameSpecifier = false;

  if (isa<UsingShadowDecl>(ND))
    ND = ND->getUnderlyingDecl();
  if (!ND)
    return 0;

  uint64_t Contexts = 0;
  if (isa<TypeDecl>(ND) || isa<ObjCInterfaceDecl>(ND) ||
      isa<ClassTemplateDecl>(ND) || isa<TemplateTemplateParmDecl>(ND) ||
      isa<TypeAliasTemplateDecl>(ND)) {
    // Types can appear in these contexts.
    if (LangOpts.CPlusPlus || !isa<TagDecl>(ND))
      Contexts |= (1LL << CodeCompletionContext::CCC_TopLevel)
               |  (1LL << CodeCompletionContext::CCC_ObjCIvarList)
               |  (1LL << CodeCompletionContext::CCC_ClassStructUnion)
               |  (1LL << CodeCompletionContext::CCC_Statement)
               |  (1LL << CodeCompletionContext::CCC_Type)
               |  (1LL << CodeCompletionContext::CCC_ParenthesizedExpression);

    // In C++, types can appear in expressions contexts (for functional casts).
    if (LangOpts.CPlusPlus)
      Contexts |= (1LL << CodeCompletionContext::CCC_Expression);

    // In Objective-C, message sends can send interfaces. In Objective-C++,
    // all types are available due to functional casts.
    if (LangOpts.CPlusPlus || isa<ObjCInterfaceDecl>(ND))
      Contexts |= (1LL << CodeCompletionContext::CCC_ObjCMessageReceiver);

    // In Objective-C, you can only be a subclass of another Objective-C class
    if (const auto *ID = dyn_cast<ObjCInterfaceDecl>(ND)) {
      // Objective-C interfaces can be used in a class property expression.
      if (ID->getDefinition())
        Contexts |= (1LL << CodeCompletionContext::CCC_Expression);
      Contexts |= (1LL << CodeCompletionContext::CCC_ObjCInterfaceName);
      Contexts |= (1LL << CodeCompletionContext::CCC_ObjCClassForwardDecl);
    }

    // Deal with tag names.
    if (isa<EnumDecl>(ND)) {
      Contexts |= (1LL << CodeCompletionContext::CCC_EnumTag);

      // Part of the nested-name-specifier in C++0x.
      if (LangOpts.CPlusPlus11)
        IsNestedNameSpecifier = true;
    } else if (const auto *Record = dyn_cast<RecordDecl>(ND)) {
      if (Record->isUnion())
        Contexts |= (1LL << CodeCompletionContext::CCC_UnionTag);
      else
        Contexts |= (1LL << CodeCompletionContext::CCC_ClassOrStructTag);

      if (LangOpts.CPlusPlus)
        IsNestedNameSpecifier = true;
    } else if (isa<ClassTemplateDecl>(ND))
      IsNestedNameSpecifier = true;
  } else if (isa<ValueDecl>(ND) || isa<FunctionTemplateDecl>(ND)) {
    // Values can appear in these contexts.
    Contexts = (1LL << CodeCompletionContext::CCC_Statement)
             | (1LL << CodeCompletionContext::CCC_Expression)
             | (1LL << CodeCompletionContext::CCC_ParenthesizedExpression)
             | (1LL << CodeCompletionContext::CCC_ObjCMessageReceiver);
  } else if (isa<ObjCProtocolDecl>(ND)) {
    Contexts = (1LL << CodeCompletionContext::CCC_ObjCProtocolName);
  } else if (isa<ObjCCategoryDecl>(ND)) {
    Contexts = (1LL << CodeCompletionContext::CCC_ObjCCategoryName);
  } else if (isa<NamespaceDecl>(ND) || isa<NamespaceAliasDecl>(ND)) {
    Contexts = (1LL << CodeCompletionContext::CCC_Namespace);

    // Part of the nested-name-specifier.
    IsNestedNameSpecifier = true;
  }

  return Contexts;
}

void ASTUnit::CacheCodeCompletionResults() {
  if (!TheSema)
    return;

  SimpleTimer Timer(WantTiming);
  Timer.setOutput("Cache global code completions for " + getMainFileName());

  // Clear out the previous results.
  ClearCachedCompletionResults();

  // Gather the set of global code completions.
  using Result = CodeCompletionResult;
  SmallVector<Result, 8> Results;
  CachedCompletionAllocator = std::make_shared<GlobalCodeCompletionAllocator>();
  CodeCompletionTUInfo CCTUInfo(CachedCompletionAllocator);
  TheSema->CodeCompletion().GatherGlobalCodeCompletions(
      *CachedCompletionAllocator, CCTUInfo, Results);

  // Translate global code completions into cached completions.
  llvm::DenseMap<CanQualType, unsigned> CompletionTypes;
  CodeCompletionContext CCContext(CodeCompletionContext::CCC_TopLevel);

  for (auto &R : Results) {
    switch (R.Kind) {
    case Result::RK_Declaration: {
      bool IsNestedNameSpecifier = false;
      CachedCodeCompletionResult CachedResult;
      CachedResult.Completion = R.CreateCodeCompletionString(
          *TheSema, CCContext, *CachedCompletionAllocator, CCTUInfo,
          IncludeBriefCommentsInCodeCompletion);
      CachedResult.ShowInContexts = getDeclShowContexts(
          R.Declaration, Ctx->getLangOpts(), IsNestedNameSpecifier);
      CachedResult.Priority = R.Priority;
      CachedResult.Kind = R.CursorKind;
      CachedResult.Availability = R.Availability;

      // Keep track of the type of this completion in an ASTContext-agnostic
      // way.
      QualType UsageType = getDeclUsageType(*Ctx, R.Declaration);
      if (UsageType.isNull()) {
        CachedResult.TypeClass = STC_Void;
        CachedResult.Type = 0;
      } else {
        CanQualType CanUsageType
          = Ctx->getCanonicalType(UsageType.getUnqualifiedType());
        CachedResult.TypeClass = getSimplifiedTypeClass(CanUsageType);

        // Determine whether we have already seen this type. If so, we save
        // ourselves the work of formatting the type string by using the
        // temporary, CanQualType-based hash table to find the associated value.
        unsigned &TypeValue = CompletionTypes[CanUsageType];
        if (TypeValue == 0) {
          TypeValue = CompletionTypes.size();
          CachedCompletionTypes[QualType(CanUsageType).getAsString()]
            = TypeValue;
        }

        CachedResult.Type = TypeValue;
      }

      CachedCompletionResults.push_back(CachedResult);

      /// Handle nested-name-specifiers in C++.
      if (TheSema->Context.getLangOpts().CPlusPlus && IsNestedNameSpecifier &&
          !R.StartsNestedNameSpecifier) {
        // The contexts in which a nested-name-specifier can appear in C++.
        uint64_t NNSContexts
          = (1LL << CodeCompletionContext::CCC_TopLevel)
          | (1LL << CodeCompletionContext::CCC_ObjCIvarList)
          | (1LL << CodeCompletionContext::CCC_ClassStructUnion)
          | (1LL << CodeCompletionContext::CCC_Statement)
          | (1LL << CodeCompletionContext::CCC_Expression)
          | (1LL << CodeCompletionContext::CCC_ObjCMessageReceiver)
          | (1LL << CodeCompletionContext::CCC_EnumTag)
          | (1LL << CodeCompletionContext::CCC_UnionTag)
          | (1LL << CodeCompletionContext::CCC_ClassOrStructTag)
          | (1LL << CodeCompletionContext::CCC_Type)
          | (1LL << CodeCompletionContext::CCC_SymbolOrNewName)
          | (1LL << CodeCompletionContext::CCC_ParenthesizedExpression);

        if (isa<NamespaceDecl>(R.Declaration) ||
            isa<NamespaceAliasDecl>(R.Declaration))
          NNSContexts |= (1LL << CodeCompletionContext::CCC_Namespace);

        if (uint64_t RemainingContexts
                                = NNSContexts & ~CachedResult.ShowInContexts) {
          // If there any contexts where this completion can be a
          // nested-name-specifier but isn't already an option, create a
          // nested-name-specifier completion.
          R.StartsNestedNameSpecifier = true;
          CachedResult.Completion = R.CreateCodeCompletionString(
              *TheSema, CCContext, *CachedCompletionAllocator, CCTUInfo,
              IncludeBriefCommentsInCodeCompletion);
          CachedResult.ShowInContexts = RemainingContexts;
          CachedResult.Priority = CCP_NestedNameSpecifier;
          CachedResult.TypeClass = STC_Void;
          CachedResult.Type = 0;
          CachedCompletionResults.push_back(CachedResult);
        }
      }
      break;
    }

    case Result::RK_Keyword:
    case Result::RK_Pattern:
      // Ignore keywords and patterns; we don't care, since they are so
      // easily regenerated.
      break;

    case Result::RK_Macro: {
      CachedCodeCompletionResult CachedResult;
      CachedResult.Completion = R.CreateCodeCompletionString(
          *TheSema, CCContext, *CachedCompletionAllocator, CCTUInfo,
          IncludeBriefCommentsInCodeCompletion);
      CachedResult.ShowInContexts
        = (1LL << CodeCompletionContext::CCC_TopLevel)
        | (1LL << CodeCompletionContext::CCC_ObjCInterface)
        | (1LL << CodeCompletionContext::CCC_ObjCImplementation)
        | (1LL << CodeCompletionContext::CCC_ObjCIvarList)
        | (1LL << CodeCompletionContext::CCC_ClassStructUnion)
        | (1LL << CodeCompletionContext::CCC_Statement)
        | (1LL << CodeCompletionContext::CCC_Expression)
        | (1LL << CodeCompletionContext::CCC_ObjCMessageReceiver)
        | (1LL << CodeCompletionContext::CCC_MacroNameUse)
        | (1LL << CodeCompletionContext::CCC_PreprocessorExpression)
        | (1LL << CodeCompletionContext::CCC_ParenthesizedExpression)
        | (1LL << CodeCompletionContext::CCC_OtherWithMacros);

      CachedResult.Priority = R.Priority;
      CachedResult.Kind = R.CursorKind;
      CachedResult.Availability = R.Availability;
      CachedResult.TypeClass = STC_Void;
      CachedResult.Type = 0;
      CachedCompletionResults.push_back(CachedResult);
      break;
    }
    }
  }

  // Save the current top-level hash value.
  CompletionCacheTopLevelHashValue = CurrentTopLevelHashValue;
}

void ASTUnit::ClearCachedCompletionResults() {
  CachedCompletionResults.clear();
  CachedCompletionTypes.clear();
  CachedCompletionAllocator = nullptr;
}

namespace {

/// Gathers information from ASTReader that will be used to initialize
/// a Preprocessor.
class ASTInfoCollector : public ASTReaderListener {
  Preprocessor &PP;
  ASTContext *Context;
  HeaderSearchOptions &HSOpts;
  PreprocessorOptions &PPOpts;
  LangOptions &LangOpt;
  std::shared_ptr<TargetOptions> &TargetOpts;
  IntrusiveRefCntPtr<TargetInfo> &Target;
  unsigned &Counter;
  bool InitializedLanguage = false;
  bool InitializedHeaderSearchPaths = false;

public:
  ASTInfoCollector(Preprocessor &PP, ASTContext *Context,
                   HeaderSearchOptions &HSOpts, PreprocessorOptions &PPOpts,
                   LangOptions &LangOpt,
                   std::shared_ptr<TargetOptions> &TargetOpts,
                   IntrusiveRefCntPtr<TargetInfo> &Target, unsigned &Counter)
      : PP(PP), Context(Context), HSOpts(HSOpts), PPOpts(PPOpts),
        LangOpt(LangOpt), TargetOpts(TargetOpts), Target(Target),
        Counter(Counter) {}

  bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                           bool AllowCompatibleDifferences) override {
    if (InitializedLanguage)
      return false;

    // FIXME: We did similar things in ReadHeaderSearchOptions too. But such
    // style is not scaling. Probably we need to invite some mechanism to
    // handle such patterns generally.
    auto PICLevel = LangOpt.PICLevel;
    auto PIE = LangOpt.PIE;

    LangOpt = LangOpts;

    LangOpt.PICLevel = PICLevel;
    LangOpt.PIE = PIE;

    InitializedLanguage = true;

    updated();
    return false;
  }

  bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                               StringRef SpecificModuleCachePath,
                               bool Complain) override {
    // llvm::SaveAndRestore doesn't support bit field.
    auto ForceCheckCXX20ModulesInputFiles =
        this->HSOpts.ForceCheckCXX20ModulesInputFiles;
    llvm::SaveAndRestore X(this->HSOpts.UserEntries);
    llvm::SaveAndRestore Y(this->HSOpts.SystemHeaderPrefixes);
    llvm::SaveAndRestore Z(this->HSOpts.VFSOverlayFiles);

    this->HSOpts = HSOpts;
    this->HSOpts.ForceCheckCXX20ModulesInputFiles =
        ForceCheckCXX20ModulesInputFiles;

    return false;
  }

  bool ReadHeaderSearchPaths(const HeaderSearchOptions &HSOpts,
                             bool Complain) override {
    if (InitializedHeaderSearchPaths)
      return false;

    this->HSOpts.UserEntries = HSOpts.UserEntries;
    this->HSOpts.SystemHeaderPrefixes = HSOpts.SystemHeaderPrefixes;
    this->HSOpts.VFSOverlayFiles = HSOpts.VFSOverlayFiles;

    // Initialize the FileManager. We can't do this in update(), since that
    // performs the initialization too late (once both target and language
    // options are read).
    PP.getFileManager().setVirtualFileSystem(createVFSFromOverlayFiles(
        HSOpts.VFSOverlayFiles, PP.getDiagnostics(),
        PP.getFileManager().getVirtualFileSystemPtr()));

    InitializedHeaderSearchPaths = true;

    return false;
  }

  bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                               bool ReadMacros, bool Complain,
                               std::string &SuggestedPredefines) override {
    this->PPOpts = PPOpts;
    return false;
  }

  bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                         bool AllowCompatibleDifferences) override {
    // If we've already initialized the target, don't do it again.
    if (Target)
      return false;

    this->TargetOpts = std::make_shared<TargetOptions>(TargetOpts);
    Target =
        TargetInfo::CreateTargetInfo(PP.getDiagnostics(), this->TargetOpts);

    updated();
    return false;
  }

  void ReadCounter(const serialization::ModuleFile &M,
                   unsigned Value) override {
    Counter = Value;
  }

private:
  void updated() {
    if (!Target || !InitializedLanguage)
      return;

    // Inform the target of the language options.
    //
    // FIXME: We shouldn't need to do this, the target should be immutable once
    // created. This complexity should be lifted elsewhere.
    Target->adjust(PP.getDiagnostics(), LangOpt);

    // Initialize the preprocessor.
    PP.Initialize(*Target);

    if (!Context)
      return;

    // Initialize the ASTContext
    Context->InitBuiltinTypes(*Target);

    // Adjust printing policy based on language options.
    Context->setPrintingPolicy(PrintingPolicy(LangOpt));

    // We didn't have access to the comment options when the ASTContext was
    // constructed, so register them now.
    Context->getCommentCommandTraits().registerCommentOptions(
        LangOpt.CommentOpts);
  }
};

/// Diagnostic consumer that saves each diagnostic it is given.
class FilterAndStoreDiagnosticConsumer : public DiagnosticConsumer {
  SmallVectorImpl<StoredDiagnostic> *StoredDiags;
  SmallVectorImpl<ASTUnit::StandaloneDiagnostic> *StandaloneDiags;
  bool CaptureNonErrorsFromIncludes = true;
  const LangOptions *LangOpts = nullptr;
  SourceManager *SourceMgr = nullptr;

public:
  FilterAndStoreDiagnosticConsumer(
      SmallVectorImpl<StoredDiagnostic> *StoredDiags,
      SmallVectorImpl<ASTUnit::StandaloneDiagnostic> *StandaloneDiags,
      bool CaptureNonErrorsFromIncludes)
      : StoredDiags(StoredDiags), StandaloneDiags(StandaloneDiags),
        CaptureNonErrorsFromIncludes(CaptureNonErrorsFromIncludes) {
    assert((StoredDiags || StandaloneDiags) &&
           "No output collections were passed to StoredDiagnosticConsumer.");
  }

  void BeginSourceFile(const LangOptions &LangOpts,
                       const Preprocessor *PP = nullptr) override {
    this->LangOpts = &LangOpts;
    if (PP)
      SourceMgr = &PP->getSourceManager();
  }

  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &Info) override;
};

/// RAII object that optionally captures and filters diagnostics, if
/// there is no diagnostic client to capture them already.
class CaptureDroppedDiagnostics {
  DiagnosticsEngine &Diags;
  FilterAndStoreDiagnosticConsumer Client;
  DiagnosticConsumer *PreviousClient = nullptr;
  std::unique_ptr<DiagnosticConsumer> OwningPreviousClient;

public:
  CaptureDroppedDiagnostics(
      CaptureDiagsKind CaptureDiagnostics, DiagnosticsEngine &Diags,
      SmallVectorImpl<StoredDiagnostic> *StoredDiags,
      SmallVectorImpl<ASTUnit::StandaloneDiagnostic> *StandaloneDiags)
      : Diags(Diags),
        Client(StoredDiags, StandaloneDiags,
               CaptureDiagnostics !=
                   CaptureDiagsKind::AllWithoutNonErrorsFromIncludes) {
    if (CaptureDiagnostics != CaptureDiagsKind::None ||
        Diags.getClient() == nullptr) {
      OwningPreviousClient = Diags.takeClient();
      PreviousClient = Diags.getClient();
      Diags.setClient(&Client, false);
    }
  }

  ~CaptureDroppedDiagnostics() {
    if (Diags.getClient() == &Client)
      Diags.setClient(PreviousClient, !!OwningPreviousClient.release());
  }
};

} // namespace

static ASTUnit::StandaloneDiagnostic
makeStandaloneDiagnostic(const LangOptions &LangOpts,
                         const StoredDiagnostic &InDiag);

static bool isInMainFile(const clang::Diagnostic &D) {
  if (!D.hasSourceManager() || !D.getLocation().isValid())
    return false;

  auto &M = D.getSourceManager();
  return M.isWrittenInMainFile(M.getExpansionLoc(D.getLocation()));
}

void FilterAndStoreDiagnosticConsumer::HandleDiagnostic(
    DiagnosticsEngine::Level Level, const Diagnostic &Info) {
  // Default implementation (Warnings/errors count).
  DiagnosticConsumer::HandleDiagnostic(Level, Info);

  // Only record the diagnostic if it's part of the source manager we know
  // about. This effectively drops diagnostics from modules we're building.
  // FIXME: In the long run, ee don't want to drop source managers from modules.
  if (!Info.hasSourceManager() || &Info.getSourceManager() == SourceMgr) {
    if (!CaptureNonErrorsFromIncludes && Level <= DiagnosticsEngine::Warning &&
        !isInMainFile(Info)) {
      return;
    }

    StoredDiagnostic *ResultDiag = nullptr;
    if (StoredDiags) {
      StoredDiags->emplace_back(Level, Info);
      ResultDiag = &StoredDiags->back();
    }

    if (StandaloneDiags) {
      std::optional<StoredDiagnostic> StoredDiag;
      if (!ResultDiag) {
        StoredDiag.emplace(Level, Info);
        ResultDiag = &*StoredDiag;
      }
      StandaloneDiags->push_back(
          makeStandaloneDiagnostic(*LangOpts, *ResultDiag));
    }
  }
}

IntrusiveRefCntPtr<ASTReader> ASTUnit::getASTReader() const {
  return Reader;
}

ASTMutationListener *ASTUnit::getASTMutationListener() {
  if (WriterData)
    return &WriterData->Writer;
  return nullptr;
}

ASTDeserializationListener *ASTUnit::getDeserializationListener() {
  if (WriterData)
    return &WriterData->Writer;
  return nullptr;
}

std::unique_ptr<llvm::MemoryBuffer>
ASTUnit::getBufferForFile(StringRef Filename, std::string *ErrorStr) {
  assert(FileMgr);
  auto Buffer = FileMgr->getBufferForFile(Filename, UserFilesAreVolatile);
  if (Buffer)
    return std::move(*Buffer);
  if (ErrorStr)
    *ErrorStr = Buffer.getError().message();
  return nullptr;
}

/// Configure the diagnostics object for use with ASTUnit.
void ASTUnit::ConfigureDiags(IntrusiveRefCntPtr<DiagnosticsEngine> Diags,
                             ASTUnit &AST,
                             CaptureDiagsKind CaptureDiagnostics) {
  assert(Diags.get() && "no DiagnosticsEngine was provided");
  if (CaptureDiagnostics != CaptureDiagsKind::None)
    Diags->setClient(new FilterAndStoreDiagnosticConsumer(
        &AST.StoredDiagnostics, nullptr,
        CaptureDiagnostics != CaptureDiagsKind::AllWithoutNonErrorsFromIncludes));
}

std::unique_ptr<ASTUnit> ASTUnit::LoadFromASTFile(
    const std::string &Filename, const PCHContainerReader &PCHContainerRdr,
    WhatToLoad ToLoad, IntrusiveRefCntPtr<DiagnosticsEngine> Diags,
    const FileSystemOptions &FileSystemOpts,
    std::shared_ptr<HeaderSearchOptions> HSOpts,
    std::shared_ptr<LangOptions> LangOpts, bool OnlyLocalDecls,
    CaptureDiagsKind CaptureDiagnostics, bool AllowASTWithCompilerErrors,
    bool UserFilesAreVolatile, IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  std::unique_ptr<ASTUnit> AST(new ASTUnit(true));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<ASTUnit>
    ASTUnitCleanup(AST.get());
  llvm::CrashRecoveryContextCleanupRegistrar<DiagnosticsEngine,
    llvm::CrashRecoveryContextReleaseRefCleanup<DiagnosticsEngine>>
    DiagCleanup(Diags.get());

  ConfigureDiags(Diags, *AST, CaptureDiagnostics);

  AST->LangOpts = LangOpts ? LangOpts : std::make_shared<LangOptions>();
  AST->OnlyLocalDecls = OnlyLocalDecls;
  AST->CaptureDiagnostics = CaptureDiagnostics;
  AST->Diagnostics = Diags;
  AST->FileMgr = new FileManager(FileSystemOpts, VFS);
  AST->UserFilesAreVolatile = UserFilesAreVolatile;
  AST->SourceMgr = new SourceManager(AST->getDiagnostics(),
                                     AST->getFileManager(),
                                     UserFilesAreVolatile);
  AST->ModuleCache = new InMemoryModuleCache;
  AST->HSOpts = HSOpts ? HSOpts : std::make_shared<HeaderSearchOptions>();
  AST->HSOpts->ModuleFormat = std::string(PCHContainerRdr.getFormats().front());
  AST->HeaderInfo.reset(new HeaderSearch(AST->HSOpts,
                                         AST->getSourceManager(),
                                         AST->getDiagnostics(),
                                         AST->getLangOpts(),
                                         /*Target=*/nullptr));
  AST->PPOpts = std::make_shared<PreprocessorOptions>();

  // Gather Info for preprocessor construction later on.

  HeaderSearch &HeaderInfo = *AST->HeaderInfo;

  AST->PP = std::make_shared<Preprocessor>(
      AST->PPOpts, AST->getDiagnostics(), *AST->LangOpts,
      AST->getSourceManager(), HeaderInfo, AST->ModuleLoader,
      /*IILookup=*/nullptr,
      /*OwnsHeaderSearch=*/false);
  Preprocessor &PP = *AST->PP;

  if (ToLoad >= LoadASTOnly)
    AST->Ctx = new ASTContext(*AST->LangOpts, AST->getSourceManager(),
                              PP.getIdentifierTable(), PP.getSelectorTable(),
                              PP.getBuiltinInfo(),
                              AST->getTranslationUnitKind());

  DisableValidationForModuleKind disableValid =
      DisableValidationForModuleKind::None;
  if (::getenv("LIBCLANG_DISABLE_PCH_VALIDATION"))
    disableValid = DisableValidationForModuleKind::All;
  AST->Reader = new ASTReader(
      PP, *AST->ModuleCache, AST->Ctx.get(), PCHContainerRdr, {},
      /*isysroot=*/"",
      /*DisableValidationKind=*/disableValid, AllowASTWithCompilerErrors);

  unsigned Counter = 0;
  AST->Reader->setListener(std::make_unique<ASTInfoCollector>(
      *AST->PP, AST->Ctx.get(), *AST->HSOpts, *AST->PPOpts, *AST->LangOpts,
      AST->TargetOpts, AST->Target, Counter));

  // Attach the AST reader to the AST context as an external AST
  // source, so that declarations will be deserialized from the
  // AST file as needed.
  // We need the external source to be set up before we read the AST, because
  // eagerly-deserialized declarations may use it.
  if (AST->Ctx)
    AST->Ctx->setExternalSource(AST->Reader);

  switch (AST->Reader->ReadAST(Filename, serialization::MK_MainFile,
                               SourceLocation(), ASTReader::ARR_None)) {
  case ASTReader::Success:
    break;

  case ASTReader::Failure:
  case ASTReader::Missing:
  case ASTReader::OutOfDate:
  case ASTReader::VersionMismatch:
  case ASTReader::ConfigurationMismatch:
  case ASTReader::HadErrors:
    AST->getDiagnostics().Report(diag::err_fe_unable_to_load_pch);
    return nullptr;
  }

  AST->OriginalSourceFile = std::string(AST->Reader->getOriginalSourceFile());

  PP.setCounterValue(Counter);

  Module *M = HeaderInfo.lookupModule(AST->getLangOpts().CurrentModule);
  if (M && AST->getLangOpts().isCompilingModule() && M->isNamedModule())
    AST->Ctx->setCurrentNamedModule(M);

  // Create an AST consumer, even though it isn't used.
  if (ToLoad >= LoadASTOnly)
    AST->Consumer.reset(new ASTConsumer);

  // Create a semantic analysis object and tell the AST reader about it.
  if (ToLoad >= LoadEverything) {
    AST->TheSema.reset(new Sema(PP, *AST->Ctx, *AST->Consumer));
    AST->TheSema->Initialize();
    AST->Reader->InitializeSema(*AST->TheSema);
  }

  // Tell the diagnostic client that we have started a source file.
  AST->getDiagnostics().getClient()->BeginSourceFile(PP.getLangOpts(), &PP);

  return AST;
}

/// Add the given macro to the hash of all top-level entities.
static void AddDefinedMacroToHash(const Token &MacroNameTok, unsigned &Hash) {
  Hash = llvm::djbHash(MacroNameTok.getIdentifierInfo()->getName(), Hash);
}

namespace {

/// Preprocessor callback class that updates a hash value with the names
/// of all macros that have been defined by the translation unit.
class MacroDefinitionTrackerPPCallbacks : public PPCallbacks {
  unsigned &Hash;

public:
  explicit MacroDefinitionTrackerPPCallbacks(unsigned &Hash) : Hash(Hash) {}

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override {
    AddDefinedMacroToHash(MacroNameTok, Hash);
  }
};

} // namespace

/// Add the given declaration to the hash of all top-level entities.
static void AddTopLevelDeclarationToHash(Decl *D, unsigned &Hash) {
  if (!D)
    return;

  DeclContext *DC = D->getDeclContext();
  if (!DC)
    return;

  if (!(DC->isTranslationUnit() || DC->getLookupParent()->isTranslationUnit()))
    return;

  if (const auto *ND = dyn_cast<NamedDecl>(D)) {
    if (const auto *EnumD = dyn_cast<EnumDecl>(D)) {
      // For an unscoped enum include the enumerators in the hash since they
      // enter the top-level namespace.
      if (!EnumD->isScoped()) {
        for (const auto *EI : EnumD->enumerators()) {
          if (EI->getIdentifier())
            Hash = llvm::djbHash(EI->getIdentifier()->getName(), Hash);
        }
      }
    }

    if (ND->getIdentifier())
      Hash = llvm::djbHash(ND->getIdentifier()->getName(), Hash);
    else if (DeclarationName Name = ND->getDeclName()) {
      std::string NameStr = Name.getAsString();
      Hash = llvm::djbHash(NameStr, Hash);
    }
    return;
  }

  if (const auto *ImportD = dyn_cast<ImportDecl>(D)) {
    if (const Module *Mod = ImportD->getImportedModule()) {
      std::string ModName = Mod->getFullModuleName();
      Hash = llvm::djbHash(ModName, Hash);
    }
    return;
  }
}

namespace {

class TopLevelDeclTrackerConsumer : public ASTConsumer {
  ASTUnit &Unit;
  unsigned &Hash;

public:
  TopLevelDeclTrackerConsumer(ASTUnit &_Unit, unsigned &Hash)
      : Unit(_Unit), Hash(Hash) {
    Hash = 0;
  }

  void handleTopLevelDecl(Decl *D) {
    if (!D)
      return;

    // FIXME: Currently ObjC method declarations are incorrectly being
    // reported as top-level declarations, even though their DeclContext
    // is the containing ObjC @interface/@implementation.  This is a
    // fundamental problem in the parser right now.
    if (isa<ObjCMethodDecl>(D))
      return;

    AddTopLevelDeclarationToHash(D, Hash);
    Unit.addTopLevelDecl(D);

    handleFileLevelDecl(D);
  }

  void handleFileLevelDecl(Decl *D) {
    Unit.addFileLevelDecl(D);
    if (auto *NSD = dyn_cast<NamespaceDecl>(D)) {
      for (auto *I : NSD->decls())
        handleFileLevelDecl(I);
    }
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    for (auto *TopLevelDecl : D)
      handleTopLevelDecl(TopLevelDecl);
    return true;
  }

  // We're not interested in "interesting" decls.
  void HandleInterestingDecl(DeclGroupRef) override {}

  void HandleTopLevelDeclInObjCContainer(DeclGroupRef D) override {
    for (auto *TopLevelDecl : D)
      handleTopLevelDecl(TopLevelDecl);
  }

  ASTMutationListener *GetASTMutationListener() override {
    return Unit.getASTMutationListener();
  }

  ASTDeserializationListener *GetASTDeserializationListener() override {
    return Unit.getDeserializationListener();
  }
};

class TopLevelDeclTrackerAction : public ASTFrontendAction {
public:
  ASTUnit &Unit;

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    CI.getPreprocessor().addPPCallbacks(
        std::make_unique<MacroDefinitionTrackerPPCallbacks>(
                                           Unit.getCurrentTopLevelHashValue()));
    return std::make_unique<TopLevelDeclTrackerConsumer>(
        Unit, Unit.getCurrentTopLevelHashValue());
  }

public:
  TopLevelDeclTrackerAction(ASTUnit &_Unit) : Unit(_Unit) {}

  bool hasCodeCompletionSupport() const override { return false; }

  TranslationUnitKind getTranslationUnitKind() override {
    return Unit.getTranslationUnitKind();
  }
};

class ASTUnitPreambleCallbacks : public PreambleCallbacks {
public:
  unsigned getHash() const { return Hash; }

  std::vector<Decl *> takeTopLevelDecls() { return std::move(TopLevelDecls); }

  std::vector<LocalDeclID> takeTopLevelDeclIDs() {
    return std::move(TopLevelDeclIDs);
  }

  void AfterPCHEmitted(ASTWriter &Writer) override {
    TopLevelDeclIDs.reserve(TopLevelDecls.size());
    for (const auto *D : TopLevelDecls) {
      // Invalid top-level decls may not have been serialized.
      if (D->isInvalidDecl())
        continue;
      TopLevelDeclIDs.push_back(Writer.getDeclID(D));
    }
  }

  void HandleTopLevelDecl(DeclGroupRef DG) override {
    for (auto *D : DG) {
      // FIXME: Currently ObjC method declarations are incorrectly being
      // reported as top-level declarations, even though their DeclContext
      // is the containing ObjC @interface/@implementation.  This is a
      // fundamental problem in the parser right now.
      if (isa<ObjCMethodDecl>(D))
        continue;
      AddTopLevelDeclarationToHash(D, Hash);
      TopLevelDecls.push_back(D);
    }
  }

  std::unique_ptr<PPCallbacks> createPPCallbacks() override {
    return std::make_unique<MacroDefinitionTrackerPPCallbacks>(Hash);
  }

private:
  unsigned Hash = 0;
  std::vector<Decl *> TopLevelDecls;
  std::vector<LocalDeclID> TopLevelDeclIDs;
  llvm::SmallVector<ASTUnit::StandaloneDiagnostic, 4> PreambleDiags;
};

} // namespace

static bool isNonDriverDiag(const StoredDiagnostic &StoredDiag) {
  return StoredDiag.getLocation().isValid();
}

static void
checkAndRemoveNonDriverDiags(SmallVectorImpl<StoredDiagnostic> &StoredDiags) {
  // Get rid of stored diagnostics except the ones from the driver which do not
  // have a source location.
  llvm::erase_if(StoredDiags, isNonDriverDiag);
}

static void checkAndSanitizeDiags(SmallVectorImpl<StoredDiagnostic> &
                                                              StoredDiagnostics,
                                  SourceManager &SM) {
  // The stored diagnostic has the old source manager in it; update
  // the locations to refer into the new source manager. Since we've
  // been careful to make sure that the source manager's state
  // before and after are identical, so that we can reuse the source
  // location itself.
  for (auto &SD : StoredDiagnostics) {
    if (SD.getLocation().isValid()) {
      FullSourceLoc Loc(SD.getLocation(), SM);
      SD.setLocation(Loc);
    }
  }
}

/// Parse the source file into a translation unit using the given compiler
/// invocation, replacing the current translation unit.
///
/// \returns True if a failure occurred that causes the ASTUnit not to
/// contain any translation-unit information, false otherwise.
bool ASTUnit::Parse(std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                    std::unique_ptr<llvm::MemoryBuffer> OverrideMainBuffer,
                    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  if (!Invocation)
    return true;

  if (VFS && FileMgr)
    assert(VFS == &FileMgr->getVirtualFileSystem() &&
           "VFS passed to Parse and VFS in FileMgr are different");

  auto CCInvocation = std::make_shared<CompilerInvocation>(*Invocation);
  if (OverrideMainBuffer) {
    assert(Preamble &&
           "No preamble was built, but OverrideMainBuffer is not null");
    Preamble->AddImplicitPreamble(*CCInvocation, VFS, OverrideMainBuffer.get());
    // VFS may have changed...
  }

  // Create the compiler instance to use for building the AST.
  std::unique_ptr<CompilerInstance> Clang(
      new CompilerInstance(std::move(PCHContainerOps)));
  Clang->setInvocation(CCInvocation);

  // Clean up on error, disengage it if the function returns successfully.
  auto CleanOnError = llvm::make_scope_exit([&]() {
    // Remove the overridden buffer we used for the preamble.
    SavedMainFileBuffer = nullptr;

    // Keep the ownership of the data in the ASTUnit because the client may
    // want to see the diagnostics.
    transferASTDataFromCompilerInstance(*Clang);
    FailedParseDiagnostics.swap(StoredDiagnostics);
    StoredDiagnostics.clear();
    NumStoredDiagnosticsFromDriver = 0;
  });

  // Ensure that Clang has a FileManager with the right VFS, which may have
  // changed above in AddImplicitPreamble.  If VFS is nullptr, rely on
  // createFileManager to create one.
  if (VFS && FileMgr && &FileMgr->getVirtualFileSystem() == VFS)
    Clang->setFileManager(&*FileMgr);
  else
    FileMgr = Clang->createFileManager(std::move(VFS));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<CompilerInstance>
    CICleanup(Clang.get());

  OriginalSourceFile =
      std::string(Clang->getFrontendOpts().Inputs[0].getFile());

  // Set up diagnostics, capturing any diagnostics that would
  // otherwise be dropped.
  Clang->setDiagnostics(&getDiagnostics());

  // Create the target instance.
  if (!Clang->createTarget())
    return true;

  assert(Clang->getFrontendOpts().Inputs.size() == 1 &&
         "Invocation must have exactly one source file!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getFormat() ==
             InputKind::Source &&
         "FIXME: AST inputs not yet supported here!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getLanguage() !=
             Language::LLVM_IR &&
         "IR inputs not support here!");

  // Configure the various subsystems.
  LangOpts = Clang->getInvocation().LangOpts;
  FileSystemOpts = Clang->getFileSystemOpts();

  ResetForParse();

  SourceMgr = new SourceManager(getDiagnostics(), *FileMgr,
                                UserFilesAreVolatile);
  if (!OverrideMainBuffer) {
    checkAndRemoveNonDriverDiags(StoredDiagnostics);
    TopLevelDeclsInPreamble.clear();
  }

  // Create the source manager.
  Clang->setSourceManager(&getSourceManager());

  // If the main file has been overridden due to the use of a preamble,
  // make that override happen and introduce the preamble.
  if (OverrideMainBuffer) {
    // The stored diagnostic has the old source manager in it; update
    // the locations to refer into the new source manager. Since we've
    // been careful to make sure that the source manager's state
    // before and after are identical, so that we can reuse the source
    // location itself.
    checkAndSanitizeDiags(StoredDiagnostics, getSourceManager());

    // Keep track of the override buffer;
    SavedMainFileBuffer = std::move(OverrideMainBuffer);
  }

  std::unique_ptr<TopLevelDeclTrackerAction> Act(
      new TopLevelDeclTrackerAction(*this));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<TopLevelDeclTrackerAction>
    ActCleanup(Act.get());

  if (!Act->BeginSourceFile(*Clang.get(), Clang->getFrontendOpts().Inputs[0]))
    return true;

  if (SavedMainFileBuffer)
    TranslateStoredDiagnostics(getFileManager(), getSourceManager(),
                               PreambleDiagnostics, StoredDiagnostics);
  else
    PreambleSrcLocCache.clear();

  if (llvm::Error Err = Act->Execute()) {
    consumeError(std::move(Err)); // FIXME this drops errors on the floor.
    return true;
  }

  transferASTDataFromCompilerInstance(*Clang);

  Act->EndSourceFile();

  FailedParseDiagnostics.clear();

  CleanOnError.release();

  return false;
}

static std::pair<unsigned, unsigned>
makeStandaloneRange(CharSourceRange Range, const SourceManager &SM,
                    const LangOptions &LangOpts) {
  CharSourceRange FileRange = Lexer::makeFileCharRange(Range, SM, LangOpts);
  unsigned Offset = SM.getFileOffset(FileRange.getBegin());
  unsigned EndOffset = SM.getFileOffset(FileRange.getEnd());
  return std::make_pair(Offset, EndOffset);
}

static ASTUnit::StandaloneFixIt makeStandaloneFixIt(const SourceManager &SM,
                                                    const LangOptions &LangOpts,
                                                    const FixItHint &InFix) {
  ASTUnit::StandaloneFixIt OutFix;
  OutFix.RemoveRange = makeStandaloneRange(InFix.RemoveRange, SM, LangOpts);
  OutFix.InsertFromRange = makeStandaloneRange(InFix.InsertFromRange, SM,
                                               LangOpts);
  OutFix.CodeToInsert = InFix.CodeToInsert;
  OutFix.BeforePreviousInsertions = InFix.BeforePreviousInsertions;
  return OutFix;
}

static ASTUnit::StandaloneDiagnostic
makeStandaloneDiagnostic(const LangOptions &LangOpts,
                         const StoredDiagnostic &InDiag) {
  ASTUnit::StandaloneDiagnostic OutDiag;
  OutDiag.ID = InDiag.getID();
  OutDiag.Level = InDiag.getLevel();
  OutDiag.Message = std::string(InDiag.getMessage());
  OutDiag.LocOffset = 0;
  if (InDiag.getLocation().isInvalid())
    return OutDiag;
  const SourceManager &SM = InDiag.getLocation().getManager();
  SourceLocation FileLoc = SM.getFileLoc(InDiag.getLocation());
  OutDiag.Filename = std::string(SM.getFilename(FileLoc));
  if (OutDiag.Filename.empty())
    return OutDiag;
  OutDiag.LocOffset = SM.getFileOffset(FileLoc);
  for (const auto &Range : InDiag.getRanges())
    OutDiag.Ranges.push_back(makeStandaloneRange(Range, SM, LangOpts));
  for (const auto &FixIt : InDiag.getFixIts())
    OutDiag.FixIts.push_back(makeStandaloneFixIt(SM, LangOpts, FixIt));

  return OutDiag;
}

/// Attempt to build or re-use a precompiled preamble when (re-)parsing
/// the source file.
///
/// This routine will compute the preamble of the main source file. If a
/// non-trivial preamble is found, it will precompile that preamble into a
/// precompiled header so that the precompiled preamble can be used to reduce
/// reparsing time. If a precompiled preamble has already been constructed,
/// this routine will determine if it is still valid and, if so, avoid
/// rebuilding the precompiled preamble.
///
/// \param AllowRebuild When true (the default), this routine is
/// allowed to rebuild the precompiled preamble if it is found to be
/// out-of-date.
///
/// \param MaxLines When non-zero, the maximum number of lines that
/// can occur within the preamble.
///
/// \returns If the precompiled preamble can be used, returns a newly-allocated
/// buffer that should be used in place of the main file when doing so.
/// Otherwise, returns a NULL pointer.
std::unique_ptr<llvm::MemoryBuffer>
ASTUnit::getMainBufferWithPrecompiledPreamble(
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    CompilerInvocation &PreambleInvocationIn,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS, bool AllowRebuild,
    unsigned MaxLines) {
  auto MainFilePath =
      PreambleInvocationIn.getFrontendOpts().Inputs[0].getFile();
  std::unique_ptr<llvm::MemoryBuffer> MainFileBuffer =
      getBufferForFileHandlingRemapping(PreambleInvocationIn, VFS.get(),
                                        MainFilePath, UserFilesAreVolatile);
  if (!MainFileBuffer)
    return nullptr;

  PreambleBounds Bounds = ComputePreambleBounds(
      PreambleInvocationIn.getLangOpts(), *MainFileBuffer, MaxLines);
  if (!Bounds.Size)
    return nullptr;

  if (Preamble) {
    if (Preamble->CanReuse(PreambleInvocationIn, *MainFileBuffer, Bounds,
                           *VFS)) {
      // Okay! We can re-use the precompiled preamble.

      // Set the state of the diagnostic object to mimic its state
      // after parsing the preamble.
      getDiagnostics().Reset();
      ProcessWarningOptions(getDiagnostics(),
                            PreambleInvocationIn.getDiagnosticOpts());
      getDiagnostics().setNumWarnings(NumWarningsInPreamble);

      PreambleRebuildCountdown = 1;
      return MainFileBuffer;
    } else {
      Preamble.reset();
      PreambleDiagnostics.clear();
      TopLevelDeclsInPreamble.clear();
      PreambleSrcLocCache.clear();
      PreambleRebuildCountdown = 1;
    }
  }

  // If the preamble rebuild counter > 1, it's because we previously
  // failed to build a preamble and we're not yet ready to try
  // again. Decrement the counter and return a failure.
  if (PreambleRebuildCountdown > 1) {
    --PreambleRebuildCountdown;
    return nullptr;
  }

  assert(!Preamble && "No Preamble should be stored at that point");
  // If we aren't allowed to rebuild the precompiled preamble, just
  // return now.
  if (!AllowRebuild)
    return nullptr;

  ++PreambleCounter;

  SmallVector<StandaloneDiagnostic, 4> NewPreambleDiagsStandalone;
  SmallVector<StoredDiagnostic, 4> NewPreambleDiags;
  ASTUnitPreambleCallbacks Callbacks;
  {
    std::optional<CaptureDroppedDiagnostics> Capture;
    if (CaptureDiagnostics != CaptureDiagsKind::None)
      Capture.emplace(CaptureDiagnostics, *Diagnostics, &NewPreambleDiags,
                      &NewPreambleDiagsStandalone);

    // We did not previously compute a preamble, or it can't be reused anyway.
    SimpleTimer PreambleTimer(WantTiming);
    PreambleTimer.setOutput("Precompiling preamble");

    const bool PreviousSkipFunctionBodies =
        PreambleInvocationIn.getFrontendOpts().SkipFunctionBodies;
    if (SkipFunctionBodies == SkipFunctionBodiesScope::Preamble)
      PreambleInvocationIn.getFrontendOpts().SkipFunctionBodies = true;

    llvm::ErrorOr<PrecompiledPreamble> NewPreamble = PrecompiledPreamble::Build(
        PreambleInvocationIn, MainFileBuffer.get(), Bounds, *Diagnostics, VFS,
        PCHContainerOps, StorePreamblesInMemory, PreambleStoragePath,
        Callbacks);

    PreambleInvocationIn.getFrontendOpts().SkipFunctionBodies =
        PreviousSkipFunctionBodies;

    if (NewPreamble) {
      Preamble = std::move(*NewPreamble);
      PreambleRebuildCountdown = 1;
    } else {
      switch (static_cast<BuildPreambleError>(NewPreamble.getError().value())) {
      case BuildPreambleError::CouldntCreateTempFile:
        // Try again next time.
        PreambleRebuildCountdown = 1;
        return nullptr;
      case BuildPreambleError::CouldntCreateTargetInfo:
      case BuildPreambleError::BeginSourceFileFailed:
      case BuildPreambleError::CouldntEmitPCH:
      case BuildPreambleError::BadInputs:
        // These erros are more likely to repeat, retry after some period.
        PreambleRebuildCountdown = DefaultPreambleRebuildInterval;
        return nullptr;
      }
      llvm_unreachable("unexpected BuildPreambleError");
    }
  }

  assert(Preamble && "Preamble wasn't built");

  TopLevelDecls.clear();
  TopLevelDeclsInPreamble = Callbacks.takeTopLevelDeclIDs();
  PreambleTopLevelHashValue = Callbacks.getHash();

  NumWarningsInPreamble = getDiagnostics().getNumWarnings();

  checkAndRemoveNonDriverDiags(NewPreambleDiags);
  StoredDiagnostics = std::move(NewPreambleDiags);
  PreambleDiagnostics = std::move(NewPreambleDiagsStandalone);

  // If the hash of top-level entities differs from the hash of the top-level
  // entities the last time we rebuilt the preamble, clear out the completion
  // cache.
  if (CurrentTopLevelHashValue != PreambleTopLevelHashValue) {
    CompletionCacheTopLevelHashValue = 0;
    PreambleTopLevelHashValue = CurrentTopLevelHashValue;
  }

  return MainFileBuffer;
}

void ASTUnit::RealizeTopLevelDeclsFromPreamble() {
  assert(Preamble && "Should only be called when preamble was built");

  std::vector<Decl *> Resolved;
  Resolved.reserve(TopLevelDeclsInPreamble.size());
  // The module file of the preamble.
  serialization::ModuleFile &MF = Reader->getModuleManager().getPrimaryModule();
  for (const auto TopLevelDecl : TopLevelDeclsInPreamble) {
    // Resolve the declaration ID to an actual declaration, possibly
    // deserializing the declaration in the process.
    if (Decl *D = Reader->GetLocalDecl(MF, TopLevelDecl))
      Resolved.push_back(D);
  }
  TopLevelDeclsInPreamble.clear();
  TopLevelDecls.insert(TopLevelDecls.begin(), Resolved.begin(), Resolved.end());
}

void ASTUnit::transferASTDataFromCompilerInstance(CompilerInstance &CI) {
  // Steal the created target, context, and preprocessor if they have been
  // created.
  assert(CI.hasInvocation() && "missing invocation");
  LangOpts = CI.getInvocation().LangOpts;
  TheSema = CI.takeSema();
  Consumer = CI.takeASTConsumer();
  if (CI.hasASTContext())
    Ctx = &CI.getASTContext();
  if (CI.hasPreprocessor())
    PP = CI.getPreprocessorPtr();
  CI.setSourceManager(nullptr);
  CI.setFileManager(nullptr);
  if (CI.hasTarget())
    Target = &CI.getTarget();
  Reader = CI.getASTReader();
  HadModuleLoaderFatalFailure = CI.hadModuleLoaderFatalFailure();
}

StringRef ASTUnit::getMainFileName() const {
  if (Invocation && !Invocation->getFrontendOpts().Inputs.empty()) {
    const FrontendInputFile &Input = Invocation->getFrontendOpts().Inputs[0];
    if (Input.isFile())
      return Input.getFile();
    else
      return Input.getBuffer().getBufferIdentifier();
  }

  if (SourceMgr) {
    if (OptionalFileEntryRef FE =
            SourceMgr->getFileEntryRefForID(SourceMgr->getMainFileID()))
      return FE->getName();
  }

  return {};
}

StringRef ASTUnit::getASTFileName() const {
  if (!isMainFileAST())
    return {};

  serialization::ModuleFile &
    Mod = Reader->getModuleManager().getPrimaryModule();
  return Mod.FileName;
}

std::unique_ptr<ASTUnit>
ASTUnit::create(std::shared_ptr<CompilerInvocation> CI,
                IntrusiveRefCntPtr<DiagnosticsEngine> Diags,
                CaptureDiagsKind CaptureDiagnostics,
                bool UserFilesAreVolatile) {
  std::unique_ptr<ASTUnit> AST(new ASTUnit(false));
  ConfigureDiags(Diags, *AST, CaptureDiagnostics);
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS =
      createVFSFromCompilerInvocation(*CI, *Diags);
  AST->Diagnostics = Diags;
  AST->FileSystemOpts = CI->getFileSystemOpts();
  AST->Invocation = std::move(CI);
  AST->FileMgr = new FileManager(AST->FileSystemOpts, VFS);
  AST->UserFilesAreVolatile = UserFilesAreVolatile;
  AST->SourceMgr = new SourceManager(AST->getDiagnostics(), *AST->FileMgr,
                                     UserFilesAreVolatile);
  AST->ModuleCache = new InMemoryModuleCache;

  return AST;
}

ASTUnit *ASTUnit::LoadFromCompilerInvocationAction(
    std::shared_ptr<CompilerInvocation> CI,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags, FrontendAction *Action,
    ASTUnit *Unit, bool Persistent, StringRef ResourceFilesPath,
    bool OnlyLocalDecls, CaptureDiagsKind CaptureDiagnostics,
    unsigned PrecompilePreambleAfterNParses, bool CacheCodeCompletionResults,
    bool UserFilesAreVolatile, std::unique_ptr<ASTUnit> *ErrAST) {
  assert(CI && "A CompilerInvocation is required");

  std::unique_ptr<ASTUnit> OwnAST;
  ASTUnit *AST = Unit;
  if (!AST) {
    // Create the AST unit.
    OwnAST = create(CI, Diags, CaptureDiagnostics, UserFilesAreVolatile);
    AST = OwnAST.get();
    if (!AST)
      return nullptr;
  }

  if (!ResourceFilesPath.empty()) {
    // Override the resources path.
    CI->getHeaderSearchOpts().ResourceDir = std::string(ResourceFilesPath);
  }
  AST->OnlyLocalDecls = OnlyLocalDecls;
  AST->CaptureDiagnostics = CaptureDiagnostics;
  if (PrecompilePreambleAfterNParses > 0)
    AST->PreambleRebuildCountdown = PrecompilePreambleAfterNParses;
  AST->TUKind = Action ? Action->getTranslationUnitKind() : TU_Complete;
  AST->ShouldCacheCodeCompletionResults = CacheCodeCompletionResults;
  AST->IncludeBriefCommentsInCodeCompletion = false;

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<ASTUnit>
    ASTUnitCleanup(OwnAST.get());
  llvm::CrashRecoveryContextCleanupRegistrar<DiagnosticsEngine,
    llvm::CrashRecoveryContextReleaseRefCleanup<DiagnosticsEngine>>
    DiagCleanup(Diags.get());

  // We'll manage file buffers ourselves.
  CI->getPreprocessorOpts().RetainRemappedFileBuffers = true;
  CI->getFrontendOpts().DisableFree = false;
  ProcessWarningOptions(AST->getDiagnostics(), CI->getDiagnosticOpts());

  // Create the compiler instance to use for building the AST.
  std::unique_ptr<CompilerInstance> Clang(
      new CompilerInstance(std::move(PCHContainerOps)));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<CompilerInstance>
    CICleanup(Clang.get());

  Clang->setInvocation(std::move(CI));
  AST->OriginalSourceFile =
      std::string(Clang->getFrontendOpts().Inputs[0].getFile());

  // Set up diagnostics, capturing any diagnostics that would
  // otherwise be dropped.
  Clang->setDiagnostics(&AST->getDiagnostics());

  // Create the target instance.
  if (!Clang->createTarget())
    return nullptr;

  assert(Clang->getFrontendOpts().Inputs.size() == 1 &&
         "Invocation must have exactly one source file!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getFormat() ==
             InputKind::Source &&
         "FIXME: AST inputs not yet supported here!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getLanguage() !=
             Language::LLVM_IR &&
         "IR inputs not support here!");

  // Configure the various subsystems.
  AST->TheSema.reset();
  AST->Ctx = nullptr;
  AST->PP = nullptr;
  AST->Reader = nullptr;

  // Create a file manager object to provide access to and cache the filesystem.
  Clang->setFileManager(&AST->getFileManager());

  // Create the source manager.
  Clang->setSourceManager(&AST->getSourceManager());

  FrontendAction *Act = Action;

  std::unique_ptr<TopLevelDeclTrackerAction> TrackerAct;
  if (!Act) {
    TrackerAct.reset(new TopLevelDeclTrackerAction(*AST));
    Act = TrackerAct.get();
  }

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<TopLevelDeclTrackerAction>
    ActCleanup(TrackerAct.get());

  if (!Act->BeginSourceFile(*Clang.get(), Clang->getFrontendOpts().Inputs[0])) {
    AST->transferASTDataFromCompilerInstance(*Clang);
    if (OwnAST && ErrAST)
      ErrAST->swap(OwnAST);

    return nullptr;
  }

  if (Persistent && !TrackerAct) {
    Clang->getPreprocessor().addPPCallbacks(
        std::make_unique<MacroDefinitionTrackerPPCallbacks>(
                                           AST->getCurrentTopLevelHashValue()));
    std::vector<std::unique_ptr<ASTConsumer>> Consumers;
    if (Clang->hasASTConsumer())
      Consumers.push_back(Clang->takeASTConsumer());
    Consumers.push_back(std::make_unique<TopLevelDeclTrackerConsumer>(
        *AST, AST->getCurrentTopLevelHashValue()));
    Clang->setASTConsumer(
        std::make_unique<MultiplexConsumer>(std::move(Consumers)));
  }
  if (llvm::Error Err = Act->Execute()) {
    consumeError(std::move(Err)); // FIXME this drops errors on the floor.
    AST->transferASTDataFromCompilerInstance(*Clang);
    if (OwnAST && ErrAST)
      ErrAST->swap(OwnAST);

    return nullptr;
  }

  // Steal the created target, context, and preprocessor.
  AST->transferASTDataFromCompilerInstance(*Clang);

  Act->EndSourceFile();

  if (OwnAST)
    return OwnAST.release();
  else
    return AST;
}

bool ASTUnit::LoadFromCompilerInvocation(
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    unsigned PrecompilePreambleAfterNParses,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  if (!Invocation)
    return true;

  assert(VFS && "VFS is null");

  // We'll manage file buffers ourselves.
  Invocation->getPreprocessorOpts().RetainRemappedFileBuffers = true;
  Invocation->getFrontendOpts().DisableFree = false;
  getDiagnostics().Reset();
  ProcessWarningOptions(getDiagnostics(), Invocation->getDiagnosticOpts());

  std::unique_ptr<llvm::MemoryBuffer> OverrideMainBuffer;
  if (PrecompilePreambleAfterNParses > 0) {
    PreambleRebuildCountdown = PrecompilePreambleAfterNParses;
    OverrideMainBuffer =
        getMainBufferWithPrecompiledPreamble(PCHContainerOps, *Invocation, VFS);
    getDiagnostics().Reset();
    ProcessWarningOptions(getDiagnostics(), Invocation->getDiagnosticOpts());
  }

  SimpleTimer ParsingTimer(WantTiming);
  ParsingTimer.setOutput("Parsing " + getMainFileName());

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<llvm::MemoryBuffer>
    MemBufferCleanup(OverrideMainBuffer.get());

  return Parse(std::move(PCHContainerOps), std::move(OverrideMainBuffer), VFS);
}

std::unique_ptr<ASTUnit> ASTUnit::LoadFromCompilerInvocation(
    std::shared_ptr<CompilerInvocation> CI,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags, FileManager *FileMgr,
    bool OnlyLocalDecls, CaptureDiagsKind CaptureDiagnostics,
    unsigned PrecompilePreambleAfterNParses, TranslationUnitKind TUKind,
    bool CacheCodeCompletionResults, bool IncludeBriefCommentsInCodeCompletion,
    bool UserFilesAreVolatile) {
  // Create the AST unit.
  std::unique_ptr<ASTUnit> AST(new ASTUnit(false));
  ConfigureDiags(Diags, *AST, CaptureDiagnostics);
  AST->Diagnostics = Diags;
  AST->OnlyLocalDecls = OnlyLocalDecls;
  AST->CaptureDiagnostics = CaptureDiagnostics;
  AST->TUKind = TUKind;
  AST->ShouldCacheCodeCompletionResults = CacheCodeCompletionResults;
  AST->IncludeBriefCommentsInCodeCompletion
    = IncludeBriefCommentsInCodeCompletion;
  AST->Invocation = std::move(CI);
  AST->FileSystemOpts = FileMgr->getFileSystemOpts();
  AST->FileMgr = FileMgr;
  AST->UserFilesAreVolatile = UserFilesAreVolatile;

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<ASTUnit>
    ASTUnitCleanup(AST.get());
  llvm::CrashRecoveryContextCleanupRegistrar<DiagnosticsEngine,
    llvm::CrashRecoveryContextReleaseRefCleanup<DiagnosticsEngine>>
    DiagCleanup(Diags.get());

  if (AST->LoadFromCompilerInvocation(std::move(PCHContainerOps),
                                      PrecompilePreambleAfterNParses,
                                      &AST->FileMgr->getVirtualFileSystem()))
    return nullptr;
  return AST;
}

std::unique_ptr<ASTUnit> ASTUnit::LoadFromCommandLine(
    const char **ArgBegin, const char **ArgEnd,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags, StringRef ResourceFilesPath,
    bool StorePreamblesInMemory, StringRef PreambleStoragePath,
    bool OnlyLocalDecls, CaptureDiagsKind CaptureDiagnostics,
    ArrayRef<RemappedFile> RemappedFiles, bool RemappedFilesKeepOriginalName,
    unsigned PrecompilePreambleAfterNParses, TranslationUnitKind TUKind,
    bool CacheCodeCompletionResults, bool IncludeBriefCommentsInCodeCompletion,
    bool AllowPCHWithCompilerErrors, SkipFunctionBodiesScope SkipFunctionBodies,
    bool SingleFileParse, bool UserFilesAreVolatile, bool ForSerialization,
    bool RetainExcludedConditionalBlocks, std::optional<StringRef> ModuleFormat,
    std::unique_ptr<ASTUnit> *ErrAST,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  assert(Diags.get() && "no DiagnosticsEngine was provided");

  // If no VFS was provided, create one that tracks the physical file system.
  // If '-working-directory' was passed as an argument, 'createInvocation' will
  // set this as the current working directory of the VFS.
  if (!VFS)
    VFS = llvm::vfs::createPhysicalFileSystem();

  SmallVector<StoredDiagnostic, 4> StoredDiagnostics;

  std::shared_ptr<CompilerInvocation> CI;

  {
    CaptureDroppedDiagnostics Capture(CaptureDiagnostics, *Diags,
                                      &StoredDiagnostics, nullptr);

    CreateInvocationOptions CIOpts;
    CIOpts.VFS = VFS;
    CIOpts.Diags = Diags;
    CIOpts.ProbePrecompiled = true; // FIXME: historical default. Needed?
    CI = createInvocation(llvm::ArrayRef(ArgBegin, ArgEnd), std::move(CIOpts));
    if (!CI)
      return nullptr;
  }

  // Override any files that need remapping
  for (const auto &RemappedFile : RemappedFiles) {
    CI->getPreprocessorOpts().addRemappedFile(RemappedFile.first,
                                              RemappedFile.second);
  }
  PreprocessorOptions &PPOpts = CI->getPreprocessorOpts();
  PPOpts.RemappedFilesKeepOriginalName = RemappedFilesKeepOriginalName;
  PPOpts.AllowPCHWithCompilerErrors = AllowPCHWithCompilerErrors;
  PPOpts.SingleFileParseMode = SingleFileParse;
  PPOpts.RetainExcludedConditionalBlocks = RetainExcludedConditionalBlocks;

  // Override the resources path.
  CI->getHeaderSearchOpts().ResourceDir = std::string(ResourceFilesPath);

  CI->getFrontendOpts().SkipFunctionBodies =
      SkipFunctionBodies == SkipFunctionBodiesScope::PreambleAndMainFile;

  if (ModuleFormat)
    CI->getHeaderSearchOpts().ModuleFormat = std::string(*ModuleFormat);

  // Create the AST unit.
  std::unique_ptr<ASTUnit> AST;
  AST.reset(new ASTUnit(false));
  AST->NumStoredDiagnosticsFromDriver = StoredDiagnostics.size();
  AST->StoredDiagnostics.swap(StoredDiagnostics);
  ConfigureDiags(Diags, *AST, CaptureDiagnostics);
  AST->Diagnostics = Diags;
  AST->FileSystemOpts = CI->getFileSystemOpts();
  VFS = createVFSFromCompilerInvocation(*CI, *Diags, VFS);
  AST->FileMgr = new FileManager(AST->FileSystemOpts, VFS);
  AST->StorePreamblesInMemory = StorePreamblesInMemory;
  AST->PreambleStoragePath = PreambleStoragePath;
  AST->ModuleCache = new InMemoryModuleCache;
  AST->OnlyLocalDecls = OnlyLocalDecls;
  AST->CaptureDiagnostics = CaptureDiagnostics;
  AST->TUKind = TUKind;
  AST->ShouldCacheCodeCompletionResults = CacheCodeCompletionResults;
  AST->IncludeBriefCommentsInCodeCompletion
    = IncludeBriefCommentsInCodeCompletion;
  AST->UserFilesAreVolatile = UserFilesAreVolatile;
  AST->Invocation = CI;
  AST->SkipFunctionBodies = SkipFunctionBodies;
  if (ForSerialization)
    AST->WriterData.reset(new ASTWriterData(*AST->ModuleCache));
  // Zero out now to ease cleanup during crash recovery.
  CI = nullptr;
  Diags = nullptr;

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<ASTUnit>
    ASTUnitCleanup(AST.get());

  if (AST->LoadFromCompilerInvocation(std::move(PCHContainerOps),
                                      PrecompilePreambleAfterNParses,
                                      VFS)) {
    // Some error occurred, if caller wants to examine diagnostics, pass it the
    // ASTUnit.
    if (ErrAST) {
      AST->StoredDiagnostics.swap(AST->FailedParseDiagnostics);
      ErrAST->swap(AST);
    }
    return nullptr;
  }

  return AST;
}

bool ASTUnit::Reparse(std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                      ArrayRef<RemappedFile> RemappedFiles,
                      IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  if (!Invocation)
    return true;

  if (!VFS) {
    assert(FileMgr && "FileMgr is null on Reparse call");
    VFS = &FileMgr->getVirtualFileSystem();
  }

  clearFileLevelDecls();

  SimpleTimer ParsingTimer(WantTiming);
  ParsingTimer.setOutput("Reparsing " + getMainFileName());

  // Remap files.
  PreprocessorOptions &PPOpts = Invocation->getPreprocessorOpts();
  for (const auto &RB : PPOpts.RemappedFileBuffers)
    delete RB.second;

  Invocation->getPreprocessorOpts().clearRemappedFiles();
  for (const auto &RemappedFile : RemappedFiles) {
    Invocation->getPreprocessorOpts().addRemappedFile(RemappedFile.first,
                                                      RemappedFile.second);
  }

  // If we have a preamble file lying around, or if we might try to
  // build a precompiled preamble, do so now.
  std::unique_ptr<llvm::MemoryBuffer> OverrideMainBuffer;
  if (Preamble || PreambleRebuildCountdown > 0)
    OverrideMainBuffer =
        getMainBufferWithPrecompiledPreamble(PCHContainerOps, *Invocation, VFS);

  // Clear out the diagnostics state.
  FileMgr.reset();
  getDiagnostics().Reset();
  ProcessWarningOptions(getDiagnostics(), Invocation->getDiagnosticOpts());
  if (OverrideMainBuffer)
    getDiagnostics().setNumWarnings(NumWarningsInPreamble);

  // Parse the sources
  bool Result =
      Parse(std::move(PCHContainerOps), std::move(OverrideMainBuffer), VFS);

  // If we're caching global code-completion results, and the top-level
  // declarations have changed, clear out the code-completion cache.
  if (!Result && ShouldCacheCodeCompletionResults &&
      CurrentTopLevelHashValue != CompletionCacheTopLevelHashValue)
    CacheCodeCompletionResults();

  // We now need to clear out the completion info related to this translation
  // unit; it'll be recreated if necessary.
  CCTUInfo.reset();

  return Result;
}

void ASTUnit::ResetForParse() {
  SavedMainFileBuffer.reset();

  SourceMgr.reset();
  TheSema.reset();
  Ctx.reset();
  PP.reset();
  Reader.reset();

  TopLevelDecls.clear();
  clearFileLevelDecls();
}

//----------------------------------------------------------------------------//
// Code completion
//----------------------------------------------------------------------------//

namespace {

  /// Code completion consumer that combines the cached code-completion
  /// results from an ASTUnit with the code-completion results provided to it,
  /// then passes the result on to
  class AugmentedCodeCompleteConsumer : public CodeCompleteConsumer {
    uint64_t NormalContexts;
    ASTUnit &AST;
    CodeCompleteConsumer &Next;

  public:
    AugmentedCodeCompleteConsumer(ASTUnit &AST, CodeCompleteConsumer &Next,
                                  const CodeCompleteOptions &CodeCompleteOpts)
        : CodeCompleteConsumer(CodeCompleteOpts), AST(AST), Next(Next) {
      // Compute the set of contexts in which we will look when we don't have
      // any information about the specific context.
      NormalContexts
        = (1LL << CodeCompletionContext::CCC_TopLevel)
        | (1LL << CodeCompletionContext::CCC_ObjCInterface)
        | (1LL << CodeCompletionContext::CCC_ObjCImplementation)
        | (1LL << CodeCompletionContext::CCC_ObjCIvarList)
        | (1LL << CodeCompletionContext::CCC_Statement)
        | (1LL << CodeCompletionContext::CCC_Expression)
        | (1LL << CodeCompletionContext::CCC_ObjCMessageReceiver)
        | (1LL << CodeCompletionContext::CCC_DotMemberAccess)
        | (1LL << CodeCompletionContext::CCC_ArrowMemberAccess)
        | (1LL << CodeCompletionContext::CCC_ObjCPropertyAccess)
        | (1LL << CodeCompletionContext::CCC_ObjCProtocolName)
        | (1LL << CodeCompletionContext::CCC_ParenthesizedExpression)
        | (1LL << CodeCompletionContext::CCC_Recovery);

      if (AST.getASTContext().getLangOpts().CPlusPlus)
        NormalContexts |= (1LL << CodeCompletionContext::CCC_EnumTag)
                       |  (1LL << CodeCompletionContext::CCC_UnionTag)
                       |  (1LL << CodeCompletionContext::CCC_ClassOrStructTag);
    }

    void ProcessCodeCompleteResults(Sema &S, CodeCompletionContext Context,
                                    CodeCompletionResult *Results,
                                    unsigned NumResults) override;

    void ProcessOverloadCandidates(Sema &S, unsigned CurrentArg,
                                   OverloadCandidate *Candidates,
                                   unsigned NumCandidates,
                                   SourceLocation OpenParLoc,
                                   bool Braced) override {
      Next.ProcessOverloadCandidates(S, CurrentArg, Candidates, NumCandidates,
                                     OpenParLoc, Braced);
    }

    CodeCompletionAllocator &getAllocator() override {
      return Next.getAllocator();
    }

    CodeCompletionTUInfo &getCodeCompletionTUInfo() override {
      return Next.getCodeCompletionTUInfo();
    }
  };

} // namespace

/// Helper function that computes which global names are hidden by the
/// local code-completion results.
static void CalculateHiddenNames(const CodeCompletionContext &Context,
                                 CodeCompletionResult *Results,
                                 unsigned NumResults,
                                 ASTContext &Ctx,
                          llvm::StringSet<llvm::BumpPtrAllocator> &HiddenNames){
  bool OnlyTagNames = false;
  switch (Context.getKind()) {
  case CodeCompletionContext::CCC_Recovery:
  case CodeCompletionContext::CCC_TopLevel:
  case CodeCompletionContext::CCC_ObjCInterface:
  case CodeCompletionContext::CCC_ObjCImplementation:
  case CodeCompletionContext::CCC_ObjCIvarList:
  case CodeCompletionContext::CCC_ClassStructUnion:
  case CodeCompletionContext::CCC_Statement:
  case CodeCompletionContext::CCC_Expression:
  case CodeCompletionContext::CCC_ObjCMessageReceiver:
  case CodeCompletionContext::CCC_DotMemberAccess:
  case CodeCompletionContext::CCC_ArrowMemberAccess:
  case CodeCompletionContext::CCC_ObjCPropertyAccess:
  case CodeCompletionContext::CCC_Namespace:
  case CodeCompletionContext::CCC_Type:
  case CodeCompletionContext::CCC_Symbol:
  case CodeCompletionContext::CCC_SymbolOrNewName:
  case CodeCompletionContext::CCC_ParenthesizedExpression:
  case CodeCompletionContext::CCC_ObjCInterfaceName:
  case CodeCompletionContext::CCC_TopLevelOrExpression:
      break;

  case CodeCompletionContext::CCC_EnumTag:
  case CodeCompletionContext::CCC_UnionTag:
  case CodeCompletionContext::CCC_ClassOrStructTag:
    OnlyTagNames = true;
    break;

  case CodeCompletionContext::CCC_ObjCProtocolName:
  case CodeCompletionContext::CCC_MacroName:
  case CodeCompletionContext::CCC_MacroNameUse:
  case CodeCompletionContext::CCC_PreprocessorExpression:
  case CodeCompletionContext::CCC_PreprocessorDirective:
  case CodeCompletionContext::CCC_NaturalLanguage:
  case CodeCompletionContext::CCC_SelectorName:
  case CodeCompletionContext::CCC_TypeQualifiers:
  case CodeCompletionContext::CCC_Other:
  case CodeCompletionContext::CCC_OtherWithMacros:
  case CodeCompletionContext::CCC_ObjCInstanceMessage:
  case CodeCompletionContext::CCC_ObjCClassMessage:
  case CodeCompletionContext::CCC_ObjCCategoryName:
  case CodeCompletionContext::CCC_IncludedFile:
  case CodeCompletionContext::CCC_Attribute:
  case CodeCompletionContext::CCC_NewName:
  case CodeCompletionContext::CCC_ObjCClassForwardDecl:
    // We're looking for nothing, or we're looking for names that cannot
    // be hidden.
    return;
  }

  using Result = CodeCompletionResult;
  for (unsigned I = 0; I != NumResults; ++I) {
    if (Results[I].Kind != Result::RK_Declaration)
      continue;

    unsigned IDNS
      = Results[I].Declaration->getUnderlyingDecl()->getIdentifierNamespace();

    bool Hiding = false;
    if (OnlyTagNames)
      Hiding = (IDNS & Decl::IDNS_Tag);
    else {
      unsigned HiddenIDNS = (Decl::IDNS_Type | Decl::IDNS_Member |
                             Decl::IDNS_Namespace | Decl::IDNS_Ordinary |
                             Decl::IDNS_NonMemberOperator);
      if (Ctx.getLangOpts().CPlusPlus)
        HiddenIDNS |= Decl::IDNS_Tag;
      Hiding = (IDNS & HiddenIDNS);
    }

    if (!Hiding)
      continue;

    DeclarationName Name = Results[I].Declaration->getDeclName();
    if (IdentifierInfo *Identifier = Name.getAsIdentifierInfo())
      HiddenNames.insert(Identifier->getName());
    else
      HiddenNames.insert(Name.getAsString());
  }
}

void AugmentedCodeCompleteConsumer::ProcessCodeCompleteResults(Sema &S,
                                            CodeCompletionContext Context,
                                            CodeCompletionResult *Results,
                                            unsigned NumResults) {
  // Merge the results we were given with the results we cached.
  bool AddedResult = false;
  uint64_t InContexts =
      Context.getKind() == CodeCompletionContext::CCC_Recovery
        ? NormalContexts : (1LL << Context.getKind());
  // Contains the set of names that are hidden by "local" completion results.
  llvm::StringSet<llvm::BumpPtrAllocator> HiddenNames;
  using Result = CodeCompletionResult;
  SmallVector<Result, 8> AllResults;
  for (ASTUnit::cached_completion_iterator
            C = AST.cached_completion_begin(),
         CEnd = AST.cached_completion_end();
       C != CEnd; ++C) {
    // If the context we are in matches any of the contexts we are
    // interested in, we'll add this result.
    if ((C->ShowInContexts & InContexts) == 0)
      continue;

    // If we haven't added any results previously, do so now.
    if (!AddedResult) {
      CalculateHiddenNames(Context, Results, NumResults, S.Context,
                           HiddenNames);
      AllResults.insert(AllResults.end(), Results, Results + NumResults);
      AddedResult = true;
    }

    // Determine whether this global completion result is hidden by a local
    // completion result. If so, skip it.
    if (C->Kind != CXCursor_MacroDefinition &&
        HiddenNames.count(C->Completion->getTypedText()))
      continue;

    // Adjust priority based on similar type classes.
    unsigned Priority = C->Priority;
    CodeCompletionString *Completion = C->Completion;
    if (!Context.getPreferredType().isNull()) {
      if (C->Kind == CXCursor_MacroDefinition) {
        Priority = getMacroUsagePriority(C->Completion->getTypedText(),
                                         S.getLangOpts(),
                               Context.getPreferredType()->isAnyPointerType());
      } else if (C->Type) {
        CanQualType Expected
          = S.Context.getCanonicalType(
                               Context.getPreferredType().getUnqualifiedType());
        SimplifiedTypeClass ExpectedSTC = getSimplifiedTypeClass(Expected);
        if (ExpectedSTC == C->TypeClass) {
          // We know this type is similar; check for an exact match.
          llvm::StringMap<unsigned> &CachedCompletionTypes
            = AST.getCachedCompletionTypes();
          llvm::StringMap<unsigned>::iterator Pos
            = CachedCompletionTypes.find(QualType(Expected).getAsString());
          if (Pos != CachedCompletionTypes.end() && Pos->second == C->Type)
            Priority /= CCF_ExactTypeMatch;
          else
            Priority /= CCF_SimilarTypeMatch;
        }
      }
    }

    // Adjust the completion string, if required.
    if (C->Kind == CXCursor_MacroDefinition &&
        Context.getKind() == CodeCompletionContext::CCC_MacroNameUse) {
      // Create a new code-completion string that just contains the
      // macro name, without its arguments.
      CodeCompletionBuilder Builder(getAllocator(), getCodeCompletionTUInfo(),
                                    CCP_CodePattern, C->Availability);
      Builder.AddTypedTextChunk(C->Completion->getTypedText());
      Priority = CCP_CodePattern;
      Completion = Builder.TakeString();
    }

    AllResults.push_back(Result(Completion, Priority, C->Kind,
                                C->Availability));
  }

  // If we did not add any cached completion results, just forward the
  // results we were given to the next consumer.
  if (!AddedResult) {
    Next.ProcessCodeCompleteResults(S, Context, Results, NumResults);
    return;
  }

  Next.ProcessCodeCompleteResults(S, Context, AllResults.data(),
                                  AllResults.size());
}

void ASTUnit::CodeComplete(
    StringRef File, unsigned Line, unsigned Column,
    ArrayRef<RemappedFile> RemappedFiles, bool IncludeMacros,
    bool IncludeCodePatterns, bool IncludeBriefComments,
    CodeCompleteConsumer &Consumer,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticsEngine &Diag, LangOptions &LangOpts, SourceManager &SourceMgr,
    FileManager &FileMgr, SmallVectorImpl<StoredDiagnostic> &StoredDiagnostics,
    SmallVectorImpl<const llvm::MemoryBuffer *> &OwnedBuffers,
    std::unique_ptr<SyntaxOnlyAction> Act) {
  if (!Invocation)
    return;

  SimpleTimer CompletionTimer(WantTiming);
  CompletionTimer.setOutput("Code completion @ " + File + ":" +
                            Twine(Line) + ":" + Twine(Column));

  auto CCInvocation = std::make_shared<CompilerInvocation>(*Invocation);

  FrontendOptions &FrontendOpts = CCInvocation->getFrontendOpts();
  CodeCompleteOptions &CodeCompleteOpts = FrontendOpts.CodeCompleteOpts;
  PreprocessorOptions &PreprocessorOpts = CCInvocation->getPreprocessorOpts();

  CodeCompleteOpts.IncludeMacros = IncludeMacros &&
                                   CachedCompletionResults.empty();
  CodeCompleteOpts.IncludeCodePatterns = IncludeCodePatterns;
  CodeCompleteOpts.IncludeGlobals = CachedCompletionResults.empty();
  CodeCompleteOpts.IncludeBriefComments = IncludeBriefComments;
  CodeCompleteOpts.LoadExternal = Consumer.loadExternal();
  CodeCompleteOpts.IncludeFixIts = Consumer.includeFixIts();

  assert(IncludeBriefComments == this->IncludeBriefCommentsInCodeCompletion);

  FrontendOpts.CodeCompletionAt.FileName = std::string(File);
  FrontendOpts.CodeCompletionAt.Line = Line;
  FrontendOpts.CodeCompletionAt.Column = Column;

  // Set the language options appropriately.
  LangOpts = CCInvocation->getLangOpts();

  // Spell-checking and warnings are wasteful during code-completion.
  LangOpts.SpellChecking = false;
  CCInvocation->getDiagnosticOpts().IgnoreWarnings = true;

  std::unique_ptr<CompilerInstance> Clang(
      new CompilerInstance(PCHContainerOps));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<CompilerInstance>
    CICleanup(Clang.get());

  auto &Inv = *CCInvocation;
  Clang->setInvocation(std::move(CCInvocation));
  OriginalSourceFile =
      std::string(Clang->getFrontendOpts().Inputs[0].getFile());

  // Set up diagnostics, capturing any diagnostics produced.
  Clang->setDiagnostics(&Diag);
  CaptureDroppedDiagnostics Capture(CaptureDiagsKind::All,
                                    Clang->getDiagnostics(),
                                    &StoredDiagnostics, nullptr);
  ProcessWarningOptions(Diag, Inv.getDiagnosticOpts());

  // Create the target instance.
  if (!Clang->createTarget()) {
    Clang->setInvocation(nullptr);
    return;
  }

  assert(Clang->getFrontendOpts().Inputs.size() == 1 &&
         "Invocation must have exactly one source file!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getFormat() ==
             InputKind::Source &&
         "FIXME: AST inputs not yet supported here!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getLanguage() !=
             Language::LLVM_IR &&
         "IR inputs not support here!");

  // Use the source and file managers that we were given.
  Clang->setFileManager(&FileMgr);
  Clang->setSourceManager(&SourceMgr);

  // Remap files.
  PreprocessorOpts.clearRemappedFiles();
  PreprocessorOpts.RetainRemappedFileBuffers = true;
  for (const auto &RemappedFile : RemappedFiles) {
    PreprocessorOpts.addRemappedFile(RemappedFile.first, RemappedFile.second);
    OwnedBuffers.push_back(RemappedFile.second);
  }

  // Use the code completion consumer we were given, but adding any cached
  // code-completion results.
  AugmentedCodeCompleteConsumer *AugmentedConsumer
    = new AugmentedCodeCompleteConsumer(*this, Consumer, CodeCompleteOpts);
  Clang->setCodeCompletionConsumer(AugmentedConsumer);

  auto getUniqueID =
      [&FileMgr](StringRef Filename) -> std::optional<llvm::sys::fs::UniqueID> {
    if (auto Status = FileMgr.getVirtualFileSystem().status(Filename))
      return Status->getUniqueID();
    return std::nullopt;
  };

  auto hasSameUniqueID = [getUniqueID](StringRef LHS, StringRef RHS) {
    if (LHS == RHS)
      return true;
    if (auto LHSID = getUniqueID(LHS))
      if (auto RHSID = getUniqueID(RHS))
        return *LHSID == *RHSID;
    return false;
  };

  // If we have a precompiled preamble, try to use it. We only allow
  // the use of the precompiled preamble if we're if the completion
  // point is within the main file, after the end of the precompiled
  // preamble.
  std::unique_ptr<llvm::MemoryBuffer> OverrideMainBuffer;
  if (Preamble && Line > 1 && hasSameUniqueID(File, OriginalSourceFile)) {
    OverrideMainBuffer = getMainBufferWithPrecompiledPreamble(
        PCHContainerOps, Inv, &FileMgr.getVirtualFileSystem(), false, Line - 1);
  }

  // If the main file has been overridden due to the use of a preamble,
  // make that override happen and introduce the preamble.
  if (OverrideMainBuffer) {
    assert(Preamble &&
           "No preamble was built, but OverrideMainBuffer is not null");

    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS =
        &FileMgr.getVirtualFileSystem();
    Preamble->AddImplicitPreamble(Clang->getInvocation(), VFS,
                                  OverrideMainBuffer.get());
    // FIXME: there is no way to update VFS if it was changed by
    // AddImplicitPreamble as FileMgr is accepted as a parameter by this method.
    // We use on-disk preambles instead and rely on FileMgr's VFS to ensure the
    // PCH files are always readable.
    OwnedBuffers.push_back(OverrideMainBuffer.release());
  } else {
    PreprocessorOpts.PrecompiledPreambleBytes.first = 0;
    PreprocessorOpts.PrecompiledPreambleBytes.second = false;
  }

  // Disable the preprocessing record if modules are not enabled.
  if (!Clang->getLangOpts().Modules)
    PreprocessorOpts.DetailedRecord = false;

  if (!Act)
    Act.reset(new SyntaxOnlyAction);

  if (Act->BeginSourceFile(*Clang.get(), Clang->getFrontendOpts().Inputs[0])) {
    if (llvm::Error Err = Act->Execute()) {
      consumeError(std::move(Err)); // FIXME this drops errors on the floor.
    }
    Act->EndSourceFile();
  }
}

bool ASTUnit::Save(StringRef File) {
  if (HadModuleLoaderFatalFailure)
    return true;

  // FIXME: Can we somehow regenerate the stat cache here, or do we need to
  // unconditionally create a stat cache when we parse the file?

  if (llvm::Error Err = llvm::writeToOutput(
          File, [this](llvm::raw_ostream &Out) {
            return serialize(Out) ? llvm::make_error<llvm::StringError>(
                                        "ASTUnit serialization failed",
                                        llvm::inconvertibleErrorCode())
                                  : llvm::Error::success();
          })) {
    consumeError(std::move(Err));
    return true;
  }
  return false;
}

static bool serializeUnit(ASTWriter &Writer, SmallVectorImpl<char> &Buffer,
                          Sema &S, raw_ostream &OS) {
  Writer.WriteAST(S, std::string(), nullptr, "");

  // Write the generated bitstream to "Out".
  if (!Buffer.empty())
    OS.write(Buffer.data(), Buffer.size());

  return false;
}

bool ASTUnit::serialize(raw_ostream &OS) {
  if (WriterData)
    return serializeUnit(WriterData->Writer, WriterData->Buffer, getSema(), OS);

  SmallString<128> Buffer;
  llvm::BitstreamWriter Stream(Buffer);
  InMemoryModuleCache ModuleCache;
  ASTWriter Writer(Stream, Buffer, ModuleCache, {});
  return serializeUnit(Writer, Buffer, getSema(), OS);
}

void ASTUnit::TranslateStoredDiagnostics(
                          FileManager &FileMgr,
                          SourceManager &SrcMgr,
                          const SmallVectorImpl<StandaloneDiagnostic> &Diags,
                          SmallVectorImpl<StoredDiagnostic> &Out) {
  // Map the standalone diagnostic into the new source manager. We also need to
  // remap all the locations to the new view. This includes the diag location,
  // any associated source ranges, and the source ranges of associated fix-its.
  // FIXME: There should be a cleaner way to do this.
  SmallVector<StoredDiagnostic, 4> Result;
  Result.reserve(Diags.size());

  for (const auto &SD : Diags) {
    // Rebuild the StoredDiagnostic.
    if (SD.Filename.empty())
      continue;
    auto FE = FileMgr.getFile(SD.Filename);
    if (!FE)
      continue;
    SourceLocation FileLoc;
    auto ItFileID = PreambleSrcLocCache.find(SD.Filename);
    if (ItFileID == PreambleSrcLocCache.end()) {
      FileID FID = SrcMgr.translateFile(*FE);
      FileLoc = SrcMgr.getLocForStartOfFile(FID);
      PreambleSrcLocCache[SD.Filename] = FileLoc;
    } else {
      FileLoc = ItFileID->getValue();
    }

    if (FileLoc.isInvalid())
      continue;
    SourceLocation L = FileLoc.getLocWithOffset(SD.LocOffset);
    FullSourceLoc Loc(L, SrcMgr);

    SmallVector<CharSourceRange, 4> Ranges;
    Ranges.reserve(SD.Ranges.size());
    for (const auto &Range : SD.Ranges) {
      SourceLocation BL = FileLoc.getLocWithOffset(Range.first);
      SourceLocation EL = FileLoc.getLocWithOffset(Range.second);
      Ranges.push_back(CharSourceRange::getCharRange(BL, EL));
    }

    SmallVector<FixItHint, 2> FixIts;
    FixIts.reserve(SD.FixIts.size());
    for (const auto &FixIt : SD.FixIts) {
      FixIts.push_back(FixItHint());
      FixItHint &FH = FixIts.back();
      FH.CodeToInsert = FixIt.CodeToInsert;
      SourceLocation BL = FileLoc.getLocWithOffset(FixIt.RemoveRange.first);
      SourceLocation EL = FileLoc.getLocWithOffset(FixIt.RemoveRange.second);
      FH.RemoveRange = CharSourceRange::getCharRange(BL, EL);
    }

    Result.push_back(StoredDiagnostic(SD.Level, SD.ID,
                                      SD.Message, Loc, Ranges, FixIts));
  }
  Result.swap(Out);
}

void ASTUnit::addFileLevelDecl(Decl *D) {
  assert(D);

  // We only care about local declarations.
  if (D->isFromASTFile())
    return;

  SourceManager &SM = *SourceMgr;
  SourceLocation Loc = D->getLocation();
  if (Loc.isInvalid() || !SM.isLocalSourceLocation(Loc))
    return;

  // We only keep track of the file-level declarations of each file.
  if (!D->getLexicalDeclContext()->isFileContext())
    return;

  SourceLocation FileLoc = SM.getFileLoc(Loc);
  assert(SM.isLocalSourceLocation(FileLoc));
  FileID FID;
  unsigned Offset;
  std::tie(FID, Offset) = SM.getDecomposedLoc(FileLoc);
  if (FID.isInvalid())
    return;

  std::unique_ptr<LocDeclsTy> &Decls = FileDecls[FID];
  if (!Decls)
    Decls = std::make_unique<LocDeclsTy>();

  std::pair<unsigned, Decl *> LocDecl(Offset, D);

  if (Decls->empty() || Decls->back().first <= Offset) {
    Decls->push_back(LocDecl);
    return;
  }

  LocDeclsTy::iterator I =
      llvm::upper_bound(*Decls, LocDecl, llvm::less_first());

  Decls->insert(I, LocDecl);
}

void ASTUnit::findFileRegionDecls(FileID File, unsigned Offset, unsigned Length,
                                  SmallVectorImpl<Decl *> &Decls) {
  if (File.isInvalid())
    return;

  if (SourceMgr->isLoadedFileID(File)) {
    assert(Ctx->getExternalSource() && "No external source!");
    return Ctx->getExternalSource()->FindFileRegionDecls(File, Offset, Length,
                                                         Decls);
  }

  FileDeclsTy::iterator I = FileDecls.find(File);
  if (I == FileDecls.end())
    return;

  LocDeclsTy &LocDecls = *I->second;
  if (LocDecls.empty())
    return;

  LocDeclsTy::iterator BeginIt =
      llvm::partition_point(LocDecls, [=](std::pair<unsigned, Decl *> LD) {
        return LD.first < Offset;
      });
  if (BeginIt != LocDecls.begin())
    --BeginIt;

  // If we are pointing at a top-level decl inside an objc container, we need
  // to backtrack until we find it otherwise we will fail to report that the
  // region overlaps with an objc container.
  while (BeginIt != LocDecls.begin() &&
         BeginIt->second->isTopLevelDeclInObjCContainer())
    --BeginIt;

  LocDeclsTy::iterator EndIt = llvm::upper_bound(
      LocDecls, std::make_pair(Offset + Length, (Decl *)nullptr),
      llvm::less_first());
  if (EndIt != LocDecls.end())
    ++EndIt;

  for (LocDeclsTy::iterator DIt = BeginIt; DIt != EndIt; ++DIt)
    Decls.push_back(DIt->second);
}

SourceLocation ASTUnit::getLocation(const FileEntry *File,
                                    unsigned Line, unsigned Col) const {
  const SourceManager &SM = getSourceManager();
  SourceLocation Loc = SM.translateFileLineCol(File, Line, Col);
  return SM.getMacroArgExpandedLocation(Loc);
}

SourceLocation ASTUnit::getLocation(const FileEntry *File,
                                    unsigned Offset) const {
  const SourceManager &SM = getSourceManager();
  SourceLocation FileLoc = SM.translateFileLineCol(File, 1, 1);
  return SM.getMacroArgExpandedLocation(FileLoc.getLocWithOffset(Offset));
}

/// If \arg Loc is a loaded location from the preamble, returns
/// the corresponding local location of the main file, otherwise it returns
/// \arg Loc.
SourceLocation ASTUnit::mapLocationFromPreamble(SourceLocation Loc) const {
  FileID PreambleID;
  if (SourceMgr)
    PreambleID = SourceMgr->getPreambleFileID();

  if (Loc.isInvalid() || !Preamble || PreambleID.isInvalid())
    return Loc;

  unsigned Offs;
  if (SourceMgr->isInFileID(Loc, PreambleID, &Offs) && Offs < Preamble->getBounds().Size) {
    SourceLocation FileLoc
        = SourceMgr->getLocForStartOfFile(SourceMgr->getMainFileID());
    return FileLoc.getLocWithOffset(Offs);
  }

  return Loc;
}

/// If \arg Loc is a local location of the main file but inside the
/// preamble chunk, returns the corresponding loaded location from the
/// preamble, otherwise it returns \arg Loc.
SourceLocation ASTUnit::mapLocationToPreamble(SourceLocation Loc) const {
  FileID PreambleID;
  if (SourceMgr)
    PreambleID = SourceMgr->getPreambleFileID();

  if (Loc.isInvalid() || !Preamble || PreambleID.isInvalid())
    return Loc;

  unsigned Offs;
  if (SourceMgr->isInFileID(Loc, SourceMgr->getMainFileID(), &Offs) &&
      Offs < Preamble->getBounds().Size) {
    SourceLocation FileLoc = SourceMgr->getLocForStartOfFile(PreambleID);
    return FileLoc.getLocWithOffset(Offs);
  }

  return Loc;
}

bool ASTUnit::isInPreambleFileID(SourceLocation Loc) const {
  FileID FID;
  if (SourceMgr)
    FID = SourceMgr->getPreambleFileID();

  if (Loc.isInvalid() || FID.isInvalid())
    return false;

  return SourceMgr->isInFileID(Loc, FID);
}

bool ASTUnit::isInMainFileID(SourceLocation Loc) const {
  FileID FID;
  if (SourceMgr)
    FID = SourceMgr->getMainFileID();

  if (Loc.isInvalid() || FID.isInvalid())
    return false;

  return SourceMgr->isInFileID(Loc, FID);
}

SourceLocation ASTUnit::getEndOfPreambleFileID() const {
  FileID FID;
  if (SourceMgr)
    FID = SourceMgr->getPreambleFileID();

  if (FID.isInvalid())
    return {};

  return SourceMgr->getLocForEndOfFile(FID);
}

SourceLocation ASTUnit::getStartOfMainFileID() const {
  FileID FID;
  if (SourceMgr)
    FID = SourceMgr->getMainFileID();

  if (FID.isInvalid())
    return {};

  return SourceMgr->getLocForStartOfFile(FID);
}

llvm::iterator_range<PreprocessingRecord::iterator>
ASTUnit::getLocalPreprocessingEntities() const {
  if (isMainFileAST()) {
    serialization::ModuleFile &
      Mod = Reader->getModuleManager().getPrimaryModule();
    return Reader->getModulePreprocessedEntities(Mod);
  }

  if (PreprocessingRecord *PPRec = PP->getPreprocessingRecord())
    return llvm::make_range(PPRec->local_begin(), PPRec->local_end());

  return llvm::make_range(PreprocessingRecord::iterator(),
                          PreprocessingRecord::iterator());
}

bool ASTUnit::visitLocalTopLevelDecls(void *context, DeclVisitorFn Fn) {
  if (isMainFileAST()) {
    serialization::ModuleFile &
      Mod = Reader->getModuleManager().getPrimaryModule();
    for (const auto *D : Reader->getModuleFileLevelDecls(Mod)) {
      if (!Fn(context, D))
        return false;
    }

    return true;
  }

  for (ASTUnit::top_level_iterator TL = top_level_begin(),
                                TLEnd = top_level_end();
         TL != TLEnd; ++TL) {
    if (!Fn(context, *TL))
      return false;
  }

  return true;
}

OptionalFileEntryRef ASTUnit::getPCHFile() {
  if (!Reader)
    return std::nullopt;

  serialization::ModuleFile *Mod = nullptr;
  Reader->getModuleManager().visit([&Mod](serialization::ModuleFile &M) {
    switch (M.Kind) {
    case serialization::MK_ImplicitModule:
    case serialization::MK_ExplicitModule:
    case serialization::MK_PrebuiltModule:
      return true; // skip dependencies.
    case serialization::MK_PCH:
      Mod = &M;
      return true; // found it.
    case serialization::MK_Preamble:
      return false; // look in dependencies.
    case serialization::MK_MainFile:
      return false; // look in dependencies.
    }

    return true;
  });
  if (Mod)
    return Mod->File;

  return std::nullopt;
}

bool ASTUnit::isModuleFile() const {
  return isMainFileAST() && getLangOpts().isCompilingModule();
}

InputKind ASTUnit::getInputKind() const {
  auto &LangOpts = getLangOpts();

  Language Lang;
  if (LangOpts.OpenCL)
    Lang = Language::OpenCL;
  else if (LangOpts.CUDA)
    Lang = Language::CUDA;
  else if (LangOpts.RenderScript)
    Lang = Language::RenderScript;
  else if (LangOpts.CPlusPlus)
    Lang = LangOpts.ObjC ? Language::ObjCXX : Language::CXX;
  else
    Lang = LangOpts.ObjC ? Language::ObjC : Language::C;

  InputKind::Format Fmt = InputKind::Source;
  if (LangOpts.getCompilingModule() == LangOptions::CMK_ModuleMap)
    Fmt = InputKind::ModuleMap;

  // We don't know if input was preprocessed. Assume not.
  bool PP = false;

  return InputKind(Lang, Fmt, PP);
}

#ifndef NDEBUG
ASTUnit::ConcurrencyState::ConcurrencyState() {
  Mutex = new std::recursive_mutex;
}

ASTUnit::ConcurrencyState::~ConcurrencyState() {
  delete static_cast<std::recursive_mutex *>(Mutex);
}

void ASTUnit::ConcurrencyState::start() {
  bool acquired = static_cast<std::recursive_mutex *>(Mutex)->try_lock();
  assert(acquired && "Concurrent access to ASTUnit!");
}

void ASTUnit::ConcurrencyState::finish() {
  static_cast<std::recursive_mutex *>(Mutex)->unlock();
}

#else // NDEBUG

ASTUnit::ConcurrencyState::ConcurrencyState() { Mutex = nullptr; }
ASTUnit::ConcurrencyState::~ConcurrencyState() {}
void ASTUnit::ConcurrencyState::start() {}
void ASTUnit::ConcurrencyState::finish() {}

#endif // NDEBUG
