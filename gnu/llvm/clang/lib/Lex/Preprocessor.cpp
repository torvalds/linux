//===- Preprocessor.cpp - C Language Family Preprocessor Implementation ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Preprocessor interface.
//
//===----------------------------------------------------------------------===//
//
// Options to support:
//   -H       - Print the name of each header file used.
//   -d[DNI] - Dump various things.
//   -fworking-directory - #line's with preprocessor's working dir.
//   -fpreprocessed
//   -dependency-file,-M,-MM,-MF,-MG,-MP,-MT,-MQ,-MD,-MMD
//   -W*
//   -w
//
// Messages to emit:
//   "Multiple include guards may be useful for:\n"
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemStatCache.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/CodeCompletionHandler.h"
#include "clang/Lex/ExternalPreprocessorSource.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/MacroArgs.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/PreprocessorLexer.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/ScratchBuffer.h"
#include "clang/Lex/Token.h"
#include "clang/Lex/TokenLexer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

/// Minimum distance between two check points, in tokens.
static constexpr unsigned CheckPointStepSize = 1024;

LLVM_INSTANTIATE_REGISTRY(PragmaHandlerRegistry)

ExternalPreprocessorSource::~ExternalPreprocessorSource() = default;

Preprocessor::Preprocessor(std::shared_ptr<PreprocessorOptions> PPOpts,
                           DiagnosticsEngine &diags, const LangOptions &opts,
                           SourceManager &SM, HeaderSearch &Headers,
                           ModuleLoader &TheModuleLoader,
                           IdentifierInfoLookup *IILookup, bool OwnsHeaders,
                           TranslationUnitKind TUKind)
    : PPOpts(std::move(PPOpts)), Diags(&diags), LangOpts(opts),
      FileMgr(Headers.getFileMgr()), SourceMgr(SM),
      ScratchBuf(new ScratchBuffer(SourceMgr)), HeaderInfo(Headers),
      TheModuleLoader(TheModuleLoader), ExternalSource(nullptr),
      // As the language options may have not been loaded yet (when
      // deserializing an ASTUnit), adding keywords to the identifier table is
      // deferred to Preprocessor::Initialize().
      Identifiers(IILookup), PragmaHandlers(new PragmaNamespace(StringRef())),
      TUKind(TUKind), SkipMainFilePreamble(0, true),
      CurSubmoduleState(&NullSubmoduleState) {
  OwnsHeaderSearch = OwnsHeaders;

  // Default to discarding comments.
  KeepComments = false;
  KeepMacroComments = false;
  SuppressIncludeNotFoundError = false;

  // Macro expansion is enabled.
  DisableMacroExpansion = false;
  MacroExpansionInDirectivesOverride = false;
  InMacroArgs = false;
  ArgMacro = nullptr;
  InMacroArgPreExpansion = false;
  NumCachedTokenLexers = 0;
  PragmasEnabled = true;
  ParsingIfOrElifDirective = false;
  PreprocessedOutput = false;

  // We haven't read anything from the external source.
  ReadMacrosFromExternalSource = false;

  BuiltinInfo = std::make_unique<Builtin::Context>();

  // "Poison" __VA_ARGS__, __VA_OPT__ which can only appear in the expansion of
  // a macro. They get unpoisoned where it is allowed.
  (Ident__VA_ARGS__ = getIdentifierInfo("__VA_ARGS__"))->setIsPoisoned();
  SetPoisonReason(Ident__VA_ARGS__,diag::ext_pp_bad_vaargs_use);
  (Ident__VA_OPT__ = getIdentifierInfo("__VA_OPT__"))->setIsPoisoned();
  SetPoisonReason(Ident__VA_OPT__,diag::ext_pp_bad_vaopt_use);

  // Initialize the pragma handlers.
  RegisterBuiltinPragmas();

  // Initialize builtin macros like __LINE__ and friends.
  RegisterBuiltinMacros();

  if(LangOpts.Borland) {
    Ident__exception_info        = getIdentifierInfo("_exception_info");
    Ident___exception_info       = getIdentifierInfo("__exception_info");
    Ident_GetExceptionInfo       = getIdentifierInfo("GetExceptionInformation");
    Ident__exception_code        = getIdentifierInfo("_exception_code");
    Ident___exception_code       = getIdentifierInfo("__exception_code");
    Ident_GetExceptionCode       = getIdentifierInfo("GetExceptionCode");
    Ident__abnormal_termination  = getIdentifierInfo("_abnormal_termination");
    Ident___abnormal_termination = getIdentifierInfo("__abnormal_termination");
    Ident_AbnormalTermination    = getIdentifierInfo("AbnormalTermination");
  } else {
    Ident__exception_info = Ident__exception_code = nullptr;
    Ident__abnormal_termination = Ident___exception_info = nullptr;
    Ident___exception_code = Ident___abnormal_termination = nullptr;
    Ident_GetExceptionInfo = Ident_GetExceptionCode = nullptr;
    Ident_AbnormalTermination = nullptr;
  }

  // Default incremental processing to -fincremental-extensions, clients can
  // override with `enableIncrementalProcessing` if desired.
  IncrementalProcessing = LangOpts.IncrementalExtensions;

  // If using a PCH where a #pragma hdrstop is expected, start skipping tokens.
  if (usingPCHWithPragmaHdrStop())
    SkippingUntilPragmaHdrStop = true;

  // If using a PCH with a through header, start skipping tokens.
  if (!this->PPOpts->PCHThroughHeader.empty() &&
      !this->PPOpts->ImplicitPCHInclude.empty())
    SkippingUntilPCHThroughHeader = true;

  if (this->PPOpts->GeneratePreamble)
    PreambleConditionalStack.startRecording();

  MaxTokens = LangOpts.MaxTokens;
}

Preprocessor::~Preprocessor() {
  assert(BacktrackPositions.empty() && "EnableBacktrack/Backtrack imbalance!");

  IncludeMacroStack.clear();

  // Free any cached macro expanders.
  // This populates MacroArgCache, so all TokenLexers need to be destroyed
  // before the code below that frees up the MacroArgCache list.
  std::fill(TokenLexerCache, TokenLexerCache + NumCachedTokenLexers, nullptr);
  CurTokenLexer.reset();

  // Free any cached MacroArgs.
  for (MacroArgs *ArgList = MacroArgCache; ArgList;)
    ArgList = ArgList->deallocate();

  // Delete the header search info, if we own it.
  if (OwnsHeaderSearch)
    delete &HeaderInfo;
}

void Preprocessor::Initialize(const TargetInfo &Target,
                              const TargetInfo *AuxTarget) {
  assert((!this->Target || this->Target == &Target) &&
         "Invalid override of target information");
  this->Target = &Target;

  assert((!this->AuxTarget || this->AuxTarget == AuxTarget) &&
         "Invalid override of aux target information.");
  this->AuxTarget = AuxTarget;

  // Initialize information about built-ins.
  BuiltinInfo->InitializeTarget(Target, AuxTarget);
  HeaderInfo.setTarget(Target);

  // Populate the identifier table with info about keywords for the current language.
  Identifiers.AddKeywords(LangOpts);

  // Initialize the __FTL_EVAL_METHOD__ macro to the TargetInfo.
  setTUFPEvalMethod(getTargetInfo().getFPEvalMethod());

  if (getLangOpts().getFPEvalMethod() == LangOptions::FEM_UnsetOnCommandLine)
    // Use setting from TargetInfo.
    setCurrentFPEvalMethod(SourceLocation(), Target.getFPEvalMethod());
  else
    // Set initial value of __FLT_EVAL_METHOD__ from the command line.
    setCurrentFPEvalMethod(SourceLocation(), getLangOpts().getFPEvalMethod());
}

void Preprocessor::InitializeForModelFile() {
  NumEnteredSourceFiles = 0;

  // Reset pragmas
  PragmaHandlersBackup = std::move(PragmaHandlers);
  PragmaHandlers = std::make_unique<PragmaNamespace>(StringRef());
  RegisterBuiltinPragmas();

  // Reset PredefinesFileID
  PredefinesFileID = FileID();
}

void Preprocessor::FinalizeForModelFile() {
  NumEnteredSourceFiles = 1;

  PragmaHandlers = std::move(PragmaHandlersBackup);
}

