//===- TypedPointerType.cpp - Typed Pointer Type --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/TypedPointerType.h"
#include "LLVMContextImpl.h"

using namespace llvm;

TypedPointerType *TypedPointerType::get(Type *EltTy, unsigned AddressSpace) {
  assert(EltTy && "Can't get a pointer to <null> type!");
  assert(isValidElementType(EltTy) && "Invalid type for pointer element!");

  LLVMContextImpl *CImpl = EltTy->getContext().pImpl;

  // Since AddressSpace #0 is the common case, we special case it.
  TypedPointerType *&Entry =
      CImpl->ASTypedPointerTypes[std::make_pair(EltTy, AddressSpace)];

  if (!Entry)
    Entry = new (CImpl->Alloc) TypedPointerType(EltTy, AddressSpace);
  return Entry;
}

TypedPointerType::TypedPointerType(Type *E, unsigned AddrSpace)
    : Type(E->getContext(), TypedPointerTyID), PointeeTy(E) {
  ContainedTys = &PointeeTy;
  NumContainedTys = 1;
  setSubclassData(AddrSpace);
}

bool TypedPointerType::isValidElementType(Type *ElemTy) {
  return !ElemTy->isVoidTy() && !ElemTy->isLabelTy() &&
         !ElemTy->isMetadataTy() && !ElemTy->isTokenTy() &&
         !ElemTy->isX86_AMXTy();
}
