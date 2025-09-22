//===- CallDescription.h - function/method call matching       --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file defines a generic mechanism for matching for function and
/// method calls of C, C++, and Objective-C languages. Instances of these
/// classes are frequently used together with the CallEvent classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CALLDESCRIPTION_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CALLDESCRIPTION_H

#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include <optional>
#include <vector>

namespace clang {
class IdentifierInfo;
} // namespace clang

namespace clang {
namespace ento {
/// A `CallDescription` is a pattern that can be used to _match_ calls
/// based on the qualified name and the argument/parameter counts.
class CallDescription {
public:
  enum class Mode {
    /// Match calls to functions from the C standard library. This also
    /// recognizes builtin variants whose name is derived by adding
    /// "__builtin", "__inline" or similar prefixes or suffixes; but only
    /// matches functions than are externally visible and are declared either
    /// directly within a TU or in the namespace 'std'.
    /// For the exact heuristics, see CheckerContext::isCLibraryFunction().
    CLibrary,

    /// An extended version of the `CLibrary` mode that also matches the
    /// hardened variants like __FOO_chk() and __builtin__FOO_chk() that take
    /// additional arguments compared to the "regular" function FOO().
    /// This is not the default behavior of `CLibrary` because in this case the
    /// checker code must be prepared to handle the different parametrization.
    /// For the exact heuristics, see CheckerContext::isHardenedVariantOf().
    CLibraryMaybeHardened,

    /// Matches "simple" functions that are not methods. (Static methods are
    /// methods.)
    SimpleFunc,

    /// Matches a C++ method (may be static, may be virtual, may be an
    /// overloaded operator, a constructor or a destructor).
    CXXMethod,

    /// Match any CallEvent that is not an ObjCMethodCall. This should not be
    /// used when the checker looks for a concrete function (and knows whether
    /// it is a method); but GenericTaintChecker uses this mode to match
    /// functions whose name was configured by the user.
    Unspecified,

    /// FIXME: Add support for ObjCMethodCall events (I'm not adding it because
    /// I'm not familiar with Objective-C). Note that currently an early return
    /// in `bool matches(const CallEvent &Call) const;` discards all
    /// Objective-C method calls.
  };

private:
  friend class CallEvent;
  using MaybeCount = std::optional<unsigned>;

  mutable std::optional<const IdentifierInfo *> II;
  // The list of the qualified names used to identify the specified CallEvent,
  // e.g. "{a, b}" represent the qualified names, like "a::b".
  std::vector<std::string> QualifiedName;
  MaybeCount RequiredArgs;
  MaybeCount RequiredParams;
  Mode MatchAs;

public:
  /// Constructs a CallDescription object.
  ///
  /// @param MatchAs Specifies the kind of the call that should be matched.
  ///
  /// @param QualifiedName The list of the name qualifiers of the function that
  /// will be matched. The user is allowed to skip any of the qualifiers.
  /// For example, {"std", "basic_string", "c_str"} would match both
  /// std::basic_string<...>::c_str() and std::__1::basic_string<...>::c_str().
  ///
  /// @param RequiredArgs The expected number of arguments that are passed to
  /// the function. Omit this parameter (or pass std::nullopt) to match every
  /// occurrence without checking the argument count in the call.
  ///
  /// @param RequiredParams The expected number of parameters in the function
  /// definition that is called. Omit this parameter to match every occurrence
  /// without checking the parameter count in the definition.
  CallDescription(Mode MatchAs, ArrayRef<StringRef> QualifiedName,
                  MaybeCount RequiredArgs = std::nullopt,
                  MaybeCount RequiredParams = std::nullopt);

  /// Get the name of the function that this object matches.
  StringRef getFunctionName() const { return QualifiedName.back(); }

  /// Get the qualified name parts in reversed order.
  /// E.g. { "std", "vector", "data" } -> "vector", "std"
  auto begin_qualified_name_parts() const {
    return std::next(QualifiedName.rbegin());
  }
  auto end_qualified_name_parts() const { return QualifiedName.rend(); }

  /// It's false, if and only if we expect a single identifier, such as
  /// `getenv`. It's true for `std::swap`, or `my::detail::container::data`.
  bool hasQualifiedNameParts() const { return QualifiedName.size() > 1; }

  /// @name Matching CallDescriptions against a CallEvent
  /// @{

  /// Returns true if the CallEvent is a call to a function that matches
  /// the CallDescription.
  ///
  /// \note This function is not intended to be used to match Obj-C method
  /// calls.
  bool matches(const CallEvent &Call) const;

  /// Returns true whether the CallEvent matches on any of the CallDescriptions
  /// supplied.
  ///
  /// \note This function is not intended to be used to match Obj-C method
  /// calls.
  friend bool matchesAny(const CallEvent &Call, const CallDescription &CD1) {
    return CD1.matches(Call);
  }

  /// \copydoc clang::ento::CallDescription::matchesAny(const CallEvent &, const CallDescription &)
  template <typename... Ts>
  friend bool matchesAny(const CallEvent &Call, const CallDescription &CD1,
                         const Ts &...CDs) {
    return CD1.matches(Call) || matchesAny(Call, CDs...);
  }
  /// @}

  /// @name Matching CallDescriptions against a CallExpr
  /// @{

