//===- ExprObjC.cpp - (ObjC) Expression AST Node Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclesses of Expr class declared in ExprObjC.h
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ExprObjC.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ComputeDependence.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/SelectorLocationsKind.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace clang;

ObjCArrayLiteral::ObjCArrayLiteral(ArrayRef<Expr *> Elements, QualType T,
                                   ObjCMethodDecl *Method, SourceRange SR)
    : Expr(ObjCArrayLiteralClass, T, VK_PRValue, OK_Ordinary),
      NumElements(Elements.size()), Range(SR), ArrayWithObjectsMethod(Method) {
  Expr **SaveElements = getElements();
  for (unsigned I = 0, N = Elements.size(); I != N; ++I)
    SaveElements[I] = Elements[I];

  setDependence(computeDependence(this));
}

ObjCArrayLiteral *ObjCArrayLiteral::Create(const ASTContext &C,
                                           ArrayRef<Expr *> Elements,
                                           QualType T, ObjCMethodDecl *Method,
                                           SourceRange SR) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(Elements.size()));
  return new (Mem) ObjCArrayLiteral(Elements, T, Method, SR);
}

ObjCArrayLiteral *ObjCArrayLiteral::CreateEmpty(const ASTContext &C,
                                                unsigned NumElements) {
  void *Mem = C.Allocate(totalSizeToAlloc<Expr *>(NumElements));
  return new (Mem) ObjCArrayLiteral(EmptyShell(), NumElements);
}

ObjCDictionaryLiteral::ObjCDictionaryLiteral(ArrayRef<ObjCDictionaryElement> VK,
                                             bool HasPackExpansions, QualType T,
                                             ObjCMethodDecl *method,
                                             SourceRange SR)
    : Expr(ObjCDictionaryLiteralClass, T, VK_PRValue, OK_Ordinary),
      NumElements(VK.size()), HasPackExpansions(HasPackExpansions), Range(SR),
      DictWithObjectsMethod(method) {
  KeyValuePair *KeyValues = getTrailingObjects<KeyValuePair>();
  ExpansionData *Expansions =
      HasPackExpansions ? getTrailingObjects<ExpansionData>() : nullptr;
  for (unsigned I = 0; I < NumElements; I++) {
    KeyValues[I].Key = VK[I].Key;
    KeyValues[I].Value = VK[I].Value;
    if (Expansions) {
      Expansions[I].EllipsisLoc = VK[I].EllipsisLoc;
      if (VK[I].NumExpansions)
        Expansions[I].NumExpansionsPlusOne = *VK[I].NumExpansions + 1;
      else
        Expansions[I].NumExpansionsPlusOne = 0;
    }
  }
  setDependence(computeDependence(this));
}

ObjCDictionaryLiteral *
ObjCDictionaryLiteral::Create(const ASTContext &C,
                              ArrayRef<ObjCDictionaryElement> VK,
                              bool HasPackExpansions, QualType T,
                              ObjCMethodDecl *method, SourceRange SR) {
  void *Mem = C.Allocate(totalSizeToAlloc<KeyValuePair, ExpansionData>(
      VK.size(), HasPackExpansions ? VK.size() : 0));
  return new (Mem) ObjCDictionaryLiteral(VK, HasPackExpansions, T, method, SR);
}

ObjCDictionaryLiteral *
ObjCDictionaryLiteral::CreateEmpty(const ASTContext &C, unsigned NumElements,
                                   bool HasPackExpansions) {
  void *Mem = C.Allocate(totalSizeToAlloc<KeyValuePair, ExpansionData>(
      NumElements, HasPackExpansions ? NumElements : 0));
  return new (Mem)
      ObjCDictionaryLiteral(EmptyShell(), NumElements, HasPackExpansions);
}

QualType ObjCPropertyRefExpr::getReceiverType(const ASTContext &ctx) const {
  if (isClassReceiver())
    return ctx.getObjCInterfaceType(getClassReceiver());

  if (isSuperReceiver())
    return getSuperReceiverType();

  return getBase()->getType();
}

ObjCMessageExpr::ObjCMessageExpr(QualType T, ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 SourceLocation SuperLoc, bool IsInstanceSuper,
                                 QualType SuperType, Selector Sel,
                                 ArrayRef<SourceLocation> SelLocs,
                                 SelectorLocationsKind SelLocsK,
                                 ObjCMethodDecl *Method, ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc, bool isImplicit)
    : Expr(ObjCMessageExprClass, T, VK, OK_Ordinary),
      SelectorOrMethod(
          reinterpret_cast<uintptr_t>(Method ? Method : Sel.getAsOpaquePtr())),
      Kind(IsInstanceSuper ? SuperInstance : SuperClass),
      HasMethod(Method != nullptr), IsDelegateInitCall(false),
      IsImplicit(isImplicit), SuperLoc(SuperLoc), LBracLoc(LBracLoc),
      RBracLoc(RBracLoc) {
  initArgsAndSelLocs(Args, SelLocs, SelLocsK);
  setReceiverPointer(SuperType.getAsOpaquePtr());
  setDependence(computeDependence(this));
}

