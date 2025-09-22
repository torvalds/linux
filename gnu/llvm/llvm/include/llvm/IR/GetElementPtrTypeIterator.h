//===- GetElementPtrTypeIterator.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an iterator for walking through the types indexed by
// getelementptr instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GETELEMENTPTRTYPEITERATOR_H
#define LLVM_IR_GETELEMENTPTRTYPEITERATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/User.h"
#include "llvm/Support/Casting.h"
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace llvm {

template <typename ItTy = User::const_op_iterator>
class generic_gep_type_iterator {

  ItTy OpIt;
  // We use two different mechanisms to store the type a GEP index applies to.
  // In some cases, we need to know the outer aggregate type the index is
  // applied within, e.g. a struct. In such cases, we store the aggregate type
  // in the iterator, and derive the element type on the fly.
  //
  // However, this is not always possible, because for the outermost index there
  // is no containing type. In such cases, or if the containing type is not
  // relevant, e.g. for arrays, the element type is stored as Type* in CurTy.
  //
  // If CurTy contains a Type* value, this does not imply anything about the
  // type itself, because it is the element type and not the outer type.
  // In particular, Type* can be a struct type.
  //
  // Consider this example:
  //
  //    %my.struct = type { i32, [ 4 x float ] }
  //    [...]
  //    %gep = getelementptr %my.struct, ptr %ptr, i32 10, i32 1, 32 3
  //
  // Iterating over the indices of this GEP, CurTy will contain the following
  // values:
  //    * i32 10: The outer index always operates on the GEP value type.
  //              CurTy contains a Type*       pointing at `%my.struct`.
  //    * i32 1:  This index is within a struct.
  //              CurTy contains a StructType* pointing at `%my.struct`.
  //    * i32 3:  This index is within an array. We reuse the "flat" indexing
  //              for arrays which is also used in the top level GEP index.
  //              CurTy contains a Type*       pointing at `float`.
  //
  // Vectors are handled separately because the layout of vectors is different
  // for overaligned elements: Vectors are always bit-packed, whereas arrays
  // respect ABI alignment of the elements.
  PointerUnion<StructType *, VectorType *, Type *> CurTy;

  generic_gep_type_iterator() = default;

public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = Type *;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

  static generic_gep_type_iterator begin(Type *Ty, ItTy It) {
    generic_gep_type_iterator I;
    I.CurTy = Ty;
    I.OpIt = It;
    return I;
  }

  static generic_gep_type_iterator end(ItTy It) {
    generic_gep_type_iterator I;
    I.OpIt = It;
    return I;
  }

  bool operator==(const generic_gep_type_iterator &x) const {
    return OpIt == x.OpIt;
  }

  bool operator!=(const generic_gep_type_iterator &x) const {
    return !operator==(x);
  }

  // FIXME: Make this the iterator's operator*() after the 4.0 release.
  // operator*() had a different meaning in earlier releases, so we're
  // temporarily not giving this iterator an operator*() to avoid a subtle
  // semantics break.
  Type *getIndexedType() const {
    if (auto *T = dyn_cast_if_present<Type *>(CurTy))
      return T;
    if (auto *VT = dyn_cast_if_present<VectorType *>(CurTy))
      return VT->getElementType();
    return cast<StructType *>(CurTy)->getTypeAtIndex(getOperand());
  }

  Value *getOperand() const { return const_cast<Value *>(&**OpIt); }

  generic_gep_type_iterator &operator++() { // Preincrement
    Type *Ty = getIndexedType();
    if (auto *ATy = dyn_cast<ArrayType>(Ty))
      CurTy = ATy->getElementType();
    else if (auto *VTy = dyn_cast<VectorType>(Ty))
      CurTy = VTy;
    else
      CurTy = dyn_cast<StructType>(Ty);
    ++OpIt;
    return *this;
  }

  generic_gep_type_iterator operator++(int) { // Postincrement
    generic_gep_type_iterator tmp = *this;
    ++*this;
    return tmp;
  }

  // All of the below API is for querying properties of the "outer type", i.e.
  // the type that contains the indexed type. Most of the time this is just
  // the type that was visited immediately prior to the indexed type, but for
  // the first element this is an unbounded array of the GEP's source element
  // type, for which there is no clearly corresponding IR type (we've
  // historically used a pointer type as the outer type in this case, but
  // pointers will soon lose their element type).
  //
  // FIXME: Most current users of this class are just interested in byte
  // offsets (a few need to know whether the outer type is a struct because
  // they are trying to replace a constant with a variable, which is only
  // legal for arrays, e.g. canReplaceOperandWithVariable in SimplifyCFG.cpp);
  // we should provide a more minimal API here that exposes not much more than
  // that.

  bool isStruct() const { return isa<StructType *>(CurTy); }
  bool isVector() const { return isa<VectorType *>(CurTy); }
  bool isSequential() const { return !isStruct(); }

  // For sequential GEP indices (all except those into structs), the index value
  // can be translated into a byte offset by multiplying with an element stride.
  // This function returns this stride, which both depends on the element type,
  // and the containing aggregate type, as vectors always tightly bit-pack their
  // elements.
  TypeSize getSequentialElementStride(const DataLayout &DL) const {
    assert(isSequential());
    Type *ElemTy = getIndexedType();
    if (isVector()) {
      assert(DL.typeSizeEqualsStoreSize(ElemTy) && "Not byte-addressable");
      return DL.getTypeStoreSize(ElemTy);
    }
    return DL.getTypeAllocSize(ElemTy);
  }

  StructType *getStructType() const { return cast<StructType *>(CurTy); }

  StructType *getStructTypeOrNull() const {
    return dyn_cast_if_present<StructType *>(CurTy);
  }
};

  using gep_type_iterator = generic_gep_type_iterator<>;

  inline gep_type_iterator gep_type_begin(const User *GEP) {
    auto *GEPOp = cast<GEPOperator>(GEP);
    return gep_type_iterator::begin(
        GEPOp->getSourceElementType(),
        GEP->op_begin() + 1);
  }

  inline gep_type_iterator gep_type_end(const User *GEP) {
    return gep_type_iterator::end(GEP->op_end());
  }

  inline gep_type_iterator gep_type_begin(const User &GEP) {
    auto &GEPOp = cast<GEPOperator>(GEP);
    return gep_type_iterator::begin(
        GEPOp.getSourceElementType(),
        GEP.op_begin() + 1);
  }

  inline gep_type_iterator gep_type_end(const User &GEP) {
    return gep_type_iterator::end(GEP.op_end());
  }

  template<typename T>
  inline generic_gep_type_iterator<const T *>
  gep_type_begin(Type *Op0, ArrayRef<T> A) {
    return generic_gep_type_iterator<const T *>::begin(Op0, A.begin());
  }

  template<typename T>
  inline generic_gep_type_iterator<const T *>
  gep_type_end(Type * /*Op0*/, ArrayRef<T> A) {
    return generic_gep_type_iterator<const T *>::end(A.end());
  }

} // end namespace llvm

#endif // LLVM_IR_GETELEMENTPTRTYPEITERATOR_H
