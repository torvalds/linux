//===- TGLexer.cpp - Lexer for TableGen -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implement the Lexer for TableGen.
//
//===----------------------------------------------------------------------===//

#include "TGLexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h" // for strtoull()/strtoll() define
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/TableGen/Error.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace llvm;

namespace {
// A list of supported preprocessing directives with their
// internal token kinds and names.
struct {
  tgtok::TokKind Kind;
  const char *Word;
} PreprocessorDirs[] = {
  { tgtok::Ifdef, "ifdef" },
  { tgtok::Ifndef, "ifndef" },
  { tgtok::Else, "else" },
  { tgtok::Endif, "endif" },
  { tgtok::Define, "define" }
};
} // end anonymous namespace

TGLexer::TGLexer(SourceMgr &SM, ArrayRef<std::string> Macros) : SrcMgr(SM) {
  CurBuffer = SrcMgr.getMainFileID();
  CurBuf = SrcMgr.getMemoryBuffer(CurBuffer)->getBuffer();
  CurPtr = CurBuf.begin();
  TokStart = nullptr;

  // Pretend that we enter the "top-level" include file.
  PrepIncludeStack.push_back(
      std::make_unique<std::vector<PreprocessorControlDesc>>());

  // Put all macros defined in the command line into the DefinedMacros set.
  for (const std::string &MacroName : Macros)
    DefinedMacros.insert(MacroName);
}

SMLoc TGLexer::getLoc() const {
  return SMLoc::getFromPointer(TokStart);
}

SMRange TGLexer::getLocRange() const {
  return {getLoc(), SMLoc::getFromPointer(CurPtr)};
}

/// ReturnError - Set the error to the specified string at the specified
/// location.  This is defined to always return tgtok::Error.
tgtok::TokKind TGLexer::ReturnError(SMLoc Loc, const Twine &Msg) {
  PrintError(Loc, Msg);
  return tgtok::Error;
}

tgtok::TokKind TGLexer::ReturnError(const char *Loc, const Twine &Msg) {
  return ReturnError(SMLoc::getFromPointer(Loc), Msg);
}

bool TGLexer::processEOF() {
  SMLoc ParentIncludeLoc = SrcMgr.getParentIncludeLoc(CurBuffer);
  if (ParentIncludeLoc != SMLoc()) {
    // If prepExitInclude() detects a problem with the preprocessing
    // control stack, it will return false.  Pretend that we reached
    // the final EOF and stop lexing more tokens by returning false
    // to LexToken().
    if (!prepExitInclude(false))
      return false;

    CurBuffer = SrcMgr.FindBufferContainingLoc(ParentIncludeLoc);
    CurBuf = SrcMgr.getMemoryBuffer(CurBuffer)->getBuffer();
    CurPtr = ParentIncludeLoc.getPointer();
    // Make sure TokStart points into the parent file's buffer.
    // LexToken() assigns to it before calling getNextChar(),
    // so it is pointing into the included file now.
    TokStart = CurPtr;
    return true;
  }

  // Pretend that we exit the "top-level" include file.
  // Note that in case of an error (e.g. control stack imbalance)
  // the routine will issue a fatal error.
  prepExitInclude(true);
  return false;
}

int TGLexer::getNextChar() {
  char CurChar = *CurPtr++;
  switch (CurChar) {
  default:
    return (unsigned char)CurChar;

  case 0: {
    // A NUL character in the stream is either the end of the current buffer or
    // a spurious NUL in the file.  Disambiguate that here.
    if (CurPtr - 1 == CurBuf.end()) {
      --CurPtr; // Arrange for another call to return EOF again.
      return EOF;
    }
    PrintError(getLoc(),
               "NUL character is invalid in source; treated as space");
    return ' ';
  }

  case '\n':
  case '\r':
    // Handle the newline character by ignoring it and incrementing the line
    // count.  However, be careful about 'dos style' files with \n\r in them.
    // Only treat a \n\r or \r\n as a single line.
    if ((*CurPtr == '\n' || (*CurPtr == '\r')) &&
        *CurPtr != CurChar)
      ++CurPtr;  // Eat the two char newline sequence.
    return '\n';
  }
}

int TGLexer::peekNextChar(int Index) const {
  return *(CurPtr + Index);
}