void Preprocessor::DumpToken(const Token &Tok, bool DumpFlags) const {
  llvm::errs() << tok::getTokenName(Tok.getKind());

  if (!Tok.isAnnotation())
    llvm::errs() << " '" << getSpelling(Tok) << "'";

  if (!DumpFlags) return;

  llvm::errs() << "\t";
  if (Tok.isAtStartOfLine())
    llvm::errs() << " [StartOfLine]";
  if (Tok.hasLeadingSpace())
    llvm::errs() << " [LeadingSpace]";
  if (Tok.isExpandDisabled())
    llvm::errs() << " [ExpandDisabled]";
  if (Tok.needsCleaning()) {
    const char *Start = SourceMgr.getCharacterData(Tok.getLocation());
    llvm::errs() << " [UnClean='" << StringRef(Start, Tok.getLength())
                 << "']";
  }

  llvm::errs() << "\tLoc=<";
  DumpLocation(Tok.getLocation());
  llvm::errs() << ">";
}

void Preprocessor::DumpLocation(SourceLocation Loc) const {
  Loc.print(llvm::errs(), SourceMgr);
}

void Preprocessor::DumpMacro(const MacroInfo &MI) const {
  llvm::errs() << "MACRO: ";
  for (unsigned i = 0, e = MI.getNumTokens(); i != e; ++i) {
    DumpToken(MI.getReplacementToken(i));
    llvm::errs() << "  ";
  }
  llvm::errs() << "\n";
}

void Preprocessor::PrintStats() {
  llvm::errs() << "\n*** Preprocessor Stats:\n";
  llvm::errs() << NumDirectives << " directives found:\n";
  llvm::errs() << "  " << NumDefined << " #define.\n";
  llvm::errs() << "  " << NumUndefined << " #undef.\n";
  llvm::errs() << "  #include/#include_next/#import:\n";
  llvm::errs() << "    " << NumEnteredSourceFiles << " source files entered.\n";
  llvm::errs() << "    " << MaxIncludeStackDepth << " max include stack depth\n";
  llvm::errs() << "  " << NumIf << " #if/#ifndef/#ifdef.\n";
  llvm::errs() << "  " << NumElse << " #else/#elif/#elifdef/#elifndef.\n";
  llvm::errs() << "  " << NumEndif << " #endif.\n";
  llvm::errs() << "  " << NumPragma << " #pragma.\n";
  llvm::errs() << NumSkipped << " #if/#ifndef#ifdef regions skipped\n";

  llvm::errs() << NumMacroExpanded << "/" << NumFnMacroExpanded << "/"
             << NumBuiltinMacroExpanded << " obj/fn/builtin macros expanded, "
             << NumFastMacroExpanded << " on the fast path.\n";
  llvm::errs() << (NumFastTokenPaste+NumTokenPaste)
             << " token paste (##) operations performed, "
             << NumFastTokenPaste << " on the fast path.\n";

  llvm::errs() << "\nPreprocessor Memory: " << getTotalMemory() << "B total";

  llvm::errs() << "\n  BumpPtr: " << BP.getTotalMemory();
  llvm::errs() << "\n  Macro Expanded Tokens: "
               << llvm::capacity_in_bytes(MacroExpandedTokens);
  llvm::errs() << "\n  Predefines Buffer: " << Predefines.capacity();
  // FIXME: List information for all submodules.
  llvm::errs() << "\n  Macros: "
               << llvm::capacity_in_bytes(CurSubmoduleState->Macros);
  llvm::errs() << "\n  #pragma push_macro Info: "
               << llvm::capacity_in_bytes(PragmaPushMacroInfo);
  llvm::errs() << "\n  Poison Reasons: "
               << llvm::capacity_in_bytes(PoisonReasons);
  llvm::errs() << "\n  Comment Handlers: "
               << llvm::capacity_in_bytes(CommentHandlers) << "\n";
}

Preprocessor::macro_iterator
Preprocessor::macro_begin(bool IncludeExternalMacros) const {
  if (IncludeExternalMacros && ExternalSource &&
      !ReadMacrosFromExternalSource) {
    ReadMacrosFromExternalSource = true;
    ExternalSource->ReadDefinedMacros();
  }

  // Make sure we cover all macros in visible modules.
  for (const ModuleMacro &Macro : ModuleMacros)
    CurSubmoduleState->Macros.insert(std::make_pair(Macro.II, MacroState()));

  return CurSubmoduleState->Macros.begin();
}

size_t Preprocessor::getTotalMemory() const {
  return BP.getTotalMemory()
    + llvm::capacity_in_bytes(MacroExpandedTokens)
    + Predefines.capacity() /* Predefines buffer. */
    // FIXME: Include sizes from all submodules, and include MacroInfo sizes,
    // and ModuleMacros.
    + llvm::capacity_in_bytes(CurSubmoduleState->Macros)
    + llvm::capacity_in_bytes(PragmaPushMacroInfo)
    + llvm::capacity_in_bytes(PoisonReasons)
    + llvm::capacity_in_bytes(CommentHandlers);
}

Preprocessor::macro_iterator
Preprocessor::macro_end(bool IncludeExternalMacros) const {
  if (IncludeExternalMacros && ExternalSource &&
      !ReadMacrosFromExternalSource) {
    ReadMacrosFromExternalSource = true;
    ExternalSource->ReadDefinedMacros();
  }

  return CurSubmoduleState->Macros.end();
}

/// Compares macro tokens with a specified token value sequence.
static bool MacroDefinitionEquals(const MacroInfo *MI,
                                  ArrayRef<TokenValue> Tokens) {
  return Tokens.size() == MI->getNumTokens() &&
      std::equal(Tokens.begin(), Tokens.end(), MI->tokens_begin());
}

StringRef Preprocessor::getLastMacroWithSpelling(
                                    SourceLocation Loc,
                                    ArrayRef<TokenValue> Tokens) const {
  SourceLocation BestLocation;
  StringRef BestSpelling;
  for (Preprocessor::macro_iterator I = macro_begin(), E = macro_end();
       I != E; ++I) {
    const MacroDirective::DefInfo
      Def = I->second.findDirectiveAtLoc(Loc, SourceMgr);
    if (!Def || !Def.getMacroInfo())
      continue;
    if (!Def.getMacroInfo()->isObjectLike())
      continue;
    if (!MacroDefinitionEquals(Def.getMacroInfo(), Tokens))
      continue;
    SourceLocation Location = Def.getLocation();
    // Choose the macro defined latest.
    if (BestLocation.isInvalid() ||
        (Location.isValid() &&
         SourceMgr.isBeforeInTranslationUnit(BestLocation, Location))) {
      BestLocation = Location;
      BestSpelling = I->first->getName();
    }
  }
  return BestSpelling;
}

void Preprocessor::recomputeCurLexerKind() {
  if (CurLexer)
    CurLexerCallback = CurLexer->isDependencyDirectivesLexer()
                           ? CLK_DependencyDirectivesLexer
                           : CLK_Lexer;
  else if (CurTokenLexer)
    CurLexerCallback = CLK_TokenLexer;
  else
    CurLexerCallback = CLK_CachingLexer;
}

bool Preprocessor::SetCodeCompletionPoint(FileEntryRef File,
                                          unsigned CompleteLine,
                                          unsigned CompleteColumn) {
  assert(CompleteLine && CompleteColumn && "Starts from 1:1");
  assert(!CodeCompletionFile && "Already set");

  // Load the actual file's contents.
  std::optional<llvm::MemoryBufferRef> Buffer =
      SourceMgr.getMemoryBufferForFileOrNone(File);
  if (!Buffer)
    return true;

  // Find the byte position of the truncation point.
  const char *Position = Buffer->getBufferStart();
  for (unsigned Line = 1; Line < CompleteLine; ++Line) {
    for (; *Position; ++Position) {
      if (*Position != '\r' && *Position != '\n')
        continue;

      // Eat \r\n or \n\r as a single line.
      if ((Position[1] == '\r' || Position[1] == '\n') &&
          Position[0] != Position[1])
        ++Position;
      ++Position;
      break;
    }
  }

  Position += CompleteColumn - 1;

  // If pointing inside the preamble, adjust the position at the beginning of
  // the file after the preamble.
  if (SkipMainFilePreamble.first &&
      SourceMgr.getFileEntryForID(SourceMgr.getMainFileID()) == File) {
    if (Position - Buffer->getBufferStart() < SkipMainFilePreamble.first)
      Position = Buffer->getBufferStart() + SkipMainFilePreamble.first;
  }

  if (Position > Buffer->getBufferEnd())
    Position = Buffer->getBufferEnd();

  CodeCompletionFile = File;
  CodeCompletionOffset = Position - Buffer->getBufferStart();

  auto NewBuffer = llvm::WritableMemoryBuffer::getNewUninitMemBuffer(
      Buffer->getBufferSize() + 1, Buffer->getBufferIdentifier());
  char *NewBuf = NewBuffer->getBufferStart();
  char *NewPos = std::copy(Buffer->getBufferStart(), Position, NewBuf);
  *NewPos = '\0';
  std::copy(Position, Buffer->getBufferEnd(), NewPos+1);
  SourceMgr.overrideFileContents(File, std::move(NewBuffer));

  return false;
}

