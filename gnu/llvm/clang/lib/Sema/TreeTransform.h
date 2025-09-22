//===------- TreeTransform.h - Semantic Tree Transformation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
//  This file implements a semantic tree transformation that takes a given
//  AST and rebuilds it, possibly transforming some nodes in the process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_TREETRANSFORM_H
#define LLVM_CLANG_LIB_SEMA_TREETRANSFORM_H

#include "CoroutineStmtBuilder.h"
#include "TypeLocBuilder.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/DiagnosticParse.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenACC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/SemaPseudoObject.h"
#include "clang/Sema/SemaSYCL.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <optional>

using namespace llvm::omp;

namespace clang {
using namespace sema;

/// A semantic tree transformation that allows one to transform one
/// abstract syntax tree into another.
///
/// A new tree transformation is defined by creating a new subclass \c X of
/// \c TreeTransform<X> and then overriding certain operations to provide
/// behavior specific to that transformation. For example, template
/// instantiation is implemented as a tree transformation where the
/// transformation of TemplateTypeParmType nodes involves substituting the
/// template arguments for their corresponding template parameters; a similar
/// transformation is performed for non-type template parameters and
/// template template parameters.
///
/// This tree-transformation template uses static polymorphism to allow
/// subclasses to customize any of its operations. Thus, a subclass can
/// override any of the transformation or rebuild operators by providing an
/// operation with the same signature as the default implementation. The
/// overriding function should not be virtual.
///
/// Semantic tree transformations are split into two stages, either of which
/// can be replaced by a subclass. The "transform" step transforms an AST node
/// or the parts of an AST node using the various transformation functions,
/// then passes the pieces on to the "rebuild" step, which constructs a new AST
/// node of the appropriate kind from the pieces. The default transformation
/// routines recursively transform the operands to composite AST nodes (e.g.,
/// the pointee type of a PointerType node) and, if any of those operand nodes
/// were changed by the transformation, invokes the rebuild operation to create
/// a new AST node.
///
/// Subclasses can customize the transformation at various levels. The
/// most coarse-grained transformations involve replacing TransformType(),
/// TransformExpr(), TransformDecl(), TransformNestedNameSpecifierLoc(),
/// TransformTemplateName(), or TransformTemplateArgument() with entirely
/// new implementations.
///
/// For more fine-grained transformations, subclasses can replace any of the
/// \c TransformXXX functions (where XXX is the name of an AST node, e.g.,
/// PointerType, StmtExpr) to alter the transformation. As mentioned previously,
/// replacing TransformTemplateTypeParmType() allows template instantiation
/// to substitute template arguments for their corresponding template
/// parameters. Additionally, subclasses can override the \c RebuildXXX
/// functions to control how AST nodes are rebuilt when their operands change.
/// By default, \c TreeTransform will invoke semantic analysis to rebuild
/// AST nodes. However, certain other tree transformations (e.g, cloning) may
/// be able to use more efficient rebuild steps.
///
/// There are a handful of other functions that can be overridden, allowing one
/// to avoid traversing nodes that don't need any transformation
/// (\c AlreadyTransformed()), force rebuilding AST nodes even when their
/// operands have not changed (\c AlwaysRebuild()), and customize the
/// default locations and entity names used for type-checking
/// (\c getBaseLocation(), \c getBaseEntity()).
template<typename Derived>
class TreeTransform {
  /// Private RAII object that helps us forget and then re-remember
  /// the template argument corresponding to a partially-substituted parameter
  /// pack.
  class ForgetPartiallySubstitutedPackRAII {
    Derived &Self;
    TemplateArgument Old;

  public:
    ForgetPartiallySubstitutedPackRAII(Derived &Self) : Self(Self) {
      Old = Self.ForgetPartiallySubstitutedPack();
    }

    ~ForgetPartiallySubstitutedPackRAII() {
      Self.RememberPartiallySubstitutedPack(Old);
    }
  };

protected:
  Sema &SemaRef;

  /// The set of local declarations that have been transformed, for
  /// cases where we are forced to build new declarations within the transformer
  /// rather than in the subclass (e.g., lambda closure types).
  llvm::DenseMap<Decl *, Decl *> TransformedLocalDecls;

public:
  /// Initializes a new tree transformer.
  TreeTransform(Sema &SemaRef) : SemaRef(SemaRef) { }

  /// Retrieves a reference to the derived class.
  Derived &getDerived() { return static_cast<Derived&>(*this); }

  /// Retrieves a reference to the derived class.
  const Derived &getDerived() const {
    return static_cast<const Derived&>(*this);
  }

  static inline ExprResult Owned(Expr *E) { return E; }
  static inline StmtResult Owned(Stmt *S) { return S; }

  /// Retrieves a reference to the semantic analysis object used for
  /// this tree transform.
  Sema &getSema() const { return SemaRef; }

  /// Whether the transformation should always rebuild AST nodes, even
  /// if none of the children have changed.
  ///
  /// Subclasses may override this function to specify when the transformation
  /// should rebuild all AST nodes.
  ///
  /// We must always rebuild all AST nodes when performing variadic template
  /// pack expansion, in order to avoid violating the AST invariant that each
  /// statement node appears at most once in its containing declaration.
  bool AlwaysRebuild() { return SemaRef.ArgumentPackSubstitutionIndex != -1; }

  /// Whether the transformation is forming an expression or statement that
  /// replaces the original. In this case, we'll reuse mangling numbers from
  /// existing lambdas.
  bool ReplacingOriginal() { return false; }

  /// Wether CXXConstructExpr can be skipped when they are implicit.
  /// They will be reconstructed when used if needed.
  /// This is useful when the user that cause rebuilding of the
  /// CXXConstructExpr is outside of the expression at which the TreeTransform
  /// started.
  bool AllowSkippingCXXConstructExpr() { return true; }

  /// Returns the location of the entity being transformed, if that
  /// information was not available elsewhere in the AST.
  ///
  /// By default, returns no source-location information. Subclasses can
  /// provide an alternative implementation that provides better location
  /// information.
  SourceLocation getBaseLocation() { return SourceLocation(); }

  /// Returns the name of the entity being transformed, if that
  /// information was not available elsewhere in the AST.
  ///
  /// By default, returns an empty name. Subclasses can provide an alternative
  /// implementation with a more precise name.
  DeclarationName getBaseEntity() { return DeclarationName(); }

  /// Sets the "base" location and entity when that
  /// information is known based on another transformation.
  ///
  /// By default, the source location and entity are ignored. Subclasses can
  /// override this function to provide a customized implementation.
  void setBase(SourceLocation Loc, DeclarationName Entity) { }

  /// RAII object that temporarily sets the base location and entity
  /// used for reporting diagnostics in types.
  class TemporaryBase {
    TreeTransform &Self;
    SourceLocation OldLocation;
    DeclarationName OldEntity;

  public:
    TemporaryBase(TreeTransform &Self, SourceLocation Location,
                  DeclarationName Entity) : Self(Self) {
      OldLocation = Self.getDerived().getBaseLocation();
      OldEntity = Self.getDerived().getBaseEntity();

      if (Location.isValid())
        Self.getDerived().setBase(Location, Entity);
    }

    ~TemporaryBase() {
      Self.getDerived().setBase(OldLocation, OldEntity);
    }
  };

  /// Determine whether the given type \p T has already been
  /// transformed.
  ///
  /// Subclasses can provide an alternative implementation of this routine
  /// to short-circuit evaluation when it is known that a given type will
  /// not change. For example, template instantiation need not traverse
  /// non-dependent types.
  bool AlreadyTransformed(QualType T) {
    return T.isNull();
  }

  /// Transform a template parameter depth level.
  ///
  /// During a transformation that transforms template parameters, this maps
  /// an old template parameter depth to a new depth.
  unsigned TransformTemplateDepth(unsigned Depth) {
    return Depth;
  }

  /// Determine whether the given call argument should be dropped, e.g.,
  /// because it is a default argument.
  ///
  /// Subclasses can provide an alternative implementation of this routine to
  /// determine which kinds of call arguments get dropped. By default,
  /// CXXDefaultArgument nodes are dropped (prior to transformation).
  bool DropCallArgument(Expr *E) {
    return E->isDefaultArgument();
  }

  /// Determine whether we should expand a pack expansion with the
  /// given set of parameter packs into separate arguments by repeatedly
  /// transforming the pattern.
  ///
  /// By default, the transformer never tries to expand pack expansions.
  /// Subclasses can override this routine to provide different behavior.
  ///
  /// \param EllipsisLoc The location of the ellipsis that identifies the
  /// pack expansion.
  ///
  /// \param PatternRange The source range that covers the entire pattern of
  /// the pack expansion.
  ///
  /// \param Unexpanded The set of unexpanded parameter packs within the
  /// pattern.
  ///
  /// \param ShouldExpand Will be set to \c true if the transformer should
  /// expand the corresponding pack expansions into separate arguments. When
  /// set, \c NumExpansions must also be set.
  ///
  /// \param RetainExpansion Whether the caller should add an unexpanded
  /// pack expansion after all of the expanded arguments. This is used
  /// when extending explicitly-specified template argument packs per
  /// C++0x [temp.arg.explicit]p9.
  ///
  /// \param NumExpansions The number of separate arguments that will be in
  /// the expanded form of the corresponding pack expansion. This is both an
  /// input and an output parameter, which can be set by the caller if the
  /// number of expansions is known a priori (e.g., due to a prior substitution)
  /// and will be set by the callee when the number of expansions is known.
  /// The callee must set this value when \c ShouldExpand is \c true; it may
  /// set this value in other cases.
  ///
  /// \returns true if an error occurred (e.g., because the parameter packs
  /// are to be instantiated with arguments of different lengths), false
  /// otherwise. If false, \c ShouldExpand (and possibly \c NumExpansions)
  /// must be set.
  bool TryExpandParameterPacks(SourceLocation EllipsisLoc,
                               SourceRange PatternRange,
                               ArrayRef<UnexpandedParameterPack> Unexpanded,
                               bool &ShouldExpand, bool &RetainExpansion,
                               std::optional<unsigned> &NumExpansions) {
    ShouldExpand = false;
    return false;
  }

  /// "Forget" about the partially-substituted pack template argument,
  /// when performing an instantiation that must preserve the parameter pack
  /// use.
  ///
  /// This routine is meant to be overridden by the template instantiator.
  TemplateArgument ForgetPartiallySubstitutedPack() {
    return TemplateArgument();
  }

  /// "Remember" the partially-substituted pack template argument
  /// after performing an instantiation that must preserve the parameter pack
  /// use.
  ///
  /// This routine is meant to be overridden by the template instantiator.
  void RememberPartiallySubstitutedPack(TemplateArgument Arg) { }

  /// Note to the derived class when a function parameter pack is
  /// being expanded.
  void ExpandingFunctionParameterPack(ParmVarDecl *Pack) { }

  /// Transforms the given type into another type.
  ///
  /// By default, this routine transforms a type by creating a
  /// TypeSourceInfo for it and delegating to the appropriate
  /// function.  This is expensive, but we don't mind, because
  /// this method is deprecated anyway;  all users should be
  /// switched to storing TypeSourceInfos.
  ///
  /// \returns the transformed type.
  QualType TransformType(QualType T);

  /// Transforms the given type-with-location into a new
  /// type-with-location.
  ///
  /// By default, this routine transforms a type by delegating to the
  /// appropriate TransformXXXType to build a new type.  Subclasses
  /// may override this function (to take over all type
  /// transformations) or some set of the TransformXXXType functions
  /// to alter the transformation.
  TypeSourceInfo *TransformType(TypeSourceInfo *DI);

  /// Transform the given type-with-location into a new
  /// type, collecting location information in the given builder
  /// as necessary.
  ///
  QualType TransformType(TypeLocBuilder &TLB, TypeLoc TL);

  /// Transform a type that is permitted to produce a
  /// DeducedTemplateSpecializationType.
  ///
  /// This is used in the (relatively rare) contexts where it is acceptable
  /// for transformation to produce a class template type with deduced
  /// template arguments.
  /// @{
  QualType TransformTypeWithDeducedTST(QualType T);
  TypeSourceInfo *TransformTypeWithDeducedTST(TypeSourceInfo *DI);
  /// @}

  /// The reason why the value of a statement is not discarded, if any.
  enum StmtDiscardKind {
    SDK_Discarded,
    SDK_NotDiscarded,
    SDK_StmtExprResult,
  };

  /// Transform the given statement.
  ///
  /// By default, this routine transforms a statement by delegating to the
  /// appropriate TransformXXXStmt function to transform a specific kind of
  /// statement or the TransformExpr() function to transform an expression.
  /// Subclasses may override this function to transform statements using some
  /// other mechanism.
  ///
  /// \returns the transformed statement.
  StmtResult TransformStmt(Stmt *S, StmtDiscardKind SDK = SDK_Discarded);

  /// Transform the given statement.
  ///
  /// By default, this routine transforms a statement by delegating to the
  /// appropriate TransformOMPXXXClause function to transform a specific kind
  /// of clause. Subclasses may override this function to transform statements
  /// using some other mechanism.
  ///
  /// \returns the transformed OpenMP clause.
  OMPClause *TransformOMPClause(OMPClause *S);

  /// Transform the given attribute.
  ///
  /// By default, this routine transforms a statement by delegating to the
  /// appropriate TransformXXXAttr function to transform a specific kind
  /// of attribute. Subclasses may override this function to transform
  /// attributed statements/types using some other mechanism.
  ///
  /// \returns the transformed attribute
  const Attr *TransformAttr(const Attr *S);

  // Transform the given statement attribute.
  //
  // Delegates to the appropriate TransformXXXAttr function to transform a
  // specific kind of statement attribute. Unlike the non-statement taking
  // version of this, this implements all attributes, not just pragmas.
  const Attr *TransformStmtAttr(const Stmt *OrigS, const Stmt *InstS,
                                const Attr *A);

  // Transform the specified attribute.
  //
  // Subclasses should override the transformation of attributes with a pragma
  // spelling to transform expressions stored within the attribute.
  //
  // \returns the transformed attribute.
#define ATTR(X)                                                                \
  const X##Attr *Transform##X##Attr(const X##Attr *R) { return R; }
#include "clang/Basic/AttrList.inc"

  // Transform the specified attribute.
  //
  // Subclasses should override the transformation of attributes to do
  // transformation and checking of statement attributes. By default, this
  // delegates to the non-statement taking version.
  //
  // \returns the transformed attribute.
#define ATTR(X)                                                                \
  const X##Attr *TransformStmt##X##Attr(const Stmt *, const Stmt *,            \
                                        const X##Attr *A) {                    \
    return getDerived().Transform##X##Attr(A);                                 \
  }
#include "clang/Basic/AttrList.inc"

  /// Transform the given expression.
  ///
  /// By default, this routine transforms an expression by delegating to the
  /// appropriate TransformXXXExpr function to build a new expression.
  /// Subclasses may override this function to transform expressions using some
  /// other mechanism.
  ///
  /// \returns the transformed expression.
  ExprResult TransformExpr(Expr *E);

  /// Transform the given initializer.
  ///
  /// By default, this routine transforms an initializer by stripping off the
  /// semantic nodes added by initialization, then passing the result to
  /// TransformExpr or TransformExprs.
  ///
  /// \returns the transformed initializer.
  ExprResult TransformInitializer(Expr *Init, bool NotCopyInit);

  /// Transform the given list of expressions.
  ///
  /// This routine transforms a list of expressions by invoking
  /// \c TransformExpr() for each subexpression. However, it also provides
  /// support for variadic templates by expanding any pack expansions (if the
  /// derived class permits such expansion) along the way. When pack expansions
  /// are present, the number of outputs may not equal the number of inputs.
  ///
  /// \param Inputs The set of expressions to be transformed.
  ///
  /// \param NumInputs The number of expressions in \c Inputs.
  ///
  /// \param IsCall If \c true, then this transform is being performed on
  /// function-call arguments, and any arguments that should be dropped, will
  /// be.
  ///
  /// \param Outputs The transformed input expressions will be added to this
  /// vector.
  ///
  /// \param ArgChanged If non-NULL, will be set \c true if any argument changed
  /// due to transformation.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool TransformExprs(Expr *const *Inputs, unsigned NumInputs, bool IsCall,
                      SmallVectorImpl<Expr *> &Outputs,
                      bool *ArgChanged = nullptr);

  /// Transform the given declaration, which is referenced from a type
  /// or expression.
  ///
  /// By default, acts as the identity function on declarations, unless the
  /// transformer has had to transform the declaration itself. Subclasses
  /// may override this function to provide alternate behavior.
  Decl *TransformDecl(SourceLocation Loc, Decl *D) {
    llvm::DenseMap<Decl *, Decl *>::iterator Known
      = TransformedLocalDecls.find(D);
    if (Known != TransformedLocalDecls.end())
      return Known->second;

    return D;
  }

  /// Transform the specified condition.
  ///
  /// By default, this transforms the variable and expression and rebuilds
  /// the condition.
  Sema::ConditionResult TransformCondition(SourceLocation Loc, VarDecl *Var,
                                           Expr *Expr,
                                           Sema::ConditionKind Kind);

  /// Transform the attributes associated with the given declaration and
  /// place them on the new declaration.
  ///
  /// By default, this operation does nothing. Subclasses may override this
  /// behavior to transform attributes.
  void transformAttrs(Decl *Old, Decl *New) { }

  /// Note that a local declaration has been transformed by this
  /// transformer.
  ///
  /// Local declarations are typically transformed via a call to
  /// TransformDefinition. However, in some cases (e.g., lambda expressions),
  /// the transformer itself has to transform the declarations. This routine
  /// can be overridden by a subclass that keeps track of such mappings.
  void transformedLocalDecl(Decl *Old, ArrayRef<Decl *> New) {
    assert(New.size() == 1 &&
           "must override transformedLocalDecl if performing pack expansion");
    TransformedLocalDecls[Old] = New.front();
  }

  /// Transform the definition of the given declaration.
  ///
  /// By default, invokes TransformDecl() to transform the declaration.
  /// Subclasses may override this function to provide alternate behavior.
  Decl *TransformDefinition(SourceLocation Loc, Decl *D) {
    return getDerived().TransformDecl(Loc, D);
  }

  /// Transform the given declaration, which was the first part of a
  /// nested-name-specifier in a member access expression.
  ///
  /// This specific declaration transformation only applies to the first
  /// identifier in a nested-name-specifier of a member access expression, e.g.,
  /// the \c T in \c x->T::member
  ///
  /// By default, invokes TransformDecl() to transform the declaration.
  /// Subclasses may override this function to provide alternate behavior.
  NamedDecl *TransformFirstQualifierInScope(NamedDecl *D, SourceLocation Loc) {
    return cast_or_null<NamedDecl>(getDerived().TransformDecl(Loc, D));
  }

  /// Transform the set of declarations in an OverloadExpr.
  bool TransformOverloadExprDecls(OverloadExpr *Old, bool RequiresADL,
                                  LookupResult &R);

  /// Transform the given nested-name-specifier with source-location
  /// information.
  ///
  /// By default, transforms all of the types and declarations within the
  /// nested-name-specifier. Subclasses may override this function to provide
  /// alternate behavior.
  NestedNameSpecifierLoc
  TransformNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS,
                                  QualType ObjectType = QualType(),
                                  NamedDecl *FirstQualifierInScope = nullptr);

  /// Transform the given declaration name.
  ///
  /// By default, transforms the types of conversion function, constructor,
  /// and destructor names and then (if needed) rebuilds the declaration name.
  /// Identifiers and selectors are returned unmodified. Subclasses may
  /// override this function to provide alternate behavior.
  DeclarationNameInfo
  TransformDeclarationNameInfo(const DeclarationNameInfo &NameInfo);

  bool TransformRequiresExprRequirements(
      ArrayRef<concepts::Requirement *> Reqs,
      llvm::SmallVectorImpl<concepts::Requirement *> &Transformed);
  concepts::TypeRequirement *
  TransformTypeRequirement(concepts::TypeRequirement *Req);
  concepts::ExprRequirement *
  TransformExprRequirement(concepts::ExprRequirement *Req);
  concepts::NestedRequirement *
  TransformNestedRequirement(concepts::NestedRequirement *Req);

  /// Transform the given template name.
  ///
  /// \param SS The nested-name-specifier that qualifies the template
  /// name. This nested-name-specifier must already have been transformed.
  ///
  /// \param Name The template name to transform.
  ///
  /// \param NameLoc The source location of the template name.
  ///
  /// \param ObjectType If we're translating a template name within a member
  /// access expression, this is the type of the object whose member template
  /// is being referenced.
  ///
  /// \param FirstQualifierInScope If the first part of a nested-name-specifier
  /// also refers to a name within the current (lexical) scope, this is the
  /// declaration it refers to.
  ///
  /// By default, transforms the template name by transforming the declarations
  /// and nested-name-specifiers that occur within the template name.
  /// Subclasses may override this function to provide alternate behavior.
  TemplateName
  TransformTemplateName(CXXScopeSpec &SS, TemplateName Name,
                        SourceLocation NameLoc,
                        QualType ObjectType = QualType(),
                        NamedDecl *FirstQualifierInScope = nullptr,
                        bool AllowInjectedClassName = false);

  /// Transform the given template argument.
  ///
  /// By default, this operation transforms the type, expression, or
  /// declaration stored within the template argument and constructs a
  /// new template argument from the transformed result. Subclasses may
  /// override this function to provide alternate behavior.
  ///
  /// Returns true if there was an error.
  bool TransformTemplateArgument(const TemplateArgumentLoc &Input,
                                 TemplateArgumentLoc &Output,
                                 bool Uneval = false);

  /// Transform the given set of template arguments.
  ///
  /// By default, this operation transforms all of the template arguments
  /// in the input set using \c TransformTemplateArgument(), and appends
  /// the transformed arguments to the output list.
  ///
  /// Note that this overload of \c TransformTemplateArguments() is merely
  /// a convenience function. Subclasses that wish to override this behavior
  /// should override the iterator-based member template version.
  ///
  /// \param Inputs The set of template arguments to be transformed.
  ///
  /// \param NumInputs The number of template arguments in \p Inputs.
  ///
  /// \param Outputs The set of transformed template arguments output by this
  /// routine.
  ///
  /// Returns true if an error occurred.
  bool TransformTemplateArguments(const TemplateArgumentLoc *Inputs,
                                  unsigned NumInputs,
                                  TemplateArgumentListInfo &Outputs,
                                  bool Uneval = false) {
    return TransformTemplateArguments(Inputs, Inputs + NumInputs, Outputs,
                                      Uneval);
  }

  /// Transform the given set of template arguments.
  ///
  /// By default, this operation transforms all of the template arguments
  /// in the input set using \c TransformTemplateArgument(), and appends
  /// the transformed arguments to the output list.
  ///
  /// \param First An iterator to the first template argument.
  ///
  /// \param Last An iterator one step past the last template argument.
  ///
  /// \param Outputs The set of transformed template arguments output by this
  /// routine.
  ///
  /// Returns true if an error occurred.
  template<typename InputIterator>
  bool TransformTemplateArguments(InputIterator First,
                                  InputIterator Last,
                                  TemplateArgumentListInfo &Outputs,
                                  bool Uneval = false);

  /// Fakes up a TemplateArgumentLoc for a given TemplateArgument.
  void InventTemplateArgumentLoc(const TemplateArgument &Arg,
                                 TemplateArgumentLoc &ArgLoc);

  /// Fakes up a TypeSourceInfo for a type.
  TypeSourceInfo *InventTypeSourceInfo(QualType T) {
    return SemaRef.Context.getTrivialTypeSourceInfo(T,
                       getDerived().getBaseLocation());
  }

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                   \
  QualType Transform##CLASS##Type(TypeLocBuilder &TLB, CLASS##TypeLoc T);
#include "clang/AST/TypeLocNodes.def"

  QualType TransformTemplateTypeParmType(TypeLocBuilder &TLB,
                                         TemplateTypeParmTypeLoc TL,
                                         bool SuppressObjCLifetime);
  QualType
  TransformSubstTemplateTypeParmPackType(TypeLocBuilder &TLB,
                                         SubstTemplateTypeParmPackTypeLoc TL,
                                         bool SuppressObjCLifetime);

  template<typename Fn>
  QualType TransformFunctionProtoType(TypeLocBuilder &TLB,
                                      FunctionProtoTypeLoc TL,
                                      CXXRecordDecl *ThisContext,
                                      Qualifiers ThisTypeQuals,
                                      Fn TransformExceptionSpec);

  template <typename Fn>
  QualType TransformAttributedType(TypeLocBuilder &TLB, AttributedTypeLoc TL,
                                   Fn TransformModifiedType);

  bool TransformExceptionSpec(SourceLocation Loc,
                              FunctionProtoType::ExceptionSpecInfo &ESI,
                              SmallVectorImpl<QualType> &Exceptions,
                              bool &Changed);

  StmtResult TransformSEHHandler(Stmt *Handler);

  QualType
  TransformTemplateSpecializationType(TypeLocBuilder &TLB,
                                      TemplateSpecializationTypeLoc TL,
                                      TemplateName Template);

  QualType
  TransformDependentTemplateSpecializationType(TypeLocBuilder &TLB,
                                      DependentTemplateSpecializationTypeLoc TL,
                                               TemplateName Template,
                                               CXXScopeSpec &SS);

  QualType TransformDependentTemplateSpecializationType(
      TypeLocBuilder &TLB, DependentTemplateSpecializationTypeLoc TL,
      NestedNameSpecifierLoc QualifierLoc);

  /// Transforms the parameters of a function type into the
  /// given vectors.
  ///
  /// The result vectors should be kept in sync; null entries in the
  /// variables vector are acceptable.
  ///
  /// LastParamTransformed, if non-null, will be set to the index of the last
  /// parameter on which transfromation was started. In the event of an error,
  /// this will contain the parameter which failed to instantiate.
  ///
  /// Return true on error.
  bool TransformFunctionTypeParams(
      SourceLocation Loc, ArrayRef<ParmVarDecl *> Params,
      const QualType *ParamTypes,
      const FunctionProtoType::ExtParameterInfo *ParamInfos,
      SmallVectorImpl<QualType> &PTypes, SmallVectorImpl<ParmVarDecl *> *PVars,
      Sema::ExtParameterInfoBuilder &PInfos, unsigned *LastParamTransformed);

  bool TransformFunctionTypeParams(
      SourceLocation Loc, ArrayRef<ParmVarDecl *> Params,
      const QualType *ParamTypes,
      const FunctionProtoType::ExtParameterInfo *ParamInfos,
      SmallVectorImpl<QualType> &PTypes, SmallVectorImpl<ParmVarDecl *> *PVars,
      Sema::ExtParameterInfoBuilder &PInfos) {
    return getDerived().TransformFunctionTypeParams(
        Loc, Params, ParamTypes, ParamInfos, PTypes, PVars, PInfos, nullptr);
  }

  /// Transforms the parameters of a requires expresison into the given vectors.
  ///
  /// The result vectors should be kept in sync; null entries in the
  /// variables vector are acceptable.
  ///
  /// Returns an unset ExprResult on success.  Returns an ExprResult the 'not
  /// satisfied' RequiresExpr if subsitution failed, OR an ExprError, both of
  /// which are cases where transformation shouldn't continue.
  ExprResult TransformRequiresTypeParams(
      SourceLocation KWLoc, SourceLocation RBraceLoc, const RequiresExpr *RE,
      RequiresExprBodyDecl *Body, ArrayRef<ParmVarDecl *> Params,
      SmallVectorImpl<QualType> &PTypes,
      SmallVectorImpl<ParmVarDecl *> &TransParams,
      Sema::ExtParameterInfoBuilder &PInfos) {
    if (getDerived().TransformFunctionTypeParams(
            KWLoc, Params, /*ParamTypes=*/nullptr,
            /*ParamInfos=*/nullptr, PTypes, &TransParams, PInfos))
      return ExprError();

    return ExprResult{};
  }

  /// Transforms a single function-type parameter.  Return null
  /// on error.
  ///
  /// \param indexAdjustment - A number to add to the parameter's
  ///   scope index;  can be negative
  ParmVarDecl *TransformFunctionTypeParam(ParmVarDecl *OldParm,
                                          int indexAdjustment,
                                          std::optional<unsigned> NumExpansions,
                                          bool ExpectParameterPack);

  /// Transform the body of a lambda-expression.
  StmtResult TransformLambdaBody(LambdaExpr *E, Stmt *Body);
  /// Alternative implementation of TransformLambdaBody that skips transforming
  /// the body.
  StmtResult SkipLambdaBody(LambdaExpr *E, Stmt *Body);

  CXXRecordDecl::LambdaDependencyKind
  ComputeLambdaDependency(LambdaScopeInfo *LSI) {
    return static_cast<CXXRecordDecl::LambdaDependencyKind>(
        LSI->Lambda->getLambdaDependencyKind());
  }

  QualType TransformReferenceType(TypeLocBuilder &TLB, ReferenceTypeLoc TL);

  StmtResult TransformCompoundStmt(CompoundStmt *S, bool IsStmtExpr);
  ExprResult TransformCXXNamedCastExpr(CXXNamedCastExpr *E);

  TemplateParameterList *TransformTemplateParameterList(
        TemplateParameterList *TPL) {
    return TPL;
  }

  ExprResult TransformAddressOfOperand(Expr *E);

  ExprResult TransformDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E,
                                                bool IsAddressOfOperand,
                                                TypeSourceInfo **RecoveryTSI);

  ExprResult TransformParenDependentScopeDeclRefExpr(
      ParenExpr *PE, DependentScopeDeclRefExpr *DRE, bool IsAddressOfOperand,
      TypeSourceInfo **RecoveryTSI);

  ExprResult TransformUnresolvedLookupExpr(UnresolvedLookupExpr *E,
                                           bool IsAddressOfOperand);

  StmtResult TransformOMPExecutableDirective(OMPExecutableDirective *S);

// FIXME: We use LLVM_ATTRIBUTE_NOINLINE because inlining causes a ridiculous
// amount of stack usage with clang.
#define STMT(Node, Parent)                        \
  LLVM_ATTRIBUTE_NOINLINE \
  StmtResult Transform##Node(Node *S);
#define VALUESTMT(Node, Parent)                   \
  LLVM_ATTRIBUTE_NOINLINE \
  StmtResult Transform##Node(Node *S, StmtDiscardKind SDK);
#define EXPR(Node, Parent)                        \
  LLVM_ATTRIBUTE_NOINLINE \
  ExprResult Transform##Node(Node *E);
#define ABSTRACT_STMT(Stmt)
#include "clang/AST/StmtNodes.inc"

#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  LLVM_ATTRIBUTE_NOINLINE                                                      \
  OMPClause *Transform##Class(Class *S);
#include "llvm/Frontend/OpenMP/OMP.inc"

  /// Build a new qualified type given its unqualified type and type location.
  ///
  /// By default, this routine adds type qualifiers only to types that can
  /// have qualifiers, and silently suppresses those qualifiers that are not
  /// permitted. Subclasses may override this routine to provide different
  /// behavior.
  QualType RebuildQualifiedType(QualType T, QualifiedTypeLoc TL);

  /// Build a new pointer type given its pointee type.
  ///
  /// By default, performs semantic analysis when building the pointer type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildPointerType(QualType PointeeType, SourceLocation Sigil);

  /// Build a new block pointer type given its pointee type.
  ///
  /// By default, performs semantic analysis when building the block pointer
  /// type. Subclasses may override this routine to provide different behavior.
  QualType RebuildBlockPointerType(QualType PointeeType, SourceLocation Sigil);

  /// Build a new reference type given the type it references.
  ///
  /// By default, performs semantic analysis when building the
  /// reference type. Subclasses may override this routine to provide
  /// different behavior.
  ///
  /// \param LValue whether the type was written with an lvalue sigil
  /// or an rvalue sigil.
  QualType RebuildReferenceType(QualType ReferentType,
                                bool LValue,
                                SourceLocation Sigil);

  /// Build a new member pointer type given the pointee type and the
  /// class type it refers into.
  ///
  /// By default, performs semantic analysis when building the member pointer
  /// type. Subclasses may override this routine to provide different behavior.
  QualType RebuildMemberPointerType(QualType PointeeType, QualType ClassType,
                                    SourceLocation Sigil);

  QualType RebuildObjCTypeParamType(const ObjCTypeParamDecl *Decl,
                                    SourceLocation ProtocolLAngleLoc,
                                    ArrayRef<ObjCProtocolDecl *> Protocols,
                                    ArrayRef<SourceLocation> ProtocolLocs,
                                    SourceLocation ProtocolRAngleLoc);

  /// Build an Objective-C object type.
  ///
  /// By default, performs semantic analysis when building the object type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildObjCObjectType(QualType BaseType,
                                 SourceLocation Loc,
                                 SourceLocation TypeArgsLAngleLoc,
                                 ArrayRef<TypeSourceInfo *> TypeArgs,
                                 SourceLocation TypeArgsRAngleLoc,
                                 SourceLocation ProtocolLAngleLoc,
                                 ArrayRef<ObjCProtocolDecl *> Protocols,
                                 ArrayRef<SourceLocation> ProtocolLocs,
                                 SourceLocation ProtocolRAngleLoc);

  /// Build a new Objective-C object pointer type given the pointee type.
  ///
  /// By default, directly builds the pointer type, with no additional semantic
  /// analysis.
  QualType RebuildObjCObjectPointerType(QualType PointeeType,
                                        SourceLocation Star);

  /// Build a new array type given the element type, size
  /// modifier, size of the array (if known), size expression, and index type
  /// qualifiers.
  ///
  /// By default, performs semantic analysis when building the array type.
  /// Subclasses may override this routine to provide different behavior.
  /// Also by default, all of the other Rebuild*Array
  QualType RebuildArrayType(QualType ElementType, ArraySizeModifier SizeMod,
                            const llvm::APInt *Size, Expr *SizeExpr,
                            unsigned IndexTypeQuals, SourceRange BracketsRange);

  /// Build a new constant array type given the element type, size
  /// modifier, (known) size of the array, and index type qualifiers.
  ///
  /// By default, performs semantic analysis when building the array type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildConstantArrayType(QualType ElementType,
                                    ArraySizeModifier SizeMod,
                                    const llvm::APInt &Size, Expr *SizeExpr,
                                    unsigned IndexTypeQuals,
                                    SourceRange BracketsRange);

  /// Build a new incomplete array type given the element type, size
  /// modifier, and index type qualifiers.
  ///
  /// By default, performs semantic analysis when building the array type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildIncompleteArrayType(QualType ElementType,
                                      ArraySizeModifier SizeMod,
                                      unsigned IndexTypeQuals,
                                      SourceRange BracketsRange);

  /// Build a new variable-length array type given the element type,
  /// size modifier, size expression, and index type qualifiers.
  ///
  /// By default, performs semantic analysis when building the array type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildVariableArrayType(QualType ElementType,
                                    ArraySizeModifier SizeMod, Expr *SizeExpr,
                                    unsigned IndexTypeQuals,
                                    SourceRange BracketsRange);

  /// Build a new dependent-sized array type given the element type,
  /// size modifier, size expression, and index type qualifiers.
  ///
  /// By default, performs semantic analysis when building the array type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildDependentSizedArrayType(QualType ElementType,
                                          ArraySizeModifier SizeMod,
                                          Expr *SizeExpr,
                                          unsigned IndexTypeQuals,
                                          SourceRange BracketsRange);

  /// Build a new vector type given the element type and
  /// number of elements.
  ///
  /// By default, performs semantic analysis when building the vector type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildVectorType(QualType ElementType, unsigned NumElements,
                             VectorKind VecKind);

  /// Build a new potentially dependently-sized extended vector type
  /// given the element type and number of elements.
  ///
  /// By default, performs semantic analysis when building the vector type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildDependentVectorType(QualType ElementType, Expr *SizeExpr,
                                      SourceLocation AttributeLoc, VectorKind);

  /// Build a new extended vector type given the element type and
  /// number of elements.
  ///
  /// By default, performs semantic analysis when building the vector type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildExtVectorType(QualType ElementType, unsigned NumElements,
                                SourceLocation AttributeLoc);

  /// Build a new potentially dependently-sized extended vector type
  /// given the element type and number of elements.
  ///
  /// By default, performs semantic analysis when building the vector type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildDependentSizedExtVectorType(QualType ElementType,
                                              Expr *SizeExpr,
                                              SourceLocation AttributeLoc);

  /// Build a new matrix type given the element type and dimensions.
  QualType RebuildConstantMatrixType(QualType ElementType, unsigned NumRows,
                                     unsigned NumColumns);

  /// Build a new matrix type given the type and dependently-defined
  /// dimensions.
  QualType RebuildDependentSizedMatrixType(QualType ElementType, Expr *RowExpr,
                                           Expr *ColumnExpr,
                                           SourceLocation AttributeLoc);

  /// Build a new DependentAddressSpaceType or return the pointee
  /// type variable with the correct address space (retrieved from
  /// AddrSpaceExpr) applied to it. The former will be returned in cases
  /// where the address space remains dependent.
  ///
  /// By default, performs semantic analysis when building the type with address
  /// space applied. Subclasses may override this routine to provide different
  /// behavior.
  QualType RebuildDependentAddressSpaceType(QualType PointeeType,
                                            Expr *AddrSpaceExpr,
                                            SourceLocation AttributeLoc);

  /// Build a new function type.
  ///
  /// By default, performs semantic analysis when building the function type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildFunctionProtoType(QualType T,
                                    MutableArrayRef<QualType> ParamTypes,
                                    const FunctionProtoType::ExtProtoInfo &EPI);

  /// Build a new unprototyped function type.
  QualType RebuildFunctionNoProtoType(QualType ResultType);

  /// Rebuild an unresolved typename type, given the decl that
  /// the UnresolvedUsingTypenameDecl was transformed to.
  QualType RebuildUnresolvedUsingType(SourceLocation NameLoc, Decl *D);

  /// Build a new type found via an alias.
  QualType RebuildUsingType(UsingShadowDecl *Found, QualType Underlying) {
    return SemaRef.Context.getUsingType(Found, Underlying);
  }

  /// Build a new typedef type.
  QualType RebuildTypedefType(TypedefNameDecl *Typedef) {
    return SemaRef.Context.getTypeDeclType(Typedef);
  }

  /// Build a new MacroDefined type.
  QualType RebuildMacroQualifiedType(QualType T,
                                     const IdentifierInfo *MacroII) {
    return SemaRef.Context.getMacroQualifiedType(T, MacroII);
  }

  /// Build a new class/struct/union type.
  QualType RebuildRecordType(RecordDecl *Record) {
    return SemaRef.Context.getTypeDeclType(Record);
  }

  /// Build a new Enum type.
  QualType RebuildEnumType(EnumDecl *Enum) {
    return SemaRef.Context.getTypeDeclType(Enum);
  }

  /// Build a new typeof(expr) type.
  ///
  /// By default, performs semantic analysis when building the typeof type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildTypeOfExprType(Expr *Underlying, SourceLocation Loc,
                                 TypeOfKind Kind);

  /// Build a new typeof(type) type.
  ///
  /// By default, builds a new TypeOfType with the given underlying type.
  QualType RebuildTypeOfType(QualType Underlying, TypeOfKind Kind);

  /// Build a new unary transform type.
  QualType RebuildUnaryTransformType(QualType BaseType,
                                     UnaryTransformType::UTTKind UKind,
                                     SourceLocation Loc);

  /// Build a new C++11 decltype type.
  ///
  /// By default, performs semantic analysis when building the decltype type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildDecltypeType(Expr *Underlying, SourceLocation Loc);

  QualType RebuildPackIndexingType(QualType Pattern, Expr *IndexExpr,
                                   SourceLocation Loc,
                                   SourceLocation EllipsisLoc,
                                   bool FullySubstituted,
                                   ArrayRef<QualType> Expansions = {});

  /// Build a new C++11 auto type.
  ///
  /// By default, builds a new AutoType with the given deduced type.
  QualType RebuildAutoType(QualType Deduced, AutoTypeKeyword Keyword,
                           ConceptDecl *TypeConstraintConcept,
                           ArrayRef<TemplateArgument> TypeConstraintArgs) {
    // Note, IsDependent is always false here: we implicitly convert an 'auto'
    // which has been deduced to a dependent type into an undeduced 'auto', so
    // that we'll retry deduction after the transformation.
    return SemaRef.Context.getAutoType(Deduced, Keyword,
                                       /*IsDependent*/ false, /*IsPack=*/false,
                                       TypeConstraintConcept,
                                       TypeConstraintArgs);
  }

  /// By default, builds a new DeducedTemplateSpecializationType with the given
  /// deduced type.
  QualType RebuildDeducedTemplateSpecializationType(TemplateName Template,
      QualType Deduced) {
    return SemaRef.Context.getDeducedTemplateSpecializationType(
        Template, Deduced, /*IsDependent*/ false);
  }

  /// Build a new template specialization type.
  ///
  /// By default, performs semantic analysis when building the template
  /// specialization type. Subclasses may override this routine to provide
  /// different behavior.
  QualType RebuildTemplateSpecializationType(TemplateName Template,
                                             SourceLocation TemplateLoc,
                                             TemplateArgumentListInfo &Args);

  /// Build a new parenthesized type.
  ///
  /// By default, builds a new ParenType type from the inner type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildParenType(QualType InnerType) {
    return SemaRef.BuildParenType(InnerType);
  }

  /// Build a new qualified name type.
  ///
  /// By default, builds a new ElaboratedType type from the keyword,
  /// the nested-name-specifier and the named type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildElaboratedType(SourceLocation KeywordLoc,
                                 ElaboratedTypeKeyword Keyword,
                                 NestedNameSpecifierLoc QualifierLoc,
                                 QualType Named) {
    return SemaRef.Context.getElaboratedType(Keyword,
                                         QualifierLoc.getNestedNameSpecifier(),
                                             Named);
  }

  /// Build a new typename type that refers to a template-id.
  ///
  /// By default, builds a new DependentNameType type from the
  /// nested-name-specifier and the given type. Subclasses may override
  /// this routine to provide different behavior.
  QualType RebuildDependentTemplateSpecializationType(
                                          ElaboratedTypeKeyword Keyword,
                                          NestedNameSpecifierLoc QualifierLoc,
                                          SourceLocation TemplateKWLoc,
                                          const IdentifierInfo *Name,
                                          SourceLocation NameLoc,
                                          TemplateArgumentListInfo &Args,
                                          bool AllowInjectedClassName) {
    // Rebuild the template name.
    // TODO: avoid TemplateName abstraction
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);
    TemplateName InstName = getDerived().RebuildTemplateName(
        SS, TemplateKWLoc, *Name, NameLoc, QualType(), nullptr,
        AllowInjectedClassName);

    if (InstName.isNull())
      return QualType();

    // If it's still dependent, make a dependent specialization.
    if (InstName.getAsDependentTemplateName())
      return SemaRef.Context.getDependentTemplateSpecializationType(
          Keyword, QualifierLoc.getNestedNameSpecifier(), Name,
          Args.arguments());

    // Otherwise, make an elaborated type wrapping a non-dependent
    // specialization.
    QualType T =
        getDerived().RebuildTemplateSpecializationType(InstName, NameLoc, Args);
    if (T.isNull())
      return QualType();
    return SemaRef.Context.getElaboratedType(
        Keyword, QualifierLoc.getNestedNameSpecifier(), T);
  }

  /// Build a new typename type that refers to an identifier.
  ///
  /// By default, performs semantic analysis when building the typename type
  /// (or elaborated type). Subclasses may override this routine to provide
  /// different behavior.
  QualType RebuildDependentNameType(ElaboratedTypeKeyword Keyword,
                                    SourceLocation KeywordLoc,
                                    NestedNameSpecifierLoc QualifierLoc,
                                    const IdentifierInfo *Id,
                                    SourceLocation IdLoc,
                                    bool DeducedTSTContext) {
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);

    if (QualifierLoc.getNestedNameSpecifier()->isDependent()) {
      // If the name is still dependent, just build a new dependent name type.
      if (!SemaRef.computeDeclContext(SS))
        return SemaRef.Context.getDependentNameType(Keyword,
                                          QualifierLoc.getNestedNameSpecifier(),
                                                    Id);
    }

    if (Keyword == ElaboratedTypeKeyword::None ||
        Keyword == ElaboratedTypeKeyword::Typename) {
      return SemaRef.CheckTypenameType(Keyword, KeywordLoc, QualifierLoc,
                                       *Id, IdLoc, DeducedTSTContext);
    }

    TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForKeyword(Keyword);

    // We had a dependent elaborated-type-specifier that has been transformed
    // into a non-dependent elaborated-type-specifier. Find the tag we're
    // referring to.
    LookupResult Result(SemaRef, Id, IdLoc, Sema::LookupTagName);
    DeclContext *DC = SemaRef.computeDeclContext(SS, false);
    if (!DC)
      return QualType();

    if (SemaRef.RequireCompleteDeclContext(SS, DC))
      return QualType();

    TagDecl *Tag = nullptr;
    SemaRef.LookupQualifiedName(Result, DC);
    switch (Result.getResultKind()) {
      case LookupResult::NotFound:
      case LookupResult::NotFoundInCurrentInstantiation:
        break;

      case LookupResult::Found:
        Tag = Result.getAsSingle<TagDecl>();
        break;

      case LookupResult::FoundOverloaded:
      case LookupResult::FoundUnresolvedValue:
        llvm_unreachable("Tag lookup cannot find non-tags");

      case LookupResult::Ambiguous:
        // Let the LookupResult structure handle ambiguities.
        return QualType();
    }

    if (!Tag) {
      // Check where the name exists but isn't a tag type and use that to emit
      // better diagnostics.
      LookupResult Result(SemaRef, Id, IdLoc, Sema::LookupTagName);
      SemaRef.LookupQualifiedName(Result, DC);
      switch (Result.getResultKind()) {
        case LookupResult::Found:
        case LookupResult::FoundOverloaded:
        case LookupResult::FoundUnresolvedValue: {
          NamedDecl *SomeDecl = Result.getRepresentativeDecl();
          Sema::NonTagKind NTK = SemaRef.getNonTagTypeDeclKind(SomeDecl, Kind);
          SemaRef.Diag(IdLoc, diag::err_tag_reference_non_tag)
              << SomeDecl << NTK << llvm::to_underlying(Kind);
          SemaRef.Diag(SomeDecl->getLocation(), diag::note_declared_at);
          break;
        }
        default:
          SemaRef.Diag(IdLoc, diag::err_not_tag_in_scope)
              << llvm::to_underlying(Kind) << Id << DC
              << QualifierLoc.getSourceRange();
          break;
      }
      return QualType();
    }

    if (!SemaRef.isAcceptableTagRedeclaration(Tag, Kind, /*isDefinition*/false,
                                              IdLoc, Id)) {
      SemaRef.Diag(KeywordLoc, diag::err_use_with_wrong_tag) << Id;
      SemaRef.Diag(Tag->getLocation(), diag::note_previous_use);
      return QualType();
    }

    // Build the elaborated-type-specifier type.
    QualType T = SemaRef.Context.getTypeDeclType(Tag);
    return SemaRef.Context.getElaboratedType(Keyword,
                                         QualifierLoc.getNestedNameSpecifier(),
                                             T);
  }

  /// Build a new pack expansion type.
  ///
  /// By default, builds a new PackExpansionType type from the given pattern.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildPackExpansionType(QualType Pattern, SourceRange PatternRange,
                                    SourceLocation EllipsisLoc,
                                    std::optional<unsigned> NumExpansions) {
    return getSema().CheckPackExpansion(Pattern, PatternRange, EllipsisLoc,
                                        NumExpansions);
  }

  /// Build a new atomic type given its value type.
  ///
  /// By default, performs semantic analysis when building the atomic type.
  /// Subclasses may override this routine to provide different behavior.
  QualType RebuildAtomicType(QualType ValueType, SourceLocation KWLoc);

  /// Build a new pipe type given its value type.
  QualType RebuildPipeType(QualType ValueType, SourceLocation KWLoc,
                           bool isReadPipe);

  /// Build a bit-precise int given its value type.
  QualType RebuildBitIntType(bool IsUnsigned, unsigned NumBits,
                             SourceLocation Loc);

  /// Build a dependent bit-precise int given its value type.
  QualType RebuildDependentBitIntType(bool IsUnsigned, Expr *NumBitsExpr,
                                      SourceLocation Loc);

  /// Build a new template name given a nested name specifier, a flag
  /// indicating whether the "template" keyword was provided, and the template
  /// that the template name refers to.
  ///
  /// By default, builds the new template name directly. Subclasses may override
  /// this routine to provide different behavior.
  TemplateName RebuildTemplateName(CXXScopeSpec &SS,
                                   bool TemplateKW,
                                   TemplateDecl *Template);

  /// Build a new template name given a nested name specifier and the
  /// name that is referred to as a template.
  ///
  /// By default, performs semantic analysis to determine whether the name can
  /// be resolved to a specific template, then builds the appropriate kind of
  /// template name. Subclasses may override this routine to provide different
  /// behavior.
  TemplateName RebuildTemplateName(CXXScopeSpec &SS,
                                   SourceLocation TemplateKWLoc,
                                   const IdentifierInfo &Name,
                                   SourceLocation NameLoc, QualType ObjectType,
                                   NamedDecl *FirstQualifierInScope,
                                   bool AllowInjectedClassName);

  /// Build a new template name given a nested name specifier and the
  /// overloaded operator name that is referred to as a template.
  ///
  /// By default, performs semantic analysis to determine whether the name can
  /// be resolved to a specific template, then builds the appropriate kind of
  /// template name. Subclasses may override this routine to provide different
  /// behavior.
  TemplateName RebuildTemplateName(CXXScopeSpec &SS,
                                   SourceLocation TemplateKWLoc,
                                   OverloadedOperatorKind Operator,
                                   SourceLocation NameLoc, QualType ObjectType,
                                   bool AllowInjectedClassName);

  /// Build a new template name given a template template parameter pack
  /// and the
  ///
  /// By default, performs semantic analysis to determine whether the name can
  /// be resolved to a specific template, then builds the appropriate kind of
  /// template name. Subclasses may override this routine to provide different
  /// behavior.
  TemplateName RebuildTemplateName(const TemplateArgument &ArgPack,
                                   Decl *AssociatedDecl, unsigned Index,
                                   bool Final) {
    return getSema().Context.getSubstTemplateTemplateParmPack(
        ArgPack, AssociatedDecl, Index, Final);
  }

  /// Build a new compound statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCompoundStmt(SourceLocation LBraceLoc,
                                       MultiStmtArg Statements,
                                       SourceLocation RBraceLoc,
                                       bool IsStmtExpr) {
    return getSema().ActOnCompoundStmt(LBraceLoc, RBraceLoc, Statements,
                                       IsStmtExpr);
  }

  /// Build a new case statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCaseStmt(SourceLocation CaseLoc,
                                   Expr *LHS,
                                   SourceLocation EllipsisLoc,
                                   Expr *RHS,
                                   SourceLocation ColonLoc) {
    return getSema().ActOnCaseStmt(CaseLoc, LHS, EllipsisLoc, RHS,
                                   ColonLoc);
  }

  /// Attach the body to a new case statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCaseStmtBody(Stmt *S, Stmt *Body) {
    getSema().ActOnCaseStmtBody(S, Body);
    return S;
  }

  /// Build a new default statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildDefaultStmt(SourceLocation DefaultLoc,
                                      SourceLocation ColonLoc,
                                      Stmt *SubStmt) {
    return getSema().ActOnDefaultStmt(DefaultLoc, ColonLoc, SubStmt,
                                      /*CurScope=*/nullptr);
  }

  /// Build a new label statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildLabelStmt(SourceLocation IdentLoc, LabelDecl *L,
                              SourceLocation ColonLoc, Stmt *SubStmt) {
    return SemaRef.ActOnLabelStmt(IdentLoc, L, ColonLoc, SubStmt);
  }

  /// Build a new attributed statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildAttributedStmt(SourceLocation AttrLoc,
                                   ArrayRef<const Attr *> Attrs,
                                   Stmt *SubStmt) {
    if (SemaRef.CheckRebuiltStmtAttributes(Attrs))
      return StmtError();
    return SemaRef.BuildAttributedStmt(AttrLoc, Attrs, SubStmt);
  }

  /// Build a new "if" statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildIfStmt(SourceLocation IfLoc, IfStatementKind Kind,
                           SourceLocation LParenLoc, Sema::ConditionResult Cond,
                           SourceLocation RParenLoc, Stmt *Init, Stmt *Then,
                           SourceLocation ElseLoc, Stmt *Else) {
    return getSema().ActOnIfStmt(IfLoc, Kind, LParenLoc, Init, Cond, RParenLoc,
                                 Then, ElseLoc, Else);
  }

  /// Start building a new switch statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildSwitchStmtStart(SourceLocation SwitchLoc,
                                    SourceLocation LParenLoc, Stmt *Init,
                                    Sema::ConditionResult Cond,
                                    SourceLocation RParenLoc) {
    return getSema().ActOnStartOfSwitchStmt(SwitchLoc, LParenLoc, Init, Cond,
                                            RParenLoc);
  }

  /// Attach the body to the switch statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildSwitchStmtBody(SourceLocation SwitchLoc,
                                   Stmt *Switch, Stmt *Body) {
    return getSema().ActOnFinishSwitchStmt(SwitchLoc, Switch, Body);
  }

  /// Build a new while statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildWhileStmt(SourceLocation WhileLoc, SourceLocation LParenLoc,
                              Sema::ConditionResult Cond,
                              SourceLocation RParenLoc, Stmt *Body) {
    return getSema().ActOnWhileStmt(WhileLoc, LParenLoc, Cond, RParenLoc, Body);
  }

  /// Build a new do-while statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildDoStmt(SourceLocation DoLoc, Stmt *Body,
                           SourceLocation WhileLoc, SourceLocation LParenLoc,
                           Expr *Cond, SourceLocation RParenLoc) {
    return getSema().ActOnDoStmt(DoLoc, Body, WhileLoc, LParenLoc,
                                 Cond, RParenLoc);
  }

  /// Build a new for statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                            Stmt *Init, Sema::ConditionResult Cond,
                            Sema::FullExprArg Inc, SourceLocation RParenLoc,
                            Stmt *Body) {
    return getSema().ActOnForStmt(ForLoc, LParenLoc, Init, Cond,
                                  Inc, RParenLoc, Body);
  }

  /// Build a new goto statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildGotoStmt(SourceLocation GotoLoc, SourceLocation LabelLoc,
                             LabelDecl *Label) {
    return getSema().ActOnGotoStmt(GotoLoc, LabelLoc, Label);
  }

  /// Build a new indirect goto statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildIndirectGotoStmt(SourceLocation GotoLoc,
                                     SourceLocation StarLoc,
                                     Expr *Target) {
    return getSema().ActOnIndirectGotoStmt(GotoLoc, StarLoc, Target);
  }

  /// Build a new return statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildReturnStmt(SourceLocation ReturnLoc, Expr *Result) {
    return getSema().BuildReturnStmt(ReturnLoc, Result);
  }

  /// Build a new declaration statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildDeclStmt(MutableArrayRef<Decl *> Decls,
                             SourceLocation StartLoc, SourceLocation EndLoc) {
    Sema::DeclGroupPtrTy DG = getSema().BuildDeclaratorGroup(Decls);
    return getSema().ActOnDeclStmt(DG, StartLoc, EndLoc);
  }

  /// Build a new inline asm statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildGCCAsmStmt(SourceLocation AsmLoc, bool IsSimple,
                               bool IsVolatile, unsigned NumOutputs,
                               unsigned NumInputs, IdentifierInfo **Names,
                               MultiExprArg Constraints, MultiExprArg Exprs,
                               Expr *AsmString, MultiExprArg Clobbers,
                               unsigned NumLabels,
                               SourceLocation RParenLoc) {
    return getSema().ActOnGCCAsmStmt(AsmLoc, IsSimple, IsVolatile, NumOutputs,
                                     NumInputs, Names, Constraints, Exprs,
                                     AsmString, Clobbers, NumLabels, RParenLoc);
  }

  /// Build a new MS style inline asm statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildMSAsmStmt(SourceLocation AsmLoc, SourceLocation LBraceLoc,
                              ArrayRef<Token> AsmToks,
                              StringRef AsmString,
                              unsigned NumOutputs, unsigned NumInputs,
                              ArrayRef<StringRef> Constraints,
                              ArrayRef<StringRef> Clobbers,
                              ArrayRef<Expr*> Exprs,
                              SourceLocation EndLoc) {
    return getSema().ActOnMSAsmStmt(AsmLoc, LBraceLoc, AsmToks, AsmString,
                                    NumOutputs, NumInputs,
                                    Constraints, Clobbers, Exprs, EndLoc);
  }

  /// Build a new co_return statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCoreturnStmt(SourceLocation CoreturnLoc, Expr *Result,
                                 bool IsImplicit) {
    return getSema().BuildCoreturnStmt(CoreturnLoc, Result, IsImplicit);
  }

  /// Build a new co_await expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCoawaitExpr(SourceLocation CoawaitLoc, Expr *Operand,
                                UnresolvedLookupExpr *OpCoawaitLookup,
                                bool IsImplicit) {
    // This function rebuilds a coawait-expr given its operator.
    // For an explicit coawait-expr, the rebuild involves the full set
    // of transformations performed by BuildUnresolvedCoawaitExpr(),
    // including calling await_transform().
    // For an implicit coawait-expr, we need to rebuild the "operator
    // coawait" but not await_transform(), so use BuildResolvedCoawaitExpr().
    // This mirrors how the implicit CoawaitExpr is originally created
    // in Sema::ActOnCoroutineBodyStart().
    if (IsImplicit) {
      ExprResult Suspend = getSema().BuildOperatorCoawaitCall(
          CoawaitLoc, Operand, OpCoawaitLookup);
      if (Suspend.isInvalid())
        return ExprError();
      return getSema().BuildResolvedCoawaitExpr(CoawaitLoc, Operand,
                                                Suspend.get(), true);
    }

    return getSema().BuildUnresolvedCoawaitExpr(CoawaitLoc, Operand,
                                                OpCoawaitLookup);
  }

  /// Build a new co_await expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildDependentCoawaitExpr(SourceLocation CoawaitLoc,
                                         Expr *Result,
                                         UnresolvedLookupExpr *Lookup) {
    return getSema().BuildUnresolvedCoawaitExpr(CoawaitLoc, Result, Lookup);
  }

  /// Build a new co_yield expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCoyieldExpr(SourceLocation CoyieldLoc, Expr *Result) {
    return getSema().BuildCoyieldExpr(CoyieldLoc, Result);
  }

  StmtResult RebuildCoroutineBodyStmt(CoroutineBodyStmt::CtorArgs Args) {
    return getSema().BuildCoroutineBodyStmt(Args);
  }

  /// Build a new Objective-C \@try statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCAtTryStmt(SourceLocation AtLoc,
                                        Stmt *TryBody,
                                        MultiStmtArg CatchStmts,
                                        Stmt *Finally) {
    return getSema().ObjC().ActOnObjCAtTryStmt(AtLoc, TryBody, CatchStmts,
                                               Finally);
  }

  /// Rebuild an Objective-C exception declaration.
  ///
  /// By default, performs semantic analysis to build the new declaration.
  /// Subclasses may override this routine to provide different behavior.
  VarDecl *RebuildObjCExceptionDecl(VarDecl *ExceptionDecl,
                                    TypeSourceInfo *TInfo, QualType T) {
    return getSema().ObjC().BuildObjCExceptionDecl(
        TInfo, T, ExceptionDecl->getInnerLocStart(),
        ExceptionDecl->getLocation(), ExceptionDecl->getIdentifier());
  }

  /// Build a new Objective-C \@catch statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCAtCatchStmt(SourceLocation AtLoc,
                                          SourceLocation RParenLoc,
                                          VarDecl *Var,
                                          Stmt *Body) {
    return getSema().ObjC().ActOnObjCAtCatchStmt(AtLoc, RParenLoc, Var, Body);
  }

  /// Build a new Objective-C \@finally statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCAtFinallyStmt(SourceLocation AtLoc,
                                            Stmt *Body) {
    return getSema().ObjC().ActOnObjCAtFinallyStmt(AtLoc, Body);
  }

  /// Build a new Objective-C \@throw statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCAtThrowStmt(SourceLocation AtLoc,
                                          Expr *Operand) {
    return getSema().ObjC().BuildObjCAtThrowStmt(AtLoc, Operand);
  }

  /// Build a new OpenMP Canonical loop.
  ///
  /// Ensures that the outermost loop in @p LoopStmt is wrapped by a
  /// OMPCanonicalLoop.
  StmtResult RebuildOMPCanonicalLoop(Stmt *LoopStmt) {
    return getSema().OpenMP().ActOnOpenMPCanonicalLoop(LoopStmt);
  }

  /// Build a new OpenMP executable directive.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildOMPExecutableDirective(
      OpenMPDirectiveKind Kind, DeclarationNameInfo DirName,
      OpenMPDirectiveKind CancelRegion, ArrayRef<OMPClause *> Clauses,
      Stmt *AStmt, SourceLocation StartLoc, SourceLocation EndLoc,
      OpenMPDirectiveKind PrevMappedDirective = OMPD_unknown) {

    return getSema().OpenMP().ActOnOpenMPExecutableDirective(
        Kind, DirName, CancelRegion, Clauses, AStmt, StartLoc, EndLoc,
        PrevMappedDirective);
  }

  /// Build a new OpenMP 'if' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPIfClause(OpenMPDirectiveKind NameModifier,
                                Expr *Condition, SourceLocation StartLoc,
                                SourceLocation LParenLoc,
                                SourceLocation NameModifierLoc,
                                SourceLocation ColonLoc,
                                SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPIfClause(
        NameModifier, Condition, StartLoc, LParenLoc, NameModifierLoc, ColonLoc,
        EndLoc);
  }

  /// Build a new OpenMP 'final' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPFinalClause(Expr *Condition, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPFinalClause(Condition, StartLoc,
                                                     LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'num_threads' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPNumThreadsClause(Expr *NumThreads,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPNumThreadsClause(NumThreads, StartLoc,
                                                          LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'safelen' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPSafelenClause(Expr *Len, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPSafelenClause(Len, StartLoc, LParenLoc,
                                                       EndLoc);
  }

  /// Build a new OpenMP 'simdlen' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPSimdlenClause(Expr *Len, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPSimdlenClause(Len, StartLoc, LParenLoc,
                                                       EndLoc);
  }

  OMPClause *RebuildOMPSizesClause(ArrayRef<Expr *> Sizes,
                                   SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPSizesClause(Sizes, StartLoc, LParenLoc,
                                                     EndLoc);
  }

  /// Build a new OpenMP 'full' clause.
  OMPClause *RebuildOMPFullClause(SourceLocation StartLoc,
                                  SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPFullClause(StartLoc, EndLoc);
  }

  /// Build a new OpenMP 'partial' clause.
  OMPClause *RebuildOMPPartialClause(Expr *Factor, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPPartialClause(Factor, StartLoc,
                                                       LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'allocator' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPAllocatorClause(Expr *A, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPAllocatorClause(A, StartLoc, LParenLoc,
                                                         EndLoc);
  }

  /// Build a new OpenMP 'collapse' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPCollapseClause(Expr *Num, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPCollapseClause(Num, StartLoc,
                                                        LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'default' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDefaultClause(DefaultKind Kind, SourceLocation KindKwLoc,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDefaultClause(
        Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'proc_bind' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPProcBindClause(ProcBindKind Kind,
                                      SourceLocation KindKwLoc,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPProcBindClause(
        Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'schedule' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPScheduleClause(
      OpenMPScheduleClauseModifier M1, OpenMPScheduleClauseModifier M2,
      OpenMPScheduleClauseKind Kind, Expr *ChunkSize, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation M1Loc, SourceLocation M2Loc,
      SourceLocation KindLoc, SourceLocation CommaLoc, SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPScheduleClause(
        M1, M2, Kind, ChunkSize, StartLoc, LParenLoc, M1Loc, M2Loc, KindLoc,
        CommaLoc, EndLoc);
  }

  /// Build a new OpenMP 'ordered' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPOrderedClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     SourceLocation LParenLoc, Expr *Num) {
    return getSema().OpenMP().ActOnOpenMPOrderedClause(StartLoc, EndLoc,
                                                       LParenLoc, Num);
  }

  /// Build a new OpenMP 'private' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPPrivateClause(ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPPrivateClause(VarList, StartLoc,
                                                       LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'firstprivate' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPFirstprivateClause(ArrayRef<Expr *> VarList,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPFirstprivateClause(VarList, StartLoc,
                                                            LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'lastprivate' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPLastprivateClause(ArrayRef<Expr *> VarList,
                                         OpenMPLastprivateModifier LPKind,
                                         SourceLocation LPKindLoc,
                                         SourceLocation ColonLoc,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPLastprivateClause(
        VarList, LPKind, LPKindLoc, ColonLoc, StartLoc, LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'shared' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPSharedClause(ArrayRef<Expr *> VarList,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPSharedClause(VarList, StartLoc,
                                                      LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'reduction' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPReductionClause(
      ArrayRef<Expr *> VarList, OpenMPReductionClauseModifier Modifier,
      SourceLocation StartLoc, SourceLocation LParenLoc,
      SourceLocation ModifierLoc, SourceLocation ColonLoc,
      SourceLocation EndLoc, CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions) {
    return getSema().OpenMP().ActOnOpenMPReductionClause(
        VarList, Modifier, StartLoc, LParenLoc, ModifierLoc, ColonLoc, EndLoc,
        ReductionIdScopeSpec, ReductionId, UnresolvedReductions);
  }

  /// Build a new OpenMP 'task_reduction' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPTaskReductionClause(
      ArrayRef<Expr *> VarList, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc,
      CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions) {
    return getSema().OpenMP().ActOnOpenMPTaskReductionClause(
        VarList, StartLoc, LParenLoc, ColonLoc, EndLoc, ReductionIdScopeSpec,
        ReductionId, UnresolvedReductions);
  }

  /// Build a new OpenMP 'in_reduction' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *
  RebuildOMPInReductionClause(ArrayRef<Expr *> VarList, SourceLocation StartLoc,
                              SourceLocation LParenLoc, SourceLocation ColonLoc,
                              SourceLocation EndLoc,
                              CXXScopeSpec &ReductionIdScopeSpec,
                              const DeclarationNameInfo &ReductionId,
                              ArrayRef<Expr *> UnresolvedReductions) {
    return getSema().OpenMP().ActOnOpenMPInReductionClause(
        VarList, StartLoc, LParenLoc, ColonLoc, EndLoc, ReductionIdScopeSpec,
        ReductionId, UnresolvedReductions);
  }

  /// Build a new OpenMP 'linear' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPLinearClause(
      ArrayRef<Expr *> VarList, Expr *Step, SourceLocation StartLoc,
      SourceLocation LParenLoc, OpenMPLinearClauseKind Modifier,
      SourceLocation ModifierLoc, SourceLocation ColonLoc,
      SourceLocation StepModifierLoc, SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPLinearClause(
        VarList, Step, StartLoc, LParenLoc, Modifier, ModifierLoc, ColonLoc,
        StepModifierLoc, EndLoc);
  }

  /// Build a new OpenMP 'aligned' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPAlignedClause(ArrayRef<Expr *> VarList, Expr *Alignment,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation ColonLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPAlignedClause(
        VarList, Alignment, StartLoc, LParenLoc, ColonLoc, EndLoc);
  }

  /// Build a new OpenMP 'copyin' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPCopyinClause(ArrayRef<Expr *> VarList,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPCopyinClause(VarList, StartLoc,
                                                      LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'copyprivate' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPCopyprivateClause(ArrayRef<Expr *> VarList,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPCopyprivateClause(VarList, StartLoc,
                                                           LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'flush' pseudo clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPFlushClause(ArrayRef<Expr *> VarList,
                                   SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPFlushClause(VarList, StartLoc,
                                                     LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'depobj' pseudo clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDepobjClause(Expr *Depobj, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDepobjClause(Depobj, StartLoc,
                                                      LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'depend' pseudo clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDependClause(OMPDependClause::DependDataTy Data,
                                    Expr *DepModifier, ArrayRef<Expr *> VarList,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDependClause(
        Data, DepModifier, VarList, StartLoc, LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'device' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDeviceClause(OpenMPDeviceClauseModifier Modifier,
                                    Expr *Device, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation ModifierLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDeviceClause(
        Modifier, Device, StartLoc, LParenLoc, ModifierLoc, EndLoc);
  }

  /// Build a new OpenMP 'map' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPMapClause(
      Expr *IteratorModifier, ArrayRef<OpenMPMapModifierKind> MapTypeModifiers,
      ArrayRef<SourceLocation> MapTypeModifiersLoc,
      CXXScopeSpec MapperIdScopeSpec, DeclarationNameInfo MapperId,
      OpenMPMapClauseKind MapType, bool IsMapTypeImplicit,
      SourceLocation MapLoc, SourceLocation ColonLoc, ArrayRef<Expr *> VarList,
      const OMPVarListLocTy &Locs, ArrayRef<Expr *> UnresolvedMappers) {
    return getSema().OpenMP().ActOnOpenMPMapClause(
        IteratorModifier, MapTypeModifiers, MapTypeModifiersLoc,
        MapperIdScopeSpec, MapperId, MapType, IsMapTypeImplicit, MapLoc,
        ColonLoc, VarList, Locs,
        /*NoDiagnose=*/false, UnresolvedMappers);
  }

  /// Build a new OpenMP 'allocate' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPAllocateClause(Expr *Allocate, ArrayRef<Expr *> VarList,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation ColonLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPAllocateClause(
        Allocate, VarList, StartLoc, LParenLoc, ColonLoc, EndLoc);
  }

  /// Build a new OpenMP 'num_teams' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPNumTeamsClause(Expr *NumTeams, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPNumTeamsClause(NumTeams, StartLoc,
                                                        LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'thread_limit' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPThreadLimitClause(Expr *ThreadLimit,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPThreadLimitClause(
        ThreadLimit, StartLoc, LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'priority' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPPriorityClause(Expr *Priority, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPPriorityClause(Priority, StartLoc,
                                                        LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'grainsize' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPGrainsizeClause(OpenMPGrainsizeClauseModifier Modifier,
                                       Expr *Device, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation ModifierLoc,
                                       SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPGrainsizeClause(
        Modifier, Device, StartLoc, LParenLoc, ModifierLoc, EndLoc);
  }

  /// Build a new OpenMP 'num_tasks' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPNumTasksClause(OpenMPNumTasksClauseModifier Modifier,
                                      Expr *NumTasks, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation ModifierLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPNumTasksClause(
        Modifier, NumTasks, StartLoc, LParenLoc, ModifierLoc, EndLoc);
  }

  /// Build a new OpenMP 'hint' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPHintClause(Expr *Hint, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPHintClause(Hint, StartLoc, LParenLoc,
                                                    EndLoc);
  }

  /// Build a new OpenMP 'detach' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDetachClause(Expr *Evt, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDetachClause(Evt, StartLoc, LParenLoc,
                                                      EndLoc);
  }

  /// Build a new OpenMP 'dist_schedule' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *
  RebuildOMPDistScheduleClause(OpenMPDistScheduleClauseKind Kind,
                               Expr *ChunkSize, SourceLocation StartLoc,
                               SourceLocation LParenLoc, SourceLocation KindLoc,
                               SourceLocation CommaLoc, SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDistScheduleClause(
        Kind, ChunkSize, StartLoc, LParenLoc, KindLoc, CommaLoc, EndLoc);
  }

  /// Build a new OpenMP 'to' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *
  RebuildOMPToClause(ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                     ArrayRef<SourceLocation> MotionModifiersLoc,
                     CXXScopeSpec &MapperIdScopeSpec,
                     DeclarationNameInfo &MapperId, SourceLocation ColonLoc,
                     ArrayRef<Expr *> VarList, const OMPVarListLocTy &Locs,
                     ArrayRef<Expr *> UnresolvedMappers) {
    return getSema().OpenMP().ActOnOpenMPToClause(
        MotionModifiers, MotionModifiersLoc, MapperIdScopeSpec, MapperId,
        ColonLoc, VarList, Locs, UnresolvedMappers);
  }

  /// Build a new OpenMP 'from' clause.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *
  RebuildOMPFromClause(ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                       ArrayRef<SourceLocation> MotionModifiersLoc,
                       CXXScopeSpec &MapperIdScopeSpec,
                       DeclarationNameInfo &MapperId, SourceLocation ColonLoc,
                       ArrayRef<Expr *> VarList, const OMPVarListLocTy &Locs,
                       ArrayRef<Expr *> UnresolvedMappers) {
    return getSema().OpenMP().ActOnOpenMPFromClause(
        MotionModifiers, MotionModifiersLoc, MapperIdScopeSpec, MapperId,
        ColonLoc, VarList, Locs, UnresolvedMappers);
  }

  /// Build a new OpenMP 'use_device_ptr' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPUseDevicePtrClause(ArrayRef<Expr *> VarList,
                                          const OMPVarListLocTy &Locs) {
    return getSema().OpenMP().ActOnOpenMPUseDevicePtrClause(VarList, Locs);
  }

  /// Build a new OpenMP 'use_device_addr' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPUseDeviceAddrClause(ArrayRef<Expr *> VarList,
                                           const OMPVarListLocTy &Locs) {
    return getSema().OpenMP().ActOnOpenMPUseDeviceAddrClause(VarList, Locs);
  }

  /// Build a new OpenMP 'is_device_ptr' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPIsDevicePtrClause(ArrayRef<Expr *> VarList,
                                         const OMPVarListLocTy &Locs) {
    return getSema().OpenMP().ActOnOpenMPIsDevicePtrClause(VarList, Locs);
  }

  /// Build a new OpenMP 'has_device_addr' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPHasDeviceAddrClause(ArrayRef<Expr *> VarList,
                                           const OMPVarListLocTy &Locs) {
    return getSema().OpenMP().ActOnOpenMPHasDeviceAddrClause(VarList, Locs);
  }

  /// Build a new OpenMP 'defaultmap' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDefaultmapClause(OpenMPDefaultmapClauseModifier M,
                                        OpenMPDefaultmapClauseKind Kind,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation MLoc,
                                        SourceLocation KindLoc,
                                        SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDefaultmapClause(
        M, Kind, StartLoc, LParenLoc, MLoc, KindLoc, EndLoc);
  }

  /// Build a new OpenMP 'nontemporal' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPNontemporalClause(ArrayRef<Expr *> VarList,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPNontemporalClause(VarList, StartLoc,
                                                           LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'inclusive' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPInclusiveClause(ArrayRef<Expr *> VarList,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPInclusiveClause(VarList, StartLoc,
                                                         LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'exclusive' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPExclusiveClause(ArrayRef<Expr *> VarList,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPExclusiveClause(VarList, StartLoc,
                                                         LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'uses_allocators' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPUsesAllocatorsClause(
      ArrayRef<SemaOpenMP::UsesAllocatorsData> Data, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPUsesAllocatorClause(
        StartLoc, LParenLoc, EndLoc, Data);
  }

  /// Build a new OpenMP 'affinity' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPAffinityClause(SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation ColonLoc,
                                      SourceLocation EndLoc, Expr *Modifier,
                                      ArrayRef<Expr *> Locators) {
    return getSema().OpenMP().ActOnOpenMPAffinityClause(
        StartLoc, LParenLoc, ColonLoc, EndLoc, Modifier, Locators);
  }

  /// Build a new OpenMP 'order' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPOrderClause(
      OpenMPOrderClauseKind Kind, SourceLocation KindKwLoc,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc,
      OpenMPOrderClauseModifier Modifier, SourceLocation ModifierKwLoc) {
    return getSema().OpenMP().ActOnOpenMPOrderClause(
        Modifier, Kind, StartLoc, LParenLoc, ModifierKwLoc, KindKwLoc, EndLoc);
  }

  /// Build a new OpenMP 'init' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPInitClause(Expr *InteropVar, OMPInteropInfo &InteropInfo,
                                  SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation VarLoc,
                                  SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPInitClause(
        InteropVar, InteropInfo, StartLoc, LParenLoc, VarLoc, EndLoc);
  }

  /// Build a new OpenMP 'use' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPUseClause(Expr *InteropVar, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation VarLoc, SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPUseClause(InteropVar, StartLoc,
                                                   LParenLoc, VarLoc, EndLoc);
  }

  /// Build a new OpenMP 'destroy' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPDestroyClause(Expr *InteropVar, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation VarLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDestroyClause(
        InteropVar, StartLoc, LParenLoc, VarLoc, EndLoc);
  }

  /// Build a new OpenMP 'novariants' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPNovariantsClause(Expr *Condition,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPNovariantsClause(Condition, StartLoc,
                                                          LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'nocontext' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPNocontextClause(Expr *Condition, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPNocontextClause(Condition, StartLoc,
                                                         LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'filter' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPFilterClause(Expr *ThreadID, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPFilterClause(ThreadID, StartLoc,
                                                      LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'bind' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPBindClause(OpenMPBindClauseKind Kind,
                                  SourceLocation KindLoc,
                                  SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPBindClause(Kind, KindLoc, StartLoc,
                                                    LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'ompx_dyn_cgroup_mem' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPXDynCGroupMemClause(Expr *Size, SourceLocation StartLoc,
                                           SourceLocation LParenLoc,
                                           SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPXDynCGroupMemClause(Size, StartLoc,
                                                             LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'ompx_attribute' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPXAttributeClause(ArrayRef<const Attr *> Attrs,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPXAttributeClause(Attrs, StartLoc,
                                                          LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'ompx_bare' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPXBareClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPXBareClause(StartLoc, EndLoc);
  }

  /// Build a new OpenMP 'align' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPAlignClause(Expr *A, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPAlignClause(A, StartLoc, LParenLoc,
                                                     EndLoc);
  }

  /// Build a new OpenMP 'at' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPAtClause(OpenMPAtClauseKind Kind, SourceLocation KwLoc,
                                SourceLocation StartLoc,
                                SourceLocation LParenLoc,
                                SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPAtClause(Kind, KwLoc, StartLoc,
                                                  LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'severity' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPSeverityClause(OpenMPSeverityClauseKind Kind,
                                      SourceLocation KwLoc,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPSeverityClause(Kind, KwLoc, StartLoc,
                                                        LParenLoc, EndLoc);
  }

  /// Build a new OpenMP 'message' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *RebuildOMPMessageClause(Expr *MS, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPMessageClause(MS, StartLoc, LParenLoc,
                                                       EndLoc);
  }

  /// Build a new OpenMP 'doacross' clause.
  ///
  /// By default, performs semantic analysis to build the new OpenMP clause.
  /// Subclasses may override this routine to provide different behavior.
  OMPClause *
  RebuildOMPDoacrossClause(OpenMPDoacrossClauseModifier DepType,
                           SourceLocation DepLoc, SourceLocation ColonLoc,
                           ArrayRef<Expr *> VarList, SourceLocation StartLoc,
                           SourceLocation LParenLoc, SourceLocation EndLoc) {
    return getSema().OpenMP().ActOnOpenMPDoacrossClause(
        DepType, DepLoc, ColonLoc, VarList, StartLoc, LParenLoc, EndLoc);
  }

  /// Rebuild the operand to an Objective-C \@synchronized statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCAtSynchronizedOperand(SourceLocation atLoc,
                                              Expr *object) {
    return getSema().ObjC().ActOnObjCAtSynchronizedOperand(atLoc, object);
  }

  /// Build a new Objective-C \@synchronized statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCAtSynchronizedStmt(SourceLocation AtLoc,
                                           Expr *Object, Stmt *Body) {
    return getSema().ObjC().ActOnObjCAtSynchronizedStmt(AtLoc, Object, Body);
  }

  /// Build a new Objective-C \@autoreleasepool statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCAutoreleasePoolStmt(SourceLocation AtLoc,
                                            Stmt *Body) {
    return getSema().ObjC().ActOnObjCAutoreleasePoolStmt(AtLoc, Body);
  }

  /// Build a new Objective-C fast enumeration statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildObjCForCollectionStmt(SourceLocation ForLoc,
                                          Stmt *Element,
                                          Expr *Collection,
                                          SourceLocation RParenLoc,
                                          Stmt *Body) {
    StmtResult ForEachStmt = getSema().ObjC().ActOnObjCForCollectionStmt(
        ForLoc, Element, Collection, RParenLoc);
    if (ForEachStmt.isInvalid())
      return StmtError();

    return getSema().ObjC().FinishObjCForCollectionStmt(ForEachStmt.get(),
                                                        Body);
  }

  /// Build a new C++ exception declaration.
  ///
  /// By default, performs semantic analysis to build the new decaration.
  /// Subclasses may override this routine to provide different behavior.
  VarDecl *RebuildExceptionDecl(VarDecl *ExceptionDecl,
                                TypeSourceInfo *Declarator,
                                SourceLocation StartLoc,
                                SourceLocation IdLoc,
                                IdentifierInfo *Id) {
    VarDecl *Var = getSema().BuildExceptionDeclaration(nullptr, Declarator,
                                                       StartLoc, IdLoc, Id);
    if (Var)
      getSema().CurContext->addDecl(Var);
    return Var;
  }

  /// Build a new C++ catch statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCXXCatchStmt(SourceLocation CatchLoc,
                                 VarDecl *ExceptionDecl,
                                 Stmt *Handler) {
    return Owned(new (getSema().Context) CXXCatchStmt(CatchLoc, ExceptionDecl,
                                                      Handler));
  }

  /// Build a new C++ try statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCXXTryStmt(SourceLocation TryLoc, Stmt *TryBlock,
                               ArrayRef<Stmt *> Handlers) {
    return getSema().ActOnCXXTryBlock(TryLoc, TryBlock, Handlers);
  }

  /// Build a new C++0x range-based for statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildCXXForRangeStmt(
      SourceLocation ForLoc, SourceLocation CoawaitLoc, Stmt *Init,
      SourceLocation ColonLoc, Stmt *Range, Stmt *Begin, Stmt *End, Expr *Cond,
      Expr *Inc, Stmt *LoopVar, SourceLocation RParenLoc,
      ArrayRef<MaterializeTemporaryExpr *> LifetimeExtendTemps) {
    // If we've just learned that the range is actually an Objective-C
    // collection, treat this as an Objective-C fast enumeration loop.
    if (DeclStmt *RangeStmt = dyn_cast<DeclStmt>(Range)) {
      if (RangeStmt->isSingleDecl()) {
        if (VarDecl *RangeVar = dyn_cast<VarDecl>(RangeStmt->getSingleDecl())) {
          if (RangeVar->isInvalidDecl())
            return StmtError();

          Expr *RangeExpr = RangeVar->getInit();
          if (!RangeExpr->isTypeDependent() &&
              RangeExpr->getType()->isObjCObjectPointerType()) {
            // FIXME: Support init-statements in Objective-C++20 ranged for
            // statement.
            if (Init) {
              return SemaRef.Diag(Init->getBeginLoc(),
                                  diag::err_objc_for_range_init_stmt)
                         << Init->getSourceRange();
            }
            return getSema().ObjC().ActOnObjCForCollectionStmt(
                ForLoc, LoopVar, RangeExpr, RParenLoc);
          }
        }
      }
    }

    return getSema().BuildCXXForRangeStmt(
        ForLoc, CoawaitLoc, Init, ColonLoc, Range, Begin, End, Cond, Inc,
        LoopVar, RParenLoc, Sema::BFRK_Rebuild, LifetimeExtendTemps);
  }

  /// Build a new C++0x range-based for statement.
  ///
  /// By default, performs semantic analysis to build the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult RebuildMSDependentExistsStmt(SourceLocation KeywordLoc,
                                          bool IsIfExists,
                                          NestedNameSpecifierLoc QualifierLoc,
                                          DeclarationNameInfo NameInfo,
                                          Stmt *Nested) {
    return getSema().BuildMSDependentExistsStmt(KeywordLoc, IsIfExists,
                                                QualifierLoc, NameInfo, Nested);
  }

  /// Attach body to a C++0x range-based for statement.
  ///
  /// By default, performs semantic analysis to finish the new statement.
  /// Subclasses may override this routine to provide different behavior.
  StmtResult FinishCXXForRangeStmt(Stmt *ForRange, Stmt *Body) {
    return getSema().FinishCXXForRangeStmt(ForRange, Body);
  }

  StmtResult RebuildSEHTryStmt(bool IsCXXTry, SourceLocation TryLoc,
                               Stmt *TryBlock, Stmt *Handler) {
    return getSema().ActOnSEHTryBlock(IsCXXTry, TryLoc, TryBlock, Handler);
  }

  StmtResult RebuildSEHExceptStmt(SourceLocation Loc, Expr *FilterExpr,
                                  Stmt *Block) {
    return getSema().ActOnSEHExceptBlock(Loc, FilterExpr, Block);
  }

  StmtResult RebuildSEHFinallyStmt(SourceLocation Loc, Stmt *Block) {
    return SEHFinallyStmt::Create(getSema().getASTContext(), Loc, Block);
  }

  ExprResult RebuildSYCLUniqueStableNameExpr(SourceLocation OpLoc,
                                             SourceLocation LParen,
                                             SourceLocation RParen,
                                             TypeSourceInfo *TSI) {
    return getSema().SYCL().BuildUniqueStableNameExpr(OpLoc, LParen, RParen,
                                                      TSI);
  }

  /// Build a new predefined expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildPredefinedExpr(SourceLocation Loc, PredefinedIdentKind IK) {
    return getSema().BuildPredefinedExpr(Loc, IK);
  }

  /// Build a new expression that references a declaration.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildDeclarationNameExpr(const CXXScopeSpec &SS,
                                        LookupResult &R,
                                        bool RequiresADL) {
    return getSema().BuildDeclarationNameExpr(SS, R, RequiresADL);
  }


  /// Build a new expression that references a declaration.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildDeclRefExpr(NestedNameSpecifierLoc QualifierLoc,
                                ValueDecl *VD,
                                const DeclarationNameInfo &NameInfo,
                                NamedDecl *Found,
                                TemplateArgumentListInfo *TemplateArgs) {
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);
    return getSema().BuildDeclarationNameExpr(SS, NameInfo, VD, Found,
                                              TemplateArgs);
  }

  /// Build a new expression in parentheses.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildParenExpr(Expr *SubExpr, SourceLocation LParen,
                                    SourceLocation RParen) {
    return getSema().ActOnParenExpr(LParen, RParen, SubExpr);
  }

  /// Build a new pseudo-destructor expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXPseudoDestructorExpr(Expr *Base,
                                            SourceLocation OperatorLoc,
                                            bool isArrow,
                                            CXXScopeSpec &SS,
                                            TypeSourceInfo *ScopeType,
                                            SourceLocation CCLoc,
                                            SourceLocation TildeLoc,
                                        PseudoDestructorTypeStorage Destroyed);

  /// Build a new unary operator expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildUnaryOperator(SourceLocation OpLoc,
                                        UnaryOperatorKind Opc,
                                        Expr *SubExpr) {
    return getSema().BuildUnaryOp(/*Scope=*/nullptr, OpLoc, Opc, SubExpr);
  }

  /// Build a new builtin offsetof expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildOffsetOfExpr(SourceLocation OperatorLoc,
                                 TypeSourceInfo *Type,
                                 ArrayRef<Sema::OffsetOfComponent> Components,
                                 SourceLocation RParenLoc) {
    return getSema().BuildBuiltinOffsetOf(OperatorLoc, Type, Components,
                                          RParenLoc);
  }

  /// Build a new sizeof, alignof or vec_step expression with a
  /// type argument.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildUnaryExprOrTypeTrait(TypeSourceInfo *TInfo,
                                         SourceLocation OpLoc,
                                         UnaryExprOrTypeTrait ExprKind,
                                         SourceRange R) {
    return getSema().CreateUnaryExprOrTypeTraitExpr(TInfo, OpLoc, ExprKind, R);
  }

  /// Build a new sizeof, alignof or vec step expression with an
  /// expression argument.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildUnaryExprOrTypeTrait(Expr *SubExpr, SourceLocation OpLoc,
                                         UnaryExprOrTypeTrait ExprKind,
                                         SourceRange R) {
    ExprResult Result
      = getSema().CreateUnaryExprOrTypeTraitExpr(SubExpr, OpLoc, ExprKind);
    if (Result.isInvalid())
      return ExprError();

    return Result;
  }

  /// Build a new array subscript expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildArraySubscriptExpr(Expr *LHS,
                                             SourceLocation LBracketLoc,
                                             Expr *RHS,
                                             SourceLocation RBracketLoc) {
    return getSema().ActOnArraySubscriptExpr(/*Scope=*/nullptr, LHS,
                                             LBracketLoc, RHS,
                                             RBracketLoc);
  }

  /// Build a new matrix subscript expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                        Expr *ColumnIdx,
                                        SourceLocation RBracketLoc) {
    return getSema().CreateBuiltinMatrixSubscriptExpr(Base, RowIdx, ColumnIdx,
                                                      RBracketLoc);
  }

  /// Build a new array section expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildArraySectionExpr(bool IsOMPArraySection, Expr *Base,
                                     SourceLocation LBracketLoc,
                                     Expr *LowerBound,
                                     SourceLocation ColonLocFirst,
                                     SourceLocation ColonLocSecond,
                                     Expr *Length, Expr *Stride,
                                     SourceLocation RBracketLoc) {
    if (IsOMPArraySection)
      return getSema().OpenMP().ActOnOMPArraySectionExpr(
          Base, LBracketLoc, LowerBound, ColonLocFirst, ColonLocSecond, Length,
          Stride, RBracketLoc);

    assert(Stride == nullptr && !ColonLocSecond.isValid() &&
           "Stride/second colon not allowed for OpenACC");

    return getSema().OpenACC().ActOnArraySectionExpr(
        Base, LBracketLoc, LowerBound, ColonLocFirst, Length, RBracketLoc);
  }

  /// Build a new array shaping expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildOMPArrayShapingExpr(Expr *Base, SourceLocation LParenLoc,
                                        SourceLocation RParenLoc,
                                        ArrayRef<Expr *> Dims,
                                        ArrayRef<SourceRange> BracketsRanges) {
    return getSema().OpenMP().ActOnOMPArrayShapingExpr(
        Base, LParenLoc, RParenLoc, Dims, BracketsRanges);
  }

  /// Build a new iterator expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult
  RebuildOMPIteratorExpr(SourceLocation IteratorKwLoc, SourceLocation LLoc,
                         SourceLocation RLoc,
                         ArrayRef<SemaOpenMP::OMPIteratorData> Data) {
    return getSema().OpenMP().ActOnOMPIteratorExpr(
        /*Scope=*/nullptr, IteratorKwLoc, LLoc, RLoc, Data);
  }

  /// Build a new call expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCallExpr(Expr *Callee, SourceLocation LParenLoc,
                                   MultiExprArg Args,
                                   SourceLocation RParenLoc,
                                   Expr *ExecConfig = nullptr) {
    return getSema().ActOnCallExpr(
        /*Scope=*/nullptr, Callee, LParenLoc, Args, RParenLoc, ExecConfig);
  }

  ExprResult RebuildCxxSubscriptExpr(Expr *Callee, SourceLocation LParenLoc,
                                     MultiExprArg Args,
                                     SourceLocation RParenLoc) {
    return getSema().ActOnArraySubscriptExpr(
        /*Scope=*/nullptr, Callee, LParenLoc, Args, RParenLoc);
  }

  /// Build a new member access expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildMemberExpr(Expr *Base, SourceLocation OpLoc,
                               bool isArrow,
                               NestedNameSpecifierLoc QualifierLoc,
                               SourceLocation TemplateKWLoc,
                               const DeclarationNameInfo &MemberNameInfo,
                               ValueDecl *Member,
                               NamedDecl *FoundDecl,
                        const TemplateArgumentListInfo *ExplicitTemplateArgs,
                               NamedDecl *FirstQualifierInScope) {
    ExprResult BaseResult = getSema().PerformMemberExprBaseConversion(Base,
                                                                      isArrow);
    if (!Member->getDeclName()) {
      // We have a reference to an unnamed field.  This is always the
      // base of an anonymous struct/union member access, i.e. the
      // field is always of record type.
      assert(Member->getType()->isRecordType() &&
             "unnamed member not of record type?");

      BaseResult =
        getSema().PerformObjectMemberConversion(BaseResult.get(),
                                                QualifierLoc.getNestedNameSpecifier(),
                                                FoundDecl, Member);
      if (BaseResult.isInvalid())
        return ExprError();
      Base = BaseResult.get();

      // `TranformMaterializeTemporaryExpr()` removes materialized temporaries
      // from the AST, so we need to re-insert them if needed (since
      // `BuildFieldRefereneExpr()` doesn't do this).
      if (!isArrow && Base->isPRValue()) {
        BaseResult = getSema().TemporaryMaterializationConversion(Base);
        if (BaseResult.isInvalid())
          return ExprError();
        Base = BaseResult.get();
      }

      CXXScopeSpec EmptySS;
      return getSema().BuildFieldReferenceExpr(
          Base, isArrow, OpLoc, EmptySS, cast<FieldDecl>(Member),
          DeclAccessPair::make(FoundDecl, FoundDecl->getAccess()),
          MemberNameInfo);
    }

    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);

    Base = BaseResult.get();
    if (Base->containsErrors())
      return ExprError();

    QualType BaseType = Base->getType();

    if (isArrow && !BaseType->isPointerType())
      return ExprError();

    // FIXME: this involves duplicating earlier analysis in a lot of
    // cases; we should avoid this when possible.
    LookupResult R(getSema(), MemberNameInfo, Sema::LookupMemberName);
    R.addDecl(FoundDecl);
    R.resolveKind();

    if (getSema().isUnevaluatedContext() && Base->isImplicitCXXThis() &&
        isa<FieldDecl, IndirectFieldDecl, MSPropertyDecl>(Member)) {
      if (auto *ThisClass = cast<CXXThisExpr>(Base)
                                ->getType()
                                ->getPointeeType()
                                ->getAsCXXRecordDecl()) {
        auto *Class = cast<CXXRecordDecl>(Member->getDeclContext());
        // In unevaluated contexts, an expression supposed to be a member access
        // might reference a member in an unrelated class.
        if (!ThisClass->Equals(Class) && !ThisClass->isDerivedFrom(Class))
          return getSema().BuildDeclRefExpr(Member, Member->getType(),
                                            VK_LValue, Member->getLocation());
      }
    }

    return getSema().BuildMemberReferenceExpr(Base, BaseType, OpLoc, isArrow,
                                              SS, TemplateKWLoc,
                                              FirstQualifierInScope,
                                              R, ExplicitTemplateArgs,
                                              /*S*/nullptr);
  }

  /// Build a new binary operator expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildBinaryOperator(SourceLocation OpLoc,
                                         BinaryOperatorKind Opc,
                                         Expr *LHS, Expr *RHS) {
    return getSema().BuildBinOp(/*Scope=*/nullptr, OpLoc, Opc, LHS, RHS);
  }

  /// Build a new rewritten operator expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXRewrittenBinaryOperator(
      SourceLocation OpLoc, BinaryOperatorKind Opcode,
      const UnresolvedSetImpl &UnqualLookups, Expr *LHS, Expr *RHS) {
    return getSema().CreateOverloadedBinOp(OpLoc, Opcode, UnqualLookups, LHS,
                                           RHS, /*RequiresADL*/false);
  }

  /// Build a new conditional operator expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildConditionalOperator(Expr *Cond,
                                        SourceLocation QuestionLoc,
                                        Expr *LHS,
                                        SourceLocation ColonLoc,
                                        Expr *RHS) {
    return getSema().ActOnConditionalOp(QuestionLoc, ColonLoc, Cond,
                                        LHS, RHS);
  }

  /// Build a new C-style cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCStyleCastExpr(SourceLocation LParenLoc,
                                         TypeSourceInfo *TInfo,
                                         SourceLocation RParenLoc,
                                         Expr *SubExpr) {
    return getSema().BuildCStyleCastExpr(LParenLoc, TInfo, RParenLoc,
                                         SubExpr);
  }

  /// Build a new compound literal expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCompoundLiteralExpr(SourceLocation LParenLoc,
                                              TypeSourceInfo *TInfo,
                                              SourceLocation RParenLoc,
                                              Expr *Init) {
    return getSema().BuildCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc,
                                              Init);
  }

  /// Build a new extended vector element access expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildExtVectorElementExpr(Expr *Base, SourceLocation OpLoc,
                                         bool IsArrow,
                                         SourceLocation AccessorLoc,
                                         IdentifierInfo &Accessor) {

    CXXScopeSpec SS;
    DeclarationNameInfo NameInfo(&Accessor, AccessorLoc);
    return getSema().BuildMemberReferenceExpr(
        Base, Base->getType(), OpLoc, IsArrow, SS, SourceLocation(),
        /*FirstQualifierInScope*/ nullptr, NameInfo,
        /* TemplateArgs */ nullptr,
        /*S*/ nullptr);
  }

  /// Build a new initializer list expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildInitList(SourceLocation LBraceLoc,
                             MultiExprArg Inits,
                             SourceLocation RBraceLoc) {
    return SemaRef.BuildInitList(LBraceLoc, Inits, RBraceLoc);
  }

  /// Build a new designated initializer expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildDesignatedInitExpr(Designation &Desig,
                                             MultiExprArg ArrayExprs,
                                             SourceLocation EqualOrColonLoc,
                                             bool GNUSyntax,
                                             Expr *Init) {
    ExprResult Result
      = SemaRef.ActOnDesignatedInitializer(Desig, EqualOrColonLoc, GNUSyntax,
                                           Init);
    if (Result.isInvalid())
      return ExprError();

    return Result;
  }

  /// Build a new value-initialized expression.
  ///
  /// By default, builds the implicit value initialization without performing
  /// any semantic analysis. Subclasses may override this routine to provide
  /// different behavior.
  ExprResult RebuildImplicitValueInitExpr(QualType T) {
    return new (SemaRef.Context) ImplicitValueInitExpr(T);
  }

  /// Build a new \c va_arg expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildVAArgExpr(SourceLocation BuiltinLoc,
                                    Expr *SubExpr, TypeSourceInfo *TInfo,
                                    SourceLocation RParenLoc) {
    return getSema().BuildVAArgExpr(BuiltinLoc,
                                    SubExpr, TInfo,
                                    RParenLoc);
  }

  /// Build a new expression list in parentheses.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildParenListExpr(SourceLocation LParenLoc,
                                  MultiExprArg SubExprs,
                                  SourceLocation RParenLoc) {
    return getSema().ActOnParenListExpr(LParenLoc, RParenLoc, SubExprs);
  }

  /// Build a new address-of-label expression.
  ///
  /// By default, performs semantic analysis, using the name of the label
  /// rather than attempting to map the label statement itself.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildAddrLabelExpr(SourceLocation AmpAmpLoc,
                                  SourceLocation LabelLoc, LabelDecl *Label) {
    return getSema().ActOnAddrLabel(AmpAmpLoc, LabelLoc, Label);
  }

  /// Build a new GNU statement expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildStmtExpr(SourceLocation LParenLoc, Stmt *SubStmt,
                             SourceLocation RParenLoc, unsigned TemplateDepth) {
    return getSema().BuildStmtExpr(LParenLoc, SubStmt, RParenLoc,
                                   TemplateDepth);
  }

  /// Build a new __builtin_choose_expr expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildChooseExpr(SourceLocation BuiltinLoc,
                                     Expr *Cond, Expr *LHS, Expr *RHS,
                                     SourceLocation RParenLoc) {
    return SemaRef.ActOnChooseExpr(BuiltinLoc,
                                   Cond, LHS, RHS,
                                   RParenLoc);
  }

  /// Build a new generic selection expression with an expression predicate.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildGenericSelectionExpr(SourceLocation KeyLoc,
                                         SourceLocation DefaultLoc,
                                         SourceLocation RParenLoc,
                                         Expr *ControllingExpr,
                                         ArrayRef<TypeSourceInfo *> Types,
                                         ArrayRef<Expr *> Exprs) {
    return getSema().CreateGenericSelectionExpr(KeyLoc, DefaultLoc, RParenLoc,
                                                /*PredicateIsExpr=*/true,
                                                ControllingExpr, Types, Exprs);
  }

  /// Build a new generic selection expression with a type predicate.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildGenericSelectionExpr(SourceLocation KeyLoc,
                                         SourceLocation DefaultLoc,
                                         SourceLocation RParenLoc,
                                         TypeSourceInfo *ControllingType,
                                         ArrayRef<TypeSourceInfo *> Types,
                                         ArrayRef<Expr *> Exprs) {
    return getSema().CreateGenericSelectionExpr(KeyLoc, DefaultLoc, RParenLoc,
                                                /*PredicateIsExpr=*/false,
                                                ControllingType, Types, Exprs);
  }

  /// Build a new overloaded operator call expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// The semantic analysis provides the behavior of template instantiation,
  /// copying with transformations that turn what looks like an overloaded
  /// operator call into a use of a builtin operator, performing
  /// argument-dependent lookup, etc. Subclasses may override this routine to
  /// provide different behavior.
  ExprResult RebuildCXXOperatorCallExpr(OverloadedOperatorKind Op,
                                        SourceLocation OpLoc,
                                        SourceLocation CalleeLoc,
                                        bool RequiresADL,
                                        const UnresolvedSetImpl &Functions,
                                        Expr *First, Expr *Second);

  /// Build a new C++ "named" cast expression, such as static_cast or
  /// reinterpret_cast.
  ///
  /// By default, this routine dispatches to one of the more-specific routines
  /// for a particular named case, e.g., RebuildCXXStaticCastExpr().
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXNamedCastExpr(SourceLocation OpLoc,
                                           Stmt::StmtClass Class,
                                           SourceLocation LAngleLoc,
                                           TypeSourceInfo *TInfo,
                                           SourceLocation RAngleLoc,
                                           SourceLocation LParenLoc,
                                           Expr *SubExpr,
                                           SourceLocation RParenLoc) {
    switch (Class) {
    case Stmt::CXXStaticCastExprClass:
      return getDerived().RebuildCXXStaticCastExpr(OpLoc, LAngleLoc, TInfo,
                                                   RAngleLoc, LParenLoc,
                                                   SubExpr, RParenLoc);

    case Stmt::CXXDynamicCastExprClass:
      return getDerived().RebuildCXXDynamicCastExpr(OpLoc, LAngleLoc, TInfo,
                                                    RAngleLoc, LParenLoc,
                                                    SubExpr, RParenLoc);

    case Stmt::CXXReinterpretCastExprClass:
      return getDerived().RebuildCXXReinterpretCastExpr(OpLoc, LAngleLoc, TInfo,
                                                        RAngleLoc, LParenLoc,
                                                        SubExpr,
                                                        RParenLoc);

    case Stmt::CXXConstCastExprClass:
      return getDerived().RebuildCXXConstCastExpr(OpLoc, LAngleLoc, TInfo,
                                                   RAngleLoc, LParenLoc,
                                                   SubExpr, RParenLoc);

    case Stmt::CXXAddrspaceCastExprClass:
      return getDerived().RebuildCXXAddrspaceCastExpr(
          OpLoc, LAngleLoc, TInfo, RAngleLoc, LParenLoc, SubExpr, RParenLoc);

    default:
      llvm_unreachable("Invalid C++ named cast");
    }
  }

  /// Build a new C++ static_cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXStaticCastExpr(SourceLocation OpLoc,
                                            SourceLocation LAngleLoc,
                                            TypeSourceInfo *TInfo,
                                            SourceLocation RAngleLoc,
                                            SourceLocation LParenLoc,
                                            Expr *SubExpr,
                                            SourceLocation RParenLoc) {
    return getSema().BuildCXXNamedCast(OpLoc, tok::kw_static_cast,
                                       TInfo, SubExpr,
                                       SourceRange(LAngleLoc, RAngleLoc),
                                       SourceRange(LParenLoc, RParenLoc));
  }

  /// Build a new C++ dynamic_cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXDynamicCastExpr(SourceLocation OpLoc,
                                             SourceLocation LAngleLoc,
                                             TypeSourceInfo *TInfo,
                                             SourceLocation RAngleLoc,
                                             SourceLocation LParenLoc,
                                             Expr *SubExpr,
                                             SourceLocation RParenLoc) {
    return getSema().BuildCXXNamedCast(OpLoc, tok::kw_dynamic_cast,
                                       TInfo, SubExpr,
                                       SourceRange(LAngleLoc, RAngleLoc),
                                       SourceRange(LParenLoc, RParenLoc));
  }

  /// Build a new C++ reinterpret_cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXReinterpretCastExpr(SourceLocation OpLoc,
                                                 SourceLocation LAngleLoc,
                                                 TypeSourceInfo *TInfo,
                                                 SourceLocation RAngleLoc,
                                                 SourceLocation LParenLoc,
                                                 Expr *SubExpr,
                                                 SourceLocation RParenLoc) {
    return getSema().BuildCXXNamedCast(OpLoc, tok::kw_reinterpret_cast,
                                       TInfo, SubExpr,
                                       SourceRange(LAngleLoc, RAngleLoc),
                                       SourceRange(LParenLoc, RParenLoc));
  }

  /// Build a new C++ const_cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXConstCastExpr(SourceLocation OpLoc,
                                           SourceLocation LAngleLoc,
                                           TypeSourceInfo *TInfo,
                                           SourceLocation RAngleLoc,
                                           SourceLocation LParenLoc,
                                           Expr *SubExpr,
                                           SourceLocation RParenLoc) {
    return getSema().BuildCXXNamedCast(OpLoc, tok::kw_const_cast,
                                       TInfo, SubExpr,
                                       SourceRange(LAngleLoc, RAngleLoc),
                                       SourceRange(LParenLoc, RParenLoc));
  }

  ExprResult
  RebuildCXXAddrspaceCastExpr(SourceLocation OpLoc, SourceLocation LAngleLoc,
                              TypeSourceInfo *TInfo, SourceLocation RAngleLoc,
                              SourceLocation LParenLoc, Expr *SubExpr,
                              SourceLocation RParenLoc) {
    return getSema().BuildCXXNamedCast(
        OpLoc, tok::kw_addrspace_cast, TInfo, SubExpr,
        SourceRange(LAngleLoc, RAngleLoc), SourceRange(LParenLoc, RParenLoc));
  }

  /// Build a new C++ functional-style cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXFunctionalCastExpr(TypeSourceInfo *TInfo,
                                          SourceLocation LParenLoc,
                                          Expr *Sub,
                                          SourceLocation RParenLoc,
                                          bool ListInitialization) {
    // If Sub is a ParenListExpr, then Sub is the syntatic form of a
    // CXXParenListInitExpr. Pass its expanded arguments so that the
    // CXXParenListInitExpr can be rebuilt.
    if (auto *PLE = dyn_cast<ParenListExpr>(Sub))
      return getSema().BuildCXXTypeConstructExpr(
          TInfo, LParenLoc, MultiExprArg(PLE->getExprs(), PLE->getNumExprs()),
          RParenLoc, ListInitialization);
    return getSema().BuildCXXTypeConstructExpr(TInfo, LParenLoc,
                                               MultiExprArg(&Sub, 1), RParenLoc,
                                               ListInitialization);
  }

  /// Build a new C++ __builtin_bit_cast expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildBuiltinBitCastExpr(SourceLocation KWLoc,
                                       TypeSourceInfo *TSI, Expr *Sub,
                                       SourceLocation RParenLoc) {
    return getSema().BuildBuiltinBitCastExpr(KWLoc, TSI, Sub, RParenLoc);
  }

  /// Build a new C++ typeid(type) expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXTypeidExpr(QualType TypeInfoType,
                                        SourceLocation TypeidLoc,
                                        TypeSourceInfo *Operand,
                                        SourceLocation RParenLoc) {
    return getSema().BuildCXXTypeId(TypeInfoType, TypeidLoc, Operand,
                                    RParenLoc);
  }


  /// Build a new C++ typeid(expr) expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXTypeidExpr(QualType TypeInfoType,
                                        SourceLocation TypeidLoc,
                                        Expr *Operand,
                                        SourceLocation RParenLoc) {
    return getSema().BuildCXXTypeId(TypeInfoType, TypeidLoc, Operand,
                                    RParenLoc);
  }

  /// Build a new C++ __uuidof(type) expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXUuidofExpr(QualType Type, SourceLocation TypeidLoc,
                                  TypeSourceInfo *Operand,
                                  SourceLocation RParenLoc) {
    return getSema().BuildCXXUuidof(Type, TypeidLoc, Operand, RParenLoc);
  }

  /// Build a new C++ __uuidof(expr) expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXUuidofExpr(QualType Type, SourceLocation TypeidLoc,
                                  Expr *Operand, SourceLocation RParenLoc) {
    return getSema().BuildCXXUuidof(Type, TypeidLoc, Operand, RParenLoc);
  }

  /// Build a new C++ "this" expression.
  ///
  /// By default, performs semantic analysis to build a new "this" expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXThisExpr(SourceLocation ThisLoc,
                                QualType ThisType,
                                bool isImplicit) {
    if (getSema().CheckCXXThisType(ThisLoc, ThisType))
      return ExprError();
    return getSema().BuildCXXThisExpr(ThisLoc, ThisType, isImplicit);
  }

  /// Build a new C++ throw expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXThrowExpr(SourceLocation ThrowLoc, Expr *Sub,
                                 bool IsThrownVariableInScope) {
    return getSema().BuildCXXThrow(ThrowLoc, Sub, IsThrownVariableInScope);
  }

  /// Build a new C++ default-argument expression.
  ///
  /// By default, builds a new default-argument expression, which does not
  /// require any semantic analysis. Subclasses may override this routine to
  /// provide different behavior.
  ExprResult RebuildCXXDefaultArgExpr(SourceLocation Loc, ParmVarDecl *Param,
                                      Expr *RewrittenExpr) {
    return CXXDefaultArgExpr::Create(getSema().Context, Loc, Param,
                                     RewrittenExpr, getSema().CurContext);
  }

  /// Build a new C++11 default-initialization expression.
  ///
  /// By default, builds a new default field initialization expression, which
  /// does not require any semantic analysis. Subclasses may override this
  /// routine to provide different behavior.
  ExprResult RebuildCXXDefaultInitExpr(SourceLocation Loc,
                                       FieldDecl *Field) {
    return getSema().BuildCXXDefaultInitExpr(Loc, Field);
  }

  /// Build a new C++ zero-initialization expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXScalarValueInitExpr(TypeSourceInfo *TSInfo,
                                           SourceLocation LParenLoc,
                                           SourceLocation RParenLoc) {
    return getSema().BuildCXXTypeConstructExpr(TSInfo, LParenLoc, std::nullopt,
                                               RParenLoc,
                                               /*ListInitialization=*/false);
  }

  /// Build a new C++ "new" expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXNewExpr(SourceLocation StartLoc, bool UseGlobal,
                               SourceLocation PlacementLParen,
                               MultiExprArg PlacementArgs,
                               SourceLocation PlacementRParen,
                               SourceRange TypeIdParens, QualType AllocatedType,
                               TypeSourceInfo *AllocatedTypeInfo,
                               std::optional<Expr *> ArraySize,
                               SourceRange DirectInitRange, Expr *Initializer) {
    return getSema().BuildCXXNew(StartLoc, UseGlobal,
                                 PlacementLParen,
                                 PlacementArgs,
                                 PlacementRParen,
                                 TypeIdParens,
                                 AllocatedType,
                                 AllocatedTypeInfo,
                                 ArraySize,
                                 DirectInitRange,
                                 Initializer);
  }

  /// Build a new C++ "delete" expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXDeleteExpr(SourceLocation StartLoc,
                                        bool IsGlobalDelete,
                                        bool IsArrayForm,
                                        Expr *Operand) {
    return getSema().ActOnCXXDelete(StartLoc, IsGlobalDelete, IsArrayForm,
                                    Operand);
  }

  /// Build a new type trait expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildTypeTrait(TypeTrait Trait,
                              SourceLocation StartLoc,
                              ArrayRef<TypeSourceInfo *> Args,
                              SourceLocation RParenLoc) {
    return getSema().BuildTypeTrait(Trait, StartLoc, Args, RParenLoc);
  }

  /// Build a new array type trait expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildArrayTypeTrait(ArrayTypeTrait Trait,
                                   SourceLocation StartLoc,
                                   TypeSourceInfo *TSInfo,
                                   Expr *DimExpr,
                                   SourceLocation RParenLoc) {
    return getSema().BuildArrayTypeTrait(Trait, StartLoc, TSInfo, DimExpr, RParenLoc);
  }

  /// Build a new expression trait expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildExpressionTrait(ExpressionTrait Trait,
                                   SourceLocation StartLoc,
                                   Expr *Queried,
                                   SourceLocation RParenLoc) {
    return getSema().BuildExpressionTrait(Trait, StartLoc, Queried, RParenLoc);
  }

  /// Build a new (previously unresolved) declaration reference
  /// expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildDependentScopeDeclRefExpr(
                                          NestedNameSpecifierLoc QualifierLoc,
                                          SourceLocation TemplateKWLoc,
                                       const DeclarationNameInfo &NameInfo,
                              const TemplateArgumentListInfo *TemplateArgs,
                                          bool IsAddressOfOperand,
                                          TypeSourceInfo **RecoveryTSI) {
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);

    if (TemplateArgs || TemplateKWLoc.isValid())
      return getSema().BuildQualifiedTemplateIdExpr(
          SS, TemplateKWLoc, NameInfo, TemplateArgs, IsAddressOfOperand);

    return getSema().BuildQualifiedDeclarationNameExpr(
        SS, NameInfo, IsAddressOfOperand, RecoveryTSI);
  }

  /// Build a new template-id expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildTemplateIdExpr(const CXXScopeSpec &SS,
                                   SourceLocation TemplateKWLoc,
                                   LookupResult &R,
                                   bool RequiresADL,
                              const TemplateArgumentListInfo *TemplateArgs) {
    return getSema().BuildTemplateIdExpr(SS, TemplateKWLoc, R, RequiresADL,
                                         TemplateArgs);
  }

  /// Build a new object-construction expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXConstructExpr(
      QualType T, SourceLocation Loc, CXXConstructorDecl *Constructor,
      bool IsElidable, MultiExprArg Args, bool HadMultipleCandidates,
      bool ListInitialization, bool StdInitListInitialization,
      bool RequiresZeroInit, CXXConstructionKind ConstructKind,
      SourceRange ParenRange) {
    // Reconstruct the constructor we originally found, which might be
    // different if this is a call to an inherited constructor.
    CXXConstructorDecl *FoundCtor = Constructor;
    if (Constructor->isInheritingConstructor())
      FoundCtor = Constructor->getInheritedConstructor().getConstructor();

    SmallVector<Expr *, 8> ConvertedArgs;
    if (getSema().CompleteConstructorCall(FoundCtor, T, Args, Loc,
                                          ConvertedArgs))
      return ExprError();

    return getSema().BuildCXXConstructExpr(Loc, T, Constructor,
                                           IsElidable,
                                           ConvertedArgs,
                                           HadMultipleCandidates,
                                           ListInitialization,
                                           StdInitListInitialization,
                                           RequiresZeroInit, ConstructKind,
                                           ParenRange);
  }

  /// Build a new implicit construction via inherited constructor
  /// expression.
  ExprResult RebuildCXXInheritedCtorInitExpr(QualType T, SourceLocation Loc,
                                             CXXConstructorDecl *Constructor,
                                             bool ConstructsVBase,
                                             bool InheritedFromVBase) {
    return new (getSema().Context) CXXInheritedCtorInitExpr(
        Loc, T, Constructor, ConstructsVBase, InheritedFromVBase);
  }

  /// Build a new object-construction expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXTemporaryObjectExpr(TypeSourceInfo *TSInfo,
                                           SourceLocation LParenOrBraceLoc,
                                           MultiExprArg Args,
                                           SourceLocation RParenOrBraceLoc,
                                           bool ListInitialization) {
    return getSema().BuildCXXTypeConstructExpr(
        TSInfo, LParenOrBraceLoc, Args, RParenOrBraceLoc, ListInitialization);
  }

  /// Build a new object-construction expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXUnresolvedConstructExpr(TypeSourceInfo *TSInfo,
                                               SourceLocation LParenLoc,
                                               MultiExprArg Args,
                                               SourceLocation RParenLoc,
                                               bool ListInitialization) {
    return getSema().BuildCXXTypeConstructExpr(TSInfo, LParenLoc, Args,
                                               RParenLoc, ListInitialization);
  }

  /// Build a new member reference expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXDependentScopeMemberExpr(Expr *BaseE,
                                                QualType BaseType,
                                                bool IsArrow,
                                                SourceLocation OperatorLoc,
                                          NestedNameSpecifierLoc QualifierLoc,
                                                SourceLocation TemplateKWLoc,
                                            NamedDecl *FirstQualifierInScope,
                                   const DeclarationNameInfo &MemberNameInfo,
                              const TemplateArgumentListInfo *TemplateArgs) {
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);

    return SemaRef.BuildMemberReferenceExpr(BaseE, BaseType,
                                            OperatorLoc, IsArrow,
                                            SS, TemplateKWLoc,
                                            FirstQualifierInScope,
                                            MemberNameInfo,
                                            TemplateArgs, /*S*/nullptr);
  }

  /// Build a new member reference expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildUnresolvedMemberExpr(Expr *BaseE, QualType BaseType,
                                         SourceLocation OperatorLoc,
                                         bool IsArrow,
                                         NestedNameSpecifierLoc QualifierLoc,
                                         SourceLocation TemplateKWLoc,
                                         NamedDecl *FirstQualifierInScope,
                                         LookupResult &R,
                                const TemplateArgumentListInfo *TemplateArgs) {
    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);

    return SemaRef.BuildMemberReferenceExpr(BaseE, BaseType,
                                            OperatorLoc, IsArrow,
                                            SS, TemplateKWLoc,
                                            FirstQualifierInScope,
                                            R, TemplateArgs, /*S*/nullptr);
  }

  /// Build a new noexcept expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildCXXNoexceptExpr(SourceRange Range, Expr *Arg) {
    return SemaRef.BuildCXXNoexceptExpr(Range.getBegin(), Arg, Range.getEnd());
  }

  /// Build a new expression to compute the length of a parameter pack.
  ExprResult RebuildSizeOfPackExpr(SourceLocation OperatorLoc, NamedDecl *Pack,
                                   SourceLocation PackLoc,
                                   SourceLocation RParenLoc,
                                   std::optional<unsigned> Length,
                                   ArrayRef<TemplateArgument> PartialArgs) {
    return SizeOfPackExpr::Create(SemaRef.Context, OperatorLoc, Pack, PackLoc,
                                  RParenLoc, Length, PartialArgs);
  }

  ExprResult RebuildPackIndexingExpr(SourceLocation EllipsisLoc,
                                     SourceLocation RSquareLoc,
                                     Expr *PackIdExpression, Expr *IndexExpr,
                                     ArrayRef<Expr *> ExpandedExprs,
                                     bool EmptyPack = false) {
    return getSema().BuildPackIndexingExpr(PackIdExpression, EllipsisLoc,
                                           IndexExpr, RSquareLoc, ExpandedExprs,
                                           EmptyPack);
  }

  /// Build a new expression representing a call to a source location
  ///  builtin.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildSourceLocExpr(SourceLocIdentKind Kind, QualType ResultTy,
                                  SourceLocation BuiltinLoc,
                                  SourceLocation RPLoc,
                                  DeclContext *ParentContext) {
    return getSema().BuildSourceLocExpr(Kind, ResultTy, BuiltinLoc, RPLoc,
                                        ParentContext);
  }

  /// Build a new Objective-C boxed expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildConceptSpecializationExpr(NestedNameSpecifierLoc NNS,
      SourceLocation TemplateKWLoc, DeclarationNameInfo ConceptNameInfo,
      NamedDecl *FoundDecl, ConceptDecl *NamedConcept,
      TemplateArgumentListInfo *TALI) {
    CXXScopeSpec SS;
    SS.Adopt(NNS);
    ExprResult Result = getSema().CheckConceptTemplateId(SS, TemplateKWLoc,
                                                         ConceptNameInfo,
                                                         FoundDecl,
                                                         NamedConcept, TALI);
    if (Result.isInvalid())
      return ExprError();
    return Result;
  }

  /// \brief Build a new requires expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildRequiresExpr(SourceLocation RequiresKWLoc,
                                 RequiresExprBodyDecl *Body,
                                 SourceLocation LParenLoc,
                                 ArrayRef<ParmVarDecl *> LocalParameters,
                                 SourceLocation RParenLoc,
                                 ArrayRef<concepts::Requirement *> Requirements,
                                 SourceLocation ClosingBraceLoc) {
    return RequiresExpr::Create(SemaRef.Context, RequiresKWLoc, Body, LParenLoc,
                                LocalParameters, RParenLoc, Requirements,
                                ClosingBraceLoc);
  }

  concepts::TypeRequirement *
  RebuildTypeRequirement(
      concepts::Requirement::SubstitutionDiagnostic *SubstDiag) {
    return SemaRef.BuildTypeRequirement(SubstDiag);
  }

  concepts::TypeRequirement *RebuildTypeRequirement(TypeSourceInfo *T) {
    return SemaRef.BuildTypeRequirement(T);
  }

  concepts::ExprRequirement *
  RebuildExprRequirement(
      concepts::Requirement::SubstitutionDiagnostic *SubstDiag, bool IsSimple,
      SourceLocation NoexceptLoc,
      concepts::ExprRequirement::ReturnTypeRequirement Ret) {
    return SemaRef.BuildExprRequirement(SubstDiag, IsSimple, NoexceptLoc,
                                        std::move(Ret));
  }

  concepts::ExprRequirement *
  RebuildExprRequirement(Expr *E, bool IsSimple, SourceLocation NoexceptLoc,
                         concepts::ExprRequirement::ReturnTypeRequirement Ret) {
    return SemaRef.BuildExprRequirement(E, IsSimple, NoexceptLoc,
                                        std::move(Ret));
  }

  concepts::NestedRequirement *
  RebuildNestedRequirement(StringRef InvalidConstraintEntity,
                           const ASTConstraintSatisfaction &Satisfaction) {
    return SemaRef.BuildNestedRequirement(InvalidConstraintEntity,
                                          Satisfaction);
  }

  concepts::NestedRequirement *RebuildNestedRequirement(Expr *Constraint) {
    return SemaRef.BuildNestedRequirement(Constraint);
  }

  /// \brief Build a new Objective-C boxed expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCBoxedExpr(SourceRange SR, Expr *ValueExpr) {
    return getSema().ObjC().BuildObjCBoxedExpr(SR, ValueExpr);
  }

  /// Build a new Objective-C array literal.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCArrayLiteral(SourceRange Range,
                                     Expr **Elements, unsigned NumElements) {
    return getSema().ObjC().BuildObjCArrayLiteral(
        Range, MultiExprArg(Elements, NumElements));
  }

  ExprResult RebuildObjCSubscriptRefExpr(SourceLocation RB,
                                         Expr *Base, Expr *Key,
                                         ObjCMethodDecl *getterMethod,
                                         ObjCMethodDecl *setterMethod) {
    return getSema().ObjC().BuildObjCSubscriptExpression(
        RB, Base, Key, getterMethod, setterMethod);
  }

  /// Build a new Objective-C dictionary literal.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCDictionaryLiteral(SourceRange Range,
                              MutableArrayRef<ObjCDictionaryElement> Elements) {
    return getSema().ObjC().BuildObjCDictionaryLiteral(Range, Elements);
  }

  /// Build a new Objective-C \@encode expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCEncodeExpr(SourceLocation AtLoc,
                                         TypeSourceInfo *EncodeTypeInfo,
                                         SourceLocation RParenLoc) {
    return SemaRef.ObjC().BuildObjCEncodeExpression(AtLoc, EncodeTypeInfo,
                                                    RParenLoc);
  }

  /// Build a new Objective-C class message.
  ExprResult RebuildObjCMessageExpr(TypeSourceInfo *ReceiverTypeInfo,
                                          Selector Sel,
                                          ArrayRef<SourceLocation> SelectorLocs,
                                          ObjCMethodDecl *Method,
                                          SourceLocation LBracLoc,
                                          MultiExprArg Args,
                                          SourceLocation RBracLoc) {
    return SemaRef.ObjC().BuildClassMessage(
        ReceiverTypeInfo, ReceiverTypeInfo->getType(),
        /*SuperLoc=*/SourceLocation(), Sel, Method, LBracLoc, SelectorLocs,
        RBracLoc, Args);
  }

  /// Build a new Objective-C instance message.
  ExprResult RebuildObjCMessageExpr(Expr *Receiver,
                                          Selector Sel,
                                          ArrayRef<SourceLocation> SelectorLocs,
                                          ObjCMethodDecl *Method,
                                          SourceLocation LBracLoc,
                                          MultiExprArg Args,
                                          SourceLocation RBracLoc) {
    return SemaRef.ObjC().BuildInstanceMessage(Receiver, Receiver->getType(),
                                               /*SuperLoc=*/SourceLocation(),
                                               Sel, Method, LBracLoc,
                                               SelectorLocs, RBracLoc, Args);
  }

  /// Build a new Objective-C instance/class message to 'super'.
  ExprResult RebuildObjCMessageExpr(SourceLocation SuperLoc,
                                    Selector Sel,
                                    ArrayRef<SourceLocation> SelectorLocs,
                                    QualType SuperType,
                                    ObjCMethodDecl *Method,
                                    SourceLocation LBracLoc,
                                    MultiExprArg Args,
                                    SourceLocation RBracLoc) {
    return Method->isInstanceMethod()
               ? SemaRef.ObjC().BuildInstanceMessage(
                     nullptr, SuperType, SuperLoc, Sel, Method, LBracLoc,
                     SelectorLocs, RBracLoc, Args)
               : SemaRef.ObjC().BuildClassMessage(nullptr, SuperType, SuperLoc,
                                                  Sel, Method, LBracLoc,
                                                  SelectorLocs, RBracLoc, Args);
  }

  /// Build a new Objective-C ivar reference expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCIvarRefExpr(Expr *BaseArg, ObjCIvarDecl *Ivar,
                                          SourceLocation IvarLoc,
                                          bool IsArrow, bool IsFreeIvar) {
    CXXScopeSpec SS;
    DeclarationNameInfo NameInfo(Ivar->getDeclName(), IvarLoc);
    ExprResult Result = getSema().BuildMemberReferenceExpr(
        BaseArg, BaseArg->getType(),
        /*FIXME:*/ IvarLoc, IsArrow, SS, SourceLocation(),
        /*FirstQualifierInScope=*/nullptr, NameInfo,
        /*TemplateArgs=*/nullptr,
        /*S=*/nullptr);
    if (IsFreeIvar && Result.isUsable())
      cast<ObjCIvarRefExpr>(Result.get())->setIsFreeIvar(IsFreeIvar);
    return Result;
  }

  /// Build a new Objective-C property reference expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCPropertyRefExpr(Expr *BaseArg,
                                        ObjCPropertyDecl *Property,
                                        SourceLocation PropertyLoc) {
    CXXScopeSpec SS;
    DeclarationNameInfo NameInfo(Property->getDeclName(), PropertyLoc);
    return getSema().BuildMemberReferenceExpr(BaseArg, BaseArg->getType(),
                                              /*FIXME:*/PropertyLoc,
                                              /*IsArrow=*/false,
                                              SS, SourceLocation(),
                                              /*FirstQualifierInScope=*/nullptr,
                                              NameInfo,
                                              /*TemplateArgs=*/nullptr,
                                              /*S=*/nullptr);
  }

  /// Build a new Objective-C property reference expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCPropertyRefExpr(Expr *Base, QualType T,
                                        ObjCMethodDecl *Getter,
                                        ObjCMethodDecl *Setter,
                                        SourceLocation PropertyLoc) {
    // Since these expressions can only be value-dependent, we do not
    // need to perform semantic analysis again.
    return Owned(
      new (getSema().Context) ObjCPropertyRefExpr(Getter, Setter, T,
                                                  VK_LValue, OK_ObjCProperty,
                                                  PropertyLoc, Base));
  }

  /// Build a new Objective-C "isa" expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildObjCIsaExpr(Expr *BaseArg, SourceLocation IsaLoc,
                                SourceLocation OpLoc, bool IsArrow) {
    CXXScopeSpec SS;
    DeclarationNameInfo NameInfo(&getSema().Context.Idents.get("isa"), IsaLoc);
    return getSema().BuildMemberReferenceExpr(BaseArg, BaseArg->getType(),
                                              OpLoc, IsArrow,
                                              SS, SourceLocation(),
                                              /*FirstQualifierInScope=*/nullptr,
                                              NameInfo,
                                              /*TemplateArgs=*/nullptr,
                                              /*S=*/nullptr);
  }

  /// Build a new shuffle vector expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildShuffleVectorExpr(SourceLocation BuiltinLoc,
                                      MultiExprArg SubExprs,
                                      SourceLocation RParenLoc) {
    // Find the declaration for __builtin_shufflevector
    const IdentifierInfo &Name
      = SemaRef.Context.Idents.get("__builtin_shufflevector");
    TranslationUnitDecl *TUDecl = SemaRef.Context.getTranslationUnitDecl();
    DeclContext::lookup_result Lookup = TUDecl->lookup(DeclarationName(&Name));
    assert(!Lookup.empty() && "No __builtin_shufflevector?");

    // Build a reference to the __builtin_shufflevector builtin
    FunctionDecl *Builtin = cast<FunctionDecl>(Lookup.front());
    Expr *Callee = new (SemaRef.Context)
        DeclRefExpr(SemaRef.Context, Builtin, false,
                    SemaRef.Context.BuiltinFnTy, VK_PRValue, BuiltinLoc);
    QualType CalleePtrTy = SemaRef.Context.getPointerType(Builtin->getType());
    Callee = SemaRef.ImpCastExprToType(Callee, CalleePtrTy,
                                       CK_BuiltinFnToFnPtr).get();

    // Build the CallExpr
    ExprResult TheCall = CallExpr::Create(
        SemaRef.Context, Callee, SubExprs, Builtin->getCallResultType(),
        Expr::getValueKindForType(Builtin->getReturnType()), RParenLoc,
        FPOptionsOverride());

    // Type-check the __builtin_shufflevector expression.
    return SemaRef.BuiltinShuffleVector(cast<CallExpr>(TheCall.get()));
  }

  /// Build a new convert vector expression.
  ExprResult RebuildConvertVectorExpr(SourceLocation BuiltinLoc,
                                      Expr *SrcExpr, TypeSourceInfo *DstTInfo,
                                      SourceLocation RParenLoc) {
    return SemaRef.ConvertVectorExpr(SrcExpr, DstTInfo, BuiltinLoc, RParenLoc);
  }

  /// Build a new template argument pack expansion.
  ///
  /// By default, performs semantic analysis to build a new pack expansion
  /// for a template argument. Subclasses may override this routine to provide
  /// different behavior.
  TemplateArgumentLoc
  RebuildPackExpansion(TemplateArgumentLoc Pattern, SourceLocation EllipsisLoc,
                       std::optional<unsigned> NumExpansions) {
    switch (Pattern.getArgument().getKind()) {
    case TemplateArgument::Expression: {
      ExprResult Result
        = getSema().CheckPackExpansion(Pattern.getSourceExpression(),
                                       EllipsisLoc, NumExpansions);
      if (Result.isInvalid())
        return TemplateArgumentLoc();

      return TemplateArgumentLoc(Result.get(), Result.get());
    }

    case TemplateArgument::Template:
      return TemplateArgumentLoc(
          SemaRef.Context,
          TemplateArgument(Pattern.getArgument().getAsTemplate(),
                           NumExpansions),
          Pattern.getTemplateQualifierLoc(), Pattern.getTemplateNameLoc(),
          EllipsisLoc);

    case TemplateArgument::Null:
    case TemplateArgument::Integral:
    case TemplateArgument::Declaration:
    case TemplateArgument::StructuralValue:
    case TemplateArgument::Pack:
    case TemplateArgument::TemplateExpansion:
    case TemplateArgument::NullPtr:
      llvm_unreachable("Pack expansion pattern has no parameter packs");

    case TemplateArgument::Type:
      if (TypeSourceInfo *Expansion
            = getSema().CheckPackExpansion(Pattern.getTypeSourceInfo(),
                                           EllipsisLoc,
                                           NumExpansions))
        return TemplateArgumentLoc(TemplateArgument(Expansion->getType()),
                                   Expansion);
      break;
    }

    return TemplateArgumentLoc();
  }

  /// Build a new expression pack expansion.
  ///
  /// By default, performs semantic analysis to build a new pack expansion
  /// for an expression. Subclasses may override this routine to provide
  /// different behavior.
  ExprResult RebuildPackExpansion(Expr *Pattern, SourceLocation EllipsisLoc,
                                  std::optional<unsigned> NumExpansions) {
    return getSema().CheckPackExpansion(Pattern, EllipsisLoc, NumExpansions);
  }

  /// Build a new C++1z fold-expression.
  ///
  /// By default, performs semantic analysis in order to build a new fold
  /// expression.
  ExprResult RebuildCXXFoldExpr(UnresolvedLookupExpr *ULE,
                                SourceLocation LParenLoc, Expr *LHS,
                                BinaryOperatorKind Operator,
                                SourceLocation EllipsisLoc, Expr *RHS,
                                SourceLocation RParenLoc,
                                std::optional<unsigned> NumExpansions) {
    return getSema().BuildCXXFoldExpr(ULE, LParenLoc, LHS, Operator,
                                      EllipsisLoc, RHS, RParenLoc,
                                      NumExpansions);
  }

  /// Build an empty C++1z fold-expression with the given operator.
  ///
  /// By default, produces the fallback value for the fold-expression, or
  /// produce an error if there is no fallback value.
  ExprResult RebuildEmptyCXXFoldExpr(SourceLocation EllipsisLoc,
                                     BinaryOperatorKind Operator) {
    return getSema().BuildEmptyCXXFoldExpr(EllipsisLoc, Operator);
  }

  /// Build a new atomic operation expression.
  ///
  /// By default, performs semantic analysis to build the new expression.
  /// Subclasses may override this routine to provide different behavior.
  ExprResult RebuildAtomicExpr(SourceLocation BuiltinLoc, MultiExprArg SubExprs,
                               AtomicExpr::AtomicOp Op,
                               SourceLocation RParenLoc) {
    // Use this for all of the locations, since we don't know the difference
    // between the call and the expr at this point.
    SourceRange Range{BuiltinLoc, RParenLoc};
    return getSema().BuildAtomicExpr(Range, Range, RParenLoc, SubExprs, Op,
                                     Sema::AtomicArgumentOrder::AST);
  }

  ExprResult RebuildRecoveryExpr(SourceLocation BeginLoc, SourceLocation EndLoc,
                                 ArrayRef<Expr *> SubExprs, QualType Type) {
    return getSema().CreateRecoveryExpr(BeginLoc, EndLoc, SubExprs, Type);
  }

  StmtResult RebuildOpenACCComputeConstruct(OpenACCDirectiveKind K,
                                            SourceLocation BeginLoc,
                                            SourceLocation DirLoc,
                                            SourceLocation EndLoc,
                                            ArrayRef<OpenACCClause *> Clauses,
                                            StmtResult StrBlock) {
    return getSema().OpenACC().ActOnEndStmtDirective(K, BeginLoc, DirLoc,
                                                     EndLoc, Clauses, StrBlock);
  }

  StmtResult RebuildOpenACCLoopConstruct(SourceLocation BeginLoc,
                                         SourceLocation DirLoc,
                                         SourceLocation EndLoc,
                                         ArrayRef<OpenACCClause *> Clauses,
                                         StmtResult Loop) {
    return getSema().OpenACC().ActOnEndStmtDirective(
        OpenACCDirectiveKind::Loop, BeginLoc, DirLoc, EndLoc, Clauses, Loop);
  }

private:
  TypeLoc TransformTypeInObjectScope(TypeLoc TL,
                                     QualType ObjectType,
                                     NamedDecl *FirstQualifierInScope,
                                     CXXScopeSpec &SS);

  TypeSourceInfo *TransformTypeInObjectScope(TypeSourceInfo *TSInfo,
                                             QualType ObjectType,
                                             NamedDecl *FirstQualifierInScope,
                                             CXXScopeSpec &SS);

  TypeSourceInfo *TransformTSIInObjectScope(TypeLoc TL, QualType ObjectType,
                                            NamedDecl *FirstQualifierInScope,
                                            CXXScopeSpec &SS);

  QualType TransformDependentNameType(TypeLocBuilder &TLB,
                                      DependentNameTypeLoc TL,
                                      bool DeducibleTSTContext);

  llvm::SmallVector<OpenACCClause *>
  TransformOpenACCClauseList(OpenACCDirectiveKind DirKind,
                             ArrayRef<const OpenACCClause *> OldClauses);

  OpenACCClause *
  TransformOpenACCClause(ArrayRef<const OpenACCClause *> ExistingClauses,
                         OpenACCDirectiveKind DirKind,
                         const OpenACCClause *OldClause);
};

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformStmt(Stmt *S, StmtDiscardKind SDK) {
  if (!S)
    return S;

  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass: break;

  // Transform individual statement nodes
  // Pass SDK into statements that can produce a value
#define STMT(Node, Parent)                                              \
  case Stmt::Node##Class: return getDerived().Transform##Node(cast<Node>(S));
#define VALUESTMT(Node, Parent)                                         \
  case Stmt::Node##Class:                                               \
    return getDerived().Transform##Node(cast<Node>(S), SDK);
#define ABSTRACT_STMT(Node)
#define EXPR(Node, Parent)
#include "clang/AST/StmtNodes.inc"

  // Transform expressions by calling TransformExpr.
#define STMT(Node, Parent)
#define ABSTRACT_STMT(Stmt)
#define EXPR(Node, Parent) case Stmt::Node##Class:
#include "clang/AST/StmtNodes.inc"
    {
      ExprResult E = getDerived().TransformExpr(cast<Expr>(S));

      if (SDK == SDK_StmtExprResult)
        E = getSema().ActOnStmtExprResult(E);
      return getSema().ActOnExprStmt(E, SDK == SDK_Discarded);
    }
  }

  return S;
}

template<typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPClause(OMPClause *S) {
  if (!S)
    return S;

  switch (S->getClauseKind()) {
  default: break;
  // Transform individual clause nodes
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  case Enum:                                                                   \
    return getDerived().Transform##Class(cast<Class>(S));
#include "llvm/Frontend/OpenMP/OMP.inc"
  }

  return S;
}


template<typename Derived>
ExprResult TreeTransform<Derived>::TransformExpr(Expr *E) {
  if (!E)
    return E;

  switch (E->getStmtClass()) {
    case Stmt::NoStmtClass: break;
#define STMT(Node, Parent) case Stmt::Node##Class: break;
#define ABSTRACT_STMT(Stmt)
#define EXPR(Node, Parent)                                              \
    case Stmt::Node##Class: return getDerived().Transform##Node(cast<Node>(E));
#include "clang/AST/StmtNodes.inc"
  }

  return E;
}

template<typename Derived>
ExprResult TreeTransform<Derived>::TransformInitializer(Expr *Init,
                                                        bool NotCopyInit) {
  // Initializers are instantiated like expressions, except that various outer
  // layers are stripped.
  if (!Init)
    return Init;

  if (auto *FE = dyn_cast<FullExpr>(Init))
    Init = FE->getSubExpr();

  if (auto *AIL = dyn_cast<ArrayInitLoopExpr>(Init)) {
    OpaqueValueExpr *OVE = AIL->getCommonExpr();
    Init = OVE->getSourceExpr();
  }

  if (MaterializeTemporaryExpr *MTE = dyn_cast<MaterializeTemporaryExpr>(Init))
    Init = MTE->getSubExpr();

  while (CXXBindTemporaryExpr *Binder = dyn_cast<CXXBindTemporaryExpr>(Init))
    Init = Binder->getSubExpr();

  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Init))
    Init = ICE->getSubExprAsWritten();

  if (CXXStdInitializerListExpr *ILE =
          dyn_cast<CXXStdInitializerListExpr>(Init))
    return TransformInitializer(ILE->getSubExpr(), NotCopyInit);

  // If this is copy-initialization, we only need to reconstruct
  // InitListExprs. Other forms of copy-initialization will be a no-op if
  // the initializer is already the right type.
  CXXConstructExpr *Construct = dyn_cast<CXXConstructExpr>(Init);
  if (!NotCopyInit && !(Construct && Construct->isListInitialization()))
    return getDerived().TransformExpr(Init);

  // Revert value-initialization back to empty parens.
  if (CXXScalarValueInitExpr *VIE = dyn_cast<CXXScalarValueInitExpr>(Init)) {
    SourceRange Parens = VIE->getSourceRange();
    return getDerived().RebuildParenListExpr(Parens.getBegin(), std::nullopt,
                                             Parens.getEnd());
  }

  // FIXME: We shouldn't build ImplicitValueInitExprs for direct-initialization.
  if (isa<ImplicitValueInitExpr>(Init))
    return getDerived().RebuildParenListExpr(SourceLocation(), std::nullopt,
                                             SourceLocation());

  // Revert initialization by constructor back to a parenthesized or braced list
  // of expressions. Any other form of initializer can just be reused directly.
  if (!Construct || isa<CXXTemporaryObjectExpr>(Construct))
    return getDerived().TransformExpr(Init);

  // If the initialization implicitly converted an initializer list to a
  // std::initializer_list object, unwrap the std::initializer_list too.
  if (Construct && Construct->isStdInitListInitialization())
    return TransformInitializer(Construct->getArg(0), NotCopyInit);

  // Enter a list-init context if this was list initialization.
  EnterExpressionEvaluationContext Context(
      getSema(), EnterExpressionEvaluationContext::InitList,
      Construct->isListInitialization());

  getSema().keepInLifetimeExtendingContext();
  SmallVector<Expr*, 8> NewArgs;
  bool ArgChanged = false;
  if (getDerived().TransformExprs(Construct->getArgs(), Construct->getNumArgs(),
                                  /*IsCall*/true, NewArgs, &ArgChanged))
    return ExprError();

  // If this was list initialization, revert to syntactic list form.
  if (Construct->isListInitialization())
    return getDerived().RebuildInitList(Construct->getBeginLoc(), NewArgs,
                                        Construct->getEndLoc());

  // Build a ParenListExpr to represent anything else.
  SourceRange Parens = Construct->getParenOrBraceRange();
  if (Parens.isInvalid()) {
    // This was a variable declaration's initialization for which no initializer
    // was specified.
    assert(NewArgs.empty() &&
           "no parens or braces but have direct init with arguments?");
    return ExprEmpty();
  }
  return getDerived().RebuildParenListExpr(Parens.getBegin(), NewArgs,
                                           Parens.getEnd());
}

template<typename Derived>
bool TreeTransform<Derived>::TransformExprs(Expr *const *Inputs,
                                            unsigned NumInputs,
                                            bool IsCall,
                                      SmallVectorImpl<Expr *> &Outputs,
                                            bool *ArgChanged) {
  for (unsigned I = 0; I != NumInputs; ++I) {
    // If requested, drop call arguments that need to be dropped.
    if (IsCall && getDerived().DropCallArgument(Inputs[I])) {
      if (ArgChanged)
        *ArgChanged = true;

      break;
    }

    if (PackExpansionExpr *Expansion = dyn_cast<PackExpansionExpr>(Inputs[I])) {
      Expr *Pattern = Expansion->getPattern();

      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      getSema().collectUnexpandedParameterPacks(Pattern, Unexpanded);
      assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

      // Determine whether the set of unexpanded parameter packs can and should
      // be expanded.
      bool Expand = true;
      bool RetainExpansion = false;
      std::optional<unsigned> OrigNumExpansions = Expansion->getNumExpansions();
      std::optional<unsigned> NumExpansions = OrigNumExpansions;
      if (getDerived().TryExpandParameterPacks(Expansion->getEllipsisLoc(),
                                               Pattern->getSourceRange(),
                                               Unexpanded,
                                               Expand, RetainExpansion,
                                               NumExpansions))
        return true;

      if (!Expand) {
        // The transform has determined that we should perform a simple
        // transformation on the pack expansion, producing another pack
        // expansion.
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
        ExprResult OutPattern = getDerived().TransformExpr(Pattern);
        if (OutPattern.isInvalid())
          return true;

        ExprResult Out = getDerived().RebuildPackExpansion(OutPattern.get(),
                                                Expansion->getEllipsisLoc(),
                                                           NumExpansions);
        if (Out.isInvalid())
          return true;

        if (ArgChanged)
          *ArgChanged = true;
        Outputs.push_back(Out.get());
        continue;
      }

      // Record right away that the argument was changed.  This needs
      // to happen even if the array expands to nothing.
      if (ArgChanged) *ArgChanged = true;

      // The transform has determined that we should perform an elementwise
      // expansion of the pattern. Do so.
      for (unsigned I = 0; I != *NumExpansions; ++I) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
        ExprResult Out = getDerived().TransformExpr(Pattern);
        if (Out.isInvalid())
          return true;

        if (Out.get()->containsUnexpandedParameterPack()) {
          Out = getDerived().RebuildPackExpansion(
              Out.get(), Expansion->getEllipsisLoc(), OrigNumExpansions);
          if (Out.isInvalid())
            return true;
        }

        Outputs.push_back(Out.get());
      }

      // If we're supposed to retain a pack expansion, do so by temporarily
      // forgetting the partially-substituted parameter pack.
      if (RetainExpansion) {
        ForgetPartiallySubstitutedPackRAII Forget(getDerived());

        ExprResult Out = getDerived().TransformExpr(Pattern);
        if (Out.isInvalid())
          return true;

        Out = getDerived().RebuildPackExpansion(
            Out.get(), Expansion->getEllipsisLoc(), OrigNumExpansions);
        if (Out.isInvalid())
          return true;

        Outputs.push_back(Out.get());
      }

      continue;
    }

    ExprResult Result =
      IsCall ? getDerived().TransformInitializer(Inputs[I], /*DirectInit*/false)
             : getDerived().TransformExpr(Inputs[I]);
    if (Result.isInvalid())
      return true;

    if (Result.get() != Inputs[I] && ArgChanged)
      *ArgChanged = true;

    Outputs.push_back(Result.get());
  }

  return false;
}

template <typename Derived>
Sema::ConditionResult TreeTransform<Derived>::TransformCondition(
    SourceLocation Loc, VarDecl *Var, Expr *Expr, Sema::ConditionKind Kind) {
  if (Var) {
    VarDecl *ConditionVar = cast_or_null<VarDecl>(
        getDerived().TransformDefinition(Var->getLocation(), Var));

    if (!ConditionVar)
      return Sema::ConditionError();

    return getSema().ActOnConditionVariable(ConditionVar, Loc, Kind);
  }

  if (Expr) {
    ExprResult CondExpr = getDerived().TransformExpr(Expr);

    if (CondExpr.isInvalid())
      return Sema::ConditionError();

    return getSema().ActOnCondition(nullptr, Loc, CondExpr.get(), Kind,
                                    /*MissingOK=*/true);
  }

  return Sema::ConditionResult();
}

template <typename Derived>
NestedNameSpecifierLoc TreeTransform<Derived>::TransformNestedNameSpecifierLoc(
    NestedNameSpecifierLoc NNS, QualType ObjectType,
    NamedDecl *FirstQualifierInScope) {
  SmallVector<NestedNameSpecifierLoc, 4> Qualifiers;

  auto insertNNS = [&Qualifiers](NestedNameSpecifierLoc NNS) {
    for (NestedNameSpecifierLoc Qualifier = NNS; Qualifier;
         Qualifier = Qualifier.getPrefix())
      Qualifiers.push_back(Qualifier);
  };
  insertNNS(NNS);

  CXXScopeSpec SS;
  while (!Qualifiers.empty()) {
    NestedNameSpecifierLoc Q = Qualifiers.pop_back_val();
    NestedNameSpecifier *QNNS = Q.getNestedNameSpecifier();

    switch (QNNS->getKind()) {
    case NestedNameSpecifier::Identifier: {
      Sema::NestedNameSpecInfo IdInfo(QNNS->getAsIdentifier(),
                                      Q.getLocalBeginLoc(), Q.getLocalEndLoc(),
                                      ObjectType);
      if (SemaRef.BuildCXXNestedNameSpecifier(/*Scope=*/nullptr, IdInfo, false,
                                              SS, FirstQualifierInScope, false))
        return NestedNameSpecifierLoc();
      break;
    }

    case NestedNameSpecifier::Namespace: {
      NamespaceDecl *NS =
          cast_or_null<NamespaceDecl>(getDerived().TransformDecl(
              Q.getLocalBeginLoc(), QNNS->getAsNamespace()));
      SS.Extend(SemaRef.Context, NS, Q.getLocalBeginLoc(), Q.getLocalEndLoc());
      break;
    }

    case NestedNameSpecifier::NamespaceAlias: {
      NamespaceAliasDecl *Alias =
          cast_or_null<NamespaceAliasDecl>(getDerived().TransformDecl(
              Q.getLocalBeginLoc(), QNNS->getAsNamespaceAlias()));
      SS.Extend(SemaRef.Context, Alias, Q.getLocalBeginLoc(),
                Q.getLocalEndLoc());
      break;
    }

    case NestedNameSpecifier::Global:
      // There is no meaningful transformation that one could perform on the
      // global scope.
      SS.MakeGlobal(SemaRef.Context, Q.getBeginLoc());
      break;

    case NestedNameSpecifier::Super: {
      CXXRecordDecl *RD =
          cast_or_null<CXXRecordDecl>(getDerived().TransformDecl(
              SourceLocation(), QNNS->getAsRecordDecl()));
      SS.MakeSuper(SemaRef.Context, RD, Q.getBeginLoc(), Q.getEndLoc());
      break;
    }

    case NestedNameSpecifier::TypeSpecWithTemplate:
    case NestedNameSpecifier::TypeSpec: {
      TypeLoc TL = TransformTypeInObjectScope(Q.getTypeLoc(), ObjectType,
                                              FirstQualifierInScope, SS);

      if (!TL)
        return NestedNameSpecifierLoc();

      QualType T = TL.getType();
      if (T->isDependentType() || T->isRecordType() ||
          (SemaRef.getLangOpts().CPlusPlus11 && T->isEnumeralType())) {
        if (T->isEnumeralType())
          SemaRef.Diag(TL.getBeginLoc(),
                       diag::warn_cxx98_compat_enum_nested_name_spec);

        if (const auto ETL = TL.getAs<ElaboratedTypeLoc>()) {
          SS.Adopt(ETL.getQualifierLoc());
          TL = ETL.getNamedTypeLoc();
        }

        SS.Extend(SemaRef.Context, TL.getTemplateKeywordLoc(), TL,
                  Q.getLocalEndLoc());
        break;
      }
      // If the nested-name-specifier is an invalid type def, don't emit an
      // error because a previous error should have already been emitted.
      TypedefTypeLoc TTL = TL.getAsAdjusted<TypedefTypeLoc>();
      if (!TTL || !TTL.getTypedefNameDecl()->isInvalidDecl()) {
        SemaRef.Diag(TL.getBeginLoc(), diag::err_nested_name_spec_non_tag)
            << T << SS.getRange();
      }
      return NestedNameSpecifierLoc();
    }
    }

    // The qualifier-in-scope and object type only apply to the leftmost entity.
    FirstQualifierInScope = nullptr;
    ObjectType = QualType();
  }

  // Don't rebuild the nested-name-specifier if we don't have to.
  if (SS.getScopeRep() == NNS.getNestedNameSpecifier() &&
      !getDerived().AlwaysRebuild())
    return NNS;

  // If we can re-use the source-location data from the original
  // nested-name-specifier, do so.
  if (SS.location_size() == NNS.getDataLength() &&
      memcmp(SS.location_data(), NNS.getOpaqueData(), SS.location_size()) == 0)
    return NestedNameSpecifierLoc(SS.getScopeRep(), NNS.getOpaqueData());

  // Allocate new nested-name-specifier location information.
  return SS.getWithLocInContext(SemaRef.Context);
}

template<typename Derived>
DeclarationNameInfo
TreeTransform<Derived>
::TransformDeclarationNameInfo(const DeclarationNameInfo &NameInfo) {
  DeclarationName Name = NameInfo.getName();
  if (!Name)
    return DeclarationNameInfo();

  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXUsingDirective:
    return NameInfo;

  case DeclarationName::CXXDeductionGuideName: {
    TemplateDecl *OldTemplate = Name.getCXXDeductionGuideTemplate();
    TemplateDecl *NewTemplate = cast_or_null<TemplateDecl>(
        getDerived().TransformDecl(NameInfo.getLoc(), OldTemplate));
    if (!NewTemplate)
      return DeclarationNameInfo();

    DeclarationNameInfo NewNameInfo(NameInfo);
    NewNameInfo.setName(
        SemaRef.Context.DeclarationNames.getCXXDeductionGuideName(NewTemplate));
    return NewNameInfo;
  }

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName: {
    TypeSourceInfo *NewTInfo;
    CanQualType NewCanTy;
    if (TypeSourceInfo *OldTInfo = NameInfo.getNamedTypeInfo()) {
      NewTInfo = getDerived().TransformType(OldTInfo);
      if (!NewTInfo)
        return DeclarationNameInfo();
      NewCanTy = SemaRef.Context.getCanonicalType(NewTInfo->getType());
    }
    else {
      NewTInfo = nullptr;
      TemporaryBase Rebase(*this, NameInfo.getLoc(), Name);
      QualType NewT = getDerived().TransformType(Name.getCXXNameType());
      if (NewT.isNull())
        return DeclarationNameInfo();
      NewCanTy = SemaRef.Context.getCanonicalType(NewT);
    }

    DeclarationName NewName
      = SemaRef.Context.DeclarationNames.getCXXSpecialName(Name.getNameKind(),
                                                           NewCanTy);
    DeclarationNameInfo NewNameInfo(NameInfo);
    NewNameInfo.setName(NewName);
    NewNameInfo.setNamedTypeInfo(NewTInfo);
    return NewNameInfo;
  }
  }

  llvm_unreachable("Unknown name kind.");
}

template<typename Derived>
TemplateName
TreeTransform<Derived>::TransformTemplateName(CXXScopeSpec &SS,
                                              TemplateName Name,
                                              SourceLocation NameLoc,
                                              QualType ObjectType,
                                              NamedDecl *FirstQualifierInScope,
                                              bool AllowInjectedClassName) {
  if (QualifiedTemplateName *QTN = Name.getAsQualifiedTemplateName()) {
    TemplateDecl *Template = QTN->getUnderlyingTemplate().getAsTemplateDecl();
    assert(Template && "qualified template name must refer to a template");

    TemplateDecl *TransTemplate
      = cast_or_null<TemplateDecl>(getDerived().TransformDecl(NameLoc,
                                                              Template));
    if (!TransTemplate)
      return TemplateName();

    if (!getDerived().AlwaysRebuild() &&
        SS.getScopeRep() == QTN->getQualifier() &&
        TransTemplate == Template)
      return Name;

    return getDerived().RebuildTemplateName(SS, QTN->hasTemplateKeyword(),
                                            TransTemplate);
  }

  if (DependentTemplateName *DTN = Name.getAsDependentTemplateName()) {
    if (SS.getScopeRep()) {
      // These apply to the scope specifier, not the template.
      ObjectType = QualType();
      FirstQualifierInScope = nullptr;
    }

    if (!getDerived().AlwaysRebuild() &&
        SS.getScopeRep() == DTN->getQualifier() &&
        ObjectType.isNull())
      return Name;

    // FIXME: Preserve the location of the "template" keyword.
    SourceLocation TemplateKWLoc = NameLoc;

    if (DTN->isIdentifier()) {
      return getDerived().RebuildTemplateName(SS,
                                              TemplateKWLoc,
                                              *DTN->getIdentifier(),
                                              NameLoc,
                                              ObjectType,
                                              FirstQualifierInScope,
                                              AllowInjectedClassName);
    }

    return getDerived().RebuildTemplateName(SS, TemplateKWLoc,
                                            DTN->getOperator(), NameLoc,
                                            ObjectType, AllowInjectedClassName);
  }

  // FIXME: Try to preserve more of the TemplateName.
  if (TemplateDecl *Template = Name.getAsTemplateDecl()) {
    TemplateDecl *TransTemplate
      = cast_or_null<TemplateDecl>(getDerived().TransformDecl(NameLoc,
                                                              Template));
    if (!TransTemplate)
      return TemplateName();

    return getDerived().RebuildTemplateName(SS, /*TemplateKeyword=*/false,
                                            TransTemplate);
  }

  if (SubstTemplateTemplateParmPackStorage *SubstPack
      = Name.getAsSubstTemplateTemplateParmPack()) {
    return getDerived().RebuildTemplateName(
        SubstPack->getArgumentPack(), SubstPack->getAssociatedDecl(),
        SubstPack->getIndex(), SubstPack->getFinal());
  }

  // These should be getting filtered out before they reach the AST.
  llvm_unreachable("overloaded function decl survived to here");
}

template<typename Derived>
void TreeTransform<Derived>::InventTemplateArgumentLoc(
                                         const TemplateArgument &Arg,
                                         TemplateArgumentLoc &Output) {
  Output = getSema().getTrivialTemplateArgumentLoc(
      Arg, QualType(), getDerived().getBaseLocation());
}

template <typename Derived>
bool TreeTransform<Derived>::TransformTemplateArgument(
    const TemplateArgumentLoc &Input, TemplateArgumentLoc &Output,
    bool Uneval) {
  const TemplateArgument &Arg = Input.getArgument();
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::Pack:
    llvm_unreachable("Unexpected TemplateArgument");

  case TemplateArgument::Integral:
  case TemplateArgument::NullPtr:
  case TemplateArgument::Declaration:
  case TemplateArgument::StructuralValue: {
    // Transform a resolved template argument straight to a resolved template
    // argument. We get here when substituting into an already-substituted
    // template type argument during concept satisfaction checking.
    QualType T = Arg.getNonTypeTemplateArgumentType();
    QualType NewT = getDerived().TransformType(T);
    if (NewT.isNull())
      return true;

    ValueDecl *D = Arg.getKind() == TemplateArgument::Declaration
                       ? Arg.getAsDecl()
                       : nullptr;
    ValueDecl *NewD = D ? cast_or_null<ValueDecl>(getDerived().TransformDecl(
                              getDerived().getBaseLocation(), D))
                        : nullptr;
    if (D && !NewD)
      return true;

    if (NewT == T && D == NewD)
      Output = Input;
    else if (Arg.getKind() == TemplateArgument::Integral)
      Output = TemplateArgumentLoc(
          TemplateArgument(getSema().Context, Arg.getAsIntegral(), NewT),
          TemplateArgumentLocInfo());
    else if (Arg.getKind() == TemplateArgument::NullPtr)
      Output = TemplateArgumentLoc(TemplateArgument(NewT, /*IsNullPtr=*/true),
                                   TemplateArgumentLocInfo());
    else if (Arg.getKind() == TemplateArgument::Declaration)
      Output = TemplateArgumentLoc(TemplateArgument(NewD, NewT),
                                   TemplateArgumentLocInfo());
    else if (Arg.getKind() == TemplateArgument::StructuralValue)
      Output = TemplateArgumentLoc(
          TemplateArgument(getSema().Context, NewT, Arg.getAsStructuralValue()),
          TemplateArgumentLocInfo());
    else
      llvm_unreachable("unexpected template argument kind");

    return false;
  }

  case TemplateArgument::Type: {
    TypeSourceInfo *DI = Input.getTypeSourceInfo();
    if (!DI)
      DI = InventTypeSourceInfo(Input.getArgument().getAsType());

    DI = getDerived().TransformType(DI);
    if (!DI)
      return true;

    Output = TemplateArgumentLoc(TemplateArgument(DI->getType()), DI);
    return false;
  }

  case TemplateArgument::Template: {
    NestedNameSpecifierLoc QualifierLoc = Input.getTemplateQualifierLoc();
    if (QualifierLoc) {
      QualifierLoc = getDerived().TransformNestedNameSpecifierLoc(QualifierLoc);
      if (!QualifierLoc)
        return true;
    }

    CXXScopeSpec SS;
    SS.Adopt(QualifierLoc);
    TemplateName Template = getDerived().TransformTemplateName(
        SS, Arg.getAsTemplate(), Input.getTemplateNameLoc());
    if (Template.isNull())
      return true;

    Output = TemplateArgumentLoc(SemaRef.Context, TemplateArgument(Template),
                                 QualifierLoc, Input.getTemplateNameLoc());
    return false;
  }

  case TemplateArgument::TemplateExpansion:
    llvm_unreachable("Caller should expand pack expansions");

  case TemplateArgument::Expression: {
    // Template argument expressions are constant expressions.
    EnterExpressionEvaluationContext Unevaluated(
        getSema(),
        Uneval ? Sema::ExpressionEvaluationContext::Unevaluated
               : Sema::ExpressionEvaluationContext::ConstantEvaluated,
        Sema::ReuseLambdaContextDecl, /*ExprContext=*/
        Sema::ExpressionEvaluationContextRecord::EK_TemplateArgument);

    Expr *InputExpr = Input.getSourceExpression();
    if (!InputExpr)
      InputExpr = Input.getArgument().getAsExpr();

    ExprResult E = getDerived().TransformExpr(InputExpr);
    E = SemaRef.ActOnConstantExpression(E);
    if (E.isInvalid())
      return true;
    Output = TemplateArgumentLoc(TemplateArgument(E.get()), E.get());
    return false;
  }
  }

  // Work around bogus GCC warning
  return true;
}

/// Iterator adaptor that invents template argument location information
/// for each of the template arguments in its underlying iterator.
template<typename Derived, typename InputIterator>
class TemplateArgumentLocInventIterator {
  TreeTransform<Derived> &Self;
  InputIterator Iter;

public:
  typedef TemplateArgumentLoc value_type;
  typedef TemplateArgumentLoc reference;
  typedef typename std::iterator_traits<InputIterator>::difference_type
    difference_type;
  typedef std::input_iterator_tag iterator_category;

  class pointer {
    TemplateArgumentLoc Arg;

  public:
    explicit pointer(TemplateArgumentLoc Arg) : Arg(Arg) { }

    const TemplateArgumentLoc *operator->() const { return &Arg; }
  };

  explicit TemplateArgumentLocInventIterator(TreeTransform<Derived> &Self,
                                             InputIterator Iter)
    : Self(Self), Iter(Iter) { }

  TemplateArgumentLocInventIterator &operator++() {
    ++Iter;
    return *this;
  }

  TemplateArgumentLocInventIterator operator++(int) {
    TemplateArgumentLocInventIterator Old(*this);
    ++(*this);
    return Old;
  }

  reference operator*() const {
    TemplateArgumentLoc Result;
    Self.InventTemplateArgumentLoc(*Iter, Result);
    return Result;
  }

  pointer operator->() const { return pointer(**this); }

  friend bool operator==(const TemplateArgumentLocInventIterator &X,
                         const TemplateArgumentLocInventIterator &Y) {
    return X.Iter == Y.Iter;
  }

  friend bool operator!=(const TemplateArgumentLocInventIterator &X,
                         const TemplateArgumentLocInventIterator &Y) {
    return X.Iter != Y.Iter;
  }
};

template<typename Derived>
template<typename InputIterator>
bool TreeTransform<Derived>::TransformTemplateArguments(
    InputIterator First, InputIterator Last, TemplateArgumentListInfo &Outputs,
    bool Uneval) {
  for (; First != Last; ++First) {
    TemplateArgumentLoc Out;
    TemplateArgumentLoc In = *First;

    if (In.getArgument().getKind() == TemplateArgument::Pack) {
      // Unpack argument packs, which we translate them into separate
      // arguments.
      // FIXME: We could do much better if we could guarantee that the
      // TemplateArgumentLocInfo for the pack expansion would be usable for
      // all of the template arguments in the argument pack.
      typedef TemplateArgumentLocInventIterator<Derived,
                                                TemplateArgument::pack_iterator>
        PackLocIterator;
      if (TransformTemplateArguments(PackLocIterator(*this,
                                                 In.getArgument().pack_begin()),
                                     PackLocIterator(*this,
                                                   In.getArgument().pack_end()),
                                     Outputs, Uneval))
        return true;

      continue;
    }

    if (In.getArgument().isPackExpansion()) {
      // We have a pack expansion, for which we will be substituting into
      // the pattern.
      SourceLocation Ellipsis;
      std::optional<unsigned> OrigNumExpansions;
      TemplateArgumentLoc Pattern
        = getSema().getTemplateArgumentPackExpansionPattern(
              In, Ellipsis, OrigNumExpansions);

      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      getSema().collectUnexpandedParameterPacks(Pattern, Unexpanded);
      assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

      // Determine whether the set of unexpanded parameter packs can and should
      // be expanded.
      bool Expand = true;
      bool RetainExpansion = false;
      std::optional<unsigned> NumExpansions = OrigNumExpansions;
      if (getDerived().TryExpandParameterPacks(Ellipsis,
                                               Pattern.getSourceRange(),
                                               Unexpanded,
                                               Expand,
                                               RetainExpansion,
                                               NumExpansions))
        return true;

      if (!Expand) {
        // The transform has determined that we should perform a simple
        // transformation on the pack expansion, producing another pack
        // expansion.
        TemplateArgumentLoc OutPattern;
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
        if (getDerived().TransformTemplateArgument(Pattern, OutPattern, Uneval))
          return true;

        Out = getDerived().RebuildPackExpansion(OutPattern, Ellipsis,
                                                NumExpansions);
        if (Out.getArgument().isNull())
          return true;

        Outputs.addArgument(Out);
        continue;
      }

      // The transform has determined that we should perform an elementwise
      // expansion of the pattern. Do so.
      for (unsigned I = 0; I != *NumExpansions; ++I) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);

        if (getDerived().TransformTemplateArgument(Pattern, Out, Uneval))
          return true;

        if (Out.getArgument().containsUnexpandedParameterPack()) {
          Out = getDerived().RebuildPackExpansion(Out, Ellipsis,
                                                  OrigNumExpansions);
          if (Out.getArgument().isNull())
            return true;
        }

        Outputs.addArgument(Out);
      }

      // If we're supposed to retain a pack expansion, do so by temporarily
      // forgetting the partially-substituted parameter pack.
      if (RetainExpansion) {
        ForgetPartiallySubstitutedPackRAII Forget(getDerived());

        if (getDerived().TransformTemplateArgument(Pattern, Out, Uneval))
          return true;

        Out = getDerived().RebuildPackExpansion(Out, Ellipsis,
                                                OrigNumExpansions);
        if (Out.getArgument().isNull())
          return true;

        Outputs.addArgument(Out);
      }

      continue;
    }

    // The simple case:
    if (getDerived().TransformTemplateArgument(In, Out, Uneval))
      return true;

    Outputs.addArgument(Out);
  }

  return false;

}

//===----------------------------------------------------------------------===//
// Type transformation
//===----------------------------------------------------------------------===//

template<typename Derived>
QualType TreeTransform<Derived>::TransformType(QualType T) {
  if (getDerived().AlreadyTransformed(T))
    return T;

  // Temporary workaround.  All of these transformations should
  // eventually turn into transformations on TypeLocs.
  TypeSourceInfo *DI = getSema().Context.getTrivialTypeSourceInfo(T,
                                                getDerived().getBaseLocation());

  TypeSourceInfo *NewDI = getDerived().TransformType(DI);

  if (!NewDI)
    return QualType();

  return NewDI->getType();
}

template<typename Derived>
TypeSourceInfo *TreeTransform<Derived>::TransformType(TypeSourceInfo *DI) {
  // Refine the base location to the type's location.
  TemporaryBase Rebase(*this, DI->getTypeLoc().getBeginLoc(),
                       getDerived().getBaseEntity());
  if (getDerived().AlreadyTransformed(DI->getType()))
    return DI;

  TypeLocBuilder TLB;

  TypeLoc TL = DI->getTypeLoc();
  TLB.reserve(TL.getFullDataSize());

  QualType Result = getDerived().TransformType(TLB, TL);
  if (Result.isNull())
    return nullptr;

  return TLB.getTypeSourceInfo(SemaRef.Context, Result);
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformType(TypeLocBuilder &TLB, TypeLoc T) {
  switch (T.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS:                                                         \
    return getDerived().Transform##CLASS##Type(TLB,                            \
                                               T.castAs<CLASS##TypeLoc>());
#include "clang/AST/TypeLocNodes.def"
  }

  llvm_unreachable("unhandled type loc!");
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformTypeWithDeducedTST(QualType T) {
  if (!isa<DependentNameType>(T))
    return TransformType(T);

  if (getDerived().AlreadyTransformed(T))
    return T;
  TypeSourceInfo *DI = getSema().Context.getTrivialTypeSourceInfo(T,
                                                getDerived().getBaseLocation());
  TypeSourceInfo *NewDI = getDerived().TransformTypeWithDeducedTST(DI);
  return NewDI ? NewDI->getType() : QualType();
}

template<typename Derived>
TypeSourceInfo *
TreeTransform<Derived>::TransformTypeWithDeducedTST(TypeSourceInfo *DI) {
  if (!isa<DependentNameType>(DI->getType()))
    return TransformType(DI);

  // Refine the base location to the type's location.
  TemporaryBase Rebase(*this, DI->getTypeLoc().getBeginLoc(),
                       getDerived().getBaseEntity());
  if (getDerived().AlreadyTransformed(DI->getType()))
    return DI;

  TypeLocBuilder TLB;

  TypeLoc TL = DI->getTypeLoc();
  TLB.reserve(TL.getFullDataSize());

  auto QTL = TL.getAs<QualifiedTypeLoc>();
  if (QTL)
    TL = QTL.getUnqualifiedLoc();

  auto DNTL = TL.castAs<DependentNameTypeLoc>();

  QualType Result = getDerived().TransformDependentNameType(
      TLB, DNTL, /*DeducedTSTContext*/true);
  if (Result.isNull())
    return nullptr;

  if (QTL) {
    Result = getDerived().RebuildQualifiedType(Result, QTL);
    if (Result.isNull())
      return nullptr;
    TLB.TypeWasModifiedSafely(Result);
  }

  return TLB.getTypeSourceInfo(SemaRef.Context, Result);
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformQualifiedType(TypeLocBuilder &TLB,
                                               QualifiedTypeLoc T) {
  QualType Result;
  TypeLoc UnqualTL = T.getUnqualifiedLoc();
  auto SuppressObjCLifetime =
      T.getType().getLocalQualifiers().hasObjCLifetime();
  if (auto TTP = UnqualTL.getAs<TemplateTypeParmTypeLoc>()) {
    Result = getDerived().TransformTemplateTypeParmType(TLB, TTP,
                                                        SuppressObjCLifetime);
  } else if (auto STTP = UnqualTL.getAs<SubstTemplateTypeParmPackTypeLoc>()) {
    Result = getDerived().TransformSubstTemplateTypeParmPackType(
        TLB, STTP, SuppressObjCLifetime);
  } else {
    Result = getDerived().TransformType(TLB, UnqualTL);
  }

  if (Result.isNull())
    return QualType();

  Result = getDerived().RebuildQualifiedType(Result, T);

  if (Result.isNull())
    return QualType();

  // RebuildQualifiedType might have updated the type, but not in a way
  // that invalidates the TypeLoc. (There's no location information for
  // qualifiers.)
  TLB.TypeWasModifiedSafely(Result);

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildQualifiedType(QualType T,
                                                      QualifiedTypeLoc TL) {

  SourceLocation Loc = TL.getBeginLoc();
  Qualifiers Quals = TL.getType().getLocalQualifiers();

  if ((T.getAddressSpace() != LangAS::Default &&
       Quals.getAddressSpace() != LangAS::Default) &&
      T.getAddressSpace() != Quals.getAddressSpace()) {
    SemaRef.Diag(Loc, diag::err_address_space_mismatch_templ_inst)
        << TL.getType() << T;
    return QualType();
  }

  // C++ [dcl.fct]p7:
  //   [When] adding cv-qualifications on top of the function type [...] the
  //   cv-qualifiers are ignored.
  if (T->isFunctionType()) {
    T = SemaRef.getASTContext().getAddrSpaceQualType(T,
                                                     Quals.getAddressSpace());
    return T;
  }

  // C++ [dcl.ref]p1:
  //   when the cv-qualifiers are introduced through the use of a typedef-name
  //   or decltype-specifier [...] the cv-qualifiers are ignored.
  // Note that [dcl.ref]p1 lists all cases in which cv-qualifiers can be
  // applied to a reference type.
  if (T->isReferenceType()) {
    // The only qualifier that applies to a reference type is restrict.
    if (!Quals.hasRestrict())
      return T;
    Quals = Qualifiers::fromCVRMask(Qualifiers::Restrict);
  }

  // Suppress Objective-C lifetime qualifiers if they don't make sense for the
  // resulting type.
  if (Quals.hasObjCLifetime()) {
    if (!T->isObjCLifetimeType() && !T->isDependentType())
      Quals.removeObjCLifetime();
    else if (T.getObjCLifetime()) {
      // Objective-C ARC:
      //   A lifetime qualifier applied to a substituted template parameter
      //   overrides the lifetime qualifier from the template argument.
      const AutoType *AutoTy;
      if ((AutoTy = dyn_cast<AutoType>(T)) && AutoTy->isDeduced()) {
        // 'auto' types behave the same way as template parameters.
        QualType Deduced = AutoTy->getDeducedType();
        Qualifiers Qs = Deduced.getQualifiers();
        Qs.removeObjCLifetime();
        Deduced =
            SemaRef.Context.getQualifiedType(Deduced.getUnqualifiedType(), Qs);
        T = SemaRef.Context.getAutoType(Deduced, AutoTy->getKeyword(),
                                        AutoTy->isDependentType(),
                                        /*isPack=*/false,
                                        AutoTy->getTypeConstraintConcept(),
                                        AutoTy->getTypeConstraintArguments());
      } else {
        // Otherwise, complain about the addition of a qualifier to an
        // already-qualified type.
        // FIXME: Why is this check not in Sema::BuildQualifiedType?
        SemaRef.Diag(Loc, diag::err_attr_objc_ownership_redundant) << T;
        Quals.removeObjCLifetime();
      }
    }
  }

  return SemaRef.BuildQualifiedType(T, Loc, Quals);
}

template<typename Derived>
TypeLoc
TreeTransform<Derived>::TransformTypeInObjectScope(TypeLoc TL,
                                                   QualType ObjectType,
                                                   NamedDecl *UnqualLookup,
                                                   CXXScopeSpec &SS) {
  if (getDerived().AlreadyTransformed(TL.getType()))
    return TL;

  TypeSourceInfo *TSI =
      TransformTSIInObjectScope(TL, ObjectType, UnqualLookup, SS);
  if (TSI)
    return TSI->getTypeLoc();
  return TypeLoc();
}

template<typename Derived>
TypeSourceInfo *
TreeTransform<Derived>::TransformTypeInObjectScope(TypeSourceInfo *TSInfo,
                                                   QualType ObjectType,
                                                   NamedDecl *UnqualLookup,
                                                   CXXScopeSpec &SS) {
  if (getDerived().AlreadyTransformed(TSInfo->getType()))
    return TSInfo;

  return TransformTSIInObjectScope(TSInfo->getTypeLoc(), ObjectType,
                                   UnqualLookup, SS);
}

template <typename Derived>
TypeSourceInfo *TreeTransform<Derived>::TransformTSIInObjectScope(
    TypeLoc TL, QualType ObjectType, NamedDecl *UnqualLookup,
    CXXScopeSpec &SS) {
  QualType T = TL.getType();
  assert(!getDerived().AlreadyTransformed(T));

  TypeLocBuilder TLB;
  QualType Result;

  if (isa<TemplateSpecializationType>(T)) {
    TemplateSpecializationTypeLoc SpecTL =
        TL.castAs<TemplateSpecializationTypeLoc>();

    TemplateName Template = getDerived().TransformTemplateName(
        SS, SpecTL.getTypePtr()->getTemplateName(), SpecTL.getTemplateNameLoc(),
        ObjectType, UnqualLookup, /*AllowInjectedClassName*/true);
    if (Template.isNull())
      return nullptr;

    Result = getDerived().TransformTemplateSpecializationType(TLB, SpecTL,
                                                              Template);
  } else if (isa<DependentTemplateSpecializationType>(T)) {
    DependentTemplateSpecializationTypeLoc SpecTL =
        TL.castAs<DependentTemplateSpecializationTypeLoc>();

    TemplateName Template
      = getDerived().RebuildTemplateName(SS,
                                         SpecTL.getTemplateKeywordLoc(),
                                         *SpecTL.getTypePtr()->getIdentifier(),
                                         SpecTL.getTemplateNameLoc(),
                                         ObjectType, UnqualLookup,
                                         /*AllowInjectedClassName*/true);
    if (Template.isNull())
      return nullptr;

    Result = getDerived().TransformDependentTemplateSpecializationType(TLB,
                                                                       SpecTL,
                                                                       Template,
                                                                       SS);
  } else {
    // Nothing special needs to be done for these.
    Result = getDerived().TransformType(TLB, TL);
  }

  if (Result.isNull())
    return nullptr;

  return TLB.getTypeSourceInfo(SemaRef.Context, Result);
}

template <class TyLoc> static inline
QualType TransformTypeSpecType(TypeLocBuilder &TLB, TyLoc T) {
  TyLoc NewT = TLB.push<TyLoc>(T.getType());
  NewT.setNameLoc(T.getNameLoc());
  return T.getType();
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformBuiltinType(TypeLocBuilder &TLB,
                                                      BuiltinTypeLoc T) {
  BuiltinTypeLoc NewT = TLB.push<BuiltinTypeLoc>(T.getType());
  NewT.setBuiltinLoc(T.getBuiltinLoc());
  if (T.needsExtraLocalData())
    NewT.getWrittenBuiltinSpecs() = T.getWrittenBuiltinSpecs();
  return T.getType();
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformComplexType(TypeLocBuilder &TLB,
                                                      ComplexTypeLoc T) {
  // FIXME: recurse?
  return TransformTypeSpecType(TLB, T);
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformAdjustedType(TypeLocBuilder &TLB,
                                                       AdjustedTypeLoc TL) {
  // Adjustments applied during transformation are handled elsewhere.
  return getDerived().TransformType(TLB, TL.getOriginalLoc());
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformDecayedType(TypeLocBuilder &TLB,
                                                      DecayedTypeLoc TL) {
  QualType OriginalType = getDerived().TransformType(TLB, TL.getOriginalLoc());
  if (OriginalType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      OriginalType != TL.getOriginalLoc().getType())
    Result = SemaRef.Context.getDecayedType(OriginalType);
  TLB.push<DecayedTypeLoc>(Result);
  // Nothing to set for DecayedTypeLoc.
  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformArrayParameterType(TypeLocBuilder &TLB,
                                                    ArrayParameterTypeLoc TL) {
  QualType OriginalType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (OriginalType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      OriginalType != TL.getElementLoc().getType())
    Result = SemaRef.Context.getArrayParameterType(OriginalType);
  TLB.push<ArrayParameterTypeLoc>(Result);
  // Nothing to set for ArrayParameterTypeLoc.
  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformPointerType(TypeLocBuilder &TLB,
                                                      PointerTypeLoc TL) {
  QualType PointeeType
    = getDerived().TransformType(TLB, TL.getPointeeLoc());
  if (PointeeType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (PointeeType->getAs<ObjCObjectType>()) {
    // A dependent pointer type 'T *' has is being transformed such
    // that an Objective-C class type is being replaced for 'T'. The
    // resulting pointer type is an ObjCObjectPointerType, not a
    // PointerType.
    Result = SemaRef.Context.getObjCObjectPointerType(PointeeType);

    ObjCObjectPointerTypeLoc NewT = TLB.push<ObjCObjectPointerTypeLoc>(Result);
    NewT.setStarLoc(TL.getStarLoc());
    return Result;
  }

  if (getDerived().AlwaysRebuild() ||
      PointeeType != TL.getPointeeLoc().getType()) {
    Result = getDerived().RebuildPointerType(PointeeType, TL.getSigilLoc());
    if (Result.isNull())
      return QualType();
  }

  // Objective-C ARC can add lifetime qualifiers to the type that we're
  // pointing to.
  TLB.TypeWasModifiedSafely(Result->getPointeeType());

  PointerTypeLoc NewT = TLB.push<PointerTypeLoc>(Result);
  NewT.setSigilLoc(TL.getSigilLoc());
  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformBlockPointerType(TypeLocBuilder &TLB,
                                                  BlockPointerTypeLoc TL) {
  QualType PointeeType
    = getDerived().TransformType(TLB, TL.getPointeeLoc());
  if (PointeeType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      PointeeType != TL.getPointeeLoc().getType()) {
    Result = getDerived().RebuildBlockPointerType(PointeeType,
                                                  TL.getSigilLoc());
    if (Result.isNull())
      return QualType();
  }

  BlockPointerTypeLoc NewT = TLB.push<BlockPointerTypeLoc>(Result);
  NewT.setSigilLoc(TL.getSigilLoc());
  return Result;
}

/// Transforms a reference type.  Note that somewhat paradoxically we
/// don't care whether the type itself is an l-value type or an r-value
/// type;  we only care if the type was *written* as an l-value type
/// or an r-value type.
template<typename Derived>
QualType
TreeTransform<Derived>::TransformReferenceType(TypeLocBuilder &TLB,
                                               ReferenceTypeLoc TL) {
  const ReferenceType *T = TL.getTypePtr();

  // Note that this works with the pointee-as-written.
  QualType PointeeType = getDerived().TransformType(TLB, TL.getPointeeLoc());
  if (PointeeType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      PointeeType != T->getPointeeTypeAsWritten()) {
    Result = getDerived().RebuildReferenceType(PointeeType,
                                               T->isSpelledAsLValue(),
                                               TL.getSigilLoc());
    if (Result.isNull())
      return QualType();
  }

  // Objective-C ARC can add lifetime qualifiers to the type that we're
  // referring to.
  TLB.TypeWasModifiedSafely(
      Result->castAs<ReferenceType>()->getPointeeTypeAsWritten());

  // r-value references can be rebuilt as l-value references.
  ReferenceTypeLoc NewTL;
  if (isa<LValueReferenceType>(Result))
    NewTL = TLB.push<LValueReferenceTypeLoc>(Result);
  else
    NewTL = TLB.push<RValueReferenceTypeLoc>(Result);
  NewTL.setSigilLoc(TL.getSigilLoc());

  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformLValueReferenceType(TypeLocBuilder &TLB,
                                                 LValueReferenceTypeLoc TL) {
  return TransformReferenceType(TLB, TL);
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformRValueReferenceType(TypeLocBuilder &TLB,
                                                 RValueReferenceTypeLoc TL) {
  return TransformReferenceType(TLB, TL);
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformMemberPointerType(TypeLocBuilder &TLB,
                                                   MemberPointerTypeLoc TL) {
  QualType PointeeType = getDerived().TransformType(TLB, TL.getPointeeLoc());
  if (PointeeType.isNull())
    return QualType();

  TypeSourceInfo* OldClsTInfo = TL.getClassTInfo();
  TypeSourceInfo *NewClsTInfo = nullptr;
  if (OldClsTInfo) {
    NewClsTInfo = getDerived().TransformType(OldClsTInfo);
    if (!NewClsTInfo)
      return QualType();
  }

  const MemberPointerType *T = TL.getTypePtr();
  QualType OldClsType = QualType(T->getClass(), 0);
  QualType NewClsType;
  if (NewClsTInfo)
    NewClsType = NewClsTInfo->getType();
  else {
    NewClsType = getDerived().TransformType(OldClsType);
    if (NewClsType.isNull())
      return QualType();
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      PointeeType != T->getPointeeType() ||
      NewClsType != OldClsType) {
    Result = getDerived().RebuildMemberPointerType(PointeeType, NewClsType,
                                                   TL.getStarLoc());
    if (Result.isNull())
      return QualType();
  }

  // If we had to adjust the pointee type when building a member pointer, make
  // sure to push TypeLoc info for it.
  const MemberPointerType *MPT = Result->getAs<MemberPointerType>();
  if (MPT && PointeeType != MPT->getPointeeType()) {
    assert(isa<AdjustedType>(MPT->getPointeeType()));
    TLB.push<AdjustedTypeLoc>(MPT->getPointeeType());
  }

  MemberPointerTypeLoc NewTL = TLB.push<MemberPointerTypeLoc>(Result);
  NewTL.setSigilLoc(TL.getSigilLoc());
  NewTL.setClassTInfo(NewClsTInfo);

  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformConstantArrayType(TypeLocBuilder &TLB,
                                                   ConstantArrayTypeLoc TL) {
  const ConstantArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  // Prefer the expression from the TypeLoc;  the other may have been uniqued.
  Expr *OldSize = TL.getSizeExpr();
  if (!OldSize)
    OldSize = const_cast<Expr*>(T->getSizeExpr());
  Expr *NewSize = nullptr;
  if (OldSize) {
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    NewSize = getDerived().TransformExpr(OldSize).template getAs<Expr>();
    NewSize = SemaRef.ActOnConstantExpression(NewSize).get();
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType() ||
      (T->getSizeExpr() && NewSize != OldSize)) {
    Result = getDerived().RebuildConstantArrayType(ElementType,
                                                   T->getSizeModifier(),
                                                   T->getSize(), NewSize,
                                             T->getIndexTypeCVRQualifiers(),
                                                   TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  // We might have either a ConstantArrayType or a VariableArrayType now:
  // a ConstantArrayType is allowed to have an element type which is a
  // VariableArrayType if the type is dependent.  Fortunately, all array
  // types have the same location layout.
  ArrayTypeLoc NewTL = TLB.push<ArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(NewSize);

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformIncompleteArrayType(
                                              TypeLocBuilder &TLB,
                                              IncompleteArrayTypeLoc TL) {
  const IncompleteArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType()) {
    Result = getDerived().RebuildIncompleteArrayType(ElementType,
                                                     T->getSizeModifier(),
                                           T->getIndexTypeCVRQualifiers(),
                                                     TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  IncompleteArrayTypeLoc NewTL = TLB.push<IncompleteArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(nullptr);

  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformVariableArrayType(TypeLocBuilder &TLB,
                                                   VariableArrayTypeLoc TL) {
  const VariableArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  ExprResult SizeResult;
  {
    EnterExpressionEvaluationContext Context(
        SemaRef, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
    SizeResult = getDerived().TransformExpr(T->getSizeExpr());
  }
  if (SizeResult.isInvalid())
    return QualType();
  SizeResult =
      SemaRef.ActOnFinishFullExpr(SizeResult.get(), /*DiscardedValue*/ false);
  if (SizeResult.isInvalid())
    return QualType();

  Expr *Size = SizeResult.get();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType() ||
      Size != T->getSizeExpr()) {
    Result = getDerived().RebuildVariableArrayType(ElementType,
                                                   T->getSizeModifier(),
                                                   Size,
                                             T->getIndexTypeCVRQualifiers(),
                                                   TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  // We might have constant size array now, but fortunately it has the same
  // location layout.
  ArrayTypeLoc NewTL = TLB.push<ArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(Size);

  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformDependentSizedArrayType(TypeLocBuilder &TLB,
                                             DependentSizedArrayTypeLoc TL) {
  const DependentSizedArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  // Array bounds are constant expressions.
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  // If we have a VLA then it won't be a constant.
  SemaRef.ExprEvalContexts.back().InConditionallyConstantEvaluateContext = true;

  // Prefer the expression from the TypeLoc;  the other may have been uniqued.
  Expr *origSize = TL.getSizeExpr();
  if (!origSize) origSize = T->getSizeExpr();

  ExprResult sizeResult
    = getDerived().TransformExpr(origSize);
  sizeResult = SemaRef.ActOnConstantExpression(sizeResult);
  if (sizeResult.isInvalid())
    return QualType();

  Expr *size = sizeResult.get();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType() ||
      size != origSize) {
    Result = getDerived().RebuildDependentSizedArrayType(ElementType,
                                                         T->getSizeModifier(),
                                                         size,
                                                T->getIndexTypeCVRQualifiers(),
                                                        TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  // We might have any sort of array type now, but fortunately they
  // all have the same location layout.
  ArrayTypeLoc NewTL = TLB.push<ArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(size);

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformDependentVectorType(
    TypeLocBuilder &TLB, DependentVectorTypeLoc TL) {
  const DependentVectorType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult Size = getDerived().TransformExpr(T->getSizeExpr());
  Size = SemaRef.ActOnConstantExpression(Size);
  if (Size.isInvalid())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType() ||
      Size.get() != T->getSizeExpr()) {
    Result = getDerived().RebuildDependentVectorType(
        ElementType, Size.get(), T->getAttributeLoc(), T->getVectorKind());
    if (Result.isNull())
      return QualType();
  }

  // Result might be dependent or not.
  if (isa<DependentVectorType>(Result)) {
    DependentVectorTypeLoc NewTL =
        TLB.push<DependentVectorTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
  } else {
    VectorTypeLoc NewTL = TLB.push<VectorTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
  }

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformDependentSizedExtVectorType(
                                      TypeLocBuilder &TLB,
                                      DependentSizedExtVectorTypeLoc TL) {
  const DependentSizedExtVectorType *T = TL.getTypePtr();

  // FIXME: ext vector locs should be nested
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  // Vector sizes are constant expressions.
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult Size = getDerived().TransformExpr(T->getSizeExpr());
  Size = SemaRef.ActOnConstantExpression(Size);
  if (Size.isInvalid())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType() ||
      Size.get() != T->getSizeExpr()) {
    Result = getDerived().RebuildDependentSizedExtVectorType(ElementType,
                                                             Size.get(),
                                                         T->getAttributeLoc());
    if (Result.isNull())
      return QualType();
  }

  // Result might be dependent or not.
  if (isa<DependentSizedExtVectorType>(Result)) {
    DependentSizedExtVectorTypeLoc NewTL
      = TLB.push<DependentSizedExtVectorTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
  } else {
    ExtVectorTypeLoc NewTL = TLB.push<ExtVectorTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
  }

  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformConstantMatrixType(TypeLocBuilder &TLB,
                                                    ConstantMatrixTypeLoc TL) {
  const ConstantMatrixType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(T->getElementType());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType()) {
    Result = getDerived().RebuildConstantMatrixType(
        ElementType, T->getNumRows(), T->getNumColumns());
    if (Result.isNull())
      return QualType();
  }

  ConstantMatrixTypeLoc NewTL = TLB.push<ConstantMatrixTypeLoc>(Result);
  NewTL.setAttrNameLoc(TL.getAttrNameLoc());
  NewTL.setAttrOperandParensRange(TL.getAttrOperandParensRange());
  NewTL.setAttrRowOperand(TL.getAttrRowOperand());
  NewTL.setAttrColumnOperand(TL.getAttrColumnOperand());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformDependentSizedMatrixType(
    TypeLocBuilder &TLB, DependentSizedMatrixTypeLoc TL) {
  const DependentSizedMatrixType *T = TL.getTypePtr();

  QualType ElementType = getDerived().TransformType(T->getElementType());
  if (ElementType.isNull()) {
    return QualType();
  }

  // Matrix dimensions are constant expressions.
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  Expr *origRows = TL.getAttrRowOperand();
  if (!origRows)
    origRows = T->getRowExpr();
  Expr *origColumns = TL.getAttrColumnOperand();
  if (!origColumns)
    origColumns = T->getColumnExpr();

  ExprResult rowResult = getDerived().TransformExpr(origRows);
  rowResult = SemaRef.ActOnConstantExpression(rowResult);
  if (rowResult.isInvalid())
    return QualType();

  ExprResult columnResult = getDerived().TransformExpr(origColumns);
  columnResult = SemaRef.ActOnConstantExpression(columnResult);
  if (columnResult.isInvalid())
    return QualType();

  Expr *rows = rowResult.get();
  Expr *columns = columnResult.get();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType() ||
      rows != origRows || columns != origColumns) {
    Result = getDerived().RebuildDependentSizedMatrixType(
        ElementType, rows, columns, T->getAttributeLoc());

    if (Result.isNull())
      return QualType();
  }

  // We might have any sort of matrix type now, but fortunately they
  // all have the same location layout.
  MatrixTypeLoc NewTL = TLB.push<MatrixTypeLoc>(Result);
  NewTL.setAttrNameLoc(TL.getAttrNameLoc());
  NewTL.setAttrOperandParensRange(TL.getAttrOperandParensRange());
  NewTL.setAttrRowOperand(rows);
  NewTL.setAttrColumnOperand(columns);
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformDependentAddressSpaceType(
    TypeLocBuilder &TLB, DependentAddressSpaceTypeLoc TL) {
  const DependentAddressSpaceType *T = TL.getTypePtr();

  QualType pointeeType = getDerived().TransformType(T->getPointeeType());

  if (pointeeType.isNull())
    return QualType();

  // Address spaces are constant expressions.
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  ExprResult AddrSpace = getDerived().TransformExpr(T->getAddrSpaceExpr());
  AddrSpace = SemaRef.ActOnConstantExpression(AddrSpace);
  if (AddrSpace.isInvalid())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || pointeeType != T->getPointeeType() ||
      AddrSpace.get() != T->getAddrSpaceExpr()) {
    Result = getDerived().RebuildDependentAddressSpaceType(
        pointeeType, AddrSpace.get(), T->getAttributeLoc());
    if (Result.isNull())
      return QualType();
  }

  // Result might be dependent or not.
  if (isa<DependentAddressSpaceType>(Result)) {
    DependentAddressSpaceTypeLoc NewTL =
        TLB.push<DependentAddressSpaceTypeLoc>(Result);

    NewTL.setAttrOperandParensRange(TL.getAttrOperandParensRange());
    NewTL.setAttrExprOperand(TL.getAttrExprOperand());
    NewTL.setAttrNameLoc(TL.getAttrNameLoc());

  } else {
    TypeSourceInfo *DI = getSema().Context.getTrivialTypeSourceInfo(
        Result, getDerived().getBaseLocation());
    TransformType(TLB, DI->getTypeLoc());
  }

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformVectorType(TypeLocBuilder &TLB,
                                                     VectorTypeLoc TL) {
  const VectorType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType()) {
    Result = getDerived().RebuildVectorType(ElementType, T->getNumElements(),
                                            T->getVectorKind());
    if (Result.isNull())
      return QualType();
  }

  VectorTypeLoc NewTL = TLB.push<VectorTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformExtVectorType(TypeLocBuilder &TLB,
                                                        ExtVectorTypeLoc TL) {
  const VectorType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ElementType != T->getElementType()) {
    Result = getDerived().RebuildExtVectorType(ElementType,
                                               T->getNumElements(),
                                               /*FIXME*/ SourceLocation());
    if (Result.isNull())
      return QualType();
  }

  ExtVectorTypeLoc NewTL = TLB.push<ExtVectorTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
ParmVarDecl *TreeTransform<Derived>::TransformFunctionTypeParam(
    ParmVarDecl *OldParm, int indexAdjustment,
    std::optional<unsigned> NumExpansions, bool ExpectParameterPack) {
  TypeSourceInfo *OldDI = OldParm->getTypeSourceInfo();
  TypeSourceInfo *NewDI = nullptr;

  if (NumExpansions && isa<PackExpansionType>(OldDI->getType())) {
    // If we're substituting into a pack expansion type and we know the
    // length we want to expand to, just substitute for the pattern.
    TypeLoc OldTL = OldDI->getTypeLoc();
    PackExpansionTypeLoc OldExpansionTL = OldTL.castAs<PackExpansionTypeLoc>();

    TypeLocBuilder TLB;
    TypeLoc NewTL = OldDI->getTypeLoc();
    TLB.reserve(NewTL.getFullDataSize());

    QualType Result = getDerived().TransformType(TLB,
                                               OldExpansionTL.getPatternLoc());
    if (Result.isNull())
      return nullptr;

    Result = RebuildPackExpansionType(Result,
                                OldExpansionTL.getPatternLoc().getSourceRange(),
                                      OldExpansionTL.getEllipsisLoc(),
                                      NumExpansions);
    if (Result.isNull())
      return nullptr;

    PackExpansionTypeLoc NewExpansionTL
      = TLB.push<PackExpansionTypeLoc>(Result);
    NewExpansionTL.setEllipsisLoc(OldExpansionTL.getEllipsisLoc());
    NewDI = TLB.getTypeSourceInfo(SemaRef.Context, Result);
  } else
    NewDI = getDerived().TransformType(OldDI);
  if (!NewDI)
    return nullptr;

  if (NewDI == OldDI && indexAdjustment == 0)
    return OldParm;

  ParmVarDecl *newParm = ParmVarDecl::Create(SemaRef.Context,
                                             OldParm->getDeclContext(),
                                             OldParm->getInnerLocStart(),
                                             OldParm->getLocation(),
                                             OldParm->getIdentifier(),
                                             NewDI->getType(),
                                             NewDI,
                                             OldParm->getStorageClass(),
                                             /* DefArg */ nullptr);
  newParm->setScopeInfo(OldParm->getFunctionScopeDepth(),
                        OldParm->getFunctionScopeIndex() + indexAdjustment);
  transformedLocalDecl(OldParm, {newParm});
  return newParm;
}

template <typename Derived>
bool TreeTransform<Derived>::TransformFunctionTypeParams(
    SourceLocation Loc, ArrayRef<ParmVarDecl *> Params,
    const QualType *ParamTypes,
    const FunctionProtoType::ExtParameterInfo *ParamInfos,
    SmallVectorImpl<QualType> &OutParamTypes,
    SmallVectorImpl<ParmVarDecl *> *PVars,
    Sema::ExtParameterInfoBuilder &PInfos,
    unsigned *LastParamTransformed) {
  int indexAdjustment = 0;

  unsigned NumParams = Params.size();
  for (unsigned i = 0; i != NumParams; ++i) {
    if (LastParamTransformed)
      *LastParamTransformed = i;
    if (ParmVarDecl *OldParm = Params[i]) {
      assert(OldParm->getFunctionScopeIndex() == i);

      std::optional<unsigned> NumExpansions;
      ParmVarDecl *NewParm = nullptr;
      if (OldParm->isParameterPack()) {
        // We have a function parameter pack that may need to be expanded.
        SmallVector<UnexpandedParameterPack, 2> Unexpanded;

        // Find the parameter packs that could be expanded.
        TypeLoc TL = OldParm->getTypeSourceInfo()->getTypeLoc();
        PackExpansionTypeLoc ExpansionTL = TL.castAs<PackExpansionTypeLoc>();
        TypeLoc Pattern = ExpansionTL.getPatternLoc();
        SemaRef.collectUnexpandedParameterPacks(Pattern, Unexpanded);

        // Determine whether we should expand the parameter packs.
        bool ShouldExpand = false;
        bool RetainExpansion = false;
        std::optional<unsigned> OrigNumExpansions;
        if (Unexpanded.size() > 0) {
          OrigNumExpansions = ExpansionTL.getTypePtr()->getNumExpansions();
          NumExpansions = OrigNumExpansions;
          if (getDerived().TryExpandParameterPacks(ExpansionTL.getEllipsisLoc(),
                                                   Pattern.getSourceRange(),
                                                   Unexpanded,
                                                   ShouldExpand,
                                                   RetainExpansion,
                                                   NumExpansions)) {
            return true;
          }
        } else {
#ifndef NDEBUG
          const AutoType *AT =
              Pattern.getType().getTypePtr()->getContainedAutoType();
          assert((AT && (!AT->isDeduced() || AT->getDeducedType().isNull())) &&
                 "Could not find parameter packs or undeduced auto type!");
#endif
        }

        if (ShouldExpand) {
          // Expand the function parameter pack into multiple, separate
          // parameters.
          getDerived().ExpandingFunctionParameterPack(OldParm);
          for (unsigned I = 0; I != *NumExpansions; ++I) {
            Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
            ParmVarDecl *NewParm
              = getDerived().TransformFunctionTypeParam(OldParm,
                                                        indexAdjustment++,
                                                        OrigNumExpansions,
                                                /*ExpectParameterPack=*/false);
            if (!NewParm)
              return true;

            if (ParamInfos)
              PInfos.set(OutParamTypes.size(), ParamInfos[i]);
            OutParamTypes.push_back(NewParm->getType());
            if (PVars)
              PVars->push_back(NewParm);
          }

          // If we're supposed to retain a pack expansion, do so by temporarily
          // forgetting the partially-substituted parameter pack.
          if (RetainExpansion) {
            ForgetPartiallySubstitutedPackRAII Forget(getDerived());
            ParmVarDecl *NewParm
              = getDerived().TransformFunctionTypeParam(OldParm,
                                                        indexAdjustment++,
                                                        OrigNumExpansions,
                                                /*ExpectParameterPack=*/false);
            if (!NewParm)
              return true;

            if (ParamInfos)
              PInfos.set(OutParamTypes.size(), ParamInfos[i]);
            OutParamTypes.push_back(NewParm->getType());
            if (PVars)
              PVars->push_back(NewParm);
          }

          // The next parameter should have the same adjustment as the
          // last thing we pushed, but we post-incremented indexAdjustment
          // on every push.  Also, if we push nothing, the adjustment should
          // go down by one.
          indexAdjustment--;

          // We're done with the pack expansion.
          continue;
        }

        // We'll substitute the parameter now without expanding the pack
        // expansion.
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
        NewParm = getDerived().TransformFunctionTypeParam(OldParm,
                                                          indexAdjustment,
                                                          NumExpansions,
                                                  /*ExpectParameterPack=*/true);
        assert(NewParm->isParameterPack() &&
               "Parameter pack no longer a parameter pack after "
               "transformation.");
      } else {
        NewParm = getDerived().TransformFunctionTypeParam(
            OldParm, indexAdjustment, std::nullopt,
            /*ExpectParameterPack=*/false);
      }

      if (!NewParm)
        return true;

      if (ParamInfos)
        PInfos.set(OutParamTypes.size(), ParamInfos[i]);
      OutParamTypes.push_back(NewParm->getType());
      if (PVars)
        PVars->push_back(NewParm);
      continue;
    }

    // Deal with the possibility that we don't have a parameter
    // declaration for this parameter.
    assert(ParamTypes);
    QualType OldType = ParamTypes[i];
    bool IsPackExpansion = false;
    std::optional<unsigned> NumExpansions;
    QualType NewType;
    if (const PackExpansionType *Expansion
                                       = dyn_cast<PackExpansionType>(OldType)) {
      // We have a function parameter pack that may need to be expanded.
      QualType Pattern = Expansion->getPattern();
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      getSema().collectUnexpandedParameterPacks(Pattern, Unexpanded);

      // Determine whether we should expand the parameter packs.
      bool ShouldExpand = false;
      bool RetainExpansion = false;
      if (getDerived().TryExpandParameterPacks(Loc, SourceRange(),
                                               Unexpanded,
                                               ShouldExpand,
                                               RetainExpansion,
                                               NumExpansions)) {
        return true;
      }

      if (ShouldExpand) {
        // Expand the function parameter pack into multiple, separate
        // parameters.
        for (unsigned I = 0; I != *NumExpansions; ++I) {
          Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
          QualType NewType = getDerived().TransformType(Pattern);
          if (NewType.isNull())
            return true;

          if (NewType->containsUnexpandedParameterPack()) {
            NewType = getSema().getASTContext().getPackExpansionType(
                NewType, std::nullopt);

            if (NewType.isNull())
              return true;
          }

          if (ParamInfos)
            PInfos.set(OutParamTypes.size(), ParamInfos[i]);
          OutParamTypes.push_back(NewType);
          if (PVars)
            PVars->push_back(nullptr);
        }

        // We're done with the pack expansion.
        continue;
      }

      // If we're supposed to retain a pack expansion, do so by temporarily
      // forgetting the partially-substituted parameter pack.
      if (RetainExpansion) {
        ForgetPartiallySubstitutedPackRAII Forget(getDerived());
        QualType NewType = getDerived().TransformType(Pattern);
        if (NewType.isNull())
          return true;

        if (ParamInfos)
          PInfos.set(OutParamTypes.size(), ParamInfos[i]);
        OutParamTypes.push_back(NewType);
        if (PVars)
          PVars->push_back(nullptr);
      }

      // We'll substitute the parameter now without expanding the pack
      // expansion.
      OldType = Expansion->getPattern();
      IsPackExpansion = true;
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
      NewType = getDerived().TransformType(OldType);
    } else {
      NewType = getDerived().TransformType(OldType);
    }

    if (NewType.isNull())
      return true;

    if (IsPackExpansion)
      NewType = getSema().Context.getPackExpansionType(NewType,
                                                       NumExpansions);

    if (ParamInfos)
      PInfos.set(OutParamTypes.size(), ParamInfos[i]);
    OutParamTypes.push_back(NewType);
    if (PVars)
      PVars->push_back(nullptr);
  }

#ifndef NDEBUG
  if (PVars) {
    for (unsigned i = 0, e = PVars->size(); i != e; ++i)
      if (ParmVarDecl *parm = (*PVars)[i])
        assert(parm->getFunctionScopeIndex() == i);
  }
#endif

  return false;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformFunctionProtoType(TypeLocBuilder &TLB,
                                                   FunctionProtoTypeLoc TL) {
  SmallVector<QualType, 4> ExceptionStorage;
  return getDerived().TransformFunctionProtoType(
      TLB, TL, nullptr, Qualifiers(),
      [&](FunctionProtoType::ExceptionSpecInfo &ESI, bool &Changed) {
        return getDerived().TransformExceptionSpec(TL.getBeginLoc(), ESI,
                                                   ExceptionStorage, Changed);
      });
}

template<typename Derived> template<typename Fn>
QualType TreeTransform<Derived>::TransformFunctionProtoType(
    TypeLocBuilder &TLB, FunctionProtoTypeLoc TL, CXXRecordDecl *ThisContext,
    Qualifiers ThisTypeQuals, Fn TransformExceptionSpec) {

  // Transform the parameters and return type.
  //
  // We are required to instantiate the params and return type in source order.
  // When the function has a trailing return type, we instantiate the
  // parameters before the return type,  since the return type can then refer
  // to the parameters themselves (via decltype, sizeof, etc.).
  //
  SmallVector<QualType, 4> ParamTypes;
  SmallVector<ParmVarDecl*, 4> ParamDecls;
  Sema::ExtParameterInfoBuilder ExtParamInfos;
  const FunctionProtoType *T = TL.getTypePtr();

  QualType ResultType;

  if (T->hasTrailingReturn()) {
    if (getDerived().TransformFunctionTypeParams(
            TL.getBeginLoc(), TL.getParams(),
            TL.getTypePtr()->param_type_begin(),
            T->getExtParameterInfosOrNull(),
            ParamTypes, &ParamDecls, ExtParamInfos))
      return QualType();

    {
      // C++11 [expr.prim.general]p3:
      //   If a declaration declares a member function or member function
      //   template of a class X, the expression this is a prvalue of type
      //   "pointer to cv-qualifier-seq X" between the optional cv-qualifer-seq
      //   and the end of the function-definition, member-declarator, or
      //   declarator.
      auto *RD = dyn_cast<CXXRecordDecl>(SemaRef.getCurLexicalContext());
      Sema::CXXThisScopeRAII ThisScope(
          SemaRef, !ThisContext && RD ? RD : ThisContext, ThisTypeQuals);

      ResultType = getDerived().TransformType(TLB, TL.getReturnLoc());
      if (ResultType.isNull())
        return QualType();
    }
  }
  else {
    ResultType = getDerived().TransformType(TLB, TL.getReturnLoc());
    if (ResultType.isNull())
      return QualType();

    if (getDerived().TransformFunctionTypeParams(
            TL.getBeginLoc(), TL.getParams(),
            TL.getTypePtr()->param_type_begin(),
            T->getExtParameterInfosOrNull(),
            ParamTypes, &ParamDecls, ExtParamInfos))
      return QualType();
  }

  FunctionProtoType::ExtProtoInfo EPI = T->getExtProtoInfo();

  bool EPIChanged = false;
  if (TransformExceptionSpec(EPI.ExceptionSpec, EPIChanged))
    return QualType();

  // Handle extended parameter information.
  if (auto NewExtParamInfos =
        ExtParamInfos.getPointerOrNull(ParamTypes.size())) {
    if (!EPI.ExtParameterInfos ||
        llvm::ArrayRef(EPI.ExtParameterInfos, TL.getNumParams()) !=
            llvm::ArrayRef(NewExtParamInfos, ParamTypes.size())) {
      EPIChanged = true;
    }
    EPI.ExtParameterInfos = NewExtParamInfos;
  } else if (EPI.ExtParameterInfos) {
    EPIChanged = true;
    EPI.ExtParameterInfos = nullptr;
  }

  // Transform any function effects with unevaluated conditions.
  // Hold this set in a local for the rest of this function, since EPI
  // may need to hold a FunctionEffectsRef pointing into it.
  std::optional<FunctionEffectSet> NewFX;
  if (ArrayRef FXConds = EPI.FunctionEffects.conditions(); !FXConds.empty()) {
    NewFX.emplace();
    EnterExpressionEvaluationContext Unevaluated(
        getSema(), Sema::ExpressionEvaluationContext::ConstantEvaluated);

    for (const FunctionEffectWithCondition &PrevEC : EPI.FunctionEffects) {
      FunctionEffectWithCondition NewEC = PrevEC;
      if (Expr *CondExpr = PrevEC.Cond.getCondition()) {
        ExprResult NewExpr = getDerived().TransformExpr(CondExpr);
        if (NewExpr.isInvalid())
          return QualType();
        std::optional<FunctionEffectMode> Mode =
            SemaRef.ActOnEffectExpression(NewExpr.get(), PrevEC.Effect.name());
        if (!Mode)
          return QualType();

        // The condition expression has been transformed, and re-evaluated.
        // It may or may not have become constant.
        switch (*Mode) {
        case FunctionEffectMode::True:
          NewEC.Cond = {};
          break;
        case FunctionEffectMode::False:
          NewEC.Effect = FunctionEffect(PrevEC.Effect.oppositeKind());
          NewEC.Cond = {};
          break;
        case FunctionEffectMode::Dependent:
          NewEC.Cond = EffectConditionExpr(NewExpr.get());
          break;
        case FunctionEffectMode::None:
          llvm_unreachable(
              "FunctionEffectMode::None shouldn't be possible here");
        }
      }
      if (!SemaRef.diagnoseConflictingFunctionEffect(*NewFX, NewEC,
                                                     TL.getBeginLoc())) {
        FunctionEffectSet::Conflicts Errs;
        NewFX->insert(NewEC, Errs);
        assert(Errs.empty());
      }
    }
    EPI.FunctionEffects = *NewFX;
    EPIChanged = true;
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ResultType != T->getReturnType() ||
      T->getParamTypes() != llvm::ArrayRef(ParamTypes) || EPIChanged) {
    Result = getDerived().RebuildFunctionProtoType(ResultType, ParamTypes, EPI);
    if (Result.isNull())
      return QualType();
  }

  FunctionProtoTypeLoc NewTL = TLB.push<FunctionProtoTypeLoc>(Result);
  NewTL.setLocalRangeBegin(TL.getLocalRangeBegin());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setExceptionSpecRange(TL.getExceptionSpecRange());
  NewTL.setLocalRangeEnd(TL.getLocalRangeEnd());
  for (unsigned i = 0, e = NewTL.getNumParams(); i != e; ++i)
    NewTL.setParam(i, ParamDecls[i]);

  return Result;
}

template<typename Derived>
bool TreeTransform<Derived>::TransformExceptionSpec(
    SourceLocation Loc, FunctionProtoType::ExceptionSpecInfo &ESI,
    SmallVectorImpl<QualType> &Exceptions, bool &Changed) {
  assert(ESI.Type != EST_Uninstantiated && ESI.Type != EST_Unevaluated);

  // Instantiate a dynamic noexcept expression, if any.
  if (isComputedNoexcept(ESI.Type)) {
    // Update this scrope because ContextDecl in Sema will be used in
    // TransformExpr.
    auto *Method = dyn_cast_if_present<CXXMethodDecl>(ESI.SourceTemplate);
    Sema::CXXThisScopeRAII ThisScope(
        SemaRef, Method ? Method->getParent() : nullptr,
        Method ? Method->getMethodQualifiers() : Qualifiers{},
        Method != nullptr);
    EnterExpressionEvaluationContext Unevaluated(
        getSema(), Sema::ExpressionEvaluationContext::ConstantEvaluated);
    ExprResult NoexceptExpr = getDerived().TransformExpr(ESI.NoexceptExpr);
    if (NoexceptExpr.isInvalid())
      return true;

    ExceptionSpecificationType EST = ESI.Type;
    NoexceptExpr =
        getSema().ActOnNoexceptSpec(NoexceptExpr.get(), EST);
    if (NoexceptExpr.isInvalid())
      return true;

    if (ESI.NoexceptExpr != NoexceptExpr.get() || EST != ESI.Type)
      Changed = true;
    ESI.NoexceptExpr = NoexceptExpr.get();
    ESI.Type = EST;
  }

  if (ESI.Type != EST_Dynamic)
    return false;

  // Instantiate a dynamic exception specification's type.
  for (QualType T : ESI.Exceptions) {
    if (const PackExpansionType *PackExpansion =
            T->getAs<PackExpansionType>()) {
      Changed = true;

      // We have a pack expansion. Instantiate it.
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      SemaRef.collectUnexpandedParameterPacks(PackExpansion->getPattern(),
                                              Unexpanded);
      assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

      // Determine whether the set of unexpanded parameter packs can and
      // should
      // be expanded.
      bool Expand = false;
      bool RetainExpansion = false;
      std::optional<unsigned> NumExpansions = PackExpansion->getNumExpansions();
      // FIXME: Track the location of the ellipsis (and track source location
      // information for the types in the exception specification in general).
      if (getDerived().TryExpandParameterPacks(
              Loc, SourceRange(), Unexpanded, Expand,
              RetainExpansion, NumExpansions))
        return true;

      if (!Expand) {
        // We can't expand this pack expansion into separate arguments yet;
        // just substitute into the pattern and create a new pack expansion
        // type.
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
        QualType U = getDerived().TransformType(PackExpansion->getPattern());
        if (U.isNull())
          return true;

        U = SemaRef.Context.getPackExpansionType(U, NumExpansions);
        Exceptions.push_back(U);
        continue;
      }

      // Substitute into the pack expansion pattern for each slice of the
      // pack.
      for (unsigned ArgIdx = 0; ArgIdx != *NumExpansions; ++ArgIdx) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), ArgIdx);

        QualType U = getDerived().TransformType(PackExpansion->getPattern());
        if (U.isNull() || SemaRef.CheckSpecifiedExceptionType(U, Loc))
          return true;

        Exceptions.push_back(U);
      }
    } else {
      QualType U = getDerived().TransformType(T);
      if (U.isNull() || SemaRef.CheckSpecifiedExceptionType(U, Loc))
        return true;
      if (T != U)
        Changed = true;

      Exceptions.push_back(U);
    }
  }

  ESI.Exceptions = Exceptions;
  if (ESI.Exceptions.empty())
    ESI.Type = EST_DynamicNone;
  return false;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformFunctionNoProtoType(
                                                 TypeLocBuilder &TLB,
                                                 FunctionNoProtoTypeLoc TL) {
  const FunctionNoProtoType *T = TL.getTypePtr();
  QualType ResultType = getDerived().TransformType(TLB, TL.getReturnLoc());
  if (ResultType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ResultType != T->getReturnType())
    Result = getDerived().RebuildFunctionNoProtoType(ResultType);

  FunctionNoProtoTypeLoc NewTL = TLB.push<FunctionNoProtoTypeLoc>(Result);
  NewTL.setLocalRangeBegin(TL.getLocalRangeBegin());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setLocalRangeEnd(TL.getLocalRangeEnd());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformUnresolvedUsingType(
    TypeLocBuilder &TLB, UnresolvedUsingTypeLoc TL) {
  const UnresolvedUsingType *T = TL.getTypePtr();
  Decl *D = getDerived().TransformDecl(TL.getNameLoc(), T->getDecl());
  if (!D)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || D != T->getDecl()) {
    Result = getDerived().RebuildUnresolvedUsingType(TL.getNameLoc(), D);
    if (Result.isNull())
      return QualType();
  }

  // We might get an arbitrary type spec type back.  We should at
  // least always get a type spec type, though.
  TypeSpecTypeLoc NewTL = TLB.pushTypeSpec(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformUsingType(TypeLocBuilder &TLB,
                                                    UsingTypeLoc TL) {
  const UsingType *T = TL.getTypePtr();

  auto *Found = cast_or_null<UsingShadowDecl>(getDerived().TransformDecl(
      TL.getLocalSourceRange().getBegin(), T->getFoundDecl()));
  if (!Found)
    return QualType();

  QualType Underlying = getDerived().TransformType(T->desugar());
  if (Underlying.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Found != T->getFoundDecl() ||
      Underlying != T->getUnderlyingType()) {
    Result = getDerived().RebuildUsingType(Found, Underlying);
    if (Result.isNull())
      return QualType();
  }

  TLB.pushTypeSpec(Result).setNameLoc(TL.getNameLoc());
  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformTypedefType(TypeLocBuilder &TLB,
                                                      TypedefTypeLoc TL) {
  const TypedefType *T = TL.getTypePtr();
  TypedefNameDecl *Typedef
    = cast_or_null<TypedefNameDecl>(getDerived().TransformDecl(TL.getNameLoc(),
                                                               T->getDecl()));
  if (!Typedef)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      Typedef != T->getDecl()) {
    Result = getDerived().RebuildTypedefType(Typedef);
    if (Result.isNull())
      return QualType();
  }

  TypedefTypeLoc NewTL = TLB.push<TypedefTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformTypeOfExprType(TypeLocBuilder &TLB,
                                                      TypeOfExprTypeLoc TL) {
  // typeof expressions are not potentially evaluated contexts
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated,
      Sema::ReuseLambdaContextDecl);

  ExprResult E = getDerived().TransformExpr(TL.getUnderlyingExpr());
  if (E.isInvalid())
    return QualType();

  E = SemaRef.HandleExprEvaluationContextForTypeof(E.get());
  if (E.isInvalid())
    return QualType();

  QualType Result = TL.getType();
  TypeOfKind Kind = Result->castAs<TypeOfExprType>()->getKind();
  if (getDerived().AlwaysRebuild() || E.get() != TL.getUnderlyingExpr()) {
    Result =
        getDerived().RebuildTypeOfExprType(E.get(), TL.getTypeofLoc(), Kind);
    if (Result.isNull())
      return QualType();
  }

  TypeOfExprTypeLoc NewTL = TLB.push<TypeOfExprTypeLoc>(Result);
  NewTL.setTypeofLoc(TL.getTypeofLoc());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformTypeOfType(TypeLocBuilder &TLB,
                                                     TypeOfTypeLoc TL) {
  TypeSourceInfo* Old_Under_TI = TL.getUnmodifiedTInfo();
  TypeSourceInfo* New_Under_TI = getDerived().TransformType(Old_Under_TI);
  if (!New_Under_TI)
    return QualType();

  QualType Result = TL.getType();
  TypeOfKind Kind = Result->castAs<TypeOfType>()->getKind();
  if (getDerived().AlwaysRebuild() || New_Under_TI != Old_Under_TI) {
    Result = getDerived().RebuildTypeOfType(New_Under_TI->getType(), Kind);
    if (Result.isNull())
      return QualType();
  }

  TypeOfTypeLoc NewTL = TLB.push<TypeOfTypeLoc>(Result);
  NewTL.setTypeofLoc(TL.getTypeofLoc());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setUnmodifiedTInfo(New_Under_TI);

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformDecltypeType(TypeLocBuilder &TLB,
                                                       DecltypeTypeLoc TL) {
  const DecltypeType *T = TL.getTypePtr();

  // decltype expressions are not potentially evaluated contexts
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated, nullptr,
      Sema::ExpressionEvaluationContextRecord::EK_Decltype);

  ExprResult E = getDerived().TransformExpr(T->getUnderlyingExpr());
  if (E.isInvalid())
    return QualType();

  E = getSema().ActOnDecltypeExpression(E.get());
  if (E.isInvalid())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      E.get() != T->getUnderlyingExpr()) {
    Result = getDerived().RebuildDecltypeType(E.get(), TL.getDecltypeLoc());
    if (Result.isNull())
      return QualType();
  }
  else E.get();

  DecltypeTypeLoc NewTL = TLB.push<DecltypeTypeLoc>(Result);
  NewTL.setDecltypeLoc(TL.getDecltypeLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformPackIndexingType(TypeLocBuilder &TLB,
                                                  PackIndexingTypeLoc TL) {
  // Transform the index
  ExprResult IndexExpr = getDerived().TransformExpr(TL.getIndexExpr());
  if (IndexExpr.isInvalid())
    return QualType();
  QualType Pattern = TL.getPattern();

  const PackIndexingType *PIT = TL.getTypePtr();
  SmallVector<QualType, 5> SubtitutedTypes;
  llvm::ArrayRef<QualType> Types = PIT->getExpansions();

  bool NotYetExpanded = Types.empty();
  bool FullySubstituted = true;

  if (Types.empty())
    Types = llvm::ArrayRef<QualType>(&Pattern, 1);

  for (const QualType &T : Types) {
    if (!T->containsUnexpandedParameterPack()) {
      QualType Transformed = getDerived().TransformType(T);
      if (Transformed.isNull())
        return QualType();
      SubtitutedTypes.push_back(Transformed);
      continue;
    }

    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    getSema().collectUnexpandedParameterPacks(T, Unexpanded);
    assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");
    // Determine whether the set of unexpanded parameter packs can and should
    // be expanded.
    bool ShouldExpand = true;
    bool RetainExpansion = false;
    std::optional<unsigned> OrigNumExpansions;
    std::optional<unsigned> NumExpansions = OrigNumExpansions;
    if (getDerived().TryExpandParameterPacks(TL.getEllipsisLoc(), SourceRange(),
                                             Unexpanded, ShouldExpand,
                                             RetainExpansion, NumExpansions))
      return QualType();
    if (!ShouldExpand) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
      // FIXME: should we keep TypeLoc for individual expansions in
      // PackIndexingTypeLoc?
      TypeSourceInfo *TI =
          SemaRef.getASTContext().getTrivialTypeSourceInfo(T, TL.getBeginLoc());
      QualType Pack = getDerived().TransformType(TLB, TI->getTypeLoc());
      if (Pack.isNull())
        return QualType();
      if (NotYetExpanded) {
        FullySubstituted = false;
        QualType Out = getDerived().RebuildPackIndexingType(
            Pack, IndexExpr.get(), SourceLocation(), TL.getEllipsisLoc(),
            FullySubstituted);
        if (Out.isNull())
          return QualType();

        PackIndexingTypeLoc Loc = TLB.push<PackIndexingTypeLoc>(Out);
        Loc.setEllipsisLoc(TL.getEllipsisLoc());
        return Out;
      }
      SubtitutedTypes.push_back(Pack);
      continue;
    }
    for (unsigned I = 0; I != *NumExpansions; ++I) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
      QualType Out = getDerived().TransformType(T);
      if (Out.isNull())
        return QualType();
      SubtitutedTypes.push_back(Out);
    }
    // If we're supposed to retain a pack expansion, do so by temporarily
    // forgetting the partially-substituted parameter pack.
    if (RetainExpansion) {
      FullySubstituted = false;
      ForgetPartiallySubstitutedPackRAII Forget(getDerived());
      QualType Out = getDerived().TransformType(T);
      if (Out.isNull())
        return QualType();
      SubtitutedTypes.push_back(Out);
    }
  }

  // A pack indexing type can appear in a larger pack expansion,
  // e.g. `Pack...[pack_of_indexes]...`
  // so we need to temporarily disable substitution of pack elements
  Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
  QualType Result = getDerived().TransformType(TLB, TL.getPatternLoc());

  QualType Out = getDerived().RebuildPackIndexingType(
      Result, IndexExpr.get(), SourceLocation(), TL.getEllipsisLoc(),
      FullySubstituted, SubtitutedTypes);
  if (Out.isNull())
    return Out;

  PackIndexingTypeLoc Loc = TLB.push<PackIndexingTypeLoc>(Out);
  Loc.setEllipsisLoc(TL.getEllipsisLoc());
  return Out;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformUnaryTransformType(
                                                            TypeLocBuilder &TLB,
                                                     UnaryTransformTypeLoc TL) {
  QualType Result = TL.getType();
  if (Result->isDependentType()) {
    const UnaryTransformType *T = TL.getTypePtr();

    TypeSourceInfo *NewBaseTSI =
        getDerived().TransformType(TL.getUnderlyingTInfo());
    if (!NewBaseTSI)
      return QualType();
    QualType NewBase = NewBaseTSI->getType();

    Result = getDerived().RebuildUnaryTransformType(NewBase,
                                                    T->getUTTKind(),
                                                    TL.getKWLoc());
    if (Result.isNull())
      return QualType();
  }

  UnaryTransformTypeLoc NewTL = TLB.push<UnaryTransformTypeLoc>(Result);
  NewTL.setKWLoc(TL.getKWLoc());
  NewTL.setParensRange(TL.getParensRange());
  NewTL.setUnderlyingTInfo(TL.getUnderlyingTInfo());
  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformDeducedTemplateSpecializationType(
    TypeLocBuilder &TLB, DeducedTemplateSpecializationTypeLoc TL) {
  const DeducedTemplateSpecializationType *T = TL.getTypePtr();

  CXXScopeSpec SS;
  TemplateName TemplateName = getDerived().TransformTemplateName(
      SS, T->getTemplateName(), TL.getTemplateNameLoc());
  if (TemplateName.isNull())
    return QualType();

  QualType OldDeduced = T->getDeducedType();
  QualType NewDeduced;
  if (!OldDeduced.isNull()) {
    NewDeduced = getDerived().TransformType(OldDeduced);
    if (NewDeduced.isNull())
      return QualType();
  }

  QualType Result = getDerived().RebuildDeducedTemplateSpecializationType(
      TemplateName, NewDeduced);
  if (Result.isNull())
    return QualType();

  DeducedTemplateSpecializationTypeLoc NewTL =
      TLB.push<DeducedTemplateSpecializationTypeLoc>(Result);
  NewTL.setTemplateNameLoc(TL.getTemplateNameLoc());

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformRecordType(TypeLocBuilder &TLB,
                                                     RecordTypeLoc TL) {
  const RecordType *T = TL.getTypePtr();
  RecordDecl *Record
    = cast_or_null<RecordDecl>(getDerived().TransformDecl(TL.getNameLoc(),
                                                          T->getDecl()));
  if (!Record)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      Record != T->getDecl()) {
    Result = getDerived().RebuildRecordType(Record);
    if (Result.isNull())
      return QualType();
  }

  RecordTypeLoc NewTL = TLB.push<RecordTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformEnumType(TypeLocBuilder &TLB,
                                                   EnumTypeLoc TL) {
  const EnumType *T = TL.getTypePtr();
  EnumDecl *Enum
    = cast_or_null<EnumDecl>(getDerived().TransformDecl(TL.getNameLoc(),
                                                        T->getDecl()));
  if (!Enum)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      Enum != T->getDecl()) {
    Result = getDerived().RebuildEnumType(Enum);
    if (Result.isNull())
      return QualType();
  }

  EnumTypeLoc NewTL = TLB.push<EnumTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformInjectedClassNameType(
                                         TypeLocBuilder &TLB,
                                         InjectedClassNameTypeLoc TL) {
  Decl *D = getDerived().TransformDecl(TL.getNameLoc(),
                                       TL.getTypePtr()->getDecl());
  if (!D) return QualType();

  QualType T = SemaRef.Context.getTypeDeclType(cast<TypeDecl>(D));
  TLB.pushTypeSpec(T).setNameLoc(TL.getNameLoc());
  return T;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformTemplateTypeParmType(
                                                TypeLocBuilder &TLB,
                                                TemplateTypeParmTypeLoc TL) {
  return getDerived().TransformTemplateTypeParmType(
      TLB, TL,
      /*SuppressObjCLifetime=*/false);
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformTemplateTypeParmType(
    TypeLocBuilder &TLB, TemplateTypeParmTypeLoc TL, bool) {
  return TransformTypeSpecType(TLB, TL);
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformSubstTemplateTypeParmType(
                                         TypeLocBuilder &TLB,
                                         SubstTemplateTypeParmTypeLoc TL) {
  const SubstTemplateTypeParmType *T = TL.getTypePtr();

  Decl *NewReplaced =
      getDerived().TransformDecl(TL.getNameLoc(), T->getAssociatedDecl());

  // Substitute into the replacement type, which itself might involve something
  // that needs to be transformed. This only tends to occur with default
  // template arguments of template template parameters.
  TemporaryBase Rebase(*this, TL.getNameLoc(), DeclarationName());
  QualType Replacement = getDerived().TransformType(T->getReplacementType());
  if (Replacement.isNull())
    return QualType();

  QualType Result = SemaRef.Context.getSubstTemplateTypeParmType(
      Replacement, NewReplaced, T->getIndex(), T->getPackIndex());

  // Propagate type-source information.
  SubstTemplateTypeParmTypeLoc NewTL
    = TLB.push<SubstTemplateTypeParmTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());
  return Result;

}

template<typename Derived>
QualType TreeTransform<Derived>::TransformSubstTemplateTypeParmPackType(
                                          TypeLocBuilder &TLB,
                                          SubstTemplateTypeParmPackTypeLoc TL) {
  return getDerived().TransformSubstTemplateTypeParmPackType(
      TLB, TL, /*SuppressObjCLifetime=*/false);
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformSubstTemplateTypeParmPackType(
    TypeLocBuilder &TLB, SubstTemplateTypeParmPackTypeLoc TL, bool) {
  return TransformTypeSpecType(TLB, TL);
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformTemplateSpecializationType(
                                                        TypeLocBuilder &TLB,
                                           TemplateSpecializationTypeLoc TL) {
  const TemplateSpecializationType *T = TL.getTypePtr();

  // The nested-name-specifier never matters in a TemplateSpecializationType,
  // because we can't have a dependent nested-name-specifier anyway.
  CXXScopeSpec SS;
  TemplateName Template
    = getDerived().TransformTemplateName(SS, T->getTemplateName(),
                                         TL.getTemplateNameLoc());
  if (Template.isNull())
    return QualType();

  return getDerived().TransformTemplateSpecializationType(TLB, TL, Template);
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformAtomicType(TypeLocBuilder &TLB,
                                                     AtomicTypeLoc TL) {
  QualType ValueType = getDerived().TransformType(TLB, TL.getValueLoc());
  if (ValueType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      ValueType != TL.getValueLoc().getType()) {
    Result = getDerived().RebuildAtomicType(ValueType, TL.getKWLoc());
    if (Result.isNull())
      return QualType();
  }

  AtomicTypeLoc NewTL = TLB.push<AtomicTypeLoc>(Result);
  NewTL.setKWLoc(TL.getKWLoc());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformPipeType(TypeLocBuilder &TLB,
                                                   PipeTypeLoc TL) {
  QualType ValueType = getDerived().TransformType(TLB, TL.getValueLoc());
  if (ValueType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ValueType != TL.getValueLoc().getType()) {
    const PipeType *PT = Result->castAs<PipeType>();
    bool isReadPipe = PT->isReadOnly();
    Result = getDerived().RebuildPipeType(ValueType, TL.getKWLoc(), isReadPipe);
    if (Result.isNull())
      return QualType();
  }

  PipeTypeLoc NewTL = TLB.push<PipeTypeLoc>(Result);
  NewTL.setKWLoc(TL.getKWLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformBitIntType(TypeLocBuilder &TLB,
                                                     BitIntTypeLoc TL) {
  const BitIntType *EIT = TL.getTypePtr();
  QualType Result = TL.getType();

  if (getDerived().AlwaysRebuild()) {
    Result = getDerived().RebuildBitIntType(EIT->isUnsigned(),
                                            EIT->getNumBits(), TL.getNameLoc());
    if (Result.isNull())
      return QualType();
  }

  BitIntTypeLoc NewTL = TLB.push<BitIntTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformDependentBitIntType(
    TypeLocBuilder &TLB, DependentBitIntTypeLoc TL) {
  const DependentBitIntType *EIT = TL.getTypePtr();

  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  ExprResult BitsExpr = getDerived().TransformExpr(EIT->getNumBitsExpr());
  BitsExpr = SemaRef.ActOnConstantExpression(BitsExpr);

  if (BitsExpr.isInvalid())
    return QualType();

  QualType Result = TL.getType();

  if (getDerived().AlwaysRebuild() || BitsExpr.get() != EIT->getNumBitsExpr()) {
    Result = getDerived().RebuildDependentBitIntType(
        EIT->isUnsigned(), BitsExpr.get(), TL.getNameLoc());

    if (Result.isNull())
      return QualType();
  }

  if (isa<DependentBitIntType>(Result)) {
    DependentBitIntTypeLoc NewTL = TLB.push<DependentBitIntTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
  } else {
    BitIntTypeLoc NewTL = TLB.push<BitIntTypeLoc>(Result);
    NewTL.setNameLoc(TL.getNameLoc());
  }
  return Result;
}

  /// Simple iterator that traverses the template arguments in a
  /// container that provides a \c getArgLoc() member function.
  ///
  /// This iterator is intended to be used with the iterator form of
  /// \c TreeTransform<Derived>::TransformTemplateArguments().
  template<typename ArgLocContainer>
  class TemplateArgumentLocContainerIterator {
    ArgLocContainer *Container;
    unsigned Index;

  public:
    typedef TemplateArgumentLoc value_type;
    typedef TemplateArgumentLoc reference;
    typedef int difference_type;
    typedef std::input_iterator_tag iterator_category;

    class pointer {
      TemplateArgumentLoc Arg;

    public:
      explicit pointer(TemplateArgumentLoc Arg) : Arg(Arg) { }

      const TemplateArgumentLoc *operator->() const {
        return &Arg;
      }
    };


    TemplateArgumentLocContainerIterator() {}

    TemplateArgumentLocContainerIterator(ArgLocContainer &Container,
                                 unsigned Index)
      : Container(&Container), Index(Index) { }

    TemplateArgumentLocContainerIterator &operator++() {
      ++Index;
      return *this;
    }

    TemplateArgumentLocContainerIterator operator++(int) {
      TemplateArgumentLocContainerIterator Old(*this);
      ++(*this);
      return Old;
    }

    TemplateArgumentLoc operator*() const {
      return Container->getArgLoc(Index);
    }

    pointer operator->() const {
      return pointer(Container->getArgLoc(Index));
    }

    friend bool operator==(const TemplateArgumentLocContainerIterator &X,
                           const TemplateArgumentLocContainerIterator &Y) {
      return X.Container == Y.Container && X.Index == Y.Index;
    }

    friend bool operator!=(const TemplateArgumentLocContainerIterator &X,
                           const TemplateArgumentLocContainerIterator &Y) {
      return !(X == Y);
    }
  };

template<typename Derived>
QualType TreeTransform<Derived>::TransformAutoType(TypeLocBuilder &TLB,
                                                   AutoTypeLoc TL) {
  const AutoType *T = TL.getTypePtr();
  QualType OldDeduced = T->getDeducedType();
  QualType NewDeduced;
  if (!OldDeduced.isNull()) {
    NewDeduced = getDerived().TransformType(OldDeduced);
    if (NewDeduced.isNull())
      return QualType();
  }

  ConceptDecl *NewCD = nullptr;
  TemplateArgumentListInfo NewTemplateArgs;
  NestedNameSpecifierLoc NewNestedNameSpec;
  if (T->isConstrained()) {
    assert(TL.getConceptReference());
    NewCD = cast_or_null<ConceptDecl>(getDerived().TransformDecl(
        TL.getConceptNameLoc(), T->getTypeConstraintConcept()));

    NewTemplateArgs.setLAngleLoc(TL.getLAngleLoc());
    NewTemplateArgs.setRAngleLoc(TL.getRAngleLoc());
    typedef TemplateArgumentLocContainerIterator<AutoTypeLoc> ArgIterator;
    if (getDerived().TransformTemplateArguments(
            ArgIterator(TL, 0), ArgIterator(TL, TL.getNumArgs()),
            NewTemplateArgs))
      return QualType();

    if (TL.getNestedNameSpecifierLoc()) {
      NewNestedNameSpec
        = getDerived().TransformNestedNameSpecifierLoc(
            TL.getNestedNameSpecifierLoc());
      if (!NewNestedNameSpec)
        return QualType();
    }
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || NewDeduced != OldDeduced ||
      T->isDependentType() || T->isConstrained()) {
    // FIXME: Maybe don't rebuild if all template arguments are the same.
    llvm::SmallVector<TemplateArgument, 4> NewArgList;
    NewArgList.reserve(NewTemplateArgs.size());
    for (const auto &ArgLoc : NewTemplateArgs.arguments())
      NewArgList.push_back(ArgLoc.getArgument());
    Result = getDerived().RebuildAutoType(NewDeduced, T->getKeyword(), NewCD,
                                          NewArgList);
    if (Result.isNull())
      return QualType();
  }

  AutoTypeLoc NewTL = TLB.push<AutoTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setConceptReference(nullptr);

  if (T->isConstrained()) {
    DeclarationNameInfo DNI = DeclarationNameInfo(
        TL.getTypePtr()->getTypeConstraintConcept()->getDeclName(),
        TL.getConceptNameLoc(),
        TL.getTypePtr()->getTypeConstraintConcept()->getDeclName());
    auto *CR = ConceptReference::Create(
        SemaRef.Context, NewNestedNameSpec, TL.getTemplateKWLoc(), DNI,
        TL.getFoundDecl(), TL.getTypePtr()->getTypeConstraintConcept(),
        ASTTemplateArgumentListInfo::Create(SemaRef.Context, NewTemplateArgs));
    NewTL.setConceptReference(CR);
  }

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformTemplateSpecializationType(
                                                        TypeLocBuilder &TLB,
                                           TemplateSpecializationTypeLoc TL,
                                                      TemplateName Template) {
  TemplateArgumentListInfo NewTemplateArgs;
  NewTemplateArgs.setLAngleLoc(TL.getLAngleLoc());
  NewTemplateArgs.setRAngleLoc(TL.getRAngleLoc());
  typedef TemplateArgumentLocContainerIterator<TemplateSpecializationTypeLoc>
    ArgIterator;
  if (getDerived().TransformTemplateArguments(ArgIterator(TL, 0),
                                              ArgIterator(TL, TL.getNumArgs()),
                                              NewTemplateArgs))
    return QualType();

  // FIXME: maybe don't rebuild if all the template arguments are the same.

  QualType Result =
    getDerived().RebuildTemplateSpecializationType(Template,
                                                   TL.getTemplateNameLoc(),
                                                   NewTemplateArgs);

  if (!Result.isNull()) {
    // Specializations of template template parameters are represented as
    // TemplateSpecializationTypes, and substitution of type alias templates
    // within a dependent context can transform them into
    // DependentTemplateSpecializationTypes.
    if (isa<DependentTemplateSpecializationType>(Result)) {
      DependentTemplateSpecializationTypeLoc NewTL
        = TLB.push<DependentTemplateSpecializationTypeLoc>(Result);
      NewTL.setElaboratedKeywordLoc(SourceLocation());
      NewTL.setQualifierLoc(NestedNameSpecifierLoc());
      NewTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
      NewTL.setTemplateNameLoc(TL.getTemplateNameLoc());
      NewTL.setLAngleLoc(TL.getLAngleLoc());
      NewTL.setRAngleLoc(TL.getRAngleLoc());
      for (unsigned i = 0, e = NewTemplateArgs.size(); i != e; ++i)
        NewTL.setArgLocInfo(i, NewTemplateArgs[i].getLocInfo());
      return Result;
    }

    TemplateSpecializationTypeLoc NewTL
      = TLB.push<TemplateSpecializationTypeLoc>(Result);
    NewTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
    NewTL.setTemplateNameLoc(TL.getTemplateNameLoc());
    NewTL.setLAngleLoc(TL.getLAngleLoc());
    NewTL.setRAngleLoc(TL.getRAngleLoc());
    for (unsigned i = 0, e = NewTemplateArgs.size(); i != e; ++i)
      NewTL.setArgLocInfo(i, NewTemplateArgs[i].getLocInfo());
  }

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformDependentTemplateSpecializationType(
                                     TypeLocBuilder &TLB,
                                     DependentTemplateSpecializationTypeLoc TL,
                                     TemplateName Template,
                                     CXXScopeSpec &SS) {
  TemplateArgumentListInfo NewTemplateArgs;
  NewTemplateArgs.setLAngleLoc(TL.getLAngleLoc());
  NewTemplateArgs.setRAngleLoc(TL.getRAngleLoc());
  typedef TemplateArgumentLocContainerIterator<
            DependentTemplateSpecializationTypeLoc> ArgIterator;
  if (getDerived().TransformTemplateArguments(ArgIterator(TL, 0),
                                              ArgIterator(TL, TL.getNumArgs()),
                                              NewTemplateArgs))
    return QualType();

  // FIXME: maybe don't rebuild if all the template arguments are the same.

  if (DependentTemplateName *DTN = Template.getAsDependentTemplateName()) {
    QualType Result = getSema().Context.getDependentTemplateSpecializationType(
        TL.getTypePtr()->getKeyword(), DTN->getQualifier(),
        DTN->getIdentifier(), NewTemplateArgs.arguments());

    DependentTemplateSpecializationTypeLoc NewTL
      = TLB.push<DependentTemplateSpecializationTypeLoc>(Result);
    NewTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
    NewTL.setQualifierLoc(SS.getWithLocInContext(SemaRef.Context));
    NewTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
    NewTL.setTemplateNameLoc(TL.getTemplateNameLoc());
    NewTL.setLAngleLoc(TL.getLAngleLoc());
    NewTL.setRAngleLoc(TL.getRAngleLoc());
    for (unsigned i = 0, e = NewTemplateArgs.size(); i != e; ++i)
      NewTL.setArgLocInfo(i, NewTemplateArgs[i].getLocInfo());
    return Result;
  }

  QualType Result
    = getDerived().RebuildTemplateSpecializationType(Template,
                                                     TL.getTemplateNameLoc(),
                                                     NewTemplateArgs);

  if (!Result.isNull()) {
    /// FIXME: Wrap this in an elaborated-type-specifier?
    TemplateSpecializationTypeLoc NewTL
      = TLB.push<TemplateSpecializationTypeLoc>(Result);
    NewTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
    NewTL.setTemplateNameLoc(TL.getTemplateNameLoc());
    NewTL.setLAngleLoc(TL.getLAngleLoc());
    NewTL.setRAngleLoc(TL.getRAngleLoc());
    for (unsigned i = 0, e = NewTemplateArgs.size(); i != e; ++i)
      NewTL.setArgLocInfo(i, NewTemplateArgs[i].getLocInfo());
  }

  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformElaboratedType(TypeLocBuilder &TLB,
                                                ElaboratedTypeLoc TL) {
  const ElaboratedType *T = TL.getTypePtr();

  NestedNameSpecifierLoc QualifierLoc;
  // NOTE: the qualifier in an ElaboratedType is optional.
  if (TL.getQualifierLoc()) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(TL.getQualifierLoc());
    if (!QualifierLoc)
      return QualType();
  }

  QualType NamedT = getDerived().TransformType(TLB, TL.getNamedTypeLoc());
  if (NamedT.isNull())
    return QualType();

  // C++0x [dcl.type.elab]p2:
  //   If the identifier resolves to a typedef-name or the simple-template-id
  //   resolves to an alias template specialization, the
  //   elaborated-type-specifier is ill-formed.
  if (T->getKeyword() != ElaboratedTypeKeyword::None &&
      T->getKeyword() != ElaboratedTypeKeyword::Typename) {
    if (const TemplateSpecializationType *TST =
          NamedT->getAs<TemplateSpecializationType>()) {
      TemplateName Template = TST->getTemplateName();
      if (TypeAliasTemplateDecl *TAT = dyn_cast_or_null<TypeAliasTemplateDecl>(
              Template.getAsTemplateDecl())) {
        SemaRef.Diag(TL.getNamedTypeLoc().getBeginLoc(),
                     diag::err_tag_reference_non_tag)
            << TAT << Sema::NTK_TypeAliasTemplate
            << llvm::to_underlying(
                   ElaboratedType::getTagTypeKindForKeyword(T->getKeyword()));
        SemaRef.Diag(TAT->getLocation(), diag::note_declared_at);
      }
    }
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      QualifierLoc != TL.getQualifierLoc() ||
      NamedT != T->getNamedType()) {
    Result = getDerived().RebuildElaboratedType(TL.getElaboratedKeywordLoc(),
                                                T->getKeyword(),
                                                QualifierLoc, NamedT);
    if (Result.isNull())
      return QualType();
  }

  ElaboratedTypeLoc NewTL = TLB.push<ElaboratedTypeLoc>(Result);
  NewTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
  NewTL.setQualifierLoc(QualifierLoc);
  return Result;
}

template <typename Derived>
template <typename Fn>
QualType TreeTransform<Derived>::TransformAttributedType(
    TypeLocBuilder &TLB, AttributedTypeLoc TL, Fn TransformModifiedTypeFn) {
  const AttributedType *oldType = TL.getTypePtr();
  QualType modifiedType = TransformModifiedTypeFn(TLB, TL.getModifiedLoc());
  if (modifiedType.isNull())
    return QualType();

  // oldAttr can be null if we started with a QualType rather than a TypeLoc.
  const Attr *oldAttr = TL.getAttr();
  const Attr *newAttr = oldAttr ? getDerived().TransformAttr(oldAttr) : nullptr;
  if (oldAttr && !newAttr)
    return QualType();

  QualType result = TL.getType();

  // FIXME: dependent operand expressions?
  if (getDerived().AlwaysRebuild() ||
      modifiedType != oldType->getModifiedType()) {
    TypeLocBuilder AuxiliaryTLB;
    AuxiliaryTLB.reserve(TL.getFullDataSize());
    QualType equivalentType =
        getDerived().TransformType(AuxiliaryTLB, TL.getEquivalentTypeLoc());
    if (equivalentType.isNull())
      return QualType();

    // Check whether we can add nullability; it is only represented as
    // type sugar, and therefore cannot be diagnosed in any other way.
    if (auto nullability = oldType->getImmediateNullability()) {
      if (!modifiedType->canHaveNullability()) {
        SemaRef.Diag((TL.getAttr() ? TL.getAttr()->getLocation()
                                   : TL.getModifiedLoc().getBeginLoc()),
                     diag::err_nullability_nonpointer)
            << DiagNullabilityKind(*nullability, false) << modifiedType;
        return QualType();
      }
    }

    result = SemaRef.Context.getAttributedType(TL.getAttrKind(),
                                               modifiedType,
                                               equivalentType);
  }

  AttributedTypeLoc newTL = TLB.push<AttributedTypeLoc>(result);
  newTL.setAttr(newAttr);
  return result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformAttributedType(TypeLocBuilder &TLB,
                                                         AttributedTypeLoc TL) {
  return getDerived().TransformAttributedType(
      TLB, TL, [&](TypeLocBuilder &TLB, TypeLoc ModifiedLoc) -> QualType {
        return getDerived().TransformType(TLB, ModifiedLoc);
      });
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformCountAttributedType(
    TypeLocBuilder &TLB, CountAttributedTypeLoc TL) {
  const CountAttributedType *OldTy = TL.getTypePtr();
  QualType InnerTy = getDerived().TransformType(TLB, TL.getInnerLoc());
  if (InnerTy.isNull())
    return QualType();

  Expr *OldCount = TL.getCountExpr();
  Expr *NewCount = nullptr;
  if (OldCount) {
    ExprResult CountResult = getDerived().TransformExpr(OldCount);
    if (CountResult.isInvalid())
      return QualType();
    NewCount = CountResult.get();
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || InnerTy != OldTy->desugar() ||
      OldCount != NewCount) {
    // Currently, CountAttributedType can only wrap incomplete array types.
    Result = SemaRef.BuildCountAttributedArrayOrPointerType(
        InnerTy, NewCount, OldTy->isCountInBytes(), OldTy->isOrNull());
  }

  TLB.push<CountAttributedTypeLoc>(Result);
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformBTFTagAttributedType(
    TypeLocBuilder &TLB, BTFTagAttributedTypeLoc TL) {
  // The BTFTagAttributedType is available for C only.
  llvm_unreachable("Unexpected TreeTransform for BTFTagAttributedType");
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformParenType(TypeLocBuilder &TLB,
                                           ParenTypeLoc TL) {
  QualType Inner = getDerived().TransformType(TLB, TL.getInnerLoc());
  if (Inner.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      Inner != TL.getInnerLoc().getType()) {
    Result = getDerived().RebuildParenType(Inner);
    if (Result.isNull())
      return QualType();
  }

  ParenTypeLoc NewTL = TLB.push<ParenTypeLoc>(Result);
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformMacroQualifiedType(TypeLocBuilder &TLB,
                                                    MacroQualifiedTypeLoc TL) {
  QualType Inner = getDerived().TransformType(TLB, TL.getInnerLoc());
  if (Inner.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Inner != TL.getInnerLoc().getType()) {
    Result =
        getDerived().RebuildMacroQualifiedType(Inner, TL.getMacroIdentifier());
    if (Result.isNull())
      return QualType();
  }

  MacroQualifiedTypeLoc NewTL = TLB.push<MacroQualifiedTypeLoc>(Result);
  NewTL.setExpansionLoc(TL.getExpansionLoc());
  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformDependentNameType(
    TypeLocBuilder &TLB, DependentNameTypeLoc TL) {
  return TransformDependentNameType(TLB, TL, false);
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformDependentNameType(
    TypeLocBuilder &TLB, DependentNameTypeLoc TL, bool DeducedTSTContext) {
  const DependentNameType *T = TL.getTypePtr();

  NestedNameSpecifierLoc QualifierLoc
    = getDerived().TransformNestedNameSpecifierLoc(TL.getQualifierLoc());
  if (!QualifierLoc)
    return QualType();

  QualType Result
    = getDerived().RebuildDependentNameType(T->getKeyword(),
                                            TL.getElaboratedKeywordLoc(),
                                            QualifierLoc,
                                            T->getIdentifier(),
                                            TL.getNameLoc(),
                                            DeducedTSTContext);
  if (Result.isNull())
    return QualType();

  if (const ElaboratedType* ElabT = Result->getAs<ElaboratedType>()) {
    QualType NamedT = ElabT->getNamedType();
    TLB.pushTypeSpec(NamedT).setNameLoc(TL.getNameLoc());

    ElaboratedTypeLoc NewTL = TLB.push<ElaboratedTypeLoc>(Result);
    NewTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
    NewTL.setQualifierLoc(QualifierLoc);
  } else {
    DependentNameTypeLoc NewTL = TLB.push<DependentNameTypeLoc>(Result);
    NewTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
    NewTL.setQualifierLoc(QualifierLoc);
    NewTL.setNameLoc(TL.getNameLoc());
  }
  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::
          TransformDependentTemplateSpecializationType(TypeLocBuilder &TLB,
                                 DependentTemplateSpecializationTypeLoc TL) {
  NestedNameSpecifierLoc QualifierLoc;
  if (TL.getQualifierLoc()) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(TL.getQualifierLoc());
    if (!QualifierLoc)
      return QualType();
  }

  return getDerived()
           .TransformDependentTemplateSpecializationType(TLB, TL, QualifierLoc);
}

template<typename Derived>
QualType TreeTransform<Derived>::
TransformDependentTemplateSpecializationType(TypeLocBuilder &TLB,
                                   DependentTemplateSpecializationTypeLoc TL,
                                       NestedNameSpecifierLoc QualifierLoc) {
  const DependentTemplateSpecializationType *T = TL.getTypePtr();

  TemplateArgumentListInfo NewTemplateArgs;
  NewTemplateArgs.setLAngleLoc(TL.getLAngleLoc());
  NewTemplateArgs.setRAngleLoc(TL.getRAngleLoc());

  typedef TemplateArgumentLocContainerIterator<
  DependentTemplateSpecializationTypeLoc> ArgIterator;
  if (getDerived().TransformTemplateArguments(ArgIterator(TL, 0),
                                              ArgIterator(TL, TL.getNumArgs()),
                                              NewTemplateArgs))
    return QualType();

  QualType Result = getDerived().RebuildDependentTemplateSpecializationType(
      T->getKeyword(), QualifierLoc, TL.getTemplateKeywordLoc(),
      T->getIdentifier(), TL.getTemplateNameLoc(), NewTemplateArgs,
      /*AllowInjectedClassName*/ false);
  if (Result.isNull())
    return QualType();

  if (const ElaboratedType *ElabT = dyn_cast<ElaboratedType>(Result)) {
    QualType NamedT = ElabT->getNamedType();

    // Copy information relevant to the template specialization.
    TemplateSpecializationTypeLoc NamedTL
      = TLB.push<TemplateSpecializationTypeLoc>(NamedT);
    NamedTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
    NamedTL.setTemplateNameLoc(TL.getTemplateNameLoc());
    NamedTL.setLAngleLoc(TL.getLAngleLoc());
    NamedTL.setRAngleLoc(TL.getRAngleLoc());
    for (unsigned I = 0, E = NewTemplateArgs.size(); I != E; ++I)
      NamedTL.setArgLocInfo(I, NewTemplateArgs[I].getLocInfo());

    // Copy information relevant to the elaborated type.
    ElaboratedTypeLoc NewTL = TLB.push<ElaboratedTypeLoc>(Result);
    NewTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
    NewTL.setQualifierLoc(QualifierLoc);
  } else if (isa<DependentTemplateSpecializationType>(Result)) {
    DependentTemplateSpecializationTypeLoc SpecTL
      = TLB.push<DependentTemplateSpecializationTypeLoc>(Result);
    SpecTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
    SpecTL.setQualifierLoc(QualifierLoc);
    SpecTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
    SpecTL.setTemplateNameLoc(TL.getTemplateNameLoc());
    SpecTL.setLAngleLoc(TL.getLAngleLoc());
    SpecTL.setRAngleLoc(TL.getRAngleLoc());
    for (unsigned I = 0, E = NewTemplateArgs.size(); I != E; ++I)
      SpecTL.setArgLocInfo(I, NewTemplateArgs[I].getLocInfo());
  } else {
    TemplateSpecializationTypeLoc SpecTL
      = TLB.push<TemplateSpecializationTypeLoc>(Result);
    SpecTL.setTemplateKeywordLoc(TL.getTemplateKeywordLoc());
    SpecTL.setTemplateNameLoc(TL.getTemplateNameLoc());
    SpecTL.setLAngleLoc(TL.getLAngleLoc());
    SpecTL.setRAngleLoc(TL.getRAngleLoc());
    for (unsigned I = 0, E = NewTemplateArgs.size(); I != E; ++I)
      SpecTL.setArgLocInfo(I, NewTemplateArgs[I].getLocInfo());
  }
  return Result;
}

template<typename Derived>
QualType TreeTransform<Derived>::TransformPackExpansionType(TypeLocBuilder &TLB,
                                                      PackExpansionTypeLoc TL) {
  QualType Pattern
    = getDerived().TransformType(TLB, TL.getPatternLoc());
  if (Pattern.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      Pattern != TL.getPatternLoc().getType()) {
    Result = getDerived().RebuildPackExpansionType(Pattern,
                                           TL.getPatternLoc().getSourceRange(),
                                                   TL.getEllipsisLoc(),
                                           TL.getTypePtr()->getNumExpansions());
    if (Result.isNull())
      return QualType();
  }

  PackExpansionTypeLoc NewT = TLB.push<PackExpansionTypeLoc>(Result);
  NewT.setEllipsisLoc(TL.getEllipsisLoc());
  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformObjCInterfaceType(TypeLocBuilder &TLB,
                                                   ObjCInterfaceTypeLoc TL) {
  // ObjCInterfaceType is never dependent.
  TLB.pushFullCopy(TL);
  return TL.getType();
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformObjCTypeParamType(TypeLocBuilder &TLB,
                                                   ObjCTypeParamTypeLoc TL) {
  const ObjCTypeParamType *T = TL.getTypePtr();
  ObjCTypeParamDecl *OTP = cast_or_null<ObjCTypeParamDecl>(
      getDerived().TransformDecl(T->getDecl()->getLocation(), T->getDecl()));
  if (!OTP)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      OTP != T->getDecl()) {
    Result = getDerived().RebuildObjCTypeParamType(
        OTP, TL.getProtocolLAngleLoc(),
        llvm::ArrayRef(TL.getTypePtr()->qual_begin(), TL.getNumProtocols()),
        TL.getProtocolLocs(), TL.getProtocolRAngleLoc());
    if (Result.isNull())
      return QualType();
  }

  ObjCTypeParamTypeLoc NewTL = TLB.push<ObjCTypeParamTypeLoc>(Result);
  if (TL.getNumProtocols()) {
    NewTL.setProtocolLAngleLoc(TL.getProtocolLAngleLoc());
    for (unsigned i = 0, n = TL.getNumProtocols(); i != n; ++i)
      NewTL.setProtocolLoc(i, TL.getProtocolLoc(i));
    NewTL.setProtocolRAngleLoc(TL.getProtocolRAngleLoc());
  }
  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformObjCObjectType(TypeLocBuilder &TLB,
                                                ObjCObjectTypeLoc TL) {
  // Transform base type.
  QualType BaseType = getDerived().TransformType(TLB, TL.getBaseLoc());
  if (BaseType.isNull())
    return QualType();

  bool AnyChanged = BaseType != TL.getBaseLoc().getType();

  // Transform type arguments.
  SmallVector<TypeSourceInfo *, 4> NewTypeArgInfos;
  for (unsigned i = 0, n = TL.getNumTypeArgs(); i != n; ++i) {
    TypeSourceInfo *TypeArgInfo = TL.getTypeArgTInfo(i);
    TypeLoc TypeArgLoc = TypeArgInfo->getTypeLoc();
    QualType TypeArg = TypeArgInfo->getType();
    if (auto PackExpansionLoc = TypeArgLoc.getAs<PackExpansionTypeLoc>()) {
      AnyChanged = true;

      // We have a pack expansion. Instantiate it.
      const auto *PackExpansion = PackExpansionLoc.getType()
                                    ->castAs<PackExpansionType>();
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      SemaRef.collectUnexpandedParameterPacks(PackExpansion->getPattern(),
                                              Unexpanded);
      assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

      // Determine whether the set of unexpanded parameter packs can
      // and should be expanded.
      TypeLoc PatternLoc = PackExpansionLoc.getPatternLoc();
      bool Expand = false;
      bool RetainExpansion = false;
      std::optional<unsigned> NumExpansions = PackExpansion->getNumExpansions();
      if (getDerived().TryExpandParameterPacks(
            PackExpansionLoc.getEllipsisLoc(), PatternLoc.getSourceRange(),
            Unexpanded, Expand, RetainExpansion, NumExpansions))
        return QualType();

      if (!Expand) {
        // We can't expand this pack expansion into separate arguments yet;
        // just substitute into the pattern and create a new pack expansion
        // type.
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);

        TypeLocBuilder TypeArgBuilder;
        TypeArgBuilder.reserve(PatternLoc.getFullDataSize());
        QualType NewPatternType = getDerived().TransformType(TypeArgBuilder,
                                                             PatternLoc);
        if (NewPatternType.isNull())
          return QualType();

        QualType NewExpansionType = SemaRef.Context.getPackExpansionType(
                                      NewPatternType, NumExpansions);
        auto NewExpansionLoc = TLB.push<PackExpansionTypeLoc>(NewExpansionType);
        NewExpansionLoc.setEllipsisLoc(PackExpansionLoc.getEllipsisLoc());
        NewTypeArgInfos.push_back(
          TypeArgBuilder.getTypeSourceInfo(SemaRef.Context, NewExpansionType));
        continue;
      }

      // Substitute into the pack expansion pattern for each slice of the
      // pack.
      for (unsigned ArgIdx = 0; ArgIdx != *NumExpansions; ++ArgIdx) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), ArgIdx);

        TypeLocBuilder TypeArgBuilder;
        TypeArgBuilder.reserve(PatternLoc.getFullDataSize());

        QualType NewTypeArg = getDerived().TransformType(TypeArgBuilder,
                                                         PatternLoc);
        if (NewTypeArg.isNull())
          return QualType();

        NewTypeArgInfos.push_back(
          TypeArgBuilder.getTypeSourceInfo(SemaRef.Context, NewTypeArg));
      }

      continue;
    }

    TypeLocBuilder TypeArgBuilder;
    TypeArgBuilder.reserve(TypeArgLoc.getFullDataSize());
    QualType NewTypeArg =
        getDerived().TransformType(TypeArgBuilder, TypeArgLoc);
    if (NewTypeArg.isNull())
      return QualType();

    // If nothing changed, just keep the old TypeSourceInfo.
    if (NewTypeArg == TypeArg) {
      NewTypeArgInfos.push_back(TypeArgInfo);
      continue;
    }

    NewTypeArgInfos.push_back(
      TypeArgBuilder.getTypeSourceInfo(SemaRef.Context, NewTypeArg));
    AnyChanged = true;
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || AnyChanged) {
    // Rebuild the type.
    Result = getDerived().RebuildObjCObjectType(
        BaseType, TL.getBeginLoc(), TL.getTypeArgsLAngleLoc(), NewTypeArgInfos,
        TL.getTypeArgsRAngleLoc(), TL.getProtocolLAngleLoc(),
        llvm::ArrayRef(TL.getTypePtr()->qual_begin(), TL.getNumProtocols()),
        TL.getProtocolLocs(), TL.getProtocolRAngleLoc());

    if (Result.isNull())
      return QualType();
  }

  ObjCObjectTypeLoc NewT = TLB.push<ObjCObjectTypeLoc>(Result);
  NewT.setHasBaseTypeAsWritten(true);
  NewT.setTypeArgsLAngleLoc(TL.getTypeArgsLAngleLoc());
  for (unsigned i = 0, n = TL.getNumTypeArgs(); i != n; ++i)
    NewT.setTypeArgTInfo(i, NewTypeArgInfos[i]);
  NewT.setTypeArgsRAngleLoc(TL.getTypeArgsRAngleLoc());
  NewT.setProtocolLAngleLoc(TL.getProtocolLAngleLoc());
  for (unsigned i = 0, n = TL.getNumProtocols(); i != n; ++i)
    NewT.setProtocolLoc(i, TL.getProtocolLoc(i));
  NewT.setProtocolRAngleLoc(TL.getProtocolRAngleLoc());
  return Result;
}

template<typename Derived>
QualType
TreeTransform<Derived>::TransformObjCObjectPointerType(TypeLocBuilder &TLB,
                                               ObjCObjectPointerTypeLoc TL) {
  QualType PointeeType = getDerived().TransformType(TLB, TL.getPointeeLoc());
  if (PointeeType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      PointeeType != TL.getPointeeLoc().getType()) {
    Result = getDerived().RebuildObjCObjectPointerType(PointeeType,
                                                       TL.getStarLoc());
    if (Result.isNull())
      return QualType();
  }

  ObjCObjectPointerTypeLoc NewT = TLB.push<ObjCObjectPointerTypeLoc>(Result);
  NewT.setStarLoc(TL.getStarLoc());
  return Result;
}

//===----------------------------------------------------------------------===//
// Statement transformation
//===----------------------------------------------------------------------===//
template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformNullStmt(NullStmt *S) {
  return S;
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCompoundStmt(CompoundStmt *S) {
  return getDerived().TransformCompoundStmt(S, false);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCompoundStmt(CompoundStmt *S,
                                              bool IsStmtExpr) {
  Sema::CompoundScopeRAII CompoundScope(getSema());
  Sema::FPFeaturesStateRAII FPSave(getSema());
  if (S->hasStoredFPFeatures())
    getSema().resetFPOptions(
        S->getStoredFPFeatures().applyOverrides(getSema().getLangOpts()));

  const Stmt *ExprResult = S->getStmtExprResult();
  bool SubStmtInvalid = false;
  bool SubStmtChanged = false;
  SmallVector<Stmt*, 8> Statements;
  for (auto *B : S->body()) {
    StmtResult Result = getDerived().TransformStmt(
        B, IsStmtExpr && B == ExprResult ? SDK_StmtExprResult : SDK_Discarded);

    if (Result.isInvalid()) {
      // Immediately fail if this was a DeclStmt, since it's very
      // likely that this will cause problems for future statements.
      if (isa<DeclStmt>(B))
        return StmtError();

      // Otherwise, just keep processing substatements and fail later.
      SubStmtInvalid = true;
      continue;
    }

    SubStmtChanged = SubStmtChanged || Result.get() != B;
    Statements.push_back(Result.getAs<Stmt>());
  }

  if (SubStmtInvalid)
    return StmtError();

  if (!getDerived().AlwaysRebuild() &&
      !SubStmtChanged)
    return S;

  return getDerived().RebuildCompoundStmt(S->getLBracLoc(),
                                          Statements,
                                          S->getRBracLoc(),
                                          IsStmtExpr);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCaseStmt(CaseStmt *S) {
  ExprResult LHS, RHS;
  {
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

    // Transform the left-hand case value.
    LHS = getDerived().TransformExpr(S->getLHS());
    LHS = SemaRef.ActOnCaseExpr(S->getCaseLoc(), LHS);
    if (LHS.isInvalid())
      return StmtError();

    // Transform the right-hand case value (for the GNU case-range extension).
    RHS = getDerived().TransformExpr(S->getRHS());
    RHS = SemaRef.ActOnCaseExpr(S->getCaseLoc(), RHS);
    if (RHS.isInvalid())
      return StmtError();
  }

  // Build the case statement.
  // Case statements are always rebuilt so that they will attached to their
  // transformed switch statement.
  StmtResult Case = getDerived().RebuildCaseStmt(S->getCaseLoc(),
                                                       LHS.get(),
                                                       S->getEllipsisLoc(),
                                                       RHS.get(),
                                                       S->getColonLoc());
  if (Case.isInvalid())
    return StmtError();

  // Transform the statement following the case
  StmtResult SubStmt =
      getDerived().TransformStmt(S->getSubStmt());
  if (SubStmt.isInvalid())
    return StmtError();

  // Attach the body to the case statement
  return getDerived().RebuildCaseStmtBody(Case.get(), SubStmt.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformDefaultStmt(DefaultStmt *S) {
  // Transform the statement following the default case
  StmtResult SubStmt =
      getDerived().TransformStmt(S->getSubStmt());
  if (SubStmt.isInvalid())
    return StmtError();

  // Default statements are always rebuilt
  return getDerived().RebuildDefaultStmt(S->getDefaultLoc(), S->getColonLoc(),
                                         SubStmt.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformLabelStmt(LabelStmt *S, StmtDiscardKind SDK) {
  StmtResult SubStmt = getDerived().TransformStmt(S->getSubStmt(), SDK);
  if (SubStmt.isInvalid())
    return StmtError();

  Decl *LD = getDerived().TransformDecl(S->getDecl()->getLocation(),
                                        S->getDecl());
  if (!LD)
    return StmtError();

  // If we're transforming "in-place" (we're not creating new local
  // declarations), assume we're replacing the old label statement
  // and clear out the reference to it.
  if (LD == S->getDecl())
    S->getDecl()->setStmt(nullptr);

  // FIXME: Pass the real colon location in.
  return getDerived().RebuildLabelStmt(S->getIdentLoc(),
                                       cast<LabelDecl>(LD), SourceLocation(),
                                       SubStmt.get());
}

template <typename Derived>
const Attr *TreeTransform<Derived>::TransformAttr(const Attr *R) {
  if (!R)
    return R;

  switch (R->getKind()) {
// Transform attributes by calling TransformXXXAttr.
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    return getDerived().Transform##X##Attr(cast<X##Attr>(R));
#include "clang/Basic/AttrList.inc"
  }
  return R;
}

template <typename Derived>
const Attr *TreeTransform<Derived>::TransformStmtAttr(const Stmt *OrigS,
                                                      const Stmt *InstS,
                                                      const Attr *R) {
  if (!R)
    return R;

  switch (R->getKind()) {
// Transform attributes by calling TransformStmtXXXAttr.
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    return getDerived().TransformStmt##X##Attr(OrigS, InstS, cast<X##Attr>(R));
#include "clang/Basic/AttrList.inc"
  }
  return TransformAttr(R);
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformAttributedStmt(AttributedStmt *S,
                                                StmtDiscardKind SDK) {
  StmtResult SubStmt = getDerived().TransformStmt(S->getSubStmt(), SDK);
  if (SubStmt.isInvalid())
    return StmtError();

  bool AttrsChanged = false;
  SmallVector<const Attr *, 1> Attrs;

  // Visit attributes and keep track if any are transformed.
  for (const auto *I : S->getAttrs()) {
    const Attr *R =
        getDerived().TransformStmtAttr(S->getSubStmt(), SubStmt.get(), I);
    AttrsChanged |= (I != R);
    if (R)
      Attrs.push_back(R);
  }

  if (SubStmt.get() == S->getSubStmt() && !AttrsChanged)
    return S;

  // If transforming the attributes failed for all of the attributes in the
  // statement, don't make an AttributedStmt without attributes.
  if (Attrs.empty())
    return SubStmt;

  return getDerived().RebuildAttributedStmt(S->getAttrLoc(), Attrs,
                                            SubStmt.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformIfStmt(IfStmt *S) {
  // Transform the initialization statement
  StmtResult Init = getDerived().TransformStmt(S->getInit());
  if (Init.isInvalid())
    return StmtError();

  Sema::ConditionResult Cond;
  if (!S->isConsteval()) {
    // Transform the condition
    Cond = getDerived().TransformCondition(
        S->getIfLoc(), S->getConditionVariable(), S->getCond(),
        S->isConstexpr() ? Sema::ConditionKind::ConstexprIf
                         : Sema::ConditionKind::Boolean);
    if (Cond.isInvalid())
      return StmtError();
  }

  // If this is a constexpr if, determine which arm we should instantiate.
  std::optional<bool> ConstexprConditionValue;
  if (S->isConstexpr())
    ConstexprConditionValue = Cond.getKnownValue();

  // Transform the "then" branch.
  StmtResult Then;
  if (!ConstexprConditionValue || *ConstexprConditionValue) {
    EnterExpressionEvaluationContext Ctx(
        getSema(), Sema::ExpressionEvaluationContext::ImmediateFunctionContext,
        nullptr, Sema::ExpressionEvaluationContextRecord::EK_Other,
        S->isNonNegatedConsteval());

    Then = getDerived().TransformStmt(S->getThen());
    if (Then.isInvalid())
      return StmtError();
  } else {
    // Discarded branch is replaced with empty CompoundStmt so we can keep
    // proper source location for start and end of original branch, so
    // subsequent transformations like CoverageMapping work properly
    Then = new (getSema().Context)
        CompoundStmt(S->getThen()->getBeginLoc(), S->getThen()->getEndLoc());
  }

  // Transform the "else" branch.
  StmtResult Else;
  if (!ConstexprConditionValue || !*ConstexprConditionValue) {
    EnterExpressionEvaluationContext Ctx(
        getSema(), Sema::ExpressionEvaluationContext::ImmediateFunctionContext,
        nullptr, Sema::ExpressionEvaluationContextRecord::EK_Other,
        S->isNegatedConsteval());

    Else = getDerived().TransformStmt(S->getElse());
    if (Else.isInvalid())
      return StmtError();
  } else if (S->getElse() && ConstexprConditionValue &&
             *ConstexprConditionValue) {
    // Same thing here as with <then> branch, we are discarding it, we can't
    // replace it with NULL nor NullStmt as we need to keep for source location
    // range, for CoverageMapping
    Else = new (getSema().Context)
        CompoundStmt(S->getElse()->getBeginLoc(), S->getElse()->getEndLoc());
  }

  if (!getDerived().AlwaysRebuild() &&
      Init.get() == S->getInit() &&
      Cond.get() == std::make_pair(S->getConditionVariable(), S->getCond()) &&
      Then.get() == S->getThen() &&
      Else.get() == S->getElse())
    return S;

  return getDerived().RebuildIfStmt(
      S->getIfLoc(), S->getStatementKind(), S->getLParenLoc(), Cond,
      S->getRParenLoc(), Init.get(), Then.get(), S->getElseLoc(), Else.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformSwitchStmt(SwitchStmt *S) {
  // Transform the initialization statement
  StmtResult Init = getDerived().TransformStmt(S->getInit());
  if (Init.isInvalid())
    return StmtError();

  // Transform the condition.
  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getSwitchLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Switch);
  if (Cond.isInvalid())
    return StmtError();

  // Rebuild the switch statement.
  StmtResult Switch =
      getDerived().RebuildSwitchStmtStart(S->getSwitchLoc(), S->getLParenLoc(),
                                          Init.get(), Cond, S->getRParenLoc());
  if (Switch.isInvalid())
    return StmtError();

  // Transform the body of the switch statement.
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  // Complete the switch statement.
  return getDerived().RebuildSwitchStmtBody(S->getSwitchLoc(), Switch.get(),
                                            Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformWhileStmt(WhileStmt *S) {
  // Transform the condition
  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getWhileLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Boolean);
  if (Cond.isInvalid())
    return StmtError();

  // Transform the body
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() &&
      Cond.get() == std::make_pair(S->getConditionVariable(), S->getCond()) &&
      Body.get() == S->getBody())
    return Owned(S);

  return getDerived().RebuildWhileStmt(S->getWhileLoc(), S->getLParenLoc(),
                                       Cond, S->getRParenLoc(), Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformDoStmt(DoStmt *S) {
  // Transform the body
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  // Transform the condition
  ExprResult Cond = getDerived().TransformExpr(S->getCond());
  if (Cond.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() &&
      Cond.get() == S->getCond() &&
      Body.get() == S->getBody())
    return S;

  return getDerived().RebuildDoStmt(S->getDoLoc(), Body.get(), S->getWhileLoc(),
                                    /*FIXME:*/S->getWhileLoc(), Cond.get(),
                                    S->getRParenLoc());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformForStmt(ForStmt *S) {
  if (getSema().getLangOpts().OpenMP)
    getSema().OpenMP().startOpenMPLoop();

  // Transform the initialization statement
  StmtResult Init = getDerived().TransformStmt(S->getInit());
  if (Init.isInvalid())
    return StmtError();

  // In OpenMP loop region loop control variable must be captured and be
  // private. Perform analysis of first part (if any).
  if (getSema().getLangOpts().OpenMP && Init.isUsable())
    getSema().OpenMP().ActOnOpenMPLoopInitialization(S->getForLoc(),
                                                     Init.get());

  // Transform the condition
  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getForLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Boolean);
  if (Cond.isInvalid())
    return StmtError();

  // Transform the increment
  ExprResult Inc = getDerived().TransformExpr(S->getInc());
  if (Inc.isInvalid())
    return StmtError();

  Sema::FullExprArg FullInc(getSema().MakeFullDiscardedValueExpr(Inc.get()));
  if (S->getInc() && !FullInc.get())
    return StmtError();

  // Transform the body
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() &&
      Init.get() == S->getInit() &&
      Cond.get() == std::make_pair(S->getConditionVariable(), S->getCond()) &&
      Inc.get() == S->getInc() &&
      Body.get() == S->getBody())
    return S;

  return getDerived().RebuildForStmt(S->getForLoc(), S->getLParenLoc(),
                                     Init.get(), Cond, FullInc,
                                     S->getRParenLoc(), Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformGotoStmt(GotoStmt *S) {
  Decl *LD = getDerived().TransformDecl(S->getLabel()->getLocation(),
                                        S->getLabel());
  if (!LD)
    return StmtError();

  // Goto statements must always be rebuilt, to resolve the label.
  return getDerived().RebuildGotoStmt(S->getGotoLoc(), S->getLabelLoc(),
                                      cast<LabelDecl>(LD));
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformIndirectGotoStmt(IndirectGotoStmt *S) {
  ExprResult Target = getDerived().TransformExpr(S->getTarget());
  if (Target.isInvalid())
    return StmtError();
  Target = SemaRef.MaybeCreateExprWithCleanups(Target.get());

  if (!getDerived().AlwaysRebuild() &&
      Target.get() == S->getTarget())
    return S;

  return getDerived().RebuildIndirectGotoStmt(S->getGotoLoc(), S->getStarLoc(),
                                              Target.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformContinueStmt(ContinueStmt *S) {
  return S;
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformBreakStmt(BreakStmt *S) {
  return S;
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformReturnStmt(ReturnStmt *S) {
  ExprResult Result = getDerived().TransformInitializer(S->getRetValue(),
                                                        /*NotCopyInit*/false);
  if (Result.isInvalid())
    return StmtError();

  // FIXME: We always rebuild the return statement because there is no way
  // to tell whether the return type of the function has changed.
  return getDerived().RebuildReturnStmt(S->getReturnLoc(), Result.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformDeclStmt(DeclStmt *S) {
  bool DeclChanged = false;
  SmallVector<Decl *, 4> Decls;
  for (auto *D : S->decls()) {
    Decl *Transformed = getDerived().TransformDefinition(D->getLocation(), D);
    if (!Transformed)
      return StmtError();

    if (Transformed != D)
      DeclChanged = true;

    Decls.push_back(Transformed);
  }

  if (!getDerived().AlwaysRebuild() && !DeclChanged)
    return S;

  return getDerived().RebuildDeclStmt(Decls, S->getBeginLoc(), S->getEndLoc());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformGCCAsmStmt(GCCAsmStmt *S) {

  SmallVector<Expr*, 8> Constraints;
  SmallVector<Expr*, 8> Exprs;
  SmallVector<IdentifierInfo *, 4> Names;

  ExprResult AsmString;
  SmallVector<Expr*, 8> Clobbers;

  bool ExprsChanged = false;

  // Go through the outputs.
  for (unsigned I = 0, E = S->getNumOutputs(); I != E; ++I) {
    Names.push_back(S->getOutputIdentifier(I));

    // No need to transform the constraint literal.
    Constraints.push_back(S->getOutputConstraintLiteral(I));

    // Transform the output expr.
    Expr *OutputExpr = S->getOutputExpr(I);
    ExprResult Result = getDerived().TransformExpr(OutputExpr);
    if (Result.isInvalid())
      return StmtError();

    ExprsChanged |= Result.get() != OutputExpr;

    Exprs.push_back(Result.get());
  }

  // Go through the inputs.
  for (unsigned I = 0, E = S->getNumInputs(); I != E; ++I) {
    Names.push_back(S->getInputIdentifier(I));

    // No need to transform the constraint literal.
    Constraints.push_back(S->getInputConstraintLiteral(I));

    // Transform the input expr.
    Expr *InputExpr = S->getInputExpr(I);
    ExprResult Result = getDerived().TransformExpr(InputExpr);
    if (Result.isInvalid())
      return StmtError();

    ExprsChanged |= Result.get() != InputExpr;

    Exprs.push_back(Result.get());
  }

  // Go through the Labels.
  for (unsigned I = 0, E = S->getNumLabels(); I != E; ++I) {
    Names.push_back(S->getLabelIdentifier(I));

    ExprResult Result = getDerived().TransformExpr(S->getLabelExpr(I));
    if (Result.isInvalid())
      return StmtError();
    ExprsChanged |= Result.get() != S->getLabelExpr(I);
    Exprs.push_back(Result.get());
  }
  if (!getDerived().AlwaysRebuild() && !ExprsChanged)
    return S;

  // Go through the clobbers.
  for (unsigned I = 0, E = S->getNumClobbers(); I != E; ++I)
    Clobbers.push_back(S->getClobberStringLiteral(I));

  // No need to transform the asm string literal.
  AsmString = S->getAsmString();
  return getDerived().RebuildGCCAsmStmt(S->getAsmLoc(), S->isSimple(),
                                        S->isVolatile(), S->getNumOutputs(),
                                        S->getNumInputs(), Names.data(),
                                        Constraints, Exprs, AsmString.get(),
                                        Clobbers, S->getNumLabels(),
                                        S->getRParenLoc());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformMSAsmStmt(MSAsmStmt *S) {
  ArrayRef<Token> AsmToks = llvm::ArrayRef(S->getAsmToks(), S->getNumAsmToks());

  bool HadError = false, HadChange = false;

  ArrayRef<Expr*> SrcExprs = S->getAllExprs();
  SmallVector<Expr*, 8> TransformedExprs;
  TransformedExprs.reserve(SrcExprs.size());
  for (unsigned i = 0, e = SrcExprs.size(); i != e; ++i) {
    ExprResult Result = getDerived().TransformExpr(SrcExprs[i]);
    if (!Result.isUsable()) {
      HadError = true;
    } else {
      HadChange |= (Result.get() != SrcExprs[i]);
      TransformedExprs.push_back(Result.get());
    }
  }

  if (HadError) return StmtError();
  if (!HadChange && !getDerived().AlwaysRebuild())
    return Owned(S);

  return getDerived().RebuildMSAsmStmt(S->getAsmLoc(), S->getLBraceLoc(),
                                       AsmToks, S->getAsmString(),
                                       S->getNumOutputs(), S->getNumInputs(),
                                       S->getAllConstraints(), S->getClobbers(),
                                       TransformedExprs, S->getEndLoc());
}

// C++ Coroutines
template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCoroutineBodyStmt(CoroutineBodyStmt *S) {
  auto *ScopeInfo = SemaRef.getCurFunction();
  auto *FD = cast<FunctionDecl>(SemaRef.CurContext);
  assert(FD && ScopeInfo && !ScopeInfo->CoroutinePromise &&
         ScopeInfo->NeedsCoroutineSuspends &&
         ScopeInfo->CoroutineSuspends.first == nullptr &&
         ScopeInfo->CoroutineSuspends.second == nullptr &&
         "expected clean scope info");

  // Set that we have (possibly-invalid) suspend points before we do anything
  // that may fail.
  ScopeInfo->setNeedsCoroutineSuspends(false);

  // We re-build the coroutine promise object (and the coroutine parameters its
  // type and constructor depend on) based on the types used in our current
  // function. We must do so, and set it on the current FunctionScopeInfo,
  // before attempting to transform the other parts of the coroutine body
  // statement, such as the implicit suspend statements (because those
  // statements reference the FunctionScopeInfo::CoroutinePromise).
  if (!SemaRef.buildCoroutineParameterMoves(FD->getLocation()))
    return StmtError();
  auto *Promise = SemaRef.buildCoroutinePromise(FD->getLocation());
  if (!Promise)
    return StmtError();
  getDerived().transformedLocalDecl(S->getPromiseDecl(), {Promise});
  ScopeInfo->CoroutinePromise = Promise;

  // Transform the implicit coroutine statements constructed using dependent
  // types during the previous parse: initial and final suspensions, the return
  // object, and others. We also transform the coroutine function's body.
  StmtResult InitSuspend = getDerived().TransformStmt(S->getInitSuspendStmt());
  if (InitSuspend.isInvalid())
    return StmtError();
  StmtResult FinalSuspend =
      getDerived().TransformStmt(S->getFinalSuspendStmt());
  if (FinalSuspend.isInvalid() ||
      !SemaRef.checkFinalSuspendNoThrow(FinalSuspend.get()))
    return StmtError();
  ScopeInfo->setCoroutineSuspends(InitSuspend.get(), FinalSuspend.get());
  assert(isa<Expr>(InitSuspend.get()) && isa<Expr>(FinalSuspend.get()));

  StmtResult BodyRes = getDerived().TransformStmt(S->getBody());
  if (BodyRes.isInvalid())
    return StmtError();

  CoroutineStmtBuilder Builder(SemaRef, *FD, *ScopeInfo, BodyRes.get());
  if (Builder.isInvalid())
    return StmtError();

  Expr *ReturnObject = S->getReturnValueInit();
  assert(ReturnObject && "the return object is expected to be valid");
  ExprResult Res = getDerived().TransformInitializer(ReturnObject,
                                                     /*NoCopyInit*/ false);
  if (Res.isInvalid())
    return StmtError();
  Builder.ReturnValue = Res.get();

  // If during the previous parse the coroutine still had a dependent promise
  // statement, we may need to build some implicit coroutine statements
  // (such as exception and fallthrough handlers) for the first time.
  if (S->hasDependentPromiseType()) {
    // We can only build these statements, however, if the current promise type
    // is not dependent.
    if (!Promise->getType()->isDependentType()) {
      assert(!S->getFallthroughHandler() && !S->getExceptionHandler() &&
             !S->getReturnStmtOnAllocFailure() && !S->getDeallocate() &&
             "these nodes should not have been built yet");
      if (!Builder.buildDependentStatements())
        return StmtError();
    }
  } else {
    if (auto *OnFallthrough = S->getFallthroughHandler()) {
      StmtResult Res = getDerived().TransformStmt(OnFallthrough);
      if (Res.isInvalid())
        return StmtError();
      Builder.OnFallthrough = Res.get();
    }

    if (auto *OnException = S->getExceptionHandler()) {
      StmtResult Res = getDerived().TransformStmt(OnException);
      if (Res.isInvalid())
        return StmtError();
      Builder.OnException = Res.get();
    }

    if (auto *OnAllocFailure = S->getReturnStmtOnAllocFailure()) {
      StmtResult Res = getDerived().TransformStmt(OnAllocFailure);
      if (Res.isInvalid())
        return StmtError();
      Builder.ReturnStmtOnAllocFailure = Res.get();
    }

    // Transform any additional statements we may have already built
    assert(S->getAllocate() && S->getDeallocate() &&
           "allocation and deallocation calls must already be built");
    ExprResult AllocRes = getDerived().TransformExpr(S->getAllocate());
    if (AllocRes.isInvalid())
      return StmtError();
    Builder.Allocate = AllocRes.get();

    ExprResult DeallocRes = getDerived().TransformExpr(S->getDeallocate());
    if (DeallocRes.isInvalid())
      return StmtError();
    Builder.Deallocate = DeallocRes.get();

    if (auto *ResultDecl = S->getResultDecl()) {
      StmtResult Res = getDerived().TransformStmt(ResultDecl);
      if (Res.isInvalid())
        return StmtError();
      Builder.ResultDecl = Res.get();
    }

    if (auto *ReturnStmt = S->getReturnStmt()) {
      StmtResult Res = getDerived().TransformStmt(ReturnStmt);
      if (Res.isInvalid())
        return StmtError();
      Builder.ReturnStmt = Res.get();
    }
  }

  return getDerived().RebuildCoroutineBodyStmt(Builder);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCoreturnStmt(CoreturnStmt *S) {
  ExprResult Result = getDerived().TransformInitializer(S->getOperand(),
                                                        /*NotCopyInit*/false);
  if (Result.isInvalid())
    return StmtError();

  // Always rebuild; we don't know if this needs to be injected into a new
  // context or if the promise type has changed.
  return getDerived().RebuildCoreturnStmt(S->getKeywordLoc(), Result.get(),
                                          S->isImplicit());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformCoawaitExpr(CoawaitExpr *E) {
  ExprResult Operand = getDerived().TransformInitializer(E->getOperand(),
                                                         /*NotCopyInit*/ false);
  if (Operand.isInvalid())
    return ExprError();

  // Rebuild the common-expr from the operand rather than transforming it
  // separately.

  // FIXME: getCurScope() should not be used during template instantiation.
  // We should pick up the set of unqualified lookup results for operator
  // co_await during the initial parse.
  ExprResult Lookup = getSema().BuildOperatorCoawaitLookupExpr(
      getSema().getCurScope(), E->getKeywordLoc());

  // Always rebuild; we don't know if this needs to be injected into a new
  // context or if the promise type has changed.
  return getDerived().RebuildCoawaitExpr(
      E->getKeywordLoc(), Operand.get(),
      cast<UnresolvedLookupExpr>(Lookup.get()), E->isImplicit());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformDependentCoawaitExpr(DependentCoawaitExpr *E) {
  ExprResult OperandResult = getDerived().TransformInitializer(E->getOperand(),
                                                        /*NotCopyInit*/ false);
  if (OperandResult.isInvalid())
    return ExprError();

  ExprResult LookupResult = getDerived().TransformUnresolvedLookupExpr(
          E->getOperatorCoawaitLookup());

  if (LookupResult.isInvalid())
    return ExprError();

  // Always rebuild; we don't know if this needs to be injected into a new
  // context or if the promise type has changed.
  return getDerived().RebuildDependentCoawaitExpr(
      E->getKeywordLoc(), OperandResult.get(),
      cast<UnresolvedLookupExpr>(LookupResult.get()));
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCoyieldExpr(CoyieldExpr *E) {
  ExprResult Result = getDerived().TransformInitializer(E->getOperand(),
                                                        /*NotCopyInit*/false);
  if (Result.isInvalid())
    return ExprError();

  // Always rebuild; we don't know if this needs to be injected into a new
  // context or if the promise type has changed.
  return getDerived().RebuildCoyieldExpr(E->getKeywordLoc(), Result.get());
}

// Objective-C Statements.

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCAtTryStmt(ObjCAtTryStmt *S) {
  // Transform the body of the @try.
  StmtResult TryBody = getDerived().TransformStmt(S->getTryBody());
  if (TryBody.isInvalid())
    return StmtError();

  // Transform the @catch statements (if present).
  bool AnyCatchChanged = false;
  SmallVector<Stmt*, 8> CatchStmts;
  for (unsigned I = 0, N = S->getNumCatchStmts(); I != N; ++I) {
    StmtResult Catch = getDerived().TransformStmt(S->getCatchStmt(I));
    if (Catch.isInvalid())
      return StmtError();
    if (Catch.get() != S->getCatchStmt(I))
      AnyCatchChanged = true;
    CatchStmts.push_back(Catch.get());
  }

  // Transform the @finally statement (if present).
  StmtResult Finally;
  if (S->getFinallyStmt()) {
    Finally = getDerived().TransformStmt(S->getFinallyStmt());
    if (Finally.isInvalid())
      return StmtError();
  }

  // If nothing changed, just retain this statement.
  if (!getDerived().AlwaysRebuild() &&
      TryBody.get() == S->getTryBody() &&
      !AnyCatchChanged &&
      Finally.get() == S->getFinallyStmt())
    return S;

  // Build a new statement.
  return getDerived().RebuildObjCAtTryStmt(S->getAtTryLoc(), TryBody.get(),
                                           CatchStmts, Finally.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCAtCatchStmt(ObjCAtCatchStmt *S) {
  // Transform the @catch parameter, if there is one.
  VarDecl *Var = nullptr;
  if (VarDecl *FromVar = S->getCatchParamDecl()) {
    TypeSourceInfo *TSInfo = nullptr;
    if (FromVar->getTypeSourceInfo()) {
      TSInfo = getDerived().TransformType(FromVar->getTypeSourceInfo());
      if (!TSInfo)
        return StmtError();
    }

    QualType T;
    if (TSInfo)
      T = TSInfo->getType();
    else {
      T = getDerived().TransformType(FromVar->getType());
      if (T.isNull())
        return StmtError();
    }

    Var = getDerived().RebuildObjCExceptionDecl(FromVar, TSInfo, T);
    if (!Var)
      return StmtError();
  }

  StmtResult Body = getDerived().TransformStmt(S->getCatchBody());
  if (Body.isInvalid())
    return StmtError();

  return getDerived().RebuildObjCAtCatchStmt(S->getAtCatchLoc(),
                                             S->getRParenLoc(),
                                             Var, Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
  // Transform the body.
  StmtResult Body = getDerived().TransformStmt(S->getFinallyBody());
  if (Body.isInvalid())
    return StmtError();

  // If nothing changed, just retain this statement.
  if (!getDerived().AlwaysRebuild() &&
      Body.get() == S->getFinallyBody())
    return S;

  // Build a new statement.
  return getDerived().RebuildObjCAtFinallyStmt(S->getAtFinallyLoc(),
                                               Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  ExprResult Operand;
  if (S->getThrowExpr()) {
    Operand = getDerived().TransformExpr(S->getThrowExpr());
    if (Operand.isInvalid())
      return StmtError();
  }

  if (!getDerived().AlwaysRebuild() &&
      Operand.get() == S->getThrowExpr())
    return S;

  return getDerived().RebuildObjCAtThrowStmt(S->getThrowLoc(), Operand.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCAtSynchronizedStmt(
                                                  ObjCAtSynchronizedStmt *S) {
  // Transform the object we are locking.
  ExprResult Object = getDerived().TransformExpr(S->getSynchExpr());
  if (Object.isInvalid())
    return StmtError();
  Object =
    getDerived().RebuildObjCAtSynchronizedOperand(S->getAtSynchronizedLoc(),
                                                  Object.get());
  if (Object.isInvalid())
    return StmtError();

  // Transform the body.
  StmtResult Body = getDerived().TransformStmt(S->getSynchBody());
  if (Body.isInvalid())
    return StmtError();

  // If nothing change, just retain the current statement.
  if (!getDerived().AlwaysRebuild() &&
      Object.get() == S->getSynchExpr() &&
      Body.get() == S->getSynchBody())
    return S;

  // Build a new statement.
  return getDerived().RebuildObjCAtSynchronizedStmt(S->getAtSynchronizedLoc(),
                                                    Object.get(), Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCAutoreleasePoolStmt(
                                              ObjCAutoreleasePoolStmt *S) {
  // Transform the body.
  StmtResult Body = getDerived().TransformStmt(S->getSubStmt());
  if (Body.isInvalid())
    return StmtError();

  // If nothing changed, just retain this statement.
  if (!getDerived().AlwaysRebuild() &&
      Body.get() == S->getSubStmt())
    return S;

  // Build a new statement.
  return getDerived().RebuildObjCAutoreleasePoolStmt(
                        S->getAtLoc(), Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformObjCForCollectionStmt(
                                                  ObjCForCollectionStmt *S) {
  // Transform the element statement.
  StmtResult Element =
      getDerived().TransformStmt(S->getElement(), SDK_NotDiscarded);
  if (Element.isInvalid())
    return StmtError();

  // Transform the collection expression.
  ExprResult Collection = getDerived().TransformExpr(S->getCollection());
  if (Collection.isInvalid())
    return StmtError();

  // Transform the body.
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  // If nothing changed, just retain this statement.
  if (!getDerived().AlwaysRebuild() &&
      Element.get() == S->getElement() &&
      Collection.get() == S->getCollection() &&
      Body.get() == S->getBody())
    return S;

  // Build a new statement.
  return getDerived().RebuildObjCForCollectionStmt(S->getForLoc(),
                                                   Element.get(),
                                                   Collection.get(),
                                                   S->getRParenLoc(),
                                                   Body.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformCXXCatchStmt(CXXCatchStmt *S) {
  // Transform the exception declaration, if any.
  VarDecl *Var = nullptr;
  if (VarDecl *ExceptionDecl = S->getExceptionDecl()) {
    TypeSourceInfo *T =
        getDerived().TransformType(ExceptionDecl->getTypeSourceInfo());
    if (!T)
      return StmtError();

    Var = getDerived().RebuildExceptionDecl(
        ExceptionDecl, T, ExceptionDecl->getInnerLocStart(),
        ExceptionDecl->getLocation(), ExceptionDecl->getIdentifier());
    if (!Var || Var->isInvalidDecl())
      return StmtError();
  }

  // Transform the actual exception handler.
  StmtResult Handler = getDerived().TransformStmt(S->getHandlerBlock());
  if (Handler.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() && !Var &&
      Handler.get() == S->getHandlerBlock())
    return S;

  return getDerived().RebuildCXXCatchStmt(S->getCatchLoc(), Var, Handler.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformCXXTryStmt(CXXTryStmt *S) {
  // Transform the try block itself.
  StmtResult TryBlock = getDerived().TransformCompoundStmt(S->getTryBlock());
  if (TryBlock.isInvalid())
    return StmtError();

  // Transform the handlers.
  bool HandlerChanged = false;
  SmallVector<Stmt *, 8> Handlers;
  for (unsigned I = 0, N = S->getNumHandlers(); I != N; ++I) {
    StmtResult Handler = getDerived().TransformCXXCatchStmt(S->getHandler(I));
    if (Handler.isInvalid())
      return StmtError();

    HandlerChanged = HandlerChanged || Handler.get() != S->getHandler(I);
    Handlers.push_back(Handler.getAs<Stmt>());
  }

  if (!getDerived().AlwaysRebuild() && TryBlock.get() == S->getTryBlock() &&
      !HandlerChanged)
    return S;

  return getDerived().RebuildCXXTryStmt(S->getTryLoc(), TryBlock.get(),
                                        Handlers);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCXXForRangeStmt(CXXForRangeStmt *S) {
  EnterExpressionEvaluationContext ForRangeInitContext(
      getSema(), Sema::ExpressionEvaluationContext::PotentiallyEvaluated,
      /*LambdaContextDecl=*/nullptr,
      Sema::ExpressionEvaluationContextRecord::EK_Other,
      getSema().getLangOpts().CPlusPlus23);

  // P2718R0 - Lifetime extension in range-based for loops.
  if (getSema().getLangOpts().CPlusPlus23) {
    auto &LastRecord = getSema().ExprEvalContexts.back();
    LastRecord.InLifetimeExtendingContext = true;
  }
  StmtResult Init =
      S->getInit() ? getDerived().TransformStmt(S->getInit()) : StmtResult();
  if (Init.isInvalid())
    return StmtError();

  StmtResult Range = getDerived().TransformStmt(S->getRangeStmt());
  if (Range.isInvalid())
    return StmtError();

  // Before c++23, ForRangeLifetimeExtendTemps should be empty.
  assert(getSema().getLangOpts().CPlusPlus23 ||
         getSema().ExprEvalContexts.back().ForRangeLifetimeExtendTemps.empty());
  auto ForRangeLifetimeExtendTemps =
      getSema().ExprEvalContexts.back().ForRangeLifetimeExtendTemps;

  StmtResult Begin = getDerived().TransformStmt(S->getBeginStmt());
  if (Begin.isInvalid())
    return StmtError();
  StmtResult End = getDerived().TransformStmt(S->getEndStmt());
  if (End.isInvalid())
    return StmtError();

  ExprResult Cond = getDerived().TransformExpr(S->getCond());
  if (Cond.isInvalid())
    return StmtError();
  if (Cond.get())
    Cond = SemaRef.CheckBooleanCondition(S->getColonLoc(), Cond.get());
  if (Cond.isInvalid())
    return StmtError();
  if (Cond.get())
    Cond = SemaRef.MaybeCreateExprWithCleanups(Cond.get());

  ExprResult Inc = getDerived().TransformExpr(S->getInc());
  if (Inc.isInvalid())
    return StmtError();
  if (Inc.get())
    Inc = SemaRef.MaybeCreateExprWithCleanups(Inc.get());

  StmtResult LoopVar = getDerived().TransformStmt(S->getLoopVarStmt());
  if (LoopVar.isInvalid())
    return StmtError();

  StmtResult NewStmt = S;
  if (getDerived().AlwaysRebuild() ||
      Init.get() != S->getInit() ||
      Range.get() != S->getRangeStmt() ||
      Begin.get() != S->getBeginStmt() ||
      End.get() != S->getEndStmt() ||
      Cond.get() != S->getCond() ||
      Inc.get() != S->getInc() ||
      LoopVar.get() != S->getLoopVarStmt()) {
    NewStmt = getDerived().RebuildCXXForRangeStmt(
        S->getForLoc(), S->getCoawaitLoc(), Init.get(), S->getColonLoc(),
        Range.get(), Begin.get(), End.get(), Cond.get(), Inc.get(),
        LoopVar.get(), S->getRParenLoc(), ForRangeLifetimeExtendTemps);
    if (NewStmt.isInvalid() && LoopVar.get() != S->getLoopVarStmt()) {
      // Might not have attached any initializer to the loop variable.
      getSema().ActOnInitializerError(
          cast<DeclStmt>(LoopVar.get())->getSingleDecl());
      return StmtError();
    }
  }

  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  // Body has changed but we didn't rebuild the for-range statement. Rebuild
  // it now so we have a new statement to attach the body to.
  if (Body.get() != S->getBody() && NewStmt.get() == S) {
    NewStmt = getDerived().RebuildCXXForRangeStmt(
        S->getForLoc(), S->getCoawaitLoc(), Init.get(), S->getColonLoc(),
        Range.get(), Begin.get(), End.get(), Cond.get(), Inc.get(),
        LoopVar.get(), S->getRParenLoc(), ForRangeLifetimeExtendTemps);
    if (NewStmt.isInvalid())
      return StmtError();
  }

  if (NewStmt.get() == S)
    return S;

  return FinishCXXForRangeStmt(NewStmt.get(), Body.get());
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformMSDependentExistsStmt(
                                                    MSDependentExistsStmt *S) {
  // Transform the nested-name-specifier, if any.
  NestedNameSpecifierLoc QualifierLoc;
  if (S->getQualifierLoc()) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(S->getQualifierLoc());
    if (!QualifierLoc)
      return StmtError();
  }

  // Transform the declaration name.
  DeclarationNameInfo NameInfo = S->getNameInfo();
  if (NameInfo.getName()) {
    NameInfo = getDerived().TransformDeclarationNameInfo(NameInfo);
    if (!NameInfo.getName())
      return StmtError();
  }

  // Check whether anything changed.
  if (!getDerived().AlwaysRebuild() &&
      QualifierLoc == S->getQualifierLoc() &&
      NameInfo.getName() == S->getNameInfo().getName())
    return S;

  // Determine whether this name exists, if we can.
  CXXScopeSpec SS;
  SS.Adopt(QualifierLoc);
  bool Dependent = false;
  switch (getSema().CheckMicrosoftIfExistsSymbol(/*S=*/nullptr, SS, NameInfo)) {
  case Sema::IER_Exists:
    if (S->isIfExists())
      break;

    return new (getSema().Context) NullStmt(S->getKeywordLoc());

  case Sema::IER_DoesNotExist:
    if (S->isIfNotExists())
      break;

    return new (getSema().Context) NullStmt(S->getKeywordLoc());

  case Sema::IER_Dependent:
    Dependent = true;
    break;

  case Sema::IER_Error:
    return StmtError();
  }

  // We need to continue with the instantiation, so do so now.
  StmtResult SubStmt = getDerived().TransformCompoundStmt(S->getSubStmt());
  if (SubStmt.isInvalid())
    return StmtError();

  // If we have resolved the name, just transform to the substatement.
  if (!Dependent)
    return SubStmt;

  // The name is still dependent, so build a dependent expression again.
  return getDerived().RebuildMSDependentExistsStmt(S->getKeywordLoc(),
                                                   S->isIfExists(),
                                                   QualifierLoc,
                                                   NameInfo,
                                                   SubStmt.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformMSPropertyRefExpr(MSPropertyRefExpr *E) {
  NestedNameSpecifierLoc QualifierLoc;
  if (E->getQualifierLoc()) {
    QualifierLoc
    = getDerived().TransformNestedNameSpecifierLoc(E->getQualifierLoc());
    if (!QualifierLoc)
      return ExprError();
  }

  MSPropertyDecl *PD = cast_or_null<MSPropertyDecl>(
    getDerived().TransformDecl(E->getMemberLoc(), E->getPropertyDecl()));
  if (!PD)
    return ExprError();

  ExprResult Base = getDerived().TransformExpr(E->getBaseExpr());
  if (Base.isInvalid())
    return ExprError();

  return new (SemaRef.getASTContext())
      MSPropertyRefExpr(Base.get(), PD, E->isArrow(),
                        SemaRef.getASTContext().PseudoObjectTy, VK_LValue,
                        QualifierLoc, E->getMemberLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformMSPropertySubscriptExpr(
    MSPropertySubscriptExpr *E) {
  auto BaseRes = getDerived().TransformExpr(E->getBase());
  if (BaseRes.isInvalid())
    return ExprError();
  auto IdxRes = getDerived().TransformExpr(E->getIdx());
  if (IdxRes.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      BaseRes.get() == E->getBase() &&
      IdxRes.get() == E->getIdx())
    return E;

  return getDerived().RebuildArraySubscriptExpr(
      BaseRes.get(), SourceLocation(), IdxRes.get(), E->getRBracketLoc());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHTryStmt(SEHTryStmt *S) {
  StmtResult TryBlock = getDerived().TransformCompoundStmt(S->getTryBlock());
  if (TryBlock.isInvalid())
    return StmtError();

  StmtResult Handler = getDerived().TransformSEHHandler(S->getHandler());
  if (Handler.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() && TryBlock.get() == S->getTryBlock() &&
      Handler.get() == S->getHandler())
    return S;

  return getDerived().RebuildSEHTryStmt(S->getIsCXXTry(), S->getTryLoc(),
                                        TryBlock.get(), Handler.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHFinallyStmt(SEHFinallyStmt *S) {
  StmtResult Block = getDerived().TransformCompoundStmt(S->getBlock());
  if (Block.isInvalid())
    return StmtError();

  return getDerived().RebuildSEHFinallyStmt(S->getFinallyLoc(), Block.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHExceptStmt(SEHExceptStmt *S) {
  ExprResult FilterExpr = getDerived().TransformExpr(S->getFilterExpr());
  if (FilterExpr.isInvalid())
    return StmtError();

  StmtResult Block = getDerived().TransformCompoundStmt(S->getBlock());
  if (Block.isInvalid())
    return StmtError();

  return getDerived().RebuildSEHExceptStmt(S->getExceptLoc(), FilterExpr.get(),
                                           Block.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHHandler(Stmt *Handler) {
  if (isa<SEHFinallyStmt>(Handler))
    return getDerived().TransformSEHFinallyStmt(cast<SEHFinallyStmt>(Handler));
  else
    return getDerived().TransformSEHExceptStmt(cast<SEHExceptStmt>(Handler));
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformSEHLeaveStmt(SEHLeaveStmt *S) {
  return S;
}

//===----------------------------------------------------------------------===//
// OpenMP directive transformation
//===----------------------------------------------------------------------===//

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPCanonicalLoop(OMPCanonicalLoop *L) {
  // OMPCanonicalLoops are eliminated during transformation, since they will be
  // recomputed by semantic analysis of the associated OMPLoopBasedDirective
  // after transformation.
  return getDerived().TransformStmt(L->getLoopStmt());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPExecutableDirective(
    OMPExecutableDirective *D) {

  // Transform the clauses
  llvm::SmallVector<OMPClause *, 16> TClauses;
  ArrayRef<OMPClause *> Clauses = D->clauses();
  TClauses.reserve(Clauses.size());
  for (ArrayRef<OMPClause *>::iterator I = Clauses.begin(), E = Clauses.end();
       I != E; ++I) {
    if (*I) {
      getDerived().getSema().OpenMP().StartOpenMPClause((*I)->getClauseKind());
      OMPClause *Clause = getDerived().TransformOMPClause(*I);
      getDerived().getSema().OpenMP().EndOpenMPClause();
      if (Clause)
        TClauses.push_back(Clause);
    } else {
      TClauses.push_back(nullptr);
    }
  }
  StmtResult AssociatedStmt;
  if (D->hasAssociatedStmt() && D->getAssociatedStmt()) {
    getDerived().getSema().OpenMP().ActOnOpenMPRegionStart(
        D->getDirectiveKind(),
        /*CurScope=*/nullptr);
    StmtResult Body;
    {
      Sema::CompoundScopeRAII CompoundScope(getSema());
      Stmt *CS;
      if (D->getDirectiveKind() == OMPD_atomic ||
          D->getDirectiveKind() == OMPD_critical ||
          D->getDirectiveKind() == OMPD_section ||
          D->getDirectiveKind() == OMPD_master)
        CS = D->getAssociatedStmt();
      else
        CS = D->getRawStmt();
      Body = getDerived().TransformStmt(CS);
      if (Body.isUsable() && isOpenMPLoopDirective(D->getDirectiveKind()) &&
          getSema().getLangOpts().OpenMPIRBuilder)
        Body = getDerived().RebuildOMPCanonicalLoop(Body.get());
    }
    AssociatedStmt =
        getDerived().getSema().OpenMP().ActOnOpenMPRegionEnd(Body, TClauses);
    if (AssociatedStmt.isInvalid()) {
      return StmtError();
    }
  }
  if (TClauses.size() != Clauses.size()) {
    return StmtError();
  }

  // Transform directive name for 'omp critical' directive.
  DeclarationNameInfo DirName;
  if (D->getDirectiveKind() == OMPD_critical) {
    DirName = cast<OMPCriticalDirective>(D)->getDirectiveName();
    DirName = getDerived().TransformDeclarationNameInfo(DirName);
  }
  OpenMPDirectiveKind CancelRegion = OMPD_unknown;
  if (D->getDirectiveKind() == OMPD_cancellation_point) {
    CancelRegion = cast<OMPCancellationPointDirective>(D)->getCancelRegion();
  } else if (D->getDirectiveKind() == OMPD_cancel) {
    CancelRegion = cast<OMPCancelDirective>(D)->getCancelRegion();
  }

  return getDerived().RebuildOMPExecutableDirective(
      D->getDirectiveKind(), DirName, CancelRegion, TClauses,
      AssociatedStmt.get(), D->getBeginLoc(), D->getEndLoc(),
      D->getMappedDirective());
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPMetaDirective(OMPMetaDirective *D) {
  // TODO: Fix This
  SemaRef.Diag(D->getBeginLoc(), diag::err_omp_instantiation_not_supported)
      << getOpenMPDirectiveName(D->getDirectiveKind());
  return StmtError();
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPParallelDirective(OMPParallelDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPSimdDirective(OMPSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTileDirective(OMPTileDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      D->getDirectiveKind(), DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPUnrollDirective(OMPUnrollDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      D->getDirectiveKind(), DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPReverseDirective(OMPReverseDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      D->getDirectiveKind(), DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPInterchangeDirective(
    OMPInterchangeDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      D->getDirectiveKind(), DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPForDirective(OMPForDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_for, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPForSimdDirective(OMPForSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_for_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPSectionsDirective(OMPSectionsDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_sections, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPSectionDirective(OMPSectionDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_section, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPScopeDirective(OMPScopeDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_scope, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPSingleDirective(OMPSingleDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_single, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPMasterDirective(OMPMasterDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_master, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPCriticalDirective(OMPCriticalDirective *D) {
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_critical, D->getDirectiveName(), nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelForDirective(
    OMPParallelForDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_for, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelForSimdDirective(
    OMPParallelForSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_for_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelMasterDirective(
    OMPParallelMasterDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_master, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelMaskedDirective(
    OMPParallelMaskedDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_masked, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelSectionsDirective(
    OMPParallelSectionsDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_sections, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTaskDirective(OMPTaskDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_task, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTaskyieldDirective(
    OMPTaskyieldDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_taskyield, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPBarrierDirective(OMPBarrierDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_barrier, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTaskwaitDirective(OMPTaskwaitDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_taskwait, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPErrorDirective(OMPErrorDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_error, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTaskgroupDirective(
    OMPTaskgroupDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_taskgroup, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPFlushDirective(OMPFlushDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_flush, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPDepobjDirective(OMPDepobjDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_depobj, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPScanDirective(OMPScanDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_scan, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPOrderedDirective(OMPOrderedDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_ordered, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPAtomicDirective(OMPAtomicDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_atomic, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTargetDirective(OMPTargetDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetDataDirective(
    OMPTargetDataDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_data, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetEnterDataDirective(
    OMPTargetEnterDataDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_enter_data, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetExitDataDirective(
    OMPTargetExitDataDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_exit_data, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetParallelDirective(
    OMPTargetParallelDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_parallel, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetParallelForDirective(
    OMPTargetParallelForDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_parallel_for, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetUpdateDirective(
    OMPTargetUpdateDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_update, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTeamsDirective(OMPTeamsDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_teams, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPCancellationPointDirective(
    OMPCancellationPointDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_cancellation_point, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPCancelDirective(OMPCancelDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_cancel, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTaskLoopDirective(OMPTaskLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_taskloop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTaskLoopSimdDirective(
    OMPTaskLoopSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_taskloop_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPMasterTaskLoopDirective(
    OMPMasterTaskLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_master_taskloop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPMaskedTaskLoopDirective(
    OMPMaskedTaskLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_masked_taskloop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPMasterTaskLoopSimdDirective(
    OMPMasterTaskLoopSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_master_taskloop_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPMaskedTaskLoopSimdDirective(
    OMPMaskedTaskLoopSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_masked_taskloop_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelMasterTaskLoopDirective(
    OMPParallelMasterTaskLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_master_taskloop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelMaskedTaskLoopDirective(
    OMPParallelMaskedTaskLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_masked_taskloop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPParallelMasterTaskLoopSimdDirective(
    OMPParallelMasterTaskLoopSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_master_taskloop_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPParallelMaskedTaskLoopSimdDirective(
    OMPParallelMaskedTaskLoopSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_masked_taskloop_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPDistributeDirective(
    OMPDistributeDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_distribute, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPDistributeParallelForDirective(
    OMPDistributeParallelForDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_distribute_parallel_for, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPDistributeParallelForSimdDirective(
    OMPDistributeParallelForSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_distribute_parallel_for_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPDistributeSimdDirective(
    OMPDistributeSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_distribute_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetParallelForSimdDirective(
    OMPTargetParallelForSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_parallel_for_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetSimdDirective(
    OMPTargetSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTeamsDistributeDirective(
    OMPTeamsDistributeDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_teams_distribute, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTeamsDistributeSimdDirective(
    OMPTeamsDistributeSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_teams_distribute_simd, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTeamsDistributeParallelForSimdDirective(
    OMPTeamsDistributeParallelForSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_teams_distribute_parallel_for_simd, DirName, nullptr,
      D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTeamsDistributeParallelForDirective(
    OMPTeamsDistributeParallelForDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_teams_distribute_parallel_for, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetTeamsDirective(
    OMPTargetTeamsDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_teams, DirName, nullptr, D->getBeginLoc());
  auto Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetTeamsDistributeDirective(
    OMPTargetTeamsDistributeDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_teams_distribute, DirName, nullptr, D->getBeginLoc());
  auto Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTargetTeamsDistributeParallelForDirective(
    OMPTargetTeamsDistributeParallelForDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_teams_distribute_parallel_for, DirName, nullptr,
      D->getBeginLoc());
  auto Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::
    TransformOMPTargetTeamsDistributeParallelForSimdDirective(
        OMPTargetTeamsDistributeParallelForSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_teams_distribute_parallel_for_simd, DirName, nullptr,
      D->getBeginLoc());
  auto Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTargetTeamsDistributeSimdDirective(
    OMPTargetTeamsDistributeSimdDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_teams_distribute_simd, DirName, nullptr, D->getBeginLoc());
  auto Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPInteropDirective(OMPInteropDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_interop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPDispatchDirective(OMPDispatchDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_dispatch, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPMaskedDirective(OMPMaskedDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_masked, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPGenericLoopDirective(
    OMPGenericLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_loop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTeamsGenericLoopDirective(
    OMPTeamsGenericLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_teams_loop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPTargetTeamsGenericLoopDirective(
    OMPTargetTeamsGenericLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_teams_loop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOMPParallelGenericLoopDirective(
    OMPParallelGenericLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_parallel_loop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOMPTargetParallelGenericLoopDirective(
    OMPTargetParallelGenericLoopDirective *D) {
  DeclarationNameInfo DirName;
  getDerived().getSema().OpenMP().StartOpenMPDSABlock(
      OMPD_target_parallel_loop, DirName, nullptr, D->getBeginLoc());
  StmtResult Res = getDerived().TransformOMPExecutableDirective(D);
  getDerived().getSema().OpenMP().EndOpenMPDSABlock(Res.get());
  return Res;
}

//===----------------------------------------------------------------------===//
// OpenMP clause transformation
//===----------------------------------------------------------------------===//
template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPIfClause(OMPIfClause *C) {
  ExprResult Cond = getDerived().TransformExpr(C->getCondition());
  if (Cond.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPIfClause(
      C->getNameModifier(), Cond.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getNameModifierLoc(), C->getColonLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPFinalClause(OMPFinalClause *C) {
  ExprResult Cond = getDerived().TransformExpr(C->getCondition());
  if (Cond.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPFinalClause(Cond.get(), C->getBeginLoc(),
                                            C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNumThreadsClause(OMPNumThreadsClause *C) {
  ExprResult NumThreads = getDerived().TransformExpr(C->getNumThreads());
  if (NumThreads.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPNumThreadsClause(
      NumThreads.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPSafelenClause(OMPSafelenClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getSafelen());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPSafelenClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPAllocatorClause(OMPAllocatorClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getAllocator());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPAllocatorClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPSimdlenClause(OMPSimdlenClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getSimdlen());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPSimdlenClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPSizesClause(OMPSizesClause *C) {
  SmallVector<Expr *, 4> TransformedSizes;
  TransformedSizes.reserve(C->getNumSizes());
  bool Changed = false;
  for (Expr *E : C->getSizesRefs()) {
    if (!E) {
      TransformedSizes.push_back(nullptr);
      continue;
    }

    ExprResult T = getDerived().TransformExpr(E);
    if (T.isInvalid())
      return nullptr;
    if (E != T.get())
      Changed = true;
    TransformedSizes.push_back(T.get());
  }

  if (!Changed && !getDerived().AlwaysRebuild())
    return C;
  return RebuildOMPSizesClause(TransformedSizes, C->getBeginLoc(),
                               C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPFullClause(OMPFullClause *C) {
  if (!getDerived().AlwaysRebuild())
    return C;
  return RebuildOMPFullClause(C->getBeginLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPPartialClause(OMPPartialClause *C) {
  ExprResult T = getDerived().TransformExpr(C->getFactor());
  if (T.isInvalid())
    return nullptr;
  Expr *Factor = T.get();
  bool Changed = Factor != C->getFactor();

  if (!Changed && !getDerived().AlwaysRebuild())
    return C;
  return RebuildOMPPartialClause(Factor, C->getBeginLoc(), C->getLParenLoc(),
                                 C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPCollapseClause(OMPCollapseClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getNumForLoops());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPCollapseClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDefaultClause(OMPDefaultClause *C) {
  return getDerived().RebuildOMPDefaultClause(
      C->getDefaultKind(), C->getDefaultKindKwLoc(), C->getBeginLoc(),
      C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPProcBindClause(OMPProcBindClause *C) {
  return getDerived().RebuildOMPProcBindClause(
      C->getProcBindKind(), C->getProcBindKindKwLoc(), C->getBeginLoc(),
      C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPScheduleClause(OMPScheduleClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getChunkSize());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPScheduleClause(
      C->getFirstScheduleModifier(), C->getSecondScheduleModifier(),
      C->getScheduleKind(), E.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getFirstScheduleModifierLoc(), C->getSecondScheduleModifierLoc(),
      C->getScheduleKindLoc(), C->getCommaLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPOrderedClause(OMPOrderedClause *C) {
  ExprResult E;
  if (auto *Num = C->getNumForLoops()) {
    E = getDerived().TransformExpr(Num);
    if (E.isInvalid())
      return nullptr;
  }
  return getDerived().RebuildOMPOrderedClause(C->getBeginLoc(), C->getEndLoc(),
                                              C->getLParenLoc(), E.get());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDetachClause(OMPDetachClause *C) {
  ExprResult E;
  if (Expr *Evt = C->getEventHandler()) {
    E = getDerived().TransformExpr(Evt);
    if (E.isInvalid())
      return nullptr;
  }
  return getDerived().RebuildOMPDetachClause(E.get(), C->getBeginLoc(),
                                             C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNowaitClause(OMPNowaitClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPUntiedClause(OMPUntiedClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPMergeableClause(OMPMergeableClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPReadClause(OMPReadClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPWriteClause(OMPWriteClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPUpdateClause(OMPUpdateClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPCaptureClause(OMPCaptureClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPCompareClause(OMPCompareClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPFailClause(OMPFailClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPSeqCstClause(OMPSeqCstClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPAcqRelClause(OMPAcqRelClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPAcquireClause(OMPAcquireClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPReleaseClause(OMPReleaseClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPRelaxedClause(OMPRelaxedClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPWeakClause(OMPWeakClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPThreadsClause(OMPThreadsClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPSIMDClause(OMPSIMDClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNogroupClause(OMPNogroupClause *C) {
  // No need to rebuild this clause, no template-dependent parameters.
  return C;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPInitClause(OMPInitClause *C) {
  ExprResult IVR = getDerived().TransformExpr(C->getInteropVar());
  if (IVR.isInvalid())
    return nullptr;

  OMPInteropInfo InteropInfo(C->getIsTarget(), C->getIsTargetSync());
  InteropInfo.PreferTypes.reserve(C->varlist_size() - 1);
  for (Expr *E : llvm::drop_begin(C->varlists())) {
    ExprResult ER = getDerived().TransformExpr(cast<Expr>(E));
    if (ER.isInvalid())
      return nullptr;
    InteropInfo.PreferTypes.push_back(ER.get());
  }
  return getDerived().RebuildOMPInitClause(IVR.get(), InteropInfo,
                                           C->getBeginLoc(), C->getLParenLoc(),
                                           C->getVarLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPUseClause(OMPUseClause *C) {
  ExprResult ER = getDerived().TransformExpr(C->getInteropVar());
  if (ER.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPUseClause(ER.get(), C->getBeginLoc(),
                                          C->getLParenLoc(), C->getVarLoc(),
                                          C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDestroyClause(OMPDestroyClause *C) {
  ExprResult ER;
  if (Expr *IV = C->getInteropVar()) {
    ER = getDerived().TransformExpr(IV);
    if (ER.isInvalid())
      return nullptr;
  }
  return getDerived().RebuildOMPDestroyClause(ER.get(), C->getBeginLoc(),
                                              C->getLParenLoc(), C->getVarLoc(),
                                              C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNovariantsClause(OMPNovariantsClause *C) {
  ExprResult Cond = getDerived().TransformExpr(C->getCondition());
  if (Cond.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPNovariantsClause(
      Cond.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNocontextClause(OMPNocontextClause *C) {
  ExprResult Cond = getDerived().TransformExpr(C->getCondition());
  if (Cond.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPNocontextClause(
      Cond.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPFilterClause(OMPFilterClause *C) {
  ExprResult ThreadID = getDerived().TransformExpr(C->getThreadID());
  if (ThreadID.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPFilterClause(ThreadID.get(), C->getBeginLoc(),
                                             C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPAlignClause(OMPAlignClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getAlignment());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPAlignClause(E.get(), C->getBeginLoc(),
                                            C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPUnifiedAddressClause(
    OMPUnifiedAddressClause *C) {
  llvm_unreachable("unified_address clause cannot appear in dependent context");
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPUnifiedSharedMemoryClause(
    OMPUnifiedSharedMemoryClause *C) {
  llvm_unreachable(
      "unified_shared_memory clause cannot appear in dependent context");
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPReverseOffloadClause(
    OMPReverseOffloadClause *C) {
  llvm_unreachable("reverse_offload clause cannot appear in dependent context");
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPDynamicAllocatorsClause(
    OMPDynamicAllocatorsClause *C) {
  llvm_unreachable(
      "dynamic_allocators clause cannot appear in dependent context");
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPAtomicDefaultMemOrderClause(
    OMPAtomicDefaultMemOrderClause *C) {
  llvm_unreachable(
      "atomic_default_mem_order clause cannot appear in dependent context");
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPAtClause(OMPAtClause *C) {
  return getDerived().RebuildOMPAtClause(C->getAtKind(), C->getAtKindKwLoc(),
                                         C->getBeginLoc(), C->getLParenLoc(),
                                         C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPSeverityClause(OMPSeverityClause *C) {
  return getDerived().RebuildOMPSeverityClause(
      C->getSeverityKind(), C->getSeverityKindKwLoc(), C->getBeginLoc(),
      C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPMessageClause(OMPMessageClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getMessageString());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPMessageClause(
      C->getMessageString(), C->getBeginLoc(), C->getLParenLoc(),
      C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPPrivateClause(OMPPrivateClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPPrivateClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPFirstprivateClause(
    OMPFirstprivateClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPFirstprivateClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPLastprivateClause(OMPLastprivateClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPLastprivateClause(
      Vars, C->getKind(), C->getKindLoc(), C->getColonLoc(), C->getBeginLoc(),
      C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPSharedClause(OMPSharedClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPSharedClause(Vars, C->getBeginLoc(),
                                             C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPReductionClause(OMPReductionClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  CXXScopeSpec ReductionIdScopeSpec;
  ReductionIdScopeSpec.Adopt(C->getQualifierLoc());

  DeclarationNameInfo NameInfo = C->getNameInfo();
  if (NameInfo.getName()) {
    NameInfo = getDerived().TransformDeclarationNameInfo(NameInfo);
    if (!NameInfo.getName())
      return nullptr;
  }
  // Build a list of all UDR decls with the same names ranged by the Scopes.
  // The Scope boundary is a duplication of the previous decl.
  llvm::SmallVector<Expr *, 16> UnresolvedReductions;
  for (auto *E : C->reduction_ops()) {
    // Transform all the decls.
    if (E) {
      auto *ULE = cast<UnresolvedLookupExpr>(E);
      UnresolvedSet<8> Decls;
      for (auto *D : ULE->decls()) {
        NamedDecl *InstD =
            cast<NamedDecl>(getDerived().TransformDecl(E->getExprLoc(), D));
        Decls.addDecl(InstD, InstD->getAccess());
      }
      UnresolvedReductions.push_back(UnresolvedLookupExpr::Create(
          SemaRef.Context, /*NamingClass=*/nullptr,
          ReductionIdScopeSpec.getWithLocInContext(SemaRef.Context), NameInfo,
          /*ADL=*/true, Decls.begin(), Decls.end(),
          /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false));
    } else
      UnresolvedReductions.push_back(nullptr);
  }
  return getDerived().RebuildOMPReductionClause(
      Vars, C->getModifier(), C->getBeginLoc(), C->getLParenLoc(),
      C->getModifierLoc(), C->getColonLoc(), C->getEndLoc(),
      ReductionIdScopeSpec, NameInfo, UnresolvedReductions);
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPTaskReductionClause(
    OMPTaskReductionClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  CXXScopeSpec ReductionIdScopeSpec;
  ReductionIdScopeSpec.Adopt(C->getQualifierLoc());

  DeclarationNameInfo NameInfo = C->getNameInfo();
  if (NameInfo.getName()) {
    NameInfo = getDerived().TransformDeclarationNameInfo(NameInfo);
    if (!NameInfo.getName())
      return nullptr;
  }
  // Build a list of all UDR decls with the same names ranged by the Scopes.
  // The Scope boundary is a duplication of the previous decl.
  llvm::SmallVector<Expr *, 16> UnresolvedReductions;
  for (auto *E : C->reduction_ops()) {
    // Transform all the decls.
    if (E) {
      auto *ULE = cast<UnresolvedLookupExpr>(E);
      UnresolvedSet<8> Decls;
      for (auto *D : ULE->decls()) {
        NamedDecl *InstD =
            cast<NamedDecl>(getDerived().TransformDecl(E->getExprLoc(), D));
        Decls.addDecl(InstD, InstD->getAccess());
      }
      UnresolvedReductions.push_back(UnresolvedLookupExpr::Create(
          SemaRef.Context, /*NamingClass=*/nullptr,
          ReductionIdScopeSpec.getWithLocInContext(SemaRef.Context), NameInfo,
          /*ADL=*/true, Decls.begin(), Decls.end(),
          /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false));
    } else
      UnresolvedReductions.push_back(nullptr);
  }
  return getDerived().RebuildOMPTaskReductionClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getColonLoc(),
      C->getEndLoc(), ReductionIdScopeSpec, NameInfo, UnresolvedReductions);
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPInReductionClause(OMPInReductionClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  CXXScopeSpec ReductionIdScopeSpec;
  ReductionIdScopeSpec.Adopt(C->getQualifierLoc());

  DeclarationNameInfo NameInfo = C->getNameInfo();
  if (NameInfo.getName()) {
    NameInfo = getDerived().TransformDeclarationNameInfo(NameInfo);
    if (!NameInfo.getName())
      return nullptr;
  }
  // Build a list of all UDR decls with the same names ranged by the Scopes.
  // The Scope boundary is a duplication of the previous decl.
  llvm::SmallVector<Expr *, 16> UnresolvedReductions;
  for (auto *E : C->reduction_ops()) {
    // Transform all the decls.
    if (E) {
      auto *ULE = cast<UnresolvedLookupExpr>(E);
      UnresolvedSet<8> Decls;
      for (auto *D : ULE->decls()) {
        NamedDecl *InstD =
            cast<NamedDecl>(getDerived().TransformDecl(E->getExprLoc(), D));
        Decls.addDecl(InstD, InstD->getAccess());
      }
      UnresolvedReductions.push_back(UnresolvedLookupExpr::Create(
          SemaRef.Context, /*NamingClass=*/nullptr,
          ReductionIdScopeSpec.getWithLocInContext(SemaRef.Context), NameInfo,
          /*ADL=*/true, Decls.begin(), Decls.end(),
          /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false));
    } else
      UnresolvedReductions.push_back(nullptr);
  }
  return getDerived().RebuildOMPInReductionClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getColonLoc(),
      C->getEndLoc(), ReductionIdScopeSpec, NameInfo, UnresolvedReductions);
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPLinearClause(OMPLinearClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  ExprResult Step = getDerived().TransformExpr(C->getStep());
  if (Step.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPLinearClause(
      Vars, Step.get(), C->getBeginLoc(), C->getLParenLoc(), C->getModifier(),
      C->getModifierLoc(), C->getColonLoc(), C->getStepModifierLoc(),
      C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPAlignedClause(OMPAlignedClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  ExprResult Alignment = getDerived().TransformExpr(C->getAlignment());
  if (Alignment.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPAlignedClause(
      Vars, Alignment.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getColonLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPCopyinClause(OMPCopyinClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPCopyinClause(Vars, C->getBeginLoc(),
                                             C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPCopyprivateClause(OMPCopyprivateClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPCopyprivateClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPFlushClause(OMPFlushClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPFlushClause(Vars, C->getBeginLoc(),
                                            C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDepobjClause(OMPDepobjClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getDepobj());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPDepobjClause(E.get(), C->getBeginLoc(),
                                             C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDependClause(OMPDependClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Expr *DepModifier = C->getModifier();
  if (DepModifier) {
    ExprResult DepModRes = getDerived().TransformExpr(DepModifier);
    if (DepModRes.isInvalid())
      return nullptr;
    DepModifier = DepModRes.get();
  }
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPDependClause(
      {C->getDependencyKind(), C->getDependencyLoc(), C->getColonLoc(),
       C->getOmpAllMemoryLoc()},
      DepModifier, Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDeviceClause(OMPDeviceClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getDevice());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPDeviceClause(
      C->getModifier(), E.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getModifierLoc(), C->getEndLoc());
}

template <typename Derived, class T>
bool transformOMPMappableExprListClause(
    TreeTransform<Derived> &TT, OMPMappableExprListClause<T> *C,
    llvm::SmallVectorImpl<Expr *> &Vars, CXXScopeSpec &MapperIdScopeSpec,
    DeclarationNameInfo &MapperIdInfo,
    llvm::SmallVectorImpl<Expr *> &UnresolvedMappers) {
  // Transform expressions in the list.
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = TT.getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return true;
    Vars.push_back(EVar.get());
  }
  // Transform mapper scope specifier and identifier.
  NestedNameSpecifierLoc QualifierLoc;
  if (C->getMapperQualifierLoc()) {
    QualifierLoc = TT.getDerived().TransformNestedNameSpecifierLoc(
        C->getMapperQualifierLoc());
    if (!QualifierLoc)
      return true;
  }
  MapperIdScopeSpec.Adopt(QualifierLoc);
  MapperIdInfo = C->getMapperIdInfo();
  if (MapperIdInfo.getName()) {
    MapperIdInfo = TT.getDerived().TransformDeclarationNameInfo(MapperIdInfo);
    if (!MapperIdInfo.getName())
      return true;
  }
  // Build a list of all candidate OMPDeclareMapperDecls, which is provided by
  // the previous user-defined mapper lookup in dependent environment.
  for (auto *E : C->mapperlists()) {
    // Transform all the decls.
    if (E) {
      auto *ULE = cast<UnresolvedLookupExpr>(E);
      UnresolvedSet<8> Decls;
      for (auto *D : ULE->decls()) {
        NamedDecl *InstD =
            cast<NamedDecl>(TT.getDerived().TransformDecl(E->getExprLoc(), D));
        Decls.addDecl(InstD, InstD->getAccess());
      }
      UnresolvedMappers.push_back(UnresolvedLookupExpr::Create(
          TT.getSema().Context, /*NamingClass=*/nullptr,
          MapperIdScopeSpec.getWithLocInContext(TT.getSema().Context),
          MapperIdInfo, /*ADL=*/true, Decls.begin(), Decls.end(),
          /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false));
    } else {
      UnresolvedMappers.push_back(nullptr);
    }
  }
  return false;
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPMapClause(OMPMapClause *C) {
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  llvm::SmallVector<Expr *, 16> Vars;
  Expr *IteratorModifier = C->getIteratorModifier();
  if (IteratorModifier) {
    ExprResult MapModRes = getDerived().TransformExpr(IteratorModifier);
    if (MapModRes.isInvalid())
      return nullptr;
    IteratorModifier = MapModRes.get();
  }
  CXXScopeSpec MapperIdScopeSpec;
  DeclarationNameInfo MapperIdInfo;
  llvm::SmallVector<Expr *, 16> UnresolvedMappers;
  if (transformOMPMappableExprListClause<Derived, OMPMapClause>(
          *this, C, Vars, MapperIdScopeSpec, MapperIdInfo, UnresolvedMappers))
    return nullptr;
  return getDerived().RebuildOMPMapClause(
      IteratorModifier, C->getMapTypeModifiers(), C->getMapTypeModifiersLoc(),
      MapperIdScopeSpec, MapperIdInfo, C->getMapType(), C->isImplicitMapType(),
      C->getMapLoc(), C->getColonLoc(), Vars, Locs, UnresolvedMappers);
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPAllocateClause(OMPAllocateClause *C) {
  Expr *Allocator = C->getAllocator();
  if (Allocator) {
    ExprResult AllocatorRes = getDerived().TransformExpr(Allocator);
    if (AllocatorRes.isInvalid())
      return nullptr;
    Allocator = AllocatorRes.get();
  }
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPAllocateClause(
      Allocator, Vars, C->getBeginLoc(), C->getLParenLoc(), C->getColonLoc(),
      C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNumTeamsClause(OMPNumTeamsClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getNumTeams());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPNumTeamsClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPThreadLimitClause(OMPThreadLimitClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getThreadLimit());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPThreadLimitClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPPriorityClause(OMPPriorityClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getPriority());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPPriorityClause(
      E.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPGrainsizeClause(OMPGrainsizeClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getGrainsize());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPGrainsizeClause(
      C->getModifier(), E.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getModifierLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNumTasksClause(OMPNumTasksClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getNumTasks());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPNumTasksClause(
      C->getModifier(), E.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getModifierLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPHintClause(OMPHintClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getHint());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPHintClause(E.get(), C->getBeginLoc(),
                                           C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPDistScheduleClause(
    OMPDistScheduleClause *C) {
  ExprResult E = getDerived().TransformExpr(C->getChunkSize());
  if (E.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPDistScheduleClause(
      C->getDistScheduleKind(), E.get(), C->getBeginLoc(), C->getLParenLoc(),
      C->getDistScheduleKindLoc(), C->getCommaLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDefaultmapClause(OMPDefaultmapClause *C) {
  // Rebuild Defaultmap Clause since we need to invoke the checking of
  // defaultmap(none:variable-category) after template initialization.
  return getDerived().RebuildOMPDefaultmapClause(C->getDefaultmapModifier(),
                                                 C->getDefaultmapKind(),
                                                 C->getBeginLoc(),
                                                 C->getLParenLoc(),
                                                 C->getDefaultmapModifierLoc(),
                                                 C->getDefaultmapKindLoc(),
                                                 C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPToClause(OMPToClause *C) {
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  llvm::SmallVector<Expr *, 16> Vars;
  CXXScopeSpec MapperIdScopeSpec;
  DeclarationNameInfo MapperIdInfo;
  llvm::SmallVector<Expr *, 16> UnresolvedMappers;
  if (transformOMPMappableExprListClause<Derived, OMPToClause>(
          *this, C, Vars, MapperIdScopeSpec, MapperIdInfo, UnresolvedMappers))
    return nullptr;
  return getDerived().RebuildOMPToClause(
      C->getMotionModifiers(), C->getMotionModifiersLoc(), MapperIdScopeSpec,
      MapperIdInfo, C->getColonLoc(), Vars, Locs, UnresolvedMappers);
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPFromClause(OMPFromClause *C) {
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  llvm::SmallVector<Expr *, 16> Vars;
  CXXScopeSpec MapperIdScopeSpec;
  DeclarationNameInfo MapperIdInfo;
  llvm::SmallVector<Expr *, 16> UnresolvedMappers;
  if (transformOMPMappableExprListClause<Derived, OMPFromClause>(
          *this, C, Vars, MapperIdScopeSpec, MapperIdInfo, UnresolvedMappers))
    return nullptr;
  return getDerived().RebuildOMPFromClause(
      C->getMotionModifiers(), C->getMotionModifiersLoc(), MapperIdScopeSpec,
      MapperIdInfo, C->getColonLoc(), Vars, Locs, UnresolvedMappers);
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPUseDevicePtrClause(
    OMPUseDevicePtrClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  return getDerived().RebuildOMPUseDevicePtrClause(Vars, Locs);
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPUseDeviceAddrClause(
    OMPUseDeviceAddrClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  return getDerived().RebuildOMPUseDeviceAddrClause(Vars, Locs);
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPIsDevicePtrClause(OMPIsDevicePtrClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  return getDerived().RebuildOMPIsDevicePtrClause(Vars, Locs);
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPHasDeviceAddrClause(
    OMPHasDeviceAddrClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  OMPVarListLocTy Locs(C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
  return getDerived().RebuildOMPHasDeviceAddrClause(Vars, Locs);
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPNontemporalClause(OMPNontemporalClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPNontemporalClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPInclusiveClause(OMPInclusiveClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPInclusiveClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPExclusiveClause(OMPExclusiveClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPExclusiveClause(
      Vars, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPUsesAllocatorsClause(
    OMPUsesAllocatorsClause *C) {
  SmallVector<SemaOpenMP::UsesAllocatorsData, 16> Data;
  Data.reserve(C->getNumberOfAllocators());
  for (unsigned I = 0, E = C->getNumberOfAllocators(); I < E; ++I) {
    OMPUsesAllocatorsClause::Data D = C->getAllocatorData(I);
    ExprResult Allocator = getDerived().TransformExpr(D.Allocator);
    if (Allocator.isInvalid())
      continue;
    ExprResult AllocatorTraits;
    if (Expr *AT = D.AllocatorTraits) {
      AllocatorTraits = getDerived().TransformExpr(AT);
      if (AllocatorTraits.isInvalid())
        continue;
    }
    SemaOpenMP::UsesAllocatorsData &NewD = Data.emplace_back();
    NewD.Allocator = Allocator.get();
    NewD.AllocatorTraits = AllocatorTraits.get();
    NewD.LParenLoc = D.LParenLoc;
    NewD.RParenLoc = D.RParenLoc;
  }
  return getDerived().RebuildOMPUsesAllocatorsClause(
      Data, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPAffinityClause(OMPAffinityClause *C) {
  SmallVector<Expr *, 4> Locators;
  Locators.reserve(C->varlist_size());
  ExprResult ModifierRes;
  if (Expr *Modifier = C->getModifier()) {
    ModifierRes = getDerived().TransformExpr(Modifier);
    if (ModifierRes.isInvalid())
      return nullptr;
  }
  for (Expr *E : C->varlists()) {
    ExprResult Locator = getDerived().TransformExpr(E);
    if (Locator.isInvalid())
      continue;
    Locators.push_back(Locator.get());
  }
  return getDerived().RebuildOMPAffinityClause(
      C->getBeginLoc(), C->getLParenLoc(), C->getColonLoc(), C->getEndLoc(),
      ModifierRes.get(), Locators);
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPOrderClause(OMPOrderClause *C) {
  return getDerived().RebuildOMPOrderClause(
      C->getKind(), C->getKindKwLoc(), C->getBeginLoc(), C->getLParenLoc(),
      C->getEndLoc(), C->getModifier(), C->getModifierKwLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPBindClause(OMPBindClause *C) {
  return getDerived().RebuildOMPBindClause(
      C->getBindKind(), C->getBindKindLoc(), C->getBeginLoc(),
      C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPXDynCGroupMemClause(
    OMPXDynCGroupMemClause *C) {
  ExprResult Size = getDerived().TransformExpr(C->getSize());
  if (Size.isInvalid())
    return nullptr;
  return getDerived().RebuildOMPXDynCGroupMemClause(
      Size.get(), C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPDoacrossClause(OMPDoacrossClause *C) {
  llvm::SmallVector<Expr *, 16> Vars;
  Vars.reserve(C->varlist_size());
  for (auto *VE : C->varlists()) {
    ExprResult EVar = getDerived().TransformExpr(cast<Expr>(VE));
    if (EVar.isInvalid())
      return nullptr;
    Vars.push_back(EVar.get());
  }
  return getDerived().RebuildOMPDoacrossClause(
      C->getDependenceType(), C->getDependenceLoc(), C->getColonLoc(), Vars,
      C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *
TreeTransform<Derived>::TransformOMPXAttributeClause(OMPXAttributeClause *C) {
  SmallVector<const Attr *> NewAttrs;
  for (auto *A : C->getAttrs())
    NewAttrs.push_back(getDerived().TransformAttr(A));
  return getDerived().RebuildOMPXAttributeClause(
      NewAttrs, C->getBeginLoc(), C->getLParenLoc(), C->getEndLoc());
}

template <typename Derived>
OMPClause *TreeTransform<Derived>::TransformOMPXBareClause(OMPXBareClause *C) {
  return getDerived().RebuildOMPXBareClause(C->getBeginLoc(), C->getEndLoc());
}

//===----------------------------------------------------------------------===//
// OpenACC transformation
//===----------------------------------------------------------------------===//
namespace {
template <typename Derived>
class OpenACCClauseTransform final
    : public OpenACCClauseVisitor<OpenACCClauseTransform<Derived>> {
  TreeTransform<Derived> &Self;
  ArrayRef<const OpenACCClause *> ExistingClauses;
  SemaOpenACC::OpenACCParsedClause &ParsedClause;
  OpenACCClause *NewClause = nullptr;

  llvm::SmallVector<Expr *> VisitVarList(ArrayRef<Expr *> VarList) {
    llvm::SmallVector<Expr *> InstantiatedVarList;
    for (Expr *CurVar : VarList) {
      ExprResult Res = Self.TransformExpr(CurVar);

      if (!Res.isUsable())
        continue;

      Res = Self.getSema().OpenACC().ActOnVar(ParsedClause.getClauseKind(),
                                              Res.get());

      if (Res.isUsable())
        InstantiatedVarList.push_back(Res.get());
    }

    return InstantiatedVarList;
  }

public:
  OpenACCClauseTransform(TreeTransform<Derived> &Self,
                         ArrayRef<const OpenACCClause *> ExistingClauses,
                         SemaOpenACC::OpenACCParsedClause &PC)
      : Self(Self), ExistingClauses(ExistingClauses), ParsedClause(PC) {}

  OpenACCClause *CreatedClause() const { return NewClause; }

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  void Visit##CLAUSE_NAME##Clause(const OpenACC##CLAUSE_NAME##Clause &Clause);
#include "clang/Basic/OpenACCClauses.def"
};

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitDefaultClause(
    const OpenACCDefaultClause &C) {
  ParsedClause.setDefaultDetails(C.getDefaultClauseKind());

  NewClause = OpenACCDefaultClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getDefaultClauseKind(),
      ParsedClause.getBeginLoc(), ParsedClause.getLParenLoc(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitIfClause(const OpenACCIfClause &C) {
  Expr *Cond = const_cast<Expr *>(C.getConditionExpr());
  assert(Cond && "If constructed with invalid Condition");
  Sema::ConditionResult Res = Self.TransformCondition(
      Cond->getExprLoc(), /*Var=*/nullptr, Cond, Sema::ConditionKind::Boolean);

  if (Res.isInvalid() || !Res.get().second)
    return;

  ParsedClause.setConditionDetails(Res.get().second);

  NewClause = OpenACCIfClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getConditionExpr(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitSelfClause(
    const OpenACCSelfClause &C) {

  if (C.hasConditionExpr()) {
    Expr *Cond = const_cast<Expr *>(C.getConditionExpr());
    Sema::ConditionResult Res =
        Self.TransformCondition(Cond->getExprLoc(), /*Var=*/nullptr, Cond,
                                Sema::ConditionKind::Boolean);

    if (Res.isInvalid() || !Res.get().second)
      return;

    ParsedClause.setConditionDetails(Res.get().second);
  }

  NewClause = OpenACCSelfClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getConditionExpr(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitNumGangsClause(
    const OpenACCNumGangsClause &C) {
  llvm::SmallVector<Expr *> InstantiatedIntExprs;

  for (Expr *CurIntExpr : C.getIntExprs()) {
    ExprResult Res = Self.TransformExpr(CurIntExpr);

    if (!Res.isUsable())
      return;

    Res = Self.getSema().OpenACC().ActOnIntExpr(OpenACCDirectiveKind::Invalid,
                                                C.getClauseKind(),
                                                C.getBeginLoc(), Res.get());
    if (!Res.isUsable())
      return;

    InstantiatedIntExprs.push_back(Res.get());
  }

  ParsedClause.setIntExprDetails(InstantiatedIntExprs);
  NewClause = OpenACCNumGangsClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getIntExprs(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitPrivateClause(
    const OpenACCPrivateClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, /*IsZero=*/false);

  NewClause = OpenACCPrivateClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitFirstPrivateClause(
    const OpenACCFirstPrivateClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, /*IsZero=*/false);

  NewClause = OpenACCFirstPrivateClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitNoCreateClause(
    const OpenACCNoCreateClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, /*IsZero=*/false);

  NewClause = OpenACCNoCreateClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitPresentClause(
    const OpenACCPresentClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, /*IsZero=*/false);

  NewClause = OpenACCPresentClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitCopyClause(
    const OpenACCCopyClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, /*IsZero=*/false);

  NewClause = OpenACCCopyClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getClauseKind(),
      ParsedClause.getBeginLoc(), ParsedClause.getLParenLoc(),
      ParsedClause.getVarList(), ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitCopyInClause(
    const OpenACCCopyInClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()), C.isReadOnly(),
                                 /*IsZero=*/false);

  NewClause = OpenACCCopyInClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getClauseKind(),
      ParsedClause.getBeginLoc(), ParsedClause.getLParenLoc(),
      ParsedClause.isReadOnly(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitCopyOutClause(
    const OpenACCCopyOutClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, C.isZero());

  NewClause = OpenACCCopyOutClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getClauseKind(),
      ParsedClause.getBeginLoc(), ParsedClause.getLParenLoc(),
      ParsedClause.isZero(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitCreateClause(
    const OpenACCCreateClause &C) {
  ParsedClause.setVarListDetails(VisitVarList(C.getVarList()),
                                 /*IsReadOnly=*/false, C.isZero());

  NewClause = OpenACCCreateClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getClauseKind(),
      ParsedClause.getBeginLoc(), ParsedClause.getLParenLoc(),
      ParsedClause.isZero(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}
template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitAttachClause(
    const OpenACCAttachClause &C) {
  llvm::SmallVector<Expr *> VarList = VisitVarList(C.getVarList());

  // Ensure each var is a pointer type.
  VarList.erase(std::remove_if(VarList.begin(), VarList.end(), [&](Expr *E) {
    return Self.getSema().OpenACC().CheckVarIsPointerType(
        OpenACCClauseKind::Attach, E);
  }), VarList.end());

  ParsedClause.setVarListDetails(VarList,
                                 /*IsReadOnly=*/false, /*IsZero=*/false);
  NewClause = OpenACCAttachClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitDevicePtrClause(
    const OpenACCDevicePtrClause &C) {
  llvm::SmallVector<Expr *> VarList = VisitVarList(C.getVarList());

  // Ensure each var is a pointer type.
  VarList.erase(std::remove_if(VarList.begin(), VarList.end(), [&](Expr *E) {
    return Self.getSema().OpenACC().CheckVarIsPointerType(
        OpenACCClauseKind::DevicePtr, E);
  }), VarList.end());

  ParsedClause.setVarListDetails(VarList,
                                 /*IsReadOnly=*/false, /*IsZero=*/false);
  NewClause = OpenACCDevicePtrClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getVarList(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitNumWorkersClause(
    const OpenACCNumWorkersClause &C) {
  Expr *IntExpr = const_cast<Expr *>(C.getIntExpr());
  assert(IntExpr && "num_workers clause constructed with invalid int expr");

  ExprResult Res = Self.TransformExpr(IntExpr);
  if (!Res.isUsable())
    return;

  Res = Self.getSema().OpenACC().ActOnIntExpr(OpenACCDirectiveKind::Invalid,
                                              C.getClauseKind(),
                                              C.getBeginLoc(), Res.get());
  if (!Res.isUsable())
    return;

  ParsedClause.setIntExprDetails(Res.get());
  NewClause = OpenACCNumWorkersClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getIntExprs()[0],
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitVectorLengthClause(
    const OpenACCVectorLengthClause &C) {
  Expr *IntExpr = const_cast<Expr *>(C.getIntExpr());
  assert(IntExpr && "vector_length clause constructed with invalid int expr");

  ExprResult Res = Self.TransformExpr(IntExpr);
  if (!Res.isUsable())
    return;

  Res = Self.getSema().OpenACC().ActOnIntExpr(OpenACCDirectiveKind::Invalid,
                                              C.getClauseKind(),
                                              C.getBeginLoc(), Res.get());
  if (!Res.isUsable())
    return;

  ParsedClause.setIntExprDetails(Res.get());
  NewClause = OpenACCVectorLengthClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getIntExprs()[0],
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitAsyncClause(
    const OpenACCAsyncClause &C) {
  if (C.hasIntExpr()) {
    ExprResult Res = Self.TransformExpr(const_cast<Expr *>(C.getIntExpr()));
    if (!Res.isUsable())
      return;

    Res = Self.getSema().OpenACC().ActOnIntExpr(OpenACCDirectiveKind::Invalid,
                                                C.getClauseKind(),
                                                C.getBeginLoc(), Res.get());
    if (!Res.isUsable())
      return;
    ParsedClause.setIntExprDetails(Res.get());
  }

  NewClause = OpenACCAsyncClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(),
      ParsedClause.getNumIntExprs() != 0 ? ParsedClause.getIntExprs()[0]
                                         : nullptr,
      ParsedClause.getEndLoc());
}
template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitWaitClause(
    const OpenACCWaitClause &C) {
  if (!C.getLParenLoc().isInvalid()) {
    Expr *DevNumExpr = nullptr;
    llvm::SmallVector<Expr *> InstantiatedQueueIdExprs;

    // Instantiate devnum expr if it exists.
    if (C.getDevNumExpr()) {
      ExprResult Res = Self.TransformExpr(C.getDevNumExpr());
      if (!Res.isUsable())
        return;
      Res = Self.getSema().OpenACC().ActOnIntExpr(OpenACCDirectiveKind::Invalid,
                                                  C.getClauseKind(),
                                                  C.getBeginLoc(), Res.get());
      if (!Res.isUsable())
        return;

      DevNumExpr = Res.get();
    }

    // Instantiate queue ids.
    for (Expr *CurQueueIdExpr : C.getQueueIdExprs()) {
      ExprResult Res = Self.TransformExpr(CurQueueIdExpr);
      if (!Res.isUsable())
        return;
      Res = Self.getSema().OpenACC().ActOnIntExpr(OpenACCDirectiveKind::Invalid,
                                                  C.getClauseKind(),
                                                  C.getBeginLoc(), Res.get());
      if (!Res.isUsable())
        return;

      InstantiatedQueueIdExprs.push_back(Res.get());
    }

    ParsedClause.setWaitDetails(DevNumExpr, C.getQueuesLoc(),
                                std::move(InstantiatedQueueIdExprs));
  }

  NewClause = OpenACCWaitClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), ParsedClause.getDevNumExpr(),
      ParsedClause.getQueuesLoc(), ParsedClause.getQueueIdExprs(),
      ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitDeviceTypeClause(
    const OpenACCDeviceTypeClause &C) {
  // Nothing to transform here, just create a new version of 'C'.
  NewClause = OpenACCDeviceTypeClause::Create(
      Self.getSema().getASTContext(), C.getClauseKind(),
      ParsedClause.getBeginLoc(), ParsedClause.getLParenLoc(),
      C.getArchitectures(), ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitAutoClause(
    const OpenACCAutoClause &C) {
  // Nothing to do, so just create a new node.
  NewClause = OpenACCAutoClause::Create(Self.getSema().getASTContext(),
                                        ParsedClause.getBeginLoc(),
                                        ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitIndependentClause(
    const OpenACCIndependentClause &C) {
  NewClause = OpenACCIndependentClause::Create(Self.getSema().getASTContext(),
                                               ParsedClause.getBeginLoc(),
                                               ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitSeqClause(
    const OpenACCSeqClause &C) {
  NewClause = OpenACCSeqClause::Create(Self.getSema().getASTContext(),
                                       ParsedClause.getBeginLoc(),
                                       ParsedClause.getEndLoc());
}

template <typename Derived>
void OpenACCClauseTransform<Derived>::VisitReductionClause(
    const OpenACCReductionClause &C) {
  SmallVector<Expr *> TransformedVars = VisitVarList(C.getVarList());
  SmallVector<Expr *> ValidVars;

  for (Expr *Var : TransformedVars) {
    ExprResult Res = Self.getSema().OpenACC().CheckReductionVar(Var);
    if (Res.isUsable())
      ValidVars.push_back(Res.get());
  }

  NewClause = OpenACCReductionClause::Create(
      Self.getSema().getASTContext(), ParsedClause.getBeginLoc(),
      ParsedClause.getLParenLoc(), C.getReductionOp(), ValidVars,
      ParsedClause.getEndLoc());
}
} // namespace
template <typename Derived>
OpenACCClause *TreeTransform<Derived>::TransformOpenACCClause(
    ArrayRef<const OpenACCClause *> ExistingClauses,
    OpenACCDirectiveKind DirKind, const OpenACCClause *OldClause) {

  SemaOpenACC::OpenACCParsedClause ParsedClause(
      DirKind, OldClause->getClauseKind(), OldClause->getBeginLoc());
  ParsedClause.setEndLoc(OldClause->getEndLoc());

  if (const auto *WithParms = dyn_cast<OpenACCClauseWithParams>(OldClause))
    ParsedClause.setLParenLoc(WithParms->getLParenLoc());

  OpenACCClauseTransform<Derived> Transform{*this, ExistingClauses,
                                            ParsedClause};
  Transform.Visit(OldClause);

  return Transform.CreatedClause();
}

template <typename Derived>
llvm::SmallVector<OpenACCClause *>
TreeTransform<Derived>::TransformOpenACCClauseList(
    OpenACCDirectiveKind DirKind, ArrayRef<const OpenACCClause *> OldClauses) {
  llvm::SmallVector<OpenACCClause *> TransformedClauses;
  for (const auto *Clause : OldClauses) {
    if (OpenACCClause *TransformedClause = getDerived().TransformOpenACCClause(
            TransformedClauses, DirKind, Clause))
      TransformedClauses.push_back(TransformedClause);
  }
  return TransformedClauses;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformOpenACCComputeConstruct(
    OpenACCComputeConstruct *C) {
  getSema().OpenACC().ActOnConstruct(C->getDirectiveKind(), C->getBeginLoc());

  if (getSema().OpenACC().ActOnStartStmtDirective(C->getDirectiveKind(),
                                                  C->getBeginLoc()))
    return StmtError();

  llvm::SmallVector<OpenACCClause *> TransformedClauses =
      getDerived().TransformOpenACCClauseList(C->getDirectiveKind(),
                                              C->clauses());
  // Transform Structured Block.
  SemaOpenACC::AssociatedStmtRAII AssocStmtRAII(getSema().OpenACC(),
                                                C->getDirectiveKind());
  StmtResult StrBlock = getDerived().TransformStmt(C->getStructuredBlock());
  StrBlock = getSema().OpenACC().ActOnAssociatedStmt(
      C->getBeginLoc(), C->getDirectiveKind(), StrBlock);

  return getDerived().RebuildOpenACCComputeConstruct(
      C->getDirectiveKind(), C->getBeginLoc(), C->getDirectiveLoc(),
      C->getEndLoc(), TransformedClauses, StrBlock);
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformOpenACCLoopConstruct(OpenACCLoopConstruct *C) {

  getSema().OpenACC().ActOnConstruct(C->getDirectiveKind(), C->getBeginLoc());

  if (getSema().OpenACC().ActOnStartStmtDirective(C->getDirectiveKind(),
                                                  C->getBeginLoc()))
    return StmtError();

  llvm::SmallVector<OpenACCClause *> TransformedClauses =
      getDerived().TransformOpenACCClauseList(C->getDirectiveKind(),
                                              C->clauses());

  // Transform Loop.
  SemaOpenACC::AssociatedStmtRAII AssocStmtRAII(getSema().OpenACC(),
                                                C->getDirectiveKind());
  StmtResult Loop = getDerived().TransformStmt(C->getLoop());
  Loop = getSema().OpenACC().ActOnAssociatedStmt(C->getBeginLoc(),
                                                 C->getDirectiveKind(), Loop);

  return getDerived().RebuildOpenACCLoopConstruct(
      C->getBeginLoc(), C->getDirectiveLoc(), C->getEndLoc(),
      TransformedClauses, Loop);
}

//===----------------------------------------------------------------------===//
// Expression transformation
//===----------------------------------------------------------------------===//
template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformConstantExpr(ConstantExpr *E) {
  return TransformExpr(E->getSubExpr());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformSYCLUniqueStableNameExpr(
    SYCLUniqueStableNameExpr *E) {
  if (!E->isTypeDependent())
    return E;

  TypeSourceInfo *NewT = getDerived().TransformType(E->getTypeSourceInfo());

  if (!NewT)
    return ExprError();

  if (!getDerived().AlwaysRebuild() && E->getTypeSourceInfo() == NewT)
    return E;

  return getDerived().RebuildSYCLUniqueStableNameExpr(
      E->getLocation(), E->getLParenLocation(), E->getRParenLocation(), NewT);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformPredefinedExpr(PredefinedExpr *E) {
  if (!E->isTypeDependent())
    return E;

  return getDerived().RebuildPredefinedExpr(E->getLocation(),
                                            E->getIdentKind());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformDeclRefExpr(DeclRefExpr *E) {
  NestedNameSpecifierLoc QualifierLoc;
  if (E->getQualifierLoc()) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(E->getQualifierLoc());
    if (!QualifierLoc)
      return ExprError();
  }

  ValueDecl *ND
    = cast_or_null<ValueDecl>(getDerived().TransformDecl(E->getLocation(),
                                                         E->getDecl()));
  if (!ND)
    return ExprError();

  NamedDecl *Found = ND;
  if (E->getFoundDecl() != E->getDecl()) {
    Found = cast_or_null<NamedDecl>(
        getDerived().TransformDecl(E->getLocation(), E->getFoundDecl()));
    if (!Found)
      return ExprError();
  }

  DeclarationNameInfo NameInfo = E->getNameInfo();
  if (NameInfo.getName()) {
    NameInfo = getDerived().TransformDeclarationNameInfo(NameInfo);
    if (!NameInfo.getName())
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      !E->isCapturedByCopyInLambdaWithExplicitObjectParameter() &&
      QualifierLoc == E->getQualifierLoc() && ND == E->getDecl() &&
      Found == E->getFoundDecl() &&
      NameInfo.getName() == E->getDecl()->getDeclName() &&
      !E->hasExplicitTemplateArgs()) {

    // Mark it referenced in the new context regardless.
    // FIXME: this is a bit instantiation-specific.
    SemaRef.MarkDeclRefReferenced(E);

    return E;
  }

  TemplateArgumentListInfo TransArgs, *TemplateArgs = nullptr;
  if (E->hasExplicitTemplateArgs()) {
    TemplateArgs = &TransArgs;
    TransArgs.setLAngleLoc(E->getLAngleLoc());
    TransArgs.setRAngleLoc(E->getRAngleLoc());
    if (getDerived().TransformTemplateArguments(E->getTemplateArgs(),
                                                E->getNumTemplateArgs(),
                                                TransArgs))
      return ExprError();
  }

  return getDerived().RebuildDeclRefExpr(QualifierLoc, ND, NameInfo,
                                         Found, TemplateArgs);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformIntegerLiteral(IntegerLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformFixedPointLiteral(
    FixedPointLiteral *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformFloatingLiteral(FloatingLiteral *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformImaginaryLiteral(ImaginaryLiteral *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformStringLiteral(StringLiteral *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCharacterLiteral(CharacterLiteral *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformUserDefinedLiteral(UserDefinedLiteral *E) {
  return getDerived().TransformCallExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformGenericSelectionExpr(GenericSelectionExpr *E) {
  ExprResult ControllingExpr;
  TypeSourceInfo *ControllingType = nullptr;
  if (E->isExprPredicate())
    ControllingExpr = getDerived().TransformExpr(E->getControllingExpr());
  else
    ControllingType = getDerived().TransformType(E->getControllingType());

  if (ControllingExpr.isInvalid() && !ControllingType)
    return ExprError();

  SmallVector<Expr *, 4> AssocExprs;
  SmallVector<TypeSourceInfo *, 4> AssocTypes;
  for (const GenericSelectionExpr::Association Assoc : E->associations()) {
    TypeSourceInfo *TSI = Assoc.getTypeSourceInfo();
    if (TSI) {
      TypeSourceInfo *AssocType = getDerived().TransformType(TSI);
      if (!AssocType)
        return ExprError();
      AssocTypes.push_back(AssocType);
    } else {
      AssocTypes.push_back(nullptr);
    }

    ExprResult AssocExpr =
        getDerived().TransformExpr(Assoc.getAssociationExpr());
    if (AssocExpr.isInvalid())
      return ExprError();
    AssocExprs.push_back(AssocExpr.get());
  }

  if (!ControllingType)
  return getDerived().RebuildGenericSelectionExpr(E->getGenericLoc(),
                                                  E->getDefaultLoc(),
                                                  E->getRParenLoc(),
                                                  ControllingExpr.get(),
                                                  AssocTypes,
                                                  AssocExprs);
  return getDerived().RebuildGenericSelectionExpr(
      E->getGenericLoc(), E->getDefaultLoc(), E->getRParenLoc(),
      ControllingType, AssocTypes, AssocExprs);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformParenExpr(ParenExpr *E) {
  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildParenExpr(SubExpr.get(), E->getLParen(),
                                       E->getRParen());
}

/// The operand of a unary address-of operator has special rules: it's
/// allowed to refer to a non-static member of a class even if there's no 'this'
/// object available.
template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformAddressOfOperand(Expr *E) {
  if (DependentScopeDeclRefExpr *DRE = dyn_cast<DependentScopeDeclRefExpr>(E))
    return getDerived().TransformDependentScopeDeclRefExpr(
        DRE, /*IsAddressOfOperand=*/true, nullptr);
  else if (UnresolvedLookupExpr *ULE = dyn_cast<UnresolvedLookupExpr>(E))
    return getDerived().TransformUnresolvedLookupExpr(
        ULE, /*IsAddressOfOperand=*/true);
  else
    return getDerived().TransformExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformUnaryOperator(UnaryOperator *E) {
  ExprResult SubExpr;
  if (E->getOpcode() == UO_AddrOf)
    SubExpr = TransformAddressOfOperand(E->getSubExpr());
  else
    SubExpr = TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildUnaryOperator(E->getOperatorLoc(),
                                           E->getOpcode(),
                                           SubExpr.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformOffsetOfExpr(OffsetOfExpr *E) {
  // Transform the type.
  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeSourceInfo());
  if (!Type)
    return ExprError();

  // Transform all of the components into components similar to what the
  // parser uses.
  // FIXME: It would be slightly more efficient in the non-dependent case to
  // just map FieldDecls, rather than requiring the rebuilder to look for
  // the fields again. However, __builtin_offsetof is rare enough in
  // template code that we don't care.
  bool ExprChanged = false;
  typedef Sema::OffsetOfComponent Component;
  SmallVector<Component, 4> Components;
  for (unsigned I = 0, N = E->getNumComponents(); I != N; ++I) {
    const OffsetOfNode &ON = E->getComponent(I);
    Component Comp;
    Comp.isBrackets = true;
    Comp.LocStart = ON.getSourceRange().getBegin();
    Comp.LocEnd = ON.getSourceRange().getEnd();
    switch (ON.getKind()) {
    case OffsetOfNode::Array: {
      Expr *FromIndex = E->getIndexExpr(ON.getArrayExprIndex());
      ExprResult Index = getDerived().TransformExpr(FromIndex);
      if (Index.isInvalid())
        return ExprError();

      ExprChanged = ExprChanged || Index.get() != FromIndex;
      Comp.isBrackets = true;
      Comp.U.E = Index.get();
      break;
    }

    case OffsetOfNode::Field:
    case OffsetOfNode::Identifier:
      Comp.isBrackets = false;
      Comp.U.IdentInfo = ON.getFieldName();
      if (!Comp.U.IdentInfo)
        continue;

      break;

    case OffsetOfNode::Base:
      // Will be recomputed during the rebuild.
      continue;
    }

    Components.push_back(Comp);
  }

  // If nothing changed, retain the existing expression.
  if (!getDerived().AlwaysRebuild() &&
      Type == E->getTypeSourceInfo() &&
      !ExprChanged)
    return E;

  // Build a new offsetof expression.
  return getDerived().RebuildOffsetOfExpr(E->getOperatorLoc(), Type,
                                          Components, E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformOpaqueValueExpr(OpaqueValueExpr *E) {
  assert((!E->getSourceExpr() || getDerived().AlreadyTransformed(E->getType())) &&
         "opaque value expression requires transformation");
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformTypoExpr(TypoExpr *E) {
  return E;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformRecoveryExpr(RecoveryExpr *E) {
  llvm::SmallVector<Expr *, 8> Children;
  bool Changed = false;
  for (Expr *C : E->subExpressions()) {
    ExprResult NewC = getDerived().TransformExpr(C);
    if (NewC.isInvalid())
      return ExprError();
    Children.push_back(NewC.get());

    Changed |= NewC.get() != C;
  }
  if (!getDerived().AlwaysRebuild() && !Changed)
    return E;
  return getDerived().RebuildRecoveryExpr(E->getBeginLoc(), E->getEndLoc(),
                                          Children, E->getType());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformPseudoObjectExpr(PseudoObjectExpr *E) {
  // Rebuild the syntactic form.  The original syntactic form has
  // opaque-value expressions in it, so strip those away and rebuild
  // the result.  This is a really awful way of doing this, but the
  // better solution (rebuilding the semantic expressions and
  // rebinding OVEs as necessary) doesn't work; we'd need
  // TreeTransform to not strip away implicit conversions.
  Expr *newSyntacticForm = SemaRef.PseudoObject().recreateSyntacticForm(E);
  ExprResult result = getDerived().TransformExpr(newSyntacticForm);
  if (result.isInvalid()) return ExprError();

  // If that gives us a pseudo-object result back, the pseudo-object
  // expression must have been an lvalue-to-rvalue conversion which we
  // should reapply.
  if (result.get()->hasPlaceholderType(BuiltinType::PseudoObject))
    result = SemaRef.PseudoObject().checkRValue(result.get());

  return result;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformUnaryExprOrTypeTraitExpr(
                                                UnaryExprOrTypeTraitExpr *E) {
  if (E->isArgumentType()) {
    TypeSourceInfo *OldT = E->getArgumentTypeInfo();

    TypeSourceInfo *NewT = getDerived().TransformType(OldT);
    if (!NewT)
      return ExprError();

    if (!getDerived().AlwaysRebuild() && OldT == NewT)
      return E;

    return getDerived().RebuildUnaryExprOrTypeTrait(NewT, E->getOperatorLoc(),
                                                    E->getKind(),
                                                    E->getSourceRange());
  }

  // C++0x [expr.sizeof]p1:
  //   The operand is either an expression, which is an unevaluated operand
  //   [...]
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated,
      Sema::ReuseLambdaContextDecl);

  // Try to recover if we have something like sizeof(T::X) where X is a type.
  // Notably, there must be *exactly* one set of parens if X is a type.
  TypeSourceInfo *RecoveryTSI = nullptr;
  ExprResult SubExpr;
  auto *PE = dyn_cast<ParenExpr>(E->getArgumentExpr());
  if (auto *DRE =
          PE ? dyn_cast<DependentScopeDeclRefExpr>(PE->getSubExpr()) : nullptr)
    SubExpr = getDerived().TransformParenDependentScopeDeclRefExpr(
        PE, DRE, false, &RecoveryTSI);
  else
    SubExpr = getDerived().TransformExpr(E->getArgumentExpr());

  if (RecoveryTSI) {
    return getDerived().RebuildUnaryExprOrTypeTrait(
        RecoveryTSI, E->getOperatorLoc(), E->getKind(), E->getSourceRange());
  } else if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getArgumentExpr())
    return E;

  return getDerived().RebuildUnaryExprOrTypeTrait(SubExpr.get(),
                                                  E->getOperatorLoc(),
                                                  E->getKind(),
                                                  E->getSourceRange());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformArraySubscriptExpr(ArraySubscriptExpr *E) {
  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(E->getRHS());
  if (RHS.isInvalid())
    return ExprError();


  if (!getDerived().AlwaysRebuild() &&
      LHS.get() == E->getLHS() &&
      RHS.get() == E->getRHS())
    return E;

  return getDerived().RebuildArraySubscriptExpr(
      LHS.get(),
      /*FIXME:*/ E->getLHS()->getBeginLoc(), RHS.get(), E->getRBracketLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformMatrixSubscriptExpr(MatrixSubscriptExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  ExprResult RowIdx = getDerived().TransformExpr(E->getRowIdx());
  if (RowIdx.isInvalid())
    return ExprError();

  ExprResult ColumnIdx = getDerived().TransformExpr(E->getColumnIdx());
  if (ColumnIdx.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Base.get() == E->getBase() &&
      RowIdx.get() == E->getRowIdx() && ColumnIdx.get() == E->getColumnIdx())
    return E;

  return getDerived().RebuildMatrixSubscriptExpr(
      Base.get(), RowIdx.get(), ColumnIdx.get(), E->getRBracketLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformArraySectionExpr(ArraySectionExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  ExprResult LowerBound;
  if (E->getLowerBound()) {
    LowerBound = getDerived().TransformExpr(E->getLowerBound());
    if (LowerBound.isInvalid())
      return ExprError();
  }

  ExprResult Length;
  if (E->getLength()) {
    Length = getDerived().TransformExpr(E->getLength());
    if (Length.isInvalid())
      return ExprError();
  }

  ExprResult Stride;
  if (E->isOMPArraySection()) {
    if (Expr *Str = E->getStride()) {
      Stride = getDerived().TransformExpr(Str);
      if (Stride.isInvalid())
        return ExprError();
    }
  }

  if (!getDerived().AlwaysRebuild() && Base.get() == E->getBase() &&
      LowerBound.get() == E->getLowerBound() &&
      Length.get() == E->getLength() &&
      (E->isOpenACCArraySection() || Stride.get() == E->getStride()))
    return E;

  return getDerived().RebuildArraySectionExpr(
      E->isOMPArraySection(), Base.get(), E->getBase()->getEndLoc(),
      LowerBound.get(), E->getColonLocFirst(),
      E->isOMPArraySection() ? E->getColonLocSecond() : SourceLocation{},
      Length.get(), Stride.get(), E->getRBracketLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformOMPArrayShapingExpr(OMPArrayShapingExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  SmallVector<Expr *, 4> Dims;
  bool ErrorFound = false;
  for (Expr *Dim : E->getDimensions()) {
    ExprResult DimRes = getDerived().TransformExpr(Dim);
    if (DimRes.isInvalid()) {
      ErrorFound = true;
      continue;
    }
    Dims.push_back(DimRes.get());
  }

  if (ErrorFound)
    return ExprError();
  return getDerived().RebuildOMPArrayShapingExpr(Base.get(), E->getLParenLoc(),
                                                 E->getRParenLoc(), Dims,
                                                 E->getBracketsRanges());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformOMPIteratorExpr(OMPIteratorExpr *E) {
  unsigned NumIterators = E->numOfIterators();
  SmallVector<SemaOpenMP::OMPIteratorData, 4> Data(NumIterators);

  bool ErrorFound = false;
  bool NeedToRebuild = getDerived().AlwaysRebuild();
  for (unsigned I = 0; I < NumIterators; ++I) {
    auto *D = cast<VarDecl>(E->getIteratorDecl(I));
    Data[I].DeclIdent = D->getIdentifier();
    Data[I].DeclIdentLoc = D->getLocation();
    if (D->getLocation() == D->getBeginLoc()) {
      assert(SemaRef.Context.hasSameType(D->getType(), SemaRef.Context.IntTy) &&
             "Implicit type must be int.");
    } else {
      TypeSourceInfo *TSI = getDerived().TransformType(D->getTypeSourceInfo());
      QualType DeclTy = getDerived().TransformType(D->getType());
      Data[I].Type = SemaRef.CreateParsedType(DeclTy, TSI);
    }
    OMPIteratorExpr::IteratorRange Range = E->getIteratorRange(I);
    ExprResult Begin = getDerived().TransformExpr(Range.Begin);
    ExprResult End = getDerived().TransformExpr(Range.End);
    ExprResult Step = getDerived().TransformExpr(Range.Step);
    ErrorFound = ErrorFound ||
                 !(!D->getTypeSourceInfo() || (Data[I].Type.getAsOpaquePtr() &&
                                               !Data[I].Type.get().isNull())) ||
                 Begin.isInvalid() || End.isInvalid() || Step.isInvalid();
    if (ErrorFound)
      continue;
    Data[I].Range.Begin = Begin.get();
    Data[I].Range.End = End.get();
    Data[I].Range.Step = Step.get();
    Data[I].AssignLoc = E->getAssignLoc(I);
    Data[I].ColonLoc = E->getColonLoc(I);
    Data[I].SecColonLoc = E->getSecondColonLoc(I);
    NeedToRebuild =
        NeedToRebuild ||
        (D->getTypeSourceInfo() && Data[I].Type.get().getTypePtrOrNull() !=
                                       D->getType().getTypePtrOrNull()) ||
        Range.Begin != Data[I].Range.Begin || Range.End != Data[I].Range.End ||
        Range.Step != Data[I].Range.Step;
  }
  if (ErrorFound)
    return ExprError();
  if (!NeedToRebuild)
    return E;

  ExprResult Res = getDerived().RebuildOMPIteratorExpr(
      E->getIteratorKwLoc(), E->getLParenLoc(), E->getRParenLoc(), Data);
  if (!Res.isUsable())
    return Res;
  auto *IE = cast<OMPIteratorExpr>(Res.get());
  for (unsigned I = 0; I < NumIterators; ++I)
    getDerived().transformedLocalDecl(E->getIteratorDecl(I),
                                      IE->getIteratorDecl(I));
  return Res;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCallExpr(CallExpr *E) {
  // Transform the callee.
  ExprResult Callee = getDerived().TransformExpr(E->getCallee());
  if (Callee.isInvalid())
    return ExprError();

  // Transform arguments.
  bool ArgChanged = false;
  SmallVector<Expr*, 8> Args;
  if (getDerived().TransformExprs(E->getArgs(), E->getNumArgs(), true, Args,
                                  &ArgChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Callee.get() == E->getCallee() &&
      !ArgChanged)
    return SemaRef.MaybeBindToTemporary(E);

  // FIXME: Wrong source location information for the '('.
  SourceLocation FakeLParenLoc
    = ((Expr *)Callee.get())->getSourceRange().getBegin();

  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  if (E->hasStoredFPFeatures()) {
    FPOptionsOverride NewOverrides = E->getFPFeatures();
    getSema().CurFPFeatures =
        NewOverrides.applyOverrides(getSema().getLangOpts());
    getSema().FpPragmaStack.CurrentValue = NewOverrides;
  }

  return getDerived().RebuildCallExpr(Callee.get(), FakeLParenLoc,
                                      Args,
                                      E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformMemberExpr(MemberExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  NestedNameSpecifierLoc QualifierLoc;
  if (E->hasQualifier()) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(E->getQualifierLoc());

    if (!QualifierLoc)
      return ExprError();
  }
  SourceLocation TemplateKWLoc = E->getTemplateKeywordLoc();

  ValueDecl *Member
    = cast_or_null<ValueDecl>(getDerived().TransformDecl(E->getMemberLoc(),
                                                         E->getMemberDecl()));
  if (!Member)
    return ExprError();

  NamedDecl *FoundDecl = E->getFoundDecl();
  if (FoundDecl == E->getMemberDecl()) {
    FoundDecl = Member;
  } else {
    FoundDecl = cast_or_null<NamedDecl>(
                   getDerived().TransformDecl(E->getMemberLoc(), FoundDecl));
    if (!FoundDecl)
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      Base.get() == E->getBase() &&
      QualifierLoc == E->getQualifierLoc() &&
      Member == E->getMemberDecl() &&
      FoundDecl == E->getFoundDecl() &&
      !E->hasExplicitTemplateArgs()) {

    // Skip for member expression of (this->f), rebuilt thisi->f is needed
    // for Openmp where the field need to be privatizized in the case.
    if (!(isa<CXXThisExpr>(E->getBase()) &&
          getSema().OpenMP().isOpenMPRebuildMemberExpr(
              cast<ValueDecl>(Member)))) {
      // Mark it referenced in the new context regardless.
      // FIXME: this is a bit instantiation-specific.
      SemaRef.MarkMemberReferenced(E);
      return E;
    }
  }

  TemplateArgumentListInfo TransArgs;
  if (E->hasExplicitTemplateArgs()) {
    TransArgs.setLAngleLoc(E->getLAngleLoc());
    TransArgs.setRAngleLoc(E->getRAngleLoc());
    if (getDerived().TransformTemplateArguments(E->getTemplateArgs(),
                                                E->getNumTemplateArgs(),
                                                TransArgs))
      return ExprError();
  }

  // FIXME: Bogus source location for the operator
  SourceLocation FakeOperatorLoc =
      SemaRef.getLocForEndOfToken(E->getBase()->getSourceRange().getEnd());

  // FIXME: to do this check properly, we will need to preserve the
  // first-qualifier-in-scope here, just in case we had a dependent
  // base (and therefore couldn't do the check) and a
  // nested-name-qualifier (and therefore could do the lookup).
  NamedDecl *FirstQualifierInScope = nullptr;
  DeclarationNameInfo MemberNameInfo = E->getMemberNameInfo();
  if (MemberNameInfo.getName()) {
    MemberNameInfo = getDerived().TransformDeclarationNameInfo(MemberNameInfo);
    if (!MemberNameInfo.getName())
      return ExprError();
  }

  return getDerived().RebuildMemberExpr(Base.get(), FakeOperatorLoc,
                                        E->isArrow(),
                                        QualifierLoc,
                                        TemplateKWLoc,
                                        MemberNameInfo,
                                        Member,
                                        FoundDecl,
                                        (E->hasExplicitTemplateArgs()
                                           ? &TransArgs : nullptr),
                                        FirstQualifierInScope);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformBinaryOperator(BinaryOperator *E) {
  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS =
      getDerived().TransformInitializer(E->getRHS(), /*NotCopyInit=*/false);
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      LHS.get() == E->getLHS() &&
      RHS.get() == E->getRHS())
    return E;

  if (E->isCompoundAssignmentOp())
    // FPFeatures has already been established from trailing storage
    return getDerived().RebuildBinaryOperator(
        E->getOperatorLoc(), E->getOpcode(), LHS.get(), RHS.get());
  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  FPOptionsOverride NewOverrides(E->getFPFeatures());
  getSema().CurFPFeatures =
      NewOverrides.applyOverrides(getSema().getLangOpts());
  getSema().FpPragmaStack.CurrentValue = NewOverrides;
  return getDerived().RebuildBinaryOperator(E->getOperatorLoc(), E->getOpcode(),
                                            LHS.get(), RHS.get());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformCXXRewrittenBinaryOperator(
    CXXRewrittenBinaryOperator *E) {
  CXXRewrittenBinaryOperator::DecomposedForm Decomp = E->getDecomposedForm();

  ExprResult LHS = getDerived().TransformExpr(const_cast<Expr*>(Decomp.LHS));
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(const_cast<Expr*>(Decomp.RHS));
  if (RHS.isInvalid())
    return ExprError();

  // Extract the already-resolved callee declarations so that we can restrict
  // ourselves to using them as the unqualified lookup results when rebuilding.
  UnresolvedSet<2> UnqualLookups;
  bool ChangedAnyLookups = false;
  Expr *PossibleBinOps[] = {E->getSemanticForm(),
                            const_cast<Expr *>(Decomp.InnerBinOp)};
  for (Expr *PossibleBinOp : PossibleBinOps) {
    auto *Op = dyn_cast<CXXOperatorCallExpr>(PossibleBinOp->IgnoreImplicit());
    if (!Op)
      continue;
    auto *Callee = dyn_cast<DeclRefExpr>(Op->getCallee()->IgnoreImplicit());
    if (!Callee || isa<CXXMethodDecl>(Callee->getDecl()))
      continue;

    // Transform the callee in case we built a call to a local extern
    // declaration.
    NamedDecl *Found = cast_or_null<NamedDecl>(getDerived().TransformDecl(
        E->getOperatorLoc(), Callee->getFoundDecl()));
    if (!Found)
      return ExprError();
    if (Found != Callee->getFoundDecl())
      ChangedAnyLookups = true;
    UnqualLookups.addDecl(Found);
  }

  if (!getDerived().AlwaysRebuild() && !ChangedAnyLookups &&
      LHS.get() == Decomp.LHS && RHS.get() == Decomp.RHS) {
    // Mark all functions used in the rewrite as referenced. Note that when
    // a < b is rewritten to (a <=> b) < 0, both the <=> and the < might be
    // function calls, and/or there might be a user-defined conversion sequence
    // applied to the operands of the <.
    // FIXME: this is a bit instantiation-specific.
    const Expr *StopAt[] = {Decomp.LHS, Decomp.RHS};
    SemaRef.MarkDeclarationsReferencedInExpr(E, false, StopAt);
    return E;
  }

  return getDerived().RebuildCXXRewrittenBinaryOperator(
      E->getOperatorLoc(), Decomp.Opcode, UnqualLookups, LHS.get(), RHS.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCompoundAssignOperator(
                                                      CompoundAssignOperator *E) {
  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  FPOptionsOverride NewOverrides(E->getFPFeatures());
  getSema().CurFPFeatures =
      NewOverrides.applyOverrides(getSema().getLangOpts());
  getSema().FpPragmaStack.CurrentValue = NewOverrides;
  return getDerived().TransformBinaryOperator(E);
}

template<typename Derived>
ExprResult TreeTransform<Derived>::
TransformBinaryConditionalOperator(BinaryConditionalOperator *e) {
  // Just rebuild the common and RHS expressions and see whether we
  // get any changes.

  ExprResult commonExpr = getDerived().TransformExpr(e->getCommon());
  if (commonExpr.isInvalid())
    return ExprError();

  ExprResult rhs = getDerived().TransformExpr(e->getFalseExpr());
  if (rhs.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      commonExpr.get() == e->getCommon() &&
      rhs.get() == e->getFalseExpr())
    return e;

  return getDerived().RebuildConditionalOperator(commonExpr.get(),
                                                 e->getQuestionLoc(),
                                                 nullptr,
                                                 e->getColonLoc(),
                                                 rhs.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformConditionalOperator(ConditionalOperator *E) {
  ExprResult Cond = getDerived().TransformExpr(E->getCond());
  if (Cond.isInvalid())
    return ExprError();

  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(E->getRHS());
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Cond.get() == E->getCond() &&
      LHS.get() == E->getLHS() &&
      RHS.get() == E->getRHS())
    return E;

  return getDerived().RebuildConditionalOperator(Cond.get(),
                                                 E->getQuestionLoc(),
                                                 LHS.get(),
                                                 E->getColonLoc(),
                                                 RHS.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformImplicitCastExpr(ImplicitCastExpr *E) {
  // Implicit casts are eliminated during transformation, since they
  // will be recomputed by semantic analysis after transformation.
  return getDerived().TransformExpr(E->getSubExprAsWritten());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCStyleCastExpr(CStyleCastExpr *E) {
  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeInfoAsWritten());
  if (!Type)
    return ExprError();

  ExprResult SubExpr
    = getDerived().TransformExpr(E->getSubExprAsWritten());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Type == E->getTypeInfoAsWritten() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildCStyleCastExpr(E->getLParenLoc(),
                                            Type,
                                            E->getRParenLoc(),
                                            SubExpr.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCompoundLiteralExpr(CompoundLiteralExpr *E) {
  TypeSourceInfo *OldT = E->getTypeSourceInfo();
  TypeSourceInfo *NewT = getDerived().TransformType(OldT);
  if (!NewT)
    return ExprError();

  ExprResult Init = getDerived().TransformExpr(E->getInitializer());
  if (Init.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      OldT == NewT &&
      Init.get() == E->getInitializer())
    return SemaRef.MaybeBindToTemporary(E);

  // Note: the expression type doesn't necessarily match the
  // type-as-written, but that's okay, because it should always be
  // derivable from the initializer.

  return getDerived().RebuildCompoundLiteralExpr(
      E->getLParenLoc(), NewT,
      /*FIXME:*/ E->getInitializer()->getEndLoc(), Init.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformExtVectorElementExpr(ExtVectorElementExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Base.get() == E->getBase())
    return E;

  // FIXME: Bad source location
  SourceLocation FakeOperatorLoc =
      SemaRef.getLocForEndOfToken(E->getBase()->getEndLoc());
  return getDerived().RebuildExtVectorElementExpr(
      Base.get(), FakeOperatorLoc, E->isArrow(), E->getAccessorLoc(),
      E->getAccessor());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformInitListExpr(InitListExpr *E) {
  if (InitListExpr *Syntactic = E->getSyntacticForm())
    E = Syntactic;

  bool InitChanged = false;

  EnterExpressionEvaluationContext Context(
      getSema(), EnterExpressionEvaluationContext::InitList);

  SmallVector<Expr*, 4> Inits;
  if (getDerived().TransformExprs(E->getInits(), E->getNumInits(), false,
                                  Inits, &InitChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() && !InitChanged) {
    // FIXME: Attempt to reuse the existing syntactic form of the InitListExpr
    // in some cases. We can't reuse it in general, because the syntactic and
    // semantic forms are linked, and we can't know that semantic form will
    // match even if the syntactic form does.
  }

  return getDerived().RebuildInitList(E->getLBraceLoc(), Inits,
                                      E->getRBraceLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformDesignatedInitExpr(DesignatedInitExpr *E) {
  Designation Desig;

  // transform the initializer value
  ExprResult Init = getDerived().TransformExpr(E->getInit());
  if (Init.isInvalid())
    return ExprError();

  // transform the designators.
  SmallVector<Expr*, 4> ArrayExprs;
  bool ExprChanged = false;
  for (const DesignatedInitExpr::Designator &D : E->designators()) {
    if (D.isFieldDesignator()) {
      if (D.getFieldDecl()) {
        FieldDecl *Field = cast_or_null<FieldDecl>(
            getDerived().TransformDecl(D.getFieldLoc(), D.getFieldDecl()));
        if (Field != D.getFieldDecl())
          // Rebuild the expression when the transformed FieldDecl is
          // different to the already assigned FieldDecl.
          ExprChanged = true;
        if (Field->isAnonymousStructOrUnion())
          continue;
      } else {
        // Ensure that the designator expression is rebuilt when there isn't
        // a resolved FieldDecl in the designator as we don't want to assign
        // a FieldDecl to a pattern designator that will be instantiated again.
        ExprChanged = true;
      }
      Desig.AddDesignator(Designator::CreateFieldDesignator(
          D.getFieldName(), D.getDotLoc(), D.getFieldLoc()));
      continue;
    }

    if (D.isArrayDesignator()) {
      ExprResult Index = getDerived().TransformExpr(E->getArrayIndex(D));
      if (Index.isInvalid())
        return ExprError();

      Desig.AddDesignator(
          Designator::CreateArrayDesignator(Index.get(), D.getLBracketLoc()));

      ExprChanged = ExprChanged || Init.get() != E->getArrayIndex(D);
      ArrayExprs.push_back(Index.get());
      continue;
    }

    assert(D.isArrayRangeDesignator() && "New kind of designator?");
    ExprResult Start
      = getDerived().TransformExpr(E->getArrayRangeStart(D));
    if (Start.isInvalid())
      return ExprError();

    ExprResult End = getDerived().TransformExpr(E->getArrayRangeEnd(D));
    if (End.isInvalid())
      return ExprError();

    Desig.AddDesignator(Designator::CreateArrayRangeDesignator(
        Start.get(), End.get(), D.getLBracketLoc(), D.getEllipsisLoc()));

    ExprChanged = ExprChanged || Start.get() != E->getArrayRangeStart(D) ||
                  End.get() != E->getArrayRangeEnd(D);

    ArrayExprs.push_back(Start.get());
    ArrayExprs.push_back(End.get());
  }

  if (!getDerived().AlwaysRebuild() &&
      Init.get() == E->getInit() &&
      !ExprChanged)
    return E;

  return getDerived().RebuildDesignatedInitExpr(Desig, ArrayExprs,
                                                E->getEqualOrColonLoc(),
                                                E->usesGNUSyntax(), Init.get());
}

// Seems that if TransformInitListExpr() only works on the syntactic form of an
// InitListExpr, then a DesignatedInitUpdateExpr is not encountered.
template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformDesignatedInitUpdateExpr(
    DesignatedInitUpdateExpr *E) {
  llvm_unreachable("Unexpected DesignatedInitUpdateExpr in syntactic form of "
                   "initializer");
  return ExprError();
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformNoInitExpr(
    NoInitExpr *E) {
  llvm_unreachable("Unexpected NoInitExpr in syntactic form of initializer");
  return ExprError();
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformArrayInitLoopExpr(ArrayInitLoopExpr *E) {
  llvm_unreachable("Unexpected ArrayInitLoopExpr outside of initializer");
  return ExprError();
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformArrayInitIndexExpr(ArrayInitIndexExpr *E) {
  llvm_unreachable("Unexpected ArrayInitIndexExpr outside of initializer");
  return ExprError();
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformImplicitValueInitExpr(
                                                     ImplicitValueInitExpr *E) {
  TemporaryBase Rebase(*this, E->getBeginLoc(), DeclarationName());

  // FIXME: Will we ever have proper type location here? Will we actually
  // need to transform the type?
  QualType T = getDerived().TransformType(E->getType());
  if (T.isNull())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      T == E->getType())
    return E;

  return getDerived().RebuildImplicitValueInitExpr(T);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformVAArgExpr(VAArgExpr *E) {
  TypeSourceInfo *TInfo = getDerived().TransformType(E->getWrittenTypeInfo());
  if (!TInfo)
    return ExprError();

  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      TInfo == E->getWrittenTypeInfo() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildVAArgExpr(E->getBuiltinLoc(), SubExpr.get(),
                                       TInfo, E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformParenListExpr(ParenListExpr *E) {
  bool ArgumentChanged = false;
  SmallVector<Expr*, 4> Inits;
  if (TransformExprs(E->getExprs(), E->getNumExprs(), true, Inits,
                     &ArgumentChanged))
    return ExprError();

  return getDerived().RebuildParenListExpr(E->getLParenLoc(),
                                           Inits,
                                           E->getRParenLoc());
}

/// Transform an address-of-label expression.
///
/// By default, the transformation of an address-of-label expression always
/// rebuilds the expression, so that the label identifier can be resolved to
/// the corresponding label statement by semantic analysis.
template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformAddrLabelExpr(AddrLabelExpr *E) {
  Decl *LD = getDerived().TransformDecl(E->getLabel()->getLocation(),
                                        E->getLabel());
  if (!LD)
    return ExprError();

  return getDerived().RebuildAddrLabelExpr(E->getAmpAmpLoc(), E->getLabelLoc(),
                                           cast<LabelDecl>(LD));
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformStmtExpr(StmtExpr *E) {
  SemaRef.ActOnStartStmtExpr();
  StmtResult SubStmt
    = getDerived().TransformCompoundStmt(E->getSubStmt(), true);
  if (SubStmt.isInvalid()) {
    SemaRef.ActOnStmtExprError();
    return ExprError();
  }

  unsigned OldDepth = E->getTemplateDepth();
  unsigned NewDepth = getDerived().TransformTemplateDepth(OldDepth);

  if (!getDerived().AlwaysRebuild() && OldDepth == NewDepth &&
      SubStmt.get() == E->getSubStmt()) {
    // Calling this an 'error' is unintuitive, but it does the right thing.
    SemaRef.ActOnStmtExprError();
    return SemaRef.MaybeBindToTemporary(E);
  }

  return getDerived().RebuildStmtExpr(E->getLParenLoc(), SubStmt.get(),
                                      E->getRParenLoc(), NewDepth);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformChooseExpr(ChooseExpr *E) {
  ExprResult Cond = getDerived().TransformExpr(E->getCond());
  if (Cond.isInvalid())
    return ExprError();

  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(E->getRHS());
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Cond.get() == E->getCond() &&
      LHS.get() == E->getLHS() &&
      RHS.get() == E->getRHS())
    return E;

  return getDerived().RebuildChooseExpr(E->getBuiltinLoc(),
                                        Cond.get(), LHS.get(), RHS.get(),
                                        E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformGNUNullExpr(GNUNullExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
  switch (E->getOperator()) {
  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
    llvm_unreachable("new and delete operators cannot use CXXOperatorCallExpr");

  case OO_Subscript:
  case OO_Call: {
    // This is a call to an object's operator().
    assert(E->getNumArgs() >= 1 && "Object call is missing arguments");

    // Transform the object itself.
    ExprResult Object = getDerived().TransformExpr(E->getArg(0));
    if (Object.isInvalid())
      return ExprError();

    // FIXME: Poor location information
    SourceLocation FakeLParenLoc = SemaRef.getLocForEndOfToken(
        static_cast<Expr *>(Object.get())->getEndLoc());

    // Transform the call arguments.
    SmallVector<Expr*, 8> Args;
    if (getDerived().TransformExprs(E->getArgs() + 1, E->getNumArgs() - 1, true,
                                    Args))
      return ExprError();

    if (E->getOperator() == OO_Subscript)
      return getDerived().RebuildCxxSubscriptExpr(Object.get(), FakeLParenLoc,
                                                  Args, E->getEndLoc());

    return getDerived().RebuildCallExpr(Object.get(), FakeLParenLoc, Args,
                                        E->getEndLoc());
  }

#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
  case OO_##Name:                                                              \
    break;

#define OVERLOADED_OPERATOR_MULTI(Name,Spelling,Unary,Binary,MemberOnly)
#include "clang/Basic/OperatorKinds.def"

  case OO_Conditional:
    llvm_unreachable("conditional operator is not actually overloadable");

  case OO_None:
  case NUM_OVERLOADED_OPERATORS:
    llvm_unreachable("not an overloaded operator?");
  }

  ExprResult First;
  if (E->getNumArgs() == 1 && E->getOperator() == OO_Amp)
    First = getDerived().TransformAddressOfOperand(E->getArg(0));
  else
    First = getDerived().TransformExpr(E->getArg(0));
  if (First.isInvalid())
    return ExprError();

  ExprResult Second;
  if (E->getNumArgs() == 2) {
    Second =
        getDerived().TransformInitializer(E->getArg(1), /*NotCopyInit=*/false);
    if (Second.isInvalid())
      return ExprError();
  }

  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  FPOptionsOverride NewOverrides(E->getFPFeatures());
  getSema().CurFPFeatures =
      NewOverrides.applyOverrides(getSema().getLangOpts());
  getSema().FpPragmaStack.CurrentValue = NewOverrides;

  Expr *Callee = E->getCallee();
  if (UnresolvedLookupExpr *ULE = dyn_cast<UnresolvedLookupExpr>(Callee)) {
    LookupResult R(SemaRef, ULE->getName(), ULE->getNameLoc(),
                   Sema::LookupOrdinaryName);
    if (getDerived().TransformOverloadExprDecls(ULE, ULE->requiresADL(), R))
      return ExprError();

    return getDerived().RebuildCXXOperatorCallExpr(
        E->getOperator(), E->getOperatorLoc(), Callee->getBeginLoc(),
        ULE->requiresADL(), R.asUnresolvedSet(), First.get(), Second.get());
  }

  UnresolvedSet<1> Functions;
  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Callee))
    Callee = ICE->getSubExprAsWritten();
  NamedDecl *DR = cast<DeclRefExpr>(Callee)->getDecl();
  ValueDecl *VD = cast_or_null<ValueDecl>(
      getDerived().TransformDecl(DR->getLocation(), DR));
  if (!VD)
    return ExprError();

  if (!isa<CXXMethodDecl>(VD))
    Functions.addDecl(VD);

  return getDerived().RebuildCXXOperatorCallExpr(
      E->getOperator(), E->getOperatorLoc(), Callee->getBeginLoc(),
      /*RequiresADL=*/false, Functions, First.get(), Second.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXMemberCallExpr(CXXMemberCallExpr *E) {
  return getDerived().TransformCallExpr(E);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformSourceLocExpr(SourceLocExpr *E) {
  bool NeedRebuildFunc = SourceLocExpr::MayBeDependent(E->getIdentKind()) &&
                         getSema().CurContext != E->getParentContext();

  if (!getDerived().AlwaysRebuild() && !NeedRebuildFunc)
    return E;

  return getDerived().RebuildSourceLocExpr(E->getIdentKind(), E->getType(),
                                           E->getBeginLoc(), E->getEndLoc(),
                                           getSema().CurContext);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformEmbedExpr(EmbedExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCUDAKernelCallExpr(CUDAKernelCallExpr *E) {
  // Transform the callee.
  ExprResult Callee = getDerived().TransformExpr(E->getCallee());
  if (Callee.isInvalid())
    return ExprError();

  // Transform exec config.
  ExprResult EC = getDerived().TransformCallExpr(E->getConfig());
  if (EC.isInvalid())
    return ExprError();

  // Transform arguments.
  bool ArgChanged = false;
  SmallVector<Expr*, 8> Args;
  if (getDerived().TransformExprs(E->getArgs(), E->getNumArgs(), true, Args,
                                  &ArgChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Callee.get() == E->getCallee() &&
      !ArgChanged)
    return SemaRef.MaybeBindToTemporary(E);

  // FIXME: Wrong source location information for the '('.
  SourceLocation FakeLParenLoc
    = ((Expr *)Callee.get())->getSourceRange().getBegin();
  return getDerived().RebuildCallExpr(Callee.get(), FakeLParenLoc,
                                      Args,
                                      E->getRParenLoc(), EC.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXNamedCastExpr(CXXNamedCastExpr *E) {
  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeInfoAsWritten());
  if (!Type)
    return ExprError();

  ExprResult SubExpr
    = getDerived().TransformExpr(E->getSubExprAsWritten());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Type == E->getTypeInfoAsWritten() &&
      SubExpr.get() == E->getSubExpr())
    return E;
  return getDerived().RebuildCXXNamedCastExpr(
      E->getOperatorLoc(), E->getStmtClass(), E->getAngleBrackets().getBegin(),
      Type, E->getAngleBrackets().getEnd(),
      // FIXME. this should be '(' location
      E->getAngleBrackets().getEnd(), SubExpr.get(), E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformBuiltinBitCastExpr(BuiltinBitCastExpr *BCE) {
  TypeSourceInfo *TSI =
      getDerived().TransformType(BCE->getTypeInfoAsWritten());
  if (!TSI)
    return ExprError();

  ExprResult Sub = getDerived().TransformExpr(BCE->getSubExpr());
  if (Sub.isInvalid())
    return ExprError();

  return getDerived().RebuildBuiltinBitCastExpr(BCE->getBeginLoc(), TSI,
                                                Sub.get(), BCE->getEndLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXStaticCastExpr(CXXStaticCastExpr *E) {
  return getDerived().TransformCXXNamedCastExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXDynamicCastExpr(CXXDynamicCastExpr *E) {
  return getDerived().TransformCXXNamedCastExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXReinterpretCastExpr(
                                                      CXXReinterpretCastExpr *E) {
  return getDerived().TransformCXXNamedCastExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXConstCastExpr(CXXConstCastExpr *E) {
  return getDerived().TransformCXXNamedCastExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXAddrspaceCastExpr(CXXAddrspaceCastExpr *E) {
  return getDerived().TransformCXXNamedCastExpr(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXFunctionalCastExpr(
                                                     CXXFunctionalCastExpr *E) {
  TypeSourceInfo *Type =
      getDerived().TransformTypeWithDeducedTST(E->getTypeInfoAsWritten());
  if (!Type)
    return ExprError();

  ExprResult SubExpr
    = getDerived().TransformExpr(E->getSubExprAsWritten());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Type == E->getTypeInfoAsWritten() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildCXXFunctionalCastExpr(Type,
                                                   E->getLParenLoc(),
                                                   SubExpr.get(),
                                                   E->getRParenLoc(),
                                                   E->isListInitialization());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXTypeidExpr(CXXTypeidExpr *E) {
  if (E->isTypeOperand()) {
    TypeSourceInfo *TInfo
      = getDerived().TransformType(E->getTypeOperandSourceInfo());
    if (!TInfo)
      return ExprError();

    if (!getDerived().AlwaysRebuild() &&
        TInfo == E->getTypeOperandSourceInfo())
      return E;

    return getDerived().RebuildCXXTypeidExpr(E->getType(), E->getBeginLoc(),
                                             TInfo, E->getEndLoc());
  }

  // Typeid's operand is an unevaluated context, unless it's a polymorphic
  // type.  We must not unilaterally enter unevaluated context here, as then
  // semantic processing can re-transform an already transformed operand.
  Expr *Op = E->getExprOperand();
  auto EvalCtx = Sema::ExpressionEvaluationContext::Unevaluated;
  if (E->isGLValue())
    if (auto *RecordT = Op->getType()->getAs<RecordType>())
      if (cast<CXXRecordDecl>(RecordT->getDecl())->isPolymorphic())
        EvalCtx = SemaRef.ExprEvalContexts.back().Context;

  EnterExpressionEvaluationContext Unevaluated(SemaRef, EvalCtx,
                                               Sema::ReuseLambdaContextDecl);

  ExprResult SubExpr = getDerived().TransformExpr(Op);
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      SubExpr.get() == E->getExprOperand())
    return E;

  return getDerived().RebuildCXXTypeidExpr(E->getType(), E->getBeginLoc(),
                                           SubExpr.get(), E->getEndLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXUuidofExpr(CXXUuidofExpr *E) {
  if (E->isTypeOperand()) {
    TypeSourceInfo *TInfo
      = getDerived().TransformType(E->getTypeOperandSourceInfo());
    if (!TInfo)
      return ExprError();

    if (!getDerived().AlwaysRebuild() &&
        TInfo == E->getTypeOperandSourceInfo())
      return E;

    return getDerived().RebuildCXXUuidofExpr(E->getType(), E->getBeginLoc(),
                                             TInfo, E->getEndLoc());
  }

  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);

  ExprResult SubExpr = getDerived().TransformExpr(E->getExprOperand());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      SubExpr.get() == E->getExprOperand())
    return E;

  return getDerived().RebuildCXXUuidofExpr(E->getType(), E->getBeginLoc(),
                                           SubExpr.get(), E->getEndLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXNullPtrLiteralExpr(
                                                     CXXNullPtrLiteralExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXThisExpr(CXXThisExpr *E) {

  // In lambdas, the qualifiers of the type depends of where in
  // the call operator `this` appear, and we do not have a good way to
  // rebuild this information, so we transform the type.
  //
  // In other contexts, the type of `this` may be overrided
  // for type deduction, so we need to recompute it.
  //
  // Always recompute the type if we're in the body of a lambda, and
  // 'this' is dependent on a lambda's explicit object parameter.
  QualType T = [&]() {
    auto &S = getSema();
    if (E->isCapturedByCopyInLambdaWithExplicitObjectParameter())
      return S.getCurrentThisType();
    if (S.getCurLambda())
      return getDerived().TransformType(E->getType());
    return S.getCurrentThisType();
  }();

  if (!getDerived().AlwaysRebuild() && T == E->getType()) {
    // Mark it referenced in the new context regardless.
    // FIXME: this is a bit instantiation-specific.
    getSema().MarkThisReferenced(E);
    return E;
  }

  return getDerived().RebuildCXXThisExpr(E->getBeginLoc(), T, E->isImplicit());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXThrowExpr(CXXThrowExpr *E) {
  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildCXXThrowExpr(E->getThrowLoc(), SubExpr.get(),
                                          E->isThrownVariableInScope());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
  ParmVarDecl *Param = cast_or_null<ParmVarDecl>(
      getDerived().TransformDecl(E->getBeginLoc(), E->getParam()));
  if (!Param)
    return ExprError();

  ExprResult InitRes;
  if (E->hasRewrittenInit()) {
    InitRes = getDerived().TransformExpr(E->getRewrittenExpr());
    if (InitRes.isInvalid())
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() && Param == E->getParam() &&
      E->getUsedContext() == SemaRef.CurContext &&
      InitRes.get() == E->getRewrittenExpr())
    return E;

  return getDerived().RebuildCXXDefaultArgExpr(E->getUsedLocation(), Param,
                                               InitRes.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
  FieldDecl *Field = cast_or_null<FieldDecl>(
      getDerived().TransformDecl(E->getBeginLoc(), E->getField()));
  if (!Field)
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Field == E->getField() &&
      E->getUsedContext() == SemaRef.CurContext)
    return E;

  return getDerived().RebuildCXXDefaultInitExpr(E->getExprLoc(), Field);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXScalarValueInitExpr(
                                                    CXXScalarValueInitExpr *E) {
  TypeSourceInfo *T = getDerived().TransformType(E->getTypeSourceInfo());
  if (!T)
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      T == E->getTypeSourceInfo())
    return E;

  return getDerived().RebuildCXXScalarValueInitExpr(T,
                                          /*FIXME:*/T->getTypeLoc().getEndLoc(),
                                                    E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXNewExpr(CXXNewExpr *E) {
  // Transform the type that we're allocating
  TypeSourceInfo *AllocTypeInfo =
      getDerived().TransformTypeWithDeducedTST(E->getAllocatedTypeSourceInfo());
  if (!AllocTypeInfo)
    return ExprError();

  // Transform the size of the array we're allocating (if any).
  std::optional<Expr *> ArraySize;
  if (E->isArray()) {
    ExprResult NewArraySize;
    if (std::optional<Expr *> OldArraySize = E->getArraySize()) {
      NewArraySize = getDerived().TransformExpr(*OldArraySize);
      if (NewArraySize.isInvalid())
        return ExprError();
    }
    ArraySize = NewArraySize.get();
  }

  // Transform the placement arguments (if any).
  bool ArgumentChanged = false;
  SmallVector<Expr*, 8> PlacementArgs;
  if (getDerived().TransformExprs(E->getPlacementArgs(),
                                  E->getNumPlacementArgs(), true,
                                  PlacementArgs, &ArgumentChanged))
    return ExprError();

  // Transform the initializer (if any).
  Expr *OldInit = E->getInitializer();
  ExprResult NewInit;
  if (OldInit)
    NewInit = getDerived().TransformInitializer(OldInit, true);
  if (NewInit.isInvalid())
    return ExprError();

  // Transform new operator and delete operator.
  FunctionDecl *OperatorNew = nullptr;
  if (E->getOperatorNew()) {
    OperatorNew = cast_or_null<FunctionDecl>(
        getDerived().TransformDecl(E->getBeginLoc(), E->getOperatorNew()));
    if (!OperatorNew)
      return ExprError();
  }

  FunctionDecl *OperatorDelete = nullptr;
  if (E->getOperatorDelete()) {
    OperatorDelete = cast_or_null<FunctionDecl>(
        getDerived().TransformDecl(E->getBeginLoc(), E->getOperatorDelete()));
    if (!OperatorDelete)
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      AllocTypeInfo == E->getAllocatedTypeSourceInfo() &&
      ArraySize == E->getArraySize() &&
      NewInit.get() == OldInit &&
      OperatorNew == E->getOperatorNew() &&
      OperatorDelete == E->getOperatorDelete() &&
      !ArgumentChanged) {
    // Mark any declarations we need as referenced.
    // FIXME: instantiation-specific.
    if (OperatorNew)
      SemaRef.MarkFunctionReferenced(E->getBeginLoc(), OperatorNew);
    if (OperatorDelete)
      SemaRef.MarkFunctionReferenced(E->getBeginLoc(), OperatorDelete);

    if (E->isArray() && !E->getAllocatedType()->isDependentType()) {
      QualType ElementType
        = SemaRef.Context.getBaseElementType(E->getAllocatedType());
      if (const RecordType *RecordT = ElementType->getAs<RecordType>()) {
        CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordT->getDecl());
        if (CXXDestructorDecl *Destructor = SemaRef.LookupDestructor(Record)) {
          SemaRef.MarkFunctionReferenced(E->getBeginLoc(), Destructor);
        }
      }
    }

    return E;
  }

  QualType AllocType = AllocTypeInfo->getType();
  if (!ArraySize) {
    // If no array size was specified, but the new expression was
    // instantiated with an array type (e.g., "new T" where T is
    // instantiated with "int[4]"), extract the outer bound from the
    // array type as our array size. We do this with constant and
    // dependently-sized array types.
    const ArrayType *ArrayT = SemaRef.Context.getAsArrayType(AllocType);
    if (!ArrayT) {
      // Do nothing
    } else if (const ConstantArrayType *ConsArrayT
                                     = dyn_cast<ConstantArrayType>(ArrayT)) {
      ArraySize = IntegerLiteral::Create(SemaRef.Context, ConsArrayT->getSize(),
                                         SemaRef.Context.getSizeType(),
                                         /*FIXME:*/ E->getBeginLoc());
      AllocType = ConsArrayT->getElementType();
    } else if (const DependentSizedArrayType *DepArrayT
                              = dyn_cast<DependentSizedArrayType>(ArrayT)) {
      if (DepArrayT->getSizeExpr()) {
        ArraySize = DepArrayT->getSizeExpr();
        AllocType = DepArrayT->getElementType();
      }
    }
  }

  return getDerived().RebuildCXXNewExpr(
      E->getBeginLoc(), E->isGlobalNew(),
      /*FIXME:*/ E->getBeginLoc(), PlacementArgs,
      /*FIXME:*/ E->getBeginLoc(), E->getTypeIdParens(), AllocType,
      AllocTypeInfo, ArraySize, E->getDirectInitRange(), NewInit.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXDeleteExpr(CXXDeleteExpr *E) {
  ExprResult Operand = getDerived().TransformExpr(E->getArgument());
  if (Operand.isInvalid())
    return ExprError();

  // Transform the delete operator, if known.
  FunctionDecl *OperatorDelete = nullptr;
  if (E->getOperatorDelete()) {
    OperatorDelete = cast_or_null<FunctionDecl>(
        getDerived().TransformDecl(E->getBeginLoc(), E->getOperatorDelete()));
    if (!OperatorDelete)
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      Operand.get() == E->getArgument() &&
      OperatorDelete == E->getOperatorDelete()) {
    // Mark any declarations we need as referenced.
    // FIXME: instantiation-specific.
    if (OperatorDelete)
      SemaRef.MarkFunctionReferenced(E->getBeginLoc(), OperatorDelete);

    if (!E->getArgument()->isTypeDependent()) {
      QualType Destroyed = SemaRef.Context.getBaseElementType(
                                                         E->getDestroyedType());
      if (const RecordType *DestroyedRec = Destroyed->getAs<RecordType>()) {
        CXXRecordDecl *Record = cast<CXXRecordDecl>(DestroyedRec->getDecl());
        SemaRef.MarkFunctionReferenced(E->getBeginLoc(),
                                       SemaRef.LookupDestructor(Record));
      }
    }

    return E;
  }

  return getDerived().RebuildCXXDeleteExpr(
      E->getBeginLoc(), E->isGlobalDelete(), E->isArrayForm(), Operand.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXPseudoDestructorExpr(
                                                     CXXPseudoDestructorExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  ParsedType ObjectTypePtr;
  bool MayBePseudoDestructor = false;
  Base = SemaRef.ActOnStartCXXMemberReference(nullptr, Base.get(),
                                              E->getOperatorLoc(),
                                        E->isArrow()? tok::arrow : tok::period,
                                              ObjectTypePtr,
                                              MayBePseudoDestructor);
  if (Base.isInvalid())
    return ExprError();

  QualType ObjectType = ObjectTypePtr.get();
  NestedNameSpecifierLoc QualifierLoc = E->getQualifierLoc();
  if (QualifierLoc) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(QualifierLoc, ObjectType);
    if (!QualifierLoc)
      return ExprError();
  }
  CXXScopeSpec SS;
  SS.Adopt(QualifierLoc);

  PseudoDestructorTypeStorage Destroyed;
  if (E->getDestroyedTypeInfo()) {
    TypeSourceInfo *DestroyedTypeInfo
      = getDerived().TransformTypeInObjectScope(E->getDestroyedTypeInfo(),
                                                ObjectType, nullptr, SS);
    if (!DestroyedTypeInfo)
      return ExprError();
    Destroyed = DestroyedTypeInfo;
  } else if (!ObjectType.isNull() && ObjectType->isDependentType()) {
    // We aren't likely to be able to resolve the identifier down to a type
    // now anyway, so just retain the identifier.
    Destroyed = PseudoDestructorTypeStorage(E->getDestroyedTypeIdentifier(),
                                            E->getDestroyedTypeLoc());
  } else {
    // Look for a destructor known with the given name.
    ParsedType T = SemaRef.getDestructorName(
        *E->getDestroyedTypeIdentifier(), E->getDestroyedTypeLoc(),
        /*Scope=*/nullptr, SS, ObjectTypePtr, false);
    if (!T)
      return ExprError();

    Destroyed
      = SemaRef.Context.getTrivialTypeSourceInfo(SemaRef.GetTypeFromParser(T),
                                                 E->getDestroyedTypeLoc());
  }

  TypeSourceInfo *ScopeTypeInfo = nullptr;
  if (E->getScopeTypeInfo()) {
    CXXScopeSpec EmptySS;
    ScopeTypeInfo = getDerived().TransformTypeInObjectScope(
                      E->getScopeTypeInfo(), ObjectType, nullptr, EmptySS);
    if (!ScopeTypeInfo)
      return ExprError();
  }

  return getDerived().RebuildCXXPseudoDestructorExpr(Base.get(),
                                                     E->getOperatorLoc(),
                                                     E->isArrow(),
                                                     SS,
                                                     ScopeTypeInfo,
                                                     E->getColonColonLoc(),
                                                     E->getTildeLoc(),
                                                     Destroyed);
}

template <typename Derived>
bool TreeTransform<Derived>::TransformOverloadExprDecls(OverloadExpr *Old,
                                                        bool RequiresADL,
                                                        LookupResult &R) {
  // Transform all the decls.
  bool AllEmptyPacks = true;
  for (auto *OldD : Old->decls()) {
    Decl *InstD = getDerived().TransformDecl(Old->getNameLoc(), OldD);
    if (!InstD) {
      // Silently ignore these if a UsingShadowDecl instantiated to nothing.
      // This can happen because of dependent hiding.
      if (isa<UsingShadowDecl>(OldD))
        continue;
      else {
        R.clear();
        return true;
      }
    }

    // Expand using pack declarations.
    NamedDecl *SingleDecl = cast<NamedDecl>(InstD);
    ArrayRef<NamedDecl*> Decls = SingleDecl;
    if (auto *UPD = dyn_cast<UsingPackDecl>(InstD))
      Decls = UPD->expansions();

    // Expand using declarations.
    for (auto *D : Decls) {
      if (auto *UD = dyn_cast<UsingDecl>(D)) {
        for (auto *SD : UD->shadows())
          R.addDecl(SD);
      } else {
        R.addDecl(D);
      }
    }

    AllEmptyPacks &= Decls.empty();
  }

  // C++ [temp.res]/8.4.2:
  //   The program is ill-formed, no diagnostic required, if [...] lookup for
  //   a name in the template definition found a using-declaration, but the
  //   lookup in the corresponding scope in the instantiation odoes not find
  //   any declarations because the using-declaration was a pack expansion and
  //   the corresponding pack is empty
  if (AllEmptyPacks && !RequiresADL) {
    getSema().Diag(Old->getNameLoc(), diag::err_using_pack_expansion_empty)
        << isa<UnresolvedMemberExpr>(Old) << Old->getName();
    return true;
  }

  // Resolve a kind, but don't do any further analysis.  If it's
  // ambiguous, the callee needs to deal with it.
  R.resolveKind();

  if (Old->hasTemplateKeyword() && !R.empty()) {
    NamedDecl *FoundDecl = R.getRepresentativeDecl()->getUnderlyingDecl();
    getSema().FilterAcceptableTemplateNames(R,
                                            /*AllowFunctionTemplates=*/true,
                                            /*AllowDependent=*/true);
    if (R.empty()) {
      // If a 'template' keyword was used, a lookup that finds only non-template
      // names is an error.
      getSema().Diag(R.getNameLoc(),
                     diag::err_template_kw_refers_to_non_template)
          << R.getLookupName() << Old->getQualifierLoc().getSourceRange()
          << Old->hasTemplateKeyword() << Old->getTemplateKeywordLoc();
      getSema().Diag(FoundDecl->getLocation(),
                     diag::note_template_kw_refers_to_non_template)
          << R.getLookupName();
      return true;
    }
  }

  return false;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformUnresolvedLookupExpr(
    UnresolvedLookupExpr *Old) {
  return TransformUnresolvedLookupExpr(Old, /*IsAddressOfOperand=*/false);
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformUnresolvedLookupExpr(UnresolvedLookupExpr *Old,
                                                      bool IsAddressOfOperand) {
  LookupResult R(SemaRef, Old->getName(), Old->getNameLoc(),
                 Sema::LookupOrdinaryName);

  // Transform the declaration set.
  if (TransformOverloadExprDecls(Old, Old->requiresADL(), R))
    return ExprError();

  // Rebuild the nested-name qualifier, if present.
  CXXScopeSpec SS;
  if (Old->getQualifierLoc()) {
    NestedNameSpecifierLoc QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(Old->getQualifierLoc());
    if (!QualifierLoc)
      return ExprError();

    SS.Adopt(QualifierLoc);
  }

  if (Old->getNamingClass()) {
    CXXRecordDecl *NamingClass
      = cast_or_null<CXXRecordDecl>(getDerived().TransformDecl(
                                                            Old->getNameLoc(),
                                                        Old->getNamingClass()));
    if (!NamingClass) {
      R.clear();
      return ExprError();
    }

    R.setNamingClass(NamingClass);
  }

  // Rebuild the template arguments, if any.
  SourceLocation TemplateKWLoc = Old->getTemplateKeywordLoc();
  TemplateArgumentListInfo TransArgs(Old->getLAngleLoc(), Old->getRAngleLoc());
  if (Old->hasExplicitTemplateArgs() &&
      getDerived().TransformTemplateArguments(Old->getTemplateArgs(),
                                              Old->getNumTemplateArgs(),
                                              TransArgs)) {
    R.clear();
    return ExprError();
  }

  // An UnresolvedLookupExpr can refer to a class member. This occurs e.g. when
  // a non-static data member is named in an unevaluated operand, or when
  // a member is named in a dependent class scope function template explicit
  // specialization that is neither declared static nor with an explicit object
  // parameter.
  if (SemaRef.isPotentialImplicitMemberAccess(SS, R, IsAddressOfOperand))
    return SemaRef.BuildPossibleImplicitMemberExpr(
        SS, TemplateKWLoc, R,
        Old->hasExplicitTemplateArgs() ? &TransArgs : nullptr,
        /*S=*/nullptr);

  // If we have neither explicit template arguments, nor the template keyword,
  // it's a normal declaration name or member reference.
  if (!Old->hasExplicitTemplateArgs() && !TemplateKWLoc.isValid())
    return getDerived().RebuildDeclarationNameExpr(SS, R, Old->requiresADL());

  // If we have template arguments, then rebuild the template-id expression.
  return getDerived().RebuildTemplateIdExpr(SS, TemplateKWLoc, R,
                                            Old->requiresADL(), &TransArgs);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformTypeTraitExpr(TypeTraitExpr *E) {
  bool ArgChanged = false;
  SmallVector<TypeSourceInfo *, 4> Args;
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I) {
    TypeSourceInfo *From = E->getArg(I);
    TypeLoc FromTL = From->getTypeLoc();
    if (!FromTL.getAs<PackExpansionTypeLoc>()) {
      TypeLocBuilder TLB;
      TLB.reserve(FromTL.getFullDataSize());
      QualType To = getDerived().TransformType(TLB, FromTL);
      if (To.isNull())
        return ExprError();

      if (To == From->getType())
        Args.push_back(From);
      else {
        Args.push_back(TLB.getTypeSourceInfo(SemaRef.Context, To));
        ArgChanged = true;
      }
      continue;
    }

    ArgChanged = true;

    // We have a pack expansion. Instantiate it.
    PackExpansionTypeLoc ExpansionTL = FromTL.castAs<PackExpansionTypeLoc>();
    TypeLoc PatternTL = ExpansionTL.getPatternLoc();
    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    SemaRef.collectUnexpandedParameterPacks(PatternTL, Unexpanded);

    // Determine whether the set of unexpanded parameter packs can and should
    // be expanded.
    bool Expand = true;
    bool RetainExpansion = false;
    std::optional<unsigned> OrigNumExpansions =
        ExpansionTL.getTypePtr()->getNumExpansions();
    std::optional<unsigned> NumExpansions = OrigNumExpansions;
    if (getDerived().TryExpandParameterPacks(ExpansionTL.getEllipsisLoc(),
                                             PatternTL.getSourceRange(),
                                             Unexpanded,
                                             Expand, RetainExpansion,
                                             NumExpansions))
      return ExprError();

    if (!Expand) {
      // The transform has determined that we should perform a simple
      // transformation on the pack expansion, producing another pack
      // expansion.
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);

      TypeLocBuilder TLB;
      TLB.reserve(From->getTypeLoc().getFullDataSize());

      QualType To = getDerived().TransformType(TLB, PatternTL);
      if (To.isNull())
        return ExprError();

      To = getDerived().RebuildPackExpansionType(To,
                                                 PatternTL.getSourceRange(),
                                                 ExpansionTL.getEllipsisLoc(),
                                                 NumExpansions);
      if (To.isNull())
        return ExprError();

      PackExpansionTypeLoc ToExpansionTL
        = TLB.push<PackExpansionTypeLoc>(To);
      ToExpansionTL.setEllipsisLoc(ExpansionTL.getEllipsisLoc());
      Args.push_back(TLB.getTypeSourceInfo(SemaRef.Context, To));
      continue;
    }

    // Expand the pack expansion by substituting for each argument in the
    // pack(s).
    for (unsigned I = 0; I != *NumExpansions; ++I) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(SemaRef, I);
      TypeLocBuilder TLB;
      TLB.reserve(PatternTL.getFullDataSize());
      QualType To = getDerived().TransformType(TLB, PatternTL);
      if (To.isNull())
        return ExprError();

      if (To->containsUnexpandedParameterPack()) {
        To = getDerived().RebuildPackExpansionType(To,
                                                   PatternTL.getSourceRange(),
                                                   ExpansionTL.getEllipsisLoc(),
                                                   NumExpansions);
        if (To.isNull())
          return ExprError();

        PackExpansionTypeLoc ToExpansionTL
          = TLB.push<PackExpansionTypeLoc>(To);
        ToExpansionTL.setEllipsisLoc(ExpansionTL.getEllipsisLoc());
      }

      Args.push_back(TLB.getTypeSourceInfo(SemaRef.Context, To));
    }

    if (!RetainExpansion)
      continue;

    // If we're supposed to retain a pack expansion, do so by temporarily
    // forgetting the partially-substituted parameter pack.
    ForgetPartiallySubstitutedPackRAII Forget(getDerived());

    TypeLocBuilder TLB;
    TLB.reserve(From->getTypeLoc().getFullDataSize());

    QualType To = getDerived().TransformType(TLB, PatternTL);
    if (To.isNull())
      return ExprError();

    To = getDerived().RebuildPackExpansionType(To,
                                               PatternTL.getSourceRange(),
                                               ExpansionTL.getEllipsisLoc(),
                                               NumExpansions);
    if (To.isNull())
      return ExprError();

    PackExpansionTypeLoc ToExpansionTL
      = TLB.push<PackExpansionTypeLoc>(To);
    ToExpansionTL.setEllipsisLoc(ExpansionTL.getEllipsisLoc());
    Args.push_back(TLB.getTypeSourceInfo(SemaRef.Context, To));
  }

  if (!getDerived().AlwaysRebuild() && !ArgChanged)
    return E;

  return getDerived().RebuildTypeTrait(E->getTrait(), E->getBeginLoc(), Args,
                                       E->getEndLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformConceptSpecializationExpr(
                                                 ConceptSpecializationExpr *E) {
  const ASTTemplateArgumentListInfo *Old = E->getTemplateArgsAsWritten();
  TemplateArgumentListInfo TransArgs(Old->LAngleLoc, Old->RAngleLoc);
  if (getDerived().TransformTemplateArguments(Old->getTemplateArgs(),
                                              Old->NumTemplateArgs, TransArgs))
    return ExprError();

  return getDerived().RebuildConceptSpecializationExpr(
      E->getNestedNameSpecifierLoc(), E->getTemplateKWLoc(),
      E->getConceptNameInfo(), E->getFoundDecl(), E->getNamedConcept(),
      &TransArgs);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformRequiresExpr(RequiresExpr *E) {
  SmallVector<ParmVarDecl*, 4> TransParams;
  SmallVector<QualType, 4> TransParamTypes;
  Sema::ExtParameterInfoBuilder ExtParamInfos;

  // C++2a [expr.prim.req]p2
  // Expressions appearing within a requirement-body are unevaluated operands.
  EnterExpressionEvaluationContext Ctx(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated,
      Sema::ReuseLambdaContextDecl);

  RequiresExprBodyDecl *Body = RequiresExprBodyDecl::Create(
      getSema().Context, getSema().CurContext,
      E->getBody()->getBeginLoc());

  Sema::ContextRAII SavedContext(getSema(), Body, /*NewThisContext*/false);

  ExprResult TypeParamResult = getDerived().TransformRequiresTypeParams(
      E->getRequiresKWLoc(), E->getRBraceLoc(), E, Body,
      E->getLocalParameters(), TransParamTypes, TransParams, ExtParamInfos);

  for (ParmVarDecl *Param : TransParams)
    if (Param)
      Param->setDeclContext(Body);

  // On failure to transform, TransformRequiresTypeParams returns an expression
  // in the event that the transformation of the type params failed in some way.
  // It is expected that this will result in a 'not satisfied' Requires clause
  // when instantiating.
  if (!TypeParamResult.isUnset())
    return TypeParamResult;

  SmallVector<concepts::Requirement *, 4> TransReqs;
  if (getDerived().TransformRequiresExprRequirements(E->getRequirements(),
                                                     TransReqs))
    return ExprError();

  for (concepts::Requirement *Req : TransReqs) {
    if (auto *ER = dyn_cast<concepts::ExprRequirement>(Req)) {
      if (ER->getReturnTypeRequirement().isTypeConstraint()) {
        ER->getReturnTypeRequirement()
                .getTypeConstraintTemplateParameterList()->getParam(0)
                ->setDeclContext(Body);
      }
    }
  }

  return getDerived().RebuildRequiresExpr(
      E->getRequiresKWLoc(), Body, E->getLParenLoc(), TransParams,
      E->getRParenLoc(), TransReqs, E->getRBraceLoc());
}

template<typename Derived>
bool TreeTransform<Derived>::TransformRequiresExprRequirements(
    ArrayRef<concepts::Requirement *> Reqs,
    SmallVectorImpl<concepts::Requirement *> &Transformed) {
  for (concepts::Requirement *Req : Reqs) {
    concepts::Requirement *TransReq = nullptr;
    if (auto *TypeReq = dyn_cast<concepts::TypeRequirement>(Req))
      TransReq = getDerived().TransformTypeRequirement(TypeReq);
    else if (auto *ExprReq = dyn_cast<concepts::ExprRequirement>(Req))
      TransReq = getDerived().TransformExprRequirement(ExprReq);
    else
      TransReq = getDerived().TransformNestedRequirement(
                     cast<concepts::NestedRequirement>(Req));
    if (!TransReq)
      return true;
    Transformed.push_back(TransReq);
  }
  return false;
}

template<typename Derived>
concepts::TypeRequirement *
TreeTransform<Derived>::TransformTypeRequirement(
    concepts::TypeRequirement *Req) {
  if (Req->isSubstitutionFailure()) {
    if (getDerived().AlwaysRebuild())
      return getDerived().RebuildTypeRequirement(
              Req->getSubstitutionDiagnostic());
    return Req;
  }
  TypeSourceInfo *TransType = getDerived().TransformType(Req->getType());
  if (!TransType)
    return nullptr;
  return getDerived().RebuildTypeRequirement(TransType);
}

template<typename Derived>
concepts::ExprRequirement *
TreeTransform<Derived>::TransformExprRequirement(concepts::ExprRequirement *Req) {
  llvm::PointerUnion<Expr *, concepts::Requirement::SubstitutionDiagnostic *> TransExpr;
  if (Req->isExprSubstitutionFailure())
    TransExpr = Req->getExprSubstitutionDiagnostic();
  else {
    ExprResult TransExprRes = getDerived().TransformExpr(Req->getExpr());
    if (TransExprRes.isUsable() && TransExprRes.get()->hasPlaceholderType())
      TransExprRes = SemaRef.CheckPlaceholderExpr(TransExprRes.get());
    if (TransExprRes.isInvalid())
      return nullptr;
    TransExpr = TransExprRes.get();
  }

  std::optional<concepts::ExprRequirement::ReturnTypeRequirement> TransRetReq;
  const auto &RetReq = Req->getReturnTypeRequirement();
  if (RetReq.isEmpty())
    TransRetReq.emplace();
  else if (RetReq.isSubstitutionFailure())
    TransRetReq.emplace(RetReq.getSubstitutionDiagnostic());
  else if (RetReq.isTypeConstraint()) {
    TemplateParameterList *OrigTPL =
        RetReq.getTypeConstraintTemplateParameterList();
    TemplateParameterList *TPL =
        getDerived().TransformTemplateParameterList(OrigTPL);
    if (!TPL)
      return nullptr;
    TransRetReq.emplace(TPL);
  }
  assert(TransRetReq && "All code paths leading here must set TransRetReq");
  if (Expr *E = TransExpr.dyn_cast<Expr *>())
    return getDerived().RebuildExprRequirement(E, Req->isSimple(),
                                               Req->getNoexceptLoc(),
                                               std::move(*TransRetReq));
  return getDerived().RebuildExprRequirement(
      TransExpr.get<concepts::Requirement::SubstitutionDiagnostic *>(),
      Req->isSimple(), Req->getNoexceptLoc(), std::move(*TransRetReq));
}

template<typename Derived>
concepts::NestedRequirement *
TreeTransform<Derived>::TransformNestedRequirement(
    concepts::NestedRequirement *Req) {
  if (Req->hasInvalidConstraint()) {
    if (getDerived().AlwaysRebuild())
      return getDerived().RebuildNestedRequirement(
          Req->getInvalidConstraintEntity(), Req->getConstraintSatisfaction());
    return Req;
  }
  ExprResult TransConstraint =
      getDerived().TransformExpr(Req->getConstraintExpr());
  if (TransConstraint.isInvalid())
    return nullptr;
  return getDerived().RebuildNestedRequirement(TransConstraint.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {
  TypeSourceInfo *T = getDerived().TransformType(E->getQueriedTypeSourceInfo());
  if (!T)
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      T == E->getQueriedTypeSourceInfo())
    return E;

  ExprResult SubExpr;
  {
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);
    SubExpr = getDerived().TransformExpr(E->getDimensionExpression());
    if (SubExpr.isInvalid())
      return ExprError();

    if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getDimensionExpression())
      return E;
  }

  return getDerived().RebuildArrayTypeTrait(E->getTrait(), E->getBeginLoc(), T,
                                            SubExpr.get(), E->getEndLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformExpressionTraitExpr(ExpressionTraitExpr *E) {
  ExprResult SubExpr;
  {
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);
    SubExpr = getDerived().TransformExpr(E->getQueriedExpression());
    if (SubExpr.isInvalid())
      return ExprError();

    if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getQueriedExpression())
      return E;
  }

  return getDerived().RebuildExpressionTrait(E->getTrait(), E->getBeginLoc(),
                                             SubExpr.get(), E->getEndLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformParenDependentScopeDeclRefExpr(
    ParenExpr *PE, DependentScopeDeclRefExpr *DRE, bool AddrTaken,
    TypeSourceInfo **RecoveryTSI) {
  ExprResult NewDRE = getDerived().TransformDependentScopeDeclRefExpr(
      DRE, AddrTaken, RecoveryTSI);

  // Propagate both errors and recovered types, which return ExprEmpty.
  if (!NewDRE.isUsable())
    return NewDRE;

  // We got an expr, wrap it up in parens.
  if (!getDerived().AlwaysRebuild() && NewDRE.get() == DRE)
    return PE;
  return getDerived().RebuildParenExpr(NewDRE.get(), PE->getLParen(),
                                       PE->getRParen());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformDependentScopeDeclRefExpr(
    DependentScopeDeclRefExpr *E) {
  return TransformDependentScopeDeclRefExpr(E, /*IsAddressOfOperand=*/false,
                                            nullptr);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformDependentScopeDeclRefExpr(
    DependentScopeDeclRefExpr *E, bool IsAddressOfOperand,
    TypeSourceInfo **RecoveryTSI) {
  assert(E->getQualifierLoc());
  NestedNameSpecifierLoc QualifierLoc =
      getDerived().TransformNestedNameSpecifierLoc(E->getQualifierLoc());
  if (!QualifierLoc)
    return ExprError();
  SourceLocation TemplateKWLoc = E->getTemplateKeywordLoc();

  // TODO: If this is a conversion-function-id, verify that the
  // destination type name (if present) resolves the same way after
  // instantiation as it did in the local scope.

  DeclarationNameInfo NameInfo =
      getDerived().TransformDeclarationNameInfo(E->getNameInfo());
  if (!NameInfo.getName())
    return ExprError();

  if (!E->hasExplicitTemplateArgs()) {
    if (!getDerived().AlwaysRebuild() && QualifierLoc == E->getQualifierLoc() &&
        // Note: it is sufficient to compare the Name component of NameInfo:
        // if name has not changed, DNLoc has not changed either.
        NameInfo.getName() == E->getDeclName())
      return E;

    return getDerived().RebuildDependentScopeDeclRefExpr(
        QualifierLoc, TemplateKWLoc, NameInfo, /*TemplateArgs=*/nullptr,
        IsAddressOfOperand, RecoveryTSI);
  }

  TemplateArgumentListInfo TransArgs(E->getLAngleLoc(), E->getRAngleLoc());
  if (getDerived().TransformTemplateArguments(
          E->getTemplateArgs(), E->getNumTemplateArgs(), TransArgs))
    return ExprError();

  return getDerived().RebuildDependentScopeDeclRefExpr(
      QualifierLoc, TemplateKWLoc, NameInfo, &TransArgs, IsAddressOfOperand,
      RecoveryTSI);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXConstructExpr(CXXConstructExpr *E) {
  // CXXConstructExprs other than for list-initialization and
  // CXXTemporaryObjectExpr are always implicit, so when we have
  // a 1-argument construction we just transform that argument.
  if (getDerived().AllowSkippingCXXConstructExpr() &&
      ((E->getNumArgs() == 1 ||
        (E->getNumArgs() > 1 && getDerived().DropCallArgument(E->getArg(1)))) &&
       (!getDerived().DropCallArgument(E->getArg(0))) &&
       !E->isListInitialization()))
    return getDerived().TransformInitializer(E->getArg(0),
                                             /*DirectInit*/ false);

  TemporaryBase Rebase(*this, /*FIXME*/ E->getBeginLoc(), DeclarationName());

  QualType T = getDerived().TransformType(E->getType());
  if (T.isNull())
    return ExprError();

  CXXConstructorDecl *Constructor = cast_or_null<CXXConstructorDecl>(
      getDerived().TransformDecl(E->getBeginLoc(), E->getConstructor()));
  if (!Constructor)
    return ExprError();

  bool ArgumentChanged = false;
  SmallVector<Expr*, 8> Args;
  {
    EnterExpressionEvaluationContext Context(
        getSema(), EnterExpressionEvaluationContext::InitList,
        E->isListInitialization());
    if (getDerived().TransformExprs(E->getArgs(), E->getNumArgs(), true, Args,
                                    &ArgumentChanged))
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      T == E->getType() &&
      Constructor == E->getConstructor() &&
      !ArgumentChanged) {
    // Mark the constructor as referenced.
    // FIXME: Instantiation-specific
    SemaRef.MarkFunctionReferenced(E->getBeginLoc(), Constructor);
    return E;
  }

  return getDerived().RebuildCXXConstructExpr(
      T, /*FIXME:*/ E->getBeginLoc(), Constructor, E->isElidable(), Args,
      E->hadMultipleCandidates(), E->isListInitialization(),
      E->isStdInitListInitialization(), E->requiresZeroInitialization(),
      E->getConstructionKind(), E->getParenOrBraceRange());
}

template<typename Derived>
ExprResult TreeTransform<Derived>::TransformCXXInheritedCtorInitExpr(
    CXXInheritedCtorInitExpr *E) {
  QualType T = getDerived().TransformType(E->getType());
  if (T.isNull())
    return ExprError();

  CXXConstructorDecl *Constructor = cast_or_null<CXXConstructorDecl>(
      getDerived().TransformDecl(E->getBeginLoc(), E->getConstructor()));
  if (!Constructor)
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      T == E->getType() &&
      Constructor == E->getConstructor()) {
    // Mark the constructor as referenced.
    // FIXME: Instantiation-specific
    SemaRef.MarkFunctionReferenced(E->getBeginLoc(), Constructor);
    return E;
  }

  return getDerived().RebuildCXXInheritedCtorInitExpr(
      T, E->getLocation(), Constructor,
      E->constructsVBase(), E->inheritedFromVBase());
}

/// Transform a C++ temporary-binding expression.
///
/// Since CXXBindTemporaryExpr nodes are implicitly generated, we just
/// transform the subexpression and return that.
template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  if (auto *Dtor = E->getTemporary()->getDestructor())
    SemaRef.MarkFunctionReferenced(E->getBeginLoc(),
                                   const_cast<CXXDestructorDecl *>(Dtor));
  return getDerived().TransformExpr(E->getSubExpr());
}

/// Transform a C++ expression that contains cleanups that should
/// be run after the expression is evaluated.
///
/// Since ExprWithCleanups nodes are implicitly generated, we
/// just transform the subexpression and return that.
template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformExprWithCleanups(ExprWithCleanups *E) {
  return getDerived().TransformExpr(E->getSubExpr());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXTemporaryObjectExpr(
                                                    CXXTemporaryObjectExpr *E) {
  TypeSourceInfo *T =
      getDerived().TransformTypeWithDeducedTST(E->getTypeSourceInfo());
  if (!T)
    return ExprError();

  CXXConstructorDecl *Constructor = cast_or_null<CXXConstructorDecl>(
      getDerived().TransformDecl(E->getBeginLoc(), E->getConstructor()));
  if (!Constructor)
    return ExprError();

  bool ArgumentChanged = false;
  SmallVector<Expr*, 8> Args;
  Args.reserve(E->getNumArgs());
  {
    EnterExpressionEvaluationContext Context(
        getSema(), EnterExpressionEvaluationContext::InitList,
        E->isListInitialization());
    if (TransformExprs(E->getArgs(), E->getNumArgs(), true, Args,
                       &ArgumentChanged))
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      T == E->getTypeSourceInfo() &&
      Constructor == E->getConstructor() &&
      !ArgumentChanged) {
    // FIXME: Instantiation-specific
    SemaRef.MarkFunctionReferenced(E->getBeginLoc(), Constructor);
    return SemaRef.MaybeBindToTemporary(E);
  }

  // FIXME: We should just pass E->isListInitialization(), but we're not
  // prepared to handle list-initialization without a child InitListExpr.
  SourceLocation LParenLoc = T->getTypeLoc().getEndLoc();
  return getDerived().RebuildCXXTemporaryObjectExpr(
      T, LParenLoc, Args, E->getEndLoc(),
      /*ListInitialization=*/LParenLoc.isInvalid());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformLambdaExpr(LambdaExpr *E) {
  // Transform any init-capture expressions before entering the scope of the
  // lambda body, because they are not semantically within that scope.
  typedef std::pair<ExprResult, QualType> InitCaptureInfoTy;
  struct TransformedInitCapture {
    // The location of the ... if the result is retaining a pack expansion.
    SourceLocation EllipsisLoc;
    // Zero or more expansions of the init-capture.
    SmallVector<InitCaptureInfoTy, 4> Expansions;
  };
  SmallVector<TransformedInitCapture, 4> InitCaptures;
  InitCaptures.resize(E->explicit_capture_end() - E->explicit_capture_begin());
  for (LambdaExpr::capture_iterator C = E->capture_begin(),
                                    CEnd = E->capture_end();
       C != CEnd; ++C) {
    if (!E->isInitCapture(C))
      continue;

    TransformedInitCapture &Result = InitCaptures[C - E->capture_begin()];
    auto *OldVD = cast<VarDecl>(C->getCapturedVar());

    auto SubstInitCapture = [&](SourceLocation EllipsisLoc,
                                std::optional<unsigned> NumExpansions) {
      ExprResult NewExprInitResult = getDerived().TransformInitializer(
          OldVD->getInit(), OldVD->getInitStyle() == VarDecl::CallInit);

      if (NewExprInitResult.isInvalid()) {
        Result.Expansions.push_back(InitCaptureInfoTy(ExprError(), QualType()));
        return;
      }
      Expr *NewExprInit = NewExprInitResult.get();

      QualType NewInitCaptureType =
          getSema().buildLambdaInitCaptureInitialization(
              C->getLocation(), C->getCaptureKind() == LCK_ByRef,
              EllipsisLoc, NumExpansions, OldVD->getIdentifier(),
              cast<VarDecl>(C->getCapturedVar())->getInitStyle() !=
                  VarDecl::CInit,
              NewExprInit);
      Result.Expansions.push_back(
          InitCaptureInfoTy(NewExprInit, NewInitCaptureType));
    };

    // If this is an init-capture pack, consider expanding the pack now.
    if (OldVD->isParameterPack()) {
      PackExpansionTypeLoc ExpansionTL = OldVD->getTypeSourceInfo()
                                             ->getTypeLoc()
                                             .castAs<PackExpansionTypeLoc>();
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      SemaRef.collectUnexpandedParameterPacks(OldVD->getInit(), Unexpanded);

      // Determine whether the set of unexpanded parameter packs can and should
      // be expanded.
      bool Expand = true;
      bool RetainExpansion = false;
      std::optional<unsigned> OrigNumExpansions =
          ExpansionTL.getTypePtr()->getNumExpansions();
      std::optional<unsigned> NumExpansions = OrigNumExpansions;
      if (getDerived().TryExpandParameterPacks(
              ExpansionTL.getEllipsisLoc(),
              OldVD->getInit()->getSourceRange(), Unexpanded, Expand,
              RetainExpansion, NumExpansions))
        return ExprError();
      if (Expand) {
        for (unsigned I = 0; I != *NumExpansions; ++I) {
          Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
          SubstInitCapture(SourceLocation(), std::nullopt);
        }
      }
      if (!Expand || RetainExpansion) {
        ForgetPartiallySubstitutedPackRAII Forget(getDerived());
        SubstInitCapture(ExpansionTL.getEllipsisLoc(), NumExpansions);
        Result.EllipsisLoc = ExpansionTL.getEllipsisLoc();
      }
    } else {
      SubstInitCapture(SourceLocation(), std::nullopt);
    }
  }

  LambdaScopeInfo *LSI = getSema().PushLambdaScope();
  Sema::FunctionScopeRAII FuncScopeCleanup(getSema());

  // Create the local class that will describe the lambda.

  // FIXME: DependencyKind below is wrong when substituting inside a templated
  // context that isn't a DeclContext (such as a variable template), or when
  // substituting an unevaluated lambda inside of a function's parameter's type
  // - as parameter types are not instantiated from within a function's DC. We
  // use evaluation contexts to distinguish the function parameter case.
  CXXRecordDecl::LambdaDependencyKind DependencyKind =
      CXXRecordDecl::LDK_Unknown;
  DeclContext *DC = getSema().CurContext;
  // A RequiresExprBodyDecl is not interesting for dependencies.
  // For the following case,
  //
  // template <typename>
  // concept C = requires { [] {}; };
  //
  // template <class F>
  // struct Widget;
  //
  // template <C F>
  // struct Widget<F> {};
  //
  // While we are substituting Widget<F>, the parent of DC would be
  // the template specialization itself. Thus, the lambda expression
  // will be deemed as dependent even if there are no dependent template
  // arguments.
  // (A ClassTemplateSpecializationDecl is always a dependent context.)
  while (DC->isRequiresExprBody())
    DC = DC->getParent();
  if ((getSema().isUnevaluatedContext() ||
       getSema().isConstantEvaluatedContext()) &&
      (DC->isFileContext() || !DC->getParent()->isDependentContext()))
    DependencyKind = CXXRecordDecl::LDK_NeverDependent;

  CXXRecordDecl *OldClass = E->getLambdaClass();
  CXXRecordDecl *Class = getSema().createLambdaClosureType(
      E->getIntroducerRange(), /*Info=*/nullptr, DependencyKind,
      E->getCaptureDefault());
  getDerived().transformedLocalDecl(OldClass, {Class});

  CXXMethodDecl *NewCallOperator =
      getSema().CreateLambdaCallOperator(E->getIntroducerRange(), Class);
  NewCallOperator->setLexicalDeclContext(getSema().CurContext);

  // Enter the scope of the lambda.
  getSema().buildLambdaScope(LSI, NewCallOperator, E->getIntroducerRange(),
                             E->getCaptureDefault(), E->getCaptureDefaultLoc(),
                             E->hasExplicitParameters(), E->isMutable());

  // Introduce the context of the call operator.
  Sema::ContextRAII SavedContext(getSema(), NewCallOperator,
                                 /*NewThisContext*/false);

  bool Invalid = false;

  // Transform captures.
  for (LambdaExpr::capture_iterator C = E->capture_begin(),
                                 CEnd = E->capture_end();
       C != CEnd; ++C) {
    // When we hit the first implicit capture, tell Sema that we've finished
    // the list of explicit captures.
    if (C->isImplicit())
      break;

    // Capturing 'this' is trivial.
    if (C->capturesThis()) {
      // If this is a lambda that is part of a default member initialiser
      // and which we're instantiating outside the class that 'this' is
      // supposed to refer to, adjust the type of 'this' accordingly.
      //
      // Otherwise, leave the type of 'this' as-is.
      Sema::CXXThisScopeRAII ThisScope(
          getSema(),
          dyn_cast_if_present<CXXRecordDecl>(
              getSema().getFunctionLevelDeclContext()),
          Qualifiers());
      getSema().CheckCXXThisCapture(C->getLocation(), C->isExplicit(),
                                    /*BuildAndDiagnose*/ true, nullptr,
                                    C->getCaptureKind() == LCK_StarThis);
      continue;
    }
    // Captured expression will be recaptured during captured variables
    // rebuilding.
    if (C->capturesVLAType())
      continue;

    // Rebuild init-captures, including the implied field declaration.
    if (E->isInitCapture(C)) {
      TransformedInitCapture &NewC = InitCaptures[C - E->capture_begin()];

      auto *OldVD = cast<VarDecl>(C->getCapturedVar());
      llvm::SmallVector<Decl*, 4> NewVDs;

      for (InitCaptureInfoTy &Info : NewC.Expansions) {
        ExprResult Init = Info.first;
        QualType InitQualType = Info.second;
        if (Init.isInvalid() || InitQualType.isNull()) {
          Invalid = true;
          break;
        }
        VarDecl *NewVD = getSema().createLambdaInitCaptureVarDecl(
            OldVD->getLocation(), InitQualType, NewC.EllipsisLoc,
            OldVD->getIdentifier(), OldVD->getInitStyle(), Init.get(),
            getSema().CurContext);
        if (!NewVD) {
          Invalid = true;
          break;
        }
        NewVDs.push_back(NewVD);
        getSema().addInitCapture(LSI, NewVD, C->getCaptureKind() == LCK_ByRef);
      }

      if (Invalid)
        break;

      getDerived().transformedLocalDecl(OldVD, NewVDs);
      continue;
    }

    assert(C->capturesVariable() && "unexpected kind of lambda capture");

    // Determine the capture kind for Sema.
    Sema::TryCaptureKind Kind
      = C->isImplicit()? Sema::TryCapture_Implicit
                       : C->getCaptureKind() == LCK_ByCopy
                           ? Sema::TryCapture_ExplicitByVal
                           : Sema::TryCapture_ExplicitByRef;
    SourceLocation EllipsisLoc;
    if (C->isPackExpansion()) {
      UnexpandedParameterPack Unexpanded(C->getCapturedVar(), C->getLocation());
      bool ShouldExpand = false;
      bool RetainExpansion = false;
      std::optional<unsigned> NumExpansions;
      if (getDerived().TryExpandParameterPacks(C->getEllipsisLoc(),
                                               C->getLocation(),
                                               Unexpanded,
                                               ShouldExpand, RetainExpansion,
                                               NumExpansions)) {
        Invalid = true;
        continue;
      }

      if (ShouldExpand) {
        // The transform has determined that we should perform an expansion;
        // transform and capture each of the arguments.
        // expansion of the pattern. Do so.
        auto *Pack = cast<VarDecl>(C->getCapturedVar());
        for (unsigned I = 0; I != *NumExpansions; ++I) {
          Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
          VarDecl *CapturedVar
            = cast_or_null<VarDecl>(getDerived().TransformDecl(C->getLocation(),
                                                               Pack));
          if (!CapturedVar) {
            Invalid = true;
            continue;
          }

          // Capture the transformed variable.
          getSema().tryCaptureVariable(CapturedVar, C->getLocation(), Kind);
        }

        // FIXME: Retain a pack expansion if RetainExpansion is true.

        continue;
      }

      EllipsisLoc = C->getEllipsisLoc();
    }

    // Transform the captured variable.
    auto *CapturedVar = cast_or_null<ValueDecl>(
        getDerived().TransformDecl(C->getLocation(), C->getCapturedVar()));
    if (!CapturedVar || CapturedVar->isInvalidDecl()) {
      Invalid = true;
      continue;
    }

    // Capture the transformed variable.
    getSema().tryCaptureVariable(CapturedVar, C->getLocation(), Kind,
                                 EllipsisLoc);
  }
  getSema().finishLambdaExplicitCaptures(LSI);

  // Transform the template parameters, and add them to the current
  // instantiation scope. The null case is handled correctly.
  auto TPL = getDerived().TransformTemplateParameterList(
      E->getTemplateParameterList());
  LSI->GLTemplateParameterList = TPL;
  if (TPL)
    getSema().AddTemplateParametersToLambdaCallOperator(NewCallOperator, Class,
                                                        TPL);

  // Transform the type of the original lambda's call operator.
  // The transformation MUST be done in the CurrentInstantiationScope since
  // it introduces a mapping of the original to the newly created
  // transformed parameters.
  TypeSourceInfo *NewCallOpTSI = nullptr;
  {
    auto OldCallOpTypeLoc =
        E->getCallOperator()->getTypeSourceInfo()->getTypeLoc();

    auto TransformFunctionProtoTypeLoc =
        [this](TypeLocBuilder &TLB, FunctionProtoTypeLoc FPTL) -> QualType {
      SmallVector<QualType, 4> ExceptionStorage;
      return this->TransformFunctionProtoType(
          TLB, FPTL, nullptr, Qualifiers(),
          [&](FunctionProtoType::ExceptionSpecInfo &ESI, bool &Changed) {
            return TransformExceptionSpec(FPTL.getBeginLoc(), ESI,
                                          ExceptionStorage, Changed);
          });
    };

    QualType NewCallOpType;
    TypeLocBuilder NewCallOpTLBuilder;

    if (auto ATL = OldCallOpTypeLoc.getAs<AttributedTypeLoc>()) {
      NewCallOpType = this->TransformAttributedType(
          NewCallOpTLBuilder, ATL,
          [&](TypeLocBuilder &TLB, TypeLoc TL) -> QualType {
            return TransformFunctionProtoTypeLoc(
                TLB, TL.castAs<FunctionProtoTypeLoc>());
          });
    } else {
      auto FPTL = OldCallOpTypeLoc.castAs<FunctionProtoTypeLoc>();
      NewCallOpType = TransformFunctionProtoTypeLoc(NewCallOpTLBuilder, FPTL);
    }

    if (NewCallOpType.isNull())
      return ExprError();
    NewCallOpTSI =
        NewCallOpTLBuilder.getTypeSourceInfo(getSema().Context, NewCallOpType);
  }

  ArrayRef<ParmVarDecl *> Params;
  if (auto ATL = NewCallOpTSI->getTypeLoc().getAs<AttributedTypeLoc>()) {
    Params = ATL.getModifiedLoc().castAs<FunctionProtoTypeLoc>().getParams();
  } else {
    auto FPTL = NewCallOpTSI->getTypeLoc().castAs<FunctionProtoTypeLoc>();
    Params = FPTL.getParams();
  }

  getSema().CompleteLambdaCallOperator(
      NewCallOperator, E->getCallOperator()->getLocation(),
      E->getCallOperator()->getInnerLocStart(),
      E->getCallOperator()->getTrailingRequiresClause(), NewCallOpTSI,
      E->getCallOperator()->getConstexprKind(),
      E->getCallOperator()->getStorageClass(), Params,
      E->hasExplicitResultType());

  getDerived().transformAttrs(E->getCallOperator(), NewCallOperator);
  getDerived().transformedLocalDecl(E->getCallOperator(), {NewCallOperator});

  {
    // Number the lambda for linkage purposes if necessary.
    Sema::ContextRAII ManglingContext(getSema(), Class->getDeclContext());

    std::optional<CXXRecordDecl::LambdaNumbering> Numbering;
    if (getDerived().ReplacingOriginal()) {
      Numbering = OldClass->getLambdaNumbering();
    }

    getSema().handleLambdaNumbering(Class, NewCallOperator, Numbering);
  }

  // FIXME: Sema's lambda-building mechanism expects us to push an expression
  // evaluation context even if we're not transforming the function body.
  getSema().PushExpressionEvaluationContext(
      E->getCallOperator()->isConsteval() ?
      Sema::ExpressionEvaluationContext::ImmediateFunctionContext :
      Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
  getSema().currentEvaluationContext().InImmediateEscalatingFunctionContext =
      getSema().getLangOpts().CPlusPlus20 &&
      E->getCallOperator()->isImmediateEscalating();

  Sema::CodeSynthesisContext C;
  C.Kind = clang::Sema::CodeSynthesisContext::LambdaExpressionSubstitution;
  C.PointOfInstantiation = E->getBody()->getBeginLoc();
  getSema().pushCodeSynthesisContext(C);

  // Instantiate the body of the lambda expression.
  StmtResult Body =
      Invalid ? StmtError() : getDerived().TransformLambdaBody(E, E->getBody());

  getSema().popCodeSynthesisContext();

  // ActOnLambda* will pop the function scope for us.
  FuncScopeCleanup.disable();

  if (Body.isInvalid()) {
    SavedContext.pop();
    getSema().ActOnLambdaError(E->getBeginLoc(), /*CurScope=*/nullptr,
                               /*IsInstantiation=*/true);
    return ExprError();
  }

  // Copy the LSI before ActOnFinishFunctionBody removes it.
  // FIXME: This is dumb. Store the lambda information somewhere that outlives
  // the call operator.
  auto LSICopy = *LSI;
  getSema().ActOnFinishFunctionBody(NewCallOperator, Body.get(),
                                    /*IsInstantiation*/ true);
  SavedContext.pop();

  // Recompute the dependency of the lambda so that we can defer the lambda call
  // construction until after we have all the necessary template arguments. For
  // example, given
  //
  // template <class> struct S {
  //   template <class U>
  //   using Type = decltype([](U){}(42.0));
  // };
  // void foo() {
  //   using T = S<int>::Type<float>;
  //             ^~~~~~
  // }
  //
  // We would end up here from instantiating S<int> when ensuring its
  // completeness. That would transform the lambda call expression regardless of
  // the absence of the corresponding argument for U.
  //
  // Going ahead with unsubstituted type U makes things worse: we would soon
  // compare the argument type (which is float) against the parameter U
  // somewhere in Sema::BuildCallExpr. Then we would quickly run into a bogus
  // error suggesting unmatched types 'U' and 'float'!
  //
  // That said, everything will be fine if we defer that semantic checking.
  // Fortunately, we have such a mechanism that bypasses it if the CallExpr is
  // dependent. Since the CallExpr's dependency boils down to the lambda's
  // dependency in this case, we can harness that by recomputing the dependency
  // from the instantiation arguments.
  //
  // FIXME: Creating the type of a lambda requires us to have a dependency
  // value, which happens before its substitution. We update its dependency
  // *after* the substitution in case we can't decide the dependency
  // so early, e.g. because we want to see if any of the *substituted*
  // parameters are dependent.
  DependencyKind = getDerived().ComputeLambdaDependency(&LSICopy);
  Class->setLambdaDependencyKind(DependencyKind);
  // Clean up the type cache created previously. Then, we re-create a type for
  // such Decl with the new DependencyKind.
  Class->setTypeForDecl(nullptr);
  getSema().Context.getTypeDeclType(Class);

  return getSema().BuildLambdaExpr(E->getBeginLoc(), Body.get()->getEndLoc(),
                                   &LSICopy);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformLambdaBody(LambdaExpr *E, Stmt *S) {
  return TransformStmt(S);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::SkipLambdaBody(LambdaExpr *E, Stmt *S) {
  // Transform captures.
  for (LambdaExpr::capture_iterator C = E->capture_begin(),
                                 CEnd = E->capture_end();
       C != CEnd; ++C) {
    // When we hit the first implicit capture, tell Sema that we've finished
    // the list of explicit captures.
    if (!C->isImplicit())
      continue;

    // Capturing 'this' is trivial.
    if (C->capturesThis()) {
      getSema().CheckCXXThisCapture(C->getLocation(), C->isExplicit(),
                                    /*BuildAndDiagnose*/ true, nullptr,
                                    C->getCaptureKind() == LCK_StarThis);
      continue;
    }
    // Captured expression will be recaptured during captured variables
    // rebuilding.
    if (C->capturesVLAType())
      continue;

    assert(C->capturesVariable() && "unexpected kind of lambda capture");
    assert(!E->isInitCapture(C) && "implicit init-capture?");

    // Transform the captured variable.
    VarDecl *CapturedVar = cast_or_null<VarDecl>(
        getDerived().TransformDecl(C->getLocation(), C->getCapturedVar()));
    if (!CapturedVar || CapturedVar->isInvalidDecl())
      return StmtError();

    // Capture the transformed variable.
    getSema().tryCaptureVariable(CapturedVar, C->getLocation());
  }

  return S;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXUnresolvedConstructExpr(
                                                  CXXUnresolvedConstructExpr *E) {
  TypeSourceInfo *T =
      getDerived().TransformTypeWithDeducedTST(E->getTypeSourceInfo());
  if (!T)
    return ExprError();

  bool ArgumentChanged = false;
  SmallVector<Expr*, 8> Args;
  Args.reserve(E->getNumArgs());
  {
    EnterExpressionEvaluationContext Context(
        getSema(), EnterExpressionEvaluationContext::InitList,
        E->isListInitialization());
    if (getDerived().TransformExprs(E->arg_begin(), E->getNumArgs(), true, Args,
                                    &ArgumentChanged))
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() &&
      T == E->getTypeSourceInfo() &&
      !ArgumentChanged)
    return E;

  // FIXME: we're faking the locations of the commas
  return getDerived().RebuildCXXUnresolvedConstructExpr(
      T, E->getLParenLoc(), Args, E->getRParenLoc(), E->isListInitialization());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXDependentScopeMemberExpr(
                                             CXXDependentScopeMemberExpr *E) {
  // Transform the base of the expression.
  ExprResult Base((Expr*) nullptr);
  Expr *OldBase;
  QualType BaseType;
  QualType ObjectType;
  if (!E->isImplicitAccess()) {
    OldBase = E->getBase();
    Base = getDerived().TransformExpr(OldBase);
    if (Base.isInvalid())
      return ExprError();

    // Start the member reference and compute the object's type.
    ParsedType ObjectTy;
    bool MayBePseudoDestructor = false;
    Base = SemaRef.ActOnStartCXXMemberReference(nullptr, Base.get(),
                                                E->getOperatorLoc(),
                                      E->isArrow()? tok::arrow : tok::period,
                                                ObjectTy,
                                                MayBePseudoDestructor);
    if (Base.isInvalid())
      return ExprError();

    ObjectType = ObjectTy.get();
    BaseType = ((Expr*) Base.get())->getType();
  } else {
    OldBase = nullptr;
    BaseType = getDerived().TransformType(E->getBaseType());
    ObjectType = BaseType->castAs<PointerType>()->getPointeeType();
  }

  // Transform the first part of the nested-name-specifier that qualifies
  // the member name.
  NamedDecl *FirstQualifierInScope
    = getDerived().TransformFirstQualifierInScope(
                                            E->getFirstQualifierFoundInScope(),
                                            E->getQualifierLoc().getBeginLoc());

  NestedNameSpecifierLoc QualifierLoc;
  if (E->getQualifier()) {
    QualifierLoc
      = getDerived().TransformNestedNameSpecifierLoc(E->getQualifierLoc(),
                                                     ObjectType,
                                                     FirstQualifierInScope);
    if (!QualifierLoc)
      return ExprError();
  }

  SourceLocation TemplateKWLoc = E->getTemplateKeywordLoc();

  // TODO: If this is a conversion-function-id, verify that the
  // destination type name (if present) resolves the same way after
  // instantiation as it did in the local scope.

  DeclarationNameInfo NameInfo
    = getDerived().TransformDeclarationNameInfo(E->getMemberNameInfo());
  if (!NameInfo.getName())
    return ExprError();

  if (!E->hasExplicitTemplateArgs()) {
    // This is a reference to a member without an explicitly-specified
    // template argument list. Optimize for this common case.
    if (!getDerived().AlwaysRebuild() &&
        Base.get() == OldBase &&
        BaseType == E->getBaseType() &&
        QualifierLoc == E->getQualifierLoc() &&
        NameInfo.getName() == E->getMember() &&
        FirstQualifierInScope == E->getFirstQualifierFoundInScope())
      return E;

    return getDerived().RebuildCXXDependentScopeMemberExpr(Base.get(),
                                                       BaseType,
                                                       E->isArrow(),
                                                       E->getOperatorLoc(),
                                                       QualifierLoc,
                                                       TemplateKWLoc,
                                                       FirstQualifierInScope,
                                                       NameInfo,
                                                       /*TemplateArgs*/nullptr);
  }

  TemplateArgumentListInfo TransArgs(E->getLAngleLoc(), E->getRAngleLoc());
  if (getDerived().TransformTemplateArguments(E->getTemplateArgs(),
                                              E->getNumTemplateArgs(),
                                              TransArgs))
    return ExprError();

  return getDerived().RebuildCXXDependentScopeMemberExpr(Base.get(),
                                                     BaseType,
                                                     E->isArrow(),
                                                     E->getOperatorLoc(),
                                                     QualifierLoc,
                                                     TemplateKWLoc,
                                                     FirstQualifierInScope,
                                                     NameInfo,
                                                     &TransArgs);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformUnresolvedMemberExpr(
    UnresolvedMemberExpr *Old) {
  // Transform the base of the expression.
  ExprResult Base((Expr *)nullptr);
  QualType BaseType;
  if (!Old->isImplicitAccess()) {
    Base = getDerived().TransformExpr(Old->getBase());
    if (Base.isInvalid())
      return ExprError();
    Base =
        getSema().PerformMemberExprBaseConversion(Base.get(), Old->isArrow());
    if (Base.isInvalid())
      return ExprError();
    BaseType = Base.get()->getType();
  } else {
    BaseType = getDerived().TransformType(Old->getBaseType());
  }

  NestedNameSpecifierLoc QualifierLoc;
  if (Old->getQualifierLoc()) {
    QualifierLoc =
        getDerived().TransformNestedNameSpecifierLoc(Old->getQualifierLoc());
    if (!QualifierLoc)
      return ExprError();
  }

  SourceLocation TemplateKWLoc = Old->getTemplateKeywordLoc();

  LookupResult R(SemaRef, Old->getMemberNameInfo(), Sema::LookupOrdinaryName);

  // Transform the declaration set.
  if (TransformOverloadExprDecls(Old, /*RequiresADL*/ false, R))
    return ExprError();

  // Determine the naming class.
  if (Old->getNamingClass()) {
    CXXRecordDecl *NamingClass = cast_or_null<CXXRecordDecl>(
        getDerived().TransformDecl(Old->getMemberLoc(), Old->getNamingClass()));
    if (!NamingClass)
      return ExprError();

    R.setNamingClass(NamingClass);
  }

  TemplateArgumentListInfo TransArgs;
  if (Old->hasExplicitTemplateArgs()) {
    TransArgs.setLAngleLoc(Old->getLAngleLoc());
    TransArgs.setRAngleLoc(Old->getRAngleLoc());
    if (getDerived().TransformTemplateArguments(
            Old->getTemplateArgs(), Old->getNumTemplateArgs(), TransArgs))
      return ExprError();
  }

  // FIXME: to do this check properly, we will need to preserve the
  // first-qualifier-in-scope here, just in case we had a dependent
  // base (and therefore couldn't do the check) and a
  // nested-name-qualifier (and therefore could do the lookup).
  NamedDecl *FirstQualifierInScope = nullptr;

  return getDerived().RebuildUnresolvedMemberExpr(
      Base.get(), BaseType, Old->getOperatorLoc(), Old->isArrow(), QualifierLoc,
      TemplateKWLoc, FirstQualifierInScope, R,
      (Old->hasExplicitTemplateArgs() ? &TransArgs : nullptr));
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXNoexceptExpr(CXXNoexceptExpr *E) {
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);
  ExprResult SubExpr = getDerived().TransformExpr(E->getOperand());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getOperand())
    return E;

  return getDerived().RebuildCXXNoexceptExpr(E->getSourceRange(),SubExpr.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformPackExpansionExpr(PackExpansionExpr *E) {
  ExprResult Pattern = getDerived().TransformExpr(E->getPattern());
  if (Pattern.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Pattern.get() == E->getPattern())
    return E;

  return getDerived().RebuildPackExpansion(Pattern.get(), E->getEllipsisLoc(),
                                           E->getNumExpansions());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformSizeOfPackExpr(SizeOfPackExpr *E) {
  // If E is not value-dependent, then nothing will change when we transform it.
  // Note: This is an instantiation-centric view.
  if (!E->isValueDependent())
    return E;

  EnterExpressionEvaluationContext Unevaluated(
      getSema(), Sema::ExpressionEvaluationContext::Unevaluated);

  ArrayRef<TemplateArgument> PackArgs;
  TemplateArgument ArgStorage;

  // Find the argument list to transform.
  if (E->isPartiallySubstituted()) {
    PackArgs = E->getPartialArguments();
  } else if (E->isValueDependent()) {
    UnexpandedParameterPack Unexpanded(E->getPack(), E->getPackLoc());
    bool ShouldExpand = false;
    bool RetainExpansion = false;
    std::optional<unsigned> NumExpansions;
    if (getDerived().TryExpandParameterPacks(E->getOperatorLoc(), E->getPackLoc(),
                                             Unexpanded,
                                             ShouldExpand, RetainExpansion,
                                             NumExpansions))
      return ExprError();

    // If we need to expand the pack, build a template argument from it and
    // expand that.
    if (ShouldExpand) {
      auto *Pack = E->getPack();
      if (auto *TTPD = dyn_cast<TemplateTypeParmDecl>(Pack)) {
        ArgStorage = getSema().Context.getPackExpansionType(
            getSema().Context.getTypeDeclType(TTPD), std::nullopt);
      } else if (auto *TTPD = dyn_cast<TemplateTemplateParmDecl>(Pack)) {
        ArgStorage = TemplateArgument(TemplateName(TTPD), std::nullopt);
      } else {
        auto *VD = cast<ValueDecl>(Pack);
        ExprResult DRE = getSema().BuildDeclRefExpr(
            VD, VD->getType().getNonLValueExprType(getSema().Context),
            VD->getType()->isReferenceType() ? VK_LValue : VK_PRValue,
            E->getPackLoc());
        if (DRE.isInvalid())
          return ExprError();
        ArgStorage = new (getSema().Context)
            PackExpansionExpr(getSema().Context.DependentTy, DRE.get(),
                              E->getPackLoc(), std::nullopt);
      }
      PackArgs = ArgStorage;
    }
  }

  // If we're not expanding the pack, just transform the decl.
  if (!PackArgs.size()) {
    auto *Pack = cast_or_null<NamedDecl>(
        getDerived().TransformDecl(E->getPackLoc(), E->getPack()));
    if (!Pack)
      return ExprError();
    return getDerived().RebuildSizeOfPackExpr(
        E->getOperatorLoc(), Pack, E->getPackLoc(), E->getRParenLoc(),
        std::nullopt, std::nullopt);
  }

  // Try to compute the result without performing a partial substitution.
  std::optional<unsigned> Result = 0;
  for (const TemplateArgument &Arg : PackArgs) {
    if (!Arg.isPackExpansion()) {
      Result = *Result + 1;
      continue;
    }

    TemplateArgumentLoc ArgLoc;
    InventTemplateArgumentLoc(Arg, ArgLoc);

    // Find the pattern of the pack expansion.
    SourceLocation Ellipsis;
    std::optional<unsigned> OrigNumExpansions;
    TemplateArgumentLoc Pattern =
        getSema().getTemplateArgumentPackExpansionPattern(ArgLoc, Ellipsis,
                                                          OrigNumExpansions);

    // Substitute under the pack expansion. Do not expand the pack (yet).
    TemplateArgumentLoc OutPattern;
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
    if (getDerived().TransformTemplateArgument(Pattern, OutPattern,
                                               /*Uneval*/ true))
      return true;

    // See if we can determine the number of arguments from the result.
    std::optional<unsigned> NumExpansions =
        getSema().getFullyPackExpandedSize(OutPattern.getArgument());
    if (!NumExpansions) {
      // No: we must be in an alias template expansion, and we're going to need
      // to actually expand the packs.
      Result = std::nullopt;
      break;
    }

    Result = *Result + *NumExpansions;
  }

  // Common case: we could determine the number of expansions without
  // substituting.
  if (Result)
    return getDerived().RebuildSizeOfPackExpr(
        E->getOperatorLoc(), E->getPack(), E->getPackLoc(), E->getRParenLoc(),
        *Result, std::nullopt);

  TemplateArgumentListInfo TransformedPackArgs(E->getPackLoc(),
                                               E->getPackLoc());
  {
    TemporaryBase Rebase(*this, E->getPackLoc(), getBaseEntity());
    typedef TemplateArgumentLocInventIterator<
        Derived, const TemplateArgument*> PackLocIterator;
    if (TransformTemplateArguments(PackLocIterator(*this, PackArgs.begin()),
                                   PackLocIterator(*this, PackArgs.end()),
                                   TransformedPackArgs, /*Uneval*/true))
      return ExprError();
  }

  // Check whether we managed to fully-expand the pack.
  // FIXME: Is it possible for us to do so and not hit the early exit path?
  SmallVector<TemplateArgument, 8> Args;
  bool PartialSubstitution = false;
  for (auto &Loc : TransformedPackArgs.arguments()) {
    Args.push_back(Loc.getArgument());
    if (Loc.getArgument().isPackExpansion())
      PartialSubstitution = true;
  }

  if (PartialSubstitution)
    return getDerived().RebuildSizeOfPackExpr(
        E->getOperatorLoc(), E->getPack(), E->getPackLoc(), E->getRParenLoc(),
        std::nullopt, Args);

  return getDerived().RebuildSizeOfPackExpr(E->getOperatorLoc(), E->getPack(),
                                            E->getPackLoc(), E->getRParenLoc(),
                                            Args.size(), std::nullopt);
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformPackIndexingExpr(PackIndexingExpr *E) {
  if (!E->isValueDependent())
    return E;

  // Transform the index
  ExprResult IndexExpr = getDerived().TransformExpr(E->getIndexExpr());
  if (IndexExpr.isInvalid())
    return ExprError();

  SmallVector<Expr *, 5> ExpandedExprs;
  if (!E->expandsToEmptyPack() && E->getExpressions().empty()) {
    Expr *Pattern = E->getPackIdExpression();
    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    getSema().collectUnexpandedParameterPacks(E->getPackIdExpression(),
                                              Unexpanded);
    assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

    // Determine whether the set of unexpanded parameter packs can and should
    // be expanded.
    bool ShouldExpand = true;
    bool RetainExpansion = false;
    std::optional<unsigned> OrigNumExpansions;
    std::optional<unsigned> NumExpansions = OrigNumExpansions;
    if (getDerived().TryExpandParameterPacks(
            E->getEllipsisLoc(), Pattern->getSourceRange(), Unexpanded,
            ShouldExpand, RetainExpansion, NumExpansions))
      return true;
    if (!ShouldExpand) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
      ExprResult Pack = getDerived().TransformExpr(Pattern);
      if (Pack.isInvalid())
        return ExprError();
      return getDerived().RebuildPackIndexingExpr(
          E->getEllipsisLoc(), E->getRSquareLoc(), Pack.get(), IndexExpr.get(),
          std::nullopt);
    }
    for (unsigned I = 0; I != *NumExpansions; ++I) {
      Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
      ExprResult Out = getDerived().TransformExpr(Pattern);
      if (Out.isInvalid())
        return true;
      if (Out.get()->containsUnexpandedParameterPack()) {
        Out = getDerived().RebuildPackExpansion(Out.get(), E->getEllipsisLoc(),
                                                OrigNumExpansions);
        if (Out.isInvalid())
          return true;
      }
      ExpandedExprs.push_back(Out.get());
    }
    // If we're supposed to retain a pack expansion, do so by temporarily
    // forgetting the partially-substituted parameter pack.
    if (RetainExpansion) {
      ForgetPartiallySubstitutedPackRAII Forget(getDerived());

      ExprResult Out = getDerived().TransformExpr(Pattern);
      if (Out.isInvalid())
        return true;

      Out = getDerived().RebuildPackExpansion(Out.get(), E->getEllipsisLoc(),
                                              OrigNumExpansions);
      if (Out.isInvalid())
        return true;
      ExpandedExprs.push_back(Out.get());
    }
  } else if (!E->expandsToEmptyPack()) {
    if (getDerived().TransformExprs(E->getExpressions().data(),
                                    E->getExpressions().size(), false,
                                    ExpandedExprs))
      return ExprError();
  }

  return getDerived().RebuildPackIndexingExpr(
      E->getEllipsisLoc(), E->getRSquareLoc(), E->getPackIdExpression(),
      IndexExpr.get(), ExpandedExprs,
      /*EmptyPack=*/ExpandedExprs.size() == 0);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformSubstNonTypeTemplateParmPackExpr(
                                          SubstNonTypeTemplateParmPackExpr *E) {
  // Default behavior is to do nothing with this transformation.
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformSubstNonTypeTemplateParmExpr(
                                          SubstNonTypeTemplateParmExpr *E) {
  // Default behavior is to do nothing with this transformation.
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformFunctionParmPackExpr(FunctionParmPackExpr *E) {
  // Default behavior is to do nothing with this transformation.
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformMaterializeTemporaryExpr(
                                                  MaterializeTemporaryExpr *E) {
  return getDerived().TransformExpr(E->getSubExpr());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXFoldExpr(CXXFoldExpr *E) {
  UnresolvedLookupExpr *Callee = nullptr;
  if (Expr *OldCallee = E->getCallee()) {
    ExprResult CalleeResult = getDerived().TransformExpr(OldCallee);
    if (CalleeResult.isInvalid())
      return ExprError();
    Callee = cast<UnresolvedLookupExpr>(CalleeResult.get());
  }

  Expr *Pattern = E->getPattern();

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  getSema().collectUnexpandedParameterPacks(Pattern, Unexpanded);
  assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

  // Determine whether the set of unexpanded parameter packs can and should
  // be expanded.
  bool Expand = true;
  bool RetainExpansion = false;
  std::optional<unsigned> OrigNumExpansions = E->getNumExpansions(),
                          NumExpansions = OrigNumExpansions;
  if (getDerived().TryExpandParameterPacks(E->getEllipsisLoc(),
                                           Pattern->getSourceRange(),
                                           Unexpanded,
                                           Expand, RetainExpansion,
                                           NumExpansions))
    return true;

  if (!Expand) {
    // Do not expand any packs here, just transform and rebuild a fold
    // expression.
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);

    ExprResult LHS =
        E->getLHS() ? getDerived().TransformExpr(E->getLHS()) : ExprResult();
    if (LHS.isInvalid())
      return true;

    ExprResult RHS =
        E->getRHS() ? getDerived().TransformExpr(E->getRHS()) : ExprResult();
    if (RHS.isInvalid())
      return true;

    if (!getDerived().AlwaysRebuild() &&
        LHS.get() == E->getLHS() && RHS.get() == E->getRHS())
      return E;

    return getDerived().RebuildCXXFoldExpr(
        Callee, E->getBeginLoc(), LHS.get(), E->getOperator(),
        E->getEllipsisLoc(), RHS.get(), E->getEndLoc(), NumExpansions);
  }

  // Formally a fold expression expands to nested parenthesized expressions.
  // Enforce this limit to avoid creating trees so deep we can't safely traverse
  // them.
  if (NumExpansions && SemaRef.getLangOpts().BracketDepth < NumExpansions) {
    SemaRef.Diag(E->getEllipsisLoc(),
                 clang::diag::err_fold_expression_limit_exceeded)
        << *NumExpansions << SemaRef.getLangOpts().BracketDepth
        << E->getSourceRange();
    SemaRef.Diag(E->getEllipsisLoc(), diag::note_bracket_depth);
    return ExprError();
  }

  // The transform has determined that we should perform an elementwise
  // expansion of the pattern. Do so.
  ExprResult Result = getDerived().TransformExpr(E->getInit());
  if (Result.isInvalid())
    return true;
  bool LeftFold = E->isLeftFold();

  // If we're retaining an expansion for a right fold, it is the innermost
  // component and takes the init (if any).
  if (!LeftFold && RetainExpansion) {
    ForgetPartiallySubstitutedPackRAII Forget(getDerived());

    ExprResult Out = getDerived().TransformExpr(Pattern);
    if (Out.isInvalid())
      return true;

    Result = getDerived().RebuildCXXFoldExpr(
        Callee, E->getBeginLoc(), Out.get(), E->getOperator(),
        E->getEllipsisLoc(), Result.get(), E->getEndLoc(), OrigNumExpansions);
    if (Result.isInvalid())
      return true;
  }

  for (unsigned I = 0; I != *NumExpansions; ++I) {
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(
        getSema(), LeftFold ? I : *NumExpansions - I - 1);
    ExprResult Out = getDerived().TransformExpr(Pattern);
    if (Out.isInvalid())
      return true;

    if (Out.get()->containsUnexpandedParameterPack()) {
      // We still have a pack; retain a pack expansion for this slice.
      Result = getDerived().RebuildCXXFoldExpr(
          Callee, E->getBeginLoc(), LeftFold ? Result.get() : Out.get(),
          E->getOperator(), E->getEllipsisLoc(),
          LeftFold ? Out.get() : Result.get(), E->getEndLoc(),
          OrigNumExpansions);
    } else if (Result.isUsable()) {
      // We've got down to a single element; build a binary operator.
      Expr *LHS = LeftFold ? Result.get() : Out.get();
      Expr *RHS = LeftFold ? Out.get() : Result.get();
      if (Callee) {
        UnresolvedSet<16> Functions;
        Functions.append(Callee->decls_begin(), Callee->decls_end());
        Result = getDerived().RebuildCXXOperatorCallExpr(
            BinaryOperator::getOverloadedOperator(E->getOperator()),
            E->getEllipsisLoc(), Callee->getBeginLoc(), Callee->requiresADL(),
            Functions, LHS, RHS);
      } else {
        Result = getDerived().RebuildBinaryOperator(E->getEllipsisLoc(),
                                                    E->getOperator(), LHS, RHS);
      }
    } else
      Result = Out;

    if (Result.isInvalid())
      return true;
  }

  // If we're retaining an expansion for a left fold, it is the outermost
  // component and takes the complete expansion so far as its init (if any).
  if (LeftFold && RetainExpansion) {
    ForgetPartiallySubstitutedPackRAII Forget(getDerived());

    ExprResult Out = getDerived().TransformExpr(Pattern);
    if (Out.isInvalid())
      return true;

    Result = getDerived().RebuildCXXFoldExpr(
        Callee, E->getBeginLoc(), Result.get(), E->getOperator(),
        E->getEllipsisLoc(), Out.get(), E->getEndLoc(), OrigNumExpansions);
    if (Result.isInvalid())
      return true;
  }

  // If we had no init and an empty pack, and we're not retaining an expansion,
  // then produce a fallback value or error.
  if (Result.isUnset())
    return getDerived().RebuildEmptyCXXFoldExpr(E->getEllipsisLoc(),
                                                E->getOperator());

  return Result;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXParenListInitExpr(CXXParenListInitExpr *E) {
  SmallVector<Expr *, 4> TransformedInits;
  ArrayRef<Expr *> InitExprs = E->getInitExprs();
  if (TransformExprs(InitExprs.data(), InitExprs.size(), true,
                     TransformedInits))
    return ExprError();

  return getDerived().RebuildParenListExpr(E->getBeginLoc(), TransformedInits,
                                           E->getEndLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformCXXStdInitializerListExpr(
    CXXStdInitializerListExpr *E) {
  return getDerived().TransformExpr(E->getSubExpr());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCStringLiteral(ObjCStringLiteral *E) {
  return SemaRef.MaybeBindToTemporary(E);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCBoolLiteralExpr(ObjCBoolLiteralExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCBoxedExpr(ObjCBoxedExpr *E) {
  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildObjCBoxedExpr(E->getSourceRange(), SubExpr.get());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCArrayLiteral(ObjCArrayLiteral *E) {
  // Transform each of the elements.
  SmallVector<Expr *, 8> Elements;
  bool ArgChanged = false;
  if (getDerived().TransformExprs(E->getElements(), E->getNumElements(),
                                  /*IsCall=*/false, Elements, &ArgChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() && !ArgChanged)
    return SemaRef.MaybeBindToTemporary(E);

  return getDerived().RebuildObjCArrayLiteral(E->getSourceRange(),
                                              Elements.data(),
                                              Elements.size());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCDictionaryLiteral(
                                                    ObjCDictionaryLiteral *E) {
  // Transform each of the elements.
  SmallVector<ObjCDictionaryElement, 8> Elements;
  bool ArgChanged = false;
  for (unsigned I = 0, N = E->getNumElements(); I != N; ++I) {
    ObjCDictionaryElement OrigElement = E->getKeyValueElement(I);

    if (OrigElement.isPackExpansion()) {
      // This key/value element is a pack expansion.
      SmallVector<UnexpandedParameterPack, 2> Unexpanded;
      getSema().collectUnexpandedParameterPacks(OrigElement.Key, Unexpanded);
      getSema().collectUnexpandedParameterPacks(OrigElement.Value, Unexpanded);
      assert(!Unexpanded.empty() && "Pack expansion without parameter packs?");

      // Determine whether the set of unexpanded parameter packs can
      // and should be expanded.
      bool Expand = true;
      bool RetainExpansion = false;
      std::optional<unsigned> OrigNumExpansions = OrigElement.NumExpansions;
      std::optional<unsigned> NumExpansions = OrigNumExpansions;
      SourceRange PatternRange(OrigElement.Key->getBeginLoc(),
                               OrigElement.Value->getEndLoc());
      if (getDerived().TryExpandParameterPacks(OrigElement.EllipsisLoc,
                                               PatternRange, Unexpanded, Expand,
                                               RetainExpansion, NumExpansions))
        return ExprError();

      if (!Expand) {
        // The transform has determined that we should perform a simple
        // transformation on the pack expansion, producing another pack
        // expansion.
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), -1);
        ExprResult Key = getDerived().TransformExpr(OrigElement.Key);
        if (Key.isInvalid())
          return ExprError();

        if (Key.get() != OrigElement.Key)
          ArgChanged = true;

        ExprResult Value = getDerived().TransformExpr(OrigElement.Value);
        if (Value.isInvalid())
          return ExprError();

        if (Value.get() != OrigElement.Value)
          ArgChanged = true;

        ObjCDictionaryElement Expansion = {
          Key.get(), Value.get(), OrigElement.EllipsisLoc, NumExpansions
        };
        Elements.push_back(Expansion);
        continue;
      }

      // Record right away that the argument was changed.  This needs
      // to happen even if the array expands to nothing.
      ArgChanged = true;

      // The transform has determined that we should perform an elementwise
      // expansion of the pattern. Do so.
      for (unsigned I = 0; I != *NumExpansions; ++I) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(getSema(), I);
        ExprResult Key = getDerived().TransformExpr(OrigElement.Key);
        if (Key.isInvalid())
          return ExprError();

        ExprResult Value = getDerived().TransformExpr(OrigElement.Value);
        if (Value.isInvalid())
          return ExprError();

        ObjCDictionaryElement Element = {
          Key.get(), Value.get(), SourceLocation(), NumExpansions
        };

        // If any unexpanded parameter packs remain, we still have a
        // pack expansion.
        // FIXME: Can this really happen?
        if (Key.get()->containsUnexpandedParameterPack() ||
            Value.get()->containsUnexpandedParameterPack())
          Element.EllipsisLoc = OrigElement.EllipsisLoc;

        Elements.push_back(Element);
      }

      // FIXME: Retain a pack expansion if RetainExpansion is true.

      // We've finished with this pack expansion.
      continue;
    }

    // Transform and check key.
    ExprResult Key = getDerived().TransformExpr(OrigElement.Key);
    if (Key.isInvalid())
      return ExprError();

    if (Key.get() != OrigElement.Key)
      ArgChanged = true;

    // Transform and check value.
    ExprResult Value
      = getDerived().TransformExpr(OrigElement.Value);
    if (Value.isInvalid())
      return ExprError();

    if (Value.get() != OrigElement.Value)
      ArgChanged = true;

    ObjCDictionaryElement Element = {Key.get(), Value.get(), SourceLocation(),
                                     std::nullopt};
    Elements.push_back(Element);
  }

  if (!getDerived().AlwaysRebuild() && !ArgChanged)
    return SemaRef.MaybeBindToTemporary(E);

  return getDerived().RebuildObjCDictionaryLiteral(E->getSourceRange(),
                                                   Elements);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCEncodeExpr(ObjCEncodeExpr *E) {
  TypeSourceInfo *EncodedTypeInfo
    = getDerived().TransformType(E->getEncodedTypeSourceInfo());
  if (!EncodedTypeInfo)
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      EncodedTypeInfo == E->getEncodedTypeSourceInfo())
    return E;

  return getDerived().RebuildObjCEncodeExpr(E->getAtLoc(),
                                            EncodedTypeInfo,
                                            E->getRParenLoc());
}

template<typename Derived>
ExprResult TreeTransform<Derived>::
TransformObjCIndirectCopyRestoreExpr(ObjCIndirectCopyRestoreExpr *E) {
  // This is a kind of implicit conversion, and it needs to get dropped
  // and recomputed for the same general reasons that ImplicitCastExprs
  // do, as well a more specific one: this expression is only valid when
  // it appears *immediately* as an argument expression.
  return getDerived().TransformExpr(E->getSubExpr());
}

template<typename Derived>
ExprResult TreeTransform<Derived>::
TransformObjCBridgedCastExpr(ObjCBridgedCastExpr *E) {
  TypeSourceInfo *TSInfo
    = getDerived().TransformType(E->getTypeInfoAsWritten());
  if (!TSInfo)
    return ExprError();

  ExprResult Result = getDerived().TransformExpr(E->getSubExpr());
  if (Result.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      TSInfo == E->getTypeInfoAsWritten() &&
      Result.get() == E->getSubExpr())
    return E;

  return SemaRef.ObjC().BuildObjCBridgedCast(
      E->getLParenLoc(), E->getBridgeKind(), E->getBridgeKeywordLoc(), TSInfo,
      Result.get());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformObjCAvailabilityCheckExpr(
    ObjCAvailabilityCheckExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCMessageExpr(ObjCMessageExpr *E) {
  // Transform arguments.
  bool ArgChanged = false;
  SmallVector<Expr*, 8> Args;
  Args.reserve(E->getNumArgs());
  if (getDerived().TransformExprs(E->getArgs(), E->getNumArgs(), false, Args,
                                  &ArgChanged))
    return ExprError();

  if (E->getReceiverKind() == ObjCMessageExpr::Class) {
    // Class message: transform the receiver type.
    TypeSourceInfo *ReceiverTypeInfo
      = getDerived().TransformType(E->getClassReceiverTypeInfo());
    if (!ReceiverTypeInfo)
      return ExprError();

    // If nothing changed, just retain the existing message send.
    if (!getDerived().AlwaysRebuild() &&
        ReceiverTypeInfo == E->getClassReceiverTypeInfo() && !ArgChanged)
      return SemaRef.MaybeBindToTemporary(E);

    // Build a new class message send.
    SmallVector<SourceLocation, 16> SelLocs;
    E->getSelectorLocs(SelLocs);
    return getDerived().RebuildObjCMessageExpr(ReceiverTypeInfo,
                                               E->getSelector(),
                                               SelLocs,
                                               E->getMethodDecl(),
                                               E->getLeftLoc(),
                                               Args,
                                               E->getRightLoc());
  }
  else if (E->getReceiverKind() == ObjCMessageExpr::SuperClass ||
           E->getReceiverKind() == ObjCMessageExpr::SuperInstance) {
    if (!E->getMethodDecl())
      return ExprError();

    // Build a new class message send to 'super'.
    SmallVector<SourceLocation, 16> SelLocs;
    E->getSelectorLocs(SelLocs);
    return getDerived().RebuildObjCMessageExpr(E->getSuperLoc(),
                                               E->getSelector(),
                                               SelLocs,
                                               E->getReceiverType(),
                                               E->getMethodDecl(),
                                               E->getLeftLoc(),
                                               Args,
                                               E->getRightLoc());
  }

  // Instance message: transform the receiver
  assert(E->getReceiverKind() == ObjCMessageExpr::Instance &&
         "Only class and instance messages may be instantiated");
  ExprResult Receiver
    = getDerived().TransformExpr(E->getInstanceReceiver());
  if (Receiver.isInvalid())
    return ExprError();

  // If nothing changed, just retain the existing message send.
  if (!getDerived().AlwaysRebuild() &&
      Receiver.get() == E->getInstanceReceiver() && !ArgChanged)
    return SemaRef.MaybeBindToTemporary(E);

  // Build a new instance message send.
  SmallVector<SourceLocation, 16> SelLocs;
  E->getSelectorLocs(SelLocs);
  return getDerived().RebuildObjCMessageExpr(Receiver.get(),
                                             E->getSelector(),
                                             SelLocs,
                                             E->getMethodDecl(),
                                             E->getLeftLoc(),
                                             Args,
                                             E->getRightLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCSelectorExpr(ObjCSelectorExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCProtocolExpr(ObjCProtocolExpr *E) {
  return E;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCIvarRefExpr(ObjCIvarRefExpr *E) {
  // Transform the base expression.
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  // We don't need to transform the ivar; it will never change.

  // If nothing changed, just retain the existing expression.
  if (!getDerived().AlwaysRebuild() &&
      Base.get() == E->getBase())
    return E;

  return getDerived().RebuildObjCIvarRefExpr(Base.get(), E->getDecl(),
                                             E->getLocation(),
                                             E->isArrow(), E->isFreeIvar());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
  // 'super' and types never change. Property never changes. Just
  // retain the existing expression.
  if (!E->isObjectReceiver())
    return E;

  // Transform the base expression.
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  // We don't need to transform the property; it will never change.

  // If nothing changed, just retain the existing expression.
  if (!getDerived().AlwaysRebuild() &&
      Base.get() == E->getBase())
    return E;

  if (E->isExplicitProperty())
    return getDerived().RebuildObjCPropertyRefExpr(Base.get(),
                                                   E->getExplicitProperty(),
                                                   E->getLocation());

  return getDerived().RebuildObjCPropertyRefExpr(Base.get(),
                                                 SemaRef.Context.PseudoObjectTy,
                                                 E->getImplicitPropertyGetter(),
                                                 E->getImplicitPropertySetter(),
                                                 E->getLocation());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCSubscriptRefExpr(ObjCSubscriptRefExpr *E) {
  // Transform the base expression.
  ExprResult Base = getDerived().TransformExpr(E->getBaseExpr());
  if (Base.isInvalid())
    return ExprError();

  // Transform the key expression.
  ExprResult Key = getDerived().TransformExpr(E->getKeyExpr());
  if (Key.isInvalid())
    return ExprError();

  // If nothing changed, just retain the existing expression.
  if (!getDerived().AlwaysRebuild() &&
      Key.get() == E->getKeyExpr() && Base.get() == E->getBaseExpr())
    return E;

  return getDerived().RebuildObjCSubscriptRefExpr(E->getRBracket(),
                                                  Base.get(), Key.get(),
                                                  E->getAtIndexMethodDecl(),
                                                  E->setAtIndexMethodDecl());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformObjCIsaExpr(ObjCIsaExpr *E) {
  // Transform the base expression.
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  // If nothing changed, just retain the existing expression.
  if (!getDerived().AlwaysRebuild() &&
      Base.get() == E->getBase())
    return E;

  return getDerived().RebuildObjCIsaExpr(Base.get(), E->getIsaMemberLoc(),
                                         E->getOpLoc(),
                                         E->isArrow());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformShuffleVectorExpr(ShuffleVectorExpr *E) {
  bool ArgumentChanged = false;
  SmallVector<Expr*, 8> SubExprs;
  SubExprs.reserve(E->getNumSubExprs());
  if (getDerived().TransformExprs(E->getSubExprs(), E->getNumSubExprs(), false,
                                  SubExprs, &ArgumentChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      !ArgumentChanged)
    return E;

  return getDerived().RebuildShuffleVectorExpr(E->getBuiltinLoc(),
                                               SubExprs,
                                               E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformConvertVectorExpr(ConvertVectorExpr *E) {
  ExprResult SrcExpr = getDerived().TransformExpr(E->getSrcExpr());
  if (SrcExpr.isInvalid())
    return ExprError();

  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeSourceInfo());
  if (!Type)
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      Type == E->getTypeSourceInfo() &&
      SrcExpr.get() == E->getSrcExpr())
    return E;

  return getDerived().RebuildConvertVectorExpr(E->getBuiltinLoc(),
                                               SrcExpr.get(), Type,
                                               E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformBlockExpr(BlockExpr *E) {
  BlockDecl *oldBlock = E->getBlockDecl();

  SemaRef.ActOnBlockStart(E->getCaretLocation(), /*Scope=*/nullptr);
  BlockScopeInfo *blockScope = SemaRef.getCurBlock();

  blockScope->TheDecl->setIsVariadic(oldBlock->isVariadic());
  blockScope->TheDecl->setBlockMissingReturnType(
                         oldBlock->blockMissingReturnType());

  SmallVector<ParmVarDecl*, 4> params;
  SmallVector<QualType, 4> paramTypes;

  const FunctionProtoType *exprFunctionType = E->getFunctionType();

  // Parameter substitution.
  Sema::ExtParameterInfoBuilder extParamInfos;
  if (getDerived().TransformFunctionTypeParams(
          E->getCaretLocation(), oldBlock->parameters(), nullptr,
          exprFunctionType->getExtParameterInfosOrNull(), paramTypes, &params,
          extParamInfos)) {
    getSema().ActOnBlockError(E->getCaretLocation(), /*Scope=*/nullptr);
    return ExprError();
  }

  QualType exprResultType =
      getDerived().TransformType(exprFunctionType->getReturnType());

  auto epi = exprFunctionType->getExtProtoInfo();
  epi.ExtParameterInfos = extParamInfos.getPointerOrNull(paramTypes.size());

  QualType functionType =
    getDerived().RebuildFunctionProtoType(exprResultType, paramTypes, epi);
  blockScope->FunctionType = functionType;

  // Set the parameters on the block decl.
  if (!params.empty())
    blockScope->TheDecl->setParams(params);

  if (!oldBlock->blockMissingReturnType()) {
    blockScope->HasImplicitReturnType = false;
    blockScope->ReturnType = exprResultType;
  }

  // Transform the body
  StmtResult body = getDerived().TransformStmt(E->getBody());
  if (body.isInvalid()) {
    getSema().ActOnBlockError(E->getCaretLocation(), /*Scope=*/nullptr);
    return ExprError();
  }

#ifndef NDEBUG
  // In builds with assertions, make sure that we captured everything we
  // captured before.
  if (!SemaRef.getDiagnostics().hasErrorOccurred()) {
    for (const auto &I : oldBlock->captures()) {
      VarDecl *oldCapture = I.getVariable();

      // Ignore parameter packs.
      if (oldCapture->isParameterPack())
        continue;

      VarDecl *newCapture =
        cast<VarDecl>(getDerived().TransformDecl(E->getCaretLocation(),
                                                 oldCapture));
      assert(blockScope->CaptureMap.count(newCapture));
    }

    // The this pointer may not be captured by the instantiated block, even when
    // it's captured by the original block, if the expression causing the
    // capture is in the discarded branch of a constexpr if statement.
    assert((!blockScope->isCXXThisCaptured() || oldBlock->capturesCXXThis()) &&
           "this pointer isn't captured in the old block");
  }
#endif

  return SemaRef.ActOnBlockStmtExpr(E->getCaretLocation(), body.get(),
                                    /*Scope=*/nullptr);
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformAsTypeExpr(AsTypeExpr *E) {
  ExprResult SrcExpr = getDerived().TransformExpr(E->getSrcExpr());
  if (SrcExpr.isInvalid())
    return ExprError();

  QualType Type = getDerived().TransformType(E->getType());

  return SemaRef.BuildAsTypeExpr(SrcExpr.get(), Type, E->getBuiltinLoc(),
                                 E->getRParenLoc());
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::TransformAtomicExpr(AtomicExpr *E) {
  bool ArgumentChanged = false;
  SmallVector<Expr*, 8> SubExprs;
  SubExprs.reserve(E->getNumSubExprs());
  if (getDerived().TransformExprs(E->getSubExprs(), E->getNumSubExprs(), false,
                                  SubExprs, &ArgumentChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() &&
      !ArgumentChanged)
    return E;

  return getDerived().RebuildAtomicExpr(E->getBuiltinLoc(), SubExprs,
                                        E->getOp(), E->getRParenLoc());
}

//===----------------------------------------------------------------------===//
// Type reconstruction
//===----------------------------------------------------------------------===//

template<typename Derived>
QualType TreeTransform<Derived>::RebuildPointerType(QualType PointeeType,
                                                    SourceLocation Star) {
  return SemaRef.BuildPointerType(PointeeType, Star,
                                  getDerived().getBaseEntity());
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildBlockPointerType(QualType PointeeType,
                                                         SourceLocation Star) {
  return SemaRef.BuildBlockPointerType(PointeeType, Star,
                                       getDerived().getBaseEntity());
}

template<typename Derived>
QualType
TreeTransform<Derived>::RebuildReferenceType(QualType ReferentType,
                                             bool WrittenAsLValue,
                                             SourceLocation Sigil) {
  return SemaRef.BuildReferenceType(ReferentType, WrittenAsLValue,
                                    Sigil, getDerived().getBaseEntity());
}

template<typename Derived>
QualType
TreeTransform<Derived>::RebuildMemberPointerType(QualType PointeeType,
                                                 QualType ClassType,
                                                 SourceLocation Sigil) {
  return SemaRef.BuildMemberPointerType(PointeeType, ClassType, Sigil,
                                        getDerived().getBaseEntity());
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildObjCTypeParamType(
           const ObjCTypeParamDecl *Decl,
           SourceLocation ProtocolLAngleLoc,
           ArrayRef<ObjCProtocolDecl *> Protocols,
           ArrayRef<SourceLocation> ProtocolLocs,
           SourceLocation ProtocolRAngleLoc) {
  return SemaRef.ObjC().BuildObjCTypeParamType(
      Decl, ProtocolLAngleLoc, Protocols, ProtocolLocs, ProtocolRAngleLoc,
      /*FailOnError=*/true);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildObjCObjectType(
           QualType BaseType,
           SourceLocation Loc,
           SourceLocation TypeArgsLAngleLoc,
           ArrayRef<TypeSourceInfo *> TypeArgs,
           SourceLocation TypeArgsRAngleLoc,
           SourceLocation ProtocolLAngleLoc,
           ArrayRef<ObjCProtocolDecl *> Protocols,
           ArrayRef<SourceLocation> ProtocolLocs,
           SourceLocation ProtocolRAngleLoc) {
  return SemaRef.ObjC().BuildObjCObjectType(
      BaseType, Loc, TypeArgsLAngleLoc, TypeArgs, TypeArgsRAngleLoc,
      ProtocolLAngleLoc, Protocols, ProtocolLocs, ProtocolRAngleLoc,
      /*FailOnError=*/true,
      /*Rebuilding=*/true);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildObjCObjectPointerType(
           QualType PointeeType,
           SourceLocation Star) {
  return SemaRef.Context.getObjCObjectPointerType(PointeeType);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, const llvm::APInt *Size,
    Expr *SizeExpr, unsigned IndexTypeQuals, SourceRange BracketsRange) {
  if (SizeExpr || !Size)
    return SemaRef.BuildArrayType(ElementType, SizeMod, SizeExpr,
                                  IndexTypeQuals, BracketsRange,
                                  getDerived().getBaseEntity());

  QualType Types[] = {
    SemaRef.Context.UnsignedCharTy, SemaRef.Context.UnsignedShortTy,
    SemaRef.Context.UnsignedIntTy, SemaRef.Context.UnsignedLongTy,
    SemaRef.Context.UnsignedLongLongTy, SemaRef.Context.UnsignedInt128Ty
  };
  QualType SizeType;
  for (const auto &T : Types)
    if (Size->getBitWidth() == SemaRef.Context.getIntWidth(T)) {
      SizeType = T;
      break;
    }

  // Note that we can return a VariableArrayType here in the case where
  // the element type was a dependent VariableArrayType.
  IntegerLiteral *ArraySize
      = IntegerLiteral::Create(SemaRef.Context, *Size, SizeType,
                               /*FIXME*/BracketsRange.getBegin());
  return SemaRef.BuildArrayType(ElementType, SizeMod, ArraySize,
                                IndexTypeQuals, BracketsRange,
                                getDerived().getBaseEntity());
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildConstantArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, const llvm::APInt &Size,
    Expr *SizeExpr, unsigned IndexTypeQuals, SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, &Size, SizeExpr,
                                        IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildIncompleteArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, unsigned IndexTypeQuals,
    SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, nullptr, nullptr,
                                       IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildVariableArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, Expr *SizeExpr,
    unsigned IndexTypeQuals, SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, nullptr,
                                       SizeExpr,
                                       IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildDependentSizedArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, Expr *SizeExpr,
    unsigned IndexTypeQuals, SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, nullptr,
                                       SizeExpr,
                                       IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildDependentAddressSpaceType(
    QualType PointeeType, Expr *AddrSpaceExpr, SourceLocation AttributeLoc) {
  return SemaRef.BuildAddressSpaceAttr(PointeeType, AddrSpaceExpr,
                                          AttributeLoc);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildVectorType(QualType ElementType,
                                                   unsigned NumElements,
                                                   VectorKind VecKind) {
  // FIXME: semantic checking!
  return SemaRef.Context.getVectorType(ElementType, NumElements, VecKind);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildDependentVectorType(
    QualType ElementType, Expr *SizeExpr, SourceLocation AttributeLoc,
    VectorKind VecKind) {
  return SemaRef.BuildVectorType(ElementType, SizeExpr, AttributeLoc);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildExtVectorType(QualType ElementType,
                                                      unsigned NumElements,
                                                 SourceLocation AttributeLoc) {
  llvm::APInt numElements(SemaRef.Context.getIntWidth(SemaRef.Context.IntTy),
                          NumElements, true);
  IntegerLiteral *VectorSize
    = IntegerLiteral::Create(SemaRef.Context, numElements, SemaRef.Context.IntTy,
                             AttributeLoc);
  return SemaRef.BuildExtVectorType(ElementType, VectorSize, AttributeLoc);
}

template<typename Derived>
QualType
TreeTransform<Derived>::RebuildDependentSizedExtVectorType(QualType ElementType,
                                                           Expr *SizeExpr,
                                                  SourceLocation AttributeLoc) {
  return SemaRef.BuildExtVectorType(ElementType, SizeExpr, AttributeLoc);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildConstantMatrixType(
    QualType ElementType, unsigned NumRows, unsigned NumColumns) {
  return SemaRef.Context.getConstantMatrixType(ElementType, NumRows,
                                               NumColumns);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildDependentSizedMatrixType(
    QualType ElementType, Expr *RowExpr, Expr *ColumnExpr,
    SourceLocation AttributeLoc) {
  return SemaRef.BuildMatrixType(ElementType, RowExpr, ColumnExpr,
                                 AttributeLoc);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildFunctionProtoType(
    QualType T,
    MutableArrayRef<QualType> ParamTypes,
    const FunctionProtoType::ExtProtoInfo &EPI) {
  return SemaRef.BuildFunctionType(T, ParamTypes,
                                   getDerived().getBaseLocation(),
                                   getDerived().getBaseEntity(),
                                   EPI);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildFunctionNoProtoType(QualType T) {
  return SemaRef.Context.getFunctionNoProtoType(T);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildUnresolvedUsingType(SourceLocation Loc,
                                                            Decl *D) {
  assert(D && "no decl found");
  if (D->isInvalidDecl()) return QualType();

  // FIXME: Doesn't account for ObjCInterfaceDecl!
  if (auto *UPD = dyn_cast<UsingPackDecl>(D)) {
    // A valid resolved using typename pack expansion decl can have multiple
    // UsingDecls, but they must each have exactly one type, and it must be
    // the same type in every case. But we must have at least one expansion!
    if (UPD->expansions().empty()) {
      getSema().Diag(Loc, diag::err_using_pack_expansion_empty)
          << UPD->isCXXClassMember() << UPD;
      return QualType();
    }

    // We might still have some unresolved types. Try to pick a resolved type
    // if we can. The final instantiation will check that the remaining
    // unresolved types instantiate to the type we pick.
    QualType FallbackT;
    QualType T;
    for (auto *E : UPD->expansions()) {
      QualType ThisT = RebuildUnresolvedUsingType(Loc, E);
      if (ThisT.isNull())
        continue;
      else if (ThisT->getAs<UnresolvedUsingType>())
        FallbackT = ThisT;
      else if (T.isNull())
        T = ThisT;
      else
        assert(getSema().Context.hasSameType(ThisT, T) &&
               "mismatched resolved types in using pack expansion");
    }
    return T.isNull() ? FallbackT : T;
  } else if (auto *Using = dyn_cast<UsingDecl>(D)) {
    assert(Using->hasTypename() &&
           "UnresolvedUsingTypenameDecl transformed to non-typename using");

    // A valid resolved using typename decl points to exactly one type decl.
    assert(++Using->shadow_begin() == Using->shadow_end());

    UsingShadowDecl *Shadow = *Using->shadow_begin();
    if (SemaRef.DiagnoseUseOfDecl(Shadow->getTargetDecl(), Loc))
      return QualType();
    return SemaRef.Context.getUsingType(
        Shadow, SemaRef.Context.getTypeDeclType(
                    cast<TypeDecl>(Shadow->getTargetDecl())));
  } else {
    assert(isa<UnresolvedUsingTypenameDecl>(D) &&
           "UnresolvedUsingTypenameDecl transformed to non-using decl");
    return SemaRef.Context.getTypeDeclType(
        cast<UnresolvedUsingTypenameDecl>(D));
  }
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildTypeOfExprType(Expr *E, SourceLocation,
                                                       TypeOfKind Kind) {
  return SemaRef.BuildTypeofExprType(E, Kind);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildTypeOfType(QualType Underlying,
                                                   TypeOfKind Kind) {
  return SemaRef.Context.getTypeOfType(Underlying, Kind);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildDecltypeType(Expr *E, SourceLocation) {
  return SemaRef.BuildDecltypeType(E);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildPackIndexingType(
    QualType Pattern, Expr *IndexExpr, SourceLocation Loc,
    SourceLocation EllipsisLoc, bool FullySubstituted,
    ArrayRef<QualType> Expansions) {
  return SemaRef.BuildPackIndexingType(Pattern, IndexExpr, Loc, EllipsisLoc,
                                       FullySubstituted, Expansions);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildUnaryTransformType(QualType BaseType,
                                            UnaryTransformType::UTTKind UKind,
                                            SourceLocation Loc) {
  return SemaRef.BuildUnaryTransformType(BaseType, UKind, Loc);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildTemplateSpecializationType(
                                                      TemplateName Template,
                                             SourceLocation TemplateNameLoc,
                                     TemplateArgumentListInfo &TemplateArgs) {
  return SemaRef.CheckTemplateIdType(Template, TemplateNameLoc, TemplateArgs);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildAtomicType(QualType ValueType,
                                                   SourceLocation KWLoc) {
  return SemaRef.BuildAtomicType(ValueType, KWLoc);
}

template<typename Derived>
QualType TreeTransform<Derived>::RebuildPipeType(QualType ValueType,
                                                 SourceLocation KWLoc,
                                                 bool isReadPipe) {
  return isReadPipe ? SemaRef.BuildReadPipeType(ValueType, KWLoc)
                    : SemaRef.BuildWritePipeType(ValueType, KWLoc);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildBitIntType(bool IsUnsigned,
                                                   unsigned NumBits,
                                                   SourceLocation Loc) {
  llvm::APInt NumBitsAP(SemaRef.Context.getIntWidth(SemaRef.Context.IntTy),
                        NumBits, true);
  IntegerLiteral *Bits = IntegerLiteral::Create(SemaRef.Context, NumBitsAP,
                                                SemaRef.Context.IntTy, Loc);
  return SemaRef.BuildBitIntType(IsUnsigned, Bits, Loc);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildDependentBitIntType(
    bool IsUnsigned, Expr *NumBitsExpr, SourceLocation Loc) {
  return SemaRef.BuildBitIntType(IsUnsigned, NumBitsExpr, Loc);
}

template<typename Derived>
TemplateName
TreeTransform<Derived>::RebuildTemplateName(CXXScopeSpec &SS,
                                            bool TemplateKW,
                                            TemplateDecl *Template) {
  return SemaRef.Context.getQualifiedTemplateName(SS.getScopeRep(), TemplateKW,
                                                  TemplateName(Template));
}

template<typename Derived>
TemplateName
TreeTransform<Derived>::RebuildTemplateName(CXXScopeSpec &SS,
                                            SourceLocation TemplateKWLoc,
                                            const IdentifierInfo &Name,
                                            SourceLocation NameLoc,
                                            QualType ObjectType,
                                            NamedDecl *FirstQualifierInScope,
                                            bool AllowInjectedClassName) {
  UnqualifiedId TemplateName;
  TemplateName.setIdentifier(&Name, NameLoc);
  Sema::TemplateTy Template;
  getSema().ActOnTemplateName(/*Scope=*/nullptr, SS, TemplateKWLoc,
                              TemplateName, ParsedType::make(ObjectType),
                              /*EnteringContext=*/false, Template,
                              AllowInjectedClassName);
  return Template.get();
}

template<typename Derived>
TemplateName
TreeTransform<Derived>::RebuildTemplateName(CXXScopeSpec &SS,
                                            SourceLocation TemplateKWLoc,
                                            OverloadedOperatorKind Operator,
                                            SourceLocation NameLoc,
                                            QualType ObjectType,
                                            bool AllowInjectedClassName) {
  UnqualifiedId Name;
  // FIXME: Bogus location information.
  SourceLocation SymbolLocations[3] = { NameLoc, NameLoc, NameLoc };
  Name.setOperatorFunctionId(NameLoc, Operator, SymbolLocations);
  Sema::TemplateTy Template;
  getSema().ActOnTemplateName(
      /*Scope=*/nullptr, SS, TemplateKWLoc, Name, ParsedType::make(ObjectType),
      /*EnteringContext=*/false, Template, AllowInjectedClassName);
  return Template.get();
}

template <typename Derived>
ExprResult TreeTransform<Derived>::RebuildCXXOperatorCallExpr(
    OverloadedOperatorKind Op, SourceLocation OpLoc, SourceLocation CalleeLoc,
    bool RequiresADL, const UnresolvedSetImpl &Functions, Expr *First,
    Expr *Second) {
  bool isPostIncDec = Second && (Op == OO_PlusPlus || Op == OO_MinusMinus);

  if (First->getObjectKind() == OK_ObjCProperty) {
    BinaryOperatorKind Opc = BinaryOperator::getOverloadedOpcode(Op);
    if (BinaryOperator::isAssignmentOp(Opc))
      return SemaRef.PseudoObject().checkAssignment(/*Scope=*/nullptr, OpLoc,
                                                    Opc, First, Second);
    ExprResult Result = SemaRef.CheckPlaceholderExpr(First);
    if (Result.isInvalid())
      return ExprError();
    First = Result.get();
  }

  if (Second && Second->getObjectKind() == OK_ObjCProperty) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Second);
    if (Result.isInvalid())
      return ExprError();
    Second = Result.get();
  }

  // Determine whether this should be a builtin operation.
  if (Op == OO_Subscript) {
    if (!First->getType()->isOverloadableType() &&
        !Second->getType()->isOverloadableType())
      return getSema().CreateBuiltinArraySubscriptExpr(First, CalleeLoc, Second,
                                                       OpLoc);
  } else if (Op == OO_Arrow) {
    // It is possible that the type refers to a RecoveryExpr created earlier
    // in the tree transformation.
    if (First->getType()->isDependentType())
      return ExprError();
    // -> is never a builtin operation.
    return SemaRef.BuildOverloadedArrowExpr(nullptr, First, OpLoc);
  } else if (Second == nullptr || isPostIncDec) {
    if (!First->getType()->isOverloadableType() ||
        (Op == OO_Amp && getSema().isQualifiedMemberAccess(First))) {
      // The argument is not of overloadable type, or this is an expression
      // of the form &Class::member, so try to create a built-in unary
      // operation.
      UnaryOperatorKind Opc
        = UnaryOperator::getOverloadedOpcode(Op, isPostIncDec);

      return getSema().CreateBuiltinUnaryOp(OpLoc, Opc, First);
    }
  } else {
    if (!First->isTypeDependent() && !Second->isTypeDependent() &&
        !First->getType()->isOverloadableType() &&
        !Second->getType()->isOverloadableType()) {
      // Neither of the arguments is type-dependent or has an overloadable
      // type, so try to create a built-in binary operation.
      BinaryOperatorKind Opc = BinaryOperator::getOverloadedOpcode(Op);
      ExprResult Result
        = SemaRef.CreateBuiltinBinOp(OpLoc, Opc, First, Second);
      if (Result.isInvalid())
        return ExprError();

      return Result;
    }
  }

  // Create the overloaded operator invocation for unary operators.
  if (!Second || isPostIncDec) {
    UnaryOperatorKind Opc
      = UnaryOperator::getOverloadedOpcode(Op, isPostIncDec);
    return SemaRef.CreateOverloadedUnaryOp(OpLoc, Opc, Functions, First,
                                           RequiresADL);
  }

  // Create the overloaded operator invocation for binary operators.
  BinaryOperatorKind Opc = BinaryOperator::getOverloadedOpcode(Op);
  ExprResult Result = SemaRef.CreateOverloadedBinOp(OpLoc, Opc, Functions,
                                                    First, Second, RequiresADL);
  if (Result.isInvalid())
    return ExprError();

  return Result;
}

template<typename Derived>
ExprResult
TreeTransform<Derived>::RebuildCXXPseudoDestructorExpr(Expr *Base,
                                                     SourceLocation OperatorLoc,
                                                       bool isArrow,
                                                       CXXScopeSpec &SS,
                                                     TypeSourceInfo *ScopeType,
                                                       SourceLocation CCLoc,
                                                       SourceLocation TildeLoc,
                                        PseudoDestructorTypeStorage Destroyed) {
  QualType BaseType = Base->getType();
  if (Base->isTypeDependent() || Destroyed.getIdentifier() ||
      (!isArrow && !BaseType->getAs<RecordType>()) ||
      (isArrow && BaseType->getAs<PointerType>() &&
       !BaseType->castAs<PointerType>()->getPointeeType()
                                              ->template getAs<RecordType>())){
    // This pseudo-destructor expression is still a pseudo-destructor.
    return SemaRef.BuildPseudoDestructorExpr(
        Base, OperatorLoc, isArrow ? tok::arrow : tok::period, SS, ScopeType,
        CCLoc, TildeLoc, Destroyed);
  }

  TypeSourceInfo *DestroyedType = Destroyed.getTypeSourceInfo();
  DeclarationName Name(SemaRef.Context.DeclarationNames.getCXXDestructorName(
                 SemaRef.Context.getCanonicalType(DestroyedType->getType())));
  DeclarationNameInfo NameInfo(Name, Destroyed.getLocation());
  NameInfo.setNamedTypeInfo(DestroyedType);

  // The scope type is now known to be a valid nested name specifier
  // component. Tack it on to the end of the nested name specifier.
  if (ScopeType) {
    if (!ScopeType->getType()->getAs<TagType>()) {
      getSema().Diag(ScopeType->getTypeLoc().getBeginLoc(),
                     diag::err_expected_class_or_namespace)
          << ScopeType->getType() << getSema().getLangOpts().CPlusPlus;
      return ExprError();
    }
    SS.Extend(SemaRef.Context, SourceLocation(), ScopeType->getTypeLoc(),
              CCLoc);
  }

  SourceLocation TemplateKWLoc; // FIXME: retrieve it from caller.
  return getSema().BuildMemberReferenceExpr(Base, BaseType,
                                            OperatorLoc, isArrow,
                                            SS, TemplateKWLoc,
                                            /*FIXME: FirstQualifier*/ nullptr,
                                            NameInfo,
                                            /*TemplateArgs*/ nullptr,
                                            /*S*/nullptr);
}

template<typename Derived>
StmtResult
TreeTransform<Derived>::TransformCapturedStmt(CapturedStmt *S) {
  SourceLocation Loc = S->getBeginLoc();
  CapturedDecl *CD = S->getCapturedDecl();
  unsigned NumParams = CD->getNumParams();
  unsigned ContextParamPos = CD->getContextParamPosition();
  SmallVector<Sema::CapturedParamNameType, 4> Params;
  for (unsigned I = 0; I < NumParams; ++I) {
    if (I != ContextParamPos) {
      Params.push_back(
             std::make_pair(
                  CD->getParam(I)->getName(),
                  getDerived().TransformType(CD->getParam(I)->getType())));
    } else {
      Params.push_back(std::make_pair(StringRef(), QualType()));
    }
  }
  getSema().ActOnCapturedRegionStart(Loc, /*CurScope*/nullptr,
                                     S->getCapturedRegionKind(), Params);
  StmtResult Body;
  {
    Sema::CompoundScopeRAII CompoundScope(getSema());
    Body = getDerived().TransformStmt(S->getCapturedStmt());
  }

  if (Body.isInvalid()) {
    getSema().ActOnCapturedRegionError();
    return StmtError();
  }

  return getSema().ActOnCapturedRegionEnd(Body.get());
}

} // end namespace clang

#endif // LLVM_CLANG_LIB_SEMA_TREETRANSFORM_H
