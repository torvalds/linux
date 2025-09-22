//===-- ASTResultSynthesizer.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_ASTRESULTSYNTHESIZER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_ASTRESULTSYNTHESIZER_H

#include "lldb/Target/Target.h"
#include "clang/Sema/SemaConsumer.h"

namespace clang {
class CompoundStmt;
class DeclContext;
class NamedDecl;
class ObjCMethodDecl;
class TypeDecl;
} // namespace clang

namespace lldb_private {

/// \class ASTResultSynthesizer ASTResultSynthesizer.h
/// "lldb/Expression/ASTResultSynthesizer.h" Adds a result variable
/// declaration to the ASTs for an expression.
///
/// Users expect the expression "i + 3" to return a result, even if a result
/// variable wasn't specifically declared.  To fulfil this requirement, LLDB
/// adds a result variable to the expression, transforming it to "int
/// $__lldb_expr_result = i + 3."  The IR transformers ensure that the
/// resulting variable is mapped to the right piece of memory.
/// ASTResultSynthesizer's job is to add the variable and its initialization
/// to the ASTs for the expression, and it does so by acting as a SemaConsumer
/// for Clang.
class ASTResultSynthesizer : public clang::SemaConsumer {
public:
  /// Constructor
  ///
  /// \param[in] passthrough
  ///     Since the ASTs must typically go through to the Clang code generator
  ///     in order to produce LLVM IR, this SemaConsumer must allow them to
  ///     pass to the next step in the chain after processing.  Passthrough is
  ///     the next ASTConsumer, or NULL if none is required.
  ///
  /// \param[in] top_level
  ///     If true, register all top-level Decls and don't try to handle the
  ///     main function.
  ///
  /// \param[in] target
  ///     The target, which contains the persistent variable store and the
  ///     AST importer.
  ASTResultSynthesizer(clang::ASTConsumer *passthrough, bool top_level,
                       Target &target);

  /// Destructor
  ~ASTResultSynthesizer() override;

  /// Link this consumer with a particular AST context
  ///
  /// \param[in] Context
  ///     This AST context will be used for types and identifiers, and also
  ///     forwarded to the passthrough consumer, if one exists.
  void Initialize(clang::ASTContext &Context) override;

  /// Examine a list of Decls to find the function $__lldb_expr and transform
  /// its code
  ///
  /// \param[in] D
  ///     The list of Decls to search.  These may contain LinkageSpecDecls,
  ///     which need to be searched recursively.  That job falls to
  ///     TransformTopLevelDecl.
  bool HandleTopLevelDecl(clang::DeclGroupRef D) override;

  /// Passthrough stub
  void HandleTranslationUnit(clang::ASTContext &Ctx) override;

  /// Passthrough stub
  void HandleTagDeclDefinition(clang::TagDecl *D) override;

  /// Passthrough stub
  void CompleteTentativeDefinition(clang::VarDecl *D) override;

  /// Passthrough stub
  void HandleVTable(clang::CXXRecordDecl *RD) override;

  /// Passthrough stub
  void PrintStats() override;

  /// Set the Sema object to use when performing transforms, and pass it on
  ///
  /// \param[in] S
  ///     The Sema to use.  Because Sema isn't externally visible, this class
  ///     casts it to an Action for actual use.
  void InitializeSema(clang::Sema &S) override;

  /// Reset the Sema to NULL now that transformations are done
  void ForgetSema() override;

  /// The parse has succeeded, so record its persistent decls
  void CommitPersistentDecls();

private:
  /// Hunt the given Decl for FunctionDecls named $__lldb_expr, recursing as
  /// necessary through LinkageSpecDecls, and calling SynthesizeResult on
  /// anything that was found
  ///
  /// \param[in] D
  ///     The Decl to hunt.
  void TransformTopLevelDecl(clang::Decl *D);

  /// Process an Objective-C method and produce the result variable and
  /// initialization
  ///
  /// \param[in] MethodDecl
  ///     The method to process.
  bool SynthesizeObjCMethodResult(clang::ObjCMethodDecl *MethodDecl);

  /// Process a function and produce the result variable and initialization
  ///
  /// \param[in] FunDecl
  ///     The function to process.
  bool SynthesizeFunctionResult(clang::FunctionDecl *FunDecl);

  /// Process a function body and produce the result variable and
  /// initialization
  ///
  /// \param[in] Body
  ///     The body of the function.
  ///
  /// \param[in] DC
  ///     The DeclContext of the function, into which the result variable
  ///     is inserted.
  bool SynthesizeBodyResult(clang::CompoundStmt *Body, clang::DeclContext *DC);

  /// Given a DeclContext for a function or method, find all types declared in
  /// the context and record any persistent types found.
  ///
  /// \param[in] FunDeclCtx
  ///     The context for the function to process.
  void RecordPersistentTypes(clang::DeclContext *FunDeclCtx);

  /// Given a TypeDecl, if it declares a type whose name starts with a dollar
  /// sign, register it as a pointer type in the target's scratch AST context.
  void MaybeRecordPersistentType(clang::TypeDecl *D);

  /// Given a NamedDecl, register it as a pointer type in the target's scratch
  /// AST context.
  void RecordPersistentDecl(clang::NamedDecl *D);

  clang::ASTContext
      *m_ast_context; ///< The AST context to use for identifiers and types.
  clang::ASTConsumer *m_passthrough; ///< The ASTConsumer down the chain, for
                                     ///passthrough.  NULL if it's a
                                     ///SemaConsumer.
  clang::SemaConsumer *m_passthrough_sema; ///< The SemaConsumer down the chain,
                                           ///for passthrough.  NULL if it's an
                                           ///ASTConsumer.

  std::vector<clang::NamedDecl *> m_decls; ///< Persistent declarations to
                                           ///register assuming the expression
                                           ///succeeds.

  Target &m_target;    ///< The target, which contains the persistent variable
                       ///store and the
  clang::Sema *m_sema; ///< The Sema to use.
  bool m_top_level;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_ASTRESULTSYNTHESIZER_H