tgtok::TokKind TGLexer::LexToken(bool FileOrLineStart) {
  TokStart = CurPtr;
  // This always consumes at least one character.
  int CurChar = getNextChar();

  switch (CurChar) {
  default:
    // Handle letters: [a-zA-Z_]
    if (isalpha(CurChar) || CurChar == '_')
      return LexIdentifier();

    // Unknown character, emit an error.
    return ReturnError(TokStart, "Unexpected character");
  case EOF:
    // Lex next token, if we just left an include file.
    // Note that leaving an include file means that the next
    // symbol is located at the end of the 'include "..."'
    // construct, so LexToken() is called with default
    // false parameter.
    if (processEOF())
      return LexToken();

    // Return EOF denoting the end of lexing.
    return tgtok::Eof;

  case ':': return tgtok::colon;
  case ';': return tgtok::semi;
  case ',': return tgtok::comma;
  case '<': return tgtok::less;
  case '>': return tgtok::greater;
  case ']': return tgtok::r_square;
  case '{': return tgtok::l_brace;
  case '}': return tgtok::r_brace;
  case '(': return tgtok::l_paren;
  case ')': return tgtok::r_paren;
  case '=': return tgtok::equal;
  case '?': return tgtok::question;
  case '#':
    if (FileOrLineStart) {
      tgtok::TokKind Kind = prepIsDirective();
      if (Kind != tgtok::Error)
        return lexPreprocessor(Kind);
    }

    return tgtok::paste;

  // The period is a separate case so we can recognize the "..."
  // range punctuator.
  case '.':
    if (peekNextChar(0) == '.') {
      ++CurPtr; // Eat second dot.
      if (peekNextChar(0) == '.') {
        ++CurPtr; // Eat third dot.
        return tgtok::dotdotdot;
      }
      return ReturnError(TokStart, "Invalid '..' punctuation");
    }
    return tgtok::dot;

  case '\r':
    PrintFatalError("getNextChar() must never return '\r'");
    return tgtok::Error;

  case ' ':
  case '\t':
    // Ignore whitespace.
    return LexToken(FileOrLineStart);
  case '\n':
    // Ignore whitespace, and identify the new line.
    return LexToken(true);
  case '/':
    // If this is the start of a // comment, skip until the end of the line or
    // the end of the buffer.
    if (*CurPtr == '/')
      SkipBCPLComment();
    else if (*CurPtr == '*') {
      if (SkipCComment())
        return tgtok::Error;
    } else // Otherwise, this is an error.
      return ReturnError(TokStart, "Unexpected character");
    return LexToken(FileOrLineStart);
  case '-': case '+':
  case '0': case '1': case '2': case '3': case '4': case '5': case '6':
  case '7': case '8': case '9': {
    int NextChar = 0;
    if (isdigit(CurChar)) {
      // Allow identifiers to start with a number if it is followed by
      // an identifier.  This can happen with paste operations like
      // foo#8i.
      int i = 0;
      do {
        NextChar = peekNextChar(i++);
      } while (isdigit(NextChar));

      if (NextChar == 'x' || NextChar == 'b') {
        // If this is [0-9]b[01] or [0-9]x[0-9A-fa-f] this is most
        // likely a number.
        int NextNextChar = peekNextChar(i);
        switch (NextNextChar) {
        default:
          break;
        case '0': case '1':
          if (NextChar == 'b')
            return LexNumber();
          [[fallthrough]];
        case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
          if (NextChar == 'x')
            return LexNumber();
          break;
        }
      }
    }

    if (isalpha(NextChar) || NextChar == '_')
      return LexIdentifier();

    return LexNumber();
  }
  case '"': return LexString();
  case '$': return LexVarName();
  case '[': return LexBracket();
  case '!': return LexExclaim();
  }
}

