//===----- Attr.h --- Helper functions for attribute handling in Sema -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file provides helpers for Sema functions that handle attributes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_ATTR_H
#define LLVM_CLANG_SEMA_ATTR_H

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/Support/Casting.h"

namespace clang {

/// isFuncOrMethodForAttrSubject - Return true if the given decl has function
/// type (function or function-typed variable) or an Objective-C
/// method.
inline bool isFuncOrMethodForAttrSubject(const Decl *D) {
  return (D->getFunctionType() != nullptr) || llvm::isa<ObjCMethodDecl>(D);
}

/// Return true if the given decl has function type (function or
/// function-typed variable) or an Objective-C method or a block.
inline bool isFunctionOrMethodOrBlockForAttrSubject(const Decl *D) {
  return isFuncOrMethodForAttrSubject(D) || llvm::isa<BlockDecl>(D);
}

/// Return true if the given decl has a declarator that should have
/// been processed by Sema::GetTypeForDeclarator.
inline bool hasDeclarator(const Decl *D) {
  // In some sense, TypedefDecl really *ought* to be a DeclaratorDecl.
  return isa<DeclaratorDecl>(D) || isa<BlockDecl>(D) ||
         isa<TypedefNameDecl>(D) || isa<ObjCPropertyDecl>(D);
}

/// hasFunctionProto - Return true if the given decl has a argument
/// information. This decl should have already passed
/// isFuncOrMethodForAttrSubject or isFunctionOrMethodOrBlockForAttrSubject.
inline bool hasFunctionProto(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return isa<FunctionProtoType>(FnTy);
  return isa<ObjCMethodDecl>(D) || isa<BlockDecl>(D);
}

/// getFunctionOrMethodNumParams - Return number of function or method
/// parameters. It is an error to call this on a K&R function (use
/// hasFunctionProto first).
inline unsigned getFunctionOrMethodNumParams(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->getNumParams();
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->getNumParams();
  return cast<ObjCMethodDecl>(D)->param_size();
}

inline const ParmVarDecl *getFunctionOrMethodParam(const Decl *D,
                                                   unsigned Idx) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getParamDecl(Idx);
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->getParamDecl(Idx);
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->getParamDecl(Idx);
  return nullptr;
}

inline QualType getFunctionOrMethodParamType(const Decl *D, unsigned Idx) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->getParamType(Idx);
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->getParamDecl(Idx)->getType();

  return cast<ObjCMethodDecl>(D)->parameters()[Idx]->getType();
}

inline SourceRange getFunctionOrMethodParamRange(const Decl *D, unsigned Idx) {
  if (auto *PVD = getFunctionOrMethodParam(D, Idx))
    return PVD->getSourceRange();
  return SourceRange();
}

inline QualType getFunctionOrMethodResultType(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return FnTy->getReturnType();
  return cast<ObjCMethodDecl>(D)->getReturnType();
}

inline SourceRange getFunctionOrMethodResultSourceRange(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getReturnTypeSourceRange();
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->getReturnTypeSourceRange();
  return SourceRange();
}

inline bool isFunctionOrMethodVariadic(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->isVariadic();
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->isVariadic();
  return cast<ObjCMethodDecl>(D)->isVariadic();
}

inline bool isInstanceMethod(const Decl *D) {
  if (const auto *MethodDecl = dyn_cast<CXXMethodDecl>(D))
    return MethodDecl->isInstance();
  return false;
}

/// Diagnose mutually exclusive attributes when present on a given
/// declaration. Returns true if diagnosed.
template <typename AttrTy>
bool checkAttrMutualExclusion(SemaBase &S, Decl *D, const ParsedAttr &AL) {
  if (const auto *A = D->getAttr<AttrTy>()) {
    S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
        << AL << A
        << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
    S.Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

template <typename AttrTy>
bool checkAttrMutualExclusion(SemaBase &S, Decl *D, const Attr &AL) {
  if (const auto *A = D->getAttr<AttrTy>()) {
    S.Diag(AL.getLocation(), diag::err_attributes_are_not_compatible)
        << &AL << A
        << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
    Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

template <typename... DiagnosticArgs>
const SemaBase::SemaDiagnosticBuilder &
appendDiagnostics(const SemaBase::SemaDiagnosticBuilder &Bldr) {
  return Bldr;
}

template <typename T, typename... DiagnosticArgs>
const SemaBase::SemaDiagnosticBuilder &
appendDiagnostics(const SemaBase::SemaDiagnosticBuilder &Bldr, T &&ExtraArg,
                  DiagnosticArgs &&...ExtraArgs) {
  return appendDiagnostics(Bldr << std::forward<T>(ExtraArg),
                           std::forward<DiagnosticArgs>(ExtraArgs)...);
}

/// Applies the given attribute to the Decl without performing any
/// additional semantic checking.
template <typename AttrType>
void handleSimpleAttribute(SemaBase &S, Decl *D,
                           const AttributeCommonInfo &CI) {
  D->addAttr(::new (S.getASTContext()) AttrType(S.getASTContext(), CI));
}

/// Add an attribute @c AttrType to declaration @c D, provided that
/// @c PassesCheck is true.
/// Otherwise, emit diagnostic @c DiagID, passing in all parameters
/// specified in @c ExtraArgs.
template <typename AttrType, typename... DiagnosticArgs>
void handleSimpleAttributeOrDiagnose(SemaBase &S, Decl *D,
                                     const AttributeCommonInfo &CI,
                                     bool PassesCheck, unsigned DiagID,
                                     DiagnosticArgs &&...ExtraArgs) {
  if (!PassesCheck) {
    SemaBase::SemaDiagnosticBuilder DB = S.Diag(D->getBeginLoc(), DiagID);
    appendDiagnostics(DB, std::forward<DiagnosticArgs>(ExtraArgs)...);
    return;
  }
  handleSimpleAttribute<AttrType>(S, D, CI);
}

} // namespace clang
#endif // LLVM_CLANG_SEMA_ATTR_H
