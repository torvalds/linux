//===- AbstractCallSite.h - Abstract call sites -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the AbstractCallSite class, which is a is a wrapper that
// allows treating direct, indirect, and callback calls the same.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ABSTRACTCALLSITE_H
#define LLVM_IR_ABSTRACTCALLSITE_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Value.h"
#include <cassert>

namespace llvm {

class Argument;
class Use;

/// AbstractCallSite
///
/// An abstract call site is a wrapper that allows to treat direct,
/// indirect, and callback calls the same. If an abstract call site
/// represents a direct or indirect call site it behaves like a stripped
/// down version of a normal call site object. The abstract call site can
/// also represent a callback call, thus the fact that the initially
/// called function (=broker) may invoke a third one (=callback callee).
/// In this case, the abstract call site hides the middle man, hence the
/// broker function. The result is a representation of the callback call,
/// inside the broker, but in the context of the original call to the broker.
///
/// There are up to three functions involved when we talk about callback call
/// sites. The caller (1), which invokes the broker function. The broker
/// function (2), that will invoke the callee zero or more times. And finally
/// the callee (3), which is the target of the callback call.
///
/// The abstract call site will handle the mapping from parameters to arguments
/// depending on the semantic of the broker function. However, it is important
/// to note that the mapping is often partial. Thus, some arguments of the
/// call/invoke instruction are mapped to parameters of the callee while others
/// are not.
class AbstractCallSite {
public:

  /// The encoding of a callback with regards to the underlying instruction.
  struct CallbackInfo {

    /// For direct/indirect calls the parameter encoding is empty. If it is not,
    /// the abstract call site represents a callback. In that case, the first
    /// element of the encoding vector represents which argument of the call
    /// site CB is the callback callee. The remaining elements map parameters
    /// (identified by their position) to the arguments that will be passed
    /// through (also identified by position but in the call site instruction).
    ///
    /// NOTE that we use LLVM argument numbers (starting at 0) and not
    /// clang/source argument numbers (starting at 1). The -1 entries represent
    /// unknown values that are passed to the callee.
    using ParameterEncodingTy = SmallVector<int, 0>;
    ParameterEncodingTy ParameterEncoding;

  };

private:

  /// The underlying call site:
  ///   caller -> callee,             if this is a direct or indirect call site
  ///   caller -> broker function,    if this is a callback call site
  CallBase *CB;

  /// The encoding of a callback with regards to the underlying instruction.
  CallbackInfo CI;

public:
  /// Sole constructor for abstract call sites (ACS).
  ///
  /// An abstract call site can only be constructed through a llvm::Use because
  /// each operand (=use) of an instruction could potentially be a different
  /// abstract call site. Furthermore, even if the value of the llvm::Use is the
  /// same, and the user is as well, the abstract call sites might not be.
  ///
  /// If a use is not associated with an abstract call site the constructed ACS
  /// will evaluate to false if converted to a boolean.
  ///
  /// If the use is the callee use of a call or invoke instruction, the
  /// constructed abstract call site will behave as a llvm::CallSite would.
  ///
  /// If the use is not a callee use of a call or invoke instruction, the
  /// callback metadata is used to determine the argument <-> parameter mapping
  /// as well as the callee of the abstract call site.
  AbstractCallSite(const Use *U);

  /// Add operand uses of \p CB that represent callback uses into
  /// \p CallbackUses.
  ///
  /// All uses added to \p CallbackUses can be used to create abstract call
  /// sites for which AbstractCallSite::isCallbackCall() will return true.
  static void getCallbackUses(const CallBase &CB,
                              SmallVectorImpl<const Use *> &CallbackUses);

  /// Conversion operator to conveniently check for a valid/initialized ACS.
  explicit operator bool() const { return CB != nullptr; }

  /// Return the underlying instruction.
  CallBase *getInstruction() const { return CB; }

  /// Return true if this ACS represents a direct call.
  bool isDirectCall() const {
    return !isCallbackCall() && !CB->isIndirectCall();
  }

  /// Return true if this ACS represents an indirect call.
  bool isIndirectCall() const {
    return !isCallbackCall() && CB->isIndirectCall();
  }

  /// Return true if this ACS represents a callback call.
  bool isCallbackCall() const {
    // For a callback call site the callee is ALWAYS stored first in the
    // transitive values vector. Thus, a non-empty vector indicates a callback.
    return !CI.ParameterEncoding.empty();
  }

