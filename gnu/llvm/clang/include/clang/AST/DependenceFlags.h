//===--- DependenceFlags.h ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_DEPENDENCEFLAGS_H
#define LLVM_CLANG_AST_DEPENDENCEFLAGS_H

#include "clang/Basic/BitmaskEnum.h"
#include "llvm/ADT/BitmaskEnum.h"
#include <cstdint>

namespace clang {
struct ExprDependenceScope {
  enum ExprDependence : uint8_t {
    UnexpandedPack = 1,
    // This expr depends in any way on
    //   - a template parameter, it implies that the resolution of this expr may
    //     cause instantiation to fail
    //   - or an error (often in a non-template context)
    //
    // Note that C++ standard doesn't define the instantiation-dependent term,
    // we follow the formal definition coming from the Itanium C++ ABI, and
    // extend it to errors.
    Instantiation = 2,
    // The type of this expr depends on a template parameter, or an error.
    Type = 4,
    // The value of this expr depends on a template parameter, or an error.
    Value = 8,

    // clang extension: this expr contains or references an error, and is
    // considered dependent on how that error is resolved.
    Error = 16,

    None = 0,
    All = 31,

    TypeValue = Type | Value,
    TypeInstantiation = Type | Instantiation,
    ValueInstantiation = Value | Instantiation,
    TypeValueInstantiation = Type | Value | Instantiation,
    ErrorDependent = Error | ValueInstantiation,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/Error)
  };
};
using ExprDependence = ExprDependenceScope::ExprDependence;

struct TypeDependenceScope {
  enum TypeDependence : uint8_t {
    /// Whether this type contains an unexpanded parameter pack
    /// (for C++11 variadic templates)
    UnexpandedPack = 1,
    /// Whether this type somehow involves
    ///   - a template parameter, even if the resolution of the type does not
    ///     depend on a template parameter.
    ///   - or an error.
    Instantiation = 2,
    /// Whether this type
    ///   - is a dependent type (C++ [temp.dep.type])
    ///   - or it somehow involves an error, e.g. decltype(recovery-expr)
    Dependent = 4,
    /// Whether this type is a variably-modified type (C99 6.7.5).
    VariablyModified = 8,

    /// Whether this type references an error, e.g. decltype(err-expression)
    /// yields an error type.
    Error = 16,

    None = 0,
    All = 31,

    DependentInstantiation = Dependent | Instantiation,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/Error)
  };
};
using TypeDependence = TypeDependenceScope::TypeDependence;

#define LLVM_COMMON_DEPENDENCE(NAME)                                           \
  struct NAME##Scope {                                                         \
    enum NAME : uint8_t {                                                      \
      UnexpandedPack = 1,                                                      \
      Instantiation = 2,                                                       \
      Dependent = 4,                                                           \
      Error = 8,                                                               \
                                                                               \
      None = 0,                                                                \
      DependentInstantiation = Dependent | Instantiation,                      \
      All = 15,                                                                \
                                                                               \
      LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/Error)                        \
    };                                                                         \
  };                                                                           \
  using NAME = NAME##Scope::NAME;

LLVM_COMMON_DEPENDENCE(NestedNameSpecifierDependence)
LLVM_COMMON_DEPENDENCE(TemplateNameDependence)
LLVM_COMMON_DEPENDENCE(TemplateArgumentDependence)
#undef LLVM_COMMON_DEPENDENCE

// A combined space of all dependence concepts for all node types.
// Used when aggregating dependence of nodes of different types.
class Dependence {
public:
  enum Bits : uint8_t {
    None = 0,

    // Contains a template parameter pack that wasn't expanded.
    UnexpandedPack = 1,
    // Depends on a template parameter or an error in some way.
    // Validity depends on how the template is instantiated or the error is
    // resolved.
    Instantiation = 2,
    // Expression type depends on template context, or an error.
    // Value and Instantiation should also be set.
    Type = 4,
    // Expression value depends on template context, or an error.
    // Instantiation should also be set.
    Value = 8,
    // Depends on template context, or an error.
    // The type/value distinction is only meaningful for expressions.
    Dependent = Type | Value,
    // Includes an error, and depends on how it is resolved.
    Error = 16,
    // Type depends on a runtime value (variable-length array).
    VariablyModified = 32,

