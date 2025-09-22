//===-- ResourceScriptParser.cpp --------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This implements the parser defined in ResourceScriptParser.h.
//
//===---------------------------------------------------------------------===//

#include "ResourceScriptParser.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"

// Take an expression returning llvm::Error and forward the error if it exists.
#define RETURN_IF_ERROR(Expr)                                                  \
  if (auto Err = (Expr))                                                       \
    return std::move(Err);

// Take an expression returning llvm::Expected<T> and assign it to Var or
// forward the error out of the function.
#define ASSIGN_OR_RETURN(Var, Expr)                                            \
  auto Var = (Expr);                                                           \
  if (!Var)                                                                    \
    return Var.takeError();

namespace llvm {
namespace rc {

RCParser::ParserError::ParserError(const Twine &Expected, const LocIter CurLoc,
                                   const LocIter End)
    : ErrorLoc(CurLoc), FileEnd(End) {
  CurMessage = "Error parsing file: expected " + Expected.str() + ", got " +
               (CurLoc == End ? "<EOF>" : CurLoc->value()).str();
}

char RCParser::ParserError::ID = 0;

RCParser::RCParser(std::vector<RCToken> TokenList)
    : Tokens(std::move(TokenList)), CurLoc(Tokens.begin()), End(Tokens.end()) {}

bool RCParser::isEof() const { return CurLoc == End; }

RCParser::ParseType RCParser::parseSingleResource() {
  // The first thing we read is usually a resource's name. However, in some
  // cases (LANGUAGE and STRINGTABLE) the resources don't have their names
  // and the first token to be read is the type.
  ASSIGN_OR_RETURN(NameToken, readTypeOrName());

  if (NameToken->equalsLower("LANGUAGE"))
    return parseLanguageResource();
  else if (NameToken->equalsLower("STRINGTABLE"))
    return parseStringTableResource();

  // If it's not an unnamed resource, what we've just read is a name. Now,
  // read resource type;
  ASSIGN_OR_RETURN(TypeToken, readTypeOrName());

  ParseType Result = std::unique_ptr<RCResource>();
  (void)!Result;

  if (TypeToken->equalsLower("ACCELERATORS"))
    Result = parseAcceleratorsResource();
  else if (TypeToken->equalsLower("BITMAP"))
    Result = parseBitmapResource();
  else if (TypeToken->equalsLower("CURSOR"))
    Result = parseCursorResource();
  else if (TypeToken->equalsLower("DIALOG"))
    Result = parseDialogResource(false);
  else if (TypeToken->equalsLower("DIALOGEX"))
    Result = parseDialogResource(true);
  else if (TypeToken->equalsLower("HTML"))
    Result = parseHTMLResource();
  else if (TypeToken->equalsLower("ICON"))
    Result = parseIconResource();
  else if (TypeToken->equalsLower("MENU"))
    Result = parseMenuResource();
  else if (TypeToken->equalsLower("MENUEX"))
    Result = parseMenuExResource();
  else if (TypeToken->equalsLower("RCDATA"))
    Result = parseUserDefinedResource(RkRcData);
  else if (TypeToken->equalsLower("VERSIONINFO"))
    Result = parseVersionInfoResource();
  else
    Result = parseUserDefinedResource(*TypeToken);

  if (Result)
    (*Result)->setName(*NameToken);

  return Result;
}

bool RCParser::isNextTokenKind(Kind TokenKind) const {
  return !isEof() && look().kind() == TokenKind;
}

const RCToken &RCParser::look() const {
  assert(!isEof());
  return *CurLoc;
}

const RCToken &RCParser::read() {
  assert(!isEof());
  return *CurLoc++;
}

void RCParser::consume() {
  assert(!isEof());
  CurLoc++;
}

// An integer description might consist of a single integer or
// an arithmetic expression evaluating to the integer. The expressions
// can contain the following tokens: <int> ( ) + - | & ~ not. Their meaning
// is the same as in C++ except for 'not' expression.
// The operators in the original RC implementation have the following
// precedence:
//   1) Unary operators (- ~ not),
//   2) Binary operators (+ - & |), with no precedence.
//
// 'not' expression is mostly useful for style values. It evaluates to 0,
// but value given to the operator is stored separately from integer value.
// It's mostly useful for control style expressions and causes bits from
// default control style to be excluded from generated style. For binary
// operators the mask from the right operand is applied to the left operand
// and masks from both operands are combined in operator result.
//
// The following grammar is used to parse the expressions Exp1:
//   Exp1 ::= Exp2 || Exp1 + Exp2 || Exp1 - Exp2 || Exp1 | Exp2 || Exp1 & Exp2
//   Exp2 ::= -Exp2 || ~Exp2 || not Expr2 || Int || (Exp1).
// (More conveniently, Exp1 is a non-empty sequence of Exp2 expressions,
// separated by binary operators.)
//
// Expressions of type Exp1 are read by parseIntExpr1(Inner) method, while Exp2
// is read by parseIntExpr2().
//
// The original Microsoft tool handles multiple unary operators incorrectly.
// For example, in 16-bit little-endian integers:
//    1 => 01 00, -1 => ff ff, --1 => ff ff, ---1 => 01 00;
//    1 => 01 00, ~1 => fe ff, ~~1 => fd ff, ~~~1 => fc ff.
// Our implementation differs from the original one and handles these
// operators correctly:
//    1 => 01 00, -1 => ff ff, --1 => 01 00, ---1 => ff ff;
//    1 => 01 00, ~1 => fe ff, ~~1 => 01 00, ~~~1 => fe ff.

Expected<RCInt> RCParser::readInt() {
  ASSIGN_OR_RETURN(Value, parseIntExpr1());
  return (*Value).getValue();
}

Expected<IntWithNotMask> RCParser::parseIntExpr1() {
  // Exp1 ::= Exp2 || Exp1 + Exp2 || Exp1 - Exp2 || Exp1 | Exp2 || Exp1 & Exp2.
  ASSIGN_OR_RETURN(FirstResult, parseIntExpr2());
  IntWithNotMask Result = *FirstResult;

  while (!isEof() && look().isBinaryOp()) {
    auto OpToken = read();
    ASSIGN_OR_RETURN(NextResult, parseIntExpr2());

    switch (OpToken.kind()) {
    case Kind::Plus:
      Result += *NextResult;
      break;

    case Kind::Minus:
      Result -= *NextResult;
      break;

    case Kind::Pipe:
      Result |= *NextResult;
      break;

    case Kind::Amp:
      Result &= *NextResult;
      break;

    default:
      llvm_unreachable("Already processed all binary ops.");
    }
  }

  return Result;
}

Expected<IntWithNotMask> RCParser::parseIntExpr2() {
  // Exp2 ::= -Exp2 || ~Exp2 || not Expr2 || Int || (Exp1).
  static const char ErrorMsg[] = "'-', '~', integer or '('";

  if (isEof())
    return getExpectedError(ErrorMsg);

  switch (look().kind()) {
  case Kind::Minus: {
    consume();
    ASSIGN_OR_RETURN(Result, parseIntExpr2());
    return -(*Result);
  }

  case Kind::Tilde: {
    consume();
    ASSIGN_OR_RETURN(Result, parseIntExpr2());
    return ~(*Result);
  }

  case Kind::Int:
    return RCInt(read());

  case Kind::LeftParen: {
    consume();
    ASSIGN_OR_RETURN(Result, parseIntExpr1());
    RETURN_IF_ERROR(consumeType(Kind::RightParen));
    return *Result;
  }

  case Kind::Identifier: {
    if (!read().value().equals_insensitive("not"))
      return getExpectedError(ErrorMsg, true);
    ASSIGN_OR_RETURN(Result, parseIntExpr2());
    return IntWithNotMask(0, (*Result).getValue());
  }

  default:
    return getExpectedError(ErrorMsg);
  }
}

Expected<StringRef> RCParser::readString() {
  if (!isNextTokenKind(Kind::String))
    return getExpectedError("string");
  return read().value();
}

Expected<StringRef> RCParser::readFilename() {
  if (!isNextTokenKind(Kind::String) && !isNextTokenKind(Kind::Identifier))
    return getExpectedError("string");
  const RCToken &Token = read();
  StringRef Str = Token.value();
  if (Token.kind() != Kind::String)
    return Str;
  while (isNextTokenKind(Kind::String)) {
    const RCToken &NextToken = read();
    StringRef Next = NextToken.value();
    bool IsWide = Str.consume_front_insensitive("L");
    Next.consume_front_insensitive("L");
    bool StrUnquoted = Str.consume_front("\"") && Str.consume_back("\"");
    bool NextUnquoted = Next.consume_front("\"") && Next.consume_back("\"");
    assert(StrUnquoted && NextUnquoted);
    (void)StrUnquoted;
    (void)NextUnquoted;

    Str = Saver.save(Twine(IsWide ? "L" : "") + "\"" + Str + Next + "\"");
  }
  return Str;
}

Expected<StringRef> RCParser::readIdentifier() {
  if (!isNextTokenKind(Kind::Identifier))
    return getExpectedError("identifier");
  return read().value();
}

Expected<IntOrString> RCParser::readIntOrString() {
  if (!isNextTokenKind(Kind::Int) && !isNextTokenKind(Kind::String))
    return getExpectedError("int or string");
  return IntOrString(read());
}

Expected<IntOrString> RCParser::readTypeOrName() {
  // We suggest that the correct resource name or type should be either an
  // identifier or an integer. The original RC tool is much more liberal.
  if (!isNextTokenKind(Kind::Identifier) && !isNextTokenKind(Kind::Int))
    return getExpectedError("int or identifier");
  return IntOrString(read());
}

Error RCParser::consumeType(Kind TokenKind) {
  if (isNextTokenKind(TokenKind)) {
    consume();
    return Error::success();
  }

  switch (TokenKind) {
#define TOKEN(TokenName)                                                       \
  case Kind::TokenName:                                                        \
    return getExpectedError(#TokenName);
#define SHORT_TOKEN(TokenName, TokenCh)                                        \
  case Kind::TokenName:                                                        \
    return getExpectedError(#TokenCh);
#include "ResourceScriptTokenList.def"
  }

  llvm_unreachable("All case options exhausted.");
}

bool RCParser::consumeOptionalType(Kind TokenKind) {
  if (isNextTokenKind(TokenKind)) {
    consume();
    return true;
  }

  return false;
}

Expected<SmallVector<RCInt, 8>> RCParser::readIntsWithCommas(size_t MinCount,
                                                             size_t MaxCount) {
  assert(MinCount <= MaxCount);

  SmallVector<RCInt, 8> Result;

  auto FailureHandler =
      [&](llvm::Error Err) -> Expected<SmallVector<RCInt, 8>> {
    if (Result.size() < MinCount)
      return std::move(Err);
    consumeError(std::move(Err));
    return Result;
  };

  for (size_t i = 0; i < MaxCount; ++i) {
    // Try to read a comma unless we read the first token.
    // Sometimes RC tool requires them and sometimes not. We decide to
    // always require them.
    if (i >= 1) {
      if (auto CommaError = consumeType(Kind::Comma))
        return FailureHandler(std::move(CommaError));
    }

    if (auto IntResult = readInt())
      Result.push_back(*IntResult);
    else
      return FailureHandler(IntResult.takeError());
  }

  return std::move(Result);
}

Expected<uint32_t> RCParser::parseFlags(ArrayRef<StringRef> FlagDesc,
                                        ArrayRef<uint32_t> FlagValues) {
  assert(!FlagDesc.empty());
  assert(FlagDesc.size() == FlagValues.size());

  uint32_t Result = 0;
  while (isNextTokenKind(Kind::Comma)) {
    consume();
    ASSIGN_OR_RETURN(FlagResult, readIdentifier());
    bool FoundFlag = false;

    for (size_t FlagId = 0; FlagId < FlagDesc.size(); ++FlagId) {
      if (!FlagResult->equals_insensitive(FlagDesc[FlagId]))
        continue;

      Result |= FlagValues[FlagId];
      FoundFlag = true;
      break;
    }

    if (!FoundFlag)
      return getExpectedError(join(FlagDesc, "/"), true);
  }

  return Result;
}

uint16_t RCParser::parseMemoryFlags(uint16_t Flags) {
  while (!isEof()) {
    const RCToken &Token = look();
    if (Token.kind() != Kind::Identifier)
      return Flags;
    const StringRef Ident = Token.value();
    if (Ident.equals_insensitive("PRELOAD"))
      Flags |= MfPreload;
    else if (Ident.equals_insensitive("LOADONCALL"))
      Flags &= ~MfPreload;
    else if (Ident.equals_insensitive("FIXED"))
      Flags &= ~(MfMoveable | MfDiscardable);
    else if (Ident.equals_insensitive("MOVEABLE"))
      Flags |= MfMoveable;
    else if (Ident.equals_insensitive("DISCARDABLE"))
      Flags |= MfDiscardable | MfMoveable | MfPure;
    else if (Ident.equals_insensitive("PURE"))
      Flags |= MfPure;
    else if (Ident.equals_insensitive("IMPURE"))
      Flags &= ~(MfPure | MfDiscardable);
    else if (Ident.equals_insensitive("SHARED"))
      Flags |= MfPure;
    else if (Ident.equals_insensitive("NONSHARED"))
      Flags &= ~(MfPure | MfDiscardable);
    else
      return Flags;
    consume();
  }
  return Flags;
}

Expected<OptionalStmtList>
RCParser::parseOptionalStatements(OptStmtType StmtsType) {
  OptionalStmtList Result;

  // The last statement is always followed by the start of the block.
  while (!isNextTokenKind(Kind::BlockBegin)) {
    ASSIGN_OR_RETURN(SingleParse, parseSingleOptionalStatement(StmtsType));
    Result.addStmt(std::move(*SingleParse));
  }

  return std::move(Result);
}

Expected<std::unique_ptr<OptionalStmt>>
RCParser::parseSingleOptionalStatement(OptStmtType StmtsType) {
  ASSIGN_OR_RETURN(TypeToken, readIdentifier());
  if (TypeToken->equals_insensitive("CHARACTERISTICS"))
    return parseCharacteristicsStmt();
  if (TypeToken->equals_insensitive("LANGUAGE"))
    return parseLanguageStmt();
  if (TypeToken->equals_insensitive("VERSION"))
    return parseVersionStmt();

  if (StmtsType != OptStmtType::BasicStmt) {
    if (TypeToken->equals_insensitive("CAPTION"))
      return parseCaptionStmt();
    if (TypeToken->equals_insensitive("CLASS"))
      return parseClassStmt();
    if (TypeToken->equals_insensitive("EXSTYLE"))
      return parseExStyleStmt();
    if (TypeToken->equals_insensitive("FONT"))
      return parseFontStmt(StmtsType);
    if (TypeToken->equals_insensitive("STYLE"))
      return parseStyleStmt();
    if (TypeToken->equals_insensitive("MENU"))
      return parseMenuStmt();
  }

  return getExpectedError("optional statement type, BEGIN or '{'",
                          /* IsAlreadyRead = */ true);
}

RCParser::ParseType RCParser::parseLanguageResource() {
  // Read LANGUAGE as an optional statement. If it's read correctly, we can
  // upcast it to RCResource.
  return parseLanguageStmt();
}

RCParser::ParseType RCParser::parseAcceleratorsResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(AcceleratorsResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(OptStatements, parseOptionalStatements());
  RETURN_IF_ERROR(consumeType(Kind::BlockBegin));

  auto Accels = std::make_unique<AcceleratorsResource>(
      std::move(*OptStatements), MemoryFlags);

  while (!consumeOptionalType(Kind::BlockEnd)) {
    ASSIGN_OR_RETURN(EventResult, readIntOrString());
    RETURN_IF_ERROR(consumeType(Kind::Comma));
    ASSIGN_OR_RETURN(IDResult, readInt());
    ASSIGN_OR_RETURN(
        FlagsResult,
        parseFlags(AcceleratorsResource::Accelerator::OptionsStr,
                   AcceleratorsResource::Accelerator::OptionsFlags));
    Accels->addAccelerator(*EventResult, *IDResult, *FlagsResult);
  }

  return std::move(Accels);
}

RCParser::ParseType RCParser::parseCursorResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(CursorResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(Arg, readFilename());
  return std::make_unique<CursorResource>(*Arg, MemoryFlags);
}

RCParser::ParseType RCParser::parseDialogResource(bool IsExtended) {
  uint16_t MemoryFlags =
      parseMemoryFlags(DialogResource::getDefaultMemoryFlags());
  // Dialog resources have the following format of the arguments:
  //  DIALOG:   x, y, width, height [opt stmts...] {controls...}
  //  DIALOGEX: x, y, width, height [, helpID] [opt stmts...] {controls...}
  // These are very similar, so we parse them together.
  ASSIGN_OR_RETURN(LocResult, readIntsWithCommas(4, 4));

  uint32_t HelpID = 0; // When HelpID is unset, it's assumed to be 0.
  if (IsExtended && consumeOptionalType(Kind::Comma)) {
    ASSIGN_OR_RETURN(HelpIDResult, readInt());
    HelpID = *HelpIDResult;
  }

  ASSIGN_OR_RETURN(OptStatements, parseOptionalStatements(
                                      IsExtended ? OptStmtType::DialogExStmt
                                                 : OptStmtType::DialogStmt));

  assert(isNextTokenKind(Kind::BlockBegin) &&
         "parseOptionalStatements, when successful, halts on BlockBegin.");
  consume();

  auto Dialog = std::make_unique<DialogResource>(
      (*LocResult)[0], (*LocResult)[1], (*LocResult)[2], (*LocResult)[3],
      HelpID, std::move(*OptStatements), IsExtended, MemoryFlags);

  while (!consumeOptionalType(Kind::BlockEnd)) {
    ASSIGN_OR_RETURN(ControlDefResult, parseControl());
    Dialog->addControl(std::move(*ControlDefResult));
  }

  return std::move(Dialog);
}

RCParser::ParseType RCParser::parseUserDefinedResource(IntOrString Type) {
  uint16_t MemoryFlags =
      parseMemoryFlags(UserDefinedResource::getDefaultMemoryFlags());
  if (isEof())
    return getExpectedError("filename, '{' or BEGIN");

  // Check if this is a file resource.
  switch (look().kind()) {
  case Kind::String:
  case Kind::Identifier: {
    ASSIGN_OR_RETURN(Filename, readFilename());
    return std::make_unique<UserDefinedResource>(Type, *Filename, MemoryFlags);
  }
  default:
    break;
  }

  RETURN_IF_ERROR(consumeType(Kind::BlockBegin));
  std::vector<IntOrString> Data;

  while (!consumeOptionalType(Kind::BlockEnd)) {
    ASSIGN_OR_RETURN(Item, readIntOrString());
    Data.push_back(*Item);

    // There can be zero or more commas after each token (but not before
    // the first one).
    while (consumeOptionalType(Kind::Comma)) {
    }
  }

  return std::make_unique<UserDefinedResource>(Type, std::move(Data),
                                               MemoryFlags);
}

RCParser::ParseType RCParser::parseVersionInfoResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(VersionInfoResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(FixedResult, parseVersionInfoFixed());
  ASSIGN_OR_RETURN(BlockResult, parseVersionInfoBlockContents(StringRef()));
  return std::make_unique<VersionInfoResource>(
      std::move(**BlockResult), std::move(*FixedResult), MemoryFlags);
}

Expected<Control> RCParser::parseControl() {
  // Each control definition (except CONTROL) follows one of the schemes below
  // depending on the control class:
  //  [class] text, id, x, y, width, height [, style] [, exstyle] [, helpID]
  //  [class]       id, x, y, width, height [, style] [, exstyle] [, helpID]
  // Note that control ids must be integers.
  // Text might be either a string or an integer pointing to resource ID.
  ASSIGN_OR_RETURN(ClassResult, readIdentifier());
  std::string ClassUpper = ClassResult->upper();
  auto CtlInfo = Control::SupportedCtls.find(ClassUpper);
  if (CtlInfo == Control::SupportedCtls.end())
    return getExpectedError("control type, END or '}'", true);

  // Read caption if necessary.
  IntOrString Caption{StringRef()};
  if (CtlInfo->getValue().HasTitle) {
    ASSIGN_OR_RETURN(CaptionResult, readIntOrString());
    RETURN_IF_ERROR(consumeType(Kind::Comma));
    Caption = *CaptionResult;
  }

  ASSIGN_OR_RETURN(ID, readInt());
  RETURN_IF_ERROR(consumeType(Kind::Comma));

  IntOrString Class;
  std::optional<IntWithNotMask> Style;
  if (ClassUpper == "CONTROL") {
    // CONTROL text, id, class, style, x, y, width, height [, exstyle] [,
    // helpID]
    ASSIGN_OR_RETURN(ClassStr, readString());
    RETURN_IF_ERROR(consumeType(Kind::Comma));
    Class = *ClassStr;
    ASSIGN_OR_RETURN(StyleVal, parseIntExpr1());
    RETURN_IF_ERROR(consumeType(Kind::Comma));
    Style = *StyleVal;
  } else {
    Class = CtlInfo->getValue().CtlClass;
  }

  // x, y, width, height
  ASSIGN_OR_RETURN(Args, readIntsWithCommas(4, 4));

  if (ClassUpper != "CONTROL") {
    if (consumeOptionalType(Kind::Comma)) {
      ASSIGN_OR_RETURN(Val, parseIntExpr1());
      Style = *Val;
    }
  }

  std::optional<uint32_t> ExStyle;
  if (consumeOptionalType(Kind::Comma)) {
    ASSIGN_OR_RETURN(Val, readInt());
    ExStyle = *Val;
  }
  std::optional<uint32_t> HelpID;
  if (consumeOptionalType(Kind::Comma)) {
    ASSIGN_OR_RETURN(Val, readInt());
    HelpID = *Val;
  }

  return Control(*ClassResult, Caption, *ID, (*Args)[0], (*Args)[1], (*Args)[2],
                 (*Args)[3], Style, ExStyle, HelpID, Class);
}

RCParser::ParseType RCParser::parseBitmapResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(BitmapResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(Arg, readFilename());
  return std::make_unique<BitmapResource>(*Arg, MemoryFlags);
}

RCParser::ParseType RCParser::parseIconResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(IconResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(Arg, readFilename());
  return std::make_unique<IconResource>(*Arg, MemoryFlags);
}

RCParser::ParseType RCParser::parseHTMLResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(HTMLResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(Arg, readFilename());
  return std::make_unique<HTMLResource>(*Arg, MemoryFlags);
}

RCParser::ParseType RCParser::parseMenuResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(MenuResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(OptStatements, parseOptionalStatements());
  ASSIGN_OR_RETURN(Items, parseMenuItemsList());
  return std::make_unique<MenuResource>(std::move(*OptStatements),
                                        std::move(*Items), MemoryFlags);
}

RCParser::ParseType RCParser::parseMenuExResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(MenuExResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(Items, parseMenuExItemsList());
  return std::make_unique<MenuExResource>(std::move(*Items), MemoryFlags);
}

Expected<MenuDefinitionList> RCParser::parseMenuItemsList() {
  RETURN_IF_ERROR(consumeType(Kind::BlockBegin));

  MenuDefinitionList List;

  // Read a set of items. Each item is of one of three kinds:
  //   MENUITEM SEPARATOR
  //   MENUITEM caption:String, result:Int [, menu flags]...
  //   POPUP caption:String [, menu flags]... { items... }
  while (!consumeOptionalType(Kind::BlockEnd)) {
    ASSIGN_OR_RETURN(ItemTypeResult, readIdentifier());

    bool IsMenuItem = ItemTypeResult->equals_insensitive("MENUITEM");
    bool IsPopup = ItemTypeResult->equals_insensitive("POPUP");
    if (!IsMenuItem && !IsPopup)
      return getExpectedError("MENUITEM, POPUP, END or '}'", true);

    if (IsMenuItem && isNextTokenKind(Kind::Identifier)) {
      // Now, expecting SEPARATOR.
      ASSIGN_OR_RETURN(SeparatorResult, readIdentifier());
      if (SeparatorResult->equals_insensitive("SEPARATOR")) {
        List.addDefinition(std::make_unique<MenuSeparator>());
        continue;
      }

      return getExpectedError("SEPARATOR or string", true);
    }

    // Not a separator. Read the caption.
    ASSIGN_OR_RETURN(CaptionResult, readString());

    // If MENUITEM, expect also a comma and an integer.
    uint32_t MenuResult = -1;

    if (IsMenuItem) {
      RETURN_IF_ERROR(consumeType(Kind::Comma));
      ASSIGN_OR_RETURN(IntResult, readInt());
      MenuResult = *IntResult;
    }

    ASSIGN_OR_RETURN(FlagsResult, parseFlags(MenuDefinition::OptionsStr,
                                             MenuDefinition::OptionsFlags));

    if (IsPopup) {
      // If POPUP, read submenu items recursively.
      ASSIGN_OR_RETURN(SubMenuResult, parseMenuItemsList());
      List.addDefinition(std::make_unique<PopupItem>(
          *CaptionResult, *FlagsResult, std::move(*SubMenuResult)));
      continue;
    }

    assert(IsMenuItem);
    List.addDefinition(
        std::make_unique<MenuItem>(*CaptionResult, MenuResult, *FlagsResult));
  }

  return std::move(List);
}

Expected<MenuDefinitionList> RCParser::parseMenuExItemsList() {
  RETURN_IF_ERROR(consumeType(Kind::BlockBegin));

  MenuDefinitionList List;

  // Read a set of items. Each item is of one of two kinds:
  //   MENUITEM caption:String [,[id][, [type][, state]]]]
  //   POPUP caption:String [,[id][, [type][, [state][, helpID]]]] { popupBody }
  while (!consumeOptionalType(Kind::BlockEnd)) {
    ASSIGN_OR_RETURN(ItemTypeResult, readIdentifier());

    bool IsMenuItem = ItemTypeResult->equals_insensitive("MENUITEM");
    bool IsPopup = ItemTypeResult->equals_insensitive("POPUP");
    if (!IsMenuItem && !IsPopup)
      return getExpectedError("MENUITEM, POPUP, END or '}'", true);

    // Not a separator. Read the caption.
    ASSIGN_OR_RETURN(CaptionResult, readString());

    // If MENUITEM, expect [,[id][, [type][, state]]]]
    if (IsMenuItem) {
      uint32_t MenuId = 0;
      uint32_t MenuType = 0;
      uint32_t MenuState = 0;

      if (consumeOptionalType(Kind::Comma)) {
        auto IntId = readInt();
        if (IntId) {
          MenuId = *IntId;
        }
        if (consumeOptionalType(Kind::Comma)) {
          auto IntType = readInt();
          if (IntType) {
            MenuType = *IntType;
          }
          if (consumeOptionalType(Kind::Comma)) {
            auto IntState = readInt();
            if (IntState) {
              MenuState = *IntState;
            }
          }
        }
      }
      List.addDefinition(std::make_unique<MenuExItem>(*CaptionResult, MenuId,
                                                      MenuType, MenuState));
      continue;
    }

    assert(IsPopup);

    uint32_t PopupId = 0;
    uint32_t PopupType = 0;
    uint32_t PopupState = 0;
    uint32_t PopupHelpID = 0;

    if (consumeOptionalType(Kind::Comma)) {
      auto IntId = readInt();
      if (IntId) {
        PopupId = *IntId;
      }
      if (consumeOptionalType(Kind::Comma)) {
        auto IntType = readInt();
        if (IntType) {
          PopupType = *IntType;
        }
        if (consumeOptionalType(Kind::Comma)) {
          auto IntState = readInt();
          if (IntState) {
            PopupState = *IntState;
          }
          if (consumeOptionalType(Kind::Comma)) {
            auto IntHelpID = readInt();
            if (IntHelpID) {
              PopupHelpID = *IntHelpID;
            }
          }
        }
      }
    }
    // If POPUP, read submenu items recursively.
    ASSIGN_OR_RETURN(SubMenuResult, parseMenuExItemsList());
    List.addDefinition(std::make_unique<PopupExItem>(
        *CaptionResult, PopupId, PopupType, PopupState, PopupHelpID,
        std::move(*SubMenuResult)));
  }

  return std::move(List);
}

RCParser::ParseType RCParser::parseStringTableResource() {
  uint16_t MemoryFlags =
      parseMemoryFlags(StringTableResource::getDefaultMemoryFlags());
  ASSIGN_OR_RETURN(OptStatements, parseOptionalStatements());
  RETURN_IF_ERROR(consumeType(Kind::BlockBegin));

  auto Table = std::make_unique<StringTableResource>(std::move(*OptStatements),
                                                     MemoryFlags);

  // Read strings until we reach the end of the block.
  while (!consumeOptionalType(Kind::BlockEnd)) {
    // Each definition consists of string's ID (an integer) and a string.
    // Some examples in documentation suggest that there might be a comma in
    // between, however we strictly adhere to the single statement definition.
    ASSIGN_OR_RETURN(IDResult, readInt());
    consumeOptionalType(Kind::Comma);

    std::vector<StringRef> Strings;
    ASSIGN_OR_RETURN(StrResult, readString());
    Strings.push_back(*StrResult);
    while (isNextTokenKind(Kind::String))
      Strings.push_back(read().value());

    Table->addStrings(*IDResult, std::move(Strings));
  }

  return std::move(Table);
}

Expected<std::unique_ptr<VersionInfoBlock>>
RCParser::parseVersionInfoBlockContents(StringRef BlockName) {
  RETURN_IF_ERROR(consumeType(Kind::BlockBegin));

  auto Contents = std::make_unique<VersionInfoBlock>(BlockName);

  while (!isNextTokenKind(Kind::BlockEnd)) {
    ASSIGN_OR_RETURN(Stmt, parseVersionInfoStmt());
    Contents->addStmt(std::move(*Stmt));
  }

  consume(); // Consume BlockEnd.

  return std::move(Contents);
}

Expected<std::unique_ptr<VersionInfoStmt>> RCParser::parseVersionInfoStmt() {
  // Expect either BLOCK or VALUE, then a name or a key (a string).
  ASSIGN_OR_RETURN(TypeResult, readIdentifier());

  if (TypeResult->equals_insensitive("BLOCK")) {
    ASSIGN_OR_RETURN(NameResult, readString());
    return parseVersionInfoBlockContents(*NameResult);
  }

  if (TypeResult->equals_insensitive("VALUE")) {
    ASSIGN_OR_RETURN(KeyResult, readString());
    // Read a non-empty list of strings and/or ints, each
    // possibly preceded by a comma. Unfortunately, the tool behavior depends
    // on them existing or not, so we need to memorize where we found them.
    std::vector<IntOrString> Values;
    BitVector PrecedingCommas;
    RETURN_IF_ERROR(consumeType(Kind::Comma));
    while (!isNextTokenKind(Kind::Identifier) &&
           !isNextTokenKind(Kind::BlockEnd)) {
      // Try to eat a comma if it's not the first statement.
      bool HadComma = Values.size() > 0 && consumeOptionalType(Kind::Comma);
      ASSIGN_OR_RETURN(ValueResult, readIntOrString());
      Values.push_back(*ValueResult);
      PrecedingCommas.push_back(HadComma);
    }
    return std::make_unique<VersionInfoValue>(*KeyResult, std::move(Values),
                                              std::move(PrecedingCommas));
  }

  return getExpectedError("BLOCK or VALUE", true);
}

Expected<VersionInfoResource::VersionInfoFixed>
RCParser::parseVersionInfoFixed() {
  using RetType = VersionInfoResource::VersionInfoFixed;
  RetType Result;

  // Read until the beginning of the block.
  while (!isNextTokenKind(Kind::BlockBegin)) {
    ASSIGN_OR_RETURN(TypeResult, readIdentifier());
    auto FixedType = RetType::getFixedType(*TypeResult);

    if (!RetType::isTypeSupported(FixedType))
      return getExpectedError("fixed VERSIONINFO statement type", true);
    if (Result.IsTypePresent[FixedType])
      return getExpectedError("yet unread fixed VERSIONINFO statement type",
                              true);

    // VERSION variations take multiple integers.
    size_t NumInts = RetType::isVersionType(FixedType) ? 4 : 1;
    ASSIGN_OR_RETURN(ArgsResult, readIntsWithCommas(1, NumInts));
    SmallVector<uint32_t, 4> ArgInts(ArgsResult->begin(), ArgsResult->end());
    while (ArgInts.size() < NumInts)
      ArgInts.push_back(0);
    Result.setValue(FixedType, ArgInts);
  }

  return Result;
}

RCParser::ParseOptionType RCParser::parseLanguageStmt() {
  ASSIGN_OR_RETURN(Args, readIntsWithCommas(/* min = */ 2, /* max = */ 2));
  return std::make_unique<LanguageResource>((*Args)[0], (*Args)[1]);
}

RCParser::ParseOptionType RCParser::parseCharacteristicsStmt() {
  ASSIGN_OR_RETURN(Arg, readInt());
  return std::make_unique<CharacteristicsStmt>(*Arg);
}

RCParser::ParseOptionType RCParser::parseVersionStmt() {
  ASSIGN_OR_RETURN(Arg, readInt());
  return std::make_unique<VersionStmt>(*Arg);
}

RCParser::ParseOptionType RCParser::parseCaptionStmt() {
  ASSIGN_OR_RETURN(Arg, readString());
  return std::make_unique<CaptionStmt>(*Arg);
}

RCParser::ParseOptionType RCParser::parseClassStmt() {
  ASSIGN_OR_RETURN(Arg, readIntOrString());
  return std::make_unique<ClassStmt>(*Arg);
}

RCParser::ParseOptionType RCParser::parseFontStmt(OptStmtType DialogType) {
  assert(DialogType != OptStmtType::BasicStmt);

  ASSIGN_OR_RETURN(SizeResult, readInt());
  RETURN_IF_ERROR(consumeType(Kind::Comma));
  ASSIGN_OR_RETURN(NameResult, readString());

  // Default values for the optional arguments.
  uint32_t FontWeight = 0;
  bool FontItalic = false;
  uint32_t FontCharset = 1;
  if (DialogType == OptStmtType::DialogExStmt) {
    if (consumeOptionalType(Kind::Comma)) {
      ASSIGN_OR_RETURN(Args, readIntsWithCommas(/* min = */ 0, /* max = */ 3));
      if (Args->size() >= 1)
        FontWeight = (*Args)[0];
      if (Args->size() >= 2)
        FontItalic = (*Args)[1] != 0;
      if (Args->size() >= 3)
        FontCharset = (*Args)[2];
    }
  }
  return std::make_unique<FontStmt>(*SizeResult, *NameResult, FontWeight,
                                    FontItalic, FontCharset);
}

RCParser::ParseOptionType RCParser::parseStyleStmt() {
  ASSIGN_OR_RETURN(Arg, readInt());
  return std::make_unique<StyleStmt>(*Arg);
}

RCParser::ParseOptionType RCParser::parseExStyleStmt() {
  ASSIGN_OR_RETURN(Arg, readInt());
  return std::make_unique<ExStyleStmt>(*Arg);
}

RCParser::ParseOptionType RCParser::parseMenuStmt() {
  ASSIGN_OR_RETURN(Arg, readIntOrString());
  return std::make_unique<MenuStmt>(*Arg);
}

Error RCParser::getExpectedError(const Twine &Message, bool IsAlreadyRead) {
  return make_error<ParserError>(
      Message, IsAlreadyRead ? std::prev(CurLoc) : CurLoc, End);
}

} // namespace rc
} // namespace llvm
