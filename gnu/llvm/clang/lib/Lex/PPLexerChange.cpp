//===--- PPLexerChange.cpp - Handle changing lexers in the preprocessor ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements pieces of the Preprocessor interface that manage the
// current lexer stack.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace clang;

//===----------------------------------------------------------------------===//
// Miscellaneous Methods.
//===----------------------------------------------------------------------===//

/// isInPrimaryFile - Return true if we're in the top-level file, not in a
/// \#include.  This looks through macro expansions and active _Pragma lexers.
bool Preprocessor::isInPrimaryFile() const {
  if (IsFileLexer())
    return IncludeMacroStack.empty();

  // If there are any stacked lexers, we're in a #include.
  assert(IsFileLexer(IncludeMacroStack[0]) &&
         "Top level include stack isn't our primary lexer?");
  return llvm::none_of(
      llvm::drop_begin(IncludeMacroStack),
      [&](const IncludeStackInfo &ISI) -> bool { return IsFileLexer(ISI); });
}

/// getCurrentLexer - Return the current file lexer being lexed from.  Note
/// that this ignores any potentially active macro expansions and _Pragma
/// expansions going on at the time.
PreprocessorLexer *Preprocessor::getCurrentFileLexer() const {
  if (IsFileLexer())
    return CurPPLexer;

  // Look for a stacked lexer.
  for (const IncludeStackInfo &ISI : llvm::reverse(IncludeMacroStack)) {
    if (IsFileLexer(ISI))
      return ISI.ThePPLexer;
  }
  return nullptr;
}


//===----------------------------------------------------------------------===//
// Methods for Entering and Callbacks for leaving various contexts
//===----------------------------------------------------------------------===//

/// EnterSourceFile - Add a source file to the top of the include stack and
/// start lexing tokens from it instead of the current buffer.
bool Preprocessor::EnterSourceFile(FileID FID, ConstSearchDirIterator CurDir,
                                   SourceLocation Loc,
                                   bool IsFirstIncludeOfFile) {
  assert(!CurTokenLexer && "Cannot #include a file inside a macro!");
  ++NumEnteredSourceFiles;

  if (MaxIncludeStackDepth < IncludeMacroStack.size())
    MaxIncludeStackDepth = IncludeMacroStack.size();

  // Get the MemoryBuffer for this FID, if it fails, we fail.
  std::optional<llvm::MemoryBufferRef> InputFile =
      getSourceManager().getBufferOrNone(FID, Loc);
  if (!InputFile) {
    SourceLocation FileStart = SourceMgr.getLocForStartOfFile(FID);
    Diag(Loc, diag::err_pp_error_opening_file)
        << std::string(SourceMgr.getBufferName(FileStart)) << "";
    return true;
  }

  if (isCodeCompletionEnabled() &&
      SourceMgr.getFileEntryForID(FID) == CodeCompletionFile) {
    CodeCompletionFileLoc = SourceMgr.getLocForStartOfFile(FID);
    CodeCompletionLoc =
        CodeCompletionFileLoc.getLocWithOffset(CodeCompletionOffset);
  }

  Lexer *TheLexer = new Lexer(FID, *InputFile, *this, IsFirstIncludeOfFile);
  if (getPreprocessorOpts().DependencyDirectivesForFile &&
      FID != PredefinesFileID) {
    if (OptionalFileEntryRef File = SourceMgr.getFileEntryRefForID(FID)) {
      if (std::optional<ArrayRef<dependency_directives_scan::Directive>>
              DepDirectives =
                  getPreprocessorOpts().DependencyDirectivesForFile(*File)) {
        TheLexer->DepDirectives = *DepDirectives;
      }
    }
  }

  EnterSourceFileWithLexer(TheLexer, CurDir);
  return false;
}

/// EnterSourceFileWithLexer - Add a source file to the top of the include stack
///  and start lexing tokens from it instead of the current buffer.
void Preprocessor::EnterSourceFileWithLexer(Lexer *TheLexer,
                                            ConstSearchDirIterator CurDir) {
  PreprocessorLexer *PrevPPLexer = CurPPLexer;

  // Add the current lexer to the include stack.
  if (CurPPLexer || CurTokenLexer)
    PushIncludeMacroStack();

  CurLexer.reset(TheLexer);
  CurPPLexer = TheLexer;
  CurDirLookup = CurDir;
  CurLexerSubmodule = nullptr;
  if (CurLexerCallback != CLK_LexAfterModuleImport)
    CurLexerCallback = TheLexer->isDependencyDirectivesLexer()
                           ? CLK_DependencyDirectivesLexer
                           : CLK_Lexer;

  // Notify the client, if desired, that we are in a new source file.
  if (Callbacks && !CurLexer->Is_PragmaLexer) {
    SrcMgr::CharacteristicKind FileType =
       SourceMgr.getFileCharacteristic(CurLexer->getFileLoc());

    FileID PrevFID;
    SourceLocation EnterLoc;
    if (PrevPPLexer) {
      PrevFID = PrevPPLexer->getFileID();
      EnterLoc = PrevPPLexer->getSourceLocation();
    }
    Callbacks->FileChanged(CurLexer->getFileLoc(), PPCallbacks::EnterFile,
                           FileType, PrevFID);
    Callbacks->LexedFileChanged(CurLexer->getFileID(),
                                PPCallbacks::LexedFileChangeReason::EnterFile,
                                FileType, PrevFID, EnterLoc);
  }
}

