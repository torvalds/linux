//===-- llvm/Argument.h - Definition of the Argument class ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Argument class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ARGUMENT_H
#define LLVM_IR_ARGUMENT_H

#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Value.h"
#include <optional>

namespace llvm {

class ConstantRange;

/// This class represents an incoming formal argument to a Function. A formal
/// argument, since it is ``formal'', does not contain an actual value but
/// instead represents the type, argument number, and attributes of an argument
/// for a specific function. When used in the body of said function, the
/// argument of course represents the value of the actual argument that the
/// function was called with.
class Argument final : public Value {
  Function *Parent;
  unsigned ArgNo;

  friend class Function;
  void setParent(Function *parent);

public:
  /// Argument constructor.
  explicit Argument(Type *Ty, const Twine &Name = "", Function *F = nullptr,
                    unsigned ArgNo = 0);

  inline const Function *getParent() const { return Parent; }
  inline       Function *getParent()       { return Parent; }

  /// Return the index of this formal argument in its containing function.
  ///
  /// For example in "void foo(int a, float b)" a is 0 and b is 1.
  unsigned getArgNo() const {
    assert(Parent && "can't get number of unparented arg");
    return ArgNo;
  }

  /// Return true if this argument has the nonnull attribute. Also returns true
  /// if at least one byte is known to be dereferenceable and the pointer is in
  /// addrspace(0).
  /// If AllowUndefOrPoison is true, respect the semantics of nonnull attribute
  /// and return true even if the argument can be undef or poison.
  bool hasNonNullAttr(bool AllowUndefOrPoison = true) const;

  /// If this argument has the dereferenceable attribute, return the number of
  /// bytes known to be dereferenceable. Otherwise, zero is returned.
  uint64_t getDereferenceableBytes() const;

  /// If this argument has the dereferenceable_or_null attribute, return the
  /// number of bytes known to be dereferenceable. Otherwise, zero is returned.
  uint64_t getDereferenceableOrNullBytes() const;

  /// If this argument has nofpclass attribute, return the mask representing
  /// disallowed floating-point values. Otherwise, fcNone is returned.
  FPClassTest getNoFPClass() const;

  /// If this argument has a range attribute, return the value range of the
  /// argument. Otherwise, std::nullopt is returned.
  std::optional<ConstantRange> getRange() const;

  /// Return true if this argument has the byval attribute.
  bool hasByValAttr() const;

  /// Return true if this argument has the byref attribute.
  bool hasByRefAttr() const;

  /// Return true if this argument has the swiftself attribute.
  bool hasSwiftSelfAttr() const;

  /// Return true if this argument has the swifterror attribute.
  bool hasSwiftErrorAttr() const;

  /// Return true if this argument has the byval, inalloca, or preallocated
  /// attribute. These attributes represent arguments being passed by value,
  /// with an associated copy between the caller and callee
  bool hasPassPointeeByValueCopyAttr() const;

  /// If this argument satisfies has hasPassPointeeByValueAttr, return the
  /// in-memory ABI size copied to the stack for the call. Otherwise, return 0.
  uint64_t getPassPointeeByValueCopySize(const DataLayout &DL) const;

  /// Return true if this argument has the byval, sret, inalloca, preallocated,
  /// or byref attribute. These attributes represent arguments being passed by
  /// value (which may or may not involve a stack copy)
  bool hasPointeeInMemoryValueAttr() const;

  /// If hasPointeeInMemoryValueAttr returns true, the in-memory ABI type is
  /// returned. Otherwise, nullptr.
  Type *getPointeeInMemoryValueType() const;

  /// If this is a byval or inalloca argument, return its alignment.
  /// FIXME: Remove this function once transition to Align is over.
  /// Use getParamAlign() instead.
  LLVM_DEPRECATED("Use getParamAlign() instead", "getParamAlign")
  uint64_t getParamAlignment() const;

  /// If this is a byval or inalloca argument, return its alignment.
  MaybeAlign getParamAlign() const;

  MaybeAlign getParamStackAlign() const;

  /// If this is a byval argument, return its type.
  Type *getParamByValType() const;

  /// If this is an sret argument, return its type.
  Type *getParamStructRetType() const;

  /// If this is a byref argument, return its type.
  Type *getParamByRefType() const;

  /// If this is an inalloca argument, return its type.
  Type *getParamInAllocaType() const;

  /// Return true if this argument has the nest attribute.
  bool hasNestAttr() const;

  /// Return true if this argument has the noalias attribute.
  bool hasNoAliasAttr() const;

  /// Return true if this argument has the nocapture attribute.
  bool hasNoCaptureAttr() const;

  /// Return true if this argument has the nofree attribute.
  bool hasNoFreeAttr() const;

  /// Return true if this argument has the sret attribute.
  bool hasStructRetAttr() const;

  /// Return true if this argument has the inreg attribute.
  bool hasInRegAttr() const;

  /// Return true if this argument has the returned attribute.
  bool hasReturnedAttr() const;

  /// Return true if this argument has the readonly or readnone attribute.
  bool onlyReadsMemory() const;

  /// Return true if this argument has the inalloca attribute.
  bool hasInAllocaAttr() const;

  /// Return true if this argument has the preallocated attribute.
  bool hasPreallocatedAttr() const;

  /// Return true if this argument has the zext attribute.
  bool hasZExtAttr() const;

  /// Return true if this argument has the sext attribute.
  bool hasSExtAttr() const;

  /// Add attributes to an argument.
  void addAttrs(AttrBuilder &B);

  void addAttr(Attribute::AttrKind Kind);

  void addAttr(Attribute Attr);

  /// Remove attributes from an argument.
  void removeAttr(Attribute::AttrKind Kind);

  void removeAttrs(const AttributeMask &AM);

  /// Check if an argument has a given attribute.
  bool hasAttribute(Attribute::AttrKind Kind) const;

  Attribute getAttribute(Attribute::AttrKind Kind) const;

  /// Method for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Value *V) {
    return V->getValueID() == ArgumentVal;
  }
};

} // End llvm namespace

#endif
