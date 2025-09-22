//===--- Expr.h - Classes for representing expressions ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPR_H
#define LLVM_CLANG_AST_EXPR_H

#include "clang/AST/APNumericStorage.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTVector.h"
#include "clang/AST/ComputeDependence.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SyncScope.h"
#include "clang/Basic/TypeTraits.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <optional>

namespace clang {
  class APValue;
  class ASTContext;
  class BlockDecl;
  class CXXBaseSpecifier;
  class CXXMemberCallExpr;
  class CXXOperatorCallExpr;
  class CastExpr;
  class Decl;
  class IdentifierInfo;
  class MaterializeTemporaryExpr;
  class NamedDecl;
  class ObjCPropertyRefExpr;
  class OpaqueValueExpr;
  class ParmVarDecl;
  class StringLiteral;
  class TargetInfo;
  class ValueDecl;

/// A simple array of base specifiers.
typedef SmallVector<CXXBaseSpecifier*, 4> CXXCastPath;

/// An adjustment to be made to the temporary created when emitting a
/// reference binding, which accesses a particular subobject of that temporary.
struct SubobjectAdjustment {
  enum {
    DerivedToBaseAdjustment,
    FieldAdjustment,
    MemberPointerAdjustment
  } Kind;

  struct DTB {
    const CastExpr *BasePath;
    const CXXRecordDecl *DerivedClass;
  };

  struct P {
    const MemberPointerType *MPT;
    Expr *RHS;
  };

  union {
    struct DTB DerivedToBase;
    const FieldDecl *Field;
    struct P Ptr;
  };

  SubobjectAdjustment(const CastExpr *BasePath,
                      const CXXRecordDecl *DerivedClass)
    : Kind(DerivedToBaseAdjustment) {
    DerivedToBase.BasePath = BasePath;
    DerivedToBase.DerivedClass = DerivedClass;
  }

  SubobjectAdjustment(const FieldDecl *Field) : Kind(FieldAdjustment) {
    this->Field = Field;
  }

  SubobjectAdjustment(const MemberPointerType *MPT, Expr *RHS)
    : Kind(MemberPointerAdjustment) {
    this->Ptr.MPT = MPT;
    this->Ptr.RHS = RHS;
  }
};

/// This represents one expression.  Note that Expr's are subclasses of Stmt.
/// This allows an expression to be transparently used any place a Stmt is
/// required.
class Expr : public ValueStmt {
  QualType TR;

public:
  Expr() = delete;
  Expr(const Expr&) = delete;
  Expr(Expr &&) = delete;
  Expr &operator=(const Expr&) = delete;
  Expr &operator=(Expr&&) = delete;

protected:
  Expr(StmtClass SC, QualType T, ExprValueKind VK, ExprObjectKind OK)
      : ValueStmt(SC) {
    ExprBits.Dependent = 0;
    ExprBits.ValueKind = VK;
    ExprBits.ObjectKind = OK;
    assert(ExprBits.ObjectKind == OK && "truncated kind");
    setType(T);
  }

  /// Construct an empty expression.
  explicit Expr(StmtClass SC, EmptyShell) : ValueStmt(SC) { }

  /// Each concrete expr subclass is expected to compute its dependence and call
  /// this in the constructor.
  void setDependence(ExprDependence Deps) {
    ExprBits.Dependent = static_cast<unsigned>(Deps);
  }
  friend class ASTImporter;   // Sets dependence directly.
  friend class ASTStmtReader; // Sets dependence directly.

public:
  QualType getType() const { return TR; }
  void setType(QualType t) {
    // In C++, the type of an expression is always adjusted so that it
    // will not have reference type (C++ [expr]p6). Use
    // QualType::getNonReferenceType() to retrieve the non-reference
    // type. Additionally, inspect Expr::isLvalue to determine whether
    // an expression that is adjusted in this manner should be
    // considered an lvalue.
    assert((t.isNull() || !t->isReferenceType()) &&
           "Expressions can't have reference type");

    TR = t;
  }

  /// If this expression is an enumeration constant, return the
  /// enumeration type under which said constant was declared.
  /// Otherwise return the expression's type.
  /// Note this effectively circumvents the weak typing of C's enum constants
  QualType getEnumCoercedType(const ASTContext &Ctx) const;

  ExprDependence getDependence() const {
    return static_cast<ExprDependence>(ExprBits.Dependent);
  }

  /// Determines whether the value of this expression depends on
  ///   - a template parameter (C++ [temp.dep.constexpr])
  ///   - or an error, whose resolution is unknown
  ///
  /// For example, the array bound of "Chars" in the following example is
  /// value-dependent.
  /// @code
  /// template<int Size, char (&Chars)[Size]> struct meta_string;
  /// @endcode
  bool isValueDependent() const {
    return static_cast<bool>(getDependence() & ExprDependence::Value);
  }

  /// Determines whether the type of this expression depends on
  ///   - a template parameter (C++ [temp.dep.expr], which means that its type
  ///     could change from one template instantiation to the next)
  ///   - or an error
  ///
  /// For example, the expressions "x" and "x + y" are type-dependent in
  /// the following code, but "y" is not type-dependent:
  /// @code
  /// template<typename T>
  /// void add(T x, int y) {
  ///   x + y;
  /// }
  /// @endcode
  bool isTypeDependent() const {
    return static_cast<bool>(getDependence() & ExprDependence::Type);
  }

  /// Whether this expression is instantiation-dependent, meaning that
  /// it depends in some way on
  ///    - a template parameter (even if neither its type nor (constant) value
  ///      can change due to the template instantiation)
  ///    - or an error
  ///
  /// In the following example, the expression \c sizeof(sizeof(T() + T())) is
  /// instantiation-dependent (since it involves a template parameter \c T), but
  /// is neither type- nor value-dependent, since the type of the inner
  /// \c sizeof is known (\c std::size_t) and therefore the size of the outer
  /// \c sizeof is known.
  ///
  /// \code
  /// template<typename T>
  /// void f(T x, T y) {
  ///   sizeof(sizeof(T() + T());
  /// }
  /// \endcode
  ///
  /// \code
  /// void func(int) {
  ///   func(); // the expression is instantiation-dependent, because it depends
  ///           // on an error.
  /// }
  /// \endcode
  bool isInstantiationDependent() const {
    return static_cast<bool>(getDependence() & ExprDependence::Instantiation);
  }

  /// Whether this expression contains an unexpanded parameter
  /// pack (for C++11 variadic templates).
  ///
  /// Given the following function template:
  ///
  /// \code
  /// template<typename F, typename ...Types>
  /// void forward(const F &f, Types &&...args) {
  ///   f(static_cast<Types&&>(args)...);
  /// }
  /// \endcode
  ///
  /// The expressions \c args and \c static_cast<Types&&>(args) both
  /// contain parameter packs.
  bool containsUnexpandedParameterPack() const {
    return static_cast<bool>(getDependence() & ExprDependence::UnexpandedPack);
  }

  /// Whether this expression contains subexpressions which had errors, e.g. a
  /// TypoExpr.
  bool containsErrors() const {
    return static_cast<bool>(getDependence() & ExprDependence::Error);
  }

  /// getExprLoc - Return the preferred location for the arrow when diagnosing
  /// a problem with a generic expression.
  SourceLocation getExprLoc() const LLVM_READONLY;

  /// Determine whether an lvalue-to-rvalue conversion should implicitly be
  /// applied to this expression if it appears as a discarded-value expression
  /// in C++11 onwards. This applies to certain forms of volatile glvalues.
  bool isReadIfDiscardedInCPlusPlus11() const;

  /// isUnusedResultAWarning - Return true if this immediate expression should
  /// be warned about if the result is unused.  If so, fill in expr, location,
  /// and ranges with expr to warn on and source locations/ranges appropriate
  /// for a warning.
  bool isUnusedResultAWarning(const Expr *&WarnExpr, SourceLocation &Loc,
                              SourceRange &R1, SourceRange &R2,
                              ASTContext &Ctx) const;

  /// isLValue - True if this expression is an "l-value" according to
  /// the rules of the current language.  C and C++ give somewhat
  /// different rules for this concept, but in general, the result of
  /// an l-value expression identifies a specific object whereas the
  /// result of an r-value expression is a value detached from any
  /// specific storage.
  ///
  /// C++11 divides the concept of "r-value" into pure r-values
  /// ("pr-values") and so-called expiring values ("x-values"), which
  /// identify specific objects that can be safely cannibalized for
  /// their resources.
  bool isLValue() const { return getValueKind() == VK_LValue; }
  bool isPRValue() const { return getValueKind() == VK_PRValue; }
  bool isXValue() const { return getValueKind() == VK_XValue; }
  bool isGLValue() const { return getValueKind() != VK_PRValue; }

  enum LValueClassification {
    LV_Valid,
    LV_NotObjectType,
    LV_IncompleteVoidType,
    LV_DuplicateVectorComponents,
    LV_InvalidExpression,
    LV_InvalidMessageExpression,
    LV_MemberFunction,
    LV_SubObjCPropertySetting,
    LV_ClassTemporary,
    LV_ArrayTemporary
  };
  /// Reasons why an expression might not be an l-value.
  LValueClassification ClassifyLValue(ASTContext &Ctx) const;

  enum isModifiableLvalueResult {
    MLV_Valid,
    MLV_NotObjectType,
    MLV_IncompleteVoidType,
    MLV_DuplicateVectorComponents,
    MLV_InvalidExpression,
    MLV_LValueCast,           // Specialized form of MLV_InvalidExpression.
    MLV_IncompleteType,
    MLV_ConstQualified,
    MLV_ConstQualifiedField,
    MLV_ConstAddrSpace,
    MLV_ArrayType,
    MLV_NoSetterProperty,
    MLV_MemberFunction,
    MLV_SubObjCPropertySetting,
    MLV_InvalidMessageExpression,
    MLV_ClassTemporary,
    MLV_ArrayTemporary
  };
  /// isModifiableLvalue - C99 6.3.2.1: an lvalue that does not have array type,
  /// does not have an incomplete type, does not have a const-qualified type,
  /// and if it is a structure or union, does not have any member (including,
  /// recursively, any member or element of all contained aggregates or unions)
  /// with a const-qualified type.
  ///
  /// \param Loc [in,out] - A source location which *may* be filled
  /// in with the location of the expression making this a
  /// non-modifiable lvalue, if specified.
  isModifiableLvalueResult
  isModifiableLvalue(ASTContext &Ctx, SourceLocation *Loc = nullptr) const;

  /// The return type of classify(). Represents the C++11 expression
  ///        taxonomy.
  class Classification {
  public:
    /// The various classification results. Most of these mean prvalue.
    enum Kinds {
      CL_LValue,
      CL_XValue,
      CL_Function, // Functions cannot be lvalues in C.
      CL_Void, // Void cannot be an lvalue in C.
      CL_AddressableVoid, // Void expression whose address can be taken in C.
      CL_DuplicateVectorComponents, // A vector shuffle with dupes.
      CL_MemberFunction, // An expression referring to a member function
      CL_SubObjCPropertySetting,
      CL_ClassTemporary, // A temporary of class type, or subobject thereof.
      CL_ArrayTemporary, // A temporary of array type.
      CL_ObjCMessageRValue, // ObjC message is an rvalue
      CL_PRValue // A prvalue for any other reason, of any other type
    };
    /// The results of modification testing.
    enum ModifiableType {
      CM_Untested, // testModifiable was false.
      CM_Modifiable,
      CM_RValue, // Not modifiable because it's an rvalue
      CM_Function, // Not modifiable because it's a function; C++ only
      CM_LValueCast, // Same as CM_RValue, but indicates GCC cast-as-lvalue ext
      CM_NoSetterProperty,// Implicit assignment to ObjC property without setter
      CM_ConstQualified,
      CM_ConstQualifiedField,
      CM_ConstAddrSpace,
      CM_ArrayType,
      CM_IncompleteType
    };

  private:
    friend class Expr;

    unsigned short Kind;
    unsigned short Modifiable;

    explicit Classification(Kinds k, ModifiableType m)
      : Kind(k), Modifiable(m)
    {}

  public:
    Classification() {}

    Kinds getKind() const { return static_cast<Kinds>(Kind); }
    ModifiableType getModifiable() const {
      assert(Modifiable != CM_Untested && "Did not test for modifiability.");
      return static_cast<ModifiableType>(Modifiable);
    }
    bool isLValue() const { return Kind == CL_LValue; }
    bool isXValue() const { return Kind == CL_XValue; }
    bool isGLValue() const { return Kind <= CL_XValue; }
    bool isPRValue() const { return Kind >= CL_Function; }
    bool isRValue() const { return Kind >= CL_XValue; }
    bool isModifiable() const { return getModifiable() == CM_Modifiable; }

    /// Create a simple, modifiable lvalue
    static Classification makeSimpleLValue() {
      return Classification(CL_LValue, CM_Modifiable);
    }

  };
  /// Classify - Classify this expression according to the C++11
  ///        expression taxonomy.
  ///
  /// C++11 defines ([basic.lval]) a new taxonomy of expressions to replace the
  /// old lvalue vs rvalue. This function determines the type of expression this
  /// is. There are three expression types:
  /// - lvalues are classical lvalues as in C++03.
  /// - prvalues are equivalent to rvalues in C++03.
  /// - xvalues are expressions yielding unnamed rvalue references, e.g. a
  ///   function returning an rvalue reference.
  /// lvalues and xvalues are collectively referred to as glvalues, while
  /// prvalues and xvalues together form rvalues.
  Classification Classify(ASTContext &Ctx) const {
    return ClassifyImpl(Ctx, nullptr);
  }

  /// ClassifyModifiable - Classify this expression according to the
  ///        C++11 expression taxonomy, and see if it is valid on the left side
  ///        of an assignment.
  ///
  /// This function extends classify in that it also tests whether the
  /// expression is modifiable (C99 6.3.2.1p1).
  /// \param Loc A source location that might be filled with a relevant location
  ///            if the expression is not modifiable.
  Classification ClassifyModifiable(ASTContext &Ctx, SourceLocation &Loc) const{
    return ClassifyImpl(Ctx, &Loc);
  }

  /// Returns the set of floating point options that apply to this expression.
  /// Only meaningful for operations on floating point values.
  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const;

  /// getValueKindForType - Given a formal return or parameter type,
  /// give its value kind.
  static ExprValueKind getValueKindForType(QualType T) {
    if (const ReferenceType *RT = T->getAs<ReferenceType>())
      return (isa<LValueReferenceType>(RT)
                ? VK_LValue
                : (RT->getPointeeType()->isFunctionType()
                     ? VK_LValue : VK_XValue));
    return VK_PRValue;
  }

  /// getValueKind - The value kind that this expression produces.
  ExprValueKind getValueKind() const {
    return static_cast<ExprValueKind>(ExprBits.ValueKind);
  }

  /// getObjectKind - The object kind that this expression produces.
  /// Object kinds are meaningful only for expressions that yield an
  /// l-value or x-value.
  ExprObjectKind getObjectKind() const {
    return static_cast<ExprObjectKind>(ExprBits.ObjectKind);
  }

  bool isOrdinaryOrBitFieldObject() const {
    ExprObjectKind OK = getObjectKind();
    return (OK == OK_Ordinary || OK == OK_BitField);
  }

  /// setValueKind - Set the value kind produced by this expression.
  void setValueKind(ExprValueKind Cat) { ExprBits.ValueKind = Cat; }

  /// setObjectKind - Set the object kind produced by this expression.
  void setObjectKind(ExprObjectKind Cat) { ExprBits.ObjectKind = Cat; }

private:
  Classification ClassifyImpl(ASTContext &Ctx, SourceLocation *Loc) const;

public:

  /// Returns true if this expression is a gl-value that
  /// potentially refers to a bit-field.
  ///
  /// In C++, whether a gl-value refers to a bitfield is essentially
  /// an aspect of the value-kind type system.
  bool refersToBitField() const { return getObjectKind() == OK_BitField; }

  /// If this expression refers to a bit-field, retrieve the
  /// declaration of that bit-field.
  ///
  /// Note that this returns a non-null pointer in subtly different
  /// places than refersToBitField returns true.  In particular, this can
  /// return a non-null pointer even for r-values loaded from
  /// bit-fields, but it will return null for a conditional bit-field.
  FieldDecl *getSourceBitField();

  /// If this expression refers to an enum constant, retrieve its declaration
  EnumConstantDecl *getEnumConstantDecl();

  const EnumConstantDecl *getEnumConstantDecl() const {
    return const_cast<Expr *>(this)->getEnumConstantDecl();
  }

  const FieldDecl *getSourceBitField() const {
    return const_cast<Expr*>(this)->getSourceBitField();
  }

  Decl *getReferencedDeclOfCallee();
  const Decl *getReferencedDeclOfCallee() const {
    return const_cast<Expr*>(this)->getReferencedDeclOfCallee();
  }

  /// If this expression is an l-value for an Objective C
  /// property, find the underlying property reference expression.
  const ObjCPropertyRefExpr *getObjCProperty() const;

  /// Check if this expression is the ObjC 'self' implicit parameter.
  bool isObjCSelfExpr() const;

  /// Returns whether this expression refers to a vector element.
  bool refersToVectorElement() const;

  /// Returns whether this expression refers to a matrix element.
  bool refersToMatrixElement() const {
    return getObjectKind() == OK_MatrixComponent;
  }

  /// Returns whether this expression refers to a global register
  /// variable.
  bool refersToGlobalRegisterVar() const;

  /// Returns whether this expression has a placeholder type.
  bool hasPlaceholderType() const {
    return getType()->isPlaceholderType();
  }

  /// Returns whether this expression has a specific placeholder type.
  bool hasPlaceholderType(BuiltinType::Kind K) const {
    assert(BuiltinType::isPlaceholderTypeKind(K));
    if (const BuiltinType *BT = dyn_cast<BuiltinType>(getType()))
      return BT->getKind() == K;
    return false;
  }

  /// isKnownToHaveBooleanValue - Return true if this is an integer expression
  /// that is known to return 0 or 1.  This happens for _Bool/bool expressions
  /// but also int expressions which are produced by things like comparisons in
  /// C.
  ///
  /// \param Semantic If true, only return true for expressions that are known
  /// to be semantically boolean, which might not be true even for expressions
  /// that are known to evaluate to 0/1. For instance, reading an unsigned
  /// bit-field with width '1' will evaluate to 0/1, but doesn't necessarily
  /// semantically correspond to a bool.
  bool isKnownToHaveBooleanValue(bool Semantic = true) const;

  /// Check whether this array fits the idiom of a flexible array member,
  /// depending on the value of -fstrict-flex-array.
  /// When IgnoreTemplateOrMacroSubstitution is set, it doesn't consider sizes
  /// resulting from the substitution of a macro or a template as special sizes.
  bool isFlexibleArrayMemberLike(
      ASTContext &Context,
      LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel,
      bool IgnoreTemplateOrMacroSubstitution = false) const;

  /// isIntegerConstantExpr - Return the value if this expression is a valid
  /// integer constant expression.  If not a valid i-c-e, return std::nullopt
  /// and fill in Loc (if specified) with the location of the invalid
  /// expression.
  ///
  /// Note: This does not perform the implicit conversions required by C++11
  /// [expr.const]p5.
  std::optional<llvm::APSInt>
  getIntegerConstantExpr(const ASTContext &Ctx,
                         SourceLocation *Loc = nullptr) const;
  bool isIntegerConstantExpr(const ASTContext &Ctx,
                             SourceLocation *Loc = nullptr) const;

  /// isCXX98IntegralConstantExpr - Return true if this expression is an
  /// integral constant expression in C++98. Can only be used in C++.
  bool isCXX98IntegralConstantExpr(const ASTContext &Ctx) const;

  /// isCXX11ConstantExpr - Return true if this expression is a constant
  /// expression in C++11. Can only be used in C++.
  ///
  /// Note: This does not perform the implicit conversions required by C++11
  /// [expr.const]p5.
  bool isCXX11ConstantExpr(const ASTContext &Ctx, APValue *Result = nullptr,
                           SourceLocation *Loc = nullptr) const;

  /// isPotentialConstantExpr - Return true if this function's definition
  /// might be usable in a constant expression in C++11, if it were marked
  /// constexpr. Return false if the function can never produce a constant
  /// expression, along with diagnostics describing why not.
  static bool isPotentialConstantExpr(const FunctionDecl *FD,
                                      SmallVectorImpl<
                                        PartialDiagnosticAt> &Diags);

  /// isPotentialConstantExprUnevaluated - Return true if this expression might
  /// be usable in a constant expression in C++11 in an unevaluated context, if
  /// it were in function FD marked constexpr. Return false if the function can
  /// never produce a constant expression, along with diagnostics describing
  /// why not.
  static bool isPotentialConstantExprUnevaluated(Expr *E,
                                                 const FunctionDecl *FD,
                                                 SmallVectorImpl<
                                                   PartialDiagnosticAt> &Diags);

  /// isConstantInitializer - Returns true if this expression can be emitted to
  /// IR as a constant, and thus can be used as a constant initializer in C.
  /// If this expression is not constant and Culprit is non-null,
  /// it is used to store the address of first non constant expr.
  bool isConstantInitializer(ASTContext &Ctx, bool ForRef,
                             const Expr **Culprit = nullptr) const;

  /// If this expression is an unambiguous reference to a single declaration,
  /// in the style of __builtin_function_start, return that declaration.  Note
  /// that this may return a non-static member function or field in C++ if this
  /// expression is a member pointer constant.
  const ValueDecl *getAsBuiltinConstantDeclRef(const ASTContext &Context) const;

  /// EvalStatus is a struct with detailed info about an evaluation in progress.
  struct EvalStatus {
    /// Whether the evaluated expression has side effects.
    /// For example, (f() && 0) can be folded, but it still has side effects.
    bool HasSideEffects = false;

    /// Whether the evaluation hit undefined behavior.
    /// For example, 1.0 / 0.0 can be folded to Inf, but has undefined behavior.
    /// Likewise, INT_MAX + 1 can be folded to INT_MIN, but has UB.
    bool HasUndefinedBehavior = false;

    /// Diag - If this is non-null, it will be filled in with a stack of notes
    /// indicating why evaluation failed (or why it failed to produce a constant
    /// expression).
    /// If the expression is unfoldable, the notes will indicate why it's not
    /// foldable. If the expression is foldable, but not a constant expression,
    /// the notes will describes why it isn't a constant expression. If the
    /// expression *is* a constant expression, no notes will be produced.
    ///
    /// FIXME: this causes significant performance concerns and should be
    /// refactored at some point. Not all evaluations of the constant
    /// expression interpreter will display the given diagnostics, this means
    /// those kinds of uses are paying the expense of generating a diagnostic
    /// (which may include expensive operations like converting APValue objects
    /// to a string representation).
    SmallVectorImpl<PartialDiagnosticAt> *Diag = nullptr;

    EvalStatus() = default;

    // hasSideEffects - Return true if the evaluated expression has
    // side effects.
    bool hasSideEffects() const {
      return HasSideEffects;
    }
  };

  /// EvalResult is a struct with detailed info about an evaluated expression.
  struct EvalResult : EvalStatus {
    /// Val - This is the value the expression can be folded to.
    APValue Val;

    // isGlobalLValue - Return true if the evaluated lvalue expression
    // is global.
    bool isGlobalLValue() const;
  };

  /// EvaluateAsRValue - Return true if this is a constant which we can fold to
  /// an rvalue using any crazy technique (that has nothing to do with language
  /// standards) that we want to, even if the expression has side-effects. If
  /// this function returns true, it returns the folded constant in Result. If
  /// the expression is a glvalue, an lvalue-to-rvalue conversion will be
  /// applied.
  bool EvaluateAsRValue(EvalResult &Result, const ASTContext &Ctx,
                        bool InConstantContext = false) const;

  /// EvaluateAsBooleanCondition - Return true if this is a constant
  /// which we can fold and convert to a boolean condition using
  /// any crazy technique that we want to, even if the expression has
  /// side-effects.
  bool EvaluateAsBooleanCondition(bool &Result, const ASTContext &Ctx,
                                  bool InConstantContext = false) const;

  enum SideEffectsKind {
    SE_NoSideEffects,          ///< Strictly evaluate the expression.
    SE_AllowUndefinedBehavior, ///< Allow UB that we can give a value, but not
                               ///< arbitrary unmodeled side effects.
    SE_AllowSideEffects        ///< Allow any unmodeled side effect.
  };

  /// EvaluateAsInt - Return true if this is a constant which we can fold and
  /// convert to an integer, using any crazy technique that we want to.
  bool EvaluateAsInt(EvalResult &Result, const ASTContext &Ctx,
                     SideEffectsKind AllowSideEffects = SE_NoSideEffects,
                     bool InConstantContext = false) const;

  /// EvaluateAsFloat - Return true if this is a constant which we can fold and
  /// convert to a floating point value, using any crazy technique that we
  /// want to.
  bool EvaluateAsFloat(llvm::APFloat &Result, const ASTContext &Ctx,
                       SideEffectsKind AllowSideEffects = SE_NoSideEffects,
                       bool InConstantContext = false) const;

  /// EvaluateAsFixedPoint - Return true if this is a constant which we can fold
  /// and convert to a fixed point value.
  bool EvaluateAsFixedPoint(EvalResult &Result, const ASTContext &Ctx,
                            SideEffectsKind AllowSideEffects = SE_NoSideEffects,
                            bool InConstantContext = false) const;

  /// isEvaluatable - Call EvaluateAsRValue to see if this expression can be
  /// constant folded without side-effects, but discard the result.
  bool isEvaluatable(const ASTContext &Ctx,
                     SideEffectsKind AllowSideEffects = SE_NoSideEffects) const;

  /// HasSideEffects - This routine returns true for all those expressions
  /// which have any effect other than producing a value. Example is a function
  /// call, volatile variable read, or throwing an exception. If
  /// IncludePossibleEffects is false, this call treats certain expressions with
  /// potential side effects (such as function call-like expressions,
  /// instantiation-dependent expressions, or invocations from a macro) as not
  /// having side effects.
  bool HasSideEffects(const ASTContext &Ctx,
                      bool IncludePossibleEffects = true) const;

  /// Determine whether this expression involves a call to any function
  /// that is not trivial.
  bool hasNonTrivialCall(const ASTContext &Ctx) const;

  /// EvaluateKnownConstInt - Call EvaluateAsRValue and return the folded
  /// integer. This must be called on an expression that constant folds to an
  /// integer.
  llvm::APSInt EvaluateKnownConstInt(
      const ASTContext &Ctx,
      SmallVectorImpl<PartialDiagnosticAt> *Diag = nullptr) const;

  llvm::APSInt EvaluateKnownConstIntCheckOverflow(
      const ASTContext &Ctx,
      SmallVectorImpl<PartialDiagnosticAt> *Diag = nullptr) const;

  void EvaluateForOverflow(const ASTContext &Ctx) const;

  /// EvaluateAsLValue - Evaluate an expression to see if we can fold it to an
  /// lvalue with link time known address, with no side-effects.
  bool EvaluateAsLValue(EvalResult &Result, const ASTContext &Ctx,
                        bool InConstantContext = false) const;

  /// EvaluateAsInitializer - Evaluate an expression as if it were the
  /// initializer of the given declaration. Returns true if the initializer
  /// can be folded to a constant, and produces any relevant notes. In C++11,
  /// notes will be produced if the expression is not a constant expression.
  bool EvaluateAsInitializer(APValue &Result, const ASTContext &Ctx,
                             const VarDecl *VD,
                             SmallVectorImpl<PartialDiagnosticAt> &Notes,
                             bool IsConstantInitializer) const;

  /// EvaluateWithSubstitution - Evaluate an expression as if from the context
  /// of a call to the given function with the given arguments, inside an
  /// unevaluated context. Returns true if the expression could be folded to a
  /// constant.
  bool EvaluateWithSubstitution(APValue &Value, ASTContext &Ctx,
                                const FunctionDecl *Callee,
                                ArrayRef<const Expr*> Args,
                                const Expr *This = nullptr) const;

  enum class ConstantExprKind {
    /// An integer constant expression (an array bound, enumerator, case value,
    /// bit-field width, or similar) or similar.
    Normal,
    /// A non-class template argument. Such a value is only used for mangling,
    /// not for code generation, so can refer to dllimported functions.
    NonClassTemplateArgument,
    /// A class template argument. Such a value is used for code generation.
    ClassTemplateArgument,
    /// An immediate invocation. The destruction of the end result of this
    /// evaluation is not part of the evaluation, but all other temporaries
    /// are destroyed.
    ImmediateInvocation,
  };

  /// Evaluate an expression that is required to be a constant expression. Does
  /// not check the syntactic constraints for C and C++98 constant expressions.
  bool EvaluateAsConstantExpr(
      EvalResult &Result, const ASTContext &Ctx,
      ConstantExprKind Kind = ConstantExprKind::Normal) const;

  /// If the current Expr is a pointer, this will try to statically
  /// determine the number of bytes available where the pointer is pointing.
  /// Returns true if all of the above holds and we were able to figure out the
  /// size, false otherwise.
  ///
  /// \param Type - How to evaluate the size of the Expr, as defined by the
  /// "type" parameter of __builtin_object_size
  bool tryEvaluateObjectSize(uint64_t &Result, ASTContext &Ctx,
                             unsigned Type) const;

  /// If the current Expr is a pointer, this will try to statically
  /// determine the strlen of the string pointed to.
  /// Returns true if all of the above holds and we were able to figure out the
  /// strlen, false otherwise.
  bool tryEvaluateStrLen(uint64_t &Result, ASTContext &Ctx) const;

  bool EvaluateCharRangeAsString(std::string &Result,
                                 const Expr *SizeExpression,
                                 const Expr *PtrExpression, ASTContext &Ctx,
                                 EvalResult &Status) const;

  /// If the current Expr can be evaluated to a pointer to a null-terminated
  /// constant string, return the constant string (without the terminating
  /// null).
  std::optional<std::string> tryEvaluateString(ASTContext &Ctx) const;

  /// Enumeration used to describe the kind of Null pointer constant
  /// returned from \c isNullPointerConstant().
  enum NullPointerConstantKind {
    /// Expression is not a Null pointer constant.
    NPCK_NotNull = 0,

    /// Expression is a Null pointer constant built from a zero integer
    /// expression that is not a simple, possibly parenthesized, zero literal.
    /// C++ Core Issue 903 will classify these expressions as "not pointers"
    /// once it is adopted.
    /// http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#903
    NPCK_ZeroExpression,

    /// Expression is a Null pointer constant built from a literal zero.
    NPCK_ZeroLiteral,

    /// Expression is a C++11 nullptr.
    NPCK_CXX11_nullptr,

    /// Expression is a GNU-style __null constant.
    NPCK_GNUNull
  };

  /// Enumeration used to describe how \c isNullPointerConstant()
  /// should cope with value-dependent expressions.
  enum NullPointerConstantValueDependence {
    /// Specifies that the expression should never be value-dependent.
    NPC_NeverValueDependent = 0,

    /// Specifies that a value-dependent expression of integral or
    /// dependent type should be considered a null pointer constant.
    NPC_ValueDependentIsNull,

    /// Specifies that a value-dependent expression should be considered
    /// to never be a null pointer constant.
    NPC_ValueDependentIsNotNull
  };

  /// isNullPointerConstant - C99 6.3.2.3p3 - Test if this reduces down to
  /// a Null pointer constant. The return value can further distinguish the
  /// kind of NULL pointer constant that was detected.
  NullPointerConstantKind isNullPointerConstant(
      ASTContext &Ctx,
      NullPointerConstantValueDependence NPC) const;

