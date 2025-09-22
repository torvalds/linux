//===- Preprocessor.h - C Language Family Preprocessor ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::Preprocessor interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PREPROCESSOR_H
#define LLVM_CLANG_LEX_PREPROCESSOR_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/PPEmbedParameters.h"
#include "clang/Lex/Token.h"
#include "clang/Lex/TokenLexer.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Registry.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

template<unsigned InternalLen> class SmallString;

} // namespace llvm

namespace clang {

class CodeCompletionHandler;
class CommentHandler;
class DirectoryEntry;
class EmptylineHandler;
class ExternalPreprocessorSource;
class FileEntry;
class FileManager;
class HeaderSearch;
class MacroArgs;
class PragmaHandler;
class PragmaNamespace;
class PreprocessingRecord;
class PreprocessorLexer;
class PreprocessorOptions;
class ScratchBuffer;
class TargetInfo;

namespace Builtin {
class Context;
}

/// Stores token information for comparing actual tokens with
/// predefined values.  Only handles simple tokens and identifiers.
class TokenValue {
  tok::TokenKind Kind;
  IdentifierInfo *II;

public:
  TokenValue(tok::TokenKind Kind) : Kind(Kind), II(nullptr) {
    assert(Kind != tok::raw_identifier && "Raw identifiers are not supported.");
    assert(Kind != tok::identifier &&
           "Identifiers should be created by TokenValue(IdentifierInfo *)");
    assert(!tok::isLiteral(Kind) && "Literals are not supported.");
    assert(!tok::isAnnotation(Kind) && "Annotations are not supported.");
  }

  TokenValue(IdentifierInfo *II) : Kind(tok::identifier), II(II) {}

  bool operator==(const Token &Tok) const {
    return Tok.getKind() == Kind &&
        (!II || II == Tok.getIdentifierInfo());
  }
};

/// Context in which macro name is used.
enum MacroUse {
  // other than #define or #undef
  MU_Other  = 0,

  // macro name specified in #define
  MU_Define = 1,

  // macro name specified in #undef
  MU_Undef  = 2
};

enum class EmbedResult {
  Invalid = -1, // Parsing error occurred.
  NotFound = 0, // Corresponds to __STDC_EMBED_NOT_FOUND__
  Found = 1,    // Corresponds to __STDC_EMBED_FOUND__
  Empty = 2,    // Corresponds to __STDC_EMBED_EMPTY__
};

/// Engages in a tight little dance with the lexer to efficiently
/// preprocess tokens.
///
/// Lexers know only about tokens within a single source file, and don't
/// know anything about preprocessor-level issues like the \#include stack,
/// token expansion, etc.
class Preprocessor {
  friend class VAOptDefinitionContext;
  friend class VariadicMacroScopeGuard;

  llvm::unique_function<void(const clang::Token &)> OnToken;
  std::shared_ptr<PreprocessorOptions> PPOpts;
  DiagnosticsEngine        *Diags;
  const LangOptions &LangOpts;
  const TargetInfo *Target = nullptr;
  const TargetInfo *AuxTarget = nullptr;
  FileManager       &FileMgr;
  SourceManager     &SourceMgr;
  std::unique_ptr<ScratchBuffer> ScratchBuf;
  HeaderSearch      &HeaderInfo;
  ModuleLoader      &TheModuleLoader;

  /// External source of macros.
  ExternalPreprocessorSource *ExternalSource;

  /// A BumpPtrAllocator object used to quickly allocate and release
  /// objects internal to the Preprocessor.
  llvm::BumpPtrAllocator BP;

  /// Identifiers for builtin macros and other builtins.
  IdentifierInfo *Ident__LINE__, *Ident__FILE__;   // __LINE__, __FILE__
  IdentifierInfo *Ident__DATE__, *Ident__TIME__;   // __DATE__, __TIME__
  IdentifierInfo *Ident__INCLUDE_LEVEL__;          // __INCLUDE_LEVEL__
  IdentifierInfo *Ident__BASE_FILE__;              // __BASE_FILE__
  IdentifierInfo *Ident__FILE_NAME__;              // __FILE_NAME__
  IdentifierInfo *Ident__TIMESTAMP__;              // __TIMESTAMP__
  IdentifierInfo *Ident__COUNTER__;                // __COUNTER__
  IdentifierInfo *Ident_Pragma, *Ident__pragma;    // _Pragma, __pragma
  IdentifierInfo *Ident__identifier;               // __identifier
  IdentifierInfo *Ident__VA_ARGS__;                // __VA_ARGS__
  IdentifierInfo *Ident__VA_OPT__;                 // __VA_OPT__
  IdentifierInfo *Ident__has_feature;              // __has_feature
  IdentifierInfo *Ident__has_extension;            // __has_extension
  IdentifierInfo *Ident__has_builtin;              // __has_builtin
  IdentifierInfo *Ident__has_constexpr_builtin;    // __has_constexpr_builtin
  IdentifierInfo *Ident__has_attribute;            // __has_attribute
  IdentifierInfo *Ident__has_embed;                // __has_embed
  IdentifierInfo *Ident__has_include;              // __has_include
  IdentifierInfo *Ident__has_include_next;         // __has_include_next
  IdentifierInfo *Ident__has_warning;              // __has_warning
  IdentifierInfo *Ident__is_identifier;            // __is_identifier
  IdentifierInfo *Ident__building_module;          // __building_module
  IdentifierInfo *Ident__MODULE__;                 // __MODULE__
  IdentifierInfo *Ident__has_cpp_attribute;        // __has_cpp_attribute
  IdentifierInfo *Ident__has_c_attribute;          // __has_c_attribute
  IdentifierInfo *Ident__has_declspec;             // __has_declspec_attribute
  IdentifierInfo *Ident__is_target_arch;           // __is_target_arch
  IdentifierInfo *Ident__is_target_vendor;         // __is_target_vendor
  IdentifierInfo *Ident__is_target_os;             // __is_target_os
  IdentifierInfo *Ident__is_target_environment;    // __is_target_environment
  IdentifierInfo *Ident__is_target_variant_os;
  IdentifierInfo *Ident__is_target_variant_environment;
  IdentifierInfo *Ident__FLT_EVAL_METHOD__;        // __FLT_EVAL_METHOD

  // Weak, only valid (and set) while InMacroArgs is true.
  Token* ArgMacro;

  SourceLocation DATELoc, TIMELoc;

  // FEM_UnsetOnCommandLine means that an explicit evaluation method was
  // not specified on the command line. The target is queried to set the
  // default evaluation method.
  LangOptions::FPEvalMethodKind CurrentFPEvalMethod =
      LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine;

  // The most recent pragma location where the floating point evaluation
  // method was modified. This is used to determine whether the
  // 'pragma clang fp eval_method' was used whithin the current scope.
  SourceLocation LastFPEvalPragmaLocation;

  LangOptions::FPEvalMethodKind TUFPEvalMethod =
      LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine;

  // Next __COUNTER__ value, starts at 0.
  unsigned CounterValue = 0;

  enum {
    /// Maximum depth of \#includes.
    MaxAllowedIncludeStackDepth = 200
  };

  // State that is set before the preprocessor begins.
  bool KeepComments : 1;
  bool KeepMacroComments : 1;
  bool SuppressIncludeNotFoundError : 1;

  // State that changes while the preprocessor runs:
  bool InMacroArgs : 1;            // True if parsing fn macro invocation args.

  /// Whether the preprocessor owns the header search object.
  bool OwnsHeaderSearch : 1;

  /// True if macro expansion is disabled.
  bool DisableMacroExpansion : 1;

  /// Temporarily disables DisableMacroExpansion (i.e. enables expansion)
  /// when parsing preprocessor directives.
  bool MacroExpansionInDirectivesOverride : 1;

  class ResetMacroExpansionHelper;

  /// Whether we have already loaded macros from the external source.
  mutable bool ReadMacrosFromExternalSource : 1;

  /// True if pragmas are enabled.
  bool PragmasEnabled : 1;

  /// True if the current build action is a preprocessing action.
  bool PreprocessedOutput : 1;

  /// True if we are currently preprocessing a #if or #elif directive
  bool ParsingIfOrElifDirective;

  /// True if we are pre-expanding macro arguments.
  bool InMacroArgPreExpansion;

  /// Mapping/lookup information for all identifiers in
  /// the program, including program keywords.
  mutable IdentifierTable Identifiers;

  /// This table contains all the selectors in the program.
  ///
  /// Unlike IdentifierTable above, this table *isn't* populated by the
  /// preprocessor. It is declared/expanded here because its role/lifetime is
  /// conceptually similar to the IdentifierTable. In addition, the current
  /// control flow (in clang::ParseAST()), make it convenient to put here.
  ///
  /// FIXME: Make sure the lifetime of Identifiers/Selectors *isn't* tied to
  /// the lifetime of the preprocessor.
  SelectorTable Selectors;

  /// Information about builtins.
  std::unique_ptr<Builtin::Context> BuiltinInfo;

  /// Tracks all of the pragmas that the client registered
  /// with this preprocessor.
  std::unique_ptr<PragmaNamespace> PragmaHandlers;

  /// Pragma handlers of the original source is stored here during the
  /// parsing of a model file.
  std::unique_ptr<PragmaNamespace> PragmaHandlersBackup;

  /// Tracks all of the comment handlers that the client registered
  /// with this preprocessor.
  std::vector<CommentHandler *> CommentHandlers;

  /// Empty line handler.
  EmptylineHandler *Emptyline = nullptr;

  /// True to avoid tearing down the lexer etc on EOF
  bool IncrementalProcessing = false;

public:
  /// The kind of translation unit we are processing.
  const TranslationUnitKind TUKind;

  /// Returns a pointer into the given file's buffer that's guaranteed
  /// to be between tokens. The returned pointer is always before \p Start.
  /// The maximum distance betweenthe returned pointer and \p Start is
  /// limited by a constant value, but also an implementation detail.
  /// If no such check point exists, \c nullptr is returned.
  const char *getCheckPoint(FileID FID, const char *Start) const;

private:
  /// The code-completion handler.
  CodeCompletionHandler *CodeComplete = nullptr;

  /// The file that we're performing code-completion for, if any.
  const FileEntry *CodeCompletionFile = nullptr;

  /// The offset in file for the code-completion point.
  unsigned CodeCompletionOffset = 0;

  /// The location for the code-completion point. This gets instantiated
  /// when the CodeCompletionFile gets \#include'ed for preprocessing.
  SourceLocation CodeCompletionLoc;

  /// The start location for the file of the code-completion point.
  ///
  /// This gets instantiated when the CodeCompletionFile gets \#include'ed
  /// for preprocessing.
  SourceLocation CodeCompletionFileLoc;

  /// The source location of the \c import contextual keyword we just
  /// lexed, if any.
  SourceLocation ModuleImportLoc;

  /// The import path for named module that we're currently processing.
  SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> NamedModuleImportPath;

  llvm::DenseMap<FileID, SmallVector<const char *>> CheckPoints;
  unsigned CheckPointCounter = 0;

  /// Whether the import is an `@import` or a standard c++ modules import.
  bool IsAtImport = false;

  /// Whether the last token we lexed was an '@'.
  bool LastTokenWasAt = false;

  /// A position within a C++20 import-seq.
  class StdCXXImportSeq {
  public:
    enum State : int {
      // Positive values represent a number of unclosed brackets.
      AtTopLevel = 0,
      AfterTopLevelTokenSeq = -1,
      AfterExport = -2,
      AfterImportSeq = -3,
    };

    StdCXXImportSeq(State S) : S(S) {}

    /// Saw any kind of open bracket.
    void handleOpenBracket() {
      S = static_cast<State>(std::max<int>(S, 0) + 1);
    }
    /// Saw any kind of close bracket other than '}'.
    void handleCloseBracket() {
      S = static_cast<State>(std::max<int>(S, 1) - 1);
    }
    /// Saw a close brace.
    void handleCloseBrace() {
      handleCloseBracket();
      if (S == AtTopLevel && !AfterHeaderName)
        S = AfterTopLevelTokenSeq;
    }
    /// Saw a semicolon.
    void handleSemi() {
      if (atTopLevel()) {
        S = AfterTopLevelTokenSeq;
        AfterHeaderName = false;
      }
    }

    /// Saw an 'export' identifier.
    void handleExport() {
      if (S == AfterTopLevelTokenSeq)
        S = AfterExport;
      else if (S <= 0)
        S = AtTopLevel;
    }
    /// Saw an 'import' identifier.
    void handleImport() {
      if (S == AfterTopLevelTokenSeq || S == AfterExport)
        S = AfterImportSeq;
      else if (S <= 0)
        S = AtTopLevel;
    }

    /// Saw a 'header-name' token; do not recognize any more 'import' tokens
    /// until we reach a top-level semicolon.
    void handleHeaderName() {
      if (S == AfterImportSeq)
        AfterHeaderName = true;
      handleMisc();
    }

    /// Saw any other token.
    void handleMisc() {
      if (S <= 0)
        S = AtTopLevel;
    }

    bool atTopLevel() { return S <= 0; }
    bool afterImportSeq() { return S == AfterImportSeq; }
    bool afterTopLevelSeq() { return S == AfterTopLevelTokenSeq; }

  private:
    State S;
    /// Whether we're in the pp-import-suffix following the header-name in a
    /// pp-import. If so, a close-brace is not sufficient to end the
    /// top-level-token-seq of an import-seq.
    bool AfterHeaderName = false;
  };

  /// Our current position within a C++20 import-seq.
  StdCXXImportSeq StdCXXImportSeqState = StdCXXImportSeq::AfterTopLevelTokenSeq;

  /// Track whether we are in a Global Module Fragment
  class TrackGMF {
  public:
    enum GMFState : int {
      GMFActive = 1,
      MaybeGMF = 0,
      BeforeGMFIntroducer = -1,
      GMFAbsentOrEnded = -2,
    };

    TrackGMF(GMFState S) : S(S) {}

    /// Saw a semicolon.
    void handleSemi() {
      // If it is immediately after the first instance of the module keyword,
      // then that introduces the GMF.
      if (S == MaybeGMF)
        S = GMFActive;
    }

    /// Saw an 'export' identifier.
    void handleExport() {
      // The presence of an 'export' keyword always ends or excludes a GMF.
      S = GMFAbsentOrEnded;
    }

    /// Saw an 'import' identifier.
    void handleImport(bool AfterTopLevelTokenSeq) {
      // If we see this before any 'module' kw, then we have no GMF.
      if (AfterTopLevelTokenSeq && S == BeforeGMFIntroducer)
        S = GMFAbsentOrEnded;
    }

