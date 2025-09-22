//===- DeclFriend.cpp - C++ Friend Declaration AST Node Implementation ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AST classes related to C++ friend
// declarations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclFriend.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstddef>

using namespace clang;

void FriendDecl::anchor() {}

FriendDecl *FriendDecl::getNextFriendSlowCase() {
  return cast_or_null<FriendDecl>(
                           NextFriend.get(getASTContext().getExternalSource()));
}

FriendDecl *FriendDecl::Create(ASTContext &C, DeclContext *DC,
                               SourceLocation L,
                               FriendUnion Friend,
                               SourceLocation FriendL,
                          ArrayRef<TemplateParameterList *> FriendTypeTPLists) {
#ifndef NDEBUG
  if (Friend.is<NamedDecl *>()) {
    const auto *D = Friend.get<NamedDecl*>();
    assert(isa<FunctionDecl>(D) ||
           isa<CXXRecordDecl>(D) ||
           isa<FunctionTemplateDecl>(D) ||
           isa<ClassTemplateDecl>(D));

    // As a temporary hack, we permit template instantiation to point
    // to the original declaration when instantiating members.
    assert(D->getFriendObjectKind() ||
           (cast<CXXRecordDecl>(DC)->getTemplateSpecializationKind()));
    // These template parameters are for friend types only.
    assert(FriendTypeTPLists.empty());
  }
#endif

  std::size_t Extra =
      FriendDecl::additionalSizeToAlloc<TemplateParameterList *>(
          FriendTypeTPLists.size());
  auto *FD = new (C, DC, Extra) FriendDecl(DC, L, Friend, FriendL,
                                           FriendTypeTPLists);
  cast<CXXRecordDecl>(DC)->pushFriendDecl(FD);
  return FD;
}

FriendDecl *FriendDecl::CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                           unsigned FriendTypeNumTPLists) {
  std::size_t Extra =
      additionalSizeToAlloc<TemplateParameterList *>(FriendTypeNumTPLists);
  return new (C, ID, Extra) FriendDecl(EmptyShell(), FriendTypeNumTPLists);
}

FriendDecl *CXXRecordDecl::getFirstFriend() const {
  ExternalASTSource *Source = getParentASTContext().getExternalSource();
  Decl *First = data().FirstFriend.get(Source);
  return First ? cast<FriendDecl>(First) : nullptr;
}
