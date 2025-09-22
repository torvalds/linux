//===--- InclusionRewriter.cpp - Rewrite includes into their expansions ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This code rewrites include invocations into their expansions.  This gives you
// a file with all included files merged into it.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Frontend/Rewriters.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace clang;
using namespace llvm;

namespace {

class InclusionRewriter : public PPCallbacks {
  /// Information about which #includes were actually performed,
  /// created by preprocessor callbacks.
  struct IncludedFile {
    FileID Id;
    SrcMgr::CharacteristicKind FileType;
    IncludedFile(FileID Id, SrcMgr::CharacteristicKind FileType)
        : Id(Id), FileType(FileType) {}
  };
  Preprocessor &PP; ///< Used to find inclusion directives.
  SourceManager &SM; ///< Used to read and manage source files.
  raw_ostream &OS; ///< The destination stream for rewritten contents.
  StringRef MainEOL; ///< The line ending marker to use.
  llvm::MemoryBufferRef PredefinesBuffer; ///< The preprocessor predefines.
  bool ShowLineMarkers; ///< Show #line markers.
  bool UseLineDirectives; ///< Use of line directives or line markers.
  /// Tracks where inclusions that change the file are found.
  std::map<SourceLocation, IncludedFile> FileIncludes;
  /// Tracks where inclusions that import modules are found.
  std::map<SourceLocation, const Module *> ModuleIncludes;
  /// Tracks where inclusions that enter modules (in a module build) are found.
  std::map<SourceLocation, const Module *> ModuleEntryIncludes;
  /// Tracks where #if and #elif directives get evaluated and whether to true.
  std::map<SourceLocation, bool> IfConditions;
  /// Used transitively for building up the FileIncludes mapping over the
  /// various \c PPCallbacks callbacks.
  SourceLocation LastInclusionLocation;
public:
  InclusionRewriter(Preprocessor &PP, raw_ostream &OS, bool ShowLineMarkers,
                    bool UseLineDirectives);
  void Process(FileID FileId, SrcMgr::CharacteristicKind FileType);
  void setPredefinesBuffer(const llvm::MemoryBufferRef &Buf) {
    PredefinesBuffer = Buf;
  }
  void detectMainFileEOL();
  void handleModuleBegin(Token &Tok) {
    assert(Tok.getKind() == tok::annot_module_begin);
    ModuleEntryIncludes.insert(
        {Tok.getLocation(), (Module *)Tok.getAnnotationValue()});
  }
private:
  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;
  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override;
  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override;
  void If(SourceLocation Loc, SourceRange ConditionRange,
          ConditionValueKind ConditionValue) override;
  void Elif(SourceLocation Loc, SourceRange ConditionRange,
            ConditionValueKind ConditionValue, SourceLocation IfLoc) override;
  void WriteLineInfo(StringRef Filename, int Line,
                     SrcMgr::CharacteristicKind FileType,
                     StringRef Extra = StringRef());
  void WriteImplicitModuleImport(const Module *Mod);
  void OutputContentUpTo(const MemoryBufferRef &FromFile, unsigned &WriteFrom,
                         unsigned WriteTo, StringRef EOL, int &lines,
                         bool EnsureNewline);
  void CommentOutDirective(Lexer &DirectivesLex, const Token &StartToken,
                           const MemoryBufferRef &FromFile, StringRef EOL,
                           unsigned &NextToWrite, int &Lines,
                           const IncludedFile *Inc = nullptr);
  const IncludedFile *FindIncludeAtLocation(SourceLocation Loc) const;
  StringRef getIncludedFileName(const IncludedFile *Inc) const;
  const Module *FindModuleAtLocation(SourceLocation Loc) const;
  const Module *FindEnteredModule(SourceLocation Loc) const;
  bool IsIfAtLocationTrue(SourceLocation Loc) const;
  StringRef NextIdentifierName(Lexer &RawLex, Token &RawToken);
};

}  // end anonymous namespace