/// LexString - Lex "[^"]*"
tgtok::TokKind TGLexer::LexString() {
  const char *StrStart = CurPtr;

  CurStrVal = "";

  while (*CurPtr != '"') {
    // If we hit the end of the buffer, report an error.
    if (*CurPtr == 0 && CurPtr == CurBuf.end())
      return ReturnError(StrStart, "End of file in string literal");

    if (*CurPtr == '\n' || *CurPtr == '\r')
      return ReturnError(StrStart, "End of line in string literal");

    if (*CurPtr != '\\') {
      CurStrVal += *CurPtr++;
      continue;
    }

    ++CurPtr;

    switch (*CurPtr) {
    case '\\': case '\'': case '"':
      // These turn into their literal character.
      CurStrVal += *CurPtr++;
      break;
    case 't':
      CurStrVal += '\t';
      ++CurPtr;
      break;
    case 'n':
      CurStrVal += '\n';
      ++CurPtr;
      break;

    case '\n':
    case '\r':
      return ReturnError(CurPtr, "escaped newlines not supported in tblgen");

    // If we hit the end of the buffer, report an error.
    case '\0':
      if (CurPtr == CurBuf.end())
        return ReturnError(StrStart, "End of file in string literal");
      [[fallthrough]];
    default:
      return ReturnError(CurPtr, "invalid escape in string literal");
    }
  }

  ++CurPtr;
  return tgtok::StrVal;
}

tgtok::TokKind TGLexer::LexVarName() {
  if (!isalpha(CurPtr[0]) && CurPtr[0] != '_')
    return ReturnError(TokStart, "Invalid variable name");

  // Otherwise, we're ok, consume the rest of the characters.
  const char *VarNameStart = CurPtr++;

  while (isalpha(*CurPtr) || isdigit(*CurPtr) || *CurPtr == '_')
    ++CurPtr;

  CurStrVal.assign(VarNameStart, CurPtr);
  return tgtok::VarName;
}

tgtok::TokKind TGLexer::LexIdentifier() {
  // The first letter is [a-zA-Z_].
  const char *IdentStart = TokStart;

  // Match the rest of the identifier regex: [0-9a-zA-Z_]*
  while (isalpha(*CurPtr) || isdigit(*CurPtr) || *CurPtr == '_')
    ++CurPtr;

  // Check to see if this identifier is a reserved keyword.
  StringRef Str(IdentStart, CurPtr-IdentStart);

  tgtok::TokKind Kind = StringSwitch<tgtok::TokKind>(Str)
                            .Case("int", tgtok::Int)
                            .Case("bit", tgtok::Bit)
                            .Case("bits", tgtok::Bits)
                            .Case("string", tgtok::String)
                            .Case("list", tgtok::List)
                            .Case("code", tgtok::Code)
                            .Case("dag", tgtok::Dag)
                            .Case("class", tgtok::Class)
                            .Case("def", tgtok::Def)
                            .Case("true", tgtok::TrueVal)
                            .Case("false", tgtok::FalseVal)
                            .Case("foreach", tgtok::Foreach)
                            .Case("defm", tgtok::Defm)
                            .Case("defset", tgtok::Defset)
                            .Case("deftype", tgtok::Deftype)
                            .Case("multiclass", tgtok::MultiClass)
                            .Case("field", tgtok::Field)
                            .Case("let", tgtok::Let)
                            .Case("in", tgtok::In)
                            .Case("defvar", tgtok::Defvar)
                            .Case("include", tgtok::Include)
                            .Case("if", tgtok::If)
                            .Case("then", tgtok::Then)
                            .Case("else", tgtok::ElseKW)
                            .Case("assert", tgtok::Assert)
                            .Case("dump", tgtok::Dump)
                            .Default(tgtok::Id);

  // A couple of tokens require special processing.
  switch (Kind) {
    case tgtok::Include:
      if (LexInclude()) return tgtok::Error;
      return Lex();
    case tgtok::Id:
      CurStrVal.assign(Str.begin(), Str.end());
      break;
    default:
      break;
  }

  return Kind;
}

/// LexInclude - We just read the "include" token.  Get the string token that
/// comes next and enter the include.
bool TGLexer::LexInclude() {
  // The token after the include must be a string.
  tgtok::TokKind Tok = LexToken();
  if (Tok == tgtok::Error) return true;
  if (Tok != tgtok::StrVal) {
    PrintError(getLoc(), "Expected filename after include");
    return true;
  }

  // Get the string.
  std::string Filename = CurStrVal;
  std::string IncludedFile;

  CurBuffer = SrcMgr.AddIncludeFile(Filename, SMLoc::getFromPointer(CurPtr),
                                    IncludedFile);
  if (!CurBuffer) {
    PrintError(getLoc(), "Could not find include file '" + Filename + "'");
    return true;
  }

  Dependencies.insert(IncludedFile);
  // Save the line number and lex buffer of the includer.
  CurBuf = SrcMgr.getMemoryBuffer(CurBuffer)->getBuffer();
  CurPtr = CurBuf.begin();

  PrepIncludeStack.push_back(
      std::make_unique<std::vector<PreprocessorControlDesc>>());
  return false;
}