  /// isOBJCGCCandidate - Return true if this expression may be used in a read/
  /// write barrier.
  bool isOBJCGCCandidate(ASTContext &Ctx) const;

  /// Returns true if this expression is a bound member function.
  bool isBoundMemberFunction(ASTContext &Ctx) const;

  /// Given an expression of bound-member type, find the type
  /// of the member.  Returns null if this is an *overloaded* bound
  /// member expression.
  static QualType findBoundMemberType(const Expr *expr);

  /// Skip past any invisible AST nodes which might surround this
  /// statement, such as ExprWithCleanups or ImplicitCastExpr nodes,
  /// but also injected CXXMemberExpr and CXXConstructExpr which represent
  /// implicit conversions.
  Expr *IgnoreUnlessSpelledInSource();
  const Expr *IgnoreUnlessSpelledInSource() const {
    return const_cast<Expr *>(this)->IgnoreUnlessSpelledInSource();
  }

  /// Skip past any implicit casts which might surround this expression until
  /// reaching a fixed point. Skips:
  /// * ImplicitCastExpr
  /// * FullExpr
  Expr *IgnoreImpCasts() LLVM_READONLY;
  const Expr *IgnoreImpCasts() const {
    return const_cast<Expr *>(this)->IgnoreImpCasts();
  }

  /// Skip past any casts which might surround this expression until reaching
  /// a fixed point. Skips:
  /// * CastExpr
  /// * FullExpr
  /// * MaterializeTemporaryExpr
  /// * SubstNonTypeTemplateParmExpr
  Expr *IgnoreCasts() LLVM_READONLY;
  const Expr *IgnoreCasts() const {
    return const_cast<Expr *>(this)->IgnoreCasts();
  }

  /// Skip past any implicit AST nodes which might surround this expression
  /// until reaching a fixed point. Skips:
  /// * What IgnoreImpCasts() skips
  /// * MaterializeTemporaryExpr
  /// * CXXBindTemporaryExpr
  Expr *IgnoreImplicit() LLVM_READONLY;
  const Expr *IgnoreImplicit() const {
    return const_cast<Expr *>(this)->IgnoreImplicit();
  }

  /// Skip past any implicit AST nodes which might surround this expression
  /// until reaching a fixed point. Same as IgnoreImplicit, except that it
  /// also skips over implicit calls to constructors and conversion functions.
  ///
  /// FIXME: Should IgnoreImplicit do this?
  Expr *IgnoreImplicitAsWritten() LLVM_READONLY;
  const Expr *IgnoreImplicitAsWritten() const {
    return const_cast<Expr *>(this)->IgnoreImplicitAsWritten();
  }

  /// Skip past any parentheses which might surround this expression until
  /// reaching a fixed point. Skips:
  /// * ParenExpr
  /// * UnaryOperator if `UO_Extension`
  /// * GenericSelectionExpr if `!isResultDependent()`
  /// * ChooseExpr if `!isConditionDependent()`
  /// * ConstantExpr
  Expr *IgnoreParens() LLVM_READONLY;
  const Expr *IgnoreParens() const {
    return const_cast<Expr *>(this)->IgnoreParens();
  }

  /// Skip past any parentheses and implicit casts which might surround this
  /// expression until reaching a fixed point.
  /// FIXME: IgnoreParenImpCasts really ought to be equivalent to
  /// IgnoreParens() + IgnoreImpCasts() until reaching a fixed point. However
  /// this is currently not the case. Instead IgnoreParenImpCasts() skips:
  /// * What IgnoreParens() skips
  /// * What IgnoreImpCasts() skips
  /// * MaterializeTemporaryExpr
  /// * SubstNonTypeTemplateParmExpr
  Expr *IgnoreParenImpCasts() LLVM_READONLY;
  const Expr *IgnoreParenImpCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenImpCasts();
  }

  /// Skip past any parentheses and casts which might surround this expression
  /// until reaching a fixed point. Skips:
  /// * What IgnoreParens() skips
  /// * What IgnoreCasts() skips
  Expr *IgnoreParenCasts() LLVM_READONLY;
  const Expr *IgnoreParenCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenCasts();
  }

  /// Skip conversion operators. If this Expr is a call to a conversion
  /// operator, return the argument.
  Expr *IgnoreConversionOperatorSingleStep() LLVM_READONLY;
  const Expr *IgnoreConversionOperatorSingleStep() const {
    return const_cast<Expr *>(this)->IgnoreConversionOperatorSingleStep();
  }

  /// Skip past any parentheses and lvalue casts which might surround this
  /// expression until reaching a fixed point. Skips:
  /// * What IgnoreParens() skips
  /// * What IgnoreCasts() skips, except that only lvalue-to-rvalue
  ///   casts are skipped
  /// FIXME: This is intended purely as a temporary workaround for code
  /// that hasn't yet been rewritten to do the right thing about those
  /// casts, and may disappear along with the last internal use.
  Expr *IgnoreParenLValueCasts() LLVM_READONLY;
  const Expr *IgnoreParenLValueCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenLValueCasts();
  }

  /// Skip past any parentheses and casts which do not change the value
  /// (including ptr->int casts of the same size) until reaching a fixed point.
  /// Skips:
  /// * What IgnoreParens() skips
  /// * CastExpr which do not change the value
  /// * SubstNonTypeTemplateParmExpr
  Expr *IgnoreParenNoopCasts(const ASTContext &Ctx) LLVM_READONLY;
  const Expr *IgnoreParenNoopCasts(const ASTContext &Ctx) const {
    return const_cast<Expr *>(this)->IgnoreParenNoopCasts(Ctx);
  }

  /// Skip past any parentheses and derived-to-base casts until reaching a
  /// fixed point. Skips:
  /// * What IgnoreParens() skips
  /// * CastExpr which represent a derived-to-base cast (CK_DerivedToBase,
  ///   CK_UncheckedDerivedToBase and CK_NoOp)
  Expr *IgnoreParenBaseCasts() LLVM_READONLY;
  const Expr *IgnoreParenBaseCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenBaseCasts();
  }

  /// Determine whether this expression is a default function argument.
  ///
  /// Default arguments are implicitly generated in the abstract syntax tree
  /// by semantic analysis for function calls, object constructions, etc. in
  /// C++. Default arguments are represented by \c CXXDefaultArgExpr nodes;
  /// this routine also looks through any implicit casts to determine whether
  /// the expression is a default argument.
  bool isDefaultArgument() const;

  /// Determine whether the result of this expression is a
  /// temporary object of the given class type.
  bool isTemporaryObject(ASTContext &Ctx, const CXXRecordDecl *TempTy) const;

  /// Whether this expression is an implicit reference to 'this' in C++.
  bool isImplicitCXXThis() const;

  static bool hasAnyTypeDependentArguments(ArrayRef<Expr *> Exprs);

  /// For an expression of class type or pointer to class type,
  /// return the most derived class decl the expression is known to refer to.
  ///
  /// If this expression is a cast, this method looks through it to find the
  /// most derived decl that can be inferred from the expression.
  /// This is valid because derived-to-base conversions have undefined
  /// behavior if the object isn't dynamically of the derived type.
  const CXXRecordDecl *getBestDynamicClassType() const;

  /// Get the inner expression that determines the best dynamic class.
  /// If this is a prvalue, we guarantee that it is of the most-derived type
  /// for the object itself.
  const Expr *getBestDynamicClassTypeExpr() const;

  /// Walk outwards from an expression we want to bind a reference to and
  /// find the expression whose lifetime needs to be extended. Record
  /// the LHSs of comma expressions and adjustments needed along the path.
  const Expr *skipRValueSubobjectAdjustments(
      SmallVectorImpl<const Expr *> &CommaLHS,
      SmallVectorImpl<SubobjectAdjustment> &Adjustments) const;
  const Expr *skipRValueSubobjectAdjustments() const {
    SmallVector<const Expr *, 8> CommaLHSs;
    SmallVector<SubobjectAdjustment, 8> Adjustments;
    return skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);
  }

  /// Checks that the two Expr's will refer to the same value as a comparison
  /// operand.  The caller must ensure that the values referenced by the Expr's
  /// are not modified between E1 and E2 or the result my be invalid.
  static bool isSameComparisonOperand(const Expr* E1, const Expr* E2);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstExprConstant &&
           T->getStmtClass() <= lastExprConstant;
  }
};
// PointerLikeTypeTraits is specialized so it can be used with a forward-decl of
// Expr. Verify that we got it right.
static_assert(llvm::PointerLikeTypeTraits<Expr *>::NumLowBitsAvailable <=
                  llvm::detail::ConstantLog2<alignof(Expr)>::value,
              "PointerLikeTypeTraits<Expr*> assumes too much alignment.");

using ConstantExprKind = Expr::ConstantExprKind;

//===----------------------------------------------------------------------===//
// Wrapper Expressions.
//===----------------------------------------------------------------------===//

/// FullExpr - Represents a "full-expression" node.
class FullExpr : public Expr {
protected:
 Stmt *SubExpr;

 FullExpr(StmtClass SC, Expr *subexpr)
     : Expr(SC, subexpr->getType(), subexpr->getValueKind(),
            subexpr->getObjectKind()),
       SubExpr(subexpr) {
   setDependence(computeDependence(this));
 }
  FullExpr(StmtClass SC, EmptyShell Empty)
    : Expr(SC, Empty) {}
public:
  const Expr *getSubExpr() const { return cast<Expr>(SubExpr); }
  Expr *getSubExpr() { return cast<Expr>(SubExpr); }

  /// As with any mutator of the AST, be very careful when modifying an
  /// existing AST to preserve its invariants.
  void setSubExpr(Expr *E) { SubExpr = E; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstFullExprConstant &&
           T->getStmtClass() <= lastFullExprConstant;
  }
};

/// Describes the kind of result that can be tail-allocated.
enum class ConstantResultStorageKind { None, Int64, APValue };

/// ConstantExpr - An expression that occurs in a constant context and
/// optionally the result of evaluating the expression.
class ConstantExpr final
    : public FullExpr,
      private llvm::TrailingObjects<ConstantExpr, APValue, uint64_t> {
  static_assert(std::is_same<uint64_t, llvm::APInt::WordType>::value,
                "ConstantExpr assumes that llvm::APInt::WordType is uint64_t "
                "for tail-allocated storage");
  friend TrailingObjects;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  size_t numTrailingObjects(OverloadToken<APValue>) const {
    return getResultStorageKind() == ConstantResultStorageKind::APValue;
  }
  size_t numTrailingObjects(OverloadToken<uint64_t>) const {
    return getResultStorageKind() == ConstantResultStorageKind::Int64;
  }

  uint64_t &Int64Result() {
    assert(getResultStorageKind() == ConstantResultStorageKind::Int64 &&
           "invalid accessor");
    return *getTrailingObjects<uint64_t>();
  }
  const uint64_t &Int64Result() const {
    return const_cast<ConstantExpr *>(this)->Int64Result();
  }
  APValue &APValueResult() {
    assert(getResultStorageKind() == ConstantResultStorageKind::APValue &&
           "invalid accessor");
    return *getTrailingObjects<APValue>();
  }
  APValue &APValueResult() const {
    return const_cast<ConstantExpr *>(this)->APValueResult();
  }

  ConstantExpr(Expr *SubExpr, ConstantResultStorageKind StorageKind,
               bool IsImmediateInvocation);
  ConstantExpr(EmptyShell Empty, ConstantResultStorageKind StorageKind);

public:
  static ConstantExpr *Create(const ASTContext &Context, Expr *E,
                              const APValue &Result);
  static ConstantExpr *
  Create(const ASTContext &Context, Expr *E,
         ConstantResultStorageKind Storage = ConstantResultStorageKind::None,
         bool IsImmediateInvocation = false);
  static ConstantExpr *CreateEmpty(const ASTContext &Context,
                                   ConstantResultStorageKind StorageKind);

  static ConstantResultStorageKind getStorageKind(const APValue &Value);
  static ConstantResultStorageKind getStorageKind(const Type *T,
                                                  const ASTContext &Context);

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return SubExpr->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubExpr->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConstantExprClass;
  }

  void SetResult(APValue Value, const ASTContext &Context) {
    MoveIntoResult(Value, Context);
  }
  void MoveIntoResult(APValue &Value, const ASTContext &Context);

  APValue::ValueKind getResultAPValueKind() const {
    return static_cast<APValue::ValueKind>(ConstantExprBits.APValueKind);
  }
  ConstantResultStorageKind getResultStorageKind() const {
    return static_cast<ConstantResultStorageKind>(ConstantExprBits.ResultKind);
  }
  bool isImmediateInvocation() const {
    return ConstantExprBits.IsImmediateInvocation;
  }
  bool hasAPValueResult() const {
    return ConstantExprBits.APValueKind != APValue::None;
  }
  APValue getAPValueResult() const;
  llvm::APSInt getResultAsAPSInt() const;
  // Iterators
  child_range children() { return child_range(&SubExpr, &SubExpr+1); }
  const_child_range children() const {
    return const_child_range(&SubExpr, &SubExpr + 1);
  }
};

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

/// OpaqueValueExpr - An expression referring to an opaque object of a
/// fixed type and value class.  These don't correspond to concrete
/// syntax; instead they're used to express operations (usually copy
/// operations) on values whose source is generally obvious from
/// context.
class OpaqueValueExpr : public Expr {
  friend class ASTStmtReader;
  Expr *SourceExpr;

public:
  OpaqueValueExpr(SourceLocation Loc, QualType T, ExprValueKind VK,
                  ExprObjectKind OK = OK_Ordinary, Expr *SourceExpr = nullptr)
      : Expr(OpaqueValueExprClass, T, VK, OK), SourceExpr(SourceExpr) {
    setIsUnique(false);
    OpaqueValueExprBits.Loc = Loc;
    setDependence(computeDependence(this));
  }

  /// Given an expression which invokes a copy constructor --- i.e.  a
  /// CXXConstructExpr, possibly wrapped in an ExprWithCleanups ---
  /// find the OpaqueValueExpr that's the source of the construction.
  static const OpaqueValueExpr *findInCopyConstruct(const Expr *expr);

  explicit OpaqueValueExpr(EmptyShell Empty)
    : Expr(OpaqueValueExprClass, Empty) {}