void Preprocessor::CodeCompleteIncludedFile(llvm::StringRef Dir,
                                            bool IsAngled) {
  setCodeCompletionReached();
  if (CodeComplete)
    CodeComplete->CodeCompleteIncludedFile(Dir, IsAngled);
}

void Preprocessor::CodeCompleteNaturalLanguage() {
  setCodeCompletionReached();
  if (CodeComplete)
    CodeComplete->CodeCompleteNaturalLanguage();
}

/// getSpelling - This method is used to get the spelling of a token into a
/// SmallVector. Note that the returned StringRef may not point to the
/// supplied buffer if a copy can be avoided.
StringRef Preprocessor::getSpelling(const Token &Tok,
                                          SmallVectorImpl<char> &Buffer,
                                          bool *Invalid) const {
  // NOTE: this has to be checked *before* testing for an IdentifierInfo.
  if (Tok.isNot(tok::raw_identifier) && !Tok.hasUCN()) {
    // Try the fast path.
    if (const IdentifierInfo *II = Tok.getIdentifierInfo())
      return II->getName();
  }

  // Resize the buffer if we need to copy into it.
  if (Tok.needsCleaning())
    Buffer.resize(Tok.getLength());

  const char *Ptr = Buffer.data();
  unsigned Len = getSpelling(Tok, Ptr, Invalid);
  return StringRef(Ptr, Len);
}

/// CreateString - Plop the specified string into a scratch buffer and return a
/// location for it.  If specified, the source location provides a source
/// location for the token.
void Preprocessor::CreateString(StringRef Str, Token &Tok,
                                SourceLocation ExpansionLocStart,
                                SourceLocation ExpansionLocEnd) {
  Tok.setLength(Str.size());

  const char *DestPtr;
  SourceLocation Loc = ScratchBuf->getToken(Str.data(), Str.size(), DestPtr);

  if (ExpansionLocStart.isValid())
    Loc = SourceMgr.createExpansionLoc(Loc, ExpansionLocStart,
                                       ExpansionLocEnd, Str.size());
  Tok.setLocation(Loc);

  // If this is a raw identifier or a literal token, set the pointer data.
  if (Tok.is(tok::raw_identifier))
    Tok.setRawIdentifierData(DestPtr);
  else if (Tok.isLiteral())
    Tok.setLiteralData(DestPtr);
}

SourceLocation Preprocessor::SplitToken(SourceLocation Loc, unsigned Length) {
  auto &SM = getSourceManager();
  SourceLocation SpellingLoc = SM.getSpellingLoc(Loc);
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(SpellingLoc);
  bool Invalid = false;
  StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
  if (Invalid)
    return SourceLocation();

  // FIXME: We could consider re-using spelling for tokens we see repeatedly.
  const char *DestPtr;
  SourceLocation Spelling =
      ScratchBuf->getToken(Buffer.data() + LocInfo.second, Length, DestPtr);
  return SM.createTokenSplitLoc(Spelling, Loc, Loc.getLocWithOffset(Length));
}

Module *Preprocessor::getCurrentModule() {
  if (!getLangOpts().isCompilingModule())
    return nullptr;

  return getHeaderSearchInfo().lookupModule(getLangOpts().CurrentModule);
}

Module *Preprocessor::getCurrentModuleImplementation() {
  if (!getLangOpts().isCompilingModuleImplementation())
    return nullptr;

  return getHeaderSearchInfo().lookupModule(getLangOpts().ModuleName);
}

//===----------------------------------------------------------------------===//
// Preprocessor Initialization Methods
//===----------------------------------------------------------------------===//

/// EnterMainSourceFile - Enter the specified FileID as the main source file,
/// which implicitly adds the builtin defines etc.
void Preprocessor::EnterMainSourceFile() {
  // We do not allow the preprocessor to reenter the main file.  Doing so will
  // cause FileID's to accumulate information from both runs (e.g. #line
  // information) and predefined macros aren't guaranteed to be set properly.
  assert(NumEnteredSourceFiles == 0 && "Cannot reenter the main file!");
  FileID MainFileID = SourceMgr.getMainFileID();

  // If MainFileID is loaded it means we loaded an AST file, no need to enter
  // a main file.
  if (!SourceMgr.isLoadedFileID(MainFileID)) {
    // Enter the main file source buffer.
    EnterSourceFile(MainFileID, nullptr, SourceLocation());

    // If we've been asked to skip bytes in the main file (e.g., as part of a
    // precompiled preamble), do so now.
    if (SkipMainFilePreamble.first > 0)
      CurLexer->SetByteOffset(SkipMainFilePreamble.first,
                              SkipMainFilePreamble.second);

    // Tell the header info that the main file was entered.  If the file is later
    // #imported, it won't be re-entered.
    if (OptionalFileEntryRef FE = SourceMgr.getFileEntryRefForID(MainFileID))
      markIncluded(*FE);
  }

  // Preprocess Predefines to populate the initial preprocessor state.
  std::unique_ptr<llvm::MemoryBuffer> SB =
    llvm::MemoryBuffer::getMemBufferCopy(Predefines, "<built-in>");
  assert(SB && "Cannot create predefined source buffer");
  FileID FID = SourceMgr.createFileID(std::move(SB));
  assert(FID.isValid() && "Could not create FileID for predefines?");
  setPredefinesFileID(FID);

  // Start parsing the predefines.
  EnterSourceFile(FID, nullptr, SourceLocation());

  if (!PPOpts->PCHThroughHeader.empty()) {
    // Lookup and save the FileID for the through header. If it isn't found
    // in the search path, it's a fatal error.
    OptionalFileEntryRef File = LookupFile(
        SourceLocation(), PPOpts->PCHThroughHeader,
        /*isAngled=*/false, /*FromDir=*/nullptr, /*FromFile=*/nullptr,
        /*CurDir=*/nullptr, /*SearchPath=*/nullptr, /*RelativePath=*/nullptr,
        /*SuggestedModule=*/nullptr, /*IsMapped=*/nullptr,
        /*IsFrameworkFound=*/nullptr);
    if (!File) {
      Diag(SourceLocation(), diag::err_pp_through_header_not_found)
          << PPOpts->PCHThroughHeader;
      return;
    }
    setPCHThroughHeaderFileID(
        SourceMgr.createFileID(*File, SourceLocation(), SrcMgr::C_User));
  }

  // Skip tokens from the Predefines and if needed the main file.
  if ((usingPCHWithThroughHeader() && SkippingUntilPCHThroughHeader) ||
      (usingPCHWithPragmaHdrStop() && SkippingUntilPragmaHdrStop))
    SkipTokensWhileUsingPCH();
}

void Preprocessor::setPCHThroughHeaderFileID(FileID FID) {
  assert(PCHThroughHeaderFileID.isInvalid() &&
         "PCHThroughHeaderFileID already set!");
  PCHThroughHeaderFileID = FID;
}

bool Preprocessor::isPCHThroughHeader(const FileEntry *FE) {
  assert(PCHThroughHeaderFileID.isValid() &&
         "Invalid PCH through header FileID");
  return FE == SourceMgr.getFileEntryForID(PCHThroughHeaderFileID);
}

bool Preprocessor::creatingPCHWithThroughHeader() {
  return TUKind == TU_Prefix && !PPOpts->PCHThroughHeader.empty() &&
         PCHThroughHeaderFileID.isValid();
}

bool Preprocessor::usingPCHWithThroughHeader() {
  return TUKind != TU_Prefix && !PPOpts->PCHThroughHeader.empty() &&
         PCHThroughHeaderFileID.isValid();
}

