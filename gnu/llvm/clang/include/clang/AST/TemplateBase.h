//===- TemplateBase.h - Core classes for C++ templates ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file provides definitions which are common for all kinds of
//  template representation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TEMPLATEBASE_H
#define LLVM_CLANG_AST_TEMPLATEBASE_H

#include "clang/AST/DependenceFlags.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace llvm {

class FoldingSetNodeID;

// Provide PointerLikeTypeTraits for clang::Expr*, this default one requires a
// full definition of Expr, but this file only sees a forward del because of
// the dependency.
template <> struct PointerLikeTypeTraits<clang::Expr *> {
  static inline void *getAsVoidPointer(clang::Expr *P) { return P; }
  static inline clang::Expr *getFromVoidPointer(void *P) {
    return static_cast<clang::Expr *>(P);
  }
  static constexpr int NumLowBitsAvailable = 2;
};

} // namespace llvm

namespace clang {

class APValue;
class ASTContext;
class Expr;
struct PrintingPolicy;
class TypeSourceInfo;
class ValueDecl;

/// Represents a template argument.
class TemplateArgument {
public:
  /// The kind of template argument we're storing.
  enum ArgKind {
    /// Represents an empty template argument, e.g., one that has not
    /// been deduced.
    Null = 0,

    /// The template argument is a type.
    Type,

    /// The template argument is a declaration that was provided for a pointer,
    /// reference, or pointer to member non-type template parameter.
    Declaration,

    /// The template argument is a null pointer or null pointer to member that
    /// was provided for a non-type template parameter.
    NullPtr,

    /// The template argument is an integral value stored in an llvm::APSInt
    /// that was provided for an integral non-type template parameter.
    Integral,

    /// The template argument is a non-type template argument that can't be
    /// represented by the special-case Declaration, NullPtr, or Integral
    /// forms. These values are only ever produced by constant evaluation,
    /// so cannot be dependent.
    /// TODO: merge Declaration, NullPtr and Integral into this?
    StructuralValue,

    /// The template argument is a template name that was provided for a
    /// template template parameter.
    Template,

    /// The template argument is a pack expansion of a template name that was
    /// provided for a template template parameter.
    TemplateExpansion,

    /// The template argument is an expression, and we've not resolved it to one
    /// of the other forms yet, either because it's dependent or because we're
    /// representing a non-canonical template argument (for instance, in a
    /// TemplateSpecializationType).
    Expression,

    /// The template argument is actually a parameter pack. Arguments are stored
    /// in the Args struct.
    Pack
  };

private:
  /// The kind of template argument we're storing.

  struct DA {
    LLVM_PREFERRED_TYPE(ArgKind)
    unsigned Kind : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDefaulted : 1;
    void *QT;
    ValueDecl *D;
  };
  struct I {
    LLVM_PREFERRED_TYPE(ArgKind)
    unsigned Kind : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDefaulted : 1;
    // We store a decomposed APSInt with the data allocated by ASTContext if
    // BitWidth > 64. The memory may be shared between multiple
    // TemplateArgument instances.
    unsigned BitWidth : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsUnsigned : 1;
    union {
      /// Used to store the <= 64 bits integer value.
      uint64_t VAL;

      /// Used to store the >64 bits integer value.
      const uint64_t *pVal;
    };
    void *Type;
  };
  struct V {
    LLVM_PREFERRED_TYPE(ArgKind)
    unsigned Kind : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDefaulted : 1;
    APValue *Value;
    void *Type;
  };
  struct A {
    LLVM_PREFERRED_TYPE(ArgKind)
    unsigned Kind : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDefaulted : 1;
    unsigned NumArgs;
    const TemplateArgument *Args;
  };
  struct TA {
    LLVM_PREFERRED_TYPE(ArgKind)
    unsigned Kind : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDefaulted : 1;
    unsigned NumExpansions;
    void *Name;
  };
  struct TV {
    LLVM_PREFERRED_TYPE(ArgKind)
    unsigned Kind : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDefaulted : 1;
    uintptr_t V;
  };
  union {
    struct DA DeclArg;
    struct I Integer;
    struct V Value;
    struct A Args;
    struct TA TemplateArg;
    struct TV TypeOrValue;
  };

