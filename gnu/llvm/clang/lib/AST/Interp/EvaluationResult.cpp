//===----- EvaluationResult.cpp - Result class  for the VM ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EvaluationResult.h"
#include "InterpState.h"
#include "Record.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/SetVector.h"

namespace clang {
namespace interp {

APValue EvaluationResult::toAPValue() const {
  assert(!empty());
  switch (Kind) {
  case LValue:
    // Either a pointer or a function pointer.
    if (const auto *P = std::get_if<Pointer>(&Value))
      return P->toAPValue(Ctx->getASTContext());
    else if (const auto *FP = std::get_if<FunctionPointer>(&Value))
      return FP->toAPValue(Ctx->getASTContext());
    else
      llvm_unreachable("Unhandled LValue type");
    break;
  case RValue:
    return std::get<APValue>(Value);
  case Valid:
    return APValue();
  default:
    llvm_unreachable("Unhandled result kind?");
  }
}

std::optional<APValue> EvaluationResult::toRValue() const {
  if (Kind == RValue)
    return toAPValue();

  assert(Kind == LValue);

  // We have a pointer and want an RValue.
  if (const auto *P = std::get_if<Pointer>(&Value))
    return P->toRValue(*Ctx, getSourceType());
  else if (const auto *FP = std::get_if<FunctionPointer>(&Value)) // Nope
    return FP->toAPValue(Ctx->getASTContext());
  llvm_unreachable("Unhandled lvalue kind");
}

static void DiagnoseUninitializedSubobject(InterpState &S, SourceLocation Loc,
                                           const FieldDecl *SubObjDecl) {
  assert(SubObjDecl && "Subobject declaration does not exist");
  S.FFDiag(Loc, diag::note_constexpr_uninitialized)
      << /*(name)*/ 1 << SubObjDecl;
  S.Note(SubObjDecl->getLocation(),
         diag::note_constexpr_subobject_declared_here);
}

static bool CheckFieldsInitialized(InterpState &S, SourceLocation Loc,
                                   const Pointer &BasePtr, const Record *R);

static bool CheckArrayInitialized(InterpState &S, SourceLocation Loc,
                                  const Pointer &BasePtr,
                                  const ConstantArrayType *CAT) {
  bool Result = true;
  size_t NumElems = CAT->getZExtSize();
  QualType ElemType = CAT->getElementType();

  if (ElemType->isRecordType()) {
    const Record *R = BasePtr.getElemRecord();
    for (size_t I = 0; I != NumElems; ++I) {
      Pointer ElemPtr = BasePtr.atIndex(I).narrow();
      Result &= CheckFieldsInitialized(S, Loc, ElemPtr, R);
    }
  } else if (const auto *ElemCAT = dyn_cast<ConstantArrayType>(ElemType)) {
    for (size_t I = 0; I != NumElems; ++I) {
      Pointer ElemPtr = BasePtr.atIndex(I).narrow();
      Result &= CheckArrayInitialized(S, Loc, ElemPtr, ElemCAT);
    }
  } else {
    for (size_t I = 0; I != NumElems; ++I) {
      if (!BasePtr.atIndex(I).isInitialized()) {
        DiagnoseUninitializedSubobject(S, Loc, BasePtr.getField());
        Result = false;
      }
    }
  }

  return Result;
}

static bool CheckFieldsInitialized(InterpState &S, SourceLocation Loc,
                                   const Pointer &BasePtr, const Record *R) {
  assert(R);
  bool Result = true;
  // Check all fields of this record are initialized.
  for (const Record::Field &F : R->fields()) {
    Pointer FieldPtr = BasePtr.atField(F.Offset);
    QualType FieldType = F.Decl->getType();

    // Don't check inactive union members.
    if (R->isUnion() && !FieldPtr.isActive())
      continue;

    if (FieldType->isRecordType()) {
      Result &= CheckFieldsInitialized(S, Loc, FieldPtr, FieldPtr.getRecord());
    } else if (FieldType->isIncompleteArrayType()) {
      // Nothing to do here.
    } else if (F.Decl->isUnnamedBitField()) {
      // Nothing do do here.
    } else if (FieldType->isArrayType()) {
      const auto *CAT =
          cast<ConstantArrayType>(FieldType->getAsArrayTypeUnsafe());
      Result &= CheckArrayInitialized(S, Loc, FieldPtr, CAT);
    } else if (!FieldPtr.isInitialized()) {
      DiagnoseUninitializedSubobject(S, Loc, F.Decl);
      Result = false;
    }
  }

  // Check Fields in all bases
  for (const Record::Base &B : R->bases()) {
    Pointer P = BasePtr.atField(B.Offset);
    if (!P.isInitialized()) {
      const Descriptor *Desc = BasePtr.getDeclDesc();
      if (Desc->asDecl())
        S.FFDiag(BasePtr.getDeclDesc()->asDecl()->getLocation(),
                 diag::note_constexpr_uninitialized_base)
            << B.Desc->getType();
      else
        S.FFDiag(BasePtr.getDeclDesc()->asExpr()->getExprLoc(),
                 diag::note_constexpr_uninitialized_base)
            << B.Desc->getType();

      return false;
    }
    Result &= CheckFieldsInitialized(S, Loc, P, B.R);
  }

  // TODO: Virtual bases

  return Result;
}

bool EvaluationResult::checkFullyInitialized(InterpState &S,
                                             const Pointer &Ptr) const {
  assert(Source);
  assert(empty());

  if (Ptr.isZero())
    return true;

  // We can't inspect dead pointers at all. Return true here so we can
  // diagnose them later.
  if (!Ptr.isLive())
    return true;

  SourceLocation InitLoc;
  if (const auto *D = Source.dyn_cast<const Decl *>())
    InitLoc = cast<VarDecl>(D)->getAnyInitializer()->getExprLoc();
  else if (const auto *E = Source.dyn_cast<const Expr *>())
    InitLoc = E->getExprLoc();

  if (const Record *R = Ptr.getRecord())
    return CheckFieldsInitialized(S, InitLoc, Ptr, R);

  if (const auto *CAT = dyn_cast_if_present<ConstantArrayType>(
          Ptr.getType()->getAsArrayTypeUnsafe()))
    return CheckArrayInitialized(S, InitLoc, Ptr, CAT);

  return true;
}

static void collectBlocks(const Pointer &Ptr,
                          llvm::SetVector<const Block *> &Blocks) {
  auto isUsefulPtr = [](const Pointer &P) -> bool {
    return P.isLive() && !P.isZero() && !P.isDummy() &&
           !P.isUnknownSizeArray() && !P.isOnePastEnd() && P.isBlockPointer();
  };

  if (!isUsefulPtr(Ptr))
    return;

  Blocks.insert(Ptr.block());

  const Descriptor *Desc = Ptr.getFieldDesc();
  if (!Desc)
    return;

  if (const Record *R = Desc->ElemRecord) {
    for (const Record::Field &F : R->fields()) {
      const Pointer &FieldPtr = Ptr.atField(F.Offset);
      assert(FieldPtr.block() == Ptr.block());
      collectBlocks(FieldPtr, Blocks);
    }
  } else if (Desc->isPrimitive() && Desc->getPrimType() == PT_Ptr) {
    const Pointer &Pointee = Ptr.deref<Pointer>();
    if (isUsefulPtr(Pointee) && !Blocks.contains(Pointee.block()))
      collectBlocks(Pointee, Blocks);

  } else if (Desc->isPrimitiveArray() && Desc->getPrimType() == PT_Ptr) {
    for (unsigned I = 0; I != Desc->getNumElems(); ++I) {
      const Pointer &ElemPointee = Ptr.atIndex(I).deref<Pointer>();
      if (isUsefulPtr(ElemPointee) && !Blocks.contains(ElemPointee.block()))
        collectBlocks(ElemPointee, Blocks);
    }
  } else if (Desc->isCompositeArray()) {
    for (unsigned I = 0; I != Desc->getNumElems(); ++I) {
      const Pointer &ElemPtr = Ptr.atIndex(I).narrow();
      collectBlocks(ElemPtr, Blocks);
    }
  }
}

bool EvaluationResult::checkReturnValue(InterpState &S, const Context &Ctx,
                                        const Pointer &Ptr,
                                        const SourceInfo &Info) {
  // Collect all blocks that this pointer (transitively) points to and
  // return false if any of them is a dynamic block.
  llvm::SetVector<const Block *> Blocks;

  collectBlocks(Ptr, Blocks);

  for (const Block *B : Blocks) {
    if (B->isDynamic()) {
      assert(B->getDescriptor());
      assert(B->getDescriptor()->asExpr());

      S.FFDiag(Info, diag::note_constexpr_dynamic_alloc)
          << Ptr.getType()->isReferenceType() << !Ptr.isRoot();
      S.Note(B->getDescriptor()->asExpr()->getExprLoc(),
             diag::note_constexpr_dynamic_alloc_here);
      return false;
    }
  }

  return true;
}

} // namespace interp
} // namespace clang