  /// Return true if @p UI is the use that defines the callee of this ACS.
  bool isCallee(Value::const_user_iterator UI) const {
    return isCallee(&UI.getUse());
  }

  /// Return true if @p U is the use that defines the callee of this ACS.
  bool isCallee(const Use *U) const {
    if (isDirectCall())
      return CB->isCallee(U);

    assert(!CI.ParameterEncoding.empty() &&
           "Callback without parameter encoding!");

    // If the use is actually in a constant cast expression which itself
    // has only one use, we look through the constant cast expression.
    if (auto *CE = dyn_cast<ConstantExpr>(U->getUser()))
      if (CE->hasOneUse() && CE->isCast())
        U = &*CE->use_begin();

    return (int)CB->getArgOperandNo(U) == CI.ParameterEncoding[0];
  }

  /// Return the number of parameters of the callee.
  unsigned getNumArgOperands() const {
    if (isDirectCall())
      return CB->arg_size();
    // Subtract 1 for the callee encoding.
    return CI.ParameterEncoding.size() - 1;
  }

  /// Return the operand index of the underlying instruction associated with @p
  /// Arg.
  int getCallArgOperandNo(Argument &Arg) const {
    return getCallArgOperandNo(Arg.getArgNo());
  }

  /// Return the operand index of the underlying instruction associated with
  /// the function parameter number @p ArgNo or -1 if there is none.
  int getCallArgOperandNo(unsigned ArgNo) const {
    if (isDirectCall())
      return ArgNo;
    // Add 1 for the callee encoding.
    return CI.ParameterEncoding[ArgNo + 1];
  }

  /// Return the operand of the underlying instruction associated with @p Arg.
  Value *getCallArgOperand(Argument &Arg) const {
    return getCallArgOperand(Arg.getArgNo());
  }

  /// Return the operand of the underlying instruction associated with the
  /// function parameter number @p ArgNo or nullptr if there is none.
  Value *getCallArgOperand(unsigned ArgNo) const {
    if (isDirectCall())
      return CB->getArgOperand(ArgNo);
    // Add 1 for the callee encoding.
    return CI.ParameterEncoding[ArgNo + 1] >= 0
               ? CB->getArgOperand(CI.ParameterEncoding[ArgNo + 1])
               : nullptr;
  }

  /// Return the operand index of the underlying instruction associated with the
  /// callee of this ACS. Only valid for callback calls!
  int getCallArgOperandNoForCallee() const {
    assert(isCallbackCall());
    assert(CI.ParameterEncoding.size() && CI.ParameterEncoding[0] >= 0);
    return CI.ParameterEncoding[0];
  }

  /// Return the use of the callee value in the underlying instruction. Only
  /// valid for callback calls!
  const Use &getCalleeUseForCallback() const {
    int CalleeArgIdx = getCallArgOperandNoForCallee();
    assert(CalleeArgIdx >= 0 &&
           unsigned(CalleeArgIdx) < getInstruction()->getNumOperands());
    return getInstruction()->getOperandUse(CalleeArgIdx);
  }

  /// Return the pointer to function that is being called.
  Value *getCalledOperand() const {
    if (isDirectCall())
      return CB->getCalledOperand();
    return CB->getArgOperand(getCallArgOperandNoForCallee());
  }

  /// Return the function being called if this is a direct call, otherwise
  /// return null (if it's an indirect call).
  Function *getCalledFunction() const {
    Value *V = getCalledOperand();
    return V ? dyn_cast<Function>(V->stripPointerCasts()) : nullptr;
  }
};

/// Apply function Func to each CB's callback call site.
template <typename UnaryFunction>
void forEachCallbackCallSite(const CallBase &CB, UnaryFunction Func) {
  SmallVector<const Use *, 4u> CallbackUses;
  AbstractCallSite::getCallbackUses(CB, CallbackUses);
  for (const Use *U : CallbackUses) {
    AbstractCallSite ACS(U);
    assert(ACS && ACS.isCallbackCall() && "must be a callback call");
    Func(ACS);
  }
}

/// Apply function Func to each CB's callback function.
template <typename UnaryFunction>
void forEachCallbackFunction(const CallBase &CB, UnaryFunction Func) {
  forEachCallbackCallSite(CB, [&Func](AbstractCallSite &ACS) {
    if (Function *Callback = ACS.getCalledFunction())
      Func(Callback);
  });
}

} // end namespace llvm

#endif // LLVM_IR_ABSTRACTCALLSITE_H