/// EnterMacro - Add a Macro to the top of the include stack and start lexing
/// tokens from it instead of the current buffer.
void Preprocessor::EnterMacro(Token &Tok, SourceLocation ILEnd,
                              MacroInfo *Macro, MacroArgs *Args) {
  std::unique_ptr<TokenLexer> TokLexer;
  if (NumCachedTokenLexers == 0) {
    TokLexer = std::make_unique<TokenLexer>(Tok, ILEnd, Macro, Args, *this);
  } else {
    TokLexer = std::move(TokenLexerCache[--NumCachedTokenLexers]);
    TokLexer->Init(Tok, ILEnd, Macro, Args);
  }

  PushIncludeMacroStack();
  CurDirLookup = nullptr;
  CurTokenLexer = std::move(TokLexer);
  if (CurLexerCallback != CLK_LexAfterModuleImport)
    CurLexerCallback = CLK_TokenLexer;
}

/// EnterTokenStream - Add a "macro" context to the top of the include stack,
/// which will cause the lexer to start returning the specified tokens.
///
/// If DisableMacroExpansion is true, tokens lexed from the token stream will
/// not be subject to further macro expansion.  Otherwise, these tokens will
/// be re-macro-expanded when/if expansion is enabled.
///
/// If OwnsTokens is false, this method assumes that the specified stream of
/// tokens has a permanent owner somewhere, so they do not need to be copied.
/// If it is true, it assumes the array of tokens is allocated with new[] and
/// must be freed.
///
void Preprocessor::EnterTokenStream(const Token *Toks, unsigned NumToks,
                                    bool DisableMacroExpansion, bool OwnsTokens,
                                    bool IsReinject) {
  if (CurLexerCallback == CLK_CachingLexer) {
    if (CachedLexPos < CachedTokens.size()) {
      assert(IsReinject && "new tokens in the middle of cached stream");
      // We're entering tokens into the middle of our cached token stream. We
      // can't represent that, so just insert the tokens into the buffer.
      CachedTokens.insert(CachedTokens.begin() + CachedLexPos,
                          Toks, Toks + NumToks);
      if (OwnsTokens)
        delete [] Toks;
      return;
    }

    // New tokens are at the end of the cached token sequnece; insert the
    // token stream underneath the caching lexer.
    ExitCachingLexMode();
    EnterTokenStream(Toks, NumToks, DisableMacroExpansion, OwnsTokens,
                     IsReinject);
    EnterCachingLexMode();
    return;
  }

  // Create a macro expander to expand from the specified token stream.
  std::unique_ptr<TokenLexer> TokLexer;
  if (NumCachedTokenLexers == 0) {
    TokLexer = std::make_unique<TokenLexer>(
        Toks, NumToks, DisableMacroExpansion, OwnsTokens, IsReinject, *this);
  } else {
    TokLexer = std::move(TokenLexerCache[--NumCachedTokenLexers]);
    TokLexer->Init(Toks, NumToks, DisableMacroExpansion, OwnsTokens,
                   IsReinject);
  }

  // Save our current state.
  PushIncludeMacroStack();
  CurDirLookup = nullptr;
  CurTokenLexer = std::move(TokLexer);
  if (CurLexerCallback != CLK_LexAfterModuleImport)
    CurLexerCallback = CLK_TokenLexer;
}

/// Compute the relative path that names the given file relative to
/// the given directory.
static void computeRelativePath(FileManager &FM, const DirectoryEntry *Dir,
                                FileEntryRef File, SmallString<128> &Result) {
  Result.clear();

  StringRef FilePath = File.getDir().getName();
  StringRef Path = FilePath;
  while (!Path.empty()) {
    if (auto CurDir = FM.getDirectory(Path)) {
      if (*CurDir == Dir) {
        Result = FilePath.substr(Path.size());
        llvm::sys::path::append(Result,
                                llvm::sys::path::filename(File.getName()));
        return;
      }
    }

    Path = llvm::sys::path::parent_path(Path);
  }

  Result = File.getName();
}