/// Initializes an InclusionRewriter with a \p PP source and \p OS destination.
InclusionRewriter::InclusionRewriter(Preprocessor &PP, raw_ostream &OS,
                                     bool ShowLineMarkers,
                                     bool UseLineDirectives)
    : PP(PP), SM(PP.getSourceManager()), OS(OS), MainEOL("\n"),
      ShowLineMarkers(ShowLineMarkers), UseLineDirectives(UseLineDirectives),
      LastInclusionLocation(SourceLocation()) {}

/// Write appropriate line information as either #line directives or GNU line
/// markers depending on what mode we're in, including the \p Filename and
/// \p Line we are located at, using the specified \p EOL line separator, and
/// any \p Extra context specifiers in GNU line directives.
void InclusionRewriter::WriteLineInfo(StringRef Filename, int Line,
                                      SrcMgr::CharacteristicKind FileType,
                                      StringRef Extra) {
  if (!ShowLineMarkers)
    return;
  if (UseLineDirectives) {
    OS << "#line" << ' ' << Line << ' ' << '"';
    OS.write_escaped(Filename);
    OS << '"';
  } else {
    // Use GNU linemarkers as described here:
    // http://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html
    OS << '#' << ' ' << Line << ' ' << '"';
    OS.write_escaped(Filename);
    OS << '"';
    if (!Extra.empty())
      OS << Extra;
    if (FileType == SrcMgr::C_System)
      // "`3' This indicates that the following text comes from a system header
      // file, so certain warnings should be suppressed."
      OS << " 3";
    else if (FileType == SrcMgr::C_ExternCSystem)
      // as above for `3', plus "`4' This indicates that the following text
      // should be treated as being wrapped in an implicit extern "C" block."
      OS << " 3 4";
  }
  OS << MainEOL;
}

void InclusionRewriter::WriteImplicitModuleImport(const Module *Mod) {
  OS << "#pragma clang module import " << Mod->getFullModuleName(true)
     << " /* clang -frewrite-includes: implicit import */" << MainEOL;
}

/// FileChanged - Whenever the preprocessor enters or exits a #include file
/// it invokes this handler.
void InclusionRewriter::FileChanged(SourceLocation Loc,
                                    FileChangeReason Reason,
                                    SrcMgr::CharacteristicKind NewFileType,
                                    FileID) {
  if (Reason != EnterFile)
    return;
  if (LastInclusionLocation.isInvalid())
    // we didn't reach this file (eg: the main file) via an inclusion directive
    return;
  FileID Id = FullSourceLoc(Loc, SM).getFileID();
  auto P = FileIncludes.insert(
      std::make_pair(LastInclusionLocation, IncludedFile(Id, NewFileType)));
  (void)P;
  assert(P.second && "Unexpected revisitation of the same include directive");
  LastInclusionLocation = SourceLocation();
}

/// Called whenever an inclusion is skipped due to canonical header protection
/// macros.
void InclusionRewriter::FileSkipped(const FileEntryRef & /*SkippedFile*/,
                                    const Token & /*FilenameTok*/,
                                    SrcMgr::CharacteristicKind /*FileType*/) {
  assert(LastInclusionLocation.isValid() &&
         "A file, that wasn't found via an inclusion directive, was skipped");
  LastInclusionLocation = SourceLocation();
}

/// This should be called whenever the preprocessor encounters include
/// directives. It does not say whether the file has been included, but it
/// provides more information about the directive (hash location instead
/// of location inside the included file). It is assumed that the matching
/// FileChanged() or FileSkipped() is called after this (or neither is
/// called if this #include results in an error or does not textually include
/// anything).
void InclusionRewriter::InclusionDirective(
    SourceLocation HashLoc, const Token & /*IncludeTok*/,
    StringRef /*FileName*/, bool /*IsAngled*/,
    CharSourceRange /*FilenameRange*/, OptionalFileEntryRef /*File*/,
    StringRef /*SearchPath*/, StringRef /*RelativePath*/,
    const Module *SuggestedModule, bool ModuleImported,
    SrcMgr::CharacteristicKind FileType) {
  if (ModuleImported) {
    auto P = ModuleIncludes.insert(std::make_pair(HashLoc, SuggestedModule));
    (void)P;
    assert(P.second && "Unexpected revisitation of the same include directive");
  } else
    LastInclusionLocation = HashLoc;
}