/// SkipBCPLComment - Skip over the comment by finding the next CR or LF.
/// Or we may end up at the end of the buffer.
void TGLexer::SkipBCPLComment() {
  ++CurPtr;  // skip the second slash.
  auto EOLPos = CurBuf.find_first_of("\r\n", CurPtr - CurBuf.data());
  CurPtr = (EOLPos == StringRef::npos) ? CurBuf.end() : CurBuf.data() + EOLPos;
}

/// SkipCComment - This skips C-style /**/ comments.  The only difference from C
/// is that we allow nesting.
bool TGLexer::SkipCComment() {
  ++CurPtr;  // skip the star.
  unsigned CommentDepth = 1;

  while (true) {
    int CurChar = getNextChar();
    switch (CurChar) {
    case EOF:
      PrintError(TokStart, "Unterminated comment!");
      return true;
    case '*':
      // End of the comment?
      if (CurPtr[0] != '/') break;

      ++CurPtr;   // End the */.
      if (--CommentDepth == 0)
        return false;
      break;
    case '/':
      // Start of a nested comment?
      if (CurPtr[0] != '*') break;
      ++CurPtr;
      ++CommentDepth;
      break;
    }
  }
}

/// LexNumber - Lex:
///    [-+]?[0-9]+
///    0x[0-9a-fA-F]+
///    0b[01]+
tgtok::TokKind TGLexer::LexNumber() {
  unsigned Base = 0;
  const char *NumStart;

  // Check if it's a hex or a binary value.
  if (CurPtr[-1] == '0') {
    NumStart = CurPtr + 1;
    if (CurPtr[0] == 'x') {
      Base = 16;
      do
        ++CurPtr;
      while (isxdigit(CurPtr[0]));
    } else if (CurPtr[0] == 'b') {
      Base = 2;
      do
        ++CurPtr;
      while (CurPtr[0] == '0' || CurPtr[0] == '1');
    }
  }

  // For a hex or binary value, we always convert it to an unsigned value.
  bool IsMinus = false;

  // Check if it's a decimal value.
  if (Base == 0) {
    // Check for a sign without a digit.
    if (!isdigit(CurPtr[0])) {
      if (CurPtr[-1] == '-')
        return tgtok::minus;
      else if (CurPtr[-1] == '+')
        return tgtok::plus;
    }

    Base = 10;
    NumStart = TokStart;
    IsMinus = CurPtr[-1] == '-';

    while (isdigit(CurPtr[0]))
      ++CurPtr;
  }

  // Requires at least one digit.
  if (CurPtr == NumStart)
    return ReturnError(TokStart, "Invalid number");

  errno = 0;
  if (IsMinus)
    CurIntVal = strtoll(NumStart, nullptr, Base);
  else
    CurIntVal = strtoull(NumStart, nullptr, Base);

  if (errno == EINVAL)
    return ReturnError(TokStart, "Invalid number");
  if (errno == ERANGE)
    return ReturnError(TokStart, "Number out of range");

  return Base == 2 ? tgtok::BinaryIntVal : tgtok::IntVal;
}

/// LexBracket - We just read '['.  If this is a code block, return it,
/// otherwise return the bracket.  Match: '[' and '[{ ( [^}]+ | }[^]] )* }]'
tgtok::TokKind TGLexer::LexBracket() {
  if (CurPtr[0] != '{')
    return tgtok::l_square;
  ++CurPtr;
  const char *CodeStart = CurPtr;
  while (true) {
    int Char = getNextChar();
    if (Char == EOF) break;

    if (Char != '}') continue;

    Char = getNextChar();
    if (Char == EOF) break;
    if (Char == ']') {
      CurStrVal.assign(CodeStart, CurPtr-2);
      return tgtok::CodeFragment;
    }
  }

  return ReturnError(CodeStart - 2, "Unterminated code block");
}

