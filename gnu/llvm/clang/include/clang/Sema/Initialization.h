//===- Initialization.h - Semantic Analysis for Initializers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides supporting data types for initialization of objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_INITIALIZATION_H
#define LLVM_CLANG_SEMA_INITIALIZATION_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace clang {

class CXXBaseSpecifier;
class CXXConstructorDecl;
class ObjCMethodDecl;
class Sema;

/// Describes an entity that is being initialized.
class alignas(8) InitializedEntity {
public:
  /// Specifies the kind of entity being initialized.
  enum EntityKind {
    /// The entity being initialized is a variable.
    EK_Variable,

    /// The entity being initialized is a function parameter.
    EK_Parameter,

    /// The entity being initialized is a non-type template parameter.
    EK_TemplateParameter,

    /// The entity being initialized is the result of a function call.
    EK_Result,

    /// The entity being initialized is the result of a statement expression.
    EK_StmtExprResult,

    /// The entity being initialized is an exception object that
    /// is being thrown.
    EK_Exception,

    /// The entity being initialized is a non-static data member
    /// subobject.
    EK_Member,

    /// The entity being initialized is an element of an array.
    EK_ArrayElement,

    /// The entity being initialized is an object (or array of
    /// objects) allocated via new.
    EK_New,

    /// The entity being initialized is a temporary object.
    EK_Temporary,

    /// The entity being initialized is a base member subobject.
    EK_Base,

    /// The initialization is being done by a delegating constructor.
    EK_Delegating,

    /// The entity being initialized is an element of a vector.
    /// or vector.
    EK_VectorElement,

    /// The entity being initialized is a field of block descriptor for
    /// the copied-in c++ object.
    EK_BlockElement,

    /// The entity being initialized is a field of block descriptor for the
    /// copied-in lambda object that's used in the lambda to block conversion.
    EK_LambdaToBlockConversionBlockElement,

    /// The entity being initialized is the real or imaginary part of a
    /// complex number.
    EK_ComplexElement,

    /// The entity being initialized is the field that captures a
    /// variable in a lambda.
    EK_LambdaCapture,

    /// The entity being initialized is the initializer for a compound
    /// literal.
    EK_CompoundLiteralInit,

    /// The entity being implicitly initialized back to the formal
    /// result type.
    EK_RelatedResult,

    /// The entity being initialized is a function parameter; function
    /// is member of group of audited CF APIs.
    EK_Parameter_CF_Audited,

    /// The entity being initialized is a structured binding of a
    /// decomposition declaration.
    EK_Binding,

    /// The entity being initialized is a non-static data member subobject of an
    /// object initialized via parenthesized aggregate initialization.
    EK_ParenAggInitMember,

    // Note: err_init_conversion_failed in DiagnosticSemaKinds.td uses this
    // enum as an index for its first %select.  When modifying this list,
    // that diagnostic text needs to be updated as well.
  };

private:
  /// The kind of entity being initialized.
  EntityKind Kind;

  /// If non-NULL, the parent entity in which this
  /// initialization occurs.
  const InitializedEntity *Parent = nullptr;

  /// The type of the object or reference being initialized.
  QualType Type;

  /// The mangling number for the next reference temporary to be created.
  mutable unsigned ManglingNumber = 0;

  struct LN {
    /// When Kind == EK_Result, EK_Exception, EK_New, the
    /// location of the 'return', 'throw', or 'new' keyword,
    /// respectively. When Kind == EK_Temporary, the location where
    /// the temporary is being created.
    SourceLocation Location;

    /// Whether the entity being initialized may end up using the
    /// named return value optimization (NRVO).
    bool NRVO;
  };

  struct VD {
    /// The VarDecl, FieldDecl, or BindingDecl being initialized.
    ValueDecl *VariableOrMember;

    /// When Kind == EK_Member, whether this is an implicit member
    /// initialization in a copy or move constructor. These can perform array
    /// copies.
    bool IsImplicitFieldInit;

    /// When Kind == EK_Member, whether this is the initial initialization
    /// check for a default member initializer.
    bool IsDefaultMemberInit;
  };

  struct C {
    /// The name of the variable being captured by an EK_LambdaCapture.
    IdentifierInfo *VarID;

    /// The source location at which the capture occurs.
    SourceLocation Location;
  };

  union {
    /// When Kind == EK_Variable, EK_Member, EK_Binding, or
    /// EK_TemplateParameter, the variable, binding, or template parameter.
    VD Variable;

    /// When Kind == EK_RelatedResult, the ObjectiveC method where
    /// result type was implicitly changed to accommodate ARC semantics.
    ObjCMethodDecl *MethodDecl;

    /// When Kind == EK_Parameter, the ParmVarDecl, with the
    /// integer indicating whether the parameter is "consumed".
    llvm::PointerIntPair<ParmVarDecl *, 1> Parameter;

    /// When Kind == EK_Temporary or EK_CompoundLiteralInit, the type
    /// source information for the temporary.
    TypeSourceInfo *TypeInfo;

    struct LN LocAndNRVO;

    /// When Kind == EK_Base, the base specifier that provides the
    /// base class. The integer specifies whether the base is an inherited
    /// virtual base.
    llvm::PointerIntPair<const CXXBaseSpecifier *, 1> Base;

    /// When Kind == EK_ArrayElement, EK_VectorElement, or
    /// EK_ComplexElement, the index of the array or vector element being
    /// initialized.
    unsigned Index;

    struct C Capture;
  };

  InitializedEntity() {}

  /// Create the initialization entity for a variable.
  InitializedEntity(VarDecl *Var, EntityKind EK = EK_Variable)
      : Kind(EK), Type(Var->getType()), Variable{Var, false, false} {}