  void initFromType(QualType T, bool IsNullPtr, bool IsDefaulted);
  void initFromDeclaration(ValueDecl *D, QualType QT, bool IsDefaulted);
  void initFromIntegral(const ASTContext &Ctx, const llvm::APSInt &Value,
                        QualType Type, bool IsDefaulted);
  void initFromStructural(const ASTContext &Ctx, QualType Type,
                          const APValue &V, bool IsDefaulted);

public:
  /// Construct an empty, invalid template argument.
  constexpr TemplateArgument() : TypeOrValue({Null, 0, /* IsDefaulted */ 0}) {}

  /// Construct a template type argument.
  TemplateArgument(QualType T, bool isNullPtr = false,
                   bool IsDefaulted = false) {
    initFromType(T, isNullPtr, IsDefaulted);
  }

  /// Construct a template argument that refers to a (non-dependent)
  /// declaration.
  TemplateArgument(ValueDecl *D, QualType QT, bool IsDefaulted = false) {
    initFromDeclaration(D, QT, IsDefaulted);
  }

  /// Construct an integral constant template argument. The memory to
  /// store the value is allocated with Ctx.
  TemplateArgument(const ASTContext &Ctx, const llvm::APSInt &Value,
                   QualType Type, bool IsDefaulted = false);

  /// Construct a template argument from an arbitrary constant value.
  TemplateArgument(const ASTContext &Ctx, QualType Type, const APValue &Value,
                   bool IsDefaulted = false);

  /// Construct an integral constant template argument with the same
  /// value as Other but a different type.
  TemplateArgument(const TemplateArgument &Other, QualType Type) {
    Integer = Other.Integer;
    Integer.Type = Type.getAsOpaquePtr();
  }

  /// Construct a template argument that is a template.
  ///
  /// This form of template argument is generally used for template template
  /// parameters. However, the template name could be a dependent template
  /// name that ends up being instantiated to a function template whose address
  /// is taken.
  ///
  /// \param Name The template name.
  ///
  /// \param IsDefaulted If 'true', implies that this TemplateArgument
  /// corresponds to a default template parameter
  TemplateArgument(TemplateName Name, bool IsDefaulted = false) {
    TemplateArg.Kind = Template;
    TemplateArg.IsDefaulted = IsDefaulted;
    TemplateArg.Name = Name.getAsVoidPointer();
    TemplateArg.NumExpansions = 0;
  }

  /// Construct a template argument that is a template pack expansion.
  ///
  /// This form of template argument is generally used for template template
  /// parameters. However, the template name could be a dependent template
  /// name that ends up being instantiated to a function template whose address
  /// is taken.
  ///
  /// \param Name The template name.
  ///
  /// \param NumExpansions The number of expansions that will be generated by
  /// instantiating
  ///
  /// \param IsDefaulted If 'true', implies that this TemplateArgument
  /// corresponds to a default template parameter
  TemplateArgument(TemplateName Name, std::optional<unsigned> NumExpansions,
                   bool IsDefaulted = false) {
    TemplateArg.Kind = TemplateExpansion;
    TemplateArg.IsDefaulted = IsDefaulted;
    TemplateArg.Name = Name.getAsVoidPointer();
    if (NumExpansions)
      TemplateArg.NumExpansions = *NumExpansions + 1;
    else
      TemplateArg.NumExpansions = 0;
  }

  /// Construct a template argument that is an expression.
  ///
  /// This form of template argument only occurs in template argument
  /// lists used for dependent types and for expression; it will not
  /// occur in a non-dependent, canonical template argument list.
  TemplateArgument(Expr *E, bool IsDefaulted = false) {
    TypeOrValue.Kind = Expression;
    TypeOrValue.IsDefaulted = IsDefaulted;
    TypeOrValue.V = reinterpret_cast<uintptr_t>(E);
  }

  /// Construct a template argument that is a template argument pack.
  ///
  /// We assume that storage for the template arguments provided
  /// outlives the TemplateArgument itself.
  explicit TemplateArgument(ArrayRef<TemplateArgument> Args) {
    this->Args.Kind = Pack;
    this->Args.IsDefaulted = false;
    this->Args.Args = Args.data();
    this->Args.NumArgs = Args.size();
  }

  static TemplateArgument getEmptyPack() {
    return TemplateArgument(std::nullopt);
  }

  /// Create a new template argument pack by copying the given set of
  /// template arguments.
  static TemplateArgument CreatePackCopy(ASTContext &Context,
                                         ArrayRef<TemplateArgument> Args);