void InclusionRewriter::If(SourceLocation Loc, SourceRange ConditionRange,
                           ConditionValueKind ConditionValue) {
  auto P = IfConditions.insert(std::make_pair(Loc, ConditionValue == CVK_True));
  (void)P;
  assert(P.second && "Unexpected revisitation of the same if directive");
}

void InclusionRewriter::Elif(SourceLocation Loc, SourceRange ConditionRange,
                             ConditionValueKind ConditionValue,
                             SourceLocation IfLoc) {
  auto P = IfConditions.insert(std::make_pair(Loc, ConditionValue == CVK_True));
  (void)P;
  assert(P.second && "Unexpected revisitation of the same elif directive");
}

/// Simple lookup for a SourceLocation (specifically one denoting the hash in
/// an inclusion directive) in the map of inclusion information, FileChanges.
const InclusionRewriter::IncludedFile *
InclusionRewriter::FindIncludeAtLocation(SourceLocation Loc) const {
  const auto I = FileIncludes.find(Loc);
  if (I != FileIncludes.end())
    return &I->second;
  return nullptr;
}

/// Simple lookup for a SourceLocation (specifically one denoting the hash in
/// an inclusion directive) in the map of module inclusion information.
const Module *
InclusionRewriter::FindModuleAtLocation(SourceLocation Loc) const {
  const auto I = ModuleIncludes.find(Loc);
  if (I != ModuleIncludes.end())
    return I->second;
  return nullptr;
}

/// Simple lookup for a SourceLocation (specifically one denoting the hash in
/// an inclusion directive) in the map of module entry information.
const Module *
InclusionRewriter::FindEnteredModule(SourceLocation Loc) const {
  const auto I = ModuleEntryIncludes.find(Loc);
  if (I != ModuleEntryIncludes.end())
    return I->second;
  return nullptr;
}

bool InclusionRewriter::IsIfAtLocationTrue(SourceLocation Loc) const {
  const auto I = IfConditions.find(Loc);
  if (I != IfConditions.end())
    return I->second;
  return false;
}

void InclusionRewriter::detectMainFileEOL() {
  std::optional<MemoryBufferRef> FromFile =
      *SM.getBufferOrNone(SM.getMainFileID());
  assert(FromFile);
  if (!FromFile)
    return; // Should never happen, but whatever.
  MainEOL = FromFile->getBuffer().detectEOL();
}

/// Writes out bytes from \p FromFile, starting at \p NextToWrite and ending at
/// \p WriteTo - 1.
void InclusionRewriter::OutputContentUpTo(const MemoryBufferRef &FromFile,
                                          unsigned &WriteFrom, unsigned WriteTo,
                                          StringRef LocalEOL, int &Line,
                                          bool EnsureNewline) {
  if (WriteTo <= WriteFrom)
    return;
  if (FromFile == PredefinesBuffer) {
    // Ignore the #defines of the predefines buffer.
    WriteFrom = WriteTo;
    return;
  }

  // If we would output half of a line ending, advance one character to output
  // the whole line ending.  All buffers are null terminated, so looking ahead
  // one byte is safe.
  if (LocalEOL.size() == 2 &&
      LocalEOL[0] == (FromFile.getBufferStart() + WriteTo)[-1] &&
      LocalEOL[1] == (FromFile.getBufferStart() + WriteTo)[0])
    WriteTo++;

  StringRef TextToWrite(FromFile.getBufferStart() + WriteFrom,
                        WriteTo - WriteFrom);
  // count lines manually, it's faster than getPresumedLoc()
  Line += TextToWrite.count(LocalEOL);

  if (MainEOL == LocalEOL) {
    OS << TextToWrite;
  } else {
    // Output the file one line at a time, rewriting the line endings as we go.
    StringRef Rest = TextToWrite;
    while (!Rest.empty()) {
      // Identify and output the next line excluding an EOL sequence if present.
      size_t Idx = Rest.find(LocalEOL);
      StringRef LineText = Rest.substr(0, Idx);
      OS << LineText;
      if (Idx != StringRef::npos) {
        // An EOL sequence was present, output the EOL sequence for the
        // main source file and skip past the local EOL sequence.
        OS << MainEOL;
        Idx += LocalEOL.size();
      }
      // Strip the line just handled. If Idx is npos or matches the end of the
      // text, Rest will be set to an empty string and the loop will terminate.
      Rest = Rest.substr(Idx);
    }
  }
  if (EnsureNewline && !TextToWrite.ends_with(LocalEOL))
    OS << MainEOL;

  WriteFrom = WriteTo;
}

