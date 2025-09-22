//===- Synthesis.cpp ------------------------------------------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Basic/TokenKinds.h"
#include "clang/Tooling/Syntax/BuildTree.h"
#include "clang/Tooling/Syntax/Tree.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "clang/Tooling/Syntax/TokenBufferTokenManager.h"

using namespace clang;

/// Exposes private syntax tree APIs required to implement node synthesis.
/// Should not be used for anything else.
class clang::syntax::FactoryImpl {
public:
  static void setCanModify(syntax::Node *N) { N->CanModify = true; }

  static void prependChildLowLevel(syntax::Tree *T, syntax::Node *Child,
                                   syntax::NodeRole R) {
    T->prependChildLowLevel(Child, R);
  }
  static void appendChildLowLevel(syntax::Tree *T, syntax::Node *Child,
                                  syntax::NodeRole R) {
    T->appendChildLowLevel(Child, R);
  }

  static std::pair<FileID, ArrayRef<Token>>
  lexBuffer(TokenBufferTokenManager &TBTM,
            std::unique_ptr<llvm::MemoryBuffer> Buffer) {
    return TBTM.lexBuffer(std::move(Buffer));
  }
};

// FIXME: `createLeaf` is based on `syntax::tokenize` internally, as such it
// doesn't support digraphs or line continuations.
syntax::Leaf *clang::syntax::createLeaf(syntax::Arena &A,
                                        TokenBufferTokenManager &TBTM,
                                        tok::TokenKind K, StringRef Spelling) {
  auto Tokens =
      FactoryImpl::lexBuffer(TBTM, llvm::MemoryBuffer::getMemBufferCopy(Spelling))
          .second;
  assert(Tokens.size() == 1);
  assert(Tokens.front().kind() == K &&
         "spelling is not lexed into the expected kind of token");

  auto *Leaf = new (A.getAllocator()) syntax::Leaf(
    reinterpret_cast<TokenManager::Key>(Tokens.begin()));
  syntax::FactoryImpl::setCanModify(Leaf);
  Leaf->assertInvariants();
  return Leaf;
}

syntax::Leaf *clang::syntax::createLeaf(syntax::Arena &A,
                                        TokenBufferTokenManager &TBTM,
                                        tok::TokenKind K) {
  const auto *Spelling = tok::getPunctuatorSpelling(K);
  if (!Spelling)
    Spelling = tok::getKeywordSpelling(K);
  assert(Spelling &&
         "Cannot infer the spelling of the token from its token kind.");
  return createLeaf(A, TBTM, K, Spelling);
}