  /// Create the initialization entity for the result of a
  /// function, throwing an object, performing an explicit cast, or
  /// initializing a parameter for which there is no declaration.
  InitializedEntity(EntityKind Kind, SourceLocation Loc, QualType Type,
                    bool NRVO = false)
      : Kind(Kind), Type(Type) {
    new (&LocAndNRVO) LN;
    LocAndNRVO.Location = Loc;
    LocAndNRVO.NRVO = NRVO;
  }

  /// Create the initialization entity for a member subobject.
  InitializedEntity(FieldDecl *Member, const InitializedEntity *Parent,
                    bool Implicit, bool DefaultMemberInit,
                    bool IsParenAggInit = false)
      : Kind(IsParenAggInit ? EK_ParenAggInitMember : EK_Member),
        Parent(Parent), Type(Member->getType()),
        Variable{Member, Implicit, DefaultMemberInit} {}

  /// Create the initialization entity for an array element.
  InitializedEntity(ASTContext &Context, unsigned Index,
                    const InitializedEntity &Parent);

  /// Create the initialization entity for a lambda capture.
  InitializedEntity(IdentifierInfo *VarID, QualType FieldType, SourceLocation Loc)
      : Kind(EK_LambdaCapture), Type(FieldType) {
    new (&Capture) C;
    Capture.VarID = VarID;
    Capture.Location = Loc;
  }

public:
  /// Create the initialization entity for a variable.
  static InitializedEntity InitializeVariable(VarDecl *Var) {
    return InitializedEntity(Var);
  }

  /// Create the initialization entity for a parameter.
  static InitializedEntity InitializeParameter(ASTContext &Context,
                                               ParmVarDecl *Parm) {
    return InitializeParameter(Context, Parm, Parm->getType());
  }

  /// Create the initialization entity for a parameter, but use
  /// another type.
  static InitializedEntity
  InitializeParameter(ASTContext &Context, ParmVarDecl *Parm, QualType Type) {
    bool Consumed = (Context.getLangOpts().ObjCAutoRefCount &&
                     Parm->hasAttr<NSConsumedAttr>());

    InitializedEntity Entity;
    Entity.Kind = EK_Parameter;
    Entity.Type =
      Context.getVariableArrayDecayedType(Type.getUnqualifiedType());
    Entity.Parent = nullptr;
    Entity.Parameter = {Parm, Consumed};
    return Entity;
  }

  /// Create the initialization entity for a parameter that is
  /// only known by its type.
  static InitializedEntity InitializeParameter(ASTContext &Context,
                                               QualType Type,
                                               bool Consumed) {
    InitializedEntity Entity;
    Entity.Kind = EK_Parameter;
    Entity.Type = Context.getVariableArrayDecayedType(Type);
    Entity.Parent = nullptr;
    Entity.Parameter = {nullptr, Consumed};
    return Entity;
  }

  /// Create the initialization entity for a template parameter.
  static InitializedEntity
  InitializeTemplateParameter(QualType T, NonTypeTemplateParmDecl *Param) {
    InitializedEntity Entity;
    Entity.Kind = EK_TemplateParameter;
    Entity.Type = T;
    Entity.Parent = nullptr;
    Entity.Variable = {Param, false, false};
    return Entity;
  }

  /// Create the initialization entity for the result of a function.
  static InitializedEntity InitializeResult(SourceLocation ReturnLoc,
                                            QualType Type) {
    return InitializedEntity(EK_Result, ReturnLoc, Type);
  }

  static InitializedEntity InitializeStmtExprResult(SourceLocation ReturnLoc,
                                            QualType Type) {
    return InitializedEntity(EK_StmtExprResult, ReturnLoc, Type);
  }

  static InitializedEntity InitializeBlock(SourceLocation BlockVarLoc,
                                           QualType Type) {
    return InitializedEntity(EK_BlockElement, BlockVarLoc, Type);
  }

  static InitializedEntity InitializeLambdaToBlock(SourceLocation BlockVarLoc,
                                                   QualType Type) {
    return InitializedEntity(EK_LambdaToBlockConversionBlockElement,
                             BlockVarLoc, Type);
  }

  /// Create the initialization entity for an exception object.
  static InitializedEntity InitializeException(SourceLocation ThrowLoc,
                                               QualType Type) {
    return InitializedEntity(EK_Exception, ThrowLoc, Type);
  }

  /// Create the initialization entity for an object allocated via new.
  static InitializedEntity InitializeNew(SourceLocation NewLoc, QualType Type) {
    return InitializedEntity(EK_New, NewLoc, Type);
  }

  /// Create the initialization entity for a temporary.
  static InitializedEntity InitializeTemporary(QualType Type) {
    return InitializeTemporary(nullptr, Type);
  }

  /// Create the initialization entity for a temporary.
  static InitializedEntity InitializeTemporary(ASTContext &Context,
                                               TypeSourceInfo *TypeInfo) {
    QualType Type = TypeInfo->getType();
    if (Context.getLangOpts().OpenCLCPlusPlus) {
      assert(!Type.hasAddressSpace() && "Temporary already has address space!");
      Type = Context.getAddrSpaceQualType(Type, LangAS::opencl_private);
    }

    return InitializeTemporary(TypeInfo, Type);
  }

  /// Create the initialization entity for a temporary.
  static InitializedEntity InitializeTemporary(TypeSourceInfo *TypeInfo,
                                               QualType Type) {
    InitializedEntity Result(EK_Temporary, SourceLocation(), Type);
    Result.TypeInfo = TypeInfo;
    return Result;
  }

  /// Create the initialization entity for a related result.
  static InitializedEntity InitializeRelatedResult(ObjCMethodDecl *MD,
                                                   QualType Type) {
    InitializedEntity Result(EK_RelatedResult, SourceLocation(), Type);
    Result.MethodDecl = MD;
    return Result;
  }

  /// Create the initialization entity for a base class subobject.
  static InitializedEntity
  InitializeBase(ASTContext &Context, const CXXBaseSpecifier *Base,
                 bool IsInheritedVirtualBase,
                 const InitializedEntity *Parent = nullptr);