void Preprocessor::PropagateLineStartLeadingSpaceInfo(Token &Result) {
  if (CurTokenLexer) {
    CurTokenLexer->PropagateLineStartLeadingSpaceInfo(Result);
    return;
  }
  if (CurLexer) {
    CurLexer->PropagateLineStartLeadingSpaceInfo(Result);
    return;
  }
  // FIXME: Handle other kinds of lexers?  It generally shouldn't matter,
  // but it might if they're empty?
}

/// Determine the location to use as the end of the buffer for a lexer.
///
/// If the file ends with a newline, form the EOF token on the newline itself,
/// rather than "on the line following it", which doesn't exist.  This makes
/// diagnostics relating to the end of file include the last file that the user
/// actually typed, which is goodness.
const char *Preprocessor::getCurLexerEndPos() {
  const char *EndPos = CurLexer->BufferEnd;
  if (EndPos != CurLexer->BufferStart &&
      (EndPos[-1] == '\n' || EndPos[-1] == '\r')) {
    --EndPos;

    // Handle \n\r and \r\n:
    if (EndPos != CurLexer->BufferStart &&
        (EndPos[-1] == '\n' || EndPos[-1] == '\r') &&
        EndPos[-1] != EndPos[0])
      --EndPos;
  }

  return EndPos;
}

static void collectAllSubModulesWithUmbrellaHeader(
    const Module &Mod, SmallVectorImpl<const Module *> &SubMods) {
  if (Mod.getUmbrellaHeaderAsWritten())
    SubMods.push_back(&Mod);
  for (auto *M : Mod.submodules())
    collectAllSubModulesWithUmbrellaHeader(*M, SubMods);
}

void Preprocessor::diagnoseMissingHeaderInUmbrellaDir(const Module &Mod) {
  std::optional<Module::Header> UmbrellaHeader =
      Mod.getUmbrellaHeaderAsWritten();
  assert(UmbrellaHeader && "Module must use umbrella header");
  const FileID &File = SourceMgr.translateFile(UmbrellaHeader->Entry);
  SourceLocation ExpectedHeadersLoc = SourceMgr.getLocForEndOfFile(File);
  if (getDiagnostics().isIgnored(diag::warn_uncovered_module_header,
                                 ExpectedHeadersLoc))
    return;

  ModuleMap &ModMap = getHeaderSearchInfo().getModuleMap();
  OptionalDirectoryEntryRef Dir = Mod.getEffectiveUmbrellaDir();
  llvm::vfs::FileSystem &FS = FileMgr.getVirtualFileSystem();
  std::error_code EC;
  for (llvm::vfs::recursive_directory_iterator Entry(FS, Dir->getName(), EC),
       End;
       Entry != End && !EC; Entry.increment(EC)) {
    using llvm::StringSwitch;

    // Check whether this entry has an extension typically associated with
    // headers.
    if (!StringSwitch<bool>(llvm::sys::path::extension(Entry->path()))
             .Cases(".h", ".H", ".hh", ".hpp", true)
             .Default(false))
      continue;

    if (auto Header = getFileManager().getOptionalFileRef(Entry->path()))
      if (!getSourceManager().hasFileInfo(*Header)) {
        if (!ModMap.isHeaderInUnavailableModule(*Header)) {
          // Find the relative path that would access this header.
          SmallString<128> RelativePath;
          computeRelativePath(FileMgr, *Dir, *Header, RelativePath);
          Diag(ExpectedHeadersLoc, diag::warn_uncovered_module_header)
              << Mod.getFullModuleName() << RelativePath;
        }
      }
  }
}