    // Dependence that is propagated syntactically, regardless of semantics.
    Syntactic = UnexpandedPack | Instantiation | Error,
    // Dependence that is propagated semantically, even in cases where the
    // type doesn't syntactically appear. This currently excludes only
    // UnexpandedPack. Even though Instantiation dependence is also notionally
    // syntactic, we also want to propagate it semantically because anything
    // that semantically depends on an instantiation-dependent entity should
    // always be instantiated when that instantiation-dependent entity is.
    Semantic =
        Instantiation | Type | Value | Dependent | Error | VariablyModified,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/VariablyModified)
  };

  Dependence() : V(None) {}

  Dependence(TypeDependence D)
      : V(translate(D, TypeDependence::UnexpandedPack, UnexpandedPack) |
          translate(D, TypeDependence::Instantiation, Instantiation) |
          translate(D, TypeDependence::Dependent, Dependent) |
          translate(D, TypeDependence::Error, Error) |
          translate(D, TypeDependence::VariablyModified, VariablyModified)) {}

  Dependence(ExprDependence D)
      : V(translate(D, ExprDependence::UnexpandedPack, UnexpandedPack) |
             translate(D, ExprDependence::Instantiation, Instantiation) |
             translate(D, ExprDependence::Type, Type) |
             translate(D, ExprDependence::Value, Value) |
             translate(D, ExprDependence::Error, Error)) {}

  Dependence(NestedNameSpecifierDependence D) :
    V ( translate(D, NNSDependence::UnexpandedPack, UnexpandedPack) |
            translate(D, NNSDependence::Instantiation, Instantiation) |
            translate(D, NNSDependence::Dependent, Dependent) |
            translate(D, NNSDependence::Error, Error)) {}

  Dependence(TemplateArgumentDependence D)
      : V(translate(D, TADependence::UnexpandedPack, UnexpandedPack) |
          translate(D, TADependence::Instantiation, Instantiation) |
          translate(D, TADependence::Dependent, Dependent) |
          translate(D, TADependence::Error, Error)) {}

  Dependence(TemplateNameDependence D)
      : V(translate(D, TNDependence::UnexpandedPack, UnexpandedPack) |
             translate(D, TNDependence::Instantiation, Instantiation) |
             translate(D, TNDependence::Dependent, Dependent) |
             translate(D, TNDependence::Error, Error)) {}

  /// Extract only the syntactic portions of this type's dependence.
  Dependence syntactic() {
    Dependence Result = *this;
    Result.V &= Syntactic;
    return Result;
  }

  /// Extract the semantic portions of this type's dependence that apply even
  /// to uses where the type does not appear syntactically.
  Dependence semantic() {
    Dependence Result = *this;
    Result.V &= Semantic;
    return Result;
  }

  TypeDependence type() const {
    return translate(V, UnexpandedPack, TypeDependence::UnexpandedPack) |
           translate(V, Instantiation, TypeDependence::Instantiation) |
           translate(V, Dependent, TypeDependence::Dependent) |
           translate(V, Error, TypeDependence::Error) |
           translate(V, VariablyModified, TypeDependence::VariablyModified);
  }

  ExprDependence expr() const {
    return translate(V, UnexpandedPack, ExprDependence::UnexpandedPack) |
           translate(V, Instantiation, ExprDependence::Instantiation) |
           translate(V, Type, ExprDependence::Type) |
           translate(V, Value, ExprDependence::Value) |
           translate(V, Error, ExprDependence::Error);
  }

  NestedNameSpecifierDependence nestedNameSpecifier() const {
    return translate(V, UnexpandedPack, NNSDependence::UnexpandedPack) |
           translate(V, Instantiation, NNSDependence::Instantiation) |
           translate(V, Dependent, NNSDependence::Dependent) |
           translate(V, Error, NNSDependence::Error);
  }

  TemplateArgumentDependence templateArgument() const {
    return translate(V, UnexpandedPack, TADependence::UnexpandedPack) |
           translate(V, Instantiation, TADependence::Instantiation) |
           translate(V, Dependent, TADependence::Dependent) |
           translate(V, Error, TADependence::Error);
  }

  TemplateNameDependence templateName() const {
    return translate(V, UnexpandedPack, TNDependence::UnexpandedPack) |
           translate(V, Instantiation, TNDependence::Instantiation) |
           translate(V, Dependent, TNDependence::Dependent) |
           translate(V, Error, TNDependence::Error);
  }