  /// Return the kind of stored template argument.
  ArgKind getKind() const { return (ArgKind)TypeOrValue.Kind; }

  /// Determine whether this template argument has no value.
  bool isNull() const { return getKind() == Null; }

  TemplateArgumentDependence getDependence() const;

  /// Whether this template argument is dependent on a template
  /// parameter such that its result can change from one instantiation to
  /// another.
  bool isDependent() const;

  /// Whether this template argument is dependent on a template
  /// parameter.
  bool isInstantiationDependent() const;

  /// Whether this template argument contains an unexpanded
  /// parameter pack.
  bool containsUnexpandedParameterPack() const;

  /// Determine whether this template argument is a pack expansion.
  bool isPackExpansion() const;

  /// Retrieve the type for a type template argument.
  QualType getAsType() const {
    assert(getKind() == Type && "Unexpected kind");
    return QualType::getFromOpaquePtr(reinterpret_cast<void *>(TypeOrValue.V));
  }

  /// Retrieve the declaration for a declaration non-type
  /// template argument.
  ValueDecl *getAsDecl() const {
    assert(getKind() == Declaration && "Unexpected kind");
    return DeclArg.D;
  }

  QualType getParamTypeForDecl() const {
    assert(getKind() == Declaration && "Unexpected kind");
    return QualType::getFromOpaquePtr(DeclArg.QT);
  }

  /// Retrieve the type for null non-type template argument.
  QualType getNullPtrType() const {
    assert(getKind() == NullPtr && "Unexpected kind");
    return QualType::getFromOpaquePtr(reinterpret_cast<void *>(TypeOrValue.V));
  }

  /// Retrieve the template name for a template name argument.
  TemplateName getAsTemplate() const {
    assert(getKind() == Template && "Unexpected kind");
    return TemplateName::getFromVoidPointer(TemplateArg.Name);
  }

  /// Retrieve the template argument as a template name; if the argument
  /// is a pack expansion, return the pattern as a template name.
  TemplateName getAsTemplateOrTemplatePattern() const {
    assert((getKind() == Template || getKind() == TemplateExpansion) &&
           "Unexpected kind");

    return TemplateName::getFromVoidPointer(TemplateArg.Name);
  }

  /// Retrieve the number of expansions that a template template argument
  /// expansion will produce, if known.
  std::optional<unsigned> getNumTemplateExpansions() const;

  /// Retrieve the template argument as an integral value.
  // FIXME: Provide a way to read the integral data without copying the value.
  llvm::APSInt getAsIntegral() const {
    assert(getKind() == Integral && "Unexpected kind");

    using namespace llvm;

    if (Integer.BitWidth <= 64)
      return APSInt(APInt(Integer.BitWidth, Integer.VAL), Integer.IsUnsigned);

    unsigned NumWords = APInt::getNumWords(Integer.BitWidth);
    return APSInt(APInt(Integer.BitWidth, ArrayRef(Integer.pVal, NumWords)),
                  Integer.IsUnsigned);
  }

  /// Retrieve the type of the integral value.
  QualType getIntegralType() const {
    assert(getKind() == Integral && "Unexpected kind");
    return QualType::getFromOpaquePtr(Integer.Type);
  }

  void setIntegralType(QualType T) {
    assert(getKind() == Integral && "Unexpected kind");
    Integer.Type = T.getAsOpaquePtr();
  }

  /// Set to 'true' if this TemplateArgument corresponds to a
  /// default template parameter.
  void setIsDefaulted(bool v) { TypeOrValue.IsDefaulted = v; }

  /// If returns 'true', this TemplateArgument corresponds to a
  /// default template parameter.
  bool getIsDefaulted() const { return (bool)TypeOrValue.IsDefaulted; }

  /// Get the value of a StructuralValue.
  const APValue &getAsStructuralValue() const { return *Value.Value; }

  /// Get the type of a StructuralValue.
  QualType getStructuralValueType() const {
    return QualType::getFromOpaquePtr(Value.Type);
  }

  /// If this is a non-type template argument, get its type. Otherwise,
  /// returns a null QualType.
  QualType getNonTypeTemplateArgumentType() const;

  /// Retrieve the template argument as an expression.
  Expr *getAsExpr() const {
    assert(getKind() == Expression && "Unexpected kind");
    return reinterpret_cast<Expr *>(TypeOrValue.V);
  }