bool Preprocessor::creatingPCHWithPragmaHdrStop() {
  return TUKind == TU_Prefix && PPOpts->PCHWithHdrStop;
}

bool Preprocessor::usingPCHWithPragmaHdrStop() {
  return TUKind != TU_Prefix && PPOpts->PCHWithHdrStop;
}

/// Skip tokens until after the #include of the through header or
/// until after a #pragma hdrstop is seen. Tokens in the predefines file
/// and the main file may be skipped. If the end of the predefines file
/// is reached, skipping continues into the main file. If the end of the
/// main file is reached, it's a fatal error.
void Preprocessor::SkipTokensWhileUsingPCH() {
  bool ReachedMainFileEOF = false;
  bool UsingPCHThroughHeader = SkippingUntilPCHThroughHeader;
  bool UsingPragmaHdrStop = SkippingUntilPragmaHdrStop;
  Token Tok;
  while (true) {
    bool InPredefines =
        (CurLexer && CurLexer->getFileID() == getPredefinesFileID());
    CurLexerCallback(*this, Tok);
    if (Tok.is(tok::eof) && !InPredefines) {
      ReachedMainFileEOF = true;
      break;
    }
    if (UsingPCHThroughHeader && !SkippingUntilPCHThroughHeader)
      break;
    if (UsingPragmaHdrStop && !SkippingUntilPragmaHdrStop)
      break;
  }
  if (ReachedMainFileEOF) {
    if (UsingPCHThroughHeader)
      Diag(SourceLocation(), diag::err_pp_through_header_not_seen)
          << PPOpts->PCHThroughHeader << 1;
    else if (!PPOpts->PCHWithHdrStopCreate)
      Diag(SourceLocation(), diag::err_pp_pragma_hdrstop_not_seen);
  }
}

void Preprocessor::replayPreambleConditionalStack() {
  // Restore the conditional stack from the preamble, if there is one.
  if (PreambleConditionalStack.isReplaying()) {
    assert(CurPPLexer &&
           "CurPPLexer is null when calling replayPreambleConditionalStack.");
    CurPPLexer->setConditionalLevels(PreambleConditionalStack.getStack());
    PreambleConditionalStack.doneReplaying();
    if (PreambleConditionalStack.reachedEOFWhileSkipping())
      SkipExcludedConditionalBlock(
          PreambleConditionalStack.SkipInfo->HashTokenLoc,
          PreambleConditionalStack.SkipInfo->IfTokenLoc,
          PreambleConditionalStack.SkipInfo->FoundNonSkipPortion,
          PreambleConditionalStack.SkipInfo->FoundElse,
          PreambleConditionalStack.SkipInfo->ElseLoc);
  }
}

void Preprocessor::EndSourceFile() {
  // Notify the client that we reached the end of the source file.
  if (Callbacks)
    Callbacks->EndOfMainFile();
}

//===----------------------------------------------------------------------===//
// Lexer Event Handling.
//===----------------------------------------------------------------------===//

/// LookUpIdentifierInfo - Given a tok::raw_identifier token, look up the
/// identifier information for the token and install it into the token,
/// updating the token kind accordingly.
IdentifierInfo *Preprocessor::LookUpIdentifierInfo(Token &Identifier) const {
  assert(!Identifier.getRawIdentifier().empty() && "No raw identifier data!");

  // Look up this token, see if it is a macro, or if it is a language keyword.
  IdentifierInfo *II;
  if (!Identifier.needsCleaning() && !Identifier.hasUCN()) {
    // No cleaning needed, just use the characters from the lexed buffer.
    II = getIdentifierInfo(Identifier.getRawIdentifier());
  } else {
    // Cleaning needed, alloca a buffer, clean into it, then use the buffer.
    SmallString<64> IdentifierBuffer;
    StringRef CleanedStr = getSpelling(Identifier, IdentifierBuffer);

    if (Identifier.hasUCN()) {
      SmallString<64> UCNIdentifierBuffer;
      expandUCNs(UCNIdentifierBuffer, CleanedStr);
      II = getIdentifierInfo(UCNIdentifierBuffer);
    } else {
      II = getIdentifierInfo(CleanedStr);
    }
  }

  // Update the token info (identifier info and appropriate token kind).
  // FIXME: the raw_identifier may contain leading whitespace which is removed
  // from the cleaned identifier token. The SourceLocation should be updated to
  // refer to the non-whitespace character. For instance, the text "\\\nB" (a
  // line continuation before 'B') is parsed as a single tok::raw_identifier and
  // is cleaned to tok::identifier "B". After cleaning the token's length is
  // still 3 and the SourceLocation refers to the location of the backslash.
  Identifier.setIdentifierInfo(II);
  Identifier.setKind(II->getTokenID());

  return II;
}

void Preprocessor::SetPoisonReason(IdentifierInfo *II, unsigned DiagID) {
  PoisonReasons[II] = DiagID;
}

void Preprocessor::PoisonSEHIdentifiers(bool Poison) {
  assert(Ident__exception_code && Ident__exception_info);
  assert(Ident___exception_code && Ident___exception_info);
  Ident__exception_code->setIsPoisoned(Poison);
  Ident___exception_code->setIsPoisoned(Poison);
  Ident_GetExceptionCode->setIsPoisoned(Poison);
  Ident__exception_info->setIsPoisoned(Poison);
  Ident___exception_info->setIsPoisoned(Poison);
  Ident_GetExceptionInfo->setIsPoisoned(Poison);
  Ident__abnormal_termination->setIsPoisoned(Poison);
  Ident___abnormal_termination->setIsPoisoned(Poison);
  Ident_AbnormalTermination->setIsPoisoned(Poison);
}

void Preprocessor::HandlePoisonedIdentifier(Token & Identifier) {
  assert(Identifier.getIdentifierInfo() &&
         "Can't handle identifiers without identifier info!");
  llvm::DenseMap<IdentifierInfo*,unsigned>::const_iterator it =
    PoisonReasons.find(Identifier.getIdentifierInfo());
  if(it == PoisonReasons.end())
    Diag(Identifier, diag::err_pp_used_poisoned_id);
  else
    Diag(Identifier,it->second) << Identifier.getIdentifierInfo();
}

void Preprocessor::updateOutOfDateIdentifier(const IdentifierInfo &II) const {
  assert(II.isOutOfDate() && "not out of date");
  getExternalSource()->updateOutOfDateIdentifier(II);
}