StringRef
InclusionRewriter::getIncludedFileName(const IncludedFile *Inc) const {
  if (Inc) {
    auto B = SM.getBufferOrNone(Inc->Id);
    assert(B && "Attempting to process invalid inclusion");
    if (B)
      return llvm::sys::path::filename(B->getBufferIdentifier());
  }
  return StringRef();
}

/// Print characters from \p FromFile starting at \p NextToWrite up until the
/// inclusion directive at \p StartToken, then print out the inclusion
/// inclusion directive disabled by a #if directive, updating \p NextToWrite
/// and \p Line to track the number of source lines visited and the progress
/// through the \p FromFile buffer.
void InclusionRewriter::CommentOutDirective(Lexer &DirectiveLex,
                                            const Token &StartToken,
                                            const MemoryBufferRef &FromFile,
                                            StringRef LocalEOL,
                                            unsigned &NextToWrite, int &Line,
                                            const IncludedFile *Inc) {
  OutputContentUpTo(FromFile, NextToWrite,
                    SM.getFileOffset(StartToken.getLocation()), LocalEOL, Line,
                    false);
  Token DirectiveToken;
  do {
    DirectiveLex.LexFromRawLexer(DirectiveToken);
  } while (!DirectiveToken.is(tok::eod) && DirectiveToken.isNot(tok::eof));
  if (FromFile == PredefinesBuffer) {
    // OutputContentUpTo() would not output anything anyway.
    return;
  }
  if (Inc) {
    OS << "#if defined(__CLANG_REWRITTEN_INCLUDES) ";
    if (isSystem(Inc->FileType))
      OS << "|| defined(__CLANG_REWRITTEN_SYSTEM_INCLUDES) ";
    OS << "/* " << getIncludedFileName(Inc);
  } else {
    OS << "#if 0 /*";
  }
  OS << " expanded by -frewrite-includes */" << MainEOL;
  OutputContentUpTo(FromFile, NextToWrite,
                    SM.getFileOffset(DirectiveToken.getLocation()) +
                        DirectiveToken.getLength(),
                    LocalEOL, Line, true);
  OS << (Inc ? "#else /* " : "#endif /*") << getIncludedFileName(Inc)
     << " expanded by -frewrite-includes */" << MainEOL;
}

/// Find the next identifier in the pragma directive specified by \p RawToken.
StringRef InclusionRewriter::NextIdentifierName(Lexer &RawLex,
                                                Token &RawToken) {
  RawLex.LexFromRawLexer(RawToken);
  if (RawToken.is(tok::raw_identifier))
    PP.LookUpIdentifierInfo(RawToken);
  if (RawToken.is(tok::identifier))
    return RawToken.getIdentifierInfo()->getName();
  return StringRef();
}