  /// Retrieve the location of this expression.
  SourceLocation getLocation() const { return OpaqueValueExprBits.Loc; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return SourceExpr ? SourceExpr->getBeginLoc() : getLocation();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SourceExpr ? SourceExpr->getEndLoc() : getLocation();
  }
  SourceLocation getExprLoc() const LLVM_READONLY {
    return SourceExpr ? SourceExpr->getExprLoc() : getLocation();
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  /// The source expression of an opaque value expression is the
  /// expression which originally generated the value.  This is
  /// provided as a convenience for analyses that don't wish to
  /// precisely model the execution behavior of the program.
  ///
  /// The source expression is typically set when building the
  /// expression which binds the opaque value expression in the first
  /// place.
  Expr *getSourceExpr() const { return SourceExpr; }

  void setIsUnique(bool V) {
    assert((!V || SourceExpr) &&
           "unique OVEs are expected to have source expressions");
    OpaqueValueExprBits.IsUnique = V;
  }

  bool isUnique() const { return OpaqueValueExprBits.IsUnique; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OpaqueValueExprClass;
  }
};

/// A reference to a declared variable, function, enum, etc.
/// [C99 6.5.1p2]
///
/// This encodes all the information about how a declaration is referenced
/// within an expression.
///
/// There are several optional constructs attached to DeclRefExprs only when
/// they apply in order to conserve memory. These are laid out past the end of
/// the object, and flags in the DeclRefExprBitfield track whether they exist:
///
///   DeclRefExprBits.HasQualifier:
///       Specifies when this declaration reference expression has a C++
///       nested-name-specifier.
///   DeclRefExprBits.HasFoundDecl:
///       Specifies when this declaration reference expression has a record of
///       a NamedDecl (different from the referenced ValueDecl) which was found
///       during name lookup and/or overload resolution.
///   DeclRefExprBits.HasTemplateKWAndArgsInfo:
///       Specifies when this declaration reference expression has an explicit
///       C++ template keyword and/or template argument list.
///   DeclRefExprBits.RefersToEnclosingVariableOrCapture
///       Specifies when this declaration reference expression (validly)
///       refers to an enclosed local or a captured variable.
class DeclRefExpr final
    : public Expr,
      private llvm::TrailingObjects<DeclRefExpr, NestedNameSpecifierLoc,
                                    NamedDecl *, ASTTemplateKWAndArgsInfo,
                                    TemplateArgumentLoc> {
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  friend TrailingObjects;

  /// The declaration that we are referencing.
  ValueDecl *D;

  /// Provides source/type location info for the declaration name
  /// embedded in D.
  DeclarationNameLoc DNLoc;

  size_t numTrailingObjects(OverloadToken<NestedNameSpecifierLoc>) const {
    return hasQualifier();
  }

  size_t numTrailingObjects(OverloadToken<NamedDecl *>) const {
    return hasFoundDecl();
  }

  size_t numTrailingObjects(OverloadToken<ASTTemplateKWAndArgsInfo>) const {
    return hasTemplateKWAndArgsInfo();
  }

  /// Test whether there is a distinct FoundDecl attached to the end of
  /// this DRE.
  bool hasFoundDecl() const { return DeclRefExprBits.HasFoundDecl; }

  DeclRefExpr(const ASTContext &Ctx, NestedNameSpecifierLoc QualifierLoc,
              SourceLocation TemplateKWLoc, ValueDecl *D,
              bool RefersToEnclosingVariableOrCapture,
              const DeclarationNameInfo &NameInfo, NamedDecl *FoundD,
              const TemplateArgumentListInfo *TemplateArgs, QualType T,
              ExprValueKind VK, NonOdrUseReason NOUR);

  /// Construct an empty declaration reference expression.
  explicit DeclRefExpr(EmptyShell Empty) : Expr(DeclRefExprClass, Empty) {}

public:
  DeclRefExpr(const ASTContext &Ctx, ValueDecl *D,
              bool RefersToEnclosingVariableOrCapture, QualType T,
              ExprValueKind VK, SourceLocation L,
              const DeclarationNameLoc &LocInfo = DeclarationNameLoc(),
              NonOdrUseReason NOUR = NOUR_None);

  static DeclRefExpr *
  Create(const ASTContext &Context, NestedNameSpecifierLoc QualifierLoc,
         SourceLocation TemplateKWLoc, ValueDecl *D,
         bool RefersToEnclosingVariableOrCapture, SourceLocation NameLoc,
         QualType T, ExprValueKind VK, NamedDecl *FoundD = nullptr,
         const TemplateArgumentListInfo *TemplateArgs = nullptr,
         NonOdrUseReason NOUR = NOUR_None);

  static DeclRefExpr *
  Create(const ASTContext &Context, NestedNameSpecifierLoc QualifierLoc,
         SourceLocation TemplateKWLoc, ValueDecl *D,
         bool RefersToEnclosingVariableOrCapture,
         const DeclarationNameInfo &NameInfo, QualType T, ExprValueKind VK,
         NamedDecl *FoundD = nullptr,
         const TemplateArgumentListInfo *TemplateArgs = nullptr,
         NonOdrUseReason NOUR = NOUR_None);

  /// Construct an empty declaration reference expression.
  static DeclRefExpr *CreateEmpty(const ASTContext &Context, bool HasQualifier,
                                  bool HasFoundDecl,
                                  bool HasTemplateKWAndArgsInfo,
                                  unsigned NumTemplateArgs);

  ValueDecl *getDecl() { return D; }
  const ValueDecl *getDecl() const { return D; }
  void setDecl(ValueDecl *NewD);

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDecl()->getDeclName(), getLocation(), DNLoc);
  }

  SourceLocation getLocation() const { return DeclRefExprBits.Loc; }
  void setLocation(SourceLocation L) { DeclRefExprBits.Loc = L; }
  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  /// Determine whether this declaration reference was preceded by a
  /// C++ nested-name-specifier, e.g., \c N::foo.
  bool hasQualifier() const { return DeclRefExprBits.HasQualifier; }

  /// If the name was qualified, retrieves the nested-name-specifier
  /// that precedes the name, with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const {
    if (!hasQualifier())
      return NestedNameSpecifierLoc();
    return *getTrailingObjects<NestedNameSpecifierLoc>();
  }

  /// If the name was qualified, retrieves the nested-name-specifier
  /// that precedes the name. Otherwise, returns NULL.
  NestedNameSpecifier *getQualifier() const {
    return getQualifierLoc().getNestedNameSpecifier();
  }

  /// Get the NamedDecl through which this reference occurred.
  ///
  /// This Decl may be different from the ValueDecl actually referred to in the
  /// presence of using declarations, etc. It always returns non-NULL, and may
  /// simple return the ValueDecl when appropriate.

  NamedDecl *getFoundDecl() {
    return hasFoundDecl() ? *getTrailingObjects<NamedDecl *>() : D;
  }

  /// Get the NamedDecl through which this reference occurred.
  /// See non-const variant.
  const NamedDecl *getFoundDecl() const {
    return hasFoundDecl() ? *getTrailingObjects<NamedDecl *>() : D;
  }

  bool hasTemplateKWAndArgsInfo() const {
    return DeclRefExprBits.HasTemplateKWAndArgsInfo;
  }

  /// Retrieve the location of the template keyword preceding
  /// this name, if any.
  SourceLocation getTemplateKeywordLoc() const {
    if (!hasTemplateKWAndArgsInfo())
      return SourceLocation();
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->TemplateKWLoc;
  }

  /// Retrieve the location of the left angle bracket starting the
  /// explicit template argument list following the name, if any.
  SourceLocation getLAngleLoc() const {
    if (!hasTemplateKWAndArgsInfo())
      return SourceLocation();
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->LAngleLoc;
  }

  /// Retrieve the location of the right angle bracket ending the
  /// explicit template argument list following the name, if any.
  SourceLocation getRAngleLoc() const {
    if (!hasTemplateKWAndArgsInfo())
      return SourceLocation();
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->RAngleLoc;
  }

  /// Determines whether the name in this declaration reference
  /// was preceded by the template keyword.
  bool hasTemplateKeyword() const { return getTemplateKeywordLoc().isValid(); }

  /// Determines whether this declaration reference was followed by an
  /// explicit template argument list.
  bool hasExplicitTemplateArgs() const { return getLAngleLoc().isValid(); }

  /// Copies the template arguments (if present) into the given
  /// structure.
  void copyTemplateArgumentsInto(TemplateArgumentListInfo &List) const {
    if (hasExplicitTemplateArgs())
      getTrailingObjects<ASTTemplateKWAndArgsInfo>()->copyInto(
          getTrailingObjects<TemplateArgumentLoc>(), List);
  }

  /// Retrieve the template arguments provided as part of this
  /// template-id.
  const TemplateArgumentLoc *getTemplateArgs() const {
    if (!hasExplicitTemplateArgs())
      return nullptr;
    return getTrailingObjects<TemplateArgumentLoc>();
  }

  /// Retrieve the number of template arguments provided as part of this
  /// template-id.
  unsigned getNumTemplateArgs() const {
    if (!hasExplicitTemplateArgs())
      return 0;
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->NumTemplateArgs;
  }

  ArrayRef<TemplateArgumentLoc> template_arguments() const {
    return {getTemplateArgs(), getNumTemplateArgs()};
  }

  /// Returns true if this expression refers to a function that
  /// was resolved from an overloaded set having size greater than 1.
  bool hadMultipleCandidates() const {
    return DeclRefExprBits.HadMultipleCandidates;
  }
  /// Sets the flag telling whether this expression refers to
  /// a function that was resolved from an overloaded set having size
  /// greater than 1.
  void setHadMultipleCandidates(bool V = true) {
    DeclRefExprBits.HadMultipleCandidates = V;
  }

  /// Is this expression a non-odr-use reference, and if so, why?
  NonOdrUseReason isNonOdrUse() const {
    return static_cast<NonOdrUseReason>(DeclRefExprBits.NonOdrUseReason);
  }

  /// Does this DeclRefExpr refer to an enclosing local or a captured
  /// variable?
  bool refersToEnclosingVariableOrCapture() const {
    return DeclRefExprBits.RefersToEnclosingVariableOrCapture;
  }

  bool isImmediateEscalating() const {
    return DeclRefExprBits.IsImmediateEscalating;
  }

  void setIsImmediateEscalating(bool Set) {
    DeclRefExprBits.IsImmediateEscalating = Set;
  }

  bool isCapturedByCopyInLambdaWithExplicitObjectParameter() const {
    return DeclRefExprBits.CapturedByCopyInLambdaWithExplicitObjectParameter;
  }

  void setCapturedByCopyInLambdaWithExplicitObjectParameter(
      bool Set, const ASTContext &Context) {
    DeclRefExprBits.CapturedByCopyInLambdaWithExplicitObjectParameter = Set;
    setDependence(computeDependence(this, Context));
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DeclRefExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class IntegerLiteral : public Expr, public APIntStorage {
  SourceLocation Loc;

  /// Construct an empty integer literal.
  explicit IntegerLiteral(EmptyShell Empty)
    : Expr(IntegerLiteralClass, Empty) { }

public:
  // type should be IntTy, LongTy, LongLongTy, UnsignedIntTy, UnsignedLongTy,
  // or UnsignedLongLongTy
  IntegerLiteral(const ASTContext &C, const llvm::APInt &V, QualType type,
                 SourceLocation l);

  /// Returns a new integer literal with value 'V' and type 'type'.
  /// \param type - either IntTy, LongTy, LongLongTy, UnsignedIntTy,
  /// UnsignedLongTy, or UnsignedLongLongTy which should match the size of V
  /// \param V - the value that the returned integer literal contains.
  static IntegerLiteral *Create(const ASTContext &C, const llvm::APInt &V,
                                QualType type, SourceLocation l);
  /// Returns a new empty integer literal.
  static IntegerLiteral *Create(const ASTContext &C, EmptyShell Empty);

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  /// Retrieve the location of the literal.
  SourceLocation getLocation() const { return Loc; }

  void setLocation(SourceLocation Location) { Loc = Location; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IntegerLiteralClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class FixedPointLiteral : public Expr, public APIntStorage {
  SourceLocation Loc;
  unsigned Scale;

  /// \brief Construct an empty fixed-point literal.
  explicit FixedPointLiteral(EmptyShell Empty)
      : Expr(FixedPointLiteralClass, Empty) {}

 public:
  FixedPointLiteral(const ASTContext &C, const llvm::APInt &V, QualType type,
                    SourceLocation l, unsigned Scale);

  // Store the int as is without any bit shifting.
  static FixedPointLiteral *CreateFromRawInt(const ASTContext &C,
                                             const llvm::APInt &V,
                                             QualType type, SourceLocation l,
                                             unsigned Scale);

  /// Returns an empty fixed-point literal.
  static FixedPointLiteral *Create(const ASTContext &C, EmptyShell Empty);

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  /// \brief Retrieve the location of the literal.
  SourceLocation getLocation() const { return Loc; }

  void setLocation(SourceLocation Location) { Loc = Location; }

  unsigned getScale() const { return Scale; }
  void setScale(unsigned S) { Scale = S; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == FixedPointLiteralClass;
  }

  std::string getValueAsString(unsigned Radix) const;

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

enum class CharacterLiteralKind { Ascii, Wide, UTF8, UTF16, UTF32 };

class CharacterLiteral : public Expr {
  unsigned Value;
  SourceLocation Loc;
public:
  // type should be IntTy
  CharacterLiteral(unsigned value, CharacterLiteralKind kind, QualType type,
                   SourceLocation l)
      : Expr(CharacterLiteralClass, type, VK_PRValue, OK_Ordinary),
        Value(value), Loc(l) {
    CharacterLiteralBits.Kind = llvm::to_underlying(kind);
    setDependence(ExprDependence::None);
  }

  /// Construct an empty character literal.
  CharacterLiteral(EmptyShell Empty) : Expr(CharacterLiteralClass, Empty) { }

  SourceLocation getLocation() const { return Loc; }
  CharacterLiteralKind getKind() const {
    return static_cast<CharacterLiteralKind>(CharacterLiteralBits.Kind);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  unsigned getValue() const { return Value; }

  void setLocation(SourceLocation Location) { Loc = Location; }
  void setKind(CharacterLiteralKind kind) {
    CharacterLiteralBits.Kind = llvm::to_underlying(kind);
  }
  void setValue(unsigned Val) { Value = Val; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CharacterLiteralClass;
  }

  static void print(unsigned val, CharacterLiteralKind Kind, raw_ostream &OS);

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class FloatingLiteral : public Expr, private APFloatStorage {
  SourceLocation Loc;

  FloatingLiteral(const ASTContext &C, const llvm::APFloat &V, bool isexact,
                  QualType Type, SourceLocation L);

  /// Construct an empty floating-point literal.
  explicit FloatingLiteral(const ASTContext &C, EmptyShell Empty);

public:
  static FloatingLiteral *Create(const ASTContext &C, const llvm::APFloat &V,
                                 bool isexact, QualType Type, SourceLocation L);
  static FloatingLiteral *Create(const ASTContext &C, EmptyShell Empty);

  llvm::APFloat getValue() const {
    return APFloatStorage::getValue(getSemantics());
  }
  void setValue(const ASTContext &C, const llvm::APFloat &Val) {
    assert(&getSemantics() == &Val.getSemantics() && "Inconsistent semantics");
    APFloatStorage::setValue(C, Val);
  }

  /// Get a raw enumeration value representing the floating-point semantics of
  /// this literal (32-bit IEEE, x87, ...), suitable for serialization.
  llvm::APFloatBase::Semantics getRawSemantics() const {
    return static_cast<llvm::APFloatBase::Semantics>(
        FloatingLiteralBits.Semantics);
  }

  /// Set the raw enumeration value representing the floating-point semantics of
  /// this literal (32-bit IEEE, x87, ...), suitable for serialization.
  void setRawSemantics(llvm::APFloatBase::Semantics Sem) {
    FloatingLiteralBits.Semantics = Sem;
  }

  /// Return the APFloat semantics this literal uses.
  const llvm::fltSemantics &getSemantics() const {
    return llvm::APFloatBase::EnumToSemantics(
        static_cast<llvm::APFloatBase::Semantics>(
            FloatingLiteralBits.Semantics));
  }

  /// Set the APFloat semantics this literal uses.
  void setSemantics(const llvm::fltSemantics &Sem) {
    FloatingLiteralBits.Semantics = llvm::APFloatBase::SemanticsToEnum(Sem);
  }

  bool isExact() const { return FloatingLiteralBits.IsExact; }
  void setExact(bool E) { FloatingLiteralBits.IsExact = E; }

  /// getValueAsApproximateDouble - This returns the value as an inaccurate
  /// double.  Note that this may cause loss of precision, but is useful for
  /// debugging dumps, etc.
  double getValueAsApproximateDouble() const;

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == FloatingLiteralClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

/// ImaginaryLiteral - We support imaginary integer and floating point literals,
/// like "1.0i".  We represent these as a wrapper around FloatingLiteral and
/// IntegerLiteral classes.  Instances of this class always have a Complex type
/// whose element type matches the subexpression.
///
class ImaginaryLiteral : public Expr {
  Stmt *Val;
public:
  ImaginaryLiteral(Expr *val, QualType Ty)
      : Expr(ImaginaryLiteralClass, Ty, VK_PRValue, OK_Ordinary), Val(val) {
    setDependence(ExprDependence::None);
  }

  /// Build an empty imaginary literal.
  explicit ImaginaryLiteral(EmptyShell Empty)
    : Expr(ImaginaryLiteralClass, Empty) { }

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return Val->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return Val->getEndLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImaginaryLiteralClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }
};

enum class StringLiteralKind {
  Ordinary,
  Wide,
  UTF8,
  UTF16,
  UTF32,
  Unevaluated
};

/// StringLiteral - This represents a string literal expression, e.g. "foo"
/// or L"bar" (wide strings). The actual string data can be obtained with
/// getBytes() and is NOT null-terminated. The length of the string data is
/// determined by calling getByteLength().
///
/// The C type for a string is always a ConstantArrayType. In C++, the char
/// type is const qualified, in C it is not.
///
/// Note that strings in C can be formed by concatenation of multiple string
/// literal pptokens in translation phase #6. This keeps track of the locations
/// of each of these pieces.
///
/// Strings in C can also be truncated and extended by assigning into arrays,
/// e.g. with constructs like:
///   char X[2] = "foobar";
/// In this case, getByteLength() will return 6, but the string literal will
/// have type "char[2]".
class StringLiteral final
    : public Expr,
      private llvm::TrailingObjects<StringLiteral, unsigned, SourceLocation,
                                    char> {
  friend class ASTStmtReader;
  friend TrailingObjects;

  /// StringLiteral is followed by several trailing objects. They are in order:
  ///
  /// * A single unsigned storing the length in characters of this string. The
  ///   length in bytes is this length times the width of a single character.
  ///   Always present and stored as a trailing objects because storing it in
  ///   StringLiteral would increase the size of StringLiteral by sizeof(void *)
  ///   due to alignment requirements. If you add some data to StringLiteral,
  ///   consider moving it inside StringLiteral.
  ///
  /// * An array of getNumConcatenated() SourceLocation, one for each of the
  ///   token this string is made of.
  ///
  /// * An array of getByteLength() char used to store the string data.

  unsigned numTrailingObjects(OverloadToken<unsigned>) const { return 1; }
  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return getNumConcatenated();
  }

  unsigned numTrailingObjects(OverloadToken<char>) const {
    return getByteLength();
  }

  char *getStrDataAsChar() { return getTrailingObjects<char>(); }
  const char *getStrDataAsChar() const { return getTrailingObjects<char>(); }

  const uint16_t *getStrDataAsUInt16() const {
    return reinterpret_cast<const uint16_t *>(getTrailingObjects<char>());
  }

  const uint32_t *getStrDataAsUInt32() const {
    return reinterpret_cast<const uint32_t *>(getTrailingObjects<char>());
  }

  /// Build a string literal.
  StringLiteral(const ASTContext &Ctx, StringRef Str, StringLiteralKind Kind,
                bool Pascal, QualType Ty, const SourceLocation *Loc,
                unsigned NumConcatenated);

  /// Build an empty string literal.
  StringLiteral(EmptyShell Empty, unsigned NumConcatenated, unsigned Length,
                unsigned CharByteWidth);

  /// Map a target and string kind to the appropriate character width.
  static unsigned mapCharByteWidth(TargetInfo const &Target,
                                   StringLiteralKind SK);

  /// Set one of the string literal token.
  void setStrTokenLoc(unsigned TokNum, SourceLocation L) {
    assert(TokNum < getNumConcatenated() && "Invalid tok number");
    getTrailingObjects<SourceLocation>()[TokNum] = L;
  }

public:
  /// This is the "fully general" constructor that allows representation of
  /// strings formed from multiple concatenated tokens.
  static StringLiteral *Create(const ASTContext &Ctx, StringRef Str,
                               StringLiteralKind Kind, bool Pascal, QualType Ty,
                               const SourceLocation *Loc,
                               unsigned NumConcatenated);

  /// Simple constructor for string literals made from one token.
  static StringLiteral *Create(const ASTContext &Ctx, StringRef Str,
                               StringLiteralKind Kind, bool Pascal, QualType Ty,
                               SourceLocation Loc) {
    return Create(Ctx, Str, Kind, Pascal, Ty, &Loc, 1);
  }

  /// Construct an empty string literal.
  static StringLiteral *CreateEmpty(const ASTContext &Ctx,
                                    unsigned NumConcatenated, unsigned Length,
                                    unsigned CharByteWidth);

  StringRef getString() const {
    assert((isUnevaluated() || getCharByteWidth() == 1) &&
           "This function is used in places that assume strings use char");
    return StringRef(getStrDataAsChar(), getByteLength());
  }

  /// Allow access to clients that need the byte representation, such as
  /// ASTWriterStmt::VisitStringLiteral().
  StringRef getBytes() const {
    // FIXME: StringRef may not be the right type to use as a result for this.
    return StringRef(getStrDataAsChar(), getByteLength());
  }

  void outputString(raw_ostream &OS) const;

  uint32_t getCodeUnit(size_t i) const {
    assert(i < getLength() && "out of bounds access");
    switch (getCharByteWidth()) {
    case 1:
      return static_cast<unsigned char>(getStrDataAsChar()[i]);
    case 2:
      return getStrDataAsUInt16()[i];
    case 4:
      return getStrDataAsUInt32()[i];
    }
    llvm_unreachable("Unsupported character width!");
  }

  // Get code unit but preserve sign info.
  int64_t getCodeUnitS(size_t I, uint64_t BitWidth) const {
    int64_t V = getCodeUnit(I);
    if (isOrdinary() || isWide()) {
      unsigned Width = getCharByteWidth() * BitWidth;
      llvm::APInt AInt(Width, (uint64_t)V);
      V = AInt.getSExtValue();
    }
    return V;
  }

  unsigned getByteLength() const { return getCharByteWidth() * getLength(); }
  unsigned getLength() const { return *getTrailingObjects<unsigned>(); }
  unsigned getCharByteWidth() const { return StringLiteralBits.CharByteWidth; }

  StringLiteralKind getKind() const {
    return static_cast<StringLiteralKind>(StringLiteralBits.Kind);
  }

  bool isOrdinary() const { return getKind() == StringLiteralKind::Ordinary; }
  bool isWide() const { return getKind() == StringLiteralKind::Wide; }
  bool isUTF8() const { return getKind() == StringLiteralKind::UTF8; }
  bool isUTF16() const { return getKind() == StringLiteralKind::UTF16; }
  bool isUTF32() const { return getKind() == StringLiteralKind::UTF32; }
  bool isUnevaluated() const { return getKind() == StringLiteralKind::Unevaluated; }
  bool isPascal() const { return StringLiteralBits.IsPascal; }

  bool containsNonAscii() const {
    for (auto c : getString())
      if (!isASCII(c))
        return true;
    return false;
  }

  bool containsNonAsciiOrNull() const {
    for (auto c : getString())
      if (!isASCII(c) || !c)
        return true;
    return false;
  }

  /// getNumConcatenated - Get the number of string literal tokens that were
  /// concatenated in translation phase #6 to form this string literal.
  unsigned getNumConcatenated() const {
    return StringLiteralBits.NumConcatenated;
  }

  /// Get one of the string literal token.
  SourceLocation getStrTokenLoc(unsigned TokNum) const {
    assert(TokNum < getNumConcatenated() && "Invalid tok number");
    return getTrailingObjects<SourceLocation>()[TokNum];
  }

  /// getLocationOfByte - Return a source location that points to the specified
  /// byte of this string literal.
  ///
  /// Strings are amazingly complex.  They can be formed from multiple tokens
  /// and can have escape sequences in them in addition to the usual trigraph
  /// and escaped newline business.  This routine handles this complexity.
  ///
  SourceLocation
  getLocationOfByte(unsigned ByteNo, const SourceManager &SM,
                    const LangOptions &Features, const TargetInfo &Target,
                    unsigned *StartToken = nullptr,
                    unsigned *StartTokenByteOffset = nullptr) const;

  typedef const SourceLocation *tokloc_iterator;

  tokloc_iterator tokloc_begin() const {
    return getTrailingObjects<SourceLocation>();
  }

  tokloc_iterator tokloc_end() const {
    return getTrailingObjects<SourceLocation>() + getNumConcatenated();
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return *tokloc_begin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return *(tokloc_end() - 1); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StringLiteralClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

enum class PredefinedIdentKind {
  Func,
  Function,
  LFunction, // Same as Function, but as wide string.
  FuncDName,
  FuncSig,
  LFuncSig, // Same as FuncSig, but as wide string
  PrettyFunction,
  /// The same as PrettyFunction, except that the
  /// 'virtual' keyword is omitted for virtual member functions.
  PrettyFunctionNoVirtual
};

/// [C99 6.4.2.2] - A predefined identifier such as __func__.
class PredefinedExpr final
    : public Expr,
      private llvm::TrailingObjects<PredefinedExpr, Stmt *> {
  friend class ASTStmtReader;
  friend TrailingObjects;

  // PredefinedExpr is optionally followed by a single trailing
  // "Stmt *" for the predefined identifier. It is present if and only if
  // hasFunctionName() is true and is always a "StringLiteral *".

  PredefinedExpr(SourceLocation L, QualType FNTy, PredefinedIdentKind IK,
                 bool IsTransparent, StringLiteral *SL);

  explicit PredefinedExpr(EmptyShell Empty, bool HasFunctionName);

  /// True if this PredefinedExpr has storage for a function name.
  bool hasFunctionName() const { return PredefinedExprBits.HasFunctionName; }

  void setFunctionName(StringLiteral *SL) {
    assert(hasFunctionName() &&
           "This PredefinedExpr has no storage for a function name!");
    *getTrailingObjects<Stmt *>() = SL;
  }

public:
  /// Create a PredefinedExpr.
  ///
  /// If IsTransparent, the PredefinedExpr is transparently handled as a
  /// StringLiteral.
  static PredefinedExpr *Create(const ASTContext &Ctx, SourceLocation L,
                                QualType FNTy, PredefinedIdentKind IK,
                                bool IsTransparent, StringLiteral *SL);

  /// Create an empty PredefinedExpr.
  static PredefinedExpr *CreateEmpty(const ASTContext &Ctx,
                                     bool HasFunctionName);

  PredefinedIdentKind getIdentKind() const {
    return static_cast<PredefinedIdentKind>(PredefinedExprBits.Kind);
  }

  bool isTransparent() const { return PredefinedExprBits.IsTransparent; }

  SourceLocation getLocation() const { return PredefinedExprBits.Loc; }
  void setLocation(SourceLocation L) { PredefinedExprBits.Loc = L; }

  StringLiteral *getFunctionName() {
    return hasFunctionName()
               ? static_cast<StringLiteral *>(*getTrailingObjects<Stmt *>())
               : nullptr;
  }

  const StringLiteral *getFunctionName() const {
    return hasFunctionName()
               ? static_cast<StringLiteral *>(*getTrailingObjects<Stmt *>())
               : nullptr;
  }

  static StringRef getIdentKindName(PredefinedIdentKind IK);
  StringRef getIdentKindName() const {
    return getIdentKindName(getIdentKind());
  }

  static std::string ComputeName(PredefinedIdentKind IK,
                                 const Decl *CurrentDecl,
                                 bool ForceElaboratedPrinting = false);

  SourceLocation getBeginLoc() const { return getLocation(); }
  SourceLocation getEndLoc() const { return getLocation(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == PredefinedExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() + hasFunctionName());
  }

  const_child_range children() const {
    return const_child_range(getTrailingObjects<Stmt *>(),
                             getTrailingObjects<Stmt *>() + hasFunctionName());
  }
};

// This represents a use of the __builtin_sycl_unique_stable_name, which takes a
// type-id, and at CodeGen time emits a unique string representation of the
// type in a way that permits us to properly encode information about the SYCL
// kernels.
class SYCLUniqueStableNameExpr final : public Expr {
  friend class ASTStmtReader;
  SourceLocation OpLoc, LParen, RParen;
  TypeSourceInfo *TypeInfo;

  SYCLUniqueStableNameExpr(EmptyShell Empty, QualType ResultTy);
  SYCLUniqueStableNameExpr(SourceLocation OpLoc, SourceLocation LParen,
                           SourceLocation RParen, QualType ResultTy,
                           TypeSourceInfo *TSI);

  void setTypeSourceInfo(TypeSourceInfo *Ty) { TypeInfo = Ty; }

  void setLocation(SourceLocation L) { OpLoc = L; }
  void setLParenLocation(SourceLocation L) { LParen = L; }
  void setRParenLocation(SourceLocation L) { RParen = L; }

public:
  TypeSourceInfo *getTypeSourceInfo() { return TypeInfo; }

  const TypeSourceInfo *getTypeSourceInfo() const { return TypeInfo; }

  static SYCLUniqueStableNameExpr *
  Create(const ASTContext &Ctx, SourceLocation OpLoc, SourceLocation LParen,
         SourceLocation RParen, TypeSourceInfo *TSI);

  static SYCLUniqueStableNameExpr *CreateEmpty(const ASTContext &Ctx);

  SourceLocation getBeginLoc() const { return getLocation(); }
  SourceLocation getEndLoc() const { return RParen; }
  SourceLocation getLocation() const { return OpLoc; }
  SourceLocation getLParenLocation() const { return LParen; }
  SourceLocation getRParenLocation() const { return RParen; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SYCLUniqueStableNameExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  // Convenience function to generate the name of the currently stored type.
  std::string ComputeName(ASTContext &Context) const;

  // Get the generated name of the type.  Note that this only works after all
  // kernels have been instantiated.
  static std::string ComputeName(ASTContext &Context, QualType Ty);
};

/// ParenExpr - This represents a parenthesized expression, e.g. "(1)".  This
/// AST node is only formed if full location information is requested.
class ParenExpr : public Expr {
  SourceLocation L, R;
  Stmt *Val;
public:
  ParenExpr(SourceLocation l, SourceLocation r, Expr *val)
      : Expr(ParenExprClass, val->getType(), val->getValueKind(),
             val->getObjectKind()),
        L(l), R(r), Val(val) {
    setDependence(computeDependence(this));
  }

  /// Construct an empty parenthesized expression.
  explicit ParenExpr(EmptyShell Empty)
    : Expr(ParenExprClass, Empty) { }

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return L; }
  SourceLocation getEndLoc() const LLVM_READONLY { return R; }

  /// Get the location of the left parentheses '('.
  SourceLocation getLParen() const { return L; }
  void setLParen(SourceLocation Loc) { L = Loc; }

  /// Get the location of the right parentheses ')'.
  SourceLocation getRParen() const { return R; }
  void setRParen(SourceLocation Loc) { R = Loc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ParenExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }
};

/// UnaryOperator - This represents the unary-expression's (except sizeof and
/// alignof), the postinc/postdec operators from postfix-expression, and various
/// extensions.
///
/// Notes on various nodes:
///
/// Real/Imag - These return the real/imag part of a complex operand.  If
///   applied to a non-complex value, the former returns its operand and the
///   later returns zero in the type of the operand.
///
class UnaryOperator final
    : public Expr,
      private llvm::TrailingObjects<UnaryOperator, FPOptionsOverride> {
  Stmt *Val;

  size_t numTrailingObjects(OverloadToken<FPOptionsOverride>) const {
    return UnaryOperatorBits.HasFPFeatures ? 1 : 0;
  }

  FPOptionsOverride &getTrailingFPFeatures() {
    assert(UnaryOperatorBits.HasFPFeatures);
    return *getTrailingObjects<FPOptionsOverride>();
  }

  const FPOptionsOverride &getTrailingFPFeatures() const {
    assert(UnaryOperatorBits.HasFPFeatures);
    return *getTrailingObjects<FPOptionsOverride>();
  }

public:
  typedef UnaryOperatorKind Opcode;

protected:
  UnaryOperator(const ASTContext &Ctx, Expr *input, Opcode opc, QualType type,
                ExprValueKind VK, ExprObjectKind OK, SourceLocation l,
                bool CanOverflow, FPOptionsOverride FPFeatures);

  /// Build an empty unary operator.
  explicit UnaryOperator(bool HasFPFeatures, EmptyShell Empty)
      : Expr(UnaryOperatorClass, Empty) {
    UnaryOperatorBits.Opc = UO_AddrOf;
    UnaryOperatorBits.HasFPFeatures = HasFPFeatures;
  }

public:
  static UnaryOperator *CreateEmpty(const ASTContext &C, bool hasFPFeatures);

  static UnaryOperator *Create(const ASTContext &C, Expr *input, Opcode opc,
                               QualType type, ExprValueKind VK,
                               ExprObjectKind OK, SourceLocation l,
                               bool CanOverflow, FPOptionsOverride FPFeatures);

  Opcode getOpcode() const {
    return static_cast<Opcode>(UnaryOperatorBits.Opc);
  }
  void setOpcode(Opcode Opc) { UnaryOperatorBits.Opc = Opc; }

  Expr *getSubExpr() const { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  /// getOperatorLoc - Return the location of the operator.
  SourceLocation getOperatorLoc() const { return UnaryOperatorBits.Loc; }
  void setOperatorLoc(SourceLocation L) { UnaryOperatorBits.Loc = L; }

  /// Returns true if the unary operator can cause an overflow. For instance,
  ///   signed int i = INT_MAX; i++;
  ///   signed char c = CHAR_MAX; c++;
  /// Due to integer promotions, c++ is promoted to an int before the postfix
  /// increment, and the result is an int that cannot overflow. However, i++
  /// can overflow.
  bool canOverflow() const { return UnaryOperatorBits.CanOverflow; }
  void setCanOverflow(bool C) { UnaryOperatorBits.CanOverflow = C; }

  /// Get the FP contractibility status of this operator. Only meaningful for
  /// operations on floating point types.
  bool isFPContractableWithinStatement(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).allowFPContractWithinStatement();
  }

  /// Get the FENV_ACCESS status of this operator. Only meaningful for
  /// operations on floating point types.
  bool isFEnvAccessOn(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).getAllowFEnvAccess();
  }

  /// isPostfix - Return true if this is a postfix operation, like x++.
  static bool isPostfix(Opcode Op) {
    return Op == UO_PostInc || Op == UO_PostDec;
  }

  /// isPrefix - Return true if this is a prefix operation, like --x.
  static bool isPrefix(Opcode Op) {
    return Op == UO_PreInc || Op == UO_PreDec;
  }

  bool isPrefix() const { return isPrefix(getOpcode()); }
  bool isPostfix() const { return isPostfix(getOpcode()); }

  static bool isIncrementOp(Opcode Op) {
    return Op == UO_PreInc || Op == UO_PostInc;
  }
  bool isIncrementOp() const {
    return isIncrementOp(getOpcode());
  }

  static bool isDecrementOp(Opcode Op) {
    return Op == UO_PreDec || Op == UO_PostDec;
  }
  bool isDecrementOp() const {
    return isDecrementOp(getOpcode());
  }

  static bool isIncrementDecrementOp(Opcode Op) { return Op <= UO_PreDec; }
  bool isIncrementDecrementOp() const {
    return isIncrementDecrementOp(getOpcode());
  }

  static bool isArithmeticOp(Opcode Op) {
    return Op >= UO_Plus && Op <= UO_LNot;
  }
  bool isArithmeticOp() const { return isArithmeticOp(getOpcode()); }

  /// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
  /// corresponds to, e.g. "sizeof" or "[pre]++"
  static StringRef getOpcodeStr(Opcode Op);

  /// Retrieve the unary opcode that corresponds to the given
  /// overloaded operator.
  static Opcode getOverloadedOpcode(OverloadedOperatorKind OO, bool Postfix);

  /// Retrieve the overloaded operator kind that corresponds to
  /// the given unary opcode.
  static OverloadedOperatorKind getOverloadedOperator(Opcode Opc);

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return isPostfix() ? Val->getBeginLoc() : getOperatorLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return isPostfix() ? getOperatorLoc() : Val->getEndLoc();
  }
  SourceLocation getExprLoc() const { return getOperatorLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryOperatorClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }

  /// Is FPFeatures in Trailing Storage?
  bool hasStoredFPFeatures() const { return UnaryOperatorBits.HasFPFeatures; }

  /// Get FPFeatures from trailing storage.
  FPOptionsOverride getStoredFPFeatures() const {
    return getTrailingFPFeatures();
  }

  /// Get the store FPOptionsOverride or default if not stored.
  FPOptionsOverride getStoredFPFeaturesOrDefault() const {
    return hasStoredFPFeatures() ? getStoredFPFeatures() : FPOptionsOverride();
  }

protected:
  /// Set FPFeatures in trailing storage, used by Serialization & ASTImporter.
  void setStoredFPFeatures(FPOptionsOverride F) { getTrailingFPFeatures() = F; }

public:
  /// Get the FP features status of this operator. Only meaningful for
  /// operations on floating point types.
  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (UnaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }
  FPOptionsOverride getFPOptionsOverride() const {
    if (UnaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  friend TrailingObjects;
  friend class ASTNodeImporter;
  friend class ASTReader;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

/// Helper class for OffsetOfExpr.

// __builtin_offsetof(type, identifier(.identifier|[expr])*)
class OffsetOfNode {
public:
  /// The kind of offsetof node we have.
  enum Kind {
    /// An index into an array.
    Array = 0x00,
    /// A field.
    Field = 0x01,
    /// A field in a dependent type, known only by its name.
    Identifier = 0x02,
    /// An implicit indirection through a C++ base class, when the
    /// field found is in a base class.
    Base = 0x03
  };

private:
  enum { MaskBits = 2, Mask = 0x03 };

  /// The source range that covers this part of the designator.
  SourceRange Range;

  /// The data describing the designator, which comes in three
  /// different forms, depending on the lower two bits.
  ///   - An unsigned index into the array of Expr*'s stored after this node
  ///     in memory, for [constant-expression] designators.
  ///   - A FieldDecl*, for references to a known field.
  ///   - An IdentifierInfo*, for references to a field with a given name
  ///     when the class type is dependent.
  ///   - A CXXBaseSpecifier*, for references that look at a field in a
  ///     base class.
  uintptr_t Data;

public:
  /// Create an offsetof node that refers to an array element.
  OffsetOfNode(SourceLocation LBracketLoc, unsigned Index,
               SourceLocation RBracketLoc)
      : Range(LBracketLoc, RBracketLoc), Data((Index << 2) | Array) {}

  /// Create an offsetof node that refers to a field.
  OffsetOfNode(SourceLocation DotLoc, FieldDecl *Field, SourceLocation NameLoc)
      : Range(DotLoc.isValid() ? DotLoc : NameLoc, NameLoc),
        Data(reinterpret_cast<uintptr_t>(Field) | OffsetOfNode::Field) {}

  /// Create an offsetof node that refers to an identifier.
  OffsetOfNode(SourceLocation DotLoc, IdentifierInfo *Name,
               SourceLocation NameLoc)
      : Range(DotLoc.isValid() ? DotLoc : NameLoc, NameLoc),
        Data(reinterpret_cast<uintptr_t>(Name) | Identifier) {}

  /// Create an offsetof node that refers into a C++ base class.
  explicit OffsetOfNode(const CXXBaseSpecifier *Base)
      : Data(reinterpret_cast<uintptr_t>(Base) | OffsetOfNode::Base) {}

  /// Determine what kind of offsetof node this is.
  Kind getKind() const { return static_cast<Kind>(Data & Mask); }

  /// For an array element node, returns the index into the array
  /// of expressions.
  unsigned getArrayExprIndex() const {
    assert(getKind() == Array);
    return Data >> 2;
  }

  /// For a field offsetof node, returns the field.
  FieldDecl *getField() const {
    assert(getKind() == Field);
    return reinterpret_cast<FieldDecl *>(Data & ~(uintptr_t)Mask);
  }

  /// For a field or identifier offsetof node, returns the name of
  /// the field.
  IdentifierInfo *getFieldName() const;

  /// For a base class node, returns the base specifier.
  CXXBaseSpecifier *getBase() const {
    assert(getKind() == Base);
    return reinterpret_cast<CXXBaseSpecifier *>(Data & ~(uintptr_t)Mask);
  }

  /// Retrieve the source range that covers this offsetof node.
  ///
  /// For an array element node, the source range contains the locations of
  /// the square brackets. For a field or identifier node, the source range
  /// contains the location of the period (if there is one) and the
  /// identifier.
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }
};

/// OffsetOfExpr - [C99 7.17] - This represents an expression of the form
/// offsetof(record-type, member-designator). For example, given:
/// @code
/// struct S {
///   float f;
///   double d;
/// };
/// struct T {
///   int i;
///   struct S s[10];
/// };
/// @endcode
/// we can represent and evaluate the expression @c offsetof(struct T, s[2].d).

class OffsetOfExpr final
    : public Expr,
      private llvm::TrailingObjects<OffsetOfExpr, OffsetOfNode, Expr *> {
  SourceLocation OperatorLoc, RParenLoc;
  // Base type;
  TypeSourceInfo *TSInfo;
  // Number of sub-components (i.e. instances of OffsetOfNode).
  unsigned NumComps;
  // Number of sub-expressions (i.e. array subscript expressions).
  unsigned NumExprs;

  size_t numTrailingObjects(OverloadToken<OffsetOfNode>) const {
    return NumComps;
  }

  OffsetOfExpr(const ASTContext &C, QualType type,
               SourceLocation OperatorLoc, TypeSourceInfo *tsi,
               ArrayRef<OffsetOfNode> comps, ArrayRef<Expr*> exprs,
               SourceLocation RParenLoc);

  explicit OffsetOfExpr(unsigned numComps, unsigned numExprs)
    : Expr(OffsetOfExprClass, EmptyShell()),
      TSInfo(nullptr), NumComps(numComps), NumExprs(numExprs) {}

public:

  static OffsetOfExpr *Create(const ASTContext &C, QualType type,
                              SourceLocation OperatorLoc, TypeSourceInfo *tsi,
                              ArrayRef<OffsetOfNode> comps,
                              ArrayRef<Expr*> exprs, SourceLocation RParenLoc);

  static OffsetOfExpr *CreateEmpty(const ASTContext &C,
                                   unsigned NumComps, unsigned NumExprs);

  /// getOperatorLoc - Return the location of the operator.
  SourceLocation getOperatorLoc() const { return OperatorLoc; }
  void setOperatorLoc(SourceLocation L) { OperatorLoc = L; }

  /// Return the location of the right parentheses.
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation R) { RParenLoc = R; }

  TypeSourceInfo *getTypeSourceInfo() const {
    return TSInfo;
  }
  void setTypeSourceInfo(TypeSourceInfo *tsi) {
    TSInfo = tsi;
  }

  const OffsetOfNode &getComponent(unsigned Idx) const {
    assert(Idx < NumComps && "Subscript out of range");
    return getTrailingObjects<OffsetOfNode>()[Idx];
  }

  void setComponent(unsigned Idx, OffsetOfNode ON) {
    assert(Idx < NumComps && "Subscript out of range");
    getTrailingObjects<OffsetOfNode>()[Idx] = ON;
  }

  unsigned getNumComponents() const {
    return NumComps;
  }

  Expr* getIndexExpr(unsigned Idx) {
    assert(Idx < NumExprs && "Subscript out of range");
    return getTrailingObjects<Expr *>()[Idx];
  }

  const Expr *getIndexExpr(unsigned Idx) const {
    assert(Idx < NumExprs && "Subscript out of range");
    return getTrailingObjects<Expr *>()[Idx];
  }

  void setIndexExpr(unsigned Idx, Expr* E) {
    assert(Idx < NumComps && "Subscript out of range");
    getTrailingObjects<Expr *>()[Idx] = E;
  }

  unsigned getNumExpressions() const {
    return NumExprs;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return OperatorLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OffsetOfExprClass;
  }

  // Iterators
  child_range children() {
    Stmt **begin = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(begin, begin + NumExprs);
  }
  const_child_range children() const {
    Stmt *const *begin =
        reinterpret_cast<Stmt *const *>(getTrailingObjects<Expr *>());
    return const_child_range(begin, begin + NumExprs);
  }
  friend TrailingObjects;
};

/// UnaryExprOrTypeTraitExpr - expression with either a type or (unevaluated)
/// expression operand.  Used for sizeof/alignof (C99 6.5.3.4) and
/// vec_step (OpenCL 1.1 6.11.12).
class UnaryExprOrTypeTraitExpr : public Expr {
  union {
    TypeSourceInfo *Ty;
    Stmt *Ex;
  } Argument;
  SourceLocation OpLoc, RParenLoc;

public:
  UnaryExprOrTypeTraitExpr(UnaryExprOrTypeTrait ExprKind, TypeSourceInfo *TInfo,
                           QualType resultType, SourceLocation op,
                           SourceLocation rp)
      : Expr(UnaryExprOrTypeTraitExprClass, resultType, VK_PRValue,
             OK_Ordinary),
        OpLoc(op), RParenLoc(rp) {
    assert(ExprKind <= UETT_Last && "invalid enum value!");
    UnaryExprOrTypeTraitExprBits.Kind = ExprKind;
    assert(static_cast<unsigned>(ExprKind) ==
               UnaryExprOrTypeTraitExprBits.Kind &&
           "UnaryExprOrTypeTraitExprBits.Kind overflow!");
    UnaryExprOrTypeTraitExprBits.IsType = true;
    Argument.Ty = TInfo;
    setDependence(computeDependence(this));
  }

  UnaryExprOrTypeTraitExpr(UnaryExprOrTypeTrait ExprKind, Expr *E,
                           QualType resultType, SourceLocation op,
                           SourceLocation rp);

  /// Construct an empty sizeof/alignof expression.
  explicit UnaryExprOrTypeTraitExpr(EmptyShell Empty)
    : Expr(UnaryExprOrTypeTraitExprClass, Empty) { }

  UnaryExprOrTypeTrait getKind() const {
    return static_cast<UnaryExprOrTypeTrait>(UnaryExprOrTypeTraitExprBits.Kind);
  }
  void setKind(UnaryExprOrTypeTrait K) {
    assert(K <= UETT_Last && "invalid enum value!");
    UnaryExprOrTypeTraitExprBits.Kind = K;
    assert(static_cast<unsigned>(K) == UnaryExprOrTypeTraitExprBits.Kind &&
           "UnaryExprOrTypeTraitExprBits.Kind overflow!");
  }

  bool isArgumentType() const { return UnaryExprOrTypeTraitExprBits.IsType; }
  QualType getArgumentType() const {
    return getArgumentTypeInfo()->getType();
  }
  TypeSourceInfo *getArgumentTypeInfo() const {
    assert(isArgumentType() && "calling getArgumentType() when arg is expr");
    return Argument.Ty;
  }
  Expr *getArgumentExpr() {
    assert(!isArgumentType() && "calling getArgumentExpr() when arg is type");
    return static_cast<Expr*>(Argument.Ex);
  }
  const Expr *getArgumentExpr() const {
    return const_cast<UnaryExprOrTypeTraitExpr*>(this)->getArgumentExpr();
  }

  void setArgument(Expr *E) {
    Argument.Ex = E;
    UnaryExprOrTypeTraitExprBits.IsType = false;
  }
  void setArgument(TypeSourceInfo *TInfo) {
    Argument.Ty = TInfo;
    UnaryExprOrTypeTraitExprBits.IsType = true;
  }

  /// Gets the argument type, or the type of the argument expression, whichever
  /// is appropriate.
  QualType getTypeOfArgument() const {
    return isArgumentType() ? getArgumentType() : getArgumentExpr()->getType();
  }

  SourceLocation getOperatorLoc() const { return OpLoc; }
  void setOperatorLoc(SourceLocation L) { OpLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return OpLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryExprOrTypeTraitExprClass;
  }

  // Iterators
  child_range children();
  const_child_range children() const;
};

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

/// ArraySubscriptExpr - [C99 6.5.2.1] Array Subscripting.
class ArraySubscriptExpr : public Expr {
  enum { LHS, RHS, END_EXPR };
  Stmt *SubExprs[END_EXPR];

  bool lhsIsBase() const { return getRHS()->getType()->isIntegerType(); }

public:
  ArraySubscriptExpr(Expr *lhs, Expr *rhs, QualType t, ExprValueKind VK,
                     ExprObjectKind OK, SourceLocation rbracketloc)
      : Expr(ArraySubscriptExprClass, t, VK, OK) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = rbracketloc;
    setDependence(computeDependence(this));
  }

  /// Create an empty array subscript expression.
  explicit ArraySubscriptExpr(EmptyShell Shell)
    : Expr(ArraySubscriptExprClass, Shell) { }

  /// An array access can be written A[4] or 4[A] (both are equivalent).
  /// - getBase() and getIdx() always present the normalized view: A[4].
  ///    In this case getBase() returns "A" and getIdx() returns "4".
  /// - getLHS() and getRHS() present the syntactic view. e.g. for
  ///    4[A] getLHS() returns "4".
  /// Note: Because vector element access is also written A[4] we must
  /// predicate the format conversion in getBase and getIdx only on the
  /// the type of the RHS, as it is possible for the LHS to be a vector of
  /// integer type
  Expr *getLHS() { return cast<Expr>(SubExprs[LHS]); }
  const Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }

  Expr *getRHS() { return cast<Expr>(SubExprs[RHS]); }
  const Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  Expr *getBase() { return lhsIsBase() ? getLHS() : getRHS(); }
  const Expr *getBase() const { return lhsIsBase() ? getLHS() : getRHS(); }

  Expr *getIdx() { return lhsIsBase() ? getRHS() : getLHS(); }
  const Expr *getIdx() const { return lhsIsBase() ? getRHS() : getLHS(); }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getLHS()->getBeginLoc();
  }
  SourceLocation getEndLoc() const { return getRBracketLoc(); }

  SourceLocation getRBracketLoc() const {
    return ArrayOrMatrixSubscriptExprBits.RBracketLoc;
  }
  void setRBracketLoc(SourceLocation L) {
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = L;
  }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getBase()->getExprLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ArraySubscriptExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

/// MatrixSubscriptExpr - Matrix subscript expression for the MatrixType
/// extension.
/// MatrixSubscriptExpr can be either incomplete (only Base and RowIdx are set
/// so far, the type is IncompleteMatrixIdx) or complete (Base, RowIdx and
/// ColumnIdx refer to valid expressions). Incomplete matrix expressions only
/// exist during the initial construction of the AST.
class MatrixSubscriptExpr : public Expr {
  enum { BASE, ROW_IDX, COLUMN_IDX, END_EXPR };
  Stmt *SubExprs[END_EXPR];

public:
  MatrixSubscriptExpr(Expr *Base, Expr *RowIdx, Expr *ColumnIdx, QualType T,
                      SourceLocation RBracketLoc)
      : Expr(MatrixSubscriptExprClass, T, Base->getValueKind(),
             OK_MatrixComponent) {
    SubExprs[BASE] = Base;
    SubExprs[ROW_IDX] = RowIdx;
    SubExprs[COLUMN_IDX] = ColumnIdx;
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = RBracketLoc;
    setDependence(computeDependence(this));
  }

  /// Create an empty matrix subscript expression.
  explicit MatrixSubscriptExpr(EmptyShell Shell)
      : Expr(MatrixSubscriptExprClass, Shell) {}

  bool isIncomplete() const {
    bool IsIncomplete = hasPlaceholderType(BuiltinType::IncompleteMatrixIdx);
    assert((SubExprs[COLUMN_IDX] || IsIncomplete) &&
           "expressions without column index must be marked as incomplete");
    return IsIncomplete;
  }
  Expr *getBase() { return cast<Expr>(SubExprs[BASE]); }
  const Expr *getBase() const { return cast<Expr>(SubExprs[BASE]); }
  void setBase(Expr *E) { SubExprs[BASE] = E; }

  Expr *getRowIdx() { return cast<Expr>(SubExprs[ROW_IDX]); }
  const Expr *getRowIdx() const { return cast<Expr>(SubExprs[ROW_IDX]); }
  void setRowIdx(Expr *E) { SubExprs[ROW_IDX] = E; }

  Expr *getColumnIdx() { return cast_or_null<Expr>(SubExprs[COLUMN_IDX]); }
  const Expr *getColumnIdx() const {
    assert(!isIncomplete() &&
           "cannot get the column index of an incomplete expression");
    return cast<Expr>(SubExprs[COLUMN_IDX]);
  }
  void setColumnIdx(Expr *E) { SubExprs[COLUMN_IDX] = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }

  SourceLocation getEndLoc() const { return getRBracketLoc(); }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getBase()->getExprLoc();
  }

  SourceLocation getRBracketLoc() const {
    return ArrayOrMatrixSubscriptExprBits.RBracketLoc;
  }
  void setRBracketLoc(SourceLocation L) {
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = L;
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MatrixSubscriptExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

/// CallExpr - Represents a function call (C99 6.5.2.2, C++ [expr.call]).
/// CallExpr itself represents a normal function call, e.g., "f(x, 2)",
/// while its subclasses may represent alternative syntax that (semantically)
/// results in a function call. For example, CXXOperatorCallExpr is
/// a subclass for overloaded operator calls that use operator syntax, e.g.,
/// "str1 + str2" to resolve to a function call.
class CallExpr : public Expr {
  enum { FN = 0, PREARGS_START = 1 };

  /// The number of arguments in the call expression.
  unsigned NumArgs;

  /// The location of the right parentheses. This has a different meaning for
  /// the derived classes of CallExpr.
  SourceLocation RParenLoc;

  // CallExpr store some data in trailing objects. However since CallExpr
  // is used a base of other expression classes we cannot use
  // llvm::TrailingObjects. Instead we manually perform the pointer arithmetic
  // and casts.
  //
  // The trailing objects are in order:
  //
  // * A single "Stmt *" for the callee expression.
  //
  // * An array of getNumPreArgs() "Stmt *" for the pre-argument expressions.
  //
  // * An array of getNumArgs() "Stmt *" for the argument expressions.
  //
  // * An optional of type FPOptionsOverride.
  //
  // Note that we store the offset in bytes from the this pointer to the start
  // of the trailing objects. It would be perfectly possible to compute it
  // based on the dynamic kind of the CallExpr. However 1.) we have plenty of
  // space in the bit-fields of Stmt. 2.) It was benchmarked to be faster to
  // compute this once and then load the offset from the bit-fields of Stmt,
  // instead of re-computing the offset each time the trailing objects are
  // accessed.

  /// Return a pointer to the start of the trailing array of "Stmt *".
  Stmt **getTrailingStmts() {
    return reinterpret_cast<Stmt **>(reinterpret_cast<char *>(this) +
                                     CallExprBits.OffsetToTrailingObjects);
  }
  Stmt *const *getTrailingStmts() const {
    return const_cast<CallExpr *>(this)->getTrailingStmts();
  }

  /// Map a statement class to the appropriate offset in bytes from the
  /// this pointer to the trailing objects.
  static unsigned offsetToTrailingObjects(StmtClass SC);

  unsigned getSizeOfTrailingStmts() const {
    return (1 + getNumPreArgs() + getNumArgs()) * sizeof(Stmt *);
  }

  size_t getOffsetOfTrailingFPFeatures() const {
    assert(hasStoredFPFeatures());
    return CallExprBits.OffsetToTrailingObjects + getSizeOfTrailingStmts();
  }

public:
  enum class ADLCallKind : bool { NotADL, UsesADL };
  static constexpr ADLCallKind NotADL = ADLCallKind::NotADL;
  static constexpr ADLCallKind UsesADL = ADLCallKind::UsesADL;

protected:
  /// Build a call expression, assuming that appropriate storage has been
  /// allocated for the trailing objects.
  CallExpr(StmtClass SC, Expr *Fn, ArrayRef<Expr *> PreArgs,
           ArrayRef<Expr *> Args, QualType Ty, ExprValueKind VK,
           SourceLocation RParenLoc, FPOptionsOverride FPFeatures,
           unsigned MinNumArgs, ADLCallKind UsesADL);

  /// Build an empty call expression, for deserialization.
  CallExpr(StmtClass SC, unsigned NumPreArgs, unsigned NumArgs,
           bool hasFPFeatures, EmptyShell Empty);

  /// Return the size in bytes needed for the trailing objects.
  /// Used by the derived classes to allocate the right amount of storage.
  static unsigned sizeOfTrailingObjects(unsigned NumPreArgs, unsigned NumArgs,
                                        bool HasFPFeatures) {
    return (1 + NumPreArgs + NumArgs) * sizeof(Stmt *) +
           HasFPFeatures * sizeof(FPOptionsOverride);
  }

  Stmt *getPreArg(unsigned I) {
    assert(I < getNumPreArgs() && "Prearg access out of range!");
    return getTrailingStmts()[PREARGS_START + I];
  }
  const Stmt *getPreArg(unsigned I) const {
    assert(I < getNumPreArgs() && "Prearg access out of range!");
    return getTrailingStmts()[PREARGS_START + I];
  }
  void setPreArg(unsigned I, Stmt *PreArg) {
    assert(I < getNumPreArgs() && "Prearg access out of range!");
    getTrailingStmts()[PREARGS_START + I] = PreArg;
  }

  unsigned getNumPreArgs() const { return CallExprBits.NumPreArgs; }

  /// Return a pointer to the trailing FPOptions
  FPOptionsOverride *getTrailingFPFeatures() {
    assert(hasStoredFPFeatures());
    return reinterpret_cast<FPOptionsOverride *>(
        reinterpret_cast<char *>(this) + CallExprBits.OffsetToTrailingObjects +
        getSizeOfTrailingStmts());
  }
  const FPOptionsOverride *getTrailingFPFeatures() const {
    assert(hasStoredFPFeatures());
    return reinterpret_cast<const FPOptionsOverride *>(
        reinterpret_cast<const char *>(this) +
        CallExprBits.OffsetToTrailingObjects + getSizeOfTrailingStmts());
  }

public:
  /// Create a call expression.
  /// \param Fn     The callee expression,
  /// \param Args   The argument array,
  /// \param Ty     The type of the call expression (which is *not* the return
  ///               type in general),
  /// \param VK     The value kind of the call expression (lvalue, rvalue, ...),
  /// \param RParenLoc  The location of the right parenthesis in the call
  ///                   expression.
  /// \param FPFeatures Floating-point features associated with the call,
  /// \param MinNumArgs Specifies the minimum number of arguments. The actual
  ///                   number of arguments will be the greater of Args.size()
  ///                   and MinNumArgs. This is used in a few places to allocate
  ///                   enough storage for the default arguments.
  /// \param UsesADL    Specifies whether the callee was found through
  ///                   argument-dependent lookup.
  ///
  /// Note that you can use CreateTemporary if you need a temporary call
  /// expression on the stack.
  static CallExpr *Create(const ASTContext &Ctx, Expr *Fn,
                          ArrayRef<Expr *> Args, QualType Ty, ExprValueKind VK,
                          SourceLocation RParenLoc,
                          FPOptionsOverride FPFeatures, unsigned MinNumArgs = 0,
                          ADLCallKind UsesADL = NotADL);

  /// Create a temporary call expression with no arguments in the memory
  /// pointed to by Mem. Mem must points to at least sizeof(CallExpr)
  /// + sizeof(Stmt *) bytes of storage, aligned to alignof(CallExpr):
  ///
  /// \code{.cpp}
  ///   alignas(CallExpr) char Buffer[sizeof(CallExpr) + sizeof(Stmt *)];
  ///   CallExpr *TheCall = CallExpr::CreateTemporary(Buffer, etc);
  /// \endcode
  static CallExpr *CreateTemporary(void *Mem, Expr *Fn, QualType Ty,
                                   ExprValueKind VK, SourceLocation RParenLoc,
                                   ADLCallKind UsesADL = NotADL);

  /// Create an empty call expression, for deserialization.
  static CallExpr *CreateEmpty(const ASTContext &Ctx, unsigned NumArgs,
                               bool HasFPFeatures, EmptyShell Empty);

  Expr *getCallee() { return cast<Expr>(getTrailingStmts()[FN]); }
  const Expr *getCallee() const { return cast<Expr>(getTrailingStmts()[FN]); }
  void setCallee(Expr *F) { getTrailingStmts()[FN] = F; }

  ADLCallKind getADLCallKind() const {
    return static_cast<ADLCallKind>(CallExprBits.UsesADL);
  }
  void setADLCallKind(ADLCallKind V = UsesADL) {
    CallExprBits.UsesADL = static_cast<bool>(V);
  }
  bool usesADL() const { return getADLCallKind() == UsesADL; }

  bool hasStoredFPFeatures() const { return CallExprBits.HasFPFeatures; }

  Decl *getCalleeDecl() { return getCallee()->getReferencedDeclOfCallee(); }
  const Decl *getCalleeDecl() const {
    return getCallee()->getReferencedDeclOfCallee();
  }

  /// If the callee is a FunctionDecl, return it. Otherwise return null.
  FunctionDecl *getDirectCallee() {
    return dyn_cast_or_null<FunctionDecl>(getCalleeDecl());
  }
  const FunctionDecl *getDirectCallee() const {
    return dyn_cast_or_null<FunctionDecl>(getCalleeDecl());
  }

  /// getNumArgs - Return the number of actual arguments to this call.
  unsigned getNumArgs() const { return NumArgs; }

  /// Retrieve the call arguments.
  Expr **getArgs() {
    return reinterpret_cast<Expr **>(getTrailingStmts() + PREARGS_START +
                                     getNumPreArgs());
  }
  const Expr *const *getArgs() const {
    return reinterpret_cast<const Expr *const *>(
        getTrailingStmts() + PREARGS_START + getNumPreArgs());
  }

  /// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < getNumArgs() && "Arg access out of range!");
    return getArgs()[Arg];
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < getNumArgs() && "Arg access out of range!");
    return getArgs()[Arg];
  }

  /// setArg - Set the specified argument.
  /// ! the dependence bits might be stale after calling this setter, it is
  /// *caller*'s responsibility to recompute them by calling
  /// computeDependence().
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < getNumArgs() && "Arg access out of range!");
    getArgs()[Arg] = ArgExpr;
  }

  /// Compute and set dependence bits.
  void computeDependence() {
    setDependence(clang::computeDependence(
        this, llvm::ArrayRef(
                  reinterpret_cast<Expr **>(getTrailingStmts() + PREARGS_START),
                  getNumPreArgs())));
  }

  /// Reduce the number of arguments in this call expression. This is used for
  /// example during error recovery to drop extra arguments. There is no way
  /// to perform the opposite because: 1.) We don't track how much storage
  /// we have for the argument array 2.) This would potentially require growing
  /// the argument array, something we cannot support since the arguments are
  /// stored in a trailing array.
  void shrinkNumArgs(unsigned NewNumArgs) {
    assert((NewNumArgs <= getNumArgs()) &&
           "shrinkNumArgs cannot increase the number of arguments!");
    NumArgs = NewNumArgs;
  }

  /// Bluntly set a new number of arguments without doing any checks whatsoever.
  /// Only used during construction of a CallExpr in a few places in Sema.
  /// FIXME: Find a way to remove it.
  void setNumArgsUnsafe(unsigned NewNumArgs) { NumArgs = NewNumArgs; }

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;
  typedef llvm::iterator_range<arg_iterator> arg_range;
  typedef llvm::iterator_range<const_arg_iterator> const_arg_range;

  arg_range arguments() { return arg_range(arg_begin(), arg_end()); }
  const_arg_range arguments() const {
    return const_arg_range(arg_begin(), arg_end());
  }

  arg_iterator arg_begin() {
    return getTrailingStmts() + PREARGS_START + getNumPreArgs();
  }
  arg_iterator arg_end() { return arg_begin() + getNumArgs(); }

  const_arg_iterator arg_begin() const {
    return getTrailingStmts() + PREARGS_START + getNumPreArgs();
  }
  const_arg_iterator arg_end() const { return arg_begin() + getNumArgs(); }

  /// This method provides fast access to all the subexpressions of
  /// a CallExpr without going through the slower virtual child_iterator
  /// interface.  This provides efficient reverse iteration of the
  /// subexpressions.  This is currently used for CFG construction.
  ArrayRef<Stmt *> getRawSubExprs() {
    return llvm::ArrayRef(getTrailingStmts(),
                          PREARGS_START + getNumPreArgs() + getNumArgs());
  }

  /// Get FPOptionsOverride from trailing storage.
  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingFPFeatures();
  }
  /// Set FPOptionsOverride in trailing storage. Used only by Serialization.
  void setStoredFPFeatures(FPOptionsOverride F) {
    assert(hasStoredFPFeatures());
    *getTrailingFPFeatures() = F;
  }

  /// Get the store FPOptionsOverride or default if not stored.
  FPOptionsOverride getStoredFPFeaturesOrDefault() const {
    return hasStoredFPFeatures() ? getStoredFPFeatures() : FPOptionsOverride();
  }

  /// Get the FP features status of this operator. Only meaningful for
  /// operations on floating point types.
  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }

  FPOptionsOverride getFPFeatures() const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  /// getBuiltinCallee - If this is a call to a builtin, return the builtin ID
  /// of the callee. If not, return 0.
  unsigned getBuiltinCallee() const;

  /// Returns \c true if this is a call to a builtin which does not
  /// evaluate side-effects within its arguments.
  bool isUnevaluatedBuiltinCall(const ASTContext &Ctx) const;

  /// getCallReturnType - Get the return type of the call expr. This is not
  /// always the type of the expr itself, if the return type is a reference
  /// type.
  QualType getCallReturnType(const ASTContext &Ctx) const;

  /// Returns the WarnUnusedResultAttr that is either declared on the called
  /// function, or its return type declaration.
  const Attr *getUnusedResultAttr(const ASTContext &Ctx) const;

  /// Returns true if this call expression should warn on unused results.
  bool hasUnusedResultAttr(const ASTContext &Ctx) const {
    return getUnusedResultAttr(Ctx) != nullptr;
  }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  /// Return true if this is a call to __assume() or __builtin_assume() with
  /// a non-value-dependent constant parameter evaluating as false.
  bool isBuiltinAssumeFalse(const ASTContext &Ctx) const;

  /// Used by Sema to implement MSVC-compatible delayed name lookup.
  /// (Usually Exprs themselves should set dependence).
  void markDependentForPostponedNameLookup() {
    setDependence(getDependence() | ExprDependence::TypeValueInstantiation);
  }

  bool isCallToStdMove() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstCallExprConstant &&
           T->getStmtClass() <= lastCallExprConstant;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingStmts(), getTrailingStmts() + PREARGS_START +
                                               getNumPreArgs() + getNumArgs());
  }

  const_child_range children() const {
    return const_child_range(getTrailingStmts(),
                             getTrailingStmts() + PREARGS_START +
                                 getNumPreArgs() + getNumArgs());
  }
};

