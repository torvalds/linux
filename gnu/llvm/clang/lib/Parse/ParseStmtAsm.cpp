//===---- ParseStmtAsm.cpp - Assembly Statement Parser --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements parsing for GCC and Microsoft inline assembly.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
using namespace clang;

namespace {
class ClangAsmParserCallback : public llvm::MCAsmParserSemaCallback {
  Parser &TheParser;
  SourceLocation AsmLoc;
  StringRef AsmString;

  /// The tokens we streamed into AsmString and handed off to MC.
  ArrayRef<Token> AsmToks;

  /// The offset of each token in AsmToks within AsmString.
  ArrayRef<unsigned> AsmTokOffsets;

public:
  ClangAsmParserCallback(Parser &P, SourceLocation Loc, StringRef AsmString,
                         ArrayRef<Token> Toks, ArrayRef<unsigned> Offsets)
      : TheParser(P), AsmLoc(Loc), AsmString(AsmString), AsmToks(Toks),
        AsmTokOffsets(Offsets) {
    assert(AsmToks.size() == AsmTokOffsets.size());
  }

  void LookupInlineAsmIdentifier(StringRef &LineBuf,
                                 llvm::InlineAsmIdentifierInfo &Info,
                                 bool IsUnevaluatedContext) override;

  StringRef LookupInlineAsmLabel(StringRef Identifier, llvm::SourceMgr &LSM,
                                 llvm::SMLoc Location,
                                 bool Create) override;

  bool LookupInlineAsmField(StringRef Base, StringRef Member,
                            unsigned &Offset) override {
    return TheParser.getActions().LookupInlineAsmField(Base, Member, Offset,
                                                       AsmLoc);
  }

  static void DiagHandlerCallback(const llvm::SMDiagnostic &D, void *Context) {
    ((ClangAsmParserCallback *)Context)->handleDiagnostic(D);
  }

private:
  /// Collect the appropriate tokens for the given string.
  void findTokensForString(StringRef Str, SmallVectorImpl<Token> &TempToks,
                           const Token *&FirstOrigToken) const;

  SourceLocation translateLocation(const llvm::SourceMgr &LSM,
                                   llvm::SMLoc SMLoc);

  void handleDiagnostic(const llvm::SMDiagnostic &D);
};
}

void ClangAsmParserCallback::LookupInlineAsmIdentifier(
    StringRef &LineBuf, llvm::InlineAsmIdentifierInfo &Info,
    bool IsUnevaluatedContext) {
  // Collect the desired tokens.
  SmallVector<Token, 16> LineToks;
  const Token *FirstOrigToken = nullptr;
  findTokensForString(LineBuf, LineToks, FirstOrigToken);

  unsigned NumConsumedToks;
  ExprResult Result = TheParser.ParseMSAsmIdentifier(LineToks, NumConsumedToks,
                                                     IsUnevaluatedContext);

  // If we consumed the entire line, tell MC that.
  // Also do this if we consumed nothing as a way of reporting failure.
  if (NumConsumedToks == 0 || NumConsumedToks == LineToks.size()) {
    // By not modifying LineBuf, we're implicitly consuming it all.

    // Otherwise, consume up to the original tokens.
  } else {
    assert(FirstOrigToken && "not using original tokens?");

    // Since we're using original tokens, apply that offset.
    assert(FirstOrigToken[NumConsumedToks].getLocation() ==
           LineToks[NumConsumedToks].getLocation());
    unsigned FirstIndex = FirstOrigToken - AsmToks.begin();
    unsigned LastIndex = FirstIndex + NumConsumedToks - 1;

    // The total length we've consumed is the relative offset
    // of the last token we consumed plus its length.
    unsigned TotalOffset =
        (AsmTokOffsets[LastIndex] + AsmToks[LastIndex].getLength() -
         AsmTokOffsets[FirstIndex]);
    LineBuf = LineBuf.substr(0, TotalOffset);
  }

  // Initialize Info with the lookup result.
  if (!Result.isUsable())
    return;
  TheParser.getActions().FillInlineAsmIdentifierInfo(Result.get(), Info);
}

StringRef ClangAsmParserCallback::LookupInlineAsmLabel(StringRef Identifier,
                                                       llvm::SourceMgr &LSM,
                                                       llvm::SMLoc Location,
                                                       bool Create) {
  SourceLocation Loc = translateLocation(LSM, Location);
  LabelDecl *Label =
      TheParser.getActions().GetOrCreateMSAsmLabel(Identifier, Loc, Create);
  return Label->getMSAsmLabel();
}