/// HandleEndOfFile - This callback is invoked when the lexer hits the end of
/// the current file.  This either returns the EOF token or pops a level off
/// the include stack and keeps going.
bool Preprocessor::HandleEndOfFile(Token &Result, bool isEndOfMacro) {
  assert(!CurTokenLexer &&
         "Ending a file when currently in a macro!");

  SourceLocation UnclosedSafeBufferOptOutLoc;

  if (IncludeMacroStack.empty() &&
      isPPInSafeBufferOptOutRegion(UnclosedSafeBufferOptOutLoc)) {
    // To warn if a "-Wunsafe-buffer-usage" opt-out region is still open by the
    // end of a file.
    Diag(UnclosedSafeBufferOptOutLoc,
         diag::err_pp_unclosed_pragma_unsafe_buffer_usage);
  }
  // If we have an unclosed module region from a pragma at the end of a
  // module, complain and close it now.
  const bool LeavingSubmodule = CurLexer && CurLexerSubmodule;
  if ((LeavingSubmodule || IncludeMacroStack.empty()) &&
      !BuildingSubmoduleStack.empty() &&
      BuildingSubmoduleStack.back().IsPragma) {
    Diag(BuildingSubmoduleStack.back().ImportLoc,
         diag::err_pp_module_begin_without_module_end);
    Module *M = LeaveSubmodule(/*ForPragma*/true);

    Result.startToken();
    const char *EndPos = getCurLexerEndPos();
    CurLexer->BufferPtr = EndPos;
    CurLexer->FormTokenWithChars(Result, EndPos, tok::annot_module_end);
    Result.setAnnotationEndLoc(Result.getLocation());
    Result.setAnnotationValue(M);
    return true;
  }

  // See if this file had a controlling macro.
  if (CurPPLexer) {  // Not ending a macro, ignore it.
    if (const IdentifierInfo *ControllingMacro =
          CurPPLexer->MIOpt.GetControllingMacroAtEndOfFile()) {
      // Okay, this has a controlling macro, remember in HeaderFileInfo.
      if (OptionalFileEntryRef FE = CurPPLexer->getFileEntry()) {
        HeaderInfo.SetFileControllingMacro(*FE, ControllingMacro);
        if (MacroInfo *MI = getMacroInfo(ControllingMacro))
          MI->setUsedForHeaderGuard(true);
        if (const IdentifierInfo *DefinedMacro =
              CurPPLexer->MIOpt.GetDefinedMacro()) {
          if (!isMacroDefined(ControllingMacro) &&
              DefinedMacro != ControllingMacro &&
              CurLexer->isFirstTimeLexingFile()) {

            // If the edit distance between the two macros is more than 50%,
            // DefinedMacro may not be header guard, or can be header guard of
            // another header file. Therefore, it maybe defining something
            // completely different. This can be observed in the wild when
            // handling feature macros or header guards in different files.

            const StringRef ControllingMacroName = ControllingMacro->getName();
            const StringRef DefinedMacroName = DefinedMacro->getName();
            const size_t MaxHalfLength = std::max(ControllingMacroName.size(),
                                                  DefinedMacroName.size()) / 2;
            const unsigned ED = ControllingMacroName.edit_distance(
                DefinedMacroName, true, MaxHalfLength);
            if (ED <= MaxHalfLength) {
              // Emit a warning for a bad header guard.
              Diag(CurPPLexer->MIOpt.GetMacroLocation(),
                   diag::warn_header_guard)
                  << CurPPLexer->MIOpt.GetMacroLocation() << ControllingMacro;
              Diag(CurPPLexer->MIOpt.GetDefinedLocation(),
                   diag::note_header_guard)
                  << CurPPLexer->MIOpt.GetDefinedLocation() << DefinedMacro
                  << ControllingMacro
                  << FixItHint::CreateReplacement(
                         CurPPLexer->MIOpt.GetDefinedLocation(),
                         ControllingMacro->getName());
            }
          }
        }
      }
    }
  }

  // Complain about reaching a true EOF within arc_cf_code_audited.
  // We don't want to complain about reaching the end of a macro
  // instantiation or a _Pragma.
  if (PragmaARCCFCodeAuditedInfo.second.isValid() && !isEndOfMacro &&
      !(CurLexer && CurLexer->Is_PragmaLexer)) {
    Diag(PragmaARCCFCodeAuditedInfo.second,
         diag::err_pp_eof_in_arc_cf_code_audited);

    // Recover by leaving immediately.
    PragmaARCCFCodeAuditedInfo = {nullptr, SourceLocation()};
  }

  // Complain about reaching a true EOF within assume_nonnull.
  // We don't want to complain about reaching the end of a macro
  // instantiation or a _Pragma.
  if (PragmaAssumeNonNullLoc.isValid() &&
      !isEndOfMacro && !(CurLexer && CurLexer->Is_PragmaLexer)) {
    // If we're at the end of generating a preamble, we should record the
    // unterminated \#pragma clang assume_nonnull so we can restore it later
    // when the preamble is loaded into the main file.
    if (isRecordingPreamble() && isInPrimaryFile())
      PreambleRecordedPragmaAssumeNonNullLoc = PragmaAssumeNonNullLoc;
    else
      Diag(PragmaAssumeNonNullLoc, diag::err_pp_eof_in_assume_nonnull);
    // Recover by leaving immediately.
    PragmaAssumeNonNullLoc = SourceLocation();
  }

  bool LeavingPCHThroughHeader = false;

  // If this is a #include'd file, pop it off the include stack and continue
  // lexing the #includer file.
  if (!IncludeMacroStack.empty()) {

    // If we lexed the code-completion file, act as if we reached EOF.
    if (isCodeCompletionEnabled() && CurPPLexer &&
        SourceMgr.getLocForStartOfFile(CurPPLexer->getFileID()) ==
            CodeCompletionFileLoc) {
      assert(CurLexer && "Got EOF but no current lexer set!");
      Result.startToken();
      CurLexer->FormTokenWithChars(Result, CurLexer->BufferEnd, tok::eof);
      CurLexer.reset();

      CurPPLexer = nullptr;
      recomputeCurLexerKind();
      return true;
    }

    if (!isEndOfMacro && CurPPLexer &&
        (SourceMgr.getIncludeLoc(CurPPLexer->getFileID()).isValid() ||
         // Predefines file doesn't have a valid include location.
         (PredefinesFileID.isValid() &&
          CurPPLexer->getFileID() == PredefinesFileID))) {
      // Notify SourceManager to record the number of FileIDs that were created
      // during lexing of the #include'd file.
      unsigned NumFIDs =
          SourceMgr.local_sloc_entry_size() -
          CurPPLexer->getInitialNumSLocEntries() + 1/*#include'd file*/;
      SourceMgr.setNumCreatedFIDsForFileID(CurPPLexer->getFileID(), NumFIDs);
    }

    bool ExitedFromPredefinesFile = false;
    FileID ExitedFID;
    if (!isEndOfMacro && CurPPLexer) {
      ExitedFID = CurPPLexer->getFileID();

      assert(PredefinesFileID.isValid() &&
             "HandleEndOfFile is called before PredefinesFileId is set");
      ExitedFromPredefinesFile = (PredefinesFileID == ExitedFID);
    }

    if (LeavingSubmodule) {
      // We're done with this submodule.
      Module *M = LeaveSubmodule(/*ForPragma*/false);

      // Notify the parser that we've left the module.
      const char *EndPos = getCurLexerEndPos();
      Result.startToken();
      CurLexer->BufferPtr = EndPos;
      CurLexer->FormTokenWithChars(Result, EndPos, tok::annot_module_end);
      Result.setAnnotationEndLoc(Result.getLocation());
      Result.setAnnotationValue(M);
    }

    bool FoundPCHThroughHeader = false;
    if (CurPPLexer && creatingPCHWithThroughHeader() &&
        isPCHThroughHeader(
            SourceMgr.getFileEntryForID(CurPPLexer->getFileID())))
      FoundPCHThroughHeader = true;

    // We're done with the #included file.
    RemoveTopOfLexerStack();

    // Propagate info about start-of-line/leading white-space/etc.
    PropagateLineStartLeadingSpaceInfo(Result);

    // Notify the client, if desired, that we are in a new source file.
    if (Callbacks && !isEndOfMacro && CurPPLexer) {
      SourceLocation Loc = CurPPLexer->getSourceLocation();
      SrcMgr::CharacteristicKind FileType =
          SourceMgr.getFileCharacteristic(Loc);
      Callbacks->FileChanged(Loc, PPCallbacks::ExitFile, FileType, ExitedFID);
      Callbacks->LexedFileChanged(CurPPLexer->getFileID(),
                                  PPCallbacks::LexedFileChangeReason::ExitFile,
                                  FileType, ExitedFID, Loc);
    }

    // Restore conditional stack as well as the recorded
    // \#pragma clang assume_nonnull from the preamble right after exiting
    // from the predefines file.
    if (ExitedFromPredefinesFile) {
      replayPreambleConditionalStack();
      if (PreambleRecordedPragmaAssumeNonNullLoc.isValid())
        PragmaAssumeNonNullLoc = PreambleRecordedPragmaAssumeNonNullLoc;
    }

    if (!isEndOfMacro && CurPPLexer && FoundPCHThroughHeader &&
        (isInPrimaryFile() ||
         CurPPLexer->getFileID() == getPredefinesFileID())) {
      // Leaving the through header. Continue directly to end of main file
      // processing.
      LeavingPCHThroughHeader = true;
    } else {
      // Client should lex another token unless we generated an EOM.
      return LeavingSubmodule;
    }
  }
  // If this is the end of the main file, form an EOF token.
  assert(CurLexer && "Got EOF but no current lexer set!");
  const char *EndPos = getCurLexerEndPos();
  Result.startToken();
  CurLexer->BufferPtr = EndPos;

  if (getLangOpts().IncrementalExtensions) {
    CurLexer->FormTokenWithChars(Result, EndPos, tok::annot_repl_input_end);
    Result.setAnnotationEndLoc(Result.getLocation());
    Result.setAnnotationValue(nullptr);
  } else {
    CurLexer->FormTokenWithChars(Result, EndPos, tok::eof);
  }

  if (isCodeCompletionEnabled()) {
    // Inserting the code-completion point increases the source buffer by 1,
    // but the main FileID was created before inserting the point.
    // Compensate by reducing the EOF location by 1, otherwise the location
    // will point to the next FileID.
    // FIXME: This is hacky, the code-completion point should probably be
    // inserted before the main FileID is created.
    if (CurLexer->getFileLoc() == CodeCompletionFileLoc)
      Result.setLocation(Result.getLocation().getLocWithOffset(-1));
  }

  if (creatingPCHWithThroughHeader() && !LeavingPCHThroughHeader) {
    // Reached the end of the compilation without finding the through header.
    Diag(CurLexer->getFileLoc(), diag::err_pp_through_header_not_seen)
        << PPOpts->PCHThroughHeader << 0;
  }

  if (!isIncrementalProcessingEnabled())
    // We're done with lexing.
    CurLexer.reset();

  if (!isIncrementalProcessingEnabled())
    CurPPLexer = nullptr;

  if (TUKind == TU_Complete) {
    // This is the end of the top-level file. 'WarnUnusedMacroLocs' has
    // collected all macro locations that we need to warn because they are not
    // used.
    for (WarnUnusedMacroLocsTy::iterator
           I=WarnUnusedMacroLocs.begin(), E=WarnUnusedMacroLocs.end();
           I!=E; ++I)
      Diag(*I, diag::pp_macro_not_used);
  }

  // If we are building a module that has an umbrella header, make sure that
  // each of the headers within the directory, including all submodules, is
  // covered by the umbrella header was actually included by the umbrella
  // header.
  if (Module *Mod = getCurrentModule()) {
    llvm::SmallVector<const Module *, 4> AllMods;
    collectAllSubModulesWithUmbrellaHeader(*Mod, AllMods);
    for (auto *M : AllMods)
      diagnoseMissingHeaderInUmbrellaDir(*M);
  }

  return true;
}