/// HandleIdentifier - This callback is invoked when the lexer reads an
/// identifier.  This callback looks up the identifier in the map and/or
/// potentially macro expands it or turns it into a named token (like 'for').
///
/// Note that callers of this method are guarded by checking the
/// IdentifierInfo's 'isHandleIdentifierCase' bit.  If this method changes, the
/// IdentifierInfo methods that compute these properties will need to change to
/// match.
bool Preprocessor::HandleIdentifier(Token &Identifier) {
  assert(Identifier.getIdentifierInfo() &&
         "Can't handle identifiers without identifier info!");

  IdentifierInfo &II = *Identifier.getIdentifierInfo();

  // If the information about this identifier is out of date, update it from
  // the external source.
  // We have to treat __VA_ARGS__ in a special way, since it gets
  // serialized with isPoisoned = true, but our preprocessor may have
  // unpoisoned it if we're defining a C99 macro.
  if (II.isOutOfDate()) {
    bool CurrentIsPoisoned = false;
    const bool IsSpecialVariadicMacro =
        &II == Ident__VA_ARGS__ || &II == Ident__VA_OPT__;
    if (IsSpecialVariadicMacro)
      CurrentIsPoisoned = II.isPoisoned();

    updateOutOfDateIdentifier(II);
    Identifier.setKind(II.getTokenID());

    if (IsSpecialVariadicMacro)
      II.setIsPoisoned(CurrentIsPoisoned);
  }

  // If this identifier was poisoned, and if it was not produced from a macro
  // expansion, emit an error.
  if (II.isPoisoned() && CurPPLexer) {
    HandlePoisonedIdentifier(Identifier);
  }

  // If this is a macro to be expanded, do it.
  if (const MacroDefinition MD = getMacroDefinition(&II)) {
    const auto *MI = MD.getMacroInfo();
    assert(MI && "macro definition with no macro info?");
    if (!DisableMacroExpansion) {
      if (!Identifier.isExpandDisabled() && MI->isEnabled()) {
        // C99 6.10.3p10: If the preprocessing token immediately after the
        // macro name isn't a '(', this macro should not be expanded.
        if (!MI->isFunctionLike() || isNextPPTokenLParen())
          return HandleMacroExpandedIdentifier(Identifier, MD);
      } else {
        // C99 6.10.3.4p2 says that a disabled macro may never again be
        // expanded, even if it's in a context where it could be expanded in the
        // future.
        Identifier.setFlag(Token::DisableExpand);
        if (MI->isObjectLike() || isNextPPTokenLParen())
          Diag(Identifier, diag::pp_disabled_macro_expansion);
      }
    }
  }

  // If this identifier is a keyword in a newer Standard or proposed Standard,
  // produce a warning. Don't warn if we're not considering macro expansion,
  // since this identifier might be the name of a macro.
  // FIXME: This warning is disabled in cases where it shouldn't be, like
  //   "#define constexpr constexpr", "int constexpr;"
  if (II.isFutureCompatKeyword() && !DisableMacroExpansion) {
    Diag(Identifier, getIdentifierTable().getFutureCompatDiagKind(II, getLangOpts()))
        << II.getName();
    // Don't diagnose this keyword again in this translation unit.
    II.setIsFutureCompatKeyword(false);
  }

  // If this is an extension token, diagnose its use.
  // We avoid diagnosing tokens that originate from macro definitions.
  // FIXME: This warning is disabled in cases where it shouldn't be,
  // like "#define TY typeof", "TY(1) x".
  if (II.isExtensionToken() && !DisableMacroExpansion)
    Diag(Identifier, diag::ext_token_used);

  // If this is the 'import' contextual keyword following an '@', note
  // that the next token indicates a module name.
  //
  // Note that we do not treat 'import' as a contextual
  // keyword when we're in a caching lexer, because caching lexers only get
  // used in contexts where import declarations are disallowed.
  //
  // Likewise if this is the standard C++ import keyword.
  if (((LastTokenWasAt && II.isModulesImport()) ||
       Identifier.is(tok::kw_import)) &&
      !InMacroArgs && !DisableMacroExpansion &&
      (getLangOpts().Modules || getLangOpts().DebuggerSupport) &&
      CurLexerCallback != CLK_CachingLexer) {
    ModuleImportLoc = Identifier.getLocation();
    NamedModuleImportPath.clear();
    IsAtImport = true;
    ModuleImportExpectsIdentifier = true;
    CurLexerCallback = CLK_LexAfterModuleImport;
  }
  return true;
}

void Preprocessor::Lex(Token &Result) {
  ++LexLevel;

  // We loop here until a lex function returns a token; this avoids recursion.
  while (!CurLexerCallback(*this, Result))
    ;

  if (Result.is(tok::unknown) && TheModuleLoader.HadFatalFailure)
    return;

  if (Result.is(tok::code_completion) && Result.getIdentifierInfo()) {
    // Remember the identifier before code completion token.
    setCodeCompletionIdentifierInfo(Result.getIdentifierInfo());
    setCodeCompletionTokenRange(Result.getLocation(), Result.getEndLoc());
    // Set IdenfitierInfo to null to avoid confusing code that handles both
    // identifiers and completion tokens.
    Result.setIdentifierInfo(nullptr);
  }

  // Update StdCXXImportSeqState to track our position within a C++20 import-seq
  // if this token is being produced as a result of phase 4 of translation.
  // Update TrackGMFState to decide if we are currently in a Global Module
  // Fragment. GMF state updates should precede StdCXXImportSeq ones, since GMF state
  // depends on the prevailing StdCXXImportSeq state in two cases.
  if (getLangOpts().CPlusPlusModules && LexLevel == 1 &&
      !Result.getFlag(Token::IsReinjected)) {
    switch (Result.getKind()) {
    case tok::l_paren: case tok::l_square: case tok::l_brace:
      StdCXXImportSeqState.handleOpenBracket();
      break;
    case tok::r_paren: case tok::r_square:
      StdCXXImportSeqState.handleCloseBracket();
      break;
    case tok::r_brace:
      StdCXXImportSeqState.handleCloseBrace();
      break;
    // This token is injected to represent the translation of '#include "a.h"'
    // into "import a.h;". Mimic the notional ';'.
    case tok::annot_module_include:
    case tok::semi:
      TrackGMFState.handleSemi();
      StdCXXImportSeqState.handleSemi();
      ModuleDeclState.handleSemi();
      break;
    case tok::header_name:
    case tok::annot_header_unit:
      StdCXXImportSeqState.handleHeaderName();
      break;
    case tok::kw_export:
      TrackGMFState.handleExport();
      StdCXXImportSeqState.handleExport();
      ModuleDeclState.handleExport();
      break;
    case tok::colon:
      ModuleDeclState.handleColon();
      break;
    case tok::period:
      ModuleDeclState.handlePeriod();
      break;
    case tok::identifier:
      // Check "import" and "module" when there is no open bracket. The two
      // identifiers are not meaningful with open brackets.
      if (StdCXXImportSeqState.atTopLevel()) {
        if (Result.getIdentifierInfo()->isModulesImport()) {
          TrackGMFState.handleImport(StdCXXImportSeqState.afterTopLevelSeq());
          StdCXXImportSeqState.handleImport();
          if (StdCXXImportSeqState.afterImportSeq()) {
            ModuleImportLoc = Result.getLocation();
            NamedModuleImportPath.clear();
            IsAtImport = false;
            ModuleImportExpectsIdentifier = true;
            CurLexerCallback = CLK_LexAfterModuleImport;
          }
          break;
        } else if (Result.getIdentifierInfo() == getIdentifierInfo("module")) {
          TrackGMFState.handleModule(StdCXXImportSeqState.afterTopLevelSeq());
          ModuleDeclState.handleModule();
          break;
        }
      }
      ModuleDeclState.handleIdentifier(Result.getIdentifierInfo());
      if (ModuleDeclState.isModuleCandidate())
        break;
      [[fallthrough]];
    default:
      TrackGMFState.handleMisc();
      StdCXXImportSeqState.handleMisc();
      ModuleDeclState.handleMisc();
      break;
    }
  }

  if (CurLexer && ++CheckPointCounter == CheckPointStepSize) {
    CheckPoints[CurLexer->getFileID()].push_back(CurLexer->BufferPtr);
    CheckPointCounter = 0;
  }

  LastTokenWasAt = Result.is(tok::at);
  --LexLevel;

  if ((LexLevel == 0 || PreprocessToken) &&
      !Result.getFlag(Token::IsReinjected)) {
    if (LexLevel == 0)
      ++TokenCount;
    if (OnToken)
      OnToken(Result);
  }
}

void Preprocessor::LexTokensUntilEOF(std::vector<Token> *Tokens) {
  while (1) {
    Token Tok;
    Lex(Tok);
    if (Tok.isOneOf(tok::unknown, tok::eof, tok::eod,
                    tok::annot_repl_input_end))
      break;
    if (Tokens != nullptr)
      Tokens->push_back(Tok);
  }
}