void ClangAsmParserCallback::findTokensForString(
    StringRef Str, SmallVectorImpl<Token> &TempToks,
    const Token *&FirstOrigToken) const {
  // For now, assert that the string we're working with is a substring
  // of what we gave to MC.  This lets us use the original tokens.
  assert(!std::less<const char *>()(Str.begin(), AsmString.begin()) &&
         !std::less<const char *>()(AsmString.end(), Str.end()));

  // Try to find a token whose offset matches the first token.
  unsigned FirstCharOffset = Str.begin() - AsmString.begin();
  const unsigned *FirstTokOffset =
      llvm::lower_bound(AsmTokOffsets, FirstCharOffset);

  // For now, assert that the start of the string exactly
  // corresponds to the start of a token.
  assert(*FirstTokOffset == FirstCharOffset);

  // Use all the original tokens for this line.  (We assume the
  // end of the line corresponds cleanly to a token break.)
  unsigned FirstTokIndex = FirstTokOffset - AsmTokOffsets.begin();
  FirstOrigToken = &AsmToks[FirstTokIndex];
  unsigned LastCharOffset = Str.end() - AsmString.begin();
  for (unsigned i = FirstTokIndex, e = AsmTokOffsets.size(); i != e; ++i) {
    if (AsmTokOffsets[i] >= LastCharOffset)
      break;
    TempToks.push_back(AsmToks[i]);
  }
}

SourceLocation
ClangAsmParserCallback::translateLocation(const llvm::SourceMgr &LSM,
                                          llvm::SMLoc SMLoc) {
  // Compute an offset into the inline asm buffer.
  // FIXME: This isn't right if .macro is involved (but hopefully, no
  // real-world code does that).
  const llvm::MemoryBuffer *LBuf =
      LSM.getMemoryBuffer(LSM.FindBufferContainingLoc(SMLoc));
  unsigned Offset = SMLoc.getPointer() - LBuf->getBufferStart();

  // Figure out which token that offset points into.
  const unsigned *TokOffsetPtr = llvm::lower_bound(AsmTokOffsets, Offset);
  unsigned TokIndex = TokOffsetPtr - AsmTokOffsets.begin();
  unsigned TokOffset = *TokOffsetPtr;

  // If we come up with an answer which seems sane, use it; otherwise,
  // just point at the __asm keyword.
  // FIXME: Assert the answer is sane once we handle .macro correctly.
  SourceLocation Loc = AsmLoc;
  if (TokIndex < AsmToks.size()) {
    const Token &Tok = AsmToks[TokIndex];
    Loc = Tok.getLocation();
    Loc = Loc.getLocWithOffset(Offset - TokOffset);
  }
  return Loc;
}

void ClangAsmParserCallback::handleDiagnostic(const llvm::SMDiagnostic &D) {
  const llvm::SourceMgr &LSM = *D.getSourceMgr();
  SourceLocation Loc = translateLocation(LSM, D.getLoc());
  TheParser.Diag(Loc, diag::err_inline_ms_asm_parsing) << D.getMessage();
}