    /// Saw a 'module' identifier.
    void handleModule(bool AfterTopLevelTokenSeq) {
      // This was the first module identifier and not preceded by any token
      // that would exclude a GMF.  It could begin a GMF, but only if directly
      // followed by a semicolon.
      if (AfterTopLevelTokenSeq && S == BeforeGMFIntroducer)
        S = MaybeGMF;
      else
        S = GMFAbsentOrEnded;
    }

    /// Saw any other token.
    void handleMisc() {
      // We saw something other than ; after the 'module' kw, so not a GMF.
      if (S == MaybeGMF)
        S = GMFAbsentOrEnded;
    }

    bool inGMF() { return S == GMFActive; }

  private:
    /// Track the transitions into and out of a Global Module Fragment,
    /// if one is present.
    GMFState S;
  };

  TrackGMF TrackGMFState = TrackGMF::BeforeGMFIntroducer;

  /// Track the status of the c++20 module decl.
  ///
  ///   module-declaration:
  ///     'export'[opt] 'module' module-name module-partition[opt]
  ///     attribute-specifier-seq[opt] ';'
  ///
  ///   module-name:
  ///     module-name-qualifier[opt] identifier
  ///
  ///   module-partition:
  ///     ':' module-name-qualifier[opt] identifier
  ///
  ///   module-name-qualifier:
  ///     identifier '.'
  ///     module-name-qualifier identifier '.'
  ///
  /// Transition state:
  ///
  ///   NotAModuleDecl --- export ---> FoundExport
  ///   NotAModuleDecl --- module ---> ImplementationCandidate
  ///   FoundExport --- module ---> InterfaceCandidate
  ///   ImplementationCandidate --- Identifier ---> ImplementationCandidate
  ///   ImplementationCandidate --- period ---> ImplementationCandidate
  ///   ImplementationCandidate --- colon ---> ImplementationCandidate
  ///   InterfaceCandidate --- Identifier ---> InterfaceCandidate
  ///   InterfaceCandidate --- period ---> InterfaceCandidate
  ///   InterfaceCandidate --- colon ---> InterfaceCandidate
  ///   ImplementationCandidate --- Semi ---> NamedModuleImplementation
  ///   NamedModuleInterface --- Semi ---> NamedModuleInterface
  ///   NamedModuleImplementation --- Anything ---> NamedModuleImplementation
  ///   NamedModuleInterface --- Anything ---> NamedModuleInterface
  ///
  /// FIXME: We haven't handle attribute-specifier-seq here. It may not be bad
  /// soon since we don't support any module attributes yet.
  class ModuleDeclSeq {
    enum ModuleDeclState : int {
      NotAModuleDecl,
      FoundExport,
      InterfaceCandidate,
      ImplementationCandidate,
      NamedModuleInterface,
      NamedModuleImplementation,
    };

  public:
    ModuleDeclSeq() = default;

    void handleExport() {
      if (State == NotAModuleDecl)
        State = FoundExport;
      else if (!isNamedModule())
        reset();
    }

    void handleModule() {
      if (State == FoundExport)
        State = InterfaceCandidate;
      else if (State == NotAModuleDecl)
        State = ImplementationCandidate;
      else if (!isNamedModule())
        reset();
    }

    void handleIdentifier(IdentifierInfo *Identifier) {
      if (isModuleCandidate() && Identifier)
        Name += Identifier->getName().str();
      else if (!isNamedModule())
        reset();
    }

    void handleColon() {
      if (isModuleCandidate())
        Name += ":";
      else if (!isNamedModule())
        reset();
    }

    void handlePeriod() {
      if (isModuleCandidate())
        Name += ".";
      else if (!isNamedModule())
        reset();
    }

    void handleSemi() {
      if (!Name.empty() && isModuleCandidate()) {
        if (State == InterfaceCandidate)
          State = NamedModuleInterface;
        else if (State == ImplementationCandidate)
          State = NamedModuleImplementation;
        else
          llvm_unreachable("Unimaged ModuleDeclState.");
      } else if (!isNamedModule())
        reset();
    }

    void handleMisc() {
      if (!isNamedModule())
        reset();
    }

    bool isModuleCandidate() const {
      return State == InterfaceCandidate || State == ImplementationCandidate;
    }

    bool isNamedModule() const {
      return State == NamedModuleInterface ||
             State == NamedModuleImplementation;
    }

    bool isNamedInterface() const { return State == NamedModuleInterface; }

    bool isImplementationUnit() const {
      return State == NamedModuleImplementation && !getName().contains(':');
    }

    StringRef getName() const {
      assert(isNamedModule() && "Can't get name from a non named module");
      return Name;
    }

    StringRef getPrimaryName() const {
      assert(isNamedModule() && "Can't get name from a non named module");
      return getName().split(':').first;
    }

    void reset() {
      Name.clear();
      State = NotAModuleDecl;
    }

  private:
    ModuleDeclState State = NotAModuleDecl;
    std::string Name;
  };

  ModuleDeclSeq ModuleDeclState;

  /// Whether the module import expects an identifier next. Otherwise,
  /// it expects a '.' or ';'.
  bool ModuleImportExpectsIdentifier = false;

  /// The identifier and source location of the currently-active
  /// \#pragma clang arc_cf_code_audited begin.
  std::pair<IdentifierInfo *, SourceLocation> PragmaARCCFCodeAuditedInfo;

  /// The source location of the currently-active
  /// \#pragma clang assume_nonnull begin.
  SourceLocation PragmaAssumeNonNullLoc;

  /// Set only for preambles which end with an active
  /// \#pragma clang assume_nonnull begin.
  ///
  /// When the preamble is loaded into the main file,
  /// `PragmaAssumeNonNullLoc` will be set to this to
  /// replay the unterminated assume_nonnull.
  SourceLocation PreambleRecordedPragmaAssumeNonNullLoc;

  /// True if we hit the code-completion point.
  bool CodeCompletionReached = false;

  /// The code completion token containing the information
  /// on the stem that is to be code completed.
  IdentifierInfo *CodeCompletionII = nullptr;

  /// Range for the code completion token.
  SourceRange CodeCompletionTokenRange;

  /// The directory that the main file should be considered to occupy,
  /// if it does not correspond to a real file (as happens when building a
  /// module).
  OptionalDirectoryEntryRef MainFileDir;

  /// The number of bytes that we will initially skip when entering the
  /// main file, along with a flag that indicates whether skipping this number
  /// of bytes will place the lexer at the start of a line.
  ///
  /// This is used when loading a precompiled preamble.
  std::pair<int, bool> SkipMainFilePreamble;

  /// Whether we hit an error due to reaching max allowed include depth. Allows
  /// to avoid hitting the same error over and over again.
  bool HasReachedMaxIncludeDepth = false;

  /// The number of currently-active calls to Lex.
  ///
  /// Lex is reentrant, and asking for an (end-of-phase-4) token can often
  /// require asking for multiple additional tokens. This counter makes it
  /// possible for Lex to detect whether it's producing a token for the end
  /// of phase 4 of translation or for some other situation.
  unsigned LexLevel = 0;

  /// The number of (LexLevel 0) preprocessor tokens.
  unsigned TokenCount = 0;

  /// Preprocess every token regardless of LexLevel.
  bool PreprocessToken = false;

  /// The maximum number of (LexLevel 0) tokens before issuing a -Wmax-tokens
  /// warning, or zero for unlimited.
  unsigned MaxTokens = 0;
  SourceLocation MaxTokensOverrideLoc;

public:
  struct PreambleSkipInfo {
    SourceLocation HashTokenLoc;
    SourceLocation IfTokenLoc;
    bool FoundNonSkipPortion;
    bool FoundElse;
    SourceLocation ElseLoc;

    PreambleSkipInfo(SourceLocation HashTokenLoc, SourceLocation IfTokenLoc,
                     bool FoundNonSkipPortion, bool FoundElse,
                     SourceLocation ElseLoc)
        : HashTokenLoc(HashTokenLoc), IfTokenLoc(IfTokenLoc),
          FoundNonSkipPortion(FoundNonSkipPortion), FoundElse(FoundElse),
          ElseLoc(ElseLoc) {}
  };

  using IncludedFilesSet = llvm::DenseSet<const FileEntry *>;

private:
  friend class ASTReader;
  friend class MacroArgs;

  class PreambleConditionalStackStore {
    enum State {
      Off = 0,
      Recording = 1,
      Replaying = 2,
    };

  public:
    PreambleConditionalStackStore() = default;

    void startRecording() { ConditionalStackState = Recording; }
    void startReplaying() { ConditionalStackState = Replaying; }
    bool isRecording() const { return ConditionalStackState == Recording; }
    bool isReplaying() const { return ConditionalStackState == Replaying; }

    ArrayRef<PPConditionalInfo> getStack() const {
      return ConditionalStack;
    }

    void doneReplaying() {
      ConditionalStack.clear();
      ConditionalStackState = Off;
    }

    void setStack(ArrayRef<PPConditionalInfo> s) {
      if (!isRecording() && !isReplaying())
        return;
      ConditionalStack.clear();
      ConditionalStack.append(s.begin(), s.end());
    }

    bool hasRecordedPreamble() const { return !ConditionalStack.empty(); }

    bool reachedEOFWhileSkipping() const { return SkipInfo.has_value(); }

    void clearSkipInfo() { SkipInfo.reset(); }

    std::optional<PreambleSkipInfo> SkipInfo;

  private:
    SmallVector<PPConditionalInfo, 4> ConditionalStack;
    State ConditionalStackState = Off;
  } PreambleConditionalStack;

  /// The current top of the stack that we're lexing from if
  /// not expanding a macro and we are lexing directly from source code.
  ///
  /// Only one of CurLexer, or CurTokenLexer will be non-null.
  std::unique_ptr<Lexer> CurLexer;

  /// The current top of the stack that we're lexing from
  /// if not expanding a macro.
  ///
  /// This is an alias for CurLexer.
  PreprocessorLexer *CurPPLexer = nullptr;

  /// Used to find the current FileEntry, if CurLexer is non-null
  /// and if applicable.
  ///
  /// This allows us to implement \#include_next and find directory-specific
  /// properties.
  ConstSearchDirIterator CurDirLookup = nullptr;

  /// The current macro we are expanding, if we are expanding a macro.
  ///
  /// One of CurLexer and CurTokenLexer must be null.
  std::unique_ptr<TokenLexer> CurTokenLexer;

  /// The kind of lexer we're currently working with.
  typedef bool (*LexerCallback)(Preprocessor &, Token &);
  LexerCallback CurLexerCallback = &CLK_Lexer;

  /// If the current lexer is for a submodule that is being built, this
  /// is that submodule.
  Module *CurLexerSubmodule = nullptr;

  /// Keeps track of the stack of files currently
  /// \#included, and macros currently being expanded from, not counting
  /// CurLexer/CurTokenLexer.
  struct IncludeStackInfo {
    LexerCallback               CurLexerCallback;
    Module                     *TheSubmodule;
    std::unique_ptr<Lexer>      TheLexer;
    PreprocessorLexer          *ThePPLexer;
    std::unique_ptr<TokenLexer> TheTokenLexer;
    ConstSearchDirIterator      TheDirLookup;

    // The following constructors are completely useless copies of the default
    // versions, only needed to pacify MSVC.
    IncludeStackInfo(LexerCallback CurLexerCallback, Module *TheSubmodule,
                     std::unique_ptr<Lexer> &&TheLexer,
                     PreprocessorLexer *ThePPLexer,
                     std::unique_ptr<TokenLexer> &&TheTokenLexer,
                     ConstSearchDirIterator TheDirLookup)
        : CurLexerCallback(std::move(CurLexerCallback)),
          TheSubmodule(std::move(TheSubmodule)), TheLexer(std::move(TheLexer)),
          ThePPLexer(std::move(ThePPLexer)),
          TheTokenLexer(std::move(TheTokenLexer)),
          TheDirLookup(std::move(TheDirLookup)) {}
  };
  std::vector<IncludeStackInfo> IncludeMacroStack;

  /// Actions invoked when some preprocessor activity is
  /// encountered (e.g. a file is \#included, etc).
  std::unique_ptr<PPCallbacks> Callbacks;

  struct MacroExpandsInfo {
    Token Tok;
    MacroDefinition MD;
    SourceRange Range;

    MacroExpandsInfo(Token Tok, MacroDefinition MD, SourceRange Range)
        : Tok(Tok), MD(MD), Range(Range) {}
  };
  SmallVector<MacroExpandsInfo, 2> DelayedMacroExpandsCallbacks;

  /// Information about a name that has been used to define a module macro.
  struct ModuleMacroInfo {
    /// The most recent macro directive for this identifier.
    MacroDirective *MD;

    /// The active module macros for this identifier.
    llvm::TinyPtrVector<ModuleMacro *> ActiveModuleMacros;

    /// The generation number at which we last updated ActiveModuleMacros.
    /// \see Preprocessor::VisibleModules.
    unsigned ActiveModuleMacrosGeneration = 0;

    /// Whether this macro name is ambiguous.
    bool IsAmbiguous = false;

    /// The module macros that are overridden by this macro.
    llvm::TinyPtrVector<ModuleMacro *> OverriddenMacros;

    ModuleMacroInfo(MacroDirective *MD) : MD(MD) {}
  };

  /// The state of a macro for an identifier.
  class MacroState {
    mutable llvm::PointerUnion<MacroDirective *, ModuleMacroInfo *> State;

    ModuleMacroInfo *getModuleInfo(Preprocessor &PP,
                                   const IdentifierInfo *II) const {
      if (II->isOutOfDate())
        PP.updateOutOfDateIdentifier(*II);
      // FIXME: Find a spare bit on IdentifierInfo and store a
      //        HasModuleMacros flag.
      if (!II->hasMacroDefinition() ||
          (!PP.getLangOpts().Modules &&
           !PP.getLangOpts().ModulesLocalVisibility) ||
          !PP.CurSubmoduleState->VisibleModules.getGeneration())
        return nullptr;

      auto *Info = State.dyn_cast<ModuleMacroInfo*>();
      if (!Info) {
        Info = new (PP.getPreprocessorAllocator())
            ModuleMacroInfo(State.get<MacroDirective *>());
        State = Info;
      }

      if (PP.CurSubmoduleState->VisibleModules.getGeneration() !=
          Info->ActiveModuleMacrosGeneration)
        PP.updateModuleMacroInfo(II, *Info);
      return Info;
    }

  public:
    MacroState() : MacroState(nullptr) {}
    MacroState(MacroDirective *MD) : State(MD) {}

    MacroState(MacroState &&O) noexcept : State(O.State) {
      O.State = (MacroDirective *)nullptr;
    }

    MacroState &operator=(MacroState &&O) noexcept {
      auto S = O.State;
      O.State = (MacroDirective *)nullptr;
      State = S;
      return *this;
    }

    ~MacroState() {
      if (auto *Info = State.dyn_cast<ModuleMacroInfo*>())
        Info->~ModuleMacroInfo();
    }