/// Lex a header-name token (including one formed from header-name-tokens if
/// \p AllowMacroExpansion is \c true).
///
/// \param FilenameTok Filled in with the next token. On success, this will
///        be either a header_name token. On failure, it will be whatever other
///        token was found instead.
/// \param AllowMacroExpansion If \c true, allow the header name to be formed
///        by macro expansion (concatenating tokens as necessary if the first
///        token is a '<').
/// \return \c true if we reached EOD or EOF while looking for a > token in
///         a concatenated header name and diagnosed it. \c false otherwise.
bool Preprocessor::LexHeaderName(Token &FilenameTok, bool AllowMacroExpansion) {
  // Lex using header-name tokenization rules if tokens are being lexed from
  // a file. Just grab a token normally if we're in a macro expansion.
  if (CurPPLexer)
    CurPPLexer->LexIncludeFilename(FilenameTok);
  else
    Lex(FilenameTok);

  // This could be a <foo/bar.h> file coming from a macro expansion.  In this
  // case, glue the tokens together into an angle_string_literal token.
  SmallString<128> FilenameBuffer;
  if (FilenameTok.is(tok::less) && AllowMacroExpansion) {
    bool StartOfLine = FilenameTok.isAtStartOfLine();
    bool LeadingSpace = FilenameTok.hasLeadingSpace();
    bool LeadingEmptyMacro = FilenameTok.hasLeadingEmptyMacro();

    SourceLocation Start = FilenameTok.getLocation();
    SourceLocation End;
    FilenameBuffer.push_back('<');

    // Consume tokens until we find a '>'.
    // FIXME: A header-name could be formed starting or ending with an
    // alternative token. It's not clear whether that's ill-formed in all
    // cases.
    while (FilenameTok.isNot(tok::greater)) {
      Lex(FilenameTok);
      if (FilenameTok.isOneOf(tok::eod, tok::eof)) {
        Diag(FilenameTok.getLocation(), diag::err_expected) << tok::greater;
        Diag(Start, diag::note_matching) << tok::less;
        return true;
      }

      End = FilenameTok.getLocation();

      // FIXME: Provide code completion for #includes.
      if (FilenameTok.is(tok::code_completion)) {
        setCodeCompletionReached();
        Lex(FilenameTok);
        continue;
      }

      // Append the spelling of this token to the buffer. If there was a space
      // before it, add it now.
      if (FilenameTok.hasLeadingSpace())
        FilenameBuffer.push_back(' ');

      // Get the spelling of the token, directly into FilenameBuffer if
      // possible.
      size_t PreAppendSize = FilenameBuffer.size();
      FilenameBuffer.resize(PreAppendSize + FilenameTok.getLength());

      const char *BufPtr = &FilenameBuffer[PreAppendSize];
      unsigned ActualLen = getSpelling(FilenameTok, BufPtr);

      // If the token was spelled somewhere else, copy it into FilenameBuffer.
      if (BufPtr != &FilenameBuffer[PreAppendSize])
        memcpy(&FilenameBuffer[PreAppendSize], BufPtr, ActualLen);

      // Resize FilenameBuffer to the correct size.
      if (FilenameTok.getLength() != ActualLen)
        FilenameBuffer.resize(PreAppendSize + ActualLen);
    }

    FilenameTok.startToken();
    FilenameTok.setKind(tok::header_name);
    FilenameTok.setFlagValue(Token::StartOfLine, StartOfLine);
    FilenameTok.setFlagValue(Token::LeadingSpace, LeadingSpace);
    FilenameTok.setFlagValue(Token::LeadingEmptyMacro, LeadingEmptyMacro);
    CreateString(FilenameBuffer, FilenameTok, Start, End);
  } else if (FilenameTok.is(tok::string_literal) && AllowMacroExpansion) {
    // Convert a string-literal token of the form " h-char-sequence "
    // (produced by macro expansion) into a header-name token.
    //
    // The rules for header-names don't quite match the rules for
    // string-literals, but all the places where they differ result in
    // undefined behavior, so we can and do treat them the same.
    //
    // A string-literal with a prefix or suffix is not translated into a
    // header-name. This could theoretically be observable via the C++20
    // context-sensitive header-name formation rules.
    StringRef Str = getSpelling(FilenameTok, FilenameBuffer);
    if (Str.size() >= 2 && Str.front() == '"' && Str.back() == '"')
      FilenameTok.setKind(tok::header_name);
  }

  return false;
}

/// Collect the tokens of a C++20 pp-import-suffix.
void Preprocessor::CollectPpImportSuffix(SmallVectorImpl<Token> &Toks) {
  // FIXME: For error recovery, consider recognizing attribute syntax here
  // and terminating / diagnosing a missing semicolon if we find anything
  // else? (Can we leave that to the parser?)
  unsigned BracketDepth = 0;
  while (true) {
    Toks.emplace_back();
    Lex(Toks.back());

    switch (Toks.back().getKind()) {
    case tok::l_paren: case tok::l_square: case tok::l_brace:
      ++BracketDepth;
      break;

    case tok::r_paren: case tok::r_square: case tok::r_brace:
      if (BracketDepth == 0)
        return;
      --BracketDepth;
      break;

    case tok::semi:
      if (BracketDepth == 0)
        return;
    break;

    case tok::eof:
      return;

    default:
      break;
    }
  }
}


/// Lex a token following the 'import' contextual keyword.
///
///     pp-import: [C++20]
///           import header-name pp-import-suffix[opt] ;
///           import header-name-tokens pp-import-suffix[opt] ;
/// [ObjC]    @ import module-name ;
/// [Clang]   import module-name ;
///
///     header-name-tokens:
///           string-literal
///           < [any sequence of preprocessing-tokens other than >] >
///
///     module-name:
///           module-name-qualifier[opt] identifier
///
///     module-name-qualifier
///           module-name-qualifier[opt] identifier .
///
/// We respond to a pp-import by importing macros from the named module.
bool Preprocessor::LexAfterModuleImport(Token &Result) {
  // Figure out what kind of lexer we actually have.
  recomputeCurLexerKind();

  // Lex the next token. The header-name lexing rules are used at the start of
  // a pp-import.
  //
  // For now, we only support header-name imports in C++20 mode.
  // FIXME: Should we allow this in all language modes that support an import
  // declaration as an extension?
  if (NamedModuleImportPath.empty() && getLangOpts().CPlusPlusModules) {
    if (LexHeaderName(Result))
      return true;

    if (Result.is(tok::colon) && ModuleDeclState.isNamedModule()) {
      std::string Name = ModuleDeclState.getPrimaryName().str();
      Name += ":";
      NamedModuleImportPath.push_back(
          {getIdentifierInfo(Name), Result.getLocation()});
      CurLexerCallback = CLK_LexAfterModuleImport;
      return true;
    }
  } else {
    Lex(Result);
  }

  // Allocate a holding buffer for a sequence of tokens and introduce it into
  // the token stream.
  auto EnterTokens = [this](ArrayRef<Token> Toks) {
    auto ToksCopy = std::make_unique<Token[]>(Toks.size());
    std::copy(Toks.begin(), Toks.end(), ToksCopy.get());
    EnterTokenStream(std::move(ToksCopy), Toks.size(),
                     /*DisableMacroExpansion*/ true, /*IsReinject*/ false);
  };

  bool ImportingHeader = Result.is(tok::header_name);
  // Check for a header-name.
  SmallVector<Token, 32> Suffix;
  if (ImportingHeader) {
    // Enter the header-name token into the token stream; a Lex action cannot
    // both return a token and cache tokens (doing so would corrupt the token
    // cache if the call to Lex comes from CachingLex / PeekAhead).
    Suffix.push_back(Result);

    // Consume the pp-import-suffix and expand any macros in it now. We'll add
    // it back into the token stream later.
    CollectPpImportSuffix(Suffix);
    if (Suffix.back().isNot(tok::semi)) {
      // This is not a pp-import after all.
      EnterTokens(Suffix);
      return false;
    }

    // C++2a [cpp.module]p1:
    //   The ';' preprocessing-token terminating a pp-import shall not have
    //   been produced by macro replacement.
    SourceLocation SemiLoc = Suffix.back().getLocation();
    if (SemiLoc.isMacroID())
      Diag(SemiLoc, diag::err_header_import_semi_in_macro);

    // Reconstitute the import token.
    Token ImportTok;
    ImportTok.startToken();
    ImportTok.setKind(tok::kw_import);
    ImportTok.setLocation(ModuleImportLoc);
    ImportTok.setIdentifierInfo(getIdentifierInfo("import"));
    ImportTok.setLength(6);

    auto Action = HandleHeaderIncludeOrImport(
        /*HashLoc*/ SourceLocation(), ImportTok, Suffix.front(), SemiLoc);
    switch (Action.Kind) {
    case ImportAction::None:
      break;

    case ImportAction::ModuleBegin:
      // Let the parser know we're textually entering the module.
      Suffix.emplace_back();
      Suffix.back().startToken();
      Suffix.back().setKind(tok::annot_module_begin);
      Suffix.back().setLocation(SemiLoc);
      Suffix.back().setAnnotationEndLoc(SemiLoc);
      Suffix.back().setAnnotationValue(Action.ModuleForHeader);
      [[fallthrough]];

    case ImportAction::ModuleImport:
    case ImportAction::HeaderUnitImport:
    case ImportAction::SkippedModuleImport:
      // We chose to import (or textually enter) the file. Convert the
      // header-name token into a header unit annotation token.
      Suffix[0].setKind(tok::annot_header_unit);
      Suffix[0].setAnnotationEndLoc(Suffix[0].getLocation());
      Suffix[0].setAnnotationValue(Action.ModuleForHeader);
      // FIXME: Call the moduleImport callback?
      break;
    case ImportAction::Failure:
      assert(TheModuleLoader.HadFatalFailure &&
             "This should be an early exit only to a fatal error");
      Result.setKind(tok::eof);
      CurLexer->cutOffLexing();
      EnterTokens(Suffix);
      return true;
    }

    EnterTokens(Suffix);
    return false;
  }

  // The token sequence
  //
  //   import identifier (. identifier)*
  //
  // indicates a module import directive. We already saw the 'import'
  // contextual keyword, so now we're looking for the identifiers.
  if (ModuleImportExpectsIdentifier && Result.getKind() == tok::identifier) {
    // We expected to see an identifier here, and we did; continue handling
    // identifiers.
    NamedModuleImportPath.push_back(
        std::make_pair(Result.getIdentifierInfo(), Result.getLocation()));
    ModuleImportExpectsIdentifier = false;
    CurLexerCallback = CLK_LexAfterModuleImport;
    return true;
  }

  // If we're expecting a '.' or a ';', and we got a '.', then wait until we
  // see the next identifier. (We can also see a '[[' that begins an
  // attribute-specifier-seq here under the Standard C++ Modules.)
  if (!ModuleImportExpectsIdentifier && Result.getKind() == tok::period) {
    ModuleImportExpectsIdentifier = true;
    CurLexerCallback = CLK_LexAfterModuleImport;
    return true;
  }

  // If we didn't recognize a module name at all, this is not a (valid) import.
  if (NamedModuleImportPath.empty() || Result.is(tok::eof))
    return true;

  // Consume the pp-import-suffix and expand any macros in it now, if we're not
  // at the semicolon already.
  SourceLocation SemiLoc = Result.getLocation();
  if (Result.isNot(tok::semi)) {
    Suffix.push_back(Result);
    CollectPpImportSuffix(Suffix);
    if (Suffix.back().isNot(tok::semi)) {
      // This is not an import after all.
      EnterTokens(Suffix);
      return false;
    }
    SemiLoc = Suffix.back().getLocation();
  }

  // Under the standard C++ Modules, the dot is just part of the module name,
  // and not a real hierarchy separator. Flatten such module names now.
  //
  // FIXME: Is this the right level to be performing this transformation?
  std::string FlatModuleName;
  if (getLangOpts().CPlusPlusModules) {
    for (auto &Piece : NamedModuleImportPath) {
      // If the FlatModuleName ends with colon, it implies it is a partition.
      if (!FlatModuleName.empty() && FlatModuleName.back() != ':')
        FlatModuleName += ".";
      FlatModuleName += Piece.first->getName();
    }
    SourceLocation FirstPathLoc = NamedModuleImportPath[0].second;
    NamedModuleImportPath.clear();
    NamedModuleImportPath.push_back(
        std::make_pair(getIdentifierInfo(FlatModuleName), FirstPathLoc));
  }

  Module *Imported = nullptr;
  // We don't/shouldn't load the standard c++20 modules when preprocessing.
  if (getLangOpts().Modules && !isInImportingCXXNamedModules()) {
    Imported = TheModuleLoader.loadModule(ModuleImportLoc,
                                          NamedModuleImportPath,
                                          Module::Hidden,
                                          /*IsInclusionDirective=*/false);
    if (Imported)
      makeModuleVisible(Imported, SemiLoc);
  }

  if (Callbacks)
    Callbacks->moduleImport(ModuleImportLoc, NamedModuleImportPath, Imported);

  if (!Suffix.empty()) {
    EnterTokens(Suffix);
    return false;
  }
  return true;
}