/// MemberExpr - [C99 6.5.2.3] Structure and Union Members.  X->F and X.F.
///
class MemberExpr final
    : public Expr,
      private llvm::TrailingObjects<MemberExpr, NestedNameSpecifierLoc,
                                    DeclAccessPair, ASTTemplateKWAndArgsInfo,
                                    TemplateArgumentLoc> {
  friend class ASTReader;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  friend TrailingObjects;

  /// Base - the expression for the base pointer or structure references.  In
  /// X.F, this is "X".
  Stmt *Base;

  /// MemberDecl - This is the decl being referenced by the field/member name.
  /// In X.F, this is the decl referenced by F.
  ValueDecl *MemberDecl;

  /// MemberDNLoc - Provides source/type location info for the
  /// declaration name embedded in MemberDecl.
  DeclarationNameLoc MemberDNLoc;

  /// MemberLoc - This is the location of the member name.
  SourceLocation MemberLoc;

  size_t numTrailingObjects(OverloadToken<NestedNameSpecifierLoc>) const {
    return hasQualifier();
  }

  size_t numTrailingObjects(OverloadToken<DeclAccessPair>) const {
    return hasFoundDecl();
  }

  size_t numTrailingObjects(OverloadToken<ASTTemplateKWAndArgsInfo>) const {
    return hasTemplateKWAndArgsInfo();
  }

  bool hasFoundDecl() const { return MemberExprBits.HasFoundDecl; }

  bool hasTemplateKWAndArgsInfo() const {
    return MemberExprBits.HasTemplateKWAndArgsInfo;
  }

  MemberExpr(Expr *Base, bool IsArrow, SourceLocation OperatorLoc,
             NestedNameSpecifierLoc QualifierLoc, SourceLocation TemplateKWLoc,
             ValueDecl *MemberDecl, DeclAccessPair FoundDecl,
             const DeclarationNameInfo &NameInfo,
             const TemplateArgumentListInfo *TemplateArgs, QualType T,
             ExprValueKind VK, ExprObjectKind OK, NonOdrUseReason NOUR);
  MemberExpr(EmptyShell Empty)
      : Expr(MemberExprClass, Empty), Base(), MemberDecl() {}

public:
  static MemberExpr *Create(const ASTContext &C, Expr *Base, bool IsArrow,
                            SourceLocation OperatorLoc,
                            NestedNameSpecifierLoc QualifierLoc,
                            SourceLocation TemplateKWLoc, ValueDecl *MemberDecl,
                            DeclAccessPair FoundDecl,
                            DeclarationNameInfo MemberNameInfo,
                            const TemplateArgumentListInfo *TemplateArgs,
                            QualType T, ExprValueKind VK, ExprObjectKind OK,
                            NonOdrUseReason NOUR);

  /// Create an implicit MemberExpr, with no location, qualifier, template
  /// arguments, and so on. Suitable only for non-static member access.
  static MemberExpr *CreateImplicit(const ASTContext &C, Expr *Base,
                                    bool IsArrow, ValueDecl *MemberDecl,
                                    QualType T, ExprValueKind VK,
                                    ExprObjectKind OK) {
    return Create(C, Base, IsArrow, SourceLocation(), NestedNameSpecifierLoc(),
                  SourceLocation(), MemberDecl,
                  DeclAccessPair::make(MemberDecl, MemberDecl->getAccess()),
                  DeclarationNameInfo(), nullptr, T, VK, OK, NOUR_None);
  }

  static MemberExpr *CreateEmpty(const ASTContext &Context, bool HasQualifier,
                                 bool HasFoundDecl,
                                 bool HasTemplateKWAndArgsInfo,
                                 unsigned NumTemplateArgs);

  void setBase(Expr *E) { Base = E; }
  Expr *getBase() const { return cast<Expr>(Base); }

  /// Retrieve the member declaration to which this expression refers.
  ///
  /// The returned declaration will be a FieldDecl or (in C++) a VarDecl (for
  /// static data members), a CXXMethodDecl, or an EnumConstantDecl.
  ValueDecl *getMemberDecl() const { return MemberDecl; }
  void setMemberDecl(ValueDecl *D);

  /// Retrieves the declaration found by lookup.
  DeclAccessPair getFoundDecl() const {
    if (!hasFoundDecl())
      return DeclAccessPair::make(getMemberDecl(),
                                  getMemberDecl()->getAccess());
    return *getTrailingObjects<DeclAccessPair>();
  }

  /// Determines whether this member expression actually had
  /// a C++ nested-name-specifier prior to the name of the member, e.g.,
  /// x->Base::foo.
  bool hasQualifier() const { return MemberExprBits.HasQualifier; }

  /// If the member name was qualified, retrieves the
  /// nested-name-specifier that precedes the member name, with source-location
  /// information.
  NestedNameSpecifierLoc getQualifierLoc() const {
    if (!hasQualifier())
      return NestedNameSpecifierLoc();
    return *getTrailingObjects<NestedNameSpecifierLoc>();
  }

  /// If the member name was qualified, retrieves the
  /// nested-name-specifier that precedes the member name. Otherwise, returns
  /// NULL.
  NestedNameSpecifier *getQualifier() const {
    return getQualifierLoc().getNestedNameSpecifier();
  }

  /// Retrieve the location of the template keyword preceding
  /// the member name, if any.
  SourceLocation getTemplateKeywordLoc() const {
    if (!hasTemplateKWAndArgsInfo())
      return SourceLocation();
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->TemplateKWLoc;
  }

  /// Retrieve the location of the left angle bracket starting the
  /// explicit template argument list following the member name, if any.
  SourceLocation getLAngleLoc() const {
    if (!hasTemplateKWAndArgsInfo())
      return SourceLocation();
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->LAngleLoc;
  }

  /// Retrieve the location of the right angle bracket ending the
  /// explicit template argument list following the member name, if any.
  SourceLocation getRAngleLoc() const {
    if (!hasTemplateKWAndArgsInfo())
      return SourceLocation();
    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->RAngleLoc;
  }

  /// Determines whether the member name was preceded by the template keyword.
  bool hasTemplateKeyword() const { return getTemplateKeywordLoc().isValid(); }

  /// Determines whether the member name was followed by an
  /// explicit template argument list.
  bool hasExplicitTemplateArgs() const { return getLAngleLoc().isValid(); }

  /// Copies the template arguments (if present) into the given
  /// structure.
  void copyTemplateArgumentsInto(TemplateArgumentListInfo &List) const {
    if (hasExplicitTemplateArgs())
      getTrailingObjects<ASTTemplateKWAndArgsInfo>()->copyInto(
          getTrailingObjects<TemplateArgumentLoc>(), List);
  }

  /// Retrieve the template arguments provided as part of this
  /// template-id.
  const TemplateArgumentLoc *getTemplateArgs() const {
    if (!hasExplicitTemplateArgs())
      return nullptr;

    return getTrailingObjects<TemplateArgumentLoc>();
  }

  /// Retrieve the number of template arguments provided as part of this
  /// template-id.
  unsigned getNumTemplateArgs() const {
    if (!hasExplicitTemplateArgs())
      return 0;

    return getTrailingObjects<ASTTemplateKWAndArgsInfo>()->NumTemplateArgs;
  }

  ArrayRef<TemplateArgumentLoc> template_arguments() const {
    return {getTemplateArgs(), getNumTemplateArgs()};
  }

  /// Retrieve the member declaration name info.
  DeclarationNameInfo getMemberNameInfo() const {
    return DeclarationNameInfo(MemberDecl->getDeclName(),
                               MemberLoc, MemberDNLoc);
  }

  SourceLocation getOperatorLoc() const { return MemberExprBits.OperatorLoc; }

  bool isArrow() const { return MemberExprBits.IsArrow; }
  void setArrow(bool A) { MemberExprBits.IsArrow = A; }

  /// getMemberLoc - Return the location of the "member", in X->F, it is the
  /// location of 'F'.
  SourceLocation getMemberLoc() const { return MemberLoc; }
  void setMemberLoc(SourceLocation L) { MemberLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  SourceLocation getExprLoc() const LLVM_READONLY { return MemberLoc; }

  /// Determine whether the base of this explicit is implicit.
  bool isImplicitAccess() const {
    return getBase() && getBase()->isImplicitCXXThis();
  }

  /// Returns true if this member expression refers to a method that
  /// was resolved from an overloaded set having size greater than 1.
  bool hadMultipleCandidates() const {
    return MemberExprBits.HadMultipleCandidates;
  }
  /// Sets the flag telling whether this expression refers to
  /// a method that was resolved from an overloaded set having size
  /// greater than 1.
  void setHadMultipleCandidates(bool V = true) {
    MemberExprBits.HadMultipleCandidates = V;
  }

  /// Returns true if virtual dispatch is performed.
  /// If the member access is fully qualified, (i.e. X::f()), virtual
  /// dispatching is not performed. In -fapple-kext mode qualified
  /// calls to virtual method will still go through the vtable.
  bool performsVirtualDispatch(const LangOptions &LO) const {
    return LO.AppleKext || !hasQualifier();
  }

  /// Is this expression a non-odr-use reference, and if so, why?
  /// This is only meaningful if the named member is a static member.
  NonOdrUseReason isNonOdrUse() const {
    return static_cast<NonOdrUseReason>(MemberExprBits.NonOdrUseReason);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MemberExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }
  const_child_range children() const {
    return const_child_range(&Base, &Base + 1);
  }
};

/// CompoundLiteralExpr - [C99 6.5.2.5]
///
class CompoundLiteralExpr : public Expr {
  /// LParenLoc - If non-null, this is the location of the left paren in a
  /// compound literal like "(int){4}".  This can be null if this is a
  /// synthesized compound expression.
  SourceLocation LParenLoc;

  /// The type as written.  This can be an incomplete array type, in
  /// which case the actual expression type will be different.
  /// The int part of the pair stores whether this expr is file scope.
  llvm::PointerIntPair<TypeSourceInfo *, 1, bool> TInfoAndScope;
  Stmt *Init;
public:
  CompoundLiteralExpr(SourceLocation lparenloc, TypeSourceInfo *tinfo,
                      QualType T, ExprValueKind VK, Expr *init, bool fileScope)
      : Expr(CompoundLiteralExprClass, T, VK, OK_Ordinary),
        LParenLoc(lparenloc), TInfoAndScope(tinfo, fileScope), Init(init) {
    setDependence(computeDependence(this));
  }

  /// Construct an empty compound literal.
  explicit CompoundLiteralExpr(EmptyShell Empty)
    : Expr(CompoundLiteralExprClass, Empty) { }

  const Expr *getInitializer() const { return cast<Expr>(Init); }
  Expr *getInitializer() { return cast<Expr>(Init); }
  void setInitializer(Expr *E) { Init = E; }

  bool isFileScope() const { return TInfoAndScope.getInt(); }
  void setFileScope(bool FS) { TInfoAndScope.setInt(FS); }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }

  TypeSourceInfo *getTypeSourceInfo() const {
    return TInfoAndScope.getPointer();
  }
  void setTypeSourceInfo(TypeSourceInfo *tinfo) {
    TInfoAndScope.setPointer(tinfo);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    // FIXME: Init should never be null.
    if (!Init)
      return SourceLocation();
    if (LParenLoc.isInvalid())
      return Init->getBeginLoc();
    return LParenLoc;
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    // FIXME: Init should never be null.
    if (!Init)
      return SourceLocation();
    return Init->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CompoundLiteralExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Init, &Init+1); }
  const_child_range children() const {
    return const_child_range(&Init, &Init + 1);
  }
};

/// CastExpr - Base class for type casts, including both implicit
/// casts (ImplicitCastExpr) and explicit casts that have some
/// representation in the source code (ExplicitCastExpr's derived
/// classes).
class CastExpr : public Expr {
  Stmt *Op;

  bool CastConsistency() const;

  const CXXBaseSpecifier * const *path_buffer() const {
    return const_cast<CastExpr*>(this)->path_buffer();
  }
  CXXBaseSpecifier **path_buffer();

  friend class ASTStmtReader;

protected:
  CastExpr(StmtClass SC, QualType ty, ExprValueKind VK, const CastKind kind,
           Expr *op, unsigned BasePathSize, bool HasFPFeatures)
      : Expr(SC, ty, VK, OK_Ordinary), Op(op) {
    CastExprBits.Kind = kind;
    CastExprBits.PartOfExplicitCast = false;
    CastExprBits.BasePathSize = BasePathSize;
    assert((CastExprBits.BasePathSize == BasePathSize) &&
           "BasePathSize overflow!");
    assert(CastConsistency());
    CastExprBits.HasFPFeatures = HasFPFeatures;
  }

  /// Construct an empty cast.
  CastExpr(StmtClass SC, EmptyShell Empty, unsigned BasePathSize,
           bool HasFPFeatures)
      : Expr(SC, Empty) {
    CastExprBits.PartOfExplicitCast = false;
    CastExprBits.BasePathSize = BasePathSize;
    CastExprBits.HasFPFeatures = HasFPFeatures;
    assert((CastExprBits.BasePathSize == BasePathSize) &&
           "BasePathSize overflow!");
  }