private:
  Bits V;

  template <typename T, typename U>
  static U translate(T Bits, T FromBit, U ToBit) {
    return (Bits & FromBit) ? ToBit : static_cast<U>(0);
  }

  // Abbreviations to make conversions more readable.
  using NNSDependence = NestedNameSpecifierDependence;
  using TADependence = TemplateArgumentDependence;
  using TNDependence = TemplateNameDependence;
};

/// Computes dependencies of a reference with the name having template arguments
/// with \p TA dependencies.
inline ExprDependence toExprDependence(TemplateArgumentDependence TA) {
  return Dependence(TA).expr();
}
inline ExprDependence toExprDependenceForImpliedType(TypeDependence D) {
  return Dependence(D).semantic().expr();
}
inline ExprDependence toExprDependenceAsWritten(TypeDependence D) {
  return Dependence(D).expr();
}
// Note: it's often necessary to strip `Dependent` from qualifiers.
// If V<T>:: refers to the current instantiation, NNS is considered dependent
// but the containing V<T>::foo likely isn't.
inline ExprDependence toExprDependence(NestedNameSpecifierDependence D) {
  return Dependence(D).expr();
}
inline ExprDependence turnTypeToValueDependence(ExprDependence D) {
  // Type-dependent expressions are always be value-dependent, so we simply drop
  // type dependency.
  return D & ~ExprDependence::Type;
}
inline ExprDependence turnValueToTypeDependence(ExprDependence D) {
  // Type-dependent expressions are always be value-dependent.
  if (D & ExprDependence::Value)
    D |= ExprDependence::Type;
  return D;
}

// Returned type-dependence will never have VariablyModified set.
inline TypeDependence toTypeDependence(ExprDependence D) {
  return Dependence(D).type();
}
inline TypeDependence toTypeDependence(NestedNameSpecifierDependence D) {
  return Dependence(D).type();
}
inline TypeDependence toTypeDependence(TemplateNameDependence D) {
  return Dependence(D).type();
}
inline TypeDependence toTypeDependence(TemplateArgumentDependence D) {
  return Dependence(D).type();
}

inline TypeDependence toSyntacticDependence(TypeDependence D) {
  return Dependence(D).syntactic().type();
}
inline TypeDependence toSemanticDependence(TypeDependence D) {
  return Dependence(D).semantic().type();
}

inline NestedNameSpecifierDependence
toNestedNameSpecifierDependendence(TypeDependence D) {
  return Dependence(D).nestedNameSpecifier();
}

inline TemplateArgumentDependence
toTemplateArgumentDependence(TypeDependence D) {
  return Dependence(D).templateArgument();
}
inline TemplateArgumentDependence
toTemplateArgumentDependence(TemplateNameDependence D) {
  return Dependence(D).templateArgument();
}
inline TemplateArgumentDependence
toTemplateArgumentDependence(ExprDependence D) {
  return Dependence(D).templateArgument();
}

inline TemplateNameDependence
toTemplateNameDependence(NestedNameSpecifierDependence D) {
  return Dependence(D).templateName();
}

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

} // namespace clang
#endif