  /// Iterator that traverses the elements of a template argument pack.
  using pack_iterator = const TemplateArgument *;

  /// Iterator referencing the first argument of a template argument
  /// pack.
  pack_iterator pack_begin() const {
    assert(getKind() == Pack);
    return Args.Args;
  }

  /// Iterator referencing one past the last argument of a template
  /// argument pack.
  pack_iterator pack_end() const {
    assert(getKind() == Pack);
    return Args.Args + Args.NumArgs;
  }

  /// Iterator range referencing all of the elements of a template
  /// argument pack.
  ArrayRef<TemplateArgument> pack_elements() const {
    return llvm::ArrayRef(pack_begin(), pack_end());
  }

  /// The number of template arguments in the given template argument
  /// pack.
  unsigned pack_size() const {
    assert(getKind() == Pack);
    return Args.NumArgs;
  }

  /// Return the array of arguments in this template argument pack.
  ArrayRef<TemplateArgument> getPackAsArray() const {
    assert(getKind() == Pack);
    return llvm::ArrayRef(Args.Args, Args.NumArgs);
  }

  /// Determines whether two template arguments are superficially the
  /// same.
  bool structurallyEquals(const TemplateArgument &Other) const;

  /// When the template argument is a pack expansion, returns
  /// the pattern of the pack expansion.
  TemplateArgument getPackExpansionPattern() const;

  /// Print this template argument to the given output stream.
  void print(const PrintingPolicy &Policy, raw_ostream &Out,
             bool IncludeType) const;

  /// Debugging aid that dumps the template argument.
  void dump(raw_ostream &Out, const ASTContext &Context) const;

  /// Debugging aid that dumps the template argument to standard error.
  void dump() const;

  /// Used to insert TemplateArguments into FoldingSets.
  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) const;
};

/// Location information for a TemplateArgument.
struct TemplateArgumentLocInfo {
private:
  struct TemplateTemplateArgLocInfo {
    // FIXME: We'd like to just use the qualifier in the TemplateName,
    // but template arguments get canonicalized too quickly.
    NestedNameSpecifier *Qualifier;
    void *QualifierLocData;
    SourceLocation TemplateNameLoc;
    SourceLocation EllipsisLoc;
  };

  llvm::PointerUnion<TemplateTemplateArgLocInfo *, Expr *, TypeSourceInfo *>
      Pointer;

  TemplateTemplateArgLocInfo *getTemplate() const {
    return Pointer.get<TemplateTemplateArgLocInfo *>();
  }

public:
  TemplateArgumentLocInfo() {}
  TemplateArgumentLocInfo(TypeSourceInfo *Declarator) { Pointer = Declarator; }

  TemplateArgumentLocInfo(Expr *E) { Pointer = E; }
  // Ctx is used for allocation -- this case is unusually large and also rare,
  // so we store the payload out-of-line.
  TemplateArgumentLocInfo(ASTContext &Ctx, NestedNameSpecifierLoc QualifierLoc,
                          SourceLocation TemplateNameLoc,
                          SourceLocation EllipsisLoc);

  TypeSourceInfo *getAsTypeSourceInfo() const {
    return Pointer.get<TypeSourceInfo *>();
  }

  Expr *getAsExpr() const { return Pointer.get<Expr *>(); }

  NestedNameSpecifierLoc getTemplateQualifierLoc() const {
    const auto *Template = getTemplate();
    return NestedNameSpecifierLoc(Template->Qualifier,
                                  Template->QualifierLocData);
  }

  SourceLocation getTemplateNameLoc() const {
    return getTemplate()->TemplateNameLoc;
  }

  SourceLocation getTemplateEllipsisLoc() const {
    return getTemplate()->EllipsisLoc;
  }
};

/// Location wrapper for a TemplateArgument.  TemplateArgument is to
/// TemplateArgumentLoc as Type is to TypeLoc.
class TemplateArgumentLoc {
  TemplateArgument Argument;
  TemplateArgumentLocInfo LocInfo;

public:
  TemplateArgumentLoc() {}

  TemplateArgumentLoc(const TemplateArgument &Argument,
                      TemplateArgumentLocInfo Opaque)
      : Argument(Argument), LocInfo(Opaque) {}

  TemplateArgumentLoc(const TemplateArgument &Argument, TypeSourceInfo *TInfo)
      : Argument(Argument), LocInfo(TInfo) {
    assert(Argument.getKind() == TemplateArgument::Type);
  }

