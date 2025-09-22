//===- llvm/MC/MCAsmParser.h - Abstract Asm Parser Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCASMPARSER_H
#define LLVM_MC_MCPARSER_MCASMPARSER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmMacro.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <string>
#include <utility>

namespace llvm {

class MCAsmLexer;
class MCAsmInfo;
class MCAsmParserExtension;
class MCContext;
class MCExpr;
class MCInstPrinter;
class MCInstrInfo;
class MCStreamer;
class MCTargetAsmParser;
class SourceMgr;

struct InlineAsmIdentifierInfo {
  enum IdKind {
    IK_Invalid,  // Initial state. Unexpected after a successful parsing.
    IK_Label,    // Function/Label reference.
    IK_EnumVal,  // Value of enumeration type.
    IK_Var       // Variable.
  };
  // Represents an Enum value
  struct EnumIdentifier {
    int64_t EnumVal;
  };
  // Represents a label/function reference
  struct LabelIdentifier {
    void *Decl;
  };
  // Represents a variable
  struct VariableIdentifier {
    void *Decl;
    bool IsGlobalLV;
    unsigned Length;
    unsigned Size;
    unsigned Type;
  };
  // An InlineAsm identifier can only be one of those
  union {
    EnumIdentifier Enum;
    LabelIdentifier Label;
    VariableIdentifier Var;
  };
  bool isKind(IdKind kind) const { return Kind == kind; }
  // Initializers
  void setEnum(int64_t enumVal) {
    assert(isKind(IK_Invalid) && "should be initialized only once");
    Kind = IK_EnumVal;
    Enum.EnumVal = enumVal;
  }
  void setLabel(void *decl) {
    assert(isKind(IK_Invalid) && "should be initialized only once");
    Kind = IK_Label;
    Label.Decl = decl;
  }
  void setVar(void *decl, bool isGlobalLV, unsigned size, unsigned type) {
    assert(isKind(IK_Invalid) && "should be initialized only once");
    Kind = IK_Var;
    Var.Decl = decl;
    Var.IsGlobalLV = isGlobalLV;
    Var.Size = size;
    Var.Type = type;
    Var.Length = size / type;
  }
  InlineAsmIdentifierInfo() = default;

private:
  // Discriminate using the current kind.
  IdKind Kind = IK_Invalid;
};

// Generic type information for an assembly object.
// All sizes measured in bytes.
struct AsmTypeInfo {
  StringRef Name;
  unsigned Size = 0;
  unsigned ElementSize = 0;
  unsigned Length = 0;
};

struct AsmFieldInfo {
  AsmTypeInfo Type;
  unsigned Offset = 0;
};

/// Generic Sema callback for assembly parser.
class MCAsmParserSemaCallback {
public:
  virtual ~MCAsmParserSemaCallback();

  virtual void LookupInlineAsmIdentifier(StringRef &LineBuf,
                                         InlineAsmIdentifierInfo &Info,
                                         bool IsUnevaluatedContext) = 0;
  virtual StringRef LookupInlineAsmLabel(StringRef Identifier, SourceMgr &SM,
                                         SMLoc Location, bool Create) = 0;
  virtual bool LookupInlineAsmField(StringRef Base, StringRef Member,
                                    unsigned &Offset) = 0;
};

/// Generic assembler parser interface, for use by target specific
/// assembly parsers.
class MCAsmParser {
public:
  using DirectiveHandler = bool (*)(MCAsmParserExtension*, StringRef, SMLoc);
  using ExtensionDirectiveHandler =
      std::pair<MCAsmParserExtension*, DirectiveHandler>;

  struct MCPendingError {
    SMLoc Loc;
    SmallString<64> Msg;
    SMRange Range;
  };

private:
  MCTargetAsmParser *TargetParser = nullptr;

protected: // Can only create subclasses.
  MCAsmParser();

  SmallVector<MCPendingError, 0> PendingErrors;

  /// Flag tracking whether any errors have been encountered.
  bool HadError = false;