/// LexExclaim - Lex '!' and '![a-zA-Z]+'.
tgtok::TokKind TGLexer::LexExclaim() {
  if (!isalpha(*CurPtr))
    return ReturnError(CurPtr - 1, "Invalid \"!operator\"");

  const char *Start = CurPtr++;
  while (isalpha(*CurPtr))
    ++CurPtr;

  // Check to see which operator this is.
  tgtok::TokKind Kind =
      StringSwitch<tgtok::TokKind>(StringRef(Start, CurPtr - Start))
          .Case("eq", tgtok::XEq)
          .Case("ne", tgtok::XNe)
          .Case("le", tgtok::XLe)
          .Case("lt", tgtok::XLt)
          .Case("ge", tgtok::XGe)
          .Case("gt", tgtok::XGt)
          .Case("if", tgtok::XIf)
          .Case("cond", tgtok::XCond)
          .Case("isa", tgtok::XIsA)
          .Case("head", tgtok::XHead)
          .Case("tail", tgtok::XTail)
          .Case("size", tgtok::XSize)
          .Case("con", tgtok::XConcat)
          .Case("dag", tgtok::XDag)
          .Case("add", tgtok::XADD)
          .Case("sub", tgtok::XSUB)
          .Case("mul", tgtok::XMUL)
          .Case("div", tgtok::XDIV)
          .Case("not", tgtok::XNOT)
          .Case("logtwo", tgtok::XLOG2)
          .Case("and", tgtok::XAND)
          .Case("or", tgtok::XOR)
          .Case("xor", tgtok::XXOR)
          .Case("shl", tgtok::XSHL)
          .Case("sra", tgtok::XSRA)
          .Case("srl", tgtok::XSRL)
          .Case("cast", tgtok::XCast)
          .Case("empty", tgtok::XEmpty)
          .Case("subst", tgtok::XSubst)
          .Case("foldl", tgtok::XFoldl)
          .Case("foreach", tgtok::XForEach)
          .Case("filter", tgtok::XFilter)
          .Case("listconcat", tgtok::XListConcat)
          .Case("listsplat", tgtok::XListSplat)
          .Case("listremove", tgtok::XListRemove)
          .Case("range", tgtok::XRange)
          .Case("strconcat", tgtok::XStrConcat)
          .Case("interleave", tgtok::XInterleave)
          .Case("substr", tgtok::XSubstr)
          .Case("find", tgtok::XFind)
          .Cases("setdagop", "setop", tgtok::XSetDagOp) // !setop is deprecated.
          .Cases("getdagop", "getop", tgtok::XGetDagOp) // !getop is deprecated.
          .Case("getdagarg", tgtok::XGetDagArg)
          .Case("getdagname", tgtok::XGetDagName)
          .Case("setdagarg", tgtok::XSetDagArg)
          .Case("setdagname", tgtok::XSetDagName)
          .Case("exists", tgtok::XExists)
          .Case("tolower", tgtok::XToLower)
          .Case("toupper", tgtok::XToUpper)
          .Case("repr", tgtok::XRepr)
          .Default(tgtok::Error);

  return Kind != tgtok::Error ? Kind : ReturnError(Start-1, "Unknown operator");
}

bool TGLexer::prepExitInclude(bool IncludeStackMustBeEmpty) {
  // Report an error, if preprocessor control stack for the current
  // file is not empty.
  if (!PrepIncludeStack.back()->empty()) {
    prepReportPreprocessorStackError();

    return false;
  }

  // Pop the preprocessing controls from the include stack.
  if (PrepIncludeStack.empty()) {
    PrintFatalError("Preprocessor include stack is empty");
  }

  PrepIncludeStack.pop_back();

  if (IncludeStackMustBeEmpty) {
    if (!PrepIncludeStack.empty())
      PrintFatalError("Preprocessor include stack is not empty");
  } else {
    if (PrepIncludeStack.empty())
      PrintFatalError("Preprocessor include stack is empty");
  }

  return true;
}