/// HandleEndOfTokenLexer - This callback is invoked when the current TokenLexer
/// hits the end of its token stream.
bool Preprocessor::HandleEndOfTokenLexer(Token &Result) {
  assert(CurTokenLexer && !CurPPLexer &&
         "Ending a macro when currently in a #include file!");

  if (!MacroExpandingLexersStack.empty() &&
      MacroExpandingLexersStack.back().first == CurTokenLexer.get())
    removeCachedMacroExpandedTokensOfLastLexer();

  // Delete or cache the now-dead macro expander.
  if (NumCachedTokenLexers == TokenLexerCacheSize)
    CurTokenLexer.reset();
  else
    TokenLexerCache[NumCachedTokenLexers++] = std::move(CurTokenLexer);

  // Handle this like a #include file being popped off the stack.
  return HandleEndOfFile(Result, true);
}

/// RemoveTopOfLexerStack - Pop the current lexer/macro exp off the top of the
/// lexer stack.  This should only be used in situations where the current
/// state of the top-of-stack lexer is unknown.
void Preprocessor::RemoveTopOfLexerStack() {
  assert(!IncludeMacroStack.empty() && "Ran out of stack entries to load");

  if (CurTokenLexer) {
    // Delete or cache the now-dead macro expander.
    if (NumCachedTokenLexers == TokenLexerCacheSize)
      CurTokenLexer.reset();
    else
      TokenLexerCache[NumCachedTokenLexers++] = std::move(CurTokenLexer);
  }

  PopIncludeMacroStack();
}

