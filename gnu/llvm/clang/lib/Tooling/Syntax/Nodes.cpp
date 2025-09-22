//===- Nodes.cpp ----------------------------------------------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/Syntax/Nodes.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

raw_ostream &syntax::operator<<(raw_ostream &OS, NodeKind K) {
  switch (K) {
#define CONCRETE_NODE(Kind, Parent)                                            \
  case NodeKind::Kind:                                                         \
    return OS << #Kind;
#include "clang/Tooling/Syntax/Nodes.inc"
  }
  llvm_unreachable("unknown node kind");
}

raw_ostream &syntax::operator<<(raw_ostream &OS, NodeRole R) {
  switch (R) {
  case syntax::NodeRole::Detached:
    return OS << "Detached";
  case syntax::NodeRole::Unknown:
    return OS << "Unknown";
  case syntax::NodeRole::OpenParen:
    return OS << "OpenParen";
  case syntax::NodeRole::CloseParen:
    return OS << "CloseParen";
  case syntax::NodeRole::IntroducerKeyword:
    return OS << "IntroducerKeyword";
  case syntax::NodeRole::LiteralToken:
    return OS << "LiteralToken";
  case syntax::NodeRole::ArrowToken:
    return OS << "ArrowToken";
  case syntax::NodeRole::ExternKeyword:
    return OS << "ExternKeyword";
  case syntax::NodeRole::TemplateKeyword:
    return OS << "TemplateKeyword";
  case syntax::NodeRole::BodyStatement:
    return OS << "BodyStatement";
  case syntax::NodeRole::ListElement:
    return OS << "ListElement";
  case syntax::NodeRole::ListDelimiter:
    return OS << "ListDelimiter";
  case syntax::NodeRole::CaseValue:
    return OS << "CaseValue";
  case syntax::NodeRole::ReturnValue:
    return OS << "ReturnValue";
  case syntax::NodeRole::ThenStatement:
    return OS << "ThenStatement";
  case syntax::NodeRole::ElseKeyword:
    return OS << "ElseKeyword";
  case syntax::NodeRole::ElseStatement:
    return OS << "ElseStatement";
  case syntax::NodeRole::OperatorToken:
    return OS << "OperatorToken";
  case syntax::NodeRole::Operand:
    return OS << "Operand";
  case syntax::NodeRole::LeftHandSide:
    return OS << "LeftHandSide";
  case syntax::NodeRole::RightHandSide:
    return OS << "RightHandSide";
  case syntax::NodeRole::Expression:
    return OS << "Expression";
  case syntax::NodeRole::Statement:
    return OS << "Statement";
  case syntax::NodeRole::Condition:
    return OS << "Condition";
  case syntax::NodeRole::Message:
    return OS << "Message";
  case syntax::NodeRole::Declarator:
    return OS << "Declarator";
  case syntax::NodeRole::Declaration:
    return OS << "Declaration";
  case syntax::NodeRole::Size:
    return OS << "Size";
  case syntax::NodeRole::Parameters:
    return OS << "Parameters";
  case syntax::NodeRole::TrailingReturn:
    return OS << "TrailingReturn";
  case syntax::NodeRole::UnqualifiedId:
    return OS << "UnqualifiedId";
  case syntax::NodeRole::Qualifier:
    return OS << "Qualifier";
  case syntax::NodeRole::SubExpression:
    return OS << "SubExpression";
  case syntax::NodeRole::Object:
    return OS << "Object";
  case syntax::NodeRole::AccessToken:
    return OS << "AccessToken";
  case syntax::NodeRole::Member:
    return OS << "Member";
  case syntax::NodeRole::Callee:
    return OS << "Callee";
  case syntax::NodeRole::Arguments:
    return OS << "Arguments";
  case syntax::NodeRole::Declarators:
    return OS << "Declarators";
  }
  llvm_unreachable("invalid role");
}

// We could have an interator in list to not pay memory costs of temporary
// vector
std::vector<syntax::NameSpecifier *>
syntax::NestedNameSpecifier::getSpecifiers() {
  auto SpecifiersAsNodes = getElementsAsNodes();
  std::vector<syntax::NameSpecifier *> Children;
  for (const auto &Element : SpecifiersAsNodes) {
    Children.push_back(llvm::cast<syntax::NameSpecifier>(Element));
  }
  return Children;
}

std::vector<syntax::List::ElementAndDelimiter<syntax::NameSpecifier>>
syntax::NestedNameSpecifier::getSpecifiersAndDoubleColons() {
  auto SpecifiersAsNodesAndDoubleColons = getElementsAsNodesAndDelimiters();
  std::vector<syntax::List::ElementAndDelimiter<syntax::NameSpecifier>>
      Children;
  for (const auto &SpecifierAndDoubleColon : SpecifiersAsNodesAndDoubleColons) {
    Children.push_back(
        {llvm::cast<syntax::NameSpecifier>(SpecifierAndDoubleColon.element),
         SpecifierAndDoubleColon.delimiter});
  }
  return Children;
}