  TemplateArgumentLoc(const TemplateArgument &Argument, Expr *E)
      : Argument(Argument), LocInfo(E) {

    // Permit any kind of template argument that can be represented with an
    // expression.
    assert(Argument.getKind() == TemplateArgument::NullPtr ||
           Argument.getKind() == TemplateArgument::Integral ||
           Argument.getKind() == TemplateArgument::Declaration ||
           Argument.getKind() == TemplateArgument::StructuralValue ||
           Argument.getKind() == TemplateArgument::Expression);
  }

  TemplateArgumentLoc(ASTContext &Ctx, const TemplateArgument &Argument,
                      NestedNameSpecifierLoc QualifierLoc,
                      SourceLocation TemplateNameLoc,
                      SourceLocation EllipsisLoc = SourceLocation())
      : Argument(Argument),
        LocInfo(Ctx, QualifierLoc, TemplateNameLoc, EllipsisLoc) {
    assert(Argument.getKind() == TemplateArgument::Template ||
           Argument.getKind() == TemplateArgument::TemplateExpansion);
  }

  /// - Fetches the primary location of the argument.
  SourceLocation getLocation() const {
    if (Argument.getKind() == TemplateArgument::Template ||
        Argument.getKind() == TemplateArgument::TemplateExpansion)
      return getTemplateNameLoc();

    return getSourceRange().getBegin();
  }

  /// - Fetches the full source range of the argument.
  SourceRange getSourceRange() const LLVM_READONLY;

  const TemplateArgument &getArgument() const { return Argument; }

  TemplateArgumentLocInfo getLocInfo() const { return LocInfo; }

  TypeSourceInfo *getTypeSourceInfo() const {
    if (Argument.getKind() != TemplateArgument::Type)
      return nullptr;
    return LocInfo.getAsTypeSourceInfo();
  }

  Expr *getSourceExpression() const {
    assert(Argument.getKind() == TemplateArgument::Expression);
    return LocInfo.getAsExpr();
  }

  Expr *getSourceDeclExpression() const {
    assert(Argument.getKind() == TemplateArgument::Declaration);
    return LocInfo.getAsExpr();
  }

  Expr *getSourceNullPtrExpression() const {
    assert(Argument.getKind() == TemplateArgument::NullPtr);
    return LocInfo.getAsExpr();
  }

  Expr *getSourceIntegralExpression() const {
    assert(Argument.getKind() == TemplateArgument::Integral);
    return LocInfo.getAsExpr();
  }

  Expr *getSourceStructuralValueExpression() const {
    assert(Argument.getKind() == TemplateArgument::StructuralValue);
    return LocInfo.getAsExpr();
  }

  NestedNameSpecifierLoc getTemplateQualifierLoc() const {
    if (Argument.getKind() != TemplateArgument::Template &&
        Argument.getKind() != TemplateArgument::TemplateExpansion)
      return NestedNameSpecifierLoc();
    return LocInfo.getTemplateQualifierLoc();
  }

  SourceLocation getTemplateNameLoc() const {
    if (Argument.getKind() != TemplateArgument::Template &&
        Argument.getKind() != TemplateArgument::TemplateExpansion)
      return SourceLocation();
    return LocInfo.getTemplateNameLoc();
  }

  SourceLocation getTemplateEllipsisLoc() const {
    if (Argument.getKind() != TemplateArgument::TemplateExpansion)
      return SourceLocation();
    return LocInfo.getTemplateEllipsisLoc();
  }
};

/// A convenient class for passing around template argument
/// information.  Designed to be passed by reference.
class TemplateArgumentListInfo {
  SmallVector<TemplateArgumentLoc, 8> Arguments;
  SourceLocation LAngleLoc;
  SourceLocation RAngleLoc;

public:
  TemplateArgumentListInfo() = default;

  TemplateArgumentListInfo(SourceLocation LAngleLoc, SourceLocation RAngleLoc)
      : LAngleLoc(LAngleLoc), RAngleLoc(RAngleLoc) {}

  // This can leak if used in an AST node, use ASTTemplateArgumentListInfo
  // instead.
  void *operator new(size_t bytes, ASTContext &C) = delete;

  SourceLocation getLAngleLoc() const { return LAngleLoc; }
  SourceLocation getRAngleLoc() const { return RAngleLoc; }

