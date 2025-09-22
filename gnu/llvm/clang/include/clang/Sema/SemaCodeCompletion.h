//===----- SemaCodeCompletion.h ------ Code completion support ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares facilities that support code completion.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMACODECOMPLETION_H
#define LLVM_CLANG_SEMA_SEMACODECOMPLETION_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

namespace clang {

class SemaCodeCompletion : public SemaBase {
public:
  SemaCodeCompletion(Sema &S, CodeCompleteConsumer *CompletionConsumer);

  using TemplateTy = OpaquePtr<TemplateName>;
  using DeclGroupPtrTy = OpaquePtr<DeclGroupRef>;

  /// Code-completion consumer.
  CodeCompleteConsumer *CodeCompleter;

  /// Describes the context in which code completion occurs.
  enum ParserCompletionContext {
    /// Code completion occurs at top-level or namespace context.
    PCC_Namespace,
    /// Code completion occurs within a class, struct, or union.
    PCC_Class,
    /// Code completion occurs within an Objective-C interface, protocol,
    /// or category.
    PCC_ObjCInterface,
    /// Code completion occurs within an Objective-C implementation or
    /// category implementation
    PCC_ObjCImplementation,
    /// Code completion occurs within the list of instance variables
    /// in an Objective-C interface, protocol, category, or implementation.
    PCC_ObjCInstanceVariableList,
    /// Code completion occurs following one or more template
    /// headers.
    PCC_Template,
    /// Code completion occurs following one or more template
    /// headers within a class.
    PCC_MemberTemplate,
    /// Code completion occurs within an expression.
    PCC_Expression,
    /// Code completion occurs within a statement, which may
    /// also be an expression or a declaration.
    PCC_Statement,
    /// Code completion occurs at the beginning of the
    /// initialization statement (or expression) in a for loop.
    PCC_ForInit,
    /// Code completion occurs within the condition of an if,
    /// while, switch, or for statement.
    PCC_Condition,
    /// Code completion occurs within the body of a function on a
    /// recovery path, where we do not have a specific handle on our position
    /// in the grammar.
    PCC_RecoveryInFunction,
    /// Code completion occurs where only a type is permitted.
    PCC_Type,
    /// Code completion occurs in a parenthesized expression, which
    /// might also be a type cast.
    PCC_ParenthesizedExpression,
    /// Code completion occurs within a sequence of declaration
    /// specifiers within a function, method, or block.
    PCC_LocalDeclarationSpecifiers,
    /// Code completion occurs at top-level in a REPL session
    PCC_TopLevelOrExpression,
  };

  void CodeCompleteModuleImport(SourceLocation ImportLoc, ModuleIdPath Path);
  void CodeCompleteOrdinaryName(Scope *S,
                                ParserCompletionContext CompletionContext);
  void CodeCompleteDeclSpec(Scope *S, DeclSpec &DS, bool AllowNonIdentifiers,
                            bool AllowNestedNameSpecifiers);

  struct CodeCompleteExpressionData;
  void CodeCompleteExpression(Scope *S, const CodeCompleteExpressionData &Data);
  void CodeCompleteExpression(Scope *S, QualType PreferredType,
                              bool IsParenthesized = false);
  void CodeCompleteMemberReferenceExpr(Scope *S, Expr *Base, Expr *OtherOpBase,
                                       SourceLocation OpLoc, bool IsArrow,
                                       bool IsBaseExprStatement,
                                       QualType PreferredType);
  void CodeCompletePostfixExpression(Scope *S, ExprResult LHS,
                                     QualType PreferredType);
  void CodeCompleteTag(Scope *S, unsigned TagSpec);
  void CodeCompleteTypeQualifiers(DeclSpec &DS);
  void CodeCompleteFunctionQualifiers(DeclSpec &DS, Declarator &D,
                                      const VirtSpecifiers *VS = nullptr);
  void CodeCompleteBracketDeclarator(Scope *S);
  void CodeCompleteCase(Scope *S);
  enum class AttributeCompletion {
    Attribute,
    Scope,
    None,
  };
  void CodeCompleteAttribute(
      AttributeCommonInfo::Syntax Syntax,
      AttributeCompletion Completion = AttributeCompletion::Attribute,
      const IdentifierInfo *Scope = nullptr);
  /// Determines the preferred type of the current function argument, by
  /// examining the signatures of all possible overloads.
  /// Returns null if unknown or ambiguous, or if code completion is off.
  ///
  /// If the code completion point has been reached, also reports the function
  /// signatures that were considered.
  ///
  /// FIXME: rename to GuessCallArgumentType to reduce confusion.
  QualType ProduceCallSignatureHelp(Expr *Fn, ArrayRef<Expr *> Args,
                                    SourceLocation OpenParLoc);
  QualType ProduceConstructorSignatureHelp(QualType Type, SourceLocation Loc,
                                           ArrayRef<Expr *> Args,
                                           SourceLocation OpenParLoc,
                                           bool Braced);
  QualType ProduceCtorInitMemberSignatureHelp(
      Decl *ConstructorDecl, CXXScopeSpec SS, ParsedType TemplateTypeTy,
      ArrayRef<Expr *> ArgExprs, IdentifierInfo *II, SourceLocation OpenParLoc,
      bool Braced);
  QualType ProduceTemplateArgumentSignatureHelp(
      TemplateTy, ArrayRef<ParsedTemplateArgument>, SourceLocation LAngleLoc);
  void CodeCompleteInitializer(Scope *S, Decl *D);
  /// Trigger code completion for a record of \p BaseType. \p InitExprs are
  /// expressions in the initializer list seen so far and \p D is the current
  /// Designation being parsed.
  void CodeCompleteDesignator(const QualType BaseType,
                              llvm::ArrayRef<Expr *> InitExprs,
                              const Designation &D);
  void CodeCompleteAfterIf(Scope *S, bool IsBracedThen);

