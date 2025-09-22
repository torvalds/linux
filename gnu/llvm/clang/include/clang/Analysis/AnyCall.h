//=== AnyCall.h - Abstraction over different callables --------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A utility class for performing generic operations over different callables.
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_CLANG_ANALYSIS_ANYCALL_H
#define LLVM_CLANG_ANALYSIS_ANYCALL_H

#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include <optional>

namespace clang {

/// An instance of this class corresponds to a call.
/// It might be a syntactically-concrete call, done as a part of evaluating an
/// expression, or it may be an abstract callee with no associated expression.
class AnyCall {
public:
  enum Kind {
    /// A function, function pointer, or a C++ method call
    Function,

    /// A call to an Objective-C method
    ObjCMethod,

    /// A call to an Objective-C block
    Block,

    /// An implicit C++ destructor call (called implicitly
    /// or by operator 'delete')
    Destructor,

    /// An implicit or explicit C++ constructor call
    Constructor,

    /// A C++ inherited constructor produced by a "using T::T" directive
    InheritedConstructor,

    /// A C++ allocation function call (operator `new`), via C++ new-expression
    Allocator,

    /// A C++ deallocation function call (operator `delete`), via C++
    /// delete-expression
    Deallocator
  };

private:
  /// Either expression or declaration (but not both at the same time)
  /// can be null.

  /// Call expression, is null when is not known (then declaration is non-null),
  /// or for implicit destructor calls (when no expression exists.)
  const Expr *E = nullptr;

  /// Corresponds to a statically known declaration of the called function,
  /// or null if it is not known (e.g. for a function pointer).
  const Decl *D = nullptr;
  Kind K;

public:
  AnyCall(const CallExpr *CE) : E(CE) {
    D = CE->getCalleeDecl();
    K = (CE->getCallee()->getType()->getAs<BlockPointerType>()) ? Block
                                                                : Function;
    if (D && ((K == Function && !isa<FunctionDecl>(D)) ||
              (K == Block && !isa<BlockDecl>(D))))
      D = nullptr;
  }

  AnyCall(const ObjCMessageExpr *ME)
      : E(ME), D(ME->getMethodDecl()), K(ObjCMethod) {}

  AnyCall(const CXXNewExpr *NE)
      : E(NE), D(NE->getOperatorNew()), K(Allocator) {}

  AnyCall(const CXXDeleteExpr *NE)
      : E(NE), D(NE->getOperatorDelete()), K(Deallocator) {}

  AnyCall(const CXXConstructExpr *NE)
      : E(NE), D(NE->getConstructor()), K(Constructor) {}

  AnyCall(const CXXInheritedCtorInitExpr *CIE)
      : E(CIE), D(CIE->getConstructor()), K(InheritedConstructor) {}

  AnyCall(const CXXDestructorDecl *D) : E(nullptr), D(D), K(Destructor) {}

  AnyCall(const CXXConstructorDecl *D) : E(nullptr), D(D), K(Constructor) {}

  AnyCall(const ObjCMethodDecl *D) : E(nullptr), D(D), K(ObjCMethod) {}

  AnyCall(const FunctionDecl *D) : E(nullptr), D(D) {
    if (isa<CXXConstructorDecl>(D)) {
      K = Constructor;
    } else if (isa <CXXDestructorDecl>(D)) {
      K = Destructor;
    } else {
      K = Function;
    }

  }

  /// If @c E is a generic call (to ObjC method /function/block/etc),
  /// return a constructed @c AnyCall object. Return std::nullopt otherwise.
  static std::optional<AnyCall> forExpr(const Expr *E) {
    if (const auto *ME = dyn_cast<ObjCMessageExpr>(E)) {
      return AnyCall(ME);
    } else if (const auto *CE = dyn_cast<CallExpr>(E)) {
      return AnyCall(CE);
    } else if (const auto *CXNE = dyn_cast<CXXNewExpr>(E)) {
      return AnyCall(CXNE);
    } else if (const auto *CXDE = dyn_cast<CXXDeleteExpr>(E)) {
      return AnyCall(CXDE);
    } else if (const auto *CXCE = dyn_cast<CXXConstructExpr>(E)) {
      return AnyCall(CXCE);
    } else if (const auto *CXCIE = dyn_cast<CXXInheritedCtorInitExpr>(E)) {
      return AnyCall(CXCIE);
    } else {
      return std::nullopt;
    }
  }

  /// If @c D is a callable (Objective-C method or a function), return
  /// a constructed @c AnyCall object. Return std::nullopt otherwise.
  // FIXME: block support.
  static std::optional<AnyCall> forDecl(const Decl *D) {
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      return AnyCall(FD);
    } else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
      return AnyCall(MD);
    }
    return std::nullopt;
  }

  /// \returns formal parameters for direct calls (including virtual calls)
  ArrayRef<ParmVarDecl *> parameters() const {
    if (!D)
      return std::nullopt;

    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      return FD->parameters();
    } else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
      return MD->parameters();
    } else if (const auto *BD = dyn_cast<BlockDecl>(D)) {
      return BD->parameters();
    } else {
      return std::nullopt;
    }
  }

  using param_const_iterator = ArrayRef<ParmVarDecl *>::const_iterator;
  param_const_iterator param_begin() const { return parameters().begin(); }
  param_const_iterator param_end() const { return parameters().end(); }
  size_t param_size() const { return parameters().size(); }
  bool param_empty() const { return parameters().empty(); }

  QualType getReturnType(ASTContext &Ctx) const {
    switch (K) {
    case Function:
      if (E)
        return cast<CallExpr>(E)->getCallReturnType(Ctx);
      return cast<FunctionDecl>(D)->getReturnType();
    case ObjCMethod:
      if (E)
        return cast<ObjCMessageExpr>(E)->getCallReturnType(Ctx);
      return cast<ObjCMethodDecl>(D)->getReturnType();
    case Block:
      // FIXME: BlockDecl does not know its return type,
      // hence the asymmetry with the function and method cases above.
      return cast<CallExpr>(E)->getCallReturnType(Ctx);
    case Destructor:
    case Constructor:
    case InheritedConstructor:
    case Allocator:
    case Deallocator:
      return cast<FunctionDecl>(D)->getReturnType();
    }
    llvm_unreachable("Unknown AnyCall::Kind");
  }

  /// \returns Function identifier if it is a named declaration,
  /// @c nullptr otherwise.
  const IdentifierInfo *getIdentifier() const {
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(D))
      return ND->getIdentifier();
    return nullptr;
  }

  const Decl *getDecl() const {
    return D;
  }

  const Expr *getExpr() const {
    return E;
  }

  Kind getKind() const {
    return K;
  }

  void dump() const {
    if (E)
      E->dump();
    if (D)
      D->dump();
  }
};

}

#endif // LLVM_CLANG_ANALYSIS_ANYCALL_H