  /// Return a pointer to the trailing FPOptions.
  /// \pre hasStoredFPFeatures() == true
  FPOptionsOverride *getTrailingFPFeatures();
  const FPOptionsOverride *getTrailingFPFeatures() const {
    return const_cast<CastExpr *>(this)->getTrailingFPFeatures();
  }

public:
  CastKind getCastKind() const { return (CastKind) CastExprBits.Kind; }
  void setCastKind(CastKind K) { CastExprBits.Kind = K; }

  static const char *getCastKindName(CastKind CK);
  const char *getCastKindName() const { return getCastKindName(getCastKind()); }

  Expr *getSubExpr() { return cast<Expr>(Op); }
  const Expr *getSubExpr() const { return cast<Expr>(Op); }
  void setSubExpr(Expr *E) { Op = E; }

  /// Retrieve the cast subexpression as it was written in the source
  /// code, looking through any implicit casts or other intermediate nodes
  /// introduced by semantic analysis.
  Expr *getSubExprAsWritten();
  const Expr *getSubExprAsWritten() const {
    return const_cast<CastExpr *>(this)->getSubExprAsWritten();
  }

  /// If this cast applies a user-defined conversion, retrieve the conversion
  /// function that it invokes.
  NamedDecl *getConversionFunction() const;

  typedef CXXBaseSpecifier **path_iterator;
  typedef const CXXBaseSpecifier *const *path_const_iterator;
  bool path_empty() const { return path_size() == 0; }
  unsigned path_size() const { return CastExprBits.BasePathSize; }
  path_iterator path_begin() { return path_buffer(); }
  path_iterator path_end() { return path_buffer() + path_size(); }
  path_const_iterator path_begin() const { return path_buffer(); }
  path_const_iterator path_end() const { return path_buffer() + path_size(); }

  /// Path through the class hierarchy taken by casts between base and derived
  /// classes (see implementation of `CastConsistency()` for a full list of
  /// cast kinds that have a path).
  ///
  /// For each derived-to-base edge in the path, the path contains a
  /// `CXXBaseSpecifier` for the base class of that edge; the entries are
  /// ordered from derived class to base class.
  ///
  /// For example, given classes `Base`, `Intermediate : public Base` and
  /// `Derived : public Intermediate`, the path for a cast from `Derived *` to
  /// `Base *` contains two entries: One for `Intermediate`, and one for `Base`,
  /// in that order.
  llvm::iterator_range<path_iterator> path() {
    return llvm::make_range(path_begin(), path_end());
  }
  llvm::iterator_range<path_const_iterator> path() const {
    return llvm::make_range(path_begin(), path_end());
  }

  const FieldDecl *getTargetUnionField() const {
    assert(getCastKind() == CK_ToUnion);
    return getTargetFieldForToUnionCast(getType(), getSubExpr()->getType());
  }

  bool hasStoredFPFeatures() const { return CastExprBits.HasFPFeatures; }

  /// Get FPOptionsOverride from trailing storage.
  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingFPFeatures();
  }

  /// Get the store FPOptionsOverride or default if not stored.
  FPOptionsOverride getStoredFPFeaturesOrDefault() const {
    return hasStoredFPFeatures() ? getStoredFPFeatures() : FPOptionsOverride();
  }

  /// Get the FP features status of this operation. Only meaningful for
  /// operations on floating point types.
  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }

  FPOptionsOverride getFPFeatures() const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  /// Return
  //  True : if this conversion changes the volatile-ness of a gl-value.
  //         Qualification conversions on gl-values currently use CK_NoOp, but
  //         it's important to recognize volatile-changing conversions in
  //         clients code generation that normally eagerly peephole loads. Note
  //         that the query is answering for this specific node; Sema may
  //         produce multiple cast nodes for any particular conversion sequence.
  //  False : Otherwise.
  bool changesVolatileQualification() const {
    return (isGLValue() && (getType().isVolatileQualified() !=
                            getSubExpr()->getType().isVolatileQualified()));
  }

  static const FieldDecl *getTargetFieldForToUnionCast(QualType unionType,
                                                       QualType opType);
  static const FieldDecl *getTargetFieldForToUnionCast(const RecordDecl *RD,
                                                       QualType opType);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstCastExprConstant &&
           T->getStmtClass() <= lastCastExprConstant;
  }

  // Iterators
  child_range children() { return child_range(&Op, &Op+1); }
  const_child_range children() const { return const_child_range(&Op, &Op + 1); }
};

/// ImplicitCastExpr - Allows us to explicitly represent implicit type
/// conversions, which have no direct representation in the original
/// source code. For example: converting T[]->T*, void f()->void
/// (*f)(), float->double, short->int, etc.
///
/// In C, implicit casts always produce rvalues. However, in C++, an
/// implicit cast whose result is being bound to a reference will be
/// an lvalue or xvalue. For example:
///
/// @code
/// class Base { };
/// class Derived : public Base { };
/// Derived &&ref();
/// void f(Derived d) {
///   Base& b = d; // initializer is an ImplicitCastExpr
///                // to an lvalue of type Base
///   Base&& r = ref(); // initializer is an ImplicitCastExpr
///                     // to an xvalue of type Base
/// }
/// @endcode
class ImplicitCastExpr final
    : public CastExpr,
      private llvm::TrailingObjects<ImplicitCastExpr, CXXBaseSpecifier *,
                                    FPOptionsOverride> {

  ImplicitCastExpr(QualType ty, CastKind kind, Expr *op,
                   unsigned BasePathLength, FPOptionsOverride FPO,
                   ExprValueKind VK)
      : CastExpr(ImplicitCastExprClass, ty, VK, kind, op, BasePathLength,
                 FPO.requiresTrailingStorage()) {
    setDependence(computeDependence(this));
    if (hasStoredFPFeatures())
      *getTrailingFPFeatures() = FPO;
  }

  /// Construct an empty implicit cast.
  explicit ImplicitCastExpr(EmptyShell Shell, unsigned PathSize,
                            bool HasFPFeatures)
      : CastExpr(ImplicitCastExprClass, Shell, PathSize, HasFPFeatures) {}

  unsigned numTrailingObjects(OverloadToken<CXXBaseSpecifier *>) const {
    return path_size();
  }

public:
  enum OnStack_t { OnStack };
  ImplicitCastExpr(OnStack_t _, QualType ty, CastKind kind, Expr *op,
                   ExprValueKind VK, FPOptionsOverride FPO)
      : CastExpr(ImplicitCastExprClass, ty, VK, kind, op, 0,
                 FPO.requiresTrailingStorage()) {
    if (hasStoredFPFeatures())
      *getTrailingFPFeatures() = FPO;
  }

  bool isPartOfExplicitCast() const { return CastExprBits.PartOfExplicitCast; }
  void setIsPartOfExplicitCast(bool PartOfExplicitCast) {
    CastExprBits.PartOfExplicitCast = PartOfExplicitCast;
  }

  static ImplicitCastExpr *Create(const ASTContext &Context, QualType T,
                                  CastKind Kind, Expr *Operand,
                                  const CXXCastPath *BasePath,
                                  ExprValueKind Cat, FPOptionsOverride FPO);

  static ImplicitCastExpr *CreateEmpty(const ASTContext &Context,
                                       unsigned PathSize, bool HasFPFeatures);

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getSubExpr()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSubExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImplicitCastExprClass;
  }

  friend TrailingObjects;
  friend class CastExpr;
};

/// ExplicitCastExpr - An explicit cast written in the source
/// code.
///
/// This class is effectively an abstract class, because it provides
/// the basic representation of an explicitly-written cast without
/// specifying which kind of cast (C cast, functional cast, static
/// cast, etc.) was written; specific derived classes represent the
/// particular style of cast and its location information.
///
/// Unlike implicit casts, explicit cast nodes have two different
/// types: the type that was written into the source code, and the
/// actual type of the expression as determined by semantic
/// analysis. These types may differ slightly. For example, in C++ one
/// can cast to a reference type, which indicates that the resulting
/// expression will be an lvalue or xvalue. The reference type, however,
/// will not be used as the type of the expression.
class ExplicitCastExpr : public CastExpr {
  /// TInfo - Source type info for the (written) type
  /// this expression is casting to.
  TypeSourceInfo *TInfo;

protected:
  ExplicitCastExpr(StmtClass SC, QualType exprTy, ExprValueKind VK,
                   CastKind kind, Expr *op, unsigned PathSize,
                   bool HasFPFeatures, TypeSourceInfo *writtenTy)
      : CastExpr(SC, exprTy, VK, kind, op, PathSize, HasFPFeatures),
        TInfo(writtenTy) {
    setDependence(computeDependence(this));
  }

  /// Construct an empty explicit cast.
  ExplicitCastExpr(StmtClass SC, EmptyShell Shell, unsigned PathSize,
                   bool HasFPFeatures)
      : CastExpr(SC, Shell, PathSize, HasFPFeatures) {}

public:
  /// getTypeInfoAsWritten - Returns the type source info for the type
  /// that this expression is casting to.
  TypeSourceInfo *getTypeInfoAsWritten() const { return TInfo; }
  void setTypeInfoAsWritten(TypeSourceInfo *writtenTy) { TInfo = writtenTy; }

  /// getTypeAsWritten - Returns the type that this expression is
  /// casting to, as written in the source code.
  QualType getTypeAsWritten() const { return TInfo->getType(); }

  static bool classof(const Stmt *T) {
     return T->getStmtClass() >= firstExplicitCastExprConstant &&
            T->getStmtClass() <= lastExplicitCastExprConstant;
  }
};

/// CStyleCastExpr - An explicit cast in C (C99 6.5.4) or a C-style
/// cast in C++ (C++ [expr.cast]), which uses the syntax
/// (Type)expr. For example: @c (int)f.
class CStyleCastExpr final
    : public ExplicitCastExpr,
      private llvm::TrailingObjects<CStyleCastExpr, CXXBaseSpecifier *,
                                    FPOptionsOverride> {
  SourceLocation LPLoc; // the location of the left paren
  SourceLocation RPLoc; // the location of the right paren

  CStyleCastExpr(QualType exprTy, ExprValueKind vk, CastKind kind, Expr *op,
                 unsigned PathSize, FPOptionsOverride FPO,
                 TypeSourceInfo *writtenTy, SourceLocation l, SourceLocation r)
      : ExplicitCastExpr(CStyleCastExprClass, exprTy, vk, kind, op, PathSize,
                         FPO.requiresTrailingStorage(), writtenTy),
        LPLoc(l), RPLoc(r) {
    if (hasStoredFPFeatures())
      *getTrailingFPFeatures() = FPO;
  }

  /// Construct an empty C-style explicit cast.
  explicit CStyleCastExpr(EmptyShell Shell, unsigned PathSize,
                          bool HasFPFeatures)
      : ExplicitCastExpr(CStyleCastExprClass, Shell, PathSize, HasFPFeatures) {}

  unsigned numTrailingObjects(OverloadToken<CXXBaseSpecifier *>) const {
    return path_size();
  }

public:
  static CStyleCastExpr *
  Create(const ASTContext &Context, QualType T, ExprValueKind VK, CastKind K,
         Expr *Op, const CXXCastPath *BasePath, FPOptionsOverride FPO,
         TypeSourceInfo *WrittenTy, SourceLocation L, SourceLocation R);

  static CStyleCastExpr *CreateEmpty(const ASTContext &Context,
                                     unsigned PathSize, bool HasFPFeatures);

  SourceLocation getLParenLoc() const { return LPLoc; }
  void setLParenLoc(SourceLocation L) { LPLoc = L; }

  SourceLocation getRParenLoc() const { return RPLoc; }
  void setRParenLoc(SourceLocation L) { RPLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LPLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSubExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CStyleCastExprClass;
  }

  friend TrailingObjects;
  friend class CastExpr;
};

/// A builtin binary operation expression such as "x + y" or "x <= y".
///
/// This expression node kind describes a builtin binary operation,
/// such as "x + y" for integer values "x" and "y". The operands will
/// already have been converted to appropriate types (e.g., by
/// performing promotions or conversions).
///
/// In C++, where operators may be overloaded, a different kind of
/// expression node (CXXOperatorCallExpr) is used to express the
/// invocation of an overloaded operator with operator syntax. Within
/// a C++ template, whether BinaryOperator or CXXOperatorCallExpr is
/// used to store an expression "x + y" depends on the subexpressions
/// for x and y. If neither x or y is type-dependent, and the "+"
/// operator resolves to a built-in operation, BinaryOperator will be
/// used to express the computation (x and y may still be
/// value-dependent). If either x or y is type-dependent, or if the
/// "+" resolves to an overloaded operator, CXXOperatorCallExpr will
/// be used to express the computation.
class BinaryOperator : public Expr {
  enum { LHS, RHS, END_EXPR };
  Stmt *SubExprs[END_EXPR];

public:
  typedef BinaryOperatorKind Opcode;

protected:
  size_t offsetOfTrailingStorage() const;

  /// Return a pointer to the trailing FPOptions
  FPOptionsOverride *getTrailingFPFeatures() {
    assert(BinaryOperatorBits.HasFPFeatures);
    return reinterpret_cast<FPOptionsOverride *>(
        reinterpret_cast<char *>(this) + offsetOfTrailingStorage());
  }
  const FPOptionsOverride *getTrailingFPFeatures() const {
    assert(BinaryOperatorBits.HasFPFeatures);
    return reinterpret_cast<const FPOptionsOverride *>(
        reinterpret_cast<const char *>(this) + offsetOfTrailingStorage());
  }

  /// Build a binary operator, assuming that appropriate storage has been
  /// allocated for the trailing objects when needed.
  BinaryOperator(const ASTContext &Ctx, Expr *lhs, Expr *rhs, Opcode opc,
                 QualType ResTy, ExprValueKind VK, ExprObjectKind OK,
                 SourceLocation opLoc, FPOptionsOverride FPFeatures);

  /// Construct an empty binary operator.
  explicit BinaryOperator(EmptyShell Empty) : Expr(BinaryOperatorClass, Empty) {
    BinaryOperatorBits.Opc = BO_Comma;
  }

public:
  static BinaryOperator *CreateEmpty(const ASTContext &C, bool hasFPFeatures);

  static BinaryOperator *Create(const ASTContext &C, Expr *lhs, Expr *rhs,
                                Opcode opc, QualType ResTy, ExprValueKind VK,
                                ExprObjectKind OK, SourceLocation opLoc,
                                FPOptionsOverride FPFeatures);
  SourceLocation getExprLoc() const { return getOperatorLoc(); }
  SourceLocation getOperatorLoc() const { return BinaryOperatorBits.OpLoc; }
  void setOperatorLoc(SourceLocation L) { BinaryOperatorBits.OpLoc = L; }

  Opcode getOpcode() const {
    return static_cast<Opcode>(BinaryOperatorBits.Opc);
  }
  void setOpcode(Opcode Opc) { BinaryOperatorBits.Opc = Opc; }

  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getLHS()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getRHS()->getEndLoc();
  }

  /// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
  /// corresponds to, e.g. "<<=".
  static StringRef getOpcodeStr(Opcode Op);

  StringRef getOpcodeStr() const { return getOpcodeStr(getOpcode()); }

  /// Retrieve the binary opcode that corresponds to the given
  /// overloaded operator.
  static Opcode getOverloadedOpcode(OverloadedOperatorKind OO);

  /// Retrieve the overloaded operator kind that corresponds to
  /// the given binary opcode.
  static OverloadedOperatorKind getOverloadedOperator(Opcode Opc);

  /// predicates to categorize the respective opcodes.
  static bool isPtrMemOp(Opcode Opc) {
    return Opc == BO_PtrMemD || Opc == BO_PtrMemI;
  }
  bool isPtrMemOp() const { return isPtrMemOp(getOpcode()); }

  static bool isMultiplicativeOp(Opcode Opc) {
    return Opc >= BO_Mul && Opc <= BO_Rem;
  }
  bool isMultiplicativeOp() const { return isMultiplicativeOp(getOpcode()); }
  static bool isAdditiveOp(Opcode Opc) { return Opc == BO_Add || Opc==BO_Sub; }
  bool isAdditiveOp() const { return isAdditiveOp(getOpcode()); }
  static bool isShiftOp(Opcode Opc) { return Opc == BO_Shl || Opc == BO_Shr; }
  bool isShiftOp() const { return isShiftOp(getOpcode()); }

  static bool isBitwiseOp(Opcode Opc) { return Opc >= BO_And && Opc <= BO_Or; }
  bool isBitwiseOp() const { return isBitwiseOp(getOpcode()); }

  static bool isRelationalOp(Opcode Opc) { return Opc >= BO_LT && Opc<=BO_GE; }
  bool isRelationalOp() const { return isRelationalOp(getOpcode()); }

  static bool isEqualityOp(Opcode Opc) { return Opc == BO_EQ || Opc == BO_NE; }
  bool isEqualityOp() const { return isEqualityOp(getOpcode()); }

  static bool isComparisonOp(Opcode Opc) { return Opc >= BO_Cmp && Opc<=BO_NE; }
  bool isComparisonOp() const { return isComparisonOp(getOpcode()); }

  static bool isCommaOp(Opcode Opc) { return Opc == BO_Comma; }
  bool isCommaOp() const { return isCommaOp(getOpcode()); }

  static Opcode negateComparisonOp(Opcode Opc) {
    switch (Opc) {
    default:
      llvm_unreachable("Not a comparison operator.");
    case BO_LT: return BO_GE;
    case BO_GT: return BO_LE;
    case BO_LE: return BO_GT;
    case BO_GE: return BO_LT;
    case BO_EQ: return BO_NE;
    case BO_NE: return BO_EQ;
    }
  }

  static Opcode reverseComparisonOp(Opcode Opc) {
    switch (Opc) {
    default:
      llvm_unreachable("Not a comparison operator.");
    case BO_LT: return BO_GT;
    case BO_GT: return BO_LT;
    case BO_LE: return BO_GE;
    case BO_GE: return BO_LE;
    case BO_EQ:
    case BO_NE:
      return Opc;
    }
  }

  static bool isLogicalOp(Opcode Opc) { return Opc == BO_LAnd || Opc==BO_LOr; }
  bool isLogicalOp() const { return isLogicalOp(getOpcode()); }

  static bool isAssignmentOp(Opcode Opc) {
    return Opc >= BO_Assign && Opc <= BO_OrAssign;
  }
  bool isAssignmentOp() const { return isAssignmentOp(getOpcode()); }

  static bool isCompoundAssignmentOp(Opcode Opc) {
    return Opc > BO_Assign && Opc <= BO_OrAssign;
  }
  bool isCompoundAssignmentOp() const {
    return isCompoundAssignmentOp(getOpcode());
  }
  static Opcode getOpForCompoundAssignment(Opcode Opc) {
    assert(isCompoundAssignmentOp(Opc));
    if (Opc >= BO_AndAssign)
      return Opcode(unsigned(Opc) - BO_AndAssign + BO_And);
    else
      return Opcode(unsigned(Opc) - BO_MulAssign + BO_Mul);
  }

  static bool isShiftAssignOp(Opcode Opc) {
    return Opc == BO_ShlAssign || Opc == BO_ShrAssign;
  }
  bool isShiftAssignOp() const {
    return isShiftAssignOp(getOpcode());
  }

  /// Return true if a binary operator using the specified opcode and operands
  /// would match the 'p = (i8*)nullptr + n' idiom for casting a pointer-sized
  /// integer to a pointer.
  static bool isNullPointerArithmeticExtension(ASTContext &Ctx, Opcode Opc,
                                               const Expr *LHS,
                                               const Expr *RHS);

  static bool classof(const Stmt *S) {
    return S->getStmtClass() >= firstBinaryOperatorConstant &&
           S->getStmtClass() <= lastBinaryOperatorConstant;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }

  /// Set and fetch the bit that shows whether FPFeatures needs to be
  /// allocated in Trailing Storage
  void setHasStoredFPFeatures(bool B) { BinaryOperatorBits.HasFPFeatures = B; }
  bool hasStoredFPFeatures() const { return BinaryOperatorBits.HasFPFeatures; }

  /// Get FPFeatures from trailing storage
  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingFPFeatures();
  }
  /// Set FPFeatures in trailing storage, used only by Serialization
  void setStoredFPFeatures(FPOptionsOverride F) {
    assert(BinaryOperatorBits.HasFPFeatures);
    *getTrailingFPFeatures() = F;
  }
  /// Get the store FPOptionsOverride or default if not stored.
  FPOptionsOverride getStoredFPFeaturesOrDefault() const {
    return hasStoredFPFeatures() ? getStoredFPFeatures() : FPOptionsOverride();
  }

  /// Get the FP features status of this operator. Only meaningful for
  /// operations on floating point types.
  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (BinaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }

  // This is used in ASTImporter
  FPOptionsOverride getFPFeatures() const {
    if (BinaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  /// Get the FP contractibility status of this operator. Only meaningful for
  /// operations on floating point types.
  bool isFPContractableWithinStatement(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).allowFPContractWithinStatement();
  }

  /// Get the FENV_ACCESS status of this operator. Only meaningful for
  /// operations on floating point types.
  bool isFEnvAccessOn(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).getAllowFEnvAccess();
  }

protected:
  BinaryOperator(const ASTContext &Ctx, Expr *lhs, Expr *rhs, Opcode opc,
                 QualType ResTy, ExprValueKind VK, ExprObjectKind OK,
                 SourceLocation opLoc, FPOptionsOverride FPFeatures,
                 bool dead2);

  /// Construct an empty BinaryOperator, SC is CompoundAssignOperator.
  BinaryOperator(StmtClass SC, EmptyShell Empty) : Expr(SC, Empty) {
    BinaryOperatorBits.Opc = BO_MulAssign;
  }

  /// Return the size in bytes needed for the trailing objects.
  /// Used to allocate the right amount of storage.
  static unsigned sizeOfTrailingObjects(bool HasFPFeatures) {
    return HasFPFeatures * sizeof(FPOptionsOverride);
  }
};

/// CompoundAssignOperator - For compound assignments (e.g. +=), we keep
/// track of the type the operation is performed in.  Due to the semantics of
/// these operators, the operands are promoted, the arithmetic performed, an
/// implicit conversion back to the result type done, then the assignment takes
/// place.  This captures the intermediate type which the computation is done
/// in.
class CompoundAssignOperator : public BinaryOperator {
  QualType ComputationLHSType;
  QualType ComputationResultType;

  /// Construct an empty CompoundAssignOperator.
  explicit CompoundAssignOperator(const ASTContext &C, EmptyShell Empty,
                                  bool hasFPFeatures)
      : BinaryOperator(CompoundAssignOperatorClass, Empty) {}

protected:
  CompoundAssignOperator(const ASTContext &C, Expr *lhs, Expr *rhs, Opcode opc,
                         QualType ResType, ExprValueKind VK, ExprObjectKind OK,
                         SourceLocation OpLoc, FPOptionsOverride FPFeatures,
                         QualType CompLHSType, QualType CompResultType)
      : BinaryOperator(C, lhs, rhs, opc, ResType, VK, OK, OpLoc, FPFeatures,
                       true),
        ComputationLHSType(CompLHSType), ComputationResultType(CompResultType) {
    assert(isCompoundAssignmentOp() &&
           "Only should be used for compound assignments");
  }

public:
  static CompoundAssignOperator *CreateEmpty(const ASTContext &C,
                                             bool hasFPFeatures);

  static CompoundAssignOperator *
  Create(const ASTContext &C, Expr *lhs, Expr *rhs, Opcode opc, QualType ResTy,
         ExprValueKind VK, ExprObjectKind OK, SourceLocation opLoc,
         FPOptionsOverride FPFeatures, QualType CompLHSType = QualType(),
         QualType CompResultType = QualType());

  // The two computation types are the type the LHS is converted
  // to for the computation and the type of the result; the two are
  // distinct in a few cases (specifically, int+=ptr and ptr-=ptr).
  QualType getComputationLHSType() const { return ComputationLHSType; }
  void setComputationLHSType(QualType T) { ComputationLHSType = T; }

  QualType getComputationResultType() const { return ComputationResultType; }
  void setComputationResultType(QualType T) { ComputationResultType = T; }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == CompoundAssignOperatorClass;
  }
};

inline size_t BinaryOperator::offsetOfTrailingStorage() const {
  assert(BinaryOperatorBits.HasFPFeatures);
  return isa<CompoundAssignOperator>(this) ? sizeof(CompoundAssignOperator)
                                           : sizeof(BinaryOperator);
}

/// AbstractConditionalOperator - An abstract base class for
/// ConditionalOperator and BinaryConditionalOperator.
class AbstractConditionalOperator : public Expr {
  SourceLocation QuestionLoc, ColonLoc;
  friend class ASTStmtReader;

protected:
  AbstractConditionalOperator(StmtClass SC, QualType T, ExprValueKind VK,
                              ExprObjectKind OK, SourceLocation qloc,
                              SourceLocation cloc)
      : Expr(SC, T, VK, OK), QuestionLoc(qloc), ColonLoc(cloc) {}

  AbstractConditionalOperator(StmtClass SC, EmptyShell Empty)
    : Expr(SC, Empty) { }

public:
  /// getCond - Return the expression representing the condition for
  ///   the ?: operator.
  Expr *getCond() const;

  /// getTrueExpr - Return the subexpression representing the value of
  ///   the expression if the condition evaluates to true.
  Expr *getTrueExpr() const;

  /// getFalseExpr - Return the subexpression representing the value of
  ///   the expression if the condition evaluates to false.  This is
  ///   the same as getRHS.
  Expr *getFalseExpr() const;

  SourceLocation getQuestionLoc() const { return QuestionLoc; }
  SourceLocation getColonLoc() const { return ColonLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConditionalOperatorClass ||
           T->getStmtClass() == BinaryConditionalOperatorClass;
  }
};

/// ConditionalOperator - The ?: ternary operator.  The GNU "missing
/// middle" extension is a BinaryConditionalOperator.
class ConditionalOperator : public AbstractConditionalOperator {
  enum { COND, LHS, RHS, END_EXPR };
  Stmt* SubExprs[END_EXPR]; // Left/Middle/Right hand sides.

  friend class ASTStmtReader;
public:
  ConditionalOperator(Expr *cond, SourceLocation QLoc, Expr *lhs,
                      SourceLocation CLoc, Expr *rhs, QualType t,
                      ExprValueKind VK, ExprObjectKind OK)
      : AbstractConditionalOperator(ConditionalOperatorClass, t, VK, OK, QLoc,
                                    CLoc) {
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    setDependence(computeDependence(this));
  }

  /// Build an empty conditional operator.
  explicit ConditionalOperator(EmptyShell Empty)
    : AbstractConditionalOperator(ConditionalOperatorClass, Empty) { }

  /// getCond - Return the expression representing the condition for
  ///   the ?: operator.
  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }

  /// getTrueExpr - Return the subexpression representing the value of
  ///   the expression if the condition evaluates to true.
  Expr *getTrueExpr() const { return cast<Expr>(SubExprs[LHS]); }

  /// getFalseExpr - Return the subexpression representing the value of
  ///   the expression if the condition evaluates to false.  This is
  ///   the same as getRHS.
  Expr *getFalseExpr() const { return cast<Expr>(SubExprs[RHS]); }

  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCond()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getRHS()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConditionalOperatorClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

/// BinaryConditionalOperator - The GNU extension to the conditional
/// operator which allows the middle operand to be omitted.
///
/// This is a different expression kind on the assumption that almost
/// every client ends up needing to know that these are different.
class BinaryConditionalOperator : public AbstractConditionalOperator {
  enum { COMMON, COND, LHS, RHS, NUM_SUBEXPRS };

  /// - the common condition/left-hand-side expression, which will be
  ///   evaluated as the opaque value
  /// - the condition, expressed in terms of the opaque value
  /// - the left-hand-side, expressed in terms of the opaque value
  /// - the right-hand-side
  Stmt *SubExprs[NUM_SUBEXPRS];
  OpaqueValueExpr *OpaqueValue;

  friend class ASTStmtReader;
public:
  BinaryConditionalOperator(Expr *common, OpaqueValueExpr *opaqueValue,
                            Expr *cond, Expr *lhs, Expr *rhs,
                            SourceLocation qloc, SourceLocation cloc,
                            QualType t, ExprValueKind VK, ExprObjectKind OK)
      : AbstractConditionalOperator(BinaryConditionalOperatorClass, t, VK, OK,
                                    qloc, cloc),
        OpaqueValue(opaqueValue) {
    SubExprs[COMMON] = common;
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    assert(OpaqueValue->getSourceExpr() == common && "Wrong opaque value");
    setDependence(computeDependence(this));
  }

  /// Build an empty conditional operator.
  explicit BinaryConditionalOperator(EmptyShell Empty)
    : AbstractConditionalOperator(BinaryConditionalOperatorClass, Empty) { }

  /// getCommon - Return the common expression, written to the
  ///   left of the condition.  The opaque value will be bound to the
  ///   result of this expression.
  Expr *getCommon() const { return cast<Expr>(SubExprs[COMMON]); }

  /// getOpaqueValue - Return the opaque value placeholder.
  OpaqueValueExpr *getOpaqueValue() const { return OpaqueValue; }

  /// getCond - Return the condition expression; this is defined
  ///   in terms of the opaque value.
  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }

  /// getTrueExpr - Return the subexpression which will be
  ///   evaluated if the condition evaluates to true;  this is defined
  ///   in terms of the opaque value.
  Expr *getTrueExpr() const {
    return cast<Expr>(SubExprs[LHS]);
  }

  /// getFalseExpr - Return the subexpression which will be
  ///   evaluated if the condition evaluates to false; this is
  ///   defined in terms of the opaque value.
  Expr *getFalseExpr() const {
    return cast<Expr>(SubExprs[RHS]);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCommon()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getFalseExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BinaryConditionalOperatorClass;
  }

  // Iterators
  child_range children() {
    return child_range(SubExprs, SubExprs + NUM_SUBEXPRS);
  }
  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + NUM_SUBEXPRS);
  }
};

inline Expr *AbstractConditionalOperator::getCond() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getCond();
  return cast<BinaryConditionalOperator>(this)->getCond();
}

inline Expr *AbstractConditionalOperator::getTrueExpr() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getTrueExpr();
  return cast<BinaryConditionalOperator>(this)->getTrueExpr();
}

inline Expr *AbstractConditionalOperator::getFalseExpr() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getFalseExpr();
  return cast<BinaryConditionalOperator>(this)->getFalseExpr();
}

/// AddrLabelExpr - The GNU address of label extension, representing &&label.
class AddrLabelExpr : public Expr {
  SourceLocation AmpAmpLoc, LabelLoc;
  LabelDecl *Label;
public:
  AddrLabelExpr(SourceLocation AALoc, SourceLocation LLoc, LabelDecl *L,
                QualType t)
      : Expr(AddrLabelExprClass, t, VK_PRValue, OK_Ordinary), AmpAmpLoc(AALoc),
        LabelLoc(LLoc), Label(L) {
    setDependence(ExprDependence::None);
  }

  /// Build an empty address of a label expression.
  explicit AddrLabelExpr(EmptyShell Empty)
    : Expr(AddrLabelExprClass, Empty) { }

  SourceLocation getAmpAmpLoc() const { return AmpAmpLoc; }
  void setAmpAmpLoc(SourceLocation L) { AmpAmpLoc = L; }
  SourceLocation getLabelLoc() const { return LabelLoc; }
  void setLabelLoc(SourceLocation L) { LabelLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AmpAmpLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return LabelLoc; }