tgtok::TokKind TGLexer::prepIsDirective() const {
  for (const auto &PD : PreprocessorDirs) {
    int NextChar = *CurPtr;
    bool Match = true;
    unsigned I = 0;
    for (; I < strlen(PD.Word); ++I) {
      if (NextChar != PD.Word[I]) {
        Match = false;
        break;
      }

      NextChar = peekNextChar(I + 1);
    }

    // Check for whitespace after the directive.  If there is no whitespace,
    // then we do not recognize it as a preprocessing directive.
    if (Match) {
      tgtok::TokKind Kind = PD.Kind;

      // New line and EOF may follow only #else/#endif.  It will be reported
      // as an error for #ifdef/#define after the call to prepLexMacroName().
      if (NextChar == ' ' || NextChar == '\t' || NextChar == EOF ||
          NextChar == '\n' ||
          // It looks like TableGen does not support '\r' as the actual
          // carriage return, e.g. getNextChar() treats a single '\r'
          // as '\n'.  So we do the same here.
          NextChar == '\r')
        return Kind;

      // Allow comments after some directives, e.g.:
      //     #else// OR #else/**/
      //     #endif// OR #endif/**/
      //
      // Note that we do allow comments after #ifdef/#define here, e.g.
      //     #ifdef/**/ AND #ifdef//
      //     #define/**/ AND #define//
      //
      // These cases will be reported as incorrect after calling
      // prepLexMacroName().  We could have supported C-style comments
      // after #ifdef/#define, but this would complicate the code
      // for little benefit.
      if (NextChar == '/') {
        NextChar = peekNextChar(I + 1);

        if (NextChar == '*' || NextChar == '/')
          return Kind;

        // Pretend that we do not recognize the directive.
      }
    }
  }

  return tgtok::Error;
}

bool TGLexer::prepEatPreprocessorDirective(tgtok::TokKind Kind) {
  TokStart = CurPtr;

  for (const auto &PD : PreprocessorDirs)
    if (PD.Kind == Kind) {
      // Advance CurPtr to the end of the preprocessing word.
      CurPtr += strlen(PD.Word);
      return true;
    }

  PrintFatalError("Unsupported preprocessing token in "
                  "prepEatPreprocessorDirective()");
  return false;
}

tgtok::TokKind TGLexer::lexPreprocessor(
    tgtok::TokKind Kind, bool ReturnNextLiveToken) {

  // We must be looking at a preprocessing directive.  Eat it!
  if (!prepEatPreprocessorDirective(Kind))
    PrintFatalError("lexPreprocessor() called for unknown "
                    "preprocessor directive");

  if (Kind == tgtok::Ifdef || Kind == tgtok::Ifndef) {
    StringRef MacroName = prepLexMacroName();
    StringRef IfTokName = Kind == tgtok::Ifdef ? "#ifdef" : "#ifndef";
    if (MacroName.empty())
      return ReturnError(TokStart, "Expected macro name after " + IfTokName);

    bool MacroIsDefined = DefinedMacros.count(MacroName) != 0;

    // Canonicalize ifndef's MacroIsDefined to its ifdef equivalent.
    if (Kind == tgtok::Ifndef)
      MacroIsDefined = !MacroIsDefined;

    // Regardless of whether we are processing tokens or not,
    // we put the #ifdef control on stack.
    // Note that MacroIsDefined has been canonicalized against ifdef.
    PrepIncludeStack.back()->push_back(
        {tgtok::Ifdef, MacroIsDefined, SMLoc::getFromPointer(TokStart)});

    if (!prepSkipDirectiveEnd())
      return ReturnError(CurPtr, "Only comments are supported after " +
                                     IfTokName + " NAME");

    // If we were not processing tokens before this #ifdef,
    // then just return back to the lines skipping code.
    if (!ReturnNextLiveToken)
      return Kind;

    // If we were processing tokens before this #ifdef,
    // and the macro is defined, then just return the next token.
    if (MacroIsDefined)
      return LexToken();

    // We were processing tokens before this #ifdef, and the macro
    // is not defined, so we have to start skipping the lines.
    // If the skipping is successful, it will return the token following
    // either #else or #endif corresponding to this #ifdef.
    if (prepSkipRegion(ReturnNextLiveToken))
      return LexToken();

    return tgtok::Error;
  } else if (Kind == tgtok::Else) {
    // Check if this #else is correct before calling prepSkipDirectiveEnd(),
    // which will move CurPtr away from the beginning of #else.
    if (PrepIncludeStack.back()->empty())
      return ReturnError(TokStart, "#else without #ifdef or #ifndef");

    PreprocessorControlDesc IfdefEntry = PrepIncludeStack.back()->back();

    if (IfdefEntry.Kind != tgtok::Ifdef) {
      PrintError(TokStart, "double #else");
      return ReturnError(IfdefEntry.SrcPos, "Previous #else is here");
    }

    // Replace the corresponding #ifdef's control with its negation
    // on the control stack.
    PrepIncludeStack.back()->pop_back();
    PrepIncludeStack.back()->push_back(
        {Kind, !IfdefEntry.IsDefined, SMLoc::getFromPointer(TokStart)});

    if (!prepSkipDirectiveEnd())
      return ReturnError(CurPtr, "Only comments are supported after #else");

    // If we were processing tokens before this #else,
    // we have to start skipping lines until the matching #endif.
    if (ReturnNextLiveToken) {
      if (prepSkipRegion(ReturnNextLiveToken))
        return LexToken();

      return tgtok::Error;
    }

    // Return to the lines skipping code.
    return Kind;
  } else if (Kind == tgtok::Endif) {
    // Check if this #endif is correct before calling prepSkipDirectiveEnd(),
    // which will move CurPtr away from the beginning of #endif.
    if (PrepIncludeStack.back()->empty())
      return ReturnError(TokStart, "#endif without #ifdef");

    auto &IfdefOrElseEntry = PrepIncludeStack.back()->back();

    if (IfdefOrElseEntry.Kind != tgtok::Ifdef &&
        IfdefOrElseEntry.Kind != tgtok::Else) {
      PrintFatalError("Invalid preprocessor control on the stack");
      return tgtok::Error;
    }

    if (!prepSkipDirectiveEnd())
      return ReturnError(CurPtr, "Only comments are supported after #endif");

    PrepIncludeStack.back()->pop_back();

    // If we were processing tokens before this #endif, then
    // we should continue it.
    if (ReturnNextLiveToken) {
      return LexToken();
    }

    // Return to the lines skipping code.
    return Kind;
  } else if (Kind == tgtok::Define) {
    StringRef MacroName = prepLexMacroName();
    if (MacroName.empty())
      return ReturnError(TokStart, "Expected macro name after #define");

    if (!DefinedMacros.insert(MacroName).second)
      PrintWarning(getLoc(),
                   "Duplicate definition of macro: " + Twine(MacroName));

    if (!prepSkipDirectiveEnd())
      return ReturnError(CurPtr,
                         "Only comments are supported after #define NAME");

    if (!ReturnNextLiveToken) {
      PrintFatalError("#define must be ignored during the lines skipping");
      return tgtok::Error;
    }

    return LexToken();
  }

  PrintFatalError("Preprocessing directive is not supported");
  return tgtok::Error;
}