/// HandleMicrosoftCommentPaste - When the macro expander pastes together a
/// comment (/##/) in microsoft mode, this method handles updating the current
/// state, returning the token on the next source line.
void Preprocessor::HandleMicrosoftCommentPaste(Token &Tok) {
  assert(CurTokenLexer && !CurPPLexer &&
         "Pasted comment can only be formed from macro");
  // We handle this by scanning for the closest real lexer, switching it to
  // raw mode and preprocessor mode.  This will cause it to return \n as an
  // explicit EOD token.
  PreprocessorLexer *FoundLexer = nullptr;
  bool LexerWasInPPMode = false;
  for (const IncludeStackInfo &ISI : llvm::reverse(IncludeMacroStack)) {
    if (ISI.ThePPLexer == nullptr) continue;  // Scan for a real lexer.

    // Once we find a real lexer, mark it as raw mode (disabling macro
    // expansions) and preprocessor mode (return EOD).  We know that the lexer
    // was *not* in raw mode before, because the macro that the comment came
    // from was expanded.  However, it could have already been in preprocessor
    // mode (#if COMMENT) in which case we have to return it to that mode and
    // return EOD.
    FoundLexer = ISI.ThePPLexer;
    FoundLexer->LexingRawMode = true;
    LexerWasInPPMode = FoundLexer->ParsingPreprocessorDirective;
    FoundLexer->ParsingPreprocessorDirective = true;
    break;
  }

  // Okay, we either found and switched over the lexer, or we didn't find a
  // lexer.  In either case, finish off the macro the comment came from, getting
  // the next token.
  if (!HandleEndOfTokenLexer(Tok)) Lex(Tok);

  // Discarding comments as long as we don't have EOF or EOD.  This 'comments
  // out' the rest of the line, including any tokens that came from other macros
  // that were active, as in:
  //  #define submacro a COMMENT b
  //    submacro c
  // which should lex to 'a' only: 'b' and 'c' should be removed.
  while (Tok.isNot(tok::eod) && Tok.isNot(tok::eof))
    Lex(Tok);

  // If we got an eod token, then we successfully found the end of the line.
  if (Tok.is(tok::eod)) {
    assert(FoundLexer && "Can't get end of line without an active lexer");
    // Restore the lexer back to normal mode instead of raw mode.
    FoundLexer->LexingRawMode = false;

    // If the lexer was already in preprocessor mode, just return the EOD token
    // to finish the preprocessor line.
    if (LexerWasInPPMode) return;

    // Otherwise, switch out of PP mode and return the next lexed token.
    FoundLexer->ParsingPreprocessorDirective = false;
    return Lex(Tok);
  }

  // If we got an EOF token, then we reached the end of the token stream but
  // didn't find an explicit \n.  This can only happen if there was no lexer
  // active (an active lexer would return EOD at EOF if there was no \n in
  // preprocessor directive mode), so just return EOF as our token.
  assert(!FoundLexer && "Lexer should return EOD before EOF in PP mode");
}