    MacroDirective *getLatest() const {
      if (auto *Info = State.dyn_cast<ModuleMacroInfo*>())
        return Info->MD;
      return State.get<MacroDirective*>();
    }

    void setLatest(MacroDirective *MD) {
      if (auto *Info = State.dyn_cast<ModuleMacroInfo*>())
        Info->MD = MD;
      else
        State = MD;
    }

    bool isAmbiguous(Preprocessor &PP, const IdentifierInfo *II) const {
      auto *Info = getModuleInfo(PP, II);
      return Info ? Info->IsAmbiguous : false;
    }

    ArrayRef<ModuleMacro *>
    getActiveModuleMacros(Preprocessor &PP, const IdentifierInfo *II) const {
      if (auto *Info = getModuleInfo(PP, II))
        return Info->ActiveModuleMacros;
      return std::nullopt;
    }

    MacroDirective::DefInfo findDirectiveAtLoc(SourceLocation Loc,
                                               SourceManager &SourceMgr) const {
      // FIXME: Incorporate module macros into the result of this.
      if (auto *Latest = getLatest())
        return Latest->findDirectiveAtLoc(Loc, SourceMgr);
      return {};
    }

    void overrideActiveModuleMacros(Preprocessor &PP, IdentifierInfo *II) {
      if (auto *Info = getModuleInfo(PP, II)) {
        Info->OverriddenMacros.insert(Info->OverriddenMacros.end(),
                                      Info->ActiveModuleMacros.begin(),
                                      Info->ActiveModuleMacros.end());
        Info->ActiveModuleMacros.clear();
        Info->IsAmbiguous = false;
      }
    }

    ArrayRef<ModuleMacro*> getOverriddenMacros() const {
      if (auto *Info = State.dyn_cast<ModuleMacroInfo*>())
        return Info->OverriddenMacros;
      return std::nullopt;
    }

    void setOverriddenMacros(Preprocessor &PP,
                             ArrayRef<ModuleMacro *> Overrides) {
      auto *Info = State.dyn_cast<ModuleMacroInfo*>();
      if (!Info) {
        if (Overrides.empty())
          return;
        Info = new (PP.getPreprocessorAllocator())
            ModuleMacroInfo(State.get<MacroDirective *>());
        State = Info;
      }
      Info->OverriddenMacros.clear();
      Info->OverriddenMacros.insert(Info->OverriddenMacros.end(),
                                    Overrides.begin(), Overrides.end());
      Info->ActiveModuleMacrosGeneration = 0;
    }
  };

  /// For each IdentifierInfo that was associated with a macro, we
  /// keep a mapping to the history of all macro definitions and #undefs in
  /// the reverse order (the latest one is in the head of the list).
  ///
  /// This mapping lives within the \p CurSubmoduleState.
  using MacroMap = llvm::DenseMap<const IdentifierInfo *, MacroState>;

  struct SubmoduleState;

  /// Information about a submodule that we're currently building.
  struct BuildingSubmoduleInfo {
    /// The module that we are building.
    Module *M;

    /// The location at which the module was included.
    SourceLocation ImportLoc;

    /// Whether we entered this submodule via a pragma.
    bool IsPragma;

    /// The previous SubmoduleState.
    SubmoduleState *OuterSubmoduleState;

    /// The number of pending module macro names when we started building this.
    unsigned OuterPendingModuleMacroNames;

    BuildingSubmoduleInfo(Module *M, SourceLocation ImportLoc, bool IsPragma,
                          SubmoduleState *OuterSubmoduleState,
                          unsigned OuterPendingModuleMacroNames)
        : M(M), ImportLoc(ImportLoc), IsPragma(IsPragma),
          OuterSubmoduleState(OuterSubmoduleState),
          OuterPendingModuleMacroNames(OuterPendingModuleMacroNames) {}
  };
  SmallVector<BuildingSubmoduleInfo, 8> BuildingSubmoduleStack;

  /// Information about a submodule's preprocessor state.
  struct SubmoduleState {
    /// The macros for the submodule.
    MacroMap Macros;

    /// The set of modules that are visible within the submodule.
    VisibleModuleSet VisibleModules;

    // FIXME: CounterValue?
    // FIXME: PragmaPushMacroInfo?
  };
  std::map<Module *, SubmoduleState> Submodules;

  /// The preprocessor state for preprocessing outside of any submodule.
  SubmoduleState NullSubmoduleState;

  /// The current submodule state. Will be \p NullSubmoduleState if we're not
  /// in a submodule.
  SubmoduleState *CurSubmoduleState;

  /// The files that have been included.
  IncludedFilesSet IncludedFiles;

  /// The set of top-level modules that affected preprocessing, but were not
  /// imported.
  llvm::SmallSetVector<Module *, 2> AffectingClangModules;

  /// The set of known macros exported from modules.
  llvm::FoldingSet<ModuleMacro> ModuleMacros;

  /// The names of potential module macros that we've not yet processed.
  llvm::SmallVector<IdentifierInfo *, 32> PendingModuleMacroNames;

  /// The list of module macros, for each identifier, that are not overridden by
  /// any other module macro.
  llvm::DenseMap<const IdentifierInfo *, llvm::TinyPtrVector<ModuleMacro *>>
      LeafModuleMacros;

  /// Macros that we want to warn because they are not used at the end
  /// of the translation unit.
  ///
  /// We store just their SourceLocations instead of
  /// something like MacroInfo*. The benefit of this is that when we are
  /// deserializing from PCH, we don't need to deserialize identifier & macros
  /// just so that we can report that they are unused, we just warn using
  /// the SourceLocations of this set (that will be filled by the ASTReader).
  using WarnUnusedMacroLocsTy = llvm::SmallDenseSet<SourceLocation, 32>;
  WarnUnusedMacroLocsTy WarnUnusedMacroLocs;

  /// This is a pair of an optional message and source location used for pragmas
  /// that annotate macros like pragma clang restrict_expansion and pragma clang
  /// deprecated. This pair stores the optional message and the location of the
  /// annotation pragma for use producing diagnostics and notes.
  using MsgLocationPair = std::pair<std::string, SourceLocation>;

  struct MacroAnnotationInfo {
    SourceLocation Location;
    std::string Message;
  };

  struct MacroAnnotations {
    std::optional<MacroAnnotationInfo> DeprecationInfo;
    std::optional<MacroAnnotationInfo> RestrictExpansionInfo;
    std::optional<SourceLocation> FinalAnnotationLoc;

    static MacroAnnotations makeDeprecation(SourceLocation Loc,
                                            std::string Msg) {
      return MacroAnnotations{MacroAnnotationInfo{Loc, std::move(Msg)},
                              std::nullopt, std::nullopt};
    }

    static MacroAnnotations makeRestrictExpansion(SourceLocation Loc,
                                                  std::string Msg) {
      return MacroAnnotations{
          std::nullopt, MacroAnnotationInfo{Loc, std::move(Msg)}, std::nullopt};
    }

    static MacroAnnotations makeFinal(SourceLocation Loc) {
      return MacroAnnotations{std::nullopt, std::nullopt, Loc};
    }
  };

  /// Warning information for macro annotations.
  llvm::DenseMap<const IdentifierInfo *, MacroAnnotations> AnnotationInfos;

  /// A "freelist" of MacroArg objects that can be
  /// reused for quick allocation.
  MacroArgs *MacroArgCache = nullptr;

  /// For each IdentifierInfo used in a \#pragma push_macro directive,
  /// we keep a MacroInfo stack used to restore the previous macro value.
  llvm::DenseMap<IdentifierInfo *, std::vector<MacroInfo *>>
      PragmaPushMacroInfo;

  // Various statistics we track for performance analysis.
  unsigned NumDirectives = 0;
  unsigned NumDefined = 0;
  unsigned NumUndefined = 0;
  unsigned NumPragma = 0;
  unsigned NumIf = 0;
  unsigned NumElse = 0;
  unsigned NumEndif = 0;
  unsigned NumEnteredSourceFiles = 0;
  unsigned MaxIncludeStackDepth = 0;
  unsigned NumMacroExpanded = 0;
  unsigned NumFnMacroExpanded = 0;
  unsigned NumBuiltinMacroExpanded = 0;
  unsigned NumFastMacroExpanded = 0;
  unsigned NumTokenPaste = 0;
  unsigned NumFastTokenPaste = 0;
  unsigned NumSkipped = 0;

  /// The predefined macros that preprocessor should use from the
  /// command line etc.
  std::string Predefines;

  /// The file ID for the preprocessor predefines.
  FileID PredefinesFileID;

  /// The file ID for the PCH through header.
  FileID PCHThroughHeaderFileID;

  /// Whether tokens are being skipped until a #pragma hdrstop is seen.
  bool SkippingUntilPragmaHdrStop = false;

  /// Whether tokens are being skipped until the through header is seen.
  bool SkippingUntilPCHThroughHeader = false;

  /// \{
  /// Cache of macro expanders to reduce malloc traffic.
  enum { TokenLexerCacheSize = 8 };
  unsigned NumCachedTokenLexers;
  std::unique_ptr<TokenLexer> TokenLexerCache[TokenLexerCacheSize];
  /// \}

  /// Keeps macro expanded tokens for TokenLexers.
  //
  /// Works like a stack; a TokenLexer adds the macro expanded tokens that is
  /// going to lex in the cache and when it finishes the tokens are removed
  /// from the end of the cache.
  SmallVector<Token, 16> MacroExpandedTokens;
  std::vector<std::pair<TokenLexer *, size_t>> MacroExpandingLexersStack;

  /// A record of the macro definitions and expansions that
  /// occurred during preprocessing.
  ///
  /// This is an optional side structure that can be enabled with
  /// \c createPreprocessingRecord() prior to preprocessing.
  PreprocessingRecord *Record = nullptr;

  /// Cached tokens state.
  using CachedTokensTy = SmallVector<Token, 1>;

  /// Cached tokens are stored here when we do backtracking or
  /// lookahead. They are "lexed" by the CachingLex() method.
  CachedTokensTy CachedTokens;

  /// The position of the cached token that CachingLex() should
  /// "lex" next.
  ///
  /// If it points beyond the CachedTokens vector, it means that a normal
  /// Lex() should be invoked.
  CachedTokensTy::size_type CachedLexPos = 0;

  /// Stack of backtrack positions, allowing nested backtracks.
  ///
  /// The EnableBacktrackAtThisPos() method pushes a position to
  /// indicate where CachedLexPos should be set when the BackTrack() method is
  /// invoked (at which point the last position is popped).
  std::vector<CachedTokensTy::size_type> BacktrackPositions;

  /// True if \p Preprocessor::SkipExcludedConditionalBlock() is running.
  /// This is used to guard against calling this function recursively.
  ///
  /// See comments at the use-site for more context about why it is needed.
  bool SkippingExcludedConditionalBlock = false;

  /// Keeps track of skipped range mappings that were recorded while skipping
  /// excluded conditional directives. It maps the source buffer pointer at
  /// the beginning of a skipped block, to the number of bytes that should be
  /// skipped.
  llvm::DenseMap<const char *, unsigned> RecordedSkippedRanges;

  void updateOutOfDateIdentifier(const IdentifierInfo &II) const;

public:
  Preprocessor(std::shared_ptr<PreprocessorOptions> PPOpts,
               DiagnosticsEngine &diags, const LangOptions &LangOpts,
               SourceManager &SM, HeaderSearch &Headers,
               ModuleLoader &TheModuleLoader,
               IdentifierInfoLookup *IILookup = nullptr,
               bool OwnsHeaderSearch = false,
               TranslationUnitKind TUKind = TU_Complete);

  ~Preprocessor();

  /// Initialize the preprocessor using information about the target.
  ///
  /// \param Target is owned by the caller and must remain valid for the
  /// lifetime of the preprocessor.
  /// \param AuxTarget is owned by the caller and must remain valid for
  /// the lifetime of the preprocessor.
  void Initialize(const TargetInfo &Target,
                  const TargetInfo *AuxTarget = nullptr);

  /// Initialize the preprocessor to parse a model file
  ///
  /// To parse model files the preprocessor of the original source is reused to
  /// preserver the identifier table. However to avoid some duplicate
  /// information in the preprocessor some cleanup is needed before it is used
  /// to parse model files. This method does that cleanup.
  void InitializeForModelFile();

  /// Cleanup after model file parsing
  void FinalizeForModelFile();

  /// Retrieve the preprocessor options used to initialize this
  /// preprocessor.
  PreprocessorOptions &getPreprocessorOpts() const { return *PPOpts; }

  DiagnosticsEngine &getDiagnostics() const { return *Diags; }
  void setDiagnostics(DiagnosticsEngine &D) { Diags = &D; }

  const LangOptions &getLangOpts() const { return LangOpts; }
  const TargetInfo &getTargetInfo() const { return *Target; }
  const TargetInfo *getAuxTargetInfo() const { return AuxTarget; }
  FileManager &getFileManager() const { return FileMgr; }
  SourceManager &getSourceManager() const { return SourceMgr; }
  HeaderSearch &getHeaderSearchInfo() const { return HeaderInfo; }

  IdentifierTable &getIdentifierTable() { return Identifiers; }
  const IdentifierTable &getIdentifierTable() const { return Identifiers; }
  SelectorTable &getSelectorTable() { return Selectors; }
  Builtin::Context &getBuiltinInfo() { return *BuiltinInfo; }
  llvm::BumpPtrAllocator &getPreprocessorAllocator() { return BP; }

  void setExternalSource(ExternalPreprocessorSource *Source) {
    ExternalSource = Source;
  }

  ExternalPreprocessorSource *getExternalSource() const {
    return ExternalSource;
  }

  /// Retrieve the module loader associated with this preprocessor.
  ModuleLoader &getModuleLoader() const { return TheModuleLoader; }

  bool hadModuleLoaderFatalFailure() const {
    return TheModuleLoader.HadFatalFailure;
  }

  /// Retrieve the number of Directives that have been processed by the
  /// Preprocessor.
  unsigned getNumDirectives() const {
    return NumDirectives;
  }

  /// True if we are currently preprocessing a #if or #elif directive
  bool isParsingIfOrElifDirective() const {
    return ParsingIfOrElifDirective;
  }

  /// Control whether the preprocessor retains comments in output.
  void SetCommentRetentionState(bool KeepComments, bool KeepMacroComments) {
    this->KeepComments = KeepComments | KeepMacroComments;
    this->KeepMacroComments = KeepMacroComments;
  }

  bool getCommentRetentionState() const { return KeepComments; }

  void setPragmasEnabled(bool Enabled) { PragmasEnabled = Enabled; }
  bool getPragmasEnabled() const { return PragmasEnabled; }

  void SetSuppressIncludeNotFoundError(bool Suppress) {
    SuppressIncludeNotFoundError = Suppress;
  }