/// Use a raw lexer to analyze \p FileId, incrementally copying parts of it
/// and including content of included files recursively.
void InclusionRewriter::Process(FileID FileId,
                                SrcMgr::CharacteristicKind FileType) {
  MemoryBufferRef FromFile;
  {
    auto B = SM.getBufferOrNone(FileId);
    assert(B && "Attempting to process invalid inclusion");
    if (B)
      FromFile = *B;
  }
  StringRef FileName = FromFile.getBufferIdentifier();
  Lexer RawLex(FileId, FromFile, PP.getSourceManager(), PP.getLangOpts());
  RawLex.SetCommentRetentionState(false);

  StringRef LocalEOL = FromFile.getBuffer().detectEOL();

  // Per the GNU docs: "1" indicates entering a new file.
  if (FileId == SM.getMainFileID() || FileId == PP.getPredefinesFileID())
    WriteLineInfo(FileName, 1, FileType, "");
  else
    WriteLineInfo(FileName, 1, FileType, " 1");

  if (SM.getFileIDSize(FileId) == 0)
    return;

  // The next byte to be copied from the source file, which may be non-zero if
  // the lexer handled a BOM.
  unsigned NextToWrite = SM.getFileOffset(RawLex.getSourceLocation());
  assert(SM.getLineNumber(FileId, NextToWrite) == 1);
  int Line = 1; // The current input file line number.

  Token RawToken;
  RawLex.LexFromRawLexer(RawToken);

  // TODO: Consider adding a switch that strips possibly unimportant content,
  // such as comments, to reduce the size of repro files.
  while (RawToken.isNot(tok::eof)) {
    if (RawToken.is(tok::hash) && RawToken.isAtStartOfLine()) {
      RawLex.setParsingPreprocessorDirective(true);
      Token HashToken = RawToken;
      RawLex.LexFromRawLexer(RawToken);
      if (RawToken.is(tok::raw_identifier))
        PP.LookUpIdentifierInfo(RawToken);
      if (RawToken.getIdentifierInfo() != nullptr) {
        switch (RawToken.getIdentifierInfo()->getPPKeywordID()) {
          case tok::pp_include:
          case tok::pp_include_next:
          case tok::pp_import: {
            SourceLocation Loc = HashToken.getLocation();
            const IncludedFile *Inc = FindIncludeAtLocation(Loc);
            CommentOutDirective(RawLex, HashToken, FromFile, LocalEOL,
                                NextToWrite, Line, Inc);
            if (FileId != PP.getPredefinesFileID())
              WriteLineInfo(FileName, Line - 1, FileType, "");
            StringRef LineInfoExtra;
            if (const Module *Mod = FindModuleAtLocation(Loc))
              WriteImplicitModuleImport(Mod);
            else if (Inc) {
              const Module *Mod = FindEnteredModule(Loc);
              if (Mod)
                OS << "#pragma clang module begin "
                   << Mod->getFullModuleName(true) << "\n";

              // Include and recursively process the file.
              Process(Inc->Id, Inc->FileType);

              if (Mod)
                OS << "#pragma clang module end /*"
                   << Mod->getFullModuleName(true) << "*/\n";
              // There's no #include, therefore no #if, for -include files.
              if (FromFile != PredefinesBuffer) {
                OS << "#endif /* " << getIncludedFileName(Inc)
                   << " expanded by -frewrite-includes */" << LocalEOL;
              }

              // Add line marker to indicate we're returning from an included
              // file.
              LineInfoExtra = " 2";
            }
            // fix up lineinfo (since commented out directive changed line
            // numbers) for inclusions that were skipped due to header guards
            WriteLineInfo(FileName, Line, FileType, LineInfoExtra);
            break;
          }
          case tok::pp_pragma: {
            StringRef Identifier = NextIdentifierName(RawLex, RawToken);
            if (Identifier == "clang" || Identifier == "GCC") {
              if (NextIdentifierName(RawLex, RawToken) == "system_header") {
                // keep the directive in, commented out
                CommentOutDirective(RawLex, HashToken, FromFile, LocalEOL,
                  NextToWrite, Line);
                // update our own type
                FileType = SM.getFileCharacteristic(RawToken.getLocation());
                WriteLineInfo(FileName, Line, FileType);
              }
            } else if (Identifier == "once") {
              // keep the directive in, commented out
              CommentOutDirective(RawLex, HashToken, FromFile, LocalEOL,
                NextToWrite, Line);
              WriteLineInfo(FileName, Line, FileType);
            }
            break;
          }
          case tok::pp_if:
          case tok::pp_elif: {
            bool elif = (RawToken.getIdentifierInfo()->getPPKeywordID() ==
                         tok::pp_elif);
            bool isTrue = IsIfAtLocationTrue(RawToken.getLocation());
            OutputContentUpTo(FromFile, NextToWrite,
                              SM.getFileOffset(HashToken.getLocation()),
                              LocalEOL, Line, /*EnsureNewline=*/true);
            do {
              RawLex.LexFromRawLexer(RawToken);
            } while (!RawToken.is(tok::eod) && RawToken.isNot(tok::eof));
            // We need to disable the old condition, but that is tricky.
            // Trying to comment it out can easily lead to comment nesting.
            // So instead make the condition harmless by making it enclose
            // and empty block. Moreover, put it itself inside an #if 0 block
            // to disable it from getting evaluated (e.g. __has_include_next
            // warns if used from the primary source file).
            OS << "#if 0 /* disabled by -frewrite-includes */" << MainEOL;
            if (elif) {
              OS << "#if 0" << MainEOL;
            }
            OutputContentUpTo(FromFile, NextToWrite,
                              SM.getFileOffset(RawToken.getLocation()) +
                                  RawToken.getLength(),
                              LocalEOL, Line, /*EnsureNewline=*/true);
            // Close the empty block and the disabling block.
            OS << "#endif" << MainEOL;
            OS << "#endif /* disabled by -frewrite-includes */" << MainEOL;
            OS << (elif ? "#elif " : "#if ") << (isTrue ? "1" : "0")
               << " /* evaluated by -frewrite-includes */" << MainEOL;
            WriteLineInfo(FileName, Line, FileType);
            break;
          }
          case tok::pp_endif:
          case tok::pp_else: {
            // We surround every #include by #if 0 to comment it out, but that
            // changes line numbers. These are fixed up right after that, but
            // the whole #include could be inside a preprocessor conditional
            // that is not processed. So it is necessary to fix the line
            // numbers one the next line after each #else/#endif as well.
            RawLex.SetKeepWhitespaceMode(true);
            do {
              RawLex.LexFromRawLexer(RawToken);
            } while (RawToken.isNot(tok::eod) && RawToken.isNot(tok::eof));
            OutputContentUpTo(FromFile, NextToWrite,
                              SM.getFileOffset(RawToken.getLocation()) +
                                  RawToken.getLength(),
                              LocalEOL, Line, /*EnsureNewline=*/ true);
            WriteLineInfo(FileName, Line, FileType);
            RawLex.SetKeepWhitespaceMode(false);
            break;
          }
          default:
            break;
        }
      }
      RawLex.setParsingPreprocessorDirective(false);
    }
    RawLex.LexFromRawLexer(RawToken);
  }
  OutputContentUpTo(FromFile, NextToWrite,
                    SM.getFileOffset(SM.getLocForEndOfFile(FileId)), LocalEOL,
                    Line, /*EnsureNewline=*/true);
}