bool TGLexer::prepSkipRegion(bool MustNeverBeFalse) {
  if (!MustNeverBeFalse)
    PrintFatalError("Invalid recursion.");

  do {
    // Skip all symbols to the line end.
    while (*CurPtr != '\n')
      ++CurPtr;

    // Find the first non-whitespace symbol in the next line(s).
    if (!prepSkipLineBegin())
      return false;

    // If the first non-blank/comment symbol on the line is '#',
    // it may be a start of preprocessing directive.
    //
    // If it is not '#' just go to the next line.
    if (*CurPtr == '#')
      ++CurPtr;
    else
      continue;

    tgtok::TokKind Kind = prepIsDirective();

    // If we did not find a preprocessing directive or it is #define,
    // then just skip to the next line.  We do not have to do anything
    // for #define in the line-skipping mode.
    if (Kind == tgtok::Error || Kind == tgtok::Define)
      continue;

    tgtok::TokKind ProcessedKind = lexPreprocessor(Kind, false);

    // If lexPreprocessor() encountered an error during lexing this
    // preprocessor idiom, then return false to the calling lexPreprocessor().
    // This will force tgtok::Error to be returned to the tokens processing.
    if (ProcessedKind == tgtok::Error)
      return false;

    if (Kind != ProcessedKind)
      PrintFatalError("prepIsDirective() and lexPreprocessor() "
                      "returned different token kinds");

    // If this preprocessing directive enables tokens processing,
    // then return to the lexPreprocessor() and get to the next token.
    // We can move from line-skipping mode to processing tokens only
    // due to #else or #endif.
    if (prepIsProcessingEnabled()) {
      if (Kind != tgtok::Else && Kind != tgtok::Endif) {
        PrintFatalError("Tokens processing was enabled by an unexpected "
                        "preprocessing directive");
        return false;
      }

      return true;
    }
  } while (CurPtr != CurBuf.end());

  // We have reached the end of the file, but never left the lines-skipping
  // mode.  This means there is no matching #endif.
  prepReportPreprocessorStackError();
  return false;
}