  void setLAngleLoc(SourceLocation Loc) { LAngleLoc = Loc; }
  void setRAngleLoc(SourceLocation Loc) { RAngleLoc = Loc; }

  unsigned size() const { return Arguments.size(); }

  const TemplateArgumentLoc *getArgumentArray() const {
    return Arguments.data();
  }

  llvm::ArrayRef<TemplateArgumentLoc> arguments() const { return Arguments; }

  const TemplateArgumentLoc &operator[](unsigned I) const {
    return Arguments[I];
  }

  TemplateArgumentLoc &operator[](unsigned I) { return Arguments[I]; }

  void addArgument(const TemplateArgumentLoc &Loc) { Arguments.push_back(Loc); }
};

/// Represents an explicit template argument list in C++, e.g.,
/// the "<int>" in "sort<int>".
/// This is safe to be used inside an AST node, in contrast with
/// TemplateArgumentListInfo.
struct ASTTemplateArgumentListInfo final
    : private llvm::TrailingObjects<ASTTemplateArgumentListInfo,
                                    TemplateArgumentLoc> {
private:
  friend class ASTNodeImporter;
  friend TrailingObjects;

  ASTTemplateArgumentListInfo(const TemplateArgumentListInfo &List);

  // FIXME: Is it ever necessary to copy to another context?
  ASTTemplateArgumentListInfo(const ASTTemplateArgumentListInfo *List);

public:
  /// The source location of the left angle bracket ('<').
  SourceLocation LAngleLoc;

  /// The source location of the right angle bracket ('>').
  SourceLocation RAngleLoc;

  /// The number of template arguments in TemplateArgs.
  unsigned NumTemplateArgs;

  SourceLocation getLAngleLoc() const { return LAngleLoc; }
  SourceLocation getRAngleLoc() const { return RAngleLoc; }

  /// Retrieve the template arguments
  const TemplateArgumentLoc *getTemplateArgs() const {
    return getTrailingObjects<TemplateArgumentLoc>();
  }
  unsigned getNumTemplateArgs() const { return NumTemplateArgs; }

  llvm::ArrayRef<TemplateArgumentLoc> arguments() const {
    return llvm::ArrayRef(getTemplateArgs(), getNumTemplateArgs());
  }

  const TemplateArgumentLoc &operator[](unsigned I) const {
    return getTemplateArgs()[I];
  }

  static const ASTTemplateArgumentListInfo *
  Create(const ASTContext &C, const TemplateArgumentListInfo &List);

  // FIXME: Is it ever necessary to copy to another context?
  static const ASTTemplateArgumentListInfo *
  Create(const ASTContext &C, const ASTTemplateArgumentListInfo *List);
};

/// Represents an explicit template argument list in C++, e.g.,
/// the "<int>" in "sort<int>".
///
/// It is intended to be used as a trailing object on AST nodes, and
/// as such, doesn't contain the array of TemplateArgumentLoc itself,
/// but expects the containing object to also provide storage for
/// that.
struct alignas(void *) ASTTemplateKWAndArgsInfo {
  /// The source location of the left angle bracket ('<').
  SourceLocation LAngleLoc;

  /// The source location of the right angle bracket ('>').
  SourceLocation RAngleLoc;

  /// The source location of the template keyword; this is used
  /// as part of the representation of qualified identifiers, such as
  /// S<T>::template apply<T>.  Will be empty if this expression does
  /// not have a template keyword.
  SourceLocation TemplateKWLoc;

  /// The number of template arguments in TemplateArgs.
  unsigned NumTemplateArgs;

  void initializeFrom(SourceLocation TemplateKWLoc,
                      const TemplateArgumentListInfo &List,
                      TemplateArgumentLoc *OutArgArray);
  // FIXME: The parameter Deps is the result populated by this method, the
  // caller doesn't need it since it is populated by computeDependence. remove
  // it.
  void initializeFrom(SourceLocation TemplateKWLoc,
                      const TemplateArgumentListInfo &List,
                      TemplateArgumentLoc *OutArgArray,
                      TemplateArgumentDependence &Deps);
  void initializeFrom(SourceLocation TemplateKWLoc);

  void copyInto(const TemplateArgumentLoc *ArgArray,
                TemplateArgumentListInfo &List) const;
};

const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      const TemplateArgument &Arg);

} // namespace clang

#endif // LLVM_CLANG_AST_TEMPLATEBASE_H
