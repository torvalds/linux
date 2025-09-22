//===---------- IssueHash.cpp - Generate identification hashes --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/IssueHash.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"

#include <functional>
#include <optional>
#include <sstream>
#include <string>

using namespace clang;

// Get a string representation of the parts of the signature that can be
// overloaded on.
static std::string GetSignature(const FunctionDecl *Target) {
  if (!Target)
    return "";
  std::string Signature;

  // When a flow sensitive bug happens in templated code we should not generate
  // distinct hash value for every instantiation. Use the signature from the
  // primary template.
  if (const FunctionDecl *InstantiatedFrom =
          Target->getTemplateInstantiationPattern())
    Target = InstantiatedFrom;

  if (!isa<CXXConstructorDecl>(Target) && !isa<CXXDestructorDecl>(Target) &&
      !isa<CXXConversionDecl>(Target))
    Signature.append(Target->getReturnType().getAsString()).append(" ");
  Signature.append(Target->getQualifiedNameAsString()).append("(");

  for (int i = 0, paramsCount = Target->getNumParams(); i < paramsCount; ++i) {
    if (i)
      Signature.append(", ");
    Signature.append(Target->getParamDecl(i)->getType().getAsString());
  }

  if (Target->isVariadic())
    Signature.append(", ...");
  Signature.append(")");

  const auto *TargetT =
      llvm::dyn_cast_or_null<FunctionType>(Target->getType().getTypePtr());

  if (!TargetT || !isa<CXXMethodDecl>(Target))
    return Signature;

  if (TargetT->isConst())
    Signature.append(" const");
  if (TargetT->isVolatile())
    Signature.append(" volatile");
  if (TargetT->isRestrict())
    Signature.append(" restrict");

  if (const auto *TargetPT =
          dyn_cast_or_null<FunctionProtoType>(Target->getType().getTypePtr())) {
    switch (TargetPT->getRefQualifier()) {
    case RQ_LValue:
      Signature.append(" &");
      break;
    case RQ_RValue:
      Signature.append(" &&");
      break;
    default:
      break;
    }
  }

  return Signature;
}

static std::string GetEnclosingDeclContextSignature(const Decl *D) {
  if (!D)
    return "";

  if (const auto *ND = dyn_cast<NamedDecl>(D)) {
    std::string DeclName;

    switch (ND->getKind()) {
    case Decl::Namespace:
    case Decl::Record:
    case Decl::CXXRecord:
    case Decl::Enum:
      DeclName = ND->getQualifiedNameAsString();
      break;
    case Decl::CXXConstructor:
    case Decl::CXXDestructor:
    case Decl::CXXConversion:
    case Decl::CXXMethod:
    case Decl::Function:
      DeclName = GetSignature(dyn_cast_or_null<FunctionDecl>(ND));
      break;
    case Decl::ObjCMethod:
      // ObjC Methods can not be overloaded, qualified name uniquely identifies
      // the method.
      DeclName = ND->getQualifiedNameAsString();
      break;
    default:
      break;
    }

    return DeclName;
  }

  return "";
}

static StringRef GetNthLineOfFile(std::optional<llvm::MemoryBufferRef> Buffer,
                                  int Line) {
  if (!Buffer)
    return "";

  llvm::line_iterator LI(*Buffer, false);
  for (; !LI.is_at_eof() && LI.line_number() != Line; ++LI)
    ;

  return *LI;
}

static std::string NormalizeLine(const SourceManager &SM, const FullSourceLoc &L,
                                 const LangOptions &LangOpts) {
  static StringRef Whitespaces = " \t\n";

  StringRef Str = GetNthLineOfFile(SM.getBufferOrNone(L.getFileID(), L),
                                   L.getExpansionLineNumber());
  StringRef::size_type col = Str.find_first_not_of(Whitespaces);
  if (col == StringRef::npos)
    col = 1; // The line only contains whitespace.
  else
    col++;
  SourceLocation StartOfLine =
      SM.translateLineCol(SM.getFileID(L), L.getExpansionLineNumber(), col);
  std::optional<llvm::MemoryBufferRef> Buffer =
      SM.getBufferOrNone(SM.getFileID(StartOfLine), StartOfLine);
  if (!Buffer)
    return {};

  const char *BufferPos = SM.getCharacterData(StartOfLine);

  Token Token;
  Lexer Lexer(SM.getLocForStartOfFile(SM.getFileID(StartOfLine)), LangOpts,
              Buffer->getBufferStart(), BufferPos, Buffer->getBufferEnd());

  size_t NextStart = 0;
  std::ostringstream LineBuff;
  while (!Lexer.LexFromRawLexer(Token) && NextStart < 2) {
    if (Token.isAtStartOfLine() && NextStart++ > 0)
      continue;
    LineBuff << std::string(SM.getCharacterData(Token.getLocation()),
                            Token.getLength());
  }

  return LineBuff.str();
}

static llvm::SmallString<32> GetMD5HashOfContent(StringRef Content) {
  llvm::MD5 Hash;
  llvm::MD5::MD5Result MD5Res;
  SmallString<32> Res;

  Hash.update(Content);
  Hash.final(MD5Res);
  llvm::MD5::stringifyResult(MD5Res, Res);

  return Res;
}

std::string clang::getIssueString(const FullSourceLoc &IssueLoc,
                                  StringRef CheckerName,
                                  StringRef WarningMessage,
                                  const Decl *IssueDecl,
                                  const LangOptions &LangOpts) {
  static StringRef Delimiter = "$";

  return (llvm::Twine(CheckerName) + Delimiter +
          GetEnclosingDeclContextSignature(IssueDecl) + Delimiter +
          Twine(IssueLoc.getExpansionColumnNumber()) + Delimiter +
          NormalizeLine(IssueLoc.getManager(), IssueLoc, LangOpts) +
          Delimiter + WarningMessage)
      .str();
}

SmallString<32> clang::getIssueHash(const FullSourceLoc &IssueLoc,
                                    StringRef CheckerName,
                                    StringRef WarningMessage,
                                    const Decl *IssueDecl,
                                    const LangOptions &LangOpts) {

  return GetMD5HashOfContent(getIssueString(
      IssueLoc, CheckerName, WarningMessage, IssueDecl, LangOpts));
}