  /// Returns true if the CallExpr is a call to a function that matches the
  /// CallDescription.
  ///
  /// When available, always prefer matching with a CallEvent! This function
  /// exists only when that is not available, for example, when _only_
  /// syntactic check is done on a piece of code.
  ///
  /// Also, StdLibraryFunctionsChecker::Signature is likely a better candicade
  /// for syntactic only matching if you are writing a new checker. This is
  /// handy if a CallDescriptionMap is already there.
  ///
  /// The function is imprecise because CallEvent may know path sensitive
  /// information, such as the precise argument count (see comments for
  /// CallEvent::getNumArgs), the called function if it was called through a
  /// function pointer, and other information not available syntactically.
  bool matchesAsWritten(const CallExpr &CE) const;

  /// Returns true whether the CallExpr matches on any of the CallDescriptions
  /// supplied.
  ///
  /// \note This function is not intended to be used to match Obj-C method
  /// calls.
  friend bool matchesAnyAsWritten(const CallExpr &CE,
                                  const CallDescription &CD1) {
    return CD1.matchesAsWritten(CE);
  }

  /// \copydoc clang::ento::CallDescription::matchesAnyAsWritten(const CallExpr &, const CallDescription &)
  template <typename... Ts>
  friend bool matchesAnyAsWritten(const CallExpr &CE,
                                  const CallDescription &CD1,
                                  const Ts &...CDs) {
    return CD1.matchesAsWritten(CE) || matchesAnyAsWritten(CE, CDs...);
  }
  /// @}

private:
  bool matchesImpl(const FunctionDecl *Callee, size_t ArgCount,
                   size_t ParamCount) const;

  bool matchNameOnly(const NamedDecl *ND) const;
  bool matchQualifiedNameParts(const Decl *D) const;
};

/// An immutable map from CallDescriptions to arbitrary data. Provides a unified
/// way for checkers to react on function calls.
template <typename T> class CallDescriptionMap {
  friend class CallDescriptionSet;

  // Some call descriptions aren't easily hashable (eg., the ones with qualified
  // names in which some sections are omitted), so let's put them
  // in a simple vector and use linear lookup.
  // TODO: Implement an actual map for fast lookup for "hashable" call
  // descriptions (eg., the ones for C functions that just match the name).
  std::vector<std::pair<CallDescription, T>> LinearMap;

public:
  CallDescriptionMap(
      std::initializer_list<std::pair<CallDescription, T>> &&List)
      : LinearMap(List) {}

  template <typename InputIt>
  CallDescriptionMap(InputIt First, InputIt Last) : LinearMap(First, Last) {}

  ~CallDescriptionMap() = default;

  // These maps are usually stored once per checker, so let's make sure
  // we don't do redundant copies.
  CallDescriptionMap(const CallDescriptionMap &) = delete;
  CallDescriptionMap &operator=(const CallDescription &) = delete;

  CallDescriptionMap(CallDescriptionMap &&) = default;
  CallDescriptionMap &operator=(CallDescriptionMap &&) = default;

  [[nodiscard]] const T *lookup(const CallEvent &Call) const {
    // Slow path: linear lookup.
    // TODO: Implement some sort of fast path.
    for (const std::pair<CallDescription, T> &I : LinearMap)
      if (I.first.matches(Call))
        return &I.second;

    return nullptr;
  }

  /// When available, always prefer lookup with a CallEvent! This function
  /// exists only when that is not available, for example, when _only_
  /// syntactic check is done on a piece of code.
  ///
  /// Also, StdLibraryFunctionsChecker::Signature is likely a better candicade
  /// for syntactic only matching if you are writing a new checker. This is
  /// handy if a CallDescriptionMap is already there.
  ///
  /// The function is imprecise because CallEvent may know path sensitive
  /// information, such as the precise argument count (see comments for
  /// CallEvent::getNumArgs), the called function if it was called through a
  /// function pointer, and other information not available syntactically.
  [[nodiscard]] const T *lookupAsWritten(const CallExpr &Call) const {
    // Slow path: linear lookup.
    // TODO: Implement some sort of fast path.
    for (const std::pair<CallDescription, T> &I : LinearMap)
      if (I.first.matchesAsWritten(Call))
        return &I.second;

    return nullptr;
  }
};

/// Enumerators of this enum class are used to construct CallDescription
/// objects; in that context the fully qualified name is needlessly verbose.
using CDM = CallDescription::Mode;

/// An immutable set of CallDescriptions.
/// Checkers can efficiently decide if a given CallEvent matches any
/// CallDescription in the set.
class CallDescriptionSet {
  CallDescriptionMap<bool /*unused*/> Impl = {};

public:
  CallDescriptionSet(std::initializer_list<CallDescription> &&List);

  CallDescriptionSet(const CallDescriptionSet &) = delete;
  CallDescriptionSet &operator=(const CallDescription &) = delete;

  [[nodiscard]] bool contains(const CallEvent &Call) const;

  /// When available, always prefer lookup with a CallEvent! This function
  /// exists only when that is not available, for example, when _only_
  /// syntactic check is done on a piece of code.
  ///
  /// Also, StdLibraryFunctionsChecker::Signature is likely a better candicade
  /// for syntactic only matching if you are writing a new checker. This is
  /// handy if a CallDescriptionMap is already there.
  ///
  /// The function is imprecise because CallEvent may know path sensitive
  /// information, such as the precise argument count (see comments for
  /// CallEvent::getNumArgs), the called function if it was called through a
  /// function pointer, and other information not available syntactically.
  [[nodiscard]] bool containsAsWritten(const CallExpr &CE) const;
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CALLDESCRIPTION_H