  bool GetSuppressIncludeNotFoundError() {
    return SuppressIncludeNotFoundError;
  }

  /// Sets whether the preprocessor is responsible for producing output or if
  /// it is producing tokens to be consumed by Parse and Sema.
  void setPreprocessedOutput(bool IsPreprocessedOutput) {
    PreprocessedOutput = IsPreprocessedOutput;
  }

  /// Returns true if the preprocessor is responsible for generating output,
  /// false if it is producing tokens to be consumed by Parse and Sema.
  bool isPreprocessedOutput() const { return PreprocessedOutput; }

  /// Return true if we are lexing directly from the specified lexer.
  bool isCurrentLexer(const PreprocessorLexer *L) const {
    return CurPPLexer == L;
  }

  /// Return the current lexer being lexed from.
  ///
  /// Note that this ignores any potentially active macro expansions and _Pragma
  /// expansions going on at the time.
  PreprocessorLexer *getCurrentLexer() const { return CurPPLexer; }

  /// Return the current file lexer being lexed from.
  ///
  /// Note that this ignores any potentially active macro expansions and _Pragma
  /// expansions going on at the time.
  PreprocessorLexer *getCurrentFileLexer() const;

  /// Return the submodule owning the file being lexed. This may not be
  /// the current module if we have changed modules since entering the file.
  Module *getCurrentLexerSubmodule() const { return CurLexerSubmodule; }

  /// Returns the FileID for the preprocessor predefines.
  FileID getPredefinesFileID() const { return PredefinesFileID; }

  /// \{
  /// Accessors for preprocessor callbacks.
  ///
  /// Note that this class takes ownership of any PPCallbacks object given to
  /// it.
  PPCallbacks *getPPCallbacks() const { return Callbacks.get(); }
  void addPPCallbacks(std::unique_ptr<PPCallbacks> C) {
    if (Callbacks)
      C = std::make_unique<PPChainedCallbacks>(std::move(C),
                                                std::move(Callbacks));
    Callbacks = std::move(C);
  }
  /// \}

  /// Get the number of tokens processed so far.
  unsigned getTokenCount() const { return TokenCount; }

  /// Get the max number of tokens before issuing a -Wmax-tokens warning.
  unsigned getMaxTokens() const { return MaxTokens; }

  void overrideMaxTokens(unsigned Value, SourceLocation Loc) {
    MaxTokens = Value;
    MaxTokensOverrideLoc = Loc;
  };

  SourceLocation getMaxTokensOverrideLoc() const { return MaxTokensOverrideLoc; }

  /// Register a function that would be called on each token in the final
  /// expanded token stream.
  /// This also reports annotation tokens produced by the parser.
  void setTokenWatcher(llvm::unique_function<void(const clang::Token &)> F) {
    OnToken = std::move(F);
  }

  void setPreprocessToken(bool Preprocess) { PreprocessToken = Preprocess; }

  bool isMacroDefined(StringRef Id) {
    return isMacroDefined(&Identifiers.get(Id));
  }
  bool isMacroDefined(const IdentifierInfo *II) {
    return II->hasMacroDefinition() &&
           (!getLangOpts().Modules || (bool)getMacroDefinition(II));
  }

  /// Determine whether II is defined as a macro within the module M,
  /// if that is a module that we've already preprocessed. Does not check for
  /// macros imported into M.
  bool isMacroDefinedInLocalModule(const IdentifierInfo *II, Module *M) {
    if (!II->hasMacroDefinition())
      return false;
    auto I = Submodules.find(M);
    if (I == Submodules.end())
      return false;
    auto J = I->second.Macros.find(II);
    if (J == I->second.Macros.end())
      return false;
    auto *MD = J->second.getLatest();
    return MD && MD->isDefined();
  }

  MacroDefinition getMacroDefinition(const IdentifierInfo *II) {
    if (!II->hasMacroDefinition())
      return {};

    MacroState &S = CurSubmoduleState->Macros[II];
    auto *MD = S.getLatest();
    while (isa_and_nonnull<VisibilityMacroDirective>(MD))
      MD = MD->getPrevious();
    return MacroDefinition(dyn_cast_or_null<DefMacroDirective>(MD),
                           S.getActiveModuleMacros(*this, II),
                           S.isAmbiguous(*this, II));
  }

  MacroDefinition getMacroDefinitionAtLoc(const IdentifierInfo *II,
                                          SourceLocation Loc) {
    if (!II->hadMacroDefinition())
      return {};

    MacroState &S = CurSubmoduleState->Macros[II];
    MacroDirective::DefInfo DI;
    if (auto *MD = S.getLatest())
      DI = MD->findDirectiveAtLoc(Loc, getSourceManager());
    // FIXME: Compute the set of active module macros at the specified location.
    return MacroDefinition(DI.getDirective(),
                           S.getActiveModuleMacros(*this, II),
                           S.isAmbiguous(*this, II));
  }

  /// Given an identifier, return its latest non-imported MacroDirective
  /// if it is \#define'd and not \#undef'd, or null if it isn't \#define'd.
  MacroDirective *getLocalMacroDirective(const IdentifierInfo *II) const {
    if (!II->hasMacroDefinition())
      return nullptr;

    auto *MD = getLocalMacroDirectiveHistory(II);
    if (!MD || MD->getDefinition().isUndefined())
      return nullptr;

    return MD;
  }

  const MacroInfo *getMacroInfo(const IdentifierInfo *II) const {
    return const_cast<Preprocessor*>(this)->getMacroInfo(II);
  }

  MacroInfo *getMacroInfo(const IdentifierInfo *II) {
    if (!II->hasMacroDefinition())
      return nullptr;
    if (auto MD = getMacroDefinition(II))
      return MD.getMacroInfo();
    return nullptr;
  }

  /// Given an identifier, return the latest non-imported macro
  /// directive for that identifier.
  ///
  /// One can iterate over all previous macro directives from the most recent
  /// one.
  MacroDirective *getLocalMacroDirectiveHistory(const IdentifierInfo *II) const;

  /// Add a directive to the macro directive history for this identifier.
  void appendMacroDirective(IdentifierInfo *II, MacroDirective *MD);
  DefMacroDirective *appendDefMacroDirective(IdentifierInfo *II, MacroInfo *MI,
                                             SourceLocation Loc) {
    DefMacroDirective *MD = AllocateDefMacroDirective(MI, Loc);
    appendMacroDirective(II, MD);
    return MD;
  }
  DefMacroDirective *appendDefMacroDirective(IdentifierInfo *II,
                                             MacroInfo *MI) {
    return appendDefMacroDirective(II, MI, MI->getDefinitionLoc());
  }

  /// Set a MacroDirective that was loaded from a PCH file.
  void setLoadedMacroDirective(IdentifierInfo *II, MacroDirective *ED,
                               MacroDirective *MD);

  /// Register an exported macro for a module and identifier.
  ModuleMacro *addModuleMacro(Module *Mod, IdentifierInfo *II,
                              MacroInfo *Macro,
                              ArrayRef<ModuleMacro *> Overrides, bool &IsNew);
  ModuleMacro *getModuleMacro(Module *Mod, const IdentifierInfo *II);

  /// Get the list of leaf (non-overridden) module macros for a name.
  ArrayRef<ModuleMacro*> getLeafModuleMacros(const IdentifierInfo *II) const {
    if (II->isOutOfDate())
      updateOutOfDateIdentifier(*II);
    auto I = LeafModuleMacros.find(II);
    if (I != LeafModuleMacros.end())
      return I->second;
    return std::nullopt;
  }

  /// Get the list of submodules that we're currently building.
  ArrayRef<BuildingSubmoduleInfo> getBuildingSubmodules() const {
    return BuildingSubmoduleStack;
  }

  /// \{
  /// Iterators for the macro history table. Currently defined macros have
  /// IdentifierInfo::hasMacroDefinition() set and an empty
  /// MacroInfo::getUndefLoc() at the head of the list.
  using macro_iterator = MacroMap::const_iterator;

  macro_iterator macro_begin(bool IncludeExternalMacros = true) const;
  macro_iterator macro_end(bool IncludeExternalMacros = true) const;

  llvm::iterator_range<macro_iterator>
  macros(bool IncludeExternalMacros = true) const {
    macro_iterator begin = macro_begin(IncludeExternalMacros);
    macro_iterator end = macro_end(IncludeExternalMacros);
    return llvm::make_range(begin, end);
  }

  /// \}

  /// Mark the given clang module as affecting the current clang module or translation unit.
  void markClangModuleAsAffecting(Module *M) {
    assert(M->isModuleMapModule());
    if (!BuildingSubmoduleStack.empty()) {
      if (M != BuildingSubmoduleStack.back().M)
        BuildingSubmoduleStack.back().M->AffectingClangModules.insert(M);
    } else {
      AffectingClangModules.insert(M);
    }
  }

  /// Get the set of top-level clang modules that affected preprocessing, but were not
  /// imported.
  const llvm::SmallSetVector<Module *, 2> &getAffectingClangModules() const {
    return AffectingClangModules;
  }

  /// Mark the file as included.
  /// Returns true if this is the first time the file was included.
  bool markIncluded(FileEntryRef File) {
    HeaderInfo.getFileInfo(File);
    return IncludedFiles.insert(File).second;
  }

  /// Return true if this header has already been included.
  bool alreadyIncluded(FileEntryRef File) const {
    HeaderInfo.getFileInfo(File);
    return IncludedFiles.count(File);
  }

  /// Get the set of included files.
  IncludedFilesSet &getIncludedFiles() { return IncludedFiles; }
  const IncludedFilesSet &getIncludedFiles() const { return IncludedFiles; }

  /// Return the name of the macro defined before \p Loc that has
  /// spelling \p Tokens.  If there are multiple macros with same spelling,
  /// return the last one defined.
  StringRef getLastMacroWithSpelling(SourceLocation Loc,
                                     ArrayRef<TokenValue> Tokens) const;

  /// Get the predefines for this processor.
  /// Used by some third-party tools to inspect and add predefines (see
  /// https://github.com/llvm/llvm-project/issues/57483).
  const std::string &getPredefines() const { return Predefines; }

  /// Set the predefines for this Preprocessor.
  ///
  /// These predefines are automatically injected when parsing the main file.
  void setPredefines(std::string P) { Predefines = std::move(P); }

  /// Return information about the specified preprocessor
  /// identifier token.
  IdentifierInfo *getIdentifierInfo(StringRef Name) const {
    return &Identifiers.get(Name);
  }

  /// Add the specified pragma handler to this preprocessor.
  ///
  /// If \p Namespace is non-null, then it is a token required to exist on the
  /// pragma line before the pragma string starts, e.g. "STDC" or "GCC".
  void AddPragmaHandler(StringRef Namespace, PragmaHandler *Handler);
  void AddPragmaHandler(PragmaHandler *Handler) {
    AddPragmaHandler(StringRef(), Handler);
  }

  /// Remove the specific pragma handler from this preprocessor.
  ///
  /// If \p Namespace is non-null, then it should be the namespace that
  /// \p Handler was added to. It is an error to remove a handler that
  /// has not been registered.
  void RemovePragmaHandler(StringRef Namespace, PragmaHandler *Handler);
  void RemovePragmaHandler(PragmaHandler *Handler) {
    RemovePragmaHandler(StringRef(), Handler);
  }

  /// Install empty handlers for all pragmas (making them ignored).
  void IgnorePragmas();

  /// Set empty line handler.
  void setEmptylineHandler(EmptylineHandler *Handler) { Emptyline = Handler; }

  EmptylineHandler *getEmptylineHandler() const { return Emptyline; }

  /// Add the specified comment handler to the preprocessor.
  void addCommentHandler(CommentHandler *Handler);

  /// Remove the specified comment handler.
  ///
  /// It is an error to remove a handler that has not been registered.
  void removeCommentHandler(CommentHandler *Handler);

  /// Set the code completion handler to the given object.
  void setCodeCompletionHandler(CodeCompletionHandler &Handler) {
    CodeComplete = &Handler;
  }

  /// Retrieve the current code-completion handler.
  CodeCompletionHandler *getCodeCompletionHandler() const {
    return CodeComplete;
  }

  /// Clear out the code completion handler.
  void clearCodeCompletionHandler() {
    CodeComplete = nullptr;
  }

  /// Hook used by the lexer to invoke the "included file" code
  /// completion point.
  void CodeCompleteIncludedFile(llvm::StringRef Dir, bool IsAngled);

  /// Hook used by the lexer to invoke the "natural language" code
  /// completion point.
  void CodeCompleteNaturalLanguage();

  /// Set the code completion token for filtering purposes.
  void setCodeCompletionIdentifierInfo(IdentifierInfo *Filter) {
    CodeCompletionII = Filter;
  }

  /// Set the code completion token range for detecting replacement range later
  /// on.
  void setCodeCompletionTokenRange(const SourceLocation Start,
                                   const SourceLocation End) {
    CodeCompletionTokenRange = {Start, End};
  }
  SourceRange getCodeCompletionTokenRange() const {
    return CodeCompletionTokenRange;
  }

  /// Get the code completion token for filtering purposes.
  StringRef getCodeCompletionFilter() {
    if (CodeCompletionII)
      return CodeCompletionII->getName();
    return {};
  }

  /// Retrieve the preprocessing record, or NULL if there is no
  /// preprocessing record.
  PreprocessingRecord *getPreprocessingRecord() const { return Record; }

  /// Create a new preprocessing record, which will keep track of
  /// all macro expansions, macro definitions, etc.
  void createPreprocessingRecord();

  /// Returns true if the FileEntry is the PCH through header.
  bool isPCHThroughHeader(const FileEntry *FE);

  /// True if creating a PCH with a through header.
  bool creatingPCHWithThroughHeader();

  /// True if using a PCH with a through header.
  bool usingPCHWithThroughHeader();

  /// True if creating a PCH with a #pragma hdrstop.
  bool creatingPCHWithPragmaHdrStop();

  /// True if using a PCH with a #pragma hdrstop.
  bool usingPCHWithPragmaHdrStop();

  /// Skip tokens until after the #include of the through header or
  /// until after a #pragma hdrstop.
  void SkipTokensWhileUsingPCH();

  /// Process directives while skipping until the through header or
  /// #pragma hdrstop is found.
  void HandleSkippedDirectiveWhileUsingPCH(Token &Result,
                                           SourceLocation HashLoc);

  /// Enter the specified FileID as the main source file,
  /// which implicitly adds the builtin defines etc.
  void EnterMainSourceFile();

  /// Inform the preprocessor callbacks that processing is complete.
  void EndSourceFile();

  /// Add a source file to the top of the include stack and
  /// start lexing tokens from it instead of the current buffer.
  ///
  /// Emits a diagnostic, doesn't enter the file, and returns true on error.
  bool EnterSourceFile(FileID FID, ConstSearchDirIterator Dir,
                       SourceLocation Loc, bool IsFirstIncludeOfFile = true);

