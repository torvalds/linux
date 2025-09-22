//===- Nodes.td - Node types in the Syntax Tree grammar -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines concrete nodes in the syntax tree.
// The archetypes they fall into (Sequence, List etc) are defined in Syntax.td.
//
// The C++ classes for the archetypes themselves are written by hand, and the
// concrete node classes will be generated. Migration to TableGen is not
// complete, so currently there is a mix of generated and hand-authored code.
//
//===----------------------------------------------------------------------===//

include "clang/Tooling/Syntax/Syntax.td"

def TranslationUnit : Unconstrained {
  let documentation = [{
    A root node for a translation unit. Parent is always null.
  }];
}

def UnqualifiedId : External<Tree> {}

// Lists
def List : External<Tree> {}
def DeclaratorList : External<List> {}
def ParameterDeclarationList : External<List> {}
def CallArguments : External<List> {}
def NestedNameSpecifier : External<List> {}

def Expression : Alternatives {
  let documentation = [{
    A base class for all expressions. Note that expressions are not statements,
    even though they are in clang.
  }];
}
def UnknownExpression : External<Expression> {}
def UnaryOperatorExpression : External<Tree> {}
def PrefixUnaryOperatorExpression : External<UnaryOperatorExpression> {}
def PostfixUnaryOperatorExpression : External<UnaryOperatorExpression> {}
def BinaryOperatorExpression : External<Expression> {}
def ParenExpression : Sequence<Expression> {
  let documentation = [{
    Models a parenthesized expression `(E)`. C++ [expr.prim.paren]
    e.g. `(3 + 2)` in `a = 1 + (3 + 2);`
  }];
  let children = [
    Role<"OpenParen", Token<"l_paren">>,
    Role<"SubExpression", Expression>,
    Role<"CloseParen", Token<"r_paren">>,
  ];
}
def LiteralExpression : Alternatives<Expression> {
  let documentation = [{
    Expression for literals. C++ [lex.literal]
  }];
}
def IntegerLiteralExpression : Sequence<LiteralExpression> {
  let documentation = [{
    Expression for integer literals. C++ [lex.icon]
  }];
  let children = [
    Role<"LiteralToken", Token<"numeric_constant">>,
  ];
}
defvar AnyCharacterLiteral = AnyToken<[
  "char_constant", "wide_char_constant", "utf8_char_constant",
  "utf16_char_constant", "utf32_char_constant"
]>;
def CharacterLiteralExpression : Sequence<LiteralExpression> {
  let documentation = [{
    Expression for character literals. C++ [lex.ccon]
  }];
  let children = [
    Role<"LiteralToken", AnyCharacterLiteral>,
  ];
}
def FloatingLiteralExpression : Sequence<LiteralExpression> {
  let documentation = [{
    Expression for floating-point literals. C++ [lex.fcon]
  }];
  let children = [
    Role<"LiteralToken", Token<"numeric_constant">>,
  ];
}
defvar AnyStringLiteral = AnyToken<[
  "string_literal", "wide_string_literal", "utf8_string_literal",
  "utf16_string_literal", "utf32_string_literal"
]>;
def StringLiteralExpression : Sequence<LiteralExpression> {
  let documentation = [{
    Expression for string-literals. C++ [lex.string]
  }];
  // FIXME: string literals may consist of multiple tokens.
  // These are merged in phase 6, but tokens are captured after phase 4.
  // The child here should be a list of literal tokens instead.
  let children = [
    Role<"LiteralToken", AnyStringLiteral>,
  ];
}
def BoolLiteralExpression : Sequence<LiteralExpression> {
  let documentation = [{
    Expression for boolean literals. C++ [lex.bool]
  }];
  let children = [
    Role<"LiteralToken", AnyToken<["kw_false","kw_true"]>>,
  ];
}
def CxxNullPtrExpression : Sequence<LiteralExpression> {
  let documentation = [{
    Expression for the `nullptr` literal. C++ [lex.nullptr]
  }];
  let children = [
    Role<"LiteralToken", Keyword<"nullptr">>,
  ];
}
def UserDefinedLiteralExpression : Alternatives<LiteralExpression> {
  let documentation = [{
    Expression for user-defined literal. C++ [lex.ext]
    user-defined-literal:
      user-defined-integer-literal
      user-defined-floating-point-literal
      user-defined-string-literal
      user-defined-character-literal
  }];
}
def IntegerUserDefinedLiteralExpression : Sequence<UserDefinedLiteralExpression> {
  let documentation = [{
    Expression for user-defined-integer-literal. C++ [lex.ext]
  }];
  let children = [
    Role<"LiteralToken", Keyword<"numeric_constant">>,
  ];
}
def FloatUserDefinedLiteralExpression : Sequence<UserDefinedLiteralExpression> {
  let documentation = [{
    Expression for user-defined-floating-point-literal. C++ [lex.ext]
  }];
  let children = [
    Role<"LiteralToken", Keyword<"numeric_constant">>,
  ];
}
def CharUserDefinedLiteralExpression : Sequence<UserDefinedLiteralExpression> {
  let documentation = [{
    Expression for user-defined-character-literal. C++ [lex.ext]
  }];
  let children = [
    Role<"LiteralToken", AnyCharacterLiteral>,
  ];
}
def StringUserDefinedLiteralExpression : Sequence<UserDefinedLiteralExpression> {
  let documentation = [{
    Expression for user-defined-string-literal. C++ [lex.ext]
  }];
  let children = [
    Role<"LiteralToken", AnyStringLiteral>,
  ];
}
def IdExpression : Sequence<Expression> {
  let documentation = [{
    Models an `id-expression`, e.g. `std::vector<int>::size`.
    C++ [expr.prim.id]
    id-expression:
      unqualified-id
      qualified-id
    qualified-id:
      nested-name-specifier template_opt unqualified-id
  }];
  let children = [
    Role<"Qualifier", Optional<NestedNameSpecifier>>,
    Role<"TemplateKeyword", Optional<Keyword<"template">>>,
    Role<"UnqualifiedId", UnqualifiedId>,
  ];
}
def MemberExpression : Sequence<Expression> {
  let documentation = [{
    Models a class member access. C++ [expr.ref]
    member-expression:
      expression -> template_opt id-expression
      expression .  template_opt id-expression
    e.g. `x.a`, `xp->a`

    Note: An implicit member access inside a class, i.e. `a` instead of
    `this->a`, is an `id-expression`.
  }];
  let children = [
    Role<"Object", Expression>,
    Role<"AccessToken", AnyToken<["period","arrow"]>>,
    Role<"TemplateKeyword", Optional<Keyword<"template">>>,
    Role<"Member", IdExpression>,
  ];
}
def ThisExpression : Sequence<Expression> {
  let documentation = [{
    Models a this expression `this`. C++ [expr.prim.this]
  }];
  let children = [
    Role<"IntroducerKeyword", Keyword<"this">>,
  ];
}
def CallExpression : Sequence<Expression> {
  let documentation = [{
    A function call. C++ [expr.call]
    call-expression:
      expression '(' call-arguments ')'
    e.g `f(1, '2')` or `this->Base::f()`
  }];
  let children = [
    Role<"Callee", Expression>,
    Role<"OpenParen", Token<"l_paren">>,
    Role<"Arguments", CallArguments>,
    Role<"CloseParen", Token<"r_paren">>,
  ];
}