void Preprocessor::EnterSubmodule(Module *M, SourceLocation ImportLoc,
                                  bool ForPragma) {
  if (!getLangOpts().ModulesLocalVisibility) {
    // Just track that we entered this submodule.
    BuildingSubmoduleStack.push_back(
        BuildingSubmoduleInfo(M, ImportLoc, ForPragma, CurSubmoduleState,
                              PendingModuleMacroNames.size()));
    if (Callbacks)
      Callbacks->EnteredSubmodule(M, ImportLoc, ForPragma);
    return;
  }

  // Resolve as much of the module definition as we can now, before we enter
  // one of its headers.
  // FIXME: Can we enable Complain here?
  // FIXME: Can we do this when local visibility is disabled?
  ModuleMap &ModMap = getHeaderSearchInfo().getModuleMap();
  ModMap.resolveExports(M, /*Complain=*/false);
  ModMap.resolveUses(M, /*Complain=*/false);
  ModMap.resolveConflicts(M, /*Complain=*/false);

  // If this is the first time we've entered this module, set up its state.
  auto R = Submodules.insert(std::make_pair(M, SubmoduleState()));
  auto &State = R.first->second;
  bool FirstTime = R.second;
  if (FirstTime) {
    // Determine the set of starting macros for this submodule; take these
    // from the "null" module (the predefines buffer).
    //
    // FIXME: If we have local visibility but not modules enabled, the
    // NullSubmoduleState is polluted by #defines in the top-level source
    // file.
    auto &StartingMacros = NullSubmoduleState.Macros;

    // Restore to the starting state.
    // FIXME: Do this lazily, when each macro name is first referenced.
    for (auto &Macro : StartingMacros) {
      // Skip uninteresting macros.
      if (!Macro.second.getLatest() &&
          Macro.second.getOverriddenMacros().empty())
        continue;

      MacroState MS(Macro.second.getLatest());
      MS.setOverriddenMacros(*this, Macro.second.getOverriddenMacros());
      State.Macros.insert(std::make_pair(Macro.first, std::move(MS)));
    }
  }

  // Track that we entered this module.
  BuildingSubmoduleStack.push_back(
      BuildingSubmoduleInfo(M, ImportLoc, ForPragma, CurSubmoduleState,
                            PendingModuleMacroNames.size()));

  if (Callbacks)
    Callbacks->EnteredSubmodule(M, ImportLoc, ForPragma);

  // Switch to this submodule as the current submodule.
  CurSubmoduleState = &State;

  // This module is visible to itself.
  if (FirstTime)
    makeModuleVisible(M, ImportLoc);
}

bool Preprocessor::needModuleMacros() const {
  // If we're not within a submodule, we never need to create ModuleMacros.
  if (BuildingSubmoduleStack.empty())
    return false;
  // If we are tracking module macro visibility even for textually-included
  // headers, we need ModuleMacros.
  if (getLangOpts().ModulesLocalVisibility)
    return true;
  // Otherwise, we only need module macros if we're actually compiling a module
  // interface.
  return getLangOpts().isCompilingModule();
}