std::vector<syntax::Expression *> syntax::CallArguments::getArguments() {
  auto ArgumentsAsNodes = getElementsAsNodes();
  std::vector<syntax::Expression *> Children;
  for (const auto &ArgumentAsNode : ArgumentsAsNodes) {
    Children.push_back(llvm::cast<syntax::Expression>(ArgumentAsNode));
  }
  return Children;
}

std::vector<syntax::List::ElementAndDelimiter<syntax::Expression>>
syntax::CallArguments::getArgumentsAndCommas() {
  auto ArgumentsAsNodesAndCommas = getElementsAsNodesAndDelimiters();
  std::vector<syntax::List::ElementAndDelimiter<syntax::Expression>> Children;
  for (const auto &ArgumentAsNodeAndComma : ArgumentsAsNodesAndCommas) {
    Children.push_back(
        {llvm::cast<syntax::Expression>(ArgumentAsNodeAndComma.element),
         ArgumentAsNodeAndComma.delimiter});
  }
  return Children;
}

std::vector<syntax::SimpleDeclaration *>
syntax::ParameterDeclarationList::getParameterDeclarations() {
  auto ParametersAsNodes = getElementsAsNodes();
  std::vector<syntax::SimpleDeclaration *> Children;
  for (const auto &ParameterAsNode : ParametersAsNodes) {
    Children.push_back(llvm::cast<syntax::SimpleDeclaration>(ParameterAsNode));
  }
  return Children;
}

std::vector<syntax::List::ElementAndDelimiter<syntax::SimpleDeclaration>>
syntax::ParameterDeclarationList::getParametersAndCommas() {
  auto ParametersAsNodesAndCommas = getElementsAsNodesAndDelimiters();
  std::vector<syntax::List::ElementAndDelimiter<syntax::SimpleDeclaration>>
      Children;
  for (const auto &ParameterAsNodeAndComma : ParametersAsNodesAndCommas) {
    Children.push_back(
        {llvm::cast<syntax::SimpleDeclaration>(ParameterAsNodeAndComma.element),
         ParameterAsNodeAndComma.delimiter});
  }
  return Children;
}

std::vector<syntax::SimpleDeclarator *>
syntax::DeclaratorList::getDeclarators() {
  auto DeclaratorsAsNodes = getElementsAsNodes();
  std::vector<syntax::SimpleDeclarator *> Children;
  for (const auto &DeclaratorAsNode : DeclaratorsAsNodes) {
    Children.push_back(llvm::cast<syntax::SimpleDeclarator>(DeclaratorAsNode));
  }
  return Children;
}

std::vector<syntax::List::ElementAndDelimiter<syntax::SimpleDeclarator>>
syntax::DeclaratorList::getDeclaratorsAndCommas() {
  auto DeclaratorsAsNodesAndCommas = getElementsAsNodesAndDelimiters();
  std::vector<syntax::List::ElementAndDelimiter<syntax::SimpleDeclarator>>
      Children;
  for (const auto &DeclaratorAsNodeAndComma : DeclaratorsAsNodesAndCommas) {
    Children.push_back(
        {llvm::cast<syntax::SimpleDeclarator>(DeclaratorAsNodeAndComma.element),
         DeclaratorAsNodeAndComma.delimiter});
  }
  return Children;
}

syntax::Expression *syntax::BinaryOperatorExpression::getLhs() {
  return cast_or_null<syntax::Expression>(
      findChild(syntax::NodeRole::LeftHandSide));
}

syntax::Leaf *syntax::UnaryOperatorExpression::getOperatorToken() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::OperatorToken));
}

syntax::Expression *syntax::UnaryOperatorExpression::getOperand() {
  return cast_or_null<syntax::Expression>(findChild(syntax::NodeRole::Operand));
}

syntax::Leaf *syntax::BinaryOperatorExpression::getOperatorToken() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::OperatorToken));
}

syntax::Expression *syntax::BinaryOperatorExpression::getRhs() {
  return cast_or_null<syntax::Expression>(
      findChild(syntax::NodeRole::RightHandSide));
}

syntax::Leaf *syntax::SwitchStatement::getSwitchKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Statement *syntax::SwitchStatement::getBody() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::BodyStatement));
}

syntax::Leaf *syntax::CaseStatement::getCaseKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Expression *syntax::CaseStatement::getCaseValue() {
  return cast_or_null<syntax::Expression>(
      findChild(syntax::NodeRole::CaseValue));
}

syntax::Statement *syntax::CaseStatement::getBody() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::BodyStatement));
}

syntax::Leaf *syntax::DefaultStatement::getDefaultKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Statement *syntax::DefaultStatement::getBody() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::BodyStatement));
}

syntax::Leaf *syntax::IfStatement::getIfKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Statement *syntax::IfStatement::getThenStatement() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::ThenStatement));
}

syntax::Leaf *syntax::IfStatement::getElseKeyword() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::ElseKeyword));
}

syntax::Statement *syntax::IfStatement::getElseStatement() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::ElseStatement));
}