  /// Add a Macro to the top of the include stack and start lexing
  /// tokens from it instead of the current buffer.
  ///
  /// \param Args specifies the tokens input to a function-like macro.
  /// \param ILEnd specifies the location of the ')' for a function-like macro
  /// or the identifier for an object-like macro.
  void EnterMacro(Token &Tok, SourceLocation ILEnd, MacroInfo *Macro,
                  MacroArgs *Args);

private:
  /// Add a "macro" context to the top of the include stack,
  /// which will cause the lexer to start returning the specified tokens.
  ///
  /// If \p DisableMacroExpansion is true, tokens lexed from the token stream
  /// will not be subject to further macro expansion. Otherwise, these tokens
  /// will be re-macro-expanded when/if expansion is enabled.
  ///
  /// If \p OwnsTokens is false, this method assumes that the specified stream
  /// of tokens has a permanent owner somewhere, so they do not need to be
  /// copied. If it is true, it assumes the array of tokens is allocated with
  /// \c new[] and the Preprocessor will delete[] it.
  ///
  /// If \p IsReinject the resulting tokens will have Token::IsReinjected flag
  /// set, see the flag documentation for details.
  void EnterTokenStream(const Token *Toks, unsigned NumToks,
                        bool DisableMacroExpansion, bool OwnsTokens,
                        bool IsReinject);

public:
  void EnterTokenStream(std::unique_ptr<Token[]> Toks, unsigned NumToks,
                        bool DisableMacroExpansion, bool IsReinject) {
    EnterTokenStream(Toks.release(), NumToks, DisableMacroExpansion, true,
                     IsReinject);
  }

  void EnterTokenStream(ArrayRef<Token> Toks, bool DisableMacroExpansion,
                        bool IsReinject) {
    EnterTokenStream(Toks.data(), Toks.size(), DisableMacroExpansion, false,
                     IsReinject);
  }

  /// Pop the current lexer/macro exp off the top of the lexer stack.
  ///
  /// This should only be used in situations where the current state of the
  /// top-of-stack lexer is known.
  void RemoveTopOfLexerStack();

  /// From the point that this method is called, and until
  /// CommitBacktrackedTokens() or Backtrack() is called, the Preprocessor
  /// keeps track of the lexed tokens so that a subsequent Backtrack() call will
  /// make the Preprocessor re-lex the same tokens.
  ///
  /// Nested backtracks are allowed, meaning that EnableBacktrackAtThisPos can
  /// be called multiple times and CommitBacktrackedTokens/Backtrack calls will
  /// be combined with the EnableBacktrackAtThisPos calls in reverse order.
  ///
  /// NOTE: *DO NOT* forget to call either CommitBacktrackedTokens or Backtrack
  /// at some point after EnableBacktrackAtThisPos. If you don't, caching of
  /// tokens will continue indefinitely.
  ///
  void EnableBacktrackAtThisPos();

  /// Disable the last EnableBacktrackAtThisPos call.
  void CommitBacktrackedTokens();

  /// Make Preprocessor re-lex the tokens that were lexed since
  /// EnableBacktrackAtThisPos() was previously called.
  void Backtrack();

  /// True if EnableBacktrackAtThisPos() was called and
  /// caching of tokens is on.
  bool isBacktrackEnabled() const { return !BacktrackPositions.empty(); }

  /// Lex the next token for this preprocessor.
  void Lex(Token &Result);

  /// Lex all tokens for this preprocessor until (and excluding) end of file.
  void LexTokensUntilEOF(std::vector<Token> *Tokens = nullptr);

  /// Lex a token, forming a header-name token if possible.
  bool LexHeaderName(Token &Result, bool AllowMacroExpansion = true);

  /// Lex the parameters for an #embed directive, returns nullopt on error.
  std::optional<LexEmbedParametersResult> LexEmbedParameters(Token &Current,
                                                             bool ForHasEmbed);

  bool LexAfterModuleImport(Token &Result);
  void CollectPpImportSuffix(SmallVectorImpl<Token> &Toks);

  void makeModuleVisible(Module *M, SourceLocation Loc);

  SourceLocation getModuleImportLoc(Module *M) const {
    return CurSubmoduleState->VisibleModules.getImportLoc(M);
  }

  /// Lex a string literal, which may be the concatenation of multiple
  /// string literals and may even come from macro expansion.
  /// \returns true on success, false if a error diagnostic has been generated.
  bool LexStringLiteral(Token &Result, std::string &String,
                        const char *DiagnosticTag, bool AllowMacroExpansion) {
    if (AllowMacroExpansion)
      Lex(Result);
    else
      LexUnexpandedToken(Result);
    return FinishLexStringLiteral(Result, String, DiagnosticTag,
                                  AllowMacroExpansion);
  }

  /// Complete the lexing of a string literal where the first token has
  /// already been lexed (see LexStringLiteral).
  bool FinishLexStringLiteral(Token &Result, std::string &String,
                              const char *DiagnosticTag,
                              bool AllowMacroExpansion);

  /// Lex a token.  If it's a comment, keep lexing until we get
  /// something not a comment.
  ///
  /// This is useful in -E -C mode where comments would foul up preprocessor
  /// directive handling.
  void LexNonComment(Token &Result) {
    do
      Lex(Result);
    while (Result.getKind() == tok::comment);
  }

  /// Just like Lex, but disables macro expansion of identifier tokens.
  void LexUnexpandedToken(Token &Result) {
    // Disable macro expansion.
    bool OldVal = DisableMacroExpansion;
    DisableMacroExpansion = true;
    // Lex the token.
    Lex(Result);

    // Reenable it.
    DisableMacroExpansion = OldVal;
  }

  /// Like LexNonComment, but this disables macro expansion of
  /// identifier tokens.
  void LexUnexpandedNonComment(Token &Result) {
    do
      LexUnexpandedToken(Result);
    while (Result.getKind() == tok::comment);
  }

  /// Parses a simple integer literal to get its numeric value.  Floating
  /// point literals and user defined literals are rejected.  Used primarily to
  /// handle pragmas that accept integer arguments.
  bool parseSimpleIntegerLiteral(Token &Tok, uint64_t &Value);

  /// Disables macro expansion everywhere except for preprocessor directives.
  void SetMacroExpansionOnlyInDirectives() {
    DisableMacroExpansion = true;
    MacroExpansionInDirectivesOverride = true;
  }

  /// Peeks ahead N tokens and returns that token without consuming any
  /// tokens.
  ///
  /// LookAhead(0) returns the next token that would be returned by Lex(),
  /// LookAhead(1) returns the token after it, etc.  This returns normal
  /// tokens after phase 5.  As such, it is equivalent to using
  /// 'Lex', not 'LexUnexpandedToken'.
  const Token &LookAhead(unsigned N) {
    assert(LexLevel == 0 && "cannot use lookahead while lexing");
    if (CachedLexPos + N < CachedTokens.size())
      return CachedTokens[CachedLexPos+N];
    else
      return PeekAhead(N+1);
  }

  /// When backtracking is enabled and tokens are cached,
  /// this allows to revert a specific number of tokens.
  ///
  /// Note that the number of tokens being reverted should be up to the last
  /// backtrack position, not more.
  void RevertCachedTokens(unsigned N) {
    assert(isBacktrackEnabled() &&
           "Should only be called when tokens are cached for backtracking");
    assert(signed(CachedLexPos) - signed(N) >= signed(BacktrackPositions.back())
         && "Should revert tokens up to the last backtrack position, not more");
    assert(signed(CachedLexPos) - signed(N) >= 0 &&
           "Corrupted backtrack positions ?");
    CachedLexPos -= N;
  }

  /// Enters a token in the token stream to be lexed next.
  ///
  /// If BackTrack() is called afterwards, the token will remain at the
  /// insertion point.
  /// If \p IsReinject is true, resulting token will have Token::IsReinjected
  /// flag set. See the flag documentation for details.
  void EnterToken(const Token &Tok, bool IsReinject) {
    if (LexLevel) {
      // It's not correct in general to enter caching lex mode while in the
      // middle of a nested lexing action.
      auto TokCopy = std::make_unique<Token[]>(1);
      TokCopy[0] = Tok;
      EnterTokenStream(std::move(TokCopy), 1, true, IsReinject);
    } else {
      EnterCachingLexMode();
      assert(IsReinject && "new tokens in the middle of cached stream");
      CachedTokens.insert(CachedTokens.begin()+CachedLexPos, Tok);
    }
  }

  /// We notify the Preprocessor that if it is caching tokens (because
  /// backtrack is enabled) it should replace the most recent cached tokens
  /// with the given annotation token. This function has no effect if
  /// backtracking is not enabled.
  ///
  /// Note that the use of this function is just for optimization, so that the
  /// cached tokens doesn't get re-parsed and re-resolved after a backtrack is
  /// invoked.
  void AnnotateCachedTokens(const Token &Tok) {
    assert(Tok.isAnnotation() && "Expected annotation token");
    if (CachedLexPos != 0 && isBacktrackEnabled())
      AnnotatePreviousCachedTokens(Tok);
  }

  /// Get the location of the last cached token, suitable for setting the end
  /// location of an annotation token.
  SourceLocation getLastCachedTokenLocation() const {
    assert(CachedLexPos != 0);
    return CachedTokens[CachedLexPos-1].getLastLoc();
  }

  /// Whether \p Tok is the most recent token (`CachedLexPos - 1`) in
  /// CachedTokens.
  bool IsPreviousCachedToken(const Token &Tok) const;

  /// Replace token in `CachedLexPos - 1` in CachedTokens by the tokens
  /// in \p NewToks.
  ///
  /// Useful when a token needs to be split in smaller ones and CachedTokens
  /// most recent token must to be updated to reflect that.
  void ReplacePreviousCachedToken(ArrayRef<Token> NewToks);

  /// Replace the last token with an annotation token.
  ///
  /// Like AnnotateCachedTokens(), this routine replaces an
  /// already-parsed (and resolved) token with an annotation
  /// token. However, this routine only replaces the last token with
  /// the annotation token; it does not affect any other cached
  /// tokens. This function has no effect if backtracking is not
  /// enabled.
  void ReplaceLastTokenWithAnnotation(const Token &Tok) {
    assert(Tok.isAnnotation() && "Expected annotation token");
    if (CachedLexPos != 0 && isBacktrackEnabled())
      CachedTokens[CachedLexPos-1] = Tok;
  }

  /// Enter an annotation token into the token stream.
  void EnterAnnotationToken(SourceRange Range, tok::TokenKind Kind,
                            void *AnnotationVal);

  /// Determine whether it's possible for a future call to Lex to produce an
  /// annotation token created by a previous call to EnterAnnotationToken.
  bool mightHavePendingAnnotationTokens() {
    return CurLexerCallback != CLK_Lexer;
  }

  /// Update the current token to represent the provided
  /// identifier, in order to cache an action performed by typo correction.
  void TypoCorrectToken(const Token &Tok) {
    assert(Tok.getIdentifierInfo() && "Expected identifier token");
    if (CachedLexPos != 0 && isBacktrackEnabled())
      CachedTokens[CachedLexPos-1] = Tok;
  }

  /// Recompute the current lexer kind based on the CurLexer/
  /// CurTokenLexer pointers.
  void recomputeCurLexerKind();

  /// Returns true if incremental processing is enabled
  bool isIncrementalProcessingEnabled() const { return IncrementalProcessing; }

  /// Enables the incremental processing
  void enableIncrementalProcessing(bool value = true) {
    IncrementalProcessing = value;
  }

  /// Specify the point at which code-completion will be performed.
  ///
  /// \param File the file in which code completion should occur. If
  /// this file is included multiple times, code-completion will
  /// perform completion the first time it is included. If NULL, this
  /// function clears out the code-completion point.
  ///
  /// \param Line the line at which code completion should occur
  /// (1-based).
  ///
  /// \param Column the column at which code completion should occur
  /// (1-based).
  ///
  /// \returns true if an error occurred, false otherwise.
  bool SetCodeCompletionPoint(FileEntryRef File, unsigned Line,
                              unsigned Column);

  /// Determine if we are performing code completion.
  bool isCodeCompletionEnabled() const { return CodeCompletionFile != nullptr; }

  /// Returns the location of the code-completion point.
  ///
  /// Returns an invalid location if code-completion is not enabled or the file
  /// containing the code-completion point has not been lexed yet.
  SourceLocation getCodeCompletionLoc() const { return CodeCompletionLoc; }

  /// Returns the start location of the file of code-completion point.
  ///
  /// Returns an invalid location if code-completion is not enabled or the file
  /// containing the code-completion point has not been lexed yet.
  SourceLocation getCodeCompletionFileLoc() const {
    return CodeCompletionFileLoc;
  }

  /// Returns true if code-completion is enabled and we have hit the
  /// code-completion point.
  bool isCodeCompletionReached() const { return CodeCompletionReached; }

  /// Note that we hit the code-completion point.
  void setCodeCompletionReached() {
    assert(isCodeCompletionEnabled() && "Code-completion not enabled!");
    CodeCompletionReached = true;
    // Silence any diagnostics that occur after we hit the code-completion.
    getDiagnostics().setSuppressAllDiagnostics(true);
  }

  /// The location of the currently-active \#pragma clang
  /// arc_cf_code_audited begin.
  ///
  /// Returns an invalid location if there is no such pragma active.
  std::pair<IdentifierInfo *, SourceLocation>
  getPragmaARCCFCodeAuditedInfo() const {
    return PragmaARCCFCodeAuditedInfo;
  }

  /// Set the location of the currently-active \#pragma clang
  /// arc_cf_code_audited begin.  An invalid location ends the pragma.
  void setPragmaARCCFCodeAuditedInfo(IdentifierInfo *Ident,
                                     SourceLocation Loc) {
    PragmaARCCFCodeAuditedInfo = {Ident, Loc};
  }

  /// The location of the currently-active \#pragma clang
  /// assume_nonnull begin.
  ///
  /// Returns an invalid location if there is no such pragma active.
  SourceLocation getPragmaAssumeNonNullLoc() const {
    return PragmaAssumeNonNullLoc;
  }

  /// Set the location of the currently-active \#pragma clang
  /// assume_nonnull begin.  An invalid location ends the pragma.
  void setPragmaAssumeNonNullLoc(SourceLocation Loc) {
    PragmaAssumeNonNullLoc = Loc;
  }

  /// Get the location of the recorded unterminated \#pragma clang
  /// assume_nonnull begin in the preamble, if one exists.
  ///
  /// Returns an invalid location if the premable did not end with
  /// such a pragma active or if there is no recorded preamble.
  SourceLocation getPreambleRecordedPragmaAssumeNonNullLoc() const {
    return PreambleRecordedPragmaAssumeNonNullLoc;
  }