/// InclusionRewriterInInput - Implement -frewrite-includes mode.
void clang::RewriteIncludesInInput(Preprocessor &PP, raw_ostream *OS,
                                   const PreprocessorOutputOptions &Opts) {
  SourceManager &SM = PP.getSourceManager();
  InclusionRewriter *Rewrite = new InclusionRewriter(
      PP, *OS, Opts.ShowLineMarkers, Opts.UseLineDirectives);
  Rewrite->detectMainFileEOL();

  PP.addPPCallbacks(std::unique_ptr<PPCallbacks>(Rewrite));
  PP.IgnorePragmas();

  // First let the preprocessor process the entire file and call callbacks.
  // Callbacks will record which #include's were actually performed.
  PP.EnterMainSourceFile();
  Token Tok;
  // Only preprocessor directives matter here, so disable macro expansion
  // everywhere else as an optimization.
  // TODO: It would be even faster if the preprocessor could be switched
  // to a mode where it would parse only preprocessor directives and comments,
  // nothing else matters for parsing or processing.
  PP.SetMacroExpansionOnlyInDirectives();
  do {
    PP.Lex(Tok);
    if (Tok.is(tok::annot_module_begin))
      Rewrite->handleModuleBegin(Tok);
  } while (Tok.isNot(tok::eof));
  Rewrite->setPredefinesBuffer(SM.getBufferOrFake(PP.getPredefinesFileID()));
  Rewrite->Process(PP.getPredefinesFileID(), SrcMgr::C_User);
  Rewrite->Process(SM.getMainFileID(), SrcMgr::C_User);
  OS->flush();
}