  /// Create the initialization entity for a delegated constructor.
  static InitializedEntity InitializeDelegation(QualType Type) {
    return InitializedEntity(EK_Delegating, SourceLocation(), Type);
  }

  /// Create the initialization entity for a member subobject.
  static InitializedEntity
  InitializeMember(FieldDecl *Member,
                   const InitializedEntity *Parent = nullptr,
                   bool Implicit = false) {
    return InitializedEntity(Member, Parent, Implicit, false);
  }

  /// Create the initialization entity for a member subobject.
  static InitializedEntity
  InitializeMember(IndirectFieldDecl *Member,
                   const InitializedEntity *Parent = nullptr,
                   bool Implicit = false) {
    return InitializedEntity(Member->getAnonField(), Parent, Implicit, false);
  }

  /// Create the initialization entity for a member subobject initialized via
  /// parenthesized aggregate init.
  static InitializedEntity InitializeMemberFromParenAggInit(FieldDecl *Member) {
    return InitializedEntity(Member, /*Parent=*/nullptr, /*Implicit=*/false,
                             /*DefaultMemberInit=*/false,
                             /*IsParenAggInit=*/true);
  }

  /// Create the initialization entity for a default member initializer.
  static InitializedEntity
  InitializeMemberFromDefaultMemberInitializer(FieldDecl *Member) {
    return InitializedEntity(Member, nullptr, false, true);
  }

  /// Create the initialization entity for an array element.
  static InitializedEntity InitializeElement(ASTContext &Context,
                                             unsigned Index,
                                             const InitializedEntity &Parent) {
    return InitializedEntity(Context, Index, Parent);
  }

  /// Create the initialization entity for a structured binding.
  static InitializedEntity InitializeBinding(VarDecl *Binding) {
    return InitializedEntity(Binding, EK_Binding);
  }

  /// Create the initialization entity for a lambda capture.
  ///
  /// \p VarID The name of the entity being captured, or nullptr for 'this'.
  static InitializedEntity InitializeLambdaCapture(IdentifierInfo *VarID,
                                                   QualType FieldType,
                                                   SourceLocation Loc) {
    return InitializedEntity(VarID, FieldType, Loc);
  }

  /// Create the entity for a compound literal initializer.
  static InitializedEntity InitializeCompoundLiteralInit(TypeSourceInfo *TSI) {
    InitializedEntity Result(EK_CompoundLiteralInit, SourceLocation(),
                             TSI->getType());
    Result.TypeInfo = TSI;
    return Result;
  }

  /// Determine the kind of initialization.
  EntityKind getKind() const { return Kind; }

  /// Retrieve the parent of the entity being initialized, when
  /// the initialization itself is occurring within the context of a
  /// larger initialization.
  const InitializedEntity *getParent() const { return Parent; }

  /// Retrieve type being initialized.
  QualType getType() const { return Type; }

  /// Retrieve complete type-source information for the object being
  /// constructed, if known.
  TypeSourceInfo *getTypeSourceInfo() const {
    if (Kind == EK_Temporary || Kind == EK_CompoundLiteralInit)
      return TypeInfo;

    return nullptr;
  }

  /// Retrieve the name of the entity being initialized.
  DeclarationName getName() const;

  /// Retrieve the variable, parameter, or field being
  /// initialized.
  ValueDecl *getDecl() const;

  /// Retrieve the ObjectiveC method being initialized.
  ObjCMethodDecl *getMethodDecl() const { return MethodDecl; }

  /// Determine whether this initialization allows the named return
  /// value optimization, which also applies to thrown objects.
  bool allowsNRVO() const;

  bool isParameterKind() const {
    return (getKind() == EK_Parameter  ||
            getKind() == EK_Parameter_CF_Audited);
  }

  bool isParamOrTemplateParamKind() const {
    return isParameterKind() || getKind() == EK_TemplateParameter;
  }

  /// Determine whether this initialization consumes the
  /// parameter.
  bool isParameterConsumed() const {
    assert(isParameterKind() && "Not a parameter");
    return Parameter.getInt();
  }

  /// Retrieve the base specifier.
  const CXXBaseSpecifier *getBaseSpecifier() const {
    assert(getKind() == EK_Base && "Not a base specifier");
    return Base.getPointer();
  }

  /// Return whether the base is an inherited virtual base.
  bool isInheritedVirtualBase() const {
    assert(getKind() == EK_Base && "Not a base specifier");
    return Base.getInt();
  }

  /// Determine whether this is an array new with an unknown bound.
  bool isVariableLengthArrayNew() const {
    return getKind() == EK_New && isa_and_nonnull<IncompleteArrayType>(
                                      getType()->getAsArrayTypeUnsafe());
  }

  /// Is this the implicit initialization of a member of a class from
  /// a defaulted constructor?
  bool isImplicitMemberInitializer() const {
    return getKind() == EK_Member && Variable.IsImplicitFieldInit;
  }

  /// Is this the default member initializer of a member (specified inside
  /// the class definition)?
  bool isDefaultMemberInitializer() const {
    return getKind() == EK_Member && Variable.IsDefaultMemberInit;
  }

  /// Determine the location of the 'return' keyword when initializing
  /// the result of a function call.
  SourceLocation getReturnLoc() const {
    assert(getKind() == EK_Result && "No 'return' location!");
    return LocAndNRVO.Location;
  }

  /// Determine the location of the 'throw' keyword when initializing
  /// an exception object.
  SourceLocation getThrowLoc() const {
    assert(getKind() == EK_Exception && "No 'throw' location!");
    return LocAndNRVO.Location;
  }

  /// If this is an array, vector, or complex number element, get the
  /// element's index.
  unsigned getElementIndex() const {
    assert(getKind() == EK_ArrayElement || getKind() == EK_VectorElement ||
           getKind() == EK_ComplexElement);
    return Index;
  }