/// Parse an identifier in an MS-style inline assembly block.
ExprResult Parser::ParseMSAsmIdentifier(llvm::SmallVectorImpl<Token> &LineToks,
                                        unsigned &NumLineToksConsumed,
                                        bool IsUnevaluatedContext) {
  // Push a fake token on the end so that we don't overrun the token
  // stream.  We use ';' because it expression-parsing should never
  // overrun it.
  const tok::TokenKind EndOfStream = tok::semi;
  Token EndOfStreamTok;
  EndOfStreamTok.startToken();
  EndOfStreamTok.setKind(EndOfStream);
  LineToks.push_back(EndOfStreamTok);

  // Also copy the current token over.
  LineToks.push_back(Tok);

  PP.EnterTokenStream(LineToks, /*DisableMacroExpansions*/ true,
                      /*IsReinject*/ true);

  // Clear the current token and advance to the first token in LineToks.
  ConsumeAnyToken();

  // Parse an optional scope-specifier if we're in C++.
  CXXScopeSpec SS;
  if (getLangOpts().CPlusPlus)
    ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                   /*ObjectHasErrors=*/false,
                                   /*EnteringContext=*/false);

  // Require an identifier here.
  SourceLocation TemplateKWLoc;
  UnqualifiedId Id;
  bool Invalid = true;
  ExprResult Result;
  if (Tok.is(tok::kw_this)) {
    Result = ParseCXXThis();
    Invalid = false;
  } else {
    Invalid =
        ParseUnqualifiedId(SS, /*ObjectType=*/nullptr,
                           /*ObjectHadErrors=*/false,
                           /*EnteringContext=*/false,
                           /*AllowDestructorName=*/false,
                           /*AllowConstructorName=*/false,
                           /*AllowDeductionGuide=*/false, &TemplateKWLoc, Id);
    // Perform the lookup.
    Result = Actions.LookupInlineAsmIdentifier(SS, TemplateKWLoc, Id,
                                               IsUnevaluatedContext);
  }
  // While the next two tokens are 'period' 'identifier', repeatedly parse it as
  // a field access. We have to avoid consuming assembler directives that look
  // like '.' 'else'.
  while (Result.isUsable() && Tok.is(tok::period)) {
    Token IdTok = PP.LookAhead(0);
    if (IdTok.isNot(tok::identifier))
      break;
    ConsumeToken(); // Consume the period.
    IdentifierInfo *Id = Tok.getIdentifierInfo();
    ConsumeToken(); // Consume the identifier.
    Result = Actions.LookupInlineAsmVarDeclField(Result.get(), Id->getName(),
                                                 Tok.getLocation());
  }

  // Figure out how many tokens we are into LineToks.
  unsigned LineIndex = 0;
  if (Tok.is(EndOfStream)) {
    LineIndex = LineToks.size() - 2;
  } else {
    while (LineToks[LineIndex].getLocation() != Tok.getLocation()) {
      LineIndex++;
      assert(LineIndex < LineToks.size() - 2); // we added two extra tokens
    }
  }

  // If we've run into the poison token we inserted before, or there
  // was a parsing error, then claim the entire line.
  if (Invalid || Tok.is(EndOfStream)) {
    NumLineToksConsumed = LineToks.size() - 2;
  } else {
    // Otherwise, claim up to the start of the next token.
    NumLineToksConsumed = LineIndex;
  }

  // Finally, restore the old parsing state by consuming all the tokens we
  // staged before, implicitly killing off the token-lexer we pushed.
  for (unsigned i = 0, e = LineToks.size() - LineIndex - 2; i != e; ++i) {
    ConsumeAnyToken();
  }
  assert(Tok.is(EndOfStream));
  ConsumeToken();

  // Leave LineToks in its original state.
  LineToks.pop_back();
  LineToks.pop_back();

  return Result;
}

/// Turn a sequence of our tokens back into a string that we can hand
/// to the MC asm parser.
static bool buildMSAsmString(Preprocessor &PP, SourceLocation AsmLoc,
                             ArrayRef<Token> AsmToks,
                             SmallVectorImpl<unsigned> &TokOffsets,
                             SmallString<512> &Asm) {
  assert(!AsmToks.empty() && "Didn't expect an empty AsmToks!");

  // Is this the start of a new assembly statement?
  bool isNewStatement = true;

  for (unsigned i = 0, e = AsmToks.size(); i < e; ++i) {
    const Token &Tok = AsmToks[i];

    // Start each new statement with a newline and a tab.
    if (!isNewStatement && (Tok.is(tok::kw_asm) || Tok.isAtStartOfLine())) {
      Asm += "\n\t";
      isNewStatement = true;
    }

    // Preserve the existence of leading whitespace except at the
    // start of a statement.
    if (!isNewStatement && Tok.hasLeadingSpace())
      Asm += ' ';

    // Remember the offset of this token.
    TokOffsets.push_back(Asm.size());

    // Don't actually write '__asm' into the assembly stream.
    if (Tok.is(tok::kw_asm)) {
      // Complain about __asm at the end of the stream.
      if (i + 1 == e) {
        PP.Diag(AsmLoc, diag::err_asm_empty);
        return true;
      }

      continue;
    }

    // Append the spelling of the token.
    SmallString<32> SpellingBuffer;
    bool SpellingInvalid = false;
    Asm += PP.getSpelling(Tok, SpellingBuffer, &SpellingInvalid);
    assert(!SpellingInvalid && "spelling was invalid after correct parse?");

    // We are no longer at the start of a statement.
    isNewStatement = false;
  }

  // Ensure that the buffer is null-terminated.
  Asm.push_back('\0');
  Asm.pop_back();

  assert(TokOffsets.size() == AsmToks.size());
  return false;
}

// Determine if this is a GCC-style asm statement.
bool Parser::isGCCAsmStatement(const Token &TokAfterAsm) const {
  return TokAfterAsm.is(tok::l_paren) || isGNUAsmQualifier(TokAfterAsm);
}

bool Parser::isGNUAsmQualifier(const Token &TokAfterAsm) const {
  return getGNUAsmQualifier(TokAfterAsm) != GNUAsmQualifiers::AQ_unspecified;
}