ObjCMessageExpr::ObjCMessageExpr(QualType T, ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 TypeSourceInfo *Receiver, Selector Sel,
                                 ArrayRef<SourceLocation> SelLocs,
                                 SelectorLocationsKind SelLocsK,
                                 ObjCMethodDecl *Method, ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc, bool isImplicit)
    : Expr(ObjCMessageExprClass, T, VK, OK_Ordinary),
      SelectorOrMethod(
          reinterpret_cast<uintptr_t>(Method ? Method : Sel.getAsOpaquePtr())),
      Kind(Class), HasMethod(Method != nullptr), IsDelegateInitCall(false),
      IsImplicit(isImplicit), LBracLoc(LBracLoc), RBracLoc(RBracLoc) {
  initArgsAndSelLocs(Args, SelLocs, SelLocsK);
  setReceiverPointer(Receiver);
  setDependence(computeDependence(this));
}

ObjCMessageExpr::ObjCMessageExpr(QualType T, ExprValueKind VK,
                                 SourceLocation LBracLoc, Expr *Receiver,
                                 Selector Sel, ArrayRef<SourceLocation> SelLocs,
                                 SelectorLocationsKind SelLocsK,
                                 ObjCMethodDecl *Method, ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc, bool isImplicit)
    : Expr(ObjCMessageExprClass, T, VK, OK_Ordinary),
      SelectorOrMethod(
          reinterpret_cast<uintptr_t>(Method ? Method : Sel.getAsOpaquePtr())),
      Kind(Instance), HasMethod(Method != nullptr), IsDelegateInitCall(false),
      IsImplicit(isImplicit), LBracLoc(LBracLoc), RBracLoc(RBracLoc) {
  initArgsAndSelLocs(Args, SelLocs, SelLocsK);
  setReceiverPointer(Receiver);
  setDependence(computeDependence(this));
}

void ObjCMessageExpr::initArgsAndSelLocs(ArrayRef<Expr *> Args,
                                         ArrayRef<SourceLocation> SelLocs,
                                         SelectorLocationsKind SelLocsK) {
  setNumArgs(Args.size());
  Expr **MyArgs = getArgs();
  for (unsigned I = 0; I != Args.size(); ++I)
    MyArgs[I] = Args[I];

  SelLocsKind = SelLocsK;
  if (!isImplicit()) {
    if (SelLocsK == SelLoc_NonStandard)
      std::copy(SelLocs.begin(), SelLocs.end(), getStoredSelLocs());
  }
}

ObjCMessageExpr *
ObjCMessageExpr::Create(const ASTContext &Context, QualType T, ExprValueKind VK,
                        SourceLocation LBracLoc, SourceLocation SuperLoc,
                        bool IsInstanceSuper, QualType SuperType, Selector Sel,
                        ArrayRef<SourceLocation> SelLocs,
                        ObjCMethodDecl *Method, ArrayRef<Expr *> Args,
                        SourceLocation RBracLoc, bool isImplicit) {
  assert((!SelLocs.empty() || isImplicit) &&
         "No selector locs for non-implicit message");
  ObjCMessageExpr *Mem;
  SelectorLocationsKind SelLocsK = SelectorLocationsKind();
  if (isImplicit)
    Mem = alloc(Context, Args.size(), 0);
  else
    Mem = alloc(Context, Args, RBracLoc, SelLocs, Sel, SelLocsK);
  return new (Mem) ObjCMessageExpr(T, VK, LBracLoc, SuperLoc, IsInstanceSuper,
                                   SuperType, Sel, SelLocs, SelLocsK, Method,
                                   Args, RBracLoc, isImplicit);
}

ObjCMessageExpr *
ObjCMessageExpr::Create(const ASTContext &Context, QualType T, ExprValueKind VK,
                        SourceLocation LBracLoc, TypeSourceInfo *Receiver,
                        Selector Sel, ArrayRef<SourceLocation> SelLocs,
                        ObjCMethodDecl *Method, ArrayRef<Expr *> Args,
                        SourceLocation RBracLoc, bool isImplicit) {
  assert((!SelLocs.empty() || isImplicit) &&
         "No selector locs for non-implicit message");
  ObjCMessageExpr *Mem;
  SelectorLocationsKind SelLocsK = SelectorLocationsKind();
  if (isImplicit)
    Mem = alloc(Context, Args.size(), 0);
  else
    Mem = alloc(Context, Args, RBracLoc, SelLocs, Sel, SelLocsK);
  return new (Mem)
      ObjCMessageExpr(T, VK, LBracLoc, Receiver, Sel, SelLocs, SelLocsK, Method,
                      Args, RBracLoc, isImplicit);
}

