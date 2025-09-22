//===- Nodes.h - syntax nodes for C/C++ grammar constructs ----*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Syntax tree nodes for C, C++ and Objective-C grammar constructs.
//
// Nodes provide access to their syntactic components, e.g. IfStatement provides
// a way to get its condition, then and else branches, tokens for 'if' and
// 'else' keywords.
// When using the accessors, please assume they can return null. This happens
// because:
//   - the corresponding subnode is optional in the C++ grammar, e.g. an else
//     branch of an if statement,
//   - syntactic errors occurred while parsing the corresponding subnode.
// One notable exception is "introducer" keywords, e.g. the accessor for the
// 'if' keyword of an if statement will never return null.
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_TOOLING_SYNTAX_NODES_H
#define LLVM_CLANG_TOOLING_SYNTAX_NODES_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/Syntax/Tree.h"
namespace clang {
namespace syntax {

/// A kind of a syntax node, used for implementing casts. The ordering and
/// blocks of enumerator constants must correspond to the inheritance hierarchy
/// of syntax::Node.
enum class NodeKind : uint16_t {
#define CONCRETE_NODE(Kind, Base) Kind,
#include "clang/Tooling/Syntax/Nodes.inc"
};
/// For debugging purposes.
raw_ostream &operator<<(raw_ostream &OS, NodeKind K);

/// A relation between a parent and child node, e.g. 'left-hand-side of
/// a binary expression'. Used for implementing accessors.
///
/// In general `NodeRole`s should be named the same as their accessors.
///
/// Some roles describe parent/child relations that occur multiple times in
/// language grammar. We define only one role to describe all instances of such
/// recurring relations. For example, grammar for both "if" and "while"
/// statements requires an opening paren and a closing paren. The opening
/// paren token is assigned the OpenParen role regardless of whether it appears
/// as a child of IfStatement or WhileStatement node. More generally, when
/// grammar requires a certain fixed token (like a specific keyword, or an
/// opening paren), we define a role for this token and use it across all
/// grammar rules with the same requirement. Names of such reusable roles end
/// with a ~Token or a ~Keyword suffix.
enum class NodeRole : uint8_t {
  // Roles common to multiple node kinds.
  /// A node without a parent
  Detached,
  /// Children of an unknown semantic nature, e.g. skipped tokens, comments.
  Unknown,
  /// An opening parenthesis in argument lists and blocks, e.g. '{', '(', etc.
  OpenParen,
  /// A closing parenthesis in argument lists and blocks, e.g. '}', ')', etc.
  CloseParen,
  /// A keywords that introduces some grammar construct, e.g. 'if', 'try', etc.
  IntroducerKeyword,
  /// A token that represents a literal, e.g. 'nullptr', '1', 'true', etc.
  LiteralToken,
  /// Tokens or Keywords.
  ArrowToken,
  ExternKeyword,
  TemplateKeyword,
  /// An inner statement for those that have only a single child of kind
  /// statement, e.g. loop body for while, for, etc; inner statement for case,
  /// default, etc.
  BodyStatement,
  /// List API roles.
  ListElement,
  ListDelimiter,