  /// Record the location of the unterminated \#pragma clang
  /// assume_nonnull begin in the preamble.
  void setPreambleRecordedPragmaAssumeNonNullLoc(SourceLocation Loc) {
    PreambleRecordedPragmaAssumeNonNullLoc = Loc;
  }

  /// Set the directory in which the main file should be considered
  /// to have been found, if it is not a real file.
  void setMainFileDir(DirectoryEntryRef Dir) { MainFileDir = Dir; }

  /// Instruct the preprocessor to skip part of the main source file.
  ///
  /// \param Bytes The number of bytes in the preamble to skip.
  ///
  /// \param StartOfLine Whether skipping these bytes puts the lexer at the
  /// start of a line.
  void setSkipMainFilePreamble(unsigned Bytes, bool StartOfLine) {
    SkipMainFilePreamble.first = Bytes;
    SkipMainFilePreamble.second = StartOfLine;
  }

  /// Forwarding function for diagnostics.  This emits a diagnostic at
  /// the specified Token's location, translating the token's start
  /// position in the current buffer into a SourcePosition object for rendering.
  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID) const {
    return Diags->Report(Loc, DiagID);
  }

  DiagnosticBuilder Diag(const Token &Tok, unsigned DiagID) const {
    return Diags->Report(Tok.getLocation(), DiagID);
  }

  /// Return the 'spelling' of the token at the given
  /// location; does not go up to the spelling location or down to the
  /// expansion location.
  ///
  /// \param buffer A buffer which will be used only if the token requires
  ///   "cleaning", e.g. if it contains trigraphs or escaped newlines
  /// \param invalid If non-null, will be set \c true if an error occurs.
  StringRef getSpelling(SourceLocation loc,
                        SmallVectorImpl<char> &buffer,
                        bool *invalid = nullptr) const {
    return Lexer::getSpelling(loc, buffer, SourceMgr, LangOpts, invalid);
  }

  /// Return the 'spelling' of the Tok token.
  ///
  /// The spelling of a token is the characters used to represent the token in
  /// the source file after trigraph expansion and escaped-newline folding.  In
  /// particular, this wants to get the true, uncanonicalized, spelling of
  /// things like digraphs, UCNs, etc.
  ///
  /// \param Invalid If non-null, will be set \c true if an error occurs.
  std::string getSpelling(const Token &Tok, bool *Invalid = nullptr) const {
    return Lexer::getSpelling(Tok, SourceMgr, LangOpts, Invalid);
  }

  /// Get the spelling of a token into a preallocated buffer, instead
  /// of as an std::string.
  ///
  /// The caller is required to allocate enough space for the token, which is
  /// guaranteed to be at least Tok.getLength() bytes long. The length of the
  /// actual result is returned.
  ///
  /// Note that this method may do two possible things: it may either fill in
  /// the buffer specified with characters, or it may *change the input pointer*
  /// to point to a constant buffer with the data already in it (avoiding a
  /// copy).  The caller is not allowed to modify the returned buffer pointer
  /// if an internal buffer is returned.
  unsigned getSpelling(const Token &Tok, const char *&Buffer,
                       bool *Invalid = nullptr) const {
    return Lexer::getSpelling(Tok, Buffer, SourceMgr, LangOpts, Invalid);
  }

  /// Get the spelling of a token into a SmallVector.
  ///
  /// Note that the returned StringRef may not point to the
  /// supplied buffer if a copy can be avoided.
  StringRef getSpelling(const Token &Tok,
                        SmallVectorImpl<char> &Buffer,
                        bool *Invalid = nullptr) const;

  /// Relex the token at the specified location.
  /// \returns true if there was a failure, false on success.
  bool getRawToken(SourceLocation Loc, Token &Result,
                   bool IgnoreWhiteSpace = false) {
    return Lexer::getRawToken(Loc, Result, SourceMgr, LangOpts, IgnoreWhiteSpace);
  }

  /// Given a Token \p Tok that is a numeric constant with length 1,
  /// return the character.
  char
  getSpellingOfSingleCharacterNumericConstant(const Token &Tok,
                                              bool *Invalid = nullptr) const {
    assert((Tok.is(tok::numeric_constant) || Tok.is(tok::binary_data)) &&
           Tok.getLength() == 1 && "Called on unsupported token");
    assert(!Tok.needsCleaning() && "Token can't need cleaning with length 1");

    // If the token is carrying a literal data pointer, just use it.
    if (const char *D = Tok.getLiteralData())
      return (Tok.getKind() == tok::binary_data) ? *D : *D - '0';

    assert(Tok.is(tok::numeric_constant) && "binary data with no data");
    // Otherwise, fall back on getCharacterData, which is slower, but always
    // works.
    return *SourceMgr.getCharacterData(Tok.getLocation(), Invalid) - '0';
  }

  /// Retrieve the name of the immediate macro expansion.
  ///
  /// This routine starts from a source location, and finds the name of the
  /// macro responsible for its immediate expansion. It looks through any
  /// intervening macro argument expansions to compute this. It returns a
  /// StringRef that refers to the SourceManager-owned buffer of the source
  /// where that macro name is spelled. Thus, the result shouldn't out-live
  /// the SourceManager.
  StringRef getImmediateMacroName(SourceLocation Loc) {
    return Lexer::getImmediateMacroName(Loc, SourceMgr, getLangOpts());
  }

  /// Plop the specified string into a scratch buffer and set the
  /// specified token's location and length to it.
  ///
  /// If specified, the source location provides a location of the expansion
  /// point of the token.
  void CreateString(StringRef Str, Token &Tok,
                    SourceLocation ExpansionLocStart = SourceLocation(),
                    SourceLocation ExpansionLocEnd = SourceLocation());

  /// Split the first Length characters out of the token starting at TokLoc
  /// and return a location pointing to the split token. Re-lexing from the
  /// split token will return the split token rather than the original.
  SourceLocation SplitToken(SourceLocation TokLoc, unsigned Length);

  /// Computes the source location just past the end of the
  /// token at this source location.
  ///
  /// This routine can be used to produce a source location that
  /// points just past the end of the token referenced by \p Loc, and
  /// is generally used when a diagnostic needs to point just after a
  /// token where it expected something different that it received. If
  /// the returned source location would not be meaningful (e.g., if
  /// it points into a macro), this routine returns an invalid
  /// source location.
  ///
  /// \param Offset an offset from the end of the token, where the source
  /// location should refer to. The default offset (0) produces a source
  /// location pointing just past the end of the token; an offset of 1 produces
  /// a source location pointing to the last character in the token, etc.
  SourceLocation getLocForEndOfToken(SourceLocation Loc, unsigned Offset = 0) {
    return Lexer::getLocForEndOfToken(Loc, Offset, SourceMgr, LangOpts);
  }

  /// Returns true if the given MacroID location points at the first
  /// token of the macro expansion.
  ///
  /// \param MacroBegin If non-null and function returns true, it is set to
  /// begin location of the macro.
  bool isAtStartOfMacroExpansion(SourceLocation loc,
                                 SourceLocation *MacroBegin = nullptr) const {
    return Lexer::isAtStartOfMacroExpansion(loc, SourceMgr, LangOpts,
                                            MacroBegin);
  }

  /// Returns true if the given MacroID location points at the last
  /// token of the macro expansion.
  ///
  /// \param MacroEnd If non-null and function returns true, it is set to
  /// end location of the macro.
  bool isAtEndOfMacroExpansion(SourceLocation loc,
                               SourceLocation *MacroEnd = nullptr) const {
    return Lexer::isAtEndOfMacroExpansion(loc, SourceMgr, LangOpts, MacroEnd);
  }

  /// Print the token to stderr, used for debugging.
  void DumpToken(const Token &Tok, bool DumpFlags = false) const;
  void DumpLocation(SourceLocation Loc) const;
  void DumpMacro(const MacroInfo &MI) const;
  void dumpMacroInfo(const IdentifierInfo *II);

  /// Given a location that specifies the start of a
  /// token, return a new location that specifies a character within the token.
  SourceLocation AdvanceToTokenCharacter(SourceLocation TokStart,
                                         unsigned Char) const {
    return Lexer::AdvanceToTokenCharacter(TokStart, Char, SourceMgr, LangOpts);
  }

  /// Increment the counters for the number of token paste operations
  /// performed.
  ///
  /// If fast was specified, this is a 'fast paste' case we handled.
  void IncrementPasteCounter(bool isFast) {
    if (isFast)
      ++NumFastTokenPaste;
    else
      ++NumTokenPaste;
  }

  void PrintStats();

  size_t getTotalMemory() const;

  /// When the macro expander pastes together a comment (/##/) in Microsoft
  /// mode, this method handles updating the current state, returning the
  /// token on the next source line.
  void HandleMicrosoftCommentPaste(Token &Tok);

  //===--------------------------------------------------------------------===//
  // Preprocessor callback methods.  These are invoked by a lexer as various
  // directives and events are found.

  /// Given a tok::raw_identifier token, look up the
  /// identifier information for the token and install it into the token,
  /// updating the token kind accordingly.
  IdentifierInfo *LookUpIdentifierInfo(Token &Identifier) const;

private:
  llvm::DenseMap<IdentifierInfo*,unsigned> PoisonReasons;

public:
  /// Specifies the reason for poisoning an identifier.
  ///
  /// If that identifier is accessed while poisoned, then this reason will be
  /// used instead of the default "poisoned" diagnostic.
  void SetPoisonReason(IdentifierInfo *II, unsigned DiagID);

  /// Display reason for poisoned identifier.
  void HandlePoisonedIdentifier(Token & Identifier);

  void MaybeHandlePoisonedIdentifier(Token & Identifier) {
    if(IdentifierInfo * II = Identifier.getIdentifierInfo()) {
      if(II->isPoisoned()) {
        HandlePoisonedIdentifier(Identifier);
      }
    }
  }

private:
  /// Identifiers used for SEH handling in Borland. These are only
  /// allowed in particular circumstances
  // __except block
  IdentifierInfo *Ident__exception_code,
                 *Ident___exception_code,
                 *Ident_GetExceptionCode;
  // __except filter expression
  IdentifierInfo *Ident__exception_info,
                 *Ident___exception_info,
                 *Ident_GetExceptionInfo;
  // __finally
  IdentifierInfo *Ident__abnormal_termination,
                 *Ident___abnormal_termination,
                 *Ident_AbnormalTermination;

  const char *getCurLexerEndPos();
  void diagnoseMissingHeaderInUmbrellaDir(const Module &Mod);

public:
  void PoisonSEHIdentifiers(bool Poison = true); // Borland

  /// Callback invoked when the lexer reads an identifier and has
  /// filled in the tokens IdentifierInfo member.
  ///
  /// This callback potentially macro expands it or turns it into a named
  /// token (like 'for').
  ///
  /// \returns true if we actually computed a token, false if we need to
  /// lex again.
  bool HandleIdentifier(Token &Identifier);

  /// Callback invoked when the lexer hits the end of the current file.
  ///
  /// This either returns the EOF token and returns true, or
  /// pops a level off the include stack and returns false, at which point the
  /// client should call lex again.
  bool HandleEndOfFile(Token &Result, bool isEndOfMacro = false);

  /// Callback invoked when the current TokenLexer hits the end of its
  /// token stream.
  bool HandleEndOfTokenLexer(Token &Result);

  /// Callback invoked when the lexer sees a # token at the start of a
  /// line.
  ///
  /// This consumes the directive, modifies the lexer/preprocessor state, and
  /// advances the lexer(s) so that the next token read is the correct one.
  void HandleDirective(Token &Result);

  /// Ensure that the next token is a tok::eod token.
  ///
  /// If not, emit a diagnostic and consume up until the eod.
  /// If \p EnableMacros is true, then we consider macros that expand to zero
  /// tokens as being ok.
  ///
  /// \return The location of the end of the directive (the terminating
  /// newline).
  SourceLocation CheckEndOfDirective(const char *DirType,
                                     bool EnableMacros = false);

  /// Read and discard all tokens remaining on the current line until
  /// the tok::eod token is found. Returns the range of the skipped tokens.
  SourceRange DiscardUntilEndOfDirective() {
    Token Tmp;
    return DiscardUntilEndOfDirective(Tmp);
  }

  /// Same as above except retains the token that was found.
  SourceRange DiscardUntilEndOfDirective(Token &Tok);

  /// Returns true if the preprocessor has seen a use of
  /// __DATE__ or __TIME__ in the file so far.
  bool SawDateOrTime() const {
    return DATELoc != SourceLocation() || TIMELoc != SourceLocation();
  }
  unsigned getCounterValue() const { return CounterValue; }
  void setCounterValue(unsigned V) { CounterValue = V; }

  LangOptions::FPEvalMethodKind getCurrentFPEvalMethod() const {
    assert(CurrentFPEvalMethod != LangOptions::FEM_UnsetOnCommandLine &&
           "FPEvalMethod should be set either from command line or from the "
           "target info");
    return CurrentFPEvalMethod;
  }

  LangOptions::FPEvalMethodKind getTUFPEvalMethod() const {
    return TUFPEvalMethod;
  }

  SourceLocation getLastFPEvalPragmaLocation() const {
    return LastFPEvalPragmaLocation;
  }

  void setCurrentFPEvalMethod(SourceLocation PragmaLoc,
                              LangOptions::FPEvalMethodKind Val) {
    assert(Val != LangOptions::FEM_UnsetOnCommandLine &&
           "FPEvalMethod should never be set to FEM_UnsetOnCommandLine");
    // This is the location of the '#pragma float_control" where the
    // execution state is modifed.
    LastFPEvalPragmaLocation = PragmaLoc;
    CurrentFPEvalMethod = Val;
    TUFPEvalMethod = Val;
  }

  void setTUFPEvalMethod(LangOptions::FPEvalMethodKind Val) {
    assert(Val != LangOptions::FEM_UnsetOnCommandLine &&
           "TUPEvalMethod should never be set to FEM_UnsetOnCommandLine");
    TUFPEvalMethod = Val;
  }

  /// Retrieves the module that we're currently building, if any.
  Module *getCurrentModule();

  /// Retrieves the module whose implementation we're current compiling, if any.
  Module *getCurrentModuleImplementation();

  /// If we are preprocessing a named module.
  bool isInNamedModule() const { return ModuleDeclState.isNamedModule(); }

  /// If we are proprocessing a named interface unit.
  /// Note that a module implementation partition is not considered as an
  /// named interface unit here although it is importable
  /// to ease the parsing.
  bool isInNamedInterfaceUnit() const {
    return ModuleDeclState.isNamedInterface();
  }

  /// Get the named module name we're preprocessing.
  /// Requires we're preprocessing a named module.
  StringRef getNamedModuleName() const { return ModuleDeclState.getName(); }