/// ParseMicrosoftAsmStatement. When -fms-extensions/-fasm-blocks is enabled,
/// this routine is called to collect the tokens for an MS asm statement.
///
/// [MS]  ms-asm-statement:
///         ms-asm-block
///         ms-asm-block ms-asm-statement
///
/// [MS]  ms-asm-block:
///         '__asm' ms-asm-line '\n'
///         '__asm' '{' ms-asm-instruction-block[opt] '}' ';'[opt]
///
/// [MS]  ms-asm-instruction-block
///         ms-asm-line
///         ms-asm-line '\n' ms-asm-instruction-block
///
StmtResult Parser::ParseMicrosoftAsmStatement(SourceLocation AsmLoc) {
  SourceManager &SrcMgr = PP.getSourceManager();
  SourceLocation EndLoc = AsmLoc;
  SmallVector<Token, 4> AsmToks;

  bool SingleLineMode = true;
  unsigned BraceNesting = 0;
  unsigned short savedBraceCount = BraceCount;
  bool InAsmComment = false;
  FileID FID;
  unsigned LineNo = 0;
  unsigned NumTokensRead = 0;
  SmallVector<SourceLocation, 4> LBraceLocs;
  bool SkippedStartOfLine = false;

  if (Tok.is(tok::l_brace)) {
    // Braced inline asm: consume the opening brace.
    SingleLineMode = false;
    BraceNesting = 1;
    EndLoc = ConsumeBrace();
    LBraceLocs.push_back(EndLoc);
    ++NumTokensRead;
  } else {
    // Single-line inline asm; compute which line it is on.
    std::pair<FileID, unsigned> ExpAsmLoc =
        SrcMgr.getDecomposedExpansionLoc(EndLoc);
    FID = ExpAsmLoc.first;
    LineNo = SrcMgr.getLineNumber(FID, ExpAsmLoc.second);
    LBraceLocs.push_back(SourceLocation());
  }

  SourceLocation TokLoc = Tok.getLocation();
  do {
    // If we hit EOF, we're done, period.
    if (isEofOrEom())
      break;

    if (!InAsmComment && Tok.is(tok::l_brace)) {
      // Consume the opening brace.
      SkippedStartOfLine = Tok.isAtStartOfLine();
      AsmToks.push_back(Tok);
      EndLoc = ConsumeBrace();
      BraceNesting++;
      LBraceLocs.push_back(EndLoc);
      TokLoc = Tok.getLocation();
      ++NumTokensRead;
      continue;
    } else if (!InAsmComment && Tok.is(tok::semi)) {
      // A semicolon in an asm is the start of a comment.
      InAsmComment = true;
      if (!SingleLineMode) {
        // Compute which line the comment is on.
        std::pair<FileID, unsigned> ExpSemiLoc =
            SrcMgr.getDecomposedExpansionLoc(TokLoc);
        FID = ExpSemiLoc.first;
        LineNo = SrcMgr.getLineNumber(FID, ExpSemiLoc.second);
      }
    } else if (SingleLineMode || InAsmComment) {
      // If end-of-line is significant, check whether this token is on a
      // new line.
      std::pair<FileID, unsigned> ExpLoc =
          SrcMgr.getDecomposedExpansionLoc(TokLoc);
      if (ExpLoc.first != FID ||
          SrcMgr.getLineNumber(ExpLoc.first, ExpLoc.second) != LineNo) {
        // If this is a single-line __asm, we're done, except if the next
        // line is MS-style asm too, in which case we finish a comment
        // if needed and then keep processing the next line as a single
        // line __asm.
        bool isAsm = Tok.is(tok::kw_asm);
        if (SingleLineMode && (!isAsm || isGCCAsmStatement(NextToken())))
          break;
        // We're no longer in a comment.
        InAsmComment = false;
        if (isAsm) {
          // If this is a new __asm {} block we want to process it separately
          // from the single-line __asm statements
          if (PP.LookAhead(0).is(tok::l_brace))
            break;
          LineNo = SrcMgr.getLineNumber(ExpLoc.first, ExpLoc.second);
          SkippedStartOfLine = Tok.isAtStartOfLine();
        } else if (Tok.is(tok::semi)) {
          // A multi-line asm-statement, where next line is a comment
          InAsmComment = true;
          FID = ExpLoc.first;
          LineNo = SrcMgr.getLineNumber(FID, ExpLoc.second);
        }
      } else if (!InAsmComment && Tok.is(tok::r_brace)) {
        // In MSVC mode, braces only participate in brace matching and
        // separating the asm statements.  This is an intentional
        // departure from the Apple gcc behavior.
        if (!BraceNesting)
          break;
      }
    }
    if (!InAsmComment && BraceNesting && Tok.is(tok::r_brace) &&
        BraceCount == (savedBraceCount + BraceNesting)) {
      // Consume the closing brace.
      SkippedStartOfLine = Tok.isAtStartOfLine();
      // Don't want to add the closing brace of the whole asm block
      if (SingleLineMode || BraceNesting > 1) {
        Tok.clearFlag(Token::LeadingSpace);
        AsmToks.push_back(Tok);
      }
      EndLoc = ConsumeBrace();
      BraceNesting--;
      // Finish if all of the opened braces in the inline asm section were
      // consumed.
      if (BraceNesting == 0 && !SingleLineMode)
        break;
      else {
        LBraceLocs.pop_back();
        TokLoc = Tok.getLocation();
        ++NumTokensRead;
        continue;
      }
    }

    // Consume the next token; make sure we don't modify the brace count etc.
    // if we are in a comment.
    EndLoc = TokLoc;
    if (InAsmComment)
      PP.Lex(Tok);
    else {
      // Set the token as the start of line if we skipped the original start
      // of line token in case it was a nested brace.
      if (SkippedStartOfLine)
        Tok.setFlag(Token::StartOfLine);
      AsmToks.push_back(Tok);
      ConsumeAnyToken();
    }
    TokLoc = Tok.getLocation();
    ++NumTokensRead;
    SkippedStartOfLine = false;
  } while (true);

  if (BraceNesting && BraceCount != savedBraceCount) {
    // __asm without closing brace (this can happen at EOF).
    for (unsigned i = 0; i < BraceNesting; ++i) {
      Diag(Tok, diag::err_expected) << tok::r_brace;
      Diag(LBraceLocs.back(), diag::note_matching) << tok::l_brace;
      LBraceLocs.pop_back();
    }
    return StmtError();
  } else if (NumTokensRead == 0) {
    // Empty __asm.
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return StmtError();
  }

  // Okay, prepare to use MC to parse the assembly.
  SmallVector<StringRef, 4> ConstraintRefs;
  SmallVector<Expr *, 4> Exprs;
  SmallVector<StringRef, 4> ClobberRefs;

  // We need an actual supported target.
  const llvm::Triple &TheTriple = Actions.Context.getTargetInfo().getTriple();
  const std::string &TT = TheTriple.getTriple();
  const llvm::Target *TheTarget = nullptr;
  if (!TheTriple.isX86()) {
    Diag(AsmLoc, diag::err_msasm_unsupported_arch) << TheTriple.getArchName();
  } else {
    std::string Error;
    TheTarget = llvm::TargetRegistry::lookupTarget(TT, Error);
    if (!TheTarget)
      Diag(AsmLoc, diag::err_msasm_unable_to_create_target) << Error;
  }

  assert(!LBraceLocs.empty() && "Should have at least one location here");

  SmallString<512> AsmString;
  auto EmptyStmt = [&] {
    return Actions.ActOnMSAsmStmt(AsmLoc, LBraceLocs[0], AsmToks, AsmString,
                                  /*NumOutputs*/ 0, /*NumInputs*/ 0,
                                  ConstraintRefs, ClobberRefs, Exprs, EndLoc);
  };
  // If we don't support assembly, or the assembly is empty, we don't
  // need to instantiate the AsmParser, etc.
  if (!TheTarget || AsmToks.empty()) {
    return EmptyStmt();
  }

  // Expand the tokens into a string buffer.
  SmallVector<unsigned, 8> TokOffsets;
  if (buildMSAsmString(PP, AsmLoc, AsmToks, TokOffsets, AsmString))
    return StmtError();

  const TargetOptions &TO = Actions.Context.getTargetInfo().getTargetOpts();
  std::string FeaturesStr =
      llvm::join(TO.Features.begin(), TO.Features.end(), ",");

  std::unique_ptr<llvm::MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TT));
  if (!MRI) {
    Diag(AsmLoc, diag::err_msasm_unable_to_create_target)
        << "target MC unavailable";
    return EmptyStmt();
  }
  // FIXME: init MCOptions from sanitizer flags here.
  llvm::MCTargetOptions MCOptions;
  std::unique_ptr<llvm::MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TT, MCOptions));
  // Get the instruction descriptor.
  std::unique_ptr<llvm::MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  std::unique_ptr<llvm::MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TT, TO.CPU, FeaturesStr));
  // Target MCTargetDesc may not be linked in clang-based tools.

  if (!MAI || !MII || !STI) {
    Diag(AsmLoc, diag::err_msasm_unable_to_create_target)
        << "target MC unavailable";
    return EmptyStmt();
  }

  llvm::SourceMgr TempSrcMgr;
  llvm::MCContext Ctx(TheTriple, MAI.get(), MRI.get(), STI.get(), &TempSrcMgr);
  std::unique_ptr<llvm::MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, /*PIC=*/false));
  Ctx.setObjectFileInfo(MOFI.get());

  std::unique_ptr<llvm::MemoryBuffer> Buffer =
      llvm::MemoryBuffer::getMemBuffer(AsmString, "<MS inline asm>");

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  TempSrcMgr.AddNewSourceBuffer(std::move(Buffer), llvm::SMLoc());

  std::unique_ptr<llvm::MCStreamer> Str(createNullStreamer(Ctx));
  std::unique_ptr<llvm::MCAsmParser> Parser(
      createMCAsmParser(TempSrcMgr, Ctx, *Str.get(), *MAI));

  std::unique_ptr<llvm::MCTargetAsmParser> TargetParser(
      TheTarget->createMCAsmParser(*STI, *Parser, *MII, MCOptions));
  // Target AsmParser may not be linked in clang-based tools.
  if (!TargetParser) {
    Diag(AsmLoc, diag::err_msasm_unable_to_create_target)
        << "target ASM parser unavailable";
    return EmptyStmt();
  }

  std::unique_ptr<llvm::MCInstPrinter> IP(
      TheTarget->createMCInstPrinter(llvm::Triple(TT), 1, *MAI, *MII, *MRI));

  // Change to the Intel dialect.
  Parser->setAssemblerDialect(1);
  Parser->setTargetParser(*TargetParser.get());
  Parser->setParsingMSInlineAsm(true);
  TargetParser->setParsingMSInlineAsm(true);

  ClangAsmParserCallback Callback(*this, AsmLoc, AsmString, AsmToks,
                                  TokOffsets);
  TargetParser->setSemaCallback(&Callback);
  TempSrcMgr.setDiagHandler(ClangAsmParserCallback::DiagHandlerCallback,
                            &Callback);

  unsigned NumOutputs;
  unsigned NumInputs;
  std::string AsmStringIR;
  SmallVector<std::pair<void *, bool>, 4> OpExprs;
  SmallVector<std::string, 4> Constraints;
  SmallVector<std::string, 4> Clobbers;
  if (Parser->parseMSInlineAsm(AsmStringIR, NumOutputs, NumInputs, OpExprs,
                               Constraints, Clobbers, MII.get(), IP.get(),
                               Callback))
    return StmtError();

  // Filter out "fpsw" and "mxcsr". They aren't valid GCC asm clobber
  // constraints. Clang always adds fpsr to the clobber list anyway.
  llvm::erase_if(Clobbers, [](const std::string &C) {
    return C == "fpsr" || C == "mxcsr";
  });

  // Build the vector of clobber StringRefs.
  ClobberRefs.insert(ClobberRefs.end(), Clobbers.begin(), Clobbers.end());

  // Recast the void pointers and build the vector of constraint StringRefs.
  unsigned NumExprs = NumOutputs + NumInputs;
  ConstraintRefs.resize(NumExprs);
  Exprs.resize(NumExprs);
  for (unsigned i = 0, e = NumExprs; i != e; ++i) {
    Expr *OpExpr = static_cast<Expr *>(OpExprs[i].first);
    if (!OpExpr)
      return StmtError();

    // Need address of variable.
    if (OpExprs[i].second)
      OpExpr =
          Actions.BuildUnaryOp(getCurScope(), AsmLoc, UO_AddrOf, OpExpr).get();

    ConstraintRefs[i] = StringRef(Constraints[i]);
    Exprs[i] = OpExpr;
  }

  // FIXME: We should be passing source locations for better diagnostics.
  return Actions.ActOnMSAsmStmt(AsmLoc, LBraceLocs[0], AsmToks, AsmStringIR,
                                NumOutputs, NumInputs, ConstraintRefs,
                                ClobberRefs, Exprs, EndLoc);
}