  /// If this is already the initializer for an array or vector
  /// element, sets the element index.
  void setElementIndex(unsigned Index) {
    assert(getKind() == EK_ArrayElement || getKind() == EK_VectorElement ||
           getKind() == EK_ComplexElement);
    this->Index = Index;
  }

  /// For a lambda capture, return the capture's name.
  StringRef getCapturedVarName() const {
    assert(getKind() == EK_LambdaCapture && "Not a lambda capture!");
    return Capture.VarID ? Capture.VarID->getName() : "this";
  }

  /// Determine the location of the capture when initializing
  /// field from a captured variable in a lambda.
  SourceLocation getCaptureLoc() const {
    assert(getKind() == EK_LambdaCapture && "Not a lambda capture!");
    return Capture.Location;
  }

  void setParameterCFAudited() {
    Kind = EK_Parameter_CF_Audited;
  }

  unsigned allocateManglingNumber() const { return ++ManglingNumber; }

  /// Dump a representation of the initialized entity to standard error,
  /// for debugging purposes.
  void dump() const;

private:
  unsigned dumpImpl(raw_ostream &OS) const;
};

/// Describes the kind of initialization being performed, along with
/// location information for tokens related to the initialization (equal sign,
/// parentheses).
class InitializationKind {
public:
  /// The kind of initialization being performed.
  enum InitKind {
    /// Direct initialization
    IK_Direct,

    /// Direct list-initialization
    IK_DirectList,

    /// Copy initialization
    IK_Copy,

    /// Default initialization
    IK_Default,

    /// Value initialization
    IK_Value
  };

private:
  /// The context of the initialization.
  enum InitContext {
    /// Normal context
    IC_Normal,

    /// Normal context, but allows explicit conversion functions
    IC_ExplicitConvs,

    /// Implicit context (value initialization)
    IC_Implicit,

    /// Static cast context
    IC_StaticCast,

    /// C-style cast context
    IC_CStyleCast,

    /// Functional cast context
    IC_FunctionalCast
  };

  /// The kind of initialization being performed.
  InitKind Kind : 8;

  /// The context of the initialization.
  InitContext Context : 8;

  /// The source locations involved in the initialization.
  SourceLocation Locations[3];

  InitializationKind(InitKind Kind, InitContext Context, SourceLocation Loc1,
                     SourceLocation Loc2, SourceLocation Loc3)
      : Kind(Kind), Context(Context) {
    Locations[0] = Loc1;
    Locations[1] = Loc2;
    Locations[2] = Loc3;
  }

public:
  /// Create a direct initialization.
  static InitializationKind CreateDirect(SourceLocation InitLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation RParenLoc) {
    return InitializationKind(IK_Direct, IC_Normal,
                              InitLoc, LParenLoc, RParenLoc);
  }

  static InitializationKind CreateDirectList(SourceLocation InitLoc) {
    return InitializationKind(IK_DirectList, IC_Normal, InitLoc, InitLoc,
                              InitLoc);
  }

  static InitializationKind CreateDirectList(SourceLocation InitLoc,
                                             SourceLocation LBraceLoc,
                                             SourceLocation RBraceLoc) {
    return InitializationKind(IK_DirectList, IC_Normal, InitLoc, LBraceLoc,
                              RBraceLoc);
  }

  /// Create a direct initialization due to a cast that isn't a C-style
  /// or functional cast.
  static InitializationKind CreateCast(SourceRange TypeRange) {
    return InitializationKind(IK_Direct, IC_StaticCast, TypeRange.getBegin(),
                              TypeRange.getBegin(), TypeRange.getEnd());
  }

  /// Create a direct initialization for a C-style cast.
  static InitializationKind CreateCStyleCast(SourceLocation StartLoc,
                                             SourceRange TypeRange,
                                             bool InitList) {
    // C++ cast syntax doesn't permit init lists, but C compound literals are
    // exactly that.
    return InitializationKind(InitList ? IK_DirectList : IK_Direct,
                              IC_CStyleCast, StartLoc, TypeRange.getBegin(),
                              TypeRange.getEnd());
  }

  /// Create a direct initialization for a functional cast.
  static InitializationKind CreateFunctionalCast(SourceRange TypeRange,
                                                 bool InitList) {
    return InitializationKind(InitList ? IK_DirectList : IK_Direct,
                              IC_FunctionalCast, TypeRange.getBegin(),
                              TypeRange.getBegin(), TypeRange.getEnd());
  }

  /// Create a copy initialization.
  static InitializationKind CreateCopy(SourceLocation InitLoc,
                                       SourceLocation EqualLoc,
                                       bool AllowExplicitConvs = false) {
    return InitializationKind(IK_Copy,
                              AllowExplicitConvs? IC_ExplicitConvs : IC_Normal,
                              InitLoc, EqualLoc, EqualLoc);
  }

  /// Create a default initialization.
  static InitializationKind CreateDefault(SourceLocation InitLoc) {
    return InitializationKind(IK_Default, IC_Normal, InitLoc, InitLoc, InitLoc);
  }

  /// Create a value initialization.
  static InitializationKind CreateValue(SourceLocation InitLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation RParenLoc,
                                        bool isImplicit = false) {
    return InitializationKind(IK_Value, isImplicit ? IC_Implicit : IC_Normal,
                              InitLoc, LParenLoc, RParenLoc);
  }

  /// Create an initialization from an initializer (which, for direct
  /// initialization from a parenthesized list, will be a ParenListExpr).
  static InitializationKind CreateForInit(SourceLocation Loc, bool DirectInit,
                                          Expr *Init) {
    if (!Init) return CreateDefault(Loc);
    if (!DirectInit)
      return CreateCopy(Loc, Init->getBeginLoc());
    if (isa<InitListExpr>(Init))
      return CreateDirectList(Loc, Init->getBeginLoc(), Init->getEndLoc());
    return CreateDirect(Loc, Init->getBeginLoc(), Init->getEndLoc());
  }