  LabelDecl *getLabel() const { return Label; }
  void setLabel(LabelDecl *L) { Label = L; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AddrLabelExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

/// StmtExpr - This is the GNU Statement Expression extension: ({int X=4; X;}).
/// The StmtExpr contains a single CompoundStmt node, which it evaluates and
/// takes the value of the last subexpression.
///
/// A StmtExpr is always an r-value; values "returned" out of a
/// StmtExpr will be copied.
class StmtExpr : public Expr {
  Stmt *SubStmt;
  SourceLocation LParenLoc, RParenLoc;
public:
  StmtExpr(CompoundStmt *SubStmt, QualType T, SourceLocation LParenLoc,
           SourceLocation RParenLoc, unsigned TemplateDepth)
      : Expr(StmtExprClass, T, VK_PRValue, OK_Ordinary), SubStmt(SubStmt),
        LParenLoc(LParenLoc), RParenLoc(RParenLoc) {
    setDependence(computeDependence(this, TemplateDepth));
    // FIXME: A templated statement expression should have an associated
    // DeclContext so that nested declarations always have a dependent context.
    StmtExprBits.TemplateDepth = TemplateDepth;
  }

  /// Build an empty statement expression.
  explicit StmtExpr(EmptyShell Empty) : Expr(StmtExprClass, Empty) { }

  CompoundStmt *getSubStmt() { return cast<CompoundStmt>(SubStmt); }
  const CompoundStmt *getSubStmt() const { return cast<CompoundStmt>(SubStmt); }
  void setSubStmt(CompoundStmt *S) { SubStmt = S; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LParenLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  unsigned getTemplateDepth() const { return StmtExprBits.TemplateDepth; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StmtExprClass;
  }

  // Iterators
  child_range children() { return child_range(&SubStmt, &SubStmt+1); }
  const_child_range children() const {
    return const_child_range(&SubStmt, &SubStmt + 1);
  }
};

/// ShuffleVectorExpr - clang-specific builtin-in function
/// __builtin_shufflevector.
/// This AST node represents a operator that does a constant
/// shuffle, similar to LLVM's shufflevector instruction. It takes
/// two vectors and a variable number of constant indices,
/// and returns the appropriately shuffled vector.
class ShuffleVectorExpr : public Expr {
  SourceLocation BuiltinLoc, RParenLoc;

  // SubExprs - the list of values passed to the __builtin_shufflevector
  // function. The first two are vectors, and the rest are constant
  // indices.  The number of values in this list is always
  // 2+the number of indices in the vector type.
  Stmt **SubExprs;
  unsigned NumExprs;

public:
  ShuffleVectorExpr(const ASTContext &C, ArrayRef<Expr*> args, QualType Type,
                    SourceLocation BLoc, SourceLocation RP);

  /// Build an empty vector-shuffle expression.
  explicit ShuffleVectorExpr(EmptyShell Empty)
    : Expr(ShuffleVectorExprClass, Empty), SubExprs(nullptr) { }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ShuffleVectorExprClass;
  }

  /// getNumSubExprs - Return the size of the SubExprs array.  This includes the
  /// constant expression, the actual arguments passed in, and the function
  /// pointers.
  unsigned getNumSubExprs() const { return NumExprs; }

  /// Retrieve the array of expressions.
  Expr **getSubExprs() { return reinterpret_cast<Expr **>(SubExprs); }

  /// getExpr - Return the Expr at the specified index.
  Expr *getExpr(unsigned Index) {
    assert((Index < NumExprs) && "Arg access out of range!");
    return cast<Expr>(SubExprs[Index]);
  }
  const Expr *getExpr(unsigned Index) const {
    assert((Index < NumExprs) && "Arg access out of range!");
    return cast<Expr>(SubExprs[Index]);
  }

  void setExprs(const ASTContext &C, ArrayRef<Expr *> Exprs);

  llvm::APSInt getShuffleMaskIdx(const ASTContext &Ctx, unsigned N) const {
    assert((N < NumExprs - 2) && "Shuffle idx out of range!");
    return getExpr(N+2)->EvaluateKnownConstInt(Ctx);
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+NumExprs);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + NumExprs);
  }
};

/// ConvertVectorExpr - Clang builtin function __builtin_convertvector
/// This AST node provides support for converting a vector type to another
/// vector type of the same arity.
class ConvertVectorExpr : public Expr {
private:
  Stmt *SrcExpr;
  TypeSourceInfo *TInfo;
  SourceLocation BuiltinLoc, RParenLoc;

  friend class ASTReader;
  friend class ASTStmtReader;
  explicit ConvertVectorExpr(EmptyShell Empty) : Expr(ConvertVectorExprClass, Empty) {}

public:
  ConvertVectorExpr(Expr *SrcExpr, TypeSourceInfo *TI, QualType DstType,
                    ExprValueKind VK, ExprObjectKind OK,
                    SourceLocation BuiltinLoc, SourceLocation RParenLoc)
      : Expr(ConvertVectorExprClass, DstType, VK, OK), SrcExpr(SrcExpr),
        TInfo(TI), BuiltinLoc(BuiltinLoc), RParenLoc(RParenLoc) {
    setDependence(computeDependence(this));
  }

  /// getSrcExpr - Return the Expr to be converted.
  Expr *getSrcExpr() const { return cast<Expr>(SrcExpr); }

  /// getTypeSourceInfo - Return the destination type.
  TypeSourceInfo *getTypeSourceInfo() const {
    return TInfo;
  }
  void setTypeSourceInfo(TypeSourceInfo *ti) {
    TInfo = ti;
  }

  /// getBuiltinLoc - Return the location of the __builtin_convertvector token.
  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }

  /// getRParenLoc - Return the location of final right parenthesis.
  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConvertVectorExprClass;
  }

  // Iterators
  child_range children() { return child_range(&SrcExpr, &SrcExpr+1); }
  const_child_range children() const {
    return const_child_range(&SrcExpr, &SrcExpr + 1);
  }
};

/// ChooseExpr - GNU builtin-in function __builtin_choose_expr.
/// This AST node is similar to the conditional operator (?:) in C, with
/// the following exceptions:
/// - the test expression must be a integer constant expression.
/// - the expression returned acts like the chosen subexpression in every
///   visible way: the type is the same as that of the chosen subexpression,
///   and all predicates (whether it's an l-value, whether it's an integer
///   constant expression, etc.) return the same result as for the chosen
///   sub-expression.
class ChooseExpr : public Expr {
  enum { COND, LHS, RHS, END_EXPR };
  Stmt* SubExprs[END_EXPR]; // Left/Middle/Right hand sides.
  SourceLocation BuiltinLoc, RParenLoc;
  bool CondIsTrue;
public:
  ChooseExpr(SourceLocation BLoc, Expr *cond, Expr *lhs, Expr *rhs, QualType t,
             ExprValueKind VK, ExprObjectKind OK, SourceLocation RP,
             bool condIsTrue)
      : Expr(ChooseExprClass, t, VK, OK), BuiltinLoc(BLoc), RParenLoc(RP),
        CondIsTrue(condIsTrue) {
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;

    setDependence(computeDependence(this));
  }

  /// Build an empty __builtin_choose_expr.
  explicit ChooseExpr(EmptyShell Empty) : Expr(ChooseExprClass, Empty) { }

  /// isConditionTrue - Return whether the condition is true (i.e. not
  /// equal to zero).
  bool isConditionTrue() const {
    assert(!isConditionDependent() &&
           "Dependent condition isn't true or false");
    return CondIsTrue;
  }
  void setIsConditionTrue(bool isTrue) { CondIsTrue = isTrue; }

  bool isConditionDependent() const {
    return getCond()->isTypeDependent() || getCond()->isValueDependent();
  }

  /// getChosenSubExpr - Return the subexpression chosen according to the
  /// condition.
  Expr *getChosenSubExpr() const {
    return isConditionTrue() ? getLHS() : getRHS();
  }

  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }
  void setCond(Expr *E) { SubExprs[COND] = E; }
  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ChooseExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

/// GNUNullExpr - Implements the GNU __null extension, which is a name
/// for a null pointer constant that has integral type (e.g., int or
/// long) and is the same size and alignment as a pointer. The __null
/// extension is typically only used by system headers, which define
/// NULL as __null in C++ rather than using 0 (which is an integer
/// that may not match the size of a pointer).
class GNUNullExpr : public Expr {
  /// TokenLoc - The location of the __null keyword.
  SourceLocation TokenLoc;

public:
  GNUNullExpr(QualType Ty, SourceLocation Loc)
      : Expr(GNUNullExprClass, Ty, VK_PRValue, OK_Ordinary), TokenLoc(Loc) {
    setDependence(ExprDependence::None);
  }

  /// Build an empty GNU __null expression.
  explicit GNUNullExpr(EmptyShell Empty) : Expr(GNUNullExprClass, Empty) { }

  /// getTokenLocation - The location of the __null token.
  SourceLocation getTokenLocation() const { return TokenLoc; }
  void setTokenLocation(SourceLocation L) { TokenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return TokenLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return TokenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GNUNullExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

/// Represents a call to the builtin function \c __builtin_va_arg.
class VAArgExpr : public Expr {
  Stmt *Val;
  llvm::PointerIntPair<TypeSourceInfo *, 1, bool> TInfo;
  SourceLocation BuiltinLoc, RParenLoc;
public:
  VAArgExpr(SourceLocation BLoc, Expr *e, TypeSourceInfo *TInfo,
            SourceLocation RPLoc, QualType t, bool IsMS)
      : Expr(VAArgExprClass, t, VK_PRValue, OK_Ordinary), Val(e),
        TInfo(TInfo, IsMS), BuiltinLoc(BLoc), RParenLoc(RPLoc) {
    setDependence(computeDependence(this));
  }

  /// Create an empty __builtin_va_arg expression.
  explicit VAArgExpr(EmptyShell Empty)
      : Expr(VAArgExprClass, Empty), Val(nullptr), TInfo(nullptr, false) {}

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  /// Returns whether this is really a Win64 ABI va_arg expression.
  bool isMicrosoftABI() const { return TInfo.getInt(); }
  void setIsMicrosoftABI(bool IsMS) { TInfo.setInt(IsMS); }

  TypeSourceInfo *getWrittenTypeInfo() const { return TInfo.getPointer(); }
  void setWrittenTypeInfo(TypeSourceInfo *TI) { TInfo.setPointer(TI); }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == VAArgExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }
};

enum class SourceLocIdentKind {
  Function,
  FuncSig,
  File,
  FileName,
  Line,
  Column,
  SourceLocStruct
};

/// Represents a function call to one of __builtin_LINE(), __builtin_COLUMN(),
/// __builtin_FUNCTION(), __builtin_FUNCSIG(), __builtin_FILE(),
/// __builtin_FILE_NAME() or __builtin_source_location().
class SourceLocExpr final : public Expr {
  SourceLocation BuiltinLoc, RParenLoc;
  DeclContext *ParentContext;

public:
  SourceLocExpr(const ASTContext &Ctx, SourceLocIdentKind Type,
                QualType ResultTy, SourceLocation BLoc,
                SourceLocation RParenLoc, DeclContext *Context);

  /// Build an empty call expression.
  explicit SourceLocExpr(EmptyShell Empty) : Expr(SourceLocExprClass, Empty) {}

  /// Return the result of evaluating this SourceLocExpr in the specified
  /// (and possibly null) default argument or initialization context.
  APValue EvaluateInContext(const ASTContext &Ctx,
                            const Expr *DefaultExpr) const;

  /// Return a string representing the name of the specific builtin function.
  StringRef getBuiltinStr() const;

  SourceLocIdentKind getIdentKind() const {
    return static_cast<SourceLocIdentKind>(SourceLocExprBits.Kind);
  }

  bool isIntType() const {
    switch (getIdentKind()) {
    case SourceLocIdentKind::File:
    case SourceLocIdentKind::FileName:
    case SourceLocIdentKind::Function:
    case SourceLocIdentKind::FuncSig:
    case SourceLocIdentKind::SourceLocStruct:
      return false;
    case SourceLocIdentKind::Line:
    case SourceLocIdentKind::Column:
      return true;
    }
    llvm_unreachable("unknown source location expression kind");
  }

  /// If the SourceLocExpr has been resolved return the subexpression
  /// representing the resolved value. Otherwise return null.
  const DeclContext *getParentContext() const { return ParentContext; }
  DeclContext *getParentContext() { return ParentContext; }

  SourceLocation getLocation() const { return BuiltinLoc; }
  SourceLocation getBeginLoc() const { return BuiltinLoc; }
  SourceLocation getEndLoc() const { return RParenLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(child_iterator(), child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SourceLocExprClass;
  }

  static bool MayBeDependent(SourceLocIdentKind Kind) {
    switch (Kind) {
    case SourceLocIdentKind::Function:
    case SourceLocIdentKind::FuncSig:
    case SourceLocIdentKind::SourceLocStruct:
      return true;
    default:
      return false;
    }
  }

private:
  friend class ASTStmtReader;
};

/// Stores data related to a single #embed directive.
struct EmbedDataStorage {
  StringLiteral *BinaryData;
  size_t getDataElementCount() const { return BinaryData->getByteLength(); }
};

/// Represents a reference to #emded data. By default, this references the whole
/// range. Otherwise it represents a subrange of data imported by #embed
/// directive. Needed to handle nested initializer lists with #embed directives.
/// Example:
///  struct S {
///    int x, y;
///  };
///
///  struct T {
///    int x[2];
///    struct S s
///  };
///
///  struct T t[] = {
///  #embed "data" // data contains 10 elements;
///  };
///
/// The resulting semantic form of initializer list will contain (EE stands
/// for EmbedExpr):
///  { {EE(first two data elements), {EE(3rd element), EE(4th element) }},
///  { {EE(5th and 6th element), {EE(7th element), EE(8th element) }},
///  { {EE(9th and 10th element), { zeroinitializer }}}
///
/// EmbedExpr inside of a semantic initializer list and referencing more than
/// one element can only appear for arrays of scalars.
class EmbedExpr final : public Expr {
  SourceLocation EmbedKeywordLoc;
  IntegerLiteral *FakeChildNode = nullptr;
  const ASTContext *Ctx = nullptr;
  EmbedDataStorage *Data;
  unsigned Begin = 0;
  unsigned NumOfElements;

public:
  EmbedExpr(const ASTContext &Ctx, SourceLocation Loc, EmbedDataStorage *Data,
            unsigned Begin, unsigned NumOfElements);
  explicit EmbedExpr(EmptyShell Empty) : Expr(SourceLocExprClass, Empty) {}

  SourceLocation getLocation() const { return EmbedKeywordLoc; }
  SourceLocation getBeginLoc() const { return EmbedKeywordLoc; }
  SourceLocation getEndLoc() const { return EmbedKeywordLoc; }

  StringLiteral *getDataStringLiteral() const { return Data->BinaryData; }
  EmbedDataStorage *getData() const { return Data; }

  unsigned getStartingElementPos() const { return Begin; }
  size_t getDataElementCount() const { return NumOfElements; }

  // Allows accessing every byte of EmbedExpr data and iterating over it.
  // An Iterator knows the EmbedExpr that it refers to, and an offset value
  // within the data.
  // Dereferencing an Iterator results in construction of IntegerLiteral AST
  // node filled with byte of data of the corresponding EmbedExpr within offset
  // that the Iterator currently has.
  template <bool Const>
  class ChildElementIter
      : public llvm::iterator_facade_base<
            ChildElementIter<Const>, std::random_access_iterator_tag,
            std::conditional_t<Const, const IntegerLiteral *,
                               IntegerLiteral *>> {
    friend class EmbedExpr;

    EmbedExpr *EExpr = nullptr;
    unsigned long long CurOffset = ULLONG_MAX;
    using BaseTy = typename ChildElementIter::iterator_facade_base;

    ChildElementIter(EmbedExpr *E) : EExpr(E) {
      if (E)
        CurOffset = E->getStartingElementPos();
    }

  public:
    ChildElementIter() : CurOffset(ULLONG_MAX) {}
    typename BaseTy::reference operator*() const {
      assert(EExpr && CurOffset != ULLONG_MAX &&
             "trying to dereference an invalid iterator");
      IntegerLiteral *N = EExpr->FakeChildNode;
      StringRef DataRef = EExpr->Data->BinaryData->getBytes();
      N->setValue(*EExpr->Ctx,
                  llvm::APInt(N->getValue().getBitWidth(), DataRef[CurOffset],
                              N->getType()->isSignedIntegerType()));
      // We want to return a reference to the fake child node in the
      // EmbedExpr, not the local variable N.
      return const_cast<typename BaseTy::reference>(EExpr->FakeChildNode);
    }
    typename BaseTy::pointer operator->() const { return **this; }
    using BaseTy::operator++;
    ChildElementIter &operator++() {
      assert(EExpr && "trying to increment an invalid iterator");
      assert(CurOffset != ULLONG_MAX &&
             "Already at the end of what we can iterate over");
      if (++CurOffset >=
          EExpr->getDataElementCount() + EExpr->getStartingElementPos()) {
        CurOffset = ULLONG_MAX;
        EExpr = nullptr;
      }
      return *this;
    }
    bool operator==(ChildElementIter Other) const {
      return (EExpr == Other.EExpr && CurOffset == Other.CurOffset);
    }
  }; // class ChildElementIter

public:
  using fake_child_range = llvm::iterator_range<ChildElementIter<false>>;
  using const_fake_child_range = llvm::iterator_range<ChildElementIter<true>>;

  fake_child_range underlying_data_elements() {
    return fake_child_range(ChildElementIter<false>(this),
                            ChildElementIter<false>());
  }

  const_fake_child_range underlying_data_elements() const {
    return const_fake_child_range(
        ChildElementIter<true>(const_cast<EmbedExpr *>(this)),
        ChildElementIter<true>());
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == EmbedExprClass;
  }

  ChildElementIter<false> begin() { return ChildElementIter<false>(this); }

  ChildElementIter<true> begin() const {
    return ChildElementIter<true>(const_cast<EmbedExpr *>(this));
  }

  template <typename Call, typename... Targs>
  bool doForEachDataElement(Call &&C, unsigned &StartingIndexInArray,
                            Targs &&...Fargs) const {
    for (auto It : underlying_data_elements()) {
      if (!std::invoke(std::forward<Call>(C), const_cast<IntegerLiteral *>(It),
                       StartingIndexInArray, std::forward<Targs>(Fargs)...))
        return false;
      StartingIndexInArray++;
    }
    return true;
  }

private:
  friend class ASTStmtReader;
};

/// Describes an C or C++ initializer list.
///
/// InitListExpr describes an initializer list, which can be used to
/// initialize objects of different types, including
/// struct/class/union types, arrays, and vectors. For example:
///
/// @code
/// struct foo x = { 1, { 2, 3 } };
/// @endcode
///
/// Prior to semantic analysis, an initializer list will represent the
/// initializer list as written by the user, but will have the
/// placeholder type "void". This initializer list is called the
/// syntactic form of the initializer, and may contain C99 designated
/// initializers (represented as DesignatedInitExprs), initializations
/// of subobject members without explicit braces, and so on. Clients
/// interested in the original syntax of the initializer list should
/// use the syntactic form of the initializer list.
///
/// After semantic analysis, the initializer list will represent the
/// semantic form of the initializer, where the initializations of all
/// subobjects are made explicit with nested InitListExpr nodes and
/// C99 designators have been eliminated by placing the designated
/// initializations into the subobject they initialize. Additionally,
/// any "holes" in the initialization, where no initializer has been
/// specified for a particular subobject, will be replaced with
/// implicitly-generated ImplicitValueInitExpr expressions that
/// value-initialize the subobjects. Note, however, that the
/// initializer lists may still have fewer initializers than there are
/// elements to initialize within the object.
///
/// After semantic analysis has completed, given an initializer list,
/// method isSemanticForm() returns true if and only if this is the
/// semantic form of the initializer list (note: the same AST node
/// may at the same time be the syntactic form).
/// Given the semantic form of the initializer list, one can retrieve
/// the syntactic form of that initializer list (when different)
/// using method getSyntacticForm(); the method returns null if applied
/// to a initializer list which is already in syntactic form.
/// Similarly, given the syntactic form (i.e., an initializer list such
/// that isSemanticForm() returns false), one can retrieve the semantic
/// form using method getSemanticForm().
/// Since many initializer lists have the same syntactic and semantic forms,
/// getSyntacticForm() may return NULL, indicating that the current
/// semantic initializer list also serves as its syntactic form.
class InitListExpr : public Expr {
  // FIXME: Eliminate this vector in favor of ASTContext allocation
  typedef ASTVector<Stmt *> InitExprsTy;
  InitExprsTy InitExprs;
  SourceLocation LBraceLoc, RBraceLoc;

  /// The alternative form of the initializer list (if it exists).
  /// The int part of the pair stores whether this initializer list is
  /// in semantic form. If not null, the pointer points to:
  ///   - the syntactic form, if this is in semantic form;
  ///   - the semantic form, if this is in syntactic form.
  llvm::PointerIntPair<InitListExpr *, 1, bool> AltForm;

  /// Either:
  ///  If this initializer list initializes an array with more elements than
  ///  there are initializers in the list, specifies an expression to be used
  ///  for value initialization of the rest of the elements.
  /// Or
  ///  If this initializer list initializes a union, specifies which
  ///  field within the union will be initialized.
  llvm::PointerUnion<Expr *, FieldDecl *> ArrayFillerOrUnionFieldInit;

public:
  InitListExpr(const ASTContext &C, SourceLocation lbraceloc,
               ArrayRef<Expr*> initExprs, SourceLocation rbraceloc);

  /// Build an empty initializer list.
  explicit InitListExpr(EmptyShell Empty)
    : Expr(InitListExprClass, Empty), AltForm(nullptr, true) { }

  unsigned getNumInits() const { return InitExprs.size(); }

  /// Retrieve the set of initializers.
  Expr **getInits() { return reinterpret_cast<Expr **>(InitExprs.data()); }

  /// Retrieve the set of initializers.
  Expr * const *getInits() const {
    return reinterpret_cast<Expr * const *>(InitExprs.data());
  }

  ArrayRef<Expr *> inits() { return llvm::ArrayRef(getInits(), getNumInits()); }

  ArrayRef<Expr *> inits() const {
    return llvm::ArrayRef(getInits(), getNumInits());
  }

  const Expr *getInit(unsigned Init) const {
    assert(Init < getNumInits() && "Initializer access out of range!");
    return cast_or_null<Expr>(InitExprs[Init]);
  }

  Expr *getInit(unsigned Init) {
    assert(Init < getNumInits() && "Initializer access out of range!");
    return cast_or_null<Expr>(InitExprs[Init]);
  }

  void setInit(unsigned Init, Expr *expr) {
    assert(Init < getNumInits() && "Initializer access out of range!");
    InitExprs[Init] = expr;

    if (expr)
      setDependence(getDependence() | expr->getDependence());
  }

  /// Mark the semantic form of the InitListExpr as error when the semantic
  /// analysis fails.
  void markError() {
    assert(isSemanticForm());
    setDependence(getDependence() | ExprDependence::ErrorDependent);
  }

  /// Reserve space for some number of initializers.
  void reserveInits(const ASTContext &C, unsigned NumInits);

  /// Specify the number of initializers
  ///
  /// If there are more than @p NumInits initializers, the remaining
  /// initializers will be destroyed. If there are fewer than @p
  /// NumInits initializers, NULL expressions will be added for the
  /// unknown initializers.
  void resizeInits(const ASTContext &Context, unsigned NumInits);

  /// Updates the initializer at index @p Init with the new
  /// expression @p expr, and returns the old expression at that
  /// location.
  ///
  /// When @p Init is out of range for this initializer list, the
  /// initializer list will be extended with NULL expressions to
  /// accommodate the new entry.
  Expr *updateInit(const ASTContext &C, unsigned Init, Expr *expr);

  /// If this initializer list initializes an array with more elements
  /// than there are initializers in the list, specifies an expression to be
  /// used for value initialization of the rest of the elements.
  Expr *getArrayFiller() {
    return ArrayFillerOrUnionFieldInit.dyn_cast<Expr *>();
  }
  const Expr *getArrayFiller() const {
    return const_cast<InitListExpr *>(this)->getArrayFiller();
  }
  void setArrayFiller(Expr *filler);

  /// Return true if this is an array initializer and its array "filler"
  /// has been set.
  bool hasArrayFiller() const { return getArrayFiller(); }

  /// Determine whether this initializer list contains a designated initializer.
  bool hasDesignatedInit() const {
    return std::any_of(begin(), end(), [](const Stmt *S) {
      return isa<DesignatedInitExpr>(S);
    });
  }

  /// If this initializes a union, specifies which field in the
  /// union to initialize.
  ///
  /// Typically, this field is the first named field within the
  /// union. However, a designated initializer can specify the
  /// initialization of a different field within the union.
  FieldDecl *getInitializedFieldInUnion() {
    return ArrayFillerOrUnionFieldInit.dyn_cast<FieldDecl *>();
  }
  const FieldDecl *getInitializedFieldInUnion() const {
    return const_cast<InitListExpr *>(this)->getInitializedFieldInUnion();
  }
  void setInitializedFieldInUnion(FieldDecl *FD) {
    assert((FD == nullptr
            || getInitializedFieldInUnion() == nullptr
            || getInitializedFieldInUnion() == FD)
           && "Only one field of a union may be initialized at a time!");
    ArrayFillerOrUnionFieldInit = FD;
  }

  // Explicit InitListExpr's originate from source code (and have valid source
  // locations). Implicit InitListExpr's are created by the semantic analyzer.
  // FIXME: This is wrong; InitListExprs created by semantic analysis have
  // valid source locations too!
  bool isExplicit() const {
    return LBraceLoc.isValid() && RBraceLoc.isValid();
  }

  /// Is this an initializer for an array of characters, initialized by a string
  /// literal or an @encode?
  bool isStringLiteralInit() const;

  /// Is this a transparent initializer list (that is, an InitListExpr that is
  /// purely syntactic, and whose semantics are that of the sole contained
  /// initializer)?
  bool isTransparent() const;

