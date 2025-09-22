//===--- InterpreterUtils.cpp - Incremental Utils --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements some common utils used in the incremental library.
//
//===----------------------------------------------------------------------===//

#include "InterpreterUtils.h"

namespace clang {

IntegerLiteral *IntegerLiteralExpr(ASTContext &C, uint64_t Val) {
  return IntegerLiteral::Create(C, llvm::APSInt::getUnsigned(Val),
                                C.UnsignedLongLongTy, SourceLocation());
}

Expr *CStyleCastPtrExpr(Sema &S, QualType Ty, Expr *E) {
  ASTContext &Ctx = S.getASTContext();
  if (!Ty->isPointerType())
    Ty = Ctx.getPointerType(Ty);

  TypeSourceInfo *TSI = Ctx.getTrivialTypeSourceInfo(Ty, SourceLocation());
  Expr *Result =
      S.BuildCStyleCastExpr(SourceLocation(), TSI, SourceLocation(), E).get();
  assert(Result && "Cannot create CStyleCastPtrExpr");
  return Result;
}

Expr *CStyleCastPtrExpr(Sema &S, QualType Ty, uintptr_t Ptr) {
  ASTContext &Ctx = S.getASTContext();
  return CStyleCastPtrExpr(S, Ty, IntegerLiteralExpr(Ctx, (uint64_t)Ptr));
}

Sema::DeclGroupPtrTy CreateDGPtrFrom(Sema &S, Decl *D) {
  SmallVector<Decl *, 1> DeclsInGroup;
  DeclsInGroup.push_back(D);
  Sema::DeclGroupPtrTy DeclGroupPtr = S.BuildDeclaratorGroup(DeclsInGroup);
  return DeclGroupPtr;
}

NamespaceDecl *LookupNamespace(Sema &S, llvm::StringRef Name,
                               const DeclContext *Within) {
  DeclarationName DName = &S.Context.Idents.get(Name);
  LookupResult R(S, DName, SourceLocation(),
                 Sema::LookupNestedNameSpecifierName);
  R.suppressDiagnostics();
  if (!Within)
    S.LookupName(R, S.TUScope);
  else {
    if (const auto *TD = dyn_cast<clang::TagDecl>(Within);
        TD && !TD->getDefinition())
      // No definition, no lookup result.
      return nullptr;

    S.LookupQualifiedName(R, const_cast<DeclContext *>(Within));
  }

  if (R.empty())
    return nullptr;

  R.resolveKind();

  return dyn_cast<NamespaceDecl>(R.getFoundDecl());
}

NamedDecl *LookupNamed(Sema &S, llvm::StringRef Name,
                       const DeclContext *Within) {
  DeclarationName DName = &S.Context.Idents.get(Name);
  LookupResult R(S, DName, SourceLocation(), Sema::LookupOrdinaryName,
                 RedeclarationKind::ForVisibleRedeclaration);

  R.suppressDiagnostics();

  if (!Within)
    S.LookupName(R, S.TUScope);
  else {
    const DeclContext *PrimaryWithin = nullptr;
    if (const auto *TD = dyn_cast<TagDecl>(Within))
      PrimaryWithin = llvm::dyn_cast_or_null<DeclContext>(TD->getDefinition());
    else
      PrimaryWithin = Within->getPrimaryContext();

    // No definition, no lookup result.
    if (!PrimaryWithin)
      return nullptr;

    S.LookupQualifiedName(R, const_cast<DeclContext *>(PrimaryWithin));
  }

  if (R.empty())
    return nullptr;
  R.resolveKind();

  if (R.isSingleResult())
    return llvm::dyn_cast<NamedDecl>(R.getFoundDecl());

  return nullptr;
}

std::string GetFullTypeName(ASTContext &Ctx, QualType QT) {
  PrintingPolicy Policy(Ctx.getPrintingPolicy());
  Policy.SuppressScope = false;
  Policy.AnonymousTagLocations = false;
  return QT.getAsString(Policy);
}
} // namespace clang