// Statements.
def Statement : External<Tree> {}
def UnknownStatement : External<Statement> {}
def DeclarationStatement : External<Statement> {}
def EmptyStatement : External<Statement> {}
def SwitchStatement : External<Statement> {}
def CaseStatement : External<Statement> {}
def DefaultStatement : External<Statement> {}
def IfStatement : External<Statement> {}
def ForStatement : External<Statement> {}
def WhileStatement : External<Statement> {}
def ContinueStatement : External<Statement> {}
def BreakStatement : External<Statement> {}
def ReturnStatement : External<Statement> {}
def RangeBasedForStatement : External<Statement> {}
def ExpressionStatement : External<Statement> {}
def CompoundStatement : External<Statement> {}

// Declarations.
def Declaration : External<Tree> {}
def UnknownDeclaration : External<Declaration> {}
def EmptyDeclaration : External<Declaration> {}
def StaticAssertDeclaration : External<Declaration> {}
def LinkageSpecificationDeclaration : External<Declaration> {}
def SimpleDeclaration : External<Declaration> {}
def TemplateDeclaration : External<Declaration> {}
def ExplicitTemplateInstantiation : External<Declaration> {}
def NamespaceDefinition : External<Declaration> {}
def NamespaceAliasDefinition : External<Declaration> {}
def UsingNamespaceDirective : External<Declaration> {}
def UsingDeclaration : External<Declaration> {}
def TypeAliasDeclaration : External<Declaration> {}

// Declarators.
def Declarator : External<Tree> {}
def SimpleDeclarator : External<Declarator> {}
def ParenDeclarator : External<Declarator> {}

def ArraySubscript : External<Tree> {}
def TrailingReturnType : External<Tree> {}
def ParametersAndQualifiers : External<Tree> {}
def MemberPointer : External<Tree> {}

// Name Specifiers.
def NameSpecifier : Alternatives {
  let documentation = [{
    A sequence of these specifiers make a `nested-name-specifier`.
    e.g. the `std` or `vector<int>` in `std::vector<int>::size`.
  }];
}
def GlobalNameSpecifier : Unconstrained<NameSpecifier> {
  let documentation = [{
    The global namespace name specifier, this specifier doesn't correspond to a
    token instead an absence of tokens before a `::` characterizes it, in
    `::std::vector<int>` it would be characterized by the absence of a token
    before the first `::`
  }];
}
def DecltypeNameSpecifier : Unconstrained<NameSpecifier> {
  let documentation = [{
    A name specifier holding a decltype, of the form: `decltype ( expression ) `
    e.g. the `decltype(s)` in `decltype(s)::size`.
  }];
}
def IdentifierNameSpecifier : Unconstrained<NameSpecifier> {
  let documentation = [{
    A identifier name specifier, of the form `identifier`
    e.g. the `std` in `std::vector<int>::size`.
  }];
}
def SimpleTemplateNameSpecifier : Unconstrained<NameSpecifier> {
  let documentation = [{
    A name specifier with a simple-template-id, of the form `template_opt
    identifier < template-args >` e.g. the `vector<int>` in
    `std::vector<int>::size`.
  }];
}