  /// Determine the initialization kind.
  InitKind getKind() const {
    return Kind;
  }

  /// Determine whether this initialization is an explicit cast.
  bool isExplicitCast() const {
    return Context >= IC_StaticCast;
  }

  /// Determine whether this initialization is a static cast.
  bool isStaticCast() const { return Context == IC_StaticCast; }

  /// Determine whether this initialization is a C-style cast.
  bool isCStyleOrFunctionalCast() const {
    return Context >= IC_CStyleCast;
  }

  /// Determine whether this is a C-style cast.
  bool isCStyleCast() const {
    return Context == IC_CStyleCast;
  }

  /// Determine whether this is a functional-style cast.
  bool isFunctionalCast() const {
    return Context == IC_FunctionalCast;
  }

  /// Determine whether this initialization is an implicit
  /// value-initialization, e.g., as occurs during aggregate
  /// initialization.
  bool isImplicitValueInit() const { return Context == IC_Implicit; }

  /// Retrieve the location at which initialization is occurring.
  SourceLocation getLocation() const { return Locations[0]; }

  /// Retrieve the source range that covers the initialization.
  SourceRange getRange() const {
    return SourceRange(Locations[0], Locations[2]);
  }

  /// Retrieve the location of the equal sign for copy initialization
  /// (if present).
  SourceLocation getEqualLoc() const {
    assert(Kind == IK_Copy && "Only copy initialization has an '='");
    return Locations[1];
  }

  bool isCopyInit() const { return Kind == IK_Copy; }

  /// Retrieve whether this initialization allows the use of explicit
  ///        constructors.
  bool AllowExplicit() const { return !isCopyInit(); }

  /// Retrieve whether this initialization allows the use of explicit
  /// conversion functions when binding a reference. If the reference is the
  /// first parameter in a copy or move constructor, such conversions are
  /// permitted even though we are performing copy-initialization.
  bool allowExplicitConversionFunctionsInRefBinding() const {
    return !isCopyInit() || Context == IC_ExplicitConvs;
  }

  /// Determine whether this initialization has a source range containing the
  /// locations of open and closing parentheses or braces.
  bool hasParenOrBraceRange() const {
    return Kind == IK_Direct || Kind == IK_Value || Kind == IK_DirectList;
  }

  /// Retrieve the source range containing the locations of the open
  /// and closing parentheses or braces for value, direct, and direct list
  /// initializations.
  SourceRange getParenOrBraceRange() const {
    assert(hasParenOrBraceRange() && "Only direct, value, and direct-list "
                                     "initialization have parentheses or "
                                     "braces");
    return SourceRange(Locations[1], Locations[2]);
  }
};

/// Describes the sequence of initializations required to initialize
/// a given object or reference with a set of arguments.
class InitializationSequence {
public:
  /// Describes the kind of initialization sequence computed.
  enum SequenceKind {
    /// A failed initialization sequence. The failure kind tells what
    /// happened.
    FailedSequence = 0,

    /// A dependent initialization, which could not be
    /// type-checked due to the presence of dependent types or
    /// dependently-typed expressions.
    DependentSequence,

    /// A normal sequence.
    NormalSequence
  };

  /// Describes the kind of a particular step in an initialization
  /// sequence.
  enum StepKind {
    /// Resolve the address of an overloaded function to a specific
    /// function declaration.
    SK_ResolveAddressOfOverloadedFunction,

    /// Perform a derived-to-base cast, producing an rvalue.
    SK_CastDerivedToBasePRValue,

    /// Perform a derived-to-base cast, producing an xvalue.
    SK_CastDerivedToBaseXValue,

    /// Perform a derived-to-base cast, producing an lvalue.
    SK_CastDerivedToBaseLValue,

    /// Reference binding to an lvalue.
    SK_BindReference,

    /// Reference binding to a temporary.
    SK_BindReferenceToTemporary,

    /// An optional copy of a temporary object to another
    /// temporary object, which is permitted (but not required) by
    /// C++98/03 but not C++0x.
    SK_ExtraneousCopyToTemporary,

    /// Direct-initialization from a reference-related object in the
    /// final stage of class copy-initialization.
    SK_FinalCopy,

    /// Perform a user-defined conversion, either via a conversion
    /// function or via a constructor.
    SK_UserConversion,

    /// Perform a qualification conversion, producing a prvalue.
    SK_QualificationConversionPRValue,

    /// Perform a qualification conversion, producing an xvalue.
    SK_QualificationConversionXValue,

    /// Perform a qualification conversion, producing an lvalue.
    SK_QualificationConversionLValue,

    /// Perform a function reference conversion, see [dcl.init.ref]p4.
    SK_FunctionReferenceConversion,

    /// Perform a conversion adding _Atomic to a type.
    SK_AtomicConversion,

    /// Perform an implicit conversion sequence.
    SK_ConversionSequence,

    /// Perform an implicit conversion sequence without narrowing.
    SK_ConversionSequenceNoNarrowing,

    /// Perform list-initialization without a constructor.
    SK_ListInitialization,

    /// Unwrap the single-element initializer list for a reference.
    SK_UnwrapInitList,

    /// Rewrap the single-element initializer list for a reference.
    SK_RewrapInitList,

    /// Perform initialization via a constructor.
    SK_ConstructorInitialization,

    /// Perform initialization via a constructor, taking arguments from
    /// a single InitListExpr.
    SK_ConstructorInitializationFromList,

    /// Zero-initialize the object
    SK_ZeroInitialization,

    /// C assignment
    SK_CAssignment,

    /// Initialization by string
    SK_StringInit,

    /// An initialization that "converts" an Objective-C object
    /// (not a point to an object) to another Objective-C object type.
    SK_ObjCObjectConversion,

    /// Array indexing for initialization by elementwise copy.
    SK_ArrayLoopIndex,

    /// Array initialization by elementwise copy.
    SK_ArrayLoopInit,