void Preprocessor::makeModuleVisible(Module *M, SourceLocation Loc) {
  CurSubmoduleState->VisibleModules.setVisible(
      M, Loc, [](Module *) {},
      [&](ArrayRef<Module *> Path, Module *Conflict, StringRef Message) {
        // FIXME: Include the path in the diagnostic.
        // FIXME: Include the import location for the conflicting module.
        Diag(ModuleImportLoc, diag::warn_module_conflict)
            << Path[0]->getFullModuleName()
            << Conflict->getFullModuleName()
            << Message;
      });

  // Add this module to the imports list of the currently-built submodule.
  if (!BuildingSubmoduleStack.empty() && M != BuildingSubmoduleStack.back().M)
    BuildingSubmoduleStack.back().M->Imports.insert(M);
}

bool Preprocessor::FinishLexStringLiteral(Token &Result, std::string &String,
                                          const char *DiagnosticTag,
                                          bool AllowMacroExpansion) {
  // We need at least one string literal.
  if (Result.isNot(tok::string_literal)) {
    Diag(Result, diag::err_expected_string_literal)
      << /*Source='in...'*/0 << DiagnosticTag;
    return false;
  }

  // Lex string literal tokens, optionally with macro expansion.
  SmallVector<Token, 4> StrToks;
  do {
    StrToks.push_back(Result);

    if (Result.hasUDSuffix())
      Diag(Result, diag::err_invalid_string_udl);

    if (AllowMacroExpansion)
      Lex(Result);
    else
      LexUnexpandedToken(Result);
  } while (Result.is(tok::string_literal));

  // Concatenate and parse the strings.
  StringLiteralParser Literal(StrToks, *this);
  assert(Literal.isOrdinary() && "Didn't allow wide strings in");

  if (Literal.hadError)
    return false;

  if (Literal.Pascal) {
    Diag(StrToks[0].getLocation(), diag::err_expected_string_literal)
      << /*Source='in...'*/0 << DiagnosticTag;
    return false;
  }

  String = std::string(Literal.GetString());
  return true;
}

bool Preprocessor::parseSimpleIntegerLiteral(Token &Tok, uint64_t &Value) {
  assert(Tok.is(tok::numeric_constant));
  SmallString<8> IntegerBuffer;
  bool NumberInvalid = false;
  StringRef Spelling = getSpelling(Tok, IntegerBuffer, &NumberInvalid);
  if (NumberInvalid)
    return false;
  NumericLiteralParser Literal(Spelling, Tok.getLocation(), getSourceManager(),
                               getLangOpts(), getTargetInfo(),
                               getDiagnostics());
  if (Literal.hadError || !Literal.isIntegerLiteral() || Literal.hasUDSuffix())
    return false;
  llvm::APInt APVal(64, 0);
  if (Literal.GetIntegerValue(APVal))
    return false;
  Lex(Tok);
  Value = APVal.getLimitedValue();
  return true;
}

void Preprocessor::addCommentHandler(CommentHandler *Handler) {
  assert(Handler && "NULL comment handler");
  assert(!llvm::is_contained(CommentHandlers, Handler) &&
         "Comment handler already registered");
  CommentHandlers.push_back(Handler);
}

void Preprocessor::removeCommentHandler(CommentHandler *Handler) {
  std::vector<CommentHandler *>::iterator Pos =
      llvm::find(CommentHandlers, Handler);
  assert(Pos != CommentHandlers.end() && "Comment handler not registered");
  CommentHandlers.erase(Pos);
}

bool Preprocessor::HandleComment(Token &result, SourceRange Comment) {
  bool AnyPendingTokens = false;
  for (std::vector<CommentHandler *>::iterator H = CommentHandlers.begin(),
       HEnd = CommentHandlers.end();
       H != HEnd; ++H) {
    if ((*H)->HandleComment(*this, Comment))
      AnyPendingTokens = true;
  }
  if (!AnyPendingTokens || getCommentRetentionState())
    return false;
  Lex(result);
  return true;
}

void Preprocessor::emitMacroDeprecationWarning(const Token &Identifier) const {
  const MacroAnnotations &A =
      getMacroAnnotations(Identifier.getIdentifierInfo());
  assert(A.DeprecationInfo &&
         "Macro deprecation warning without recorded annotation!");
  const MacroAnnotationInfo &Info = *A.DeprecationInfo;
  if (Info.Message.empty())
    Diag(Identifier, diag::warn_pragma_deprecated_macro_use)
        << Identifier.getIdentifierInfo() << 0;
  else
    Diag(Identifier, diag::warn_pragma_deprecated_macro_use)
        << Identifier.getIdentifierInfo() << 1 << Info.Message;
  Diag(Info.Location, diag::note_pp_macro_annotation) << 0;
}