namespace {
// Allocates the concrete syntax `Tree` according to its `NodeKind`.
syntax::Tree *allocateTree(syntax::Arena &A, syntax::NodeKind Kind) {
  switch (Kind) {
  case syntax::NodeKind::Leaf:
    assert(false);
    break;
  case syntax::NodeKind::TranslationUnit:
    return new (A.getAllocator()) syntax::TranslationUnit;
  case syntax::NodeKind::UnknownExpression:
    return new (A.getAllocator()) syntax::UnknownExpression;
  case syntax::NodeKind::ParenExpression:
    return new (A.getAllocator()) syntax::ParenExpression;
  case syntax::NodeKind::ThisExpression:
    return new (A.getAllocator()) syntax::ThisExpression;
  case syntax::NodeKind::IntegerLiteralExpression:
    return new (A.getAllocator()) syntax::IntegerLiteralExpression;
  case syntax::NodeKind::CharacterLiteralExpression:
    return new (A.getAllocator()) syntax::CharacterLiteralExpression;
  case syntax::NodeKind::FloatingLiteralExpression:
    return new (A.getAllocator()) syntax::FloatingLiteralExpression;
  case syntax::NodeKind::StringLiteralExpression:
    return new (A.getAllocator()) syntax::StringLiteralExpression;
  case syntax::NodeKind::BoolLiteralExpression:
    return new (A.getAllocator()) syntax::BoolLiteralExpression;
  case syntax::NodeKind::CxxNullPtrExpression:
    return new (A.getAllocator()) syntax::CxxNullPtrExpression;
  case syntax::NodeKind::IntegerUserDefinedLiteralExpression:
    return new (A.getAllocator()) syntax::IntegerUserDefinedLiteralExpression;
  case syntax::NodeKind::FloatUserDefinedLiteralExpression:
    return new (A.getAllocator()) syntax::FloatUserDefinedLiteralExpression;
  case syntax::NodeKind::CharUserDefinedLiteralExpression:
    return new (A.getAllocator()) syntax::CharUserDefinedLiteralExpression;
  case syntax::NodeKind::StringUserDefinedLiteralExpression:
    return new (A.getAllocator()) syntax::StringUserDefinedLiteralExpression;
  case syntax::NodeKind::PrefixUnaryOperatorExpression:
    return new (A.getAllocator()) syntax::PrefixUnaryOperatorExpression;
  case syntax::NodeKind::PostfixUnaryOperatorExpression:
    return new (A.getAllocator()) syntax::PostfixUnaryOperatorExpression;
  case syntax::NodeKind::BinaryOperatorExpression:
    return new (A.getAllocator()) syntax::BinaryOperatorExpression;
  case syntax::NodeKind::UnqualifiedId:
    return new (A.getAllocator()) syntax::UnqualifiedId;
  case syntax::NodeKind::IdExpression:
    return new (A.getAllocator()) syntax::IdExpression;
  case syntax::NodeKind::CallExpression:
    return new (A.getAllocator()) syntax::CallExpression;
  case syntax::NodeKind::UnknownStatement:
    return new (A.getAllocator()) syntax::UnknownStatement;
  case syntax::NodeKind::DeclarationStatement:
    return new (A.getAllocator()) syntax::DeclarationStatement;
  case syntax::NodeKind::EmptyStatement:
    return new (A.getAllocator()) syntax::EmptyStatement;
  case syntax::NodeKind::SwitchStatement:
    return new (A.getAllocator()) syntax::SwitchStatement;
  case syntax::NodeKind::CaseStatement:
    return new (A.getAllocator()) syntax::CaseStatement;
  case syntax::NodeKind::DefaultStatement:
    return new (A.getAllocator()) syntax::DefaultStatement;
  case syntax::NodeKind::IfStatement:
    return new (A.getAllocator()) syntax::IfStatement;
  case syntax::NodeKind::ForStatement:
    return new (A.getAllocator()) syntax::ForStatement;
  case syntax::NodeKind::WhileStatement:
    return new (A.getAllocator()) syntax::WhileStatement;
  case syntax::NodeKind::ContinueStatement:
    return new (A.getAllocator()) syntax::ContinueStatement;
  case syntax::NodeKind::BreakStatement:
    return new (A.getAllocator()) syntax::BreakStatement;
  case syntax::NodeKind::ReturnStatement:
    return new (A.getAllocator()) syntax::ReturnStatement;
  case syntax::NodeKind::RangeBasedForStatement:
    return new (A.getAllocator()) syntax::RangeBasedForStatement;
  case syntax::NodeKind::ExpressionStatement:
    return new (A.getAllocator()) syntax::ExpressionStatement;
  case syntax::NodeKind::CompoundStatement:
    return new (A.getAllocator()) syntax::CompoundStatement;
  case syntax::NodeKind::UnknownDeclaration:
    return new (A.getAllocator()) syntax::UnknownDeclaration;
  case syntax::NodeKind::EmptyDeclaration:
    return new (A.getAllocator()) syntax::EmptyDeclaration;
  case syntax::NodeKind::StaticAssertDeclaration:
    return new (A.getAllocator()) syntax::StaticAssertDeclaration;
  case syntax::NodeKind::LinkageSpecificationDeclaration:
    return new (A.getAllocator()) syntax::LinkageSpecificationDeclaration;
  case syntax::NodeKind::SimpleDeclaration:
    return new (A.getAllocator()) syntax::SimpleDeclaration;
  case syntax::NodeKind::TemplateDeclaration:
    return new (A.getAllocator()) syntax::TemplateDeclaration;
  case syntax::NodeKind::ExplicitTemplateInstantiation:
    return new (A.getAllocator()) syntax::ExplicitTemplateInstantiation;
  case syntax::NodeKind::NamespaceDefinition:
    return new (A.getAllocator()) syntax::NamespaceDefinition;
  case syntax::NodeKind::NamespaceAliasDefinition:
    return new (A.getAllocator()) syntax::NamespaceAliasDefinition;
  case syntax::NodeKind::UsingNamespaceDirective:
    return new (A.getAllocator()) syntax::UsingNamespaceDirective;
  case syntax::NodeKind::UsingDeclaration:
    return new (A.getAllocator()) syntax::UsingDeclaration;
  case syntax::NodeKind::TypeAliasDeclaration:
    return new (A.getAllocator()) syntax::TypeAliasDeclaration;
  case syntax::NodeKind::SimpleDeclarator:
    return new (A.getAllocator()) syntax::SimpleDeclarator;
  case syntax::NodeKind::ParenDeclarator:
    return new (A.getAllocator()) syntax::ParenDeclarator;
  case syntax::NodeKind::ArraySubscript:
    return new (A.getAllocator()) syntax::ArraySubscript;
  case syntax::NodeKind::TrailingReturnType:
    return new (A.getAllocator()) syntax::TrailingReturnType;
  case syntax::NodeKind::ParametersAndQualifiers:
    return new (A.getAllocator()) syntax::ParametersAndQualifiers;
  case syntax::NodeKind::MemberPointer:
    return new (A.getAllocator()) syntax::MemberPointer;
  case syntax::NodeKind::GlobalNameSpecifier:
    return new (A.getAllocator()) syntax::GlobalNameSpecifier;
  case syntax::NodeKind::DecltypeNameSpecifier:
    return new (A.getAllocator()) syntax::DecltypeNameSpecifier;
  case syntax::NodeKind::IdentifierNameSpecifier:
    return new (A.getAllocator()) syntax::IdentifierNameSpecifier;
  case syntax::NodeKind::SimpleTemplateNameSpecifier:
    return new (A.getAllocator()) syntax::SimpleTemplateNameSpecifier;
  case syntax::NodeKind::NestedNameSpecifier:
    return new (A.getAllocator()) syntax::NestedNameSpecifier;
  case syntax::NodeKind::MemberExpression:
    return new (A.getAllocator()) syntax::MemberExpression;
  case syntax::NodeKind::CallArguments:
    return new (A.getAllocator()) syntax::CallArguments;
  case syntax::NodeKind::ParameterDeclarationList:
    return new (A.getAllocator()) syntax::ParameterDeclarationList;
  case syntax::NodeKind::DeclaratorList:
    return new (A.getAllocator()) syntax::DeclaratorList;
  }
  llvm_unreachable("unknown node kind");
}
} // namespace