    /// Array initialization (from an array rvalue).
    SK_ArrayInit,

    /// Array initialization (from an array rvalue) as a GNU extension.
    SK_GNUArrayInit,

    /// Array initialization from a parenthesized initializer list.
    /// This is a GNU C++ extension.
    SK_ParenthesizedArrayInit,

    /// Pass an object by indirect copy-and-restore.
    SK_PassByIndirectCopyRestore,

    /// Pass an object by indirect restore.
    SK_PassByIndirectRestore,

    /// Produce an Objective-C object pointer.
    SK_ProduceObjCObject,

    /// Construct a std::initializer_list from an initializer list.
    SK_StdInitializerList,

    /// Perform initialization via a constructor taking a single
    /// std::initializer_list argument.
    SK_StdInitializerListConstructorCall,

    /// Initialize an OpenCL sampler from an integer.
    SK_OCLSamplerInit,

    /// Initialize an opaque OpenCL type (event_t, queue_t, etc.) with zero
    SK_OCLZeroOpaqueType,

    /// Initialize an aggreagate with parenthesized list of values.
    /// This is a C++20 feature.
    SK_ParenthesizedListInit
  };

  /// A single step in the initialization sequence.
  class Step {
  public:
    /// The kind of conversion or initialization step we are taking.
    StepKind Kind;

    // The type that results from this initialization.
    QualType Type;

    struct F {
      bool HadMultipleCandidates;
      FunctionDecl *Function;
      DeclAccessPair FoundDecl;
    };

    union {
      /// When Kind == SK_ResolvedOverloadedFunction or Kind ==
      /// SK_UserConversion, the function that the expression should be
      /// resolved to or the conversion function to call, respectively.
      /// When Kind == SK_ConstructorInitialization or SK_ListConstruction,
      /// the constructor to be called.
      ///
      /// Always a FunctionDecl, plus a Boolean flag telling if it was
      /// selected from an overloaded set having size greater than 1.
      /// For conversion decls, the naming class is the source type.
      /// For construct decls, the naming class is the target type.
      struct F Function;

      /// When Kind = SK_ConversionSequence, the implicit conversion
      /// sequence.
      ImplicitConversionSequence *ICS;

      /// When Kind = SK_RewrapInitList, the syntactic form of the
      /// wrapping list.
      InitListExpr *WrappingSyntacticList;
    };

    void Destroy();
  };

private:
  /// The kind of initialization sequence computed.
  enum SequenceKind SequenceKind;

  /// Steps taken by this initialization.
  SmallVector<Step, 4> Steps;

public:
  /// Describes why initialization failed.
  enum FailureKind {
    /// Too many initializers provided for a reference.
    FK_TooManyInitsForReference,

    /// Reference initialized from a parenthesized initializer list.
    FK_ParenthesizedListInitForReference,

    /// Array must be initialized with an initializer list.
    FK_ArrayNeedsInitList,

    /// Array must be initialized with an initializer list or a
    /// string literal.
    FK_ArrayNeedsInitListOrStringLiteral,

    /// Array must be initialized with an initializer list or a
    /// wide string literal.
    FK_ArrayNeedsInitListOrWideStringLiteral,

    /// Initializing a wide char array with narrow string literal.
    FK_NarrowStringIntoWideCharArray,

    /// Initializing char array with wide string literal.
    FK_WideStringIntoCharArray,

    /// Initializing wide char array with incompatible wide string
    /// literal.
    FK_IncompatWideStringIntoWideChar,

    /// Initializing char8_t array with plain string literal.
    FK_PlainStringIntoUTF8Char,

    /// Initializing char array with UTF-8 string literal.
    FK_UTF8StringIntoPlainChar,

    /// Array type mismatch.
    FK_ArrayTypeMismatch,

    /// Non-constant array initializer
    FK_NonConstantArrayInit,

    /// Cannot resolve the address of an overloaded function.
    FK_AddressOfOverloadFailed,

    /// Overloading due to reference initialization failed.
    FK_ReferenceInitOverloadFailed,

    /// Non-const lvalue reference binding to a temporary.
    FK_NonConstLValueReferenceBindingToTemporary,

    /// Non-const lvalue reference binding to a bit-field.
    FK_NonConstLValueReferenceBindingToBitfield,

    /// Non-const lvalue reference binding to a vector element.
    FK_NonConstLValueReferenceBindingToVectorElement,

    /// Non-const lvalue reference binding to a matrix element.
    FK_NonConstLValueReferenceBindingToMatrixElement,

    /// Non-const lvalue reference binding to an lvalue of unrelated
    /// type.
    FK_NonConstLValueReferenceBindingToUnrelated,

    /// Rvalue reference binding to an lvalue.
    FK_RValueReferenceBindingToLValue,

    /// Reference binding drops qualifiers.
    FK_ReferenceInitDropsQualifiers,

    /// Reference with mismatching address space binding to temporary.
    FK_ReferenceAddrspaceMismatchTemporary,

    /// Reference binding failed.
    FK_ReferenceInitFailed,

    /// Implicit conversion failed.
    FK_ConversionFailed,

    /// Implicit conversion failed.
    FK_ConversionFromPropertyFailed,

    /// Too many initializers for scalar
    FK_TooManyInitsForScalar,

    /// Scalar initialized from a parenthesized initializer list.
    FK_ParenthesizedListInitForScalar,

    /// Reference initialization from an initializer list
    FK_ReferenceBindingToInitList,

    /// Initialization of some unused destination type with an
    /// initializer list.
    FK_InitListBadDestinationType,

    /// Overloading for a user-defined conversion failed.
    FK_UserConversionOverloadFailed,

    /// Overloading for initialization by constructor failed.
    FK_ConstructorOverloadFailed,

    /// Overloading for list-initialization by constructor failed.
    FK_ListConstructorOverloadFailed,

    /// Default-initialization of a 'const' object.
    FK_DefaultInitOfConst,