  /// Is this the zero initializer {0} in a language which considers it
  /// idiomatic?
  bool isIdiomaticZeroInitializer(const LangOptions &LangOpts) const;

  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  void setLBraceLoc(SourceLocation Loc) { LBraceLoc = Loc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setRBraceLoc(SourceLocation Loc) { RBraceLoc = Loc; }

  bool isSemanticForm() const { return AltForm.getInt(); }
  InitListExpr *getSemanticForm() const {
    return isSemanticForm() ? nullptr : AltForm.getPointer();
  }
  bool isSyntacticForm() const {
    return !AltForm.getInt() || !AltForm.getPointer();
  }
  InitListExpr *getSyntacticForm() const {
    return isSemanticForm() ? AltForm.getPointer() : nullptr;
  }

  void setSyntacticForm(InitListExpr *Init) {
    AltForm.setPointer(Init);
    AltForm.setInt(true);
    Init->AltForm.setPointer(this);
    Init->AltForm.setInt(false);
  }

  bool hadArrayRangeDesignator() const {
    return InitListExprBits.HadArrayRangeDesignator != 0;
  }
  void sawArrayRangeDesignator(bool ARD = true) {
    InitListExprBits.HadArrayRangeDesignator = ARD;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == InitListExprClass;
  }

  // Iterators
  child_range children() {
    const_child_range CCR = const_cast<const InitListExpr *>(this)->children();
    return child_range(cast_away_const(CCR.begin()),
                       cast_away_const(CCR.end()));
  }

  const_child_range children() const {
    // FIXME: This does not include the array filler expression.
    if (InitExprs.empty())
      return const_child_range(const_child_iterator(), const_child_iterator());
    return const_child_range(&InitExprs[0], &InitExprs[0] + InitExprs.size());
  }

  typedef InitExprsTy::iterator iterator;
  typedef InitExprsTy::const_iterator const_iterator;
  typedef InitExprsTy::reverse_iterator reverse_iterator;
  typedef InitExprsTy::const_reverse_iterator const_reverse_iterator;

  iterator begin() { return InitExprs.begin(); }
  const_iterator begin() const { return InitExprs.begin(); }
  iterator end() { return InitExprs.end(); }
  const_iterator end() const { return InitExprs.end(); }
  reverse_iterator rbegin() { return InitExprs.rbegin(); }
  const_reverse_iterator rbegin() const { return InitExprs.rbegin(); }
  reverse_iterator rend() { return InitExprs.rend(); }
  const_reverse_iterator rend() const { return InitExprs.rend(); }

  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

/// Represents a C99 designated initializer expression.
///
/// A designated initializer expression (C99 6.7.8) contains one or
/// more designators (which can be field designators, array
/// designators, or GNU array-range designators) followed by an
/// expression that initializes the field or element(s) that the
/// designators refer to. For example, given:
///
/// @code
/// struct point {
///   double x;
///   double y;
/// };
/// struct point ptarray[10] = { [2].y = 1.0, [2].x = 2.0, [0].x = 1.0 };
/// @endcode
///
/// The InitListExpr contains three DesignatedInitExprs, the first of
/// which covers @c [2].y=1.0. This DesignatedInitExpr will have two
/// designators, one array designator for @c [2] followed by one field
/// designator for @c .y. The initialization expression will be 1.0.
class DesignatedInitExpr final
    : public Expr,
      private llvm::TrailingObjects<DesignatedInitExpr, Stmt *> {
public:
  /// Forward declaration of the Designator class.
  class Designator;

private:
  /// The location of the '=' or ':' prior to the actual initializer
  /// expression.
  SourceLocation EqualOrColonLoc;

  /// Whether this designated initializer used the GNU deprecated
  /// syntax rather than the C99 '=' syntax.
  LLVM_PREFERRED_TYPE(bool)
  unsigned GNUSyntax : 1;

  /// The number of designators in this initializer expression.
  unsigned NumDesignators : 15;

  /// The number of subexpressions of this initializer expression,
  /// which contains both the initializer and any additional
  /// expressions used by array and array-range designators.
  unsigned NumSubExprs : 16;

  /// The designators in this designated initialization
  /// expression.
  Designator *Designators;

  DesignatedInitExpr(const ASTContext &C, QualType Ty,
                     llvm::ArrayRef<Designator> Designators,
                     SourceLocation EqualOrColonLoc, bool GNUSyntax,
                     ArrayRef<Expr *> IndexExprs, Expr *Init);

  explicit DesignatedInitExpr(unsigned NumSubExprs)
    : Expr(DesignatedInitExprClass, EmptyShell()),
      NumDesignators(0), NumSubExprs(NumSubExprs), Designators(nullptr) { }

public:
  /// Represents a single C99 designator.
  ///
  /// @todo This class is infuriatingly similar to clang::Designator,
  /// but minor differences (storing indices vs. storing pointers)
  /// keep us from reusing it. Try harder, later, to rectify these
  /// differences.
  class Designator {
    /// A field designator, e.g., ".x".
    struct FieldDesignatorInfo {
      /// Refers to the field that is being initialized. The low bit
      /// of this field determines whether this is actually a pointer
      /// to an IdentifierInfo (if 1) or a FieldDecl (if 0). When
      /// initially constructed, a field designator will store an
      /// IdentifierInfo*. After semantic analysis has resolved that
      /// name, the field designator will instead store a FieldDecl*.
      uintptr_t NameOrField;

      /// The location of the '.' in the designated initializer.
      SourceLocation DotLoc;

      /// The location of the field name in the designated initializer.
      SourceLocation FieldLoc;

      FieldDesignatorInfo(const IdentifierInfo *II, SourceLocation DotLoc,
                          SourceLocation FieldLoc)
          : NameOrField(reinterpret_cast<uintptr_t>(II) | 0x1), DotLoc(DotLoc),
            FieldLoc(FieldLoc) {}
    };

    /// An array or GNU array-range designator, e.g., "[9]" or "[10...15]".
    struct ArrayOrRangeDesignatorInfo {
      /// Location of the first index expression within the designated
      /// initializer expression's list of subexpressions.
      unsigned Index;

      /// The location of the '[' starting the array range designator.
      SourceLocation LBracketLoc;

      /// The location of the ellipsis separating the start and end
      /// indices. Only valid for GNU array-range designators.
      SourceLocation EllipsisLoc;

      /// The location of the ']' terminating the array range designator.
      SourceLocation RBracketLoc;

      ArrayOrRangeDesignatorInfo(unsigned Index, SourceLocation LBracketLoc,
                                 SourceLocation RBracketLoc)
          : Index(Index), LBracketLoc(LBracketLoc), RBracketLoc(RBracketLoc) {}

      ArrayOrRangeDesignatorInfo(unsigned Index,
                                 SourceLocation LBracketLoc,
                                 SourceLocation EllipsisLoc,
                                 SourceLocation RBracketLoc)
          : Index(Index), LBracketLoc(LBracketLoc), EllipsisLoc(EllipsisLoc),
            RBracketLoc(RBracketLoc) {}
    };

    /// The kind of designator this describes.
    enum DesignatorKind {
      FieldDesignator,
      ArrayDesignator,
      ArrayRangeDesignator
    };

    DesignatorKind Kind;

    union {
      /// A field designator, e.g., ".x".
      struct FieldDesignatorInfo FieldInfo;

      /// An array or GNU array-range designator, e.g., "[9]" or "[10..15]".
      struct ArrayOrRangeDesignatorInfo ArrayOrRangeInfo;
    };

    Designator(DesignatorKind Kind) : Kind(Kind) {}

  public:
    Designator() {}

    bool isFieldDesignator() const { return Kind == FieldDesignator; }
    bool isArrayDesignator() const { return Kind == ArrayDesignator; }
    bool isArrayRangeDesignator() const { return Kind == ArrayRangeDesignator; }

    //===------------------------------------------------------------------===//
    // FieldDesignatorInfo

    /// Creates a field designator.
    static Designator CreateFieldDesignator(const IdentifierInfo *FieldName,
                                            SourceLocation DotLoc,
                                            SourceLocation FieldLoc) {
      Designator D(FieldDesignator);
      new (&D.FieldInfo) FieldDesignatorInfo(FieldName, DotLoc, FieldLoc);
      return D;
    }

    const IdentifierInfo *getFieldName() const;

    FieldDecl *getFieldDecl() const {
      assert(isFieldDesignator() && "Only valid on a field designator");
      if (FieldInfo.NameOrField & 0x01)
        return nullptr;
      return reinterpret_cast<FieldDecl *>(FieldInfo.NameOrField);
    }

    void setFieldDecl(FieldDecl *FD) {
      assert(isFieldDesignator() && "Only valid on a field designator");
      FieldInfo.NameOrField = reinterpret_cast<uintptr_t>(FD);
    }

    SourceLocation getDotLoc() const {
      assert(isFieldDesignator() && "Only valid on a field designator");
      return FieldInfo.DotLoc;
    }

    SourceLocation getFieldLoc() const {
      assert(isFieldDesignator() && "Only valid on a field designator");
      return FieldInfo.FieldLoc;
    }

    //===------------------------------------------------------------------===//
    // ArrayOrRangeDesignator

    /// Creates an array designator.
    static Designator CreateArrayDesignator(unsigned Index,
                                            SourceLocation LBracketLoc,
                                            SourceLocation RBracketLoc) {
      Designator D(ArrayDesignator);
      new (&D.ArrayOrRangeInfo) ArrayOrRangeDesignatorInfo(Index, LBracketLoc,
                                                           RBracketLoc);
      return D;
    }

    /// Creates a GNU array-range designator.
    static Designator CreateArrayRangeDesignator(unsigned Index,
                                                 SourceLocation LBracketLoc,
                                                 SourceLocation EllipsisLoc,
                                                 SourceLocation RBracketLoc) {
      Designator D(ArrayRangeDesignator);
      new (&D.ArrayOrRangeInfo) ArrayOrRangeDesignatorInfo(Index, LBracketLoc,
                                                           EllipsisLoc,
                                                           RBracketLoc);
      return D;
    }

    unsigned getArrayIndex() const {
      assert((isArrayDesignator() || isArrayRangeDesignator()) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRangeInfo.Index;
    }

    SourceLocation getLBracketLoc() const {
      assert((isArrayDesignator() || isArrayRangeDesignator()) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRangeInfo.LBracketLoc;
    }

    SourceLocation getEllipsisLoc() const {
      assert(isArrayRangeDesignator() &&
             "Only valid on an array-range designator");
      return ArrayOrRangeInfo.EllipsisLoc;
    }

    SourceLocation getRBracketLoc() const {
      assert((isArrayDesignator() || isArrayRangeDesignator()) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRangeInfo.RBracketLoc;
    }

    SourceLocation getBeginLoc() const LLVM_READONLY {
      if (isFieldDesignator())
        return getDotLoc().isInvalid() ? getFieldLoc() : getDotLoc();
      return getLBracketLoc();
    }

    SourceLocation getEndLoc() const LLVM_READONLY {
      return isFieldDesignator() ? getFieldLoc() : getRBracketLoc();
    }

    SourceRange getSourceRange() const LLVM_READONLY {
      return SourceRange(getBeginLoc(), getEndLoc());
    }
  };

  static DesignatedInitExpr *Create(const ASTContext &C,
                                    llvm::ArrayRef<Designator> Designators,
                                    ArrayRef<Expr*> IndexExprs,
                                    SourceLocation EqualOrColonLoc,
                                    bool GNUSyntax, Expr *Init);

  static DesignatedInitExpr *CreateEmpty(const ASTContext &C,
                                         unsigned NumIndexExprs);

  /// Returns the number of designators in this initializer.
  unsigned size() const { return NumDesignators; }

  // Iterator access to the designators.
  llvm::MutableArrayRef<Designator> designators() {
    return {Designators, NumDesignators};
  }

  llvm::ArrayRef<Designator> designators() const {
    return {Designators, NumDesignators};
  }

  Designator *getDesignator(unsigned Idx) { return &designators()[Idx]; }
  const Designator *getDesignator(unsigned Idx) const {
    return &designators()[Idx];
  }

  void setDesignators(const ASTContext &C, const Designator *Desigs,
                      unsigned NumDesigs);

  Expr *getArrayIndex(const Designator &D) const;
  Expr *getArrayRangeStart(const Designator &D) const;
  Expr *getArrayRangeEnd(const Designator &D) const;

  /// Retrieve the location of the '=' that precedes the
  /// initializer value itself, if present.
  SourceLocation getEqualOrColonLoc() const { return EqualOrColonLoc; }
  void setEqualOrColonLoc(SourceLocation L) { EqualOrColonLoc = L; }

  /// Whether this designated initializer should result in direct-initialization
  /// of the designated subobject (eg, '{.foo{1, 2, 3}}').
  bool isDirectInit() const { return EqualOrColonLoc.isInvalid(); }

  /// Determines whether this designated initializer used the
  /// deprecated GNU syntax for designated initializers.
  bool usesGNUSyntax() const { return GNUSyntax; }
  void setGNUSyntax(bool GNU) { GNUSyntax = GNU; }

  /// Retrieve the initializer value.
  Expr *getInit() const {
    return cast<Expr>(*const_cast<DesignatedInitExpr*>(this)->child_begin());
  }

  void setInit(Expr *init) {
    *child_begin() = init;
  }

  /// Retrieve the total number of subexpressions in this
  /// designated initializer expression, including the actual
  /// initialized value and any expressions that occur within array
  /// and array-range designators.
  unsigned getNumSubExprs() const { return NumSubExprs; }

  Expr *getSubExpr(unsigned Idx) const {
    assert(Idx < NumSubExprs && "Subscript out of range");
    return cast<Expr>(getTrailingObjects<Stmt *>()[Idx]);
  }

  void setSubExpr(unsigned Idx, Expr *E) {
    assert(Idx < NumSubExprs && "Subscript out of range");
    getTrailingObjects<Stmt *>()[Idx] = E;
  }

  /// Replaces the designator at index @p Idx with the series
  /// of designators in [First, Last).
  void ExpandDesignator(const ASTContext &C, unsigned Idx,
                        const Designator *First, const Designator *Last);

  SourceRange getDesignatorsSourceRange() const;

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DesignatedInitExprClass;
  }

  // Iterators
  child_range children() {
    Stmt **begin = getTrailingObjects<Stmt *>();
    return child_range(begin, begin + NumSubExprs);
  }
  const_child_range children() const {
    Stmt * const *begin = getTrailingObjects<Stmt *>();
    return const_child_range(begin, begin + NumSubExprs);
  }

  friend TrailingObjects;
};

/// Represents a place-holder for an object not to be initialized by
/// anything.
///
/// This only makes sense when it appears as part of an updater of a
/// DesignatedInitUpdateExpr (see below). The base expression of a DIUE
/// initializes a big object, and the NoInitExpr's mark the spots within the
/// big object not to be overwritten by the updater.
///
/// \see DesignatedInitUpdateExpr
class NoInitExpr : public Expr {
public:
  explicit NoInitExpr(QualType ty)
      : Expr(NoInitExprClass, ty, VK_PRValue, OK_Ordinary) {
    setDependence(computeDependence(this));
  }

  explicit NoInitExpr(EmptyShell Empty)
    : Expr(NoInitExprClass, Empty) { }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == NoInitExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return SourceLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SourceLocation(); }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

// In cases like:
//   struct Q { int a, b, c; };
//   Q *getQ();
//   void foo() {
//     struct A { Q q; } a = { *getQ(), .q.b = 3 };
//   }
//
// We will have an InitListExpr for a, with type A, and then a
// DesignatedInitUpdateExpr for "a.q" with type Q. The "base" for this DIUE
// is the call expression *getQ(); the "updater" for the DIUE is ".q.b = 3"
//
class DesignatedInitUpdateExpr : public Expr {
  // BaseAndUpdaterExprs[0] is the base expression;
  // BaseAndUpdaterExprs[1] is an InitListExpr overwriting part of the base.
  Stmt *BaseAndUpdaterExprs[2];

public:
  DesignatedInitUpdateExpr(const ASTContext &C, SourceLocation lBraceLoc,
                           Expr *baseExprs, SourceLocation rBraceLoc);

  explicit DesignatedInitUpdateExpr(EmptyShell Empty)
    : Expr(DesignatedInitUpdateExprClass, Empty) { }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DesignatedInitUpdateExprClass;
  }

  Expr *getBase() const { return cast<Expr>(BaseAndUpdaterExprs[0]); }
  void setBase(Expr *Base) { BaseAndUpdaterExprs[0] = Base; }

  InitListExpr *getUpdater() const {
    return cast<InitListExpr>(BaseAndUpdaterExprs[1]);
  }
  void setUpdater(Expr *Updater) { BaseAndUpdaterExprs[1] = Updater; }

  // Iterators
  // children = the base and the updater
  child_range children() {
    return child_range(&BaseAndUpdaterExprs[0], &BaseAndUpdaterExprs[0] + 2);
  }
  const_child_range children() const {
    return const_child_range(&BaseAndUpdaterExprs[0],
                             &BaseAndUpdaterExprs[0] + 2);
  }
};

/// Represents a loop initializing the elements of an array.
///
/// The need to initialize the elements of an array occurs in a number of
/// contexts:
///
///  * in the implicit copy/move constructor for a class with an array member
///  * when a lambda-expression captures an array by value
///  * when a decomposition declaration decomposes an array
///
/// There are two subexpressions: a common expression (the source array)
/// that is evaluated once up-front, and a per-element initializer that
/// runs once for each array element.
///
/// Within the per-element initializer, the common expression may be referenced
/// via an OpaqueValueExpr, and the current index may be obtained via an
/// ArrayInitIndexExpr.
class ArrayInitLoopExpr : public Expr {
  Stmt *SubExprs[2];

  explicit ArrayInitLoopExpr(EmptyShell Empty)
      : Expr(ArrayInitLoopExprClass, Empty), SubExprs{} {}

public:
  explicit ArrayInitLoopExpr(QualType T, Expr *CommonInit, Expr *ElementInit)
      : Expr(ArrayInitLoopExprClass, T, VK_PRValue, OK_Ordinary),
        SubExprs{CommonInit, ElementInit} {
    setDependence(computeDependence(this));
  }

  /// Get the common subexpression shared by all initializations (the source
  /// array).
  OpaqueValueExpr *getCommonExpr() const {
    return cast<OpaqueValueExpr>(SubExprs[0]);
  }

  /// Get the initializer to use for each array element.
  Expr *getSubExpr() const { return cast<Expr>(SubExprs[1]); }

  llvm::APInt getArraySize() const {
    return cast<ConstantArrayType>(getType()->castAsArrayTypeUnsafe())
        ->getSize();
  }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == ArrayInitLoopExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCommonExpr()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getCommonExpr()->getEndLoc();
  }

  child_range children() {
    return child_range(SubExprs, SubExprs + 2);
  }
  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + 2);
  }

  friend class ASTReader;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

/// Represents the index of the current element of an array being
/// initialized by an ArrayInitLoopExpr. This can only appear within the
/// subexpression of an ArrayInitLoopExpr.
class ArrayInitIndexExpr : public Expr {
  explicit ArrayInitIndexExpr(EmptyShell Empty)
      : Expr(ArrayInitIndexExprClass, Empty) {}

public:
  explicit ArrayInitIndexExpr(QualType T)
      : Expr(ArrayInitIndexExprClass, T, VK_PRValue, OK_Ordinary) {
    setDependence(ExprDependence::None);
  }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == ArrayInitIndexExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return SourceLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SourceLocation(); }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  friend class ASTReader;
  friend class ASTStmtReader;
};

/// Represents an implicitly-generated value initialization of
/// an object of a given type.
///
/// Implicit value initializations occur within semantic initializer
/// list expressions (InitListExpr) as placeholders for subobject
/// initializations not explicitly specified by the user.
///
/// \see InitListExpr
class ImplicitValueInitExpr : public Expr {
public:
  explicit ImplicitValueInitExpr(QualType ty)
      : Expr(ImplicitValueInitExprClass, ty, VK_PRValue, OK_Ordinary) {
    setDependence(computeDependence(this));
  }

  /// Construct an empty implicit value initialization.
  explicit ImplicitValueInitExpr(EmptyShell Empty)
    : Expr(ImplicitValueInitExprClass, Empty) { }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImplicitValueInitExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return SourceLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SourceLocation(); }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class ParenListExpr final
    : public Expr,
      private llvm::TrailingObjects<ParenListExpr, Stmt *> {
  friend class ASTStmtReader;
  friend TrailingObjects;

  /// The location of the left and right parentheses.
  SourceLocation LParenLoc, RParenLoc;

  /// Build a paren list.
  ParenListExpr(SourceLocation LParenLoc, ArrayRef<Expr *> Exprs,
                SourceLocation RParenLoc);

  /// Build an empty paren list.
  ParenListExpr(EmptyShell Empty, unsigned NumExprs);

public:
  /// Create a paren list.
  static ParenListExpr *Create(const ASTContext &Ctx, SourceLocation LParenLoc,
                               ArrayRef<Expr *> Exprs,
                               SourceLocation RParenLoc);

  /// Create an empty paren list.
  static ParenListExpr *CreateEmpty(const ASTContext &Ctx, unsigned NumExprs);

  /// Return the number of expressions in this paren list.
  unsigned getNumExprs() const { return ParenListExprBits.NumExprs; }

  Expr *getExpr(unsigned Init) {
    assert(Init < getNumExprs() && "Initializer access out of range!");
    return getExprs()[Init];
  }

  const Expr *getExpr(unsigned Init) const {
    return const_cast<ParenListExpr *>(this)->getExpr(Init);
  }

  Expr **getExprs() {
    return reinterpret_cast<Expr **>(getTrailingObjects<Stmt *>());
  }

  ArrayRef<Expr *> exprs() { return llvm::ArrayRef(getExprs(), getNumExprs()); }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceLocation getBeginLoc() const { return getLParenLoc(); }
  SourceLocation getEndLoc() const { return getRParenLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ParenListExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() + getNumExprs());
  }
  const_child_range children() const {
    return const_child_range(getTrailingObjects<Stmt *>(),
                             getTrailingObjects<Stmt *>() + getNumExprs());
  }
};

/// Represents a C11 generic selection.
///
/// A generic selection (C11 6.5.1.1) contains an unevaluated controlling
/// expression, followed by one or more generic associations.  Each generic
/// association specifies a type name and an expression, or "default" and an
/// expression (in which case it is known as a default generic association).
/// The type and value of the generic selection are identical to those of its
/// result expression, which is defined as the expression in the generic
/// association with a type name that is compatible with the type of the
/// controlling expression, or the expression in the default generic association
/// if no types are compatible.  For example:
///
/// @code
/// _Generic(X, double: 1, float: 2, default: 3)
/// @endcode
///
/// The above expression evaluates to 1 if 1.0 is substituted for X, 2 if 1.0f
/// or 3 if "hello".
///
/// As an extension, generic selections are allowed in C++, where the following
/// additional semantics apply:
///
/// Any generic selection whose controlling expression is type-dependent or
/// which names a dependent type in its association list is result-dependent,
/// which means that the choice of result expression is dependent.
/// Result-dependent generic associations are both type- and value-dependent.
///
/// We also allow an extended form in both C and C++ where the controlling
/// predicate for the selection expression is a type rather than an expression.
/// This type argument form does not perform any conversions for the
/// controlling type, which makes it suitable for use with qualified type
/// associations, which is not possible with the expression form.
class GenericSelectionExpr final
    : public Expr,
      private llvm::TrailingObjects<GenericSelectionExpr, Stmt *,
                                    TypeSourceInfo *> {
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  friend TrailingObjects;

  /// The number of association expressions and the index of the result
  /// expression in the case where the generic selection expression is not
  /// result-dependent. The result index is equal to ResultDependentIndex
  /// if and only if the generic selection expression is result-dependent.
  unsigned NumAssocs : 15;
  unsigned ResultIndex : 15; // NB: ResultDependentIndex is tied to this width.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExprPredicate : 1;
  enum : unsigned {
    ResultDependentIndex = 0x7FFF
  };

  unsigned getIndexOfControllingExpression() const {
    // If controlled by an expression, the first offset into the Stmt *
    // trailing array is the controlling expression, the associated expressions
    // follow this.
    assert(isExprPredicate() && "Asking for the controlling expression of a "
                                "selection expr predicated by a type");
    return 0;
  }

  unsigned getIndexOfControllingType() const {
    // If controlled by a type, the first offset into the TypeSourceInfo *
    // trailing array is the controlling type, the associated types follow this.
    assert(isTypePredicate() && "Asking for the controlling type of a "
                                 "selection expr predicated by an expression");
    return 0;
  }

  unsigned getIndexOfStartOfAssociatedExprs() const {
    // If the predicate is a type, then the associated expressions are the only
    // Stmt * in the trailing array, otherwise we need to offset past the
    // predicate expression.
    return (int)isExprPredicate();
  }

  unsigned getIndexOfStartOfAssociatedTypes() const {
    // If the predicate is a type, then the associated types follow it in the
    // trailing array. Otherwise, the associated types are the only
    // TypeSourceInfo * in the trailing array.
    return (int)isTypePredicate();
  }


  /// The location of the "default" and of the right parenthesis.
  SourceLocation DefaultLoc, RParenLoc;

  // GenericSelectionExpr is followed by several trailing objects.
  // They are (in order):
  //
  // * A single Stmt * for the controlling expression or a TypeSourceInfo * for
  //   the controlling type, depending on the result of isTypePredicate() or
  //   isExprPredicate().
  // * An array of getNumAssocs() Stmt * for the association expressions.
  // * An array of getNumAssocs() TypeSourceInfo *, one for each of the
  //   association expressions.
  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    // Add one to account for the controlling expression; the remainder
    // are the associated expressions.
    return getNumAssocs() + (int)isExprPredicate();
  }

  unsigned numTrailingObjects(OverloadToken<TypeSourceInfo *>) const {
    // Add one to account for the controlling type predicate, the remainder
    // are the associated types.
    return getNumAssocs() + (int)isTypePredicate();
  }

  template <bool Const> class AssociationIteratorTy;
  /// Bundle together an association expression and its TypeSourceInfo.
  /// The Const template parameter is for the const and non-const versions
  /// of AssociationTy.
  template <bool Const> class AssociationTy {
    friend class GenericSelectionExpr;
    template <bool OtherConst> friend class AssociationIteratorTy;
    using ExprPtrTy = std::conditional_t<Const, const Expr *, Expr *>;
    using TSIPtrTy =
        std::conditional_t<Const, const TypeSourceInfo *, TypeSourceInfo *>;
    ExprPtrTy E;
    TSIPtrTy TSI;
    bool Selected;
    AssociationTy(ExprPtrTy E, TSIPtrTy TSI, bool Selected)
        : E(E), TSI(TSI), Selected(Selected) {}

  public:
    ExprPtrTy getAssociationExpr() const { return E; }
    TSIPtrTy getTypeSourceInfo() const { return TSI; }
    QualType getType() const { return TSI ? TSI->getType() : QualType(); }
    bool isSelected() const { return Selected; }
    AssociationTy *operator->() { return this; }
    const AssociationTy *operator->() const { return this; }
  }; // class AssociationTy

  /// Iterator over const and non-const Association objects. The Association
  /// objects are created on the fly when the iterator is dereferenced.
  /// This abstract over how exactly the association expressions and the
  /// corresponding TypeSourceInfo * are stored.
  template <bool Const>
  class AssociationIteratorTy
      : public llvm::iterator_facade_base<
            AssociationIteratorTy<Const>, std::input_iterator_tag,
            AssociationTy<Const>, std::ptrdiff_t, AssociationTy<Const>,
            AssociationTy<Const>> {
    friend class GenericSelectionExpr;
    // FIXME: This iterator could conceptually be a random access iterator, and
    // it would be nice if we could strengthen the iterator category someday.
    // However this iterator does not satisfy two requirements of forward
    // iterators:
    // a) reference = T& or reference = const T&
    // b) If It1 and It2 are both dereferenceable, then It1 == It2 if and only
    //    if *It1 and *It2 are bound to the same objects.
    // An alternative design approach was discussed during review;
    // store an Association object inside the iterator, and return a reference
    // to it when dereferenced. This idea was discarded because of nasty
    // lifetime issues:
    //    AssociationIterator It = ...;
    //    const Association &Assoc = *It++; // Oops, Assoc is dangling.
    using BaseTy = typename AssociationIteratorTy::iterator_facade_base;
    using StmtPtrPtrTy =
        std::conditional_t<Const, const Stmt *const *, Stmt **>;
    using TSIPtrPtrTy = std::conditional_t<Const, const TypeSourceInfo *const *,
                                           TypeSourceInfo **>;
    StmtPtrPtrTy E = nullptr;
    TSIPtrPtrTy TSI; // Kept in sync with E.
    unsigned Offset = 0, SelectedOffset = 0;
    AssociationIteratorTy(StmtPtrPtrTy E, TSIPtrPtrTy TSI, unsigned Offset,
                          unsigned SelectedOffset)
        : E(E), TSI(TSI), Offset(Offset), SelectedOffset(SelectedOffset) {}

  public:
    AssociationIteratorTy() : E(nullptr), TSI(nullptr) {}
    typename BaseTy::reference operator*() const {
      return AssociationTy<Const>(cast<Expr>(*E), *TSI,
                                  Offset == SelectedOffset);
    }
    typename BaseTy::pointer operator->() const { return **this; }
    using BaseTy::operator++;
    AssociationIteratorTy &operator++() {
      ++E;
      ++TSI;
      ++Offset;
      return *this;
    }
    bool operator==(AssociationIteratorTy Other) const { return E == Other.E; }
  }; // class AssociationIterator

  /// Build a non-result-dependent generic selection expression accepting an
  /// expression predicate.
  GenericSelectionExpr(const ASTContext &Context, SourceLocation GenericLoc,
                       Expr *ControllingExpr,
                       ArrayRef<TypeSourceInfo *> AssocTypes,
                       ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
                       SourceLocation RParenLoc,
                       bool ContainsUnexpandedParameterPack,
                       unsigned ResultIndex);

  /// Build a result-dependent generic selection expression accepting an
  /// expression predicate.
  GenericSelectionExpr(const ASTContext &Context, SourceLocation GenericLoc,
                       Expr *ControllingExpr,
                       ArrayRef<TypeSourceInfo *> AssocTypes,
                       ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
                       SourceLocation RParenLoc,
                       bool ContainsUnexpandedParameterPack);

  /// Build a non-result-dependent generic selection expression accepting a
  /// type predicate.
  GenericSelectionExpr(const ASTContext &Context, SourceLocation GenericLoc,
                       TypeSourceInfo *ControllingType,
                       ArrayRef<TypeSourceInfo *> AssocTypes,
                       ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
                       SourceLocation RParenLoc,
                       bool ContainsUnexpandedParameterPack,
                       unsigned ResultIndex);

  /// Build a result-dependent generic selection expression accepting a type
  /// predicate.
  GenericSelectionExpr(const ASTContext &Context, SourceLocation GenericLoc,
                       TypeSourceInfo *ControllingType,
                       ArrayRef<TypeSourceInfo *> AssocTypes,
                       ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
                       SourceLocation RParenLoc,
                       bool ContainsUnexpandedParameterPack);

  /// Build an empty generic selection expression for deserialization.
  explicit GenericSelectionExpr(EmptyShell Empty, unsigned NumAssocs);

public:
  /// Create a non-result-dependent generic selection expression accepting an
  /// expression predicate.
  static GenericSelectionExpr *
  Create(const ASTContext &Context, SourceLocation GenericLoc,
         Expr *ControllingExpr, ArrayRef<TypeSourceInfo *> AssocTypes,
         ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc, bool ContainsUnexpandedParameterPack,
         unsigned ResultIndex);

  /// Create a result-dependent generic selection expression accepting an
  /// expression predicate.
  static GenericSelectionExpr *
  Create(const ASTContext &Context, SourceLocation GenericLoc,
         Expr *ControllingExpr, ArrayRef<TypeSourceInfo *> AssocTypes,
         ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc, bool ContainsUnexpandedParameterPack);

  /// Create a non-result-dependent generic selection expression accepting a
  /// type predicate.
  static GenericSelectionExpr *
  Create(const ASTContext &Context, SourceLocation GenericLoc,
         TypeSourceInfo *ControllingType, ArrayRef<TypeSourceInfo *> AssocTypes,
         ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc, bool ContainsUnexpandedParameterPack,
         unsigned ResultIndex);

  /// Create a result-dependent generic selection expression accepting a type
  /// predicate
  static GenericSelectionExpr *
  Create(const ASTContext &Context, SourceLocation GenericLoc,
         TypeSourceInfo *ControllingType, ArrayRef<TypeSourceInfo *> AssocTypes,
         ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc, bool ContainsUnexpandedParameterPack);

  /// Create an empty generic selection expression for deserialization.
  static GenericSelectionExpr *CreateEmpty(const ASTContext &Context,
                                           unsigned NumAssocs);

  using Association = AssociationTy<false>;
  using ConstAssociation = AssociationTy<true>;
  using AssociationIterator = AssociationIteratorTy<false>;
  using ConstAssociationIterator = AssociationIteratorTy<true>;
  using association_range = llvm::iterator_range<AssociationIterator>;
  using const_association_range =
      llvm::iterator_range<ConstAssociationIterator>;

  /// The number of association expressions.
  unsigned getNumAssocs() const { return NumAssocs; }

  /// The zero-based index of the result expression's generic association in
  /// the generic selection's association list.  Defined only if the
  /// generic selection is not result-dependent.
  unsigned getResultIndex() const {
    assert(!isResultDependent() &&
           "Generic selection is result-dependent but getResultIndex called!");
    return ResultIndex;
  }

  /// Whether this generic selection is result-dependent.
  bool isResultDependent() const { return ResultIndex == ResultDependentIndex; }

  /// Whether this generic selection uses an expression as its controlling
  /// argument.
  bool isExprPredicate() const { return IsExprPredicate; }
  /// Whether this generic selection uses a type as its controlling argument.
  bool isTypePredicate() const { return !IsExprPredicate; }

  /// Return the controlling expression of this generic selection expression.
  /// Only valid to call if the selection expression used an expression as its
  /// controlling argument.
  Expr *getControllingExpr() {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfControllingExpression()]);
  }
  const Expr *getControllingExpr() const {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfControllingExpression()]);
  }

  /// Return the controlling type of this generic selection expression. Only
  /// valid to call if the selection expression used a type as its controlling
  /// argument.
  TypeSourceInfo *getControllingType() {
    return getTrailingObjects<TypeSourceInfo *>()[getIndexOfControllingType()];
  }
  const TypeSourceInfo* getControllingType() const {
    return getTrailingObjects<TypeSourceInfo *>()[getIndexOfControllingType()];
  }

  /// Return the result expression of this controlling expression. Defined if
  /// and only if the generic selection expression is not result-dependent.
  Expr *getResultExpr() {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                     getResultIndex()]);
  }
  const Expr *getResultExpr() const {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                     getResultIndex()]);
  }

  ArrayRef<Expr *> getAssocExprs() const {
    return {reinterpret_cast<Expr *const *>(getTrailingObjects<Stmt *>() +
                                            getIndexOfStartOfAssociatedExprs()),
            NumAssocs};
  }
  ArrayRef<TypeSourceInfo *> getAssocTypeSourceInfos() const {
    return {getTrailingObjects<TypeSourceInfo *>() +
                getIndexOfStartOfAssociatedTypes(),
            NumAssocs};
  }

  /// Return the Ith association expression with its TypeSourceInfo,
  /// bundled together in GenericSelectionExpr::(Const)Association.
  Association getAssociation(unsigned I) {
    assert(I < getNumAssocs() &&
           "Out-of-range index in GenericSelectionExpr::getAssociation!");
    return Association(
        cast<Expr>(
            getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                         I]),
        getTrailingObjects<
            TypeSourceInfo *>()[getIndexOfStartOfAssociatedTypes() + I],
        !isResultDependent() && (getResultIndex() == I));
  }
  ConstAssociation getAssociation(unsigned I) const {
    assert(I < getNumAssocs() &&
           "Out-of-range index in GenericSelectionExpr::getAssociation!");
    return ConstAssociation(
        cast<Expr>(
            getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                         I]),
        getTrailingObjects<
            TypeSourceInfo *>()[getIndexOfStartOfAssociatedTypes() + I],
        !isResultDependent() && (getResultIndex() == I));
  }

  association_range associations() {
    AssociationIterator Begin(getTrailingObjects<Stmt *>() +
                                  getIndexOfStartOfAssociatedExprs(),
                              getTrailingObjects<TypeSourceInfo *>() +
                                  getIndexOfStartOfAssociatedTypes(),
                              /*Offset=*/0, ResultIndex);
    AssociationIterator End(Begin.E + NumAssocs, Begin.TSI + NumAssocs,
                            /*Offset=*/NumAssocs, ResultIndex);
    return llvm::make_range(Begin, End);
  }

  const_association_range associations() const {
    ConstAssociationIterator Begin(getTrailingObjects<Stmt *>() +
                                       getIndexOfStartOfAssociatedExprs(),
                                   getTrailingObjects<TypeSourceInfo *>() +
                                       getIndexOfStartOfAssociatedTypes(),
                                   /*Offset=*/0, ResultIndex);
    ConstAssociationIterator End(Begin.E + NumAssocs, Begin.TSI + NumAssocs,
                                 /*Offset=*/NumAssocs, ResultIndex);
    return llvm::make_range(Begin, End);
  }

  SourceLocation getGenericLoc() const {
    return GenericSelectionExprBits.GenericLoc;
  }
  SourceLocation getDefaultLoc() const { return DefaultLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceLocation getBeginLoc() const { return getGenericLoc(); }
  SourceLocation getEndLoc() const { return getRParenLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GenericSelectionExprClass;
  }

  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() +
                           numTrailingObjects(OverloadToken<Stmt *>()));
  }
  const_child_range children() const {
    return const_child_range(getTrailingObjects<Stmt *>(),
                             getTrailingObjects<Stmt *>() +
                                 numTrailingObjects(OverloadToken<Stmt *>()));
  }
};