/// parseGNUAsmQualifierListOpt - Parse a GNU extended asm qualifier list.
///       asm-qualifier:
///         volatile
///         inline
///         goto
///
///       asm-qualifier-list:
///         asm-qualifier
///         asm-qualifier-list asm-qualifier
bool Parser::parseGNUAsmQualifierListOpt(GNUAsmQualifiers &AQ) {
  while (true) {
    const GNUAsmQualifiers::AQ A = getGNUAsmQualifier(Tok);
    if (A == GNUAsmQualifiers::AQ_unspecified) {
      if (Tok.isNot(tok::l_paren)) {
        Diag(Tok.getLocation(), diag::err_asm_qualifier_ignored);
        SkipUntil(tok::r_paren, StopAtSemi);
        return true;
      }
      return false;
    }
    if (AQ.setAsmQualifier(A))
      Diag(Tok.getLocation(), diag::err_asm_duplicate_qual)
          << GNUAsmQualifiers::getQualifierName(A);
    ConsumeToken();
  }
  return false;
}

/// ParseAsmStatement - Parse a GNU extended asm statement.
///       asm-statement:
///         gnu-asm-statement
///         ms-asm-statement
///
/// [GNU] gnu-asm-statement:
///         'asm' asm-qualifier-list[opt] '(' asm-argument ')' ';'
///
/// [GNU] asm-argument:
///         asm-string-literal
///         asm-string-literal ':' asm-operands[opt]
///         asm-string-literal ':' asm-operands[opt] ':' asm-operands[opt]
///         asm-string-literal ':' asm-operands[opt] ':' asm-operands[opt]
///                 ':' asm-clobbers
///
/// [GNU] asm-clobbers:
///         asm-string-literal
///         asm-clobbers ',' asm-string-literal
///
StmtResult Parser::ParseAsmStatement(bool &msAsm) {
  assert(Tok.is(tok::kw_asm) && "Not an asm stmt");
  SourceLocation AsmLoc = ConsumeToken();

  if (getLangOpts().AsmBlocks && !isGCCAsmStatement(Tok)) {
    msAsm = true;
    return ParseMicrosoftAsmStatement(AsmLoc);
  }

  SourceLocation Loc = Tok.getLocation();
  GNUAsmQualifiers GAQ;
  if (parseGNUAsmQualifierListOpt(GAQ))
    return StmtError();

  if (GAQ.isGoto() && getLangOpts().SpeculativeLoadHardening)
    Diag(Loc, diag::warn_slh_does_not_support_asm_goto);

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  ExprResult AsmString(ParseAsmStringLiteral(/*ForAsmLabel*/ false));

  // Check if GNU-style InlineAsm is disabled.
  // Error on anything other than empty string.
  if (!(getLangOpts().GNUAsm || AsmString.isInvalid())) {
    const auto *SL = cast<StringLiteral>(AsmString.get());
    if (!SL->getString().trim().empty())
      Diag(Loc, diag::err_gnu_inline_asm_disabled);
  }

  if (AsmString.isInvalid()) {
    // Consume up to and including the closing paren.
    T.skipToEnd();
    return StmtError();
  }

  SmallVector<IdentifierInfo *, 4> Names;
  ExprVector Constraints;
  ExprVector Exprs;
  ExprVector Clobbers;

  if (Tok.is(tok::r_paren)) {
    // We have a simple asm expression like 'asm("foo")'.
    T.consumeClose();
    return Actions.ActOnGCCAsmStmt(
        AsmLoc, /*isSimple*/ true, GAQ.isVolatile(),
        /*NumOutputs*/ 0, /*NumInputs*/ 0, nullptr, Constraints, Exprs,
        AsmString.get(), Clobbers, /*NumLabels*/ 0, T.getCloseLocation());
  }

  // Parse Outputs, if present.
  bool AteExtraColon = false;
  if (Tok.is(tok::colon) || Tok.is(tok::coloncolon)) {
    // In C++ mode, parse "::" like ": :".
    AteExtraColon = Tok.is(tok::coloncolon);
    ConsumeToken();

    if (!AteExtraColon && ParseAsmOperandsOpt(Names, Constraints, Exprs))
      return StmtError();
  }

  unsigned NumOutputs = Names.size();

  // Parse Inputs, if present.
  if (AteExtraColon || Tok.is(tok::colon) || Tok.is(tok::coloncolon)) {
    // In C++ mode, parse "::" like ": :".
    if (AteExtraColon)
      AteExtraColon = false;
    else {
      AteExtraColon = Tok.is(tok::coloncolon);
      ConsumeToken();
    }

    if (!AteExtraColon && ParseAsmOperandsOpt(Names, Constraints, Exprs))
      return StmtError();
  }

  assert(Names.size() == Constraints.size() &&
         Constraints.size() == Exprs.size() && "Input operand size mismatch!");

  unsigned NumInputs = Names.size() - NumOutputs;

  // Parse the clobbers, if present.
  if (AteExtraColon || Tok.is(tok::colon) || Tok.is(tok::coloncolon)) {
    if (AteExtraColon)
      AteExtraColon = false;
    else {
      AteExtraColon = Tok.is(tok::coloncolon);
      ConsumeToken();
    }
    // Parse the asm-string list for clobbers if present.
    if (!AteExtraColon && isTokenStringLiteral()) {
      while (true) {
        ExprResult Clobber(ParseAsmStringLiteral(/*ForAsmLabel*/ false));

        if (Clobber.isInvalid())
          break;

        Clobbers.push_back(Clobber.get());

        if (!TryConsumeToken(tok::comma))
          break;
      }
    }
  }
  if (!GAQ.isGoto() && (Tok.isNot(tok::r_paren) || AteExtraColon)) {
    Diag(Tok, diag::err_expected) << tok::r_paren;
    SkipUntil(tok::r_paren, StopAtSemi);
    return StmtError();
  }

  // Parse the goto label, if present.
  unsigned NumLabels = 0;
  if (AteExtraColon || Tok.is(tok::colon)) {
    if (!AteExtraColon)
      ConsumeToken();

    while (true) {
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected) << tok::identifier;
        SkipUntil(tok::r_paren, StopAtSemi);
        return StmtError();
      }
      LabelDecl *LD = Actions.LookupOrCreateLabel(Tok.getIdentifierInfo(),
                                                  Tok.getLocation());
      Names.push_back(Tok.getIdentifierInfo());
      if (!LD) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return StmtError();
      }
      ExprResult Res =
          Actions.ActOnAddrLabel(Tok.getLocation(), Tok.getLocation(), LD);
      Exprs.push_back(Res.get());
      NumLabels++;
      ConsumeToken();
      if (!TryConsumeToken(tok::comma))
        break;
    }
  } else if (GAQ.isGoto()) {
    Diag(Tok, diag::err_expected) << tok::colon;
    SkipUntil(tok::r_paren, StopAtSemi);
    return StmtError();
  }
  T.consumeClose();
  return Actions.ActOnGCCAsmStmt(AsmLoc, false, GAQ.isVolatile(), NumOutputs,
                                 NumInputs, Names.data(), Constraints, Exprs,
                                 AsmString.get(), Clobbers, NumLabels,
                                 T.getCloseLocation());
}