    /// Initialization of an incomplete type.
    FK_Incomplete,

    /// Variable-length array must not have an initializer.
    FK_VariableLengthArrayHasInitializer,

    /// List initialization failed at some point.
    FK_ListInitializationFailed,

    /// Initializer has a placeholder type which cannot be
    /// resolved by initialization.
    FK_PlaceholderType,

    /// Trying to take the address of a function that doesn't support
    /// having its address taken.
    FK_AddressOfUnaddressableFunction,

    /// List-copy-initialization chose an explicit constructor.
    FK_ExplicitConstructor,

    /// Parenthesized list initialization failed at some point.
    /// This is a C++20 feature.
    FK_ParenthesizedListInitFailed,

    // A designated initializer was provided for a non-aggregate type.
    FK_DesignatedInitForNonAggregate,
  };

private:
  /// The reason why initialization failed.
  FailureKind Failure;

  /// The failed result of overload resolution.
  OverloadingResult FailedOverloadResult;

  /// The candidate set created when initialization failed.
  OverloadCandidateSet FailedCandidateSet;

  /// The incomplete type that caused a failure.
  QualType FailedIncompleteType;

  /// The fixit that needs to be applied to make this initialization
  /// succeed.
  std::string ZeroInitializationFixit;
  SourceLocation ZeroInitializationFixitLoc;

public:
  /// Call for initializations are invalid but that would be valid
  /// zero initialzations if Fixit was applied.
  void SetZeroInitializationFixit(const std::string& Fixit, SourceLocation L) {
    ZeroInitializationFixit = Fixit;
    ZeroInitializationFixitLoc = L;
  }

private:
  /// Prints a follow-up note that highlights the location of
  /// the initialized entity, if it's remote.
  void PrintInitLocationNote(Sema &S, const InitializedEntity &Entity);

public:
  /// Try to perform initialization of the given entity, creating a
  /// record of the steps required to perform the initialization.
  ///
  /// The generated initialization sequence will either contain enough
  /// information to diagnose
  ///
  /// \param S the semantic analysis object.
  ///
  /// \param Entity the entity being initialized.
  ///
  /// \param Kind the kind of initialization being performed.
  ///
  /// \param Args the argument(s) provided for initialization.
  ///
  /// \param TopLevelOfInitList true if we are initializing from an expression
  ///        at the top level inside an initializer list. This disallows
  ///        narrowing conversions in C++11 onwards.
  /// \param TreatUnavailableAsInvalid true if we want to treat unavailable
  ///        as invalid.
  InitializationSequence(Sema &S,
                         const InitializedEntity &Entity,
                         const InitializationKind &Kind,
                         MultiExprArg Args,
                         bool TopLevelOfInitList = false,
                         bool TreatUnavailableAsInvalid = true);
  void InitializeFrom(Sema &S, const InitializedEntity &Entity,
                      const InitializationKind &Kind, MultiExprArg Args,
                      bool TopLevelOfInitList, bool TreatUnavailableAsInvalid);

  ~InitializationSequence();

  /// Perform the actual initialization of the given entity based on
  /// the computed initialization sequence.
  ///
  /// \param S the semantic analysis object.
  ///
  /// \param Entity the entity being initialized.
  ///
  /// \param Kind the kind of initialization being performed.
  ///
  /// \param Args the argument(s) provided for initialization, ownership of
  /// which is transferred into the routine.
  ///
  /// \param ResultType if non-NULL, will be set to the type of the
  /// initialized object, which is the type of the declaration in most
  /// cases. However, when the initialized object is a variable of
  /// incomplete array type and the initializer is an initializer
  /// list, this type will be set to the completed array type.
  ///
  /// \returns an expression that performs the actual object initialization, if
  /// the initialization is well-formed. Otherwise, emits diagnostics
  /// and returns an invalid expression.
  ExprResult Perform(Sema &S,
                     const InitializedEntity &Entity,
                     const InitializationKind &Kind,
                     MultiExprArg Args,
                     QualType *ResultType = nullptr);

  /// Diagnose an potentially-invalid initialization sequence.
  ///
  /// \returns true if the initialization sequence was ill-formed,
  /// false otherwise.
  bool Diagnose(Sema &S,
                const InitializedEntity &Entity,
                const InitializationKind &Kind,
                ArrayRef<Expr *> Args);

  /// Determine the kind of initialization sequence computed.
  enum SequenceKind getKind() const { return SequenceKind; }

  /// Set the kind of sequence computed.
  void setSequenceKind(enum SequenceKind SK) { SequenceKind = SK; }

  /// Determine whether the initialization sequence is valid.
  explicit operator bool() const { return !Failed(); }

  /// Determine whether the initialization sequence is invalid.
  bool Failed() const { return SequenceKind == FailedSequence; }

  using step_iterator = SmallVectorImpl<Step>::const_iterator;

  step_iterator step_begin() const { return Steps.begin(); }
  step_iterator step_end()   const { return Steps.end(); }

  using step_range = llvm::iterator_range<step_iterator>;

  step_range steps() const { return {step_begin(), step_end()}; }

  /// Determine whether this initialization is a direct reference
  /// binding (C++ [dcl.init.ref]).
  bool isDirectReferenceBinding() const;

  /// Determine whether this initialization failed due to an ambiguity.
  bool isAmbiguous() const;

  /// Determine whether this initialization is direct call to a
  /// constructor.
  bool isConstructorInitialization() const;

  /// Add a new step in the initialization that resolves the address
  /// of an overloaded function to a specific function declaration.
  ///
  /// \param Function the function to which the overloaded function reference
  /// resolves.
  void AddAddressOverloadResolutionStep(FunctionDecl *Function,
                                        DeclAccessPair Found,
                                        bool HadMultipleCandidates);