  bool ShowParsedOperands = false;

public:
  MCAsmParser(const MCAsmParser &) = delete;
  MCAsmParser &operator=(const MCAsmParser &) = delete;
  virtual ~MCAsmParser();

  virtual void addDirectiveHandler(StringRef Directive,
                                   ExtensionDirectiveHandler Handler) = 0;

  virtual void addAliasForDirective(StringRef Directive, StringRef Alias) = 0;

  virtual SourceMgr &getSourceManager() = 0;

  virtual MCAsmLexer &getLexer() = 0;
  const MCAsmLexer &getLexer() const {
    return const_cast<MCAsmParser*>(this)->getLexer();
  }

  virtual MCContext &getContext() = 0;

  /// Return the output streamer for the assembler.
  virtual MCStreamer &getStreamer() = 0;

  MCTargetAsmParser &getTargetParser() const { return *TargetParser; }
  void setTargetParser(MCTargetAsmParser &P);

  virtual unsigned getAssemblerDialect() { return 0;}
  virtual void setAssemblerDialect(unsigned i) { }

  bool getShowParsedOperands() const { return ShowParsedOperands; }
  void setShowParsedOperands(bool Value) { ShowParsedOperands = Value; }

  /// Run the parser on the input source buffer.
  virtual bool Run(bool NoInitialTextSection, bool NoFinalize = false) = 0;

  virtual void setParsingMSInlineAsm(bool V) = 0;
  virtual bool isParsingMSInlineAsm() = 0;

  virtual bool discardLTOSymbol(StringRef) const { return false; }

  virtual bool isParsingMasm() const { return false; }

  virtual bool defineMacro(StringRef Name, StringRef Value) { return true; }

  virtual bool lookUpField(StringRef Name, AsmFieldInfo &Info) const {
    return true;
  }
  virtual bool lookUpField(StringRef Base, StringRef Member,
                           AsmFieldInfo &Info) const {
    return true;
  }

  virtual bool lookUpType(StringRef Name, AsmTypeInfo &Info) const {
    return true;
  }

  /// Parse MS-style inline assembly.
  virtual bool parseMSInlineAsm(
      std::string &AsmString, unsigned &NumOutputs, unsigned &NumInputs,
      SmallVectorImpl<std::pair<void *, bool>> &OpDecls,
      SmallVectorImpl<std::string> &Constraints,
      SmallVectorImpl<std::string> &Clobbers, const MCInstrInfo *MII,
      const MCInstPrinter *IP, MCAsmParserSemaCallback &SI) = 0;

  /// Emit a note at the location \p L, with the message \p Msg.
  virtual void Note(SMLoc L, const Twine &Msg,
                    SMRange Range = std::nullopt) = 0;

  /// Emit a warning at the location \p L, with the message \p Msg.
  ///
  /// \return The return value is true, if warnings are fatal.
  virtual bool Warning(SMLoc L, const Twine &Msg,
                       SMRange Range = std::nullopt) = 0;

  /// Return an error at the location \p L, with the message \p Msg. This
  /// may be modified before being emitted.
  ///
  /// \return The return value is always true, as an idiomatic convenience to
  /// clients.
  bool Error(SMLoc L, const Twine &Msg, SMRange Range = std::nullopt);

  /// Emit an error at the location \p L, with the message \p Msg.
  ///
  /// \return The return value is always true, as an idiomatic convenience to
  /// clients.
  virtual bool printError(SMLoc L, const Twine &Msg,
                          SMRange Range = std::nullopt) = 0;

  bool hasPendingError() { return !PendingErrors.empty(); }

  bool printPendingErrors() {
    bool rv = !PendingErrors.empty();
    for (auto &Err : PendingErrors) {
      printError(Err.Loc, Twine(Err.Msg), Err.Range);
    }
    PendingErrors.clear();
    return rv;
  }

  void clearPendingErrors() { PendingErrors.clear(); }

  bool addErrorSuffix(const Twine &Suffix);

  /// Get the next AsmToken in the stream, possibly handling file
  /// inclusion first.
  virtual const AsmToken &Lex() = 0;