  void CodeCompleteQualifiedId(Scope *S, CXXScopeSpec &SS, bool EnteringContext,
                               bool IsUsingDeclaration, QualType BaseType,
                               QualType PreferredType);
  void CodeCompleteUsing(Scope *S);
  void CodeCompleteUsingDirective(Scope *S);
  void CodeCompleteNamespaceDecl(Scope *S);
  void CodeCompleteNamespaceAliasDecl(Scope *S);
  void CodeCompleteOperatorName(Scope *S);
  void CodeCompleteConstructorInitializer(
      Decl *Constructor, ArrayRef<CXXCtorInitializer *> Initializers);

  void CodeCompleteLambdaIntroducer(Scope *S, LambdaIntroducer &Intro,
                                    bool AfterAmpersand);
  void CodeCompleteAfterFunctionEquals(Declarator &D);

  void CodeCompleteObjCAtDirective(Scope *S);
  void CodeCompleteObjCAtVisibility(Scope *S);
  void CodeCompleteObjCAtStatement(Scope *S);
  void CodeCompleteObjCAtExpression(Scope *S);
  void CodeCompleteObjCPropertyFlags(Scope *S, ObjCDeclSpec &ODS);
  void CodeCompleteObjCPropertyGetter(Scope *S);
  void CodeCompleteObjCPropertySetter(Scope *S);
  void CodeCompleteObjCPassingType(Scope *S, ObjCDeclSpec &DS,
                                   bool IsParameter);
  void CodeCompleteObjCMessageReceiver(Scope *S);
  void CodeCompleteObjCSuperMessage(Scope *S, SourceLocation SuperLoc,
                                    ArrayRef<const IdentifierInfo *> SelIdents,
                                    bool AtArgumentExpression);
  void CodeCompleteObjCClassMessage(Scope *S, ParsedType Receiver,
                                    ArrayRef<const IdentifierInfo *> SelIdents,
                                    bool AtArgumentExpression,
                                    bool IsSuper = false);
  void CodeCompleteObjCInstanceMessage(
      Scope *S, Expr *Receiver, ArrayRef<const IdentifierInfo *> SelIdents,
      bool AtArgumentExpression, ObjCInterfaceDecl *Super = nullptr);
  void CodeCompleteObjCForCollection(Scope *S, DeclGroupPtrTy IterationVar);
  void CodeCompleteObjCSelector(Scope *S,
                                ArrayRef<const IdentifierInfo *> SelIdents);
  void
  CodeCompleteObjCProtocolReferences(ArrayRef<IdentifierLocPair> Protocols);
  void CodeCompleteObjCProtocolDecl(Scope *S);
  void CodeCompleteObjCInterfaceDecl(Scope *S);
  void CodeCompleteObjCClassForwardDecl(Scope *S);
  void CodeCompleteObjCSuperclass(Scope *S, IdentifierInfo *ClassName,
                                  SourceLocation ClassNameLoc);
  void CodeCompleteObjCImplementationDecl(Scope *S);
  void CodeCompleteObjCInterfaceCategory(Scope *S, IdentifierInfo *ClassName,
                                         SourceLocation ClassNameLoc);
  void CodeCompleteObjCImplementationCategory(Scope *S,
                                              IdentifierInfo *ClassName,
                                              SourceLocation ClassNameLoc);
  void CodeCompleteObjCPropertyDefinition(Scope *S);
  void CodeCompleteObjCPropertySynthesizeIvar(Scope *S,
                                              IdentifierInfo *PropertyName);
  void CodeCompleteObjCMethodDecl(Scope *S,
                                  std::optional<bool> IsInstanceMethod,
                                  ParsedType ReturnType);
  void CodeCompleteObjCMethodDeclSelector(
      Scope *S, bool IsInstanceMethod, bool AtParameterName,
      ParsedType ReturnType, ArrayRef<const IdentifierInfo *> SelIdents);
  void CodeCompleteObjCClassPropertyRefExpr(Scope *S,
                                            const IdentifierInfo &ClassName,
                                            SourceLocation ClassNameLoc,
                                            bool IsBaseExprStatement);
  void CodeCompletePreprocessorDirective(bool InConditional);
  void CodeCompleteInPreprocessorConditionalExclusion(Scope *S);
  void CodeCompletePreprocessorMacroName(bool IsDefinition);
  void CodeCompletePreprocessorExpression();
  void CodeCompletePreprocessorMacroArgument(Scope *S, IdentifierInfo *Macro,
                                             MacroInfo *MacroInfo,
                                             unsigned Argument);
  void CodeCompleteIncludedFile(llvm::StringRef Dir, bool IsAngled);
  void CodeCompleteNaturalLanguage();
  void CodeCompleteAvailabilityPlatformName();
  void
  GatherGlobalCodeCompletions(CodeCompletionAllocator &Allocator,
                              CodeCompletionTUInfo &CCTUInfo,
                              SmallVectorImpl<CodeCompletionResult> &Results);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMACODECOMPLETION_H
