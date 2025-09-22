//===--- SemaBase.h - Common utilities for semantic analysis-----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SemaBase class, which provides utilities for Sema
// and its parts like SemaOpenACC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMABASE_H
#define LLVM_CLANG_SEMA_SEMABASE_H

#include "clang/AST/Decl.h"
#include "clang/AST/Redeclarable.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/DenseMap.h"
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace clang {

class ASTContext;
class DiagnosticsEngine;
class LangOptions;
class Sema;

class SemaBase {
public:
  SemaBase(Sema &S);

  Sema &SemaRef;

  ASTContext &getASTContext() const;
  DiagnosticsEngine &getDiagnostics() const;
  const LangOptions &getLangOpts() const;

  /// Helper class that creates diagnostics with optional
  /// template instantiation stacks.
  ///
  /// This class provides a wrapper around the basic DiagnosticBuilder
  /// class that emits diagnostics. ImmediateDiagBuilder is
  /// responsible for emitting the diagnostic (as DiagnosticBuilder
  /// does) and, if the diagnostic comes from inside a template
  /// instantiation, printing the template instantiation stack as
  /// well.
  class ImmediateDiagBuilder : public DiagnosticBuilder {
    Sema &SemaRef;
    unsigned DiagID;

  public:
    ImmediateDiagBuilder(DiagnosticBuilder &DB, Sema &SemaRef, unsigned DiagID)
        : DiagnosticBuilder(DB), SemaRef(SemaRef), DiagID(DiagID) {}
    ImmediateDiagBuilder(DiagnosticBuilder &&DB, Sema &SemaRef, unsigned DiagID)
        : DiagnosticBuilder(DB), SemaRef(SemaRef), DiagID(DiagID) {}

    // This is a cunning lie. DiagnosticBuilder actually performs move
    // construction in its copy constructor (but due to varied uses, it's not
    // possible to conveniently express this as actual move construction). So
    // the default copy ctor here is fine, because the base class disables the
    // source anyway, so the user-defined ~ImmediateDiagBuilder is a safe no-op
    // in that case anwyay.
    ImmediateDiagBuilder(const ImmediateDiagBuilder &) = default;

    ~ImmediateDiagBuilder();

    /// Teach operator<< to produce an object of the correct type.
    template <typename T>
    friend const ImmediateDiagBuilder &
    operator<<(const ImmediateDiagBuilder &Diag, const T &Value) {
      const DiagnosticBuilder &BaseDiag = Diag;
      BaseDiag << Value;
      return Diag;
    }

    // It is necessary to limit this to rvalue reference to avoid calling this
    // function with a bitfield lvalue argument since non-const reference to
    // bitfield is not allowed.
    template <typename T,
              typename = std::enable_if_t<!std::is_lvalue_reference<T>::value>>
    const ImmediateDiagBuilder &operator<<(T &&V) const {
      const DiagnosticBuilder &BaseDiag = *this;
      BaseDiag << std::move(V);
      return *this;
    }
  };

  /// A generic diagnostic builder for errors which may or may not be deferred.
  ///
  /// In CUDA, there exist constructs (e.g. variable-length arrays, try/catch)
  /// which are not allowed to appear inside __device__ functions and are
  /// allowed to appear in __host__ __device__ functions only if the host+device
  /// function is never codegen'ed.
  ///
  /// To handle this, we use the notion of "deferred diagnostics", where we
  /// attach a diagnostic to a FunctionDecl that's emitted iff it's codegen'ed.
  ///
  /// This class lets you emit either a regular diagnostic, a deferred
  /// diagnostic, or no diagnostic at all, according to an argument you pass to
  /// its constructor, thus simplifying the process of creating these "maybe
  /// deferred" diagnostics.
  class SemaDiagnosticBuilder {
  public:
    enum Kind {
      /// Emit no diagnostics.
      K_Nop,
      /// Emit the diagnostic immediately (i.e., behave like Sema::Diag()).
      K_Immediate,
      /// Emit the diagnostic immediately, and, if it's a warning or error, also
      /// emit a call stack showing how this function can be reached by an a
      /// priori known-emitted function.
      K_ImmediateWithCallStack,
      /// Create a deferred diagnostic, which is emitted only if the function
      /// it's attached to is codegen'ed.  Also emit a call stack as with
      /// K_ImmediateWithCallStack.
      K_Deferred
    };

    SemaDiagnosticBuilder(Kind K, SourceLocation Loc, unsigned DiagID,
                          const FunctionDecl *Fn, Sema &S);
    SemaDiagnosticBuilder(SemaDiagnosticBuilder &&D);
    SemaDiagnosticBuilder(const SemaDiagnosticBuilder &) = default;

    // The copy and move assignment operator is defined as deleted pending
    // further motivation.
    SemaDiagnosticBuilder &operator=(const SemaDiagnosticBuilder &) = delete;
    SemaDiagnosticBuilder &operator=(SemaDiagnosticBuilder &&) = delete;

    ~SemaDiagnosticBuilder();

    bool isImmediate() const { return ImmediateDiag.has_value(); }

    /// Convertible to bool: True if we immediately emitted an error, false if
    /// we didn't emit an error or we created a deferred error.
    ///
    /// Example usage:
    ///
    ///   if (SemaDiagnosticBuilder(...) << foo << bar)
    ///     return ExprError();
    ///
    /// But see DiagIfDeviceCode() and DiagIfHostCode() -- you probably
    /// want to use these instead of creating a SemaDiagnosticBuilder yourself.
    operator bool() const { return isImmediate(); }

    template <typename T>
    friend const SemaDiagnosticBuilder &
    operator<<(const SemaDiagnosticBuilder &Diag, const T &Value) {
      if (Diag.ImmediateDiag)
        *Diag.ImmediateDiag << Value;
      else if (Diag.PartialDiagId)
        Diag.getDeviceDeferredDiags()[Diag.Fn][*Diag.PartialDiagId].second
            << Value;
      return Diag;
    }

    // It is necessary to limit this to rvalue reference to avoid calling this
    // function with a bitfield lvalue argument since non-const reference to
    // bitfield is not allowed.
    template <typename T,
              typename = std::enable_if_t<!std::is_lvalue_reference<T>::value>>
    const SemaDiagnosticBuilder &operator<<(T &&V) const {
      if (ImmediateDiag)
        *ImmediateDiag << std::move(V);
      else if (PartialDiagId)
        getDeviceDeferredDiags()[Fn][*PartialDiagId].second << std::move(V);
      return *this;
    }

    friend const SemaDiagnosticBuilder &
    operator<<(const SemaDiagnosticBuilder &Diag, const PartialDiagnostic &PD);

    void AddFixItHint(const FixItHint &Hint) const;

    friend ExprResult ExprError(const SemaDiagnosticBuilder &) {
      return ExprError();
    }
    friend StmtResult StmtError(const SemaDiagnosticBuilder &) {
      return StmtError();
    }
    operator ExprResult() const { return ExprError(); }
    operator StmtResult() const { return StmtError(); }
    operator TypeResult() const { return TypeError(); }
    operator DeclResult() const { return DeclResult(true); }
    operator MemInitResult() const { return MemInitResult(true); }

    using DeferredDiagnosticsType =
        llvm::DenseMap<CanonicalDeclPtr<const FunctionDecl>,
                       std::vector<PartialDiagnosticAt>>;

  private:
    Sema &S;
    SourceLocation Loc;
    unsigned DiagID;
    const FunctionDecl *Fn;
    bool ShowCallStack;

    // Invariant: At most one of these Optionals has a value.
    // FIXME: Switch these to a Variant once that exists.
    std::optional<ImmediateDiagBuilder> ImmediateDiag;
    std::optional<unsigned> PartialDiagId;

    DeferredDiagnosticsType &getDeviceDeferredDiags() const;
  };

  /// Emit a diagnostic.
  SemaDiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID,
                             bool DeferHint = false);

  /// Emit a partial diagnostic.
  SemaDiagnosticBuilder Diag(SourceLocation Loc, const PartialDiagnostic &PD,
                             bool DeferHint = false);

  /// Build a partial diagnostic.
  PartialDiagnostic PDiag(unsigned DiagID = 0);
};

} // namespace clang

#endif