/// ParseAsmOperands - Parse the asm-operands production as used by
/// asm-statement, assuming the leading ':' token was eaten.
///
/// [GNU] asm-operands:
///         asm-operand
///         asm-operands ',' asm-operand
///
/// [GNU] asm-operand:
///         asm-string-literal '(' expression ')'
///         '[' identifier ']' asm-string-literal '(' expression ')'
///
//
// FIXME: Avoid unnecessary std::string trashing.
bool Parser::ParseAsmOperandsOpt(SmallVectorImpl<IdentifierInfo *> &Names,
                                 SmallVectorImpl<Expr *> &Constraints,
                                 SmallVectorImpl<Expr *> &Exprs) {
  // 'asm-operands' isn't present?
  if (!isTokenStringLiteral() && Tok.isNot(tok::l_square))
    return false;

  while (true) {
    // Read the [id] if present.
    if (Tok.is(tok::l_square)) {
      BalancedDelimiterTracker T(*this, tok::l_square);
      T.consumeOpen();

      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected) << tok::identifier;
        SkipUntil(tok::r_paren, StopAtSemi);
        return true;
      }

      IdentifierInfo *II = Tok.getIdentifierInfo();
      ConsumeToken();

      Names.push_back(II);
      T.consumeClose();
    } else
      Names.push_back(nullptr);

    ExprResult Constraint(ParseAsmStringLiteral(/*ForAsmLabel*/ false));
    if (Constraint.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return true;
    }
    Constraints.push_back(Constraint.get());

    if (Tok.isNot(tok::l_paren)) {
      Diag(Tok, diag::err_expected_lparen_after) << "asm operand";
      SkipUntil(tok::r_paren, StopAtSemi);
      return true;
    }

    // Read the parenthesized expression.
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    ExprResult Res = Actions.CorrectDelayedTyposInExpr(ParseExpression());
    T.consumeClose();
    if (Res.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return true;
    }
    Exprs.push_back(Res.get());
    // Eat the comma and continue parsing if it exists.
    if (!TryConsumeToken(tok::comma))
      return false;
  }
}

const char *Parser::GNUAsmQualifiers::getQualifierName(AQ Qualifier) {
  switch (Qualifier) {
    case AQ_volatile: return "volatile";
    case AQ_inline: return "inline";
    case AQ_goto: return "goto";
    case AQ_unspecified: return "unspecified";
  }
  llvm_unreachable("Unknown GNUAsmQualifier");
}

Parser::GNUAsmQualifiers::AQ
Parser::getGNUAsmQualifier(const Token &Tok) const {
  switch (Tok.getKind()) {
    case tok::kw_volatile: return GNUAsmQualifiers::AQ_volatile;
    case tok::kw_inline: return GNUAsmQualifiers::AQ_inline;
    case tok::kw_goto: return GNUAsmQualifiers::AQ_goto;
    default: return GNUAsmQualifiers::AQ_unspecified;
  }
}
bool Parser::GNUAsmQualifiers::setAsmQualifier(AQ Qualifier) {
  bool IsDuplicate = Qualifiers & Qualifier;
  Qualifiers |= Qualifier;
  return IsDuplicate;
}