ObjCMessageExpr *
ObjCMessageExpr::Create(const ASTContext &Context, QualType T, ExprValueKind VK,
                        SourceLocation LBracLoc, Expr *Receiver, Selector Sel,
                        ArrayRef<SourceLocation> SelLocs,
                        ObjCMethodDecl *Method, ArrayRef<Expr *> Args,
                        SourceLocation RBracLoc, bool isImplicit) {
  assert((!SelLocs.empty() || isImplicit) &&
         "No selector locs for non-implicit message");
  ObjCMessageExpr *Mem;
  SelectorLocationsKind SelLocsK = SelectorLocationsKind();
  if (isImplicit)
    Mem = alloc(Context, Args.size(), 0);
  else
    Mem = alloc(Context, Args, RBracLoc, SelLocs, Sel, SelLocsK);
  return new (Mem)
      ObjCMessageExpr(T, VK, LBracLoc, Receiver, Sel, SelLocs, SelLocsK, Method,
                      Args, RBracLoc, isImplicit);
}

ObjCMessageExpr *ObjCMessageExpr::CreateEmpty(const ASTContext &Context,
                                              unsigned NumArgs,
                                              unsigned NumStoredSelLocs) {
  ObjCMessageExpr *Mem = alloc(Context, NumArgs, NumStoredSelLocs);
  return new (Mem) ObjCMessageExpr(EmptyShell(), NumArgs);
}

ObjCMessageExpr *ObjCMessageExpr::alloc(const ASTContext &C,
                                        ArrayRef<Expr *> Args,
                                        SourceLocation RBraceLoc,
                                        ArrayRef<SourceLocation> SelLocs,
                                        Selector Sel,
                                        SelectorLocationsKind &SelLocsK) {
  SelLocsK = hasStandardSelectorLocs(Sel, SelLocs, Args, RBraceLoc);
  unsigned NumStoredSelLocs =
      (SelLocsK == SelLoc_NonStandard) ? SelLocs.size() : 0;
  return alloc(C, Args.size(), NumStoredSelLocs);
}

ObjCMessageExpr *ObjCMessageExpr::alloc(const ASTContext &C, unsigned NumArgs,
                                        unsigned NumStoredSelLocs) {
  return (ObjCMessageExpr *)C.Allocate(
      totalSizeToAlloc<void *, SourceLocation>(NumArgs + 1, NumStoredSelLocs),
      alignof(ObjCMessageExpr));
}

void ObjCMessageExpr::getSelectorLocs(
    SmallVectorImpl<SourceLocation> &SelLocs) const {
  for (unsigned i = 0, e = getNumSelectorLocs(); i != e; ++i)
    SelLocs.push_back(getSelectorLoc(i));
}


QualType ObjCMessageExpr::getCallReturnType(ASTContext &Ctx) const {
  if (const ObjCMethodDecl *MD = getMethodDecl()) {
    QualType QT = MD->getReturnType();
    if (QT == Ctx.getObjCInstanceType()) {
      // instancetype corresponds to expression types.
      return getType();
    }
    return QT;
  }
  return Ctx.getReferenceQualifiedType(this);
}

SourceRange ObjCMessageExpr::getReceiverRange() const {
  switch (getReceiverKind()) {
  case Instance:
    return getInstanceReceiver()->getSourceRange();

  case Class:
    return getClassReceiverTypeInfo()->getTypeLoc().getSourceRange();

  case SuperInstance:
  case SuperClass:
    return getSuperLoc();
  }

  llvm_unreachable("Invalid ReceiverKind!");
}

Selector ObjCMessageExpr::getSelector() const {
  if (HasMethod)
    return reinterpret_cast<const ObjCMethodDecl *>(SelectorOrMethod)
        ->getSelector();
  return Selector(SelectorOrMethod);
}

QualType ObjCMessageExpr::getReceiverType() const {
  switch (getReceiverKind()) {
  case Instance:
    return getInstanceReceiver()->getType();
  case Class:
    return getClassReceiver();
  case SuperInstance:
  case SuperClass:
    return getSuperType();
  }

  llvm_unreachable("unexpected receiver kind");
}

ObjCInterfaceDecl *ObjCMessageExpr::getReceiverInterface() const {
  QualType T = getReceiverType();

  if (const ObjCObjectPointerType *Ptr = T->getAs<ObjCObjectPointerType>())
    return Ptr->getInterfaceDecl();

  if (const ObjCObjectType *Ty = T->getAs<ObjCObjectType>())
    return Ty->getInterface();

  return nullptr;
}

Stmt::child_range ObjCMessageExpr::children() {
  Stmt **begin;
  if (getReceiverKind() == Instance)
    begin = reinterpret_cast<Stmt **>(getTrailingObjects<void *>());
  else
    begin = reinterpret_cast<Stmt **>(getArgs());
  return child_range(begin,
                     reinterpret_cast<Stmt **>(getArgs() + getNumArgs()));
}

Stmt::const_child_range ObjCMessageExpr::children() const {
  auto Children = const_cast<ObjCMessageExpr *>(this)->children();
  return const_child_range(Children.begin(), Children.end());
}

StringRef ObjCBridgedCastExpr::getBridgeKindName() const {
  switch (getBridgeKind()) {
  case OBC_Bridge:
    return "__bridge";
  case OBC_BridgeTransfer:
    return "__bridge_transfer";
  case OBC_BridgeRetained:
    return "__bridge_retained";
  }

  llvm_unreachable("Invalid BridgeKind!");
}