syntax::Leaf *syntax::ForStatement::getForKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Statement *syntax::ForStatement::getBody() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::BodyStatement));
}

syntax::Leaf *syntax::WhileStatement::getWhileKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Statement *syntax::WhileStatement::getBody() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::BodyStatement));
}

syntax::Leaf *syntax::ContinueStatement::getContinueKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Leaf *syntax::BreakStatement::getBreakKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Leaf *syntax::ReturnStatement::getReturnKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Expression *syntax::ReturnStatement::getReturnValue() {
  return cast_or_null<syntax::Expression>(
      findChild(syntax::NodeRole::ReturnValue));
}

syntax::Leaf *syntax::RangeBasedForStatement::getForKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Statement *syntax::RangeBasedForStatement::getBody() {
  return cast_or_null<syntax::Statement>(
      findChild(syntax::NodeRole::BodyStatement));
}

syntax::Expression *syntax::ExpressionStatement::getExpression() {
  return cast_or_null<syntax::Expression>(
      findChild(syntax::NodeRole::Expression));
}

syntax::Leaf *syntax::CompoundStatement::getLbrace() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::OpenParen));
}

std::vector<syntax::Statement *> syntax::CompoundStatement::getStatements() {
  std::vector<syntax::Statement *> Children;
  for (auto *C = getFirstChild(); C; C = C->getNextSibling()) {
    assert(C->getRole() == syntax::NodeRole::Statement);
    Children.push_back(cast<syntax::Statement>(C));
  }
  return Children;
}

syntax::Leaf *syntax::CompoundStatement::getRbrace() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::CloseParen));
}

syntax::Expression *syntax::StaticAssertDeclaration::getCondition() {
  return cast_or_null<syntax::Expression>(
      findChild(syntax::NodeRole::Condition));
}

syntax::Expression *syntax::StaticAssertDeclaration::getMessage() {
  return cast_or_null<syntax::Expression>(findChild(syntax::NodeRole::Message));
}

std::vector<syntax::SimpleDeclarator *>
syntax::SimpleDeclaration::getDeclarators() {
  std::vector<syntax::SimpleDeclarator *> Children;
  for (auto *C = getFirstChild(); C; C = C->getNextSibling()) {
    if (C->getRole() == syntax::NodeRole::Declarator)
      Children.push_back(cast<syntax::SimpleDeclarator>(C));
  }
  return Children;
}

syntax::Leaf *syntax::TemplateDeclaration::getTemplateKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Declaration *syntax::TemplateDeclaration::getDeclaration() {
  return cast_or_null<syntax::Declaration>(
      findChild(syntax::NodeRole::Declaration));
}

syntax::Leaf *syntax::ExplicitTemplateInstantiation::getTemplateKeyword() {
  return cast_or_null<syntax::Leaf>(
      findChild(syntax::NodeRole::IntroducerKeyword));
}

syntax::Leaf *syntax::ExplicitTemplateInstantiation::getExternKeyword() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::ExternKeyword));
}

syntax::Declaration *syntax::ExplicitTemplateInstantiation::getDeclaration() {
  return cast_or_null<syntax::Declaration>(
      findChild(syntax::NodeRole::Declaration));
}

syntax::Leaf *syntax::ParenDeclarator::getLparen() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::OpenParen));
}

syntax::Leaf *syntax::ParenDeclarator::getRparen() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::CloseParen));
}

syntax::Leaf *syntax::ArraySubscript::getLbracket() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::OpenParen));
}

syntax::Expression *syntax::ArraySubscript::getSize() {
  return cast_or_null<syntax::Expression>(findChild(syntax::NodeRole::Size));
}

syntax::Leaf *syntax::ArraySubscript::getRbracket() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::CloseParen));
}

syntax::Leaf *syntax::TrailingReturnType::getArrowToken() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::ArrowToken));
}

syntax::SimpleDeclarator *syntax::TrailingReturnType::getDeclarator() {
  return cast_or_null<syntax::SimpleDeclarator>(
      findChild(syntax::NodeRole::Declarator));
}

syntax::Leaf *syntax::ParametersAndQualifiers::getLparen() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::OpenParen));
}

syntax::ParameterDeclarationList *
syntax::ParametersAndQualifiers::getParameters() {
  return cast_or_null<syntax::ParameterDeclarationList>(
      findChild(syntax::NodeRole::Parameters));
}

syntax::Leaf *syntax::ParametersAndQualifiers::getRparen() {
  return cast_or_null<syntax::Leaf>(findChild(syntax::NodeRole::CloseParen));
}

syntax::TrailingReturnType *
syntax::ParametersAndQualifiers::getTrailingReturn() {
  return cast_or_null<syntax::TrailingReturnType>(
      findChild(syntax::NodeRole::TrailingReturn));
}

#define NODE(Kind, Parent)                                                     \
  static_assert(sizeof(syntax::Kind) > 0, "Missing Node subclass definition");
#include "clang/Tooling/Syntax/Nodes.inc"