  /// If we are implementing an implementation module unit.
  /// Note that the module implementation partition is not considered as an
  /// implementation unit.
  bool isInImplementationUnit() const {
    return ModuleDeclState.isImplementationUnit();
  }

  /// If we're importing a standard C++20 Named Modules.
  bool isInImportingCXXNamedModules() const {
    // NamedModuleImportPath will be non-empty only if we're importing
    // Standard C++ named modules.
    return !NamedModuleImportPath.empty() && getLangOpts().CPlusPlusModules &&
           !IsAtImport;
  }

  /// Allocate a new MacroInfo object with the provided SourceLocation.
  MacroInfo *AllocateMacroInfo(SourceLocation L);

  /// Turn the specified lexer token into a fully checked and spelled
  /// filename, e.g. as an operand of \#include.
  ///
  /// The caller is expected to provide a buffer that is large enough to hold
  /// the spelling of the filename, but is also expected to handle the case
  /// when this method decides to use a different buffer.
  ///
  /// \returns true if the input filename was in <>'s or false if it was
  /// in ""'s.
  bool GetIncludeFilenameSpelling(SourceLocation Loc,StringRef &Buffer);

  /// Given a "foo" or \<foo> reference, look up the indicated file.
  ///
  /// Returns std::nullopt on failure.  \p isAngled indicates whether the file
  /// reference is for system \#include's or not (i.e. using <> instead of "").
  OptionalFileEntryRef
  LookupFile(SourceLocation FilenameLoc, StringRef Filename, bool isAngled,
             ConstSearchDirIterator FromDir, const FileEntry *FromFile,
             ConstSearchDirIterator *CurDir, SmallVectorImpl<char> *SearchPath,
             SmallVectorImpl<char> *RelativePath,
             ModuleMap::KnownHeader *SuggestedModule, bool *IsMapped,
             bool *IsFrameworkFound, bool SkipCache = false,
             bool OpenFile = true, bool CacheFailures = true);

  /// Given a "Filename" or \<Filename> reference, look up the indicated embed
  /// resource. \p isAngled indicates whether the file reference is for
  /// system \#include's or not (i.e. using <> instead of ""). If \p OpenFile
  /// is true, the file looked up is opened for reading, otherwise it only
  /// validates that the file exists. Quoted filenames are looked up relative
  /// to \p LookupFromFile if it is nonnull.
  ///
  /// Returns std::nullopt on failure.
  OptionalFileEntryRef
  LookupEmbedFile(StringRef Filename, bool isAngled, bool OpenFile,
                  const FileEntry *LookupFromFile = nullptr);

  /// Return true if we're in the top-level file, not in a \#include.
  bool isInPrimaryFile() const;

  /// Lex an on-off-switch (C99 6.10.6p2) and verify that it is
  /// followed by EOD.  Return true if the token is not a valid on-off-switch.
  bool LexOnOffSwitch(tok::OnOffSwitch &Result);

  bool CheckMacroName(Token &MacroNameTok, MacroUse isDefineUndef,
                      bool *ShadowFlag = nullptr);

  void EnterSubmodule(Module *M, SourceLocation ImportLoc, bool ForPragma);
  Module *LeaveSubmodule(bool ForPragma);

private:
  friend void TokenLexer::ExpandFunctionArguments();

  void PushIncludeMacroStack() {
    assert(CurLexerCallback != CLK_CachingLexer &&
           "cannot push a caching lexer");
    IncludeMacroStack.emplace_back(CurLexerCallback, CurLexerSubmodule,
                                   std::move(CurLexer), CurPPLexer,
                                   std::move(CurTokenLexer), CurDirLookup);
    CurPPLexer = nullptr;
  }

  void PopIncludeMacroStack() {
    CurLexer = std::move(IncludeMacroStack.back().TheLexer);
    CurPPLexer = IncludeMacroStack.back().ThePPLexer;
    CurTokenLexer = std::move(IncludeMacroStack.back().TheTokenLexer);
    CurDirLookup  = IncludeMacroStack.back().TheDirLookup;
    CurLexerSubmodule = IncludeMacroStack.back().TheSubmodule;
    CurLexerCallback = IncludeMacroStack.back().CurLexerCallback;
    IncludeMacroStack.pop_back();
  }

  void PropagateLineStartLeadingSpaceInfo(Token &Result);

  /// Determine whether we need to create module macros for #defines in the
  /// current context.
  bool needModuleMacros() const;

  /// Update the set of active module macros and ambiguity flag for a module
  /// macro name.
  void updateModuleMacroInfo(const IdentifierInfo *II, ModuleMacroInfo &Info);

  DefMacroDirective *AllocateDefMacroDirective(MacroInfo *MI,
                                               SourceLocation Loc);
  UndefMacroDirective *AllocateUndefMacroDirective(SourceLocation UndefLoc);
  VisibilityMacroDirective *AllocateVisibilityMacroDirective(SourceLocation Loc,
                                                             bool isPublic);

  /// Lex and validate a macro name, which occurs after a
  /// \#define or \#undef.
  ///
  /// \param MacroNameTok Token that represents the name defined or undefined.
  /// \param IsDefineUndef Kind if preprocessor directive.
  /// \param ShadowFlag Points to flag that is set if macro name shadows
  ///                   a keyword.
  ///
  /// This emits a diagnostic, sets the token kind to eod,
  /// and discards the rest of the macro line if the macro name is invalid.
  void ReadMacroName(Token &MacroNameTok, MacroUse IsDefineUndef = MU_Other,
                     bool *ShadowFlag = nullptr);

  /// ReadOptionalMacroParameterListAndBody - This consumes all (i.e. the
  /// entire line) of the macro's tokens and adds them to MacroInfo, and while
  /// doing so performs certain validity checks including (but not limited to):
  ///   - # (stringization) is followed by a macro parameter
  /// \param MacroNameTok - Token that represents the macro name
  /// \param ImmediatelyAfterHeaderGuard - Macro follows an #ifdef header guard
  ///
  ///  Either returns a pointer to a MacroInfo object OR emits a diagnostic and
  ///  returns a nullptr if an invalid sequence of tokens is encountered.
  MacroInfo *ReadOptionalMacroParameterListAndBody(
      const Token &MacroNameTok, bool ImmediatelyAfterHeaderGuard);

  /// The ( starting an argument list of a macro definition has just been read.
  /// Lex the rest of the parameters and the closing ), updating \p MI with
  /// what we learn and saving in \p LastTok the last token read.
  /// Return true if an error occurs parsing the arg list.
  bool ReadMacroParameterList(MacroInfo *MI, Token& LastTok);

  /// Provide a suggestion for a typoed directive. If there is no typo, then
  /// just skip suggesting.
  ///
  /// \param Tok - Token that represents the directive
  /// \param Directive - String reference for the directive name
  void SuggestTypoedDirective(const Token &Tok, StringRef Directive) const;

  /// We just read a \#if or related directive and decided that the
  /// subsequent tokens are in the \#if'd out portion of the
  /// file.  Lex the rest of the file, until we see an \#endif.  If \p
  /// FoundNonSkipPortion is true, then we have already emitted code for part of
  /// this \#if directive, so \#else/\#elif blocks should never be entered. If
  /// \p FoundElse is false, then \#else directives are ok, if not, then we have
  /// already seen one so a \#else directive is a duplicate.  When this returns,
  /// the caller can lex the first valid token.
  void SkipExcludedConditionalBlock(SourceLocation HashTokenLoc,
                                    SourceLocation IfTokenLoc,
                                    bool FoundNonSkipPortion, bool FoundElse,
                                    SourceLocation ElseLoc = SourceLocation());

  /// Information about the result for evaluating an expression for a
  /// preprocessor directive.
  struct DirectiveEvalResult {
    /// The integral value of the expression.
    std::optional<llvm::APSInt> Value;

    /// Whether the expression was evaluated as true or not.
    bool Conditional;

    /// True if the expression contained identifiers that were undefined.
    bool IncludedUndefinedIds;

    /// The source range for the expression.
    SourceRange ExprRange;
  };

  /// Evaluate an integer constant expression that may occur after a
  /// \#if or \#elif directive and return a \p DirectiveEvalResult object.
  ///
  /// If the expression is equivalent to "!defined(X)" return X in IfNDefMacro.
  DirectiveEvalResult EvaluateDirectiveExpression(IdentifierInfo *&IfNDefMacro,
                                                  bool CheckForEoD = true);

  /// Evaluate an integer constant expression that may occur after a
  /// \#if or \#elif directive and return a \p DirectiveEvalResult object.
  ///
  /// If the expression is equivalent to "!defined(X)" return X in IfNDefMacro.
  /// \p EvaluatedDefined will contain the result of whether "defined" appeared
  /// in the evaluated expression or not.
  DirectiveEvalResult EvaluateDirectiveExpression(IdentifierInfo *&IfNDefMacro,
                                                  Token &Tok,
                                                  bool &EvaluatedDefined,
                                                  bool CheckForEoD = true);

  /// Process a '__has_embed("path" [, ...])' expression.
  ///
  /// Returns predefined `__STDC_EMBED_*` macro values if
  /// successful.
  EmbedResult EvaluateHasEmbed(Token &Tok, IdentifierInfo *II);

  /// Process a '__has_include("path")' expression.
  ///
  /// Returns true if successful.
  bool EvaluateHasInclude(Token &Tok, IdentifierInfo *II);

  /// Process '__has_include_next("path")' expression.
  ///
  /// Returns true if successful.
  bool EvaluateHasIncludeNext(Token &Tok, IdentifierInfo *II);

  /// Get the directory and file from which to start \#include_next lookup.
  std::pair<ConstSearchDirIterator, const FileEntry *>
  getIncludeNextStart(const Token &IncludeNextTok) const;

  /// Install the standard preprocessor pragmas:
  /// \#pragma GCC poison/system_header/dependency and \#pragma once.
  void RegisterBuiltinPragmas();

  /// Register builtin macros such as __LINE__ with the identifier table.
  void RegisterBuiltinMacros();

  /// If an identifier token is read that is to be expanded as a macro, handle
  /// it and return the next token as 'Tok'.  If we lexed a token, return true;
  /// otherwise the caller should lex again.
  bool HandleMacroExpandedIdentifier(Token &Identifier, const MacroDefinition &MD);

  /// Cache macro expanded tokens for TokenLexers.
  //
  /// Works like a stack; a TokenLexer adds the macro expanded tokens that is
  /// going to lex in the cache and when it finishes the tokens are removed
  /// from the end of the cache.
  Token *cacheMacroExpandedTokens(TokenLexer *tokLexer,
                                  ArrayRef<Token> tokens);

  void removeCachedMacroExpandedTokensOfLastLexer();

  /// Determine whether the next preprocessor token to be
  /// lexed is a '('.  If so, consume the token and return true, if not, this
  /// method should have no observable side-effect on the lexed tokens.
  bool isNextPPTokenLParen();

  /// After reading "MACRO(", this method is invoked to read all of the formal
  /// arguments specified for the macro invocation.  Returns null on error.
  MacroArgs *ReadMacroCallArgumentList(Token &MacroName, MacroInfo *MI,
                                       SourceLocation &MacroEnd);

  /// If an identifier token is read that is to be expanded
  /// as a builtin macro, handle it and return the next token as 'Tok'.
  void ExpandBuiltinMacro(Token &Tok);

  /// Read a \c _Pragma directive, slice it up, process it, then
  /// return the first token after the directive.
  /// This assumes that the \c _Pragma token has just been read into \p Tok.
  void Handle_Pragma(Token &Tok);

  /// Like Handle_Pragma except the pragma text is not enclosed within
  /// a string literal.
  void HandleMicrosoft__pragma(Token &Tok);

  /// Add a lexer to the top of the include stack and
  /// start lexing tokens from it instead of the current buffer.
  void EnterSourceFileWithLexer(Lexer *TheLexer, ConstSearchDirIterator Dir);

  /// Set the FileID for the preprocessor predefines.
  void setPredefinesFileID(FileID FID) {
    assert(PredefinesFileID.isInvalid() && "PredefinesFileID already set!");
    PredefinesFileID = FID;
  }

  /// Set the FileID for the PCH through header.
  void setPCHThroughHeaderFileID(FileID FID);

  /// Returns true if we are lexing from a file and not a
  /// pragma or a macro.
  static bool IsFileLexer(const Lexer* L, const PreprocessorLexer* P) {
    return L ? !L->isPragmaLexer() : P != nullptr;
  }

  static bool IsFileLexer(const IncludeStackInfo& I) {
    return IsFileLexer(I.TheLexer.get(), I.ThePPLexer);
  }

  bool IsFileLexer() const {
    return IsFileLexer(CurLexer.get(), CurPPLexer);
  }

  //===--------------------------------------------------------------------===//
  // Caching stuff.
  void CachingLex(Token &Result);

  bool InCachingLexMode() const {
    // If the Lexer pointers are 0 and IncludeMacroStack is empty, it means
    // that we are past EOF, not that we are in CachingLex mode.
    return !CurPPLexer && !CurTokenLexer && !IncludeMacroStack.empty();
  }

  void EnterCachingLexMode();
  void EnterCachingLexModeUnchecked();

  void ExitCachingLexMode() {
    if (InCachingLexMode())
      RemoveTopOfLexerStack();
  }

  const Token &PeekAhead(unsigned N);
  void AnnotatePreviousCachedTokens(const Token &Tok);

  //===--------------------------------------------------------------------===//
  /// Handle*Directive - implement the various preprocessor directives.  These
  /// should side-effect the current preprocessor object so that the next call
  /// to Lex() will return the appropriate token next.
  void HandleLineDirective();
  void HandleDigitDirective(Token &Tok);
  void HandleUserDiagnosticDirective(Token &Tok, bool isWarning);
  void HandleIdentSCCSDirective(Token &Tok);
  void HandleMacroPublicDirective(Token &Tok);
  void HandleMacroPrivateDirective();

  /// An additional notification that can be produced by a header inclusion or
  /// import to tell the parser what happened.
  struct ImportAction {
    enum ActionKind {
      None,
      ModuleBegin,
      ModuleImport,
      HeaderUnitImport,
      SkippedModuleImport,
      Failure,
    } Kind;
    Module *ModuleForHeader = nullptr;

    ImportAction(ActionKind AK, Module *Mod = nullptr)
        : Kind(AK), ModuleForHeader(Mod) {
      assert((AK == None || Mod || AK == Failure) &&
             "no module for module action");
    }
  };