  /// Add a new step in the initialization that performs a derived-to-
  /// base cast.
  ///
  /// \param BaseType the base type to which we will be casting.
  ///
  /// \param Category Indicates whether the result will be treated as an
  /// rvalue, an xvalue, or an lvalue.
  void AddDerivedToBaseCastStep(QualType BaseType,
                                ExprValueKind Category);

  /// Add a new step binding a reference to an object.
  ///
  /// \param BindingTemporary True if we are binding a reference to a temporary
  /// object (thereby extending its lifetime); false if we are binding to an
  /// lvalue or an lvalue treated as an rvalue.
  void AddReferenceBindingStep(QualType T, bool BindingTemporary);

  /// Add a new step that makes an extraneous copy of the input
  /// to a temporary of the same class type.
  ///
  /// This extraneous copy only occurs during reference binding in
  /// C++98/03, where we are permitted (but not required) to introduce
  /// an extra copy. At a bare minimum, we must check that we could
  /// call the copy constructor, and produce a diagnostic if the copy
  /// constructor is inaccessible or no copy constructor matches.
  //
  /// \param T The type of the temporary being created.
  void AddExtraneousCopyToTemporary(QualType T);

  /// Add a new step that makes a copy of the input to an object of
  /// the given type, as the final step in class copy-initialization.
  void AddFinalCopy(QualType T);

  /// Add a new step invoking a conversion function, which is either
  /// a constructor or a conversion function.
  void AddUserConversionStep(FunctionDecl *Function,
                             DeclAccessPair FoundDecl,
                             QualType T,
                             bool HadMultipleCandidates);

  /// Add a new step that performs a qualification conversion to the
  /// given type.
  void AddQualificationConversionStep(QualType Ty,
                                     ExprValueKind Category);

  /// Add a new step that performs a function reference conversion to the
  /// given type.
  void AddFunctionReferenceConversionStep(QualType Ty);

  /// Add a new step that performs conversion from non-atomic to atomic
  /// type.
  void AddAtomicConversionStep(QualType Ty);

  /// Add a new step that applies an implicit conversion sequence.
  void AddConversionSequenceStep(const ImplicitConversionSequence &ICS,
                                 QualType T, bool TopLevelOfInitList = false);

  /// Add a list-initialization step.
  void AddListInitializationStep(QualType T);

  /// Add a constructor-initialization step.
  ///
  /// \param FromInitList The constructor call is syntactically an initializer
  /// list.
  /// \param AsInitList The constructor is called as an init list constructor.
  void AddConstructorInitializationStep(DeclAccessPair FoundDecl,
                                        CXXConstructorDecl *Constructor,
                                        QualType T,
                                        bool HadMultipleCandidates,
                                        bool FromInitList, bool AsInitList);

  /// Add a zero-initialization step.
  void AddZeroInitializationStep(QualType T);

  /// Add a C assignment step.
  //
  // FIXME: It isn't clear whether this should ever be needed;
  // ideally, we would handle everything needed in C in the common
  // path. However, that isn't the case yet.
  void AddCAssignmentStep(QualType T);

  /// Add a string init step.
  void AddStringInitStep(QualType T);

  /// Add an Objective-C object conversion step, which is
  /// always a no-op.
  void AddObjCObjectConversionStep(QualType T);

  /// Add an array initialization loop step.
  void AddArrayInitLoopStep(QualType T, QualType EltTy);

  /// Add an array initialization step.
  void AddArrayInitStep(QualType T, bool IsGNUExtension);

  /// Add a parenthesized array initialization step.
  void AddParenthesizedArrayInitStep(QualType T);

  /// Add a step to pass an object by indirect copy-restore.
  void AddPassByIndirectCopyRestoreStep(QualType T, bool shouldCopy);

  /// Add a step to "produce" an Objective-C object (by
  /// retaining it).
  void AddProduceObjCObjectStep(QualType T);

  /// Add a step to construct a std::initializer_list object from an
  /// initializer list.
  void AddStdInitializerListConstructionStep(QualType T);

  /// Add a step to initialize an OpenCL sampler from an integer
  /// constant.
  void AddOCLSamplerInitStep(QualType T);

  /// Add a step to initialzie an OpenCL opaque type (event_t, queue_t, etc.)
  /// from a zero constant.
  void AddOCLZeroOpaqueTypeStep(QualType T);

  void AddParenthesizedListInitStep(QualType T);

  /// Add steps to unwrap a initializer list for a reference around a
  /// single element and rewrap it at the end.
  void RewrapReferenceInitList(QualType T, InitListExpr *Syntactic);

  /// Note that this initialization sequence failed.
  void SetFailed(FailureKind Failure) {
    SequenceKind = FailedSequence;
    this->Failure = Failure;
    assert((Failure != FK_Incomplete || !FailedIncompleteType.isNull()) &&
           "Incomplete type failure requires a type!");
  }

  /// Note that this initialization sequence failed due to failed
  /// overload resolution.
  void SetOverloadFailure(FailureKind Failure, OverloadingResult Result);

  /// Retrieve a reference to the candidate set when overload
  /// resolution fails.
  OverloadCandidateSet &getFailedCandidateSet() {
    return FailedCandidateSet;
  }

  /// Get the overloading result, for when the initialization
  /// sequence failed due to a bad overload.
  OverloadingResult getFailedOverloadResult() const {
    return FailedOverloadResult;
  }

  /// Note that this initialization sequence failed due to an
  /// incomplete type.
  void setIncompleteTypeFailure(QualType IncompleteType) {
    FailedIncompleteType = IncompleteType;
    SetFailed(FK_Incomplete);
  }

  /// Determine why initialization failed.
  FailureKind getFailureKind() const {
    assert(Failed() && "Not an initialization failure!");
    return Failure;
  }

  /// Dump a representation of this initialization sequence to
  /// the given stream, for debugging purposes.
  void dump(raw_ostream &OS) const;

  /// Dump a representation of this initialization sequence to
  /// standard error, for debugging purposes.
  void dump() const;
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_INITIALIZATION_H