StringRef TGLexer::prepLexMacroName() {
  // Skip whitespaces between the preprocessing directive and the macro name.
  while (*CurPtr == ' ' || *CurPtr == '\t')
    ++CurPtr;

  TokStart = CurPtr;
  // Macro names start with [a-zA-Z_].
  if (*CurPtr != '_' && !isalpha(*CurPtr))
    return "";

  // Match the rest of the identifier regex: [0-9a-zA-Z_]*
  while (isalpha(*CurPtr) || isdigit(*CurPtr) || *CurPtr == '_')
    ++CurPtr;

  return StringRef(TokStart, CurPtr - TokStart);
}

bool TGLexer::prepSkipLineBegin() {
  while (CurPtr != CurBuf.end()) {
    switch (*CurPtr) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
      break;

    case '/': {
      int NextChar = peekNextChar(1);
      if (NextChar == '*') {
        // Skip C-style comment.
        // Note that we do not care about skipping the C++-style comments.
        // If the line contains "//", it may not contain any processable
        // preprocessing directive.  Just return CurPtr pointing to
        // the first '/' in this case.  We also do not care about
        // incorrect symbols after the first '/' - we are in lines-skipping
        // mode, so incorrect code is allowed to some extent.

        // Set TokStart to the beginning of the comment to enable proper
        // diagnostic printing in case of error in SkipCComment().
        TokStart = CurPtr;

        // CurPtr must point to '*' before call to SkipCComment().
        ++CurPtr;
        if (SkipCComment())
          return false;
      } else {
        // CurPtr points to the non-whitespace '/'.
        return true;
      }

      // We must not increment CurPtr after the comment was lexed.
      continue;
    }

    default:
      return true;
    }

    ++CurPtr;
  }

  // We have reached the end of the file.  Return to the lines skipping
  // code, and allow it to handle the EOF as needed.
  return true;
}

bool TGLexer::prepSkipDirectiveEnd() {
  while (CurPtr != CurBuf.end()) {
    switch (*CurPtr) {
    case ' ':
    case '\t':
      break;

    case '\n':
    case '\r':
      return true;

    case '/': {
      int NextChar = peekNextChar(1);
      if (NextChar == '/') {
        // Skip C++-style comment.
        // We may just return true now, but let's skip to the line/buffer end
        // to simplify the method specification.
        ++CurPtr;
        SkipBCPLComment();
      } else if (NextChar == '*') {
        // When we are skipping C-style comment at the end of a preprocessing
        // directive, we can skip several lines.  If any meaningful TD token
        // follows the end of the C-style comment on the same line, it will
        // be considered as an invalid usage of TD token.
        // For example, we want to forbid usages like this one:
        //     #define MACRO class Class {}
        // But with C-style comments we also disallow the following:
        //     #define MACRO /* This macro is used
        //                      to ... */ class Class {}
        // One can argue that this should be allowed, but it does not seem
        // to be worth of the complication.  Moreover, this matches
        // the C preprocessor behavior.

        // Set TokStart to the beginning of the comment to enable proper
        // diagnostic printer in case of error in SkipCComment().
        TokStart = CurPtr;
        ++CurPtr;
        if (SkipCComment())
          return false;
      } else {
        TokStart = CurPtr;
        PrintError(CurPtr, "Unexpected character");
        return false;
      }

      // We must not increment CurPtr after the comment was lexed.
      continue;
    }

    default:
      // Do not allow any non-whitespaces after the directive.
      TokStart = CurPtr;
      return false;
    }

    ++CurPtr;
  }

  return true;
}

bool TGLexer::prepIsProcessingEnabled() {
  for (const PreprocessorControlDesc &I :
       llvm::reverse(*PrepIncludeStack.back()))
    if (!I.IsDefined)
      return false;

  return true;
}

void TGLexer::prepReportPreprocessorStackError() {
  if (PrepIncludeStack.back()->empty())
    PrintFatalError("prepReportPreprocessorStackError() called with "
                    "empty control stack");

  auto &PrepControl = PrepIncludeStack.back()->back();
  PrintError(CurBuf.end(), "Reached EOF without matching #endif");
  PrintError(PrepControl.SrcPos, "The latest preprocessor control is here");

  TokStart = CurPtr;
}