Module *Preprocessor::LeaveSubmodule(bool ForPragma) {
  if (BuildingSubmoduleStack.empty() ||
      BuildingSubmoduleStack.back().IsPragma != ForPragma) {
    assert(ForPragma && "non-pragma module enter/leave mismatch");
    return nullptr;
  }

  auto &Info = BuildingSubmoduleStack.back();

  Module *LeavingMod = Info.M;
  SourceLocation ImportLoc = Info.ImportLoc;

  if (!needModuleMacros() ||
      (!getLangOpts().ModulesLocalVisibility &&
       LeavingMod->getTopLevelModuleName() != getLangOpts().CurrentModule)) {
    // If we don't need module macros, or this is not a module for which we
    // are tracking macro visibility, don't build any, and preserve the list
    // of pending names for the surrounding submodule.
    BuildingSubmoduleStack.pop_back();

    if (Callbacks)
      Callbacks->LeftSubmodule(LeavingMod, ImportLoc, ForPragma);

    makeModuleVisible(LeavingMod, ImportLoc);
    return LeavingMod;
  }

  // Create ModuleMacros for any macros defined in this submodule.
  llvm::SmallPtrSet<const IdentifierInfo*, 8> VisitedMacros;
  for (unsigned I = Info.OuterPendingModuleMacroNames;
       I != PendingModuleMacroNames.size(); ++I) {
    auto *II = PendingModuleMacroNames[I];
    if (!VisitedMacros.insert(II).second)
      continue;

    auto MacroIt = CurSubmoduleState->Macros.find(II);
    if (MacroIt == CurSubmoduleState->Macros.end())
      continue;
    auto &Macro = MacroIt->second;

    // Find the starting point for the MacroDirective chain in this submodule.
    MacroDirective *OldMD = nullptr;
    auto *OldState = Info.OuterSubmoduleState;
    if (getLangOpts().ModulesLocalVisibility)
      OldState = &NullSubmoduleState;
    if (OldState && OldState != CurSubmoduleState) {
      // FIXME: It'd be better to start at the state from when we most recently
      // entered this submodule, but it doesn't really matter.
      auto &OldMacros = OldState->Macros;
      auto OldMacroIt = OldMacros.find(II);
      if (OldMacroIt == OldMacros.end())
        OldMD = nullptr;
      else
        OldMD = OldMacroIt->second.getLatest();
    }

    // This module may have exported a new macro. If so, create a ModuleMacro
    // representing that fact.
    bool ExplicitlyPublic = false;
    for (auto *MD = Macro.getLatest(); MD != OldMD; MD = MD->getPrevious()) {
      assert(MD && "broken macro directive chain");

      if (auto *VisMD = dyn_cast<VisibilityMacroDirective>(MD)) {
        // The latest visibility directive for a name in a submodule affects
        // all the directives that come before it.
        if (VisMD->isPublic())
          ExplicitlyPublic = true;
        else if (!ExplicitlyPublic)
          // Private with no following public directive: not exported.
          break;
      } else {
        MacroInfo *Def = nullptr;
        if (DefMacroDirective *DefMD = dyn_cast<DefMacroDirective>(MD))
          Def = DefMD->getInfo();

        // FIXME: Issue a warning if multiple headers for the same submodule
        // define a macro, rather than silently ignoring all but the first.
        bool IsNew;
        // Don't bother creating a module macro if it would represent a #undef
        // that doesn't override anything.
        if (Def || !Macro.getOverriddenMacros().empty())
          addModuleMacro(LeavingMod, II, Def, Macro.getOverriddenMacros(),
                         IsNew);

        if (!getLangOpts().ModulesLocalVisibility) {
          // This macro is exposed to the rest of this compilation as a
          // ModuleMacro; we don't need to track its MacroDirective any more.
          Macro.setLatest(nullptr);
          Macro.setOverriddenMacros(*this, {});
        }
        break;
      }
    }
  }
  PendingModuleMacroNames.resize(Info.OuterPendingModuleMacroNames);

  // FIXME: Before we leave this submodule, we should parse all the other
  // headers within it. Otherwise, we're left with an inconsistent state
  // where we've made the module visible but don't yet have its complete
  // contents.

  // Put back the outer module's state, if we're tracking it.
  if (getLangOpts().ModulesLocalVisibility)
    CurSubmoduleState = Info.OuterSubmoduleState;

  BuildingSubmoduleStack.pop_back();

  if (Callbacks)
    Callbacks->LeftSubmodule(LeavingMod, ImportLoc, ForPragma);

  // A nested #include makes the included submodule visible.
  makeModuleVisible(LeavingMod, ImportLoc);
  return LeavingMod;
}