  /// Get the current AsmToken from the stream.
  const AsmToken &getTok() const;

  /// Report an error at the current lexer location.
  bool TokError(const Twine &Msg, SMRange Range = std::nullopt);

  bool parseTokenLoc(SMLoc &Loc);
  bool parseToken(AsmToken::TokenKind T, const Twine &Msg = "unexpected token");
  /// Attempt to parse and consume token, returning true on
  /// success.
  bool parseOptionalToken(AsmToken::TokenKind T);

  bool parseComma() { return parseToken(AsmToken::Comma, "expected comma"); }
  bool parseRParen() { return parseToken(AsmToken::RParen, "expected ')'"); }
  bool parseEOL();
  bool parseEOL(const Twine &ErrMsg);

  bool parseMany(function_ref<bool()> parseOne, bool hasComma = true);

  bool parseIntToken(int64_t &V, const Twine &ErrMsg);

  bool check(bool P, const Twine &Msg);
  bool check(bool P, SMLoc Loc, const Twine &Msg);

  /// Parse an identifier or string (as a quoted identifier) and set \p
  /// Res to the identifier contents.
  virtual bool parseIdentifier(StringRef &Res) = 0;

  /// Parse up to the end of statement and return the contents from the
  /// current token until the end of the statement; the current token on exit
  /// will be either the EndOfStatement or EOF.
  virtual StringRef parseStringToEndOfStatement() = 0;

  /// Parse the current token as a string which may include escaped
  /// characters and return the string contents.
  virtual bool parseEscapedString(std::string &Data) = 0;

  /// Parse an angle-bracket delimited string at the current position if one is
  /// present, returning the string contents.
  virtual bool parseAngleBracketString(std::string &Data) = 0;

  /// Skip to the end of the current statement, for error recovery.
  virtual void eatToEndOfStatement() = 0;

  /// Parse an arbitrary expression.
  ///
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \return - False on success.
  virtual bool parseExpression(const MCExpr *&Res, SMLoc &EndLoc) = 0;
  bool parseExpression(const MCExpr *&Res);

  /// Parse a primary expression.
  ///
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \return - False on success.
  virtual bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc,
                                AsmTypeInfo *TypeInfo) = 0;

  /// Parse an arbitrary expression, assuming that an initial '(' has
  /// already been consumed.
  ///
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \return - False on success.
  virtual bool parseParenExpression(const MCExpr *&Res, SMLoc &EndLoc) = 0;

  /// Parse an expression which must evaluate to an absolute value.
  ///
  /// \param Res - The value of the absolute expression. The result is undefined
  /// on error.
  /// \return - False on success.
  virtual bool parseAbsoluteExpression(int64_t &Res) = 0;

  /// Ensure that we have a valid section set in the streamer. Otherwise,
  /// report an error and switch to .text.
  /// \return - False on success.
  virtual bool checkForValidSection() = 0;

  /// Parse an arbitrary expression of a specified parenthesis depth,
  /// assuming that the initial '(' characters have already been consumed.
  ///
  /// \param ParenDepth - Specifies how many trailing expressions outside the
  /// current parentheses we have to parse.
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \return - False on success.
  virtual bool parseParenExprOfDepth(unsigned ParenDepth, const MCExpr *&Res,
                                     SMLoc &EndLoc) = 0;

  /// Parse a .gnu_attribute.
  bool parseGNUAttribute(SMLoc L, int64_t &Tag, int64_t &IntegerValue);
};

/// Create an MCAsmParser instance for parsing assembly similar to gas syntax
MCAsmParser *createMCAsmParser(SourceMgr &, MCContext &, MCStreamer &,
                               const MCAsmInfo &, unsigned CB = 0);

/// Create an MCAsmParser instance for parsing Microsoft MASM-style assembly
MCAsmParser *createMCMasmParser(SourceMgr &, MCContext &, MCStreamer &,
                                const MCAsmInfo &, struct tm, unsigned CB = 0);

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCASMPARSER_H