  // Roles specific to particular node kinds.
  OperatorToken,
  Operand,
  LeftHandSide,
  RightHandSide,
  ReturnValue,
  CaseValue,
  ThenStatement,
  ElseKeyword,
  ElseStatement,
  Expression,
  Statement,
  Condition,
  Message,
  Declarator,
  Declaration,
  Size,
  Parameters,
  TrailingReturn,
  UnqualifiedId,
  Qualifier,
  SubExpression,
  Object,
  AccessToken,
  Member,
  Callee,
  Arguments,
  Declarators
};
/// For debugging purposes.
raw_ostream &operator<<(raw_ostream &OS, NodeRole R);

#include "clang/Tooling/Syntax/NodeClasses.inc"

/// Models a `nested-name-specifier`. C++ [expr.prim.id.qual]
/// e.g. the `std::vector<int>::` in `std::vector<int>::size`.
class NestedNameSpecifier final : public List {
public:
  NestedNameSpecifier() : List(NodeKind::NestedNameSpecifier) {}
  static bool classof(const Node *N);
  std::vector<NameSpecifier *> getSpecifiers();
  std::vector<List::ElementAndDelimiter<syntax::NameSpecifier>>
  getSpecifiersAndDoubleColons();
};

/// Models an `unqualified-id`. C++ [expr.prim.id.unqual]
/// e.g. the `size` in `std::vector<int>::size`.
class UnqualifiedId final : public Tree {
public:
  UnqualifiedId() : Tree(NodeKind::UnqualifiedId) {}
  static bool classof(const Node *N);
};

/// An expression of an unknown kind, i.e. one not currently handled by the
/// syntax tree.
class UnknownExpression final : public Expression {
public:
  UnknownExpression() : Expression(NodeKind::UnknownExpression) {}
  static bool classof(const Node *N);
};

/// Models arguments of a function call.
///   call-arguments:
///     delimited_list(expression, ',')
/// Note: This construct is a simplification of the grammar rule for
/// `expression-list`, that is used in the definition of `call-expression`
class CallArguments final : public List {
public:
  CallArguments() : List(NodeKind::CallArguments) {}
  static bool classof(const Node *N);
  std::vector<Expression *> getArguments();
  std::vector<List::ElementAndDelimiter<Expression>> getArgumentsAndCommas();
};

/// An abstract class for prefix and postfix unary operators.
class UnaryOperatorExpression : public Expression {
public:
  UnaryOperatorExpression(NodeKind K) : Expression(K) {}
  static bool classof(const Node *N);
  Leaf *getOperatorToken();
  Expression *getOperand();
};

/// <operator> <operand>
///
/// For example:
///   +a          -b
///   !c          not c
///   ~d          compl d
///   *e          &f
///   ++h         --h
///   __real i    __imag i
class PrefixUnaryOperatorExpression final : public UnaryOperatorExpression {
public:
  PrefixUnaryOperatorExpression()
      : UnaryOperatorExpression(NodeKind::PrefixUnaryOperatorExpression) {}
  static bool classof(const Node *N);
};

/// <operand> <operator>
///
/// For example:
///   a++
///   b--
class PostfixUnaryOperatorExpression final : public UnaryOperatorExpression {
public:
  PostfixUnaryOperatorExpression()
      : UnaryOperatorExpression(NodeKind::PostfixUnaryOperatorExpression) {}
  static bool classof(const Node *N);
};

/// <lhs> <operator> <rhs>
///
/// For example:
///   a + b
///   a bitor 1
///   a |= b
///   a and_eq b
class BinaryOperatorExpression final : public Expression {
public:
  BinaryOperatorExpression() : Expression(NodeKind::BinaryOperatorExpression) {}
  static bool classof(const Node *N);
  Expression *getLhs();
  Leaf *getOperatorToken();
  Expression *getRhs();
};

/// An abstract node for C++ statements, e.g. 'while', 'if', etc.
/// FIXME: add accessors for semicolon of statements that have it.
class Statement : public Tree {
public:
  Statement(NodeKind K) : Tree(K) {}
  static bool classof(const Node *N);
};

/// A statement of an unknown kind, i.e. one not currently handled by the syntax
/// tree.
class UnknownStatement final : public Statement {
public:
  UnknownStatement() : Statement(NodeKind::UnknownStatement) {}
  static bool classof(const Node *N);
};

/// E.g. 'int a, b = 10;'
class DeclarationStatement final : public Statement {
public:
  DeclarationStatement() : Statement(NodeKind::DeclarationStatement) {}
  static bool classof(const Node *N);
};

/// The no-op statement, i.e. ';'.
class EmptyStatement final : public Statement {
public:
  EmptyStatement() : Statement(NodeKind::EmptyStatement) {}
  static bool classof(const Node *N);
};

/// switch (<cond>) <body>
class SwitchStatement final : public Statement {
public:
  SwitchStatement() : Statement(NodeKind::SwitchStatement) {}
  static bool classof(const Node *N);
  Leaf *getSwitchKeyword();
  Statement *getBody();
};

/// case <value>: <body>
class CaseStatement final : public Statement {
public:
  CaseStatement() : Statement(NodeKind::CaseStatement) {}
  static bool classof(const Node *N);
  Leaf *getCaseKeyword();
  Expression *getCaseValue();
  Statement *getBody();
};

/// default: <body>
class DefaultStatement final : public Statement {
public:
  DefaultStatement() : Statement(NodeKind::DefaultStatement) {}
  static bool classof(const Node *N);
  Leaf *getDefaultKeyword();
  Statement *getBody();
};

/// if (cond) <then-statement> else <else-statement>
/// FIXME: add condition that models 'expression  or variable declaration'
class IfStatement final : public Statement {
public:
  IfStatement() : Statement(NodeKind::IfStatement) {}
  static bool classof(const Node *N);
  Leaf *getIfKeyword();
  Statement *getThenStatement();
  Leaf *getElseKeyword();
  Statement *getElseStatement();
};

/// for (<init>; <cond>; <increment>) <body>
class ForStatement final : public Statement {
public:
  ForStatement() : Statement(NodeKind::ForStatement) {}
  static bool classof(const Node *N);
  Leaf *getForKeyword();
  Statement *getBody();
};

/// while (<cond>) <body>
class WhileStatement final : public Statement {
public:
  WhileStatement() : Statement(NodeKind::WhileStatement) {}
  static bool classof(const Node *N);
  Leaf *getWhileKeyword();
  Statement *getBody();
};

/// continue;
class ContinueStatement final : public Statement {
public:
  ContinueStatement() : Statement(NodeKind::ContinueStatement) {}
  static bool classof(const Node *N);
  Leaf *getContinueKeyword();
};

/// break;
class BreakStatement final : public Statement {
public:
  BreakStatement() : Statement(NodeKind::BreakStatement) {}
  static bool classof(const Node *N);
  Leaf *getBreakKeyword();
};

/// return <expr>;
/// return;
class ReturnStatement final : public Statement {
public:
  ReturnStatement() : Statement(NodeKind::ReturnStatement) {}
  static bool classof(const Node *N);
  Leaf *getReturnKeyword();
  Expression *getReturnValue();
};

/// for (<decl> : <init>) <body>
class RangeBasedForStatement final : public Statement {
public:
  RangeBasedForStatement() : Statement(NodeKind::RangeBasedForStatement) {}
  static bool classof(const Node *N);
  Leaf *getForKeyword();
  Statement *getBody();
};

/// Expression in a statement position, e.g. functions calls inside compound
/// statements or inside a loop body.
class ExpressionStatement final : public Statement {
public:
  ExpressionStatement() : Statement(NodeKind::ExpressionStatement) {}
  static bool classof(const Node *N);
  Expression *getExpression();
};

/// { statement1; statement2; … }
class CompoundStatement final : public Statement {
public:
  CompoundStatement() : Statement(NodeKind::CompoundStatement) {}
  static bool classof(const Node *N);
  Leaf *getLbrace();
  /// FIXME: use custom iterator instead of 'vector'.
  std::vector<Statement *> getStatements();
  Leaf *getRbrace();
};

/// A declaration that can appear at the top-level. Note that this does *not*
/// correspond 1-to-1 to clang::Decl. Syntax trees distinguish between top-level
/// declarations (e.g. namespace definitions) and declarators (e.g. variables,
/// typedefs, etc.). Declarators are stored inside SimpleDeclaration.
class Declaration : public Tree {
public:
  Declaration(NodeKind K) : Tree(K) {}
  static bool classof(const Node *N);
};

/// Declaration of an unknown kind, e.g. not yet supported in syntax trees.
class UnknownDeclaration final : public Declaration {
public:
  UnknownDeclaration() : Declaration(NodeKind::UnknownDeclaration) {}
  static bool classof(const Node *N);
};

/// A semicolon in the top-level context. Does not declare anything.
class EmptyDeclaration final : public Declaration {
public:
  EmptyDeclaration() : Declaration(NodeKind::EmptyDeclaration) {}
  static bool classof(const Node *N);
};

/// static_assert(<condition>, <message>)
/// static_assert(<condition>)
class StaticAssertDeclaration final : public Declaration {
public:
  StaticAssertDeclaration() : Declaration(NodeKind::StaticAssertDeclaration) {}
  static bool classof(const Node *N);
  Expression *getCondition();
  Expression *getMessage();
};

/// extern <string-literal> declaration
/// extern <string-literal> { <decls>  }
class LinkageSpecificationDeclaration final : public Declaration {
public:
  LinkageSpecificationDeclaration()
      : Declaration(NodeKind::LinkageSpecificationDeclaration) {}
  static bool classof(const Node *N);
};

class DeclaratorList final : public List {
public:
  DeclaratorList() : List(NodeKind::DeclaratorList) {}
  static bool classof(const Node *N);
  std::vector<SimpleDeclarator *> getDeclarators();
  std::vector<List::ElementAndDelimiter<syntax::SimpleDeclarator>>
  getDeclaratorsAndCommas();
};

/// Groups multiple declarators (e.g. variables, typedefs, etc.) together. All
/// grouped declarators share the same declaration specifiers (e.g. 'int' or
/// 'typedef').
class SimpleDeclaration final : public Declaration {
public:
  SimpleDeclaration() : Declaration(NodeKind::SimpleDeclaration) {}
  static bool classof(const Node *N);
  /// FIXME: use custom iterator instead of 'vector'.
  std::vector<SimpleDeclarator *> getDeclarators();
};

/// template <template-parameters> <declaration>
class TemplateDeclaration final : public Declaration {
public:
  TemplateDeclaration() : Declaration(NodeKind::TemplateDeclaration) {}
  static bool classof(const Node *N);
  Leaf *getTemplateKeyword();
  Declaration *getDeclaration();
};

/// template <declaration>
/// Examples:
///     template struct X<int>
///     template void foo<int>()
///     template int var<double>
class ExplicitTemplateInstantiation final : public Declaration {
public:
  ExplicitTemplateInstantiation()
      : Declaration(NodeKind::ExplicitTemplateInstantiation) {}
  static bool classof(const Node *N);
  Leaf *getTemplateKeyword();
  Leaf *getExternKeyword();
  Declaration *getDeclaration();
};

/// namespace <name> { <decls> }
class NamespaceDefinition final : public Declaration {
public:
  NamespaceDefinition() : Declaration(NodeKind::NamespaceDefinition) {}
  static bool classof(const Node *N);
};

/// namespace <name> = <namespace-reference>
class NamespaceAliasDefinition final : public Declaration {
public:
  NamespaceAliasDefinition()
      : Declaration(NodeKind::NamespaceAliasDefinition) {}
  static bool classof(const Node *N);
};

/// using namespace <name>
class UsingNamespaceDirective final : public Declaration {
public:
  UsingNamespaceDirective() : Declaration(NodeKind::UsingNamespaceDirective) {}
  static bool classof(const Node *N);
};

/// using <scope>::<name>
/// using typename <scope>::<name>
class UsingDeclaration final : public Declaration {
public:
  UsingDeclaration() : Declaration(NodeKind::UsingDeclaration) {}
  static bool classof(const Node *N);
};

/// using <name> = <type>
class TypeAliasDeclaration final : public Declaration {
public:
  TypeAliasDeclaration() : Declaration(NodeKind::TypeAliasDeclaration) {}
  static bool classof(const Node *N);
};

/// Covers a name, an initializer and a part of the type outside declaration
/// specifiers. Examples are:
///     `*a` in `int *a`
///     `a[10]` in `int a[10]`
///     `*a = nullptr` in `int *a = nullptr`
/// Declarators can be unnamed too:
///     `**` in `new int**`
///     `* = nullptr` in `void foo(int* = nullptr)`
/// Most declarators you encounter are instances of SimpleDeclarator. They may
/// contain an inner declarator inside parentheses, we represent it as
/// ParenDeclarator. E.g.
///     `(*a)` in `int (*a) = 10`
class Declarator : public Tree {
public:
  Declarator(NodeKind K) : Tree(K) {}
  static bool classof(const Node *N);
};

/// A top-level declarator without parentheses. See comment of Declarator for
/// more details.
class SimpleDeclarator final : public Declarator {
public:
  SimpleDeclarator() : Declarator(NodeKind::SimpleDeclarator) {}
  static bool classof(const Node *N);
};

/// Declarator inside parentheses.
/// E.g. `(***a)` from `int (***a) = nullptr;`
/// See comment of Declarator for more details.
class ParenDeclarator final : public Declarator {
public:
  ParenDeclarator() : Declarator(NodeKind::ParenDeclarator) {}
  static bool classof(const Node *N);
  Leaf *getLparen();
  Leaf *getRparen();
};

/// Array size specified inside a declarator.
/// E.g:
///   `[10]` in `int a[10];`
///   `[static 10]` in `void f(int xs[static 10]);`
class ArraySubscript final : public Tree {
public:
  ArraySubscript() : Tree(NodeKind::ArraySubscript) {}
  static bool classof(const Node *N);
  // TODO: add an accessor for the "static" keyword.
  Leaf *getLbracket();
  Expression *getSize();
  Leaf *getRbracket();
};

/// Trailing return type after the parameter list, including the arrow token.
/// E.g. `-> int***`.
class TrailingReturnType final : public Tree {
public:
  TrailingReturnType() : Tree(NodeKind::TrailingReturnType) {}
  static bool classof(const Node *N);
  // TODO: add accessors for specifiers.
  Leaf *getArrowToken();
  // FIXME: This should be a `type-id` following the grammar. Fix this once we
  // have a representation of `type-id`s.
  SimpleDeclarator *getDeclarator();
};

/// Models a `parameter-declaration-list` which appears within
/// `parameters-and-qualifiers`. See C++ [dcl.fct]
class ParameterDeclarationList final : public List {
public:
  ParameterDeclarationList() : List(NodeKind::ParameterDeclarationList) {}
  static bool classof(const Node *N);
  std::vector<SimpleDeclaration *> getParameterDeclarations();
  std::vector<List::ElementAndDelimiter<syntax::SimpleDeclaration>>
  getParametersAndCommas();
};

/// Parameter list for a function type and a trailing return type, if the
/// function has one.
/// E.g.:
///  `(int a) volatile ` in `int foo(int a) volatile;`
///  `(int a) &&` in `int foo(int a) &&;`
///  `() -> int` in `auto foo() -> int;`
///  `() const` in `int foo() const;`
///  `() noexcept` in `int foo() noexcept;`
///  `() throw()` in `int foo() throw();`
///
/// (!) override doesn't belong here.
class ParametersAndQualifiers final : public Tree {
public:
  ParametersAndQualifiers() : Tree(NodeKind::ParametersAndQualifiers) {}
  static bool classof(const Node *N);
  Leaf *getLparen();
  ParameterDeclarationList *getParameters();
  Leaf *getRparen();
  TrailingReturnType *getTrailingReturn();
};

/// Member pointer inside a declarator
/// E.g. `X::*` in `int X::* a = 0;`
class MemberPointer final : public Tree {
public:
  MemberPointer() : Tree(NodeKind::MemberPointer) {}
  static bool classof(const Node *N);
};

#define CONCRETE_NODE(Kind, Base)                                              \
  inline bool Kind::classof(const Node *N) {                                   \
    return N->getKind() == NodeKind::Kind;                                     \
  }
#define ABSTRACT_NODE(Kind, Base, First, Last)                                 \
  inline bool Kind::classof(const Node *N) {                                   \
    return N->getKind() >= NodeKind::First && N->getKind() <= NodeKind::Last;  \
  }
#include "clang/Tooling/Syntax/Nodes.inc"

} // namespace syntax
} // namespace clang
#endif