syntax::Tree *clang::syntax::createTree(
    syntax::Arena &A,
    ArrayRef<std::pair<syntax::Node *, syntax::NodeRole>> Children,
    syntax::NodeKind K) {
  auto *T = allocateTree(A, K);
  FactoryImpl::setCanModify(T);
  for (const auto &Child : Children)
    FactoryImpl::appendChildLowLevel(T, Child.first, Child.second);

  T->assertInvariants();
  return T;
}

syntax::Node *clang::syntax::deepCopyExpandingMacros(syntax::Arena &A,
                                                    TokenBufferTokenManager &TBTM,
                                                     const syntax::Node *N) {
  if (const auto *L = dyn_cast<syntax::Leaf>(N))
    // `L->getToken()` gives us the expanded token, thus we implicitly expand
    // any macros here.
    return createLeaf(A, TBTM, TBTM.getToken(L->getTokenKey())->kind(),
                       TBTM.getText(L->getTokenKey()));

  const auto *T = cast<syntax::Tree>(N);
  std::vector<std::pair<syntax::Node *, syntax::NodeRole>> Children;
  for (const auto *Child = T->getFirstChild(); Child;
       Child = Child->getNextSibling())
    Children.push_back({deepCopyExpandingMacros(A, TBTM, Child), Child->getRole()});

  return createTree(A, Children, N->getKind());
}

syntax::EmptyStatement *clang::syntax::createEmptyStatement(syntax::Arena &A,  TokenBufferTokenManager &TBTM) {
  return cast<EmptyStatement>(
      createTree(A, {{createLeaf(A, TBTM, tok::semi), NodeRole::Unknown}},
                 NodeKind::EmptyStatement));
}
