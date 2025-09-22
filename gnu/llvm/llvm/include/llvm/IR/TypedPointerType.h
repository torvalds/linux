//===- llvm/IR/TypedPointerType.h - Typed Pointer Type --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains typed pointer type information. It is separated out into
// a separate file to make it less likely to accidentally use this type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TYPEDPOINTERTYPE_H
#define LLVM_IR_TYPEDPOINTERTYPE_H

#include "llvm/IR/Type.h"

namespace llvm {

/// A few GPU targets, such as DXIL and SPIR-V, have typed pointers. This
/// pointer type abstraction is used for tracking the types of these pointers.
/// It is not legal to use this type, or derived types containing this type, in
/// LLVM IR.
class TypedPointerType : public Type {
  explicit TypedPointerType(Type *ElType, unsigned AddrSpace);

  Type *PointeeTy;

public:
  TypedPointerType(const TypedPointerType &) = delete;
  TypedPointerType &operator=(const TypedPointerType &) = delete;

  /// This constructs a pointer to an object of the specified type in a numbered
  /// address space.
  static TypedPointerType *get(Type *ElementType, unsigned AddressSpace);

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Return the address space of the Pointer type.
  unsigned getAddressSpace() const { return getSubclassData(); }

  Type *getElementType() const { return PointeeTy; }

  /// Implement support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == TypedPointerTyID;
  }
};

} // namespace llvm

#endif // LLVM_IR_TYPEDPOINTERTYPE_H
