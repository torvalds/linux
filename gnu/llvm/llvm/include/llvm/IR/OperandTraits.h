//===-- llvm/OperandTraits.h - OperandTraits class definition ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the traits classes that are handy for enforcing the correct
// layout of various User subclasses. It also provides the means for accessing
// the operands in the most efficient manner.
//

#ifndef LLVM_IR_OPERANDTRAITS_H
#define LLVM_IR_OPERANDTRAITS_H

#include "llvm/IR/User.h"

namespace llvm {

//===----------------------------------------------------------------------===//
//                          FixedNumOperand Trait Class
//===----------------------------------------------------------------------===//

/// FixedNumOperandTraits - determine the allocation regime of the Use array
/// when it is a prefix to the User object, and the number of Use objects is
/// known at compile time.

template <typename SubClass, unsigned ARITY>
struct FixedNumOperandTraits {
  static Use *op_begin(SubClass* U) {
    static_assert(
        !std::is_polymorphic<SubClass>::value,
        "adding virtual methods to subclasses of User breaks use lists");
    return reinterpret_cast<Use*>(U) - ARITY;
  }
  static Use *op_end(SubClass* U) {
    return reinterpret_cast<Use*>(U);
  }
  static unsigned operands(const User*) {
    return ARITY;
  }
};

//===----------------------------------------------------------------------===//
//                          OptionalOperand Trait Class
//===----------------------------------------------------------------------===//

/// OptionalOperandTraits - when the number of operands may change at runtime.
/// Naturally it may only decrease, because the allocations may not change.

template <typename SubClass, unsigned ARITY = 1>
struct OptionalOperandTraits : public FixedNumOperandTraits<SubClass, ARITY> {
  static unsigned operands(const User *U) {
    return U->getNumOperands();
  }
};

//===----------------------------------------------------------------------===//
//                          VariadicOperand Trait Class
//===----------------------------------------------------------------------===//

/// VariadicOperandTraits - determine the allocation regime of the Use array
/// when it is a prefix to the User object, and the number of Use objects is
/// only known at allocation time.

template <typename SubClass, unsigned MINARITY = 0>
struct VariadicOperandTraits {
  static Use *op_begin(SubClass* U) {
    static_assert(
        !std::is_polymorphic<SubClass>::value,
        "adding virtual methods to subclasses of User breaks use lists");
    return reinterpret_cast<Use*>(U) - static_cast<User*>(U)->getNumOperands();
  }
  static Use *op_end(SubClass* U) {
    return reinterpret_cast<Use*>(U);
  }
  static unsigned operands(const User *U) {
    return U->getNumOperands();
  }
};

//===----------------------------------------------------------------------===//
//                          HungoffOperand Trait Class
//===----------------------------------------------------------------------===//

/// HungoffOperandTraits - determine the allocation regime of the Use array
/// when it is not a prefix to the User object, but allocated at an unrelated
/// heap address.
///
/// This is the traits class that is needed when the Use array must be
/// resizable.

template <unsigned MINARITY = 1>
struct HungoffOperandTraits {
  static Use *op_begin(User* U) {
    return U->getHungOffOperands();
  }
  static Use *op_end(User* U) {
    return U->getHungOffOperands() + U->getNumOperands();
  }
  static unsigned operands(const User *U) {
    return U->getNumOperands();
  }
};

/// Macro for generating in-class operand accessor declarations.
/// It should only be called in the public section of the interface.
///
#define DECLARE_TRANSPARENT_OPERAND_ACCESSORS(VALUECLASS) \
  public: \
  inline VALUECLASS *getOperand(unsigned) const; \
  inline void setOperand(unsigned, VALUECLASS*); \
  inline op_iterator op_begin(); \
  inline const_op_iterator op_begin() const; \
  inline op_iterator op_end(); \
  inline const_op_iterator op_end() const; \
  protected: \
  template <int> inline Use &Op(); \
  template <int> inline const Use &Op() const; \
  public: \
  inline unsigned getNumOperands() const

/// Macro for generating out-of-class operand accessor definitions
#define DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CLASS, VALUECLASS) \
CLASS::op_iterator CLASS::op_begin() { \
  return OperandTraits<CLASS>::op_begin(this); \
} \
CLASS::const_op_iterator CLASS::op_begin() const { \
  return OperandTraits<CLASS>::op_begin(const_cast<CLASS*>(this)); \
} \
CLASS::op_iterator CLASS::op_end() { \
  return OperandTraits<CLASS>::op_end(this); \
} \
CLASS::const_op_iterator CLASS::op_end() const { \
  return OperandTraits<CLASS>::op_end(const_cast<CLASS*>(this)); \
} \
VALUECLASS *CLASS::getOperand(unsigned i_nocapture) const { \
  assert(i_nocapture < OperandTraits<CLASS>::operands(this) \
         && "getOperand() out of range!"); \
  return cast_or_null<VALUECLASS>( \
    OperandTraits<CLASS>::op_begin(const_cast<CLASS*>(this))[i_nocapture].get()); \
} \
void CLASS::setOperand(unsigned i_nocapture, VALUECLASS *Val_nocapture) { \
  assert(i_nocapture < OperandTraits<CLASS>::operands(this) \
         && "setOperand() out of range!"); \
  OperandTraits<CLASS>::op_begin(this)[i_nocapture] = Val_nocapture; \
} \
unsigned CLASS::getNumOperands() const { \
  return OperandTraits<CLASS>::operands(this); \
} \
template <int Idx_nocapture> Use &CLASS::Op() { \
  return this->OpFrom<Idx_nocapture>(this); \
} \
template <int Idx_nocapture> const Use &CLASS::Op() const { \
  return this->OpFrom<Idx_nocapture>(this); \
}


} // End llvm namespace

#endif