void Preprocessor::emitRestrictExpansionWarning(const Token &Identifier) const {
  const MacroAnnotations &A =
      getMacroAnnotations(Identifier.getIdentifierInfo());
  assert(A.RestrictExpansionInfo &&
         "Macro restricted expansion warning without recorded annotation!");
  const MacroAnnotationInfo &Info = *A.RestrictExpansionInfo;
  if (Info.Message.empty())
    Diag(Identifier, diag::warn_pragma_restrict_expansion_macro_use)
        << Identifier.getIdentifierInfo() << 0;
  else
    Diag(Identifier, diag::warn_pragma_restrict_expansion_macro_use)
        << Identifier.getIdentifierInfo() << 1 << Info.Message;
  Diag(Info.Location, diag::note_pp_macro_annotation) << 1;
}

void Preprocessor::emitRestrictInfNaNWarning(const Token &Identifier,
                                             unsigned DiagSelection) const {
  Diag(Identifier, diag::warn_fp_nan_inf_when_disabled) << DiagSelection << 1;
}

void Preprocessor::emitFinalMacroWarning(const Token &Identifier,
                                         bool IsUndef) const {
  const MacroAnnotations &A =
      getMacroAnnotations(Identifier.getIdentifierInfo());
  assert(A.FinalAnnotationLoc &&
         "Final macro warning without recorded annotation!");

  Diag(Identifier, diag::warn_pragma_final_macro)
      << Identifier.getIdentifierInfo() << (IsUndef ? 0 : 1);
  Diag(*A.FinalAnnotationLoc, diag::note_pp_macro_annotation) << 2;
}

bool Preprocessor::isSafeBufferOptOut(const SourceManager &SourceMgr,
                                      const SourceLocation &Loc) const {
  // The lambda that tests if a `Loc` is in an opt-out region given one opt-out
  // region map:
  auto TestInMap = [&SourceMgr](const SafeBufferOptOutRegionsTy &Map,
                                const SourceLocation &Loc) -> bool {
    // Try to find a region in `SafeBufferOptOutMap` where `Loc` is in:
    auto FirstRegionEndingAfterLoc = llvm::partition_point(
        Map, [&SourceMgr,
              &Loc](const std::pair<SourceLocation, SourceLocation> &Region) {
          return SourceMgr.isBeforeInTranslationUnit(Region.second, Loc);
        });

    if (FirstRegionEndingAfterLoc != Map.end()) {
      // To test if the start location of the found region precedes `Loc`:
      return SourceMgr.isBeforeInTranslationUnit(
          FirstRegionEndingAfterLoc->first, Loc);
    }
    // If we do not find a region whose end location passes `Loc`, we want to
    // check if the current region is still open:
    if (!Map.empty() && Map.back().first == Map.back().second)
      return SourceMgr.isBeforeInTranslationUnit(Map.back().first, Loc);
    return false;
  };

  // What the following does:
  //
  // If `Loc` belongs to the local TU, we just look up `SafeBufferOptOutMap`.
  // Otherwise, `Loc` is from a loaded AST.  We look up the
  // `LoadedSafeBufferOptOutMap` first to get the opt-out region map of the
  // loaded AST where `Loc` is at.  Then we find if `Loc` is in an opt-out
  // region w.r.t. the region map.  If the region map is absent, it means there
  // is no opt-out pragma in that loaded AST.
  //
  // Opt-out pragmas in the local TU or a loaded AST is not visible to another
  // one of them.  That means if you put the pragmas around a `#include
  // "module.h"`, where module.h is a module, it is not actually suppressing
  // warnings in module.h.  This is fine because warnings in module.h will be
  // reported when module.h is compiled in isolation and nothing in module.h
  // will be analyzed ever again.  So you will not see warnings from the file
  // that imports module.h anyway. And you can't even do the same thing for PCHs
  //  because they can only be included from the command line.

  if (SourceMgr.isLocalSourceLocation(Loc))
    return TestInMap(SafeBufferOptOutMap, Loc);

  const SafeBufferOptOutRegionsTy *LoadedRegions =
      LoadedSafeBufferOptOutMap.lookupLoadedOptOutMap(Loc, SourceMgr);

  if (LoadedRegions)
    return TestInMap(*LoadedRegions, Loc);
  return false;
}

bool Preprocessor::enterOrExitSafeBufferOptOutRegion(
    bool isEnter, const SourceLocation &Loc) {
  if (isEnter) {
    if (isPPInSafeBufferOptOutRegion())
      return true; // invalid enter action
    InSafeBufferOptOutRegion = true;
    CurrentSafeBufferOptOutStart = Loc;

    // To set the start location of a new region:

    if (!SafeBufferOptOutMap.empty()) {
      [[maybe_unused]] auto *PrevRegion = &SafeBufferOptOutMap.back();
      assert(PrevRegion->first != PrevRegion->second &&
             "Shall not begin a safe buffer opt-out region before closing the "
             "previous one.");
    }
    // If the start location equals to the end location, we call the region a
    // open region or a unclosed region (i.e., end location has not been set
    // yet).
    SafeBufferOptOutMap.emplace_back(Loc, Loc);
  } else {
    if (!isPPInSafeBufferOptOutRegion())
      return true; // invalid enter action
    InSafeBufferOptOutRegion = false;

    // To set the end location of the current open region:

    assert(!SafeBufferOptOutMap.empty() &&
           "Misordered safe buffer opt-out regions");
    auto *CurrRegion = &SafeBufferOptOutMap.back();
    assert(CurrRegion->first == CurrRegion->second &&
           "Set end location to a closed safe buffer opt-out region");
    CurrRegion->second = Loc;
  }
  return false;
}

bool Preprocessor::isPPInSafeBufferOptOutRegion() {
  return InSafeBufferOptOutRegion;
}
bool Preprocessor::isPPInSafeBufferOptOutRegion(SourceLocation &StartLoc) {
  StartLoc = CurrentSafeBufferOptOutStart;
  return InSafeBufferOptOutRegion;
}

SmallVector<SourceLocation, 64>
Preprocessor::serializeSafeBufferOptOutMap() const {
  assert(!InSafeBufferOptOutRegion &&
         "Attempt to serialize safe buffer opt-out regions before file being "
         "completely preprocessed");

  SmallVector<SourceLocation, 64> SrcSeq;

  for (const auto &[begin, end] : SafeBufferOptOutMap) {
    SrcSeq.push_back(begin);
    SrcSeq.push_back(end);
  }
  // Only `SafeBufferOptOutMap` gets serialized. No need to serialize
  // `LoadedSafeBufferOptOutMap` because if this TU loads a pch/module, every
  // pch/module in the pch-chain/module-DAG will be loaded one by one in order.
  // It means that for each loading pch/module m, it just needs to load m's own
  // `SafeBufferOptOutMap`.
  return SrcSeq;
}

bool Preprocessor::setDeserializedSafeBufferOptOutMap(
    const SmallVectorImpl<SourceLocation> &SourceLocations) {
  if (SourceLocations.size() == 0)
    return false;

  assert(SourceLocations.size() % 2 == 0 &&
         "ill-formed SourceLocation sequence");

  auto It = SourceLocations.begin();
  SafeBufferOptOutRegionsTy &Regions =
      LoadedSafeBufferOptOutMap.findAndConsLoadedOptOutMap(*It, SourceMgr);

  do {
    SourceLocation Begin = *It++;
    SourceLocation End = *It++;

    Regions.emplace_back(Begin, End);
  } while (It != SourceLocations.end());
  return true;
}

ModuleLoader::~ModuleLoader() = default;

CommentHandler::~CommentHandler() = default;

EmptylineHandler::~EmptylineHandler() = default;

CodeCompletionHandler::~CodeCompletionHandler() = default;

void Preprocessor::createPreprocessingRecord() {
  if (Record)
    return;

  Record = new PreprocessingRecord(getSourceManager());
  addPPCallbacks(std::unique_ptr<PPCallbacks>(Record));
}

const char *Preprocessor::getCheckPoint(FileID FID, const char *Start) const {
  if (auto It = CheckPoints.find(FID); It != CheckPoints.end()) {
    const SmallVector<const char *> &FileCheckPoints = It->second;
    const char *Last = nullptr;
    // FIXME: Do better than a linear search.
    for (const char *P : FileCheckPoints) {
      if (P > Start)
        break;
      Last = P;
    }
    return Last;
  }

  return nullptr;
}