  OptionalFileEntryRef LookupHeaderIncludeOrImport(
      ConstSearchDirIterator *CurDir, StringRef &Filename,
      SourceLocation FilenameLoc, CharSourceRange FilenameRange,
      const Token &FilenameTok, bool &IsFrameworkFound, bool IsImportDecl,
      bool &IsMapped, ConstSearchDirIterator LookupFrom,
      const FileEntry *LookupFromFile, StringRef &LookupFilename,
      SmallVectorImpl<char> &RelativePath, SmallVectorImpl<char> &SearchPath,
      ModuleMap::KnownHeader &SuggestedModule, bool isAngled);
  // Binary data inclusion
  void HandleEmbedDirective(SourceLocation HashLoc, Token &Tok,
                            const FileEntry *LookupFromFile = nullptr);
  void HandleEmbedDirectiveImpl(SourceLocation HashLoc,
                                const LexEmbedParametersResult &Params,
                                StringRef BinaryContents);

  // File inclusion.
  void HandleIncludeDirective(SourceLocation HashLoc, Token &Tok,
                              ConstSearchDirIterator LookupFrom = nullptr,
                              const FileEntry *LookupFromFile = nullptr);
  ImportAction
  HandleHeaderIncludeOrImport(SourceLocation HashLoc, Token &IncludeTok,
                              Token &FilenameTok, SourceLocation EndLoc,
                              ConstSearchDirIterator LookupFrom = nullptr,
                              const FileEntry *LookupFromFile = nullptr);
  void HandleIncludeNextDirective(SourceLocation HashLoc, Token &Tok);
  void HandleIncludeMacrosDirective(SourceLocation HashLoc, Token &Tok);
  void HandleImportDirective(SourceLocation HashLoc, Token &Tok);
  void HandleMicrosoftImportDirective(Token &Tok);

public:
  /// Check that the given module is available, producing a diagnostic if not.
  /// \return \c true if the check failed (because the module is not available).
  ///         \c false if the module appears to be usable.
  static bool checkModuleIsAvailable(const LangOptions &LangOpts,
                                     const TargetInfo &TargetInfo,
                                     const Module &M, DiagnosticsEngine &Diags);

  // Module inclusion testing.
  /// Find the module that owns the source or header file that
  /// \p Loc points to. If the location is in a file that was included
  /// into a module, or is outside any module, returns nullptr.
  Module *getModuleForLocation(SourceLocation Loc, bool AllowTextual);

  /// We want to produce a diagnostic at location IncLoc concerning an
  /// unreachable effect at location MLoc (eg, where a desired entity was
  /// declared or defined). Determine whether the right way to make MLoc
  /// reachable is by #include, and if so, what header should be included.
  ///
  /// This is not necessarily fast, and might load unexpected module maps, so
  /// should only be called by code that intends to produce an error.
  ///
  /// \param IncLoc The location at which the missing effect was detected.
  /// \param MLoc A location within an unimported module at which the desired
  ///        effect occurred.
  /// \return A file that can be #included to provide the desired effect. Null
  ///         if no such file could be determined or if a #include is not
  ///         appropriate (eg, if a module should be imported instead).
  OptionalFileEntryRef getHeaderToIncludeForDiagnostics(SourceLocation IncLoc,
                                                        SourceLocation MLoc);

  bool isRecordingPreamble() const {
    return PreambleConditionalStack.isRecording();
  }

  bool hasRecordedPreamble() const {
    return PreambleConditionalStack.hasRecordedPreamble();
  }

  ArrayRef<PPConditionalInfo> getPreambleConditionalStack() const {
      return PreambleConditionalStack.getStack();
  }

  void setRecordedPreambleConditionalStack(ArrayRef<PPConditionalInfo> s) {
    PreambleConditionalStack.setStack(s);
  }

  void setReplayablePreambleConditionalStack(
      ArrayRef<PPConditionalInfo> s, std::optional<PreambleSkipInfo> SkipInfo) {
    PreambleConditionalStack.startReplaying();
    PreambleConditionalStack.setStack(s);
    PreambleConditionalStack.SkipInfo = SkipInfo;
  }

  std::optional<PreambleSkipInfo> getPreambleSkipInfo() const {
    return PreambleConditionalStack.SkipInfo;
  }

private:
  /// After processing predefined file, initialize the conditional stack from
  /// the preamble.
  void replayPreambleConditionalStack();

  // Macro handling.
  void HandleDefineDirective(Token &Tok, bool ImmediatelyAfterHeaderGuard);
  void HandleUndefDirective();

  // Conditional Inclusion.
  void HandleIfdefDirective(Token &Result, const Token &HashToken,
                            bool isIfndef, bool ReadAnyTokensBeforeDirective);
  void HandleIfDirective(Token &IfToken, const Token &HashToken,
                         bool ReadAnyTokensBeforeDirective);
  void HandleEndifDirective(Token &EndifToken);
  void HandleElseDirective(Token &Result, const Token &HashToken);
  void HandleElifFamilyDirective(Token &ElifToken, const Token &HashToken,
                                 tok::PPKeywordKind Kind);

  // Pragmas.
  void HandlePragmaDirective(PragmaIntroducer Introducer);

public:
  void HandlePragmaOnce(Token &OnceTok);
  void HandlePragmaMark(Token &MarkTok);
  void HandlePragmaPoison();
  void HandlePragmaSystemHeader(Token &SysHeaderTok);
  void HandlePragmaDependency(Token &DependencyTok);
  void HandlePragmaPushMacro(Token &Tok);
  void HandlePragmaPopMacro(Token &Tok);
  void HandlePragmaIncludeAlias(Token &Tok);
  void HandlePragmaModuleBuild(Token &Tok);
  void HandlePragmaHdrstop(Token &Tok);
  IdentifierInfo *ParsePragmaPushOrPopMacro(Token &Tok);

  // Return true and store the first token only if any CommentHandler
  // has inserted some tokens and getCommentRetentionState() is false.
  bool HandleComment(Token &result, SourceRange Comment);

  /// A macro is used, update information about macros that need unused
  /// warnings.
  void markMacroAsUsed(MacroInfo *MI);

  void addMacroDeprecationMsg(const IdentifierInfo *II, std::string Msg,
                              SourceLocation AnnotationLoc) {
    auto Annotations = AnnotationInfos.find(II);
    if (Annotations == AnnotationInfos.end())
      AnnotationInfos.insert(std::make_pair(
          II,
          MacroAnnotations::makeDeprecation(AnnotationLoc, std::move(Msg))));
    else
      Annotations->second.DeprecationInfo =
          MacroAnnotationInfo{AnnotationLoc, std::move(Msg)};
  }

  void addRestrictExpansionMsg(const IdentifierInfo *II, std::string Msg,
                               SourceLocation AnnotationLoc) {
    auto Annotations = AnnotationInfos.find(II);
    if (Annotations == AnnotationInfos.end())
      AnnotationInfos.insert(
          std::make_pair(II, MacroAnnotations::makeRestrictExpansion(
                                 AnnotationLoc, std::move(Msg))));
    else
      Annotations->second.RestrictExpansionInfo =
          MacroAnnotationInfo{AnnotationLoc, std::move(Msg)};
  }

  void addFinalLoc(const IdentifierInfo *II, SourceLocation AnnotationLoc) {
    auto Annotations = AnnotationInfos.find(II);
    if (Annotations == AnnotationInfos.end())
      AnnotationInfos.insert(
          std::make_pair(II, MacroAnnotations::makeFinal(AnnotationLoc)));
    else
      Annotations->second.FinalAnnotationLoc = AnnotationLoc;
  }

  const MacroAnnotations &getMacroAnnotations(const IdentifierInfo *II) const {
    return AnnotationInfos.find(II)->second;
  }

  void emitMacroExpansionWarnings(const Token &Identifier,
                                  bool IsIfnDef = false) const {
    IdentifierInfo *Info = Identifier.getIdentifierInfo();
    if (Info->isDeprecatedMacro())
      emitMacroDeprecationWarning(Identifier);

    if (Info->isRestrictExpansion() &&
        !SourceMgr.isInMainFile(Identifier.getLocation()))
      emitRestrictExpansionWarning(Identifier);

    if (!IsIfnDef) {
      if (Info->getName() == "INFINITY" && getLangOpts().NoHonorInfs)
        emitRestrictInfNaNWarning(Identifier, 0);
      if (Info->getName() == "NAN" && getLangOpts().NoHonorNaNs)
        emitRestrictInfNaNWarning(Identifier, 1);
    }
  }

  static void processPathForFileMacro(SmallVectorImpl<char> &Path,
                                      const LangOptions &LangOpts,
                                      const TargetInfo &TI);

  static void processPathToFileName(SmallVectorImpl<char> &FileName,
                                    const PresumedLoc &PLoc,
                                    const LangOptions &LangOpts,
                                    const TargetInfo &TI);

private:
  void emitMacroDeprecationWarning(const Token &Identifier) const;
  void emitRestrictExpansionWarning(const Token &Identifier) const;
  void emitFinalMacroWarning(const Token &Identifier, bool IsUndef) const;
  void emitRestrictInfNaNWarning(const Token &Identifier,
                                 unsigned DiagSelection) const;

  /// This boolean state keeps track if the current scanned token (by this PP)
  /// is in an "-Wunsafe-buffer-usage" opt-out region. Assuming PP scans a
  /// translation unit in a linear order.
  bool InSafeBufferOptOutRegion = false;

  /// Hold the start location of the current "-Wunsafe-buffer-usage" opt-out
  /// region if PP is currently in such a region.  Hold undefined value
  /// otherwise.
  SourceLocation CurrentSafeBufferOptOutStart; // It is used to report the start location of an never-closed region.

  using SafeBufferOptOutRegionsTy =
      SmallVector<std::pair<SourceLocation, SourceLocation>, 16>;
  // An ordered sequence of "-Wunsafe-buffer-usage" opt-out regions in this
  // translation unit. Each region is represented by a pair of start and
  // end locations.
  SafeBufferOptOutRegionsTy SafeBufferOptOutMap;

  // The "-Wunsafe-buffer-usage" opt-out regions in loaded ASTs.  We use the
  // following structure to manage them by their ASTs.
  struct {
    // A map from unique IDs to region maps of loaded ASTs.  The ID identifies a
    // loaded AST. See `SourceManager::getUniqueLoadedASTID`.
    llvm::DenseMap<FileID, SafeBufferOptOutRegionsTy> LoadedRegions;

    // Returns a reference to the safe buffer opt-out regions of the loaded
    // AST where `Loc` belongs to. (Construct if absent)
    SafeBufferOptOutRegionsTy &
    findAndConsLoadedOptOutMap(SourceLocation Loc, SourceManager &SrcMgr) {
      return LoadedRegions[SrcMgr.getUniqueLoadedASTFileID(Loc)];
    }

    // Returns a reference to the safe buffer opt-out regions of the loaded
    // AST where `Loc` belongs to. (This const function returns nullptr if
    // absent.)
    const SafeBufferOptOutRegionsTy *
    lookupLoadedOptOutMap(SourceLocation Loc,
                          const SourceManager &SrcMgr) const {
      FileID FID = SrcMgr.getUniqueLoadedASTFileID(Loc);
      auto Iter = LoadedRegions.find(FID);

      if (Iter == LoadedRegions.end())
        return nullptr;
      return &Iter->getSecond();
    }
  } LoadedSafeBufferOptOutMap;

public:
  /// \return true iff the given `Loc` is in a "-Wunsafe-buffer-usage" opt-out
  /// region.  This `Loc` must be a source location that has been pre-processed.
  bool isSafeBufferOptOut(const SourceManager&SourceMgr, const SourceLocation &Loc) const;

  /// Alter the state of whether this PP currently is in a
  /// "-Wunsafe-buffer-usage" opt-out region.
  ///
  /// \param isEnter true if this PP is entering a region; otherwise, this PP
  /// is exiting a region
  /// \param Loc the location of the entry or exit of a
  /// region
  /// \return true iff it is INVALID to enter or exit a region, i.e.,
  /// attempt to enter a region before exiting a previous region, or exiting a
  /// region that PP is not currently in.
  bool enterOrExitSafeBufferOptOutRegion(bool isEnter,
                                         const SourceLocation &Loc);

  /// \return true iff this PP is currently in a "-Wunsafe-buffer-usage"
  ///          opt-out region
  bool isPPInSafeBufferOptOutRegion();

  /// \param StartLoc output argument. It will be set to the start location of
  /// the current "-Wunsafe-buffer-usage" opt-out region iff this function
  /// returns true.
  /// \return true iff this PP is currently in a "-Wunsafe-buffer-usage"
  ///          opt-out region
  bool isPPInSafeBufferOptOutRegion(SourceLocation &StartLoc);

  /// \return a sequence of SourceLocations representing ordered opt-out regions
  /// specified by
  /// `\#pragma clang unsafe_buffer_usage begin/end`s of this translation unit.
  SmallVector<SourceLocation, 64> serializeSafeBufferOptOutMap() const;

  /// \param SrcLocSeqs a sequence of SourceLocations deserialized from a
  /// record of code `PP_UNSAFE_BUFFER_USAGE`.
  /// \return true iff the `Preprocessor` has been updated; false `Preprocessor`
  /// is same as itself before the call.
  bool setDeserializedSafeBufferOptOutMap(
      const SmallVectorImpl<SourceLocation> &SrcLocSeqs);

private:
  /// Helper functions to forward lexing to the actual lexer. They all share the
  /// same signature.
  static bool CLK_Lexer(Preprocessor &P, Token &Result) {
    return P.CurLexer->Lex(Result);
  }
  static bool CLK_TokenLexer(Preprocessor &P, Token &Result) {
    return P.CurTokenLexer->Lex(Result);
  }
  static bool CLK_CachingLexer(Preprocessor &P, Token &Result) {
    P.CachingLex(Result);
    return true;
  }
  static bool CLK_DependencyDirectivesLexer(Preprocessor &P, Token &Result) {
    return P.CurLexer->LexDependencyDirectiveToken(Result);
  }
  static bool CLK_LexAfterModuleImport(Preprocessor &P, Token &Result) {
    return P.LexAfterModuleImport(Result);
  }
};

/// Abstract base class that describes a handler that will receive
/// source ranges for each of the comments encountered in the source file.
class CommentHandler {
public:
  virtual ~CommentHandler();

  // The handler shall return true if it has pushed any tokens
  // to be read using e.g. EnterToken or EnterTokenStream.
  virtual bool HandleComment(Preprocessor &PP, SourceRange Comment) = 0;
};

/// Abstract base class that describes a handler that will receive
/// source ranges for empty lines encountered in the source file.
class EmptylineHandler {
public:
  virtual ~EmptylineHandler();

  // The handler handles empty lines.
  virtual void HandleEmptyline(SourceRange Range) = 0;
};

/// Helper class to shuttle information about #embed directives from the
/// preprocessor to the parser through an annotation token.
struct EmbedAnnotationData {
  StringRef BinaryData;
};

/// Registry of pragma handlers added by plugins
using PragmaHandlerRegistry = llvm::Registry<PragmaHandler>;

} // namespace clang

#endif // LLVM_CLANG_LEX_PREPROCESSOR_H