//===----------------------------------------------------------------------===//
// Clang Extensions
//===----------------------------------------------------------------------===//

/// ExtVectorElementExpr - This represents access to specific elements of a
/// vector, and may occur on the left hand side or right hand side.  For example
/// the following is legal:  "V.xy = V.zw" if V is a 4 element extended vector.
///
/// Note that the base may have either vector or pointer to vector type, just
/// like a struct field reference.
///
class ExtVectorElementExpr : public Expr {
  Stmt *Base;
  IdentifierInfo *Accessor;
  SourceLocation AccessorLoc;
public:
  ExtVectorElementExpr(QualType ty, ExprValueKind VK, Expr *base,
                       IdentifierInfo &accessor, SourceLocation loc)
      : Expr(ExtVectorElementExprClass, ty, VK,
             (VK == VK_PRValue ? OK_Ordinary : OK_VectorComponent)),
        Base(base), Accessor(&accessor), AccessorLoc(loc) {
    setDependence(computeDependence(this));
  }

  /// Build an empty vector element expression.
  explicit ExtVectorElementExpr(EmptyShell Empty)
    : Expr(ExtVectorElementExprClass, Empty) { }

  const Expr *getBase() const { return cast<Expr>(Base); }
  Expr *getBase() { return cast<Expr>(Base); }
  void setBase(Expr *E) { Base = E; }

  IdentifierInfo &getAccessor() const { return *Accessor; }
  void setAccessor(IdentifierInfo *II) { Accessor = II; }

  SourceLocation getAccessorLoc() const { return AccessorLoc; }
  void setAccessorLoc(SourceLocation L) { AccessorLoc = L; }

  /// getNumElements - Get the number of components being selected.
  unsigned getNumElements() const;

  /// containsDuplicateElements - Return true if any element access is
  /// repeated.
  bool containsDuplicateElements() const;

  /// getEncodedElementAccess - Encode the elements accessed into an llvm
  /// aggregate Constant of ConstantInt(s).
  void getEncodedElementAccess(SmallVectorImpl<uint32_t> &Elts) const;

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return AccessorLoc; }

  /// isArrow - Return true if the base expression is a pointer to vector,
  /// return false if the base expression is a vector.
  bool isArrow() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ExtVectorElementExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }
  const_child_range children() const {
    return const_child_range(&Base, &Base + 1);
  }
};

/// BlockExpr - Adaptor class for mixing a BlockDecl with expressions.
/// ^{ statement-body }   or   ^(int arg1, float arg2){ statement-body }
class BlockExpr : public Expr {
protected:
  BlockDecl *TheBlock;
public:
  BlockExpr(BlockDecl *BD, QualType ty)
      : Expr(BlockExprClass, ty, VK_PRValue, OK_Ordinary), TheBlock(BD) {
    setDependence(computeDependence(this));
  }

  /// Build an empty block expression.
  explicit BlockExpr(EmptyShell Empty) : Expr(BlockExprClass, Empty) { }

  const BlockDecl *getBlockDecl() const { return TheBlock; }
  BlockDecl *getBlockDecl() { return TheBlock; }
  void setBlockDecl(BlockDecl *BD) { TheBlock = BD; }

  // Convenience functions for probing the underlying BlockDecl.
  SourceLocation getCaretLocation() const;
  const Stmt *getBody() const;
  Stmt *getBody();

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCaretLocation();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getBody()->getEndLoc();
  }

  /// getFunctionType - Return the underlying function type for this block.
  const FunctionProtoType *getFunctionType() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BlockExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

/// Copy initialization expr of a __block variable and a boolean flag that
/// indicates whether the expression can throw.
struct BlockVarCopyInit {
  BlockVarCopyInit() = default;
  BlockVarCopyInit(Expr *CopyExpr, bool CanThrow)
      : ExprAndFlag(CopyExpr, CanThrow) {}
  void setExprAndFlag(Expr *CopyExpr, bool CanThrow) {
    ExprAndFlag.setPointerAndInt(CopyExpr, CanThrow);
  }
  Expr *getCopyExpr() const { return ExprAndFlag.getPointer(); }
  bool canThrow() const { return ExprAndFlag.getInt(); }
  llvm::PointerIntPair<Expr *, 1, bool> ExprAndFlag;
};

/// AsTypeExpr - Clang builtin function __builtin_astype [OpenCL 6.2.4.2]
/// This AST node provides support for reinterpreting a type to another
/// type of the same size.
class AsTypeExpr : public Expr {
private:
  Stmt *SrcExpr;
  SourceLocation BuiltinLoc, RParenLoc;

  friend class ASTReader;
  friend class ASTStmtReader;
  explicit AsTypeExpr(EmptyShell Empty) : Expr(AsTypeExprClass, Empty) {}

public:
  AsTypeExpr(Expr *SrcExpr, QualType DstType, ExprValueKind VK,
             ExprObjectKind OK, SourceLocation BuiltinLoc,
             SourceLocation RParenLoc)
      : Expr(AsTypeExprClass, DstType, VK, OK), SrcExpr(SrcExpr),
        BuiltinLoc(BuiltinLoc), RParenLoc(RParenLoc) {
    setDependence(computeDependence(this));
  }

  /// getSrcExpr - Return the Expr to be converted.
  Expr *getSrcExpr() const { return cast<Expr>(SrcExpr); }

  /// getBuiltinLoc - Return the location of the __builtin_astype token.
  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }

  /// getRParenLoc - Return the location of final right parenthesis.
  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AsTypeExprClass;
  }

  // Iterators
  child_range children() { return child_range(&SrcExpr, &SrcExpr+1); }
  const_child_range children() const {
    return const_child_range(&SrcExpr, &SrcExpr + 1);
  }
};

/// PseudoObjectExpr - An expression which accesses a pseudo-object
/// l-value.  A pseudo-object is an abstract object, accesses to which
/// are translated to calls.  The pseudo-object expression has a
/// syntactic form, which shows how the expression was actually
/// written in the source code, and a semantic form, which is a series
/// of expressions to be executed in order which detail how the
/// operation is actually evaluated.  Optionally, one of the semantic
/// forms may also provide a result value for the expression.
///
/// If any of the semantic-form expressions is an OpaqueValueExpr,
/// that OVE is required to have a source expression, and it is bound
/// to the result of that source expression.  Such OVEs may appear
/// only in subsequent semantic-form expressions and as
/// sub-expressions of the syntactic form.
///
/// PseudoObjectExpr should be used only when an operation can be
/// usefully described in terms of fairly simple rewrite rules on
/// objects and functions that are meant to be used by end-developers.
/// For example, under the Itanium ABI, dynamic casts are implemented
/// as a call to a runtime function called __dynamic_cast; using this
/// class to describe that would be inappropriate because that call is
/// not really part of the user-visible semantics, and instead the
/// cast is properly reflected in the AST and IR-generation has been
/// taught to generate the call as necessary.  In contrast, an
/// Objective-C property access is semantically defined to be
/// equivalent to a particular message send, and this is very much
/// part of the user model.  The name of this class encourages this
/// modelling design.
class PseudoObjectExpr final
    : public Expr,
      private llvm::TrailingObjects<PseudoObjectExpr, Expr *> {
  // PseudoObjectExprBits.NumSubExprs - The number of sub-expressions.
  // Always at least two, because the first sub-expression is the
  // syntactic form.

  // PseudoObjectExprBits.ResultIndex - The index of the
  // sub-expression holding the result.  0 means the result is void,
  // which is unambiguous because it's the index of the syntactic
  // form.  Note that this is therefore 1 higher than the value passed
  // in to Create, which is an index within the semantic forms.
  // Note also that ASTStmtWriter assumes this encoding.

  Expr **getSubExprsBuffer() { return getTrailingObjects<Expr *>(); }
  const Expr * const *getSubExprsBuffer() const {
    return getTrailingObjects<Expr *>();
  }

  PseudoObjectExpr(QualType type, ExprValueKind VK,
                   Expr *syntactic, ArrayRef<Expr*> semantic,
                   unsigned resultIndex);

  PseudoObjectExpr(EmptyShell shell, unsigned numSemanticExprs);

  unsigned getNumSubExprs() const {
    return PseudoObjectExprBits.NumSubExprs;
  }

public:
  /// NoResult - A value for the result index indicating that there is
  /// no semantic result.
  enum : unsigned { NoResult = ~0U };

  static PseudoObjectExpr *Create(const ASTContext &Context, Expr *syntactic,
                                  ArrayRef<Expr*> semantic,
                                  unsigned resultIndex);

  static PseudoObjectExpr *Create(const ASTContext &Context, EmptyShell shell,
                                  unsigned numSemanticExprs);

  /// Return the syntactic form of this expression, i.e. the
  /// expression it actually looks like.  Likely to be expressed in
  /// terms of OpaqueValueExprs bound in the semantic form.
  Expr *getSyntacticForm() { return getSubExprsBuffer()[0]; }
  const Expr *getSyntacticForm() const { return getSubExprsBuffer()[0]; }

  /// Return the index of the result-bearing expression into the semantics
  /// expressions, or PseudoObjectExpr::NoResult if there is none.
  unsigned getResultExprIndex() const {
    if (PseudoObjectExprBits.ResultIndex == 0) return NoResult;
    return PseudoObjectExprBits.ResultIndex - 1;
  }

  /// Return the result-bearing expression, or null if there is none.
  Expr *getResultExpr() {
    if (PseudoObjectExprBits.ResultIndex == 0)
      return nullptr;
    return getSubExprsBuffer()[PseudoObjectExprBits.ResultIndex];
  }
  const Expr *getResultExpr() const {
    return const_cast<PseudoObjectExpr*>(this)->getResultExpr();
  }

  unsigned getNumSemanticExprs() const { return getNumSubExprs() - 1; }

  typedef Expr * const *semantics_iterator;
  typedef const Expr * const *const_semantics_iterator;
  semantics_iterator semantics_begin() {
    return getSubExprsBuffer() + 1;
  }
  const_semantics_iterator semantics_begin() const {
    return getSubExprsBuffer() + 1;
  }
  semantics_iterator semantics_end() {
    return getSubExprsBuffer() + getNumSubExprs();
  }
  const_semantics_iterator semantics_end() const {
    return getSubExprsBuffer() + getNumSubExprs();
  }

  ArrayRef<Expr*> semantics() {
    return ArrayRef(semantics_begin(), semantics_end());
  }
  ArrayRef<const Expr*> semantics() const {
    return ArrayRef(semantics_begin(), semantics_end());
  }

  Expr *getSemanticExpr(unsigned index) {
    assert(index + 1 < getNumSubExprs());
    return getSubExprsBuffer()[index + 1];
  }
  const Expr *getSemanticExpr(unsigned index) const {
    return const_cast<PseudoObjectExpr*>(this)->getSemanticExpr(index);
  }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getSyntacticForm()->getExprLoc();
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getSyntacticForm()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSyntacticForm()->getEndLoc();
  }

  child_range children() {
    const_child_range CCR =
        const_cast<const PseudoObjectExpr *>(this)->children();
    return child_range(cast_away_const(CCR.begin()),
                       cast_away_const(CCR.end()));
  }
  const_child_range children() const {
    Stmt *const *cs = const_cast<Stmt *const *>(
        reinterpret_cast<const Stmt *const *>(getSubExprsBuffer()));
    return const_child_range(cs, cs + getNumSubExprs());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == PseudoObjectExprClass;
  }

  friend TrailingObjects;
  friend class ASTStmtReader;
};

/// AtomicExpr - Variadic atomic builtins: __atomic_exchange, __atomic_fetch_*,
/// __atomic_load, __atomic_store, and __atomic_compare_exchange_*, for the
/// similarly-named C++11 instructions, and __c11 variants for <stdatomic.h>,
/// and corresponding __opencl_atomic_* for OpenCL 2.0.
/// All of these instructions take one primary pointer, at least one memory
/// order. The instructions for which getScopeModel returns non-null value
/// take one synch scope.
class AtomicExpr : public Expr {
public:
  enum AtomicOp {
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS) AO ## ID,
#include "clang/Basic/Builtins.inc"
    // Avoid trailing comma
    BI_First = 0
  };

private:
  /// Location of sub-expressions.
  /// The location of Scope sub-expression is NumSubExprs - 1, which is
  /// not fixed, therefore is not defined in enum.
  enum { PTR, ORDER, VAL1, ORDER_FAIL, VAL2, WEAK, END_EXPR };
  Stmt *SubExprs[END_EXPR + 1];
  unsigned NumSubExprs;
  SourceLocation BuiltinLoc, RParenLoc;
  AtomicOp Op;

  friend class ASTStmtReader;
public:
  AtomicExpr(SourceLocation BLoc, ArrayRef<Expr*> args, QualType t,
             AtomicOp op, SourceLocation RP);

  /// Determine the number of arguments the specified atomic builtin
  /// should have.
  static unsigned getNumSubExprs(AtomicOp Op);

  /// Build an empty AtomicExpr.
  explicit AtomicExpr(EmptyShell Empty) : Expr(AtomicExprClass, Empty) { }

  Expr *getPtr() const {
    return cast<Expr>(SubExprs[PTR]);
  }
  Expr *getOrder() const {
    return cast<Expr>(SubExprs[ORDER]);
  }
  Expr *getScope() const {
    assert(getScopeModel() && "No scope");
    return cast<Expr>(SubExprs[NumSubExprs - 1]);
  }
  Expr *getVal1() const {
    if (Op == AO__c11_atomic_init || Op == AO__opencl_atomic_init)
      return cast<Expr>(SubExprs[ORDER]);
    assert(NumSubExprs > VAL1);
    return cast<Expr>(SubExprs[VAL1]);
  }
  Expr *getOrderFail() const {
    assert(NumSubExprs > ORDER_FAIL);
    return cast<Expr>(SubExprs[ORDER_FAIL]);
  }
  Expr *getVal2() const {
    if (Op == AO__atomic_exchange || Op == AO__scoped_atomic_exchange)
      return cast<Expr>(SubExprs[ORDER_FAIL]);
    assert(NumSubExprs > VAL2);
    return cast<Expr>(SubExprs[VAL2]);
  }
  Expr *getWeak() const {
    assert(NumSubExprs > WEAK);
    return cast<Expr>(SubExprs[WEAK]);
  }
  QualType getValueType() const;

  AtomicOp getOp() const { return Op; }
  StringRef getOpAsString() const {
    switch (Op) {
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS)                                        \
  case AO##ID:                                                                 \
    return #ID;
#include "clang/Basic/Builtins.inc"
    }
    llvm_unreachable("not an atomic operator?");
  }
  unsigned getNumSubExprs() const { return NumSubExprs; }

  Expr **getSubExprs() { return reinterpret_cast<Expr **>(SubExprs); }
  const Expr * const *getSubExprs() const {
    return reinterpret_cast<Expr * const *>(SubExprs);
  }

  bool isVolatile() const {
    return getPtr()->getType()->getPointeeType().isVolatileQualified();
  }

  bool isCmpXChg() const {
    return getOp() == AO__c11_atomic_compare_exchange_strong ||
           getOp() == AO__c11_atomic_compare_exchange_weak ||
           getOp() == AO__hip_atomic_compare_exchange_strong ||
           getOp() == AO__opencl_atomic_compare_exchange_strong ||
           getOp() == AO__opencl_atomic_compare_exchange_weak ||
           getOp() == AO__hip_atomic_compare_exchange_weak ||
           getOp() == AO__atomic_compare_exchange ||
           getOp() == AO__atomic_compare_exchange_n ||
           getOp() == AO__scoped_atomic_compare_exchange ||
           getOp() == AO__scoped_atomic_compare_exchange_n;
  }

  bool isOpenCL() const {
    return getOp() >= AO__opencl_atomic_compare_exchange_strong &&
           getOp() <= AO__opencl_atomic_store;
  }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AtomicExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(SubExprs, SubExprs+NumSubExprs);
  }
  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + NumSubExprs);
  }

  /// Get atomic scope model for the atomic op code.
  /// \return empty atomic scope model if the atomic op code does not have
  ///   scope operand.
  static std::unique_ptr<AtomicScopeModel> getScopeModel(AtomicOp Op) {
    // FIXME: Allow grouping of builtins to be able to only check >= and <=
    if (Op >= AO__opencl_atomic_compare_exchange_strong &&
        Op <= AO__opencl_atomic_store && Op != AO__opencl_atomic_init)
      return AtomicScopeModel::create(AtomicScopeModelKind::OpenCL);
    if (Op >= AO__hip_atomic_compare_exchange_strong &&
        Op <= AO__hip_atomic_store)
      return AtomicScopeModel::create(AtomicScopeModelKind::HIP);
    if (Op >= AO__scoped_atomic_add_fetch && Op <= AO__scoped_atomic_xor_fetch)
      return AtomicScopeModel::create(AtomicScopeModelKind::Generic);
    return AtomicScopeModel::create(AtomicScopeModelKind::None);
  }

  /// Get atomic scope model.
  /// \return empty atomic scope model if this atomic expression does not have
  ///   scope operand.
  std::unique_ptr<AtomicScopeModel> getScopeModel() const {
    return getScopeModel(getOp());
  }
};

/// TypoExpr - Internal placeholder for expressions where typo correction
/// still needs to be performed and/or an error diagnostic emitted.
class TypoExpr : public Expr {
  // The location for the typo name.
  SourceLocation TypoLoc;

public:
  TypoExpr(QualType T, SourceLocation TypoLoc)
      : Expr(TypoExprClass, T, VK_LValue, OK_Ordinary), TypoLoc(TypoLoc) {
    assert(T->isDependentType() && "TypoExpr given a non-dependent type");
    setDependence(ExprDependence::TypeValueInstantiation |
                  ExprDependence::Error);
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return TypoLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return TypoLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == TypoExprClass;
  }

};

/// This class represents BOTH the OpenMP Array Section and OpenACC 'subarray',
/// with a boolean differentiator.
/// OpenMP 5.0 [2.1.5, Array Sections].
/// To specify an array section in an OpenMP construct, array subscript
/// expressions are extended with the following syntax:
/// \code
/// [ lower-bound : length : stride ]
/// [ lower-bound : length : ]
/// [ lower-bound : length ]
/// [ lower-bound : : stride ]
/// [ lower-bound : : ]
/// [ lower-bound : ]
/// [ : length : stride ]
/// [ : length : ]
/// [ : length ]
/// [ : : stride ]
/// [ : : ]
/// [ : ]
/// \endcode
/// The array section must be a subset of the original array.
/// Array sections are allowed on multidimensional arrays. Base language array
/// subscript expressions can be used to specify length-one dimensions of
/// multidimensional array sections.
/// Each of the lower-bound, length, and stride expressions if specified must be
/// an integral type expressions of the base language. When evaluated
/// they represent a set of integer values as follows:
/// \code
/// { lower-bound, lower-bound + stride, lower-bound + 2 * stride,... ,
/// lower-bound + ((length - 1) * stride) }
/// \endcode
/// The lower-bound and length must evaluate to non-negative integers.
/// The stride must evaluate to a positive integer.
/// When the size of the array dimension is not known, the length must be
/// specified explicitly.
/// When the stride is absent it defaults to 1.
/// When the length is absent it defaults to ⌈(size − lower-bound)/stride⌉,
/// where size is the size of the array dimension. When the lower-bound is
/// absent it defaults to 0.
///
///
/// OpenACC 3.3 [2.7.1 Data Specification in Data Clauses]
/// In C and C++, a subarray is an array name followed by an extended array
/// range specification in brackets, with start and length, such as
///
/// AA[2:n]
///
/// If the lower bound is missing, zero is used. If the length is missing and
/// the array has known size, the size of the array is used; otherwise the
/// length is required. The subarray AA[2:n] means elements AA[2], AA[3], . . .
/// , AA[2+n-1]. In C and C++, a two dimensional array may be declared in at
/// least four ways:
///
/// -Statically-sized array: float AA[100][200];
/// -Pointer to statically sized rows: typedef float row[200]; row* BB;
/// -Statically-sized array of pointers: float* CC[200];
/// -Pointer to pointers: float** DD;
///
/// Each dimension may be statically sized, or a pointer to dynamically
/// allocated memory. Each of these may be included in a data clause using
/// subarray notation to specify a rectangular array:
///
/// -AA[2:n][0:200]
/// -BB[2:n][0:m]
/// -CC[2:n][0:m]
/// -DD[2:n][0:m]
///
/// Multidimensional rectangular subarrays in C and C++ may be specified for any
/// array with any combination of statically-sized or dynamically-allocated
/// dimensions. For statically sized dimensions, all dimensions except the first
/// must specify the whole extent to preserve the contiguous data restriction,
/// discussed below. For dynamically allocated dimensions, the implementation
/// will allocate pointers in device memory corresponding to the pointers in
/// local memory and will fill in those pointers as appropriate.
///
/// In Fortran, a subarray is an array name followed by a comma-separated list
/// of range specifications in parentheses, with lower and upper bound
/// subscripts, such as
///
/// arr(1:high,low:100)
///
/// If either the lower or upper bounds are missing, the declared or allocated
/// bounds of the array, if known, are used. All dimensions except the last must
/// specify the whole extent, to preserve the contiguous data restriction,
/// discussed below.
///
/// Restrictions
///
/// -In Fortran, the upper bound for the last dimension of an assumed-size dummy
/// array must be specified.
///
/// -In C and C++, the length for dynamically allocated dimensions of an array
/// must be explicitly specified.
///
/// -In C and C++, modifying pointers in pointer arrays during the data
/// lifetime, either on the host or on the device, may result in undefined
/// behavior.
///
/// -If a subarray  appears in a data clause, the implementation may choose to
/// allocate memory for only that subarray on the accelerator.
///
/// -In Fortran, array pointers may appear, but pointer association is not
/// preserved in device memory.
///
/// -Any array or subarray in a data clause, including Fortran array pointers,
/// must be a contiguous section of memory, except for dynamic multidimensional
/// C arrays.
///
/// -In C and C++, if a variable or array of composite type appears, all the
/// data members of the struct or class are allocated and copied, as
/// appropriate. If a composite member is a pointer type, the data addressed by
/// that pointer are not implicitly copied.
///
/// -In Fortran, if a variable or array of composite type appears, all the
/// members of that derived type are allocated and copied, as appropriate. If
/// any member has the allocatable or pointer attribute, the data accessed
/// through that member are not copied.
///
/// -If an expression is used in a subscript or subarray expression in a clause
/// on a data construct, the same value is used when copying data at the end of
/// the data region, even if the values of variables in the expression change
/// during the data region.
class ArraySectionExpr : public Expr {
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

public:
  enum ArraySectionType { OMPArraySection, OpenACCArraySection };

private:
  enum {
    BASE,
    LOWER_BOUND,
    LENGTH,
    STRIDE,
    END_EXPR,
    OPENACC_END_EXPR = STRIDE
  };

  ArraySectionType ASType = OMPArraySection;
  Stmt *SubExprs[END_EXPR] = {nullptr};
  SourceLocation ColonLocFirst;
  SourceLocation ColonLocSecond;
  SourceLocation RBracketLoc;

public:
  // Constructor for OMP array sections, which include a 'stride'.
  ArraySectionExpr(Expr *Base, Expr *LowerBound, Expr *Length, Expr *Stride,
                   QualType Type, ExprValueKind VK, ExprObjectKind OK,
                   SourceLocation ColonLocFirst, SourceLocation ColonLocSecond,
                   SourceLocation RBracketLoc)
      : Expr(ArraySectionExprClass, Type, VK, OK), ASType(OMPArraySection),
        ColonLocFirst(ColonLocFirst), ColonLocSecond(ColonLocSecond),
        RBracketLoc(RBracketLoc) {
    setBase(Base);
    setLowerBound(LowerBound);
    setLength(Length);
    setStride(Stride);
    setDependence(computeDependence(this));
  }

  // Constructor for OpenACC sub-arrays, which do not permit a 'stride'.
  ArraySectionExpr(Expr *Base, Expr *LowerBound, Expr *Length, QualType Type,
                   ExprValueKind VK, ExprObjectKind OK, SourceLocation ColonLoc,
                   SourceLocation RBracketLoc)
      : Expr(ArraySectionExprClass, Type, VK, OK), ASType(OpenACCArraySection),
        ColonLocFirst(ColonLoc), RBracketLoc(RBracketLoc) {
    setBase(Base);
    setLowerBound(LowerBound);
    setLength(Length);
    setDependence(computeDependence(this));
  }

  /// Create an empty array section expression.
  explicit ArraySectionExpr(EmptyShell Shell)
      : Expr(ArraySectionExprClass, Shell) {}

  /// Return original type of the base expression for array section.
  static QualType getBaseOriginalType(const Expr *Base);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ArraySectionExprClass;
  }

  bool isOMPArraySection() const { return ASType == OMPArraySection; }
  bool isOpenACCArraySection() const { return ASType == OpenACCArraySection; }

  /// Get base of the array section.
  Expr *getBase() { return cast<Expr>(SubExprs[BASE]); }
  const Expr *getBase() const { return cast<Expr>(SubExprs[BASE]); }

  /// Get lower bound of array section.
  Expr *getLowerBound() { return cast_or_null<Expr>(SubExprs[LOWER_BOUND]); }
  const Expr *getLowerBound() const {
    return cast_or_null<Expr>(SubExprs[LOWER_BOUND]);
  }

  /// Get length of array section.
  Expr *getLength() { return cast_or_null<Expr>(SubExprs[LENGTH]); }
  const Expr *getLength() const { return cast_or_null<Expr>(SubExprs[LENGTH]); }

  /// Get stride of array section.
  Expr *getStride() {
    assert(ASType != OpenACCArraySection &&
           "Stride not valid in OpenACC subarrays");
    return cast_or_null<Expr>(SubExprs[STRIDE]);
  }

  const Expr *getStride() const {
    assert(ASType != OpenACCArraySection &&
           "Stride not valid in OpenACC subarrays");
    return cast_or_null<Expr>(SubExprs[STRIDE]);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return RBracketLoc; }

  SourceLocation getColonLocFirst() const { return ColonLocFirst; }
  SourceLocation getColonLocSecond() const {
    assert(ASType != OpenACCArraySection &&
           "second colon for stride not valid in OpenACC subarrays");
    return ColonLocSecond;
  }
  SourceLocation getRBracketLoc() const { return RBracketLoc; }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getBase()->getExprLoc();
  }

  child_range children() {
    return child_range(
        &SubExprs[BASE],
        &SubExprs[ASType == OMPArraySection ? END_EXPR : OPENACC_END_EXPR]);
  }

  const_child_range children() const {
    return const_child_range(
        &SubExprs[BASE],
        &SubExprs[ASType == OMPArraySection ? END_EXPR : OPENACC_END_EXPR]);
  }

private:
  /// Set base of the array section.
  void setBase(Expr *E) { SubExprs[BASE] = E; }

  /// Set lower bound of the array section.
  void setLowerBound(Expr *E) { SubExprs[LOWER_BOUND] = E; }

  /// Set length of the array section.
  void setLength(Expr *E) { SubExprs[LENGTH] = E; }

  /// Set length of the array section.
  void setStride(Expr *E) {
    assert(ASType != OpenACCArraySection &&
           "Stride not valid in OpenACC subarrays");
    SubExprs[STRIDE] = E;
  }

  void setColonLocFirst(SourceLocation L) { ColonLocFirst = L; }

  void setColonLocSecond(SourceLocation L) {
    assert(ASType != OpenACCArraySection &&
           "second colon for stride not valid in OpenACC subarrays");
    ColonLocSecond = L;
  }
  void setRBracketLoc(SourceLocation L) { RBracketLoc = L; }
};

/// Frontend produces RecoveryExprs on semantic errors that prevent creating
/// other well-formed expressions. E.g. when type-checking of a binary operator
/// fails, we cannot produce a BinaryOperator expression. Instead, we can choose
/// to produce a recovery expression storing left and right operands.
///
/// RecoveryExpr does not have any semantic meaning in C++, it is only useful to
/// preserve expressions in AST that would otherwise be dropped. It captures
/// subexpressions of some expression that we could not construct and source
/// range covered by the expression.
///
/// By default, RecoveryExpr uses dependence-bits to take advantage of existing
/// machinery to deal with dependent code in C++, e.g. RecoveryExpr is preserved
/// in `decltype(<broken-expr>)` as part of the `DependentDecltypeType`. In
/// addition to that, clang does not report most errors on dependent
/// expressions, so we get rid of bogus errors for free. However, note that
/// unlike other dependent expressions, RecoveryExpr can be produced in
/// non-template contexts.
///
/// We will preserve the type in RecoveryExpr when the type is known, e.g.
/// preserving the return type for a broken non-overloaded function call, a
/// overloaded call where all candidates have the same return type. In this
/// case, the expression is not type-dependent (unless the known type is itself
/// dependent)
///
/// One can also reliably suppress all bogus errors on expressions containing
/// recovery expressions by examining results of Expr::containsErrors().
class RecoveryExpr final : public Expr,
                           private llvm::TrailingObjects<RecoveryExpr, Expr *> {
public:
  static RecoveryExpr *Create(ASTContext &Ctx, QualType T,
                              SourceLocation BeginLoc, SourceLocation EndLoc,
                              ArrayRef<Expr *> SubExprs);
  static RecoveryExpr *CreateEmpty(ASTContext &Ctx, unsigned NumSubExprs);

  ArrayRef<Expr *> subExpressions() {
    auto *B = getTrailingObjects<Expr *>();
    return llvm::ArrayRef(B, B + NumExprs);
  }

  ArrayRef<const Expr *> subExpressions() const {
    return const_cast<RecoveryExpr *>(this)->subExpressions();
  }

  child_range children() {
    Stmt **B = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(B, B + NumExprs);
  }

  SourceLocation getBeginLoc() const { return BeginLoc; }
  SourceLocation getEndLoc() const { return EndLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == RecoveryExprClass;
  }

private:
  RecoveryExpr(ASTContext &Ctx, QualType T, SourceLocation BeginLoc,
               SourceLocation EndLoc, ArrayRef<Expr *> SubExprs);
  RecoveryExpr(EmptyShell Empty, unsigned NumSubExprs)
      : Expr(RecoveryExprClass, Empty), NumExprs(NumSubExprs) {}

  size_t numTrailingObjects(OverloadToken<Stmt *>) const { return NumExprs; }

  SourceLocation BeginLoc, EndLoc;
  unsigned NumExprs;
  friend TrailingObjects;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

} // end namespace clang

#endif // LLVM_CLANG_AST_EXPR_H
